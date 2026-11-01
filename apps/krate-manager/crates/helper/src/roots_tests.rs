//! `roots::tests` (owner T004; plan §7.1): SC-007's roots rows and FR-026's canonical form.

use super::*;
use crate::classify::InnoVerdict;
use crate::cli::{self, Args};
use crate::error;
use crate::seams::Seams;
use crate::seams::fake::{FakeRegistry, ScriptedRunner, TempIo, World};
use crate::{Exit, HelperOs, digest};
use std::cell::RefCell;
use std::ffi::OsString;
use std::fs;

thread_local! {
    /// The roots the recording `RunFn` was handed, per test thread (no shared lock).
    static SEEN: RefCell<Vec<AllowedRoots>> = const { RefCell::new(Vec::new()) };
}

fn recording(_args: &Args, _os: HelperOs, roots: &AllowedRoots, _seams: &Seams<'_>) -> Exit {
    SEEN.with(|seen| seen.borrow_mut().push(roots.clone()));
    Exit {
        status: 0,
        code: None,
    }
}

fn never_classified(_exit_code: i32, _log: &[String]) -> InnoVerdict {
    InnoVerdict::Failed
}

/// SC-007: a job and the manager's settings file beside it, both naming an extra root in every key FR-001 and the
/// settings know; the roots `main_with` hands its `RunFn` are still exactly `AllowedRoots::production()`.
#[test]
fn binary_roots_ignore_job_and_settings() {
    let tmp = tempfile::TempDir::new().expect("a temp dir");
    let state = tmp.path().join(STATE_DIR_NAME);
    let jobs = state.join("jobs");
    fs::create_dir_all(&jobs).expect("jobs/");
    let decoy = tmp.path().join("decoy");
    fs::create_dir_all(&decoy).expect("decoy/");
    let job = jobs.join("job-0123456789abcdef0123456789abcdef.json");
    let settings = state.join("settings.json");
    let production = AllowedRoots::production().expect("the production roots resolve");
    let decoy = decoy.to_str().expect("a UTF-8 temp path").to_owned();

    for extra in [r"C:\Windows", "/etc", decoy.as_str()] {
        let ops = serde_json::json!([
            { "op": "run_inno_installer", "file": extra, "sha256": extra },
            { "op": "run_inno_uninstaller", "app_id": extra },
            { "op": "run_pkg_installer", "file": extra, "sha256": extra },
            { "op": "uninstall_pkg", "pkg_id": extra },
        ]);
        let bytes = serde_json::to_vec(&serde_json::json!({ "job_version": 1, "ops": ops }))
            .expect("job JSON");
        fs::write(&job, &bytes).expect("the job fixture");
        let settings_bytes = serde_json::to_vec(&serde_json::json!({
            "schema_version": 1,
            "linux_plugin_root": extra,
            "linux_graphics_workaround": false,
        }))
        .expect("settings JSON");
        fs::write(&settings, settings_bytes).expect("the settings fixture");

        let world = World::new();
        let io = TempIo::new(&world, tmp.path());
        let runner = ScriptedRunner::new(&world);
        let registry = FakeRegistry::new(&world);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: never_classified,
        };
        let args: Vec<OsString> = vec![
            "--job".into(),
            job.clone().into_os_string(),
            "--job-sha256".into(),
            digest::hex(&digest::sha256(&bytes)).into(),
        ];
        let exit = cli::main_with(args, &seams, recording);
        assert_eq!(
            exit,
            Exit {
                status: 0,
                code: None
            },
            "{extra}"
        );
        let seen: Vec<AllowedRoots> = SEEN.with(|seen| seen.borrow_mut().drain(..).collect());
        assert_eq!(seen, vec![production.clone()], "{extra}");
    }
}

#[cfg(windows)]
#[test]
fn production_roots_windows() {
    use crate::os::{KnownFolder, known_folder};
    let folder = |id| known_folder(id).expect("the known folder resolves");
    let roots = AllowedRoots::production().expect("the production roots resolve");
    let vst3_vendor = folder(KnownFolder::ProgramFilesCommon)
        .join("VST3")
        .join("Krate Audio");
    let preset_vendor = folder(KnownFolder::ProgramData).join("Krate Audio");
    assert_eq!(
        roots,
        AllowedRoots {
            os: OsRoots::Windows(WindowsRoots {
                vst3_vendor: vst3_vendor.clone(),
                preset_vendor: preset_vendor.clone(),
                run_base: folder(KnownFolder::Windows).join("Temp"),
            }),
            state_dir: StateDirRule::UnderProfiles(folder(KnownFolder::UserProfiles)),
        }
    );
    // The literal values of this box and the hosted runner (D3); compared, never touched.
    assert_eq!(
        vst3_vendor,
        PathBuf::from(r"C:\Program Files\Common Files\VST3\Krate Audio")
    );
    assert_eq!(preset_vendor, PathBuf::from(r"C:\ProgramData\Krate Audio"));
}

#[cfg(target_os = "macos")]
#[test]
fn production_roots_macos() {
    let roots = AllowedRoots::production().expect("the production roots resolve");
    assert_eq!(
        roots,
        AllowedRoots {
            os: OsRoots::Macos(MacosRoots {
                vst3: PathBuf::from("/Library/Audio/Plug-Ins/VST3"),
                components: PathBuf::from("/Library/Audio/Plug-Ins/Components"),
                presets: PathBuf::from("/Library/Application Support/Krate Audio"),
                applications: PathBuf::from("/Applications"),
                volume: PathBuf::from("/"),
                run_base: PathBuf::from(
                    "/Library/Application Support/com.krateaudio.krate-manager-helper"
                ),
                run_ancestors: vec![
                    PathBuf::from("/Library"),
                    PathBuf::from("/Library/Application Support"),
                ],
            }),
            state_dir: StateDirRule::SameOwnerNonRoot,
        }
    );
}

#[cfg(all(unix, not(target_os = "macos")))]
#[test]
fn production_roots_linux() {
    assert_eq!(
        AllowedRoots::production(),
        Ok(AllowedRoots {
            os: OsRoots::None,
            state_dir: StateDirRule::SameOwnerNonRoot,
        })
    );
}

/// FR-026: an existing root is its canonical form; a missing tail is the longest existing ancestor's canonical form
/// with the plain missing components re-joined; the run base, its ancestors and the volume are never canonicalized
/// (D7: the run directory's path is built from the known folder or the literal).
#[test]
fn canonical_keeps_missing_tails() {
    let tmp = tempfile::TempDir::new().expect("a temp dir");
    let base = tmp.path();
    fs::create_dir_all(base.join("vst3")).expect("vst3/");
    let canon = fs::canonicalize(base).expect("the temp dir canonicalizes");
    let world = World::new();
    let io = TempIo::new(&world, base);

    let macos = AllowedRoots {
        os: OsRoots::Macos(MacosRoots {
            vst3: base.join("vst3"),
            components: base.join("missing").join("Components"),
            presets: base.join("missing").join("Krate Audio"),
            applications: base.join("Applications"),
            volume: base.to_path_buf(),
            run_base: base.join("Library").join(HELPER_FOLDER),
            run_ancestors: vec![base.join("Library")],
        }),
        state_dir: StateDirRule::Exact(base.join(STATE_DIR_NAME)),
    };
    let MacosRoots {
        volume,
        run_base,
        run_ancestors,
        ..
    } = match &macos.os {
        OsRoots::Macos(m) => m.clone(),
        _ => unreachable!("built as macOS above"),
    };
    assert_eq!(
        canonical(&io, &macos),
        Ok(CanonicalRoots {
            roots: AllowedRoots {
                os: OsRoots::Macos(MacosRoots {
                    vst3: canon.join("vst3"),
                    components: canon.join("missing").join("Components"),
                    presets: canon.join("missing").join("Krate Audio"),
                    applications: canon.join("Applications"),
                    volume,
                    run_base,
                    run_ancestors,
                }),
                state_dir: StateDirRule::Exact(canon.join(STATE_DIR_NAME)),
            },
        })
    );

    let windows = AllowedRoots {
        os: OsRoots::Windows(WindowsRoots {
            vst3_vendor: base.join("vst3").join("Krate Audio"),
            preset_vendor: base.join("ProgramData").join("Krate Audio"),
            run_base: base.join("run"),
        }),
        state_dir: StateDirRule::UnderProfiles(base.to_path_buf()),
    };
    assert_eq!(
        canonical(&io, &windows),
        Ok(CanonicalRoots {
            roots: AllowedRoots {
                os: OsRoots::Windows(WindowsRoots {
                    vst3_vendor: canon.join("vst3").join("Krate Audio"),
                    preset_vendor: canon.join("ProgramData").join("Krate Audio"),
                    run_base: base.join("run"),
                }),
                state_dir: StateDirRule::UnderProfiles(canon.clone()),
            },
        })
    );

    let linux = AllowedRoots {
        os: OsRoots::None,
        state_dir: StateDirRule::SameOwnerNonRoot,
    };
    assert_eq!(
        canonical(&io, &linux),
        Ok(CanonicalRoots {
            roots: linux.clone()
        })
    );
}

/// FR-026: a component that is not plain (`..`) or a relative root is `E_HELPER_ROOTS`.
#[test]
fn canonical_refuses_non_plain_components() {
    let tmp = tempfile::TempDir::new().expect("a temp dir");
    let base = tmp.path();
    let world = World::new();
    let io = TempIo::new(&world, base);
    let with_vst3 = |vst3: PathBuf| AllowedRoots {
        os: OsRoots::Windows(WindowsRoots {
            vst3_vendor: vst3,
            preset_vendor: base.join("presets"),
            run_base: base.join("run"),
        }),
        state_dir: StateDirRule::Exact(base.join(STATE_DIR_NAME)),
    };
    for bad in [
        base.join("missing").join("..").join("x"),
        base.join("..").join("x"),
        PathBuf::from("relative").join("x"),
    ] {
        assert_eq!(
            canonical(&io, &with_vst3(bad.clone())),
            Err(error::ROOTS),
            "{}",
            bad.display()
        );
    }
    let state = AllowedRoots {
        os: OsRoots::None,
        state_dir: StateDirRule::Exact(base.join("missing").join("..").join(STATE_DIR_NAME)),
    };
    assert_eq!(canonical(&io, &state), Err(error::ROOTS));
}
