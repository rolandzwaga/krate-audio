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

fn graphics_settings(on: bool) -> Settings {
    Settings {
        linux_plugin_root: None,
        linux_graphics_workaround: on,
    }
}

/// SC-012: the pair only on Linux with the toggle on.
#[test]
fn graphics_env_cases() {
    let (on, off) = (graphics_settings(true), graphics_settings(false));
    assert_eq!(graphics_env(Os::Linux, &on), Some(GRAPHICS_ENV));
    assert_eq!(graphics_env(Os::Linux, &off), None);
    assert_eq!(graphics_env(Os::Windows, &on), None);
    assert_eq!(graphics_env(Os::Macos, &on), None);
}

/// SC-012: `set` is called once with the fixed pair on Linux with the toggle on, never otherwise.
#[test]
fn apply_graphics_env_cases() {
    const NONE: Vec<(String, String)> = Vec::new();
    let (on, off) = (graphics_settings(true), graphics_settings(false));
    let recorded = |os: Os, settings: Option<&Settings>| {
        let mut calls: Vec<(String, String)> = Vec::new();
        apply_graphics_env(os, settings, |name, value| {
            calls.push((name.to_owned(), value.to_owned()));
        });
        calls
    };
    assert_eq!(
        recorded(Os::Linux, Some(&on)),
        [("WEBKIT_DISABLE_DMABUF_RENDERER".to_owned(), "1".to_owned())]
    );
    assert_eq!(recorded(Os::Linux, Some(&off)), NONE);
    assert_eq!(recorded(Os::Linux, None), NONE);
    assert_eq!(recorded(Os::Windows, Some(&on)), NONE);
    assert_eq!(recorded(Os::Macos, Some(&on)), NONE);
}
