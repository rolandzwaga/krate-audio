//! Windows default roots (D8 Windows column).

use super::{APP_IDENTIFIER, HostIo, Os, OsEnv, Platform, joined, required_abs};
use crate::detect::{self, InnoValues, PkgOutputs, RecordRead};
use crate::feed::PlainName;
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, RootsError, STAGING_DIR};
use crate::version::OsVersion;

/// The HKLM parent of every Inno uninstall key, read in the 64-bit view (FR-030, FR-033).
pub const UNINSTALL_KEY: &str = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
/// The Inno `AppVersion` value name (FR-026, FR-033).
pub const DISPLAY_VERSION: &str = "DisplayVersion";
/// The Inno install-directory value name (FR-003, FR-033).
pub const INSTALL_LOCATION: &str = "InstallLocation";

const LOCALAPPDATA: &str = "LOCALAPPDATA";
const COMMON_PROGRAM_W6432: &str = "CommonProgramW6432";
const PROGRAM_DATA: &str = "ProgramData";
const USERPROFILE: &str = "USERPROFILE";

/// The D9 Windows key list, in the order `default_roots` reads it.
pub const ENV_KEYS: &[&str] = &[
    LOCALAPPDATA,
    COMMON_PROGRAM_W6432,
    PROGRAM_DATA,
    USERPROFILE,
];

pub struct WindowsPlatform;

impl Platform for WindowsPlatform {
    fn os(&self) -> Os {
        Os::Windows
    }

    fn default_roots(&self, env: &OsEnv) -> Result<RootPaths, RootsError> {
        let local = required_abs(env, LOCALAPPDATA)?;
        let common = required_abs(env, COMMON_PROGRAM_W6432)?;
        let program_data = required_abs(env, PROGRAM_DATA)?;
        let profile = required_abs(env, USERPROFILE)?;
        let state_dir = joined(&local, &[APP_IDENTIFIER]);
        Ok(RootPaths {
            plugin_roots: vec![
                PluginRoot {
                    kind: RootKind::Vst3User,
                    path: joined(&local, &["Programs", "Common", "VST3"]),
                },
                PluginRoot {
                    kind: RootKind::Vst3Global,
                    path: joined(&common, &["VST3"]),
                },
            ],
            presets: PresetRoots {
                factory: joined(&program_data, &["Krate Audio"]),
                user: joined(&profile, &["Documents", "Krate Audio"]),
            },
            staging_dir: joined(&state_dir, &[STAGING_DIR]),
            state_dir,
        })
    }

    fn os_version(&self, io: &dyn HostIo) -> Option<OsVersion> {
        io.rtl_version()
            .and_then(|(major, minor, build)| detect::windows_version(major, minor, build))
    }

    fn inno_values(&self, io: &dyn HostIo, key: &PlainName) -> RecordRead<InnoValues> {
        let mut subkey = String::with_capacity(UNINSTALL_KEY.len() + 1 + key.as_str().len());
        subkey.push_str(UNINSTALL_KEY);
        subkey.push('\\'); // a char literal: not a string literal of this file (PD-4)
        subkey.push_str(key.as_str());
        detect::inno_read_from(io.registry_values(&subkey, [DISPLAY_VERSION, INSTALL_LOCATION]))
    }

    fn pkg_outputs(&self, _io: &dyn HostIo, _pkg_id: &PlainName) -> RecordRead<PkgOutputs> {
        RecordRead::Absent
    }
}

#[cfg(test)]
#[path = "windows_tests.rs"]
mod tests;
