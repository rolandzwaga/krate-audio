//! FR-105 (plan §4.5, SI-14): the manual elevation check a person runs by hand; no test, gate or workflow runs it.
//!
//! `cargo build -p krate-manager-helper`, then
//! `cargo run -p krate-manager --example manual_elevation --features manual-elevation -- <empty dir>`.
//!
//! It raises the real elevation prompt once, for a job whose one op the helper refuses before any spawn, registry read
//! or filesystem effect, prints the `LaunchOutcome` and the result file, and removes only what it created; `<empty dir>`
//! itself stays. Expected: Windows `Exited(4)` (`E_HELPER_JOB_LOCATION`: `<empty dir>` is not under the profiles
//! folder, FR-025); macOS `Exited(0)` with exactly one `refused E_HELPER_PKG_ID`.

use krate_manager_core::install::job::{self, JobOp, WrittenJob};
use krate_manager_core::install::{ElevatedCommand, Elevation, FsInstallIo};
use krate_manager_core::platform::{APP_IDENTIFIER, NativePlatform, Os, Platform};
use krate_manager_lib::resolve_elevation;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::time::Duration;

const USAGE: &str = "usage: manual_elevation <existing empty directory>";
/// SI-14: the bound of the one launch.
const LAUNCH_BOUND: Duration = Duration::from_secs(120);

fn main() -> ExitCode {
    let mut args = std::env::args_os().skip(1);
    let (Some(arg), None) = (args.next(), args.next()) else {
        eprintln!("{USAGE}");
        return ExitCode::from(2);
    };
    let Some(dir) = empty_dir(Path::new(&arg)) else {
        eprintln!(
            "refused: {} is not an existing, empty directory that is not a link; nothing written",
            Path::new(&arg).display()
        );
        return ExitCode::from(2);
    };
    // SI-14: one op the helper's own matcher refuses (`is_app_id` / `is_pkg_id`), never an installer op, so no run
    // directory is ever created.
    let op = match NativePlatform.os() {
        Os::Windows => JobOp::RunInnoUninstaller {
            app_id: "not-a-guid".to_owned(),
        },
        Os::Macos => JobOp::UninstallPkg {
            pkg_id: "com.krateaudio.NOT-VALID".to_owned(),
        },
        Os::Linux => {
            eprintln!("Linux has no elevated helper; nothing written");
            return ExitCode::from(1);
        }
    };
    // PD-23: `Path::parent` is lexical, so the synthetic exe sits in the profile dir (`target/debug`), where
    // `cargo build -p krate-manager-helper` puts the helper, not in `target/debug/examples`.
    let profile = match std::env::current_exe() {
        Ok(exe) => exe.parent().and_then(Path::parent).map(Path::to_path_buf),
        Err(e) => {
            eprintln!("cannot read this example's path: {e}; nothing written");
            return ExitCode::from(1);
        }
    };
    let Some(profile) = profile else {
        eprintln!("this example's path has no profile directory; nothing written");
        return ExitCode::from(1);
    };
    let elevation = resolve_elevation(Some(&profile.join("manual_elevation")));
    let Some(helper) = elevation.helper() else {
        eprintln!(
            "no helper beside {}: run `cargo build -p krate-manager-helper` first; nothing written",
            profile.display()
        );
        return ExitCode::from(1);
    };
    let mut created = Vec::new();
    let mut written = None;
    let code = launch_once(&elevation, helper, op, &dir, &mut created, &mut written);
    if remove_created(written.as_ref(), &created) {
        code
    } else {
        ExitCode::from(1)
    }
}

/// SI-14 step 1: `arg` exists, is a directory (by `symlink_metadata`, so a link or junction is refused) and has no
/// entry; its canonical form, so the job path is the verbatim `\\?\` form on Windows (FR-027).
fn empty_dir(arg: &Path) -> Option<PathBuf> {
    if !fs::symlink_metadata(arg).ok()?.file_type().is_dir() {
        return None;
    }
    if fs::read_dir(arg).ok()?.next().is_some() {
        return None;
    }
    fs::canonicalize(arg).ok()
}

/// SI-14 steps 2, 3 and 5: the two directories (each recorded in `created` once made), the job (in `written`), the one
/// launch, and what it printed.
fn launch_once(
    elevation: &dyn Elevation,
    helper: &Path,
    op: JobOp,
    dir: &Path,
    created: &mut Vec<PathBuf>,
    written: &mut Option<WrittenJob>,
) -> ExitCode {
    let state = dir.join(APP_IDENTIFIER);
    for path in [state.clone(), state.join(job::JOBS_DIR)] {
        if let Err(e) = fs::create_dir(&path) {
            eprintln!("cannot create {}: {e}", path.display());
            return ExitCode::from(1);
        }
        created.push(path);
    }
    let job = match job::write(&FsInstallIo, &state, vec![op]) {
        Ok(job) => written.insert(job),
        Err(e) => {
            eprintln!("cannot write the job: {e}");
            return ExitCode::from(1);
        }
    };
    let Some(job_path) = job.path.to_str() else {
        eprintln!("the job path {} is not UTF-8", job.path.display());
        return ExitCode::from(1);
    };
    let command = ElevatedCommand {
        program: helper.to_path_buf(),
        args: vec![
            "--job".to_owned(),
            job_path.to_owned(),
            "--job-sha256".to_owned(),
            job.sha256.clone(),
        ],
    };
    println!("launching {} {:?}", command.program.display(), command.args);
    let outcome = elevation.launch(&command, LAUNCH_BOUND);
    println!("outcome: {outcome:?}");
    match fs::read(&job.result) {
        Ok(bytes) => println!(
            "result {}:\n{}",
            job.result.display(),
            String::from_utf8_lossy(&bytes)
        ),
        Err(e) => println!("no result at {}: {e}", job.result.display()),
    }
    ExitCode::SUCCESS
}

/// SI-14 step 6: the job file, its result (if the helper wrote one), then the recorded directories innermost first,
/// each with `remove_file` / `remove_dir` (never a tree removal). An error is printed, not retried.
fn remove_created(job: Option<&WrittenJob>, created: &[PathBuf]) -> bool {
    let mut clean = true;
    let mut report = |path: &Path, removed: io::Result<()>| {
        if let Err(e) = removed {
            eprintln!("cannot remove {}: {e}", path.display());
            clean = false;
        }
    };
    if let Some(job) = job {
        report(&job.path, fs::remove_file(&job.path));
        if fs::symlink_metadata(&job.result).is_ok() {
            report(&job.result, fs::remove_file(&job.result));
        }
    }
    for path in created.iter().rev() {
        report(path, fs::remove_dir(path));
    }
    clean
}
