//! The Tauri shell: glue only (PD-1). Every decision lives in `krate_manager_core`.

mod commands;
#[cfg(feature = "dev-feed")]
mod dev_feed;
mod log_sink;
mod navigation;
pub mod os_roots;

use krate_manager_core::context::{
    LOCKED_EXIT_DELAY, SharedState, assemble_context, log_startup_errors, startup_notice,
};
use krate_manager_core::roots::Roots;
use os_roots::FromOs;

const FEED_URL: &str = "https://krateaudio.com/versions.json";

pub fn run() {
    #[expect(
        clippy::disallowed_methods,
        reason = "the one production Roots, built once at startup"
    )]
    let roots = Roots::from_os();
    // PD-25: the log sink takes `&Roots`.
    let sink_roots = roots.as_ref().ok().cloned();
    #[cfg(feature = "dev-feed")]
    let feed_url = roots
        .as_ref()
        .ok()
        .and_then(|r| dev_feed::read(r.state_dir()))
        .unwrap_or_else(|| FEED_URL.to_owned());
    #[cfg(not(feature = "dev-feed"))]
    let feed_url = FEED_URL.to_owned();
    let state = assemble_context(roots, &feed_url);
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
        .manage(SharedState::new(state))
        .setup(move |app| {
            if locked {
                let handle = app.handle().clone();
                std::thread::spawn(move || {
                    std::thread::sleep(LOCKED_EXIT_DELAY);
                    handle.exit(1);
                });
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::app_version,
            commands::startup_error
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
