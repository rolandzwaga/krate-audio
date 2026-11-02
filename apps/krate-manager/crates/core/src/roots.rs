//! Validated plug-in, preset, state and staging roots (FR-013, FR-102). Pure: nothing here
//! touches the filesystem.

use std::path::{Component, Path, PathBuf};

pub const STAGING_DIR: &str = "staging";
pub const LOGS_DIR: &str = "logs";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RootKind {
    Configured,
    Vst3User,
    Vst3Global,
    AuUser,
    AuGlobal,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PluginRoot {
    pub kind: RootKind,
    pub path: PathBuf,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PresetRoots {
    pub factory: PathBuf,
    pub user: PathBuf,
}

/// Unvalidated input to `Roots::new` (public fields; the only way in is `Roots::new`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RootPaths {
    pub plugin_roots: Vec<PluginRoot>,
    pub presets: PresetRoots,
    pub state_dir: PathBuf,
    pub staging_dir: PathBuf,
}

#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum RootsError {
    #[error("environment variable {var} is missing, empty, relative or not normalised")]
    Env { var: &'static str },
    #[error("root {which} is not an absolute path")]
    Relative { which: &'static str },
    #[error("root {which} has a `..` component or names a filesystem root")]
    NotNormal { which: &'static str },
    #[error("no {kind:?} plug-in root")]
    MissingRoot { kind: RootKind },
    #[error("staging_dir is not state_dir/{STAGING_DIR}")]
    Staging,
}

/// Validated roots. No `Default`, no `From`, no public field.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Roots {
    plugin_roots: Vec<PluginRoot>,
    presets: PresetRoots,
    state_dir: PathBuf,
    staging_dir: PathBuf,
    /// PD-24: `None` in production; the canonical temp base for test roots.
    sandbox: Option<PathBuf>,
}

/// No `..` component and at least one normal component (PD-26). Lexical only.
fn is_normal(path: &Path) -> bool {
    let mut has_normal = false;
    for component in path.components() {
        match component {
            Component::ParentDir => return false,
            Component::Normal(_) => has_normal = true,
            Component::Prefix(_) | Component::RootDir | Component::CurDir => {}
        }
    }
    has_normal
}

impl Roots {
    /// FR-013. Checks, in order: every path absolute, every path normal, one `Vst3User`
    /// and one `Vst3Global` root, `staging_dir == state_dir/staging`. Touches nothing.
    pub fn new(paths: RootPaths) -> Result<Roots, RootsError> {
        let named: Vec<(&'static str, &Path)> = paths
            .plugin_roots
            .iter()
            .map(|r| ("plugin_root", r.path.as_path()))
            .chain([
                ("presets.factory", paths.presets.factory.as_path()),
                ("presets.user", paths.presets.user.as_path()),
                ("state_dir", paths.state_dir.as_path()),
                ("staging_dir", paths.staging_dir.as_path()),
            ])
            .collect();
        if let Some(&(which, _)) = named.iter().find(|(_, p)| !p.is_absolute()) {
            return Err(RootsError::Relative { which });
        }
        if let Some(&(which, _)) = named.iter().find(|(_, p)| !is_normal(p)) {
            return Err(RootsError::NotNormal { which });
        }
        for kind in [RootKind::Vst3User, RootKind::Vst3Global] {
            if !paths.plugin_roots.iter().any(|r| r.kind == kind) {
                return Err(RootsError::MissingRoot { kind });
            }
        }
        if paths.staging_dir != paths.state_dir.join(STAGING_DIR) {
            return Err(RootsError::Staging);
        }
        Ok(Roots {
            plugin_roots: paths.plugin_roots,
            presets: paths.presets,
            state_dir: paths.state_dir,
            staging_dir: paths.staging_dir,
            sandbox: None,
        })
    }

    /// FR-102. A copy with any `Configured` entry removed and, for `Some(x)`, `x` placed
    /// first unless it equals a remaining entry's path. On a sandboxed value, panics when
    /// `x` is not a strict descendant of the sandbox base (PD-24).
    pub fn with_configured_plugin_root(&self, root: Option<&Path>) -> Result<Roots, RootsError> {
        let mut plugin_roots: Vec<PluginRoot> = self
            .plugin_roots
            .iter()
            .filter(|r| r.kind != RootKind::Configured)
            .cloned()
            .collect();
        if let Some(x) = root {
            if !x.is_absolute() {
                return Err(RootsError::Relative {
                    which: "configured",
                });
            }
            if !is_normal(x) {
                return Err(RootsError::NotNormal {
                    which: "configured",
                });
            }
            if let Some(base) = &self.sandbox
                && !(x != base.as_path() && x.starts_with(base))
            {
                panic!(
                    "configured plug-in root {} is outside the test sandbox {}",
                    x.display(),
                    base.display()
                );
            }
            if !plugin_roots.iter().any(|r| r.path == x) {
                plugin_roots.insert(
                    0,
                    PluginRoot {
                        kind: RootKind::Configured,
                        path: x.to_path_buf(),
                    },
                );
            }
        }
        Ok(Roots {
            plugin_roots,
            presets: self.presets.clone(),
            state_dir: self.state_dir.clone(),
            staging_dir: self.staging_dir.clone(),
            sandbox: self.sandbox.clone(),
        })
    }

    /// PD-24: called only by `test_support::temp_roots_in`, with the canonical base.
    #[cfg(any(test, feature = "test-support"))]
    pub(crate) fn sandboxed(self, base: PathBuf) -> Roots {
        Roots {
            sandbox: Some(base),
            ..self
        }
    }

    /// FR-005: on a sandboxed (test) `Roots`, panics unless `path` is a strict descendant of the sandbox base; on a
    /// production `Roots` (no sandbox) does nothing. Lexical, like `with_configured_plugin_root`.
    pub(crate) fn check_sandboxed(&self, path: &Path) {
        if let Some(base) = &self.sandbox
            && !(path != base.as_path() && path.starts_with(base))
        {
            panic!(
                "install location {} is outside the test sandbox {}",
                path.display(),
                base.display()
            );
        }
    }

    pub fn plugin_roots(&self) -> &[PluginRoot] {
        &self.plugin_roots
    }

    pub fn presets(&self) -> &PresetRoots {
        &self.presets
    }

    pub fn state_dir(&self) -> &Path {
        &self.state_dir
    }

    pub fn staging_dir(&self) -> &Path {
        &self.staging_dir
    }

    /// `state_dir/logs` (FR-104).
    pub fn logs_dir(&self) -> PathBuf {
        self.state_dir.join(LOGS_DIR)
    }

    /// The first `Vst3User` root; `new` guarantees one and `with_configured_plugin_root`
    /// never removes it.
    pub fn vst3_user(&self) -> &Path {
        self.plugin_roots
            .iter()
            .find(|r| r.kind == RootKind::Vst3User)
            .map(|r| r.path.as_path())
            .expect("Roots::new guarantees a Vst3User root")
    }

    /// The first `Vst3Global` root; `new` guarantees one and `with_configured_plugin_root`
    /// never removes it (Phase 10 FR-080).
    pub fn vst3_global(&self) -> &Path {
        self.plugin_roots
            .iter()
            .find(|r| r.kind == RootKind::Vst3Global)
            .map(|r| r.path.as_path())
            .expect("Roots::new guarantees a Vst3Global root")
    }
}

#[cfg(test)]
#[path = "roots_tests.rs"]
mod tests;
