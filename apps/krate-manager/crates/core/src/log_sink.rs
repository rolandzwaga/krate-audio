//! The log line format and the size-capped, two-generation log file (FR-104, D23; plan §4.7,
//! PD-10). The `log::Log` glue lives in `src-tauri`; this is the part that can be tested.

use crate::roots::Roots;
use std::fs::{File, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

pub const LOG_FILE: &str = "krate-manager.log";
pub const LOG_FILE_ROTATED: &str = "krate-manager.log.1";
pub const LOG_CAP_BYTES: u64 = 1024 * 1024;
pub const MAX_LINE_BYTES: usize = 4096;

/// `<unix_secs> <LEVEL> <target>: <message>` plus LF. Every control character in `message`
/// becomes a space, so a record is one line; the line, LF included, is at most
/// `MAX_LINE_BYTES` bytes, cut on a char boundary.
pub fn format_line(unix_secs: u64, level: log::Level, target: &str, message: &str) -> String {
    let message: String = message
        .chars()
        .map(|c| if c.is_control() { ' ' } else { c })
        .collect();
    let line = format!("{unix_secs} {level} {target}: {message}");
    let mut out = cut_at_boundary(&line, MAX_LINE_BYTES - 1).to_owned();
    out.push('\n');
    out
}

/// The longest prefix of `s` of at most `max` bytes that ends on a char boundary.
fn cut_at_boundary(s: &str, max: usize) -> &str {
    if s.len() <= max {
        return s;
    }
    let mut end = max;
    while !s.is_char_boundary(end) {
        end -= 1;
    }
    &s[..end]
}

fn open_append(path: &Path) -> io::Result<File> {
    OpenOptions::new().create(true).append(true).open(path)
}

/// `<logs dir>/krate-manager.log`, rotated once to `krate-manager.log.1` before a write that
/// would take it past `cap` bytes. No file is ever over the cap.
#[derive(Debug)]
pub struct RotatingLog {
    dir: PathBuf,
    file: Option<File>,
    size: u64,
    cap: u64,
}

impl RotatingLog {
    /// `open_with_cap(roots, LOG_CAP_BYTES)`.
    pub fn open(roots: &Roots) -> io::Result<RotatingLog> {
        Self::open_with_cap(roots, LOG_CAP_BYTES)
    }

    /// Creates `roots.logs_dir()` if absent and opens the log in append mode; the current
    /// size comes from the file.
    pub fn open_with_cap(roots: &Roots, cap_bytes: u64) -> io::Result<RotatingLog> {
        let dir = roots.logs_dir();
        std::fs::create_dir_all(&dir)?;
        let file = open_append(&dir.join(LOG_FILE))?;
        let size = file.metadata()?.len();
        Ok(RotatingLog {
            dir,
            file: Some(file),
            size,
            cap: cap_bytes,
        })
    }

    /// Writes one line (PD-10): a line over the cap is cut to fit (LF kept); a write that
    /// would take a non-empty log past the cap first renames it over `.1`. A failed rename
    /// writes nothing and returns the rename error.
    pub fn write_line(&mut self, line: &str) -> io::Result<()> {
        let cut;
        let line = if line.len() as u64 > self.cap {
            let max = usize::try_from(self.cap.saturating_sub(1)).unwrap_or(usize::MAX);
            let body = line.strip_suffix('\n').unwrap_or(line);
            cut = format!("{}\n", cut_at_boundary(body, max));
            cut.as_str()
        } else {
            line
        };
        let len = line.len() as u64;
        let log = self.dir.join(LOG_FILE);

        if self.size > 0 && self.size + len > self.cap {
            // Close the handle first: Windows cannot rename a file this process holds open.
            self.file = None;
            let renamed = std::fs::rename(&log, self.dir.join(LOG_FILE_ROTATED));
            let file = self.file.insert(open_append(&log)?);
            match renamed {
                Ok(()) => self.size = 0,
                Err(e) => {
                    self.size = file.metadata()?.len();
                    return Err(e);
                }
            }
        }

        let file = match self.file.take() {
            Some(file) => file,
            None => open_append(&log)?,
        };
        self.file.insert(file).write_all(line.as_bytes())?;
        self.size += len;
        Ok(())
    }
}

#[cfg(test)]
#[path = "log_sink_tests.rs"]
mod tests;
