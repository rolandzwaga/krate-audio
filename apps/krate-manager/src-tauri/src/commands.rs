//! The app's commands (FR-040, FR-042; Phase 6 FR-060, FR-064; Phase 7 FR-060, FR-061; Phase 9 FR-096).

use crate::os_elevation::NativeElevation;
use crate::os_facts::OsHostIo;
use krate_manager_core::context::{SharedState, startup_notice};
use krate_manager_core::detect::PlatformFacts;
use krate_manager_core::download::{
    self, CancelToken, DownloadEnv, DownloadError, DownloadPolicy, DownloadSummary, FsStagingIo,
    Progress,
};
use krate_manager_core::extract::{ExtractEnv, ExtractLimits, FsExtractIo};
use krate_manager_core::feed::FetchMode;
use krate_manager_core::install::{self, ActionSummary, FsInstallIo, InstallProgress};
use krate_manager_core::status::{self, StatusView};
use std::sync::{Arc, PoisonError};
use std::time::{SystemTime, UNIX_EPOCH};

#[tauri::command]
pub fn app_version() -> &'static str {
    env!("CARGO_PKG_VERSION")
}

#[tauri::command]
pub fn startup_error(state: tauri::State<'_, SharedState>) -> Option<&'static str> {
    startup_notice(&state.read().unwrap_or_else(PoisonError::into_inner))
}

#[tauri::command]
pub async fn plugin_status(state: tauri::State<'_, SharedState>) -> Result<StatusView, String> {
    status_for(&state, FetchMode::Normal).await
}

#[tauri::command]
pub async fn check_now(state: tauri::State<'_, SharedState>) -> Result<StatusView, String> {
    status_for(&state, FetchMode::CheckNow).await
}

async fn status_for(state: &SharedState, mode: FetchMode) -> Result<StatusView, String> {
    let ctx = {
        let guard = state.read().unwrap_or_else(PoisonError::into_inner);
        guard.context.clone().map_err(|e| e.to_string())?
    }; // the guard is dropped before any await
    let now_unix = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    tauri::async_runtime::spawn_blocking(move || {
        status::build(&ctx, mode, now_unix, &PlatformFacts { io: &OsHostIo })
    })
    .await
    .map_err(|e| format!("E_STATUS_TASK: {e}"))
}

#[tauri::command]
pub fn open_release_page(url: String) -> Result<(), String> {
    let Some(valid) = status::release_page_url(&url) else {
        return Err("E_OPEN_REJECTED".to_owned());
    };
    tauri_plugin_opener::open_url(valid, None::<&str>).map_err(|e| format!("E_OPEN_FAILED: {e}"))
}

/// Download and verify one plugin's native asset into staging (Phase 7 FR-060): a thin wrapper over
/// `download::for_command`; progress goes to `on_progress`.
#[tauri::command]
pub async fn download_plugin(
    state: tauri::State<'_, SharedState>,
    name: String,
    expected_version: String,
    on_progress: tauri::ipc::Channel<Progress>,
) -> Result<DownloadSummary, String> {
    let ctx = {
        let guard = state.read().unwrap_or_else(PoisonError::into_inner);
        guard.context.clone().map_err(|e| e.to_string())?
    }; // the guard is dropped before any await
    let now_unix = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    tauri::async_runtime::spawn_blocking(move || {
        let policy = DownloadPolicy::production();
        let cancel = CancelToken::new(); // never set in Phase 7 (FR-017)
        let env = DownloadEnv {
            ctx: &ctx,
            io: &FsStagingIo,
            policy: &policy,
            cancel: &cancel,
        };
        download::for_command(
            env,
            &name,
            &expected_version,
            now_unix,
            &PlatformFacts { io: &OsHostIo },
            &mut download::channel_progress(|e| on_progress.send(e)),
        )
        .map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| {
        DownloadError::Task {
            detail: e.to_string(),
        }
        .to_string()
    })?
}

/// Install or update one plugin (Phase 9 FR-096): a thin wrapper over `install::for_command` with
/// `install::native_env`; progress goes to `on_progress`.
#[tauri::command]
pub async fn install_plugin(
    state: tauri::State<'_, SharedState>,
    name: String,
    expected_version: String,
    action: String,
    on_progress: tauri::ipc::Channel<InstallProgress>,
    elevation: tauri::State<'_, Arc<NativeElevation>>,
) -> Result<ActionSummary, String> {
    let elevation = Arc::clone(&elevation);
    let ctx = {
        let guard = state.read().unwrap_or_else(PoisonError::into_inner);
        guard.context.clone().map_err(|e| e.to_string())?
    }; // the guard is dropped before any await
    let now_unix = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    tauri::async_runtime::spawn_blocking(move || {
        let policy = DownloadPolicy::production();
        let cancel = CancelToken::new(); // never set in Phase 9
        let download = DownloadEnv {
            ctx: &ctx,
            io: &FsStagingIo,
            policy: &policy,
            cancel: &cancel,
        };
        let extract = ExtractEnv {
            ctx: &ctx,
            io: &FsExtractIo,
            limits: &ExtractLimits::PRODUCTION,
        };
        install::for_command(
            install::native_env(
                &ctx,
                &FsInstallIo,
                extract,
                download,
                &*elevation,
                &OsHostIo,
            ),
            &name,
            &expected_version,
            &action,
            now_unix,
            &PlatformFacts { io: &OsHostIo },
            &mut install::channel_progress(|e| on_progress.send(e)),
        )
        .map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| {
        DownloadError::Task {
            detail: e.to_string(),
        }
        .to_string()
    })?
}

/// Remove one plugin the manager installed (Phase 9 FR-096): a thin wrapper over
/// `install::remove_for_command` with `install::native_env`; progress goes to `on_progress`.
#[tauri::command]
pub async fn remove_plugin(
    state: tauri::State<'_, SharedState>,
    name: String,
    on_progress: tauri::ipc::Channel<InstallProgress>,
    elevation: tauri::State<'_, Arc<NativeElevation>>,
) -> Result<ActionSummary, String> {
    let elevation = Arc::clone(&elevation);
    let ctx = {
        let guard = state.read().unwrap_or_else(PoisonError::into_inner);
        guard.context.clone().map_err(|e| e.to_string())?
    }; // the guard is dropped before any await
    let now_unix = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    tauri::async_runtime::spawn_blocking(move || {
        let policy = DownloadPolicy::production();
        let cancel = CancelToken::new(); // never set in Phase 9
        let download = DownloadEnv {
            ctx: &ctx,
            io: &FsStagingIo,
            policy: &policy,
            cancel: &cancel,
        };
        let extract = ExtractEnv {
            ctx: &ctx,
            io: &FsExtractIo,
            limits: &ExtractLimits::PRODUCTION,
        };
        install::remove_for_command(
            install::native_env(
                &ctx,
                &FsInstallIo,
                extract,
                download,
                &*elevation,
                &OsHostIo,
            ),
            &name,
            now_unix,
            &PlatformFacts { io: &OsHostIo },
            &mut install::channel_progress(|e| on_progress.send(e)),
        )
        .map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| {
        DownloadError::Task {
            detail: e.to_string(),
        }
        .to_string()
    })?
}
