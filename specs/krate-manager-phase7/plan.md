# Implementation Plan: Krate Manager Phase 7 — Download and Verify

**Spec:** `specs/krate-manager-phase7/spec.md` (DRAFT revision 3: review round and the 2026-10-31 clarification session
Q1-Q6 applied, `spec.md:18-19`, `:1226-1238`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 7 (lines 599-630); Core Philosophy "Verify before anything touches
the asset" (69-70); Key decision 4 (174-176); Repository Layout `tools/feed-contract/keys/krate-signing.pub` (239-240);
Phase 1 C3 and the KAT (284-295); Phase 2 signing tooling (356-372); Dependency Graph edges 1→7, 4→7, 5→7, 7→8, 7→10,
7→11 (952-953); Cross-Cutting Constraints (980-1021)
**Status:** DRAFT plan, revision 4, 2026-10-31 (review round 1 applied; the user's precondition rulings Q-A and Q-B of
2026-10-31 applied in place, `spec.md:1240-1250`; review round 2 applied, SI-27 to SI-30; see §10 and Review notes), verified against `feat/krate-manager` at
`2f3c93d7` (`git log --oneline -1`; untracked: `specs/krate-manager-phase7/`).
**Lanes:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm typecheck / lint /
test, the version lint, `tauri build`, the local `cargo tree` record), the **pipeline lane** (`node --test` over
`tools/feed-contract/tests/`, because `manager-ci-shape.test.js` changes) and an optional local **L-WSL** early signal
for the `cfg(unix)` code (§8). No C++ file changes, so there is no `run-gate.js` / `header-consumers.js` lane.

**Build preconditions.** The spec edits of §0.2 (SI-1 to SI-30) land before `tasks.md`. SI-27 is blocking because
FR-014's `max_body = size - requested_from`, fixed before the head, makes D4's `200` fallback end `E_DOWNLOAD_SIZE`, so
SC-007 cannot pass as specified. SI-25 is blocking because
FR-045 / FR-060 as written name 8- and 9-parameter signatures that SC-001's `-D warnings` lane refuses and the Q-B
ruling forbids suppressing. SI-1, SI-2, SI-3, SI-4 and SI-18 are blocking: without them the good KAT case fails `E_VERIFY_SHA256`, an existing guard (`config_guard.rs`,
`injection.rs`, the clippy-guard `--locked` step) goes red on code the spec requires, or SC-009 (e) asks for an outcome
the transport cannot produce (ureq 3.5.0 hides a gzip `Content-Encoding`, SI-18). §10's precondition table: Q-A and
Q-B are **decided** (Q-A as recommended; Q-B against the recommendation, so the download entry points take a
`DownloadEnv`, PD-11, with no lint suppression anywhere); R-1, opened by the Q-B ruling, has a recommendation this
plan is written to. SI-25 and SI-26 carry the Q-B ruling into the spec.

---

## 0. What this plan adds to the spec

The spec fixes requirements, names, codes and test names. This plan fixes the module boundaries and exact signatures,
the algorithms the spec describes in prose (the transport generalisation, the attempt loop with its request budget,
the progress state machine, the prune under the registry lock, the verification pass), the manifest and lockfile edits,
every change an existing guard needs to stay green, the per-SC test design and the local close procedure. Where the
tree or a crate contradicts a spec sentence, the conflict is in §0.2.

### 0.1 Plan decisions

| Id | Decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **The stall bound travels in the purpose: `AgentPurpose::Download { stall: Duration }`**, not as a new `stall` parameter of `agent_config`, `walk` and `run_chain`. `agent_config(config, proxy, purpose)` keeps its three parameters; for `Download` it adds `timeout_per_read(Some(stall))`, `accept("application/octet-stream")`, `accept_encoding("identity")`; `Feed` and `PacFetch` build exactly today's config. | `http_tests.rs:282-303` (`agent_trusts_the_os_store`) calls `agent_config(&config, None, purpose)` with three arguments; a fourth parameter would change that test, against FR-011's "every existing `agent_config` and `walk` test keeps passing unchanged". `AgentPurpose` derives `Clone, Copy, Debug, PartialEq, Eq` (`http.rs:622`) and `Duration` is all of them. | FR-010, FR-011, SC-010 (mechanism; SI-6) |
| PD-2 | **One private error-mapping trait over the hop loop.** `trait ChainError: From<HttpError> { fn from_ureq(e: ureq::Error, phase: Phase) -> Self; }`, implemented by `HttpError` (`map_error`, unchanged) and `StreamError` (`Timeout(PerRead)` → `Stalled`, else `Http(map_error(..))`). `walk<E: ChainError>` maps its call error through it; the redirect loop of `run_chain` becomes `final_response<E: ChainError>` (returns the final response), and `run_chain` = `final_response::<HttpError>` + the unchanged 200-only rule + `read_body`. | FR-010 requires `PerRead` to be classified "before" `map_error` for an error "returned by the call"; today `walk` maps call errors itself (`http.rs:1057-1065`), so the raw error must reach a purpose-specific mapper. The one-agent guard requires the single `new_with_config(agent_config(` site in `walk` (`injection.rs:1560-1595`), so `get_streamed` must reuse `walk`, not copy it. | FR-011, FR-012 (mechanism; SI-6) |
| PD-3 | **`DownloadRequest` carries the `running` OS version** it was resolved with (private field), so `obtain_and`'s prune resolves every feed entry "as FR-001 with the same `running`". | FR-005 names `running`; `obtain_and`'s FR-045 signature has no such parameter. | FR-001, FR-005 (SI-7) |
| PD-4 | **`Expected` gains `format: AssetFormat`.** | `VerifiedArtifact` holds `format` (FR-039) and only `verify_file` builds it (FR-039), from an `Expected` that FR-030 lists without a format. | FR-030 (SI-8) |
| PD-5 | **A failed cached file emits `Restarted { reason: CachedInvalid }`** (a fifth `RestartReason`), after its `Verifying` and before the fresh download's first request. **Decided** (Q-A, 2026-10-31, as recommended, `spec.md:1244-1245`). | FR-046 emits `Started { resumed_from: size }` before the cached file is re-verified; when it fails, the full re-download that follows would hold `received` at `size` with no `Restarted`, which breaks FR-022's "strictly increase in a run without `Restarted`". | FR-020, FR-022, FR-046, SC-029 b (SI-9) |
| PD-6 | **`StagingIo` = `create_dir_all`, `kind_of`, `len_of`, `open_partial`, `rename`, `remove_file`, `remove_dir_all`, `read_dir_entries`; `PartialFile` = `write_all`, `sync`.** `len_of(path) -> io::Result<Option<u64>>` (`fs::symlink_metadata`; `NotFound` → `None`; anything that is not a regular file → `Err(InvalidInput)`) is the one existence / length probe of a file (the cached file of FR-046, the partial of D4); its failures are reported as step `open`. `kind_of(path) -> io::Result<Option<EntryKind>>` (`fs::symlink_metadata(path)`, `NotFound` → `None`, else the `EntryKind` of its `file_type()`, no follow) probes the `<hex>` directory itself before anything under it is touched (PD-19, step `open`). `read_dir_entries(dir) -> io::Result<Vec<StagedEntry>>` with `StagedEntry { name: OsString, kind: EntryKind }`, `EntryKind = Dir \| File \| Other` (from `DirEntry::file_type()`, which does not follow a link, so a symlink is `Other`), tells the prune what an entry really is (PD-19). `PartialFile::len` is dropped: the download tracks `on_disk` from `len_of` plus what it wrote. | FR-050's list has no way to ask whether the cached file or a partial exists without creating one (`open_partial` creates), and no way to tell a directory from a file in `downloads/`. | FR-050 (SI-10) |
| PD-7 | **The request budget.** One `obtain_and` makes at most `policy.attempts` requests, plus exactly one more after D4's `HashMismatchAfterResume`. Every request consumes one unit: a ranged request, a retry, and the immediate full request after a `416` or a `RestartFull` 206 (no backoff, no `Retrying`). `Retrying { attempt }` is the 1-based index of the request that failed; a retry happens only while a unit is left. The hash re-download happens at most once and never after FR-046's failed cached file (so at most two `Verifying` events, FR-021). | Keeps FR-015's "total requests ≤ `attempts` + 1" exact while D4 adds immediate follow-ups; keeps FR-023's "at most `attempts - 1`" `Retrying` events (§4.4.8 proof). | FR-015, FR-021, FR-023 (SI-12) |
| PD-8 | **"Assembled from more than one response" (D4)** = the `.part` held bytes before this call's first request (a previous run's partial, including a complete one) or at least two responses of this call wrote bytes into it since it was last truncated. Only such a file earns the one full re-download on `E_VERIFY_SHA256`. | D4 is silent on a partial left by a previous run; a stale corrupt one must not fail every later call (FR-047 removes it, but the re-download is what the user expects). | D4, FR-047 (wording) |
| PD-9 | **`classify_head` treats a 206 whose `Content-Range` is right but whose `Content-Length` is wrong as `RestartFull`.** | FR-013's table lists `Continue` for a 206 with the exact range and a matching or absent length, and `RestartFull` for "any other range"; this combination is in neither row. | FR-013 (SI-11) |
| PD-10 | **`DownloadError::Verify { target, error }`**, a struct variant, not `Verify(VerifyError)`; every transfer variant carries `target: Box<Target>` (`Target { name, asset, url }`, the feed `AssetUrl`). | FR-016 / D12 / SC-030 require the feed URL in every transfer error's `Display`, verification errors included (SC-030 c). Boxing keeps `DownloadError` under clippy's `result_large_err` default of 128 bytes (three `String`s are 72 bytes, the largest `VerifyError` about 72 more). | FR-052 (SI-13) |
| PD-11 | **The shared inputs travel in one `DownloadEnv<'a>`; no lint is suppressed anywhere** (Q-B, 2026-10-31, decided **against** the revision 2 recommendation, `spec.md:1246-1250`). `#[derive(Clone, Copy)] pub struct DownloadEnv<'a> { pub ctx: &'a AppContext, pub io: &'a dyn StagingIo, pub policy: &'a DownloadPolicy, pub cancel: &'a CancelToken }`, borrowed and passed **by value** (it is four references). `ctx` already carries the staging root (`ctx.roots`), the transport config (`ctx.http`) and the release key (`ctx.signing_key`, FR-041), so none of them is a separate field: one source each, no field that could disagree with the context. Per call stay outside: `feed` and `request` (a batch of one feed obtains several requests), `progress` (a `&mut` closure, exclusive to one download, so it cannot sit in a `Copy` struct), `next` (generic per call) and `host` (the command path only; `FakeHostFacts` keeps `RefCell` state, `test_support.rs:199-235`). Arity: `obtain_and` 8 → 5, `run_command` 7 → 4, `for_command` 9 → 6; `command_request` (5) and `discard` (2) keep their parameters (neither does a transfer; `command_request` takes `env.ctx`); `get_streamed` stays at 7 (`http.rs`, not a download entry point; any later input joins `StreamLimits`). Every private helper also stays ≤ 7: one that needs more takes the env plus a private per-call state struct. `cancel` inside the env is R-1 (§10). Enforcement: clippy's `-D warnings` lane with the default threshold, plus the new guard `too_many_arguments_is_never_suppressed` (§4.10.1), which reports any `allow` / `expect` of `clippy::too_many_arguments` or of a group that holds it, a lint table that lowers it, and a `too-many-arguments-threshold` in a clippy config file. | clippy `too_many_arguments` is in `complexity` (warn by default) and fires when `args > too_many_arguments_threshold`, default 7 (§1.2); with `-D warnings` (`manager-ci.yml`, SC-001) an 8-parameter function is an error. The workspace denies `allow_attributes` (`Cargo.toml:44-46`), so the only suppression left was `#[expect]`, which the ruling forbids; the existing `lint_suppressions` scanner already parses every `allow` / `expect` lint name in the workspace (`injection.rs:1140-1183`), so the guard reuses it. | FR-017, FR-045, FR-060 (SI-25); FR-086, SC-001 (SI-26) |
| PD-12 | **The prune holds the in-flight registry's lock for its whole pass.** | Without it, download A could snapshot the registry, download B (another artifact, absent from A's possibly newer feed) insert itself and create its `.part`, and A then remove that `.part`. Holding one short lock over at most a few `remove` calls closes the race; parallel tests in separate temp roots only serialise their prunes. | FR-005, FR-006 (mechanism) |
| PD-13 | **SHA-256 for tests comes from `verify.rs`:** `#[cfg(test)] pub(crate) fn sha256_of(bytes: &[u8]) -> [u8; 32]`. | FR-082 confines `sha2` to `verify.rs`, while every Phase 7 test feed needs the SHA-256 of a generated asset and SC-026 hashes files on disk. | FR-082, FR-085 (SI-5) |
| PD-14 | **Test contexts use the KAT key unless the test verifies.** `test_support::kat_signing_key()` parses `tools/feed-contract/fixtures/kat/kat.pub` (`include_str!`); the Phase 4-6 context builders and `live_feed.rs` take it; Phase 7 tests that verify use `TestSigner::key()` or the KAT key itself. | FR-041 ("every core test builds its context with a throwaway key or the KAT key"); FR-087 bans `SigningKey :: release` in `test_support.rs`. | FR-041 (SI-17) |
| PD-15 | **Error details are scrubbed of URLs and URL fragments.** Every `detail: String` a `DownloadError` takes from an `HttpError` (`Offline`, `Tls`, `Body`) or a `FeedError` passes `scrub(detail)`, which replaces each whitespace-delimited token containing `://`, `/` or `?` with `<redacted>` (a path-and-query fragment such as `/cdn/a.bin?token=…` carries no `://`; an OS error text such as `connection reset (os error 10054)` holds none of the three). `HttpError::Url { url, .. }` keeps only its `reason`; the URL shown is the target's feed URL. SC-030 (d) drives a body error behind the redirect, and `scrub` has a socket-less row in the same test. | D12: no redirect target in an error; `ureq` error texts are not ours, and `HttpError::Url` holds the refused hop's URL (`http.rs:220-221`, `:277-280`). | FR-016, FR-053 (mechanism; SC-030, SI-22) |
| PD-16 | **No new `Platform` method.** Phase 7's only filesystem root is `Roots::staging_dir()`, already resolved by Phase 4's three `Platform` implementations; the per-OS behaviour it has (Windows share modes and `Zone.Identifier`, macOS quarantine xattrs, Unix permissions) is proven by tests, not abstracted. | `roots.rs:195-200`; spec FR-003, FR-004. | none |
| PD-17 | **A gzip-coded response is accepted as ureq decodes it; the bytes on disk are still checked.** ureq 3.5.0 with its default `gzip` feature (kept by the workspace pin) removes `Content-Encoding` and `Content-Length` from a `Content-Encoding: gzip` response before the caller sees it and hands over the decoded body, and no public API exposes the raw header or turns decoding off per agent (§1.2). So `classify_head` can refuse only the encodings ureq leaves visible (every value other than exactly `gzip`: `br` with no `brotli` feature compiled in, `deflate`, `zstd`, `compress`, `x-gzip`, `GZIP`, an unknown token). A gzip-coded asset reaches `classify_head` as a `200` with no length → `Full`; the guards are then `max_body` (decoded bytes past `size` → `E_DOWNLOAD_SIZE`, nothing past `size` written) and `verify_file` (size, SHA-256, signature): gzip of the signed bytes yields the signed bytes and verifies; gzip of anything else of the same length ends `E_VERIFY_SHA256` with no retry. Disabling ureq's `gzip` feature is rejected: the Feed agent shares the pin, and Phase 5's feed tests decode gzip (`http_tests.rs:1204-1233`, `Reply::Gzip`), which FR-011 keeps unchanged; a second agent with other features is forbidden by the one-agent guard (`injection.rs:1560-1595`). | `ureq-3.5.0/src/run.rs:117-125`; `src/body/mod.rs:111` (`pub(crate) struct ResponseInfo`), `:655-660`, `:684-693`, `:942-953`; `src/middleware.rs:207-210` (a middleware chain ends in `run()`, so a middleware sees the stripped headers too); `Cargo.toml:74-77`; workspace `Cargo.toml:39`; `Cargo.lock` `ureq` entry lists `flate2`, no `brotli-decompressor` | FR-013, FR-014, SC-009 e, External facts (SI-18) |
| PD-18 | **`discard` enters the in-flight registry.** It takes `InFlight::enter(downloads, sha256)` for the artifact before removing `<hex>/` and holds it until the removal returns; `None` (an `obtain_and` of the same hash is running) → `E_DOWNLOAD_BUSY` (`DiscardBusy { name, asset }`, nothing removed). While `discard` holds the key, an `obtain_and` of that hash is `E_DOWNLOAD_BUSY` (FR-006) instead of re-verifying a directory being removed. | FR-049's "an in-flight artifact is unaffected" has no mechanism otherwise: a second `obtain_and` in FR-046's cached path could receive a `VerifiedArtifact` whose path `discard` just removed (Unix), or `discard` would fail against its open handle (Windows). `VerifiedArtifact` carries no feed URL, so `DiscardBusy` holds the name and asset instead of a `Target`. | FR-049, SC-035 f (SI-20) |
| PD-19 | **Nothing in `downloads/` is followed through a link.** The prune keeps a live `<hex>` only when its kind is `Dir` and a live `<hex>.part` only when it is `File`; any other kind (a symlink, a junction, a socket) is removed with `remove_file`, which removes the link itself, and on Windows, where `remove_file` refuses a directory symlink or junction, by `remove_dir_all` after that refusal (which also removes the link itself, never its target). Step 4 of `obtain_and` probes `kind_of(<hex>)` before `len_of(<hex>/<asset>)`: anything but `None` or `Some(Dir)` (a `<hex>` link the prune could not remove) → `E_STAGING_IO`, step `open`, nothing under it measured, opened or removed (§4.4.8). `len_of` refuses a non-regular file; `FsStagingIo::create_dir_all` creates, then requires `symlink_metadata(dir)` to be a directory (a linked `downloads/` → `InvalidInput`, step `create_dir`); `verify_file` step 1 requires `symlink_metadata(path)` to be a regular file before it opens it (else `E_VERIFY_IO { kind: InvalidInput }`). The prune runs before any open, so a planted `<hex>.part` link is gone before `open_partial` could write or truncate through it, and a planted `<hex>` link before `create_dir_all` / `rename` could place the verified file in its target. | Std: `DirEntry::file_type` "will not traverse symlinks" (https://doc.rust-lang.org/std/fs/struct.DirEntry.html#method.file_type); `symlink_metadata` "does not follow" the final link (https://doc.rust-lang.org/std/fs/fn.symlink_metadata.html); `remove_file` on a link removes the link (https://doc.rust-lang.org/std/fs/fn.remove_file.html). Windows: std's `unlink` is `DeleteFileW`, whose `ACCESS_DENIED` fallback opens with `FILE_FLAG_OPEN_REPARSE_POINT` but without `FILE_FLAG_BACKUP_SEMANTICS` and so cannot open a directory; `rmdir` is `RemoveDirectoryW`; `FileType::new` marks a reparse point with the name-surrogate bit (`0x20000000`, which a junction's `IO_REPARSE_TAG_MOUNT_POINT` carries) as a symlink, so `is_dir()` is false for a junction (`library/std/src/sys/fs/windows.rs`, `rust-lang/rust` master, fetched 2026-10-31); `remove_dir_all` "does not follow symbolic links and it will simply remove the symbolic link itself" (std 1.99.1 docs, `fs::remove_dir_all`). `check_sandboxed` is lexical (`roots.rs:173-185`, read this session). | FR-005, FR-050, FR-031 (SI-23, SI-29) |
| PD-20 | **The meter starts where the file is.** `Meter::started(resumed_from)` sets `on_disk = resumed_from` and `high_water = resumed_from`; `restarted` sets `on_disk = 0` (and leaves `high_water`). | FR-022's "`on_disk` is the `.part`'s length after the write" for a partial left by a previous run; a meter starting at 0 would end at `size - resumed_from` and never fire the `on_disk == size` emission. | FR-022 (mechanism) |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before tasks)

| Id | Blocking | Issue (evidence) | Proposed spec edit |
|---|---|---|---|
| SI-1 | **yes** | `tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.sha256` is **not** the archive's SHA-256: it is the C4 listing digest, `db287e49…` (`fixtures/README.md:175`, "`digest(lines)` and a final LF"; `sha256sum` of the `.listing` file is `db287e49…`). The archive's SHA-256 is `5b980e5b8ae7811a064966a1ec1380df016081a12bdeaf643486026007b62890` (`fixtures/kat/README.md` table; `sha256sum` this session). SC-004 says "`sha256` from the `.sha256` file", so the good KAT case would end `E_VERIFY_SHA256`. | SC-004 and FR-085: the expected SHA-256 is the archive's, recorded in `fixtures/kat/README.md` and recomputed in the test with `verify::sha256_of` (PD-13), the two compared; the `.sha256` file is not read (it is Phase 8's payload digest). `message.txt`'s SHA-256 is computed the same way (no README records it). |
| SI-2 | **yes** | `config_guard.rs:831-838` requires `assemble_context(` in `src-tauri/src/lib.rs` to have exactly three top-level arguments, and the plant at `:1704-1712` rewrites the literal text `assemble_context(roots, &feed_url, http)`. FR-041 adds a fourth argument, so the real-config test and the plant both go red. `LIB_RS_STRINGS` (`:46-61`) does not pin `SigningKey::release()`. The spec's Existing-pieces table lists neither. | FR-041 / FR-061: `config_guard.rs` expects four arguments, the plant text becomes `assemble_context(roots, &feed_url, http, SigningKey::release())` → three arguments, `LIB_RS_STRINGS` gains `"SigningKey::release()"` (14 → 15), and a new check pins `generate_handler!` to the six commands (§4.10.2). |
| SI-3 | **yes** | `tests/fixtures/clippy-guard/` has its own `Cargo.lock` (494 packages) and depends on `krate-manager-core` and `krate-manager` by path (`clippy-guard/Cargo.toml:6-8`); `manager-ci.yml:102` runs its clippy with `--locked`. Core's two new normal dependencies make that lock stale (commit `2f3c93d7` records the same red in Phases 5 and 6). | New components: `tests/fixtures/clippy-guard/Cargo.lock`, re-locked with `cargo update --workspace --manifest-path tests/fixtures/clippy-guard/Cargo.toml` (§4.1). |
| SI-4 | **yes** | Three existing test-body rules refuse Phase 7 tests: (a) TA-1 (`injection.rs:341-344`, `:1794-1815`): a core test may start only `sleep`, `printf` or a program listed for its one body, so SC-031's `Command::new("/usr/bin/xattr")` is a `test-program` finding; (b) `BODY_RULES` (`:321-327`): `. with_pac (` only in `PAC_BODIES` (`:304-317`), and SC-010's `streamed_pac_fetch_keeps_the_feed_bound_unix` needs a PAC source; (c) `HttpConfig :: production (` only in `PRODUCTION_BODIES` (`:284-294`), and the `Download` agent's `https_only` under `HttpsOnly` (FR-010) is asserted with the production config. | FR-086 / FR-087: `TEST_PROGRAM_BODIES` gains `(download_tests.rs, verified_file_has_no_quarantine_macos, "/usr/bin/xattr")`, `PAC_BODIES` gains `(http_tests.rs, streamed_pac_fetch_keeps_the_feed_bound_unix)`, `PRODUCTION_BODIES` gains `(http_tests.rs, streamed_policy_cases)` (§4.10.1). |
| SI-5 | no | FR-082 confines `sha2` to `verify.rs`, but every test feed needs a SHA-256 (FR-085) and SC-026 hashes the file on disk. | FR-082: tests hash through `verify::sha256_of`, `#[cfg(test)]` (PD-13). |
| SI-6 | no | FR-011 asks for a `stall` parameter on `run_chain`, `walk` and `agent_config` and, in the same sentence, for every existing `agent_config` test to pass unchanged; `http_tests.rs:282-303` calls the three-parameter form. FR-010 also needs a call-phase `PerRead` before `map_error`, which `walk` applies itself today (`http.rs:1057-1065`). | FR-011, FR-012, SC-010: the bound is `AgentPurpose::Download { stall }` (PD-1); the hop loop is generic over a private `ChainError` (PD-2); SC-010 inspects `agent_config(&cfg, None, AgentPurpose::Download { stall: 30 s })`. |
| SI-7 | no | FR-005 resolves the feed "with the same `running`"; `obtain_and` (FR-045) has no `running`. | FR-001 / FR-005: the request keeps `running` (PD-3). |
| SI-8 | no | `Expected` (FR-030) has no `format`; `VerifiedArtifact` (FR-039) has one. | FR-030: `Expected.format` (PD-4). |
| SI-9 | no | FR-046 + FR-022 contradiction on a failed cached file (PD-5). **Decided** by the user (Q-A, 2026-10-31, as recommended, `spec.md:1244-1245`). | FR-020: `RestartReason::CachedInvalid`; FR-046 and SC-029 b: the failed cached file emits `Restarted { CachedInvalid }` after its `Verifying` and before the fresh download's first request. |
| SI-10 | no | FR-050's `StagingIo` cannot probe existence or tell a directory from a file (PD-6). | FR-050 as PD-6; FR-082's `read_dir` site is `FsStagingIo::read_dir_entries`. |
| SI-11 | no | FR-013's table has no row for a 206 with the exact range and a wrong `Content-Length` (PD-9). | FR-013: `RestartFull`. |
| SI-12 | no | FR-015's bound vs D4's immediate follow-ups; FR-021's "at most two" `Verifying` vs a cached failure followed by a multi-response hash mismatch (PD-7). | FR-015 / FR-023: PD-7's budget rule. |
| SI-13 | no | `DownloadError::Verify(VerifyError)` cannot carry the feed URL SC-030 c requires (PD-10). | FR-052: `Verify { target, error }`; `code()` reports `error.code()`. |
| SI-14 | no | SC-017 and FR-082 name self-tests without names; FR-086's list is therefore incomplete. | FR-086: the names of §4.11 (`no_secret_key_scanner_reports_plants`, `phase7_host_scanner_reports_plants`, `verify_confinement_scanner_reports_plants`, `download_verify_scanner_reports_plants`), plus the three plan tests (`verify::tests::verify_steps_stop_at_first_failure`, `verify::tests::signing_key_text_cases`, `http::tests::streamed_body_cases`). |
| SI-15 | no | SC-031 names `/usr/bin/xattr`; TA-1's table comment says "bare program name resolved through `PATH`" (`injection.rs:339-340`). The rule compares the literal exactly, so the absolute path works and avoids a `PATH` lookup. | FR-086 / SC-031 unchanged; the `TEST_PROGRAM_BODIES` doc comment says "the program literal" (§4.10.1). |
| SI-16 | no | FR-041 implies, but does not list, the mechanical edits to every `AppContext` literal of Phases 4-6 (`detect_tests.rs:100-101`, `:1985`; `feed_tests.rs:248-249`, `:1673`, `:1756`; `http_tests.rs:129-130`, `:1644`, `:1972`; `status_tests.rs:78-79`) and every `assemble_context` / `assemble_with_os` call (`context_tests.rs`, 28 sites; `detect_tests.rs`; `live_feed.rs:48`, feature-gated and never compiled in CI, so it would rot silently). | New components: those files, each gaining only `signing_key: kat_signing_key()` or the fourth argument (PD-14). |
| SI-17 | no | FR-041 says every core test builds its context with a throwaway key or the KAT key but names no helper, and FR-087 bans `SigningKey :: release` in `test_support.rs`. | FR-041: `test_support::kat_signing_key()` (PD-14). |
| SI-18 | **yes** | ureq 3.5.0 strips `Content-Encoding` and `Content-Length` from a gzip response before the caller sees it and decodes the body (`ureq-3.5.0/src/run.rs:117-125`; gzip is a default feature, `Cargo.toml:74-77`, kept by the workspace pin, `apps/krate-manager/Cargo.toml:39`). SC-009 (e) (`Gzip { body }` → `E_DOWNLOAD_ENCODING`, `spec.md:828`) therefore cannot pass, and the External facts row (`spec.md:143`) records only that `content_length()` is `None` after decoding, not that the headers are removed. No public ureq API exposes the raw encoding (`ResponseInfo` is `pub(crate)`, `body/mod.rs:111`) or turns decoding off per agent (PD-17). | External facts row: add the header stripping and `ContentEncoding::from` matching exactly `"gzip"` / `"br"` (`body/mod.rs:942-953`). FR-013: "A `Content-Encoding` that reaches `classify_head` other than absent or `identity` is `E_DOWNLOAD_ENCODING`; a `gzip` coding is decoded by the transport and never reaches it, so its guard is `max_body` and `verify_file` (PD-17)." SC-009 (e) becomes four rows: (e1) `Content-Encoding: br` → 1 request, `E_DOWNLOAD_ENCODING`, 0 bytes written, no `.part`; (e2) `Gzip { gzip(asset) }` → verified, 1 request; (e3) `Gzip { gzip(other bytes of length size) }` → `E_VERIFY_SHA256`, 1 request, no `Retrying`; (e4) `Gzip { gzip(size + 1 bytes) }` → `E_DOWNLOAD_SIZE`, 1 request, no `.part`. SC-008's pure table keeps `gzip` → `E_DOWNLOAD_ENCODING` (the function still refuses it if a transport shows it). |
| SI-19 | no | SC-037's `Paced` case (7 chunks 100 ms apart, `stall 2 s`, `spec.md:1050-1053`) transfers in about 700 ms, shorter than its stall bound, so an implementation that used `stall` as a whole-transfer bound would pass; the spec's "longer than any single stall bound" contradicts its own 2 s. | SC-037 (revised in review round 2 for runner scheduling, §9): `stall = 1 s`, 10 chunks 150 ms apart (the transfer takes ≥ 1.35 s, each gap ≈ 15 % of the bound), `attempt_deadline = 5 s`; assert `Ok`, verified, `elapsed > policy.stall` (and ≥ 1.35 s), at most `policy.attempts` requests. `Reply::Paced` answers every request with the full `200` body (§4.8), so `Ok` means one response delivered all ten chunks in longer than the stall bound, whatever a spurious retry did before it; a `Retrying` is printed, not asserted against. |
| SI-20 | no | FR-049's "an in-flight artifact is unaffected" (`spec.md:549-550`) has no mechanism and no SC row. | FR-049: `discard` enters the registry; a running `obtain_and` of the same hash → `E_DOWNLOAD_BUSY`, nothing removed (PD-18). FR-052 gains `DiscardBusy { name, asset }` (code `E_DOWNLOAD_BUSY`). SC-035 gains (f). |
| SI-21 | no | FR-082 confines `minisign` to `crates/core/Cargo.toml` **`[dev-dependencies]`** (`spec.md:661-663`); `CONFINED_TOML_KEYS` checks a key per file, not per section. | FR-082: the section restriction is enforced by `core_dependencies_are_the_fr007_set` (normal set = the 13 names, which exclude `minisign`; dev set = `{flate2, minisign}`) and by FR-081's walk; the `CONFINED_TOML_KEYS` row confines the key to the two files. FR-081's self-test plant of `minisign` in core's `[dev-dependencies]` (not reported) is explicit (§4.10.2). |
| SI-22 | no | FR-083's guard reads plaintext lines only (`spec.md:672-675`), but the release key is made with `tauri signer generate` (`probes/results.md:27`), whose key files are one base64 line of a standard minisign file (`probes/results.md:10`): no `untrusted comment:` or `secret key` text is visible. The Phase 2 scanner's rule (a), a name ending in `.key` (`tools/feed-contract/tests/c3-trusted-comment.test.js:211-228`), is not copied either. SC-030 has no case whose `detail` comes from behind the redirect, so PD-15's scrub is untested. | FR-083: add (1) a file whose name ends in `.key`; (2) every run of STANDARD base64 of ≥ 40 characters that decodes canonically is checked by the plaintext rules on the decoded text, plus Phase 2 rule (c) (a line ending `secret key` whose next line decodes to `Ed` at bytes 0-1 and `B2` at 4-5); (3) `TAURI_SIGNING_PRIVATE_KEY` followed by `=` or `:` and a non-empty value that does not start `${{ secrets.`. Self-test plants each, built at run time. SC-030 gains (d) and the FR-053 counts and fields (§7.1). |
| SI-23 | no | Nothing in FR-005 / FR-050 / FR-031 refuses a symlink inside `downloads/`, so a planted `<hex>.part` or `<hex>` link would be written, truncated or renamed through, and `verify_file` would open by path through a link. | FR-050: `StagedEntry.kind` (`Dir`/`File`/`Other`), `len_of` refuses a non-regular file, `create_dir_all` refuses a linked directory; FR-005: a live entry is kept only with the right kind; FR-031: step 1 requires a regular file (`E_VERIFY_IO`). FR-086: `download::tests::staged_symlinks_are_not_followed` in the `macos` and `linux` lists (PD-19). |
| SI-24 | no | FR-022's rules are asserted only in SC-005; SC-006 (resume), SC-008 (c) (complete `.part`, no `Bytes`) and SC-034 (c) check other fields only, so a meter that restarted `on_disk` at 0 for a previous run's partial would pass. | SC-002, SC-006 to SC-009, SC-029, SC-034: every recorded run passes `assert_progress_invariants(events, size, outcome)` (§7). |
| SI-25 | **yes** | The Q-B ruling (`spec.md:1246-1250`) forbids `#[expect(clippy::too_many_arguments)]`, but FR-045 still gives `obtain_and` eight parameters (`spec.md:526-528`) and FR-060 gives `run_command` seven and `for_command` nine (`spec.md:590-594`), with the command's nine-argument call (`spec.md:598-600`). Built as written, `for_command` and `obtain_and` fail SC-001's `-D warnings` clippy lane; built to the ruling, the code no longer matches the FRs. FR-017 (`spec.md:409-410`) says `obtain_and` and `for_command` "take a `&CancelToken`" as a parameter. | **FR-045** (new first sentence): "`download::DownloadEnv<'a>` (`Clone, Copy`; `pub` fields `ctx: &'a AppContext`, `io: &'a dyn StagingIo`, `policy: &'a DownloadPolicy`, `cancel: &'a CancelToken`) bundles the inputs every download entry point shares; no public function of `download` takes more than 7 parameters and no lint is suppressed for it (Q-B)." Signature: `obtain_and<T>(env: DownloadEnv<'_>, feed: &Feed, request: &DownloadRequest, progress: &mut dyn FnMut(Progress), next: impl FnOnce(VerifiedArtifact) -> T) -> Result<Obtained<T>, DownloadError>`. **FR-060**: `run_command(env: DownloadEnv<'_>, feed: &Feed, request: &DownloadRequest, progress: &mut dyn FnMut(Progress))`; `for_command(env: DownloadEnv<'_>, name, expected_version, now_unix, host, progress)`; `command_request` unchanged (`for_command` passes `env.ctx`); the command's call becomes `download::for_command(DownloadEnv { ctx: &ctx, io: &FsStagingIo, policy: &policy, cancel: &cancel }, &name, &expected_version, now_unix, &PlatformFacts { io: &OsHostIo }, &mut download::channel_progress(…))` with `policy = DownloadPolicy::production()` and `cancel = CancelToken::new()` bound inside the closure. **FR-017** (R-1): "`get_streamed` takes a `&CancelToken` parameter; `obtain_and`, `run_command` and `for_command` take it as `DownloadEnv::cancel`." **New components**: the `download.rs` row gains `DownloadEnv`; the `test_support.rs` row gains `test_policy()` and `DownloadRig` (§4.7). `discard` (FR-049) is unchanged. |
| SI-26 | no | Nothing in the spec enforces the Q-B ruling past this phase's review: `#[expect]` is permitted by the workspace lints (`Cargo.toml:44-46` deny only `allow_attributes*`), so a later phase could add one silently, and SC-001's threshold (`spec.md:738-739`) names `#[allow` but not `#[expect(`. | **FR-086**: `cargo.all` gains `too_many_arguments_is_never_suppressed` (§4.10.1, §4.11). **SC-001** threshold gains: "no `#[expect(` or `#[allow(` naming `clippy::too_many_arguments` (or `clippy::complexity`, `clippy::all`, `warnings`) anywhere in the workspace, no lint table lowering it and no `too-many-arguments-threshold` in a clippy config file, proven by `too_many_arguments_is_never_suppressed`". |
| SI-27 | **yes** | FR-014 fixes `max_body` as `size - requested_from` (`spec.md:390`) and FR-010 puts it in `StreamLimits`, chosen before the response head is seen (`spec.md:346-349`), but D4 accepts a `200` to a ranged request as the full body written from byte 0 (`spec.md:204-205`). That body is `size` bytes against a limit of `size - from`, so the D4 fallback ends `E_DOWNLOAD_SIZE` with the `.part` removed after `size - from` bytes: SC-007 `ignored_range_falls_back_to_full` (`Truncated(400,000)` then a full `200`) cannot pass as specified. The limit depends on the head verdict. | **FR-010**: `StreamLimits { stall, deadline }` (no `max_body`); `HeadAction::{Read { max_body: u64 }, Stop}`: the limit is returned by `on_head` after it has seen the head, and `get_streamed` ends `Http(TooLarge { max })` once a chunk would take the total past that `max_body`. **FR-013** (`spec.md:382`): `HeadAction::Read { max_body: size - requested_from }` for `Continue`, `HeadAction::Read { max_body: size }` for `Full` (whatever `requested_from` was), `Stop` otherwise. **FR-014**: "`max_body` is `size - requested_from` for a `Continue` verdict and `size` for a `Full` verdict (D4's `200` fallback writes from byte 0)". SC-007 unchanged; `streamed_body_cases` gains the per-head limit rows (§7.1). |
| SI-28 | no | FR-030 makes `verify_file` `pub` with a caller-built `Expected` and a caller-chosen `SigningKey`, and FR-040 makes `SigningKey::from_text` `pub` (`spec.md:440-443`, `:504`). Any crate of the workspace can then mint a `VerifiedArtifact` for any file signed by a key it made, with a binding it chose, so D7's "only a verified file becomes a `VerifiedArtifact`" holds only for the release key by convention. FR-043's guard checks literals, derives, conversion impls and `relocated`, not who reaches `verify_file`. Nothing outside core needs either function: `download.rs` is the only production caller and every test that calls them is in-crate (`verify_tests.rs`, `download_tests.rs`, `test_signer.rs`, `test_support.rs` all compile into core's lib). | **FR-030**: `pub(crate) fn verify_file`; `pub(crate) struct Expected` with `pub(crate)` fields and a `pub(crate) fn from_request` (a feed-derived `DownloadRequest` is the only production source). FR-037's "Phase 11's shared code" reaches it through a core function that takes the key from `AppContext.signing_key`, never from its caller (Phase 11's to add). **FR-040**: `pub(crate) fn from_text`; `SigningKey::release()` and `key_id_hex()` stay `pub` (`src-tauri/src/lib.rs` calls `release()`). **FR-043** gains the call-site rule: `verify_file` and `from_text` declared `pub ( crate ) fn`; `verify_file (` called only in `verify.rs`, `download.rs`, `verify_tests.rs`, `download_tests.rs`; `SigningKey :: from_text (` / `from_text (` only in `verify.rs`, `test_signer.rs`, `test_support.rs`, `verify_tests.rs`, `download_tests.rs`; `struct Expected` holds no `pub` field without `( crate )`; plants: `pub fn verify_file`, a `verify_file(` call and a `SigningKey::from_text(` call in `src-tauri/src/commands.rs`, a bare `pub` field in `Expected`. |
| SI-29 | no | SI-23's link rules stop at the prune for Windows: std's `remove_file` cannot remove a directory symlink or junction there (PD-19 evidence), and step 4 then measured and verified `<hex>/<asset>` through a `<hex>` junction the prune left, because `len_of` / `verify_file` check only the final component. A junction needs no privilege to create (`cmd /C mklink /J`, already used by `detect_tests.rs:2587-2593` `listing_skips_junction_windows`, admitted by TA-1 at `injection.rs:341-344`), so Windows can be tested. | **FR-050**: `StagingIo` gains `kind_of(path) -> io::Result<Option<EntryKind>>` (PD-6); the prune removes an `Other` entry with `remove_file` and, on Windows only, after that fails, with `remove_dir_all`. **FR-046**: the cached-file probe first requires `kind_of(<hex>)` to be `None` or `Some(Dir)`, else `E_STAGING_IO`, step `open`, nothing under `<hex>` measured, opened or removed. **FR-086**: the `windows` list gains `download::tests::staged_links_are_not_followed_windows`; TA-1's `TEST_PROGRAM_BODIES` gains `(download_tests.rs, staged_links_are_not_followed_windows, "cmd")`. |
| SI-30 | no | Three requirements have no detecting test: FR-005's in-flight exception (`spec.md:332-335`; the prune test seeds a static tree, and SC-014's two held artifacts share one feed that offers both), D5's "not retried: a URL or redirect refusal, TLS" at the download level (`spec.md:214-215`; SC-010 asserts only the `http` layer's `E_FEED_*` codes), and FR-060's "`for_command` is `command_request` then `run_command`" (`spec.md:593-594`; SC-028's success row (d) calls the two separately). | **FR-086** `cargo.all` gains `download::tests::prune_spares_in_flight_artifact` and `download::tests::non_retryable_transport_errors` (§4.11). **SC-014** gains the in-flight prune case, **SC-009** the non-retryable transport rows, **SC-028** a `for_command` row that reaches `run_command` (§7.1). |

Not an issue, recorded: SC-013 expects `E_STAGING_IO` for a file held without delete sharing. `DeleteFileW` then fails
with `ERROR_SHARING_VIOLATION`, which std's Windows `decode_error_kind` has no arm for, so its kind is uncategorised
and FR-051 maps it to `E_STAGING_IO` (`library/std/src/sys/io/error/windows.rs`, fetched 2026-10-31 from
`rust-lang/rust` master: the match lists `ERROR_ACCESS_DENIED → PermissionDenied`, `ERROR_DISK_FULL → StorageFull`, …,
and no sharing-violation arm).

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-10-31 at `2f3c93d7`)

| Fact | Where |
|---|---|
| `CONNECT_TIMEOUT` 10 s, `TOTAL_TIMEOUT` 30 s, `MAX_REDIRECTS` 5 | `crates/core/src/http.rs:22-24` |
| `HttpError` (8 variants, code-first `Display`) and `code()` | `http.rs:217-251` |
| `check_url` (`HttpsOnly`: scheme `https` and a host; `LoopbackOnly`: http(s) on `127.0.0.1` / `[::1]`) | `http.rs:276-303` |
| `next_hop` (301/302/303/307/308, `Location` joined and re-checked) | `http.rs:345-382` |
| `AgentPurpose { Feed, #[cfg(not(windows))] PacFetch }`; `agent_config` is the workspace's only `config_builder()` | `http.rs:621-650` |
| `map_error` (every `Timeout` → field-less `HttpError::Timeout`; Call-phase default `Offline`) | `http.rs:664-681` |
| `open_pac(config, pac, deadline)`, `pac_route(config, state, hop, deadline, static_proxy)` | `http.rs:737`, `:891-897` |
| `get` (parse, `check_url`, static proxy, PAC state, `run_chain`) | `http.rs:935-971` |
| `run_chain` (deadline check per hop, `walk`, `next_hop`, final status must be 200, `read_body`) | `http.rs:986-1027` |
| `walk` (fresh `HttpConfig` per entry with `total_timeout: remaining`; the one `new_with_config(agent_config(`; maps call errors itself) | `http.rs:1030-1071` |
| `read_body` (`take(max+1)`, `read_to_end`) | `http.rs:1074-1089` |
| `agent_config` called with three arguments by the Phase 5 test | `http_tests.rs:274-304` |
| One-agent guard: `config_builder` once, in `agent_config`; `new_with_config` calls followed by `(agent_config(` = `[true]` | `crates/core/tests/injection.rs:1560-1595` |
| `CONFINED_TOKENS` (18), `CONFINED_TOML_KEYS` (9), `NEWTYPES`, `CONSTRUCTOR_TRAITS`, `PRODUCTION_BODIES` (9), `PAC_BODIES` (12), `BODY_RULES`, `TEST_PROGRAMS`, `TEST_PROGRAM_BODIES` (2) | `injection.rs:223-248`, `:252-268`, `:279-281`, `:284-294`, `:304-317`, `:320-327`, `:337-344` |
| `FR007_DEPENDENCIES` (11) and the dev-dependency assertion `{flate2}` | `injection.rs:101-113`, `:2625-2635` |
| `scan_core_file` (forbidden substrings outside `platform/`, inline `mod tests {` refused, `sandboxed` only in `roots.rs`); `is_scanned` skips `*_tests.rs` and `test_support.rs` | `injection.rs:1091-1140`, `:1205-1207` |
| `is_test_source` = `crates/*/src/**/*_tests.rs` or `crates/*/tests/*.rs` | `injection.rs:1365-1372` |
| The static-lock rule applies to test sources and `fixture_server.rs` only | `injection.rs:1846-1900` |
| `assemble_context(` argument count must be three; plant rewrites `assemble_context(roots, &feed_url, http)` | `crates/core/tests/config_guard.rs:831-838`, `:1704-1712` |
| `LIB_RS_STRINGS` (14) | `config_guard.rs:46-61` |
| Capability permissions exactly five; `build.rs` commands exactly five (squeezed text) | `config_guard.rs:607-626`, `:640-653` |
| "sixth capability permission allow-planted" plant | `config_guard.rs:1542-1550` |
| Roster scan walks `crates/core/src` recursively minus `*_tests.rs` / `test_support.rs`; its `must` list | `crates/core/tests/roster.rs:79-81`, `:101-118`, `:129-147` |
| Identifier scan's explicit file list and `must` list | `roster.rs:245-262`, `:270-289` |
| `Feed`, `PluginEntry`, `StableRelease`, `PlatformAsset`, `AssetFormat`, `PlatformKey` + `as_str` | `crates/core/src/feed.rs:37-119` |
| `AssetUrl::checked` (C1: `https://`, no `/releases/latest/`), `AssetName::from_url` (last segment, pattern, no `.`/`..`, no reserved stem), both `pub(crate)` | `feed.rs:155-214`, `:218-222` |
| `FetchMode`, `FeedSnapshot`, `FeedCheck`, `FeedError` (`Clone, Debug`, no `PartialEq`; holds `Arc<io::Error>`), `check` | `feed.rs:589-694`, `:741-745` |
| `contract::MinisigText`, `parse_minisig_text` (LF/CRLF, 4 lines, canonical base64, 74-byte `ED` blob, 64-byte global), `parse_trusted_comment` (C3), `RuleCode::as_str` | `contract.rs:56-95`, `:166-263` |
| `signature_findings`: every defined key's signature passes `parse_minisig_text` + `parse_trusted_comment`; no binding | `contract.rs:868-894` |
| `include_str!("../../../../../tools/feed-contract/versions.v2.schema.json")` | `contract.rs:11-14` |
| `Availability`, `platform_candidates`, `resolve`, `resolve_native` | `crates/core/src/version.rs:184-240` |
| `OsVersion` is `Copy`, `Eq`, `Ord`, `Display` | `version.rs:115-180` |
| `STAGING_DIR`, `Roots::new` (staging = state/staging), `check_sandboxed` (`pub(crate)`), `staging_dir()` | `crates/core/src/roots.rs:6`, `:80-114`, `:173-185`, `:199-201` |
| `AppContext { roots, settings, feed_url, http }` derives `Clone, Debug, PartialEq`; `assemble_context`, `assemble_with_os`, `reassemble_with_os` (literal at `:162-167`) | `crates/core/src/context.rs:48-56`, `:69-111`, `:142-171` |
| `SettingsIo` / `save_with` (each I/O step behind a trait) | `crates/core/src/settings.rs:270-306` |
| One process per state dir (`try_lock`) | `crates/core/src/state_lock.rs:53-75` |
| `fixture_server`: `Reply` (12 variants), `RecordedRequest`, exact-path routes, `answer(&Reply)`, `ok_head`, `status_response`, `reason` | `crates/core/src/fixture_server.rs:14-57`, `:99-104`, `:288-345`, `:424-466` |
| `test_support`: `fresh_temp_roots`, `install_capture` / `take_mine`, `FakeHostFacts` (`RefCell` calls) | `crates/core/src/test_support.rs:132-136`, `:178-197`, `:199-235` |
| `lib.rs`: `fixture_server` and `test_support` under `cfg(any(test, feature = "test-support"))` | `crates/core/src/lib.rs:7-8`, `:22-23` |
| core `[dependencies]` (11 keys), `[dev-dependencies] flate2`, target sections | `crates/core/Cargo.toml:15-39` |
| Workspace pins; `[workspace.lints.clippy] allow_attributes = "deny"`, `allow_attributes_without_reason = "deny"`; release `panic = "abort"` | `apps/krate-manager/Cargo.toml:13-41`, `:44-46`, `:48-53` |
| `clippy.toml` disallows `Roots::new`, `std::env::temp_dir`, … (tests get them only through `test_support`) | `apps/krate-manager/clippy.toml:1-10` |
| clippy-guard fixture: own `Cargo.lock`, path deps on core and src-tauri | `tests/fixtures/clippy-guard/Cargo.toml:6-8` |
| `commands.rs` pattern: clone `AppContext`, drop the guard, `spawn_blocking`; `E_STATUS_TASK` | `src-tauri/src/commands.rs:21-44` |
| `src-tauri/Cargo.toml`: `[lib] test = false`, `[[bin]] test = false`; dev-dep core `test-support` | `src-tauri/Cargo.toml:10-19`, `:33-34` |
| `lib.rs` assembly (`:56`) and `generate_handler!` (five, `:86-92`); `build.rs` five commands; `main.json` five permissions | `src-tauri/src/lib.rs:56`, `:86-92`; `src-tauri/build.rs:3-9`; `src-tauri/capabilities/main.json:5-11` |
| `manager-ci.yml`: clippy-guard `--locked` step; `cargo test --workspace --locked` teed; `core dependency tree` (`-e normal`); "required tests ran" regex `^test (\S+)(?: - should panic)? \.\.\. ok$`, count must be 1 | `.github/workflows/manager-ci.yml:97-115` (command at `:102`), `:117-120`, `:122-136`, `:153-178` (regex at `:166`) |
| `required-tests.json`: `cargo.all` 165, `windows` 7, `macos` 24, `linux` 22 | `apps/krate-manager/tests/required-tests.json` (counted with node) |
| `EXPECTED_REQUIRED` deep-compared with `required-tests.json` (`util.isDeepStrictEqual`: order matters) | `tools/feed-contract/tests/manager-ci-shape.test.js:337`, `:739-748` |
| `feed-tools.yml` asserts the TAP top-level plan count | `.github/workflows/feed-tools.yml:126-127` |
| Release key: `untrusted comment: minisign public key: 9FEB36F9114273BC`, LF only; decoded key id (bytes 2..10 reversed) `9FEB36F9114273BC`; KAT keys `440FA75168B852F7`, `63861317F7D60239` | `tools/feed-contract/keys/krate-signing.pub`; `fixtures/kat/kat.pub`; `fixtures/phase2/kat-secret/kat.pub` (decoded with node this session) |
| KAT archive 452 bytes, SHA-256 `5b980e5b…b62890`; `.minisig` LF; `.sha256` = listing digest | `fixtures/kat/README.md`; `fixtures/README.md:170-176`; `sha256sum` this session |
| `message.txt` 25 bytes, SHA-256 `e12376ba…d60cce`, trusted comment `krate:1:KatPlugin:1.2.3:linux-x86_64:message.txt` | `fixtures/phase2/kat-secret/` (`sha256sum`, `cat` this session) |
| Fixtures are byte-exact: `tools/feed-contract/fixtures/** -text` | `.gitattributes:26`; `git check-attr` this session (`text: unset`) |
| C1 v2 shapes the test-feed builder must satisfy: `tagStable` `^[a-z0-9]+/v<core>$`, `pubDate`, `signature` four LF lines ending in LF, `platform` (`size` 1..2^31-1, `payload` required iff `tar.gz`), per-prefix `format` and `min_os`, `install` conditionals (darwin: `au_component` + `macos_pkg_id`; windows: `windows_uninstall_key` `^\{GUID\}_is1$`) | `tools/feed-contract/versions.v2.schema.json` `$defs` (read with node this session) |
| CONTRACT.md: verifier checks the signature first, then `checkBinding`; `E_TC_BINDING` names the first differing field; JSON fixture signatures are zero-filled | `tools/feed-contract/CONTRACT.md:219-226`, `:439`, `:508-522` |
| Q-B inputs (read 2026-10-31 for revision 3): `[workspace.lints.clippy]` holds only `allow_attributes = "deny"`, `allow_attributes_without_reason = "deny"`; `crates/core`, `crates/helper`, `src-tauri` each declare `[lints] workspace = true`; the clippy-guard fixture manifest declares no `[lints]` | `apps/krate-manager/Cargo.toml:44-46`; `crates/core/Cargo.toml:42-43`; `crates/helper/Cargo.toml:13-14`; `src-tauri/Cargo.toml:47-48` |
| The only clippy config file is `apps/krate-manager/clippy.toml` (10 lines, `disallowed-methods` only, no `too-many-arguments-threshold`); no `#[expect` / `#[allow` of `too_many_arguments` exists in the workspace today (grep over `*.rs`, `*.toml`, `target/` excluded) | `apps/krate-manager/clippy.toml:1-10`; grep this session |
| `lint_suppressions(text)` blanks comments and literals, then reads every `allow(…)` / `expect(…)` lint name of every attribute (also inside `cfg_attr`, inner `#![…]`, whitespace removed); `GROUP_SUPPRESSIONS = ["clippy::style", "clippy::all", "warnings"]`; `workspace_rs_files()` = `crates/*/{src,tests}/**/*.rs` + `src-tauri/src/**/*.rs`; the D6 test adds the clippy-guard fixture's `src/` and plants in an in-memory `context.rs`; `section_keys(toml, header)` | `crates/core/tests/injection.rs:96-98`, `:1140-1183`, `:1289`, `:1315-1332`, `:2558-2622` |
| `run_chain` already has 7 parameters (the clippy limit, not over it); `walk` 6 | `http.rs:986-994`, `:1030-1037` |
| `HostFacts` (`os_version`, `inno_values`, `pkg_outputs`); `FakeHostFacts` records calls in `RefCell`s | `crates/core/src/detect.rs:181-184`; `test_support.rs:199-235` |
| `AppContext` is shared across threads in existing tests (`thread::scope`, `feed_tests.rs:1489-1497`); its `HttpConfig.pac_host` is `PacHostHandle(Arc<dyn PacHost>)` with `PacHost: Send + Sync` | `context.rs:50-56`; `http.rs:35-42`; `pac.rs:70`, `:110` |
| No `DownloadEnv`, `DownloadRig`, `test_policy` or `too_many_arguments_is_never_suppressed` exists in `apps/` or `tools/` (ODR check) | grep this session |

### 1.2 External facts (verified 2026-10-31)

| Fact | Source |
|---|---|
| `minisign-verify` 0.3.0 is the newest (2026-09-25; 0.2.5 2026-03-03), MIT, edition 2018, **no dependencies**. `Error` derives only `Debug` (variants `InvalidEncoding, InvalidSignature, IoError, UnexpectedAlgorithm, UnexpectedKeyId, UnsupportedAlgorithm, UnsupportedLegacyMode`). `PublicKey` derives `Clone, Debug, Eq, PartialEq`, fields private (no key-id accessor). `Signature` derives `Clone`. `Signature::decode(&str)` (`str::lines`, 74-byte blob, 64-byte global, `trusted comment: ` prefix, `ED` → prehashed, `Ed` → legacy, else `UnsupportedAlgorithm`); `trusted_comment(&self) -> &str` (`[17..]`); `PublicKey::decode(&str)` (two lines), `from_base64` (42 bytes, `Ed`/`ED`); `verify_stream(&'a self, &'a Signature) -> Result<StreamVerifier<'a>, Error>` refuses `UnexpectedKeyId` then `UnsupportedLegacyMode`; `StreamVerifier::update(&mut self, &[u8])`; `finalize(&mut self) -> Result<(), Error>` (Blake2b-512, Ed25519 over the hash, then over `signature ‖ trusted_comment`). | crates.io API `https://crates.io/api/v1/crates/minisign-verify`; source `minisign-verify-0.3.0/src/lib.rs:116-132`, `:189-208`, `:220-283`, `:287-412` (downloaded from `https://crates.io/api/v1/crates/minisign-verify/0.3.0/download` this session); https://docs.rs/minisign-verify/0.3.0 |
| `tauri-plugin-updater` 2.14.1 depends on `minisign-verify = "0.3"`, calls `public_key.verify(data, &signature, true)` and reads `signature.trusted_comment()` only after ("Only now is the trusted comment usable") | source `tauri-plugin-updater-2.14.1/Cargo.toml:100-101`, `src/updater.rs:1907-1913` |
| `minisign` 0.10.0 is the newest (2026-09-25), MIT. `sign<R: Read>(pk: Option<&PublicKey>, sk: &SecretKey, data_reader: R, trusted_comment: Option<&str>, untrusted_comment: Option<&str>) -> Result<SignatureBox>` (prehashed `ED`; `None` comment → `timestamp:<now>`); `KeyPair::generate_unencrypted_keypair() -> Result<KeyPair>` with `pub pk`, `pub sk` (in memory); `PublicKey::keynum(&self) -> &[u8]`, `to_base64(&self) -> String`; `SignatureBox::to_string()` writes four lines with `writeln!` (LF, final LF). Dependencies: `ct-codecs ^1.1.7`, `scrypt ^0.11.0` (no default features), `getrandom ^0.4`, `rpassword ^7.5.4` (`cfg(any(windows, unix))`). `getrandom` 0.4.3 MSRV 1.85. | crates.io API; source `minisign-0.10.0/Cargo.toml`, `src/lib.rs:69-120`, `src/keypair.rs:29-45`, `src/public_key.rs:82`, `:195`, `src/signature_box.rs:133-161`, `src/constants.rs:2`; https://docs.rs/minisign/0.10.0/minisign/fn.sign.html |
| `sha2` 0.11.1 is the newest (2026-10-12), `MIT OR Apache-2.0`, `rust-version = "1.88"`; deps `cfg-if ^1`, `digest ^0.11`, `cpufeatures ^0.3` (x86 / x86_64 / aarch64); `use sha2::{Sha256, Digest}`, `Sha256::new()`, `update`, `finalize()`. `digest` 0.11's output is a `hybrid_array::Array`, which converts into `[u8; N]` (`impl<T, U, const N: usize> From<Array<T, U>> for [T; N]`) and derefs to a slice. | crates.io API; source `sha2-0.11.1/Cargo.toml`, `README.md` ("Incremental API"); `hybrid-array-0.4.15/src/lib.rs:718`, `:757` (downloaded this session); https://docs.rs/sha2/0.11.1 |
| `ureq` 3.5.0 (lockfile): `ConfigBuilder::timeout_per_read(Option<Duration>)` "Max duration for each transport read … covers headers, body, TLS handshakes … The timeout restarts for each read"; `accept_encoding` default advertises gzip/brotli, and decompression "only looks at the `Content-Encoding` response header"; `Config::timeout_per_read(&self)`, `Config::timeouts(&self)`, `https_only(&self)`; `Timeout::{PerRead, PerWrite, Global, PerCall, Resolve, Connect, SendRequest}`; a per-read expiry is `Error::Timeout(Timeout::PerRead)`; `impl From<io::Error> for Error` unwraps a wrapped `ureq::Error`; `Body::as_reader()`; the reader "will error if `Content-Length` is set, but the connection is closed before all bytes are received" | local registry `ureq-3.5.0/src/config.rs:284`, `:389-436`, `:528`, `:640-664`, `:761`, `:780-796`; `src/timings.rs:14-39`, `:558`; `src/error.rs:214-229`; `src/body/mod.rs:201`, `:230-262`; https://docs.rs/ureq/3.5.0 |
| `ureq` 3.5.0 **hides a gzip coding** (read 2026-10-31 for review round 1): `run.rs:117-125` "If the body will be decompressed, strip Content-Encoding and Content-Length from the response headers" → `parts.headers.remove(CONTENT_ENCODING)`, `remove(CONTENT_LENGTH)` when `info.is_decompressing()`; `is_decompressing` is true for `Gzip` under `#[cfg(feature = "gzip")]` and for `Brotli` under `#[cfg(feature = "brotli")]` (`body/mod.rs:684-693`); `ContentEncoding::from` maps exactly `"gzip"` → `Gzip`, `"br"` → `Brotli`, anything else → `Unknown` (case-sensitive, `:942-953`); `ResponseInfo` is `pub(crate)` (`:111`) and `Body`'s public accessors are `mime_type`, `charset`, `content_length` only (`:154-201`); a middleware chain ends in `run()` (`middleware.rs:207-210`), so no hook sees the raw header. Default features `rustls`, `gzip` (`Cargo.toml:74-77`; `brotli` is not default, `:68`); the workspace pin keeps the defaults (`apps/krate-manager/Cargo.toml:39`, only `platform-verifier`, `socks-proxy` added), and `Cargo.lock`'s `ureq` entry lists `flate2` and no `brotli-decompressor`. `accept_encoding` does not affect decoding (`config.rs:651-662`). | local registry `ureq-3.5.0/` as cited; `apps/krate-manager/Cargo.toml:39`; `apps/krate-manager/Cargo.lock` (`ureq` 3.5.0 entry) |
| Tauri 2.12.5: `pub struct Channel<TSend = InvokeResponseBody> { inner: Arc<ChannelInner>, phantom }`; `pub fn send(&self, data: TSend) -> crate::Result<()> where TSend: IpcResponse`; `impl CommandArg for Channel<TSend>`; `impl<T: Serialize> IpcResponse for T` | local registry `tauri-2.12.5/src/ipc/channel.rs:188-191`, `:457-463`, `:466`; `src/ipc/mod.rs:176-181`; https://docs.rs/tauri/2.12.5/tauri/ipc/struct.Channel.html; https://v2.tauri.app/develop/calling-frontend/ (as cited in the spec) |
| Rust std Windows error kinds: no arm for `ERROR_SHARING_VIOLATION` (uncategorised); `ERROR_DISK_FULL` → `StorageFull`, `ERROR_DISK_QUOTA_EXCEEDED` → `QuotaExceeded`, `ERROR_WRITE_PROTECT` → `ReadOnlyFilesystem`, `ERROR_ACCESS_DENIED` → `PermissionDenied` | `https://raw.githubusercontent.com/rust-lang/rust/master/library/std/src/sys/io/error/windows.rs` (fetched this session); https://doc.rust-lang.org/std/io/enum.ErrorKind.html |
| Rust std Windows links (review round 2, SI-29): `fs::remove_file` → `unlink` = `DeleteFileW`, whose `ACCESS_DENIED` fallback reopens with `FILE_FLAG_OPEN_REPARSE_POINT` only (no `FILE_FLAG_BACKUP_SEMANTICS`, so a directory cannot be opened) and calls `posix_delete`; `fs::remove_dir` → `rmdir` = `RemoveDirectoryW`; `FileType::new` sets `is_symlink` for a reparse point whose tag has the name-surrogate bit `0x20000000` (a junction's `IO_REPARSE_TAG_MOUNT_POINT` has it), and `is_dir()` = `!is_symlink && is_directory`; `fs::remove_dir_all` "does not follow symbolic links and it will simply remove the symbolic link itself"; `symlink_dir` "Windows treats symlink creation as a privileged action" (Developer Mode, `SeCreateSymbolicLinkPrivilege` or administrator); `os::windows::fs::junction_point` is nightly-only (#121709), so a stable test makes a junction with `cmd /C mklink /J` | `https://raw.githubusercontent.com/rust-lang/rust/master/library/std/src/sys/fs/windows.rs` (fetched 2026-10-31); local std 1.99.1 docs `std/fs/fn.remove_dir_all.html`, `std/os/windows/fs/fn.symlink_dir.html`, `std/os/windows/fs/fn.junction_point.html`; repo precedent `crates/core/src/detect_tests.rs:2569-2594` (read this session) |
| RFC 9110 range rules (§14.2, §14.4, §15.3.7.1, §15.5.17, §12.5.3) | as recorded in the spec's External facts (`spec.md:144`), not re-read here |
| clippy `too_many_arguments` (Q-B): group `complexity` (warn by default), "Checks for functions with too many parameters … Consider grouping some parameters into a new type"; fires when `args > too_many_arguments_threshold`; trait-impl methods and non-Rust-ABI functions are skipped. `too-many-arguments-threshold`: "The maximum number of argument a function or method can have", default `7`. Clippy reads `clippy.toml` or `.clippy.toml` from `CLIPPY_CONF_DIR`, else `CARGO_MANIFEST_DIR`, else the current directory, walking up the parents; lint levels can also be set in a `[lints.clippy]` table. Local toolchain: `clippy 0.1.99 (58cf8f9c35 2026-10-13)`, `rust-toolchain.toml` channel `stable` | source `rust-clippy` master `clippy_lints/src/functions/mod.rs` (`TOO_MANY_ARGUMENTS`, `#[clippy::version = "pre 1.29.0"]`) and `clippy_lints/src/functions/too_many_arguments.rs` (`if args > too_many_arguments_threshold`), fetched 2026-10-31 from `raw.githubusercontent.com/rust-lang/rust-clippy/master/…`; https://doc.rust-lang.org/clippy/lint_configuration.html#too-many-arguments-threshold; https://doc.rust-lang.org/clippy/configuration.html; `cargo clippy --version` this session |

---

## 2. File layout

```
apps/krate-manager/
  Cargo.toml                         🔶 three pins (§4.1)
  Cargo.lock                         🔶 additive re-lock (§4.1)
  crates/core/Cargo.toml             🔶 minisign-verify, sha2 normal; minisign dev (§4.1)
  crates/core/src/lib.rs             🔶 pub mod download; pub mod verify; #[cfg(test)] mod test_signer;
  crates/core/src/http.rs            🔶 CancelToken, AgentPurpose::Download, ChainError, final_response, get_streamed (§4.2)
  crates/core/src/http_tests.rs      🔶 3 new tests; 3 AppContext literals gain signing_key (§4.2.7, PD-14)
  crates/core/src/verify.rs          🆕 RELEASE_KEY_TEXT, SigningKey, Expected, verify_file, VerifiedArtifact, VerifyError (§4.3)
  crates/core/src/verify_tests.rs    🆕 SC-004, SC-016, plan tests (§7.1)
  crates/core/src/download.rs        🆕 DownloadEnv, the pipeline, staging, command functions (§4.4)
  crates/core/src/download_tests.rs  🆕 (§7.1)
  crates/core/src/test_signer.rs     🆕 #[cfg(test)] TestSigner (§4.6)
  crates/core/src/context.rs         🔶 AppContext.signing_key; the extra parameter (§4.5)
  crates/core/src/context_tests.rs   🔶 the extra argument at every assemble call (SI-16)
  crates/core/src/detect_tests.rs, feed_tests.rs, status_tests.rs  🔶 AppContext literals gain signing_key (SI-16)
  crates/core/src/fixture_server.rs  🔶 Reply::{Ranged, Script, Paced}; per-route hit counter (§4.8)
  crates/core/src/fixture_server_tests.rs  🔶 3 new tests
  crates/core/src/test_support.rs    🔶 kat_signing_key, native_key, TestFeed builder, FaultyStagingIo, test_policy, DownloadRig (§4.7)
  crates/core/tests/injection.rs     🔶 tables + 13 new tests, incl. too_many_arguments_is_never_suppressed (§4.10.1)
  crates/core/tests/config_guard.rs  🔶 six commands / permissions, 4-argument assemble, handler pin, signing-crate walk (§4.10.2)
  crates/core/tests/roster.rs        🔶 download.rs, verify.rs in both guards (§4.10.3)
  crates/core/tests/live_feed.rs     🔶 the fourth argument (SI-16)
  src-tauri/src/commands.rs          🔶 download_plugin (§4.9)
  src-tauri/src/lib.rs               🔶 SigningKey::release(); sixth handler
  src-tauri/build.rs                 🔶 sixth command
  src-tauri/capabilities/main.json   🔶 allow-download-plugin
  tests/required-tests.json          🔶 §4.11
  tests/fixtures/clippy-guard/Cargo.lock  🔶 re-lock (SI-3)
tools/feed-contract/tests/manager-ci-shape.test.js  🔶 EXPECTED_REQUIRED only (FR-091)
specs/INDEX.md                       🔶 regenerated (FR-091)
specs/krate-manager-phase7/          🆕 spec, plan, tasks, compliance, evidence/
```

Nothing under `plugins/`, `dsp/`, `cmake/`, `extern/`, `.github/workflows/`, and nothing else under
`tools/feed-contract/` (FR-090). `src/` (TypeScript) is unchanged (FR-063).

---

## 3. Error model

Every `Display` begins with its code (`http.rs:217`). `DownloadError` and `VerifyError` are `Clone, Debug, PartialEq,
Eq, thiserror::Error` with `code() -> &'static str`; tests assert `code()` and the named fields, never message text.
A transfer error's `Display` is `<CODE>: <name> <asset> (<feed AssetUrl>): <what>` (`Target`'s `Display`), with no
redirect URL and every foreign detail scrubbed (PD-15).

| Code | Variant | Raised by | Retried (D5) | `.part` afterwards |
|---|---|---|---|---|
| `E_DOWNLOAD_UNKNOWN_PLUGIN` | `UnknownPlugin { name }` | `request_for` | — | — |
| `E_DOWNLOAD_UNAVAILABLE` | `Unavailable { name, why: Unavailability }` (`NotThroughManager`, `NotForThisMachine`, `NeedsNewerOs { min_os: OsVersion }`) | `request_for` | — | — |
| `E_DOWNLOAD_NO_FEED` | `NoFeed { cause: &'static str, detail: String }` (`cause` = the `FeedError::code()`) | `command_request` | — | — |
| `E_DOWNLOAD_VERSION_CHANGED` | `VersionChanged { name, expected, found }` | `command_request` (FR-065) | — | untouched |
| `E_DOWNLOAD_TASK` | `Task { detail }` | `src-tauri` join error | — | — |
| `E_DOWNLOAD_PATH` | `Path { reason: &'static str }` | `staged_paths`, `discard` | no | nothing written |
| `E_DOWNLOAD_BUSY` | `Busy { target }` | `obtain_and` (FR-006) | no | untouched |
| `E_DOWNLOAD_BUSY` | `DiscardBusy { name, asset }` | `discard` while the same hash is in flight (PD-18) | no | untouched |
| `E_DOWNLOAD_CANCELLED` | `Cancelled { target }` | FR-017 | no | kept |
| `E_DOWNLOAD_ENCODING` | `Encoding { target, encoding }` | `classify_head`, for a coding ureq leaves visible (not `gzip`, PD-17) | no | removed (FR-014) |
| `E_DOWNLOAD_SIZE` | `Size { target, expected, found: Option<u64> }` | `classify_head`, `max_body` | no | removed (FR-014) |
| `E_DOWNLOAD_OFFLINE` | `Offline { target, detail }` | `HttpError::Offline` | yes | kept |
| `E_DOWNLOAD_TIMEOUT` | `Timeout { target }` | `HttpError::Timeout` (deadline, connect, resolve) | yes | kept |
| `E_DOWNLOAD_STALLED` | `Stalled { target }` | `StreamError::Stalled` | yes | kept |
| `E_DOWNLOAD_TLS` | `Tls { target, detail }` | `HttpError::Tls` | no | kept |
| `E_DOWNLOAD_URL` | `Url { target, reason }` | `HttpError::Url` (url dropped) | no | kept |
| `E_DOWNLOAD_REDIRECT` | `Redirect { target, reason }` | `HttpError::Redirect` | no | kept |
| `E_DOWNLOAD_HTTP_STATUS` | `HttpStatus { target, status }` | `HttpError::Status` | 408, 429, 500, 502, 503, 504 | kept |
| `E_DOWNLOAD_BODY` | `Body { target, detail }` | `HttpError::Body`, a close-delimited body shorter than `size` | yes | kept |
| `E_STAGING_FULL` / `E_STAGING_DENIED` / `E_STAGING_IO` | `Staging { target, code: StagingCode, step: StagingStep, kind: io::ErrorKind }` | every `StagingIo` step (FR-051) | no | removed after `write` / `sync` / `truncate`; kept after `rename` and `create_dir` of `<hex>/` |
| `E_VERIFY_*`, `E_TC_BINDING` | `Verify { target, error: VerifyError }` | FR-030 | only D4's one re-download | removed (FR-047) |

`StagingStep::as_str`: `create_dir`, `open`, `write`, `sync`, `truncate`, `rename`, `remove` (FR-051). `StagingCode`
from the kind: `StorageFull | QuotaExceeded` → `Full`, `PermissionDenied | ReadOnlyFilesystem` → `Denied`, else `Io`.

`VerifyError` (`verify.rs`):

| Code | Variant | Step (D6) |
|---|---|---|
| `E_VERIFY_IO` | `Io { kind: io::ErrorKind }` | 1 (`symlink_metadata` / open; not a regular file → `InvalidInput`, PD-19) / 4 (read) |
| `E_VERIFY_SIZE` | `Size { expected: u64, found: u64 }` | 1, and 4 when the length changed under the read |
| `E_VERIFY_SIGNATURE_TEXT` | `SignatureText { detail: String }` | 2 |
| `E_VERIFY_KEY_ID` | `KeyId { expected: String, found: String }` (16 upper-case hex digits each, little-endian u64, as minisign prints it) | 3 |
| `E_VERIFY_SHA256` | `Sha256 { expected: String, found: String }` (64 lower-case hex digits) | 5 |
| `E_VERIFY_SIGNATURE` | `Signature` | 6 |
| `E_VERIFY_TRUSTED_COMMENT` | `TrustedComment { code: &'static str, detail: String }` (`code` = `RuleCode::as_str()`) | 7 |
| `E_TC_BINDING` | `Binding { field: &'static str, signed: String, expected: String }` | 8 |
| `E_VERIFY_KEY_TEXT` | `KeyText { detail: String }` | `SigningKey::from_text` |

`StreamError` (`http.rs`) is internal plumbing that `download` maps (FR-016); it has no codes of its own:
`Http(HttpError)` (transparent), `Stalled`, `Sink { kind: io::ErrorKind }`, `HeadStopped`, `Cancelled`.

---

## 4. Module design

### 4.1 Manifests and lockfiles (FR-080, FR-081, SI-3)

Root `Cargo.toml` `[workspace.dependencies]`, kept in key order:

```toml
# Phase 7 D1: core's tests only (TestSigner), never a normal dependency (FR-081)
minisign = "0.10.0"
# Phase 7 D1: verify.rs only (FR-082)
minisign-verify = "0.3.0"
# Phase 7 D1: verify.rs only (FR-082)
sha2 = "0.11.1"
```

`crates/core/Cargo.toml`: `[dependencies]` gains `minisign-verify = { workspace = true }` and `sha2 = { workspace =
true }` (13 keys); `[dev-dependencies]` gains `minisign = { workspace = true }`. No features, no target section.

Lock procedure (once, before any gate; every later command uses `--locked`):

1. `cargo update --workspace` from `apps/krate-manager/` (updates only the workspace's own entries and resolves the new
   requirements; no existing package version moves). Record `git diff --stat Cargo.lock`; the diff must only add
   packages (`minisign-verify`, `sha2 0.11.1`, `digest 0.11.x`, `block-buffer`, `crypto-common`, `hybrid-array`,
   `typenum` if new, `cpufeatures 0.3`, and the dev-only `minisign`, `ct-codecs`, `scrypt`, `rpassword` and their
   dependencies) and add dependency names to `krate-manager-core`'s entry.
2. `cargo update --workspace --manifest-path tests/fixtures/clippy-guard/Cargo.toml` (the fixture's own lock; it
   reaches core's normal dependencies through its path dependency, not core's dev-dependencies).
3. `cargo tree -p krate-manager-core -e normal --locked --prefix none --format "{p}"`: `minisign-verify v0.3.0` and
   `sha2 v0.11.1` present, no `minisign`, `rpassword` or `scrypt` (local evidence of FR-081 beside SC-018's test).

### 4.2 `http.rs` changes (FR-010 to FR-012, FR-017; PD-1, PD-2)

#### 4.2.1 Cancellation token

```rust
/// FR-017: set once, read before each request, after each body chunk and between backoff slices.
#[derive(Debug, Default)]
pub struct CancelToken(std::sync::atomic::AtomicBool);

impl CancelToken {
    pub fn new() -> Self;
    pub fn cancel(&self);            // store(true, Ordering::SeqCst)
    pub fn is_cancelled(&self) -> bool; // load(Ordering::SeqCst)
}
```

#### 4.2.2 The purpose

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum AgentPurpose {
    Feed,
    #[cfg(not(windows))]
    PacFetch,
    /// A release asset (Phase 7 D2): per-read bound `stall`, octet-stream, identity encoding.
    Download { stall: Duration },
}
```

`agent_config` (still the one `config_builder()`):

- `https_only(matches!(purpose, AgentPurpose::Feed | AgentPurpose::Download { .. }) && config.policy == UrlPolicy::HttpsOnly)`;
- the shared chain unchanged (`max_redirects(0)`, `http_status_as_error(false)`, `proxy`, `PlatformVerifier`,
  `timeout_connect`, `timeout_global(Some(config.total_timeout))`, `user_agent`);
- then `match purpose { Download { stall } => b.accept("application/octet-stream").accept_encoding("identity")
  .timeout_per_read(Some(stall)), _ => b.accept("application/json") }` and `.build()`.

`hop_rule(AgentPurpose::Download { .. }) = next_hop`.

#### 4.2.3 The hop loop, generic over its error

```rust
/// Private: how a hop-loop caller wants a ureq error.
trait ChainError: From<HttpError> {
    fn from_ureq(e: ureq::Error, phase: Phase) -> Self;
}
impl ChainError for HttpError { fn from_ureq(e: ureq::Error, phase: Phase) -> Self { map_error(e, phase) } }
impl ChainError for StreamError {
    fn from_ureq(e: ureq::Error, phase: Phase) -> Self {
        match e {
            ureq::Error::Timeout(ureq::Timeout::PerRead) => StreamError::Stalled,
            other => StreamError::Http(map_error(other, phase)),
        }
    }
}

fn walk<E: ChainError>(config, hop_url, headers, deadline, purpose, entries) -> Result<Response<Body>, E>;
//   identical body; the two `HttpError::Timeout` / `Offline` returns become `E::from(..)`,
//   the call error `E::from_ureq(e, Phase::Call)`.
fn final_response<E: ChainError>(config, first, headers, deadline, purpose, route: &mut Route<'_>)
    -> Result<Response<Body>, E>;
//   today's run_chain loop up to `Hop::Done`, which returns the response (status check moved out).
fn run_chain(config, first, headers, max_bytes, deadline, purpose, route) -> Result<Vec<u8>, HttpError> {
    let mut response = final_response::<HttpError>(config, first, headers, deadline, purpose, route)?;
    let status = response.status().as_u16();
    if status != 200 { return Err(HttpError::Status { status }); }
    read_body(&mut response, max_bytes)
}
```

`get` and `fetch_direct` call `run_chain` exactly as today, so every Phase 5 test runs the same code path. The preamble
of `get` (parse, `check_url`, static proxy) moves into a private `fn prepare(config, url) -> Result<(url::Url,
StaticProxy<'_>), HttpError>` used by both `get` and `get_streamed`.

#### 4.2.4 `get_streamed`

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StreamLimits { pub stall: Duration, pub deadline: Duration }   // no max_body: it depends on the head (SI-27)
/// The final response's headers as ureq hands them over: for a `Content-Encoding: gzip` response ureq has already
/// removed `Content-Encoding` and `Content-Length` and decodes the body (PD-17), so both are `None` here.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ResponseHead { pub status: u16, pub content_length: Option<u64>,
                          pub content_range: Option<String>, pub content_encoding: Option<String> }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HeadAction { Read { max_body: u64 }, Stop }   // the body limit, chosen after the head is seen (SI-27)
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StreamEnd { pub bytes: u64 }
#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum StreamError {
    #[error(transparent)] Http(#[from] HttpError),
    #[error("no byte arrived within the stall bound")] Stalled,
    #[error("the sink refused a chunk: {kind:?}")] Sink { kind: io::ErrorKind },
    #[error("the caller stopped at the response head")] HeadStopped,
    #[error("cancelled")] Cancelled,
}

pub fn get_streamed(
    config: &HttpConfig, url: &str, headers: &[(&str, &str)], limits: StreamLimits,
    on_head: &mut dyn FnMut(&ResponseHead) -> HeadAction,
    sink: &mut dyn FnMut(&[u8]) -> io::Result<()>,
    cancel: &CancelToken,
) -> Result<StreamEnd, StreamError>;
```

Seven parameters: at clippy's limit, not over it (§1.2), so the Q-B ruling leaves it as it is. `get_streamed` is a
transport function of `http`, not a download entry point, and takes no `DownloadEnv` (`http` must not depend on
`download`). An input added later joins `StreamLimits`, never an eighth parameter.

Order: (1) `cancel` set → `Cancelled`, no request. (2) `start = Instant::now()`; chain deadline `start +
limits.deadline`; PAC bound `start + config.total_timeout.min(limits.deadline)` passed to `open_pac` and `pac_route`
(FR-011). (3) `prepare`, PAC state, route closure as `get`. (4) `final_response::<StreamError>(…,
AgentPurpose::Download { stall: limits.stall }, …)`. (5) Final status not 200 / 206 → `Http(Status)`. (6)
`ResponseHead` from the final response's headers as ureq leaves them (`content-length` parsed as `u64`, the others as
received; after a gzip decode neither `Content-Encoding` nor `Content-Length` is present, PD-17). (7)
`on_head` → `Stop` → `HeadStopped`, response dropped unread; `Read { max_body }` → that `max_body` is this
response's limit (SI-27: the caller knows whether the server honoured its `Range` only now, so a `200` to a ranged
request can be read whole). (8) Body loop over `response.body_mut().as_reader()`
with a 64 KiB heap buffer: `cancel` checked before every read; `Interrupted` retried; a read error goes through
`StreamError::from_ureq(ureq::Error::from(e), Phase::Body)` (so a body `PerRead` is `Stalled`, a body `Global` is
`Http(Timeout)`, an early close is `Http(Body)`); a chunk that would take the total past the `max_body` of step 7 is not
passed on and ends `Http(TooLarge { max: max_body })`; `sink` errors end `Sink { kind }` with nothing more read. (9)
`Ok(StreamEnd { bytes })`.

The pub signatures name no `ureq` or `rustls` type (`injection.rs:1536-1552`); `ChainError` is private.

#### 4.2.5 Unchanged

`HttpError` (no variant, no field), `map_error`, `check_url`, `next_hop`, `read_body`, `get`'s behaviour,
`TOTAL_TIMEOUT` as the feed bound.

### 4.3 `verify.rs` (FR-030 to FR-043; D6, D7)

```rust
pub const RELEASE_KEY_TEXT: &str =
    include_str!("../../../../../tools/feed-contract/keys/krate-signing.pub");
const CHUNK: usize = 64 * 1024;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SigningKey { public: minisign_verify::PublicKey, key_id: [u8; 8] }
impl SigningKey {
    /// `PublicKey::decode`, then line 2 base64-decoded (`base64::engine::general_purpose::STANDARD`) for bytes 2..10.
    /// Crate-private (SI-28): no other crate can turn a key it made into a `SigningKey`.
    pub(crate) fn from_text(text: &str) -> Result<SigningKey, VerifyError>;   // E_VERIFY_KEY_TEXT
    /// Panics on failure: a build input SC-016 proves parses.
    pub fn release() -> SigningKey;
    /// As minisign prints it: `u64::from_le_bytes(key_id)` in `{:016X}` (KAT: `440FA75168B852F7`).
    pub fn key_id_hex(&self) -> String;
}

pub(crate) struct Expected<'a> {                                        // SI-28: crate-private, fields too
    pub(crate) name: &'a str, pub(crate) version: &'a str, pub(crate) platform: PlatformKey,
    pub(crate) asset: &'a AssetName, pub(crate) format: AssetFormat, pub(crate) size: u64,
    pub(crate) sha256: [u8; 32], pub(crate) signature: &'a str,
}
impl<'a> Expected<'a> { pub(crate) fn from_request(request: &'a DownloadRequest) -> Expected<'a>; }

/// ```compile_fail
/// use krate_manager_core::feed::{AssetFormat, AssetName, PlatformKey};
/// use krate_manager_core::verify::VerifiedArtifact;
/// fn forge(asset: &AssetName) -> VerifiedArtifact {
///     VerifiedArtifact {
///         path: std::path::PathBuf::new(), name: String::new(), version: String::new(),
///         platform: PlatformKey::LinuxX86_64, asset: asset.clone(), size: 0,
///         sha256: [0; 32], format: AssetFormat::TarGz,
///     }
/// }
/// ```
/// ```compile_fail
/// let _: krate_manager_core::verify::VerifiedArtifact = Default::default();
/// ```
/// ```no_run
/// use krate_manager_core::feed::{AssetFormat, AssetName, PlatformKey};
/// use krate_manager_core::verify::VerifiedArtifact;
/// fn read(a: &VerifiedArtifact, asset: &AssetName) -> (PlatformKey, AssetFormat, bool) {
///     (a.platform(), a.format(), a.asset() == asset)
/// }
/// ```
#[derive(Clone, Debug)]
pub struct VerifiedArtifact {
    path: PathBuf, name: String, version: String, platform: PlatformKey,
    asset: AssetName, size: u64, sha256: [u8; 32], format: AssetFormat,
}
impl VerifiedArtifact {
    pub fn path(&self) -> &Path; pub fn name(&self) -> &str; pub fn version(&self) -> &str;
    pub fn platform(&self) -> PlatformKey; pub fn asset(&self) -> &AssetName; pub fn size(&self) -> u64;
    pub fn sha256(&self) -> &[u8; 32]; pub fn format(&self) -> AssetFormat;
    pub(crate) fn relocated(self, to: PathBuf) -> VerifiedArtifact;   // called only by download.rs (FR-043)
}

pub(crate) fn verify_file(path: &Path, expected: &Expected<'_>, key: &SigningKey)
    -> Result<VerifiedArtifact, VerifyError>;                          // SI-28: crate-private
pub(crate) fn hex_lower(bytes: &[u8]) -> String;
#[cfg(test)] pub(crate) fn sha256_of(bytes: &[u8]) -> [u8; 32];      // PD-13
```

**Who can mint a `VerifiedArtifact` (SI-28).** `verify_file`, `Expected` and `SigningKey::from_text` are
`pub(crate)`, so outside `krate-manager-core` the only route to a `VerifiedArtifact` is `download::obtain_and`, which
builds `Expected` from a feed-derived `DownloadRequest` and passes `env.ctx.signing_key` (the release key in
production, `lib.rs`'s one `assemble_context` call). Every caller this phase needs is in core: `download.rs`
(production), `verify_tests.rs` (KAT and plan tests), `download_tests.rs` (SC-011's crash check, SC-031's link case),
`test_signer.rs` (`key()`) and `test_support.rs` (`kat_signing_key()`), which compiles into core's lib under
`test-support` and therefore needs no `cfg` gate on `from_text`. `SigningKey::release()` and `key_id_hex()` stay `pub`
(`src-tauri/src/lib.rs` calls `release()`); `VerifiedArtifact`'s accessors stay `pub` (Phases 8 and 10 read them).
Phase 11, if it verifies a manager update with the same code, adds a core function that takes the key from
`AppContext.signing_key`, never from its caller. The FR-043 guard pins the visibility and the call sites (§4.10.1).

The first doctest is a **complete** literal of all eight fields with valid values (`AssetName` has no public
constructor, so it arrives as a parameter, a public route that needs none): the only reason it can fail to compile is
field privacy (E0451), so it fails if a field ever becomes `pub`. A partial literal would fail with E0063 (missing
fields) whatever the visibility, and rustdoc checks a `compile_fail,E0451` code only on nightly, so the literal itself
must be complete. The third, plain (`no_run`) doctest uses the same paths, the same parameter type and the accessors and
must compile, so a misspelled path cannot be what makes the first one fail.

`verify_file`, in D6's order, stopping at the first failure:

1. `fs::symlink_metadata(path)` (error → `Io { kind }`, e.g. `NotFound`); not `file_type().is_file()` → `Io { kind:
   InvalidInput }` (PD-19); `File::open(path)` (error → `Io`); `file.metadata()?.len() != size` → `Size`, nothing read.
2. `contract::parse_minisig_text(signature)` → `SignatureText { detail: e.to_string() }`; then
   `minisign_verify::Signature::decode(signature)` → `SignatureText`.
3. `key.public.verify_stream(&sig)`: `UnexpectedKeyId` → `KeyId { expected: key.key_id_hex(), found: <hex of
   MinisigText.key_id> }`; `UnsupportedLegacyMode` (unreachable after step 2) → `SignatureText`.
4. One loop of `read` into a `vec![0u8; CHUNK]`; each chunk `sha.update(chunk)` and `verifier.update(chunk)`;
   `Interrupted` retried; other errors → `Io { kind }`; total ≠ `size` → `Size { expected, found: total }`.
5. `let digest: [u8; 32] = sha.finalize().into();` ≠ `sha256` → `Sha256 { expected: hex, found: hex }`.
6. `verifier.finalize()` → `Signature` on any error.
7. `contract::parse_trusted_comment(sig.trusted_comment())` → `TrustedComment { code, detail }` (`ContractError::Rule
   { code, .. }` → `code.as_str()`; `Unsupported` cannot come from this function).
8. Compare `name`, `version`, `platform`, `asset` with `expected.name`, `expected.version`,
   `expected.platform.as_str()`, `expected.asset.as_str()` byte for byte; the first difference → `Binding { field,
   signed, expected }` with `field` one of `"name" | "version" | "platform" | "asset"`.
9. `Ok(VerifiedArtifact { path: path.to_path_buf(), …, format: expected.format })`.

The file is never read whole (FR-034, FR-082); the key is never read from disk at run time (D6).

### 4.4 `download.rs` (FR-001 to FR-066)

#### 4.4.1 Types

```rust
pub const LOG_TARGET: &str = "krate_manager_core::download";
pub const DOWNLOADS_DIR: &str = "downloads";
pub const PROGRESS_STEP: u64 = 256 * 1024;
pub const DOWNLOAD_STALL: Duration = Duration::from_secs(30);
pub const DOWNLOAD_ATTEMPT_DEADLINE: Duration = Duration::from_secs(3600);
const BACKOFF_SLICE: Duration = Duration::from_millis(50);
pub use crate::http::CancelToken;

#[derive(Clone, Debug, PartialEq)]
pub struct DownloadRequest {
    name: String, version_text: String, key: PlatformKey, asset: PlatformAsset,
    running: Option<OsVersion>,                          // PD-3
    #[cfg(test)] fetch_url: Option<String>,             // D13
}
impl DownloadRequest {
    pub fn name(&self) -> &str; pub fn version_text(&self) -> &str;
    pub fn key(&self) -> PlatformKey; pub fn asset(&self) -> &PlatformAsset;
    #[cfg(test)] pub(crate) fn with_fixture_url(self, url: &str) -> Result<DownloadRequest, &'static str>;
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DownloadPolicy { pub attempts: u32, pub backoff: Vec<Duration>, pub stall: Duration, pub attempt_deadline: Duration }
impl DownloadPolicy { pub fn production() -> Self; } // 4, [1 s, 2 s, 4 s], 30 s, 3600 s (FR-018)

/// The inputs every download entry point shares (Q-B, PD-11): the context (staging root `ctx.roots`, transport
/// `ctx.http`, release key `ctx.signing_key`), the staging I/O, the policy and the cancel token (R-1). Four borrows,
/// `Copy`, passed by value; a caller builds one per call (or one per thread) from parts it owns, and a variant is
/// struct-update syntax: `DownloadEnv { io: &FsStagingIo, ..env }`. Not `Debug` (`dyn StagingIo` is not) and not
/// required to be `Send`: a thread builds its own from shared parts.
#[derive(Clone, Copy)]
pub struct DownloadEnv<'a> {
    pub ctx: &'a AppContext,
    pub io: &'a dyn StagingIo,
    pub policy: &'a DownloadPolicy,
    pub cancel: &'a CancelToken,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RestartReason { RangeIgnored, RangeRefused, PartialTooLong, HashMismatchAfterResume, CachedInvalid } // PD-5

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Progress {
    Started { total: u64, resumed_from: u64 },
    Bytes { received: u64, on_disk: u64, total: u64 },
    Restarted { reason: RestartReason },
    Retrying { attempt: u32, of: u32, code: String, wait_ms: u64 },
    Verifying,
    Verified,
}

pub struct Obtained<T> { pub value: T, pub resumed: bool, pub cached: bool, pub transferred: u64 }

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct DownloadSummary { pub name: String, pub version: String, pub platform: String,
                             pub asset: String, pub size: u64, pub resumed: bool }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HeadVerdict { Full, Continue, RestartFull }

pub(crate) struct StagedPaths { pub part: PathBuf, pub dir: PathBuf, pub verified: PathBuf }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Target { pub name: String, pub asset: String, pub url: String }
```

`Obtained.resumed` = the verified bytes were assembled from more than one piece (PD-8); `cached` = FR-046's path;
`transferred` = body bytes this call wrote to disk across all its responses.

`with_fixture_url`: `url::Url::parse`; scheme `http`; host `Host::Ipv4(127.0.0.1)` or `Host::Ipv6(::1)` (a name such as
`localhost` is a `Domain` and refused); `port()` is `Some` (an explicit non-default port); `path_segments().next_back()
== Some(self.asset.asset_name.as_str())`; else `Err` with a static reason. No `"http://"` literal is needed (FR-082).
The transfer fetches `self.fetch_target()`: under `cfg(test)` `fetch_url.as_deref().unwrap_or(asset.url.as_str())`,
otherwise `asset.url.as_str()`. Logs and errors always use `asset.url` (D13).

#### 4.4.2 `request_for` (FR-001)

`feed.plugins.get(name)` (none → `UnknownPlugin`); `version::resolve_native(entry, running)`: `Available { key, asset
}` → a request with `asset.clone()` and `running.copied()`; the other three → `Unavailable { why }`.

#### 4.4.3 Paths (FR-003)

```rust
pub(crate) fn staged_paths(downloads: &Path, hex: &str, asset: &str) -> Result<StagedPaths, DownloadError>;
```

`hex`: exactly 64 bytes, each `0-9` or `a-f`. `asset`: non-empty, no `/` or `\`, not `.` / `..`, and
`Path::new(asset).components()` is exactly one `Component::Normal` (on Windows `C:x` and `C:\x` start with a `Prefix`,
so they are refused there; elsewhere `C:x` is a normal name, as SC-033 expects). `part = downloads/<hex>.part`, `dir =
downloads/<hex>`, `verified = dir/<asset>`; each `starts_with(downloads)` and differs from it. Any failure → `Path {
reason }`, nothing touched. `obtain_and` then calls `ctx.roots.check_sandboxed(&paths.verified)`.

#### 4.4.4 Staging I/O (FR-050, FR-051; PD-6)

```rust
pub trait PartialFile { fn write_all(&mut self, bytes: &[u8]) -> io::Result<()>; fn sync(&mut self) -> io::Result<()>; }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EntryKind { Dir, File, Other }                                  // Other: a symlink, junction, socket, …
pub struct StagedEntry { pub name: OsString, pub kind: EntryKind }       // from DirEntry::file_type() (no follow)
pub trait StagingIo {
    fn create_dir_all(&self, dir: &Path) -> io::Result<()>;             // then symlink_metadata(dir) must be a dir
    fn kind_of(&self, path: &Path) -> io::Result<Option<EntryKind>>;    // fs::symlink_metadata: NotFound → None,
                                                                         // else the kind, never following (SI-29)
    fn len_of(&self, path: &Path) -> io::Result<Option<u64>>;           // fs::symlink_metadata: NotFound → None,
                                                                         // not a regular file → Err(InvalidInput)
    fn open_partial(&self, path: &Path, truncate: bool) -> io::Result<Box<dyn PartialFile>>;
    fn rename(&self, from: &Path, to: &Path) -> io::Result<()>;
    fn remove_file(&self, path: &Path) -> io::Result<()>;
    fn remove_dir_all(&self, dir: &Path) -> io::Result<()>;
    fn read_dir_entries(&self, dir: &Path) -> io::Result<Vec<StagedEntry>>;  // the one read_dir (FR-082)
}
pub struct FsStagingIo;
```

`FsStagingIo::open_partial`: `truncate == false` → `OpenOptions::new().append(true).create(true)`; `true` →
`.write(true).create(true).truncate(true)` (std refuses `append` + `truncate`). `sync` = `File::sync_all`. No mode,
attribute or timestamp is set anywhere (FR-004). `remove_dir_all` is std's, which does not follow symlinks.
`FsStagingIo::create_dir_all(dir)` = `fs::create_dir_all(dir)`, then `fs::symlink_metadata(dir)?.file_type().is_dir()`
or `Err(InvalidInput)` (a `downloads/` or `<hex>` that is a link is refused, step `create_dir`, PD-19). `len_of` never
follows the final link. `FsStagingIo::kind_of` maps `symlink_metadata(path)?.file_type()` exactly as
`read_dir_entries` maps `DirEntry::file_type()` (`is_dir()` → `Dir`, `is_file()` → `File`, else `Other`; on Windows a
junction or directory symlink is `Other`, PD-19 evidence). Nothing opens a staged path that the prune has not just
classified by `EntryKind`, and nothing is measured or opened under `<hex>/` until `kind_of(<hex>)` has been `None`
or `Some(Dir)` in the same call (PD-19, SI-29).

#### 4.4.5 The in-flight registry and the prune (FR-005, FR-006; D10, PD-12)

```rust
static IN_FLIGHT: Mutex<BTreeSet<(PathBuf, [u8; 32])>> = Mutex::new(BTreeSet::new());
struct InFlight { key: (PathBuf, [u8; 32]) }          // Drop removes the key (also during unwinding)
impl InFlight { fn enter(downloads: &Path, sha: [u8; 32]) -> Option<InFlight>; }  // None = busy
fn registry() -> MutexGuard<'static, BTreeSet<…>>   // lock().unwrap_or_else(PoisonError::into_inner)
```

`prune(io, downloads, live: &BTreeSet<String>)` takes the registry guard for its whole pass (PD-12), lists
`downloads/`, and for each entry: name not UTF-8, or neither `<hex>.part` nor `<hex>` (64 lower-case hex) → remove;
the wrong kind (a `<hex>` that is not `EntryKind::Dir`, a `<hex>.part` that is not `EntryKind::File`, PD-19) → remove,
whatever `live` and the registry say; a valid `<hex>` of the right kind that is in `live` or in flight for this
`downloads` dir → keep; else remove. `Dir` entries go through `remove_dir_all`, everything else (`File`, `Other`:
files, symlinks, junctions) through `remove_file`, which removes the link itself, never its target. On Windows
(`#[cfg(windows)]`), an `Other` entry whose `remove_file` fails is retried once with `remove_dir_all`, because std's
`remove_file` cannot remove a directory symlink or a junction there and `remove_dir_all` removes the link itself
without following it (PD-19 evidence); only the second failure is logged. A link the prune still cannot remove is
logged as any failed removal and is never followed, because every later access checks before it opens (SI-29):
step 4 probes `kind_of(<hex>)` before `len_of(<hex>/<asset>)` and refuses anything but `None` / `Some(Dir)`
(`E_STAGING_IO`, step `open`, nothing under `<hex>` measured, opened or removed); step 5's `len_of` refuses a `.part`
link (`E_STAGING_IO`, step `open`); step 8's `create_dir_all` refuses a `<hex>` link (`E_STAGING_IO`, step
`create_dir`, the `.part` kept); `verify_file` step 1 refuses a final-component link. The call fails closed at each.
Proof per OS: `staged_symlinks_are_not_followed` (`#[cfg(unix)]`, symlinks) and `staged_links_are_not_followed_windows`
(`#[cfg(windows)]`, junctions made with `cmd /C mklink /J`, which needs no privilege, unlike a symbolic link; the
same mechanism as `detect_tests.rs:2587-2593`), each with a case where the removal is made to fail and step 4 must
refuse (§7.1). A failure logs one `warn!` `E_STAGING_*` with step `remove` and the entry name and continues. `live` = the lower-case hex of every entry's native asset
(`resolve_native(entry, request.running.as_ref())` → `Available`).

#### 4.4.6 `classify_head` (FR-013; PD-9)

```rust
pub fn classify_head(requested_from: u64, size: u64, head: &ResponseHead) -> Result<HeadVerdict, DownloadError>;
```

Encoding first: `content_encoding` present and not ASCII-case-insensitively `identity` (trimmed) → `Encoding`. This
sees only what ureq leaves in the head (PD-17): `br` (no `brotli` feature), `deflate`, `zstd`, `compress`, `x-gzip`,
`GZIP` and unknown tokens arrive and are refused; exactly `gzip` never arrives, because ureq has removed the header and
decodes the body, so a gzip response is classified by the rows below with `Content-Length` absent, and its guards are
`max_body` and `verify_file`. The function still refuses `gzip` when a head carries it (the pure table tests it). Then:

| `status` | `requested_from` | `Content-Range` | `Content-Length` | Verdict |
|---|---|---|---|---|
| 200 | any | ignored | absent or `size` | `Full` |
| 200 | any | ignored | other | `Err(Size { expected: size, found: Some(cl) })` |
| 206 | > 0 | exactly `bytes <from>-<size-1>/<size>` | absent or `size - from` | `Continue` |
| 206 | > 0 | exactly that | other | `RestartFull` (PD-9) |
| 206 | > 0 | anything else or absent | any | `RestartFull` |
| 206 | 0 | any | any | `RestartFull` |
| other | — | — | — | `Err(HttpStatus { status })` (defensive; `get_streamed` never passes one) |

The function is pure; errors carry a placeholder target that `obtain_and` replaces with the real one (a private
`DownloadError::with_target`).

#### 4.4.7 Progress state (FR-020 to FR-023; D8)

One private `Meter` owns the `&mut dyn FnMut(Progress)` and the counters: `size`, `on_disk`, `high_water`,
`since_last` (bytes since the last `Bytes`), `requests`. Methods: `started(resumed_from)` (emits `Started`, sets
`on_disk = resumed_from` and `high_water = resumed_from`: the existing `.part` length, so a resumed run's `Bytes`
continue from it and its last one reaches `on_disk == size`, PD-20),
`wrote(n)` (`on_disk += n`, `high_water = max(high_water, on_disk)`, `since_last += n`; emit `Bytes { received:
high_water, on_disk, total: size }` when `since_last ≥ PROGRESS_STEP` or `on_disk == size`, then `since_last = 0`),
`restarted(reason)` (emit; `on_disk = 0`, `since_last = 0`), `retrying(attempt, of, code, wait)`, `verifying()`,
`verified()`. The meter never emits on a timer. Because `on_head` and `sink` both need it, the attempt state
(`Meter`, the open `PartialFile`, the kept verdict, the recorded staging failure) lives in one `RefCell` and both
closures borrow it in turn.

#### 4.4.8 `obtain_and` (FR-045 to FR-047, FR-017, FR-018; D4, D5, D7; PD-5, PD-7, PD-8)

```rust
pub fn obtain_and<T>(
    env: DownloadEnv<'_>, feed: &Feed, request: &DownloadRequest,
    progress: &mut dyn FnMut(Progress),
    next: impl FnOnce(VerifiedArtifact) -> T,
) -> Result<Obtained<T>, DownloadError>;
```

Five parameters, no attribute (PD-11). In the steps below `ctx`, `io`, `policy` and `cancel` are `env.ctx`,
`env.io`, `env.policy` and `env.cancel`. A thin wrapper runs the body below (a private `fn obtain_body<T>(env, feed,
request, progress, next)`, the same five), then logs once: `info!` on `Ok` (plugin, version, platform key,
`transferred`, `resumed`, `cached`) or `warn!("{e}")` on `Err` (FR-053). The private helpers keep within the limit
too: `prune(io, downloads, live)` (3), `wait_or_cancel(wait, cancel)` (2), `staged_paths(downloads, hex, asset)` (3),
`classify_head(requested_from, size, head)` (3); a helper split out of the transfer loop takes `env`, `request` and
the private `RefCell` attempt state of §4.4.7, never a list of its fields.

1. `downloads = ctx.roots.staging_dir().join(DOWNLOADS_DIR)`; `InFlight::enter` → `None` → `Busy` (no I/O, no event).
2. `cancel.is_cancelled()` → `Cancelled` (no I/O, no event; SC-034 a).
3. `staged_paths` + `check_sandboxed`; `io.create_dir_all(&downloads)` (step `create_dir`); `prune`.
4. **Cached file (FR-046).** First `io.kind_of(&dir)` (step `open`): `None` → no cached file, go to step 5;
   `Some(Dir)` → continue; any other kind (a `<hex>` link the prune could not remove) → `Staging { step: open, kind:
   InvalidInput }` (`E_STAGING_IO`), returned with nothing under `<hex>` measured, opened or removed and no event
   (PD-19, SI-29). Then `io.len_of(&verified)` (step `open`) is `Some` → `Started { resumed_from: size }`,
   `Verifying`, `verify_file(&verified, …)`: `Ok` → `Verified`, `next`, `Obtained { resumed: false, cached: true,
   transferred: 0 }`. `Err` → `io.remove_dir_all(&dir)` (step `remove`; a failure is returned), best-effort removal of
   a `.part`, `Restarted { CachedInvalid }`, `cache_failed = true`, `part_len = 0`.
5. **Existing partial.** Otherwise `part_len = io.len_of(&part)?.unwrap_or(0)`; `Started { resumed_from: part_len if
   ≤ size else 0 }`; `part_len > size` → `open_partial(part, true)` (step `truncate`), `Restarted { PartialTooLong }`,
   `part_len = 0`. `foreign = part_len > 0` (PD-8).
6. **Budget.** `budget = policy.attempts.max(1)` (a zero policy still makes one request), `requests = 0`, `redo_left = !cache_failed`.
7. **Transfer loop** while `part_len < size`:
   - `requests == budget` → return the last retryable error (or, for a refusal with no unit left, `HttpStatus {
     status: 416 | 206 }`).
   - `cancel` set → `Cancelled`.
   - `requests += 1`; `from = part_len`; headers `[("Range", "bytes=<from>-")]` when `from > 0`, else none.
   - `get_streamed(&ctx.http, fetch_target, headers, StreamLimits { stall: policy.stall, deadline:
     policy.attempt_deadline }, on_head, sink, cancel)` where:
     - `on_head` keeps `classify_head(from, size, head)`; `Full` with `from > 0` → `open_partial(part, true)`
       (`truncate`), `Restarted { RangeIgnored }`, `part_len = 0`, `foreign = false`, `writers = 0`, then `Read {
       max_body: size }` (D4's fallback writes the whole body from byte 0, SI-27); `Full` with `from == 0` →
       `open_partial(part, false)` (`open`), `Read { max_body: size }`; `Continue` → `open_partial(part, false)`
       (`open`), `Read { max_body: size - from }`; on an I/O error the step and kind are recorded and `Stop`
       returned; `RestartFull` and `Err` → `Stop`. The limit is therefore always `size - part_len` after the verdict,
       so nothing past byte `size` of the file is ever written (FR-014).
     - `sink` writes the chunk (`write`; recorded on error), counts the first byte of this response into `writers`,
       calls `meter.wrote(n)` and adds to `transferred`.
   - Outcome:
     - `Ok(StreamEnd)` and `part_len == size` → `sync` (step `sync`), close, leave the loop.
     - `Ok(StreamEnd)` with `part_len < size` (a close-delimited body ended short) → as `Body` below.
     - `HeadStopped` with `RestartFull`, or `Http(Status { 416 })` with `from > 0` → `open_partial(part, true)`
       (`truncate`), `Restarted { RangeRefused }`, `part_len = 0`, `foreign = false`, `writers = 0`, next iteration
       at once (no backoff, no `Retrying`; it consumes a unit, PD-7).
     - `HeadStopped` with a classify error, or `Http(TooLarge)` → `Size` / `Encoding`; remove the `.part` (best
       effort, FR-014); return.
     - `HeadStopped` / `Sink` with a recorded staging failure → `Staging`; after `write` / `truncate` (and `sync`)
       remove the `.part` (best effort); return.
     - `Cancelled` → return `Cancelled`; the `.part` stays (FR-017).
     - a retryable error (`Offline`, `Timeout`, `Stalled`, `Body`, `HttpStatus` 408 / 429 / 500 / 502 / 503 / 504):
       `part_len = io.len_of(&part)?` (the file is the truth after a partial write); if `requests < budget` →
       `Retrying { attempt: requests, of: policy.attempts, code, wait_ms }` with `wait = backoff[min(requests - 1,
       len - 1)]` (zero when the list is empty), then `wait_or_cancel(wait, cancel)` in `BACKOFF_SLICE` slices
       (`Cancelled` if set); else return it.
     - any other error → return it.
8. **Verify the `.part`.** `Verifying`; `verify_file(&part, …)`:
   - `Ok(a)` → `io.create_dir_all(&dir)` (`create_dir`, `.part` kept on failure), `io.rename(&part, &verified)`
     (`rename`, `.part` kept), `a.relocated(verified)`, `Verified`, `next(a)` → `Obtained { resumed: foreign ||
     writers ≥ 2, cached: false, transferred }`.
   - `Err(Sha256)` with `(foreign || writers ≥ 2) && redo_left` → remove `.part`, `Restarted { HashMismatchAfterResume
     }`, `redo_left = false`, `budget += 1`, `part_len = 0`, `foreign = false`, `writers = 0`, back to step 7.
   - any other `Err` → remove `.part` (best effort), return `Verify { target, error }` (FR-047).

Proof of the PD-7 bounds: requests ≤ `attempts` until the one redo raises the budget to `attempts + 1`, so requests ≤
`attempts + 1` (FR-015). A `Retrying` is emitted only for a failed request with a unit left; the request that
completed the redone file's predecessor succeeded, so at most `attempts + 1 - 2 = attempts - 1` requests failed with a
unit left (FR-023). `Verifying` occurs once per pass; the redo is refused after a cached failure, so at most two
(FR-021).

No path through steps 4-8 calls `next` for a failure, and `next` receives only a `relocated` artifact at
`downloads/<hex>/<asset>` (FR-039).

#### 4.4.9 `discard` (FR-049)

```rust
pub fn discard(artifact: VerifiedArtifact, io: &dyn StagingIo) -> Result<(), DownloadError>;
```

Unchanged by Q-B: two parameters, and no `DownloadEnv`, because `discard` needs only the staging I/O (it makes no
request, uses no policy or token, and derives its registry key from the artifact's own path, not from `ctx.roots`).
`downloads = artifact.path().parent()?.parent()?` (missing → `Path`); `paths = staged_paths(downloads,
&hex_lower(artifact.sha256()), artifact.asset().as_str())?`; `paths.verified != artifact.path()` → `Path`;
`InFlight::enter(downloads, *artifact.sha256())` → `None` → `DiscardBusy { name, asset }` (`E_DOWNLOAD_BUSY`, nothing
removed, PD-18); holding that guard, `io.remove_dir_all(&paths.dir)`: `Ok` or `NotFound` → `Ok(())`; else `Staging {
step: remove }` returned; the guard drops on return. The registry key is the same `(downloads, sha256)` pair
`obtain_and` enters (the artifact's path was built from that `downloads` join, so the `PathBuf`s compare equal).
Nothing else in `downloads/` is touched.

#### 4.4.10 Command functions (FR-060 to FR-066)

```rust
pub fn command_request(ctx: &AppContext, name: &str, expected_version: &str, now_unix: u64, host: &dyn HostFacts)
    -> Result<(Feed, DownloadRequest), DownloadError>;
pub fn run_command(env: DownloadEnv<'_>, feed: &Feed, request: &DownloadRequest,
                   progress: &mut dyn FnMut(Progress)) -> Result<DownloadSummary, DownloadError>;
pub fn for_command(env: DownloadEnv<'_>, name: &str, expected_version: &str, now_unix: u64,
                   host: &dyn HostFacts, progress: &mut dyn FnMut(Progress))
    -> Result<DownloadSummary, DownloadError>;
pub fn channel_progress<E>(send: impl FnMut(Progress) -> Result<(), E>) -> impl FnMut(Progress);
```

Arity (PD-11): `command_request` 5, `run_command` 4, `for_command` 6, `channel_progress` 1; no attribute.
`command_request` keeps `ctx` rather than the env: it does no staging I/O and no transfer, so it is handed nothing it
must not use (FR-065: no staging I/O before the version check). `for_command(env, …)` is
`command_request(env.ctx, name, expected_version, now_unix, host)` then `run_command(env, &feed, &request, progress)`,
nothing else.

`command_request`: `feed::check(ctx, FetchMode::Normal, now_unix)`; no snapshot → `NoFeed { cause: error.code(),
detail: scrub(error) }`; `running = host.os_version()` (once, before the name lookup, so SC-028 b sees `["os_version"]`);
`request_for(&snapshot.feed, name, running.as_ref())?` (an unknown name is decided before the version, FR-062);
`request.version_text != expected_version` → `VersionChanged`. `run_command` calls `obtain_and(env, feed, request,
progress, |a| (name, version, platform.as_str(), asset, size))` and fills `resumed`. `for_command` is the two in
sequence.
`channel_progress` returns `move |e| { let _ = send(e); }` (the `Result` is dropped on purpose: the webview may be
gone, FR-064; written as `if send(e).is_err() {}` if clippy prefers it).

### 4.5 `context.rs` (FR-041)

`AppContext` gains `pub signing_key: SigningKey`; `assemble_context(roots, feed_url, http, signing_key: SigningKey)`
and `assemble_with_os(os, roots, feed_url, http, signing_key)` pass it into the literal at `:102-107`;
`reassemble_with_os` copies `ctx.signing_key.clone()` into the literal at `:162-167`. No other behaviour changes.

### 4.6 `test_signer.rs` (FR-083; D1)

Declared `#[cfg(test)] mod test_signer;` in `lib.rs`; the only file besides `verify_tests.rs` and `download_tests.rs`
that names `minisign ::`.

```rust
pub(crate) struct TestSigner { pair: minisign::KeyPair }
impl TestSigner {
    pub(crate) fn new() -> TestSigner;                      // generate_unencrypted_keypair(), in memory only
    pub(crate) fn key_id(&self) -> [u8; 8];                 // pair.pk.keynum()
    pub(crate) fn public_text(&self) -> String;             // "untrusted comment: minisign public key: <ID>\n<to_base64()>\n"
    pub(crate) fn key(&self) -> SigningKey;                 // SigningKey::from_text(&public_text())
    pub(crate) fn sign(&self, data: &[u8], trusted_comment: &str) -> String;   // minisign::sign(Some(pk), sk, data, Some(tc), None)?.to_string()
    pub(crate) fn sign_c3(&self, data: &[u8], name: &str, version: &str, key: PlatformKey, asset: &str) -> String;
}
pub(crate) fn c3_comment(name: &str, version: &str, key: PlatformKey, asset: &str) -> String; // "krate:1:…"
/// Line 2's bytes 2..10 replaced by `id`, re-encoded with base64 STANDARD (canonical); for SC-003 g.
pub(crate) fn with_key_id(signature: &str, id: [u8; 8]) -> String;
```

No key, public or secret, is written to any file (FR-083).

### 4.7 `test_support.rs` additions (FR-083, FR-085)

Compiled into core's lib with `test-support` too, so it uses only std and core's normal dependencies (D1):

- `pub fn kat_signing_key() -> SigningKey` (`include_str!` of `tools/feed-contract/fixtures/kat/kat.pub`; PD-14).
- `pub fn native_key() -> PlatformKey` = the first of `version::platform_candidates(NativePlatform.os(),
  Arch::native())`, panicking on an empty list.
- `pub struct TestPlugin<'a> { pub name: &'a str, pub version: &'a str, pub key: PlatformKey, pub asset: &'a str,
  pub size: u64, pub sha256: [u8; 32], pub signature: String, pub min_os: Option<&'a str> }` and `pub fn
  test_feed(plugins: &[TestPlugin<'_>]) -> Feed`: builds a v2 document with `serde_json::json!` (`version`,
  `download_url` and `release_notes_url` on `https://assets.example.invalid/<lower>/`, `release_notes: ""`, `tag:
  "<lower>/v<version>"`, `pub_date: "2026-10-31T00:00:00Z"`, `channel: "stable"`, one `platforms` entry under `key`
  with `url: https://assets.example.invalid/<lower>/v<version>/<asset>`, the format and `min_os` its prefix requires
  (`darwin` default `15.0`, `linux` default `2.31`), a `payload` for `tar.gz`, and the `install` block the prefix
  requires: `vst3_bundle "<Name>.vst3"`, darwin `au_component "<Name>.component"` + `macos_pkg_id
  "com.krateaudio.<lower>"`, windows `windows_uninstall_key "{0000000N-0000-4000-8000-00000000000N}_is1"` with N the
  plugin's index), then `feed::parse(&bytes)` and panics with the findings on rejection. Every test feed therefore
  passes the contract (FR-085).
- `pub struct FaultyStagingIo` (std atomics, so `&self` methods): delegates to `FsStagingIo`; fields `fail: Option<(Op,
  usize /* n-th call of that op */, io::ErrorKind)>`, `panic_after: Option<usize>` (panic right after the k-th call
  returns, counting every method and every `PartialFile::write_all` / `sync`), `calls: AtomicUsize`, `written:
  AtomicU64` (bytes passed to `write_all`), `per_op: [AtomicUsize; 11]` (calls per `Op`). `Op` = `CreateDir | KindOf
  | LenOf | Open | Truncate | Write | Sync | Rename | RemoveFile | RemoveDir | ReadDir` (`Open` / `Truncate` =
  `open_partial(_, false / true)`). Constructors, so no test
  spells the atomics: `pub fn healthy() -> FaultyStagingIo` (no fault; delegates every call), `pub fn failing(op: Op,
  nth: usize, kind: io::ErrorKind) -> FaultyStagingIo`, `pub fn panicking_after(k: usize) -> FaultyStagingIo`;
  `pub fn calls(&self) -> usize`, `pub fn calls_of(&self, op: Op) -> usize` and `pub fn written(&self) -> u64` read
  the counters (`calls_of` lets a row prove that a step never ran: SC-003 c, the step-4 refusal cases of §7.1).
- `pub fn test_policy() -> DownloadPolicy` = §7's test policy, `DownloadPolicy { attempts: 4, backoff:
  vec![Duration::ZERO], stall: Duration::from_secs(2), attempt_deadline: Duration::from_secs(10) }` (one definition
  instead of a literal per test file).
- **`DownloadRig`** (Q-B, PD-11): the one place a test builds a `DownloadEnv`.

  ```rust
  pub struct DownloadRig {
      pub ctx: AppContext,
      pub io: FaultyStagingIo,
      pub policy: DownloadPolicy,
      pub cancel: CancelToken,
  }
  impl DownloadRig {
      /// `io: FaultyStagingIo::healthy()`, `policy: test_policy()`, `cancel: CancelToken::new()`.
      pub fn new(ctx: AppContext) -> DownloadRig;
      /// `DownloadEnv { ctx: &self.ctx, io: &self.io, policy: &self.policy, cancel: &self.cancel }`.
      pub fn env(&self) -> DownloadEnv<'_>;
  }
  ```

  The test builds `ctx` (an `AppContext` literal as the Phase 4-6 tests do, SI-16, over `fresh_temp_roots()`, with
  `http: HttpConfig::loopback(SEC, SEC)` and `signing_key: signer.key()` or `kat_signing_key()`: `TestSigner` is
  `#[cfg(test)]` in core and cannot be named here, D1) and hands it to `new`. A row varies the rig by assigning a
  public field before the call (`rig.policy = DownloadPolicy { stall: …, ..test_policy() }`, `rig.io =
  FaultyStagingIo::failing(…)`) or by struct update on one call (`DownloadEnv { io: &FsStagingIo, ..rig.env() }`, the
  "second `obtain_and` with `FsStagingIo`" of SC-011). A row that needs fresh counters builds a new rig. Threads:
  `DownloadRig` is `Sync` (`AppContext` is shared across `thread::scope` threads already, `feed_tests.rs:1489-1497`;
  `FaultyStagingIo` holds atomics and plain data; `CancelToken` is an `AtomicBool`; `DownloadPolicy` is plain data),
  so a scoped thread captures `&rig` and calls `rig.env()` itself; `DownloadEnv` never crosses a thread. The rig is
  compiled with `test-support` too, so it uses only `download`'s public types (no `minisign`, D1).

The FR-088 patterns stay absent from `test_support.rs` (`injection.rs:198-221`): no `127.0.0.1:<port>`, no
`/usr/`, no `ProgramData`.

### 4.8 `fixture_server.rs` additions (FR-084)

```rust
Ranged { body: Vec<u8> },
/// The k-th request to the route gets the k-th reply; the last repeats. Resolved only at the top level.
Script(Vec<Reply>),
/// 200 with Content-Length, `body` sent `chunk` bytes at a time with `interval` between chunks.
Paced { body: Vec<u8>, chunk: usize, interval: Duration },
```

- `Shared` gains `hits: Mutex<BTreeMap<String, usize>>`; `reply_for(path)` resolves a `Script` by the route's hit
  count (incremented under the same lock), so `answer` never sees a `Script`.
- `answer(&mut self, reply: &Reply, request: &RecordedRequest)`; `Delayed` passes `request` on.
- `Ranged`: a `Range` header (name ASCII-case-insensitive) of exactly `bytes=<digits>-` with `n < len` → `HTTP/1.1
  206 Partial Content`, `Content-Range: bytes <n>-<len-1>/<len>`, `Content-Length: <len-n>`, `Connection: close`,
  the tail; `n ≥ len` → `status_response(416, &[("Content-Range", "bytes */<len>")])`; no or another `Range` →
  `ok_head(len)` + body.
- `Paced`: `ok_head(len)`, then the chunks with `self.shared.wait(interval)` between (stops on shutdown).
- `reason` gains `206 Partial Content`, `408 Request Timeout`, `416 Range Not Satisfiable`, `429 Too Many Requests`.
- Existing replies, the TLS answer, the `CONNECT` rule and every existing test are unchanged.

### 4.9 `src-tauri` (FR-060, FR-061, FR-066)

`commands.rs`:

```rust
#[tauri::command]
pub async fn download_plugin(
    state: tauri::State<'_, SharedState>,
    name: String,
    expected_version: String,
    on_progress: tauri::ipc::Channel<Progress>,
) -> Result<DownloadSummary, String> {
    let ctx = { let guard = state.read().unwrap_or_else(PoisonError::into_inner);
                guard.context.clone().map_err(|e| e.to_string())? };   // the guard is dropped before any await
    let now_unix = SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |d| d.as_secs());
    tauri::async_runtime::spawn_blocking(move || {
        let policy = DownloadPolicy::production();
        let cancel = CancelToken::new();                                  // never set in Phase 7 (FR-017)
        let env = DownloadEnv { ctx: &ctx, io: &FsStagingIo, policy: &policy, cancel: &cancel };
        download::for_command(env, &name, &expected_version, now_unix, &PlatformFacts { io: &OsHostIo },
            &mut download::channel_progress(|e| on_progress.send(e)))
            .map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| DownloadError::Task { detail: e.to_string() }.to_string())?
}
```

The `DownloadEnv` is built inside the `spawn_blocking` closure from values the closure owns (`ctx`, `policy`,
`cancel`), so it borrows nothing across the `await` and need not be `Send`; `commands.rs` imports `DownloadEnv`,
`DownloadPolicy`, `FsStagingIo` and `CancelToken` from `krate_manager_core::download`. No guard pins the call's text
(`config_guard.rs` and `injection.rs` check only which crates and host types `commands.rs` names, `config_guard.rs:104`,
`:1622-1647`, `:1976-1997`; `injection.rs:150`, `:2215-2228`; `OsHostIo` stays in `commands.rs`).

`lib.rs`: `use krate_manager_core::verify::SigningKey;`, `assemble_context(roots, &feed_url, http,
SigningKey::release())` (the one call, both feature arms share it), `commands::download_plugin` appended to
`generate_handler!`. `build.rs`: `"download_plugin"` appended. `capabilities/main.json`: `"allow-download-plugin"`
appended (Tauri derives `allow-<command>` from the app manifest, as for the five existing ones). CSP, windows and
description unchanged. No TypeScript caller (FR-063).

### 4.10 Guards

#### 4.10.1 `crates/core/tests/injection.rs`

Table edits (each array length updated):

| Table | Change | Rule |
|---|---|---|
| `FR007_DEPENDENCIES` | + `"minisign-verify"`, `"sha2"` (13) | FR-080 |
| dev-dependency assertion in `core_dependencies_are_the_fr007_set` | `{flate2, minisign}` | FR-080 |
| `CONFINED_TOML_KEYS` | + `("minisign-verify", [Cargo.toml, crates/core/Cargo.toml])`, `("sha2", same)`, `("minisign", same)` (12). The table confines a key to files, not sections; `minisign`'s "`[dev-dependencies]` only" is enforced by `core_dependencies_are_the_fr007_set` (its normal set is the 13 names, without `minisign`, and its dev set is exactly `{flate2, minisign}`) and by FR-081's walk (SI-21) | FR-082 |
| `PRODUCTION_BODIES` | + `(HTTP_TESTS_RS, "streamed_policy_cases")` (10) | SI-4 c |
| `PAC_BODIES` | + `(HTTP_TESTS_RS, "streamed_pac_fetch_keeps_the_feed_bound_unix")` (13) | SI-4 b |
| `TEST_PROGRAM_BODIES` | + `(DOWNLOAD_TESTS_RS, "verified_file_has_no_quarantine_macos", "/usr/bin/xattr")`, `(DOWNLOAD_TESTS_RS, "staged_links_are_not_followed_windows", "cmd")` (4); doc comment "the program literal" | SI-4 a, SI-15, SI-29 |
| new consts | `DOWNLOAD_RS`, `DOWNLOAD_TESTS_RS`, `VERIFY_RS`, `VERIFY_TESTS_RS`, `TEST_SIGNER_RS` | — |
| new const | `ARITY_SUPPRESSIONS: [&str; 2] = ["clippy::too_many_arguments", "clippy::complexity"]` (the lint and the one group that holds it that `GROUP_SUPPRESSIONS`, `injection.rs:98`, does not already report; `clippy::all` and `warnings` are D6 findings anywhere today) | Q-B (PD-11, SI-26) |

New tests (each a plain `#[test]`; every self-test plants each violation in an in-memory copy of the file set and
requires each to be reported, the existing `plant` / `assert_reported` helpers, `injection.rs:2056-2113`):

| Test | Checks |
|---|---|
| `verified_artifact_has_no_public_constructor` / `verified_artifact_scanner_reports_plants` | FR-043: `pub struct VerifiedArtifact {` once in the workspace, in `verify.rs`; no `pub` token inside its braces; no `Default` / `Deserialize` in its outer derives; no `impl` of `CONSTRUCTOR_TRAITS` for it anywhere; the token pair `VerifiedArtifact {` in code outside `verify.rs` only when preceded by `->` (a return type); `relocated` declared `pub ( crate ) fn relocated` in `verify.rs` and called (`relocated (`) only in `download.rs`. **Minting routes (SI-28):** `verify_file` declared once, as `pub ( crate ) fn verify_file`, in `verify.rs`, and the token pair `verify_file (` elsewhere in code only in `download.rs`, `verify_tests.rs`, `download_tests.rs`; `from_text` declared once, as `pub ( crate ) fn from_text`, in `verify.rs`, and `from_text (` elsewhere only in `test_signer.rs`, `test_support.rs`, `verify_tests.rs`, `download_tests.rs`; `struct Expected` declared `pub ( crate ) struct Expected` and no `pub` token inside its braces that is not followed by `( crate )`. Plants: a `pub` field, `#[derive(Default)]`, `impl From<PathBuf> for VerifiedArtifact` in `download.rs`, a struct literal in `download.rs`, a `relocated(` call in `test_support.rs`, `pub fn verify_file` in `verify.rs`, `pub fn from_text` in `verify.rs`, a `verify_file(` call and a `SigningKey::from_text(` call in `src-tauri/src/commands.rs`, a bare `pub name` field in `Expected`. |
| `no_secret_key_in_app_tree` / `no_secret_key_scanner_reports_plants` | FR-083 (SI-22), over every file under `apps/krate-manager/` except any `target/`, `node_modules/`, `dist/` directory (links not followed), read lossily: **(1)** no file name ends in `.key` (Phase 2 rule (a), `tools/feed-contract/tests/c3-trusted-comment.test.js:211-228`); **(2)** the plaintext rule on the file's lines: no line starts `untrusted comment:` and contains `secret key`, and no line matching `^untrusted comment: .*secret key\r?$` is followed by a line whose base64 decodes to ≥ 6 bytes with `Ed` at 0-1 and `B2` at 4-5 (Phase 2 rule (c)); **(3)** the same plaintext rule on decoded text: every maximal run of the STANDARD base64 alphabet (`A-Z a-z 0-9 + /`, then up to two `=`) of ≥ 40 characters that decodes with `base64::engine::general_purpose::STANDARD` (core's normal dependency, canonical padding) is decoded, read lossily, split into lines and checked by (2) — the form `tauri signer generate` writes (one base64 line of a standard minisign file, `specs/krate-manager/probes/results.md:10`, `:27`); **(4)** no `TAURI_SIGNING_PRIVATE_KEY` followed by optional spaces, `=` or `:`, optional spaces or a quote, and a non-empty value that does not start `${{ secrets.` (a CI secret reference, which in any case lives outside `apps/`); **(5)** no test source (`is_test_source`, `test_support.rs`, `test_signer.rs`) holds the tokens `generate_and_write_encrypted_keypair`, `generate_and_write_unencrypted_keypair`, `generate_encrypted_keypair`. Plants, each built at run time so that `injection.rs`'s own text holds none of them (the base64 by `STANDARD.encode` of a `format!`-assembled header, the variable name by `format!("{}={}", NAME, value)`): the plaintext line in a fixture text; a `x.key` file; `STANDARD.encode("untrusted comment: rsign encrypted secret key\n<a 74-byte blob starting Ed..B2>\n")` in a fixture text; `TAURI_SIGNING_PRIVATE_KEY=<that base64>` in an `.env` file; `TAURI_SIGNING_PRIVATE_KEY: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY }}` (must **not** be reported); a base64 public-key file text (must not be reported); each token in `download_tests.rs`. |
| `phase7_tests_hold_no_real_host` / `phase7_host_scanner_reports_plants` | FR-087 over `download_tests.rs`, `verify_tests.rs`, `test_signer.rs`, `test_support.rs` (each must exist): raw text holds no `github.com`, `githubusercontent.com`, `krateaudio.com`, `--test-threads`; code holds no `HttpConfig :: production` and no `SigningKey :: release` except inside `verify_tests.rs` `fn release_key_is_the_committed_key_and_not_a_test_key`. Plants: each needle; a missing file. |
| `verify_crates_are_confined` / `verify_confinement_scanner_reports_plants` | FR-082 via `confinement()`: `minisign_verify` only `VERIFY_RS`; `sha2` only `VERIFY_RS`; `minisign ::` only `TEST_SIGNER_RS`, `VERIFY_TESTS_RS`, `DOWNLOAD_TESTS_RS`; `test_signer` in `lib.rs` only as `# [ cfg ( test ) ] mod test_signer ;`; `ureq` still only `HTTP_RS` / `HTTP_TESTS_RS` (existing row). Plants: each crate in `download.rs`, `minisign ::` in `test_support.rs`, `pub mod test_signer;`. |
| `download_and_verify_hold_no_url_or_whole_read` / `download_verify_scanner_reports_plants` | FR-082: no literal of `download.rs` / `verify.rs` contains `http://` or `https://`; no `read_to_end`, `read_to_string`, `fs :: read (` token; `read_dir` exactly once in `download.rs`, inside `fn read_dir_entries` of `impl StagingIo for FsStagingIo`, and 0 times in `verify.rs`. Plants: each. |
| `too_many_arguments_is_never_suppressed` (one test, plants inline as in `disallowed_methods_expectations_are_the_seven_d6_sites`, `injection.rs:2558-2622`) | Q-B (PD-11, SI-26), over a pure `fn arity_problems(rs: &[(String, String)], manifests: &[(String, String)], clippy_configs: &[(String, String)]) -> Vec<String>`: **(1)** every file of `workspace_rs_files()` (`injection.rs:1315-1332`) plus the clippy-guard fixture's `src/` (as `:2561-2567`): no attribute whose `suppressed_lints` (`:1160-1183`, comments and literals blanked first) holds an `ARITY_SUPPRESSIONS` name, in `allow` or `expect`, outer or inner, inside `cfg_attr` or not; **(2)** `section_keys` (`:1289`) of `[workspace.lints.clippy]` in `apps/krate-manager/Cargo.toml` and of `[lints.clippy]` in every member manifest (`crates/*/Cargo.toml`, `src-tauri/Cargo.toml`) and in `tests/fixtures/clippy-guard/Cargo.toml` hold neither `too_many_arguments` nor `complexity` (a lint table can lower the level without an attribute, §1.2); **(3)** no `clippy.toml` or `.clippy.toml` in `apps/krate-manager/`, any `crates/*/`, `src-tauri/` or the fixture directory holds a `too-many-arguments-threshold` key (clippy reads the nearest such file walking up from the manifest dir, §1.2; today only `apps/krate-manager/clippy.toml` exists, `:1-10`). The real tree gives zero problems. Plants, each into an in-memory copy and each required to be reported: `#[expect(clippy::too_many_arguments, reason = "x")]` before a `fn` in `crates/core/src/download.rs`'s copy (the reason does not excuse it); `#[cfg_attr(test, allow(clippy::too_many_arguments))]` in `src-tauri/src/commands.rs`'s copy; `#![expect(clippy :: complexity)]` in `context.rs`'s copy; `too_many_arguments = "allow"` appended under the root `[workspace.lints.clippy]`; `too-many-arguments-threshold = 9` in the copy of `clippy.toml`. Quiet case (not reported): the lint name only in a `//` comment and in a string literal. The plant strings live in literals, which the scanner blanks, so `injection.rs`'s own text is clean |
| `fixture_url_seam_is_test_only` / `fixture_url_seam_scanner_reports_plants` | SC-032: `with_fixture_url` and `fetch_url` declared only in `download.rs`, each declaration directly preceded by `# [ cfg ( test ) ]` (optionally followed by `pub ( crate )`); `with_fixture_url` used elsewhere only in `download_tests.rs`; no `src-tauri/` file holds either. Plants: the attribute changed to `cfg(any(test, feature = "test-support"))`, a call in `test_support.rs`, a mention in `src-tauri/src/commands.rs`. |

#### 4.10.2 `crates/core/tests/config_guard.rs`

The Q-B ruling changes nothing here: no `config_guard.rs` check reads a download signature or the `for_command` call
text (§4.9); the six-command, six-permission and four-argument `assemble_context` expectations below stand as
revision 2 planned them.

- `main.json` permissions: the five + `"allow-download-plugin"` (`:607-626`).
- `build.rs`: `commands(&["app_version","startup_error","plugin_status","check_now","open_release_page",
  "download_plugin"])` and its message (`:640-653`).
- New check beside it: the squeezed `lib.rs` contains `generate_handler![commands::app_version,commands::startup_error,
  commands::plugin_status,commands::check_now,commands::open_release_page,commands::download_plugin]`. New plant:
  `commands::download_plugin` removed from the handler list → reported.
- `assemble_context(` argument count: expected four, message "expected four (FR-061, Phase 7 FR-041)" (`:831-838`);
  the plant (`:1704-1712`) replaces `assemble_context(roots, &feed_url, http, SigningKey::release())` with
  `assemble_context(roots, &feed_url, http)` and expects "with 3 top-level arguments".
- `LIB_RS_STRINGS` + `"SigningKey::release()"` (15).
- Label of the `:1543` plant: "seventh capability permission allow-planted" (its body is unchanged).
- New `core_has_no_signing_crate` / `signing_crate_scan_reports_plants` (FR-081): for each of `MEMBERS`, roots =
  keys of `[dependencies]`, every `[target.'…'.dependencies]` and `[build-dependencies]`, never
  `[dev-dependencies]`; walk the committed `Cargo.lock` (`[[package]]` blocks: `name`, `version`, `source`,
  `dependencies`; a dependency string `"<name>"` or `"<name> <version>"`, with an optional `" (<source>)"`, resolves
  to its package, by name when unique, else by name and version). A reached package without `source` is a workspace
  member and is expanded through **its manifest's** normal roots, never its lock entry (which also lists its
  dev-dependencies); a registry package is expanded through its lock entry. A reached package named `minisign`,
  `rpassword` or `scrypt` is a finding naming the member and the path. Plants (in-memory): `minisign = { workspace =
  true }` in core's `[dependencies]` (reported); an `"rpassword"` string added to the `log` package's lock dependencies
  (reported); and, on a copy of core's manifest whose `[dev-dependencies]` is replaced by exactly `minisign = {
  workspace = true }`, the walk must report **nothing** (FR-081's not-reported case, planted explicitly so it does not
  depend on the real tree keeping its dev-dependency). The real tree must also give zero findings.

#### 4.10.3 `crates/core/tests/roster.rs`

`roster_and_hosts_absent_from_sources` `must` list + `crates/core/src/download.rs`, `crates/core/src/verify.rs` (the
recursive walk already scans them; this makes it non-vacuous). `identifier_files()` + the same two paths, and its
`must` list + both (FR-088).

### 4.11 `tests/required-tests.json` and `EXPECTED_REQUIRED` (FR-086, FR-091)

Appended, in this order, to `cargo.all` (165 → 223; revision 2 planned 220, Q-B adds `too_many_arguments_is_never_suppressed`, review round 2 the last two) and identically to `EXPECTED_REQUIRED.cargo.all`:

```
download::tests::good_artifact_verifies
download::tests::rejections_never_reach_next_stage
verify::tests::kat_verifies_and_one_byte_changes_fail
verify::tests::kat_secret_message_verifies
verify::tests::invalid_trusted_comment_is_named
verify::tests::verify_steps_stop_at_first_failure          (plan)
verify::tests::signing_key_text_cases                      (plan)
download::tests::progress_is_monotonic_and_ends_at_size
download::tests::interrupted_download_resumes
download::tests::partial_from_a_previous_run_resumes
download::tests::stall_resumes
download::tests::ignored_range_falls_back_to_full
download::tests::truncate_failure_after_ignored_range
download::tests::range_edge_cases
download::tests::classify_head_table
download::tests::retries_are_bounded
download::tests::stall_and_deadline_are_named
http::tests::streamed_policy_cases
http::tests::streamed_body_cases                           (plan)
download::tests::staging_errors_are_named
download::tests::crash_after_each_step_recovers
download::tests::prune_keeps_only_live_artifacts
download::tests::same_artifact_twice_is_busy
download::tests::busy_registry_is_released_on_error
verified_artifact_has_no_public_constructor
verified_artifact_scanner_reports_plants
verify::tests::release_key_is_the_committed_key_and_not_a_test_key
no_secret_key_in_app_tree
no_secret_key_scanner_reports_plants
phase7_tests_hold_no_real_host
phase7_host_scanner_reports_plants
core_has_no_signing_crate
signing_crate_scan_reports_plants
verify_crates_are_confined
verify_confinement_scanner_reports_plants
download_and_verify_hold_no_url_or_whole_read
download_verify_scanner_reports_plants
download::tests::request_for_cases
fixture_server::tests::ranged_cases
fixture_server::tests::script_advances_per_request
fixture_server::tests::paced_delivers_all_bytes_in_chunks
download::tests::two_artifacts_reach_one_consumer
download::tests::channel_adapter_forwards_in_order
download::tests::command_flow_cases
download::tests::cached_verified_file_is_reverified
download::tests::redirect_target_never_logged
download::tests::writes_nothing_outside_downloads
download::tests::fixture_url_seam_cases
fixture_url_seam_is_test_only
fixture_url_seam_scanner_reports_plants
download::tests::staged_paths_cases
download::tests::cancellation_cases
download::tests::discard_cases
download::tests::retrying_events_are_ordered_and_serialize
download::tests::policy_has_no_overall_budget
download::tests::slow_moving_download_is_not_an_error    (comply round: SC-037 split, each test < 3 s)
too_many_arguments_is_never_suppressed                     (Q-B, SI-26)
download::tests::prune_spares_in_flight_artifact            (review round 2, SI-30)
download::tests::non_retryable_transport_errors             (review round 2, SI-30)
```

`windows` (7 → 10): `download::tests::prune_skips_a_file_open_without_share_delete_windows`,
`download::tests::verified_file_has_no_zone_identifier_windows`,
`download::tests::staged_links_are_not_followed_windows` (`#[cfg(windows)]`, SI-29). `macos` (24 → 28):
`download::tests::readonly_staging_dir_unix`, `download::tests::verified_file_has_no_quarantine_macos`,
`http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix`, `download::tests::staged_symlinks_are_not_followed`.
`linux` (22 → 25): `download::tests::readonly_staging_dir_unix`,
`http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix`, `download::tests::staged_symlinks_are_not_followed`
(`#[cfg(unix)]`, PD-19). The
The `windows`, `macos` and `linux` lists of `EXPECTED_REQUIRED` change identically (the deep compare covers the whole object). The `compile_fail` and `no_run` doctests are not listed (SC-015). No `vitest` change. Every name is unique across the workspace's test
binaries (the CI rule wants exactly one `ok` line, `manager-ci.yml:166-168`).

### 4.12 State layout

```
<state dir>/                      Roots::state_dir() (Phase 4: the OS app-data location; tests: <temp>/state)
  manager.lock                    Phase 4
  settings.json                   Phase 4
  feed-cache.json                 Phase 5
  logs/                           Phase 4
  staging/                        Roots::staging_dir()
    downloads/                    Phase 7 (DOWNLOADS_DIR)
      <sha256hex>.part            a transfer in progress or interrupted (resumable)
      <sha256hex>/<asset name>    a file that passed verify_file, renamed in from the .part
```

One verified artifact per offered plugin at most (FR-005); a prune never removes what the feed in use still offers or
what is in flight. `discard` removes one `<hex>/` after its install (Phases 9, 10).

### 4.13 The OS abstraction

Phase 7 adds no `Platform` method (PD-16). The staging root is Phase 4's per-OS state dir (Windows, macOS and Linux
`default_roots()`, `platform/{windows,macos,linux}.rs`), injected through `Roots`. What differs per OS is proven, not
abstracted: Windows share-mode semantics (SC-013) and no `Zone.Identifier` (SC-031), macOS no
`com.apple.quarantine` (SC-031), Unix permission errors (SC-012), and `/var` → `/private/var` canonical temp roots on
macOS (every test, SC-021). `http.rs`'s PAC path is already per-OS (`AgentPurpose::PacFetch` and the engine exist only
off Windows, `http.rs:621-627`, `:753-813`).

---

## 5. Dependencies

| Crate | Version | Kind | Why this one | Verified against |
|---|---|---|---|---|
| `minisign-verify` | 0.3.0 | core normal | The verifier the Tauri updater uses (one implementation for Phase 7 and Phase 11), zero dependencies, a streaming verifier, rejects non-canonical `S` | crates.io API; source 0.3.0 `src/lib.rs`; https://docs.rs/minisign-verify/0.3.0 |
| `sha2` | 0.11.1 | core normal | The streamed SHA-256 of FR-034; current major; MSRV 1.88 ≤ the workspace's 1.90 (`Cargo.toml:9`). 0.10.9 in the lock is reached only through `src-tauri`'s graph, not core's (spec D1) | crates.io API; source 0.11.1 `Cargo.toml`, `README.md`; https://docs.rs/sha2/0.11.1 |
| `minisign` | 0.10.0 | core dev | Throwaway keys and signatures in memory per test (roadmap lines 615-617); brings `scrypt`, `rpassword`, `ct-codecs`, `getrandom 0.4` (MSRV 1.85) into the dev graph only | crates.io API; source 0.10.0; https://docs.rs/minisign/0.10.0/minisign/fn.sign.html |
| `ureq` | 3.5.0 (unchanged) | core normal | The Phase 5 transport, gaining a purpose | local registry source |
| `tauri` | 2.12.5 (unchanged) | src-tauri | `ipc::Channel` | local registry source |
| `base64`, `url`, `serde`, `serde_json`, `thiserror`, `log` | unchanged | core normal | key-id decoding, the D13 seam's parse, event serialisation, errors, logs | `crates/core/Cargo.toml:15-28` |

No npm package and no Tauri plugin is added. Blake2b-512 comes from `minisign-verify`'s `StreamVerifier` (no Blake2
crate).

---

## 6. Safety design

- **Injected roots.** Phase 7 writes only under `Roots::staging_dir()/downloads` (`roots.rs:199-201`), built by
  `src-tauri`'s one `Roots::from_os()` in production and by `fresh_temp_roots()` in every test; on a sandboxed (test)
  `Roots`, `check_sandboxed` panics on a path outside the temp base (§4.4.3). `check_sandboxed` is lexical
  (`roots.rs:173-185`), so "only under `downloads/`" also needs PD-19: no write, truncate, rename or verify goes
  through a link inside `downloads/` (or a linked `downloads/` itself); a link found there is removed or refused before
  anything measures or opens it, including a `<hex>` link the prune could not remove, which step 4 refuses by
  `kind_of` (SI-29; `staged_symlinks_are_not_followed`, PENDING-CI on macOS and Linux;
  `staged_links_are_not_followed_windows`, junctions, local and `check (windows-latest)`). Links planted above
  `downloads/` (the state dir and `staging/`, Phase 4's roots) and a same-user race between the prune and the open are
  outside this phase's model: both need write access to the user's own state dir. The feed URL, `HttpConfig` (policy,
  proxy, PAC) and the release key arrive through `AppContext`; tests use `HttpConfig::loopback`, the fixture server on
  `127.0.0.1` and throwaway or KAT keys. No test acquires the real state lock, reads the real state dir, the registry,
  a VST3 or preset folder.
- **Verify before anything touches the asset.** The order is fixed in §4.3: size → signature text → key id (before
  any byte is read) → one read feeding SHA-256 and Blake2b → SHA-256 → Ed25519 data and global signatures → C3 parse
  of the now-authenticated comment → binding to name, version, platform key and asset name. Only then does a
  `VerifiedArtifact` exist; it has no public constructor (§4.3, FR-043 guard), and the function that makes one,
  `verify_file`, is `pub(crate)` together with `Expected` and `SigningKey::from_text` (SI-28), so outside core the
  only route to one is `obtain_and`, with a feed-derived `Expected` and the context's release key; the guard pins
  those call sites. `obtain_and` passes it to `next` only after the verified rename, and Phases 8 and 10 accept
  nothing else. A cached file is re-verified before every use
  (FR-046).
- **Bytes on disk are the signed bytes.** The request sends `Accept-Encoding: identity`. A `Content-Encoding` that
  ureq leaves in the head (anything but exactly `gzip`) is refused before any body byte is written
  (`E_DOWNLOAD_ENCODING`). A `gzip` coding is **not** visible: ureq removes `Content-Encoding` and `Content-Length` and
  decodes the body (`ureq-3.5.0/src/run.rs:117-125`, PD-17), so the decoded bytes are what reach the `.part`. That is
  safe because the guard is the content, not the header: `max_body` stops writing at the first decoded byte past
  `size` (`E_DOWNLOAD_SIZE`), and `verify_file` accepts the file only if its size, SHA-256 and signature match the feed
  (a CDN that gzips the signed asset yields the signed bytes and verifies; any other bytes fail `E_VERIFY_*`, SC-009
  e2-e4). Nothing is added to the file (no mode, attribute, stream or timestamp).
- **Zip-slip class.** Phase 7 extracts nothing (Phase 8's). The only feed strings that reach the filesystem are the
  SHA-256 (as 64 checked hex digits) and the asset name (one checked normal component), joined by one pure function
  that requires strict descendants of `downloads/` (§4.4.3); the prune removes only direct children of `downloads/`,
  classifies each by `DirEntry::file_type()` (no follow) and removes a link as a link (`remove_file`), never through it
  (PD-19).
- **Transport.** Every hop of every download passes `check_url` under the context policy (HTTPS-only in production,
  loopback-only in tests) and the redirect rule; the redirect target's signed query never reaches a log or an error
  (D12, PD-15, SC-030).
- **The elevated helper and the UI.** Untouched: Phase 7 elevates nothing and runs no process. The webview gains one
  command, `download_plugin(name, expected_version, on_progress)`: no URL, no path, no hash in or out (FR-062); its
  permission `allow-download-plugin` is the sixth and only addition to the capability, whose CSP and remote-content
  rules stay as they are; no cancel command exists (FR-061).
- **Keys.** The release public key is compiled in from the user-committed file and proven not to be a fixture key
  (FR-042). Test keys live in memory for one test; `minisign` is a dev-dependency whose callers are `#[cfg(test)]`
  modules only; no private key is generated into or written into the repository by any test. The FR-083 guard catches
  a committed private key in the app tree in both forms it can take: the plaintext minisign / rsign2 file and the
  one-line base64 file `tauri signer generate` writes (`probes/results.md:10`, `:27`), plus any `*.key` file and a
  `TAURI_SIGNING_PRIVATE_KEY` assignment with a literal value (SI-22). It does not scan outside `apps/krate-manager/`
  (the Phase 2 scanner walks `tools/feed-contract/`, `c3-trusted-comment.test.js:207`, `:272`), and it cannot recognise a key re-encoded in some other way.
- **Unsigned reality.** No certificate is involved; the manager adds no quarantine (SC-031 proves the verified file has
  none on macOS and no Mark-of-the-Web on Windows).

---

## 7. Test plan

All Rust tests run with cargo's default test threads (FR-087). Every network test builds `HttpConfig::loopback(SEC,
SEC)` and talks only to its own `FixtureServer` on `127.0.0.1` through D13's seam; test feeds come from
`test_support::test_feed` with `https://assets.example.invalid/…` URLs. Every download test builds its environment
through `test_support::DownloadRig` (§4.7): `let rig = DownloadRig::new(ctx); obtain_and(rig.env(), &feed, &request,
&mut record, |a| a)`, `run_command(rig.env(), …)`, `for_command(rig.env(), name, version, NOW, &host, &mut record)`;
no test spells a `DownloadEnv` literal except through struct update on `rig.env()`. Test policy unless a row says
otherwise: `test_support::test_policy()`, `DownloadPolicy { attempts: 4, backoff: vec![Duration::ZERO], stall: 2 s,
attempt_deadline: 10 s }`; a row that changes it assigns `rig.policy = DownloadPolicy { …, ..test_policy() }`. Assets are
seeded pseudo-random bytes (a small xorshift in `download_tests.rs`) of sizes that are not multiples of 64 KiB, signed
by `TestSigner::sign_c3`; the context's `signing_key` is that signer's `key()`. Errors are asserted by `code()` and
named fields.

**Shared progress check (SI-24).** `download_tests.rs` has one helper, `fn assert_progress_invariants(events:
&[Progress], size: u64, ok: bool)`, called on **every** recorded run of SC-002, SC-005 to SC-009, SC-029, SC-034 and
SC-036 (each row below that records events). It asserts FR-021's order (exactly one `Started`, first; `Verifying` at
most twice; on `ok` exactly one `Verified`, last, right after a `Verifying`; on failure no `Verified`; every `Started`
/ `Bytes` `total == size`) and FR-022's rules: `received` never decreases; with no `Restarted` in the run, `received`
strictly increases and `on_disk == received` in every `Bytes`; the first `Bytes` is `> Started.resumed_from`; after
each `Restarted`, the next `Bytes.on_disk ≤ PROGRESS_STEP`; on `ok` with at least one `Bytes`, the last has `received
== on_disk == size`; on `ok` with nothing transferred (FR-046, a complete `.part`), no `Bytes` at all. A row may add
its own exact-sequence assertions on top.

### 7.1 SC → test

| SC | Test (file) | Assertion strategy |
|---|---|---|
| SC-001 | the app lane (§7.4); `too_many_arguments_is_never_suppressed` (`injection.rs`, §4.10.1) | every command exit 0, 0 `warning:` lines, no `#[allow` / `#[expect` / `@ts-ignore` / `eslint-disable` added by this phase (`git diff -U0 2f3c93d7 -- apps/krate-manager` holds no added line with `allow(`, `expect(` in an attribute, `@ts-ignore` or `eslint-disable`), **no lint suppression at all** (Q-B): clippy with `-D warnings` and the default threshold passes over `obtain_and` (5 parameters), `run_command` (4), `for_command` (6), `command_request` (5), `discard` (2) and `get_streamed` (7); the guard reports 0 problems on the real tree and each of its five plants |
| SC-002 | `download::tests::good_artifact_verifies` (`download_tests.rs`) | 1,048,593-byte `Alpha` asset `Alpha-1.2.3-test.bin` on the native key, `Reply::Ranged`; `next` records `artifact.*`, checks inside the closure that the verified path exists and the `.part` does not; afterwards file bytes equal served bytes, `server.requests()` has one entry whose headers hold `Accept-Encoding: identity`, `Accept: application/octet-stream`, `User-Agent: krate-manager/<ver>` and no `Range`; `Obtained { resumed: false, cached: false, transferred: size }` |
| SC-003 | `download::tests::rejections_never_reach_next_stage` | a `fn rejection_rows() -> Vec<Row>` shared with SC-031; each row in a fresh temp root with its own feed and server; `next` increments `extract` (`AssetFormat::TarGz`) or `install`; assert the code (and binding `field`), both counters 0, no verified file, no `.part` (`len_of` None), no `Verified` event. Row (c) runs on the rig's counting `FaultyStagingIo` and asserts `written() == 0` and `calls_of(Op::Open) + calls_of(Op::Truncate) == 0` (the head is refused before any `.part` is created or any body byte written, `spec.md:757-758`); row (d) uses `Reply::Garbage` with the exact close-delimited head and a `FaultyStagingIo` whose `written ≤ size`; row (f) asserts the `E_VERIFY_KEY_ID` fields: `expected == ctx_signer.key().key_id_hex()`, `found == other.key().key_id_hex()`, the two different, each matching `^[0-9A-F]{16}$` (FR-033); row (g) uses `with_key_id(other.sign_c3(..), ctx_signer.key_id())`; rows (h)-(k) assert `field` and also `signed` / `expected` equal the signed and the feed value; row (m) edits line 3 after signing |
| SC-004 | `verify::tests::kat_verifies_and_one_byte_changes_fail`, `verify::tests::kat_secret_message_verifies`, `verify::tests::invalid_trusted_comment_is_named` (`verify_tests.rs`) | KAT files read in place (`CARGO_MANIFEST_DIR/../../../../tools/feed-contract/fixtures/…`); expected SHA-256 = the `kat/README.md` constant `5b980e5b…`, asserted equal to `sha256_of(archive)` first (SI-1); `Expected` with `AssetName::from_url(&AssetUrl::checked("https://assets.example.invalid/kat/<name>"))`, format `TarGz`, key `kat.pub` via `SigningKey::from_text`. Tamper copies in a `tempfile::TempDir`: archive byte flipped (→ `E_VERIFY_SHA256`; with the recomputed hash → `E_VERIFY_SIGNATURE`); signature line char 50 replaced by another base64 char (→ `E_VERIFY_SIGNATURE`); trusted comment `…:1.2.4:…` (→ `E_VERIFY_SIGNATURE`). Step 7: `TestSigner` comments `timestamp:1` → `E_VERIFY_TRUSTED_COMMENT` with `code == "E_TC_FIELDS"`; `krate:1:alpha:1.2.3:linux-x86_64:a.bin` → `"E_TC_NAME"` |
| SC-005 | `download::tests::progress_is_monotonic_and_ends_at_size` | SC-002's run recorded; the event list matched by shape: `Started { size, 0 }`, ≥ 4 `Bytes` (5 expected: 256 Ki, 512 Ki, 768 Ki, 1 Mi, size), `Verifying`, `Verified`; `received` strictly increasing, `== on_disk`, every gap but the last ≥ `PROGRESS_STEP`, last `== size`; no `Restarted` / `Retrying` |
| SC-006 | `download::tests::interrupted_download_resumes`, `download::tests::partial_from_a_previous_run_resumes`, `download::tests::stall_resumes` | `Script([Truncated { declared_len: size, sent: 400,000 }, Ranged])`: second request's `Range: bytes=400000-`, served body bytes total `size` (400,000 + `size` − 400,000), exactly one `Retrying { 1, 4, "E_DOWNLOAD_BODY", 0 }` between the `Bytes` runs, no `Restarted`, `resumed == true`; a seeded 400,000-byte correct `.part` → first request ranged, `Started { resumed_from: 400000 }`, then (PD-20) the first `Bytes.on_disk > 400000`, `on_disk == received` throughout, the last `Bytes` `received == on_disk == size`, and `Obtained.transferred == size - 400000`; `HeadersThenStall { sent: 300,000, stall: 5 s }` with `policy.stall = 200 ms` (the stall is what this case tests, so the bound stays short) → the first `Retrying` has `code == "E_DOWNLOAD_STALLED"`, the second request carries `Range: bytes=300000-`, the run ends `Ok` and verified with at most `policy.attempts` requests; it does not assert exactly one `Retrying`, because a scheduler pause on a loaded runner can add a harmless second one (§9); each < 5 s (`Instant`); `assert_progress_invariants` on all three |
| SC-007 | `download::tests::ignored_range_falls_back_to_full`, `download::tests::truncate_failure_after_ignored_range` | `Script([Truncated(400,000), Ok { body }])`: one `Retrying`, one `Restarted { RangeIgnored }`, the second request carries `Range: bytes=400000-` and its `200` body of all `size` bytes is written whole (SI-27: the limit after a `Full` verdict is `size`, not `size - 400000`), so the run ends `Ok`, not `E_DOWNLOAD_SIZE`, with `transferred == 400000 + size`, the next `Bytes.on_disk ≤ 256 Ki`, `received` non-decreasing and ending at `size`, file equals served bytes; with `FaultyStagingIo { fail: (Truncate, 1, StorageFull) }` → `E_STAGING_FULL`, `step == "truncate"`, `.part` gone, `next` not called |
| SC-008 | `download::tests::range_edge_cases`, `download::tests::classify_head_table` | (a) seeded partial + `Script([Status(416), Ranged])` → `Restarted { RangeRefused }`, verified, 2 requests; (b) the three-reply script with the exact `Garbage` 206 head → no byte of it written (`FaultyStagingIo.written` before/after), third request without `Range`; (c) complete `.part` → 0 requests, verified path, no `.part`, events exactly `Started { size, size }`, `Verifying`, `Verified` (FR-021: `Started` before verification; no `Bytes`); (d) `size + 10` partial → `PartialTooLong`, 1 request; (e) wrong-prefix partial → `HashMismatchAfterResume`, 1 full request (no `Range`), `received` non-decreasing; (f) full request serves wrong bytes too → `E_VERIFY_SHA256`, 2 requests total. The table test enumerates every row of §4.4.6 plus `gzip`, `br`, `deflate`, `zstd`, `x-gzip` → `E_DOWNLOAD_ENCODING`, `IDENTITY` and ` identity ` accepted, all without a socket; `assert_progress_invariants` on (a) to (f) |
| SC-009 | `download::tests::retries_are_bounded`, `download::tests::stall_and_deadline_are_named` | (e) as rewritten by SI-18, four end-to-end rows against the fixture server, each with no `Retrying`, nothing at the verified path unless stated, and `next` called only in e2: (e1) `Reply::Garbage` with the exact head `HTTP/1.1 200 OK`, `Content-Length: <size>`, `Content-Encoding: br`, `Connection: close` and the asset bytes → 1 request, `E_DOWNLOAD_ENCODING` (`encoding == "br"`), `FaultyStagingIo.written == 0`, no `.part`; (e2) `Reply::Gzip { body: gzip(asset) }` (`flate2`, core's dev-dependency, as `http_tests.rs:110-114`) → `Ok`, verified, 1 request, file bytes equal the asset; (e3) `Reply::Gzip { body: gzip(other bytes of length size) }` → 1 request, `E_VERIFY_SHA256`, no `.part`; (e4) `Reply::Gzip { body: gzip(size + 1 bytes) }` → 1 request, `E_DOWNLOAD_SIZE`, `written ≤ size`, no `.part`. Request counts from `server.requests().len()`; `Retrying` lists compared field by field; the `backoff = [50 ms]` case asserts `elapsed ≥ 150 ms`; each `Retrying` recorded with the server's request count at that moment (the closure reads `server.requests().len()`) to prove it precedes the next request; (f) `stall 100 ms`, deadline 2 s → `E_DOWNLOAD_STALLED`, 4 requests; (g) `stall 2 s`, deadline 300 ms → `E_DOWNLOAD_TIMEOUT`, 4 requests; each < 3 s; `assert_progress_invariants` on every row |
| SC-009 / FR-016 / D5 (SI-30) | `download::tests::non_retryable_transport_errors` | D5's "not retried: a URL or redirect refusal, TLS" at the download level (`spec.md:214-215`), each row a fresh rig and server, through D13's seam, each asserting the code, exactly 1 request on the asset route, no `Retrying`, no `.part`, `next` not called, and the error's `Display` naming the feed URL: (1) the asset route answers `Redirect { 302, "https://example.invalid/x" }` → `check_url` refuses the hop under the loopback policy → `E_DOWNLOAD_URL`; (2) the asset route starts a chain `/r1` … `/r6` of `302`s (one more hop than `MAX_REDIRECTS`, `http.rs:22-24`) → `E_DOWNLOAD_REDIRECT`, each chain route requested exactly once; (3) the asset route answers `Redirect { 302, "https://127.0.0.1:<port>/x" }` on the same fixture server, whose plain-HTTP answer to a TLS hello is the `E_FEED_TLS` of `http_tests.rs:1277-1283` → `E_DOWNLOAD_TLS` (the count is taken on the asset route by path, whatever the server records for the TLS hello). A download that retried any of these would show 4 asset-route requests and 3 `Retrying` events |
| SC-010 | `http::tests::streamed_policy_cases`, `http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix` (`http_tests.rs`) | `get_streamed` under loopback refuses `https://example.invalid/x` with 0 server requests; a redirect to `https://example.invalid/` → `E_FEED_URL`; a 6-hop redirect chain → `E_FEED_REDIRECT`; `agent_config(&cfg_3600, None, AgentPurpose::Download { stall: 30 s })`: `accept_encoding` / `accept` values, `timeout_per_read() == Some(30 s)`, `timeouts().global == Some(3600 s)`, `PlatformVerifier`, `max_redirects() == 0`, and `https_only()` true under `HttpConfig::production()` (the SI-4 c row); the `Feed` agent unchanged (`timeout_per_read() == None`, `application/json`). The `_unix` case: loopback config `total_timeout = 300 ms`, `with_pac(PacSource { url: Some(server.url("/pac")), .. })` on `Stall(5 s)`, `limits.deadline = 10 s` → returns in < 2 s |
| (plan) | `http::tests::streamed_body_cases` | FR-010: a `Reply::Gzip` response reaches `on_head` with `content_encoding == None` and `content_length == None` and streams the **decoded** bytes (pins PD-17's ureq behaviour: an upgrade that stops stripping the header turns this red, and the plan's gzip rows are then re-examined); a `Content-Encoding: br` response reaches `on_head` with `content_encoding == Some("br")`; a 206 reaches `on_head` and streams; `Stop` → `HeadStopped` with no body bytes passed to `sink`; a `sink` error → `Sink { kind }` and no further `sink` call; the limit comes from `on_head` (SI-27): a request with `Range: bytes=1000-` to a route that ignores it (`Reply::Ok` of `n` bytes) and an `on_head` returning `Read { max_body: n }` streams all `n` bytes and returns `StreamEnd { bytes: n }` (the `Full`-after-ranged case), while the same request with `Read { max_body: n - 1000 }` ends `Http(TooLarge { max: n - 1000 })` with exactly `n - 1000` bytes passed to `sink`; a body `PerRead` → `Stalled`; `on_head` called once, before the first `sink` call |
| SC-011 | `download::tests::staging_errors_are_named`, `download::tests::crash_after_each_step_recovers` | `FaultyStagingIo` rows: `(Write, 3, StorageFull)`, `(Sync, 1, StorageFull)`, `(Rename, 1, StorageFull)`, `(Write, 1, QuotaExceeded)`, `(CreateDir, 1, PermissionDenied)`, `(Open, 1, PermissionDenied)` → code + `step`; `.part` gone after write / sync, kept after rename, and the rename case's second `obtain_and` with `DownloadEnv { io: &FsStagingIo, ..rig.env() }` makes 0 requests; each row's rig sets `rig.io = FaultyStagingIo::failing(op, n, kind)`. Crash: N = `calls` of a clean SC-002 run; for k in 1..=N, and again over SC-007's route, `catch_unwind(AssertUnwindSafe(..))` around an `obtain_and` on a fresh rig with `rig.io = FaultyStagingIo::panicking_after(k)` (N read from `rig.io.calls()`); then not `E_DOWNLOAD_BUSY`, the verified path empty or `verify_file` `Ok`, and a healthy rerun against `Ranged` ends verified with the served bytes |
| SC-012 | `download::tests::readonly_staging_dir_unix` (`#[cfg(unix)]`) | probe file's `MetadataExt::uid() == 0` → `panic!("cannot prove a permission error as root")`; `downloads/` at `0o500` → `E_STAGING_DENIED`, `step == "open"`, nothing written, `next` not called; mode restored to `0o700` before the `TempDir` drops |
| SC-013 | `download::tests::prune_skips_a_file_open_without_share_delete_windows` (`#[cfg(windows)]`) | stale `downloads/<other-hex>.part` opened with `OpenOptionsExt::share_mode(0x1)` (`FILE_SHARE_READ`; core has no `windows-sys`, so the documented value); `obtain_and` `Ok`; file still there; `take_mine()` holds one `warn!` with `E_STAGING_IO` and `remove`; after the handle drops, a second `obtain_and` removes it |
| SC-014 | `download::tests::prune_keeps_only_live_artifacts`, `download::tests::same_artifact_twice_is_busy`, `download::tests::busy_registry_is_released_on_error` | seeded tree, then a full listing compared with the expected set; the busy test uses `thread::scope`, two `mpsc` channels (`ready`, `go`) inside A's progress closure on `Started`, A and B each call `rig.env()` inside their own scoped thread over one shared `&rig` (§4.7); B's call must be `E_DOWNLOAD_BUSY` with an empty event list and `server.requests().is_empty()`; two different artifacts held at `Started` then released both `Ok`; a failing `obtain_and` (route `Status(404)`) followed by the same request on `Ranged` → `Ok` |
| SC-014 / FR-005 in-flight exception (SI-30, PD-12) | `download::tests::prune_spares_in_flight_artifact` | feeds F1 (offers X) and F2 (offers Y only), one root, one server. Thread A (`thread::scope`) obtains X under F1 and is held, through the `ready` / `go` channels, inside its progress closure at its **first `Bytes`** event, so `downloads/<hexX>.part` exists with at least one chunk on disk. The test thread then runs `obtain_and` for Y under F2, whose prune sees X as not live: it must end `Ok`, and afterwards `<hexX>.part` still exists with a length ≥ A's reported `on_disk`. After `go`, A ends `Ok` and verified with its file equal to the served bytes. Control (non-vacuity): once A has returned, a third `obtain_and` for Y under F2 (the cached path, 0 requests) removes `<hexX>/`, so the earlier survival was the registry's doing, not a prune that skips everything. A prune that ignored the registry deletes A's `.part` under it and A ends with a staging or verify error |
| SC-015 | `compile_fail` doctests on `VerifiedArtifact` (`verify.rs`); `verified_artifact_has_no_public_constructor`, `verified_artifact_scanner_reports_plants` (`injection.rs`) | the two `compile_fail` doctests fail to compile and the `no_run` twin compiles (local evidence only); the struct literal names all eight fields with valid values, so privacy is its only possible failure (§4.3); guard 0 findings, every plant reported |
| SC-016 | `verify::tests::release_key_is_the_committed_key_and_not_a_test_key` | reads `keys/krate-signing.pub` at test time (`fs::read_to_string`) as `file`; **on the parsed key:** `SigningKey::release() == SigningKey::from_text(&file)?` (the derived `PartialEq` of FR-040 over `PublicKey` and the key id, so this compares what `release()` actually parsed, not the `include_str!` text), and `SigningKey::release() != SigningKey::from_text(<kat.pub text>)?` and `!= SigningKey::from_text(<phase2/kat-secret/kat.pub text>)?`; `key_id_hex() == "9FEB36F9114273BC"` = the id in the file's untrusted comment, and differs from `440FA75168B852F7` and `63861317F7D60239`; **on the text (FR-042's file check):** line 2 of `RELEASE_KEY_TEXT` decodes to the same 42 bytes as line 2 of `file`, and its bytes 10..42 differ from both KAT keys' 32-byte keys |
| SC-017 | `no_secret_key_in_app_tree`, `no_secret_key_scanner_reports_plants`, `phase7_tests_hold_no_real_host`, `phase7_host_scanner_reports_plants` | 0 findings; every plant reported |
| SC-018 | `core_dependencies_are_the_fr007_set` (amended), `core_has_no_signing_crate`, `signing_crate_scan_reports_plants`, `verify_crates_are_confined`, `verify_confinement_scanner_reports_plants`, `download_and_verify_hold_no_url_or_whole_read`, `download_verify_scanner_reports_plants`, `roster_and_hosts_absent_from_sources`, `install_identifiers_absent_from_sources` | 0 findings each; plants reported; the CI `core dependency tree` notice `none excluded` |
| SC-019 | `real_src_tauri_config_passes_the_guard`, `planted_config_changes_fail_the_guard` (`config_guard.rs`, amended §4.10.2); `download::tests::request_for_cases` | six commands / permissions / handlers, no `cancel_download`; request rows with `v1-only.json` and `mixed.json` read in place (`include_bytes!`), a non-native key only, a `min_os` above `FakeHostFacts`' version, a native entry (request fields equal the entry's) |
| SC-020 | `fixture_server::tests::ranged_cases`, `script_advances_per_request`, `paced_delivers_all_bytes_in_chunks` (`fixture_server_tests.rs`) | raw-socket requests with the file's existing helpers (`send`, `read_all`, `parse`); `Paced` read with timestamps per chunk |
| SC-021 / SC-022 / SC-023 | the CI legs (§7.3) | — |
| SC-024 | the local `cargo test` log | sum of the `finished in` lines of the core lib test binary and `injection` / `config_guard` before (Phase 6 log in `specs/krate-manager-phase6/evidence/`) and after; the delta < 20 s, recorded in compliance |
| SC-025 | `git diff --name-only 2f3c93d7..HEAD` (plus untracked) | only FR-090 / FR-091 paths; and, for FR-063 (FR-091 admits all of `apps/krate-manager/**`, so the scope rule alone cannot see a TypeScript change), `git diff --name-only 2f3c93d7 -- apps/krate-manager/src` and `git ls-files --others --exclude-standard -- apps/krate-manager/src` are both empty |
| SC-026 | `download::tests::two_artifacts_reach_one_consumer` | one feed with `Alpha` and `Bravo`; both `obtain_and(.., \|a\| a)`; then `fn batch(items: &[VerifiedArtifact]) -> usize` called once; per item `path()` exists, equals `downloads/<hex>/<asset>`, `sha256_of(fs::read(path)) == *sha256()` |
| SC-027 | `download::tests::channel_adapter_forwards_in_order` | recording closure vs `channel_progress(send)` sequences equal; `send` always `Err(())` → still verified, call count = event count; `send` failing only on call 2 → later events delivered |
| SC-028 | `download::tests::command_flow_cases` | context `feed_url = server.url("/versions.json")` in a `DownloadRig`; `for_command(rig.env(), …)` and `run_command(rig.env(), …)`, `command_request(&rig.ctx, …)`; `FakeHostFacts { os_version: Some(<satisfying>) }`; rows (a), (b), (c), (e), (d) as the spec; (e) pre-seeds a stale `.part` and a live verified directory and asserts both survive, no asset request, no event; (d) `serde_json::to_value(summary)` key set and the value scan (`/`, `\`, `://`, a 64-hex run); (f) (SI-30, FR-060's "nothing else") `for_command(rig.env(), <good name>, <equal expected_version>, NOW, &host, &mut record)` on a good entry with a pre-seeded stale `downloads/<other-hex>.part`: the request is built from the feed, so it fetches the feed's `https://assets.example.invalid/…` URL (no D13 seam on this path), which the loopback policy refuses → `E_DOWNLOAD_URL`, with the feed request recorded, 0 asset-route requests, exactly one `Started` event received, no `Retrying`, and the stale `.part` gone. Only `run_command` → `obtain_and` emits `Started` and prunes, so the row proves the call reached it with the env it was given |
| SC-029 | `download::tests::cached_verified_file_is_reverified` | (a) second run: 0 new requests, events exactly `Started { size, size }`, `Verifying`, `Verified`, `cached: true`; (b) flipped byte → `<hex>/` removed, one new request without `Range`, `Restarted { CachedInvalid }` present (SI-9), `next` called once with the served bytes; `assert_progress_invariants` on both (a: no `Bytes`; b: after the `Restarted`, `on_disk` restarts ≤ `PROGRESS_STEP` and the last `Bytes` reaches `size`) |
| SC-030 | `download::tests::redirect_target_never_logged` | `install_capture()`; route `/<asset>` → `Redirect { 302, "/cdn/a.bin?token=SECRET-TOKEN" }`; the target route serves (a) `Ranged`, (b) `Status(500)`, (c) a same-key-id foreign signature, (d) `Truncated { declared_len: size, sent: 1000 }` on every request (→ `E_DOWNLOAD_BODY` after 4 requests on the `/cdn/…` route: a `detail` that comes from the transport behind the redirect, which (b) and (c) never produce); `take_mine()` and each error `Display` contain neither `SECRET-TOKEN` nor `/cdn/`; (b)/(c)/(d) `warn!` and `Display` contain `https://assets.example.invalid/`. **FR-053 counts and fields:** per run, records at target `krate_manager_core::download` number exactly one `info!` and zero `warn!` for (a), and exactly one `warn!` and zero `info!` for (b), (c), (d); (a)'s `info!` line holds the plugin name, the version, `PlatformKey::as_str()`, the byte count `transferred`, and `resumed=false`, `cached=false`. A socket-less row: `scrub("io: /cdn/a.bin?token=SECRET-TOKEN failed at https://x.invalid/y")` holds neither `SECRET-TOKEN`, `/cdn/` nor `://`, and `scrub("connection reset (os error 10054)")` is unchanged (PD-15) |
| SC-031 | `download::tests::writes_nothing_outside_downloads`, `verified_file_has_no_quarantine_macos` (`#[cfg(target_os = "macos")]`), `verified_file_has_no_zone_identifier_windows` (`#[cfg(windows)]`) | tree snapshot (path, kind, length, SHA-256 via `sha256_of`) before and after SC-002's run and each `rejection_rows()` row; every new or changed path under `<state>/staging/downloads/`, nothing removed outside; `Command::new("/usr/bin/xattr").arg("-l")` exits 0 and no line starts `com.apple.quarantine`; `File::open("<file>:Zone.Identifier")` fails `NotFound` |
| SC-031 / FR-050 (SI-23) | `download::tests::staged_symlinks_are_not_followed` (`#[cfg(unix)]`, `download_tests.rs`) | in a `fresh_temp_roots()` base: `<base>/outside/f` (2 × `size` seeded bytes) and `<base>/outside/d/` (one file); `std::os::unix::fs::symlink` plants `downloads/<hex>.part -> outside/f` and `downloads/<hex> -> outside/d` for the live artifact's `<hex>`; `obtain_and` over `Ranged` → `Ok`, verified, 1 request without `Range` (the link was not taken as a partial); afterwards `outside/` is byte-identical to its snapshot (path, kind, length, `sha256_of`), both links are gone (`symlink_metadata` of each path is a regular directory / `NotFound`), and the artifact path's `symlink_metadata` is a regular file whose `canonicalize()` starts with the canonical `downloads/`. Second case: `staging/downloads -> outside/dl/` → `E_STAGING_IO`, `step == "create_dir"`, `outside/dl/` still empty, 0 requests. Third case (socket-less): `verify_file` on a link to a correct file → `E_VERIFY_IO { kind: InvalidInput }`. Fourth case (SI-29, the step-4 refusal): `downloads/<hex> -> outside/d/` where `outside/d/<asset>` holds the **correct signed bytes**, with `rig.io = FaultyStagingIo::failing(RemoveFile, 1, PermissionDenied)` so the prune cannot remove the link → `E_STAGING_IO`, `step == "open"`, `rig.io.calls_of(Op::LenOf) == 0` (nothing under `<hex>` measured), 0 requests, `next` not called, no event, `outside/` byte-identical (the outside file neither opened for write nor removed). `assert_progress_invariants` on the first run |
| SC-031 / FR-050 Windows (SI-29) | `download::tests::staged_links_are_not_followed_windows` (`#[cfg(windows)]`, `download_tests.rs`) | the Unix test's layout, with NTFS junctions made by `Command::new("cmd").args(["/C", "mklink", "/J"])` on plain drive paths (the `\\?\` prefix stripped), exactly as `detect_tests.rs:2578-2594` does, and asserting `made.status.success()` (a junction needs no `SeCreateSymbolicLinkPrivilege`, so the case cannot skip): (1) junctions `downloads/<hex>.part -> outside/p/` and `downloads/<hex> -> outside/d/` (holding a correct `<asset>`); `obtain_and` over `Ranged` → `Ok`, verified, 1 request without `Range`; both junctions gone (the prune's `remove_file` fails on a directory link and its `remove_dir_all` retry removes the link, §4.4.5), `outside/` byte-identical, the artifact path's `symlink_metadata` a regular file whose `canonicalize()` starts with the canonical `downloads/`; (2) the step-4 refusal: the `<hex>` junction only, `rig.io = FaultyStagingIo::failing(RemoveDir, 1, PermissionDenied)` (the real `remove_file` fails on the junction, the injected fault fails the retry) → `E_STAGING_IO`, `step == "open"`, `calls_of(Op::LenOf) == 0`, 0 requests, `next` not called, `outside/d/<asset>` unchanged and present; (3) `staging/downloads` itself a junction to `outside/dl/` → `E_STAGING_IO`, `step == "create_dir"`, `outside/dl/` still empty, 0 requests. Every junction left is removed with `fs::remove_dir` before the `TempDir` drops. `assert_progress_invariants` on (1) |
| SC-032 | `download::tests::fixture_url_seam_cases`; `fixture_url_seam_is_test_only`, `fixture_url_seam_scanner_reports_plants` | accepted / refused URLs as the spec lists; then SC-030 proves logs still name the feed URL |
| SC-033 | `download::tests::staged_paths_cases` | the valid triple; each bad `asset` / `hex`; `#[cfg(windows)]` adds `C:x`, `C:\x`; `downloads` listed empty after |
| SC-034 | `download::tests::cancellation_cases` | (a) `rig.cancel.cancel()` before the call: 0 requests, no `.part`, empty event list; (b) set on the 2nd `Bytes`: `E_DOWNLOAD_CANCELLED` within 2 s, 1 request, no `Retrying` / `Verifying` / `Verified`, `.part` length in `1..size` with the served prefix, registry released; (c) a fresh token (`DownloadEnv { cancel: &CancelToken::new(), ..rig.env() }`) resumes at the `.part` length: first request `Range: bytes=<len>-`, `Started { resumed_from: len }`, first `Bytes.on_disk > len`, `on_disk == received` throughout, the last `Bytes` `received == on_disk == size`, verified (PD-20); `assert_progress_invariants` on (a) to (d); (d) `Script([Status(503), Ranged])`, `backoff = [5 s]`, a scoped thread holding `&rig.cancel` cancels 100 ms after the first request (it polls `server.requests().len()`) → cancelled in < 1 s, 1 request, the `Retrying` already present; (e) `get_streamed` with a sink that cancels → `StreamError::Cancelled`, one `sink` call; (f) covered by SC-028 d and SC-019 |
| SC-035 | `download::tests::discard_cases` | (a) discard → `<hex>/` gone, next `obtain_and` 1 request, `cached: false`; (b) Alpha discarded, Bravo's file still verifies, a stale `.part` untouched; (c) already removed → `Ok`; (d) `discard(artifact, &FaultyStagingIo::failing(RemoveDir, 1, PermissionDenied))` → `E_STAGING_DENIED` `remove`, `StorageFull` → `E_STAGING_FULL`; (e) SC-014 and SC-026 re-run unchanged; (f) (SI-20, PD-18) with `thread::scope` and SC-014's `ready` / `go` channels, an `obtain_and` B of artifact X is held at `Started` while `discard` is called on a `VerifiedArtifact` of the same hash from an earlier run → `E_DOWNLOAD_BUSY` (`DiscardBusy`), `<hex>/` still present; after `go`, B ends `Ok` with its artifact path existing; then `discard` of that artifact → `Ok`, `<hex>/` gone; a `discard` of a *different* hash while B is held → `Ok` (only the same hash is refused) |
| SC-036 | `download::tests::retrying_events_are_ordered_and_serialize` | the `[10, 20, 40] ms` run's exact event list; the stall route's `Retrying.code == "E_DOWNLOAD_STALLED"`; the per-event server request count `== attempt`; `serde_json::to_value` key sets; the union of `kind` values ⊆ the six |
| SC-037 | `download::tests::policy_has_no_overall_budget` | the full struct literal equals `production()` (this one test spells the production policy; every other row uses `rig.policy`); SC-009 g's route with `backoff [100 ms]` → 4 requests, `elapsed ≥ 1.5 s`; `Reply::Paced` (10 chunks, 150 ms apart, **`stall = 1 s`**, `attempt_deadline = 5 s`; SI-19 as revised in review round 2: the transfer takes ≥ 1.35 s, longer than the stall bound, while each 150 ms gap uses about 15 % of it, so a scheduler pause on a 3-4 core hosted runner has 850 ms of slack instead of 200 ms) → `Ok`, verified, at most `policy.attempts` requests, and the measured `elapsed > policy.stall` (and ≥ 1.35 s). `Paced` answers every request, ranged or not, with the full `200` body (§4.8), so `Ok` can only come from one response that delivered all ten chunks in longer than the stall bound: an implementation that applied `stall` to the whole transfer (`timeout_global(stall)` instead of `timeout_per_read(stall)`) cuts every attempt at 1 s and ends `E_DOWNLOAD_TIMEOUT` / `E_DOWNLOAD_STALLED`, failing the row whatever its retries do. A `Retrying` is printed for the log, not asserted against (a retry here is harmless and is the flake the reviewer named, §9); each < 3 s (expected ≈ 1.4 s, under 60 % of the bar, the project's wall-clock rule) |
| (plan) | `verify::tests::verify_steps_stop_at_first_failure` | FR-031 to FR-036 per step with a hand-built `Expected` over a `TestSigner` file: a size mismatch is reported before the signature text is parsed (a malformed signature in the same `Expected` does not change the code); a key-id mismatch before any read (a 0-length file of the right `size` metadata is not needed: the code proves the order); a legacy `Ed` signature text → `E_VERIFY_SIGNATURE_TEXT`; FR-034's I/O outcome: `verify_file` on a path that does not exist → `E_VERIFY_IO { kind: NotFound }`; FR-038's comparison order (each SC-003 row h-k differs in one field only, so these rows pin the order): a signature bound to another name **and** another version → `E_TC_BINDING { field: "name" }`; another version and another asset → `field: "version"`; another platform and another asset → `field: "platform"`; each also asserts `signed` and `expected` of the reported field |
| (plan) | `verify::tests::signing_key_text_cases` | FR-040: `from_text` of empty, one-line, bad base64, 41-byte texts → `E_VERIFY_KEY_TEXT`; CRLF text accepted; FR-040's equality: `from_text(lf) == from_text(crlf)` of the same key, `from_text(kat.pub) == from_text(kat.pub)` from two separate reads, and `from_text(kat.pub) != from_text(phase2/kat-secret/kat.pub)`; `key_id_hex()` of `kat.pub` = `440FA75168B852F7` |

### 7.2 FRs without their own SC

| FR | Covered by |
|---|---|
| FR-002 (no URL literal, no URL built from parts) | `download_and_verify_hold_no_url_or_whole_read`; review of `download.rs` |
| FR-004 (no attribute, mode, timestamp) | SC-031; `FsStagingIo` sets none (review) |
| FR-012 (`run_chain` keeps 200-only for feed / PAC) | every Phase 5 `http::tests` case (unchanged, in `cargo.all`) |
| FR-005 (in-flight entries spared by the prune) | `download::tests::prune_spares_in_flight_artifact` (SC-014 row; SI-30) |
| FR-016 (mapping table) | SC-003, SC-009, SC-030, `classify_head_table`, `streamed_body_cases`; `E_DOWNLOAD_URL`, `E_DOWNLOAD_REDIRECT` and `E_DOWNLOAD_TLS` (and D5's "not retried" for them) by `download::tests::non_retryable_transport_errors` (SI-30); `E_DOWNLOAD_URL` through the command path also by SC-028 (f) |
| FR-060 (`for_command` = `command_request` then `run_command`) | SC-028 (a)-(c), (e) (stops in `command_request`) and (f) (reaches `run_command`, SI-30) |
| FR-018 (no overall bound) | SC-037 |
| FR-048 (downgrade) | SC-003 i |
| FR-053 (one `info!` / one `warn!`) | SC-030 (its §7.1 row asserts the counts per run and the `info!` fields) |
| FR-034 (I/O error → `E_VERIFY_IO { kind }`) | `verify::tests::verify_steps_stop_at_first_failure` (missing path → `NotFound`); `staged_symlinks_are_not_followed` (a link → `InvalidInput`) |
| FR-034 ("a file that changed length under the read → `E_VERIFY_SIZE`") | **review only**: the step 4 total is compared with `size` after the loop (§4.3), but producing the condition needs a writer racing the read between step 1's length check and step 4, which no deterministic test can schedule without a hook in `verify_file` that FR-082's single-read rule leaves no room for |
| FR-040 (equal when decoded from the same text) | `verify::tests::signing_key_text_cases`; `release_key_is_the_committed_key_and_not_a_test_key` (`release() == from_text(file)`) |
| FR-041 (`reassemble` keeps the previous value) | `context::tests::reassemble_cases` (already in `cargo.all`) gains `assert_eq!(reassemble_with_os(..).signing_key, ctx.signing_key)` for its success rows, whose contexts carry `kat_signing_key()` (PD-14), so a `reassemble` that substituted `SigningKey::release()` or any other key would differ |
| FR-063 (no TS change) | SC-025's `apps/krate-manager/src` check (tracked diff and untracked files both empty; §7.1, §7.4 step 5) |
| FR-066 (permanence) | SC-019 (the six-command contract) |

### 7.3 PENDING-CI rows (exact job and assertion)

Each closes on a leg only when that run's committed `tests/required-tests.json` lists the names, the leg's
`required tests ran` step passed with no `::error title=required tests ran (<os>)::…` line (`manager-ci.yml:153-178`)
and its `::notice` counts equal `223 + <os list>` cargo (233 windows, 251 macos, 248 linux) and the unchanged vitest
count.

| Row | Job | Assertion |
|---|---|---|
| SC-001 macOS / Linux / Windows | `check (macos-latest)`, `check (ubuntu-24.04)`, `check (windows-latest)` | `cargo fmt`, both clippy steps, the clippy-guard step (`rc≠0`, `disallowed=3`, `other-errors=0`), `cargo test`, `typecheck`, `lint`, `vitest`, `tauri build` and `build warnings` (`warnings=0`) green |
| SC-012 | `check (macos-latest)`, `check (ubuntu-24.04)` | `test download::tests::readonly_staging_dir_unix ... ok` once (`macos`, `linux` lists) |
| SC-010 `_unix` | same two | `test http::tests::streamed_pac_fetch_keeps_the_feed_bound_unix ... ok` once |
| SC-013, SC-031 Windows | `check (windows-latest)` | `prune_skips_a_file_open_without_share_delete_windows`, `verified_file_has_no_zone_identifier_windows` once each (`windows` list) |
| SC-031 macOS | `check (macos-latest)` | `verified_file_has_no_quarantine_macos` once (`macos` list) |
| PD-19 / SI-23 (no link followed in staging) | `check (macos-latest)`, `check (ubuntu-24.04)` | `test download::tests::staged_symlinks_are_not_followed ... ok` once (`macos`, `linux` lists): `outside/` byte-identical, both links gone, the artifact a regular file under the canonical `downloads/`, the linked `downloads/` refused at `create_dir`, the unremovable `<hex>` link refused at step 4 with `calls_of(LenOf) == 0` |
| PD-19 / SI-29 Windows | `check (windows-latest)` (also run locally on this box, §7.4 step 3) | `test download::tests::staged_links_are_not_followed_windows ... ok` once (`windows` list): junctions removed by the `remove_dir_all` retry, `outside/` byte-identical, the unremovable `<hex>` junction refused at step 4, the junctioned `downloads/` refused at `create_dir` |
| SC-018 tree | all three | `::notice title=core dependency tree (<os>)::<n> crates, none excluded` |
| SC-021 / SC-022 / SC-023 | the three legs | every `cargo.all` name plus the leg's list passes exactly once on APFS (`/var/folders/…` temp roots, so every path check runs under the `/var` → `/private/var` link), ext4 and NTFS |

### 7.4 Local close procedure (Windows, this box)

From `apps/krate-manager/`, logs to `specs/krate-manager-phase7/evidence/`:

1. `git status --porcelain > evidence/porcelain-before.txt`; the §4.1 lock procedure first (one-time).
2. Concurrently (the box has 32 cores; cargo saturates them, the rest takes seconds):
   - app lane, sequential within itself because it shares `target/`: `cargo fmt --all --check`; `cargo clippy
     --workspace --all-targets --locked -- -D warnings`; `cargo clippy -p krate-manager --all-targets --features
     dev-feed --locked -- -D warnings`; the clippy-guard command of `manager-ci.yml:102`; `cargo test --workspace
     --locked 2>&1 | tee evidence/cargo-test.txt` (default threads); `cargo tree -p krate-manager-core -e normal
     --locked --prefix none --format "{p}" > evidence/core-tree.txt`; `npm run tauri build 2>&1 | tee
     evidence/tauri-build.txt`.
   - frontend: `npm run typecheck`, `npm run lint`, `npm test -- --reporter=default --reporter=json
     --outputFile.json=evidence/vitest.json`.
   - pipeline lane (repo root): `node --test "tools/feed-contract/tests/*.test.js"`; `node
     tools/lint-manager-version.js`.
3. The required-tests rule over `evidence/cargo-test.txt` and `evidence/vitest.json` for `windows` (the node snippet of
   `manager-ci.yml:155-177`, run by hand): 233 cargo names, each exactly once (`staged_links_are_not_followed_windows`
among them: Windows junction evidence is local as well as `check (windows-latest)`).
4. SC-024: the per-binary `finished in` lines against Phase 6's log.
5. `node tools/gen-specs-index.js`, then `--check`; `git diff --name-only` against FR-090 / FR-091; FR-063:
   `git diff --name-only 2f3c93d7 -- apps/krate-manager/src` and `git ls-files --others --exclude-standard --
   apps/krate-manager/src` both empty; SC-001's
   suppression check, `git diff -U0 2f3c93d7 -- apps/krate-manager > evidence/diff-u0.txt` searched for added lines
   holding `allow(` / `expect(` in an attribute, `@ts-ignore` or `eslint-disable` (expected: none; Q-B).
6. `git status --porcelain` minus `evidence/` equals step 1 plus the intended changes; `node
   tools/clean-test-output.js --apply` for scratch output.

---

## 8. Build integration and gate lanes

- **Workspace:** members unchanged (`crates/core`, `crates/helper`, `src-tauri`); three new core source files plus
  their two test files; dependencies per §4.1; two lockfiles re-locked additively.
- **`package.json`:** unchanged (no script, no package).
- **Workflows:** none changed (FR-090). `manager-ci.yml` already runs every lane Phase 7 needs on all three OSes; its
  path filter includes `apps/krate-manager/**` and `tools/feed-contract/**` (`manager-ci.yml:5-17`). `feed-tools.yml`'s
  TAP plan count is unchanged (only `EXPECTED_REQUIRED`'s data changes, no new `node:test` case).
- **Lanes:** app lane + frontend + pipeline lane concurrently (§7.4). No C++ change, so no `run-gate.js` /
  `header-consumers.js` lane; `git diff --name-only` at close must show nothing under `plugins/`, `dsp/`, `cmake/`.
- **L-WSL (optional early signal):** inside WSL Ubuntu in its own target dir, beside the Windows lane, sequential
  within itself: `cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings` (catches a `cfg(unix)`
  compile or lint error in `readonly_staging_dir_unix` and `streamed_pac_fetch_keeps_the_feed_bound_unix` before the
  push), then `cargo test -p krate-manager-core download:: verify:: http::tests::streamed fixture_server::`. A red is
  fixed before pushing; a green closes nothing (rows stay PENDING-CI). macOS has no local compile.
- **Test-time cost:** debug-profile hashing of the ~1 MiB test assets (SHA-256 and Blake2b in `verify_file`, Blake2b
  again in `minisign::sign`) is the only new hot spot. If SC-024's measured delta exceeds 20 s, the remedy is
  `[profile.dev.package.sha2]`, `[profile.dev.package.minisign-verify]` and `[profile.dev.package.minisign] opt-level
  = 3` in the root `Cargo.toml` (dev and test profiles only, no release effect), recorded with before/after timings;
  never smaller assets than the spec names.
- **CI cost:** the first build on each leg compiles `sha2 0.11`, `minisign-verify`, and for tests `minisign`,
  `scrypt`, `rpassword`; `Swatinem/rust-cache@v2` caches them after the first green run.

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| WebKitGTK quirks on Linux | No UI in Phase 7. The `Channel` carries at most one `Bytes` event per 256 KiB (about 240 for a 60 MB installer) plus a handful of others, all small JSON; the Linux leg builds the app (`tauri build`). Channel delivery on WebKitGTK is exercised for real only when Phase 9 / 12 add a caller. |
| Windows file locks while a DAW runs | Not reachable: Phase 7 writes only under staging; a DAW maps plug-in DLLs, never staged files. An antivirus or indexer holding a staged file can make the prune skip it (SC-013, one `warn!`) or the final rename fail (`E_STAGING_IO`, step `rename`, the complete `.part` kept and re-verified without a request next time). Defender deleting a flagged unsigned `.exe` after the write surfaces as `E_VERIFY_IO` or `E_STAGING_*`, never as a verified file; the first-launch docs (Phase 12) cover Defender. |
| Admin prompts | None: Phase 7 elevates nothing and the staging dir is per-user. |
| Offline | `feed::check` serves the cached feed (Phase 5); a download from it fails `E_DOWNLOAD_OFFLINE` after 4 attempts. Worst case with an unreachable route: 4 × 10 s connect bound + 1 + 2 + 4 s backoff ≈ 47 s; a DNS failure is immediate. With no cancel command in Phase 7 and no UI caller, nothing waits on it yet; Phase 12 sets the token. |
| A slow but moving link | No overall bound (Q5): only the per-read stall (30 s) and per-attempt deadline (3600 s) apply; the `CancelToken` is the stop (FR-018). |
| The release CDN's signed redirect expires between attempts | Every attempt starts at the feed's per-tag URL and follows a fresh redirect (spec Edge cases). |
| A CDN that formats `Content-Range` differently | The exact comparison sends it to `RestartFull`: one full request, never a wrong splice; the SHA-256 is the last line of defence. |
| `ureq` transparent gzip | ureq 3.5.0 removes `Content-Encoding: gzip` and `Content-Length` and decodes the body before `on_head` runs (`run.rs:117-125`), so a gzip coding cannot be refused at the head (PD-17). The request still says `Accept-Encoding: identity`; other codings ureq leaves visible are refused before any byte is written (`E_DOWNLOAD_ENCODING`). A gzip response is guarded by the content: decoded bytes past `size` stop at `max_body` (`E_DOWNLOAD_SIZE`), and `verify_file` passes only the signed bytes (SC-009 e2-e4). Residual cost, stated honestly: a gzip body that decodes **shorter** than `size` cannot be told apart from a close-delimited body that ended early, so it is `E_DOWNLOAD_BODY` and retried (D5) within the request budget; whatever the retries assemble still verifies only if it is the signed bytes. `streamed_body_cases` pins the stripping, so a ureq upgrade that changes it goes red. |
| A transport error text holding a URL | PD-15's scrub (tokens with `://`, `/` or `?`) and the target-only URL (SC-030 a-d and its socket-less `scrub` row). |
| A link planted inside `downloads/` | PD-19, SI-29: classified without following, removed as a link (on Windows `remove_file`, then `remove_dir_all` for a directory link or junction), and if it survives, refused by `kind_of` at step 4, by `len_of`, `create_dir_all` and `verify_file`; Unix proof `staged_symlinks_are_not_followed` (PENDING-CI), Windows proof `staged_links_are_not_followed_windows` (junctions, local and `check (windows-latest)`). Windows symbolic links proper (which need a privilege) are not planted: they take the same `Other` path as a junction, because std classifies both as a symlink by the name-surrogate bit. |
| Scheduler-induced spurious stalls on the macOS and Windows legs | `timeout_per_read` also covers the wait for the first response bytes, so a fixture-server thread not scheduled for longer than `policy.stall` reads as `Stalled`. On 3-4 core hosted runners with dozens of server-backed tests in parallel, 200-300 ms pauses are plausible. Mitigation: a short stall bound only where the stall is the subject (SC-009 f, SC-006 `stall_resumes`), whose outcome a spurious stall cannot change; the test policy's 2 s elsewhere; SC-037 `Paced` at a 1 s bound with 150 ms gaps; rows where a retry would be harmless (SC-006 `stall_resumes`, SC-037 `Paced`) assert the outcome and `elapsed`, not "exactly one" or "no" `Retrying`. Expected durations stay under 60 % of each `< N s` bar. A red on CI that names `E_DOWNLOAD_STALLED` in a non-stall row is read as this risk first (the recording closure prints each `Retrying`), never answered by widening a bar. |
| A committed tauri-format private key | SI-22's base64, `.key` and `TAURI_SIGNING_PRIVATE_KEY` rules in `no_secret_key_in_app_tree`. |
| Disk full | `E_STAGING_FULL` with its step; the partial removed to return the space; no free-space pre-check (std has none). |
| Crash or kill mid-download | `.part` resumes; the verified path only ever holds a file that passed `verify_file` before the rename (SC-011). The `<hex>/` directory entry is not fsynced, so after a power loss the rename may be lost; the next run then re-verifies the `.part` with no request. |
| Registry poisoning after a panic | `lock().unwrap_or_else(PoisonError::into_inner)`; the drop guard removes the key during unwinding (SC-011's crash rows prove a new call is not busy). Release builds abort on panic (`Cargo.toml:48-53`), so a panic there ends the process, as a kill would. |
| `len_without_is_empty`-style clippy surprises in new traits | `PartialFile` has no `len` (PD-6). |
| `too_many_arguments` | Q-B decided: `DownloadEnv` (PD-11) brings every download entry point to ≤ 6 parameters with no suppression; `too_many_arguments_is_never_suppressed` fails on any `allow` / `expect` of the lint or of `clippy::complexity`, on a lint table that lowers it and on a raised threshold, so the ruling holds in later phases too. |
| A later phase needs one more input on a download entry point | It joins `DownloadEnv` if every call shares it, else a per-call struct (as `StreamLimits` does for `get_streamed`, already at 7); the guard leaves no other way. Adding a field to `DownloadEnv` touches `DownloadRig::env`, the one `commands.rs` literal and nothing else, because tests never spell the struct (§7). |
| `DownloadEnv` across threads (Phase 12's parallel downloads) | It is a `Copy` bundle of borrows and is not required to be `Send`: each worker builds its own from shared `Sync` parts, as SC-014's scoped threads do with `&rig` (§4.7). `&dyn StagingIo` is `Send` only if the trait were `Sync`; Phase 7 does not add that bound because nothing needs it. |
| A guard counting `disallowed_methods` expectations | Phase 7 adds none: tests reach temp dirs only through `fresh_temp_roots()` (`injection.rs:87-95`, D6 table unchanged). |
| `Cargo.lock` churn | `cargo update --workspace` only (§4.1), diff checked additive before any gate; the clippy-guard lock re-locked the same way (SI-3). |
| `minisign` dev graph on CI (`rpassword` needs a TTY only when asked for a password) | `TestSigner` uses `generate_unencrypted_keypair` and never prompts. |
| macOS temp roots under `/var` → `/private/var` | `fresh_temp_roots()` canonicalises the base (`test_support.rs:40-48`), so `starts_with` checks compare like with like. |

---

## 10. Precondition table (for the user)

| Id | Question | Status | Ruling / recommendation | Alternatives | Where the plan applies it |
|---|---|---|---|---|---|
| Q-A | (SI-9, PD-5) After a cached verified file fails re-verification, what does the progress stream show? | **Decided** 2026-10-31, as recommended (`spec.md:1244-1245`) | Emit `Restarted { reason: CachedInvalid }` (serialised `"cached_invalid"`) after the failed pass's `Verifying` and before the fresh download's first request. | Emit nothing, so `received` stays at the full size for that run (breaks FR-022 as written). | PD-5, SI-9, §4.4.1 `RestartReason`, §4.4.8 step 4, SC-029 b |
| Q-B | (PD-11) `obtain_and` (8 parameters) and `for_command` (9) exceed clippy's `too_many_arguments` limit of 7. | **Decided** 2026-10-31, **against** the revision 2 recommendation, the user's choice (`spec.md:1246-1250`) | `DownloadEnv { ctx, io, policy, cancel }` (four borrows, `Copy`, by value); every public download entry point ≤ 7 parameters (`obtain_and` 5, `run_command` 4, `for_command` 6, `command_request` 5, `discard` 2); no lint suppression anywhere, guarded by `too_many_arguments_is_never_suppressed`. | The revision 2 recommendation, two `#[expect(clippy::too_many_arguments, reason = …)]` (rejected by the ruling). | PD-11, SI-25, SI-26, §1.1, §1.2, §2, §4.2.4, §4.4.1, §4.4.8-§4.4.10, §4.7, §4.9, §4.10.1, §4.10.2, §4.11, §7, §7.3, §7.4, §9 |
| R-1 | (opened by Q-B) Does the `CancelToken` travel inside `DownloadEnv` or stay a separate parameter? The Q2 clarification (`spec.md:1234`) calls it "a `&CancelToken` … parameter on `obtain_and` and `get_streamed`, threaded through `for_command`", so the recommendation rewords that ruling's text (not its behaviour). | **Open**; this plan is written to the recommendation | **Inside the env** (`DownloadEnv::cancel`). `obtain_and` 5, `run_command` 4, `for_command` 6 parameters, which leaves room under the limit. Behaviour is unchanged: the same checks, codes and `.part` rule (FR-017), and the token stays per download, because the env is built per call (Phase 12 gives download *i* its own token with `DownloadEnv { cancel: &token_i, ..shared }`). `get_streamed` keeps its `&CancelToken` parameter (it takes no env). FR-017 is reworded by SI-25. | **A separate parameter** next to `progress`, with the env holding `ctx`, `io`, `policy`: `obtain_and` 6, `run_command` 5, `for_command` 7, exactly at the limit, so the next input on `for_command` needs another bundle. This keeps Q2's wording as written. | §4.4.1 (`DownloadEnv` field), §4.4.8-§4.4.10, §4.9, SI-25 (FR-017 sentence), SC-034 rows (`rig.cancel`) |

---

## Review notes

Revision 1: no review round yet.

Revision 2 (review round 1, 2026-10-31). Applied, with the evidence re-read this session:

- **Blocker (ureq strips a gzip `Content-Encoding`; spec-coverage) and major (same defect; xplat-reality):**
  confirmed in `ureq-3.5.0/src/run.rs:117-125` and `body/mod.rs:684-693`, `:942-953`; the workspace keeps the default
  `gzip` feature (`apps/krate-manager/Cargo.toml:39`, `Cargo.lock` `ureq` → `flate2`). Resolved by the first issue's
  option (a): PD-17, SI-18 (blocking spec edit of FR-013, SC-009 e and the External facts row), §1.2 row, §4.2.4,
  §4.4.6, §6, §9, SC-009 e1-e4, and a pinning case in `streamed_body_cases`. The second issue's option (a), disabling
  ureq's `gzip` / `brotli` features, is **rejected**: the Feed agent shares the one pin and the one agent
  (`injection.rs:1560-1595`), and Phase 5's `status_and_body_errors` decodes `Reply::Gzip` (`http_tests.rs:1204-1233`),
  which FR-011 requires to keep passing unchanged. Its requested end-to-end test ("`Content-Encoding: gzip` →
  `E_DOWNLOAD_ENCODING` with no `.part` bytes") is replaced by SC-009 e1 (`br`, the coding ureq leaves visible →
  `E_DOWNLOAD_ENCODING`, 0 bytes written) and e2-e4 (gzip → verified / `E_VERIFY_SHA256` / `E_DOWNLOAD_SIZE`, 1
  request each): with ureq 3.5.0 no implementation can produce the requested outcome, and no requirement was relaxed to
  get there, since the guard that matters (the bytes on disk are the signed bytes) is still asserted end to end.
- **Major (SC-037's Paced case cannot fail):** SI-19; `stall = 300 ms`, `elapsed > policy.stall` asserted.
- **Major (the struct-literal doctest fails for missing fields):** §4.3 now has a complete eight-field literal, with
  the `AssetName` taken as a parameter, plus a `no_run` twin that must compile.
- **Major (FR-022 for a resumed run has no detecting test):** PD-20 (`started` sets `on_disk` and `high_water`), SI-24,
  the shared `assert_progress_invariants`, and explicit resume assertions in SC-006, SC-008 c, SC-029, SC-034 c.
- **Major (the secret-key guard misses the tauri base64 key format):** SI-22, §4.10.1 rules (1)-(5) with run-time
  plants, §6 Keys bullet corrected. One deliberate deviation from the suggested resolution: a `TAURI_SIGNING_PRIVATE_KEY`
  value that starts `${{ secrets.` is not reported (it is a reference, not a key), so a later in-app doc can show the
  CI wiring; the self-test plants it as a must-not-report case.
- **Minor (`discard` vs an in-flight artifact):** the first suggested option, PD-18 / SI-20, SC-035 (f), with a new
  `DiscardBusy { name, asset }` variant because a `VerifiedArtifact` carries no feed URL for `Busy`'s `Target`.
- **Minor (SC-016 tautology):** SC-016 compares the parsed `SigningKey`s; `signing_key_text_cases` covers FR-040's
  equality rule.
- **Minor (SC-030 does not test the scrub; FR-053 counts not in the row):** SC-030 (d), the socket-less `scrub` row,
  the counts and `info!` fields in §7.1. PD-15's scrub is also widened to tokens holding `/` or `?`: the review's
  example (a path-and-query fragment with no `://`) would have passed the old scrub, and the new row (d) would then be
  red by design rather than by chance.
- **Minor (`CONFINED_TOML_KEYS` has no section; FR-081's not-reported plant relied on the real tree):** SI-21 (the
  section rule is enforced by `core_dependencies_are_the_fr007_set` and FR-081) and the explicit in-memory plant in
  §4.10.2.
- **Minor (FR clauses without a test):** §7.2 rows for FR-034 (I/O case tested; the length-changed-under-read case
  review-only, with the reason), FR-040 and FR-041 (`reassemble_cases` extended, no new required-test name).
- **Minor (symlinks inside `downloads/` are followed):** PD-19, SI-23, §4.3 step 1, §4.4.4, §4.4.5, §6, a new
  `#[cfg(unix)]` test in the `macos` and `linux` lists (28 and 25 names; per-leg totals 229, 248, 245), PENDING-CI on
  `check (macos-latest)` and `check (ubuntu-24.04)`. A linked `downloads/` itself is also refused (beyond the
  suggestion, same rule). Windows link removal is review-only, with a fail-closed argument (§4.4.5).

Revision 3 (the user's precondition rulings of 2026-10-31, `spec.md:1240-1250`), applied in place:

- **Q-A, as recommended.** No design change: PD-5 and SI-9 are marked decided, and SI-9's edit now also says the
  `Restarted { CachedInvalid }` comes before the fresh download's first request (SC-029 b already asserts it).
- **Q-B, against the recommendation.** PD-11 is rewritten: a `DownloadEnv<'a> { ctx, io, policy, cancel }` of four
  borrows, `Copy`, passed by value, so `obtain_and` takes 5 parameters, `run_command` 4 and `for_command` 6, with no
  `#[expect]`. The field set is the inputs every call shares. `ctx` already holds the staging root, the transport config
  and the release key, so those get no fields of their own. `feed`, `request`, `progress`, `next` and `host` stay per
  call (reasons in PD-11). Re-read for it this session: the workspace lint table (`Cargo.toml:44-46`), member
  `[lints]` (`crates/core/Cargo.toml:42-43`, `crates/helper/Cargo.toml:13-14`, `src-tauri/Cargo.toml:47-48`),
  `clippy.toml:1-10`, `injection.rs:87-98`, `:1140-1183`, `:1289`, `:1315-1332`, `:2558-2622`, `http.rs:35-42`,
  `:986-994`, `:1030-1037`, `detect.rs:181-184`, `pac.rs:70`, `:110`, `feed_tests.rs:1489-1497`, and the guards'
  `commands.rs` checks (`config_guard.rs:104`, `:1622-1647`, `:1976-1997`; `injection.rs:150`, `:2215-2228`), none of
  which pins a download signature. Clippy's lint and threshold are cited from its source and docs (§1.2). Sections
  rewritten: §0.1 PD-11, §0.2 SI-25 (blocking: FR-017 / FR-045 / FR-060 signatures, the command call, New components)
  and SI-26 (FR-086, SC-001), §1.1, §1.2, §2, §4.2.4 (`get_streamed` stays at 7), §4.4.1, §4.4.8, §4.4.9 (`discard`
  unchanged), §4.4.10, §4.7 (`test_policy`, `DownloadRig`, `FaultyStagingIo` constructors), §4.9, §4.10.1 (new guard
  `too_many_arguments_is_never_suppressed`), §4.10.2 (no change), §4.11 (`cargo.all` 221; per-leg totals 230, 249,
  246), §7 (rig-built envs; SC-001, SC-011, SC-014, SC-028, SC-034, SC-035, SC-037 rows), §7.3, §7.4, §9, §10.
- **New decision R-1** (the cancel token's placement), which the user owns because it rewords the Q2 clarification's
  "parameter on `obtain_and`". The plan is written to its recommendation (inside the env).
- **One addition beyond the ruling's text, stated here:** the guard `too_many_arguments_is_never_suppressed` (one new
  required test). The ruling says "no clippy lint suppression anywhere". Without the guard, only this phase's review
  would hold that line, because the workspace lints permit `#[expect]` and SC-001 named only `#[allow`. The guard reuses the
  existing `lint_suppressions` parser.

Revision 4 (review round 2, 2026-10-31). Every issue applied; none rejected. Evidence re-read this session:
`spec.md:200-216` (D4, D5), `:328-336` (FR-005), `:386-392` (FR-013, FR-014), `:440-525` (FR-030 to FR-043),
`:588-600` (FR-060), `:752-775` (SC-003, SC-004), `:1044-1053` (SC-037); `roots.rs:173-185` (`check_sandboxed`,
lexical); `http_tests.rs:1277-1283` (`E_FEED_TLS` from a plain-HTTP answer to TLS); `injection.rs:337-344` and
`:1794-1815` (TA-1, `cmd` already admitted for one body); `detect_tests.rs:2569-2594` (junctions via `mklink /J`);
`apps/krate-manager/Cargo.toml:44-46` and `clippy.toml` (no nursery lint, so `pub(crate)` items raise nothing); std's
Windows `fs` source and the local std 1.99.1 docs (§1.2 row).

- **Major (body limit fixed before the head; SC-007 cannot pass):** SI-27 (blocking). `StreamLimits` loses `max_body`;
  `HeadAction::Read { max_body }` carries it from `on_head`, `size` for `Full` and `size - from` for `Continue`
  (§4.2.4 steps 7-8, §4.4.8 step 7). SC-007 now asserts the `200` after a ranged request is written whole and the run
  ends `Ok`; `streamed_body_cases` gains the per-head limit rows (a `Full` after a ranged request streams all `n`
  bytes; a `size - from` limit on the same response ends `TooLarge`).
- **Major (FR-005's in-flight exception untested):** `download::tests::prune_spares_in_flight_artifact` (SC-014 row,
  §4.11, SI-30), held at A's first `Bytes` so the `.part` exists, with a control run proving the prune does remove
  X once A is out of flight.
- **Major (FR-016 / D5 transport codes untested at the download level):** `download::tests::non_retryable_transport_errors`
  (three rows: URL, redirect limit, TLS; 1 asset-route request, no `Retrying`), §7.2 FR-016 row corrected.
- **Major (`verify_file` / `from_text` public, so any crate can mint a `VerifiedArtifact`):** SI-28. `verify_file`,
  `Expected` (type, fields, `from_request`) and `SigningKey::from_text` are `pub(crate)`. Every caller is in core,
  `test_support.rs` included (it compiles into core's lib), so no `cfg(feature = "test-support")` gate is needed. The
  FR-043 guard gains the declaration and call-site rules and their plants (§4.10.1); §4.3 and §6 state the route.
- **Minor (`for_command` never shown to reach `run_command`):** SC-028 (f), with the expected `E_DOWNLOAD_URL`, the
  feed request, 0 asset requests, one `Started` and the stale `.part` pruned; §7.2 FR-060 row.
- **Minor (FR-063 not detectable by SC-025):** the `apps/krate-manager/src` tracked-diff and untracked-file check in
  SC-025, §7.2 and §7.4 step 5.
- **Minor (SC-003 c, FR-038 order, FR-033 fields):** SC-003 (c) asserts `written() == 0` and no `open_partial` call
  through the new `FaultyStagingIo::calls_of`; (f) asserts both key-id fields and their `^[0-9A-F]{16}$` form;
  `verify_steps_stop_at_first_failure` gains three two-field binding rows that pin the order. Beyond the suggestion:
  rows (h)-(k) also assert `signed` / `expected`.
- **Minor (step 4 follows a `<hex>` link the prune could not remove):** SI-29. `StagingIo::kind_of` (the reviewer's
  first option under another name, since `len_of` keeps its regular-file meaning) runs before `len_of(<hex>/<asset>)`
  and refuses anything but `None` / `Dir` (`E_STAGING_IO`, step `open`); PD-6, PD-19, §4.4.4, §4.4.5, §4.4.8 step 4.
  The sentence claiming links on Windows need a privilege is replaced: junctions do not.
- **Minor (Windows link removal untested):** on Windows the prune retries an `Other` entry's failed `remove_file`
  with `remove_dir_all` rather than the suggested `remove_dir`: both reach `RemoveDirectoryW` for a link, and
  `remove_dir_all` is already in `StagingIo` and documented not to follow links, so the trait gains no removal
  method. The new `staged_links_are_not_followed_windows` (`windows` list, 7 → 10) uses junctions via `cmd /C mklink
  /J`, as `listing_skips_junction_windows` does, instead of `symlink_dir` with a skip on `PermissionDenied`: a
  skipping test still prints `... ok` and would satisfy the required-tests rule without asserting anything, while a
  junction needs no privilege, so the case always runs. It also runs locally on this box. Both link tests gain a
  case where the removal is made to fail and step 4 must refuse.
- **Minor (spurious stalls on hosted runners):** SC-037 `Paced` at `stall = 1 s` with 10 chunks 150 ms apart (≈ 1.4 s,
  under 60 % of its `< 3 s` bar; the suggestion's 12 chunks would sit at about 1.8 s, on the 60 % line); SI-19's spec
  edit revised to match. SC-037 and SC-006 `stall_resumes` assert the outcome and `elapsed` instead of "no" / "exactly
  one" `Retrying`. That is not a weakened proof: `Paced` answers every request with the full body, so `Ok` still
  requires one response longer than the stall bound. New §9 risk row.
- Counts: `cargo.all` 221 → 223, `windows` list 10; per-leg totals 233 / 251 / 248 (§4.11, §7.3, §7.4).
