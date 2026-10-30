//! krate-manager-core: platform roots, settings, startup state (Tauri-free).

pub mod context;
pub mod log_sink;
pub mod navigation;
pub mod platform;
pub mod roots;
pub mod settings;
pub mod state_lock;
#[cfg(any(test, feature = "test-support"))]
pub mod test_support;
