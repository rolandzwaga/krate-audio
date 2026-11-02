//! FR-081 (b), (e), SI-29: the per-child redirected directories, the recorded-writes file, the parent's walk, and
//! the Windows known-folder snapshot (the only file naming `dirs::`).

use std::collections::BTreeSet;
use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

/// The file each child appends every path it wrote to, inside its redirected temp dir.
const WRITES_FILE: &str = "writes.txt";
/// The redirected temp dir's name inside a scenario dir.
const TMP_DIR: &str = "tmp";

/// FR-081 (b): every per-user directory variable and the subdirectory of `dir` it points at.
const REDIRECTED: [(&str, &str); 12] = [
    ("LOCALAPPDATA", "local"),
    ("APPDATA", "roaming"),
    ("USERPROFILE", "home"),
    ("HOME", "home"),
    ("TMP", TMP_DIR),
    ("TEMP", TMP_DIR),
    ("TMPDIR", TMP_DIR),
    ("XDG_CONFIG_HOME", "xdg-config"),
    ("XDG_DATA_HOME", "xdg-data"),
    ("XDG_CACHE_HOME", "xdg-cache"),
    ("XDG_STATE_HOME", "xdg-state"),
    ("XDG_RUNTIME_DIR", "run"),
];

/// Creates `dir` and its redirected subdirectories; the child's environment overrides.
pub fn prepare(dir: &Path) -> io::Result<Vec<(&'static str, PathBuf)>> {
    let mut envs = Vec::with_capacity(REDIRECTED.len());
    for (var, sub) in REDIRECTED {
        let path = dir.join(sub);
        fs::create_dir_all(&path)?;
        envs.push((var, path));
    }
    Ok(envs)
}

/// A child records one path it wrote (its canonical form) in its own temp dir's writes file.
pub fn record(path: &Path) {
    let file = krate_manager_core::test_support::canonical_temp_dir().join(WRITES_FILE);
    let line = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    let mut f = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&file)
        .unwrap_or_else(|e| panic!("cannot open {}: {e}", file.display()));
    writeln!(f, "{}", line.display())
        .unwrap_or_else(|e| panic!("cannot write {}: {e}", file.display()));
}

/// SI-29: the entry names, read only, of the manager's per-user folders the OS resolves (Windows known folders).
#[cfg(windows)]
pub fn snapshot() -> BTreeSet<PathBuf> {
    use krate_manager_core::platform::APP_IDENTIFIER;
    let mut out = BTreeSet::new();
    for base in [dirs::data_local_dir(), dirs::data_dir()]
        .into_iter()
        .flatten()
    {
        let dir = base.join(APP_IDENTIFIER);
        if let Ok(entries) = fs::read_dir(&dir) {
            out.extend(entries.flatten().map(|e| e.path()));
        }
    }
    out
}

/// SI-29: the known-folder snapshot is Windows-only; elsewhere every default path follows the redirected variables.
#[cfg(not(windows))]
pub fn snapshot() -> BTreeSet<PathBuf> {
    BTreeSet::new()
}

/// Walks `dir` without following links; the first path whose real location is outside `canon`.
fn walk(dir: &Path, canon: &Path) -> Result<(), String> {
    let Ok(entries) = fs::read_dir(dir) else {
        return Ok(());
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let real =
            fs::canonicalize(&path).map_err(|_| format!("{} (unresolvable)", path.display()))?;
        if !real.starts_with(canon) {
            return Err(real.display().to_string());
        }
        let Ok(kind) = entry.file_type() else {
            return Err(format!("{} (no file type)", path.display()));
        };
        if kind.is_dir() {
            walk(&path, canon)?;
        }
    }
    Ok(())
}

/// FR-081 (e): every file under each scenario dir and every recorded write is under `canon`, and no entry was added
/// to the Windows known folders. The first offending path.
pub fn check(canon: &Path, dirs: &[PathBuf], before: &BTreeSet<PathBuf>) -> Result<(), String> {
    for dir in dirs {
        walk(dir, canon)?;
        let writes = dir.join(TMP_DIR).join(WRITES_FILE);
        let text =
            fs::read_to_string(&writes).map_err(|_| format!("{} (missing)", writes.display()))?;
        for line in text.lines().filter(|l| !l.is_empty()) {
            if !Path::new(line).starts_with(canon) {
                return Err(line.to_owned());
            }
        }
    }
    let after = snapshot();
    match after.difference(before).next() {
        Some(added) => Err(added.display().to_string()),
        None => Ok(()),
    }
}
