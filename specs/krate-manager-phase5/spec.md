# Feature Specification: Krate Manager Phase 5 — Feed Client and Version Model

**Spec slug:** `krate-manager-phase5`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 5 (lines 527-552); Decisions log Q9 (line 32), ROSTER (line 33);
Positioning (lines 52-56: the manager reads `https://krateaudio.com/versions.json` directly); "Feed and artifact model"
(lines 128-148); Phase 4's injection note (lines 476-479: "the HTTP client base joins `AppContext` in Phase 5");
Phase 3's fixture hand-over (lines 451-453); Phase 11's "not available for this Mac" (lines 818-821); Cross-Cutting
Constraints (lines 968-1009)
**Depends on:** Phase 1 (the C1 schema, the reader rules and the fixtures in `tools/feed-contract/`, read in place;
edge 1→5, roadmap line 940) and Phase 4 (`Roots`, the state dir, `AppContext`, `test_support`, `manager-ci.yml`; edge
4→5). It needs no Phase 2 or Phase 3 output except the opt-in live test (roadmap lines 943-946).
**Consumed by:** Phase 6 (the typed feed, `Availability`, `UpdateState`, `reduce_class_version`; roadmap lines 556-589),
Phase 7 (`core::http`, the fixture server, `parse_minisig_text` / `parse_trusted_comment`, `PlatformAsset`; lines
596-606), Phase 9 and 10 (`InstallFacts`, `AssetFormat`), Phase 11 (`ManagerInfo`, platform resolution on Intel Macs,
lines 818-821), Phase 12 ("Update all" over `UpdateState`).
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-10-30 · verified against `feat/krate-manager` at `a28b1181` ("docs(krate-manager): roadmap Phase 3 status
line", `git log --oneline -1`, clean tree); `specs/krate-manager-phase5/` did not exist before this file.
**Revised:** 2026-10-30 after the challenge review (19 issues; resolutions in the FR / SC rows, rejections and partial
adoptions in Review notes), then after the clarify session of 2026-10-30 (eight decisions, `## Clarifications`; new
FR-037, FR-038, FR-046, FR-048, FR-049, FR-058, FR-059 and SC-026 to SC-032), then after the second clarify session of 2026-10-31 (seven rulings R-1 to R-7: PAC and WPAD in v1 on all
three OSes, the OS proxy readers, the `ureq` features, the macOS-only core dependency, the per-asset drop; new FR-029,
FR-064 to FR-069, FR-073 to FR-075 and SC-033 to SC-040), then after the third clarify session of 2026-10-31 (rulings R-8 to R-15: the PAC engine, WPAD discovery, the answer walk, PAC DNS and local time, the script world, and Windows evaluating PAC through the OS; new FR-092 to FR-095 and SC-041 to SC-044). The `ureq` 3.5.0 and `rustls` 0.23.46 behaviour the
revision relies on was read from the published crates (https://static.crates.io/crates/ureq/ureq-3.5.0.crate,
https://static.crates.io/crates/rustls/rustls-0.23.46.crate, the `rustls` version `ureq` 3.5.0 requires,
`Cargo.toml:206-207`) and is cited by file:line in External facts.

---

## Overview

Phase 5 gives the Tauri-free core its first network code. `core::contract` validates a `versions.json` document against
the Phase 1 contract by interpreting the committed JSON Schemas themselves (`tools/feed-contract/versions.v2.schema.json`
and `versions.v1.schema.json`, embedded at compile time) and porting the contract's code rules (minisign text shape,
the C3 trusted-comment grammar, install values, install uniqueness), so its verdict on every fixture in
`tools/feed-contract/fixtures/feeds/cases.json` equals the Node validator's. `core::feed` turns an accepted document into
a typed model whose plugin list is exactly the feed's `plugins` keys (ROSTER), fetches it from the injected URL through
`core::http` (a blocking `ureq` client that allows only HTTPS in production and only loopback addresses in tests),
keeps the last good copy in the state dir and falls back to it, flagged stale, when the network fails or a fetched
document is rejected. `core::version` holds the version model: three-part core versions with SemVer precedence, the
reduction of four-part VST3 class versions to three parts, the installed-versus-available comparison, and the
resolution of the running machine's platform key (native key, then `darwin-universal`) and the comparison of the chosen
asset's `min_os` with an injected running-OS version. The HTTP client trusts the OS certificate store and honours the
proxy environment variables and the OS proxy settings, both injected as data (`HttpConfig.proxy`, and `HttpConfig.pac`
for a PAC script or WPAD auto-detect, which the manager evaluates itself with a bundled JavaScript engine (`rquickjs`, `core::pac`) on macOS and Linux, and which the OS evaluates on Windows through WinHTTP). A local fixture HTTP server
(test-only, `std::net`) serves the fixtures; an opt-in live test against the real feed is feature-gated, ignored, and
never runs in a gate. `AppContext` gains the HTTP configuration as its second injected field, with its production value
built in `src-tauri` setup. Nothing is rendered, no Tauri command is added, and no plug-in folder is read.

---

## Scope

**In scope (roadmap Phase 5 components, lines 531-538):**

1. `core::feed`: fetch from the injected URL over HTTPS (HTTP only to the local fixture server), parse schema 1 and 2,
   ignore unknown keys, validate every v2 entry against the Phase 1 contract, keep the last good copy in the state dir
   for offline use; a manual "check now" (lines 532-535; D2-D8).
2. `core::version`: SemVer compare, platform-key resolution for the running OS / arch (native key, then
   `darwin-universal`), the `min_os` comparison against an injected running-OS version (`Availability::NeedsNewerOs`),
   four-part class versions reduced to three parts (lines 536-537; D9, D10).
3. A local fixture HTTP server used by tests; an opt-in live-network test, ignored by default and never run in a gate
   (line 538; D12, D13).
4. The HTTP client configuration as an injected `AppContext` field with its production value built in `src-tauri`
   setup (roadmap lines 476-479; Phase 4 spec D17; D11).
5. The roster and host guards the measurable outcomes name (lines 547-550; D14).
6. PAC and WPAD proxy auto-configuration on Windows, macOS and Linux: evaluated in process by a bundled JavaScript engine on
   macOS and Linux, by the OS (WinHTTP) on Windows (D6, D16, FR-064 to FR-069, FR-092 to FR-095).

**Non-goals (owned elsewhere):**

| Not in Phase 5 | Owner |
|---|---|
| Reading plug-in folders, `moduleinfo.json`, the registry, `pkgutil`; choosing class versus top-level `Version`; the status screen; rendering `notes_markdown` | Phase 6 (roadmap lines 556-589). Phase 5 supplies `reduce_class_version` and `CoreVersion::parse` only |
| Any Tauri command, capability change, or UI change (no command calls the feed in this phase) | Phase 6 (the first visible product, line 570). The roadmap's "Does NOT … render UI beyond what tests need" (line 552) |
| Downloading assets, `Range` resume, streamed hashing, minisign cryptography, the embedded public key, the C3 **binding** check (`checkBinding`) | Phase 7 (lines 596-606). Phase 5 ports only the structural parsers the feed's code rules need (FR-005, FR-006) |
| An origin rewrite that sends `https://github.com/...` asset URLs to the fixture server | Phase 7, if its tests need one; Phase 5's fixture tests fetch the feed URL itself, which is already injected |
| Reading the real running-OS version (macOS product version, glibc version, Windows version) | Phase 6, through `Platform` (Clarifications Q3). Phase 5 supplies the pure comparison and takes the version as an injected parameter (FR-035, FR-037, FR-038) |
| The `prerelease` sibling as an installable channel | Not in v1 (Q9, roadmap lines 32, 962). Phase 5 validates it with the document and leaves it out of the model (FR-024) |
| `manager.latest_json_url` as an updater endpoint | Phase 11 (lines 829-831: the updater endpoint is compiled in). Phase 5 carries the value (FR-028) |
| Conditional requests (`If-None-Match` / `ETag`) | Not named by the roadmap; the feed is 1,246 bytes today (measured, External facts) |
| Any plugin source, installer, `release.yml`, `feed.yml`, `ci.yml`, or a `tools/feed-contract/` contract or fixture file | Never in this phase (FR-090) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 5 does with it |
|---|---|---|
| `tools/feed-contract/versions.v2.schema.json` (245 lines) | C1, JSON Schema 2020-12, `$id` `https://krateaudio.com/schemas/versions.v2.schema.json`; keywords used (counted this session): `$schema, $id, title, $comment, $defs, type` (`object`, `string`, `integer`), `properties, additionalProperties, patternProperties, propertyNames, required, dependentRequired, const, enum, pattern, minimum, maximum, not, allOf, $ref, if, then, else`, boolean schemas | ✅ Embedded at compile time and interpreted (FR-001, FR-002). Not modified |
| `tools/feed-contract/versions.v1.schema.json` (22 lines) | The v1 compatibility floor: `plugins` an object of objects with a string `version`, optional string `download_url` / `release_notes` | ✅ Embedded and interpreted for `schema_version` 1 (FR-001, D3) |
| `tools/feed-contract/CONTRACT.md:28` | "Phase 5 (the manager's Rust feed client) parses C1 with these reader rules, reuses the error-code names where it rejects the same thing" | ✅ The code vocabulary of FR-003, FR-008 |
| `tools/feed-contract/CONTRACT.md:32-112` | C1 field table (types, required-ness, patterns, readers) and the code rules run after Ajv (`:105-108`) | ✅ The model fields of FR-021 |
| `tools/feed-contract/CONTRACT.md:114-142` | Reader rules 1-8: ignore unknown keys; platform resolution (native key, then `darwin-universal`; keys outside the five ignored); manager v1 ignores `prerelease`; asset name = last URL segment; install uniqueness (a duplicate "must not let either plugin be installed or removed through the manager"); "not available through the manager"; no reserved names; never a path unchecked | ✅ FR-022 to FR-026, FR-034, FR-035, D1 |
| `tools/feed-contract/CONTRACT.md:198-242` | C3 grammar (`krate:1:<Name>:<version>:<platform-key>:<asset>`, codes `E_TC_FIELDS/NAME/VERSION/PLATFORM/ASSET`) and the `.minisig` text form (four lines, LF, 74-byte `ED` blob, 64-byte global signature, canonical base64) | ✅ Ported structurally (FR-005, FR-006) |
| `tools/feed-contract/contract.js:226-242`, `:249-271`, `:276-280`, `:299-318`, `:363-384`, `:405-413`, `:421-478`, `:485-492` | `checkInstallValues`, `findInstallDuplicates` (ASCII case fold for `vst3_bundle`, `au_component`, `macos_pkg_id`; an entry and its own sibling are one plugin), `definedPlatformKeys`, `parseTrustedComment`, `parseMinisigText`, `ajvFindings` (`{code: 'E_SCHEMA', instancePath, keyword}`), `feedCodeFindings`, `validateFeed` (schema findings then, for v2, code findings) | ✅ The behaviour FR-003 to FR-007 port to Rust. Not modified |
| `tools/feed-contract/fixtures/README.md:46-105` | `feeds/` set: `valid/`, `invalid/`, `cases.json` rows `{rule, file, schema, expect, plugin?}`; an error row passes "when `X` is among `validateFeed`'s findings"; `malformed-json`'s finding is `E_INPUT`; "Phase 5 (the Rust feed client reads this set in place and must reach each row's verdict)" (`:103-105`) | ✅ Read in place by SC-002 |
| `tools/feed-contract/fixtures/feeds/cases.json` | 90 rows this session: 10 `accept`, 1 `accept-without-platforms` (`unknown-platform-key`, plugin `Delta`), 79 errors; row `Phase3/one-v2-eight-v1` is Phase 3's nine-plugin feed (roadmap lines 451-453) | ✅ SC-002, SC-010, SC-014 |
| `tools/feed-contract/fixtures/feeds/valid/*.json` (11 files) | `full-v2` (Alpha `windows-x86_64, darwin-aarch64, linux-x86_64`; Bravo `windows-x86_64, darwin-x86_64, linux-x86_64` + sibling; Charlie `windows-x86_64, darwin-universal, linux-x86_64`; `manager`), `mixed` (+ v1-only `Delta`), `tenth-plugin` (10 plugins), `one-v2-eight-v1` (9), `v1-only` (`schema_version` 1, 3 plugins), `unknown-platform-key` (`Delta` only `windows-aarch64`), `unknown-extra-keys`, `prerelease-plain`, `prerelease-suffixed`, `sibling-same-install`, `notes-14000` (listed this session) | ✅ SC-005, SC-006, SC-014 |
| `tools/feed-contract/fixtures/feeds/invalid/schema_version-const.json` | A full-v2 body with `"schema_version": 1`; its `cases.json` row checks it under the **v2** schema (`/schema_version const`) | ✅ The one row whose client verdict differs from its schema-level verdict (D3, SC-004) |
| `tools/feed-contract/fixtures/gen-feed/*/expected.v2.json` (57 files) | Generator outputs; all 57 pass `validateFeed(doc, {schema: 'v2'})` (run this session with `contract.js`) | ✅ SC-003: every one parses with zero findings |
| `tools/feed-contract/fixtures/trusted-comments.json`, `fixtures/kat/` (README `:27-45`, `:162-188`) | C3 `valid` / `invalid` / `binding` rows; the real rsign2 `.minisig` (trusted comment `krate:1:KatPlugin:1.2.3:linux-x86_64:KatPlugin-1.2.3-Linux-x64.tar.gz`) | ✅ SC-020 (structure only; Phase 7 does the cryptography) |
| `plugins/shared/src/update/update_checker.cpp:209-251` | The shipped parser: reads only `plugins.<Name>.{version, download_url, release_notes}`, never `schema_version`; any exception gives an empty result | ✅ Unchanged; the reason the feed stays additive. Phase 5 reads, never writes, the feed |
| `plugins/iterum/src/update/iterum_update_config.h:12` | Endpoint `https://rolandzwaga.github.io/krate-audio/versions.json` (301 to `https://krateaudio.com/versions.json`, measured) | ✅ Unchanged; the manager uses the `krateaudio.com` URL directly (roadmap line 55) |
| `plugins/*/version.json` (`name`) | Nine names this session (`Disrumpo, Gradus, Innexus, Iterum, Membrum, Profundum, Ruinae, Seraphis, Vorago`); `plugins/shared/` has none; neither embedded schema file contains any of them or `api.github.com` (grep this session) | ✅ The word list of the roster guards, read at test / step time (D14, FR-082, FR-083) |
| `tools/check-bundle.js:50-54` | Every class `Version` must equal `version.json` or start with `"<ver>."` | ✅ The rule `reduce_class_version` follows (FR-031). A built Iterum bundle's moduleinfo has top-level `"1.0.0"` and class `"1.0.0.1"` (`build/windows-x64-release/VST3/Release/Iterum.vst3/Contents/Resources/moduleinfo.json:3,20`) |
| `apps/krate-manager/crates/core/src/context.rs:47-53`, `:66-102`, `:126-161`, `:190-199` | `AppContext { roots, settings, feed_url }` (`Clone, Debug, PartialEq`), `assemble_context(roots, feed_url)`, `reassemble_with_os`, `parse_dev_feed_url` (accepts `http`/`https`) | 🔶 `AppContext` gains `http: HttpConfig`; `assemble_context` takes it; `reassemble` keeps it (FR-060) |
| `apps/krate-manager/src-tauri/src/lib.rs:16`, `:26-34` | `const FEED_URL: &str = "https://krateaudio.com/versions.json";`; the `dev-feed` override replaces the URL | 🔶 Passes `HttpConfig::production()`, or under `dev-feed` the config `dev_feed::http_config` derives from the override URL (FR-061) |
| `apps/krate-manager/src-tauri/src/dev_feed.rs:1-11` | `DEV_FEED_FILE`, `read(state_dir)` | 🔶 Gains `http_config(url)` (FR-061) |
| `apps/krate-manager/crates/core/Cargo.toml:13-19` | `[dependencies]` `log, serde, serde_json, tempfile, thiserror, url` | 🔶 Adds `ureq` (with `platform-verifier`), `rustls`, `base64`, `regex` (FR-040, FR-080), `flate2` under `[dev-dependencies]` (FR-040) and the `live-network` feature (FR-072) |
| `apps/krate-manager/Cargo.toml` (`[workspace.dependencies]`, `[profile.release]` with `strip = true`, `panic = "abort"`) | Workspace pins | 🔶 Pins `ureq = { version = "3.5.0", features = ["platform-verifier"] }` (the plan fixes the exact feature list, FR-040), `base64 = "0.23.1"`, `regex = "1.13.1"`, `rustls = { version = "0.23.46", default-features = false }`, `flate2 = "1.1.10"` |
| `apps/krate-manager/Cargo.lock` | Already holds `base64 0.23.1`, `regex 1.13.1`, `semver 1.0.28`, `reqwest 0.13.5` (via `tauri`, no TLS feature), `tokio 1.54.1`; no `ureq`, `rustls`, `ring`, `webpki-roots` (read this session) | 🔶 Regenerated with `ureq` and its TLS stack; CI keeps `--locked` |
| `apps/krate-manager/crates/core/tests/injection.rs:12-20`, `:82-84`, `:453-455`, `:562-608`, `:818-823`, `:826-849` | `FORBIDDEN_OUTSIDE_PLATFORM` (includes `"krateaudio.com"`, `"api.github.com"`) over every scanned core file, comments included; `FR007_DEPENDENCIES`; `is_scanned` (skips `test_support.rs` and `*_tests.rs`); `core_dependencies_are_the_fr007_set`; `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` (exactly one `krateaudio.com` in `src-tauri/src`, in `lib.rs`) | 🔶 `FR007_DEPENDENCIES` gains four names (FR-080); the env-read test admits the proxy variables in one `src-tauri` file (FR-063); a new `ureq` confinement test (FR-081). The host rules stand, so no core file may name the feed host, even in a comment |
| `apps/krate-manager/crates/core/tests/config_guard.rs:37`, `:512-514`, `:946-952` | Static guard over `src-tauri` (`assemble_context(` present, `log_sink::init(` after it) | 🔶 Gains the FR-063 assertions |
| `apps/krate-manager/crates/core/src/test_support.rs:30-38`, `:117-130` | `canonical_temp_dir`, `temp_roots`, `fresh_temp_roots` | ✅ Every Phase 5 test that writes uses `fresh_temp_roots()` |
| `apps/krate-manager/crates/core/src/settings.rs:219-232`, `:280-302` | `SettingsIo` fault points and `save_with`: create dir → create temp → write → sync temp → persist → (Unix) sync dir | ✅ The write sequence the cache follows (FR-052); `settings.rs` behaviour and tests unchanged |
| `apps/krate-manager/crates/core/src/platform/mod.rs:9`, `:12`, `:55-56` | `APP_IDENTIFIER`, `pub enum Os`, `trait Platform { fn os(&self) -> Os; … }` | 🔶 `Os` is reused by `core::version`; `APP_IDENTIFIER` is the positive control of FR-083. `trait Platform` is unchanged: the per-OS system-proxy reader is `SystemProxyFromOs for NativePlatform` (FR-049) (plan SI-1) |
| `apps/krate-manager/tests/required-tests.json` | `cargo.all`, `cargo.windows / macos / linux`, `vitest` lists | 🔶 Gains this phase's test names (FR-085) |
| `.github/workflows/manager-ci.yml:5-15`, `:115-118`, `:135-160`, `:182-192` | Path filter (`apps/krate-manager/**`, itself, `tools/lint-manager-version.js`); `cargo test --workspace --locked`; "required tests ran" (exactly one passing line per name); "dev-feed absent" grep over the release binary | 🔶 Path filter adds `tools/feed-contract/**`; a "roster absent" step (FR-083, FR-084) |
| `tools/feed-contract/tests/manager-ci-shape.test.js:147`, `:156-198`, `:315`, `:400-401`, `:636` | `PATHS`, `STEPS`, `DEV_FEED_STEPS`, the paths assertion, the top-level test | 🔶 Updated for FR-083, FR-084 and the `live-network` ban (FR-072) |

---

## External facts used (verified this session)

| Fact | Source |
|---|---|
| `ureq` 3.5.0 (2026-10-21, `rust-version = "1.85"`, inside the workspace's 1.90 floor); `default = ["rustls", "gzip"]`; `rustls = ["rustls-no-provider", "_ring", "rustls-webpki-roots"]`; `platform-verifier` is a separate feature | https://crates.io/api/v1/crates/ureq ; `Cargo.toml.orig` of https://static.crates.io/crates/ureq/ureq-3.5.0.crate |
| `Agent::new_with_config(Config)`, `Agent::config_builder()`, `agent.get(uri)`, `RequestBuilder::header(k, v)`, `.call() -> Result<Response<Body>, Error>`; `ConfigBuilder::https_only(bool)` ("limit requests (including redirects) to https only", default `false`), `max_redirects(u32)` ("If `max_redirects` is 0, no redirects are followed and the response is always returned"), `http_status_as_error(bool)` (default `true`), `timeout_global(Option<Duration>)`, `timeout_connect(Option<Duration>)`, `proxy(Option<Proxy>)`, `user_agent(…)`; `Body::with_config().limit(u64).read_to_vec()`, default body limit 10 MB | ureq 3.5.0 `src/agent.rs:112,123,326`, `src/request.rs:86,469`, `src/config.rs:520-593,632,761,848`, `src/body/mod.rs:300-330,421,502` |
| `https_only` is enforced per call: `if config.https_only() && uri.scheme() != Some(&Scheme::HTTPS) { return Err(Error::RequireHttpsOnly(…)) }` | ureq 3.5.0 `src/run.rs:155-156`, `src/error.rs:141-143` |
| `Config::default()` sets `proxy: Proxy::try_from_env()`, which reads `ALL_PROXY`, `HTTPS_PROXY`, `HTTP_PROXY` (both cases) and `NO_PROXY` | ureq 3.5.0 `src/config.rs:1073`, `src/proxy.rs:218-250` |
| `ureq::Error` variants include `StatusCode(u16)`, `Io`, `Timeout`, `HostNotFound`, `ConnectionFailed`, `BodyExceedsLimit(u64)`, `TooManyRedirects`, `Tls`, `Rustls`, `RequireHttpsOnly`, `Protocol`, `Decompress`, `BodyStalled` | ureq 3.5.0 `src/error.rs` (`pub enum Error`) |
| TLS roots default to `RootCerts::WebPki` (Mozilla's bundle, compiled in); `RootCerts::PlatformVerifier` needs the `platform-verifier` feature | ureq 3.5.0 `src/tls/mod.rs:154,221,345,353,375` |
| `reqwest` 0.13.5 (already in the lock via `tauri` 2.12.5, `default-features = false`, `json, stream`): its `rustls` feature is `["__rustls-aws-lc-rs", "dep:rustls-platform-verifier", "__rustls"]`; `blocking` pulls `tokio/sync` and futures; `tauri-plugin-updater` 2.14.1 also depends on `reqwest ^0.13` | `Cargo.toml.orig` of https://static.crates.io/crates/reqwest/reqwest-0.13.5.crate ; https://crates.io/api/v1/crates/tauri/2.12.5/dependencies ; https://crates.io/api/v1/crates/tauri-plugin-updater/2.14.1/dependencies |
| `jsonschema` 0.60.2 (2026-10-26) default features `resolve-http, resolve-file, tls-aws-lc-rs, idna`; non-optional dependencies include `ahash, fancy-regex, fraction (with-bigint), uuid-simd, referencing, email_address, strum, …` | https://crates.io/api/v1/crates/jsonschema and `/crates/jsonschema/0.60.2/dependencies` |
| `base64` 0.23.1 (latest, `rust-version = "1.71.0"`): `engine::general_purpose::STANDARD` = standard alphabet with padding and `decode_allow_trailing_bits: false`; `Engine::encode`, `Engine::decode` | `src/engine/general_purpose/mod.rs:331-341,436`, `src/engine/mod.rs:148,277` of https://static.crates.io/crates/base64/base64-0.23.1.crate |
| `regex` 1.13.1 (2026-07-15, MSRV 1.65), already in the lock | https://crates.io/api/v1/crates/regex |
| JSON Schema 2020-12: `integer` matches any number with a zero fractional part; `const` / `enum` compare instances by JSON equality (numbers mathematically); `pattern` is an ECMA-262 regex, not implicitly anchored; `additionalProperties` applies to keys not matched by `properties` or `patternProperties`; `propertyNames` validates each key as a string | https://json-schema.org/draft/2020-12/json-schema-core (§4.2.1, §4.2.2, §10.3.2.3) and https://json-schema.org/draft/2020-12/json-schema-validation (§6.1.2, §6.1.3, §6.3.3) |
| Live feed, 2026-10-30: `https://krateaudio.com/versions.json` is `schema_version` 1, nine plugins, 1,246 bytes, `Content-Type: application/json; charset=utf-8`, `Cache-Control: max-age=600`, `ETag`, served by Fastly (`Via: 1.1 varnish`, `X-Cache`). A request with a unique query string (`?cb=<random>`) and one with `Cache-Control: no-cache` + `Pragma: no-cache` both came back `X-Cache: HIT` with the same `Age: 13`/`14` as a plain request: **the Pages edge ignores both the query string and the request directives**. `https://rolandzwaga.github.io/krate-audio/versions.json` answers `301` with `Location: https://krateaudio.com/versions.json` | `curl -sI` from this box, 2026-10-30 (three requests within one second, after a 5-second wait) |
| The research saw the same `max-age=600` and recommended "re-fetch on a manual 'Check now' with `Cache-Control: no-cache`" | `specs/krate-manager/research_notes/Krate Audio plugin manager design/feed_and_ci.md:106,112,123,131` |
| RFC 9111 §5.2.1.4: the `no-cache` request directive "indicates that the client prefers a stored response not be used to satisfy the request without successful validation on the origin server" (a preference caches may honour) | https://www.rfc-editor.org/rfc/rfc9111#section-5.2.1.4 |
| `ureq` 3.5.0 body limit: `LimitReader::read` returns `Err(BodyExceedsLimit)` whenever `left == 0`, before reading the inner reader (`src/body/limit.rs:22-25`); `BodyWithConfig::read_to_vec` calls `read_to_end` (`src/body/mod.rs:605-610`), which reads again after the last byte to find EOF, so a body of exactly `limit` bytes fails. The limit sits **under** the content decoder: `BodyReader.reader` is `MaybeLossyDecoder<CharsetDecoder<ContentDecoder<LimitReader<BodySourceRef>>>>` (`mod.rs:783`), built in `do_build` (`mod.rs:518-525`), so it counts wire bytes. `Body::as_reader()` = `with_config().reader()` (`mod.rs:230-232`) decodes, with a default `BodyWithConfig` limit of `u64::MAX` (`mod.rs:491`, "The default limit is `u64::MAX` (unlimited)", `:501`) | ureq 3.5.0 crate source (above) |
| `ureq` 3.5.0 with the `gzip` feature sends `Accept-Encoding: gzip` on every request that sets none (`src/run.rs:307-328`) | ureq 3.5.0 crate source |
| TLS errors: the rustls handshake runs as `conn.complete_io(&mut sock)?` (`ureq src/tls/rustls.rs:78`); `ClientConnection::new` is the only source of `Error::Rustls` (`rustls.rs:75`, `From<rustls::Error>` at `src/error.rs:295-299`); `From<io::Error> for Error` turns every io error that does not wrap a ureq error into `Error::Io` (`error.rs:214-227`). rustls 0.23.46 `complete_io` wraps every record-processing error (a corrupt record, a certificate failure) as `io::Error::new(io::ErrorKind::InvalidData, e)` with `e: rustls::Error` (`src/conn.rs:617-618`, `:697-701`), but an EOF during the handshake is a plain `io::ErrorKind::UnexpectedEof` with no `rustls::Error` inside (`conn.rs:725`). A record whose header content-type byte is not a TLS one fails `read_opaque_message_header` (`src/msgs/message/outbound.rs:274-280`) and becomes `rustls::Error::InvalidMessage(InvalidContentType)` (`src/msgs/deframer/mod.rs:60`) | ureq 3.5.0 and rustls 0.23.46 crate sources |
| `flate2` 1.1.10 is already in `Cargo.lock` (`Cargo.lock:769-778`; `ureq`'s `gzip` feature also depends on `flate2 ^1.0.30`, ureq `Cargo.toml:183-185`); `flate2::write::GzEncoder::new(w, Compression)` and `finish()` | `~/.cargo/registry/src/…/flate2-1.1.10/src/gz/write.rs:46,70,148` |
| `regex` 1.13.1: `.` is "any character except new line"; `\s` is "whitespace (`\p{White_Space}`)" (Unicode White_Space includes U+0085 NEL and excludes U+FEFF). ECMA-262 `\s` is WhiteSpace ∪ LineTerminator: U+0009, U+000B, U+000C, U+0020, U+00A0, U+FEFF, every `Zs` (U+1680, U+2000-U+200A, U+202F, U+205F, U+3000), and U+000A, U+000D, U+2028, U+2029; ECMA `.` without the `s` flag excludes the four LineTerminators | `~/.cargo/registry/src/…/regex-1.13.1/src/lib.rs:669,928`; https://tc39.es/ecma262/#sec-white-space , https://tc39.es/ecma262/#sec-line-terminators , https://tc39.es/ecma262/#sec-compiletocharset |
| `tauri::async_runtime::spawn_blocking` exists in tauri 2.12.5 (for Phase 6, which calls the blocking client off the main thread) | `tauri-2.12.5/src/async_runtime.rs:311` (local cargo registry copy) |

---

## Decisions taken in this spec (open to challenge in the clarify scan)

The roadmap defers no decision to this spec by name. D1-D15 settle points the roadmap names but does not pin down.

- **D1, a feed is accepted or rejected as a whole.** Any finding rejects the document (`E_FEED_INVALID`, carrying every
  finding) and the last good copy stays (roadmap line 545: "a rejected feed never replaces the last good copy"). Reasons:
  reader rule 5 (CONTRACT.md:127-135) requires that a duplicate install fact block both plugins, which per-entry
  acceptance would have to re-derive; the generator validates its own output before deploy (Phase 3's `dry_run`,
  roadmap lines 436-441), so an invalid live feed is a pipeline defect, not a state to route around; the contract
  defines no per-entry trust rule. Consequence, accepted: one malformed entry hides every update until the feed is
  fixed; the user still sees the cached copy, flagged stale, with the named error. **Ruled 2026-10-30 (Clarifications
  Q1, option B): the `prerelease` sibling is the one exception.** Contract findings under `/plugins/<Name>/prerelease`
  are dropped and logged (one `warn!` each) in `feed::parse`, so a broken beta sibling never blocks stable updates
  (Q9, roadmap lines 32, 543: the manager "ignores" the sibling in v1); every other finding still rejects the whole
  document. `contract::validate` itself is unchanged and still reports sibling findings (its verdicts equal the Node
  validator's, SC-002); the drop is a `feed::parse` filter (FR-024, SC-010).
- **D2, the contract is interpreted, not re-typed.** `core::contract` embeds the two committed schema files with
  `include_str!` and runs a small interpreter for exactly the keywords they use (Existing repo pieces), reporting each
  failure in `contract.js`'s own vocabulary (`E_SCHEMA` + RFC 6901 `instance_path` + `keyword`). The code rules
  (`feedCodeFindings`) are ported by hand. Not the `jsonschema` crate: its default features pull an HTTP and TLS stack
  and it adds about twenty crates (External facts); not a hand-written per-field validator: the schema would then exist
  twice and drift. An unsupported keyword in a future schema fails the build's tests (FR-001), never silently passes.
  `regex` 1.13.1 evaluates `pattern` (already in the lock). **Ruled 2026-10-30 (Clarifications Q6, option A):** the
  interpreter over the embedded committed schemas plus the hand-ported code rules, Ajv-equivalent findings and the ECMA
  pattern translation, proven by parity tests over every `cases.json` row and every gen-feed output (FR-001 to FR-004,
  SC-002, SC-003, SC-025).
- **D3, schema dispatch.** `schema_version` absent or `1` → the v1 floor only; every entry is v1-only (its `platforms`
  and `install`, if any, are ignored, because only a version-2 document promises C1). `2` → C1 (schema plus code rules).
  An integer above 2 → `E_FEED_FUTURE_SCHEMA { found }` ("a newer Krate Manager is needed"). Anything else (0, a
  negative, a fraction, a string) → `E_FEED_SCHEMA_VERSION`. So `invalid/schema_version-const.json`, which `cases.json`
  rejects under the v2 schema, is accepted by the client as a v1 document whose three entries are all "not available
  through the manager" (SC-004).
- **D4, HTTP client: `ureq` 3.5.0 with its `platform-verifier` feature and `RootCerts::PlatformVerifier`** (ruled
  2026-10-30, Clarifications Q4, option B): rustls on `ring`, gzip, SOCKS proxy support (`socks-proxy`; the `ureq` default features are kept, ruled 2026-10-31,
Clarifications R-5), and the **OS trust store on all three OSes** as the
  only trust source, so a TLS-intercepting corporate proxy whose CA the OS trusts works. The agent's TLS config sets
  `RootCerts::PlatformVerifier` explicitly (FR-040, FR-043, SC-026). It is blocking with no async runtime, so `core`
  stays Tauri-free and runtime-free and its tests use plain threads; it enforces `https_only` itself. Not `reqwest`: its
  blocking client runs a tokio runtime inside (External facts). `reqwest`'s `rustls` feature uses the same family of
  verifier (`rustls-platform-verifier`, External facts), and the plan verifies which `reqwest` / `rustls` features
  `tauri-plugin-updater` (Phase 11) actually uses and records whether the two TLS stacks agree (FR-040, SC-017).
  Consequence, accepted: Phase 11's `tauri-plugin-updater` brings `reqwest`'s TLS stack too, so the shipped app carries
  two TLS stacks (the plan's record says whether they differ in provider or verifier). That an untrusted certificate is
  reported as `E_FEED_TLS` follows from FR-044's downcast rule (rustls reports a certificate failure through the same
  `InvalidData` path as any record error, External facts) but is **not tested** in Phase 5: no test has a certificate
  (Review notes).
- **D5, the URL policy is a value with two variants.** `UrlPolicy::HttpsOnly` (production): every request and every
  redirect hop must be `https`. `UrlPolicy::LoopbackOnly` (tests, and the `dev-feed` build when its URL is loopback):
  every hop must be `http` or `https` to the IP literal `127.0.0.1` or `[::1]` (not `localhost`, whose resolution is
  not the client's to vouch for). The client follows redirects itself (`ureq` `max_redirects(0)`), at most
  `MAX_REDIRECTS = 5` hops, and checks every hop against the policy before connecting, so a test can never reach the
  network and production can never fall back to `http`.
- **D6, the proxy is injected data, never read by core (ruled 2026-10-30, Clarifications Q5, option C).** Production
  honours `HTTPS_PROXY` / `ALL_PROXY` / `NO_PROXY` (read in `src-tauri` setup) **and** the OS proxy settings (Windows
  Internet settings, macOS SystemConfiguration, GNOME / KDE on Linux), read by a per-OS reader behind `Platform` (three
  implementations, each tested with injected inputs). The resolved result is injected as `HttpConfig.proxy`
  (`Option<ProxyConfig>`) and `HttpConfig.pac` (`Option<PacSource>`, D16), both `None` in every test that does not test the proxy. `ureq`'s `Config::default()`
  reads the proxy variables itself (External facts); the client therefore always replaces the value explicitly with
  `config.proxy` (a `None` means no proxy), so core's own code reads no environment (Phase 4 D9); `ureq` reads the proxy
  variables while building its default config and the value is always replaced (FR-043) (plan SI-11), and tests are independent
  of a runner's `HTTP_PROXY`. **PAC and WPAD are in v1 on all three OSes (ruled 2026-10-31, Clarifications R-1)**, and the
  environment wins over the OS settings (FR-048, FR-069); the PAC half of the injected proxy is `HttpConfig.pac` (D16).
- **D7, every check goes to the network; the cache is only a fallback.** There is no local freshness window. "Check
  now" (`FetchMode::CheckNow`) differs from a normal check only in sending `Cache-Control: no-cache` and `Pragma:
  no-cache`, which RFC 9111 caches that honour the directive (a proxy, a CDN that respects it) will revalidate.
  **Measured conflict with roadmap lines 534-535** ("A manual 'check now' bypasses caches"): the Pages edge ignores
  both directives and a unique query string (External facts), so no client can see a release sooner than the edge's
  `max-age=600`. **Ruled 2026-10-30 (Clarifications Q2, option A):** "check now" is an unconditional network fetch plus
  `Cache-Control: no-cache` and `Pragma: no-cache`; `feed::EDGE_MAX_AGE_SECS = 600` is exposed for Phase 6's "a new
  release can take up to 10 minutes to appear" text; roadmap lines 534-535 are restated and Phase 6 is named owner of
  that text (FR-051, FR-090, SC-009).
- **D8, the cache is one disposable file.** `<state>/feed-cache.json` = `{"cache_version": 1, "fetched_at_unix": <u64>,
  "source_url": "<the feed URL fetched>", "body": "<the accepted document's text, verbatim>"}`, written atomically with
  the settings sequence. It is used only when `source_url` equals the current feed URL (so a `dev-feed` audition never
  leaks into a production run), and its `body` is re-parsed with the current rules on every read. Anything unreadable,
  of another `cache_version`, or rejected on re-parse is ignored (logged) and overwritten by the next accepted fetch;
  the cache is never deleted. **The fresh document is authoritative (ruled 2026-10-30, Clarifications Q7, option A):** an
  accepted fetch replaces the cache and the model even when it names a lower version for some plugin than the cached copy;
  there is no per-plugin monotonic rule and no age damping, so a rollback takes effect at once (FR-050, FR-058, SC-031).
  Likewise one check is one attempt (Clarifications Q8, option A): no automatic retry; the user's "check now" is the
  retry (FR-050, FR-059, SC-032).
- **D9, the version model is core-only.** `CoreVersion { major, minor, patch }` (`u32` each) parses only the bounded
  core grammar of C1 (`^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$`, CONTRACT.md:40-41), and its
  `Ord` is SemVer 2.0.0 §11 precedence for versions without a prerelease (major, then minor, then patch, numerically,
  https://semver.org/spec/v2.0.0.html#spec-item-11). Prereleases are never compared in v1 (Q9), so the `semver` crate is
  not needed. A version string outside the grammar (an old v1 feed's `1.2.0-beta`) is carried as text with no
  `CoreVersion`, and its comparison is `Unknown`.
- **D10, platform resolution takes `(Os, Arch)` and the running OS version as parameters.** The `min_os` comparison is
  Phase 5's (ruled 2026-10-30, Clarifications Q3, option A): a pure comparison of `platforms.<key>.min_os` against an
  injected running-OS version (the macOS product version, the Linux glibc version; Windows per FR-037), with an
  `Availability::NeedsNewerOs { min_os }` variant; Phase 6 reads the real value through `Platform` and owns that
  roadmap sentence (FR-035, FR-037, FR-038, SC-027). `Arch` is `X86_64`, `Aarch64` or `Other`; the native
  value comes from `cfg!(target_arch = …)` of the running binary (so the universal macOS manager resolves the slice it
  runs as, and an x64 manager under Windows-on-Arm emulation resolves `windows-x86_64`). The lookup table lives in
  `core::version`, not under `platform/`, so Phase 4's platform literal allow-list (`injection.rs:40-68`) is untouched.
- **D11, the injected HTTP configuration is data.** `core::http::HttpConfig { policy: UrlPolicy, connect_timeout:
  Duration, total_timeout: Duration }` joins `AppContext` (which stays `Clone + Debug + PartialEq`); a `ureq::Agent` is
  built from it per check. `HttpConfig::production()` is `HttpsOnly`, 10 s connect, 30 s total; the value is built in
  `src-tauri` setup (roadmap lines 476-479). This is Phase 4 D17's deferred "HTTP client base".
- **D12, the fixture server is `std::net` only.** `core::fixture_server` (compiled under `cfg(any(test, feature =
  "test-support"))`) binds `127.0.0.1:0`, serves a scripted route table from a thread, and records each request's
  method, path and headers. No HTTP-server crate is added. Phase 7 extends it (for `Range`).
- **D13, the live test is doubly opt-in.** It is compiled only with core's `live-network` feature and is also
  `#[ignore]`, so neither a default `cargo test` nor `--include-ignored` runs it; no workflow, npm script or gate
  enables the feature. Its URL is read from `src-tauri/src/lib.rs`'s `FEED_URL` line, so no second copy of the feed
  host exists.
- **D14, the roster and host guards read their word list from the repo.** The plugin names a guard searches for are the
  `name` fields of `plugins/*/version.json` (nine today), read at test time, so the guard itself names no plugin and a
  new plugin joins it without an edit. The guard runs over sources (a cargo test) and over the built release binary and
  `dist/` (a `manager-ci` step on every leg), case-sensitively, because the dev box's checkout path contains the
  lower-case word `iterum` (`f:/projects/iterum`) and a debug path could carry it.
- **D15, names and codes.** New modules `core::contract`, `core::feed`, `core::http`, `core::version` (flat files, each
  with a `<stem>_tests.rs`, Phase 4's convention), `core::fixture_server` (test-only). Every error's `Display` begins
  with its code (Phase 4 convention, `context.rs:24-35`). The codes are listed in plan §3, the full code table (plan SI-5).

- **D16, PAC and WPAD are honoured on all three OSes; macOS and Linux evaluate in process, Windows asks the OS (ruled 2026-10-31,
  Clarifications R-1, R-8, R-15; the user rejected "leave out" and "Windows only").** On macOS and Linux `core::pac` bundles
  `rquickjs` 0.14.0 (FR-073(d)) and runs `FindProxyForURL` itself. **On Windows the OS evaluates the PAC** (FR-092): the manager
  calls `WinHttpGetProxyForUrlEx` (ruled R-16: it always executes out of process) (tasks TA-25), the script runs in WinHTTP's auto-proxy machinery, and QuickJS is neither compiled nor linked
  there. The injected form is `HttpConfig.pac: Option<PacSource { auto_detect: bool, url: Option<String> }>` beside the static
  `HttpConfig.proxy` (which then serves as the OS static fallback); on macOS and Linux the PAC script is fetched once per
  check, always direct, and evaluated per request hop (FR-064 to FR-069). The environment's static proxy and `NO_PROXY` win
  over every OS source (FR-048). A PAC failure never fails a check: one `warn!`, then the fallback (FR-068). The ordered result
  list is honoured inside the one-attempt rule (FR-059, FR-067). The engine, like `ureq`, is confined to one file (FR-081).
  The residual risk of a C engine in the manager process is accepted for macOS and Linux, where auto-detect is off by default,
  and mitigated by the RustSec check of FR-093.

---

## Functional Requirements

### A. Contract interpreter, `core::contract` (roadmap line 533; D2)

- **FR-001** `core::contract` embeds `tools/feed-contract/versions.v2.schema.json` and `versions.v1.schema.json` with
  `include_str!` (paths relative to the source file, into the repo's `tools/feed-contract/`; the files are never copied
  into `apps/`). It compiles each once (`std::sync::OnceLock`) into an interpreter tree supporting exactly: boolean
  schemas; `type` with `object`, `string`, `integer` (any other `type` value is unsupported); `properties`,
  `additionalProperties`, `patternProperties`, `propertyNames`, `required`, `dependentRequired`, `const`, `enum`,
  `pattern`, `minimum`, `maximum`, `not`, `allOf`, `if` / `then` / `else`, and `$ref` to `#/$defs/<name>` only. The
  annotations `$schema`, `$id`, `title`, `$comment` and the `$defs` container are accepted and ignored. Any other keyword,
  an unresolvable `$ref`, or a `pattern` the `regex` crate cannot compile is `ContractError::Unsupported { pointer,
  keyword }` at compile time; test `contract::tests::embedded_schemas_compile` proves both files compile.
- **FR-002** Interpretation follows JSON Schema 2020-12 (External facts): an `integer` is any JSON number with a zero
  fractional part; `const` and `enum` compare by JSON equality, numbers mathematically; `pattern` is an unanchored
  search with `regex` 1.13.1 in its default Unicode mode (Ajv compiles patterns with the `u` flag), after
  `contract::ecma_pattern_to_rust` rewrites the two constructs whose meaning differs between ECMA-262 and `regex`
  (External facts): `\s`, outside or inside a class, becomes the explicit ECMA set (U+0009-U+000D, U+0020, U+00A0,
  U+1680, U+2000-U+200A, U+2028, U+2029, U+202F, U+205F, U+3000, U+FEFF; so U+0085 is not whitespace and U+FEFF is),
  and `.` outside a class becomes `[^\n\r\u{2028}\u{2029}]`. Any other escape whose ECMA and `regex` meanings differ
  (`\S`, `\d`, `\D`, `\w`, `\W`, `\b`, `\B`) is `ContractError::Unsupported` (none occurs in the committed schemas,
  whose patterns use explicit classes); `additionalProperties`
  applies to keys matched by neither `properties` nor `patternProperties`; `propertyNames` validates each key as a string
  instance located at the object's path; `if` applies `then` when it passes and `else` when it fails; a missing property
  is not an error unless `required` / `dependentRequired` says so.
- **FR-003** Every failing keyword yields one `Finding { code: "E_SCHEMA", instance_path, keyword, message }`, where
  `instance_path` is the RFC 6901 pointer of the instance (`""` for the document root, `/plugins/Alpha/version` and so
  on, `~` and `/` escaped as `contract.js:402-403` does), and `keyword` is the failing keyword's name, or `"false
  schema"` for a `false` schema, as `contract.js` `ajvFindings` reports (`:405-413`). All findings are collected (Ajv's
  `allErrors`); in addition a failed `then` / `else` adds a finding with keyword `if` at the same instance path, and a
  failed `propertyNames` adds one with keyword `propertyNames`, as Ajv does. Order is document order and is not part of
  the contract.
- **FR-004** After the v2 schema, the code rules of `contract.js` `feedCodeFindings` (`:421-478`) run, for each entry and
  for its `prerelease` sibling: a `signature` holding a CR is `E_SIGNATURE_TEXT`; otherwise `parse_minisig_text`
  (FR-005) failing is `E_SIGNATURE_TEXT`; under the five defined platform keys only, the trusted comment failing
  `parse_trusted_comment` (FR-006) is its `E_TC_*` code; `install` failing `check_install_values` (`:226-242`: a
  non-string, `/`, `\`, `:`, `..`, a `preset_subdir` ending in a space, a reserved stem, or a value outside its pattern)
  is `E_INSTALL_VALUE`; and `find_install_duplicates` (`:249-271`: `vst3_bundle`, `au_component`, `macos_pkg_id`
  compared after ASCII case folding, `windows_uninstall_key` exactly; an entry and its own sibling are one plugin) yields
  `E_INSTALL_DUPLICATE` naming the field and both plugin keys. Each finding's `instance_path` is the one `contract.js`
  computes (the signature's path; the install field's path; for a duplicate, the field of the plugin that comes second
  **in bytewise key order**, equal to `contract.js` for every bytewise-ordered document; the parity test compares codes
  for duplicate rows, as `cases.json` does) (plan SI-8).
- **FR-005** `contract::parse_minisig_text(text: &str) -> Result<MinisigText, ContractError>` follows `contract.js`
  `:363-384`: lines split on LF or CRLF, an optional final line end, exactly four non-empty lines with no stray CR, line 1
  starting `untrusted comment: `, line 3 starting `trusted comment: `, lines 2 and 4 canonical standard base64 (a
  `base64` 0.23.1 `STANDARD` decode whose re-encode equals the input), line 2 decoding to 74 bytes starting `0x45 0x44`
  (`ED`), line 4 to 64 bytes. It returns the untrusted comment, the 8-byte key id, the 64-byte signature, the trusted
  comment (the untrimmed rest of line 3) and the 64-byte global signature. It performs no cryptography (Phase 7).
- **FR-006** `contract::parse_trusted_comment(s: &str) -> Result<TrustedComment, ContractError>` follows C3
  (CONTRACT.md:200-217) and `contract.js:299-318` in the same order: a CR or LF, a field count other than six, or a prefix
  other than `krate:1` is `E_TC_FIELDS`; then `<Name>` (the C1 plugin-key rule with no reserved stem) `E_TC_NAME`,
  `<version>` (the bounded core with an optional SemVer prerelease, no build metadata) `E_TC_VERSION`, `<platform-key>`
  (one of the five defined keys) `E_TC_PLATFORM`, `<asset>` (`^[A-Za-z0-9][A-Za-z0-9._-]*$`, not `.` or `..`)
  `E_TC_ASSET`. `TrustedComment { name, version, platform, asset }`. The binding check is Phase 7's.
- **FR-007** `contract::validate(doc: &serde_json::Value, schema: Schema) -> Vec<Finding>`, `Schema::{V1, V2}`: `V1`
  runs only the v1 schema; `V2` runs the v2 schema, then FR-004 (as `contract.js` `validateFeed`, `:485-492`). Empty
  means valid.
- **FR-008** Codes reuse the contract's names where the client rejects the same thing (CONTRACT.md:28): `E_INPUT`,
  `E_SCHEMA`, `E_SIGNATURE_TEXT`, `E_TC_FIELDS`, `E_TC_NAME`, `E_TC_VERSION`, `E_TC_PLATFORM`, `E_TC_ASSET`,
  `E_INSTALL_VALUE`, `E_INSTALL_DUPLICATE`.

### B. Parse and the feed model, `core::feed` (roadmap lines 532-533, 547-549; D1, D3)

- **FR-020** `feed::parse(bytes: &[u8]) -> Result<Feed, FeedError>`: bytes that are not UTF-8, or not one JSON value
  (`serde_json::from_slice`), are `E_INPUT`. Then D3's dispatch on `schema_version`: absent or `1` → `validate(V1)`;
  `2` → `validate(V2)`; an integer `> 2` → `E_FEED_FUTURE_SCHEMA { found }`; anything else → `E_FEED_SCHEMA_VERSION`. A
  non-empty finding list is `E_FEED_INVALID { findings }`, whose `Display` names the count and the first finding. Only
  an empty list builds the model.
- **FR-021** The model, built only from a validated document:
  `Feed { schema_version: u8, plugins: BTreeMap<String, PluginEntry>, manager: Option<ManagerInfo> }`;
  `PluginEntry { name, version_text: String, version: Option<CoreVersion>, download_url: Option<PageUrl>, release:
  Option<StableRelease> }` (`release` is `Some` for every entry of a version-2 document and `None` for every entry of a
  v1 document; `download_url` is `None` only when the key is absent, and `PageUrl` is a checked newtype with the C1 `url`
  rule, distinct from `AssetUrl`) (plan SI-19);
  `StableRelease { tag, pub_date, release_notes_url, notes_markdown: Option<String>, platforms: BTreeMap<PlatformKey,
  PlatformAsset>, install: Option<InstallFacts> }`, `platforms` holding only the five defined keys (empty, and `install`
  `None`, for a v2 entry that has no `platforms` yet, CONTRACT.md:50);
  `PlatformAsset { url: AssetUrl, asset_name: AssetName (plan SI-18), size: u64, sha256: [u8; 32], signature: String, format:
  AssetFormat, min_os: Option<OsVersion> (FR-037; always `Some` for `darwin-*` and `linux-*`, which the schema requires), payload: Option<Payload { files: u64, sha256: [u8; 32] }> }`;
  `AssetFormat::{InnoExe, Pkg, TarGz}`; `PlatformKey::{WindowsX86_64, DarwinAarch64, DarwinX86_64, DarwinUniversal,
  LinuxX86_64}` with `as_str()` giving the C1 spelling; `InstallFacts { vst3_bundle: PlainName, au_component:
  Option<PlainName>, preset_subdir: Option<PlainName>, windows_uninstall_key: Option<PlainName>, macos_pkg_id:
  Option<PlainName> }`; `ManagerInfo { latest_json_url: String }`. `version` is `CoreVersion::parse(version_text)`
  (always `Some` for a v2 entry, whose version passed the core pattern).
- **FR-022** Unknown keys at any level never fail a parse and never reach the model (reader rule 1, CONTRACT.md:118-119):
  the model of `valid/unknown-extra-keys.json` equals the model of `valid/full-v2.json`.
- **FR-023** "Not available through the manager" (reader rule 6, CONTRACT.md:136-138): an entry with `release: None`
  (a v1 document), or whose `release.platforms` is empty (a v2 entry without `platforms`, or whose keys are all outside
  the five defined ones, which are dropped, reader rule 2), resolves to `Availability::NotThroughManager` (FR-035) on
  every OS. It stays in `Feed.plugins` with its `version_text` (and, in a v2 document, its notes).
- **FR-024** The `prerelease` sibling is absent from the model (reader rule 3, Q9): the model's assets for an entry with
  a sibling are the stable entry's own. **A contract-invalid sibling never rejects the document (Clarifications Q1, option
  B):** in `feed::parse`, after `contract::validate` and before the verdict, every finding whose `instance_path` equals
  `/plugins/<Name>/prerelease` or starts with `/plugins/<Name>/prerelease/` (`<Name>` RFC 6901-escaped as FR-003 does) is
  dropped and logged with one `warn!` each (target `krate_manager_core::feed`, naming the finding's code, path and keyword).
  An `E_INSTALL_DUPLICATE` is dropped iff at least one of its two sides is a `prerelease` install, whatever the key
  order (plan PD-9) (plan SI-8). Every other finding still rejects the whole document (D1, `E_FEED_INVALID`):
  the prefix is the only exception, so a sibling finding reported under any other path (the plugin entry's own path, the
  document root) still rejects. The filter lives in `feed::parse` only; `contract::validate` (FR-007) is not filtered. A
  sibling that was dropped is still absent from the model. SC-010 tests the seven `invalid/sibling-*.json` rows.
- **FR-025** `PlatformAsset.asset_name` is the last `/`-separated segment of the URL's path, not percent-decoded
  (reader rule 4, CONTRACT.md:125-126), taken from `url::Url::path_segments()` (not percent-decoded); it must pass
  plan PD-21's rule (`^[A-Za-z0-9][A-Za-z0-9._-]*$`, not `.` or `..`, not a reserved stem), else the asset is dropped
  with `E_MODEL_ASSET_NAME` (R-7, FR-029(c)) (plan SI-18).
- **FR-026** Feed strings that later phases join to filesystem roots are typed so they cannot be built unchecked
  (reader rule 8, CONTRACT.md:141-142; Cross-Cutting "Feed strings never become filesystem paths unvalidated", roadmap
  line 982): `PlainName` has no public constructor and is made only by the model builder from a value that passed
  `check_install_values`; `AssetUrl` likewise only from a URL that passed the C1 `url` rule (`https`, no whitespace, no
  `/releases/latest/`). Both expose `as_str()`. `AssetName` (FR-025) and `PageUrl` (FR-021, a release-page link
  checked by the C1 `url` rule) join `PlainName` and `AssetUrl`, with no public constructor and `as_str()`; only
  `PlatformAsset.url` is an `AssetUrl` (plan SI-18, SI-19). `sha256` and `payload.sha256` are decoded to bytes.
- **FR-027** Roster (ROSTER, roadmap lines 33, 1001-1003): `Feed.plugins` holds exactly the document's `plugins` keys
  whose entry the model keeps (every v2 key; a v1 entry whose key fails the C1 `pluginName` rule or whose `download_url`
  fails the C1 `url` rule is dropped with one `warn!` `E_MODEL_V1_ENTRY`, FR-029(c); test
  `feed::tests::v1_entries_are_sanitized`) (plan SI-19), in bytewise key order; no code in `apps/krate-manager` (outside test fixtures and test files) names a plugin.
- **FR-028** `Feed.manager` carries `manager.latest_json_url` when present (Phase 11's consumer); nothing in Phase 5
  fetches it.
- **FR-029** **A contract-valid document the model cannot fully hold never changes the verdict (ruled 2026-10-31,
  Clarifications R-7).** When the contract accepts a value the model cannot represent, the model drops **only the affected
  platform asset** (or, in a v1 document, **only the affected entry**) with exactly one `warn!` (target
  `krate_manager_core::feed`) naming the log code and the RFC 6901 path of the offending value; the document is still
  accepted, and the verdict is `contract::validate`'s alone (FR-007, FR-020), equal to the Node validator's and `dry_run`'s.
  The cases: (a) a JSON integer the model needs as `u64` (`payload.files`, `size`) that `feed::json_u64` refuses
  (`as_u64()`, else a number with a zero fractional part in `0..=2^53`, so `"size": 2.0` is accepted as 2 and `"files":
  1e300` is not) → `E_MODEL_RANGE`; (b) a `min_os` with a component that overflows `u32` (FR-037), e.g. `11.4294967296` →
  `E_MODEL_RANGE`; (c) any further value a typed model constructor refuses although the contract accepted it (an asset name
  derived from the URL that is not a safe file name, `E_MODEL_ASSET_NAME`; a v1 entry's plugin key or `download_url`
  outside the C1 rules, `E_MODEL_V1_ENTRY`). These are log codes only: never a `Finding`, never `E_FEED_INVALID`, never a
  `FeedError`. A dropped platform key is treated as absent (reader rule 2), so a dropped `darwin-aarch64` falls to
  `darwin-universal` when the entry has one (FR-033 to FR-035), and an entry all of whose assets are dropped is
  `NotThroughManager` (FR-023). Every other asset, entry and plugin of the document is kept; the cache stores the whole
  accepted body verbatim (FR-052); the dropped asset is simply not offered. SC-037.

### C. Version model and platform resolution, `core::version` (roadmap lines 536-537; D9, D10)

- **FR-030** `CoreVersion::parse(&str) -> Option<CoreVersion>` accepts exactly the bounded core grammar (D9); `Display`
  writes `major.minor.patch` and round-trips; `Ord` is (major, minor, patch) numerically.
- **FR-031** `version::reduce_class_version(&str) -> Option<CoreVersion>` (roadmap line 537; `check-bundle.js:50-54`):
  `X.Y.Z` → that version; `X.Y.Z.N` with `N` one to nine ASCII digits → `X.Y.Z`; anything else (fewer or more parts, a
  non-digit fourth part, a prerelease suffix, a part outside the core grammar, surrounding whitespace) → `None`. It
  makes no claim about which moduleinfo field to read (Phase 6, roadmap lines 560-565).
- **FR-032** `version::compare(installed: Installed, available: Option<CoreVersion>) -> UpdateState` with
  `Installed::{No, VersionUnknown, Version(CoreVersion)}` (Phase 6 fills it from detection) and
  `UpdateState::{NotInstalled, UpToDate, UpdateAvailable, InstalledNewer, Unknown}`: `No` → `NotInstalled`;
  `VersionUnknown`, or an `available` of `None` with an install present → `Unknown`; otherwise by `Ord`: equal →
  `UpToDate`, available greater → `UpdateAvailable`, installed greater → `InstalledNewer`. An older available version
  is never an update (the downgrade edge case).
- **FR-033** `version::Arch::{X86_64, Aarch64, Other}` and `Arch::native()` from `cfg!(target_arch = "x86_64")` /
  `cfg!(target_arch = "aarch64")`.
- **FR-034** `version::platform_candidates(os: Os, arch: Arch) -> &'static [PlatformKey]` (reader rule 2,
  CONTRACT.md:120-122): `(Windows, X86_64)` → `[WindowsX86_64]`; `(Macos, Aarch64)` → `[DarwinAarch64, DarwinUniversal]`;
  `(Macos, X86_64)` → `[DarwinX86_64, DarwinUniversal]`; `(Linux, X86_64)` → `[LinuxX86_64]`; every other pair → `[]`.
- **FR-035** `version::resolve(entry: &PluginEntry, os: Os, arch: Arch, running: Option<&OsVersion>) -> Availability`
  with `Availability::{Available { key: PlatformKey, asset: &PlatformAsset }, NotThroughManager, NotForThisMachine,
  NeedsNewerOs { min_os: OsVersion }}` (Clarifications Q3): `NotThroughManager` per FR-023; otherwise the first
  candidate present in `platforms`; none present → `NotForThisMachine` (an Intel Mac and an arm64-only plugin: "not
  available for this Mac", roadmap lines 818-821). For the chosen asset the `min_os` rule of FR-037 then applies: an
  asset whose `min_os` is greater than `running` is `NeedsNewerOs { min_os }` (the asset's own value, parsed); otherwise
  `Available`. The first present candidate is final: a native asset that needs a newer OS is not replaced by the
  `darwin-universal` one. `running` is a parameter, never read inside `core::version::resolve`.
- **FR-036** `version::resolve_native(entry, running: Option<&OsVersion>)` = `resolve(entry, NativePlatform.os(),
  Arch::native(), running)`; it is the only call site of the two native values in `core::version`. It reads no OS
  version: Phase 6 reads the real running-OS version through `Platform` and passes it in.
- **FR-037** `version::OsVersion` parses exactly the C1 `min_os` grammar `^[0-9]+(\.[0-9]+){1,3}$` (CONTRACT.md:66) into
  two to four numeric components (each a `u32`; a component that overflows is a parse failure); `Display` writes the
  components as parsed; `Ord` compares component by component numerically with missing trailing components as 0
  (`11.0` equals `11.0.0`; `2.31` is less than `2.34`; `10.9` is less than `10.10`). Meaning per OS: macOS the product
  version (`sw_vers -productVersion`), Linux the glibc version, Windows the Windows version as major.minor[.build] (the
  contract makes Windows `min_os` optional and defines no other semantics, CONTRACT.md:163-164). `min_os` of `None`
  (Windows only) never gates. `running` of `None` (Phase 6 could not read it) never gates: `NeedsNewerOs` is returned
  only when the running version is known and lower, because an unreadable version is not evidence of an old OS.
- **FR-038** `Availability::NeedsNewerOs { min_os }` is distinct from `NotForThisMachine` and carries the required
  version for Phase 6's "needs macOS 12 or newer" text; `UpdateState` (FR-032) is independent of it. It is a pure
  function of `(entry, os, arch, running)`: no environment, no file, no process.

### D. HTTP transport, `core::http` (roadmap line 532; D4-D6, D11)

- **FR-040** `krate-manager-core` depends on `ureq` 3.5.0 with its `platform-verifier` feature enabled (D4,
  Clarifications Q4) **and its `socks-proxy` feature, with `ureq`'s default features kept** (ruled 2026-10-31,
  Clarifications R-5: `features = ["platform-verifier", "socks-proxy"]`, `default-features` not disabled, so
  `rustls-webpki-roots` stays compiled in, unused; the compiled-in Mozilla roots are in no case the trust source, FR-043,
  SC-026, SC-038) and on `rustls` (`"0.23.46"`,
  `default-features = false`, only to name `rustls::Error` in FR-044's downcast). `ureq` and `rustls` are used only in
  `crates/core/src/http.rs` (FR-081). `flate2` 1.1.10 (already in the lock, External facts) joins core's
  `[dev-dependencies]` only, so tests can build `Reply::Gzip` bodies (FR-070). **The plan verifies and records** which
  `reqwest` and `rustls` features `tauri-plugin-updater` 2.14.1 (Phase 11) uses (crate source or `cargo tree -e features`
  of a scratch dependency, nothing committed) and whether its TLS verifier agrees with `ureq`'s `platform-verifier`
  stack (same verifier family, same provider or not); the record goes in the plan and in compliance (SC-017).
- **FR-041** `http::UrlPolicy::{HttpsOnly, LoopbackOnly}` and `http::HttpConfig { policy, connect_timeout, total_timeout,
  proxy: Option<ProxyConfig>, pac: Option<PacSource> }` (`Clone, Debug, PartialEq`); `HttpConfig::production()` = `HttpsOnly`,
  `CONNECT_TIMEOUT` 10 s, `TOTAL_TIMEOUT` 30 s, `proxy: None`; `HttpConfig::loopback(connect_timeout, total_timeout)` =
  `LoopbackOnly`, `proxy: None`; `HttpConfig::with_proxy(self, Option<ProxyConfig>) -> HttpConfig` is the only way a
  static proxy enters, and `HttpConfig::with_pac(self, Option<PacSource>) -> HttpConfig` is the only way a PAC source
  enters (FR-046, FR-064); `pac` is `None` in `production()` and `loopback()`. `HttpConfig` also carries `pac_host:
  Option<PacHostHandle>` (`None` in `production()` and `loopback()`), and `HttpConfig::with_pac_host(self,
  Option<PacHostHandle>) -> HttpConfig` is the only way a PAC host enters (plan SI-26). `ProxyConfig` and `PacSource` are plain data
  (FR-046, FR-064), not `ureq` types, so `AppContext` stays free of `ureq`.
- **FR-042** `http::check_url(policy, &url::Url) -> Result<(), HttpError>`: `HttpsOnly` accepts only scheme `https`;
  `LoopbackOnly` accepts scheme `http` or `https` with host the IP literal `127.0.0.1` or `[::1]` only. Everything else
  (another scheme, `http` to any other host in `HttpsOnly`, `localhost`, a DNS name in `LoopbackOnly`, a URL with
  userinfo) is `E_FEED_URL` before any socket is opened. `http::check_proxy(policy, &ProxyConfig)` applies the same
  rule to the proxy itself: under `LoopbackOnly` the proxy host must be the IP literal `127.0.0.1` or `[::1]`
  (otherwise `E_FEED_URL`, before any socket is opened, so a test can never send a request to a remote proxy); under
  `HttpsOnly` any proxy host is accepted, but only the schemes FR-046 lists (`http`, `https`, `socks4`, `socks5`). A PAC URL
  is checked by `http::check_pac_url` (FR-065) and the proxies a PAC script names by the same rule (FR-067).
- **FR-043** `http::get(config: &HttpConfig, url: &str, headers: &[(&str, &str)], max_bytes: u64) -> Result<Vec<u8>,
  HttpError>` builds a `ureq` agent with `https_only(policy == HttpsOnly)`, `max_redirects(0)`, `http_status_as_error(false)`,
  `proxy(config.proxy)` (D6, FR-046: always set explicitly, so `ureq`'s own environment read never takes effect and
  `None` means no proxy; when `config.pac` is `Some`, the proxy is chosen per request hop by FR-067 and the agent is built
  per hop), the TLS root certificates `RootCerts::PlatformVerifier` (D4: the OS trust store, set
  explicitly, never `ureq`'s default `WebPki` bundle), `timeout_connect(connect_timeout)`, `timeout_global(total_timeout)`
  and `user_agent("krate-manager/<core
  version>")`, sends `Accept: application/json` plus `headers`, and follows redirects itself. The agent's configuration is
  built by the pure `pub(crate)` function `http::agent_config(&HttpConfig, Option<ureq::Proxy>, AgentPurpose) ->
  ureq::config::Config` (`AgentPurpose::{Feed, PacFetch}`: `Feed` sets `https_only(policy == HttpsOnly)`, `PacFetch`
  sets `https_only(false)` and leaves the scheme rule to `check_pac_url`, plan PD-28) (plan SI-20) (it and `http_tests.rs`
  are the only places that name the type, FR-081), so
  SC-026 and SC-030 can inspect it without a socket. Every hop's decision is the
  pure function `http::next_hop(policy: UrlPolicy, current: &url::Url, status: u16, location: Option<&str>) ->
  Result<Hop, HttpError>`, `Hop::{Done, Follow(url::Url)}`, which `get` calls after every response: a status other than
  `301`, `302`, `303`, `307`, `308` is `Done`; one of those with a `Location` resolved against `current`
  (`url::Url::join`) that passes FR-042 under `policy` is `Follow`; a `Location` that fails FR-042 (an `https` → `http`
  hop under `HttpsOnly`, a non-loopback host under `LoopbackOnly`) is `E_FEED_URL`; a missing or unparsable `Location`
  is `E_FEED_REDIRECT`. More than `MAX_REDIRECTS = 5` hops is `E_FEED_REDIRECT`. The final status must be `200`; any
  other is `E_FEED_HTTP_STATUS { status }` (including `304`, which the client never invites). **The body cap counts
  decoded bytes and admits exactly `max_bytes`:** the body is read through `Body::as_reader()` (which decodes gzip and
  sets no `ureq` limit, External facts) wrapped in `std::io::Read::take(max_bytes + 1)`, and a result longer than
  `max_bytes` is `E_FEED_TOO_LARGE`. Neither `with_config().limit(max_bytes)` (which fails a body of exactly the limit,
  `limit.rs:22-25`) nor any `ureq` limit is the bound, because a `ureq` limit counts compressed wire bytes (`mod.rs:783`)
  and a gzip body under the cap on the wire could expand without bound.
- **FR-044** `ureq` errors map to codes: no response received (`HostNotFound`, `ConnectionFailed`, `Io` before a status
  line that is not a TLS error, `Protocol`) → `E_FEED_OFFLINE`; `Timeout` → `E_FEED_TIMEOUT`; `Tls`, `Rustls`,
  `TlsRequired`, and **an `Error::Io` whose inner error is a `rustls::Error`** (`io::Error::get_ref()` then
  `downcast_ref::<rustls::Error>()`; this is how a failed handshake arrives, External facts: `rustls.rs:78`,
  `error.rs:218-227`, rustls `conn.rs:697-701`) → `E_FEED_TLS`; `RequireHttpsOnly` → `E_FEED_URL`; an `Io`,
  `Decompress` or `BodyStalled` while reading a `200` body → `E_FEED_BODY`; any other → `E_FEED_OFFLINE` with the `ureq`
  message kept. An io error from the body reader that wraps a `ureq` error is first turned back into it with
  `ureq::Error::from(io::Error)` (`error.rs:218-227`), so a `Timeout` mid-body is `E_FEED_TIMEOUT`. The downcast needs
  the `rustls::Error` type, which `ureq` 3.5.0 does not re-export (its `lib.rs:583-637` `pub use` list has no `rustls`),
  so core depends on `rustls` directly with `default-features = false` and no features, the same requirement `ureq`
  declares (`version = "0.23.46"`, `default-features = false`, ureq `Cargo.toml:206-214`), so Cargo resolves one
  `rustls` and the downcast type is the one `ureq` wraps; `rustls` is named only in `http.rs` (FR-040, FR-080, FR-081).
  The plan confirms the mapping against `ureq` 3.5.0 behaviour on the fixture server (SC-012), and confirms that a
  certificate the OS verifier rejects (D4) reaches the same `rustls::Error` downcast (the `platform-verifier` path
  reports through `rustls::Error::InvalidCertificate`); if it does not, the plan states the extra mapping. A proxy that
  refuses or fails the `CONNECT` (`ureq` proxy errors) is `E_FEED_OFFLINE` with the `ureq` message kept: the cached copy
  is used like any other no-response failure (FR-055). A PAC failure (fetch, script or result) is never a `ureq` outcome
  and never an error code of the check; FR-068 handles it.
- **FR-045** No non-test code outside `http.rs` opens a socket or names a URL scheme policy (`fixture_server.rs` is
  test-only, `cfg(any(test, feature = "test-support"))`, D12); `ureq_is_confined_to_http_rs` (FR-081) enforces it. Phase 7
  extends `core::http` for streamed downloads rather than adding a second client.
- **FR-046** `http::ProxyConfig { url: String, no_proxy: Vec<String> }` (plan SI-12) (`Clone, Debug, PartialEq`; `url`
  is the proxy URL with scheme `http`, `https`, `socks4` or `socks5`, the last two through the `socks-proxy` feature
  (FR-040), optional `user:pass@` userinfo kept out of `Debug`/log output; `no_proxy` is the bypass list
  as the sources give it, one entry per host, domain suffix, IP or CIDR) is the injected form of Clarifications Q5.
  `http::get` converts it to `ureq::Proxy` inside `http.rs`; the conversion happens once, before any socket opens, and a
  proxy `ureq` refuses is `E_FEED_URL` (redacted), never a direct request (plan SI-20; SC-028 `loopback_proxy_only` gains
  the `http://%FF@127.0.0.1:<port>` row). It applies the bypass list to the request's host and every
  redirect hop's host: a host on the list is fetched direct. `HttpConfig.proxy` is `None` in `production()`, in
  `loopback()` and in every test that does not test the proxy (FR-087 extends to the proxy, SC-030). Credentials in a
  proxy URL are redacted (`***`) in every `Debug`, `Display` and log line (FR-056).
- **FR-048** Proxy resolution is the pure function `http::resolve_proxy(env: &ProxyEnv, system: &SystemProxy, target:
  &str) -> ProxyResolution` (plan SI-6, SI-27), `ProxyResolution { proxy: Option<ProxyConfig>, pac: Option<PacSource> }` (Clarifications
  Q5, option C; R-1). `ProxyEnv { https_proxy, all_proxy, no_proxy: Option<String> }` is read from `HTTPS_PROXY` /
  `ALL_PROXY` / `NO_PROXY` (each in upper and lower case) **only in `src-tauri` setup** (FR-063), never in core (Phase 4
  D9); `SystemProxy` is the per-OS reader's output (FR-049). The rules are decided, not left to the plan: **(a) the
  environment wins over the OS settings**: a static proxy from the environment (`HTTPS_PROXY`, else `ALL_PROXY`; the
  upper/lower-case merge and the order between the two are tested by SC-028) is the result with `pac: None`, and the OS
  settings, PAC and WPAD included, are not consulted; **(b) PAC and WPAD are honoured** (R-1): with no environment proxy,
  the OS settings are used in the order of FR-069, and a `pac_url` or an `auto_detect` flag in `SystemProxy` becomes
  `ProxyResolution.pac` (never silently ignored, never reduced to a `warn!`-and-direct); **(c) `NO_PROXY`** from the
  environment, when it matches the target host (exact host, domain suffix, IP, CIDR, `*`), yields the empty resolution
  (direct, `pac: None`) from every source, PAC included, and no PAC script is fetched; the OS bypass list applies to the OS
  static proxy only, and the PAC script decides for itself. `resolve_proxy` is tested over a table (SC-028), and
  `ProxyEnv::from_vars` (the upper/lower-case merge of the raw variables) by `http::tests::proxy_env_from_vars_cases`
  (plan SI-21).
- **FR-049** The OS proxy settings are read into `SystemProxy { proxy: Option<ProxyConfig>, pac_url: Option<String>,
  auto_detect: bool }`, with three implementations: the pure parsers live in `core::sysproxy` and the real readers in
  `src-tauri/src/os_proxy.rs`, reached through `SystemProxyFromOs for NativePlatform`; `trait Platform` is unchanged
  (plan SI-1, PD-1). The Windows parser input is `WinIeProxyConfig { auto_detect: bool, auto_config_url, proxy,
  proxy_bypass: Option<String> }`, the WinHTTP structure (no undocumented `AutoDetect` registry value is read) (plan SI-9). **Windows** (R-2) calls
  `WinHttpGetIEProxyConfigForCurrentUser` (the current user's Internet settings as WinHTTP reports them: `fAutoDetect`,
  `lpszAutoConfigUrl`, `lpszProxy` including the `http=…;https=…` form, `lpszProxyBypass` including `<local>`), converts
  the wide strings and frees every string it returns with `GlobalFree`. **macOS** (R-3) reads the SystemConfiguration proxy
  dictionary (`HTTPSEnable` / `HTTPSProxy` / `HTTPSPort`, `ExceptionsList`, `ProxyAutoConfigEnable` /
  `ProxyAutoConfigURLString`, `ProxyAutoDiscoveryEnable`) through `system-configuration` 0.8.0, converted by
  `sysproxy_cf::from_cf_proxies` (FR-075). **Linux** (R-4, FR-074) reads GNOME (`org.gnome.system.proxy`: `mode`, `https`,
  `ignore-hosts`, `autoconfig-url`) or KDE (`kioslaverc`: `ProxyType`, `httpsProxy`, `NoProxyFor`, `Proxy Config Script`).
  The PAC and WPAD mapping (R-1): Windows `fAutoDetect` → `auto_detect`, `lpszAutoConfigUrl` → `pac_url`; macOS PAC
  enabled with a non-empty URL string → `pac_url`, `ProxyAutoDiscoveryEnable` 1 → `auto_detect`; GNOME `mode` `auto` with
  a non-empty `autoconfig-url` → `pac_url`, with an empty one → `auto_detect`; KDE `ProxyType` 2 → `pac_url` from `Proxy
  Config Script`, 3 → `auto_detect`. A static proxy found beside a PAC source is kept in `proxy` (FR-069). Each
  implementation is split into a thin real reader and a **pure parser over injected raw inputs** (the WinHTTP strings and
  flag, the dictionary pairs, the `gsettings` / `kioslaverc` text); every parser is tested with injected inputs on every
  host (SC-029). Each reader is further split into one OS call (in `os_proxy.rs`) and core seams with tests:
  `sysproxy::tests::win_ie_strings_are_read_and_freed_once` (every leg), `sysproxy_cf::tests::from_cf_proxies_cases`
  (macOS, R-6; SC-040), `sysproxy::tests::bounded_run_abandons_and_reaps` (macOS, Linux),
  `sysproxy::tests::linux_desktop_choice_cases` (SC-039), `sysproxy::tests::kioslaverc_path_cases` and
  `sysproxy::tests::kde_file_read_cases` (every leg) (plan SI-21). A read failure is `SystemProxy::default()` (no proxy) with one `warn!`, never an error that blocks the
  feed check. The real readers read and never write the registry, SystemConfiguration or gsettings; no test calls a real
  reader, and no test touches the real registry, preferences or dconf (FR-087). The crates the readers use are ruled in
  FR-073.
- **FR-064** **PAC and WPAD are in v1 on all three OSes (ruled 2026-10-31, Clarifications R-1); where the script runs is ruled by
  R-8 and R-15.** On **macOS and Linux** the manager evaluates PAC scripts in process: new module `core::pac` (flat file
  `pac.rs` with `pac_tests.rs`) bundles `rquickjs` 0.14.0 (QuickJS-ng, FR-073(d)). On **Windows the OS evaluates the PAC**
  (FR-092) and the engine is not compiled or linked. `http::PacSource { auto_detect: bool, url: Option<String> }`
  (`Clone, Debug, PartialEq`, plain data like `ProxyConfig`) is what `HttpConfig.pac` carries: `url` is a PAC URL from the OS
  settings (Windows `AutoConfigURL`, macOS `ProxyAutoConfigURLString`, GNOME `autoconfig-url`, KDE `Proxy Config Script`),
  `auto_detect` the WPAD flag (FR-049). `pac::evaluate(script: &str, url: &url::Url, host: PacHostHandle) ->
  Result<Vec<ProxyChoice>, PacError>` (macOS and Linux; a handle, because `rquickjs`'s `Function::new` needs captures
  that outlive `'js`, an `Arc`) (plan SI-25) runs `FindProxyForURL(url, host)` and parses the answer (FR-067); the
  URL passed is the request hop's URL, with an `https` hop's path and query replaced by `/`, and `host` is the hop's host.
  **The script's world (R-14) is exactly** the standard PAC helper functions `isPlainHostName`, `dnsDomainIs`,
  `localHostOrDomainIs`, `isResolvable`, `isInNet`, `dnsResolve`, `myIpAddress`, `dnsDomainLevels`, `shExpMatch`,
  `weekdayRange`, `dateRange`, `timeRange` plus `alert`, a no-op, and nothing else of the machine: **no `Date`** (it would read
  the real clock outside `PacHost`), **none of the IPv6 `*Ex` variants** (`isResolvableEx`, `dnsResolveEx`, `isInNetEx`,
  `myIpAddressEx`, `sortIpAddressList`, `FindProxyForURLEx`), **no `convert_addr`**, and no file, network, process or
  environment access. A script that calls a name outside that set throws, which is `E_PAC_SCRIPT` and the fallback (FR-068).
  The helpers follow the MDN PAC page's definitions, including: **a reversed range matches only its two endpoints**
  (`weekdayRange("FRI", "MON")` is true on Friday and Monday only, `timeRange(23, 0)` true in hour 23 and hour 0 only, a
  reversed `dateRange` true on its two endpoint dates only; it does not wrap around), and `dateRange(x, "GMT")` with one number
  takes a value above 31 as a year. The pure helper functions, the answer parser (FR-067) and the clock arithmetic (FR-095)
  are compiled on every OS and tested on every host; only the engine glue is `cfg(not(windows))`. What touches the machine goes
  through the injected `PacHost` trait (name resolution, `my_ip`, `now`, `wpad_candidates`, and on Windows the OS evaluation of
  FR-092; the plan fixes the signatures: `PacHost: Send + Sync` with defaulted methods `resolve(&self, &str, Duration)
  -> Option<IpAddr>`, `my_ip(&self) -> IpAddr`, `now(&self) -> PacClock`, `wpad_candidates(&self, Duration) ->
  Vec<String>`, `os_evaluates(&self) -> bool`, `os_find_proxy(&self, &PacSource, &str, Duration) ->
  Result<Vec<OsProxyEntry>, PacError>` (plan SI-25)), so tests inject answers and no test resolves a real name or reads a real clock
  (FR-087). SC-033.
- **FR-065** On macOS and Linux (on Windows WinHTTP fetches the script, FR-092) the PAC script is fetched by `http.rs` (FR-045, FR-081) with FR-043's agent configuration except that the fetch
  is **always direct** (never through `config.proxy` or another PAC answer, so a PAC cannot be needed to fetch itself),
  and except `https_only`, which a PAC fetch leaves off because FR-065 admits `http` (plan SI-20).
  `http::check_pac_url(policy, &url::Url) -> Result<(), HttpError>`: under `HttpsOnly` scheme `http` or `https` (PAC files
  are served over plain `http` in practice; the script decides routing only, Safety), under `LoopbackOnly` scheme `http` or
  `https` with host the IP literal `127.0.0.1` or `[::1]`; any other scheme (a `file:` PAC URL included), and any URL with
  userinfo (so no PAC credential is ever held or logged), is refused before a socket is opened. Redirects and the status
  rule are FR-043's, with this scheme rule applied per hop. The body is capped at `MAX_PAC_BYTES` = 1 MiB of decoded bytes
  by FR-043's `take(MAX_PAC_BYTES + 1)` rule (exactly the cap is accepted). One fetch per check, no retry (FR-059). The fetch
  is inside the check's total timeout (FR-067): discovery, fetch and compile share `PAC_PHASE_BUDGET` = 10 s, inside the
  chain deadline (plan SI-28). A failure is a PAC failure (FR-068). SC-034, SC-036.
- **FR-066** WPAD discovery (R-9). `PacSource.auto_detect` discovers the PAC URL. **Windows asks the OS:** discovery is WinHTTP's
  own (the DHCP-then-DNS rules of `WinHttpDetectAutoProxyConfigUrl`, reached through `WinHttpGetProxyForUrlEx` with auto-detect,
  FR-092, with no separate detection step) (tasks TA-25); the manager derives no candidate on Windows. **macOS and Linux:** the only candidate is the constant
  `pac::WPAD_DNS_URL = "http://wpad/wpad.dat"`, a single-label name the OS resolver completes with its own DNS search list
  (Chromium's rule); there is **no DNS devolution, no DHCP lookup** and no derivation from the local host name (the earlier
  `pac::wpad_candidate_urls` is withdrawn). `PacHost::wpad_candidates` returns exactly `[WPAD_DNS_URL]` for the real host
  there; tests inject loopback candidates. The candidates are fetched direct in order under FR-065's rules and the first that
  answers `200` with a body that compiles as a PAC script is used; when `auto_detect` finds none and `url` is `Some`, `url` is
  fetched next; when nothing yields a script it is a PAC failure (FR-068). Discovery is inside the check's total timeout. The
  real discovery is read-only; no test calls it (FR-087); the constant, the candidate rule and the walk (candidates injected as
  loopback URLs) are tested (SC-035).
- **FR-067** The answer of `FindProxyForURL` is a `;`-separated list **honoured in order** (R-1, R-10): entries are trimmed and
  their keywords case-insensitive: `DIRECT` connects directly; `PROXY host:port` and `HTTP host:port` use an `http` proxy;
  `HTTPS host:port` an `https` proxy; `SOCKS host:port` and `SOCKS4 host:port` a `socks4` proxy; `SOCKS5 host:port` a
  `socks5` proxy (a missing port defaults to 80 for `PROXY` / `HTTP`, 443 for `HTTPS`, 1080 for `SOCKS*`). An empty entry is
  skipped silently; an unknown keyword or an entry with no host is skipped with one `warn!` `E_PAC_RESULT`; an answer that
  is not a string (`null` included), or that leaves no usable entry, is a PAC failure (FR-068). An answer longer than
  `MAX_PAC_ANSWER_BYTES` (64 KiB) is `E_PAC_RESULT`; only the first `MAX_PAC_ENTRIES` (16) usable entries are walked, the
  rest dropped with one `warn!` (plan SI-29). `http::get` evaluates the PAC for **each
  request hop** (the first request and every redirect hop, because the answer depends on the URL; the script itself is
  fetched once per check) and walks that hop's list: the first entry is tried; an entry that yields **no response** (the
  `E_FEED_OFFLINE` class of FR-044: connection refused or unreachable, a proxy refusing or failing the `CONNECT`, a
  connect-phase timeout while total time remains) advances to the next entry; any response, any TLS failure, a status, a
  body failure or the total timeout ends the walk with that outcome. When every entry fails, the result is the last entry's
  `E_FEED_OFFLINE` with its message kept, and `check` falls back to the cache (FR-055). **Inside the one-attempt rule
  (FR-059):** the walk is part of the single request chain, no entry is tried twice and no second walk follows, and the
  chain's `total_timeout` bounds the PAC fetch, the evaluation and the walk together. Under `LoopbackOnly` every proxy a
  PAC answer names must be `127.0.0.1` or `[::1]` (otherwise `E_FEED_URL` before any socket opens, FR-042). `no_proxy`
  (FR-046) does not apply to PAC answers. On Windows the OS's answer (FR-092) is mapped to the same list and walked by this same rule: each entry at most once, advancing only on no-response, bounded by the chain deadline. SC-034, SC-041.
- **FR-068** A PAC failure never fails a check by itself and never panics. The failures: a PAC fetch failure of any
  FR-043 class or an out-of-policy URL, and a WPAD discovery that finds no script (`E_PAC_FETCH`); a script that does not
  compile, throws, defines no `FindProxyForURL`, or exceeds its bound (`E_PAC_SCRIPT`); an unusable answer (`E_PAC_RESULT`,
  FR-067). Each logs exactly one `warn!` (target `krate_manager_core::http`) naming the code, the PAC URL with its query
  removed, and the cause, and the hop then takes the fallback of FR-069: the OS static proxy when `HttpConfig.proxy` is
  `Some`, else a direct connection. These are log codes, never a `FeedError` or a `Finding`. Bounds: `MAX_PAC_BYTES`
  (FR-065); one `FindProxyForURL` call runs at most `PAC_EVAL_BUDGET` = 2 s of wall clock, enforced by `rquickjs`'s interrupt handler (`Runtime::set_interrupt_handler`, which also stops a catastrophic regular expression) so that no thread outlives the check and no thread or
  process is killed, and within the remaining total timeout; a script that loops forever is `E_PAC_SCRIPT` after the
  budget; the engine's memory limit (`Runtime::set_memory_limit`; the `rust-alloc` feature, which turns it into a no-op, is barred, FR-073(d)) is set and a script that exhausts it is `E_PAC_SCRIPT` with the
  process alive: `PAC_MEMORY_LIMIT` = 32 MiB (`Runtime::set_memory_limit`), `PAC_STACK_LIMIT` = 256 KiB
  (`Runtime::set_max_stack_size`), and a stack overflow is `E_PAC_SCRIPT` too (plan SI-28). The engine bounds apply on macOS and Linux; on Windows the OS call's failure (no script found, any WinHTTP error, a timeout) or an unusable answer is the same failure class with the same single `warn!` and fallback (FR-092). SC-036, SC-041.
- **FR-069** Source order (R-1; environment variables still win over OS settings). (1) A static proxy from the environment
  (FR-048(a)) is used and no PAC is consulted. (2) Otherwise the OS settings, in the order WinHTTP itself uses: `auto_detect`
  (WPAD, FR-066) first, then `pac_url`, then the OS static `proxy`, then direct; an absent stage is skipped and a failed stage
  (FR-068) falls to the next. `ProxyResolution` therefore carries `pac` and the OS static `proxy` together when both exist:
  while the PAC works its answer is final (including `DIRECT`, even with a static proxy configured), and when it fails the
  static proxy, else a direct connection, is used. (3) `NO_PROXY` matching the target host ends every source (FR-048(c)).
  SC-028, SC-035.
- **FR-092** **On Windows the OS evaluates the PAC (ruled 2026-10-31, Clarifications R-15); the in-process engine is neither
  compiled nor linked there.** With `HttpConfig.pac` `Some` the real Windows PAC host calls `WinHttpGetProxyForUrlEx` (on a
  resolver from `WinHttpCreateProxyResolver` over a `WINHTTP_FLAG_ASYNC` session; completion through
  `WinHttpSetStatusCallback`, the result through `WinHttpGetProxyResult` / `WinHttpFreeProxyResult`, cancelled by closing the
  resolver; ruled R-16) (plan SI-38) for each request hop (auto-detect when `PacSource.auto_detect`, and/or the configured `PacSource.url` as the auto-config URL, per the
  FR-049 reader's result), so the script is fetched and run in Windows' WinHTTP auto-proxy machinery and not in the manager
  process, and WPAD discovery is WinHTTP's (R-9, FR-066). The call is made from `src-tauri`, in the one file that holds the
  real Windows reader (FR-073(b)), and reaches core as an `PacHost` method (FR-064), so core names no Windows type; it runs
  through the bounded runner, waits at most the remaining chain deadline, and an unanswered call is abandoned and never killed.
  Its answer, the ordered `WINHTTP_PROXY_RESULT_ENTRY` list, is mapped by a pure parser, tested over injected inputs on every
  host, to the ordered `ProxyChoice` list that FR-067 walks (an entry whose `fProxy` is false gives `DIRECT`) (plan SI-38). Any failure of the
  call or an unusable answer is a PAC failure (FR-068, one `warn!`) with FR-069's fallback. WinHTTP, not the manager, fetches
  and runs the script here, so FR-065's URL rule and size cap and the engine bounds of FR-068 do not apply on Windows; the
  consequence is recorded in Safety. The `windows-sys` features stay exactly the two of FR-073(b): `GetTimeZoneInformation` and
  `Win32_System_Time` are not used, because the manager has no PAC path of its own on Windows. The call never offers the
  user's domain credentials: `WINHTTP_AUTOPROXY_OPTIONS.fAutoLogonIfChallenged` is `FALSE` on every call, so a PAC server that
  challenges with NTLM / Negotiate fails that call (`E_PAC_FETCH`, one `warn!`, FR-068 / FR-069 fallback) and no second call
  with credentials is made (FR-059) (ruled 2026-10-31, Clarifications R-17). SC-041.
- **FR-093** The RustSec advisory check (R-15). The Phase 12 release checklist in `specs/Krate-manager-roadmap.md` gains the
  sentence that, before every manager release, the RustSec advisory database (https://rustsec.org/) is checked for
  `rquickjs`, `rquickjs-core` and `rquickjs-sys`, and that an advisory is answered with a pin bump. This phase adds the sentence
  (FR-090); it does not run the check and adds no tool for it. SC-044.
- **FR-094** PAC DNS (R-12; macOS and Linux, where the manager evaluates PAC itself). `dnsResolve`, `isResolvable` and `isInNet`
  with a name resolve through a bounded lookup on a helper thread that is **abandoned, never killed**, after `PAC_DNS_WAIT` =
  1 s (sooner when the remaining evaluation or chain deadline is shorter); an unanswered lookup is "unresolvable". A session
  looks up **at most 16 distinct names**; further names are unresolvable without a lookup, with one `warn!`. Every answer,
  including "unresolvable", is **memoised** for the rest of the session. An IP literal is parsed without a lookup. A hung
  resolver therefore never holds a check past its total timeout (FR-041). SC-042.
- **FR-095** PAC local time (R-13; macOS and Linux). `weekdayRange`, `dateRange` and `timeRange` read the clock only through
  `PacHost::now()`, which returns the Unix time and the OS local offset from UTC (or none), read **once per PAC session** (a DST
  change applies at the next check). Without the `"GMT"` argument the local fields (Unix time plus offset) are used, with it
  the UTC fields. macOS reads the offset from CFTimeZone (FR-075); Linux from `date +%z` through the bounded runner of FR-074.
  An unreadable offset is UTC plus exactly one `warn!` per session. Windows reads no offset (FR-092). SC-043.

### E. Check, cache and offline, `core::feed` (roadmap lines 532-535, 544-546; D1, D7, D8)

- **FR-050** `feed::check(ctx: &AppContext, mode: FetchMode, now_unix: u64) -> FeedCheck`, `FetchMode::{Normal,
  CheckNow}`, `FeedCheck { snapshot: Option<FeedSnapshot>, error: Option<FeedError>, cache_error: Option<FeedError> }`,
  `FeedSnapshot { feed: Feed, fetched_at_unix: u64, source: SnapshotSource::{Fresh, Cached} }`. Steps: (1)
  `http::get(&ctx.http, &ctx.feed_url, headers, MAX_FEED_BYTES)` with `MAX_FEED_BYTES` = 4 MiB of decoded body
  (FR-043), exactly one request chain and no retry (FR-059); (2) `feed::parse`; (3)
  on success write the cache (FR-052) and return `Fresh` with `fetched_at_unix = now_unix`, whatever versions the
  document names relative to the cached copy (FR-058); (4) on any failure in (1) or
  (2) read the cache (FR-053) and return it as `Cached` (its own `fetched_at_unix`) with `error: Some(<the failure>)`, or
  `snapshot: None` with the error when no usable cache exists. It never panics. The cache I/O is a seam:
  `pub(crate) fn check_with(io: &mut dyn CacheIo, ctx: &AppContext, mode: FetchMode, now_unix: u64) -> FeedCheck` does
  the work, and `check` is `check_with(&mut StdCacheIo, ctx, mode, now_unix)`. `CacheIo` has one method per FR-052 step
  (`create_dir`, `create_temp`, `write`, `sync_temp`, `persist`, `sync_dir`, plus `read` for FR-053), the shape of
  `settings::SettingsIo` (`settings.rs:219-232`, `save_with(io: &mut dyn SettingsIo, …)` at `:280-284`); the plan
  decides whether the cache reuses `SettingsIo` or declares its own trait with the same step methods.
- **FR-051** `FetchMode::CheckNow` adds `Cache-Control: no-cache` and `Pragma: no-cache` to the request; `Normal` sends
  neither (D7). Both always make the request. `feed::EDGE_MAX_AGE_SECS: u64 = 600`, documented on `FetchMode::CheckNow`
  as "a release can take up to this long to reach the client through the Pages edge (measured 2026-10-30, D7)", is the
  data Phase 6 renders beside its "check now" control; it is a measured fact, not a client timer, and nothing in Phase 5
  reads it. Ruled 2026-10-30 (Clarifications Q2, option A): "check now" is an unconditional network fetch plus these two
  headers, and Phase 6 owns the "a new release can take up to 10 minutes to appear" text (FR-090).
- **FR-052** The cache (D8) is `<state_dir>/feed-cache.json` (`FEED_CACHE_FILE`), the four keys of D8 in that order,
  pretty-printed with a final LF, written by: create the state dir if absent → create a temp file in it → write → sync
  the temp file → persist (rename) over the target → on Unix sync the directory; the same sequence and fault points as
  `settings::save_with` (`settings.rs:219-232`, `:280-302`). Each step is a fault point a `CacheIo` implementation can
  fail (FR-050, SC-013). A failure at create dir, create temp, write, sync temp or persist leaves the previous cache file
  byte-identical; a failure at sync dir (Unix only) comes after the rename, so the new file is in place, exactly as
  Phase 4's settings test expects (`settings_tests.rs:410-414`). Either way the result is `cache_error:
  Some(E_FEED_CACHE_IO { step })` beside the fresh snapshot (the fetched feed is still returned).
- **FR-053** Reading the cache: a missing file is "no cache"; a file that is not the D8 shape, carries another
  `cache_version`, names another `source_url` than `ctx.feed_url`, or whose `body` fails `feed::parse` is ignored with
  one `warn!` and counts as "no cache". A read never writes, renames or deletes.
- **FR-054** A fetched document that `feed::parse` rejects (any code of FR-020) never touches the cache file (roadmap
  line 545); neither does any transport failure.
- **FR-055** Offline (roadmap line 546): with a usable cache, every transport failure returns the cached snapshot,
  `source: Cached`, and the failure in `error`; without one, `snapshot: None` and the failure.
- **FR-056** Logging (Phase 4 D23 facade, target `krate_manager_core::feed`): exactly one line per `check`: `info!` on a
  fresh accept (`feed ok: <n> plugins, schema <v>`), `warn!` on a failure naming its code and whether a cached copy from
  `<fetched_at_unix>` is used; plus one `warn!` per ignored cache (FR-053), per cache write failure (FR-052) and per
  `prerelease` finding dropped by `feed::parse` (FR-024), one per asset or v1 entry the model drops or sanitises (FR-029;
  plan PD-8, PD-21, PD-22) (plan SI-16) and per PAC failure (FR-068). No log line carries proxy credentials (FR-046) and none carries a PAC URL's query (FR-065).
- **FR-057** `check` takes `&AppContext` and holds no lock; two concurrent checks each write a complete cache file and
  the last rename wins (atomic replace), so the file is never a mix of two writes. A check whose rename loses a race
  (Windows can refuse a replace while another rename is in flight) reports `E_FEED_CACHE_IO { step: persist }` and its
  temp file is removed (`tempfile` 3.27.0 returns the unpersisted `NamedTempFile` in `PersistError.file`, `src/file/mod.rs:516-521`, and dropping it deletes the path, `impl Drop for TempPath`, `:402`). SC-024.
- **FR-058** The fresh, contract-valid document is authoritative (Clarifications Q7, option A): an accepted fetch
  replaces `<state_dir>/feed-cache.json` and is the returned model even when it names a lower version for any plugin than
  the cached copy (a stale edge node, a deliberate rollback, a replayed document). `feed::check` never compares the fetched
  document with the cache: no per-plugin monotonic rule, no age damping (`EDGE_MAX_AGE_SECS` is not a timer), so a
  rollback takes effect at once. The consequence, accepted: an update may appear in one check and vanish in the next;
  Phase 7's per-asset signatures still bind each version. SC-031.
- **FR-059** One check is one attempt (Clarifications Q8, option A): `feed::check` makes exactly one request chain (the
  first request plus its redirect hops, FR-043), with no automatic retry for `E_FEED_OFFLINE`, `E_FEED_TIMEOUT`, `5xx` or
  any other failure, before falling back to the cache (FR-055). Retrying is the user's "check now" (Phase 6). The fixture
  server records exactly one request for the feed path per failing check. **PAC under the one-attempt rule (R-1):** the PAC
  script is fetched once per check (no retry, FR-065) and the entries of `FindProxyForURL`'s answer are walked in order
  inside the same single request chain (FR-067); the walk is not a retry. SC-032, SC-034.

### F. Injection and the Tauri shell (roadmap lines 476-479; D11)

- **FR-060** `AppContext` gains `pub http: HttpConfig`; `assemble_context(roots, feed_url, http: HttpConfig)` and
  `assemble_with_os` store it; `reassemble` / `reassemble_with_os` carry it unchanged, like `feed_url`. Phase 4's
  behaviour is otherwise unchanged (its `context::tests` keep passing with the new argument).
- **FR-061** `src-tauri/src/lib.rs` passes `HttpConfig::production().with_proxy(resolution.proxy).with_pac(resolution.pac)
  .with_pac_host(Some(os_proxy::pac_host()))` (plan SI-26), where `resolution` is
  `http::resolve_proxy(&proxy_env::read(), &NativePlatform.system_proxy(), &feed_url)` (FR-048, FR-049; `target: &str`,
  plan SI-6; `proxy_env.rs` is
  the single `src-tauri` file that reads `HTTPS_PROXY` / `ALL_PROXY` / `NO_PROXY`, FR-063); under `#[cfg(feature = "dev-feed")]` it passes
  `dev_feed::http_config(&feed_url, …)`, a one-line call of the pure core helper `http::config_for_dev_url(url: &str,
  resolution: ProxyResolution, pac_host: Option<PacHostHandle>) -> HttpConfig` (plan SI-15, SI-27, PD-12), which returns
  `HttpConfig::loopback(CONNECT_TIMEOUT, TOTAL_TIMEOUT)` with the proxy, the PAC source and the PAC host dropped when the
  URL's host is `127.0.0.1` or `[::1]` (a `dev-feed` audition against a loopback server never goes through a system
  proxy), and `HttpConfig::production().with_proxy(resolution.proxy).with_pac(resolution.pac).with_pac_host(pac_host)`
  otherwise (a non-loopback `dev-feed` URL gets the same proxy and PAC source as production). Without the feature, neither `config_for_dev_url`, `LoopbackOnly`
  nor `HttpConfig::loopback` is named anywhere in `src-tauri`.
- **FR-062** No Tauri command, capability, permission, CSP, window or frontend file changes in this phase (roadmap line
  552). The blocking `feed::check` is called off the main thread by Phase 6 (`tauri::async_runtime::spawn_blocking`,
  External facts); Phase 5 adds no call.
- **FR-063** `crates/core/tests/config_guard.rs` gains the assertions: `src-tauri/src/lib.rs` names
  `HttpConfig::production()` and passes an HTTP config to `assemble_context(`; the tokens `config_for_dev_url`,
  `LoopbackOnly` and `HttpConfig::loopback` occur in `src-tauri/src` only inside `dev_feed.rs`; `crates/core/Cargo.toml` declares
  `live-network = ["test-support"]` (its live test uses `fresh_temp_roots()`, plan PD-14) (plan SI-7) and no `default`
  feature list containing it; the Phase 4 check of `crates/core/Cargo.toml` `[features]` (`config_guard.rs:725-731`) is
  that assertion, amended to exactly the two lines `test-support = []` and `live-network = ["test-support"]` (tasks
  TA-1); **the proxy assertions (Clarifications Q5):** the
  tokens `HTTPS_PROXY`, `ALL_PROXY` and `NO_PROXY` occur in `src-tauri/src` only in `proxy_env.rs` and in no file under
  `crates/core/src` (core reads no environment, Phase 4 D9; Phase 4's env-read test
  `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` is updated to admit `proxy_env.rs` for exactly the
  names it reads and nothing else: `proxy_env.rs` reads `HTTPS_PROXY`, `https_proxy`, `ALL_PROXY`, `all_proxy`,
  `NO_PROXY`, `no_proxy`, and on Linux only `XDG_CURRENT_DESKTOP`, `XDG_CONFIG_HOME`, `HOME`, through one `var_os` call
  site, which carries one `#[expect(clippy::disallowed_methods, reason = …)]`; `injection.rs` `D6_SITES` gains
  `("src-tauri/src/proxy_env.rs", 1)` and `D6_TOTAL` becomes 8, the test keeping its name (plan SI-3)), `lib.rs` passes the resolved proxy and PAC source through `HttpConfig::with_proxy` / `HttpConfig::with_pac` and calls
  `resolve_proxy`, and `HttpConfig::with_proxy` / `HttpConfig::with_pac` / `resolve_proxy` occur in no `src-tauri` file but `lib.rs` and
  `proxy_env.rs`; `.with_pac_host(` occurs in `src-tauri` only in `lib.rs`, and `OsPacHost` only in `os_proxy.rs` (plan
  SI-26). A planted `lib.rs` without `with_proxy` and a planted `std::env::var("HTTPS_PROXY")` in another
  `src-tauri` file are each reported (self-test, SC-018).

### G. Fixture server and live test (roadmap line 538; D12, D13)

- **FR-070** `core::fixture_server::FixtureServer::start(routes: Vec<(String, Reply)>) -> FixtureServer` binds
  `127.0.0.1:0` (never a non-loopback address; `FixtureServer::start_v6(routes)` binds `[::1]:0`, a second loopback host
  for the per-hop bypass test) (plan SI-22), serves each connection on a thread, and exposes `url(path) -> String`
  (`http://127.0.0.1:<port><path>`) and `requests() -> Vec<RecordedRequest { method, path, headers }>`. `Reply` covers:
  `Ok { body, content_type }`; `Status(u16)`; `Redirect { status, location }`; `Truncated { declared_len, sent: Vec<u8> }`
  (a `Content-Length` larger than the bytes sent, then close); `Stall(Duration)` (accept, send nothing, wait at most the
  duration on a `Condvar` / shutdown flag that `Drop` signals, so dropping the server ends every stall at once and never
  blocks for the stall's length); `Close` (accept and close at once); `Garbage(Vec<u8>)` (bytes that are not HTTP);
  `Gzip { body: Vec<u8> }` (status 200 with `Content-Encoding: gzip` and the given, already compressed, bytes; the test
  builds them with `flate2::write::GzEncoder`, FR-040, so the server itself stays `std`-only);
  `HeadersThenStall { declared_len, sent, stall }` (headers and part of the body, then a stall: FR-044's mid-body
  recovery) and `Delayed { after, reply: Box<Reply> }` (a delayed reply, e.g. a redirect for the chain deadline), both
  ending at once on `Drop` as `Stall` does (plan SI-22). An unknown path is `404`.
  **Non-HTTP input:** a connection whose first byte is `0x16` (a TLS handshake record, which a ClientHello always is) is
  answered without waiting for a request line: the server reads that one record (its 5-byte header and the length it
  declares), writes the fixed bytes `HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n`, shuts
  down its write half and reads until EOF or 2 s before closing, so the client's rustls reads non-TLS bytes and fails
  with a `rustls::Error` (External facts) rather than an EOF or a reset, and the case can never wait for a CRLFCRLF that
  a ClientHello never sends. Dropping the value stops the server and joins its threads. It uses only `std`.
  **Proxy role (Clarifications Q5):** a connection whose request line is `CONNECT <host>:<port> HTTP/1.1` is recorded
  (method `CONNECT`, path `<host>:<port>`, headers) and answered by a scripted `Reply::ConnectOk` (`HTTP/1.1 200 Connection
  established`, after which the connection carries the bytes of the next scripted reply, so a following ClientHello is
  answered as FR-070's non-TLS case) or `Reply::ConnectRefused(u16)`. After `ConnectOk` the connection is served again
  from the route table (the tunnelled request is recorded like any other); there is no absolute-form role, because
  `ureq` 3.5.0 sends `CONNECT` for every target through an `http` / `https` proxy (plan SI-2, PD-7). A `CONNECT` whose
  authority is other than `127.0.0.1:<port>` or `[::1]:<port>` is answered `403` and never tunnelled, so the proxy role
  cannot be used to leave the machine (test `fixture_server::tests::connect_to_remote_host_is_refused`) (plan SI-22).
  `fixture_server::tests::replies_behave_as_scripted` tests every `Reply` variant, `set_route`, `requests()`, the `0x16`
  answer, the `ConnectOk` re-serve and `Drop` ending `Stall` / `Delayed` / `HeadersThenStall` at once (tasks TA-8).
- **FR-071** Tests read fixtures in place from `tools/feed-contract/fixtures/` through paths built from
  `env!("CARGO_MANIFEST_DIR")` (a compile-time value, not an environment read), never copy them into the repo, and write
  only into `fresh_temp_roots()` state dirs. Each test starts its own server and its own roots, so tests run in
  parallel.
- **FR-072** `crates/core/tests/live_feed.rs` holds one test, `live_feed_parses`, compiled only under
  `#[cfg(feature = "live-network")]` and marked `#[ignore = "live network; opt-in only, never in a gate"]`. It reads the
  URL from the `const FEED_URL: &str = "…";` line of `src-tauri/src/lib.rs`, runs `feed::check` with
  `HttpConfig::production()` into `fresh_temp_roots()`, and asserts a `Fresh` snapshot with at least one plugin. No
  workflow, npm script, `required-tests.json` entry or gate command enables `live-network`; the shape test asserts that
  `manager-ci.yml` and `package.json` never contain the word (FR-084). Running it is a manual user step:
  `cargo test -p krate-manager-core --features live-network --test live_feed -- --ignored`.

### H. Guards, CI and lanes (roadmap lines 547-550, 998-1000; D14)

- **FR-073** The dependencies fixed by the rulings of 2026-10-31 (Clarifications R-1 to R-6, R-8, R-15): **(a)** `ureq` with its
  default features kept plus `platform-verifier` and `socks-proxy` (R-5, FR-040). **(b)** Windows reader (R-2):
  `windows-sys` 0.61.2 (already in the lock) with exactly the features `Win32_Foundation` and `Win32_Networking_WinHttp`, a
  Windows-target-only dependency, used for `WinHttpGetIEProxyConfigForCurrentUser` (FR-049) and `WinHttpGetProxyForUrlEx` with `WinHttpOpen` (a `WINHTTP_FLAG_ASYNC` session) / `WinHttpCreateProxyResolver` / `WinHttpSetStatusCallback` / `WinHttpGetProxyResult` / `WinHttpFreeProxyResult` / `WinHttpCloseHandle` (FR-092; no separate `WinHttpDetectAutoProxyConfigUrl` step, FR-066) (plan SI-38) in the one file that holds the real Windows reader. **(c)** macOS reader (R-3): `system-configuration`
  0.8.0 and `system-configuration-sys` 0.6.0, macOS-target-only, used only in the file that holds the real macOS reader.
  **(d)** The PAC engine (R-8, R-15): `rquickjs` 0.14.0 (QuickJS-ng), **default features only** (`["std"]`) and no other feature; the
  `rust-alloc` feature is barred because it turns `set_memory_limit` into a no-op. It is a **non-Windows** dependency, declared
  in `crates/core/Cargo.toml` under `[target.'cfg(not(windows))'.dependencies]`, so it is neither compiled nor linked on Windows
  (R-15); it builds on the macOS and Linux CI legs with no system library; the plan records its resolved feature set and the
  binary-size change and shows how it meets FR-068's bound; it is confined to `pac.rs` (FR-081). **(e)** Linux: no
  crate (FR-074). **(f)** macOS-only `core-foundation` 0.10.1 in core (FR-075). `Cargo.lock` regenerates with `--locked`
  green; compliance records `cargo tree` showing exactly one `core-foundation` and one `rustls`. Nothing else is added
  without a new ruling. SC-038.
- **FR-074** Linux reads GNOME and KDE, chosen by `XDG_CURRENT_DESKTOP` (R-4): the pure `linux_uses_kde(current_desktop:
  Option<&str>) -> bool` is true when the colon-separated value has an item equal to `KDE` (case-sensitive) and false for
  every other value, including unset and empty (those take the GNOME path). The GNOME reader runs `gsettings get` calls and
  the KDE reader reads `$XDG_CONFIG_HOME/kioslaverc` (default `~/.config/kioslaverc`); no crate is added. The environment
  names the Linux reader reads (`XDG_CURRENT_DESKTOP`, `XDG_CONFIG_HOME`, `HOME`) are read in `src-tauri` only, in
  `src-tauri/src/proxy_env.rs` (FR-063, SI-3), which hands them to the reader in `os_proxy.rs` as `ProxyInputs`; the
  environment-read guard admits exactly those three names there and nothing else (plan SI-36), and
  core reads no environment (Phase 4 D9). A `gsettings` call is bounded to 2 s in total and its child is never killed (the roadmap forbids a code path that kills another process). The Linux local offset for the PAC clock (FR-095) is read with `date +%z` through the same bounded runner (2 s, the child never killed, no crate); the parser of its output is pure and tested on every host. SC-039, SC-043.
- **FR-075** `core-foundation` 0.10.1 is a macOS-only dependency of `crates/core` (`[target.'cfg(target_os =
  "macos")'.dependencies]`, R-6), used only in `sysproxy_cf.rs`, by `sysproxy_cf::system_utc_offset` (below) and by `sysproxy_cf::from_cf_proxies(&CFDictionary<CFString, CFType>) ->
  Vec<(String, ScValue)>` in `crates/core/src/sysproxy_cf.rs` (`#[cfg(target_os = "macos")]`), the conversion from the
  SystemConfiguration proxy dictionary to the key/value pairs the pure macOS parser takes (FR-049; `ScValue` is a value
  enum for string, integer and boolean entries). `[dependencies]` stays FR-080's set; `core_dependencies_are_the_fr007_set`
  also asserts that the macOS target table is exactly `core-foundation` and that `core-foundation` is named in code only in
  `sysproxy_cf.rs` and its tests. The same file holds `system_utc_offset() -> Option<i32>`, the macOS local offset from CFTimeZone for the PAC clock (R-13, FR-095): read-only, called only by the real PAC host, named in no test source; its range and rounding rule is a pure function tested on every host (SC-043). SC-040, SC-043.
- **FR-080** `injection.rs` `FR007_DEPENDENCIES` becomes `["base64", "log", "regex", "rustls", "serde", "serde_json",
  "tempfile", "thiserror", "url", "ureq"]`, and `core_dependencies_are_the_fr007_set` keeps checking
  `crates/core/Cargo.toml` `[dependencies]` against it; `[dev-dependencies]` is exactly `["flate2"]`. `cargo tree -p krate-manager-core -e normal` holds no `tauri*`, `tokio`, `reqwest`,
  `hyper`, `dirs*`, `directories*` or `home` crate (recorded in compliance). The `platform-verifier` feature (FR-040)
  adds its own transitive crates (`rustls-platform-verifier` and its per-OS dependencies); they are not in the excluded
  list, are recorded in compliance with the `cargo tree` output, and the lock regenerates with `--locked` green. The
  per-OS proxy-reader dependencies are ruled by the user and are not in this set (FR-073(b), (c), FR-075), and neither is `rquickjs`, which lives only in the non-Windows target table (FR-073(d), SC-038).
- **FR-081** New test `ureq_is_confined_to_http_rs` (in `injection.rs`): the tokens `ureq` and `rustls` occur, in code,
  in no workspace `.rs` file except `crates/core/src/http.rs` and its `http_tests.rs` (which inspects `agent_config`,
  FR-043), and `http.rs` holds no `pub use ureq` / `pub use rustls` and no `pub` item exposing a `ureq` type;
  `ureq` and `rustls` appear in no `Cargo.toml` except `crates/core/Cargo.toml` and the workspace root's
  `[workspace.dependencies]`; the tokens `TcpStream` and `TcpListener` occur, in code, in no workspace `.rs` file except
  `http.rs` and `fixture_server.rs` (and their `*_tests.rs`); the tokens `UdpSocket` and `to_socket_addrs` occur, in code,
  only in `http.rs` (and its tests), and `pac.rs` names no socket type; a planted `UdpSocket::bind` in `pac.rs` is reported
  (self-test) (plan SI-31). Planted `use ureq as u;` and `std::net::TcpStream::connect`
  in another in-memory file are each reported (self-test). `rquickjs` is confined the same way to
  `crates/core/src/pac.rs` and its `pac_tests.rs` (and to `crates/core/Cargo.toml` plus the workspace root), `windows-sys`,
  `system-configuration` and `core-foundation` are named in code only in the files FR-073 and FR-075 name, and a planted
  use of each outside its file is reported (self-test).
- **FR-082** New integration test file `crates/core/tests/roster.rs`: `roster_and_hosts_absent_from_sources` reads the
  `name` of every `plugins/*/version.json` (repo root = app root's `../..`), fails if fewer than nine names are found
  (a vacuous scan), and searches, case-sensitively, every file under `crates/core/src`, `crates/helper/src`,
  `src-tauri/src`, `src-tauri/capabilities`, the `src-tauri/tauri*.conf.json` files, `src/` and `index.html`, except
  `*_tests.rs`, `test_support.rs` and `*.test.ts`, for each name and for `api.github.com`; any hit
  fails with the file and line. `roster_scanner_reports_planted_names` runs the same scanner on in-memory text and
  must report a planted name and a planted host.
- **FR-083** `manager-ci.yml` step "roster absent", after "dev-feed absent" on every leg: it builds the name list as
  FR-082 does (Node, from `../../plugins/*/version.json`), requires at least nine names, requires the positive control
  `com.krateaudio.krate-manager` (`platform::APP_IDENTIFIER`) to occur in the release binary (so the scan reads the
  binary), then counts, with a Node byte search (`Buffer.includes`) over the release binary and every file under `dist/`
  (plan SI-17), each name and `api.github.com` in the release binary
  (`target/release/krate-manager.exe` on Windows, `target/release/krate-manager` elsewhere) and in every file under
  `dist/`; every count must be 0. It prints a `::notice` or `::error` naming the leg (Phase 4 D21).
- **FR-084** `manager-ci.yml`'s `push` and `pull_request` path filters add `'tools/feed-contract/**'` (the Rust tests
  read the contract and its fixtures in place, so a contract change must run them). `manager-ci-shape.test.js`: `PATHS`
  gains the line, `STEPS` gains "roster absent", and the existing top-level test also asserts that the string
  `live-network` appears in neither `manager-ci.yml` nor `apps/krate-manager/package.json`. The shape test also gains
  the new `EXPECTED_REQUIRED` (equal to plan §4.13), a `COMMANDS['roster absent']` entry and `'roster absent'` in
  `ASSERTION_STEPS` (plan SI-4). If the TAP plan count of
  `feed-tools.yml` (`PLAN = "1..59"`) changes, it is updated with the names list.
- **FR-085** `tests/required-tests.json`: every new Rust test named in SC-002 to SC-040 joins `cargo.all` (among them
  `feed::tests::accept_rows_parse_through_the_client`, `feed::tests::gen_feed_outputs_parse`,
  `feed::tests::check_logs_one_line`, `feed::tests::concurrent_checks_leave_one_whole_file`,
  `contract::tests::ecma_pattern_translation`, `http::tests::next_hop_cases` and
  `production_config_only_in_allowed_tests`), except the three `version::tests::native_candidates_<os>` tests, which
  join `cargo.windows`, `cargo.macos` and `cargo.linux` respectively, and the `sysproxy_cf` tests of SC-040, which join `cargo.macos` only, and every `cfg(not(windows))` test of SC-033 to SC-036 and SC-042 (plan §4.13 lists them), which joins `cargo.macos` and `cargo.linux` only (plan SI-39); the per-OS reader seams of FR-049 join the OS lists they compile on (`sysproxy::tests::bounded_run_abandons_and_reaps` `cargo.macos` and `cargo.linux`) (plan SI-21); `fixture_server::tests::replies_behave_as_scripted` (FR-070) joins `cargo.all` (tasks TA-8); `live_feed_parses` is
  never listed.
- **FR-086** No new test or step uses `--test-threads=1`, a global mutex around tests, or a fixed port; every server
  binds port 0.
- **FR-087** New test `production_config_only_in_allowed_tests` (in `injection.rs`): in every test source
  (`crates/*/src/**/*_tests.rs`, `crates/*/src/test_support.rs`, `crates/*/tests/*.rs`), the tokens
  `HttpConfig::production()` and `UrlPolicy::HttpsOnly` occur in code only inside the bodies of
  `https_only_refuses_before_connecting`, `config_for_dev_url_cases`, `url_policy_cases`, `next_hop_cases`,
  `agent_trusts_the_os_store`, `proxy_scheme_cases`, `production_values` (plan SI-10), `pac_policy_cases` and
  `wpad_candidate_cases` (plan SI-26) in `http_tests.rs` / `pac_tests.rs` (none of
  which connects to a non-loopback host: the first is refused before connecting, the others are pure) and in
  `crates/core/tests/live_feed.rs`; as string literals they may also occur in `config_guard.rs` (FR-063's needles) and
  in `injection.rs` itself (its plants). A function body is the text from `fn <name>(` to the next top-level `fn ` or
  `#[test]`. A planted `HttpConfig::production()` in another in-memory test body is reported (self-test). This is the
  mechanical form of SC-021's "nothing leaves the machine". **The proxy extension (Clarifications Q5):** in the same test
  sources, `.with_proxy(` occurs only inside the bodies of the proxy-path tests of SC-030 (whose proxy is always the
  loopback fixture server, FR-042), and `.system_proxy(` (the real readers of FR-049) occurs in no test source (the
  parsers are tested through their injected-input functions); a planted `.with_proxy(` in another body and a planted
  `NativePlatform.system_proxy()` are each reported (self-test). **The PAC extension (R-1):** `.with_pac(` occurs only inside
  the bodies of the PAC-path tests of SC-034 to SC-036 (whose PAC URL is always the loopback fixture server, FR-065); no test
  calls a real WPAD discovery, an OS PAC reader or the real Windows OS PAC evaluator (FR-066, FR-092), no test fetches a PAC URL that is not `127.0.0.1` / `[::1]`, and
  no test resolves a real name or reads a real clock through `PacHost`; a planted `.with_pac(` in another body and a planted
  call of the real discovery function are each reported (self-test). `.with_pac_host(` occurs only inside the PAC-path
  test bodies of SC-034 to SC-036 and SC-041; `OsPacHost` (whose Windows `os_find_proxy` is the real Windows OS PAC
  evaluator), `std_resolve(`, `std_my_ip(` and `system_utc_offset(` occur in no test source (plan SI-26); and
  `PacHostHandle::new(` occurs only inside `impl FakePacHost` in `pac_tests.rs` (tasks TA-12).
- **FR-088** One process-wide capture logger in `test_support` (`install_capture()`, idempotent through a `OnceLock`;
  it records `(ThreadId, target, level, message)` for every target under `krate_manager_core`, and `take_mine()` returns
  and removes the calling thread's records, the per-thread filter of `context_tests.rs:446-484`). Phase 4's
  `context::tests::startup_errors_are_logged` calls it instead of `log::set_logger(&CAPTURE).expect("the only test that
  installs a logger")` (`context_tests.rs:497`), which would panic in whichever of two logger-installing tests runs
  second in the shared unit-test binary (`log::set_logger` succeeds once per process); its own `Capture` /
  `CAPTURE` / `take_mine` move to `test_support`. `feed::tests::check_logs_one_line` (SC-022) calls it too. No test
  calls `log::set_logger` directly.

### I. Scope fences

- **FR-090** Files outside `apps/krate-manager/**` this phase may change: `.github/workflows/manager-ci.yml` (FR-083,
  FR-084), `tools/feed-contract/tests/manager-ci-shape.test.js` (FR-084), `.github/workflows/feed-tools.yml` only for the
  `PLAN` / names lines if FR-084 changes the count, `specs/krate-manager-phase5/**`, in `specs/Krate-manager-roadmap.md`
  only the Phase 5 status line and, per the user's ruling (Clarifications Q2, option A, 2026-10-30), lines 534-535
  (restated as "a manual 'check now' always fetches from the network with `no-cache` request headers; the Pages edge
  ignores them and may serve a copy up to 600 s old, measured 2026-10-30") and one sentence added to Phase 6's components
  naming Phase 6 as the owner of a "check now" control whose text states that a new release can take up to ten minutes
  to appear (`feed::EDGE_MAX_AGE_SECS`, FR-051, D7), **and** (Clarifications Q3, option A) the roadmap sentence that
  compares `min_os` with the running OS: Phase 5 owns the pure comparison and `Availability::NeedsNewerOs`, Phase 6 owns
  reading the real running-OS version through `Platform` and showing the outcome (FR-035 to FR-038); **and** (Clarifications R-15) the Phase 12 release-checklist sentence naming the RustSec check for `rquickjs` (FR-093);
  `specs/krate-manager/probes/results.md`, add-only: exactly one new table row, the R-15 Windows auto-detect measurement
  (plan §4.14), and no other change to that file (plan SI-41); and
  `specs/INDEX.md` (FR-091). The roadmap edits are made in this phase's build, nowhere else. Nothing under
  `plugins/`, `dsp/`, `cmake/`, `extern/`, no `tools/feed-contract/` schema, contract, listing or fixture file, and no other
  workflow.
- **FR-091** `specs/INDEX.md` is regenerated with `node tools/gen-specs-index.js`, so `ci.yml`'s `--check` stays green.

---

## Success Criteria

Rust SCs run locally on Windows with `cargo test --workspace --locked` from `apps/krate-manager/` (default test
threads); a run counts only with every binary reporting `0 failed`. macOS and Linux rows are **PENDING-CI** on
`manager-ci.yml` jobs `check (macos-latest)` and `check (ubuntu-24.04)`, closed only by that leg's `required tests ran
(<os>)` `::notice` whose list contains the row's test names (Phase 4 FR-077, D21), or, for FR-083, by the leg's
`roster absent` `::notice`. Node SCs run with `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"`.

- **SC-001 (Windows local gate, zero warnings).** From `apps/krate-manager/`: `cargo fmt --all --check` exit 0;
  `cargo clippy --workspace --all-targets --locked -- -D warnings` exit 0 with 0 `warning:` lines; `cargo clippy -p
  krate-manager-core --all-targets --features live-network --locked -- -D warnings` exit 0 (the gated test compiles
  clean, it is not run); `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` exit
  0; `cargo test --workspace --locked` exit 0; `npm run typecheck`, `npm run lint`, `npm test` exit 0;
  `node ../../tools/lint-manager-version.js` exit 0; `npm run tauri build` exit 0 with 0 `^warning:` lines; the FR-083
  scan run by hand over `target/release/krate-manager.exe` and `dist/` gives the positive control > 0 and every count
  0 (output saved under `specs/krate-manager-phase5/evidence/`); `git status --porcelain` taken after the lane, with
  the lines under `specs/krate-manager-phase5/evidence/` removed, equals the snapshot taken before the lane (the
  phase's own changes are uncommitted at gate time, so "empty" is unreachable; the criterion is that the build adds no
  tracked or untracked file, the one Phase 4 measured, `specs/krate-manager-phase4/compliance.md:88`); no `todo!(` or
  `unimplemented!(` occurs in any workspace `.rs` file outside `*_tests.rs` and `tests/` (`grep -rn`, 0 lines) (tasks
  TA-4). Threshold: every command exit 0, 0 warnings, the two status snapshots equal, 0 `todo!(` / `unimplemented!(` lines.
- **SC-002 (the contract verdicts, read in place).** `contract::tests::c1_cases_reproduce` reads
  `tools/feed-contract/fixtures/feeds/cases.json` and, for every row, runs `contract::validate` with the row's `schema`
  on the row's file (`E_INPUT` rows through `serde_json` first): `accept` → 0 findings (and a `v2` accept row also has 0
  findings under `V1`: every v2-valid document is v1-valid, Phase 1 FR-012, CONTRACT.md:110-112); `accept-without-platforms` → 0 findings; `{"error": X}` → `X` is among the findings, as a code or as the pair
  `"<instance_path> <keyword>"` (README `:51-65`). The test asserts it visited every row and that the row count equals
  the file's (90 today, never a hard-coded number in the loop). `feed::tests::accept_rows_parse_through_the_client`
  runs the client itself over the same file: every `accept` and `accept-without-platforms` row's file (11 today, the
  test requires at least one and counts them from the file) gives `feed::parse` → `Ok(Feed)`, with `Feed.plugins` keys
  equal to the document's `plugins` keys and `Feed.schema_version` equal to the document's `schema_version`, and for
  every `accept-without-platforms` row the named plugin's entry resolves `NotThroughManager` (FR-023) on all four
  `(Os, Arch)` pairs of FR-034 (moved here from `c1_cases_reproduce`, because `contract` builds no model) (tasks TA-2); a builder
  defect (a failed `sha256` decode, `PlainName` / `AssetUrl` construction or `format` mapping, a panic) fails it.
  Threshold: every row, both tests. Local; PENDING-CI on both legs (`all` list).
- **SC-003 (every generator output is accepted).** `contract::tests::gen_feed_outputs_accepted`: each
  `tools/feed-contract/fixtures/gen-feed/*/expected.v2.json` (57 today; the test requires at least one) has 0 findings
  under `V2`, and each `expected.legacy.json` (16 today) has 0 under `V1`. `feed::tests::gen_feed_outputs_parse`: each
  of the same files also gives `feed::parse` → `Ok(Feed)`, `schema_version` 2 for every `expected.v2.json` and 1 for
  every `expected.legacy.json`, with `Feed.plugins` keys equal to the document's (these outputs are the closest stand-in
  for what Phase 3 deploys). Threshold: every file, both tests. Local; PENDING-CI (`all`).
- **SC-004 (schema dispatch).** `feed::tests::schema_version_dispatch`: `valid/full-v2.json` with `schema_version`
  replaced in memory by: absent → v1 path (accepted, every entry `release: None`); `1` → the same; `2` → v2 model; `3` →
  `E_FEED_FUTURE_SCHEMA { found: 3 }`; `0`, `-1`, `2.5`, `"2"`, `null` → `E_FEED_SCHEMA_VERSION`; `2.0` → v2 model
  (FR-002's integer rule). `invalid/schema_version-const.json` parsed by `feed::parse` is accepted with 3 entries, each
  `NotThroughManager` on all four `(Os, Arch)` pairs of FR-034 (D3). `invalid/malformed-json.json`, a body with a UTF-8
  BOM, a non-UTF-8 byte and an HTML page (a captive portal) are each `E_INPUT`. Threshold: every case. Local; PENDING-CI
  (`all`).
- **SC-005 (the model).** `feed::tests::model_of_full_v2`: `valid/full-v2.json` gives exactly the expected `Feed`:
  three plugins `Alpha`, `Bravo`, `Charlie` in that order; each `version` / `tag` / `pub_date` / `release_notes_url` /
  `notes_markdown`; each platform's `url`, `asset_name` (equal to the URL's last segment, e.g.
  `Alpha-1.2.0-Windows-x64.exe`), `size` (the byte length of `krate fixture asset <asset name>\n`, README `:78`,
  computed in the test) and `sha256` (the document's hex, decoded), `format`, `min_os` (`11.0` on darwin, `2.31` on
  linux, `10.0` on Charlie's windows, README `:72`, `:82`); the Linux `payload` with `files: 3` (README `:79`); Bravo's `preset_subdir`
  `Kits`; `manager.latest_json_url`. `feed::tests::unknown_keys_do_not_change_the_model`: the models of
  `unknown-extra-keys.json` and `full-v2.json` are equal. `feed::tests::prerelease_sibling_is_ignored`: in
  `prerelease-plain.json` and `prerelease-suffixed.json` Alpha's model equals its stable entry (version `1.2.0`, its own
  three assets), never the sibling's (`alpha/v1.3.0` / `alpha/v1.3.0-beta.1`). Threshold: exact equality. Local;
  PENDING-CI (`all`).
- **SC-006 (platform resolution on every host).** `version::tests::resolve_table` (pure, all pairs run on every OS, `running` = `None` so `min_os` never gates; the
  `min_os` rows are SC-027): over `full-v2.json`: `(Windows, X86_64)` → Alpha, Bravo, Charlie `Available(windows-x86_64)`; `(Macos, Aarch64)` →
  Alpha `darwin-aarch64`, Bravo `NotForThisMachine`, Charlie `darwin-universal`; `(Macos, X86_64)` → Alpha
  `NotForThisMachine`, Bravo `darwin-x86_64`, Charlie `darwin-universal`; `(Linux, X86_64)` → all `linux-x86_64`;
  `(Windows, Aarch64)`, `(Linux, Aarch64)`, `(Macos, Other)` → all `NotForThisMachine`. Over `mixed.json`, Delta is
  `NotThroughManager` on every pair; over `unknown-platform-key.json`, Delta (only `windows-aarch64`) is
  `NotThroughManager` on every pair; over `v1-only.json`, every entry on every pair. `platform_candidates` returns the
  FR-034 table for all pairs. Threshold: every cell. Local; PENDING-CI (`all`).
- **SC-007 (native resolution per OS).** Each test is gated on `target_os` only and expects the row that matches the
  binary's `cfg!(target_arch)`, so it holds on every host the manager supports (Q6: the universal macOS build covers
  Intel Macs, roadmap line 29), not only on today's runners. `version::tests::native_candidates_windows` (Windows):
  `Arch::native()` is `X86_64` on an `x86_64` build (`Aarch64` on an `aarch64` one) and `resolve_native(entry, None)` over
  `full-v2.json` gives the SC-006 row of that `(Windows, arch)` pair; `version::tests::native_candidates_macos`
  (macOS): `Arch::native()` is `Aarch64` on an `aarch64` build and `X86_64` on an `x86_64` one, and the result equals
  the `(Macos, Aarch64)` or `(Macos, X86_64)` row respectively; `version::tests::native_candidates_linux` (Linux): the
  same for `(Linux, arch)`. Local Windows (the `x86_64` branch); PENDING-CI `check (macos-latest)` (`macos` list; the
  arm64 runner exercises the `aarch64` branch, and the `x86_64` branch is unproven until an Intel host runs it) and
  `check (ubuntu-24.04)` (`linux` list).
- **SC-008 (the version model).** `version::tests::parse_core_cases`: `0.0.0`, `1.10.2`, `999999999.0.1` parse;
  `1.2`, `1.2.3.4`, `01.2.3`, `1.02.3`, `1.2.3-beta.1`, `1.2.3+b1`, ` 1.2.3`, `1000000000.0.0`, `` (empty) do not;
  `Display` round-trips; ordering `0.12.2 < 0.12.10 < 1.0.0 < 1.9.9 < 1.10.0`.
  `version::tests::reduce_class_version_cases`: `1.0.0.1` → `1.0.0`; `1.0.4.0` → `1.0.4`; `0.8.0.3` → `0.8.0`; `1.2.3` →
  `1.2.3`; `1.2.3.0123456789` (ten digits), `1.2.3.x`, `1.2.3.4.5`, `1.2`, `1.2.3-beta`, `1.2.3.` and `01.2.3.4` → `None`.
  `version::tests::compare_cases`: every row of FR-032 (`NotInstalled`, `Unknown` both ways, `UpToDate`,
  `UpdateAvailable`, `InstalledNewer` for installed `1.0.4` against available `1.0.3`). Threshold: every case. Local;
  PENDING-CI (`all`).
- **SC-009 (a fresh fetch writes the cache).** `feed::tests::check_fresh_writes_cache`: a fixture server serving
  `full-v2.json` at `/versions.json`, `AppContext` from `fresh_temp_roots()` with `feed_url` = that URL and
  `HttpConfig::loopback`; `check(Normal, 1_700_000_000)` returns `Fresh`, the full-v2 model, `fetched_at_unix`
  1700000000, no error; `<state>/feed-cache.json` exists with exactly the D8 keys, `source_url` = the URL and `body`
  byte-equal to the served file; the recorded request is one `GET /versions.json` with `User-Agent:` equal to
  `format!("krate-manager/{}", env!("CARGO_PKG_VERSION"))` (a compile-time value, FR-071; never a literal version, so a
  manager version bump through `version.json` and `tools/lint-manager-version.js` cannot turn it red), `Accept:
  application/json` and no `Cache-Control` / `Pragma`; the test also reads `apps/krate-manager/version.json` at test
  time and asserts its `version` equals `CARGO_PKG_VERSION`.
  `feed::tests::check_now_sends_no_cache`: the same with `CheckNow` records `Cache-Control: no-cache` and `Pragma:
  no-cache`, and `feed::EDGE_MAX_AGE_SECS` is `600` (FR-051, ruled Clarifications Q2). A `CheckNow` check performed
  immediately after a `Normal` one still makes its own request (the server records two `GET`s): there is no local
  freshness window (D7). Threshold: exact.
  Local; PENDING-CI (`all`).
- **SC-010 (a rejected feed never replaces the last good copy).** `feed::tests::rejected_feed_keeps_last_good_copy`: for
  every `cases.json` error row whose client verdict is a rejection (the client's verdict follows the document's own
  `schema_version`, D3: every error row today except `schema_version-const`, whose document says `1`, and the seven
  `sibling-*` rows, which FR-024 accepts; the two `v1` rows
  carry `schema_version: 2` and are rejected under C1 too), with the cache seeded by a fresh check of `full-v2.json` from the same URL, a
  second check serving the invalid file returns `Cached` with the full-v2 model and `error` = `E_FEED_INVALID` (or
  `E_INPUT` for `malformed-json`) and the cache file's bytes and modification time are unchanged. It covers the roadmap's
  named cases (line 541-545): malformed JSON, wrong types (`version-type`, `platform-size-type`), an `install` value
  with `..` (`install-dotdot`), a `/releases/latest/` URL (`url-latest`), an `http://` asset URL (`url-https`), a
  `tar.gz` entry without `payload` (`payload-tar-gz-required`). The seven `invalid/sibling-*.json` rows
  (`sibling-version-forbidden`, `-download_url-forbidden`, `-release_notes-forbidden`, `-channel-const`,
  `-tag-required`, `-tag-pattern`, `-platforms-requires-install`, all on Bravo's sibling) are **not** rejection rows
  (Clarifications Q1, option B, FR-024): the client's verdict on each is accepted. `feed::tests::broken_sibling_never_blocks_stable`:
  for each of the seven rows (the file is the changed `full-v2` body, so the stable model is the full-v2 stable model
  with Bravo's `prerelease` absent), `feed::parse` returns `Ok(Feed)` whose Bravo entry equals the `full-v2.json` Bravo
  entry (its stable version and its three assets), and the capture logger of FR-088 records exactly one `warn!` (target
  `krate_manager_core::feed`) per dropped finding, naming the finding's code, `instance_path` and keyword (for the seven
  rows, the pairs in `cases.json`): `/plugins/Bravo/prerelease/version false schema`, `.../download_url false schema`, `.../release_notes false schema`,
  `.../channel const`, `/plugins/Bravo/prerelease required`, `.../tag pattern`, `/plugins/Bravo/prerelease dependentRequired`;
  a `check` that serves each row after a seeding check of `full-v2.json` returns `Fresh` with Bravo's stable model, no
  `error`, and overwrites the cache. Two boundary cases pin the prefix filter: a document with a sibling finding **and**
  a stable-entry finding (`invalid/install-dotdot.json` with a broken sibling merged in memory) is still `E_FEED_INVALID`
  and its findings list holds only the non-sibling finding; and the pure filter `feed::tests::sibling_prefix_filter`
  over synthetic findings drops the paths `/plugins/Bravo/prerelease`, `/plugins/Bravo/prerelease/tag` and
  `/plugins/Bravo/prerelease/platforms/windows-x86_64/url` and `/plugins/Alpha/prerelease/tag` (every plugin's own
  sibling prefix), and keeps `/plugins/Bravo`, `/plugins/Bravo/version`, `/plugins/Bravo/prereleaseX/tag`,
  `/prerelease` and `/plugins/prerelease/tag` (a plugin that is *named* `prerelease`: only the third path segment
  decides); an `E_INSTALL_DUPLICATE` one of whose two sides is a sibling's `install` field is dropped, in both key
  orders (the sibling's plugin key sorting before and after the stable one's) (plan SI-8). `contract::validate` on the seven files still reports each finding (SC-002
  unchanged). Threshold: every row. Local; PENDING-CI (`all`).
- **SC-011 (offline and stale).** `feed::tests::offline_returns_cached_flagged_stale`, every case with
  `HttpConfig::loopback(1 s, 1 s)`: after a seeding fresh check, each of `Reply::Close`, `Reply::Garbage` and
  `Reply::Stall(30 s)` → `Cached`, the seeded model, `error` `E_FEED_OFFLINE` (the first two) or `E_FEED_TIMEOUT`; the
  same three with no cache → `snapshot: None` and the same codes. There is no "server dropped, port now refuses" case: a
  released ephemeral port can be handed to a server another parallel test starts (FR-086), which answers `404`; a
  refused connection maps through the same no-response row of FR-044 as `Reply::Close`. Threshold: every case; the
  whole test, including dropping the stalled server (FR-070 ends the stall at once), finishes in under 5 s. Every case
  also asserts `requests()` holds exactly one request for the feed path (no retry, FR-059, SC-032). Local;
  PENDING-CI (`all`).
- **SC-012 (transport rules).** `http::tests::url_policy_cases` (pure): `HttpsOnly` accepts `https://example.invalid/x`
  and refuses `http://127.0.0.1/`, `ftp://…`, `file:///…`, `https://u:p@host/`; `LoopbackOnly` accepts
  `http://127.0.0.1:9/x`, `http://[::1]:9/x`, `https://127.0.0.1/x` and refuses `http://localhost/`,
  `https://example.invalid/`, `http://10.0.0.1/`, `http://127.0.0.2/` — each `E_FEED_URL`.
  `http::tests::redirects_are_rechecked` (fixture server): a chain of 5 redirects (`301`, `302`, `303`, `307`, `308`)
  ending at the feed succeeds; 6 is `E_FEED_REDIRECT`; a redirect without `Location` is `E_FEED_REDIRECT`; a redirect to
  `https://example.invalid/versions.json` under `LoopbackOnly` is `E_FEED_URL` and the server recorded no further request
  (nothing left the machine); a relative `Location` (`/v2.json`) is followed. `http::tests::next_hop_cases` (pure, the
  production policy the fixture server cannot reach): under `HttpsOnly`, `https://a.invalid/` + `302 Location:
  http://b.invalid/` → `E_FEED_URL`; + `302 Location: /v2.json` → `Follow(https://a.invalid/v2.json)` (a relative hop
  stays `https`); + `302 Location: https://b.invalid/x` → `Follow`; + `302` with no `Location` → `E_FEED_REDIRECT`; +
  `200` → `Done`; under `LoopbackOnly`, `http://127.0.0.1:9/` + `307 Location: http://example.invalid/` →
  `E_FEED_URL`. `http::tests::status_and_body_errors`:
  `404`, `500`, `304` → `E_FEED_HTTP_STATUS` with that status; a 200 body of `MAX_FEED_BYTES + 1` bytes →
  `E_FEED_TOO_LARGE`; exactly `MAX_FEED_BYTES` bytes → `http::get` returns all of them (the boundary pair that pins
  FR-043's `take(max_bytes + 1)` wrapper; `ureq`'s own limit would fail this case, `limit.rs:22-25`);
  `Reply::Gzip` whose body is small on the wire (a gzip of zeros, its compressed length asserted below 64 KiB) and
  decodes to `MAX_FEED_BYTES + 1` bytes → `E_FEED_TOO_LARGE`, and one that decodes to exactly `MAX_FEED_BYTES` bytes →
  all of them, decoded (the cap counts decoded bytes); `Reply::Truncated` → `E_FEED_BODY`; `https://127.0.0.1:<port>/`
  against the plain-HTTP server → `E_FEED_TLS` (FR-070's `0x16` answer, FR-044's `rustls::Error` downcast) within the
  1 s / 1 s config, never `E_FEED_OFFLINE` or `E_FEED_TIMEOUT`. No case uses a certificate (D4, Review notes).
  `http::tests::https_only_refuses_before_connecting`: `HttpConfig::production()` with the fixture server's `http://`
  URL → `E_FEED_URL` and `requests()` is empty. `get` calling `next_hop` on every hop is checked by
  `redirects_are_rechecked` (the `LoopbackOnly` refusal and the six-hop limit run through it). Threshold: every case.
  Local; PENDING-CI (`all`).
- **SC-013 (cache rules and write faults).** `feed::tests::cache_rules`: a cache whose `source_url` differs from the
  current URL is ignored (an offline check returns `snapshot: None`); a cache with `cache_version: 2`, one that is not
  JSON, one with a fifth key, and one whose `body` is `invalid/install-dotdot.json` are each ignored with one `warn!`,
  and the next accepted fetch overwrites the file; a read leaves every such file byte-identical.
  `feed::tests::cache_write_fault_keeps_previous`: driving `feed::check_with` (FR-050) with a `CacheIo` that fails at
  each step in turn (create dir, create temp, write, sync temp, persist, and on Unix sync dir), a check of a changed
  feed returns `Fresh` with the new model and `cache_error` `E_FEED_CACHE_IO { step }` naming that step; after a fault
  at create dir, create temp, write, sync temp or persist the previous cache file is byte-identical, and after a fault
  at sync dir the file holds the new bytes (the rename already happened, FR-052, the `settings_tests.rs:410-414`
  split); in every case no stray temp file is read as the cache on the next check. Threshold: every case and step.
  Local; PENDING-CI (`all`; the `sync dir` step runs on the Unix legs only).
- **SC-014 (the roster follows the feed, ROSTER).** `feed::tests::roster_follows_the_feed`: `tenth-plugin.json` → 10
  entries whose names equal its `plugins` keys; `one-v2-eight-v1.json` → 9; the same document with its first key
  removed in memory → 8, the remaining keys; `v1-only.json` → 3; no list of names appears in the test (it compares
  against the document's own keys). Roadmap lines 547-549. Threshold: exact. Local; PENDING-CI (`all`).
- **SC-015 (no plugin name or `api.github.com` in the sources).** `roster_and_hosts_absent_from_sources` and
  `roster_scanner_reports_planted_names` (FR-082) pass; the first reports at least nine names read. Phase 4's
  `core_sources_hold_no_production_literal` and `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` still
  pass (no core file names the feed host; `src-tauri/src` names it once). Threshold: 0 hits, scanner self-test reports
  both plants. Local; PENDING-CI (`all`).
- **SC-016 (no plugin name or `api.github.com` in the built app).** `manager-ci.yml` step "roster absent" (FR-083) on
  every leg: positive control > 0, at least nine names, every count 0, `::notice`. Local Windows: the same commands by
  hand (SC-001). PENDING-CI: `check (macos-latest)` and `check (ubuntu-24.04)`, step "roster absent" `::notice`.
  Roadmap lines 549-550.
- **SC-017 (dependencies and confinement).** `core_dependencies_are_the_fr007_set` with FR-080's list and
  `ureq_is_confined_to_http_rs` (FR-081, `ureq`, `rustls`, `TcpStream`, `TcpListener`) pass, its self-test reporting
  both plants; compliance records `cargo tree -p krate-manager-core -e normal` with none of FR-080's excluded crates,
  with `ureq v3.5.0` and with exactly one `rustls` version; it also records `rustls-platform-verifier` (the
  `platform-verifier` feature, D4) in the tree, and the plan's / compliance's record of which `reqwest` and `rustls`
  features `tauri-plugin-updater` 2.14.1 uses and whether its TLS verifier agrees with `ureq`'s (FR-040; the verdict
  "agree" or "differ, and how" is written, not left blank). Threshold: exact set, 0 confinement findings. Local;
  PENDING-CI (`all`).
- **SC-018 (the HTTP configuration is injected).** `context::tests::http_config_is_carried`: `assemble_with_os` with
  `HttpConfig::loopback(…)` (once without a proxy, once with `proxy: Some(<a loopback ProxyConfig>)` set by struct update, so FR-087's `.with_proxy(` rule is not touched) stores it in `AppContext.http`, proxy and PAC source included (a third struct-update case with `pac: Some(<a loopback
  PacSource>)`);
  `reassemble_with_os` with new settings keeps it; Phase 4's
  `context::tests` pass unchanged in substance. The FR-063 assertions pass inside `real_src_tauri_config_passes_the_guard`,
  and `planted_config_changes_fail_the_guard` gains four plants (a `lib.rs` without `HttpConfig::production()`, a
  `LoopbackOnly` outside `dev_feed.rs`, a `lib.rs` without `with_proxy`, a `std::env::var("HTTPS_PROXY")` in a
  `src-tauri` file other than `proxy_env.rs`), each reported (FR-063). `dev_feed`'s policy choice is a pure core helper
  (`http::config_for_dev_url(url: &str, resolution: ProxyResolution, pac_host: Option<PacHostHandle>) -> HttpConfig`,
  called by `dev_feed::http_config`) tested by
  `http::tests::config_for_dev_url_cases` (`http://127.0.0.1:8080/v.json` → loopback; `https://example.invalid/v.json` →
  production; `http://localhost:8080/` → production, which then refuses it), each also with a resolution carrying a
  proxy and a PAC source: the loopback result drops them, the production result keeps them (plan SI-15, SI-27). Threshold: every case. Local; PENDING-CI
  (`all`).
- **SC-019 (the live test is opt-in only).** `cargo test -p krate-manager-core --locked -- --list` does not list
  `live_feed_parses`; `cargo test -p krate-manager-core --features live-network --test live_feed --locked -- --list
  --ignored` lists it once (both recorded in compliance; the test itself is not run by any agent or gate). The shape test
  (FR-084) asserts `live-network` is absent from `manager-ci.yml` and `package.json`. Threshold: exact. Local.
- **SC-020 (signature text and C3, structurally).** `contract::tests::trusted_comment_cases`: every
  `trusted-comments.json` `valid` row parses to its four fields; every `invalid` row fails with its `code`.
  `contract::tests::minisig_text_cases`: the KAT `.minisig` (`fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.minisig`)
  parses with key id `440FA75168B852F7` and the KAT trusted comment, which then passes `parse_trusted_comment`; every
  `full-v2.json` platform `signature` (the zero-filled `fixtureSignature` form, README `:22-25`) parses; the
  changed signature values (Alpha `linux-x86_64`) of `invalid/signature-legacy-Ed.json`, `signature-blob-length.json`
  and `signature-noncanonical-base64.json` fail `parse_minisig_text` with `E_SIGNATURE_TEXT`; that of
  `signature-trusted-comment.json` parses but its trusted comment fails with `E_TC_FIELDS`; those of
  `signature-cr.json` (a CRLF line end, which a `.minisig` file may have) and `signature-no-final-lf.json` parse, and
  are rejected only inside a feed: by FR-004's CR rule (`E_SIGNATURE_TEXT`) and by the schema `pattern` (SC-002),
  exactly as `contract.js` behaves (checked this session: `parseMinisigText` accepts both). Threshold: every row. Local; PENDING-CI (`all`).
- **SC-021 (parallel, nothing leaves the machine).** `cargo test --workspace --locked` runs with the default thread
  count; the shape test asserts no `--test-threads` in `manager-ci.yml`; `fixture_server::tests::binds_loopback_port_zero`
  asserts the server's local address is `127.0.0.1` with a non-zero ephemeral port and that two servers started at once
  get different ports; `fixture_server::tests::replies_behave_as_scripted` proves every `Reply` variant, `set_route`,
  `requests()`, the `0x16` answer, the `ConnectOk` re-serve and that `Drop` ends `Stall` / `Delayed` /
  `HeadersThenStall` at once (tasks TA-8). No Phase 5 test connects to a non-loopback host: under `LoopbackOnly` every non-loopback URL is
  refused by FR-042 before a socket opens (SC-012), and `production_config_only_in_allowed_tests` (FR-087) proves
  mechanically that `HttpConfig::production()` / `UrlPolicy::HttpsOnly`, the only policy that accepts a remote `https`
  host, appears in no test outside FR-087's allowed bodies (plan SI-10) and `live_feed.rs`; its self-test reports
  the plant. Threshold: exact. Local; PENDING-CI (`all`).
- **SC-022 (logging).** `feed::tests::check_logs_one_line`: with the shared capture logger of FR-088
  (`test_support::install_capture()`, which `context::tests::startup_errors_are_logged` now also calls, so the two
  tests pass in either order in one parallel run), a fresh check emits one `info!` (target
  `krate_manager_core::feed`) naming the plugin count, an offline check with a cache emits one `warn!` naming
  `E_FEED_OFFLINE` and the cached `fetched_at_unix`, and an ignored cache adds one `warn!`. Threshold: exact counts.
  Local; PENDING-CI (`all`).
- **SC-023 (Node lane).** `node --test` over `tools/feed-contract/tests/*.test.js` passes with `# fail 0` and the
  `feed-tools.yml` `PLAN` line, including the updated `manager-ci.yml keeps the Phase 4 shape` test (new `PATHS` line,
  "roster absent" step, `live-network` ban). Local, Windows; PENDING-CI `feed-tools.yml` `node-test (ubuntu-latest |
  macos-latest | windows-latest)`.
- **SC-024 (concurrent checks leave one whole file, FR-057).** `feed::tests::concurrent_checks_leave_one_whole_file`:
  two fixture servers serve `valid/full-v2.json` and `valid/mixed.json`; one `AppContext` from `fresh_temp_roots()` is
  cloned eight times with `feed_url` alternating between the two servers' URLs (one state dir, so no second
  `StateLock`, Phase 4 D22); eight threads released by a `std::sync::Barrier` each run `check(Normal, …)`. Afterwards
  every check returned `Fresh`; any `cache_error` is `E_FEED_CACHE_IO { step: persist }` and at least one check has
  none; `feed-cache.json` parses as D8, its `body` is byte-equal to one of the two served files and its `source_url` is
  the URL of the server that served that file; the state dir holds no file other than `feed-cache.json` (and Phase 4's
  own files, compared with a listing taken before). Threshold: exact. Local; PENDING-CI (`all`).
- **SC-025 (ECMA pattern semantics, FR-002).** `contract::tests::ecma_pattern_translation`: through
  `ecma_pattern_to_rust`, `^https://[^\s]+$` refuses `https://a\u{FEFF}b` and accepts `https://a\u{0085}b` (both as
  Ajv does, the two directions in which `regex`'s own `\s` differs); the `reservedStem` pattern matches `CON.txt` and
  does not match `CON.\r`, `CON.\u{2028}` (ECMA `.` excludes them); `\d`, `\w`, `\b` in a pattern are
  `ContractError::Unsupported`. The reference verdicts were measured for this revision by compiling the schema's own
  `$defs.url.pattern` and `$defs.reservedStem.pattern` (`versions.v2.schema.json:48-62`) with Node's `RegExp(…, 'u')`,
  as `contract.js:130-132` does: `https://a\u{FEFF}b` no match, `https://a\u{0085}b` match, `CON.txt` match,
  `CON.\r` and `CON.\u{2028}` no match; compliance re-records them. Threshold: every case. Local;
  PENDING-CI (`all`).
- **SC-026 (the OS trust store, D4, FR-043).** `http::tests::agent_trusts_the_os_store`: `http::agent_config` of
  `HttpConfig::loopback(…)` has its TLS root certificates set to `RootCerts::PlatformVerifier` (`matches!`), not
  `WebPki`; and the agent config of every `UrlPolicy` variant agrees (the trust source does not depend on the policy).
  No certificate is used (Review notes). Threshold: exact. Local; PENDING-CI (`all`).
- **SC-027 (the `min_os` comparison, FR-035 to FR-038).** `version::tests::os_version_cases`: `11.0`, `2.31`, `10.0.19045`,
  `10.0.19045.1` parse; `11`, `11.`, `1.2.3.4.5`, `a.b`, `-1.0`, `11.0 `, `4294967296.0` do not; `11.0 == 11.0.0`,
  `2.31 < 2.34`, `10.9 < 10.10`, `12.0 > 11.7.10`. `version::tests::needs_newer_os_table`: over `full-v2.json` (macOS
  `min_os` `11.0`, Linux `2.31`, Charlie's Windows `10.0`): `(Macos, Aarch64, Some(10.15))` → Alpha
  `NeedsNewerOs { min_os: 11.0 }`, `(Macos, Aarch64, Some(11.0))` → `Available` (equal is enough), `(Macos, Aarch64,
  Some(14.2))` → `Available`; `(Linux, X86_64, Some(2.28))` → `NeedsNewerOs { min_os: 2.31 }`, `Some(2.31)` and `Some(2.39)`
  → `Available`; `(Windows, X86_64, Some(6.3))` → Charlie `NeedsNewerOs { min_os: 10.0 }`, Alpha (no Windows `min_os`)
  `Available` for every running version; `running = None` → `Available` on every pair (an unreadable version never
  gates); `NotForThisMachine` and `NotThroughManager` are unaffected by `running`; a native `darwin-aarch64` asset
  needing 13.0 on a machine running 12.0 is `NeedsNewerOs` and not replaced by the `darwin-universal` asset of the same
  entry (a modified in-memory copy of Charlie). `NeedsNewerOs` and `UpdateState` are independent (FR-038). Threshold:
  every cell. Local; PENDING-CI (`all`).
- **SC-028 (proxy resolution, FR-046, FR-048).** `http::tests::resolve_proxy_cases` (pure, every host): environment only
  (`https_proxy` set → that proxy; `all_proxy` only → that proxy; both → the order the plan states for them); OS settings
  only → that proxy; environment and OS settings both present → the environment's proxy with `pac: None` (FR-048(a)); `no_proxy` from the environment matching the target host (exact
  host, domain suffix `.example.invalid`, IP, CIDR `10.0.0.0/8`, `*`) → `None`; the OS bypass list likewise (`<local>`
  on Windows); the NO_PROXY and OS-bypass rules of FR-048(c), including `NO_PROXY` matching with a PAC source present → the empty
  resolution; a `pac_url` and/or `auto_detect` in `SystemProxy` with no environment proxy → `ProxyResolution { pac:
  Some(PacSource { .. }), .. }` carrying both flags, with the OS static `proxy` kept beside it as the fallback (FR-069);
  an environment proxy with a PAC source also present → the environment proxy and `pac: None`; PAC source only → `proxy:
  None`; nothing set → `None`; a malformed proxy value → `None` with one `warn!`; credentials
  in a proxy URL never appear in the `Debug`, `Display` or log output of `ProxyConfig` (`***`). `http::tests::
  loopback_proxy_only`: under `LoopbackOnly`, a proxy host that is not `127.0.0.1` / `[::1]` is `E_FEED_URL` before a
  socket opens and the server records nothing; a proxy URL `ureq` refuses (`http://%FF@127.0.0.1:<port>`) is
  `E_FEED_URL`, never a direct request (plan SI-20). Threshold: every row. Local; PENDING-CI (`all`).
- **SC-029 (the three OS proxy readers, FR-049).** `sysproxy::tests::windows_proxy_parser_cases`,
  `sysproxy::tests::macos_proxy_parser_cases` and `sysproxy::tests::linux_proxy_parser_cases` (plan SI-1) (GNOME and KDE rows): each
  pure parser, run on every host, over injected raw inputs (the Windows rows as `WinIeProxyConfig { auto_detect,
  auto_config_url, proxy, proxy_bypass }` values, the WinHTTP structure (plan SI-9), a
  SystemConfiguration dictionary as key/value pairs, `gsettings` output text, `kioslaverc` text): proxy off → no proxy;
  manual single proxy → that proxy; per-scheme list (`http=a:1;https=b:2`) → the `https` one; bypass list → `no_proxy`
  entries (`<local>`, `*.corp`, `10.*`, `ExceptionsList`, `ignore-hosts`, `NoProxyFor`); a PAC URL and an auto-detect flag
  → `pac_url` / `auto_detect` set (FR-049: Windows `fAutoDetect` / `lpszAutoConfigUrl`; macOS `ProxyAutoConfigEnable` with
  `ProxyAutoConfigURLString`, `ProxyAutoDiscoveryEnable`; GNOME `mode` `auto` with a non-empty `autoconfig-url` → `pac_url`,
  with an empty one → `auto_detect`; KDE `ProxyType` 2 → `pac_url`, 3 → `auto_detect`); a missing or malformed input → `SystemProxy::default()` with one `warn!`. The
  real-reader functions are compiled on their own OS only (`cfg(target_os)`), are not called by any test
  (`production_config_only_in_allowed_tests`, FR-087); `trait Platform` is unchanged and has no test double (plan SI-1).
  Threshold: every row, each parser. Local (all three parsers run on Windows); PENDING-CI on both other legs (`all`; the
  real reader of each OS is exercised only by that OS's compile and clippy).
- **SC-030 (the proxy path, FR-046, FR-070).** `http::tests::proxy_path_connect` (fixture server as proxy, loopback
  policy): `proxy: Some(<fixture server URL>)` and the target `https://127.0.0.1:<port2>/versions.json` (a second
  fixture server that is never reached): the proxy records `CONNECT 127.0.0.1:<port2>` with a `Host` header and no
  request reaches the target server; with `Reply::ConnectOk` followed by the plain-HTTP bytes of FR-070 the result is
  `E_FEED_TLS` (the tunnel was opened, then the TLS failure); with `Reply::ConnectRefused(407)` and with `Reply::Close`
  `http::get` returns `E_FEED_OFFLINE` (that `check` then returns the cached copy is asserted by
  `feed::tests::check_through_proxy_writes_cache`, because `http` has no cache) (tasks TA-9). `http::tests::proxy_path_http`: target
  `http://127.0.0.1:<port2>/versions.json` through the proxy: the proxy records `CONNECT 127.0.0.1:<port2>`, then the
  tunnelled `GET /versions.json`, and the body is returned; the target server records nothing (plan SI-2). `http::tests::no_proxy_bypass`: the same proxy config with the target host on its
  `no_proxy` list → the request goes to the target server directly and the proxy records nothing. `feed::tests::
  check_through_proxy_writes_cache`: `check` with the proxy config over the HTTP path writes the cache as SC-009, and
  after that seeding check, with `Reply::ConnectRefused(407)` and with `Reply::Close` at the proxy, `check` returns the
  cached copy with `E_FEED_OFFLINE` (tasks TA-9). A proxy
  credential in the proxy URL reaches the proxy as a `Proxy-Authorization` header and appears in no recorded log line.
  Threshold: every case; nothing leaves the machine (a `CONNECT` to a non-loopback host is answered `403` and never
  tunnelled, FR-070). Local; PENDING-CI (`all`).
- **SC-031 (the fresh document is authoritative, FR-058).** `feed::tests::fresh_lower_version_replaces_cache`: the cache
  seeded by a `Normal` check of `full-v2.json` (Alpha `1.2.0`); a second check serving the same document with Alpha's
  `version` / `tag` lowered to `1.1.0` (a contract-valid in-memory change) returns `Fresh`, the lowered model, no
  `error`, and the cache file's `body` is byte-equal to the second document; an offline third check then returns the
  lowered model as `Cached` (the lowering persisted). A fourth check serving `full-v2.json` again restores `1.2.0` (the
  update that vanished and came back). No `warn!` is emitted for the lowering (it is not an anomaly). Threshold: exact.
  Local; PENDING-CI (`all`).
- **SC-032 (one attempt per check, FR-059).** `feed::tests::one_attempt_per_check`: with a seeded cache and
  `HttpConfig::loopback(1 s, 1 s)`, each of `Reply::Status(503)`, `Reply::Status(500)`, `Reply::Close`, `Reply::Garbage`
  and `Reply::Stall(30 s)` makes `check` return `Cached` with the seeded model and the failure code (`E_FEED_HTTP_STATUS`
  `{ status }` for the two statuses) and `requests()` holds exactly one request for the feed path; with no cache,
  `snapshot: None` and still one request; a redirect chain of two hops to a `503` is one chain (three recorded requests,
  one per hop, and no second chain). The call returns within the total timeout plus 1 s (no sleep-and-retry). Threshold:
  exact. Local; PENDING-CI (`all`).
- **SC-033 (PAC evaluation, FR-064, FR-067).** `pac::tests::pac_helper_cases` (the pure helper functions, every host, `PacHost` injected): a table over
  every helper function of FR-064 (`isPlainHostName("intranet")` true and `("a.b")` false; `dnsDomainIs`,
  `localHostOrDomainIs`, `dnsDomainLevels`, `shExpMatch` with `*` and `?`; `isInNet("10.1.2.3", "10.0.0.0", "255.0.0.0")`
  true and `("11.1.2.3", …)` false through an injected `resolve`; `dnsResolve` and `isResolvable` through the injected
  resolver; `myIpAddress` through `my_ip`; `weekdayRange`, `dateRange`, `timeRange` through an injected `now`), each row's
  expected value written in the test from the MDN PAC page's definitions (FR-064) (plan SI-35). `pac::tests::find_proxy_result_cases`: `PROXY a:1;
  DIRECT` → [http a:1, direct] in that order; `proxy a:1;direct` (case-insensitive); `HTTPS p:3128`; `SOCKS s` → socks4
  s:1080; `SOCKS4 s:2`; `SOCKS5 s:3`; ` PROXY a ; ; DIRECT` → http a:80 then direct, the empty entry skipped without a warn;
  `BOGUS x; DIRECT` → [direct] plus exactly one `warn!` `E_PAC_RESULT`; an empty string, `undefined`, a number and an object
  → `E_PAC_RESULT` (a PAC failure); an answer longer than `MAX_PAC_ANSWER_BYTES` (64 KiB) → `E_PAC_RESULT`; an answer
  with more than `MAX_PAC_ENTRIES` (16) usable entries → the first 16 plus exactly one `warn!` (plan SI-29).
  `pac::tests::url_argument_cases`: a script that branches on its `url` argument sees the
  hop's full URL for `http` and `scheme://host:port/` for `https` (path and query replaced), and its `host` argument is the
  hop's host. **R-14 rows** (`pac::tests::script_world_cases`, through the engine): `typeof` of `Date`, `require`, `std`, `os`, `fetch`,
  `setTimeout`, `print` and `console` is `"undefined"`; `alert("x")` returns without effect; a script that calls
  `isResolvableEx`, `dnsResolveEx`, `isInNetEx`, `myIpAddressEx`, `sortIpAddressList` or `convert_addr` is `E_PAC_SCRIPT`; and the
  reversed-range rows through an injected `now`: `weekdayRange("FRI", "MON")` true on Friday and Monday and false on Wednesday
  and Saturday, `timeRange(23, 0)` true at 23:30 and 00:30 and false at 12:00, a reversed `dateRange` true on its two endpoint
  dates and false between them. The answer parser rows (`find_proxy_result_cases`) run on every host; `url_argument_cases`,
  `script_world_cases` and every row that runs a script are `cfg(not(windows))`. Threshold: every row. Local on this Windows
  host: the pure rows; the engine rows are not runnable here, PENDING-CI on the macOS and Linux legs (`cargo.macos`, `cargo.linux`).
- **SC-034 (the PAC path, FR-065, FR-067, FR-059).** Fixture servers on loopback, loopback policy.
  `http::tests::pac_path_proxy`: server A serves a script (`Reply::Ok`, content type `application/x-ns-proxy-autoconfig`,
  built with the proxy's port) returning `PROXY 127.0.0.1:<B>`; B is the proxy of FR-070 and the target C is never reached
  directly: B records `CONNECT 127.0.0.1:<C>` and, after `ConnectOk`, the tunnelled `GET /versions.json`; the body
  arrives as in SC-030 `proxy_path_http` (plan SI-34). `pac_path_direct`: the script
  returns `DIRECT` while `HttpConfig.proxy` is `Some(B)` → the request reaches C directly and B records nothing (a working
  PAC is final, FR-069). `pac_list_walk`: the script returns `PROXY 127.0.0.1:<B1>; PROXY 127.0.0.1:<B2>; DIRECT` with B1
  answering `Reply::ConnectRefused(407)` → B1 records one `CONNECT`, then B2 records one and serves it; with B1 and B2 both
  refusing, the third entry `DIRECT` reaches C; with all entries failing the result is `E_FEED_OFFLINE` and `check` returns
  the seeded cache as `Cached`; no entry is tried twice. `pac_per_hop`: C redirects to C2 and the script returns a different
  list for each port (`shExpMatch(url, "*:<port>/*")`) → the first hop uses the first list and the redirect hop the second,
  while A records exactly one request for the PAC path. `pac_fetched_once_never_via_proxy`: with `proxy: Some(B)` and `pac:
  Some(A's URL)`, B records nothing for the PAC path and A records exactly one request per check, also when the check fails.
  `pac_policy_cases`: `http::check_pac_url` under `HttpsOnly` accepts `http` and `https` and refuses `file:`, `ftp:` and a
  URL with userinfo; under `LoopbackOnly` a non-loopback host is refused with the server recording nothing; a PAC answer
  naming `example.invalid:80` under `LoopbackOnly` is `E_FEED_URL` with no socket opened. The whole check returns within the
  total timeout plus 1 s. The rows that fetch the script through the manager (everything above except the pure `check_pac_url` rows, which run on every host) are `cfg(not(windows))`: on Windows the OS fetches the script and SC-041 is their twin. Threshold: every case; nothing leaves the machine. Local on this Windows host: the pure rows; PENDING-CI on the macOS and Linux legs for the rest.
- **SC-035 (WPAD and source order, FR-066, FR-069).** `pac::tests::wpad_candidate_cases` (pure rule, every host; R-9): `pac::WPAD_DNS_URL` is exactly `http://wpad/wpad.dat` and the
  real macOS / Linux candidate list is exactly that one URL, whatever the local host name (`host.a.b.example.com`, `host` and
  the empty name give the same list: no devolved candidate, no DHCP); on Windows the manager derives no candidate (FR-092).
  `http::tests::wpad_discovery_walk` (macOS and Linux, `cfg(not(windows))`): the injected `PacHost::wpad_candidates` returns [a `404` fixture URL, a fixture URL
  serving a script] → the first is requested, then the second, whose script is used (recorded order); [`404`, `404`] with
  `url: None` → `E_PAC_FETCH` and the fallback; with both `auto_detect: true` and `url: Some(<script URL>)`, a working
  candidate wins and the `url` path is never requested, and when every candidate fails the `url` path is requested next.
  `http::tests::pac_fallback_to_static_proxy`: a failing PAC (`404`) with `proxy: Some(B)` → the request goes through B; with
  `proxy: None` → direct; a working PAC answering `DIRECT` with `proxy: Some(B)` → direct and B records nothing.
  `pac_fallback_to_static_proxy` is `cfg(not(windows))` like `wpad_discovery_walk` (its failing PAC is a `404` the manager
  fetches, which it never does on Windows, FR-092; their Windows twins are SC-041's `os_pac_failure_cases` / `os_pac_walk`)
  (plan SI-39). `http::tests::resolve_proxy_cases` (SC-028) carries the pure order rows of FR-069. Threshold: every case.
  Local: `wpad_candidate_cases`; PENDING-CI (`all`) for it, and on the macOS and Linux legs (`cargo.macos`, `cargo.linux`)
  for `wpad_discovery_walk` and `pac_fallback_to_static_proxy` (plan SI-39).
- **SC-036 (PAC failure and bounds, FR-068).** `pac::tests::script_failure_cases`: a syntax error, a script that throws, a
  script with no `FindProxyForURL`, one returning a non-string, `function FindProxyForURL(){ while (true) {} }` and a
  stack overflow (`function f(){ f() } f()`, `E_PAC_SCRIPT` under `PAC_STACK_LIMIT`) (plan SI-28) each
  return a `PacError` (`E_PAC_SCRIPT` or `E_PAC_RESULT`); the endless loop returns within `PAC_EVAL_BUDGET` + 1 s with the
  process alive and no thread left running; a script that allocates until the engine's memory limit is `E_PAC_SCRIPT` with
  the process alive. `http::tests::pac_body_cap`: a PAC body of exactly `MAX_PAC_BYTES` decoded bytes is accepted and one
  byte more is `E_PAC_FETCH`. `feed::tests::check_survives_pac_failure`: with a `404` PAC and with an endless-loop PAC, a
  `check` over a fixture feed returns `Fresh`, `error: None`, the cache written, the request made through the static
  fallback or direct (FR-069). Each failure emits exactly one `warn!` naming the code and the PAC URL without its query, and
  no warn for the cases that succeed. Threshold: every case; the longest case under `PAC_EVAL_BUDGET` + 1 s. The engine rows, `pac_body_cap` and `check_survives_pac_failure` are `cfg(not(windows))`: not runnable on this Windows host, PENDING-CI on the macOS and Linux legs; the Windows failure rows are SC-041.
- **SC-037 (a contract-valid value the model cannot hold, FR-029).** `feed::tests::model_range_cases` over in-memory
  modified copies of `full-v2.json`, each first shown to be valid by `contract::validate(…, V2)` returning no findings:
  Alpha's `linux-x86_64` `payload.files` = `1e300` → `parse` is `Ok`, that one asset is absent from the model, every other
  asset and plugin equals the unmodified model, and exactly one `warn!` `E_MODEL_RANGE` names
  `/plugins/Alpha/platforms/linux-x86_64/payload/files`; a `size` of `2.0` is accepted as 2 (no warn); a `min_os` of
  `11.4294967296` on `darwin-aarch64` of an entry that also has `darwin-universal` → the `darwin-aarch64` asset is dropped
  with one `warn!` `E_MODEL_RANGE` at its `min_os` path and `resolve` for `(Macos, Aarch64)` returns the
  `darwin-universal` asset; an entry whose every asset is dropped → `NotThroughManager`; `parse` never returns
  `E_FEED_INVALID` for any of them, and `check` returns `Fresh` with the cache `body` byte-equal to the document. One
  further row of the same shape for every rule of FR-029(c) the model carries (an asset name that is not a safe file name,
  a v1 entry with a plugin key or `download_url` outside the C1 rules, dropping that entry only). Compliance records the
  Node validator's verdict on the same documents (accepted). Threshold: exact; exactly one `warn!` per dropped item and none
  for a kept one. Local; PENDING-CI (`all`).
- **SC-038 (the ruled dependency set, FR-040, FR-073, FR-080).** `injection.rs` `reader_dependencies_are_the_ruled_set`
  over the committed `Cargo.toml` files: `ureq` is `3.5.0` with features exactly `["platform-verifier", "socks-proxy"]` and
  no `default-features = false`; `windows-sys` is `0.61.2` with features exactly `["Win32_Foundation",
  "Win32_Networking_WinHttp"]` under a Windows target table of exactly one crate; `system-configuration` is `0.8.0` under a
  macOS target table of exactly one crate; `rquickjs` is `0.14.0` with no `features` key and no `default-features = false`, declared only in the
  `[target.'cfg(not(windows))'.dependencies]` table of `crates/core/Cargo.toml` (a table of exactly that one crate) and absent
  from `[dependencies]`, from every Windows target table and from `FR007_DEPENDENCIES`, with `cargo tree -p krate-manager-core
  --target x86_64-pc-windows-msvc` listing no `rquickjs*` crate (recorded in compliance); a planted `rust-alloc` feature and a
  planted `rquickjs` in `[dependencies]` are each reported (self-test); a planted wrong feature list and a planted second crate naming `windows-sys` are each reported
  (self-test). Compliance records `cargo tree -p krate-manager-core -e features -i webpki-roots` (compiled in through the
  default `rustls` feature) beside SC-026's proof that `RootCerts::PlatformVerifier`, not `WebPki`, is the trust source, and
  `cargo tree -d` showing exactly one `core-foundation` and one `rustls`. Threshold: exact set. Local; PENDING-CI (`all`).
- **SC-039 (the Linux desktop choice, FR-074).** `sysproxy::tests::linux_desktop_choice_cases` (plan SI-36) (pure, every host):
  `Some("KDE")`, `Some("X-Cinnamon:KDE")` and `Some("KDE:plasma")` → KDE; `Some("GNOME")`, `Some("ubuntu:GNOME")`,
  `Some("XFCE")`, `Some("KDEPlasma")`, `Some("kde")`, `Some("")` and `None` → GNOME. The GNOME and KDE parser rows are
  SC-029's. The environment-read guard test admits exactly `XDG_CURRENT_DESKTOP`, `XDG_CONFIG_HOME` and `HOME` in
  `src-tauri/src/proxy_env.rs` (FR-074) (plan SI-36), and a planted `std::env::var("XDG_CURRENT_DESKTOP")` in another `src-tauri` file is reported
  (self-test). Threshold: every row. Local; PENDING-CI (`all`).
- **SC-040 (the macOS CFDictionary conversion, FR-075).** `sysproxy_cf::tests::from_cf_proxies_cases` (compiled and run on
  the macOS leg only, dictionaries built with `CFDictionary::from_CFType_pairs`): string, number (CFNumber) and boolean
  (CFBoolean) values convert to the expected `ScValue`s under the keys of FR-049 (`HTTPSEnable`, `HTTPSProxy`,
  `HTTPSPort`, `ExceptionsList` as a CFArray of strings, `ProxyAutoConfigEnable`, `ProxyAutoConfigURLString`,
  `ProxyAutoDiscoveryEnable`); a missing key and a value of the wrong CF type are skipped, not a panic. The macOS target
  table assertion of FR-075 runs on every leg. Threshold: every row. Not runnable on this Windows host (the module is
  `cfg(target_os = "macos")`); PENDING-CI on the macOS leg (`cargo.macos`).

- **SC-041 (Windows: the OS evaluates the PAC, FR-092, FR-067, R-10, R-15).** All with an injected `PacHost` whose
  OS-evaluation method returns canned answers (no WinHTTP call: the real call is in no test source, FR-087) and loopback fixture
  servers, so they run on every host. `http::tests::os_pac_walk`: the injected answer `[PROXY 127.0.0.1:<B1>, PROXY
  127.0.0.1:<B2>, DIRECT]` behaves exactly as SC-034's `pac_list_walk` (B1 refusing, B2 serving; each entry at most once; all
  failing gives `E_FEED_OFFLINE` and `check` returns the seeded cache as `Cached`); an answer of `DIRECT` with `proxy: Some(B)`
  reaches the target directly and B records nothing; the host is asked once per request hop and the manager requests no PAC
  URL from any server (a PAC fixture server records zero requests). `http::tests::os_pac_failure_cases`: an injected failure
  (no script found, an error, a timeout) and an unusable answer give one `warn!` (`E_PAC_FETCH`, `E_PAC_SCRIPT` or
  `E_PAC_RESULT`; the test injects one failure of each code, `E_PAC_SCRIPT` being WinHTTP's
  `ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT`) (plan SI-40), the request
  goes through the static proxy when `proxy: Some`, else direct, and `check` returns `Fresh` with `error: None`; an injected
  call that never returns is abandoned and `get` returns within the total timeout plus 1 s with no thread killed.
  `pac::tests::winhttp_answer_cases` (pure, over `WinHttpGetProxyForUrlEx`'s ordered entry list, R-16) (plan SI-38):
  `[Direct]` → `DIRECT`; `[Http a:8080, Http b:3128]` → [http a:8080, http b:3128] in order; `[Http a:8080, Direct]` →
  [http a:8080, direct]; `[Https p:3128]` → https p:3128; `[Socks s:1080]` → socks4 s:1080; `[Http a:0]` → http a:80; an
  `Other(3)` entry, an empty host and a host `a;b` are skipped with one `warn!` `E_PAC_RESULT` each; an empty list →
  `E_PAC_RESULT`. The SC-038 `cargo tree` row proves the engine is not linked on
  Windows. Threshold: every row. Local; PENDING-CI (`all`).
- **SC-042 (PAC DNS bounds, FR-094, R-12).** `pac::tests::dns_bound_cases` over an injected lookup function (no real DNS):
  a lookup that never returns is "unresolvable" after `PAC_DNS_WAIT` = 1 s (the call returns within `PAC_DNS_WAIT` + 1 s), its
  worker is not killed (it completes later without effect) and the next distinct name still resolves; the 17th distinct name is
  unresolvable with no lookup call and exactly one `warn!`; the same name asked twice calls the lookup once, a failed answer
  included; an IP literal makes no lookup call. Through the engine: a script that calls `dnsResolve` on 20 distinct hanging
  names returns within `PAC_EVAL_BUDGET` + 1 s with the process alive. Threshold: every row. `cfg(not(windows))`: not runnable
  on this Windows host, PENDING-CI on the macOS and Linux legs.
- **SC-043 (PAC local time, FR-095, R-13).** `pac::tests::pac_clock_cases` (pure, every host) with an injected `PacClock` at
  2026-10-31 23:30:00 UTC (a Saturday) and offset +2 h: without `"GMT"` `weekdayRange("SUN")` is true and `timeRange(1, 2)` is
  true; with `"GMT"` `weekdayRange("SAT")` is true, `weekdayRange("SUN")` false and `timeRange(1, 2)` false; an unknown offset
  uses the UTC fields and two calls in one session emit exactly one `warn!`. `sysproxy::tests::parse_utc_offset_cases`:
  `+0130` gives 5400, `-0800` gives -28800, `+0000` gives 0, and the empty string, `garbage` and `+2500` give none.
  `sysproxy::tests::offset_range_cases`: a CFTimeZone-style offset within ±14 h rounds to whole seconds, one beyond it or not
  finite is none. The real macOS and Linux readers are in no test source (FR-087). Threshold: every row. Local; PENDING-CI
  (`all`).
- **SC-044 (the RustSec checklist sentence, FR-093, R-15).** Compliance records a text search of `specs/Krate-manager-roadmap.md`
  after the build: the Phase 12 release checklist holds one line naming `rustsec.org`, `rquickjs`, `rquickjs-core` and
  `rquickjs-sys` and a pin bump on an advisory, and the phase's roadmap diff touches only the lines FR-090 permits; and
  `git diff --numstat -- specs/krate-manager/probes/results.md` is `1 0` (one added row, no removed line, FR-090) (plan
  SI-41). Threshold: exact; a recorded manual search, no test.

---

## Safety

- **Injected roots and URL.** The only filesystem path Phase 5 writes is `<state_dir>/feed-cache.json` (and its temp
  file beside it), reached through `ctx.roots.state_dir()`; tests build `AppContext` from `fresh_temp_roots()` (Phase 4
  FR-020), so no test can write outside a fresh temp dir. The feed URL and the HTTP configuration are `AppContext`
  fields whose production values are built only in `src-tauri` setup (FR-060, FR-061); core names no feed host
  (`injection.rs:12-20`, still enforced).
- **The network.** Tests use `UrlPolicy::LoopbackOnly`, which refuses every hop that is not the IP literal `127.0.0.1` or
  `[::1]` before a socket is opened (FR-042, SC-012), and a `None` proxy except in the SC-030 tests whose proxy is the
  loopback fixture server (FR-042, FR-087; D6), so no test reaches github.com,
  api.github.com, krateaudio.com or any other host. The live test is compiled only under `live-network` and is also
  `#[ignore]`d (FR-072, SC-019). The production policy appears in no test that could connect (FR-087), and no code
  outside `http.rs` and the test-only `fixture_server.rs` names a socket type (FR-081). Production uses `HttpsOnly` on
  every redirect hop (FR-043, `next_hop`, SC-012 `next_hop_cases`), `ureq`'s own
  `https_only` included as a second check. No code path names `api.github.com` (SC-015, SC-016).
- **PAC scripts (R-1).** A PAC script is untrusted input that decides only routing. On macOS and Linux it runs in an engine whose world holds
  the PAC helper functions and nothing else (no file, network, process or environment access, FR-064), under a
  1 MiB size bound, a 2 s evaluation bound and the engine's memory limit (FR-065, FR-068); it is fetched always direct, with
  no userinfo, and a failure falls back to the static proxy or direct, never to a hang or an error (FR-068). A PAC (or a
  WPAD answer) can send the feed request to a proxy of its choosing, but the feed is still fetched over HTTPS against the OS
  trust store (D4, FR-043) and the downloads are signature-bound in Phase 7, so a hostile PAC can deny service, not forge a
  release. On macOS and Linux the engine is C code (QuickJS-ng) in the manager process with no OS sandbox; that residual risk is accepted there, where auto-detect is off by default, and is mitigated by the RustSec check (FR-093). On Windows the OS runs the script in WinHTTP and the manager neither fetches, size-bounds nor evaluates it (FR-092), so the engine risk does not exist there. Tests fetch a PAC only from loopback fixture servers and call no real discovery, OS PAC reader or OS PAC evaluator (FR-087).
- **What is verified before anything is used.** The feed document is validated as a whole against the Phase 1 schema
  and code rules before any field reaches the model (D1, FR-020); install names reach later phases only as `PlainName`
  values that passed `check_install_values` (FR-026), so no feed string becomes a path unchecked. Nothing is downloaded,
  extracted or executed in this phase; the signature text is parsed structurally and verified cryptographically only in
  Phase 7.
- **Resource limits.** The feed body is capped at 4 MiB of **decoded** bytes by a `take(MAX_FEED_BYTES + 1)` over the
  decoding reader (FR-043, FR-050), so a gzip body that is small on the wire cannot expand past the cap (`ureq`'s own
  limit counts wire bytes and is not used, External facts; SC-012's `Reply::Gzip` cases prove it); connect and total
  timeouts are bounded (FR-041); at most five redirects (FR-043).
- **The elevated helper.** Untouched: still Phase 4's stub.
- **The webview.** Unchanged: no new command, capability or CSP entry (FR-062).
- **What tests must never touch.** The real state dir (`%LOCALAPPDATA%\com.krateaudio.krate-manager`, `~/Library/Application
  Support/com.krateaudio.krate-manager`, `$XDG_DATA_HOME/com.krateaudio.krate-manager`) and its `feed-cache.json`; any
  plug-in or preset folder, `ProgramData`, the registry; any non-loopback network address. No test launches the app.

---

## Edge cases

| Case | Behaviour | Where |
|---|---|---|
| Interrupted download of the feed (connection drops mid-body) | `E_FEED_BODY`; the cached copy is returned flagged stale; the cache is untouched | FR-044, FR-054, SC-010, SC-012 |
| Disk full / write error while saving the cache | The fresh feed is still returned; `cache_error` `E_FEED_CACHE_IO { step }`; the previous cache file is intact | FR-052, SC-013 |
| Cache file locked by another process on Windows (antivirus) | The persist step fails → `E_FEED_CACHE_IO { step: persist }`, previous cache intact | FR-052 |
| File locked by a running DAW (Windows) | Not applicable: nothing is installed | Phase 10 |
| Partial install | Not applicable | Phases 9-10 |
| Downgrade attempt: the feed (stale edge copy, a rolled-back release, a replayed old document) names a version older than the installed one | `UpdateState::InstalledNewer`; an older version is never an update. The document itself is accepted (the feed is the authority; it carries no signature of its own); Phase 7's per-asset signatures bind the version they sign | FR-032, SC-008 |
| A fresh, valid feed names an older version than the cached copy | Accepted: it replaces the cache and the model at once (Q7); an update may appear and vanish between two checks | D8, FR-058, SC-031 |
| A failed check (offline, timeout, `5xx`) | One attempt; the cache fallback; no automatic retry; "check now" is the retry (Q8) | FR-059, SC-032 |
| The asset's `min_os` is above the running OS | `Availability::NeedsNewerOs { min_os }`; not replaced by another candidate; an unreadable running version never gates | FR-035, FR-037, FR-038, SC-027 |
| An unrelated OS trust-store CA (TLS-intercepting proxy) | Trusted through the OS store (D4); an untrusted certificate is `E_FEED_TLS`, cached copy stale (derived, not tested) | D4, FR-043, FR-044, SC-026 |
| A PAC URL or WPAD auto-detect in the OS settings (no environment proxy) | Honoured on all three OSes, per request hop: on Windows the OS (WinHTTP) fetches and evaluates the script; on macOS and Linux the script is fetched direct and evaluated in process by `rquickjs`; the ordered answer is walked inside one attempt (R-1, R-8, R-10, R-15) | D16, FR-048, FR-064 to FR-067, FR-069, FR-092, SC-028, SC-033 to SC-035, SC-041 |
| A PAC script that calls `Date`, an IPv6 `*Ex` helper or `convert_addr`, or a hostile or endless script (macOS, Linux) | The name is undefined: `E_PAC_SCRIPT`, one `warn!`, the static proxy or direct; a loop or a heap blow-up ends at the bound with the process alive (R-14) | FR-064, FR-068, SC-033, SC-036 |
| A PAC host lookup that hangs, or a local offset that cannot be read (macOS, Linux) | The lookup is abandoned after 1 s, at most 16 names per session, memoised; an unreadable offset is UTC with one `warn!` (R-12, R-13) | FR-094, FR-095, SC-042, SC-043 |
| A PAC that cannot be fetched, does not compile, loops forever, or answers nothing usable | One `warn!` (`E_PAC_FETCH` / `E_PAC_SCRIPT` / `E_PAC_RESULT`), then the OS static proxy if any, else direct; the check is not failed by it | FR-068, SC-036 |
| An environment proxy while the OS has PAC / WPAD, or `NO_PROXY` matching the host | The environment wins; no PAC is consulted; a `NO_PROXY` match is direct from every source | FR-048, FR-069, SC-028 |
| A contract-valid document with a value the model cannot hold (`"files": 1e300`, `min_os` `11.4294967296`) | Only that platform asset (or v1 entry) is dropped with one `warn!` `E_MODEL_RANGE` (or the rule's code); the document is accepted and cached whole | FR-029, SC-037 |
| A proxy that refuses the `CONNECT` | `E_FEED_OFFLINE`, cached copy stale | FR-044, SC-030 |
| Corrupt feed (truncated JSON, an HTML captive-portal page, a BOM, non-UTF-8) | `E_INPUT`; cache untouched; cached copy returned stale | FR-020, SC-004, SC-010 |
| Feed valid JSON but breaking the contract (wrong type, `..` in `install`, `/releases/latest/`, `http://` asset URL, `tar.gz` without `payload`, a duplicate bundle name) | `E_FEED_INVALID` with every finding; whole feed rejected (D1); cache untouched | FR-020, D1, SC-002, SC-010 |
| Offline (no DNS, connection refused, reset) | `E_FEED_OFFLINE`; the cached copy flagged stale, or no snapshot (tested through `Reply::Close` and `Reply::Garbage`; a refused port is not reproducible without a race, SC-011) | FR-044, FR-055, SC-011 |
| A server that accepts and never answers | `E_FEED_TIMEOUT` after the total timeout | FR-041, SC-011 |
| A captive portal answering `302` to `http://…` | `E_FEED_URL` (production refuses `http` on every hop) | FR-042, FR-043 (`next_hop`), SC-012 (`next_hop_cases`) |
| `schema_version` 1 (today's live feed) | Accepted on the v1 floor; every plugin listed with its version text, "not available through the manager" | D3, FR-023, SC-004 |
| `schema_version` 1 on a document that also carries v2 keys | v1 rules only; the v2 keys are ignored | D3, SC-004 |
| `schema_version` 2 | C1 rules, the full model | FR-020, SC-005 |
| `schema_version` 3 or higher | `E_FEED_FUTURE_SCHEMA`; cached copy stale; the message says a newer manager is needed (the manager's self-update endpoint is compiled in, roadmap lines 829-831, so the user can still update) | D3, SC-004 |
| `schema_version` absent, `"2"`, `2.0`, `0` | Absent → v1; `2.0` → v2 (JSON Schema integer); `"2"`, `0` → `E_FEED_SCHEMA_VERSION` | D3, FR-002, SC-004 |
| Bundles that still say `0.8.0` at top level | Not read in this phase. `reduce_class_version` turns the class `Version` (`1.0.4.0`) into `1.0.4` and `CoreVersion::parse("0.8.0")` reads the top-level value; Phase 6 chooses the class value (roadmap lines 560-565) | FR-030, FR-031, SC-008 |
| A class version with an unexpected form (`1.2.3.x`, five parts) | `None` → Phase 6 reports "version unknown" | FR-031, SC-008 |
| A v1 feed version with a prerelease suffix (`1.2.0-beta`, which today's generator could have published, roadmap line 199) | `version_text` kept, `version: None`, comparison `Unknown` | D9, FR-021, FR-032 |
| A plugin only on a platform key outside the five (`windows-aarch64`) | Ignored key; the entry is "not available through the manager" | FR-023, SC-006 |
| Intel Mac and a plugin built arm64-only | `NotForThisMachine` ("not available for this Mac") | FR-035, SC-006 |
| The manager's x64 build under Windows-on-Arm emulation | `Arch::native()` is the binary's own arch, `X86_64`, so `windows-x86_64` is used | D10 |
| A tenth plugin in the feed, or one removed | Ten or eight entries, no manager release | FR-027, SC-014 |
| A cache written during a `dev-feed` audition, then a production run | Ignored (another `source_url`) | D8, FR-053, SC-013 |
| A cache that a newer validator rejects (a manager update tightened a rule) | Ignored, overwritten by the next accepted fetch | D8, FR-053, SC-013 |
| Two checks at once | Each writes a complete file; last rename wins; a check that loses the rename race reports `E_FEED_CACHE_IO { step: persist }` and leaves no temp file | FR-057, SC-024 |
| "Check now" right after a release | Sends `no-cache`; the Pages edge may still serve a copy up to 600 s old (measured); Phase 6 shows the ten-minute note (Q2) | D7, FR-051, FR-090, SC-009 |
| A server that is not TLS on an `https` URL | `E_FEED_TLS` (the `rustls::Error` downcast); cached copy stale | FR-044, FR-070, SC-012 |
| `HTTPS_PROXY` / `ALL_PROXY` / `NO_PROXY` set, or an OS proxy configured | Honoured: read in `src-tauri` setup and the OS reader, resolved and injected as `HttpConfig.proxy` / `HttpConfig.pac`; core reads no environment; tests use `None` | D6, FR-046, FR-048, FR-049, FR-063, SC-028 to SC-030 |
| A huge body (misconfigured server, decompression bomb) | Stops after `MAX_FEED_BYTES + 1` decoded bytes, `E_FEED_TOO_LARGE`; exactly 4 MiB decoded is accepted | FR-043, FR-050, SC-012 |
| A `notes_markdown` over 14,000 bytes | Accepted and carried (`notes-14000.json` is an `accept` row, parsed by the client too) | SC-002 |
| A contract-invalid `prerelease` sibling on one plugin | Its findings are dropped and logged (one `warn!` each); the feed is accepted and stable updates are unaffected (Q1) | D1, FR-024, FR-056, SC-010 |
| A feed pattern whose meaning differs between ECMA-262 and Rust `regex` (`\s`: U+FEFF is ECMA whitespace, U+0085 is Rust whitespace; `.`: ECMA excludes CR, U+2028, U+2029, Rust only LF) | `ecma_pattern_to_rust` rewrites `\s` and `.` to the ECMA sets before compiling, so the verdict equals Ajv's in both directions; any other divergent escape is refused at compile time | FR-002, SC-025 |

---

## New components

| Path | Purpose |
|---|---|
| `apps/krate-manager/crates/core/src/contract.rs` + `contract_tests.rs` | The schema interpreter over the embedded C1 / v1 schemas, the ported code rules, `parse_minisig_text`, `parse_trusted_comment`, `Finding`, `ContractError` (FR-001 to FR-008) |
| `apps/krate-manager/crates/core/src/feed.rs` + `feed_tests.rs` | `parse`, the typed model (`Feed`, `PluginEntry`, `StableRelease`, `PlatformAsset`, `InstallFacts`, `PlainName`, `AssetUrl`), `check`, the cache, `FeedError` (FR-020 to FR-028, FR-050 to FR-057) |
| `apps/krate-manager/crates/core/src/version.rs` + `version_tests.rs` | `CoreVersion`, `reduce_class_version`, `compare` / `UpdateState`, `Arch`, `platform_candidates`, `resolve` / `Availability` (FR-030 to FR-036) |
| `apps/krate-manager/crates/core/src/http.rs` + `http_tests.rs` | `UrlPolicy`, `HttpConfig`, `check_url`, `next_hop`, `get` with self-followed redirects and the decoded-byte cap, `config_for_dev_url`, the `ureq` / `rustls` error mapping (FR-040 to FR-045) |
| `apps/krate-manager/crates/core/src/test_support.rs` (changed) | The shared capture logger `install_capture()` / `take_mine()` (FR-088); `context_tests.rs` loses its own `Capture` |
| `apps/krate-manager/crates/core/src/fixture_server.rs` + `fixture_server_tests.rs` | Test-only loopback HTTP server with scripted replies, recorded requests and the proxy role (`CONNECT`, then the tunnelled request served again; no absolute-form role, plan SI-2) (FR-070) |
| `apps/krate-manager/crates/core/src/sysproxy.rs` + `sysproxy_tests.rs`; `apps/krate-manager/src-tauri/src/os_proxy.rs` | The pure injected-input parsers per OS and the reader seams in core; the real readers in `os_proxy.rs` through `SystemProxyFromOs for NativePlatform`; `trait Platform` and `platform/{windows,macos,linux}.rs` unchanged (FR-049, SC-029) (plan SI-1) |
| `apps/krate-manager/src-tauri/src/proxy_env.rs` | The single `src-tauri` file that reads `HTTPS_PROXY` / `ALL_PROXY` / `NO_PROXY` (FR-048, FR-063) |
| `apps/krate-manager/crates/core/src/pac.rs` + `pac_tests.rs` | The `rquickjs` glue (`cfg(not(windows))`), `evaluate`, the pure PAC helper functions, `PacHost`, `WPAD_DNS_URL`, the result-list and WinHTTP-answer parsers, the PAC clock (FR-064, FR-066, FR-067, FR-092, FR-095) |
| `apps/krate-manager/crates/core/src/sysproxy_cf.rs` + `sysproxy_cf_tests.rs` (macOS only) | `from_cf_proxies`, the CFDictionary conversion; `core-foundation` 0.10.1 (FR-075) |
| The real per-OS proxy readers (a thin file per OS, the crate that holds them named by the plan) | `WinHttpGetIEProxyConfigForCurrentUser` with `windows-sys` 0.61.2; the SystemConfiguration dictionary with `system-configuration` 0.8.0; `gsettings` and `kioslaverc` on Linux, chosen by `XDG_CURRENT_DESKTOP` (FR-049, FR-073, FR-074) |
| `apps/krate-manager/crates/core/tests/roster.rs` | Source scan for plugin names and `api.github.com` (FR-082) |
| `apps/krate-manager/crates/core/tests/live_feed.rs` | The opt-in live test, feature `live-network`, `#[ignore]` (FR-072) |
| `apps/krate-manager/crates/core/Cargo.toml` (changed) | `ureq`, `rustls`, `base64`, `regex` dependencies; `flate2` dev-dependency; `live-network = ["test-support"]` feature (plan SI-7) (FR-040, FR-072, FR-080) |
| `.github/workflows/manager-ci.yml` (changed) | `tools/feed-contract/**` path filter; step "roster absent" (FR-083, FR-084) |

---

## Traceability

| Roadmap statement (line) | FR / SC |
|---|---|
| Fetch `versions.json` from the injected URL (production `https://krateaudio.com/versions.json`) over HTTPS, HTTP only for the local fixture server (532) | D5, D11, FR-041 to FR-043, FR-050, FR-060, FR-061, SC-009, SC-012, SC-018 |
| Parse schema 1 and 2, ignore unknown keys (533) | D3, FR-020, FR-022, SC-004, SC-005 |
| Validate every v2 entry against the Phase 1 contract (533) | D1, D2, FR-001 to FR-007, SC-002, SC-003, SC-020 |
| Keep the last good copy in the state dir for offline use (534) | D8, FR-052 to FR-055, SC-010, SC-011, SC-013 |
| A manual "check now" bypasses caches (Pages edge cache ~10 minutes) (534-535) | D7, FR-051, FR-090, SC-009. **Departure, ruled 2026-10-30 (Q2, option A):** the edge ignores request directives and query strings (measured), so "check now" is an unconditional fetch plus `no-cache` headers, `EDGE_MAX_AGE_SECS` is exposed, and the roadmap lines are restated in this phase's build (FR-090) |
| SemVer compare (536) | D9, FR-030, FR-032, SC-008 |
| Platform-key resolution: native key, then `darwin-universal` (536-537) | D10, FR-033 to FR-036, SC-006, SC-007 |
| Four-part class versions reduced to three parts (`check-bundle.js:52`) (537) | FR-031, SC-008 |
| Local fixture HTTP server; opt-in live test, ignored, never in a gate (538) | D12, D13, FR-070 to FR-072, SC-019, SC-021 |
| Fixture feeds (Phase 1 set, read in place), each to its result through the client (accept, accept-without-platforms, or a named rejection); a rejected feed never replaces the last good copy (541-545) | FR-071, SC-002 (`c1_cases_reproduce` and `accept_rows_parse_through_the_client`), SC-003 (`gen_feed_outputs_parse`), SC-004, SC-010 |
| Phase 3's one-v2 / eight-v1 feed in Phase 5's list (451-453, 542) | SC-002 (row `Phase3/one-v2-eight-v1`), SC-014 |
| A `prerelease` sibling ignored in v1 (543; Q9, 32) | FR-024, SC-005 (left out of the model); a bad sibling never rejects the feed (Q1, option B: D1, FR-024, SC-010) |
| Offline: cached feed flagged stale (546) | FR-055, SC-011 |
| Roster: the plugin list is exactly the feed's keys; ten and eight entries with no code change; a grep of the built core and UI for the nine names (547-549; ROSTER, 33, 1001-1003) | D14, FR-027, FR-082, FR-083, SC-014, SC-015, SC-016 |
| No code path names `api.github.com`; a test greps the built core (550; 141-143) | FR-082, FR-083, SC-015, SC-016 |
| Does NOT read the disk for plugins, download, or render UI (552) | Non-goals, FR-062, FR-090 |
| HTTP client base joins `AppContext` in Phase 5 with its production value built in setup (476-479) | D11, FR-060, FR-061, SC-018 |
| "Not available for this Mac" via Phase 5's resolution (818-821) | FR-035, SC-006 |
| `min_os` compared with the running OS (CONTRACT.md:66; no roadmap line named it, placed in Phase 5 by Q3) | D10, FR-035 to FR-038, FR-090, SC-027 |
| Trust roots, proxy support, last-good-copy replacement, retry policy (no roadmap line; Q4, Q5, Q7, Q8) | D4, FR-040, FR-043, SC-026; D6, FR-046, FR-048, FR-049, SC-028 to SC-030; D8, FR-058, SC-031; FR-059, SC-032 |
| PAC and WPAD in v1 on all three OSes, OS readers, `ureq` features, macOS-only core dependency, per-asset drop (no roadmap line; Clarifications R-1 to R-7, 2026-10-31) | R-1: D16, FR-048, FR-049, FR-059, FR-064 to FR-069, SC-028, SC-033 to SC-036; R-2, R-3, R-5: FR-040, FR-073, SC-038; R-4: FR-074, SC-039; R-6: FR-075, SC-040; R-7: FR-029, SC-037; R-8: FR-073, FR-080, SC-038; R-9: FR-066, FR-092, SC-035, SC-041; R-10: FR-067, FR-092, SC-034, SC-041; R-12: FR-094, SC-042; R-13: FR-095, FR-074, FR-075, SC-043; R-14: FR-064, SC-033; R-15: FR-064, FR-092, FR-093, FR-073, FR-090, SC-041, SC-044 |
| Cross-cutting: never touch the real system; no test calls github.com or api.github.com (970-977) | D5, D6, D13, FR-081, FR-087, Safety, SC-012, SC-019, SC-021 |
| Cross-cutting: feed strings never become paths unvalidated (982) | FR-026 |
| Cross-cutting: cross-platform, PENDING-CI per leg (992-995) | SC-007, every PENDING-CI row |
| Cross-cutting: zero warnings; parallel tests (996-1000) | SC-001, FR-086, FR-088 (one process-wide capture logger), SC-021, SC-022 |
| Cross-cutting: additive (1004-1005) | The manager reads the feed only; Non-goals, FR-090 |

**Additions and departures.** Additions: the whole-document verdict (D1); interpreting the committed schemas instead of a
second definition (D2); `E_FEED_FUTURE_SCHEMA` / `E_FEED_SCHEMA_VERSION` (D3); the self-followed, per-hop-checked
redirects and the IP-literal loopback policy (D5); injected proxy support from the environment and the OS settings (D6);
the OS trust store (D4); the `min_os` comparison (D10); the `source_url`-keyed disposable cache (D8);
`PlainName` / `AssetUrl` (FR-026); the built-binary scan with a positive control (FR-083); the `tools/feed-contract/**`
path filter on `manager-ci.yml` (FR-084); the decoded-byte body cap (FR-043); the ECMA pattern translation (FR-002);
the production-policy and socket confinement guards (FR-081, FR-087). **Departure, ruled by the user 2026-10-30
(Clarifications Q2):** roadmap lines 534-535, "check now bypasses caches", is not achievable against the Pages edge as
measured on 2026-10-30; the outcome is restated in FR-090 and the ten-minute text is Phase 6's.

---

## Review notes

- **The `min_os` roadmap gap is closed** (Clarifications Q3): comparing `platforms.<key>.min_os` with the running OS
  (CONTRACT.md:66) was assigned to no roadmap phase; Phase 5 owns the pure comparison and `Availability::NeedsNewerOs`,
  Phase 6 reads the real running-OS version (FR-035 to FR-038, FR-090).
- **Review 2026-10-30, partial adoptions and rejections** (every other issue is applied as suggested in the row it
  names):
  - *SC-011, refused port:* the suggested "bind a `TcpListener`, keep it in scope without accepting" is **rejected**:
    `TcpListener::bind` listens, so the kernel completes the TCP handshake into the backlog and the client's request
    then waits for an answer, which is `E_FEED_TIMEOUT`, not a refusal. `std::net` has no bind-without-listen. The
    alternative the issue offers is adopted instead: the dropped-server case is removed and `Reply::Close` covers the
    no-response path (SC-011).
  - *FR-044 / FR-070, the non-TLS answer:* the suggested "close the connection at once on a first byte of `0x16`" is
    **not adopted as stated**: rustls reports an EOF during the handshake as a plain `UnexpectedEof` with no
    `rustls::Error` inside (rustls `conn.rs:725`), so the downcast would miss it and the case would read
    `E_FEED_OFFLINE`. The server instead answers the ClientHello with fixed plain-HTTP bytes, which rustls rejects as
    a record error wrapped in `InvalidData` (`conn.rs:697-701`). The downcast rule itself is adopted.
  - *FR-044, a certificate failure:* the issue's second option is taken: the private-CA edge-case row is dropped and
    D4 marks the `E_FEED_TLS` outcome as derived, not tested. A certificate test needs a TLS server and a certificate
    built at test time (a generator such as `rcgen` plus `rustls`'s server side as dev-dependencies) for a path that
    reaches the same downcast as the tested non-TLS case; the plan may add it if the clarify scan wants the evidence.
  - *FR-043, `ureq` version access:* FR-044's downcast needs `rustls::Error` by name and `ureq` 3.5.0 does not
    re-export `rustls` (`lib.rs:583-637`), so the resolution adds `rustls` as a direct, featureless dependency
    (FR-040, FR-080, FR-081). This is a consequence of the issue's resolution, not a separate choice.
  - *The `gzip` decompression bomb:* of the two suggested fixes, the decoded-stream cap (`as_reader()` +
    `take(MAX_FEED_BYTES + 1)`) is chosen over disabling gzip, because D4 keeps `ureq`'s `gzip` feature and the cap
    then holds whatever a server sends. `Reply::Gzip` carries bytes the test compresses with `flate2` (already in the
    lock), so the fixture server stays `std`-only as D12 requires.
  - *D7 / "check now" owner (minor):* both options are taken (Clarifications Q2, option A): the data
    (`feed::EDGE_MAX_AGE_SECS`, FR-051) and the roadmap sentence naming Phase 6 as owner (FR-090).
  - *SC-009 User-Agent:* raised twice (testability and reality lenses); one fix resolves both.
  - *FR-050 cache seam:* the issue's `check_with` is adopted; whether the cache reuses `settings::SettingsIo` or
    declares a `CacheIo` with the same steps is left to the plan, because `SettingsIo` also carries settings-only steps
    (`read_existing`, `settings.rs:219-232`).

---

## Clarifications

### Session 2026-10-30

The clarify scan's eight questions were put to the user and decided. Q-ids are that scan's, not the roadmap Decisions'
Q1-Q10. The decisions are carried in the D / FR / SC rows named in brackets; downstream stages consume only the FR / SC
lists.

- Q1, what a contract finding rejects: whole document, but findings under `/plugins/<Name>/prerelease` are dropped and
  logged (one `warn!` each) in `feed::parse`, so a broken beta sibling never blocks stable updates; the seven `sibling-*`
  rows are accepted by the client. [D1, FR-024, FR-056, SC-010]
- Q2, what "check now" becomes: an unconditional network fetch plus `Cache-Control: no-cache` and `Pragma: no-cache`;
  `feed::EDGE_MAX_AGE_SECS = 600` exposed for Phase 6's "a new release can take up to 10 minutes to appear" text; roadmap
  lines 534-535 restated in this phase's build. [D7, FR-051, FR-090, SC-009]
- Q3, who compares `min_os`: Phase 5 adds the pure comparison against an injected running-OS version (macOS product
  version, Linux glibc version; Windows major.minor[.build]) and `Availability::NeedsNewerOs { min_os }`; `resolve`
  takes the running version as a parameter; Phase 6 reads the real value through `Platform` and owns that roadmap
  sentence. [D10, FR-021, FR-035, FR-036, FR-037, FR-038, FR-090, SC-006, SC-007, SC-027]
- Q4, TLS trust roots: `ureq`'s `platform-verifier` feature with `RootCerts::PlatformVerifier`, the OS trust store on all
  three OSes; the dependency set and the `cargo tree` record change; the plan verifies which `reqwest` / `rustls`
  features `tauri-plugin-updater` uses (Phase 11) and records whether they agree. [D4, FR-040, FR-043, FR-044, FR-080,
  SC-017, SC-026]
- Q5, proxy: honour `HTTPS_PROXY` / `ALL_PROXY` / `NO_PROXY` (read in `src-tauri` setup, core reads no environment) and
  the OS proxy settings (Windows Internet settings, macOS SystemConfiguration, GNOME / KDE) through a per-OS reader
  behind `Platform` (three implementations, each tested with injected inputs); the result is injected as
  `HttpConfig.proxy`, always `None` in tests that do not test the proxy; the fixture server gains `CONNECT` handling for
  the proxy-path test; the config guard gains the proxy assertions; the plan states the precedence between environment
  and OS settings and the handling of PAC / WPAD, and leaving PAC / WPAD out is a precondition ruling for the user. (Decided 2026-10-31: R-1 below, PAC and WPAD are in.)
  [D6, FR-041, FR-042, FR-043, FR-046, FR-048, FR-049, FR-061, FR-063, FR-070, FR-080, FR-087, SC-018, SC-028, SC-029,
  SC-030]
- Q6, how C1 is enforced in Rust: an interpreter over the embedded committed JSON Schemas (`include_str!`) plus the
  hand-ported `contract.js` code rules, Ajv-equivalent findings and the ECMA pattern translation; parity tests over the
  90 `cases.json` rows and the gen-feed outputs. [D2, FR-001, FR-002, FR-003, FR-004, SC-002, SC-003, SC-025]
- Q7, a fresh valid feed naming a lower version than the cache: the fresh document is authoritative; it replaces the cache
  and the model at once, rollbacks included. [D8, FR-050, FR-058, SC-031]
- Q8, retry policy: one attempt per check, then the cache fallback; the user's "check now" is the retry. [D8, FR-050,
  FR-059, SC-011, SC-032]

### Session 2026-10-31

The plan's seven precondition rulings (R-1 to R-7, plan §0.3) were put to the user and decided. R-ids are the plan's. The
decisions are carried in the D / FR / SC rows named in brackets; downstream stages consume only the FR / SC lists. Numbers
the decision needs but the user did not state (the 1 MiB PAC size bound, the 2 s evaluation bound, the default proxy ports,
the WPAD candidate rule) are the spec's own choices to implement the decision and are open to the next challenge.

- R-1, PAC and WPAD: **in v1 on all three OSes** (the user rejected "leave out" and "Windows only"). The manager bundles a
  JavaScript engine and evaluates PAC scripts itself on Windows, macOS and Linux (superseded for Windows by R-15 below: the OS evaluates there). A PAC URL from the OS settings (Windows
  `AutoConfigURL`, macOS `ProxyAutoConfigURLString`, GNOME `autoconfig-url`, KDE `Proxy Config Script`) is fetched and
  evaluated; WPAD auto-detect (Windows auto-detect, macOS `ProxyAutoDiscoveryEnable`, GNOME / KDE auto mode) discovers the
  PAC URL. `FindProxyForURL`'s result list (`PROXY` / `HTTPS` / `SOCKS` / `DIRECT`) is honoured in order within the
  one-attempt rule. Environment variables still win over OS settings. [D6, D16, FR-048, FR-049, FR-059, FR-064, FR-065,
  FR-066, FR-067, FR-068, FR-069, FR-073, FR-081, FR-087, SC-028, SC-029, SC-033, SC-034, SC-035, SC-036, SC-038]
- R-2, Windows reader: `windows-sys` 0.61.2 (`WinHttpGetIEProxyConfigForCurrentUser`), features `Win32_Foundation` and
  `Win32_Networking_WinHttp`, as recommended. [FR-049, FR-073, FR-081, SC-029, SC-038]
- R-3, macOS reader: `system-configuration` 0.8.0 + `system-configuration-sys` 0.6.0, as recommended. [FR-049, FR-073,
  FR-081, SC-029, SC-038]
- R-4, Linux: GNOME and KDE, chosen by `XDG_CURRENT_DESKTOP`, as recommended. [FR-049, FR-074, SC-029, SC-039]
- R-5, `ureq`: default features kept plus `platform-verifier` and `socks-proxy`, as recommended. [D4, FR-040, FR-042,
  FR-046, FR-073, SC-026, SC-038]
- R-6, `core-foundation` 0.10.1 as a macOS-only dependency of `crates/core` holding `sysproxy_cf::from_cf_proxies`, tested on
  the macOS leg, as recommended. [FR-049, FR-073, FR-075, FR-085, SC-040]
- R-7, a contract-valid document the model cannot fully hold: drop only the affected asset (or v1 entry) with one `warn!`
  naming its code and path; the verdict is the contract's, as recommended. [FR-029, FR-056, SC-037]

### Session 2026-10-31, part 2 (the open rulings R-8 to R-15)

The plan's open rulings R-8 to R-10 and R-12 to R-15 (plan §0.3; R-11 was withdrawn by the plan's challenge round 2) were put to
the user and decided. R-ids are the plan's. The decisions are carried in the FR / SC rows named in brackets; downstream stages
consume only the FR / SC lists. The plan's "open" rulings are encoded here; this spec had no separate open-clarifications
section to delete.

- R-8, the PAC engine: `rquickjs` 0.14.0 (QuickJS-ng), default features only, `rust-alloc` barred; per R-15 used on macOS and
  Linux only (a `cfg(not(windows))` dependency). [FR-073, FR-080, FR-081, FR-068, SC-038]
- R-9, WPAD discovery: Windows asks the OS (WinHTTP's own DHCP-then-DNS rules); macOS and Linux try only
  `http://wpad/wpad.dat` completed by the resolver's search list; no devolution, no DHCP on macOS / Linux (Chromium's rule).
  [FR-066, FR-092, SC-035, SC-041]
- R-10, the answer list: walked inside the one attempt, each entry at most once, advancing only on no-response, bounded by the
  chain deadline. [FR-067, FR-092, SC-034, SC-041]
- R-12, PAC DNS: bounded lookup on a helper thread, abandoned (never killed) after `PAC_DNS_WAIT` = 1 s, at most 16 distinct
  names per session, memoised (macOS and Linux). [FR-094, SC-042]
- R-13, local time: the OS local offset per session (macOS CFTimeZone, Linux `date +%z` through the bounded runner); unreadable
  is UTC plus one `warn!`. On Windows no offset is read: the OS evaluates PAC, so `GetTimeZoneInformation` / `Win32_System_Time`
  is not needed (the `windows-sys` features stay two). [FR-095, FR-074, FR-075, FR-092, FR-073, SC-043]
- R-14, the script world: exactly the standard helper set plus a no-op `alert`; no `Date`, no IPv6 `*Ex` variants, no
  `convert_addr`; reversed ranges match only their endpoints. [FR-064, SC-033]
- R-15, where PAC runs: on Windows the OS evaluates PAC (`WinHttpGetProxyForUrl`, auto-detect and/or the configured
  AutoConfigURL per the R-2 reader's result), QuickJS is neither compiled nor linked there; macOS and Linux keep the in-process
  `rquickjs` evaluator; WPAD works on all three OSes; the RustSec advisory check on the `rquickjs` crates joins the Phase 12
  release checklist before every manager release. Measurement for the record: on the user's own (not fresh) Windows 11 profile,
  HKCU Internet Settings\Connections DefaultConnectionSettings flags byte = 0x09: auto-detect ON, no PAC URL, no manual proxy
  (read-only, 2026-10-31). [FR-064, FR-065, FR-068, FR-073, FR-080, FR-081, FR-085, FR-087, FR-090, FR-092, FR-093, SC-033,
  SC-034, SC-036, SC-038, SC-041, SC-044]

### Session 2026-10-31, part 3 (the open rulings R-16 and R-17)

The plan's open rulings R-16 and R-17 (plan §0.3, opened by R-15) were put to the user and decided, both as recommended.

- R-16, the Windows PAC call: `WinHttpGetProxyForUrlEx` (documented "always executes out-of-process"; returns the full proxy
  list for the FR-067 walk), not `WinHttpGetProxyForUrl`, whose documented fallback runs the script in the manager process.
  The plan's SI-38 carries the text change. [FR-092, FR-073, FR-066, SC-041]
- R-17, Windows credentials for a PAC server: `fAutoLogonIfChallenged` = `FALSE` on every call; a PAC server that asks for
  NTLM / Negotiate fails that call with the FR-068 fallback, no credentialed second call. [FR-092, SC-041]
