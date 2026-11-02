//! The self-update adapter (Phase 11 D7, FR-022, FR-023): the updater plugin driven by the core's
//! `self_update` rules, the kept update, the one installer, the error map and the updater-e2e module.

use krate_manager_core::http::{HttpConfig, ProxyConfig, UrlPolicy};
use krate_manager_core::pac::ProxyChoice;
use krate_manager_core::platform::{NativePlatform, Platform};
use krate_manager_core::self_update::{
    HopRules, InFlightGuard, ManagerUpdateView, Packaging, SelfUpdateError, SelfUpdateProgress,
    UpdateFacts, UpdateTimeouts, UrlRefusal, check_update_package_url, check_update_url,
    proxy_choice, redacted, redirect_decision, self_install_supported, update_action, view,
};
use std::fmt;
use std::path::Path;
use std::sync::{Mutex, PoisonError};
use tauri::utils::config::BundleType;
use tauri::utils::platform::bundle_type;
use tauri::{AppHandle, Manager, Runtime, Url};
use tauri_plugin_updater::{Update, Updater, UpdaterExt};

/// PD-9: the `HttpConfig` a self-update uses when the startup context failed (managed).
pub struct SelfUpdateHttp(pub HttpConfig);

/// FR-023: the one update the last check found (managed, single slot).
#[derive(Default)]
pub struct KeptUpdate(Mutex<Option<Update>>);

/// Everything one check or install reads (PD-29, PD-36). `Clone` so the updater's `'static` client
/// closure can own the copy it reads (`build_updater`).
#[derive(Clone)]
pub struct UpdateInputs {
    pub http: HttpConfig,
    pub policy: UrlPolicy,
    pub timeouts: UpdateTimeouts,
    pub packaging: Packaging,
}

impl UpdateInputs {
    /// PD-36: the production inputs, the only ones the commands build.
    pub fn production(http: HttpConfig) -> Self {
        Self {
            http,
            policy: UrlPolicy::HttpsOnly,
            timeouts: UpdateTimeouts::production(),
            packaging: packaging(),
        }
    }
}

/// PD-24: a `.deb` / `.rpm` install is a system package.
pub fn packaging() -> Packaging {
    match bundle_type() {
        Some(BundleType::Deb | BundleType::Rpm) => Packaging::SystemPackage,
        _ => Packaging::SelfContained,
    }
}

/// The step that hands verified bytes to the OS install (FR-022).
pub trait UpdateInstaller<R: Runtime>: Send + Sync {
    fn install(
        &self,
        app: &AppHandle<R>,
        update: &Update,
        bytes: Vec<u8>,
    ) -> Result<(), SelfUpdateError>;
}

/// The only `Update::install` call (FR-094).
pub struct NativeUpdateInstaller;

impl<R: Runtime> UpdateInstaller<R> for NativeUpdateInstaller {
    fn install(
        &self,
        app: &AppHandle<R>,
        update: &Update,
        bytes: Vec<u8>,
    ) -> Result<(), SelfUpdateError> {
        update.install(bytes).map_err(|e| map_error(&e))?;
        // Windows: the updater has launched the setup and ended this process inside the call above.
        #[cfg(windows)]
        {
            let _ = app;
            Ok(())
        }
        // macOS / Linux (FR-016): free the state lock so the relaunched copy can take it, then relaunch.
        #[cfg(not(windows))]
        {
            use krate_manager_core::context::{LockRelease, SharedState, release_for_restart};
            if release_for_restart(&app.state::<SharedState>()) == LockRelease::StillShared {
                log::error!(
                    "E_SELF_UPDATE_LOCK_SHARED: the state lock is still shared; the relaunched manager may find it held"
                );
            }
            app.restart()
        }
    }
}

/// D2: the compiled `plugins.updater.endpoints[0]`.
pub fn endpoint_of(config: &tauri::Config) -> Result<Url, SelfUpdateError> {
    let text = config
        .plugins
        .0
        .get("updater")
        .and_then(|u| u.get("endpoints"))
        .and_then(|e| e.get(0))
        .and_then(serde_json::Value::as_str)
        .ok_or_else(|| SelfUpdateError::Manifest {
            detail: "the app config has no plugins.updater.endpoints[0]".to_owned(),
        })?;
    Url::parse(text).map_err(|e| SelfUpdateError::Manifest {
        detail: redacted(&format!("the updater endpoint does not parse: {e}")),
    })
}

/// D17: a proxy route as the URL the updater takes.
fn proxy_url(cfg: &ProxyConfig) -> Result<Url, SelfUpdateError> {
    Url::parse(&cfg.url).map_err(|e| SelfUpdateError::Network {
        detail: redacted(&format!("the proxy URL does not parse: {e}")),
    })
}

/// PD-5: a redirect hop the core refused, carried through reqwest's error to `map_error`.
#[derive(Debug)]
struct HopRefused(UrlRefusal);

impl fmt::Display for HopRefused {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "redirect refused: {:?}", self.0)
    }
}

impl std::error::Error for HopRefused {}

/// FR-022, FR-027: the updater with the core's timeouts, route and redirect policy.
pub fn build_updater<R: Runtime>(
    app: &AppHandle<R>,
    inputs: &UpdateInputs,
) -> Result<Updater, SelfUpdateError> {
    let endpoint = endpoint_of(app.config())?;
    check_update_url(inputs.policy, &endpoint)?;
    let rules = HopRules {
        policy: inputs.policy,
        endpoint_host: endpoint.host_str().unwrap_or_default().to_owned(),
    };
    let builder = app.updater_builder().timeout(inputs.timeouts.check);
    let builder = match proxy_choice(&inputs.http, &endpoint)? {
        ProxyChoice::Direct => builder.no_proxy(),
        ProxyChoice::Proxy(cfg) => builder.proxy(proxy_url(&cfg)?),
    };
    // The client closure is `'static`: it owns this copy of the inputs, the rules and the endpoint.
    let inputs = inputs.clone();
    builder
        .configure_client(move |b| {
            let (rules, endpoint) = (rules.clone(), endpoint.clone());
            b.read_timeout(inputs.timeouts.stall)
                .redirect(reqwest::redirect::Policy::custom(
                    move |a| match redirect_decision(&rules, &endpoint, a.previous(), a.url()) {
                        Ok(()) => a.follow(),
                        Err(r) => a.error(HopRefused(r)),
                    },
                ))
        })
        .build()
        .map_err(|e| map_error(&e))
}

/// D7, FR-023, FR-026: one check; the update it finds is kept.
pub async fn check<R: Runtime>(app: &AppHandle<R>, inputs: &UpdateInputs) -> ManagerUpdateView {
    let found = match build_updater(app, inputs) {
        Ok(updater) => updater.check().await.map_err(|e| map_error(&e)),
        Err(e) => Err(e),
    };
    // PD-31, PD-27: an announced update whose package URL fails the core's rule is a check error.
    let outcome = found.and_then(|found| {
        found
            .map(|u| {
                update_action(inputs.packaging, inputs.policy, &u.download_url, &u.version)
                    .map(|a| (u, a))
            })
            .transpose()
    });
    if let Err(e) = &outcome {
        log::warn!("manager update check failed: {e:?}");
    }
    let current = app.package_info().version.to_string();
    let os = NativePlatform.os();
    let kept = app.state::<KeptUpdate>();
    let mut slot = kept.0.lock().unwrap_or_else(PoisonError::into_inner);
    match outcome {
        Ok(Some((u, action))) => {
            // The updater parsed this text as RFC 3339 (`pub_date`); `Update.date` is its parsed form.
            let date_rfc3339 = u
                .date
                .and_then(|_| u.raw_json.get("pub_date"))
                .and_then(serde_json::Value::as_str)
                .map(str::to_owned);
            let facts = UpdateFacts {
                version: &u.version,
                date_rfc3339,
                body: u.body.as_deref(),
                action,
            };
            let shown = view(os, &current, Some(facts), None);
            *slot = Some(u);
            shown
        }
        Ok(None) => {
            *slot = None;
            view(os, &current, None, None)
        }
        Err(e) => {
            *slot = None;
            view(os, &current, None, Some(e))
        }
    }
}

/// PD-29: the package URL rule, the download bound and the route, before `download`.
pub fn prepare_download(update: &mut Update, inputs: &UpdateInputs) -> Result<(), SelfUpdateError> {
    check_update_package_url(inputs.policy, &update.download_url, &update.version)?;
    update.timeout = Some(inputs.timeouts.download);
    match proxy_choice(&inputs.http, &update.download_url)? {
        ProxyChoice::Direct => {
            update.proxy = None;
            update.no_proxy = true;
        }
        ProxyChoice::Proxy(cfg) => {
            update.proxy = Some(proxy_url(&cfg)?);
            update.no_proxy = false;
        }
    }
    Ok(())
}

/// D7, FR-015 to FR-019, FR-023, FR-027: install the kept update.
pub async fn install<R: Runtime>(
    app: &AppHandle<R>,
    inputs: &UpdateInputs,
    installer: &dyn UpdateInstaller<R>,
    busy: &(dyn Fn() -> bool + Sync),
    state_dir: Option<&Path>,
    progress: &(dyn Fn(SelfUpdateProgress) + Sync),
) -> Result<(), SelfUpdateError> {
    // PD-24: a system-package install never updates itself.
    self_install_supported(inputs.packaging)?;
    // PD-37: the relaunch needs the binary path, which a symlinked path does not give.
    #[cfg(target_os = "macos")]
    tauri::process::current_binary(&app.env()).map_err(|_| SelfUpdateError::Unsupported)?;
    // FR-015, PD-28: busy before the slot, so the kept update stays for the next attempt.
    if busy() {
        return Err(SelfUpdateError::Busy);
    }
    // FR-023: from here every error leaves the slot empty.
    let mut update = app
        .state::<KeptUpdate>()
        .0
        .lock()
        .unwrap_or_else(PoisonError::into_inner)
        .take()
        .ok_or(SelfUpdateError::None)?;
    // FR-017: plugin actions are refused until this returns.
    let _flight = InFlightGuard::enter(state_dir);
    progress(SelfUpdateProgress::Downloading);
    prepare_download(&mut update, inputs)?;
    // The download verifies the signature before it returns the bytes.
    let bytes = update
        .download(|_, _| {}, || {})
        .await
        .map_err(|e| map_error(&e))?;
    // FR-019: busy again after the download.
    if busy() {
        return Err(SelfUpdateError::Busy);
    }
    progress(SelfUpdateProgress::Installing);
    UpdateInstaller::install(installer, app, &update, bytes)
}

/// True when the source chain holds a JSON parse error (latest.json that is not JSON).
fn is_json_error(e: &reqwest::Error) -> bool {
    std::iter::successors(Some(e as &(dyn std::error::Error + 'static)), |e| {
        e.source()
    })
    .any(|e| e.is::<serde_json::Error>())
}

/// PD-5: the core's refusal, if a redirect hop was refused.
fn hop_refusal(e: &reqwest::Error) -> Option<UrlRefusal> {
    std::iter::successors(Some(e as &(dyn std::error::Error + 'static)), |e| {
        e.source()
    })
    .find_map(|e| e.downcast_ref::<HopRefused>())
    .map(|h| h.0)
}

/// FR-013: an updater error as its self-update code.
pub fn map_error(e: &tauri_plugin_updater::Error) -> SelfUpdateError {
    use tauri_plugin_updater::Error as E;
    let detail = || redacted(&e.to_string());
    match e {
        E::Reqwest(inner) => match hop_refusal(inner) {
            Some(reason) => SelfUpdateError::Url { reason },
            // spec corrupt-manifest: latest.json is not JSON (a stalled body is a decode error too, so match serde).
            None if is_json_error(inner) => SelfUpdateError::Manifest { detail: detail() },
            None => SelfUpdateError::Network { detail: detail() },
        },
        E::Network(_) => SelfUpdateError::Network { detail: detail() },
        E::ReleaseNotFound
        | E::Serialization(_)
        | E::TargetNotFound(_)
        | E::TargetsNotFound(_)
        | E::UrlParse(_)
        | E::Semver(_)
        | E::FormatDate => SelfUpdateError::Manifest { detail: detail() },
        E::Minisign(_)
        | E::Base64(_)
        | E::SignatureUtf8(_)
        | E::SignedVersionMismatch { .. }
        | E::MissingSignedVersion => SelfUpdateError::Signature { detail: detail() },
        E::InsecureTransportProtocol => SelfUpdateError::Url {
            reason: UrlRefusal::Policy,
        },
        E::UnsupportedArch | E::UnsupportedOs => SelfUpdateError::Unsupported,
        E::Io(_)
        | E::TempDirNotOnSameMountPoint
        | E::PreviousAppNotRestored(_)
        | E::BinaryNotFoundInArchive
        | E::TempDirNotFound
        | E::AuthenticationFailed
        | E::DebInstallFailed
        | E::PackageInstallFailed
        | E::InvalidUpdaterFormat
        | E::FailedToDetermineExtractPath
        | E::EmptyEndpoints
        | E::Http(_)
        | E::InvalidHeaderValue(_)
        | E::InvalidHeaderName(_)
        | E::Tauri(_) => SelfUpdateError::Install { detail: detail() },
        // PD-3: the enum is `#[non_exhaustive]`; the exact pin makes this arm unreachable today.
        _ => SelfUpdateError::Install {
            detail: redacted(&format!("unmapped updater error: {e}")),
        },
    }
}

/// FR-070, PD-26: the release workflow's updater-e2e build only.
#[cfg(feature = "updater-e2e")]
pub mod e2e {
    #[cfg(feature = "dev-feed")]
    compile_error!("updater-e2e and dev-feed are never combined");

    use super::{NativeUpdateInstaller, UpdateInputs, check, install};
    use crate::os_elevation::NativeElevation;
    use krate_manager_core::context::SharedState;
    use krate_manager_core::http::{self, HttpConfig, UrlPolicy};
    use krate_manager_core::self_update::{SelfUpdateError, self_update_busy};
    use std::path::Path;
    use std::sync::{Arc, PoisonError};
    use tauri::{AppHandle, Manager, Runtime};

    /// The content of every marker file; the e2e job also finds it in the old build's executable.
    const MARKER: &str = "krate-e2e-marker-v1";
    /// The loopback feed that replaces the production one.
    const ENDPOINT_FEED: &str = env!("KRATE_E2E_FEED_URL");
    const MARKER_DIR: &str = env!("KRATE_E2E_MARKER_DIR");
    const OLD_VERSION: &str = env!("KRATE_E2E_OLD_VERSION");

    /// TA-27: the loopback config of the e2e build, for the feed and the update alike.
    fn loopback_http() -> HttpConfig {
        HttpConfig::loopback(http::CONNECT_TIMEOUT, http::TOTAL_TIMEOUT)
    }

    /// The e2e feed and loopback config in place of production's.
    pub fn override_feed(
        production_feed: String,
        production_http: HttpConfig,
    ) -> (String, HttpConfig) {
        drop((production_feed, production_http));
        (ENDPOINT_FEED.to_owned(), loopback_http())
    }

    fn write_marker(name: &str) {
        let dir = Path::new(MARKER_DIR);
        let written =
            std::fs::create_dir_all(dir).and_then(|()| std::fs::write(dir.join(name), MARKER));
        if let Err(e) = written {
            log::error!("updater-e2e marker {name}: {e}");
        }
    }

    /// Called from `setup`.
    pub fn start<R: Runtime>(app: &AppHandle<R>) {
        let version = app.package_info().version.to_string();
        let (locked, ctx) = {
            let state = app.state::<SharedState>();
            let guard = state.read().unwrap_or_else(PoisonError::into_inner);
            (guard.lock.is_some(), guard.context.clone().ok())
        };
        if locked {
            write_marker(&format!("launched-{version}"));
        }
        if version != OLD_VERSION {
            return;
        }
        let app = app.clone();
        let elevation = Arc::clone(&app.state::<Arc<NativeElevation>>());
        tauri::async_runtime::spawn(async move {
            let inputs = UpdateInputs {
                policy: UrlPolicy::LoopbackOnly,
                ..UpdateInputs::production(loopback_http())
            };
            let shown = check(&app, &inputs).await;
            let failure = if let Some(error) = shown.error {
                Some(error.code)
            } else if shown.available.is_some() {
                let busy = || self_update_busy(&*elevation, ctx.as_ref());
                let state_dir = ctx.as_ref().map(|c| c.roots.state_dir());
                install(
                    &app,
                    &inputs,
                    &NativeUpdateInstaller,
                    &busy,
                    state_dir,
                    &|_| {},
                )
                .await
                .err()
                .map(|e| e.code())
            } else {
                Some(SelfUpdateError::None.code())
            };
            if let Some(code) = failure {
                log::error!("updater-e2e: {code}");
                write_marker(&format!("error-{code}"));
            }
        });
    }
}
