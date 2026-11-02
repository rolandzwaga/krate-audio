# Feature Specification: Krate Manager Phase 8 — Safe Extraction and Staging (Linux tar.gz)

**Spec slug:** `krate-manager-phase8`
**Roadmap:** `specs/Krate-manager-roadmap.md` → Phase 8 (lines 634-659); Phase 1 C1 `payload` and C4 (lines 270-275,
296-301); Core Philosophy "Verify before anything touches the asset" (lines 69-70); Per-OS table, Linux row (line 118:
"The manager extracts the existing Linux-x64 `.tar.gz` itself"); Phase 9, the consumer (lines 661-703: "an install plan
from (staged tree from Phase 8, …)"); Dependency Graph edges 1→8, 4→8, 7→8, 8→9 (lines 954-955); Cross-Cutting
Constraints (lines 982-1023).
**Depends on:** Phase 1 (C4 rules and canonical listing, `tools/feed-contract/listing.js`, the listing and KAT
fixtures), Phase 4 (`Roots::staging_dir`, `AppContext`, `test_support::fresh_temp_roots`, `manager-ci.yml`), Phase 5
(the feed model: `PluginEntry`, `PlatformAsset.payload`, `InstallFacts.vst3_bundle`), Phase 7 (`VerifiedArtifact`,
`verify::verify_file`, `TestSigner`, the KAT signing key).
**Consumed by:** Phase 9 (the Linux `InstallDriver` installs only a `StagedTree`, edge 8→9).
**Status:** DRAFT, revision 4 (plan §0.2 SI-1 to SI-12 and tasks TA-1 applied); revision 3 applied the review issues
of 2026-11-01, then encoded the eight clarifications of 2026-11-01 (see Review notes and Clarifications):
specification only, no implementation.
**Date:** 2026-10-31 · verified against `feat/krate-manager` at `2499fb13` ("feat(krate-manager): Phase 7 download and
verify", `git rev-parse --short HEAD`, clean tree); `specs/krate-manager-phase8/` did not exist before this file.

---

## Overview

Phase 8 turns a verified Linux release tarball into a staged directory tree that Phase 9 may install. The new module
`core::extract` takes a `VerifiedArtifact` (Phase 7; its only constructor is verification, `verify.rs:113-162`) together
with the feed entry it was obtained for, checks that the two describe the same asset, and reads the archive **twice**
from one open handle. Pass 1 decompresses the gzip stream and parses every tar header with an in-crate reader that
applies the Phase 1 C4 rules in the contract's order (`CONTRACT.md` section 6, lines 310-337), builds the canonical
listing, and compares its line count and SHA-256 with the feed entry's `payload.files` / `payload.sha256` and its root
with the C4 Linux layout for the entry's `install.vst3_bundle`. **Nothing is written to disk before pass 1 accepts the
whole archive**, so every crafted archive is refused with zero bytes written anywhere. Pass 2 creates a fresh work
directory under `<staging>/extract/`, creates the planned directories, streams the file data into files opened
create-new, applies only the normalised exec bit, creates the in-bundle relative symlinks last, and hashes the archive
bytes it read so a file changed after verification is caught. Then a post-check proves the result physically:
every entry canonicalised lies inside the canonicalised work directory, and the canonical listing rebuilt from the tree
on disk (no link followed) equals the planned one. Only then does a `StagedTree` exist, a type with no public
constructor that Phase 9 consumes. No extended attribute, owner, group or timestamp from the archive is ever applied.
Phase 8 has no UI, no Tauri command and no process; it writes nowhere but `<staging>/extract/`.

---

## Scope

**In scope (roadmap Phase 8 components, lines 638-645):**

1. `core::extract`: tar.gz extraction of a **verified** Linux release asset into a fresh staging directory, rejecting
   absolute paths, `..` components, drive prefixes, entries that resolve outside the target, symlinks whose target
   escapes the bundle, hard links and device files; preserving in-bundle relative symlinks and exec bits; applying no
   extended attributes (lines 639-642; D1 to D8).
2. The staged-layout check: the C4 canonical listing of the extracted tree has the feed entry's `payload.files` and
   `payload.sha256`, and its root matches the C4 Linux layout; no `release.json` is read (lines 643-645; D3, D6).

**Non-goals (owned elsewhere):**

| Not in Phase 8 | Owner |
|---|---|
| Moving or copying the staged tree into `~/.vst3` or the preset folder, the journaled `<P>.vst3` swap, the recovery pass, install manifests, Install / Update / Remove UI and progress | Phase 9 (roadmap lines 668-689) |
| Hashing each placed file for a manifest | Phase 9 (line 683) |
| Extracting a Windows `.exe` or a macOS `.pkg`, or any zip | Never: the installers run verbatim (STRATEGY, roadmap line 23; Phase 10) |
| Downloading or verifying the archive | Phase 7 (done); Phase 8 accepts only its `VerifiedArtifact` |
| Releasing the downloaded artifact after install (`download::discard`) | Phase 9 calls it (Phase 7 FR-049) |
| A user-facing extraction progress bar or cancel control | Not in the roadmap for Phase 8; Phase 9 owns install progress (line 689) |
| Reading any installed bundle, or reading `moduleinfo.json` for anything but the one staged-version check of FR-057 | Phase 6 (detection) |
| Making the staged tree durable (`sync_all` on files or directories) | Phase 9 syncs what it places (FR-055) |
| Moving or renaming parts out of a `StagedTree` | Phase 9 copies out of it (FR-054) |
| Any change to `tools/feed-contract/` contracts, schemas, `listing.js` or committed fixtures, `plugins/`, `dsp/`, `release.yml`, `feed.yml`, `ci.yml`, `src-tauri/`, the webview | Never in this phase (FR-091) |

---

## Existing repo pieces (verified this session)

| File (lines) | What it is | What Phase 8 does with it |
|---|---|---|
| `specs/Krate-manager-roadmap.md:634-659` | Phase 8 components, outcomes, "Does NOT" | Source of every FR / SC (Traceability) |
| `specs/Krate-manager-roadmap.md:270-275`, `:296-301` | `payload: {files, sha256}` only on the Linux `tar.gz`; "the manager rebuilds the list from the verified, extracted Linux tree (Phase 8) and compares digests"; C4 layout and listing | ✅ FR-030 to FR-034 |
| `specs/Krate-manager-roadmap.md:668-683` | Phase 9 builds its plan from "staged tree from Phase 8" and stages `<P>.vst3.krate-new` itself | ✅ Why `StagedTree` is the hand-off (D7) and why Phase 8 never touches a plug-in root |
| `tools/feed-contract/CONTRACT.md:258-268` | C4 root layout: exactly `<vst3_bundle>` (d), `presets` (d), `README.txt` (f), `CHANGELOG.txt` (f); bundle holds `Contents/Resources/moduleinfo.json` and `Contents/x86_64-linux/<Name>.so`; `E_LAYOUT` | ✅ FR-033 |
| `CONTRACT.md:270-286` | Listing grammar: `<type> <mode> <path>[ -> <target>]`, modes normalised (file `0755` iff `mode & 0o111`, else `0644`; dir `0755`; link `0777`), `./` and root dropped, implied ancestors added as `d 0755`, sorted by UTF-8 bytes, LF after every line; digest = lower-case hex SHA-256 of the text | ✅ FR-020 to FR-023 |
| `CONTRACT.md:289-302` | KAT archive listing, 10 lines, digest `db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6` | ✅ SC-004 |
| `CONTRACT.md:304-308` | Archive sources: gzip, ustar `prefix` (POSIX magic only), GNU `L` / `K`, pax `x` `path` / `linkpath`, pax `g` ignored, every checksum verified; directory source uses `lstat`, never follows a link, refuses on win32 (`E_LISTING_NO_POSIX_MODES`) | ✅ FR-010 to FR-016; D4 |
| `CONTRACT.md:310-337` | FR-031 rules in check order (per-entry path rules, per-symlink form and containment, cross-entry through-symlink / duplicate / chain) with their `E_LISTING_*` codes; archive errors `E_LISTING_CHECKSUM`, `E_LISTING_TRUNCATED`, `E_LISTING_CORRUPT` | ✅ FR-017 to FR-019: the same rules, order and codes |
| `CONTRACT.md:339-347` | "Phase 8 obligations": apply the rules to the archive actually extracted, rebuild the listing from the extracted tree and compare `payload.files` / `payload.sha256` and the layout; physically check every entry with `std::fs::canonicalize` inside the canonicalised staging dir ("A lexical check alone is not enough"); preserve in-bundle relative symlinks and exec bits, apply no extended attributes, never add quarantine | ✅ The phase's acceptance contract: FR-017, FR-030 to FR-033, FR-040 to FR-046 |
| `CONTRACT.md:60-69` | C1 `platforms.<key>.payload` "required on `tar.gz`", read by "manager (Phase 8 staged-layout check)" | ✅ FR-030 |
| `CONTRACT.md:71`, `:141-142` | `install.vst3_bundle` "manager (… Phase 8 layout …)"; reader rule 8: re-check the pattern before joining a value to a root | ✅ FR-002, FR-033 |
| `tools/feed-contract/listing.js:31-53` | `decode` (fatal UTF-8), `checkCharacters` (empty; NUL/CR/LF; leading `/` → `E_LISTING_ABSOLUTE`; `^[A-Za-z]:` → `E_LISTING_DRIVE`; `\` → `E_LISTING_BACKSLASH`) | ✅ The Rust port (FR-017) |
| `listing.js:56-72`, `:75-95`, `:104-108` | `canonicalPath` (` -> ` refused, `./` dropped, root `.` / `./` dropped, empty / `.` / `..` segment → `E_LISTING_DOTDOT`, non f/d/l → `E_LISTING_TYPE`); `checkLink` (root symlink, form rule, lexical containment in the top-level entry); `modeOf` | ✅ FR-017, FR-021 |
| `listing.js:112-159` | `listingFromEntries`: rule 3 (a) through-symlink, (b) duplicates then implied ancestors (file vs implied dir → `E_LISTING_DUPLICATE`), (c) chain; sort by `Buffer.compare` | ✅ FR-018, FR-020 |
| `listing.js:169-191` | `octal` (leading spaces, octal digits, then NUL/space only; base-256 high bit → `E_LISTING_CORRUPT`); `checkChecksum` accepts the unsigned **or** signed byte sum | ✅ FR-012 |
| `listing.js:194-215` | `paxRecords` (`<len> <key>=<value>\n`, length must end at an LF, `=` required) ; `TYPES = {'0','\0' → f, '5' → d, '2' → l}` | ✅ FR-013, FR-014 |
| `listing.js:220-274` | `readTarEntries`: an all-zero full block ends the archive; a header past the data end, data past the stream end, a missing end block, or a dangling `L` / `K` / `x` → `E_LISTING_TRUNCATED`; precedence pax `path` > GNU `L` > header name (and pax `linkpath` > `K` > header linkname); ustar prefix joined only under magic `ustar\0` version `00` | ✅ FR-011, FR-013 |
| `listing.js:277-285`, `:328-330`, `:359-382` | `listingFromTarGz` (`zlib.gunzipSync`, error → `E_LISTING_CORRUPT`); `digest`; `checkLinuxLayout` (six required items, missing / wrong type / extra root item → `E_LAYOUT`) | ✅ FR-010, FR-022, FR-033 |
| `listing.js:290-318` | `listingFromDirectory`: `readdir` + `lstat` + `readlink`, names as bytes; throws `E_LISTING_NO_POSIX_MODES` on win32 | ✅ The model of FR-043's on-disk listing; D4 for Windows |
| `tools/feed-contract/fixtures/listings/README.md` | `good/` (9 archives with `.listing` and `.sha256`) and `bad/` (22 archives with `.expected-error`): absolute, `..`, drive, backslash, two symlink escapes, root symlink, hard link, char device, duplicate, file/dir clash, truncated, no end block, bad checksum, malformed pax, base-256 size, dotdot chain, symlink chain, self link, through-symlink, two layout cases; "The bad archives are only ever parsed, never extracted"; `.gitattributes` keeps them byte-exact | ✅ Read in place by the parity tests (SC-002, SC-003); Phase 8 adds runtime-built archives only in temp dirs, never committed (FR-085) |
| Entry counts, measured this session with `listing.js` `readTarEntries` (count script in the session scratchpad, output in Review notes) | good: `dot-slash` 11 entries (the root `./` among them) for 10 lines; `exec-and-symlink` 11 / 11; `implicit-dirs` 5 / 10; `long-link` 13; `long-name` 12; `pax-global` 10; `pax-path` 18; `real-shaped` 14 / 14; `ustar-prefix` 12; KAT 10. bad: every per-entry defect archive is the 10-entry base tree plus the defect as the **last** entry (11 entries; `symlink-chain` and `through-symlink` 12, `symlink-dotdot-chain` 14); `layout-extra-root-file` 11, `layout-missing-presets` 8; `bad-checksum`, `base256-size`, `no-end-block`, `pax-malformed`, `truncated` fail while reading. No fixture exceeds 18 entries | ✅ FR-015's count rule (root entries not counted, rules before the bound); the per-row inputs of SC-002, SC-003, SC-006 |
| `listings/good/real-shaped.listing` / `.sha256` | 14 lines, digest `064e90ee22cae2f54d4d58b10c240cde9ca7256267d1268db4c20a872e59ff39` (spaces in preset names) | ✅ SC-005 |
| `listings/good/exec-and-symlink.listing` / `.sha256` | `f 0755 …/Alpha.so`, `l 0777 …/libalpha.so -> Alpha.so`; digest `baad75026258c646e2887dd01d00dbd85f57ec43111430c2a57563da80659704` | ✅ SC-005, SC-012 |
| `tools/feed-contract/fixtures/kat/README.md:10` | `KatPlugin-1.2.3-Linux-x64.tar.gz`, 452 bytes, sha256 `5b980e5b…b62890`, C4 layout; its `.sha256` file holds the listing digest `db287e49…` (Phase 7 plan SI-1) | ✅ SC-004 (verified with the KAT key, then extracted) |
| `.github/workflows/release.yml:410-435` | Linux staging (`mv artifact staging/<Name>.vst3`, `cp -r presets`, `README.txt`, `CHANGELOG.txt`), then `cd staging; tar -czvf ../<Name>-<ver>-Linux-x64.tar.gz *` (GNU tar on `ubuntu-latest`, no wrapping folder) | ✅ What a real archive looks like (GNU format, single gzip member, owner the runner's uid); unchanged |
| `apps/krate-manager/crates/core/src/verify.rs:113-162` | `VerifiedArtifact { path, name, version, platform, asset, size, sha256, format }`, private fields, accessors, no public constructor (compile-fail doctests `:92-105`) | ✅ The only input (FR-001); no field added (D7) |
| `verify.rs:165-287`, `:290-307` | `pub(crate) fn verify_file(path, &Expected, &SigningKey)`; `hex_lower`; `#[cfg(test)] sha256_of` (PD-13: `sha2` stays in `verify.rs`) | 🔶 Gains one crate-private streaming hasher, `verify::Sha256Stream` (`new`, `update`, `finish -> [u8; 32]`), so `extract.rs` hashes without naming `sha2` (FR-082) |
| `crates/core/src/feed.rs:44-62`, `:64-80`, `:82-87`, `:121-128`, `:136-150` | `PluginEntry { name, version_text, …, release }`; `StableRelease { platforms, install, … }`; `PlatformAsset { asset_name, size, sha256, format, payload, … }`; `Payload { files: u64, sha256: [u8; 32] }`; `AssetFormat::{InnoExe, Pkg, TarGz}`; `InstallFacts { vst3_bundle: PlainName, … }`; `PlainName` (no public constructor) | ✅ The entry Phase 8 checks the artifact against (FR-002); `payload` and `vst3_bundle` come from it |
| `crates/core/src/contract.rs:1037-1039` | `vst3_bundle` pattern `^[A-Za-z0-9][A-Za-z0-9 _.-]*\.vst3$` (the feed does not tie it to `<Name>.vst3`; only C2 does, `CONTRACT.md:184`) | ✅ Why the layout check uses the entry's `vst3_bundle`, not `<name>.vst3` (FR-033) |
| `crates/core/src/download.rs:28`, `:122-157`, `:676-690`, `:1319-1325`, `:1480-1516` | `DOWNLOADS_DIR = "downloads"`; `staged_paths` (one checked join, `E_DOWNLOAD_PATH`); `prune` iterates only `downloads/`; `obtain_and(…, next: impl FnOnce(VerifiedArtifact) -> T)`; `discard(artifact, io)` | ✅ Phase 8's work dirs live in a sibling `extract/` that Phase 7's prune never reads (D2); `staged_paths` is the model of FR-041's join |
| `download.rs:209-215`, `:547-578` | `DownloadEnv<'a> { ctx, io, policy, cancel }` (`Copy`, borrows); `StagingIo` trait + `FsStagingIo` (every filesystem step injectable, `kind_of` never follows a link) | ✅ The pattern of `ExtractEnv` and `ExtractIo` (D5) |
| `crates/core/src/roots.rs:6`, `:103-105`, `:199-201` | `STAGING_DIR = "staging"`; `Roots::new` requires `staging_dir == state_dir/staging`; `staging_dir()` | ✅ The only root Phase 8 writes under (FR-040) |
| `roots.rs:80-112`, `:166-171`, `:175-185` | `Roots::new` accepts any absolute, normal path set (no canonicalisation); `#[cfg(any(test, feature = "test-support"))] pub(crate) fn sandboxed(self, base)`; `check_sandboxed` is a **lexical** `starts_with` against the sandbox base | ✅ Why a non-canonical test `Roots` must spell its sandbox base the same way as its roots (FR-089, SC-012) |
| `crates/core/src/context.rs:51-60` | `AppContext { roots, settings, feed_url, http, signing_key }` | ✅ Read only (`ctx.roots.staging_dir()`); no field added |
| `crates/core/src/test_support.rs:48-56`, `:60-78`, `:83-133`, `:135-144` | `canonical_temp_dir()` (canonicalised `std::env::temp_dir()`); `check_under_temp` returns the **canonical** base; `temp_roots_in` builds every root, `state_dir` and `staging_dir` included, from that canonical base and sandboxes to it (`:93-116`); `temp_roots(base)`; `fresh_temp_roots() -> (TempDir, Roots)` | ✅ Every Phase 8 test stages under a `fresh_temp_roots()` TempDir; 🔶 because its roots are already canonical (on macOS `/private/var/folders/…`), the symlinked-parent cases use the new FR-089 constructors instead |
| `test_support.rs:153-179`, `:186-192`, `:194`, `:284-289` | the capture logger (`Capture`, `install_capture`, `take_mine`, FR-088 of Phase 4); `kat_signing_key()` (the KAT public key via `SigningKey::from_text`, inside the allowlisted `test_support.rs`) | ✅ FR-052's log assertion (SC-021); the KAT key for SC-004 without a `from_text (` in `extract_tests.rs` (FR-084) |
| `crates/core/src/test_signer.rs:10-71` | `#[cfg(test)] TestSigner { new, key, sign, sign_c3 }`, `c3_comment` | ✅ Signs fixture archives so tests obtain a real `VerifiedArtifact` through `verify_file` (FR-084) |
| `crates/core/src/state_lock.rs:53-75` | One process per state dir | ✅ Why a leftover work dir from a crash is never live and may be pruned (D2) |
| `crates/core/src/detect.rs:207-247` | `pub struct Listing` / `ListingLimits` (the Phase 6 directory scan of VST3 roots) | ✅ Name clash avoided: Phase 8's type is `CanonicalListing` (FR-020) |
| `apps/krate-manager/Cargo.toml:17`; `crates/core/Cargo.toml:15-31`, `:34-37` | `flate2 = "1.1.10"` pinned; core `[dependencies]` (13 keys); `[dev-dependencies] flate2, minisign` | 🔶 `flate2` moves from core's `[dev-dependencies]` to `[dependencies]` (FR-080) |
| `Cargo.lock:1037-1043`; `tests/fixtures/clippy-guard/Cargo.lock:1028` | `flate2` 1.1.10 → `crc32fast`, `miniz_oxide 0.9.1` already locked in both | ✅ No new crate enters either lock (FR-080) |
| `crates/core/tests/injection.rs:106-120`, `:299-318`, `:2839-2856`, `:3354-3365` | `FR007_DEPENDENCIES` (13 names); `CONFINED_TOML_KEYS` (12); `verify_confinement_findings` (`minisign_verify`, `sha2` only in `verify.rs`); `core_dependencies_are_the_fr007_set` asserts dev-dependencies `{flate2, minisign}` | 🔶 14 names (`flate2` added), dev-dependencies `{minisign}`, a `flate2` TOML row, and FR-082 / FR-083 rules for `extract.rs` |
| `injection.rs:2479-2510`, `:2579-2583`, `:2584-2594`, `:2595-2621`, `:4345` | `minting_route_findings(name, callers)` reports every `<name> (` call in a file not in `callers`; `verify_file` callers `[VERIFY_RS, DOWNLOAD_RS, VERIFY_TESTS_RS, DOWNLOAD_TESTS_RS]`; `from_text` callers `[VERIFY_RS, TEST_SIGNER_RS, TEST_SUPPORT_RS, VERIFY_TESTS_RS, DOWNLOAD_TESTS_RS]`; `struct Expected` checked for declaration and field privacy only (construction sites are not scoped); planted cases in `verified_artifact_scanner_reports_plants` | 🔶 `verify_file` callers gain `EXTRACT_TESTS_RS` (FR-088); `from_text` and the `Expected` check unchanged |
| `crates/core/src/lib.rs:1-28` | Module list (`pub mod download; … pub mod verify;`) | 🔶 `pub mod extract;` |
| `apps/krate-manager/tests/required-tests.json` (`cargo.all` 224, `windows` 10, `macos` 28, `linux` 25); `.github/workflows/manager-ci.yml:117-120`, `:153-178` | step `cargo test` runs `cargo test --workspace --locked 2>&1 \| tee "$RUNNER_TEMP/cargo-test.txt"` (no `-p`, no filter); step `required tests ran` requires every name in `[...req.cargo.all, ...req.cargo[key]]` to have exactly one `test <name> ... ok` line (`if (c !== 1) bad.push(…)`), so a lane name not compiled on a leg turns that leg red; the existing lanes are disjoint | 🔶 FR-086 names; the workflow is unchanged |
| `apps/krate-manager/Cargo.toml:54-58` | `[profile.release]` alone sets `panic = "abort"`; the test profile unwinds | ✅ Why a panic is not a simulated kill (SC-010, D5) |
| `tools/feed-contract/tests/manager-ci-shape.test.js:337`, `:816` | `EXPECTED_REQUIRED`, deep-compared with `required-tests.json` | 🔶 Gains exactly FR-086's names (the Phase 7 precedent) |
| `specs/krate-manager-phase7/spec.md:246-249` (D7) | Threat model: a same-user process can rewrite the staged file after verification; the privilege boundary is Phase 10's helper | 🔶 Narrowed for Linux by D6: the staged tree is proven to come from bytes whose SHA-256 equals the verified one |

---

## External facts used (verified this session)

| Fact | Source |
|---|---|
| `flate2` 1.1.10 (published 2026-08-28, MIT OR Apache-2.0, `rust-version` 1.67, the newest version): default features `rust_backend` (pure-Rust `miniz_oxide`) and `runtime_detection`. `flate2::read::MultiGzDecoder::new(r)` "decodes a gzip file that may have multiple members … returns Ok(0) once the underlying reader does"; per member it checks the CRC-32 and the length in the footer and returns an error on a mismatch, and when bytes follow a member it parses them as the next gzip header (so trailing non-gzip bytes are an error). `GzDecoder` instead stops after one member | https://crates.io/api/v1/crates/flate2 ; local registry copy `flate2-1.1.10/Cargo.toml` `[features]`, `src/gz/read.rs:95-108`, `:222-233`, `:272`, `src/gz/bufread.rs:305-353` (read this session); https://docs.rs/flate2/1.1.10/flate2/read/struct.MultiGzDecoder.html |
| Node v24.7.0 `zlib.gunzipSync` (the `listing.js` gzip step) decodes concatenated members (`gzip("abc") ‖ gzip("def")` → `abcdef`), ignores trailing zero bytes, and fails on trailing non-gzip bytes ("incorrect header check") | Run on this box this session (`node -e …`, output recorded in Review notes) |
| `tar` 0.4.46 (published 2026-05-18, the newest version) is **not** used: `Archive::entries` treats EOF at a header boundary as the end of the archive (`src/archive.rs:296-299`: "EOF is an indicator that we are at the end of the archive"), so a missing end-of-archive block passes where C4 says `E_LISTING_TRUNCATED`; its checksum compares the unsigned sum only (`:316-325`), where `listing.js:177-191` also accepts the signed sum; and its numeric fields accept GNU base-256 (`src/header.rs:1512-1513`), where C4 says `E_LISTING_CORRUPT` | https://crates.io/api/v1/crates/tar ; crate source 0.4.46 downloaded from https://static.crates.io/crates/tar/tar-0.4.46.crate and read this session |
| `std::fs::OpenOptions::create_new`: "No file is allowed to exist at the target location, also no (dangling) symlink. In this way, if the call succeeds, the file returned is guaranteed to be new … This option is useful because it is atomic" (stable 1.9.0) | https://doc.rust-lang.org/std/fs/struct.OpenOptions.html#method.create_new (std 1.99.1) |
| `std::fs::canonicalize`: "Returns the canonical, absolute form of a path with all intermediate components normalized and symbolic links resolved"; `realpath` on Unix, `CreateFile` + `GetFinalPathNameByHandle` on Windows (extended-length `\\?\` form); errors when the path does not exist or a non-final component is not a directory | https://doc.rust-lang.org/std/fs/fn.canonicalize.html (std 1.99.1) |
| `std::os::unix::fs::symlink(original, link)`: "The `link` path will be a symbolic link pointing to the `original` path" (Unix only, stable 1.1.0) | https://doc.rust-lang.org/std/os/unix/fs/fn.symlink.html |
| `std::os::unix::fs::PermissionsExt::{mode, set_mode, from_mode}` (Unix only, stable 1.1.0) | https://doc.rust-lang.org/std/os/unix/fs/trait.PermissionsExt.html |
| `tempfile` 3.27.0 (the workspace pin, `Cargo.toml`): `Builder::prefix`, `Builder::tempdir_in(dir) -> io::Result<TempDir>` (`src/lib.rs:326`, `:621`); `TempDir::keep(self) -> PathBuf` (`src/dir/mod.rs:418`); `impl Drop for TempDir` runs `remove_dir_all(self.path())` unless cleanup was disabled (`src/dir/mod.rs:498-503`), so a `TempDir` dropped during unwinding deletes its directory | local registry copy `tempfile-3.27.0` (read this session); https://docs.rs/tempfile/3.27.0/tempfile/struct.Builder.html |
| Rust `io::ErrorKind::{StorageFull (1.83), QuotaExceeded (1.85), PermissionDenied, ReadOnlyFilesystem (1.83), AlreadyExists, Unsupported}`; toolchain `rustc 1.99.1`, workspace `rust-version = "1.90"` | https://doc.rust-lang.org/std/io/enum.ErrorKind.html ; `rustc --version` this session |

---

## Decisions taken in this spec (open to challenge in the clarify scan)

The roadmap defers no decision to this spec by name. D1 to D9 settle what its Phase 8 text names but does not pin down.

- **D1, an in-crate tar reader, not the `tar` crate.** CONTRACT.md requires Phase 8 to apply "the same rules, in the same
  order" as `listing.js` to the archive it extracts (`:341-343`). The `tar` crate differs on three of them (External
  facts: no end block required, unsigned checksum only, base-256 accepted), and its `unpack` / `unpack_in` would put the
  write path in code Phase 8 does not control. So `extract.rs` holds a header reader that is a line-by-line port of
  `listing.js:169-274` (octal fields, checksum unsigned-or-signed, pax records, GNU `L` / `K`, pax `g` ignored, ustar
  prefix under POSIX magic only, precedence pax > GNU > header), and the rule engine is a port of `listing.js:31-159`.
  Gzip comes from `flate2`'s `bufread` decoders, driven member by member so that it matches Node's behaviour exactly,
  including the one place a plain `MultiGzDecoder` differs: after a member ends, a remainder of only zero bytes is
  accepted and ignored (as Node's `zlib.gunzipSync` does), any other trailing bytes are `E_LISTING_CORRUPT` (FR-010,
  Q8). The parity tests (SC-002, SC-003, SC-031) prove the two readers agree on every committed fixture and on zero
  padding. One residual difference remains and is measured, not hidden (SC-031): Node also ignores a remainder whose
  first byte is zero but which holds a later non-zero byte (`00 00 01`); the Rust reader refuses it.
- **D2, where staged trees live.** `<staging>/extract/` (`EXTRACT_DIR = "extract"`), a sibling of Phase 7's
  `downloads/`, which Phase 7's prune never reads (`download.rs:676-690`). Each extraction gets a fresh work directory
  `extract/<sha256hex>.<random>` made by `ExtractIo::create_work_dir`, which `FsExtractIo` implements as
  `tempfile::Builder::new().prefix("<hex>.").tempdir_in(extract)` followed at once by `TempDir::keep()`, so no
  `TempDir` value outlives the call and no `Drop` (`tempfile` `src/dir/mod.rs:498-503`) ever removes anything behind
  `ExtractIo`'s back. On success the directory becomes the `StagedTree`'s root; on any failure `stage` removes it
  through `ExtractIo::remove_tree`, the one removal path. A registry of live roots (in process, one manager process
  per state dir, `state_lock.rs:53-75`) holds **both** in-progress work dirs and finished `StagedTree` roots: a work
  dir is registered live the moment `create_work_dir` returns, through a drop guard (the model is
  `download.rs:652-671`) that moves it to the finished state on success and deregisters it on every other exit, and
  every prune runs under the registry lock (the model is `download.rs:676-677`). So a concurrent `stage` can never
  remove another call's in-progress or finished tree, and concurrent `stage` calls are independent (Q1). A prune
  removes each `extract/` entry that is not live: the leftovers of a crash or of a tree Phase 9 dropped without
  `discard`. It runs at the start of pass 2 of every `stage` and in the public `extract::prune(env)` that Phase 9's
  start-up recovery pass calls (Q6, FR-040, FR-058). Two extractions of the same artifact never share a directory.
- **D3, two passes over one handle, nothing written before pass 1 accepts.** Pass 1 reads headers and skips data,
  building the plan (every entry's canonical path, type, normalised mode, size, link target) and the canonical
  listing, and checks rules, limits, payload and layout. Only then does pass 2 create anything. Every crafted-archive
  rejection therefore happens before the first filesystem call (SC-006 counts zero `ExtractIo` writes). Pass 2
  re-parses the same headers and must reproduce pass 1's plan entry for entry (`E_EXTRACT_CHANGED` otherwise).
- **D4, Windows and macOS run the logic, Linux is the product.** Production extraction happens only on Linux (the Linux
  `InstallDriver`, Phase 9); the code compiles and its tests run on all three OSes. NTFS carries no POSIX modes and a
  symlink needs a privilege on Windows (Phase 1 D4, `CONTRACT.md:307-308`): on non-Unix targets `FsExtractIo` writes
  files and directories without modes and reports `supports_symlinks() == false`, so `stage` refuses a plan holding a
  symlink with `E_EXTRACT_UNSUPPORTED` after pass 1 and before pass 2 creates anything (FR-042); its `read_tree`
  reports no modes, and the on-disk listing (FR-047) takes paths and types from disk and modes from the plan. The Windows evidence for symlinks and exec bits is
  the in-memory `MemExtractIo` (test support), as the roadmap's "unit test on Windows for the logic" (line 652) allows;
  the real-filesystem evidence is PENDING-CI on the macOS and Linux legs (line 653).
- **D5, filesystem steps are injected.** Every write and every post-check read goes through an `ExtractIo` trait (open
  archive, list `extract/`, create the work dir, create dir, create file new, write, set mode, create symlink, remove
  tree, kind of a path without following links, read tree without following links, canonicalize, read one bounded
  file for the version check of FR-057, remove one `extract/` entry by kind (FR-058), and the capability
  query `supports_symlinks`; there is deliberately **no** `sync_file` / `sync_dir`, FR-055), as `download.rs` does with `StagingIo` (`:547-561`). Every method returns
  `io::Result<_>` (or a plain value for `supports_symlinks`); `stage` alone turns an `io::Error` into an
  `ExtractError` (FR-045), and no `ExtractIo` method produces an `E_*` code. Production is `FsExtractIo`; tests inject
  `StorageFull`, `PermissionDenied`, a cleanup that never runs (a `remove_tree` that returns `Ok` and removes nothing,
  the state a killed process leaves), a counting wrapper and a fail-closed recording wrapper (FR-087). A panic is
  **not** used as a simulated kill: the test profile unwinds (only `[profile.release]` sets `panic = "abort"`,
  `Cargo.toml:54-58`), so a panic would run every destructor and fake a state the product cannot be in.
  `ExtractEnv<'a> { ctx, io, limits }` (borrows, `Copy`) carries the inputs, like `DownloadEnv`.
- **D6, the staged tree comes from the verified bytes.** Both passes hash the raw archive bytes they read through
  `verify::Sha256Stream`, reading the raw file to EOF; each must equal `artifact.sha256()`, or the work dir (if one
  exists) is removed and the result is `E_EXTRACT_CHANGED`. Precedence (FR-044): an error pass 1 finds while reading
  (gzip, tar, rule or limit) is reported as is, because the hash cannot be completed past it; the pass-1 hash is
  compared at the end of pass 1, before FR-030 to FR-033 and before any write. This closes, for the staged tree, Phase 7 D7's window in which a same-user process rewrites the
  staged file after verification: whatever is extracted is provably the signed archive. A rewrite of the staged
  *tree* after `stage` returns is outside this phase's threat model for the same reason as Phase 7 D7 (that process
  can write `~/.vst3` directly), and Phase 9 owns the copy into the roots.
- **D7, `StagedTree` is the hand-off; `VerifiedArtifact` is not changed.** `stage(env, entry, &artifact)` borrows the
  artifact (Phase 9 still calls `download::discard` on it after installing) and takes the feed entry, from which the
  `payload` and `install.vst3_bundle` come, after requiring that the entry describes exactly that artifact (name,
  version, platform key present, asset name, size, SHA-256, format `TarGz`). Carrying `payload` inside
  `VerifiedArtifact` was not chosen: it would change Phase 7's type, its constructor and its tests for a fact the
  entry already holds, and the equality check makes a mismatched pair a named error.
- **D8, bounds before memory or disk.** `ExtractLimits { max_headers: 100_000, max_tar_bytes: 4 GiB,
  max_meta_bytes: 64 KiB, max_path_bytes: 4096, max_segment_bytes: 255, max_joined_bytes: 4095,
  max_moduleinfo_bytes: 1 MiB }` (`ExtractLimits::PRODUCTION`), injected so tests use small values.
  Decompressed bytes are counted as they are read (a gzip bomb stops at the bound, not at the end); every `L`, `K`, `x`
  or `g` record's data is read into memory only up to `max_meta_bytes`; a header flood stops at `max_headers`; and an
  archive with more **counted** entries than the entry's `payload.files` cannot produce that listing (each counted entry
  yields exactly one listing line and implied ancestors only add lines), so it stops at the `payload.files + 1`-th
  counted entry. A counted entry is an f / d / l entry that passed its per-entry rules (FR-017, FR-018) and survived
  `canonicalPath` (`listing.js:56-72`): the archive-root `.` / `./` directory, which `canonicalPath` drops
  (`return null`) and which may occur more than once, and the pax `g` / `x` and GNU `L` / `K` records, which are not
  entries, are not counted (the last four count only towards `max_headers`). The `dot-slash` fixture has 11 entries
  for 10 lines because of its root `./` (fixtures README); counting it would refuse a C4-valid archive. Every bound is
  `E_EXTRACT_LIMIT` naming which (`headers`, `tar_bytes`, `meta`, `path`, `segment`, `joined`, `entries`, `moduleinfo`).
  `segment` is one path segment over 255 bytes (the Linux file-name limit, FR-015); `joined` is the longest planned
  path joined to the work dir exceeding 4095 bytes (Linux `PATH_MAX`, FR-059); `moduleinfo` is the staged
  `moduleinfo.json` over `max_moduleinfo_bytes` (FR-057). `listing.js` has none of these; they only
  refuse archives no release produces (a real Linux tarball holds hundreds of entries and tens of MB).
- **D9, codes.** A rule `listing.js` names keeps its `E_LISTING_*` / `E_LAYOUT` code (the Phase 5 practice of reusing
  contract codes, `CONTRACT.md:28`), and the payload comparisons reuse `E_LISTING_COUNT` / `E_LISTING_DIGEST` (the codes
  C2 uses for the same comparison, `CONTRACT.md:189-191`). Codes only Phase 8 can raise are `E_EXTRACT_*` (including `E_EXTRACT_VERSION`, FR-057). Every error
  `Display` begins with its code; logs name the plugin, version, asset name and the archive entry path, never an
  absolute path outside the state dir.

---

## Functional Requirements

### A. Inputs and the entry binding (roadmap lines 639, 643-645; D7)

- **FR-001** `extract::stage(env: ExtractEnv<'_>, entry: &PluginEntry, artifact: &VerifiedArtifact) ->
  Result<StagedTree, ExtractError>` is the only public extraction entry point. There is no public function that takes a
  path, a reader or bytes and extracts them; the pure listing functions of FR-020 to FR-023 write nothing. The only
  other public function that touches the filesystem is `extract::prune(env)` (FR-058), which only removes and extracts
  nothing; `discard` (FR-051) removes one tree.
- **FR-002** Before any I/O, `stage` requires, in this order, and refuses the first failure with
  `E_EXTRACT_ENTRY_MISMATCH { field }`: `artifact.format() == AssetFormat::TarGz` (field `format`; also refuses every
  `.exe` and `.pkg`); `entry.name == artifact.name()`; `entry.version_text == artifact.version()`; `entry.release` is
  `Some` (field `release`, a v1-only entry); `release.platforms[artifact.platform()]` exists (field `platform`); that
  asset's `asset_name`, `size`, `sha256`, `format` equal the artifact's; its `payload` is `Some` (field `payload`);
  `release.install` is `Some` (field `install`). `install.vst3_bundle` is re-checked against the C1 pattern
  (`contract.rs:1037-1039`) and against "one normal path component" before it is used (reader rule 8, `CONTRACT.md:141-142`).
- **FR-003** Phase 8 reads only the artifact file (`artifact.path()`) and writes only under
  `ctx.roots.staging_dir()/extract`. No other path from the feed or the archive is opened, created or removed.

### B. Archive reading (roadmap lines 639-642; D1, D8)

- **FR-009** Pass 1 is the crate-private `plan_archive(reader: impl Read, limits: &ExtractLimits, max_entries: u64) ->
  Result<ArchivePlan, ExtractError>`: FR-010 to FR-019 and the listing of FR-020, nothing else (no payload, no layout,
  no `ExtractIo`). FR-030 to FR-033 are the crate-private `check_staged_layout(listing: &CanonicalListing, payload:
  &Payload, vst3_bundle: &PlainName) -> Result<(), ExtractError>`. `stage` calls `plan_archive` with `max_entries =
  payload.files`, then compares the pass-1 hash (FR-044), then calls `check_staged_layout`. Tests call the two parts
  separately with the inputs each SC states (SC-002, SC-003, SC-007); in-crate tests build a `PlainName` through
  `PlainName::checked` (`feed.rs:148-152`).
- **FR-010** The archive is opened once through `ExtractIo::open_archive` and read member by member through
  `flate2::bufread::GzDecoder` (a loop over members, or a peek before a multi-member decoder reads the next header);
  a gzip error (bad magic, bad CRC or length, truncated member, trailing non-gzip bytes) is `E_LISTING_CORRUPT`. As in
  `listing.js` (Node `zlib.gunzipSync`), after a member ends a remainder consisting **only of zero bytes** (any
  length, including zero) is accepted and ignored, not an error; a remainder holding any non-zero byte is parsed as
  the next gzip header and is `E_LISTING_CORRUPT` when it is not one. Zero padding is counted against `max_tar_bytes`
  like every drained byte and is hashed with the rest of the raw file (FR-044). `tools/feed-contract/` is not changed
  (FR-091). After the
  end-of-archive block the decoder is drained to EOF (counted against `max_tar_bytes`) so the final CRC is checked;
  the drained bytes are never interpreted, as in `listing.js:228-232`.
- **FR-011** Header loop, as `listing.js:226-273`: a full all-zero 512-byte block ends the archive; a short block that
  is not all zero, data running past the stream end, the stream ending before an end block, or an `L` / `K` / `x`
  record with no following header is `E_LISTING_TRUNCATED`.
- **FR-012** Every header's checksum is verified with the unsigned or the signed byte sum (`E_LISTING_CHECKSUM`); the
  `mode` and `size` fields must be octal (optional leading spaces, then NUL / space only), and a base-256 field or any
  other form is `E_LISTING_CORRUPT` (`listing.js:169-191`).
- **FR-013** GNU `L` / `K` set the next header's path / link target (trailing NULs cut); a pax `x` record's `path` /
  `linkpath` set them with precedence over `L` / `K` and over the header's own fields; every other pax key (including
  `SCHILY.xattr.*`, `uid`, `uname`, `mtime`) is ignored and never applied; pax `g` is ignored; a malformed pax record is
  `E_LISTING_CORRUPT` (`listing.js:194-213`, `:248-256`). The ustar `prefix` is joined (`prefix/name`) only under the
  POSIX magic `ustar\0` with version `00` (`listing.js:259-262`).
- **FR-014** Typeflags `0` and NUL are files, `5` directories, `2` symlinks; every other typeflag (hard link `1`, char
  `3`, block `4`, FIFO `6`, contiguous `7`, GNU `D` / `S` / `V` / `M`, …) yields an entry of type other that the rules
  refuse with `E_LISTING_TYPE` (`listing.js:215`, `:68-70`).
- **FR-015** Limits (D8): more than `max_headers` headers of any kind (`which: "headers"`); more than `max_tar_bytes`
  decompressed bytes (`"tar_bytes"`); an `L` / `K` / `x` / `g` record larger than `max_meta_bytes` (`"meta"`); an entry
  path or link target longer than `max_path_bytes` bytes after FR-013's precedence is applied (`"path"`); an entry path
  holding a segment longer than `max_segment_bytes` (255) bytes, split on `/` after FR-013's precedence is applied
  (`"segment"`; a symlink target's segments are not bounded here, FR-018 and `max_path_bytes` cover them, since a
  target is never a file name on disk); more counted
  entries than `max_entries` (`"entries"`, D8: only f / d / l entries that are not the archive root `.` / `./`) →
  `E_EXTRACT_LIMIT { which }`, raised as soon as the bound is crossed, with no further byte read. A per-entry rule
  failure (FR-017, FR-018) is recorded, not raised: pass 1 keeps reading, so an archive error later in the stream wins,
  as in `listing.js`; a Phase 8 bound stops reading at once; the reported code is the stopping archive error, else the
  first rule-1 failure, else the first rule-2 failure, else the bound, else FR-019 (plan PD-1) (plan SI-5). A failing
  entry is not counted towards `entries` and not stored. Order on one entry:
  its header-level bounds (`headers`, `meta`, `path`, `segment`) while it is read, then its per-entry rules (FR-017, FR-018), then
  the `entries` bound; so an entry that breaks a per-entry rule is refused by that rule even when it is also the
  `max_entries + 1`-th entry. The cross-entry rules (FR-019) run after the last header, so an archive with more counted
  entries than `max_entries` is refused with `entries` before FR-019 is reached.
- **FR-016** Header owner, group, `uname`, `gname`, `mtime` and device numbers are read only as far as the checksum
  needs them and are never applied; of the header `mode`, only `mode & 0o111 != 0` reaches the plan (FR-020), so
  setuid / setgid / sticky bits are dropped there (SC-022).

### C. The C4 rules (roadmap lines 639-642; `CONTRACT.md:310-337`)

- **FR-017** Per entry, in `listing.js:31-72`'s order: valid UTF-8, not empty, no NUL / CR / LF, no ` -> `
  (`E_LISTING_PATH`); no leading `/` (`E_LISTING_ABSOLUTE`; this also catches `//server/share`); no `^[A-Za-z]:`
  (`E_LISTING_DRIVE`); no `\` (`E_LISTING_BACKSLASH`; this also catches `\\server\share`); after one leading `./` and
  a directory's single trailing `/` are dropped, no empty, `.` or `..` segment (`E_LISTING_DOTDOT`); the archive root
  `.` / `./` directory is dropped; type f, d or l only (`E_LISTING_TYPE`).
- **FR-018** Per symlink (`listing.js:75-95`): a one-segment link path is `E_LISTING_ROOT_SYMLINK`; the target obeys the
  character rules; its segments are zero or more leading `..` then at least one segment none of which is empty, `.` or
  `..` (`E_LISTING_SYMLINK_FORM`); resolved lexically from the link's parent it must land strictly inside the link's
  own top-level entry (`E_LISTING_SYMLINK_ESCAPE`).
- **FR-019** Across entries (`listing.js:121-151`): no entry under a symlink path (`E_LISTING_THROUGH_SYMLINK`, checked
  before implied ancestors); no path twice and no path that is a file in one entry and an implied directory of another
  (`E_LISTING_DUPLICATE`); no link whose resolved target, or a proper prefix of it, is a symlink path, itself included
  (`E_LISTING_SYMLINK_CHAIN`). The first failure in this order wins, so an archive that `listing.js` refuses with code
  X is refused by Phase 8 with code X.

### D. The canonical listing (roadmap lines 643-645; `CONTRACT.md:270-286`)

- **FR-020** `extract::CanonicalListing` (lines without terminators) is built from the accepted entries plus their
  implied ancestor directories (`d 0755`), modes normalised (file `0755` iff `mode & 0o111 != 0`, else `0644`; dir
  `0755`; link `0777`), sorted by the UTF-8 bytes of the path, each line `<t> <mmmm> <path>` or `l 0777 <path> ->
  <target>`. The name `Listing` is not reused (`detect.rs:222` has one).
- **FR-021** The same builder serves two sources: the archive's entries (pass 1) and the tree on disk (FR-043). A pure
  `CanonicalListing::from_entries(&[ListingEntry]) -> Result<CanonicalListing, ExtractError>` is public for tests and
  for Phase 9.
- **FR-022** `CanonicalListing::text()` is every line plus LF (UTF-8); `files()` is the line count; `digest()` is the
  SHA-256 of `text()` (computed through `verify::Sha256Stream`, FR-082).
- **FR-023** For every committed good fixture, the Rust listing text equals the committed `.listing` byte for byte and
  its digest equals the `.sha256` file; for every committed bad fixture, `plan_archive` (or, for the two `layout-*`
  archives, `check_staged_layout`) fails with the code in its `.expected-error` (SC-002, SC-003).

### E. The staged-layout check, before any write (roadmap lines 643-645)

- **FR-030** After pass 1: `listing.files() == payload.files`, else `E_LISTING_COUNT { expected, found }`.
- **FR-031** `listing.digest() == payload.sha256`, else `E_LISTING_DIGEST { expected, found }` (lower-case hex).
- **FR-032** Both are checked before the layout and before any filesystem write; a listing that matches the payload is
  exactly the listing Phase 2 computed from this archive (no extra, no missing entry, no wrong mode or link).
- **FR-033** The C4 Linux layout for `install.vst3_bundle` (`listing.js:359-382`): the root holds exactly
  `<vst3_bundle>` (d), `presets` (d), `README.txt` (f), `CHANGELOG.txt` (f); the bundle holds
  `Contents/Resources/moduleinfo.json` (f) and `Contents/x86_64-linux/<stem>.so` (f), `<stem>` = `vst3_bundle` minus
  `.vst3`; a missing item, a wrong type or any other root item → `E_LAYOUT { item }`.
- **FR-034** No `release.json` is fetched or read; the payload and the bundle name come from the feed entry only.

### F. Writing the tree (roadmap lines 638-642; D2, D3, D5)

- **FR-040** Pass 2 starts by pruning `extract/` (D2, FR-058: the same routine as the public `extract::prune`, run
  under the registry lock; every entry not in the live registry is removed by kind through `ExtractIo`, failures
  logged and skipped), then creates `extract/` if absent and the work directory
  `extract/<hex>.<random>` (`ExtractIo::create_work_dir`, D2). The work directory is registered live (FR-049) the
  moment `create_work_dir` returns, before the next step; the prune, `create_work_dir` and the registration run in one
  critical section of the registry lock (plan PD-2) (plan SI-6). `create_work_dir` requires `extract/` to be a
  directory after creating it (`symlink_metadata`, never following a link; a link or any other kind at `extract/` is
  `E_EXTRACT_IO { step: "create_work_dir", kind: InvalidInput }`, nothing created under its target, plan PD-6) (plan
  SI-9). The work directory must be a directory that
  `ExtractIo::kind_of` (`symlink_metadata`, never following a link) reports as such, and its canonical path
  (`ExtractIo::canonicalize`) must lie strictly under the canonical `staging_dir()` (`E_EXTRACT_ESCAPE` otherwise, with
  the work directory removed and no entry written; SC-020).
- **FR-041** Every write target is built by one join, `entry_path(work, rel) -> Result<PathBuf, ExtractError>`: `rel`
  is split on `/`, every segment must be one `Component::Normal` with no `\`, `:` or NUL, and the joined path must be a
  strict descendant of `work` (`E_EXTRACT_PATH` otherwise). No other code joins an archive string to a path. On
  `cfg(windows)` only, a segment whose name matches `contract::reserved_stem_re()` (CON, PRN, AUX, NUL, COM0-9,
  LPT0-9, with or without an extension; `contract.rs:138`, `:1024`) or that ends in `.` or a space is also `E_EXTRACT_PATH`: Win32 maps such a
  name to a device or strips the trailing character, so the created path would differ from the listed one. These
  names stay legal on Linux and in C4 (no parity change); because FR-059's joined-length check runs `entry_path` over
  every listing line, such an archive is refused on Windows before any write (plan §4.3.8) (plan SI-12).
- **FR-042** Order of writes: every planned directory, in listing order (parents before children), with `create_dir`
  (never `create_dir_all`; an existing path is `E_EXTRACT_COLLISION`); then every file in archive order, opened
  `write + create_new` (an existing name, including one that differs only in letter case on a case-insensitive volume,
  is `E_EXTRACT_COLLISION`), its data copied exactly `size` bytes, each file's bytes hashed as they are written (FR-056); then every symlink with
  `std::os::unix::fs::symlink(target, link)`. `E_EXTRACT_UNSUPPORTED` is produced by `stage`, not by an `ExtractIo`
  error: when the accepted plan holds a symlink and `env.io.supports_symlinks()` is false (`FsExtractIo` on non-Unix,
  D4), `stage` refuses after pass 1 and before the prune, the work directory or any write (SC-023). An `io::Error` of
  kind `Unsupported` returned by a `symlink` call is an I/O failure like any other (`E_EXTRACT_IO`, FR-045). Because
  symlinks come last and FR-019 forbids entries under a link, no write ever passes through a link.
- **FR-043** Modes (Unix): each file gets exactly its normalised mode (`0755` or `0644`) and each directory `0755` via
  `set_permissions(PermissionsExt::from_mode)`, so the umask cannot change the result; no setuid, setgid or sticky bit
  is ever set. No owner, group, timestamp or extended attribute is set or copied; no `com.apple.quarantine` is ever
  written (FR-013, FR-016).
- **FR-044** Pass 2 re-parses the headers under FR-010 to FR-019 and requires the same plan (path, type, normalised
  mode, size, target, in the same order); both passes' SHA-256 of the raw archive (read to EOF) must equal
  `artifact.sha256()` (D6). Any difference is `E_EXTRACT_CHANGED`. Precedence within pass 1: a gzip, tar, rule or
  limit error found while reading is reported as is (its code wins over the hash); the hash is compared once the raw
  file is read to EOF, before FR-030 to FR-033 and before any write. Within pass 2: a parse error, a plan difference
  and a hash mismatch are all `E_EXTRACT_CHANGED` (pass 1 already accepted the verified bytes).
- **FR-045** `stage` maps every `io::Error` an `ExtractIo` method returns (D5): `E_EXTRACT_FULL` (`StorageFull`,
  `QuotaExceeded`), `E_EXTRACT_DENIED` (`PermissionDenied`, `ReadOnlyFilesystem`), `E_EXTRACT_COLLISION`
  (`AlreadyExists` from `create_dir` or `create_file`, FR-042), or `E_EXTRACT_IO { step, kind }` for every other kind,
  each naming the step (`open_archive`, `read_archive`, `create_work_dir`, `create_dir`, `create_file`, `write`,
  `set_mode`, `symlink`, `kind_of`, `read_tree`, `canonicalize`, `read_file`, `remove`) and the archive path. Step
  `read_archive` is an `io::Error` returned by the archive handle's `read` or `rewind` in either pass (a tap under the
  gzip decoder records the first source error); an `UnexpectedEof` raised by the gzip decoder itself is
  `E_LISTING_CORRUPT`, not a read error (plan PD-4) (plan SI-7). On every failure after the work directory
  exists, the work directory is removed through `ExtractIo::remove_tree` (best effort, a removal failure logged), so
  no partial tree survives a returned error unless the removal itself failed; such a leftover, like a crash's, is a
  non-live `extract/` entry that the next `stage` prunes (D2, SC-010).

### G. The physical post-check (roadmap lines 642-645; `CONTRACT.md:344-346`)

- **FR-046** After pass 2, for every listing path: `canonicalize` of a file or directory, and of a symlink's parent,
  must lie inside the canonicalised work directory; a symlink whose `canonicalize` succeeds must resolve strictly inside
  the canonicalised `work/<top-level entry>`; a dangling in-bundle link (C4 permits it) is checked through its parent
  only. Both sides are canonicalised through `ExtractIo::canonicalize` (the work directory once, every listed path, or
  its parent, once), so a staging dir reached through a symlinked parent (macOS `/var` → `/private/var`) is handled
  (SC-012). A failure is `E_EXTRACT_ESCAPE { path }`.
- **FR-047** The tree is re-read through `ExtractIo::read_tree` (directory walk, `symlink_metadata`, `read_link`, never
  following a link, names as bytes) into `ListingEntry`s, each with `mode: Option<u32>`, and passed through FR-020's
  builder; the result must equal the pass-1 listing line for line (`E_EXTRACT_TREE_MISMATCH { line }`, naming the first
  differing line of either side, in listing order). A `None` mode (only `FsExtractIo` on non-Unix, D4) takes the plan's
  mode for that path; `MemExtractIo` always reports `Some`. On a mismatch the work directory is removed and no
  `StagedTree` is returned (SC-019).
- **FR-048** Only after FR-046, FR-047 and the version check of FR-057 pass is the work directory kept and a
  `StagedTree` returned.

### H. The result (roadmap line 668; D7)

- **FR-050** `extract::StagedTree` has private fields and no public constructor (no `Default`, no `From`, no public
  literal; compile-fail doctests as for `VerifiedArtifact`, `verify.rs:92-105`). Accessors: `root()` (the work
  directory), `bundle()` (`root/<vst3_bundle>`), `presets()` (`root/presets`), `name()`, `version()`, `vst3_bundle()`,
  `listing() -> &CanonicalListing`, `archive_sha256()`, `file_hashes()` (FR-056). It is not `Clone`.
- **FR-051** `extract::discard(tree, io)` removes the tree's root (re-derived and checked to be a direct child of
  `extract/`) and drops it from the live registry; dropping a `StagedTree` without `discard` removes it from the
  registry only, so the next `stage` or `prune` removes it (D2).
- **FR-052** One `info!` line per `stage` outcome (target `krate_manager_core::extract`): plugin, version, asset name,
  listing lines, and the error code on failure; no absolute path (SC-021).

### H2. Concurrency, hand-off contract, file hashes, version check, prune, path bounds (clarifications of 2026-11-01)

- **FR-049** (Q1) Live registration and concurrent `stage`. The registry of live roots (D2) is one process-wide
  structure behind one lock. `create_work_dir`'s return value is registered live **before** any other step touches it,
  and the prune, `create_work_dir` and the registration run in one critical section of the registry lock (one lock
  acquisition: prune `extract/`, `create_work_dir`, insert the returned path, release; plan PD-2) (plan SI-6), so no
  prune can list `extract/` between the creation and the registration; the registration is held by a drop guard
  (model `download.rs:652-671`): on success the guard hands the root to the `StagedTree`
  (still live), on every error return and on unwinding it deregisters the root. Every prune (FR-040, FR-058) runs
  while holding that lock, so it can neither miss a root created before it took the lock nor remove one created after
  (a root created while the prune waits is registered before the prune lists `extract/`, or after it finished).
  Concurrent `stage` calls, for the same artifact or different ones, are safe and independent: each owns its own work
  dir (FR-040, D2), no call serialises behind another beyond the registry lock's short critical sections (never held
  across pass 1, pass 2 reads or writes; the one `create_work_dir` call inside it is a single directory creation, plan
  PD-2), and no call ever removes a work dir or a `StagedTree` root that another call
  holds live. There is no process-wide `stage` mutex.
- **FR-054** (Q2) `StagedTree` is a read-only source. Its public API offers accessors (FR-050) and nothing that moves
  or consumes a part: no method taking `self` by value (the only function that consumes the tree is the
  free function `discard`, FR-051), no `into_bundle` / `into_presets` / `into_root` / `rename` / `persist` /
  `release` / `into_inner`. Phase 9 copies out of `root()` / `bundle()` / `presets()`; `discard` removes the whole root
  and need not tolerate a missing child. Nothing in Phase 8 renames a part out of the tree.
- **FR-055** (Q3) The staged tree is disposable and Phase 8 makes no durability promise: `ExtractIo` has no
  `sync_file` / `sync_dir` method and `extract.rs` calls no `sync_all`, `sync_data` or `fsync`-class function. A crash
  or power loss leaves a non-live `extract/` entry that the next prune removes (FR-058). Phase 9 syncs what it places.
- **FR-056** (Q4) Per-file hashes. In pass 2, the bytes of every file are hashed with `verify::Sha256Stream` (FR-082)
  as they are written, in the same loop that writes them (`size` bytes, FR-042), and `StagedTree::file_hashes() ->
  &[(String, [u8; 32])]` returns one `(relative path, SHA-256)` pair per planned file, in listing order, the path
  being the canonical listing path (relative to `root()`). Symlinks and directories have no hash. A file's hash is the
  SHA-256 of exactly the bytes handed to `ExtractIo::write` for it. Nothing re-reads a staged file to produce or check
  a hash: no third read of the staged files (the single bounded read of `moduleinfo.json`, FR-057, is not a hash read
  and is the only staged-file content read in `stage`).
- **FR-057** (Q5) Staged version check. After FR-046 and FR-047 and before FR-048, `stage` reads the staged
  `<vst3_bundle>/Contents/Resources/moduleinfo.json` (path built by `entry_path`, FR-041) through
  `ExtractIo::read_file(path, limits.max_moduleinfo_bytes)` and parses it with Phase 6's reader
  `detect.rs` `moduleinfo_version`, whose visibility is raised from private to `pub(crate)` and whose behaviour is not
  changed. The reader is passed the relative path `Contents/Resources/moduleinfo.json`, never an absolute path, so
  the warning it logs on a parse failure stays free of absolute paths (FR-052). The result must be
  `CopyVersion::Known` with a version equal to `entry.version_text` (the same equality Phase 6 uses to compare a
  detected copy with a feed version); any other result (a different version, `Unknown`, a file over
  `max_moduleinfo_bytes` as `E_EXTRACT_LIMIT { which: "moduleinfo" }`) is `E_EXTRACT_VERSION { expected, found }`
  (`found` the reduced class `Version`, or `unknown`; a size overrun is the `E_EXTRACT_LIMIT` above). On failure the
  work dir is removed through `ExtractIo::remove_tree` and no `StagedTree` is returned. No other part of
  `moduleinfo.json` is read or kept. The size check uses the `moduleinfo.json` size already in the plan. The staged
  version check decodes before it parses (plan PD-5) (plan SI-8): a plan size above `max_moduleinfo_bytes` is
  `E_EXTRACT_LIMIT { which: "moduleinfo" }` before any read, and a read that returns more than the bound (the file
  grew after pass 2) is the same code; `stage` then requires the bytes to be UTF-8 (`E_EXTRACT_VERSION { found:
  "unknown" }` otherwise), strips one leading U+FEFF (byte-order mark), and passes the text with the relative path
  `Contents/Resources/moduleinfo.json` to `detect::moduleinfo_version(text: &str, path: &Path)` (`detect.rs:692`;
  its caller `vst3_version` does the same bounded read, UTF-8 check and BOM strip, `detect.rs:565-581`); the result
  must be `CopyVersion::Known { version, .. }` with `Some(version) == entry.version`, the `CoreVersion` equality
  Phase 6 uses through `version::compare` (`status.rs:364`, `version.rs:84-94`), not a text equality; in the error,
  `expected` is `entry.version_text` and `found` is `version.to_string()` or `unknown`.
- **FR-058** (Q6) Leftover removal. `extract::prune(env: ExtractEnv<'_>) -> usize` is public and is the routine
  FR-040 runs: under the registry lock (FR-049) it lists `extract/` through `ExtractIo` and removes every entry that is
  not live, **by kind** read without following links (`ExtractIo::kind_of`): a directory with its whole tree
  (`remove_tree`), a regular file with `remove_file`, a symlink as the link itself and never its target (a link to a
  directory is removed as a link, nothing under its target is read or removed); any other kind is removed as a file,
  or logged and skipped when removal fails. On Windows a link that `remove_file` refuses (a directory symlink or a
  junction) is removed with `remove_dir_all`, which removes the link itself and does not follow it (std 1.99.1,
  https://doc.rust-lang.org/std/fs/fn.remove_dir_all.html; the fallback sits inside `impl ExtractIo for FsExtractIo`,
  plan PD-7) (plan SI-10). `prune` lists `extract/` only when `kind_of(extract)` is a directory; any other kind is
  logged and nothing is removed (`kind_of(extract)` `None` returns 0; a link, a file or any other kind logs
  `E_EXTRACT_ESCAPE` once and returns 0 without listing or removing anything, because `read_dir` follows a link given
  as its argument; plan PD-6) (plan SI-9). A per-entry failure is logged (no absolute path) and skipped, never
  aborts the rest, and never fails the calling `stage`. A missing `extract/` is not an error (nothing is created by
  `prune`). The return value is the number of entries removed. Phase 9's start-up recovery pass calls `prune`; `prune`
  never reads or removes anything outside `extract/`, and it is not called by a `stage` that fails before pass 2
  (so a refused archive still writes nothing, FR-087, SC-006).
- **FR-059** (Q7) Joined-path bound. After FR-030 to FR-033 and the `supports_symlinks` refusal (FR-042) and before the
  prune, the work dir or any write, `stage` computes the byte length of the longest planned listing path joined to the
  work dir as it will be created, `<staging>/extract/<64 hex>.<random>/<path>` (the random part is `tempfile`'s
  default 6 characters), and refuses with `E_EXTRACT_LIMIT { which: "joined" }` when it exceeds
  `max_joined_bytes` (4095, Linux `PATH_MAX` less the terminator). Together with FR-015's segment bound no
  `ENAMETOOLONG` can occur mid-write, and no partial tree ever starts. The check runs on every OS with the byte
  length of the OS string form of the joined path.

### I. Dependencies, guards, fixtures and lanes (Cross-Cutting lines 1008-1014)

- **FR-080** `flate2` (workspace pin 1.1.10, default features) moves from core's `[dev-dependencies]` to
  `[dependencies]`; core's dev-dependencies become exactly `{minisign}`. `injection.rs` `FR007_DEPENDENCIES` gains
  `flate2` (14 names), `core_dependencies_are_the_fr007_set` expects `{minisign}`, and `CONFINED_TOML_KEYS` gains
  `("flate2", ["Cargo.toml", "crates/core/Cargo.toml"])`. The clippy-guard lock gains `"flate2",` in
  `krate-manager-core`'s dependency list (a path dependency's dev-dependencies are not in a dependent's lock, so moving
  `flate2` to `[dependencies]` changes it, and `manager-ci.yml`'s clippy-guard step runs with `--locked`), re-locked
  from `apps/krate-manager/` with `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml
  --format-version 1 > /dev/null` (the workflow's ruled re-lock command, commit `2f3c93d7`, the Phase 7 TA-5
  precedent) (tasks TA-1), and is committed with the phase: exactly one added line, no `[[package]]` added. No new
  `[[package]]` enters either lock (`flate2` 1.1.10 is already locked in both, `clippy-guard/Cargo.lock:1028-1036`)
  (plan SI-3).
- **FR-081** The `core dependency tree` step (`manager-ci.yml:122-136`) still lists no excluded crate.
- **FR-082** Confinement (an `injection.rs` rule beside `verify_confinement_findings`): `flate2` appears in non-test code
  only in `extract.rs`; `sha2` stays only in `verify.rs` (`extract.rs` uses `verify::Sha256Stream`); `tar` is not a
  dependency anywhere in the workspace: `tar` is not a key in any workspace manifest and no `[[package]]` named `tar` is
  in either lock (`apps/krate-manager/Cargo.lock`, `tests/fixtures/clippy-guard/Cargo.lock`), so a transitive `tar` is
  caught too (plan SI-12).
- **FR-083** In `extract.rs`, the mutating filesystem tokens (`create_dir`, `OpenOptions`, `create_new`, `symlink`,
  `set_permissions`, `remove_dir_all`, `remove_file`, `rename`, `File :: create`, `fs :: write`, `fs :: copy`) appear
  only inside `impl ExtractIo for FsExtractIo`, except that the two `ExtractIo` method names `create_dir` and `symlink`
  may also appear as a method declaration (`fn create_dir` / `fn symlink` inside `trait ExtractIo` or any `impl
  ExtractIo for …`) and as a method call (`. create_dir (` / `. symlink (`); std has no method of either name, so a
  method call is always the trait's (plan SI-2). The same rule covers, as whole-identifier token sequences, `File ::
  options`, `DirBuilder`, `remove_dir`, `soft_link`, `symlink_file`, `symlink_dir` and `set_len` (every std entry point
  that creates, truncates or removes a file or directory or makes a link), which may appear in `extract.rs` only inside
  `impl ExtractIo for FsExtractIo` (plan SI-12). `hard_link`, `Command`, `TcpStream`, `xattr`, `thread :: spawn`,
  `sync_all`, `sync_data`, `sync_file`, `sync_dir` appear nowhere in it (FR-055). `create_dir_all` appears only in `fn
  create_work_dir` of `impl ExtractIo for FsExtractIo` (plan SI-2). `tempfile` appears in `extract.rs` only inside that
  same `fn create_work_dir`, where the only `tempfile` method token is `tempdir_in`, so `tempfile :: tempdir`, `TempDir
  :: new`, `Builder :: … . tempdir (`, `tempfile (` and `NamedTempFile`, which create under the real
  `env::temp_dir()`, appear nowhere in the file and a file-scope `use tempfile` is reported (plan §4.6) (plan SI-12).
- **FR-084** Tests obtain a `VerifiedArtifact` only through `verify::verify_file` (crate-private, reachable from core's
  own test modules), called from `extract_tests.rs` (FR-088): the KAT archive with `test_support::kat_signing_key()`
  (`test_support.rs:284-289`), every other fixture archive signed in memory by `TestSigner` with its C3 comment and
  checked with `TestSigner::key()` (Phase 7 FR-083). `extract_tests.rs` therefore never calls `SigningKey::from_text`,
  and the `from_text` allowlist (`injection.rs:2584-2594`) is unchanged. No key is written to disk.
- **FR-085** Committed fixtures (`tools/feed-contract/fixtures/listings/**`, `fixtures/kat/**`) are read in place and
  never modified. Archives the corpus needs beyond them (UNC paths, the Windows drive path `C:\evil`, the oversize
  cases, a 4097-byte path in a GNU `L` record, a pax `SCHILY.xattr.com.apple.quarantine` record, a case-folding pair,
  an NFC name, setuid / setgid / sticky header modes with an old mtime, trailing gzip garbage, trailing zero padding
  after the gzip member, a 256-byte segment, a path whose joined length exceeds 4095 bytes, an archive whose
  `moduleinfo.json` carries another version) are built at test run
  time into a temp dir by a `#[cfg(test)]` ustar builder in `extract_tests.rs` (hand-made 512-byte headers with
  checksums, `flate2::write::GzEncoder`), and none is committed. The same builder, or an in-test byte edit of a
  committed fixture's bytes copied into the temp dir, makes the further runtime archives of plan §7.1 (plan SI-11):
  the three two-defect archives of SC-006 (FR-015's deferral, plan PD-1: `../x` then a header with a bad checksum; an
  escaping link then `/etc/evil`; `/etc/evil` then the base tree with `payload.files` 9); the header flood (20 pax `g`
  records then the base tree, with an injected `max_headers` 16); the `moduleinfo.json` that is not UTF-8 and the one
  with a leading U+FEFF and version `1.2.0` (SC-028 (g), (h), FR-057); the three streaming rows of SC-003 (a name with
  the bytes `c3 28`, a name `Alpha.vst3/a -> b`, a typeflag `6` header `Alpha.vst3/fifo`); the dangling in-bundle link
  `Alpha.vst3/Contents/x86_64-linux/missing.so -> nothere.so` (SC-012, SC-020); the archive holding
  `presets/aux.vstpreset` (SC-020); a zero-length preset (SC-027); the pass-2 copies of `real-shaped` served after the
  rewind (SC-009 (e1), (e2), (f)); and the gzip-layer variants of `real-shaped.tar.gz` (SC-031 (g1) to (g6)). A runtime archive that must stage carries a payload
  whose `files` and `sha256` come from a listing text written **literally** in the test (hashed with
  `verify::Sha256Stream`), never from a listing the code under test computed. Every runtime archive that must stage carries, at
  `<vst3_bundle>/Contents/Resources/moduleinfo.json`, a document of the shape of the committed fixtures
  (`{"Name": …, "Version": "<v>", "Classes": [{"Name": …, "Version": "<v>"}]}`) whose `<v>` equals the entry's
  `version_text` (FR-057). The committed Alpha fixtures carry `1.2.0` and the KAT archive `1.2.3` (measured this
  session with `tar -xzO`), so every test that stages an Alpha fixture uses an entry with `version_text` `1.2.0`.
- **FR-086** `apps/krate-manager/tests/required-tests.json` lanes are disjoint: every SC test name without an OS suffix
  goes under `cargo.all` only; the `_unix` names under `cargo.macos` and `cargo.linux` only; the `_macos` names under
  `cargo.macos` only; the `_windows` name under `cargo.windows` only (the `required tests ran` step requires each name
  of `cargo.all` plus the leg's lane exactly once, `manager-ci.yml:163-169`, so a `cfg(unix)` name in `cargo.all`
  would turn the Windows leg red). The names, per lane:
  - `cargo.all` (24): `extract::tests::concurrent_stage_is_safe`, `extract::tests::file_hashes_match_written_bytes`,
    `extract::tests::moduleinfo_version_must_match`, `extract::tests::prune_removes_by_kind`,
    `extract::tests::path_length_bounds`, `extract::tests::trailing_zero_padding_is_accepted`,
    `extract::tests::good_fixtures_match_committed_listings`,
    `extract::tests::bad_fixtures_fail_with_expected_code`, `extract::tests::kat_archive_stages`,
    `extract::tests::real_shaped_round_trip`, `extract::tests::dot_slash_round_trip`,
    `extract::tests::exec_and_symlink_round_trip_logic`, `extract::tests::crafted_archives_are_refused`,
    `extract::tests::payload_and_layout_mismatches`, `extract::tests::entry_binding_cases`,
    `extract::tests::changed_archive_is_refused`, `extract::tests::io_failures_leave_no_tree`,
    `extract::tests::pax_xattr_records_are_ignored`, `extract::tests::case_fold_collision`,
    `extract::tests::discard_and_prune`, `extract::tests::tree_mismatch_is_refused`,
    `extract::tests::entry_path_rejects_every_bad_form`, `extract::tests::work_dir_must_be_inside_staging`,
    `extract::tests::stage_logs_one_line_per_outcome`.
  - `cargo.macos` and `cargo.linux` (5 each): `extract::tests::prune_removes_real_link_unix`,
    `extract::tests::posix_modes_and_links_unix`,
    `extract::tests::special_bits_and_mtime_not_applied_unix`, `extract::tests::escape_cases_unix`,
    `extract::tests::symlinked_staging_root_unix`.
  - `cargo.macos` only (2): `extract::tests::staged_tree_has_no_xattrs_macos`,
    `extract::tests::raw_temp_dir_staging_root_macos`.
  - `cargo.windows` only (1): `extract::tests::symlink_entry_unsupported_windows`.

  `manager-ci-shape.test.js` `EXPECTED_REQUIRED` gains exactly the same names in the same lanes. The SC-016 doctests and
  the SC-017 `injection.rs` cases run inside existing required names (`injection_scanner_reports_planted_violations`,
  `verified_artifact_scanner_reports_plants`, …) and add no name.
- **FR-087** No Phase 8 test runs a process other than `/usr/bin/xattr` (SC-013, macOS) and `cmd` (`cmd /C mklink /J`
  on paths inside its own TempDir, the junction block of SC-023, Windows), opens a network socket, or
  touches a path outside its own temp dir (a `fresh_temp_roots()` TempDir, or one of FR-089's constructors on a fresh
  TempDir); tests run in parallel with cargo's default threads. `injection.rs` `TEST_PROGRAM_BODIES` (the TA-1 program rule,
  `injection.rs:393-409` at `2499fb13`) gains `(EXTRACT_TESTS_RS, "staged_tree_has_no_xattrs_macos", "/usr/bin/xattr")` and
  `(EXTRACT_TESTS_RS, "symlink_entry_unsupported_windows", "cmd")`, array length 4 → 6, so
  `production_config_only_in_allowed_tests` stays green; `cmd` is allowed in `symlink_entry_unsupported_windows` only
  (the Phase 7 precedent: the `cmd` row of `staged_links_are_not_followed_windows`) (plan SI-1). **Fail-closed rule:** every test row whose expected
  outcome is a refusal stages over a fail-closed recording `ExtractIo` (test support): its mutating methods
  (`create_work_dir`, `create_dir`, `create_file`, `write`, `set_mode`, `symlink`, `remove_tree`, `remove_entry`) record the call and
  return `io::Error::other("fail-closed")` without reaching `std::fs`; only `open_archive` and the read-only methods
  forward to `FsExtractIo`. A regression that let a refused archive through therefore reaches no real path (an absolute
  `/etc/evil`, `C:\evil` or `\\server\share\evil` join can create nothing and open no SMB connection) and fails the
  row's code assertion instead. A row may run over a forwarding `FsExtractIo` wrapper only when every path the archive
  names resolves inside the row's own TempDir (the `../` and symlink-escape rows, SC-006, SC-012). The test doubles'
  powers are those of plan §4.5 (plan SI-12): one call record `XOp` shared by every wrapper and by `MemExtractIo`, in
  which every call carries its path and `Write` carries the exact bytes passed to `ExtractFile::write_all`
  (`is_mutating` true for exactly the eight kinds above); `RecordingExtractIo::forwarding(inner)` and `fail_closed()`
  with optional `with_open_archive` (replacing the archive handle, e.g. by a `TwoPhaseArchive` that serves other bytes
  after the rewind), `with_read_tree`, `with_canonicalize` and `with_kind_of` overrides; `FaultyExtractIo` with a list
  of planned failures (the n-th call of a kind fails with a given `io::ErrorKind`), a `remove_tree` that is forwarded,
  a no-op returning `Ok`, or a failure (`RemoveTreeMode::{Forward, Noop, Fail}`), and an archive read fault after a
  given number of bytes of pass 1 or pass 2 (`ReadFault`).
- **FR-088** `injection.rs` gains `const EXTRACT_TESTS_RS: &str = "crates/core/src/extract_tests.rs"`, and the
  `verify_file` callers of `minting_route_findings` (`injection.rs:2579-2583`) become `[VERIFY_RS, DOWNLOAD_RS,
  VERIFY_TESTS_RS, DOWNLOAD_TESTS_RS, EXTRACT_TESTS_RS]`. Nothing else in `verified_artifact_findings` changes: the
  `from_text` callers stay as they are (FR-084), and the `struct Expected` check covers declaration and field privacy
  only (`injection.rs:2595-2621`), so building an `Expected` in `extract_tests.rs` needs no allowlist. `extract.rs`
  (non-test) stays off both lists (SC-017).
- **FR-089** `test_support.rs` gains two `Roots` constructors whose roots are deliberately **not** canonical, each on a
  caller-owned fresh `TempDir`, each running `check_under_temp` (`test_support.rs:60-78`) on the canonical form first
  and building through `Roots::new` (`roots.rs:80-112`, which accepts any absolute, normal paths) and
  `Roots::sandboxed` with a sandbox base spelled exactly like the roots, so `check_sandboxed`'s lexical comparison
  (`roots.rs:175-185`) holds:
  - `temp_roots_via_link(td: &Path) -> Roots` (`#[cfg(unix)]`): creates `<td>/real` and the symlink `<td>/via -> real`
    and spells every root as `<canonical td>/via/{plugins,presets,state}/…`, sandbox base `<canonical td>/via`;
  - `temp_roots_raw(td: &Path) -> Roots` (`#[cfg(target_os = "macos")]`): spells every root on `td` exactly as
    `TempDir::new()` returned it, under the raw `std::env::temp_dir()` (`/var/folders/…`), sandbox base `td`.
  Both create the root directories as `temp_roots_in` does; neither touches a path outside `td`.

### J. Scope fences

- **FR-090** No `src-tauri` change: no command, capability or permission; no webview change.
- **FR-091** No change to `tools/feed-contract/` except `tests/manager-ci-shape.test.js` (FR-086); none to `plugins/`,
  `dsp/`, `cmake/`, `extern/`, `release.yml`, `feed.yml`, `ci.yml`, `manager-ci.yml`.

---

## Success Criteria

Rust SCs run locally on Windows with `cargo test --workspace --locked` from `apps/krate-manager/` (default test
threads); a run counts only with every binary reporting `0 failed`. macOS and Linux rows are **PENDING-CI** on
`manager-ci.yml` jobs `check (macos-latest)` and `check (ubuntu-24.04)` and close on a leg only when the run's
committed `tests/required-tests.json` lists the row's test names and the leg's `required tests ran` step passed with no
`::error` line (`manager-ci.yml:153-178`). The PENDING-CI evidence of every row is the job's step `cargo test`
(`cargo test --workspace --locked`, `manager-ci.yml:117-120`, the whole workspace, no `-p` and no filter), counted by
its step `required tests ran`; no row names a command no CI job runs. Every "named error" is asserted by `code()`,
never by message text. Every test stages under a fresh `TempDir`: through `fresh_temp_roots()`, except the
symlinked-parent cases of SC-012, which build their deliberately non-canonical roots on a fresh `TempDir` with FR-089's
`temp_roots_via_link` / `temp_roots_raw` (`fresh_temp_roots()` yields canonical roots only, `test_support.rs:83-116`,
so it cannot exercise FR-046's two-sided canonicalisation). Every row expected to be refused stages over the
fail-closed recording `ExtractIo` of FR-087 unless the row says otherwise.

- **SC-001 (local gate, zero warnings).** `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --locked --
  -D warnings`; `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings`; `cargo test
  --workspace --locked`; `npm run typecheck`, `npm run lint`, `npm test`; `node ../../tools/lint-manager-version.js`;
  and from the repo root `node --test tools/feed-contract/tests/manager-ci-shape.test.js`. Threshold: every command
  exit 0, 0 `warning:` lines, no new `#[allow` / `#[expect` without a one-line reason, every Phase 4-7 test still
  passing. PENDING-CI: the same steps green on `check (windows-latest)`, `check (macos-latest)`, `check (ubuntu-24.04)`.
- **SC-002 (Node / Rust parity on the good corpus).** `extract::tests::good_fixtures_match_committed_listings`: for each
  of the 9 archives in `listings/good/` and the KAT archive, `plan_archive` (FR-009, no `ExtractIo`,
  `ExtractLimits::PRODUCTION`) called with `max_entries` = the committed `.listing` line count yields a listing whose
  `text()` equals the committed `.listing` bytes and whose `digest()` equals the `.sha256` file's first 64 characters;
  then `check_staged_layout` with the true payload (`files` = that line count, `sha256` = that digest) and
  `vst3_bundle` `Alpha.vst3` (the 9 good archives) or `KatPlugin.vst3` (KAT) returns `Ok`. `dot-slash` (11 entries,
  10 lines, `max_entries` 10) is one of the rows, so the root `./` entry is proven not to count (D8). Threshold:
  10 / 10. Local; PENDING-CI (`all`).
- **SC-003 (Node / Rust parity on the bad corpus).** `extract::tests::bad_fixtures_fail_with_expected_code`: each of the
  22 archives in `listings/bad/` fails with exactly its `.expected-error` code. Inputs: the 20 malformed archives go
  through `plan_archive` alone with `ExtractLimits::PRODUCTION` and `max_entries` 64 (above every fixture's entry
  count, at most 18, measured this session), so the rule and not the `entries` bound refuses; the two `layout-*`
  archives pass `plan_archive` (`max_entries` 64), the test asserts their listing line counts are the fixtures
  README's 11 and 8, and `check_staged_layout` with a payload of that listing's own count and digest and
  `vst3_bundle` `Alpha.vst3` returns `E_LAYOUT` naming `NOTES.txt` / `presets` (the payload cannot refuse first, so
  the layout rule is what is asserted). The same test also runs (plan SI-12, §7.1 SC-003): **the pure rule table**,
  every one of the 33 reject rows of `c4-listing-tar.test.js:196-231` ported literally to `ListingEntry` values (`f` =
  file mode 0644, `d` = dir 0755, `l` = symlink 0777; byte paths, so the two invalid-UTF-8 rows carry `[0xc3, 0x28]`;
  the FIFO row is `EntryType::Other(b'6')`), each through `CanonicalListing::from_entries` with the same code as the
  Node row (every `E_LISTING_PATH` case included), and the accept rows of `:236-280` (in-bundle links, implied
  ancestors with the `./` root dropped, UTF-8 byte sort), each whose `lines()` deep-equals the Node expectation; and
  **three streaming rows** through `plan_archive` (`max_entries` 64), each the base tree plus one runtime-built header
  (FR-085): a name with the bytes `c3 28` → `E_LISTING_PATH`, a name `Alpha.vst3/a -> b` → `E_LISTING_PATH`, a
  typeflag `6` header `Alpha.vst3/fifo` → `E_LISTING_TYPE` (the per-entry functions are shared by the streaming
  reader and the pure builder, plan PD-11). Threshold: 22 / 22 fixtures, 33 / 33 rejects, every accept row, 3 / 3
  streaming rows. Local; PENDING-CI (`all`).
- **SC-004 (the KAT round trip).** `extract::tests::kat_archive_stages`: the KAT archive verified through `verify_file`
  with `test_support::kat_signing_key()` (`kat.pub`) and an entry `KatPlugin` 1.2.3 `linux-x86_64`, payload `{files: 10, sha256: db287e49…}`,
  `vst3_bundle` `KatPlugin.vst3` (its `moduleinfo.json` class `Version` is `1.2.3`, so FR-057 passes);
  `stage` with `FsExtractIo` returns a `StagedTree` whose `listing().digest()` is
  `db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6`, whose `bundle()` holds
  `Contents/Resources/moduleinfo.json` and `Contents/x86_64-linux/KatPlugin.so` with the archive's bytes, and whose
  root is a direct child of `<staging>/extract/`. A `#[cfg(unix)]` block of the same test asserts the real modes
  (plan SI-12): `symlink_metadata(..).permissions().mode() & 0o7777` is `0o644` for
  `Contents/Resources/moduleinfo.json` and `Contents/x86_64-linux/KatPlugin.so` and `0o755` for `KatPlugin.vst3`,
  `Contents`, `Contents/Resources` and `Contents/x86_64-linux` (the KAT `.listing`'s modes). Threshold: every
  assertion. Local (Windows: paths, types, bytes); PENDING-CI (`all`, real modes on macOS and Linux).
- **SC-005 (a real-shaped tree round-trips; the logic on Windows).** `extract::tests::real_shaped_round_trip` (FsExtractIo,
  every OS) and `extract::tests::exec_and_symlink_round_trip_logic` (`MemExtractIo`, every OS): `real-shaped` and
  `exec-and-symlink`, each signed by `TestSigner` with an entry (`version_text` `1.2.0`, FR-085) whose payload is the committed `.listing` count and
  `.sha256` digest, stage successfully; the staged tree's listing equals the committed `.listing`; in the Mem case the
  recorded tree holds `Alpha.so` with mode `0755` and `libalpha.so` as a link with target `Alpha.so`. And
  `extract::tests::dot_slash_round_trip` (FsExtractIo, every OS): `dot-slash` (11 tar entries, the root `./` among
  them, 10 listing lines) signed by `TestSigner` with its true payload `{files: 10, sha256: <dot-slash.sha256>}` and
  `vst3_bundle` `Alpha.vst3` stages through the full `stage` path (so the D8 `entries` bound of 10 is not crossed by
  the root entry); the staged listing equals the committed `.listing`. Threshold: every assertion. Local; PENDING-CI (`all`).
- **SC-006 (the crafted corpus is refused 100 %, zero bytes outside).** `extract::tests::crafted_archives_are_refused`:
  rows = the 20 malformed `listings/bad/` archives plus the FR-085 runtime archives: `../` in a pax `path`, `/etc/evil`,
  `C:\evil`, `C:/evil`, `\\server\share\evil`, `//server/share/evil`, an escaping symlink, a symlink chain, a duplicate
  entry, an oversized entry count (the 10-entry base tree with `payload.files` 9 → `E_EXTRACT_LIMIT { which:
  "entries" }`, and `max_headers + 1` headers with a small injected limit → `"headers"`), a 64 KiB + 1 GNU `L` record
  (→ `"meta"`), a 4097-byte entry path delivered through a GNU `L` record under the production `max_meta_bytes`
  (→ `"path"`, `max_path_bytes` 4096), a 256-byte path segment (→ `"segment"`; the 255-byte twin stages in SC-030), a
  path of 4000 bytes in segments of at most 255 under the production limits whose joined length with a staging dir
  of at least 100 bytes exceeds 4095 (→ `"joined"`, refused before any write, FR-059), a gzip bomb over an injected
  `max_tar_bytes` of 1 MiB (→ `"tar_bytes"`), trailing non-zero gzip garbage (→ `E_LISTING_CORRUPT`; trailing zero
  bytes are accepted, SC-031). Each row: a fresh temp root and a correctly signed `VerifiedArtifact`. Payload: the
  oversized-entry-count row as stated; every other row an entry with `payload.files` 64 (above every row's counted
  entries, so the `entries` bound never pre-empts the rule, FR-015) and a zero `sha256` (no refused archive has a
  valid listing, and the rules run before FR-030 compares). `stage` runs over the **fail-closed recording**
  `ExtractIo` of FR-087 and returns the row's named code; the recorder holds **zero** mutating calls
  (`create_work_dir`, `create_dir`, `create_file`, `write`, `set_mode`, `symlink`, `remove_tree`, `remove_entry`); a snapshot (relative
  path, type, length) of the whole TempDir taken before `stage` equals the snapshot after it, and
  `<staging>/extract/` holds no entry. Three two-defect runtime rows prove FR-015's first-failure order (plan PD-1,
  R-1) (plan SI-5): `[f ../x, base tree, a header with a bad checksum]` → `E_LISTING_CHECKSUM` (the later archive error
  wins over the recorded rule-1 failure); `[l Alpha.vst3/Contents/x -> ../../../etc, f /etc/evil, base tree]` →
  `E_LISTING_ABSOLUTE` (the rule-1 failure wins over the earlier rule-2 failure); `[f /etc/evil, base tree of 10
  entries]` with `payload.files` 9 → `E_LISTING_ABSOLUTE` (the rule failure wins over the `entries` bound). The
  `"headers"` row is a header flood: 20 pax `g` records then the base tree, with an injected `max_headers` 16 (FR-085,
  plan SI-11). The rows whose would-be targets all resolve inside the row's TempDir (`../` in a
  pax `path`, `dotdot-path`, `symlink-escape-root`, `symlink-escape-bundle`, the escaping-symlink and chain rows) run a
  second time over a forwarding counting wrapper around `FsExtractIo`, with the same zero-write and snapshot
  assertions, so the refusal is also shown with the real filesystem behind it; the absolute, drive and UNC rows never
  run over a forwarding wrapper. Threshold: 100 % of rows refused with the named code, 0 mutating calls, 0 snapshot
  differences. Local; PENDING-CI (`all`).
- **SC-007 (payload and layout mismatches are refused before writing).** `extract::tests::payload_and_layout_mismatches`:
  through `stage`, (a) `real-shaped` (14 counted entries, 14 lines) with `payload.files` 15 → `E_LISTING_COUNT
  { expected: 15, found: 14 }` (the `entries` bound of 15 is never crossed, so FR-030 reports); (a2) `implicit-dirs`
  (5 counted entries, 10 lines) with `payload.files` 9 → `E_LISTING_COUNT { expected: 9, found: 10 }` (5 entries stay
  under the bound; the implied ancestors push the line count over it); (a3) `real-shaped` with `payload.files` 13 →
  `E_EXTRACT_LIMIT { which: "entries" }` on the 14th counted entry, before FR-030 (FR-015's order); (b) `real-shaped`
  with one digest bit flipped → `E_LISTING_DIGEST`; (c) `real-shaped` with `vst3_bundle` `Bravo.vst3` and the matching
  payload → `E_LAYOUT`; (d) the two committed `layout-*` archives with the payload of their own listing (count and
  digest, as in SC-003) → `E_LAYOUT` naming `NOTES.txt` / `presets`. Each over the fail-closed recorder with 0
  mutating calls. Threshold: every row. Local; PENDING-CI (`all`).
- **SC-008 (the entry binding).** `extract::tests::entry_binding_cases`: one row per FR-002 field (a `.exe` artifact; a
  `.pkg` artifact; another name; another version; a v1-only entry; no platform for the artifact's key; another asset
  name, size, SHA-256; no payload; no install) → `E_EXTRACT_ENTRY_MISMATCH` naming that field, with zero `ExtractIo`
  calls of any kind (not even `open_archive`). Threshold: every row. Local; PENDING-CI (`all`).
- **SC-009 (the archive changed after verification).** `extract::tests::changed_archive_is_refused`, on `real-shaped`
  (a single `gzip -n` member: FLG byte 0, MTIME 0, checked this session with `od`): after `verify_file` returns, the
  staged file is replaced by (a) a re-gzipped copy of its tar stream with one byte XOR `0x01` inside `README.txt`'s
  data → `E_EXTRACT_CHANGED` (every header and the gzip CRC are valid, so only D6's hash sees it, at the end of
  pass 1); (b) a re-gzipped copy with the first byte of the first header's `name` field (tar offset 0) XOR `0x01`
  → `E_LISTING_CHECKSUM` (a single changed byte always changes both the unsigned and the signed sum, and a pass-1
  read error wins over the hash, FR-044); (c) the original file with gzip header byte 4 (the first MTIME byte; no
  FHCRC flag, so the decoder accepts it) XOR `0x01` → `E_EXTRACT_CHANGED` (the tar stream is unchanged, so only the
  raw hash differs). Each over the fail-closed recorder, 0 mutating calls. (d) With a fake `open_archive` (wrapping a
  forwarding `FsExtractIo`) that serves the original bytes on the first open-and-read and row (a)'s bytes on the
  rewind, the result is `E_EXTRACT_CHANGED` and the work directory no longer exists. The fake is
  `RecordingExtractIo::forwarding(&FsExtractIo).with_open_archive(..)` returning a `TwoPhaseArchive` whose first bytes
  are the original and whose bytes after the rewind are, in turn (plan SI-12): (d) row (a)'s bytes; **(e1)** a
  re-gzipped copy of the tar stream with one more well-formed file header and data (`presets/Extra.vstpreset`, 5 bytes,
  valid checksum) before the end-of-archive block; **(e2)** a re-gzipped copy in which `README.txt`'s header `size` is
  one larger (checksum recomputed, data re-padded, every other byte unchanged); **(f)** a re-gzipped copy with entry 5's
  header checksum field XOR `0x01`. (d), (e1), (e2) and (f) each return `E_EXTRACT_CHANGED` ((f) is **not**
  `E_LISTING_CHECKSUM`: in pass 2 a parse error is `E_EXTRACT_CHANGED`, FR-044); in (e1) the record holds no
  `CreateFile` / `Write` for `presets/Extra.vstpreset`; in (e2) no `CreateFile` for `README.txt`, and for every path
  the summed `Write` bytes are at most its plan size; in (f) no `CreateFile` for entry 5 or any later entry; in (d) to
  (f) the work dir is gone afterwards and `extract/` is empty. So the per-entry comparison of pass 2, not only the
  end-of-stream hash, is what refuses (e1), (e2) and (f). Threshold: every row, each with exactly that one code. Local;
  PENDING-CI (`all`).
- **SC-010 (disk full, denied, crash).** `extract::tests::io_failures_leave_no_tree`: a fault-injecting `ExtractIo` over
  `FsExtractIo` returns `StorageFull` at the first `write` → `E_EXTRACT_FULL`; `PermissionDenied` at `create_dir` →
  `E_EXTRACT_DENIED`; `Unsupported` at `symlink` (over `MemExtractIo`, so it runs on Windows too) → `E_EXTRACT_IO
  { step: "symlink" }`; in each case the work directory is gone and no `StagedTree` exists. Cleanup that never runs
  (the state a killed process leaves; D5, not a panic): (i) for each write step k (`create_dir`, `create_file`,
  `write`, `set_mode` on Unix / Mem, `symlink` on Mem), a fault-injecting `ExtractIo` returns an error at step k **and**
  makes `remove_tree` a no-op returning `Ok`; `stage` returns the mapped error, the work dir is left with the partial
  content written before step k, and the next `stage` of any artifact over the plain `FsExtractIo` (or `MemExtractIo`)
  prunes it; (ii) the test plants a leftover `extract/<64 hex>.<random>` directory holding a partial
  `Alpha.vst3/Contents/` tree directly, then stages `real-shaped`, which prunes it. After (i) and (ii) `extract/`
  holds only the new tree. (iii) Cleanup fails (plan SI-12): over `FsExtractIo`, the first `write` fails with
  `StorageFull` and `remove_tree` fails with `PermissionDenied`, under `install_capture`: the result is
  `E_EXTRACT_FULL { step: "write" }` (the original error, not the cleanup's), one `Warn` record at target
  `krate_manager_core::extract` names `remove` and `PermissionDenied` with no absolute path (raw or canonical
  TempDir), the work dir is still present, and the next `stage` over plain `FsExtractIo` prunes it. (iv) Archive read
  failures (plan SI-12, FR-045 step `read_archive`): staging `real-shaped` over `FsExtractIo` with a read fault after
  1024 bytes of pass 1 of kind `PermissionDenied` → `E_EXTRACT_DENIED { step: "read_archive" }`, of kind `TimedOut` →
  `E_EXTRACT_IO { step: "read_archive", kind: TimedOut }` (both with 0 mutating calls and no work dir), and after 1024
  bytes of pass 2 of kind `PermissionDenied` → `E_EXTRACT_DENIED { step: "read_archive" }` with the work dir removed
  and `extract/` empty. Threshold: every case. Local; PENDING-CI (`all`).
- **SC-011 (real POSIX filesystem: exec bits and links).** `extract::tests::posix_modes_and_links_unix`: `exec-and-symlink`
  staged with `FsExtractIo` (whatever the runner's umask, FR-043 sets modes explicitly); the test asserts
  `symlink_metadata(Alpha.so).permissions().mode() & 0o7777 == 0o755`, every other
  file `0o644`, every directory `0o755`, `read_link(libalpha.so) == "Alpha.so"` and `symlink_metadata(libalpha.so)` is a
  symlink; the rebuilt listing digest equals `baad7502…`. PENDING-CI: jobs `check (macos-latest)` and
  `check (ubuntu-24.04)`, step `cargo test` (`cargo test --workspace --locked`, `manager-ci.yml:117-120`), counted by
  step `required tests ran` (`manager-ci.yml:153-178`) through the `_unix` name in `cargo.macos` and `cargo.linux`.
- **SC-012 (escapes on a real POSIX filesystem, symlinked staging root).** Three tests.
  (1) `extract::tests::escape_cases_unix`: the `symlink-escape-root`, `symlink-escape-bundle`, `symlink-chain`,
  `symlink-self` and `through-symlink` fixtures, staged over a forwarding counting wrapper around the real
  `FsExtractIo` (every path they name resolves inside the TempDir, FR-087), are refused with their codes and SC-006's
  zero-write and snapshot assertions.
  (2) `extract::tests::symlinked_staging_root_unix` (macOS and Linux): roots from `temp_roots_via_link` (FR-089) on a
  fresh `TempDir`. Precondition, asserted before staging so the case cannot pass vacuously:
  `ctx.roots.staging_dir() != std::fs::canonicalize(ctx.roots.staging_dir())`. Then `real-shaped` staged over a
  forwarding wrapper around `FsExtractIo` that records every `canonicalize` argument succeeds, and the record holds the
  work directory (spelled through `via`) and every listed path or its parent, so both sides of FR-046's comparison
  went through `canonicalize` (an implementation that canonicalised only the entries would compare `…/real/…` with
  `…/via/…` and refuse; one that canonicalised neither would be caught by the record). Negative twin: the same roots
  with a wrapper whose `canonicalize` maps one staged file outside the canonical work dir (to a sibling inside the
  TempDir) → `E_EXTRACT_ESCAPE` naming that path, the work dir removed. Two FR-046 rows on the same roots (plan
  SI-12): a runtime archive of the base tree plus `l Alpha.vst3/Contents/x86_64-linux/missing.so -> nothere.so`
  (literal listing payload, FR-085) stages, `symlink_metadata(missing.so)` is a link, `read_link` is `nothere.so` and
  `metadata(missing.so)` is `NotFound` (a dangling link, checked through its parent only); and `exec-and-symlink` over
  `forwarding(&FsExtractIo).with_canonicalize(..)` replacing the result for the link itself (the argument ending in
  `x86_64-linux/libalpha.so`) with `Ok(<canonical work>/presets/x)` → `E_EXTRACT_ESCAPE` naming
  `Alpha.vst3/Contents/x86_64-linux/libalpha.so` (inside the work dir but outside `work/Alpha.vst3`, so a check against
  the work dir alone would pass it), the work dir removed.
  (3) `extract::tests::raw_temp_dir_staging_root_macos` (macOS only): roots from `temp_roots_raw` (FR-089), spelled
  under the raw `std::env::temp_dir()` (`/var/folders/…`, where `/var` is a symlink to `/private/var`); the same
  precondition (`staging_dir() != canonicalize(staging_dir())`, i.e. the canonical form starts `/private/var/`), the
  same `real-shaped` success with the same `canonicalize` record, as the roadmap outcome names (lines 655-657).
  PENDING-CI: jobs `check (macos-latest)` (1, 2, 3) and `check (ubuntu-24.04)` (1, 2), step `cargo test`
  (`manager-ci.yml:117-120`), counted by step `required tests ran` (`manager-ci.yml:153-178`) through the names in
  `cargo.macos` / `cargo.linux`.
- **SC-013 (no extended attributes, no quarantine).** `extract::tests::staged_tree_has_no_xattrs_macos`: an archive
  carrying a pax `SCHILY.xattr.com.apple.quarantine` record on `README.txt` stages; `/usr/bin/xattr -lr <root>` prints
  no `com.apple.quarantine` line and no attribute at all. PENDING-CI, `check (macos-latest)`. On every OS,
  `extract::tests::pax_xattr_records_are_ignored` asserts the plan carries no attribute and `MemExtractIo` recorded no
  attribute call (the trait has none). Local; PENDING-CI (`all`).
- **SC-014 (case-folding collision).** `extract::tests::case_fold_collision`: a runtime archive holding
  `presets/a.vstpreset` and `presets/A.vstpreset` (valid under C4, distinct byte paths) with its true payload (FR-085,
  a literal listing), staged with `FsExtractIo`. The expected branch is pinned per target, never selected by the
  probe: on Windows and macOS the test first asserts, as a precondition with the message "temp volume expected
  case-insensitive on this target", that the probe (create `x`, then open `X`, in its own TempDir) reports
  case-insensitive, then asserts `stage` returns `E_EXTRACT_COLLISION` and `extract/` holds no work dir; on Linux it
  asserts the probe reports case-sensitive ("… case-sensitive …"), then that both files are staged with their own
  bytes. A runner whose volume differs from the pinned expectation goes red on the precondition, never green on the
  other branch. Local (Windows: collision); PENDING-CI (`all`: `check (macos-latest)` collision, `check (ubuntu-24.04)`
  both files, step `cargo test`).
- **SC-015 (live registry and discard).** `extract::tests::discard_and_prune`: two `stage` calls of the same artifact
  return two distinct roots; `discard` of the first removes exactly its directory; a third `stage` while the second
  tree is alive leaves the second untouched; after dropping the second without `discard`, the next `stage` removes it.
  `discard` of a tree whose root was swapped (a `StagedTree` built in-crate by the child test module) returns
  `E_EXTRACT_PATH` and removes nothing, for three decoys, all inside the same `fresh_temp_roots()` TempDir: (i) a
  sibling outside `extract/`, `<base>/plugins/vst3-user/victim/` holding a canary file; (ii) `extract/` itself;
  (iii) a grandchild of `extract/`, `extract/<live root>/Alpha.vst3`. After each, the canary file and the live tree are
  still present and the TempDir snapshot (relative path, type, length) equals the one taken before the `discard`
  call, so the "direct child of `extract/`" check is exercised in both directions. Threshold: every assertion. Local;
  PENDING-CI (`all`).
- **SC-016 (`StagedTree` cannot be forged, and is a read-only source).** Compile-fail doctests in `extract.rs`: a struct literal and
  `Default::default()` of `StagedTree` do not compile; a `no_run` doctest reads every accessor, `file_hashes()`
  included. `injection.rs` gains a scan that `StagedTree { ` appears in non-test code only inside `extract.rs`, and a
  scan of the `impl StagedTree` blocks (FR-054) that finds no method taking `self` by value and none named
  `into_*`, `rename`, `persist`, `release` or `take_*`; its planted case (an `fn into_bundle(self)` in a planted
  `impl StagedTree`) is still reported. Threshold: doctests pass as written.
  Local; PENDING-CI (`all`).
- **SC-017 (guards).** `core_dependencies_are_the_fr007_set` (14 names, dev `{minisign}`), the `flate2` TOML row, the
  FR-082 and FR-083 confinement rules, and each new rule's planted-violation case in
  `injection_scanner_reports_planted_violations` (a `flate2::` in `feed.rs`, a `sha2` in `extract.rs`, a
  `create_dir` outside `impl ExtractIo for FsExtractIo`, a `sync_all` in `extract.rs` (FR-055, SC-026), a `tar` dependency key) each fail as planted and pass on the
  real tree. The `create_dir` plant is `fn f() { std::fs::create_dir("x").ok(); }` in `extract.rs` outside the impl,
  which is reported, while the real tree, whose `trait ExtractIo` declares `fn create_dir` / `fn symlink` and whose
  `stage` calls `. create_dir (` / `. symlink (`, stays green (FR-083's method-name exception) (plan SI-2). The FR-088 allowlist: `verified_artifact_findings` reports nothing on the real tree once
  `extract_tests.rs` calls `verify_file (`, and `verified_artifact_scanner_reports_plants` (`injection.rs:4345`) gains
  two planted cases that are still reported: `verify_file (` in `extract.rs` (non-test) and `from_text (` in
  `extract_tests.rs` (the `from_text` list is not widened, FR-084). Threshold: every rule. Local; PENDING-CI (`all`).
- **SC-018 (required tests ran).** `required-tests.json` and `EXPECTED_REQUIRED` hold exactly FR-086's names in FR-086's
  disjoint lanes (`node --test tools/feed-contract/tests/manager-ci-shape.test.js` green). PENDING-CI: each leg's
  `required tests ran` step counts every Phase 8 name of `cargo.all` plus its own lane exactly once.
- **SC-019 (the physical tree must equal the plan).** `extract::tests::tree_mismatch_is_refused` (every OS): over a
  wrapper around `MemExtractIo` whose `read_tree` tampers with the recorded tree, `exec-and-symlink` (true payload)
  is staged with `read_tree` returning, in turn, (a) one extra file `presets/extra.vstpreset`; (b) the tree without
  `README.txt`; (c) `Alpha.so` with mode `0644` where the plan says `0755`; (d) `libalpha.so` with target `Other.so`;
  and a runtime archive holding `presets/Café.vstpreset` in NFC (`Caf\u{e9}`, literal listing) with `read_tree`
  returning the NFD spelling (`Cafe\u{301}`, the HFS+ edge case). Each returns `E_EXTRACT_TREE_MISMATCH` whose `line`
  is the first differing line in listing order (asserted literally per row), `remove_tree` was called on the work dir
  and the Mem tree no longer holds it, and no `StagedTree` exists. Threshold: 5 / 5. Local; PENDING-CI (`all`).
- **SC-020 (the one join and the work-dir check).** `extract::tests::entry_path_rejects_every_bad_form` (every OS, a
  pure table over a work path inside a `fresh_temp_roots()` TempDir, no I/O): `entry_path` returns `E_EXTRACT_PATH` for
  `""`, `/abs`, `a//b`, `a/./b`, `a/../b`, `..`, `.`, `a\b`, `C:x`, `a:b`, `a\0b`, `a/` and a segment that `Path`
  parses as a prefix on Windows (`C:`), and for the good input `Alpha.vst3/Contents/x86_64-linux/Alpha.so` returns
  `work` joined with those four segments, a strict descendant of `work`. These inputs never reach it through `stage`
  (pass 1 refuses them first), so the table is the only proof of FR-041's defence in depth. FR-041's Windows branch
  (plan SI-12): a `#[cfg(windows)]` block asserts `E_EXTRACT_PATH` for `aux.txt`, `Alpha.vst3/CON`,
  `presets/com1.json`, `presets/Nul`, `presets/x.` and `presets/x ` (trailing space), and a `stage` of a C4-valid
  runtime archive holding `presets/aux.vstpreset` (literal payload) over the fail-closed recorder returns
  `E_EXTRACT_PATH` with 0 mutating calls (refused by FR-059's join before the work dir); a `#[cfg(not(windows))]` block
  asserts that the same six inputs each return `Ok` with `work` plus their segments, and that the same `stage`, over
  `forwarding(&FsExtractIo)`, stages.
  `extract::tests::work_dir_must_be_inside_staging` (every OS): `real-shaped` over a wrapper around `FsExtractIo` whose
  (a) `kind_of(work dir)` reports a file, (b) `canonicalize(work dir)` returns a sibling of `staging/` inside the
  TempDir (`<base>/plugins/vst3-user`) → `E_EXTRACT_ESCAPE` each, with no `create_dir` / `create_file` call recorded
  after `create_work_dir` and the work dir removed. Further rows of the same test (over `MemExtractIo`, logic evidence
  on every OS): `extract` planted as a link (plan SI-9) → `E_EXTRACT_IO { step: "create_work_dir", kind:
  InvalidInput }`, nothing created in the link target (the real-filesystem proof is SC-029's
  `prune_removes_real_link_unix` block); and two FR-046 rows (plan SI-12): (e) the dangling-link archive of SC-012
  stages and the Mem tree holds `missing.so` as a link with target `nothere.so`; (f) `exec-and-symlink` over
  `forwarding(&mem).with_canonicalize(..)` replacing the result for `…/x86_64-linux/libalpha.so` with
  `Ok(<work>/presets/x)` → `E_EXTRACT_ESCAPE` naming `Alpha.vst3/Contents/x86_64-linux/libalpha.so`, the Mem tree
  without the work dir. Threshold: every row. Local; PENDING-CI (`all`).
- **SC-021 (one log line per outcome, no absolute path).** `extract::tests::stage_logs_one_line_per_outcome` (every
  OS): with `test_support::install_capture()` (`test_support.rs:186-192`), one successful `stage` of `real-shaped` and
  one refused `stage` of `absolute-path` (fail-closed recorder); `take_mine()` (`:194`) yields exactly one record per
  call, target `krate_manager_core::extract`, level `Info`, each naming the plugin, version and asset name, the success
  record the listing line count 14, the failure record `E_LISTING_ABSOLUTE`; neither record contains the TempDir path
  (raw or canonical) nor the staging path. Threshold: every assertion. Local; PENDING-CI (`all`).
- **SC-022 (no special bits, no archive timestamp).** `extract::tests::special_bits_and_mtime_not_applied_unix`: a
  runtime archive of the base tree whose `Alpha.so` header mode is `04755`, `README.txt` `02644`, `presets/` `01755`,
  every header mtime `978307200` (2001-01-01T00:00:00Z), with its literal listing (`f 0755`, `f 0644`, `d 0755`)
  as payload, staged with `FsExtractIo`: `symlink_metadata(…).permissions().mode() & 0o7777` is `0o755` for
  `Alpha.so`, `0o644` for `README.txt`, `0o755` for `presets/`, and `README.txt`'s `modified()` is later than
  2001-01-02T00:00:00Z. PENDING-CI: jobs `check (macos-latest)` and `check (ubuntu-24.04)`, step `cargo test`, counted
  by step `required tests ran` through the `_unix` name.
- **SC-023 (no symlinks on Windows).** `extract::tests::symlink_entry_unsupported_windows` (`cfg(windows)`):
  `exec-and-symlink` (true payload) staged with the real `FsExtractIo` behind a forwarding counting wrapper returns
  `E_EXTRACT_UNSUPPORTED`, the wrapper recorded no mutating call (not even `create_work_dir`), and `extract/` holds no
  entry; `real-shaped` (no link) staged the same way succeeds. **Junction block** (FR-058's Windows fallback and the
  directory-only `extract/`, plan PD-6 / PD-7) (plan SI-10, SI-12): each case on its own `fresh_temp_roots()` with a
  canary directory `<TempDir>/canary` holding `keep.txt`, junctions made with `cmd /C mklink /J <link> <target>` on
  plain (non-`\\?\`) paths as the Phase 7 test `staged_links_are_not_followed_windows` does (FR-087's `cmd` row):
  (j1) `<staging>/extract/<64 hex>.abcdef` a junction to `canary`, then `extract::prune` over `FsExtractIo` returns 1,
  the junction is gone (`symlink_metadata` → `NotFound`) and `canary/keep.txt` exists with its bytes (the link was
  removed and not followed); (j2) `<staging>/extract` itself a junction to `canary`, then `prune` returns 0 with the
  `canary` snapshot unchanged, and `stage` of `real-shaped` over `forwarding(&FsExtractIo)` returns `E_EXTRACT_IO
  { step: "create_work_dir", kind: InvalidInput }` with the `canary` snapshot still unchanged. Local; PENDING-CI
  (`check (windows-latest)`, lane `cargo.windows`).
- **SC-024 (concurrent `stage` is safe; FR-049).** `extract::tests::concurrent_stage_is_safe` (every OS): over a
  gate-controlled wrapper around `MemExtractIo` (a barrier the test releases) and again over `FsExtractIo`, thread A
  runs `stage` of `real-shaped` and is held inside pass 2 (after `create_work_dir` returned, before the last write)
  while thread B runs `stage` to completion (its pass 2 prune runs while A's work dir exists in `extract/`): in the
  `MemExtractIo` variant B stages `exec-and-symlink`; in the `FsExtractIo` variant B stages `ustar-prefix` (no link,
  12 lines) on every OS, because `FsExtractIo` on Windows refuses a symlink (D4, SC-023); one test body, no `cfg`
  branch (plan SI-4, R-2). Asserted: B succeeds and A's in-progress work dir was not removed (it is present, with the content
  written so far, when A is released); A then succeeds; the two roots are distinct; then 8 threads each `stage` the
  same artifact at once and all 8 succeed with 8 distinct roots, all present afterwards; a `prune` (FR-058) run while
  A is held removes nothing live. A twin where the wrapper makes A's `stage` fail after `create_work_dir` shows that
  A's root is deregistered (the next `prune` removes it if its `remove_tree` was a no-op). Threshold: every
  assertion, 0 live roots removed. Local; PENDING-CI (`all`).
- **SC-025 (read-only source; FR-054).** Covered by SC-016's `impl StagedTree` scan and its planted case, and by
  `extract::tests::discard_and_prune` (SC-015), which stages, copies a file out of `bundle()` with plain `std::fs`
  reads, and shows `discard` removing the whole root with nothing renamed out beforehand. Threshold: the SC-016 scan
  reports nothing on the real tree and reports the planted `into_bundle`. Local; PENDING-CI (`all`).
- **SC-026 (no durability calls; FR-055).** The FR-083 token rule in `injection.rs` finds no `sync_all`, `sync_data`,
  `sync_file` or `sync_dir` in `extract.rs`, and the `ExtractIo` trait declares no method of those names; its planted
  case (a `sync_all` in `extract.rs`) is reported (SC-017). Threshold: rule green on the real tree, red on the plant.
  Local; PENDING-CI (`all`).
- **SC-027 (per-file hashes; FR-056).** `extract::tests::file_hashes_match_written_bytes` (every OS): `real-shaped`
  staged over `FsExtractIo` and `exec-and-symlink` staged over `MemExtractIo` (on every OS, because `FsExtractIo` on
  Windows refuses a symlink; one test body, no `cfg` branch; plan SI-4, R-2), the hash-equals-`write`-payloads clause
  on the Mem recording wrapper; `file_hashes()` holds exactly one entry per `f` line of the listing, in listing order, with
  the listing path as key and no entry for a directory or symlink; each hash equals the SHA-256 (computed in the test
  with `verify::Sha256Stream`) of the file's bytes as built into the test archive (from the in-test builder, not read
  back from disk); over a recording wrapper around `MemExtractIo` the hash equals the SHA-256 of the concatenated
  `write` payloads of that file; and the wrapper's record holds **no** read of a staged file other than the single
  `read_file` of `moduleinfo.json` (SC-028), i.e. no third read. A zero-length file (runtime archive) has the SHA-256
  of the empty input. Threshold: every file, 0 extra reads. Local; PENDING-CI (`all`).
- **SC-028 (staged version check; FR-057).** `extract::tests::moduleinfo_version_must_match` (every OS): (a) `real-shaped`
  with an entry `version_text` `1.2.0` stages (the fixture's class `Version` is `1.2.0`); (b) the same archive with
  `version_text` `1.2.1` and a payload and signature matching that entry → `E_EXTRACT_VERSION { expected: "1.2.1",
  found: "1.2.0" }`, the work dir removed, no `StagedTree`; (c) a runtime archive whose `moduleinfo.json` is not valid
  JSON, and (d) one with no class `Version` → `E_EXTRACT_VERSION` with `found` `unknown`; (e) a runtime archive whose
  `moduleinfo.json` is `max_moduleinfo_bytes + 1` bytes (injected small limit) → `E_EXTRACT_LIMIT { which:
  "moduleinfo" }`; (f) the captured log of (c) contains no absolute path and names `Contents/Resources/moduleinfo.json`
  (FR-057, FR-052); (g) a runtime archive whose `moduleinfo.json` is not valid UTF-8 → `E_EXTRACT_VERSION { found:
  "unknown" }`; (h) one whose `moduleinfo.json` starts with a U+FEFF byte-order mark and carries version `1.2.0`
  stages (plan SI-8). In (b) to (e) and (g) `remove_tree` was called on the work dir and `extract/` is empty
  afterwards.
  `detect.rs` `moduleinfo_version` is `pub(crate)` and every Phase 6 `detect` test still passes unchanged. Threshold:
  every row, exactly that one code. Local; PENDING-CI (`all`).
- **SC-029 (prune; FR-058).** `extract::tests::prune_removes_by_kind` (every OS, over `MemExtractIo`): `extract/` holds
  a non-live directory tree, a non-live regular file, a non-live symlink to a directory outside `extract/` holding a
  canary file (Mem models the link), and one live `StagedTree` root; `extract::prune(env)` returns 3, removes the
  directory tree, the file and the link **as a link** (the canary and the link target are untouched), and leaves the
  live root and its content; a missing `extract/` returns 0 and creates nothing; a `remove_entry` failure on one
  entry (injected `PermissionDenied`) is logged and skipped and the other two are still removed; the captured log has
  no absolute path. `extract::tests::prune_removes_real_link_unix` (macOS and Linux): the same link case on the real
  filesystem under `fresh_temp_roots()` with a canary directory outside `extract/` inside the TempDir; after `prune`
  the canary and its file exist, the link is gone, and a following `stage` succeeds. A linked `extract/` (plan SI-9):
  over `MemExtractIo`, `extract` planted as a link to a canary directory → `prune` returns 0 and the link target is
  untouched; and `prune_removes_real_link_unix` gains the production-body block: on a fresh `fresh_temp_roots()`,
  `<staging>/extract` planted as a real `std::os::unix::fs::symlink` to `<TempDir>/canary` holding `keep.txt`, then
  `prune` over `FsExtractIo` returns 0, then `stage` of `real-shaped` over `RecordingExtractIo::forwarding(&FsExtractIo)`
  returns `E_EXTRACT_IO { step: "create_work_dir", kind: InvalidInput }` (so `FsExtractIo::create_work_dir`'s own
  directory check refused, not the later canonicalisation, which would give `E_EXTRACT_ESCAPE`), the record holds one
  `CreateWorkDir` and no later mutating call, and the canary's snapshot before equals after (no `<64 hex>.*` entry
  created in the link target). `prune` over the fail-closed
  recorder is called by no refused `stage` (SC-006's 0 mutating calls). Threshold: every assertion. Local; PENDING-CI
  (`all`; the `_unix` name in `cargo.macos` and `cargo.linux`).
- **SC-030 (path-length bounds; FR-015, FR-059).** `extract::tests::path_length_bounds` (every OS): (a) a runtime
  archive with a 255-byte segment (true payload and `moduleinfo.json` version) stages and the file exists with its own
  bytes; (b) a 256-byte segment → `E_EXTRACT_LIMIT { which: "segment" }`; (c) with an injected `max_joined_bytes` equal
  to the exact joined length of the longest planned path, `stage` succeeds, and with that value minus one it returns
  `E_EXTRACT_LIMIT { which: "joined" }` (the boundary in both directions); in (c)'s success branch, independently of
  the formula (plan SI-12), `tree.root().file_name()`'s encoded length is exactly 64 + 1 + 6 = 71 bytes and starts with
  `<hex>.`, `tree.root().parent()` is `<staging>/extract`, and `tree.root()` joined with the longest path's components
  has an `as_os_str().as_encoded_bytes().len()` equal to the computed length (the path actually created, so a wrong
  random-part length, prefix spelling or separator in the joined-length computation goes red); (d) in (b) and (c)-minus-one the fail-closed
  recorder holds 0 mutating calls and `extract/` holds no entry. Threshold: every row. Local; PENDING-CI (`all`).
- **SC-031 (gzip zero padding matches Node; FR-010).** `extract::tests::trailing_zero_padding_is_accepted` (every OS):
  `real-shaped.tar.gz` with 1, 10, 511 and 100000 zero bytes appended goes through `plan_archive` (`max_entries`
  64) and yields a listing equal to the committed `.listing`; through `stage` (signed over the padded bytes, true
  payload) it stages; the padded bytes are hashed (the padded file's SHA-256 is what `VerifiedArtifact` holds). With
  one non-zero byte among the trailing bytes (`00 00 01`) and with `garbage!` appended the result is
  `E_LISTING_CORRUPT`. Node (`zlib.gunzipSync`, v24.7.0) was run on this box on the same padded inputs: 0, 1, 10, 511
  and 100000 zero bytes are accepted (matches Rust), `garbage!` fails with `incorrect header check` (matches Rust), and
  `00 00 01` is **accepted** by Node although it is not "only zero bytes": Node ignores any trailing remainder whose
  first byte is zero. The decision (Q8) is the literal rule "a remainder of only zero bytes", so the `00 00 01` row
  expects `E_LISTING_CORRUPT` in Rust and this one residual divergence (a padded archive with a non-zero byte after a
  leading zero) is recorded here and in the compliance row, not hidden; no release archive can carry it (GNU
  `tar -czvf` never pads). No file under `tools/feed-contract/` changes (FR-091). A padding larger than an injected
  `max_tar_bytes` → `E_EXTRACT_LIMIT { which: "tar_bytes" }`. Gzip-layer rows through `plan_archive` (`max_entries`
  64, FR-010) (plan SI-12): (g1) `real-shaped`'s decoded tar split at byte 1000 (not a block boundary) into two
  members, `gz(tar[..1000]) ‖ gz(tar[1000..])` → a listing equal to the committed `.listing` (members concatenate as
  Node's `gunzipSync` does); (g2) the committed file with byte `len - 8` (the first CRC-32 byte) XOR `0x01`; (g3) byte
  `len - 1` (the last ISIZE byte) XOR `0x01`; (g4) the file cut 4 bytes before EOF (inside the 8-byte trailer); (g5)
  the file cut at `len / 2` (inside the deflate stream); (g6) the first byte replaced by `0x00`: (g2) to (g6) each
  `E_LISTING_CORRUPT`, never `E_EXTRACT_IO { step: "read_archive" }` (a byte-slice source never errors; a short
  trailer's `UnexpectedEof` is a decoder error, FR-045). Threshold: every row. Local; PENDING-CI (`all`).

---

## Safety

- **Injected roots.** Phase 8 writes only under `ctx.roots.staging_dir()/extract` (FR-003, FR-040), which production
  derives from the OS state dir in `src-tauri` (Phase 4) and every test from `fresh_temp_roots()` or FR-089's
  constructors on a fresh `TempDir`, whose sandboxed roots panic outside the temp base (`test_support.rs:83-144`).
  It never writes to `~/.vst3`, a VST3 / Components folder, a preset folder, ProgramData or the registry; that is Phase 9 (Linux) and the installers (Windows, macOS).
- **Verified before extraction.** `stage` accepts only a `VerifiedArtifact` (size, SHA-256, minisign signature and the
  C3 binding already checked, Phase 7) and only together with the feed entry that describes exactly that artifact
  (FR-002). Both passes re-hash the archive bytes and must match the verified SHA-256 (D6, FR-044), so the staged tree
  is derived from the signed bytes and nothing else. The `payload` digest is a consistency check of the extracted tree
  against the feed; the trust anchor is the signature.
- **Zip-slip class, on paper and on disk.** Every C4 rule runs on the archive actually extracted, before any write
  (FR-017 to FR-019, D3): absolute, drive, UNC, backslash and `..` paths, escaping and chained symlinks, entries under
  a link, hard links, devices and FIFOs are refused. On disk, each target comes from one checked join (FR-041), files
  are created `create_new` (never through an existing name or link), symlinks are created last, and the physical
  post-check canonicalises both sides (FR-046), because "a lexical check alone is not enough" (`CONTRACT.md:344-346`).
- **Bounded resources.** Decompressed bytes, header count, metadata record size, path length and entry count are
  bounded before memory or disk is spent (D8, FR-015).
- **Nothing added, nothing kept from the archive's metadata.** Only the normalised exec bit is applied; no owner,
  group, timestamp, setuid / setgid / sticky bit or extended attribute (pax `SCHILY.xattr.*` included), so no
  quarantine can be written (FR-013, FR-016, FR-043; SC-013).
- **The elevated helper.** Untouched: Linux has no elevation (roadmap line 118), and Phase 8 runs no process.
- **Keys.** Tests use the committed KAT public key and in-memory `TestSigner` keys only; no private key is written or
  committed (FR-084).
- **What tests never touch.** No test writes outside its temp dir, reads the real state dir, runs an installer or the
  helper, or opens a network connection; only `/usr/bin/xattr -lr` on a temp path runs, on macOS, and `cmd /C mklink
  /J` on temp paths, on Windows (FR-087) (plan SI-1). Bad
  fixtures are parsed in pass 1 and refused before the first write. **Fail-closed even on regression:** every row
  expected to be refused stages over a recording `ExtractIo` whose mutating methods never reach `std::fs` (FR-087), so
  if pass 1 and `entry_path` regressed together, `work.join("/etc/evil")`, `C:\evil` or `\\server\share\evil` would
  still create nothing on this dev box and open no SMB connection; the row fails on its code instead. Only rows whose
  every named path resolves inside their own TempDir run over the real filesystem as well.
- **Non-canonical test roots stay sandboxed.** FR-089's constructors check the canonical form with `check_under_temp`
  before creating anything and sandbox to a base spelled like the roots, so `check_sandboxed` (lexical,
  `roots.rs:175-185`) still panics on any path outside the TempDir.

---

## Edge cases

| Case | Behaviour |
|---|---|
| Interrupted extraction (crash, kill, power loss mid-write) | The work dir `extract/<hex>.<random>` is left; no `StagedTree` was returned, so nothing consumes it; the next `stage` or Phase 9's start-up `extract::prune` removes it (D2, FR-058; SC-010 plants exactly this leftover, and injects a cleanup that never runs, instead of a panic, which would unwind and run every destructor) |
| Cleanup after an error fails (`remove_tree` errors) | The error is logged, the original error is returned, and the leftover is pruned by the next `stage` (FR-045, SC-010) |
| Interrupted download | Not reachable: `stage` needs a `VerifiedArtifact`, which only a complete, verified file yields (Phase 7) |
| Disk full during pass 2 | `E_EXTRACT_FULL` naming the step and entry; the work dir is removed to return the space (FR-045, SC-010). No free-space pre-check (`std` has none) |
| Staging not writable | `E_EXTRACT_DENIED` (SC-010) |
| File locked by a running DAW on Windows | Not reachable: Phase 8 writes only into a fresh work dir under staging, and production extraction is Linux only (D4). Placing over a loaded bundle is Phase 9's journaled swap |
| Partial install | Not reachable: Phase 8 installs nothing. A partial *staged tree* never survives a returned error (FR-045) |
| Downgrade attempt | The archive's version is bound by the signature (Phase 7) and the entry must name the same version (FR-002); whether an older offered version is installed is Phase 9's decision over `version::compare` |
| Corrupt archive (bad gzip, checksum, truncated, malformed pax, base-256) | `E_LISTING_CORRUPT` / `E_LISTING_CHECKSUM` / `E_LISTING_TRUNCATED` in pass 1, nothing written (SC-003) |
| Archive changed between verification and extraction, or between the passes | `E_EXTRACT_CHANGED`, the work dir removed (D6, SC-009) |
| Corrupt feed | `feed::check` rejects it (Phase 5); a parsed `Feed` always has `payload` on a `tar.gz` entry, and FR-002 still refuses an entry without one |
| Offline | Irrelevant: extraction makes no request; the entry may come from the cached feed, and the binding (FR-002) is what matters |
| Feed `schema_version` 1 vs 2 | A v1 document or v1-only entry has no `release` / `platforms` → no `VerifiedArtifact` exists for it; passed anyway, `E_EXTRACT_ENTRY_MISMATCH { field: "release" }` (SC-008) |
| Bundles that still say `0.8.0` | Phase 8 reads the staged `moduleinfo.json` class `Version` and requires it to equal the entry's version (FR-057); a bundle whose class `Version` differs is refused with `E_EXTRACT_VERSION` (SC-028) |
| Archive valid under C4 but differing from the feed's payload | `E_LISTING_COUNT` / `E_LISTING_DIGEST` before any write (SC-007) |
| Names differing only in letter case, on NTFS or default APFS | `E_EXTRACT_COLLISION`, no tree (SC-014); on Linux both are created |
| Non-ASCII names on a normalising filesystem (HFS+ NFD) | The rebuilt listing differs from the plan → `E_EXTRACT_TREE_MISMATCH`, refused safely (SC-019 row with an NFC name read back as NFD); production is Linux (ext4 / btrfs keep bytes) |
| Archive root `./` entry (GNU `tar -C dir .`), possibly repeated | Dropped from the listing and not counted towards the `entries` bound (D8, FR-015; SC-002, SC-005 `dot-slash`) |
| setuid / setgid / sticky bits or an old mtime in a header | Dropped; staged modes are exactly `0755` / `0644`, timestamps are extraction time (FR-016, FR-043, SC-022) |
| A dangling in-bundle relative symlink | Allowed by C4 (lexical containment holds); checked through its parent (FR-046) |
| Trailing zero bytes after the gzip stream | Accepted and ignored, as Node does (FR-010, SC-031); any remainder with a non-zero byte is `E_LISTING_CORRUPT` (Node also accepts `00 00 01`, the one measured residual difference, D1); never produced by `tar -czvf` |
| Two `stage` calls at once | Each owns its work dir, registered live at creation; no call removes another's tree (FR-049, SC-024) |
| The bundle's `moduleinfo.json` class `Version` differs from the entry's version | `E_EXTRACT_VERSION`, tree removed (FR-057, SC-028) |
| A path segment over 255 bytes, or a joined path over 4095 bytes | `E_EXTRACT_LIMIT { segment }` in pass 1, or `{ joined }` before pass 2; no partial tree (FR-015, FR-059, SC-030) |
| A leftover `extract/` entry that is a file or a symlink | Removed by kind, a link as the link itself (FR-058, SC-029) |
| Power loss right after `stage` returns | The tree may be incomplete on disk; nothing syncs it (FR-055); Phase 9 copies and syncs what it places |
| Archive owner / mtime from the CI runner | Ignored (FR-016); staged files belong to the user, timestamps are extraction time |
| Symlink entry on Windows | `E_EXTRACT_UNSUPPORTED` with `FsExtractIo`, raised by `stage` from `supports_symlinks() == false` after pass 1 and before any write (D4, FR-042, SC-023); never a production path |
| The same artifact staged twice | Two independent work dirs (D2, SC-015) |

---

## New components

| Module / file | Purpose |
|---|---|
| `apps/krate-manager/crates/core/src/extract.rs` (🆕) | `EXTRACT_DIR`, `ExtractLimits` (+ `PRODUCTION`), `ExtractEnv<'a> { ctx, io, limits }`, the gzip + tar header reader (D1), the C4 rule engine, `ListingEntry`, `CanonicalListing` (`from_entries`, `text`, `files`, `digest`, `lines`), the crate-private `plan_archive` (pass 1) and `check_staged_layout` (FR-009), `entry_path` (FR-041), `ExtractIo` (D5, `io::Result` methods, `supports_symlinks`) + `FsExtractIo`, the live registry and prune, `stage`, `prune` (FR-058), the live registry with its drop guard (FR-049), `StagedTree` (+ `file_hashes`), `discard`, `ExtractError` (+ `code()`, incl. `E_EXTRACT_VERSION`) |
| `crates/core/src/extract_tests.rs` (🆕) | SC-002 to SC-016, SC-019 to SC-024 and SC-027 to SC-031 cases; the `#[cfg(test)]` ustar / gzip archive builder (FR-085) |
| `crates/core/src/verify.rs` (🔶) | `pub(crate) struct Sha256Stream` (`new`, `update`, `finish`) so `sha2` stays confined (FR-082) |
| `crates/core/src/test_support.rs` (🔶) | `MemExtractIo` (in-memory tree with modes and links), a fault-injecting (including a no-op `remove_tree`), a forwarding counting and a fail-closed recording `ExtractIo` (FR-087), a temp-dir snapshot helper (SC-006); `temp_roots_via_link` (`cfg(unix)`) and `temp_roots_raw` (`cfg(target_os = "macos")`) (FR-089); no `minisign` use (Phase 7 FR-083) |
| `crates/core/src/detect.rs` (🔶) | `moduleinfo_version` from private to `pub(crate)`, behaviour unchanged (FR-057) |
| `crates/core/src/lib.rs` (🔶) | `pub mod extract;` |
| `crates/core/Cargo.toml` (🔶) | `flate2` from `[dev-dependencies]` to `[dependencies]` (FR-080) |
| `crates/core/tests/injection.rs` (🔶) | FR-080, FR-082, FR-083 rules and their planted cases; `EXTRACT_TESTS_RS` added to the `verify_file` callers of `minting_route_findings` and two planted cases in `verified_artifact_scanner_reports_plants` (FR-088); the `StagedTree` constructor scan (SC-016, SC-017); `TEST_PROGRAM_BODIES` gains `(EXTRACT_TESTS_RS, "staged_tree_has_no_xattrs_macos", "/usr/bin/xattr")` and `(EXTRACT_TESTS_RS, "symlink_entry_unsupported_windows", "cmd")`, length 4 → 6 (FR-087) (plan SI-1) |
| `apps/krate-manager/tests/required-tests.json`, `tools/feed-contract/tests/manager-ci-shape.test.js` (🔶) | FR-086 |
| `apps/krate-manager/tests/fixtures/clippy-guard/Cargo.lock` (🔶) | FR-080: `"flate2",` added to `krate-manager-core`'s dependency list, re-locked with `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 > /dev/null` (plan SI-3) (tasks TA-1) |

---

## Traceability

| Roadmap statement (line) | FR / SC |
|---|---|
| "tar.gz extraction of a **verified** artifact (the Linux release asset; … no zip and no Windows or macOS asset) into a fresh staging directory" (639) | D2, D7, FR-001 to FR-003, FR-040; SC-004, SC-008, SC-015 |
| "Rejects absolute paths, `..` components, drive prefixes, entries that resolve outside the target, symlinks whose target escapes the bundle, hard links and device files" (639-641) | D1, D3, FR-010 to FR-019, FR-041, FR-046; SC-003, SC-006, SC-012 |
| "preserves in-bundle relative symlinks and exec bits" (641) | FR-042, FR-043; SC-005, SC-011 |
| "Applies no extended attributes (the manager never adds quarantine)" (641-642) | FR-013, FR-016, FR-043; SC-013 |
| "The path checks (zip-slip class) apply to tar entries unchanged" (642) | FR-017 to FR-019 (the `listing.js` rules and codes); SC-002, SC-003 |
| Staged-layout check: listing count and digest equal `payload`, root matches the C4 layout, "no extra, no missing file, no wrong mode or link"; "no `release.json`" (643-645) | FR-030 to FR-034, FR-047; SC-004, SC-005, SC-007 |
| Outcome: crafted corpus (`../`, absolute, Windows drive and UNC, escaping symlink, symlink chain, duplicate entries, oversized entry count) rejected 100 %, zero bytes outside the staging dir (648-650) | D8, FR-015, FR-085; SC-006 |
| Outcome: real-shaped bundle round-trips with symlinks and exec bits, digest equals the Phase 1 fixture's (unit test on Windows for the logic) (651-652) | D4; SC-004, SC-005 |
| Outcome: PENDING-CI macOS / Linux `cargo test -p krate-manager-core extract::`: exec bits, relative symlinks, escaping-symlink and chain rejected, zero-bytes scan, canonicalisation of both sides under `/var` → `/private/var` (653-657) | FR-046, FR-089; SC-011, SC-012, SC-013, SC-022. The roadmap's `-p krate-manager-core extract::` filter is met by the `extract::tests::*` names inside the job step `cargo test` (`cargo test --workspace --locked`, `manager-ci.yml:117-120`), which runs core's tests unfiltered, counted by `required tests ran` (FR-091 keeps the workflow unchanged). The `/var` → `/private/var` clause is proven by SC-012 (3) on roots spelled under the raw `std::env::temp_dir()`, not by `fresh_temp_roots()`, whose roots are already canonical |
| Outcome: crafted corpus "zero bytes written outside the staging dir" (648-649), on this dev box with the user's real plug-ins | FR-087 fail-closed recorder; SC-006, SC-012 (1) |
| FR-041 / FR-040 / FR-047 / FR-052 / FR-016 defence-in-depth checks | SC-019, SC-020, SC-021, SC-022 |
| "Does NOT: write into any plug-in or preset root" (659) | FR-003, FR-090, FR-091; Safety |
| CONTRACT.md "Phase 8 obligations" 1-3 (339-347) | FR-017 to FR-019, FR-030 to FR-033, FR-043, FR-046, FR-047 |
| Never touch the real system (Cross-Cutting 984-991) | FR-003, FR-087; Safety |
| Every artifact verified before extraction; links escaping the target rejected (992-997) | FR-001, FR-002, FR-044 (D6); SC-008, SC-009 |
| Unsigned reality: never add quarantine (1003-1005) | FR-043; SC-013 |
| Cross-platform, PENDING-CI per OS (1006-1009) | D4; SC-011 to SC-014, SC-022, SC-023 |
| Zero warnings; parallel tests (1010-1014) | SC-001, FR-087 |
| Clarification Q1 to Q8 (2026-11-01), not in the roadmap text: concurrent `stage`, read-only hand-off, no sync, per-file hashes (feeds line 683), staged version check, public prune, path-length bounds, gzip zero padding | FR-049, FR-054 to FR-059, FR-010, FR-015; SC-024 to SC-031 |

---

## Open questions

None. The roadmap defers no Phase 8 decision to this spec; D1 to D9 are this spec's own decisions and stand open to
challenge in the clarify scan.

---

## Review notes

- Node gzip behaviour behind D1, run this session on v24.7.0: `zlib.gunzipSync(gzip("abc") ‖ gzip("def"))` → `abcdef`;
  with 10 trailing zero bytes → `abc`; with trailing `garbage!` → error `incorrect header check`.
- The roadmap's Phase 8 outcome names "duplicate entries" and "symlink chain" (line 649): both are committed bad
  fixtures (`duplicate-entry`, `file-dir-clash`, `symlink-chain`, `symlink-self`, `symlink-dotdot-chain`); UNC paths
  and the oversized entry count have no committed fixture, so SC-006 builds them at run time (FR-085) instead of
  adding files to Phase 1's byte-exact fixture tree.
- Revision 2 (review of 2026-11-01), all 15 issues applied, none rejected, no threshold relaxed:
  - Entry-count bound (two issues): counted entries exclude the archive root `.` / `./` and the pax / GNU records;
    per-entry rules run before the bound on the same entry (D8, FR-015). Measured this session with
    `listing.js` `readTarEntries` over every fixture (script in the session scratchpad): `dot-slash` 11 entries /
    10 lines, `real-shaped` 14 / 14, `implicit-dirs` 5 / 10, per-entry bad archives 11 with the defect last, maximum
    18 (`pax-path`), KAT 10. SC-007 (a) was internally contradictory (files − 1 trips the bound first); it now has
    files + 1 and `implicit-dirs` files − 1 for `E_LISTING_COUNT`, and files − 1 on `real-shaped` for
    `E_EXTRACT_LIMIT { which: "entries" }`, so both codes are reachable and asserted. SC-002 / SC-003 now state their
    `plan_archive` / `check_staged_layout` inputs (FR-009).
  - Symlinked staging root (two issues): `fresh_temp_roots()` builds every root from the canonical base
    (`test_support.rs:83-116`), so SC-012 as written compared canonical paths on both sides. FR-089 adds two
    non-canonical constructors and SC-012 asserts `staging_dir() != canonicalize(staging_dir())` first, plus a record
    of every `canonicalize` argument; the macOS raw `/var/folders` spelling the roadmap names (lines 655-657) is its
    own test.
  - Panic as a simulated kill: the test profile unwinds (`Cargo.toml:54-58`) and `TempDir`'s `Drop` removes its
    directory (`tempfile` 3.27.0 `src/dir/mod.rs:498-503`), so a panic fakes an impossible state. Replaced by a no-op
    `remove_tree` and a planted leftover; D2 now `keep()`s the work dir at creation so every removal goes through
    `ExtractIo`.
  - Counting wrapper over the real filesystem for absolute / drive / UNC rows: replaced by the FR-087 fail-closed
    recorder; only rows whose named paths resolve inside the TempDir also run over `FsExtractIo`.
  - `verify_file` allowlist (`injection.rs:2579-2583`): FR-088 adds `EXTRACT_TESTS_RS`. The `from_text` list is not
    widened because `test_support::kat_signing_key()` and `TestSigner::key()` already supply both keys from
    allowlisted files; the `Expected` check is declaration-and-fields only (`injection.rs:2595-2621`), so it needs no
    change (the reviewer's "if it is also scoped" checked: it is not).
  - PENDING-CI evidence: rows name job, step `cargo test` (`cargo test --workspace --locked`) and step
    `required tests ran`; the roadmap's `-p … extract::` filter is mapped in Traceability.
  - FR-086 lanes made disjoint and listed per lane (18 / 4 / 4 + 2 / 1 new names).
  - New SCs for FRs that had none: SC-019 (FR-047, incl. the NFD edge case), SC-020 (FR-041, FR-040), SC-021
    (FR-052), SC-022 (FR-016, FR-043), SC-023 (the Windows `E_EXTRACT_UNSUPPORTED` path; FR-042 / D5 now say it comes
    from `supports_symlinks()`, not from an `io::Error`, which FR-045 maps to `E_EXTRACT_IO`).
  - SC-009: precedence stated (D6, FR-044) and every row has one expected code; the gzip MTIME row (c) checks the
    raw-hash path with an identical tar stream (FLG 0 and MTIME 0 on `real-shaped.tar.gz` / `exec-and-symlink.tar.gz`,
    `od -t x1 -N 10` this session).
  - SC-014 pinned per target; SC-015 decoys inside the TempDir with a canary and both directions of "direct child".
- Revision 4 (2026-11-01, tasks T002): plan §0.2 SI-1 to SI-12 (`plan.md:52-63`) and tasks TA-1 applied in place, each
  edit tagged `(plan SI-n)` / `(tasks TA-1)`, under the user's acceptance of both plan §10 recommendations (R-1 defer,
  R-2 substitute; Clarifications, "plan preconditions"). Touched: FR-015 (SI-5), FR-040 (SI-6, SI-9),
  FR-041 (SI-12), FR-045 (SI-7), FR-049 (SI-6), FR-057 (SI-8), FR-058 (SI-9, SI-10), FR-080 (SI-3 with TA-1's
  `cargo metadata` re-lock in place of SI-3's `cargo update` form), FR-082, FR-083 (SI-2, SI-12), FR-085
  (SI-11), FR-087 (SI-1, SI-12); SC-003, SC-004, SC-006 (SI-5), SC-009, SC-010, SC-012, SC-017 (SI-2), SC-020 (SI-9,
  SI-12), SC-023 (SI-10), SC-024 and SC-027 (SI-4), SC-028 (SI-8), SC-029 (SI-9), SC-030, SC-031; Safety and New
  components (SI-1, SI-3). No FR / SC id, test name or lane count changed (plan §4.7 totals stay 248 / 11 / 35 / 30 /
  29); `## Clarifications` untouched.

---

## Clarifications

### Session 2026-11-01

Eight questions from the clarify scan of 2026-11-01, all answered by the user. Each line: question, decision, then the
enforcing ids. The spec body (D1, D2, D5, D8, D9, FR-001, FR-010, FR-015, FR-040, FR-042, FR-045, FR-048, FR-050,
FR-051, FR-083, FR-085 to FR-087, the edge cases and the traceability table) was updated to match.

- Q1: How is a concurrent `stage` made safe against another call's prune? -> Register the work dir as live the moment
  `create_work_dir` returns (a drop guard, as `download.rs:652-671`) and prune under the registry lock; concurrent
  stage calls are safe and independent (no process-wide mutex). [FR-049, FR-040, FR-058, SC-024]
- Q2: Does Phase 9 copy out of the `StagedTree` or move parts out? -> Copy only: `StagedTree` is a read-only source
  (no consuming or move API), Phase 9 copies out of it, `discard` removes the whole root. [FR-054, FR-051, SC-016,
  SC-025]
- Q3: Does Phase 8 make the staged tree durable (`sync_all`)? -> No sync in Phase 8: the staged tree is disposable (a
  crash prunes it), Phase 9 syncs what it places. [FR-055, FR-083, SC-017, SC-026]
- Q4: Does Phase 8 hash each file's content while writing it? -> Yes: per-file SHA-256 of the bytes written in pass 2
  (through `verify::Sha256Stream`), exposed as `StagedTree::file_hashes()`; no third read of the staged files.
  [FR-056, FR-042, FR-050, SC-027]
- Q5: Does `stage` read the staged moduleinfo? -> Yes, refuse: read the staged `Contents/Resources/moduleinfo.json`
  with Phase 6's reader (`detect.rs` `moduleinfo_version`, made `pub(crate)`) and require the reduced class `Version`
  to equal `entry.version_text`, else a new `E_EXTRACT_VERSION` and no tree. [FR-057, FR-048, FR-085, SC-028]
- Q6: When and how are leftovers removed? -> A public `extract::prune(env)` that Phase 9's start-up recovery pass
  calls, plus the FR-040 prune; every non-live entry in `extract/` is removed by kind (dir tree, file, link as the link
  itself, never following it) through `ExtractIo`. [FR-058, FR-040, FR-001, SC-029]
- Q7: Which path-length bound? -> Pass 1 also refuses a path segment over 255 bytes (`E_EXTRACT_LIMIT { which:
  "segment" }`), and `stage` refuses before pass 2 when the longest planned path joined to the work dir exceeds 4095
  bytes (Linux `PATH_MAX`; code `E_EXTRACT_LIMIT { which: "joined" }`); no partial tree ever starts. [FR-015, FR-059,
  SC-006, SC-030]
- Q8: Keep the gzip trailing-zero divergence from `listing.js`? -> No: match `listing.js`; after a gzip member ends, a
  remainder of only zero bytes is accepted (a loop over `flate2::bufread::GzDecoder` members, or a peek before a
  multi-member decoder reads the next header); one more Node/Rust parity row; no change to `tools/feed-contract`.
  [FR-010, FR-091, SC-031]

Choices made while encoding (not stated by the user; challenge them in review): the `joined` and `moduleinfo` values
of `E_EXTRACT_LIMIT { which }` and `max_moduleinfo_bytes` (1 MiB); the `random` part of the work dir name counted as
6 characters in FR-059; `prune` returning the removed count; `file_hashes()` returning `(path, [u8; 32])` pairs; and
the measured fact that Node also accepts `00 00 01` after a gzip member (SC-031), which the literal "only zero bytes"
decision refuses in Rust.

### Session 2026-11-01 (plan preconditions)

The user ruled on the two preconditions of plan §10 and tasks.md "Preconditions", both as recommended.

- R-1 (PD-1): which code does a multi-defect archive report first? → defer per-entry rule failures (two bounded
  slots) so the first code equals listing.js's on every archive, with SI-5's three two-defect parity rows. [FR-019, SC-006]
- R-2 (SI-4): how do SC-024 / SC-027 stage on Windows, where FsExtractIo has no symlinks or modes? → substitute on
  every OS: SC-024's FsExtractIo variant stages ustar-prefix; SC-027 stages exec-and-symlink over MemExtractIo and
  real-shaped over FsExtractIo; one test body, no cfg branches. [SC-024, SC-027]
