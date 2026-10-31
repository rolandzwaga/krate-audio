//! The one HTTP request path, its URL and proxy policy (D4 to D6, FR-040 to FR-049, FR-064 to FR-069, FR-092;
//! plan §4.5, §4.5.1).

use crate::pac::{self, PAC_PHASE_BUDGET, PacCode, PacError, PacHostHandle, ProxyChoice};
use crate::sysproxy::SystemProxy;
use log::warn;
use std::fmt;
use std::io::{self, Read};
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
#[cfg(not(windows))]
use std::net::{ToSocketAddrs, UdpSocket};
#[cfg(not(windows))]
use std::sync::Arc;
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};
use url::Host;

/// FR-065 (re-exported from pac.rs).
pub use crate::pac::MAX_PAC_BYTES;

pub const CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
pub const TOTAL_TIMEOUT: Duration = Duration::from_secs(30);
pub const MAX_REDIRECTS: u32 = 5;

/// Which URLs a request may reach (FR-042).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UrlPolicy {
    HttpsOnly,
    LoopbackOnly,
}

/// Everything a request needs (FR-041, SI-26, PD-32).
#[derive(Clone, Debug, PartialEq)]
pub struct HttpConfig {
    pub policy: UrlPolicy,
    pub connect_timeout: Duration,
    pub total_timeout: Duration,
    pub proxy: Option<ProxyConfig>,
    pub pac: Option<PacSource>,
    pub pac_host: Option<PacHostHandle>,
}

impl HttpConfig {
    /// `HttpsOnly`, 10 s, 30 s, no proxy, no PAC source, no PAC host.
    pub fn production() -> Self {
        HttpConfig {
            policy: UrlPolicy::HttpsOnly,
            connect_timeout: CONNECT_TIMEOUT,
            total_timeout: TOTAL_TIMEOUT,
            proxy: None,
            pac: None,
            pac_host: None,
        }
    }

    /// `LoopbackOnly`, no proxy, no PAC source, no PAC host.
    pub fn loopback(connect_timeout: Duration, total_timeout: Duration) -> Self {
        HttpConfig {
            policy: UrlPolicy::LoopbackOnly,
            connect_timeout,
            total_timeout,
            proxy: None,
            pac: None,
            pac_host: None,
        }
    }

    /// The only way a static proxy enters (FR-041, FR-046).
    pub fn with_proxy(self, proxy: Option<ProxyConfig>) -> Self {
        HttpConfig { proxy, ..self }
    }

    /// The only way a PAC source enters (FR-041, FR-064).
    pub fn with_pac(self, pac: Option<PacSource>) -> Self {
        HttpConfig { pac, ..self }
    }

    /// The only way a PAC host enters (PD-32).
    pub fn with_pac_host(self, pac_host: Option<PacHostHandle>) -> Self {
        HttpConfig { pac_host, ..self }
    }
}

/// What the OS settings say about PAC (FR-064): plain data, compared by value.
#[derive(Clone, PartialEq, Eq)]
pub struct PacSource {
    pub auto_detect: bool,
    pub url: Option<String>,
}

impl fmt::Debug for PacSource {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("PacSource")
            .field("auto_detect", &self.auto_detect)
            .field("url", &self.url.as_deref().map(pac_url_for_log))
            .finish()
    }
}

/// The proxy decision for one target (FR-048).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ProxyResolution {
    pub proxy: Option<ProxyConfig>,
    pub pac: Option<PacSource>,
}

/// A static proxy and its bypass list (FR-046, SI-12).
#[derive(Clone, PartialEq, Eq)]
pub struct ProxyConfig {
    pub url: String,
    pub no_proxy: Vec<String>,
}

impl fmt::Debug for ProxyConfig {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ProxyConfig")
            .field("url", &redact_userinfo(&self.url))
            .field("no_proxy", &self.no_proxy)
            .finish()
    }
}

impl fmt::Display for ProxyConfig {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&redact_userinfo(&self.url))
    }
}

/// The merged proxy environment (FR-048).
#[derive(Clone, Default, PartialEq, Eq)]
pub struct ProxyEnv {
    pub https_proxy: Option<String>,
    pub all_proxy: Option<String>,
    pub no_proxy: Option<String>,
}

impl fmt::Debug for ProxyEnv {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ProxyEnv")
            .field(
                "https_proxy",
                &self.https_proxy.as_deref().map(redact_userinfo),
            )
            .field("all_proxy", &self.all_proxy.as_deref().map(redact_userinfo))
            .field("no_proxy", &self.no_proxy.as_deref().map(redact_userinfo))
            .finish()
    }
}

/// The six raw environment values in a fixed shape; `src-tauri/src/proxy_env.rs` fills it (FR-063), core never
/// names the variables (config guard, §4.11.2).
#[derive(Clone, Default, PartialEq, Eq)]
pub struct ProxyVars {
    pub https_upper: Option<String>,
    pub https_lower: Option<String>,
    pub all_upper: Option<String>,
    pub all_lower: Option<String>,
    pub no_upper: Option<String>,
    pub no_lower: Option<String>,
}

impl fmt::Debug for ProxyVars {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let show = |v: &Option<String>| v.as_deref().map(redact_userinfo);
        f.debug_struct("ProxyVars")
            .field("https_upper", &show(&self.https_upper))
            .field("https_lower", &show(&self.https_lower))
            .field("all_upper", &show(&self.all_upper))
            .field("all_lower", &show(&self.all_lower))
            .field("no_upper", &show(&self.no_upper))
            .field("no_lower", &show(&self.no_lower))
            .finish()
    }
}

impl ProxyEnv {
    /// Per pair: the non-empty upper value, else the non-empty lower value, else `None`.
    pub fn from_vars(v: &ProxyVars) -> ProxyEnv {
        fn pick(upper: &Option<String>, lower: &Option<String>) -> Option<String> {
            [upper, lower]
                .into_iter()
                .flatten()
                .find(|s| !s.is_empty())
                .cloned()
        }
        ProxyEnv {
            https_proxy: pick(&v.https_upper, &v.https_lower),
            all_proxy: pick(&v.all_upper, &v.all_lower),
            no_proxy: pick(&v.no_upper, &v.no_lower),
        }
    }
}

/// The value with any userinfo replaced by `***` (PD-19), also for a schemeless `u:p@h:1` (`***@h:1`).
pub(crate) fn redact_userinfo(s: &str) -> String {
    let (prefix, rest) = match s.find("://") {
        Some(i) => s.split_at(i + 3),
        None => ("", s),
    };
    let authority_end = rest.find(['/', '?', '#']).unwrap_or(rest.len());
    match rest[..authority_end].rfind('@') {
        Some(at) => format!("{prefix}***{}", &rest[at..]),
        None => s.to_owned(),
    }
}

/// A PAC URL for a log line (PD-19, FR-065, FR-068): userinfo redacted, query and fragment removed.
pub(crate) fn pac_url_for_log(s: &str) -> String {
    let mut redacted = redact_userinfo(s);
    if let Some(end) = redacted.find(['?', '#']) {
        redacted.truncate(end);
    }
    redacted
}

/// Why a request failed (plan §3). Every `Display` begins with its code.
#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum HttpError {
    #[error("E_FEED_URL: {reason}: {url}")]
    Url { url: String, reason: &'static str },
    #[error("E_FEED_REDIRECT: {reason}")]
    Redirect { reason: &'static str },
    #[error("E_FEED_HTTP_STATUS: HTTP status {status}")]
    Status { status: u16 },
    #[error("E_FEED_TOO_LARGE: the body is larger than {max} bytes")]
    TooLarge { max: u64 },
    #[error("E_FEED_OFFLINE: {detail}")]
    Offline { detail: String },
    #[error("E_FEED_TIMEOUT: the request did not complete in time")]
    Timeout,
    #[error("E_FEED_TLS: {detail}")]
    Tls { detail: String },
    #[error("E_FEED_BODY: {detail}")]
    Body { detail: String },
}

impl HttpError {
    pub fn code(&self) -> &'static str {
        match self {
            HttpError::Url { .. } => "E_FEED_URL",
            HttpError::Redirect { .. } => "E_FEED_REDIRECT",
            HttpError::Status { .. } => "E_FEED_HTTP_STATUS",
            HttpError::TooLarge { .. } => "E_FEED_TOO_LARGE",
            HttpError::Offline { .. } => "E_FEED_OFFLINE",
            HttpError::Timeout => "E_FEED_TIMEOUT",
            HttpError::Tls { .. } => "E_FEED_TLS",
            HttpError::Body { .. } => "E_FEED_BODY",
        }
    }
}

/// The proxy schemes FR-046 admits (PD-34): `socks4a` and `socks5h` are not among them.
const PROXY_SCHEMES: [&str; 4] = ["http", "https", "socks4", "socks5"];

/// `127.0.0.1` or `[::1]` as an IP literal. A non-special scheme (`socks5://127.0.0.1`) keeps an IPv4 host as an
/// opaque `Domain`, so that text is read as an address too; a name such as `localhost` is never loopback here.
fn is_loopback_host(url: &url::Url) -> bool {
    match url.host() {
        Some(Host::Ipv4(a)) => a == Ipv4Addr::LOCALHOST,
        Some(Host::Ipv6(a)) => a == Ipv6Addr::LOCALHOST,
        Some(Host::Domain(d)) => d.parse::<Ipv4Addr>() == Ok(Ipv4Addr::LOCALHOST),
        None => false,
    }
}

fn has_userinfo(url: &url::Url) -> bool {
    !url.username().is_empty() || url.password().is_some()
}

fn has_host(url: &url::Url) -> bool {
    url.host_str().is_some_and(|h| !h.is_empty())
}

/// FR-042.
pub fn check_url(policy: UrlPolicy, url: &url::Url) -> Result<(), HttpError> {
    let refuse = |reason: &'static str| HttpError::Url {
        url: redact_userinfo(url.as_str()),
        reason,
    };
    if has_userinfo(url) {
        return Err(refuse("a URL with userinfo is not allowed"));
    }
    match policy {
        UrlPolicy::HttpsOnly => {
            if url.scheme() != "https" {
                return Err(refuse("only https is allowed"));
            }
            if !has_host(url) {
                return Err(refuse("the URL has no host"));
            }
        }
        UrlPolicy::LoopbackOnly => {
            if !matches!(url.scheme(), "http" | "https") {
                return Err(refuse("only http and https are allowed"));
            }
            if !is_loopback_host(url) {
                return Err(refuse("only 127.0.0.1 and [::1] are allowed"));
            }
        }
    }
    Ok(())
}

/// Schemes `http`, `https`, `socks4`, `socks5`.
pub fn check_proxy(policy: UrlPolicy, proxy: &ProxyConfig) -> Result<(), HttpError> {
    let refuse = |reason: &'static str| HttpError::Url {
        url: redact_userinfo(&proxy.url),
        reason,
    };
    let parsed = url::Url::parse(&proxy.url).map_err(|_| refuse("the proxy URL does not parse"))?;
    if !PROXY_SCHEMES.contains(&parsed.scheme()) {
        return Err(refuse("the proxy scheme is not allowed"));
    }
    if !has_host(&parsed) {
        return Err(refuse("the proxy URL has no host"));
    }
    if policy == UrlPolicy::LoopbackOnly && !is_loopback_host(&parsed) {
        return Err(refuse("only a 127.0.0.1 or [::1] proxy is allowed"));
    }
    Ok(())
}

/// FR-065.
pub fn check_pac_url(policy: UrlPolicy, url: &url::Url) -> Result<(), HttpError> {
    let refuse = |reason: &'static str| HttpError::Url {
        url: pac_url_for_log(url.as_str()),
        reason,
    };
    if !matches!(url.scheme(), "http" | "https") {
        return Err(refuse("a PAC URL must be http or https"));
    }
    if has_userinfo(url) {
        return Err(refuse("a PAC URL with userinfo is not allowed"));
    }
    if !has_host(url) {
        return Err(refuse("the PAC URL has no host"));
    }
    if policy == UrlPolicy::LoopbackOnly && !is_loopback_host(url) {
        return Err(refuse("only 127.0.0.1 and [::1] are allowed"));
    }
    Ok(())
}

/// One redirect decision (FR-043).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Hop {
    Done,
    Follow(url::Url),
}

type UrlCheck = fn(UrlPolicy, &url::Url) -> Result<(), HttpError>;
type NextHop = fn(UrlPolicy, &url::Url, u16, Option<&str>) -> Result<Hop, HttpError>;

fn next_hop_with(
    policy: UrlPolicy,
    current: &url::Url,
    status: u16,
    location: Option<&str>,
    check: UrlCheck,
) -> Result<Hop, HttpError> {
    if !matches!(status, 301 | 302 | 303 | 307 | 308) {
        return Ok(Hop::Done);
    }
    let location = location.ok_or(HttpError::Redirect {
        reason: "a redirect without a Location header",
    })?;
    let next = current.join(location).map_err(|_| HttpError::Redirect {
        reason: "a redirect with an unparsable Location header",
    })?;
    check(policy, &next)?;
    Ok(Hop::Follow(next))
}

pub fn next_hop(
    policy: UrlPolicy,
    current: &url::Url,
    status: u16,
    location: Option<&str>,
) -> Result<Hop, HttpError> {
    next_hop_with(policy, current, status, location, check_url)
}

/// The PAC fetch's redirect step (FR-065, macOS / Linux): `next_hop`'s status and `Location` rules with
/// `check_pac_url` in place of `check_url`.
#[cfg(not(windows))]
pub(crate) fn next_pac_hop(
    policy: UrlPolicy,
    current: &url::Url,
    status: u16,
    location: Option<&str>,
) -> Result<Hop, HttpError> {
    next_hop_with(policy, current, status, location, check_pac_url)
}

/// `[::1]` → `::1`, a trailing root dot removed, ASCII lower case.
fn normalize_host(host: &str) -> String {
    let host = host.trim();
    let host = host
        .strip_prefix('[')
        .and_then(|h| h.strip_suffix(']'))
        .unwrap_or(host);
    host.trim_end_matches('.').to_ascii_lowercase()
}

/// A bypass entry without its `:port` suffix (`[v6]:p` and `[v6]` unbracketed; a bare IPv6 literal is kept).
fn strip_port(entry: &str) -> &str {
    if let Some(rest) = entry.strip_prefix('[') {
        return rest.split(']').next().unwrap_or(rest);
    }
    match entry.rsplit_once(':') {
        Some((h, p))
            if !h.contains(':') && !p.is_empty() && p.bytes().all(|b| b.is_ascii_digit()) =>
        {
            h
        }
        _ => entry,
    }
}

/// An IPv4 address written with 1 to 4 parts (`169.254` = `169.254.0.0`, the macOS short form).
fn parse_v4_short(s: &str) -> Option<Ipv4Addr> {
    let parts: Vec<&str> = s.split('.').collect();
    if parts.is_empty() || parts.len() > 4 {
        return None;
    }
    let mut octets = [0u8; 4];
    for (slot, part) in octets.iter_mut().zip(&parts) {
        if part.is_empty() || !part.bytes().all(|b| b.is_ascii_digit()) {
            return None;
        }
        *slot = part.parse().ok()?;
    }
    Some(Ipv4Addr::from(octets))
}

fn in_cidr(ip: IpAddr, net: IpAddr, bits: u32) -> bool {
    match (ip, net) {
        (IpAddr::V4(a), IpAddr::V4(n)) if bits <= 32 => {
            let mask = u32::MAX.checked_shl(32 - bits).unwrap_or(0);
            u32::from(a) & mask == u32::from(n) & mask
        }
        (IpAddr::V6(a), IpAddr::V6(n)) if bits <= 128 => {
            let mask = u128::MAX.checked_shl(128 - bits).unwrap_or(0);
            u128::from(a) & mask == u128::from(n) & mask
        }
        _ => false,
    }
}

fn entry_matches(raw: &str, host: &str, ip: Option<IpAddr>) -> bool {
    let entry = raw.trim();
    if entry.is_empty() {
        return false;
    }
    if entry == "*" {
        return true;
    }
    if entry.eq_ignore_ascii_case("<local>") {
        return !host.contains('.');
    }
    let entry = strip_port(entry).trim_end_matches('.').to_ascii_lowercase();
    if let Some((net, bits)) = entry.split_once('/') {
        let Ok(bits) = bits.parse::<u32>() else {
            return false;
        };
        let net = match net.parse::<IpAddr>() {
            Ok(n) => n,
            Err(_) => match parse_v4_short(net) {
                Some(n) => IpAddr::V4(n),
                None => return false,
            },
        };
        return ip.is_some_and(|ip| in_cidr(ip, net, bits));
    }
    if let Some(prefix) = entry.strip_suffix(".*")
        && let Some(net) = parse_v4_short(prefix)
    {
        let parts = u32::try_from(prefix.split('.').count()).unwrap_or(4);
        return ip.is_some_and(|ip| in_cidr(ip, IpAddr::V4(net), 8 * parts.min(4)));
    }
    if let Ok(entry_ip) = entry.parse::<IpAddr>() {
        return ip == Some(entry_ip);
    }
    if let Some(suffix) = entry.strip_prefix("*.") {
        return host.len() > suffix.len() + 1 && host.ends_with(&format!(".{suffix}"));
    }
    if entry.starts_with('.') {
        return host.len() > entry.len() && host.ends_with(&entry);
    }
    host == entry
}

/// PD-18.
pub fn bypasses(no_proxy: &[String], host: &str) -> bool {
    let host = normalize_host(host);
    if host.is_empty() {
        return false;
    }
    let ip = host.parse::<IpAddr>().ok();
    no_proxy.iter().any(|e| entry_matches(e, &host, ip))
}

/// A usable static proxy URL: `http://` prefixed when it has no scheme, scheme in `PROXY_SCHEMES`, a host.
fn valid_proxy_url(value: &str) -> Option<String> {
    let value = value.trim();
    let full = if value.contains("://") {
        value.to_owned()
    } else {
        format!("http://{value}")
    };
    let parsed = url::Url::parse(&full).ok()?;
    (PROXY_SCHEMES.contains(&parsed.scheme()) && has_host(&parsed)).then_some(full)
}

fn split_list(value: Option<&str>) -> Vec<String> {
    value
        .unwrap_or("")
        .split(',')
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map(str::to_owned)
        .collect()
}

/// PD-5, PD-13, SI-27.
pub fn resolve_proxy(env: &ProxyEnv, system: &SystemProxy, target: &str) -> ProxyResolution {
    let Ok(target_url) = url::Url::parse(target) else {
        warn!(
            "proxy resolution: the target {} does not parse; using a direct connection",
            pac_url_for_log(target)
        );
        return ProxyResolution::default();
    };
    let host = target_url.host_str().unwrap_or("");
    let env_list = split_list(env.no_proxy.as_deref());
    if bypasses(&env_list, host) {
        return ProxyResolution::default();
    }
    let env_value = [&env.https_proxy, &env.all_proxy]
        .into_iter()
        .flatten()
        .find(|v| !v.is_empty());
    if let Some(value) = env_value {
        match valid_proxy_url(value) {
            Some(url) => {
                return ProxyResolution {
                    proxy: Some(ProxyConfig {
                        url,
                        no_proxy: env_list,
                    }),
                    pac: None,
                };
            }
            None => warn!(
                "proxy resolution: the environment proxy {} is not usable; ignored",
                redact_userinfo(value)
            ),
        }
    }
    let pac_url = match &system.pac_url {
        Some(u) if url::Url::parse(u).is_ok() => Some(u.clone()),
        Some(u) => {
            warn!(
                "proxy resolution: the PAC URL {} does not parse; ignored",
                pac_url_for_log(u)
            );
            None
        }
        None => None,
    };
    let pac = (pac_url.is_some() || system.auto_detect).then_some(PacSource {
        auto_detect: system.auto_detect,
        url: pac_url,
    });
    let proxy = system
        .proxy
        .as_ref()
        .and_then(|p| match valid_proxy_url(&p.url) {
            Some(url) => {
                let mut no_proxy = p.no_proxy.clone();
                no_proxy.extend(env_list.iter().cloned());
                (!bypasses(&no_proxy, host)).then_some(ProxyConfig { url, no_proxy })
            }
            None => {
                warn!(
                    "proxy resolution: the OS proxy {} is not usable; ignored",
                    redact_userinfo(&p.url)
                );
                None
            }
        });
    ProxyResolution { proxy, pac }
}

/// PD-12, SI-27.
pub fn config_for_dev_url(
    url: &str,
    resolution: ProxyResolution,
    pac_host: Option<PacHostHandle>,
) -> HttpConfig {
    let loopback = url::Url::parse(url).is_ok_and(|u| is_loopback_host(&u));
    if loopback {
        HttpConfig::loopback(CONNECT_TIMEOUT, TOTAL_TIMEOUT)
    } else {
        HttpConfig::production()
            .with_proxy(resolution.proxy)
            .with_pac(resolution.pac)
            .with_pac_host(pac_host)
    }
}

/// PD-23: a proxy the HTTP client refuses is `E_FEED_URL`, never a direct request.
pub(crate) fn to_ureq_proxy(proxy: &ProxyConfig) -> Result<ureq::Proxy, HttpError> {
    ureq::Proxy::new(&proxy.url).map_err(|_| HttpError::Url {
        url: redact_userinfo(&proxy.url),
        reason: "proxy not accepted by the HTTP client",
    })
}

/// PD-28, SI-20: no PAC fetch on Windows.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum AgentPurpose {
    Feed,
    #[cfg(not(windows))]
    PacFetch,
}

/// SI-20: the workspace's only `config_builder()`.
pub(crate) fn agent_config(
    config: &HttpConfig,
    proxy: Option<ureq::Proxy>,
    purpose: AgentPurpose,
) -> ureq::config::Config {
    ureq::Agent::config_builder()
        .https_only(purpose == AgentPurpose::Feed && config.policy == UrlPolicy::HttpsOnly)
        .max_redirects(0)
        .http_status_as_error(false)
        .proxy(proxy)
        .tls_config(
            ureq::tls::TlsConfig::builder()
                .root_certs(ureq::tls::RootCerts::PlatformVerifier)
                .build(),
        )
        .timeout_connect(Some(config.connect_timeout))
        .timeout_global(Some(config.total_timeout))
        .user_agent(concat!("krate-manager/", env!("CARGO_PKG_VERSION")))
        .accept("application/json")
        .build()
}

/// Which part of a request an error arose in (FR-044).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Phase {
    Call,
    Body,
}

fn wraps_rustls(e: &io::Error) -> bool {
    e.get_ref()
        .is_some_and(|inner| inner.downcast_ref::<rustls::Error>().is_some())
}

/// FR-044.
pub(crate) fn map_error(e: ureq::Error, phase: Phase) -> HttpError {
    use ureq::Error as E;
    let detail = e.to_string();
    match e {
        E::Timeout(_) => HttpError::Timeout,
        E::Tls(_) | E::Rustls(_) | E::TlsRequired => HttpError::Tls { detail },
        E::Io(ref inner) if wraps_rustls(inner) => HttpError::Tls { detail },
        E::RequireHttpsOnly(uri) => HttpError::Url {
            url: redact_userinfo(&uri),
            reason: "the HTTP client requires https",
        },
        E::Io(_) | E::Decompress(..) | E::BodyStalled if phase == Phase::Body => {
            HttpError::Body { detail }
        }
        _ => HttpError::Offline { detail },
    }
}

/// PD-33: the classes after which the answer walk advances to its next entry.
pub(crate) fn is_no_response(e: &ureq::Error, deadline_left: bool) -> bool {
    use io::ErrorKind as K;
    use ureq::Error as E;
    match e {
        E::HostNotFound | E::ConnectionFailed | E::ConnectProxyFailed(_) => true,
        E::Timeout(ureq::Timeout::Resolve | ureq::Timeout::Connect) => deadline_left,
        E::Io(inner) => {
            matches!(
                inner.kind(),
                K::ConnectionRefused
                    | K::ConnectionReset
                    | K::ConnectionAborted
                    | K::HostUnreachable
                    | K::NetworkUnreachable
            ) && !wraps_rustls(inner)
        }
        _ => false,
    }
}

/// The PAC state of one `get` call (§4.5.1).
enum PacState {
    /// macOS / Linux: the in-process engine (R-8).
    #[cfg(not(windows))]
    Engine {
        session: pac::PacSession,
        source_for_log: String,
    },
    /// The OS evaluates (R-15).
    Os {
        host: PacHostHandle,
        source: PacSource,
        source_for_log: String,
    },
    /// The fallback for every remaining hop of this call.
    Failed,
}

/// FR-068: exactly one line per PAC failure.
fn warn_pac_failure(code: PacCode, source_for_log: &str, cause: &str, has_static_proxy: bool) {
    let using = if has_static_proxy {
        "the static proxy"
    } else {
        "a direct connection"
    };
    warn!(
        target: pac::LOG_TARGET,
        "{}: {source_for_log}: {cause}; using {using}",
        code.as_str()
    );
}

/// Opened once per `get`, before the first hop.
fn open_pac(config: &HttpConfig, pac: &PacSource, deadline: Instant) -> PacState {
    if let Some(host) = &config.pac_host
        && host.get().os_evaluates()
    {
        return PacState::Os {
            host: host.clone(),
            source: pac.clone(),
            source_for_log: pac
                .url
                .as_deref()
                .map_or_else(|| "WPAD".to_owned(), pac_url_for_log),
        };
    }
    open_engine(config, pac, deadline)
}

/// Windows has no in-process engine (R-15): reached only without a PAC host.
#[cfg(windows)]
fn open_engine(config: &HttpConfig, pac: &PacSource, _deadline: Instant) -> PacState {
    let source_for_log = pac
        .url
        .as_deref()
        .map_or_else(|| "WPAD".to_owned(), pac_url_for_log);
    warn_pac_failure(
        PacCode::Fetch,
        &source_for_log,
        "no PAC evaluator",
        config.proxy.is_some(),
    );
    PacState::Failed
}

/// macOS / Linux: WPAD candidates, then `pac.url`; the first that fetches and compiles wins (FR-065, FR-066).
#[cfg(not(windows))]
fn open_engine(config: &HttpConfig, pac: &PacSource, deadline: Instant) -> PacState {
    let host = config
        .pac_host
        .clone()
        .unwrap_or_else(|| PacHostHandle::new(Arc::new(pac::InertPacHost)));
    let now = Instant::now();
    let phase_deadline = (now + PAC_PHASE_BUDGET).min(deadline);
    let mut candidates = Vec::new();
    if pac.auto_detect {
        candidates.extend(
            host.get()
                .wpad_candidates(phase_deadline.saturating_duration_since(now)),
        );
    }
    candidates.extend(pac.url.iter().cloned());
    let mut last = (
        PacCode::Fetch,
        "WPAD".to_owned(),
        "no WPAD candidate".to_owned(),
    );
    for candidate in candidates {
        let for_log = pac_url_for_log(&candidate);
        match fetch_script(config, &candidate, phase_deadline) {
            Err(cause) => last = (PacCode::Fetch, for_log, cause),
            Ok(text) => match pac::PacSession::open(
                &text,
                host.clone(),
                pac::PacLimits::production(),
                phase_deadline,
            ) {
                Ok(session) => {
                    return PacState::Engine {
                        session,
                        source_for_log: for_log,
                    };
                }
                Err(e) => last = (e.code, for_log, e.detail),
            },
        }
    }
    warn_pac_failure(last.0, &last.1, &last.2, config.proxy.is_some());
    PacState::Failed
}

/// One PAC candidate: parsed, `check_pac_url`, fetched direct; the error is the log cause.
#[cfg(not(windows))]
fn fetch_script(config: &HttpConfig, candidate: &str, deadline: Instant) -> Result<String, String> {
    let url = url::Url::parse(candidate).map_err(|_| "the PAC URL does not parse".to_owned())?;
    check_pac_url(config.policy, &url).map_err(|e| e.to_string())?;
    let body = fetch_direct(config, &url, MAX_PAC_BYTES, deadline)
        .map_err(|e| pac::log_text(&e.to_string()))?;
    Ok(String::from_utf8_lossy(&body).into_owned())
}

/// The hop loop of `get` with no proxy, `AgentPurpose::PacFetch` and `next_pac_hop` (PD-28).
#[cfg(not(windows))]
fn fetch_direct(
    config: &HttpConfig,
    url: &url::Url,
    max_bytes: u64,
    deadline: Instant,
) -> Result<Vec<u8>, HttpError> {
    run_chain(
        config,
        url.clone(),
        &[],
        max_bytes,
        deadline,
        AgentPurpose::PacFetch,
        &mut |_| Ok(vec![None]),
    )
}

/// A converted static proxy and the config it came from (for its bypass list).
type StaticProxy<'a> = Option<(&'a ProxyConfig, ureq::Proxy)>;

/// The hop's one entry without a PAC: the static proxy, or `None` when it is absent or bypassed (PD-4).
fn static_route(static_proxy: &StaticProxy<'_>, hop: &url::Url) -> Option<ureq::Proxy> {
    match static_proxy {
        Some((p, converted)) if !bypasses(&p.no_proxy, hop.host_str().unwrap_or("")) => {
            Some(converted.clone())
        }
        _ => None,
    }
}

/// The Windows OS evaluation of one hop (FR-092), bounded (PD-35).
fn os_find(
    host: &PacHostHandle,
    source: &PacSource,
    hop: &url::Url,
    deadline: Instant,
) -> Result<Vec<ProxyChoice>, PacError> {
    let wait = PAC_PHASE_BUDGET.min(deadline.saturating_duration_since(Instant::now()));
    let (h, s, u) = (host.clone(), source.clone(), pac::pac_url_argument(hop));
    let entries = match bounded(move || h.get().os_find_proxy(&s, &u, wait), wait) {
        None => {
            return Err(PacError {
                code: PacCode::Fetch,
                detail: format!("no answer from the OS PAC evaluator within {wait:?}"),
            });
        }
        // The host's text is not ours: escaped and truncated like a script's (§4.15).
        Some(answer) => answer.map_err(|e| PacError {
            code: e.code,
            detail: pac::log_text(&e.detail),
        })?,
    };
    let (text, render_warnings) = pac::os_answer(&entries);
    for w in &render_warnings {
        warn!(target: pac::LOG_TARGET, "{w}");
    }
    let (choices, parse_warnings) = pac::parse_answer(&text)?;
    for w in &parse_warnings {
        warn!(target: pac::LOG_TARGET, "{w}");
    }
    Ok(choices)
}

/// The hop's choice list through the PAC state (§4.5.1); a PAC failure warns once and takes the fallback.
fn pac_route(
    config: &HttpConfig,
    state: &mut PacState,
    hop: &url::Url,
    deadline: Instant,
    static_proxy: &StaticProxy<'_>,
) -> Result<Vec<Option<ureq::Proxy>>, HttpError> {
    let answer = match state {
        #[cfg(not(windows))]
        PacState::Engine {
            session,
            source_for_log,
        } => session
            .find_proxy(hop, (Instant::now() + pac::PAC_EVAL_BUDGET).min(deadline))
            .map_err(|e| (e, source_for_log.clone())),
        PacState::Os {
            host,
            source,
            source_for_log,
        } => os_find(host, source, hop, deadline).map_err(|e| (e, source_for_log.clone())),
        PacState::Failed => return Ok(vec![static_route(static_proxy, hop)]),
    };
    match answer {
        Ok(choices) => {
            let mut entries = Vec::with_capacity(choices.len());
            for choice in &choices {
                match choice {
                    ProxyChoice::Direct => entries.push(None),
                    ProxyChoice::Proxy(p) => {
                        check_proxy(config.policy, p)?;
                        entries.push(Some(to_ureq_proxy(p)?));
                    }
                }
            }
            Ok(entries)
        }
        Err((e, source_for_log)) => {
            warn_pac_failure(e.code, &source_for_log, &e.detail, config.proxy.is_some());
            *state = PacState::Failed;
            Ok(vec![static_route(static_proxy, hop)])
        }
    }
}

pub fn get(
    config: &HttpConfig,
    url: &str,
    headers: &[(&str, &str)],
    max_bytes: u64,
) -> Result<Vec<u8>, HttpError> {
    let deadline = Instant::now() + config.total_timeout;
    let first = url::Url::parse(url).map_err(|_| HttpError::Url {
        url: redact_userinfo(url),
        reason: "the URL does not parse",
    })?;
    check_url(config.policy, &first)?;
    let static_proxy: StaticProxy<'_> = match &config.proxy {
        Some(p) => {
            check_proxy(config.policy, p)?;
            Some((p, to_ureq_proxy(p)?))
        }
        None => None,
    };
    let mut pac_state = config
        .pac
        .as_ref()
        .map(|pac| open_pac(config, pac, deadline));
    let mut route = |hop: &url::Url| match pac_state.as_mut() {
        None => Ok(vec![static_route(&static_proxy, hop)]),
        Some(state) => pac_route(config, state, hop, deadline, &static_proxy),
    };
    run_chain(
        config,
        first,
        headers,
        max_bytes,
        deadline,
        AgentPurpose::Feed,
        &mut route,
    )
}

type Route<'a> = dyn FnMut(&url::Url) -> Result<Vec<Option<ureq::Proxy>>, HttpError> + 'a;

/// The redirect rule of a purpose: `next_hop` for the feed, `next_pac_hop` for the PAC fetch (FR-065).
fn hop_rule(purpose: AgentPurpose) -> NextHop {
    match purpose {
        AgentPurpose::Feed => next_hop,
        #[cfg(not(windows))]
        AgentPurpose::PacFetch => next_pac_hop,
    }
}

/// The request chain under one deadline (PD-3): per hop the route's entries are walked (PD-33), then the purpose's
/// redirect rule.
fn run_chain(
    config: &HttpConfig,
    first: url::Url,
    headers: &[(&str, &str)],
    max_bytes: u64,
    deadline: Instant,
    purpose: AgentPurpose,
    route: &mut Route<'_>,
) -> Result<Vec<u8>, HttpError> {
    let next = hop_rule(purpose);
    let mut hop_url = first;
    let mut redirects = 0u32;
    loop {
        if deadline <= Instant::now() {
            return Err(HttpError::Timeout);
        }
        let entries = route(&hop_url)?;
        let mut response = walk(config, &hop_url, headers, deadline, purpose, entries)?;
        let status = response.status().as_u16();
        let location = response
            .headers()
            .get("location")
            .and_then(|v| v.to_str().ok());
        match next(config.policy, &hop_url, status, location)? {
            Hop::Follow(to) => {
                redirects += 1;
                if redirects > MAX_REDIRECTS {
                    return Err(HttpError::Redirect {
                        reason: "more than 5 redirects",
                    });
                }
                hop_url = to;
            }
            Hop::Done => {
                if status != 200 {
                    return Err(HttpError::Status { status });
                }
                return read_body(&mut response, max_bytes);
            }
        }
    }
}

/// One hop's entries in order; only a no-response error advances (PD-33), each entry at most once.
fn walk(
    config: &HttpConfig,
    hop_url: &url::Url,
    headers: &[(&str, &str)],
    deadline: Instant,
    purpose: AgentPurpose,
    entries: Vec<Option<ureq::Proxy>>,
) -> Result<ureq::http::Response<ureq::Body>, HttpError> {
    let count = entries.len();
    for (index, proxy) in entries.into_iter().enumerate() {
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            return Err(HttpError::Timeout);
        }
        let hop_config = HttpConfig {
            policy: config.policy,
            connect_timeout: config.connect_timeout.min(remaining),
            total_timeout: remaining,
            proxy: None,
            pac: None,
            pac_host: None,
        };
        let agent = ureq::Agent::new_with_config(agent_config(&hop_config, proxy, purpose));
        let mut request = agent.get(hop_url.as_str());
        for (name, value) in headers {
            request = request.header(*name, *value);
        }
        match request.call() {
            Ok(response) => return Ok(response),
            Err(e) => {
                let left = deadline > Instant::now();
                if index + 1 < count && is_no_response(&e, left) {
                    continue;
                }
                return Err(map_error(e, Phase::Call));
            }
        }
    }
    Err(HttpError::Offline {
        detail: "no connection entry to try".to_owned(),
    })
}

/// FR-043: decoded bytes, `take(max_bytes + 1)`, exactly `max_bytes` admitted.
fn read_body(
    response: &mut ureq::http::Response<ureq::Body>,
    max_bytes: u64,
) -> Result<Vec<u8>, HttpError> {
    let mut out = Vec::new();
    response
        .body_mut()
        .as_reader()
        .take(max_bytes.saturating_add(1))
        .read_to_end(&mut out)
        .map_err(|e| map_error(ureq::Error::from(e), Phase::Body))?;
    if out.len() as u64 > max_bytes {
        return Err(HttpError::TooLarge { max: max_bytes });
    }
    Ok(out)
}

/// The real resolver the macOS / Linux PAC host delegates to (PD-29); sockets stay in this file.
#[cfg(not(windows))]
pub fn std_resolve(name: &str, wait: Duration) -> Option<IpAddr> {
    if let Ok(ip) = name.parse::<IpAddr>() {
        return Some(ip);
    }
    let name = name.to_owned();
    bounded(
        move || {
            (name.as_str(), 0)
                .to_socket_addrs()
                .ok()?
                .next()
                .map(|a| a.ip())
        },
        wait,
    )
    .flatten()
}

/// The real local address the macOS / Linux PAC host delegates to (PD-29): a UDP `connect` sends no packet.
#[cfg(not(windows))]
pub fn std_my_ip() -> IpAddr {
    let local = || -> Option<IpAddr> {
        let socket = UdpSocket::bind((Ipv4Addr::UNSPECIFIED, 0)).ok()?;
        socket.connect((Ipv4Addr::new(198, 51, 100, 1), 80)).ok()?;
        Some(socket.local_addr().ok()?.ip())
    };
    local().unwrap_or(IpAddr::V4(Ipv4Addr::LOCALHOST))
}

/// Every OS: the PAC DNS lookups (macOS / Linux) and the Windows OS evaluation call (PD-35) run through it. An
/// unanswered thread is abandoned, never killed; it ends when `f` returns.
pub fn bounded<T: Send + 'static>(
    f: impl FnOnce() -> T + Send + 'static,
    wait: Duration,
) -> Option<T> {
    let (tx, rx) = mpsc::sync_channel(1);
    thread::Builder::new()
        .name("krate-bounded".to_owned())
        .spawn(move || {
            // The caller may have stopped waiting: nobody receives this.
            tx.send(f()).ok();
        })
        .ok()?;
    rx.recv_timeout(wait).ok()
}

#[cfg(test)]
#[path = "http_tests.rs"]
mod tests;
