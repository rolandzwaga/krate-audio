//! SC-011 (D1, PD-11): jobs the core writes parse under the helper's schema for each op's own OS, their hashes agree,
//! a result the helper writes maps through the core's FR-076 reader, one changed byte either way fails, and the core's
//! copies of the helper's codes, timeouts and state-dir name equal the helper's.
//!
//! The fakes (`seams::fake`) exist only with the helper's `test-support` feature (PD-8), which `cargo test --workspace`
//! turns on through core's dev-dependency; a `-p krate-manager-helper` run needs `--features test-support`, else this
//! file compiles to no test.
#![cfg(feature = "test-support")]

use krate_manager_core::install::FsInstallIo;
use krate_manager_core::install::job::{
    self as core_job, JobOp, OpReport, ResultError, WrittenJob,
};
use krate_manager_core::platform::APP_IDENTIFIER;
use krate_manager_helper::classify::classify_inno;
use krate_manager_helper::cli::Args;
use krate_manager_helper::result::{self, OpEntry, OpOutcome, ResultFile};
use krate_manager_helper::roots::{
    AllowedRoots, MacosRoots, OsRoots, STATE_DIR_NAME, StateDirRule, WindowsRoots,
};
use krate_manager_helper::seams::fake::{
    FakeRegistry, Match, Script, ScriptedRunner, TempIo, World,
};
use krate_manager_helper::seams::{Seams, WriteSync};
use krate_manager_helper::{Exit, HelperOs, digest, error, ops, run};
use std::ffi::OsString;
use std::io;
use std::path::{Path, PathBuf};

/// An in-memory `WriteSync` for `result::write`.
#[derive(Default)]
struct Sink(Vec<u8>);

impl io::Write for Sink {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.0.extend_from_slice(buf);
        Ok(buf.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl WriteSync for Sink {
    fn sync_all(&mut self) -> io::Result<()> {
        Ok(())
    }
}

/// The temp layout: `<base>/<APP_IDENTIFIER>/{jobs,staging}` and every root under `<base>`.
struct Layout {
    _td: tempfile::TempDir,
    base: PathBuf,
    state: PathBuf,
    staging: PathBuf,
}

fn mkdirs(dir: &Path) {
    std::fs::create_dir_all(dir).unwrap_or_else(|e| panic!("cannot create {}: {e}", dir.display()));
}

fn layout() -> Layout {
    let td = tempfile::TempDir::new().expect("cannot create a TempDir");
    // Canonical from the start: the helper requires canonical job and source paths (FR-020, FR-027).
    let base = std::fs::canonicalize(td.path()).expect("the TempDir canonicalizes");
    let state = base.join(APP_IDENTIFIER);
    let staging = state.join("staging");
    mkdirs(&state.join(core_job::JOBS_DIR));
    mkdirs(&staging.join("downloads"));
    for dir in ["vst3_vendor", "preset_vendor", "run_base"] {
        mkdirs(&base.join("win").join(dir));
    }
    for dir in ["vst3", "components", "presets", "applications", "volume"] {
        mkdirs(&base.join("mac").join(dir));
    }
    mkdirs(&base.join("mac_root"));
    Layout {
        _td: td,
        base,
        state,
        staging,
    }
}

/// The roots for `os`, every one under the temp base; the state dir is the layout's, exactly.
fn roots_for(l: &Layout, os: HelperOs) -> AllowedRoots {
    let win = l.base.join("win");
    let mac = l.base.join("mac");
    let os_roots = match os {
        HelperOs::Windows => OsRoots::Windows(WindowsRoots {
            vst3_vendor: win.join("vst3_vendor"),
            preset_vendor: win.join("preset_vendor"),
            run_base: win.join("run_base"),
        }),
        HelperOs::Macos => OsRoots::Macos(MacosRoots {
            vst3: mac.join("vst3"),
            components: mac.join("components"),
            presets: mac.join("presets"),
            applications: mac.join("applications"),
            volume: mac.join("volume"),
            run_base: l.base.join("mac_root").join("helper"),
            run_ancestors: vec![l.base.join("mac_root")],
        }),
        HelperOs::Linux => OsRoots::None,
    };
    AllowedRoots {
        os: os_roots,
        state_dir: StateDirRule::Exact(l.state.clone()),
    }
}

fn args_for(written: &WrittenJob) -> Args {
    Args {
        job: written.path.clone(),
        job_sha256: digest::parse_hex64(&written.sha256)
            .expect("the core's hash is 64 lower-case hex"),
        #[cfg(feature = "ci-temp-roots")]
        ci_roots: None,
    }
}

/// A file under staging, as a real download leaves it (Phase 7 layout).
fn staged(l: &Layout, name: &str, bytes: &[u8]) -> (String, String) {
    let path = l.staging.join("downloads").join(name);
    std::fs::write(&path, bytes).unwrap_or_else(|e| panic!("cannot write {}: {e}", path.display()));
    let file = path.to_str().expect("a UTF-8 temp path").to_owned();
    (file, digest::hex(&digest::sha256(bytes)))
}

/// The first `needle` in `bytes`.
fn find(bytes: &[u8], needle: &[u8]) -> usize {
    bytes
        .windows(needle.len())
        .position(|w| w == needle)
        .expect("the needle is present")
}

#[test]
fn core_written_jobs_parse() {
    let l = layout();
    let world = World::new();
    // D7's macOS chain: the run directory's ancestors are root-owned (PD-5), on every leg.
    world.root_owned(&l.base.join("mac_root"));
    let io = TempIo::new(&world, &l.base);
    let runner = ScriptedRunner::new(&world);
    // No receipt: `pkgutil --pkg-info` exits 1 (FR-033).
    runner.on(
        Match::Prefix(vec![
            OsString::from(ops::PKGUTIL),
            OsString::from("--pkg-info"),
        ]),
        Script {
            exit: 1,
            ..Script::default()
        },
    );
    // Every other child exits 0 at once; an Inno setup leaves its `/LOG=` file, as a real one does.
    runner.on(
        Match::Any,
        Script {
            effect: Some(Box::new(|argv: &[OsString]| {
                for arg in argv {
                    if let Some(log) = arg.to_str().and_then(|a| a.strip_prefix("/LOG=")) {
                        std::fs::write(log, b"Log opened.\r\nInstallation process succeeded.\r\n")
                            .unwrap_or_else(|e| panic!("cannot write the fake log {log}: {e}"));
                    }
                }
            })),
            ..Script::default()
        },
    );
    let registry = FakeRegistry::new(&world);
    let seams = Seams {
        io: &io,
        runner: &runner,
        registry: &registry,
        classify: classify_inno,
    };

    let (exe, exe_sha) = staged(&l, "a.exe", b"MZ fixture inno setup");
    let (pkg, pkg_sha) = staged(&l, "a.pkg", b"xar! fixture pkg");
    // Each of the four kinds, with its own OS (D18: both values in the one test, on every leg), and the outcome the
    // helper's validation and the fakes lead to: the installers run their copies, the uninstalls pass their id checks
    // and meet no key / no receipt.
    type Expect = fn(&OpReport) -> bool;
    let cases: [(JobOp, HelperOs, Expect); 4] = [
        (
            JobOp::RunInnoInstaller {
                file: exe.clone(),
                sha256: exe_sha.clone(),
            },
            HelperOs::Windows,
            |r| *r == OpReport::Done,
        ),
        (
            JobOp::RunInnoUninstaller {
                app_id: "{0A1B2C3D-0000-4000-8000-00000000A1FA}".to_owned(),
            },
            HelperOs::Windows,
            |r| {
                matches!(
                    r,
                    OpReport::Failed {
                        code: "E_HELPER_NO_KEY",
                        ..
                    }
                )
            },
        ),
        (
            JobOp::RunPkgInstaller {
                file: pkg.clone(),
                sha256: pkg_sha.clone(),
            },
            HelperOs::Macos,
            |r| *r == OpReport::Done,
        ),
        (
            JobOp::UninstallPkg {
                pkg_id: "com.krateaudio.alpha".to_owned(),
            },
            HelperOs::Macos,
            |r| {
                matches!(
                    r,
                    OpReport::Failed {
                        code: "E_HELPER_PKG_RECEIPT",
                        ..
                    }
                )
            },
        ),
    ];

    let mut good_result: Option<(WrittenJob, Vec<u8>)> = None;
    for (op, os, expect) in cases {
        let written = core_job::write(&FsInstallIo, &l.state, vec![op.clone()])
            .unwrap_or_else(|e| panic!("the core writes {op:?}: {e}"));
        assert_eq!(written.ops, vec![op.clone()]);
        // The core's hash is the hash of the bytes on disk.
        let job_bytes = std::fs::read(&written.path).expect("the job is on disk");
        assert_eq!(
            written.sha256,
            digest::hex(&digest::sha256(&job_bytes)),
            "{op:?}"
        );

        let exit = run(&args_for(&written), os, &roots_for(&l, os), &seams);
        assert_eq!(
            exit,
            Exit {
                status: 0,
                code: None
            },
            "{op:?} on {os:?}"
        );
        // The result lies where the core looks for it, and binds to the core's hash.
        let bytes = std::fs::read(&written.result)
            .unwrap_or_else(|e| panic!("no result at {}: {e}", written.result.display()));
        let value: serde_json::Value = serde_json::from_slice(&bytes).expect("the result is JSON");
        assert_eq!(
            value["job_error"],
            serde_json::Value::Null,
            "{op:?}: {value}"
        );
        assert_eq!(value["job_sha256"], written.sha256.as_str(), "{op:?}");
        let reports = core_job::read_result(&bytes, &written).unwrap_or_else(|e| {
            panic!("the core reads the helper's result for {op:?}: {e:?} {value}")
        });
        assert_eq!(reports.len(), 1, "{op:?}");
        assert!(expect(&reports[0]), "{op:?} on {os:?}: {reports:?}");
        good_result.get_or_insert((written, bytes));
    }

    // One changed byte in the job: the hash binding refuses it (exit 3) and the core reads the refusal.
    let written = core_job::write(
        &FsInstallIo,
        &l.state,
        vec![JobOp::UninstallPkg {
            pkg_id: "com.krateaudio.alpha".to_owned(),
        }],
    )
    .expect("the core writes the job");
    let mut job_bytes = std::fs::read(&written.path).expect("the job is on disk");
    let at = find(&job_bytes, b"alpha");
    job_bytes[at] = b'b';
    std::fs::write(&written.path, &job_bytes).expect("the job is rewritten");
    let exit = run(
        &args_for(&written),
        HelperOs::Macos,
        &roots_for(&l, HelperOs::Macos),
        &seams,
    );
    assert_eq!(
        exit,
        Exit {
            status: 3,
            code: Some("E_HELPER_JOB_HASH")
        }
    );
    let bytes = std::fs::read(&written.result).expect("an exit-3 result is written");
    let value: serde_json::Value = serde_json::from_slice(&bytes).expect("the result is JSON");
    assert_eq!(value["job_error"], "E_HELPER_JOB_HASH");
    assert_eq!(value["ops"], serde_json::json!([]));
    assert_eq!(
        core_job::read_result(&bytes, &written),
        Err(ResultError::Helper("E_HELPER_JOB_HASH"))
    );

    // One changed byte in a good result: the core refuses it.
    let (written, mut bytes) = good_result.expect("a good result was read");
    let at = find(&bytes, written.sha256.as_bytes());
    bytes[at] = if bytes[at] == b'0' { b'1' } else { b'0' };
    assert_eq!(
        core_job::read_result(&bytes, &written),
        Err(ResultError::NoResult)
    );

    // `result::write`'s output maps through the core's reader, every outcome kind once.
    let written = core_job::write(
        &FsInstallIo,
        &l.state,
        vec![
            JobOp::RunInnoInstaller {
                file: exe.clone(),
                sha256: exe_sha.clone(),
            },
            JobOp::RunInnoInstaller {
                file: exe,
                sha256: exe_sha,
            },
            JobOp::RunPkgInstaller {
                file: pkg,
                sha256: pkg_sha,
            },
            JobOp::RunInnoUninstaller {
                app_id: "{0A1B2C3D-0000-4000-8000-00000000A1FA}".to_owned(),
            },
            JobOp::UninstallPkg {
                pkg_id: "com.krateaudio.alpha".to_owned(),
            },
        ],
    )
    .expect("the core writes the job");
    let entry = |index: u32, op: &'static str, outcome, code, exit_code| OpEntry {
        index,
        op,
        outcome,
        code,
        exit_code,
        seconds: 1.25,
        log_tail: vec!["a line".to_owned()],
    };
    let file = ResultFile {
        result_version: 1,
        job_sha256: written.sha256.clone(),
        job_error: None,
        ops: vec![
            entry(0, "run_inno_installer", OpOutcome::Done, None, Some(0)),
            entry(
                1,
                "run_inno_installer",
                OpOutcome::Locked,
                Some(error::LOCKED.code),
                Some(5),
            ),
            entry(
                2,
                "run_pkg_installer",
                OpOutcome::Failed,
                Some(error::EXIT.code),
                Some(1),
            ),
            entry(
                3,
                "run_inno_uninstaller",
                OpOutcome::Refused,
                Some(error::APP_ID.code),
                None,
            ),
            entry(
                4,
                "uninstall_pkg",
                OpOutcome::NotRun,
                Some(error::AFTER_TIMEOUT.code),
                None,
            ),
        ],
    };
    let mut sink = Sink::default();
    result::write(&mut sink, &file).expect("the result serializes");
    assert_eq!(
        core_job::read_result(&sink.0, &written),
        Ok(vec![
            OpReport::Done,
            OpReport::Locked,
            OpReport::Failed {
                code: "E_HELPER_EXIT",
                exit_code: Some(1)
            },
            OpReport::Refused {
                code: "E_HELPER_APP_ID"
            },
            OpReport::NotRun {
                code: "E_HELPER_AFTER_TIMEOUT"
            },
        ])
    );
    // The same file with one byte changed in its hash is refused.
    let mut changed = sink.0.clone();
    let at = find(&changed, written.sha256.as_bytes());
    changed[at] = if changed[at] == b'0' { b'1' } else { b'0' };
    assert_eq!(
        core_job::read_result(&changed, &written),
        Err(ResultError::NoResult)
    );

    // PD-11: the core's copies of the code catalogue, the timeouts and the state-dir name.
    for code in error::CODES {
        assert_eq!(core_job::helper_code(code), Some(code), "{code}");
    }
    assert_eq!(core_job::helper_code("E_HELPER_NOT_A_CODE"), None);
    assert_eq!(core_job::INNO_INSTALL_TIMEOUT, ops::INNO_INSTALL_TIMEOUT);
    assert_eq!(core_job::PKG_INSTALL_TIMEOUT, ops::PKG_INSTALL_TIMEOUT);
    assert_eq!(
        core_job::UNINSTALL_POLL_TIMEOUT,
        ops::UNINSTALL_POLL_TIMEOUT
    );
    assert_eq!(
        core_job::UNINSTALL_POLL_INTERVAL,
        ops::UNINSTALL_POLL_INTERVAL
    );
    assert_eq!(core_job::PKGUTIL_TIMEOUT, ops::PKGUTIL_TIMEOUT);
    assert_eq!(STATE_DIR_NAME, APP_IDENTIFIER);
}
