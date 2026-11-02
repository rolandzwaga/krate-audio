//! The proxy environment (FR-048, FR-063): the only proxy-variable read in the app. It only copies values; the
//! merge and its empty-as-unset rule are core's (`ProxyEnv::from_vars`).

use krate_manager_core::http::{ProxyEnv, ProxyVars};
#[cfg(target_os = "linux")]
use std::path::PathBuf;

const NAMES: &[&str] = &[
    "HTTPS_PROXY",
    "https_proxy",
    "ALL_PROXY",
    "all_proxy",
    "NO_PROXY",
    "no_proxy",
    #[cfg(target_os = "linux")]
    "XDG_CURRENT_DESKTOP",
    #[cfg(target_os = "linux")]
    "XDG_CONFIG_HOME",
    #[cfg(target_os = "linux")]
    "HOME",
];

/// Safe to log: `ProxyEnv`'s `Debug` redacts (PD-19).
#[derive(Debug)]
pub struct ProxyInputs {
    pub env: ProxyEnv,
    #[cfg(target_os = "linux")]
    pub desktop: Option<String>,
    #[cfg(target_os = "linux")]
    pub config_home: Option<PathBuf>,
    #[cfg(target_os = "linux")]
    pub home: Option<PathBuf>,
}

pub fn read() -> ProxyInputs {
    let mut values: Vec<(&str, Option<String>)> = Vec::with_capacity(NAMES.len());
    for name in NAMES {
        #[expect(
            clippy::disallowed_methods,
            reason = "the proxy environment, read once at startup (FR-048, FR-063)"
        )]
        let value = std::env::var_os(name);
        // A value that is not UTF-8 is stored as unset.
        values.push((*name, value.and_then(|v| v.into_string().ok())));
    }
    let got = |name: &str| {
        values
            .iter()
            .find(|(n, _)| *n == name)
            .and_then(|(_, v)| v.clone())
    };
    let vars = ProxyVars {
        https_upper: got("HTTPS_PROXY"),
        https_lower: got("https_proxy"),
        all_upper: got("ALL_PROXY"),
        all_lower: got("all_proxy"),
        no_upper: got("NO_PROXY"),
        no_lower: got("no_proxy"),
    };
    ProxyInputs {
        env: ProxyEnv::from_vars(&vars),
        #[cfg(target_os = "linux")]
        desktop: got("XDG_CURRENT_DESKTOP"),
        #[cfg(target_os = "linux")]
        config_home: got("XDG_CONFIG_HOME").map(PathBuf::from),
        #[cfg(target_os = "linux")]
        home: got("HOME").map(PathBuf::from),
    }
}
