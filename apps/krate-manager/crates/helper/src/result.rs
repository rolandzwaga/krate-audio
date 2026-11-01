//! The result codec (plan §4.1.5; FR-003, SI-11, SI-28).

use crate::seams::WriteSync;
use serde::Serialize;
use std::io;

/// The result file (FR-003); with `job_error` set, `ops` is empty (SI-11).
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct ResultFile {
    pub result_version: u32,
    pub job_sha256: String,
    pub job_error: Option<&'static str>,
    pub ops: Vec<OpEntry>,
}

/// One op's entry (FR-003).
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct OpEntry {
    pub index: u32,
    pub op: &'static str,
    pub outcome: OpOutcome,
    pub code: Option<&'static str>,
    pub exit_code: Option<i32>,
    pub seconds: f64,
    pub log_tail: Vec<String>,
}

/// An op's outcome (FR-003).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum OpOutcome {
    Done,
    Locked,
    Failed,
    Refused,
    NotRun,
}

/// FR-003: `log_tail` keeps at most this many lines.
const TAIL_LINES: usize = 40;
/// FR-003: each `log_tail` line holds at most this many bytes.
const TAIL_LINE_BYTES: usize = 512;

/// `serde_json::to_vec`, `write_all`, `sync_all` (FR-003).
pub fn write(out: &mut dyn WriteSync, file: &ResultFile) -> io::Result<()> {
    let bytes = serde_json::to_vec(file).map_err(io::Error::other)?;
    out.write_all(&bytes)?;
    out.sync_all()
}

/// The raw lines of `bytes`: split on `\n`, one trailing `\r` stripped; a final `\n` ends the last line and adds none.
fn raw_lines(bytes: &[u8]) -> Vec<&[u8]> {
    if bytes.is_empty() {
        return Vec::new();
    }
    let body = bytes.strip_suffix(b"\n").unwrap_or(bytes);
    body.split(|b| *b == b'\n')
        .map(|line| line.strip_suffix(b"\r").unwrap_or(line))
        .collect()
}

/// Every line of `bytes` (split on `\n`, a trailing `\r` stripped), lossy UTF-8: `classify`'s input (SI-28).
pub fn log_lines(bytes: &[u8]) -> Vec<String> {
    raw_lines(bytes)
        .into_iter()
        .map(|line| String::from_utf8_lossy(line).into_owned())
        .collect()
}

/// FR-003: the last 40 lines (empty lines counted), each cut to at most 512 bytes at a char boundary, lossy UTF-8.
pub fn log_tail(bytes: &[u8]) -> Vec<String> {
    let lines = raw_lines(bytes);
    let first = lines.len().saturating_sub(TAIL_LINES);
    lines[first..]
        .iter()
        .map(|line| {
            let mut s = String::from_utf8_lossy(line).into_owned();
            let mut end = s.len().min(TAIL_LINE_BYTES);
            while !s.is_char_boundary(end) {
                end -= 1;
            }
            s.truncate(end);
            s
        })
        .collect()
}

#[cfg(test)]
#[path = "result_tests.rs"]
mod tests;
