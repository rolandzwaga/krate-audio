//! The Inno outcome rule (plan §4.1.11; D10, FR-062).

/// What one Inno setup run did (D10).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InnoVerdict {
    Installed,
    Locked,
    Failed,
}

/// The exit code Inno Setup 6.7.1 returned when a plug-in file was held open (M-1: Manager CI run 41342540379,
/// `/SUPPRESSMSGBOXES` defaulting the Abort/Retry/Ignore box to Abort).
pub const LOCKED_EXIT: i32 = 5;
/// The fixed part of the file-in-use line that run logged (`tests/fixtures/helper/inno-locked/locked_line.txt`).
pub const LOCKED_LINE: &str = "DeleteFile: The existing file appears to be in use (5).";

/// `(exit_code, every line of the bounded log tail)` → the verdict: `0` → `Installed`; `Locked` only for the recorded
/// exit code with the recorded line in the log; every other code `Failed` (FR-062: an Inno that logs differently is a
/// plain failure, never `Locked`).
pub fn classify_inno(exit_code: i32, log: &[String]) -> InnoVerdict {
    if exit_code == 0 {
        InnoVerdict::Installed
    } else if exit_code == LOCKED_EXIT && log.iter().any(|l| l.contains(LOCKED_LINE)) {
        InnoVerdict::Locked
    } else {
        InnoVerdict::Failed
    }
}

#[cfg(test)]
#[path = "classify_tests.rs"]
mod tests;
