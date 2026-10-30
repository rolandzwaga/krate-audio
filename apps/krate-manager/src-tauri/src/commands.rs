//! The app's two read-only commands (FR-040, FR-042).

use krate_manager_core::context::{SharedState, startup_notice};
use std::sync::PoisonError;

#[tauri::command]
pub fn app_version() -> &'static str {
    env!("CARGO_PKG_VERSION")
}

#[tauri::command]
pub fn startup_error(state: tauri::State<'_, SharedState>) -> Option<&'static str> {
    startup_notice(&state.read().unwrap_or_else(PoisonError::into_inner))
}
