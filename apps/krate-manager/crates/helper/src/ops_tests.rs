//! `ops::tests` (owner T006; plan §7.1 SC-002 to SC-006, SC-027): the four ops, the batch runner and the fake
//! sandbox, over `TempDir` roots through `seams::fake` with an explicit `HelperOs` (never the real system).

use super::{
    INNO_INSTALL_TIMEOUT, INNO_KEY_SUFFIX, OpCtx, PKGUTIL, UNINSTALL_KEY, UNINSTALL_POLL_INTERVAL,
    UNINSTALL_POLL_TIMEOUT, run_batch,
};
use crate::classify::{InnoVerdict, classify_inno};
use crate::cli::Args;
use crate::digest;
use crate::job::Op;
use crate::result::{OpEntry, OpOutcome};
use crate::roots::{
    AllowedRoots, CanonicalRoots, HELPER_FOLDER, MacosRoots, OsRoots, STATE_DIR_NAME, StateDirRule,
    WindowsRoots,
};
use crate::seams::fake::{
    Call, Event, FakeRegistry, Fault, Hook, Match, Script, ScriptedRunner, TempIo, World,
};
use crate::seams::{HelperIo, Seams};
use crate::validate::JobPlace;
use crate::{Exit, HelperOs, run};
use serde_json::{Value, json};
use std::any::Any;
use std::cell::RefCell;
use std::ffi::OsString;
use std::fs;
use std::io;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::{Duration, Instant};
use tempfile::TempDir;

const APP_A: &str = "{00000001-0000-4000-8000-000000000001}";
const APP_B: &str = "{00000002-0000-4000-8000-000000000002}";
const PKG_A: &str = "com.krateaudio.alpha";
const STEM: &str = "job-0123456789abcdef0123456789abcdef";

/// A temp tree with a state dir, staging, the Windows roots and a macOS volume, every path canonical.
struct Rig {
    _dir: TempDir,
    base: PathBuf,
    state: PathBuf,
    staging: PathBuf,
    win: WindowsRoots,
    mac: MacosRoots,
}

fn mkdirs(p: &Path) {
    fs::create_dir_all(p).expect("create a temp directory");
}

/// One Inno install as the registry and the disk show it.
struct Unins {
    subkey: String,
    install_dir: PathBuf,
    bundle: PathBuf,
    exe: PathBuf,
}

impl Rig {
    fn new() -> Rig {
        let dir = TempDir::new().expect("temp dir");
        let base = fs::canonicalize(dir.path()).expect("canonical temp dir");
        let state = base.join(STATE_DIR_NAME);
        let staging = state.join("staging");
        mkdirs(&state.join("jobs"));
        mkdirs(&staging.join("downloads"));
        let win = WindowsRoots {
            vst3_vendor: base.join("common").join("VST3").join("Krate Audio"),
            preset_vendor: base.join("data").join("Krate Audio"),
            run_base: base.join("wintemp"),
        };
        let vol = base.join("vol");
        let library = vol.join("Library");
        let support = library.join("Application Support");
        let plugins = library.join("Audio").join("Plug-Ins");
        let mac = MacosRoots {
            vst3: plugins.join("VST3"),
            components: plugins.join("Components"),
            presets: support.join("Krate Audio"),
            applications: vol.join("Applications"),
            volume: vol.clone(),
            run_base: support.join(HELPER_FOLDER),
            run_ancestors: vec![library, support],
        };
        for p in [&win.vst3_vendor, &win.preset_vendor, &win.run_base] {
            mkdirs(p);
        }
        for p in [
            &mac.vst3,
            &mac.components,
            &mac.presets,
            &mac.applications,
            &mac.run_base,
        ] {
            mkdirs(p);
        }
        Rig {
            _dir: dir,
            base,
            state,
            staging,
            win,
            mac,
        }
    }

    fn place(&self) -> JobPlace {
        JobPlace {
            stem: STEM.to_owned(),
            result_path: self.state.join("jobs").join(format!("{STEM}.result.json")),
            staging_root: self.staging.clone(),
        }
    }

    fn roots(&self, os: HelperOs) -> AllowedRoots {
        AllowedRoots {
            os: match os {
                HelperOs::Windows => OsRoots::Windows(self.win.clone()),
                HelperOs::Macos => OsRoots::Macos(self.mac.clone()),
                HelperOs::Linux => OsRoots::None,
            },
            state_dir: StateDirRule::Exact(self.state.clone()),
        }
    }

    /// The macOS run-directory chain marked root-owned (PD-5): both ancestors and the helper folder.
    fn root_own(&self, world: &World) {
        for d in self.mac.run_ancestors.iter().chain([&self.mac.run_base]) {
            world.root_owned(d);
        }
    }

    /// A staged artifact under `<staging>/downloads/`: its canonical path string and its SHA-256.
    fn stage(&self, name: &str, bytes: &[u8]) -> (String, [u8; 32]) {
        let p = self.staging.join("downloads").join(name);
        fs::write(&p, bytes).expect("stage an artifact");
        (
            p.to_str().expect("UTF-8 temp path").to_owned(),
            digest::sha256(bytes),
        )
    }

    fn inno(&self, name: &str, bytes: &[u8]) -> Op {
        let (file, sha256) = self.stage(name, bytes);
        Op::RunInnoInstaller { file, sha256 }
    }

    fn pkg(&self, name: &str, bytes: &[u8]) -> Op {
        let (file, sha256) = self.stage(name, bytes);
        Op::RunPkgInstaller { file, sha256 }
    }

    /// An Inno install of `plugin` under the vendor root, with its uninstaller and its registry key.
    fn inno_install(&self, registry: &FakeRegistry<'_>, app_id: &str, plugin: &str) -> Unins {
        let install_dir = self.win.vst3_vendor.join(plugin);
        let bundle = install_dir.join(format!("{plugin}.vst3"));
        mkdirs(&bundle);
        let exe = install_dir.join("unins000.exe");
        fs::write(&exe, b"uninstaller").expect("write the uninstaller");
        let subkey = format!("{UNINSTALL_KEY}\\{app_id}{INNO_KEY_SUFFIX}");
        let quoted = format!("\"{}\"", exe.to_str().expect("UTF-8 temp path"));
        registry.set(
            &subkey,
            Some(&quoted),
            Some(install_dir.to_str().expect("UTF-8 temp path")),
        );
        Unins {
            subkey,
            install_dir,
            bundle,
            exe,
        }
    }

    /// `rel` (a receipt line) under the macOS volume.
    fn on_vol(&self, rel: &str) -> PathBuf {
        rel.split('/')
            .fold(self.mac.volume.clone(), |p, segment| p.join(segment))
    }

    /// Creates the receipt's directories and files and returns `(--only-files, --only-dirs)` stdout.
    fn receipt(&self, dirs: &[&str], files: &[&str]) -> (Vec<u8>, Vec<u8>) {
        for d in dirs {
            mkdirs(&self.on_vol(d));
        }
        for f in files {
            fs::write(self.on_vol(f), b"receipt file").expect("write a receipt file");
        }
        let listing = |lines: &[&str]| lines.iter().map(|l| format!("{l}\n")).collect::<String>();
        (listing(files).into_bytes(), listing(dirs).into_bytes())
    }
}

/// The installer uninstall: the exe, the bundle and the key vanish at `at` on the fake clock.
fn vanish_at(world: &World, unins: &Unins, at: Duration) {
    world.at(at, Event::RemovePath(unins.exe.clone()));
    world.at(at, Event::RemovePath(unins.bundle.clone()));
    world.at(at, Event::RemoveKey(unins.subkey.clone()));
}

fn batch(rig: &Rig, os: HelperOs, ops: &[Op], seams: &Seams<'_>) -> Vec<OpEntry> {
    let roots = CanonicalRoots {
        roots: rig.roots(os),
    };
    let place = rig.place();
    run_batch(
        ops,
        &OpCtx {
            os,
            roots: &roots,
            place: &place,
        },
        seams,
    )
}

fn job_json(ops: &[Op]) -> Vec<u8> {
    let ops: Vec<Value> = ops
        .iter()
        .map(|op| match op {
            Op::RunInnoInstaller { file, sha256 } | Op::RunPkgInstaller { file, sha256 } => {
                json!({ "op": op.kind(), "file": file, "sha256": digest::hex(sha256) })
            }
            Op::RunInnoUninstaller { app_id } => json!({ "op": op.kind(), "app_id": app_id }),
            Op::UninstallPkg { pkg_id } => json!({ "op": op.kind(), "pkg_id": pkg_id }),
        })
        .collect();
    serde_json::to_vec(&json!({ "job_version": 1, "ops": ops })).expect("job json")
}

/// The whole `run` over a job written at `<S>/jobs/<STEM>.json`: its `Exit` and the result file, parsed.
fn run_job(rig: &Rig, os: HelperOs, ops: &[Op], seams: &Seams<'_>) -> (Exit, Value) {
    let bytes = job_json(ops);
    let job = rig.state.join("jobs").join(format!("{STEM}.json"));
    fs::write(&job, &bytes).expect("write the job");
    let args = Args {
        job,
        job_sha256: digest::sha256(&bytes),
        #[cfg(feature = "ci-temp-roots")]
        ci_roots: None,
    };
    let exit = run(&args, os, &rig.roots(os), seams);
    let written = fs::read(rig.place().result_path).expect("the result file was written");
    (exit, serde_json::from_slice(&written).expect("result json"))
}

fn strs(v: &[&str]) -> Vec<OsString> {
    v.iter().map(OsString::from).collect()
}

fn summary(entries: &[OpEntry]) -> Vec<(OpOutcome, Option<&'static str>)> {
    entries.iter().map(|e| (e.outcome, e.code)).collect()
}

fn spawned(world: &World) -> Vec<Vec<OsString>> {
    world
        .calls()
        .into_iter()
        .filter_map(|c| match c {
            Call::Spawn(argv) => Some(argv),
            _ => None,
        })
        .collect()
}

/// Every child the helper started, `spawn` and `output` alike, in order.
fn launched(world: &World) -> Vec<Vec<OsString>> {
    world
        .calls()
        .into_iter()
        .filter_map(|c| match c {
            Call::Spawn(argv) | Call::Output(argv) => Some(argv),
            _ => None,
        })
        .collect()
}

fn count(world: &World, f: impl Fn(&Call) -> bool) -> usize {
    world.calls().iter().filter(|c| f(c)).count()
}

fn position(calls: &[Call], f: impl Fn(&Call) -> bool) -> usize {
    calls.iter().position(f).expect("the call was recorded")
}

/// The run directory a recorded installer spawn names (its copy's parent).
fn run_dir_of(argv: &[OsString]) -> PathBuf {
    argv.iter()
        .map(Path::new)
        .find(|p| {
            p.file_name()
                .and_then(|n| n.to_str())
                .is_some_and(|n| n.starts_with("installer-"))
        })
        .and_then(Path::parent)
        .expect("the spawn names a run copy")
        .to_path_buf()
}

/// A script whose child hashes every `installer-*` file it is handed, as the installer would read it.
fn hashing(seen: &Mutex<Vec<[u8; 32]>>) -> Script<'_> {
    Script {
        effect: Some(Box::new(move |argv: &[OsString]| {
            for a in argv {
                let p = Path::new(a);
                let copy = p
                    .file_name()
                    .and_then(|n| n.to_str())
                    .is_some_and(|n| n.starts_with("installer-"));
                if copy {
                    let bytes = fs::read(p).expect("read the run copy");
                    seen.lock()
                        .expect("digest list")
                        .push(digest::sha256(&bytes));
                }
            }
        })),
        ..Script::default()
    }
}

/// A script whose child writes `log` to its `/LOG=` path, as Inno does.
fn writes_log(log: Vec<u8>, exit: i32, never_exits: bool) -> Script<'static> {
    Script {
        exit,
        never_exits,
        effect: Some(Box::new(move |argv: &[OsString]| {
            let path = argv
                .iter()
                .find_map(|a| a.to_str()?.strip_prefix("/LOG=").map(PathBuf::from));
            if let Some(path) = path {
                fs::write(path, &log).expect("write the installer log");
            }
        })),
        ..Script::default()
    }
}

fn info(pkg_id: &str) -> Vec<u8> {
    format!(
        "package-id: {pkg_id}\nversion: 1.0.0\nvolume: /\nlocation: \ninstall-time: 1700000000\n"
    )
    .into_bytes()
}

fn pkgutil_argv(args: &[&str]) -> Vec<OsString> {
    std::iter::once(PKGUTIL)
        .chain(args.iter().copied())
        .map(OsString::from)
        .collect()
}

/// Scripts the three receipt reads of `pkg_id` (stdout per argv).
fn script_receipt(
    runner: &ScriptedRunner<'_>,
    pkg_id: &str,
    info: Vec<u8>,
    files: Vec<u8>,
    dirs: Vec<u8>,
) {
    for (args, stdout) in [
        (vec!["--pkg-info", pkg_id], info),
        (vec!["--files", pkg_id, "--only-files"], files),
        (vec!["--files", pkg_id, "--only-dirs"], dirs),
    ] {
        runner.on(
            Match::Prefix(pkgutil_argv(&args)),
            Script {
                stdout,
                ..Script::default()
            },
        );
    }
}

/// A test classifier (SC-003): exit 99 is `Locked`, everything else the production rule.
fn locked_on_99(code: i32, log: &[String]) -> InnoVerdict {
    if code == 99 {
        InnoVerdict::Locked
    } else {
        classify_inno(code, log)
    }
}

thread_local! {
    /// What `recording_classifier` was handed, per call, on this test's thread (no process-wide lock).
    static SEEN: RefCell<Vec<Vec<String>>> = const { RefCell::new(Vec::new()) };
}

fn recording_classifier(code: i32, log: &[String]) -> InnoVerdict {
    SEEN.with(|s| s.borrow_mut().push(log.to_vec()));
    classify_inno(code, log)
}

const FLAGS4: [&str; 4] = [
    "/VERYSILENT",
    "/SUPPRESSMSGBOXES",
    "/NORESTART",
    "/NOCLOSEAPPLICATIONS",
];

#[test]
fn exact_argv() {
    // Windows: one installer, one uninstaller.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let unins = rig.inno_install(&registry, APP_A, "Alpha");
        vanish_at(&world, &unins, Duration::from_secs(1));
        let digests = Mutex::new(Vec::new());
        let runner = ScriptedRunner::new(&world);
        runner.on(Match::Any, hashing(&digests));
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let (file, sha256) = rig.stage("alpha-setup.exe", b"inno setup bytes");
        let installer = Op::RunInnoInstaller { file, sha256 };
        let entries = batch(
            &rig,
            HelperOs::Windows,
            &[
                installer,
                Op::RunInnoUninstaller {
                    app_id: APP_A.to_owned(),
                },
            ],
            &seams,
        );
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Done, None), (OpOutcome::Done, None)]
        );
        let spawns = spawned(&world);
        assert_eq!(spawns.len(), 2, "{spawns:?}");
        let run = run_dir_of(&spawns[0]);
        assert_eq!(run.parent(), Some(rig.win.run_base.as_path()));
        let name = run
            .file_name()
            .and_then(|n| n.to_str())
            .expect("run dir name");
        let hex = name.strip_prefix("run-").expect("run-<hex>");
        assert!(
            hex.len() == 32
                && hex
                    .bytes()
                    .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b)),
            "{name}"
        );
        let mut want = vec![run.join("installer-0.exe").into_os_string()];
        want.extend(FLAGS4.iter().map(OsString::from));
        let mut log = OsString::from("/LOG=");
        log.push(run.join("inno-0.log"));
        want.push(log);
        assert_eq!(spawns[0], want);
        let mut want = vec![unins.exe.clone().into_os_string()];
        want.extend(strs(&["/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"]));
        assert_eq!(spawns[1], want);
        assert!(world.calls().contains(&Call::UninstallValues(
            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{00000001-0000-4000-8000-000000000001}_is1"
                .to_owned()
        )));
        assert_eq!(*digests.lock().expect("digests"), [sha256]);
        for arg in spawns.iter().flatten() {
            let text = arg.to_str().expect("UTF-8 argv");
            let path = Path::new(text.strip_prefix("/LOG=").unwrap_or(text));
            assert!(!path.starts_with(&rig.staging), "{text} is under staging");
        }
    }
    // macOS: one pkg installer, one pkg uninstall.
    {
        let rig = Rig::new();
        let world = World::new();
        rig.root_own(&world);
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (files, dirs) = rig.receipt(
            &[
                "Library/Audio/Plug-Ins/VST3/Alpha.vst3",
                "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents",
            ],
            &["Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents/Info.plist"],
        );
        let digests = Mutex::new(Vec::new());
        let runner = ScriptedRunner::new(&world);
        for (args, stdout) in [
            (
                vec!["/usr/sbin/pkgutil", "--pkg-info", "com.krateaudio.alpha"],
                info(PKG_A),
            ),
            (
                vec![
                    "/usr/sbin/pkgutil",
                    "--files",
                    "com.krateaudio.alpha",
                    "--only-files",
                ],
                files,
            ),
            (
                vec![
                    "/usr/sbin/pkgutil",
                    "--files",
                    "com.krateaudio.alpha",
                    "--only-dirs",
                ],
                dirs,
            ),
        ] {
            runner.on(
                Match::Prefix(strs(&args)),
                Script {
                    stdout,
                    ..Script::default()
                },
            );
        }
        runner.on(Match::Any, hashing(&digests));
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let (file, sha256) = rig.stage("alpha.pkg", b"pkg bytes");
        let installer = Op::RunPkgInstaller { file, sha256 };
        let entries = batch(
            &rig,
            HelperOs::Macos,
            &[
                installer,
                Op::UninstallPkg {
                    pkg_id: PKG_A.to_owned(),
                },
            ],
            &seams,
        );
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Done, None), (OpOutcome::Done, None)]
        );
        let all = launched(&world);
        let run = run_dir_of(&all[0]);
        assert_eq!(run.parent(), Some(rig.mac.run_base.as_path()));
        let copy = run.join("installer-0.pkg").into_os_string();
        let want = [
            vec![
                OsString::from("/usr/sbin/installer"),
                OsString::from("-pkg"),
                copy,
                OsString::from("-target"),
                OsString::from("/"),
            ],
            strs(&["/usr/sbin/pkgutil", "--pkg-info", "com.krateaudio.alpha"]),
            strs(&[
                "/usr/sbin/pkgutil",
                "--files",
                "com.krateaudio.alpha",
                "--only-files",
            ]),
            strs(&[
                "/usr/sbin/pkgutil",
                "--files",
                "com.krateaudio.alpha",
                "--only-dirs",
            ]),
            strs(&["/usr/sbin/pkgutil", "--forget", "com.krateaudio.alpha"]),
        ];
        assert_eq!(all, want);
        assert_eq!(*digests.lock().expect("digests"), [sha256]);
        for arg in all.iter().flatten() {
            assert!(
                !Path::new(arg).starts_with(&rig.staging),
                "{arg:?} is under staging"
            );
        }
        assert!(
            !rig.on_vol("Library/Audio/Plug-Ins/VST3/Alpha.vst3")
                .exists()
        );
        assert!(rig.mac.vst3.is_dir());
    }
}

#[test]
fn batch_of_three_middle_locked() {
    let rig = Rig::new();
    let world = World::new();
    let io = TempIo::new(&world, &rig.base);
    let registry = FakeRegistry::new(&world);
    let runner = ScriptedRunner::new(&world);
    runner.on(
        Match::Nth(1),
        Script {
            exit: 99,
            ..Script::default()
        },
    );
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: locked_on_99,
    };
    let ops = [
        rig.inno("alpha.exe", b"alpha"),
        rig.inno("beta.exe", b"beta"),
        rig.inno("gamma.exe", b"gamma"),
    ];
    let (exit, result) = run_job(&rig, HelperOs::Windows, &ops, &seams);
    assert_eq!(
        exit,
        Exit {
            status: 0,
            code: None
        }
    );
    let field = |key: &str| -> Vec<Value> {
        result["ops"]
            .as_array()
            .expect("ops")
            .iter()
            .map(|e| e[key].clone())
            .collect()
    };
    assert_eq!(
        field("outcome"),
        [json!("done"), json!("locked"), json!("done")]
    );
    assert_eq!(
        field("code"),
        [Value::Null, json!("E_HELPER_LOCKED"), Value::Null]
    );
    assert_eq!(field("exit_code"), [json!(0), json!(99), json!(0)]);
    let calls = world.calls();
    let spawns: Vec<usize> = (0..calls.len())
        .filter(|&i| matches!(calls[i], Call::Spawn(_)))
        .collect();
    let waits: Vec<usize> = (0..calls.len())
        .filter(|&i| matches!(calls[i], Call::Wait { .. }))
        .collect();
    assert_eq!((spawns.len(), waits.len()), (3, 3), "{calls:?}");
    assert!(
        waits[1] < spawns[2],
        "the third installer starts after the second exited"
    );
    let creates: Vec<usize> = (0..calls.len())
        .filter(|&i| matches!(calls[i], Call::CreateRunDir(_)))
        .collect();
    assert_eq!(creates.len(), 1, "{calls:?}");
    assert!(creates[0] < spawns[0]);
    let Call::Spawn(first) = &calls[spawns[0]] else {
        unreachable!()
    };
    let run = run_dir_of(first);
    let removed: Vec<usize> = (0..calls.len())
        .filter(|&i| matches!(&calls[i], Call::RemoveDir(_)))
        .collect();
    assert_eq!(removed.len(), 1, "{calls:?}");
    assert_eq!(calls[removed[0]], Call::RemoveDir(run.clone()));
    assert!(removed[0] > waits[2]);
    assert!(!run.exists());
}

#[test]
fn batch_timeout_stops_the_rest() {
    let rig = Rig::new();
    let world = World::new();
    let io = TempIo::new(&world, &rig.base);
    let registry = FakeRegistry::new(&world);
    let runner = ScriptedRunner::new(&world);
    runner.on(
        Match::Nth(1),
        Script {
            never_exits: true,
            ..Script::default()
        },
    );
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: classify_inno,
    };
    let ops = [
        rig.inno("alpha.exe", b"alpha"),
        rig.inno("beta.exe", b"beta"),
        rig.inno("gamma.exe", b"gamma"),
    ];
    let (exit, result) = run_job(&rig, HelperOs::Windows, &ops, &seams);
    assert_eq!(
        exit,
        Exit {
            status: 0,
            code: None
        }
    );
    let entries = result["ops"].as_array().expect("ops");
    let pairs: Vec<(Value, Value)> = entries
        .iter()
        .map(|e| (e["outcome"].clone(), e["code"].clone()))
        .collect();
    assert_eq!(
        pairs,
        [
            (json!("done"), Value::Null),
            (json!("failed"), json!("E_HELPER_TIMEOUT")),
            (json!("not_run"), json!("E_HELPER_AFTER_TIMEOUT")),
        ]
    );
    // The fake clock moved by exactly the installer timeout during op 2; nothing else advances it.
    assert_eq!(
        entries[1]["seconds"].as_f64(),
        Some(INNO_INSTALL_TIMEOUT.as_secs_f64())
    );
    let spawns = spawned(&world);
    assert_eq!(spawns.len(), 2, "no installer starts beside a running one");
    assert_eq!(
        count(
            &world,
            |c| matches!(c, Call::Wait { timeout, .. } if *timeout == INNO_INSTALL_TIMEOUT)
        ),
        2
    );
    // No third copy was made; `ProcessRunner` has no kill, and no call but these two waits touched a child.
    assert_eq!(
        count(
            &world,
            |c| matches!(c, Call::CreateNew(p) if p.ends_with("installer-2.exe"))
        ),
        0
    );
}

#[test]
fn uninstall_only_job_has_no_run_dir() {
    let rig = Rig::new();
    let world = World::new();
    let io = TempIo::new(&world, &rig.base);
    let registry = FakeRegistry::new(&world);
    let alpha = rig.inno_install(&registry, APP_A, "Alpha");
    let beta = rig.inno_install(&registry, APP_B, "Beta");
    vanish_at(&world, &alpha, Duration::from_secs(1));
    vanish_at(&world, &beta, Duration::from_secs(3));
    let runner = ScriptedRunner::new(&world);
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: classify_inno,
    };
    let ops = [APP_A, APP_B].map(|id| Op::RunInnoUninstaller {
        app_id: id.to_owned(),
    });
    let entries = batch(&rig, HelperOs::Windows, &ops, &seams);
    assert_eq!(
        summary(&entries),
        [(OpOutcome::Done, None), (OpOutcome::Done, None)]
    );
    assert_eq!(
        count(&world, |c| matches!(
            c,
            Call::CreateRunDir(_) | Call::CreateHelperFolder(_)
        )),
        0
    );
    assert_eq!(
        fs::read_dir(&rig.win.run_base).expect("run base").count(),
        0
    );
}

#[test]
fn run_dir_failure_fails_installers() {
    let rig = Rig::new();
    let world = World::new();
    world.fault(Fault::CreateRunDir(io::ErrorKind::PermissionDenied));
    let io = TempIo::new(&world, &rig.base);
    let registry = FakeRegistry::new(&world);
    let alpha = rig.inno_install(&registry, APP_A, "Alpha");
    vanish_at(&world, &alpha, Duration::from_secs(1));
    let runner = ScriptedRunner::new(&world);
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: classify_inno,
    };
    let ops = [
        rig.inno("alpha.exe", b"alpha"),
        Op::RunInnoUninstaller {
            app_id: APP_A.to_owned(),
        },
        rig.inno("beta.exe", b"beta"),
    ];
    let entries = batch(&rig, HelperOs::Windows, &ops, &seams);
    assert_eq!(
        summary(&entries),
        [
            (OpOutcome::Failed, Some("E_HELPER_RUN_DIR")),
            (OpOutcome::Done, None),
            (OpOutcome::Failed, Some("E_HELPER_RUN_DIR")),
        ]
    );
    let spawns = spawned(&world);
    assert_eq!(spawns.len(), 1, "only the uninstaller ran: {spawns:?}");
    assert_eq!(spawns[0][0], alpha.exe.into_os_string());
    assert_eq!(count(&world, |c| matches!(c, Call::CreateNew(_))), 0);
}

#[test]
fn copy_error_is_named() {
    let rig = Rig::new();
    let world = World::new();
    world.fault(Fault::WriteAfter {
        name: OsString::from("installer-0.exe"),
        bytes: 1000,
        kind: io::ErrorKind::StorageFull,
    });
    let io = TempIo::new(&world, &rig.base);
    let registry = FakeRegistry::new(&world);
    let runner = ScriptedRunner::new(&world);
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: classify_inno,
    };
    let ops = [
        rig.inno("alpha.exe", &vec![7u8; 200 * 1024]),
        rig.inno("beta.exe", b"beta"),
    ];
    let entries = batch(&rig, HelperOs::Windows, &ops, &seams);
    assert_eq!(
        summary(&entries),
        [
            (OpOutcome::Failed, Some("E_HELPER_COPY")),
            (OpOutcome::Done, None)
        ]
    );
    let spawns = spawned(&world);
    assert_eq!(spawns.len(), 1, "{spawns:?}");
    let run = run_dir_of(&spawns[0]);
    assert_eq!(Path::new(&spawns[0][0]), run.join("installer-1.exe"));
    let calls = world.calls();
    let partial = run.join("installer-0.exe");
    let removed = position(&calls, |c| *c == Call::RemoveFile(partial.clone()));
    let spawn = position(&calls, |c| matches!(c, Call::Spawn(_)));
    assert!(
        removed < spawn,
        "the partial copy is removed before the next op"
    );
}

#[test]
fn spawn_error_is_named() {
    let rig = Rig::new();
    let world = World::new();
    let io = TempIo::new(&world, &rig.base);
    let registry = FakeRegistry::new(&world);
    let runner = ScriptedRunner::new(&world);
    runner.on(
        Match::Nth(0),
        Script {
            spawn_error: Some(io::ErrorKind::NotFound),
            ..Script::default()
        },
    );
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: classify_inno,
    };
    let ops = [
        rig.inno("alpha.exe", b"alpha"),
        rig.inno("beta.exe", b"beta"),
    ];
    let entries = batch(&rig, HelperOs::Windows, &ops, &seams);
    assert_eq!(
        summary(&entries),
        [
            (OpOutcome::Failed, Some("E_HELPER_SPAWN")),
            (OpOutcome::Done, None)
        ]
    );
    let last = spawned(&world)
        .pop()
        .expect("the second installer was spawned");
    assert!(Path::new(&last[0]).ends_with("installer-1.exe"));
}

#[test]
fn classify_sees_whole_bounded_log() {
    // A matching line followed by 500 more: the classifier sees every line, the result keeps the last 40.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let mut lines = vec![
            "Log opened.".to_owned(),
            "MATCH: the file is in use".to_owned(),
        ];
        lines.extend((0..500).map(|n| format!("Rollback line {n}")));
        let runner = ScriptedRunner::new(&world);
        runner.on(
            Match::Any,
            writes_log(lines.join("\r\n").into_bytes(), 5, false),
        );
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: recording_classifier,
        };
        SEEN.with(|s| s.borrow_mut().clear());
        let entries = batch(
            &rig,
            HelperOs::Windows,
            &[rig.inno("alpha.exe", b"alpha")],
            &seams,
        );
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Failed, Some("E_HELPER_EXIT"))]
        );
        assert_eq!(entries[0].exit_code, Some(5));
        let seen = SEEN.with(|s| s.borrow().clone());
        assert_eq!(seen.len(), 1);
        let at = seen[0]
            .iter()
            .position(|l| l == "MATCH: the file is in use")
            .expect("the classifier saw the matching line");
        assert_eq!(seen[0][at + 1..], lines[2..]);
        assert_eq!(entries[0].log_tail, lines[lines.len() - 40..]);
    }
    // A 3 MiB log: only its last MiB reaches the classifier.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        // 64-byte records (63 + '\n'), no trailing newline: 3 MiB - 1 bytes.
        let lines: Vec<String> = (0..49_152)
            .map(|n| format!("{n:06} {}", "x".repeat(56)))
            .collect();
        let log = lines.join("\n");
        assert_eq!(log.len(), 3 * 1024 * 1024 - 1);
        let runner = ScriptedRunner::new(&world);
        runner.on(Match::Any, writes_log(log.into_bytes(), 0, false));
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: recording_classifier,
        };
        SEEN.with(|s| s.borrow_mut().clear());
        let entries = batch(
            &rig,
            HelperOs::Windows,
            &[rig.inno("alpha.exe", b"alpha")],
            &seams,
        );
        assert_eq!(summary(&entries), [(OpOutcome::Done, None)]);
        let seen = SEEN.with(|s| s.borrow().clone());
        assert_eq!(seen.len(), 1);
        let handed = seen[0].join("\n").len();
        assert!(
            handed == 1024 * 1024 || handed == 1024 * 1024 - 1,
            "the classifier was handed {handed} bytes"
        );
        assert!(!seen[0].contains(&lines[0]));
        assert!(!seen[0].contains(&lines[32_767]));
        assert!(seen[0].contains(&lines[32_768]));
        assert_eq!(seen[0].last(), lines.last());
    }
}

#[test]
fn uninstall_polls_until_gone() {
    let rig = Rig::new();
    let world = World::new();
    let io = TempIo::new(&world, &rig.base);
    let registry = FakeRegistry::new(&world);
    let alpha = rig.inno_install(&registry, APP_A, "Alpha");
    vanish_at(&world, &alpha, Duration::from_secs(3));
    let runner = ScriptedRunner::new(&world);
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: classify_inno,
    };
    let entries = batch(
        &rig,
        HelperOs::Windows,
        &[Op::RunInnoUninstaller {
            app_id: APP_A.to_owned(),
        }],
        &seams,
    );
    assert_eq!(summary(&entries), [(OpOutcome::Done, None)]);
    assert_eq!(entries[0].seconds, 3.0);
    assert_eq!(count(&world, |c| matches!(c, Call::Sleep(_))), 6);
    assert_eq!(
        count(&world, |c| *c == Call::Sleep(UNINSTALL_POLL_INTERVAL)),
        6
    );
    assert!(!alpha.install_dir.join("Alpha.vst3").exists());
}

#[test]
fn uninstall_poll_times_out() {
    // The uninstaller exits 0 but nothing ever disappears: 241 checks, 240 sleeps, 120 s on the fake clock.
    {
        let wall = Instant::now();
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let alpha = rig.inno_install(&registry, APP_A, "Alpha");
        let runner = ScriptedRunner::new(&world);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(
            &rig,
            HelperOs::Windows,
            &[Op::RunInnoUninstaller {
                app_id: APP_A.to_owned(),
            }],
            &seams,
        );
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Failed, Some("E_HELPER_UNINSTALL_TIMEOUT"))]
        );
        assert_eq!(entries[0].seconds, UNINSTALL_POLL_TIMEOUT.as_secs_f64());
        assert_eq!(
            count(&world, |c| *c == Call::Sleep(UNINSTALL_POLL_INTERVAL)),
            240
        );
        assert_eq!(count(&world, |c| matches!(c, Call::Sleep(_))), 240);
        assert_eq!(
            count(&world, |c| *c == Call::KeyExists(alpha.subkey.clone())),
            241
        );
        assert!(
            wall.elapsed() < Duration::from_secs(5),
            "{:?}",
            wall.elapsed()
        );
    }
    // A non-zero exit: `failed E_HELPER_EXIT`, no poll.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let alpha = rig.inno_install(&registry, APP_A, "Alpha");
        let runner = ScriptedRunner::new(&world);
        runner.on(
            Match::Any,
            Script {
                exit: 3,
                ..Script::default()
            },
        );
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(
            &rig,
            HelperOs::Windows,
            &[Op::RunInnoUninstaller {
                app_id: APP_A.to_owned(),
            }],
            &seams,
        );
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Failed, Some("E_HELPER_EXIT"))]
        );
        assert_eq!(entries[0].exit_code, Some(3));
        assert_eq!(count(&world, |c| matches!(c, Call::Sleep(_))), 0);
        assert_eq!(
            count(&world, |c| *c == Call::KeyExists(alpha.subkey.clone())),
            0
        );
    }
}

#[test]
fn uninstaller_never_exits() {
    let rig = Rig::new();
    let world = World::new();
    let io = TempIo::new(&world, &rig.base);
    let registry = FakeRegistry::new(&world);
    let alpha = rig.inno_install(&registry, APP_A, "Alpha");
    let beta = rig.inno_install(&registry, APP_B, "Beta");
    // Op 2 starts at 120 s (op 1's whole wait); its install vanishes a second later.
    vanish_at(&world, &beta, Duration::from_secs(121));
    let runner = ScriptedRunner::new(&world);
    runner.on(
        Match::Nth(0),
        Script {
            never_exits: true,
            ..Script::default()
        },
    );
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: classify_inno,
    };
    let ops = [APP_A, APP_B].map(|id| Op::RunInnoUninstaller {
        app_id: id.to_owned(),
    });
    let entries = batch(&rig, HelperOs::Windows, &ops, &seams);
    assert_eq!(
        summary(&entries),
        [
            (OpOutcome::Failed, Some("E_HELPER_UNINSTALL_TIMEOUT")),
            (OpOutcome::Done, None),
        ]
    );
    assert_eq!(entries[0].seconds, UNINSTALL_POLL_TIMEOUT.as_secs_f64());
    assert_eq!(
        count(
            &world,
            |c| matches!(c, Call::Wait { timeout, .. } if *timeout == UNINSTALL_POLL_TIMEOUT)
        ),
        2
    );
    assert_eq!(
        count(&world, |c| *c == Call::KeyExists(alpha.subkey.clone())),
        0,
        "no poll follows an uninstaller that never exited"
    );
    assert_eq!(spawned(&world).len(), 2);
}

#[test]
fn runs_the_copy_never_the_original() {
    // Rewritten right after the copy: the spawn names the run copy, whose hash is the job's.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (file, sha256) = rig.stage("alpha.exe", b"the verified setup");
        world.hook(Hook::RewriteAfterCopy {
            src: PathBuf::from(&file),
            bytes: b"a tampered setup".to_vec(),
        });
        let digests = Mutex::new(Vec::new());
        let runner = ScriptedRunner::new(&world);
        runner.on(Match::Any, hashing(&digests));
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(
            &rig,
            HelperOs::Windows,
            &[Op::RunInnoInstaller {
                file: file.clone(),
                sha256,
            }],
            &seams,
        );
        assert_eq!(summary(&entries), [(OpOutcome::Done, None)]);
        let spawns = spawned(&world);
        assert_eq!(spawns.len(), 1);
        assert_eq!(
            Path::new(&spawns[0][0]),
            run_dir_of(&spawns[0]).join("installer-0.exe")
        );
        assert_eq!(*digests.lock().expect("digests"), [sha256]);
        assert_eq!(
            fs::read(&file).expect("the original"),
            b"a tampered setup",
            "the hook rewrote the original"
        );
    }
    // Rewritten during the copy: the copy's digest differs, `E_HELPER_HASH`, nothing spawned, the copy removed.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (file, sha256) = rig.stage("alpha.exe", &vec![1u8; 200 * 1024]);
        world.hook(Hook::RewriteDuringCopy {
            src: PathBuf::from(&file),
            bytes: vec![2u8; 200 * 1024],
        });
        let runner = ScriptedRunner::new(&world);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(
            &rig,
            HelperOs::Windows,
            &[Op::RunInnoInstaller { file, sha256 }],
            &seams,
        );
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Failed, Some("E_HELPER_HASH"))]
        );
        assert_eq!(spawned(&world).len(), 0);
        assert!(
            count(
                &world,
                |c| matches!(c, Call::RemoveFile(p) if p.ends_with("installer-0.exe"))
            ) >= 1
        );
    }
    // Replaced by a link (to identical bytes) before the open: the no-follow open refuses it.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (file, sha256) = rig.stage("alpha.exe", b"the verified setup");
        let (target, _) = rig.stage("twin.exe", b"the verified setup");
        world.hook(Hook::LinkBeforeOpen {
            src: PathBuf::from(&file),
            target: PathBuf::from(target),
        });
        let runner = ScriptedRunner::new(&world);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(
            &rig,
            HelperOs::Windows,
            &[Op::RunInnoInstaller { file, sha256 }],
            &seams,
        );
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Refused, Some("E_HELPER_SOURCE"))]
        );
        assert_eq!(spawned(&world).len(), 0);
    }
    // Unix: a FIFO swapped in before the open neither blocks nor runs.
    #[cfg(unix)]
    {
        let wall = Instant::now();
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (file, sha256) = rig.stage("alpha.pkg", b"pkg bytes");
        rig.root_own(&world);
        world.hook(Hook::FifoBeforeOpen {
            src: PathBuf::from(&file),
        });
        let runner = ScriptedRunner::new(&world);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(
            &rig,
            HelperOs::Macos,
            &[Op::RunPkgInstaller { file, sha256 }],
            &seams,
        );
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Refused, Some("E_HELPER_SOURCE"))]
        );
        assert_eq!(spawned(&world).len(), 0);
        assert!(
            wall.elapsed() < Duration::from_secs(1),
            "{:?}",
            wall.elapsed()
        );
    }
}

/// SC-006's receipt for `--install-location /`.
const PKG_DIRS: [&str; 14] = [
    "Library",
    "Library/Audio",
    "Library/Audio/Plug-Ins",
    "Library/Audio/Plug-Ins/VST3",
    "Library/Audio/Plug-Ins/VST3/Alpha.vst3",
    "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents",
    "Library/Audio/Plug-Ins/Components",
    "Library/Audio/Plug-Ins/Components/Alpha.component",
    "Library/Application Support",
    "Library/Application Support/Krate Audio",
    "Library/Application Support/Krate Audio/Alpha",
    "Applications",
    "Applications/Alpha AUv3.app",
    "Applications/Alpha AUv3.app/Contents",
];
const PKG_FILES: [&str; 4] = [
    "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents/Info.plist",
    "Library/Audio/Plug-Ins/Components/Alpha.component/Info.plist",
    "Library/Application Support/Krate Audio/Alpha/Init.vstpreset",
    "Applications/Alpha AUv3.app/Contents/Info.plist",
];

fn uninstall_a() -> [Op; 1] {
    [Op::UninstallPkg {
        pkg_id: PKG_A.to_owned(),
    }]
}

#[test]
fn pkg_paths_classified() {
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (files, dirs) = rig.receipt(&PKG_DIRS, &PKG_FILES);
        let user = rig.on_vol("Library/Application Support/Krate Audio/Alpha/Mine.vstpreset");
        fs::write(&user, b"the user's own preset").expect("plant a user preset");
        let runner = ScriptedRunner::new(&world);
        script_receipt(&runner, PKG_A, info(PKG_A), files, dirs);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(&rig, HelperOs::Macos, &uninstall_a(), &seams);
        assert_eq!(summary(&entries), [(OpOutcome::Done, None)]);
        for f in PKG_FILES {
            assert!(!rig.on_vol(f).exists(), "{f} removed");
        }
        for d in [
            "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents",
            "Library/Audio/Plug-Ins/VST3/Alpha.vst3",
            "Library/Audio/Plug-Ins/Components/Alpha.component",
            "Applications/Alpha AUv3.app/Contents",
            "Applications/Alpha AUv3.app",
        ] {
            assert!(!rig.on_vol(d).exists(), "{d} removed");
        }
        for d in [
            "Library",
            "Library/Audio",
            "Library/Audio/Plug-Ins",
            "Library/Audio/Plug-Ins/VST3",
            "Library/Audio/Plug-Ins/Components",
            "Library/Application Support",
            "Library/Application Support/Krate Audio",
            "Applications",
        ] {
            assert!(rig.on_vol(d).is_dir(), "{d} is structural and kept");
        }
        assert!(
            user.is_file(),
            "the non-empty preset folder keeps the user's file"
        );
        let calls = world.calls();
        let at = |rel: &str| position(&calls, |c| *c == Call::RemoveDir(rig.on_vol(rel)));
        assert!(
            at("Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents")
                < at("Library/Audio/Plug-Ins/VST3/Alpha.vst3")
        );
        assert!(at("Applications/Alpha AUv3.app/Contents") < at("Applications/Alpha AUv3.app"));
        let forget = pkgutil_argv(&["--forget", PKG_A]);
        let all = launched(&world);
        assert_eq!(all.iter().filter(|a| **a == forget).count(), 1);
        assert_eq!(all.last(), Some(&forget), "--forget comes last");
    }
    // A directory outside every rule refuses the whole op; nothing is removed.
    for stray in ["Applications/Other.app", "Applications/alpha AUv3.app"] {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let mut dirs = PKG_DIRS.to_vec();
        dirs.push(stray);
        let (files, dirs) = rig.receipt(&dirs, &PKG_FILES);
        let runner = ScriptedRunner::new(&world);
        script_receipt(&runner, PKG_A, info(PKG_A), files, dirs);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(&rig, HelperOs::Macos, &uninstall_a(), &seams);
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Refused, Some("E_HELPER_PKG_PATH"))],
            "{stray}"
        );
        for f in PKG_FILES {
            assert!(rig.on_vol(f).is_file(), "{stray}: {f} kept");
        }
        assert_eq!(
            count(&world, |c| matches!(
                c,
                Call::RemoveFile(_) | Call::RemoveDir(_)
            )),
            0,
            "{stray}"
        );
        assert!(!launched(&world).contains(&pkgutil_argv(&["--forget", PKG_A])));
    }
}

#[test]
fn pkg_failure_paths() {
    let small_dirs = [
        "Library/Audio/Plug-Ins/VST3/Alpha.vst3",
        "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents",
    ];
    let small_files = [
        "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents/Info.plist",
        "Library/Audio/Plug-Ins/VST3/Alpha.vst3/Contents/PkgInfo",
    ];
    let forget = pkgutil_argv(&["--forget", PKG_A]);
    // No receipt: no `--files` read.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let runner = ScriptedRunner::new(&world);
        runner.on(
            Match::Prefix(pkgutil_argv(&["--pkg-info", PKG_A])),
            Script {
                exit: 1,
                ..Script::default()
            },
        );
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(&rig, HelperOs::Macos, &uninstall_a(), &seams);
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Failed, Some("E_HELPER_PKG_RECEIPT"))]
        );
        assert_eq!(launched(&world), [pkgutil_argv(&["--pkg-info", PKG_A])]);
    }
    // The receipt names another package: nothing read further, nothing removed.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (files, dirs) = rig.receipt(&small_dirs, &small_files);
        let runner = ScriptedRunner::new(&world);
        script_receipt(&runner, PKG_A, info("com.krateaudio.beta"), files, dirs);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(&rig, HelperOs::Macos, &uninstall_a(), &seams);
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Failed, Some("E_HELPER_PKG_RECEIPT"))]
        );
        assert_eq!(launched(&world), [pkgutil_argv(&["--pkg-info", PKG_A])]);
        for f in small_files {
            assert!(rig.on_vol(f).is_file(), "{f} kept");
        }
    }
    // One file cannot be removed: `E_HELPER_REMOVE`, the rest removed, no `--forget`; the retry forgets once.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (files, dirs) = rig.receipt(&small_dirs, &small_files);
        let stuck = rig.on_vol(small_files[1]);
        world.fault(Fault::RemoveFile(stuck.clone()));
        let runner = ScriptedRunner::new(&world);
        script_receipt(&runner, PKG_A, info(PKG_A), files, dirs);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(&rig, HelperOs::Macos, &uninstall_a(), &seams);
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Failed, Some("E_HELPER_REMOVE"))]
        );
        assert!(!rig.on_vol(small_files[0]).exists());
        assert!(stuck.is_file());
        assert!(!launched(&world).contains(&forget));
        world.clear_faults();
        let entries = batch(&rig, HelperOs::Macos, &uninstall_a(), &seams);
        assert_eq!(summary(&entries), [(OpOutcome::Done, None)]);
        assert!(!stuck.exists());
        assert_eq!(launched(&world).iter().filter(|a| **a == forget).count(), 1);
    }
    // `--forget` exits non-zero.
    {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (files, dirs) = rig.receipt(&small_dirs, &small_files);
        let runner = ScriptedRunner::new(&world);
        script_receipt(&runner, PKG_A, info(PKG_A), files, dirs);
        runner.on(
            Match::Prefix(forget.clone()),
            Script {
                exit: 1,
                ..Script::default()
            },
        );
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(&rig, HelperOs::Macos, &uninstall_a(), &seams);
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Failed, Some("E_HELPER_FORGET"))]
        );
        for f in small_files {
            assert!(!rig.on_vol(f).exists(), "{f} removed");
        }
    }
    // A `pkgutil` that cannot be started is `E_HELPER_SPAWN` (plan SI-12), at each of its four runs; the rule goes
    // before the receipt's own, so it wins. Nothing is removed before `--forget`; the forget leg has removed the files.
    for (args, removed) in [
        (vec!["--pkg-info", PKG_A], false),
        (vec!["--files", PKG_A, "--only-files"], false),
        (vec!["--files", PKG_A, "--only-dirs"], false),
        (vec!["--forget", PKG_A], true),
    ] {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let (files, dirs) = rig.receipt(&small_dirs, &small_files);
        let runner = ScriptedRunner::new(&world);
        runner.on(
            Match::Prefix(pkgutil_argv(&args)),
            Script {
                spawn_error: Some(io::ErrorKind::NotFound),
                ..Script::default()
            },
        );
        script_receipt(&runner, PKG_A, info(PKG_A), files, dirs);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(&rig, HelperOs::Macos, &uninstall_a(), &seams);
        assert_eq!(
            summary(&entries),
            [(OpOutcome::Failed, Some("E_HELPER_SPAWN"))],
            "{args:?}"
        );
        assert_eq!(launched(&world).last(), Some(&pkgutil_argv(&args)));
        for f in small_files {
            assert_eq!(!rig.on_vol(f).exists(), removed, "{args:?}: {f}");
        }
    }
}

#[test]
fn run_dir_removed_after_job() {
    let both: &[&str] = &["installer-0.exe", "inno-0.log"];
    let rows: [(&str, Script<'static>, OpOutcome, &[&str]); 3] = [
        (
            "done",
            writes_log(b"Installed.".to_vec(), 0, false),
            OpOutcome::Done,
            both,
        ),
        (
            "failed",
            writes_log(b"Failed.".to_vec(), 1, false),
            OpOutcome::Failed,
            both,
        ),
        // The still-running installer may not have written its log yet; its copy is removed either way.
        (
            "timed out",
            Script {
                never_exits: true,
                ..Script::default()
            },
            OpOutcome::Failed,
            &["installer-0.exe"],
        ),
    ];
    for (name, script, outcome, files) in rows {
        let rig = Rig::new();
        let world = World::new();
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let runner = ScriptedRunner::new(&world);
        runner.on(Match::Any, script);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let entries = batch(
            &rig,
            HelperOs::Windows,
            &[rig.inno("alpha.exe", b"alpha")],
            &seams,
        );
        assert_eq!(entries[0].outcome, outcome, "{name}");
        let calls = world.calls();
        let Some(Call::Spawn(argv)) = calls.iter().find(|c| matches!(c, Call::Spawn(_))) else {
            panic!("{name}: no spawn");
        };
        let run = run_dir_of(argv);
        let dir = position(&calls, |c| *c == Call::RemoveDir(run.clone()));
        for file in files {
            let at = position(&calls, |c| *c == Call::RemoveFile(run.join(file)));
            assert!(at < dir, "{name}: {file} removed before the directory");
        }
        assert!(!run.exists(), "{name}: the run directory is gone");
    }
}

/// Builds one SC-027 defect into the macOS run-directory chain; the rest is root-owned.
fn chain_defect(rig: &Rig, world: &World, defect: &str) {
    let library = &rig.mac.run_ancestors[0];
    let support = &rig.mac.run_ancestors[1];
    match defect {
        "uid 501" => {
            world.owner(library, 501, 0o755);
            world.root_owned(support);
            world.root_owned(&rig.mac.run_base);
        }
        "group-writable" | "world-writable" => {
            world.root_owned(library);
            let mode = if defect == "group-writable" {
                0o775
            } else {
                0o757
            };
            world.owner(support, 0, mode);
            world.root_owned(&rig.mac.run_base);
        }
        "symlink" => {
            world.root_owned(library);
            world.root_owned(support);
            let real = support.join("elsewhere");
            mkdirs(&real);
            world.root_owned(&real);
            fs::remove_dir(&rig.mac.run_base).expect("remove the helper folder");
            #[cfg(unix)]
            std::os::unix::fs::symlink(&real, &rig.mac.run_base)
                .expect("symbolic-link privilege not held: enable Developer Mode");
            #[cfg(windows)]
            std::os::windows::fs::symlink_dir(&real, &rig.mac.run_base)
                .expect("symbolic-link privilege not held: enable Developer Mode");
        }
        _ => {
            rig.root_own(world);
            world.fault(Fault::CreateRunDir(io::ErrorKind::AlreadyExists));
        }
    }
}

#[test]
fn run_dir_refusals_spawn_nothing() {
    let rows = [
        (HelperOs::Macos, "uid 501"),
        (HelperOs::Macos, "group-writable"),
        (HelperOs::Macos, "world-writable"),
        (HelperOs::Macos, "symlink"),
        (HelperOs::Macos, "existing name"),
        (HelperOs::Windows, "existing name"),
    ];
    for (os, defect) in rows {
        let rig = Rig::new();
        let world = World::new();
        if os == HelperOs::Macos {
            chain_defect(&rig, &world, defect);
        } else {
            world.fault(Fault::CreateRunDir(io::ErrorKind::AlreadyExists));
        }
        let io = TempIo::new(&world, &rig.base);
        let registry = FakeRegistry::new(&world);
        let runner = ScriptedRunner::new(&world);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: classify_inno,
        };
        let ops = match os {
            HelperOs::Windows => [
                rig.inno("alpha.exe", b"alpha"),
                rig.inno("beta.exe", b"beta"),
            ],
            _ => [rig.pkg("alpha.pkg", b"alpha"), rig.pkg("beta.pkg", b"beta")],
        };
        let entries = batch(&rig, os, &ops, &seams);
        assert_eq!(
            summary(&entries),
            [
                (OpOutcome::Failed, Some("E_HELPER_RUN_DIR")),
                (OpOutcome::Failed, Some("E_HELPER_RUN_DIR")),
            ],
            "{os:?} {defect}"
        );
        assert_eq!(spawned(&world).len(), 0, "{os:?} {defect}: zero spawns");
        assert_eq!(
            count(&world, |c| matches!(c, Call::CreateNew(_))),
            0,
            "{os:?} {defect}: zero copies"
        );
    }
}

#[test]
#[should_panic(expected = "outside the test sandbox")]
fn temp_io_refuses_outside_base() {
    let a = TempDir::new().expect("temp dir A");
    let b = TempDir::new().expect("temp dir B");
    let world = World::new();
    let io = TempIo::new(&world, a.path());
    let _ = io.create_run_dir(&b.path().join("run-0123456789abcdef0123456789abcdef"));
}

fn panic_text(payload: &(dyn Any + Send)) -> String {
    payload
        .downcast_ref::<&str>()
        .map(|s| (*s).to_owned())
        .or_else(|| payload.downcast_ref::<String>().cloned())
        .unwrap_or_default()
}

/// `temp_io_refuses_outside_base`'s companion rows: every other mutating method and every source read outside the
/// base panics too, and the other temp dir is untouched.
#[test]
fn temp_io_refuses_every_other_method_outside_base() {
    let a = TempDir::new().expect("temp dir A");
    let b = TempDir::new().expect("temp dir B");
    let victim = b.path().join("victim.bin");
    fs::write(&victim, b"keep").expect("write the victim");
    let sub = b.path().join("sub");
    mkdirs(&sub);
    let new_file = b.path().join("new.bin");
    let helper = b.path().join("helper");
    type Row<'a> = (&'static str, Box<dyn Fn(&dyn HelperIo) + 'a>);
    let rows: [Row<'_>; 7] = [
        (
            "create_new",
            Box::new(|io| {
                let _ = io.create_new(&new_file);
            }),
        ),
        (
            "create_helper_folder",
            Box::new(|io| {
                let _ = io.create_helper_folder(&helper);
            }),
        ),
        (
            "remove_file",
            Box::new(|io| {
                let _ = io.remove_file(&victim);
            }),
        ),
        (
            "remove_dir",
            Box::new(|io| {
                let _ = io.remove_dir(&sub);
            }),
        ),
        (
            "open_source",
            Box::new(|io| {
                let _ = io.open_source(&victim);
            }),
        ),
        (
            "read_bounded",
            Box::new(|io| {
                let _ = io.read_bounded(&victim, 16);
            }),
        ),
        (
            "read_tail",
            Box::new(|io| {
                let _ = io.read_tail(&victim, 16);
            }),
        ),
    ];
    for (name, call) in rows {
        let world = World::new();
        let io = TempIo::new(&world, a.path());
        let payload = catch_unwind(AssertUnwindSafe(|| call(&io)))
            .expect_err(&format!("{name} outside the base must panic"));
        assert!(
            panic_text(&*payload).contains("outside the test sandbox"),
            "{name}: {:?}",
            panic_text(&*payload)
        );
    }
    assert_eq!(fs::read(&victim).expect("the victim"), b"keep");
    assert!(sub.is_dir());
    let mut left: Vec<OsString> = fs::read_dir(b.path())
        .expect("list B")
        .map(|e| e.expect("entry").file_name())
        .collect();
    left.sort();
    assert_eq!(left, strs(&["sub", "victim.bin"]));
}
