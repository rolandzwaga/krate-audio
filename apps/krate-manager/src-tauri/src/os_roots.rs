//! The production roots: the one process-environment read for roots (FR-014, D9).

use krate_manager_core::platform::{ENV_KEYS, NativePlatform, OsEnv, Platform};
use krate_manager_core::roots::{Roots, RootsError};
use std::ffi::OsString;

pub trait FromOs {
    fn from_os() -> Result<Roots, RootsError>;
}

impl FromOs for Roots {
    fn from_os() -> Result<Roots, RootsError> {
        let mut pairs: Vec<(&'static str, OsString)> = Vec::new();
        for key in ENV_KEYS {
            #[expect(
                clippy::disallowed_methods,
                reason = "the only process-environment read for roots (D9)"
            )]
            let value = std::env::var_os(key);
            if let Some(v) = value {
                pairs.push((*key, v));
            }
        }
        let env = OsEnv::from_pairs(pairs.iter().map(|(k, v)| (*k, v.as_os_str())));
        let paths = NativePlatform.default_roots(&env)?;
        #[expect(
            clippy::disallowed_methods,
            reason = "from_os is the production constructor"
        )]
        let roots = Roots::new(paths);
        roots
    }
}
