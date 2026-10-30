//! The app's own origin and the navigation origin check (FR-044, D12; plan §4.9). Tauri-free
//! (D12a): `src-tauri` passes Tauri's `&Url` (the same `url::Url`) straight in.

use crate::platform::Os;
use url::Url;

/// The Vite dev server the debug build loads.
pub const DEV_URL: &str = "http://localhost:1420";

/// The origin the webview serves the app from: `DEV_URL` in dev mode, otherwise Tauri's
/// custom-protocol origin for `os`.
pub fn app_origin(dev: bool, os: Os) -> Url {
    let text = if dev {
        DEV_URL
    } else {
        match os {
            Os::Windows => "http://tauri.localhost",
            Os::Macos | Os::Linux => "tauri://localhost",
        }
    };
    Url::parse(text).expect("constant URL")
}

/// True when `target` has the same scheme, host and port (known defaults filled in) as `app`.
/// `tauri://localhost` has no known default port, so both sides compare as `None`.
pub fn is_app_origin(target: &Url, app: &Url) -> bool {
    target.scheme() == app.scheme()
        && target.host_str() == app.host_str()
        && target.port_or_known_default() == app.port_or_known_default()
}

#[cfg(test)]
#[path = "navigation_tests.rs"]
mod tests;
