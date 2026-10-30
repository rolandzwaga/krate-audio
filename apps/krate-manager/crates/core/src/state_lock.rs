//! Single instance by an exclusive lock on `<state>/manager.lock` (FR-100, D22; plan §4.6).

use crate::roots::Roots;
use std::io;

pub const LOCK_FILE: &str = "manager.lock";

/// The IO step an acquire failed at.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LockStep {
    CreateDir,
    Open,
    Lock,
}

impl LockStep {
    pub fn as_str(self) -> &'static str {
        match self {
            LockStep::CreateDir => "create-dir",
            LockStep::Open => "open",
            LockStep::Lock => "lock",
        }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum LockError {
    #[error("the state lock is held by another instance")]
    Held,
    #[error("state lock failed at {}: {source}", .op.as_str())]
    Io {
        op: LockStep,
        #[source]
        source: io::Error,
    },
}

/// Holds the lock for as long as it lives. No `Drop` impl: closing the `File` releases the
/// lock, and the lock file is never written or deleted (deleting a lock file races).
#[derive(Debug)]
pub struct StateLock {
    _file: std::fs::File,
}

fn at(op: LockStep) -> impl FnOnce(io::Error) -> LockError {
    move |source| LockError::Io { op, source }
}

impl StateLock {
    /// Creates `roots.state_dir()` if absent, opens `manager.lock` (create, read + write,
    /// no truncate; not append-only, which Windows refuses to lock) and takes an exclusive
    /// `try_lock`. Already held, by this or another process → `Held`.
    pub fn acquire(roots: &Roots) -> Result<StateLock, LockError> {
        let dir = roots.state_dir();
        std::fs::create_dir_all(dir).map_err(at(LockStep::CreateDir))?;
        let file = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(dir.join(LOCK_FILE))
            .map_err(at(LockStep::Open))?;
        match file.try_lock() {
            Ok(()) => Ok(StateLock { _file: file }),
            Err(std::fs::TryLockError::WouldBlock) => Err(LockError::Held),
            Err(std::fs::TryLockError::Error(source)) => Err(LockError::Io {
                op: LockStep::Lock,
                source,
            }),
        }
    }
}

#[cfg(test)]
#[path = "state_lock_tests.rs"]
mod tests;
