use super::*;
use crate::roots::{PluginRoot, PresetRoots, RootKind};
use std::ffi::OsStr;
use std::path::PathBuf;

const LOCAL: &str = r"C:\km-fixture\Users\u\AppData\Local";
const COMMON: &str = r"C:\km-fixture\Common Files";
const DATA: &str = r"C:\km-fixture\ProgramData";
const PROFILE: &str = r"C:\km-fixture\Users\u";

const NAMES: [&str; 4] = [
    "LOCALAPPDATA",
    "CommonProgramW6432",
    "ProgramData",
    "USERPROFILE",
];
const LOWER_NAMES: [&str; 4] = [
    "localappdata",
    "commonprogramw6432",
    "programdata",
    "userprofile",
];
const VALUES: [&str; 4] = [LOCAL, COMMON, DATA, PROFILE];

fn env_of(pairs: &[(&str, &str)]) -> OsEnv {
    OsEnv::from_pairs(pairs.iter().map(|&(k, v)| (k, OsStr::new(v))))
}

fn full(names: [&str; 4]) -> Vec<(&str, &str)> {
    names.into_iter().zip(VALUES).collect()
}

fn expected() -> RootPaths {
    RootPaths {
        plugin_roots: vec![
            PluginRoot {
                kind: RootKind::Vst3User,
                path: PathBuf::from(r"C:\km-fixture\Users\u\AppData\Local\Programs\Common\VST3"),
            },
            PluginRoot {
                kind: RootKind::Vst3Global,
                path: PathBuf::from(r"C:\km-fixture\Common Files\VST3"),
            },
        ],
        presets: PresetRoots {
            factory: PathBuf::from(r"C:\km-fixture\ProgramData\Krate Audio"),
            user: PathBuf::from(r"C:\km-fixture\Users\u\Documents\Krate Audio"),
        },
        state_dir: PathBuf::from(
            r"C:\km-fixture\Users\u\AppData\Local\com.krateaudio.krate-manager",
        ),
        staging_dir: PathBuf::from(
            r"C:\km-fixture\Users\u\AppData\Local\com.krateaudio.krate-manager\staging",
        ),
    }
}

#[test]
fn default_roots_from_env() {
    let p = WindowsPlatform;
    assert_eq!(p.os(), Os::Windows);
    assert_eq!(ENV_KEYS, NAMES);

    assert_eq!(p.default_roots(&env_of(&full(NAMES))), Ok(expected()));
    assert_eq!(p.default_roots(&env_of(&full(LOWER_NAMES))), Ok(expected()));

    assert_eq!(
        p.default_roots(&env_of(&[])),
        Err(RootsError::Env {
            var: "LOCALAPPDATA"
        })
    );

    for (i, var) in NAMES.into_iter().enumerate() {
        let mut missing = full(NAMES);
        missing.remove(i);
        assert_eq!(
            p.default_roots(&env_of(&missing)),
            Err(RootsError::Env { var }),
            "{var} missing"
        );
        for bad in ["", "Local"] {
            let mut pairs = full(NAMES);
            pairs[i].1 = bad;
            assert_eq!(
                p.default_roots(&env_of(&pairs)),
                Err(RootsError::Env { var }),
                "{var}={bad:?}"
            );
        }
    }

    let mut hop = full(NAMES);
    hop[0].1 = r"C:\km-fixture\..\x";
    assert_eq!(
        p.default_roots(&env_of(&hop)),
        Err(RootsError::Env {
            var: "LOCALAPPDATA"
        })
    );
}
