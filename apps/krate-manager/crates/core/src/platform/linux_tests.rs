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

/// The D8 Linux column for `HOME=/home/u`, with the state dir under `data_home`.
fn expected(data_home: &str) -> RootPaths {
    let state_dir = PathBuf::from(data_home).join("com.krateaudio.krate-manager");
    RootPaths {
        plugin_roots: vec![
            root(RootKind::Vst3User, "/home/u/.vst3"),
            root(RootKind::Vst3Global, "/usr/lib64/vst3"),
            root(RootKind::Vst3Global, "/usr/lib/vst3"),
            root(RootKind::Vst3Global, "/usr/local/lib64/vst3"),
            root(RootKind::Vst3Global, "/usr/local/lib/vst3"),
        ],
        presets: PresetRoots {
            factory: PathBuf::from("/home/u/Documents/Krate Audio"),
            user: PathBuf::from("/home/u/Documents/Krate Audio"),
        },
        staging_dir: state_dir.join("staging"),
        state_dir,
    }
}

#[test]
fn default_roots_from_env() {
    let p = LinuxPlatform;
    assert_eq!(p.os(), Os::Linux);
    assert_eq!(ENV_KEYS, ["HOME", "XDG_DATA_HOME"]);

    let fallback = expected("/home/u/.local/share");
    assert_eq!(
        fallback.state_dir,
        PathBuf::from("/home/u/.local/share/com.krateaudio.krate-manager")
    );
    assert_eq!(
        p.default_roots(&env_of(&[("HOME", "/home/u")])),
        Ok(fallback.clone())
    );

    let data = expected("/data");
    assert_eq!(
        data.state_dir,
        PathBuf::from("/data/com.krateaudio.krate-manager")
    );
    assert_eq!(
        p.default_roots(&env_of(&[("HOME", "/home/u"), ("XDG_DATA_HOME", "/data")])),
        Ok(data)
    );

    for xdg in ["", "rel/path", "/data/../x"] {
        assert_eq!(
            p.default_roots(&env_of(&[("HOME", "/home/u"), ("XDG_DATA_HOME", xdg)])),
            Ok(fallback.clone()),
            "XDG_DATA_HOME={xdg:?}"
        );
    }

    let home_err = Err(RootsError::Env { var: "HOME" });
    assert_eq!(p.default_roots(&env_of(&[])), home_err, "HOME missing");
    assert_eq!(
        p.default_roots(&env_of(&[("XDG_DATA_HOME", "/data")])),
        home_err,
        "HOME missing, XDG_DATA_HOME set"
    );
    for bad in ["", "home/u", "/home/u/../x"] {
        assert_eq!(
            p.default_roots(&env_of(&[("HOME", bad)])),
            home_err,
            "HOME={bad:?}"
        );
    }
    assert_eq!(
        p.default_roots(&env_of(&[("home", "/home/u")])),
        home_err,
        "names match exactly off Windows"
    );
    assert!(p.default_roots(&env_of(&[("HOME", "/")])).is_ok());
}
