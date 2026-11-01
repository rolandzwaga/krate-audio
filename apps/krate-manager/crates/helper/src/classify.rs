//! The Inno outcome rule (plan §4.1.11; D10, FR-062).

/// What one Inno setup run did (D10).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InnoVerdict {
    Installed,
    Locked,
    Failed,
}

/// `(exit_code, every line of the bounded log tail)` → the verdict; until M-1 `0` → `Installed`, else `Failed`, never
/// `Locked`. The provisional rule reads no log line (`_log`); M-1's measured rule does (FR-062).
pub fn classify_inno(exit_code: i32, _log: &[String]) -> InnoVerdict {
    if exit_code == 0 {
        InnoVerdict::Installed
    } else {
        InnoVerdict::Failed
    }
}

#[cfg(test)]
#[path = "classify_tests.rs"]
mod tests;
