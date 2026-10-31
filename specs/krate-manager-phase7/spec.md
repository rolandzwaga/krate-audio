# Feature Specification: Krate Manager Phase 7 — Download and Verify

**Spec slug:** `krate-manager-phase7`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 7 (lines 599-630); Core Philosophy "Verify before anything touches
the asset" (lines 69-70); Feed and artifact model (lines 141-148: per-tag URLs, no `api.github.com`, one minisign key);
Key decision 4 (lines 174-176: one minisign key, never in the repo); Repository Layout (lines 239-240:
`tools/feed-contract/keys/krate-signing.pub`, "Phase 7 embeds it"); Phase 1 C3 and the known-answer fixture (lines
284-295); Phase 2 signing tooling (lines 356-372: "Phase 7 checks the Rust path against the same fixture"); Dependency
Graph edges 1→7, 4→7, 5→7, 7→8, 7→10, 7→11 (lines 952-953); Cross-Cutting Constraints (lines 980-1021).
**Depends on:** Phase 1 (C1 `platforms` entry, C3 grammar, the rsign2 known-answer fixture), Phase 2 (FR-061: the
committed release public key), Phase 4 (`Roots::staging_dir`, `AppContext`, `test_support`, the capability file,
`manager-ci.yml`), Phase 5 (`http` transport and policy, the feed model `PlatformAsset` / `AssetUrl` / `AssetName`,
`version::resolve_native`, `contract::parse_minisig_text` / `parse_trusted_comment`, the fixture server), Phase 6
(`HostFacts::os_version`, the command pattern in `src-tauri/src/commands.rs`).
**Consumed by:** Phase 8 (extracts only a `VerifiedArtifact`, edge 7→8), Phase 10 (the helper runs only a verified
installer asset, edge 7→10), Phase 11 (the shared verification code and key, edge 7→11), Phase 12 (bounded parallel
downloads, the queue).
**Status:** DRAFT, revision 3 (revision 2 plus the six clarification decisions of 2026-10-31; see "Clarifications" and
"Review notes" at the end): specification only, no implementation. Amended 2026-10-31 by tasks T001 with plan §0.2's
SI-1 to SI-30 and tasks TA-5 and TA-13; every changed sentence carries its `(plan SI-n)` or `(tasks TA-n)` tag.
**Date:** 2026-10-31 · verified against `feat/krate-manager` at `2f3c93d7` ("fix(workflows): krate-manager tasks re-lock
the clippy-guard fixture; no diagram tick", `git rev-parse --short HEAD`, clean tree); `specs/krate-manager-phase7/` did
not exist before this file.

---

## Overview

Phase 7 turns a feed entry into a file the later phases may trust. `core::download` resolves the entry's native asset
(Phase 5 `version::resolve_native`), streams it over HTTPS into the injected staging directory through the one Phase 5
transport in `http.rs` (same URL policy, proxy and PAC routing and redirect rule, a download-specific timeout model),
reports progress through a plain callback, resumes an interrupted transfer with a `Range` request and falls back to a
full download when the server ignores it, and retries a bounded number of times. `core::verify` then reads the staged
file once, streamed, and checks, in a fixed order, its size against the feed `size`, its SHA-256 against the feed
`sha256`, its minisign signature against the release public key compiled in from
`tools/feed-contract/keys/krate-signing.pub` (injected through `AppContext`, so tests use a throwaway key), and the
signature's trusted comment against the Phase 1 C3 grammar and the entry's plugin name, version, platform key and asset
file name. Only a file that passes every check becomes a `VerifiedArtifact`, a type with no public constructor; it is
the only input Phases 8 and 10 accept. `download::obtain_and` hands the owned artifact to its continuation after
verification and never before; the continuation may simply return it, so Phase 10 can collect the verified artifacts of
a batch and build one helper job from them. The guarantee that nothing unverified reaches Phases 8 and 10 rests on the
type, not on where the call happens. `src-tauri` adds one command, `download_plugin`, a thin wrapper over the core
function `download::for_command`; it takes a feed key, the version the status screen showed (`expected_version`, refused with
`E_DOWNLOAD_VERSION_CHANGED` when the re-fetched feed offers another) and a Tauri `Channel`, and the core adapter
`download::channel_progress` forwards the progress events to it. The API carries a `CancelToken` (no command sets it in
this phase) and `download::discard` releases a verified artifact after its install. Nothing is extracted, placed, executed or elevated in
this phase.

---

## Scope

**In scope (roadmap Phase 7 components, lines 603-617):**

1. `core::download`: streamed HTTPS download into the staging dir; progress through a plain callback that `core` tests
   observe directly; `src-tauri` adapts it to a Tauri `Channel`; resume with a `Range` request; bounded retries (lines
   604-608; D2 to D5, D8, D9).
2. `core::verify`: `Content-Length` and file size vs feed `size`, streamed SHA-256 vs feed `sha256`, the minisign
   signature vs the embedded public key (the `minisign-verify` crate, version confirmed below), and the C3 trusted-comment
   binding to the feed entry; a verified artifact is marked so and nothing else is ever extracted or executed (lines
   609-614; D1, D6, D7).
3. Test keys and signatures made per test run with the `minisign` crate as a dev-dependency, never committed (lines
   615-617; D1).

**Non-goals (owned elsewhere):**

| Not in Phase 7 | Owner |
|---|---|
| Extracting anything, the C4 listing check of an extracted tree, the Linux `payload` comparison | Phase 8 (roadmap lines 632-657) |
| Placing files, manifests, the journaled swap, Install / Update / Remove buttons or any UI that starts a download | Phase 9 (lines 659-701), Phase 10 (lines 703-804), Phase 12 (lines 873-924) |
| Running an installer, the helper's copy-and-rehash into an admin-only directory | Phase 10 (lines 733-735) |
| "Update all", the download queue, the bound on parallel downloads, a cancel command that sets a `CancelToken` | Phase 12 (lines 878-882); Phase 7 provides only the token parameter (FR-017) |
| Building one helper job from several verified artifacts (one elevation prompt per batch); how long a verified artifact stays in staging while a batch waits, including across a feed change that stops offering it | Phase 10 (lines 746-752), Phase 12 (lines 878-882); Phase 7 only guarantees that its own prune never removes an artifact the feed it is given still offers (FR-005, SC-026), and provides `download::discard` for them to release an artifact after a successful install (FR-049, SC-035) |
| The manager's own update download and its Tauri `.sig` format | Phase 11 (lines 806-871; CONTRACT.md section 5: "the manager's own updates keep Tauri's format") |
| Deciding whether an older offered version may be installed | Phase 6 `version::compare` already never calls it an update (`version.rs:83-94`); the install phases act on that state |
| A beta channel; downloading a `prerelease` sibling | Not in v1 (Q9, roadmap line 974); Phase 5 leaves it out of the model |
| In-app Sigstore / provenance verification | Not in v1 (roadmap line 971) |
| Any change to `plugins/`, `dsp/`, `cmake/`, `extern/`, `tools/feed-contract/` contracts, schemas or fixtures, `release.yml`, `feed.yml`, `ci.yml` | Never in this phase (FR-091) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 7 does with it |
|---|---|---|
| `specs/Krate-manager-roadmap.md:599-630` | Phase 7 components, measurable outcomes, "Does NOT" | The source of every FR / SC (Traceability) |
| `apps/krate-manager/crates/core/src/http.rs:22-24` | `CONNECT_TIMEOUT` 10 s, `TOTAL_TIMEOUT` 30 s, `MAX_REDIRECTS` 5 | ✅ `CONNECT_TIMEOUT` and `MAX_REDIRECTS` reused. `TOTAL_TIMEOUT` is not the download bound (D2) |
| `http.rs:28-42`, `:276-303` | `UrlPolicy::{HttpsOnly, LoopbackOnly}`; `HttpConfig { policy, connect_timeout, total_timeout, proxy, pac, pac_host }`; `check_url` | ✅ Every download request and hop passes `check_url` under `ctx.http.policy` (FR-011) |
| `http.rs:219-251` | `HttpError::{Url, Redirect, Status, TooLarge, Offline, Timeout, Tls, Body}` + `code()` (`E_FEED_*`); `Body { detail: String }` carries no `io::ErrorKind`, and `Timeout` carries no `ureq::Timeout` | ✅ The enum is unchanged. The streaming function returns a separate `StreamError` that wraps it and adds the sink, stall and head-refusal cases (FR-010); `download` maps each to an `E_DOWNLOAD_*` code (FR-016) |
| `http.rs:345-382` | `Hop::{Done, Follow}`, `next_hop` (301/302/303/307/308, `Location` joined and re-checked) | ✅ The download's redirect rule, unchanged (FR-011) |
| `http.rs:622-650` | `AgentPurpose::{Feed, PacFetch}`; `agent_config`: `max_redirects(0)`, `http_status_as_error(false)`, `RootCerts::PlatformVerifier`, `timeout_connect`, `timeout_global(total_timeout)`, `user_agent("krate-manager/<ver>")`, `accept("application/json")` | 🔶 Gains `AgentPurpose::Download { stall: Duration }`, the stall bound inside the purpose, with no new parameter (FR-010, FR-011) (plan SI-6): for `Download`, `accept("application/octet-stream")`, `accept_encoding("identity")`, `timeout_per_read(stall)`; `timeout_global(config.total_timeout)` is unchanged, and `walk` already sets `total_timeout` to the remaining deadline per hop (`http.rs:1044-1051`) |
| `http.rs:665-681` | `map_error(ureq::Error, Phase)`: every `E::Timeout(_)` becomes the field-less `HttpError::Timeout` | ✅ Reused unchanged; `get_streamed` classifies `Timeout::PerRead` as `StreamError::Stalled` *before* calling it (FR-010) |
| `http.rs:737`, `:891-897` | `open_pac(config, pac, deadline)`; `pac_route(config, state, hop, deadline, static_proxy)` | ✅ `get_streamed` passes them the feed bound `start + min(config.total_timeout, limits.deadline)`, not the download deadline (FR-011) |
| `http.rs:935-971`, `:986-1027`, `:1030-1071`, `:1074-1089` | `get` (URL parse, `check_url`, static proxy, PAC state, `run_chain`); `run_chain` (route → `walk` → `next_hop`, final status must be 200); `walk` (builds a fresh `HttpConfig` per route entry and calls `agent_config(&hop_config, proxy, purpose)`); `read_body` (`take(max_bytes + 1)` into a `Vec`) | 🔶 `run_chain` and `walk` keep their parameters; `walk` is generic over a private `ChainError`, and `run_chain`'s redirect loop becomes `final_response<E>`, which `get_streamed` uses to accept 200 and 206 and hand the body to a sink instead of `read_body` (plan SI-6); `get`'s behaviour and its tests are unchanged (FR-010 to FR-012) |
| `apps/krate-manager/crates/core/tests/injection.rs:221-246`, `:251-268` | `CONFINED_TOKENS` (`ureq`, `rustls` only in `http.rs` / `http_tests.rs`); `CONFINED_TOML_KEYS` | ✅ `ureq` stays confined to `http.rs` (this is why the streaming transport is in `http.rs`, D2). 🔶 Gains `minisign_verify`, `sha2`, `minisign` rows (FR-082) |
| `injection.rs:101-113`, `:2624-2635` | `FR007_DEPENDENCIES` (11 names); `core_dependencies_are_the_fr007_set` also asserts `[dev-dependencies]` is exactly `{flate2}` | 🔶 13 names (`minisign-verify`, `sha2` added); dev-dependencies `{flate2, minisign}` (FR-080) |
| `injection.rs:279`, `:1635-1665` | `NEWTYPES` and the feed.rs constructor-seam scan | ✅ Unchanged; 🆕 a sibling scan for `VerifiedArtifact` in `verify.rs` (FR-043) |
| `crates/core/src/feed.rs:44-52`, `:54-62`, `:64-74`, `:89-119` | `PluginEntry { name, version_text, version, download_url, release }`; `StableRelease { …, platforms, install }`; `PlatformAsset { url: AssetUrl, asset_name: AssetName, size: u64, sha256: [u8; 32], signature: String, format, min_os, payload }`; `PlatformKey` + `as_str()` | ✅ Every download input comes from these (ROSTER); nothing new in the model |
| `feed.rs:155-171`, `:191-214` | `AssetUrl` (C1 url rule, no `/releases/latest/`); `AssetName::from_url` (last URL segment, `^[A-Za-z0-9][A-Za-z0-9._-]*$`, not `.`/`..`, no reserved stem) | ✅ The download URL and the staged file name; the asset name is the C3 binding field (CONTRACT.md reader rule 4) |
| `feed.rs:167-169`, `:218-222` | `AssetUrl::checked` is the only constructor and admits only `is_c1_url` (the `https://` prefix is required) | ✅ Why a contract-valid feed can never point at the plain-HTTP fixture server, and so why tests need the FR-085 fetch-URL seam |
| `contract.rs:868-890` | `signature_findings`: at parse time every defined platform key's `signature` passes `parse_minisig_text` and `parse_trusted_comment` (C3), but no binding to the entry is checked | ✅ A parsed `Feed` never carries a C3-invalid trusted comment (FR-037: step 7 is defence in depth through `obtain_and`), while a valid comment bound to another plugin, version, platform or asset does reach step 8 |
| `feed.rs:589-594`, `:603-614`, `:741` | `FetchMode`; `FeedSnapshot`; `FeedCheck`; `check(ctx, mode, now_unix)` | ✅ The command takes the entry from `feed::check(ctx, FetchMode::Normal, now)` (FR-062) |
| `crates/core/src/contract.rs:11-14` | `include_str!("../../../../../tools/feed-contract/versions.v2.schema.json")` | ✅ The embedding pattern `verify.rs` follows for the public key (FR-040) |
| `contract.rs:78-95`, `:167-219`, `:223-263` | `MinisigText`; `TrustedComment { name, version, platform, asset }`; `parse_minisig_text` (4 lines, canonical base64, 74-byte `ED` blob, 64-byte global); `parse_trusted_comment` (C3, `E_TC_FIELDS` / `E_TC_NAME` / `E_TC_VERSION` / `E_TC_PLATFORM` / `E_TC_ASSET`) | ✅ Verification re-runs both on the feed value (FR-021, FR-026) |
| `crates/core/src/version.rs:185-197`, `:211-240`, `:83-94` | `Availability::{Available { key, asset }, NotThroughManager, NotForThisMachine, NeedsNewerOs }`; `resolve`, `resolve_native(entry, running)`; `compare` ("An older available version is never an update") | ✅ The request is built from `Available` only (FR-001) |
| `crates/core/src/roots.rs:6`, `:103-105`, `:195-200` | `STAGING_DIR = "staging"`; `Roots::new` requires `staging_dir == state_dir/staging`; `state_dir()`, `staging_dir()` | ✅ The only directory Phase 7 writes (FR-003) |
| `crates/core/src/context.rs:49-56`, `:69-75`, `:79-111` | `AppContext { roots, settings, feed_url, http }` ("later phases add fields here"); `assemble_context(roots, feed_url, http)`; `assemble_with_os` | 🔶 `AppContext` gains `signing_key: SigningKey`; `assemble_context` and `assemble_with_os` gain the parameter (FR-041) |
| `crates/core/src/settings.rs:270-306` | `SettingsIo` trait and `save_with(io, roots, settings)`: each I/O step behind the trait so a test injects a failure | ✅ The precedent for `StagingIo` (D11, FR-050) |
| `crates/core/src/state_lock.rs:53-75` | One process per state dir (`try_lock` on `manager.lock`) | ✅ Why staging needs no cross-process lock; in-process exclusion only (D10) |
| `crates/core/src/fixture_server.rs:16-49`, `:53-57`, `:99-104`, `:115-118`, `:157`, `:165` | `Reply::{Ok, Status, Redirect, Truncated, Stall, Close, Garbage, Gzip, …, HeadersThenStall, Delayed}`; `RecordedRequest { method, path, headers }`; route by exact path, 404 otherwise; `start()` binds `127.0.0.1:0`; `set_route`; `requests()`; `url(path)` is `http://{addr}{path}` (`:147-150`); `Garbage(bytes)` sends the bytes as given (`:310-312`); a TLS hello gets a plain-HTTP answer, so an `https://127.0.0.1:<port>/` request ends `E_FEED_TLS` (`http_tests.rs:1277-1283`) | 🔶 Gains `Reply::Ranged { body }` and `Reply::Script(Vec<Reply>)` (FR-084); malformed heads use the existing `Garbage` |
| `crates/core/src/lib.rs:22-23`; `src-tauri/Cargo.toml:33-34` | `#[cfg(any(test, feature = "test-support"))] pub mod test_support;`; src-tauri's `[dev-dependencies]` enable core's `test-support` | ✅ So `test_support.rs` is compiled as part of core's *lib* (without core's dev-dependencies) whenever src-tauri's test targets build: it must never use `minisign` (FR-083 puts `TestSigner` in a `#[cfg(test)]` module) |
| `crates/core/src/test_support.rs:132-136`, `:178-197`, `:199-235` | `fresh_temp_roots()` → `(TempDir, Roots)`; `install_capture` / `take_mine` (this thread's log records); `FakeHostFacts` (records `os_version` calls) | ✅ Every Phase 7 test stages under `fresh_temp_roots`; SC-030 reads logs through the capture; SC-028 runs the command flow on `FakeHostFacts` |
| `crates/core/src/detect.rs:181-195` | `trait HostFacts { os_version, … }`; `PlatformFacts { io }` | ✅ The command reads the running OS version for `resolve_native` (FR-062) |
| `crates/core/Cargo.toml:15-31` | `[dependencies]` (11 keys); `[dev-dependencies] flate2` | 🔶 FR-080 |
| `apps/krate-manager/Cargo.toml:13-41` | `[workspace.dependencies]` pins | 🔶 `minisign-verify`, `sha2`, `minisign` pins (FR-080) |
| `apps/krate-manager/Cargo.lock:1959-1977` | `krate-manager-core`'s lock entry lists normal and dev dependencies together (the dev-only `flate2` sits beside `ureq`); the lock records no dependency kind | ✅ Why FR-081 walks the lock graph only from the manifest's normal roots |
| `apps/krate-manager/src-tauri/src/commands.rs:21-44` | `plugin_status` / `check_now`: async, clone `AppContext` out of `SharedState`, drop the guard, `spawn_blocking` over the core function `status::build(&ctx, mode, now_unix, &PlatformFacts { io: &OsHostIo })` (`status.rs:176`) | ✅ The pattern `download_plugin` follows: a thin wrapper over `download::for_command` (FR-060) |
| `src-tauri/Cargo.toml:10-19` | `[lib] test = false, doctest = false`; `[[bin]] test = false` | ✅ No unit test in `src-tauri` runs, so every testable piece of the command (the flow and the `Channel` adapter) lives in core (FR-060, SC-027, SC-028) |
| `src-tauri/src/lib.rs:50-56`, `:86-92` | `HttpConfig::production()…`; `assemble_context(roots, &feed_url, http)`; `generate_handler![…]` (five commands) | 🔶 Builds `SigningKey::release()` and passes it; six handlers (FR-041, FR-061) |
| `src-tauri/build.rs:3-9`, `src-tauri/capabilities/main.json` | Five commands; five `allow-*` permissions | 🔶 Six each (FR-061) |
| `crates/core/tests/config_guard.rs` (Phase 6 FR-063) | Capabilities exactly `main.json` with the listed permissions; `build.rs` registers exactly the listed commands | 🔶 Six commands and six permissions (FR-061) |
| `config_guard.rs:831-838`, `:1704-1712`, `:46-61` | `assemble_context(` in `src-tauri/src/lib.rs` must have exactly three top-level arguments; the plant rewrites the literal `assemble_context(roots, &feed_url, http)`; `LIB_RS_STRINGS` (14 strings) does not pin `SigningKey::release()` | 🔶 Four arguments expected; the plant rewrites `assemble_context(roots, &feed_url, http, SigningKey::release())` to the three-argument form and expects "with 3 top-level arguments"; `LIB_RS_STRINGS` gains `"SigningKey::release()"` (15); a new check pins `generate_handler!` to the six commands (FR-041, FR-061) (plan SI-2) |
| `tools/feed-contract/keys/krate-signing.pub` (2 lines; committed by the user in `4f4c4175`) | `untrusted comment: minisign public key: 9FEB36F9114273BC` + base64 of 42 bytes | ✅ Compiled in as the release key (FR-040) |
| `tools/feed-contract/keys/README.md` section 1, section 2 | The file is the base64-decoded Tauri `.pub`; never a fixture key (`kat/kat.pub`, `phase2/kat-secret/kat.pub`) | ✅ FR-042's test-key refusal mirrors Phase 2 plan PD-23 |
| `tools/feed-contract/CONTRACT.md:60-66`, reader rule 4 (`:125-126`), section 5 (`:198-256`), section 8 (`:508-522`) | Platform entry fields and who reads them ("Phase 7 downloads it", "checks the downloaded size", "verifies before anything is extracted or executed", "verifies the signature and the C3 binding"); asset name = last URL segment; C3 grammar, binding rule (`E_TC_BINDING` names the first differing field), `.minisig` text form, KAT; JSON fixture signatures are zero-filled and never verify cryptographically | ✅ FR-020 to FR-028; why Phase 7 tests sign their own fixtures (FR-083) |
| `tools/feed-contract/fixtures/kat/` (`KatPlugin-1.2.3-Linux-x64.tar.gz` 452 B, `.minisig`, `.sha256`, `kat.pub` key id `440FA75168B852F7`) | The rsign2 known-answer fixture, trusted comment `krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz` | ✅ Read in place (FR-085) |
| `tools/feed-contract/fixtures/phase2/kat-secret/` (`kat.pub` key id `63861317F7D60239`, `message.txt` 25 B, `message.txt.minisig`, README) | A second rsign2 signature, trusted comment `krate:1:KatPlugin:1.2.3:linux-x86_64:message.txt`; a test key whose secret half is committed on purpose | ✅ A second known answer (FR-085); refused as a release key (FR-042) |
| `apps/krate-manager/tests/required-tests.json`; `.github/workflows/manager-ci.yml:117-120`, `:122-136`, `:153-178` | Required test names; `cargo test --workspace --locked` teed to a log; `core dependency tree`; "required tests ran" (one `test <name> ... ok` per listed name, regex `^test (\S+)(?: - should panic)? \.\.\. ok$` at `:166`, `::notice` with counts) | 🔶 FR-086. The workflow itself is unchanged. The regex counts no doctest line (they contain spaces) and cannot tell a test that returned early from one that asserted (SC-012, SC-015) |
| `tools/feed-contract/tests/manager-ci-shape.test.js:337`, `:739-748` | `EXPECTED_REQUIRED`, deep-compared with `required-tests.json` | 🔶 Gains exactly FR-086's names (FR-091) |
| `specs/krate-manager/research_notes/…/tauri_feasibility.md:17`, `:26` | `plugin-upload` has no resume; download with progress + resume is a Rust command with a manual `Range: bytes=N-` on retry, progress over a `Channel`, SHA-256 with `sha2` | ✅ D2, D4, D8 |
| `specs/krate-manager/research_notes/…/self_update.md:21`, `:122` | The updater verifies with `minisign_verify` (`public_key.verify(data, &signature, true)`), reads the trusted comment only after verification; `minisign-verify` 0.3 rejects a non-canonical Ed25519 `S` | ✅ D6 (order: cryptography before the trusted comment is used) |
| `specs/krate-manager/probes/results.md:9`, `:10` | Files the manager downloads itself carry no Mark-of-the-Web; `tauri signer` keys are minisign keys, verified both ways with rsign2 | ✅ The download writes the bytes and nothing else (FR-004); the release key format (FR-040) |
| `specs/krate-manager/reports/Krate Audio plugin manager design.md:46` | "the minisign key becomes the single root of trust for the manager and all nine plugins" | ✅ Why the key is compiled in and injected, never read from disk at run time (D6) |

---

## External facts used (verified this session)

| Fact | Source |
|---|---|
| `minisign-verify` 0.3.0 (published 2026-09-25, MIT, the newest version; 0.2.5 was 2026-03-03): "zero-dependency" (no `[dependencies]` in its manifest). API: `PublicKey::decode(&str)` (two-line `.pub` text), `PublicKey::from_base64`, `Signature::decode(&str)` (four lines, `str::lines`, 74-byte blob, 64-byte global, the trusted-comment line must start `trusted comment: `; `0x45 0x44` → prehashed, `0x45 0x64` → legacy, else `UnsupportedAlgorithm`), `Signature::trusted_comment(&self) -> &str` (the line after its 17-byte prefix), `PublicKey::verify(&self, bin, &Signature, allow_legacy: bool)`, `PublicKey::verify_stream(&self, &Signature) -> Result<StreamVerifier, Error>` (refuses a key-id mismatch with `UnexpectedKeyId` and a legacy signature with `UnsupportedLegacyMode` before any data), `StreamVerifier::update(&mut self, &[u8])`, `StreamVerifier::finalize(&mut self) -> Result<(), Error>` (Blake2b-512 of the data, then Ed25519 over it and over `signature ‖ trusted_comment`). `Error::{InvalidEncoding, InvalidSignature, IoError, UnexpectedAlgorithm, UnexpectedKeyId, UnsupportedAlgorithm, UnsupportedLegacyMode}` | https://crates.io/api/v1/crates/minisign-verify ; crate source 0.3.0 `src/lib.rs:117-131`, `:221-283`, `:287-417` (downloaded from https://crates.io/api/v1/crates/minisign-verify/0.3.0/download , read this session); https://docs.rs/minisign-verify/0.3.0 |
| `tauri-plugin-updater` 2.14.1 depends on `minisign-verify = "0.3"` and calls `public_key.verify(data, &signature, true)` then `verify_signed_version(signature.trusted_comment(), …)`, with the comment "Only now is the trusted comment usable: minisign's global signature covers it" | crate source 2.14.1 `Cargo.toml:100-101`, `src/updater.rs:1893-1916` (read this session) |
| `minisign` 0.10.0 (published 2026-09-25, MIT, the newest version): `KeyPair::generate_unencrypted_keypair() -> Result<KeyPair>` (in memory, `pub pk`, `pub sk`), `minisign::sign(pk: Option<&PublicKey>, sk: &SecretKey, data_reader: R, trusted_comment: Option<&str>, untrusted_comment: Option<&str>) -> Result<SignatureBox>` (always the prehashed `ED` algorithm), `SignatureBox::to_string()` (the four-line text), `PublicKey::to_box()` / `PublicKeyBox::into_string()`. Normal dependencies: `ct-codecs ^1.1.7`, `scrypt ^0.11.0` (no default features), `getrandom ^0.4`, `rpassword ^7.5.4` (Windows and Unix) | https://crates.io/api/v1/crates/minisign ; crate source 0.10.0 `Cargo.toml`, `src/lib.rs:60-100`, `src/keypair.rs:15-40`, `src/public_key.rs:50-66`, `src/signature_box.rs:133-170` (read this session); https://docs.rs/minisign/0.10.0/minisign/fn.sign.html |
| `sha2` 0.11.1 (published 2026-10-12, MIT OR Apache-2.0, `rust-version` 1.88, the newest version): `use sha2::{Sha256, Digest}`; `Sha256::new()`, `update`, `finalize()`; dependencies `cfg-if ^1`, `digest ^0.11`, `cpufeatures ^0.3` (x86 / x86_64 / aarch64); default features `alloc`, `oid`. `sha2` 0.10.9 is already in `Cargo.lock` (`:3333-3335`) through `wry` and `tauri-codegen`, not through core | https://crates.io/api/v1/crates/sha2 ; crate source 0.11.1 `Cargo.toml`, `README.md`, `src/lib.rs:19` (read this session) |
| `ureq` 3.5.0 (the version in the lockfile, newest): default features `rustls`, `gzip`; `ConfigBuilder::timeout_per_read` ("Max duration for each transport read … The timeout restarts for each read"), `timeout_global` ("The budget continues after the response is returned, while its body is read"), `accept_encoding` ("Defaults to `Default`, which will add `gz` and `brotli` depending on the feature flags … triggering the automatic decompression behavior is not affected since that only looks at the `Content-Encoding` response header"); after transparent decompression `Body::content_length()` is `None`; with the default `gzip` feature (kept by the workspace pin, `apps/krate-manager/Cargo.toml:39`) ureq removes the `Content-Encoding` and `Content-Length` headers from a `Content-Encoding: gzip` response before the caller sees it and hands over the decoded body, and `ContentEncoding::from` matches exactly `"gzip"` / `"br"` (anything else is left as received); no public API exposes the raw encoding (`ResponseInfo` is `pub(crate)`) or turns decoding off per agent (plan SI-18); `Body::as_reader` "is not limited by default"; `ureq::Timeout::{PerRead, Global, Connect, …}` | local registry copy `ureq-3.5.0/Cargo.toml:74-78`, `src/config.rs:649-664`, `:755-800`, `:905-931`, `src/body/mod.rs:195-262`, `:684-710`, `src/timings.rs:14-44`; header stripping `src/run.rs:117-125`, `src/body/mod.rs:111` (`pub(crate) struct ResponseInfo`), `:942-953` (`ContentEncoding::from`) |
| HTTP ranges (RFC 9110): "A server MAY ignore the Range header field" (§14.2); a single-part 206 "MUST generate a Content-Range header field" (§15.3.7.1), e.g. `Content-Range: bytes 42-1233/1234` (§14.4); 416 means "none of the requested ranges are satisfiable" (§15.5.17); "If no Accept-Encoding header field is in the request, any content coding is considered acceptable" (§12.5.3) | https://www.rfc-editor.org/rfc/rfc9110 (text read this session, lines 5556-5570, 6474-6480, 6627, 7108-7118, 7755-7761) |
| Tauri 2 channels: a command takes `on_event: Channel<DownloadEvent>` (`use tauri::{AppHandle, ipc::Channel}`), the event type is `#[derive(Clone, Serialize)]`, sent with `on_event.send(…)`; JS `new Channel<T>()` from `@tauri-apps/api/core`, `onmessage`, passed as a command argument; "Channels are designed to be fast and deliver ordered data. They are used internally for streaming operations such as download progress" | https://v2.tauri.app/develop/calling-frontend/ (Channels); https://v2.tauri.app/develop/calling-rust/ (Channels: "the recommended mechanism for streaming data such as streamed HTTP responses") |
| Rust `io::ErrorKind::StorageFull` ("The underlying storage … is full", stable 1.83.0), `ReadOnlyFilesystem` (1.83.0), `QuotaExceeded` (1.85.0), `PermissionDenied` (1.0.0); toolchain on this box `rustc 1.99.1`, workspace `rust-version = "1.90"` | https://doc.rust-lang.org/std/io/enum.ErrorKind.html ; `rustc --version`; `apps/krate-manager/Cargo.toml:9` |
| `minisign-verify` 0.3.0 derives: `#[derive(Clone, Debug, Eq, PartialEq)] pub struct PublicKey { untrusted_comment: Option<String>, signature_algorithm: [u8; 2], key_id: [u8; 8], key: [u8; 32] }` (all fields private, no public key-id accessor); `Signature` derives only `Clone` | crate source 0.3.0 `src/lib.rs:189-195`, `:220-228` (re-downloaded from https://crates.io/api/v1/crates/minisign-verify/0.3.0/download on 2026-10-31 for revision 2 and read) |
| `ureq` 3.5.0 per-read timeout and accessors: a transport read past `timeout_per_read` fails with `Error::Timeout(Timeout::PerRead)` (`src/timings.rs:558`); a body reader's `io::Error` that wraps a `ureq::Error` is unwrapped back to it by `impl From<io::Error> for Error` (`src/error.rs:218-228`); `Config::timeout_per_read(&self) -> Option<Duration>` (`src/config.rs:417`), `Config::timeouts(&self) -> Timeouts` (`:436`), `Timeouts { pub global, pub per_call, … }` (`:1021-1030`) | local registry copy `ureq-3.5.0` (read this session); https://docs.rs/ureq/3.5.0/ureq/config/struct.Config.html |
| Tauri 2.12.5 (the lockfile version, `Cargo.lock:3660-3661`): `impl Channel<TSend> { pub fn send(&self, data: TSend) -> crate::Result<()> where TSend: IpcResponse }` | local registry copy `tauri-2.12.5/src/ipc/channel.rs:457-460`; https://docs.rs/tauri/2.12.5/tauri/ipc/struct.Channel.html |
| `std::os::unix::fs::MetadataExt::uid(&self) -> u32` (the file owner's user id); POSIX `open` with `O_CREAT`: "the user ID of the file shall be set to the effective user ID of the process" | https://doc.rust-lang.org/std/os/unix/fs/trait.MetadataExt.html#tymethod.uid ; https://pubs.opengroup.org/onlinepubs/9799919799/functions/open.html |
| minisign signature format: the prehashed `ED` variant signs `Blake2b-512(<file>)`; the global signature is `ed25519(<signature> ‖ <trusted_comment>)` | https://jedisct1.github.io/minisign/ "Signature format" (as cited by `CONTRACT.md:219-220`) |

---

## Decisions taken in this spec (open to challenge in the clarify scan)

The roadmap defers one fact to this spec by name: "the spec confirms the current version" of `minisign-verify` (line
611). It is **0.3.0** (External facts), the version the roadmap expected. D1-D12 settle points the roadmap names but does
not pin down; D13 is a test seam the review of revision 1 showed to be necessary.

- **D1, the crates.** Normal dependencies of core: `minisign-verify` 0.3.0 (the crate the Tauri updater uses, zero
  dependencies, a streaming verifier) and `sha2` 0.11.1 (current; its MSRV 1.88 is under the workspace's 1.90). Not
  `sha2` 0.10.9, although it is already in the app's lock graph: that copy is reached only through `src-tauri`'s `wry`
  and the `tauri-codegen` build dependency, so core gains a new crate either way, and the current major is the one that
  will get fixes. Dev-dependency of core: `minisign` 0.10.0, used only through `KeyPair::generate_unencrypted_keypair`
  (in memory) and `minisign::sign`; no test writes a key to disk, not even to a temp dir. Blake2b-512 comes from
  `minisign-verify`'s `StreamVerifier`, so no separate Blake2 crate. Consequence, accepted: `minisign`'s `rpassword` and
  `scrypt` enter the dev-dependency graph only; the `core dependency tree` CI step reads `-e normal` and does not see
  them (`manager-ci.yml:126`). Because a dev-dependency exists only for core's own test targets, the code that calls
  `minisign` lives only in `#[cfg(test)]` modules of core (`test_signer.rs`, `download_tests.rs`, `verify_tests.rs`),
  never in `test_support.rs`, which is also compiled into core's lib with the `test-support` feature when src-tauri's
  test targets build (`lib.rs:22-23`, `src-tauri/Cargo.toml:33-34`) and would then have no `minisign` to link (FR-083).
  `minisign` is not made an optional normal dependency behind `test-support`: that would put it in core's
  `[dependencies]`, against FR-007's set, FR-081 and the `core dependency tree` step.
- **D2, one transport, a download purpose.** `ureq` may appear only in `http.rs` (`injection.rs:224`), so the streaming
  request lives there as `http::get_streamed` (FR-010), sharing `get`'s URL parse, `check_url`, static proxy, PAC routing,
  `walk` and `next_hop`. The download differs from a feed fetch in four agent settings: `Accept:
  application/octet-stream`; `Accept-Encoding: identity`, so a server never compresses the bytes the hash covers (with no
  `Accept-Encoding`, RFC 9110 lets a server pick any coding, and `ureq`'s default advertises gzip); no 30 s total bound,
  which a 60 MB installer on a slow link would exceed, but a per-read stall bound `DOWNLOAD_STALL = 30 s` and a per-attempt
  bound `DOWNLOAD_ATTEMPT_DEADLINE = 3600 s`; and final statuses 200 and 206 instead of 200 only. A response that still
  carries a `Content-Encoding` other than `identity` when it reaches the download is refused (`E_DOWNLOAD_ENCODING`, no
  retry); a `gzip` coding never reaches it, because ureq strips the header and decodes the body, so the bytes on disk are
  then checked by the size bound and verification like any other (FR-013) (plan SI-18). The Phase 5 `HttpError` enum is
  not changed: `get_streamed` returns its own `StreamError`, which wraps an `HttpError` and adds the three outcomes only
  a streamed body has (a sink error with its `io::ErrorKind`, a per-read stall, a head the caller refused), so no Phase
  5 match or test changes. The stall bound reaches `ureq` inside the purpose, `AgentPurpose::Download { stall }`, with
  no new parameter of `run_chain`, `walk` or `agent_config` (the feed and PAC purposes keep their agents identical)
  (plan SI-6); the
  attempt deadline needs no new field, because `walk` already sets each hop's `total_timeout` to what is left of the
  chain's deadline (`http.rs:1044-1051`). The PAC fetch and PAC evaluation keep the feed's 30 s bound.
- **D3, the staging layout.** Everything lives under `<staging>/downloads/` (`Roots::staging_dir()` = `<state>/staging`,
  `roots.rs:103-105`). A transfer in progress is `<sha256hex>.part`; a verified file is
  `<sha256hex>/<asset file name>`, created by renaming the `.part` after every check passed. The SHA-256 (64 lower-case
  hex digits, from the feed) is the key, so a partial can only ever be resumed by a request for the same bytes, and two
  plugins never share a name (feed keys may differ only in letter case, and Windows and default APFS fold case). The
  asset file name (`AssetName`, `feed.rs:196-214`: `^[A-Za-z0-9][A-Za-z0-9._-]*$`, never `.`, `..` or a reserved stem)
  is the only feed string that becomes a path component, and it is checked again at the join (FR-003). Before a
  download, every entry of `downloads/` whose SHA-256 is not the native asset hash of some entry of the feed in use, and
  that is not in flight, is removed (FR-005), so staging holds at most one artifact per offered plugin. A verified
  artifact obtained earlier for another offered plugin therefore survives a later `obtain_and` with the same feed, which
  is what lets Phase 10 collect a batch (D7, SC-026). The prune decides nothing about an artifact whose hash a *newer*
  feed no longer offers while a batch still holds it: that lifetime belongs to Phases 10 and 12 (Non-goals).
- **D4, the resume rule.** A partial of `n` bytes, `0 < n < size`, is resumed with `Range: bytes=<n>-`. The answer is
  accepted as a continuation only when it is `206` with `Content-Range: bytes <n>-<size-1>/<size>` exactly and, when
  present, `Content-Length: <size-n>`. A `200` means the server ignored the range (RFC 9110 §14.2): the partial is
  truncated and the body is written from byte 0 (a `Restarted` progress event). A `416`, or a `206` with any other
  range, discards the partial and makes one full request. A partial of exactly `size` bytes (a crash after the last
  byte, before the rename) goes straight to verification with no request; a partial longer than `size` is discarded. If
  verification fails with `E_VERIFY_SHA256` on a file that was assembled from more than one response, the file is
  discarded and the download is repeated once in full; a SHA-256 mismatch on a single full response is final. No
  `If-Range` is sent: a release asset at a per-tag URL is not expected to change, and the hash catches a splice.
- **D5, bounded retries.** `DownloadPolicy { attempts: 4, backoff: [1 s, 2 s, 4 s], stall: 30 s, attempt_deadline: 3600
  s }` (`DownloadPolicy::production()`), passed in `DownloadEnv::policy` (FR-045) (plan SI-25) so tests use zero backoff and a short stall. Retried
  (each retry resumes from what is on disk): no response (`E_DOWNLOAD_OFFLINE`), a timeout or a stall, a body that ends
  early or errors (`E_DOWNLOAD_BODY`), and statuses 408, 429, 500, 502, 503, 504. Not retried: every other status, a
  URL or redirect refusal, TLS, `E_DOWNLOAD_SIZE`, `E_DOWNLOAD_ENCODING`, a staging I/O error, and every verification
  error except D4's one full re-download, and a cancellation (`E_DOWNLOAD_CANCELLED`, FR-017). Decided 2026-10-31 (Q5):
  there is no overall bound on one `obtain_and`; the 3600 s deadline applies to each attempt, 4 attempts, and a download
  that is slow but keeps moving is not an error. The way to stop one is the `CancelToken` (FR-017, FR-018).
- **D6, verification is one streamed pass over the staged file, in a fixed order.** (1) the file's length equals the
  feed `size`; (2) the feed `signature` text passes `contract::parse_minisig_text` and `minisign_verify::Signature::decode`;
  (3) its key id equals the release key's (`verify_stream`, before any byte is read); (4) one read of the file, in 64 KiB
  chunks, feeds both SHA-256 and the `StreamVerifier`; (5) the SHA-256 equals the feed `sha256`; (6) `finalize` passes
  (the Ed25519 signature over Blake2b-512 of the data and the global signature over signature ‖ trusted comment); (7)
  the trusted comment, now authenticated, parses under C3 (`contract::parse_trusted_comment`); (8) its four fields equal
  the entry's feed key, `version_text`, platform key `as_str()` and `AssetName`. Cryptography comes before the trusted
  comment is used, as the updater does (External facts) and as CONTRACT.md section 5 prescribes ("A verifier first
  checks the signature cryptographically … then `checkBinding`"). The key is injected (`AppContext.signing_key`), never
  read from disk at run time: production builds it once from the text compiled in from
  `tools/feed-contract/keys/krate-signing.pub`, tests from a throwaway key.
- **D7, "marked verified" is a type; the continuation receives it by value.** `verify::VerifiedArtifact` has private
  fields and no public constructor; only `verify::verify_file` returns one. The download's public entry point is
  `download::obtain_and(…, next: impl FnOnce(VerifiedArtifact) -> T) -> Result<Obtained<T>, DownloadError>`, which
  calls `next` only after verification succeeded and never for a failure, and hands it the **owned** artifact. A caller
  that needs the artifact later returns it from `next` (`obtain_and(…, |a| a)`). This matches Q7 (roadmap line 30) and
  Phase 10 (lines 746-752): Phase 10 obtains the artifacts of a batch one by one (or Phase 12 in bounded parallel),
  collects the returned `VerifiedArtifact`s and builds **one** helper job from them, behind one elevation prompt
  (Cross-Cutting, line 997); nothing in Phase 7 runs an install per download. The guarantee that nothing unverified
  reaches Phases 8 and 10 therefore rests on the type, not on where the call happens: their entry points accept only
  `VerifiedArtifact` (singly or as a batch), and the only ways to get one are `verify_file` and `obtain_and` (FR-039,
  FR-043). The roadmap's "call count 0" outcome is measured with counting continuations that stand in for those entry
  points (SC-003), and SC-026 proves two artifacts obtained one after the other can both reach one batch consumer.
  Threat model, stated: a process running as
  the same user can rewrite the staged file after verification; Phase 7 does not defend against that, because the same
  process can write every user-writable destination too. The privilege boundary is Phase 10's helper, which copies the
  installer into an admin-only directory, re-hashing as it copies (roadmap lines 733-735).
- **D8, progress is a closure of typed events, and `received` never goes backwards.** `&mut dyn FnMut(Progress)` with
  `Progress::{Started { total, resumed_from }, Bytes { received, on_disk, total }, Restarted { reason }, Retrying { attempt, of, code, wait_ms
  }, Verifying, Verified }` (`serde::Serialize`, tagged; `Retrying` per Q4, FR-023; no heartbeat event). `on_disk` is the length of the file being assembled (the `.part`); it drops
  after a `Restarted`. `received` is the progress toward a full copy, the largest `on_disk` reached so far in this
  `obtain_and` (starting at `resumed_from`), so it **never decreases in any run**, strictly increases in a run without
  `Restarted`, and the last `Bytes` has `received == on_disk == size`. This keeps the roadmap outcome ("monotonically
  increasing byte counts ending at the feed `size`", line 626) unconditional; a restart is signalled only by the
  `Restarted` event and the drop of `on_disk`, which a later UI may show. `Bytes` is emitted when at least 256 KiB were
  written since the previous `Bytes` event and always when `on_disk == size`. Core tests observe it directly; no Tauri
  type enters core.
- **D9, one command, a feed key in, no path out; the logic is in core.** `download_plugin(name: String,
  expected_version: String, on_progress: Channel<Progress>) -> Result<DownloadSummary, String>`: async,
  `spawn_blocking`, takes only a feed key and the version the status screen showed (never a URL or a path from the
  webview; `expected_version` is only compared with the feed entry's `version_text`, FR-065) and returns `{ name, version, platform, asset, size, resumed }` with no filesystem path. A
  Tauri `Channel` exists only as a command argument (External facts), so the roadmap's "`src-tauri` adapts it to a Tauri
  `Channel` for the webview" (line 606) needs a command. Because `src-tauri` runs no unit tests (`src-tauri/Cargo.toml:
  10-19`), the command is a thin wrapper, as Phase 6's `status_for` is over `status::build` (`commands.rs:31-44`): the
  flow (`feed::check`, the OS version, `request_for`, `obtain_and`, the summary) is the core function
  `download::for_command`, and the adapter from a fallible send to the progress closure is the core function
  `download::channel_progress`; the command passes `|e| on_progress.send(e)` (`Channel::send(&self, TSend) ->
  tauri::Result<()>`, External facts) and nothing else. No UI calls it in Phase 7: the Install / Update buttons are
  Phase 9's (roadmap line 687) and Phase 10's, "Update all" Phase 12's. Decided 2026-10-31 (Q6): the command is
  permanent (not marked internal, not removed when Phase 9 adds its install command); the future UI is two-step
  (bounded parallel downloads, then one install batch over the cached verified files, which the install command
  re-verifies, FR-046). Decided 2026-10-31 (Q1): the command re-fetches the feed on every call (as `status` does), and
  `expected_version` makes a feed that moved on since the status screen a named refusal instead of a silent download of
  another version. The command passes a `CancelToken` that nothing sets (Q2, FR-017).
- **D10, in-process exclusion per artifact.** One manager process owns a state dir (`state_lock.rs:53-75`). Within it, a
  second `obtain_and` for the same `(staging dir, SHA-256)` while one is in flight returns `E_DOWNLOAD_BUSY` at once;
  different artifacts may download in parallel (Phase 12 bounds how many). The registry is keyed by staging dir too, so
  parallel tests in separate temp roots never contend. No lock file: on Windows a `try_lock` would also block the
  verifier's own read handle.
- **D11, staging I/O is injected.** Every filesystem step of the download and the verified rename goes through a
  `StagingIo` trait (create dir, open partial, write, sync, truncate, rename, remove), as `settings.rs` does with
  `SettingsIo`; production is `FsStagingIo`. Tests inject `StorageFull`, `PermissionDenied` (SC-011) and a simulated
  crash after each step (a panic from the injected `StagingIo`, which skips every cleanup path just as a killed process
  would; SC-011). A write, sync or truncate failure removes the partial (best effort, so a full disk gets its space
  back); a failed final rename keeps the complete `.part`, which the next request re-verifies without the network (D4).
- **D12, codes and logs.** Every error `Display` begins with its code (the Phase 5 convention, `http.rs:217-236`).
  Logs and errors name the feed asset URL only: never a redirect target, whose query string on a release CDN is a
  signed, time-limited token.
- **D13, a test-only fetch-URL seam, because the contract admits only `https://`.** A contract-valid feed can only carry
  `https://` asset URLs (`AssetUrl::checked` → `is_c1_url`, `feed.rs:167-169`, `:218-222`; schema pattern
  `^https://[^\s]+$`), and the fixture server speaks plain HTTP (`fixture_server.rs:147-150`; an `https://` request to
  it ends `E_FEED_TLS`, `http_tests.rs:1277-1283`). So a test feed that passes `feed::parse` cannot point at the fixture
  server. Phase 7 closes the gap with one seam that keeps the feed, the binding, the logs and FR-002 unchanged: a
  `#[cfg(test)]` field `DownloadRequest::fetch_url` and a `#[cfg(test)] pub(crate) fn with_fixture_url(self, url: &str)
  -> Result<DownloadRequest, &'static str>` that accepts only `http://127.0.0.1:<port>/…` or `http://[::1]:<port>/…`
  whose last path segment equals the request's `AssetName`, checked by parsing with `url::Url` (scheme `http`, host the
  IP literal `127.0.0.1` or `::1`, an explicit port), so `download.rs` still holds no `"http://"` literal (FR-082). The
  download *fetches* that URL; every log line and error
  still names the feed's `AssetUrl`. Release and `test-support` builds have neither the field nor the function (FR-085,
  guard SC-032). Not chosen: a TLS fixture server with an injected test root, which would make `RootCerts` injectable
  in `HttpConfig` and change the Phase 5 transport for every purpose.

---

## Functional Requirements

### A. The request and the staging directory (roadmap lines 604-605; D3, D10)

- **FR-001** `download::DownloadRequest` is built only by `download::request_for(feed: &Feed, name: &str, running:
  Option<&OsVersion>) -> Result<DownloadRequest, DownloadError>`: the entry is `feed.plugins[name]`, resolved with
  `version::resolve_native(entry, running)` (`version.rs:233-240`); only `Availability::Available { key, asset }` yields
  a request (`name`, `version_text`, `key`, a clone of `asset`, and the `running` it was resolved with, a private field
  `running: Option<OsVersion>` that FR-005's prune reuses) (plan SI-7). A missing name is `E_DOWNLOAD_UNKNOWN_PLUGIN`;
  `NotThroughManager`, `NotForThisMachine` and `NeedsNewerOs { min_os }` are `E_DOWNLOAD_UNAVAILABLE` naming the
  variant. Every per-plugin fact comes from the feed entry (ROSTER, roadmap line 33): core ships no plugin name, URL or
  hash. Under `#[cfg(test)]` only, `DownloadRequest` also has the D13 field `fetch_url: Option<String>` (set only by
  `with_fixture_url`); the transfer fetches `fetch_url` when it is `Some` and the entry's `AssetUrl` otherwise, and in
  every build the logs and errors name the `AssetUrl`.
- **FR-002** The download URL is the entry's `AssetUrl` (`feed.rs:160-171`: `https://`, no `/releases/latest/`). Phase 7
  holds no URL literal in `download.rs` or `verify.rs` and never constructs a URL from parts (guard FR-082).
- **FR-003** Paths: `downloads = roots.staging_dir()/downloads`; partial `downloads/<hex>.part`; verified
  `downloads/<hex>/<asset>`, where `<hex>` is the feed `sha256` as 64 lower-case hex digits and `<asset>` is
  `AssetName::as_str()`. The join is one pure function, `pub(crate) fn staged_paths(downloads: &Path, hex: &str, asset:
  &str) -> Result<StagedPaths, DownloadError>` (`StagedPaths { part, dir, verified }`), which takes plain strings so its
  refusals are testable even though `AssetName` (`feed.rs:202-213`) can never carry a bad name through the public API:
  `hex` must be exactly 64 lower-case hex digits, `asset` one normal component (non-empty, no `/` or `\`, not `.` / `..`,
  not absolute, no drive prefix), and the joined paths strict descendants of `downloads` (lexical, `Path::starts_with`);
  a failure is `E_DOWNLOAD_PATH` and nothing is written (SC-033). Through `obtain_and` this is defence in depth. On a
  sandboxed (test) `Roots` the path also goes through `Roots::check_sandboxed` (`roots.rs:173-185`).
- **FR-004** The download writes only the response bytes. It sets no extended attribute, alternate data stream, mode
  or timestamp of its own (the manager never adds quarantine; Cross-Cutting "Unsigned reality", roadmap lines
  1001-1003), and it writes nothing outside `downloads/`. Proven by SC-031: a before/after tree diff of the test's temp
  root, the absence of `com.apple.quarantine` on the verified file on macOS (PENDING-CI) and of a `Zone.Identifier`
  stream on Windows.
- **FR-005** Pruning (D3): at the start of `obtain_and`, every entry of `downloads/` whose name is not `<hex>.part` or
  `<hex>` for a `<hex>` in the set of native-asset SHA-256s of the feed passed in (each entry resolved as FR-001 with the
  same `running`, the one the request carries, FR-001) (plan SI-7) is removed, except entries of an in-flight artifact
  (FR-006). A name that is neither shape is removed
  too. A live entry is kept only with the right kind (`read_dir_entries`, FR-050): a `<hex>` only when it is
  `EntryKind::Dir` and a `<hex>.part` only when it is `EntryKind::File`; any other kind (a symlink, a junction, a
  socket) is removed whatever the feed and the registry say, through `remove_file` (on Windows retried once with
  `remove_dir_all`, FR-050), which removes the link itself and never follows it (plan SI-23). A removal that fails is logged at `warn!` with its code and does not fail the download. A verified artifact of
  another plugin the same feed offers is therefore never removed, whether or not a caller still holds its
  `VerifiedArtifact` (SC-026).
- **FR-006** In-process exclusion (D10): a static registry of `(downloads dir, sha256)` in flight; `obtain_and` inserts
  before any staging I/O and before its first `Progress` event, and removes on every exit path, including a panic
  (a drop guard). A second request for a key already present is `E_DOWNLOAD_BUSY`, returned before any request, file
  operation or `Progress` event.

### B. Transport (roadmap lines 604-608; D2, D4, D5)

- **FR-010** `http.rs` gains `AgentPurpose::Download { stall: Duration }` (the stall bound travels in the purpose, so
  `agent_config(config, proxy, purpose)` keeps its three parameters) (plan SI-6) and `pub fn get_streamed(config:
  &HttpConfig, url: &str, headers:
  &[(&str, &str)], limits: StreamLimits, on_head: &mut dyn FnMut(&ResponseHead) -> HeadAction, sink: &mut dyn
  FnMut(&[u8]) -> io::Result<()>, cancel: &CancelToken) -> Result<StreamEnd, StreamError>` (`CancelToken`: FR-017) where
  `StreamLimits { stall: Duration, deadline: Duration }` (no `max_body`: the body limit depends on the head)
  (plan SI-27), `ResponseHead { status: u16, content_length: Option<u64>, content_range: Option<String>,
  content_encoding: Option<String> }` (header values as ureq hands them over, the final hop only), `HeadAction::{Read {
  max_body: u64 }, Stop}` (the limit is returned by `on_head` after it has seen the head) (plan SI-27),
  `StreamEnd { bytes: u64 }` and `StreamError` (`Clone, Debug, PartialEq, Eq, thiserror::Error`):
  `Http(HttpError)` (every Phase 5 outcome, the enum unchanged, `http.rs:219-251`), `Stalled` (a per-read timeout,
  below), `Sink { kind: io::ErrorKind }` (the sink returned an error; nothing more is read) and `HeadStopped`
  (`on_head` returned `Stop`; no body byte is read) and `Cancelled` (the token was set: checked before the request and
  before each chunk is read, FR-017). `HttpError` gains no variant and no field, so no Phase 5 match,
  construction or test changes. Classification order inside `get_streamed`: a `ureq::Error::Timeout(Timeout::PerRead)`
  (ureq-3.5.0 `src/timings.rs:558`), whether returned by the call or by a body read (an `io::Error` from the body reader
  is turned back into the wrapped `ureq::Error` by `From<io::Error>`, `src/error.rs:218-228`), is `Stalled`; every other
  `ureq` error goes through the unchanged `map_error` (`http.rs:665-681`). The call-phase `PerRead` reaches this
  classification because the hop loop is generic over a private error-mapping trait (`ChainError: From<HttpError>`
  with `from_ureq(e, phase)`, implemented by `HttpError` through `map_error` and by `StreamError` with `PerRead` →
  `Stalled`), so `walk` hands the raw call error to the purpose's own mapper (FR-012) (plan SI-6). The `Download` agent sets
  `https_only(policy == HttpsOnly)`, `max_redirects(0)`, `http_status_as_error(false)`, the proxy chosen per hop exactly
  as `get` does, `RootCerts::PlatformVerifier`, `timeout_connect(connect_timeout)`, `timeout_per_read(stall)`,
  `timeout_global(total_timeout)` (which `walk` sets to the remaining attempt deadline, FR-011), the same `user_agent`,
  `accept("application/octet-stream")` and `accept_encoding("identity")`. `on_head` runs once, on the final response,
  before any body byte reaches `sink`; `Stop` drops the response unread (`HeadStopped`), `Read { max_body }` makes that
  `max_body` this response's limit. The body is read through `Body::as_reader()` in chunks of at most 64 KiB, each
  passed to `sink`; a chunk that would take the total past the `max_body` `on_head` returned is not passed on and ends
  `StreamError::Http(HttpError::TooLarge { max: max_body })` (plan SI-27).
- **FR-011** `get_streamed` uses `check_url`, the static-proxy and PAC routing and `next_hop` unchanged (`http.rs:276-303`,
  `:345-382`, `:935-971`). `walk` (`http.rs:1030-1071`) and `agent_config` (`:630-650`) keep their parameters:
  the stall bound reaches the agent inside the purpose, `AgentPurpose::Download { stall }` (FR-010), and
  `agent_config` branches on it, `Download { stall }` → `accept("application/octet-stream")`,
  `accept_encoding("identity")`, `timeout_per_read(Some(stall))`, and `https_only` under `HttpsOnly` for `Feed` and
  `Download` (today it is set only for `Feed`, `http.rs:636`). `Feed` and `PacFetch` build exactly today's config, so
  `agent_trusts_the_os_store` (`http_tests.rs:282-303`) and every existing `agent_config` and `walk` test keep passing
  with the three-argument `agent_config(&config, None, purpose)` unchanged (SC-001, SC-010) (plan SI-6). `walk` becomes
  generic over the private `ChainError` of FR-010, mapping its call error with `E::from_ureq(e, Phase::Call)`, so the
  one `new_with_config(agent_config(` site in `walk` stays the only agent construction (plan SI-6). The
  chain deadline of `get_streamed` is `start + limits.deadline`, reaching each hop's `total_timeout` through `walk`'s
  existing `hop_config` (`http.rs:1044-1051`); `open_pac` and `pac_route` (`http.rs:737`, `:891-897`) receive the feed
  bound `start + min(config.total_timeout, limits.deadline)`, so a PAC fetch never waits an hour. A final status of `200`
  or `206` reaches `on_head`; any other final status is `StreamError::Http(HttpError::Status)`. `get` keeps its
  behaviour, its 200-only rule and its tests (FR-012).
- **FR-012** `run_chain` (`http.rs:986-1027`) keeps its parameters and its behaviour: its redirect loop becomes the
  private `final_response<E: ChainError>(…) -> Result<Response<Body>, E>` (the loop up to `Hop::Done`, returning the
  final response), and `run_chain` is `final_response::<HttpError>` plus the unchanged 200-only rule and `read_body`;
  `get_streamed` calls `final_response::<StreamError>` with `AgentPurpose::Download { stall: limits.stall }` and
  accepts 200 and 206 (plan SI-6). `AgentPurpose::Feed` and `PacFetch` keep `[200]` and `read_body`. Every Phase 5
  `http::tests` case passes unchanged (SC-001).
- **FR-013** `download` sends exactly these extra headers: `Range: bytes=<n>-` when resuming (D4), nothing otherwise.
  Its `on_head` closure calls the pure function `download::classify_head(requested_from: u64, size: u64, head:
  &ResponseHead) -> Result<HeadVerdict, DownloadError>` (testable without a socket), keeps the result, and returns
  `HeadAction::Read { max_body: size - requested_from }` for `Continue`, `HeadAction::Read { max_body: size }` for
  `Full` (whatever `requested_from` was: D4's `200` fallback writes the whole body from byte 0), and `HeadAction::Stop`
  otherwise (plan SI-27); after `StreamError::HeadStopped` the
  download acts on the kept result. `classify_head` returns: `Full` for `200` with `Content-Length` absent
  or equal to `size`; `Continue` for `206` when `requested_from > 0`, `Content-Range` is exactly
  `bytes <requested_from>-<size-1>/<size>` and `Content-Length` is absent or `size - requested_from`; `RestartFull` for
  `206` with the exact range but any other `Content-Length` (plan SI-11), for `206` with any other range or for `206`
  to a request without `Range`. A `Content-Encoding` that reaches `classify_head` other than absent or `identity`
  (ASCII case-insensitive) is `E_DOWNLOAD_ENCODING`; a `gzip` coding is decoded by the transport and never reaches it
  (ureq 3.5.0 removes `Content-Encoding` and `Content-Length` from a gzip response and decodes the body, External
  facts), so its guard is `max_body` and `verify_file` (plan SI-18). A `200` whose `Content-Length` differs from `size` is
  `E_DOWNLOAD_SIZE` (the roadmap's "`Content-Length` vs feed `size`", line 609). `416` is not a head verdict (FR-011
  makes it a status error); `download` turns a `416` to a ranged request into one full request (D4).
- **FR-014** Size during the body: `max_body` is `size - requested_from` for a `Continue` verdict and `size` for a
  `Full` verdict (D4's `200` fallback writes from byte 0) (plan SI-27); a body longer than that is `E_DOWNLOAD_SIZE`
  at the first excess byte (no more is written); a body that ends shorter is `E_DOWNLOAD_BODY` (retried, D5) when the
  connection ended early, and the bytes received stay in the partial for the next attempt. On `E_DOWNLOAD_SIZE` and on
  `E_DOWNLOAD_ENCODING` (both final, D5) the `.part` is removed (best effort, a failed removal logged as FR-051 does):
  the server and the feed disagree about the bytes, so a kept `size`-byte partial would only be re-verified without a
  request (D4) and fail, and a later request starts from byte 0.
- **FR-015** Retries follow D5. Between attempts the download sleeps `policy.backoff[i]` (the last value repeated if
  `attempts - 1` exceeds its length). Each attempt starts from the partial's current length. Total requests for one
  `obtain_and` ≤ `attempts` + 1 (the one extra is D4's full re-download). The request budget: one `obtain_and` makes at
  most `policy.attempts` requests, plus exactly one more after D4's `HashMismatchAfterResume`; every request consumes
  one unit, a ranged request, a retry, and the immediate full request after a `416` or a `RestartFull` `206` alike (the
  last two with no backoff and no `Retrying`); a retry happens only while a unit is left; the hash re-download happens at
  most once and never after FR-046's failed cached file, so a run holds at most two `Verifying` events (plan SI-12).
- **FR-016** Error mapping (D12), from `StreamError`: `Stalled` → `E_DOWNLOAD_STALLED`; `Sink { kind }` → the FR-051
  staging code for the step that failed; `HeadStopped` → the kept `classify_head` result (FR-013); `Cancelled` → `E_DOWNLOAD_CANCELLED` (FR-017); `Http(e)`:
  `Offline` → `E_DOWNLOAD_OFFLINE`, `Timeout` (the attempt deadline, a connect or a resolve timeout, never a per-read
  one) → `E_DOWNLOAD_TIMEOUT`, `Tls` → `E_DOWNLOAD_TLS`, `Url` → `E_DOWNLOAD_URL`, `Redirect` → `E_DOWNLOAD_REDIRECT`,
  `Status { status }` → `E_DOWNLOAD_HTTP_STATUS { status }`, `TooLarge` → `E_DOWNLOAD_SIZE`, `Body` →
  `E_DOWNLOAD_BODY`. A retried attempt's code is never visible in the result, so SC-009 (f) and (g) prove the stall and
  deadline codes on runs where every attempt ends the same way. Every `DownloadError` names the plugin, the asset name
  and the feed URL, and no redirect-hop URL (D12).

- **FR-017** Cancellation (Q2). `http::CancelToken` is a `Sync` wrapper over an `AtomicBool` (`new()`, `cancel(&self)`,
  `is_cancelled(&self) -> bool`; `Ordering::Relaxed` or stronger), re-exported by `download`. `get_streamed` (FR-010)
  takes a `&CancelToken` parameter; `obtain_and` (FR-045), `run_command` and `for_command` (FR-060) take it as
  `DownloadEnv::cancel` (plan SI-25). The token is checked
  (1) before each request of an attempt, including the first, (2) after every chunk read from the body and before the
  next read, and (3) during every backoff wait, which sleeps in slices of at most 50 ms and checks between slices. Once
  it is set, the call returns `E_DOWNLOAD_CANCELLED` (`DownloadError::Cancelled`, FR-016) within one chunk read, one
  slice or one stall bound (a read already blocked in the transport ends when it returns or at `policy.stall`); it makes
  no further request and no retry (D5), emits no `Retrying`, `Verifying` or `Verified`, calls `next` zero times, and
  leaves the in-flight registry entry released (FR-006). Every byte already written stays in `<hex>.part`, which is not
  removed (the exception to FR-014 and FR-051's removals): a later `obtain_and` resumes from it (D4). A token that is set
  before the call starts gives `E_DOWNLOAD_CANCELLED` with zero requests and no `.part` created. Phase 7 provides no
  cancel command: `download_plugin` passes a token nobody sets (FR-060), so no webview path sets one. Proven by SC-034.
- **FR-018** No overall time bound (Q5). `DownloadPolicy::production()` is exactly `attempts: 4`, `backoff: [1 s, 2 s,
  4 s]`, `stall: 30 s`, `attempt_deadline: 3600 s` (D5), and `obtain_and` adds no wall-clock budget across attempts and
  backoff: the only bounds are the per-attempt deadline (`E_DOWNLOAD_TIMEOUT`), the stall bound (`E_DOWNLOAD_STALLED`),
  the attempt count and the token (FR-017). A download whose reads each complete within the stall bound is never
  failed because the whole run is slow. Proven by SC-037.

### C. Progress (roadmap lines 604-606, 626; D8)

- **FR-020** `download::Progress` (`Clone, Debug, PartialEq, Serialize`, `#[serde(tag = "kind", rename_all =
  "snake_case")]`): `Started { total: u64, resumed_from: u64 }`, `Bytes { received: u64, on_disk: u64, total: u64 }`,
  `Restarted { reason: RestartReason }` (`RangeIgnored`, `RangeRefused`, `PartialTooLong`, `HashMismatchAfterResume`,
  `CachedInvalid`: FR-046's cached verified file failed re-verification) (plan SI-9),
  `Retrying { attempt: u32, of: u32, code: String, wait_ms: u64 }` (FR-023), `Verifying`, `Verified`. `total` is always
  the feed `size`. There is no heartbeat or other time-based event.
- **FR-021** Order: one `Started` before the first request (or before verification when a partial or a cached file is
  already complete), `Bytes` events per D8, a `Retrying` after each failed attempt that will be retried and before its
  backoff wait (FR-023; it interleaves with `Bytes` and `Restarted` in the order the attempts happen), `Verifying` before each verification pass (at most two in one
  `obtain_and`: FR-046's failed cached file or FR-047's re-download adds one), `Verified` once after the pass that
  succeeded. A failed download ends with no `Verified`. The callback is called on the download's own thread; it must
  not block for long (`channel_progress` only forwards, FR-064).
- **FR-022** Emission and monotonicity (D8): a `Bytes` event when ≥ `PROGRESS_STEP = 256 * 1024` bytes were written
  since the last `Bytes` event or when `on_disk == total`. `on_disk` is the `.part`'s length after the write;
  `received = max(resumed_from, every on_disk so far in this obtain_and)`. In **every** run the `received` values never
  decrease, and in every successful run that transfers a byte the last `Bytes` has `received == on_disk == size` (a run
  that transfers nothing, FR-046 or a complete `.part`, emits no `Bytes`); in a run without `Restarted` they strictly increase
  and `on_disk == received` throughout; the first `Bytes` after a resume is greater than `resumed_from`. After a
  `Restarted`, `on_disk` starts again at ≤ `PROGRESS_STEP`.

- **FR-023** Retry events (Q4). Before each backoff wait (FR-015), `obtain_and` emits exactly one `Progress::Retrying {
  attempt, of, code, wait_ms }`: `attempt` is the 1-based index of the request that just failed, counted in FR-015's
  request budget (a `416` or `RestartFull` follow-up consumes an index too) (plan SI-12), `of` is
  `policy.attempts`, `code` is the failed attempt's `DownloadError::code()` (for example `E_DOWNLOAD_STALLED`,
  `E_DOWNLOAD_OFFLINE`, `E_DOWNLOAD_HTTP_STATUS`), and `wait_ms` is the backoff about to be waited, in milliseconds
  (`policy.backoff[attempt - 1]`, FR-015's repeat rule). It is emitted only for a failed request with a unit of FR-015's
  budget left, which keeps the count at most `attempts - 1` (plan §4.4.8's proof) (plan SI-12). The event is emitted after the failed attempt's last `Bytes` and
  before the wait, so the webview hears something during a stall plus backoff. It is emitted only for an attempt that
  will be retried: the final failed attempt, a non-retryable code (D5) and a cancellation emit none, and D4's one full
  re-download after `HashMismatchAfterResume` is signalled by `Restarted` only. A run's `Retrying` count is therefore at
  most `attempts - 1`. `Retrying` has no effect on FR-022's `received` / `on_disk` rules. Nothing else is emitted on a
  timer (no heartbeat). Proven by SC-036 and SC-009.

### D. Verification, `core::verify` (roadmap lines 609-614; D6, D7)

- **FR-030** `pub(crate) fn verify::verify_file(path: &Path, expected: &Expected, key: &SigningKey) ->
  Result<VerifiedArtifact, VerifyError>`, where `pub(crate) struct Expected { name: &str, version: &str, platform:
  PlatformKey, asset: &AssetName, format: AssetFormat, size: u64, sha256: [u8; 32], signature: &str }` (every field
  `pub(crate)`; `format` is what `VerifiedArtifact.format` is built from, FR-039) (plan SI-8) is built from a
  `DownloadRequest` (`pub(crate) fn Expected::from_request`, a feed-derived request being the only production source)
  and, in tests, from the KAT files (plan SI-28). Both are crate-private, so outside `krate-manager-core` the only route
  to a `VerifiedArtifact` is `download::obtain_and`, which passes `ctx.signing_key`; Phase 11's shared code (FR-037)
  reaches verification through a core function that takes the key from `AppContext.signing_key`, never from its caller
  (Phase 11's to add) (plan SI-28). It runs D6's eight steps in order and stops at the first failure.
- **FR-031** Step 1: before the file is opened, `fs::symlink_metadata(path)` must succeed (an error → `E_VERIFY_IO {
  kind }`, e.g. `NotFound`) and name a regular file, never followed through a final-component link (anything else →
  `E_VERIFY_IO { kind: InvalidInput }`) (plan SI-23); then the file's length (`metadata().len()` on the opened handle)
  differs from `size` → `E_VERIFY_SIZE {
  expected, found }`, nothing read.
- **FR-032** Step 2: `contract::parse_minisig_text(signature)` fails → `E_VERIFY_SIGNATURE_TEXT` carrying the contract
  message; then `minisign_verify::Signature::decode` fails → `E_VERIFY_SIGNATURE_TEXT`. Legacy (`Ed`) signatures are
  refused here (the contract requires `ED`) and again by `verify_stream` (`UnsupportedLegacyMode`).
- **FR-033** Step 3: `key.public.verify_stream(&signature)` (the private field of `SigningKey`; there is no public
  accessor, so core's public API names no `minisign_verify` type) (tasks TA-13) returning `UnexpectedKeyId` →
  `E_VERIFY_KEY_ID { expected, found }` (both as 16 upper-case hex digits), nothing read.
- **FR-034** Step 4: one sequential read in 64 KiB chunks; each chunk updates SHA-256 and the `StreamVerifier`; the
  bytes read must total `size` (a file that changed length under the read → `E_VERIFY_SIZE`); an I/O error →
  `E_VERIFY_IO { kind }`. The file is never read whole into memory (guard FR-082).
- **FR-035** Step 5: SHA-256 ≠ `sha256` → `E_VERIFY_SHA256 { expected, found }` (64 hex digits each).
- **FR-036** Step 6: `finalize()` fails → `E_VERIFY_SIGNATURE`. This covers a tampered file whose feed hash was also
  changed, a signature by a different key with the same key id, a changed signature line and a changed trusted comment
  (the global signature).
- **FR-037** Step 7: `contract::parse_trusted_comment(signature.trusted_comment())` fails → `E_VERIFY_TRUSTED_COMMENT`
  carrying the contract code (`E_TC_FIELDS`, `E_TC_NAME`, `E_TC_VERSION`, `E_TC_PLATFORM`, `E_TC_ASSET`). Through
  `obtain_and` this step is defence in depth and unreachable from a contract-valid feed: `feed::parse` already runs
  `parse_trusted_comment` on every defined platform key's signature (`contract.rs:868-890`), so a parsed `Feed` never
  carries a C3-invalid comment. `verify_file` keeps the step because it is also called on inputs that did not come from
  a feed (the KAT files, Phase 11's shared code), and SC-004 proves it with a hand-built `Expected`.
- **FR-038** Step 8 (the C3 binding, CONTRACT.md section 5): the parsed fields are compared in the order name, version,
  platform, asset with `expected.name`, `expected.version`, `expected.platform.as_str()`, `expected.asset.as_str()`,
  byte for byte; the first difference is `E_TC_BINDING { field, signed, expected }`. A valid signature for asset A
  presented with asset B's metadata therefore fails, whichever field differs.
- **FR-039** `VerifiedArtifact` (D7): private fields `path: PathBuf`, `name: String`, `version: String`, `platform:
  PlatformKey`, `asset: AssetName`, `size: u64`, `sha256: [u8; 32]`, `format: AssetFormat`; read-only accessors; no
  `Default`, `From`, `TryFrom`, `FromStr`, `Deserialize`; `Clone` and `Debug` allowed. A `compile_fail` doctest proves
  it cannot be built outside the module. Only `verify_file` constructs it. Inside `obtain_and`, `verify_file` runs on
  the `.part`; `download` then renames the `.part` to the verified path and hands `next` an artifact whose `path` is
  the FR-003 verified path (a crate-private `VerifiedArtifact::relocated(self, to: PathBuf)` in `verify.rs`, used only
  by `download` after a successful rename), so the `path` `next` sees is always `downloads/<hex>/<asset>`, never the
  `.part` (SC-002, SC-008 c, SC-029).

### E. The release key (roadmap lines 610-611, 239-240; Key decision 4)

- **FR-040** `verify::RELEASE_KEY_TEXT: &str = include_str!("../../../../../tools/feed-contract/keys/krate-signing.pub")`
  (the path form `contract.rs:11-14` uses). `verify::SigningKey` wraps a `minisign_verify::PublicKey` plus its key id
  (`[u8; 8]`, kept because `PublicKey` exposes none, 0.3.0 `src/lib.rs:189-195`) and derives `Clone, Debug, PartialEq,
  Eq`, which `AppContext`'s `#[derive(Clone, Debug, PartialEq)]` (`context.rs:48`) requires; `PublicKey` derives all
  four (External facts), so no manual impl is needed. Two `SigningKey`s are equal when decoded from the same text;
  `pub(crate) fn SigningKey::from_text(&str) -> Result<SigningKey, VerifyError>` (`PublicKey::decode`; a failure is
  `E_VERIFY_KEY_TEXT`; crate-private, so no other crate can turn a key it made into a `SigningKey`) (plan SI-28) and
  `pub fn SigningKey::release() -> SigningKey` (parses `RELEASE_KEY_TEXT`; a failure panics, because
  it is a build input that SC-016 proves parses); `release()` and `key_id_hex()` stay `pub`, because
  `src-tauri/src/lib.rs` calls `release()` (plan SI-28).
- **FR-041** `AppContext` (`context.rs:49-56`) gains `pub signing_key: SigningKey`; `assemble_context` and
  `assemble_with_os` (`context.rs:69-111`) gain a `signing_key: SigningKey` parameter, passed through unchanged;
  `reassemble` keeps the previous value. `src-tauri/src/lib.rs` passes `SigningKey::release()` at its one
  `assemble_context` call (`lib.rs:56`), which then has four top-level arguments, `assemble_context(roots, &feed_url,
  http, SigningKey::release())`; `config_guard.rs`'s argument count (`:831-838`) expects four, its plant (`:1704-1712`)
  rewrites that four-argument text to `assemble_context(roots, &feed_url, http)` and expects "with 3 top-level
  arguments", and `LIB_RS_STRINGS` (`:46-61`) gains `"SigningKey::release()"` (14 → 15) (plan SI-2). Every core test
  builds its context with a throwaway key or the KAT key, `test_support::kat_signing_key()` (which parses
  `tools/feed-contract/fixtures/kat/kat.pub` through `include_str!`; `SigningKey :: release` never appears in
  `test_support.rs`, FR-087) (plan SI-17), so no test depends on the release key except FR-042's.
- **FR-042** The release key is not a test key: `verify::tests::release_key_is_the_committed_key_and_not_a_test_key`
  asserts that `SigningKey::release()`'s 42 decoded bytes equal the base64 line of
  `tools/feed-contract/keys/krate-signing.pub` read at test time, that its key id equals the id in that file's untrusted
  comment, and that neither its key id nor its 32-byte Ed25519 key equals that of `fixtures/kat/kat.pub` or
  `fixtures/phase2/kat-secret/kat.pub` (the Phase 2 plan PD-23 rule, `keys/README.md` section 2).
- **FR-043** Seam guard (in `injection.rs`, test `verified_artifact_has_no_public_constructor`, self-test
  `verified_artifact_scanner_reports_plants`): `pub struct VerifiedArtifact` is declared once, in `verify.rs`, with no
  `pub` field and no derive of `Default` or `Deserialize`; no `impl` of `From`, `TryFrom`, `FromStr`, `Default` or
  `Deserialize` for it exists in the workspace; the struct-literal token sequence `VerifiedArtifact {` occurs in code only
  in `verify.rs`; `relocated` (FR-039) is `pub(crate)` and called only in `download.rs`. The minting routes are pinned
  too (plan SI-28): `verify_file` and `from_text` are each declared once, in `verify.rs`, as `pub ( crate ) fn`;
  `verify_file (` is called only in `verify.rs`, `download.rs`, `verify_tests.rs` and `download_tests.rs`;
  `SigningKey :: from_text (` / `from_text (` only in `verify.rs`, `test_signer.rs`, `test_support.rs`,
  `verify_tests.rs` and `download_tests.rs`; `struct Expected` is declared `pub ( crate ) struct Expected` and holds no
  `pub` field without `( crate )`. The self-test plants each
  violation in an in-memory copy and requires each to be reported, including `pub fn verify_file` and `pub fn
  from_text` in `verify.rs`, a `verify_file(` call and a `SigningKey::from_text(` call in `src-tauri/src/commands.rs`,
  and a bare `pub` field in `Expected` (plan SI-28).

### F. The pipeline, `download::obtain_and` (roadmap lines 604-614, 620-628; D4, D7)

- **FR-045** `download::DownloadEnv<'a>` (`Clone, Copy`; `pub` fields `ctx: &'a AppContext`, `io: &'a dyn StagingIo`,
  `policy: &'a DownloadPolicy`, `cancel: &'a CancelToken`) bundles the inputs every download entry point shares; no
  public function of `download` takes more than 7 parameters and no lint is suppressed for it (Q-B) (plan SI-25).
  `download::obtain_and<T>(env: DownloadEnv<'_>, feed: &Feed, request: &DownloadRequest, progress: &mut dyn
  FnMut(Progress), next: impl FnOnce(VerifiedArtifact) -> T) -> Result<Obtained<T>, DownloadError>` (cancellation:
  FR-017, through `env.cancel`) (plan SI-25), with `pub struct Obtained<T> { pub value: T, pub resumed: bool, pub cached:
  bool, pub transferred: u64 }`, runs, in order: FR-006 exclusion, FR-005 prune, a cached verified file (FR-046), the
  transfer (FR-010 to FR-016 with D4's resume), FR-030's verification of the `.part`, the rename to the verified path,
  then `next` exactly once with the owned artifact. `next` may return the artifact itself (`|a| a`), which is how Phase
  10 collects a batch for one helper job (D7); it may also consume it at once. It is the only public function of
  `download` that returns or passes a `VerifiedArtifact` out (FR-049's `discard` only takes one in), and `next` is never called for a failure.
- **FR-046** Before anything under `downloads/<hex>/` is measured, opened or removed, `StagingIo::kind_of(<hex>)` must
  be `None` (no cached file) or `Some(EntryKind::Dir)`; any other kind (a `<hex>` link the prune could not remove) is
  `E_STAGING_IO`, step `open`, returned with nothing under `<hex>` measured, opened or removed and no `Progress` event
  (plan SI-29). A verified file already at `downloads/<hex>/<asset>` is re-verified (FR-030) before use, with no request;
  if it passes, `next` gets it (`Obtained { resumed: false, cached: true, transferred: 0 }`, progress exactly
  `Started { resumed_from: size }`, `Verifying`, `Verified`); if it fails, the `<hex>` directory is removed, the file is
  never passed to `next`, `Restarted { reason: CachedInvalid }` is emitted after its `Verifying` and before the fresh
  download's first request, and the transfer starts from nothing (one full request, SC-029) (plan SI-9).
- **FR-047** On any verification error the `.part` is removed, no verified path is created, and `next` is not called.
  The single exception is D4's `E_VERIFY_SHA256` after a multi-response transfer, which removes the `.part`, emits
  `Restarted { HashMismatchAfterResume }` and makes one full transfer; if that fails verification too, its error is
  final.
- **FR-048** Downgrades and replays: the binding (FR-038) ties the signature to the entry's version, so an older signed
  asset presented under a newer version fails `E_TC_BINDING { field: "version" }`. Whether an honestly older offered
  version may be installed is not Phase 7's decision (Non-goals; `version.rs:83-94`).
- **FR-049** Release of a used artifact (Q3). `pub fn download::discard(artifact: VerifiedArtifact, io: &dyn StagingIo)
  -> Result<(), DownloadError>` consumes the artifact and removes its `downloads/<hex>/` directory (the artifact's
  `path()` parent, re-derived through `staged_paths` from its `sha256` and `asset`, never from a caller string) through
  `StagingIo::remove_dir_all`, so a later `obtain_and` finds no cached file. It is for Phases 9 and 10 to call after a
  successful install; Phase 7 calls it from nowhere but its tests. It touches no other entry of `downloads/`: a
  `<hex>.part` or another artifact's directory stays, and an in-flight artifact is unaffected: `discard` enters the
  in-flight registry (FR-006) for the artifact's `(downloads, sha256)` before removing `<hex>/` and holds it until the
  removal returns; while an `obtain_and` of the same hash is running it returns `E_DOWNLOAD_BUSY`
  (`DownloadError::DiscardBusy { name, asset }`) with nothing removed, and while `discard` holds the key an
  `obtain_and` of that hash is `E_DOWNLOAD_BUSY` (FR-006) (plan SI-20). A directory that is
  already gone is `Ok(())`; any other removal failure is the FR-051 code with step `remove` (`E_STAGING_DENIED` /
  `E_STAGING_FULL` / `E_STAGING_IO`), returned, not swallowed. FR-005's prune rule is unchanged: the prune still keeps
  every artifact the feed offers, and `discard` is the only way a consumer releases one earlier. `discard` is the one
  other public `download` function that takes a `VerifiedArtifact` (it can only drop one, never produce one), so FR-043's
  guard is unaffected. Proven by SC-035.

### G. Staging I/O and errors (roadmap lines 628, 620-622; D11, D12)

- **FR-050** `download::StagingIo` (object safe): `create_dir_all(dir)` (creates, then requires `symlink_metadata(dir)`
  to be a directory, else `Err(InvalidInput)`: a linked `downloads/` or `<hex>` is refused, step `create_dir`),
  `kind_of(path) -> io::Result<Option<EntryKind>>` (`fs::symlink_metadata`, never following the final link: `NotFound`
  → `None`, else the `EntryKind` of its `file_type()`), `len_of(path) -> io::Result<Option<u64>>` (the one existence and
  length probe of a file: `fs::symlink_metadata`, `NotFound` → `None`, anything that is not a regular file →
  `Err(InvalidInput)`; its failures are reported as step `open`), `open_partial(path, truncate: bool) -> Box<dyn
  PartialFile>` (append position = current length), `PartialFile::{write_all, sync}` (no `len`: the download tracks
  `on_disk` from `len_of` plus what it wrote), `rename(from, to)`, `remove_file`, `remove_dir_all`, and
  `read_dir_entries(dir) -> io::Result<Vec<StagedEntry>>` with `StagedEntry { name: OsString, kind: EntryKind }` and
  `EntryKind = Dir | File | Other` (from `DirEntry::file_type()`, which does not follow a link, so a symlink or a
  junction is `Other`) (plan SI-10) (plan SI-23) (plan SI-29). The prune removes an `Other` entry with `remove_file`,
  which removes the link itself, never its target, and on Windows only, after that `remove_file` fails (std cannot
  remove a directory symlink or a junction with it there), once more with `remove_dir_all`, which also removes the link
  itself; only the second failure is logged (plan SI-29). `FsStagingIo` implements the trait with `std::fs`. `read_dir`
  appears in `download.rs` only inside `FsStagingIo::read_dir_entries` (guard FR-082) (plan SI-10).
- **FR-051** A staging I/O error is `E_STAGING_FULL` when its kind is `StorageFull` or `QuotaExceeded`,
  `E_STAGING_DENIED` when `PermissionDenied` or `ReadOnlyFilesystem`, else `E_STAGING_IO`; each carries the step
  (`create_dir`, `open`, `write`, `sync`, `truncate`, `rename`, `remove`) and the kind. On `write`, `sync` or `truncate`
  the partial is removed (best effort; a failed removal is logged, not raised); on `rename` the complete `.part` is kept
  (D11). The `truncate` step is `open_partial(path, truncate: true)`, used when a `200` answers a ranged request (D4)
  or a partial is discarded. No staging error ever leaves a file at the verified path, and `next` is never called after
  one. A process killed between any two `StagingIo` steps leaves either nothing, a `.part` the next `obtain_and`
  resumes, re-verifies or discards (D4), or a verified directory that passed verification before the rename; the next
  `obtain_and` ends verified (SC-011's crash cases).
- **FR-052** `download::DownloadError` and `verify::VerifyError` (`Clone, Debug, PartialEq, Eq, thiserror::Error`) with
  `code() -> &'static str`; `DownloadError::Verify { target, error }` (a struct variant carrying the transfer's
  `Target { name, asset, url }`, the feed `AssetUrl`, so a verification error's `Display` names the feed URL too,
  SC-030 c) reports `error.code()`, and every transfer variant carries `target: Box<Target>` (plan SI-13);
  `DownloadError::DiscardBusy { name, asset }` (code `E_DOWNLOAD_BUSY`) is FR-049's refusal; it names the plugin and
  the asset but no feed URL, because a `VerifiedArtifact` carries none (plan SI-20). The full code set: the
  `E_DOWNLOAD_*` codes of FR-001, FR-003, FR-006, FR-013, FR-016 (`UNKNOWN_PLUGIN`, `UNAVAILABLE`, `PATH`, `BUSY`,
  `ENCODING`, `SIZE`, `OFFLINE`, `TIMEOUT`, `STALLED`, `TLS`, `URL`, `REDIRECT`, `HTTP_STATUS`, `BODY`, `CANCELLED`),
  FR-060's `E_DOWNLOAD_NO_FEED` and `E_DOWNLOAD_TASK`, FR-065's `E_DOWNLOAD_VERSION_CHANGED`, the
  `E_STAGING_*` codes of FR-051, and the `E_VERIFY_*` / `E_TC_BINDING` codes of FR-031 to FR-038 and FR-040.
- **FR-053** Logging (`log` target `krate_manager_core::download`): one `info!` line per successful `obtain_and` (plugin,
  version, platform key, bytes transferred, whether it resumed, whether a cached file was used) and one `warn!` line per
  failure (its code and the feed URL). No log line and no error `Display` holds a redirect target or a `Location` value
  (D12); SC-030 proves it on a success and on three failures behind a redirect, one of them a transport `detail`, and
  pins the per-run counts (one `info!` on success, one `warn!` on failure, at this target) and the `info!` fields
  (plan SI-22).

### H. The Tauri command (roadmap line 606; D9)

- **FR-060** The command's logic is core, in three functions of `download` (D9):
  `command_request(ctx: &AppContext, name: &str, expected_version: &str, now_unix: u64, host: &dyn HostFacts) ->
  Result<(Feed, DownloadRequest), DownloadError>` runs `feed::check(ctx, FetchMode::Normal, now_unix)` (no snapshot →
  `E_DOWNLOAD_NO_FEED`, added to FR-052's set, naming the check's error code), reads `host.os_version()` once, calls
  `request_for` and then applies FR-065's version check;
  `run_command(env: DownloadEnv<'_>, feed: &Feed, request: &DownloadRequest, progress: &mut dyn FnMut(Progress)) ->
  Result<DownloadSummary, DownloadError>` calls `obtain_and` with a
  `next` that reads the artifact's name, version, platform, asset and size, and fills `resumed` from `Obtained`; and
  `for_command(env: DownloadEnv<'_>, name, expected_version, now_unix, host, progress)` is `command_request` (which is
  unchanged and is passed `env.ctx`) then `run_command`, nothing else (plan SI-25). `src-tauri/src/commands.rs`
  gains `#[tauri::command] pub async fn download_plugin(state: State<'_, SharedState>, name: String, expected_version:
  String, on_progress: tauri::ipc::Channel<Progress>) -> Result<DownloadSummary, String>`, a thin wrapper like `status_for`
  (`commands.rs:31-44`): clone the `AppContext` out of `SharedState` and drop the guard before any await, then in
  `spawn_blocking` one call `download::for_command(DownloadEnv { ctx: &ctx, io: &FsStagingIo, policy: &policy, cancel:
  &cancel }, &name, &expected_version, now_unix, &PlatformFacts { io: &OsHostIo }, &mut
  download::channel_progress(|e| on_progress.send(e)))`, with `policy = DownloadPolicy::production()` and `cancel =
  CancelToken::new()` bound inside the closure (plan SI-25); no code path of Phase 7 sets the token (FR-017). Errors are
  returned as their `Display` text (code first); a join error is `E_DOWNLOAD_TASK`. The wrapper holds no other logic,
  so SC-027 and SC-028 test everything but the two Tauri calls.
- **FR-061** `build.rs` registers six commands and `capabilities/main.json` grants six permissions (`allow-download-plugin`
  added); `lib.rs` `generate_handler!` lists six; `config_guard.rs` asserts exactly these sets, expects four top-level
  arguments at `lib.rs`'s `assemble_context(` (plant: the three-argument `assemble_context(roots, &feed_url, http)` →
  "with 3 top-level arguments"), pins `"SigningKey::release()"` in `LIB_RS_STRINGS` (15 strings), and gains a check that
  the squeezed `lib.rs` holds `generate_handler![commands::app_version,commands::startup_error,commands::plugin_status,
  commands::check_now,commands::open_release_page,commands::download_plugin]` (plant: `commands::download_plugin`
  removed → reported) (plan SI-2). The CSP and the webview's
  remote-content rules are unchanged. There is no seventh command and no `cancel_download` permission in Phase 7
  (Q2); the guard's expected sets contain exactly the six and fail on a seventh.
- **FR-062** `DownloadSummary { name, version, platform, asset, size, resumed }` (`Serialize`) carries no path, no URL
  and no hash: its `serde_json` keys are exactly those six, and no value holds a path separator, `://` or a 64-hex-digit
  run (SC-028). The command accepts no argument other than the feed key, `expected_version` and the channel;
  `expected_version` is only compared with the entry's `version_text` (FR-065) and is never a path, URL or request
  field. A name that is not a feed key is `E_DOWNLOAD_UNKNOWN_PLUGIN`, decided before the version comparison.
- **FR-063** No TypeScript caller is added in Phase 7 (D9); `src/` is unchanged except where a type-check of the
  existing files requires nothing (no change expected).
- **FR-064** The `Channel` adapter (roadmap line 606) is the core function `download::channel_progress<E>(send: impl
  FnMut(Progress) -> Result<(), E>) -> impl FnMut(Progress)`: each event is passed to `send` unchanged, in the order
  received, on the calling thread; a `send` error is dropped (the webview may have gone away) and never aborts or
  slows the download, and later events are still offered to `send`. No Tauri type is named in core: `src-tauri` passes
  `|e| on_progress.send(e)`, whose `tauri::Result<()>` fits `Result<(), E>` (`Channel::send`, External facts). Proven by
  SC-027.
- **FR-065** Expected version (Q1). After `request_for` succeeds, `command_request` compares `request.version_text` with
  `expected_version` byte for byte; a difference is `E_DOWNLOAD_VERSION_CHANGED` (`DownloadError::VersionChanged {
  expected, found }`, both strings from the version text grammar, no path or URL), returned before `obtain_and` runs,
  so no prune, no staging I/O, no `Progress` event and no asset request happen under the changed feed. An equal
  version proceeds exactly as before. The re-fetch itself (FR-060's `feed::check`) is unchanged: no snapshot is kept in
  `AppState` and `context.rs` and Phase 6's `status_for` are not changed. Proven by SC-028 (e).
- **FR-066** Permanence of the command (Q6). `download_plugin` is a permanent webview command: Phase 7 does not mark it
  internal, feature-gate it or schedule its removal, and the six-command / six-permission sets of FR-061 are the
  contract the later phases extend. Its result is deliberately pathless (FR-062), so a later install command obtains
  the cached verified file through `obtain_and` (which re-verifies it with no request, FR-046) rather than through a
  path the webview holds. The two-step flow (bounded parallel downloads, then one install batch over the cached files)
  is Phase 9, 10 and 12's; Phase 7 only guarantees the pieces it needs: independent artifacts download in parallel
  (D10, SC-014), a second `obtain_and` for a cached artifact makes no request (SC-029) and an obtained artifact can be
  held for a batch (SC-026).

### I. Dependencies, guards, fixtures and lanes (roadmap lines 615-617; Cross-Cutting lines 980-1021)

- **FR-080** Workspace pins: `minisign-verify = "0.3.0"`, `sha2 = "0.11.1"`, `minisign = "0.10.0"`. `crates/core/Cargo.toml`
  `[dependencies]` gains `minisign-verify` and `sha2` (`workspace = true`); `[dev-dependencies]` gains `minisign`.
  `injection.rs` `FR007_DEPENDENCIES` becomes 13 names and the dev-dependency assertion `{flate2, minisign}`. The
  `core dependency tree` step (`manager-ci.yml:122-136`) still lists no excluded crate on any leg.
- **FR-081** Neither `minisign` nor any other signing-capable crate is a normal dependency of any workspace member:
  the normal-dependency closure of `krate-manager-core`, `krate-manager-helper` and `krate-manager` holds no package
  named `minisign` (`minisign-verify` is expected), `rpassword` or `scrypt`. Measurement method, chosen because
  `Cargo.lock` records no dependency kind (core's entry lists the dev-only `flate2` beside its normal deps,
  `Cargo.lock:1959-1977`) and because Phase 7 may not change `manager-ci.yml` (FR-090): `config_guard.rs` test
  `core_has_no_signing_crate` reads each member's `Cargo.toml`, takes as roots only the keys of `[dependencies]` and of
  every `[target.'…'.dependencies]` section (never `[dev-dependencies]`; `[build-dependencies]` are included, which is
  stricter), and walks the committed `Cargo.lock` graph from those roots (a lock dependency string `"<name>"` or
  `"<name> <version>"` resolves to its `[[package]]` entry; a registry package's lock entry lists its normal and build
  dependencies only). Any reached package with a forbidden name is a finding. Its self-test
  `signing_crate_scan_reports_plants` plants, in in-memory copies, a `minisign` key in core's `[dependencies]`, an
  `rpassword` edge under a reached lock entry, and a `minisign` key in core's `[dev-dependencies]` (which must **not**
  be reported, proving the walk ignores dev-dependencies). The `core dependency tree` CI step is unchanged.
- **FR-082** Confinement (in `injection.rs`, tests `verify_crates_are_confined` and
  `download_and_verify_hold_no_url_or_whole_read`, each with a self-test that plants one violation per rule): the path
  `minisign_verify` only in `verify.rs`; `sha2` only in `verify.rs`; `minisign ::` (the dev crate) only in
  `test_signer.rs`, `verify_tests.rs` and `download_tests.rs`, never in `test_support.rs` (D1); `test_signer` declared
  in `lib.rs` only as `#[cfg(test)] mod test_signer;`; `ureq` still only in `http.rs` / `http_tests.rs`;
  `download.rs` and `verify.rs` hold no `"http://"` or `"https://"` literal, no `read_to_end`, `read_to_string`,
  `fs :: read (`; `read_dir` only once in `download.rs`, inside `FsStagingIo::read_dir_entries` (FR-050) (plan SI-10);
  `CONFINED_TOML_KEYS` gains `minisign-verify`, `sha2` and `minisign`, each confined to the two files `Cargo.toml` and
  `crates/core/Cargo.toml` (the table confines a key per file, not per section). `minisign`'s "`[dev-dependencies]`
  only" is enforced by `core_dependencies_are_the_fr007_set` (the normal set is the 13 names of FR-080, which exclude
  `minisign`; the dev set is exactly `{flate2, minisign}`) and by FR-081's walk, whose self-test plants `minisign` in
  core's `[dev-dependencies]` explicitly and requires it not to be reported (plan SI-21). Because `sha2` is confined to
  `verify.rs`, every test that needs a SHA-256 (the FR-085 test feeds, SC-026's and SC-031's hashes of files on disk)
  takes it from `verify::sha256_of(bytes: &[u8]) -> [u8; 32]`, a `#[cfg(test)] pub(crate)` function of `verify.rs`
  (plan SI-5).
- **FR-083** Test keys (roadmap lines 615-617): every Phase 7 signature a test checks is made in the test process by
  `test_signer::TestSigner` (wrapping `minisign::KeyPair::generate_unencrypted_keypair` and `minisign::sign` with an
  explicit C3 trusted comment), except the two committed rsign2 known answers (FR-085). `TestSigner` lives in
  `crates/core/src/test_signer.rs`, declared `#[cfg(test)] mod test_signer;`, because `minisign` is a dev-dependency
  that core's lib does not have when it is built with `test-support` for src-tauri's tests (D1, `lib.rs:22-23`,
  `src-tauri/Cargo.toml:33-34`); its only users are `download_tests.rs` and `verify_tests.rs`. `test_support.rs` keeps
  only the minisign-free helpers (the feed builder, which takes signature text as input, and the fault-injecting
  `StagingIo`). No test writes a key, public or
  secret, to any file. Guard `no_secret_key_in_app_tree` (in `injection.rs`): no tracked file under
  `apps/krate-manager/` (outside `target/`, `node_modules/`, `dist/`) holds a line starting `untrusted comment:` that
  contains `secret key`, and no test source holds the tokens `generate_and_write_encrypted_keypair`,
  `generate_and_write_unencrypted_keypair` or `generate_encrypted_keypair`. Because the release key is made with
  `tauri signer generate`, whose key files are one base64 line of a standard minisign file with no visible `untrusted
  comment:` or `secret key` text (`specs/krate-manager/probes/results.md:10`, `:27`), the guard also reports (1) a file
  whose name ends in `.key` (Phase 2 rule (a), `tools/feed-contract/tests/c3-trusted-comment.test.js:211-228`); (2)
  every run of the STANDARD base64 alphabet of ≥ 40 characters that decodes canonically is decoded and its text
  checked by the plaintext rule above, plus Phase 2 rule (c) (a line ending `secret key` whose next line decodes to
  `Ed` at bytes 0-1 and `B2` at bytes 4-5); (3) `TAURI_SIGNING_PRIVATE_KEY` followed by `=` or `:` and a non-empty value
  that does not start `${{ secrets.` (plan SI-22). Its self-test `no_secret_key_scanner_reports_plants` plants each
  violation, every plant built at run time so that `injection.rs`'s own text holds none of them, and requires a
  `${{ secrets.TAURI_SIGNING_PRIVATE_KEY }}` reference and a base64 public-key text not to be reported (plan SI-22).
- **FR-084** `fixture_server.rs` gains `Reply::Ranged { body: Vec<u8> }` (no `Range` header → `200` with the body and
  `Content-Length`; `Range: bytes=<n>-` with `n < len` → `206`, `Content-Range: bytes <n>-<len-1>/<len>`, the tail;
  `n ≥ len` → `416`), `Reply::Script(Vec<Reply>)` (the k-th request to the route gets the k-th reply, the last one
  repeats) and `Reply::Paced { body: Vec<u8>, chunk: usize, interval: Duration }` (`200` with `Content-Length`, the body
  sent `chunk` bytes at a time with `interval` between chunks; used by SC-037). Its existing replies and tests are unchanged. `fixture_server::tests` gain `ranged_cases` and
  `script_advances_per_request`. Heads that `Ranged` cannot produce (a close-delimited `200` with no `Content-Length`,
  a `206` with a deliberately wrong `Content-Range`) use the existing `Reply::Garbage(bytes)`, which sends its bytes as
  given (`fixture_server.rs:310-312`); the SC rows that need one spell out the exact bytes (SC-003 d, SC-008 b).
- **FR-085** Fixtures are read in place, never copied or regenerated: `tools/feed-contract/fixtures/kat/` (the archive,
  `.minisig`, `kat.pub`) and `tools/feed-contract/fixtures/phase2/kat-secret/` (`message.txt`,
  `message.txt.minisig`, `kat.pub`; the committed `kat.key` is never read). The expected SHA-256 of the KAT archive is
  the archive's own, `5b980e5b8ae7811a064966a1ec1380df016081a12bdeaf643486026007b62890` as recorded in
  `fixtures/kat/README.md`, recomputed in the test with `verify::sha256_of` and the two compared; the `.sha256` file is
  not read (it is the C4 listing digest, Phase 8's payload digest), and `message.txt`'s SHA-256 is computed the same way
  (no README records it) (plan SI-1). Downloadable test assets are generated at
  test time (seeded pseudo-random bytes, sizes that are not multiples of 64 KiB) and signed by `TestSigner`; test feeds
  are built in memory by a `test_support` builder that goes through `feed::parse` (so every feed a test uses passes the
  contract). Their asset URLs are C1 `https://` URLs on a reserved name (`https://assets.example.invalid/<tag>/<asset>`;
  the host never resolves, and the loopback policy would refuse it before any connection, FR-087), and each test points
  the transfer at its own fixture server through D13's seam, `request.with_fixture_url(&server.url("/<asset>"))`. The
  seam is tested by `download::tests::fixture_url_seam_cases` and guarded by `fixture_url_seam_is_test_only` (SC-032).
- **FR-086** `tests/required-tests.json`: every new Rust test named in SC-002 to SC-020 and SC-026 to SC-037 joins
  `cargo.all`, together with the guard self-tests `no_secret_key_scanner_reports_plants`,
  `phase7_host_scanner_reports_plants`, `verify_confinement_scanner_reports_plants` and
  `download_verify_scanner_reports_plants` and the three plan tests `verify::tests::verify_steps_stop_at_first_failure`,
  `verify::tests::signing_key_text_cases` and `http::tests::streamed_body_cases` (plan SI-14), the Q-B guard
  `too_many_arguments_is_never_suppressed` (plan SI-26), and `download::tests::prune_spares_in_flight_artifact` and
  `download::tests::non_retryable_transport_errors` (plan SI-30); the OS-gated ones join their OS list instead, and the
  three lists are exactly (plan SI-14) (plan SI-23) (plan SI-29) (plan SI-30): `windows`:
  `download::tests::prune_skips_a_file_open_without_share_delete_windows`,
  `download::tests::verified_file_has_no_zone_identifier_windows`,
  `download::tests::staged_links_are_not_followed_windows`; `macos`: `download::tests::readonly_staging_dir_unix`,
  `download::tests::verified_file_has_no_quarantine_macos`,
  `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix`, `download::tests::staged_symlinks_are_not_followed`;
  `linux`: `download::tests::readonly_staging_dir_unix`,
  `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix`, `download::tests::staged_symlinks_are_not_followed`.
  TA-1's `TEST_PROGRAM_BODIES` (`injection.rs:341-344`) gains `(download_tests.rs, staged_links_are_not_followed_windows,
  "cmd")` beside FR-087's `/usr/bin/xattr` row (plan SI-4), because that test makes NTFS junctions with `cmd /C mklink /J`
  (plan SI-29). The
  `compile_fail` doctest of SC-015 is not listed: the `required tests ran` regex cannot match a doctest line (SC-015).
  No `vitest` change.
- **FR-087** Tests run in parallel (cargo's default threads); no new test uses `--test-threads`, a global mutex, a fixed
  port or a sleep longer than 1 s. Every new test that makes a request builds its `HttpConfig` with
  `HttpConfig::loopback(…)`, so a non-loopback URL is refused before any connection (`http.rs:293-299`). Guard
  `phase7_tests_hold_no_real_host` scans `download_tests.rs`, `verify_tests.rs`, `test_signer.rs` and `test_support.rs` for `github.com`,
  `githubusercontent.com`, `krateaudio.com`, `HttpConfig :: production`, `SigningKey :: release` (outside FR-042's one
  test body) and `--test-threads` and requires 0 hits; a missing file is a finding. The existing test-body rules of
  `injection.rs` admit the three Phase 7 tests that need them, each in its one body: `TEST_PROGRAM_BODIES` (TA-1,
  `injection.rs:341-344`) gains `(download_tests.rs, verified_file_has_no_quarantine_macos, "/usr/bin/xattr")`,
  `PAC_BODIES` (`:304-317`) gains `(http_tests.rs, streamed_pac_fetch_keeps_the_feed_bound_unix)` and
  `PRODUCTION_BODIES` (`:284-294`) gains `(http_tests.rs, streamed_policy_cases)` (the `Download` agent's `https_only`
  under `HttpsOnly` is asserted with the production config) (plan SI-4).
- **FR-088** ROSTER (roadmap line 33): the Phase 5 roster guard (`roster_and_hosts_absent_from_sources`, `roster.rs:130`) and Phase 6's
  identifier guard (`install_identifiers_absent_from_sources`, `roster.rs:270`) cover `download.rs` and `verify.rs`: the guard's file list gains both.
  No plugin name, bundle name, AppId or pkg identifier appears in them.

### J. Scope fences

- **FR-090** Phase 7 changes nothing under `plugins/`, `dsp/`, `cmake/`, `extern/`, `.github/workflows/` and nothing in
  `tools/feed-contract/` except `tests/manager-ci-shape.test.js`'s `EXPECTED_REQUIRED` (FR-091).
- **FR-091** Files outside `apps/krate-manager/**` this phase may change: `specs/krate-manager-phase7/**`;
  `specs/Krate-manager-roadmap.md`, only the Phase 7 status line; `specs/INDEX.md`, regenerated with
  `node tools/gen-specs-index.js` so `ci.yml:172`'s `--check` stays green; and
  `tools/feed-contract/tests/manager-ci-shape.test.js`, whose `EXPECTED_REQUIRED` (`:337`) gains exactly FR-086's names,
  nothing removed, because `checkRequired` (`:739-748`) deep-compares it with `required-tests.json`.

---

## Success Criteria

Rust SCs run locally on Windows with `cargo test --workspace --locked` from `apps/krate-manager/` (default test
threads); a run counts only with every binary reporting `0 failed`. macOS and Linux rows are **PENDING-CI** on
`manager-ci.yml` jobs `check (macos-latest)` and `check (ubuntu-24.04)`, and close on a leg only when (1) the run's
committed `tests/required-tests.json` lists the row's test names, (2) the leg's `required tests ran` step passed with no
`::error title=required tests ran (<os>)::…` line (`manager-ci.yml:153-177`), and (3) its `::notice` counts equal the
file's list lengths. Every network test talks only to `fixture_server` on `127.0.0.1`. Every "named error" below is
asserted by `code()`, never by message text.

- **SC-001 (Windows local gate, zero warnings).** From `apps/krate-manager/`: `cargo fmt --all --check`; `cargo clippy
  --workspace --all-targets --locked -- -D warnings`; `cargo clippy -p krate-manager --all-targets --features dev-feed
  --locked -- -D warnings`; `cargo test --workspace --locked`; `npm run typecheck`, `npm run lint`, `npm test`;
  `node ../../tools/lint-manager-version.js`; `npm run tauri build`; and from the repo root `node --test
  tools/feed-contract/tests/manager-ci-shape.test.js`. Threshold: every command exit 0, 0 `warning:` lines, no
  `#[allow` / `@ts-ignore` / `eslint-disable` added without a one-line reason, every Phase 4-6 test still passing;
  no `#[expect(` or `#[allow(` naming `clippy::too_many_arguments` (or `clippy::complexity`, `clippy::all`, `warnings`)
  anywhere in the workspace, no lint table lowering it and no `too-many-arguments-threshold` in a clippy config file,
  proven by `too_many_arguments_is_never_suppressed` (plan SI-26).
  PENDING-CI: the same steps green on `check (windows-latest)`, `check (macos-latest)`, `check (ubuntu-24.04)`.
- **SC-002 (a good artifact verifies end to end).** `download::tests::good_artifact_verifies`: a 1,048,593-byte seeded
  asset signed by `TestSigner` with the C3 comment of a test feed entry (`Alpha`, `1.2.3`, the native platform key, asset
  `Alpha-1.2.3-test.bin`, feed URL `https://assets.example.invalid/alpha/Alpha-1.2.3-test.bin`), served as
  `Reply::Ranged` at `/Alpha-1.2.3-test.bin` and reached through `request.with_fixture_url(…)` (D13, FR-085).
  `obtain_and` returns `Ok` with `Obtained { resumed: false, cached: false, transferred: size }`; `next` ran exactly
  once and saw `size`, `sha256`, `name`, `version`, `platform` and `asset` equal to the feed's, and
  `artifact.path() == downloads/<hex>/<asset>`; while `next` ran, that file existed and `downloads/<hex>.part` did not;
  the file at the verified path has the served bytes; no `.part` remains; the server recorded exactly one request, with
  `Accept-Encoding: identity`, `Accept: application/octet-stream`, the manager `User-Agent` and no `Range`. The recorded
  run passes `assert_progress_invariants(events, size, outcome)`, the one shared helper of `download_tests.rs` that
  asserts FR-021's order and FR-022's rules on a recorded run (plan SI-24). Threshold:
  every assertion. Local; PENDING-CI (`all`).
- **SC-003 (every rejection is named and never reaches a next stage).** `download::tests::rejections_never_reach_next_stage`,
  one row per case, each with a fresh temp root, a contract-valid feed (FR-085) and two counting continuations standing
  in for Phase 8's extraction and Phase 10's install driver (the `next` closure increments `extract` when the
  artifact's format is `tar.gz` and `install` otherwise; the native platform key fixes the format per OS, so the Linux
  leg exercises `extract` and the Windows and macOS legs `install`, and every row asserts both counters): (a) one byte
  of the served asset flipped, feed `sha256` of the original → `E_VERIFY_SHA256`; (b) the same flip with the feed
  `sha256` of the flipped bytes → `E_VERIFY_SIGNATURE`; (c) feed `size` one more than served (server sends
  `Content-Length` = served) → `E_DOWNLOAD_SIZE` before any body byte is written; (d) served body one byte longer than
  feed `size` with no `Content-Length`, as `Reply::Garbage` of exactly `HTTP/1.1 200 OK\r\nContent-Type:
  application/octet-stream\r\nConnection: close\r\n\r\n` followed by `size + 1` asset bytes, then close (a
  close-delimited body) → `E_DOWNLOAD_SIZE`; the bytes written through a counting wrapper around `FsStagingIo` total at
  most `size`, and the `.part` is removed (FR-014); (e) feed `sha256` of other bytes → `E_VERIFY_SHA256`; (f) signed by
  a second `TestSigner` (other key id) → `E_VERIFY_KEY_ID`; (g) signed by a second key whose key id was set equal to the
  context key's (the 8 id bytes copied into the signature blob before re-encoding) → `E_VERIFY_SIGNATURE`; (h) a valid
  signature bound to plugin `Bravo` → `E_TC_BINDING { field: "name" }`; (i) bound to version `1.2.2` → `E_TC_BINDING {
  field: "version" }`; (j) bound to another defined platform key → `{ field: "platform" }`; (k) bound to another asset
  name → `{ field: "asset" }`; (m) the trusted-comment line of a valid signature replaced after signing by another
  C3-valid comment (the same fields with version `1.2.4`; the feed still parses, `contract.rs:868-890`) →
  `E_VERIFY_SIGNATURE`. Row (l) of revision 1 (a `timestamp:1` comment) is not reachable through a contract-valid feed
  (FR-037) and is `verify::tests::invalid_trusted_comment_is_named` in SC-004. Threshold: for all 12 rows, the expected
  code, `extract == 0` and `install == 0`, no file at the verified path, no `.part` left, and no `Verified` progress
  event. Local; PENDING-CI (`all`).
- **SC-004 (the rsign2 known answers; Rust agrees with rsign2 and Node).** `verify::tests::kat_verifies_and_one_byte_changes_fail`:
  `verify_file` over `fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz` with `kat.pub`, `sha256` the archive's SHA-256
  `5b980e5b8ae7811a064966a1ec1380df016081a12bdeaf643486026007b62890` (recorded in `fixtures/kat/README.md`, and first
  asserted equal to `verify::sha256_of` of the archive read in place; the `.sha256` file is not read, because it holds
  the C4 listing digest `db287e49…`, Phase 8's payload digest; `message.txt`'s SHA-256 is computed the same way)
  (plan SI-1), the
  `.minisig` text and the binding `KatPlugin` / `1.2.3` / `linux-x86_64` / the archive name → `Ok`. Copies (in a temp
  dir) with (a) one archive byte changed → `E_VERIFY_SHA256`, and with the hash recomputed → `E_VERIFY_SIGNATURE`; (b)
  one base64 character of the signature line changed inside the 64-byte signature part, still canonical →
  `E_VERIFY_SIGNATURE`; (c) the trusted comment changed to `…:1.2.4:…` → `E_VERIFY_SIGNATURE`. `verify::tests::kat_secret_message_verifies`:
  `phase2/kat-secret/message.txt` with its `.minisig`, `kat.pub` and binding `KatPlugin` / `1.2.3` / `linux-x86_64` /
  `message.txt` → `Ok`. Together with Phase 2's `tools/minisign.js` KAT test and its rsign2 ↔ Node interop
  (`feed-tools.yml`, roadmap lines 403-405), the rsign2, Node and Rust paths accept the same signature.
  `verify::tests::invalid_trusted_comment_is_named` (FR-037, step 7 reached directly): a temp file signed by
  `TestSigner` with the trusted comment `timestamp:1`, checked by `verify_file` with a hand-built `Expected` whose size,
  hash and signature text match → `E_VERIFY_TRUSTED_COMMENT` with inner `E_TC_FIELDS`; and a C3-shaped comment with an
  invalid name field → inner `E_TC_NAME`. Threshold: every case. Local; PENDING-CI (`all`).
- **SC-005 (progress).** `download::tests::progress_is_monotonic_and_ends_at_size`: over SC-002's download, the events are
  `Started { total: size, resumed_from: 0 }`, then ≥ 4 `Bytes`, then `Verifying`, then `Verified`; `received` strictly
  increases and equals `on_disk` in every `Bytes`, every step but the last is ≥ 256 KiB, the last `Bytes` has
  `received == on_disk == size`; no `Restarted` and no `Retrying`. Pure core test, no Tauri. Threshold: every assertion. Local;
  PENDING-CI (`all`).
- **SC-006 (an interrupted download resumes and verifies).** `download::tests::interrupted_download_resumes`: route
  `Script([Truncated { declared_len: size, sent: first 400,000 bytes }, Ranged])`. Result `Ok` and verified; the second
  recorded request carries `Range: bytes=400000-`; the server sent `size` body bytes in total across both requests;
  progress: one `Started { resumed_from: 0 }`, `Bytes` strictly increasing across the resume, no `Restarted`, and exactly
  one `Retrying { attempt: 1, of: 4, code: "E_DOWNLOAD_BODY", wait_ms: 0 }` (zero-backoff policy) between the two
  `Bytes` runs (FR-023).
  `download::tests::partial_from_a_previous_run_resumes`: a `.part` of 400,000 correct bytes left in staging, then
  `obtain_and` → first request has `Range: bytes=400000-`, `Started { resumed_from: 400000 }`, result verified.
  `download::tests::stall_resumes`: `Script([HeadersThenStall { declared_len: size, sent: 300,000 bytes, stall: 5 s },
  Ranged])` with `policy.stall = 200 ms` → the first attempt is cut by the stall bound (its code is not observable in an
  `Ok` result, FR-016; SC-009 f proves it is `E_DOWNLOAD_STALLED`), the second carries `Range: bytes=300000-`, result
  verified. Every recorded run of the three tests passes `assert_progress_invariants(events, size, outcome)`
  (plan SI-24). Threshold: every assertion; each test < 5 s. Local; PENDING-CI (`all`).
- **SC-007 (a server that ignores `Range` falls back to a full download).** `download::tests::ignored_range_falls_back_to_full`:
  `Script([Truncated(400,000 of size), Ok { body }])` → the second response is `200`; progress shows one `Retrying`
  (FR-023), then one `Restarted {
  RangeIgnored }` followed by `Bytes` whose `on_disk` starts at a value ≤ 256 KiB and rises to `size`, while `received`
  never decreases across the whole run (it holds at its high-water mark until `on_disk` passes it) and ends at `size`
  (D8); result verified; the verified file equals the served bytes (no duplicated prefix).
  `download::tests::truncate_failure_after_ignored_range`: the same route with an injected `StorageFull` at the
  `truncate` step (`open_partial(…, truncate: true)`) → `E_STAGING_FULL { step: "truncate" }`, the `.part` removed,
  `next` not called, nothing at the verified path. Every recorded run passes `assert_progress_invariants(events, size,
  outcome)` (plan SI-24). Threshold: every assertion. Local; PENDING-CI (`all`).
- **SC-008 (range edge cases).** `download::tests::range_edge_cases`: (a) `416` to the ranged request → one full request,
  `Restarted { RangeRefused }`, verified; (b) route `Script([Truncated(400,000 of size), Garbage(<head> + <full body>),
  Ranged])`, where `<head>` is exactly `HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-<size-1>/<size>\r\n
  Content-Length: <size>\r\nConnection: close\r\n\r\n` (no line break after `\r\n`; the deliberately wrong range for a
  `bytes=400000-` request) → `RestartFull`: no body byte of the 206 is written, `Restarted { RangeRefused }`, the third
  request carries no `Range`, verified; (c) a complete correct `.part` (exactly `size` bytes) → zero requests,
  verified, and `next` sees `artifact.path() == downloads/<hex>/<asset>` with no `.part` left; (d) a `.part` of `size +
  10` bytes → discarded (`Restarted { PartialTooLong }`), one full request, verified; (e) a `.part` whose first 400,000
  bytes are wrong, then `Ranged` → `Restarted { HashMismatchAfterResume }` (the first pass failed `E_VERIFY_SHA256`),
  one full request, verified, `received` never decreasing (D8); (f) as (e) but the full request serves wrong bytes too
  → final `E_VERIFY_SHA256`, total requests 2, `next` not called. `download::tests::classify_head_table` covers FR-013's verdict table with no socket (every
  status, `Content-Range` and `Content-Length` combination listed there, plus `Content-Encoding: gzip` →
  `E_DOWNLOAD_ENCODING` and `Content-Encoding: IDENTITY` accepted). Every recorded run of rows (a) to (f) passes
  `assert_progress_invariants(events, size, outcome)` (plan SI-24). Threshold: every cell. Local; PENDING-CI (`all`).
- **SC-009 (bounded retries).** `download::tests::retries_are_bounded`: (a) `Close` on every request → exactly 4
  requests, `E_DOWNLOAD_OFFLINE`; (b) `Script([Status(503), Status(503), Ranged])` → `Ok` after 3 requests; (c)
  `Status(404)` → 1 request, `E_DOWNLOAD_HTTP_STATUS { status: 404 }`; (d) `Status(429)` four times → 4 requests; (e)
  four rows, each with no `Retrying` and `next` called only in (e2) (plan SI-18): (e1) `Reply::Garbage` with the exact
  head `HTTP/1.1 200 OK`, `Content-Length: <size>`, `Content-Encoding: br`, `Connection: close` and the asset bytes → 1
  request, `E_DOWNLOAD_ENCODING`, 0 bytes written, no `.part`; (e2) `Reply::Gzip { body: gzip(asset) }` → `Ok`,
  verified, 1 request, the file equals the asset; (e3) `Reply::Gzip { body: gzip(other bytes of length size) }` → 1
  request, `E_VERIFY_SHA256`, no `.part`; (e4) `Reply::Gzip { body: gzip(size + 1 bytes) }` → 1 request,
  `E_DOWNLOAD_SIZE`, at most `size` bytes written, no `.part` (FR-014). With zero
  backoff; a further case with `backoff = [50 ms]` and (a)'s route asserts the elapsed time is ≥ 150 ms (3 waits).
  The recorded progress of (a) holds exactly 3 `Retrying` events (`attempt` 1, 2, 3; `of: 4`; `code:
  "E_DOWNLOAD_OFFLINE"`), none after the 4th request; of (b) exactly 2 (`code: "E_DOWNLOAD_HTTP_STATUS"`); of (c) and
  (e1) to (e4) none (non-retryable or no failure); of (d) exactly 3; `wait_ms` equals the policy's backoff entry (the 50 ms case: 50, 50, 50),
  and every `Retrying` precedes the next request's first `Bytes` and the final error (FR-021, FR-023).
  `download::tests::non_retryable_transport_errors` (D5's "not retried: a URL or redirect refusal, TLS" at the download
  level; each row a fresh root and server, through D13's seam, asserting the code, exactly 1 request on the asset
  route, no `Retrying`, no `.part`, `next` not called, and an error `Display` naming the feed URL): (1) the asset route
  answers `Redirect { 302, "https://example.invalid/x" }`, which `check_url` refuses under the loopback policy →
  `E_DOWNLOAD_URL`; (2) the asset route starts a chain of six `302`s (one more hop than `MAX_REDIRECTS`) →
  `E_DOWNLOAD_REDIRECT`, each chain route requested once; (3) the asset route answers `Redirect { 302,
  "https://127.0.0.1:<port>/x" }` on the same fixture server, whose plain-HTTP answer to a TLS hello is `E_FEED_TLS`
  (`http_tests.rs:1277-1283`) → `E_DOWNLOAD_TLS` (plan SI-30). Every recorded run of this SC passes
  `assert_progress_invariants(events, size, outcome)` (plan SI-24).
  `download::tests::stall_and_deadline_are_named` (FR-016, the codes a retried attempt hides): (f) `HeadersThenStall {
  declared_len: size, sent: 100,000 bytes, stall: 5 s }` on every request, `policy.stall = 100 ms`, a 2 s attempt
  deadline → exactly 4 requests, final `E_DOWNLOAD_STALLED`; (g) the same route with `policy.stall = 2 s` and
  `attempt_deadline = 300 ms` → exactly 4 requests, final `E_DOWNLOAD_TIMEOUT` (the deadline fires long before the
  stall bound). Threshold: every count and code; (f) and (g) each < 3 s. Local; PENDING-CI (`all`).
- **SC-010 (transport policy).** `http::tests::streamed_policy_cases`: `get_streamed` under `UrlPolicy::LoopbackOnly`
  refuses `https://example.invalid/x` before connecting (`E_FEED_URL`); a redirect from the fixture server to a
  non-loopback host is refused (`E_FEED_URL`); 6 redirects → `E_FEED_REDIRECT`; `agent_config(&cfg, None,
  AgentPurpose::Download { stall: 30 s })` (inspected without a socket, as Phase 5 SC-026 does, with the unchanged
  three-argument form), where `cfg` is an `HttpConfig` whose `total_timeout` is a 3,600 s attempt deadline (plan SI-6),
  has `accept_encoding` `identity`, `accept` `application/octet-stream`, `https_only()` true when `cfg` is built from
  `HttpConfig::production()` (plan SI-4),
  `cfg.timeout_per_read() == Some(30 s)` and `cfg.timeouts().global == Some(3,600 s)` (ureq-3.5.0 `Config::
  timeout_per_read`, `Config::timeouts`, `src/config.rs:417`, `:436`, `Timeouts.global` `:1024`), so there is no 30 s
  global bound, `RootCerts::PlatformVerifier`, `max_redirects(0)`; the `Feed` agent (`AgentPurpose::Feed`, no stall
  bound) (plan SI-6) still has
  `timeout_per_read() == None`, `accept` `application/json` and `timeouts().global == Some(total_timeout)`, exactly as
  before. Every Phase 5 `http::tests` case passes unchanged. `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix`
  (`#[cfg(not(windows))]`, because Windows has no PAC fetch, `http.rs:621-627`): an `HttpConfig` with
  `total_timeout = 300 ms` and a PAC URL on a fixture route `Stall(5 s)`, `get_streamed` with `limits.deadline = 10 s`
  to a route that answers at once → the whole call returns, with whatever Phase 5's PAC-failure rule yields, in < 2 s
  (the PAC fetch kept the 300 ms feed bound, FR-011). Threshold: every assertion. Local (except the `_unix` case);
  PENDING-CI (`all`; the `_unix` case `check (macos-latest)`, `check (ubuntu-24.04)`).
- **SC-011 (disk full and permission errors leave nothing verified).** `download::tests::staging_errors_are_named`: an
  injected `StagingIo` returns `StorageFull` at `write` (on the 3rd chunk), at `sync`, and at `rename`; `QuotaExceeded`
  at `write`; `PermissionDenied` at `create_dir` and at `open`. Each → `E_STAGING_FULL` / `E_STAGING_DENIED` with the
  step; `next` not called; no file at the verified path; after `write` / `sync` the `.part` is gone, after `rename` the
  complete `.part` remains and a second `obtain_and` with a healthy `FsStagingIo` verifies it with zero requests.
  (The `truncate` step is SC-007's `truncate_failure_after_ignored_range`.) `download::tests::crash_after_each_step_recovers`
  (D11, FR-051): for each `k` from 1 to the number of `StagingIo` calls a clean SC-002 download makes, and once more
  over SC-007's `RangeIgnored` route, an injected `StagingIo` that delegates to `FsStagingIo` and panics right after
  its `k`-th call returns (so no cleanup path of `download` runs, as for a killed process); the panic is caught with
  `std::panic::catch_unwind`; then: the in-flight registry entry is gone (a new `obtain_and` for the same request is
  not `E_DOWNLOAD_BUSY`), the verified path holds either nothing or a file that passed verification (`verify_file`
  over it is `Ok`), and a second `obtain_and` with `FsStagingIo` against `Reply::Ranged` ends verified with the served
  bytes. Threshold: every case, every `k`. Local; PENDING-CI (`all`).
- **SC-012 (real permission error, macOS and Linux).** `download::tests::readonly_staging_dir_unix`
  (`#[cfg(unix)]`): `downloads/` created with mode `0o500` in a temp root → `E_STAGING_DENIED { step: "open" }`, nothing
  written, `next` not called; the mode is restored before the `TempDir` drops. The test **fails**, never skips, when it
  runs as root: it first creates a probe file in the temp root and reads its owner with
  `std::os::unix::fs::MetadataExt::uid` (POSIX sets a new file's owner to the process's effective uid, External facts);
  owner 0 fails the test with "cannot prove a permission error as root". A skip would still print `test … ... ok`, which
  the `required tests ran` regex (`manager-ci.yml:166`) counts as proof. PENDING-CI: `check (macos-latest)` (`macos`
  list), `check (ubuntu-24.04)` (`linux` list).
- **SC-013 (a file open elsewhere on Windows).** `download::tests::prune_skips_a_file_open_without_share_delete_windows`
  (`#[cfg(windows)]`): a stale `downloads/<other-hex>.part` held open by the test with
  `OpenOptionsExt::share_mode(FILE_SHARE_READ)` (no delete sharing) → `obtain_and` still succeeds for the live artifact,
  the stale file remains, one `warn!` line with `E_STAGING_IO` and step `remove` is logged; after the handle drops, a
  second `obtain_and` removes it. Local (Windows); PENDING-CI `check (windows-latest)` (`windows` list).
- **SC-014 (pruning and exclusion).** `download::tests::prune_keeps_only_live_artifacts`: staging seeded with a
  `.part` and a verified directory for a hash no feed entry has, a file `notes.txt`, and a verified directory for a live
  entry → after `obtain_and` for the live entry only the live entry's files exist. `download::tests::same_artifact_twice_is_busy`
  (deterministic, no timing window): thread A runs `obtain_and` against `Reply::Ranged` with a progress closure that,
  on `Started`, sends on an `mpsc` channel `ready` and then blocks on a second channel `go`; the test thread waits for
  `ready` (A is now in flight, FR-006 inserted it before `Started`), calls `obtain_and` for the same request → exactly
  `E_DOWNLOAD_BUSY`, no `Progress` event for that call, and the server has recorded 0 requests; then it sends `go`, and
  A ends `Ok` after exactly 1 request. Two different artifacts started the same way (both held at `Started`, then
  released) → both `Ok`. `download::tests::busy_registry_is_released_on_error` → after a failed `obtain_and`, the same request
  proceeds. `download::tests::prune_spares_in_flight_artifact` (FR-005's in-flight exception): feeds F1 (offers X) and
  F2 (offers Y only), one root, one server; thread A obtains X under F1 and is held, through the `ready` / `go`
  channels, inside its progress closure at its first `Bytes` event, so `downloads/<hexX>.part` exists with at least one
  chunk on disk; the test thread then runs `obtain_and` for Y under F2, whose prune sees X as not live → it ends `Ok`,
  and `<hexX>.part` still exists with a length ≥ A's reported `on_disk`; after `go`, A ends `Ok` and verified with its
  file equal to the served bytes; control (non-vacuity): once A has returned, a third `obtain_and` for Y under F2 (the
  cached path, 0 requests) removes `<hexX>/`, so the earlier survival was the registry's doing (plan SI-30).
  Threshold: every assertion. Local; PENDING-CI (`all`).
- **SC-015 (the verified marker is a type).** `verify.rs`'s `compile_fail` doctest (a struct literal and a
  `Default::default()` of `VerifiedArtifact` outside the module) runs under `cargo test --doc -p krate-manager-core`
  (part of `cargo test --workspace`); `injection.rs` `verified_artifact_has_no_public_constructor` and its self-test
  (FR-043) pass. Threshold: doctest fails to compile as expected; 0 findings; every plant reported. The doctest is
  **local evidence only**: its result line (`test src/verify.rs - verify::VerifiedArtifact (line N) - compile fail ...
  ok`) contains spaces, so the `required tests ran` regex (`manager-ci.yml:166`) cannot count it, and a doctest
  missing on a leg would go unnoticed there (a failing one still fails the `cargo test` step). The CI-countable proof
  on every leg is `verified_artifact_has_no_public_constructor` and its self-test, listed in `all`: PENDING-CI (`all`).
- **SC-016 (the release key).** `verify::tests::release_key_is_the_committed_key_and_not_a_test_key` (FR-042):
  `SigningKey::release()` parses, its bytes and key id equal the committed file's (today `9FEB36F9114273BC`), and differ
  from `440FA75168B852F7` and `63861317F7D60239` and from both test keys' 32-byte keys. Threshold: every assertion. Local;
  PENDING-CI (`all`).
- **SC-017 (no private key, no real host).** `injection.rs` `no_secret_key_in_app_tree` (FR-083),
  `phase7_tests_hold_no_real_host` (FR-087) and their self-tests: 0 findings, every plant reported. Threshold: 0 / all.
  Local; PENDING-CI (`all`).
- **SC-018 (dependencies and confinement).** `core_dependencies_are_the_fr007_set` (13 names; dev `{flate2, minisign}`),
  `config_guard.rs` `core_has_no_signing_crate` and its self-test `signing_crate_scan_reports_plants` (FR-081; the
  planted dev-dependency `minisign` must not be reported), `verify_crates_are_confined` and
  `download_and_verify_hold_no_url_or_whole_read` with their self-tests (FR-082), the extended roster and identifier
  guards (FR-088). Threshold: 0 findings each. Local; PENDING-CI (`all`), plus the `core dependency tree` step's
  `::notice … none excluded` on all three legs.
- **SC-019 (the command surface).** `config_guard.rs`: `build.rs` registers exactly the six commands, `main.json` grants
  exactly the six permissions (`download_plugin` / `allow-download-plugin` among them, no `cancel_download`, FR-061,
  FR-066), the CSP check unchanged. `download::tests::request_for_cases`: an unknown name →
  `E_DOWNLOAD_UNKNOWN_PLUGIN`; a v1-only entry (`tools/feed-contract/fixtures/feeds/valid/v1-only.json`, read in place)
  → `E_DOWNLOAD_UNAVAILABLE` (`NotThroughManager`); `mixed.json`'s v1 entries likewise; an entry with only another OS's
  key → `NotForThisMachine`; a `min_os` above the passed running version → `NeedsNewerOs`; a native entry → a request
  whose fields equal the entry's. The command's flow and its `Channel` adapter are SC-028 and SC-027. Threshold: every
  case. Local; PENDING-CI (`all`).
- **SC-020 (fixture server additions).** `fixture_server::tests::ranged_cases` (no `Range` → 200 + full body; `bytes=10-`
  → 206 + exact `Content-Range` + tail; `bytes=<len>-` → 416) and `script_advances_per_request` (three requests to a
  two-reply script get reply 1, 2, 2) and `paced_delivers_all_bytes_in_chunks` (the body arrives complete, in chunks of
  the given size, the whole response taking at least `(chunks - 1) × interval`). Every existing `fixture_server::tests` case passes. Threshold: every assertion.
  Local; PENDING-CI (`all`).
- **SC-021 (macOS leg).** PENDING-CI `check (macos-latest)`: SC-002 to SC-020 and SC-026 to SC-037 run on a real APFS
  volume with the staging dir under `std::env::temp_dir()` (`/var/folders/…`), including the `macos` list
  (`readonly_staging_dir_unix`, `verified_file_has_no_quarantine_macos`,
  `streamed_pac_fetch_keeps_the_feed_bound_unix`, `staged_symlinks_are_not_followed`, FR-086) (plan SI-23); the
  `required tests ran` notice counts match. Threshold: 0 failed,
  0 missing.
- **SC-022 (Linux leg).** PENDING-CI `check (ubuntu-24.04)`: as SC-021 on ext4 with the `linux` list. Threshold: 0 failed,
  0 missing.
- **SC-023 (Windows CI leg).** PENDING-CI `check (windows-latest)`: as SC-021 with the `windows` list, including
  `prune_skips_a_file_open_without_share_delete_windows`, `verified_file_has_no_zone_identifier_windows` and
  `staged_links_are_not_followed_windows` (FR-086) (plan SI-29).
  Threshold: 0 failed, 0 missing.
- **SC-024 (lane cost).** The Phase 7 tests (`download::`, `verify::`, the new `http::` and `fixture_server::` cases)
  together add less than 20 s of wall clock to the local `cargo test --workspace --locked` run (measured from the
  per-binary `finished in` lines before and after, recorded in compliance); no test sleeps longer than 1 s (FR-087).
- **SC-025 (scope).** `git diff --name-only <phase base>..HEAD` lists only paths allowed by FR-090 and FR-091.
  Threshold: 0 others.
- **SC-026 (artifacts of a batch stay available for one consumer; D7, FR-005, FR-045).**
  `download::tests::two_artifacts_reach_one_consumer`: one contract-valid feed offering `Alpha` and `Bravo` (both with
  a native platform entry, different bytes and hashes), both served as `Reply::Ranged`. `obtain_and(…Alpha…, |a| a)`,
  then `obtain_and(…Bravo…, |a| a)`, each returning its owned `VerifiedArtifact`. After the second call (whose prune
  ran while Alpha's artifact was held by the test and still offered by the feed): Alpha's file is still at its
  verified path with its served bytes; a stand-in batch consumer `fn batch(items: &[VerifiedArtifact]) -> usize`
  (standing in for Phase 10's one-job builder) is called **once** with both artifacts; for each, `path()` exists, is
  `downloads/<its hex>/<its asset>`, and the SHA-256 of the file on disk equals `sha256()`. Threshold: every
  assertion. Local; PENDING-CI (`all`).
- **SC-027 (the `Channel` adapter forwards every event; roadmap line 606, FR-064).**
  `download::tests::channel_adapter_forwards_in_order`: SC-002's download run once with a plain recording closure and
  once through `channel_progress(send)`, where `send` records each event into a `Vec` and returns `Ok(())` → the two
  sequences are identical (every `Progress` event forwarded once, unchanged, in order), and the last is `Verified`.
  With a `send` that returns `Err(())` on every call (and counts its calls) the download still ends `Ok` and verified,
  and `send` was called once per event of the recording run. With a `send` that fails only on its second call, every
  later event is still delivered. Threshold: every assertion. Local; PENDING-CI (`all`). (The remaining step,
  `Channel::send` itself, is Tauri's; `src-tauri` runs no unit tests, External facts and `src-tauri/Cargo.toml:10-19`.)
- **SC-028 (the command flow; FR-060, FR-062, FR-065).** `download::tests::command_flow_cases`, on `FakeHostFacts` (with a
  running OS version that satisfies the entry) and a context whose `feed_url` is a fixture route; every `for_command` /
  `command_request` call passes the entry's `version_text` as `expected_version` unless the row says otherwise: (a) the
  feed route
  answers `Status(404)` and no cache exists → `for_command` is `E_DOWNLOAD_NO_FEED`, and the server saw no request
  other than the feed request; (b) the feed route serves a contract-valid v2 document and `name = "Zulu"` (not a feed
  key) → `E_DOWNLOAD_UNKNOWN_PLUGIN`, and `FakeHostFacts.calls == ["os_version"]`; (c) a v1-only entry →
  `E_DOWNLOAD_UNAVAILABLE`; (e) a good entry with `expected_version` one patch below the feed's `version_text` →
  `E_DOWNLOAD_VERSION_CHANGED` carrying both versions, the server saw no request for the asset route, `downloads/`
  was not created or touched (a pre-seeded stale `.part` and a seeded verified directory of a live entry still exist,
  proving no prune ran), and the progress closure was never called; with `name = "Zulu"` and a wrong
  `expected_version` the code is `E_DOWNLOAD_UNKNOWN_PLUGIN` (FR-062's order); with `expected_version` equal, the flow
  proceeds to row (d); (d) for a good entry, `command_request` returns the feed and a request equal to
  `request_for`'s, and `run_command` over `request.with_fixture_url(…)` returns a `DownloadSummary` whose
  `serde_json::to_value` object has exactly the keys `{name, version, platform, asset, size, resumed}`, whose values
  equal the entry's (`resumed == false`), and none of whose string values contains `/`, `\`, `://` or a run of 64 hex
  digits; (f) FR-060's "`for_command` is `command_request` then `run_command`": `for_command` with a good name and an
  equal `expected_version` on a good entry, with a pre-seeded stale `downloads/<other-hex>.part`; the request is built
  from the feed, so it fetches the feed's `https://assets.example.invalid/…` URL (no D13 seam on this path), which the
  loopback policy refuses → `E_DOWNLOAD_URL`, with the feed request recorded, 0 asset-route requests, exactly one
  `Started` event, no `Retrying`, and the stale `.part` gone (only `run_command` → `obtain_and` emits `Started` and
  prunes, so the row proves the call reached it with the env it was given) (plan SI-30). Threshold: every assertion.
  Local; PENDING-CI (`all`).
- **SC-029 (a cached verified file is re-verified; FR-046).** `download::tests::cached_verified_file_is_reverified`:
  (a) `obtain_and` twice against `Reply::Ranged`: the second run makes 0 requests, `next` runs once with
  `artifact.path() == downloads/<hex>/<asset>`, its progress is exactly `Started { total: size, resumed_from: size }`,
  `Verifying`, `Verified`, and `Obtained { resumed: false, cached: true, transferred: 0 }`; (b) as (a) but one byte of
  the verified file is flipped between the runs: the second run removes `downloads/<hex>/`, emits `Restarted { reason:
  CachedInvalid }` after its first `Verifying` and before the re-download's first request (plan SI-9), makes exactly
  one full request (no `Range`), `next` sees the served bytes (the file at its path hashes to the feed `sha256`), and the
  tampered bytes never reach `next` (a counting `next` is called once, after the re-download). Every recorded run
  passes `assert_progress_invariants(events, size, outcome)` (plan SI-24). Threshold: every
  assertion. Local; PENDING-CI (`all`).
- **SC-030 (no redirect target in a log line or an error; D12, FR-053).** `download::tests::redirect_target_never_logged`,
  with `test_support::install_capture` and `take_mine` (this thread's `krate_manager_core` records): the fetched route
  answers `Redirect { status: 302, location: "/cdn/a.bin?token=SECRET-TOKEN" }`, and the route
  `/cdn/a.bin?token=SECRET-TOKEN` (the fixture server matches the whole request target) answers (a) `Reply::Ranged` →
  `Ok`; (b) `Status(500)` on every request → 4 requests, `E_DOWNLOAD_HTTP_STATUS`; (c) `Reply::Ranged` with a
  signature by another key of the same key id → `E_VERIFY_SIGNATURE`; (d) `Truncated { declared_len: size, sent: 1000 }`
  on every request → `E_DOWNLOAD_BODY` after 4 requests on the `/cdn/…` route, an error whose `detail` comes from the
  transport behind the redirect, which (b) and (c) never produce (plan SI-22). In every case no captured record and
  (for b, c, d) no `DownloadError` `Display` contains `SECRET-TOKEN` or `/cdn/`; the one `warn!` line of (b), (c) and
  (d), and each error's `Display`, contain the feed's `AssetUrl` (`https://assets.example.invalid/…`, D13). FR-053's
  counts and fields: per run, the records at target `krate_manager_core::download` are exactly one `info!` and zero
  `warn!` for (a), and exactly one `warn!` and zero `info!` for (b), (c) and (d); (a)'s `info!` line holds the plugin
  name, the version, `PlatformKey::as_str()`, the byte count transferred, and `resumed=false`, `cached=false`. A
  socket-less row over `download`'s private detail scrubber (plan PD-15: each whitespace-delimited token containing
  `://`, `/` or `?` becomes `<redacted>`): `scrub("io: /cdn/a.bin?token=SECRET-TOKEN failed at https://x.invalid/y")` holds neither
  `SECRET-TOKEN`, `/cdn/` nor `://`, and `scrub("connection reset (os error 10054)")` is unchanged (plan SI-22).
  Threshold: every assertion. Local; PENDING-CI (`all`).
- **SC-031 (the download writes only the bytes, only under `downloads/`; FR-004, roadmap lines 1001-1003).**
  `download::tests::writes_nothing_outside_downloads`: the temp root's tree (every path, kind, length and content
  hash) is snapshotted after the fixture is set up and again after SC-002's download and after each SC-003 row; every
  path that is new or changed lies under `<state>/staging/downloads/`, and nothing was removed outside it.
  `download::tests::verified_file_has_no_quarantine_macos` (`#[cfg(target_os = "macos")]`): after SC-002's download,
  `/usr/bin/xattr -l <verified file>` exits 0 and prints no line starting `com.apple.quarantine` (only the quarantine
  attribute is asserted absent: whether the OS attaches other attributes to a file a test binary writes is unmeasured,
  and the manager is not their author). `download::tests::verified_file_has_no_zone_identifier_windows`
  (`#[cfg(windows)]`): opening `<verified file>:Zone.Identifier` for reading fails with `NotFound` (no Mark-of-the-Web
  stream; probes `results.md:9`). `download::tests::staged_symlinks_are_not_followed` (`#[cfg(unix)]`): symlinks planted
  at `downloads/<hex>.part` and `downloads/<hex>` pointing outside staging are removed by the prune, never written,
  truncated or renamed through (the run verifies with one request and no `Range`, and the outside tree is
  byte-identical afterwards); a linked `staging/downloads` → `E_STAGING_IO`, step `create_dir`; `verify_file` on a link
  → `E_VERIFY_IO { kind: InvalidInput }`; a `<hex>` link the prune cannot remove → `E_STAGING_IO`, step `open`, nothing
  under it measured (plan SI-23). `download::tests::staged_links_are_not_followed_windows` (`#[cfg(windows)]`): the same
  layout with NTFS junctions made by `cmd /C mklink /J` (no privilege needed, so the case cannot skip), the prune's
  `remove_dir_all` retry removing them (plan SI-29). The full assertion lists are plan §7.1's rows "SC-031 / FR-050".
  Threshold: every assertion. Local (the tree diff and the Windows cases);
  PENDING-CI: `all`, `check (macos-latest)` (`macos` list), `check (ubuntu-24.04)` (`linux` list), `check
  (windows-latest)` (`windows` list).
- **SC-032 (the fetch-URL seam is test-only and narrow; D13, FR-085).** `download::tests::fixture_url_seam_cases`:
  `with_fixture_url` accepts `http://127.0.0.1:<port>/<asset>` and `http://[::1]:<port>/x/<asset>` and refuses a
  non-loopback host, `https://127.0.0.1:<port>/<asset>`, `http://localhost:<port>/<asset>`, a URL with no port, and a
  last segment other than the request's `AssetName`; after it, `DownloadError`s and log lines still name the feed URL
  (SC-030). `injection.rs` `fixture_url_seam_is_test_only` with self-test `fixture_url_seam_scanner_reports_plants`:
  `with_fixture_url` and `fetch_url` are declared only in `download.rs`, each item directly under a `#[cfg(test)]`
  attribute (not `test-support`), and the token `with_fixture_url` occurs elsewhere only in `download_tests.rs`; no file
  under `src-tauri/` holds it. Threshold: every case; 0 findings; every plant reported. Local; PENDING-CI (`all`).
- **SC-033 (the path join refuses every bad component; FR-003).** `download::tests::staged_paths_cases`: a valid
  `hex` / `asset` pair gives `part = downloads/<hex>.part`, `dir = downloads/<hex>`, `verified = downloads/<hex>/<asset>`;
  `asset` values `a/b`, `a\b`, `..`, `.`, empty, `/abs`, and (on Windows) `C:x` and `C:\x`, and `hex` values of 63 or
  65 digits or with an upper-case digit, each give `E_DOWNLOAD_PATH`, with nothing created under `downloads`.
  Threshold: every case. Local; PENDING-CI (`all`).
- **SC-034 (cancellation; Q2, FR-017, FR-016).** `download::tests::cancellation_cases`: (a) a token set before the call
  → `E_DOWNLOAD_CANCELLED`, 0 requests, no `.part`, no `Progress` event at all (the call returns before `Started`),
  `next` not called; (b) the progress closure sets the token on the 2nd `Bytes` event of an SC-002 download
  → `E_DOWNLOAD_CANCELLED` within 2 s, exactly 1 request, no `Retrying`, `Verifying` or `Verified` event, `next` not
  called, `downloads/<hex>.part` still exists with a length between 1 byte and `size - 1` and a prefix equal to the
  served bytes, nothing at the verified path, and the in-flight registry released (a new `obtain_and` for the request
  is not `E_DOWNLOAD_BUSY`); (c) a second `obtain_and` after (b) with a fresh token against `Reply::Ranged` resumes with
  `Range: bytes=<the .part length>-`, `Started { resumed_from: <that length> }`, and ends verified; (d) a route
  `Script([Status(503), Ranged])` with `backoff = [5 s]` and a thread that sets the token 100 ms after the first request
  → `E_DOWNLOAD_CANCELLED` in < 1 s (the backoff wait is interrupted, at most one 50 ms slice late), exactly 1 request;
  the `Retrying` for that backoff was already emitted (it precedes the wait), and no second request follows; (e)
  `get_streamed` with a token set from inside `sink` → `StreamError::Cancelled` after that chunk, no further `sink`
  call; (f) `for_command` / `download_plugin`'s call path creates a `CancelToken` that is never set, so SC-028 (d) ends
  `Ok` (the unset token changes nothing), and `config_guard.rs` finds no `cancel_download` command (SC-019); (g) the
  token set while a read is blocked in the transport (`HeadersThenStall` route, `stall = 500 ms`, `backoff = [5 s]`, a
  thread that cancels 100 ms after the `.part` holds the 100,000 sent bytes) → `E_DOWNLOAD_CANCELLED` in < 1 s, exactly 1
  request, no `Retrying` (the stalled read's error is not retried and not reported, FR-017, FR-023), `next` not called,
  the `.part` kept at 100,000 bytes. Every recorded run of (a) to (d) and (g) passes
  `assert_progress_invariants(events, size, outcome)` (plan SI-24).
  Threshold: every assertion; each test < 3 s. Local; PENDING-CI (`all`).
- **SC-035 (`discard`; Q3, FR-049).** `download::tests::discard_cases`: (a) after `obtain_and(…, |a| a)` returns an
  artifact, `discard(artifact, &FsStagingIo)` → `Ok`, `downloads/<hex>/` no longer exists, and a following `obtain_and`
  for the same request makes exactly 1 request (nothing cached, `cached: false`); (b) with two artifacts held (SC-026's
  setup), discarding Alpha leaves Bravo's directory and file untouched and still verifying; a stale `<other>.part` is
  untouched too; (c) `discard` of an artifact whose directory was already removed → `Ok`; (d) an injected `StagingIo`
  whose `remove_dir_all` returns `PermissionDenied` → `E_STAGING_DENIED { step: "remove" }` returned, and with
  `StorageFull` → `E_STAGING_FULL`; (e) a prune with a feed that still offers the artifact keeps it without any
  `discard` (FR-005 unchanged, SC-014 and SC-026 still pass); (f) with `thread::scope` and SC-014's `ready` / `go`
  channels, an `obtain_and` B of artifact X held at `Started` while `discard` is called on a `VerifiedArtifact` of the
  same hash from an earlier run → `E_DOWNLOAD_BUSY` (`DiscardBusy`), `<hex>/` still present; after `go`, B ends `Ok`
  with its artifact path existing; then `discard` of that artifact → `Ok`, `<hex>/` gone; a `discard` of a different
  hash while B is held → `Ok` (only the same hash is refused) (plan SI-20). Threshold: every assertion. Local; PENDING-CI (`all`).
- **SC-036 (retry events; Q4, FR-023, FR-021).** `download::tests::retrying_events_are_ordered_and_serialize`: over
  SC-009 (a)'s route with `backoff = [10 ms, 20 ms, 40 ms]` the progress is `Started`, then `Retrying { attempt: 1, of: 4,
  code: "E_DOWNLOAD_OFFLINE", wait_ms: 10 }`, `Retrying { attempt: 2, …, wait_ms: 20 }`, `Retrying { attempt: 3, …,
  wait_ms: 40 }` and nothing after the 4th failed request; over SC-006's `stall_resumes` route the one `Retrying` has
  `code: "E_DOWNLOAD_STALLED"`; every `Retrying` is observed before the next request reaches the server (the closure
  records the server's request count at each event: attempt k's event sees k requests); `serde_json::to_value` of a
  `Retrying` has exactly the keys `{kind, attempt, of, code, wait_ms}` with `kind == "retrying"`; the set of `kind` values any
  recorded run emits is a subset of `{started, bytes, restarted, retrying, verifying, verified}` (no heartbeat kind).
  Threshold: every assertion. Local; PENDING-CI (`all`).
- **SC-037 (no overall bound; Q5, FR-018).** `download::tests::policy_has_no_overall_budget`: `DownloadPolicy::production()`
  has `attempts == 4`, `backoff == [1 s, 2 s, 4 s]`, `stall == 30 s`, `attempt_deadline == 3600 s` and no other time
  field (the struct literal in the test names every field); over SC-009 (g)'s route (`attempt_deadline = 300 ms`,
  4 attempts, backoff `[100 ms]`) the run ends `E_DOWNLOAD_TIMEOUT` with exactly 4 requests and an elapsed time ≥ 4 ×
  300 ms + 3 × 100 ms = 1.5 s, i.e. nothing cut the run short at one attempt's deadline; and, in the separate test
  `download::tests::slow_moving_download_is_not_an_error` (split out because the two runs' floors, 1.5 s + 1.35 s,
  leave one test no margin under the 3 s bar), a download served as `Reply::Paced`
  (FR-084) in 10 chunks 150 ms apart with `stall = 1 s` and `attempt_deadline = 5 s` (the transfer takes ≥ 1.35 s,
  longer than the stall bound, while each 150 ms gap uses about 15 % of it) ends `Ok` and verified, with at most
  `policy.attempts` requests and a measured elapsed time `> policy.stall` (and ≥ 1.35 s): a slow but moving download is
  not an error. `Reply::Paced` answers every request with the full `200` body, so `Ok` means one response delivered all
  ten chunks in longer than the stall bound, whatever a spurious retry did before it; a `Retrying` is printed, not
  asserted against (plan SI-19). Threshold: every assertion; each test < 3 s. Local; PENDING-CI (`all`).

---

## Safety

- **Injected roots and inputs.** Phase 7 writes only under `Roots::staging_dir()/downloads` (`roots.rs:195-200`), which
  production derives from the OS state dir in `src-tauri` and every test from `fresh_temp_roots()`; sandboxed roots
  panic on a path outside the temp base (FR-003). The feed URL, the HTTP configuration (URL policy, proxy, PAC) and the
  release public key all come from `AppContext` (FR-041); tests use `HttpConfig::loopback`, the fixture server on
  `127.0.0.1` and throwaway keys. The webview passes a feed key and nothing else (FR-062).
- **Verified before anything touches the asset.** Size, SHA-256, the minisign signature (both the data and the global
  signature) and the C3 binding to plugin, version, platform key and asset name are all checked before a
  `VerifiedArtifact` exists (FR-030 to FR-039), and `obtain_and` calls the next stage only with one (FR-045). Phases 8
  and 10 accept nothing else, whether they consume one artifact at once or a batch collected from several
  `obtain_and` calls (D7). The
  manager extracts no `.exe` or `.pkg` and Phase 7 extracts nothing; the elevated helper (Phase 10) only ever receives a
  verified installer, which it re-hashes while copying it into an admin-only directory.
- **What a feed string can become.** The asset URL is fetched under the HTTPS-only policy with every redirect hop
  re-checked; the asset file name is one re-checked path component under `downloads/<hex>/`; the SHA-256 becomes a
  64-hex-digit directory name. No other feed string reaches the filesystem (FR-003).
- **Bytes on disk are the signed bytes.** `Accept-Encoding: identity`, and any `Content-Encoding` other than `identity`
  that reaches the download is refused (D2, FR-013); a `gzip` response, which ureq decodes and hides, is held to the
  same size bound and verification (plan SI-18); the size bound stops writing at the first excess byte (FR-014); nothing is added to the file
  (no quarantine, no stream, no mode change, FR-004, SC-031).
- **Test seams.** The only test seam on the download path, D13's fetch-URL override, exists only under `#[cfg(test)]`
  and accepts only a loopback `http://` URL ending in the request's asset name (SC-032); release builds, and core built
  with `test-support` for src-tauri's tests, contain neither the field nor the function.
- **The elevated helper.** Untouched in Phase 7. Nothing here runs a process, elevates, or writes outside staging.
- **Keys.** The release public key is compiled in from the user-committed `krate-signing.pub` and proved not to be a
  test key (FR-042). Test keys exist only in memory for the length of one test (FR-083); no private key is generated
  into, written into or committed to the repository.
- **What tests never touch.** No test writes to a real VST3, Components or preset folder, ProgramData, the registry or
  the real state dir; no test runs the helper or an installer; no test calls github.com, githubusercontent.com,
  krateaudio.com or api.github.com (FR-087 guard, plus the loopback policy enforced at run time). No live-network test is
  added.
- **Logs.** No redirect target is logged or put in an error, because a release CDN's signed query string is a
  credential for a few minutes (D12, FR-053, SC-030).

---

## Edge cases

| Case | Behaviour |
|---|---|
| Interrupted download (connection reset, truncated body) | Bytes received stay in `<hex>.part`; the next attempt (D5) or the next request (a later run) resumes with `Range` (FR-014, FR-015, SC-006) |
| Stalled server (headers, then silence) | `DOWNLOAD_STALL` ends the attempt as `E_DOWNLOAD_STALLED`; retried with `Range` (SC-006) |
| Server ignores `Range` | `200` → `Restarted { RangeIgnored }`, the partial truncated, full body written (SC-007) |
| `416` or a wrong `Content-Range` | One full request (D4, SC-008) |
| Release CDN's signed redirect URL expired between attempts | Every attempt starts again at the feed URL, so a fresh redirect is followed (FR-011) |
| Crash after the last byte, before the rename | The next request finds a `.part` of exactly `size` bytes and verifies it with no request (SC-008 c) |
| Crash or kill mid-transfer | `.part` holds a prefix; resumed next time. Nothing is ever at the verified path unless verification passed (SC-011 crash cases) |
| Several updates in one batch | Each artifact is obtained by its own `obtain_and` and returned by value; Phase 10 builds one job and one elevation prompt from all of them (D7, SC-026). A later `obtain_and` with the same feed never prunes an earlier artifact of that batch |
| Disk full | `E_STAGING_FULL` with its step; the partial is removed to give the space back; `next` never called (SC-011). No free-space pre-check: `std` has none, and a full disk is reported when it happens |
| Staging not writable (permissions, read-only volume) | `E_STAGING_DENIED` (SC-011 injected; SC-012 real on macOS / Linux) |
| File locked by a running DAW on Windows | Not reachable in Phase 7: it writes only to staging. A staged file another process holds open (an antivirus scan) can make pruning skip it (SC-013) or the final rename fail (`E_STAGING_IO { step: "rename" }`, the complete `.part` kept and re-verified next time). The DAW-locked plug-in case is Phase 10's |
| Partial install | Not reachable: Phase 7 installs nothing. A partial *download* is the `.part` file above |
| Downgrade attempt | An older signed asset relabelled as a newer version fails `E_TC_BINDING { version }` (FR-048). An honestly older offered version is downloaded only if asked; `version::compare` never calls it an update (`version.rs:83-94`), and the install phases act on that |
| Corrupt feed | `feed::check` rejects it and keeps the last good copy (Phase 5); a `Feed` value always passed the contract, so `request_for` never sees a malformed entry |
| Offline | `feed::check` falls back to the cached feed; per-tag asset URLs stay valid, so a download from a stale snapshot is allowed and fails with `E_DOWNLOAD_OFFLINE` after the bounded retries (SC-009 a) |
| Feed `schema_version` 1 vs 2 | A v1 document or a v1-only entry has no `platforms` → `E_DOWNLOAD_UNAVAILABLE` (`NotThroughManager`, SC-019); only a v2 entry with a native platform yields a request |
| Bundles that still say `0.8.0` | Not relevant to Phase 7: download and verification never read an installed bundle; the version bound into the signature is the feed entry's `version_text` |
| Fixture signatures in the Phase 1 JSON feeds | Zero-filled, structurally valid only (CONTRACT.md section 8): a test feeding one to `verify_file` expects `E_VERIFY_KEY_ID` or `E_VERIFY_SIGNATURE`, never success; Phase 7 tests sign their own assets (FR-083) |
| Same artifact requested twice at once | `E_DOWNLOAD_BUSY` for the second (SC-014) |
| Two feed entries that differ only in letter case | Staging keys on the SHA-256, never the name (D3) |
| An already verified file from an earlier run | Re-verified before use, no network; a failure removes it and downloads afresh (FR-046, SC-029) |
| Server sends `Content-Encoding: gzip` | ureq decodes it and hides the header, so the decoded bytes are written and then size-bounded and verified: gzip of the signed bytes verifies, anything else ends `E_VERIFY_SHA256` or `E_DOWNLOAD_SIZE` with no retry and no `.part` left (FR-013, FR-014, SC-009 e2-e4) (plan SI-18) |
| Server sends another `Content-Encoding` (`br`, `deflate`, …) | `E_DOWNLOAD_ENCODING`, no retry, nothing written, no `.part` left (FR-013, FR-014, SC-009 e1) (plan SI-18) |
| Server sends more bytes than the feed `size` | `E_DOWNLOAD_SIZE` at the first excess byte, the `.part` removed (FR-014, SC-003 d) |
| A legacy (`Ed`, not prehashed) signature | Refused by the contract parse and by `verify_stream` (FR-032) |

---

## New components

| Module / file | Purpose |
|---|---|
| `apps/krate-manager/crates/core/src/download.rs` | `DownloadRequest` (with the `#[cfg(test)]` `fetch_url` / `with_fixture_url` seam, D13), `request_for`, `DownloadPolicy`, `Progress`, `RestartReason`, `classify_head`, `staged_paths`, `StagingIo` / `PartialFile` / `FsStagingIo`, the in-flight registry, pruning, `obtain_and`, `Obtained`, `discard` (FR-049), `command_request` / `run_command` / `for_command` (with `expected_version` and the `CancelToken`), `channel_progress`, `DownloadError`, `DownloadSummary`; `DownloadEnv<'a>` (`ctx`, `io`, `policy`, `cancel`), the inputs every download entry point shares (FR-045, Q-B) (plan SI-25) |
| `crates/core/src/download_tests.rs` | SC-002, SC-003, SC-005 to SC-009, SC-011 to SC-014, SC-019, SC-026 to SC-031, SC-032's `fixture_url_seam_cases`, SC-033 to SC-037 |
| `crates/core/src/verify.rs` | `RELEASE_KEY_TEXT`, `SigningKey`, `Expected`, `verify_file`, `VerifiedArtifact` (and its crate-private `relocated`), `VerifyError` |
| `crates/core/src/verify_tests.rs` | SC-004 (including `invalid_trusted_comment_is_named`), SC-016, the per-step unit cases behind SC-003 |
| `crates/core/src/test_signer.rs` (🆕, `#[cfg(test)] mod test_signer;`) | `TestSigner` (in-memory `minisign` key pair, C3-commented signatures); never compiled into core's lib (D1, FR-083) |
| `crates/core/src/http.rs` (🔶) | `CancelToken` (FR-017), `StreamError::Cancelled`, `AgentPurpose::Download`, `StreamLimits`, `ResponseHead`, `HeadAction`, `StreamEnd`, `StreamError`, `get_streamed`; `AgentPurpose::Download { stall }` instead of a `stall` parameter, the private `ChainError` and `final_response` behind an unchanged `run_chain` (plan SI-6) |
| `crates/core/src/fixture_server.rs` (🔶) | `Reply::Ranged`, `Reply::Script`, `Reply::Paced` |
| `crates/core/src/test_support.rs` (🔶) | A test-feed builder over `feed::parse` (takes signature text as input; no `minisign`), a fault-injecting and a counting `StagingIo`; `kat_signing_key()` (FR-041); `test_policy()` (the one test `DownloadPolicy`: 4 attempts, zero backoff, `stall` 2 s, `attempt_deadline` 10 s) and `DownloadRig { ctx, io, policy, cancel }` with `new(ctx)` and `env() -> DownloadEnv<'_>`, the one place a test builds a `DownloadEnv` (plan SI-25) |
| `crates/core/src/{detect,feed,http,status}_tests.rs`, `context_tests.rs`, `crates/core/tests/live_feed.rs` (🔶) | Mechanical edits only: every Phase 4-6 `AppContext` literal (`detect_tests.rs:100-101`, `:1985`; `feed_tests.rs:248-249`, `:1673`, `:1756`; `http_tests.rs:129-130`, `:1644`, `:1972`; `status_tests.rs:78-79`) gains `signing_key: kat_signing_key()`, and every `assemble_context` / `assemble_with_os` call (`context_tests.rs`, 28 sites; `detect_tests.rs`; `live_feed.rs:48`, feature-gated and never compiled in CI, so checked by text) gains the fourth argument `kat_signing_key()` (FR-041) (plan SI-16) |
| `apps/krate-manager/tests/fixtures/clippy-guard/Cargo.lock` (🔶) | The clippy-guard fixture's own lock (it depends on `krate-manager-core` and `krate-manager` by path, and `manager-ci.yml`'s `clippy-guard` step runs `--locked`), re-locked for core's two new normal dependencies with `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 > /dev/null` from `apps/krate-manager/`, which adds the missing entries and changes nothing the lock already has (plan SI-3) (tasks TA-5) |
| `crates/core/src/context.rs` (🔶) | `AppContext.signing_key`; the extra `assemble_context` parameter |
| `apps/krate-manager/src-tauri/src/commands.rs` (🔶) | `download_plugin(name, expected_version, on_progress)`, a thin wrapper over `download::for_command` and `download::channel_progress`, passing a `CancelToken` nobody sets |
| `src-tauri/build.rs`, `src-tauri/capabilities/main.json`, `src-tauri/src/lib.rs` (🔶) | The sixth command and permission; `SigningKey::release()` at setup |
| `crates/core/src/lib.rs` (🔶) | `pub mod download;`, `pub mod verify;`, `#[cfg(test)] mod test_signer;` |
| `crates/core/tests/injection.rs`, `config_guard.rs`, `roster.rs` (🔶) | FR-043, FR-080 to FR-083, FR-087, FR-088 guards; SC-032's `fixture_url_seam_is_test_only` |
| `apps/krate-manager/Cargo.toml`, `crates/core/Cargo.toml`, `Cargo.lock` (🔶) | The three pins (FR-080) |
| `apps/krate-manager/tests/required-tests.json` (🔶) | FR-086 |

---

## Traceability

| Roadmap statement (line) | FR / SC |
|---|---|
| "streamed HTTPS download into the staging dir" (604) | FR-001 to FR-006, FR-010 to FR-016, FR-045; SC-002, SC-010, SC-033 |
| "progress reported through a plain callback / … that `core` tests observe directly (core stays Tauri-free)" (604-605) | D8, FR-020 to FR-022; SC-005, SC-007 |
| "`src-tauri` adapts it to a Tauri `Channel` for the webview" (606) | D9, FR-060 to FR-064; SC-027 (the adapter forwards every event in order and survives send errors), SC-028 (the command flow and its summary), SC-019 (the registered command and permission) |
| "Resume with a `Range` request … bounded retries" (606-608) | D4, D5, FR-013 to FR-016; SC-006 to SC-009, SC-029 |
| "`Content-Length` vs feed `size`, streamed SHA-256 vs feed `sha256`" (609) | FR-013, FR-014, FR-031, FR-034, FR-035; SC-003 c, d, e |
| "minisign signature vs the embedded public key, `tools/feed-contract/keys/krate-signing.pub` compiled in" (609-610) | FR-040 to FR-042; SC-016 |
| "the `minisign-verify` crate … the spec confirms the current version" (610-612) | External facts (0.3.0), D1, FR-080 |
| "the trusted-comment binding … plugin, version, platform key and asset name must equal the feed entry" (612-614) | FR-037, FR-038; SC-003 h to k and m, SC-004 (`invalid_trusted_comment_is_named`) |
| "A verified artifact is marked so; nothing else is ever extracted or executed" (614) | D7, FR-039, FR-043, FR-045 to FR-047; SC-003, SC-015, SC-026, SC-029 |
| Q7 (30), Phase 10 (746-752), Cross-Cutting (997): one elevation prompt per batch | D7 (the artifact is returned by value), FR-005, FR-045; SC-026. The batch job itself is Phase 10 (Non-goals) |
| "throwaway keys made per test run … with the `minisign` crate as a dev-dependency (0.10.0 …), never committed" (615-617) | D1, FR-080, FR-081, FR-083; SC-017, SC-018 |
| Outcome: good artifact verifies; tamper, wrong size, wrong hash, other key, other plugin or version rejected; extraction and install entry points called 0 times (620-622) | SC-002, SC-003 |
| Outcome: KAT verifies through `core::verify` and fails after a one-byte change to file, signature or trusted comment; rsign2, Node and Rust agree (623-625) | FR-085; SC-004 |
| Outcome: monotonically increasing byte counts ending at the feed `size` (626) | D8, FR-022 (unconditional: `received` never decreases, also after a `Restarted`); SC-005, SC-006, SC-007, SC-008 e |
| Outcome: interrupted download resumes and verifies; a server that ignores `Range` falls back (627) | SC-006, SC-007, SC-008 |
| Outcome: disk-full and permission errors are named and leave no half file marked verified (628) | D11, FR-050, FR-051; SC-007 (`truncate`), SC-011 (including the crash cases), SC-012 |
| "Does NOT: extract, place files, or elevate" (630) | Non-goals; FR-004, FR-090 |
| Never touch the real system; no github.com in a test (982-989) | FR-085 (D13 seam), FR-087; SC-017, SC-032; Safety |
| Every artifact verified before extraction or execution; feed strings never become paths unvalidated (990-995) | FR-003, FR-030 to FR-039; SC-033; Safety |
| Logs never hold a redirect target (D12) | FR-053; SC-030 |
| Clarification Q1: the command acts on the version the UI showed | D9, FR-060, FR-062, FR-065; SC-028 (e) |
| Clarification Q2: cancellation in the API, no cancel command | D5, FR-010, FR-016, FR-017, FR-045, FR-061; SC-034, SC-019 |
| Clarification Q3: releasing a verified artifact after install | FR-049; SC-035 |
| Clarification Q4: retries are reported | D8, FR-020, FR-021, FR-023; SC-009, SC-036 |
| Clarification Q5: no overall bound | D5, FR-018; SC-037 |
| Clarification Q6: the command is permanent, two-step future UI | D9, FR-061, FR-066; SC-019 |
| No private key generated into or committed to the repo (994-995; Key decision 4, 174-176) | FR-083; SC-017 |
| The manager never adds quarantine (1001-1003) | FR-004; SC-031 (`verified_file_has_no_quarantine_macos`, PENDING-CI `check (macos-latest)`; the tree diff; `verified_file_has_no_zone_identifier_windows`) |
| Cross-platform, PENDING-CI per OS (1004-1007) | SC-010 (`_unix`), SC-012, SC-013, SC-021 to SC-023, SC-031 |
| Zero warnings; parallel tests (1008-1012) | SC-001, FR-087, SC-024 |
| The feed is the roster (1013-1015; ROSTER, 33) | FR-001, FR-088 |

---

## Open questions

None. The roadmap defers to this spec only the confirmation of the `minisign-verify` version (line 611), answered
above: 0.3.0. D1 to D13 are this spec's own decisions and stand open to challenge in the clarify scan. Revision 1's
narrowing of the progress outcome to runs without `Restarted` is withdrawn (D8 now keeps line 626 unconditional), so it
needs no ruling. Cancelling a download is not in the roadmap; the clarification session of 2026-10-31 (Q2) gave the
API a `CancelToken` (FR-017) but no cancel command, so nothing in Phase 7 sets one; revision 1's attribution of it to
Phase 12 is removed. Every clarification question is decided; see "Clarifications".

---

## Review notes

Revision 2 resolves every issue of the revision 1 review; none was rejected. Where an issue offered alternatives, the
choice and its reason:

- **D7 / batching.** `next` now receives the owned `VerifiedArtifact` (`FnOnce(VerifiedArtifact) -> T`) instead of
  cloning it out of a reference, so returning it for a batch is the API, not a convention; `obtain_and` returns
  `Obtained<T>` because `resumed` is a download fact `next` cannot see (revision 1's "a `next` that builds the summary"
  could not fill it).
- **Fixture seam (blocker).** The `#[cfg(test)]` fetch-URL override (D13) was chosen over a TLS fixture server, which
  would make `RootCerts` injectable for every Phase 5 purpose. `#[cfg(test)]` rather than `test-support`, because no
  consumer outside core's own tests needs it and src-tauri's test builds should not carry it.
- **`TestSigner` (blocker).** Moved to a `#[cfg(test)]` module; making `minisign` an optional normal dependency was
  rejected because it contradicts FR-007's set, FR-081 and the `core dependency tree` step.
- **FR-081.** The manifest-roots lock walk was chosen; extending the CI step was not possible (FR-090 fences
  `.github/workflows/`), and `cargo metadata` would make the guard run a subprocess where every other guard reads files.
- **`Channel` adapter and command logic.** Both moved into core (`channel_progress`, `for_command`) instead of a
  `src-tauri` function with a unit test, because `src-tauri` declares `test = false` for its lib and bin
  (`src-tauri/Cargo.toml:10-19`), so a test there would never run.
- **`HttpError` carrier.** A separate `StreamError` was chosen over a new `HttpError` variant or field, so no Phase 5
  match or test changes. The review cited `http_tests.rs:905` as a `Body { detail }` construction; that line is a
  `map_error(…).code()` assertion, and no test constructs `HttpError::Body` (grep this session). The finding stands
  regardless: `map_error` and `code()` match the variant.
- **SC-010 timeouts.** The review's "`timeouts().per_call` / `per_read` equal the stall bound" was adjusted: ureq 3.5.0
  keeps the per-read bound outside `Timeouts`, as `Config::timeout_per_read()` (`src/config.rs:417`), so SC-010 asserts
  that accessor and leaves `per_call` unset.
- **SC-031 (macOS).** The assertion names `com.apple.quarantine` only, not an empty `xattr -l` listing, because whether
  the OS attaches attributes of its own to a file a test binary writes has not been measured.

---

## Clarifications

### Session 2026-10-31

The clarify scan of 2026-10-31 raised six questions against revision 2; the user decided each. The decisions are in the
FR / SC rows named in brackets (and in D5 and D9), which are the text downstream stages consume.

- Q1: Which feed and version does `download_plugin` / `download::for_command` act on? B: both take `name` plus `expected_version` (the version the status screen showed); after the re-fetch a different `version_text` is refused with the new `E_DOWNLOAD_VERSION_CHANGED` before `obtain_and` runs, so no prune happens under a changed feed. [FR-060, FR-062, FR-065, FR-052, SC-028, SC-019]
- Q2: Does Phase 7 provide cancellation? B: a `&CancelToken` (`AtomicBool` wrapper) parameter on `obtain_and` and `get_streamed`, threaded through `for_command`, checked per chunk, before each attempt and during backoff; cancelling gives `E_DOWNLOAD_CANCELLED`, no retry, the `.part` kept for a later resume; no cancel command in Phase 7, the command passes a token nobody sets. [FR-010, FR-016, FR-017, FR-045, FR-052, FR-060, FR-061, SC-034]
- Q3: How long does a verified artifact stay in staging after use? B: add `download::discard(artifact: VerifiedArtifact)`, which consumes the artifact and removes its `<hex>/` directory through `StagingIo`, for Phases 9 and 10 to call after a successful install; FR-005's prune rule is unchanged. [FR-049, FR-005, SC-035]
- Q4: Does `Progress` report retries? B: add `Progress::Retrying { attempt: u32, of: u32, code: String, wait_ms: u64 }`, emitted before each backoff; FR-021's order and SC-009's counts include it; no heartbeat event. [FR-020, FR-021, FR-023, SC-009, SC-036]
- Q5: What bounds one `obtain_and` in wall-clock time? A (given Q2 = B): keep 3600 s per attempt, 4 attempts, no overall bound; the cancel token is the way to stop a too-slow download, and a moving trickle is not an error. [FR-018, FR-084, SC-037, SC-020]
- Q6: Is `download_plugin` a permanent webview command? A: it stays a permanent webview command (the sixth command and capability permission); the future UI is two-step (bounded parallel downloads, then one install batch over the cached verified files). [FR-061, FR-066, SC-019]

### Session 2026-10-31 (plan preconditions)

The user ruled on the two preconditions of plan §10 and tasks.md "Preconditions".

- Q-A (plan SI-9, PD-5): what does the progress stream show when a cached verified file fails re-verification? → as
  recommended: `Restarted { reason: CachedInvalid }` before the fresh download. [FR-020, FR-046, SC-029]
- Q-B (plan PD-11): `obtain_and` (8 parameters) and `for_command` (9) exceed clippy's `too_many_arguments` limit →
  AGAINST the recommendation (the user's choice): no `#[expect(clippy::too_many_arguments)]`. The shared inputs are
  bundled into a `DownloadEnv` struct so every public download entry point stays within clippy's default limit of 7
  parameters with no lint suppression; the plan was revised in place for this ruling and tasks.md regenerated from it,
  so this bullet does not block T001 / T002. [FR-045, FR-060]
- R-1 (opened by Q-B): does the `CancelToken` travel inside `DownloadEnv` or stay a separate parameter? → as
  recommended: inside the env (`DownloadEnv::cancel`); behaviour unchanged (FR-017); `get_streamed` keeps its
  `&CancelToken` parameter. [FR-017, FR-045, FR-060, SC-034]
