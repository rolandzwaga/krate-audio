//! The helper's codes (plan §3.1); the core keeps its own copy, cross-checked through `CODES` (PD-11).

/// One helper code; no path, no free text.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct HelperError {
    pub code: &'static str,
}

/// A known-folder lookup failed, or a root has no existing ancestor / a non-plain missing component (FR-026, FR-052).
pub const ROOTS: HelperError = HelperError {
    code: "E_HELPER_ROOTS",
};
/// FR-021, FR-025.
pub const JOB_LOCATION: HelperError = HelperError {
    code: "E_HELPER_JOB_LOCATION",
};
/// The result name exists, a dangling link included (FR-003).
pub const RESULT_EXISTS: HelperError = HelperError {
    code: "E_HELPER_RESULT_EXISTS",
};
/// The result file cannot be created or written for another reason (SI-12).
pub const RESULT_WRITE: HelperError = HelperError {
    code: "E_HELPER_RESULT_WRITE",
};
/// FR-023.
pub const JOB_READ: HelperError = HelperError {
    code: "E_HELPER_JOB_READ",
};
/// FR-005, D2.
pub const JOB_HASH: HelperError = HelperError {
    code: "E_HELPER_JOB_HASH",
};
/// FR-001, FR-002, FR-036.
pub const JOB_PARSE: HelperError = HelperError {
    code: "E_HELPER_JOB_PARSE",
};
/// FR-020.
pub const SOURCE: HelperError = HelperError {
    code: "E_HELPER_SOURCE",
};
/// FR-031.
pub const APP_ID: HelperError = HelperError {
    code: "E_HELPER_APP_ID",
};
/// FR-031.
pub const UNINSTALLER: HelperError = HelperError {
    code: "E_HELPER_UNINSTALLER",
};
/// FR-033.
pub const PKG_ID: HelperError = HelperError {
    code: "E_HELPER_PKG_ID",
};
/// FR-033, FR-034.
pub const PKG_PATH: HelperError = HelperError {
    code: "E_HELPER_PKG_PATH",
};
/// D7, FR-030.
pub const RUN_DIR: HelperError = HelperError {
    code: "E_HELPER_RUN_DIR",
};
/// FR-030 step 2.
pub const COPY: HelperError = HelperError {
    code: "E_HELPER_COPY",
};
/// FR-030 step 2, FR-035.
pub const HASH: HelperError = HelperError {
    code: "E_HELPER_HASH",
};
/// An installer, uninstaller or `pkgutil` could not be started (SI-12).
pub const SPAWN: HelperError = HelperError {
    code: "E_HELPER_SPAWN",
};
/// FR-030 to FR-032.
pub const EXIT: HelperError = HelperError {
    code: "E_HELPER_EXIT",
};
/// FR-042.
pub const TIMEOUT: HelperError = HelperError {
    code: "E_HELPER_TIMEOUT",
};
/// FR-031.
pub const NO_KEY: HelperError = HelperError {
    code: "E_HELPER_NO_KEY",
};
/// FR-031, D6, PD-24.
pub const UNINSTALL_TIMEOUT: HelperError = HelperError {
    code: "E_HELPER_UNINSTALL_TIMEOUT",
};
/// FR-033.
pub const PKG_RECEIPT: HelperError = HelperError {
    code: "E_HELPER_PKG_RECEIPT",
};
/// FR-033.
pub const REMOVE: HelperError = HelperError {
    code: "E_HELPER_REMOVE",
};
/// FR-033.
pub const FORGET: HelperError = HelperError {
    code: "E_HELPER_FORGET",
};
/// `classify` → `Locked` (D10).
pub const LOCKED: HelperError = HelperError {
    code: "E_HELPER_LOCKED",
};
/// Every op after a timed-out installer (FR-042).
pub const AFTER_TIMEOUT: HelperError = HelperError {
    code: "E_HELPER_AFTER_TIMEOUT",
};

/// Every code above, for the core's cross-check (PD-11, SC-011).
pub const CODES: [&str; 25] = [
    ROOTS.code,
    JOB_LOCATION.code,
    RESULT_EXISTS.code,
    RESULT_WRITE.code,
    JOB_READ.code,
    JOB_HASH.code,
    JOB_PARSE.code,
    SOURCE.code,
    APP_ID.code,
    UNINSTALLER.code,
    PKG_ID.code,
    PKG_PATH.code,
    RUN_DIR.code,
    COPY.code,
    HASH.code,
    SPAWN.code,
    EXIT.code,
    TIMEOUT.code,
    NO_KEY.code,
    UNINSTALL_TIMEOUT.code,
    PKG_RECEIPT.code,
    REMOVE.code,
    FORGET.code,
    LOCKED.code,
    AFTER_TIMEOUT.code,
];
