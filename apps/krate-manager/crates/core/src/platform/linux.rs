//! Linux default roots (D8 Linux column).

use super::{
    APP_IDENTIFIER, HostIo, OS_VERSION_WAIT, Os, OsEnv, Platform, abs_normal, joined, required_abs,
};
use crate::detect::{self, InnoValues, PkgOutputs, RecordRead};
use crate::feed::PlainName;
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, RootsError, STAGING_DIR};
use crate::version::OsVersion;
use std::path::PathBuf;

/// glibc's `getconf`, run by absolute path like the macOS tools (FR-033, SI-19).
pub const GETCONF: &str = "/usr/bin/getconf";
/// The `getconf` variable naming the glibc version (FR-030, FR-033).
pub const GLIBC_NAME: &str = "GNU_LIBC_VERSION";

const HOME: &str = "HOME";
const XDG_DATA_HOME: &str = "XDG_DATA_HOME";

/// The D9 Linux key list, in the order `default_roots` reads it (`XDG_DATA_HOME` optional).
pub const ENV_KEYS: &[&str] = &[HOME, XDG_DATA_HOME];

pub struct LinuxPlatform;

impl Platform for LinuxPlatform {
    fn os(&self) -> Os {
        Os::Linux
    }

    fn default_roots(&self, env: &OsEnv) -> Result<RootPaths, RootsError> {
        let home = required_abs(env, HOME)?;
        // XDG 0.8: a missing, empty or relative value is ignored; one with `..` likewise (PD-26).
        let data_home = env
            .get(XDG_DATA_HOME)
            .and_then(abs_normal)
            .unwrap_or_else(|| joined(&home, &[".local", "share"]));
        let state_dir = joined(&data_home, &[APP_IDENTIFIER]);
        let global = |path: &str| PluginRoot {
            kind: RootKind::Vst3Global,
            path: PathBuf::from(path),
        };
        Ok(RootPaths {
            plugin_roots: vec![
                PluginRoot {
                    kind: RootKind::Vst3User,
                    path: joined(&home, &[".vst3"]),
                },
                global("/usr/lib64/vst3"),
                global("/usr/lib/vst3"),
                global("/usr/local/lib64/vst3"),
                global("/usr/local/lib/vst3"),
            ],
            presets: PresetRoots {
                factory: joined(&home, &["Documents", "Krate Audio"]),
                user: joined(&home, &["Documents", "Krate Audio"]),
            },
            staging_dir: joined(&state_dir, &[STAGING_DIR]),
            state_dir,
        })
    }

    fn os_version(&self, io: &dyn HostIo) -> Option<OsVersion> {
        detect::os_version_from_run(
            io.run(&[GETCONF, GLIBC_NAME], OS_VERSION_WAIT),
            detect::parse_glibc_version,
        )
    }

    fn inno_values(&self, _io: &dyn HostIo, _key: &PlainName) -> RecordRead<InnoValues> {
        RecordRead::Absent
    }

    fn pkg_outputs(&self, _io: &dyn HostIo, _pkg_id: &PlainName) -> RecordRead<PkgOutputs> {
        RecordRead::Absent
    }
}

#[cfg(test)]
#[path = "linux_tests.rs"]
mod tests;
