//! `cli::tests` (owner T004; plan §7.1): FR-005's argument rules, PD-25's malformed hash, SC-019's `--ci-roots` rows.

use super::*;
use crate::classify::InnoVerdict;
use crate::digest;
use crate::seams::fake::{FakeRegistry, ScriptedRunner, TempIo, World};
use std::cell::RefCell;
use std::fs;
use std::path::Path;

thread_local! {
    /// What the recording `RunFn` was handed, per test thread (no shared lock, Rule 6).
    static SEEN: RefCell<Vec<(Args, HelperOs, AllowedRoots)>> = const { RefCell::new(Vec::new()) };
}

fn recording(args: &Args, os: HelperOs, roots: &AllowedRoots, _seams: &Seams<'_>) -> Exit {
    SEEN.with(|seen| seen.borrow_mut().push((args.clone(), os, roots.clone())));
    RECORDED
}

/// What the recording `RunFn` returns.
const RECORDED: Exit = Exit {
    status: 0,
    code: None,
};

/// FR-005, D15: a usage error.
const USAGE_EXIT: Exit = Exit {
    status: 2,
    code: None,
};

fn take_seen() -> Vec<(Args, HelperOs, AllowedRoots)> {
    SEEN.with(|seen| seen.borrow_mut().drain(..).collect())
}

fn never_classified(_exit_code: i32, _log: &[String]) -> InnoVerdict {
    InnoVerdict::Failed
}

/// `main_with` over the fake seams sandboxed to `base`, with the recording `RunFn`.
fn drive(base: &Path, args: &[OsString]) -> Exit {
    let world = World::new();
    let io = TempIo::new(&world, base);
    let runner = ScriptedRunner::new(&world);
    let registry = FakeRegistry::new(&world);
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: never_classified,
    };
    main_with(args.to_vec(), &seams, recording)
}

fn argv(parts: &[&str]) -> Vec<OsString> {
    parts.iter().map(OsString::from).collect()
}

const HEX: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

fn entries(dir: &Path) -> Vec<OsString> {
    fs::read_dir(dir)
        .expect("the temp dir lists")
        .map(|e| e.expect("an entry").file_name())
        .collect()
}

#[test]
fn parse_args_accepts_exactly_the_two_flags() {
    let digest = digest::parse_hex64(HEX).expect("the fixture hash parses");
    for parts in [
        argv(&["--job", "j.json", "--job-sha256", HEX]),
        argv(&["--job-sha256", HEX, "--job", "j.json"]),
    ] {
        let args = parse_args(&parts).expect("either order parses (FR-005)");
        assert_eq!(args.job, PathBuf::from("j.json"));
        assert_eq!(args.job_sha256, digest);
    }
    let refused: [&[&str]; 9] = [
        &[],
        &["--anything"],
        &["--job", "j.json"],
        &["--job-sha256", HEX],
        &["--job", "a", "--job", "b", "--job-sha256", HEX],
        &["--job", "a", "--job-sha256", HEX, "--job-sha256", HEX],
        &["--job", "a", "--job-sha256", HEX, "extra"],
        &["--job", "a", "--job-sha256"],
        &["--job=a", "--job-sha256", HEX],
    ];
    for parts in refused {
        assert!(
            parse_args(&argv(parts)).is_none(),
            "{parts:?} is a usage error"
        );
    }
}

#[cfg(not(feature = "ci-temp-roots"))]
#[test]
fn ci_roots_flag_is_usage_error() {
    let tmp = tempfile::TempDir::new().expect("a temp dir");
    let cases: [&[&str]; 3] = [
        &["--ci-roots", "x"],
        &["--job", "j.json", "--job-sha256", HEX, "--ci-roots", "x"],
        &["--ci-roots", "x", "--job-sha256", HEX, "--job", "j.json"],
    ];
    for parts in cases {
        assert!(
            parse_args(&argv(parts)).is_none(),
            "{parts:?} parses as None"
        );
        assert_eq!(drive(tmp.path(), &argv(parts)), USAGE_EXIT, "{parts:?}");
    }
    assert!(take_seen().is_empty(), "the RunFn is never called");
    assert!(entries(tmp.path()).is_empty(), "nothing written");
}

#[test]
fn malformed_hash_is_usage_error() {
    let tmp = tempfile::TempDir::new().expect("a temp dir");
    let job = tmp.path().join("job.json");
    let job = job.to_str().expect("a UTF-8 temp path");
    let upper = HEX.to_ascii_uppercase();
    for hash in ["xyz", &HEX[..63], upper.as_str()] {
        let parts = argv(&["--job", job, "--job-sha256", hash]);
        assert!(parse_args(&parts).is_none(), "{hash:?} is not 64 lower hex");
        assert_eq!(drive(tmp.path(), &parts), USAGE_EXIT, "{hash:?}");
    }
    assert!(take_seen().is_empty(), "the RunFn is never called");
    assert!(entries(tmp.path()).is_empty(), "nothing written");
}

#[cfg(feature = "ci-temp-roots")]
#[test]
fn ci_roots_file_is_read() {
    use crate::error;
    use crate::roots::{OsRoots, STATE_DIR_NAME, StateDirRule};
    use std::path::MAIN_SEPARATOR;

    let tmp = tempfile::TempDir::new().expect("a temp dir");
    let real = tmp.path().join("real");
    let outside = tmp.path().join("outside");
    fs::create_dir_all(&real).expect("real/");
    fs::create_dir_all(&outside).expect("outside/");
    let state_dir = real.join(STATE_DIR_NAME);
    let job = state_dir
        .join("jobs")
        .join("job-0123456789abcdef0123456789abcdef.json");
    let file = real.join("ci-roots.json");
    let production = AllowedRoots::production().expect("the production roots resolve");
    let s = |p: &Path| p.to_str().expect("a UTF-8 temp path").to_owned();

    // The file's keys per OS (FR-052) and the roots they must yield: the plug-in, preset and application roots and
    // the state dir from the file; `run_base`, `run_ancestors` and `volume` stay production (PD-5).
    let (keys, expected_os): (Vec<(&str, PathBuf)>, OsRoots) = match &production.os {
        OsRoots::Windows(w) => {
            let keys = vec![
                ("vst3_vendor", real.join("vst3").join("Krate Audio")),
                ("preset_vendor", real.join("presets")),
            ];
            let mut w = w.clone();
            w.vst3_vendor = keys[0].1.clone();
            w.preset_vendor = keys[1].1.clone();
            (keys, OsRoots::Windows(w))
        }
        OsRoots::Macos(m) => {
            let keys = vec![
                ("vst3", real.join("VST3")),
                ("components", real.join("Components")),
                ("presets", real.join("presets")),
                ("applications", real.join("Applications")),
            ];
            let mut m = m.clone();
            m.vst3 = keys[0].1.clone();
            m.components = keys[1].1.clone();
            m.presets = keys[2].1.clone();
            m.applications = keys[3].1.clone();
            (keys, OsRoots::Macos(m))
        }
        OsRoots::None => (Vec::new(), OsRoots::None),
    };
    let write = |pairs: &[(&str, String)]| {
        let mut object = serde_json::Map::new();
        for (k, v) in pairs {
            object.insert((*k).to_owned(), serde_json::Value::String(v.clone()));
        }
        fs::write(&file, serde_json::to_vec(&object).expect("JSON")).expect("ci-roots.json");
    };
    let mut good: Vec<(&str, String)> = vec![("state_dir", s(&state_dir))];
    good.extend(keys.iter().map(|(k, p)| (*k, s(p))));
    let parts = argv(&[
        "--ci-roots",
        &s(&file),
        "--job",
        &s(&job),
        "--job-sha256",
        HEX,
    ]);

    write(&good);
    assert_eq!(drive(tmp.path(), &parts), RECORDED);
    let seen = take_seen();
    assert_eq!(seen.len(), 1, "the RunFn is called once");
    let (args, helper_os, roots) = &seen[0];
    assert_eq!(args.ci_roots.as_deref(), Some(file.as_path()));
    assert_eq!(args.job, job);
    assert_eq!(*helper_os, HelperOs::current());
    assert_eq!(
        *roots,
        AllowedRoots {
            os: expected_os,
            state_dir: StateDirRule::Exact(state_dir.clone()),
        }
    );

    // A repeated `--ci-roots` is a usage error (FR-005).
    let mut repeated = parts.clone();
    repeated.extend(argv(&["--ci-roots", &s(&file)]));
    assert_eq!(drive(tmp.path(), &repeated), USAGE_EXIT);

    // Refused, exit 4 `E_HELPER_ROOTS`, the RunFn never called: a root (or the state dir) outside the file's
    // directory, a `..` escape, a relative path, a missing key, a key `--ci-roots` may not set (PD-5), not JSON.
    let roots_exit = Exit {
        status: 4,
        code: Some(error::ROOTS.code),
    };
    let mut bad: Vec<Vec<(&str, String)>> = Vec::new();
    for i in 0..good.len() {
        let mut out_of_bounds = good.clone();
        out_of_bounds[i].1 = s(&outside.join("x"));
        bad.push(out_of_bounds);
        let mut escape = good.clone();
        escape[i].1 = format!(
            "{}{MAIN_SEPARATOR}..{MAIN_SEPARATOR}..{MAIN_SEPARATOR}outside",
            good[i].1
        );
        bad.push(escape);
        let mut relative = good.clone();
        relative[i].1 = "relative".to_owned();
        bad.push(relative);
        let mut missing = good.clone();
        missing.remove(i);
        bad.push(missing);
    }
    let mut extra = good.clone();
    extra.push(("run_base", s(&real.join("run"))));
    bad.push(extra);
    for pairs in &bad {
        write(pairs);
        assert_eq!(drive(tmp.path(), &parts), roots_exit, "{pairs:?}");
    }
    fs::write(&file, b"not json").expect("ci-roots.json");
    assert_eq!(drive(tmp.path(), &parts), roots_exit, "not JSON");
    assert!(take_seen().is_empty(), "no refused file reaches the RunFn");
}
