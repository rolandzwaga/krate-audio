//! The Tauri shell: glue only (PD-1). Every decision lives in `krate_manager_core`.

mod commands;
#[cfg(feature = "dev-feed")]
mod dev_feed;
mod log_sink;
mod navigation;
pub mod os_elevation;
mod os_facts;
mod os_proxy;
pub mod os_roots;
mod proxy_env;
pub mod self_update;

use krate_manager_core::context::{
    LOCKED_EXIT_DELAY, SharedState, assemble_context, log_startup_errors, startup_notice,
};
#[cfg(not(feature = "dev-feed"))]
use krate_manager_core::http::HttpConfig;
use krate_manager_core::http::resolve_proxy;
use krate_manager_core::install::{self, FsInstallIo};
use krate_manager_core::platform::{NativePlatform, Platform};
use krate_manager_core::roots::Roots;
use krate_manager_core::verify::SigningKey;
use os_proxy::SystemProxyFromOs;
use os_roots::FromOs;
use std::path::Path;
use std::sync::Arc;

const FEED_URL: &str = "https://krateaudio.com/versions.json";

/// Phase 12 FR-088: a plugin page opens only on the production feed's host.
pub(crate) fn plugin_page_host() -> String {
    krate_manager_core::status::url_host(FEED_URL).unwrap_or_default()
}

/// Phase 10 PD-23: the one construction of `NativeElevation`, the helper resolved beside `exe` once (FR-090).
pub fn resolve_elevation(exe: Option<&Path>) -> os_elevation::NativeElevation {
    os_elevation::NativeElevation::new(install::helper_beside(
        NativePlatform.os(),
        exe,
        &FsInstallIo,
    ))
}

pub fn run() {
    #[expect(
        clippy::disallowed_methods,
        reason = "the one production Roots, built once at startup"
    )]
    let roots = Roots::from_os();
    // PD-25: the log sink takes `&Roots`.
    let sink_roots = roots.as_ref().ok().cloned();
    // Phase 12 FR-045, FR-046 (PD-12): the Linux graphics workaround from the stored settings, before any thread.
    #[cfg(target_os = "linux")]
    let loaded = roots
        .as_ref()
        .ok()
        .map(krate_manager_core::settings::load)
        .and_then(|o| o.error.is_none().then_some(o.settings));
    #[cfg(target_os = "linux")]
    krate_manager_core::platform::apply_graphics_env(
        NativePlatform.os(),
        loaded.as_ref(),
        |name, value| {
            // SAFETY: single-threaded: before the first thread (sysproxy.rs:210 can spawn one from :61)
            unsafe { std::env::set_var(name, value) }
        },
    );
    #[cfg(feature = "dev-feed")]
    let feed_url = roots
        .as_ref()
        .ok()
        .and_then(|r| dev_feed::read(r.state_dir()))
        .unwrap_or_else(|| FEED_URL.to_owned());
    #[cfg(not(feature = "dev-feed"))]
    let feed_url = FEED_URL.to_owned();
    // The OS proxy settings, resolved once per launch (FR-048, FR-061, FR-063).
    let inputs = proxy_env::read();
    let resolution = resolve_proxy(
        &inputs.env,
        &NativePlatform.system_proxy(&inputs),
        &feed_url,
    );
    #[cfg(not(feature = "dev-feed"))]
    let http = HttpConfig::production()
        .with_proxy(resolution.proxy)
        .with_pac(resolution.pac)
        .with_pac_host(Some(os_proxy::pac_host()));
    #[cfg(feature = "dev-feed")]
    let http = dev_feed::http_config(&feed_url, resolution, Some(os_proxy::pac_host()));
    #[cfg(feature = "updater-e2e")]
    let (feed_url, http) = self_update::e2e::override_feed(feed_url, http);
    let self_update_http = self_update::SelfUpdateHttp(http.clone());
    let state = assemble_context(roots, &feed_url, http, SigningKey::release());
    match &sink_roots {
        Some(r) => {
            log_sink::init(r);
            log_startup_errors(&state);
            #[cfg(feature = "dev-feed")]
            if feed_url != FEED_URL {
                log::warn!("dev-feed override: {feed_url}");
            }
        }
        None => {
            if let Err(e) = &state.context {
                eprintln!("{e}");
            }
        }
    }
    let locked = startup_notice(&state).is_some();
    tauri::Builder::default()
        .plugin(navigation::guard())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .manage(SharedState::new(state))
        .manage(Arc::new(resolve_elevation(
            std::env::current_exe().ok().as_deref(),
        )))
        .manage(self_update_http)
        .manage(self_update::KeptUpdate::default())
        .setup(move |app| {
            if locked {
                let handle = app.handle().clone();
                std::thread::spawn(move || {
                    std::thread::sleep(LOCKED_EXIT_DELAY);
                    handle.exit(1);
                });
            }
            #[cfg(feature = "updater-e2e")]
            self_update::e2e::start(app.handle());
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::app_version,
            commands::startup_error,
            commands::plugin_status,
            commands::check_now,
            commands::open_release_page,
            commands::download_plugin,
            commands::install_plugin,
            commands::remove_plugin,
            commands::manager_update_check,
            commands::manager_update_install,
            commands::update_all,
            commands::settings_view,
            commands::settings_save
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
