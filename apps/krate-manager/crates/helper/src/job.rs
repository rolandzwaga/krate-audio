//! The job codec (plan §4.1.4; FR-001, FR-002, FR-036; PD-2).

use crate::HelperOs;
use crate::digest;
use crate::error::{self, HelperError};
use serde::{Deserialize, Deserializer};

/// FR-023: the job read bound.
pub const MAX_JOB_BYTES: u64 = 64 * 1024;
/// FR-001: at most this many ops per job.
pub const MAX_OPS: usize = 32;

/// One op of the fixed set (FR-030 to FR-036).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Op {
    RunInnoInstaller { file: String, sha256: [u8; 32] },
    RunInnoUninstaller { app_id: String },
    RunPkgInstaller { file: String, sha256: [u8; 32] },
    UninstallPkg { pkg_id: String },
}

impl Op {
    /// The op's `op` value (FR-001).
    pub fn kind(&self) -> &'static str {
        match self {
            Op::RunInnoInstaller { .. } => "run_inno_installer",
            Op::RunInnoUninstaller { .. } => "run_inno_uninstaller",
            Op::RunPkgInstaller { .. } => "run_pkg_installer",
            Op::UninstallPkg { .. } => "uninstall_pkg",
        }
    }

    /// The OS the op runs on (FR-036): Inno ops Windows, pkg ops macOS.
    pub fn target(&self) -> HelperOs {
        match self {
            Op::RunInnoInstaller { .. } | Op::RunInnoUninstaller { .. } => HelperOs::Windows,
            Op::RunPkgInstaller { .. } | Op::UninstallPkg { .. } => HelperOs::Macos,
        }
    }
}

/// FR-001's key set per kind, for `job::tests::no_field_adds_a_root`.
pub const KEYS: [(&str, &[&str]); 4] = [
    ("run_inno_installer", &["op", "file", "sha256"]),
    ("run_inno_uninstaller", &["op", "app_id"]),
    ("run_pkg_installer", &["op", "file", "sha256"]),
    ("uninstall_pkg", &["op", "pkg_id"]),
];

/// The job as read (PD-2): no other key, none twice (serde_derive's `duplicate_field`).
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawJob {
    job_version: u64,
    ops: Vec<RawOp>,
}

/// One op as read (PD-2): every key any kind knows, each a `Slot`, so an explicit `null` stays visible.
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawOp {
    op: String,
    #[serde(default)]
    file: Slot,
    #[serde(default)]
    sha256: Slot,
    #[serde(default)]
    app_id: Slot,
    #[serde(default)]
    pkg_id: Slot,
}

/// A key's value: `Absent` when the key is missing, else `Present`, `null` included (PD-2).
#[derive(Default)]
enum Slot {
    #[default]
    Absent,
    Present(serde_json::Value),
}

impl<'de> Deserialize<'de> for Slot {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Slot, D::Error> {
        serde_json::Value::deserialize(deserializer).map(Slot::Present)
    }
}

impl Slot {
    /// The string a present key holds; `None` when absent, `null` or another type.
    fn string(self) -> Option<String> {
        match self {
            Slot::Present(serde_json::Value::String(s)) => Some(s),
            Slot::Absent | Slot::Present(_) => None,
        }
    }

    fn is_absent(&self) -> bool {
        matches!(self, Slot::Absent)
    }
}

/// FR-001: the job and each op are JSON objects. serde accepts the array form for a derived struct too
/// (`serde_json` `deserialize_struct` takes `[`, serde_derive emits `visit_seq`), which would fill the fields by
/// position, so the shape is checked on a `Value` first; the typed pass then sees every key, repeats included.
fn objects_only(bytes: &[u8]) -> Option<()> {
    let value: serde_json::Value = serde_json::from_slice(bytes).ok()?;
    let job = value.as_object()?;
    if let Some(serde_json::Value::Array(ops)) = job.get("ops")
        && !ops.iter().all(serde_json::Value::is_object)
    {
        return None;
    }
    Some(())
}

/// One op of `kind` with exactly its keys present, each a string (FR-001).
fn op(raw: RawOp) -> Option<Op> {
    let RawOp {
        op: kind,
        file,
        sha256,
        app_id,
        pkg_id,
    } = raw;
    let op = match kind.as_str() {
        "run_inno_installer" | "run_pkg_installer" => {
            if !(app_id.is_absent() && pkg_id.is_absent()) {
                return None;
            }
            let file = file.string()?;
            let sha256 = digest::parse_hex64(&sha256.string()?)?;
            if kind == "run_inno_installer" {
                Op::RunInnoInstaller { file, sha256 }
            } else {
                Op::RunPkgInstaller { file, sha256 }
            }
        }
        "run_inno_uninstaller" => {
            if !(file.is_absent() && sha256.is_absent() && pkg_id.is_absent()) {
                return None;
            }
            Op::RunInnoUninstaller {
                app_id: app_id.string()?,
            }
        }
        "uninstall_pkg" => {
            if !(file.is_absent() && sha256.is_absent() && app_id.is_absent()) {
                return None;
            }
            Op::UninstallPkg {
                pkg_id: pkg_id.string()?,
            }
        }
        _ => return None,
    };
    Some(op)
}

/// `parse` with every defect as `None`.
fn parse_ops(bytes: &[u8], os: HelperOs) -> Option<Vec<Op>> {
    objects_only(bytes)?;
    let raw: RawJob = serde_json::from_slice(bytes).ok()?;
    if raw.job_version != 1 || raw.ops.is_empty() || raw.ops.len() > MAX_OPS {
        return None;
    }
    let ops = raw.ops.into_iter().map(op).collect::<Option<Vec<Op>>>()?;
    // FR-001, FR-036: every op targets `os`, so `HelperOs::Linux` refuses every job.
    ops.iter().all(|op| op.target() == os).then_some(ops)
}

/// The job's ops for `os`, or `E_HELPER_JOB_PARSE` (FR-001, FR-002, FR-036; PD-2). The contents of `file`, `app_id`
/// and `pkg_id` are checked per op later (FR-020, FR-031, FR-033).
pub fn parse(bytes: &[u8], os: HelperOs) -> Result<Vec<Op>, HelperError> {
    parse_ops(bytes, os).ok_or(error::JOB_PARSE)
}

#[cfg(test)]
#[path = "job_tests.rs"]
mod tests;
