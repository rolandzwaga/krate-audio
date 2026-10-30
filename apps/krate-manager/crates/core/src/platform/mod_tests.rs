use super::*;
use crate::test_support::fresh_temp_roots;

/// Recursive listing of `dir`: (relative path, is_dir, size in bytes), sorted.
fn listing(dir: &Path) -> Vec<(PathBuf, bool, u64)> {
    fn walk(root: &Path, dir: &Path, out: &mut Vec<(PathBuf, bool, u64)>) {
        for entry in std::fs::read_dir(dir).expect("read_dir") {
            let entry = entry.expect("dir entry");
            let path = entry.path();
            let meta = std::fs::symlink_metadata(&path).expect("symlink_metadata");
            let rel = path.strip_prefix(root).expect("strip_prefix").to_path_buf();
            out.push((rel, meta.is_dir(), meta.len()));
            if meta.is_dir() {
                walk(root, &path, out);
            }
        }
    }
    let mut out = Vec::new();
    walk(dir, dir, &mut out);
    out.sort();
    out
}

#[cfg(windows)]
fn env_vars(env_dir: &Path) -> Vec<(&'static str, PathBuf)> {
    vec![
        ("LOCALAPPDATA", env_dir.join("LocalAppData")),
        ("CommonProgramW6432", env_dir.join("Common Files")),
        ("ProgramData", env_dir.join("ProgramData")),
        ("USERPROFILE", env_dir.join("UserProfile")),
    ]
}

#[cfg(not(windows))]
fn env_vars(env_dir: &Path) -> Vec<(&'static str, PathBuf)> {
    vec![
        ("HOME", env_dir.join("home")),
        ("XDG_DATA_HOME", env_dir.join("xdg")),
    ]
}

#[test]
fn default_roots_touches_nothing() {
    let (_td, roots) = fresh_temp_roots();
    let base = roots
        .state_dir()
        .parent()
        .expect("the state dir has a parent")
        .to_path_buf();
    let vars = env_vars(&base.join("env"));
    let env = OsEnv::from_pairs(vars.iter().map(|(k, v)| (*k, v.as_os_str())));
    for key in ENV_KEYS {
        assert!(env.get(key).is_some(), "{key} is set");
    }

    let before = listing(&base);
    let paths = NativePlatform
        .default_roots(&env)
        .expect("temp env values are accepted");
    assert_eq!(listing(&base), before);
    assert!(paths.state_dir.starts_with(&base), "{:?}", paths.state_dir);
    assert!(!paths.state_dir.exists());
}
