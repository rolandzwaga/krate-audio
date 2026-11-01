//! `seams::tests` (owner T007; plan §7.1).

use super::fake::{Call, Event, FakeRegistry, Fault, Match, Script, ScriptedRunner, TempIo, World};
use super::{HelperIo, ProcessRunner, Registry};
use crate::ops::{INNO_KEY_SUFFIX, UNINSTALL_KEY};
use crate::validate::{Kind, LstatFacts};
use std::ffi::OsString;
use std::fs;
use std::io::{ErrorKind, Write};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::{Path, PathBuf};
use std::time::Duration;

fn argv(parts: &[&str]) -> Vec<OsString> {
    parts.iter().map(OsString::from).collect()
}

fn secs(s: u64) -> Duration {
    Duration::from_secs(s)
}

fn temp() -> tempfile::TempDir {
    tempfile::tempdir().expect("temp dir")
}

fn fixture_key(app_id: &str) -> String {
    format!("{UNINSTALL_KEY}\\{app_id}{INNO_KEY_SUFFIX}")
}

/// The panic text of `f`, `None` when it returns.
fn panic_text(f: impl FnOnce()) -> Option<String> {
    let payload = catch_unwind(AssertUnwindSafe(f)).err()?;
    let text = payload
        .downcast_ref::<String>()
        .cloned()
        .or_else(|| payload.downcast_ref::<&str>().map(|s| (*s).to_owned()));
    Some(text.unwrap_or_default())
}

fn assert_sandbox_panic(what: &str, f: impl FnOnce()) {
    let text = panic_text(f).unwrap_or_else(|| panic!("{what} did not panic"));
    assert!(
        text.contains("outside the test sandbox"),
        "{what} panicked with {text:?}"
    );
}

fn create_kind(io: &TempIo<'_>, path: &Path) -> Option<ErrorKind> {
    io.create_new(path).err().map(|e| e.kind())
}

fn dir(kind: Kind, uid: u32, mode: u32) -> Option<LstatFacts> {
    Some(LstatFacts {
        kind,
        uid: Some(uid),
        mode: Some(mode),
    })
}

/// The clock advances by `sleep`, by `wait`'s scripted `takes` (split over several waits) and by the full timeout of a
/// child that never exits; `output` follows the same rules.
fn clock_cases() {
    let world = World::new();
    let base = temp();
    let io = TempIo::new(&world, base.path());
    let runner = ScriptedRunner::new(&world);
    assert_eq!(world.now(), Duration::ZERO);
    io.sleep(secs(2));
    assert_eq!(io.now(), secs(2));
    assert_eq!(world.now(), secs(2));
    runner.on(
        Match::Prefix(argv(&["slow"])),
        Script {
            exit: 3,
            takes: secs(3),
            ..Script::default()
        },
    );
    runner.on(
        Match::Prefix(argv(&["hang"])),
        Script {
            never_exits: true,
            ..Script::default()
        },
    );
    let slow = runner.spawn(&argv(&["slow", "x"])).expect("spawn slow");
    assert_eq!(world.now(), secs(2), "spawn takes no time");
    assert_eq!(runner.wait(slow, secs(1)).expect("wait"), None);
    assert_eq!(world.now(), secs(3));
    assert_eq!(runner.wait(slow, secs(10)).expect("wait"), Some(3));
    assert_eq!(world.now(), secs(5), "only the remaining 2 s of `takes`");
    assert_eq!(runner.wait(slow, secs(10)).expect("wait"), Some(3));
    assert_eq!(world.now(), secs(5), "an exited child takes no time");
    let hang = runner.spawn(&argv(&["hang"])).expect("spawn hang");
    assert_eq!(runner.wait(hang, secs(7)).expect("wait"), None);
    assert_eq!(world.now(), secs(12));
    world.advance(secs(1));
    assert_eq!(world.now(), secs(13));
    assert_eq!(
        runner.output(&argv(&["slow"]), secs(10)).expect("output"),
        Some((3, Vec::new()))
    );
    assert_eq!(world.now(), secs(16));
    assert_eq!(
        runner.output(&argv(&["slow"]), secs(1)).expect("output"),
        None
    );
    assert_eq!(world.now(), secs(17));
    assert_eq!(
        runner.output(&argv(&["hang"]), secs(2)).expect("output"),
        None
    );
    assert_eq!(world.now(), secs(19));
}

/// `Event::RemovePath` removes the real path (a file, a directory tree) once the clock reaches its time, not before;
/// outside every sandbox base it panics and removes nothing.
fn event_cases() {
    let world = World::new();
    let base = temp();
    let io = TempIo::new(&world, base.path());
    let file = base.path().join("unins000.exe");
    let tree = base.path().join("Alpha.vst3");
    fs::write(&file, b"x").expect("write");
    fs::create_dir_all(tree.join("Contents")).expect("mkdir");
    fs::write(tree.join("Contents").join("f"), b"y").expect("write");
    world.at(secs(3), Event::RemovePath(file.clone()));
    world.at(secs(3), Event::RemovePath(tree.clone()));
    world.advance(secs(2));
    assert!(file.exists() && tree.exists(), "not before 3 s");
    world.advance(Duration::from_millis(999));
    assert!(file.exists() && tree.exists(), "not at 2.999 s");
    world.advance(Duration::from_millis(2));
    assert!(!file.exists(), "removed after 3 s");
    assert!(!tree.exists(), "the tree removed after 3 s");
    let late = base.path().join("late");
    fs::write(&late, b"z").expect("write");
    world.at(secs(5), Event::RemovePath(late.clone()));
    io.sleep(secs(1));
    assert!(late.exists());
    io.sleep(secs(1));
    assert!(!late.exists(), "sleep fires events too");

    let other = temp();
    let keep = other.path().join("keep");
    fs::write(&keep, b"k").expect("write");
    world.at(world.now(), Event::RemovePath(keep.clone()));
    assert_sandbox_panic("Event::RemovePath outside the base", || {
        world.advance(Duration::ZERO)
    });
    assert!(keep.exists(), "nothing removed outside the sandbox");
    world.advance(secs(1));
    assert!(keep.exists(), "a fired event does not fire again");
}

/// `FakeRegistry` reads the world's map; `Event::RemoveKey` empties the entry.
fn registry_cases() {
    let world = World::new();
    let reg = FakeRegistry::new(&world);
    let key = fixture_key("{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}");
    let bare = fixture_key("{11111111-2222-3333-4444-555555555555}");
    assert_eq!(reg.uninstall_values(&key), None);
    assert!(!reg.key_exists(&key));
    reg.set(&key, Some("unins000.exe"), Some("install"));
    reg.set(&bare, None, None);
    assert_eq!(
        reg.uninstall_values(&key),
        Some((Some("unins000.exe".to_owned()), Some("install".to_owned())))
    );
    assert_eq!(reg.uninstall_values(&bare), Some((None, None)));
    assert!(reg.key_exists(&key) && reg.key_exists(&bare));
    world.at(secs(1), Event::RemoveKey(key.clone()));
    world.advance(Duration::from_millis(500));
    assert!(reg.key_exists(&key), "not before 1 s");
    world.advance(Duration::from_millis(600));
    assert!(!reg.key_exists(&key));
    assert_eq!(reg.uninstall_values(&key), None);
    assert!(reg.key_exists(&bare), "only its own key");
}

/// `Match::Nth` counts `spawn` and `output` together; the first matching rule wins; no rule exits 0; a spawn error;
/// the effect runs, with the argv, when the child exits.
fn runner_cases() {
    let world = World::new();
    let base = temp();
    let runner = ScriptedRunner::new(&world);
    runner.on(
        Match::Nth(1),
        Script {
            exit: 9,
            stdout: b"second".to_vec(),
            ..Script::default()
        },
    );
    runner.on(
        Match::Any,
        Script {
            exit: 4,
            ..Script::default()
        },
    );
    let first = runner.spawn(&argv(&["p"])).expect("spawn");
    assert_eq!(runner.wait(first, secs(1)).expect("wait"), Some(4));
    assert_eq!(
        runner.output(&argv(&["q"]), secs(1)).expect("output"),
        Some((9, b"second".to_vec()))
    );
    let third = runner.spawn(&argv(&["p"])).expect("spawn");
    assert_eq!(runner.wait(third, secs(1)).expect("wait"), Some(4));

    let made = base.path().join("made");
    let seen = made.clone();
    let runner = ScriptedRunner::new(&world);
    runner.on(
        Match::Prefix(argv(&["tool", "--a"])),
        Script {
            exit: 5,
            ..Script::default()
        },
    );
    runner.on(
        Match::Prefix(argv(&["missing"])),
        Script {
            spawn_error: Some(ErrorKind::NotFound),
            ..Script::default()
        },
    );
    runner.on(
        Match::Prefix(argv(&["make"])),
        Script {
            takes: secs(1),
            effect: Some(Box::new(move |args: &[OsString]| {
                fs::write(&made, args.len().to_string()).expect("effect write");
            })),
            ..Script::default()
        },
    );
    let a = runner.spawn(&argv(&["tool", "--a", "x"])).expect("spawn");
    assert_eq!(runner.wait(a, secs(1)).expect("wait"), Some(5));
    let b = runner.spawn(&argv(&["tool", "--b"])).expect("spawn");
    assert_eq!(runner.wait(b, secs(1)).expect("wait"), Some(0), "no rule");
    assert_eq!(
        runner.spawn(&argv(&["missing"])).err().map(|e| e.kind()),
        Some(ErrorKind::NotFound)
    );
    assert_eq!(
        runner
            .output(&argv(&["missing"]), secs(1))
            .err()
            .map(|e| e.kind()),
        Some(ErrorKind::NotFound)
    );
    let m = runner.spawn(&argv(&["make", "it"])).expect("spawn");
    assert!(!seen.exists(), "no effect before the exit");
    assert_eq!(
        runner.wait(m, Duration::from_millis(500)).expect("wait"),
        None
    );
    assert!(!seen.exists(), "no effect while running");
    assert_eq!(runner.wait(m, secs(1)).expect("wait"), Some(0));
    assert_eq!(fs::read_to_string(&seen).expect("effect ran"), "2");
}

/// `Fault::CreateNew` hits only its file name; `WriteAfter` fails after exactly N bytes; `RemoveFile`, `CreateRunDir`;
/// `clear_faults`.
fn fault_cases() {
    let world = World::new();
    let base = temp();
    let io = TempIo::new(&world, base.path());
    let sub = base.path().join("sub");
    fs::create_dir(&sub).expect("mkdir");
    world.fault(Fault::CreateNew {
        name: "r.json".into(),
        kind: ErrorKind::PermissionDenied,
    });
    assert_eq!(
        create_kind(&io, &base.path().join("r.json")),
        Some(ErrorKind::PermissionDenied)
    );
    assert_eq!(
        create_kind(&io, &sub.join("r.json")),
        Some(ErrorKind::PermissionDenied)
    );
    assert!(!base.path().join("r.json").exists());
    assert_eq!(create_kind(&io, &base.path().join("other.json")), None);
    assert_eq!(
        create_kind(&io, &base.path().join("other.json")),
        Some(ErrorKind::AlreadyExists),
        "create_new fails on an existing name"
    );

    world.fault(Fault::WriteAfter {
        name: "w.bin".into(),
        bytes: 5,
        kind: ErrorKind::StorageFull,
    });
    {
        let mut w = io.create_new(&base.path().join("w.bin")).expect("create");
        let e = w.write_all(b"0123456789").expect_err("a write fault");
        assert_eq!(e.kind(), ErrorKind::StorageFull);
    }
    assert_eq!(fs::read(base.path().join("w.bin")).expect("read"), b"01234");
    {
        let mut w = io.create_new(&sub.join("w.bin")).expect("create");
        w.write_all(b"abcde").expect("exactly N bytes fit");
        w.sync_all().expect("sync");
        let e = w.write_all(b"f").expect_err("byte N + 1 fails");
        assert_eq!(e.kind(), ErrorKind::StorageFull);
    }
    {
        let mut w = io
            .create_new(&base.path().join("free.bin"))
            .expect("create");
        w.write_all(&[7u8; 64]).expect("no fault on another name");
    }

    let doomed = base.path().join("doomed");
    fs::write(&doomed, b"d").expect("write");
    world.fault(Fault::RemoveFile(doomed.clone()));
    assert!(io.remove_file(&doomed).is_err());
    assert!(doomed.exists());
    world.fault(Fault::CreateRunDir(ErrorKind::PermissionDenied));
    assert_eq!(
        io.create_run_dir(&base.path().join("run-f"))
            .err()
            .map(|e| e.kind()),
        Some(ErrorKind::PermissionDenied)
    );
    assert!(!base.path().join("run-f").exists());

    world.clear_faults();
    assert_eq!(create_kind(&io, &base.path().join("r.json")), None);
    io.remove_file(&doomed)
        .expect("no fault after clear_faults");
    io.create_run_dir(&base.path().join("run-f"))
        .expect("no fault after clear_faults");
}

/// `root_owned(d)`: `d` and every directory `TempIo` creates under it report uid 0, `0o755`, a run directory `0o700`,
/// by the given or the canonical spelling; `owner` overrides apply; the real kind; an absent path is `None`.
fn owner_cases() {
    let world = World::new();
    let base = temp();
    let io = TempIo::new(&world, base.path());
    let d = base.path().join("lib");
    fs::create_dir(&d).expect("mkdir");
    world.root_owned(&d);
    assert_eq!(io.lstat(&d).expect("lstat"), dir(Kind::Dir, 0, 0o755));
    let canonical_d = io.canonicalize(&d).expect("canonicalize");
    assert_eq!(
        io.lstat(&canonical_d).expect("lstat"),
        dir(Kind::Dir, 0, 0o755)
    );
    let helper = d.join("helper");
    io.create_helper_folder(&helper).expect("helper folder");
    io.create_helper_folder(&helper)
        .expect("helper folder when present");
    assert_eq!(io.lstat(&helper).expect("lstat"), dir(Kind::Dir, 0, 0o755));
    let run = helper.join("run-0001");
    io.create_run_dir(&run).expect("run dir");
    assert_eq!(io.lstat(&run).expect("lstat"), dir(Kind::Dir, 0, 0o700));
    assert!(
        io.create_run_dir(&run).is_err(),
        "fails on an existing name"
    );
    let canonical_run = canonical_d.join("run-0002");
    io.create_run_dir(&canonical_run)
        .expect("run dir, canonical");
    assert_eq!(
        io.lstat(&d.join("run-0002")).expect("lstat"),
        dir(Kind::Dir, 0, 0o700)
    );

    let f = base.path().join("f.txt");
    fs::write(&f, b"f").expect("write");
    world.owner(&f, 501, 0o644);
    assert_eq!(io.lstat(&f).expect("lstat"), dir(Kind::File, 501, 0o644));
    world.owner(&d, 0, 0o775);
    assert_eq!(io.lstat(&d).expect("lstat"), dir(Kind::Dir, 0, 0o775));
    assert_eq!(io.lstat(&base.path().join("absent")).expect("lstat"), None);
    let plain = io.lstat(base.path()).expect("lstat").expect("present");
    assert_eq!(plain.kind, Kind::Dir);
    #[cfg(windows)]
    assert_eq!((plain.uid, plain.mode), (None, None), "no uid on Windows");
    #[cfg(unix)]
    assert!(plain.uid.is_some() && plain.mode.is_some());
}

/// Every mutating call and every open outside `base` (or on `base` itself, or through `..`) panics; `canonicalize`,
/// `lstat` and `list_dir` do not.
fn sandbox_cases() {
    let world = World::new();
    let base = temp();
    let io = TempIo::new(&world, base.path());
    let other = temp();
    let outside = other.path().join("x");
    fs::write(&outside, b"keep").expect("write");
    let o = other.path();
    assert_sandbox_panic("create_new", || {
        let _ = io.create_new(&o.join("new"));
    });
    assert_sandbox_panic("create_helper_folder", || {
        let _ = io.create_helper_folder(&o.join("h"));
    });
    assert_sandbox_panic("create_run_dir", || {
        let _ = io.create_run_dir(&o.join("run-x"));
    });
    assert_sandbox_panic("remove_file", || {
        let _ = io.remove_file(&outside);
    });
    assert_sandbox_panic("remove_dir", || {
        let _ = io.remove_dir(o);
    });
    assert_sandbox_panic("remove_dir of base", || {
        let _ = io.remove_dir(base.path());
    });
    assert_sandbox_panic("open_source", || {
        let _ = io.open_source(&outside);
    });
    assert_sandbox_panic("read_bounded", || {
        let _ = io.read_bounded(&outside, 10);
    });
    assert_sandbox_panic("read_tail", || {
        let _ = io.read_tail(&outside, 10);
    });
    let escape: PathBuf = base.path().join("..").join("escape");
    assert_sandbox_panic("create_new through ..", || {
        let _ = io.create_new(&escape);
    });
    assert!(outside.exists() && !o.join("new").exists() && !o.join("h").exists());
    assert!(!o.join("run-x").exists() && !escape.exists());

    assert!(io.canonicalize(o).is_ok());
    assert_eq!(
        io.lstat(&outside).expect("lstat").map(|f| f.kind),
        Some(Kind::File)
    );
    fs::write(o.join("a"), b"a").expect("write");
    assert_eq!(
        io.list_dir(o).expect("list_dir"),
        vec![OsString::from("a"), OsString::from("x")]
    );
}

/// `calls()` records in order; `spawns()` and `removals()` filter it; `random_hex` is counter-based.
fn record_cases() {
    let world = World::new();
    let base = temp();
    let io = TempIo::new(&world, base.path());
    let runner = ScriptedRunner::new(&world);
    let reg = FakeRegistry::new(&world);
    let file = base.path().join("c.json");
    let run = base.path().join("run-1");
    let folder = base.path().join("hf");
    io.sleep(secs(1));
    drop(io.create_new(&file).expect("create"));
    let child = runner.spawn(&argv(&["a"])).expect("spawn");
    runner.wait(child, secs(2)).expect("wait");
    runner.output(&argv(&["b"]), secs(3)).expect("output");
    io.create_run_dir(&run).expect("run dir");
    io.create_helper_folder(&folder).expect("folder");
    io.remove_file(&file).expect("remove");
    io.remove_dir(&run).expect("remove dir");
    reg.uninstall_values("k");
    reg.key_exists("k");
    assert_eq!(
        world.calls(),
        vec![
            Call::Sleep(secs(1)),
            Call::CreateNew(file.clone()),
            Call::Spawn(argv(&["a"])),
            Call::Wait {
                child,
                timeout: secs(2)
            },
            Call::Output(argv(&["b"])),
            Call::CreateRunDir(run.clone()),
            Call::CreateHelperFolder(folder),
            Call::RemoveFile(file.clone()),
            Call::RemoveDir(run.clone()),
            Call::UninstallValues("k".to_owned()),
            Call::KeyExists("k".to_owned()),
        ]
    );
    assert_eq!(world.spawns(), vec![argv(&["a"])]);
    assert_eq!(world.removals(), vec![file, run]);

    let first = io.random_hex();
    let second = io.random_hex();
    for h in [&first, &second] {
        assert_eq!(h.len(), 32);
        assert!(
            h.chars()
                .all(|c| c.is_ascii_digit() || ('a'..='f').contains(&c))
        );
    }
    assert_ne!(first, second);
    let again = World::new();
    assert_eq!(TempIo::new(&again, base.path()).random_hex(), first);
}

/// The core's loopback holds the fakes behind `Elevation: Sync` (plan §4.1.9).
fn assert_sync<T: Sync>() {}

#[test]
fn fake_world_cases() {
    assert_sync::<World>();
    assert_sync::<TempIo<'_>>();
    assert_sync::<ScriptedRunner<'_>>();
    assert_sync::<FakeRegistry<'_>>();
    clock_cases();
    event_cases();
    registry_cases();
    runner_cases();
    fault_cases();
    owner_cases();
    sandbox_cases();
    record_cases();
}
