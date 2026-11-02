//! Startup assembly, the locked instance, live reassembly, startup logging and the dev-feed
//! parser (FR-017, FR-101 to FR-104, FR-106, D17, D22; plan §4.8).

use crate::extract::{ExtractEnv, ExtractLimits, FsExtractIo};
use crate::http::HttpConfig;
use crate::install;
use crate::platform::{NativePlatform, Os, Platform};
use crate::roots::{Roots, RootsError};
use crate::settings::{self, Settings, SettingsError};
use crate::state_lock::{LockError, LockStep, StateLock};
use crate::verify::SigningKey;
use std::io;
use std::sync::{Arc, PoisonError};
use std::time::Duration;

/// The text a refused second instance shows (FR-101).
pub const LOCKED_NOTICE: &str = "Krate Manager is already running";
/// How long a refused instance stays open before it exits by itself (FR-101).
pub const LOCKED_EXIT_DELAY: Duration = Duration::from_secs(5);
/// The `log` target of `log_startup_errors` (FR-104).
pub const STARTUP_LOG_TARGET: &str = "krate_manager_core::startup";

/// The longest dev-feed URL `parse_dev_feed_url` accepts, in bytes (FR-106).
const MAX_DEV_FEED_URL_BYTES: usize = 2048;

/// Why startup has no `AppContext` (FR-017). `Clone` so `reassemble` can return an `Err`
/// context unchanged (PD-3).
#[derive(Clone, Debug, thiserror::Error)]
pub enum StartupError {
    #[error("E_STARTUP_ROOTS: {0}")]
    Roots(#[source] RootsError),
    #[error("E_STARTUP_LOCKED: {LOCKED_NOTICE}")]
    Locked,
    #[error("E_STARTUP_LOCK_IO: state lock failed at {}: {source}", .op.as_str())]
    LockIo {
        op: LockStep,
        source: Arc<io::Error>,
    },
}

impl StartupError {
    pub fn code(&self) -> &'static str {
        match self {
            StartupError::Roots(_) => "E_STARTUP_ROOTS",
            StartupError::Locked => "E_STARTUP_LOCKED",
            StartupError::LockIo { .. } => "E_STARTUP_LOCK_IO",
        }
    }
}

/// The one injection point (D17): later phases add fields here.
#[derive(Clone, Debug, PartialEq)]
pub struct AppContext {
    pub roots: Roots,
    pub settings: Settings,
    pub feed_url: String,
    /// The one HTTP configuration every request of this instance uses (FR-060).
    pub http: HttpConfig,
    /// The key every downloaded artifact is verified against (Phase 7 FR-041): the release key
    /// in production, a throwaway or the KAT key in tests.
    pub signing_key: SigningKey,
}

/// What `src-tauri` manages (FR-017, FR-103). The lock lives as long as the state.
#[derive(Debug)]
pub struct AppState {
    pub context: Result<AppContext, StartupError>,
    pub settings_error: Option<SettingsError>,
    pub lock: Option<Arc<StateLock>>,
}

pub type SharedState = std::sync::RwLock<AppState>;

/// FR-017 on this OS.
pub fn assemble_context(
    roots: Result<Roots, RootsError>,
    feed_url: &str,
    http: HttpConfig,
    signing_key: SigningKey,
) -> AppState {
    assemble_with_os(NativePlatform.os(), roots, feed_url, http, signing_key)
}

/// Lock, then load, then (Linux only) fold the configured root, then return. Creates only
/// the state dir (if absent) and `manager.lock`; a refused lock loads nothing.
pub(crate) fn assemble_with_os(
    os: Os,
    roots: Result<Roots, RootsError>,
    feed_url: &str,
    http: HttpConfig,
    signing_key: SigningKey,
) -> AppState {
    let roots = match roots {
        Ok(roots) => roots,
        Err(e) => return failed(StartupError::Roots(e)),
    };
    let lock = match StateLock::acquire(&roots) {
        Ok(lock) => lock,
        Err(LockError::Held) => return failed(StartupError::Locked),
        Err(LockError::Io { op, source }) => {
            return failed(StartupError::LockIo {
                op,
                source: Arc::new(source),
            });
        }
    };
    let outcome = settings::load(&roots);
    let roots = fold(os, &roots, &outcome.settings);
    let ctx = AppContext {
        roots,
        settings: outcome.settings,
        feed_url: feed_url.to_owned(),
        http,
        signing_key,
    };
    // Phase 9 FR-080: the install recovery pass, once, on every OS; its report is logged, the state unchanged.
    let report = install::recover(
        &ctx,
        &install::FsInstallIo,
        ExtractEnv {
            ctx: &ctx,
            io: &FsExtractIo,
            limits: &ExtractLimits::PRODUCTION,
        },
    );
    install::log_recovery(&report);
    AppState {
        context: Ok(ctx),
        settings_error: outcome.error,
        lock: Some(Arc::new(lock)),
    }
}

fn failed(error: StartupError) -> AppState {
    AppState {
        context: Err(error),
        settings_error: None,
        lock: None,
    }
}

/// FR-102: the configured root folded in on Linux; on Windows and macOS the stored value is
/// ignored. Only called with validated settings, whose checks the fold repeats, so it cannot
/// fail; on a sandboxed test `Roots` an outside root panics (PD-24).
fn fold(os: Os, roots: &Roots, settings: &Settings) -> Roots {
    let configured = match os {
        Os::Linux => settings.linux_plugin_root.as_deref(),
        Os::Windows | Os::Macos => None,
    };
    roots
        .with_configured_plugin_root(configured)
        .expect("validated settings hold an absolute, normal root")
}

/// FR-103 on this OS.
pub fn reassemble(prev: &AppState, settings: Settings) -> AppState {
    reassemble_with_os(NativePlatform.os(), prev, settings)
}

/// No I/O. An `Err` context comes back unchanged; settings that fail `validate` keep the
/// previous context and report the error; otherwise the roots are re-derived with the new
/// configured root, with the same feed URL, the same HTTP configuration, the same signing key
/// and the same lock.
pub(crate) fn reassemble_with_os(os: Os, prev: &AppState, settings: Settings) -> AppState {
    let lock = prev.lock.clone();
    let ctx = match &prev.context {
        Ok(ctx) => ctx,
        Err(e) => {
            return AppState {
                context: Err(e.clone()),
                settings_error: None,
                lock,
            };
        }
    };
    if let Err(e) = settings::validate(&settings) {
        return AppState {
            context: Ok(ctx.clone()),
            settings_error: Some(e),
            lock,
        };
    }
    AppState {
        context: Ok(AppContext {
            roots: fold(os, &ctx.roots, &settings),
            settings,
            feed_url: ctx.feed_url.clone(),
            http: ctx.http.clone(),
            signing_key: ctx.signing_key.clone(),
        }),
        settings_error: None,
        lock,
    }
}

/// The one way to swap in new settings (PD-2): the read guard is dropped before the write
/// guard is taken, so this thread never holds both.
pub fn apply_settings(shared: &SharedState, settings: Settings) {
    let next = {
        let guard = shared.read().unwrap_or_else(PoisonError::into_inner);
        reassemble(&guard, settings)
    };
    *shared.write().unwrap_or_else(PoisonError::into_inner) = next;
}

/// `LOCKED_NOTICE` exactly when another instance holds the lock (FR-101).
pub fn startup_notice(state: &AppState) -> Option<&'static str> {
    matches!(state.context, Err(StartupError::Locked)).then_some(LOCKED_NOTICE)
}

/// One `error!` per startup or settings error present; each Display begins with its code.
pub fn log_startup_errors(state: &AppState) {
    if let Err(e) = &state.context {
        log::error!(target: STARTUP_LOG_TARGET, "{e}");
    }
    if let Some(e) = &state.settings_error {
        log::error!(target: STARTUP_LOG_TARGET, "{e}");
    }
}

/// The trimmed text when it is a single-line absolute `http`/`https` URL with a host, at
/// most 2048 bytes (FR-106); otherwise `None`.
pub fn parse_dev_feed_url(text: &str) -> Option<String> {
    let t = text.trim();
    if t.is_empty() || t.len() > MAX_DEV_FEED_URL_BYTES || t.contains(['\n', '\r']) {
        return None;
    }
    let url = url::Url::parse(t).ok()?;
    let web = matches!(url.scheme(), "http" | "https");
    let host = url.host_str().is_some_and(|h| !h.is_empty());
    (web && host).then(|| t.to_owned())
}

/// What `release_for_restart` did with the state lock (Phase 11 FR-016, PD-10).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LockRelease {
    Released,
    NoLock,
    StillShared,
}

/// Phase 11 FR-016: the state lock released before the manager restarts into its update. The
/// last `Arc` dropped closes the lock file, which releases the lock (PD-10); an `Arc` still
/// shared is put back.
pub fn release_for_restart(state: &SharedState) -> LockRelease {
    let mut guard = state.write().unwrap_or_else(PoisonError::into_inner);
    let Some(lock) = guard.lock.take() else {
        return LockRelease::NoLock;
    };
    match Arc::try_unwrap(lock) {
        Ok(lock) => {
            drop(lock);
            LockRelease::Released
        }
        Err(shared) => {
            guard.lock = Some(shared);
            LockRelease::StillShared
        }
    }
}

#[cfg(test)]
#[path = "context_tests.rs"]
mod tests;
