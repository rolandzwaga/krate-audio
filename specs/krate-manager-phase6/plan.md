# Implementation Plan: Krate Manager Phase 6 — Detection and the Status Screen

**Spec:** `specs/krate-manager-phase6/spec.md` (DRAFT revision 3: review round and the 2026-10-31 clarification session
Q1-Q8 applied, `spec.md:16-17`, `:1358-1399`)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 6 (lines 557-595); Decisions STRATEGY (23), Q4 (27), ROSTER (33);
Key decision 6 (182-188); Phase 10 `run_inno_uninstaller` (714-719, widened by spec FR-092); Not in v1 (971-972);
Cross-Cutting Constraints (975-1016)
**Status:** DRAFT plan, 2026-10-31, verified against `feat/krate-manager` at `3efe70de` (`git log --oneline -1`;
untracked: `specs/krate-manager-phase6/`). Revision 2: the challenge round's 21 issues applied (see Review notes).
**Lanes:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm typecheck / lint /
test, the version lint, `tauri build`, the local `cargo tree` record), the **pipeline lane** (`node --test` over
`tools/feed-contract/tests/`, because `manager-ci-shape.test.js` changes) and an optional local **L-WSL** early signal
(core tests on WSL Ubuntu, §8). No C++ file changes, so there is no `run-gate.js` / `header-consumers.js` lane.

**Build preconditions.** The spec edits of §0.2 (SI-1 to SI-21) land before `tasks.md`; SI-1, SI-2, SI-7 and SI-15
are blocking (the build goes red on existing guards, or the code contradicts the spec, without them). SI-14 needs the
user's re-ruling of Q2 (§0.3, §10 Q-C) before the roadmap hunk of FR-092 is written; no Phase 6 code depends on it. The
questions of §10 have recommended answers this plan is written to.

---

## 0. What this plan adds to the spec

The spec fixes requirements, names, codes and success criteria down to the test names. This plan fixes the file
contents and module boundaries, the exact Rust and TypeScript signatures, the algorithms the spec describes in prose
(the trailing-comma pass, the listing walk, the notes builder and its flattening, the candidate de-duplication), the
manifest and lockfile edits, the changes every existing guard needs to stay green, the golden-file workflow, the test
plan per SC and the local close procedure. Where the existing tree contradicts a spec sentence, the conflict is in §0.2.

### 0.1 Plan decisions

| Id | Decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **`candidates` de-duplicates the nested copies itself.** `CandidateInputs.nested` carries every listing match of this plugin's bundle name; `candidates` drops a match that is `same_path` (Windows table) or `==` (macOS, Linux) as a fixed candidate it built, and drops a later match equal to an earlier one. So the dedupe of FR-006 is pure and covered by SC-002 / SC-029, and `Listing` only filters by name. | FR-006 asks for "a match that is not `same_path` as a fixed candidate"; doing it in the pure function keeps `Listing` free of the candidate table | FR-001, FR-006 (mechanism only) |
| PD-2 | **`ListedCopy` carries `root_index`.** `ListedCopy { root: RootKind, root_index: usize, path: PathBuf }`; `root_index` indexes `plugin_roots`, so a nested copy under the third Linux `Vst3Global` root gets rank 3. | Linux has four `Vst3Global` roots (`platform/linux.rs:38-41`) and FR-001 ranks "the n-th `Vst3Global` root" by position; a `RootKind` alone cannot say which | FR-006 (SI-5) |
| PD-3 | **The listing's case rule follows the host, the dedupe's follows the `Os` argument.** `Listing::scan` reads the real filesystem, so it compares names ASCII-case-insensitively when `cfg!(windows)` and exactly otherwise; `Listing::copies_of(bundle)` filters the stored matches with the **same** `name_matches(on_disk_name, bundle)` that `scan` used, so a Windows `alpha.vst3` found for `Alpha.vst3` is returned for `Alpha.vst3` (never an exact compare of the on-disk name); `candidates(os, …)` dedupes with `same_path` when `os == Os::Windows`, `==` otherwise. | FR-006 "ASCII case-insensitive on Windows, exact elsewhere"; FR-001 "Windows semantics, whichever host runs it" | none |
| PD-4 | **The Windows uninstall subkey is built without a string literal.** `windows.rs` builds `UNINSTALL_KEY` + `\` + key with `String::push_str` / `push('\\')`. `format!("{UNINSTALL_KEY}\\{}")` would add a literal that `scan_core_file` reports as `platform-literal-not-allow-listed` (`injection.rs:985-992`); char literals are blanked and not collected (`injection.rs:381-385`, `:494-512`). The `PLATFORM_LITERALS` entry for `UNINSTALL_KEY` is its **source text** with doubled backslashes, because `Literal.text` is the raw text between the quotes (`injection.rs:366-372`). Raw strings are refused in `platform/` (`:986-987`), so the constant is a normal escaped literal. | `injection.rs` lexer | FR-033 (wording, SI-8) |
| PD-5 | **Wait constants.** `platform/mod.rs`: `pub const OS_VERSION_WAIT: Duration = Duration::from_secs(2)`; `platform/macos.rs`: `pub const PKGUTIL_WAIT: Duration = Duration::from_secs(5)`. Durations are not string literals, so `PLATFORM_LITERALS` is unaffected. | FR-030, D8 | none |
| PD-6 | **Logging of a refused `InstallLocation` happens in `detect_plugin`, not in `inno_record`.** `inno_record(&InnoValues) -> InstallerRecord` stays pure (FR-026's signature); `detect_plugin` writes the one `warn!` `E_RECORD_LOCATION` naming the plugin when the raw value is `Some` and the record's `install_location` is `None`, adding the value only when it has no NUL and is at most 32,767 UTF-16 units. | FR-003 names the plugin, which `&InnoValues` does not carry | none |
| PD-7 | **The AU branch of `probe` on a non-macOS host returns `None`** (no copy, no log). Production and `fresh_temp_roots()` roots carry AU roots on macOS only (`test_support.rs:23-30`), so no such candidate is ever probed off macOS; the arm exists so `probe` compiles without `plist`. | FR-021 "evaluated on another host only builds paths" | none |
| PD-8 | **Notes flattening is one rule for blocks and inlines.** When a block container (a `List` or a `BlockQuote`) would open at depth 9, everything until its matching end is collected as plain text (texts and inline code verbatim, soft / hard breaks and block boundaries as one space, runs of spaces kept as written, the result trimmed) and appended to the innermost allowed block container as one `Paragraph [Text t]`. When an inline container (`Strong`, `Emphasis`) would open at depth 9, its content is appended to the innermost allowed inline container as `Text` (merged with an adjacent `Text`). | FR-053; SC-011's list case ("joins the eighth level's item as one `Text`") and quote case ("as a `Paragraph`") then describe the same output | SC-011 wording (SI-9) |
| PD-9 | **`HtmlBlock`** text is the concatenation of its `Html` events with the final line break removed; inline code and code blocks keep their text verbatim (a code block keeps its trailing `\n`). | FR-052 leaves the trailing newline open; `pre` renders it either way, the golden fixes it | none |
| PD-10 | **The golden files are written by hand from the failure message.** `golden_views` compares `to_string_pretty(&view) + "\n"` with the committed file and, on a mismatch or a missing file, fails with the full expected text in the message. The implementer reviews it and writes the file. No test writes into the repo (FR-086); no environment variable switches a "bless" mode (`clippy.toml` bans `std::env::var` outside two files). | FR-086, FR-010 of Phase 4 | none |
| PD-11 | **Line endings of the fixtures.** `apps/krate-manager/tests/fixtures/.gitattributes` holds `detect/** -text` and `status/** -text`, so Git never rewrites them. This box has `core.autocrlf=true` (`git config core.autocrlf`), the root `.gitattributes` has no rule for `apps/` (only `tools/feed-contract/fixtures/** -text`), and the goldens are compared byte for byte. A nested `.gitattributes` sits inside `apps/krate-manager/**`, which FR-090 allows. | SC-012 byte equality | New components (SI-7) |
| PD-12 | **The view goldens reach TypeScript through a runtime validator.** `src/view.ts` exports `toStatusView(value: unknown): StatusView`, which checks every key, type and enum value and throws a `TypeError` naming the JSON path; every `renders <scenario>` case passes its imported golden through it. A golden that drifts from the types fails `npm test`. A JSON import cannot fail `tsc` for an enum drift: TypeScript widens JSON string values to `string`, which is not assignable to `"windows" \| "macos" \| "linux"` (so a direct assignment never compiles, and a cast would hide every drift). | `tsconfig.json` (`resolveJsonModule`) | FR-076 (SI-4) |
| PD-13 | **A rejected `check_now` keeps the previous cards** and replaces only the feed line with `Could not load the plugin list: <rejection text>`; a rejected first `plugin_status` shows that line and no cards. The ten-minute note is rendered with the first view (its value comes from `edge_max_age_secs`) and absent before it. | FR-073 / FR-074 do not say what happens to the cards; §10 Q-A | FR-073, FR-074 (§10 Q-A) |
| PD-14 | **The status texts live in two flat tables.** `STATUS_TEXTS` (every FR-071 cell, plus the feed-line templates of FR-074, the `Check now` label and the ten-minute note, all of which some golden reaches) and `ACTION_TEXTS` (the pending text, `Checking…`, the open-failure text). Templates use `{name}` placeholders and one `fill(template, values)` helper. | FR-071 coverage rule ("so the table and the code cannot drift") | none |
| PD-15 | **New root pins are checked where they are declared.** `core_dependencies_are_the_fr007_set` gains: the root pins `pulldown-cmark = { version = "0.13.6", default-features = false }` and `plist = { version = "1.10.3", default-features = false }`, and member entries exactly `{ workspace = true }`. `CONFINED_TOML_KEYS` (`injection.rs:153-164`) gains `pulldown-cmark` and `plist` (root + `crates/core/Cargo.toml`) and `tauri-plugin-opener` (root + `src-tauri/Cargo.toml`). `READER_PINS` changes only windows-sys's feature list (FR-081). | FR-080, FR-065 | none |
| PD-16 | **windows-sys feature order keeps `Win32_Networking_WinHttp` last**, so the existing `reader_dependencies_are_the_ruled_set` plant anchor `"Win32_Networking_WinHttp"]` (`injection.rs:2707-2709`) still matches exactly once (`replace_one` asserts one match, `:1936-1940`); `features_finding` sorts before comparing (`:1915-1922`), so the order is otherwise free. The root comment "exactly two features, no Win32_System_Time" (`Cargo.toml:35`) becomes "exactly five features (FR-081), no Win32_System_Time". | `injection.rs` | none |
| PD-17 | **The listing reads each directory whole, then sorts.** `list_root` collects a directory's `DirEntry` values, sorts them by `file_name().as_encoded_bytes()`, then examines them in order until the root's cap. A sorted order needs every name of that one directory; the memory is the names of one directory, bounded by the OS. | FR-006 "visited in file-name byte order" | none |
| PD-18 | **`renderShell` awaits the first status load.** `main.ts` appends the status section (pending text, disabled button) before awaiting `plugin_status`, so a test that holds `plugin_status` pending observes the pending state, and a test that awaits `renderShell` sees the settled state. | FR-070, SC-035; `mockIPC` calls the handler inside `invoke` and returns its result (`node_modules/@tauri-apps/api/mocks.js:126-132`) | none |
| PD-19 | **The case-rule test asserts on `Listing` only**, at a nested path (`vst3-user/Old/alpha.vst3`), through both `paths()` and `copies_of("Alpha.vst3")`: `listing_case_windows` expects `copies_of` to return exactly the `vst3-user/Old/alpha.vst3` copy, `listing_case_exact_unix` expects `paths()` and `copies_of` both empty. So a `scan` that matches case-insensitively but a `copies_of` that compares exactly fails the Windows test. At a fixed candidate path the case rule cannot be observed: NTFS and default APFS are case-insensitive, so `fs::metadata` of `vst3-user/Alpha.vst3` finds `alpha.vst3` whatever the listing does. | SC-029 | SC-029 (SI-6) |
| PD-20 | **The opener crate is called, never registered as a plugin.** `open_release_page` calls the crate-root free function `tauri_plugin_opener::open_url(valid, None::<&str>)`; `lib.rs` gains no `.plugin(tauri_plugin_opener::init())` and nothing names `OpenerExt`. `init()` is `Builder::default().build()`, whose `Default` sets `open_js_links_on_click: true`, so registering it would inject the crate's `init-iife.js` into every page and register the three plugin IPC commands `open_url`, `open_path`, `reveal_item_in_dir` (denied only because no `opener:*` permission is granted: one capability edit from an arbitrary path opener). The free function needs no managed state: it calls the private `open`, which calls `::open::that_detached` when `with` is `None`. | docs.rs source `tauri-plugin-opener` 2.7.2 `src/lib.rs` (re-export `pub use open::{open_path, open_url};`, `Builder` `Default`, `build`, `init`) and `src/open.rs` (`pub fn open_url<P: AsRef<str>, S: AsRef<str>>(url: P, with: Option<S>) -> crate::Result<()>`) (§1.2); the cross-cutting "narrowest Tauri capability set" constraint | D15, FR-064, FR-065 (SI-15) |
| PD-21 | **The view enums carry the FR-046 spellings by their variant names, never by a `rename`.** `status.rs` has its own `UnknownReasonView` whose variant for `NoModuleInfo` is `NoModuleinfo` (with a one-line doc: "spelled so `rename_all = "snake_case"` gives FR-046's `no_moduleinfo`; `NoModuleInfo` would give `no_module_info`, and `rename` is an FR-084 token"); every other view variant already snake-cases to its FR-046 string. `golden_views` also serialises every variant of every view enum (`OsName`, `FeedSource`, `AvailabilityView`, `InstalledView`, `UpdateView`, `FormatView`, `PlaceView`, `VersionSourceView`, `UnknownReasonView`, `RecordKind`) and compares each against the FR-046 strings written as literals in the test. | serde's `SnakeCase` inserts `_` before every uppercase character after the first, so `NoModuleInfo` → `no_module_info`; FR-084 bans the identifier `rename` in `status.rs` (`spec.md:743-744`) and the scanner tokenises attributes as code (`injection.rs:589-620`: `strip_comments_and_literals` keeps attributes in `code`) | none |
| PD-22 | **`read_bounded` checks the file type before it opens.** `std::fs::metadata(path)` first (it follows symlinks, as FR-004 rules): `NotFound` → `ReadFail::NotFound`; other error → `ReadFail::Io`; `!m.is_file()` (a directory, FIFO, socket, device) → `ReadFail::Io`; only then `File::open`. So a FIFO or tty named `moduleinfo.json`, `Info.plist` or `<stem>.json` is never opened: open(2) `O_RDONLY` on a FIFO blocks until a writer appears, which `take` does not bound, and detection is sequential (ruled Q4), so one such file would hold every plugin's status. | FR-011 "any read error → `Unreadable`"; FR-020; FR-028 | SC-005, SC-027 cases (SI-17) |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before tasks)

| Id | Issue (evidence) | Proposed fix | Blocking? |
|---|---|---|---|
| SI-1 | `injection.rs:147` confines the token `windows_sys` to `src-tauri/src/os_proxy.rs`: `("windows_sys", &[OS_PROXY_RS])`, checked by `ureq_is_confined_to_http_rs` (`:2324`). FR-032 puts `windows_sys` calls in `os_facts.rs`, so that existing test goes red. FR-082 and FR-090 do not name the change. | FR-082 adds: "`CONFINED_TOKENS`' `windows_sys` row becomes `[OS_PROXY_RS, OS_FACTS_RS]`"; the existing `TAURI_LIB_RS` plant (`:2400-2403`) stays reported. | yes |
| SI-2 | The Phase 4 guard forbids every Tauri plugin crate in `src-tauri/Cargo.toml`: any line containing `tauri-plugin` fails "names a tauri-plugin crate (FR-004)" (`config_guard.rs:688-695`), the `[dependencies]` key set is exactly five (`:32-33`, checked `:671-677`), and two existing plants expect `tauri-plugin-opener` to be reported (`:1310-1331`). Phase 4 FR-004 says "no Tauri plugin crate dependency and no plugin permission" (`specs/krate-manager-phase4/spec.md:389-395`). The Q8 ruling (FR-065) needs exactly that crate, while FR-063 says "every other Phase 4 / Phase 5 assertion stands". | FR-063 / FR-065 state the narrowing: the `[dependencies]` key set gains `tauri-plugin-opener` (six keys); the only accepted `tauri-plugin` line is exactly `tauri-plugin-opener = { workspace = true }` in `[dependencies]`; any other `tauri-plugin*` line, or that crate in another table, still fails; the `[dependencies]` plant changes to `tauri-plugin-fs = "2"` (reported twice as now), the `[build-dependencies]` plant stays. Traceability's departures gain "Phase 4 FR-004 (no Tauri plugin crate) is narrowed by Q8 to exactly `tauri-plugin-opener`, with no plugin permission granted". | yes |
| SI-3 | FR-085's OS lists omit three OS-gated tests SC-029 names: `detect::tests::listing_case_windows` (Windows), `detect::tests::listing_skips_symlinked_dirs_unix` (macOS, Linux), and the unnamed macOS / Linux counterpart of the case test ("the same assertion on macOS and Linux expects no match"). | Name it `detect::tests::listing_case_exact_unix` (`cfg(unix)`); FR-085 adds `listing_case_windows` to `windows` and the two `_unix` tests to `macos` and `linux`. | no (names only) |
| SI-4 | FR-076 says a golden that no longer matches the types "fails `npm run typecheck`". JSON imports widen string values to `string`, so a golden is never assignable to the `StatusView` union types: typecheck cannot carry that check (PD-12). | FR-076: "`vitest` imports the golden fixtures with `resolveJsonModule` and passes each through `toStatusView` (`src/view.ts`), which rejects a missing key, a wrong type or an unknown enum value with a `TypeError` naming the path, so a drifted fixture fails `npm test`." | no |
| SI-5 | FR-006's `ListedCopy { root: RootKind, path }` cannot give a nested copy "its root's rank": Linux has four `Vst3Global` roots ranked 1-4 by position (FR-001; `linux.rs:38-41`). | `ListedCopy { root: RootKind, root_index: usize, path: PathBuf }` (PD-2). | no |
| SI-6 | SC-029's case rule cannot be observed at a fixed candidate path on NTFS or default APFS (PD-19). | SC-029: the case tests call `Listing::scan` directly with the bundle at `vst3-user/Old/alpha.vst3` and assert on the listing's matches. | no |
| SI-7 | The goldens and detect fixtures are compared byte for byte, but this box checks out with `core.autocrlf=true` and the root `.gitattributes` has no rule for `apps/`; a Windows checkout would get CRLF and SC-012 would fail. | New components add `apps/krate-manager/tests/fixtures/.gitattributes` (PD-11). | yes |
| SI-8 | FR-033 says `PLATFORM_LITERALS` "gains exactly these ten literals"; the scanner compares the **source text** (`injection.rs:366-372`), so `UNINSTALL_KEY`'s entry is `"SOFTWARE\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall"` in `injection.rs` source, and the subkey must be built without a new literal (PD-4). | FR-033: one sentence naming both. | no |
| SI-9 | FR-053 / SC-011 describe the flattened form differently for lists ("joins the eighth level's item as one `Text`") and quotes ("as a `Paragraph`"). | SC-011: both become "one `Paragraph [Text …]` appended to the innermost allowed container" (PD-8). | no |
| SI-10 | SC-019's coverage case says "every literal part … occurs in the `textContent` of at least one rendered card", but the feed-line and check-control texts sit outside the cards (PD-14). | "… of at least one rendered status section". | no |
| SI-11 | FR-073 / FR-074 do not say whether a rejected `check_now` keeps the previous cards, and the ten-minute note needs `edge_max_age_secs` from a view that does not exist before the first result. | PD-13, §10 Q-A. | no |
| SI-12 | The tasks stage needs SC-008's `outside_install_location_panics` to name the panic text: `check_sandboxed` reuses `with_configured_plugin_root`'s check (`roots.rs:136-144`) but its message must name the install location, not "configured plug-in root". | FR-005: the panic reads `install location <path> is outside the test sandbox <base>`. | no |
| SI-13 | FR-042's `Detection::Checked { copies, record, record_error }` cannot answer FR-044 when the managed copy is absent: `loads_before_managed` ranks against the managed *location* (SC-006 Bravo: rank 1 with no copy there), and `assemble` sees only probed copies. | FR-042 adds a fourth field `managed_rank: Option<u8>`, the managed VST3 candidate's rank (§4.6). | no |
| SI-14 | FR-092 widens Phase 10's `run_inno_uninstaller` to any `unins*.exe` in the directory HKLM `InstallLocation` names, on the premise "a value only an administrator can write" (`spec.md:803`, Q2 at `:1314-1318`). The premise holds for the value, not for the files it points to: every `setup.iss` has `DefaultDirName={commonpf}\Common Files\VST3\Krate Audio\<P>` (`plugins/iterum/installers/windows/setup.iss:37`), `PrivilegesRequired=admin` (`:50`), `UsePreviousAppDir=yes` (`:53`) and no `DisableDirPage` or `[Dirs]` `Permissions:` entry (`grep` over `plugins/*/installers/windows/setup.iss`, 0 files), so on a first install the user may pick any folder, whose DACL is inherited from its parent. On a non-system NTFS volume that folder can be writable by unelevated users, so `unins000.exe` / `unins000.dat` there could be replaced, and the widened rule would run them elevated behind the manager's own UAC prompt. | Re-rule Q2 (§10 Q-C). Recommended (a): FR-092 no longer widens the rule; it **adds** one sentence to Phase 10 after the unchanged clause: "A managed copy whose recorded `InstallLocation` lies outside the compiled-in Windows plug-in root is never uninstalled by the helper: the manager shows it as managed and tells the user to uninstall it from Windows Settings." D1's parenthesis and the Q2 clarification text drop "the roadmap's Phase 10 rule is widened to read the same value". SC-028 becomes: exactly 1 occurrence of `in the plugin's install dir under the compiled-in Windows plug-in root`, exactly 1 of `is never uninstalled by the helper`, and the roadmap diff shows only the Phase 6 status line and that added sentence. (b), if the user keeps the widening: FR-092's sentence also requires the helper to check, before running anything, that the owner and DACL of `unins*.exe`, `unins*.dat` and every ancestor directory up to the volume root grant no write, delete, `WRITE_DAC` or `WRITE_OWNER` right to any principal other than Administrators, SYSTEM or TrustedInstaller, and to refuse with a named result otherwise; SC-028's needle becomes that full sentence. FR-001 (the managed location) is unchanged either way: Phase 6 only reads. | needs the ruling (roadmap hunk only) |
| SI-15 | FR-064, FR-065 and D15 have `lib.rs` add `.plugin(tauri_plugin_opener::init())` and the command call `app.opener().open_url(…)` through `OpenerExt`. `init()` injects the crate's `init-iife.js` into every page and registers three plugin IPC commands (`open_url`, `open_path`, `reveal_item_in_dir`) the app never uses (PD-20). | D15 / FR-064: the command is `pub fn open_release_page(url: String) -> Result<(), String>` (no `AppHandle`, which would be an unused parameter) and calls `tauri_plugin_opener::open_url(valid, None::<&str>)`, the crate-root free function; `lib.rs` registers no opener plugin. FR-065: the identifier `tauri_plugin_opener` occurs only in `src-tauri/src/commands.rs`; no source holds the token sequences `tauri_plugin_opener :: init`, `tauri_plugin_opener :: Builder` or the identifier `OpenerExt`; `opener_guard_reports_plants` also plants `.plugin(tauri_plugin_opener::init())` in `commands.rs` and expects it reported. SI-2's crate narrowing stands (the crate is still a dependency). `LIB_RS_STRINGS` is unchanged (14). | yes |
| SI-16 | FR-012 says the pass removes **each** comma outside a string followed by whitespace and `}` / `]` and "changes nothing else", so `[,]` → `[]` would parse; SC-004 requires `[,]` and `{,}` → `Corrupt`. §4.5.5 keeps a comma whose previous non-whitespace character is `[`, `{` or `,`. | FR-012 gains: "… and then `}` or `]`, and is preceded, ignoring JSON whitespace, by a character other than `[`, `{` or `,`" (§4.5.5). | no |
| SI-17 | Several specified branches have no test case: FR-011 (one leading U+FEFF skipped; non-UTF-8 bytes → `Corrupt`), FR-012 (a non-object root), FR-013 (a), (c), (d) non-string values, FR-020 (a plist over 1 MiB), FR-006 (an unreadable directory skipped without a log), FR-028 (an oversize or unreadable manifest), FR-052 (`HtmlBlock`, `Rule`, `SoftBreak`, `List(Some(1))`), FR-072 (the tag mapping), and PD-22 (a non-regular file). | The SCs gain the cases of §7.1 rows 004/005, 007, 011, 013, 019, 027, 029 verbatim (each named there with its expected result). No new test name except SI-20's. | no |
| SI-18 | FR-028 maps "anything else" to `E_RECORD_PARSE`, but a manifest that cannot be read or is over 4 MiB is a read failure, which every other record maps to `E_RECORD_READ` (`RegistryRead::Failed`, `pkgutil --files` `NoOutput`; §3). | FR-028: "… missing → absent; a read error, a path that is not a regular file, or more than 4 MiB → `E_RECORD_READ`; …anything else → `E_RECORD_PARSE`". SC-007 gains a 4 MiB + 1 byte manifest (generated at test time) and a directory named `Alpha.json`, both `E_RECORD_READ`. | no |
| SI-19 | FR-033 has Linux run a bare `getconf`, which `Command::new` resolves through the process `PATH`, while macOS runs its tools by absolute path. | FR-033: `GETCONF = "/usr/bin/getconf"` (glibc's `getconf` is installed there by Debian / Ubuntu `libc-bin` and Fedora `glibc-common`); where it is absent the run is `NoOutput`, so `None`, which never gates (Phase 5 FR-037). `PLATFORM_LITERALS` keeps 37 entries (the `getconf` entry's text changes). | no |
| SI-20 | SC-029 proves "a linked directory is not entered" only on Unix; Windows has its own listing semantics (NTFS junctions can be made without admin rights, `mklink /J`) and no test. | SC-029 gains `detect::tests::listing_skips_junction_windows` (gated on Windows): inside the temp root, a junction loop `vst3-user/a/loop` → `vst3-user/a`, and a junction `vst3-user/out` → a temp directory outside every root that holds `Alpha.vst3`; expected: the listing terminates, reports no copy through either junction, and the root's `examined` count is finite (capture logger `debug!` line). FR-085's `windows` list adds it. | no |
| SI-21 | FR-061 fixes only the `permissions` array; the capability `description` "The main window may call only the app's own read-only commands." (`capabilities/main.json:3`) is no longer true once `open_release_page` opens the browser. | FR-061 adds: "`description` is exactly `The main window may call only the app's own commands.`"; `real_src_tauri_config_passes_the_guard` compares it exactly. | no |

### 0.3 Precondition rulings for the user

None is needed to start the code. One re-ruling is needed before the FR-092 roadmap hunk is written: **Q2's premise
was incomplete** (SI-14; §10 Q-C). Q2 was ruled on "a value only an administrator can write"; the value is admin-only,
but the folder it names, and the `unins*.exe` / `unins*.dat` in it, need not be. This plan is written to option (a)
(keep Phase 10's "under the compiled-in root" clause for elevated uninstalls), which changes no Phase 6 code. Two
small product questions are also in §10 with the answer this plan is written to.

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-10-31 at `3efe70de`)

| Fact | Where |
|---|---|
| `APP_IDENTIFIER`, `enum Os`, `trait Platform { os; default_roots }`, per-OS modules compiled on their own OS only, `pub use <os>::{ENV_KEYS, …Platform as NativePlatform}` | `crates/core/src/platform/mod.rs:9`, `:12-16`, `:55-58`, `:60-71` |
| Windows roots `Vst3User` `%LOCALAPPDATA%\Programs\Common\VST3`, `Vst3Global` `%CommonProgramW6432%\VST3`; the literal `"Krate Audio"` already in `windows.rs` | `platform/windows.rs:33-42`, `:44` |
| macOS roots VST3 user/global, AU user/global | `platform/macos.rs:23-40` |
| Linux `Vst3User` `~/.vst3` and four `Vst3Global` roots in order | `platform/linux.rs:33-42` |
| Exactly three `impl Platform for …` in the workspace | `grep -rn "impl Platform for"`: `linux.rs:15`, `macos.rs:14`, `windows.rs:21` |
| `RootKind`, `with_configured_plugin_root`'s sandbox check `x != base && x.starts_with(base)` and panic, `vst3_user()` | `crates/core/src/roots.rs:10-16`, `:136-144`, `:196-202` |
| `Settings::linux_plugin_root_or_default` | `settings.rs:28-32` |
| `AppContext { roots, settings, feed_url, http }` (`Clone`), `fold` (Linux only), `reassemble_with_os` | `context.rs:49-56`, `:124-132`, `:142` |
| `HttpConfig` holds `PacHostHandle(Arc<dyn PacHost>)`, `PacHost: Send + Sync`, so `AppContext` is `Send` | `http.rs:35-42`; `pac.rs:70`, `:110` |
| `feed::EDGE_MAX_AGE_SECS = 600`; `Feed.plugins: BTreeMap<String, PluginEntry>`; `PluginEntry.version: Option<CoreVersion>`; `StableRelease.release_notes_url`, `notes_markdown`, `install`; `InstallFacts` five fields; `PlainName` (no public constructor, `pub(crate) fn checked`) | `feed.rs:22`, `:37-62`, `:121-153` |
| A refused `install` clears `platforms` and sets `install: None` | `feed.rs:459-469` |
| `FetchMode`, `SnapshotSource`, `FeedSnapshot`, `FeedCheck` (public fields), `FeedError` (`Display` begins with its code), `check(ctx, mode, now_unix)` | `feed.rs:589-675`, `:741-743` |
| `HttpError::Offline { detail }` → `E_FEED_OFFLINE: …` | `http.rs:228-229` |
| `CoreVersion::parse` (exactly three parts), `reduce_class_version`, `compare`, `Arch::native`, `OsVersion::parse` (2-4 parts), `resolve`, `resolve_native` | `version.rs:32-47`, `:56-65`, `:84-94`, `:105-113`, `:125-137`, `:211-240` |
| `run_bounded(commands, bound) -> BoundedRun { outputs, abandoned }`, compiled on every OS; spawn failure / non-zero exit / read error all `outputs: None, abandoned: []` | `sysproxy.rs:180-229` |
| The existing `run_bounded` call pattern in `src-tauri` | `src-tauri/src/os_proxy.rs:144-152`, `:221-227` |
| `PLUGIN_DIRS`, `fresh_temp_roots`, `install_capture` (max level `Trace`), `take_mine` (per thread) | `test_support.rs:23-30`, `:125-129`, `:171-190` |
| Test helpers: fixture feeds read from `CARGO_MANIFEST_DIR/../../../../tools/feed-contract/fixtures`; `context(roots, url)` = loopback 1 s / 1 s, `Settings::default()` | `feed_tests.rs:50-56`, `:236-255` |
| `FixtureServer::start(routes)`, `url(path)`, `set_route`, `requests()`; `Reply::{Ok, Close, …}` | `fixture_server.rs:16-50`, `:107-165` |
| `src-tauri`: two commands, builder, `build.rs`, capability, `Cargo.toml` (`test = false` both targets, `[target.'cfg(windows)'.dependencies] windows-sys`) | `src-tauri/src/commands.rs:1-14`; `lib.rs:72-90`; `build.rs:1-8`; `capabilities/main.json:1-6`; `Cargo.toml:10-19`, `:24-36` |
| Workspace pins; `windows-sys` two features with the comment "exactly two features"; `[workspace.lints.clippy] allow_attributes = "deny"` (so every suppression is `#[expect(…, reason = …)]`) | `apps/krate-manager/Cargo.toml:13-36`, `:38-40` |
| Core `[dependencies]` (10 keys), `[dev-dependencies]` `flate2`, target tables `rquickjs` (not Windows), `core-foundation` (macOS) | `crates/core/Cargo.toml:15-37` |
| `injection.rs`: `FORBIDDEN_OUTSIDE_PLATFORM`, `FS_TOKENS` (platform + roots), `PLATFORM_LITERALS` (27), `FR007_DEPENDENCIES` (10), `CONFINED_TOKENS` (18, `windows_sys` → `os_proxy.rs` only), `CONFINED_TOML_KEYS` (6), `READER_PINS` (windows-sys 2 features); literal text = source text; char literals not collected; `scan_core_file`; `confinement` helper; `plant` / `replace_one` (asserts one anchor) | `crates/core/tests/injection.rs:15-71`, `:87-98`, `:124-164`, `:275-304`, `:352-405`, `:494-512`, `:968-1017`, `:625-646`, `:1925-1940` |
| `core_dependencies_are_the_fr007_set` asserts the two target tables exactly one crate each | `injection.rs:2218-2264` (`:2240-2247`) |
| `reader_dependencies_are_the_ruled_set` plant anchor `"\"Win32_Networking_WinHttp\"]"` | `injection.rs:2702-2747` (`:2705-2710`) |
| `config_guard.rs`: capability permissions exactly two (`:527-532`); `build.rs` two commands (`:547-561`); `SRC_TAURI_DEPENDENCIES` five (`:32-33`); no `tauri-plugin` line (`:688-695`); opener plants (`:1310-1331`); `LIB_RS_STRINGS` (`:35-50`); frontend scan bans `http://` / `https://` in every `src/` file and `index.html` (`:285-301`, `:860-889`) | `crates/core/tests/config_guard.rs` |
| `roster.rs` scans `crates/*/src`, `src-tauri/src`, capabilities, `src`, conf files, `index.html`; excludes `*_tests.rs`, `test_support.rs`, `*.test.ts`; case-sensitive | `crates/core/tests/roster.rs:76-79`, `:98-124`, `:127-170` |
| `tests/required-tests.json`: `cargo.all` 130 names, `cargo.windows` 2, `cargo.macos` 17, `cargo.linux` 16, `vitest` 3 | `node -e` over the file, this session |
| `main.ts` `renderShell` flow; four vitest cases, three of them required | `src/main.ts:1-26`; `src/main.test.ts:7-70` |
| ESLint `strictTypeChecked`, sink bans; `tests/fixtures/` ignored | `eslint.config.mjs:6-23` |
| `tsconfig.json`: `strict`, `noUncheckedIndexedAccess`, `exactOptionalPropertyTypes`, `resolveJsonModule`, `include: ["src"]`, no `types` key (so `@types/node` 26.6.6 is visible to tests) | `tsconfig.json`; `package.json` |
| vitest `environment: "jsdom"`, `include: ["src/**/*.test.ts"]` | `vite.config.ts:29-32` |
| `manager-ci.yml` `check` job: step order, `cargo test` tee, `required tests ran` rule (one `test <name> ... ok` line per name; `- should panic` accepted), notice carries counts only | `.github/workflows/manager-ci.yml:28-162` |
| The shape test's `STEPS`, `ASSERTION_STEPS`, `COMMANDS` | `tools/feed-contract/tests/manager-ci-shape.test.js:163-280` |
| `feed-tools.yml` pins the TAP plan `1..59`, so the shape test may change only inside its existing test cases | `.github/workflows/feed-tools.yml:79-86` |
| Fixture feeds: `full-v2` Alpha / Bravo / Charlie with install blocks (Alpha `darwin-aarch64` `min_os` `11.0`, notes `"### Fixed\n\n- A fixture note."`); `tenth-plugin` adds Delta … Juliet (`install: {vst3_bundle}` only, `linux-x86_64` only); `mixed` Delta v2 without install; `v1-only` three v1 entries; every `release_notes_url` `https://github.com/rolandzwaga/krate-audio/releases/tag/<tag>` | `tools/feed-contract/fixtures/feeds/valid/*.json` (summarised with `node -e`, this session) |
| `CONTRACT.md` install patterns: `windows_uninstall_key` `^\{[0-9A-F]{8}-…\}_is1$` (upper-case hex), `macos_pkg_id` `^com\.krateaudio\.[a-z0-9]+$` | `tools/feed-contract/CONTRACT.md:71-75` |
| `.gitattributes` active rules: four `eol=lf` files, `tools/macos-fastmath-repro.sh`, `tools/feed-contract/fixtures/** -text`; `git config core.autocrlf` = `true` | repo root `.gitattributes`; this session |
| `core`'s normal dependency tree today: 76 crates on the Windows host, 0 matches of the FR-080 regex; also 0 for `--target x86_64-apple-darwin` and `x86_64-unknown-linux-gnu` | `cargo tree -p krate-manager-core -e normal --locked --offline --prefix none --format "{p}"`, this session |
| `CHANGELOG.md` `[0.1.0] - Unreleased` holds one Phase 4 bullet; Phase 5 added none | `apps/krate-manager/CHANGELOG.md:8-12`; `git log -- CHANGELOG.md` |

### 1.2 External facts (verified 2026-10-31)

| Fact | Source |
|---|---|
| `pulldown-cmark` 0.13.6: features `{"default":["getopts","html"],"gen-tests":[],"html":["pulldown-cmark-escape"],"simd":["pulldown-cmark-escape?/simd"]}`, `rust_version` 1.71.1, MIT, published 2026-10-30. `Tag` variants: `Heading { level: HeadingLevel, id, classes, attrs }`, `BlockQuote(Option<BlockQuoteKind>)`, `CodeBlock(CodeBlockKind)`, `List(Option<u64>)`, `Link { link_type, dest_url, title, id }`, `Image { … }`, unit `Paragraph`, `HtmlBlock`, `Item`, `Emphasis`, `Strong`, `Strikethrough`, … `HeadingLevel` `H1 = 1` … `H6 = 6` (explicit discriminants, so `level as u8` is the level) | https://crates.io/api/v1/crates/pulldown-cmark/0.13.6 ; https://docs.rs/pulldown-cmark/0.13.6/pulldown_cmark/enum.Tag.html ; https://docs.rs/pulldown-cmark/0.13.6/pulldown_cmark/enum.HeadingLevel.html |
| `plist` 1.10.3: `default = ["serde"]`; `Value::from_reader<R: Read + Seek>` is not feature-gated (`src/value.rs:40`); `Value::as_dictionary` (`:211`), `as_string` (`:317`), `Dictionary::get` (`src/dictionary.rs:38`); only `Data`, `de`, `ser` are behind `serde` (`src/lib.rs:85-121`) | local registry copy `plist-1.10.3` |
| `windows-sys` 0.61.2: `RegCloseKey` (`Registry/mod.rs:2`), `RegGetValueW` (`:45`), `RegOpenKeyExW` (`:56`), `HKEY = *mut c_void` (`:147`), `HKEY_LOCAL_MACHINE` (`:153`), `KEY_READ` 131097 (`:169`), `KEY_WOW64_64KEY` 256 (`:172`), `RRF_RT_REG_SZ` 2 (`:1087`); `RtlGetVersion` in `Wdk/System/SystemServices/mod.rs:1472-1473`, itself gated on `Win32_System_SystemInformation`; `OSVERSIONINFOW` with `Default` (`SystemInformation/mod.rs:438-446`); `ERROR_FILE_NOT_FOUND` 2 (`Foundation/mod.rs:2220`), `ERROR_SUCCESS` 0 (`:3957`), `STATUS_SUCCESS` (`:8727`); features `Wdk_System_SystemServices` (`Cargo.toml:54`), `Win32_System_Registry` (`:234`), `Win32_System_SystemInformation` (`:248`) | local registry copy `windows-sys-0.61.2/src/Windows/…` |
| `tauri-plugin-opener` 2.7.2: crate root `pub use open::{open_path, open_url};`; `src/open.rs` `pub fn open_url<P: AsRef<str>, S: AsRef<str>>(url: P, with: Option<S>) -> crate::Result<()>`, which calls the private `open`, which calls `::open::that_detached` when `with` is `None` (no managed state, no `AppHandle`, no scope check). `pub fn init<R: Runtime>() -> TauriPlugin<R, Option<config::Config>>` is `Builder::default().build()`; `Builder::default()` sets `open_js_links_on_click: true`, and `build` registers `generate_handler![commands::open_url, commands::open_path, commands::reveal_item_in_dir]` and, with that flag, `js_init_script(include_str!("init-iife.js"))`. So registering the plugin adds a page script and three IPC commands; this plan does not register it (PD-20) | https://docs.rs/crate/tauri-plugin-opener/2.7.2/source/src/lib.rs ; https://docs.rs/crate/tauri-plugin-opener/2.7.2/source/src/open.rs (fetched 2026-10-31) |
| Rust std 1.99.1 (the `stable` channel of `rust-toolchain.toml`, `rustc 1.99.1 (58cf8f9c3 2026-10-13)` on this box), Windows: `DirEntry::file_type` builds `FileType::new(self.data.dwFileAttributes, /* reparse_tag = */ self.data.dwReserved0)` (`library/std/src/sys/fs/windows.rs:179-184`); `FileType::new` sets `is_symlink = is_reparse_point && reparse_tag & 0x20000000 != 0` and `is_dir()` is `!self.is_symlink && self.is_directory` (`:1187-1199`). Bit 29 is the reparse tag's name-surrogate bit; `IO_REPARSE_TAG_MOUNT_POINT` (junctions) = `0xA0000003` and `IO_REPARSE_TAG_SYMLINK` = `0xA000000C` both carry it (`windows-sys-0.61.2/src/Windows/Win32/System/SystemServices/mod.rs:2378`, `:2390`). So a junction or a directory symlink under a Windows root is not `is_dir()` and is not entered | https://raw.githubusercontent.com/rust-lang/rust/1.99.1/library/std/src/sys/fs/windows.rs ; https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-point-tags (bit N "name surrogate"); local `windows-sys-0.61.2` |
| `tauri` 2.12.5: `async_runtime::spawn_blocking<F, R>(func: F) -> JoinHandle<R>` with `F: FnOnce() -> R + Send + 'static` (`src/async_runtime.rs:309-318`); `JoinHandle<T>: Future<Output = crate::Result<T>>` (`:164-171`) | local registry copy `tauri-2.12.5` |
| `@tauri-apps/api` 2.12.4 `mockIPC`: `async function invoke(cmd, args) { … return cb(cmd, args); }`, so a handler returning a promise holds the call pending until it settles | `node_modules/@tauri-apps/api/mocks.js:87-132`, `package.json:3` |
| `url` 2.5.8: `port()` "Return the port number for this URL, if any"; default ports "are omitted if they match the well known port of a special URL"; `username()` empty when absent; `password()` `None` when absent. The page does not state host lower-casing; SC-032's `GitHub.com` case is the evidence (WHATWG host parsing), and the comparison is `eq_ignore_ascii_case` regardless (§4.6) | https://docs.rs/url/2.5.8/url/struct.Url.html |
| Spec's External facts table (RegOpenKeyExW / RegGetValueW semantics, `RtlGetVersion`, `pkgutil`, `sw_vers`, `confstr`, `cargo tree` flags, POSIX `ENOTDIR`, Inno `AppVersion` / `UsePreviousAppDir`) | `spec.md:137-157`, taken as verified there; this plan relies on no further claim |

---

## 2. File layout

New and changed files, all under `apps/krate-manager/` unless named otherwise.

```
crates/core/Cargo.toml                    + pulldown-cmark; macOS table + plist
crates/core/src/lib.rs                    + pub mod detect; pub mod notes; pub mod status;
crates/core/src/platform/mod.rs           + VENDOR_DIR, OS_VERSION_WAIT, HostIo, RunOutcome, RegistryRead,
                                            three trait Platform methods, re-exports of the FR-033 constants
crates/core/src/platform/windows.rs       + UNINSTALL_KEY, DISPLAY_VERSION, INSTALL_LOCATION; three methods
crates/core/src/platform/macos.rs         + PKGUTIL, PKG_INFO_FLAG, PKG_FILES_FLAG, SW_VERS, SW_VERS_FLAG, PKGUTIL_WAIT
crates/core/src/platform/linux.rs         + GETCONF, GLIBC_NAME
crates/core/src/roots.rs                  + pub(crate) fn check_sandboxed
crates/core/src/detect.rs                 new (FR-001 to FR-036)
crates/core/src/detect_tests.rs           new
crates/core/src/status.rs                 new (FR-040 to FR-049)
crates/core/src/status_tests.rs           new
crates/core/src/notes.rs                  new (FR-050 to FR-054)
crates/core/src/notes_tests.rs            new
crates/core/src/test_support.rs           + FakeHostFacts, FakeHostIo
crates/core/tests/injection.rs            guards (§4.12)
crates/core/tests/config_guard.rs         guards (§4.12)
crates/core/tests/roster.rs               install-identifier guard (§4.12)
src-tauri/Cargo.toml                      + tauri-plugin-opener in [dependencies]
src-tauri/build.rs                        five commands
src-tauri/capabilities/main.json          five permissions; description (SI-21)
src-tauri/src/lib.rs                      mod os_facts; five handlers (no opener plugin registered, PD-20)
src-tauri/src/commands.rs                 + plugin_status, check_now, open_release_page (tauri_plugin_opener::open_url)
src-tauri/src/os_facts.rs                 new: OsHostIo
Cargo.toml                                + pulldown-cmark, plist, tauri-plugin-opener pins; windows-sys 5 features
Cargo.lock                                regenerated additively (§4.1)
src/view.ts                               new: view types + toStatusView
src/status.ts                             new: STATUS_TEXTS, ACTION_TEXTS, section, cards, check control
src/notes.ts                              new: block renderer
src/status.test.ts                        new
src/main.ts, src/main.test.ts, src/styles.css   changed (main.test.ts per §4.10)
tests/fixtures/.gitattributes             new (PD-11)
tests/fixtures/detect/…                   new committed texts (§4.11)
tests/fixtures/status/<scenario>.json     13 new goldens
tests/required-tests.json                 + names (§4.14)
../../.github/workflows/manager-ci.yml    + step `core dependency tree`
../../tools/feed-contract/tests/manager-ci-shape.test.js   + that step in three lists
../../specs/Krate-manager-roadmap.md      Phase 6 status line; Phase 10 sentence (FR-092)
../../specs/INDEX.md                      regenerated
```

Nothing under `plugins/`, `dsp/`, `cmake/`, `extern/`, `tools/feed-contract/` contracts / schemas / fixtures,
`crates/helper`, `index.html`, `tauri*.conf.json`, `package.json`, `package-lock.json` or any other workflow (FR-062,
FR-065, FR-090).

---

## 3. Error model

Rust core returns values, never `Result` to the UI: detection failures become `CopyVersion::Unknown(reason)` or a
`record_error` code in the view; nothing in `detect` / `status` / `notes` panics on production `Roots` (the only panic
is FR-005's, on a sandboxed test `Roots`, and the `debug_assert!` of FR-001 for a context `fold` cannot produce).

| Code (line begins with it) | Level | Target | Raised by | Visible as |
|---|---|---|---|---|
| `E_DETECT_MODULEINFO` | warn | `krate_manager_core::detect` | `probe` metadata error other than `NotFound` (FR-004); moduleinfo `TooLarge`, `Unreadable`, `Corrupt` (FR-011, FR-012) | copy `unknown_reason` |
| `E_DETECT_CLASSES` | warn | detect | class versions disagree / unparseable (FR-013 b) | `classes_disagree` |
| `E_DETECT_TOP_LEVEL` | warn, or debug for `0.8.0` | detect | top-level vs class mismatch (FR-013 d) | log only |
| `E_DETECT_PLIST` | warn | detect | AU plist missing / unreadable / too large / corrupt (FR-020) | AU `unknown_reason` |
| `E_DETECT_LISTING_CAP` | warn | detect | a root hit `max_entries` (FR-006) | log only |
| `listing: <kind>: <n> entries examined` | debug | detect | every listed root (FR-006) | log only |
| `E_RECORD_READ` | warn | detect | `RegistryRead::Failed`; `pkgutil --files` `NoOutput` after a good `--pkg-info`; manifest read error other than `NotFound`, a manifest path that is not a regular file, or a manifest over 4 MiB (SI-18) | `record_error` |
| `E_RECORD_TIMEOUT` | warn | detect | a `pkgutil` run `Abandoned` | `record_error` |
| `E_RECORD_PARSE` | warn | detect | `--pkg-info` text without `package-id` = expected / `version`; manifest shape | `record_error` |
| `E_RECORD_FUTURE` | warn | detect | manifest `manifest_version` > 1 | `record_error` |
| `E_RECORD_MISMATCH` | warn | detect | manifest `plugin` ≠ entry name | `record_error` |
| `E_RECORD_LOCATION` | warn | detect | an `InstallLocation` that fails FR-003 (PD-6) | log only; record kept without location |
| `status: <n> plugins, <m> copies, feed <fresh\|cached\|none>` | info | `krate_manager_core::status` | once per `build` (FR-047) | log only |
| `E_STATUS_TASK: <join error>` | — | — | `plugin_status` / `check_now` when `spawn_blocking` fails | command rejection → feed line |
| `<StartupError Display>` (`E_STARTUP_…`) | — | — | `plugin_status` / `check_now` on an `Err` context | command rejection → feed line |
| `E_OPEN_REJECTED` | — | — | `open_release_page` with a URL that fails FR-049 | command rejection → card line |
| `E_OPEN_FAILED: <opener error>` | — | — | `open_release_page` when the opener fails | command rejection → card line |

Every warn line has the shape `<CODE> <plugin or path>: <reason>[: <version texts>]`. No line carries the content of a
moduleinfo, plist, receipt or manifest beyond the named version texts (FR-047): the reason is a fixed word
(`too large`, `not UTF-8`, `parse error at line L column C`, `root not an object`), and `serde_json::Error`'s
`Display` is never logged whole (it can quote input in some variants); only `line()` and `column()` are.

---

## 4. Module design

### 4.1 Manifests and lockfile

Root `Cargo.toml` `[workspace.dependencies]` (alphabetical, as today):

```toml
# Phase 6 D12: notes.rs only; no HTML writer
pulldown-cmark = { version = "0.13.6", default-features = false }
# Phase 6 D12: detect.rs AU reader only (macOS); already in the macOS graph through tauri
plist = { version = "1.10.3", default-features = false }
# Phase 6 D15: src-tauri commands.rs only (free fn open_url); never registered as a plugin; no opener:* permission
tauri-plugin-opener = "2.7.2"
# R-2; R-13, R-15; Phase 6 FR-081: exactly five features, no Win32_System_Time
windows-sys = { version = "0.61.2", features = ["Win32_Foundation", "Win32_System_Registry", "Win32_System_SystemInformation", "Wdk_System_SystemServices", "Win32_Networking_WinHttp"] }
```

`crates/core/Cargo.toml`: `[dependencies]` gains `pulldown-cmark = { workspace = true }`; the macOS table becomes
`core-foundation = { workspace = true }` and `plist = { workspace = true }` (comment: `# R-6: sysproxy_cf only;
Phase 6: detect.rs AU reader only`). Workspace inheritance keeps `default-features = false` from the root.

`src-tauri/Cargo.toml` `[dependencies]` gains `tauri-plugin-opener = { workspace = true }`; the Windows table stays
`windows-sys = { workspace = true }` (its comment gains `os_facts.rs`).

**Lockfile.** One non-locked `cargo check --workspace` from `apps/krate-manager/` after the manifest edits adds exactly
the new packages (`pulldown-cmark`, its `bitflags` / `memchr` / `unicase` if not present, `tauri-plugin-opener` and its
`open`, `dunce`, `glob`, `windows` 0.62, `zbus` on Linux, `objc2-*` on macOS as resolved); `plist` is already locked
(spec External facts). The diff of `Cargo.lock` is checked to add packages only: any changed `version =` line of an
existing package is reverted with `cargo update -p <name> --precise <old>` before anything else runs. All later commands
use `--locked`.

### 4.2 `platform/mod.rs`

```rust
use crate::detect::{InnoValues, PkgOutputs, RecordRead};
use crate::feed::PlainName;
use crate::version::OsVersion;
use std::time::Duration;

/// The vendor folder of the Windows Inno layout (FR-002); the only spelling outside the default roots.
pub const VENDOR_DIR: &str = "Krate Audio";
/// `sw_vers` / `getconf` bound (D8).
pub const OS_VERSION_WAIT: Duration = Duration::from_secs(2);

/// One bounded child run (FR-031). `NoOutput` folds spawn failure, non-zero exit and a read error (`run_bounded`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RunOutcome { Output(String), NoOutput, Abandoned }

/// Two values of one HKLM key, 64-bit view (FR-031).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RegistryRead { NoKey, Values([Option<String>; 2]), Failed }

/// The raw host primitives (D7 layer 1): no decision in any implementation.
pub trait HostIo {
    fn run(&self, argv: &[&str], bound: Duration) -> RunOutcome;
    fn registry_values(&self, subkey: &str, names: [&str; 2]) -> RegistryRead;
    fn rtl_version(&self) -> Option<(u32, u32, u32)>;
}

pub trait Platform {
    fn os(&self) -> Os;
    fn default_roots(&self, env: &OsEnv) -> Result<RootPaths, RootsError>;
    /// FR-030, FR-036.
    fn os_version(&self, io: &dyn HostIo) -> Option<OsVersion>;
    /// FR-030: Windows only; elsewhere `Absent` with no `io` call.
    fn inno_values(&self, io: &dyn HostIo, key: &PlainName) -> RecordRead<InnoValues>;
    /// FR-030: macOS only; elsewhere `Absent` with no `io` call.
    fn pkg_outputs(&self, io: &dyn HostIo, pkg_id: &PlainName) -> RecordRead<PkgOutputs>;
}

#[cfg(target_os = "windows")]
pub use windows::{DISPLAY_VERSION, ENV_KEYS, INSTALL_LOCATION, UNINSTALL_KEY, WindowsPlatform as NativePlatform};
#[cfg(target_os = "macos")]
pub use macos::{ENV_KEYS, MacosPlatform as NativePlatform, PKG_FILES_FLAG, PKG_INFO_FLAG, PKGUTIL, PKGUTIL_WAIT, SW_VERS, SW_VERS_FLAG};
#[cfg(target_os = "linux")]
pub use linux::{ENV_KEYS, GETCONF, GLIBC_NAME, LinuxPlatform as NativePlatform};
```

The only new string literal is `"Krate Audio"`, already allow-listed (`injection.rs:52`). `mod_tests.rs` is untouched.

### 4.3 `platform/{windows,macos,linux}.rs`

Constants exactly as FR-033 (`windows.rs`: `UNINSTALL_KEY = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"`,
`DISPLAY_VERSION`, `INSTALL_LOCATION`; `macos.rs`: `PKGUTIL = "/usr/sbin/pkgutil"`, `PKG_INFO_FLAG = "--pkg-info"`,
`PKG_FILES_FLAG = "--files"`, `SW_VERS = "/usr/bin/sw_vers"`, `SW_VERS_FLAG = "-productVersion"`; `linux.rs`:
`GETCONF = "/usr/bin/getconf"` (SI-19: an absolute path like macOS's tools, so `PATH` never picks the program),
`GLIBC_NAME = "GNU_LIBC_VERSION"`), each `pub`.

```rust
// windows.rs
fn os_version(&self, io: &dyn HostIo) -> Option<OsVersion> {
    io.rtl_version().and_then(|(major, minor, build)| detect::windows_version(major, minor, build))
}
fn inno_values(&self, io: &dyn HostIo, key: &PlainName) -> RecordRead<InnoValues> {
    let mut subkey = String::with_capacity(UNINSTALL_KEY.len() + 1 + key.as_str().len());
    subkey.push_str(UNINSTALL_KEY);
    subkey.push('\\'); // a char literal: not a string literal of this file (PD-4)
    subkey.push_str(key.as_str());
    detect::inno_read_from(io.registry_values(&subkey, [DISPLAY_VERSION, INSTALL_LOCATION]))
}
fn pkg_outputs(&self, _io: &dyn HostIo, _pkg_id: &PlainName) -> RecordRead<PkgOutputs> { RecordRead::Absent }

// macos.rs
fn pkg_outputs(&self, io: &dyn HostIo, pkg_id: &PlainName) -> RecordRead<PkgOutputs> {
    let id = pkg_id.as_str(); // its own argv element; matched ^com\.krateaudio\.[a-z0-9]+$ at feed parse
    detect::pkg_read_from(io.run(&[PKGUTIL, PKG_INFO_FLAG, id], PKGUTIL_WAIT),
                          || io.run(&[PKGUTIL, PKG_FILES_FLAG, id], PKGUTIL_WAIT))
}
fn os_version(&self, io: &dyn HostIo) -> Option<OsVersion> {
    detect::os_version_from_run(io.run(&[SW_VERS, SW_VERS_FLAG], OS_VERSION_WAIT), detect::parse_product_version)
}
fn inno_values(&self, _io: &dyn HostIo, _key: &PlainName) -> RecordRead<InnoValues> { RecordRead::Absent }

// linux.rs
fn os_version(&self, io: &dyn HostIo) -> Option<OsVersion> {
    detect::os_version_from_run(io.run(&[GETCONF, GLIBC_NAME], OS_VERSION_WAIT), detect::parse_glibc_version)
}
// inno_values, pkg_outputs → RecordRead::Absent with no io call
```

None of the three files gains an `FS_TOKENS` identifier, `Command`, `read_dir` or any FR-084 token.

### 4.4 `roots.rs`

```rust
/// FR-005: on a sandboxed (test) `Roots`, panics unless `path` is a strict descendant of the sandbox base; on a
/// production `Roots` (no sandbox) does nothing. Lexical, like `with_configured_plugin_root`.
pub(crate) fn check_sandboxed(&self, path: &Path) {
    if let Some(base) = &self.sandbox
        && !(path != base.as_path() && path.starts_with(base))
    {
        panic!("install location {} is outside the test sandbox {}", path.display(), base.display());
    }
}
```

Always compiled (the `sandbox` field always exists, `roots.rs:61`). The token `check_sandboxed` is not the token
`sandboxed`, so `RULE_SANDBOXED` (`injection.rs:1012-1014`) stays quiet in `detect.rs`.

### 4.5 `core::detect` (`crates/core/src/detect.rs`)

#### 4.5.1 Types and constants

```rust
pub const MAX_MODULEINFO_BYTES: u64 = 1024 * 1024;     // also the AU plist bound
pub const MAX_MANIFEST_BYTES: u64 = 4 * 1024 * 1024;
pub const MANIFESTS_DIR: &str = "manifests";
pub const LISTING_MAX_DEPTH: usize = 4;
pub const LISTING_MAX_ENTRIES: usize = 20_000;
pub(crate) const LOG_TARGET: &str = "krate_manager_core::detect";
const MAX_INSTALL_LOCATION_UNITS: usize = 32_767;

#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum CopyFormat { Vst3, Au }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Place { Root(RootKind), WindowsVendor(RootKind), InstallerLocation, Nested(RootKind) }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Candidate { pub format: CopyFormat, pub place: Place, pub path: PathBuf, pub managed: bool, pub rank: Option<u8> }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ListedCopy { pub root: RootKind, pub root_index: usize, pub path: PathBuf }   // PD-2
pub struct CandidateInputs<'a> {
    pub plugin_roots: &'a [PluginRoot],
    pub facts: &'a InstallFacts,
    pub windows_install_location: Option<&'a Path>,
    pub linux_managed_root: &'a Path,
    pub nested: &'a [ListedCopy],
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub enum VersionSource { Classes, TopLevel, AuPlist }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UnknownReason { SingleFile, NoModuleInfo, TooLarge, Unreadable, Corrupt, ClassesDisagree, NoVersion, NotABundle, AuNoVersion }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CopyVersion { Known { version: CoreVersion, source: VersionSource }, Unknown(UnknownReason) }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DetectedCopy { pub format: CopyFormat, pub place: Place, pub path: PathBuf, pub managed: bool, pub rank: Option<u8>, pub version: CopyVersion }

#[derive(Clone, Debug, PartialEq, Eq)] pub enum RecordRead<T> { Absent, Found(T), Failed(&'static str) }
#[derive(Clone, Debug, Default, PartialEq, Eq)] pub struct InnoValues { pub display_version: Option<String>, pub install_location: Option<String> }
#[derive(Clone, Debug, PartialEq, Eq)] pub struct PkgOutputs { pub pkg_info: String, pub files: String }
#[derive(Clone, Debug, PartialEq, Eq)] pub struct PkgInfo { pub version_text: String, pub location: Option<String> }
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum InstallerRecord {
    Inno { version_text: Option<String>, version: Option<CoreVersion>, install_location: Option<PathBuf> },
    Pkg { version_text: String, version: Option<CoreVersion>, location: Option<String>, files: u64 },
    Manifest { version_text: String, version: Option<CoreVersion> },
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Detection {
    NotChecked,
    Checked { copies: Vec<DetectedCopy>, record: Option<InstallerRecord>, record_error: Option<&'static str>, managed_rank: Option<u8> }, // SI-13
}

/// D7 layer 3: what detection and status consume.
pub trait HostFacts {
    fn os_version(&self) -> Option<OsVersion>;
    fn inno_values(&self, key: &PlainName) -> RecordRead<InnoValues>;
    fn pkg_outputs(&self, pkg_id: &PlainName) -> RecordRead<PkgOutputs>;
}
/// The one production `HostFacts`: delegates to `NativePlatform` with `io` (FR-025).
pub struct PlatformFacts<'a> { pub io: &'a dyn HostIo }
impl HostFacts for PlatformFacts<'_> { /* three one-line delegations to NativePlatform.<m>(self.io, …) */ }
```

The error codes are `pub(crate) const E_…: &str` in `detect.rs` (FR-030's `Failed(&'static str)` carries them).

#### 4.5.2 `candidates` and `same_path` (FR-001, FR-006 dedupe, PD-1)

```rust
pub fn candidates(os: Os, inputs: &CandidateInputs<'_>) -> Vec<Candidate>;
pub fn same_path(a: &Path, b: &Path) -> bool;   // component count equal, each pair eq_ignore_ascii_case on encoded bytes
```

Algorithm, `bundle = facts.vst3_bundle.as_str()`, `stem = bundle.strip_suffix(".vst3")` (the C1 pattern guarantees
the suffix; `unwrap_or(bundle)` keeps it total):

1. `rank_of(index)`: walk `plugin_roots[..=index]`; `Vst3User` → `Some(0)`; the k-th `Vst3Global` (1-based, counting
   only `Vst3Global` entries up to and including `index`) → `Some(k)` (saturating to `u8::MAX`); `Configured`, AU → `None`.
2. Fixed candidates in `plugin_roots` order:
   - Windows, roots of kind `Vst3User` / `Vst3Global`: `<root>.join(bundle)` (`Root(kind)`), then
     `<root>.join(VENDOR_DIR).join(stem).join(bundle)` (`WindowsVendor(kind)`), both with `rank_of(i)`. Then, for
     `Some(loc)`, `target = loc.join(bundle)`: if no earlier candidate is `same_path(target)`, push
     `InstallerLocation` with `rank: None`. Managed: with `Some(loc)` the candidate `same_path` to `target`; without,
     the `WindowsVendor(Vst3Global)` candidate of the first `Vst3Global` root.
   - macOS: VST3 `<root>.join(bundle)` for `Vst3User` / `Vst3Global` roots; then, when `au_component` is `Some(c)`,
     `<root>.join(c)` for `AuUser` / `AuGlobal` roots (`format: Au`, `rank: None`). Managed: the first `Vst3Global`
     VST3 candidate and the first `AuGlobal` AU candidate.
   - Linux: `<root>.join(bundle)` for `Configured`, `Vst3User`, `Vst3Global`. Managed: the candidate whose root path
     `==` `linux_managed_root`; none matching → `debug_assert!(false, …)`, no managed candidate in release.
3. Nested: for each `ListedCopy` in order, skip it if it equals (`same_path` on Windows, `==` elsewhere) any fixed
   candidate path or an already appended nested path; else push `Candidate { format: Vst3, place: Nested(root),
   path, managed: false, rank: rank_of(root_index) }`.

`same_path` compares `a.components()` and `b.components()` pairwise with
`x.as_os_str().as_encoded_bytes().eq_ignore_ascii_case(y.as_os_str().as_encoded_bytes())`, after checking equal
counts; a `Prefix` component compares the same way, so `\\?\C:` and `\\?\c:` match.

#### 4.5.3 `usable_install_location` (FR-003)

```rust
pub fn usable_install_location(text: &str) -> Option<PathBuf>
```
`text.contains('\0')` → `None`; `text.encode_utf16().count() > 32_767` → `None`; trim trailing `'\\'` and `'/'`;
`Path::new(trimmed)`: `is_absolute()`, at least one `Component::Normal`, no `Component::ParentDir`; else `None`.
On a non-Windows host a `C:\…` value is not absolute (`Path` is host semantics) and is refused, which is harmless:
production only calls it on Windows, and tests build host-native locations (SC-007).

#### 4.5.4 The listing (FR-006, PD-3, PD-17)

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)] pub struct ListingLimits { pub max_depth: usize, pub max_entries: usize }
impl ListingLimits { pub const PRODUCTION: ListingLimits = ListingLimits { max_depth: LISTING_MAX_DEPTH, max_entries: LISTING_MAX_ENTRIES }; }
#[derive(Clone, Debug, Default)] pub struct Listing { matches: Vec<ListedMatch> }
#[derive(Clone, Debug)] struct ListedMatch { root: RootKind, root_index: usize, name: OsString, path: PathBuf }
impl Listing {
    pub fn scan(roots: &[PluginRoot], names: &[&str], limits: ListingLimits) -> Listing;
    /// This plugin's matches in scan order (root order, then breadth-first, name order): every stored match with
    /// `name_matches(&m.name, bundle)`, the same rule `scan` used (PD-3), never an exact compare of the on-disk name.
    pub fn copies_of(&self, bundle: &str) -> Vec<ListedCopy>;
    #[cfg(test)] pub(crate) fn paths(&self) -> Vec<&Path>;
}
fn list_root(index: usize, root: &PluginRoot, names: &[&str], limits: ListingLimits, out: &mut Vec<ListedMatch>);
fn name_matches(name: &OsStr, wanted: &str) -> bool; // cfg!(windows): eq_ignore_ascii_case on encoded bytes; else ==
```

`list_root` (the one `read_dir` of the phase): a `VecDeque<(PathBuf, depth)>` seeded with `(root.path, 0)`;
`examined = 0`. Pop a directory; `std::fs::read_dir(dir)`: error → continue (no log); collect `Ok` entries (an `Err`
item is skipped), sort by `file_name().as_encoded_bytes()`. For each entry: if `examined == max_entries` → set capped,
stop the root; `examined += 1`; `depth' = depth + 1`; if its name matches one of `names` → push a `ListedMatch` and do
not enter it; else if `depth' < max_depth` and `entry.file_type()` is `Ok(t)` with `t.is_dir()` (a symlink's
`DirEntry::file_type` is the link's own type, so `is_dir()` is false and the link is not entered; on Windows std's
`is_dir()` is false for every name-surrogate reparse point, junctions and directory symlinks alike, §1.2) → enqueue
`(path, depth')`. After the root: one `debug!` `listing: <kind:?>: <examined> entries examined`; when capped one
`warn!` `E_DETECT_LISTING_CAP <kind:?>: stopped at <max_entries> entries, depth <deepest dequeued depth>`. A depth-4
match is reported (it sits in a depth-3 directory, which is entered since 3 < 4); nothing at depth 5 is read.
Roots of kind `AuUser` / `AuGlobal` are skipped. `names` is de-duplicated by the caller.

#### 4.5.5 `probe` and the readers (FR-004, FR-010 to FR-013, FR-020)

```rust
pub fn probe(candidate: &Candidate) -> Option<DetectedCopy>;
fn read_bounded(path: &Path, max: u64) -> Result<Vec<u8>, ReadFail>;   // metadata + is_file, then File::open + take(max + 1) + read_to_end (PD-22)
enum ReadFail { NotFound, TooLarge, Io }
pub fn strip_trailing_commas(text: &str) -> Cow<'_, str>;
fn moduleinfo_version(text: &str, path: &Path) -> CopyVersion;          // FR-012 parse + FR-013 rule + logs
#[cfg(target_os = "macos")] fn read_au_plist(bundle: &Path) -> CopyVersion;
```

- `probe`: `std::fs::metadata(&c.path)`: `Err(NotFound)` → `None`; other `Err` → copy `Unknown(Unreadable)` + one warn;
  `Ok(m)`: VST3: `m.is_file()` → `SingleFile`; directory → read `Contents/Resources/moduleinfo.json`. AU (macOS):
  file → `NotABundle`; directory → `read_au_plist`. AU off macOS → `None` (PD-7). Any other file type (a socket) →
  `Unknown(Unreadable)` + warn.
- `read_bounded` first calls `std::fs::metadata(path)` (follows symlinks, FR-004): `NotFound` → `ReadFail::NotFound`,
  any other error → `Io`, a result that is not `is_file()` (directory, FIFO, socket, device) → `Io`, so a FIFO or tty is
  never opened and cannot block the build (PD-22). Then `File::open` (std's Windows default share mode allows readers
  and writers, so only an exclusive lock such as SC-016's `share_mode(0)` makes it fail) and `Read::take(max + 1)`;
  more than `max` bytes → `TooLarge`. No `OpenOptions` (FR-084). The window between `metadata` and `open` is not
  closed (a file swapped for a FIFO in that instant would still block); nothing in a plug-in folder does that, and
  closing it needs `O_NONBLOCK`, i.e. `OpenOptions`, which FR-084 bans in `detect.rs`.
- moduleinfo: `NotFound` → `NoModuleInfo` (no log); `TooLarge`; `Io` → `Unreadable`; `String::from_utf8` failure →
  `Corrupt`; skip one leading U+FEFF.
- `strip_trailing_commas`: one pass over chars with states `Normal`, `InString`, `Escape`, tracking the last
  non-whitespace char seen outside strings. In `Normal`, a `,` is dropped when (i) the next non-whitespace char (JSON
  whitespace: space, `\t`, `\n`, `\r`) is `}` or `]` and (ii) the last non-whitespace char before it is not `[`, `{`
  or `,`; otherwise it is kept. Rule (ii) keeps `[,]` and `{,}` invalid, so they still parse as `Corrupt` (SC-004);
  it is FR-012's added clause (SI-16), so spec and code say the same;
  together the two rules are exactly "a comma after an element and before the closing bracket", the SDK writer's
  shape (`moduleinfocreator.cpp:48-53`). `"` toggles `InString`; `\` in a string → `Escape` (the next char is kept
  verbatim). Returns `Cow::Borrowed` when nothing was dropped.
- `moduleinfo_version`: `serde_json::from_str::<Value>`; error or non-object → `Corrupt` + warn (line/column only).
  FR-013 (a)-(d) as written; class texts joined by `, ` in the `E_DETECT_CLASSES` line; a non-string class `Version`
  is shown as `<non-string>`.
- `read_au_plist`: `read_bounded(Contents/Info.plist, 1 MiB)`: `NotFound` / `Io` → `Unreadable` + warn; `TooLarge`
  + warn; `plist::Value::from_reader(Cursor::new(bytes))` error or non-dictionary root → `Corrupt` + warn;
  `CFBundleShortVersionString` string accepted by `CoreVersion::parse` → `Known { AuPlist }`; else `AuNoVersion`.

#### 4.5.6 Records, mappers, OS-version parsers (FR-026 to FR-035)

```rust
pub fn inno_record(values: &InnoValues) -> InstallerRecord;                         // pure (PD-6)
pub fn parse_pkg_info(text: &str, expected: &PlainName) -> Result<PkgInfo, &'static str>;
pub fn count_pkg_files(text: &str) -> u64;                                          // non-empty lines (trim_end '\r')
pub fn pkg_record(outputs: &PkgOutputs, expected: &PlainName) -> Result<InstallerRecord, &'static str>;
pub fn read_manifest(state_dir: &Path, stem: &str, plugin: &str) -> RecordRead<InstallerRecord>;
pub fn inno_read_from(read: RegistryRead) -> RecordRead<InnoValues>;
pub fn pkg_read_from(info: RunOutcome, files: impl FnOnce() -> RunOutcome) -> RecordRead<PkgOutputs>;
pub fn os_version_from_run(run: RunOutcome, parse: fn(&str) -> Option<OsVersion>) -> Option<OsVersion>;
pub fn parse_product_version(text: &str) -> Option<OsVersion>;
pub fn parse_glibc_version(text: &str) -> Option<OsVersion>;
pub fn windows_version(major: u32, minor: u32, build: u32) -> Option<OsVersion>;    // OsVersion::parse(&format!(..))
```

`parse_pkg_info` splits each line at the first `": "`; `package-id` must equal `expected.as_str()`, `version` must be
present (its text kept, `CoreVersion::parse` for `version`), `location` kept, others ignored. `read_manifest`:
`<state_dir>/manifests/<stem>.json`, `read_bounded(…, 4 MiB)`: `NotFound` → `Absent`; `TooLarge` / `Io` (a read
error, or a path that is not a regular file) → `Failed(E_RECORD_READ)` (FR-028 as amended by SI-18); then JSON object with `manifest_version` integer 1 / `plugin` / `version` string → `Found`;
integer > 1 → `Failed(E_RECORD_FUTURE)`; `plugin` another string → `Failed(E_RECORD_MISMATCH)`; anything else →
`Failed(E_RECORD_PARSE)`. The other mappers exactly as FR-034 / FR-035.

#### 4.5.7 `detect_plugin` (FR-029, FR-042)

```rust
pub fn detect_plugin(os: Os, ctx: &AppContext, entry: &PluginEntry, host: &dyn HostFacts, listing: &Listing) -> Detection
```
1. `facts = entry.release?.install?` else `NotChecked`.
2. Record, by `os`: Windows with `facts.windows_uninstall_key` `Some(k)` → `host.inno_values(k)`; macOS with
   `macos_pkg_id` `Some(id)` → `host.pkg_outputs(id)` then `pkg_record`; Linux → `read_manifest(ctx.roots.state_dir(),
   stem, &entry.name)`. `Failed(code)` / parse `Err(code)` → `record_error = Some(code)` + one warn
   `<code> <plugin>: installer record`. Windows: when the raw `InstallLocation` is `Some` and the record's is `None`,
   one warn `E_RECORD_LOCATION` (PD-6).
3. `loc = record.install_location` (Windows only); `ctx.roots.check_sandboxed(loc)` before anything else (FR-005).
4. `linux_managed_root = ctx.settings.linux_plugin_root_or_default(&ctx.roots)`;
   `nested = listing.copies_of(facts.vst3_bundle.as_str())`; `candidates(os, …)`; `probe` each in order.
5. `Checked { copies, record, record_error, managed_rank }`, `managed_rank` = the managed VST3 candidate's `rank`
   (`None` when there is no managed candidate or it is an `InstallerLocation` / `Configured` one).

### 4.6 `core::status` (`crates/core/src/status.rs`)

```rust
pub const RELEASE_PAGE_HOST: &str = "github.com";
pub const MAX_RELEASE_PAGE_URL_BYTES: usize = 2048;
pub(crate) const LOG_TARGET: &str = "krate_manager_core::status";

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct StatusView { pub os: OsName, pub edge_max_age_secs: u64, pub running_os: Option<String>, pub feed: FeedView, pub plugins: Vec<PluginView> }
#[derive(…, Serialize)] #[serde(rename_all = "snake_case")] pub enum OsName { Windows, Macos, Linux }
#[derive(…, Serialize)] pub struct FeedView { pub source: FeedSource, pub fetched_at_unix: Option<u64>, pub schema_version: Option<u8>, pub error: Option<String> }
#[derive(…, Serialize)] #[serde(rename_all = "snake_case")] pub enum FeedSource { Fresh, Cached, None }
#[derive(…, Serialize)]
pub struct PluginView {
    pub name: String, pub latest: String, pub availability: AvailabilityView, pub min_os: Option<String>,
    pub installed: InstalledView, pub installed_version: Option<String>, pub update: Option<UpdateView>,
    pub copies: Vec<CopyView>, pub record: Option<RecordView>, pub record_error: Option<String>,
    pub record_without_files: bool, pub record_version_differs: bool, pub release_page_url: Option<String>,
    pub notes: Vec<notes::Block>,
}
// AvailabilityView { Available, NotThroughManager, NotForThisMachine, NeedsNewerOs }
// InstalledView { NotChecked, NotInstalled, Elsewhere, VersionUnknown, Version }
// UpdateView { NotInstalled, UpToDate, UpdateAvailable, InstalledNewer, Unknown }
// CopyView { format: FormatView, place: PlaceView, vendor_folder, nested, path, managed, loads_before_managed, version,
//            version_source: Option<VersionSourceView>, unknown_reason: Option<UnknownReasonView> }
// FormatView { Vst3, Au }; VersionSourceView { Classes, TopLevel, AuPlist }
// UnknownReasonView { SingleFile, NoModuleinfo, TooLarge, Unreadable, Corrupt, ClassesDisagree, NoVersion, NotABundle, AuNoVersion }
//   (PD-21: `NoModuleinfo`, not `NoModuleInfo`, so `rename_all = "snake_case"` yields FR-046's `no_moduleinfo`;
//    mapped from `detect::UnknownReason` by an exhaustive `match`, so a new core reason fails to compile)
// PlaceView { Configured, Vst3User, Vst3Global, AuUser, AuGlobal, InstallerLocation }
// RecordView { kind: RecordKind { Inno, Pkg, Manifest }, version: Option<String>, files: Option<u64> }

pub fn build(ctx: &AppContext, mode: FetchMode, now_unix: u64, host: &dyn HostFacts) -> StatusView;
pub fn assemble(os: Os, arch: Arch, check: &FeedCheck, running: Option<OsVersion>, detections: &BTreeMap<String, Detection>) -> StatusView;
pub fn release_page_url(text: &str) -> Option<String>;
```

Field order of every struct is the FR-046 key order, so `to_string_pretty` produces the golden layout; every `Option`
serialises as `null` (no `skip_serializing_if`), every `Vec` always present. Every view enum carries
`#[serde(rename_all = "snake_case")]` and no `rename` (FR-084 bans that identifier in `status.rs`); the variant names
are chosen so the rule gives the FR-046 strings (PD-21), and `golden_views` pins every one of them against literals.

- **`build`**: `check = feed::check(ctx, mode, now_unix)`; `running = host.os_version()`; when a snapshot exists:
  `names` = the de-duplicated `vst3_bundle` strings of entries with install facts, `listing = Listing::scan(
  ctx.roots.plugin_roots(), &names, ListingLimits::PRODUCTION)` (once), then `detect_plugin(NativePlatform.os(), …)`
  per entry in feed order on the calling thread; `assemble(NativePlatform.os(), Arch::native(), &check, running,
  &detections)`; one `info!`. No lock, no write (the cache write is Phase 5's).
- **`assemble`** per entry: `availability = version::resolve(entry, os, arch, running.as_ref())`; `min_os` set only
  for `NeedsNewerOs`; `detection = detections.get(name)` (absent → `NotChecked`). For `Checked`: the managed VST3 copy
  is the copy with `format: Vst3, managed: true`; FR-043 / FR-044 exactly, where `loads_before_managed` compares a
  copy's rank with `Detection::Checked.managed_rank` (below). `update = None` for `NotChecked`. `release_page_url =
  entry.release.as_ref().and_then(|r| release_page_url(&r.release_notes_url))`. `notes =
  notes::blocks(notes_markdown)`, or `[]` when absent.
- **`managed_rank`.** FR-044 ranks a copy against the managed *location*, which has a rank whether or not a copy sits
  there (SC-006's Bravo row: rank 1 with no managed copy). `assemble` sees only probed copies, so
  `Detection::Checked` carries a fourth field `managed_rank: Option<u8>`, which `detect_plugin` copies from the managed
  VST3 **candidate** (SI-13).
- **`release_page_url`**: `text.len() <= 2048`; `Url::parse(text)`; `scheme() == "https"`; `host_str()` is `Some(h)`
  with `h.eq_ignore_ascii_case(RELEASE_PAGE_HOST)`; `username().is_empty()`, `password().is_none()`, `port().is_none()`
  → `Some(url.as_str().to_owned())` (the parser lower-cases the host; the case check is belt and braces, §1.2).

### 4.7 `core::notes` (`crates/core/src/notes.rs`)

```rust
pub const MAX_NOTES_BYTES: usize = 65_536;
pub const MAX_DEPTH: usize = 8;
pub const TOO_LONG: &str = "Release notes are too long to show here.";

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Block {
    Heading { level: u8, inlines: Vec<Inline> },
    Paragraph { inlines: Vec<Inline> },
    List { ordered: bool, start: Option<u64>, items: Vec<Vec<Block>> },
    Quote { blocks: Vec<Block> },
    Code { text: String },
    Rule,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Inline { Text { text: String }, Strong { inlines: Vec<Inline> }, Emphasis { inlines: Vec<Inline> }, Code { text: String }, Break }

pub fn blocks(markdown: &str) -> Vec<Block>;
```

Builder: a stack of block frames (`Root`, `List { ordered, start, items }`, `Item(Vec<Block>)`, `Quote(Vec<Block>)`,
`Heading(level)`, `Paragraph`, `Code(String)`, `Html(String)`) and, inside a paragraph or heading, a stack of inline
frames (`Strong`, `Emphasis`). `Start(Tag::List(n))` / `BlockQuote` increments the block depth; at depth 9 it starts
an overflow collector (PD-8) that swallows events until the matching end. Inline content arriving in an `Item` with no
open paragraph opens an implicit `Paragraph` (tight list), closed at the next block start or the item end. `Link` and
`Image` push nothing (their children flow into the parent: the destination is never read). `SoftBreak` → `Text " "`,
`HardBreak` → `Break`; `Html` / `InlineHtml` → literal `Text`; any other event's text (`InlineMath`, `DisplayMath`,
`FootnoteReference`, `TaskListMarker` are not produced under `Options::empty()`) → `Text`. Adjacent `Text`s merge when an
inline vector is closed. `List(Some(1))` → `start: None`; `List(Some(n))` → `Some(n)`. Over 65,536 bytes → `[Paragraph
[Text TOO_LONG]]` without parsing; empty after `trim()` → `[]`.

### 4.8 `test_support.rs` fakes (FR-083)

```rust
#[derive(Default)]
pub struct FakeHostFacts {
    pub os_version: Option<OsVersion>,
    pub inno: BTreeMap<String, RecordRead<InnoValues>>,
    pub pkg: BTreeMap<String, RecordRead<PkgOutputs>>,
    pub calls: RefCell<Vec<String>>,          // "os_version", "inno_values <key>", "pkg_outputs <id>"
}
impl HostFacts for FakeHostFacts { /* record the call; unlisted key → Absent */ }

#[derive(Default)]
pub struct FakeHostIo {
    pub runs: BTreeMap<Vec<String>, RunOutcome>,
    pub registry: BTreeMap<String, RegistryRead>,
    pub rtl: Option<(u32, u32, u32)>,
    pub calls: RefCell<Vec<String>>,          // "run <argv joined by ' '>", "registry <subkey> <n1> <n2>", "rtl_version"
}
impl HostIo for FakeHostIo { /* record; unlisted argv → NoOutput; unlisted subkey → NoKey */ }
```

Neither starts a process or touches the registry; `RefCell` keeps each fake per thread (SC-018's concurrent test gives
each thread its own fake).

### 4.9 `src-tauri`

**`os_facts.rs`** (glue only, FR-032):

```rust
//! The raw host primitives behind `HostIo` (FR-032). No decision lives here: every argv, subkey and mapping is core's.
use krate_manager_core::platform::{HostIo, RegistryRead, RunOutcome};
use krate_manager_core::sysproxy;
use std::process::Command;
use std::time::Duration;

pub struct OsHostIo;

impl HostIo for OsHostIo {
    fn run(&self, argv: &[&str], bound: Duration) -> RunOutcome {
        let Some((program, args)) = argv.split_first() else { return RunOutcome::NoOutput };
        let mut command = Command::new(program);
        command.args(args);
        let run = sysproxy::run_bounded(vec![command], bound);
        match (run.outputs, run.abandoned.is_empty()) {
            (Some(mut outputs), _) if outputs.len() == 1 => RunOutcome::Output(outputs.remove(0)),
            (_, false) => RunOutcome::Abandoned,
            _ => RunOutcome::NoOutput,
        }
    }
    #[cfg(windows)] fn registry_values(&self, subkey: &str, names: [&str; 2]) -> RegistryRead { windows::registry_values(subkey, names) }
    #[cfg(not(windows))] fn registry_values(&self, _: &str, _: [&str; 2]) -> RegistryRead { RegistryRead::Failed }
    #[cfg(windows)] fn rtl_version(&self) -> Option<(u32, u32, u32)> { windows::rtl_version() }
    #[cfg(not(windows))] fn rtl_version(&self) -> Option<(u32, u32, u32)> { None }
}
```

The Windows submodule (`#[cfg(windows)] mod windows`) uses `windows_sys::Win32::System::Registry::{RegOpenKeyExW,
RegGetValueW, RegCloseKey, HKEY, HKEY_LOCAL_MACHINE, KEY_READ, KEY_WOW64_64KEY, RRF_RT_REG_SZ}`,
`Win32::Foundation::{ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, STATUS_SUCCESS}`,
`Wdk::System::SystemServices::RtlGetVersion`, `Win32::System::SystemInformation::OSVERSIONINFOW`:

- `wide(s) = s.encode_utf16().chain(Some(0)).collect::<Vec<u16>>()`.
- Open: `RegOpenKeyExW(HKEY_LOCAL_MACHINE, wide(subkey).as_ptr(), 0, KEY_READ | KEY_WOW64_64KEY, &mut hkey)`;
  `ERROR_FILE_NOT_FOUND` → `NoKey`; other ≠ `ERROR_SUCCESS` → `Failed`.
- Per name: size query `RegGetValueW(hkey, null(), name, RRF_RT_REG_SZ, null_mut(), null_mut(), &mut size)`;
  `ERROR_FILE_NOT_FOUND` → `None`; other error → `Failed` (after close); then `vec![0u16; size.div_ceil(2)]` and the
  read with the same size; error (including a value that grew, `ERROR_MORE_DATA`) → `Failed`; the data up to the first
  NUL through `String::from_utf16_lossy`.
- `RegCloseKey(hkey)` on every path after a successful open (a small guard struct with `Drop`).
- `rtl_version`: `let mut info = OSVERSIONINFOW { dwOSVersionInfoSize: size_of::<OSVERSIONINFOW>() as u32,
  ..Default::default() }`; `RtlGetVersion(&mut info) == STATUS_SUCCESS` → `Some((major, minor, build))`.
- Each `unsafe` block carries a `// SAFETY:` line like `os_proxy.rs` (`:86-89`). No `RegSet*`, `RegCreate*`,
  `RegDelete*` is named.

**`commands.rs`**:

```rust
#[tauri::command]
pub async fn plugin_status(state: tauri::State<'_, SharedState>) -> Result<StatusView, String> { status_for(&state, FetchMode::Normal).await }
#[tauri::command]
pub async fn check_now(state: tauri::State<'_, SharedState>) -> Result<StatusView, String> { status_for(&state, FetchMode::CheckNow).await }

async fn status_for(state: &SharedState, mode: FetchMode) -> Result<StatusView, String> {
    let ctx = {
        let guard = state.read().unwrap_or_else(PoisonError::into_inner);
        guard.context.clone().map_err(|e| e.to_string())?
    }; // the guard is dropped before any await
    let now_unix = SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |d| d.as_secs());
    tauri::async_runtime::spawn_blocking(move || status::build(&ctx, mode, now_unix, &PlatformFacts { io: &OsHostIo }))
        .await
        .map_err(|e| format!("E_STATUS_TASK: {e}"))
}

#[tauri::command]
pub fn open_release_page(url: String) -> Result<(), String> {
    let Some(valid) = status::release_page_url(&url) else { return Err("E_OPEN_REJECTED".to_owned()) };
    tauri_plugin_opener::open_url(valid, None::<&str>).map_err(|e| format!("E_OPEN_FAILED: {e}"))
}
```
The crate-root free function (PD-20, SI-15): no `OpenerExt`, no `AppHandle` parameter, no plugin state. A sync
command runs on the main thread; `open_url` hands off to `::open::that_detached` and returns, so it does not block
the UI.

**`lib.rs`**: `mod os_facts;`; the builder is unchanged (`.plugin(navigation::guard())` only, **no**
`tauri_plugin_opener::init()`, so no opener page script and no opener IPC command exists, PD-20);
`generate_handler![commands::app_version, commands::startup_error, commands::plugin_status, commands::check_now,
commands::open_release_page]`. **`build.rs`**: `commands(&["app_version", "startup_error", "plugin_status",
"check_now", "open_release_page"])`. **`capabilities/main.json`**: `"permissions": ["allow-app-version",
"allow-startup-error", "allow-plugin-status", "allow-check-now", "allow-open-release-page"]`; the description becomes
exactly "The main window may call only the app's own commands." (it can no longer say read-only; FR-061 as amended by
SI-21, compared exactly by `real_src_tauri_config_passes_the_guard`).

### 4.10 Frontend

**`src/view.ts`**: the FR-046 / FR-051 types as string-literal unions and `readonly` interfaces, plus

```ts
export function toStatusView(value: unknown): StatusView; // throws TypeError("<json path>: <expected>") on any mismatch
```
(object keys checked exactly: a missing or an extra key fails; arrays checked per element; blocks and inlines by
`type`).

**`src/status.ts`**:

```ts
export const STATUS_TEXTS = { /* PD-14: every FR-071 cell, the FR-074 feed lines, "Check now", the note */ } as const satisfies Record<string, string>;
export const ACTION_TEXTS = { pending: "Checking for plugins…", checking: "Checking…", openFailed: "Could not open the release page: {text}" } as const;
export function fill(template: string, values: Readonly<Record<string, string>>): string;
export type Invoker = <T>(cmd: string, args?: Record<string, unknown>) => Promise<T>;
export interface StatusSection { readonly element: HTMLElement; readonly button: HTMLButtonElement; render(view: StatusView): void; reject(text: string): void; }
export function createStatusSection(invoke: Invoker): StatusSection;   // pending text, disabled button, click → check_now
export function renderCards(view: StatusView, invoke: Invoker): HTMLElement[];
export async function mountStatus(root: HTMLElement, invoke: Invoker): Promise<void>; // append, plugin_status, render|reject, enable
```

DOM: `section.status > p.feed-line, div.check > (button.check-now, p.check-note), div.cards > article.plugin*`; a card
is `h2`, `p.installed`, `p.latest`, `p.state`, `ul.copies > li*`, `p.record-notice*`, optional `button.open-release`,
optional `p.open-error`, `details.notes > summary + blocks`. Every string through `textContent`; the only properties
set from data are `ol.start` and `button.disabled`; the release URL lives in the click closure. `Check now` click:
ignored while pending; otherwise disable, text `Checking…`, `invoke("check_now")`, `render` or `reject` (PD-13), text
`Check now`, enable. Template literals with numbers use `String(n)` (`restrict-template-expressions`).

**`src/notes.ts`**: `export function renderBlocks(blocks: readonly Block[]): Node[]` per FR-072 (`h{min(L+2,6)}`,
`p`, `ul`/`ol`, `li`, `blockquote`, `pre > code`, `hr`, `strong`, `em`, `code`, `br`), with a `switch` on `type`
whose `default` assigns the value to a `never`-typed constant, so a new block or inline type fails `tsc`. The
`never` default catches a missing arm, not a wrong one; the mapping itself is pinned by `renders notes-rendering`
(§7.1 row 019): `ol.start` is set as a property only when `start` is not `null`; `notes: []` renders one `p` reading
`No release notes.` after the `summary`.

**`src/main.ts`**: after the version line, `await mountStatus(root, invoke)`; the locked path returns before it
(FR-070). **`src/styles.css`**: rules for `.status`, `.plugin`, `.copies`, `.record-notice`, `details.notes`; no
font, image or URL (FR-077), enforced by the CSS rule of `check_frontend` (§4.12).

**`src/main.test.ts`, the four Phase 4 cases (FR-070).** Each mock throws on an unexpected command today
(`main.test.ts:7-70`), so each must say how it treats `plugin_status`:
- `shows the title and the version` and `shows the version when startup_error fails`: the mock answers
  `plugin_status` with `toStatusView` of the `no-feed` golden (the smallest view, no cards); expected calls
  `["startup_error", "app_version", "plugin_status"]`; the existing assertions stay, plus `section.status` present.
- `shows "version unavailable" when app_version fails`: its mock already throws for every command but
  `startup_error`, so `plugin_status` rejects; expected calls `["startup_error", "app_version", "plugin_status"]`,
  plus the feed line reads `Could not load the plugin list: ` followed by the rejection text (FR-074).
- `shows only the notice when another instance is running`: unchanged, expected calls `["startup_error"]`, and no
  `section.status` exists.
The three required titles keep their names (FR-070); the fourth, not required, keeps its name too.

### 4.11 Fixtures

- `tests/fixtures/.gitattributes`: `detect/** -text`, `status/** -text` (PD-11).
- `tests/fixtures/detect/moduleinfo/`: `current.json` (Iterum-shaped, top `1.2.0`, two classes `1.2.0.1`, trailing
  commas as `Iterum.vst3/…/moduleinfo.json:12-13`), `iterum-shape.json` (top `1.0.0`, classes `1.0.0.1`, SC-004),
  `legacy-080.json`, `top-103-class-104.json`, `real-080.json`, `classes-disagree.json`, `class-x.json`,
  `no-class-top-123.json`, `no-version.json`, `truncated.json`; each holds `"KMContent": "KM-CONTENT-MARKER"` in an
  ignored key (before the cut in `truncated.json`). The 1 MiB + 1 file is generated at test time (never committed).
- `tests/fixtures/detect/plist/`: `template.plist` (`CFBundleVersion` `1.0`, `AudioComponents[0].version` 1, no
  short version, `KMContentMarker`), `short-120.plist`, the binary form of `short-120.plist` (not committed: the macOS test
  writes it at test time with `plist::Value::to_writer_binary`, `plist-1.10.3/src/value.rs:76`),
  `short-12.plist`, `short-1234.plist`, `corrupt.plist` (with the marker).
- `tests/fixtures/detect/pkgutil/`: `alpha-pkg-info.txt` (`package-id: com.krateaudio.alpha`, `version: 1.2.0`,
  `volume: /`, `location: /`, `install-time: 1720421876`, `KM-CONTENT-MARKER: x`), `alpha-files.txt` (3 lines).
- `tests/fixtures/detect/manifest/`: `alpha-v1.json` (with the marker key), `alpha-v2.json`, `other-plugin.json`,
  `wrong-types.json`.
- `tests/fixtures/status/`: the 13 SC-012 goldens (`fresh-all-states`, `cached-stale`, `no-feed`, `future-schema`,
  `macos-intel-not-for-this-mac`, `not-for-this-computer-linux`, `needs-newer-os-macos`, `needs-newer-os-linux`,
  `needs-newer-os-windows`, `macos-au-copies`, `record-notices`, `notes-rendering`, `installed-elsewhere`), written per
  PD-10. Invented names only (Alpha … Juliet).
- `notes-rendering` content. `golden_views` parses `full-v2.json` and, in the test, sets Alpha's
  `release.notes_markdown` (a `pub` field, `feed.rs:59`) to one Markdown text holding, in this order: a Heading 1, a
  Heading 5, an ordered list starting at 3, an unordered list with a nested unordered list, a quote, a fenced code
  block (`let x = 1;`), a thematic break, a paragraph with `**strong**`, `*emphasis*` and `` `inline code` `` and a hard
  break (`two spaces` + newline); Bravo keeps `notes_markdown: None`, so its card carries `notes: []`. The fixture feed
  file itself is not changed (FR-090). The Markdown is written in the test with no URL (the FR-045 frontend scan and
  SC-011's "no `http`" rule).
- Test-time moduleinfo / plist / manifest inputs (never committed, built in the test under the row's temp root, each
  carrying the `KM-CONTENT-MARKER` key where it is JSON): `current.json` with a leading UTF-8 BOM; the bytes
  `0xFF 0xFE` followed by `{}`; `[]`; a class `Version: 1`; no class version with top-level `Version: 1`; classes
  `1.2.0.1` with no top-level `Version`; classes `1.2.0.1` with top-level `Version: 1`; a directory named
  `moduleinfo.json`; the 1 MiB + 1 byte moduleinfo and AU `Info.plist`; the 4 MiB + 1 byte manifest and a directory
  named `Alpha.json` (SI-17, SI-18).

### 4.12 Guard changes in `crates/core/tests/`

**`injection.rs`**
- `PLATFORM_LITERALS` → 37 entries (FR-033; PD-4 for `UNINSTALL_KEY`'s source form; the Linux entry is
  `/usr/bin/getconf`, SI-19).
- `FR007_DEPENDENCIES` → 11 (`pulldown-cmark`); `core_dependencies_are_the_fr007_set`: the macOS target table is
  `[core-foundation, plist]`, both `{workspace=true}`; root pins of the two new crates (PD-15); its existing plant stays.
- `READER_PINS[1].features` → the five FR-081 features (PD-16).
- `CONFINED_TOKENS` `windows_sys` → `[OS_PROXY_RS, OS_FACTS_RS]` (SI-1), with `const OS_FACTS_RS: &str =
  "src-tauri/src/os_facts.rs"`.
- `CONFINED_TOML_KEYS` → 9 rows (PD-15).
- New tests (FR-082, FR-083, FR-084, FR-088), each reusing `workspace_rs_files`, `lex`, `confinement`, `plant`,
  `assert_reported`:
  - `notes_and_plist_crates_are_confined`: `pulldown_cmark` only in `notes.rs` / `notes_tests.rs`; `plist ::` only in
    `detect.rs` / `detect_tests.rs`, and in `detect.rs` only inside `fn read_au_plist` whose outer attributes
    (`outer_attributes`, `:890`) contain `target_os = "macos"`.
  - `host_io_symbols_are_confined`: `RegOpenKeyExW`, `RegGetValueW`, `RegCloseKey`, `RtlGetVersion` only in
    `os_facts.rs`; the literals `/usr/sbin/pkgutil`, `/usr/bin/sw_vers`, `/usr/bin/getconf`, `GNU_LIBC_VERSION` and the `UNINSTALL_KEY`
    source text only in their `platform/<os>.rs` (literal scan via `strip_comments_and_literals`; `injection.rs`
    itself exempt); `OsHostIo` only in `os_facts.rs` and `commands.rs`.
  - `confinement_scanner_reports_plants`: one plant per rule above (e.g. `use pulldown_cmark::Parser;` in `status.rs`,
    `fn f() { let _ = plist::Value::Boolean(true); }` in `detect.rs` outside the AU fn, `RtlGetVersion` in
    `commands.rs`, `"/usr/sbin/pkgutil"` in `linux.rs`, `OsHostIo` in `lib.rs`), each reported.
  - `test_sources_never_name_os_host_io` / `test_source_scanner_reports_planted_os_host_io` (FR-083) over
    `is_test_source` files minus `injection.rs`.
  - `detect_status_notes_hold_no_write_or_listing_token` / `token_scanner_reports_planted_read_dir` (FR-084): the token
    list over `detect.rs`, `notes.rs`, `status.rs` and the non-test `platform/*.rs`; `read_dir` exactly once in
    `detect.rs` and inside `fn list_root`; path tokens `fs :: write`, `fs :: copy`, `File :: create`; the plants of
    FR-084.
  - `test_sources_hold_no_real_location` / `real_location_scanner_reports_plants` (FR-088): the eleven regexes over the
    six named files (the two `.ts` files read from `src/`), using the `regex` crate (a normal dependency of the package,
    available to its integration tests).

**`config_guard.rs`**
- Capability permissions: the five (FR-061); the capability `description` compared exactly with
  `The main window may call only the app's own commands.` (SI-21; today only the key set is checked, `CAPABILITY_KEYS`,
  `config_guard.rs:30`), with a planted other description reported in `planted_config_changes_fail_the_guard`;
  `build.rs` check: the five-command string; `LIB_RS_STRINGS` unchanged (14 entries: no opener plugin is registered,
  PD-20).
- `SRC_TAURI_DEPENDENCIES` → six keys; the `tauri-plugin` rule narrowed per SI-2; plants per SI-2;
  `planted_config_changes_fail_the_guard` gains a sixth permission `allow-planted`, `"allow-fs-read"` and
  `"opener:allow-open-url"` (FR-063), each reported.
- `check_frontend` gains a CSS rule (FR-077): in every file under `src/` whose name ends `.css`, a line containing
  (ASCII case-insensitive) `url(`, `@font-face`, `@import` or `image-set(` is reported as `<file>:<line>: a font, image
  or URL in a stylesheet (FR-077)`. `src/styles.css` holds none today (`font-family: system-ui, …` names installed
  families, not a font file, and is not matched). `frontend_source_scan_reports_planted_strings` gains four plants in
  a `src/planted.css` (`src: url(x.woff2)`, `@font-face {`, `@import "x.css";`, `background: image-set("a.png" 1x)`),
  each reported; the URL plant is not `http`-prefixed, so it proves the CSS rule, not the existing URL ban.
- New `opener_is_confined_and_not_granted_to_the_webview` + `opener_guard_reports_plants` (FR-065 as amended by
  SI-15): the identifier `tauri_plugin_opener` only in `src-tauri/src/commands.rs` (`holds_token`, `:711-721`); no
  `src-tauri/src` file holds the token sequences `tauri_plugin_opener :: init` or `tauri_plugin_opener :: Builder`
  or the identifier `OpenerExt` (commands.rs included); no capability permission starting `opener:`; no
  `tauri-plugin-opener` key in `crates/core/Cargo.toml` or `crates/helper/Cargo.toml`; no `@tauri-apps/plugin-opener`
  key in `package.json` `dependencies` / `devDependencies`; `tauri.conf.json` has no `plugins.opener` key. The `src/`
  rule is an **import** rule, as FR-065 words it, over the non-`*.test.ts` files of `src/`: a line holding
  `from "@tauri-apps/plugin-opener"`, `from '@tauri-apps/plugin-opener'`, `import("@tauri-apps/plugin-opener")` or
  `import('@tauri-apps/plugin-opener')` is reported. Test files are excluded because SC-034's source-scan case in
  `src/status.test.ts` holds `@tauri-apps/plugin-opener` as a needle and plants it in an in-memory copy; the import
  forms are excluded from that test file as well, because `status.test.ts` builds its needle by concatenation
  (`"@tauri-apps/" + "plugin-opener"`), so neither scan depends on the other's exclusion. Plants: a planted
  `"opener:allow-open-url"` permission, `tauri_plugin_opener` in `status.rs` and in `lib.rs`,
  `.plugin(tauri_plugin_opener::init())` in `commands.rs`, `use tauri_plugin_opener::OpenerExt;` in `commands.rs`, and
  `import { openUrl } from "@tauri-apps/plugin-opener";` in a planted non-test `src/planted.ts`, each reported; the
  same import line in a planted `src/planted.test.ts` is **not** reported.

**`roster.rs`**: `install_identifiers_absent_from_sources` / `install_identifier_scanner_reports_plants` (FR-089)
with `regex` patterns `com\.krateaudio\.` (exempt: the `APP_IDENTIFIER` line of `platform/mod.rs`),
`\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-`, `_is1`.

### 4.13 Workflow and the shape test (FR-080, FR-090)

`manager-ci.yml`, after `cargo test`:

```yaml
      - name: core dependency tree
        run: |
          set -euo pipefail
          out="$RUNNER_TEMP/core-tree.txt"
          cargo tree -p krate-manager-core -e normal --locked --prefix none --format "{p}" > "$out"
          names=$(awk '{print $1}' "$out" | sort -u)
          n=$(printf '%s\n' "$names" | grep -c . || true)
          hits=$(printf '%s\n' "$names" | grep -E '^(tauri(-.*)?|tokio|reqwest|hyper|dirs(-.*)?|directories(-.*)?|home)$' || true)
          if [ -z "$hits" ] && [ "$n" -ge 1 ]; then
            echo "::notice title=core dependency tree ($RUNNER_OS)::$n crates, none excluded"
          else
            for h in $hits; do echo "::error title=core dependency tree ($RUNNER_OS)::$h"; done
            [ -n "$hits" ] || echo "::error title=core dependency tree ($RUNNER_OS)::no crates listed"
            exit 1
          fi
```

`manager-ci-shape.test.js`: `'core dependency tree'` after `'cargo test'` in `STEPS`, in `ASSERTION_STEPS`, and a
`COMMANDS` entry holding the `cargo tree …` line and the `if [ -z "$hits" ] && [ "$n" -ge 1 ]; then` line. These are
edits inside existing test cases, so `feed-tools.yml`'s TAP plan `1..59` is unchanged. No other workflow changes.

### 4.14 `tests/required-tests.json`

`cargo.all` gains (SC numbers in brackets): `detect::tests::candidate_table` [002], `trailing_comma_cases` [004],
`installed_state_table` [005], `duplicates_in_two_roots` [006], `record_parsers` [007], `records_through_fake_host`,
`outside_install_location_panics` [008], `os_version_parsers` [010], `detection_leaves_the_tree_unchanged` [017],
`host_read_mappers` [026], `nested_copies_listed` [029], `listing_caps_honoured`, `listing_production_limits` [030],
`installed_elsewhere` [031] (all `detect::tests::`); `status::tests::feed_driven_detection` [009],
`running_os_gates` [010], `golden_views` [012], `build_cases`, `concurrent_builds` [018], `listing_runs_once_per_root`
[030], `release_page_url_cases` [032]; `notes::tests::blocks_cases` [011]; `notes_and_plist_crates_are_confined`,
`host_io_symbols_are_confined`, `confinement_scanner_reports_plants`, `test_sources_never_name_os_host_io`,
`test_source_scanner_reports_planted_os_host_io`, `detect_status_notes_hold_no_write_or_listing_token`,
`token_scanner_reports_planted_read_dir`, `test_sources_hold_no_real_location`, `real_location_scanner_reports_plants`
[022, 024]; `opener_is_confined_and_not_granted_to_the_webview`, `opener_guard_reports_plants` [033];
`install_identifiers_absent_from_sources`, `install_identifier_scanner_reports_plants` [022]. (`real_src_tauri_config_
passes_the_guard`, `planted_config_changes_fail_the_guard`, `core_dependencies_are_the_fr007_set`,
`reader_dependencies_are_the_ruled_set`, `core_sources_hold_no_production_literal`,
`frontend_source_scan_reports_planted_strings` are already listed.) 35 new names: 14 `detect::tests`, 7
`status::tests`, 1 `notes::tests`, 9 injection, 2 config_guard, 2 roster. The added cases of SI-17 / PD-21 / the
FR-077 CSS rule live inside these or already-listed tests and add no name.

`cargo.windows` + `detect::tests::native_layout_windows`, `locked_moduleinfo_windows`,
`host_facts_through_fake_io_windows`, `listing_case_windows` (SI-3), `listing_skips_junction_windows` (SI-20). `cargo.macos` + `native_layout_macos`,
`au_plist_cases_macos`, `pkg_receipt_macos`, `host_facts_through_fake_io_macos`, `probe_metadata_error_unix`,
`listing_skips_symlinked_dirs_unix`, `listing_case_exact_unix`. `cargo.linux` + `native_layout_linux`,
`linux_detection_linux`, `host_facts_through_fake_io_linux`, `probe_metadata_error_unix`,
`listing_skips_symlinked_dirs_unix`, `listing_case_exact_unix`.

`vitest` + the 13 `renders <scenario>` titles, `does not call status commands when another instance is running`
(new: the Phase 4 locked title is `shows only the notice when another instance is running`), `every status text is
rendered by a scenario`, the four SC-020 titles, the four SC-034 titles, the three SC-035 titles (13 + 1 + 1 + 4 + 4 +
3 = 26 new). Phase 4's three titles stay.

**Totals after the change** (today 130 / 2 / 17 / 16 / 3, §1.1): `cargo.all` 165, `cargo.windows` 7, `cargo.macos`
24, `cargo.linux` 22, `vitest` 29. SC-023's notice counts per leg are therefore `n = 165 + 7 = 172` (Windows),
`165 + 24 = 189` (macOS), `165 + 22 = 187` (Linux), `m = 29`.

### 4.15 Roadmap and index (FR-090 to FR-092)

The roadmap gets the Phase 6 status line at the close (`**Status: ✅ COMPLETE (PENDING-CI: N item(s)) (date)** — see
specs/krate-manager-phase6/compliance.md`) and the FR-092 hunk at lines 714-719, as two hunks only;
`node tools/gen-specs-index.js` regenerates `specs/INDEX.md`, then `node tools/gen-specs-index.js --check` (`ci.yml:172`).
SC-028's node check is a one-off `node -e` recorded in compliance (whitespace collapsed).

**The FR-092 hunk waits for the re-ruling of Q2 (SI-14, §10 Q-C).** The rule as the spec words it today ("an
`unins*.exe` in the install dir the helper itself reads from the same 64-bit HKLM key") is **not** written: the
`InstallLocation` value is admin-only, but the folder it names may be a user-chosen folder whose inherited DACL lets
unelevated code replace `unins000.exe` / `unins000.dat`, which the widened rule would then run elevated. Under the
recommended answer (a), the roadmap's clause "requires the executable to be an `unins*.exe` in the plugin's install
dir under the compiled-in Windows plug-in root" (`specs/Krate-manager-roadmap.md:716-717`) stays word for word, and
one sentence is added after the `run_inno_uninstaller` bullet's existing text: "A managed copy whose recorded
`InstallLocation` lies outside the compiled-in Windows plug-in root is never uninstalled by the helper: the manager
shows it as managed and tells the user to uninstall it from Windows Settings." SC-028 then checks 1 occurrence of `in
the plugin's install dir under the compiled-in Windows plug-in root`, 1 of `is never uninstalled by the helper`, and
those two hunks. Under (b) the sentence is the widened one plus the DACL check of SI-14, and SC-028's needle is that
sentence. Phase 10's own spec must also review `run_inno_installer`'s write into a non-default `InstallLocation`
(`UsePreviousAppDir=yes`); that is recorded for Phase 10, not changed here.

---

## 5. Dependencies

| Crate / package | Version | Where | Why this, not something else | Verified against |
|---|---|---|---|---|
| `pulldown-cmark` | 0.13.6, `default-features = false` | core `[dependencies]` | CommonMark event parser; no HTML writer compiled (`html` off); D9 rejects a TS Markdown library and a hand-written subset | crates.io API 0.13.6 (features, MSRV 1.71.1, MIT); docs.rs `Tag`, `HeadingLevel` (§1.2) |
| `plist` | 1.10.3, `default-features = false` | core macOS target table | reads XML and binary plists; already in the macOS graph through `tauri` | local `plist-1.10.3` source (§1.2) |
| `windows-sys` | 0.61.2, +3 features | `src-tauri` Windows table (existing) | the registry and `RtlGetVersion` FFI; first-party Microsoft bindings already used | local `windows-sys-0.61.2` source (§1.2) |
| `tauri-plugin-opener` | 2.7.2 (manifest `"2.7.2"` = `^2.7.2`; `Cargo.lock` + `--locked` hold 2.7.2) | `src-tauri` `[dependencies]` | first-party Tauri crate opening a URL in the system browser cross-platform (ruled Q8); only its free function `open_url` is called, the plugin is never registered (PD-20), so no opener page script or IPC command exists; core's host check is the boundary and no `opener:*` permission is granted | docs.rs source `lib.rs`, `open.rs` 2.7.2 (§1.2); crates.io deps (spec External facts) |
| `serde` (derive) | 1.0.229 (existing) | core | `Serialize` for the view | workspace pin |
| `url` | 2.5.8 (existing) | core | FR-049 parsing | docs.rs (§1.2) |
| `regex` | 1.13.1 (existing core dependency) | core integration tests | FR-088 / FR-089 scanners | workspace pin |
| npm | none added | — | `@tauri-apps/plugin-opener` deliberately not installed (FR-065) | `package.json` |

No other crate. Nothing in `crates/helper`.

---

## 6. Safety design

- **Injected roots.** Detection reads only paths built from `ctx.roots.plugin_roots()`, the settings' Linux root, a
  validated `InstallLocation` (FR-003, then `check_sandboxed`, FR-005) and `ctx.roots.state_dir()/manifests/`, plus
  the bounded listing under the injected VST3 roots (never AU roots, never deeper than 4, at most 20,000 entries per
  root, no linked directory entered: on Unix a symlink's `DirEntry::file_type` is the link's own type, on Windows std's
  `is_dir()` is false for junctions and directory symlinks (name-surrogate reparse points, §1.2); tested by
  `listing_skips_symlinked_dirs_unix` and `listing_skips_junction_windows`). A read target that is not a regular
  file (a FIFO, a device, a directory) is never opened (PD-22). Every test builds its context from `fresh_temp_roots()`; a fake record naming
  a real folder panics before the first `stat`. Feed strings reach paths only as `PlainName` values; the registry
  value passes FR-003 before it is joined.
- **Read-only by construction.** `detect.rs`, `status.rs`, `notes.rs` and the non-test `platform/` files hold no write,
  rename, remove, create, permission, process, socket or thread token (FR-084 guard); `read_dir` once, in `list_root`.
  Every read is bounded (1 MiB moduleinfo / plist, 4 MiB manifest). SC-017 snapshots the tree before and after a build.
- **Verify before extract / execute.** Nothing is downloaded, extracted or executed in Phase 6 (Phases 7-10).
- **The host readers.** Registry: `KEY_READ | KEY_WOW64_64KEY`, `RegGetValueW` with `RRF_RT_REG_SZ`, `RegCloseKey`
  on every path; no write API named (FR-082 guard confines the four FFI names to `os_facts.rs`). Processes: only
  `/usr/sbin/pkgutil --pkg-info|--files <id>` (id an argv element that matched `^com\.krateaudio\.[a-z0-9]+$`),
  `/usr/bin/sw_vers -productVersion`, `/usr/bin/getconf GNU_LIBC_VERSION`, each by absolute path, so the process
  `PATH` never chooses the program (SI-19); no shell; bounded (5 s / 2 s) and never killed (`run_bounded`). Tests use
  only `FakeHostFacts` / `FakeHostIo`; no test names `OsHostIo` (FR-083 guard).
- **The elevated helper.** Untouched (Phase 4 stub); nothing calls it. The spec's FR-092 would have widened Phase 10's
  `run_inno_uninstaller` to any `unins*.exe` in the folder HKLM `InstallLocation` names. The value is admin-only, the
  folder is not: `setup.iss` lets the user choose it on a first install and sets no permissions on it (SI-14), so an
  unelevated process could replace the uninstaller the helper would then run elevated. This plan does not write that
  widening; it is put back to the user as a re-ruling of Q2 (§10 Q-C), recommended (a): keep the "under the
  compiled-in root" clause and send copies outside it to Windows Settings.
- **The Tauri capability set.** Five app commands: two read the system (`plugin_status`, `check_now`), two read state
  (`app_version`, `startup_error`), one opens a URL only after `status::release_page_url` accepts it (`https`, host
  `github.com`, no userinfo, no port, ≤ 2,048 bytes), checked once when the view is built and again in the command, on
  the received string, whose validated form is what is opened. The opener crate is not registered as a plugin
  (PD-20), so the webview gains no injected script and no opener IPC command (`open_url`, `open_path`,
  `reveal_item_in_dir` are never registered), and the guard reports a planted `tauri_plugin_opener::init`. No
  `opener:*`, `fs:*`, `shell:*` or `http:*` permission; CSP, navigation guard and the markup-sink ban unchanged; notes reach the DOM only as `textContent`; no
  `a`, `img`, `iframe`, `script`, `style`, `link`, `object`, `embed`. The residual (any `github.com` page can be
  opened by a compromised webview) is the accepted Q8 boundary.
- **No real system in tests.** Nothing reads `%LOCALAPPDATA%\Programs\Common\VST3`, `C:\Program Files\Common Files\VST3`,
  `/Library/Audio/Plug-Ins`, `~/.vst3`, `ProgramData`, the registry or receipts; no test launches the app or a
  non-loopback connection. The user may audition with `npm run tauri dev` (read-only against the real folders); that is
  a user step, not a gate.
- **Keys.** No key is involved in this phase.

---

## 7. Test plan

### 7.1 SC → test

| SC | Test (file) | Strategy |
|---|---|---|
| 001 | the lane of §7.4 | exit codes, 0 `warning:` lines, `git status --porcelain` snapshot equality minus `evidence/` |
| 002 | `detect::tests::candidate_table` (`detect_tests.rs`) | synthetic `PluginRoot` lists joined from one `TempDir` path (no FS access); InstallFacts from `full-v2.json` / `tenth-plugin.json` parsed through `feed::parse`; expected `Vec<Candidate>` built with `Path::join`; cases (a)-(c) |
| 003 | `native_layout_windows/_macos/_linux` | `fresh_temp_roots()`; Linux configured via `context::reassemble_with_os(Os::Linux, …)`; assert managed candidate paths |
| 004 | `trailing_comma_cases` | inline strings + `iterum-shape.json`; `[,]`, `{,}` and `[1,,]` → `Corrupt` (FR-012 as amended, SI-16) |
| 005 | `installed_state_table` | one `fresh_temp_roots()` per row, fixture copied to the managed path; `install_capture` + `take_mine`; level / code / marker asserts; the 1 MiB + 1 file written at test time. Added rows (SI-17, test-time inputs of §4.11): UTF-8 BOM + `current.json` → `Known 1.2.0, Classes`, no log (FR-011); bytes `0xFF 0xFE` + `{}` → `Corrupt`, one `E_DETECT_MODULEINFO` `not UTF-8` (FR-011); `[]` → `Corrupt`, one `E_DETECT_MODULEINFO` `root not an object` (FR-012); class `Version: 1` → `ClassesDisagree`, one `E_DETECT_CLASSES` naming `<non-string>` (FR-013 a); no class version, top-level `Version: 1` → `NoVersion`, no log (FR-013 c); classes `1.2.0.1`, no top-level → `Known 1.2.0, Classes`, no log (FR-013 d); classes `1.2.0.1`, top-level `Version: 1` → `Known 1.2.0, Classes`, one `warn!` `E_DETECT_TOP_LEVEL` (FR-013 d); a directory named `Contents/Resources/moduleinfo.json` → `Unreadable`, one `E_DETECT_MODULEINFO` (PD-22; on every host) |
| 006 | `duplicates_in_two_roots` | trees on disk, `detect_plugin(Os::X, …)` with `FakeHostFacts`, then `assemble(Os::X, …)`, compare `PluginView`s |
| 007 | `record_parsers` | pure; committed texts; capture logger for `E_RECORD_LOCATION` and the marker; added (SI-18): a 4 MiB + 1 byte `manifests/Alpha.json` generated at test time and a directory named `manifests/Alpha.json` → `Failed(E_RECORD_READ)` each |
| 008 | `records_through_fake_host`, `outside_install_location_panics` | `FakeHostFacts` per OS row; call lists from `calls`; `#[should_panic(expected = "outside the test sandbox")]` |
| 009 | `status::tests::feed_driven_detection` | `status::build` (not `assemble`, which takes `running` as an argument and never calls the host) against the Phase 5 `FixtureServer` serving `tenth-plugin.json`, `v1-only.json` and `mixed.json` in turn (`set_route`, as in row 018), each with a fresh `FakeHostFacts` and a `fresh_temp_roots()` context holding `Juliet.vst3` in the managed location; asserts the SC-009 view facts, availability for `NativePlatform.os()`, and for `v1-only.json` `calls == ["os_version"]` exactly (so `build`'s listing and feed path run and no record lookup happens) |
| 010 | `os_version_parsers`, `status::tests::running_os_gates` | pure |
| 011 | `notes::tests::blocks_cases` | exact model equality; `serde_json::to_string` of each model contains no `http`. Added cases (SI-17, PD-9): `<div>\nx\n</div>` (a CommonMark type-6 HTML block, unlike `<b>x</b>`, which is `InlineHtml` inside a paragraph) → `[Paragraph [Text "<div>\nx\n</div>"]]` (final line break removed); `a\n\n---\n\nb` → `[Paragraph [Text "a"], Rule, Paragraph [Text "b"]]`; `a\nb` → `[Paragraph [Text "a b"]]` (`SoftBreak` → `Text " "`, merged); `1. x` → `List { ordered: true, start: None, … }` |
| 012 | `status::tests::golden_views` | `assemble` on synthetic `FeedCheck` (`feed::parse` of fixtures; `FeedError::Http(HttpError::Offline{…})`, `FeedError::FutureSchema{found: 3}`; `notes-rendering`'s Markdown set in the test, §4.11) and synthetic `Detection`s with `PathBuf::from("/fixture/…")`; byte equality; OS-consistency rule over every file; exact directory listing; plus every variant of every view enum serialised with `serde_json::to_string` and compared with the FR-046 strings written as literals in the test (`"no_moduleinfo"` among the nine `unknown_reason` values, PD-21) |
| 013 | `au_plist_cases_macos` | macOS; binary plist written at test time with `plist::Value::to_writer_binary`; added (SI-17): an `Info.plist` of 1 MiB + 1 byte written at test time → `Unknown(TooLarge)`, one `E_DETECT_PLIST` (FR-020) |
| 014 | `pkg_receipt_macos` | macOS; `PlatformFacts { io: &FakeHostIo }`; argvs from `platform::PKGUTIL` etc. |
| 015 | `linux_detection_linux` | Linux; manifest under `state/manifests/Alpha.json` |
| 016 | `locked_moduleinfo_windows` | Windows; `OpenOptions::new().read(true).share_mode(0)` held during `detect_plugin` |
| 017 | `detection_leaves_the_tree_unchanged` + the FR-084 guard | recursive snapshot (rel path, kind, size, mtime) before / after `status::build` against a fixture server |
| 018 | `status::tests::build_cases`, `concurrent_builds` | `FixtureServer` + loopback `HttpConfig`; `Reply::Close` via `set_route`; recorded request headers; `std::sync::Barrier` local to the test |
| 019 | `src/status.test.ts`: `renders <scenario>` ×13, `does not call status commands when another instance is running`, `every status text is rendered by a scenario` | goldens imported as JSON → `toStatusView` → `renderCards` / section into a detached element; element and attribute walk; template-part coverage. `renders notes-rendering` also pins the FR-072 mapping (SI-17): in Alpha's `details`, after the `summary`, the element children's tag sequence is exactly `h3` (Heading 1), `h6` (Heading 5), `ol` with `.start === 3`, `ul` holding a nested `ul` inside an `li`, `blockquote`, `pre` whose only child is `code` with the code text verbatim, `hr`, `p` holding `strong`, `em`, `code` and a `br`; Bravo's `details` (`notes: []`) holds `summary` then exactly one `p` reading `No release notes.` |
| 020 | `src/main.test.ts`: four SC-020 titles | `mockIPC` with deferred promises; the four Phase 4 cases updated per §4.10 (FR-070) |
| 021 | `real_src_tauri_config_passes_the_guard`, `planted_config_changes_fail_the_guard` | existing tests, updated tables |
| 022 | the FR-080 to FR-084, FR-089 guards + the `core dependency tree` step | plants reported, 0 findings on the real tree; local `cargo tree` output recorded |
| 023 | `required tests ran` rule | locally: the same Node script as `manager-ci.yml:140-162` run over the local `cargo test` log and `vitest --reporter=json` output (commands in §7.4) |
| 024 | `test_sources_hold_no_real_location`, `real_location_scanner_reports_plants`, roster guards | as written |
| 025 | `host_facts_through_fake_io_<os>` | per OS; `NativePlatform` with `FakeHostIo`; expected argv / subkey from the re-exported constants |
| 026 | `host_read_mappers` | pure; a counting closure |
| 027 | `probe_metadata_error_unix` | `cfg(unix)`; `vst3-global` dir removed and replaced by a file; added (SI-17, PD-22): a FIFO at `Alpha.vst3/Contents/Resources/moduleinfo.json`, made in the temp tree with `/usr/bin/mkfifo` (std has no `mkfifo`; a test-only process inside the temp dir, no real location), then `probe` run on a helper thread whose result comes back over an `mpsc` channel with `recv_timeout(5 s)`: expected `Unknown(Unreadable)` and one `E_DETECT_MODULEINFO` within the timeout, so a regression fails the case instead of hanging the binary |
| 028 | node check | `node -e` over the roadmap; `git diff --stat` of the roadmap |
| 029 | `nested_copies_listed`, `listing_case_windows`, `listing_case_exact_unix`, `listing_skips_symlinked_dirs_unix`, `listing_skips_junction_windows` | on-disk trees; case tests on `Listing::scan` at `vst3-user/Old/alpha.vst3`, asserting `paths()` and `copies_of("Alpha.vst3")` (Windows: exactly that copy; Unix: none) (PD-3, PD-19); symlink loop `a/loop -> ..`; added to the unix test (SI-17): a directory `vst3-user/locked` with mode `0o000` holding `Alpha.vst3` is skipped with no `warn!` and no match (FR-006), its mode restored before the `TempDir` drops; the case first asserts that `read_dir` of that directory fails (it succeeds for root, so a root run fails loudly with that cause rather than passing vacuously; CI runners and the WSL user are not root). Windows junctions (SI-20): made with `cmd /C mklink /J <link> <target>` (no admin right needed; `symlink_dir` needs one, so directory symlinks are not created; std treats both through the same name-surrogate branch, §1.2) |
| 030 | `listing_caps_honoured`, `listing_production_limits`, `status::tests::listing_runs_once_per_root` | `ListingLimits { max_depth: 2, max_entries: 5 }`; capture logger |
| 031 | `installed_elsewhere` | `assemble` per OS over synthetic `Detection`s |
| 032 | `status::tests::release_page_url_cases` | pure; the 2,049-byte URL built in the test; `api.` host via `format!` |
| 033 | `opener_is_confined_and_not_granted_to_the_webview`, `opener_guard_reports_plants` + recorded `cargo tree` / `npm ls` | §4.12 (import rule over non-test `src/` files; no `tauri_plugin_opener::init` / `Builder` / `OpenerExt`; the identifier only in `commands.rs`) |
| 034 | `src/status.test.ts`: four SC-034 titles | the URL comes from the `installed-elsewhere` golden (the frontend scan forbids `https://` in `src/`, `config_guard.rs:870-874`); source scan reads `src/*.ts` with `node:fs`, excluding `*.test.ts`; its needles are built by concatenation (`"@tauri-apps/" + "plugin-opener"`, `"window." + "open"`, …), so `status.test.ts` itself never holds a needle literally and the Rust opener guard (an import rule over non-test files, §4.12) is green on the real tree either way |
| 035 | `src/main.test.ts`: three SC-035 titles | `plugin_status` held on a deferred promise; `await` a microtask flush, assert pending, resolve / reject |

### 7.2 FRs without their own SC

FR-002 (no `"Krate Audio"` literal outside `platform/`: `FORBIDDEN_OUTSIDE_PLATFORM` does not list it, so
`host_io_symbols_are_confined` also asserts that no non-`platform/` core source holds the literal `Krate Audio`, with a
plant in `detect.rs` among `confinement_scanner_reports_plants`' plants). FR-047 (no content in logs: the markers of
SC-005, SC-007, SC-013). FR-062 (`tauri.conf.json` unchanged: `config_guard`'s CSP and conf checks). FR-077 (styles:
the new CSS rule of `check_frontend`, `url(` / `@font-face` / `@import` / `image-set(` in a `.css` file, 0 hits on the
real tree via `real_src_tauri_config_passes_the_guard` and four plants reported by
`frontend_source_scan_reports_planted_strings`, §4.12; the existing `http://` / `https://` ban alone would pass
`url(img.png)` or `@font-face`). FR-087 (roster guard). FR-090 / FR-091 (`git diff --name-only` at close; `--check`).
FR-092 (SC-028).

### 7.3 PENDING-CI rows (exact job and assertion)

| Row | Job | Closes when |
|---|---|---|
| SC-001 (macOS, Linux) | `check (macos-latest)`, `check (ubuntu-24.04)` | `clippy`, `cargo test`, `tauri build`, `build warnings` steps green |
| SC-002, 004-012, 017, 018, 021, 022 tests, 024, 026, 029 (`all`), 030-033 | both legs | `required tests ran` passes with no `::error` and notice `n = len(all) + len(<os>)`, `m = len(vitest)` |
| SC-003 | both legs | `native_layout_macos` / `_linux` named in the `macos` / `linux` lists, rule above |
| SC-013, SC-014 | `check (macos-latest)` | `au_plist_cases_macos`, `pkg_receipt_macos` in `macos` |
| SC-015 | `check (ubuntu-24.04)` | `linux_detection_linux` in `linux` |
| SC-025 | both legs | `host_facts_through_fake_io_macos` / `_linux` |
| SC-027, SC-029 (unix tests) | both legs | `probe_metadata_error_unix`, `listing_skips_symlinked_dirs_unix`, `listing_case_exact_unix` |
| SC-022 (tree) | both legs | `core dependency tree` notice `<n> crates, none excluded`, no `::error`; the macOS `n` includes `plist` |
| SC-019, 020, 034, 035 | every leg | `vitest` step green and the `required tests ran` vitest count |
| SC-024 ("roster absent") | both legs | `roster absent` notice, 0 hits |

**No local macOS compile signal.** The `cfg(target_os = "macos")` code (`read_au_plist` with `plist::Value::from_reader`,
`MacosPlatform::os_version` / `pkg_outputs`, the macOS `pub use` line) and the `cfg(not(windows))` stubs of
`os_facts.rs` are never compiled on this box: the Windows lane compiles only the Windows arms, and a local
`cargo check --target aarch64-apple-darwin` of core is not usable even though the target's `rust-std` is installed
(`~/.rustup/toolchains/stable-x86_64-pc-windows-msvc/lib/rustlib/manifest-rust-std-aarch64-apple-darwin`), because
core's macOS graph includes `rquickjs-sys`, whose build script compiles QuickJS C with `cc::Build`
(`rquickjs-sys-0.14.0/build.rs:153`) and needs an Apple-targeting C toolchain and SDK. So the first push is expected
to be the compile, clippy and test check of the macOS arms (`check (macos-latest)`: `clippy` and `cargo test` steps),
and a red there is a Phase 6 defect fixed before close, not a surprise. The Linux arms get the L-WSL signal of §8
(core only) before the push.

### 7.4 Local close procedure (Windows, this box)

From `apps/krate-manager/`, logs to `specs/krate-manager-phase6/evidence/` (excluded from the porcelain check):

1. `git status --porcelain > evidence/porcelain-before.txt`.
2. Concurrently (independent processes; cargo uses the whole box, the rest are seconds):
   - app lane, sequential within itself because it shares `target/`: `cargo fmt --all --check`; `cargo clippy
     --workspace --all-targets --locked -- -D warnings`; `cargo clippy -p krate-manager --all-targets --features
     dev-feed --locked -- -D warnings`; the clippy-guard command of `manager-ci.yml:102`; `cargo test --workspace
     --locked 2>&1 | tee evidence/cargo-test.txt` (default test threads); `cargo tree -p krate-manager-core -e normal
     --locked --prefix none --format "{p}" > evidence/core-tree.txt` and the same for `-p krate-manager`
     (`tauri-plugin-opener v2.7.2` present); `npm run tauri build 2>&1 | tee evidence/tauri-build.txt`.
   - frontend: `npm run typecheck`, `npm run lint`, `npm test -- --reporter=default --reporter=json
     --outputFile.json=evidence/vitest.json`; `npm ls @tauri-apps/plugin-opener` (empty).
   - pipeline lane (repo root): `node --test "tools/feed-contract/tests/*.test.js"`; `node tools/lint-manager-version.js`.
3. The required-tests rule over `evidence/cargo-test.txt` and `evidence/vitest.json` for `windows`.
4. Roster-absent and dev-feed-absent checks of `manager-ci.yml:184-218` run by hand over `target/release/krate-manager.exe`.
5. `node tools/gen-specs-index.js` then `--check`; the SC-028 node check; `git diff --name-only` against FR-090.
6. `git status --porcelain` minus `evidence/` equals step 1 plus the intended changes; `node tools/clean-test-output.js
   --apply` (repo rule) for scratch output.

Expected wall clock from Phase 5's logs is dominated by the first build after the new crates (opener, pulldown-cmark);
record it and the core count used in compliance.

---

## 8. Build integration and gate lanes

- **Workspace:** members unchanged; three new core modules; one new `src-tauri` module; dependencies per §4.1.
- **`package.json`:** unchanged (no new script or package).
- **Workflows changed:** `manager-ci.yml` (one step). `feed-tools.yml` unchanged (TAP plan count unchanged, §4.13).
- **Lanes:** app lane + frontend + pipeline lane concurrently (§7.4). No C++ change, so no `run-gate.js` /
  `header-consumers.js` lane; `git diff --name-only` at close must show nothing under `plugins/`, `dsp/`, `cmake/`.
- **L-WSL (optional early signal):** inside WSL Ubuntu in its own target dir, beside the Windows lane (4 WSL cores),
  sequential within itself (shared target dir): `cargo clippy -p krate-manager-core --all-targets --locked -- -D
  warnings` (the CI gate of SC-001, so a Linux-only dead-code, unused-import or lint error in a `cfg(target_os =
  "linux")` / `cfg(unix)` arm shows before the push), then `cargo test -p krate-manager-core detect:: status:: notes::`.
  `src-tauri` is not linted there: it needs the WebKitGTK development packages of the Tauri Linux prerequisites, which
  this plan does not assume are installed in WSL, so `os_facts.rs`'s `cfg(not(windows))` stubs first compile on the
  `check (ubuntu-24.04)` leg. It is the only local run of the `_linux` / `_unix` tests; a red is fixed before pushing,
  a green closes nothing (rows stay PENDING-CI). macOS has no local compile or test (§7.3).
- **CI cost:** first build on each leg compiles `tauri-plugin-opener` (Linux adds `zbus`, macOS `objc2-*`) and
  `pulldown-cmark`; `Swatinem/rust-cache@v2` caches them after the first green run.

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| A DAW running on Windows holds plug-in files | A DAW maps the DLL, not `moduleinfo.json`; `File::open` with std's shared mode reads it. An exclusive lock (backup tool, antivirus) makes that one copy "version unknown (cannot be read)" with one warning while the run continues (SC-016). Nothing is written, so no lock can corrupt state. |
| Admin prompts | None: Phase 6 elevates nothing; HKLM is read with `KEY_READ`, which an unelevated user has on the Uninstall key. An access-denied read is `E_RECORD_READ` and a notice, never a prompt. |
| Offline at launch | `plugin_status` waits up to Phase 5's 30 s `TOTAL_TIMEOUT`; the screen shows `Checking for plugins…` and a disabled `Check now` (FR-070, SC-035); then the cached feed with "Could not refresh the list: …", or "Could not load the plugin list: …" and no cards. Detection runs over the cached feed. |
| WebKitGTK quirks on Linux | Only standard elements (`details` / `summary`, lists, `pre`), no web fonts, images or remote content; `toLocaleString()` follows the system locale and the tests assert only literal template parts. The Linux leg builds the app (`tauri build`); the visual check is the user's `tauri dev` audition. The Phase 12 NVIDIA toggle is not needed for this phase's static DOM. |
| `xdg-open` (or the macOS / Windows opener) missing or failing | `open_url` returns an error → `E_OPEN_FAILED: …` → `Could not open the release page: …` on the card (FR-078); nothing else breaks. |
| A large VST3 root (thousands of third-party bundles) | Depth 4, 20,000 entries per root, one `warn!` when capped; one traversal per build, not per plugin (SC-030). A single directory is read whole before sorting (PD-17): memory is its names. |
| Case-insensitive APFS on macOS | The listing compares names exactly on macOS (spec); a differently-cased nested copy on APFS is missed by the listing, but at a fixed candidate path `stat` finds it anyway. Recorded; the user ruled exact matching off Windows (Q3). |
| `/usr/lib64` a symlink to `/usr/lib` on some Linux distributions | The same bundle can appear as two copies at two fixed candidates; both are listed with their versions, which is accurate about what hosts scan. |
| Inno writes no `InstallLocation` | FR-026 treats it as optional; the default vendor path is managed; Phase 10's `helper-elevated (windows-latest)` job is the first real key read. |
| WOW64 registry redirection | `KEY_WOW64_64KEY` reads the 64-bit view whatever the process bitness (the manager is x64 anyway). |
| `RtlGetVersion` unavailable or failing | `None`, which never gates (Phase 5 FR-037). |
| `pkgutil` slow or hanging | 5 s bound per run, abandoned and reaped, never killed; `E_RECORD_TIMEOUT` notice; detection of files unaffected. Nine plugins × two runs are sequential (ruled Q4), so a slow `pkgutil` delays the first render: the pending text covers it. |
| macOS privacy prompts (TCC) | Phase 6 reads no `~/Documents`, Desktop or Downloads path (the manifest lives in the state dir; presets are not read), so no prompt. |
| CRLF checkouts break byte-equal goldens | `tests/fixtures/.gitattributes` `-text` (PD-11). |
| The opener plugin's 3.0 line | The manifest line `tauri-plugin-opener = "2.7.2"` is a caret requirement (`^2.7.2`), as every workspace pin is (`Cargo.toml:13-36`), so it never admits 3.x (`3.0.0-alpha.3` exists and is not used, spec External facts) but would admit a later 2.x; `Cargo.lock` holds 2.7.2 and every command runs with `--locked`, so only a deliberate `cargo update` moves it, and SC-033's recorded `cargo tree` line `tauri-plugin-opener v2.7.2` would show the move at close. |
| A user-chosen Inno folder that unelevated code can write | Phase 6 only reads it. The Phase 10 uninstall widening that would run an `unins*.exe` from it elevated is not written (SI-14, §10 Q-C). |
| `Cargo.lock` churn when adding crates | Additive-only lock update checked before any gate (§4.1). |
| JSON goldens and TS types drifting | `toStatusView` fails `npm test` on any mismatch (PD-12); `golden_views` fixes the Rust side byte for byte. |
| Smart App Control / SmartScreen | Not affected by this phase (no new binary type; the app is unsigned as before). |

---

## 10. Open questions (for the user)

- **Q-A (SI-11, PD-13):** after a rejected "Check now", keep the previous cards and show the error in the feed line
  (recommended, and what this plan builds), or clear the cards?
- **Q-B:** add one `### Added` bullet to `apps/krate-manager/CHANGELOG.md` `[0.1.0] - Unreleased` for the status
  screen? The spec does not ask for it and Phase 5 added none; this plan adds nothing unless the user says so.

- **Q-C (SI-14), a re-ruling of Q2.** Q2 widened Phase 10's `run_inno_uninstaller` to any `unins*.exe` in the folder
  HKLM `InstallLocation` names, because "only an administrator can write that value". The value is admin-only, but the
  folder is the one the user picked on the first install; Inno's `setup.iss` sets no permissions on it, so on a
  second drive it can be writable without elevation, and a replaced `unins000.exe` would run elevated behind the
  manager's own UAC prompt. (a) **Recommended, and what this plan writes:** keep Phase 10's "under the compiled-in
  Windows plug-in root" clause; a copy installed elsewhere is still shown as the managed copy (Phase 6 is unchanged),
  but the manager never runs its uninstaller and tells the user to uninstall it from Windows Settings. (b) Keep the
  widening, and have the Phase 10 helper refuse unless the owner and DACL of `unins*.exe`, `unins*.dat` and every
  ancestor folder up to the volume root grant no write, delete, `WRITE_DAC` or `WRITE_OWNER` right to anyone but
  Administrators, SYSTEM or TrustedInstaller.

The spec edits of §0.2 (SI-1 to SI-21) are for the main loop before `tasks.md`; SI-1, SI-2, SI-7 and SI-15 block the
build; SI-14 waits for Q-C and blocks only the roadmap hunk.

## Review notes

Challenge round 1 (2026-10-31), 21 issues: none rejected outright; every blocker and major applied in full. Where the
resolution differs from the suggestion:

- **Opener guard vs SC-034 (major).** Both suggestions applied: the guard is an import rule over non-`*.test.ts` files
  (FR-065's own wording), and `status.test.ts` builds its needles by concatenation (§4.12, §7.1 row 034).
- **`no_moduleinfo` (major).** The core variant keeps the spec's name `UnknownReason::NoModuleInfo` (FR-011, FR-013
  and SC-005 use it); the view gets its own `UnknownReasonView::NoModuleinfo` (PD-21). The spelling assertion lives
  inside `golden_views`, so no required-test name is added.
- **FR-092 (major).** Neither (a) nor (b) is applied silently: the change reverses part of a user ruling, so it goes
  back to the user as Q-C with (a) recommended, and the plan writes no FR-092 roadmap hunk until then (SI-14). No
  Phase 6 code depends on the answer.
- **Opener registration (major).** Applied as suggested (free function, no plugin). It narrows the spec's D15 /
  FR-064 / FR-065 (SI-15); the Q8 ruling (the crate, no `opener:*` permission, host check in core) is unchanged.
- **`read_bounded` file type (minor).** Partly rejected: the suggested observable case, a directory named
  `moduleinfo.json`, does **not** distinguish the fix on Unix: without the `metadata` check `File::open` succeeds on a
  directory and `read_to_end` then fails with `EISDIR`, which is already `Io` → `Unreadable`. The directory row is kept
  (it documents the outcome on every host), and the observable case is a FIFO under `probe_metadata_error_unix` with a
  `recv_timeout`, so a regression fails instead of hanging (§7.1 row 027).
- **Opener pin (minor).** The reword option was taken, not `=2.7.2`: every workspace pin is a caret requirement held by
  `Cargo.lock` + `--locked` (`Cargo.toml:13-36`), and an exact pin on one crate would break that convention for no
  gain over the lockfile.
- **L-WSL clippy (minor).** Applied for `krate-manager-core` only; `src-tauri` needs WebKitGTK headers this plan does
  not assume in WSL, so its non-Windows stubs first compile in CI (§8, §7.3).
- **Windows junctions (two minors).** Merged into one test, `listing_skips_junction_windows`, made with `mklink /J`
  (no admin right), citing std 1.99.1's Windows `FileType` (§1.2). Directory symlinks are not created (they need a
  privilege on this box); they take the same name-surrogate branch in std.
- **Counts (minor).** Corrected to 35 `cargo.all` and 26 `vitest`; with SI-20 the Windows list gains one more than the
  issue's totals: 165 / 7 / 24 / 22 / 29 (§4.14).
