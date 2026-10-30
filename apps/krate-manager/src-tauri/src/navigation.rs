//! The navigation guard (FR-044): the webview may navigate only within the app's own origin.

use krate_manager_core::navigation::{app_origin, is_app_origin};
use krate_manager_core::platform::{NativePlatform, Platform};

pub fn guard<R: tauri::Runtime>() -> tauri::plugin::TauriPlugin<R> {
    tauri::plugin::Builder::new("navigation-guard")
        .on_navigation(|_webview, url| {
            is_app_origin(url, &app_origin(tauri::is_dev(), NativePlatform.os()))
        })
        .build()
}
