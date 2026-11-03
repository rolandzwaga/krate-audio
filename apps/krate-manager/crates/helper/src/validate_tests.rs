//! `validate::tests` (owner T005; plan §7.1 rows SC-001, SC-008, SC-027; spec `spec.md:885-994`, `:1151-1159`).
//!
//! Every `run`-driven row works over a fresh `TempDir` through `seams::fake` with an explicit `HelperOs`, so each OS's
//! logic runs on every leg (D18). Paths are built with `Path::join` from temp components.

use super::*;
use crate::classify::classify_inno;
use crate::cli::Args;
use crate::error;
use crate::job::MAX_JOB_BYTES;
use crate::ops::{INNO_FLAGS, INNO_KEY_SUFFIX, INSTALLER, PKGUTIL, UNINSTALL_FLAGS, UNINSTALL_KEY};
use crate::roots::{
    AllowedRoots, HELPER_FOLDER, MacosRoots, OsRoots, STATE_DIR_NAME, StateDirRule, WindowsRoots,
};
use crate::seams::Seams;
use crate::seams::fake::{
    Call, Event, FakeRegistry, Fault, Match, Script, ScriptedRunner, TempIo, World,
};
use crate::{Exit, HelperOs, digest, run};
use serde_json::{Value, json};
use std::collections::BTreeMap;
use std::ffi::{OsStr, OsString};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::Duration;
use tempfile::TempDir;

// ---------------------------------------------------------------------------------------------------------------
// The tree
// ---------------------------------------------------------------------------------------------------------------

/// The one job name every row uses unless it tests the name (FR-021).
const JOB_NAME: &str = "job-0123456789abcdef0123456789abcdef.json";
/// The staged installer's bytes.
const STAGED: &[u8] = b"staged installer bytes\n";
/// A valid Inno AppId (D4).
const APP_ID: &str = "{0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0}";
/// A valid pkg id (FR-033).
const PKG_ID: &str = "com.krateaudio.alpha";
/// The invoking user's uid on the macOS rows (D17).
const USER_UID: u32 = 501;

/// `top` + `parts`, one `Path::join` per component.
fn join(top: &Path, parts: &[&str]) -> PathBuf {
    parts.iter().fold(top.to_path_buf(), |p, c| p.join(c))
}

/// The Windows state dir `<top>/profiles/u/AppData/Local/<STATE_DIR_NAME>` (FR-025).
fn win_state(top: &Path) -> PathBuf {
    join(top, &["profiles", "u", "AppData", "Local", STATE_DIR_NAME])
}

/// The macOS (and Linux) state dir `<top>/macuser/<STATE_DIR_NAME>`.
fn mac_state(top: &Path) -> PathBuf {
    join(top, &["macuser", STATE_DIR_NAME])
}

/// The Windows roots spelled under `top`, `UnderProfiles(<top>/profiles)`.
fn win_roots(top: &Path) -> WindowsRoots {
    WindowsRoots {
        vst3_vendor: join(top, &["win", "vst3", "Vendor"]),
        preset_vendor: join(top, &["win", "presets", "Vendor"]),
        run_base: join(top, &["win", "temp"]),
    }
}

/// The macOS roots spelled under `top` (PD-5: `volume`, `run_base`, `run_ancestors` temp too).
fn mac_roots(top: &Path) -> MacosRoots {
    let volume = join(top, &["vol"]);
    let library = volume.join("Library");
    let support = library.join("Application Support");
    MacosRoots {
        vst3: join(&library, &["Audio", "Plug-Ins", "VST3"]),
        components: join(&library, &["Audio", "Plug-Ins", "Components"]),
        presets: support.join("Vendor"),
        applications: volume.join("Applications"),
        run_base: support.join(HELPER_FOLDER),
        run_ancestors: vec![library, support],
        volume,
    }
}

fn allowed_win(top: &Path) -> AllowedRoots {
    AllowedRoots {
        os: OsRoots::Windows(win_roots(top)),
        state_dir: StateDirRule::UnderProfiles(top.join("profiles")),
    }
}

fn allowed_mac(top: &Path) -> AllowedRoots {
    AllowedRoots {
        os: OsRoots::Macos(mac_roots(top)),
        state_dir: StateDirRule::SameOwnerNonRoot,
    }
}

fn allowed_linux() -> AllowedRoots {
    AllowedRoots {
        os: OsRoots::None,
        state_dir: StateDirRule::SameOwnerNonRoot,
    }
}

/// One test's temp tree (real files) and world (fake clock, owners, calls).
struct Fx {
    /// The `TempDir` as created (macOS: the raw `/var/…` spelling); the `TempIo` sandbox base, which `TempIo` also
    /// takes in its canonical form, so a root left in either spelling (never canonicalized: `run_base`, D7) is inside.
    raw: PathBuf,
    /// Its canonical form; every tree path below.
    base: PathBuf,
    world: World,
    _tmp: TempDir,
}

impl Fx {
    /// The full layout under `<base>` (both OSes' state dirs and roots, a planted `outside/`), the macOS run chain
    /// marked root-owned and the macOS state paths owned by `USER_UID`.
    fn new() -> Fx {
        let fx = Fx::bare();
        fx.layout(&fx.base.clone());
        fx
    }

    /// Only the temp dir and the world.
    fn bare() -> Fx {
        let tmp = TempDir::new().expect("temp dir");
        let raw = tmp.path().to_path_buf();
        let base = fs::canonicalize(&raw).expect("canonical temp dir");
        Fx {
            raw,
            base,
            world: World::new(),
            _tmp: tmp,
        }
    }

    /// Every directory and staged file of both layouts under `top` (a real directory below `base`).
    fn layout(&self, top: &Path) {
        let w = win_roots(top);
        let m = mac_roots(top);
        for d in [
            &w.vst3_vendor,
            &w.preset_vendor,
            &w.run_base,
            &m.vst3,
            &m.components,
            &m.presets,
            &m.applications,
            &m.run_base,
        ] {
            fs::create_dir_all(d).expect("root dir");
        }
        for state in [win_state(top), mac_state(top)] {
            fs::create_dir_all(state.join("jobs")).expect("jobs dir");
            fs::create_dir_all(join(&state, &["staging", "downloads", "ab"])).expect("staging");
        }
        put(
            &join(&win_state(top), &["staging", "downloads", "ab", "a.exe"]),
            STAGED,
        );
        put(
            &join(&mac_state(top), &["staging", "downloads", "ab", "a.pkg"]),
            STAGED,
        );
        let outside = self.base.join("outside");
        fs::create_dir_all(&outside).expect("outside");
        put(&outside.join("x.exe"), b"outside exe\n");
        put(&outside.join("x.pkg"), b"outside pkg\n");
        put(&outside.join("unins000.exe"), b"outside uninstaller\n");
        for d in m.run_ancestors.iter().chain([&m.run_base]) {
            self.world.root_owned(d);
        }
        let state = mac_state(top);
        let parent = state.parent().expect("state parent").to_path_buf();
        for d in [
            parent,
            state.clone(),
            state.join("jobs"),
            state.join("staging"),
        ] {
            self.world.owner(&d, USER_UID, 0o755);
        }
        self.world
            .owner(&state.join("jobs").join(JOB_NAME), USER_UID, 0o644);
    }

    fn path(&self, parts: &[&str]) -> PathBuf {
        join(&self.base, parts)
    }
}

/// Write `bytes` to `path`.
fn put(path: &Path, bytes: &[u8]) {
    fs::write(path, bytes).unwrap_or_else(|e| panic!("cannot write {}: {e}", path.display()));
}

/// A symbolic link to a file (Unix `symlink`, Windows `symlink_file`; Rule 15).
fn link_file(target: &Path, link: &Path) {
    #[cfg(unix)]
    std::os::unix::fs::symlink(target, link).expect("symlink");
    #[cfg(windows)]
    std::os::windows::fs::symlink_file(target, link).unwrap_or_else(|e| {
        panic!("symbolic-link privilege not held: enable Developer Mode ({e})")
    });
}

/// A symbolic link to a directory (Unix `symlink`, Windows `symlink_dir`; Rule 15, plan `:561-565`).
fn link_dir(target: &Path, link: &Path) {
    #[cfg(unix)]
    std::os::unix::fs::symlink(target, link).expect("symlink");
    #[cfg(windows)]
    std::os::windows::fs::symlink_dir(target, link).unwrap_or_else(|e| {
        panic!("symbolic-link privilege not held: enable Developer Mode ({e})")
    });
}

/// What a tree node holds, for the byte-identical comparison.
#[derive(Debug, PartialEq, Eq)]
enum Node {
    Dir,
    File(Vec<u8>),
    Link(PathBuf),
    Other,
}

/// Every node under `root` (relative path → node), links never followed, `skip` left out.
fn snapshot(root: &Path, skip: Option<&Path>) -> BTreeMap<PathBuf, Node> {
    fn walk(root: &Path, dir: &Path, skip: Option<&Path>, out: &mut BTreeMap<PathBuf, Node>) {
        let entries = fs::read_dir(dir).unwrap_or_else(|e| panic!("list {}: {e}", dir.display()));
        for entry in entries {
            let path = entry.expect("entry").path();
            if skip == Some(path.as_path()) {
                continue;
            }
            let meta = fs::symlink_metadata(&path).expect("lstat");
            let node = if meta.file_type().is_symlink() {
                Node::Link(fs::read_link(&path).expect("read link"))
            } else if meta.is_dir() {
                walk(root, &path, skip, out);
                Node::Dir
            } else if meta.is_file() {
                Node::File(fs::read(&path).expect("read"))
            } else {
                Node::Other
            };
            out.insert(
                path.strip_prefix(root).expect("under root").to_path_buf(),
                node,
            );
        }
    }
    let mut out = BTreeMap::new();
    walk(root, root, skip, &mut out);
    out
}

/// `dir` and `rest` joined as text: `PathBuf::push` drops `..` from a verbatim (`\\?\`) path, so a `..` component
/// is spelled out.
fn spelled(dir: &Path, rest: &[&str]) -> String {
    let sep = std::path::MAIN_SEPARATOR_STR;
    format!("{}{sep}{}", s(dir), rest.join(sep))
}

// ---------------------------------------------------------------------------------------------------------------
// Jobs
// ---------------------------------------------------------------------------------------------------------------

fn s(p: &Path) -> &str {
    p.to_str().expect("UTF-8 temp path")
}

fn inno(file: &Path, sha: &[u8; 32]) -> Value {
    json!({ "op": "run_inno_installer", "file": s(file), "sha256": digest::hex(sha) })
}

fn pkg_install(file: &Path, sha: &[u8; 32]) -> Value {
    json!({ "op": "run_pkg_installer", "file": s(file), "sha256": digest::hex(sha) })
}

fn inno_uninstall(app_id: &str) -> Value {
    json!({ "op": "run_inno_uninstaller", "app_id": app_id })
}

fn pkg_uninstall(pkg_id: &str) -> Value {
    json!({ "op": "uninstall_pkg", "pkg_id": pkg_id })
}

fn job_bytes(ops: &[Value]) -> Vec<u8> {
    serde_json::to_vec(&json!({ "job_version": 1, "ops": ops })).expect("job json")
}

/// The staged installer of `os`'s layout under `top` and its op.
fn staged_op(top: &Path, os: HelperOs) -> Value {
    let sha = digest::sha256(STAGED);
    match os {
        HelperOs::Windows => inno(
            &join(&win_state(top), &["staging", "downloads", "ab", "a.exe"]),
            &sha,
        ),
        _ => pkg_install(
            &join(&mac_state(top), &["staging", "downloads", "ab", "a.pkg"]),
            &sha,
        ),
    }
}

fn args(job: &Path, sha: [u8; 32]) -> Args {
    Args {
        job: job.to_path_buf(),
        job_sha256: sha,
        #[cfg(feature = "ci-temp-roots")]
        ci_roots: None,
    }
}

fn argv(parts: &[&str]) -> Vec<OsString> {
    parts.iter().map(OsString::from).collect()
}

fn subkey(app_id: &str) -> String {
    format!("{UNINSTALL_KEY}\\{app_id}{INNO_KEY_SUFFIX}")
}

/// A runner whose every child exits 0 at once and, when its argv names a `/LOG=` file, writes that log (the Inno
/// installer's own log, FR-030 step 5).
fn ok_runner(world: &World) -> ScriptedRunner<'_> {
    let runner = ScriptedRunner::new(world);
    runner.on(
        Match::Any,
        Script {
            effect: Some(Box::new(|argv: &[OsString]| {
                if let Some(log) = argv
                    .iter()
                    .find_map(|a| a.to_str().and_then(|a| a.strip_prefix("/LOG=")))
                {
                    let _ = fs::write(log, b"Log opened.\nInstallation process succeeded.\n");
                }
            })),
            ..Script::default()
        },
    );
    runner
}

/// The three `pkgutil` reads of FR-033 for `pkg_id` (exit 0, these stdouts).
fn pkgutil_reads(runner: &ScriptedRunner<'_>, pkg_id: &str, files: &str, dirs: &str) {
    let info =
        format!("package-id: {pkg_id}\nversion: 1.0.0\nvolume: /\nlocation: /\ninstall-time: 1\n");
    for (prefix, stdout) in [
        (vec![PKGUTIL, "--pkg-info", pkg_id], info.into_bytes()),
        (
            vec![PKGUTIL, "--files", pkg_id, "--only-files"],
            files.as_bytes().to_vec(),
        ),
        (
            vec![PKGUTIL, "--files", pkg_id, "--only-dirs"],
            dirs.as_bytes().to_vec(),
        ),
    ] {
        runner.on(
            Match::Prefix(argv(&prefix)),
            Script {
                stdout,
                ..Script::default()
            },
        );
    }
}

/// One `run` call's inputs besides the seams.
struct Job<'a> {
    os: HelperOs,
    roots: &'a AllowedRoots,
    path: &'a Path,
    sha: [u8; 32],
}

/// The runner and registry of one row.
struct Fakes<'a, 'w> {
    runner: &'a ScriptedRunner<'w>,
    registry: &'a FakeRegistry<'w>,
}

/// `run` over `io` and `fakes` with the production classifier.
fn run_job(io: &dyn crate::seams::HelperIo, job: &Job<'_>, fakes: &Fakes<'_, '_>) -> Exit {
    let seams = Seams {
        io,
        runner: fakes.runner,
        registry: fakes.registry,
        classify: classify_inno,
    };
    run(&args(job.path, job.sha), job.os, job.roots, &seams)
}

/// The result file beside `job` (FR-003).
fn result_path(job: &Path) -> PathBuf {
    let stem = job.file_stem().and_then(OsStr::to_str).expect("job stem");
    job.with_file_name(format!("{stem}.result.json"))
}

fn read_result(job: &Path) -> Value {
    let bytes = fs::read(result_path(job)).expect("result file");
    serde_json::from_slice(&bytes).expect("result json")
}

/// One row's expected verdict.
#[derive(Clone, Copy, Debug)]
enum Want {
    /// Exit 4, this code, nothing written.
    Exit4(&'static str),
    /// Exit 3, `job_error` this code, no op entry.
    Exit3(&'static str),
    /// Exit 0, op 0 this outcome and code.
    Op(&'static str, &'static str),
}

/// One crafted job through `run`: the verdict, zero spawns, no removal outside the run base, the `pkgutil` reads
/// only (no `--forget`), and the temp tree byte-identical before and after (the result file aside when one is due).
fn refuses(label: &str, fx: &Fx, job: &Job<'_>, fakes: &Fakes<'_, '_>, want: Want) {
    let result = result_path(job.path);
    let skip = match want {
        Want::Exit4(_) => None,
        _ => Some(result.as_path()),
    };
    let before = snapshot(&fx.base, skip);
    let io = TempIo::new(&fx.world, &fx.raw);
    let exit = run_job(&io, job, fakes);
    assert_eq!(
        snapshot(&fx.base, skip),
        before,
        "{label}: the temp tree changed"
    );
    assert!(
        fx.world.spawns().is_empty(),
        "{label}: spawned {:?}",
        fx.world.spawns()
    );
    let run_base = match &job.roots.os {
        OsRoots::Windows(w) => Some(w.run_base.clone()),
        OsRoots::Macos(m) => Some(m.run_base.clone()),
        OsRoots::None => None,
    };
    // The run directory and its copy are the helper's own (D7, FR-043); nothing else is ever removed.
    for r in fx.world.removals() {
        assert!(
            run_base.as_ref().is_some_and(|b| under(&r, b, job.os)),
            "{label}: removed {} outside the run directory",
            r.display()
        );
    }
    for call in fx.world.calls() {
        if let Call::Output(a) = call {
            assert!(
                a.get(1)
                    .is_some_and(|f| f == "--pkg-info" || f == "--files"),
                "{label}: ran {a:?}, not a pkgutil read"
            );
        }
    }
    match want {
        Want::Exit4(code) => {
            assert_eq!(
                exit,
                Exit {
                    status: 4,
                    code: Some(code)
                },
                "{label}"
            );
        }
        Want::Exit3(code) => {
            assert_eq!(
                exit,
                Exit {
                    status: 3,
                    code: Some(code)
                },
                "{label}"
            );
            let r = read_result(job.path);
            assert_eq!(r["job_error"], json!(code), "{label}: {r}");
            assert_eq!(r["ops"], json!([]), "{label}: {r}");
        }
        Want::Op(outcome, code) => {
            assert_eq!(
                exit,
                Exit {
                    status: 0,
                    code: None
                },
                "{label}"
            );
            let r = read_result(job.path);
            assert_eq!(r["job_error"], Value::Null, "{label}: {r}");
            assert_eq!(r["ops"][0]["outcome"], json!(outcome), "{label}: {r}");
            assert_eq!(r["ops"][0]["code"], json!(code), "{label}: {r}");
        }
    }
}

/// A fresh tree for `os`; the job `bytes` written at `job_at(state)`; `refuses` with the job's own hash.
fn row(label: &str, os: HelperOs, want: Want, setup: impl FnOnce(&Fx) -> (PathBuf, Vec<u8>)) {
    row_with(label, os, want, |fx, _, _| setup(fx));
}

/// `row` with the runner and registry handed to `setup` for scripts and keys.
fn row_with(
    label: &str,
    os: HelperOs,
    want: Want,
    setup: impl FnOnce(&Fx, &ScriptedRunner<'_>, &FakeRegistry<'_>) -> (PathBuf, Vec<u8>),
) {
    let fx = Fx::new();
    let runner = ScriptedRunner::new(&fx.world);
    let registry = FakeRegistry::new(&fx.world);
    let (job, bytes) = setup(&fx, &runner, &registry);
    if let Some(parent) = job.parent() {
        fs::create_dir_all(parent).expect("job dir");
    }
    put(&job, &bytes);
    let roots = roots_for(&fx.base, os);
    let job = Job {
        os,
        roots: &roots,
        path: &job,
        sha: digest::sha256(&bytes),
    };
    let fakes = Fakes {
        runner: &runner,
        registry: &registry,
    };
    refuses(label, &fx, &job, &fakes, want);
}

fn roots_for(top: &Path, os: HelperOs) -> AllowedRoots {
    match os {
        HelperOs::Windows => allowed_win(top),
        HelperOs::Macos => allowed_mac(top),
        HelperOs::Linux => allowed_linux(),
    }
}

/// The job path of `os`'s state dir under `top`.
fn job_at(top: &Path, os: HelperOs) -> PathBuf {
    let state = match os {
        HelperOs::Windows => win_state(top),
        _ => mac_state(top),
    };
    state.join("jobs").join(JOB_NAME)
}

// ---------------------------------------------------------------------------------------------------------------
// SC-001: crafted jobs (spec `spec.md:896-915`)
// ---------------------------------------------------------------------------------------------------------------

/// Picks a macOS state-dir path (under `<S>`) and the uid it is then reported owned by.
type OwnerPick = fn(&Path) -> (PathBuf, u32);
/// Plants an install under the root and returns its `(UninstallString, InstallLocation)`.
type RegistryValues = fn(&Fx, &Path) -> (String, String);

#[test]
fn rejects_crafted_jobs() {
    // The matchers, and their cross-check against the core's contract (`contract.rs:267`).
    let ids = [
        APP_ID,
        "{8F7C1A2B-3C4D-5E6F-7A8B-9C0D1E2F3A4B}",
        "{8F7C1A2B-3C4D-5E6F-7A8B-9C0D1E2F3A4B}_is1",
        "{8f7c1a2b-3c4d-5e6f-7a8b-9c0d1e2f3a4b}",
        "..\\x",
        "{8F7C1A2B-3C4D-5E6F-7A8B-9C0D1E2F3A4}",
        "{8F7C1A2B3C4D-5E6F-7A8B-9C0D1E2F3A4B0}",
        "8F7C1A2B-3C4D-5E6F-7A8B-9C0D1E2F3A4B",
        "{8F7C1A2B-3C4D-5E6F-7A8B-9C0D1E2F3A4G}",
        "",
    ];
    for k in ids {
        let core = krate_manager_core::contract::check_install_value(
            "windows_uninstall_key",
            &Value::String(format!("{k}_is1")),
        );
        assert_eq!(is_app_id(k), core.is_ok(), "app id {k:?}: core {core:?}");
    }
    assert!(is_app_id(APP_ID));
    let pkgs = [
        PKG_ID,
        "com.krateaudio.a1",
        "com.krateaudio.Alpha",
        "com.example.x",
        "com.krateaudio.a/b",
        "com.krateaudio.",
        "com.krateaudio.a.b",
        "com.krateaudio.a-b",
        "xcom.krateaudio.a",
    ];
    for k in pkgs {
        let core = krate_manager_core::contract::check_install_value(
            "macos_pkg_id",
            &Value::String(k.to_owned()),
        );
        assert_eq!(is_pkg_id(k), core.is_ok(), "pkg id {k:?}: core {core:?}");
    }
    assert!(is_pkg_id(PKG_ID));
    for (name, ok) in [
        ("Alpha", true),
        ("A", true),
        (&*format!("A{}", "b".repeat(63)), true),
        (&*format!("A{}", "b".repeat(64)), false),
        ("alpha", false),
        ("Al pha", false),
        ("", false),
        ("1Alpha", false),
    ] {
        assert_eq!(is_plugin_name(name), ok, "plugin name {name:?}");
    }
    for (name, ok) in [
        ("unins000.exe", true),
        ("UNINS123.EXE", true),
        ("unins00.exe", false),
        ("unins0000.exe", false),
        ("unins00a.exe", false),
        ("setup.exe", false),
        ("unins000.exe ", false),
    ] {
        assert_eq!(is_unins_name(name), ok, "uninstaller name {name:?}");
    }
    for (name, ok) in [
        (JOB_NAME, true),
        ("job-0123456789ABCDEF0123456789abcdef.json", false),
        ("job-0123456789abcdef0123456789abcde.json", false),
        ("job-0123456789abcdef0123456789abcdef.JSON", false),
        ("job-0123456789abcdef0123456789abcdef.json.tmp", false),
        ("jab-0123456789abcdef0123456789abcdef.json", false),
    ] {
        assert_eq!(is_job_name(name), ok, "job name {name:?}");
    }

    use HelperOs::{Linux, Macos, Windows};
    let staged_exe = |top: &Path| join(&win_state(top), &["staging", "downloads", "ab", "a.exe"]);
    let sha = digest::sha256(STAGED);

    // Installer sources (FR-020): outside staging, `..`, through a linked directory, a link to a staged file.
    row(
        "file outside staging",
        Windows,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let op = inno(
                &fx.path(&["outside", "x.exe"]),
                &digest::sha256(b"outside exe\n"),
            );
            (job_at(&fx.base, Windows), job_bytes(&[op]))
        },
    );
    row(
        "file with a `..` component",
        Windows,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let state = win_state(&fx.base);
            let file = spelled(
                &state,
                &["staging", "downloads", "..", "downloads", "ab", "a.exe"],
            );
            (
                job_at(&fx.base, Windows),
                job_bytes(&[inno(Path::new(&file), &sha)]),
            )
        },
    );
    row(
        "file with a `..` component (pkg)",
        Macos,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let state = mac_state(&fx.base);
            let file = spelled(&state, &["staging", "downloads", "ab", "..", "ab", "a.pkg"]);
            (
                job_at(&fx.base, Macos),
                job_bytes(&[pkg_install(Path::new(&file), &sha)]),
            )
        },
    );
    row(
        "file through a linked directory inside staging",
        Windows,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let state = win_state(&fx.base);
            link_dir(&fx.path(&["outside"]), &join(&state, &["staging", "ldir"]));
            let file = join(&state, &["staging", "ldir", "x.exe"]);
            (
                job_at(&fx.base, Windows),
                job_bytes(&[inno(&file, &digest::sha256(b"outside exe\n"))]),
            )
        },
    );
    row(
        "file through a linked directory inside staging (pkg)",
        Macos,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let state = mac_state(&fx.base);
            link_dir(&fx.path(&["outside"]), &join(&state, &["staging", "ldir"]));
            let file = join(&state, &["staging", "ldir", "x.pkg"]);
            (
                job_at(&fx.base, Macos),
                job_bytes(&[pkg_install(&file, &digest::sha256(b"outside pkg\n"))]),
            )
        },
    );
    row(
        "file a link to a staged file",
        Windows,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let link = join(
                &win_state(&fx.base),
                &["staging", "downloads", "ab", "l.exe"],
            );
            link_file(&staged_exe(&fx.base), &link);
            (job_at(&fx.base, Windows), job_bytes(&[inno(&link, &sha)]))
        },
    );
    row(
        "file a link to a staged file (pkg)",
        Macos,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let dir = join(&mac_state(&fx.base), &["staging", "downloads", "ab"]);
            link_file(&dir.join("a.pkg"), &dir.join("l.pkg"));
            (
                job_at(&fx.base, Macos),
                job_bytes(&[pkg_install(&dir.join("l.pkg"), &sha)]),
            )
        },
    );
    row(
        "file a directory",
        Windows,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let dir = join(&win_state(&fx.base), &["staging", "downloads", "d.exe"]);
            fs::create_dir_all(&dir).expect("dir");
            (job_at(&fx.base, Windows), job_bytes(&[inno(&dir, &sha)]))
        },
    );
    row(
        "file with the wrong extension",
        Windows,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let file = join(
                &win_state(&fx.base),
                &["staging", "downloads", "ab", "a.msi"],
            );
            put(&file, STAGED);
            (job_at(&fx.base, Windows), job_bytes(&[inno(&file, &sha)]))
        },
    );
    row(
        "pkg file with the Inno extension",
        Macos,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let file = join(
                &mac_state(&fx.base),
                &["staging", "downloads", "ab", "a.exe"],
            );
            put(&file, STAGED);
            (
                job_at(&fx.base, Macos),
                job_bytes(&[pkg_install(&file, &sha)]),
            )
        },
    );
    row(
        "file not absolute",
        Windows,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let op =
                json!({ "op": "run_inno_installer", "file": "a.exe", "sha256": digest::hex(&sha) });
            (job_at(&fx.base, Windows), job_bytes(&[op]))
        },
    );

    // `sha256` not the file's (FR-030 step 2): `failed`, the copy deleted (the run directory's own removals only).
    for os in [Windows, Macos] {
        row(
            "sha256 not the file's",
            os,
            Want::Op("failed", error::HASH.code),
            |fx| {
                let mut op = staged_op(&fx.base, os);
                op["sha256"] = json!(digest::hex(&digest::sha256(b"other bytes")));
                (job_at(&fx.base, os), job_bytes(&[op]))
            },
        );
    }

    // The job itself (FR-001, FR-002, FR-036): exit 3, `E_HELPER_JOB_PARSE`, no op entry.
    let parse = Want::Exit3(error::JOB_PARSE.code);
    for os in [Windows, Macos] {
        row("unknown op value", os, parse, |fx| {
            let mut op = staged_op(&fx.base, os);
            op["op"] = json!("run_shell");
            (job_at(&fx.base, os), job_bytes(&[op]))
        });
        for (key, value) in [
            ("args", json!(["/x"])),
            ("roots", json!(["/"])),
            ("settings_roots", json!(["C:\\Windows"])),
            ("dest", json!("/etc")),
            ("log", json!("x.log")),
        ] {
            row(&format!("extra field {key:?}"), os, parse, |fx| {
                let mut op = staged_op(&fx.base, os);
                op[key] = value;
                (job_at(&fx.base, os), job_bytes(&[op]))
            });
        }
        row("duplicate op key", os, parse, |fx| {
            let text = match os {
                Windows => format!(
                    "{{\"job_version\":1,\"ops\":[{{\"op\":\"run_inno_uninstaller\",\"op\":\"run_inno_installer\",\"app_id\":\"{APP_ID}\"}}]}}"
                ),
                _ => format!(
                    "{{\"job_version\":1,\"ops\":[{{\"op\":\"run_pkg_installer\",\"op\":\"uninstall_pkg\",\"pkg_id\":\"{PKG_ID}\"}}]}}"
                ),
            };
            (job_at(&fx.base, os), text.into_bytes())
        });
        row("duplicate job key", os, parse, |fx| {
            let op = staged_op(&fx.base, os);
            let text = format!("{{\"job_version\":1,\"job_version\":1,\"ops\":[{op}]}}");
            (job_at(&fx.base, os), text.into_bytes())
        });
        row("job_version 2", os, parse, |fx| {
            let bytes =
                serde_json::to_vec(&json!({ "job_version": 2, "ops": [staged_op(&fx.base, os)] }))
                    .expect("json");
            (job_at(&fx.base, os), bytes)
        });
        row("33 ops", os, parse, |fx| {
            let ops = vec![staged_op(&fx.base, os); 33];
            (job_at(&fx.base, os), job_bytes(&ops))
        });
        row("0 ops", os, parse, |fx| {
            (job_at(&fx.base, os), job_bytes(&[]))
        });
    }

    // The op's target OS is not `os` (FR-036, D18).
    row("an Inno op with os = Macos", Macos, parse, |fx| {
        (
            job_at(&fx.base, Macos),
            job_bytes(&[staged_op(&fx.base, Windows)]),
        )
    });
    row("an Inno uninstall with os = Macos", Macos, parse, |fx| {
        (
            job_at(&fx.base, Macos),
            job_bytes(&[inno_uninstall(APP_ID)]),
        )
    });
    row("a pkg op with os = Windows", Windows, parse, |fx| {
        (
            job_at(&fx.base, Windows),
            job_bytes(&[staged_op(&fx.base, Macos)]),
        )
    });
    row("a pkg uninstall with os = Windows", Windows, parse, |fx| {
        (
            job_at(&fx.base, Windows),
            job_bytes(&[pkg_uninstall(PKG_ID)]),
        )
    });
    for (label, op) in [
        ("run_inno_installer with os = Linux", 0),
        ("run_inno_uninstaller with os = Linux", 1),
        ("run_pkg_installer with os = Linux", 2),
        ("uninstall_pkg with os = Linux", 3),
    ] {
        row(label, Linux, parse, |fx| {
            let op = match op {
                0 => staged_op(&fx.base, Windows),
                1 => inno_uninstall(APP_ID),
                2 => staged_op(&fx.base, Macos),
                _ => pkg_uninstall(PKG_ID),
            };
            (job_at(&fx.base, Linux), job_bytes(&[op]))
        });
    }

    // The job's hash and its bound (FR-023).
    for os in [Windows, Macos] {
        let fx = Fx::new();
        let job = job_at(&fx.base, os);
        put(&job, &job_bytes(&[staged_op(&fx.base, os)]));
        let runner = ScriptedRunner::new(&fx.world);
        let registry = FakeRegistry::new(&fx.world);
        let roots = roots_for(&fx.base, os);
        let wrong = Job {
            os,
            roots: &roots,
            path: &job,
            sha: digest::sha256(b"another job"),
        };
        let fakes = Fakes {
            runner: &runner,
            registry: &registry,
        };
        refuses(
            "job bytes not --job-sha256",
            &fx,
            &wrong,
            &fakes,
            Want::Exit3(error::JOB_HASH.code),
        );
        for (label, len) in [
            ("job of 64 KiB + 1 byte", MAX_JOB_BYTES + 1),
            ("job of 4 x 64 KiB", 4 * MAX_JOB_BYTES),
        ] {
            let fx = Fx::new();
            let job = job_at(&fx.base, os);
            let mut bytes = job_bytes(&[staged_op(&fx.base, os)]);
            bytes.resize(usize::try_from(len).expect("len"), b' ');
            put(&job, &bytes);
            let runner = ScriptedRunner::new(&fx.world);
            let registry = FakeRegistry::new(&fx.world);
            let io = Counting::new(TempIo::new(&fx.world, &fx.raw));
            let before = snapshot(&fx.base, Some(&result_path(&job)));
            let roots = roots_for(&fx.base, os);
            let call = Job {
                os,
                roots: &roots,
                path: &job,
                sha: digest::sha256(&bytes),
            };
            let fakes = Fakes {
                runner: &runner,
                registry: &registry,
            };
            let exit = run_job(&io, &call, &fakes);
            assert_eq!(
                exit,
                Exit {
                    status: 3,
                    code: Some(error::JOB_READ.code)
                },
                "{label}"
            );
            assert_eq!(
                snapshot(&fx.base, Some(&result_path(&job))),
                before,
                "{label}"
            );
            assert_eq!(
                read_result(&job)["job_error"],
                json!(error::JOB_READ.code),
                "{label}"
            );
            let reads = io.reads.lock().expect("reads").clone();
            assert_eq!(reads.len(), 1, "{label}: one read of the job, {reads:?}");
            assert!(
                reads.iter().all(|n| *n as u64 <= MAX_JOB_BYTES + 1),
                "{label}: the read went past the bound: {reads:?}"
            );
            assert!(fx.world.spawns().is_empty(), "{label}");
        }
    }

    // The job's location (FR-021, FR-025, D17): exit 4, nothing written.
    let location = Want::Exit4(error::JOB_LOCATION.code);
    for os in [Windows, Macos] {
        let state = |fx: &Fx| match os {
            Windows => win_state(&fx.base),
            _ => mac_state(&fx.base),
        };
        row("job beside jobs/, not in it", os, location, |fx| {
            (
                state(fx).join(JOB_NAME),
                job_bytes(&[staged_op(&fx.base, os)]),
            )
        });
        row("job outside every state dir", os, location, |fx| {
            (
                fx.path(&["outside", JOB_NAME]),
                job_bytes(&[staged_op(&fx.base, os)]),
            )
        });
        row("job in a directory not named jobs", os, location, |fx| {
            (
                join(&state(fx), &["staging", JOB_NAME]),
                job_bytes(&[staged_op(&fx.base, os)]),
            )
        });
        for name in [
            "job-0123456789ABCDEF0123456789abcdef.json",
            "job-123.json",
            "job-0123456789abcdef0123456789abcdef.JSON",
            "settings.json",
        ] {
            row(&format!("job named {name:?}"), os, location, |fx| {
                (
                    join(&state(fx), &["jobs", name]),
                    job_bytes(&[staged_op(&fx.base, os)]),
                )
            });
        }
        {
            // Written at its real path; handed over spelled with `..` (a verbatim path does not resolve `..`, so
            // the spelled path is never written through).
            let fx = Fx::new();
            let bytes = job_bytes(&[staged_op(&fx.base, os)]);
            put(&state(&fx).join("jobs").join(JOB_NAME), &bytes);
            let spelled_job =
                PathBuf::from(spelled(&state(&fx), &["jobs", "..", "jobs", JOB_NAME]));
            let roots = roots_for(&fx.base, os);
            let runner = ScriptedRunner::new(&fx.world);
            let registry = FakeRegistry::new(&fx.world);
            refuses(
                "job path not canonical (a `..` component)",
                &fx,
                &Job {
                    os,
                    roots: &roots,
                    path: &spelled_job,
                    sha: digest::sha256(&bytes),
                },
                &Fakes {
                    runner: &runner,
                    registry: &registry,
                },
                location,
            );
        }
        row("job reached through a linked jobs/", os, location, |fx| {
            let real = fx.path(&["outside", "jobs"]);
            fs::create_dir_all(&real).expect("dir");
            let st = state(fx);
            fs::remove_dir(st.join("jobs")).expect("rm jobs");
            link_dir(&real, &st.join("jobs"));
            (
                st.join("jobs").join(JOB_NAME),
                job_bytes(&[staged_op(&fx.base, os)]),
            )
        });
    }
    row(
        "a shaped state dir outside the profiles",
        Windows,
        location,
        |fx| {
            let st = join(&fx.base, &["elsewhere", STATE_DIR_NAME]);
            fs::create_dir_all(st.join("staging")).expect("dir");
            (
                st.join("jobs").join(JOB_NAME),
                job_bytes(&[staged_op(&fx.base, Windows)]),
            )
        },
    );
    row("<S> two levels below a profile", Windows, location, |fx| {
        let st = join(
            &fx.base,
            &["profiles", "u", "x", "AppData", "Local", STATE_DIR_NAME],
        );
        fs::create_dir_all(st.join("staging")).expect("dir");
        (
            st.join("jobs").join(JOB_NAME),
            job_bytes(&[staged_op(&fx.base, Windows)]),
        )
    });
    row(
        "<S> directly in the profiles folder",
        Windows,
        location,
        |fx| {
            let st = join(&fx.base, &["profiles", "AppData", "Local", STATE_DIR_NAME]);
            fs::create_dir_all(st.join("staging")).expect("dir");
            (
                st.join("jobs").join(JOB_NAME),
                job_bytes(&[staged_op(&fx.base, Windows)]),
            )
        },
    );
    row("<S> under Roaming, not Local", Windows, location, |fx| {
        let st = join(
            &fx.base,
            &["profiles", "u", "AppData", "Roaming", STATE_DIR_NAME],
        );
        fs::create_dir_all(st.join("staging")).expect("dir");
        (
            st.join("jobs").join(JOB_NAME),
            job_bytes(&[staged_op(&fx.base, Windows)]),
        )
    });
    row(
        "<S> not named the app identifier",
        Windows,
        location,
        |fx| {
            let st = join(
                &fx.base,
                &["profiles", "u", "AppData", "Local", "com.example.other"],
            );
            fs::create_dir_all(st.join("staging")).expect("dir");
            (
                st.join("jobs").join(JOB_NAME),
                job_bytes(&[staged_op(&fx.base, Windows)]),
            )
        },
    );
    let owners: [(&str, OwnerPick); 5] = [
        ("<S>'s parent owned by uid 0", |st| {
            (st.parent().expect("parent").to_path_buf(), 0)
        }),
        ("<S> owned by uid 0", |st| (st.to_path_buf(), 0)),
        ("jobs/ owned by uid 0", |st| (st.join("jobs"), 0)),
        ("staging/ owned by another uid", |st| {
            (st.join("staging"), USER_UID + 1)
        }),
        ("the job owned by another uid", |st| {
            (st.join("jobs").join(JOB_NAME), USER_UID + 1)
        }),
    ];
    for (label, pick) in owners {
        for os in [Macos, Linux] {
            row(label, os, location, |fx| {
                let (path, uid) = pick(&mac_state(&fx.base));
                fx.world.owner(&path, uid, 0o755);
                (
                    job_at(&fx.base, os),
                    job_bytes(&[staged_op(&fx.base, Macos)]),
                )
            });
        }
    }
    row(
        "staging/ a link under SameOwnerNonRoot",
        Macos,
        location,
        |fx| {
            let st = mac_state(&fx.base);
            fs::remove_dir_all(st.join("staging")).expect("rm staging");
            link_dir(&fx.path(&["outside"]), &st.join("staging"));
            (job_at(&fx.base, Macos), job_bytes(&[pkg_uninstall(PKG_ID)]))
        },
    );
    // An absent staging/ owns nothing and holds no source, so the location stands (an uninstall-only job on a
    // fresh state dir); the op is then refused on its own defect, proving the job got past FR-025.
    row(
        "staging/ absent under SameOwnerNonRoot",
        Macos,
        Want::Op("refused", error::PKG_ID.code),
        |fx| {
            fs::remove_dir_all(mac_state(&fx.base).join("staging")).expect("rm staging");
            (
                job_at(&fx.base, Macos),
                job_bytes(&[pkg_uninstall("com.example.x")]),
            )
        },
    );

    // The result file (FR-003, SI-12): a planted name, a link at it, a write fault.
    for os in [Windows, Macos] {
        row(
            "result file already present",
            os,
            Want::Exit4(error::RESULT_EXISTS.code),
            |fx| {
                let job = job_at(&fx.base, os);
                put(&result_path(&job), b"planted\n");
                (job, job_bytes(&[staged_op(&fx.base, os)]))
            },
        );
        row(
            "a symlink planted at the result name",
            os,
            Want::Exit4(error::RESULT_EXISTS.code),
            |fx| {
                let job = job_at(&fx.base, os);
                link_file(&fx.path(&["outside", "x.exe"]), &result_path(&job));
                (job, job_bytes(&[staged_op(&fx.base, os)]))
            },
        );
        row(
            "a dangling symlink at the result name",
            os,
            Want::Exit4(error::RESULT_EXISTS.code),
            |fx| {
                let job = job_at(&fx.base, os);
                link_file(&fx.path(&["outside", "missing.json"]), &result_path(&job));
                (job, job_bytes(&[staged_op(&fx.base, os)]))
            },
        );
        row(
            "the result file cannot be created",
            os,
            Want::Exit4(error::RESULT_WRITE.code),
            |fx| {
                let job = job_at(&fx.base, os);
                let name = result_path(&job).file_name().expect("name").to_os_string();
                fx.world.fault(Fault::CreateNew {
                    name,
                    kind: io::ErrorKind::PermissionDenied,
                });
                (job, job_bytes(&[staged_op(&fx.base, os)]))
            },
        );
    }

    // The Inno uninstaller (FR-031, D4).
    for id in [
        "{8F7C1A2B-3C4D-5E6F-7A8B-9C0D1E2F3A4B}_is1",
        "{8f7c1a2b-3c4d-5e6f-7a8b-9c0d1e2f3a4b}",
        "..\\x",
    ] {
        row_with(
            &format!("app_id {id:?}"),
            Windows,
            Want::Op("refused", error::APP_ID.code),
            |fx, _, reg| {
                reg.set(&subkey(id), Some("unins000.exe"), Some(s(&fx.base)));
                (job_at(&fx.base, Windows), job_bytes(&[inno_uninstall(id)]))
            },
        );
    }
    let install = |fx: &Fx| {
        let dir = win_roots(&fx.base).vst3_vendor.join("Alpha");
        fs::create_dir_all(dir.join("Alpha.vst3")).expect("bundle");
        put(&dir.join("unins000.exe"), b"uninstaller\n");
        put(&dir.join("setup.exe"), b"setup\n");
        dir
    };
    let uninstallers: [(&str, RegistryValues); 6] = [
        ("UninstallString outside the plug-in root", |fx, dir| {
            (
                format!("\"{}\"", s(&fx.path(&["outside", "unins000.exe"]))),
                format!("{}\\", s(dir)),
            )
        }),
        (
            "UninstallString in the root but not unins###.exe",
            |_, dir| {
                (
                    format!("\"{}\"", s(&dir.join("setup.exe"))),
                    s(dir).to_owned(),
                )
            },
        ),
        (
            "UninstallString with an argument after the quoted path",
            |_, dir| {
                (
                    format!("\"{}\" /SILENT", s(&dir.join("unins000.exe"))),
                    s(dir).to_owned(),
                )
            },
        ),
        ("UninstallString with an unbalanced quote", |_, dir| {
            (
                format!("\"{}", s(&dir.join("unins000.exe"))),
                s(dir).to_owned(),
            )
        }),
        ("InstallLocation nested two levels deep", |_, dir| {
            let nested = dir.join("Nested");
            fs::create_dir_all(&nested).expect("nested");
            put(&nested.join("unins000.exe"), b"nested\n");
            (
                format!("\"{}\"", s(&nested.join("unins000.exe"))),
                s(&nested).to_owned(),
            )
        }),
        ("InstallLocation the vendor root itself", |fx, _| {
            let root = win_roots(&fx.base).vst3_vendor;
            put(&root.join("unins000.exe"), b"root\n");
            (
                format!("\"{}\"", s(&root.join("unins000.exe"))),
                s(&root).to_owned(),
            )
        }),
    ];
    for (label, values) in uninstallers {
        row_with(
            label,
            Windows,
            Want::Op("refused", error::UNINSTALLER.code),
            |fx, _, reg| {
                let dir = install(fx);
                let (command, location) = values(fx, &dir);
                reg.set(&subkey(APP_ID), Some(&command), Some(&location));
                (
                    job_at(&fx.base, Windows),
                    job_bytes(&[inno_uninstall(APP_ID)]),
                )
            },
        );
    }
    row_with(
        "UninstallString missing",
        Windows,
        Want::Op("refused", error::UNINSTALLER.code),
        |fx, _, reg| {
            let dir = install(fx);
            reg.set(&subkey(APP_ID), None, Some(s(&dir)));
            (
                job_at(&fx.base, Windows),
                job_bytes(&[inno_uninstall(APP_ID)]),
            )
        },
    );

    // The pkg id (FR-033).
    for id in [
        "com.krateaudio.Alpha",
        "com.example.x",
        "com.krateaudio.a/b",
    ] {
        row_with(
            &format!("pkg_id {id:?}"),
            Macos,
            Want::Op("refused", error::PKG_ID.code),
            |fx, runner, _| {
                let _ = fx;
                pkgutil_reads(runner, id, "", "");
                (job_at(&fx.base, Macos), job_bytes(&[pkg_uninstall(id)]))
            },
        );
    }

    // The receipt (FR-033, FR-034, D16): one bad line refuses the whole op, nothing removed, no `--forget`.
    let bundle_files = "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents/Info.plist\n";
    let bundle_dirs = "Library\nLibrary/Audio\nLibrary/Audio/Plug-Ins\nLibrary/Audio/Plug-Ins/VST3\n\
                       Library/Audio/Plug-Ins/VST3/Alpha.vst3\nLibrary/Audio/Plug-Ins/VST3/Alpha.vst3/Contents\n";
    let plant_bundle = |fx: &Fx| {
        let m = mac_roots(&fx.base);
        fs::create_dir_all(join(&m.vst3, &["Alpha.vst3", "Contents"])).expect("bundle");
        put(
            &join(&m.vst3, &["Alpha.vst3", "Contents", "Info.plist"]),
            b"plist\n",
        );
        fs::create_dir_all(m.volume.join("etc")).expect("etc");
        put(&join(&m.volume, &["etc", "hosts"]), b"hosts\n");
        put(&join(&m.volume, &["Library", "foo"]), b"foo\n");
        put(&join(&m.volume, &["Library", "Audio", "foo"]), b"foo\n");
        put(&m.volume.join("foo"), b"foo\n");
    };
    let receipts: [(&str, String, String); 11] = [
        (
            "--only-files with ../etc/passwd",
            format!("{bundle_files}../etc/passwd\n"),
            bundle_dirs.to_owned(),
        ),
        (
            "--only-files with etc/hosts",
            format!("{bundle_files}etc/hosts\n"),
            bundle_dirs.to_owned(),
        ),
        (
            "--only-dirs with usr/local",
            bundle_files.to_owned(),
            format!("{bundle_dirs}usr/local\n"),
        ),
        (
            "--only-files with Library/foo",
            format!("{bundle_files}Library/foo\n"),
            bundle_dirs.to_owned(),
        ),
        (
            "--only-files with Library/Audio/foo",
            format!("{bundle_files}Library/Audio/foo\n"),
            bundle_dirs.to_owned(),
        ),
        (
            "--only-files with foo at the volume",
            format!("{bundle_files}foo\n"),
            bundle_dirs.to_owned(),
        ),
        (
            "--only-files with an absolute line",
            format!("{bundle_files}/etc/hosts\n"),
            bundle_dirs.to_owned(),
        ),
        (
            "--only-files with an empty segment",
            format!("{bundle_files}Library//foo\n"),
            bundle_dirs.to_owned(),
        ),
        (
            "--only-files with a `.` segment",
            format!("{bundle_files}Library/./foo\n"),
            bundle_dirs.to_owned(),
        ),
        (
            "--only-files listing an allowed root",
            format!("{bundle_files}Library/Audio/Plug-Ins/VST3\n"),
            bundle_dirs.to_owned(),
        ),
        (
            "--only-dirs with Applications/Other.app",
            bundle_files.to_owned(),
            format!("{bundle_dirs}Applications/Other.app\n"),
        ),
    ];
    for (label, files, dirs) in receipts {
        row_with(
            label,
            Macos,
            Want::Op("refused", error::PKG_PATH.code),
            |fx, runner, _| {
                plant_bundle(fx);
                pkgutil_reads(runner, PKG_ID, &files, &dirs);
                (job_at(&fx.base, Macos), job_bytes(&[pkg_uninstall(PKG_ID)]))
            },
        );
    }
    row_with(
        "--only-dirs with a lower-case AUv3 app",
        Macos,
        Want::Op("refused", error::PKG_PATH.code),
        |fx, runner, _| {
            plant_bundle(fx);
            let dirs = format!("{bundle_dirs}Applications/alpha AUv3.app\n");
            pkgutil_reads(runner, PKG_ID, bundle_files, &dirs);
            (job_at(&fx.base, Macos), job_bytes(&[pkg_uninstall(PKG_ID)]))
        },
    );

    // FR-027: a plain `C:\…` `file` while staging is verbatim.
    #[cfg(windows)]
    row(
        "file in plain form, staging verbatim",
        Windows,
        Want::Op("refused", error::SOURCE.code),
        |fx| {
            let verbatim = staged_exe(&fx.base);
            let plain = s(&verbatim)
                .strip_prefix(r"\\?\")
                .expect("a verbatim temp path")
                .to_owned();
            let op =
                json!({ "op": "run_inno_installer", "file": plain, "sha256": digest::hex(&sha) });
            (job_at(&fx.base, Windows), job_bytes(&[op]))
        },
    );
}

/// A `HelperIo` that records the length of every `read_bounded` result and otherwise is `inner`.
struct Counting<'w> {
    inner: TempIo<'w>,
    reads: Mutex<Vec<usize>>,
}

impl<'w> Counting<'w> {
    fn new(inner: TempIo<'w>) -> Counting<'w> {
        Counting {
            inner,
            reads: Mutex::new(Vec::new()),
        }
    }
}

impl crate::seams::HelperIo for Counting<'_> {
    fn read_bounded(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        let bytes = self.inner.read_bounded(path, max)?;
        self.reads.lock().expect("reads").push(bytes.len());
        Ok(bytes)
    }
    fn read_tail(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        self.inner.read_tail(path, max)
    }
    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        self.inner.canonicalize(path)
    }
    fn lstat(&self, path: &Path) -> io::Result<Option<LstatFacts>> {
        self.inner.lstat(path)
    }
    fn open_source(&self, path: &Path) -> io::Result<Box<dyn io::Read + '_>> {
        self.inner.open_source(path)
    }
    fn create_new(&self, path: &Path) -> io::Result<Box<dyn crate::seams::WriteSync + '_>> {
        self.inner.create_new(path)
    }
    fn create_helper_folder(&self, path: &Path) -> io::Result<()> {
        self.inner.create_helper_folder(path)
    }
    fn create_run_dir(&self, path: &Path) -> io::Result<()> {
        self.inner.create_run_dir(path)
    }
    fn list_dir(&self, dir: &Path) -> io::Result<Vec<OsString>> {
        self.inner.list_dir(dir)
    }
    fn remove_file(&self, path: &Path) -> io::Result<()> {
        self.inner.remove_file(path)
    }
    fn remove_dir(&self, path: &Path) -> io::Result<()> {
        self.inner.remove_dir(path)
    }
    fn in_use(&self, path: &Path) -> io::Result<bool> {
        self.inner.in_use(path)
    }
    fn random_hex(&self) -> String {
        self.inner.random_hex()
    }
    fn sleep(&self, d: Duration) {
        self.inner.sleep(d);
    }
    fn now(&self) -> Duration {
        self.inner.now()
    }
}

// ---------------------------------------------------------------------------------------------------------------
// SC-001 valid rows, SC-002 calls
// ---------------------------------------------------------------------------------------------------------------

/// Write `ops` as the job at `job`, run it over `fx` (exit 0) and return the result file.
fn perform(
    fx: &Fx,
    os: HelperOs,
    roots: &AllowedRoots,
    job: &Path,
    ops: &[Value],
    fakes: &Fakes<'_, '_>,
) -> Value {
    let bytes = job_bytes(ops);
    put(job, &bytes);
    let io = TempIo::new(&fx.world, &fx.raw);
    let call = Job {
        os,
        roots,
        path: job,
        sha: digest::sha256(&bytes),
    };
    let exit = run_job(&io, &call, fakes);
    let result = read_result(job);
    assert_eq!(
        exit,
        Exit {
            status: 0,
            code: None
        },
        "{result}"
    );
    result
}

/// The run copy an installer spawn names: `<run_base>/run-<hex>/installer-0.<ext>`.
fn assert_run_copy(copy: &OsString, run_base: &Path, ext: &str, os: HelperOs) {
    let copy = Path::new(copy);
    assert_eq!(
        copy.file_name().and_then(OsStr::to_str),
        Some(&*format!("installer-0.{ext}")),
        "{}",
        copy.display()
    );
    let run_dir = copy.parent().expect("run dir");
    assert!(
        run_dir
            .file_name()
            .and_then(OsStr::to_str)
            .is_some_and(|n| n.starts_with("run-")),
        "{}",
        copy.display()
    );
    let parent = run_dir.parent().expect("run base");
    assert!(
        same_path(parent, run_base, os)
            || same_path(&fs::canonicalize(parent).expect("canon"), run_base, os),
        "{} not under {}",
        copy.display(),
        run_base.display()
    );
}

#[test]
fn performs_valid_jobs() {
    // run_inno_installer (Windows): the copy is spawned with the fixed flags (FR-030).
    {
        let fx = Fx::new();
        let roots = allowed_win(&fx.base);
        let job = job_at(&fx.base, HelperOs::Windows);
        let file = join(
            &win_state(&fx.base),
            &["staging", "downloads", "ab", "a.exe"],
        );
        #[cfg(windows)]
        for p in [&job, &file, &win_state(&fx.base).join("staging")] {
            assert!(
                s(p).starts_with(r"\\?\"),
                "FR-027: {} is verbatim",
                p.display()
            );
        }
        let runner = ok_runner(&fx.world);
        let registry = FakeRegistry::new(&fx.world);
        let r = perform(
            &fx,
            HelperOs::Windows,
            &roots,
            &job,
            &[inno(&file, &digest::sha256(STAGED))],
            &Fakes {
                runner: &runner,
                registry: &registry,
            },
        );
        assert_eq!(r["ops"][0]["outcome"], json!("done"), "{r}");
        let spawns = fx.world.spawns();
        assert_eq!(spawns.len(), 1, "{spawns:?}");
        let a = &spawns[0];
        assert_eq!(a.len(), 6, "{a:?}");
        assert_run_copy(
            &a[0],
            &win_roots(&fx.base).run_base,
            "exe",
            HelperOs::Windows,
        );
        assert_eq!(a[1..5], argv(&INNO_FLAGS)[..], "{a:?}");
        assert!(
            a[5].to_str()
                .is_some_and(|l| l.starts_with("/LOG=") && l.ends_with("inno-0.log")),
            "{a:?}"
        );
        assert!(file.exists(), "the staged original stays");
    }
    // run_inno_uninstaller (Windows): the registry's own path spawned, polled until gone (FR-031, D6).
    {
        let fx = Fx::new();
        let roots = allowed_win(&fx.base);
        let job = job_at(&fx.base, HelperOs::Windows);
        let dir = win_roots(&fx.base).vst3_vendor.join("Alpha");
        let bundle = dir.join("Alpha.vst3");
        let unins = dir.join("unins000.exe");
        fs::create_dir_all(&bundle).expect("bundle");
        put(&unins, b"uninstaller\n");
        let registry = FakeRegistry::new(&fx.world);
        registry.set(
            &subkey(APP_ID),
            Some(&format!("\"{}\"", s(&unins))),
            Some(&format!("{}\\", s(&dir))),
        );
        fx.world
            .at(Duration::from_millis(1), Event::RemoveKey(subkey(APP_ID)));
        let runner = ScriptedRunner::new(&fx.world);
        let (gone_exe, gone_bundle) = (unins.clone(), bundle.clone());
        runner.on(
            Match::Any,
            Script {
                effect: Some(Box::new(move |_: &[OsString]| {
                    let _ = fs::remove_file(&gone_exe);
                    let _ = fs::remove_dir(&gone_bundle);
                })),
                ..Script::default()
            },
        );
        let r = perform(
            &fx,
            HelperOs::Windows,
            &roots,
            &job,
            &[inno_uninstall(APP_ID)],
            &Fakes {
                runner: &runner,
                registry: &registry,
            },
        );
        assert_eq!(r["ops"][0]["outcome"], json!("done"), "{r}");
        let mut want = vec![OsString::from(s(&unins))];
        want.extend(argv(&UNINSTALL_FLAGS));
        assert_eq!(fx.world.spawns(), vec![want]);
    }
    // run_pkg_installer (macOS) reaches `done` on every leg: the run chain is temp and root-owned (PD-5).
    {
        let fx = Fx::new();
        let roots = allowed_mac(&fx.base);
        let job = job_at(&fx.base, HelperOs::Macos);
        let file = join(
            &mac_state(&fx.base),
            &["staging", "downloads", "ab", "a.pkg"],
        );
        let runner = ok_runner(&fx.world);
        let registry = FakeRegistry::new(&fx.world);
        let r = perform(
            &fx,
            HelperOs::Macos,
            &roots,
            &job,
            &[pkg_install(&file, &digest::sha256(STAGED))],
            &Fakes {
                runner: &runner,
                registry: &registry,
            },
        );
        assert_eq!(r["ops"][0]["outcome"], json!("done"), "{r}");
        let spawns = fx.world.spawns();
        assert_eq!(spawns.len(), 1, "{spawns:?}");
        let a = &spawns[0];
        assert_eq!(a.len(), 5, "{a:?}");
        assert_eq!(a[0], OsString::from(INSTALLER));
        assert_eq!(a[1], OsString::from("-pkg"));
        assert_run_copy(&a[2], &mac_roots(&fx.base).run_base, "pkg", HelperOs::Macos);
        assert_eq!(a[3..], argv(&["-target", "/"])[..], "{a:?}");
    }
    // uninstall_pkg (macOS): every listed file removed, the bundle directories deepest first, the structural ones
    // and the root kept (FR-033, FR-034, D16).
    {
        let fx = Fx::new();
        let roots = allowed_mac(&fx.base);
        let job = job_at(&fx.base, HelperOs::Macos);
        let m = mac_roots(&fx.base);
        let contents = join(&m.vst3, &["Alpha.vst3", "Contents"]);
        fs::create_dir_all(&contents).expect("bundle");
        put(&contents.join("Info.plist"), b"plist\n");
        let auv3 = join(&m.applications, &["Alpha AUv3.app", "Contents"]);
        fs::create_dir_all(&auv3).expect("auv3");
        put(&auv3.join("Info.plist"), b"plist\n");
        let runner = ScriptedRunner::new(&fx.world);
        pkgutil_reads(
            &runner,
            PKG_ID,
            "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents/Info.plist\n\
             Applications/Alpha AUv3.app/Contents/Info.plist\r\n",
            "Library\nLibrary/Audio\nLibrary/Audio/Plug-Ins\nLibrary/Audio/Plug-Ins/VST3\n\
             Library/Audio/Plug-Ins/VST3/Alpha.vst3\nLibrary/Audio/Plug-Ins/VST3/Alpha.vst3/Contents\n\
             Applications\nApplications/Alpha AUv3.app\nApplications/Alpha AUv3.app/Contents\n",
        );
        let registry = FakeRegistry::new(&fx.world);
        let r = perform(
            &fx,
            HelperOs::Macos,
            &roots,
            &job,
            &[pkg_uninstall(PKG_ID)],
            &Fakes {
                runner: &runner,
                registry: &registry,
            },
        );
        assert_eq!(r["ops"][0]["outcome"], json!("done"), "{r}");
        assert!(!m.vst3.join("Alpha.vst3").exists(), "the bundle is removed");
        assert!(
            !m.applications.join("Alpha AUv3.app").exists(),
            "the AUv3 app is removed"
        );
        for kept in [
            &m.vst3,
            &m.components,
            &m.applications,
            &m.volume.join("Library"),
        ] {
            assert!(kept.is_dir(), "{} is kept", kept.display());
        }
        assert!(
            fx.world
                .spawns()
                .iter()
                .all(|a| a.first() == Some(&OsString::from(PKGUTIL)))
        );
    }
}

/// The pure `pkg_plan` classification over the real temp tree (FR-034, D16): files, removable directories deepest
/// first, structural directories dropped.
#[test]
fn pkg_plan_classifies() {
    let fx = Fx::new();
    let io = TempIo::new(&fx.world, &fx.raw);
    let m = mac_roots(&fx.base);
    fs::create_dir_all(join(&m.presets, &["Alpha", "Kits"])).expect("presets");
    let files = b"Library/Application Support/Vendor/Alpha/Kits/a.preset\n";
    let dirs = b"Library/Application Support\nLibrary/Application Support/Vendor\n\
                 Library/Application Support/Vendor/Alpha\nLibrary/Application Support/Vendor/Alpha/Kits\n";
    let plan = pkg_plan(&io, &m, "", files, dirs).expect("accepted");
    assert_eq!(
        plan.files,
        vec![join(&m.presets, &["Alpha", "Kits", "a.preset"])]
    );
    assert_eq!(
        plan.dirs,
        vec![
            join(&m.presets, &["Alpha", "Kits"]),
            m.presets.join("Alpha")
        ]
    );
    // `location` (FR-033): joined onto the volume; "/" and "" are the volume itself.
    let plan = pkg_plan(
        &io,
        &m,
        "/Library/Application Support/",
        b"Vendor/Alpha/x\n",
        b"",
    )
    .expect("location");
    assert_eq!(plan.files, vec![join(&m.presets, &["Alpha", "x"])]);
    assert_eq!(
        pkg_plan(&io, &m, "/", b"", b"Library\n").expect("root"),
        PkgPlan {
            files: vec![],
            dirs: vec![]
        }
    );
    for (location, files) in [
        ("..", &b"Library/Audio/Plug-Ins/VST3/x\n"[..]),
        ("Library/../Library", b"Audio/Plug-Ins/VST3/x\n"),
        ("", b"Library/Audio/Plug-Ins/VST3/x\0y\n"),
        ("", b"Library/Audio/Plug-Ins/VST3/\xff\n"),
        ("", b"Library/Audio/Plug-Ins/VST3/\n"),
        ("", b"Applications/Alpha AUv3.app\n"),
        ("", b"Applications/x\n"),
    ] {
        assert_eq!(
            pkg_plan(&io, &m, location, files, b""),
            Err(error::PKG_PATH),
            "{location:?} {:?}",
            String::from_utf8_lossy(files)
        );
    }
}

/// `same_path` / `under` (FR-027): component-wise, ASCII case-insensitive on Windows only.
#[test]
fn path_rules() {
    let a = Path::new("/x/Alpha/b");
    assert!(same_path(a, Path::new("/x/Alpha/b"), HelperOs::Macos));
    assert!(same_path(a, Path::new("/x//Alpha/./b"), HelperOs::Macos));
    assert!(!same_path(a, Path::new("/x/alpha/b"), HelperOs::Macos));
    assert!(same_path(a, Path::new("/X/alpha/B"), HelperOs::Windows));
    assert!(!same_path(a, Path::new("/x/Alpha"), HelperOs::Windows));
    assert!(under(a, Path::new("/x"), HelperOs::Macos));
    assert!(under(a, Path::new("/X/ALPHA"), HelperOs::Windows));
    assert!(!under(a, a, HelperOs::Macos));
    assert!(!under(
        Path::new("/x/Alphabet"),
        Path::new("/x/Alpha"),
        HelperOs::Windows
    ));
    assert!(!under(Path::new("/x"), a, HelperOs::Macos));
    assert!(!under(
        Path::new("x/Alpha/b"),
        Path::new("/x"),
        HelperOs::Macos
    ));
}

/// FR-027 with real `std::fs::canonicalize` output: verbatim paths compare case-insensitively, the `Prefix`
/// included; the plain form of the same path is a different path; the valid Inno job runs.
#[cfg(windows)]
#[test]
fn verbatim_paths_windows() {
    let fx = Fx::new();
    let base = s(&fx.base).to_owned();
    assert!(base.starts_with(r"\\?\"), "{base} is verbatim");
    let lower = format!(r"\\?\{}", base[4..].to_ascii_lowercase());
    assert!(
        same_path(&fx.base, Path::new(&lower), HelperOs::Windows),
        "{lower}"
    );
    assert!(under(
        &fx.base.join("x"),
        Path::new(&lower),
        HelperOs::Windows
    ));
    assert!(
        !same_path(&fx.base, Path::new(&base[4..]), HelperOs::Windows),
        "plain is not verbatim"
    );
    assert!(!under(
        &fx.base.join("x"),
        Path::new(&base[4..]),
        HelperOs::Windows
    ));
    assert!(
        !same_path(&fx.base, Path::new(&lower), HelperOs::Macos),
        "case-sensitive off Windows"
    );

    let roots = allowed_win(&fx.base);
    let job = job_at(&fx.base, HelperOs::Windows);
    let file = join(
        &win_state(&fx.base),
        &["staging", "downloads", "ab", "a.exe"],
    );
    let runner = ok_runner(&fx.world);
    let registry = FakeRegistry::new(&fx.world);
    let r = perform(
        &fx,
        HelperOs::Windows,
        &roots,
        &job,
        &[inno(&file, &digest::sha256(STAGED))],
        &Fakes {
            runner: &runner,
            registry: &registry,
        },
    );
    assert_eq!(r["ops"][0]["outcome"], json!("done"), "{r}");
    assert_eq!(fx.world.spawns().len(), 1);

    // The staged file spelled in the other case is still the staged file, the plain spelling is not (FR-027);
    // `source` reads only `canonicalize` and `lstat`, so it is checked directly.
    let io = TempIo::new(&fx.world, &fx.raw);
    let canonical = crate::roots::CanonicalRoots {
        roots: allowed_win(&fx.base),
    };
    let place = job_location(&io, &job, &canonical, HelperOs::Windows).expect("the job's place");
    let flipped = format!(r"\\?\{}", s(&file)[4..].to_ascii_lowercase());
    assert_eq!(
        source(&io, &flipped, &place, HelperOs::Windows, "exe"),
        Ok(PathBuf::from(&flipped))
    );
    assert_eq!(
        source(&io, &s(&file)[4..], &place, HelperOs::Windows, "exe"),
        Err(error::SOURCE)
    );
    assert_eq!(
        source(&io, s(&file), &place, HelperOs::Macos, "pkg"),
        Err(error::SOURCE),
        "the op's extension"
    );
}

// ---------------------------------------------------------------------------------------------------------------
// SC-008: canonicalization under a symlinked root (spec `spec.md:981-994`)
// ---------------------------------------------------------------------------------------------------------------

/// A tree at `<base>/real`, reached as `<raw>/link` (macOS: `/var/…` → `/private/var/…` on top of the link).
struct Linked {
    fx: Fx,
    /// The canonical spelling: `<base>/real`.
    real: PathBuf,
    /// The linked spelling: `<raw>/link`.
    link: PathBuf,
}

fn linked() -> Linked {
    let fx = Fx::bare();
    let real = fx.base.join("real");
    fs::create_dir_all(&real).expect("real");
    link_dir(&real, &fx.base.join("link"));
    let link = fx.raw.join("link");
    fx.layout(&real);
    // The run chain also under its linked spelling, whichever the canonical roots keep.
    for d in mac_roots(&link)
        .run_ancestors
        .iter()
        .chain([&mac_roots(&link).run_base])
    {
        fx.world.root_owned(d);
    }
    Linked { fx, real, link }
}

/// `installer_os`'s roots spelled under `top`, the state dir `Exact` / `UnderProfiles` under `state_top`.
fn linked_roots(installer_os: HelperOs, top: &Path, state_top: &Path) -> AllowedRoots {
    match installer_os {
        HelperOs::Windows => AllowedRoots {
            os: OsRoots::Windows(win_roots(top)),
            state_dir: StateDirRule::UnderProfiles(state_top.join("profiles")),
        },
        _ => AllowedRoots {
            os: OsRoots::Macos(mac_roots(top)),
            state_dir: StateDirRule::Exact(mac_state(state_top)),
        },
    }
}

/// The staged installer of `installer_os` under `top`, and its extension.
fn staged_file(installer_os: HelperOs, top: &Path) -> PathBuf {
    match installer_os {
        HelperOs::Windows => join(&win_state(top), &["staging", "downloads", "ab", "a.exe"]),
        _ => join(&mac_state(top), &["staging", "downloads", "ab", "a.pkg"]),
    }
}

fn installer_op(installer_os: HelperOs, file: &Path, sha: &[u8; 32]) -> Value {
    match installer_os {
        HelperOs::Windows => inno(file, sha),
        _ => pkg_install(file, sha),
    }
}

/// One installer row of SC-008: `file` (its bytes hashing to `sha`) under `roots` → `done` with one spawn, or
/// `refused` with `refused` and none.
fn linked_installer(
    label: &str,
    l: &Linked,
    (installer_os, roots): (HelperOs, &AllowedRoots),
    (file, sha): (&Path, [u8; 32]),
    refused: Option<&'static str>,
) {
    let job = job_at(&l.real, installer_os);
    let runner = ok_runner(&l.fx.world);
    let registry = FakeRegistry::new(&l.fx.world);
    let fakes = Fakes {
        runner: &runner,
        registry: &registry,
    };
    let op = installer_op(installer_os, file, &sha);
    let r = perform(&l.fx, installer_os, roots, &job, &[op], &fakes);
    match refused {
        Some(code) => {
            assert_eq!(r["ops"][0]["outcome"], json!("refused"), "{label}: {r}");
            assert_eq!(r["ops"][0]["code"], json!(code), "{label}: {r}");
            assert!(
                l.fx.world.spawns().is_empty(),
                "{label}: {:?}",
                l.fx.world.spawns()
            );
        }
        None => {
            assert_eq!(r["ops"][0]["outcome"], json!("done"), "{label}: {r}");
            assert_eq!(l.fx.world.spawns().len(), 1, "{label}");
        }
    }
}

/// One `uninstall_pkg` row of SC-008 with the macOS roots and the volume given under the linked spelling.
fn linked_pkg(l: &Linked, files: &str, dirs: &str) -> Value {
    let roots = linked_roots(HelperOs::Macos, &l.link, &l.link);
    let job = job_at(&l.real, HelperOs::Macos);
    let runner = ScriptedRunner::new(&l.fx.world);
    pkgutil_reads(&runner, PKG_ID, files, dirs);
    let registry = FakeRegistry::new(&l.fx.world);
    let fakes = Fakes {
        runner: &runner,
        registry: &registry,
    };
    perform(
        &l.fx,
        HelperOs::Macos,
        &roots,
        &job,
        &[pkg_uninstall(PKG_ID)],
        &fakes,
    )
}

/// The six rows of the SC-008 table for `installer_os`'s installer and the macOS receipt (FR-020, FR-026, FR-034).
fn symlinked_parent_root(installer_os: HelperOs) {
    let sha = digest::sha256(STAGED);
    let source = Some(error::SOURCE.code);
    let done = None;

    // 1. Roots / StagingRoot linked; `file` canonical inside canonical staging → accepted, `done`.
    let l = linked();
    let roots = linked_roots(installer_os, &l.link, &l.link);
    let file = staged_file(installer_os, &l.real);
    linked_installer(
        "canonical file, linked roots",
        &l,
        (installer_os, &roots),
        (&file, sha),
        done,
    );

    // 2. Roots linked; the same `file` spelled through the link → `E_HELPER_SOURCE` (not canonical).
    let l = linked();
    let roots = linked_roots(installer_os, &l.link, &l.link);
    let file = staged_file(installer_os, &l.link);
    linked_installer(
        "file spelled through the link",
        &l,
        (installer_os, &roots),
        (&file, sha),
        source,
    );

    // 3. Roots canonical; `file` inside staging whose parent is a link to a directory outside → `E_HELPER_SOURCE`.
    let l = linked();
    let roots = linked_roots(installer_os, &l.real, &l.real);
    let downloads = staged_file(installer_os, &l.real)
        .parent()
        .and_then(Path::parent)
        .expect("downloads")
        .to_path_buf();
    link_dir(&l.fx.base.join("outside"), &downloads.join("out"));
    let name = if installer_os == HelperOs::Windows {
        "x.exe"
    } else {
        "x.pkg"
    };
    let outside_sha =
        digest::sha256(&fs::read(l.fx.base.join("outside").join(name)).expect("outside"));
    let file = downloads.join("out").join(name);
    linked_installer(
        "parent a link out of staging",
        &l,
        (installer_os, &roots),
        (&file, outside_sha),
        source,
    );

    // 4. A pkg `--only-files` entry under an allowed root, reached normally → accepted and removed.
    let l = linked();
    let vst3 = mac_roots(&l.real).vst3;
    fs::create_dir_all(vst3.join("Alpha.vst3")).expect("bundle");
    put(&join(&vst3, &["Alpha.vst3", "Info.plist"]), b"plist\n");
    let r = linked_pkg(
        &l,
        "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Info.plist\n",
        "Library/Audio/Plug-Ins/VST3\nLibrary/Audio/Plug-Ins/VST3/Alpha.vst3\n",
    );
    assert_eq!(r["ops"][0]["outcome"], json!("done"), "{r}");
    assert!(!vst3.join("Alpha.vst3").exists(), "the entry is removed");
    assert!(vst3.is_dir(), "the root stays");

    // 5. A pkg entry whose parent is a link inside the root pointing outside every root → `E_HELPER_PKG_PATH`.
    let l = linked();
    let vst3 = mac_roots(&l.real).vst3;
    let target = l.fx.base.join("outside").join("bundle");
    fs::create_dir_all(&target).expect("outside bundle");
    put(&target.join("Info.plist"), b"outside plist\n");
    link_dir(&target, &vst3.join("Beta.vst3"));
    let r = linked_pkg(&l, "Library/Audio/Plug-Ins/VST3/Beta.vst3/Info.plist\n", "");
    assert_eq!(r["ops"][0]["outcome"], json!("refused"), "{r}");
    assert_eq!(r["ops"][0]["code"], json!(error::PKG_PATH.code), "{r}");
    assert!(
        target.join("Info.plist").exists(),
        "nothing removed outside"
    );
    assert!(
        l.fx.world.removals().is_empty(),
        "{:?}",
        l.fx.world.removals()
    );

    // 6. A root whose tail does not exist yet under the linked spelling → accepted (FR-026).
    let l = linked();
    let mut roots = linked_roots(installer_os, &l.link, &l.link);
    match &mut roots.os {
        OsRoots::Windows(w) => {
            w.preset_vendor = join(&l.link, &["win", "missing", "deeper", "Vendor"])
        }
        OsRoots::Macos(m) => {
            m.presets = join(
                &m.volume,
                &["Library", "Application Support", "Missing", "Vendor"],
            )
        }
        OsRoots::None => unreachable!("an installer OS"),
    }
    let file = staged_file(installer_os, &l.real);
    linked_installer(
        "a root with a missing tail",
        &l,
        (installer_os, &roots),
        (&file, sha),
        done,
    );
}

#[cfg(unix)]
#[test]
fn symlinked_parent_root_unix() {
    symlinked_parent_root(HelperOs::Macos);
}

#[cfg(windows)]
#[test]
fn symlinked_parent_root_windows() {
    symlinked_parent_root(HelperOs::Windows);
}

// ---------------------------------------------------------------------------------------------------------------
// SC-027: the run directory's checks (spec `spec.md:1151-1159`)
// ---------------------------------------------------------------------------------------------------------------

#[test]
fn run_dir_ancestors() {
    let dir = |uid, mode| LstatFacts {
        kind: Kind::Dir,
        uid: Some(uid),
        mode: Some(mode),
    };
    // `/Library`, `/Library/Application Support`, the helper folder, the new run directory.
    let good = [dir(0, 0o755), dir(0, 0o755), dir(0, 0o755), dir(0, 0o700)];
    assert_eq!(run_dir_ancestors_ok(&good), Ok(()));
    for (label, at, bad) in [
        ("an ancestor owned by uid 501", 0, dir(501, 0o755)),
        ("group-writable 0o775", 1, dir(0, 0o775)),
        ("world-writable 0o757", 2, dir(0, 0o757)),
        (
            "a symlink",
            3,
            LstatFacts {
                kind: Kind::Symlink,
                uid: Some(0),
                mode: Some(0o755),
            },
        ),
        (
            "no uid (a Windows lstat)",
            0,
            LstatFacts {
                kind: Kind::Dir,
                uid: None,
                mode: None,
            },
        ),
        (
            "a file",
            3,
            LstatFacts {
                kind: Kind::File,
                uid: Some(0),
                mode: Some(0o700),
            },
        ),
    ] {
        let mut facts = good;
        facts[at] = bad;
        assert_eq!(run_dir_ancestors_ok(&facts), Err(error::RUN_DIR), "{label}");
    }
    assert_eq!(
        run_dir_ancestors_ok(&[]),
        Err(error::RUN_DIR),
        "an empty chain"
    );
}

#[test]
fn run_dir_sddl() {
    assert_eq!(RUN_DIR_SDDL, "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)");
}
