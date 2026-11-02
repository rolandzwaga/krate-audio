//! Arguments and the binary's entry (plan §4.1.3; FR-005, FR-050, FR-052, SI-1).

use crate::digest;
use crate::error::HelperError;
use crate::roots::AllowedRoots;
use crate::seams::Seams;
use crate::{Exit, HelperOs};
use std::ffi::OsString;
use std::path::PathBuf;

/// The parsed argv (FR-005).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Args {
    pub job: PathBuf,
    pub job_sha256: [u8; 32],
    #[cfg(feature = "ci-temp-roots")]
    pub ci_roots: Option<PathBuf>,
}

/// The one usage line (exit 2).
pub const USAGE: &str = "usage: krate-manager-helper --job <path> --job-sha256 <64 lower-case hex>";

/// Store `value` in an empty slot; a repeated flag is a usage error (FR-005).
fn once<T>(slot: &mut Option<T>, value: T) -> Option<()> {
    if slot.is_some() {
        return None;
    }
    *slot = Some(value);
    Some(())
}

/// The argv without argv[0]; `None` is a usage error (FR-005, PD-25): exactly `--job <path>` and `--job-sha256 <64
/// lower-case hex>` in either order (plus `--ci-roots <path>` only in a `ci-temp-roots` build), each once, nothing
/// else.
pub fn parse_args(args: &[OsString]) -> Option<Args> {
    let mut job: Option<PathBuf> = None;
    let mut job_sha256: Option<[u8; 32]> = None;
    #[cfg(feature = "ci-temp-roots")]
    let mut ci_roots: Option<PathBuf> = None;
    let mut rest = args.iter();
    while let Some(flag) = rest.next() {
        let value = rest.next()?;
        match flag.to_str()? {
            "--job" => once(&mut job, PathBuf::from(value))?,
            "--job-sha256" => once(&mut job_sha256, digest::parse_hex64(value.to_str()?)?)?,
            #[cfg(feature = "ci-temp-roots")]
            "--ci-roots" => once(&mut ci_roots, PathBuf::from(value))?,
            _ => return None,
        }
    }
    Some(Args {
        job: job?,
        job_sha256: job_sha256?,
        #[cfg(feature = "ci-temp-roots")]
        ci_roots,
    })
}

/// The library entry `main_with` calls (FR-050, SI-17).
pub type RunFn = fn(&Args, HelperOs, &AllowedRoots, &Seams<'_>) -> Exit;

/// The roots of this run: production, or in a `ci-temp-roots` build given `--ci-roots` the file's (FR-052).
#[cfg(feature = "ci-temp-roots")]
fn roots_for(args: &Args, seams: &Seams<'_>) -> Result<AllowedRoots, HelperError> {
    match &args.ci_roots {
        Some(file) => crate::roots::from_ci_file(seams.io, file),
        None => AllowedRoots::production(),
    }
}

/// The roots of this run: always production (FR-051).
#[cfg(not(feature = "ci-temp-roots"))]
fn roots_for(_args: &Args, _seams: &Seams<'_>) -> Result<AllowedRoots, HelperError> {
    AllowedRoots::production()
}

/// Parse, build the roots, take `HelperOs::current()` and call `run` (FR-050). A usage error prints `USAGE` once on
/// stderr and exits 2 (D15); a root that cannot be built exits 4 `E_HELPER_ROOTS` with nothing written.
pub fn main_with(args: Vec<OsString>, seams: &Seams<'_>, run: RunFn) -> Exit {
    let Some(args) = parse_args(&args) else {
        eprintln!("{USAGE}");
        return Exit {
            status: 2,
            code: None,
        };
    };
    match roots_for(&args, seams) {
        Ok(roots) => run(&args, HelperOs::current(), &roots, seams),
        Err(e) => Exit {
            status: 4,
            code: Some(e.code),
        },
    }
}

#[cfg(test)]
#[path = "cli_tests.rs"]
mod tests;
