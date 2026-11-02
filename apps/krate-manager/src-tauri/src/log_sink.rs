//! The `log::Log` glue over core's `RotatingLog` (FR-105, PD-25).

use krate_manager_core::log_sink::{RotatingLog, format_line};
use krate_manager_core::roots::Roots;
use log::{Level, LevelFilter, Log, Metadata, Record};
use std::sync::{Mutex, OnceLock, PoisonError};
use std::time::{SystemTime, UNIX_EPOCH};

struct FileLogger {
    inner: Mutex<RotatingLog>,
}

impl Log for FileLogger {
    fn enabled(&self, metadata: &Metadata<'_>) -> bool {
        metadata.level() <= Level::Info
    }

    fn log(&self, record: &Record<'_>) {
        let line = format_line(
            now_secs(),
            record.level(),
            record.target(),
            &record.args().to_string(),
        );
        let mut log = self.inner.lock().unwrap_or_else(PoisonError::into_inner);
        let _ = log.write_line(&line);
    }

    fn flush(&self) {}
}

static LOGGER: OnceLock<FileLogger> = OnceLock::new();

fn now_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs())
}

/// Opens `<logs dir>/krate-manager.log` and installs the logger at Info; any error leaves
/// logging off.
pub fn init(roots: &Roots) {
    let Ok(log) = RotatingLog::open(roots) else {
        return;
    };
    let logger = LOGGER.get_or_init(|| FileLogger {
        inner: Mutex::new(log),
    });
    if log::set_logger(logger).is_ok() {
        log::set_max_level(LevelFilter::Info);
    }
}
