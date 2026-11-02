//! Self-update rules (Phase 11 D8): URL, package-origin and redirect rules, proxy choice, busy and packaging rules,
//! the in-flight registry and action gate, codes and sentences, progress events, the view model, timeouts. Tauri-free.

use crate::context::AppContext;
use crate::download;
use crate::http::{self, HttpConfig, UrlPolicy};
use crate::install::{self, Elevation};
use crate::notes;
use crate::pac::ProxyChoice;
use crate::platform::Os;
use crate::status::{self, OsName};
use serde::Serialize;
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::Duration;
use url::Url;

/// FR-027: the bound of one update check.
pub const CHECK_TIMEOUT: Duration = Duration::from_secs(30);
/// FR-027: the bound of the package download, set on `Update.timeout`.
pub const DOWNLOAD_TIMEOUT: Duration = Duration::from_secs(3600);
/// FR-027: the per-read stall bound of the updater's client.
pub const STALL_BOUND: Duration = Duration::from_secs(30);
/// PD-4: the GitHub API host, matched by its labels.
const API_HOST_LABELS: [&str; 3] = ["api", "github", "com"];
/// FR-011a: the package origin.
pub const GITHUB_HOST: &str = "github.com";
/// PD-27: owner/repo of every package URL and release page; the preflight's E_PREFLIGHT_REPO reads this line.
pub const MANAGER_REPO: &str = "rolandzwaga/krate-audio";
/// FR-011a: the host GitHub redirects release downloads to.
pub const RELEASE_ASSET_HOST: &str = "release-assets.githubusercontent.com";
const LATEST_SEGMENT: &str = "/releases/latest/";

const NETWORK_SENTENCE: &str = "Could not reach the Krate Manager update server.";
const MANIFEST_SENTENCE: &str =
    "The Krate Manager update information is incomplete or has no entry for this computer.";
const SIGNATURE_SENTENCE: &str =
    "The Krate Manager update failed its signature check and was not installed.";
const URL_SENTENCE: &str =
    "The Krate Manager update points to an address the manager does not use.";
const BUSY_SENTENCE: &str =
    "Finish the plugin installs and removals first, then update Krate Manager.";
const NONE_SENTENCE: &str = "There is no Krate Manager update to install. Check for updates first.";
const INSTALL_SENTENCE: &str =
    "Krate Manager could not install its update. It keeps running the current version.";
const UNSUPPORTED_SENTENCE: &str = "Krate Manager cannot update itself on this computer. Install the new version from its release page.";

/// Why an update URL or redirect hop was refused (FR-011, FR-011a).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum UrlRefusal {
    ApiHost,
    LatestPath,
    Policy,
    Host,
}

/// Every self-update failure (FR-013). Every `Display` begins with its code and carries its fixed sentence.
#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error, Serialize)]
#[serde(tag = "code", rename_all = "SCREAMING_SNAKE_CASE")]
pub enum SelfUpdateError {
    #[error("E_SELF_UPDATE_NETWORK: {}", NETWORK_SENTENCE)]
    Network { detail: String },
    #[error("E_SELF_UPDATE_MANIFEST: {}", MANIFEST_SENTENCE)]
    Manifest { detail: String },
    #[error("E_SELF_UPDATE_SIGNATURE: {}", SIGNATURE_SENTENCE)]
    Signature { detail: String },
    #[error("E_SELF_UPDATE_URL: {}", URL_SENTENCE)]
    Url { reason: UrlRefusal },
    #[error("E_SELF_UPDATE_BUSY: {}", BUSY_SENTENCE)]
    Busy,
    #[error("E_SELF_UPDATE_NONE: {}", NONE_SENTENCE)]
    None,
    #[error("E_SELF_UPDATE_INSTALL: {}", INSTALL_SENTENCE)]
    Install { detail: String },
    #[error("E_SELF_UPDATE_UNSUPPORTED: {}", UNSUPPORTED_SENTENCE)]
    Unsupported,
}

/// The serialized form the UI reads (tasks TA-21): the code, its fixed sentence, and the variant's fields.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct ErrorView {
    pub code: &'static str,
    pub sentence: &'static str,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<UrlRefusal>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub detail: Option<String>,
}

impl SelfUpdateError {
    pub fn code(&self) -> &'static str {
        match self {
            SelfUpdateError::Network { .. } => CODES[0],
            SelfUpdateError::Manifest { .. } => CODES[1],
            SelfUpdateError::Signature { .. } => CODES[2],
            SelfUpdateError::Url { .. } => CODES[3],
            SelfUpdateError::Busy => CODES[4],
            SelfUpdateError::None => CODES[5],
            SelfUpdateError::Install { .. } => CODES[6],
            SelfUpdateError::Unsupported => CODES[7],
        }
    }

    pub fn sentence(&self) -> &'static str {
        match self {
            SelfUpdateError::Network { .. } => NETWORK_SENTENCE,
            SelfUpdateError::Manifest { .. } => MANIFEST_SENTENCE,
            SelfUpdateError::Signature { .. } => SIGNATURE_SENTENCE,
            SelfUpdateError::Url { .. } => URL_SENTENCE,
            SelfUpdateError::Busy => BUSY_SENTENCE,
            SelfUpdateError::None => NONE_SENTENCE,
            SelfUpdateError::Install { .. } => INSTALL_SENTENCE,
            SelfUpdateError::Unsupported => UNSUPPORTED_SENTENCE,
        }
    }

    pub fn view(&self) -> ErrorView {
        let (reason, detail) = match self {
            SelfUpdateError::Url { reason } => (Some(*reason), None),
            SelfUpdateError::Network { detail }
            | SelfUpdateError::Manifest { detail }
            | SelfUpdateError::Signature { detail }
            | SelfUpdateError::Install { detail } => (None, Some(detail.clone())),
            SelfUpdateError::Busy | SelfUpdateError::None | SelfUpdateError::Unsupported => {
                (None, None)
            }
        };
        ErrorView {
            code: self.code(),
            sentence: self.sentence(),
            reason,
            detail,
        }
    }
}

/// Every `SelfUpdateError` code, in variant order (SC-003).
pub const CODES: [&str; 8] = [
    "E_SELF_UPDATE_NETWORK",
    "E_SELF_UPDATE_MANIFEST",
    "E_SELF_UPDATE_SIGNATURE",
    "E_SELF_UPDATE_URL",
    "E_SELF_UPDATE_BUSY",
    "E_SELF_UPDATE_NONE",
    "E_SELF_UPDATE_INSTALL",
    "E_SELF_UPDATE_UNSUPPORTED",
];

/// The install's progress events (PD-25), sent over `manager_update_install`'s channel.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "event", rename_all = "snake_case")]
pub enum SelfUpdateProgress {
    Downloading,
    Installing,
}

/// The three self-update bounds (PD-29).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct UpdateTimeouts {
    pub check: Duration,
    pub download: Duration,
    pub stall: Duration,
}

impl UpdateTimeouts {
    /// `CHECK_TIMEOUT`, `DOWNLOAD_TIMEOUT`, `STALL_BOUND`.
    pub const fn production() -> Self {
        UpdateTimeouts {
            check: CHECK_TIMEOUT,
            download: DOWNLOAD_TIMEOUT,
            stall: STALL_BOUND,
        }
    }
}

/// PD-9, FR-026: the context's `HttpConfig`, or `fallback` when there is no context.
pub fn self_update_http(ctx: Option<&AppContext>, fallback: &HttpConfig) -> HttpConfig {
    ctx.map_or_else(|| fallback.clone(), |c| c.http.clone())
}

/// How this copy of the manager was installed (PD-24).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Packaging {
    SystemPackage,
    SelfContained,
}

/// PD-24: a system-package install never updates itself.
pub fn self_install_supported(p: Packaging) -> Result<(), SelfUpdateError> {
    match p {
        Packaging::SystemPackage => Err(SelfUpdateError::Unsupported),
        Packaging::SelfContained => Ok(()),
    }
}

fn url_refused(reason: UrlRefusal) -> SelfUpdateError {
    SelfUpdateError::Url { reason }
}

/// A host as compared here: lower-cased, one trailing `.` stripped.
fn normalized_host(host: &str) -> String {
    let lower = host.to_ascii_lowercase();
    match lower.strip_suffix('.') {
        Some(stripped) => stripped.to_owned(),
        None => lower,
    }
}

fn host_of(url: &Url) -> Option<String> {
    url.host_str().map(normalized_host)
}

fn is_api_host(url: &Url) -> bool {
    host_of(url).is_some_and(|h| h.split('.').eq(API_HOST_LABELS))
}

/// FR-011: the endpoint and package URL rule, in this order: the API host, a latest-release path, the policy.
pub fn check_update_url(policy: UrlPolicy, url: &Url) -> Result<(), SelfUpdateError> {
    if is_api_host(url) {
        return Err(url_refused(UrlRefusal::ApiHost));
    }
    if url.path().contains(LATEST_SEGMENT) {
        return Err(url_refused(UrlRefusal::LatestPath));
    }
    http::check_url(policy, url).map_err(|_| url_refused(UrlRefusal::Policy))
}

/// `x.y.z`, each part `0` or a number without a leading zero.
fn is_semver_core(text: &str) -> bool {
    let parts: Vec<&str> = text.split('.').collect();
    parts.len() == 3
        && parts.iter().all(|p| {
            !p.is_empty()
                && p.bytes().all(|b| b.is_ascii_digit())
                && (*p == "0" || !p.starts_with('0'))
        })
}

/// The tag segment of a per-tag package path `<owner>/<repo>/releases/download/manager/v<x.y.z>/<name>` of
/// `MANAGER_REPO`, or `None`.
fn per_tag_segments(url: &Url) -> Option<&str> {
    let segments: Vec<&str> = url.path_segments()?.collect();
    let [owner, repo, "releases", "download", "manager", tag, name] = segments[..] else {
        return None;
    };
    let named = [owner, repo, tag, name].iter().all(|s| !s.is_empty());
    let repo_ok = format!("{owner}/{repo}") == MANAGER_REPO;
    let tag_ok = tag.strip_prefix('v').is_some_and(is_semver_core);
    (named && repo_ok && tag_ok).then_some(tag)
}

/// FR-011, FR-011a, PD-27: a package URL names this repository's release of the announced version.
pub fn check_update_package_url(
    policy: UrlPolicy,
    url: &Url,
    announced: &str,
) -> Result<(), SelfUpdateError> {
    check_update_url(policy, url)?;
    let host_ok = match policy {
        UrlPolicy::HttpsOnly => host_of(url).as_deref() == Some(GITHUB_HOST),
        UrlPolicy::LoopbackOnly => true,
    };
    let tag_ok = per_tag_segments(url).is_some_and(|tag| tag == format!("v{announced}"));
    if url.query().is_none() && url.fragment().is_none() && tag_ok && host_ok {
        Ok(())
    } else {
        Err(url_refused(UrlRefusal::Host))
    }
}

/// Which request a redirect hop belongs to (PD-5).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HopKind {
    Endpoint,
    Package,
}

/// What a redirect hop is checked against (FR-011a).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HopRules {
    pub policy: UrlPolicy,
    pub endpoint_host: String,
}

/// FR-011a: one redirect hop.
pub fn check_update_hop(
    rules: &HopRules,
    kind: HopKind,
    from: &Url,
    to: &Url,
) -> Result<(), SelfUpdateError> {
    check_update_url(rules.policy, to)?;
    let accepted = match (rules.policy, kind) {
        // The URL check already required a loopback `to`.
        (UrlPolicy::LoopbackOnly, _) => true,
        (UrlPolicy::HttpsOnly, HopKind::Endpoint) => {
            host_of(to) == Some(normalized_host(&rules.endpoint_host))
        }
        (UrlPolicy::HttpsOnly, HopKind::Package) => {
            let package_host = |url: &Url| {
                host_of(url).is_some_and(|h| h == GITHUB_HOST || h == RELEASE_ASSET_HOST)
            };
            package_host(from) && package_host(to)
        }
    };
    if accepted {
        Ok(())
    } else {
        Err(url_refused(UrlRefusal::Host))
    }
}

/// PD-5: `Endpoint` iff the chain started at the endpoint.
pub fn hop_kind(first: &Url, endpoint: &Url) -> HopKind {
    if first == endpoint {
        HopKind::Endpoint
    } else {
        HopKind::Package
    }
}

/// PD-5: the redirect policy's one decision. More than `http::MAX_REDIRECTS` previous URLs is `Policy`; otherwise
/// the hop from the last previous URL to `to`, judged by the rule of the chain's first URL.
pub fn redirect_decision(
    rules: &HopRules,
    endpoint: &Url,
    previous: &[Url],
    to: &Url,
) -> Result<(), UrlRefusal> {
    if previous.len() > http::MAX_REDIRECTS as usize {
        return Err(UrlRefusal::Policy);
    }
    // A redirect attempt always has the original URL in `previous`; an empty chain is refused, never guessed.
    let (Some(first), Some(from)) = (previous.first(), previous.last()) else {
        return Err(UrlRefusal::Policy);
    };
    check_update_hop(rules, hop_kind(first, endpoint), from, to).map_err(|e| match e {
        SelfUpdateError::Url { reason } => reason,
        _ => UrlRefusal::Host,
    })
}

/// D17, PD-6: the route a self-update request takes; an `Err` is `Network` and no request is made.
pub fn proxy_choice(http: &HttpConfig, url: &Url) -> Result<ProxyChoice, SelfUpdateError> {
    http::route_choice(http, url).map_err(|e| SelfUpdateError::Network {
        detail: e.to_string(),
    })
}

/// FR-015, PD-8: an elevated batch or a plugin action is running. With no context only the elevation guard can be
/// held (FR-026).
pub fn self_update_busy(elevation: &dyn Elevation, ctx: Option<&AppContext>) -> bool {
    elevation.guard().is_held()
        || ctx.is_some_and(|c| {
            install::any_busy(c.roots.state_dir())
                || download::any_in_flight(&c.roots.staging_dir().join(download::DOWNLOADS_DIR))
        })
}

/// FR-017, PD-7: the state dirs (`None` with no context) whose manager is updating itself.
static SELF_UPDATING: Mutex<BTreeSet<Option<PathBuf>>> = Mutex::new(BTreeSet::new());

fn self_updating() -> MutexGuard<'static, BTreeSet<Option<PathBuf>>> {
    SELF_UPDATING.lock().unwrap_or_else(PoisonError::into_inner)
}

/// FR-017: a self-update in flight; dropping it (also while unwinding) clears the flag.
pub struct InFlightGuard {
    key: Option<PathBuf>,
}

impl InFlightGuard {
    pub fn enter(state_dir: Option<&Path>) -> InFlightGuard {
        let key = state_dir.map(Path::to_path_buf);
        self_updating().insert(key.clone());
        InFlightGuard { key }
    }
}

impl Drop for InFlightGuard {
    fn drop(&mut self) {
        self_updating().remove(&self.key);
    }
}

/// FR-017: whether the manager of `state_dir` is updating itself.
pub fn in_flight(state_dir: &Path) -> bool {
    self_updating().contains(&Some(state_dir.to_path_buf()))
}

/// FR-018: the refusal of a plugin action while the manager updates itself.
#[derive(Clone, Copy, Debug, PartialEq, Eq, thiserror::Error)]
#[error("E_INSTALL_SELF_UPDATING: Krate Manager is updating itself. Try again after it restarts.")]
pub struct SelfUpdating;

/// FR-018: the code of `SelfUpdating`.
pub const SELF_UPDATING_CODE: &str = "E_INSTALL_SELF_UPDATING";

/// FR-018: the first check of every plugin action and elevated batch.
pub fn action_gate(state_dir: &Path) -> Result<(), SelfUpdating> {
    if in_flight(state_dir) {
        Err(SelfUpdating)
    } else {
        Ok(())
    }
}

/// FR-014: what the update line shows.
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct ManagerUpdateView {
    pub os: OsName,
    pub current: String,
    pub available: Option<AvailableView>,
    pub error: Option<ErrorView>,
}

/// FR-014: the announced update.
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct AvailableView {
    pub version: String,
    pub date: Option<String>,
    pub notes: Vec<notes::Block>,
    pub action: UpdateAction,
}

/// PD-31: what the update line offers.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum UpdateAction {
    InstallAndRestart,
    OpenReleasePage { url: String },
}

/// PD-31, SI-40: the action for an announced update, after its package URL passed PD-27.
/// The release page is built from `MANAGER_REPO` and `announced`, never from the URL's segments.
pub fn update_action(
    packaging: Packaging,
    policy: UrlPolicy,
    download_url: &Url,
    announced: &str,
) -> Result<UpdateAction, SelfUpdateError> {
    check_update_package_url(policy, download_url, announced)?;
    match packaging {
        Packaging::SelfContained => Ok(UpdateAction::InstallAndRestart),
        Packaging::SystemPackage => {
            let page = format!(
                "https://{}/{}/releases/tag/manager/v{}",
                status::RELEASE_PAGE_HOST,
                MANAGER_REPO,
                announced
            );
            if status::release_page_url(&page).as_deref() == Some(page.as_str()) {
                Ok(UpdateAction::OpenReleasePage { url: page })
            } else {
                Err(url_refused(UrlRefusal::Host))
            }
        }
    }
}

/// FR-014: the facts of one announced update `view` reads.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UpdateFacts<'a> {
    pub version: &'a str,
    pub date_rfc3339: Option<String>,
    pub body: Option<&'a str>,
    pub action: UpdateAction,
}

/// FR-014: the view of one check.
pub fn view(
    os: Os,
    current: &str,
    update: Option<UpdateFacts<'_>>,
    error: Option<SelfUpdateError>,
) -> ManagerUpdateView {
    // tasks TA-7: `status::os_name` is private, so the three arms are repeated here.
    let os = match os {
        Os::Windows => OsName::Windows,
        Os::Macos => OsName::Macos,
        Os::Linux => OsName::Linux,
    };
    ManagerUpdateView {
        os,
        current: current.to_owned(),
        available: update.map(|u| AvailableView {
            version: u.version.to_owned(),
            date: u.date_rfc3339,
            notes: u.body.map(notes::blocks).unwrap_or_default(),
            action: u.action,
        }),
        error: error.as_ref().map(SelfUpdateError::view),
    }
}

/// TA-6: `text` with any URL userinfo redacted: `http::redact_userinfo` over each whitespace-separated word, so a
/// second URL in the same text is redacted too.
pub fn redacted(text: &str) -> String {
    text.split_inclusive(char::is_whitespace)
        .map(http::redact_userinfo)
        .collect()
}

#[cfg(test)]
#[path = "self_update_tests.rs"]
mod tests;
