//! The OS proxy settings: pure parsers and the reader seams, compiled on every OS (FR-049, FR-074, FR-075,
//! FR-095; plan §4.6).

use crate::http::{ProxyConfig, redact_userinfo};
use std::fmt;
use std::io::{ErrorKind, Read};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitStatus, Stdio};
use std::thread;
use std::time::{Duration, Instant};

const LOG_TARGET: &str = "krate_manager_core::sysproxy";

/// What one OS reader found (FR-049).
#[derive(Clone, Default, PartialEq)]
pub struct SystemProxy {
    pub proxy: Option<ProxyConfig>,
    pub pac_url: Option<String>,
    pub auto_detect: bool,
}

impl fmt::Debug for SystemProxy {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("SystemProxy")
            .field("proxy", &self.proxy)
            .field("pac_url", &self.pac_url.as_deref().map(redact_userinfo))
            .field("auto_detect", &self.auto_detect)
            .finish()
    }
}

/// The WinHTTP current-user proxy configuration as owned strings (R-2, SI-9).
#[derive(Clone, Default, PartialEq, Eq)]
pub struct WinIeProxyConfig {
    pub auto_detect: bool,
    pub auto_config_url: Option<String>,
    pub proxy: Option<String>,
    pub proxy_bypass: Option<String>,
}

/// PD-19: the proxy and PAC strings can carry userinfo.
impl fmt::Debug for WinIeProxyConfig {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("WinIeProxyConfig")
            .field("auto_detect", &self.auto_detect)
            .field(
                "auto_config_url",
                &self.auto_config_url.as_deref().map(redact_userinfo),
            )
            .field("proxy", &self.proxy.as_deref().map(redact_userinfo))
            .field("proxy_bypass", &self.proxy_bypass)
            .finish()
    }
}

/// Windows: `proxy` with an `=` takes its `https=` entry (none → no static proxy), else the whole string; `http://`
/// is prefixed when schemeless; the bypass list is `proxy_bypass` split on `;` and whitespace.
pub fn from_windows(raw: &WinIeProxyConfig) -> SystemProxy {
    let proxy = match raw
        .proxy
        .as_deref()
        .map(str::trim)
        .filter(|s| !s.is_empty())
    {
        None => None,
        Some(value) => {
            let Some(entry) = windows_https_entry(value) else {
                return malformed(
                    "the Windows proxy list (lpszProxy) has an entry that is not scheme=host[:port]",
                );
            };
            match entry {
                None => None,
                Some(entry) => match proxy_url(entry) {
                    Some(url) => Some(ProxyConfig {
                        url,
                        no_proxy: split_list(raw.proxy_bypass.as_deref().unwrap_or(""), |c| {
                            c == ';' || c.is_whitespace()
                        }),
                    }),
                    None => {
                        return malformed(
                            "the Windows proxy (lpszProxy) is not a [scheme://]host[:port] value",
                        );
                    }
                },
            }
        }
    };
    SystemProxy {
        proxy,
        pac_url: non_empty(raw.auto_config_url.as_deref()),
        auto_detect: raw.auto_detect,
    }
}

/// `Some(None)`: a per-scheme list without an `https=` entry; `None`: a malformed list.
fn windows_https_entry(value: &str) -> Option<Option<&str>> {
    if !value.contains('=') {
        return Some(Some(value));
    }
    let mut https = None;
    for entry in value
        .split(|c: char| c == ';' || c.is_whitespace())
        .filter(|e| !e.is_empty())
    {
        let (scheme, target) = entry.split_once('=')?;
        if target.is_empty() {
            return None;
        }
        if https.is_none() && scheme.eq_ignore_ascii_case("https") {
            https = Some(target);
        }
    }
    Some(https)
}

/// The WinHTTP structure's four members as raw values (PWSTR = *mut u16), so no windows-sys type enters core.
#[derive(Clone, Copy, Debug)]
pub struct WinIeRaw {
    pub auto_detect: bool,
    pub auto_config_url: *mut u16,
    pub proxy: *mut u16,
    pub proxy_bypass: *mut u16,
}

/// Reads each non-null pointer up to its NUL into `String::from_utf16_lossy`, then calls `free` on it exactly once;
/// null pointers are `None` and never freed. Production passes `GlobalFree`.
///
/// # Safety
///
/// Every non-null pointer is a NUL-terminated UTF-16 buffer, valid until `free` is called on it.
pub unsafe fn take_win_ie(raw: WinIeRaw, free: &mut dyn FnMut(*mut u16)) -> WinIeProxyConfig {
    // SAFETY: the caller guarantees `auto_config_url` is null or a live NUL-terminated UTF-16 buffer.
    let auto_config_url = unsafe { take_wide(raw.auto_config_url, free) };
    // SAFETY: the caller guarantees `proxy` is null or a live NUL-terminated UTF-16 buffer.
    let proxy = unsafe { take_wide(raw.proxy, free) };
    // SAFETY: the caller guarantees `proxy_bypass` is null or a live NUL-terminated UTF-16 buffer.
    let proxy_bypass = unsafe { take_wide(raw.proxy_bypass, free) };
    WinIeProxyConfig {
        auto_detect: raw.auto_detect,
        auto_config_url,
        proxy,
        proxy_bypass,
    }
}

/// # Safety
///
/// `p` is null or a NUL-terminated UTF-16 buffer valid until `free` is called on it.
unsafe fn take_wide(p: *mut u16, free: &mut dyn FnMut(*mut u16)) -> Option<String> {
    if p.is_null() {
        return None;
    }
    // SAFETY: `p` is non-null and, per this function's contract, a live NUL-terminated buffer; it is freed after.
    let s = unsafe { read_wide(p.cast_const()) };
    free(p);
    s
}

/// Reads one NUL-terminated UTF-16 string without freeing it. Null → `None`.
///
/// # Safety
///
/// A non-null pointer is a NUL-terminated UTF-16 buffer valid for the call.
pub unsafe fn read_wide(p: *const u16) -> Option<String> {
    if p.is_null() {
        return None;
    }
    let mut len = 0usize;
    // SAFETY: the buffer is NUL-terminated, so every index up to and including the NUL lies inside it.
    while unsafe { *p.add(len) } != 0 {
        len += 1;
    }
    // SAFETY: the `len` units before the NUL were each just read from the same live buffer.
    let units = unsafe { std::slice::from_raw_parts(p, len) };
    Some(String::from_utf16_lossy(units))
}

/// Linux: the outputs of a bounded run of child commands (PD-17).
#[derive(Debug)]
pub struct BoundedRun {
    pub outputs: Option<Vec<String>>,
    pub abandoned: Vec<u32>,
}

const POLL: Duration = Duration::from_millis(20);

/// Runs each command with stdout piped, polling `try_wait` every 20 ms, for at most `bound` in total (PD-17).
/// `Some(outputs, in order)` when all exited 0 in time; `None` otherwise. A child still running at the bound has its
/// stdout dropped and goes to a detached reaper thread (never killed); its pid is listed in `abandoned`.
pub fn run_bounded(commands: Vec<Command>, bound: Duration) -> BoundedRun {
    let deadline = Instant::now() + bound;
    let failed = |abandoned| BoundedRun {
        outputs: None,
        abandoned,
    };
    let mut outputs = Vec::with_capacity(commands.len());
    for mut command in commands {
        command
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::null());
        let Ok(mut child) = command.spawn() else {
            return failed(Vec::new());
        };
        let Some(status) = wait_until(&mut child, deadline) else {
            let pid = child.id();
            drop(child.stdout.take());
            thread::spawn(move || {
                let _ = child.wait();
            });
            return failed(vec![pid]);
        };
        let mut text = String::new();
        let read = child
            .stdout
            .take()
            .map(|mut out| out.read_to_string(&mut text));
        if !status.success() || !matches!(read, Some(Ok(_))) {
            return failed(Vec::new());
        }
        outputs.push(text);
    }
    BoundedRun {
        outputs: Some(outputs),
        abandoned: Vec::new(),
    }
}

/// The child's exit status if it exits before `deadline`; `None` at the deadline or on a wait error.
fn wait_until(child: &mut Child, deadline: Instant) -> Option<ExitStatus> {
    loop {
        match child.try_wait() {
            Ok(Some(status)) => return Some(status),
            Ok(None) => {}
            Err(_) => return None,
        }
        let now = Instant::now();
        if now >= deadline {
            return None;
        }
        thread::sleep(POLL.min(deadline - now));
    }
}

/// An absolute `config_home` → `<config_home>/kioslaverc`; an unset or relative one (the XDG Base Directory rule
/// ignores relative values) → `<home>/.config/kioslaverc`; no usable (absolute) `home` → `None`.
pub fn kioslaverc_path(config_home: Option<&Path>, home: Option<&Path>) -> Option<PathBuf> {
    match config_home {
        Some(dir) if dir.is_absolute() => Some(dir.join("kioslaverc")),
        _ => home
            .filter(|h| h.is_absolute())
            .map(|h| h.join(".config").join("kioslaverc")),
    }
}

/// A missing file is the default with no warning; a read error is the default with one.
pub fn read_kde(path: &Path) -> SystemProxy {
    match std::fs::read_to_string(path) {
        Ok(text) => from_kde(&text),
        Err(e) if e.kind() == ErrorKind::NotFound => SystemProxy::default(),
        Err(e) => malformed(&format!(
            "the KDE proxy settings file (kioslaverc) cannot be read ({:?})",
            e.kind()
        )),
    }
}

/// A SystemConfiguration value (FR-075): string, integer, boolean, or a list of strings.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ScValue {
    Int(i64),
    Bool(bool),
    Text(String),
    List(Vec<String>),
}

/// Keys as SystemConfiguration names them. An `…Enable` key is on when `Int(1)` or `Bool(true)`; a key of the wrong
/// `ScValue` variant counts as absent.
pub fn from_macos(dict: &[(String, ScValue)]) -> SystemProxy {
    let get = |key: &str| dict.iter().find(|(k, _)| k == key).map(|(_, v)| v);
    let on = |key: &str| matches!(get(key), Some(ScValue::Int(1) | ScValue::Bool(true)));
    let text = |key: &str| match get(key) {
        Some(ScValue::Text(s)) => non_empty(Some(s)),
        _ => None,
    };
    let proxy = match text("HTTPSProxy").filter(|_| on("HTTPSEnable")) {
        None => None,
        Some(host) => {
            let port = match get("HTTPSPort") {
                Some(ScValue::Int(p)) => *p,
                _ => 443,
            };
            let Some(url) = host_port_url(&host, port) else {
                return malformed(
                    "the macOS HTTPS proxy (HTTPSProxy, HTTPSPort) is not a host and a port in 1-65535",
                );
            };
            let no_proxy = match get("ExceptionsList") {
                Some(ScValue::List(items)) => clean_list(items.iter().map(String::as_str)),
                _ => Vec::new(),
            };
            Some(ProxyConfig { url, no_proxy })
        }
    };
    SystemProxy {
        proxy,
        pac_url: text("ProxyAutoConfigURLString").filter(|_| on("ProxyAutoConfigEnable")),
        auto_detect: on("ProxyAutoDiscoveryEnable"),
    }
}

/// Each field is the output text of one GNOME settings read.
#[derive(Clone, Default, PartialEq, Eq)]
pub struct GnomeRaw {
    pub mode: String,
    pub autoconfig_url: String,
    pub ignore_hosts: String,
    pub https_host: String,
    pub https_port: String,
}

/// PD-19: the autoconfig URL can carry userinfo.
impl fmt::Debug for GnomeRaw {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("GnomeRaw")
            .field("mode", &self.mode)
            .field("autoconfig_url", &redact_userinfo(&self.autoconfig_url))
            .field("ignore_hosts", &self.ignore_hosts)
            .field("https_host", &redact_userinfo(&self.https_host))
            .field("https_port", &self.https_port)
            .finish()
    }
}

/// GNOME: `'manual'` with a non-empty https host and a port in 1-65535 → `http://host:port` (port 0 means unset);
/// `'auto'` → `pac_url` from a non-empty autoconfig URL, else `auto_detect`; the ignore-hosts list → bypass.
pub fn from_gnome(raw: &GnomeRaw) -> SystemProxy {
    match gnome(raw) {
        Ok(p) => p,
        Err(field) => malformed(&format!(
            "the GNOME proxy setting {field} is not a valid value of its GVariant type"
        )),
    }
}

fn gnome(raw: &GnomeRaw) -> Result<SystemProxy, &'static str> {
    let mode = gvariant_string(&raw.mode).ok_or("mode")?;
    match mode.as_str() {
        "none" => Ok(SystemProxy::default()),
        "manual" => {
            let host = gvariant_string(&raw.https_host).ok_or("https host")?;
            let port: i64 = raw.https_port.trim().parse().map_err(|_| "https port")?;
            if !(0..=65_535).contains(&port) {
                return Err("https port");
            }
            if host.trim().is_empty() || port == 0 {
                return Ok(SystemProxy::default());
            }
            let url = host_port_url(host.trim(), port).ok_or("https host")?;
            let ignore = gvariant_string_list(&raw.ignore_hosts).ok_or("ignore-hosts")?;
            Ok(SystemProxy {
                proxy: Some(ProxyConfig {
                    url,
                    no_proxy: clean_list(ignore.iter().map(String::as_str)),
                }),
                ..SystemProxy::default()
            })
        }
        "auto" => {
            let url = gvariant_string(&raw.autoconfig_url).ok_or("autoconfig-url")?;
            Ok(match non_empty(Some(&url)) {
                Some(pac_url) => SystemProxy {
                    pac_url: Some(pac_url),
                    ..SystemProxy::default()
                },
                None => SystemProxy {
                    auto_detect: true,
                    ..SystemProxy::default()
                },
            })
        }
        _ => Err("mode"),
    }
}

/// KDE `kioslaverc`, `[Proxy Settings]` group: `ProxyType` 1 → `httpsProxy` (the "host port" form turned into
/// `host:port`, `http://` prefixed when schemeless), 2 → `pac_url` = `Proxy Config Script`, 3 → `auto_detect`,
/// 0 and 4 → none; `NoProxyFor` split on `,`; `ReversedException=true` (an allow-list) is ignored with one warning.
pub fn from_kde(kioslaverc: &str) -> SystemProxy {
    match kde(kioslaverc) {
        Ok(p) => p,
        Err(field) => malformed(&format!(
            "the KDE proxy setting {field} in [Proxy Settings] is not a valid value"
        )),
    }
}

fn kde(text: &str) -> Result<SystemProxy, &'static str> {
    let mut in_group = false;
    let mut keys: Vec<(&str, &str)> = Vec::new();
    for line in text.lines().map(str::trim) {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') {
            in_group = line == "[Proxy Settings]";
        } else if in_group && let Some((k, v)) = line.split_once('=') {
            keys.push((k.trim(), v.trim()));
        }
    }
    // A later line overrides an earlier one.
    let get = |key: &str| keys.iter().rev().find(|(k, _)| *k == key).map(|(_, v)| *v);
    let proxy_type: u8 = match get("ProxyType") {
        None => 0,
        Some(v) => v.parse().map_err(|_| "ProxyType")?,
    };
    match proxy_type {
        0 | 4 => Ok(SystemProxy::default()),
        1 => {
            let Some(value) = non_empty(get("httpsProxy")) else {
                return Ok(SystemProxy::default());
            };
            let joined = match value.rsplit_once(' ') {
                Some((host, port)) => format!("{}:{}", host.trim_end(), port),
                None => value,
            };
            let url = proxy_url(&joined).ok_or("httpsProxy")?;
            let mut no_proxy = clean_list(get("NoProxyFor").unwrap_or("").split(','));
            if get("ReversedException").is_some_and(|v| v.eq_ignore_ascii_case("true")) {
                log::warn!(
                    target: LOG_TARGET,
                    "the KDE proxy setting ReversedException=true makes NoProxyFor an allow-list, which the bypass \
                     matcher cannot express (FR-049); NoProxyFor is ignored"
                );
                no_proxy.clear();
            }
            Ok(SystemProxy {
                proxy: Some(ProxyConfig { url, no_proxy }),
                ..SystemProxy::default()
            })
        }
        2 => Ok(SystemProxy {
            pac_url: non_empty(get("Proxy Config Script")),
            ..SystemProxy::default()
        }),
        3 => Ok(SystemProxy {
            auto_detect: true,
            ..SystemProxy::default()
        }),
        _ => Err("ProxyType"),
    }
}

/// R-4, FR-074: `true` iff an element of the `:`-split value is exactly `KDE`.
pub fn linux_uses_kde(current_desktop: Option<&str>) -> bool {
    current_desktop.is_some_and(|d| d.split(':').any(|e| e == "KDE"))
}

/// One GVariant text-format string (`'…'` or `"…"`, with backslash escapes) and nothing else but whitespace.
pub(crate) fn gvariant_string(s: &str) -> Option<String> {
    let (value, rest) = gvariant_str_prefix(s.trim())?;
    rest.trim().is_empty().then_some(value)
}

/// A GVariant text-format string array: `['a', 'b']`, `[]` or the typed empty form `@as []`.
pub(crate) fn gvariant_string_list(s: &str) -> Option<Vec<String>> {
    let s = s.trim();
    let s = s.strip_prefix("@as").map_or(s, str::trim_start);
    let mut rest = s.strip_prefix('[')?.trim_start();
    let mut items = Vec::new();
    if let Some(after) = rest.strip_prefix(']') {
        return after.trim().is_empty().then_some(items);
    }
    loop {
        let (item, after) = gvariant_str_prefix(rest)?;
        items.push(item);
        let after = after.trim_start();
        if let Some(next) = after.strip_prefix(',') {
            rest = next.trim_start();
        } else {
            let end = after.strip_prefix(']')?;
            return end.trim().is_empty().then_some(items);
        }
    }
}

/// Parses one quoted GVariant string at the start of `s`; returns it and the text after its closing quote.
fn gvariant_str_prefix(s: &str) -> Option<(String, &str)> {
    let mut chars = s.char_indices();
    let (_, quote) = chars.next()?;
    if quote != '\'' && quote != '"' {
        return None;
    }
    let mut out = String::new();
    while let Some((i, c)) = chars.next() {
        if c == quote {
            return Some((out, &s[i + c.len_utf8()..]));
        }
        if c != '\\' {
            out.push(c);
            continue;
        }
        let (_, e) = chars.next()?;
        let unescaped = match e {
            'a' => '\u{7}',
            'b' => '\u{8}',
            'f' => '\u{c}',
            'n' => '\n',
            'r' => '\r',
            't' => '\t',
            'v' => '\u{b}',
            '\\' | '\'' | '"' => e,
            'u' | 'U' => {
                let digits = if e == 'u' { 4 } else { 8 };
                let mut code = 0u32;
                for _ in 0..digits {
                    code = code * 16 + chars.next()?.1.to_digit(16)?;
                }
                char::from_u32(code)?
            }
            _ => return None,
        };
        out.push(unescaped);
    }
    None
}

/// The largest UTC offset kept, ±14 h (PD-31).
const MAX_OFFSET_SECS: i32 = 14 * 3600;

/// A `+hhmm` / `-hhmm` offset with one trailing LF, in seconds; hh ≤ 14, mm ≤ 59, anything else `None`
/// (PD-31, R-13).
pub fn parse_utc_offset(s: &str) -> Option<i32> {
    let s = s.strip_suffix('\n').unwrap_or(s);
    let &[sign, h1, h2, m1, m2] = s.as_bytes() else {
        return None;
    };
    let sign = match sign {
        b'+' => 1,
        b'-' => -1,
        _ => return None,
    };
    let digit = |b: u8| b.is_ascii_digit().then(|| i32::from(b - b'0'));
    let hh = digit(h1)? * 10 + digit(h2)?;
    let mm = digit(m1)? * 10 + digit(m2)?;
    (hh <= 14 && mm <= 59).then_some(sign * (hh * 3600 + mm * 60))
}

/// A macOS time-zone offset in seconds, kept only when finite and within ±14 h (PD-31).
pub fn cf_offset_secs(seconds: f64) -> Option<i32> {
    if !seconds.is_finite() {
        return None;
    }
    let rounded = seconds.round();
    // In range, so the cast is exact.
    (rounded.abs() <= f64::from(MAX_OFFSET_SECS)).then_some(rounded as i32)
}

/// One `warn!` naming the field and the rule (never the value, FR-056), and no OS proxy.
fn malformed(what: &str) -> SystemProxy {
    log::warn!(target: LOG_TARGET, "{what} (FR-049); using no OS proxy settings");
    SystemProxy::default()
}

fn non_empty(s: Option<&str>) -> Option<String> {
    s.map(str::trim)
        .filter(|s| !s.is_empty())
        .map(str::to_owned)
}

fn split_list(s: &str, sep: impl Fn(char) -> bool) -> Vec<String> {
    clean_list(s.split(sep))
}

fn clean_list<'a>(items: impl Iterator<Item = &'a str>) -> Vec<String> {
    items
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map(str::to_owned)
        .collect()
}

/// `http://<host>:<port>` for a bare host (an IPv6 literal bracketed), or `None` when either is invalid.
fn host_port_url(host: &str, port: i64) -> Option<String> {
    let host = if host.contains(':') && !host.starts_with('[') {
        format!("[{host}]")
    } else {
        host.to_owned()
    };
    proxy_url(&format!("{host}:{port}"))
}

/// `value` with `http://` prefixed when it has no scheme, if it is `scheme://[userinfo@]host[:port][/]`.
fn proxy_url(value: &str) -> Option<String> {
    let url = if value.contains("://") {
        value.to_owned()
    } else {
        format!("http://{value}")
    };
    let (scheme, rest) = url.split_once("://")?;
    let scheme_ok = scheme.starts_with(|c: char| c.is_ascii_alphabetic())
        && scheme
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '+' | '-' | '.'));
    let authority = rest.strip_suffix('/').unwrap_or(rest);
    let host_port = authority.rsplit_once('@').map_or(authority, |(_, hp)| hp);
    (scheme_ok && host_port_ok(host_port)).then_some(url)
}

/// `host[:port]` or `[v6][:port]`; a port is 1-65535.
fn host_port_ok(s: &str) -> bool {
    let (host, port, v6) = match s.strip_prefix('[') {
        Some(inner) => {
            let Some((host, after)) = inner.split_once(']') else {
                return false;
            };
            let port = match after {
                "" => None,
                _ => match after.strip_prefix(':') {
                    Some(p) => Some(p),
                    None => return false,
                },
            };
            (host, port, true)
        }
        None => match s.split_once(':') {
            Some((host, port)) => (host, Some(port), false),
            None => (s, None, false),
        },
    };
    let host_ok = !host.is_empty()
        && host.chars().all(|c| {
            if v6 {
                c.is_ascii_hexdigit() || matches!(c, ':' | '.')
            } else {
                c.is_alphanumeric() || matches!(c, '-' | '.' | '_')
            }
        });
    let port_ok = port.is_none_or(|p| {
        p.bytes().all(|b| b.is_ascii_digit()) && p.parse::<u16>().is_ok_and(|n| n != 0)
    });
    host_ok && port_ok
}

#[cfg(test)]
#[path = "sysproxy_tests.rs"]
mod tests;
