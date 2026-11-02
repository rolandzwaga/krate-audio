use super::*;
use crate::roots::{PluginRoot, PresetRoots, RootKind};
use std::ffi::OsStr;
use std::path::PathBuf;

fn env_of(pairs: &[(&str, &str)]) -> OsEnv {
    OsEnv::from_pairs(pairs.iter().map(|&(k, v)| (k, OsStr::new(v))))
}

fn root(kind: RootKind, path: &str) -> PluginRoot {
    PluginRoot {
        kind,
        path: PathBuf::from(path),
    }
}

fn expected() -> RootPaths {
    RootPaths {
        plugin_roots: vec![
            root(RootKind::Vst3User, "/Users/u/Library/Audio/Plug-Ins/VST3"),
            root(RootKind::Vst3Global, "/Library/Audio/Plug-Ins/VST3"),
            root(
                RootKind::AuUser,
                "/Users/u/Library/Audio/Plug-Ins/Components",
            ),
            root(RootKind::AuGlobal, "/Library/Audio/Plug-Ins/Components"),
        ],
        presets: PresetRoots {
            factory: PathBuf::from("/Library/Application Support/Krate Audio"),
            user: PathBuf::from("/Users/u/Documents/Krate Audio"),
        },
        state_dir: PathBuf::from(
            "/Users/u/Library/Application Support/com.krateaudio.krate-manager",
        ),
        staging_dir: PathBuf::from(
            "/Users/u/Library/Application Support/com.krateaudio.krate-manager/staging",
        ),
    }
}

#[test]
fn default_roots_from_env() {
    let p = MacosPlatform;
    assert_eq!(p.os(), Os::Macos);
    assert_eq!(ENV_KEYS, ["HOME"]);

    assert_eq!(
        p.default_roots(&env_of(&[("HOME", "/Users/u")])),
        Ok(expected())
    );

    let home_err = Err(RootsError::Env { var: "HOME" });
    assert_eq!(p.default_roots(&env_of(&[])), home_err, "HOME missing");
    for bad in ["", "Users/u", "/Users/u/../x"] {
        assert_eq!(
            p.default_roots(&env_of(&[("HOME", bad)])),
            home_err,
            "HOME={bad:?}"
        );
    }
    assert_eq!(
        p.default_roots(&env_of(&[("home", "/Users/u")])),
        home_err,
        "names match exactly off Windows"
    );
    assert!(p.default_roots(&env_of(&[("HOME", "/")])).is_ok());
}
