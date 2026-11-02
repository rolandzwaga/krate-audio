//! krate-manager-core: platform roots, settings, startup state (Tauri-free).

pub mod context;
pub mod contract;
pub mod detect;
pub mod download;
pub mod extract;
pub mod feed;
#[cfg(any(test, feature = "test-support"))]
pub mod fixture_server;
pub mod http;
pub mod install;
pub mod log_sink;
pub mod navigation;
pub mod notes;
pub mod pac;
pub mod platform;
pub mod roots;
pub mod self_update;
pub mod settings;
pub mod state_lock;
pub mod status;
pub mod sysproxy;
#[cfg(target_os = "macos")]
pub mod sysproxy_cf;
#[cfg(test)]
mod tar_builder_tests;
#[cfg(test)]
mod test_signer;
#[cfg(any(test, feature = "test-support"))]
pub mod test_support;
pub mod verify;
pub mod version;
