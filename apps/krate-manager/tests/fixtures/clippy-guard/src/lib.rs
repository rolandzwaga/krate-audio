#[cfg(test)]
mod tests {
    use krate_manager_core::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, Roots};
    use krate_manager_lib::os_roots::FromOs;
    use std::env;
    use std::path::PathBuf;

    #[test]
    fn calls_from_os() { let _ = Roots::from_os(); }

    #[test]
    fn calls_roots_new() {
        let s = PathBuf::from("/s");
        let _ = Roots::new(RootPaths {
            plugin_roots: vec![PluginRoot { kind: RootKind::Vst3User, path: PathBuf::from("/u") },
                               PluginRoot { kind: RootKind::Vst3Global, path: PathBuf::from("/g") }],
            presets: PresetRoots { factory: PathBuf::from("/f"), user: PathBuf::from("/p") },
            staging_dir: s.join("staging"),
            state_dir: s,
        });
    }

    #[test]
    fn calls_env_var() { let _ = env::var("HOME"); }
}
