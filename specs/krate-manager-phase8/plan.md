# Implementation Plan: Krate Manager Phase 8 — Safe Extraction and Staging (Linux tar.gz)

**Spec:** `specs/krate-manager-phase8/spec.md` (DRAFT revision 3: review issues of 2026-11-01 applied and the eight
clarifications Q1-Q8 of 2026-11-01 encoded, `spec.md:14-15`, `:1031-1069`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 8 (lines 634-659); Phase 1 C1 `payload` and C4 (270-275, 296-301);
Core Philosophy "Verify before anything touches the asset" (69-70); per-OS table, Linux row (118); Phase 9, the consumer
(661-703); Dependency Graph edges 1→8, 4→8, 7→8, 8→9 (954-955); Cross-Cutting Constraints (982-1023)
**Status:** DRAFT plan, revision 2, 2026-11-01 (the plan review's 15 issues applied, see Review notes), verified
against `feat/krate-manager` at `2499fb13` (`git rev-parse --short HEAD`; untracked: `specs/krate-manager-phase8/`).
**Lanes:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm typecheck / lint /
test, the version lint) and the **pipeline lane** (`node --test tools/feed-contract/tests/`, because
`manager-ci-shape.test.js` changes). No C++ file changes, so there is no `run-gate.js` / `header-consumers.js` lane;
no `src-tauri` or webview change, so `tauri build` is only re-run as part of the unchanged CI leg.

**Build preconditions.** The spec edits of §0.2 land before `tasks.md`. SI-1, SI-2, SI-3 and SI-4 are **blocking**:
built as the spec reads, SI-1 turns `production_config_only_in_allowed_tests` red (SC-013's `/usr/bin/xattr` has no
TA-1 row), SI-2 makes FR-083's token rule report every `ExtractIo` call site (the trait's own method names are banned
tokens), SI-3 turns the clippy-guard `--locked` step red on every leg (its lock **will** change, the spec says "if"),
and SI-4 makes SC-024 and SC-027 unpassable on Windows (`exec-and-symlink` cannot stage through `FsExtractIo` there,
by D4's own rule). §10 lists the two decisions this plan takes that the user may want to overrule.

---

## 0. What this plan adds to the spec

The spec fixes requirements, names, codes and test names. This plan fixes the module boundaries and exact signatures,
the algorithms the spec states in prose (the gzip member loop, the streaming tar reader, the rule engine and its
first-failure order, the two passes, the live registry and prune, the post-checks), the manifest and lockfile edits,
every change an existing guard needs to stay green, the per-SC test design and the local close procedure. Where the
tree or a crate contradicts a spec sentence, the conflict is in §0.2.

### 0.1 Plan decisions

| Id | Decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **Pass 1 defers per-entry rule failures so the first code equals `listing.js`'s on every archive, not only on the single-defect corpus.** While reading, a header-level archive error (gzip, checksum, octal, pax, truncation) or a source I/O error stops pass 1 at once; a per-entry rule-1 failure (FR-017) and a rule-2 failure (FR-018) are each **recorded once** (the first in archive order) and reading goes on; the failing entry is not counted (D8) and not stored. A Phase 8 bound (`headers`, `tar_bytes`, `meta`, `path`, `segment`, `entries`) stops reading at once. Resolution at the end of pass 1: (1) the archive error that stopped it; (2) else the first recorded rule-1 failure, then the first rule-2 failure; (3) else the bound that stopped it; (4) else FR-019 (rule 3) over the stored entries; (5) else the plan. | `listing.js` reads **every** header before applying any rule (`readTarEntries`, `:220-274`, is called before `listingFromEntries`, `:284`), then applies rule 1 to all entries (`:114-117`), then rule 2 to all links (`:118-120`), then rule 3 (`:121-151`). A stream-and-raise reader reports rule 1 of entry 3 where Node reports a checksum error at entry 9, and rule 2 of entry 3 where Node reports rule 1 of entry 5. `CONTRACT.md:341-343` ("the same rules, in the same order") and FR-019's "an archive that `listing.js` refuses with code X is refused by Phase 8 with code X" hold only with the deferral. Cost: two `Option<ExtractError>` slots; memory and reads stay bounded by D8. | FR-015, FR-019 (SI-5) |
| PD-2 | **`create_work_dir` runs inside the registry's critical section, together with the prune.** One lock acquisition: prune `extract/` → `create_work_dir` → insert the returned path into the registry → release. | FR-049 claims a prune "can neither miss a root created before it took the lock nor remove one created after". If `create_work_dir` ran outside the lock, call B's prune could list `extract/` between A's `mkdir` and A's registration and remove A's brand-new directory. Holding the lock across one `mkdir` (`tempfile` retries name collisions itself, `tempfile-3.27.0/src/util.rs:39-67`) keeps the section short. The model: `download.rs:676-677`, whose prune holds `IN_FLIGHT` for its whole pass (Phase 7 PD-12). | FR-040, FR-049 (SI-6) |
| PD-3 | **`ExtractIo` is a concrete trait with 14 methods and two helper traits (§4.3.2).** The archive handle is `Box<dyn ArchiveRead>` (`Read` + `rewind`), so "two passes over one handle" (D3) and SC-009 (d)'s fake that serves other bytes "on the rewind" are both direct. A staged file is `Box<dyn ExtractFile>` (`write_all`), so the recording wrappers see every `write` (FR-087) the way `FaultyStagingIo` sees `PartialFile::write_all` (`test_support.rs:477-498`). | D5 lists the steps in prose; FR-087 names `create_work_dir`, `create_dir`, `create_file`, `write`, `set_mode`, `symlink`, `remove_tree`, `remove_entry` as the mutating methods. | D5 (mechanism) |
| PD-4 | **Archive read errors are step `read_archive`.** An `io::Error` returned by the archive handle's `read` or `rewind` (not an `UnexpectedEof` from the gzip decoder, which is `E_LISTING_CORRUPT`) maps through FR-045 with step `read_archive`. A tap under the decoder records the first source error, so the two are told apart. | FR-045's step list has `open_archive` but no step for a read error in mid-archive. | FR-045 (SI-7) |
| PD-5 | **The staged version check decodes before it parses.** `stage` reads the bounded bytes, requires UTF-8 (`found: unknown` otherwise), strips one leading U+FEFF, and passes the text with the relative path `Contents/Resources/moduleinfo.json` to `detect::moduleinfo_version`; the result must be `CopyVersion::Known { version, .. }` with `Some(version) == entry.version`; `expected` in the error is `entry.version_text`, `found` is `version.to_string()` or `unknown`. A plan size above `max_moduleinfo_bytes` is `E_EXTRACT_LIMIT { which: "moduleinfo" }` before any read; a read that returns more than the bound (the file grew after pass 2) is the same code. | `moduleinfo_version(text: &str, path: &Path)` takes text (`detect.rs:692`); its caller `vst3_version` does the bounded read, the UTF-8 check and the BOM strip (`detect.rs:565-581`). Phase 6 compares a detected copy with the feed through `version::compare(.., entry.version)` (`status.rs:364`, `version.rs:84-94`): `CoreVersion` equality, not text equality. | FR-057 (SI-8) |
| PD-6 | **`extract/` must be a real directory before anything lists it or creates under it.** `prune` asks `kind_of(extract)` first: `None` → returns 0; `Some(Dir)` → lists it; any other kind (a planted link, a file) → logs `E_EXTRACT_ESCAPE` once and returns 0 without listing or removing anything. `FsExtractIo::create_work_dir` runs `create_dir_all(extract)` and then requires `symlink_metadata(extract)` to be a directory (`InvalidInput` otherwise, step `create_work_dir`), as `FsStagingIo::create_dir_all` does for `downloads/` (`download.rs:578-585`). The production body is tested on the real filesystem: `prune_removes_real_link_unix` (SI-9 block, a real symlink at `extract/`) and the Windows junction block of `symlink_entry_unsupported_windows` (j2); the Mem rows (SC-020, SC-029) are logic evidence only. | `read_dir` follows a link given as its argument, so a prune of a linked `extract/` would remove the contents of the link's target. Phase 7 closed the same hole for `downloads/` (Phase 7 PD-19). | FR-040, FR-058 (SI-9) |
| PD-7 | **Windows removes a directory link with a fallback.** `FsExtractIo::remove_entry(path, NodeKind::Symlink \| NodeKind::Other)` calls `fs::remove_file` and, on Windows only, after that fails, `fs::remove_dir_all`, which "does not follow symbolic links and it will simply remove the symbolic link itself" (std 1.99.1, `fs::remove_dir_all`, https://doc.rust-lang.org/std/fs/fn.remove_dir_all.html). The fallback sits **inside** `impl ExtractIo for FsExtractIo` so FR-083's token rule holds. Tested on `windows-latest` and on this box by the junction block (j1) of `symlink_entry_unsupported_windows` (§7.1 SC-023, SI-10). | `download.rs:730-739` (`remove_link`), Phase 7 PD-19: std's `remove_file` cannot remove a Windows directory symlink or junction. | FR-058 (SI-10) |
| PD-8 | **`StagedTree` stores its `extract/` directory and holds its registry entry.** `discard` refuses (`E_EXTRACT_PATH`, nothing removed) unless `root.parent() == Some(extract_dir)`, the root's name is `<64 lower-case hex>.<6 alphanumerics>` and the root is in the live registry. The registry entry is a `LiveRoot` guard field, so dropping a `StagedTree` deregisters it (FR-051) with no `Drop` impl on `StagedTree` itself. | FR-051 / SC-015's "direct child of `extract/`" needs the `extract/` path, which `discard(tree, io)` (no env) can only get from the tree. The name check mirrors `staged_paths`'s hex check (`download.rs:122-157`, `:174`). | FR-050, FR-051 (mechanism) |
| PD-9 | **Every write target and the joined-length bound go through the same join.** `entry_path` (FR-041) splits on `/` and pushes each checked segment; FR-059 measures `staging/extract/<64 hex>.XXXXXX` joined with the longest listing path through `entry_path` too, as `as_os_str().as_encoded_bytes().len()` (stable 1.74.0, "an unspecified, platform-specific, self-synchronizing superset of UTF-8", https://doc.rust-lang.org/std/ffi/struct.OsStr.html#method.as_encoded_bytes, std 1.99.1), so the measured and the created path have the same separators. | FR-059: "the byte length of the OS string form of the joined path"; `tempfile`'s random part is 6 alphanumerics (`NUM_RAND_CHARS`, `tempfile-3.27.0/src/lib.rs:195`; `util.rs:15`). | FR-059 (mechanism) |
| PD-10 | **No new `Platform` method; the per-OS seam is `FsExtractIo` itself.** Linux and macOS share one `cfg(unix)` body (modes through `PermissionsExt`, symlinks through `std::os::unix::fs::symlink`); every other target writes no mode and reports `supports_symlinks() == false` (D4). `stage` asserts `ctx.roots.check_sandboxed(&extract_dir)` before any write (a no-op in production, a panic for a test root outside its sandbox, `roots.rs:175-185`). | Phase 7 PD-16 precedent; Phase 8's only root is `Roots::staging_dir()` (`roots.rs:199-201`), already resolved per OS by Phase 4. | none |
| PD-11 | **The pure listing builder and the streaming reader share one rule implementation.** `CanonicalListing::from_entries` (FR-021) applies rule 1 to all entries, rule 2 to all links, then rule 3, exactly `listing.js:112-159`; `plan_archive` calls the same per-entry functions as entries arrive (PD-1) and the same rule-3 function at the end. | FR-021 (one builder for the archive and the tree on disk) and FR-019 (one order). | FR-020, FR-021 (mechanism) |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before tasks)

| Id | Blocking | Issue (evidence) | Proposed spec edit |
|---|---|---|---|
| SI-1 | **yes** | SC-013's `staged_tree_has_no_xattrs_macos` starts `/usr/bin/xattr` from `extract_tests.rs`. TA-1 lets a `crates/core` test start only `sleep` / `printf` or a program listed for its one body (`injection.rs:393-409`, rule at `:1871-1897`); `TEST_PROGRAM_BODIES` holds four rows, none for `extract_tests.rs`. `production_config_only_in_allowed_tests` asserts the real tree has no `test_source_findings` (`injection.rs:3692-3712`), so it goes red. FR-087 names the program but no FR adds the row. | FR-087 / New components (`injection.rs`): `TEST_PROGRAM_BODIES` gains `(EXTRACT_TESTS_RS, "staged_tree_has_no_xattrs_macos", "/usr/bin/xattr")` and `(EXTRACT_TESTS_RS, "symlink_entry_unsupported_windows", "cmd")` (the PD-7 junction block, SI-10; array length 4 → 6). The Phase 7 precedents are Phase 7 SI-4 (`download_tests.rs` row at `:400-403`) and the `cmd` row of `staged_links_are_not_followed_windows` (`injection.rs:404-408`, the body at `download_tests.rs:2949-2965`). |
| SI-2 | **yes** | FR-083 bans `create_dir` and `symlink` outside `impl ExtractIo for FsExtractIo`, while FR-087 / D5 name `create_dir` and `symlink` as `ExtractIo` **methods**. The scanner matches tokens (`seq_at(toks, &[...])`, `injection.rs:803-813`), so the trait declaration (`fn create_dir`, `fn symlink`), every `env.io.create_dir(` / `env.io.symlink(` in `stage`, and every wrapper in `test_support.rs` would be reported; the rule cannot pass on any implementation that keeps the spec's method names. | FR-083: "…appear only inside `impl ExtractIo for FsExtractIo`, except that the two `ExtractIo` method names `create_dir` and `symlink` may also appear as a method declaration (`fn create_dir` / `fn symlink` inside `trait ExtractIo` or any `impl ExtractIo for …`) and as a method call (`. create_dir (` / `. symlink (`); std has no method of either name, so a method call is always the trait's. `create_dir_all` appears only in `fn create_work_dir` of `impl ExtractIo for FsExtractIo`." SC-017 gains the planted case `fn f() { std::fs::create_dir("x").ok(); }` in `extract.rs` outside the impl (reported) and keeps the real tree green. |
| SI-3 | **yes** | FR-080 says the clippy-guard lock changes "if" `cargo metadata` changes it. It **will**: `tests/fixtures/clippy-guard/Cargo.lock:2020-2039` lists `krate-manager-core`'s dependencies without `flate2` (a path dependency's dev-dependencies are not in a dependent's lock), so moving `flate2` to `[dependencies]` adds `"flate2"` to that list, and `manager-ci.yml`'s clippy-guard step runs with `--locked` (`manager-ci.yml:100-114`). The workspace lock already lists it (`Cargo.lock:2049-2054`, dev-dependencies included). | FR-080: "the clippy-guard lock gains `"flate2"` in `krate-manager-core`'s dependency list (re-locked with `cargo update --workspace --manifest-path tests/fixtures/clippy-guard/Cargo.toml`, the commit `2f3c93d7` procedure) and is committed with the phase; no new `[[package]]` enters either lock (`flate2` 1.1.10 is at `clippy-guard/Cargo.lock:1028-1036`)." New components: the clippy-guard `Cargo.lock` row loses "only if". |
| SI-4 | **yes** | D4: `FsExtractIo` on non-Unix reports `supports_symlinks() == false` and `stage` refuses a plan with a symlink (`E_EXTRACT_UNSUPPORTED`, SC-023 asserts exactly that for `exec-and-symlink` on Windows). Yet SC-024 runs B = `exec-and-symlink` "again over `FsExtractIo`" and asserts B succeeds (`cargo.all`, so on Windows too), and SC-027 stages `exec-and-symlink` without naming the I/O (in `cargo.all`). Measured this session over the committed listings: `exec-and-symlink`, `long-link` and `pax-path` hold a link; `dot-slash`, `implicit-dirs`, `long-name`, `pax-global`, `real-shaped`, `ustar-prefix` hold none, and all nine carry `moduleinfo.json` Version `1.2.0` (`tar -xzO`). | SC-024: the `FsExtractIo` variant's thread B stages `ustar-prefix` (no link, 12 lines) on every OS; the `MemExtractIo` variant keeps `exec-and-symlink`. SC-027: `exec-and-symlink` is staged over `MemExtractIo` (every OS) and `real-shaped` over `FsExtractIo`; the hash-equals-`write`-payloads clause stays on the Mem recording wrapper. |
| SI-5 | no | The first-failure order on archives with more than one defect (PD-1). FR-015's "raised as soon as the bound is crossed" and FR-017 / FR-018's per-entry timing read as stream-and-raise, which reports a different first code than `listing.js` (`:220-284`) on a two-defect archive; the committed bad set has one defect per archive (`listings/README.md`: "exactly one defect"), so SC-003 cannot see the difference. | FR-015: add "A per-entry rule failure (FR-017, FR-018) is recorded, not raised: pass 1 keeps reading, so an archive error later in the stream wins, as in `listing.js`; a Phase 8 bound stops reading at once; the reported code is the stopping archive error, else the first rule-1 failure, else the first rule-2 failure, else the bound, else FR-019 (PD-1)." SC-006 gains three runtime rows (§7.1): `..` entry then a bad checksum → `E_LISTING_CHECKSUM`; an escaping link then an absolute path → `E_LISTING_ABSOLUTE`; an absolute path then `payload.files` crossed → `E_LISTING_ABSOLUTE`. |
| SI-6 | no | FR-049's "neither miss a root created before it took the lock nor remove one created after" needs the creation inside the lock (PD-2); the spec registers "the moment `create_work_dir` returns", which leaves the window between `mkdir` and registration. | FR-040 / FR-049: "the prune, `create_work_dir` and the registration run in one critical section of the registry lock (PD-2)". |
| SI-7 | no | FR-045 has no step for an archive read error after the open (PD-4). | FR-045: steps gain `read_archive`. |
| SI-8 | no | FR-057 passes the staged file to `moduleinfo_version`, which takes `&str` (`detect.rs:692`); the decode, BOM strip and `CoreVersion` comparison are unstated (PD-5). | FR-057: PD-5's sentence; SC-028 gains (g) a `moduleinfo.json` that is not UTF-8 → `E_EXTRACT_VERSION { found: "unknown" }` and (h) one with a leading BOM and version `1.2.0` → stages. |
| SI-9 | no | Nothing refuses a linked `extract/` (PD-6): FR-058's prune lists it through `ExtractIo` and `read_dir` follows a link argument. | FR-058: "`prune` lists `extract/` only when `kind_of(extract)` is a directory; any other kind is logged and nothing is removed"; FR-040: `create_work_dir` requires `extract/` to be a directory after creating it. SC-029 gains (Mem) a linked `extract/` → `prune` returns 0, the link target's canary untouched; `prune_removes_real_link_unix` gains the same on the real filesystem, followed by a `stage` over `forwarding(&FsExtractIo)` → `E_EXTRACT_IO { step: "create_work_dir", kind: InvalidInput }` with the link target's snapshot unchanged (the production `create_work_dir` body, §7.1 SC-029); SC-020 gains a linked `extract/` row → `E_EXTRACT_IO { step: "create_work_dir", kind: InvalidInput }`, nothing created in the target. |
| SI-10 | no | FR-058 removes "a symlink as the link itself… any other kind as a file"; on Windows `remove_file` cannot remove a directory symlink or junction (`download.rs:734-739`). | FR-058: "on Windows a link that `remove_file` refuses is removed with `remove_dir_all`, which removes the link itself (PD-7)". No new test name, but a test: a `#[cfg(windows)]` junction block inside the existing `cargo.windows` name `symlink_entry_unsupported_windows` (§7.1 SC-023 row) plants junctions with `cmd /C mklink /J` exactly as the Phase 7 test does (`download_tests.rs:2949-2965`), under the SI-1 `cmd` row, and proves both PD-7 (a junction child of `extract/` is removed as itself) and PD-6 on Windows (a junction at `extract/` itself is never listed). |
| SI-11 | no | FR-085 lists the runtime archives but not the PD-1 / SI-5 two-defect archives, the SI-8 BOM / non-UTF-8 archives or the header-flood archive's shape. | FR-085: add them (§7.1 gives each). |
| SI-12 | no | The plan review (revision 2) added assertions and guard rules inside existing test names that the spec's SC / FR text does not yet state; no name or lane count changes (§4.7), so nothing goes red if the spec lags, but the compliance rows need the text. | SC-003: + the pure rule table (every reject row of `c4-listing-tar.test.js:196-231` and the accept rows of `:236-280`) through `CanonicalListing::from_entries` with the same code per row, and three streaming rows through `plan_archive`. SC-004: + real modes (`cfg(unix)`). SC-009: + rows (e1), (e2), (f). SC-010: + rows (iii) cleanup fails, (iv) archive read failures. SC-012 / SC-020: + the dangling-link and the link-lands-outside-its-top rows; the SI-9 Fs row. SC-023: + the junction block (SI-10). SC-030 (c): + the real-root assertion. SC-031: + the gzip-layer rows. FR-041: + Windows reserved stems and a trailing dot / space refused on `cfg(windows)`. FR-082: "`tar` is not a key in any workspace manifest and no `[[package]]` named `tar` is in either lock". FR-083: + `File :: options`, `DirBuilder`, `remove_dir`, `soft_link`, `symlink_file`, `symlink_dir`, `set_len`, and the `tempfile` rule (§4.6). FR-087: the wrapper powers of §4.5 (the `open_archive` / `read_tree` overrides, `XOp` with paths and `write` bytes, a list of planned failures, a failing `remove_tree`, a read fault). |

Not an issue, recorded: FR-059 counts tempfile's random part as 6 characters; `tempfile-3.27.0/src/lib.rs:195`
(`const NUM_RAND_CHARS: usize = 6;`) and `src/util.rs:15` (`rng.alphanumeric()`) confirm it. FR-081's dependency-tree
step is unaffected: `flate2` is already in core's normal graph through `ureq`'s default `gzip` feature (`cargo tree
-p krate-manager-core -e features -i flate2 --locked`, this session: `flate2 feature "default"` ← `ureq v3.5.0`), and no
crate enables flate2's `zlib-rs` feature on any target. Measured this session over **every** target triple, not only
the host (plain `cargo tree` resolves the host triple alone, so a macOS-only or Linux-only table such as tauri/wry's
`webkit2gtk` would be missed): `cargo tree --workspace --target all -e features -i zlib-rs --locked` prints
"nothing to print"; `cargo tree --workspace --target all -e features -i flate2 --locked` lists exactly the flate2
features `any_impl`, `default`, `miniz_oxide`, `runtime_detection`, `rust_backend` (reached through `ureq`, `png` ←
`ico` ← `tauri-codegen`, and others), no `zlib-rs`. `zlib-rs` 0.6.8 is a `[[package]]` in both locks
(`Cargo.lock:1041-1045` lists it in flate2's dependency array, `:5388`; `clippy-guard/Cargo.lock:5287`) as flate2's optional dependency; the
`--target all -e features` tree above shows no feature activating it. So core builds with the
`miniz_oxide` backend on all three legs. The same `--target all` form finds no `tar` package (`cargo tree --workspace
--target all -i tar --locked`: "package ID specification `tar` did not match any packages"; `grep '^name = "tar"'`
over both locks: no hit).

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-11-01 at `2499fb13`)

| Fact | Where |
|---|---|
| `VerifiedArtifact` private fields, accessors `path`, `name`, `version`, `platform`, `asset`, `size`, `sha256`, `format`; compile-fail doctests | `crates/core/src/verify.rs:90-162` |
| `pub(crate) struct Expected<'a>` with `pub(crate)` fields incl. `format`; `pub(crate) fn verify_file(path, &Expected, &SigningKey)`; `sha2` imported only here (`:9`); `hex_lower` `:290-299`; `#[cfg(test)] sha256_of` `:302-307` | `verify.rs:61-71`, `:165-287` |
| `DOWNLOADS_DIR = "downloads"`; `staged_paths` one checked join; `is_hex64`; `DownloadEnv` four borrows, `Copy` | `download.rs:28`, `:122-157`, `:174`, `:209-214` |
| `EntryKind { Dir, File, Other }` from a non-following `file_type()`; `StagingIo`; `FsStagingIo::create_dir_all` requires a real directory afterwards | `download.rs:518-538`, `:547-561`, `:578-585` |
| `static IN_FLIGHT: Mutex<BTreeSet<…>>`; `InFlight` guard with `Drop`; the prune holds the lock for its whole pass; Windows `remove_link` fallback | `download.rs:646-672`, `:676-677`, `:730-739` |
| `discard(artifact, io)` re-derives and checks the path | `download.rs:1480-1516` |
| `Roots::new` (absolute, normal, `staging_dir == state_dir/staging`, no canonicalisation); `sandboxed`; lexical `check_sandboxed`; `staging_dir()` | `roots.rs:80-113`, `:166-171`, `:175-185`, `:199-201` |
| `AppContext { roots, settings, feed_url, http, signing_key }` | `context.rs:51-60` |
| One process per state dir (`StateLock::acquire`, exclusive `try_lock`) | `state_lock.rs:53-71` |
| `canonical_temp_dir` (holds one of the two `disallowed_methods` sites); `check_under_temp` returns the canonical base; `temp_roots_in` builds every root from the canonical base; `fresh_temp_roots`; capture logger `install_capture` / `take_mine`; `kat_signing_key`; `test_feed_document` gives a Linux entry `payload {files: 1, sha256: <asset sha>}` and `install.vst3_bundle = "<Name>.vst3"` | `test_support.rs:47-56`, `:60-79`, `:83-132`, `:140-144`, `:186-205`, `:284-289`, `:312-368` (`:335`, `:337`) |
| `FaultyStagingIo` (one planned failure, call counters, a wrapping partial file) — the model of the Phase 8 wrappers | `test_support.rs:376-543` |
| `TestSigner { new, key, sign, sign_c3 }`, `c3_comment`, `#[cfg(test)]` | `test_signer.rs:10-73` |
| `PluginEntry { name, version_text, version: Option<CoreVersion>, download_url, release }`; `StableRelease { platforms, install, … }`; `PlatformAsset { asset_name, size, sha256, format, payload, … }`; `Payload { files: u64, sha256: [u8; 32] }` (pub fields); `AssetFormat`; `InstallFacts.vst3_bundle: PlainName`; `PlainName::checked(field, &Value)` crate-private | `feed.rs:37-87`, `:121-128`, `:141-153` |
| `check_install_value` (no `/`, `\`, `:`, `..`, reserved stem, pattern) is `pub`; `vst3_bundle` pattern | `contract.rs:267-296`, `:1036-1040` |
| `MAX_MODULEINFO_BYTES = 1 MiB`; `CopyVersion::{Known { version, source }, Unknown(_)}`; `vst3_version` reads bounded, checks UTF-8, strips BOM; private `moduleinfo_version(text, path)` logs at target `krate_manager_core::detect` with `path.display()` | `detect.rs:18`, `:105-111`, `:553-582`, `:690-776` |
| `CoreVersion::parse`; `reduce_class_version`; `compare` (`Ordering::Equal` → `UpToDate`); the status card passes `entry.version` | `version.rs:33-46`, `:56-65`, `:84-94`; `status.rs:364` |
| Workspace pins `flate2 = "1.1.10"`, `tempfile = "3.27.0"`; lints deny `allow_attributes*`; `[profile.release] panic = "abort"` only | `apps/krate-manager/Cargo.toml:17`, `:42`, `:50-52`, `:54-59` |
| Core `[dependencies]` (13 keys, `tempfile` among them), `[dev-dependencies] flate2, minisign` | `crates/core/Cargo.toml:15-32`, `:34-37` |
| `pub mod` list; `#[cfg(test)] mod test_signer;` | `crates/core/src/lib.rs:1-28` |
| `FR007_DEPENDENCIES` (13); `CONFINED_TOML_KEYS` (12); `TEST_PROGRAMS` / `TEST_PROGRAM_BODIES` (4); `is_scanned` skips `test_support.rs` and `*_tests.rs`; inline `mod tests {` banned; `sandboxed` token only in `roots.rs`; `is_test_source`; the static-lock rule covers test sources and `fixture_server.rs` only | `injection.rs:106-120`, `:299-318`, `:393-409`, `:1281-1283`, `:1182-1187`, `:1210-1212`, `:1441-1448`, `:1938-1967` |
| `minting_route_findings` (`<name> (` calls only in `callers`); `verify_file` callers `[VERIFY_RS, DOWNLOAD_RS, VERIFY_TESTS_RS, DOWNLOAD_TESTS_RS]` | `injection.rs:2478-2509`, `:2579-2583` |
| `verify_confinement_findings` (`minisign_verify`, `sha2` only in `verify.rs`; `minisign ::` in the signer and two test files); `download_verify_findings` covers only `download.rs` / `verify.rs`; FR-084's write tokens cover only `detect.rs`, `notes.rs`, `status.rs`, `platform/` | `injection.rs:2843-2856`, `:2885-2945`, `:2364-2422` |
| `core_dependencies_are_the_fr007_set` asserts dev `{flate2, minisign}` | `injection.rs:3353-3365` |
| `flate2` already used by test sources `download_tests.rs:56`, `http_tests.rs:111`, `fixture_server_tests.rs:4-5` | `grep -rn flate2` this session |
| `FR084_TOKENS` (19, incl. `remove_dir`); `TEST_PROGRAM_BODIES` (4; the `cmd` rows of `listing_skips_junction_windows` and `staged_links_are_not_followed_windows`); `spanned_tokens` splits on non-`[A-Za-z0-9_]` (whole-identifier tokens); `seq_at` exact token sequences | `injection.rs:223-243`, `:396-409`, `:758-778`, `:803-813` |
| Phase 7 junction test: `staged_links_are_not_followed_windows` (`#[cfg(windows)]`) makes junctions with `cmd /C mklink /J` on plain (non-`\\?\`) paths inside the test's base; Windows `remove_link` = `remove_file` then `remove_dir_all` | `download_tests.rs:2949-2965`; `download.rs:734-739` |
| Node C4 in-memory rule table: helpers `f` / `d` / `l` (`l` mode 0777); 33 reject rows; accept rows (in-bundle links, D3 normalisation, UTF-8 byte sort) | `tools/feed-contract/tests/c4-listing-tar.test.js:103-105`, `:196-231`, `:236-280` |
| `RESERVED_STEM_PATTERN` (CON PRN AUX NUL COM0-9 LPT0-9, optional `.ext`); private `reserved_stem_re` | `contract.rs:138`, `:1024-1027` |
| Production temp files use the `_in(dir)` form | `feed.rs:716-718`, `settings.rs:250-252` |
| `manager-ci.yml`: clippy-guard `--locked` step; `cargo test --workspace --locked \| tee`; core dependency tree; `required tests ran` (each name of `cargo.all` + the leg's lane exactly once) | `.github/workflows/manager-ci.yml:100-114`, `:117-120`, `:122-136`, `:153-178` |
| `required-tests.json`: `cargo.all` 224, `windows` 10, `macos` 28, `linux` 25, `vitest` 29 | `node -e` this session |
| `EXPECTED_REQUIRED` deep-compared with `required-tests.json` | `tools/feed-contract/tests/manager-ci-shape.test.js:337`, `:816` |
| `listing.js` rule functions, tar reader, gzip step, layout check (every line cited in §4.3) | `tools/feed-contract/listing.js:31-382` |
| C4 rules in check order, archive errors, Phase 8 obligations | `tools/feed-contract/CONTRACT.md:310-337`, `:335-337`, `:339-347` |
| Fixture facts: 9 good + KAT, 22 bad with `.expected-error`; links in `exec-and-symlink`, `long-link`, `pax-path` only; every good moduleinfo `{"Name": "Alpha", "Version": "1.2.0", "Classes": [{"Name": "Alpha", "Version": "1.2.0"}]}`; KAT moduleinfo `1.2.3`; KAT archive sha256 `5b980e5b…b62890`, its `.sha256` file = listing digest `db287e49…`; `real-shaped.tar.gz` header `1f 8b 08 00 00 00 00 00 02 03` (FLG 0, MTIME 0) | `tools/feed-contract/fixtures/listings/**`, `fixtures/kat/README.md:10`; `tar -xzO`, `sha256sum`, `od` this session |

### 1.2 External facts (verified 2026-11-01)

| Fact | Source |
|---|---|
| `flate2` 1.1.10: `bufread::GzDecoder::new(r)` "Creates a new decoder from the given reader, immediately parsing the gzip header"; "After reading a single member of the gzip data this reader will return Ok(0) even if there are more bytes available in the underlying reader … call `into_inner()` after Ok(0) to recover the underlying reader"; `get_mut`, `into_inner`, `header`, `reset`. In source: `new` parses the header from `r` (`src/gz/bufread.rs:235-254`); `Read` reads the 8-byte trailer through the inner reader and returns `corrupt()` on a CRC / ISIZE mismatch, then `End` when not multi (`:306-354`); a short trailer is `UnexpectedEof` (`src/gz/mod.rs:252-260`); `corrupt()` / `bad_header()` are `InvalidInput` (`mod.rs:288-296`); default features `rust_backend`, `runtime_detection` (`Cargo.toml:62-65`); `rust-version` 1.67.0 | https://docs.rs/flate2/1.1.10/flate2/bufread/struct.GzDecoder.html ; local registry copy `flate2-1.1.10` |
| `tempfile` 3.27.0: `tempdir()` and `TempDir::new()` create "inside of `env::temp_dir()`" (`src/dir/mod.rs:48`, `:64`, `:187`), the real system temp dir; `Builder::prefix` (`src/lib.rs:326`), `Builder::tempdir_in` (`:621`), `NUM_RAND_CHARS = 6` (`:195`), alphanumeric names (`src/util.rs:15`), retries on collision (`util.rs:39-67`); `TempDir::keep(self) -> PathBuf` disables cleanup (`src/dir/mod.rs:418-421`); `impl Drop for TempDir` removes unless disabled (`:498-503`) | local registry copy `tempfile-3.27.0`; https://docs.rs/tempfile/3.27.0/tempfile/struct.Builder.html |
| `std::fs::remove_dir_all` "does **not** follow symbolic links and it will simply remove the symbolic link itself"; protects against symlink TOCTOU races on most platforms (not Miri, QNX, Redox, VxWorks) | https://doc.rust-lang.org/std/fs/fn.remove_dir_all.html (std 1.99.1) |
| `OsStr::as_encoded_bytes(&self) -> &[u8]`, stable 1.74.0 | https://doc.rust-lang.org/std/ffi/struct.OsStr.html#method.as_encoded_bytes (std 1.99.1) |
| `Seek::rewind(&mut self) -> Result<(), Error>`, stable 1.55.0, "equivalent to `seek(SeekFrom::Start(0))`" | https://doc.rust-lang.org/std/io/trait.Seek.html#method.rewind |
| `OpenOptions::create_new` (atomic, refuses an existing file or dangling link); `fs::canonicalize` (`realpath` / `GetFinalPathNameByHandle`); `std::os::unix::fs::symlink`; `PermissionsExt::from_mode` | as cited in the spec's External facts (`spec.md:131-134`), std 1.99.1 |
| Toolchain `rustc 1.99.1 (58cf8f9c3 2026-10-13)`, `cargo 1.99.1`; `rust-toolchain.toml` channel `stable`; workspace `rust-version = "1.90"` (every API above is older) | `rustc --version`, `cargo --version` this session; `apps/krate-manager/rust-toolchain.toml`; `Cargo.toml:9` |

---

## 2. File layout

| File | Change |
|---|---|
| `apps/krate-manager/crates/core/src/extract.rs` | 🆕 the whole module (§4.3) |
| `apps/krate-manager/crates/core/src/extract_tests.rs` | 🆕 `#[cfg(test)] #[path = "extract_tests.rs"] mod tests;` (the inline-module ban, `injection.rs:1182-1187`); the ustar / gzip builder; every SC test (§7) |
| `crates/core/src/verify.rs` | 🔶 `pub(crate) struct Sha256Stream` (§4.2) |
| `crates/core/src/contract.rs` | 🔶 `fn reserved_stem_re` → `pub(crate) fn reserved_stem_re` (`:1024`), so FR-041's Windows branch (§4.3.8) uses the one reserved-stem pattern; nothing else |
| `crates/core/src/detect.rs` | 🔶 `fn moduleinfo_version` → `pub(crate) fn moduleinfo_version`; nothing else |
| `crates/core/src/lib.rs` | 🔶 `pub mod extract;` (alphabetical, after `pub mod download;`) |
| `crates/core/src/test_support.rs` | 🔶 `MemExtractIo`, `FaultyExtractIo`, `RecordingExtractIo` (forwarding and fail-closed modes), `snapshot`, `temp_roots_via_link`, `temp_roots_raw` (§4.5) |
| `crates/core/Cargo.toml` | 🔶 `flate2` from `[dev-dependencies]` to `[dependencies]` with the comment `# Phase 8 D1: extract.rs only (FR-082)` |
| `apps/krate-manager/Cargo.toml` | 🔶 comment above the existing `flate2` pin: `# Phase 8 D1: core's extract.rs (gzip); the tar reader is in-crate` (no version change) |
| `apps/krate-manager/tests/fixtures/clippy-guard/Cargo.lock` | 🔶 re-locked (SI-3) |
| `crates/core/tests/injection.rs` | 🔶 §4.6 |
| `apps/krate-manager/tests/required-tests.json` | 🔶 FR-086's 32 names |
| `tools/feed-contract/tests/manager-ci-shape.test.js` | 🔶 `EXPECTED_REQUIRED` gains the same 32 names |

No other file changes (FR-090, FR-091): no `src-tauri`, no webview, no workflow, no `tools/feed-contract/` contract,
schema, `listing.js` or fixture.

---

## 3. Error model

```rust
/// The C4 codes Phase 8 reuses from `listing.js` (D9, `CONTRACT.md:310-337`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ListingCode {
    Path, Absolute, Drive, Backslash, Dotdot, Type, RootSymlink, SymlinkForm, SymlinkEscape,
    ThroughSymlink, Duplicate, SymlinkChain, Checksum, Truncated, Corrupt,
}
impl ListingCode { pub fn as_str(self) -> &'static str { /* "E_LISTING_PATH", … */ } }

#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum ExtractError {
    #[error("E_EXTRACT_ENTRY_MISMATCH: the feed entry and the artifact differ in {field}")]
    EntryMismatch { field: &'static str },
    #[error("{}: {detail}", .code.as_str())]
    Listing { code: ListingCode, detail: String },          // detail names the archive entry, never a disk path
    #[error("E_LISTING_COUNT: the feed says {expected} listing lines, the archive has {found}")]
    Count { expected: u64, found: u64 },
    #[error("E_LISTING_DIGEST: expected {expected}, found {found}")]
    Digest { expected: String, found: String },              // lower-case hex
    #[error("E_LAYOUT: {item}")]
    Layout { item: String },
    #[error("E_EXTRACT_LIMIT: the {which} bound was crossed")]
    Limit { which: &'static str },                           // headers tar_bytes meta path segment joined entries moduleinfo
    #[error("E_EXTRACT_CHANGED: the archive differs from the verified bytes")]
    Changed,
    #[error("E_EXTRACT_UNSUPPORTED: {entry} is a symlink and this filesystem cannot hold one")]
    Unsupported { entry: String },
    #[error("E_EXTRACT_PATH: {reason}")]
    Path { reason: &'static str },
    #[error("E_EXTRACT_COLLISION: {entry} already exists in the work directory")]
    Collision { entry: String },
    #[error("E_EXTRACT_ESCAPE: {path} resolves outside the work directory")]
    Escape { path: String },                                 // a listing path, "<work dir>" or "extract"
    #[error("E_EXTRACT_TREE_MISMATCH: first differing line {line:?}")]
    TreeMismatch { line: String },
    #[error("E_EXTRACT_VERSION: expected {expected}, the staged moduleinfo says {found}")]
    Version { expected: String, found: String },
    #[error("E_EXTRACT_FULL: step {step} on {entry}")]
    Full { step: &'static str, entry: String },
    #[error("E_EXTRACT_DENIED: step {step} on {entry}")]
    Denied { step: &'static str, entry: String },
    #[error("E_EXTRACT_IO: step {step} on {entry}: {kind:?}")]
    Io { step: &'static str, kind: io::ErrorKind, entry: String },
}
impl ExtractError { pub fn code(&self) -> &'static str { /* the prefix of Display */ } }
```

- Every `Display` begins with its code (D9); tests assert `code()` only.
- `entry` / `path` / `item` / `line` hold archive-relative listing paths (or the fixed words `<work dir>`, `extract`,
  `<archive>`), never an absolute path: FR-052's "no absolute path outside the state dir" holds for errors too.
- FR-045 mapping, in `stage` only (`fn io_error(step, entry, e)`): `StorageFull`, `QuotaExceeded` → `Full`;
  `PermissionDenied`, `ReadOnlyFilesystem` → `Denied`; `AlreadyExists` from `create_dir` / `create_file` →
  `Collision`; every other kind → `Io`. Steps: `open_archive`, `read_archive` (PD-4), `create_work_dir`, `create_dir`,
  `create_file`, `write`, `set_mode`, `symlink`, `kind_of`, `read_tree`, `canonicalize`, `read_file`, `remove`.
- Size: the largest variant is two `String`s plus a tag (≈ 56 bytes), under clippy's `result_large_err` default of
  128 bytes, so no boxing is needed.

---

## 4. Module design

### 4.1 Manifests and lockfiles (FR-080, FR-081; SI-3)

```toml
# crates/core/Cargo.toml [dependencies], alphabetical
# Phase 8 D1: extract.rs only (FR-082)
flate2 = { workspace = true }
```
`[dev-dependencies]` keeps only `minisign` (with its Phase 7 comment). Then, from `apps/krate-manager/`:
`cargo update --workspace --manifest-path tests/fixtures/clippy-guard/Cargo.toml` (only the `krate-manager-core`
dependency list changes; `git diff` of that lock must show one added line `"flate2",`) and `cargo metadata --locked
--format-version 1 > /dev/null` for the workspace (no change expected; `Cargo.lock:2049-2054` already lists it).

### 4.2 `verify.rs` (FR-082)

```rust
/// A streaming SHA-256 for `extract.rs` (Phase 8 FR-082): `sha2` stays confined to this file.
pub(crate) struct Sha256Stream(Sha256);
impl Sha256Stream {
    pub(crate) fn new() -> Sha256Stream;
    pub(crate) fn update(&mut self, bytes: &[u8]);
    pub(crate) fn finish(self) -> [u8; 32];
}
```
`verify_file` is not changed (`sha256_of` stays as it is; Phase 7 tests use it).

### 4.3 `extract.rs`

#### 4.3.1 Constants, limits, env

```rust
pub const LOG_TARGET: &str = "krate_manager_core::extract";
pub const EXTRACT_DIR: &str = "extract";
const BLOCK: usize = 512;
const CHUNK: usize = 64 * 1024;           // file copy and raw read buffer
const RANDOM_LEN: usize = 6;              // tempfile NUM_RAND_CHARS (PD-9)

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ExtractLimits {
    pub max_headers: u64, pub max_tar_bytes: u64, pub max_meta_bytes: u64,
    pub max_path_bytes: usize, pub max_segment_bytes: usize, pub max_joined_bytes: usize,
    pub max_moduleinfo_bytes: u64,
}
impl ExtractLimits {
    pub const PRODUCTION: ExtractLimits = ExtractLimits {
        max_headers: 100_000, max_tar_bytes: 4 << 30, max_meta_bytes: 64 << 10,
        max_path_bytes: 4096, max_segment_bytes: 255, max_joined_bytes: 4095,
        max_moduleinfo_bytes: crate::detect::MAX_MODULEINFO_BYTES,   // 1 MiB, one source
    };
}

#[derive(Clone, Copy)]
pub struct ExtractEnv<'a> { pub ctx: &'a AppContext, pub io: &'a dyn ExtractIo, pub limits: &'a ExtractLimits }
```

#### 4.3.2 `ExtractIo` and `FsExtractIo` (D4, D5; PD-3, PD-6, PD-7, PD-10)

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NodeKind { Dir, File, Symlink, Other }          // never following a link

pub trait ArchiveRead: Read { fn rewind(&mut self) -> io::Result<()>; }
pub trait ExtractFile { fn write_all(&mut self, bytes: &[u8]) -> io::Result<()>; }

/// Every filesystem step of Phase 8 (D5). No method maps to an `E_*` code; `stage` does (FR-045).
pub trait ExtractIo {
    fn open_archive(&self, path: &Path) -> io::Result<Box<dyn ArchiveRead>>;
    /// `NotFound` → `Ok(None)`; the entries of a directory, never following links.
    fn list_dir(&self, dir: &Path) -> io::Result<Option<Vec<(OsString, NodeKind)>>>;
    /// Creates `extract` if absent, requires it to be a directory, then `<extract>/<prefix><6 random>`.
    fn create_work_dir(&self, extract: &Path, prefix: &str) -> io::Result<PathBuf>;
    fn create_dir(&self, path: &Path) -> io::Result<()>;               // never create_dir_all
    fn create_file(&self, path: &Path) -> io::Result<Box<dyn ExtractFile>>; // write + create_new
    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()>;     // no-op on non-Unix FsExtractIo
    fn symlink(&self, target: &str, link: &Path) -> io::Result<()>;
    fn supports_symlinks(&self) -> bool;
    fn remove_tree(&self, path: &Path) -> io::Result<()>;
    fn remove_entry(&self, path: &Path, kind: NodeKind) -> io::Result<()>; // a link as the link itself
    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>>;   // symlink_metadata
    fn read_tree(&self, root: &Path) -> io::Result<Vec<ListingEntry>>; // relative names as bytes, no follow
    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf>;
    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>>; // reads at most max + 1 bytes
}
pub struct FsExtractIo;
```

`impl ExtractIo for FsExtractIo` (the only place FR-083's mutating tokens appear):

| Method | Body | Linux / macOS (`cfg(unix)`) | Windows (`cfg(not(unix))`) |
|---|---|---|---|
| `open_archive` | `File::open`; `ArchiveRead::rewind` = `Seek::rewind` | same | same |
| `list_dir` | `fs::read_dir` + `DirEntry::file_type()` (no follow) | same | a junction is not `is_dir()` → `Symlink`/`Other` (Phase 7 PD-19) |
| `create_work_dir` | `fs::create_dir_all(extract)`, then `symlink_metadata(extract).is_dir()` else `InvalidInput` (PD-6), then `tempfile::Builder::new().prefix(prefix).tempdir_in(extract)?.keep()` | dir mode 0700 from tempfile (the root is not listed) | same |
| `create_dir` | `fs::create_dir` | — | — |
| `create_file` | `OpenOptions::new().write(true).create_new(true).open` | — | case-insensitive NTFS → `AlreadyExists` for a case twin |
| `set_mode` | `fs::set_permissions(path, Permissions::from_mode(mode))` | exact, umask-independent (FR-043) | `Ok(())`, nothing applied (D4) |
| `symlink` | `std::os::unix::fs::symlink(target, link)` | real link | `Err(Unsupported)` (never reached: `stage` refuses first, FR-042) |
| `supports_symlinks` | | `true` | `false` |
| `remove_tree` | `fs::remove_dir_all` (does not follow links) | same | same |
| `remove_entry` | `Dir` → `remove_dir_all`; `File` → `remove_file`; `Symlink` / `Other` → `remove_file`, Windows fallback `remove_dir_all` (PD-7) | | fallback |
| `kind_of` | `fs::symlink_metadata`, `NotFound` → `None` | same | same |
| `read_tree` | recursive `read_dir` + `symlink_metadata` + `read_link`; names via `OsStrExt::as_bytes` | `mode: Some(meta.mode() & 0o7777)` | names via `to_str()` (a non-UTF-8 name is `InvalidData`), `mode: None` |
| `canonicalize` | `fs::canonicalize` | `realpath` | `\\?\` form on both sides, so comparisons agree |
| `read_file` | `File::open(path)?.take(max + 1).read_to_end` | same | same |

Nothing in the impl sets an owner, group, timestamp or extended attribute, and no `sync_*` call exists (FR-043, FR-055).

#### 4.3.3 The raw reader and the gzip member loop (FR-010, FR-044; D1, D6; PD-4)

Layering for each pass, bottom to top:

1. `Box<dyn ArchiveRead>` (one handle, opened once; pass 2 calls `rewind`).
2. `Hashed<'h>`: `Read` over `&mut dyn ArchiveRead` that feeds every byte to a `Sha256Stream` and records the
   first non-`Interrupted` source error kind (`Cell<Option<io::ErrorKind>>`); owned by `stage`, so the hash is read
   after `plan_archive` returns.
3. `BufReader::with_capacity(CHUNK, …)` (gives the `BufRead` the bufread decoder needs and keeps the member boundary
   in its buffer).
4. `GzMembers<R: BufRead>` (`Read`), the member loop:
   - `state = Member(bufread::GzDecoder<R>)` built with `GzDecoder::new(inner)` (header parsed at once,
     `bufread.rs:238-254`); a read returning `Ok(0)` means the member ended with a correct CRC and ISIZE.
   - On `Ok(0)`: `inner = decoder.into_inner()`; `fill_buf()`: empty → `Done`; first byte non-zero → a new member
     starts there (`GzDecoder::new(inner)`; a bad header is an `InvalidInput` error → `E_LISTING_CORRUPT`); first byte
     zero → consume to EOF, every byte must be zero (each counted against `max_tar_bytes`, FR-010), else
     `E_LISTING_CORRUPT`; then `Done`. This is FR-010's literal rule ("a remainder of only zero bytes"); `00 00 01` is
     refused, the measured residual difference from Node (SC-031).
   - Any error (from the decoder, `into_inner` / `fill_buf` or the padding scan) with the tap's source error set →
     FR-045's mapping with step `read_archive`, entry `<archive>` and the **tap's recorded kind** (so a source
     `PermissionDenied` is `Denied`, `StorageFull` cannot occur on a read, any other kind is `Io`); any other error
     (bad magic, bad header, CRC / ISIZE mismatch, an `UnexpectedEof` inside the deflate stream or the 8-byte
     trailer, `mod.rs:252-260`) → `Listing { code: Corrupt, .. }`. Tested by SC-031's gzip-layer rows and SC-010
     (iv) (§7.1).
5. `Budget`: counts decompressed bytes and padding; the byte that would exceed `max_tar_bytes` stops reading with
   `Limit { which: "tar_bytes" }` before it is passed up (a gzip bomb stops at the bound, D8).

After the end-of-archive block the tar reader drains the stream to `Done` (counted, never interpreted,
`listing.js:228-232`), so the last CRC is checked and the raw file is read to EOF (D6).

#### 4.3.4 The tar header reader (FR-011 to FR-016; a port of `listing.js:163-274`)

```rust
struct TarReader<R: Read> { src: R, limits: ExtractLimits, headers: u64, pending: Pending, ended: bool }
#[derive(Default)] struct Pending { long_name: Option<Vec<u8>>, long_link: Option<Vec<u8>>,
                                    pax_path: Option<Vec<u8>>, pax_linkpath: Option<Vec<u8>> }
struct RawEntry { kind: EntryType, mode: u32, size: u64, path: Vec<u8>, target: Option<Vec<u8>> }
impl<R: Read> TarReader<R> {
    fn next_entry(&mut self) -> Result<Option<RawEntry>, ExtractError>; // None after the end block + drain
    fn copy_data(&mut self, size: u64, sink: &mut dyn FnMut(&[u8]) -> Result<(), ExtractError>) -> Result<(), ExtractError>;
    fn skip_data(&mut self, size: u64) -> Result<(), ExtractError>;     // plus padding to the 512 boundary
}
```

Per header, in `listing.js`'s order:
1. Read 512 bytes. 0 bytes at EOF → not ended → `Truncated` ("before the end-of-archive block", `:271`). A short
   block (1..511 bytes, zero or not) → `Truncated` (`:228-233`). A full all-zero block → `ended`, drain, `None`.
2. `headers += 1`; above `max_headers` → `Limit { "headers" }` (counts L/K/x/g records too, D8).
3. Checksum over bytes 0..148, eight spaces, 156..512; stored field parsed by `^ *([0-7]+)[\0 ]*$` (latin-1);
   accepted if it equals the unsigned **or** the signed sum, else `Checksum` (`:177-191`; an unparseable field
   never matches).
4. `mode` (100..108) and `size` (124..136): high bit of the first byte → `Corrupt` (base-256); else the same octal
   pattern, else `Corrupt` (`:169-175`). Parsed into `u64`.
5. Typeflag byte 156.
6. `L`, `K`, `x`, `g`: `size > max_meta_bytes` → `Limit { "meta" }` before reading; read exactly `size` bytes (EOF
   first → `Truncated`, `:239`), then padding. `L` → `pending.long_name = cut_at_nul(data)`; `K` → `long_link`;
   `x` → `pax_records(data)` (a port of `:194-213`: decimal length, `end > len || end <= sp + 1 || data[end-1] != LF`
   → `Corrupt`, no `=` → `Corrupt`; keys decoded lossily; last value wins), and only `path` / `linkpath` set
   `pending` (every other key, `SCHILY.xattr.*` included, is dropped here, FR-013); `g` → ignored. Continue.
7. Any other typeflag: name = `cut_at_nul(h[0..100])`; with magic `h[257..263] == b"ustar\0"` and version
   `h[263..265] == b"00"` and a non-empty `cut_at_nul(h[345..500])`, name = `prefix + "/" + name` (`:258-262`);
   `path = pax_path ?? long_name ?? name` and, for typeflag `2`, `target = pax_linkpath ?? long_link ??
   cut_at_nul(h[157..257])`, where `??` is **presence**, not non-emptiness (JS `||` on a `Buffer` is truthy even when
   empty, `:264-265`). Kind: `'0'` or `NUL` → File, `'5'` → Dir, `'2'` → Symlink, anything else → `Other(flag)`
   (`:215`). `pending` is reset. The path and target are bounded here: more than `max_path_bytes` → `Limit { "path" }`,
   a `/`-segment of the path over `max_segment_bytes` → `Limit { "segment" }` (FR-015; target segments unbounded).
8. At EOF with `pending` holding any of its four fields set → `Truncated` (`:272`); an `x` record with neither `path`
   nor `linkpath` sets nothing, exactly as `Object.keys(pending)` stays empty in Node.

Data running past the stream end (`copy_data` / `skip_data` reaching EOF before `size` bytes) → `Truncated`; EOF inside
the padding is left to the next header read, which reports `Truncated` too (Node: `off` passes `tar.length`, `:226`,
`:271`). Owner, group, names, mtime and device fields are never parsed (FR-016).

#### 4.3.5 The rule engine and `CanonicalListing` (FR-017 to FR-022; PD-11)

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EntryType { File, Dir, Symlink, Other(u8) }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ListingEntry { pub kind: EntryType, pub mode: Option<u32>, pub path: Vec<u8>, pub target: Option<Vec<u8>> }

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CanonicalListing { lines: Vec<String> }
impl CanonicalListing {
    pub fn from_entries(entries: &[ListingEntry]) -> Result<CanonicalListing, ExtractError>;
    pub fn lines(&self) -> &[String];
    pub fn text(&self) -> String;          // every line + '\n'
    pub fn files(&self) -> u64;            // line count
    pub fn digest(&self) -> [u8; 32];      // Sha256Stream over text()
}
```

Private functions, each a port:
- `canonical_path(&ListingEntry) -> Result<Option<String>, ExtractError>` (`:56-72`): `str::from_utf8` (fatal, a BOM
  kept) else `Path`; contains `" -> "` → `Path`; `check_characters` (`:47-53`: empty, NUL/CR/LF → `Path`; leading `/`
  → `Absolute`; `^[A-Za-z]:` → `Drive`; `\` → `Backslash`); strip one `./`; a `Dir` whose rest is `""` or `"."` →
  `Ok(None)` (the root, dropped and not counted); a `Dir`'s single trailing `/` dropped; any empty / `.` / `..`
  segment → `Dotdot`; kind `Other` → `Type`.
- `check_link(path, raw_target) -> Result<(String, String), ExtractError>` (`:75-95`): one segment → `RootSymlink`;
  decode + `check_characters` on the target; form rule (leading `..` run then ≥ 1 plain segment) → `SymlinkForm`;
  lexical resolution of `dirname(path)/target` with a segment stack (`..` pops, or stays as a leading `..` when the
  stack is empty), result `.` / starting `..` / first segment ≠ the link's first segment / fewer than two segments →
  `SymlinkEscape`. Returns `(target, resolved)`.
- `rule3(items) -> Result<Vec<String>, ExtractError>` (`:121-158`): (a) through-symlink over proper prefixes, (b)
  duplicates in item order then implied ancestors (`d`, mode `0755`; a non-dir hit → `Duplicate`), (c) chain over
  `properPrefixes(resolved) + [resolved]`; then lines sorted by `String` order (byte order, as `Buffer.compare`),
  `"<t> <mmmm> <path>"` / `"l 0777 <path> -> <target>"`, mode normalised by `mode_of` (`:104-108`; a `None` mode on a
  file is treated as 0, `0644`).
- `from_entries` = rule 1 for every entry, then rule 2 for every link, then `rule3` (exactly Node's three loops).

#### 4.3.6 `plan_archive` (FR-009, FR-015; PD-1)

```rust
pub(crate) struct ArchivePlan { pub(crate) entries: Vec<PlanEntry>, pub(crate) listing: CanonicalListing }
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct PlanEntry { pub(crate) path: String, pub(crate) kind: PlanKind }
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum PlanKind { File { size: u64, exec: bool }, Dir, Symlink { target: String } }

pub(crate) fn plan_archive(reader: impl Read, limits: &ExtractLimits, max_entries: u64)
    -> Result<ArchivePlan, ExtractError>;
```

Loop over `TarReader::next_entry` (skipping data): for each entry run `canonical_path`; `Ok(None)` → skip (root);
`Err` → record as `first_rule1` if empty, skip; for a link run `check_link`, `Err` → `first_rule2` if empty, skip;
otherwise it is counted: `counted += 1`, and `counted > max_entries` stops reading with `Limit { "entries" }`; else it is
pushed. Stopping errors and the end resolve as PD-1. The entries are in archive order; the listing is `rule3` over
them. Pure: no `ExtractIo`, no payload, no layout. Tests call it with a `&[u8]` reader (SC-002, SC-003, SC-031).

#### 4.3.7 `check_staged_layout` (FR-030 to FR-033)

```rust
pub(crate) fn check_staged_layout(listing: &CanonicalListing, payload: &Payload, vst3_bundle: &PlainName)
    -> Result<(), ExtractError>;
```
Order: `files()` vs `payload.files` → `Count`; `digest()` vs `payload.sha256` → `Digest`; then the six required items
and "no other root item" of `listing.js:359-382` over the parsed lines (path → type) → `Layout { item }` naming the
item (`NOTES.txt`, `presets`, …), `<stem>` = bundle minus `.vst3`.

#### 4.3.8 `entry_path` and the joined bound (FR-041, FR-059; PD-9)

```rust
pub(crate) fn entry_path(work: &Path, rel: &str) -> Result<PathBuf, ExtractError>;
```
`rel` non-empty; split on `/`; every segment non-empty, not `.` / `..`, holding no `\`, `:` or NUL, and
`Path::new(seg).components()` exactly one `Component::Normal` (on Windows `C:` parses as `Prefix`); join each; the
result must satisfy `p != work && p.starts_with(work)`; else `Path { reason }`. On `cfg(windows)` only, a segment
whose name matches `contract::reserved_stem_re()` (CON, PRN, AUX, NUL, COM0-9, LPT0-9, with or without an extension,
`contract.rs:138`) or that ends in `.` or a space is also `Path { reason }`: Win32 maps such a name to a device or
strips the trailing character, so the created path would differ from the listed one. These names are legal on
Linux and in C4 (no parity change); the production extraction target is Linux only (D4), and the Windows
`FsExtractIo` exists for the Windows test leg, which must never write to a device or to a different name. Because
FR-059's `joined_len` calls `entry_path` over every listing line at step 6, such an archive is refused on Windows
before any write. `fn joined_len(staging: &Path, hex:
&str, rel: &str) -> Result<usize, ExtractError>` = `entry_path(&staging.join(EXTRACT_DIR).join(format!("{hex}.{}",
"x".repeat(RANDOM_LEN))), rel)?.as_os_str().as_encoded_bytes().len()`, over every listing line; the maximum above
`max_joined_bytes` → `Limit { "joined" }`.

#### 4.3.9 Live registry and `prune` (D2, FR-049, FR-058; PD-2, PD-6, PD-7)

```rust
static LIVE: Mutex<BTreeSet<PathBuf>> = Mutex::new(BTreeSet::new());
fn live() -> MutexGuard<'static, BTreeSet<PathBuf>>;        // poison → into_inner (download.rs:648-650)
struct LiveRoot { path: PathBuf }                           // Drop: live().remove(&path); also on unwinding
pub fn prune(env: ExtractEnv<'_>) -> usize;
fn prune_locked(io: &dyn ExtractIo, extract: &Path, live: &BTreeSet<PathBuf>) -> usize;
```

`prune_locked`: `kind_of(extract)`: `None` → 0, `Some(Dir)` → continue, else warn and 0 (PD-6). `list_dir(extract)`
(`None` → 0; `Err` → warn, 0). For each `(name, kind)` whose `extract.join(name)` is not in `live`: `Dir` →
`remove_tree`, `File` → `remove_entry(File)`, `Symlink` / `Other` → `remove_entry(kind)` (a link as itself, PD-7); a
failure (other than `NotFound`) is a `warn!` naming the entry name and kind (relative, FR-052) and the loop goes on;
the count of successful removals is returned. `prune(env)` = lock, `prune_locked`, unlock. The registry is
process-wide; a test's prune only lists its own `extract/`, so parallel tests only serialise these short sections.

#### 4.3.10 `stage` (FR-001 to FR-059)

```rust
pub fn stage(env: ExtractEnv<'_>, entry: &PluginEntry, artifact: &VerifiedArtifact) -> Result<StagedTree, ExtractError>;
```
`stage` = `stage_body` + one `info!` line (§4.3.12). `stage_body`, in order:

1. **Binding (FR-002), no I/O.** `format == TarGz` (`format`); `entry.name == artifact.name()` (`name`);
   `entry.version_text == artifact.version()` (`version`); `entry.release` (`release`); `platforms[artifact.platform()]`
   (`platform`); its `asset_name`, `size`, `sha256`, `format` (`asset`, `size`, `sha256`, `format`); its `payload`
   (`payload`); `release.install` (`install`); `contract::check_install_value("vst3_bundle", &Value::String(..))` and one
   `Component::Normal` (`install`). Keeps `payload`, a clone of `vst3_bundle`, `hex = hex_lower(artifact.sha256())`.
2. **Open.** `io.open_archive(artifact.path())` (step `open_archive`).
3. **Pass 1.** `Hashed` over the handle → `plan_archive(BufReader…, env.limits, payload.files)`; then
   `hashed.finish() != *artifact.sha256()` → `Changed` (FR-044: a reading error already returned with its own code).
4. **Layout.** `check_staged_layout(&plan.listing, &payload, &bundle)` (FR-030 to FR-033).
5. **Symlinks.** A `Symlink` in the plan and `!io.supports_symlinks()` → `Unsupported { entry }` (FR-042).
6. **Joined bound.** FR-059 (§4.3.8).
7. **Sandbox and critical section (PD-2, PD-10).** `extract = staging_dir().join(EXTRACT_DIR)`;
   `ctx.roots.check_sandboxed(&extract)`; lock `LIVE`; `prune_locked`; `create_work_dir(&extract, &format!("{hex}."))`
   (step `create_work_dir`); insert; unlock; `guard = LiveRoot { path: work }`. Nothing before this step writes.
8. **Work-dir check (FR-040).** `kind_of(work) == Some(Dir)` and `canonicalize(work)` strictly under
   `canonicalize(staging_dir())`, else `Escape { path: "<work dir>" }`.
   From here every error runs `cleanup(io, &work)`: `remove_tree`, a failure logged as one `warn!` naming the step
   `remove` and the error kind (no path), then returns the **original** error, never the cleanup's (FR-045, the
   spec's edge case "Cleanup after an error fails"; SC-010 (iii)); `guard` drops on return and deregisters, so the
   leftover is a non-live entry the next prune removes.
9. **Directories (FR-042, FR-043).** For each `d` line in listing order: `entry_path`, `create_dir`, `set_mode(0o755)`.
10. **Pass 2 (FR-044, FR-056).** `rewind` (step `read_archive`); a new `Hashed` + `GzMembers` + `TarReader`; the same
    per-entry canonicalisation as pass 1, compared with `plan.entries[i]` (kind, path, normalised mode, size, target)
    **on the header, before any `create_file` or `write` for that entry**, so an entry the plan does not hold is never
    created and no file receives more than its planned size; any parse error (a gzip, tar or rule error, whatever
    `E_LISTING_*` code pass 1 would give it), missing / extra / differing entry → `Changed` (SC-009 (e1), (e2), (f)).
    A source error (the tap is set, PD-4) maps as in pass 1, step `read_archive` (SC-010 (iv)). `File`: `create_file(entry_path)`, `copy_data` in
    `CHUNK` slices into `ExtractFile::write_all` and a per-file `Sha256Stream` in the same loop, drop the file,
    `set_mode(0o755 | 0o644)`; push `(path, digest)`. `Dir`: already created. `Symlink`: deferred. After the stream:
    raw hash ≠ `artifact.sha256()` → `Changed`.
11. **Symlinks last (FR-042).** `io.symlink(target, &entry_path(work, path)?)`.
12. **Physical post-check (FR-046).** `cw = canonicalize(work)`; per line: `f` / `d` → `canonicalize(entry_path)`
    strictly under `cw`; `l` → `canonicalize(parent)` under `cw`, then `canonicalize(link)`: `Ok(p)` must be strictly
    under `cw.join(top)`, `NotFound` → dangling (allowed), other error → `Io { step: "canonicalize" }`; a failure →
    `Escape { path: <listing path> }`.
13. **Tree listing (FR-047).** `read_tree(work)` → fill each `None` mode from the plan's normalised mode for that
    path → `CanonicalListing::from_entries` (an `Err` → `TreeMismatch` with its message's path) → compare with
    `plan.listing` line by line → `TreeMismatch { line }` (the first differing line of either side; a length
    difference names the first extra line).
14. **Version (FR-057, PD-5).**
15. **Result (FR-048).** `StagedTree { root: work, extract_dir: extract, name, version, vst3_bundle, listing,
    archive_sha256, file_hashes, live: guard }`.

#### 4.3.11 `StagedTree` and `discard` (FR-050, FR-051, FR-054; PD-8)

```rust
/// ```compile_fail
/// let _: krate_manager_core::extract::StagedTree = Default::default();
/// ```
/// ```compile_fail
/// fn forge() -> krate_manager_core::extract::StagedTree {
///     krate_manager_core::extract::StagedTree {
///         root: std::path::PathBuf::new(), extract_dir: std::path::PathBuf::new(),
///         name: String::new(), version: String::new(), vst3_bundle: todo!(), listing: todo!(),
///         archive_sha256: [0; 32], file_hashes: Vec::new(), live: todo!(),
///     }
/// }
/// ```
/// ```no_run
/// fn read(t: &krate_manager_core::extract::StagedTree) {
///     let _ = (t.root(), t.bundle(), t.presets(), t.name(), t.version(), t.vst3_bundle(),
///              t.listing(), t.archive_sha256(), t.file_hashes());
/// }
/// ```
#[derive(Debug)]
pub struct StagedTree {
    root: PathBuf, extract_dir: PathBuf, name: String, version: String, vst3_bundle: PlainName,
    listing: CanonicalListing, archive_sha256: [u8; 32], file_hashes: Vec<(String, [u8; 32])>, live: LiveRoot,
}
impl StagedTree {
    pub fn root(&self) -> &Path;  pub fn bundle(&self) -> PathBuf;  pub fn presets(&self) -> PathBuf;
    pub fn name(&self) -> &str;   pub fn version(&self) -> &str;    pub fn vst3_bundle(&self) -> &PlainName;
    pub fn listing(&self) -> &CanonicalListing; pub fn archive_sha256(&self) -> &[u8; 32];
    pub fn file_hashes(&self) -> &[(String, [u8; 32])];
}
pub fn discard(tree: StagedTree, io: &dyn ExtractIo) -> Result<(), ExtractError>;
```
The second compile-fail literal names **every** field (as `verify.rs:92-101` does for `VerifiedArtifact`), with
`todo!()` where a field's type has no public constructor (`todo!()` coerces to any type), so the only error it can
raise is the private-field error: if any field became `pub` the others would still fail, and if all became `pub`
the doctest would compile and go red. A field added later would make the literal fail with E0063 instead (the
plan does not rely on `compile_fail` error-code matching), so `extract_findings` (§4.6) also asserts that the body of
`pub struct StagedTree` in `extract.rs` holds no `pub` token, with a planted `pub root: PathBuf` reported.
All accessors take `&self` (FR-054); no `Clone`, `Default`, `From` or public literal. `discard`: PD-8's three checks
(`Path { reason }`, nothing removed), then `remove_tree(root)` while the root is still registered (`NotFound` →
`Ok`), then the tree (and its `LiveRoot`) drops.

#### 4.3.12 Logging (FR-052)

One line per `stage`, `info!(target: LOG_TARGET, …)`: `"{name} {version} {asset}: staged, {lines} listing lines"` or
`"{name} {version} {asset}: refused, {code}"`, names taken from the artifact (the entry may be the mismatching side).
`warn!` lines from `cleanup` and `prune` name only the step, the error kind and relative entry names. No `Display` of
any `Path` anywhere in the module.

### 4.4 `detect.rs` (FR-057)

`fn moduleinfo_version` becomes `pub(crate) fn moduleinfo_version`; body, logs and callers unchanged (the Phase 6
`detect` tests run unchanged in `cargo test --workspace`). Its parse-failure `warn!` prints `path.display()`, which is
the relative `Contents/Resources/moduleinfo.json` because `stage` passes that path (SC-028 f).

### 4.5 `test_support.rs` additions (FR-087, FR-089)

- **The call record**, shared by every wrapper and by `MemExtractIo` (one type, no separate `MemCall`):
  ```rust
  #[derive(Clone, Copy, Debug, PartialEq, Eq)]
  pub enum XKind { OpenArchive, Rewind, ListDir, CreateWorkDir, CreateDir, CreateFile, Write, SetMode, Symlink,
                   RemoveTree, RemoveEntry, KindOf, ReadTree, Canonicalize, ReadFile }
  #[derive(Clone, Debug, PartialEq, Eq)]
  pub enum XOp {
      OpenArchive(PathBuf), Rewind, ListDir(PathBuf), CreateWorkDir { extract: PathBuf, prefix: String },
      CreateDir(PathBuf), CreateFile(PathBuf), Write { path: PathBuf, bytes: Vec<u8> }, SetMode(PathBuf, u32),
      Symlink { target: String, link: PathBuf }, RemoveTree(PathBuf), RemoveEntry(PathBuf, NodeKind),
      KindOf(PathBuf), ReadTree(PathBuf), Canonicalize(PathBuf), ReadFile(PathBuf),
  }
  impl XOp { pub fn kind(&self) -> XKind; pub fn path(&self) -> Option<&Path>; pub fn is_mutating(&self) -> bool }
  ```
  Every call carries its path; `Write` carries the exact slice passed to `ExtractFile::write_all` (recorded by the
  wrapper's own `ExtractFile`, which wraps the inner one and pushes `Write { path: <the create_file path>, bytes }`
  per call), so SC-027's "hash == concat of recorded `write` payloads" and "no read but one `ReadFile` of
  `…/Contents/Resources/moduleinfo.json`" are plain filters over `calls()`. `is_mutating` is true for exactly
  FR-087's eight kinds. Archive `read` calls are not recorded one by one (noise); a `rewind` is.
- `MemExtractIo`: `Mutex<MemFs>` (`BTreeMap<PathBuf, MemNode>`, `MemNode::{Dir { mode }, File { bytes, mode },
  Link { target }}`) plus a `Mutex<Vec<XOp>>` record; absolute keys spelled as the test's roots; `open_archive`
  reads the real archive file (read only); `create_work_dir` names `<prefix><6-char counter>`; `canonicalize` resolves
  Mem links component by component, and a link whose target (or any later component) is absent is `NotFound`, as
  `realpath` reports a dangling link (FR-046's dangling branch, SC-020 (e)); `read_tree` reports `mode: Some(..)`;
  `supports_symlinks() == true`; `remove_entry` of a link removes the link only. Builder methods to plant nodes
  (SC-010 ii, SC-029) and accessors to read nodes back (SC-005, SC-019). No `static` (the static-lock rule,
  `injection.rs:1938-1967`, covers test sources; `test_support.rs` keeps the same discipline).
- `TwoPhaseArchive { first: Vec<u8>, after_rewind: Vec<u8>, pos: usize, rewound: bool }`: an `ArchiveRead` serving
  `first` until the first `rewind`, then `after_rewind` (SC-009 (d), (e1), (e2), (f)).
- `FaultyExtractIo<'a>` (the `FaultyStagingIo` model):
  ```rust
  pub enum RemoveTreeMode { Forward, Noop, Fail(io::ErrorKind) }
  pub struct ReadFault { pub pass: u8, pub after: u64, pub kind: io::ErrorKind } // pass 1 = before the first rewind
  pub struct FaultyExtractIo<'a> {
      inner: &'a dyn ExtractIo,
      fails: Vec<(XKind, usize, io::ErrorKind)>,   // each: fail the n-th call (1-based) of that kind
      remove_tree: RemoveTreeMode,                 // Noop: Ok, nothing removed (SC-010 i, D5); Fail: SC-010 iii
      read_fault: Option<ReadFault>,               // the archive handle's read fails after `after` bytes of `pass`
      calls: Mutex<Vec<XOp>>,
  }
  ```
  so a write-step failure and a failing cleanup combine in one row (SC-010 (iii)), and a mid-archive source error is
  injected below the decoder (SC-010 (iv)); `open_archive` wraps the inner handle in a counting `ArchiveRead` that
  returns `Err(kind)` once `after` bytes of the given pass have been served.
- `RecordingExtractIo<'a>`, two constructors: `forwarding(inner)` (every call recorded and delegated) and
  `fail_closed()` (FR-087: `create_work_dir`, `create_dir`, `create_file`, `write`, `set_mode`, `symlink`,
  `remove_tree`, `remove_entry` are recorded and return `io::Error::other("fail-closed")` without touching
  `std::fs`; `open_archive`, `list_dir`, `kind_of`, `read_tree`, `canonicalize`, `read_file` forward to
  `FsExtractIo`; `supports_symlinks` forwards). `mutating_calls() -> usize`, `calls() -> Vec<XOp>`,
  `canonicalized() -> Vec<PathBuf>`. Optional overrides, each a builder method taking a `Box<dyn Fn … + Send + Sync
  + 'a>` (so the wrapper stays usable from SC-024's threads), consulted before delegating; the call is recorded
  either way:
  - `with_open_archive(|path| -> io::Result<Box<dyn ArchiveRead>>)`: replaces the inner handle (SC-009 (d), (e1),
    (e2), (f) return a `TwoPhaseArchive`);
  - `with_read_tree(|root, &mut Vec<ListingEntry>|)`: mutates the inner `read_tree` result before it is returned
    (SC-019 (a)-(d) and the NFC/NFD row, over `forwarding(&mem)`);
  - `with_canonicalize(|path, inner_result| -> io::Result<PathBuf>)`: maps the inner result (SC-012 negative twin,
    SC-012 / SC-020 link-lands-outside-its-top rows, SC-020 b);
  - `with_kind_of(|path, inner_result| -> io::Result<Option<NodeKind>>)` (SC-020 a).
- `snapshot(root: &Path) -> Vec<(String, char, u64)>`: relative path, `f`/`d`/`l`, length, via `symlink_metadata`
  (never following), sorted (SC-006).
- `#[cfg(unix)] pub fn temp_roots_via_link(td: &Path) -> Roots` and `#[cfg(target_os = "macos")] pub fn
  temp_roots_raw(td: &Path) -> Roots` (FR-089): `check_under_temp(td, &canonical_temp_dir())` first (panic on
  failure, before anything is created); `via_link` makes `<canon td>/real` and `std::os::unix::fs::symlink("real",
  <canon td>/via)` and spells every root under `<canon td>/via`; `raw` spells every root under `td` as given (a
  `TempDir::new()` path is already the raw `std::env::temp_dir()` spelling, so **no new `std::env::temp_dir` call** and
  `D6_SITES` stay 2 for this file, `injection.rs:92-100`). Both reuse a private `roots_at(base) -> Roots` factored
  out of `temp_roots_in` (same `RootPaths`, `Roots::new` under its existing `#[expect]`, `sandboxed(base)`, the same
  `create_dir_all` loop), so the `disallowed_methods` count of `test_support.rs` is unchanged.

### 4.6 Guards: `crates/core/tests/injection.rs`

| Change | Rule / spec | Planted case (where) |
|---|---|---|
| `const EXTRACT_RS`, `EXTRACT_TESTS_RS` | — | — |
| `FR007_DEPENDENCIES` `[&str; 14]` + `"flate2"`; dev set `{minisign}` in `core_dependencies_are_the_fr007_set` | FR-080, SC-017 | — (the real manifest is the case) |
| `CONFINED_TOML_KEYS` `[…; 13]` + `("flate2", &["Cargo.toml", "crates/core/Cargo.toml"])` | FR-080 | `flate2 = "1"` in `src-tauri/Cargo.toml` (existing TOML-plant test, `injection_scanner_reports_planted_violations`) |
| `verify_confinement_findings` gains `("sha2", &[VERIFY_RS])` unchanged and a new `extract_findings(files)` holding: `flate2` in non-test files (`!is_test_source`) only in `EXTRACT_RS`; FR-083's tokens per SI-2, **extended** (SI-12) so the token list that may appear in `extract.rs` only inside `impl ExtractIo for FsExtractIo` is FR-083's eleven plus `File :: options`, `DirBuilder`, `remove_dir`, `soft_link`, `symlink_file`, `symlink_dir`, `set_len` (every std entry point that creates, truncates or removes a file or directory or makes a link, matched as whole-identifier token sequences, `spanned_tokens` / `seq_at`, `injection.rs:758-778`, `:803-813`; the list is then a superset of `FR084_TOKENS`' filesystem entries, `:223-243`); `create_dir_all` only in `fn create_work_dir` of `impl ExtractIo for FsExtractIo`; **`tempfile`** in `extract.rs` only inside that same `fn create_work_dir`, where the only `tempfile` method token is `tempdir_in` (so `tempfile :: tempdir`, `TempDir :: new`, `Builder :: … . tempdir (`, `tempfile (` and `NamedTempFile`, which create under the real `env::temp_dir()`, `tempfile-3.27.0/src/dir/mod.rs:48`, `:64`, `:187`, appear nowhere in the file; a `use tempfile` at file scope is outside the fn and therefore reported); `hard_link`, `Command`, `TcpStream`, `xattr`, `thread :: spawn`, `sync_all`, `sync_data`, `sync_file`, `sync_dir` nowhere in `extract.rs`; `ExtractIo`'s trait body declares no `sync_*`; `tar` is a key in no workspace manifest **and** no `[[package]]` with `name = "tar"` is in `apps/krate-manager/Cargo.lock` or `tests/fixtures/clippy-guard/Cargo.lock` (FR-082's "not a dependency anywhere", so a transitive `tar` is caught too; today neither lock has one, §0.2); `StagedTree {` in non-test files only in `extract.rs`; the body of `pub struct StagedTree` holds no `pub` token (FR-050, §4.3.11); `impl StagedTree` blocks hold no `fn` whose first parameter is `self` / `mut self` and none named `into_*`, `take_*`, `rename`, `persist`, `release`; no string literal in `extract.rs` holds `release.json` (FR-034) | FR-082, FR-083, FR-034, FR-055, SC-016, SC-017, SC-026 | `verify_confinement_scanner_reports_plants` gains: `use flate2::read::GzDecoder;` in `feed.rs`; `use sha2::Sha256;` in `extract.rs`; `fn f() { std::fs::create_dir("x").ok(); }` in `extract.rs`; `fn f(x: std::fs::File) { x.sync_all().ok(); }` in `extract.rs`; `fn f() { let _ = tempfile::tempdir(); }` in `extract.rs`; `fn f(p: &std::path::Path) { let _ = std::fs::File::options().write(true).create(true).open(p); }` in `extract.rs` outside the impl; `fn f() { let _ = std::fs::DirBuilder::new().create("x"); }` in `extract.rs` outside the impl; `tar = "0.4"` in `crates/core/Cargo.toml`; a planted `Cargo.lock` text `[[package]]\nname = "tar"\nversion = "0.4.44"` (the lock half); `pub struct StagedTree { pub root: PathBuf }` in `extract.rs`; `impl StagedTree { pub fn into_bundle(self) -> PathBuf { self.root } }` in `extract.rs`. Each reported as planted, the real tree green |
| `TEST_PROGRAM_BODIES` `[…; 6]` + `(EXTRACT_TESTS_RS, "staged_tree_has_no_xattrs_macos", "/usr/bin/xattr")` and `(EXTRACT_TESTS_RS, "symlink_entry_unsupported_windows", "cmd")` | SI-1, SI-10 | existing plants unchanged |
| `minting_route_findings` `verify_file` callers + `EXTRACT_TESTS_RS` | FR-088 | `verified_artifact_scanner_reports_plants` gains `fn f() { let _ = verify_file(p, e, k); }` in `extract.rs` (reported) and `fn f() { let _ = SigningKey::from_text(t); }` in `extract_tests.rs` (reported) |

Every new check runs in an existing `#[test]` (FR-086: no new name): the real-tree half in the function that already
asserts its neighbour's base is empty, the plants in the matching `*_reports_plants` function.

### 4.7 `tests/required-tests.json` and `EXPECTED_REQUIRED` (FR-086)

Appended in FR-086's order: `cargo.all` gains the 24 `extract::tests::*` names; `cargo.macos` gains the 5 `_unix` names
then the 2 `_macos` names; `cargo.linux` the 5 `_unix` names; `cargo.windows` `extract::tests::symlink_entry_unsupported_windows`.
Totals: `all` 248, `windows` 11, `macos` 35, `linux` 30, `vitest` 29 unchanged. Every `_unix` / `_macos` / `_windows`
test carries the matching `#[cfg(...)]` so it compiles only on the legs whose lane names it (the step requires
exactly one passing line per name, `manager-ci.yml:163-169`).

### 4.8 State layout

```
<state dir>/                          (Roots::state_dir, injected; production from src-tauri, Phase 4)
  manager.lock                        one process per state dir (state_lock.rs:53-71)
  staging/                            Roots::staging_dir
    downloads/<hex>/<asset>           Phase 7 verified artifacts (Phase 7 prune reads only downloads/)
    extract/                          Phase 8, EXTRACT_DIR; a real directory (PD-6)
      <64 hex>.<6 alnum>/             one work dir per stage; a StagedTree root on success
        <vst3_bundle>/ presets/ README.txt CHANGELOG.txt
```
Nothing else is written; no manifest, journal or setting (Phase 9). A non-live entry is a leftover and is pruned.

### 4.9 The OS abstraction

Phase 8 adds no `Platform` method (PD-10). Its single OS seam is `ExtractIo`, with `FsExtractIo`'s `cfg(unix)` body
(Linux, macOS) and `cfg(not(unix))` body (Windows), and the test implementations `MemExtractIo` /
`RecordingExtractIo` / `FaultyExtractIo`, which run the same logic on all three. Tests per implementation: Windows
(`symlink_entry_unsupported_windows` with its junction block for PD-6 / PD-7's Windows branch, the `cfg(windows)`
reserved-name block of `entry_path_rejects_every_bad_form`, `case_fold_collision`, every `cargo.all` Fs row), Linux
and macOS (the five `_unix` names; the `cfg(unix)` mode block of `kat_archive_stages`; the `cfg(not(windows))`
acceptance block of `entry_path_rejects_every_bad_form`), macOS (`_macos` names).

### 4.10 Tauri commands, capabilities, UI

None (FR-090). No command, capability, permission, CSP or webview change; Phase 9 calls `stage` from its driver.

---

## 5. Dependencies

| Crate | Version | Use | Verified against | Why |
|---|---|---|---|---|
| `flate2` | 1.1.10 (existing workspace pin, default features → `miniz_oxide`) | `bufread::GzDecoder` in `extract.rs`; `write::GzEncoder` in `extract_tests.rs` | https://docs.rs/flate2/1.1.10/flate2/bufread/struct.GzDecoder.html ; registry source (§1.2) | already in core's normal graph through `ureq` (§0.2 "Not an issue"); member-exact gzip with CRC/ISIZE checks |
| `tempfile` | 3.27.0 (existing core dependency) | `Builder::prefix(..).tempdir_in(..)` + `keep()` for the work dir | https://docs.rs/tempfile/3.27.0/tempfile/struct.Builder.html ; registry source (§1.2) | collision-retrying unique directory creation |
| `sha2` | 0.11.1 (existing) | through `verify::Sha256Stream` only | `verify.rs:9` | FR-082 confinement |
| `log`, `thiserror`, `serde_json` | existing pins | log lines, `ExtractError`, the `Value` for `check_install_value` | `Cargo.toml:19`, `:43`, `:33` | — |
| `tar` | **not used** | — | spec External facts (`spec.md:130`) | three C4 divergences (D1) |
| std | 1.99.1 (`rust-version` 1.90) | `fs`, `os::unix::fs`, `io::Seek::rewind`, `OsStr::as_encoded_bytes`, `sync::{Mutex, Barrier}` | §1.2 | — |

No npm package, no Tauri plugin, no new crate in either lock.

---

## 6. Safety design

- **Injected roots.** The only root is `ctx.roots.staging_dir()`; every write is under `staging/extract/` (FR-003,
  FR-040); `check_sandboxed(extract)` makes a mis-rooted test panic before any write (PD-10); tests stage under
  `fresh_temp_roots()` or FR-089's constructors on a fresh `TempDir`.
- **Verify before extract.** `stage` takes only a `VerifiedArtifact` (no public constructor, `verify.rs:90-162`) and
  a feed entry bound to it field by field (FR-002) before any I/O; both passes re-hash the raw bytes and must equal the
  verified SHA-256 (D6), so whatever is staged is the signed archive.
- **Nothing written before the whole archive is accepted.** Pass 1 (rules, bounds, payload, layout, symlink support,
  joined length) finishes before the prune, the work dir or any write (D3; SC-006 counts 0 mutating calls).
- **Zip-slip class.** C4 rules 1-3 in `listing.js` order (PD-1, PD-11); one join (`entry_path`); `create_dir` (never
  `_all`) and `create_new` files so no existing name or link is written through; symlinks created last and never
  traversed (rule 3a); the post-check canonicalises both sides (FR-046) and the tree is re-listed without following
  links (FR-047).
- **The work area itself.** `extract/` must be a real directory (PD-6); the work dir must be a real directory whose
  canonical path is under the canonical staging dir (FR-040); prune removes links as links (PD-7) and never lists a
  linked `extract/`.
- **Bounded resources.** Every D8 bound is checked before the memory or bytes it limits are spent.
- **No metadata from the archive.** Only the exec bit reaches the disk; pax keys other than `path` / `linkpath` are
  dropped at parse time; no xattr API exists in the module (FR-083 bans the token); so no quarantine is ever written.
- **Elevated helper.** Not involved (Linux needs none; no process is started).
- **Tauri capability set.** Unchanged and not widened: the webview cannot reach `extract` (no command exists).
- **Keys.** The KAT public key and in-memory `TestSigner` keys only; nothing written to disk (FR-084).
- **Tests on this dev box.** Fail-closed recorder for every refused row (FR-087), so a regression cannot create
  `C:\evil` or open `\\server\share`; only rows whose named paths resolve inside their own TempDir also run over the
  real filesystem; the only processes are `/usr/bin/xattr -lr <temp path>` on macOS and, on Windows, `cmd /C mklink
  /J <link> <target>` with both paths inside the test's own TempDir (SI-1 / SI-10 rows, the Phase 7 pattern of
  `download_tests.rs:2949-2965`).

---

## 7. Test plan

All in `crates/core/src/extract_tests.rs` (module `extract::tests`) unless named otherwise. Shared helpers in the test
module: `ustar(entries) -> Vec<u8>` (512-byte headers, checksum, optional GNU `L`/`K`, pax `x`/`g`, end block) and
`gz(bytes)` (`flate2::write::GzEncoder`); `signed(bytes, entry) -> (TempDir-staged file, VerifiedArtifact)` through
`TestSigner::sign_c3` + `verify_file` (FR-084); `alpha_entry(version, payload)` = `test_feed(&[TestPlugin { name:
"Alpha", key: LinuxX86_64, … }])` with its `payload` replaced (the fields are `pub`, `feed.rs:75-79`); `literal_payload(text)`
hashes a listing text written literally in the test (FR-085). Every `stage` row asserts `code()` only.

### 7.1 SC → test

| SC | Test | Inputs | Assertions |
|---|---|---|---|
| SC-001 | the gate commands (§7.4) | — | exit 0, 0 `warning:` lines, no new `#[allow`/`#[expect` |
| SC-002 | `good_fixtures_match_committed_listings` | 9 good + KAT, `plan_archive(&bytes[..], PRODUCTION, lines)` | `text()` == `.listing` bytes; `hex(digest())` == first 64 chars of `.sha256`; `check_staged_layout` Ok with `Alpha.vst3` / `KatPlugin.vst3`; `dot-slash` at `max_entries` 10 |
| SC-003 | `bad_fixtures_fail_with_expected_code` | (1) 22 bad; 20 via `plan_archive` (max 64); 2 `layout-*` via `plan_archive` then `check_staged_layout` with their own payload. (2) **The rule table (SI-12)**: every one of the 33 reject rows of `c4-listing-tar.test.js:196-231`, ported literally to `ListingEntry` values (`f` = File mode 0644, `d` = Dir 0755, `l` = Symlink 0777 as `:103-105`; byte paths, so the two invalid-UTF-8 rows carry `[0xc3, 0x28]`; the FIFO row is `EntryType::Other(b'6')`), each through `CanonicalListing::from_entries`; and the accept rows of `:236-280` (in-bundle links, D3 normalisation with implied ancestors and the `./` root dropped, UTF-8 byte sort). (3) **Streaming rows** through `plan_archive` (max 64), each the base tree plus one runtime-built ustar header (FR-085): a name with bytes `c3 28`, a name `Alpha.vst3/a -> b`, a typeflag `6` header `Alpha.vst3/fifo` | (1) code == `.expected-error` trimmed; layout lines 11 / 8; items `NOTES.txt` / `presets`. (2) per reject row the same code as the Node row's third column (so every `E_LISTING_PATH` case, the absolute / drive / backslash targets, `a//b` and `a/./b`, the five `E_LISTING_SYMLINK_FORM` variants, the FIFO, the three escapes, the chains and "a link under a link" are each asserted, `ListingCode::Path` included); per accept row `lines()` deep-equals the Node expectation. (3) `E_LISTING_PATH`, `E_LISTING_PATH`, `E_LISTING_TYPE` (the per-entry functions of PD-11 are shared, so the streaming path and the pure builder agree, PD-1). Threshold: 22 / 22 + 33 / 33 rejects + every accept row + 3 / 3 streaming |
| SC-004 | `kat_archive_stages` | KAT copied into the TempDir, `verify_file` with `kat_signing_key()`, entry KatPlugin 1.2.3 `{10, db287e49…}` | digest; `bundle()` holds the two files with archive bytes (compared with `tar` bytes read through `plan`-independent `flate2` + the test's own header walk); root's parent == `<staging>/extract`; **`#[cfg(unix)]` block**: `symlink_metadata(..).permissions().mode() & 0o7777` is `0o644` for `Contents/Resources/moduleinfo.json` and `Contents/x86_64-linux/KatPlugin.so` and `0o755` for `KatPlugin.vst3`, `Contents`, `Contents/Resources`, `Contents/x86_64-linux` (the KAT `.listing`'s modes) — SC-004's "real modes on macOS and Linux" clause |
| SC-005 | `real_shaped_round_trip`, `exec_and_symlink_round_trip_logic`, `dot_slash_round_trip` | Fs / Mem / Fs; committed payloads; version 1.2.0 | listing == `.listing`; Mem: `Alpha.so` mode `0o755`, `libalpha.so` link → `Alpha.so` |
| SC-006 | `crafted_archives_are_refused` | 20 bad + runtime rows (spec list) **+ SI-5 rows**: `[f ../x, base…, header with bad checksum]` → `E_LISTING_CHECKSUM`; `[l Alpha.vst3/Contents/x -> ../../../etc, f /etc/evil, base…]` → `E_LISTING_ABSOLUTE`; `[f /etc/evil, base 10 entries]` with `payload.files` 9 → `E_LISTING_ABSOLUTE`; header flood = 20 pax `g` records + base, `max_headers` 16 → `headers` | fail-closed recorder, 0 mutating calls; snapshot before == after; `extract/` absent or empty; TempDir-internal rows repeated over `RecordingExtractIo::forwarding(&FsExtractIo)` |
| SC-007 | `payload_and_layout_mismatches` | rows (a), (a2), (a3), (b), (c), (d) as spec | codes and `{expected, found}`; 0 mutating calls |
| SC-008 | `entry_binding_cases` | one row per FR-002 field; `.exe` / `.pkg` artifacts verified as `inno-exe` / `pkg` assets of a Windows / darwin `test_feed` | `EntryMismatch { field }`; `RecordingExtractIo::fail_closed().calls()` empty (not even `open_archive`) |
| SC-009 | `changed_archive_is_refused` | (a)-(c) rewrite the staged file after `verify_file`; (d)-(f): `RecordingExtractIo::forwarding(&FsExtractIo).with_open_archive(..)` returning a `TwoPhaseArchive` (§4.5) whose `first` is the original bytes and whose `after_rewind` is: (d) row (a)'s bytes; **(e1)** a re-gzipped copy of the tar stream with one more well-formed file header + data (`presets/Extra.vstpreset`, 5 bytes, valid checksum) inserted before the end-of-archive block; **(e2)** a re-gzipped copy in which `README.txt`'s header `size` is one larger (checksum recomputed, data re-padded, every other byte unchanged); **(f)** a re-gzipped copy with entry 5's header checksum field XOR `0x01` | one code each; (d), (e1), (e2), (f) each `E_EXTRACT_CHANGED` (FR-044's pass-2 rule: (f) is **not** `E_LISTING_CHECKSUM`); (e1) the record holds no `CreateFile` / `Write` for `presets/Extra.vstpreset`; (e2) no `CreateFile` for `README.txt`, and for every path the summed `Write` bytes ≤ its plan size; (f) no `CreateFile` for entry 5 or any later entry; in (d)-(f) the work dir is gone afterwards and `extract/` is empty. So the per-entry comparison (§4.3.10 step 10), not only the end-of-stream hash, is what refuses (e1), (e2) and (f) |
| SC-010 | `io_failures_leave_no_tree` | `FaultyExtractIo` over Fs (`write`→`StorageFull`, `create_dir`→`PermissionDenied`) and over Mem (`symlink`→`Unsupported`); (i) each step k with `RemoveTreeMode::Noop`; (ii) planted leftover; **(iii)** over Fs, `fails = [(Write, 1, StorageFull)]` with `RemoveTreeMode::Fail(PermissionDenied)`, under `install_capture`; **(iv)** archive read faults over Fs, staging `real-shaped`: `ReadFault { pass: 1, after: 1024, kind: PermissionDenied }`, `ReadFault { pass: 1, after: 1024, kind: TimedOut }`, `ReadFault { pass: 2, after: 1024, kind: PermissionDenied }` | codes; work dir gone; (i) partial content present, next `stage` prunes it; (ii) pruned; `extract/` holds only the new tree; (iii) `E_EXTRACT_FULL { step: "write" }` (the original error, not the cleanup's), one `Warn` record at target `krate_manager_core::extract` naming `remove` and `PermissionDenied` with no absolute path (raw or canonical TempDir), the work dir still present, and the next `stage` over plain `FsExtractIo` prunes it; (iv) `E_EXTRACT_DENIED { step: "read_archive" }`, `E_EXTRACT_IO { step: "read_archive", kind: TimedOut }` (both pass 1: 0 mutating calls, no work dir) and `E_EXTRACT_DENIED { step: "read_archive" }` (pass 2: work dir removed, `extract/` empty) |
| SC-011 | `posix_modes_and_links_unix` (`cfg(unix)`) | `exec-and-symlink`, Fs, test sets umask-independent check | `mode & 0o7777` 0755 / 0644 / dirs 0755; `read_link`; rebuilt digest `baad7502…` |
| SC-012 | `escape_cases_unix`, `symlinked_staging_root_unix`, `raw_temp_dir_staging_root_macos` | as spec; `RecordingExtractIo::forwarding` records `canonicalize` arguments; **`symlinked_staging_root_unix` gains two FR-046 rows**: (1) a runtime archive = the base tree plus `l Alpha.vst3/Contents/x86_64-linux/missing.so -> nothere.so` (literal listing payload, FR-085); (2) `exec-and-symlink` over `forwarding(&FsExtractIo).with_canonicalize(..)` replacing the result for the argument ending in `x86_64-linux/libalpha.so` (the link itself, not its parent) with `Ok(<canonical work>/presets/x)` | precondition `staging_dir() != canonicalize(staging_dir())`; record holds the `via`/raw work dir and every listed path or parent; negative twin `Escape` naming the path; (1) stages, `symlink_metadata(missing.so)` is a link, `read_link == "nothere.so"`, `metadata(missing.so)` is `NotFound` (dangling, checked through its parent only); (2) `E_EXTRACT_ESCAPE` naming `Alpha.vst3/Contents/x86_64-linux/libalpha.so` (inside the work dir but outside `work/Alpha.vst3`, so a check against `cw` alone would pass it), work dir removed |
| SC-013 | `staged_tree_has_no_xattrs_macos` (`cfg(target_os = "macos")`), `pax_xattr_records_are_ignored` | pax `SCHILY.xattr.com.apple.quarantine=…` before `README.txt` | `Command::new("/usr/bin/xattr").args(["-lr", root])` stdout empty (SI-1 row); every OS: plan has no attribute, Mem record has no attribute op (the enum has none) |
| SC-014 | `case_fold_collision` | `presets/a.vstpreset` + `presets/A.vstpreset`, literal payload | pinned per target: Windows / macOS precondition "case-insensitive" then `Collision`, `extract/` empty; Linux precondition "case-sensitive" then both files with their bytes |
| SC-015 | `discard_and_prune` | two stages of one artifact; decoys made by assigning `tree.root` (child-module field access) of a real staged tree | distinct roots; `discard` removes exactly one; third stage leaves the live one; dropped tree pruned by the next stage; decoys (i)-(iii) → `Path`, canary and snapshot unchanged; a file copied out of `bundle()` with `std::fs::read` (SC-025) |
| SC-016 | doctests in `extract.rs` + `extract_findings` real tree and plant | — | compile-fail ×2 pass; `no_run` compiles; plant `into_bundle(self)` reported |
| SC-017 | injection.rs rows of §4.6 | — | real tree green; each plant reported |
| SC-018 | `node --test tools/feed-contract/tests/manager-ci-shape.test.js` | — | deep-equal |
| SC-019 | `tree_mismatch_is_refused` | `RecordingExtractIo::forwarding(&mem).with_read_tree(..)` tampering the result (a)-(d) + NFC/NFD row | `TreeMismatch { line }` literal per row; `remove_tree` recorded; Mem tree lacks the work dir |
| SC-020 | `entry_path_rejects_every_bad_form`, `work_dir_must_be_inside_staging` | pure table; **`#[cfg(windows)]` block** (FR-041 Windows branch, §4.3.8): `aux.txt`, `Alpha.vst3/CON`, `presets/com1.json`, `presets/Nul`, `presets/x.`, `presets/x ` ; **`#[cfg(not(windows))]` block**: the same six inputs; plus one `stage` over the fail-closed recorder of a C4-valid runtime archive holding `presets/aux.vstpreset` (literal payload); wrappers overriding `kind_of` / `canonicalize`; **+ SI-9 row**: `extract` planted as a link (Mem; logic evidence for every OS, the real-filesystem proof is in `prune_removes_real_link_unix`, SC-029) → `Io { step: "create_work_dir" }`; **+ two FR-046 rows over Mem (every OS)**: (e) the dangling-link archive of SC-012 (1) over `MemExtractIo`; (f) `exec-and-symlink` over `forwarding(&mem).with_canonicalize(..)` replacing the result for `…/x86_64-linux/libalpha.so` with `Ok(<work>/presets/x)` | `Path`; good input is `work` + 4 segments; `Escape`; no `create_dir` / `create_file` after `create_work_dir`; work dir removed; Windows block: each `Path { .. }`, and the `stage` row is `E_EXTRACT_PATH` with 0 mutating calls (refused at step 6, before the work dir); non-Windows block: each `Ok` with `work` + its segments, and the `stage` row (over `forwarding(&FsExtractIo)`) stages; (e) stages, the Mem tree holds `missing.so` as a link with target `nothere.so`; (f) `E_EXTRACT_ESCAPE` naming `Alpha.vst3/Contents/x86_64-linux/libalpha.so`, Mem tree lacks the work dir |
| SC-021 | `stage_logs_one_line_per_outcome` | `install_capture`; real-shaped (Fs), `absolute-path` (fail-closed) | `take_mine()` filtered to target `krate_manager_core::extract`: one `Info` each; names; `14`; `E_LISTING_ABSOLUTE`; no TempDir path (raw or canonical) and no staging path in any captured record |
| SC-022 | `special_bits_and_mtime_not_applied_unix` | modes `04755` / `02644` / `01755`, mtime 978307200, literal payload | exact modes; `modified()` > 2001-01-02 |
| SC-023 | `symlink_entry_unsupported_windows` (`cfg(windows)`) | `exec-and-symlink` and `real-shaped` over `RecordingExtractIo::forwarding(&FsExtractIo)`; **junction block (PD-6 / PD-7 on Windows, SI-10)**, each case on its own `fresh_temp_roots()` with a canary directory `<TempDir>/canary` holding `keep.txt`, junctions made with `cmd /C mklink /J <link> <target>` on plain (non-`\\?\`) paths as `download_tests.rs:2949-2965` does (the SI-1 `cmd` row): (j1) `<staging>/extract/<64 hex>.abcdef` a junction to `canary`, then `extract::prune` over `FsExtractIo`; (j2) `<staging>/extract` itself a junction to `canary`, then `prune`, then `stage` of `real-shaped` over `forwarding(&FsExtractIo)` | `Unsupported`, 0 mutating calls, `extract/` absent; real-shaped stages; (j1) `prune` returns 1, the junction is gone (`symlink_metadata` → `NotFound`), `canary/keep.txt` exists with its bytes (so `remove_entry`'s `remove_file` → `remove_dir_all` fallback removed the link and did not follow it); (j2) `prune` returns 0, `canary` snapshot unchanged, and `stage` returns `E_EXTRACT_IO { step: "create_work_dir", kind: InvalidInput }` with `canary`'s snapshot still unchanged |
| SC-024 | `concurrent_stage_is_safe` | `std::thread::scope`; gate wrapper (a `std::sync::Barrier` pair, local to the test) blocking A's first `create_file`; Mem variant B = `exec-and-symlink`; **Fs variant B = `ustar-prefix` (SI-4)**; 8 threads one artifact; `prune` while A held; failing-A twin | B ok, A's work dir present with its directories when released, A ok, roots distinct; 8 distinct roots present; `prune` returns 0 live removals; twin: A deregistered and pruned |
| SC-025 | covered by SC-015 and SC-016 | — | — |
| SC-026 | `extract_findings` `sync_*` rule and plant | — | green / reported |
| SC-027 | `file_hashes_match_written_bytes` | `real-shaped` (Fs) and `exec-and-symlink` (**Mem, SI-4**); a runtime archive with a zero-length preset | one entry per `f` line, listing order; hash == SHA-256 of builder bytes (fixture bytes extracted by the test's own tar walk over `flate2`-decoded fixture bytes); Mem (over `RecordingExtractIo::forwarding(&mem)`): hash == SHA-256 of the concatenated `bytes` of the `XOp::Write { path, .. }` records for that path, in record order; the record holds exactly one `XOp::ReadFile`, its path ending in `Contents/Resources/moduleinfo.json`, and no `XOp::OpenArchive` of a path under the work dir; empty file = SHA-256 of "" |
| SC-028 | `moduleinfo_version_must_match` | (a)-(f) as spec **+ (g) non-UTF-8, (h) BOM (SI-8)** | codes; work dir removed; `extract/` empty; log names `Contents/Resources/moduleinfo.json`, no absolute path |
| SC-029 | `prune_removes_by_kind`, `prune_removes_real_link_unix` | Mem tree with dir / file / link-to-canary / live root; injected `remove_entry` failure; **+ linked `extract/` (SI-9)**; **`prune_removes_real_link_unix` gains the SI-9 production-body block** (no new name): on a fresh `fresh_temp_roots()`, `<staging>/extract` planted as a real `std::os::unix::fs::symlink` to a canary directory `<TempDir>/canary` holding `keep.txt`, then `prune` over `FsExtractIo`, then `stage` of `real-shaped` over `RecordingExtractIo::forwarding(&FsExtractIo)` | returns 3; link removed as link; canary intact; missing `extract/` → 0, nothing created; failure logged, others removed; linked `extract/` → 0, target untouched; SI-9 block: `prune` returns 0; `stage` returns `E_EXTRACT_IO { step: "create_work_dir", kind: InvalidInput }` (so `FsExtractIo::create_work_dir`'s own `symlink_metadata(extract).is_dir()` check refused, not step 8's canonicalize, which would give `E_EXTRACT_ESCAPE`); the record holds one `CreateWorkDir` and no later mutating call; `snapshot(canary)` before == after (no `<64 hex>.*` entry created in the link target) |
| SC-030 | `path_length_bounds` | 255- / 256-byte segment; injected `max_joined_bytes` = exact length (computed by the test as `staging.join("extract").join("<64 hex>.xxxxxx").join(<components>)`) and minus one | stage / `segment` / stage / `joined`; 0 mutating calls on the refusals; **in (c)'s success branch, independent of the formula**: `tree.root().file_name()`'s encoded length is exactly 64 + 1 + 6 = 71 bytes and starts with `format!("{hex}.")`, `tree.root().parent() == Some(<staging>/extract)`, and `tree.root().join(<longest path's components>).as_os_str().as_encoded_bytes().len() == computed` (the path actually created, so a wrong `RANDOM_LEN`, prefix spelling or separator in `joined_len` goes red) |
| SC-031 | `trailing_zero_padding_is_accepted` | padding 1, 10, 511, 100000 zeros; `00 00 01`; `garbage!`; padding over injected `max_tar_bytes`; **gzip-layer rows through `plan_archive` (max 64), FR-010**: (g1) `real-shaped`'s decoded tar split at byte 1000 (not a block boundary) into two members, `gz(tar[..1000]) ‖ gz(tar[1000..])`; (g2) the committed file with byte `len - 8` (the first CRC32 byte) XOR `0x01`; (g3) byte `len - 1` (the last ISIZE byte) XOR `0x01`; (g4) the file cut 4 bytes before EOF (inside the 8-byte trailer); (g5) the file cut at `len / 2` (inside the deflate stream); (g6) first byte replaced by `0x00` | listing == committed; padded stage ok; `Corrupt` ×2; `tar_bytes`; (g1) listing == committed `.listing` (members concatenate as Node's `gunzipSync`); (g2)-(g6) each `E_LISTING_CORRUPT`, never `E_EXTRACT_IO { step: "read_archive" }` (a `&[u8]` source never errors, so the tap is unset; a short trailer's `UnexpectedEof` is a decoder error, §4.3.3) |

### 7.2 FRs without their own SC

| FR | Covered by |
|---|---|
| FR-001 (one entry point) | SC-016 doctests (no other public extracting fn exists to call); code review of `pub fn` list in `extract.rs` |
| FR-003 | SC-006 snapshots, SC-021 log paths, SC-029 canary |
| FR-034 (no `release.json`) | `extract_findings`: no `release.json` literal in `extract.rs` (added to the same rule; plant `"release.json"` reported) |
| FR-048 | SC-019, SC-028 (failures before the tree exists) and SC-004 (success after all checks) |
| FR-052 | SC-021, SC-028 f, SC-029 |
| FR-054 | SC-016 / SC-025 |
| FR-081 | CI step `core dependency tree` (unchanged, `manager-ci.yml:122-136`) and the local `cargo tree` record (§7.4) |

### 7.3 PENDING-CI rows (exact job and assertion)

Every row closes on a leg only when the run's committed `tests/required-tests.json` lists its names and the leg's step
`required tests ran` (`manager-ci.yml:153-178`) passed with no `::error`; the evidence is step `cargo test`
(`cargo test --workspace --locked`, `:117-120`).

| Row | Job(s) | Names / step |
|---|---|---|
| SC-001 | `check (windows-latest)`, `check (macos-latest)`, `check (ubuntu-24.04)` | steps cargo fmt, clippy, clippy dev-feed, clippy-guard (the SI-3 lock), cargo test, typecheck, lint, vitest |
| SC-002 to SC-010, SC-015 to SC-021, SC-024 to SC-031 (`cargo.all`) | all three | the 24 `cargo.all` names |
| SC-004 real modes | `check (macos-latest)`, `check (ubuntu-24.04)` | `extract::tests::kat_archive_stages` (`cargo.all`), its `#[cfg(unix)]` mode block (§7.1 SC-004): the `test … ok` line on those two legs is the evidence; on Windows the block is not compiled and the row closes on paths, types and bytes only |
| SC-011, SC-012 (1, 2) incl. the dangling-link and link-outside-its-top rows, SC-022, SC-029 real link incl. the SI-9 production-body block | `check (macos-latest)`, `check (ubuntu-24.04)` | the 5 `_unix` names |
| SC-012 (3), SC-013 | `check (macos-latest)` | `raw_temp_dir_staging_root_macos`, `staged_tree_has_no_xattrs_macos` |
| SC-014 | macOS (collision branch), Linux (both files), Windows (collision) | `case_fold_collision` |
| SC-020 FR-041 per-OS blocks | `check (windows-latest)` (the `cfg(windows)` refusal block, also local on this box), `check (macos-latest)`, `check (ubuntu-24.04)` (the `cfg(not(windows))` acceptance block) | `extract::tests::entry_path_rejects_every_bad_form` |
| SC-023 incl. the junction block (PD-6, PD-7 Windows branch) | `check (windows-latest)` (also local on this box) | `symlink_entry_unsupported_windows` |
| SC-018 | all three | `required tests ran` counts 248 + 11 / 35 / 30 cargo names |

### 7.4 Local close procedure (Windows, this box)

From `apps/krate-manager/` (Git Bash), each command once, output to a log under the session scratchpad:
1. `cargo fmt --all --check`
2. `cargo clippy --workspace --all-targets --locked -- -D warnings`
3. `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings`
4. `CARGO_TARGET_DIR="$PWD/target" cargo clippy --manifest-path tests/fixtures/clippy-guard/Cargo.toml --all-targets --locked -- -D warnings` (expected: exit ≠ 0 with exactly the three disallowed-method errors, as the CI step counts)
5. `cargo test --workspace --locked` (default threads; record the `test result:` lines and wall clock; every binary `0 failed`)
6. `cargo tree -p krate-manager-core -e normal --locked --prefix none --format "{p}"` (no tauri / tokio / reqwest / hyper / dirs / home)
7. `npm run typecheck`, `npm run lint`, `npm test`
8. `node ../../tools/lint-manager-version.js`
9. from the repo root: `node --test tools/feed-contract/tests/` (the whole pipeline lane, parallel files by default)

Steps 2-4 and 7-9 are independent of step 5 and can run while it runs (different processes; clippy and test share
`target/` but cargo's lock serialises only the overlapping build units). Optional early signal for the `cfg(unix)`
code, not evidence: in WSL, `cargo test -p krate-manager-core extract::` with `CARGO_TARGET_DIR` on the WSL
filesystem (never `/mnt/f`, which drops modes and links), recorded as "WSL, informative"; the rows stay PENDING-CI.

---

## 8. Build integration and gate lanes

- Workspace: unchanged members (`core`, `helper`, `src-tauri`); `extract` is a module of `krate-manager-core`.
- `package.json` scripts: unchanged.
- CI workflow files: **none change** (FR-091). `manager-ci.yml` already runs everything (its path filter covers
  `apps/krate-manager/**` and `tools/feed-contract/**`, `:6-16`); `feed-tools.yml` runs every
  `tools/feed-contract/tests/*.test.js`, `manager-ci-shape.test.js` included (`feed-tools.yml:79-83`).
- Lanes: app lane (§7.4 steps 1-8) and pipeline lane (step 9). No C++ lane: `node tools/header-consumers.js` is not
  needed because no file under `plugins/`, `dsp/` or a C++ test changes (FR-091).

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| A two-defect archive reports a different first code than `listing.js` | PD-1 deferral + SI-5 rows; parity proven on the committed corpus (SC-002, SC-003) and on the runtime two-defect rows |
| Prune races a concurrent `stage` | PD-2 (creation inside the lock), `LiveRoot` guard (deregisters on every exit and unwinding), SC-024 |
| A planted link at `extract/` or inside it makes prune delete elsewhere | PD-6, PD-7; `remove_dir_all` removes links as links (std docs) |
| Windows: antivirus or the search indexer holds a just-written file, so `remove_tree` of a failed work dir fails | the removal failure is logged, the original error returned, the leftover pruned by the next `stage` or Phase 9's start-up `prune` (FR-045, FR-058) |
| Windows: a DAW holding a loaded bundle | not reachable: Phase 8 writes only into a fresh work dir; production extraction is Linux only (D4) |
| Windows `MAX_PATH` on deep staged paths | std converts long absolute paths to the `\\?\` form itself; FR-059's 4095-byte bound applies on every OS; SC-030 runs on Windows |
| macOS runner volume unexpectedly case-sensitive (or Linux case-insensitive) | SC-014 is pinned per target and goes red on the precondition, never green on the other branch |
| macOS `/var` → `/private/var` | both sides canonicalised (FR-046); SC-012 (3) proves it on the raw spelling |
| HFS+ / APFS name normalisation | SC-019's NFD row: refused safely as `TreeMismatch` |
| `flate2` backend change through feature unification (`zlib-rs`) | verified absent this session on **every** target triple (`cargo tree --workspace --target all -e features -i zlib-rs` / `-i flate2`, §0.2), so the macOS and Linux legs resolve the same backend as Windows; the gzip layer (`flate2::gz`) is backend-independent; SC-002 / SC-031 (incl. the gzip-layer rows) would show a difference on the leg where it changed |
| Windows device names or trailing dot / space in a staged name (a test leg writing to `NUL` or to a stripped name) | FR-041's `cfg(windows)` branch refuses them at step 6, before any write (§4.3.8); SC-020's per-OS blocks; production extraction is Linux only (D4) |
| A failing cleanup masks the real error | `cleanup` logs and returns the original error (§4.3.10 step 8); SC-010 (iii) |
| Gzip bomb / header flood / huge pax records | D8 bounds before memory or disk; SC-006, SC-031 |
| Offline | irrelevant: no network call; the entry may come from the cached feed |
| Admin prompts | none: no elevation on Linux, no process started |
| WebKitGTK quirks on Linux | not touched: no UI change in this phase |
| The test profile unwinds (`panic = "abort"` only in release, `Cargo.toml:54-59`) | no panic used as a simulated kill; `RemoveTreeMode::Noop` + planted leftovers (D5, SC-010) |
| A long CI leg | the new tests are small (largest archive: 100 000 zero bytes of padding; gzip bomb capped at 1 MiB injected); they run inside the existing `cargo test` step in parallel threads |

---

## 10. Precondition table (for the user)

| Id | Question | Recommendation | Effect if overruled |
|---|---|---|---|
| R-1 | PD-1: defer per-entry rule failures so the first code matches `listing.js` on every archive (vs. raise at once, matching only on single-defect archives) | **Defer** (two slots, bounded; makes FR-019's claim and `CONTRACT.md:341-343` literally true) | Stream-and-raise: simpler loop; FR-019's sentence must be narrowed to "single-defect archives" and SI-5's three rows dropped |
| R-2 | SI-4: substitute `ustar-prefix` (Fs, SC-024) and Mem (`exec-and-symlink`, SC-027) on every OS | **Substitute on every OS** (one test body, no `cfg` branches, no lane change) | Per-OS branches inside two `cargo.all` tests |

---

## Review notes

- Measured this session: link-bearing good fixtures (`grep -c '^l '` over the `.listing` files: `exec-and-symlink` 1,
  `long-link` 1, `pax-path` 1, others 0); every good `moduleinfo.json` Version 1.2.0, KAT 1.2.3 (`tar -xzOf`); KAT
  archive SHA-256 `5b980e5b…b62890` (`sha256sum`) vs its `.sha256` file `db287e49…` (the listing digest);
  `real-shaped.tar.gz` gzip header `1f 8b 08 00 00 00 00 00 02 03`.
- Measured this session: `cargo tree -e features -i flate2` (default features via `ureq`), `cargo tree -i zlib-rs`
  (nothing), clippy-guard lock's core entry without `flate2` (`:2020-2039`), workspace lock with it (`:2049-2054`).
  Revision 2 re-ran the backend check with `--target all` (§0.2): `-e features -i zlib-rs` nothing to print;
  `-e features -i flate2` features `any_impl`, `default`, `miniz_oxide`, `runtime_detection`, `rust_backend` only;
  `-i tar` no such package; no `name = "tar"` in either lock.
- Revision 2 (plan review, 15 issues, 2026-11-01): every issue applied, none rejected. Majors: the C4 rule table and
  streaming rows in SC-003 (all 33 Node reject rows and the accept rows, `ListingCode::Path` asserted); SC-009 (e1),
  (e2), (f) for pass 2's per-entry comparison and its `E_EXTRACT_CHANGED` mapping (step 10 now compares on the header
  before any create); the SI-9 production-body block in `prune_removes_real_link_unix`; the gzip-layer rows of SC-031
  and the `read_archive` rows of SC-010 (iv). Minors: §4.5's wrapper powers (`XOp` with paths and `write` bytes, the
  `open_archive` / `read_tree` / `canonicalize` / `kind_of` overrides, `TwoPhaseArchive`, a list of planned
  failures, `RemoveTreeMode::Fail`, `ReadFault`); SC-010 (iii) original-error-wins; SC-030 (c) asserted on the real
  root; SC-004's real-modes clause moved to a `cfg(unix)` block of `kat_archive_stages`; FR-046's dangling and
  outside-its-top rows (Fs in `symlinked_staging_root_unix`, Mem in `work_dir_must_be_inside_staging`); the PD-7 / PD-6
  Windows junction block in `symlink_entry_unsupported_windows` (+ the `cmd` TA-1 row; the two Windows-branch issues
  were one fix); the lock-level `tar` rule and the all-fields compile-fail literal plus a no-`pub`-field rule; FR-083's
  extended token list and the `tempfile` rule; the `--target all` backend check; FR-041's Windows reserved-name branch
  (rejecting the names, the review's second option, because a test-only Windows `FsExtractIo` that may write to
  `NUL` is itself a "never touch the real system" risk). No test name and no lane count changed (§4.7 stays 248 /
  11 / 35 / 30); the spec text for the additions is SI-12.
- The spec's line references re-read and found accurate: `download.rs:652-671` (guard), `:676-677` (prune lock),
  `test_support.rs:83-144`, `roots.rs:166-185`, `injection.rs:2579-2583`, `manager-ci.yml:117-120`, `:153-178`.
