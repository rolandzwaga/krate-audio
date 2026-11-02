//! The four operations and the batch runner (plan §4.1.10; FR-030 to FR-036, FR-040 to FR-043; PD-17, PD-24).

use crate::HelperOs;
use crate::classify::InnoVerdict;
use crate::digest::Sha256Stream;
use crate::error::{self, HelperError};
use crate::job::Op;
use crate::result::{self, OpEntry, OpOutcome};
use crate::roots::{CanonicalRoots, OsRoots};
use crate::seams::{HelperIo, Seams, WriteSync};
use crate::validate::{self, JobPlace, LstatFacts};
use std::ffi::OsString;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::time::Duration;

/// FR-030 step 4 (D14).
pub const INNO_INSTALL_TIMEOUT: Duration = Duration::from_secs(600);
/// FR-032 (D14).
pub const PKG_INSTALL_TIMEOUT: Duration = Duration::from_secs(600);
/// D6, PD-24: the uninstaller's own wait and the poll bound.
pub const UNINSTALL_POLL_TIMEOUT: Duration = Duration::from_secs(120);
/// D6.
pub const UNINSTALL_POLL_INTERVAL: Duration = Duration::from_millis(500);
/// FR-033.
pub const PKGUTIL_TIMEOUT: Duration = Duration::from_secs(30);
/// FR-030 step 3.
pub const INNO_FLAGS: [&str; 4] = [
    "/VERYSILENT",
    "/SUPPRESSMSGBOXES",
    "/NORESTART",
    "/NOCLOSEAPPLICATIONS",
];
/// FR-031.
pub const UNINSTALL_FLAGS: [&str; 3] = ["/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"];
/// FR-032.
pub const INSTALLER: &str = "/usr/sbin/installer";
/// FR-033.
pub const PKGUTIL: &str = "/usr/sbin/pkgutil";
/// FR-031: the HKLM parent of every Inno uninstall key.
pub const UNINSTALL_KEY: &str = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
/// FR-031: the suffix Inno gives its uninstall key.
pub const INNO_KEY_SUFFIX: &str = "_is1";

/// FR-035: the copy's chunk size.
const CHUNK: usize = 64 * 1024;
/// SI-28: the installer log is read from its last MiB.
const LOG_TAIL_BYTES: u64 = 1024 * 1024;
/// PD-19: a name collision on the run directory is retried up to four times.
const RUN_DIR_ATTEMPTS: usize = 5;

/// What every op of one batch borrows.
pub struct OpCtx<'a> {
    pub os: HelperOs,
    pub roots: &'a CanonicalRoots,
    pub place: &'a JobPlace,
}

/// FR-035: 64 KiB chunks, the digest over exactly the bytes written, `sync_all` before returning. The source is opened
/// through `HelperIo::open_source` (PD-6: no final link, no blocking, a regular file), failing `E_HELPER_SOURCE`; a read
/// or write error removes the partial copy and is `E_HELPER_COPY`.
pub fn copy_hashed(io: &dyn HelperIo, src: &Path, dest: &Path) -> Result<[u8; 32], HelperError> {
    let mut reader = io.open_source(src).map_err(|_| error::SOURCE)?;
    let mut out = io.create_new(dest).map_err(|_| error::COPY)?;
    let copied = copy_into(&mut *reader, &mut *out);
    drop(out);
    drop(reader);
    copied.map_err(|_| {
        // The partial copy is the helper's own file in the admin-only run directory; a failed removal is left to
        // the run directory's cleanup (FR-043).
        let _ = io.remove_file(dest);
        error::COPY
    })
}

/// The copy loop of `copy_hashed`: every chunk written, then hashed, then the destination synced.
fn copy_into(reader: &mut dyn Read, out: &mut dyn WriteSync) -> io::Result<[u8; 32]> {
    let mut buf = vec![0u8; CHUNK];
    let mut digest = Sha256Stream::new();
    loop {
        let n = match reader.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        };
        out.write_all(&buf[..n])?;
        digest.update(&buf[..n]);
    }
    out.sync_all()?;
    Ok(digest.finish())
}

/// One op's outcome before its index, kind and time are added.
struct Step {
    outcome: OpOutcome,
    code: Option<&'static str>,
    exit_code: Option<i32>,
    log_tail: Vec<String>,
    /// FR-042: an installer still running at its timeout stops the batch.
    stops: bool,
}

impl Step {
    fn of(outcome: OpOutcome, e: HelperError) -> Step {
        Step {
            outcome,
            code: Some(e.code),
            exit_code: None,
            log_tail: Vec::new(),
            stops: false,
        }
    }

    fn done(exit_code: Option<i32>) -> Step {
        Step {
            outcome: OpOutcome::Done,
            code: None,
            exit_code,
            log_tail: Vec::new(),
            stops: false,
        }
    }

    fn failed(e: HelperError) -> Step {
        Step::of(OpOutcome::Failed, e)
    }

    fn refused(e: HelperError) -> Step {
        Step::of(OpOutcome::Refused, e)
    }

    fn exited(mut self, code: i32) -> Step {
        self.exit_code = Some(code);
        self
    }
}

/// The job's one run directory (D7), created lazily before its first installer op.
enum RunDir {
    NotYet,
    Ready(PathBuf),
    /// Creation or the ancestor check failed; the path when the directory itself was created.
    Failed(Option<PathBuf>),
}

/// The ops strictly in order, one entry each (FR-040 to FR-043).
pub fn run_batch(ops: &[Op], ctx: &OpCtx<'_>, seams: &Seams<'_>) -> Vec<OpEntry> {
    let io = seams.io;
    let mut run_dir = RunDir::NotYet;
    let mut stopped = false;
    let mut entries = Vec::with_capacity(ops.len());
    for (i, op) in ops.iter().enumerate() {
        let index = u32::try_from(i).unwrap_or(u32::MAX);
        if stopped {
            entries.push(entry(
                index,
                op,
                Step::of(OpOutcome::NotRun, error::AFTER_TIMEOUT),
                0.0,
            ));
            continue;
        }
        let start = io.now();
        let step = match op {
            Op::RunInnoInstaller { file, sha256 } | Op::RunPkgInstaller { file, sha256 } => {
                if matches!(run_dir, RunDir::NotYet) {
                    run_dir = create_run_dir(ctx, io);
                }
                match &run_dir {
                    RunDir::Ready(run) => installer(i, op, file, sha256, run, ctx, seams),
                    _ => Step::failed(error::RUN_DIR),
                }
            }
            Op::RunInnoUninstaller { app_id } => inno_uninstaller(app_id, ctx, seams),
            Op::UninstallPkg { pkg_id } => uninstall_pkg(pkg_id, ctx, seams),
        };
        let seconds = io.now().saturating_sub(start).as_secs_f64();
        stopped = step.stops;
        entries.push(entry(index, op, step, seconds));
    }
    remove_run_dir(io, run_dir);
    entries
}

fn entry(index: u32, op: &Op, step: Step, seconds: f64) -> OpEntry {
    OpEntry {
        index,
        op: op.kind(),
        outcome: step.outcome,
        code: step.code,
        exit_code: step.exit_code,
        seconds,
        log_tail: step.log_tail,
    }
}

/// `run-<random_hex>` under `run_base` (D7); an existing name is retried with a new one (PD-19), never reused.
fn fresh_run_dir(io: &dyn HelperIo, run_base: &Path) -> Option<PathBuf> {
    for _ in 0..RUN_DIR_ATTEMPTS {
        let run = run_base.join(format!("run-{}", io.random_hex()));
        match io.create_run_dir(&run) {
            Ok(()) => return Some(run),
            Err(e) if e.kind() == io::ErrorKind::AlreadyExists => {}
            Err(_) => return None,
        }
    }
    None
}

/// D7: Windows creates the run directory with its DACL (the seam's job); macOS creates the helper folder when absent,
/// then the run directory, then checks `run_ancestors` + `run_base` + the new directory (PD-5, PD-26).
fn create_run_dir(ctx: &OpCtx<'_>, io: &dyn HelperIo) -> RunDir {
    match (ctx.os, &ctx.roots.roots.os) {
        (HelperOs::Windows, OsRoots::Windows(w)) => match fresh_run_dir(io, &w.run_base) {
            Some(run) => RunDir::Ready(run),
            None => RunDir::Failed(None),
        },
        (HelperOs::Macos, OsRoots::Macos(m)) => {
            if io.create_helper_folder(&m.run_base).is_err() {
                return RunDir::Failed(None);
            }
            let Some(run) = fresh_run_dir(io, &m.run_base) else {
                return RunDir::Failed(None);
            };
            let chain: Option<Vec<LstatFacts>> = m
                .run_ancestors
                .iter()
                .chain([&m.run_base, &run])
                .map(|p| io.lstat(p).ok().flatten())
                .collect();
            match chain.map(|facts| validate::run_dir_ancestors_ok(&facts)) {
                Some(Ok(())) => RunDir::Ready(run),
                _ => RunDir::Failed(Some(run)),
            }
        }
        _ => RunDir::Failed(None),
    }
}

/// FR-043: every entry of the run directory, then the directory; failures are ignored. A directory whose ancestor
/// check failed is only `remove_dir`ed: it may sit under a path someone else controls, so it is never listed.
fn remove_run_dir(io: &dyn HelperIo, run_dir: RunDir) {
    match run_dir {
        RunDir::Ready(run) => {
            for name in io.list_dir(&run).unwrap_or_default() {
                let _ = io.remove_file(&run.join(name));
            }
            let _ = io.remove_dir(&run);
        }
        RunDir::Failed(Some(run)) => {
            let _ = io.remove_dir(&run);
        }
        RunDir::Failed(None) | RunDir::NotYet => {}
    }
}

/// FR-030, FR-032: validate, copy with the hash, spawn the copy, wait, map the outcome.
fn installer(
    i: usize,
    op: &Op,
    file: &str,
    sha256: &[u8; 32],
    run: &Path,
    ctx: &OpCtx<'_>,
    seams: &Seams<'_>,
) -> Step {
    let io = seams.io;
    let ext = match op {
        Op::RunInnoInstaller { .. } => "exe",
        _ => "pkg",
    };
    let src = match validate::source(io, file, ctx.place, ctx.os, ext) {
        Ok(src) => src,
        Err(e) => return Step::refused(e),
    };
    let copy = run.join(format!("installer-{i}.{ext}"));
    match copy_hashed(io, &src, &copy) {
        Ok(digest) if digest == *sha256 => {}
        Ok(_) => {
            let _ = io.remove_file(&copy);
            return Step::failed(error::HASH);
        }
        // FR-020: a source the no-follow open refuses is a source refusal like the lstat check's.
        Err(e) if e == error::SOURCE => return Step::refused(e),
        Err(e) => return Step::failed(e),
    }
    let (argv, timeout, log) = match op {
        Op::RunInnoInstaller { .. } => {
            let log = run.join(format!("inno-{i}.log"));
            let mut log_arg = OsString::from("/LOG=");
            log_arg.push(log.as_os_str());
            let mut argv = vec![copy.into_os_string()];
            argv.extend(INNO_FLAGS.iter().map(OsString::from));
            argv.push(log_arg);
            (argv, INNO_INSTALL_TIMEOUT, Some(log))
        }
        _ => {
            let argv = vec![
                OsString::from(INSTALLER),
                OsString::from("-pkg"),
                copy.into_os_string(),
                OsString::from("-target"),
                OsString::from("/"),
            ];
            (argv, PKG_INSTALL_TIMEOUT, None)
        }
    };
    let Ok(child) = seams.runner.spawn(&argv) else {
        return Step::failed(error::SPAWN);
    };
    // FR-042: never killed; a wait error is treated as a child that may still run, so the batch stops too.
    let Ok(Some(code)) = seams.runner.wait(child, timeout) else {
        return Step {
            stops: true,
            ..Step::failed(error::TIMEOUT)
        };
    };
    let Some(log) = log else {
        return if code == 0 {
            Step::done(Some(code))
        } else {
            Step::failed(error::EXIT).exited(code)
        };
    };
    // SI-28: the last MiB, every line of it to the classifier, the last 40 to the result. A missing log reads empty.
    let bytes = io.read_tail(&log, LOG_TAIL_BYTES).unwrap_or_default();
    let mut step = match (seams.classify)(code, &result::log_lines(&bytes)) {
        InnoVerdict::Installed => Step::done(Some(code)),
        InnoVerdict::Locked => Step::of(OpOutcome::Locked, error::LOCKED).exited(code),
        InnoVerdict::Failed => Step::failed(error::EXIT).exited(code),
    };
    step.log_tail = result::log_tail(&bytes);
    step
}

/// FR-031, D6, PD-24.
fn inno_uninstaller(app_id: &str, ctx: &OpCtx<'_>, seams: &Seams<'_>) -> Step {
    let io = seams.io;
    if !validate::is_app_id(app_id) {
        return Step::refused(error::APP_ID);
    }
    let OsRoots::Windows(roots) = &ctx.roots.roots.os else {
        return Step::refused(error::UNINSTALLER);
    };
    let subkey = format!("{UNINSTALL_KEY}\\{app_id}{INNO_KEY_SUFFIX}");
    let Some(values) = seams.registry.uninstall_values(&subkey) else {
        return Step::failed(error::NO_KEY);
    };
    let unins = match validate::uninstaller(io, roots, values) {
        Ok(unins) => unins,
        Err(e) => return Step::refused(e),
    };
    // FR-027: `exe` is the registry's own (unquoted) string, whose canonical form passed FR-031.
    let mut argv = vec![unins.exe.clone().into_os_string()];
    argv.extend(UNINSTALL_FLAGS.iter().map(OsString::from));
    let Ok(child) = seams.runner.spawn(&argv) else {
        return Step::failed(error::SPAWN);
    };
    match seams.runner.wait(child, UNINSTALL_POLL_TIMEOUT) {
        Ok(Some(0)) => {}
        Ok(Some(code)) => return Step::failed(error::EXIT).exited(code),
        Ok(None) | Err(_) => return Step::failed(error::UNINSTALL_TIMEOUT),
    }
    let poll_start = io.now();
    loop {
        // All three are read on every check, so each check is one `key_exists` (SC-004 counts them).
        let exe_gone = matches!(io.lstat(&unins.exe), Ok(None));
        let no_bundle = no_vst3_entry(io, &unins.install_dir);
        let key_gone = !seams.registry.key_exists(&subkey);
        if exe_gone && no_bundle && key_gone {
            return Step::done(Some(0));
        }
        if io.now().saturating_sub(poll_start) >= UNINSTALL_POLL_TIMEOUT {
            return Step::failed(error::UNINSTALL_TIMEOUT).exited(0);
        }
        io.sleep(UNINSTALL_POLL_INTERVAL);
    }
}

/// D6: no entry of `dir` ends in `.vst3` (ASCII case-insensitive); an absent directory has none, any other listing
/// error counts as "still there".
fn no_vst3_entry(io: &dyn HelperIo, dir: &Path) -> bool {
    match io.list_dir(dir) {
        Ok(names) => !names
            .iter()
            .any(|n| n.to_string_lossy().to_ascii_lowercase().ends_with(".vst3")),
        Err(e) => e.kind() == io::ErrorKind::NotFound,
    }
}

/// One bounded `pkgutil` run: its stdout on exit 0; `E_HELPER_SPAWN` when it cannot be started (plan SI-12);
/// `otherwise` on another exit or the timeout.
fn pkgutil(
    seams: &Seams<'_>,
    args: &[&str],
    otherwise: HelperError,
) -> Result<Vec<u8>, HelperError> {
    let argv: Vec<OsString> = std::iter::once(PKGUTIL)
        .chain(args.iter().copied())
        .map(OsString::from)
        .collect();
    match seams.runner.output(&argv, PKGUTIL_TIMEOUT) {
        Ok(Some((0, stdout))) => Ok(stdout),
        Ok(_) => Err(otherwise),
        Err(_) => Err(error::SPAWN),
    }
}

/// FR-033: the receipt's `location` and its two listings (files, dirs).
fn read_receipt(
    seams: &Seams<'_>,
    pkg_id: &str,
) -> Result<(String, Vec<u8>, Vec<u8>), HelperError> {
    let info = pkgutil(seams, &["--pkg-info", pkg_id], error::PKG_RECEIPT)?;
    let location = receipt_location(&info, pkg_id).ok_or(error::PKG_RECEIPT)?;
    let files = pkgutil(
        seams,
        &["--files", pkg_id, "--only-files"],
        error::PKG_RECEIPT,
    )?;
    let dirs = pkgutil(
        seams,
        &["--files", pkg_id, "--only-dirs"],
        error::PKG_RECEIPT,
    )?;
    Ok((location, files, dirs))
}

/// `--pkg-info`'s `location` when its `package-id` is `pkg_id` (absent location = empty, FR-033).
fn receipt_location(info: &[u8], pkg_id: &str) -> Option<String> {
    let text = String::from_utf8_lossy(info);
    let mut id = None;
    let mut location = String::new();
    for line in text.lines() {
        if let Some(v) = line.strip_prefix("package-id:") {
            id = Some(v.trim().to_owned());
        } else if let Some(v) = line.strip_prefix("location:") {
            v.trim().clone_into(&mut location);
        }
    }
    (id.as_deref() == Some(pkg_id)).then_some(location)
}

/// FR-033, FR-034: read the receipt, validate every line, remove, then forget only when every file is gone.
fn uninstall_pkg(pkg_id: &str, ctx: &OpCtx<'_>, seams: &Seams<'_>) -> Step {
    let io = seams.io;
    if !validate::is_pkg_id(pkg_id) {
        return Step::refused(error::PKG_ID);
    }
    let OsRoots::Macos(roots) = &ctx.roots.roots.os else {
        return Step::refused(error::PKG_PATH);
    };
    let (location, files, dirs) = match read_receipt(seams, pkg_id) {
        Ok(receipt) => receipt,
        Err(e) => return Step::failed(e),
    };
    let plan = match validate::pkg_plan(io, roots, &location, &files, &dirs) {
        Ok(plan) => plan,
        Err(e) => return Step::refused(e),
    };
    for file in &plan.files {
        // A link is removed as the link; an absent entry counts as removed (checked below).
        let _ = io.remove_file(file);
    }
    for dir in &plan.dirs {
        // Deepest first; a non-empty directory is left.
        let _ = io.remove_dir(dir);
    }
    if plan.files.iter().any(|f| !matches!(io.lstat(f), Ok(None))) {
        return Step::failed(error::REMOVE);
    }
    match pkgutil(seams, &["--forget", pkg_id], error::FORGET) {
        Ok(_) => Step::done(None),
        Err(e) => Step::failed(e),
    }
}

#[cfg(test)]
#[path = "ops_tests.rs"]
mod tests;
