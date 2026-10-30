//! macOS default roots (D8 macOS column).

use super::{APP_IDENTIFIER, Os, OsEnv, Platform, joined, required_abs};
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, RootsError, STAGING_DIR};
use std::path::PathBuf;

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
}

#[cfg(test)]
#[path = "macos_tests.rs"]
mod tests;
