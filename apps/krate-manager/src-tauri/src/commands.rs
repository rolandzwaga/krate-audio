//! The app's commands (FR-040, FR-042; Phase 6 FR-060, FR-064; Phase 7 FR-060, FR-061; Phase 9 FR-096;
//! Phase 11 FR-020; Phase 12 FR-060, FR-061).

use crate::os_elevation::NativeElevation;
use crate::os_facts::OsHostIo;
use crate::self_update::{self, NativeUpdateInstaller, SelfUpdateHttp, UpdateInputs};
use krate_manager_core::context::{SharedState, startup_notice};
use krate_manager_core::detect::PlatformFacts;
use krate_manager_core::download::{
    self, CancelToken, DownloadEnv, DownloadError, DownloadPolicy, DownloadSummary, FsStagingIo,
    Progress,
};
use krate_manager_core::extract::{ExtractEnv, ExtractLimits, FsExtractIo};
use krate_manager_core::feed::FetchMode;
use krate_manager_core::install::{
    self, ActionSummary, BatchItem, BatchPolicy, BatchProgress, BatchSummaryView, FsInstallIo,
    InstallProgress,
};
use krate_manager_core::self_update::{
    ManagerUpdateView, SelfUpdateProgress, self_update_busy, self_update_http,
};
use krate_manager_core::settings::{self, SettingsView};
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
    let Some(valid) = status::open_url(&url) else {
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
    let (ticket, ctx) = settings::command_context(&state).map_err(|e| e.to_string())?;
    let now_unix = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    tauri::async_runtime::spawn_blocking(move || {
        let _ticket = ticket; // held until the core call returns (PD-11)
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
    let (ticket, ctx) = settings::command_context(&state).map_err(|e| e.to_string())?;
    let now_unix = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    tauri::async_runtime::spawn_blocking(move || {
        let _ticket = ticket; // held until the core call returns (PD-11)
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

/// Update every listed plugin in one batch (Phase 12 FR-001, FR-060): a thin wrapper over
/// `install::batch_for_command` with `install::native_env`; progress goes to `on_progress`.
#[tauri::command]
pub async fn update_all(
    state: tauri::State<'_, SharedState>,
    items: Vec<BatchItem>,
    on_progress: tauri::ipc::Channel<BatchProgress>,
    elevation: tauri::State<'_, Arc<NativeElevation>>,
) -> Result<BatchSummaryView, String> {
    let elevation = Arc::clone(&elevation);
    let (ticket, ctx) = settings::command_context(&state).map_err(|e| e.to_string())?;
    let now_unix = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    tauri::async_runtime::spawn_blocking(move || {
        let _ticket = ticket; // held until the core call returns (PD-11)
        let policy = DownloadPolicy::production();
        let cancel = CancelToken::new(); // never set (Phase 12 D15: no cancel)
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
        install::batch_for_command(
            install::native_env(
                &ctx,
                &FsInstallIo,
                extract,
                download,
                &*elevation,
                &OsHostIo,
            ),
            &FsStagingIo,
            &BatchPolicy::PRODUCTION,
            &items,
            now_unix,
            &PlatformFacts { io: &OsHostIo },
            &mut install::batch_channel_progress(|e| on_progress.send(e)),
        )
        .map(|summary| summary.view())
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

/// The settings section's view (Phase 12 FR-040, SI-20): `async`, calling the core inline (a guard read only).
#[tauri::command]
pub async fn settings_view(state: tauri::State<'_, SharedState>) -> Result<SettingsView, String> {
    settings::view(&state).map_err(|e| e.to_string())
}

/// Save the Linux settings (Phase 12 FR-041 to FR-043, SI-20): `async`, calling the core inline (it holds the
/// state's write guard, PD-9).
#[tauri::command]
pub async fn settings_save(
    state: tauri::State<'_, SharedState>,
    linux_plugin_root: Option<String>,
    graphics_workaround: bool,
) -> Result<SettingsView, String> {
    settings::save_for_command(&state, linux_plugin_root, graphics_workaround)
        .map_err(|e| e.to_string())
}

/// Check for a Krate Manager update (Phase 11 FR-020, D7): a thin wrapper over `self_update::check`; with no
/// startup context the self-update fallback `HttpConfig` is used (PD-9, FR-026).
#[tauri::command]
pub async fn manager_update_check(
    app: tauri::AppHandle,
    http: tauri::State<'_, SelfUpdateHttp>,
    state: tauri::State<'_, SharedState>,
) -> Result<ManagerUpdateView, String> {
    let ctx = {
        let guard = state.read().unwrap_or_else(PoisonError::into_inner);
        guard.context.clone().ok()
    }; // the guard is dropped before any await
    let inputs = UpdateInputs::production(self_update_http(ctx.as_ref(), &http.0));
    Ok(self_update::check(&app, &inputs).await)
}

/// Install the kept Krate Manager update (Phase 11 FR-020, D7): a thin wrapper over `self_update::install`;
/// progress goes to `on_progress`, a failure is the serialized `ErrorView` (tasks TA-21).
#[tauri::command]
pub async fn manager_update_install(
    app: tauri::AppHandle,
    http: tauri::State<'_, SelfUpdateHttp>,
    state: tauri::State<'_, SharedState>,
    elevation: tauri::State<'_, Arc<NativeElevation>>,
    on_progress: tauri::ipc::Channel<SelfUpdateProgress>,
) -> Result<(), String> {
    let elevation = Arc::clone(&elevation);
    let ctx = {
        let guard = state.read().unwrap_or_else(PoisonError::into_inner);
        guard.context.clone().ok()
    }; // the guard is dropped before any await
    let inputs = UpdateInputs::production(self_update_http(ctx.as_ref(), &http.0));
    let busy = || self_update_busy(&*elevation, ctx.as_ref());
    let state_dir = ctx.as_ref().map(|c| c.roots.state_dir());
    let progress = |p| {
        let _ = on_progress.send(p);
    };
    self_update::install(
        &app,
        &inputs,
        &NativeUpdateInstaller,
        &busy,
        state_dir,
        &progress,
    )
    .await
    .map_err(|e| serde_json::to_string(&e.view()).unwrap_or_else(|_| e.to_string()))
}
