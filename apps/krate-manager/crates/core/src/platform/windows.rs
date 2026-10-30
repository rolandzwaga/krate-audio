//! Windows default roots (D8 Windows column).

use super::{APP_IDENTIFIER, Os, OsEnv, Platform, joined, required_abs};
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, RootsError, STAGING_DIR};

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
}

#[cfg(test)]
#[path = "windows_tests.rs"]
mod tests;
