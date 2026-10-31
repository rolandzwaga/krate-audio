# Implementation Plan: Krate Manager Phase 5 — Feed Client and Version Model

**Spec:** `specs/krate-manager-phase5/spec.md` (DRAFT, reviewed: challenge round 1 and Clarifications Q1-Q8 of
2026-10-30)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 5 (lines 527-552); Decisions Q9 and ROSTER (lines 32-33);
Positioning (52-56); "Feed and artifact model" (128-148); Phase 4 injection note (476-479); Phase 3 fixture hand-over
(451-453); Phase 11 "not available for this Mac" (818-821); Cross-Cutting Constraints (968-1009)
**Status:** DRAFT plan, 2026-10-30, verified against `feat/krate-manager` at `a28b1181` (`git log --oneline -1`;
untracked: `specs/krate-manager-phase5/`). Challenge round 1 applied 2026-10-30 (18 issues; see Review notes).
**Revised 2026-10-31** for the user's precondition rulings R-1 to R-7 (§0.3, all decided). R-2 to R-7 confirm this
plan's recommendations and change nothing. **R-1 overturns "leave PAC/WPAD out":** PAC and WPAD are in v1 on all three
OSes with a bundled JavaScript engine; environment variables still win over OS settings (spec Clarifications
2026-10-31 R-1, `spec.md:1508-1514`), and among the OS settings the order is FR-069's WinHTTP order (WPAD, then the PAC
URL, then the OS static proxy as the PAC's failure fallback, `spec.md:616-621`). The
revision adds `core::pac` (§4.15), the PAC path through `http::get` (§4.5.1), PD-25 to PD-34, SI-24 to SI-35 and the
new rulings R-8 to R-15 that R-1 opens. It re-verified the repo at `a145271b` (spec revised 2026-10-31 with FR-029,
FR-064 to FR-069, FR-073 to FR-075 and SC-033 to SC-040, `spec.md:19-23`; read this session).
**Challenge round 2 applied 2026-10-31** (13 issues; see Review notes): SC-037 to SC-040 gained their own §7.1 rows,
PD-5 follows FR-069 (SI-23 and R-11 withdrawn), PD-22 drops a v1 entry with a bad `download_url` (FR-029(c)), the PAC
fetch no longer inherits `https_only`, `ScValue` gained `Bool`, the OS crates got token confinement, and the in-process
PAC engine's residual risk is stated and put to the user as R-15.
**Revised again 2026-10-31 for the rulings R-8 to R-15** (spec Clarifications 2026-10-31 part 2, `spec.md:1611-1638`, read
this session with FR-064 to FR-069, FR-073 to FR-075, FR-080, FR-081, FR-085, FR-087, FR-090, FR-092 to FR-095 and
SC-033 to SC-044). R-8 to R-10 and R-12 to R-14 confirm this plan's recommendations. **R-15 picks a design the plan did
not list:** on Windows the OS evaluates PAC and QuickJS is neither compiled nor linked (`rquickjs` moves to
`[target.'cfg(not(windows))'.dependencies]`); macOS and Linux keep the in-process evaluator. Reading Microsoft's
documentation for the call R-15 names (`WinHttpGetProxyForUrl`) showed that it falls back to running the script in the
calling process when it cannot reach the WinHTTP auto-proxy service, and that the flag which disables that fallback is
documented for Windows Server 2003 only. That is put to the user as the new **R-16**, and this plan is written to its
recommendation, `WinHttpGetProxyForUrlEx`, which is documented to always execute out of process (PD-35). R-15 also
opens **R-17** (whether the OS evaluation offers domain credentials to a PAC server). The revision adds PD-35, PD-36 and
SI-38 to SI-40; it rewrites PD-25, PD-27 to PD-32, §1.2, §2, §3, §4.1, §4.5, §4.5.1, §4.6, §4.10, §4.11.1, §4.13 to §4.15,
§5 to §10; it withdraws SI-32; and it takes the spec's test names (`script_world_cases`, `wpad_candidate_cases`,
`dns_bound_cases`, `pac_clock_cases`, `parse_utc_offset_cases`, `offset_range_cases`, SC-041's three).
**Lanes:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy live-network / clippy-guard / cargo test,
npm typecheck / lint / test, the version lint, `tauri build`, the roster scan by hand), the **pipeline lane**
(`node --test` over `tools/feed-contract/tests/`) and, since R-15, the local **L-WSL** lane (core clippy + tests on WSL
Ubuntu 24.04, the only local run of the macOS / Linux PAC engine, §8). No C++ file changes, so there is no `run-gate.js` /
`header-consumers.js` lane (§8).

**Build preconditions.** R-1 to R-10 and R-12 to R-15 are decided (2026-10-31; R-11 is withdrawn). The open rulings
R-16 and R-17 (§0.3, opened by R-15) and the spec edits of §0.2 must land before the build stage starts. Every open row has a recommended answer that this plan is
written against; a different answer changes only the sections named in its row.

---

## 0. What this plan adds to the spec

The spec fixes requirements and success criteria. This plan fixes file contents and module boundaries, the public Rust
API, the `ureq` feature list, the proxy precedence / PAC / bypass rules the spec delegates (FR-048 (a)-(c)), the OS
proxy readers and the crates they need (FR-049), the PAC engine, its sandbox and bounds, the PAC fetch, WPAD discovery
and the answer walk (FR-064 to FR-069, ruling R-1), the record of `tauri-plugin-updater`'s TLS stack (FR-040, SC-017),
the test names CI checks, and the local close procedure. Where the plan must correct a spec sentence it is listed in
§0.2 and repeated at its use site.

### 0.1 Plan decisions

| Id | Decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **The OS proxy readers live in `src-tauri`, the pure parsers in core.** New core module `crates/core/src/sysproxy.rs` (pure, compiled on every OS) holds `SystemProxy`, the raw-input types and the three parsers. New `src-tauri/src/os_proxy.rs` holds the thin real readers behind an extension trait `SystemProxyFromOs` implemented for `NativePlatform` (`fn system_proxy(&self) -> SystemProxy`), the same pattern Phase 4 used for `Roots::from_os()` (`src-tauri/src/os_roots.rs:7-33`). `lib.rs` still reads `NativePlatform.system_proxy()`. **Every step of a reader except the OS call itself lives in core, where it can be tested** (challenge round 1): `src-tauri` has no test binary (`src-tauri/Cargo.toml` `[lib] test = false`, `[[bin]] test = false`, Phase 4 D12a, `specs/krate-manager-phase4/spec.md:275-282`), so a test placed there would never run. Core gets the Windows UTF-16 read-and-free step (`sysproxy::take_win_ie`, over raw pointers, no `windows-sys` type), the macOS `CFDictionary` → `ScValue` conversion (`sysproxy_cf`, macOS only, R-6), the bounded child runner and the KDE path and file read (`sysproxy::run_bounded`, `kioslaverc_path`, `read_kde`), and the upper/lower environment merge (`http::ProxyEnv::from_vars`). `os_proxy.rs` keeps one OS call per OS plus the wiring. | A real reader cannot live under `crates/core/src/platform/`: every non-attribute string literal there must be in the 27-entry `PLATFORM_LITERALS` allow-list (`crates/core/tests/injection.rs:40-68`), the files may not name any of the 14 `FS_TOKENS` (`:22-37`, applied only to `platform/` and `roots.rs`, `:375-381`), so the KDE reader could not open `kioslaverc` there, and core may not read the environment at all (`clippy.toml` `disallowed-methods` on `std::env::var*`, D9): the core seams therefore take every input as an argument. The per-OS modules are also compiled only on their own OS (`platform/mod.rs:60-71`), so a parser placed there could not run "on every host" as SC-029 requires. | FR-049, SC-029 (see SI-1) |
| PD-2 | **`ureq` 3.5.0 with `default-features` kept and `features = ["platform-verifier", "socks-proxy"]`.** The default set is `["rustls", "gzip"]`, and `rustls` = `["rustls-no-provider", "_ring", "rustls-webpki-roots"]` (ureq `Cargo.toml.orig:21,25`). So `webpki-roots` stays compiled in but is never the trust source: `agent_config` always sets `RootCerts::PlatformVerifier` (SC-026). | Dropping `rustls-webpki-roots` needs `default-features = false` plus either the internal `_ring` feature ("INTERNAL FEATURES. DO NOT USE", `Cargo.toml.orig:56`) or a hand-installed crypto provider through the "unversioned" API (`tls/mod.rs:188-189`, `unversioned_rustls_crypto_provider`). `socks-proxy = []` adds no crate (`:28`) and without it a `socks5://` proxy is only warned about (`unversioned/transport/mod.rs:449-477`); FR-046 admits `socks5`. Ruling R-5. | FR-040 |
| PD-3 | **One deadline per chain.** `http::get` takes `Instant::now()` once; each hop's agent gets `timeout_global(Some(remaining))` and `timeout_connect(Some(min(connect_timeout, remaining)))`; `remaining == 0` before a hop is `E_FEED_TIMEOUT`. | `timeout_global` bounds one call (ureq `config.rs:761`); without a chain deadline five redirects could take 6 × 30 s. SC-032 requires the call to return "within the total timeout plus 1 s". | FR-041, FR-043 (mechanism) |
| PD-4 | **Proxy bypass is decided by core, per hop; `ureq`'s `NO_PROXY` matcher is never used.** `http::bypasses(&[String], host) -> bool` implements the PD-19 grammar; a hop whose host is bypassed gets an agent built from the same `HttpConfig` with `proxy: None`. The `ureq::Proxy` given to the agent never carries a no-proxy list. | `ureq`'s `NoProxyEntry` knows exact hosts, `*`/`.` suffixes and prefixes but no IP range (`proxy.rs:612-640`); SC-028 requires CIDR `10.0.0.0/8` and `<local>`. | FR-046 (mechanism) |
| PD-5 | **Proxy resolution rules (FR-048 (a)-(c)), stated:** (a) the environment first, then the OS settings: `HTTPS_PROXY`, then `https_proxy`, then `ALL_PROXY`, then `all_proxy` (an empty value counts as unset; this upper-before-lower merge is the pure core function `http::ProxyEnv::from_vars`, tested by `proxy_env_from_vars_cases`, so `src-tauri` only copies six raw values); `HTTP_PROXY` is not read (the feed is `https`). Only when none is set do the OS settings apply. (b) **PAC and WPAD are honoured on all three OSes (ruling R-1, decided 2026-10-31), in FR-069's WinHTTP order:** with no environment proxy, the OS settings apply as FR-069 (2) states (`spec.md:616-621`): a `pac_url` and/or `auto_detect` from the OS reader becomes `ProxyResolution.pac = Some(PacSource { auto_detect, url })`, and an OS static proxy found beside it is kept in `ProxyResolution.proxy` (FR-049 "A static proxy found beside a PAC source is kept in `proxy`"); `resolve_proxy` therefore returns **both halves** when both exist. In `http::get` the PAC then decides (WPAD candidates first, then `pac_url`, FR-066), its answer is final while it works (a `DIRECT` answer included, even with a static proxy configured), and on any PAC failure the static proxy, else a direct connection, is used (FR-068, §4.5.1). An OS static proxy with no PAC source is `{ proxy: Some(_), pac: None }`; nothing at all is direct. (c) The bypass list travels with its source and the environment's list always applies: an environment proxy uses `NO_PROXY`/`no_proxy` only; an OS proxy uses the OS bypass list **plus** `NO_PROXY`/`no_proxy`. A target host on `NO_PROXY` gives the empty resolution from every source, PAC included, and no PAC script is fetched (FR-048(c)); the OS bypass list applies to the OS static proxy only and the PAC script decides for itself (FR-067). | (a) is the spec's default (FR-048) and what `hyper-util` 0.1.21 does for `reqwest`'s `system-proxy`: environment values first, the system reader fills only empty ones (`src/client/proxy/matcher.rs:238-247`, `with_system` `if builder.https.is_empty()` at `:608`, `:712`). (b) is FR-069 (2) as written, with FR-048(b), FR-049 and SC-028 (`spec.md:1140-1141`, "with the OS static `proxy` kept beside it as the fallback"); the R-1 clarification (`spec.md:1508-1514`) rules only that the environment wins over the OS settings and states no order inside the OS settings. (Round 1 of this revision quoted an order "environment, then the OS static proxy, then PAC/WPAD" as R-1's text; the spec holds no such sentence, so SI-23 and R-11 are withdrawn, challenge round 2.) (c) `NO_PROXY` is an explicit user statement; applying it to an OS proxy too can only send fewer requests through a proxy, never more. | FR-048 (a)-(c), FR-069, SC-028 (as written) |
| PD-6 | **The cache declares its own `CacheIo` trait** in `feed.rs` with the FR-052 steps (`create_dir`, `create_temp`, `write`, `sync_temp`, `persist`, Unix `sync_dir`) plus `read`; it does not reuse `settings::SettingsIo`. | `SettingsIo` carries `read_existing`, a step the cache does not have (`settings.rs:221-232`), and its `StdSettingsIo` uses the settings temp prefix `.settings.json.` (`settings.rs:11`, `:249-253`); reusing it would change a Phase 4 file for no gain. Same by-value temp handling (Phase 4 PD-9). | FR-050 (the plan's choice) |
| PD-7 | **The fixture server's route table is mutable and keyed by path, and `CONNECT` keeps the connection.** `FixtureServer::set_route(path, Reply)` replaces a reply at the same port. After `Reply::ConnectOk`, the same connection is served again as a fresh connection (TLS byte detection first, then a request line answered from the route table). There is no absolute-form handling. | The cache is used only when `source_url` equals the feed URL (D8, FR-053), and the URL holds the port, so a seed-then-reject test must change the reply without restarting the server. `ureq` 3.5.0 tunnels **every** target through `CONNECT` when an `http`/`https` proxy is set (`unversioned/transport/connect.rs:38-58`, `config.rs:251-259`), `http` targets included, so it never sends an absolute-form request (SI-2). | FR-070, SC-030 (see SI-2) |
| PD-8 | **JSON integers in the model** go through `feed::json_u64(&Value) -> Option<u64>`: `as_u64()`, else an `f64` with a zero fractional part in `0..=2^53`. **A contract-valid value the model cannot hold never changes the verdict (ruling R-7, recommended):** a `payload.files` that `json_u64` refuses (`1e300`), or a `min_os` with a component past `u32` (`11.4294967296`), drops **that one platform asset** from the model with one `warn!` naming `E_MODEL_RANGE` and the RFC 6901 path; the document is still accepted and every other asset and plugin is kept. A dropped key is treated as absent, so reader rule 2 applies unchanged (a dropped `darwin-aarch64` falls to `darwin-universal` when present). `E_MODEL_RANGE` is a log code, not a `Finding`, and never reaches `E_FEED_INVALID`. | JSON Schema's `integer` is any number with a zero fractional part (spec External facts), so `"size": 2.0` is valid, but `serde_json::Value::as_u64` returns `None` for it. `size` is bounded by the schema (`maximum` 2147483647, `versions.v2.schema.json` `platform.size`), `payload.files` and `min_os` are not. Phase 1's Node validator and Phase 3's `dry_run` accept such a document, so turning it into `E_FEED_INVALID` would let a deployable feed block every manager's updates (D1 rejects the whole document) and break the verdict parity of FR-007 / SC-002. Dropping only the unrepresentable asset keeps parity and fails safe (that asset is not offered). | FR-021, FR-037 (see SI-16, R-7) |
| PD-9 | **An `E_INSTALL_DUPLICATE` is dropped by the sibling filter iff at least one of its two sides is a `prerelease` install**, whatever the key order. Core's duplicate finding carries both sides (`DuplicateSides { first, second }`, each a full RFC 6901 path). | The spec's prefix rule decides on `instance_path`, which `contract.js` puts on "the second plugin's field" in document order (`contract.js:466-478`). `serde_json`'s `Map` is a `BTreeMap` (bytewise order) without its `preserve_order` feature, so "second" can differ: Alpha's sibling clashing with Bravo's stable `install` is reported at `/plugins/Bravo/install/…` (kept, reject) in one order and at `/plugins/Alpha/prerelease/install/…` (dropped, accept) in the other. The manager never uses a sibling's install facts (Q9), so a clash with a sibling cannot misname a stable plugin's files; dropping it in both orders is the Q1 intent. | FR-004, FR-024, SC-010 (see SI-8) |
| PD-10 | **The interpreter follows Ajv's error-reporting conventions** (table in §4.2.2): `$ref`, `allOf`, `properties`, `patternProperties` and schema-valued `additionalProperties` report only their children's findings; `not`, `const`, `enum`, `type`, `pattern`, `minimum`, `maximum` report themselves; `required` / `dependentRequired` one finding per missing name at the object's path; a `false` schema reports `"false schema"`; a failed `then`/`else` adds `if`; a failed `propertyNames` reports the key's findings at the object's path plus `propertyNames`. `$defs` compile into an arena in two passes (names first), so `$ref` needs no recursion guard. | `cases.json` rows name exactly these pairs (`"/plugins pattern"` and `"/plugins not"` for `propertyNames`, `"... false schema"`, `"... dependentRequired"`, listed this session); the Phase 1 README defines a row as passing when its pair is **among** the findings (`fixtures/README.md:57-62`). | FR-001 to FR-003 (mechanism) |
| PD-11 | **`ecma_pattern_to_rust` is a one-pass tokenizer**: outside a class, `\s` → the ECMA set as a class and `.` → `[^\n\r\u{2028}\u{2029}]`; inside a class, `\s` → the same ranges without brackets; `\S \d \D \w \W \b \B` anywhere → `Unsupported`; every other escape and character is copied. The result is compiled with `regex::Regex::new` (Unicode default). | The committed schemas use `\s` only inside `[^\s]` (`$defs.url`), `.` only in `(\..*)?` (`$defs.reservedStem`), and otherwise explicit classes (read this session). | FR-002 |
| PD-12 | **`http::config_for_dev_url(url: &str, resolution: ProxyResolution, pac_host: Option<PacHostHandle>) -> HttpConfig`**: loopback host → `HttpConfig::loopback(CONNECT_TIMEOUT, TOTAL_TIMEOUT)` with the proxy, the PAC source and the PAC host dropped; any other URL → `HttpConfig::production().with_proxy(resolution.proxy).with_pac(resolution.pac).with_pac_host(pac_host)`. | FR-061 says both "neither carries a proxy or a PAC source" and "a non-loopback `dev-feed` URL gets the same proxy and PAC source as production", and FR-063 allows `.with_proxy(` / `.with_pac(` in no `src-tauri` file but `lib.rs` / `proxy_env.rs`; taking the resolution as an argument satisfies both. | FR-061, SC-018 (see SI-15, SI-27) |
| PD-13 | **`resolve_proxy` takes `target: &str`** and parses it; an unparsable target is the empty `ProxyResolution` (direct, no PAC) plus one `warn!`. | `src-tauri` has no `url` dependency and its `[dependencies]` set is fixed at five keys by `config_guard.rs:32-33`. | FR-048, FR-061 (see SI-6) |
| PD-14 | **`live-network = ["test-support"]`** in `crates/core/Cargo.toml`, and `crates/core/tests/live_feed.rs` starts with `#![cfg(feature = "live-network")]`. | The live test writes its cache into `fresh_temp_roots()`, which exists only under `cfg(any(test, feature = "test-support"))` (`lib.rs:10-11`); an integration test is not `cfg(test)` for the library. | FR-063, FR-072 (see SI-7) |
| PD-15 | **`schema_version` classification** (`feed::classify_schema_version`): absent → V1; a JSON number that is an integer by FR-002 (`as_u64`, or `as_i64`, or an `f64` with zero fraction) → `1` V1, `2` V2, `> 2` `E_FEED_FUTURE_SCHEMA { found }` (`found` saturates at `u64::MAX` for a huge float), `≤ 0` `E_FEED_SCHEMA_VERSION`; a fraction, a string, `null`, a bool, an array or an object → `E_FEED_SCHEMA_VERSION`. | SC-004's cases (`2.0` → v2; `0`, `-1`, `2.5`, `"2"`, `null` → `E_FEED_SCHEMA_VERSION`). | D3 (made concrete) |
| PD-16 | **`FeedError` and `HttpError` are `Clone`**: the cache-write variant keeps `Arc<std::io::Error>`, as Phase 4 PD-3 did for `StartupError` (`context.rs:24-35`). | `FeedCheck` carries two errors and Phase 6 will hand it to the UI layer. | FR-050 (shape) |
| PD-17 | **OS readers (R-2, R-3, R-4):** Windows `WinHttpGetIEProxyConfigForCurrentUser` through `windows-sys` 0.61.2 (already in `Cargo.lock`); macOS `SCDynamicStoreCopyProxies` through `system-configuration` 0.8.0; Linux GNOME through five `gsettings get` calls and KDE through `$XDG_CONFIG_HOME/kioslaverc` (default `~/.config/kioslaverc`), chosen by `XDG_CURRENT_DESKTOP`. Every child process is waited for at most 2 s in total and is never killed. **A child still running at the bound is handed to a detached reaper thread** (`std::thread::spawn(move \|\| { let _ = child.wait(); })`, `child` moved after its piped `stdout` handle is dropped): it is reaped whenever it exits, so it never stays a zombie; a late `gsettings` that writes to the closed pipe gets `EPIPE` / `SIGPIPE` and exits, and is then reaped. The bound and the commands are arguments of the core function `sysproxy::run_bounded` (PD-1), so the path is tested with `sleep` / `printf` on the Unix legs. The Linux PAC host's local-zone read (`date +%z`, PD-31, R-13) uses the same runner and bound. | §1.2 rows for each API; `hyper-util` 0.1.21 reads macOS through `system-configuration` (`matcher.rs:570-656`) and Windows through the same registry values WinHTTP reports (`:657-723`). The roadmap forbids any code path that closes or kills another process (Cross-Cutting, roadmap lines 984-988); a stuck `gsettings` is our own child, but not killing it keeps that rule mechanical. `std::process::Child` has no `Drop`: a dropped child keeps running, and one that exits without a `wait` stays a zombie (https://doc.rust-lang.org/std/process/struct.Child.html, "There is no implementation of Drop for child processes"), so dropping it un-waited (the round-0 plan) leaked a zombie per hung call. | FR-049 |
| PD-18 | **Bypass-entry grammar (one matcher for every source):** `*` (all); `<local>` (a host with no dot, Windows); an IP literal (exact); a CIDR `a.b.c.d/n` or `x::y/n`, IPv4 written short as macOS does (`169.254/16` = `169.254.0.0/16`); an IPv4 wildcard `10.*`, `192.168.*` (→ CIDR, as `hyper-util`'s `ipv4_wildcard_to_cidr`, `matcher.rs:660-683`); `*.suffix` and `.suffix` (any subdomain, not the bare domain); otherwise an exact host, ASCII case-insensitive. A `:port` suffix is removed before matching; an IPv6 literal in `[…]` is unbracketed. Separators: `,` (environment, KDE), `;` (Windows), list items (macOS, GNOME). | SC-028 and SC-029 rows; ureq's matcher semantics for `*`/`.`/`*.` (`proxy.rs:620-638`) kept so a `NO_PROXY` that works with `curl`-style tools works here. | FR-046, FR-048 (c) |
| PD-19 | **Redaction:** `ProxyConfig` implements `Debug` by hand and has a `Display` that both print the URL with any userinfo replaced by `***` (`http://***@proxy:3128`); no log line formats the raw `url` field. **Every type that holds a raw proxy or PAC string gets the same hand-written `Debug`:** `http::ProxyVars` (the six raw values), `http::ProxyEnv`, and `sysproxy::SystemProxy` (`pac_url` can carry userinfo; its `proxy` is already a `ProxyConfig`), and `http::PacSource` (its `url`). PAC URLs are logged through `http::pac_url_for_log(&str) -> String` = `redact_userinfo` with the query and fragment removed (FR-065, FR-068: no log line carries a PAC URL's query). One helper `http::redact_userinfo(&str) -> String` serves all five; it also handles a schemeless value (`u:p@host:3128` → `***@host:3128`), since environment values often have no scheme. `src-tauri`'s `ProxyInputs` derives `Debug` and is safe because its fields' `Debug` is. | FR-046, FR-056. `ureq` logs its own traffic at debug/trace only, and the production logger records Info and above (`src-tauri/src/log_sink.rs:15`, `:54`). | FR-046 (mechanism) |
| PD-20 | **Every new FR-087 / FR-081 / FR-063 / FR-082 scan reuses `injection.rs`'s lexer** (`strip_comments_and_literals`, `tokens`, `injection.rs:112-165`, `:314-337`): "in code" means after comments and literals are blanked. `roster.rs` is a separate integration test and gets its own small scanner (it searches raw text, comments included, as FR-082 requires). | One lexer, already self-tested by `injection_scanner_reports_planted_violations`. | FR-081, FR-082, FR-087 |
| PD-21 | **`asset_name` is a checked newtype `feed::AssetName(String)`**, no public constructor, built only by the model builder from `url::Url::parse(asset_url).path_segments().and_then(\|s\| s.last())` (never by splitting the raw string, so a query, a fragment or a `..` segment the URL parser normalises can never be the name). It must match `^[A-Za-z0-9][A-Za-z0-9._-]*$`, must not be `.` or `..`, and must not be a reserved stem (`RESERVED_STEM_RE`). A segment that fails drops that platform asset with one `warn!` naming `E_MODEL_ASSET_NAME` and `/plugins/<P>/platforms/<k>/url` (ruling R-7, same mechanism as PD-8). | The C1 `url` rule is only `^https://[^\s]+$` plus `not /releases/latest/` (`versions.v2.schema.json` `$defs.url`), and `contract.js` checks the asset regex only inside the trusted comment (`ASSET_NAME_RE`, `contract.js:131`; `TC_FIELD_OK.asset`, `:291-296`), never against the URL segment (reader rule 4 leaves that comparison to C3, `CONTRACT.md:125-126`). So a schema-valid feed can yield `""` (`https://h/a/`), `x.exe:ads` (an NTFS stream), `CON.exe`, or `%2e%2e`-style text. Phase 7 downloads into a staging dir by this name and Phase 10's helper runs the staged installer by file (roadmap lines 596, 722-731); the roadmap's "Feed strings never become filesystem paths unvalidated" (line 982) needs a type, as FR-026 gives install names. The reserved-stem rule is a client-only addition: `TC_FIELD_OK.asset` does not apply it, but `CON.exe` opened under a Windows staging dir names the console device. | FR-021, FR-025, FR-026 (see SI-18) |
| PD-22 | **A v1 entry is sanitised at the model boundary.** v1 acceptance stays (the in-editor checkers need it), but a v1 plugin key that fails the C1 `pluginName` rule (`PLUGIN_NAME_RE` `^[A-Z][A-Za-z0-9]{0,63}$` and not `RESERVED_STEM_RE`, `contract.js:124-130`, `isPluginName` `:203`) drops the entry from the model with one `warn!` `E_MODEL_V1_ENTRY` at `/plugins/<key>`; a v1 entry whose `download_url` is present and fails the C1 `url` rule is **dropped likewise** (the whole entry, one `warn!` `E_MODEL_V1_ENTRY` at `/plugins/<key>/download_url`), as FR-029(c) and SC-037 rule ("dropping that entry only", `spec.md:394-399`, `:1244-1245`). `download_url` is typed `Option<PageUrl>` (review round 3: its own checked newtype, never `AssetUrl`, because it is a release-page link with no hash or signature behind it, `.github/workflows/release.yml:485` `release_url: ${{ steps.release.outputs.url }}`, `tools/gen-feed.js:277` `download_url: plugin.versionJson.url`) and is `None` only when the key is absent; a v2 `download_url` always passes (it is `$ref: #/$defs/url`), so a v2 refusal is a builder defect `accept_rows_parse_through_the_client` catches. | The v1 floor schema allows any plugin key and any `download_url` string (`versions.v1.schema.json`, whole file: `additionalProperties` of `{version, download_url, release_notes}` strings). Without this, a feed served as v1 (or with no `schema_version`, PD-15) puts `file:///…`, `javascript:` or `http://` text and keys such as `..\x` into the model, while every v2 name and URL passed C1. A v2 key always passes `pluginName` and a v2 `download_url` is `$ref: #/$defs/url` (`versions.v2.schema.json:219`), so FR-027 and the stored URLs change only for v1 documents. Round 1 kept the entry with `download_url: None`; that contradicted FR-029(c) (challenge round 2) and is withdrawn. | FR-021, FR-027, FR-029 (see SI-19) |
| PD-23 | **A proxy the HTTP client cannot take fails closed.** `http::get` converts `config.proxy` once, before any socket opens, with `pub(crate) fn to_ureq_proxy(&ProxyConfig) -> Result<ureq::Proxy, HttpError>`; a refusal is `E_FEED_URL` (`HttpError::Url`, redacted URL, reason `"proxy not accepted by the HTTP client"`), never a direct agent. `agent_config` takes the converted value: `agent_config(&HttpConfig, Option<ureq::Proxy>)`. | `resolve_proxy` validates with the `url` crate; `ureq::Proxy::new` parses with `http::Uri` and percent-decodes userinfo as UTF-8, failing with `InvalidProxyUrl` otherwise (ureq 3.5.0 `src/proxy.rs:176-212`, `decode_userinfo` `:484-489`, read this session from https://static.crates.io/crates/ureq/ureq-3.5.0.crate). `http://%FF@127.0.0.1:1` passes `url::Url::parse` and fails `decode_userinfo`. The round-0 `and_then(to_ureq_proxy)` turned that into a silent direct connection, bypassing a proxy the user or an egress policy set. | FR-043, FR-046 (see SI-20) |
| PD-24 | **The schema compiler has a text entry point:** `pub(crate) fn compile_schema_text(text: &str) -> Result<Compiled, ContractError>`; the `OnceLock`s call it on the two embedded texts. `ContractError::Unsupported { pointer, keyword }`: `pointer` is the RFC 6901 pointer of the **schema object** holding the keyword (`""` = root), `keyword` the keyword's name. | FR-001's refusal of unknown keywords, unresolvable `$ref`s, other `type`s and uncompilable patterns needs a test over synthetic schemas; the committed schemas use only supported constructs, so neither `embedded_schemas_compile` nor the parity tests would see an interpreter that silently ignored an unknown keyword (D2, spec line 167). | FR-001 |
| PD-25 | **The in-process PAC engine (macOS and Linux only, rulings R-8 and R-15) is `rquickjs` 0.14.0 (QuickJS-ng, vendored C, compiled by `cc`) with `default-features` (`["std"]`) and no other feature, declared under `[target.'cfg(not(windows))'.dependencies]` of `crates/core/Cargo.toml`, so the Windows binary neither compiles nor links QuickJS** (on Windows the OS evaluates, PD-35). One `Runtime` + `Context` per PAC session (one `http::get` call), created and dropped on the calling thread; never `rust-alloc` (it turns `set_memory_limit` into a no-op, rquickjs-core `src/runtime/base.rs:151-162`), never `parallel`, `loader`, `dyn-load` or `futures`. Every item of `pac.rs` that names `rquickjs` carries `#[cfg(not(windows))]`; the Windows build of core is the mechanical proof (the crate is absent there, so an un-gated use does not compile on this box), and `cargo tree -p krate-manager-core -e normal --target x86_64-pc-windows-msvc` listing no `rquickjs*` crate is the recorded one (SC-038, §7.4). | It is the only candidate with both a wall-clock interrupt (`Runtime::set_interrupt_handler`, "If the provided closure returns `true` the interpreter will raise an uncatchable exception", `base.rs:120-128`) and a heap limit (`set_memory_limit`, `:151-162`), which FR-068 requires. Measured in a scratch crate this session (Windows, release, the workspace's `lto`/`strip`/`panic = "abort"` profile): `while(true){}` interrupted at 300 ms of a 300 ms deadline; `/^(a+)+$/.test('a'.repeat(40)+'b')` interrupted at 300 ms (libregexp calls the same handler, `quickjs/libregexp.c:2800-2802`); a doubling string hit a 16 MiB limit at once; the context evaluated `1+1` afterwards; the binary grew by 1,034,752 bytes (that growth now lands on the macOS and Linux binaries only, §4.1). `boa_engine` has only `RuntimeLimits` (loop-iteration, recursion, stack, backtrace: `src/vm/runtime_limits.rs:37-94` of 0.22.0), no heap limit and no interrupt, and 0.22.0 needs Rust 1.91 (`Cargo.toml:14`) against the workspace's 1.90 (`apps/krate-manager/Cargo.toml:9`). Spec FR-073(d) (`spec.md:792-796`) rules the target table. | FR-064, FR-068, FR-073(d) (R-8, R-15) |
| PD-26 | **The script's world is `Context::custom::<(Eval, RegExpCompiler, RegExp, Json)>`** (rquickjs-core `src/context/builder.rs:51-99`) plus the PAC helpers as native functions on the global object; no `Date`, `Promise`, `Proxy`, `MapSet`, `TypedArrays`, `Performance`, `WeakRef`, `DOMException` intrinsic, no module loader, no `std`/`os` module (rquickjs-sys compiles only `libregexp.c`, `libunicode.c`, `quickjs.c`, `dtoa.c`, `build.rs:143`; QuickJS-ng's `quickjs-libc` is not built). Measured: in that context `typeof` of `Date`, `require`, `std`, `os`, `fetch`, `setTimeout`, `print`, `console` is `"undefined"`. The helpers are pure Rust functions in `pac.rs` (`pac::helpers::*`, unit-tested without the engine) wrapped by `Function::new`; every helper that touches the machine goes through `PacHost` (FR-064). `Eval` is needed because `Ctx::eval` compiles through it (`src/context/ctx.rs:160`); a script calling `eval` reaches the same sandbox. | FR-064's "the standard PAC helper functions and nothing else of the machine"; `Date` would read the real clock outside `PacHost` (FR-087). The `*Ex` IPv6 variants, `convert_addr` and `Date` are out (R-14). | FR-064 (R-14) |
| PD-27 | **Bounds, all constants in `pac.rs` (compiled on every OS; the engine bounds act on macOS and Linux only):** `MAX_PAC_BYTES = 1 MiB` decoded (FR-065, the script size cap); `PAC_EVAL_BUDGET = 2 s` per `FindProxyForURL` call and per compile, enforced by the interrupt handler against `min(start + budget, chain deadline)`; `PAC_MEMORY_LIMIT = 32 MiB` (`set_memory_limit`); `PAC_STACK_LIMIT = 256 KiB` (`set_max_stack_size`, the QuickJS default, set explicitly, `base.rs:164-169`); `MAX_PAC_ANSWER_BYTES = 64 KiB` and `MAX_PAC_ENTRIES = 16` (a longer answer is `E_PAC_RESULT`; entries past the sixteenth are dropped with one `warn!`; both apply to the Windows OS answer too, through the shared parser, PD-36); `PAC_PHASE_BUDGET = 10 s`: on macOS and Linux for discovery plus fetch plus compile together, inside the chain deadline (the PAC fetch's own timeout: `min(10 s, remaining)`, so a slow PAC server leaves ≥ 20 s of the 30 s chain for the feed); on Windows the bound of one OS evaluation call per hop (`min(10 s, remaining)`, PD-35). A throw, an interruption, an out-of-memory or a stack overflow is `E_PAC_SCRIPT`; the runtime is dropped and the hop takes the fallback (FR-068). On Windows WinHTTP fetches and runs the script under its own limits, which the manager neither sets nor relies on (FR-092, `spec.md:642-644`). | FR-068 asks the plan to name the mechanism. The interrupt handler only runs between bytecodes and inside libregexp; a native helper is not interruptible, which is why every machine-touching helper is itself bounded (PD-29). The answer and entry caps bound what a garbage-returning script can make the walk do (16 entries × the connect timeout is still inside the chain deadline). | FR-065, FR-067, FR-068 (see SI-28, SI-29) |
| PD-28 | **(macOS and Linux; on Windows WinHTTP fetches the script, FR-092.) The PAC session is fetched and compiled once per `http::get` call, lazily before the first hop, always direct** (`proxy: None`, never a PAC answer), through the same agent builder (`agent_config(config, None, AgentPurpose::PacFetch)`, which differs from the feed's `AgentPurpose::Feed` only in `https_only(false)`: ureq's `https_only` refuses every `http` URI with `Error::RequireHttpsOnly` (§1.2, `config.rs` getters), so a PAC fetch built with the feed's `https_only(true)` under `HttpsOnly` would fail every `http` PAC URL, `pac::WPAD_DNS_URL` included; the scheme rule for the PAC fetch is `check_pac_url`, applied before the first socket and on every redirect hop) with the per-hop rule `check_pac_url` (FR-065) instead of `check_url`, the FR-043 redirect and status rules, and `take(MAX_PAC_BYTES + 1)`. `AgentPurpose::PacFetch` and the fetch code are `#[cfg(not(windows))]` (on Windows nothing constructs them, so an un-gated variant would warn `dead_code`). Nothing is cached across checks (the next check fetches again: FR-059's one fetch per check, and a changed PAC takes effect at the next check). Within one check the script text, the compiled context and the DNS answers (PD-29) are reused by every hop. `http` PAC URLs are accepted under `HttpsOnly` (FR-065): the security consequence is in §6. | FR-065, FR-059. Re-using the hop machinery keeps one agent builder (SC-026's `ureq_is_confined_to_http_rs` pins it). | FR-065 |
| PD-29 | **Every PAC helper that can block is bounded on its own (macOS and Linux, ruling R-12).** DNS (`dnsResolve`, `isResolvable`, `isInNet` with a name): the real host resolves through `http::std_resolve(name, wait)`, which parses an IP literal without DNS, else runs `(name, 0).to_socket_addrs()` on a spawned thread and waits `recv_timeout(min(PAC_DNS_WAIT = 1 s, eval deadline))`; an unanswered lookup is "unresolvable" and its thread is **abandoned, never killed**: it ends when the OS resolver returns. Per session at most `PAC_MAX_LOOKUPS = 16` distinct names are looked up (the rest are unresolvable, one `warn!`), each answer memoised (`pac::DnsMemo`). `myIpAddress`: `http::std_my_ip()` binds a UDP socket to `0.0.0.0:0`, `connect`s it to `198.51.100.1:80` (TEST-NET-2, RFC 5737; a UDP `connect` sends no packet, it only picks the route's source address) and returns `local_addr`, else `127.0.0.1` (MDN's stated fallback). `std_resolve`, `std_my_ip` and `DnsMemo` are `#[cfg(not(windows))]`: on Windows nothing resolves a PAC name or reads the local address (R-15 drops them there). The bounded runner is the generic `http::bounded<T: Send + 'static>(f: impl FnOnce() -> T + Send + 'static, wait: Duration) -> Option<T>`, compiled on every OS: it bounds these lookups and, on Windows, the OS evaluation call (PD-35). | `std::net::ToSocketAddrs` has no timeout; a hung resolver inside a native helper would hold the check past FR-041's total timeout, which the interrupt handler cannot stop. The abandoned thread is the cost (R-12). Sockets stay in `http.rs` (FR-045): `UdpSocket` joins the FR-081 confinement (SI-31). | FR-064, FR-068, FR-094 (R-12; see SI-30, SI-31) |
| PD-30 | **WPAD discovery (ruling R-9).** **Windows asks the OS and derives no candidate:** the OS evaluation call (PD-35) carries `WINHTTP_AUTOPROXY_AUTO_DETECT` with `dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP \| WINHTTP_AUTO_DETECT_TYPE_DNS_A` when `PacSource.auto_detect`, and `WINHTTP_AUTOPROXY_CONFIG_URL` with `lpszAutoConfigUrl` when `PacSource.url` is `Some`; with both, WinHTTP "first attempts auto-detection, and then, if auto-detection fails to locate a PAC URL, "falls back" to the auto-config URL", which is FR-069's order. There is no separate `WinHttpDetectAutoProxyConfigUrl` step (FR-073(b) admits it "only where the plan keeps a separate detection step"; this plan does not). **macOS and Linux:** the single candidate `pac::WPAD_DNS_URL = "http://wpad/wpad.dat"`, a single-label name the OS resolver completes with its own DNS search list; **no DNS devolution** and no DHCP; the pure `pac::wpad_dns_candidates() -> Vec<String>` returns exactly `[WPAD_DNS_URL]` and is what the real host returns (it takes no host name, so the list cannot depend on one). The candidates are fetched direct in order (PD-28); the first `200` whose body compiles is used; then `PacSource.url` if every candidate failed; nothing → `E_PAC_FETCH`. | WINHTTP_AUTOPROXY_OPTIONS: "If both the DHCP and DNS auto detect flags are specified, then DHCP is used first; if no PAC URL is discovered using DHCP, then DNS is used" (https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_autoproxy_options); the fall-back sentence: https://learn.microsoft.com/en-us/windows/win32/winhttp/winhttp-autoproxy-api ("If the PAC URL is Known in Advance"). Chromium resolves `const char kWpadUrl[] = "http://wpad/wpad.dat";` and states "we not use DNS devolution to find the WPAD host, since that could be dangerous … let the operating system apply the DNS suffix search paths" (https://chromium.googlesource.com/chromium/src/+/main/net/proxy_resolution/pac_file_decider.cc). | FR-066, SC-035 (R-9; see SI-24) |
| PD-31 | **PAC time helpers take an injected clock (macOS and Linux, ruling R-13)** `PacClock { unix_secs: i64, local_offset_secs: Option<i32> }` from `PacHost::now()`; the broken-down date is computed by hand (days-from-civil, no crate) in the pure `pac::clock_civil(clock, gmt, &mut warned) -> helpers::Civil` (compiled on every OS, SC-043). With `"GMT"` the UTC fields are used; without it the local fields (`unix_secs + offset`), and an unknown offset (`None`) falls back to UTC with one `warn!` per session. Real offsets: macOS `CFTimeZone::system().seconds_from_gmt(CFDate::now())` (core-foundation 0.10.1 `src/timezone.rs:45,52`, `src/date.rs:35`) in **`sysproxy_cf::system_utc_offset()`** (`crates/core/src/sysproxy_cf.rs`, macOS only), so `core_foundation` stays named only in `sysproxy_cf.rs` and its tests (FR-075, FR-081); it rounds and range-checks through the pure `sysproxy::cf_offset_secs(f64) -> Option<i32>` (every leg), and it is the one OS read in core, read-only, called only by `OsPacHost` and named in no test source (§4.11.1); no new crate; Linux `date +%z` through `run_bounded` (2 s), parsed by the pure `sysproxy::parse_utc_offset("+0130") -> Option<i32>`. **Windows reads no offset** (R-13, R-15): the OS evaluates PAC there, so `GetTimeZoneInformation` and the `Win32_System_Time` feature are not used and the round-1 `sysproxy::win_utc_offset` is withdrawn. Read once per PAC session, so a DST change applies at the next check. | `std` has no local time zone. MDN: "The optional `gmt` is `"GMT"` or omitted; omitted means local time" (https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Proxy_servers_and_tunneling/Proxy_Auto-Configuration_PAC_file). Spec FR-092 / FR-095 (`spec.md:644-645`, `:656-660`). | FR-064, FR-095 (R-13; see SI-32) |
| PD-32 | **The real `PacHost` is injected from `src-tauri`, not built by core.** `pac::PacHost: Send + Sync` carries the engine's machine access and the Windows OS evaluation behind one seam (FR-064, `spec.md:576-578`), every method with an inert default: `resolve(&self, name: &str, wait: Duration) -> Option<IpAddr>` (default `None`), `my_ip(&self) -> IpAddr` (`127.0.0.1`), `now(&self) -> PacClock` (the fixed epoch `PacClock { unix_secs: 0, local_offset_secs: None }`), `wpad_candidates(&self, wait: Duration) -> Vec<String>` (empty), `os_evaluates(&self) -> bool` (`false`), `os_find_proxy(&self, source: &PacSource, url: &str, wait: Duration) -> Result<Vec<OsProxyEntry>, PacError>` (`E_PAC_FETCH` "no OS PAC evaluator"). `pac::PacHostHandle(Arc<dyn PacHost>)` (`Clone`; `Debug` prints `PacHostHandle`; `PartialEq` is `Arc::ptr_eq`) travels as `HttpConfig.pac_host: Option<PacHostHandle>` (`None` in `production()` / `loopback()`, set only through `with_pac_host`). With `pac: Some` and `pac_host: None`, the session uses `pac::InertPacHost` (`impl PacHost for InertPacHost {}`: every default; it reads no clock, so a test that leaves the host unset reads none either; production always sets the real host). `src-tauri/src/os_proxy.rs` holds `OsPacHost`: on macOS and Linux it overrides the four engine methods (`resolve` / `my_ip` delegate to core's `http::std_resolve` / `http::std_my_ip`, `now` and `wpad_candidates` are PD-30 / PD-31); on Windows it overrides only `os_evaluates` (`true`) and `os_find_proxy` (PD-35). `os_evaluates` is what selects the OS path in `http::get` (§4.5.1), so the OS path is compiled and tested on every host with a fake host (SC-041) while production takes it on Windows only. | The Windows OS call needs `windows-sys`, which FR-073(b) puts in "the one file that holds the real Windows reader": a `src-tauri` dependency, so core can only receive the host (the macOS local offset is core's `sysproxy_cf::system_utc_offset`, PD-31, which `OsPacHost` calls). `rquickjs`'s `Function::new` requires `F: IntoJsFunc<'js, P> + 'js` (rquickjs-core `src/value/function.rs:41-44`) under the higher-ranked `'js` of `Context::with`, so a helper closure cannot borrow `&dyn PacHost`: it captures an `Arc` clone. `Option` keeps every Phase 4 `AppContext` equality test unchanged (`None == None`). Defaults keep each real host to the methods its OS uses (R-15: "dropped if nothing on Windows needs them"). | FR-041, FR-064, FR-066, FR-092 (see SI-25, SI-26) |
| PD-33 | **The answer walk (ruling R-10).** `http::get` evaluates the PAC per hop (`FindProxyForURL(url, host)`, an `https` hop's URL reduced to `scheme://host[:port]/` by the shared `pac::pac_url_argument` for **both** evaluators, so the Windows OS call receives the reduced URL too, FR-064) and walks that hop's `Vec<ProxyChoice>` in order inside the one request chain: an entry is advanced past only on the **no-response** class (`HostNotFound`, `ConnectionFailed`, `ConnectProxyFailed(_)`, `Timeout(Timeout::Resolve \| Timeout::Connect)` while the chain deadline has time left: ureq 3.5.0 `src/timings.rs:14-49`, `Connect` = "Total connection setup timeout, including proxy DNS, negotiation, and TLS"); any response, a TLS error after connecting, a status, a body error, `Timeout::Global` or a policy refusal ends the walk with that outcome. Each entry is tried at most once; the last entry's `E_FEED_OFFLINE` is the chain's error. Every proxy an answer names passes `check_proxy` before the walk opens any socket (`LoopbackOnly` → `E_FEED_URL`, FR-067). `PROXY`/`HTTP` → `http://`, `HTTPS` → `https://`, `SOCKS`/`SOCKS4` → `socks4://`, `SOCKS5` → `socks5://` (ureq 3.5.0 parses all four and supports SOCKS4 under `socks-proxy`, `src/proxy.rs:149-150`, `:498-499`; `src/unversioned/transport/socks.rs:151-166`); default ports 80 / 443 / 1080 (the spec's choice; MDN states none). | FR-059, FR-067 (R-10). One fresh agent per entry, as per hop today (PD-4). | FR-059, FR-067 |
| PD-34 | **`check_proxy` accepts `socks4`** under both policies (FR-042 / FR-046 as revised list `http`, `https`, `socks4`, `socks5`); `socks4a` / `socks5h` are refused (no source produces them: a PAC answer maps `SOCKS*` to `socks4`/`socks5`). The round-0 plan refused `socks4`. | FR-042, FR-046, FR-067: a PAC `SOCKS host:port` answer is SOCKS4 (spec SC-033 `SOCKS s` → socks4 `s:1080`). | FR-042, FR-046 |
| PD-35 | **Windows: the OS evaluates the PAC through `WinHttpGetProxyForUrlEx` (rulings R-15, R-16 as recommended).** `OsPacHost::os_find_proxy(source, url, wait)` (`src-tauri/src/os_proxy.rs`, `#[cfg(windows)]`): (1) `WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_NO_PROXY, null, null, WINHTTP_FLAG_ASYNC)` (a session whose own requests would go direct; it sends none); (2) `WinHttpSetStatusCallback(session, Some(on_status), WINHTTP_CALLBACK_FLAG_REQUEST_ERROR \| WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE \| WINHTTP_CALLBACK_FLAG_HANDLES, 0)` (the last flag is not exported by `windows-sys` 0.61.2, so `os_proxy.rs` defines it as winhttp.h does: `WINHTTP_CALLBACK_STATUS_HANDLE_CREATED \| WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING`, §1.2), before (3) `WinHttpCreateProxyResolver(session, &mut resolver)` (the resolver inherits the session's callback); (3b) `ctx = Box::into_raw(Box::new(CallbackCtx { tx }))` and `WinHttpSetOption(resolver, WINHTTP_OPTION_CONTEXT_VALUE, &ctx, size_of::<usize>())`, which binds `ctx` to the resolver so its `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` is delivered with `ctx` (WinHttpCloseHandle: the closing callback "is made" only for a handle "created with a non-NULL context value"); (4) `WinHttpGetProxyForUrlEx(resolver, url, &options, ctx)` with `options` per PD-30 plus `fAutoLogonIfChallenged = FALSE` (R-17), `lpvReserved` null, `dwReserved` 0, and no `RUN_INPROCESS` / `RUN_OUTPROCESS_ONLY` / `NO_CACHE_*` flag ("This option has no effect when passed to WinHttpGetProxyForUrlEx"); any return but `ERROR_IO_PENDING` is that call's failure; (5) wait `recv_timeout(wait)` on a capacity-1 channel the callback `try_send`s `(status, dwError)` into (`on_status` ignores a callback whose context is 0, i.e. the session's and the resolver's `HANDLE_CREATED`; on the resolver's `HANDLE_CLOSING` it reclaims and drops `ctx` and does nothing else; otherwise it reads `WINHTTP_ASYNC_RESULT.dwError` for `REQUEST_ERROR` and `try_send`s through `&(*ctx).tx`); `GETPROXYFORURL_COMPLETE` → `WinHttpGetProxyResult(resolver, &mut result)`, copy each `WINHTTP_PROXY_RESULT_ENTRY` into a core `pac::OsProxyEntry` (`fProxy == 0` → `Direct`; `ProxyScheme` `WINHTTP_INTERNET_SCHEME_HTTP` / `_HTTPS` / `_SOCKS` → `Http` / `Https` / `Socks`, any other → `Other(n)`; `pwszProxy` through `sysproxy::read_wide`, `ProxyPort`), then `WinHttpFreeProxyResult(&mut result)`; (6) close the resolver, then the session; **the waiting thread never frees `ctx`** (review round 3): once (3b) has bound it, `ctx` is owned by the resolver handle and is freed only by `on_status` on that handle's `HANDLE_CLOSING`, the last callback WinHTTP makes for it, on the normal, error, refused-call and cancel paths alike. A `GETPROXYFORURL_COMPLETE` or `REQUEST_ERROR` becomes visible to `rx` while the callback thread may still be inside `try_send`, so freeing on receipt would drop the sender under another thread's `&SyncSender`; WinHttpCloseHandle's remarks say the application "must still be prepared to receive callbacks for the closed handle", must "maintain that binding until the callback function receives a WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING notification", for which it "must enable the WINHTTP_CALLBACK_FLAG_HANDLES flag", and should "be conservative in freeing the context structure until after having received the WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING notification" (https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle, read 2026-10-31). If (3b) fails (the option table lists `WINHTTP_OPTION_CONTEXT_VALUE` for session and request handles only, so its acceptance on a resolver handle is undocumented, §9), the call still proceeds and `ctx` is **deliberately leaked** (one `Box` holding one `SyncSender`, per hop) rather than freed by the waiting thread; `ctx` is allocated in (3b), after the resolver exists, so a failure before it has nothing to free. **Error classes:** `ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT` → `E_PAC_SCRIPT`; every other code (`ERROR_WINHTTP_AUTODETECTION_FAILED`, `_UNABLE_TO_DOWNLOAD_SCRIPT`, `_UNRECOGNIZED_SCHEME`, `_LOGIN_FAILURE`, `_AUTO_PROXY_SERVICE_ERROR`, `_OPERATION_CANCELLED`, …) → `E_PAC_FETCH` with the number in the detail. **Time-out (FR-092 "abandoned and never killed"):** core calls the host inside `http::bounded(…, min(PAC_PHASE_BUDGET, remaining))` (§4.5.1), so `get` never waits longer, whatever the host does (proven over a fake host, SC-041); inside the host, an unanswered call is **cancelled by closing its resolver handle** (the documented cancel, not a kill: "When finished or cancelling an outstanding operation, close this handle with WinHttpCloseHandle"), then the session is closed and the host returns at once; the cancel's late `REQUEST_ERROR` finds `ctx` still alive (its `try_send` meets a dropped receiver, which is not an error) and `ctx` is freed by the resolver's `HANDLE_CLOSING` whenever WinHTTP delivers it, so no wait and no grace constant is needed (the round-2 `OS_CANCEL_GRACE` is withdrawn). `WinHttpSetTimeouts` is not used: its page names name resolution, connect, send and receive of "HTTP transactions" and says nothing of autoproxy, and the script runs in the service. Each call opens and closes its own session; the out-of-process service caches the PAC URL and script for "the whole computer" while `fAutoLogonIfChallenged` is `FALSE`, so per-hop calls do not refetch the script each time. | Ex: "This function always executes out-of-process"; "provides the application with the full proxy list that was returned by the PAC script allowing the application to better handle failover to "DIRECT" and to understand SOCKS"; asynchronous, `ERROR_IO_PENDING`, callback order (https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpgetproxyforurlex, minimum client Windows 8). `WinHttpCreateProxyResolver`: "The session handle must be opened using WINHTTP_FLAG_ASYNC" (https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpcreateproxyresolver). Entry fields: https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_proxy_result_entry. Cache: https://learn.microsoft.com/en-us/windows/win32/winhttp/autoproxy-cache. `WinHttpSetTimeouts`: https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsettimeouts. Every symbol is in `windows-sys` 0.61.2 under the two ruled features (§1.2). Why not `WinHttpGetProxyForUrl` as R-15 names it: R-16. | FR-092, FR-073(b), FR-066, FR-068 (R-15, R-16, R-17; see SI-38) |
| PD-36 | **One answer parser and one walk for both evaluators.** The Windows entry list is rendered into PAC answer text by the pure `pac::os_answer(&[OsProxyEntry]) -> (String, Vec<String>)` and that text goes through the same `pac::parse_answer` the QuickJS answer goes through, then the same PD-33 walk: `Direct` → `DIRECT`; `Http` → `PROXY host[:port]`; `Https` → `HTTPS host[:port]`; `Socks` → `SOCKS host[:port]` (FR-067 maps `SOCKS` to `socks4`; the docs do not say which PAC keywords WinHTTP folds into its one SOCKS scheme, §9); a port of 0 is omitted so FR-067's default port applies; an unbracketed IPv6 literal is bracketed; an `Other(_)` scheme, an empty host, or a host holding `;`, whitespace or a control character is skipped with one warning (`E_PAC_RESULT`, through `log_text`), so the rendering cannot change the entry count. Entries joined with `"; "`, in WinHTTP's order. An empty list renders `""`, which `parse_answer` turns into `E_PAC_RESULT` (zero usable entries). `MAX_PAC_ANSWER_BYTES` and `MAX_PAC_ENTRIES` therefore bound the OS answer too. Compiled and tested on every host (`pac::tests::winhttp_answer_cases`, SC-041). | The ruling asks for one tested parser and walker for both paths; rendering to text reuses `parse_answer`'s tested grammar instead of a second mapper. | FR-067, FR-092 (R-10, R-15; see SI-38) |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before build)

| Id | Spec text | Problem (evidence) | Proposed fix |
|---|---|---|---|
| SI-1 | FR-049 "`trait Platform` gains `fn system_proxy`, with three implementations"; New components "`platform/{windows,macos,linux}.rs` (changed)"; SC-029 test names `platform::tests::*_proxy_parser_cases` | Phase 4's guards forbid it (PD-1): platform literal allow-list (`injection.rs:40-68`), FS tokens (`:22-37`), no environment in core (`clippy.toml`), per-OS modules not compiled elsewhere (`platform/mod.rs:60-71`). Also "every `Platform` test double is updated": there are none (`grep -rn "impl Platform"` finds only the three real implementations). | FR-049: parsers in `core::sysproxy`, readers in `src-tauri/src/os_proxy.rs` through `SystemProxyFromOs for NativePlatform`; `trait Platform` unchanged. SC-029 names: `sysproxy::tests::windows_proxy_parser_cases`, `sysproxy::tests::macos_proxy_parser_cases`, `sysproxy::tests::linux_proxy_parser_cases`. Drop the test-double sentence. |
| SI-2 | FR-070 "a plain request whose request target is an absolute-form `http://127.0.0.1:<port>/path` … is recorded"; SC-030 `proxy_path_http` "the proxy records an absolute-form request for that target" | `ureq` 3.5.0 sends `CONNECT <host>:<port>` for every target through an `http`/`https` proxy (`unversioned/transport/connect.rs:38-58`, `:109-124`; `config.rs:251-259` `connect_proxy_uri` returns the proxy for every `is_connect()` protocol), so an absolute-form request never happens and the SC as written cannot pass. | FR-070: drop the absolute-form role; after `ConnectOk` the connection is served again (PD-7). SC-030 `proxy_path_http`: the proxy records `CONNECT 127.0.0.1:<port2>`, then the tunnelled `GET /versions.json`, and the body is returned; the target server records nothing. |
| SI-3 | FR-063 "`proxy_env.rs` … for exactly those three names and nothing else"; Phase 4 test `disallowed_methods_expectations_are_the_seven_d6_sites` | (1) Reading the environment in `proxy_env.rs` is a `disallowed_methods` call, so it needs one `#[expect(clippy::disallowed_methods, reason = …)]`, and `D6_SITES` / `D6_TOTAL` (`injection.rs:70-77`) must gain `("src-tauri/src/proxy_env.rs", 1)` and become 8. (2) The KDE reader (R-4) needs `XDG_CURRENT_DESKTOP`, `XDG_CONFIG_HOME` and `HOME` on Linux. | FR-063: `proxy_env.rs` reads `HTTPS_PROXY`, `https_proxy`, `ALL_PROXY`, `all_proxy`, `NO_PROXY`, `no_proxy`, and on Linux only `XDG_CURRENT_DESKTOP`, `XDG_CONFIG_HOME`, `HOME`, through one `var_os` call site; the D6 table gains that site (total 8; the test keeps its name so `required-tests.json` keeps its line). If R-4 drops KDE, the three Linux names go too. |
| SI-4 | FR-084 / FR-085 (only `PATHS`, `STEPS` and the `live-network` ban named) | `manager-ci-shape.test.js` also requires `required-tests.json` to equal its `EXPECTED_REQUIRED` verbatim (`:318`, check at `:527`), pins each step's commands in `COMMANDS` (`:205`) and lists the `::notice`/`::error` steps in `ASSERTION_STEPS` (`:192`). Adding tests to `required-tests.json` or a step to `manager-ci.yml` without these fails `manager-ci.yml keeps the Phase 4 shape`. | FR-084: the shape test also gains the new `EXPECTED_REQUIRED` (equal to §4.13), a `COMMANDS['roster absent']` entry and `'roster absent'` in `ASSERTION_STEPS`. |
| SI-5 | D15 "The codes are listed in FR-008, FR-020 and FR-047" | There is no FR-047. | Point D15 at plan §3 (the full code table), or add FR-047 with that table. |
| SI-6 | FR-048 `resolve_proxy(…, target: &url::Url)`; FR-061 passes `&feed_url` | `feed_url` is a `String` in `src-tauri` (`lib.rs:26-33`), which has no `url` dependency and whose `[dependencies]` set is guarded (`config_guard.rs:32-33`). | `target: &str` (PD-13). |
| SI-7 | FR-063 "`crates/core/Cargo.toml` declares `live-network = []`" | `live_feed.rs` uses `fresh_temp_roots()`, compiled only with `test-support` (PD-14). | `live-network = ["test-support"]`; the guard asserts that line and no `default` containing `live-network`. |
| SI-8 | FR-004 "Each finding's `instance_path` is the one `contract.js` computes … for a duplicate, the second plugin's field"; FR-024 sibling prefix rule for duplicates | Order dependence (PD-9): `serde_json` iterates bytewise, `contract.js` in document order. In `invalid/dup-vst3_bundle-casefold.json` (plugin keys not bytewise sorted, checked this session: it and `plugin-key-pattern.json`, `plugin-key-reserved.json` are the only unsorted fixtures) the reported path differs. And a stable-vs-sibling clash is kept or dropped depending on order. | FR-004: a duplicate's `instance_path` is the field of the plugin that comes second **in bytewise key order** (equal to `contract.js` for every bytewise-ordered document; the parity test compares codes for duplicate rows, as `cases.json` does). FR-024: a duplicate is dropped iff either side is a `prerelease` install (PD-9); SC-010's duplicate boundary case gains both key orders. |
| SI-9 | SC-029 Windows "registry values as strings and a DWORD"; FR-049 Windows reads `ProxyEnable`, `ProxyServer`, `ProxyOverride`, `AutoConfigURL`, `AutoDetect` | No documented `AutoDetect` value exists under `…\Internet Settings`; the auto-detect flag lives in the undocumented `Connections\DefaultConnectionSettings` blob. `WinHttpGetIEProxyConfigForCurrentUser` returns all four facts with documented meanings (§1.2). | With R-2 as recommended: the Windows parser input is `WinIeProxyConfig { auto_detect: bool, auto_config_url, proxy, proxy_bypass: Option<String> }` (the WinHTTP structure); SC-029's Windows rows use it. |
| SI-10 | FR-087 allowed bodies: `https_only_refuses_before_connecting`, `config_for_dev_url_cases`, `url_policy_cases` | `next_hop_cases` (SC-012, "under `HttpsOnly` …") and `agent_trusts_the_os_store` (SC-026, "the agent config of every `UrlPolicy` variant agrees") must name `UrlPolicy::HttpsOnly` / `HttpConfig::production()`; both are pure. | Add `next_hop_cases`, `agent_trusts_the_os_store`, `proxy_scheme_cases` (FR-042's `HttpsOnly` proxy-scheme rule, pure `check_proxy` calls) and `production_values` (FR-041's `production()` literals, pure) to FR-087's list (still no socket). |
| SI-11 | D6 / FR-043 "`ureq`'s own environment read never takes effect" | True for the value, but the read still happens: `Agent::config_builder()` starts from `Config::default()`, whose `proxy` is `Proxy::try_from_env()` (`config.rs:1065-1073`), which calls `std::env::var` (`proxy.rs:232-246`). | D6: "core's own code reads no environment; `ureq` reads the proxy variables while building its default config and the value is always replaced (FR-043)". No code change. |
| SI-12 | FR-046 `ProxyConfig { url: String }` then "and `no_proxy: Vec<String>`" | The struct is stated twice with different fields. | `ProxyConfig { url: String, no_proxy: Vec<String> }`. |
| SI-13 | FR-085 lists `production_config_only_in_allowed_tests` among `cargo.all` names | Integration tests print bare names; unit tests print `module::tests::name`. The list must use the printed form (`required tests ran` matches `^test (\S+)… ok$`, `manager-ci.yml:146`). | No spec change; §4.13 uses the printed forms. |
| SI-14 | FR-085 `feed::tests::check_now_sends_no_cache` etc. | No issue; listed for completeness of the name check in §4.13. | — |
| SI-15 | FR-061 dev-feed proxy sentence | Contradiction resolved by PD-12. | FR-061: `dev_feed::http_config(&feed_url, proxy)` calls `http::config_for_dev_url(url, proxy)`; SC-018's `config_for_dev_url_cases` gains a proxy argument (loopback drops it, production keeps it). |
| SI-16 | FR-021 / FR-037 say nothing about a contract-valid value the model cannot hold | PD-8: `payload.files` has no schema maximum and `min_os` components no bound (`versions.v2.schema.json`), so `1e300` or `11.4294967296` is schema-valid but unrepresentable; the Node validator and `dry_run` accept it. Silently choosing "reject" would break FR-007 / SC-002 parity and, under D1, block every update. | New FR (ruling R-7): "A value the contract accepts but the model cannot hold (`json_u64` refuses it, or a `min_os` component exceeds `u32`) drops that one platform asset with one `warn!` naming `E_MODEL_RANGE` and its path; the document's verdict is unchanged. `"size": 2.0` is accepted as 2." Test `feed::tests::model_range_cases` (SC-005 gains it). If R-7 rules "reject", the FR says `E_FEED_INVALID` instead and the test's verdict column flips. FR-056's list of extra `warn!`s gains "one per asset or v1 entry the model drops or sanitises (PD-8, PD-21, PD-22)". |
| SI-17 | FR-083 "counts, with `grep -acF`, each name and `api.github.com`" | The plan's step is a Node byte search (§4.12): one script on all three legs, no per-file bash loop. The spec and the shape test's `COMMANDS` entry must name the same command. | FR-083: "counts, with a Node byte search (`Buffer.includes`) over the release binary and every file under `dist/`, each name and `api.github.com`"; the ≥ 9 names and positive-control requirements are unchanged. `COMMANDS['roster absent']` (§4.12) pins the Node heredoc. |
| SI-18 | FR-021 `asset_name: String`; FR-025 (last segment, not decoded); FR-026 lists only `PlainName` and `AssetUrl` | PD-21: the segment is the feed string most likely to become a path, and nothing checks it. | FR-021: `asset_name: AssetName`. FR-025: the segment is taken from `url::Url::path_segments()` (not percent-decoded) and must pass PD-21's rule, else the asset is dropped with `E_MODEL_ASSET_NAME` (R-7). FR-026: `AssetName` joins `PlainName` and `AssetUrl` (no public constructor). |
| SI-19 | FR-021 `download_url: Option<String>`; FR-027 "`Feed.plugins` holds exactly the document's `plugins` keys" | PD-22: the v1 floor checks neither the key nor `download_url`. | FR-021: `download_url: Option<PageUrl>` (`None` only when absent; `PageUrl` is a checked newtype with the C1 `url` rule, distinct from `AssetUrl`, review round 3). FR-026: `PageUrl` joins `PlainName`, `AssetUrl` and `AssetName` (no public constructor), and only `PlatformAsset.url` is an `AssetUrl`. FR-027: "exactly the document's `plugins` keys whose entry the model keeps (every v2 key; a v1 entry whose key fails the C1 `pluginName` rule or whose `download_url` fails the C1 `url` rule is dropped with one `warn!` `E_MODEL_V1_ENTRY`, FR-029(c))". FR-029 itself needs no change (challenge round 2: the plan now follows it). Test `feed::tests::v1_entries_are_sanitized`. |
| SI-20 | FR-043 `agent_config(&HttpConfig) -> ureq::config::Config`, `proxy(config.proxy)`; FR-046 "`http::get` converts it to `ureq::Proxy`" | PD-23: a proxy `url` accepts and `ureq` refuses must not become a direct connection. | FR-043: `agent_config(&HttpConfig, Option<ureq::Proxy>, AgentPurpose)` (`AgentPurpose::{Feed, PacFetch}`: `Feed` sets `https_only(policy == HttpsOnly)`, `PacFetch` sets `https_only(false)` and leaves the scheme rule to `check_pac_url`, PD-28; FR-065's "FR-043's agent configuration except that the fetch is always direct" gains "and except `https_only`, which a PAC fetch leaves off because FR-065 admits `http`"); FR-046: "the conversion happens once, before any socket opens; a proxy `ureq` refuses is `E_FEED_URL` (redacted), never a direct request". `loopback_proxy_only` gains the `http://%FF@127.0.0.1:<port>` row. |
| SI-21 | FR-049 / SC-029: only the pure parsers are tested; the readers' own logic (UTF-16 read and `GlobalFree`, `CFDictionary` downcasts, the bounded `gsettings` wait, the `kioslaverc` path, the desktop choice, the upper/lower environment merge) is tested nowhere, and `src-tauri` cannot hold a test (D12a) | PD-1, PD-5, PD-17. A wrong downcast, a pointer read bug or a broken timeout would fall back to `SystemProxy::default()` on a user's machine with every lane green. | FR-049: each reader is split into one OS call (in `os_proxy.rs`) and core seams with tests: `sysproxy::tests::win_ie_strings_are_read_and_freed_once` (every leg), `sysproxy_cf::tests::from_cf_proxies_cases` (macOS, R-6; SC-040's name), `sysproxy::tests::bounded_run_abandons_and_reaps` (macOS, Linux), `sysproxy::tests::linux_desktop_choice_cases` (SC-039, SI-36), `sysproxy::tests::kioslaverc_path_cases` and `sysproxy::tests::kde_file_read_cases` (every leg); FR-048: `http::tests::proxy_env_from_vars_cases` tests `ProxyEnv::from_vars`. FR-085: the per-OS names join `cargo.macos` / `cargo.linux`. |
| SI-22 | FR-070 `Reply` list; "binds `127.0.0.1:0` (never another address)" | Three behaviours have no reply that triggers them: a mid-body stall (FR-044's `ureq::Error::from` recovery), a delayed redirect (PD-3's chain deadline), and a second loopback host for a per-hop bypass test (FR-046 "every redirect hop's host"). | FR-070 gains `Reply::HeadersThenStall { declared_len, sent, stall }` and `Reply::Delayed { after, reply: Box<Reply> }` (both end at once on `Drop`, as `Stall` does) and `FixtureServer::start_v6(routes)`, which binds `[::1]:0`; "never another address" becomes "never a non-loopback address". The `CONNECT` refusal names its rule: an authority other than `127.0.0.1:<port>` or `[::1]:<port>` is answered `403` and never tunnelled (test `fixture_server::tests::connect_to_remote_host_is_refused`). |
| SI-23 | FR-069 "(2) Otherwise the OS settings, in the order WinHTTP itself uses: `auto_detect` (WPAD) first, then `pac_url`, then the OS static `proxy`"; FR-048(b); FR-049 "A static proxy found beside a PAC source is kept in `proxy` (FR-069)"; SC-028 "a `pac_url` and/or `auto_detect` … with the OS static `proxy` kept beside it as the fallback" | **Withdrawn (challenge round 2, 2026-10-31).** Round 1 claimed the R-1 ruling text states "environment first, then the OS static proxy, then PAC/WPAD". The spec's R-1 entry (`spec.md:1508-1514`) says only "Environment variables still win over OS settings", and no other source holds that order. | None: FR-069, FR-048(b), FR-049 and SC-028 stand as written, and the plan follows them (PD-5, §4.5 `resolve_proxy`, §7.1 SC-028 / SC-035). |
| SI-24 | FR-066, SC-035 (WPAD candidates) | **Applied in the spec 2026-10-31** (FR-066 `spec.md:589-599`, SC-035 `:1260-1262`): macOS and Linux try only `pac::WPAD_DNS_URL`, no devolution, no DHCP. With R-15 the Windows half changed too: discovery is part of the OS evaluation call (PD-30, PD-35), with no separate `WinHttpDetectAutoProxyConfigUrl` step, which FR-073(b) already allows (`spec.md:790`). | None. SC-035's test is the spec's `pac::tests::wpad_candidate_cases` over the pure `pac::wpad_dns_candidates()` (PD-30). |
| SI-25 | FR-064 "What touches the machine goes through the injected `PacHost` trait (…, and on Windows the OS evaluation of FR-092; the plan fixes the signatures)" (`spec.md:576-578`) | PD-32 / PD-35 fix them: `Function::new` needs `'js`-outliving captures (an `Arc`), the waits are passed in, the clock has a type, and the OS evaluation needs a selector so its path can be tested on every host. | No spec change: `PacHost: Send + Sync` with defaulted methods `resolve(&self, &str, Duration) -> Option<IpAddr>`, `my_ip(&self) -> IpAddr`, `now(&self) -> PacClock`, `wpad_candidates(&self, Duration) -> Vec<String>`, `os_evaluates(&self) -> bool`, `os_find_proxy(&self, &PacSource, &str, Duration) -> Result<Vec<OsProxyEntry>, PacError>`; `pac::evaluate(script: &str, url: &url::Url, host: PacHostHandle) -> Result<Vec<ProxyChoice>, PacError>` (macOS / Linux; FR-064's `host: &dyn PacHost` becomes a handle for the `Arc` reason). |
| SI-26 | FR-041 `HttpConfig { policy, connect_timeout, total_timeout, proxy, pac }`; FR-061 / FR-063 / FR-087 name `with_proxy` / `with_pac` only | PD-32: the real PAC host comes from `src-tauri`. | FR-041: `HttpConfig` gains `pac_host: Option<PacHostHandle>` (`None` in `production()` and `loopback()`) and `with_pac_host(self, Option<PacHostHandle>)`, the only way a host enters. FR-061: `lib.rs` adds `.with_pac_host(Some(os_proxy::pac_host()))`. FR-063: `.with_pac_host(` occurs in `src-tauri` only in `lib.rs`; `OsPacHost` only in `os_proxy.rs`. FR-087: `.with_pac_host(` only in the PAC-path test bodies of SC-034 to SC-036 and SC-041; `OsPacHost` (whose Windows `os_find_proxy` is "the real Windows OS PAC evaluator" of FR-087), `std_resolve(`, `std_my_ip(` and `system_utc_offset(` in no test source; the allowed bodies for `HttpConfig::production()` / `UrlPolicy::HttpsOnly` gain `pac_policy_cases` and `wpad_candidate_cases` (both pure `check_pac_url` calls under `HttpsOnly`). |
| SI-27 | FR-048 `resolve_proxy(…, target: &url::Url) -> ProxyResolution`; FR-061 `http::config_for_dev_url(url) -> HttpConfig` | SI-6 (`target: &str`) applies to the new return type; PD-12 needs the resolution and host as arguments. | `resolve_proxy(env, system, target: &str) -> ProxyResolution`; `config_for_dev_url(url: &str, resolution: ProxyResolution, pac_host: Option<PacHostHandle>) -> HttpConfig`; SC-018's `config_for_dev_url_cases` gains a resolution with a PAC source (loopback drops it, production keeps it). |
| SI-28 | FR-065 "The fetch is inside the check's total timeout"; FR-068 "the engine's memory limit, where it has one, is set … (the plan names the mechanism)" | PD-27 names the mechanism and gives the PAC phase its own bound. | FR-065: discovery, fetch and compile share `PAC_PHASE_BUDGET` = 10 s, inside the chain deadline. FR-068: `PAC_MEMORY_LIMIT` = 32 MiB (`Runtime::set_memory_limit`), `PAC_STACK_LIMIT` = 256 KiB, the 2 s budget through `Runtime::set_interrupt_handler`; a stack overflow is `E_PAC_SCRIPT` (SC-036 gains `function f(){ f() } f()`). |
| SI-29 | FR-067 says nothing about a very long answer | PD-27: a script can return megabytes of entries. | FR-067: an answer longer than `MAX_PAC_ANSWER_BYTES` (64 KiB) is `E_PAC_RESULT`; only the first `MAX_PAC_ENTRIES` (16) usable entries are walked, the rest dropped with one `warn!`. SC-033 gains both rows. The spec's "not a string → failure" is kept for `null` (MDN reads `null` as "no proxies"). Under FR-069 the failure fallback is the OS static proxy when one is configured, else direct, so a `null` answer beside a configured static proxy uses that proxy where a browser would go direct; the plan keeps the spec's rule (a failure is never more permissive than the static setting) and `find_proxy_result_cases` pins it. |
| SI-30 | FR-068 / FR-092 thread wording | **Applied in the spec 2026-10-31** (FR-068 `spec.md:622-625`, FR-092 `:638-639`, FR-094 `:650-655`): the engine is interrupted on the calling thread; a DNS lookup (macOS / Linux) or the Windows OS evaluation call that has not answered in time is abandoned, never killed. The Windows WPAD call of round 1 no longer exists (PD-30); the Windows bound is core's `http::bounded` around `os_find_proxy` plus the host's cancel by closing its own resolver handle (PD-35), which kills nothing. | None. `http::tests::bounded_call_abandons_after_wait` (every host) and SC-041's never-returning row prove the bound. |
| SI-31 | FR-081 confinement lists `ureq`, `rustls`, `TcpStream`, `TcpListener`, `rquickjs` (`spec.md:821-830`) | PD-29 opens a `UdpSocket` and calls `to_socket_addrs` in `http.rs` (macOS / Linux only, `#[cfg(not(windows))]`). | FR-081: `UdpSocket` and `to_socket_addrs` occur in code only in `http.rs` (and its tests); `pac.rs` names no socket type. A planted `UdpSocket::bind` in `pac.rs` is reported (self-test). The `rquickjs` confinement is already the spec's. |
| SI-32 | FR-073(b) / SC-038 `windows-sys` features | **Withdrawn (rulings R-13 and R-15, 2026-10-31).** Windows reads no local offset (the OS evaluates PAC), so `Win32_System_Time` is not added; the features stay exactly `Win32_Foundation` and `Win32_Networking_WinHttp` (FR-092 `spec.md:644-645`, SC-038 `:1295-1296`). Every symbol PD-35 uses is in those two (§1.2). | None. |
| SI-33 | FR-073(d), FR-080, SC-038 (the engine crate) | **Superseded by the spec revision of 2026-10-31** (`spec.md:792-796`, `:814-820`, `:1297-1301`): `rquickjs = "0.14.0"`, default features only, in `crates/core/Cargo.toml` `[target.'cfg(not(windows))'.dependencies]` (a table of exactly that one crate), not in `[dependencies]` and not in `FR007_DEPENDENCIES` (the ten names). | None: §4.1 and §4.11.1 follow the spec. |
| SI-34 | SC-034 `pac_path_proxy` "B records the `CONNECT` / absolute-form request" | SI-2: `ureq` 3.5.0 tunnels every target. | "B records `CONNECT 127.0.0.1:<C>` and, after `ConnectOk`, the tunnelled `GET /versions.json`; the body arrives as in SC-030 `proxy_path_http`." |
| SI-35 | FR-064 "with their Netscape-defined meanings"; SC-033 `weekdayRange`, `dateRange`, `timeRange` rows | "Netscape-defined" has no single verified text for a reversed range: MDN, the source read this session, states that a reversed range matches only its endpoints (`weekdayRange("FRI", "MON")` "true Friday and Monday only", `timeRange(23, 0)` true "only … if the current hour is 23:00 or midnight") and that Firefox before 49 evaluated ranges differently (R-14). | FR-064: the helpers follow MDN's page (URL in §1.2), including "a reversed range matches only its endpoints" and "`dateRange(x, gmt)` with one number takes a value above 31 as a year"; SC-033 gains one reversed row per time helper. If R-14 rules "wrap around", the rows flip. |
| SI-36 | FR-074 "read in `src-tauri` only, in the file that holds the real reader"; SC-039 `platform::tests::linux_desktop_choice_cases` | PD-1 / SI-1 / SI-3: the environment is read in `src-tauri/src/proxy_env.rs` (the one proxy-environment read, which hands the three Linux values to the reader in `os_proxy.rs` as `ProxyInputs`), and `linux_uses_kde` is a pure `core::sysproxy` function, not a `platform` one. | FR-074: "…are read in `src-tauri` only, in `src-tauri/src/proxy_env.rs` (FR-063, SI-3); the environment-read guard admits exactly those three names there and nothing else". SC-039: `sysproxy::tests::linux_desktop_choice_cases`, rows unchanged. |
| SI-37 | FR-075 "used only by `sysproxy_cf::from_cf_proxies`"; FR-073(f) | **Applied in the spec 2026-10-31** (FR-075 `spec.md:807-813`, R-13 decided as recommended): `system_utc_offset() -> Option<i32>` sits beside `from_cf_proxies` in `sysproxy_cf.rs`, so `core_foundation` stays named only there and in its tests. | None. |
| SI-38 | FR-092 "the real Windows PAC host calls `WinHttpGetProxyForUrl` … Its answer (access type, proxy list, bypass list) is mapped … (a direct access type or a bypass match gives `DIRECT`)" (`spec.md:633-645`); FR-073(b) "`WinHttpGetProxyForUrl` with `WinHttpOpen` / `WinHttpCloseHandle`" (`:790`); SC-041 `pac::tests::winhttp_answer_cases` rows (`:1329-1331`) | R-16 (recommended): `WinHttpGetProxyForUrl` may run the script in the manager's process (§0.3 R-16); the Ex form always runs out of process and returns ordered entries, not a `WINHTTP_PROXY_INFO`. | FR-092: "…calls `WinHttpGetProxyForUrlEx` (on a resolver from `WinHttpCreateProxyResolver` over a `WINHTTP_FLAG_ASYNC` session; completion through `WinHttpSetStatusCallback`, the result through `WinHttpGetProxyResult` / `WinHttpFreeProxyResult`, cancelled by closing the resolver) … Its answer, the ordered `WINHTTP_PROXY_RESULT_ENTRY` list, is mapped … (an entry whose `fProxy` is false gives `DIRECT`)". FR-073(b): those function names. SC-041 `winhttp_answer_cases`: `[Direct]` → `DIRECT`; `[Http a:8080, Http b:3128]` → [http a:8080, http b:3128] in order; `[Http a:8080, Direct]` → [http a:8080, direct]; `[Https p:3128]` → https p:3128; `[Socks s:1080]` → socks4 s:1080; `[Http a:0]` → http a:80; an `Other(3)` entry, an empty host and a host `a;b` are skipped with one `warn!` `E_PAC_RESULT` each; an empty list → `E_PAC_RESULT`. If R-16 rules (i) or (ii), this row is withdrawn and the spec stands (the `WINHTTP_PROXY_INFO` mapping of §0.3 R-16). |
| SI-39 | FR-085 "the engine-bound tests of SC-033, SC-036 and SC-042 … join `cargo.macos` and `cargo.linux` only" (`spec.md:854`); SC-035 "Local; PENDING-CI (`all`)" (`:1269-1270`) | SC-034's manager-fetch tests (`pac_path_proxy`, `pac_path_direct`, `pac_list_walk`, `pac_per_hop`, `pac_fetched_once_never_via_proxy`, `spec.md:1259`) and SC-035's `wpad_discovery_walk` (`:1263`) and `pac_fallback_to_static_proxy` (its failing PAC is a `404` the manager fetches, which it never does on Windows, FR-092) are `cfg(not(windows))` too. Listed in `cargo.all`, the Windows leg's `required tests ran` step would not find them and go red. | FR-085: "…and every `cfg(not(windows))` test of SC-033 to SC-036 and SC-042 (plan §4.13 lists them) joins `cargo.macos` and `cargo.linux` only". SC-035: `pac_fallback_to_static_proxy` is `cfg(not(windows))` like `wpad_discovery_walk` (their Windows twins are SC-041's `os_pac_failure_cases` / `os_pac_walk`); PENDING-CI on the macOS and Linux legs for both. |
| SI-40 | SC-041 `os_pac_failure_cases` "one `warn!` (`E_PAC_FETCH` or `E_PAC_RESULT`)" (`spec.md:1325-1326`) | WinHTTP reports a script that fails to run as `ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT` ("An error occurred executing the script code", the `WinHttpGetProxyForUrl` and Ex pages), which is FR-068's `E_PAC_SCRIPT` class. | SC-041: "one `warn!` (`E_PAC_FETCH`, `E_PAC_SCRIPT` or `E_PAC_RESULT`)"; the test injects one failure of each code. |
| SI-41 | FR-090 "Files outside `apps/krate-manager/**` this phase may change: …" (`spec.md:886-898`); SC-044 "`git diff` of the roadmap touches only FR-090's lines" | The plan has the build add the R-15 auto-detect measurement to `specs/krate-manager/probes/results.md` (§2, §4.14; spec Clarifications R-15 asks for the record), but FR-090 does not list that file; the only admission is tasks.md TA-13 (b) (`tasks.md:234`), which is not a requirement, so the close's `git diff --stat` scope check (§7.2 FR-090) would report it as out of scope (review round 3). | FR-090 gains: "`specs/krate-manager/probes/results.md`, add-only: exactly one new table row, the R-15 Windows auto-detect measurement (plan §4.14), and no other change to that file". SC-044 (and the L-SCOPE close check) expect that one added row: the file's `git diff` is one `+` line and no `-` line. |

### 0.3 Precondition rulings for the user (the spec makes each one the user's, FR-048, FR-049, FR-080; R-8 to R-14 opened by R-1; R-16 and R-17 opened by R-15)

| Id | Status | Question | Recommended | Alternatives and their cost |
|---|---|---|---|---|
| R-1 | **Decided 2026-10-31: overturned.** PAC and WPAD in v1 on all three OSes, bundled JavaScript engine; environment variables still win over OS settings (spec Clarifications 2026-10-31 R-1, `spec.md:1508-1514`); the order inside the OS settings is the spec's FR-069 (WPAD, PAC URL, static proxy as the fallback); NO_PROXY applies (FR-048(c)) (this plan §0.1 PD-5, PD-25 to PD-34, §4.15) | PAC and WPAD (FR-048 (b)) | Round-0 recommendation, **not taken**: leave out of v1. Static proxies (environment or OS) work; PAC/WPAD with no static proxy is a direct connection plus one `warn!` naming the setting; the user can set `HTTPS_PROXY`. A failed direct connection falls back to the cached feed (FR-055). | (i) Windows-only PAC/WPAD through `WinHttpGetProxyForUrl` (OS-evaluated, documented; no new crate, `windows-sys` feature already needed): one OS gets it, two do not. (ii) A JavaScript engine to run PAC on every OS: a large third-party dependency for a feed of 1,246 bytes. |
| R-2 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 R-2) | Windows reader crate (FR-049, FR-080) | **`windows-sys` 0.61.2** (already in `Cargo.lock:4092-4099`, new direct dependency of `src-tauri` on Windows only, features `Win32_Foundation`, `Win32_Networking_WinHttp`): `WinHttpGetIEProxyConfigForCurrentUser`, documented, gives auto-detect, PAC URL, proxy and bypass. Needs `unsafe` FFI (one call, four `GlobalFree`s). | `winreg` 0.56.0 (already in `Cargo.lock:4194-4201` through `embed-resource`; safe API, what `ureq`'s `win-system-proxy` and `hyper-util` read, `proxy.rs:252-276`, `matcher.rs:695-723`): no `unsafe`, but no documented auto-detect flag (SI-9). |
| R-3 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 R-3) | macOS reader crate | **`system-configuration` 0.8.0** (2026-08-11) + `system-configuration-sys` 0.6.0: two new crates; `core-foundation` 0.10.1 and `bitflags` 2 are already locked (`Cargo.lock:346`, `:119`). `SCDynamicStoreBuilder::new(..).build()?.get_proxies()` (`dynamic_store.rs:68,105,202`). | (i) `system-configuration` 0.7.0, the version `hyper-util` requires (`Cargo.toml:281-283`), so Phase 11's updater could share it, but it pulls `core-foundation` 0.9, a third new crate. (ii) Parse `/usr/sbin/scutil --proxy` text: no crate, but the output format is undocumented (searched this session). |
| R-4 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 R-4) | Linux desktop coverage | **GNOME and KDE**, chosen by `XDG_CURRENT_DESKTOP` (KDE when its colon-separated list contains `KDE`, GNOME otherwise); needs the three Linux names of SI-3 and no crate. | GNOME only: no extra environment names, KDE users rely on `HTTPS_PROXY`. |
| R-5 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 R-5) | `ureq` features | **Defaults kept + `platform-verifier`, `socks-proxy`** (PD-2): `webpki-roots` compiled in, unused, proven unused by SC-026. | `default-features = false` with the internal `_ring` feature: a smaller binary, on a feature the crate tells users not to use. |
| R-6 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 R-6) | A macOS-only core dependency so the `CFDictionary` conversion can be tested (FR-080 makes a reader dependency the user's) | **`core-foundation` 0.10.1 as `[target.'cfg(target_os = "macos")'.dependencies]` of `crates/core`** (already in `Cargo.lock:346`, the version R-3's `system-configuration` 0.8.0 builds on; no new crate). It holds only `sysproxy_cf::from_cf_proxies(&CFDictionary<CFString, CFType>) -> Vec<(String, ScValue)>`, tested on the macOS leg with a dictionary built by `CFDictionary::from_CFType_pairs` (core-foundation 0.10.1 `src/dictionary.rs:43`). `[dependencies]` stays the FR-080 set; compliance's `cargo tree` must show one `core-foundation`. | Keep the conversion in `src-tauri`, untested: a wrong downcast or key spelling ships with every lane green (the round-1 xplat finding), recorded as an accepted risk. |
| R-7 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 R-7) | A contract-valid document the model cannot fully hold (PD-8 range, PD-21 asset name; PD-22's v1 entries follow the same rule) | **Drop only the affected asset (or v1 entry) with one `warn!` naming its code and path; the verdict is the contract's.** Node's validator and `dry_run` accept these documents, so a rejection would let a deployable feed block every manager's updates (D1), and the dropped asset is simply not offered. | Whole-document rejection (`E_FEED_INVALID` with a client-only finding): strictest, but breaks FR-007 / SC-002 verdict parity and turns a generator slip into an update outage for every user. |
| R-8 | **Decided 2026-10-31: as recommended, per R-15 on macOS and Linux only** (spec Clarifications 2026-10-31 part 2, `spec.md:1618-1619`; a `cfg(not(windows))` dependency, PD-25) | The bundled JavaScript engine (FR-073(d)) | **`rquickjs` 0.14.0** (2026-09-18, MSRV 1.87, MIT; QuickJS-ng vendored; default features only), PD-25. It meets FR-068 by the engine's own means: a wall-clock interrupt and a heap limit, both measured this session (300 ms deadline → interrupted at 300 ms, also inside a catastrophic regex; 16 MiB limit → error, context still usable). Binary +1,034,752 bytes measured on Windows x64 release; under R-15 that growth moves to the macOS and Linux binaries (unmeasured there, about the same per macOS slice) and the Windows binary carries none. New crates for the macOS and Linux targets: `rquickjs`, `rquickjs-core`, `rquickjs-sys`, `allocator-api2` (`hashbrown` 0.17.1, `foldhash`, `equivalent`, `cc` are already locked); `Cargo.lock` lists them for every target. Pre-generated bindings ship for `aarch64-apple-darwin`, `x86_64-apple-darwin` and `x86_64-unknown-linux-gnu` (`rquickjs-sys` 0.14.0 `src/bindings/`), so no `bindgen` / libclang on any leg. **L-XCHECK:** `rquickjs-sys`'s build script needs a C compiler for the target; on this box `cargo check --target x86_64-unknown-linux-gnu` fails with `failed to find tool "x86_64-linux-gnu-gcc"` and `--target aarch64-apple-darwin` with `failed to find tool "cc"` (measured), the same toolchain class as `ring`'s (tasks TA-7); the Linux engine builds and runs natively in the local L-WSL lane (§8). | (i) `boa_engine` (pure Rust): 0.22.0 needs Rust 1.91 (`Cargo.toml:14`), above the workspace's 1.90; no heap limit and no interrupt (`src/vm/runtime_limits.rs:37-94`), and with `panic = "abort"` an allocation failure aborts the process. (ii) Out of process: superseded by R-15 on Windows. |
| R-9 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 part 2, `spec.md:1620-1622`) | WPAD discovery per OS (FR-066): DHCP option 252, DNS, devolution | **Windows asks the OS** (WinHTTP's own DHCP-then-DNS rules; under R-15 inside the OS evaluation call, PD-30 / PD-35, no separate `WinHttpDetectAutoProxyConfigUrl`). **macOS and Linux: the single DNS candidate `http://wpad/wpad.dat`**, completed by the OS resolver's search list, as Chromium does; **no devolution, no DHCP** (PD-30). | (i) Devolution stopping at two labels: reaches `wpad.co.uk` from `host.a.example.co.uk`. (ii) Devolution bounded by the Public Suffix List: a new crate and its data. (iii) DHCP 252 on macOS / Linux: a lease API not verified. |
| R-10 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 part 2, `spec.md:1623-1624`; FR-067 and FR-092 now state it for both evaluators) | Does an answer entry tried after a failed first entry belong to FR-055 / FR-059's one attempt? | **Yes:** the ordered list is walked inside the single request chain, each entry at most once, advancing only on no-response (PD-33); no second walk, no retry of an entry; the chain deadline bounds the whole walk. The Windows entry list goes through the same parser and walk (PD-36). | First entry only: `PROXY a; DIRECT` would never reach `DIRECT` when `a` is down. |
| R-11 | **Withdrawn (challenge round 2, 2026-10-31)** | (Round 1: what "environment first, then the OS static proxy, then PAC/WPAD" means when the OS has both a static proxy and a PAC source.) | None. The quoted order is not in the R-1 ruling (`spec.md:1508-1514`); the spec's FR-069 WinHTTP order is the requirement and the plan follows it (PD-5). | — |
| R-12 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 part 2, `spec.md:1625-1626`; macOS and Linux, FR-094) | Real DNS for `dnsResolve` / `isResolvable` / `isInNet`, whose `std` lookup has no timeout | **Bounded lookup on a helper thread, abandoned (never killed) after `PAC_DNS_WAIT` = 1 s; at most 16 distinct names per session, memoised** (PD-29). On Windows no PAC name is resolved by the manager (R-15). | (i) Blocking lookups on the check thread. (ii) No DNS. |
| R-13 | **Decided 2026-10-31: as recommended for macOS and Linux; Windows reads no offset** (spec Clarifications 2026-10-31 part 2, `spec.md:1627-1629`; FR-095) | Local time for `weekdayRange` / `dateRange` / `timeRange` without the `"GMT"` argument; `std` has no time zone | **The OS local offset per PAC session:** macOS `CFTimeZone` (no new crate), Linux `date +%z` through the bounded runner (no crate); an unreadable offset is UTC plus one `warn!` (PD-31). On Windows the OS evaluates PAC, so `GetTimeZoneInformation` / `Win32_System_Time` are not used and the `windows-sys` features stay two (SI-32 withdrawn). | UTC always. |
| R-14 | **Decided 2026-10-31: as recommended** (spec Clarifications 2026-10-31 part 2, `spec.md:1630-1631`; FR-064, SC-033; it governs the in-process engine, macOS and Linux) | The script's world beyond the ruled helper set | **Exactly the ruled helpers plus `alert` (a no-op)**; intrinsics `Eval`, `RegExp`, `RegExpCompiler`, `Json` only; no `Date`, no IPv6 `*Ex` variants, no `convert_addr`; reversed ranges match only their endpoints (MDN, SI-35). A script that calls a missing name throws: `E_PAC_SCRIPT` and the fallback (FR-068). On Windows the script's world is WinHTTP's, not the manager's. | (i) Add the `*Ex` set. (ii) Add `Date`. (iii) Wrapping reversed ranges. |
| R-15 | **Decided 2026-10-31: a design the plan did not list** (spec Clarifications 2026-10-31 part 2, `spec.md:1632-1638`; FR-064, FR-092, FR-093) | Where PAC runs, given the in-process engine's residual risk (§6) | **As ruled:** on Windows the OS evaluates PAC (auto-detect and / or the configured AutoConfigURL from the R-2 reader), QuickJS is neither compiled nor linked there (`[target.'cfg(not(windows))'.dependencies]`, PD-25); macOS and Linux keep the in-process `rquickjs` evaluator; WPAD on all three OSes; the RustSec advisory check for `rquickjs`, `rquickjs-core`, `rquickjs-sys` joins the Phase 12 release checklist (FR-093, §4.14). **Measured for the record** (the user, read-only, 2026-10-31): on the user's own, not fresh, Windows 11 profile HKCU `…\Internet Settings\Connections` value `DefaultConnectionSettings` has flags byte `0x09`: auto-detect on, no PAC URL, no manual proxy. So on that profile the Windows PAC path runs by default with no environment proxy; the build records the measurement in `specs/krate-manager/probes/results.md` (§4.14, tasks TA-13 (b)). **The ruled call cannot be relied on to stay out of process (R-16)**, so this plan is written to R-16's recommendation. PD-25, PD-27 to PD-36, §4.5.1, §4.10, §4.15, §6. | The round-2 options (a) a child process, (b) `https` scripts only, (c) accept the in-process risk everywhere: superseded by the ruling. |
| R-16 | **Decided 2026-10-31: `WinHttpGetProxyForUrlEx`, as recommended** (spec Clarifications part 3) | R-15 names `WinHttpGetProxyForUrl`. Microsoft's documentation does **not** guarantee that call runs the script outside the manager process on Windows 10 22H2 / 11 (the Q6 floor, roadmap line 29). Which WinHTTP call evaluates PAC on Windows? | **`WinHttpGetProxyForUrlEx`** (PD-35): "This function always executes out-of-process" (https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpgetproxyforurlex; minimum client Windows 8). It also "provides the application with the full proxy list that was returned by the PAC script allowing the application to better handle failover to "DIRECT" and to understand SOCKS" (same page), which is R-10's walk. Same two `windows-sys` features: every symbol is in `Win32_Networking_WinHttp` plus `ERROR_IO_PENDING` in `Win32_Foundation` (§1.2). Cost: the asynchronous form (a `WINHTTP_FLAG_ASYNC` session, which `WinHttpCreateProxyResolver` requires; a status callback; a resolver handle; `WinHttpGetProxyResult` / `WinHttpFreeProxyResult`): about 80 lines of `unsafe` in `os_proxy.rs`, untested by design like the other OS calls. Its cancel is closing the resolver handle, which kills nothing. The spec edits are SI-38. | (i) **`WinHttpGetProxyForUrl` as ruled.** It hands the work to the WinHTTP Web Proxy Auto-Discovery service when it can, but "If WinHTTP cannot delegate WPAD protocol processing to an external, out-of-process Web Proxy Auto-Discovery service, WinHttpGetProxyForUrl loads the COM runtime within the application process", where "WinHTTP uses Microsoft JScript component to execute the script" (https://learn.microsoft.com/en-us/windows/win32/winhttp/autoproxy-issues-in-winhttp), and "By default, WinHTTP is configured to fall back to auto-discover a proxy in-process" (https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_autoproxy_options). So an untrusted script can still run in the manager's process (in Microsoft's engine instead of QuickJS), the risk R-15 meant to remove. Its `WINHTTP_PROXY_INFO` answer is a string list (`([<scheme>=][<scheme>"://"]<server>[":"<port>])` items separated by semicolons or whitespace, plus a bypass list with `<local>`, https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_proxy_info). It maps onto the walk as follows: `dwAccessType` `WINHTTP_ACCESS_TYPE_NO_PROXY` or a null `lpszProxy` → `DIRECT`; a bypass-list match of the hop host (`http::bypasses`, PD-18) → `DIRECT`; otherwise each item in order, a `scheme=` item kept only for the hop's scheme, rendered `PROXY` / `HTTPS` / `SOCKS` into `parse_answer`. That list loses a `DIRECT` failover entry and SOCKS (the Ex page's own comparison), so `PROXY a; DIRECT` with `a` down ends `E_FEED_OFFLINE` and the cache, not `DIRECT`. (ii) **The same call with `WINHTTP_AUTOPROXY_RUN_OUTPROCESS_ONLY`**, the flag that disables the in-process fallback. It is documented "available on Windows Server 2003 only" (same options page), so its effect on Windows 10 / 11 is undocumented; proving it needs a probe that stops `WinHttpAutoProxySvc` and watches the call, which changes system state and was not run. Same answer-shape loss as (i). Either alternative changes only `os_proxy.rs`'s call and the shape of `OsProxyEntry`; SI-38 is then withdrawn. |
| R-17 | **Decided 2026-10-31: `FALSE`, as recommended** (spec Clarifications part 3) | Should the Windows OS evaluation offer the user's domain credentials to a PAC server that challenges with NTLM / Negotiate (`WINHTTP_AUTOPROXY_OPTIONS.fAutoLogonIfChallenged`)? | **No (`FALSE`)**: "If this flag is FALSE and authentication is required to download the PAC file, the WinHttpGetProxyForUrl function fails" (options page; the Ex call takes the same structure). The result is `E_PAC_FETCH`, one `warn!` and the FR-068 fallback. With `FALSE` the out-of-process service also caches the PAC URL and script machine-wide ("if the out-of-process service is used, and the fAutoLogonIfChallenged flag … is true, then the autoproxy URL and script are not cached", https://learn.microsoft.com/en-us/windows/win32/winhttp/autoproxy-cache). One call per hop, no retry (FR-059). | (i) `TRUE` on every call (Microsoft's samples do this): an authenticated corporate PAC works, but the user's domain credentials are offered automatically to whatever server answers the PAC URL or the WPAD lookup, and the service cache is off. (ii) Microsoft's two-step pattern: `FALSE`, then `TRUE` after `ERROR_WINHTTP_LOGIN_FAILURE` (same cache page). That is a second call per hop, which FR-059's one-attempt rule would have to admit. |

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-10-30 at `a28b1181`)

| Fact | Where |
|---|---|
| `AppContext { roots, settings, feed_url }`, `Clone, Debug, PartialEq`; `assemble_context(roots, feed_url)` → `assemble_with_os(os, roots, feed_url)`; `reassemble_with_os` copies `feed_url`; `parse_dev_feed_url` | `crates/core/src/context.rs:47-53`, `:66-102`, `:133-161`, `:190-199` |
| 27 call sites of `assemble_*` / `reassemble*` in `context_tests.rs`, 3 in `tests/config_guard.rs` (as needles), 1 in `src-tauri/src/lib.rs` | `grep -c` this session |
| `Platform { fn os(&self) -> Os; fn default_roots(&self, env: &OsEnv) -> … }`; per-OS modules behind `cfg(target_os)`; `APP_IDENTIFIER` | `crates/core/src/platform/mod.rs:55-58`, `:60-71`, `:9` |
| Only three `impl Platform` exist (Windows, macOS, Linux) | `grep -rn "impl Platform"` |
| `FORBIDDEN_OUTSIDE_PLATFORM` (incl. `krateaudio.com`, `api.github.com`, `"/usr/`), `FS_TOKENS`, `PLATFORM_LITERALS` (27), `D6_SITES` (5 files) / `D6_TOTAL = 7`, `FR007_DEPENDENCIES` (6) | `crates/core/tests/injection.rs:12-20`, `:22-37`, `:40-68`, `:70-77`, `:82-83` |
| `scan_core_file` (platform literal / FS token / substring rules), `is_scanned` skips `test_support.rs` and `*_tests.rs`, `core_dependencies_are_the_fr007_set`, `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` (`var_os` only in `os_roots.rs`) | `injection.rs:339-386`, `:453-455`, `:818-823`, `:826-849` |
| `SRC_TAURI_DEPENDENCIES` = 5 keys; `LIB_RS_STRINGS`; `check_src_tauri_manifest` reads only `[dependencies]`, `[features]` (exactly `dev-feed = []`), `[lib]`, `[[bin]]` | `crates/core/tests/config_guard.rs:32-43`, `:586-646` |
| `SettingsIo` (7 methods, by-value temp), `save_with` step order, `StdSettingsIo::persist` drops `PersistError.file` | `crates/core/src/settings.rs:221-232`, `:281-307`, `:265-267` |
| Settings fault test: after a `SyncDir` fault the new bytes are in place | `crates/core/src/settings_tests.rs:410-414` |
| `canonical_temp_dir`, `temp_roots`, `fresh_temp_roots`; `test_support` behind `cfg(any(test, feature = "test-support"))` | `crates/core/src/test_support.rs:30-38`, `:117-126`; `crates/core/src/lib.rs:10-11` |
| Phase 4's `Capture` logger and `log::set_logger(&CAPTURE).expect(…)` | `crates/core/src/context_tests.rs:446-484`, `:497` |
| `FEED_URL`, `run()`, the `dev-feed` branch; `dev_feed::read`; `FromOs for Roots` with two `#[expect(clippy::disallowed_methods)]` | `src-tauri/src/lib.rs:16`, `:18-70`, `:26-33`; `src-tauri/src/dev_feed.rs:8-12`; `src-tauri/src/os_roots.rs:7-33` |
| Production logger records Info and above | `src-tauri/src/log_sink.rs:15`, `:54` |
| Core `[features]` `test-support = []`; `[dependencies]` six; workspace pins incl. `tempfile 3.27.0`, `url 2.5.8`, `[profile.release] strip = true, panic = "abort"`, `workspace.lints.clippy allow_attributes = "deny"` | `crates/core/Cargo.toml:9-19`; `Cargo.toml:13-33` |
| Lock: `base64 0.23.1`, `regex 1.13.1`, `flate2 1.1.10`, `core-foundation 0.10.1`, `bitflags 2.13.2`, `windows-sys 0.61.2`, `winreg 0.56.0`, `reqwest 0.13.5`, `hyper 1.12.0`, `tokio 1.54.1`; no `ureq`, `rustls`, `ring`, `webpki-roots`, `system-configuration` | `apps/krate-manager/Cargo.lock` (grep this session; lines 92, 2453, 770, 346, 119, 4092, 4194, 2482, 1259, 3400) |
| `manager-ci.yml`: path filters; matrix `[windows-latest, macos-latest, ubuntu-24.04]`; `cargo test --workspace --locked … | tee`; "required tests ran" matcher `^test (\S+)(?: - should panic)? \.\.\. ok$`, exactly one passing line per name; "dev-feed absent"; "Checkout unchanged" | `.github/workflows/manager-ci.yml:5-15`, `:31-32`, `:115-118`, `:135-160` (`:146`), `:182-192`, `:257-266` |
| Shape test: `PATHS`, `STEPS`, `ASSERTION_STEPS`, `COMMANDS`, `SERIAL_FLAGS` (incl. `--test-threads`), `DEV_FEED_STEPS`, `EXPECTED_REQUIRED` verbatim check | `tools/feed-contract/tests/manager-ci-shape.test.js:147`, `:156`, `:192`, `:205`, `:298-300`, `:315`, `:318`, `:527` |
| `feed-tools.yml` TAP plan `PLAN = "1..59"` | `.github/workflows/feed-tools.yml:86` |
| `contract.js`: patterns (`CORE`, `SEMVER_RE`, `PLUGIN_NAME_RE`, `RESERVED_STEM_RE`, `ASSET_NAME_RE`, `URL_RE`), `INSTALL_PATTERNS`, `checkInstallValues` (throws at the **first** failing field), `findInstallDuplicates`, `definedPlatformKeys`, `parseTrustedComment`, `parseMinisigText` / `decodeCanonicalBase64`, `pointer`, `ajvFindings`, `feedCodeFindings`, `validateFeed` | `tools/feed-contract/contract.js:120-151`, `:226-242`, `:249-271`, `:276-280`, `:299-318`, `:356-384`, `:402-413`, `:421-478`, `:485-492` |
| C1 schema keywords and patterns (`$defs.url` `^https://[^\s]+$` + `not` `/releases/latest/`; `$defs.reservedStem` with `(\..*)?$`; `platform.size` `maximum` 2147483647; `payload.files` `minimum` 1, no maximum; `min_os` `^[0-9]+(\.[0-9]+){1,3}$`) | `tools/feed-contract/versions.v2.schema.json` (whole file read this session) |
| `cases.json`: 90 rows (10 accept, 1 accept-without-platforms, 79 error); every error pair form listed; only `dup-vst3_bundle-casefold`, `plugin-key-pattern`, `plugin-key-reserved` have plugin keys out of bytewise order | `node` over `fixtures/feeds/cases.json` and every fixture with a `plugins` object, this session |
| Reader rules 1-8 | `tools/feed-contract/CONTRACT.md:114-142` |
| `gen-feed/`: 69 case directories, 16 `expected.legacy.json` | `ls` this session |

### 1.2 External facts (verified 2026-10-30)

| Fact | Source |
|---|---|
| `ureq` 3.5.0 features: `default = ["rustls", "gzip"]`; `rustls = ["rustls-no-provider", "_ring", "rustls-webpki-roots"]`; `platform-verifier = ["dep:rustls-platform-verifier"]`; `socks-proxy = []`; `win-system-proxy = ["dep:winreg"]`; `rustls-platform-verifier = { version = "0.7.0", default-features = false }`; `rustls = { version = "0.23.46", default-features = false, features = ["logging", "std", "tls12"] }` | `Cargo.toml.orig:20-60`, `:78`, `:82` of https://static.crates.io/crates/ureq/ureq-3.5.0.crate (downloaded to the scratchpad this session) |
| `build_config`: provider = `TlsConfig` provider, else the process default, else ring; `RootCerts::PlatformVerifier` → `rustls_platform_verifier::Verifier::new(provider)?` as a custom verifier; `RootCerts::WebPki` → `webpki_roots::TLS_SERVER_ROOTS` | ureq `src/tls/rustls.rs:136-190` |
| `TlsConfig::builder()`, `TlsConfigBuilder::root_certs(RootCerts)`, `TlsConfig::root_certs(&self) -> &RootCerts` (default `WebPki`); `RootCerts::{Specific, PlatformVerifier, WebPki}`, `#[non_exhaustive]` | ureq `src/tls/mod.rs:124`, `:155`, `:221`, `:335-353` |
| `ConfigBuilder::tls_config`, `proxy(Option<Proxy>)`, `accept(impl Into<AutoHeaderValue>)`, `max_redirects`, `https_only`, `http_status_as_error`, `timeout_global`, `timeout_connect`, `user_agent`; getters `Config::tls_config()`, `proxy()`, `https_only()`, `max_redirects()`; `impl<S: AsRef<str>> From<S> for AutoHeaderValue` | ureq `src/config.rs:520-663`, `:761`, `:848`, `:284-342`, `:962` |
| `Config::default()` sets `proxy: Proxy::try_from_env()`; `try_from_env` reads `ALL_PROXY`, `all_proxy`, `HTTPS_PROXY`, `https_proxy`, `HTTP_PROXY`, `http_proxy` and `NO_PROXY`/`no_proxy` | ureq `src/config.rs:1065-1073`; `src/proxy.rs:232-246`, `:584-593` |
| `Proxy::new(&str)` parses scheme `http`, `https`, `socks4`, `socks4a`, `socks`/`socks5`, `socks5h`; userinfo decoded; no scheme → `http` | ureq `src/proxy.rs:159-212`, `:496-502` |
| An `http`/`https` proxy is always used through `CONNECT host:port` (also for `http` targets), sending `Host`, `User-Agent`, `Proxy-Connection: Keep-Alive` and, with credentials, `Proxy-Authorization: Basic …`; a non-200 answer is `Error::ConnectProxyFailed("proxy server responded 407/…")`; no answer is `ConnectProxyFailed("proxy server did not respond")` | ureq `src/unversioned/transport/connect.rs:38-58`, `:109-158`; `src/config.rs:251-259` |
| `ureq`'s `NoProxyEntry`: `*`, `*suffix`, `.suffix`, `prefix*`, `prefix.`, exact; no IP range | ureq `src/proxy.rs:612-640` |
| `ureq::Error` is `#[non_exhaustive]`; variants include `StatusCode`, `Io`, `Timeout`, `HostNotFound`, `ConnectionFailed`, `Protocol`, `Tls`, `Rustls`, `RequireHttpsOnly`, `Decompress`, `ConnectProxyFailed`, `InvalidProxyUrl`, `TlsRequired`, `BodyStalled`; `pub use ureq_proto::http`; `Body::as_reader(&mut self)` | ureq `src/error.rs:8-190`; `src/lib.rs:583-640`; `src/body/mod.rs:230` |
| `rustls-platform-verifier` 0.7.1 (2026-09-24, MSRV 1.85; `ureq`'s `^0.7.0` and `reqwest`'s `>=0.6.0, <0.8.0` resolve to it); every verification failure is a `rustls::Error` (`InvalidCertificate(…)` or `General(…)`), on Windows through CryptoAPI, on Apple through Security.framework, on Linux through `rustls-native-certs` + `webpki`; Windows depends on `windows-sys >=0.52.0, <0.62.0` | crates.io API this session; https://static.crates.io/crates/rustls-platform-verifier/rustls-platform-verifier-0.7.1.crate `src/verification/windows.rs:33,183,531-535,699-745`, `apple.rs:81,117,139,222-232`, `Cargo.toml:94-112,141-142` |
| `tauri-plugin-updater` 2.14.1: `default = ["rustls-tls", "system-proxy", "zip"]`; `rustls-tls = ["reqwest/rustls-no-provider", "dep:rustls"]`; `rustls = { version = "0.23", features = ["ring"] }`; `system-proxy = ["reqwest/system-proxy"]`; `reqwest` `features = ["json", "stream"], default-features = false`; installs `rustls::crypto::ring::default_provider()` as the process default when none is set | https://static.crates.io/crates/tauri-plugin-updater/tauri-plugin-updater-2.14.1.crate `Cargo.toml:62-74`, `:106-118`; `src/updater.rs:578-584` |
| `reqwest` 0.13.5: `rustls-no-provider = ["dep:rustls-platform-verifier", "__rustls"]`; `system-proxy = ["hyper-util/client-proxy-system"]`; `hyper-util` 0.1.21 `client-proxy-system` = `system-configuration` `^0.7` (macOS) + `windows-registry` (Windows); environment first, system fills empty values | local registry `reqwest-0.13.5/Cargo.toml:169-177`, `:185`, `:399-400`; `hyper-util-0.1.21/Cargo.toml:77-79`, `:281-287`; `src/client/proxy/matcher.rs:238-247`, `:570-723` |
| `WinHttpGetIEProxyConfigForCurrentUser(*mut WINHTTP_CURRENT_USER_IE_PROXY_CONFIG) -> BOOL`; members `fAutoDetect` ("automatically detect settings"), `lpszAutoConfigUrl`, `lpszProxy` ("if … specifies 'use a proxy server'"), `lpszProxyBypass`; the caller frees non-NULL strings with `GlobalFree`; `ERROR_FILE_NOT_FOUND` when no settings exist | https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpgetieproxyconfigforcurrentuser ; https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_current_user_ie_proxy_config ; `windows-sys` 0.61.2 `src/Windows/Win32/Networking/WinHttp/mod.rs:16,424`, `Foundation/mod.rs:7`, features `Win32_Foundation`, `Win32_Networking_WinHttp` (`Cargo.toml:85,133`) |
| `system-configuration` 0.8.0: `pub extern crate core_foundation`, `pub extern crate system_configuration_sys as sys`; `SCDynamicStoreBuilder::new(name)`, `.build() -> Option<SCDynamicStore>`, `get_proxies() -> Option<CFDictionary<CFString, CFType>>`; `system-configuration-sys` 0.6.0 statics `kSCPropNetProxiesHTTPSEnable/HTTPSProxy/HTTPSPort`, `ExceptionsList`, `ProxyAutoConfigEnable`, `ProxyAutoConfigURLString`, `ProxyAutoDiscoveryEnable` | crates.io API this session; `system-configuration-0.8.0/src/lib.rs:24-26`, `src/dynamic_store.rs:68,105,202`; `system-configuration-sys-0.6.0/src/schema_definitions.rs:442,470,484,488,490` |
| GNOME `org.gnome.system.proxy`: `mode` (`none`/`manual`/`auto`), `autoconfig-url` (s), `ignore-hosts` (as, default `['localhost', '127.0.0.0/8', '::1']`); `org.gnome.system.proxy.https` `host` (s), `port` (i) | https://gitlab.gnome.org/GNOME/gsettings-desktop-schemas/-/raw/master/schemas/org.gnome.system.proxy.gschema.xml.in |
| `gsettings get SCHEMA KEY` prints the value as serialized GVariant; GVariant text: strings in `'…'` or `"…"` with `\` escapes, arrays `[…, …]`, an empty array may need a type annotation | https://manpages.ubuntu.com/manpages/noble/man1/gsettings.1.html ; https://docs.gtk.org/glib/gvariant-text-format.html |
| KDE `kioslaverc` group `Proxy Settings`: `ProxyType` (enum order `NoProxy, ManualProxy, PACProxy, WPADProxy, EnvVarProxy`, default 0), `<protocol>Proxy` (`httpsProxy`), `NoProxyFor`, `Proxy Config Script`, `ReversedException`; `"http://host 8080"` is read as `http://host:8080` (last space before an integer port) | https://api.kde.org/legacy/4.14-api/kdelibs-apidocs/kio/html/kprotocolmanager_8cpp_source.html (lines 309-387, 985); https://api.kde.org/legacy/4.14-api/kdelibs-apidocs/kio/html/classKProtocolManager.html |
| Round-1 additions (read 2026-10-30/31): ureq `Proxy::new` → `new_with_flag` parses `http::Uri`, `decode_userinfo` fails non-UTF-8 percent-decoding with `InvalidProxyUrl` (`src/proxy.rs:159-212`, `:484-489`); a corrupt gzip body is `Error::Decompress("gzip", e).into_io()` (`src/body/gzip.rs:23`), and `From<io::Error> for Error` unwraps a wrapped ureq error (`src/error.rs:218-227`); `std::process::Child` has no `Drop` and an un-waited exited child is a zombie (https://doc.rust-lang.org/std/process/struct.Child.html); core-foundation 0.10.1 `CFDictionary::from_CFType_pairs` (`src/dictionary.rs:43`), `CFType::downcast` (`src/base.rs:84`), `CFNumber::to_i64` (`src/number.rs:45`), `CFArray::from_CFTypes` (`src/array.rs:82`); windows-sys 0.61.2 `GlobalAlloc` needs `Win32_System_Memory` (`src/Windows/Win32/System/Memory/mod.rs:30`, `Cargo.toml:222`), `GlobalFree` is in `Win32_Foundation` (`Foundation/mod.rs:7`) | ureq 3.5.0 crate (scratchpad download this session); local cargo registry `core-foundation-0.10.1`, `windows-sys-0.61.2` |
| `rquickjs` 0.14.0 / `rquickjs-core` 0.14.0 / `rquickjs-sys` 0.14.0 (all 2026-09-18; `rquickjs` MSRV 1.87): `default = ["std"]`; `rust-alloc` "Use Rust global allocator … otherwise libc allocator will be used"; `Runtime::new`, `set_interrupt_handler(Option<InterruptHandler>)` ("If the provided closure returns `true` the interpreter will raise and uncatchable exception"), `set_memory_limit(usize)` ("a Noop when a custom allocator is being used, as is the case for the "rust-alloc" or "allocator" features"), `set_max_stack_size(usize)` ("The default values is 256x1024 bytes"); `InterruptHandler = Box<dyn FnMut() -> bool + 'static>` without `parallel`; `Context::custom::<I: Intrinsic>`, intrinsics `Date, Eval, RegExpCompiler, RegExp, Json, Proxy, MapSet, TypedArrays, Promise, Performance, WeakRef, DOMException`; `Ctx::eval`, `Ctx::globals`; `Function::new<P, F>(ctx: Ctx<'js>, f: F) where F: IntoJsFunc<'js, P> + 'js`; deps `rquickjs-sys` + `hashbrown` 0.17; sys `build-dependencies` `cc = "1"`, `bindgen` optional; bundled bindings for 20 targets incl. `x86_64-pc-windows-msvc`, `aarch64-apple-darwin`, `x86_64-apple-darwin`, `x86_64-unknown-linux-gnu`; C sources `libregexp.c, libunicode.c, quickjs.c, dtoa.c`; QuickJS-ng (`quickjs/README.md:23-24`); libregexp calls the interrupt check (`quickjs/libregexp.c:2800-2802`, `lre_check_timeout`, `quickjs.c:49605`) | crates.io API (`/api/v1/crates/rquickjs`, `-core`, `-sys`) this session; https://static.crates.io/crates/rquickjs/rquickjs-0.14.0.crate (and `-core`, `-sys`) `Cargo.toml.orig` `[features]`; rquickjs-core `src/runtime/base.rs:50,120-128,151-169`, `src/runtime.rs:49-52`, `src/context/base.rs:64-99`, `src/context/builder.rs:51-99`, `src/context/ctx.rs:160,230`, `src/value/function.rs:41-44`; rquickjs-sys `build.rs:143,258-262`, `src/bindings/` listing |
| Scratch measurement (this session, `rquickjs = "=0.14.0"`, release profile = the workspace's `codegen-units = 1`, `lto`, `opt-level = 3`, `panic = "abort"`, `strip`, Windows x64): build 14.5 s, 0 warnings; binary 112,640 → 1,147,392 bytes; `Context::custom::<(Eval, RegExp, RegExpCompiler)>`: a PAC function returns `"PROXY a:1; DIRECT"`; `while(true){}` with a 300 ms interrupt deadline → `Err` after 300 ms; `/^(a+)+$/.test('a'.repeat(40)+'b')` → `Err` after 300 ms; a doubling string with `set_memory_limit(16 MiB)` → `Err` at once; `1+1` afterwards → 2; `typeof Date/require/std/os/fetch/setTimeout/print/console` all `"undefined"`; `cargo tree -e normal`: `rquickjs → rquickjs-core → {hashbrown 0.17.1 → allocator-api2, equivalent, foldhash; rquickjs-sys}`; `cargo check --target x86_64-unknown-linux-gnu` → `failed to find tool "x86_64-linux-gnu-gcc"`, `--target aarch64-apple-darwin` → `failed to find tool "cc"` | scratchpad crate `jsprobe` (outside the repo, not committed) |
| `boa_engine` 0.22.0 (2026-08-28) `rust-version = "1.91.0"`; 0.21.1 (2026-03-29) MSRV 1.88; `RuntimeLimits`: loop iteration, backtrace, stack size, recursion limits only; no memory limit or interrupt hook in `src/` (grep) | crates.io API this session; https://static.crates.io/crates/boa_engine/boa_engine-0.22.0.crate `Cargo.toml:14`, `src/vm/runtime_limits.rs:37-94` |
| `WinHttpDetectAutoProxyConfigUrl(DWORD dwAutoDetectFlags, LPWSTR *ppwstrAutoConfigUrl) -> BOOL` (**not used since R-15**: discovery is inside the OS evaluation call, PD-30); "If both the DHCP and DNS auto detect flags are set, DHCP is used first" | https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpdetectautoproxyconfigurl ; `windows-sys` 0.61.2 `src/Windows/Win32/Networking/WinHttp/mod.rs:9,294-295` |
| Chromium WPAD: `const char kWpadUrl[] = "http://wpad/wpad.dat";` "we not use DNS devolution to find the WPAD host, since that could be dangerous … Instead we directly resolve "wpad", and let the operating system apply the DNS suffix search paths" | https://chromium.googlesource.com/chromium/src/+/main/net/proxy_resolution/pac_file_decider.cc |
| PAC helpers and answers: predefined `isPlainHostName, dnsDomainIs, localHostOrDomainIs, isResolvable, isInNet, dnsResolve, convert_addr, myIpAddress, dnsDomainLevels, shExpMatch, weekdayRange, dateRange, timeRange, alert`; `gmt` omitted "means local time"; reversed ranges match only the endpoints in current Firefox; `myIpAddress` falls back to the loopback address; "If the string is null, no proxies should be used"; `DIRECT`, `PROXY`, `SOCKS`, and `HTTP`, `HTTPS`, `SOCKS4`, `SOCKS5` in recent Firefox; no default port stated | https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Proxy_servers_and_tunneling/Proxy_Auto-Configuration_PAC_file |
| `ureq` 3.5.0 SOCKS: `socks4`, `socks4a` "(requires **socks-proxy** feature)", parsed at `src/proxy.rs:498-499`, SOCKS4/5 resolve locally (`:60-62`), handshake `src/unversioned/transport/socks.rs:151-166`; `Timeout` variants `PerRead, PerWrite, Global, PerCall, Resolve, Connect` ("Total connection setup timeout, including proxy DNS, negotiation, and TLS"), `SendRequest, SendBody, RecvResponse, RecvBody` | ureq 3.5.0 crate (scratchpad) `src/proxy.rs:24-62,149-150,498-499`, `src/timings.rs:14-49` |
| `GetTimeZoneInformation` (**not used since R-13 / R-15**: Windows reads no local offset; `Win32_System_Time` is not added) | https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-gettimezoneinformation ; `windows-sys` 0.61.2 `Cargo.toml:251` |
| `WinHttpGetProxyForUrl(hSession, lpcwszUrl, pAutoProxyOptions, pProxyInfo) -> BOOL`; "Free the lpszProxy and lpszProxyBypass strings … using the GlobalFree function"; errors `ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR`, `_BAD_AUTO_PROXY_SCRIPT`, `_LOGIN_FAILURE`, `_OPERATION_CANCELLED` ("usually because the handle … was closed before the operation completed"), `_UNABLE_TO_DOWNLOAD_SCRIPT`, `_UNRECOGNIZED_SCHEME`; "must be called on a per-URL basis"; minimum client Windows XP | https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpgetproxyforurl (read 2026-10-31) |
| `WINHTTP_AUTOPROXY_OPTIONS { dwFlags, dwAutoDetectFlags, lpszAutoConfigUrl, lpvReserved, dwReserved, fAutoLogonIfChallenged }`; `WINHTTP_AUTOPROXY_RUN_INPROCESS` "Executes the … (WPAD) protocol in-process instead of delegating to an out-of-process WinHTTP AutoProxy Service, if available … This option has no effect when passed to WinHttpGetProxyForUrlEx … This flag is deprecated"; `WINHTTP_AUTOPROXY_RUN_OUTPROCESS_ONLY` "By default, WinHTTP is configured to fall back to auto-discover a proxy in-process … it can be disabled using this flag … no effect when passed to WinHttpGetProxyForUrlEx … available on Windows Server 2003 only"; DHCP first, then DNS; `fAutoLogonIfChallenged` FALSE → the call fails when the PAC needs authentication | https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_autoproxy_options (read 2026-10-31) |
| WinHTTP runs PAC with "Microsoft JScript"; "If WinHTTP cannot delegate WPAD protocol processing to an external, out-of-process Web Proxy Auto-Discovery service, WinHttpGetProxyForUrl loads the COM runtime within the application process for the duration of the call"; the out-of-process service "runs under the low-privileged Local Service built-in user account" is stated for Windows Server 2003 only, as are a 60 s script run limit and a 1 MB PAC file limit ("Windows XP with SP2 and Windows Server 2003"); the script cannot instantiate ActiveX objects; "WinHTTP uses only the first proxy server in the list … does not failover"; the calls are "blocking, synchronous" and auto-detection "possibly as long as several seconds"; both flags and a URL: "first attempts auto-detection, and then … falls back to the auto-config URL" | https://learn.microsoft.com/en-us/windows/win32/winhttp/autoproxy-issues-in-winhttp ; https://learn.microsoft.com/en-us/windows/win32/winhttp/winhttp-autoproxy-api (read 2026-10-31) |
| Autoproxy cache: "If the out-of-process service is active … the cached autoproxy URL and script are available to the whole computer. However, if … fAutoLogonIfChallenged … is true, then the autoproxy URL and script are not cached"; Microsoft's improvement: `FALSE` first, `TRUE` after `ERROR_WINHTTP_LOGIN_FAILURE` | https://learn.microsoft.com/en-us/windows/win32/winhttp/autoproxy-cache (read 2026-10-31) |
| `WinHttpGetProxyForUrlEx(hResolver, pcwszUrl, pAutoProxyOptions, pContext) -> DWORD`: "fully Asynchronous and cancellable"; "always executes asynchronously and returns immediately with ERROR_IO_PENDING"; set the callback with `WinHttpSetStatusCallback` "before WinHttpCreateProxyResolver", flags `WINHTTP_CALLBACK_FLAG_REQUEST_ERROR \| WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE`; then `WinHttpGetProxyResult` on the resolver; "This function always executes out-of-process"; minimum client Windows 8. `WinHttpCreateProxyResolver`: "The session handle must be opened using WINHTTP_FLAG_ASYNC"; "When finished or cancelling an outstanding operation, close this handle with WinHttpCloseHandle". `WINHTTP_PROXY_RESULT_ENTRY { fProxy, fBypass, ProxyScheme, pwszProxy, ProxyPort }`: `fProxy` "TRUE if the result contains a proxy", `fBypass` "applies only if fProxy is FALSE" | https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpgetproxyforurlex ; https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpcreateproxyresolver ; https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_proxy_result_entry (read 2026-10-31) |
| `WINHTTP_PROXY_INFO { dwAccessType, lpszProxy, lpszProxyBypass }`: list items `([<scheme>=][<scheme>"://"]<server>[":"<port>])` "separated by semicolons or whitespace", bypass list with `<local>`. `WinHttpOpen(pszAgentW, dwAccessType, pszProxyW, pszProxyBypassW, dwFlags)`, `WINHTTP_ACCESS_TYPE_NO_PROXY`, `WINHTTP_FLAG_ASYNC`. `WinHttpSetTimeouts(hInternet, nResolveTimeout, nConnectTimeout, nSendTimeout, nReceiveTimeout)` sets "time-outs involved with HTTP transactions" and names no autoproxy behaviour | https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_proxy_info ; https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpopen ; https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsettimeouts (read 2026-10-31) |
| `windows-sys` 0.61.2 declares, all in `Win32_Networking_WinHttp` (`Cargo.toml:133`): `WinHttpCloseHandle` (`mod.rs:4`), `WinHttpCreateProxyResolver` (`:7`), `WinHttpFreeProxyResult` (`:10`), `WinHttpGetIEProxyConfigForCurrentUser` (`:16`), `WinHttpGetProxyForUrl` (`:17`), `WinHttpGetProxyForUrlEx(hresolver: *const c_void, pcwszurl: PCWSTR, pautoproxyoptions: *const WINHTTP_AUTOPROXY_OPTIONS, pcontext: usize) -> u32` (`:18`), `WinHttpGetProxyResult` (`:20`), `WinHttpOpen` (`:25`), `WinHttpSetStatusCallback` (`:44`); `WINHTTP_ACCESS_TYPE_NO_PROXY = 1` (`:237`); `WINHTTP_AUTOPROXY_AUTO_DETECT = 1`, `_CONFIG_URL = 2` (`:268-269`); `WINHTTP_AUTO_DETECT_TYPE_DHCP = 1`, `_DNS_A = 2` (`:294-295`); `WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE` (`:299`), `_FLAG_REQUEST_ERROR` (`:305`), `_STATUS_GETPROXYFORURL_COMPLETE` (`:324`), `_STATUS_REQUEST_ERROR` (`:334`); `WINHTTP_FLAG_ASYNC` (`:554`); `WINHTTP_INTERNET_SCHEME_HTTP = 1`, `_HTTPS = 2`, `_FTP = 3`, `_SOCKS = 4` (`:606-609`); structs `WINHTTP_ASYNC_RESULT { dwResult, dwError }` (`:248`), `WINHTTP_AUTOPROXY_OPTIONS` (`:277`), `WINHTTP_PROXY_RESULT { cEntries, pEntries }` (`:790`), `WINHTTP_PROXY_RESULT_ENTRY` (`:801`); `WINHTTP_STATUS_CALLBACK = Option<unsafe extern "system" fn(*mut c_void, usize, u32, *mut c_void, u32)>` (`:1118`); error constants `ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR` (`:68`), `_BAD_AUTO_PROXY_SCRIPT` (`:69`), `_LOGIN_FAILURE` (`:96`), `_OPERATION_CANCELLED` (`:99`), `_UNABLE_TO_DOWNLOAD_SCRIPT` (`:119`), `_AUTODETECTION_FAILED` (`:67`). `GlobalFree` (`Foundation/mod.rs:7`) and `ERROR_IO_PENDING = 997` (`Foundation/mod.rs:2870`) are in `Win32_Foundation` (`Cargo.toml:85`) | local cargo registry `windows-sys-0.61.2` (grep this session) |
| The PD-35 context lifetime (review round 3). WinHttpCloseHandle: "If there is a status callback registered for the handle being closed and the handle was created with a non-NULL context value, a WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING callback is made. This is the last callback made from the handle"; "Even after a call to WinHttpCloseHandle returns, the application must still be prepared to receive callbacks for the closed handle"; "it should maintain that binding until the callback function receives a WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING notification … the application must enable the WINHTTP_CALLBACK_FLAG_HANDLES flag"; "be conservative in freeing the context structure until after having received the WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING notification". WinHttpSetStatusCallback: `WINHTTP_CALLBACK_FLAG_HANDLES` "Activates when an HINTERNET handle is created or closed"; a callback set on the session is inherited by handles created from it. `WINHTTP_OPTION_CONTEXT_VALUE`: "Sets or retrieves a DWORD_PTR that contains a pointer to the context value associated with this HINTERNET handle"; the option table marks it for session and request handles (a resolver handle is not listed). `windows-sys` 0.61.2 exports `WinHttpSetOption` (`WinHttp/mod.rs:42`), `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING = 2048` (`:326`), `WINHTTP_CALLBACK_STATUS_HANDLE_CREATED = 1024` (`:327`) and `WINHTTP_OPTION_CONTEXT_VALUE = 45` (`:643`), all in `Win32_Networking_WinHttp`, but **not** `WINHTTP_CALLBACK_FLAG_HANDLES` (its flag list, `:296-308`, lacks it); the Windows SDK header defines it as `(WINHTTP_CALLBACK_STATUS_HANDLE_CREATED \| WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)` (`Windows Kits/10/Include/10.0.26100.0/um/winhttp.h:1089`), so `os_proxy.rs` defines that one constant itself | https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle ; https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsetstatuscallback ; https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags (read 2026-10-31); local registry and SDK header (grep this session) |
| Local Linux toolchain for the in-process engine: WSL Ubuntu 24.04.3 LTS (glibc 2.39, the CI leg's `ubuntu-24.04`), `cargo 1.99.1`, `rustc 1.99.1`, `gcc 13.3.0` at `/usr/bin/cc`; `~/.wslconfig` `processors=4`, `guiApplications=false`, `networkingMode=mirrored`; the repo already drives WSL (`tools/check-portability.js:248-270`) | `wsl -e bash -lc '…'` this session (read-only) |
| core-foundation 0.10.1 `CFTimeZone::system()`, `seconds_from_gmt(CFDate) -> CFTimeInterval`, `CFDate::now()` | local registry `core-foundation-0.10.1/src/timezone.rs:45,52`, `src/date.rs:35` |
| JSON Schema 2020-12 semantics, ECMA-262 `\s` / `.`, RFC 9111 `no-cache`, the live feed's caching, `base64` 0.23.1, `regex` 1.13.1, `tempfile` 3.27.0 `PersistError`, `tauri::async_runtime::spawn_blocking` | spec External facts (verified by the spec stage this session; re-used, not re-derived) |

**FR-040 / SC-017 record (the spec asks the plan to write it):** `tauri-plugin-updater` 2.14.1 with default features
uses `reqwest` 0.13 with `rustls-no-provider` (no `aws-lc-rs`), i.e. **`rustls-platform-verifier`** for trust, and
installs **`ring`** as the process-wide rustls provider. `ureq` 3.5.0 with PD-2's features uses **`rustls-platform-
verifier` 0.7.x** and **`ring`**. **Verdict: agree** — same verifier crate (one resolved version), same provider, same
`rustls` 0.23 line; the second TLS stack Phase 11 adds is `hyper-rustls` / `tokio-rustls` plumbing over the same
`rustls`. One consequence for Phase 11: once the updater installs `ring` as the process default, `ureq`'s
`build_config` step 2 picks that same provider (`tls/rustls.rs:139-143`). A second note for Phase 11: the updater's
default `system-proxy` feature reads the OS proxy through `hyper-util` with `system-configuration` `^0.7`, so with R-3's
0.8.0 the tree would hold two `system-configuration` versions unless Phase 11 turns that feature off and passes this
phase's resolved proxy to `configure_client` (`updater.rs:427`). Compliance re-records `cargo tree -e features` after
the build.

---

## 2. File layout

```
apps/krate-manager/
  Cargo.toml                        [workspace.dependencies] + ureq, rustls, base64, regex, flate2,
                                    windows-sys, system-configuration, core-foundation, rquickjs (§4.1)
  crates/core/Cargo.toml            + ureq, rustls, base64, regex; dev: flate2; not(windows): rquickjs (R-8,
                                    R-15); macOS: core-foundation (R-6); feature live-network
  crates/core/src/lib.rs            + pub mod contract, feed, http, pac, sysproxy, version; sysproxy_cf (macOS
                                    only); fixture_server (test-only)
  crates/core/src/pac.rs            + pac_tests.rs           (§4.15, R-1, R-8, R-15: shared pure half on every OS,
                                    engine items cfg(not(windows)))
  crates/core/src/sysproxy_cf.rs    + sysproxy_cf_tests.rs   (§4.6, cfg(target_os = "macos"), R-6)
  crates/core/src/contract.rs       + contract_tests.rs      (§4.2)
  crates/core/src/feed.rs           + feed_tests.rs          (§4.3)
  crates/core/src/version.rs        + version_tests.rs       (§4.4)
  crates/core/src/http.rs           + http_tests.rs          (§4.5)
  crates/core/src/sysproxy.rs       + sysproxy_tests.rs      (§4.6)
  crates/core/src/fixture_server.rs + fixture_server_tests.rs (§4.7, cfg(any(test, feature = "test-support")))
  crates/core/src/test_support.rs   changed: shared capture logger (§4.8)
  crates/core/src/context.rs        changed: AppContext.http (§4.9)
  crates/core/src/context_tests.rs  changed: new argument; uses test_support capture; http_config_is_carried
  crates/core/tests/injection.rs    changed (§4.11.1)
  crates/core/tests/config_guard.rs changed (§4.11.2)
  crates/core/tests/roster.rs       new (§4.11.3)
  crates/core/tests/live_feed.rs    new (§4.11.4)
  src-tauri/Cargo.toml              + [target.'cfg(windows)'] windows-sys, [target.'cfg(target_os = "macos")']
                                    system-configuration (§4.1)
  src-tauri/src/lib.rs              changed (§4.10)
  src-tauri/src/proxy_env.rs        new: the one proxy environment read
  src-tauri/src/os_proxy.rs         new: SystemProxyFromOs for NativePlatform (three real readers) and OsPacHost
                                    (the real PacHost: Windows OS evaluation, PD-35; macOS / Linux DNS, local
                                    address, WPAD candidate, local offset, PD-32)
  src-tauri/src/dev_feed.rs         changed: http_config(url, proxy)
  tests/required-tests.json         changed (§4.13)
.github/workflows/manager-ci.yml    path filter + step "roster absent" (§4.12)
.github/workflows/feed-tools.yml    PLAN line only if the TAP count changes (§4.12)
tools/feed-contract/tests/manager-ci-shape.test.js  PATHS, STEPS, ASSERTION_STEPS, COMMANDS, EXPECTED_REQUIRED,
                                    live-network ban (§4.12)
specs/Krate-manager-roadmap.md      Phase 5 status line; lines 534-535; one Phase 6 sentence; the min_os sentence;
                                    the Phase 12 RustSec sentence (§4.14)
specs/krate-manager/probes/results.md  one row, add-only: the R-15 auto-detect measurement (§4.14; FR-090 as
                                    amended by SI-41)
specs/INDEX.md                      regenerated (FR-091)
```

Every new core file follows Phase 4's convention: a flat `<stem>.rs` with `#[cfg(test)] #[path = "<stem>_tests.rs"] mod
tests;` (no inline `mod tests {`, `injection.rs` `RULE_INLINE_TESTS`).

---

## 3. Error model

Every `Display` begins with its code (Phase 4 convention, `context.rs:24-35`). `code(&self) -> &'static str` on each
enum.

| Code | Type / variant | Raised by | Meaning |
|---|---|---|---|
| `E_INPUT` | `FeedError::Input { reason }` | `feed::parse` | not UTF-8, or not one JSON value (BOM, HTML, truncated) |
| `E_FEED_INVALID` | `FeedError::Invalid { findings: Vec<Finding> }` | `feed::parse` | ≥ 1 finding after the sibling filter; `Display` = count + first finding |
| `E_FEED_FUTURE_SCHEMA` | `FeedError::FutureSchema { found: u64 }` | `feed::parse` | `schema_version` > 2: "a newer Krate Manager is needed" |
| `E_FEED_SCHEMA_VERSION` | `FeedError::SchemaVersion` | `feed::parse` | `schema_version` present and not 1 / 2 / > 2 (PD-15) |
| `E_FEED_CACHE_IO` | `FeedError::CacheIo { step: CacheStep, source: Arc<io::Error> }` | `feed::check` | a write step failed (FR-052) |
| `E_FEED_URL` | `HttpError::Url { url: String /* redacted */, reason: &'static str }` | `http::check_url`, `check_proxy`, `to_ureq_proxy` (PD-23), `next_hop`, `RequireHttpsOnly` | refused before connecting |
| `E_FEED_REDIRECT` | `HttpError::Redirect { reason }` | `next_hop`, the hop counter | no / bad `Location`, more than 5 hops |
| `E_FEED_HTTP_STATUS` | `HttpError::Status { status: u16 }` | `http::get` | final status not 200 |
| `E_FEED_TOO_LARGE` | `HttpError::TooLarge { max: u64 }` | `http::get` | decoded body > `max_bytes` |
| `E_FEED_OFFLINE` | `HttpError::Offline { detail: String }` | error mapping | no response (DNS, refused, reset, garbage, proxy refusal) |
| `E_FEED_TIMEOUT` | `HttpError::Timeout` | error mapping, PD-3 | connect / global deadline |
| `E_FEED_TLS` | `HttpError::Tls { detail }` | error mapping | `Tls`, `Rustls`, `TlsRequired`, or `Io` wrapping `rustls::Error` |
| `E_FEED_BODY` | `HttpError::Body { detail }` | error mapping | `Io` / `Decompress` / `BodyStalled` while reading a 200 body |
| `E_SCHEMA` | `Finding.code` | `contract::validate` | a JSON Schema keyword failed |
| `E_SIGNATURE_TEXT`, `E_TC_FIELDS`, `E_TC_NAME`, `E_TC_VERSION`, `E_TC_PLATFORM`, `E_TC_ASSET`, `E_INSTALL_VALUE`, `E_INSTALL_DUPLICATE` | `Finding.code`; `ContractError::Rule { code: RuleCode, message }` from the two parsers | `contract` | the code rules (FR-004 to FR-006, FR-008) |
| `E_MODEL_RANGE` | log code only (`feed::MODEL_RANGE`), never a `Finding` | `feed` model builder | PD-8 / R-7: an unrepresentable number; that platform asset is dropped |
| `E_MODEL_ASSET_NAME` | log code only | `feed` model builder | PD-21 / R-7: the URL's last segment fails the asset-name rule; that platform asset is dropped |
| `E_MODEL_V1_ENTRY` | log code only | `feed` model builder | PD-22, FR-029(c): a v1 key failing `pluginName`, or a v1 `download_url` failing the C1 `url` rule; either drops that entry |
| `E_PAC_FETCH` | log code only (`pac::PacCode::Fetch`), never a `FeedError` | `http` PAC session; on Windows `OsPacHost::os_find_proxy` (PD-35) and an unanswered OS call (`http::bounded` → `None`) | FR-068, FR-092: macOS / Linux: a PAC URL refused by `check_pac_url`, a PAC fetch failure of any FR-043 class, a body over `MAX_PAC_BYTES`, or a WPAD discovery that yields no script; Windows: any WinHTTP error but `ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT` (no script found, download failure, login required, service error, cancelled), no answer within `min(PAC_PHASE_BUDGET, remaining)`, or no OS evaluator (an `InertPacHost`); the hop takes the fallback |
| `E_PAC_SCRIPT` | log code only (`PacCode::Script`) | `pac::PacSession` (macOS / Linux); `os_find_proxy` (Windows) | the script does not compile, throws, defines no `FindProxyForURL`, is interrupted (`PAC_EVAL_BUDGET`), exhausts `PAC_MEMORY_LIMIT` or overflows `PAC_STACK_LIMIT`; on Windows `ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT` |
| `E_PAC_RESULT` | log code only (`PacCode::Result`) | `pac::parse_answer`, `pac::os_answer` (PD-36) | the answer is not a string, longer than 64 KiB, or has no usable entry, an empty Windows entry list included (a failure); an unknown keyword, an entry without a host, or a Windows entry of another scheme or with an unusable host (skipped, one `warn!`) |
| `E_SCHEMA_UNSUPPORTED` | `ContractError::Unsupported { pointer, keyword }` | `contract::compile_schema_text` | a schema uses a keyword / `type` / `$ref` / pattern the interpreter does not support (PD-24; `embedded_schemas_compile`, `unsupported_schema_constructs_are_refused`) |

`FeedError::Http(#[from] HttpError)` is transparent (its `code()` is the inner one). `FeedCheck.error` holds a
`FeedError` (transport errors arrive as `FeedError::Http`).

---

## 4. Module design

### 4.1 Manifests

`apps/krate-manager/Cargo.toml` `[workspace.dependencies]` gains (exact pins, the workspace style):

```toml
base64 = "0.23.1"
flate2 = "1.1.10"
regex = "1.13.1"
rustls = { version = "0.23.46", default-features = false }
ureq = { version = "3.5.0", features = ["platform-verifier", "socks-proxy"] }   # R-5, PD-2
windows-sys = { version = "0.61.2", features = ["Win32_Foundation", "Win32_Networking_WinHttp"] }   # R-2; R-13/R-15: no Win32_System_Time
system-configuration = "0.8.0"                                                   # R-3
core-foundation = "0.10.1"                                                       # R-6
rquickjs = "0.14.0"                                                              # R-8, default features only
```

(Shown wrapped and annotated for reading; the committed lines are one entry per line with reasons on full-line `#`
comments above them, tasks TA-15.)

`crates/core/Cargo.toml`:

```toml
[features]
test-support = []
live-network = ["test-support"]      # PD-14, never enabled by any workflow, script or gate

[dependencies]
base64 = { workspace = true }
log = { workspace = true }
regex = { workspace = true }
rustls = { workspace = true }        # only to name rustls::Error in http.rs (FR-044)
serde = { workspace = true }
serde_json = { workspace = true }
tempfile = { workspace = true }
thiserror = { workspace = true }
url = { workspace = true }
ureq = { workspace = true }

[dev-dependencies]
flate2 = { workspace = true }

[target.'cfg(not(windows))'.dependencies]
rquickjs = { workspace = true }      # R-8, R-15: pac.rs only (FR-081), never on Windows; never `features`, never rust-alloc (PD-25)

[target.'cfg(target_os = "macos")'.dependencies]
core-foundation = { workspace = true }   # R-6: sysproxy_cf only
```

`[dependencies]` is exactly the ten FR-080 names (`spec.md:814-820`); `rquickjs` is in no other table (FR-073(d)).

`src-tauri/Cargo.toml` adds two target sections (the `[dependencies]` set stays the five `config_guard.rs:32-33` pins):

```toml
[target.'cfg(windows)'.dependencies]
windows-sys = { workspace = true }

[target.'cfg(target_os = "macos")'.dependencies]
system-configuration = { workspace = true }
```

`Cargo.lock` is regenerated once by a plain `cargo build` after the manifest edit; every CI and gate command then
keeps `--locked` (the lock is target-independent, so it lists `rquickjs*` although Windows never builds them).
Compliance records `cargo tree -p krate-manager-core -e normal` on this host (FR-080 exclusions absent, `ureq v3.5.0`,
one `rustls`, `rustls-platform-verifier v0.7.1`, `ring`, `webpki-roots`), the same with `--target
x86_64-pc-windows-msvc` showing **no `rquickjs*` crate** and with `--target x86_64-unknown-linux-gnu` and `--target
aarch64-apple-darwin` showing `rquickjs v0.14.0` with `rquickjs-core` / `rquickjs-sys` 0.14.0 (`cargo tree` resolves a
target without building it, so these run on this box), and `cargo tree -p krate-manager -e normal --target all` (the two
OS crates; exactly one `core-foundation`, shared by core's R-6 section and `system-configuration`). **Binary size:**
compliance records `target/release/krate-manager.exe` built at the phase's base commit and at this close; with R-15 the
Windows binary carries no QuickJS, so the growth is `ureq` / `rustls` / `ring` and the phase's own code. The macOS and
Linux binaries carry the engine; their growth is not measured locally (the CI artifacts are not readable with this box's
token, and WSL has no WebKitGTK to build the app) and is stated as the scratch measurement's estimate, about +1.0 MiB per
Linux binary and per macOS slice (§1.2), not as a measurement.

### 4.2 `core::contract` (`crates/core/src/contract.rs`)

#### 4.2.1 API

```rust
//! The Phase 1 C1 contract, interpreted (D2, FR-001 to FR-008; plan §4.2).
use serde_json::Value;

pub const V2_SCHEMA_TEXT: &str =
    include_str!("../../../../../tools/feed-contract/versions.v2.schema.json");
pub const V1_SCHEMA_TEXT: &str =
    include_str!("../../../../../tools/feed-contract/versions.v1.schema.json");

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Schema { V1, V2 }

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Finding {
    pub code: &'static str,             // "E_SCHEMA", "E_TC_FIELDS", …
    pub instance_path: String,          // RFC 6901, "" = root
    pub keyword: Option<&'static str>,  // Some for E_SCHEMA ("pattern", "false schema", …)
    pub message: String,
    pub duplicate: Option<DuplicateSides>, // Some only for E_INSTALL_DUPLICATE (PD-9)
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DuplicateSides { pub field: &'static str, pub first: String, pub second: String } // full paths

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RuleCode { SignatureText, TcFields, TcName, TcVersion, TcPlatform, TcAsset, InstallValue }
impl RuleCode { pub fn as_str(self) -> &'static str { /* "E_SIGNATURE_TEXT", … */ } }

#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum ContractError {
    #[error("E_SCHEMA_UNSUPPORTED: {keyword} at {pointer}")]
    Unsupported { pointer: String, keyword: String },
    #[error("{}: {message}", .code.as_str())]
    Rule { code: RuleCode, message: String },
}

pub struct MinisigText {
    pub untrusted_comment: String,
    pub key_id: [u8; 8],
    pub signature: [u8; 64],
    pub trusted_comment: String,
    pub global_signature: [u8; 64],
}
pub struct TrustedComment { pub name: String, pub version: String, pub platform: String, pub asset: String }

pub const PLATFORM_KEYS: [&str; 5] =
    ["windows-x86_64", "darwin-aarch64", "darwin-x86_64", "darwin-universal", "linux-x86_64"];

pub fn validate(doc: &Value, schema: Schema) -> Vec<Finding>;
pub fn parse_minisig_text(text: &str) -> Result<MinisigText, ContractError>;
pub fn parse_trusted_comment(s: &str) -> Result<TrustedComment, ContractError>;
pub fn check_install_value(field: &str, value: &Value) -> Result<(), ContractError>; // one field, FR-004 order
pub(crate) fn compile_schema_text(text: &str) -> Result<Compiled, ContractError>;   // PD-24
pub(crate) fn ecma_pattern_to_rust(pattern: &str) -> Result<String, ContractError>;
pub(crate) fn pointer(segments: &[&str]) -> String; // `~` → `~0`, `/` → `~1` (contract.js:402-403)
```

The five platform key strings are data of the contract, not platform literals: `contract.rs` is outside `platform/`,
and its literals hit none of `FORBIDDEN_OUTSIDE_PLATFORM` (`injection.rs:12-20`).

#### 4.2.2 Interpreter

```rust
pub(crate) struct Compiled { root: NodeId, arena: Vec<Node> }   // pub(crate): PD-24's return type (private_interfaces)
enum Node { Bool(bool), Schema(Box<Keywords>) }
struct Keywords {
    types: Option<Vec<JsonType>>,            // object | string | integer only
    properties: Vec<(String, NodeId)>,
    pattern_properties: Vec<(regex::Regex, NodeId)>,
    additional: Option<NodeId>,
    property_names: Option<NodeId>,
    required: Vec<String>,
    dependent_required: Vec<(String, Vec<String>)>,
    constant: Option<Value>, enumeration: Option<Vec<Value>>,
    pattern: Option<regex::Regex>,
    minimum: Option<f64>, maximum: Option<f64>,
    not: Option<NodeId>, all_of: Vec<NodeId>,
    if_then_else: Option<(NodeId, Option<NodeId>, Option<NodeId>)>,
    reference: Option<NodeId>,               // "#/$defs/<name>" only
}
static V2: OnceLock<Result<Compiled, ContractError>> = OnceLock::new();   // and V1
```

Compile (`compile_schema_text`, PD-24): parse the text with `serde_json`, reserve one arena slot per `$defs` name
(pass 1), compile each definition and the root (pass 2). Accepted-and-ignored: `$schema`, `$id`, `title`, `$comment`,
`$defs`. Anything else (or a `type` other than the three, a `$ref` that is not `#/$defs/<name>` or names a missing
definition, a pattern `ecma_pattern_to_rust` or `Regex::new` refuses) is `Unsupported { pointer, keyword }`, `pointer`
being the schema object's location (`""`, `/properties/a`, `/$defs/url`), so no keyword is ever silently ignored. `validate` on a failed compile panics with the error: the embedded schemas are build inputs and
`embedded_schemas_compile` proves both compile on every leg.

Evaluation `eval(node, instance, path, out: &mut Vec<Finding>) -> bool` (valid?), all keywords evaluated
(`allErrors`), reporting per PD-10:

| Keyword | Applies to | Finding(s) on failure |
|---|---|---|
| `false` schema | any | `"false schema"` at `path` |
| `type` | any | `"type"` |
| `const` / `enum` | any | `"const"` / `"enum"`; JSON equality, numbers by value (`f64` compare after `as_f64`; integers compare exactly when both are `u64`/`i64`) |
| `pattern` | strings | `"pattern"` (unanchored `is_match`) |
| `minimum` / `maximum` | numbers | `"minimum"` / `"maximum"` |
| `required` | objects | one `"required"` per missing name, at the object's path |
| `dependentRequired` | objects | one `"dependentRequired"` per missing dependent |
| `properties`, `patternProperties`, `additionalProperties` (schema) | objects | children only, at `path/<key>` |
| `propertyNames` | objects | the key's findings at the **object's** path, plus `"propertyNames"` |
| `not` | any | `"not"` (inner findings discarded) |
| `allOf`, `$ref` | any | children only |
| `if`/`then`/`else` | any | the branch's findings plus `"if"` |

`integer` = a JSON number with a zero fractional part (`as_u64`/`as_i64` or `as_f64().fract() == 0.0`).

#### 4.2.3 Code rules (FR-004 to FR-006), a port of `contract.js:421-478`

For each plugin entry and its `prerelease` sibling (when an object): for each `platforms` entry whose `signature` is a
string: a CR → `E_SIGNATURE_TEXT`; else `parse_minisig_text` failing → its code; else, only under `PLATFORM_KEYS`,
`parse_trusted_comment(trusted_comment)` failing → its code; path `<base>/platforms/<key>/signature`. Then
`check_install_values(part.install)` over the five fields in `contract.js`'s `INSTALL_PATTERNS` order, **stopping at
the first failing field** (one finding, `contract.js:226-242`), path `<base>/install/<field>`. Then
`find_install_duplicates` over `doc.plugins` in iteration (bytewise) order with the same ASCII folding and "an entry
and its own sibling are one plugin" rule (`:249-271`); each duplicate yields `E_INSTALL_DUPLICATE` at the second
plugin's field and fills `DuplicateSides` (PD-9, SI-8).

`parse_minisig_text` (FR-005): split on `\n`, strip one trailing `\r` per line only when the line ended `\r\n`
(`/\r?\n/`), drop one empty last element, require 4 non-empty lines with no remaining `\r`, the two prefixes, lines 2
and 4 canonical standard base64 (`base64::engine::general_purpose::STANDARD.decode`, then `encode` equals the input),
74 bytes starting `0x45 0x44`, 64 bytes. `parse_trusted_comment` (FR-006): the `contract.js:299-318` order with the
same regexes (`CORE`, `SEMVER_RE` bounded core + optional prerelease, `PLUGIN_NAME_RE` and not `RESERVED_STEM_RE`,
`PLATFORM_KEYS`, `ASSET_NAME_RE` and not `.`/`..`), compiled once in `OnceLock<Regex>`.

### 4.3 `core::feed` (`crates/core/src/feed.rs`)

#### 4.3.1 The model

```rust
pub const FEED_CACHE_FILE: &str = "feed-cache.json";
pub const CACHE_VERSION: u64 = 1;
pub const MAX_FEED_BYTES: u64 = 4 * 1024 * 1024;
/// A release can take up to this long to reach the client through the Pages edge (measured 2026-10-30, D7).
pub const EDGE_MAX_AGE_SECS: u64 = 600;
pub(crate) const LOG_TARGET: &str = "krate_manager_core::feed";

#[derive(Clone, Debug, PartialEq)]
pub struct Feed { pub schema_version: u8, pub plugins: BTreeMap<String, PluginEntry>, pub manager: Option<ManagerInfo> }
#[derive(Clone, Debug, PartialEq)]
pub struct PluginEntry {
    pub name: String, pub version_text: String, pub version: Option<CoreVersion>,
    pub download_url: Option<PageUrl>, pub release: Option<StableRelease>,    // PD-22, SI-19: a page link, never fetched for install
}
#[derive(Clone, Debug, PartialEq)]
pub struct StableRelease {
    pub tag: String, pub pub_date: String, pub release_notes_url: String, pub notes_markdown: Option<String>,
    pub platforms: BTreeMap<PlatformKey, PlatformAsset>, pub install: Option<InstallFacts>,
}
#[derive(Clone, Debug, PartialEq)]
pub struct PlatformAsset {
    pub url: AssetUrl, pub asset_name: AssetName, pub size: u64, pub sha256: [u8; 32], pub signature: String,
    pub format: AssetFormat, pub min_os: Option<OsVersion>, pub payload: Option<Payload>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub struct Payload { pub files: u64, pub sha256: [u8; 32] }
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum AssetFormat { InnoExe, Pkg, TarGz }
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum PlatformKey { WindowsX86_64, DarwinAarch64, DarwinX86_64, DarwinUniversal, LinuxX86_64 }
impl PlatformKey { pub fn as_str(self) -> &'static str; pub fn parse(s: &str) -> Option<Self>; }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct InstallFacts {
    pub vst3_bundle: PlainName, pub au_component: Option<PlainName>, pub preset_subdir: Option<PlainName>,
    pub windows_uninstall_key: Option<PlainName>, pub macos_pkg_id: Option<PlainName>,
}
#[derive(Clone, Debug, PartialEq, Eq)] pub struct ManagerInfo { pub latest_json_url: String }
#[derive(Clone, Debug, PartialEq, Eq)] pub struct PlainName(String);   // no public constructor (FR-026)
#[derive(Clone, Debug, PartialEq, Eq)] pub struct AssetUrl(String);    // no public constructor; only PlatformAsset.url
#[derive(Clone, Debug, PartialEq, Eq)] pub struct PageUrl(String);     // no public constructor; PluginEntry.download_url
#[derive(Clone, Debug, PartialEq, Eq)] pub struct AssetName(String);   // no public constructor (PD-21)
impl PlainName { pub fn as_str(&self) -> &str; pub(crate) fn checked(field: &str, v: &Value) -> Option<Self>; }
impl AssetUrl  { pub fn as_str(&self) -> &str; pub(crate) fn checked(v: &str) -> Option<Self>; }
impl PageUrl   { pub fn as_str(&self) -> &str; pub(crate) fn checked(v: &str) -> Option<Self>; }   // same C1 rule
impl AssetName { pub fn as_str(&self) -> &str; pub(crate) fn from_url(url: &AssetUrl) -> Option<Self>; }
pub(crate) const MODEL_RANGE: &str = "E_MODEL_RANGE";            // log codes (§3), never findings
pub(crate) const MODEL_ASSET_NAME: &str = "E_MODEL_ASSET_NAME";
pub(crate) const MODEL_V1_ENTRY: &str = "E_MODEL_V1_ENTRY";
```

`PlainName::checked` re-runs `contract::check_install_value` (reader rule 8, `CONTRACT.md:141-142`); `AssetUrl::checked`
and `PageUrl::checked` both re-run the C1 `url` rule (`https://`, no ECMA whitespace, no `/releases/latest/`) through
one private function. They are two types on purpose (review round 3): `download_url` is the release **page**
(`release.yml:485`, `tools/gen-feed.js:277`) and a v1 entry carries it with no hash and no signature, while an
`AssetUrl` exists only inside a `PlatformAsset` beside its `sha256` and `signature`. No `From`, no shared trait and no
conversion function joins them, so a URL nothing can verify is not type-compatible with what Phase 7 downloads and
Phase 10's helper runs. For a v2 install name or asset URL a
`None` from a schema-valid document is a builder defect: `accept_rows_parse_through_the_client` catches it. `AssetName::
from_url` takes `url::Url::parse(url.as_str()).ok()?.path_segments()?.last()` (never a split of the raw string; not
percent-decoded, FR-025) and applies PD-21's rule (`^[A-Za-z0-9][A-Za-z0-9._-]*$`, not `.`/`..`, not
`RESERVED_STEM_RE`, all `OnceLock<Regex>`). Each of the four newtypes carries a `compile_fail` doctest
(`PlainName(String::new())` does not compile outside the crate), and `feed_model_seams_hold` (§4.11.1) proves no `pub`
constructor, `From`, `FromStr` or `Deserialize` exists for them.

#### 4.3.2 `parse`

```rust
pub fn parse(bytes: &[u8]) -> Result<Feed, FeedError>;
pub(crate) fn classify_schema_version(doc: &Value) -> Result<Schema, FeedError>;   // PD-15
pub(crate) fn drop_sibling_findings(findings: Vec<Finding>) -> (Vec<Finding>, Vec<Finding>); // (kept, dropped)
pub(crate) fn json_u64(v: &Value) -> Option<u64>;                                  // PD-8
```

Steps: `std::str::from_utf8` → `serde_json::from_slice::<Value>` (either failing: `E_INPUT`) → classify → `validate` →
`drop_sibling_findings` (one `warn!` per dropped finding, target `LOG_TARGET`, `"{code} {instance_path} {keyword}"`) →
non-empty kept → `E_FEED_INVALID` → build. The sibling rule: drop when the third RFC 6901 segment pair is
`/plugins/<seg>/prerelease` exactly or followed by `/` (split the path on `/`, require `parts[1] == "plugins"` and
`parts[3] == "prerelease"`; `<seg>` is any segment, so a plugin named `prerelease` at `parts[2]` does not trigger it);
an `E_INSTALL_DUPLICATE` is dropped when either `DuplicateSides` path matches that rule (PD-9). The builder: V1 →
each entry whose key passes `pluginName` (else dropped, one `warn!` `E_MODEL_V1_ENTRY /plugins/<key>`, PD-22)
and whose `download_url`, when present, passes `PageUrl::checked` (else the whole entry is dropped, one `warn!`
`E_MODEL_V1_ENTRY /plugins/<key>/download_url`, FR-029(c)) becomes `{ version_text, version: CoreVersion::parse,
download_url: Some(checked) or None when absent, release: None }`; V2 → as FR-021, keeping only the five keys (`PlatformKey::parse`),
`asset_name` = `AssetName::from_url` (PD-21), hex decoded by hand (`[0-9a-f]{64}` is guaranteed), `min_os` →
`OsVersion::parse`, `size` and `payload.files` through `json_u64`. **Per-asset drop (R-7):** an asset whose
`AssetName::from_url` is `None` (`E_MODEL_ASSET_NAME`, path `…/platforms/<k>/url`), whose `OsVersion::parse` is `None`
from an overflowing component, or whose `size` / `payload.files` `json_u64` refuses (`E_MODEL_RANGE`, the value's
path) is left out of `platforms` with one `warn!` `"{code} {path}"` (target `LOG_TARGET`); the document, the entry and
its other assets stay. These `warn!`s are in addition to FR-056's one summary line.

#### 4.3.3 `check` and the cache

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum FetchMode { Normal, CheckNow }
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum SnapshotSource { Fresh, Cached }
#[derive(Clone, Debug)] pub struct FeedSnapshot { pub feed: Feed, pub fetched_at_unix: u64, pub source: SnapshotSource }
#[derive(Clone, Debug)] pub struct FeedCheck {
    pub snapshot: Option<FeedSnapshot>, pub error: Option<FeedError>, pub cache_error: Option<FeedError>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CacheStep { CreateDir, CreateTemp, Write, SyncTemp, Persist, #[cfg(unix)] SyncDir }

pub fn check(ctx: &AppContext, mode: FetchMode, now_unix: u64) -> FeedCheck;
pub(crate) fn check_with(io: &mut dyn CacheIo, ctx: &AppContext, mode: FetchMode, now_unix: u64) -> FeedCheck;

pub(crate) trait CacheIo {                                   // PD-6
    fn read(&mut self, path: &Path) -> io::Result<Option<Vec<u8>>>;   // NotFound → Ok(None)
    fn create_dir(&mut self, dir: &Path) -> io::Result<()>;
    fn create_temp(&mut self, dir: &Path) -> io::Result<NamedTempFile>;   // prefix ".feed-cache.json."
    fn write(&mut self, tmp: NamedTempFile, bytes: &[u8]) -> io::Result<NamedTempFile>;
    fn sync_temp(&mut self, tmp: NamedTempFile) -> io::Result<NamedTempFile>;
    fn persist(&mut self, tmp: NamedTempFile, target: &Path) -> io::Result<()>;  // drops PersistError.file
    #[cfg(unix)] fn sync_dir(&mut self, dir: &Path) -> io::Result<()>;
}
pub(crate) struct StdCacheIo;
```

`check_with`: headers = `[]` for `Normal`, `[("Cache-Control", "no-cache"), ("Pragma", "no-cache")]` for `CheckNow`;
`http::get(&ctx.http, &ctx.feed_url, headers, MAX_FEED_BYTES)` once (FR-059); `parse`; on success: encode the cache
(`#[derive(Serialize)] struct CacheFileV1<'a> { cache_version: u64, fetched_at_unix: u64, source_url: &'a str, body:
&'a str }`, `to_vec_pretty` + LF; field order is declaration order) and run the FR-052 steps; on failure: read the cache
(FR-053: parse as `Value`, exactly the four keys, `cache_version == 1`, `source_url == ctx.feed_url`, `body` a string
that `parse` accepts; else one `warn!` and "no cache"). Exactly one summary line per call (FR-056): `info!("feed ok:
{n} plugins, schema {v}")` or `warn!("{code}: …; using cached copy from {fetched_at_unix}" | "…; no cached copy")`.
The cache body is the fetched bytes as UTF-8 text verbatim (they passed `from_utf8`).

### 4.4 `core::version` (`crates/core/src/version.rs`)

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct CoreVersion { pub major: u32, pub minor: u32, pub patch: u32 }       // derive Ord = field order (FR-030)
impl CoreVersion { pub fn parse(s: &str) -> Option<Self>; }
impl fmt::Display for CoreVersion;
pub fn reduce_class_version(s: &str) -> Option<CoreVersion>;                    // FR-031

#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum Installed { No, VersionUnknown, Version(CoreVersion) }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UpdateState { NotInstalled, UpToDate, UpdateAvailable, InstalledNewer, Unknown }
pub fn compare(installed: Installed, available: Option<CoreVersion>) -> UpdateState;  // FR-032

#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum Arch { X86_64, Aarch64, Other }
impl Arch { pub fn native() -> Arch; }   // cfg!(target_arch = "x86_64") / "aarch64"

#[derive(Clone, Copy, Debug)] pub struct OsVersion { parts: [u32; 4], len: u8 }   // 2..=4 components
impl OsVersion { pub fn parse(s: &str) -> Option<Self>; pub fn components(&self) -> &[u32]; }
impl PartialEq/Eq/PartialOrd/Ord for OsVersion   // by hand: missing trailing components are 0 (11.0 == 11.0.0)
impl fmt::Display for OsVersion                  // as parsed

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Availability<'a> {
    Available { key: PlatformKey, asset: &'a PlatformAsset },
    NotThroughManager, NotForThisMachine, NeedsNewerOs { min_os: OsVersion },
}
pub fn platform_candidates(os: Os, arch: Arch) -> &'static [PlatformKey];        // FR-034 table
pub fn resolve<'a>(entry: &'a PluginEntry, os: Os, arch: Arch, running: Option<&OsVersion>) -> Availability<'a>;
pub fn resolve_native<'a>(entry: &'a PluginEntry, running: Option<&OsVersion>) -> Availability<'a>;
```

Parsers are hand-written over bytes (no regex): `CoreVersion::parse` = three dot-separated parts, each `0` or
`[1-9][0-9]{0,8}`; `reduce_class_version` = `CoreVersion::parse` of the whole string, or of the first three parts
when exactly four parts exist and the fourth is 1-9 ASCII digits (leading zeros allowed in the fourth, so `1.0.4.0` and
`0.8.0.3` reduce; `check-bundle.js:50-54` only requires the `"<ver>."` prefix); `OsVersion::parse` = 2-4 non-empty
digit runs, each `u32::from_str` (overflow → `None`). `resolve`: `release` `None` or empty `platforms` →
`NotThroughManager`; first candidate present → apply FR-037 (`min_os` `Some` and `running` `Some` and `min_os >
*running` → `NeedsNewerOs`); none present → `NotForThisMachine`. `resolve_native` is the only place naming
`NativePlatform` / `Arch::native()` in this module (FR-036).

### 4.5 `core::http` (`crates/core/src/http.rs`)

```rust
pub const CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
pub const TOTAL_TIMEOUT: Duration = Duration::from_secs(30);
pub const MAX_REDIRECTS: u32 = 5;

#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum UrlPolicy { HttpsOnly, LoopbackOnly }
#[derive(Clone, Debug, PartialEq)]
pub struct HttpConfig { pub policy: UrlPolicy, pub connect_timeout: Duration, pub total_timeout: Duration,
                        pub proxy: Option<ProxyConfig>, pub pac: Option<PacSource>,
                        pub pac_host: Option<PacHostHandle> }                       // SI-26, PD-32
impl HttpConfig {
    pub fn production() -> Self;                                   // HttpsOnly, 10 s, 30 s, None, None, None
    pub fn loopback(connect_timeout: Duration, total_timeout: Duration) -> Self;   // LoopbackOnly, None ×3
    pub fn with_proxy(self, proxy: Option<ProxyConfig>) -> Self;
    pub fn with_pac(self, pac: Option<PacSource>) -> Self;         // FR-041, FR-064
    pub fn with_pac_host(self, host: Option<PacHostHandle>) -> Self;   // PD-32: the only way a host enters
}
/// What the OS settings say about PAC (FR-064): plain data, compared by value.
#[derive(Clone, PartialEq, Eq)] pub struct PacSource { pub auto_detect: bool, pub url: Option<String> }
impl fmt::Debug for PacSource;                                     // PD-19: url through pac_url_for_log
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ProxyResolution { pub proxy: Option<ProxyConfig>, pub pac: Option<PacSource> }   // FR-048
pub const MAX_PAC_BYTES: u64 = 1024 * 1024;                        // FR-065 (re-exported from pac.rs)
#[derive(Clone, PartialEq, Eq)] pub struct ProxyConfig { pub url: String, pub no_proxy: Vec<String> }  // SI-12
impl fmt::Debug for ProxyConfig; impl fmt::Display for ProxyConfig;        // PD-19 redaction
#[derive(Clone, Default, PartialEq, Eq)]
pub struct ProxyEnv { pub https_proxy: Option<String>, pub all_proxy: Option<String>, pub no_proxy: Option<String> }
/// The six raw environment values in a fixed shape; `src-tauri/src/proxy_env.rs` fills it (FR-063), core never
/// names the variables (config guard, §4.11.2).
#[derive(Clone, Default, PartialEq, Eq)]
pub struct ProxyVars { pub https_upper: Option<String>, pub https_lower: Option<String>,
                       pub all_upper: Option<String>, pub all_lower: Option<String>,
                       pub no_upper: Option<String>, pub no_lower: Option<String> }
impl ProxyEnv { pub fn from_vars(v: &ProxyVars) -> ProxyEnv; }   // per pair: non-empty upper, else non-empty lower, else None
impl fmt::Debug for ProxyEnv; impl fmt::Debug for ProxyVars;      // PD-19: values through redact_userinfo
pub(crate) fn redact_userinfo(s: &str) -> String;                 // also schemeless `u:p@h:1` → `***@h:1`
pub(crate) fn pac_url_for_log(s: &str) -> String;                 // redact_userinfo, query and fragment removed

pub fn check_url(policy: UrlPolicy, url: &url::Url) -> Result<(), HttpError>;
pub fn check_proxy(policy: UrlPolicy, proxy: &ProxyConfig) -> Result<(), HttpError>;   // http, https, socks4, socks5
pub fn check_pac_url(policy: UrlPolicy, url: &url::Url) -> Result<(), HttpError>;      // FR-065
pub enum Hop { Done, Follow(url::Url) }
pub fn next_hop(policy: UrlPolicy, current: &url::Url, status: u16, location: Option<&str>) -> Result<Hop, HttpError>;
/// The PAC fetch's redirect step (FR-065, macOS / Linux): `next_hop`'s status and `Location` rules with `check_pac_url`
/// in place of `check_url`; both are one private `next_hop_with(policy, current, status, location, check)`.
#[cfg(not(windows))]
pub(crate) fn next_pac_hop(policy: UrlPolicy, current: &url::Url, status: u16, location: Option<&str>)
    -> Result<Hop, HttpError>;
pub fn bypasses(no_proxy: &[String], host: &str) -> bool;                    // PD-18
pub fn resolve_proxy(env: &ProxyEnv, system: &SystemProxy, target: &str) -> ProxyResolution;   // PD-5, PD-13, SI-27
pub fn config_for_dev_url(url: &str, resolution: ProxyResolution, pac_host: Option<PacHostHandle>)
    -> HttpConfig;                                                                         // PD-12, SI-27
pub fn get(config: &HttpConfig, url: &str, headers: &[(&str, &str)], max_bytes: u64) -> Result<Vec<u8>, HttpError>;
/// The real resolver and local address the macOS / Linux PAC host delegates to (PD-29); sockets stay in this file.
#[cfg(not(windows))] pub fn std_resolve(name: &str, wait: Duration) -> Option<IpAddr>;
#[cfg(not(windows))] pub fn std_my_ip() -> IpAddr;
/// Every OS: the PAC DNS lookups (macOS / Linux) and the Windows OS evaluation call (PD-35) run through it.
pub fn bounded<T: Send + 'static>(f: impl FnOnce() -> T + Send + 'static, wait: Duration) -> Option<T>;  // PD-29
pub(crate) fn to_ureq_proxy(proxy: &ProxyConfig) -> Result<ureq::Proxy, HttpError>;   // PD-23: refusal = E_FEED_URL
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum AgentPurpose { Feed, #[cfg(not(windows))] PacFetch }   // PD-28, SI-20: no PAC fetch on Windows
pub(crate) fn agent_config(config: &HttpConfig, proxy: Option<ureq::Proxy>, purpose: AgentPurpose)
    -> ureq::config::Config;                                                        // SI-20
pub(crate) fn map_error(e: ureq::Error, phase: Phase) -> HttpError;          // Phase::{Call, Body}
pub(crate) fn is_no_response(e: &ureq::Error, deadline_left: bool) -> bool;  // PD-33's advance rule
```

`agent_config` (the only `config_builder()` in the workspace, and the only argument `Agent::new_with_config` ever
gets; `ureq_is_confined_to_http_rs` pins both, §4.11.1, so the trust store SC-026 inspects is the one `get` uses):

```rust
ureq::Agent::config_builder()
    .https_only(purpose == AgentPurpose::Feed && config.policy == UrlPolicy::HttpsOnly)   // PacFetch: false (PD-28)
    .max_redirects(0)
    .http_status_as_error(false)
    .proxy(proxy)          // already converted (PD-23); None means none: replaces try_from_env (SI-11)
    .tls_config(ureq::tls::TlsConfig::builder().root_certs(ureq::tls::RootCerts::PlatformVerifier).build())
    .timeout_connect(Some(config.connect_timeout))
    .timeout_global(Some(config.total_timeout))
    .user_agent(concat!("krate-manager/", env!("CARGO_PKG_VERSION")))
    .accept("application/json")
    .build()
```

`get`: parse `url` (`url::Url::parse`, failure `E_FEED_URL`) → `check_url` → `check_proxy` and `to_ureq_proxy` (when
`Some`; either failing is `E_FEED_URL` and no socket opens, PD-23) → when `config.pac` is `Some`, open the PAC session
(§4.5.1) → loop over hops ≤ `MAX_REDIRECTS`: per hop compute the remaining deadline (PD-3), then the hop's **choice
list**: from the PAC session when it is ready (§4.5.1), otherwise one entry, the converted static proxy or `None`
instead of it when `bypasses(no_proxy, hop host)` (PD-4, decided again for every hop); walk the list (PD-33): per entry
`ureq::Agent::new_with_config(agent_config(&hop, entry_proxy, purpose))` (`purpose` is `Feed` for `get`'s
own hops and, on macOS and Linux, `PacFetch` for `fetch_direct`, which runs the same loop, §4.5.1), `agent.get(hop_url.as_str())`
with each header, `.call()`; an error for which `is_no_response(&e, remaining > 0)` holds and another entry remains
advances to it, any other outcome ends the walk (`map_error(_, Call)` for an error); then
`next_hop(policy, &hop_url, status, location)`; on `Done` and
status 200 read `resp.body_mut().as_reader().take(max_bytes + 1).read_to_end(&mut out)` (`map_error` through
`ureq::Error::from(io::Error)` then `Body`); `out.len() as u64 > max_bytes` → `E_FEED_TOO_LARGE`. `map_error`:
`HostNotFound | ConnectionFailed | Protocol | ConnectProxyFailed(_)` → `Offline`; `Timeout(_)` → `Timeout`; `Tls(_) |
Rustls(_) | TlsRequired` → `Tls`; `Io(e)` with `e.get_ref().and_then(|i| i.downcast_ref::<rustls::Error>())` → `Tls`;
`RequireHttpsOnly(_)` → `Url`; in `Body` phase `Io | Decompress | BodyStalled` → `Body`; in `Call` phase `Io` →
`Offline`; `_` → `Offline { detail: e.to_string() }`. The platform verifier reports certificate failures as
`rustls::Error::InvalidCertificate`/`General` (§1.2), which the handshake wraps in `InvalidData` (spec External facts,
rustls `conn.rs:697-701`), so the downcast covers them: no extra mapping (FR-044's "plan confirms" sentence).

`is_no_response(e, deadline_left)` (PD-33): `HostNotFound | ConnectionFailed | ConnectProxyFailed(_)` → true;
`Timeout(Timeout::Resolve | Timeout::Connect)` → `deadline_left`; `Io(e)` whose kind is `ConnectionRefused`,
`ConnectionReset`, `ConnectionAborted`, `HostUnreachable` or `NetworkUnreachable` and that wraps no `rustls::Error` →
true; everything else (a status, `Tls`/`Rustls`, `Timeout::Global`, `RequireHttpsOnly`, a body error) → false. The
same classes map to `E_FEED_OFFLINE` in `map_error`, so "advance" and "offline" never disagree.

`resolve_proxy` (PD-5; FR-048, FR-069): (1) env proxy = first non-empty of `env.https_proxy`,
`env.all_proxy`; its bypass list = split `env.no_proxy` on `,`; result `{ proxy: Some, pac: None }`. (2) Else, a target
host on the environment's `NO_PROXY` → the empty resolution (FR-048(c): no PAC is consulted). (3) Else the OS settings,
both halves computed independently: `pac` = `Some(PacSource { auto_detect, url: pac_url })` when `pac_url.is_some() ||
auto_detect`, else `None`; `proxy` = the OS static proxy with `no_proxy = system.proxy.no_proxy ++ env list`, or `None`
when there is none or the target is on that combined list (the OS bypass list applies to the static proxy only; the
PAC decides for itself, FR-048(c)). The result carries both when both exist (FR-069 (2): the PAC first, the static
proxy as its failure fallback in `get`, §4.5.1); nothing → empty. Validate a static proxy URL with `url::Url::parse`
(prefix `http://` when no `://`), scheme ∈ `http`, `https`, `socks4`, `socks5` (else that source is dropped with one
`warn!`, redacted, and the next stage applies); a `pac_url` that `url::Url::parse` refuses is dropped with one `warn!`
(`auto_detect` still applies); target parse failure → empty + `warn!`. No `warn!` when a static proxy and a PAC
source are both configured (FR-069's normal case, not an anomaly).

`check_url`: `HttpsOnly` → scheme `https`, a host, no username/password; `LoopbackOnly` → scheme `http`|`https`,
`url.host()` is `Host::Ipv4(127.0.0.1)` or `Host::Ipv6(::1)`, no userinfo. `check_proxy` applies the host rule under
`LoopbackOnly` and the scheme rule (`http`, `https`, `socks4`, `socks5`; anything else, `socks4a`, `socks5h` and `ftp`
included, is `E_FEED_URL`, PD-34) under both; under `HttpsOnly` any host is accepted (FR-042). `check_pac_url`
(FR-065): scheme `http` or `https` under both policies, no userinfo, and under `LoopbackOnly` the host rule of
`check_url`; `file:`, `ftp:`, `data:` and every other scheme → `E_FEED_URL` (logged as `E_PAC_FETCH`, §4.5.1).

#### 4.5.1 The PAC path in `get` (FR-064 to FR-069, FR-092, PD-27 to PD-36)

```rust
enum PacState {                                      // private; one per `get` call
    /// macOS / Linux: the in-process engine (R-8).
    #[cfg(not(windows))] Engine { session: pac::PacSession, source_for_log: String },
    /// The OS evaluates (R-15): production on Windows; on every OS with a host whose `os_evaluates()` is true (SC-041).
    Os { host: pac::PacHostHandle, source: PacSource, source_for_log: String },
    /// The fallback for every remaining hop of this call.
    Failed,
}
// `source_for_log` = `pac_url_for_log` of the PAC URL (or of the winning WPAD candidate), or "WPAD"; for `Os`,
// `pac_url_for_log(url)` when `PacSource.url` is `Some`, else "WPAD" (never the raw URL: SC-041 plants a query).
```

**Open** (once per `get`, before the first hop, only when `config.pac` is `Some`; the host is `config.pac_host`, else
`pac::InertPacHost`):

- **The OS evaluates** (`host.os_evaluates()`, the real Windows host, PD-32): `PacState::Os` at once, with no I/O. The
  manager fetches no script and derives no WPAD candidate (FR-065, FR-066 do not apply on Windows, FR-092).
- **Otherwise, macOS and Linux** (`#[cfg(not(windows))]`): `phase_deadline = min(now + PAC_PHASE_BUDGET, chain
  deadline)`. Candidates: when `auto_detect`, `host.wpad_candidates(phase_deadline - now)`; then `pac.url` if `Some`.
  For each candidate in order: parse, `check_pac_url` (a refusal is that candidate's failure, no socket), fetch
  **direct** with `fetch_direct(config, &url, MAX_PAC_BYTES, phase_deadline)` (the hop loop of `get` with `proxy:
  None`, `AgentPurpose::PacFetch` so ureq's `https_only` is off and an `http` PAC URL is not refused with
  `RequireHttpsOnly` under `HttpsOnly` (PD-28), `check_pac_url` per hop through `next_pac_hop` (every redirect hop's
URL, not only the first, and never `check_url`, so an https→http PAC redirect is followed under `HttpsOnly` and a
non-loopback redirect is refused under `LoopbackOnly`; SC-034 rows (d), (e)), the FR-043 status and redirect rules,
  `take(MAX_PAC_BYTES + 1)` → over the cap is a failure; the response's content type is not checked: whether the body
  compiles is the test), then `PacSession::open(text, host.clone(), PacLimits::production(), phase_deadline)`; the first
  candidate that fetches **and** compiles wins (a compile failure moves on to the next candidate, FR-066 "a body that
  compiles as a PAC script"). When none wins: exactly one `warn!` (target `krate_manager_core::http`)
  `"{E_PAC_FETCH|E_PAC_SCRIPT}: {pac URL via pac_url_for_log, or "WPAD"}: {cause}; using {the static proxy|a direct
  connection}"` (the code and URL are the last candidate's; WPAD with no candidate is `E_PAC_FETCH` "WPAD: no WPAD
  candidate"; a script-derived `cause` is escaped and truncated as §4.15 states) and `PacState::Failed`. Discovery, fetch
  and compile are inside the chain deadline: a PAC phase that used 10 s leaves 20 s for the feed.
- **Otherwise, Windows** (`#[cfg(windows)]`; reached only with an `InertPacHost`, i.e. `pac_host: None`, which production
  never builds): exactly one `warn!` `"E_PAC_FETCH: {source_for_log}: no PAC evaluator; using …"` and
  `PacState::Failed`. The in-process engine does not exist on Windows (R-15).

**Per hop** with `Engine { session, .. }` (macOS / Linux): `session.find_proxy(&hop_url, min(now + PAC_EVAL_BUDGET,
chain deadline))` → `Ok(choices)` or `Err(PacError)`.

**Per hop** with `Os { host, source, .. }` (FR-092): `wait = min(PAC_PHASE_BUDGET, chain deadline - now)`; `let (h, s, u)
= (host.clone(), source.clone(), pac::pac_url_argument(&hop_url));` (FR-064's reduction on Windows too, review round 3:
WinHTTP runs the same untrusted, possibly WPAD-supplied script, which could exfiltrate a path or query through
`dnsResolve`; `http::get` is the one request path, FR-045, so later phases' redirect hops with signed query tokens
would otherwise reach it on Windows only), then `http::bounded(move || h.get().os_find_proxy(&s, &u, wait),
wait)`, so `get` waits at most `wait` whatever the host does and an unanswered call's thread is abandoned, never killed
(the real host also cancels its own call by closing its resolver handle, PD-35). `None` → `PacError { Fetch, "no answer
from the OS PAC evaluator within {wait:?}" }`; `Some(Err(e))` → `e`; `Some(Ok(entries))` → `pac::os_answer(&entries)` →
one `warn!` per rendering warning → `pac::parse_answer(&text)` → `Ok(choices)` or `Err(PacError)` (PD-36). The host is
asked exactly once per request hop (SC-041).

**Either evaluator:** each proxy of `Ok(choices)` is checked by `check_proxy` before any socket (a refusal is the chain's
`E_FEED_URL`, FR-067), then the list is walked (PD-33). `Err(PacError)` → exactly one `warn!` (target
`krate_manager_core::http`) in the open phase's format, `"{E_PAC_FETCH|E_PAC_SCRIPT|E_PAC_RESULT}: {source_for_log}:
{cause}; using {the static proxy|a direct connection}"` (FR-068: the code, the PAC URL with its query removed, the
cause; `cause` is the escaped, truncated text of §4.15), the state becomes `Failed` (a script or OS evaluation that failed
once is not asked again in this call) and this hop takes the fallback. **Fallback** (`Failed`, FR-068):
`[Proxy(config.proxy)]` when `Some` (with its bypass rule), else `[Direct]`. `no_proxy` never filters a PAC answer
(FR-067). With `pac: None` the path is exactly the pre-PAC one.

The macOS / Linux PAC fetch, the WPAD candidates and every walk entry are recorded by the fixture servers like any
request, so the SC-034 counts ("A records exactly one request for the PAC path") observe the one-fetch rule directly;
on the OS path a PAC fixture server records zero requests (SC-041).

`ProxyEnv::from_vars`: for each of the three pairs, the upper value when non-empty, else the lower value when
non-empty, else `None` (FR-048 "each in upper and lower case", PD-5 (a)). `proxy_env.rs` stores a non-UTF-8 value as
`None`.

### 4.6 `core::sysproxy` (`crates/core/src/sysproxy.rs`)

```rust
#[derive(Clone, Default, PartialEq)]
pub struct SystemProxy { pub proxy: Option<ProxyConfig>, pub pac_url: Option<String>, pub auto_detect: bool }
impl fmt::Debug for SystemProxy;                                         // PD-19: pac_url through redact_userinfo

pub struct WinIeProxyConfig { pub auto_detect: bool, pub auto_config_url: Option<String>,
                              pub proxy: Option<String>, pub proxy_bypass: Option<String> }        // R-2, SI-9
pub fn from_windows(raw: &WinIeProxyConfig) -> SystemProxy;
/// The WinHTTP structure's four members as raw values (PWSTR = *mut u16), so no windows-sys type enters core.
pub struct WinIeRaw { pub auto_detect: bool, pub auto_config_url: *mut u16, pub proxy: *mut u16,
                      pub proxy_bypass: *mut u16 }
/// Reads each non-null pointer up to its NUL into `String::from_utf16_lossy`, then calls `free` on it exactly once;
/// null pointers are `None` and never freed. Production passes `GlobalFree`.
/// # Safety: every non-null pointer is a NUL-terminated UTF-16 buffer, valid until `free` is called on it.
pub unsafe fn take_win_ie(raw: WinIeRaw, free: &mut dyn FnMut(*mut u16)) -> WinIeProxyConfig;
/// Reads one NUL-terminated UTF-16 string without freeing it (`take_win_ie`'s read step; also the PD-35 entry host,
/// whose memory `WinHttpFreeProxyResult` frees). Null → None.
/// # Safety: a non-null pointer is a NUL-terminated UTF-16 buffer valid for the call.
pub unsafe fn read_wide(p: *const u16) -> Option<String>;

/// Linux: runs each command with stdout piped, polling `try_wait` every 20 ms, for at most `bound` in total.
/// Some(outputs, in order) when all exited 0 in time; None otherwise. A child still running at the bound goes
/// to a detached reaper thread (PD-17); its pid is listed in `abandoned`.
pub struct BoundedRun { pub outputs: Option<Vec<String>>, pub abandoned: Vec<u32> }
pub fn run_bounded(commands: Vec<std::process::Command>, bound: Duration) -> BoundedRun;
pub fn kioslaverc_path(config_home: Option<&Path>, home: Option<&Path>) -> Option<PathBuf>;
pub fn read_kde(path: &Path) -> SystemProxy;    // missing file = default with no warn; read error = default + warn!

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ScValue { Int(i64), Bool(bool), Text(String), List(Vec<String>) }   // FR-075: string, integer, boolean
pub fn from_macos(dict: &[(String, ScValue)]) -> SystemProxy;          // keys as SystemConfiguration names them

pub struct GnomeRaw { pub mode: String, pub autoconfig_url: String, pub ignore_hosts: String,
                      pub https_host: String, pub https_port: String }  // each the `gsettings get` output text
pub fn from_gnome(raw: &GnomeRaw) -> SystemProxy;
pub fn from_kde(kioslaverc: &str) -> SystemProxy;
pub fn linux_uses_kde(current_desktop: Option<&str>) -> bool;           // R-4
pub(crate) fn gvariant_string(s: &str) -> Option<String>;
pub(crate) fn gvariant_string_list(s: &str) -> Option<Vec<String>>;     // accepts "@as []"

/// The local-zone offsets the real macOS / Linux PAC host reads (PD-31, R-13), as pure steps compiled on every OS
/// (SC-043). Windows reads none (R-15), so the round-1 `win_utc_offset` is withdrawn.
/// `date +%z` output: `+hhmm` / `-hhmm` and one trailing LF; hh ≤ 14, mm ≤ 59; anything else → None.
pub fn parse_utc_offset(s: &str) -> Option<i32>;
/// macOS `CFTimeZone::seconds_from_gmt` value (a `CFTimeInterval`, f64 seconds): non-finite → None (through
/// `f64::is_finite`), else rounded to i32 and kept only within ±14 h (±50 400 s).
pub fn cf_offset_secs(seconds: f64) -> Option<i32>;
```

Rules: Windows — `proxy` `Some` → if it contains `=`, take the `https=` entry (none → no static proxy), else the whole
string; prefix `http://` when it has no scheme; bypass = `proxy_bypass` split on `;` and whitespace. macOS —
an `…Enable` key is **on** when its value is `Int(1)` or `Bool(true)` (SystemConfiguration stores these flags as
`CFNumber`, a `CFBoolean` is accepted the same way, FR-075); `HTTPSEnable` on and `HTTPSProxy` non-empty `Text` →
`http://<host>:<HTTPSPort (Int) or 443>`; `ExceptionsList` (`List`) → bypass;
`ProxyAutoConfigEnable` on with a non-empty `ProxyAutoConfigURLString` → `pac_url`; `ProxyAutoDiscoveryEnable` on → `auto_detect`; a key of the wrong `ScValue` variant counts as absent. Every parser keeps a static proxy and a PAC source side by side when both are configured; the order between them is FR-069's, applied by `resolve_proxy` and `get` (PD-5), not the parsers'.
GNOME — `mode 'manual'` with a non-empty https host and a port in 1-65535 → `http://host:port` (port `0`,
the schema default, means unset: no static proxy); `mode 'auto'` → `pac_url` from a non-empty `autoconfig-url`, else `auto_detect`;
`ignore-hosts` list → bypass. KDE — `[Proxy Settings]` group, `ProxyType=1` → `httpsProxy` (the KDE "host port" form
turned into `host:port`, `http://` prefixed when schemeless); `2` → `pac_url` = `Proxy Config Script`; `3` →
`auto_detect`; `4` (environment variables) and `0` → none; `NoProxyFor` split on `,`; `ReversedException=true` makes
the list an allow-list, which the bypass matcher cannot express: ignored with one `warn!`. Any malformed input →
`SystemProxy::default()` with one `warn!` (target `krate_manager_core::sysproxy`).

`kioslaverc_path`: an absolute `config_home` → `<config_home>/kioslaverc`; an unset or relative one (the XDG Base
Directory rule ignores relative values) → `<home>/.config/kioslaverc`; no usable `home` → `None`. "Absolute" is
`Path::is_absolute` of the host OS, so the test (`kioslaverc_path_cases`, every leg) builds its fixtures from
`tempfile::tempdir()` paths (absolute on every OS, a drive prefix on Windows) and a relative `Path::new("rel")`, and
compares against `PathBuf::join` results, never against `/home/…` string literals, which are not absolute on Windows.
`linux_uses_kde`: `true` iff the `:`-split `XDG_CURRENT_DESKTOP` value has an element exactly equal to `KDE`
(case-sensitive; `split(':').any(|e| e == "KDE")`, never a substring test), else `false`, unset and empty included
(FR-074). Both are pure and compiled
on every OS, as are `take_win_ie` and `read_wide` (plain memory) and `read_kde` (`std::fs::read_to_string`; `sysproxy.rs` is outside
`platform/`, so the FS-token rule does not apply, `injection.rs:375-381`). `run_bounded` compiles everywhere; its test is
`#[cfg(unix)]`.

**`crates/core/src/sysproxy_cf.rs`** (`#[cfg(target_os = "macos")] pub mod sysproxy_cf;`, R-6):
`pub fn from_cf_proxies(dict: &CFDictionary<CFString, CFType>) -> Vec<(String, ScValue)>`: every key copied as a
`String`; a value that downcasts (`CFType::downcast`, core-foundation 0.10.1 `src/base.rs:84`) to `CFBoolean`
(`impl_TCFType!(CFBoolean, …, CFBooleanGetTypeID)`, `src/boolean.rs:24`) → `ScValue::Bool` (`bool::from`,
`:47-50`), to `CFNumber` → `ScValue::Int` (`to_i64`, `src/number.rs:45`), to `CFString` → `Text`, to a `CFArray`
whose every element is a `CFString` → `List`; any other value (a `CFData`, an array holding a non-string) is skipped,
never a panic. It names no key, so it holds no literal.
`pub fn system_utc_offset() -> Option<i32>`: `sysproxy::cf_offset_secs(CFTimeZone::system().seconds_from_gmt(
CFDate::now()))` (`src/timezone.rs:45,52`, `src/date.rs:35`), the macOS local offset `OsPacHost::now` reads (PD-31).
It is the one OS read in core (read-only, macOS only), kept here so `core_foundation` is named only in this file and its
tests (FR-075, FR-081); no test calls it (§4.11.1), its pure step `cf_offset_secs` is tested on every leg
(`sysproxy::tests::offset_range_cases`, SC-043).

### 4.7 `core::fixture_server` (`crates/core/src/fixture_server.rs`, `cfg(any(test, feature = "test-support"))`)

```rust
#[derive(Clone, Debug)]
pub enum Reply {
    Ok { body: Vec<u8>, content_type: &'static str }, Status(u16), Redirect { status: u16, location: String },
    Truncated { declared_len: usize, sent: Vec<u8> }, Stall(Duration), Close, Garbage(Vec<u8>),
    Gzip { body: Vec<u8> }, ConnectOk, ConnectRefused(u16),
    /// 200 with `Content-Length: declared_len`, then `sent`, then wait `stall` (Condvar, ends on Drop), then close.
    HeadersThenStall { declared_len: usize, sent: Vec<u8>, stall: Duration },     // SI-22
    /// Wait `after` (Condvar, ends on Drop), then answer with `reply`.
    Delayed { after: Duration, reply: Box<Reply> },                                // SI-22
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RecordedRequest { pub method: String, pub path: String, pub headers: Vec<(String, String)> }
pub struct FixtureServer { addr: SocketAddr, shared: Arc<Shared>, accept: Option<JoinHandle<()>> }
impl FixtureServer {
    pub fn start(routes: Vec<(String, Reply)>) -> FixtureServer;      // binds 127.0.0.1:0 only
    pub fn start_v6(routes: Vec<(String, Reply)>) -> FixtureServer;   // binds [::1]:0 only (SI-22); a bind failure panics
    pub fn url(&self, path: &str) -> String;                          // http://127.0.0.1:<port><path> or http://[::1]:<port><path>
    pub fn port(&self) -> u16;
    pub fn set_route(&self, path: &str, reply: Reply);                 // PD-7
    pub fn requests(&self) -> Vec<RecordedRequest>;
}
impl Drop for FixtureServer   // set shutdown, notify_all the stall Condvar, wake accept with one loopback connect, join
```

Per connection (thread): peek the first byte; `0x16` → read one TLS record (5-byte header + declared length, bounded
16 KiB), write `HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n`, `shutdown(Write)`, read
to EOF or 2 s, close. Else read the request head (to CRLFCRLF, bounded 64 KiB), record it, look up the route: method
`CONNECT` → `path` = authority; an authority that is not `127.0.0.1:<port>` or `[::1]:<port>` (`localhost` and every
DNS name included) → `403` and close, whatever the route table holds, and nothing is dialled (the server opens no
outbound socket at all: its one `TcpStream::connect` is `Drop`'s wake-up of its own listener, pinned by
`ureq_is_confined_to_http_rs`); `ConnectOk` → `200 Connection established`
and **serve the same stream again from the top** (PD-7); `ConnectRefused(s)` → that status, close. Other methods →
`Ok` with `Content-Length`; `Status` with an empty body; `Redirect` with `Location`; `Truncated`; `Stall` waits on the
Condvar for the duration or shutdown; `Close`; `Garbage`; `Gzip` with `Content-Encoding: gzip`; `HeadersThenStall`
and `Delayed` wait on the same Condvar, so `Drop` ends them at once. Unknown path → `404`.
One `Connection: close` per response (no keep-alive), so every hop is one connection.

### 4.8 `core::test_support` additions (FR-088)

```rust
pub fn install_capture();          // OnceLock; log::set_logger(&CAPTURE) once; set_max_level(Trace)
pub fn take_mine() -> Vec<(String, log::Level, String)>;  // (target, level, message) of this thread, removed
```

Records every record whose target starts with `krate_manager_core`. `context_tests.rs` deletes its `Capture`,
`CAPTURE`, `take_mine` (`:446-484`) and calls `install_capture()` in `startup_errors_are_logged`, filtering
`STARTUP_LOG_TARGET`. `injection.rs`'s plan-level check (§4.11.1) asserts no test file calls `log::set_logger`.

### 4.9 `core::context` change (FR-060)

`AppContext` gains `pub http: HttpConfig`; `assemble_context(roots, feed_url: &str, http: HttpConfig)`,
`assemble_with_os(os, roots, feed_url, http)`; `reassemble_with_os` copies `ctx.http.clone()`. The 27
`context_tests.rs` call sites pass `HttpConfig::loopback(Duration::from_secs(1), Duration::from_secs(1))` through one
helper `fn http() -> HttpConfig`. `http_config_is_carried` (SC-018) builds the proxied variant by struct update
(`HttpConfig { proxy: Some(ProxyConfig { url: "http://127.0.0.1:9".into(), no_proxy: vec![] }), ..http() }`) and the
PAC variant the same way (`HttpConfig { pac: Some(PacSource { auto_detect: true, url: Some("http://127.0.0.1:9/p.pac"
.into()) }), ..http() }`, SC-018's third case), so FR-087's `.with_proxy(` / `.with_pac(` rules are not touched.

### 4.10 `src-tauri`

`proxy_env.rs` (the one environment read for proxies, SI-3):

```rust
//! The proxy environment (FR-048, FR-063): the only proxy-variable read in the app.
use krate_manager_core::http::{ProxyEnv, ProxyVars};
const NAMES: &[&str] = &["HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy", "NO_PROXY", "no_proxy",
    #[cfg(target_os = "linux")] "XDG_CURRENT_DESKTOP", #[cfg(target_os = "linux")] "XDG_CONFIG_HOME",
    #[cfg(target_os = "linux")] "HOME"];
#[derive(Debug)]   // safe: ProxyEnv's Debug redacts (PD-19)
pub struct ProxyInputs { pub env: ProxyEnv, pub desktop: Option<String>, pub config_home: Option<PathBuf>,
                         pub home: Option<PathBuf> }
pub fn read() -> ProxyInputs {
    // one #[expect(clippy::disallowed_methods, reason = "…")] var_os loop over NAMES into `got(name)`, then:
    let vars = ProxyVars { https_upper: got("HTTPS_PROXY"), https_lower: got("https_proxy"),
                           all_upper: got("ALL_PROXY"), all_lower: got("all_proxy"),
                           no_upper: got("NO_PROXY"), no_lower: got("no_proxy") };
    ProxyInputs { env: ProxyEnv::from_vars(&vars), /* Linux: desktop, config_home, home */ .. }
}
```

The merge and its empty-as-unset rule are core's (`from_vars`, tested); this file only copies values. The six
field-to-name pairs are needles of the config guard (§4.11.2), so a swapped pair fails a test.

`os_proxy.rs`:

```rust
pub trait SystemProxyFromOs { fn system_proxy(&self, inputs: &ProxyInputs) -> SystemProxy; }
impl SystemProxyFromOs for NativePlatform { … }   // cfg per OS, each delegating to a core sysproxy parser
```

Each reader is one OS call plus core seams (PD-1, SI-21). Windows: one `unsafe {
WinHttpGetIEProxyConfigForCurrentUser(&mut cfg) }`, then `unsafe { sysproxy::take_win_ie(WinIeRaw { auto_detect:
cfg.fAutoDetect != 0, auto_config_url: cfg.lpszAutoConfigUrl, proxy: cfg.lpszProxy, proxy_bypass: cfg.lpszProxyBypass
}, &mut |p| { GlobalFree(p.cast()); }) }` → `sysproxy::from_windows`. macOS: `SCDynamicStoreBuilder::new("krate-manager")
.build()` → `get_proxies()` → `sysproxy_cf::from_cf_proxies` → `sysproxy::from_macos`. Linux: `linux_uses_kde(desktop)`
→ `kioslaverc_path(config_home, home)` → `read_kde`; else `run_bounded` over five `Command::new("gsettings").args(["get",
schema, key])`, bound 2 s (PD-17) → `from_gnome`. Any failure → `SystemProxy::default()` + one `warn!`. What stays
untested is only the OS call and the struct-field wiring above, a few lines per OS. The trait takes `&ProxyInputs` (a
deviation from the spec's zero-argument signature, recorded in SI-1/SI-3: the Linux reader needs the three names).

**The real PAC host** (`os_proxy.rs`, PD-32; never constructed by a test, FR-087 / SI-26). One struct, one `impl` per
OS family, each overriding only the methods its OS uses (the rest keep `PacHost`'s inert defaults):

```rust
pub struct OsPacHost;                                   // stateless; every call reads the OS afresh
pub fn pac_host() -> PacHostHandle { PacHostHandle::new(Arc::new(OsPacHost)) }

#[cfg(not(windows))]                                    // macOS, Linux: the in-process engine's machine access
impl krate_manager_core::pac::PacHost for OsPacHost {
    fn resolve(&self, name: &str, wait: Duration) -> Option<IpAddr> { http::std_resolve(name, wait) }
    fn my_ip(&self) -> IpAddr { http::std_my_ip() }
    fn now(&self) -> PacClock { PacClock { unix_secs: /* SystemTime → i64 */, local_offset_secs: local_offset() } }
    fn wpad_candidates(&self, _wait: Duration) -> Vec<String> { pac::wpad_dns_candidates() }   // R-9, PD-30
}

#[cfg(windows)]                                         // Windows: the OS evaluates (R-15, R-16, PD-35)
impl krate_manager_core::pac::PacHost for OsPacHost {
    fn os_evaluates(&self) -> bool { true }
    fn os_find_proxy(&self, source: &PacSource, url: &str, wait: Duration) -> Result<Vec<OsProxyEntry>, PacError> {
        win_find_proxy(source, url, wait)
    }
}
```

`win_find_proxy` (`#[cfg(windows)]`, the one `unsafe` block of PD-35, about 80 lines): `WinHttpOpen(w("krate-manager"),
WINHTTP_ACCESS_TYPE_NO_PROXY, null(), null(), WINHTTP_FLAG_ASYNC)` (null → `E_PAC_FETCH` with
`io::Error::last_os_error()`); `WinHttpSetStatusCallback(session, Some(on_status),
WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE | WINHTTP_CALLBACK_FLAG_HANDLES, 0)`
(`const WINHTTP_CALLBACK_FLAG_HANDLES: u32 = WINHTTP_CALLBACK_STATUS_HANDLE_CREATED |
WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING;` in `os_proxy.rs`, winhttp.h's own definition, §1.2); `WinHttpCreateProxyResolver(
session, &mut resolver)` (≠ 0 → close the session, `E_PAC_FETCH`; nothing is allocated yet); a capacity-1
`sync_channel::<(u32, u32)>`, its sender boxed as `ctx = Box::into_raw(Box::new(CallbackCtx { tx }))`;
`WinHttpSetOption(resolver, WINHTTP_OPTION_CONTEXT_VALUE, (&ctx as *const *mut CallbackCtx).cast(), size_of::<usize>()
as u32)` (FALSE → `ctx` is marked leaked for this call and the call proceeds, PD-35); from here on the waiting thread
never frees `ctx`; `WINHTTP_AUTOPROXY_OPTIONS { dwFlags: (auto_detect → WINHTTP_AUTOPROXY_AUTO_DETECT) | (url →
WINHTTP_AUTOPROXY_CONFIG_URL), dwAutoDetectFlags: auto_detect → WINHTTP_AUTO_DETECT_TYPE_DHCP |
WINHTTP_AUTO_DETECT_TYPE_DNS_A else 0, lpszAutoConfigUrl: the wide URL or null, lpvReserved: null(), dwReserved: 0,
fAutoLogonIfChallenged: 0 }` (R-17); `WinHttpGetProxyForUrlEx(resolver, wide(url), &options, ctx as usize)`: any return
but `ERROR_IO_PENDING` → close the resolver (its `HANDLE_CLOSING` frees `ctx`), then the session, that code's error (no
completion callback follows, the Ex page: "If the call fails after returning ERROR_IO_PENDING then a callback of
WINHTTP_CALLBACK_STATUS_REQUEST_ERROR will be issued");
`rx.recv_timeout(wait)`:
`(WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE, _)` → `WinHttpGetProxyResult(resolver, &mut result)` (≠ 0 → error),
copy `result.pEntries[..cEntries]` into `OsProxyEntry`s (PD-35's mapping, host via `sysproxy::read_wide`),
`WinHttpFreeProxyResult(&mut result)`; `(WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, code)` → the error class of PD-35;
timeout → the "no answer" error. On **every** path after the Ex call (complete, error, timeout) the waiting thread
does the same two things and nothing else: `WinHttpCloseHandle(resolver)` (on the timeout path this is the cancel),
then `WinHttpCloseHandle(session)`; it never frees `ctx`.
`on_status(_, dw_context, status, info, _)`: `dw_context == 0` → return (the session has no context value, and the
resolver's `HANDLE_CREATED` arrives before (3b)); `status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` →
`drop(Box::from_raw(dw_context as *mut CallbackCtx))` and return (the resolver's last callback, WinHttpCloseHandle
remarks); otherwise `(*(dw_context as *const CallbackCtx)).tx.try_send((status, if status ==
WINHTTP_CALLBACK_STATUS_REQUEST_ERROR { (*info.cast::<WINHTTP_ASYNC_RESULT>()).dwError } else { 0 }))`, whose
`Full` / `Disconnected` results are ignored (a late callback after the waiter returned). It allocates, logs and panics
nothing (it runs on a WinHTTP thread); its one deallocation is `ctx`'s, on `HANDLE_CLOSING`. **No path frees `ctx`
before the resolver's `HANDLE_CLOSING`**; the only alternative outcome is the deliberate leak when (3b) fails. The UTF-16 conversion of the inputs is `str::encode_utf16` plus a NUL.

`local_offset()` (macOS / Linux only, PD-31, R-13): macOS `krate_manager_core::sysproxy_cf::system_utc_offset()` (so
`os_proxy.rs` names no `core_foundation` path, FR-075); Linux `sysproxy::run_bounded(vec![Command::new("date").arg("+%z")],
2 s)` → `sysproxy::parse_utc_offset`. Any failure → `None` (the session then uses UTC with one `warn!`, PD-31). Windows has
no `local_offset`, no `std_resolve` / `std_my_ip` call and no WPAD candidate (R-15).

`lib.rs` (`run()` after `feed_url` is known):

```rust
let inputs = proxy_env::read();
let resolution = resolve_proxy(&inputs.env, &NativePlatform.system_proxy(&inputs), &feed_url);
#[cfg(not(feature = "dev-feed"))]
let http = HttpConfig::production().with_proxy(resolution.proxy).with_pac(resolution.pac)
    .with_pac_host(Some(os_proxy::pac_host()));
#[cfg(feature = "dev-feed")]
let http = dev_feed::http_config(&feed_url, resolution, Some(os_proxy::pac_host()));
let state = assemble_context(roots, &feed_url, http);
```

`dev_feed.rs` gains `pub fn http_config(url: &str, resolution: ProxyResolution, pac_host: Option<PacHostHandle>) ->
HttpConfig { http::config_for_dev_url(url, resolution, pac_host) }`. Note FR-063 (as amended by SI-3/SI-15/SI-26):
`HttpConfig::production()` appears in `lib.rs`, `with_proxy` / `with_pac` / `with_pac_host` / `resolve_proxy` only in
`lib.rs`, `config_for_dev_url` only in `dev_feed.rs`, `OsPacHost` only in `os_proxy.rs`. The OS settings are resolved
once per launch (an OS proxy change needs a restart, §9); on macOS and Linux the PAC script, the WPAD answer and the
local offset are read afresh at every check (PD-28); on Windows WinHTTP is asked at every request hop and its service
decides what it caches (PD-35).

### 4.11 Integration tests

#### 4.11.1 `crates/core/tests/injection.rs`

- `FR007_DEPENDENCIES` → exactly the ten names of FR-080 (`spec.md:814-815`; `rquickjs` is not among them, R-15);
  new `dev_dependencies_are_flate2_only` (folded into `core_dependencies_are_the_fr007_set`:
  `section_keys(&manifest, "[dev-dependencies]") == {"flate2"}`), and the core target sections are exactly two:
  `[target.'cfg(not(windows))'.dependencies]` holding exactly `rquickjs = { workspace = true }` (R-8, R-15; no
  `features` / `default-features` key, and the root pin is exactly `rquickjs = "0.14.0"`, so `rust-alloc`, which
  disables the memory limit, cannot be switched on, PD-25) and `[target.'cfg(target_os = "macos")'.dependencies]`
  holding exactly `core-foundation = { workspace = true }` (R-6).
- `D6_SITES` gains `("src-tauri/src/proxy_env.rs", 1)`, `D6_TOTAL = 8` (SI-3).
- `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` (FR-063, FR-074, SC-039): the check becomes `fn
  src_tauri_env_findings(files: &[(String, String)]) -> Vec<String>`, run on the real `src-tauri/src` files (no
  finding) and on plants. Rules, over the `injection.rs` lexer (PD-20): (1) after blanking comments and literals, the
  token sequences `env :: var`, `env :: var_os`, `env :: vars`, `env :: vars_os` and the bare tokens `var_os` /
  `vars_os` occur only in `os_roots.rs` and `proxy_env.rs` (the round-0 rule searched raw text for `var_os` only, so
  a `std::env::var(…)` elsewhere passed it); (2) the string literals `"XDG_CURRENT_DESKTOP"`, `"XDG_CONFIG_HOME"` and
  `"HOME"` occur in `src-tauri/src` only in `proxy_env.rs` (case-sensitive; `os_roots.rs` reads the core `ENV_KEYS`
  table and names none of them); (3) in `proxy_env.rs` every string literal equals a `NAMES` entry or appears in an
  attribute, so the file admits exactly the six proxy names and, on Linux, those three (SI-3, SI-36); (4)
  `krateaudio.com` still once, in `lib.rs`. Plants reported: `std::env::var("XDG_CURRENT_DESKTOP")` in an in-memory
  `os_proxy.rs` (SC-039's self-test; rules 1 and 2), `let _ = std::env::vars();` in `lib.rs` (rule 1), and an extra
  `"LANG"` literal in `proxy_env.rs` (rule 3).
- New `ureq_is_confined_to_http_rs` (FR-081): over every workspace `.rs` (`crates/*/src`, `crates/*/tests`,
  `src-tauri/src`), after blanking: tokens `ureq` / `rustls` only in `crates/core/src/http.rs` and `http_tests.rs`;
  `http.rs` has no `pub use ureq`/`pub use rustls` and no `pub` item whose signature names `ureq::`/`rustls::`
  (`pub(crate) fn agent_config` is allowed); tokens `TcpStream` / `TcpListener` only in `http.rs`,
  `fixture_server.rs` and their `_tests.rs`; `ureq` / `rustls` as TOML keys only in `crates/core/Cargo.toml` and the
  root `[workspace.dependencies]`. **One agent, one trust store (SC-026):** in `http.rs` after blanking, the token
  sequence `config_builder` occurs once, inside `fn agent_config`; `new_with_config` occurs once and is followed by
  `( agent_config (`; none of `new_with_defaults`, `Agent :: new`, `ureq :: get`, `ureq :: post`, `ureq :: agent`,
  `ureq :: run` occurs. In `fixture_server.rs`, `TcpStream :: connect` occurs once, inside `impl Drop`. Four in-memory
  plants reported: `use ureq as u;`, `std::net::TcpStream::connect` in another file, a second
  `ureq::Agent::config_builder()` in `http.rs`, and `Agent::new_with_config(other)`. **The PAC extension (FR-081,
  SI-31):** the token `rquickjs` only in `crates/core/src/pac.rs` and `pac_tests.rs` (and as a TOML key only in
  `crates/core/Cargo.toml` and the root), and `pac.rs` has no `pub` item whose signature names `rquickjs::`; that every
  such item is `#[cfg(not(windows))]` is proven by compilation, not by this scan: `rquickjs` is absent from the Windows
  dependency graph (§4.1), so an un-gated use fails `cargo clippy` / `cargo test` on this box and on the Windows leg. The
  tokens `UdpSocket` and `to_socket_addrs` only in `http.rs` (`pac.rs` opens no socket and resolves no name: every
  machine call goes through `PacHost`). **The R-15 / R-16 extension:** the tokens `GetTimeZoneInformation`,
  `WinHttpDetectAutoProxyConfigUrl` and `WinHttpGetProxyForUrl` (the call R-16 replaces; the lexer's tokens keep it
  apart from `WinHttpGetProxyForUrlEx`) occur in **no** workspace `.rs` file, and `WinHttpGetProxyForUrlEx`,
  `WinHttpCreateProxyResolver`, `WinHttpGetProxyResult`, `WinHttpFreeProxyResult`, `WinHttpSetStatusCallback` and
`WinHttpSetOption` (PD-35's context binding) only in
  `src-tauri/src/os_proxy.rs`. Six more plants reported: `use rquickjs::Runtime;` in `http.rs`,
  `std::net::UdpSocket::bind` in `pac.rs`, `("x", 0).to_socket_addrs()` in `pac.rs`, `GetTimeZoneInformation(&mut t)`
  and `WinHttpGetProxyForUrl(s, u, o, i)` in an in-memory `os_proxy.rs`, and `WinHttpGetProxyForUrlEx(r, u, o, 0)` in
  `lib.rs`. **The OS-crate extension (FR-073,
  FR-075, FR-081; challenge round 2):** after blanking, the token `windows_sys` occurs only in `src-tauri/src/os_proxy.rs`;
  `system_configuration` only in `src-tauri/src/os_proxy.rs`; `core_foundation` only in `crates/core/src/sysproxy_cf.rs`
  and `sysproxy_cf_tests.rs` (so neither `os_proxy.rs` nor any other core file reaches it, not even through the
  `system_configuration::core_foundation` re-export, PD-31; this one rule and its plant run inside
  `core_dependencies_are_the_fr007_set`, the test FR-075 names, through the same shared scan helper); as TOML keys, `windows-sys` and `system-configuration`
  only in `src-tauri/Cargo.toml` and the root `[workspace.dependencies]`, `core-foundation` only in
  `crates/core/Cargo.toml` and the root. Three more plants reported: `use windows_sys::Win32::Foundation::GlobalFree;`
  in `lib.rs`, `use system_configuration::dynamic_store::SCDynamicStoreBuilder;` in `lib.rs`, and
  `use core_foundation::string::CFString;` in `crates/core/src/sysproxy.rs`.
- New `feed_model_seams_hold` (FR-026, FR-036, PD-21, PD-22): in `feed.rs` after blanking, each of `PlainName`,
  `AssetUrl`, `PageUrl`, `AssetName` is declared `pub struct <T> ( String ) ;`, has no `#[derive]` naming `Deserialize`
  or `Default`, no `impl From <…> for <T>` / `impl FromStr for <T>`, and every `fn` in its `impl` block that returns
  `Self` or `Option < Self >` is `pub ( crate )` or private; no `impl From < PageUrl >` / `impl From < AssetUrl >` and no
  `fn` taking one of the two URL types returns the other (no conversion between a page link and an asset URL); the
  token `AssetUrl` occurs in a field type only inside `struct PlatformAsset`; in `version.rs`, `Arch :: native (` and
  `NativePlatform` each occur exactly once, inside `fn resolve_native`. Three plants reported: a `pub fn new` on
  `AssetName`, a second `Arch::native()` in `fn resolve`, and `pub download_url: Option<AssetUrl>` in `PluginEntry`.
- New `production_config_only_in_allowed_tests` (FR-087 + SI-10): body extraction from `fn <name>(` to the next
  top-level `fn ` or `#[test]`; allowed bodies `https_only_refuses_before_connecting`, `config_for_dev_url_cases`,
  `url_policy_cases`, `next_hop_cases`, `agent_trusts_the_os_store`, `proxy_scheme_cases`, `production_values` (all in
  `http_tests.rs`), plus for R-1 `pac_policy_cases` (`http_tests.rs`; its `HttpsOnly` half is pure `check_pac_url`
  calls, its socket half is `LoopbackOnly`) and `wpad_candidate_cases` (`pac_tests.rs`, a pure `check_pac_url`
  call; SI-26 extends FR-087's list with both), and the whole of
  `tests/live_feed.rs`; literals allowed in `config_guard.rs` and `injection.rs`; `.with_proxy(` only in
  `proxy_path_connect`, `proxy_path_http`, `no_proxy_bypass`, `loopback_proxy_only`, `check_through_proxy_writes_cache`
  (each proxy a loopback fixture server or a refused-before-connecting value);
  `.system_proxy(` in no test source; `Command::new(` in `crates/core` test sources only with the literal `"sleep"`
  or `"printf"` (Phase 4's `crates/helper/tests/stub.rs:9` runs its own built binary and is outside the rule), and the
  literal `gsettings` in no test source (so `bounded_run_abandons_and_reaps` can never start the real reader's
  program); `log::set_logger` only in `test_support.rs`; plants reported. **The PAC extension (FR-087, SI-26):**
  `.with_pac(` and `.with_pac_host(` only inside the bodies of the PAC-path tests (`pac_path_proxy`,
  `pac_path_direct`, `pac_list_walk`, `pac_per_hop`, `pac_fetched_once_never_via_proxy`, `pac_policy_cases`,
  `wpad_discovery_walk`, `pac_fallback_to_static_proxy`, `pac_body_cap`, `check_survives_pac_failure`, and for R-15
  `os_pac_walk`, `os_pac_failure_cases`), each of whose PAC URLs and WPAD candidates is a `FixtureServer::url(…)` value
  or a refused-before-connecting literal (the two OS-path tests carry a loopback PAC URL that must record zero requests);
  the tokens `OsPacHost`, `std_resolve (`, `std_my_ip (`, `pac_host (` and `system_utc_offset (` in no test source (so
  no test resolves a real name, reads the local address or zone, runs the real discovery or calls the real Windows OS
  evaluator, `OsPacHost::os_find_proxy`); `PacHostHandle :: new (` in test sources only with a `FakePacHost` argument
  (the test double in `pac_tests.rs`, whose `resolve` answers from a table, `now` returns a fixed `PacClock`,
  `wpad_candidates` returns the injected list, and whose OS mode (`os_evaluates` true) answers `os_find_proxy` from a
  scripted queue: entries, a `PacError`, or a call that blocks until the test drops a `Sender` it holds, so an abandoned
  call's thread ends after the assertion and is never killed; it counts its `os_find_proxy` and `now()` calls and records each `os_find_proxy` URL); a planted `.with_pac(` in another body, a planted
  `http::std_resolve("example.invalid", …)`, a planted `os_proxy::pac_host()` and a planted
  `sysproxy_cf::system_utc_offset()` are each reported. **The FR-086 extension (review round 3; `spec.md:856-857`: no
  `--test-threads=1`, no global mutex around tests, no fixed port in any new test):** over every test source
  (`crates/*/src/**/*_tests.rs`, `crates/*/tests/*.rs`) plus `fixture_server.rs`, with the same lexer, after comments and
  literals are blanked (literal texts kept for the port rule): (1) a `static` item (from the token `static` to its `;`)
  whose tokens include `Mutex` or `RwLock` is reported, so no test source holds a process-wide lock that serialises
  tests (`test_support.rs`, the shared capture store of §4.8, is neither a test source nor a production source here: Phase 4
  already treats it as its own class, `injection.rs:336-337`, `:453-455` `is_scanned`; whatever lock its record store uses is
  held only while one record is pushed or taken, never around a test body); (2) the token sequence `TcpListener :: bind` occurs only in
  `fixture_server.rs`; (3) every `bind (` call whose first argument is a string literal must have that literal end in
  `:0`, and one whose first argument is a `( <literal> , <integer> )` tuple must have the integer `0`, in every scanned
  file, `fixture_server.rs` included (its `"127.0.0.1:0"` / `"[::1]:0"` pass). Plants reported: `static LOCK:
  std::sync::Mutex<()> = std::sync::Mutex::new(());` in an in-memory `feed_tests.rs`, `static GATE: RwLock<()>` in an
  in-memory `http_tests.rs`, `TcpListener::bind("127.0.0.1:0")` in an in-memory `http_tests.rs` (rule 2), and
  `std::net::UdpSocket::bind("127.0.0.1:8080")` in an in-memory `fixture_server_tests.rs` (rule 3). The
  `--test-threads` half stays the shape test's (`manager-ci-shape.test.js` scans `manager-ci.yml`), and the dev-dependency
  set stays `{"flate2"}` (`core_dependencies_are_the_fr007_set`), so no `serial_test`-style crate can enter either.
- New `reader_dependencies_are_the_ruled_set` (SC-038, `spec.md:1293-1304`): `ureq` `3.5.0` with features
  exactly `["platform-verifier", "socks-proxy"]` and no `default-features = false`; `windows-sys` `0.61.2` with
  features exactly `["Win32_Foundation", "Win32_Networking_WinHttp"]` under a Windows target table of exactly one crate
  (SI-32 withdrawn: no `Win32_System_Time`); `system-configuration` `0.8.0` under a macOS target table of exactly one
  crate; `rquickjs` `0.14.0` with no `features` key and no `default-features = false`, declared only in
  `crates/core/Cargo.toml` `[target.'cfg(not(windows))'.dependencies]` (a table of exactly that one crate), absent from
  `[dependencies]`, from every Windows target table and from `FR007_DEPENDENCIES`. Plants, each reported: a wrong
  `windows-sys` feature list (`Win32_System_Time` added), a second crate under the Windows table, `rquickjs = { …,
  features = ["rust-alloc"] }`, `rquickjs` in core `[dependencies]`, `rquickjs` under `[target.'cfg(windows)'.dependencies]`.
  The `cargo tree --target x86_64-pc-windows-msvc` record (no `rquickjs*`) is compliance's, §7.4.
- `core_sources_hold_no_production_literal` must keep passing: no new core source contains `krateaudio.com` or
  `api.github.com` even in a comment, and `sysproxy.rs` / `http.rs` contain none of the five forbidden path prefixes.

#### 4.11.2 `crates/core/tests/config_guard.rs`

`LIB_RS_STRINGS` gains `"HttpConfig::production()"`, `"resolve_proxy("`, `".with_proxy("`, `".with_pac("`,
`".with_pac_host("`, `"os_proxy::pac_host()"`, `"proxy_env::read()"`; `.with_pac(` / `.with_pac_host(` in no
`src-tauri` file but `lib.rs` (SI-26); `OsPacHost` only in `os_proxy.rs`;
new checks: `assemble_context(` call has three arguments (the text between its parentheses has two top-level commas);
`config_for_dev_url`, `LoopbackOnly`, `HttpConfig::loopback` only in `dev_feed.rs`; `HTTPS_PROXY`, `ALL_PROXY`,
`NO_PROXY` only in `proxy_env.rs` and in no `crates/core/src` file (case-sensitive); `proxy_env.rs` contains each of
the six pairs `https_upper: got("HTTPS_PROXY")`, `https_lower: got("https_proxy")`, `all_upper: got("ALL_PROXY")`,
`all_lower: got("all_proxy")`, `no_upper: got("NO_PROXY")`, `no_lower: got("no_proxy")` (whitespace-normalised), so the
one untested copy step cannot swap a pair; `crates/core/Cargo.toml`
`[features]` is exactly `test-support = []` and `live-network = ["test-support"]`, no `default`; `src-tauri/Cargo.toml`
target sections are exactly `[target.'cfg(windows)'.dependencies] windows-sys = { workspace = true }` and
`[target.'cfg(target_os = "macos")'.dependencies] system-configuration = { workspace = true }`. Four plants of SC-018
plus one for the Cargo target sections, one swapped `proxy_env.rs` pair and one `lib.rs` without `.with_pac(`.

#### 4.11.3 `crates/core/tests/roster.rs` (FR-082)

`plugin_names()` reads `../../plugins/*/version.json` `name`s from `env!("CARGO_MANIFEST_DIR")/../../../..`
(repo root), asserting ≥ 9. `scan(rel, text, names) -> Vec<String>` searches raw text case-sensitively for each name
and `api.github.com`. Scanned: `crates/core/src/**`, `crates/helper/src/**`, `src-tauri/src/**`,
`src-tauri/capabilities/**`, `src-tauri/tauri*.conf.json`, `src/**`, `index.html`, excluding `*_tests.rs`,
`test_support.rs`, `*.test.ts`. Tests: `roster_and_hosts_absent_from_sources`, `roster_scanner_reports_planted_names`.

#### 4.11.4 `crates/core/tests/live_feed.rs`

```rust
#![cfg(feature = "live-network")]
#[test]
#[ignore = "live network; opt-in only, never in a gate"]
fn live_feed_parses() { /* FEED_URL read from src-tauri/src/lib.rs; check(&ctx(production), Normal, now) → Fresh, ≥ 1 plugin */ }
```

### 4.12 Workflows and the shape test

`manager-ci.yml`: both path filters add `- 'tools/feed-contract/**'`; step `roster absent` after `dev-feed absent`:

```yaml
      - name: roster absent
        run: |
          set -euo pipefail
          case "$RUNNER_OS" in Windows) bin=target/release/krate-manager.exe ;; *) bin=target/release/krate-manager ;; esac
          node - "$bin" <<'ROSTER'
          const fs = require("fs"), path = require("path");
          const [bin] = process.argv.slice(2);
          const root = path.join("..", "..", "plugins");
          const names = fs.readdirSync(root).map((d) => path.join(root, d, "version.json"))
            .filter((f) => fs.existsSync(f)).map((f) => JSON.parse(fs.readFileSync(f, "utf8")).name).filter(Boolean);
          const files = [bin, ...fs.readdirSync("dist", { recursive: true }).map((f) => path.join("dist", f))
            .filter((f) => fs.statSync(f).isFile())];
          const bad = [];
          if (names.length < 9) bad.push(`only ${names.length} plugin names read`);
          const control = fs.readFileSync(bin).includes("com.krateaudio.krate-manager");
          if (!control) bad.push("positive control com.krateaudio.krate-manager not in the binary");
          for (const f of files) { const b = fs.readFileSync(f);
            for (const w of [...names, "api.github.com"]) if (b.includes(w)) bad.push(`${w} in ${f}`); }
          const os = process.env.RUNNER_OS;
          for (const b of bad) console.log(`::error title=roster absent (${os})::${b}`);
          if (bad.length) process.exit(1);
          console.log(`::notice title=roster absent (${os})::${names.length} names, 0 hits in ${files.length} files`);
          ROSTER
```

FR-083 as amended by SI-17: a Node byte search (`Buffer.includes`) over the release binary and every file under
`dist/`, one script on all three legs; the positive control proves it reads the binary. The shape test pins this
command.

`manager-ci-shape.test.js` (SI-4): `PATHS` + `"- 'tools/feed-contract/**'"`; `STEPS` + `'roster absent'` after
`'dev-feed absent'`; `ASSERTION_STEPS` + `'roster absent'`; `COMMANDS['roster absent'] = ['node - "$bin" <<\'ROSTER\'']`;
`EXPECTED_REQUIRED` = §4.13; the top-level test asserts `live-network` absent from the workflow and `package.json`.
`feed-tools.yml` `PLAN` changes only if new top-level `node:test` tests are added: this plan adds subtests inside the
existing top-level test, so the plan count stays `1..59` (re-measured at close).

### 4.13 `tests/required-tests.json` additions

`cargo.all` gains (printed names):
`contract::tests::embedded_schemas_compile`, `contract::tests::unsupported_schema_constructs_are_refused`,
`contract::tests::keyword_semantics_cases`, `contract::tests::c1_cases_reproduce`,
`contract::tests::gen_feed_outputs_accepted`, `contract::tests::ecma_pattern_translation`,
`contract::tests::trusted_comment_cases`, `contract::tests::minisig_text_cases`,
`feed::tests::accept_rows_parse_through_the_client`, `feed::tests::gen_feed_outputs_parse`,
`feed::tests::schema_version_dispatch`, `feed::tests::model_of_full_v2`, `feed::tests::model_range_cases`,
`feed::tests::asset_name_cases`, `feed::tests::v1_entries_are_sanitized`,
`feed::tests::unknown_keys_do_not_change_the_model`, `feed::tests::prerelease_sibling_is_ignored`,
`feed::tests::check_fresh_writes_cache`, `feed::tests::check_now_sends_no_cache`,
`feed::tests::rejected_feed_keeps_last_good_copy`, `feed::tests::broken_sibling_never_blocks_stable`,
`feed::tests::sibling_prefix_filter`, `feed::tests::offline_returns_cached_flagged_stale`, `feed::tests::cache_rules`,
`feed::tests::cache_write_fault_keeps_previous`, `feed::tests::roster_follows_the_feed`,
`feed::tests::check_logs_one_line`, `feed::tests::concurrent_checks_leave_one_whole_file`,
`feed::tests::fresh_lower_version_replaces_cache`, `feed::tests::one_attempt_per_check`,
`feed::tests::check_through_proxy_writes_cache`, `version::tests::resolve_table`, `version::tests::parse_core_cases`,
`version::tests::reduce_class_version_cases`, `version::tests::compare_cases`, `version::tests::os_version_cases`,
`version::tests::needs_newer_os_table`, `http::tests::url_policy_cases`, `http::tests::redirects_are_rechecked`,
`http::tests::next_hop_cases`, `http::tests::status_and_body_errors`,
`http::tests::https_only_refuses_before_connecting`, `http::tests::config_for_dev_url_cases`,
`http::tests::agent_trusts_the_os_store`, `http::tests::resolve_proxy_cases`, `http::tests::loopback_proxy_only`,
`http::tests::proxy_path_connect`, `http::tests::proxy_path_http`, `http::tests::no_proxy_bypass`,
`http::tests::proxy_env_from_vars_cases`, `http::tests::proxy_scheme_cases`, `http::tests::chain_shares_one_deadline`,
`http::tests::production_values`,
`sysproxy::tests::windows_proxy_parser_cases`, `sysproxy::tests::macos_proxy_parser_cases`,
`sysproxy::tests::linux_proxy_parser_cases`, `sysproxy::tests::win_ie_strings_are_read_and_freed_once`,
`sysproxy::tests::linux_desktop_choice_cases`, `sysproxy::tests::kioslaverc_path_cases`, `sysproxy::tests::kde_file_read_cases`,
`fixture_server::tests::binds_loopback_port_zero`, `fixture_server::tests::connect_to_remote_host_is_refused`,
`context::tests::http_config_is_carried`, `ureq_is_confined_to_http_rs`, `feed_model_seams_hold`,
`production_config_only_in_allowed_tests`, `roster_and_hosts_absent_from_sources`, `roster_scanner_reports_planted_names`,
and for R-1 / R-15 the PAC tests that compile on every OS: `pac::tests::pac_helper_cases`,
`pac::tests::find_proxy_result_cases`, `pac::tests::wpad_candidate_cases`, `pac::tests::winhttp_answer_cases`,
`pac::tests::pac_clock_cases`, `http::tests::pac_policy_cases` (its manager-fetch rows are `#[cfg(not(windows))]`
blocks inside the test), `http::tests::os_pac_walk`, `http::tests::os_pac_failure_cases`,
`http::tests::no_response_class_cases`, `http::tests::bounded_call_abandons_after_wait`,
`sysproxy::tests::parse_utc_offset_cases`, `sysproxy::tests::offset_range_cases`, `reader_dependencies_are_the_ruled_set`.
`cargo.windows` + `version::tests::native_candidates_windows`; `cargo.macos` + `version::tests::native_candidates_macos`,
`sysproxy_cf::tests::from_cf_proxies_cases`, `sysproxy::tests::bounded_run_abandons_and_reaps`; `cargo.linux` +
`version::tests::native_candidates_linux`, `sysproxy::tests::bounded_run_abandons_and_reaps`. **`cargo.macos` and
`cargo.linux` both** + the `#[cfg(not(windows))]` PAC tests (FR-085 as amended by SI-39; they do not exist in the Windows
test binary, so the Windows leg's `required tests ran` step must not look for them): `pac::tests::url_argument_cases`,
`pac::tests::script_world_cases`, `pac::tests::script_failure_cases`, `pac::tests::dns_bound_cases`,
`http::tests::pac_path_proxy`, `http::tests::pac_path_direct`, `http::tests::pac_list_walk`, `http::tests::pac_per_hop`,
`http::tests::pac_fetched_once_never_via_proxy`, `http::tests::wpad_discovery_walk`,
`http::tests::pac_fallback_to_static_proxy`, `http::tests::pac_body_cap`, `feed::tests::check_survives_pac_failure`. `live_feed_parses` and the
four newtypes' `compile_fail` doctests are never listed (a doctest's printed name has spaces, which the
`required tests ran` matcher `^test (\S+)…` cannot match, `manager-ci.yml:146`; a failing doctest still fails the
`cargo test` step).

### 4.14 Roadmap and index edits (FR-090, FR-091)

At build close: the Phase 5 status line (Phase 4's form, roadmap line 467); lines 534-535 restated as "A manual "check
now" always fetches from the network with `Cache-Control: no-cache` / `Pragma: no-cache`; the Pages edge ignores them
and may serve a copy up to 600 s old (measured 2026-10-30)."; Phase 6 Components gains "Phase 6 owns the "check now"
control, whose text states that a new release can take up to ten minutes to appear (`feed::EDGE_MAX_AGE_SECS`)." and
"Phase 6 reads the running OS version through `Platform` and passes it to Phase 5's `version::resolve`, which compares
it with the asset's `min_os` (`Availability::NeedsNewerOs`)."; Phase 12 gains "Before every manager release, check
the RustSec advisory database (https://rustsec.org/) for `rquickjs`, `rquickjs-core` and `rquickjs-sys`, the vendored
QuickJS-ng PAC engine that Phase 5 pins for the macOS and Linux builds (R-15); an advisory is answered with a pin bump
before the release." (FR-093, SC-044: one line naming `rustsec.org`, the three crates and the pin bump); then
`node tools/gen-specs-index.js`.

**The R-15 measurement** (FR-090 as amended by SI-41, §0.2; tasks TA-13 (b)): `specs/krate-manager/probes/results.md` gains one
table row, in the file's three-column form (`results.md:6-7`): question "Windows: is "Automatically detect settings"
(WPAD) on for a user profile?"; result "**On, on the user's own (not fresh) Windows 11 profile**: no PAC URL, no manual
proxy. A fresh-profile default was not measured."; evidence "Read-only, 2026-10-31: HKCU `…\Internet
Settings\Connections` value `DefaultConnectionSettings`, flags byte `0x09` (spec Clarifications R-15). Consequence: the
Windows PAC path (the OS evaluation, plan PD-35) runs by default on such a profile when no environment proxy is set."
The build writes the row exactly once and changes nothing else in that file (L-SCOPE: its `git diff` is one added
line and no removed line, SI-41).

### 4.15 `core::pac` (`crates/core/src/pac.rs`, R-1, R-8, R-15; FR-064 to FR-068, FR-092, FR-094, FR-095)

The file has two halves (R-15, FR-064 "only the engine glue is `cfg(not(windows))`", `spec.md:575-576`): the **shared
pure half**, compiled and tested on every OS (constants, the host seam, the answer parser, the Windows answer renderer,
the helpers, the clock arithmetic, the FR-064 URL argument `pac_url_argument`), and the **engine glue**, every item of
it `#[cfg(not(windows))]` (the `rquickjs` session, `evaluate`, the DNS memo). No inline module: the gate is per item, so `pac_tests.rs` stays
one flat file whose engine tests carry the same attribute.

```rust
//! PAC (D16, FR-064 to FR-068, FR-092; plan §4.15). The only file that names rquickjs (FR-081); every item that does
//! is #[cfg(not(windows))]: on Windows the OS evaluates (R-15) and only the shared pure half below is compiled.

// ---- shared, every OS -------------------------------------------------------------------------------------------
pub const MAX_PAC_BYTES: u64 = 1024 * 1024;                 // FR-065 (macOS / Linux fetch)
pub const PAC_EVAL_BUDGET: Duration = Duration::from_secs(2);   // FR-068, per compile and per call (engine)
pub const PAC_PHASE_BUDGET: Duration = Duration::from_secs(10); // PD-27: engine discovery + fetch + compile; Windows: one OS call
pub const PAC_MEMORY_LIMIT: usize = 32 * 1024 * 1024;       // PD-27 (engine)
pub const PAC_STACK_LIMIT: usize = 256 * 1024;              // PD-27 (engine)
pub const MAX_PAC_ANSWER_BYTES: usize = 64 * 1024;          // PD-27, SI-29 (both evaluators, through parse_answer)
pub const MAX_PAC_ENTRIES: usize = 16;                      // PD-27, SI-29 (both evaluators)
pub const PAC_DNS_WAIT: Duration = Duration::from_secs(1);  // PD-29, FR-094 (engine)
pub const PAC_MAX_LOOKUPS: usize = 16;                      // PD-29, FR-094 (engine)
pub const WPAD_DNS_URL: &str = "http://wpad/wpad.dat";      // PD-30, R-9
pub(crate) const LOG_TARGET: &str = "krate_manager_core::http";   // FR-068 names this target

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PacClock { pub unix_secs: i64, pub local_offset_secs: Option<i32> }   // PD-31, FR-095
/// One entry of the Windows OS answer (a WINHTTP_PROXY_RESULT_ENTRY, PD-35), as plain data: no windows-sys type in core.
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum OsProxyKind { Direct, Http, Https, Socks, Other(i32) }
#[derive(Clone, Debug, PartialEq, Eq)] pub struct OsProxyEntry { pub kind: OsProxyKind, pub host: String, pub port: u16 }

pub trait PacHost: Send + Sync {                           // SI-25, PD-32; every method has the inert default
    fn resolve(&self, _name: &str, _wait: Duration) -> Option<IpAddr> { None }                 // engine
    fn my_ip(&self) -> IpAddr { IpAddr::V4(Ipv4Addr::LOCALHOST) }                               // engine
    fn now(&self) -> PacClock { PacClock { unix_secs: 0, local_offset_secs: None } }           // engine
    fn wpad_candidates(&self, _wait: Duration) -> Vec<String> { Vec::new() }                   // engine
    fn os_evaluates(&self) -> bool { false }                                                   // FR-092
    fn os_find_proxy(&self, _source: &PacSource, _url: &str, _wait: Duration)
        -> Result<Vec<OsProxyEntry>, PacError> {                                               // FR-092
        Err(PacError { code: PacCode::Fetch, detail: "no OS PAC evaluator".to_owned() })
    }
}
#[derive(Clone)] pub struct PacHostHandle(Arc<dyn PacHost>);
impl PacHostHandle { pub fn new(host: Arc<dyn PacHost>) -> Self; pub fn get(&self) -> &dyn PacHost; }
impl fmt::Debug for PacHostHandle;   // "PacHostHandle"
impl PartialEq for PacHostHandle;    // Arc::ptr_eq
pub struct InertPacHost;             // PD-32
impl PacHost for InertPacHost {}     // every default

#[derive(Clone, Debug, PartialEq, Eq)] pub enum ProxyChoice { Direct, Proxy(ProxyConfig) }  // no_proxy empty
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum PacCode { Fetch, Script, Result }
impl PacCode { pub fn as_str(self) -> &'static str; }       // "E_PAC_FETCH", "E_PAC_SCRIPT", "E_PAC_RESULT"
#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
#[error("{}: {detail}", .code.as_str())]
pub struct PacError { pub code: PacCode, pub detail: String }

pub fn wpad_dns_candidates() -> Vec<String>;                // [WPAD_DNS_URL]: the real macOS / Linux list (PD-30)
pub(crate) fn parse_answer(answer: &str) -> Result<(Vec<ProxyChoice>, Vec<String>), PacError>;  // (choices, warnings)
pub(crate) fn os_answer(entries: &[OsProxyEntry]) -> (String, Vec<String>);   // PD-36: (PAC answer text, warnings)
/// FR-064's URL argument for BOTH evaluators (review round 3): an `https` hop → `scheme://host[:port]/` (path, query
/// and fragment dropped), any other hop → the URL as is. Shared, so the Windows OS path hands WinHTTP's script the same
/// reduced URL the in-process engine gets; `http.rs`'s OS path uses it on every OS (no `dead_code` on Windows).
pub(crate) fn pac_url_argument(url: &url::Url) -> String;
pub(crate) fn log_text(s: &str) -> String;                  // escape_debug, then ≤ 200 chars (script text in logs)
pub fn clock_civil(clock: PacClock, gmt: bool, warned: &mut bool) -> helpers::Civil;            // FR-095, PD-31
pub mod helpers {                                           // pure, tested without the engine (PD-26)
    pub struct Civil { … }  pub enum Arg { … }              // broken-down date; a helper argument (number or string)
    // Input caps (review round 3): None = an over-length argument, which the engine turns into a thrown TypeError.
    pub const MAX_SHEXP_SUBJECT: usize = 4096;             // bytes of shExpMatch's `s`
    pub const MAX_SHEXP_PATTERN: usize = 1024;             // bytes of shExpMatch's `pat`
    pub const MAX_HOST_ARG: usize = 253;                   // bytes of every host / domain argument (a DNS name's text)
    pub fn is_plain_host_name(h: &str) -> Option<bool>;
    pub fn dns_domain_is(h: &str, d: &str) -> Option<bool>;
    pub fn local_host_or_domain_is(h: &str, hd: &str) -> Option<bool>;
    pub fn dns_domain_levels(h: &str) -> Option<u32>;
    pub fn sh_exp_match(s: &str, pat: &str) -> Option<bool>; // iterative greedy wildcard matcher, see below; no regex
    pub fn is_in_net(ip: Ipv4Addr, pattern: Ipv4Addr, mask: Ipv4Addr) -> bool;
    /// The host-touching helpers as plain functions over `&dyn PacHost` (no memo, no cap: those are `DnsMemo`'s), so
    /// SC-033's `dnsResolve` / `isResolvable` / `isInNet`-by-name / `myIpAddress` rows run on every host (spec
    /// `spec.md:1225-1230`); the engine wraps them with `DnsMemo`.
    pub fn dns_resolve(host: &dyn PacHost, name: &str, wait: Duration) -> Option<Ipv4Addr>;  // IP literal: no lookup;
                                                    // `name` over MAX_HOST_ARG: the engine throws before calling this
    pub fn my_ip_address(host: &dyn PacHost) -> Ipv4Addr;                                      // IPv6 answer → 127.0.0.1
    pub fn weekday_range(now: &Civil, args: &[Arg]) -> Option<bool>;   // None = bad arguments → script throws
    pub fn date_range(now: &Civil, args: &[Arg]) -> Option<bool>;
    pub fn time_range(now: &Civil, args: &[Arg]) -> Option<bool>;
    pub fn civil(unix_secs: i64) -> Civil;                 // days-from-civil, by hand (PD-31)
}

// ---- engine glue, macOS and Linux only (R-8, R-15) ----------------------------------------------------------------
#[cfg(not(windows))] #[derive(Clone, Copy, Debug)]
pub struct PacLimits { pub eval_budget: Duration, pub memory: usize, pub stack: usize }
#[cfg(not(windows))] impl PacLimits { pub fn production() -> Self; }   // the constants above; tests use production()
/// One compiled script for one `http::get` call (PD-28). Not `Send`: rquickjs's `Runtime` is `Rc`-based without
/// the `parallel` feature, so the session lives and dies on the calling thread (no engine thread exists).
#[cfg(not(windows))]
pub struct PacSession { rt: rquickjs::Runtime, ctx: rquickjs::Context, deadline: Rc<Cell<Instant>>, … }
#[cfg(not(windows))]
impl PacSession {
    pub fn open(script: &str, host: PacHostHandle, limits: PacLimits, deadline: Instant) -> Result<Self, PacError>;
    pub fn find_proxy(&mut self, url: &url::Url, deadline: Instant) -> Result<Vec<ProxyChoice>, PacError>;
}
#[cfg(not(windows))]
pub fn evaluate(script: &str, url: &url::Url, host: PacHostHandle) -> Result<Vec<ProxyChoice>, PacError>; // SI-25
#[cfg(not(windows))] pub(crate) struct DnsMemo { … }        // PD-29, FR-094: ≤ 16 names, memoised, 1 s wait each
```

**Zero warnings on Windows without an `#[allow]`** (`workspace.lints.clippy allow_attributes = "deny"`,
`apps/krate-manager/Cargo.toml:13-33`): the shared half's `pub(crate)` items (`parse_answer`, `os_answer`, `log_text`,
`pac_url_argument`) are all used by `http.rs`'s OS path; `clock_civil`, `helpers`, `wpad_dns_candidates` and the engine constants, which
only the engine or the macOS / Linux host uses, are `pub`, as `sysproxy`'s parsers are, and a `pub` item of the library
never warns `dead_code`. Everything else the engine alone uses is `#[cfg(not(windows))]`.

**`open`** (engine, in this order): `script.len() > MAX_PAC_BYTES` → `E_PAC_SCRIPT` ("script too large"; the fetch
already capped it, the check covers a direct `evaluate`); `Runtime::new()`; `set_memory_limit(limits.memory)`;
`set_max_stack_size(limits.stack)`; `set_interrupt_handler(Some(Box::new(move || Instant::now() >= deadline.get())))`
over a shared `Rc<Cell<Instant>>` that `open` and every `find_proxy` set before running code;
`Context::custom::<(Eval, RegExpCompiler, RegExp, Json)>` (PD-26); register the helpers (below) on `ctx.globals()`;
`ctx.eval::<(), _>(script)` with the deadline `min(now + eval_budget, deadline)`; then `globals().get::<_,
Function>("FindProxyForURL")` must exist, else `E_PAC_SCRIPT` "no FindProxyForURL". Any `rquickjs::Error` (a syntax
error, a throw, the interrupt's uncatchable exception, "out of memory", a stack overflow) is `E_PAC_SCRIPT` with the
caught exception's message (`Ctx::catch`, `ctx.rs:252`) passed through `pac::log_text`.

**Script-derived text in logs** (challenge round 2): every string that comes from the script or from the OS answer (a
caught exception's message, an answer entry named in an `E_PAC_RESULT` warning, the answer itself in a "not usable"
cause, a Windows entry host) reaches a log line only through `pub(crate) fn log_text(s: &str) -> String` =
`s.escape_debug()` (std `str::escape_debug`, https://doc.rust-lang.org/std/primitive.str.html#method.escape_debug: CR,
LF, tab and other control characters become `\r`, `\n`, `\t`, `\u{…}`), then truncated to 200 `char`s on a char boundary
(`chars().take(200)`) with `…` appended when cut. So a hostile script cannot forge log lines (`throw "x\n<forged line>"`)
in the Info-and-above production log (`src-tauri/src/log_sink.rs:15`, `:54`) users send with support requests. `alert`
still logs nothing.

**`find_proxy`** (engine): set the deadline to `min(now + eval_budget, deadline)`; call
`FindProxyForURL(pac_url_argument(url), host_of(url))`; a non-string result (`null`, `undefined`, a number, an object)
→ `E_PAC_RESULT`; a string → `parse_answer`, whose warnings become one `warn!` each (`E_PAC_RESULT <log_text(entry)>`).
A session whose call failed is not called again (§4.5.1).

**`parse_answer`** (every OS; FR-067, PD-27, PD-33): over 64 KiB → `E_PAC_RESULT`; split on `;`, trim each entry; empty →
skipped silently; first whitespace-separated word case-insensitive: `DIRECT` (no argument) → `Direct`; `PROXY` / `HTTP` →
`http://host:port`, `HTTPS` → `https://…`, `SOCKS` / `SOCKS4` → `socks4://…`, `SOCKS5` → `socks5://…`; `host:port` with
the port 1-65535, `[v6]:port` accepted, no port → 80 / 443 / 1080; anything else (an unknown keyword, no host, a bad
port, extra words) → skipped with a warning; after the sixteenth usable entry the rest is dropped with one warning;
zero usable entries → `E_PAC_RESULT`. It is the one parser both evaluators feed: the QuickJS string directly, the
Windows entry list through `os_answer` (PD-36).

**`os_answer`** (every OS; PD-36): per entry in order, `Direct` → `DIRECT`, `Http` → `PROXY h`, `Https` → `HTTPS h`,
`Socks` → `SOCKS h`, where `h` is `host:port`, or `host` when `port` is 0, with an unbracketed IPv6 literal bracketed;
`Other(n)`, an empty host (for a non-`Direct` entry) and a host holding `;`, whitespace or a control character are
skipped with one warning each (`E_PAC_RESULT: OS proxy entry … skipped`, the host through `log_text`); the rest joined
with `"; "`. The text then goes through `parse_answer` in `http.rs` (§4.5.1), so the caps and the warning rules are one
code path.

**Helpers** (engine) registered with `Function::new` (each closure owns an `Arc` clone of the host and a per-session
`Rc<RefCell<DnsMemo>>`): `isPlainHostName(h)`, `dnsDomainIs(h, d)`, `localHostOrDomainIs(h, hd)`, `dnsDomainLevels(h)`,
`shExpMatch(s, p)` (pure, `helpers::*`); `isResolvable(h)`, `dnsResolve(h)` (dotted IPv4 string or `null`; an IPv6-only
answer is `null`), `isInNet(h, pat, mask)` (`h` resolved first when it is not an IPv4 literal; unresolvable → `false`)
through `DnsMemo::lookup(host, name)` = an IP literal parsed without a lookup, else a memo hit, else (≤ `PAC_MAX_LOOKUPS`
names) `helpers::dns_resolve(host, name, min(PAC_DNS_WAIT, deadline - now))`, else `None` with one `warn!` "DNS lookup
cap" (FR-094); `myIpAddress()` → `helpers::my_ip_address(host)` as a dotted IPv4 string; `weekdayRange`, `dateRange`,
`timeRange` over `clock_civil(clock, gmt, &mut warned)` (`"GMT"` as last argument → UTC fields; `local_offset_secs:
None` → UTC plus one `warn!` per session, FR-095). **Once per session, concretely:** the `PacSession` owns one
`Rc<OnceCell<PacClock>>` and one `Rc<Cell<bool>>` `warned` flag, both shared by the three time helpers' closures and by
every `find_proxy` call of the session; the first time-helper call of the session fills the cell from `host.now()`
(the only `now()` call site in `pac.rs`) and every later call, in the same or a later hop, reads the cell; `warned` is
never reset within the session. A session whose script calls no time helper calls `now()` zero times. The time helpers take MDN's argument forms, reversed
ranges matching only their endpoints (R-14, SI-35); `alert(_)` → nothing (it never logs script text). A helper given bad
arguments throws a `TypeError`, which the script may catch; uncaught it is `E_PAC_SCRIPT`.

**Every helper call has a stated cost bound (review round 3).** No helper is interruptible (the interrupt handler runs
between bytecodes and inside libregexp only, PD-27), so each native call must be cheap on its own; the script can only
repeat calls, and between calls the interrupt fires. (1) **Input caps:** an argument longer than its cap makes the
helper throw a `TypeError` (`helpers::*` return `None`; uncaught, `E_PAC_SCRIPT` and the FR-068 fallback; caught, the
script continues under its 2 s budget): `shExpMatch`'s `s` over `MAX_SHEXP_SUBJECT` = 4096 bytes or `pat` over
`MAX_SHEXP_PATTERN` = 1024 bytes; every host or domain argument of `isPlainHostName`, `dnsDomainIs`,
`localHostOrDomainIs`, `dnsDomainLevels`, `isResolvable`, `dnsResolve` and `isInNet` (its host) over `MAX_HOST_ARG` = 253
bytes (the text form of a DNS name; an over-length name is refused before `DnsMemo` or `PacHost::resolve` sees it).
`isInNet`'s pattern and mask must parse as IPv4 literals (≤ 15 bytes) or the call throws. (2) **`sh_exp_match`'s
algorithm** is the iterative greedy wildcard matcher with a single backtrack point: walk `s` and `pat` byte by byte; on
`*` record (star position, subject position) and advance the pattern; on `?` or an equal byte advance both; on a
mismatch with a recorded star restart the pattern after that star and the subject one past the recorded position; with
no recorded star fail; after `s` is consumed, only trailing `*`s may remain. No recursion and no allocation, worst case
`O(len(s) · len(pat))` steps, so under the caps at most 4096 × 1024 ≈ 4.2 × 10⁶ byte comparisons per call. A naive
recursive backtracker (exponential in the number of `*`s) or a regex translation is not allowed. (3) The remaining pure
helpers are linear in their capped inputs; the DNS helpers are bounded by `PAC_DNS_WAIT` and `PAC_MAX_LOOKUPS` (PD-29);
the time helpers by their fixed argument count (at most 7 arguments, more throws).

**`InertPacHost`** and **`FakePacHost`** (in `pac_tests.rs`, compiled on every OS: a name → `Option<IpAddr>` table, a
call log, a fixed `PacClock` with a `now()` call count (FR-095), an injected candidate list, the URL each
`os_find_proxy` call received, and the OS mode of §4.11.1 with its scripted `os_find_proxy`
queue and call count; it records the `wait` each `resolve` and `os_find_proxy` received, so `dns_bound_cases` and
`os_pac_failure_cases` can assert the wait never exceeds `PAC_DNS_WAIT` / `PAC_PHASE_BUDGET` or the remaining budget).
The real bound (`http::bounded` over a thread) is proven by `http::tests::bounded_call_abandons_after_wait` with a
closure that sleeps, and on the OS path by `os_pac_failure_cases`' blocking row.

---

## 5. Dependencies

| Crate | Version | Where | Why / justification | Verified against |
|---|---|---|---|---|
| `ureq` | 3.5.0, features defaults + `platform-verifier`, `socks-proxy` | core | Blocking client with no async runtime, `https_only`, explicit proxy and TLS roots (D4, PD-2) | crate source §1.2 |
| `rustls` | 0.23.46, `default-features = false` | core | Only to name `rustls::Error` for the FR-044 downcast; one resolved copy | ureq `Cargo.toml.orig:82` |
| `rustls-platform-verifier` | 0.7.1 (transitive) | core | OS trust store (D4) | crate source §1.2 |
| `ring` | 0.17.x (transitive, `rustls` `ring` feature through `ureq`'s `_ring`) | core | rustls provider; same as the updater's | `tls/rustls.rs:145-148` |
| `webpki-roots` | transitive, unused | core | Compiled in by `ureq`'s default `rustls` feature (R-5) | `Cargo.toml.orig:25,36` |
| `base64` | 0.23.1 (in lock) | core | Canonical base64 check (FR-005) | spec External facts |
| `regex` | 1.13.1 (in lock) | core | `pattern` evaluation (D2) | spec External facts |
| `flate2` | 1.1.10 (in lock) | core dev | `Reply::Gzip` bodies | spec External facts |
| `rquickjs` | 0.14.0 (new; + `rquickjs-core` 0.14.0, `rquickjs-sys` 0.14.0, `allocator-api2` 0.2.21 new; `hashbrown` 0.17.1, `foldhash` 0.2.0, `equivalent` 1.0.3 and build-time `cc` 1.10.0 already locked), default features (`std`) only | core, **`[target.'cfg(not(windows))'.dependencies]` only** (R-15): compiled and linked on macOS and Linux, absent from the Windows build | The in-process PAC engine for macOS and Linux (R-1, R-8, PD-25): interrupt + heap limit; vendored QuickJS-ng, bundled bindings for the macOS and Linux targets; +1,034,752 bytes measured on Windows x64 release in a scratch crate, now an estimate for the macOS / Linux binaries (§4.1); MIT; RustSec check before every release (FR-093) | crates.io API + crate sources §1.2; scratch build |
| `windows-sys` | 0.61.2 (in lock), exactly `Win32_Foundation`, `Win32_Networking_WinHttp` | src-tauri, Windows | R-2 (`WinHttpGetIEProxyConfigForCurrentUser`, `GlobalFree`) and R-15 / R-16 (`WinHttpOpen`, `WinHttpSetStatusCallback`, `WinHttpCreateProxyResolver`, `WinHttpGetProxyForUrlEx`, `WinHttpGetProxyResult`, `WinHttpFreeProxyResult`, `WinHttpCloseHandle`, `ERROR_IO_PENDING`), all inside the two features (§1.2); no `Win32_System_Time` (R-13, SI-32 withdrawn) | §1.2 |
| `system-configuration` | 0.8.0 (+ `system-configuration-sys` 0.6.0, new) | src-tauri, macOS | R-3: `SCDynamicStoreCopyProxies` only; its `core_foundation` re-export is never named (FR-075, §4.11.1) | §1.2 |
| `core-foundation` | 0.10.1 (in lock; the version `system-configuration` 0.8.0 builds on, so one resolved copy) | core, macOS target table | R-6: `sysproxy_cf::from_cf_proxies` and `sysproxy_cf::system_utc_offset` (PD-31, SI-37), named only in `sysproxy_cf.rs` and its tests | §1.2; `core-foundation-0.10.1/src/boolean.rs:24,47`, `timezone.rs:45,52` |
| none | — | Linux | `gsettings`, `kioslaverc` and `date +%z` through `std` (R-4, R-13) | §1.2 |

No npm package, Tauri plugin, frontend file or capability changes (FR-062). `serde_json`'s `preserve_order` is
deliberately not enabled (SI-8): it would change map order for `tauri` too.

---

## 6. Safety design

- **Injected roots.** The only path written is `<state_dir>/feed-cache.json` and its `.feed-cache.json.*` temp file;
  `check` reaches it only through `ctx.roots.state_dir()`. Every writing test builds `AppContext` from
  `fresh_temp_roots()` (Phase 4 FR-020, `test_support.rs:117-126`). `sysproxy` reads nothing; the real readers in
  `os_proxy.rs` read and never write (registry read through WinHTTP, SystemConfiguration copy, `gsettings get`, one file
  read) and are called only from `run()`; `production_config_only_in_allowed_tests` proves no test calls
  `.system_proxy(`.
- **The network.** `LoopbackOnly` refuses every non-loopback hop and every non-loopback proxy before a socket opens
  (FR-042, `check_proxy`); the fixture server binds `127.0.0.1:0` (or `[::1]:0`), opens no outbound socket, and
  answers a `CONNECT` to any other authority `403` (`connect_to_remote_host_is_refused`). A proxy the HTTP client
  refuses is `E_FEED_URL`, never a silent direct connection (PD-23).
  `HttpConfig::production()` / `UrlPolicy::HttpsOnly` appear only in seven pure or refusing test bodies and the opt-in
  live test. No code names `api.github.com` (roster scan over sources and over the built binary and `dist/`).
- **Verify before use.** Phase 5 downloads, extracts and executes nothing. The document is validated as a whole
  (schema + code rules) before any value reaches the model; install names reach later phases only as `PlainName`
  (re-checked), asset file names only as `AssetName` (taken from the parsed URL's last segment, PD-21), and URLs only
  as one of two checked types that never convert into each other (review round 3): **only `PlatformAsset`, which
  carries `sha256` and `signature`, holds a URL the later download phases may fetch for install** (`AssetUrl`); the
  release-page link `download_url` is a `PageUrl` (v1 entries carry it with nothing to verify it), which Phase 7's
  downloader cannot accept by type (`feed_model_seams_hold`). A v1 document cannot widen this: its keys must pass `pluginName` and its `download_url` the C1 `url`
  rule, or they are dropped (PD-22). An unrepresentable or unsafe value drops only its asset (R-7). Signature texts
  are parsed structurally; cryptography is Phase 7's.
  The zip-slip guard and the helper's operation set are Phases 8 and 10 and are untouched (the helper is still Phase
  4's stub).
- **PAC scripts and WPAD answers (R-1, R-15), what holds on every OS.** A PAC script is untrusted input that only
  chooses routing. Whoever controls it (an on-path attacker of an `http` PAC URL, or whoever answers WPAD on the local
  network) can send the feed and later downloads through a proxy of its choice (which then sees the target host names in
  `CONNECT` and can refuse, delay or drop them: denial of service and metadata), make the manager open TCP connections
  to up to 16 hosts per hop of its choosing (no payload beyond a `CONNECT` line or a TLS ClientHello), and on macOS and
  Linux burn up to the PAC bounds of CPU and memory. It cannot read or alter the feed or an asset: the feed is HTTPS
  against the OS trust store on every hop (D4, `https_only` on the feed agent) and Phase 7 verifies every asset's
  signature before use. The answer reaches the walk only through `parse_answer` (both OSes' answers, PD-36), whose caps
  (64 KiB, 16 entries) and `check_proxy` bound what it can ask for; every script- or OS-derived string reaches a log
  line only through `log_text` (no forged log lines). A failure is one `warn!` and the fallback, never an error of the
  check, a panic or a killed thread or process (FR-068). The cached feed and "check now" remain the recovery;
  `HTTPS_PROXY` wins over every OS source.
- **Windows: the OS evaluates (R-15, R-16 as recommended).** The manager neither fetches, size-bounds nor runs the script
  (FR-092). `WinHttpGetProxyForUrlEx` "always executes out-of-process" (PD-35, §1.2), so the script, its download and
  WPAD discovery run in the WinHTTP Web Proxy Auto-Discovery service, not in the manager process, and the manager gets
  only an entry list. **Residual risk on Windows:** a bug in Windows' own PAC engine or downloader is Microsoft's and
  runs in that service, not as code in the manager; the routing consequences above remain; the script runs under
  WinHTTP's limits, which this plan does not state (Microsoft documents a 60 s run limit and a 1 MB file limit for
  Windows XP SP2 and Server 2003 only, §1.2); the manager offers no domain credentials to a PAC server
  (`fAutoLogonIfChallenged = FALSE`, R-17). **Callback memory safety (review round 3):** no path frees the callback
  context before the resolver's `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` (the callback is registered with
  `WINHTTP_CALLBACK_FLAG_HANDLES` and the context is bound to the resolver with `WINHTTP_OPTION_CONTEXT_VALUE`); the
  waiting thread only closes handles, on the normal, error and cancel paths alike, because WinHttpCloseHandle's remarks
  say callbacks can still arrive after the close returns and the context must be kept until `HANDLE_CLOSING`
  (PD-35, §1.2). If the context cannot be bound, it is leaked, never freed early. This path is untested by design
  (it would call the real WinHTTP service), so the rule is stated here and reviewed, not proven by a test.
  What the manager's own process still bounds: the wait (`http::bounded`,
  `PAC_PHASE_BUDGET`, cancelled by closing the resolver, PD-35), the answer size and entry count, and the proxies it
  will dial. The R-15 measurement shows the path is live by default on a real profile (auto-detect on, flags byte
  `0x09`, §4.14). **If R-16 is ruled (i) or (ii)**, this bullet no longer holds when the service cannot take the call:
  WinHTTP then runs the script with Microsoft JScript inside the manager process (§0.3 R-16), which is the in-process
  risk below with a different engine.
- **macOS and Linux: the in-process engine (R-8, R-14).** The script runs in a QuickJS context holding the ruled helpers
  and four inert intrinsics (PD-26): no file, socket, process, environment, timer, module loader or clock outside
  `PacHost`, measured (`typeof require/std/os/fetch/setTimeout/print/console/Date` all `"undefined"`, §1.2). Bounds:
  1 MiB script, 2 s per compile and per call (interrupt handler, also inside regex backtracking), 32 MiB heap, 256 KiB
  stack, 16 DNS names at ≤ 1 s each, 10 s for the whole PAC phase, all inside the 30 s chain (PD-27, PD-29); and,
  because a native helper cannot be interrupted, a cost bound per helper call: argument caps (4096 / 1024 bytes for
  `shExpMatch`, 253 for every host argument, over-length throws) and an `O(len(s) · len(pat))` iterative wildcard
  matcher, so one call is at most about 4.2 × 10⁶ byte comparisons (§4.15); with
  `panic = "abort"` an engine allocation failure is QuickJS's own `out of memory` exception, not a Rust allocation
  abort, because the heap limit is enforced by QuickJS's allocator (rquickjs's default libc allocator; `rust-alloc` is
  barred, PD-25, so the limit is never a no-op). The PAC is fetched direct, never through a proxy or another PAC
  answer, with no userinfo, and logged without its query (PD-19). **Residual risk on macOS and Linux, not bounded by the
  above:** the engine is C (QuickJS-ng: `quickjs.c`, `libregexp.c`, `libunicode.c`, `dtoa.c`, `rquickjs-sys`
  `build.rs:143`) running inside the manager process on the calling thread, with no OS sandbox. PD-26 / PD-27 are
  language-level limits; they do not contain a memory-safety bug in the C engine. Such a bug, reached by a script from
  a LAN responder for `wpad` or an on-path attacker of an `http` PAC URL, is **code execution as the user** in the
  manager process. R-15 accepts that risk on these two OSes, where the spec records auto-detect as off by default
  (Safety, `spec.md:1376`), and mitigates it with the exact pin (`rquickjs = "0.14.0"`,
  `reader_dependencies_are_the_ruled_set`), so an engine update is always a reviewed change, and the RustSec advisory
  check before every manager release (FR-093, §4.14).
- **Resource limits.** 4 MiB of decoded body (`take(max + 1)` over the decoding reader); one chain deadline of 30 s
  (PD-3); 10 s connect; 5 hops; the PAC bounds above; the fixture server bounds every read (64 KiB head, 16 KiB TLS
  record).
- **TLS.** The OS trust store only (`RootCerts::PlatformVerifier`, SC-026, for the feed agent and the PAC-fetch agent
  alike); `https_only` on every **feed** hop in production, checked twice (our `next_hop` and `ureq`'s
  `RequireHttpsOnly`). The macOS / Linux PAC fetch runs with `AgentPurpose::PacFetch` (`https_only(false)`, PD-28)
  because FR-065 admits `http` PAC URLs; its scheme rule is checked once per hop, by `check_pac_url`. On Windows the
  manager makes no PAC fetch (WinHTTP's own rule: `http:` and `https:` PAC URLs only, §1.2).
- **Credentials.** Proxy userinfo is redacted in `Debug`, `Display` and every log line (PD-19), in `ProxyConfig`,
  `ProxyVars`, `ProxyEnv` and `SystemProxy` alike, so an `assert_eq!` failure message cannot print it; it reaches only the
  proxy, as `Proxy-Authorization` on `CONNECT` (ureq `connect.rs:116-124`).
- **Tauri capability set and CSP:** unchanged (FR-062).
- **What tests never touch:** the real state dir and its `feed-cache.json`, any plug-in or preset folder,
  `ProgramData`, the registry (no test calls the WinHTTP reader; `take_win_ie` is tested over test-owned buffers),
  dconf/gsettings (the bounded runner is tested with `sleep` / `printf` only, guarded by
  `production_config_only_in_allowed_tests`), the real SystemConfiguration store (`from_cf_proxies_cases` builds its own
  dictionary), any non-loopback address, a real DNS lookup, the real local address, the real clock or time zone, the
  real WPAD discovery, and the real Windows OS evaluation or the WinHTTP auto-proxy service (every PAC test uses
  `FakePacHost` or `InertPacHost` and loopback PAC URLs; `OsPacHost`, `std_resolve(`, `std_my_ip(`, `pac_host(` are
  banned from test sources, §4.11.1, and `src-tauri` has no test binary).

---

## 7. Test plan

All Rust tests run with `cargo test --workspace --locked` from `apps/krate-manager/` (default threads). Fixtures are
read in place through `env!("CARGO_MANIFEST_DIR")` + `../../../../tools/feed-contract/fixtures/`. Each test starts its
own `FixtureServer` and its own `fresh_temp_roots()`; no fixed port, no global lock (FR-086). Proxy and cache tests
use `HttpConfig::loopback(1 s, 1 s)` unless stated.

### 7.1 SC → test

| SC | Test (file) | Assertion strategy |
|---|---|---|
| SC-001 | §7.4 commands | exit codes, warning counts, status snapshots |
| SC-002 | `contract::tests::c1_cases_reproduce` (`contract_tests.rs`); `feed::tests::accept_rows_parse_through_the_client` (`feed_tests.rs`) | every row of `cases.json` (count = file length); accept: 0 findings (v2 rows also 0 under V1); a-w-p: 0 + `resolve(...) == NotThroughManager` for the named plugin on the four FR-034 pairs; error: `X` among codes or `"<path> <keyword>"` pairs; client: every accept file parses, keys and `schema_version` equal the document's |
| SC-003 | `contract::tests::gen_feed_outputs_accepted`; `feed::tests::gen_feed_outputs_parse` | glob `gen-feed/*/expected.v2.json` and `expected.legacy.json` (counts ≥ 1, printed); 0 findings; `parse` Ok with `schema_version` 2 / 1 |
| SC-004 | `feed::tests::schema_version_dispatch` | in-memory edits of `full-v2.json`; the `schema_version-const` file; malformed, BOM, `0xFF` byte, `<html>` page → `E_INPUT` |
| SC-005 | `feed::tests::model_of_full_v2`, `unknown_keys_do_not_change_the_model`, `prerelease_sibling_is_ignored`, `asset_name_cases`, `v1_entries_are_sanitized` (`model_range_cases` is SC-037's row) | expected `Feed` built in the test from README facts (`size` = `format!("krate fixture asset {name}\n").len()`, `sha256` from the document) and `assert_eq!`. `asset_name_cases`: asset URLs ending `/a/` (empty), `/x.exe:ads`, `/CON.exe`, `/%2e%2e`, `/a.exe?x=/..`, `/a/..` → the asset dropped with one `E_MODEL_ASSET_NAME` `warn!`, except `/a.exe?x=/..`, whose name is `a.exe` (the query is not a segment); a normal `Iterum-1.0.0-Windows-x64.exe` is kept verbatim. `v1_entries_are_sanitized` (FR-029(c), PD-22): a v1 document with entries whose `"download_url"` is `"file:///etc/passwd"` and `"javascript:x"` (each entry → absent from `Feed.plugins`, one `warn!` `E_MODEL_V1_ENTRY /plugins/<key>/download_url` each), an `https` URL (→ kept, `Some`), no `download_url` (→ kept, `None`, no warn) and a key `"../X"` (→ absent, one `warn!` `E_MODEL_V1_ENTRY /plugins/..~1X`); every kept entry equals the same document's entry without the dropped ones |
| SC-006 | `version::tests::resolve_table` | every `(Os, Arch)` pair × plugin of `full-v2`, `mixed`, `unknown-platform-key`, `v1-only`; `platform_candidates` table |
| SC-007 | `version::tests::native_candidates_{windows,macos,linux}` (`#[cfg(target_os = …)]`) | branch on `cfg!(target_arch)`; equals the SC-006 row |
| SC-008 | `version::tests::parse_core_cases`, `reduce_class_version_cases`, `compare_cases` | the listed strings |
| SC-009 | `feed::tests::check_fresh_writes_cache`, `check_now_sends_no_cache` | recorded request headers; the cache file's text equals a hand-written expected string (the four D8 keys in order `cache_version`, `fetched_at_unix`, `source_url`, `body`, two-space pretty print, final LF; FR-052), not a `Value` comparison, which would lose the order; `User-Agent` vs `env!("CARGO_PKG_VERSION")`; `apps/krate-manager/version.json` read at test time; two GETs for Normal then CheckNow |
| SC-010 | `feed::tests::rejected_feed_keeps_last_good_copy`, `broken_sibling_never_blocks_stable`, `sibling_prefix_filter` | seed with `full-v2`, `set_route` to each rejection row's file; bytes and mtime unchanged; for an `E_FEED_INVALID` row the error's `to_string()` starts with `E_FEED_INVALID`, contains the finding count, and contains the first finding's `instance_path` (FR-020); the seven sibling rows accepted with one `warn!` each (`take_mine`); boundary cases incl. the PD-9 duplicate in both key orders |
| SC-011 | `feed::tests::offline_returns_cached_flagged_stale` | `Close`, `Garbage`, `Stall(30 s)` with and without cache; `Instant` < 5 s; one request each; after every case with a seeded cache, `feed-cache.json` is byte-identical to the seeded file with the same mtime, and the returned `fetched_at_unix` equals the seeded value (FR-054 transport half); without a seed, no `feed-cache.json` exists afterwards |
| SC-012 | `http::tests::url_policy_cases`, `redirects_are_rechecked`, `next_hop_cases`, `status_and_body_errors`, `https_only_refuses_before_connecting`, `chain_shares_one_deadline`, `production_values`, `no_response_class_cases` | pure tables; fixture chains; boundary pair at `MAX_FEED_BYTES` and `+1`, both plain and `Gzip` (compressed length < 64 KiB asserted); `Truncated` → `E_FEED_BODY`; `HeadersThenStall { declared_len: 100, sent: 10 bytes, stall: 30 s }` under `loopback(1 s, 1 s)` → `E_FEED_TIMEOUT`, **not** `E_FEED_BODY` (proves the `ureq::Error::from(io::Error)` recovery, FR-044), elapsed < 2 s; `Reply::Gzip { body: b"not gzip" }` → `E_FEED_BODY` (ureq's gzip decoder raises `Error::Decompress("gzip", e).into_io()`, ureq `src/body/gzip.rs:23`, turned back by `error.rs:218-227`); `https://127.0.0.1:<port>` → `E_FEED_TLS`. `chain_shares_one_deadline` (PD-3): under `loopback(1 s, 1 s)`, `/h1` → `/h2` → `/h3` → `/ok`, each of the three redirects `Delayed { after: 400 ms }` and `/ok` an instant `Ok`; asserts `E_FEED_TIMEOUT`, elapsed < 1.5 s, at most three recorded requests and none for `/ok` (a per-hop `timeout_global` would return `Ok` after ~1.2 s). `no_response_class_cases` (pure, PD-33): `is_no_response` over constructed `ureq::Error` values (`HostNotFound`, `ConnectionFailed`, `ConnectProxyFailed(..)`, `Timeout(Connect)` with and without time left, `Timeout(Global)`, `Io` of `ConnectionRefused` and of `InvalidData` wrapping a `rustls::Error`, `StatusCode(503)`) and the same values through `map_error` (every advancing class is `E_FEED_OFFLINE`). `production_values`: `HttpConfig::production() == HttpConfig { policy: HttpsOnly, connect_timeout: 10 s, total_timeout: 30 s, proxy: None, pac: None, pac_host: None }` written as literals (FR-041; `CONNECT_TIMEOUT` / `TOTAL_TIMEOUT` also asserted as `Duration::from_secs(10)` / `(30)`) |
| SC-013 | `feed::tests::cache_rules`, `cache_write_fault_keeps_previous` | planted caches; a `FaultCacheIo` failing at each `CacheStep` (Unix adds `SyncDir`), as `settings_tests.rs:395-420` does; per fault, `take_mine()` holds exactly one `warn!` containing `E_FEED_CACHE_IO` and the step's name (FR-056) besides the one summary line |
| SC-014 | `feed::tests::roster_follows_the_feed` | counts vs the document's own keys |
| SC-015 | `roster_and_hosts_absent_from_sources`, `roster_scanner_reports_planted_names` (`tests/roster.rs`); Phase 4 tests still green | 0 hits, ≥ 9 names, plants reported |
| SC-016 | `manager-ci.yml` step `roster absent`; local hand run (§7.4) | positive control, 0 hits |
| SC-017 | `core_dependencies_are_the_fr007_set`, `ureq_is_confined_to_http_rs` (with the `rquickjs` / `UdpSocket` / `to_socket_addrs` extension, SI-31); compliance `cargo tree` records and the §1.2 verdict | exact sets, 0 findings |
| SC-018 | `context::tests::http_config_is_carried`; `real_src_tauri_config_passes_the_guard`, `planted_config_changes_fail_the_guard`; `http::tests::config_for_dev_url_cases` | struct equality, three cases (no proxy; a loopback `ProxyConfig`; a loopback `PacSource`, §4.9); `config_for_dev_url_cases` over a `ProxyResolution` with a static proxy and one with a PAC source plus a `FakePacHost` handle: loopback URL → `proxy`, `pac`, `pac_host` all `None`; non-loopback → all three kept (`PacHostHandle` equality by `Arc::ptr_eq`); plants, including a `lib.rs` without `.with_pac(` |
| SC-019 | compliance: `cargo test -p krate-manager-core --locked -- --list` and `… --features live-network --test live_feed --locked -- --list --ignored` | 0 / 1 occurrence of `live_feed_parses` |
| SC-020 | `contract::tests::trusted_comment_cases`, `minisig_text_cases` | `trusted-comments.json` rows; the KAT `.minisig` key id `440FA75168B852F7`; the named invalid files' Alpha `linux-x86_64` signature |
| SC-021 | `fixture_server::tests::binds_loopback_port_zero`; `production_config_only_in_allowed_tests`; shape test `--test-threads` ban | local addr `127.0.0.1`, two servers, distinct ports; `start_v6` local addr `[::1]` |
| SC-022 | `feed::tests::check_logs_one_line` | `install_capture()` + `take_mine()` counts per level |
| SC-023 | `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js"` | `# fail 0`, plan line |
| SC-024 | `feed::tests::concurrent_checks_leave_one_whole_file` | `Barrier(8)`; all eight `FeedCheck`s have `snapshot` `Some` with `source == SnapshotSource::Fresh` and `error == None`; every `Some(cache_error)` matches `FeedError::CacheIo { step: CacheStep::Persist, .. }` (FR-057: losing the rename race is a `persist` failure and the fetched feed is still returned); at least one `cache_error` is `None`; the final file parses as D8, its `body` is byte-equal to one served file and its `source_url` is that server's URL; state-dir listing before/after (only `feed-cache.json` added) |
| SC-025 | `contract::tests::ecma_pattern_translation` | the listed strings through the schema's own `$defs.url` / `$defs.reservedStem` patterns, plus `\d`/`\w`/`\b` → `Unsupported` |
| (FR-001) | `contract::tests::unsupported_schema_constructs_are_refused` | `compile_schema_text` over synthetic schemas, each `Err(Unsupported { pointer, keyword })` with the expected pair: `{"minLength":1}` → `("", "minLength")`; `{"type":"array"}` → `("", "type")`; `{"$ref":"#/$defs/missing"}` → `("", "$ref")`; `{"$ref":"other.json"}` → `("", "$ref")`; `{"pattern":"("}` → `("", "pattern")`; `{"pattern":"\\w"}` → `("", "pattern")`; nested `{"properties":{"a":{"minLength":1}}}` → `("/properties/a", "minLength")`; and a control `{"type":"object","$comment":"x"}` → `Ok` |
| (FR-002, FR-003) | `contract::tests::keyword_semantics_cases` (review round 3) | synthetic schemas through `compile_schema_text` (PD-24), each instance validated and the **exact** finding list compared as a sorted list of `(code, instance_path, keyword)` (every code `E_SCHEMA`), not "among": (1) `{"if":{"required":["a"]},"then":{"required":["p"]}}` on `{"a":1}` → `[("", "required"), ("", "if")]`, and on `{"a":1,"p":2}` and on `{}` → `[]`; (2) `{"if":{"required":["a"]},"then":{},"else":{"required":["p"]}}` on `{}` → `[("", "required"), ("", "if")]`, on `{"p":1}` → `[]`; (3) `{"properties":{"o":{"propertyNames":{"pattern":"^a"}}}}` on `{"o":{"b":1,"ab":2}}` → `[("/o", "pattern"), ("/o", "propertyNames")]` (the key's finding at the **object's** path, FR-002), on `{"o":{"ab":1}}` → `[]`; (4) `{"enum":["x","y"]}` on `"z"` → `[("", "enum")]`, on `"x"` → `[]`; `{"enum":[1,"x"]}` on `1.0` → `[]` (numbers mathematically); (5) `{"const":2}` on `2.0` → `[]`, on `3` → `[("", "const")]`; (6) `{"type":"integer"}` on `2.0` → `[]`, on `2.5` → `[("", "type")]`. The v2 schema uses all three keywords (`propertyNames` at `versions.v2.schema.json:11`, `:134`, `:171`; `if` / `then` / `else` at `:127-129`, `if` / `then` at `:166` / `:176` and `:189` / `:200`; `enum` at `:115`, read this session), and `cases.json` has no row expecting `if`, `propertyNames` or `enum`, so without this test an interpreter that never emits the extra `if` / `propertyNames` finding, or whose `enum` always passes, would pass `c1_cases_reproduce` |
| SC-026 | `http::tests::agent_trusts_the_os_store`; `ureq_is_confined_to_http_rs` | `matches!(agent_config(&c, None, p).tls_config().root_certs(), RootCerts::PlatformVerifier)` for both policies × every `AgentPurpose` the OS compiles; and (pure, PD-28) `agent_config(&HttpConfig::production(), None, AgentPurpose::Feed).https_only() == true`, plus, in a `#[cfg(not(windows))]` block (the variant does not exist on Windows), `agent_config(&HttpConfig::production(), None, AgentPurpose::PacFetch).https_only() == false`, so a production `http` PAC fetch (`pac::WPAD_DNS_URL` included) is not refused by ureq's `RequireHttpsOnly`; `LoopbackOnly` gives `false`; the guard proves `get`'s only agent is `Agent::new_with_config(agent_config(…))` and no other builder exists (§4.11.1) |
| SC-027 | `version::tests::os_version_cases`, `needs_newer_os_table` | the listed versions and cells. **As parsed (FR-037 `Display`, FR-035 "the asset's own value, parsed", FR-038; review round 3):** `os_version_cases` asserts `OsVersion::parse(s).unwrap().to_string() == s` for `11.0`, `2.31`, `10.0.19045` and `10.0.19045.1`, and `components()` of the four has lengths 2, 2, 3 and 4 with the parsed values, so a `Display` that pads or a parser that normalises fails although `11.0 == 11.0.0` holds; `needs_newer_os_table` compares `min_os.to_string()` (`"11.0"`, `"2.31"`, `"10.0"`) and `min_os.components()` of each `NeedsNewerOs` cell, not only `==` |
| SC-028 | `http::tests::resolve_proxy_cases`, `loopback_proxy_only`, `proxy_env_from_vars_cases`, `proxy_scheme_cases` | table over `ProxyEnv` × `SystemProxy` × target, asserting `ProxyResolution` values (PD-5, FR-048, FR-069 as written: environment proxy → `{ proxy, pac: None }` even with an OS static proxy and a PAC source present; OS static proxy beside a PAC source, no environment proxy → **both** `{ proxy: Some(static), pac: Some(PacSource { auto_detect, url }) }`, no `warn!` (SC-028 "with the OS static `proxy` kept beside it as the fallback"); the same with the target on the OS bypass list → `{ proxy: None, pac: Some(..) }` (the OS list applies to the static proxy only, FR-048(c)); OS static proxy only → `{ proxy: Some, pac: None }`; PAC source only → `{ proxy: None, pac: Some(PacSource { auto_detect, url }) }` for each of url only, auto-detect only, both; `NO_PROXY` matching the target with a PAC source present → the empty resolution; an unparsable `pac_url` with `auto_detect` → `pac: Some { auto_detect: true, url: None }` plus one `warn!`; PD-18 bypass entries; redaction, including `format!("{:?}", PacSource)` of `http://u:p@127.0.0.1/x.pac?token=s` showing neither `u:p` nor `token`). `proxy_env_from_vars_cases` over `ProxyEnv::from_vars`: upper only, lower only, both (upper wins), empty upper with lower set (lower wins), both empty (`None`), `https` unset with `all` set (then `resolve_proxy` uses `all`), and the `no` pair the same way; `format!("{:?}", …)` of a `ProxyVars` / `ProxyEnv` holding `http://u:p@127.0.0.1:1` and the schemeless `u:p@127.0.0.1:1`, and of a `SystemProxy` whose `pac_url` is `http://u:p@127.0.0.1/x.pac`, contains `***` and not `u:p`. `proxy_scheme_cases` (pure): under `HttpsOnly`, `http://`, `https://`, `socks4://` and `socks5://` proxies on a non-loopback host (`proxy.example.invalid:3128`, never contacted) are accepted by `check_proxy`, `ftp://`, `socks4a://` and `socks5h://` are `E_FEED_URL` (PD-34). `loopback_proxy_only` gains the PD-23 row: proxy `http://%FF@127.0.0.1:<proxy port>` passes `check_proxy` and fails `to_ureq_proxy` (ureq `decode_userinfo`, `proxy.rs:484-489`), so `get` is `E_FEED_URL` and both the proxy and the target fixture server record zero requests |
| SC-029 | `sysproxy::tests::{windows,macos,linux}_proxy_parser_cases` (SI-1 names); `sysproxy::tests::win_ie_strings_are_read_and_freed_once`, `kioslaverc_path_cases`, `kde_file_read_cases` (every leg); `sysproxy::tests::bounded_run_abandons_and_reaps` (`#[cfg(unix)]`: macOS, Linux) (SI-21); the desktop choice is SC-039's row and the CF conversion SC-040's | pure parsers over injected `WinIeProxyConfig`, `ScValue` pairs (`macos_proxy_parser_cases` holds every `…Enable` key once as `Int(1)` and once as `Bool(true)`, `Int(0)` / `Bool(false)` as off, and a `Text("1")` as absent), GVariant texts, `kioslaverc` texts. `win_ie_strings_…`: three `Vec<u16>` buffers (ASCII, a non-BMP character, a lone surrogate `0xD800` → U+FFFD) plus one null pointer, a recording `free`: each non-null pointer freed exactly once, the null one never, strings as expected. `kioslaverc_path_cases` (every leg, portable fixtures, §4.6): with `t = tempfile::tempdir()`, `kioslaverc_path(Some(&t.path().join("cfg")), Some(t.path()))` == `t.path().join("cfg").join("kioslaverc")`; a relative `Some(Path::new("rel"))` with home `t.path()` == `t.path().join(".config").join("kioslaverc")`; `None` with home → the same; `None` without home → `None`; no `/home/…` literal (it is not absolute on Windows). `kde_file_read_cases` (a `tempfile` dir): a missing file → default, no `warn!`; a `ProxyType=1` file → the proxy. `bounded_run_…`: `[printf "'manual'"]` → `Some(["'manual'"])`; `[sleep 1]` with bound 300 ms → `None`, returns in < 0.9 s (before the child exits), one pid in `abandoned`; on Linux `/proc/<pid>` is gone within 3 s (reaped, not left a zombie). |
| SC-030 | `http::tests::proxy_path_connect`, `proxy_path_http` (SI-2), `no_proxy_bypass`; `feed::tests::check_through_proxy_writes_cache`; `fixture_server::tests::connect_to_remote_host_is_refused` | proxy server records `CONNECT 127.0.0.1:<port2>` + `Host`; `ConnectOk` + non-TLS answer → `E_FEED_TLS`; `ConnectRefused(407)` / `Close` → `E_FEED_OFFLINE` + cached copy; tunnelled GET served; target server records nothing; `Proxy-Authorization: Basic dTpw` for `u:p@`; no captured log line contains `u:p`. `no_proxy_bypass` gains a per-hop case (FR-046): `no_proxy = ["::1"]`, the first hop `http://127.0.0.1:<p1>/a` goes through the loopback proxy (recorded `CONNECT 127.0.0.1:<p1>`, the tunnelled `GET /a` answered `Redirect` to `http://[::1]:<p2>/b`), the second hop goes direct to a `start_v6` server, which records `GET /b`, and the proxy records no second `CONNECT`. `connect_to_remote_host_is_refused`: a raw `TcpStream` to a server whose route table holds `ConnectOk` sends `CONNECT example.invalid:443 HTTP/1.1` (and, on a second connection, `CONNECT localhost:443`); the answer starts `HTTP/1.1 403`, the stream reaches EOF, and the request is recorded |
| SC-031 | `feed::tests::fresh_lower_version_replaces_cache` | in-memory lowered Alpha; four checks |
| SC-032 | `feed::tests::one_attempt_per_check` | five failure replies with and without cache; one request each; a two-hop chain to `503` = three requests; elapsed < total + 1 s; after each seeded case `feed-cache.json` is byte-identical with the same mtime and the returned `fetched_at_unix` is the seeded value (FR-054) |
| SC-033 | Every host: `pac::tests::pac_helper_cases`, `find_proxy_result_cases`. `cfg(not(windows))` (macOS, Linux): `pac::tests::url_argument_cases`, `script_world_cases` | `pac_helper_cases` (pure, the `helpers::*` functions called directly with injected values; `dnsResolve`, `isResolvable`, `isInNet` by name and `myIpAddress` through `helpers::dns_resolve` / `my_ip_address` over a `FakePacHost`): the spec's table (`spec.md:1225-1230`), expected values written from MDN's definitions, plus one reversed range per time helper (endpoints only, SI-35) and `"GMT"` vs local over `clock_civil` with a fixed `PacClock { unix_secs: 1_700_000_000, local_offset_secs: Some(3600) }`. `find_proxy_result_cases` (`parse_answer`, pure): the spec's rows plus `[::1]:3128`, port `0` / `65536` skipped with a warn, a 64 KiB + 1 answer → `E_PAC_RESULT`, 20 `DIRECT` entries → 16 kept and one warn (SI-29). `url_argument_cases` (engine): as the spec (`https` → `scheme://host:port/`). `script_world_cases` (engine, R-14, `spec.md:1236-1241`): a script returning `[typeof Date, typeof require, typeof std, typeof os, typeof fetch, typeof setTimeout, typeof print, typeof console].join()` → eight `undefined`; `alert("x")` returns without effect; scripts calling `isResolvableEx`, `dnsResolveEx`, `isInNetEx`, `myIpAddressEx`, `sortIpAddressList`, `convert_addr` → each `E_PAC_SCRIPT`; the reversed-range rows through a `FakePacHost` clock (`weekdayRange("FRI", "MON")` true Friday and Monday, false Wednesday and Saturday; `timeRange(23, 0)` true 23:30 and 00:30, false 12:00; a reversed `dateRange` true on its two endpoint dates, false between). **Helper cost bounds (review round 3, §4.15):** `pac_helper_cases` gains the pure rows `sh_exp_match` over `"a".repeat(4096)` and the 20-star pattern `"*a".repeat(20) + "*b"` → `Some(false)`, the same subject and the cap-sized pattern `"*a".repeat(511) + "*b"` (1024 bytes) → `Some(false)`, and the same subject against `"*a*"` → `Some(true)`; `"abc"` vs `"a?c"`, `"a*c"`, `"*"`, `""` vs `"*"` → true, `"abc"` vs `"a*d"`, `""` vs `"?"` → false (algorithm sanity); `sh_exp_match("a".repeat(4097), "*")`, `sh_exp_match("a", "x".repeat(1025))`, `dns_domain_is("h".repeat(254), ".x")`, `local_host_or_domain_is`, `is_plain_host_name` and `dns_domain_levels` over a 254-byte host → `None`; at exactly 4096 / 1024 / 253 bytes → `Some(_)`. `script_world_cases` gains the engine rows: `shExpMatch('a'.repeat(4096), '*a'.repeat(511) + '*b')` → `false`, the whole `FindProxyForURL` call returning within `PAC_EVAL_BUDGET` (a recursive backtracker would not); `shExpMatch('a'.repeat(1e6), '*a*b')` and `dnsResolve('h'.repeat(254))` uncaught → `E_PAC_SCRIPT` (the over-length `TypeError`) with zero `resolve` calls on the `FakePacHost`, and the same calls inside `try { … } catch (e) { return "DIRECT" }` → `[Direct]`. **Once per session (FR-095, `spec.md:656-660`):** a `PacSession` opened over a `FakePacHost` whose clock is `local_offset_secs: None` and a script whose `FindProxyForURL` calls `weekdayRange("SAT")`, `timeRange(1, 2)` and `dateRange(31)`, then `find_proxy` for two hops of the same session → the fake's `now()` count is exactly 1 and `take_mine` holds exactly one offset `warn!` for the session; a script calling no time helper → `now()` count 0. Local on this box: the two every-host tests; the engine tests in L-WSL (§8) and PENDING-CI |
| SC-034 | Every host: `http::tests::pac_policy_cases` (pure `check_pac_url` rows). `cfg(not(windows))`: `pac_path_proxy`, `pac_path_direct`, `pac_list_walk`, `pac_per_hop`, `pac_fetched_once_never_via_proxy`, and `pac_policy_cases`' fetch rows (in `#[cfg(not(windows))]` blocks) | as the spec (`spec.md:1244-1259`), with SI-34 (B records `CONNECT 127.0.0.1:<C>` then the tunnelled `GET /versions.json` for an `http` target C); every config built by `HttpConfig::loopback(1 s, 2 s).with_pac(Some(PacSource { auto_detect: false, url: Some(A.url("/p.pac")) })).with_pac_host(Some(fake))`; `pac_list_walk` with all three entries failing ends `E_FEED_OFFLINE`, each proxy records exactly one `CONNECT`, and `feed::check` over the same config returns the seeded cache; elapsed < total + 1 s in every case. **Non-advancing rows (review round 3; FR-067 "any response, any TLS failure, a status, a body failure or the total timeout ends the walk with that outcome", `spec.md:607-611`):** the script answers `PROXY 127.0.0.1:<B1>; PROXY 127.0.0.1:<B2>; DIRECT`; (a) target `https://127.0.0.1:<C>/versions.json`, B1's route for `127.0.0.1:<C>` is `ConnectOk` (B1 then serves the tunnel in plain HTTP, its TLS-record answer of §4.7) → `E_FEED_TLS`, B1 records the `CONNECT`, B2 and C record **zero** requests; (b) target `http://127.0.0.1:<C>/versions.json`, B1 `ConnectOk` and its tunnelled `/versions.json` route `Status(503)` → `E_FEED_HTTP_STATUS { status: 503 }`, B1 records the `CONNECT` and the tunnelled `GET`, B2 and C record zero; (c) the same with B1's tunnelled route `Stall(30 s)` under `loopback(1 s, 2 s)` → `E_FEED_TIMEOUT` (`Timeout::Global`, after the connection is set up), elapsed < 3 s, B2 and C record zero. A walk that advanced on every error, or on a TLS failure or a status, fails (a), (b) or (c). **PAC redirect rows (FR-065 "with this scheme rule applied per hop", `spec.md:585-586`)**, in `pac_policy_cases`' `#[cfg(not(windows))]` block: (d) under `LoopbackOnly`, PAC server A answers `/p.pac` with `Redirect { status: 302, location: "http://example.invalid/p.pac" }` → exactly one `E_PAC_FETCH` `warn!`, the fallback taken (the target is reached direct, `proxy: None`), and A records exactly `/p.pac` (no request leaves for the redirect target, SC-021); (e) A answers `/p.pac` with `Redirect { status: 302, location: "/q.pac" }` and `/q.pac` serves a script answering `PROXY 127.0.0.1:<B>` → B records the `CONNECT`, no PAC `warn!`, and A records exactly `/p.pac` then `/q.pac`. Pure rows of the same block over `http::next_pac_hop` (§4.5.1, the PAC fetch's redirect step with `check_pac_url` in place of `check_url`): `next_pac_hop(HttpsOnly, https://pac.example.invalid/p.pac, 302, Some("http://pac.example.invalid/q.pac"))` → `Follow(http://pac.example.invalid/q.pac)` while `next_hop` with the same arguments is `E_FEED_URL` (so a fetch that used `check_url` per hop would refuse a legitimate https→http PAC redirect); `Some("file:///p.pac")` → `E_FEED_URL`; `next_pac_hop(LoopbackOnly, http://127.0.0.1:1/p.pac, 302, Some("http://example.invalid/p.pac"))` → `E_FEED_URL` (nothing is contacted: these are pure calls). The Windows twin of the walk is SC-041's `os_pac_walk` |
| SC-035 | Every host: `pac::tests::wpad_candidate_cases`; the FR-069 rows of `resolve_proxy_cases` (SC-028). `cfg(not(windows))`: `http::tests::wpad_discovery_walk`, `pac_fallback_to_static_proxy` (SI-39) | `wpad_candidate_cases` (pure): `WPAD_DNS_URL == "http://wpad/wpad.dat"`, it passes `check_pac_url` under `HttpsOnly`, its host is the single label `wpad`, `wpad_dns_candidates() == [WPAD_DNS_URL]` (a function of no host name, so `host.a.b.example.com`, `host` and `""` cannot change it: no devolved candidate, no DHCP), and `InertPacHost.wpad_candidates(_)` is empty; on Windows no candidate is derived (FR-092: the real Windows host overrides no `wpad_candidates`). `wpad_discovery_walk`: the `FakePacHost` candidate list carries loopback fixture URLs; recorded request order on the servers (WPAD candidates first, then `pac_url`, FR-069 (2)); the `wait` passed to `wpad_candidates` is ≤ `PAC_PHASE_BUDGET`. `pac_fallback_to_static_proxy` uses the `HttpConfig` that `resolve_proxy` returns for an OS static proxy beside a failing PAC source (passed through `HttpConfig::loopback(…)` by struct update with that `proxy` and `pac`), so the PAC is tried first and the static proxy records the fallback `CONNECT`; the Windows twin is `os_pac_failure_cases` |
| SC-036 | Every host: `http::tests::bounded_call_abandons_after_wait`. `cfg(not(windows))`: `pac::tests::script_failure_cases`; `http::tests::pac_body_cap`; `feed::tests::check_survives_pac_failure` | `script_failure_cases`: the spec's five plus `function f(){ f() } f()` (stack, SI-28), `try { while(true){} } catch(e) {}` (the interrupt is uncatchable), a regex `/^(a+)+$/` over 40 `a` and a `b`, and `var a=[]; while(true) a.push('x'.repeat(1e5))` (heap) → each `E_PAC_SCRIPT`, each returning in < `PAC_EVAL_BUDGET` + 1 s, and a fresh `evaluate` of a good script on the same thread afterwards succeeds (process alive, nothing left running: the engine has no thread). `pac_body_cap`: exactly `MAX_PAC_BYTES` (a script padded with a comment) accepted, +1 → `E_PAC_FETCH`. `bounded_call_abandons_after_wait`: `bounded(|| { sleep(2 s); 1 }, 100 ms)` → `None` in < 1 s; `bounded(|| 7, 1 s)` → `Some(7)`. `check_survives_pac_failure`: `404` PAC (an open-phase failure) and endless-loop PAC (a per-hop failure, §4.5.1) → `Fresh`, `error: None`, cache written, exactly one warn each naming the code and the PAC URL without its query (`?k=v` planted: the warn contains the path and not `k=v`); a good PAC adds no warn. **Log escaping (§4.15, challenge round 2):** in `script_failure_cases`, a script whose `FindProxyForURL` throws `"a\nb\rc"` and one returning `"PROXY x\ny; DIRECT"` → every captured `warn!` message (`test_support::take_mine`) contains no raw `\n` or `\r` and does contain the escaped `\\n`; a 300-char multi-byte exception message is cut to 200 `char`s without a panic. The Windows failure rows are SC-041's |
| SC-037 | `feed::tests::model_range_cases` (FR-029, R-7) | in-memory modified copies of `valid/full-v2.json`; **each modified document is first asserted valid: `contract::validate(&doc, Schema::V2)` is empty**. Rows: (1) Alpha's `linux-x86_64` `payload.files` = `1e300` → `parse` `Ok`, that asset absent, every other asset and plugin equal to the unmodified model, exactly one `warn!` `E_MODEL_RANGE /plugins/Alpha/platforms/linux-x86_64/payload/files` (`take_mine`); (2) Alpha's `windows-x86_64` `size` = `2.0` → kept with `size == 2`, no warn; (3) **fall-through:** Charlie (which has `darwin-universal` and no `darwin-aarch64`) gains a `darwin-aarch64` asset copied from its `darwin-universal` one (signature text included) with `min_os` `"11.4294967296"` → that asset dropped, one `warn!` `E_MODEL_RANGE /plugins/Charlie/platforms/darwin-aarch64/min_os`, and `version::resolve(&charlie, Os::Macos, Arch::Aarch64, None)` is `Available { key: DarwinUniversal, .. }` (not `NotForThisMachine`); (4) **all assets dropped:** Alpha with `darwin-aarch64` `min_os` `"11.4294967296"`, `linux-x86_64` `payload.files` `1e300` and `windows-x86_64` URL ending `/CON.exe` → Alpha still in `Feed.plugins` (the entry is kept, not dropped), `platforms` empty, three warns (two `E_MODEL_RANGE`, one `E_MODEL_ASSET_NAME`), and `resolve` is `NotThroughManager` on all four FR-034 pairs; (5) one FR-029(c) asset-name row (Bravo's `windows-x86_64` URL ending `/x.exe:ads` → that asset dropped, one `E_MODEL_ASSET_NAME`); (6) one FR-029(c) v1 row (`valid/v1-only.json` with Bravo's `download_url` set to `"javascript:x"`, validated under `Schema::V1` → Bravo absent, Alpha and Charlie kept, one `E_MODEL_V1_ENTRY /plugins/Bravo/download_url`). No row returns `E_FEED_INVALID`. (7) **`check`:** for rows 1, 3 and 4 a fixture server serves the modified bytes and `feed::check(&ctx, Normal, now)` returns `snapshot.source == Fresh`, `error == None`, and the cache file's `body` is byte-equal to the served document. Exactly one warn per dropped item, none for a kept one. Compliance records the Node validator's verdict (accepted) on the same six documents through `contract.js` `validateFeed` (`contract.js:485`, exported `:653`), §7.4 |
| SC-038 | `reader_dependencies_are_the_ruled_set` (§4.11.1, `spec.md:1293-1304`); compliance `cargo tree -p krate-manager-core -e normal --target x86_64-pc-windows-msvc` (no `rquickjs*`), `cargo tree -p krate-manager-core -e features -i rquickjs --target x86_64-unknown-linux-gnu` and the same for `aarch64-apple-darwin` (features `std` only), `cargo tree -p krate-manager-core -e features -i webpki-roots`, `cargo tree --workspace -d --target all` and `cargo tree --workspace -i core-foundation --target all` (§7.4) | exact sets; `windows-sys` features exactly two; `rquickjs` only in core's `cfg(not(windows))` table; one `core-foundation`, one `rustls`, read from the `--target all` output (a plain `cargo tree -d` on this Windows host resolves only the host target, where neither the macOS-only `core-foundation` nor `rquickjs` appears); `cargo tree` resolves a foreign target without building it, so all of these run on this box |
| SC-039 | `sysproxy::tests::linux_desktop_choice_cases` (SI-36; pure, every leg); `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` (§4.11.1) | `linux_uses_kde`: `Some("KDE")`, `Some("X-Cinnamon:KDE")`, `Some("KDE:plasma")` → `true`; `Some("GNOME")`, `Some("ubuntu:GNOME")`, `Some("XFCE")`, `Some("KDEPlasma")`, `Some("kde")`, `Some("")`, `None` → `false` (`KDEPlasma` and `kde` catch a substring or case-folding implementation). The guard: zero findings on the real files; the planted `std::env::var("XDG_CURRENT_DESKTOP")` in an in-memory `os_proxy.rs` is reported, as are the other two plants of §4.11.1 |
| SC-040 | `sysproxy_cf::tests::from_cf_proxies_cases` (`#[cfg(target_os = "macos")]`, `cargo.macos`); `core_dependencies_are_the_fr007_set` (every leg) | dictionaries from `CFDictionary::from_CFType_pairs`: `HTTPSEnable` = `CFNumber 1` → `Int(1)`; `ProxyAutoConfigEnable` = `CFBoolean::true_value()` → `Bool(true)` and `ProxyAutoDiscoveryEnable` = `CFBoolean::false_value()` → `Bool(false)` (core-foundation 0.10.1 `src/boolean.rs:28,32`); `HTTPSProxy` and `ProxyAutoConfigURLString` = `CFString` → `Text`; `HTTPSPort` = `CFNumber 3128` → `Int(3128)`; `ExceptionsList` = `CFArray<CFString>` → `List`; skipped, no panic: a `CFData::from_buffer(b"x")` value (`src/data.rs:30`), a `CFArray` holding a `CFNumber`; a missing key is simply absent. `from_macos` of the converted pairs gives the static proxy, the bypass list, `pac_url` and `auto_detect: false`. The guard: macOS target table exactly `core-foundation = { workspace = true }`, and `core_foundation` named in code only in `sysproxy_cf.rs` / `sysproxy_cf_tests.rs` (planted `use core_foundation::string::CFString;` in `sysproxy.rs` reported) |
| SC-041 | Every host: `http::tests::os_pac_walk`, `http::tests::os_pac_failure_cases`, `pac::tests::winhttp_answer_cases` (FR-092, R-10, R-15, R-16) | `FakePacHost` in OS mode (`os_evaluates` true, a scripted `os_find_proxy` queue, a call count; no WinHTTP call, FR-087), loopback fixture servers, `HttpConfig::loopback(1 s, 2 s).with_pac(Some(PacSource { auto_detect: true, url: Some(P.url("/p.pac?token=s")) })).with_pac_host(Some(fake))` (a query is planted on the OS path too, review round 3: FR-056 `spec.md:703`, FR-068 `spec.md:619-620`). `os_pac_walk`: the injected entries `[Http 127.0.0.1:<B1>, Http 127.0.0.1:<B2>, Direct]` behave exactly as SC-034's `pac_list_walk` (B1 answering `ConnectRefused(407)` → one `CONNECT` there, then B2 serves; both refusing → `DIRECT` reaches C; all failing → `E_FEED_OFFLINE` and `feed::check` returns the seeded cache as `Cached`; no entry tried twice), **including SC-034's non-advancing rows (a) `ConnectOk` + plain-HTTP tunnel to an `https` target → `E_FEED_TLS`, (b) tunnelled `Status(503)` → `E_FEED_HTTP_STATUS { status: 503 }`, (c) tunnelled `Stall(30 s)` → `E_FEED_TIMEOUT`, each with B2 and C recording zero requests** (FR-067's "ends the walk" for the OS evaluator, FR-092); `[Direct]` with `proxy: Some(B)` reaches C directly and B records nothing; a redirect C → C2 makes exactly two `os_find_proxy` calls (once per hop) with the hops' URLs (plain `http` hops, passed whole); **for an `https` hop** (target `https://127.0.0.1:<C>/dir/versions.json?sig=s`, injected `[Direct]`) the URL the fake receives is exactly `https://127.0.0.1:<C>/` (FR-064's reduction on the OS path, review round 3; the hop then ends `E_FEED_TLS` against the plain fixture); P records **zero** requests in every row (the manager fetches no PAC on the OS path), and no captured log line of any row contains `token=s`. `os_pac_failure_cases`: injected `PacError`s of each code (`E_PAC_FETCH` "WinHTTP error 12180", `E_PAC_SCRIPT` "WinHTTP error 12166", SI-40) and an unusable answer (`[]`, and `[Other(3) ftp:21]`) → exactly one `warn!` naming the code (`E_PAC_RESULT` for the two unusable answers) **that contains `/p.pac` and contains neither `token=s` nor `?`** (the PAC URL with its query removed), the request through the static proxy when `proxy: Some(B)`, else direct, and `check` returns `Fresh` with `error: None`; a blocking call (the fake waits on a channel the test holds) → `get` returns within the total timeout + 1 s, the warn is `E_PAC_FETCH` "no answer from the OS PAC evaluator" (with the same `/p.pac`-without-query rule), the `wait` the fake received is ≤ `PAC_PHASE_BUDGET` and ≤ the total timeout, and the test then drops its sender so the abandoned thread ends on its own (never killed). `winhttp_answer_cases` (pure, `os_answer` → `parse_answer`): SI-38's rows (`[Direct]` → [direct]; `[Http a:8080, Http b:3128]` → [http a:8080, http b:3128]; `[Http a:8080, Direct]` → [http a:8080, direct]; `[Https p:3128]` → https; `[Socks s:1080]` → socks4 s:1080; `[Http a:0]` → http a:80; `[Http ::1:3128]` → http `[::1]:3128`; `Other(3)`, an empty host and the host `a;b` skipped with one warn each; `[]` → `E_PAC_RESULT`; 20 `Direct` → 16 kept, one warn) | 
| SC-042 | `cfg(not(windows))`: `pac::tests::dns_bound_cases` (FR-094, R-12) | over a `FakePacHost` whose `resolve` can block (no real DNS), through `DnsMemo` and, for the last row, the engine: a lookup that never answers is "unresolvable" after `PAC_DNS_WAIT` (the call returns within `PAC_DNS_WAIT` + 1 s), its worker is not killed and the next distinct name still resolves; the 17th distinct name is unresolvable with no `resolve` call and exactly one `warn!`; the same name twice → one call, a failed answer included; an IP literal → no call; every `wait` received ≤ `PAC_DNS_WAIT` and ≤ the remaining budget; a script calling `dnsResolve` on 20 distinct hanging names returns within `PAC_EVAL_BUDGET` + 1 s with the process alive |
| SC-043 | Every host: `pac::tests::pac_clock_cases`, `sysproxy::tests::parse_utc_offset_cases`, `sysproxy::tests::offset_range_cases` (FR-095, R-13) | `pac_clock_cases` over `clock_civil` and the time helpers: the spec's rows (`spec.md:1340-1344`: 2026-10-31 23:30:00 UTC, a Saturday, offset +2 h; without `"GMT"` `weekdayRange("SUN")` and `timeRange(1, 2)` true; with `"GMT"` `weekdayRange("SAT")` true, `weekdayRange("SUN")` and `timeRange(1, 2)` false; `local_offset_secs: None` → UTC fields, two calls with one `warned` flag → exactly one `warn!`), plus `helpers::civil` at 0, 951_782_400 (2000-02-29), 4_102_444_800 (2100-01-01) and a negative value. `parse_utc_offset_cases`: `+0130` → 5400, `-0800` → −28800, `+0000` → 0, each also with one trailing LF; `""`, `garbage`, `+2500`, `0100`, `+01:00` → `None`. `offset_range_cases` (`cf_offset_secs`): 3600.4 → 3600, −50 400.0 → −50 400, 50 401.0 → `None`, `f64::NAN` and `f64::INFINITY` → `None`. No Windows reader exists (R-13) |
| SC-044 | compliance record (FR-093) | a text search of `specs/Krate-manager-roadmap.md` after the build: the Phase 12 checklist holds exactly one line naming `rustsec.org`, `rquickjs`, `rquickjs-core`, `rquickjs-sys` and the pin bump (§4.14); `git diff` of the roadmap touches only FR-090's lines; `git diff --numstat specs/krate-manager/probes/results.md` is `1 0` (the one SI-41 row, add-only); recorded in `$EVID/rustsec-line.txt` (§7.4) |

### 7.2 FR → test (FRs without their own SC)

FR-001 `embedded_schemas_compile`, `unsupported_schema_constructs_are_refused`; FR-002 (`if`/`then`/`else`,
`propertyNames` at the object's path, `enum` / `const` by JSON equality with numbers by value, `integer`) and FR-003
(the extra `if` and `propertyNames` findings, exact lists) `keyword_semantics_cases`; FR-020 (`Display`)
`rejected_feed_keeps_last_good_copy`; FR-021/FR-029/FR-037 edges (SI-16, SC-037) `model_range_cases`; FR-022/025/026
`model_of_full_v2` (asset names, typed values), `asset_name_cases`, `feed_model_seams_hold` (no public constructor);
FR-021/FR-027/FR-029(c) v1 (SI-19) `v1_entries_are_sanitized`; FR-028 `model_of_full_v2` (`manager`); FR-036
`native_candidates_*` and `feed_model_seams_hold` (the single call site); FR-041 `production_values`; FR-042 proxy
schemes `proxy_scheme_cases`; FR-043 one agent `ureq_is_confined_to_http_rs`; FR-044 mid-body timeout and corrupt gzip
`status_and_body_errors`; FR-045/FR-081 `ureq_is_confined_to_http_rs`; FR-046 per-hop bypass `no_proxy_bypass`,
conversion refusal `loopback_proxy_only`, redaction `resolve_proxy_cases`; FR-048 upper/lower merge
`proxy_env_from_vars_cases`; FR-049 reader seams (SI-21) the `sysproxy` tests of SC-029 and `from_cf_proxies_cases` (SC-040); FR-074 `linux_desktop_choice_cases` and the env-read guard (SC-039); FR-075 `core_dependencies_are_the_fr007_set` (macOS table, `core_foundation` confinement); FR-052
key order and final LF `check_fresh_writes_cache`; FR-053 `cache_rules`; FR-054 rejection half
`rejected_feed_keeps_last_good_copy`, transport half `offline_returns_cached_flagged_stale` and
`one_attempt_per_check`; FR-056 `check_logs_one_line`, `cache_write_fault_keeps_previous` (one `warn!` per write
fault); FR-057 SC-024; FR-070 (`CONNECT` refusal) `connect_to_remote_host_is_refused`; PD-3 `chain_shares_one_deadline`;
FR-062
`real_src_tauri_config_passes_the_guard` (capabilities unchanged, Phase 4 checks); FR-063 config guard; FR-071 every
fixture test (paths from `CARGO_MANIFEST_DIR`); FR-086 shape test (`--test-threads`) + `binds_loopback_port_zero` +
the FR-086 extension of `production_config_only_in_allowed_tests` (no `static` `Mutex` / `RwLock` in a test source,
`TcpListener::bind` only in `fixture_server.rs`, no non-zero bind port; four plants, §4.11.1); FR-088
`startup_errors_are_logged` and `check_logs_one_line` in one run; FR-090/091 `git diff --stat` at close and
`node tools/gen-specs-index.js --check`. R-1 / R-15: FR-064 script world `script_world_cases`; FR-064 URL argument on both
evaluators `url_argument_cases` (engine) and `os_pac_walk`'s `https` hop row (OS path); FR-064 / FR-068 helper cost
bounds `pac_helper_cases`, `script_world_cases` (caps and the iterative wildcard matcher, §4.15); FR-065 PAC URL policy
and cap `pac_policy_cases` (including the per-hop redirect rows (d), (e) and the `next_pac_hop` rows), `pac_body_cap`;
FR-066 `wpad_discovery_walk`, `wpad_candidate_cases`; FR-067 walk and advance rule `pac_list_walk`, `os_pac_walk`,
`no_response_class_cases`, and FR-067's "any response, any TLS failure, a status, a body failure or the total timeout
ends the walk with that outcome" (`spec.md:607-611`) by the non-advancing rows (a) `E_FEED_TLS`, (b) `503`, (c)
`E_FEED_TIMEOUT` of `pac_list_walk` and `os_pac_walk`, each with B2 and C recording zero requests; answer caps
`find_proxy_result_cases`, `winhttp_answer_cases`; FR-068 bounds `script_failure_cases`, `dns_bound_cases`, `bounded_call_abandons_after_wait`, the
check never fails `check_survives_pac_failure`, `os_pac_failure_cases`; FR-069 order `resolve_proxy_cases`,
`wpad_discovery_walk`, `pac_fallback_to_static_proxy`; FR-065 PAC fetch without `https_only` `agent_trusts_the_os_store`;
FR-068 per-hop warn and log escaping `check_survives_pac_failure`, `script_failure_cases`; FR-092 `os_pac_walk`,
`os_pac_failure_cases`, `winhttp_answer_cases` and the `cargo tree --target x86_64-pc-windows-msvc` record; FR-094
`dns_bound_cases`; FR-095 `pac_clock_cases`, `parse_utc_offset_cases`, `offset_range_cases`, and the once-per-session
read (one `now()` call and one offset `warn!` across two hops) in `script_world_cases`; FR-056 / FR-068 PAC URL without
its query on the OS path `os_pac_failure_cases` and `os_pac_walk` (`?token=s` planted); FR-093 the SC-044 record;
FR-073(d) / FR-081 engine confinement `ureq_is_confined_to_http_rs`, `reader_dependencies_are_the_ruled_set`, and the
Windows build itself (an un-gated `rquickjs` use does not compile there, §4.11.1);
FR-073(b), (c) / FR-075 / FR-081 OS-crate confinement (`windows_sys`, `system_configuration`, `core_foundation` token
rules and their three plants) `ureq_is_confined_to_http_rs` and `core_dependencies_are_the_fr007_set`.

### 7.3 PENDING-CI rows (exact job and assertion)

| Row | Job | Assertion |
|---|---|---|
| Every `cargo.all` test (SC-002 to SC-043 Rust rows that compile on every OS) on macOS and Linux | `manager-ci.yml` `check (macos-latest)`, `check (ubuntu-24.04)` | step `required tests ran (<os>)` `::notice` with the §4.13 list |
| SC-007 native resolution | same legs | `version::tests::native_candidates_macos` (the arm64 runner proves the `aarch64` branch; the `x86_64` branch stays unproven until an Intel host runs it) / `_linux` in the per-OS list |
| SC-013 `SyncDir` fault step | same legs | `feed::tests::cache_write_fault_keeps_previous` |
| SC-016 built-app scan | same legs | step `roster absent (<os>)` `::notice` |
| SC-029 reader seams on their own OS | same legs | step `required tests ran (macos-latest)` lists `sysproxy::tests::bounded_run_abandons_and_reaps`; `required tests ran (ubuntu-24.04)` lists `sysproxy::tests::bounded_run_abandons_and_reaps` (with its `/proc` reap assertion); every leg lists `win_ie_strings_are_read_and_freed_once`, `kioslaverc_path_cases`, `kde_file_read_cases` (the Windows leg is the one that proves the portable-fixture rule of §4.6). The OS call of each reader (WinHTTP, `SCDynamicStoreCopyProxies`, spawning `gsettings`) and its field wiring stay untested by design (they would read the runner's real settings, FR-049); `clippy` and `tauri build` green on each leg prove they compile |
| SC-030 per-hop bypass over IPv6 loopback | same legs (and Windows) | `http::tests::no_proxy_bypass` in `required tests ran (<os>)`; it needs `[::1]` on the runner, which the three hosted images configure (if a leg lacks it, `start_v6` panics and the test fails; it never skips) |
| SC-023 | `feed-tools.yml` `node-test (ubuntu-latest \| macos-latest \| windows-latest)` | the TAP plan line and `# fail 0` |
| SC-033 to SC-036 and SC-042: the in-process engine and the manager's PAC fetch (R-1, R-8, R-15), macOS and Linux only | `check (macos-latest)`, `check (ubuntu-24.04)` | `required tests ran (<os>)` lists the thirteen `cfg(not(windows))` names of §4.13 (`pac::tests::url_argument_cases`, `script_world_cases`, `script_failure_cases`, `dns_bound_cases`; `http::tests::pac_path_proxy`, `pac_path_direct`, `pac_list_walk`, `pac_per_hop`, `pac_fetched_once_never_via_proxy`, `wpad_discovery_walk`, `pac_fallback_to_static_proxy`, `pac_body_cap`; `feed::tests::check_survives_pac_failure`) through `cargo.macos` / `cargo.linux` (SI-39). **These are the only proof of the in-process evaluator:** this Windows box cannot compile it (R-15 removes `rquickjs` from the Windows graph), L-XCHECK cannot cross-build it (no cross C compiler, §1.2, TA-7), and L-WSL (§8) runs it natively on Ubuntu 24.04 as an early local signal that never closes this row (Cross-Cutting: macOS / Linux evidence is CI's). The engine's macOS and Linux build (`rquickjs-sys`'s C compile with the runner's `cc`, the bundled `aarch64-apple-darwin` / `x86_64-unknown-linux-gnu` bindings) is proven by the same legs' `cargo test` and `tauri build` steps; the macOS leg alone proves the Apple target |
| SC-041 OS-path walk and failure rules (FR-092) | every leg (`cargo.all`) | `required tests ran (<os>)` lists `http::tests::os_pac_walk`, `http::tests::os_pac_failure_cases`, `pac::tests::winhttp_answer_cases` (local on this box too; the macOS and Linux legs prove the path is OS-independent, as SC-041 requires "so they run on every host") |
| The real PAC host on its own OS (Windows: `WinHttpGetProxyForUrlEx` and its callback; macOS / Linux: WPAD candidate, local offset, real DNS, `myIpAddress`) | none: untested by design | `OsPacHost` would read the runner's real network, resolver, zone or WinHTTP auto-proxy service (FR-087); only its pure steps (`read_wide`, `take_win_ie`, `os_answer`, `parse_answer`, `parse_utc_offset`, `cf_offset_secs`, `wpad_dns_candidates`, `bounded`) are tested (`sysproxy_cf::system_utc_offset` is named in no test source), and `clippy` on this box plus `clippy` + `tauri build` on each leg prove the per-OS code compiles (the Windows `unsafe` block compiles and is linted here, locally). Recorded as a gap, not a PENDING-CI row (§9) |
| SC-037 model drops on macOS and Linux | `check (macos-latest)`, `check (ubuntu-24.04)` | `feed::tests::model_range_cases` in `required tests ran (<os>)` |
| SC-039 desktop choice and env guard | every leg | `sysproxy::tests::linux_desktop_choice_cases` and `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots` in `required tests ran (<os>)` |
| SC-040 CF conversion | `check (macos-latest)` only | `required tests ran (macos-latest)` lists `sysproxy_cf::tests::from_cf_proxies_cases` (`cargo.macos`, FR-085); not runnable on this Windows host (`cfg(target_os = "macos")`); the `core_foundation` confinement half runs on every leg (`core_dependencies_are_the_fr007_set`) |
| SC-038 dependency set | every leg | `reader_dependencies_are_the_ruled_set` in `cargo.all`; the Windows leg's green `cargo test` with `rquickjs` absent from its graph and the local `cargo tree --target x86_64-pc-windows-msvc` record (§7.4) prove "not linked on Windows"; the macOS leg's `tauri build` is the only proof that the macOS app links `rquickjs-sys` (for whichever slices that build produces) |
| SC-043 PAC clock and offset parsers | every leg (`cargo.all`) | `pac::tests::pac_clock_cases`, `sysproxy::tests::parse_utc_offset_cases`, `sysproxy::tests::offset_range_cases` in `required tests ran (<os>)` (local on this box too) |

### 7.4 Local close procedure (Windows, this box)

From `apps/krate-manager/`, output captured under `specs/krate-manager-phase5/evidence/`:

```bash
git -C ../.. status --porcelain > "$EVID/status-before.txt"
cargo fmt --all --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo clippy -p krate-manager-core --all-targets --features live-network --locked -- -D warnings
cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings
(cd tests/fixtures/clippy-guard && cargo clippy --all-targets -- -D warnings)   # must FAIL naming disallowed_methods (Phase 4)
cargo test --workspace --locked 2>&1 | tee "$EVID/cargo-test.txt"
cargo test -p krate-manager-core --locked -- --list > "$EVID/list-default.txt"
cargo test -p krate-manager-core --features live-network --test live_feed --locked -- --list --ignored > "$EVID/list-live.txt"
cargo tree -p krate-manager-core -e normal > "$EVID/tree-core.txt"
cargo tree -p krate-manager-core -e normal --target x86_64-pc-windows-msvc > "$EVID/tree-core-windows.txt"   # no rquickjs* (SC-038, R-15)
cargo tree -p krate-manager-core -e normal --target x86_64-unknown-linux-gnu > "$EVID/tree-core-linux.txt"   # rquickjs 0.14.0
cargo tree -p krate-manager -e normal --target all > "$EVID/tree-app.txt"
cargo tree -p krate-manager-core -e features -i rquickjs --target x86_64-unknown-linux-gnu > "$EVID/tree-rquickjs-features-linux.txt"   # std only
cargo tree -p krate-manager-core -e features -i rquickjs --target aarch64-apple-darwin > "$EVID/tree-rquickjs-features-macos.txt"      # std only (SC-038)
cargo tree -p krate-manager-core -e features -i webpki-roots > "$EVID/tree-webpki.txt"          # compiled in, unused (SC-038)
cargo tree --workspace -d --target all > "$EVID/tree-dups.txt"                                  # no second rustls / core-foundation
cargo tree --workspace -i core-foundation --target all > "$EVID/tree-core-foundation.txt"       # exactly one version (SC-038)
# SC-037 Node parity: a Node one-off (Write tool + `node <file>` in the scratchpad, never a heredoc) applies the six
# model_range_cases edits to full-v2.json / v1-only.json and prints validateFeed(doc, { schema }) for each, expected
# zero findings, into "$EVID/model-range-node.txt"
npm run typecheck && npm run lint && npm test
node ../../tools/lint-manager-version.js
npm run tauri build 2>&1 | tee "$EVID/tauri-build.txt"
# the roster absent script of §4.12, run with bin=target/release/krate-manager.exe
node --test --test-reporter=tap "../../tools/feed-contract/tests/*.test.js" > "$EVID/node-test.tap"
grep -n 'rustsec.org' ../../specs/Krate-manager-roadmap.md > "$EVID/rustsec-line.txt"   # SC-044: one Phase 12 line
git -C ../.. status --porcelain | grep -v '^?? specs/krate-manager-phase5/evidence/' > "$EVID/status-after.txt"
```

**L-WSL** (the in-process engine on Linux, locally; an early signal, never a PENDING-CI closure). Concurrently with the
block above (WSL is capped at 4 cores by `~/.wslconfig`, so it does not oversubscribe the box), from the repo root:

```bash
wsl -e bash -lc 'cd /mnt/f/projects/iterum/apps/krate-manager && export CARGO_TARGET_DIR=$HOME/km-target && cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings && cargo test -p krate-manager-core --locked' 2>&1 | tee "$EVID/wsl-core.txt"
```

It builds `rquickjs-sys` with WSL's own `gcc` and runs the core suite, the `cfg(not(windows))` engine and PAC-fetch tests
included, on Ubuntu 24.04 (the CI leg's image version, §1.2). The target dir is on WSL's ext4 file system, never the
Windows `target/` (the two toolchains must not share artifacts) and never inside the repo; `--locked` keeps
`Cargo.lock` unchanged. Only `-p krate-manager-core`: WSL has no WebKitGTK, so `src-tauri` does not build there (§4.1).
The tests bind loopback only (`networkingMode=mirrored` shares the host's loopback; every server binds port 0, FR-086)
and the bounded-runner test spawns `sleep` / `printf` only. The lane's verdict is recorded beside the PENDING-CI rows of
§7.3, not instead of them.

`cargo test` already runs every binary's tests in parallel threads; the Node suite runs its files in parallel. The
build and the tests are not timing-sensitive; the only wall-clock bound (SC-011 < 5 s, SC-032 total + 1 s) has a
large margin over the 1 s timeouts; the PAC bounds (SC-036: each failure case < 2 s + 1 s, measured 300 ms for a
300 ms deadline, §1.2; SC-041's blocking row: total + 1 s) are deadlines the code enforces, not machine-speed budgets,
so they are not `[wallclock]`-class cases and run in the parallel pool. On this box the engine rows are absent from the
Windows test binary (R-15); `cargo test -- --list` shows which PAC tests ran here, and L-WSL runs the rest. Nothing here touches the installed plugins, the registry or the network beyond
loopback. `node tools/clean-test-output.js --apply` at close.

---

## 8. Build integration and gate lanes

- **Workspace:** unchanged members; new core modules; two OS-specific dependencies in `src-tauri`; in core a
  non-Windows `rquickjs` and a macOS-only `core-foundation` (§4.1).
- **`package.json`:** unchanged (no script names `live-network`, asserted by the shape test).
- **Workflows changed:** `manager-ci.yml` (path filter, `roster absent`); `feed-tools.yml` only if the TAP plan count
  moves (it should not, §4.12).
- **Lanes:** app lane (§7.4 first block), pipeline lane (`node --test`) and L-WSL (below). All three run concurrently
  on the box: the app lane is cargo-bound, the Node suite takes seconds and L-WSL is capped at WSL's 4 cores. No C++ file is touched (`git diff --name-only` at close must
  show nothing under `plugins/`, `dsp/`, `cmake/`), so no `run-gate.js` / `header-consumers.js` lane.
- **CI cost:** `ring` and `rustls-platform-verifier` add a first-build compile on each leg, and `rquickjs-sys` (four C
  files, 14.5 s for the whole scratch crate on this box, §1.2) on the macOS and Linux legs only (R-15: the Windows leg
  no longer builds it); `Swatinem/rust-cache@v2` (already a step) caches them after the first green run.
- **L-XCHECK:** `rquickjs-sys`'s and `ring`'s build scripts compile C for the target, so the cross check from Windows
  fails in a build script for both targets (measured, §1.2, tasks TA-7): the lane records the skip verbatim. The engine
  half of `pac.rs` is `cfg(not(windows))`, so this box never compiles it natively either.
- **L-WSL (new, local, early signal):** `cargo clippy` + `cargo test -p krate-manager-core` inside WSL Ubuntu 24.04
  (cargo 1.99.1, gcc 13.3.0, measured this session, §1.2), the command of §7.4. It is the only local run of the
  in-process engine and the manager's PAC fetch; it runs beside the Windows app lane (4 WSL cores of 32), in its own
  target dir. A red there is a red to fix before pushing; a green there closes nothing (§7.3 rows stay PENDING-CI).
  Its cost is one cold `ring` + `rquickjs-sys` build per fresh target dir (the dir persists between runs).

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| `ring` fails to build on a leg (C toolchain) | `ring` 0.17 ships pregenerated assembly for x86_64 MSVC; the macOS/Linux legs have `cc`. The first `cargo build` after the manifest edit is the check; a failure is a precondition stop, not a feature change. |
| A TLS-intercepting corporate proxy | OS trust store (D4): its CA is trusted when the OS trusts it; otherwise `E_FEED_TLS` and the cached copy. |
| `rquickjs-sys` fails to build on the macOS or Linux leg (C toolchain, bindings) | Bundled bindings exist for both targets (§1.2), so no libclang; the runners have a C compiler (`ring` needs one too). L-WSL builds it locally on Ubuntu 24.04 before any push (§8); the first CI run is the macOS check. A failure is a precondition stop (R-8 (i) `boa_engine` is the recorded alternative, with its MSRV and memory-limit costs). The Windows leg cannot hit it: the crate is not in its graph. |
| A hostile or hijacked PAC / WPAD answer (an `http` PAC URL, a stranger answering WPAD) | On every OS it can only choose routing: HTTPS against the OS store on every feed hop and Phase 7's signatures keep content safe; the cost is denial of service, host-name metadata to its proxy, and connections to ≤ 16 hosts per hop (§6). Recovery: the cached feed, `HTTPS_PROXY` (which wins over every OS source), or turning auto-detect off. Log forging is closed by `pac::log_text` for both evaluators' text (§4.15). On Windows the R-15 measurement shows auto-detect on by default on a real profile, so this path is live there with no user action. |
| A memory-safety bug in a PAC engine, reached by such a script | **macOS and Linux (QuickJS-ng in process):** not mitigated by the PAC bounds; code execution as the user in the manager process (§6), accepted by R-15; mitigations: exact pin, the RustSec check before every release (FR-093, §4.14). **Windows:** the script runs in the WinHTTP auto-proxy service through `WinHttpGetProxyForUrlEx` (documented always out of process, PD-35), so such a bug is not code in the manager; this holds only under R-16's recommendation; under R-16 (i) / (ii) WinHTTP's documented in-process JScript fallback brings it back into the manager process whenever the service cannot take the call. |
| A PAC script that is slow, loops or allocates | macOS / Linux: engine interrupt at 2 s, 32 MiB heap, 256 KiB stack, then the fallback (PD-27); measured on the engine (§1.2), tested by SC-036. Windows: WinHTTP's own limits run it; the manager waits at most `min(PAC_PHASE_BUDGET, remaining)` per hop, then cancels by closing the resolver and falls back (PD-35, SC-041). |
| A hung DNS resolver, WPAD discovery or WinHTTP auto-proxy service | Bounded waits; an unanswered thread is abandoned, never killed (R-12, PD-29, PD-35). macOS / Linux: up to 16 lookup threads per check may outlive it, each ending when the OS resolver returns. Windows: one helper thread per unanswered hop, which cancels its own call by closing the resolver handle and the session and then ends; the callback context lives until the resolver's `HANDLE_CLOSING` (PD-35), so a late callback never reads freed memory; if Windows never delivers that notification, the context (one `Box` holding a `SyncSender`) stays allocated, a few bytes. The check returns on time in every case. |
| `WINHTTP_OPTION_CONTEXT_VALUE` on a resolver handle is undocumented (the option table lists session and request handles only, §1.2) | Either outcome is memory-safe (PD-35): accepted → the context is freed on the resolver's `HANDLE_CLOSING`; refused → the context is leaked for that call, one small `Box` per hop of a check that runs PAC. Nothing frees it early, and nothing here is tested on a real machine (the OS call is untested by design); the build's reviewer checks the `unsafe` block against the WinHttpCloseHandle remarks. |
| The real PAC host's OS calls are untested | Pure steps tested on every leg (`read_wide`, `os_answer`, `parse_answer`, `parse_utc_offset`, `cf_offset_secs`, `wpad_dns_candidates`, `bounded`); the OS-path control flow in `get` is tested on every leg over a fake host (SC-041). What stays untested is the OS calls themselves: on Windows the `WinHttpGetProxyForUrlEx` sequence and its callback (about 80 lines of `unsafe`, PD-35, compiled and linted locally); on macOS / Linux `CFTimeZone`, `date +%z`, DNS and the UDP local-address probe, which compile on each leg. A failure degrades to `E_PAC_FETCH` and the fallback, "no candidate", UTC or unresolvable with one `warn!`, never an error (§7.3). A wrong scheme or port copy in the Windows entry loop would misroute rather than fail; the mapping table is PD-35's and the reviewer checks it against the `WINHTTP_PROXY_RESULT_ENTRY` page. |
| WinHTTP's one SOCKS scheme hides the SOCKS version | The `WINHTTP_PROXY_RESULT_ENTRY` page names only `WINHTTP_INTERNET_SCHEME_SOCKS`; the plan maps it to FR-067's `SOCKS` → `socks4` (PD-36). A PAC that answers `SOCKS5 h:p` may then be tried as SOCKS4 on Windows; a failed handshake is the no-response or error class of PD-33, so the walk moves on or ends with that outcome, never a silent direct connection. SOCKS PAC answers are rare for a feed fetch; recorded, not mitigated. |
| The WinHTTP auto-proxy service is disabled or unavailable on a Windows machine | `WinHttpGetProxyForUrlEx` "always executes out-of-process", so the call fails (`E_PAC_FETCH`, one `warn!`) and the hop takes FR-069's fallback (static proxy, else direct); the script never runs in the manager. A network that needs its PAC then needs `HTTPS_PROXY` or the OS static proxy; Phase 12's docs name this. |
| The in-process evaluator cannot be tested on this Windows box | R-15 removes it from the Windows build, so its tests run on the macOS and Linux CI legs (PENDING-CI, §7.3) and locally only in L-WSL (Linux, §8). macOS-specific behaviour of the engine is proven by the macOS leg alone. |
| WPAD found nothing where the network relies on DNS devolution | R-9: by design (macOS / Linux use the OS search list only); the user can set the PAC URL in the OS settings or `HTTPS_PROXY`. |
| Binary size | Windows: no QuickJS (R-15); the release `.exe` before and after is recorded at close (§4.1). macOS and Linux: about +1.0 MiB per binary / per macOS slice estimated from the Windows scratch measurement (§1.2), not measured (no readable CI artifact, no WebKitGTK in WSL). |
| OS proxy changed while the app runs | The OS settings are resolved once per launch (§4.10); "restart to apply" noted for Phase 12's docs. The PAC script itself is fetched at every check, so a changed PAC applies at the next check. |
| `gsettings` hangs (dbus) | 2 s bound, child handed to a reaper thread, never killed (PD-17); default "no proxy". |
| Windows revocation checks through CryptoAPI are slow behind a proxy | `timeout_global` / chain deadline bounds the check (30 s); a timeout falls back to the cache. |
| Windows file lock on `feed-cache.json` (antivirus, a second instance) | `persist` failure → `E_FEED_CACHE_IO { step: persist }`, fresh feed still returned, previous file intact (FR-052, FR-057). A running DAW locks nothing here. |
| Offline at launch | `Cached` snapshot flagged stale, or `snapshot: None` with `E_FEED_OFFLINE`; never a panic. |
| A broken live feed | Whole-document rejection (D1) keeps the last good copy; Phase 3's `dry_run` keeps a broken generator out of Pages. |
| WebKitGTK quirks on Linux | No UI change in Phase 5; the Linux leg still builds the app (`tauri build`). |
| Admin prompts | None: Phase 5 elevates nothing. |
| Two `system-configuration` versions after Phase 11 | Recorded in §1.2 for Phase 11 (turn off the updater's `system-proxy` and pass this phase's proxy, or pick R-3 (i)). |
| Ajv-parity drift when the schema changes | The parity tests read `cases.json` and every gen-feed output in place, and `manager-ci.yml` now runs on `tools/feed-contract/**` changes (FR-084); an unsupported new keyword fails `embedded_schemas_compile`. |
| `ureq` reads proxy variables while building its default config | The value is always replaced (SI-11); tests never depend on the runner's environment. |
| A host without IPv6 loopback (`no_proxy_bypass`, `binds_loopback_port_zero` v6 case) | This box binds `[::1]:0` (checked 2026-10-31 with a Node `net` listener); the hosted CI images configure `::1`, which the first CI run confirms (§7.3). A missing `::1` fails the test loudly (no skip); the fix would be a second `127.0.0.x` address, which Windows does not route by default, so it is not the fallback. |
---

## 10. Open questions (for the user)

R-1 to R-10 and R-12 to R-15 (§0.3) are decided (2026-10-31); R-11 is withdrawn. Two precondition rulings are open,
both opened by R-15, and the plan is written to their recommended answers:

- **R-16:** `WinHttpGetProxyForUrlEx` (documented always out of process) instead of the `WinHttpGetProxyForUrl` R-15
  names (documented to fall back to running the script in the calling process; the flag that stops that fallback is
  documented for Windows Server 2003 only).
- **R-17:** `fAutoLogonIfChallenged = FALSE`: no domain credentials are offered to a PAC server.

The spec edits of §0.2 (SI-1 to SI-41; SI-23 and SI-32 withdrawn; SI-24, SI-30, SI-33 and SI-37 already applied by the
spec revision of 2026-10-31) are for the main loop to apply before `tasks.md`. SI-38 depends on R-16, and SI-39,
SI-40 and SI-41 are needed whatever R-16 rules (SI-19's proposed fix now names `PageUrl`, review round 3). `tasks.md` predates this revision and the tasks stage regenerates it. Nothing else
is open.

## Review notes

Challenge round 1 (2026-10-30, 18 issues). Every issue was applied; none was rejected. Five were applied in a form
that differs from the suggested resolution, for the reasons below.

- **FR-048 merge (`ProxyEnv::from_lookup(get: impl Fn(&str) -> Option<String>)`).** Applied as
  `ProxyEnv::from_vars(&ProxyVars)` instead. A lookup closure keyed by name would put `"HTTPS_PROXY"` and the other
  names into a `crates/core/src` file, which FR-063 and the config guard forbid (§4.11.2: the names occur only in
  `proxy_env.rs`). `ProxyVars` has six named fields, so the merge and the empty-as-unset rule are tested in core, and
  the one remaining copy step in `proxy_env.rs` is pinned by six config-guard needles, so a swapped pair still fails
  a test. The test is named `http::tests::proxy_env_from_vars_cases` after the function. `sysproxy::kioslaverc_path`
  is as suggested.
- **SC-029 per-OS reader tests.** The suggestion assumed tests could live next to the readers in `src-tauri`; they
  cannot (`[lib] test = false`, Phase 4 D12a). The testable logic moved to core instead (PD-1, SI-21). The Windows
  string step is tested over test-owned `Vec<u16>` buffers with an injected `free`, not over a `GlobalAlloc`ed
  structure: the `GlobalAlloc` route needs the extra `windows-sys` feature `Win32_System_Memory` (windows-sys 0.61.2
  `Cargo.toml:222`, `src/Windows/Win32/System/Memory/mod.rs:30`) and an OS-only test, while the injected form runs on
  every leg. Production passes `GlobalFree` itself. The macOS conversion needs `core-foundation` in core, which is
  ruling R-6. The Windows test goes in `cargo.all`, not `cargo.windows`, because it runs on every leg.
- **Asset names (PD-21) and model range (PD-8).** Both of the offered outcomes (reject the document, or drop the
  asset) are put to the user as R-7, and the plan recommends dropping the asset. The reason is that the Node validator
  and `dry_run` accept these documents, so a rejection breaks verdict parity and, under D1, every update. The
  reserved-stem rule for asset names goes beyond C3's `TC_FIELD_OK.asset` (`contract.js:295`) and is marked
  client-only.
- **V1 entries (PD-22).** Of the two offered options, the plan takes "drop a v1 key that fails `pluginName`", not
  "display-only". A typed invariant holds for every later phase without anyone having to remember it. FR-027 is
  amended (SI-19) and changes only for v1 documents.
- **Per-hop bypass test.** This needs a second loopback host, so the fixture server gains `start_v6` (`[::1]:0`,
  SI-22). "Never another address" becomes "never a non-loopback address". `::1` binds on this box; that it exists on
  the CI legs is a PENDING-CI row (§7.3).

Revision for the user's rulings (2026-10-31). R-2 to R-7 were decided as recommended and their sections are
unchanged. R-1 (PAC and WPAD in v1 on all three OSes, a bundled engine, the environment winning over OS settings) was
written in: PD-5, PD-12, PD-13, PD-17, PD-19 rewritten; PD-25 to PD-34, §4.5.1 and §4.15 added; §3, §4.1, §4.5, §4.6,
§4.9 to §4.11, §4.13, §5, §6, §7, §8, §9 and §10 updated; the round-0 "PAC/WPAD-only networks" accepted gap is removed
from §9. Two engine facts were measured in a scratch crate rather than taken from documentation: the interrupt and
heap limit (§1.2) and the L-XCHECK failure of `rquickjs-sys`'s build script. The decisions R-1 opens that are the
user's are R-8 to R-14; where the spec, revised the same day, chose differently from this plan (WPAD devolution,
the `PacHost` signature, the PAC host's injection, the thread wording of FR-068, the extra `windows-sys` feature) the
difference is an SI row (SI-24 to SI-35), not a silent override. (That revision also departed from the FR-069 order
on the strength of a ruling text the spec does not hold; challenge round 2 withdrew it, below.)

Challenge round 2 (2026-10-31, 13 issues: 7 major, 6 minor). Every issue was applied; none was rejected. Where a
suggested resolution offered a choice, the plan took the following one, for the reason given.

- **SC-037 / FR-029(c) (v1 entry with a bad `download_url`).** Of "drop the whole entry" and "an SI asking to amend
  FR-029(c)", the plan takes the first: FR-029(c) and SC-037 are the ruled text (R-7, `spec.md:394-399`,
  `:1244-1245`) and the plan has no evidence against them; a v1 entry is never installable through the manager anyway
  (`release: None`, FR-023), so dropping it costs only a roster line for a document the feed no longer serves as v1.
  PD-22, §3, §4.3.2, SI-19 and the `v1_entries_are_sanitized` rows changed; `model_range_cases` became SC-037's own row
  with all four missing assertions and the named fall-through entry (Charlie with an added `darwin-aarch64`).
- **SC-039 test name.** The spec's `platform::tests::linux_desktop_choice_cases` cannot exist (SI-1: `linux_uses_kde`
  is in `core::sysproxy`), so the test is `sysproxy::tests::linux_desktop_choice_cases` and SI-36 asks for the module
  rename and for FR-074's "the file that holds the real reader" to name `proxy_env.rs`. The path half of the old
  combined test became `kioslaverc_path_cases`.
- **SC-040 (`ScValue::Bool`).** Applied in the code (the first option), not by amending the spec: SystemConfiguration's
  flags are normally `CFNumber`, but nothing in the spec or the sources read this session guarantees that, and
  accepting `CFBoolean` costs one variant. The test uses the spec's name `from_cf_proxies_cases`.
- **FR-075 / FR-081 (`core_foundation` in `os_proxy.rs`).** Of "move the read into `sysproxy_cf.rs`" and "an SI
  admitting `os_proxy.rs`", the plan takes the first, so the token confinement stays exactly the spec's; the cost is
  one read-only OS call in core (macOS only, named in no test source), recorded as SI-37 because FR-075 says
  "used only by `from_cf_proxies`".
- **PD-5 order.** The plan follows FR-069 as written (the issue's second option). The quoted "environment, then the
  OS static proxy, then PAC/WPAD" has no source in the spec or the clarifications; SI-23 and R-11 are withdrawn rather
  than kept as a proposed departure, because the plan has no reason to depart from FR-069.
- **PAC engine residual risk (R-15).** Added as an open ruling with the three suggested options, plus one finding the
  suggestion did not state: a same-user child process (option (a)) contains crashes and heap corruption but, without an
  OS sandbox, not code execution as the user, so it is presented with that limit. The plan stays written against (c)
  and recommends (b) if the `fAutoDetect` measurement shows auto-detect on by default. "Run outside the sandbox" is
  removed from §6's "cannot do" list, and R-8 (ii)'s "would have to kill" reason is corrected. The advisory check is
  placed in Phase 12's release checklist (a roadmap sentence, §4.14) rather than in `manager-ci.yml`, so this phase's
  CI shape test is unchanged; the measurement is a read-only probe on a fresh Windows 11 profile, not a CI step,
  because a hosted runner is a Windows Server image.
- **PAC fetch `https_only`.** `agent_config` gained an `AgentPurpose` argument (the issue's first option) instead of a
  bare `bool`, so a call site cannot pass the wrong polarity unnamed; the proof is the pure `https_only()` assertion in
  `agent_trusts_the_os_store`, which FR-087 already admits `HttpConfig::production()` into, so no socket test needs a
  production config.

Revision for the rulings R-8 to R-15 (2026-10-31, part 2). R-8, R-9, R-10, R-12, R-13 and R-14 were decided as
recommended; their rows are marked decided and their sections changed only where R-15 narrows them to macOS and Linux
(R-8, R-12, R-13, R-14) or moves the Windows half into the OS call (R-9). R-15 was written in as ruled. Three findings
came from verifying it against Microsoft's documentation this session (§1.2), and each is a precondition row, not a
silent change:

- **The out-of-process question (R-16).** The task asked whether `WinHttpGetProxyForUrl` evaluates out of process and
  whether to set `WINHTTP_AUTOPROXY_RUN_OUTPROCESS_ONLY`. The documentation answers that it delegates to the WinHTTP Web
  Proxy Auto-Discovery service when it can, but by default falls back to running the script in the calling process
  (COM and JScript loaded in-process), and that the flag disabling the fallback is "available on Windows Server 2003
  only". So the ruled call cannot be relied on to keep the script out of the manager process on the Windows 10 22H2 /
  11 floor. `WinHttpGetProxyForUrlEx` is documented to "always execute out-of-process" and is inside that floor
  (Windows 8+). The plan is written to it (PD-35), and the spec edits are SI-38.
- **The time-out.** `WinHttpSetTimeouts`' page says nothing of autoproxy, so the bound is a wait, not a session
  timeout: core's `http::bounded` around the host call (abandoned, never killed, the PD-29 pattern), and inside the
  host the documented cancel, closing the resolver handle.
- **The answer shape.** The `WINHTTP_PROXY_INFO` list of `WinHttpGetProxyForUrl` loses `DIRECT` failover and SOCKS;
  the Ex entries keep them. Rendering the entries into PAC answer text (PD-36) gives both evaluators the one tested
  `parse_answer` and the one walk, as the ruling asks. R-16 (i) records the `WINHTTP_PROXY_INFO` mapping in case the
  user keeps the ruled call.

R-17 (`fAutoLogonIfChallenged`) is a decision the Windows call forces and the user has not made. The Windows-only
time-zone helper (`win_utc_offset`, `GetTimeZoneInformation`, `Win32_System_Time`), the Windows WPAD call
(`WinHttpDetectAutoProxyConfigUrl`) and the Windows PAC DNS / local-address use were dropped (SI-32 withdrawn). The guards
now ban those names tree-wide. Engine confinement is proven by the Windows build: `rquickjs` is absent from the Windows
graph, so an un-gated use cannot compile. The test names now follow the spec's SC-033 to SC-043, and FR-085's placement
gained SC-034's and SC-035's `cfg(not(windows))` tests (SI-39). The new L-WSL lane is a local Linux run of the engine
tests (WSL Ubuntu 24.04 with cargo and gcc, checked this session). It closes no PENDING-CI row.

Review round 3 (2026-10-31, 12 issues: 4 major, 8 minor). Every issue was applied; none was rejected and no threshold
was relaxed. Four were applied in a form that differs in detail from the suggested resolution:

- **PD-35 context lifetime.** Applied as suggested (`WINHTTP_CALLBACK_FLAG_HANDLES`, the context bound to the resolver
  with `WINHTTP_OPTION_CONTEXT_VALUE`, freed only on its `HANDLE_CLOSING`, the waiting thread only closing handles),
  with two facts found while verifying it: `windows-sys` 0.61.2 does not export `WINHTTP_CALLBACK_FLAG_HANDLES`, so
  `os_proxy.rs` defines it from the two exported status constants exactly as `winhttp.h:1089` does; and Microsoft's
  option table lists `WINHTTP_OPTION_CONTEXT_VALUE` for session and request handles only. Because acceptance on a
  resolver handle is therefore undocumented, the plan combines the issue's two options: bind and free on
  `HANDLE_CLOSING` when the option is accepted, deliberately leak when it is refused, and never free from the waiting
  thread (§1.2, §6, §9). `OS_CANCEL_GRACE` is withdrawn, because no free waits on the cancel's callback any more.
- **Helper caps.** The suggested caps (4096 / 1024 bytes for `shExpMatch`, 253 for host arguments) are taken as
  given. The issue's "throws a TypeError, which becomes `E_PAC_SCRIPT`" holds when the script does not catch it; a
  script that catches it continues under its 2 s budget (the interrupt runs between calls), which still bounds it. The "1 MB string with a
  20-star pattern returns within budget" row became two rows: a cap-sized subject and a cap-sized pattern returning
  within `PAC_EVAL_BUDGET`, and a 1 MB subject that throws at once (with the cap in place a 1 MB string never reaches
  the matcher).
- **OS-path query check.** No `os_pac_walk` row reaches the FR-068 fallback (a walk ends with its own outcome), so the
  planted `?token=s` query is on the shared SC-041 config, and every `os_pac_walk` row asserts that no captured line
  contains it, while `os_pac_failure_cases` asserts the positive form (`/p.pac` present, `token=s` and `?` absent).
- **PAC redirect per hop.** The two suggested fixture rows are kept. A pure `next_pac_hop` row is added, because under
  `LoopbackOnly` `check_url` and `check_pac_url` accept the same loopback `http` URLs, so a fixture row alone cannot
  tell a fetch that uses `check_url` per hop from one that uses `check_pac_url`. Only `HttpsOnly` separates them, and
  under `HttpsOnly` socket tests are banned (FR-087). The pure row is inside `pac_policy_cases`, which FR-087 already
  admits.
