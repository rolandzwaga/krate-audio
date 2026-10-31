//! Temp-sandboxed roots for tests (FR-020, FR-022). Compiled only under
//! `cfg(any(test, feature = "test-support"))`.

use crate::detect::{HostFacts, InnoValues, PkgOutputs, RecordRead};
use crate::feed::PlainName;
use crate::platform::{HostIo, RegistryRead, RunOutcome};
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, Roots, STAGING_DIR};
use crate::version::OsVersion;
use log::{Level, LevelFilter, Log, Metadata, Record};
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock, PoisonError};
use std::thread::{self, ThreadId};
use std::time::Duration;

#[derive(Debug, thiserror::Error)]
pub enum GuardError {
    #[error("cannot canonicalize {which}: {source}")]
    Canonicalize {
        which: &'static str,
        #[source]
        source: std::io::Error,
    },
    #[error("{} is not strictly under the temp root {}", .base.display(), .temp_root.display())]
    Outside { base: PathBuf, temp_root: PathBuf },
}

/// The plug-in roots `temp_roots_in` makes under `<base>/plugins` (D8 kinds; AU on macOS only).
const PLUGIN_DIRS: &[(RootKind, &str)] = &[
    (RootKind::Vst3User, "vst3-user"),
    (RootKind::Vst3Global, "vst3-global"),
    #[cfg(target_os = "macos")]
    (RootKind::AuUser, "au-user"),
    #[cfg(target_os = "macos")]
    (RootKind::AuGlobal, "au-global"),
];

/// `std::env::temp_dir()` canonicalized; panics if canonicalization fails.
pub fn canonical_temp_dir() -> PathBuf {
    #[expect(
        clippy::disallowed_methods,
        reason = "the one temp-dir read, for the test guard"
    )]
    let raw = std::env::temp_dir();
    std::fs::canonicalize(&raw)
        .unwrap_or_else(|e| panic!("cannot canonicalize the temp dir {}: {e}", raw.display()))
}

/// The canonical `base` when it is a strict descendant of the canonical `temp_root`.
/// Reads metadata only; creates nothing.
pub fn check_under_temp(base: &Path, temp_root: &Path) -> Result<PathBuf, GuardError> {
    let canonical_base =
        std::fs::canonicalize(base).map_err(|source| GuardError::Canonicalize {
            which: "base",
            source,
        })?;
    let canonical_temp =
        std::fs::canonicalize(temp_root).map_err(|source| GuardError::Canonicalize {
            which: "temp_root",
            source,
        })?;
    if canonical_base != canonical_temp && canonical_base.starts_with(&canonical_temp) {
        Ok(canonical_base)
    } else {
        Err(GuardError::Outside {
            base: canonical_base,
            temp_root: canonical_temp,
        })
    }
}

/// The guard first (panic before any directory exists), then every root as a subdirectory
/// of the canonical base, one `join` per component (PD-6), sandboxed to that base (PD-24).
fn temp_roots_in(base: &Path, temp_root: &Path) -> Roots {
    let canonical_base = match check_under_temp(base, temp_root) {
        Ok(b) => b,
        Err(e) => panic!(
            "temp_roots refused: base {} is not under temp root {}: {e}",
            base.display(),
            temp_root.display()
        ),
    };
    let plugins = canonical_base.join("plugins");
    let presets = canonical_base.join("presets");
    let state_dir = canonical_base.join("state");
    let paths = RootPaths {
        plugin_roots: PLUGIN_DIRS
            .iter()
            .map(|&(kind, dir)| PluginRoot {
                kind,
                path: plugins.join(dir),
            })
            .collect(),
        presets: PresetRoots {
            factory: presets.join("factory"),
            user: presets.join("user"),
        },
        staging_dir: state_dir.join(STAGING_DIR),
        state_dir,
    };
    #[expect(
        clippy::disallowed_methods,
        reason = "temp_roots is the test constructor"
    )]
    let roots = Roots::new(paths)
        .expect("temp_roots paths are valid by construction")
        .sandboxed(canonical_base);
    let dirs = roots
        .plugin_roots()
        .iter()
        .map(|r| r.path.as_path())
        .chain([
            roots.presets().factory.as_path(),
            roots.presets().user.as_path(),
            roots.state_dir(),
            roots.staging_dir(),
        ]);
    for dir in dirs {
        std::fs::create_dir_all(dir)
            .unwrap_or_else(|e| panic!("temp_roots cannot create {}: {e}", dir.display()));
    }
    roots
}

/// Roots under `base`, which must be a strict descendant of the system temp dir.
pub fn temp_roots(base: &Path) -> Roots {
    temp_roots_in(base, &canonical_temp_dir())
}

/// A new `TempDir` and `temp_roots` on it; keep the `TempDir` alive while the roots are used.
pub fn fresh_temp_roots() -> (tempfile::TempDir, Roots) {
    let td = tempfile::TempDir::new().expect("cannot create a TempDir");
    let roots = temp_roots(td.path());
    (td, roots)
}

/// The crate's `log` targets all start with this; the capture logger records only those.
const CAPTURE_TARGET_PREFIX: &str = "krate_manager_core";

/// One captured record per `log` call: (thread, target, level, message).
type CapturedRecord = (ThreadId, String, Level, String);

/// The shared capture store (FR-088). The only `log::set_logger` in the workspace's tests.
struct Capture {
    records: Mutex<Vec<CapturedRecord>>,
}

impl Log for Capture {
    fn enabled(&self, metadata: &Metadata) -> bool {
        metadata.target().starts_with(CAPTURE_TARGET_PREFIX)
    }

    fn log(&self, record: &Record) {
        if self.enabled(record.metadata()) {
            self.records
                .lock()
                .unwrap_or_else(PoisonError::into_inner)
                .push((
                    thread::current().id(),
                    record.target().to_owned(),
                    record.level(),
                    record.args().to_string(),
                ));
        }
    }

    fn flush(&self) {}
}

static CAPTURE: Capture = Capture {
    records: Mutex::new(Vec::new()),
};
static INSTALLED: OnceLock<()> = OnceLock::new();

/// Installs the one process-wide capture logger (FR-088); idempotent, safe from any number of
/// test threads at once.
pub fn install_capture() {
    INSTALLED.get_or_init(|| {
        log::set_logger(&CAPTURE).expect("the capture logger is the only logger a test installs");
        log::set_max_level(LevelFilter::Trace);
    });
}

/// The calling thread's captured `(target, level, message)` records, removed from the store.
pub fn take_mine() -> Vec<(String, Level, String)> {
    let me = thread::current().id();
    let mut all = CAPTURE
        .records
        .lock()
        .unwrap_or_else(PoisonError::into_inner);
    let (mine, rest): (Vec<_>, Vec<_>) = all.drain(..).partition(|r| r.0 == me);
    *all = rest;
    mine.into_iter()
        .map(|(_, target, level, message)| (target, level, message))
        .collect()
}

/// The test `HostFacts` (FR-083): records every call, then answers from its maps; an unlisted key is `Absent`.
/// Starts no process and reads no registry; `RefCell` keeps it per thread.
#[derive(Default)]
pub struct FakeHostFacts {
    pub os_version: Option<OsVersion>,
    pub inno: BTreeMap<String, RecordRead<InnoValues>>,
    pub pkg: BTreeMap<String, RecordRead<PkgOutputs>>,
    /// `os_version`, `inno_values <key>`, `pkg_outputs <id>`, in call order.
    pub calls: RefCell<Vec<String>>,
}

impl HostFacts for FakeHostFacts {
    fn os_version(&self) -> Option<OsVersion> {
        self.calls.borrow_mut().push("os_version".to_owned());
        self.os_version
    }

    fn inno_values(&self, key: &PlainName) -> RecordRead<InnoValues> {
        self.calls
            .borrow_mut()
            .push(format!("inno_values {}", key.as_str()));
        self.inno
            .get(key.as_str())
            .cloned()
            .unwrap_or(RecordRead::Absent)
    }

    fn pkg_outputs(&self, pkg_id: &PlainName) -> RecordRead<PkgOutputs> {
        self.calls
            .borrow_mut()
            .push(format!("pkg_outputs {}", pkg_id.as_str()));
        self.pkg
            .get(pkg_id.as_str())
            .cloned()
            .unwrap_or(RecordRead::Absent)
    }
}

/// The test `HostIo` (FR-083): records every call, then answers from its maps; an unlisted argv is `NoOutput`,
/// an unlisted subkey `NoKey`. Starts no process and reads no registry; `RefCell` keeps it per thread.
#[derive(Default)]
pub struct FakeHostIo {
    pub runs: BTreeMap<Vec<String>, RunOutcome>,
    pub registry: BTreeMap<String, RegistryRead>,
    pub rtl: Option<(u32, u32, u32)>,
    /// `run <argv joined by ' '>`, `registry <subkey> <n1> <n2>`, `rtl_version`, in call order.
    pub calls: RefCell<Vec<String>>,
}

impl HostIo for FakeHostIo {
    fn run(&self, argv: &[&str], _bound: Duration) -> RunOutcome {
        self.calls
            .borrow_mut()
            .push(format!("run {}", argv.join(" ")));
        let key: Vec<String> = argv.iter().map(|arg| (*arg).to_owned()).collect();
        self.runs.get(&key).cloned().unwrap_or(RunOutcome::NoOutput)
    }

    fn registry_values(&self, subkey: &str, names: [&str; 2]) -> RegistryRead {
        let [first, second] = names;
        self.calls
            .borrow_mut()
            .push(format!("registry {subkey} {first} {second}"));
        self.registry
            .get(subkey)
            .cloned()
            .unwrap_or(RegistryRead::NoKey)
    }

    fn rtl_version(&self) -> Option<(u32, u32, u32)> {
        self.calls.borrow_mut().push("rtl_version".to_owned());
        self.rtl
    }
}

#[cfg(test)]
#[path = "test_support_tests.rs"]
mod tests;
