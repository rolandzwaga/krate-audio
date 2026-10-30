//! Linux default roots (D8 Linux column).

use super::{APP_IDENTIFIER, Os, OsEnv, Platform, abs_normal, joined, required_abs};
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, RootsError, STAGING_DIR};
use std::path::PathBuf;

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
}

#[cfg(test)]
#[path = "linux_tests.rs"]
mod tests;
