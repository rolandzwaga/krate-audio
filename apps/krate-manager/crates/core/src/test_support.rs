//! Temp-sandboxed roots for tests (FR-020, FR-022). Compiled only under
//! `cfg(any(test, feature = "test-support"))`.

use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, Roots, STAGING_DIR};
use std::path::{Path, PathBuf};

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

#[cfg(test)]
#[path = "test_support_tests.rs"]
mod tests;
