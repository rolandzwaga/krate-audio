//! The per-OS default roots (FR-010 to FR-012, D7-D9). Pure: every root is computed from an
//! injected `OsEnv`; nothing here reads the process environment or touches the filesystem.

use crate::roots::{RootPaths, RootsError};
use std::ffi::{OsStr, OsString};
use std::path::{Component, Path, PathBuf};

/// The Tauri `identifier`, the last component of the state dir on every OS (D7).
pub const APP_IDENTIFIER: &str = "com.krateaudio.krate-manager";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Os {
    Windows,
    Macos,
    Linux,
}

/// Process-environment snapshot; built from literal pairs in tests and by `FromOs` in production.
#[derive(Clone, Debug, Default)]
pub struct OsEnv {
    pairs: Vec<(String, OsString)>,
}

impl OsEnv {
    pub fn from_pairs<'a>(pairs: impl IntoIterator<Item = (&'a str, &'a OsStr)>) -> Self {
        OsEnv {
            pairs: pairs
                .into_iter()
                .map(|(name, value)| (name.to_owned(), value.to_owned()))
                .collect(),
        }
    }

    /// Windows: ASCII-case-insensitive name match; elsewhere exact. A name given twice
    /// resolves to its last value, as in a map built from the pairs.
    pub fn get(&self, name: &str) -> Option<&OsStr> {
        self.pairs
            .iter()
            .rev()
            .find(|(key, _)| name_matches(key, name))
            .map(|(_, value)| value.as_os_str())
    }
}

#[cfg(windows)]
fn name_matches(key: &str, name: &str) -> bool {
    key.eq_ignore_ascii_case(name)
}

#[cfg(not(windows))]
fn name_matches(key: &str, name: &str) -> bool {
    key == name
}

pub trait Platform {
    fn os(&self) -> Os;
    fn default_roots(&self, env: &OsEnv) -> Result<RootPaths, RootsError>;
}

#[cfg(target_os = "windows")]
mod windows;
#[cfg(target_os = "windows")]
pub use windows::{ENV_KEYS, WindowsPlatform as NativePlatform};
#[cfg(target_os = "macos")]
mod macos;
#[cfg(target_os = "macos")]
pub use macos::{ENV_KEYS, MacosPlatform as NativePlatform};
#[cfg(target_os = "linux")]
mod linux;
#[cfg(target_os = "linux")]
pub use linux::{ENV_KEYS, LinuxPlatform as NativePlatform};

/// The value as a path when it is absolute and has no `Component::ParentDir` (an empty
/// value is not absolute). Lexical only.
fn abs_normal(value: &OsStr) -> Option<PathBuf> {
    let path = Path::new(value);
    let normal = !path.components().any(|c| c == Component::ParentDir);
    (path.is_absolute() && normal).then(|| path.to_path_buf())
}

/// Missing, empty, relative, or with a `Component::ParentDir` → `RootsError::Env { var }` (PD-26).
/// Pure: `Path::is_absolute` and `Path::components` are lexical.
fn required_abs(env: &OsEnv, var: &'static str) -> Result<PathBuf, RootsError> {
    env.get(var)
        .and_then(abs_normal)
        .ok_or(RootsError::Env { var })
}

/// `base` with each component pushed in turn (FR-011, PD-6).
fn joined(base: &Path, components: &[&str]) -> PathBuf {
    let mut path = base.to_path_buf();
    for component in components {
        path.push(component);
    }
    path
}

#[cfg(test)]
#[path = "mod_tests.rs"]
mod tests;
