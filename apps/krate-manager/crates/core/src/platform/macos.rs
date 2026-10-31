//! macOS default roots (D8 macOS column).

use super::{APP_IDENTIFIER, HostIo, OS_VERSION_WAIT, Os, OsEnv, Platform, joined, required_abs};
use crate::detect::{self, InnoValues, PkgOutputs, RecordRead};
use crate::feed::PlainName;
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, RootsError, STAGING_DIR};
use crate::version::OsVersion;
use std::path::PathBuf;
use std::time::Duration;

/// The base-system receipt tool, run by absolute path (FR-030, FR-033).
pub const PKGUTIL: &str = "/usr/sbin/pkgutil";
pub const PKG_INFO_FLAG: &str = "--pkg-info";
pub const PKG_FILES_FLAG: &str = "--files";
/// The OS version tool, run by absolute path (FR-030, FR-033).
pub const SW_VERS: &str = "/usr/bin/sw_vers";
pub const SW_VERS_FLAG: &str = "-productVersion";
/// The bound of each `pkgutil` run (FR-030, PD-5).
pub const PKGUTIL_WAIT: Duration = Duration::from_secs(5);

const HOME: &str = "HOME";

/// The D9 macOS key list.
pub const ENV_KEYS: &[&str] = &[HOME];

pub struct MacosPlatform;

impl Platform for MacosPlatform {
    fn os(&self) -> Os {
        Os::Macos
    }

    fn default_roots(&self, env: &OsEnv) -> Result<RootPaths, RootsError> {
        let home = required_abs(env, HOME)?;
        let state_dir = joined(&home, &["Library", "Application Support", APP_IDENTIFIER]);
        Ok(RootPaths {
            plugin_roots: vec![
                PluginRoot {
                    kind: RootKind::Vst3User,
                    path: joined(&home, &["Library", "Audio", "Plug-Ins", "VST3"]),
                },
                PluginRoot {
                    kind: RootKind::Vst3Global,
                    path: PathBuf::from("/Library/Audio/Plug-Ins/VST3"),
                },
                PluginRoot {
                    kind: RootKind::AuUser,
                    path: joined(&home, &["Library", "Audio", "Plug-Ins", "Components"]),
                },
                PluginRoot {
                    kind: RootKind::AuGlobal,
                    path: PathBuf::from("/Library/Audio/Plug-Ins/Components"),
                },
            ],
            presets: PresetRoots {
                factory: PathBuf::from("/Library/Application Support/Krate Audio"),
                user: joined(&home, &["Documents", "Krate Audio"]),
            },
            staging_dir: joined(&state_dir, &[STAGING_DIR]),
            state_dir,
        })
    }

    fn os_version(&self, io: &dyn HostIo) -> Option<OsVersion> {
        detect::os_version_from_run(
            io.run(&[SW_VERS, SW_VERS_FLAG], OS_VERSION_WAIT),
            detect::parse_product_version,
        )
    }

    fn inno_values(&self, _io: &dyn HostIo, _key: &PlainName) -> RecordRead<InnoValues> {
        RecordRead::Absent
    }

    fn pkg_outputs(&self, io: &dyn HostIo, pkg_id: &PlainName) -> RecordRead<PkgOutputs> {
        // Its own argv element (no shell); it matched the contract's macos_pkg_id pattern at feed parse.
        let id = pkg_id.as_str();
        detect::pkg_read_from(io.run(&[PKGUTIL, PKG_INFO_FLAG, id], PKGUTIL_WAIT), || {
            io.run(&[PKGUTIL, PKG_FILES_FLAG, id], PKGUTIL_WAIT)
        })
    }
}

#[cfg(test)]
#[path = "macos_tests.rs"]
mod tests;
