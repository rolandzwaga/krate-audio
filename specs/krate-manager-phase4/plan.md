# Implementation Plan: Krate Manager Phase 4 — Scaffold, Platform Abstraction and the Manager's CI

**Spec:** `specs/krate-manager-phase4/spec.md` (DRAFT, reviewed: challenge round 1 and Clarifications Q1-Q7 of
2026-10-30)
**Roadmap:** `specs/Krate-manager-roadmap.md` Phase 4 (lines 463-519); Decisions Q1, Q3, Q5, Q6, ROSTER (lines 24-33);
Architecture Overview "Processes" (104-110) and "Where state lives" (158-163); Cross-Cutting Constraints (961-1002)
**Status:** DRAFT plan, 2026-10-30, revised after plan review round 1 (see "Review notes"), verified against `feat/krate-manager` at `10593783` (untracked:
`specs/krate-manager-phase4/`; `apps/` does not exist, `ls apps` this session)
**Lanes:** the **app lane** (cargo fmt / clippy / clippy dev-feed / clippy-guard / cargo test, npm typecheck / lint /
test, the version lint, `tauri build`) and the **pipeline lane** (`node --test` over `tools/feed-contract/tests/`). No
C++ file changes, so there is no `run-gate.js` / `header-consumers.js` lane (§8).

---

## 0. What this plan adds to the spec

The spec fixes requirements and success criteria. This plan fixes file contents and module boundaries, the public Rust
and TypeScript API, the exact text of `manager-ci.yml` and the `feed-tools.yml` edit, the test-name list the CI
"required tests ran" step checks, and the local close procedure. Where the plan decides something the spec leaves
open, or must correct a spec sentence, it is listed here and repeated at its use site.

### 0.1 Plan decisions

| Id | Decision | Why (evidence) | Spec text touched |
|---|---|---|---|
| PD-1 | **Startup state is computed before the Tauri builder and managed with `Builder::manage`**, not inside `.setup`. `run()` does `Roots::from_os()`, the feed URL, `assemble_context`, the log sink and `log_startup_errors`, then `tauri::Builder::default().manage(SharedState::new(state))…`; `.setup` only schedules the locked-instance exit. | `Builder::manage` exists at `tauri` 2.12.5 `crates/tauri/src/app.rs:2055`; managing before `.run` means no command (`startup_error` is the frontend's first call) can ever see an unmanaged `State<SharedState>`, whatever the order between the setup hook and the creation of config windows (not needed, so not researched). `.setup`'s closure is `FnOnce(&mut App<R>) -> Result<(), Box<dyn Error>> + Send` (`app.rs:1838-1843`). FR-045's `.manage(` assertion still holds. | FR-017 ("`app.manage(...)` in setup"), FR-103 (mechanism only) |
| PD-2 | **`context::apply_settings(shared: &SharedState, settings: Settings)`** is the one way to swap in new settings: it takes the read guard, computes `reassemble`, drops the guard, then takes the write guard. SC-028's last case calls it. | The spec's literal `*shared.write() = reassemble(&shared.read(), s)` deadlocks or panics: the assigned value is evaluated first, but its temporary read guard lives to the end of the statement, so `write()` runs while this thread still holds the read lock (Rust reference, "Assignment expressions": "the assigned value operand is evaluated first, followed by the assignee expression"; "Temporary scopes": a temporary lives to the end of the enclosing statement, https://doc.rust-lang.org/reference/expressions/operator-expr.html#assignment-expressions, https://doc.rust-lang.org/reference/destructors.html#temporary-scopes; `std::sync::RwLock::write`: "might panic when called if the lock is already held by the current thread", https://doc.rust-lang.org/std/sync/struct.RwLock.html#method.write). | SC-028 (see SI-2) |
| PD-3 | **`StartupError` is `Clone`**: `LockIo { op: LockStep, source: Arc<std::io::Error> }`. `AppState` itself is not `Clone`; `reassemble` builds a new one. | `reassemble` must return an `Err` context "unchanged" from `&AppState` (FR-103), which needs a copy; `io::Error` is not `Clone`, `Arc<io::Error>` is, and `impl<T: Error> Error for Arc<T>` keeps it usable as a `#[source]` (https://doc.rust-lang.org/std/sync/struct.Arc.html#impl-Error-for-Arc%3CT%3E). | FR-017 (variant shape) |
| PD-4 | The missing-root rejection of `Roots::new` is **`RootsError::MissingRoot { kind: RootKind }`**. | SC-005 requires "a missing `Vst3User` root" to be rejected but names no variant. | FR-013 (see SI-4) |
| PD-5 | **Platform literal rule, made mechanical:** in a non-test file under `crates/core/src/platform/`, *every* string literal **outside an attribute** must be a member of `PLATFORM_LITERALS` (§4.10): the exact env names, path components, absolute D8 literals and `APP_IDENTIFIER`. A raw string (`r"…"`, `r#"…"#`) outside an attribute is itself a violation. **Literals inside an outer or inner attribute (`#[…]`, `#![…]`) are not checked** (option (a) of the plan review). The lexer recognises an attribute as `#`, an optional `!`, then `[` up to the matching `]`, counting brackets only outside literals. | FR-021 says "path string literals only from an allow-list" without saying how the scanner tells a path literal from another. Checking every literal needs no such judgement. The platform files hold no other non-attribute literal. They do carry attribute literals the allow-list must not have to name: six `#[cfg(target_os = "windows" \| "macos" \| "linux")]` in `mod.rs` (§4.2), and the FR-021 `#[path = "<stem>_tests.rs"]` in each of the four files. An attribute literal is compile-time only (a `cfg` predicate, a module file path, a lint `reason`), so it cannot become a runtime root. Exempting attributes keeps the allow-list a list of runtime values. Naming every `cfg` value and test-file name instead would grow with each new attribute. | FR-021 (definition) |
| PD-6 | **Absolute D8 roots are whole literals; env-derived roots are pushed component by component.** E.g. macOS `Vst3Global` = `PathBuf::from("/Library/Audio/Plug-Ins/VST3")`; Windows `Vst3User` = `LOCALAPPDATA` + `push("Programs")`, `push("Common")`, `push("VST3")`. `temp_roots_in` also joins one component per call (`base.join("plugins").join("vst3-user")`). | FR-011 asks for `PathBuf::push` joins. On Windows `std::fs::canonicalize` returns extended-length (`\\?\`) paths, to which "you can only join backslash-delimited paths" (https://doc.rust-lang.org/std/fs/fn.canonicalize.html), so a `join("plugins/vst3-user")` on a canonical temp base would produce a broken path. | FR-011, FR-020 (mechanism) |
| PD-7 | **The settings key set is checked explicitly** (exactly `schema_version`, `linux_plugin_root`, `linux_graphics_workaround`) after the `schema_version` classification and before typed deserialization; `#[serde(deny_unknown_fields)]` stays as a second net. | FR-033 makes a *missing* field `Corrupt`; an explicit key check holds whatever serde does with a missing `Option` field, so the plan does not rely on that behaviour. | FR-032, FR-033 (mechanism) |
| PD-8 | **Classification order** (shared by `load` and `save`'s refusal check): not JSON or not an object → `Corrupt`; `schema_version` missing, or not a JSON integer (`serde_json::Value::as_i64` and `as_u64` both `None`: a fraction, a string, or an integer outside both ranges that `serde_json` holds as `f64`) → `Corrupt`; `> 1` → `FutureVersion { found }` (decided before any other field is looked at, so a v2 file with an unknown field is still `FutureVersion`, SC-010); **any integer below 1, negatives included** (`as_i64` < 1) → `Invalid { reason: "schema_version below 1" }`; `1` → key-set check, typed parse (`Corrupt`), path check (`Invalid`). | SC-010 requires `FutureVersion` for "one that is otherwise corrupt", and the order makes that the rule rather than an accident. FR-033 defines `Invalid` as "`schema_version` < 1", which covers `-1` as well as `0`. `load_rejections_keep_defaults_and_file` therefore carries a `schema_version: -1` case beside SC-010's `0` (plan review). | FR-033, FR-036 (made concrete) |
| PD-9 | **`SettingsIo` steps that hold the temp file take it by value and hand it back** (`fn write(&mut self, tmp: NamedTempFile, bytes: &[u8]) -> io::Result<NamedTempFile>`). The fault double's *abort* mode calls `NamedTempFile::keep()` on the temp it holds, then returns an error, which leaves the temp file exactly where a crashed process would. *Fail* mode returns the error without keeping, so the temp is removed by its destructor. | FR-034's abort "stops without running destructors, leaving its temp file"; the double can only do that if it owns the temp at that step. `keep` and `persist` per https://docs.rs/tempfile/3.27.0/tempfile/struct.NamedTempFile.html. | FR-031, FR-034 (mechanism) |
| PD-10 | **Log truncation and rotation edges:**<br>- `format_line` cuts the message so the whole line, LF included, is at most `MAX_LINE_BYTES`. It steps back with `str::is_char_boundary`.<br>- `RotatingLog::write_line` given a line longer than the cap writes it into a fresh file, cut to at most `cap` bytes with the LF kept. The cut uses the same `is_char_boundary` step-back (`format_line`'s helper `cut_at_boundary(s, max) -> &str`), so a cut inside a multi-byte character never leaves invalid UTF-8.<br>- If the rotation rename fails, `write_line` reopens `LOG_FILE` in append mode, returns the rename error and writes nothing, so no file ever exceeds the cap.<br>Both edges are tested in `rotating_log_cases` (§7.1 SC-029). The failed rename is forced by making `krate-manager.log.1` a directory: renaming a file over an existing directory is refused on Unix and on Windows (https://doc.rust-lang.org/std/fs/fn.rename.html, "Platform-specific behavior": "when `from` is not a directory and `to` exists, `to` may not be a directory"; "Otherwise on Windows, … `to` must not be a directory"). | FR-104 says "neither over the cap" without covering an over-long line or a failed rename. `is_char_boundary` is stable since Rust 1.9; the workspace's `rust-version = "1.90"` makes clippy's `incompatible_msrv` lint flag any std API stabilized later, so the plan uses no newer helper. | FR-104 (made concrete) |
| PD-11 | **"build warnings" strips ANSI colour codes portably and is non-vacuous:** it counts `^warning:` after `esc=$(printf '\033'); sed "s/${esc}\[[0-9;]*m//g"`, and fails unless the stripped log also holds cargo's `Finished` line for the release profile. The ESC byte comes from `printf`, not a `\x1b` regex escape: `\xHH` is a GNU sed extension, and the `macos-latest` leg runs BSD sed, which would strip nothing (plan review). | `tauri-cli` 2.12.4 passes `--color always` to cargo only in `dev` (`crates/tauri-cli/src/interface/rust/desktop.rs:71-72`), so a build log is uncoloured today. The strip keeps a future colour change from turning the count into a silent 0 on all three legs, and the `Finished` check proves the log captured cargo at all. Setting `CARGO_TERM_COLOR=never` alone would not do it: a `--color always` that tauri-cli passes on the command line overrides the variable. | FR-071 (made robust) |
| PD-12 | **The FR-077 matcher accepts libtest's should-panic form** `test <name> - should panic ... ok` besides `test <name> ... ok`. | Measured this session (cargo 1.99.1, scratch crate, then deleted): a `#[should_panic]` test prints `test tests::panicking_case - should panic ... ok`. SC-007 has two such tests (`temp_roots_in_outside_panics`, `temp_roots_on_temp_dir_itself_panics`), so the literal FR-077 matcher would fail both on every leg. | FR-077 (see SI-1) |
| PD-13 | **The version lint's TOML subset includes `key = true` / `key = false`** and `[[table]]` headers (both read as section switches when outside a read section). | FR-002 puts `publish = false` in `[workspace.package]`, a section the lint reads (D18), and D18's subset ("section headers, `key = "string"`, `key.workspace = true`, string arrays") has no boolean, so the real tree would fail `E_TOML_UNSUPPORTED`. `src-tauri/Cargo.toml` has `[[bin]]` (FR-004). | D18, FR-063 (see SI-3) |
| PD-14 | **`[profile.release]` sits in the root `Cargo.toml` only**; `src-tauri/Cargo.toml` has none. | Cargo reads profiles only from the workspace root (https://doc.rust-lang.org/cargo/reference/profiles.html: "Cargo only looks at the profile settings in the Cargo.toml manifest at the root of the workspace") and, measured this session on a scratch member, prints `warning: profiles for the non root package will be ignored, specify profiles at the workspace root:`, which the "build warnings" count would turn red. The template block is `codegen-units = 1, lto = true, opt-level = 3, panic = "abort", strip = true` (`templates/_base_/src-tauri/Cargo.toml.lte` at `c6f5cf68`). | FR-002 (placement) |
| PD-15 | **`apps/krate-manager/.gitignore`** = `/target/`, `/dist/`, `/src-tauri/gen/`, `/src-tauri/permissions/autogenerated/`, `/tests/fixtures/clippy-guard/target/`. | `tauri-build` writes app-command permissions to `./permissions/autogenerated` relative to the build script's directory (`tauri-build` 2.7.4 `src/acl.rs:281`) and capability schemas to `gen/schemas` (`src/lib.rs:17`). The fixture line covers a hand run without `CARGO_TARGET_DIR`. `git check-ignore` this session: `target/`, `dist/` under `apps/krate-manager/` are not ignored by the root file; `node_modules/` is (`.gitignore:92`). | FR-003 (paths named) |
| PD-16 | **The fixture lockfile is derived, not resolved fresh:** `tests/fixtures/clippy-guard/Cargo.lock` is made by copying the workspace `Cargo.lock` and running `cargo metadata --manifest-path tests/fixtures/clippy-guard/Cargo.toml --format-version 1 --offline > /dev/null` once, which adds the fixture package and drops unused entries while keeping every pin. Any later workspace-lock change repeats that command (tasks.md carries it). | FR-072 runs the fixture with `--locked` and a shared target dir; equal pins are what makes the dependency artifacts reusable. A stale fixture lock fails the CI step loudly (a non-disallowed `error:` line), so drift cannot pass silently. | FR-016 (procedure) |
| PD-17 | **The fixture's `Roots::new` call gets a real `RootPaths` built from literals**, not `todo!()`. | `Roots::new(todo!())` makes rustc emit `unreachable_code` ("unreachable call"), a fourth diagnostic under `-D warnings` that FR-072's "no other error" rule rejects. The fixture is never run, so the paths' validity is irrelevant. | FR-016 (body) |
| PD-18 | A rejected `startup_error` call in the frontend is treated as "no notice" (then `app_version` as usual). A fourth Vitest case, `shows the version when startup_error fails`, proves it. It stays out of `required-tests.json`'s `vitest` list, which FR-080 pins to SC-017's three titles. It still runs and must pass in every `npm test`. | FR-046 covers three states. A rejection of the first call is not one of them, and an empty window would be worse than the version line. | FR-046 (edge) |
| PD-19 | `tools/lint-manager-version.js` joins **both** lists in `hygiene.test.js`: `single` (FR-082) and the completeness list `missing` (`hygiene.test.js:119-133`). | `scannedFiles()` silently drops a path that does not exist (`:46` `.filter((f) => fs.existsSync(f))`); the `missing` list is what turns a renamed tool into a red test. | FR-082 (one more line) |
| PD-20 | `manager-ci-shape.test.js` carries its own small YAML-text helpers (copied in form from `workflow-shape.test.js:18-113`) and a pure `checkManagerCi(files) -> string[]`; SC-019's mutations run on in-memory copies. | FR-090 permits no new helper file under `tools/feed-contract/tests/helpers/`, and requiring another `*.test.js` would run its tests. In-memory mutation keeps every test write-free. | FR-080 (structure) |
| PD-21 | The Tauri window is declared with an explicit `"label": "main"`. | FR-005 asks for "one window labelled `main`" and FR-041 grants window `main`; making the label explicit lets FR-045 assert it rather than rely on a default. | FR-005 (explicit) |
| PD-22 | **The seven D6 sites are counted, not just fulfilled.** `injection.rs` gains `disallowed_methods_expectations_are_the_seven_d6_sites` (§4.10). Over every `*.rs` file under `crates/*/src`, `crates/*/tests`, `src-tauri/src` and `tests/fixtures/clippy-guard/src`, it reads each lint-level attribute (`allow(`, `expect(`, also inside `cfg_attr(…)`) from the comment-stripped, literal-blanked code text. It asserts two things. (1) The per-file count of `clippy::disallowed_methods` is exactly the §4.1 table: `src-tauri/src/lib.rs` 1, `src-tauri/src/os_roots.rs` 2, `crates/core/src/test_support.rs` 2, `crates/core/src/roots_tests.rs` 1, `crates/core/src/test_support_tests.rs` 1, every other file 0 (the fixture included). (2) No lint-level attribute anywhere names `clippy::style`, `clippy::all` or `warnings`. A planted in-memory eighth site and a planted `#[expect(clippy::style, reason = "x")]` are each reported. | An unfulfilled-`expect` warning catches a *missing* site, never an *extra* one. An eighth `#[expect(clippy::disallowed_methods, reason = …)]` would pass every other gate (plan review). `disallowed-methods` belongs to the `clippy::style` and `clippy::all` groups (`clippy-driver -W help`, clippy 0.1.99, this session: both group lines list `clippy::disallowed-methods`), so an `expect` of either group, or of `warnings`, would silence it with no `disallowed_methods` token in sight. | FR-015 (made mechanical) |
| PD-23 | **Manifest and app-metadata guard.** `config_guard.rs` gains `check_app_meta` (§4.10). Over the root `Cargo.toml` and the three member manifests, it asserts:<br>- the root `[workspace]` `members` is exactly the FR-002 three, and `[workspace.lints.clippy]` holds both `deny`s;<br>- every member has `[lints]` with `workspace = true`;<br>- `test-support` appears in a member manifest only inside a `[dev-dependencies]` section, except core's `[features]`, which is exactly `test-support = []`.<br>Over `package.json`, `tsconfig.json`, `eslint.config.mjs` and `CHANGELOG.md`, it asserts the FR-050 to FR-052 and FR-062 facts.<br>`check_config` additionally asserts:<br>- src-tauri's `[dependencies]` keys are exactly {`tauri`, `serde`, `serde_json`, `log`, `krate-manager-core`};<br>- no line of `src-tauri/Cargo.toml` contains `tauri-plugin`;<br>- the `tauri` line is exactly `tauri = { workspace = true }`;<br>- the root `[workspace.dependencies]` `tauri` entry has `features = []`.<br>Each assertion has a planted mutation. | FR-002's `[lints] workspace = true` and FR-022's dev-only `test-support` were mapped to checks that cannot see them. Without `[lints]` in core, `allow_attributes = "deny"` does not reach `test_support.rs` or any core test. A `test-support` in `[dependencies]` would compile `temp_roots` into the release binary. FR-004's and FR-050's dependency lists, FR-051's strict flags, FR-052's `reportUnusedDisableDirectives` and FR-062's CHANGELOG entry regress silently: removing them compiles and makes nothing red (plan review). `config_guard.rs` runs on every `manager-ci` trigger (`apps/krate-manager/**`), whereas `manager-ci-shape.test.js` runs only on `feed-tools.yml`'s narrower path list, so the guard lives in Rust. | FR-002, FR-004, FR-022, FR-050 to FR-052, FR-062 (made mechanical) |
| PD-24 | **Test roots carry a sandbox, so no derivation can leave temp.** `Roots` has a private field `sandbox: Option<PathBuf>`.<br>- `Roots::new` sets it to `None`.<br>- `test_support::temp_roots_in` sets it to the canonical, guard-checked base through the crate-private `Roots::sandboxed(self, base: PathBuf) -> Roots`.<br>- `with_configured_plugin_root` on a sandboxed value **panics** when the configured path is not a strict, component-wise descendant of the sandbox base. The message names both paths, like `temp_roots_in`.<br>- `Clone` and every derivation keep the field.<br>Production `Roots` (from `from_os`) always have `None` and never take the branch. Tests can obtain a `Roots` only from `temp_roots` or `fresh_temp_roots` (FR-015) or from the one D6 validation site 6. So a configured root that reaches a test's `AppContext`, whether by a direct `with_configured_plugin_root` call, by `assemble_with_os` reading a planted `settings.json`, or by `reassemble_with_os`, stays inside the test's temp base or the test panics. The injection scanner allows the identifier `sandboxed` in no scanned file but `roots.rs`. New tests: `roots::tests::sandboxed_roots_refuse_an_outside_configured_root` and `context::tests::sandbox_holds_through_assemble_and_reassemble`. | `with_configured_plugin_root` is a public producer of a new `Roots`, outside `disallowed-methods`, that accepts any absolute path. `fresh_temp_roots().1.with_configured_plugin_root(Some(Path::new("/usr/lib/vst3")))` compiled clean, and so did the settings-file route through `assemble_with_os`. Meanwhile §6 and FR-083 claimed a non-temp `Roots` was impossible. Phase 9 treats `plugin_roots` as its allowed-root set (spec Clarifications). Putting the derivation on the clippy list (review option (a)) would add four `#[expect]` sites and still leave the settings-file route open. The sandbox closes both routes with no new suppression and keeps the D6 count at seven. | FR-013, FR-083, FR-102 (see SI-8) |
| PD-25 | **Every Phase 4 API that writes takes `&Roots`, not a bare `&Path`:**<br>- `settings::load(roots)`, `settings::save(roots, settings)`, `save_with(io, roots, settings)`;<br>- `StateLock::acquire(roots)`;<br>- `RotatingLog::open(roots)` and `open_with_cap(roots, cap_bytes)`, which write `roots.logs_dir()`;<br>- the src-tauri sink's `log_sink::init(roots: &Roots)`.<br>`run()` keeps a clone of the `Ok` roots for `init` (`let sink_roots = roots.as_ref().ok().cloned();`), because `assemble_context` consumes `roots` and a locked start has no `AppContext`. `dev_feed::read(state_dir: &Path)` stays a bare path: it only reads. | With bare paths, `settings::save(Path::new(r"C:\Users\…\com.krateaudio.krate-manager"), &s)` or `StateLock::acquire` on the real state dir passed clippy and the FR-021 scan, which skips `*_tests.rs` and `test_support.rs` (plan review). Production already passes only `roots.state_dir()` and `roots.logs_dir()` (§4.8, §4.12). Taking `&Roots` extends the `Roots` guard (FR-015 plus PD-24) to every file Phase 4 writes, with no new mechanism. | FR-031, FR-032, FR-100, FR-104, FR-105 (signatures; see SI-9) |
| PD-26 | **Roots and the configured root must be normal paths.**<br>- `Roots::new` rejects any path with a `Component::ParentDir`, and any plug-in, preset, state or staging root with no `Component::Normal` (a filesystem root such as `/` or `C:\`), as `RootsError::NotNormal { which }`.<br>- `with_configured_plugin_root` rejects either as `NotNormal { which: "configured" }`.<br>- `required_abs` rejects an env value with a `ParentDir` component as `Env { var }`. `HOME=/` stays accepted, since the derived roots have normal components.<br>- `settings::validate` rejects a `linux_plugin_root` with a `ParentDir` component as `Invalid { reason: "linux_plugin_root is not normalised" }`, and one with no `Normal` component as `Invalid { reason: "linux_plugin_root is a filesystem root" }`.<br>`CurDir` needs no rule: `Path::components` normalises interior `.` away, and a leading `.` makes the path relative, which is already rejected. | Validity was purely lexical "absolute". `Path::components` never resolves `..` ("Occurrences of `.` are normalized away, except if they are at the beginning of the path"; `..` is kept as `ParentDir`, https://doc.rust-lang.org/std/path/struct.Path.html#method.components). So `/home/u/.vst3/../.vst3` passed `validate` and FR-102's component-wise equality saw it as a second root: Phase 6 would list each bundle twice, and Phase 9's lexical containment check would get a non-normal entry. `LOCALAPPDATA=C:\Users\u\..\..\Windows` reached `staging_dir`. `/` was accepted as an install root (plan review). | FR-012, FR-013, FR-033, FR-102 (see SI-10) |
| PD-27 | **No `cmd \| grep -q` under `pipefail` in `manager-ci.yml`.** A step captures the writer's output in a variable first (`out=$(plutil -p "$plist")`, `info=$(codesign -dv "$app" 2>&1)`) and then greps the here-string (`grep -q … <<<"$out"`). `manager-ci-shape.test.js` fails on any `run:` text that holds `\| grep -q`. | `grep -q` exits at its first match, so the writer can take SIGPIPE (exit 141), which `pipefail` makes the pipeline's status. In the quarantine step a match, meaning the key is present, would then read as false and print "absent": a false green. In the codesign step `Signature=adhoc` is not `codesign -dv`'s last stderr line, so a correct bundle could fail. Both run only on the PENDING-CI macOS leg (plan review). | FR-074 (mechanism) |

### 0.2 Spec issues found while planning (for the main loop to fix in the spec before build)

| Id | Spec text | Issue | Proposed spec fix |
|---|---|---|---|
| SI-1 | FR-077 "exactly one line `test <name> ... ok`" | libtest prints `test <name> - should panic ... ok` for `#[should_panic]` tests (measured, PD-12); SC-007's two should-panic tests would never match | "exactly one line `test <name> ... ok` or `test <name> - should panic ... ok`" |
| SI-2 | SC-028 "after `*shared.write() = reassemble(&shared.read(), s)` on a `SharedState`" | Deadlocks / panics (PD-2) | "after `context::apply_settings(&shared, s)` (which drops the read guard before taking the write guard)"; add `apply_settings` to FR-103 |
| SI-3 | D18 subset; FR-002 `publish = false` in `[workspace.package]` | The lint would reject the real tree (PD-13) | D18: add "`key = true` / `key = false`" and "`[[table]]` headers" to the subset |
| SI-4 | SC-005 "rejects … a missing `Vst3User` root" | No variant named, and SC-005 tests only one of FR-013's two required kinds | FR-013: "`RootsError::MissingRoot { kind }` for a missing `Vst3User` or `Vst3Global` root". SC-005: "a missing `Vst3User` root and a missing `Vst3Global` root (`MissingRoot { kind }`)" |
| SI-5 | Existing pieces row `ci.yml:39-42` | The concurrency block is at `ci.yml:41-43` (`concurrency:` at 41, `cancel-in-progress: true` at 43, read this session) | Cite `:41-43` |
| SI-6 | External facts row "This box: … rustc / cargo 1.98.1" | This session the box reports `rustc 1.99.1 (58cf8f9c3 2026-10-13)`, `cargo 1.99.1`, `clippy 0.1.99`: D4's `rustup update stable` has already run | Update the row; SC-001's "after `rustup update stable`" stays as the rule |
| SI-7 | FR-017 "`app.manage(SharedState::new(state))` (FR-103)" in the setup list; FR-103 "(`app.manage(SharedState::new(state))` in setup …)" | PD-1 assembles before the builder and registers with `Builder::manage`; compliance would read FR-017's literal "in setup" as unmet, and FR-045's `.manage(` string cannot tell the two apart | FR-017 and FR-103: "`run()` assembles the state before the builder and registers it with `tauri::Builder::manage(SharedState::new(state))`; `.setup` only schedules the locked-instance exit (PD-1)" |
| SI-8 | FR-013 "Its one other producer is `Roots::with_configured_plugin_root`"; FR-083 "(FR-015 makes any other `Roots` a clippy error)" | Not true as written: `with_configured_plugin_root` (directly, or through `assemble_with_os` / `reassemble_with_os` from a settings value) produced a `Roots` naming any absolute path, with no clippy error (plan review). PD-24 closes it with a sandbox | FR-013: add "`Roots` carries a private `sandbox: Option<PathBuf>`, `None` from `new`, set by `test_support::temp_roots_in` (crate-private `Roots::sandboxed`); `with_configured_plugin_root` on a sandboxed value panics on a path that is not a strict descendant of the sandbox base". FR-083: "(FR-015 makes any other `Roots` constructor a clippy error, and FR-013's sandbox makes every derivation of a test `Roots` panic outside its temp base)". New SC-027 case: a sandboxed `Roots` given an outside configured root panics, directly and through `assemble_with_os` / `reassemble_with_os` |
| SI-9 | FR-031 `save(state_dir: &Path, …)`, FR-032 `load(state_dir: &Path)`, FR-100 `StateLock::acquire(state_dir: &Path)`, FR-104 `RotatingLog { open(dir: &Path), open_with_cap(dir, cap_bytes) }`, FR-105 `init(logs_dir: &Path)`, FR-017 `settings::load(roots.state_dir())` | The writing APIs took bare paths, so a test could write the real state dir with every gate green (plan review, PD-25) | `save(roots: &Roots, …)`, `load(roots: &Roots)`, `StateLock::acquire(roots: &Roots)` (state dir `roots.state_dir()`), `RotatingLog::open(roots: &Roots)` / `open_with_cap(roots, cap_bytes)` writing `roots.logs_dir()`, `init(roots: &Roots)`; FR-017 "`settings::load(&roots)`", "`StateLock` on `roots`" |
| SI-10 | FR-012 "`Env { var }` … missing, empty, or not absolute"; FR-013 "requires every path absolute"; FR-033 `Invalid` "a relative or empty `linux_plugin_root`"; FR-102 "A relative path is `RootsError::Relative`" | Lexical "absolute" admits `..` and the filesystem root (PD-26) | FR-012: "missing, empty, not absolute, or with a `..` component". FR-013: "every path absolute and normal (no `..` component, at least one normal component; `RootsError::NotNormal { which }`)". FR-033 `Invalid` adds "a `linux_plugin_root` with a `..` component or naming a filesystem root". FR-102 adds "a path with a `..` component is `NotNormal { which: \"configured\" }`". SC-005, SC-010 and SC-027 gain the PD-26 cases (§7.1) |
| SI-11 | SC-006 / SC-008, SC-015, SC-029, SC-017 and FR-077's list | The plan adds tests the SCs do not name: `disallowed_methods_expectations_are_the_seven_d6_sites` (PD-22), `real_app_metadata_passes_the_guard` and `planted_app_metadata_changes_fail_the_guard` (PD-23), the two PD-24 sandbox tests, the PD-10 cases in `rotating_log_cases`, the PD-18 Vitest case | SC-008 adds "and `disallowed_methods_expectations_are_the_seven_d6_sites` (the D6 count is exactly seven, no lint-group suppression)"; SC-015 adds the two app-metadata tests; SC-027 adds the two sandbox tests; SC-029 adds "a line longer than the cap leaves no file over the cap, valid UTF-8; a failed rotation rename writes nothing". The Rust names join `required-tests.json` `all` (§4.18). The PD-18 Vitest case stays outside the pinned `vitest` list |

---

## 1. Inputs verified this session

### 1.1 Repo facts (file:line, read 2026-10-30 at `10593783`)

| File:line | Fact used |
|---|---|
| `specs/Krate-manager-roadmap.md:463-519` | Phase 4 components and measurable outcomes; `:473` the injection sentence D17 departs from; `:497` the apt line D16 departs from (`:496` ends "an Ubuntu leg with" and stays untouched, FR-090); `:882-887` Phase 12's first-launch bullet that gains the Windows-floor sentence |
| `specs/Krate-manager-roadmap.md:213-250` | Repository Layout the tree in §2 follows |
| `.gitignore:92-94` | `node_modules/`, `/package.json`, `/package-lock.json`; `git check-ignore` this session: `apps/krate-manager/{Cargo.lock, package.json lock, src/**, src-tauri/icons/**, tests/**, rust-toolchain.toml}` not ignored, `apps/krate-manager/node_modules/x` ignored by `:92`, `target/` and `dist/` not ignored |
| `.gitattributes` | Only four LF pins (`specs/_architecture_/*.json`, `specs/INDEX.md`, `.claude/workflows/*.js`); `git config core.autocrlf` = `true` on this box, so every committed text file checks out CRLF here. Every parser in this phase splits on `\r?\n` (Node) or uses `str::lines()` (Rust), and SC-012 adds a CRLF fixture variant |
| `.github/workflows/feed-tools.yml:3-34` | Push / PR path lists (identical sets), `workflow_dispatch` |
| `.github/workflows/feed-tools.yml:40-53` | `node-test` matrix `ubuntu-latest, macos-latest, windows-latest`; `actions/checkout@v6`, `actions/setup-node@v4`, Node `'24'` |
| `.github/workflows/feed-tools.yml:71-137` | `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" \| tee "$RUNNER_TEMP/tap.txt"` then `node - "$RUNNER_TEMP/tap.txt" <<'TAPCHECK'`; `PLAN = "1..57"` at `:78`, `NAMES` from `:79` |
| `.github/workflows/feed-tools.yml:142-144` | "Checkout unchanged": `test -z "$(git status --porcelain)"` |
| `.github/workflows/ci.yml:4-23` | Path filters list no `apps/**` |
| `.github/workflows/ci.yml:41-43` | `concurrency: group: ${{ github.workflow }}-${{ github.ref }}`, `cancel-in-progress: true` |
| `tools/feed-contract/tests/hygiene.test.js:2` | First statement `require('./helpers/write-guard.js');` |
| `tools/feed-contract/tests/hygiene.test.js:34-53` | `scannedFiles()`: `single` list `:35-45`, filtered by `fs.existsSync` at `:46`, plus `tools/feed-contract/*.js`, `tests/*.test.js`, `tests/helpers/*.js` |
| `tools/feed-contract/tests/hygiene.test.js:74-81` | `CHILD_EXEMPT`; the lint and both new test files stay non-exempt |
| `tools/feed-contract/tests/hygiene.test.js:113-148` | `tools stay offline (a)`: completeness list `missing` `:119-133`; per file: no network module, no `fetch(`, no `child_process` unless exempt, no destructured `fs` / `child_process` require |
| `tools/feed-contract/tests/hygiene.test.js:150-158` | `(b)`: every `tests/*.test.js` has the guard line as first statement |
| `tools/feed-contract/tests/hygiene.test.js:356-370` | `no serial test flags` scans `feed-tools.yml` and `tools/feed-contract/**/*.md` |
| `tools/feed-contract/tests/hygiene.test.js:612-622` | `SC-021: no test file sets a serial concurrency option` (regex `concurrency:\s*(1\|false)`) over every test JS file: the two new files must not match |
| `tools/feed-contract/tests/helpers/write-guard.js:1-20` | Wraps every `fs` write: a path outside `realpath(os.tmpdir())` throws `E_WRITE_OUTSIDE_TMP`; `child_process` limited to `git`, `tar`, `rsign` |
| `tools/feed-contract/tests/workflow-shape.test.js:18-113` | Text helpers (`readWorkflow`, `indentOf`, `jobBlock`, `stepBlocks`, `field`, `runText`, `stepByName`, `count`) whose form PD-20 copies |
| `tools/lint-ieee-flag-sets.js:1-17` | Lint form: shebang, title rule, WHY, usage, exit codes 0 / 1 / 2, `'use strict'` |
| `plugins/vorago/version.json`, `plugins/membrum/version.json` | `{version, name, description, publisher, url, copyright[, preset_subdir]}` |
| `plugins/vorago/CHANGELOG.md:1-8` | Keep a Changelog 1.1.0 header wording, `## [x.y.z] - date`, `### Added` |
| `plugins/shared/src/platform/preset_paths.cpp:8-31` | User presets `USERPROFILE` / `HOME` + `Documents/Krate Audio/<P>` |
| `plugins/shared/src/platform/preset_paths.cpp:33-51` | Factory presets `PROGRAMDATA\Krate Audio\<P>`; macOS `/Library/Application Support/Krate Audio/<P>`; Linux `/usr/share/krate-audio/<lower>` |
| `specs/krate-manager/probes/macos-gatekeeper-probe.js:135-141` | Q6: `plutil -p …/Info.plist` for `LSFileQuarantineEnabled`; `codesign --verify --deep --strict -v`; `codesign -dv` → `Signature=adhoc` |
| `specs/krate-manager/probes/results.md:11`, `:17`, `:18` | No `LSFileQuarantineEnabled` from the bundler; `codesign --verify --deep --strict` passes on the ad-hoc Tauri bundle (tauri-cli 2.12.4); a quarantined ad-hoc app is blocked at launch |
| `tools/gen-specs-index.js:1-8`; `specs/INDEX.md:202-204` | `--check` mode; Phases 1-3 rows |
| `LICENSE:1-3` | MIT License, Copyright (c) 2025 Roland Zwaga → `license = "MIT"` in `[workspace.package]` |

### 1.2 External facts (verified 2026-10-30)

| Fact | Source |
|---|---|
| Crate versions: `tauri` 2.12.5, `tauri-build` 2.7.4, `log` 0.4.34, `url` 2.5.8, `tempfile` 3.27.0, `thiserror` 2.0.21, `serde` 1.0.229, `serde_json` 1.0.154 (`max_stable_version`) | `https://crates.io/api/v1/crates/<name>` |
| `tauri` 2.12.5 depends on `serde_json "1"`, `serde "1"` (derive), `url "2"`, `thiserror "2"`, `log "0.4.21"`, so the core's six crates add only `tempfile` to the app's tree | `crates/tauri/Cargo.toml:45-76` at tag `tauri-v2.12.5` |
| npm: `@tauri-apps/cli` 2.12.4, `@tauri-apps/api` 2.12.4, `vite` 8.3.5 (engines `^20.19.0 \|\| >=22.12.0`), `vitest` 5.0.5 (engines `^22.12.0 \|\| ^24.0.0 \|\| >=26.0.0`, peer `vite ^6.4.0 \|\| ^7.0.0 \|\| ^8.0.0`), `jsdom` 29.1.1 (engines `… >=24.0.0`), `typescript` 6.0.3, `eslint` 10.14.0 (engines `… >=24`), `@eslint/js` 10.0.1 (peer `eslint ^10.0.0`), `typescript-eslint` 8.72.1 (peer `typescript >=4.8.4 <6.1.0`), `@types/node` 26.6.6 | `npm view <pkg> version engines peerDependencies` |
| This box: `rustc 1.99.1`, `cargo 1.99.1`, `clippy 0.1.99`, Node v24.7.0, npm 11.10.0 | `rustc --version`, `cargo --version`, `cargo clippy --version`, `node --version`, `npm --version` |
| `tauri_build::try_build(attributes: Attributes) -> Result<()>` (`src/lib.rs:652`); `Attributes::app_manifest(mut self, manifest: AppManifest) -> Self` (`:552`); `AppManifest::new()`, `AppManifest::commands(mut self, commands: &'static [&'static str]) -> Self` ("permissions autogenerated in the format of `allow-$command` and `deny-$command`", `src/acl.rs:91-120`); app permissions written to `./permissions/autogenerated` (`src/acl.rs:281`); release builds emit `MACOSX_DEPLOYMENT_TARGET=<minimumSystemVersion>` (`src/lib.rs:779-782`); the Windows resource gets `FileVersion` / `ProductVersion` = `config.version` (`src/lib.rs:823-830`) | `tauri-build` at tag `tauri-build-v2.7.4` |
| `tauri::plugin::Builder::new(name: &'static str)` (`plugin.rs:281`), `on_navigation<F>(self, F) -> Self where F: Fn(&Webview<R>, &Url) -> bool + Send + 'static` (`:459-465`), `build(self) -> TauriPlugin<R, C>` (`:754`); `pub use url::Url;` (`lib.rs:92`); `pub const fn is_dev() -> bool { !cfg!(feature = "custom-protocol") }` (`lib.rs:316-319`); `AppHandle::exit(&self, exit_code: i32)` (`app.rs:581`, in `impl<R: Runtime> AppHandle<R>` from `:494`); `Builder::setup` (`app.rs:1838-1843`), `Builder::manage<T>(self, state: T) -> Self` (`:2055`), `Builder::plugin` (`:1962`), `invoke_handler` (`:1723`) | `tauri` at tag `tauri-v2.12.5` |
| With an app ACL manifest every app command is ACL-checked; the channel fetch command is exempt | `crates/tauri/src/webview/mod.rs:2076-2108` at `tauri-v2.12.5` |
| `#[tauri::command]` expansion emits `#[allow(unused_imports)]`, `#[allow(unused_variables)]`, `#[allow(unused)]` | `crates/tauri-macros/src/command/wrapper.rs:285, 329, 332, 344` at `tauri-v2.12.5` |
| Clippy's `allow_attributes` skips attributes whose span is `from_expansion()` or from a proc macro; `allow_attributes_without_reason` skips `in_external_macro` / proc-macro attributes. So the Tauri macros' `#[allow]`s do not trip FR-002's two `deny`s | `rust-lang/rust-clippy` `clippy_lints/src/attrs/allow_attributes.rs:10-14`, `allow_attributes_without_reason.rs:19` (default branch, read 2026-10-30) |
| `tauri-cli` renames the built binary only when `mainBinaryName` is set (`rename_app`, `desktop.rs:333-349`), so `tauri build` leaves `target/release/krate-manager(.exe)`; it adds `tauri/custom-protocol` to the build features (`rust.rs:481`); `--color always` only for `dev` (`desktop.rs:71-72`) | `tauri-cli` at tag `tauri-cli-v2.12.4` |
| `create-tauri-app` templates at `c6f5cf68`: vanilla-ts `package.json` (`"build": "tsc && vite build"`, `vite ^8.0.16`, `typescript ~6.0.3`, `@tauri-apps/plugin-opener`), `vite.config.ts` (`// @ts-expect-error … @types/node`, `process.env.TAURI_DEV_HOST`, port 1420, `strictPort`, HMR 1421, ignore `**/src-tauri/**`), `tsconfig.json` (target ES2020, `module: ESNext`, `lib ["ES2020","DOM"]`, `skipLibCheck`, bundler mode, `allowImportingTsExtensions`, `resolveJsonModule`, `isolatedModules`, `noEmit`, `strict`, `noUnusedLocals`, `noUnusedParameters`, `noFallthroughCasesInSwitch`, `include ["src"]`); `_base_/src-tauri`: `Cargo.toml` (edition 2024, `[lib] crate-type = ["staticlib","cdylib","rlib"]`, `[profile.release]` as PD-14), `build.rs` (`tauri_build::build()`), v2 `tauri.conf.json` (`"$schema": "https://schema.tauri.app/config/2"`, window 800×600, `csp: null`, `targets: "all"`, five icon paths), `lib.rs` (`greet`, `.plugin(tauri_plugin_opener::init())`, `#[cfg_attr(mobile, tauri::mobile_entry_point)]`), `main.rs` (`#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]`), icons `32x32.png 128x128.png 128x128@2x.png icon.icns icon.ico …` | `gh api repos/tauri-apps/create-tauri-app/contents/templates/…?ref=c6f5cf68942f187ba7bb738a49ee505c28e6ca5a` |
| `@tauri-apps/api/mocks`: `mockIPC(cb: (cmd: string, payload?: InvokeArgs) => unknown, options?)` (`mocks.ts:143-146`); the mocked `invoke` is `async` and returns `cb(cmd, args)`, so a throwing callback rejects the call (`:191-199`); `clearMocks()` (`:355`) | `packages/api/src/mocks.ts` at tag `@tauri-apps/api-v2.12.4` |
| Vitest: with an existing Vite config, add `/// <reference types="vitest/config" />` to get the `test` types | vitest `docs/guide/index.md:29-32` (default branch; latest release `v5.0.5`) |
| `File::try_lock(&self) -> Result<(), TryLockError>`; `TryLockError { Error(io::Error), WouldBlock }` (1.89.0); "Returns `Err(TryLockError::WouldBlock)` if a different lock is already held on this file (via another handle/descriptor)"; released when the handle is closed; `flock(LOCK_EX \| LOCK_NB)` on Unix, `LockFileEx` on Windows; "On Windows, locking a file will fail if the file is opened only for append" | https://doc.rust-lang.org/std/fs/struct.File.html#method.try_lock ; https://doc.rust-lang.org/std/fs/enum.TryLockError.html |
| `log::set_logger(&'static dyn Log) -> Result<(), SetLoggerError>` "may only be called once", available without the `std` feature; `set_max_level(LevelFilter)`; the runtime max level starts at 0 = `Off` (`static MAX_LOG_LEVEL_FILTER: AtomicUsize = AtomicUsize::new(0);`, `src/lib.rs:467`), so a test logger must call `set_max_level` | https://docs.rs/log/0.4.34/log/fn.set_logger.html ; `rust-lang/log` `src/lib.rs` at tag `0.4.34` |
| `tempfile::Builder::prefix(&mut self, &S) -> &mut Self` (default `.tmp`), `tempfile_in(&self, dir) -> io::Result<NamedTempFile>`; `NamedTempFile::persist(self, P) -> Result<F, PersistError<F>>` "will atomically replace it"; `as_file`, `keep` | https://docs.rs/tempfile/3.27.0/tempfile/struct.Builder.html ; https://docs.rs/tempfile/3.27.0/tempfile/struct.NamedTempFile.html |
| Clippy `disallowed-methods` resolves a **trait associated function** path: with `{ path = "ka::os_roots::FromOs::from_os" }`, both `kc::Roots::from_os()` and `<kc::Roots as FromOs>::from_os()` in a test gave `error: use of a disallowed method \`ka::os_roots::FromOs::from_os\``; a statement-level `#[expect(clippy::disallowed_methods, reason = …)]` on the setup call and on an inherent `kc::Roots::new` call inside the trait impl were fulfilled (the lib target passed `-D warnings` with `allow_attributes` and `allow_attributes_without_reason` denied); each diagnostic carries `= help: … index.html#disallowed_methods`, the first also `` `-D clippy::disallowed-methods` implied by `-D warnings` ``; the run ends `error: could not compile \`ka\` (lib test) due to 2 previous errors` | Scratch two-crate workspace, clippy 0.1.99, this session (deleted after) |
| libtest prints `test <path> ... ok` and, for `#[should_panic]`, `test <path> - should panic ... ok`; a `#[path = "lib_tests.rs"] mod tests;` in `src/lib.rs` yields test paths `tests::…` | Same scratch workspace, `cargo test` |
| Tauri Debian prerequisites line: `libwebkit2gtk-4.1-dev build-essential curl wget file libxdo-dev libssl-dev libayatana-appindicator3-dev librsvg2-dev` | https://v2.tauri.app/start/prerequisites/ |
| `Swatinem/rust-cache` latest `v2.9.3`; `actions/checkout` latest `v7.0.1`, `actions/setup-node` latest `v7.1.0` (the plan keeps `@v6` / `@v4` to match `feed-tools.yml:47-50`, FR-070) | `gh api repos/<owner>/<repo>/releases/latest` |

Every other external fact the spec relies on (config keys, CSP page, capability docs, merge-patch platform files,
runner labels, XDG, WOW64, clippy configuration lookup, cargo target fields, `#[path]` rule, Vitest reporters,
`fileParallelism`, npm/cli#4828, tauri#13419) is cited in the spec's "External facts used" table and is used here
unchanged.

---

## 2. File layout

```
apps/krate-manager/
  Cargo.toml                    virtual workspace (§4.1)
  Cargo.lock                    committed; every CI cargo command passes --locked
  clippy.toml                   the eight disallowed methods (§4.1)
  rust-toolchain.toml           stable, clippy + rustfmt, minimal profile
  .gitignore                    PD-15
  version.json                  {"version": "0.1.0", "name": "Krate Manager"}
  CHANGELOG.md                  Keep a Changelog header + "## [0.1.0] - Unreleased"
  package.json, package-lock.json
  tsconfig.json, tsconfig.node.json, vite.config.ts, eslint.config.mjs
  index.html
  src/main.ts, src/styles.css, src/main.test.ts
  crates/core/                  krate-manager-core (lib krate_manager_core)
    Cargo.toml
    src/lib.rs
    src/platform/mod.rs + mod_tests.rs
    src/platform/windows.rs + windows_tests.rs     #[cfg(target_os = "windows")]
    src/platform/macos.rs + macos_tests.rs         #[cfg(target_os = "macos")]
    src/platform/linux.rs + linux_tests.rs         #[cfg(target_os = "linux")]
    src/roots.rs + roots_tests.rs
    src/settings.rs + settings_tests.rs
    src/state_lock.rs + state_lock_tests.rs
    src/log_sink.rs + log_sink_tests.rs
    src/context.rs + context_tests.rs
    src/navigation.rs + navigation_tests.rs
    src/test_support.rs + test_support_tests.rs    cfg(any(test, feature = "test-support"))
    tests/injection.rs                             FR-021, SC-008
    tests/config_guard.rs                          FR-045, SC-015
  crates/helper/                krate-manager-helper
    Cargo.toml, src/lib.rs, src/main.rs, tests/stub.rs
  src-tauri/                    krate-manager (lib krate_manager_lib, bin krate-manager); no tests/ directory
    Cargo.toml, build.rs
    tauri.conf.json, tauri.windows.conf.json, tauri.macos.conf.json, tauri.linux.conf.json
    capabilities/main.json
    icons/                      the template set, unchanged
    src/main.rs, src/lib.rs, src/os_roots.rs, src/commands.rs, src/navigation.rs, src/log_sink.rs,
    src/dev_feed.rs             (compiled only with feature dev-feed)
  tests/required-tests.json     FR-077 (§4.18)
  tests/fixtures/clippy-guard/  Cargo.toml, Cargo.lock, src/lib.rs (excluded from the workspace)
.github/workflows/manager-ci.yml          new (§4.16)
.github/workflows/feed-tools.yml          paths + PLAN + NAMES (§4.17)
tools/lint-manager-version.js             new (§4.15)
tools/feed-contract/tests/lint-manager-version.test.js   new (SC-012)
tools/feed-contract/tests/manager-ci-shape.test.js       new (SC-019)
tools/feed-contract/tests/hygiene.test.js                one path in two lists (PD-19)
specs/Krate-manager-roadmap.md            status line, :473, :497, Phase 12 bullet (§4.19)
specs/INDEX.md                            regenerated
```

---

## 3. Error model

Rust errors use `thiserror` 2 derives; every user-facing or logged error carries a stable `code()`.

| Type (module) | Variants | `code()` |
|---|---|---|
| `RootsError` (`roots`) | `Env { var: &'static str }` (required variable missing, empty, relative, or with a `..` component, PD-26); `Relative { which: &'static str }` (`which` ∈ `"plugin_root"`, `"presets.factory"`, `"presets.user"`, `"state_dir"`, `"staging_dir"`, `"configured"`); `NotNormal { which: &'static str }` (same `which` set; a `..` component or no normal component, PD-26); `MissingRoot { kind: RootKind }` (PD-4); `Staging` (`staging_dir != state_dir/staging`) | none of its own; wrapped by `StartupError::Roots` (`E_STARTUP_ROOTS`) and by `reassemble` |
| `LockError` (`state_lock`) | `Held`; `Io { op: LockStep, source: io::Error }`, `LockStep` ∈ `CreateDir` / `Open` / `Lock` (`as_str`: `create-dir`, `open`, `lock`) | mapped by `StartupError` |
| `StartupError` (`context`), `Clone` (PD-3) | `Roots(RootsError)`; `Locked`; `LockIo { op: LockStep, source: Arc<io::Error> }` | `E_STARTUP_ROOTS`, `E_STARTUP_LOCKED`, `E_STARTUP_LOCK_IO` |
| `SettingsError` (`settings`) | `Corrupt { detail: String }`; `FutureVersion { found: u64 }`; `Invalid { reason: &'static str }` (`"schema_version below 1"` (PD-8), `"linux_plugin_root is relative"`, `"linux_plugin_root is empty"`, `"linux_plugin_root is not normalised"`, `"linux_plugin_root is a filesystem root"` (PD-26)); `NotUtf8`; `Io { op: SettingsStep, source: io::Error }`, `SettingsStep` ∈ `ReadExisting`, `CreateDir`, `CreateTemp`, `Write`, `SyncTemp`, `Persist`, `SyncDir` (`as_str`: `read-existing`, `create-dir`, `create-temp`, `write`, `sync-temp`, `persist`, `sync-dir`) | `E_SETTINGS_CORRUPT`, `E_SETTINGS_FUTURE_VERSION`, `E_SETTINGS_INVALID`, `E_SETTINGS_PATH_NOT_UTF8`, `E_SETTINGS_IO` |
| `GuardError` (`test_support`) | `Canonicalize { which: &'static str, source: io::Error }` (`which` ∈ `"base"`, `"temp_root"`); `Outside { base: PathBuf, temp_root: PathBuf }` | test-only |

Display strings name the code first (`"E_SETTINGS_IO: write failed at sync-temp: …"`), so a log line always holds it.

**Tauri command payloads (the whole Phase 4 IPC surface).** Neither command returns `Result`; neither takes arguments.

| Command | Rust signature | JSON result | TypeScript call |
|---|---|---|---|
| `app_version` | `fn app_version() -> &'static str` | `"0.1.0"` | `invoke<string>("app_version")` |
| `startup_error` | `fn startup_error(state: tauri::State<'_, SharedState>) -> Option<&'static str>` | `"Krate Manager is already running"` or `null` | `invoke<string \| null>("startup_error")` |

**Node lint** (`tools/lint-manager-version.js`): exit 0 clean, 1 with ≥ 1 error (one stderr line each,
`lint-manager-version: <CODE>: <file>: <message>`), 2 usage error; codes per FR-063. **Helper stub:** exit 2, one stderr
line. **CI steps:** a `::notice` (pass) or `::error` (fail) per assertion (D21).

---

## 4. Module design

### 4.1 Workspace manifests

`apps/krate-manager/Cargo.toml`:

```toml
[workspace]
members = ["crates/core", "crates/helper", "src-tauri"]
exclude = ["tests/fixtures/clippy-guard"]
resolver = "3"

[workspace.package]
version = "0.1.0"
edition = "2024"
rust-version = "1.90"
license = "MIT"
publish = false

[workspace.dependencies]
krate-manager-core = { path = "crates/core" }
log = "0.4.34"
serde = { version = "1.0.229", features = ["derive"] }
serde_json = "1.0.154"
tauri = { version = "2.12.5", features = [] }
tauri-build = { version = "2.7.4", features = [] }
tempfile = "3.27.0"
thiserror = "2.0.21"
url = "2.5.8"

[workspace.lints.clippy]
allow_attributes = "deny"
allow_attributes_without_reason = "deny"

[profile.release]
codegen-units = 1
lto = true
opt-level = 3
panic = "abort"
strip = true
```

`[workspace.dependencies]` lines are inline tables, but the version lint reads only `[workspace]` and
`[workspace.package]` of this file (D18, PD-13), so they are outside its subset by design. `resolver = "3"` makes
resolution prefer versions whose `rust-version` fits 1.90 (https://doc.rust-lang.org/cargo/reference/resolver.html#rust-version);
that only shapes `Cargo.lock`.

`apps/krate-manager/crates/core/Cargo.toml` (FR-002, FR-007, FR-022):

```toml
[package]
name = "krate-manager-core"
description = "Krate Manager core: platform roots, settings, startup state (Tauri-free)"
version.workspace = true
edition.workspace = true
rust-version.workspace = true
license.workspace = true
publish.workspace = true

[features]
test-support = []

[dependencies]
log = { workspace = true }
serde = { workspace = true }
serde_json = { workspace = true }
tempfile = { workspace = true }
thiserror = { workspace = true }
url = { workspace = true }

[lints]
workspace = true
```

There is no `[lib]` table, so the library is `krate_manager_core`, the package name with `-` turned into `_`, which is
cargo's default target name (https://doc.rust-lang.org/cargo/reference/cargo-targets.html#the-name-field). There are
no `[dev-dependencies]`: core's unit tests reach `test_support` through `cfg(test)`, and its two integration tests only
read files with `std` and `serde_json`. The version lint reads only `[package]` here (D18), and every line in that
section is in the PD-13 subset. `config_guard.rs`'s `check_app_meta` (PD-23) asserts:
- `[lints]` holds `workspace = true` in all three member manifests;
- `[features]` here is exactly `test-support = []`;
- `test-support` appears in the helper and src-tauri manifests only inside `[dev-dependencies]`.

`core_dependencies_are_the_fr007_set` asserts the `[dependencies]` key set.

`apps/krate-manager/clippy.toml` (FR-015; clippy finds it from each member's `CARGO_MANIFEST_DIR` by walking up):

```toml
disallowed-methods = [
  { path = "krate_manager_lib::os_roots::FromOs::from_os", reason = "production roots only in app setup; tests use temp_roots()" },
  { path = "krate_manager_core::roots::Roots::new", reason = "tests build roots with temp_roots(); production with Roots::from_os()" },
  { path = "std::env::var", reason = "the environment is read only in os_roots.rs and test_support (D9)" },
  { path = "std::env::var_os", reason = "the environment is read only in os_roots.rs and test_support (D9)" },
  { path = "std::env::vars", reason = "the environment is read only in os_roots.rs and test_support (D9)" },
  { path = "std::env::vars_os", reason = "the environment is read only in os_roots.rs and test_support (D9)" },
  { path = "std::env::home_dir", reason = "the environment is read only in os_roots.rs and test_support (D9)" },
  { path = "std::env::temp_dir", reason = "the environment is read only in os_roots.rs and test_support (D9)" },
]
```

`rust-toolchain.toml`: `[toolchain]` `channel = "stable"`, `components = ["clippy", "rustfmt"]`, `profile = "minimal"`
(D4).

`version.json`: `{"version": "0.1.0", "name": "Krate Manager"}` plus final LF (FR-061).

`CHANGELOG.md` (FR-062): the `plugins/vorago/CHANGELOG.md:1-6` header with "Krate Manager", then
`## [0.1.0] - Unreleased`, `### Added`, `- Scaffold: Tauri 2 app, platform abstraction, settings model and the manager CI.`

**The seven D6 `#[expect]` sites** (each `#[expect(clippy::disallowed_methods, reason = "<one line>")]` on one
statement; any other suppression in the workspace is a defect, and
`disallowed_methods_expectations_are_the_seven_d6_sites` counts them per file and rejects any `clippy::style`,
`clippy::all` or `warnings` suppression, PD-22):

| # | File | Statement | Reason text |
|---|---|---|---|
| 1 | `src-tauri/src/lib.rs` `run()` | `let roots = Roots::from_os();` | `"the one production Roots, built once at startup"` |
| 2 | `src-tauri/src/os_roots.rs` | `let value = std::env::var_os(key);` | `"the only process-environment read for roots (D9)"` |
| 3 | `src-tauri/src/os_roots.rs` | `let roots = Roots::new(paths);` | `"from_os is the production constructor"` |
| 4 | `crates/core/src/test_support.rs` `temp_roots_in` | `let roots = Roots::new(paths)…;` | `"temp_roots is the test constructor"` |
| 5 | `crates/core/src/test_support.rs` `canonical_temp_dir` | `let raw = std::env::temp_dir();` | `"the one temp-dir read, for the test guard"` |
| 6 | `crates/core/src/roots_tests.rs` `new_validates_and_touches_nothing` | `let result = Roots::new(paths);` (one `let` per case is one site only if the cases go through one helper `fn try_new(p: RootPaths) -> Result<Roots, RootsError>` holding the single call) | `"the Roots::new validation test"` |
| 7 | `crates/core/src/test_support_tests.rs` `check_under_temp_symlink_escape` | `let on_ci = std::env::var_os("CI").is_some();` | `"symlink privilege failures fail only on CI"` |

### 4.2 `core::platform` (`crates/core/src/platform/`)

`mod.rs`:

```rust
pub const APP_IDENTIFIER: &str = "com.krateaudio.krate-manager";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Os { Windows, Macos, Linux }

/// Process-environment snapshot; built from literal pairs in tests and by `FromOs` in production.
#[derive(Clone, Debug, Default)]
pub struct OsEnv { pairs: Vec<(String, OsString)> }
impl OsEnv {
    pub fn from_pairs<'a>(pairs: impl IntoIterator<Item = (&'a str, &'a OsStr)>) -> Self;
    /// Windows: ASCII-case-insensitive name match (`#[cfg(windows)]`); elsewhere exact.
    pub fn get(&self, name: &str) -> Option<&OsStr>;
}

pub trait Platform {
    fn os(&self) -> Os;
    fn default_roots(&self, env: &OsEnv) -> Result<RootPaths, RootsError>;
}

#[cfg(target_os = "windows")] mod windows;
#[cfg(target_os = "windows")] pub use windows::{WindowsPlatform as NativePlatform, ENV_KEYS};
#[cfg(target_os = "macos")] mod macos;
#[cfg(target_os = "macos")] pub use macos::{MacosPlatform as NativePlatform, ENV_KEYS};
#[cfg(target_os = "linux")] mod linux;
#[cfg(target_os = "linux")] pub use linux::{LinuxPlatform as NativePlatform, ENV_KEYS};

/// Missing, empty, relative, or with a `Component::ParentDir` → `RootsError::Env { var }` (PD-26).
/// Pure: `Path::is_absolute` and `Path::components` are lexical.
fn required_abs(env: &OsEnv, var: &'static str) -> Result<PathBuf, RootsError>;

#[cfg(test)] #[path = "mod_tests.rs"] mod tests;
```

`pub type NativePlatform` (FR-010) is realised as the `pub use … as NativePlatform` re-export (a unit struct, so
`NativePlatform.default_roots(&env)` works as written in the spec). `ENV_KEYS: &[&str]` is the D9 key list of the
compiled OS; `os_roots.rs` iterates it, so the key list lives once, beside the code that reads it.

Per-OS implementations (each a `pub struct XPlatform;` with `impl Platform`), all pure (FR-011), in the D8 order:

| | Windows (`windows.rs`) | macOS (`macos.rs`) | Linux (`linux.rs`) |
|---|---|---|---|
| `ENV_KEYS` | `["LOCALAPPDATA", "CommonProgramW6432", "ProgramData", "USERPROFILE"]` | `["HOME"]` | `["HOME", "XDG_DATA_HOME"]` |
| `plugin_roots` | `Vst3User` = LOCALAPPDATA+`Programs`+`Common`+`VST3`; `Vst3Global` = CommonProgramW6432+`VST3` | `Vst3User` = HOME+`Library`+`Audio`+`Plug-Ins`+`VST3`; `Vst3Global` `"/Library/Audio/Plug-Ins/VST3"`; `AuUser` = HOME+`Library`+`Audio`+`Plug-Ins`+`Components`; `AuGlobal` `"/Library/Audio/Plug-Ins/Components"` | `Vst3User` = HOME+`.vst3`; `Vst3Global` ×4: `"/usr/lib64/vst3"`, `"/usr/lib/vst3"`, `"/usr/local/lib64/vst3"`, `"/usr/local/lib/vst3"` |
| `presets.factory` | ProgramData+`Krate Audio` | `"/Library/Application Support/Krate Audio"` | HOME+`Documents`+`Krate Audio` |
| `presets.user` | USERPROFILE+`Documents`+`Krate Audio` | HOME+`Documents`+`Krate Audio` | HOME+`Documents`+`Krate Audio` |
| `state_dir` | LOCALAPPDATA+`APP_IDENTIFIER` | HOME+`Library`+`Application Support`+`APP_IDENTIFIER` | (XDG_DATA_HOME if set, non-empty and absolute, else HOME+`.local`+`share`)+`APP_IDENTIFIER` |
| `staging_dir` | state+`staging` | state+`staging` | state+`staging` |

The `"staging"` component is in `roots.rs` (`pub const STAGING_DIR: &str = "staging";`) and used by every
implementation, so `Roots::new`'s `Staging` check and `default_roots` agree by construction. Required variables are
read in the `ENV_KEYS` order, so the first error names the first missing one (`LOCALAPPDATA` / `HOME`, SC-024(a)).
Each per-OS `default_roots_from_env` test gains one PD-26 case: its first required variable with a `..` component →
`Env { var }`. The cases are `C:\km-fixture\..\x` for `LOCALAPPDATA`, `/Users/u/../x` for macOS `HOME` and
`/home/u/../x` for Linux `HOME`, all test-file literals that are never touched. A Linux `XDG_DATA_HOME` with a `..`
component is treated like a relative one and falls back to `$HOME/.local/share`.

The `mod.rs` and per-OS files carry string literals only in attributes (the six `cfg(target_os = …)`, the four
`#[path = "<stem>_tests.rs"]`), which PD-5 exempts, plus `PLATFORM_LITERALS` members.

### 4.3 `core::roots` (`crates/core/src/roots.rs`)

```rust
pub const STAGING_DIR: &str = "staging";
pub const LOGS_DIR: &str = "logs";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RootKind { Configured, Vst3User, Vst3Global, AuUser, AuGlobal }

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PluginRoot { pub kind: RootKind, pub path: PathBuf }

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PresetRoots { pub factory: PathBuf, pub user: PathBuf }

/// Unvalidated input to `Roots::new` (public fields; the only way in is `Roots::new`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RootPaths { pub plugin_roots: Vec<PluginRoot>, pub presets: PresetRoots, pub state_dir: PathBuf, pub staging_dir: PathBuf }

/// Validated roots. No `Default`, no `From`, no public field.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Roots {
    plugin_roots: Vec<PluginRoot>, presets: PresetRoots, state_dir: PathBuf, staging_dir: PathBuf,
    sandbox: Option<PathBuf>,                                             // PD-24: None in production
}

impl Roots {
    pub fn new(paths: RootPaths) -> Result<Roots, RootsError>;            // FR-013; clippy-disallowed outside D6 sites
    pub fn with_configured_plugin_root(&self, root: Option<&Path>) -> Result<Roots, RootsError>; // FR-102, PD-24, PD-26
    pub(crate) fn sandboxed(self, base: PathBuf) -> Roots;                // PD-24; called only by test_support
    pub fn plugin_roots(&self) -> &[PluginRoot];
    pub fn presets(&self) -> &PresetRoots;
    pub fn state_dir(&self) -> &Path;
    pub fn staging_dir(&self) -> &Path;
    pub fn logs_dir(&self) -> PathBuf;                                    // state_dir/logs (FR-104)
    /// The first `Vst3User` root; `new` guarantees one exists and `with_configured_plugin_root` never removes it.
    pub fn vst3_user(&self) -> &Path;
}
```

`new` checks, in this order:
1. every path is absolute (`Relative { which }`);
2. every path is normal, with no `Component::ParentDir` and at least one `Component::Normal` (`NotNormal { which }`, PD-26);
3. there is at least one `Vst3User` and one `Vst3Global` (`MissingRoot { kind }`);
4. `staging_dir == state_dir.join(STAGING_DIR)` (`Staging`).

It sets `sandbox: None`.

`with_configured_plugin_root` removes any `Configured` entry. Then, for `Some(x)`:
- `x` relative → `Relative { which: "configured" }`;
- `x` not normal → `NotNormal { which: "configured" }`;
- when `self.sandbox` is `Some(b)` and `x` is not a strict, component-wise descendant of `b` (`x != b && x.starts_with(b)`) → **panic** naming `x` and `b` (PD-24). The check runs after the normal-path check, so a lexical `starts_with` cannot be fooled by `..`;
- `x` equal (`Path ==`, component-wise, so `/a/./b` equals `/a/b`) to a remaining entry's path → no entry added;
- otherwise `PluginRoot { Configured, x }` is inserted at index 0.

It builds the result with a struct literal inside `roots.rs`, never through `new` (FR-013), and copies `sandbox`.
`sandboxed` sets `sandbox: Some(base)` and nothing else. The injection scanner reports the identifier `sandboxed` in
any scanned file other than `roots.rs` (§4.10), and `test_support.rs` (unscanned) is its one caller. Nothing in the
file touches the filesystem (FR-021 token rule).

The sandbox comparison is lexical. Tests therefore derive a configured path from the roots themselves:
`roots.state_dir().parent()` is the canonical base, `\\?\`-prefixed on Windows and `/private/var/…` on macOS. A spelling
of the same folder that is not canonical (the `TempDir::path()` form) is outside by this test and panics, which errs
safe. Symlinks inside the base are not resolved, because Phase 4 writes nothing into plug-in roots. The writing phases
(8 and 9) canonicalise the install target before their own containment check (§6).

### 4.4 `core::test_support` (`crates/core/src/test_support.rs`)

```rust
pub fn canonical_temp_dir() -> PathBuf;                                    // D6 site 5; panics if canonicalize fails
pub fn check_under_temp(base: &Path, temp_root: &Path) -> Result<PathBuf, GuardError>;
fn temp_roots_in(base: &Path, temp_root: &Path) -> Roots;                  // private; child-module tests (D6 site 4)
pub fn temp_roots(base: &Path) -> Roots;                                   // temp_roots_in(base, &canonical_temp_dir())
pub fn fresh_temp_roots() -> (tempfile::TempDir, Roots);
```

`temp_roots_in`: `check_under_temp(base, temp_root)` first, `panic!` on `Err` (message names both paths) before any
directory exists; then builds `RootPaths` from the canonical base with one `join` per component (PD-6):
`plugins/vst3-user` (`Vst3User`), `plugins/vst3-global` (`Vst3Global`), and on macOS `plugins/au-user`,
`plugins/au-global`; `presets/factory`, `presets/user`; `state`, `state/staging`; then `Roots::new(...)` (an `expect`
with message: the paths are valid by construction), `.sandboxed(canonical_base)` (PD-24; the same statement, so still
D6 site 4) and `create_dir_all` for each. On Windows every root is a
`\\?\`-prefixed canonical path; that is consistent across the whole `Roots` value and accepted by every `std::fs` call
and by `tempfile`.

### 4.5 `core::settings` (`crates/core/src/settings.rs`)

```rust
pub const SETTINGS_FILE: &str = "settings.json";
pub const SCHEMA_VERSION: u64 = 1;
const TEMP_PREFIX: &str = ".settings.json.";

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Settings { pub linux_plugin_root: Option<PathBuf>, pub linux_graphics_workaround: bool }
impl Settings {
    pub fn linux_plugin_root_or_default<'a>(&'a self, roots: &'a Roots) -> &'a Path; // stored path or roots.vst3_user()
}
pub fn validate(settings: &Settings) -> Result<(), SettingsError>;       // NotUtf8 / Invalid (relative, empty, `..`, fs root; PD-26)

#[derive(Debug)]
pub struct LoadOutcome { pub settings: Settings, pub error: Option<SettingsError> }
pub fn load(roots: &Roots) -> LoadOutcome;                               // reads roots.state_dir(); NotFound → defaults (PD-25)
pub fn save(roots: &Roots, settings: &Settings) -> Result<(), SettingsError>; // save_with(&mut StdSettingsIo, roots, …)

pub(crate) fn classify(bytes: &[u8]) -> Result<Settings, SettingsError>; // PD-7, PD-8
pub(crate) fn encode(settings: &Settings) -> Result<Vec<u8>, SettingsError>; // serde_json::to_vec_pretty + b'\n'

pub(crate) trait SettingsIo {
    fn read_existing(&mut self, path: &Path) -> io::Result<Option<Vec<u8>>>;        // NotFound → Ok(None)
    fn create_dir(&mut self, dir: &Path) -> io::Result<()>;                           // create_dir_all
    fn create_temp(&mut self, dir: &Path) -> io::Result<NamedTempFile>;              // Builder::new().prefix(TEMP_PREFIX).tempfile_in(dir)
    fn write(&mut self, tmp: NamedTempFile, bytes: &[u8]) -> io::Result<NamedTempFile>;
    fn sync_temp(&mut self, tmp: NamedTempFile) -> io::Result<NamedTempFile>;        // tmp.as_file().sync_all()
    fn persist(&mut self, tmp: NamedTempFile, target: &Path) -> io::Result<()>;       // tmp.persist(target).map_err(|e| e.error)
    #[cfg(unix)] fn sync_dir(&mut self, dir: &Path) -> io::Result<()>;                 // File::open(dir)?.sync_all()
}
pub(crate) fn save_with(io: &mut dyn SettingsIo, roots: &Roots, settings: &Settings) -> Result<(), SettingsError>;
```

The file struct is private:
`#[derive(Serialize, Deserialize)] #[serde(deny_unknown_fields)] struct SettingsFileV1 { schema_version: u64,
linux_plugin_root: Option<String>, linux_graphics_workaround: bool }` (field order = key order, FR-030). `encode`
of `Settings::default()` is exactly:

```
{
  "schema_version": 1,
  "linux_plugin_root": null,
  "linux_graphics_workaround": false
}
```

plus LF (`serde_json` pretty printer, two-space indent). `save_with` order (FR-031, FR-034, FR-036): `validate` →
`read_existing` → if `Some(bytes)` and `classify(bytes)` is `Err(FutureVersion { found })` return it (nothing written)
→ `create_dir` → `create_temp` → `write` → `sync_temp` → `persist` → (Unix) `sync_dir`. Each `io::Error` becomes
`SettingsError::Io { op: <step>, source }`. The `SettingsIo` steps keep taking `&Path`, because they are
crate-private and are called only by `save_with` with paths it derives from `roots.state_dir()`.

`validate` rejects a `linux_plugin_root` with a `..` component, or with no normal component, as `Invalid` (PD-26).
`classify` runs the same path check on a loaded v1 file, so `load` rejects such a file with `E_SETTINGS_INVALID`.
SC-010 gains three cases:
- `load` of a file whose `linux_plugin_root` is `<canonical base>/a/../b`, JSON-escaped with `serde_json::to_string`
  so Windows backslashes survive;
- `load` of the base's filesystem root (`base.ancestors().last()`);
- `save` of each of the two.

Each is `E_SETTINGS_INVALID`, with bytes and listing unchanged. `load_rejections_keep_defaults_and_file` also carries
`schema_version: -1` → `E_SETTINGS_INVALID` (PD-8). The test double (`settings_tests.rs`) is
`struct FaultIo { at: SettingsStep, mode: Mode }`, `enum Mode { Fail, AbortAfter }`, delegating to `StdSettingsIo`
except at `at` (PD-9).

### 4.6 `core::state_lock` (`crates/core/src/state_lock.rs`)

```rust
pub const LOCK_FILE: &str = "manager.lock";
#[derive(Debug)] pub struct StateLock { _file: std::fs::File }
impl StateLock { pub fn acquire(roots: &Roots) -> Result<StateLock, LockError>; }   // PD-25
```

`acquire`: `create_dir_all(roots.state_dir())` (`CreateDir`); `OpenOptions::new().read(true).write(true).create(true)
.truncate(false).open(roots.state_dir().join(LOCK_FILE))` (`Open`; explicit `truncate(false)` because clippy's
`suspicious_open_options` asks for it with `create(true)`, and not append-only because Windows refuses a lock on an
append-only handle, std docs); `file.try_lock()`: `Err(TryLockError::WouldBlock)` → `Held`,
`Err(TryLockError::Error(e))` → `Io { op: Lock, source: e }`. No `Drop` impl: closing the `File` releases the lock;
the file is never written or deleted. The std docs say the lock is released when the handle "along with any other
file descriptors/handles duplicated or inherited from it" is closed, so whether a child process can inherit the lock
handle matters once a later phase spawns processes; Phase 4 spawns none, and Phase 10 (the first phase that starts the
helper and installers) verifies handle inheritance before relying on it. SC-025's "absent state dir" and "state dir
is a regular file" cases start from `fresh_temp_roots()`. They first `remove_dir_all(roots.state_dir())`, which is
inside the temp base and removes the `state/staging` that `temp_roots` made. Then they either leave the path absent or
write a regular file at it, and call `acquire(&roots)`.

### 4.7 `core::log_sink` (`crates/core/src/log_sink.rs`)

```rust
pub const LOG_FILE: &str = "krate-manager.log";
pub const LOG_FILE_ROTATED: &str = "krate-manager.log.1";
pub const LOG_CAP_BYTES: u64 = 1024 * 1024;
pub const MAX_LINE_BYTES: usize = 4096;

pub fn format_line(unix_secs: u64, level: log::Level, target: &str, message: &str) -> String;
pub struct RotatingLog { dir: PathBuf, file: Option<File>, size: u64, cap: u64 }
impl RotatingLog {
    pub fn open(roots: &Roots) -> io::Result<RotatingLog>;                   // open_with_cap(roots, LOG_CAP_BYTES) (PD-25)
    pub fn open_with_cap(roots: &Roots, cap_bytes: u64) -> io::Result<RotatingLog>; // dir = roots.logs_dir(); create_dir_all, append, size from metadata
    pub fn write_line(&mut self, line: &str) -> io::Result<()>;              // PD-10
}
fn cut_at_boundary(s: &str, max: usize) -> &str;                             // longest prefix ≤ max bytes ending on a char boundary
```

`format_line` produces `format!("{unix_secs} {level} {target}: {msg}\n")`. Every `char::is_control` in `message`
becomes a space, and the message is cut with `cut_at_boundary` (PD-10).

`write_line` (PD-10) works in three steps:
1. If `line.len() > cap`, it is cut to `cut_at_boundary(line_without_lf, cap - 1)` plus `\n`.
2. If `size > 0 && size + len > cap`, it rotates:
   - it takes the handle out (`self.file = None`), because Windows cannot rename an open file it holds without share-delete, so the handle is closed first;
   - it calls `std::fs::rename(log, log.1)`, which replaces the old `.1` (std docs);
   - in both outcomes it reopens `LOG_FILE` in append mode into `self.file`. On a rename error it re-reads `size` from metadata and returns the error without writing.
   - On success `size = 0`.
3. It calls `write_all` and adds `len` to `size`.

`rotating_log_cases` (SC-029) gains two cases, each in its own `fresh_temp_roots()`:
- An over-long line: a 300-byte line ending in multi-byte characters, with the cut position inside one, written with
  `open_with_cap(&roots, 200)` after a short line. Afterwards no file in the logs dir is over 200 bytes, and both
  files are valid UTF-8 (`String::from_utf8` on their bytes).
- A failed rotation: `logs/krate-manager.log.1` is created as a directory holding one file. Lines are written until
  rotation is due, and that `write_line` returns `Err`. `krate-manager.log` is then byte-identical to before the call
  and at most 200 bytes. After the directory is removed, the next `write_line` rotates and succeeds, which shows the
  handle was reopened.

Both cases write only inside the temp base.

### 4.8 `core::context` (`crates/core/src/context.rs`)

```rust
pub const LOCKED_NOTICE: &str = "Krate Manager is already running";
pub const LOCKED_EXIT_DELAY: Duration = Duration::from_secs(5);
pub const STARTUP_LOG_TARGET: &str = "krate_manager_core::startup";

#[derive(Clone, Debug, PartialEq)]
pub struct AppContext { pub roots: Roots, pub settings: Settings, pub feed_url: String }

#[derive(Debug)]
pub struct AppState {
    pub context: Result<AppContext, StartupError>,
    pub settings_error: Option<SettingsError>,
    pub lock: Option<Arc<StateLock>>,
}
pub type SharedState = std::sync::RwLock<AppState>;

pub fn assemble_context(roots: Result<Roots, RootsError>, feed_url: &str) -> AppState; // assemble_with_os(NativePlatform.os(), …)
pub(crate) fn assemble_with_os(os: Os, roots: Result<Roots, RootsError>, feed_url: &str) -> AppState;
pub fn reassemble(prev: &AppState, settings: Settings) -> AppState;      // reassemble_with_os(NativePlatform.os(), …)
pub(crate) fn reassemble_with_os(os: Os, prev: &AppState, settings: Settings) -> AppState;
pub fn apply_settings(shared: &SharedState, settings: Settings);         // PD-2
pub fn startup_notice(state: &AppState) -> Option<&'static str>;
pub fn log_startup_errors(state: &AppState);
pub fn parse_dev_feed_url(text: &str) -> Option<String>;
```

`assemble_with_os` (FR-017, FR-100, FR-102) handles the two `roots` cases:
- `Err(e)` → `{Err(Roots(e)), None, None}`.
- `Ok(roots)` runs four steps:
  1. `StateLock::acquire(&roots)` (`Held` → `Locked`; `Io` → `LockIo` with the source in an `Arc`);
  2. `load(&roots)`;
  3. when `os == Os::Linux`, fold the configured root: `roots.with_configured_plugin_root(settings.linux_plugin_root.as_deref())`;
  4. return `{Ok(AppContext {…}), outcome.error, Some(Arc::new(lock))}`.

A fold `Err` cannot occur for a loaded `Ok` settings value. `load` rejects a relative or non-normal path as `Invalid`
and then returns defaults (PD-26). The fold's `Relative` / `NotNormal` checks repeat `validate`'s. On a sandboxed test
`Roots` (PD-24), a planted `settings.json` naming a path outside the temp base panics in the fold. The settings-file
route therefore cannot give a test a real plug-in root.

`reassemble_with_os` (FR-103): `prev.context` `Err(e)` → `{Err(e.clone()), None, prev.lock.clone()}`; `validate`
failure → `{Ok(prev ctx clone), Some(Invalid{..}), lock clone}`; else roots = `prev roots.with_configured_plugin_root(
if Linux { s.linux_plugin_root.as_deref() } else { None })`, same `feed_url`, `Arc::clone(lock)`, `settings_error:
None`. No I/O. On a sandboxed `prev`, an outside configured root panics as in `assemble_with_os` (PD-24).

`context::tests::sandbox_holds_through_assemble_and_reassemble` (PD-24) builds `fresh_temp_roots()` and an outside
path `o = canonical_temp_dir().join("km-outside-sandbox")`. The path is absolute and outside the `TempDir`, and is
never created. Under `catch_unwind(AssertUnwindSafe(…))` it asserts a panic for two calls:
- (a) `assemble_with_os(Os::Linux, Ok(roots), FEED)` over a planted `settings.json` whose `linux_plugin_root` is `o`;
- (b) `reassemble_with_os(Os::Linux, &prev, Settings { linux_plugin_root: Some(o), .. })`.

The base's listing outside `state/` is unchanged. `roots::tests::sandboxed_roots_refuse_an_outside_configured_root`
checks the same panic for a direct `with_configured_plugin_root(Some(&o))` call. In the same `TempDir` it checks that a
path inside the base (`roots.state_dir().parent().unwrap().join("configured")`) is accepted. Neither test names a real
system path.

`apply_settings`: `let next = { let g = shared.read().unwrap_or_else(PoisonError::into_inner); reassemble(&g, settings) };`
then `*shared.write().unwrap_or_else(PoisonError::into_inner) = next;`.

`startup_notice`: `Some(LOCKED_NOTICE)` iff `matches!(state.context, Err(StartupError::Locked))`.

`log_startup_errors`: one `log::error!(target: STARTUP_LOG_TARGET, "{}", e)` for the context error and one for
`settings_error`, each Display beginning with the code (§3).

`parse_dev_feed_url`: `let t = text.trim();` `None` if `t.is_empty()`, `t.len() > 2048`, or `t` contains `'\n'` / `'\r'`;
`url::Url::parse(t)` must succeed with scheme `http` or `https` and a non-empty `host_str()`; returns
`Some(t.to_owned())` (the trimmed text, not the normalised URL).

### 4.9 `core::navigation` (`crates/core/src/navigation.rs`)

```rust
pub const DEV_URL: &str = "http://localhost:1420";
pub fn app_origin(dev: bool, os: Os) -> url::Url;  // DEV_URL | "http://tauri.localhost" (Windows) | "tauri://localhost"
pub fn is_app_origin(target: &url::Url, app: &url::Url) -> bool; // scheme, host_str, port_or_known_default equal
```

The three constants parse by construction (`Url::parse(..).expect("constant URL")`). `tauri://localhost` has no known
default port, so `None == None`; `http://tauri.localhost` compares port 80. `tauri::Url` is this same `url::Url`
(re-export, `tauri` `lib.rs:92`; one `url` 2.5.8 in the lock), so `src-tauri` passes Tauri's `&Url` straight in.

### 4.10 Core integration tests

**`crates/core/tests/injection.rs`** (FR-021, SC-008). A small scanner in the test file:

```rust
struct Finding { file: String, rule: &'static str, detail: String }
fn scan_core_file(rel: &str, text: &str) -> Vec<Finding>;   // rel = path under crates/core/src, '/'-separated
fn strip_comments_and_literals(text: &str) -> (String /* code with literals blanked */, Vec<Literal>);
```

The lexer handles `//`, nested `/* */`, `"…"` with escapes, `b"…"`, char literals (`'"'`, `'\''`, lifetimes are not
literals), and raw strings `r"…"` / `r#"…"#` (recorded with `raw = true`). It also marks attribute spans: `#`, an
optional `!`, then `[` up to the matching `]`, with brackets counted only outside literals and comments. Each recorded
`Literal` carries `in_attribute: bool` (PD-5).

Rules, applied to every scanned file (every file except `test_support.rs` and `*_tests.rs`):
- Fail on `mod tests {` in the code text.
- Outside `platform/`: fail on the raw-text substrings `"C:\\`, `"/Library`, `"/usr/`, `"/home`, `"/Users`,
  `krateaudio.com` and `api.github.com`.
- Inside `platform/`: every literal with `in_attribute == false` must be in `PLATFORM_LITERALS`, and no such literal
  may be raw (PD-5). Attribute literals (`cfg(target_os = "…")`, `path = "…_tests.rs"`) are not checked.
- In `platform/` and `roots.rs`: none of the FR-021 tokens may appear as a whole identifier of the code text (split on
  non-`[A-Za-z0-9_]`).
- In every scanned file but `roots.rs`: no identifier `sandboxed` (PD-24).

`PLATFORM_LITERALS` = the six env names of §4.2, the components `"Programs"`, `"Common"`, `"VST3"`, `"Krate Audio"`,
`"Documents"`, `"Library"`, `"Audio"`, `"Plug-Ins"`, `"Components"`, `"Application Support"`, `".vst3"`, `".local"`,
`"share"`, the seven absolute literals of §4.2, and `"com.krateaudio.krate-manager"`.

Tests:
- `core_sources_hold_no_production_literal`: a scan of the real tree gives empty findings. The real `platform/mod.rs`,
  with its six `cfg` attributes and its `#[path = "mod_tests.rs"]`, passes.
- `injection_scanner_reports_planted_violations`: the five SC-008 strings are each reported as a `Finding` with the
  expected rule. So are two more planted cases. First, `PathBuf::from("/etc")` in a `platform/` file
  (rule: non-allow-listed platform literal), while in the same planted text `#[cfg(target_os = "windows")]` and
  `#[cfg(test)] #[path = "windows_tests.rs"] mod tests;` give no finding. Second, `.sandboxed(` in a planted
  `context.rs`.
- `disallowed_methods_expectations_are_the_seven_d6_sites` (PD-22): the real tree matches the §4.1 per-file table, with
  0 in `tests/fixtures/clippy-guard/src/lib.rs`, and no `clippy::style`, `clippy::all` or `warnings` lint-level
  attribute. The same counting function, run on in-memory text, reports two plants: an eighth
  `#[expect(clippy::disallowed_methods, reason = "x")]` planted in `context.rs` text, and a planted
  `#[expect(clippy::style, reason = "x")]`. This file itself holds `disallowed_methods` only inside string literals,
  which the lexer blanks.
- `core_dependencies_are_the_fr007_set`: text-parses `crates/core/Cargo.toml` and asserts the `[dependencies]` keys
  are exactly {serde, serde_json, thiserror, tempfile, url, log}.
- `src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots`: in the non-test files of `src-tauri/src/`,
  `krateaudio.com` occurs once and in `lib.rs`, and `var_os` occurs only in `os_roots.rs`.

**`crates/core/tests/config_guard.rs`** (FR-045, SC-015). Loads into a `struct ConfigFiles { conf: serde_json::Value,
platform_confs: Vec<(String, serde_json::Value)>, capabilities: Vec<(String, serde_json::Value)>, build_rs: String,
cargo_toml: String, lib_rs: String, src_tauri_texts: Vec<(String, String)>, tests_dir_exists: bool, info_plist_exists:
bool }` and runs `fn check_config(f: &ConfigFiles) -> Vec<String>` (one message per failed FR-045 assertion) and
`fn check_frontend(files: &[(String, String)]) -> Vec<String>` (FR-045 source scan over `src/**` and `index.html`).
Tests: `real_src_tauri_config_passes_the_guard` (both empty), `planted_config_changes_fail_the_guard` (each SC-015
config/`Cargo.toml`/`lib.rs` mutation applied to a clone → the vector holds the expected message),
`frontend_source_scan_reports_planted_strings` (the four planted strings). Text checks on `Cargo.toml` use a section
splitter (`[lib]`, `[[bin]]`, `[features]`, `[dependencies]`); `lib.rs` counts via `str::matches(..).count()`.

`check_config` also asserts the PD-23 dependency facts (FR-004). `ConfigFiles` gains `workspace_cargo_toml: String`,
the root manifest. Each fact has a mutation in `planted_config_changes_fail_the_guard`:
- the src-tauri `[dependencies]` keys are exactly {`tauri`, `serde`, `serde_json`, `log`, `krate-manager-core`}.
  Mutation: add `tauri-plugin-opener = "2"`, which fails both this rule and the next;
- no line of `src-tauri/Cargo.toml` contains `tauri-plugin`. Mutation: the same line under `[build-dependencies]`;
- the `tauri` line is exactly `tauri = { workspace = true }`. Mutation: `tauri = { workspace = true, features = ["devtools"] }`;
- the root `[workspace.dependencies]` `tauri` entry has `features = []`. Mutation: `features = ["tray-icon"]`.

**`check_app_meta(f: &AppMetaFiles) -> Vec<String>`** (PD-23) loads `struct AppMetaFiles { workspace_cargo_toml:
String, member_manifests: Vec<(String, String)>, package_json: serde_json::Value, tsconfig: serde_json::Value,
eslint_config: String, changelog: String, version: String }` from `apps/krate-manager/`. It asserts:
- **FR-002**: the root `[workspace]` `members` is exactly `["crates/core", "crates/helper", "src-tauri"]`, and
  `[workspace.lints.clippy]` holds `allow_attributes = "deny"` and `allow_attributes_without_reason = "deny"`. Every
  member manifest has a `[lints]` section containing `workspace = true`.
- **FR-022**: in every member manifest, every line containing `test-support` sits in a `[dev-dependencies]` section,
  except core's `[features]`. That section's non-blank lines are exactly `test-support = []`.
- **FR-050**: `dependencies` deep-equals `{"@tauri-apps/api": "2.12.4"}`. Every `devDependencies` value matches
  `^[0-9]+\.[0-9]+\.[0-9]+$` (no `^`, `~`, range or tag). There is no `version` key, and no key `@tauri-apps/plugin-*`
  in either map. `scripts` deep-equals FR-050's six.
- **FR-051**: `compilerOptions` has `strict`, `noUnusedLocals`, `noUnusedParameters`, `noFallthroughCasesInSwitch`,
  `noUncheckedIndexedAccess` and `exactOptionalPropertyTypes` all `true`, and no `types` key. `include` deep-equals
  `["src"]`. `tsconfig.json` is written as plain JSON with no comments, so `serde_json` parses it. Any comment the
  template's file carries is dropped when the options are copied.
- **FR-052**: `eslint.config.mjs` contains `reportUnusedDisableDirectives: "error"`, `tseslint.configs.strictTypeChecked`
  and `projectService: true`.
- **FR-062**: `CHANGELOG.md`'s first six lines, read with `str::lines()` so CRLF checkouts pass, equal the
  `plugins/vorago/CHANGELOG.md:1-6` header with "Vorago" replaced by "Krate Manager". Some line starts with
  `## [<version.json version>] - `.

Tests: `real_app_metadata_passes_the_guard` (empty) and `planted_app_metadata_changes_fail_the_guard`. The second
applies one mutation per assertion to a clone, and each mutation must yield its message:
- **Manifests**: remove core's `[lints]` section; move `test-support` into src-tauri's `[dependencies]` line for
  `krate-manager-core`; add `default = ["test-support"]` to core's `[features]`; drop a `deny`; add a fourth member.
- **`package.json`**: add `"@tauri-apps/plugin-opener": "2.5.0"` to `dependencies`; change `"vite": "8.3.5"` to `"^8.3.5"`.
- **`tsconfig.json`**: delete `noUncheckedIndexedAccess`; delete `exactOptionalPropertyTypes`; add `"types": ["node"]`.
- **ESLint and CHANGELOG**: remove `reportUnusedDisableDirectives`; change the CHANGELOG heading to `## [0.1.1] - Unreleased`;
  drop the header's Keep a Changelog line.

Both test files read only (`std::fs::read_to_string` / `read_dir`), so they need no temp dir.

### 4.11 `crates/helper`

`Cargo.toml`: package `krate-manager-helper`, workspace-inherited fields, `[lints] workspace = true`, no dependencies,
`[dev-dependencies] krate-manager-core = { workspace = true, features = ["test-support"] }` (FR-022). `src/lib.rs`:
a crate doc comment only. `src/main.rs`:

```rust
fn main() -> std::process::ExitCode {
    eprintln!("krate-manager-helper: no operations in this build");
    std::process::ExitCode::from(2)
}
```

`tests/stub.rs`: `helper_stub_exits_2_with_one_line` runs `env!("CARGO_BIN_EXE_krate-manager-helper")` with no
argument and with `--anything` (`std::process::Command`), asserting status code 2, stdout empty, stderr exactly the
line plus the platform newline.

### 4.12 `src-tauri` (package `krate-manager`)

`Cargo.toml` (FR-004):

```toml
[package]
name = "krate-manager"
description = "Krate Manager"
version.workspace = true
edition.workspace = true
rust-version.workspace = true
license.workspace = true
publish.workspace = true

[lib]
name = "krate_manager_lib"
path = "src/lib.rs"
test = false
doctest = false

[[bin]]
name = "krate-manager"
path = "src/main.rs"
test = false

[build-dependencies]
tauri-build = { workspace = true }

[dependencies]
tauri = { workspace = true }
serde = { workspace = true }
serde_json = { workspace = true }
log = { workspace = true }
krate-manager-core = { workspace = true }

[dev-dependencies]
krate-manager-core = { workspace = true, features = ["test-support"] }

[features]
dev-feed = []

[lints]
workspace = true
```

`build.rs`:

```rust
fn main() {
    tauri_build::try_build(
        tauri_build::Attributes::new()
            .app_manifest(tauri_build::AppManifest::new().commands(&["app_version", "startup_error"])),
    )
    .expect("tauri-build failed");
}
```

`tauri.conf.json` (FR-005, D13, PD-21):

```json
{
  "$schema": "https://schema.tauri.app/config/2",
  "productName": "Krate Manager",
  "version": "0.1.0",
  "identifier": "com.krateaudio.krate-manager",
  "build": {
    "beforeDevCommand": "npm run dev",
    "devUrl": "http://localhost:1420",
    "beforeBuildCommand": "npm run build",
    "frontendDist": "../dist"
  },
  "app": {
    "withGlobalTauri": false,
    "windows": [{ "label": "main", "title": "Krate Manager", "width": 800, "height": 600 }],
    "security": {
      "csp": "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; font-src 'self'; connect-src ipc: http://ipc.localhost; object-src 'none'; base-uri 'none'; form-action 'none'; frame-src 'none'; frame-ancestors 'none'"
    }
  },
  "bundle": {
    "active": true,
    "icon": ["icons/32x32.png", "icons/128x128.png", "icons/128x128@2x.png", "icons/icon.icns", "icons/icon.ico"],
    "macOS": { "signingIdentity": "-", "minimumSystemVersion": "15.0" },
    "windows": { "nsis": { "installMode": "currentUser" } }
  }
}
```

`tauri.windows.conf.json` = `{"bundle": {"targets": ["nsis"]}}`, `tauri.macos.conf.json` = `{"bundle": {"targets":
["app"]}}`, `tauri.linux.conf.json` = `{"bundle": {"targets": ["deb"]}}` (D15).

`capabilities/main.json` (FR-041):

```json
{
  "identifier": "main",
  "description": "The main window may call only the app's own read-only commands.",
  "windows": ["main"],
  "permissions": ["allow-app-version", "allow-startup-error"]
}
```

`src/main.rs`: the template's `#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]` and
`fn main() { krate_manager_lib::run() }`.

`src/lib.rs` (glue only; PD-1):

```rust
mod commands;
#[cfg(feature = "dev-feed")]
mod dev_feed;
mod log_sink;
mod navigation;
pub mod os_roots;

use krate_manager_core::context::{assemble_context, log_startup_errors, startup_notice, SharedState, LOCKED_EXIT_DELAY};
use krate_manager_core::roots::Roots;
use os_roots::FromOs;

const FEED_URL: &str = "https://krateaudio.com/versions.json";

pub fn run() {
    #[expect(clippy::disallowed_methods, reason = "the one production Roots, built once at startup")]
    let roots = Roots::from_os();
    let sink_roots = roots.as_ref().ok().cloned();                       // PD-25: init takes &Roots
    #[cfg(feature = "dev-feed")]
    let feed_url = roots.as_ref().ok().and_then(|r| dev_feed::read(r.state_dir())).unwrap_or_else(|| FEED_URL.to_owned());
    #[cfg(not(feature = "dev-feed"))]
    let feed_url = FEED_URL.to_owned();
    let state = assemble_context(roots, &feed_url);
    match &sink_roots {
        Some(r) => {
            log_sink::init(r);
            log_startup_errors(&state);
            #[cfg(feature = "dev-feed")]
            if feed_url != FEED_URL { log::warn!("dev-feed override: {feed_url}"); }
        }
        None => if let Err(e) = &state.context { eprintln!("{e}"); },
    }
    let locked = startup_notice(&state).is_some();
    tauri::Builder::default()
        .plugin(navigation::guard())
        .manage(SharedState::new(state))
        .setup(move |app| {
            if locked {
                let handle = app.handle().clone();
                std::thread::spawn(move || { std::thread::sleep(LOCKED_EXIT_DELAY); handle.exit(1); });
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![commands::app_version, commands::startup_error])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
```

The feed URL is chosen by two `cfg`'d `let`s rather than a helper function, because a helper taking the roots would
leave its parameter unused (an `unused_variables` warning) in the default build.

This holds every FR-045 string: `.plugin(navigation::guard())`, `assemble_context(`, `.manage(`, `SharedState`,
`startup_notice`, `startup_error`, `LOCKED_EXIT_DELAY`, `log_sink::init(` after `assemble_context(`, one `eprintln!`.

`src/os_roots.rs` (FR-014):

```rust
pub trait FromOs { fn from_os() -> Result<Roots, RootsError>; }
impl FromOs for Roots {
    fn from_os() -> Result<Roots, RootsError> {
        let mut pairs: Vec<(&'static str, OsString)> = Vec::new();
        for key in ENV_KEYS {
            #[expect(clippy::disallowed_methods, reason = "the only process-environment read for roots (D9)")]
            let value = std::env::var_os(key);
            if let Some(v) = value { pairs.push((*key, v)); }
        }
        let env = OsEnv::from_pairs(pairs.iter().map(|(k, v)| (*k, v.as_os_str())));
        let paths = NativePlatform.default_roots(&env)?;
        #[expect(clippy::disallowed_methods, reason = "from_os is the production constructor")]
        let roots = Roots::new(paths);
        roots
    }
}
```

(The scratch run showed an attributed `let … ; value` raised no `let_and_return`; if clippy 0.1.99 on the real tree
disagrees, the fix is a `match` on the call, not an `allow`.)

`src/commands.rs`: `#[tauri::command] pub fn app_version() -> &'static str { env!("CARGO_PKG_VERSION") }` and
`#[tauri::command] pub fn startup_error(state: tauri::State<'_, SharedState>) -> Option<&'static str> {
startup_notice(&state.read().unwrap_or_else(PoisonError::into_inner)) }`.

`src/navigation.rs` (FR-044): `pub fn guard<R: tauri::Runtime>() -> tauri::plugin::TauriPlugin<R> {
tauri::plugin::Builder::new("navigation-guard").on_navigation(|_webview, url| is_app_origin(url,
&app_origin(tauri::is_dev(), NativePlatform.os()))).build() }`.

`src/log_sink.rs` (FR-105): `struct FileLogger { inner: Mutex<RotatingLog> }` implementing `log::Log` (`enabled`:
`level <= Info`; `log`: `format_line(now_secs(), level, target, &record.args().to_string())`, lock with
`unwrap_or_else(PoisonError::into_inner)`, `let _ = write_line(..)`; `flush`: no-op);
`static LOGGER: OnceLock<FileLogger>`; `pub fn init(roots: &Roots)` (PD-25): `RotatingLog::open(roots)`, which
writes `roots.logs_dir()`, → on `Ok`,
`LOGGER.get_or_init(..)`, `log::set_logger(logger)` and on success `log::set_max_level(LevelFilter::Info)`; on any
error it returns silently. `now_secs()` = `SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |d| d.as_secs())`.
Records from Tauri's own crates (`tauri` logs through `log`, e.g. `app.rs:583`) land in the same file at Info and up.

`src/dev_feed.rs` (FR-106): `pub const DEV_FEED_FILE: &str = "dev-feed-url.txt";`
`pub fn read(state_dir: &Path) -> Option<String> { std::fs::read_to_string(state_dir.join(DEV_FEED_FILE)).ok()
.and_then(|t| parse_dev_feed_url(&t)) }`.

### 4.13 `tests/fixtures/clippy-guard/`

`Cargo.toml`: package `clippy-guard`, `edition = "2024"`, `publish = false`, `[dependencies] krate-manager = { path
= "../../../src-tauri" }`, `krate-manager-core = { path = "../../../crates/core" }`. No `[lints]`, no `[workspace]`
table (the parent's `exclude` makes it its own root). `src/lib.rs`:

```rust
#[cfg(test)]
mod tests {
    use krate_manager_core::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, Roots};
    use krate_manager_lib::os_roots::FromOs;
    use std::env;
    use std::path::PathBuf;

    #[test]
    fn calls_from_os() { let _ = Roots::from_os(); }

    #[test]
    fn calls_roots_new() {
        let s = PathBuf::from("/s");
        let _ = Roots::new(RootPaths {
            plugin_roots: vec![PluginRoot { kind: RootKind::Vst3User, path: PathBuf::from("/u") },
                               PluginRoot { kind: RootKind::Vst3Global, path: PathBuf::from("/g") }],
            presets: PresetRoots { factory: PathBuf::from("/f"), user: PathBuf::from("/p") },
            staging_dir: s.join("staging"),
            state_dir: s,
        });
    }

    #[test]
    fn calls_env_var() { let _ = env::var("HOME"); }
}
```

`Cargo.lock` per PD-16. Never run; linted by FR-072 only.

### 4.14 Frontend

`package.json` (FR-050):

```json
{
  "name": "krate-manager",
  "private": true,
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "vite build",
    "typecheck": "tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json",
    "lint": "eslint . --max-warnings 0",
    "test": "vitest run",
    "tauri": "tauri"
  },
  "dependencies": { "@tauri-apps/api": "2.12.4" },
  "devDependencies": {
    "@eslint/js": "10.0.1",
    "@tauri-apps/cli": "2.12.4",
    "@types/node": "26.6.6",
    "eslint": "10.14.0",
    "jsdom": "29.1.1",
    "typescript": "6.0.3",
    "typescript-eslint": "8.72.1",
    "vite": "8.3.5",
    "vitest": "5.0.5"
  }
}
```

`tsconfig.json`: the template's options (§1.2) plus `noUncheckedIndexedAccess: true`, `exactOptionalPropertyTypes:
true`, `include: ["src"]`, no `types` (FR-051). `tsconfig.node.json`: `{"compilerOptions": {"target": "ES2022",
"module": "ESNext", "moduleResolution": "bundler", "strict": true, "noEmit": true, "skipLibCheck": true, "types":
["node"]}, "include": ["vite.config.ts"]}`.

`vite.config.ts` (FR-053): first line `/// <reference types="vitest/config" />`; the template body with the
`@ts-expect-error` line deleted (`import process from "node:process";` now type-checks through `@types/node`); a
`test: { environment: "jsdom", include: ["src/**/*.test.ts"] }` block, no `fileParallelism`, `pool` or `maxWorkers`.

`eslint.config.mjs` (FR-052):

```js
import js from "@eslint/js";
import { defineConfig } from "eslint/config";
import tseslint from "typescript-eslint";

const SINKS = "is banned: build DOM with createElement and textContent (spec FR-052)";
export default defineConfig(
  { ignores: ["dist/", "src-tauri/", "crates/", "target/", "tests/fixtures/", "eslint.config.mjs", "vite.config.ts"] },
  js.configs.recommended,
  tseslint.configs.strictTypeChecked,
  { languageOptions: { parserOptions: { projectService: true, tsconfigRootDir: import.meta.dirname } } },
  { linterOptions: { reportUnusedDisableDirectives: "error" } },
  {
    rules: {
      "no-restricted-properties": ["error",
        { property: "innerHTML", message: `innerHTML ${SINKS}` },
        { property: "outerHTML", message: `outerHTML ${SINKS}` },
        { property: "insertAdjacentHTML", message: `insertAdjacentHTML ${SINKS}` },
        { object: "document", property: "write", message: `document.write ${SINKS}` },
        { object: "document", property: "writeln", message: `document.writeln ${SINKS}` }],
      "no-restricted-syntax": ["error",
        { selector: "CallExpression[callee.name='eval']", message: `eval ${SINKS}` },
        { selector: "NewExpression[callee.name='Function']", message: `new Function ${SINKS}` }],
    },
  },
);
```

`index.html`: `<!doctype html>`, `<meta charset="UTF-8">`, `<title>Krate Manager</title>`, `<link
rel="stylesheet" href="/src/styles.css">`, body `<main id="app"></main>` and `<script type="module"
src="/src/main.ts"></script>`. The stylesheet is linked from HTML, not imported from TypeScript, so no `<style>` element
is ever injected (D13 `style-src 'self'`). `src/styles.css`: system font stack and spacing, no `url(` and no
`@import`.

`src/main.ts` (FR-046, PD-18):

```ts
import { invoke } from "@tauri-apps/api/core";

export async function renderShell(root: HTMLElement): Promise<void> {
  root.replaceChildren();
  const title = document.createElement("h1");
  title.textContent = "Krate Manager";
  root.append(title);
  const notice = await invoke<string | null>("startup_error").catch(() => null);
  const line = document.createElement("p");
  if (notice !== null) {
    line.className = "notice";
    line.textContent = notice;
    root.append(line);
    return;
  }
  try {
    line.textContent = await invoke<string>("app_version");
  } catch {
    line.textContent = "version unavailable";
  }
  line.className = "version";
  root.append(line);
}

const app = document.getElementById("app");
if (app !== null) void renderShell(app);
```

`src/main.test.ts` (FR-054, SC-017): `afterEach(clearMocks)`; each test builds `document.createElement("main")`, sets
`mockIPC((cmd) => { calls.push(cmd); … })`, `await renderShell(root)`, then asserts `root.querySelector("h1")
?.textContent`, the `p` text and `calls`. Titles: `shows the title and the version`, `shows "version unavailable" when
app_version fails`, `shows only the notice when another instance is running`. There is a fourth case for PD-18,
`shows the version when startup_error fails`: `mockIPC` throws for `startup_error` and answers `"0.1.0"` for
`app_version`. It asserts the `h1` text, a `p.version` reading `0.1.0`, no `.notice`, and
`calls == ["startup_error", "app_version"]`. It is not in `required-tests.json` (PD-18). In jsdom the module's
bootstrap finds no `#app` and does nothing.

### 4.15 `tools/lint-manager-version.js`

```js
#!/usr/bin/env node
// lint-manager-version.js — header: WHY (one version source, Q1), usage, exit codes 0 / 1 / 2.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const USAGE = 'usage: node tools/lint-manager-version.js [--root <dir>]';
const VERSION_RE = /^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$/;
function parseTomlSubset(text, file, readSections) -> { sections: Map<string, Map<string, Value>>, errors: [] }
function lint(root) -> { errors: [{ code, file, message }] }
function main(argv, io) -> number      // io = { stdout, stderr } with write()
module.exports = { lint, main, parseTomlSubset };
if (require.main === module) process.exitCode = main(process.argv.slice(2), process);
```

`parseTomlSubset` splits on `/\r?\n/`, skips blank lines and `#` comments, recognises `[a.b]` and `[[a]]` headers, and
inside a read section accepts only `key = "string"`, `key = true|false` (PD-13), `key.workspace = true`, and
single- or multi-line arrays of strings; any other line in a read section is `E_TOML_UNSUPPORTED` naming the line
number. `lint(root)` checks, in this order, reporting every error: `version.json` (`E_VERSION_FILE`,
`E_VERSION_FORMAT`); `src-tauri/tauri.conf.json` (`E_TAURI_CONF`, `E_VERSION_MISMATCH`); every
`src-tauri/tauri.*.conf.json` sorted (`E_VERSION_SOURCE` for a `version` key); `package.json` (`E_VERSION_SOURCE`);
root `Cargo.toml` (`[workspace]` `members`, glob → `E_CARGO_TOML`; `[workspace.package] version` missing →
`E_CARGO_TOML`, unequal → `E_VERSION_MISMATCH`); each member in `members` order (missing file or no `[package]`
version line → `E_CARGO_TOML`; literal unequal → `E_VERSION_MISMATCH`; `version.workspace = true` passes). Messages
name both values (`version 0.1.1 != version.json 0.1.0`). Paths in messages are root-relative, `/`-separated.

### 4.16 `.github/workflows/manager-ci.yml`

```yaml
name: Manager CI

# Spec: specs/krate-manager-phase4/spec.md FR-070 to FR-077, FR-106, FR-107; plan §4.16.
# Every assertion step prints a ::notice (pass) or ::error (fail) naming the leg (D21): compliance reads annotations.
on:
  push:
    paths:
      - 'apps/krate-manager/**'
      - '.github/workflows/manager-ci.yml'
      - 'tools/lint-manager-version.js'
  pull_request:
    paths:
      - 'apps/krate-manager/**'
      - '.github/workflows/manager-ci.yml'
      - 'tools/lint-manager-version.js'
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: manager-ci-${{ github.ref }}
  cancel-in-progress: true

jobs:
  check:
    name: check (${{ matrix.os }})
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        os: [windows-latest, macos-latest, ubuntu-24.04]
    defaults:
      run:
        shell: bash
        working-directory: apps/krate-manager
    steps:
      - uses: actions/checkout@v6

      - uses: actions/setup-node@v4
        with:
          node-version: '24'

      - name: Linux system packages
        if: runner.os == 'Linux'
        run: |
          set -euo pipefail
          sudo apt-get update
          sudo apt-get install -y libwebkit2gtk-4.1-dev build-essential curl wget file libxdo-dev libssl-dev libayatana-appindicator3-dev librsvg2-dev

      - name: Rust toolchain
        run: |
          set -euo pipefail
          rustup toolchain install
          rustc --version
          cargo clippy --version

      - uses: Swatinem/rust-cache@v2
        with:
          workspaces: apps/krate-manager -> target

      - name: npm ci
        run: |
          set -euo pipefail
          npm ci

      - name: version lint
        run: |
          set -euo pipefail
          if node ../../tools/lint-manager-version.js; then
            echo "::notice title=version lint ($RUNNER_OS)::version.json, tauri.conf.json and every Cargo.toml agree"
          else
            echo "::error title=version lint ($RUNNER_OS)::lint-manager-version failed"; exit 1
          fi

      - name: cargo fmt
        run: |
          set -euo pipefail
          cargo fmt --all --check

      - name: clippy
        run: |
          set -euo pipefail
          cargo clippy --workspace --all-targets --locked -- -D warnings

      - name: clippy dev-feed
        run: |
          set -euo pipefail
          if cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings; then
            echo "::notice title=clippy dev-feed ($RUNNER_OS)::clean"
          else
            echo "::error title=clippy dev-feed ($RUNNER_OS)::clippy failed with --features dev-feed"; exit 1
          fi

      - name: clippy-guard
        run: |
          set -euo pipefail
          out="$RUNNER_TEMP/guard.txt"
          rc=0
          CARGO_TARGET_DIR="$PWD/target" cargo clippy --manifest-path tests/fixtures/clippy-guard/Cargo.toml --all-targets --locked -- -D warnings > "$out" 2>&1 || rc=$?
          total=$(grep -c '^error: use of a disallowed method `' "$out" || true)
          from_os=$(grep -c '^error: use of a disallowed method `krate_manager_lib::os_roots::FromOs::from_os`' "$out" || true)
          new=$(grep -c '^error: use of a disallowed method `krate_manager_core::roots::Roots::new`' "$out" || true)
          envvar=$(grep -c '^error: use of a disallowed method `std::env::var`' "$out" || true)
          lint=$(grep -cE 'disallowed[-_]methods' "$out" || true)
          other=$(grep -E '^error' "$out" | grep -vcE '^error: (use of a disallowed method `|could not compile `)' || true)
          msg="rc=$rc disallowed=$total from_os=$from_os Roots::new=$new std::env::var=$envvar lint-named=$lint other-errors=$other"
          if [ "$rc" != "0" ] && [ "$total" = "3" ] && [ "$from_os" = "1" ] && [ "$new" = "1" ] && [ "$envvar" = "1" ] && [ "$lint" -ge 1 ] && [ "$other" = "0" ]; then
            echo "::notice title=clippy-guard ($RUNNER_OS)::$msg"
          else
            cat "$out"
            echo "::error title=clippy-guard ($RUNNER_OS)::$msg"; exit 1
          fi

      - name: cargo test
        run: |
          set -euo pipefail
          cargo test --workspace --locked 2>&1 | tee "$RUNNER_TEMP/cargo-test.txt"

      - name: typecheck
        run: |
          set -euo pipefail
          npm run typecheck

      - name: lint
        run: |
          set -euo pipefail
          npm run lint

      - name: vitest
        run: |
          set -euo pipefail
          npm test -- --reporter=default --reporter=json --outputFile.json="$RUNNER_TEMP/vitest.json"

      - name: required tests ran
        run: |
          set -euo pipefail
          node - "$RUNNER_TEMP/cargo-test.txt" "$RUNNER_TEMP/vitest.json" "$RUNNER_OS" <<'REQUIRED'
          const fs = require("fs");
          const [cargoLog, vitestJson, runnerOs] = process.argv.slice(2);
          const key = { Windows: "windows", macOS: "macos", Linux: "linux" }[runnerOs];
          const req = JSON.parse(fs.readFileSync("tests/required-tests.json", "utf8"));
          const bad = [];
          if (!key) bad.push(`unknown RUNNER_OS ${runnerOs}`);
          const names = [...req.cargo.all, ...(key ? req.cargo[key] : [])];
          const passed = new Map();
          for (const raw of fs.readFileSync(cargoLog, "utf8").split(/\r?\n/)) {
            const m = /^test (\S+)(?: - should panic)? \.\.\. ok$/.exec(raw);
            if (m) passed.set(m[1], (passed.get(m[1]) || 0) + 1);
          }
          for (const n of names) { const c = passed.get(n) || 0; if (c !== 1) bad.push(`cargo ${n}: ${c} passing line(s), want 1`); }
          const results = JSON.parse(fs.readFileSync(vitestJson, "utf8")).testResults.flatMap((f) => f.assertionResults);
          for (const t of req.vitest) {
            const c = results.filter((r) => r.title === t && r.status === "passed").length;
            if (c !== 1) bad.push(`vitest "${t}": ${c} passed, want 1`);
          }
          for (const b of bad) console.log(`::error title=required tests ran (${runnerOs})::${b}`);
          if (bad.length) process.exit(1);
          console.log(`::notice title=required tests ran (${runnerOs})::${names.length} cargo + ${req.vitest.length} vitest`);
          REQUIRED

      - name: tauri build
        run: |
          set -euo pipefail
          npm run tauri build 2>&1 | tee "$RUNNER_TEMP/tauri-build.txt"

      - name: build warnings
        run: |
          set -euo pipefail
          clean="$RUNNER_TEMP/tauri-build.clean.txt"
          esc=$(printf '\033')
          sed "s/${esc}\[[0-9;]*m//g" "$RUNNER_TEMP/tauri-build.txt" > "$clean"
          finished=$(grep -cE '^ *Finished `release` profile' "$clean" || true)
          n=$(grep -c '^warning:' "$clean" || true)
          if [ "$finished" -ge 1 ] && [ "$n" = "0" ]; then
            echo "::notice title=build warnings ($RUNNER_OS)::0 warning lines in the release build"
          else
            grep '^warning:' "$clean" || true
            echo "::error title=build warnings ($RUNNER_OS)::warnings=$n finished-lines=$finished"; exit 1
          fi

      - name: dev-feed absent
        run: |
          set -euo pipefail
          case "$RUNNER_OS" in Windows) bin=target/release/krate-manager.exe ;; *) bin=target/release/krate-manager ;; esac
          marker=$(grep -cF 'dev-feed-url.txt' src-tauri/src/dev_feed.rs || true)
          hits=$(grep -acF 'dev-feed-url.txt' "$bin" || true)
          if [ "$marker" -ge 1 ] && [ "$hits" = "0" ]; then
            echo "::notice title=dev-feed absent ($RUNNER_OS)::marker in source=$marker, in $bin=0"
          else
            echo "::error title=dev-feed absent ($RUNNER_OS)::marker in source=$marker, in $bin=$hits"; exit 1
          fi

      - name: artifact version
        run: |
          set -euo pipefail
          want=$(node -p "require('./version.json').version")
          case "$RUNNER_OS" in
            Windows)
              got=$(powershell -NoProfile -Command "(Get-Item 'target/release/krate-manager.exe').VersionInfo.ProductVersion" | tr -d '\r')
              nsis="target/release/bundle/nsis/Krate Manager_${want}_x64-setup.exe"
              if [ -f "$nsis" ]; then extra="nsis=present"; else extra="nsis=missing"; got="$got (NSIS file missing)"; fi ;;
            macOS)
              got=$(plutil -extract CFBundleShortVersionString raw "target/release/bundle/macos/Krate Manager.app/Contents/Info.plist")
              extra="app" ;;
            Linux)
              debs=(target/release/bundle/deb/*.deb)
              if [ "${#debs[@]}" = "1" ] && [ -f "${debs[0]}" ]; then got=$(dpkg-deb -f "${debs[0]}" Version); else got="debs=${#debs[@]}"; fi
              extra="deb" ;;
          esac
          if [ "$got" = "$want" ]; then
            echo "::notice title=artifact version ($RUNNER_OS)::$got ($extra)"
          else
            echo "::error title=artifact version ($RUNNER_OS)::got '$got', want '$want' ($extra)"; exit 1
          fi

      - name: 'plist: LSMinimumSystemVersion'
        if: runner.os == 'macOS'
        run: |
          set -euo pipefail
          v=$(plutil -extract LSMinimumSystemVersion raw "target/release/bundle/macos/Krate Manager.app/Contents/Info.plist")
          if [ "$v" = "15.0" ]; then echo "::notice title=plist: LSMinimumSystemVersion::15.0"; else echo "::error title=plist: LSMinimumSystemVersion::$v"; exit 1; fi

      - name: 'plist: no LSFileQuarantineEnabled'
        if: runner.os == 'macOS'
        run: |
          set -euo pipefail
          if ! out=$(plutil -p "target/release/bundle/macos/Krate Manager.app/Contents/Info.plist"); then
            echo "::error title=plist: no LSFileQuarantineEnabled::plutil -p failed"; exit 1
          fi
          if grep -q LSFileQuarantineEnabled <<<"$out"; then
            echo "::error title=plist: no LSFileQuarantineEnabled::key present"; exit 1
          fi
          echo "::notice title=plist: no LSFileQuarantineEnabled::absent"

      - name: codesign verify
        if: runner.os == 'macOS'
        run: |
          set -euo pipefail
          if codesign --verify --deep --strict -v "target/release/bundle/macos/Krate Manager.app"; then
            echo "::notice title=codesign verify::exit 0"
          else
            echo "::error title=codesign verify::codesign --verify --deep --strict failed"; exit 1
          fi

      - name: codesign ad-hoc
        if: runner.os == 'macOS'
        run: |
          set -euo pipefail
          info=$(codesign -dv "target/release/bundle/macos/Krate Manager.app" 2>&1) || true
          if grep -q 'Signature=adhoc' <<<"$info"; then
            echo "::notice title=codesign ad-hoc::Signature=adhoc"
          else
            echo "::error title=codesign ad-hoc::no Signature=adhoc"; exit 1
          fi

      - name: Checkout unchanged
        working-directory: .
        run: |
          set -euo pipefail
          if [ -z "$(git status --porcelain)" ]; then
            echo "::notice title=checkout unchanged ($RUNNER_OS)::clean"
          else
            git status --porcelain
            echo "::error title=checkout unchanged ($RUNNER_OS)::the run changed or added files"; exit 1
          fi
```

No `${{ }}` appears in any `run:` text (Phase 3's FR-014 discipline); `$RUNNER_OS`, `$RUNNER_TEMP`, `$PWD` are runner
environment variables. No step runs the app, the NSIS installer or anything elevated (FR-076).

No `run:` text pipes into `grep -q` (PD-27). Each match test greps a file or a captured variable. The two pipelines
that remain feed readers that consume all their input: `grep -E '^error' "$out" | grep -vcE …` in clippy-guard, and
`… | tr -d '\r'` in artifact version. So `pipefail` cannot see a SIGPIPE from an early-exiting reader. Both
captures fail closed with an annotation:
- A failed `plutil -p` is its own `::error`. An empty `$out` must never read as "key absent".
- `codesign -dv` runs under `|| true`. Its exit status is `codesign verify`'s concern, and an output without
  `Signature=adhoc` takes the `::error` branch.

### 4.17 `feed-tools.yml`, `hygiene.test.js` and the two Node tests

`feed-tools.yml` (FR-081): both path lists gain `'tools/lint-manager-version.js'`,
`'.github/workflows/manager-ci.yml'`, `'apps/krate-manager/package.json'`, `'apps/krate-manager/tests/required-tests.json'`;
`:78` `PLAN = "1..59"`; `NAMES` gains `"lint-manager-version accepts equal versions and names every drift"` and
`"manager-ci.yml keeps the Phase 4 shape"`.

`hygiene.test.js` (FR-082, PD-19): `path.join(TOOLS_DIR, 'lint-manager-version.js')` in `single` (`:35-45`) and
`'tools/lint-manager-version.js'` in `missing` (`:119-133`).

`lint-manager-version.test.js` (SC-012): first statement the guard; `function makeTree(overrides) -> dir` writes the
all-equal tree under `fs.mkdtempSync(path.join(os.tmpdir(), 'kmv-'))` (root `Cargo.toml` with three members,
`crates/core` literal `version = "0.1.0"`, `crates/helper` and `src-tauri` `version.workspace = true`, the four
`tauri*.conf.json`, `package.json` without `version`, `publish = false` present); one top-level test with one subtest
per SC-012 case, each calling `main(['--root', dir], io)` and asserting the exit code and the exact first stderr line;
plus a CRLF variant of the all-equal tree (exit 0); `t.after` removes the tree. Four more subtests cover FR-063 codes
and conditions SC-012 leaves out, still under the one top-level test, so `PLAN` stays `1..59`:
- `src-tauri/tauri.conf.json` deleted → exit 1, first stderr line `lint-manager-version: E_TAURI_CONF:
  src-tauri/tauri.conf.json: …`;
- `tauri.conf.json` = `{` (not JSON) → `E_TAURI_CONF`;
- `tauri.conf.json` without `version` → `E_TAURI_CONF`;
- `crates/helper/Cargo.toml` deleted while `members` still names `crates/helper` → `E_CARGO_TOML` naming
  `crates/helper/Cargo.toml`.

`version.json` stays valid in each of these, so the expected code is the first error the fixed check order (§4.15)
reports.

`manager-ci-shape.test.js` (SC-019, PD-20): reads `.github/workflows/manager-ci.yml`,
`apps/krate-manager/tests/required-tests.json`, `apps/krate-manager/package.json`, and
`.github/workflows/release-manager.yml` if present; `checkManagerCi(files) -> string[]` asserts FR-070 to FR-077,
FR-106, FR-107 (triggers and the three paths ×2, matrix, `permissions: contents: read`, concurrency, `shell: bash` +
working directory, the step names in §4.16 order, each step's command text, the clippy-guard assertions incl. `= "3"`,
`build warnings`, the Windows `ProductVersion` line, the four macOS steps gated `runner.os == 'macOS'`, a `::notice`
and `::error` in every assertion step, `required tests ran` after `vitest` reading `tests/required-tests.json`, no
`secrets.` / `environment:` / FR-076 flag, `dev-feed` only in the two named steps and never on the `tauri build`
line, no `| grep -q` in any `run:` text (PD-27), and the "build warnings" strip built from `printf '\033'` with no
`\x1b` (PD-11)); `package.json` scripts free of FR-076 flags and of `dev-feed`; `required-tests.json` deep-equal to the
`EXPECTED_REQUIRED` constant of §4.18. The real files give `[]`; each SC-019 mutation (string edits on the in-memory
text / JSON) gives a non-empty list. So do two plan-review mutations: the codesign step rewritten as
`codesign -dv "$app" 2>&1 | grep -q 'Signature=adhoc'`, and the strip rewritten as `sed 's/\x1b\[[0-9;]*m//g'`.

### 4.18 `apps/krate-manager/tests/required-tests.json`

```json
{
  "cargo": {
    "all": [
      "roots::tests::new_validates_and_touches_nothing",
      "roots::tests::with_configured_plugin_root_cases",
      "roots::tests::logs_dir_is_state_logs",
      "roots::tests::sandboxed_roots_refuse_an_outside_configured_root",
      "platform::tests::default_roots_touches_nothing",
      "test_support::tests::check_under_temp_cases",
      "test_support::tests::check_under_temp_symlink_escape",
      "test_support::tests::temp_roots_in_outside_panics",
      "test_support::tests::temp_roots_in_refusal_creates_nothing",
      "test_support::tests::temp_roots_on_temp_dir_itself_panics",
      "test_support::tests::temp_roots_inside_temp_ok",
      "core_sources_hold_no_production_literal",
      "injection_scanner_reports_planted_violations",
      "core_dependencies_are_the_fr007_set",
      "src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots",
      "disallowed_methods_expectations_are_the_seven_d6_sites",
      "settings::tests::round_trip_values",
      "settings::tests::written_bytes_are_d10",
      "settings::tests::missing_file_gives_defaults_and_writes_nothing",
      "settings::tests::plugin_root_or_default_uses_vst3_user",
      "settings::tests::load_rejections_keep_defaults_and_file",
      "settings::tests::save_rejects_invalid_values_and_writes_nothing",
      "settings::tests::save_refuses_future_version_file",
      "settings::tests::save_overwrites_corrupt_and_invalid_files",
      "settings::tests::fault_at_each_step_keeps_previous_file",
      "settings::tests::abort_leaves_previous_file_and_stray_temp_is_ignored",
      "real_src_tauri_config_passes_the_guard",
      "planted_config_changes_fail_the_guard",
      "frontend_source_scan_reports_planted_strings",
      "real_app_metadata_passes_the_guard",
      "planted_app_metadata_changes_fail_the_guard",
      "navigation::tests::is_app_origin_cases",
      "navigation::tests::app_origin_cases",
      "helper_stub_exits_2_with_one_line",
      "context::tests::assemble_cases",
      "state_lock::tests::second_acquire_is_held_while_first_lives",
      "state_lock::tests::acquire_creates_state_dir_and_lock_file_only",
      "state_lock::tests::lock_file_survives_drop",
      "state_lock::tests::acquire_io_error_is_named",
      "context::tests::assemble_locked_instance",
      "context::tests::startup_notice_cases",
      "context::tests::configured_root_cases",
      "context::tests::reassemble_cases",
      "context::tests::sandbox_holds_through_assemble_and_reassemble",
      "log_sink::tests::format_line_cases",
      "log_sink::tests::rotating_log_cases",
      "context::tests::startup_errors_are_logged",
      "context::tests::parse_dev_feed_url_cases"
    ],
    "windows": ["platform::windows::tests::default_roots_from_env"],
    "macos": ["platform::macos::tests::default_roots_from_env"],
    "linux": ["platform::linux::tests::default_roots_from_env"]
  },
  "vitest": [
    "shows the title and the version",
    "shows \"version unavailable\" when app_version fails",
    "shows only the notice when another instance is running"
  ]
}
```

Integration-test names (no module prefix in libtest output) are chosen unique across the workspace's binaries, as
FR-077's "exactly one line" needs. The five plan-review additions (PD-22, PD-23, PD-24; SI-11) are in `all`, so
"required tests ran" proves them on every leg. The PD-18 Vitest case is deliberately not in `vitest` (FR-080 pins
that list to SC-017's three).

### 4.19 Roadmap and index edits (FR-090, FR-091)

- After the Phase 4 heading: the status line at close (`**Status: ✅ COMPLETE (PENDING-CI: n item(s)) (date)** — see
  specs/krate-manager-phase4/compliance.md`, the form of `:258`).
- `:473`: "Feed URL, HTTP client base and the elevation launcher are injected the same way." → "The feed URL is
  injected the same way (`AppContext`, Phase 4); the HTTP client base joins `AppContext` in Phase 5 and the elevation
  launcher in Phase 10, each with its production value built in the same setup (Phase 4 spec D17)."
- `:497` only (FR-090 names line 497, so `:496`, which ends "an Ubuntu leg with", is left byte-identical). On line 497
  the parenthetical "(`libwebkit2gtk-4.1-dev libappindicator3-dev librsvg2-dev patchelf`, report line 65)" is replaced
  by "(on `ubuntu-24.04`, the current Tauri prerequisites apt line, https://v2.tauri.app/start/prerequisites/; the report
  line 65 `libappindicator3-dev … patchelf` set belongs to Phase 11's `ubuntu:22.04` container; Phase 4 spec D16)".
  Nothing else on the line changes, and the edit stays one line: "WebKitGTK 4.1 (" before it and ". Each leg runs" after
  it are kept. The close check is `git diff -U0 c66c8557 -- specs/Krate-manager-roadmap.md`, whose hunks must touch only
  the four FR-090 places. It is recorded with SC-023, because SC-023's name-only diff cannot see a line-level breach.
- Phase 12 first-launch bullet (`:882`): append "It states the supported Windows floor, Windows 10 22H2 and Windows 11
  x64 (Q6), which no `tauri.conf.json` key can carry (Phase 4 spec Non-goals)."
- `node tools/gen-specs-index.js` regenerates `specs/INDEX.md` (LF pinned by `.gitattributes`).

---

## 5. Dependencies

Prefer the standard library and first-party Tauri: Phase 4 uses **no Tauri plugin** (single instance is a `std` file
lock testable in core, D22; logging is the `log` facade plus a 60-line sink, D23; `tauri-plugin-opener` is removed),
and each third-party crate below is already in Tauri's own dependency tree except `tempfile`.

| Package | Version | Where | Why | Verified against |
|---|---|---|---|---|
| `tauri` | 2.12.5 (default features) | `src-tauri` | The shell; `rust-version` 1.90 | crates.io API; source tag `tauri-v2.12.5` (https://github.com/tauri-apps/tauri/tree/tauri-v2.12.5); https://docs.rs/tauri/2.12.5 |
| `tauri-build` | 2.7.4 | `src-tauri` build | `try_build` + `AppManifest::commands` (ACL for app commands) | tag `tauri-build-v2.7.4`; https://docs.rs/tauri-build/2.7.4 |
| `serde` | 1.0.229 (`derive`) | core, `src-tauri` | Settings file struct | https://docs.rs/serde/1.0.229 |
| `serde_json` | 1.0.154 | core, `src-tauri` | Settings encode/classify; config guard parses JSON | https://docs.rs/serde_json/1.0.154 |
| `thiserror` | 2.0.21 | core | `Error`/`Display` derives for five error enums; already in Tauri's tree (`tauri` `Cargo.toml:57`) | https://docs.rs/thiserror/2.0.21 |
| `tempfile` | 3.27.0 | core | `NamedTempFile` atomic replace for settings (FR-031); `TempDir` for `fresh_temp_roots` | https://docs.rs/tempfile/3.27.0 |
| `url` | 2.5.8 | core | `Url` for the navigation decision; the same type as `tauri::Url` | https://docs.rs/url/2.5.8 |
| `log` | 0.4.34 | core, `src-tauri` | Facade (D23); Tauri logs through it too | https://docs.rs/log/0.4.34 |
| `@tauri-apps/api` | 2.12.4 | runtime | `invoke`; `mocks` in tests | `npm view`; `packages/api/src/mocks.ts` at `@tauri-apps/api-v2.12.4` |
| `@tauri-apps/cli` | 2.12.4 | dev | `tauri build` | `npm view`; tag `tauri-cli-v2.12.4` |
| `vite` | 8.3.5 | dev | Bundler (template) | `npm view` (engines) |
| `vitest` | 5.0.5 | dev | Tests; JSON reporter for FR-077 | `npm view`; vitest `docs/guide/index.md` |
| `jsdom` | 29.1.1 | dev | Test DOM (30.x needs Node `^24.15.0`; box has 24.7.0) | `npm view jsdom@29.1.1 engines` |
| `typescript` | 6.0.3 | dev | `tsc --noEmit` (typescript-eslint peer `<6.1.0`) | `npm view` |
| `eslint` | 10.14.0 | dev | Linter (D2) | `npm view` |
| `@eslint/js` | 10.0.1 | dev | `recommended` | `npm view` (peer `eslint ^10.0.0`) |
| `typescript-eslint` | 8.72.1 | dev | `strictTypeChecked`, `projectService` | `npm view`; https://typescript-eslint.io/getting-started/typed-linting/ |
| `@types/node` | 26.6.6 | dev | Types for `vite.config.ts` only | `npm view` |
| `actions/checkout` | v6 | CI | Same as `feed-tools.yml` | `feed-tools.yml:47` |
| `actions/setup-node` | v4, Node `'24'` | CI | Same as `feed-tools.yml` | `feed-tools.yml:49-51` |
| `Swatinem/rust-cache` | v2 (latest v2.9.3) | CI | Caches `apps/krate-manager/target` and `~/.cargo` | `gh api repos/Swatinem/rust-cache/releases/latest` |

Rust toolchain: stable via `rust-toolchain.toml` (D4); the box is at 1.99.1 (§1.2).

---

## 6. Safety design

- **Injected roots: what is mechanical and what is not.**
  - `Roots` has one validated constructor (`Roots::new`) and one public derivation (`with_configured_plugin_root`).
    Core reads no environment (clippy `disallowed-methods` over the six `std::env` functions, §4.1), and it computes
    default paths without touching anything (FR-021 token rule on `platform/` and `roots.rs`).
  - Production builds roots once, in `run()` (D6 site 1, through `os_roots.rs`, sites 2-3).
  - **Constructors (clippy, mechanical):** a test can construct a `Roots` only through `temp_roots` /
    `fresh_temp_roots` (site 4), which check first, create second, and refuse the system temp itself (strict
    descendant). The one other route is the reviewed validation site 6. The clippy-guard fixture proves the guard
    catches `from_os`, `Roots::new` and an env read through an import (FR-072). The seven `#[expect]`s prove each
    listed path resolves (an unfulfilled expectation fails `-D warnings`). PD-22's count proves there is no eighth
    site and no group-level suppression.
  - **Derivations (sandbox, mechanical, PD-24):** a `temp_roots` value carries its canonical base. Every derivation of
    it panics on a configured root outside that base, whether through `with_configured_plugin_root` directly, through
    `assemble_with_os` reading a planted `settings.json`, or through `reassemble_with_os`. So no `AppContext` a test
    builds can name a real plug-in folder. Production `Roots` carry no sandbox.
  - **Writes (type, mechanical, PD-25):** every Phase 4 API that writes takes `&Roots` (`settings::save`, `load`,
    `StateLock::acquire`, `RotatingLog::open*`, the sink's `init`). So a test's writes go only where a sandboxed
    `Roots` points.
  - **What stays convention-guarded:** the crate-private `SettingsIo` steps and `test_support`'s own body, which take
    `&Path` and are reviewed code. The sandbox is lexical, so a symlink inside a temp base is not resolved. Phase 8/9
    must canonicalise every install and uninstall target, and check it against the canonical allowed-root set, before
    it writes. Their tests must reach those writes only through a sandboxed `AppContext`.
- **Normal paths (PD-26).** No root, env-derived value or configured root holds a `..` component, and none is a
  filesystem root. Lexical containment checks in later phases therefore start from normal paths, and FR-102's
  equal-path rule cannot be dodged by a second spelling.
- **Verify before extract or execute.** Not applicable: nothing is downloaded, extracted or executed in Phase 4. The
  state dir's `staging/` is named (`Roots::staging_dir`) so Phase 10's helper has one source root to check against.
  The helper runs elevated and the value comes from the unelevated app, so the Phase 10 helper must **re-derive the
  staging root itself** (from its own `Platform` computation) or canonicalise and compare it. It must never trust a
  staging path passed in by the unelevated app. PD-26 makes the app's value normal, but that is not a trust boundary.
- **Zip-slip.** Not applicable (Phase 8). `check_under_temp` is a test guard, not the extraction guard.
- **The elevated helper.** A stub with no operation that exits 2; not bundled (`externalBin` absent, FR-005), not
  launched by anything; no UI path reaches it.
- **Tauri capability set.** `capabilities/main.json` grants window `main` exactly `allow-app-version` and
  `allow-startup-error`; `build.rs` registers the app manifest, so every app command is ACL-checked
  (`webview/mod.rs:2076-2108`); no `core:*`, no plugin permission, no `remote`. Both commands are read-only and take
  no argument, so no path or string from the webview reaches Rust in this phase.
- **No remote content.** D13 CSP (`'self'` only, IPC origins only in `connect-src`), no `devCsp`, `withGlobalTauri:
  false`, the navigation guard refuses every origin but the app's own, and markup sinks are banned twice (ESLint, and
  `config_guard.rs`'s lint-independent scan).
- **Single instance and state integrity.** `manager.lock` keeps two managers from writing one state dir; settings are
  written temp → fsync → rename → dir fsync, and `save` refuses to overwrite a newer schema.
- **No certificate, no key.** `signingIdentity: "-"`; no workflow reads a secret; no key material exists in this
  phase.
- **What a test may touch.** Only `TempDir`s (Rust: every writing API takes a sandboxed `&Roots`, PD-25, PD-24) and
  `os.tmpdir()` (Node, enforced by the write guard); frontend
  tests only `mockIPC`. No test launches the app or the NSIS installer. The local `tauri build` is a build: it writes
  `apps/krate-manager/target/` and the NSIS toolset cache under `%LOCALAPPDATA%\tauri` (spec Safety), nothing else.

---

## 7. Test plan

### 7.1 SC → test

| SC | Test file → test name(s) | Assertion strategy | Where proven |
|---|---|---|---|
| SC-001 | (gate) | Commands of §7.4, exit codes, `^warning:` count 0, `ProductVersion`, `grep -acF` 0, `git status` empty | Local Windows; `check (windows-latest)` all steps |
| SC-002 | `platform/windows_tests.rs` → `platform::windows::tests::default_roots_from_env` | Literal `OsEnv` under `C:\km-fixture`; full `RootPaths` equality; lower-case names; each of four vars missing/empty/`Local` → `Env{var}` | Local; `check (windows-latest)` "required tests ran" |
| SC-003 | `platform/macos_tests.rs` → `platform::macos::tests::default_roots_from_env` | `HOME=/Users/u`; missing/empty/relative → `Env{HOME}` | PENDING-CI `check (macos-latest)` "required tests ran" (`macos` list) |
| SC-004 | `platform/linux_tests.rs` → `platform::linux::tests::default_roots_from_env` | `HOME=/home/u`; XDG set / empty / relative; `HOME` missing | PENDING-CI `check (ubuntu-24.04)` "required tests ran" (`linux` list) |
| SC-005 | `roots_tests.rs` → `new_validates_and_touches_nothing`; `platform/mod_tests.rs` → `default_roots_touches_nothing` | Recursive listing (names, kinds, sizes) of a `fresh_temp_roots()` base before == after; error variants, all through the single `try_new` helper (D6 site 6): relative → `Relative`; no `Vst3User` entry → `MissingRoot { kind: Vst3User }`; no `Vst3Global` entry → `MissingRoot { kind: Vst3Global }` (SI-4); wrong staging → `Staging`; `<base>/a/../b` as a plugin root → `NotNormal { which: "plugin_root" }`; `base.ancestors().last()` (the filesystem root) as `presets.user` → `NotNormal { which: "presets.user" }` (PD-26) | Local; macOS/Linux PENDING-CI (`all`) |
| SC-006 | (CI step) `clippy-guard`; workspace `clippy` | FR-072 counts; `#[expect]` fulfilment = clean workspace clippy | Local by hand (output saved to `evidence/clippy-guard.txt`); PENDING-CI "clippy-guard" `::notice` on macOS / Linux |
| SC-007 | `test_support_tests.rs` → the six names | Fake temp root `td/fake_tmp` inside a fresh `TempDir`; `catch_unwind` + listing; symlink via `std::os::{unix,windows}::fs`; 1314 handling with D6 site 7 | Local (+ `--nocapture` record of whether the symlink case ran); PENDING-CI (`all`) |
| SC-008 | `tests/injection.rs` → five names | Real-tree scan empty (attribute literals exempt, PD-5); planted strings each reported, plus `PathBuf::from("/etc")` in a platform file reported and `#[cfg(target_os = "windows")]` not; `[dependencies]` set; D6 suppression count exactly the §4.1 table with no group suppression, and a planted eighth site reported (PD-22) | Local; PENDING-CI (`all`); `cargo tree -p krate-manager-core -e normal` saved to evidence |
| SC-009 | `settings_tests.rs` → `round_trip_values`, `written_bytes_are_d10`, `missing_file_gives_defaults_and_writes_nothing`, `plugin_root_or_default_uses_vst3_user` | `fresh_temp_roots().state_dir()`; byte equality with the §4.5 literal; listing unchanged | Local; PENDING-CI (`all`) |
| SC-010 | `settings_tests.rs` → `load_rejections_keep_defaults_and_file`, `save_rejects_invalid_values_and_writes_nothing`, `save_refuses_future_version_file`, `save_overwrites_corrupt_and_invalid_files` | Planted files; bytes + `metadata().modified()` before/after; listings. Beyond SC-010's list: `schema_version: -1` → `E_SETTINGS_INVALID` (PD-8); a `linux_plugin_root` with a `..` component and one naming the filesystem root → `E_SETTINGS_INVALID` for both `load` and `save` (PD-26) | Local; PENDING-CI (`all`) |
| SC-011 | `settings_tests.rs` → `fault_at_each_step_keeps_previous_file`, `abort_leaves_previous_file_and_stray_temp_is_ignored` | `FaultIo` at each of 6 (Windows) / 7 (Unix) steps; `AbortAfter` at `Write` and `SyncTemp` (PD-9) | Local; PENDING-CI (`all`: POSIX rename and dir sync) |
| SC-012 | `tools/feed-contract/tests/lint-manager-version.test.js` → `lint-manager-version accepts equal versions and names every drift` | Temp trees; exit code + exact first stderr line per subtest; CRLF variant; plus the FR-063 subtests of §4.17 (`E_TAURI_CONF` ×3, `E_CARGO_TOML` for a member with no `Cargo.toml`) | Local `node --test`; PENDING-CI `feed-tools.yml` `node-test (macos-latest \| ubuntu-latest)`; real tree: "version lint" `::notice` on all legs |
| SC-013 | (CI steps) four macOS steps | `plutil`, `codesign`; output captured before `grep`, never `\| grep -q` under `pipefail` (PD-27, asserted by SC-019) | PENDING-CI `check (macos-latest)` |
| SC-014 | (CI step) "artifact version" | `ProductVersion` / `CFBundleShortVersionString` / `dpkg-deb -f` | Local Windows; PENDING-CI macOS / Linux |
| SC-015 | `tests/config_guard.rs` → five names | `check_config` / `check_frontend` / `check_app_meta` on real files (empty) and on each planted mutation (non-empty), including the PD-23 dependency, manifest, `package.json`, `tsconfig.json`, ESLint and CHANGELOG mutations | Local; PENDING-CI (`all`) |
| SC-016 | `navigation_tests.rs` → `is_app_origin_cases`, `app_origin_cases` | Table of URL pairs | Local; PENDING-CI (`all`) |
| SC-017 | `src/main.test.ts` → three titles (+ the PD-18 case `shows the version when startup_error fails`, outside the pinned list) | `mockIPC` recording commands; DOM text | Local; PENDING-CI (`vitest` list) |
| SC-018 | `crates/helper/tests/stub.rs` → `helper_stub_exits_2_with_one_line` | Spawn the built bin | Local; PENDING-CI (`all`) |
| SC-019 | `tools/feed-contract/tests/manager-ci-shape.test.js` → `manager-ci.yml keeps the Phase 4 shape` | `checkManagerCi` on real and mutated in-memory files | Local; PENDING-CI `feed-tools.yml` all legs |
| SC-020 | (CI step) "Checkout unchanged" | `git status --porcelain` | Local after SC-001; PENDING-CI all legs |
| SC-021 | shape test + grep | No serial flag in `manager-ci.yml`, `package.json`, `vite.config.ts`; `grep -rnE -- '--test-threads\|--no-file-parallelism\|fileParallelism\|maxWorkers\|pool:' apps/krate-manager --exclude-dir=node_modules --exclude-dir=target` recorded empty | Local |
| SC-022 | `node --test` over `tools/feed-contract/tests/*.test.js` | `# fail 0`, plan `1..59`, both names `ok`, hygiene covers the lint | Local; PENDING-CI `feed-tools.yml` three legs |
| SC-023 | (gate) | `git diff --name-only c66c8557 -- . ':!apps/krate-manager'` ⊆ FR-090 list; `git diff -U0 c66c8557 -- specs/Krate-manager-roadmap.md` hunks only at the Phase 4 status line, `:473`, `:497` and the Phase 12 bullet (§4.19; the name-only diff cannot see a line-level breach); `gen-specs-index.js --check` exit 0 | Local |
| SC-024 | `context_tests.rs` → `assemble_cases` | (a)-(c) of the spec; listing diffs | Local; PENDING-CI (`all`) |
| SC-025 | `state_lock_tests.rs` → four names | Two `acquire`s; listing; file kept after drop; state dir = regular file → `Io{CreateDir}` | Local; PENDING-CI (`all`: `flock` vs `LockFileEx`) |
| SC-026 | `context_tests.rs` → `assemble_locked_instance`, `startup_notice_cases` | Held lock + corrupt settings planted; notice table; `LOCKED_EXIT_DELAY == 5 s` | Local; PENDING-CI (`all`) |
| SC-027 | `context_tests.rs` → `configured_root_cases`, `sandbox_holds_through_assemble_and_reassemble`; `roots_tests.rs` → `with_configured_plugin_root_cases`, `sandboxed_roots_refuse_an_outside_configured_root` | `assemble_with_os` / `reassemble_with_os` with each `Os`; `X` derived from `roots.state_dir().parent()` (the canonical base); `<vst3-user>/../vst3-user` → `NotNormal { which: "configured" }`, so no duplicate entry (PD-26); an outside path panics directly, through a planted `settings.json` and through `reassemble` (PD-24) | Local; PENDING-CI (`all`) |
| SC-028 | `context_tests.rs` → `reassemble_cases` | `Arc::ptr_eq`; further `acquire` still `Held`; listing unchanged; `apply_settings` (PD-2) | Local; PENDING-CI (`all`) |
| SC-029 | `log_sink_tests.rs` → `format_line_cases`, `rotating_log_cases` | `open_with_cap(&roots, 200)`; sizes; reopen appends; an over-long line leaves no file over 200 bytes and both valid UTF-8; a failed rotation rename (`.1` is a directory) returns `Err` and leaves the log byte-identical (PD-10) | Local; PENDING-CI (`all`) |
| SC-030 | `context_tests.rs` → `startup_errors_are_logged`; `roots_tests.rs` → `logs_dir_is_state_logs` | A capture `log::Log` installed once with `set_logger` + `set_max_level(Trace)` (the default runtime level is `Off`, §1.2), filtering records by target `STARTUP_LOG_TARGET` **and** by the test thread's `ThreadId`, so parallel tests cannot pollute it | Local; PENDING-CI (`all`) |
| SC-031 | `context_tests.rs` → `parse_dev_feed_url_cases`; CI "clippy dev-feed", "dev-feed absent" | Table; grep counts | Local; PENDING-CI on macOS / Linux |

### 7.2 FR → test (FRs without their own SC)

| FR | Covered by |
|---|---|
| FR-001, FR-003 | SC-020 (checkout unchanged), SC-023; the tree of §2 checked in review |
| FR-002 | SC-012 real-tree run ("version lint"); `real_app_metadata_passes_the_guard` / `planted_app_metadata_changes_fail_the_guard` (members, both `deny`s, `[lints] workspace = true` in every member, PD-23); SC-001 (clippy with the two `deny`s) |
| FR-004, FR-005 | SC-015 (`config_guard.rs`; FR-004's dependency key set, no `tauri-plugin`, featureless `tauri` in `check_config`, PD-23) |
| FR-006 | SC-018 |
| FR-007 | SC-008 (`core_dependencies_are_the_fr007_set`) + evidence `cargo tree` |
| FR-012, FR-013 | SC-002 to SC-005 and SC-027, with the PD-4 and PD-26 cases (`MissingRoot` for both kinds, `NotNormal`, `Env` on `..`) |
| FR-015 | SC-006 (fixture + fulfilled `#[expect]`s) and `disallowed_methods_expectations_are_the_seven_d6_sites` (exactly seven, no group suppression, PD-22) |
| FR-021 | SC-008 (PD-5 attribute exemption and planted cases) |
| FR-022 | `planted_app_metadata_changes_fail_the_guard` (`test-support` only under `[dev-dependencies]`; core `[features]` exactly `test-support = []`, PD-23); SC-001 builds the dev-dependency feature |
| FR-031, FR-032, FR-100, FR-104, FR-105 (signatures) | Compile-time: every writing API takes `&Roots` (PD-25, SI-9); SC-009 to SC-011, SC-025, SC-029 call them with `fresh_temp_roots()` values |
| FR-035, FR-083 | Three mechanical layers (§6). SC-006 plus PD-22: no `Roots` constructor outside the seven sites. The PD-24 sandbox tests (`sandboxed_roots_refuse_an_outside_configured_root`, `sandbox_holds_through_assemble_and_reassemble`): no derivation outside the temp base. PD-25 signatures: writes only through a `Roots`. FR-021 scan |
| FR-040 to FR-043 | SC-015 |
| FR-046, FR-054 | SC-017 (+ the PD-18 case) |
| FR-050 to FR-052 | `planted_app_metadata_changes_fail_the_guard` (dependency set, exact versions, scripts, the six `tsconfig.json` flags, no `types`, `reportUnusedDisableDirectives`, PD-23); SC-001 (`typecheck`, `lint`, `test`); SC-015 (source scan) |
| FR-053 | SC-021 (parallelism keys); SC-001 (`npm test` runs under jsdom) |
| FR-060, FR-061, FR-063 | SC-012 (+ the §4.17 `E_TAURI_CONF` and missing-member `E_CARGO_TOML` subtests) |
| FR-062 | `real_app_metadata_passes_the_guard` / `planted_app_metadata_changes_fail_the_guard` (header wording, `## [<version.json>] - ` heading, PD-23) |
| FR-070 to FR-077, FR-080 | SC-019 + the CI run |
| FR-081, FR-082 | SC-022 |
| FR-090, FR-091 | SC-023 (name-only diff + the roadmap `-U0` hunk check, §4.19) |
| FR-017, FR-103 (mechanism) | SC-015 (`.manage(`), SC-024, SC-028; wording per SI-7 |
| FR-101 | SC-026 + SC-017 + SC-015 (`LOCKED_EXIT_DELAY` in `lib.rs`) |
| FR-105 | SC-030 + SC-015 |
| FR-106, FR-107 | SC-031 + SC-015 + SC-019 |

### 7.3 PENDING-CI rows (exact job and assertion)

| Row | Job | Step | Annotation that closes it |
|---|---|---|---|
| SC-003 | `check (macos-latest)` | required tests ran | `::notice title=required tests ran (macOS)::<n> cargo + 3 vitest` |
| SC-004 | `check (ubuntu-24.04)` | required tests ran | `… (Linux)` |
| SC-005, SC-007 to SC-011, SC-015 to SC-018, SC-024 to SC-031 (Rust / Vitest parts) | `check (macos-latest)`, `check (ubuntu-24.04)` | required tests ran | the same two notices (`all` and `vitest` lists) |
| SC-006 | both | clippy-guard | `::notice title=clippy-guard (<os>)::rc=101 disallowed=3 …` |
| SC-012, SC-019, SC-022 | `feed-tools.yml` `node-test (macos-latest)`, `node-test (ubuntu-latest)`, `node-test (windows-latest)` | node --test | the TAP check's `TAP plan=1..59` line and a green step (that step has no annotation; it is a Phase 1 step and stays unchanged) |
| SC-013 | `check (macos-latest)` | the four plist / codesign steps | four `::notice`s |
| SC-014 | macOS, Linux | artifact version | `::notice title=artifact version (<os>)::0.1.0 (…)` |
| SC-020 | all three | Checkout unchanged | `::notice title=checkout unchanged (<os>)::clean` |
| SC-031 (CI part) | macOS, Linux | clippy dev-feed, dev-feed absent | two `::notice`s |

### 7.4 Local close procedure (Windows, this box)

All from `apps/krate-manager/` unless noted; logs to `f:/tmp/km4/` (outside the repo); wall clock of each command is
recorded in `specs/krate-manager-phase4/evidence/timings.md` for the next run's planning.

1. `rustup show` (expect 1.99.1 stable), `npm ci`.
2. **In parallel** (independent; the cargo commands share one target-dir lock and run in one chain):
   - chain A: `cargo fmt --all --check` → `cargo clippy --workspace --all-targets --locked -- -D warnings` →
     `cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings` →
     `CARGO_TARGET_DIR="$PWD/target" cargo clippy --manifest-path tests/fixtures/clippy-guard/Cargo.toml --all-targets
     --locked -- -D warnings > f:/tmp/km4/guard.txt 2>&1` (must fail with the FR-072 counts; copy to
     `evidence/clippy-guard.txt`) → `cargo test --workspace --locked 2>&1 | tee f:/tmp/km4/cargo-test.txt` →
     `cargo test -p krate-manager-core check_under_temp_symlink_escape -- --nocapture` (records whether the link was
     made) → `cargo tree -p krate-manager-core -e normal > evidence/core-tree.txt`;
   - chain B: `npm run typecheck` → `npm run lint` → `npm test -- --reporter=default --reporter=json
     --outputFile.json=f:/tmp/km4/vitest.json`;
   - chain C (repo root): `node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" > f:/tmp/km4/tap.txt`
     and `node tools/lint-manager-version.js`.
   cargo uses every core in chain A; Node's test runner and Vitest run files in parallel workers in B and C, so the
   three chains overlap without a serial process anywhere.
3. The FR-077 check script run by hand over `f:/tmp/km4/cargo-test.txt` and `vitest.json` with `RUNNER_OS=Windows`
   (copied from §4.16 into the scratchpad, not the repo).
4. After step 2 (the release build would wait on the target-dir lock anyway): `npm run tauri build 2>&1 | tee
   f:/tmp/km4/tauri-build.txt`; the "build warnings", "dev-feed absent" and "artifact version" logic by hand.
5. Repo root: `git status --porcelain` (empty apart from the phase's intended changes before commit; after commit,
   empty), `git diff --name-only c66c8557 -- . ':!apps/krate-manager'`, `node tools/gen-specs-index.js --check`.
6. `node tools/clean-test-output.js --apply` and remove `f:/tmp/km4/` (memory: always clean up test output).

---

## 8. Build integration and gate lanes

**Workspace layout:** a Cargo virtual workspace at `apps/krate-manager/` (three members, one excluded fixture), and
an npm package at the same directory (`package.json` beside `Cargo.toml`, as the template lays it out; `src-tauri`'s
`frontendDist: "../dist"`). `target/` is the shared cargo output for the workspace, the fixture (via
`CARGO_TARGET_DIR`) and `tauri build`.

**npm scripts:** `dev`, `build`, `typecheck`, `lint`, `test`, `tauri` (§4.14). `tauri build` runs `npm run build`
first (`beforeBuildCommand`).

**CI workflow files:** `.github/workflows/manager-ci.yml` (new, §4.16) and `.github/workflows/feed-tools.yml`
(paths, `PLAN`, `NAMES`). `ci.yml`, `release.yml`, `feed.yml`, `docs.yml` untouched (FR-090); `ci.yml`'s path filter
does not fire on `apps/**` (`ci.yml:4-23`).

**Gate lanes for this phase:**

| Lane | Runs | When |
|---|---|---|
| App lane | §7.4 chain A + B + steps 3-4 | Every build-stage gate that touched `apps/krate-manager/**` or `tools/lint-manager-version.js` |
| Pipeline lane | §7.4 chain C | Every gate that touched `tools/**`, `.github/workflows/manager-ci.yml`, `feed-tools.yml`, `apps/krate-manager/package.json` or `tests/required-tests.json` |
| C++ lane | none | No file under `plugins/`, `dsp/`, `tests/` or CMake changes (FR-090), so `node tools/header-consumers.js` has nothing to list and `tools/run-gate.js` is not invoked |

The app lane's `cargo test` is already parallel per binary (default test threads) and Vitest runs files in parallel;
nothing in this phase sets a serial flag (SC-021).

**Measure next time:** the three `manager-ci` legs each compile Tauri twice (check-mode for clippy, release + LTO for
`tauri build`). After the first run, read each step's duration from the run page; if `tauri build` dominates (expected
on `macos-latest`, 3 vCPU), the cheapest win is splitting the leg into two parallel jobs (checks, build), which is a
spec change (FR-070 says one job) to bring to the user with the measured numbers, not to make silently.

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| **WebKitGTK quirks on Linux** (the NVIDIA / DMABUF blank-window class Phase 12's toggle targets) | Phase 4 only builds on Linux; nothing launches the webview, so no runtime quirk can redden CI. The toggle's stored value exists (`linux_graphics_workaround`, default `false`) and takes effect in Phase 12. The apt line is the current prerequisites page's (D16), on a pinned `ubuntu-24.04`. |
| **Windows file locks while a DAW runs** | Nothing is installed in Phase 4. The two files the manager writes can be locked by antivirus: a blocked `settings.json` rename is `E_SETTINGS_IO` at `persist` with the old file intact (SC-011); a blocked log rotation drops that line and keeps the cap (PD-10). |
| **Admin prompts** | None: no elevation code, NSIS `currentUser`, the helper is a stub. The `windows-latest` runner runs elevated with UAC off (spec External facts), which only matters for SC-007's symlink case, now required on CI. |
| **Offline behaviour** | The Phase 4 app does no network I/O at runtime. Build-time: `tauri build` downloads the SHA-1-pinned NSIS toolset on first use (`tauri-bundler` `nsis/mod.rs:38-44, 82-90`), so a first offline local bundle fails; `cargo` and `npm ci` need their registries unless cached. CI always has network. |
| CSP / navigation guard never exercised at runtime in Phase 4 (no step launches the app) | Both are proven statically (SC-015, SC-016) and the origins are Tauri's documented defaults; a guard that refused the app's own page would show a blank window on first launch. Open question §10.1 offers the user a one-time manual launch. |
| D13 applies in `tauri dev` too (no `devCsp`), which may block Vite's HMR websocket on `ws://localhost:1420` | Dev-only inconvenience (reload by hand); the stylesheet is linked from HTML so no injected `<style>` is needed. Not a gate. Whether Tauri injects the CSP into `devUrl` content at all was not verified; either outcome is harmless to the gates. |
| A new stable Rust adds a clippy lint (D4) | The fix is code, never `allow` (`allow_attributes` is denied anyway). |
| `let_and_return` or another style lint on an `#[expect]`-attributed `let` in the real tree | Scratch run showed none (§1.2); if it fires, restructure the statement (e.g. `match`), keep the `expect` on the call. |
| A Tauri macro expansion trips `allow_attributes` | Clippy skips expansion and proc-macro attributes (§1.2, `allow_attributes.rs:10-14`); if one ever fires, the build stops and the main loop decides with the user (spec edge case), no workspace-wide allow. |
| macOS directory fsync (`File::sync_all` on a directory fd) errors on APFS | First proof is the macOS leg (SC-009 / SC-011 PENDING-CI). If it fails, the outcome is a spec ruling (keep the step and fix, or drop the dir sync on macOS), not a silent change. |
| Windows `canonicalize` returns `\\?\` paths | PD-6: every join is one component; SC-007 `temp_roots_inside_temp_ok` exercises it on Windows. |
| Two processes write the log (the locked instance logs its code) | Both open in append mode; the locked instance exits within `LOCKED_EXIT_DELAY`; at worst one rotation races and a line lands in `.1`. Accepted. |
| `windows_subsystem = "windows"` hides the `eprintln!` of a Roots error in a Windows release build | Only reachable with a stripped environment (no `LOCALAPPDATA`); later phases surface `AppState.context` errors in the UI. Accepted for Phase 4. |
| `package-lock.json` loses other platforms' optional native packages | Generated once from an empty `node_modules` (FR-050); `npm ci` on three legs fails loudly if it did. |
| The fixture's `Cargo.lock` drifts from the workspace's | PD-16 procedure; a stale lock makes the clippy-guard step red with a named error. |
| libtest output format change | The FR-077 matcher and the PD-12 should-panic form are the only parse points; a change turns "required tests ran" red, never vacuously green (each name must match once). |

---

## 10. Open questions (for the user)

1. **One manual launch on Windows before close?** Nothing in Phase 4 runs the built app (FR-076; spec Safety: no test
   launches it), so the CSP, the navigation guard, the window and the "already running" path are proven only
   statically. A one-time `target/release/krate-manager.exe` launch (and a second one while the first is open) by the
   user would show them working, at the cost of creating the real state dir
   `%LOCALAPPDATA%\com.krateaudio.krate-manager` (with `manager.lock`, `logs/` and WebView2 data). Should the build
   stage stop for that audition, or close without it?

---

## Review notes

Plan review round 1 (2026-10-30). Every issue was applied; none was rejected. Where an issue offered alternatives:

- **PD-5 attribute literals:** option (a). The lexer exempts literals inside `#[…]` / `#![…]`, with planted pass and
  fail cases. The allow-list stays a list of runtime values (PD-5 "Why").
- **Exact D6 count:** applied as suggested (PD-22). It is widened to `clippy::style`, `clippy::all` and `warnings`,
  because `disallowed-methods` belongs to the first two groups (`clippy-driver -W help`, clippy 0.1.99), so an
  `expect` of a group would silence it with no `disallowed_methods` token.
- **Member manifests, FR-004 / FR-050 to FR-052 / FR-062 facts:** all placed in `config_guard.rs` (PD-23), not in
  `manager-ci-shape.test.js`. `config_guard.rs` runs on every `apps/krate-manager/**` push. `feed-tools.yml`'s path
  list names only `package.json` and `required-tests.json` from the app, so a `tsconfig.json` or `CHANGELOG.md` edit
  would never run the Node test.
- **Negative `schema_version`:** first option. Every integer below 1 is `Invalid { reason: "schema_version below 1" }`,
  which matches FR-033's "< 1" as written, so no SI row is needed (PD-8).
- **Roadmap line 496:** first option. The edit is confined to line 497's parenthetical. SC-023 gains a `-U0` hunk
  check (§4.19).
- **`with_configured_plugin_root` hole:** closed by neither option as written but by a third, stronger one (PD-24):
  a sandbox carried by every `temp_roots` value.
  - Option (a), the clippy entry plus `#[expect]` at the context sites, would have raised the D6 count from seven to
    eleven. It would also have left the settings-file route through `assemble_with_os` open, because that call sits
    at an `#[expect]`ed production site.
  - The sandbox closes the direct call, the settings-file route and `reassemble`, adds no suppression, and needs no
    per-test `assert_roots_under_temp` convention.
  - Option (b)'s honesty requirement is met in §6, which now says which layers are mechanical and which (the
    crate-private `SettingsIo` steps, symlinks inside a temp base) are not. SI-8 carries the spec wording.
- **Bare-path writing APIs:** first option, routing through `&Roots` (PD-25, SI-9), not the absolute-literal test
  scan. The scan would have needed an allow-list for SC-002's `C:\km-fixture` vectors and still could not see a path
  built at run time.
- **Normal paths:** applied as suggested (PD-26, SI-10), with two refinements. `CurDir` needs no rule, because
  `Path::components` drops interior `.` and a leading `.` is relative. The filesystem-root rejection applies to
  `Roots::new` paths and the configured root, but not to env values, so `HOME=/` keeps working: its derived roots
  are normal. The Phase 10 helper's own re-derivation of the staging root is recorded in §6.
- **Pipe into `grep -q`:** capture first (PD-27), plus a shape-test rule. The `plutil -p` capture fails with its own
  `::error`, so an empty capture never reads as "key absent".
- **BSD sed:** portable `printf '\033'` strip (PD-11). `CARGO_TERM_COLOR=never` alone was not taken: a command-line
  `--color always` overrides it.
- **PD-10 / PD-18 tests:** applied. The failed-rename case was added as well as the over-long line, forced
  portably by a directory at `krate-manager.log.1` (std `rename` docs). The PD-18 Vitest case stays out of the pinned
  `vitest` list.
