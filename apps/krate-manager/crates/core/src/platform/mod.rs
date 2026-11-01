//! The per-OS default roots (FR-010 to FR-012, D7-D9). Pure: every root is computed from an
//! injected `OsEnv`; nothing here reads the process environment or touches the filesystem.

use crate::detect::{InnoValues, PkgOutputs, RecordRead};
use crate::feed::PlainName;
use crate::roots::{RootPaths, RootsError};
use crate::version::OsVersion;
use std::ffi::{OsStr, OsString};
use std::path::{Component, Path, PathBuf};
use std::time::Duration;

/// The Tauri `identifier`, the last component of the state dir on every OS (D7).
pub const APP_IDENTIFIER: &str = "com.krateaudio.krate-manager";
/// The vendor folder of the Windows Inno layout (FR-002); the only spelling outside the default roots.
pub const VENDOR_DIR: &str = "Krate Audio";
/// `sw_vers` / `getconf` bound (D8).
pub const OS_VERSION_WAIT: Duration = Duration::from_secs(2);
/// Phase 10 FR-077: the one process signal, after a macOS install or update (cfg-free so every leg tests the driver).
pub const KILLALL_ARGV: [&str; 3] = ["/usr/bin/killall", "-9", "AudioComponentRegistrar"];

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

/// One bounded child run (FR-031). `NoOutput` folds spawn failure, non-zero exit and a read error (`run_bounded`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RunOutcome {
    Output(String),
    NoOutput,
    Abandoned,
}

/// Two values of one HKLM key, 64-bit view (FR-031).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RegistryRead {
    NoKey,
    Values([Option<String>; 2]),
    Failed,
}

/// The raw host primitives (D7 layer 1): no decision in any implementation.
pub trait HostIo {
    fn run(&self, argv: &[&str], bound: Duration) -> RunOutcome;
    fn registry_values(&self, subkey: &str, names: [&str; 2]) -> RegistryRead;
    fn rtl_version(&self) -> Option<(u32, u32, u32)>;
}

pub trait Platform {
    fn os(&self) -> Os;
    fn default_roots(&self, env: &OsEnv) -> Result<RootPaths, RootsError>;
    /// FR-030, FR-036.
    fn os_version(&self, io: &dyn HostIo) -> Option<OsVersion>;
    /// FR-030: Windows only; elsewhere `Absent` with no `io` call.
    fn inno_values(&self, io: &dyn HostIo, key: &PlainName) -> RecordRead<InnoValues>;
    /// FR-030: macOS only; elsewhere `Absent` with no `io` call.
    fn pkg_outputs(&self, io: &dyn HostIo, pkg_id: &PlainName) -> RecordRead<PkgOutputs>;
}

#[cfg(target_os = "windows")]
mod windows;
#[cfg(target_os = "windows")]
pub use windows::{
    DISPLAY_VERSION, ENV_KEYS, INSTALL_LOCATION, UNINSTALL_KEY, WindowsPlatform as NativePlatform,
};
#[cfg(target_os = "macos")]
mod macos;
#[cfg(target_os = "macos")]
pub use macos::{
    ENV_KEYS, MacosPlatform as NativePlatform, PKG_FILES_FLAG, PKG_INFO_FLAG, PKGUTIL,
    PKGUTIL_WAIT, SW_VERS, SW_VERS_FLAG,
};
#[cfg(target_os = "linux")]
mod linux;
#[cfg(target_os = "linux")]
pub use linux::{ENV_KEYS, GETCONF, GLIBC_NAME, LinuxPlatform as NativePlatform};

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
