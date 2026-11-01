//! krate-manager-helper: the elevated helper's fixed operation set (Phase 10 plan §4.1, PD-1 to PD-7).

pub mod classify;
pub mod cli;
pub mod digest;
pub mod error;
pub mod job;
pub mod ops;
pub mod os;
pub mod result;
pub mod roots;
pub mod seams;
pub mod validate;

use std::io;
use std::process::ExitCode;

/// The OS a job is checked and run for (D18); passed as a value so every OS's logic runs on every leg.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HelperOs {
    Windows,
    Macos,
    Linux,
}

impl HelperOs {
    /// The build's own OS: the only cfg-derived OS value (D18); only `cli::main_with` passes it.
    pub fn current() -> HelperOs {
        if cfg!(windows) {
            HelperOs::Windows
        } else if cfg!(target_os = "macos") {
            HelperOs::Macos
        } else {
            HelperOs::Linux
        }
    }
}

/// What `run` ended with (PD-4): the process status and the code behind it, if any.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Exit {
    pub status: u8,
    pub code: Option<&'static str>,
}

impl From<Exit> for ExitCode {
    fn from(exit: Exit) -> ExitCode {
        ExitCode::from(exit.status)
    }
}

/// Exit 4: nothing written (D15).
fn nothing_written(e: error::HelperError) -> Exit {
    Exit {
        status: 4,
        code: Some(e.code),
    }
}

/// Steps 4 to 6 of plan §4.1.2: the bounded read, the hash binding (D2), the parse.
fn read_job(
    args: &cli::Args,
    os: HelperOs,
    seams: &seams::Seams<'_>,
) -> Result<Vec<job::Op>, error::HelperError> {
    let bytes = seams
        .io
        .read_bounded(&args.job, job::MAX_JOB_BYTES)
        .map_err(|_| error::JOB_READ)?;
    if bytes.len() as u64 > job::MAX_JOB_BYTES {
        return Err(error::JOB_READ);
    }
    if digest::sha256(&bytes) != args.job_sha256 {
        return Err(error::JOB_HASH);
    }
    job::parse(&bytes, os)
}

/// One job (plan §4.1.2, PD-3): every exit-4 point lies before the result file is reserved, every exit-3 point after
/// it, so an exit-3 result always exists.
pub fn run(
    args: &cli::Args,
    os: HelperOs,
    roots: &roots::AllowedRoots,
    seams: &seams::Seams<'_>,
) -> Exit {
    // 1. The roots, canonicalized once (FR-026).
    let canonical = match roots::canonical(seams.io, roots) {
        Ok(canonical) => canonical,
        Err(e) => return nothing_written(e),
    };
    // 2. The job's location (FR-021, FR-025, FR-027).
    let place = match validate::job_location(seams.io, &args.job, &canonical, os) {
        Ok(place) => place,
        Err(e) => return nothing_written(e),
    };
    // 3. The result file, reserved with `create_new` (FR-003).
    let mut out = match seams.io.create_new(&place.result_path) {
        Ok(out) => out,
        Err(e) if e.kind() == io::ErrorKind::AlreadyExists => {
            return nothing_written(error::RESULT_EXISTS);
        }
        Err(_) => return nothing_written(error::RESULT_WRITE),
    };
    // 4 to 7. Read, hash, parse, run.
    let (job_error, entries) = match read_job(args, os, seams) {
        Ok(ops) => {
            let ctx = ops::OpCtx {
                os,
                roots: &canonical,
                place: &place,
            };
            (None, ops::run_batch(&ops, &ctx, seams))
        }
        Err(e) => (Some(e.code), Vec::new()),
    };
    let file = result::ResultFile {
        result_version: 1,
        job_sha256: digest::hex(&args.job_sha256),
        job_error,
        ops: entries,
    };
    if result::write(&mut *out, &file).is_err() {
        return nothing_written(error::RESULT_WRITE);
    }
    match job_error {
        Some(code) => Exit {
            status: 3,
            code: Some(code),
        },
        None => Exit {
            status: 0,
            code: None,
        },
    }
}
