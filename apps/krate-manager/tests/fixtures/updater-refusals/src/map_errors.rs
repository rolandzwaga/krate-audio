//! `map-errors` (plan §3.2, §4.7): 31 of the updater's 32 `Error` variants (all but `Reqwest`, which has no public
//! constructor) constructed and mapped with `map_error`, each compared with its §3.2 row. No server, no mock app.

use base64::Engine;
use base64::engine::general_purpose::STANDARD;
use krate_manager_core::self_update::{SelfUpdateError, UrlRefusal};
use krate_manager_lib::self_update::map_error;
use std::path::PathBuf;
use tauri_plugin_updater::Error;

/// The row count the table must have.
const ROWS: usize = 31;

/// The code (and, for `E_SELF_UPDATE_URL`, the reason) a row expects.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Want {
    Network,
    Manifest,
    Signature,
    Url(UrlRefusal),
    Unsupported,
    Install,
    /// `Busy` or `None`: never a mapped updater error.
    Other,
}

fn got(e: &SelfUpdateError) -> Want {
    match e {
        SelfUpdateError::Network { .. } => Want::Network,
        SelfUpdateError::Manifest { .. } => Want::Manifest,
        SelfUpdateError::Signature { .. } => Want::Signature,
        SelfUpdateError::Url { reason } => Want::Url(*reason),
        SelfUpdateError::Unsupported => Want::Unsupported,
        SelfUpdateError::Install { .. } => Want::Install,
        SelfUpdateError::Busy | SelfUpdateError::None => Want::Other,
    }
}

/// The error a constructor returns; a constructor that succeeds is a failure of the row.
fn err_of<T, E>(r: Result<T, E>, what: &str) -> Result<E, String> {
    match r {
        Err(e) => Ok(e),
        Ok(_) => Err(format!("{what} did not fail")),
    }
}

fn rows() -> Result<Vec<(&'static str, Error, Want)>, String> {
    Ok(vec![
        // The 15 unit variants.
        ("EmptyEndpoints", Error::EmptyEndpoints, Want::Install),
        ("ReleaseNotFound", Error::ReleaseNotFound, Want::Manifest),
        ("UnsupportedArch", Error::UnsupportedArch, Want::Unsupported),
        ("UnsupportedOs", Error::UnsupportedOs, Want::Unsupported),
        (
            "FailedToDetermineExtractPath",
            Error::FailedToDetermineExtractPath,
            Want::Install,
        ),
        (
            "TempDirNotOnSameMountPoint",
            Error::TempDirNotOnSameMountPoint,
            Want::Install,
        ),
        (
            "BinaryNotFoundInArchive",
            Error::BinaryNotFoundInArchive,
            Want::Install,
        ),
        ("TempDirNotFound", Error::TempDirNotFound, Want::Install),
        (
            "AuthenticationFailed",
            Error::AuthenticationFailed,
            Want::Install,
        ),
        ("DebInstallFailed", Error::DebInstallFailed, Want::Install),
        (
            "PackageInstallFailed",
            Error::PackageInstallFailed,
            Want::Install,
        ),
        (
            "InvalidUpdaterFormat",
            Error::InvalidUpdaterFormat,
            Want::Install,
        ),
        ("FormatDate", Error::FormatDate, Want::Manifest),
        (
            "InsecureTransportProtocol",
            Error::InsecureTransportProtocol,
            Want::Url(UrlRefusal::Policy),
        ),
        (
            "MissingSignedVersion",
            Error::MissingSignedVersion,
            Want::Signature,
        ),
        // The six with plain data.
        (
            "TargetNotFound",
            Error::TargetNotFound("x".to_owned()),
            Want::Manifest,
        ),
        (
            "TargetsNotFound",
            Error::TargetsNotFound(vec![]),
            Want::Manifest,
        ),
        ("Network", Error::Network("x".to_owned()), Want::Network),
        (
            "SignatureUtf8",
            Error::SignatureUtf8("x".to_owned()),
            Want::Signature,
        ),
        (
            "SignedVersionMismatch",
            Error::SignedVersionMismatch {
                signed: "0.0.9".to_owned(),
                announced: "0.2.0".to_owned(),
            },
            Want::Signature,
        ),
        (
            "PreviousAppNotRestored",
            Error::PreviousAppNotRestored(PathBuf::from("x")),
            Want::Install,
        ),
        // The ten wrapping another crate's error.
        ("Io", Error::Io(std::io::Error::other("x")), Want::Install),
        (
            "Serialization",
            Error::Serialization(err_of(
                serde_json::from_str::<serde_json::Value>("{"),
                "serde_json",
            )?),
            Want::Manifest,
        ),
        (
            "UrlParse",
            Error::UrlParse(err_of(url::Url::parse(""), "Url::parse")?),
            Want::Manifest,
        ),
        (
            "Base64",
            Error::Base64(err_of(STANDARD.decode("!"), "base64 decode")?),
            Want::Signature,
        ),
        (
            "Semver",
            Error::Semver(err_of(semver::Version::parse("x"), "semver")?),
            Want::Manifest,
        ),
        (
            "Minisign",
            Error::Minisign(err_of(
                minisign_verify::PublicKey::from_base64("x"),
                "minisign-verify",
            )?),
            Want::Signature,
        ),
        (
            "Http",
            Error::Http(err_of(
                http::Request::builder().uri("\u{0}").body(()),
                "http::Request",
            )?),
            Want::Install,
        ),
        (
            "InvalidHeaderValue",
            Error::InvalidHeaderValue(err_of(http::HeaderValue::from_str("\n"), "HeaderValue")?),
            Want::Install,
        ),
        (
            "InvalidHeaderName",
            Error::InvalidHeaderName(err_of(http::HeaderName::from_bytes(b" "), "HeaderName")?),
            Want::Install,
        ),
        (
            "Tauri",
            Error::Tauri(tauri::Error::UnknownPath),
            Want::Install,
        ),
    ])
}

/// `Ok` when all 31 rows map as §3.2 says; else the first row that does not, `<variant> → <got>`.
pub fn run() -> Result<(), String> {
    let rows = rows()?;
    if rows.len() != ROWS {
        return Err(format!("table has {} rows, not {ROWS}", rows.len()));
    }
    for (name, e, want) in &rows {
        let mapped = map_error(e);
        if got(&mapped) != *want {
            return Err(format!("{name} → {} ({:?})", mapped.code(), got(&mapped)));
        }
    }
    Ok(())
}
