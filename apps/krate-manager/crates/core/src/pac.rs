//! PAC (D16, FR-064 to FR-068, FR-092; plan §4.15). The only file that names rquickjs (FR-081); every item that does
//! is #[cfg(not(windows))]: on Windows the OS evaluates (R-15) and only the shared pure half below is compiled.

use crate::http::{PacSource, ProxyConfig};
#[cfg(not(windows))]
use std::cell::{Cell, OnceCell, RefCell};
#[cfg(not(windows))]
use std::collections::HashMap;
use std::fmt;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
#[cfg(not(windows))]
use std::rc::Rc;
use std::sync::Arc;
use std::time::Duration;
#[cfg(not(windows))]
use std::time::Instant;

// ---- shared, every OS -------------------------------------------------------------------------------------------

/// FR-065 (the macOS / Linux PAC fetch).
pub const MAX_PAC_BYTES: u64 = 1024 * 1024;
/// FR-068, per compile and per call (engine).
pub const PAC_EVAL_BUDGET: Duration = Duration::from_secs(2);
/// PD-27: engine discovery + fetch + compile; Windows: one OS call.
pub const PAC_PHASE_BUDGET: Duration = Duration::from_secs(10);
/// PD-27 (engine).
pub const PAC_MEMORY_LIMIT: usize = 32 * 1024 * 1024;
/// PD-27 (engine).
pub const PAC_STACK_LIMIT: usize = 256 * 1024;
/// PD-27, SI-29 (both evaluators, through the answer parser).
pub const MAX_PAC_ANSWER_BYTES: usize = 64 * 1024;
/// PD-27, SI-29 (both evaluators).
pub const MAX_PAC_ENTRIES: usize = 16;
/// PD-29, FR-094 (engine).
pub const PAC_DNS_WAIT: Duration = Duration::from_secs(1);
/// PD-29, FR-094 (engine).
pub const PAC_MAX_LOOKUPS: usize = 16;
/// PD-30, R-9.
pub const WPAD_DNS_URL: &str = "http://wpad/wpad.dat";
/// FR-068 names this target.
pub(crate) const LOG_TARGET: &str = "krate_manager_core::http";

/// The PAC clock (PD-31, FR-095).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PacClock {
    pub unix_secs: i64,
    pub local_offset_secs: Option<i32>,
}

/// One entry of the Windows OS answer (a WINHTTP_PROXY_RESULT_ENTRY, PD-35), as plain data: no windows-sys type in
/// core.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OsProxyKind {
    Direct,
    Http,
    Https,
    Socks,
    Other(i32),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OsProxyEntry {
    pub kind: OsProxyKind,
    pub host: String,
    pub port: u16,
}

/// The machine access of the PAC engine and the Windows OS evaluation behind one seam (SI-25, PD-32); every method
/// has the inert default.
pub trait PacHost: Send + Sync {
    /// Engine.
    fn resolve(&self, _name: &str, _wait: Duration) -> Option<IpAddr> {
        None
    }
    /// Engine.
    fn my_ip(&self) -> IpAddr {
        IpAddr::V4(Ipv4Addr::LOCALHOST)
    }
    /// Engine.
    fn now(&self) -> PacClock {
        PacClock {
            unix_secs: 0,
            local_offset_secs: None,
        }
    }
    /// Engine.
    fn wpad_candidates(&self, _wait: Duration) -> Vec<String> {
        Vec::new()
    }
    /// FR-092.
    fn os_evaluates(&self) -> bool {
        false
    }
    /// FR-092.
    fn os_find_proxy(
        &self,
        _source: &PacSource,
        _url: &str,
        _wait: Duration,
    ) -> Result<Vec<OsProxyEntry>, PacError> {
        Err(PacError {
            code: PacCode::Fetch,
            detail: "no OS PAC evaluator".to_owned(),
        })
    }
}

/// The host as `HttpConfig` carries it (PD-32): `Debug` prints `PacHostHandle`, equality is identity.
#[derive(Clone)]
pub struct PacHostHandle(Arc<dyn PacHost>);

impl PacHostHandle {
    pub fn new(host: Arc<dyn PacHost>) -> Self {
        PacHostHandle(host)
    }

    pub fn get(&self) -> &dyn PacHost {
        &*self.0
    }
}

impl fmt::Debug for PacHostHandle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("PacHostHandle")
    }
}

impl PartialEq for PacHostHandle {
    fn eq(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }
}

/// Every default (PD-32): it reads no clock.
pub struct InertPacHost;

impl PacHost for InertPacHost {}

/// One entry of a PAC answer; a proxy's `no_proxy` is empty.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ProxyChoice {
    Direct,
    Proxy(ProxyConfig),
}

/// The FR-068 log codes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PacCode {
    Fetch,
    Script,
    Result,
}

impl PacCode {
    pub fn as_str(self) -> &'static str {
        match self {
            PacCode::Fetch => "E_PAC_FETCH",
            PacCode::Script => "E_PAC_SCRIPT",
            PacCode::Result => "E_PAC_RESULT",
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
#[error("{}: {detail}", .code.as_str())]
pub struct PacError {
    pub code: PacCode,
    pub detail: String,
}

/// `[WPAD_DNS_URL]`: the real macOS / Linux candidate list (PD-30). It takes no host name, so it cannot depend on
/// one: no DNS devolution, no DHCP.
pub fn wpad_dns_candidates() -> Vec<String> {
    vec![WPAD_DNS_URL.to_owned()]
}

/// The one answer parser of both evaluators (FR-067, PD-27, PD-33, PD-36): `(choices, warnings)`, each warning an
/// `E_PAC_RESULT` line whose script text went through `log_text`. The caller logs the warnings.
pub(crate) fn parse_answer(answer: &str) -> Result<(Vec<ProxyChoice>, Vec<String>), PacError> {
    if answer.len() > MAX_PAC_ANSWER_BYTES {
        return Err(PacError {
            code: PacCode::Result,
            detail: format!(
                "the PAC answer is {} bytes, more than {MAX_PAC_ANSWER_BYTES}",
                answer.len()
            ),
        });
    }
    let mut choices = Vec::new();
    let mut warnings = Vec::new();
    for raw in answer.split(';') {
        let entry = raw.trim();
        if entry.is_empty() {
            continue;
        }
        if choices.len() == MAX_PAC_ENTRIES {
            warnings.push(format!(
                "E_PAC_RESULT: PAC answer entries after the first {MAX_PAC_ENTRIES} dropped"
            ));
            break;
        }
        match parse_entry(entry) {
            Ok(choice) => choices.push(choice),
            Err(reason) => warnings.push(format!(
                "E_PAC_RESULT: PAC answer entry \"{}\" skipped: {reason}",
                log_text(entry)
            )),
        }
    }
    if choices.is_empty() {
        return Err(PacError {
            code: PacCode::Result,
            detail: format!("no usable entry in the PAC answer \"{}\"", log_text(answer)),
        });
    }
    Ok((choices, warnings))
}

/// One trimmed, non-empty answer entry (FR-067).
fn parse_entry(entry: &str) -> Result<ProxyChoice, &'static str> {
    let mut words = entry.split_whitespace();
    let keyword = words.next().ok_or("empty entry")?;
    let argument = words.next();
    if words.next().is_some() {
        return Err("extra words");
    }
    let is = |name: &str| keyword.eq_ignore_ascii_case(name);
    let (scheme, default_port) = if is("DIRECT") {
        return match argument {
            None => Ok(ProxyChoice::Direct),
            Some(_) => Err("DIRECT takes no argument"),
        };
    } else if is("PROXY") || is("HTTP") {
        ("http", 80)
    } else if is("HTTPS") {
        ("https", 443)
    } else if is("SOCKS") || is("SOCKS4") {
        ("socks4", 1080)
    } else if is("SOCKS5") {
        ("socks5", 1080)
    } else {
        return Err("unknown keyword");
    };
    let (host, port) = parse_host_port(argument.ok_or("no host")?, default_port)?;
    Ok(ProxyChoice::Proxy(ProxyConfig {
        url: format!("{scheme}://{host}:{port}"),
        no_proxy: Vec::new(),
    }))
}

/// `host[:port]` or `[v6][:port]`; the host is a DNS name or IPv4 literal of `[A-Za-z0-9._-]`, or a bracketed IPv6
/// literal (returned with its brackets); the port 1-65535, else `default_port`.
fn parse_host_port(s: &str, default_port: u16) -> Result<(String, u16), &'static str> {
    let (host, port) = if let Some(rest) = s.strip_prefix('[') {
        let (inside, after) = rest.split_once(']').ok_or("unclosed IPv6 bracket")?;
        inside
            .parse::<Ipv6Addr>()
            .map_err(|_| "not an IPv6 literal")?;
        let port = match after {
            "" => None,
            _ => Some(after.strip_prefix(':').ok_or("bad port")?),
        };
        (format!("[{inside}]"), port)
    } else {
        let (host, port) = match s.split_once(':') {
            Some((host, port)) => (host, Some(port)),
            None => (s, None),
        };
        if port.is_some_and(|p| p.contains(':')) {
            return Err("an IPv6 address must be bracketed");
        }
        if host.is_empty() {
            return Err("no host");
        }
        if !host
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'.' | b'-' | b'_'))
        {
            return Err("bad host");
        }
        (host.to_owned(), port)
    };
    let port = match port {
        None => default_port,
        Some(p) => {
            if p.is_empty() || p.len() > 5 || !p.bytes().all(|b| b.is_ascii_digit()) {
                return Err("bad port");
            }
            match p.parse::<u16>() {
                Ok(port) if port != 0 => port,
                _ => return Err("port out of range"),
            }
        }
    };
    Ok((host, port))
}

/// PD-36: the Windows entry list rendered as PAC answer text for `parse_answer`, plus one warning per skipped entry.
pub(crate) fn os_answer(entries: &[OsProxyEntry]) -> (String, Vec<String>) {
    let mut parts = Vec::new();
    let mut warnings = Vec::new();
    for entry in entries {
        let keyword = match entry.kind {
            OsProxyKind::Direct => {
                parts.push("DIRECT".to_owned());
                continue;
            }
            OsProxyKind::Http => "PROXY",
            OsProxyKind::Https => "HTTPS",
            OsProxyKind::Socks => "SOCKS",
            OsProxyKind::Other(scheme) => {
                warnings.push(format!(
                    "E_PAC_RESULT: OS proxy entry of scheme {scheme} skipped"
                ));
                continue;
            }
        };
        if entry.host.is_empty() {
            warnings.push("E_PAC_RESULT: OS proxy entry with no host skipped".to_owned());
            continue;
        }
        if entry
            .host
            .chars()
            .any(|c| c == ';' || c.is_whitespace() || c.is_control())
        {
            warnings.push(format!(
                "E_PAC_RESULT: OS proxy entry \"{}\" skipped",
                log_text(&entry.host)
            ));
            continue;
        }
        let host = if entry.host.contains(':') && !entry.host.starts_with('[') {
            format!("[{}]", entry.host)
        } else {
            entry.host.clone()
        };
        parts.push(match entry.port {
            0 => format!("{keyword} {host}"),
            port => format!("{keyword} {host}:{port}"),
        });
    }
    (parts.join("; "), warnings)
}

/// FR-064's URL argument for both evaluators: an `https` hop → `scheme://host[:port]/` (path, query, fragment and
/// userinfo dropped), any other hop → the URL as is.
pub(crate) fn pac_url_argument(url: &url::Url) -> String {
    if url.scheme() != "https" {
        return url.as_str().to_owned();
    }
    let host = url.host_str().unwrap_or_default();
    match url.port() {
        Some(port) => format!("https://{host}:{port}/"),
        None => format!("https://{host}/"),
    }
}

/// Script- or OS-derived text for a log line (plan §4.15): `escape_debug`, then at most 200 `char`s and `…`.
pub(crate) fn log_text(s: &str) -> String {
    const MAX_CHARS: usize = 200;
    let mut out = String::new();
    for (count, c) in s.escape_debug().enumerate() {
        if count == MAX_CHARS {
            out.push('…');
            break;
        }
        out.push(c);
    }
    out
}

/// The broken-down time the time helpers read (FR-095, PD-31): `gmt` → the UTC fields; else the local fields, and
/// an unknown offset → the UTC fields plus one `warn!` per `warned` flag (one per session).
pub fn clock_civil(clock: PacClock, gmt: bool, warned: &mut bool) -> helpers::Civil {
    if gmt {
        return helpers::civil(clock.unix_secs);
    }
    match clock.local_offset_secs {
        Some(offset) => helpers::civil(clock.unix_secs.saturating_add(i64::from(offset))),
        None => {
            if !*warned {
                *warned = true;
                log::warn!(
                    target: LOG_TARGET,
                    "PAC local time: the OS local UTC offset is unreadable; the time helpers use UTC (FR-095)"
                );
            }
            helpers::civil(clock.unix_secs)
        }
    }
}

/// The PAC helpers, pure, tested without the engine (PD-26). Definitions from MDN's PAC page (plan §1.2): a reversed
/// range matches only its two endpoints (SI-35). `None` = bad or over-length arguments (the engine throws).
pub mod helpers {
    use super::PacHost;
    use std::net::{IpAddr, Ipv4Addr};
    use std::time::Duration;

    /// A broken-down date (`weekday` 0 = Sunday, `month` 1-12).
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub struct Civil {
        pub year: i64,
        pub month: u32,
        pub day: u32,
        pub weekday: u32,
        pub hour: u32,
        pub minute: u32,
        pub second: u32,
    }

    /// A helper argument (number or string).
    #[derive(Clone, Debug, PartialEq)]
    pub enum Arg {
        Num(f64),
        Text(String),
    }

    /// Bytes of shExpMatch's `s`.
    pub const MAX_SHEXP_SUBJECT: usize = 4096;
    /// Bytes of shExpMatch's `pat`.
    pub const MAX_SHEXP_PATTERN: usize = 1024;
    /// Bytes of every host / domain argument (a DNS name's text).
    pub const MAX_HOST_ARG: usize = 253;

    const WEEKDAYS: [&str; 7] = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"];
    const MONTHS: [&str; 12] = [
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    ];

    fn host_ok(h: &str) -> Option<()> {
        (h.len() <= MAX_HOST_ARG).then_some(())
    }

    pub fn is_plain_host_name(h: &str) -> Option<bool> {
        host_ok(h)?;
        Some(!h.contains('.'))
    }

    pub fn dns_domain_is(h: &str, d: &str) -> Option<bool> {
        host_ok(h)?;
        host_ok(d)?;
        Some(h.ends_with(d))
    }

    /// Exact match, or `h` is `hd`'s first labels (`"www"` vs `"www.mozilla.org"`).
    pub fn local_host_or_domain_is(h: &str, hd: &str) -> Option<bool> {
        host_ok(h)?;
        host_ok(hd)?;
        Some(
            h == hd
                || (hd.len() > h.len()
                    && hd.starts_with(h)
                    && hd.as_bytes().get(h.len()) == Some(&b'.')),
        )
    }

    pub fn dns_domain_levels(h: &str) -> Option<u32> {
        host_ok(h)?;
        // At most 253 dots, so the count fits.
        u32::try_from(h.bytes().filter(|&b| b == b'.').count()).ok()
    }

    /// The iterative greedy wildcard matcher with one backtrack point (plan §4.15): `*` any run, `?` one byte; no
    /// recursion, no allocation, at most `len(s) · len(pat)` steps under the caps.
    pub fn sh_exp_match(s: &str, pat: &str) -> Option<bool> {
        if s.len() > MAX_SHEXP_SUBJECT || pat.len() > MAX_SHEXP_PATTERN {
            return None;
        }
        let (s, p) = (s.as_bytes(), pat.as_bytes());
        let (mut i, mut j) = (0, 0);
        // (the star's pattern index, the subject index it was recorded at)
        let mut star: Option<(usize, usize)> = None;
        while i < s.len() {
            if p.get(j) == Some(&b'*') {
                star = Some((j, i));
                j += 1;
            } else if p.get(j).is_some_and(|&c| c == b'?' || c == s[i]) {
                i += 1;
                j += 1;
            } else if let Some((star_j, star_i)) = star {
                j = star_j + 1;
                i = star_i + 1;
                star = Some((star_j, star_i + 1));
            } else {
                return Some(false);
            }
        }
        while p.get(j) == Some(&b'*') {
            j += 1;
        }
        Some(j == p.len())
    }

    pub fn is_in_net(ip: Ipv4Addr, pattern: Ipv4Addr, mask: Ipv4Addr) -> bool {
        let mask = u32::from(mask);
        u32::from(ip) & mask == u32::from(pattern) & mask
    }

    /// An IPv4 literal answers itself and an IPv6 literal is `None`, both without a lookup; else the host's answer,
    /// IPv4 only (an IPv6-only answer is `None`).
    pub fn dns_resolve(host: &dyn PacHost, name: &str, wait: Duration) -> Option<Ipv4Addr> {
        match name.parse::<IpAddr>() {
            Ok(IpAddr::V4(ip)) => Some(ip),
            Ok(IpAddr::V6(_)) => None,
            Err(_) => match host.resolve(name, wait) {
                Some(IpAddr::V4(ip)) => Some(ip),
                _ => None,
            },
        }
    }

    /// An IPv6 answer → `127.0.0.1` (MDN's fallback).
    pub fn my_ip_address(host: &dyn PacHost) -> Ipv4Addr {
        match host.my_ip() {
            IpAddr::V4(ip) => ip,
            IpAddr::V6(_) => Ipv4Addr::LOCALHOST,
        }
    }

    /// True when the last argument is `"GMT"`: the caller then passes the UTC fields.
    pub fn wants_gmt(args: &[Arg]) -> bool {
        matches!(args.last(), Some(Arg::Text(t)) if t == "GMT")
    }

    fn without_gmt(args: &[Arg]) -> &[Arg] {
        if wants_gmt(args) {
            &args[..args.len() - 1]
        } else {
            args
        }
    }

    /// `a..=b` forward, only `a` and `b` reversed (SI-35); `a == b` is that one value.
    fn in_range<K: Ord>(now: K, a: K, b: K) -> bool {
        if a <= b {
            a <= now && now <= b
        } else {
            now == a || now == b
        }
    }

    fn weekday(arg: &Arg) -> Option<u32> {
        match arg {
            Arg::Text(t) => (0u32..)
                .zip(WEEKDAYS)
                .find_map(|(i, name)| (name == t.as_str()).then_some(i)),
            Arg::Num(_) => None,
        }
    }

    fn integer(arg: &Arg, min: f64, max: f64) -> Option<i64> {
        match *arg {
            // A whole number inside [min, max], both well inside i64: the cast is exact.
            Arg::Num(x) if x.fract() == 0.0 && x >= min && x <= max => Some(x as i64),
            _ => None,
        }
    }

    fn small(arg: &Arg, min: u32, max: u32) -> Option<u32> {
        integer(arg, f64::from(min), f64::from(max)).and_then(|v| u32::try_from(v).ok())
    }

    /// `weekdayRange(wd1[, wd2][, "GMT"])`.
    pub fn weekday_range(now: &Civil, args: &[Arg]) -> Option<bool> {
        match without_gmt(args) {
            [one] => Some(now.weekday == weekday(one)?),
            [a, b] => Some(in_range(now.weekday, weekday(a)?, weekday(b)?)),
            _ => None,
        }
    }

    /// One `dateRange` specifier.
    #[derive(Clone, Copy)]
    enum DatePart {
        Day(u32),
        Month(u32),
        Year(i64),
    }

    impl DatePart {
        fn rank(self) -> u8 {
            match self {
                DatePart::Day(_) => 0,
                DatePart::Month(_) => 1,
                DatePart::Year(_) => 2,
            }
        }
    }

    /// A number up to 31 is a day, above 31 a year (MDN: "ambiguity is resolved by assuming year is greater than
    /// 31"); a month is its three-letter name.
    fn date_part(arg: &Arg) -> Option<DatePart> {
        match arg {
            Arg::Text(t) => (1u32..)
                .zip(MONTHS)
                .find_map(|(i, name)| (name == t.as_str()).then_some(DatePart::Month(i))),
            Arg::Num(_) => match integer(arg, 1.0, 1.0e9)? {
                day @ 1..=31 => u32::try_from(day).ok().map(DatePart::Day),
                year => Some(DatePart::Year(year)),
            },
        }
    }

    /// A date point: consecutive specifiers in the order day, month, year (`24, "DEC"`, `"OCT", 1995`,
    /// `1, "JUN", 1995`).
    fn date_point(args: &[Arg]) -> Option<Vec<DatePart>> {
        let parts = args.iter().map(date_part).collect::<Option<Vec<_>>>()?;
        let consecutive = parts.windows(2).all(|w| w[1].rank() == w[0].rank() + 1);
        (!parts.is_empty() && consecutive).then_some(parts)
    }

    /// The point's values in significance order (year, month, day).
    fn point_key(point: &[DatePart]) -> Vec<i64> {
        point
            .iter()
            .rev()
            .map(|part| match *part {
                DatePart::Day(v) | DatePart::Month(v) => i64::from(v),
                DatePart::Year(y) => y,
            })
            .collect()
    }

    /// `now`'s fields of the point's shape, in significance order.
    fn now_key(shape: &[DatePart], now: &Civil) -> Vec<i64> {
        shape
            .iter()
            .rev()
            .map(|part| match part {
                DatePart::Day(_) => i64::from(now.day),
                DatePart::Month(_) => i64::from(now.month),
                DatePart::Year(_) => now.year,
            })
            .collect()
    }

    /// `dateRange(...)` in every MDN form: one point, or two points of the same shape (inclusive of both end units).
    pub fn date_range(now: &Civil, args: &[Arg]) -> Option<bool> {
        let args = without_gmt(args);
        if args.len() > 6 {
            return None;
        }
        if let Some(point) = date_point(args) {
            return Some(now_key(&point, now) == point_key(&point));
        }
        if !args.len().is_multiple_of(2) {
            return None;
        }
        let (first, second) = args.split_at(args.len() / 2);
        let (a, b) = (date_point(first)?, date_point(second)?);
        let same_shape = a.len() == b.len() && a.iter().zip(&b).all(|(x, y)| x.rank() == y.rank());
        if !same_shape {
            return None;
        }
        Some(in_range(now_key(&a, now), point_key(&a), point_key(&b)))
    }

    /// `timeRange(...)`: `(h)` that hour; `(h1, h2)`, `(h1, m1, h2, m2)`, `(h1, m1, s1, h2, m2, s2)` from the first
    /// instant to the second (MDN: `timeRange(12, 13)` "from noon to 1pm"); a reversed (or equal) pair matches only
    /// the hour, minute or second of its two endpoints (`timeRange(23, 0)`: hours 23 and 0).
    pub fn time_range(now: &Civil, args: &[Arg]) -> Option<bool> {
        let args = without_gmt(args);
        let field = |arg: &Arg, i: usize| small(arg, 0, if i == 0 { 23 } else { 59 });
        let fields = |part: &[Arg]| -> Option<Vec<u32>> {
            part.iter().enumerate().map(|(i, a)| field(a, i)).collect()
        };
        let now_fields = [now.hour, now.minute, now.second];
        let seconds = |f: &[u32]| {
            f.iter()
                .chain(std::iter::repeat(&0))
                .take(3)
                .fold(0u32, |acc, &v| acc * 60 + v)
        };
        match args.len() {
            1 => Some(now.hour == field(&args[0], 0)?),
            2 | 4 | 6 => {
                let (first, second) = args.split_at(args.len() / 2);
                let (a, b) = (fields(first)?, fields(second)?);
                let unit = &now_fields[..a.len()];
                if seconds(&a) < seconds(&b) {
                    let t = seconds(&now_fields);
                    Some(seconds(&a) <= t && t <= seconds(&b))
                } else {
                    Some(unit == a.as_slice() || unit == b.as_slice())
                }
            }
            _ => None,
        }
    }

    /// Days-from-civil by hand (PD-31; H. Hinnant's `civil_from_days`), proleptic Gregorian, UTC.
    pub fn civil(unix_secs: i64) -> Civil {
        let days = unix_secs.div_euclid(86_400);
        let second_of_day = unix_secs.rem_euclid(86_400);
        let z = days + 719_468;
        let era = z.div_euclid(146_097);
        let doe = z.rem_euclid(146_097);
        let yoe = (doe - doe / 1_460 + doe / 36_524 - doe / 146_096) / 365;
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        let mp = (5 * doy + 2) / 153;
        let day = doy - (153 * mp + 2) / 5 + 1;
        let month = if mp < 10 { mp + 3 } else { mp - 9 };
        let year = yoe + era * 400 + i64::from(month <= 2);
        // Every value below is inside its small range by construction.
        let narrow = |v: i64| u32::try_from(v).unwrap_or_default();
        Civil {
            year,
            month: narrow(month),
            day: narrow(day),
            weekday: narrow((days + 4).rem_euclid(7)),
            hour: narrow(second_of_day / 3_600),
            minute: narrow(second_of_day % 3_600 / 60),
            second: narrow(second_of_day % 60),
        }
    }
}

// ---- engine glue, macOS and Linux only (R-8, R-15) ----------------------------------------------------------------

#[cfg(not(windows))]
#[derive(Clone, Copy, Debug)]
pub struct PacLimits {
    pub eval_budget: Duration,
    pub memory: usize,
    pub stack: usize,
}

#[cfg(not(windows))]
impl PacLimits {
    /// The constants above; tests use `production()`.
    pub fn production() -> Self {
        PacLimits {
            eval_budget: PAC_EVAL_BUDGET,
            memory: PAC_MEMORY_LIMIT,
            stack: PAC_STACK_LIMIT,
        }
    }
}

/// One compiled script for one `http::get` call (PD-28). Not `Send`: rquickjs's `Runtime` is `Rc`-based without the
/// `parallel` feature, so the session lives and dies on the calling thread (no engine thread exists).
#[cfg(not(windows))]
pub struct PacSession {
    ctx: rquickjs::Context,
    deadline: Rc<Cell<Instant>>,
    eval_budget: Duration,
}

#[cfg(not(windows))]
impl PacSession {
    /// Size check, runtime, limits, interrupt, the restricted context, the helpers, the script, `FindProxyForURL`
    /// present (plan §4.15 "open").
    pub fn open(
        script: &str,
        host: PacHostHandle,
        limits: PacLimits,
        deadline: Instant,
    ) -> Result<Self, PacError> {
        use rquickjs::context::intrinsic::{Eval, Json, RegExp, RegExpCompiler};
        if script.len() as u64 > MAX_PAC_BYTES {
            return Err(script_failure("script too large".to_owned()));
        }
        let rt = rquickjs::Runtime::new().map_err(|e| script_failure(e.to_string()))?;
        rt.set_memory_limit(limits.memory);
        rt.set_max_stack_size(limits.stack);
        let cell = Rc::new(Cell::new(budget_deadline(limits.eval_budget, deadline)));
        let interrupt = Rc::clone(&cell);
        rt.set_interrupt_handler(Some(Box::new(move || Instant::now() >= interrupt.get())));
        let ctx = rquickjs::Context::custom::<(Eval, RegExpCompiler, RegExp, Json)>(&rt)
            .map_err(|e| script_failure(e.to_string()))?;
        let env = HelperEnv {
            host,
            memo: Rc::new(RefCell::new(DnsMemo::new())),
            deadline: Rc::clone(&cell),
            clock: Rc::new(OnceCell::new()),
            warned: Rc::new(Cell::new(false)),
        };
        ctx.with(|ctx| {
            register_helpers(&ctx, &env).map_err(|e| caught(&ctx, e))?;
            cell.set(budget_deadline(limits.eval_budget, deadline));
            ctx.eval::<(), _>(script).map_err(|e| caught(&ctx, e))?;
            let entry: rquickjs::Value = ctx
                .globals()
                .get("FindProxyForURL")
                .map_err(|e| caught(&ctx, e))?;
            if entry.is_function() {
                Ok(())
            } else {
                Err(script_failure("no FindProxyForURL".to_owned()))
            }
        })?;
        Ok(PacSession {
            ctx,
            deadline: cell,
            eval_budget: limits.eval_budget,
        })
    }

    /// `FindProxyForURL(pac_url_argument(url), host)` under `min(now + eval_budget, deadline)`; a non-string result
    /// is `E_PAC_RESULT`, a string goes through `parse_answer`, whose warnings are logged one `warn!` each.
    pub fn find_proxy(
        &mut self,
        url: &url::Url,
        deadline: Instant,
    ) -> Result<Vec<ProxyChoice>, PacError> {
        self.deadline
            .set(budget_deadline(self.eval_budget, deadline));
        let argument = pac_url_argument(url);
        let host = match url.host() {
            Some(url::Host::Ipv6(ip)) => ip.to_string(),
            Some(other) => other.to_string(),
            None => String::new(),
        };
        let answer = self.ctx.with(|ctx| {
            let entry: rquickjs::Function = ctx
                .globals()
                .get("FindProxyForURL")
                .map_err(|e| caught(&ctx, e))?;
            let value: rquickjs::Value =
                entry.call((argument, host)).map_err(|e| caught(&ctx, e))?;
            match value.as_string() {
                Some(text) => text.to_string().map_err(|e| caught(&ctx, e)),
                None => Err(PacError {
                    code: PacCode::Result,
                    detail: format!("FindProxyForURL returned {}", value.type_name()),
                }),
            }
        })?;
        let (choices, warnings) = parse_answer(&answer)?;
        for warning in warnings {
            log::warn!(target: LOG_TARGET, "{warning}");
        }
        Ok(choices)
    }
}

/// SI-25: a one-shot `open` + `find_proxy`, each step under its own `PAC_EVAL_BUDGET`.
#[cfg(not(windows))]
pub fn evaluate(
    script: &str,
    url: &url::Url,
    host: PacHostHandle,
) -> Result<Vec<ProxyChoice>, PacError> {
    let deadline = Instant::now() + PAC_PHASE_BUDGET;
    PacSession::open(script, host, PacLimits::production(), deadline)?.find_proxy(url, deadline)
}

/// PD-29, FR-094: at most `PAC_MAX_LOOKUPS` distinct names per session, every answer memoised, each wait
/// `min(PAC_DNS_WAIT, deadline - now)`.
#[cfg(not(windows))]
pub(crate) struct DnsMemo {
    answers: HashMap<String, Option<Ipv4Addr>>,
    cap_warned: bool,
}

#[cfg(not(windows))]
impl DnsMemo {
    pub(crate) fn new() -> Self {
        DnsMemo {
            answers: HashMap::new(),
            cap_warned: false,
        }
    }

    pub(crate) fn lookup(
        &mut self,
        host: &dyn PacHost,
        name: &str,
        deadline: Instant,
    ) -> Option<Ipv4Addr> {
        if name.parse::<IpAddr>().is_ok() {
            return helpers::dns_resolve(host, name, Duration::ZERO);
        }
        if let Some(answer) = self.answers.get(name) {
            return *answer;
        }
        if self.answers.len() >= PAC_MAX_LOOKUPS {
            if !self.cap_warned {
                self.cap_warned = true;
                log::warn!(
                    target: LOG_TARGET,
                    "PAC DNS lookup cap: more than {PAC_MAX_LOOKUPS} distinct names in one session; the rest are unresolvable (FR-094)"
                );
            }
            return None;
        }
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            return None;
        }
        let answer = helpers::dns_resolve(host, name, PAC_DNS_WAIT.min(remaining));
        self.answers.insert(name.to_owned(), answer);
        answer
    }
}

#[cfg(not(windows))]
fn budget_deadline(budget: Duration, deadline: Instant) -> Instant {
    (Instant::now() + budget).min(deadline)
}

#[cfg(not(windows))]
fn script_failure(detail: String) -> PacError {
    PacError {
        code: PacCode::Script,
        detail: log_text(&detail),
    }
}

/// Any `rquickjs::Error` → `E_PAC_SCRIPT` with the caught exception's message (`Ctx::catch`) through `log_text`.
#[cfg(not(windows))]
fn caught(ctx: &rquickjs::Ctx<'_>, error: rquickjs::Error) -> PacError {
    if !matches!(error, rquickjs::Error::Exception) {
        return script_failure(error.to_string());
    }
    let value = ctx.catch();
    let message = if let Some(text) = value.as_string() {
        text.to_string()
            .unwrap_or_else(|_| "an exception string that cannot be read".to_owned())
    } else if let Some(exception) = value.as_exception() {
        let name: Option<rquickjs::Coerced<String>> = exception.get("name").ok().flatten();
        let message = exception.message().unwrap_or_default();
        match name {
            Some(name) => format!("{}: {message}", name.0),
            None => message,
        }
    } else if let Some(number) = value.as_number() {
        format!("uncaught {number}")
    } else {
        format!("uncaught {}", value.type_name())
    };
    script_failure(message)
}

/// What every helper closure owns (PD-32: an `Arc` clone of the host, never a borrow).
#[cfg(not(windows))]
#[derive(Clone)]
struct HelperEnv {
    host: PacHostHandle,
    memo: Rc<RefCell<DnsMemo>>,
    deadline: Rc<Cell<Instant>>,
    clock: Rc<OnceCell<PacClock>>,
    warned: Rc<Cell<bool>>,
}

#[cfg(not(windows))]
impl HelperEnv {
    /// The session clock, read from the host on first use only (FR-095).
    fn civil_now(&self, gmt: bool) -> helpers::Civil {
        let clock = *self.clock.get_or_init(|| self.host.get().now());
        let mut warned = self.warned.get();
        let civil = clock_civil(clock, gmt, &mut warned);
        self.warned.set(warned);
        civil
    }

    fn lookup(&self, name: &str) -> Option<Ipv4Addr> {
        self.memo
            .borrow_mut()
            .lookup(self.host.get(), name, self.deadline.get())
    }
}

#[cfg(not(windows))]
type JsArgs<'js> = rquickjs::function::Rest<rquickjs::Value<'js>>;

/// A string argument at `index`, at most `cap` bytes, else a thrown `TypeError`.
#[cfg(not(windows))]
fn text_arg(
    ctx: &rquickjs::Ctx<'_>,
    args: &JsArgs<'_>,
    index: usize,
    cap: usize,
) -> rquickjs::Result<String> {
    let text = match args.0.get(index).and_then(|v| v.as_string()) {
        Some(text) => text.to_string()?,
        None => {
            return Err(rquickjs::Exception::throw_type(
                ctx,
                "a string argument is required",
            ));
        }
    };
    if text.len() > cap {
        return Err(rquickjs::Exception::throw_type(ctx, "argument too long"));
    }
    Ok(text)
}

#[cfg(not(windows))]
fn time_args(ctx: &rquickjs::Ctx<'_>, args: &JsArgs<'_>) -> rquickjs::Result<Vec<helpers::Arg>> {
    if args.0.len() > 7 {
        return Err(rquickjs::Exception::throw_type(ctx, "too many arguments"));
    }
    args.0
        .iter()
        .map(|v| {
            if let Some(number) = v.as_number() {
                Ok(helpers::Arg::Num(number))
            } else if let Some(text) = v.as_string() {
                Ok(helpers::Arg::Text(text.to_string()?))
            } else {
                Err(rquickjs::Exception::throw_type(
                    ctx,
                    "a number or string argument is required",
                ))
            }
        })
        .collect()
}

#[cfg(not(windows))]
fn checked<T>(ctx: &rquickjs::Ctx<'_>, value: Option<T>) -> rquickjs::Result<T> {
    value.ok_or_else(|| rquickjs::Exception::throw_type(ctx, "bad or over-length arguments"))
}

#[cfg(not(windows))]
fn ipv4_arg(
    ctx: &rquickjs::Ctx<'_>,
    args: &JsArgs<'_>,
    index: usize,
) -> rquickjs::Result<Ipv4Addr> {
    let text = text_arg(ctx, args, index, 15)?;
    checked(ctx, text.parse::<Ipv4Addr>().ok())
}

/// The PAC helpers on the global object (plan §4.15 "Helpers"); nothing else of the machine.
#[cfg(not(windows))]
fn register_helpers<'js>(ctx: &rquickjs::Ctx<'js>, env: &HelperEnv) -> rquickjs::Result<()> {
    use helpers::MAX_HOST_ARG;
    use rquickjs::{Ctx, Function, Value};
    let globals = ctx.globals();

    globals.set(
        "isPlainHostName",
        Function::new(
            ctx.clone(),
            |ctx: Ctx<'js>, args: JsArgs<'js>| -> rquickjs::Result<_> {
                let h = text_arg(&ctx, &args, 0, MAX_HOST_ARG)?;
                checked(&ctx, helpers::is_plain_host_name(&h))
            },
        )?,
    )?;
    globals.set(
        "dnsDomainIs",
        Function::new(
            ctx.clone(),
            |ctx: Ctx<'js>, args: JsArgs<'js>| -> rquickjs::Result<_> {
                let h = text_arg(&ctx, &args, 0, MAX_HOST_ARG)?;
                let d = text_arg(&ctx, &args, 1, MAX_HOST_ARG)?;
                checked(&ctx, helpers::dns_domain_is(&h, &d))
            },
        )?,
    )?;
    globals.set(
        "localHostOrDomainIs",
        Function::new(
            ctx.clone(),
            |ctx: Ctx<'js>, args: JsArgs<'js>| -> rquickjs::Result<_> {
                let h = text_arg(&ctx, &args, 0, MAX_HOST_ARG)?;
                let hd = text_arg(&ctx, &args, 1, MAX_HOST_ARG)?;
                checked(&ctx, helpers::local_host_or_domain_is(&h, &hd))
            },
        )?,
    )?;
    globals.set(
        "dnsDomainLevels",
        Function::new(
            ctx.clone(),
            |ctx: Ctx<'js>, args: JsArgs<'js>| -> rquickjs::Result<_> {
                let h = text_arg(&ctx, &args, 0, MAX_HOST_ARG)?;
                checked(&ctx, helpers::dns_domain_levels(&h))
            },
        )?,
    )?;
    globals.set(
        "shExpMatch",
        Function::new(
            ctx.clone(),
            |ctx: Ctx<'js>, args: JsArgs<'js>| -> rquickjs::Result<_> {
                let s = text_arg(&ctx, &args, 0, helpers::MAX_SHEXP_SUBJECT)?;
                let pat = text_arg(&ctx, &args, 1, helpers::MAX_SHEXP_PATTERN)?;
                checked(&ctx, helpers::sh_exp_match(&s, &pat))
            },
        )?,
    )?;
    let e = env.clone();
    globals.set(
        "isResolvable",
        Function::new(
            ctx.clone(),
            move |ctx: Ctx<'js>, args: JsArgs<'js>| -> rquickjs::Result<_> {
                let h = text_arg(&ctx, &args, 0, MAX_HOST_ARG)?;
                Ok(e.lookup(&h).is_some())
            },
        )?,
    )?;
    let e = env.clone();
    globals.set(
        "dnsResolve",
        Function::new(
            ctx.clone(),
            move |ctx: Ctx<'js>, args: JsArgs<'js>| -> rquickjs::Result<_> {
                let h = text_arg(&ctx, &args, 0, MAX_HOST_ARG)?;
                Ok(match e.lookup(&h) {
                    Some(ip) => {
                        rquickjs::String::from_str(ctx.clone(), &ip.to_string())?.into_value()
                    }
                    None => Value::new_null(ctx.clone()),
                })
            },
        )?,
    )?;
    let e = env.clone();
    globals.set(
        "isInNet",
        Function::new(
            ctx.clone(),
            move |ctx: Ctx<'js>, args: JsArgs<'js>| -> rquickjs::Result<_> {
                let pattern = ipv4_arg(&ctx, &args, 1)?;
                let mask = ipv4_arg(&ctx, &args, 2)?;
                // MDN's own pattern passes dnsResolve(host), which is null when unresolvable: false, not a throw.
                if args
                    .0
                    .first()
                    .is_none_or(|v| v.is_null() || v.is_undefined())
                {
                    return Ok(false);
                }
                let h = text_arg(&ctx, &args, 0, MAX_HOST_ARG)?;
                Ok(e.lookup(&h)
                    .is_some_and(|ip| helpers::is_in_net(ip, pattern, mask)))
            },
        )?,
    )?;
    let e = env.clone();
    globals.set(
        "myIpAddress",
        Function::new(
            ctx.clone(),
            move |ctx: Ctx<'js>, _args: JsArgs<'js>| -> rquickjs::Result<_> {
                let ip = helpers::my_ip_address(e.host.get());
                rquickjs::String::from_str(ctx, &ip.to_string())
            },
        )?,
    )?;
    type TimeHelper = fn(&helpers::Civil, &[helpers::Arg]) -> Option<bool>;
    let time_helpers: [(&str, TimeHelper); 3] = [
        ("weekdayRange", helpers::weekday_range),
        ("dateRange", helpers::date_range),
        ("timeRange", helpers::time_range),
    ];
    for (name, helper) in time_helpers {
        let e = env.clone();
        globals.set(
            name,
            Function::new(
                ctx.clone(),
                move |ctx: Ctx<'js>, args: JsArgs<'js>| -> rquickjs::Result<_> {
                    let args = time_args(&ctx, &args)?;
                    let now = e.civil_now(helpers::wants_gmt(&args));
                    checked(&ctx, helper(&now, &args))
                },
            )?,
        )?;
    }
    // alert: a no-op that never logs script text.
    globals.set(
        "alert",
        Function::new(ctx.clone(), |_args: JsArgs<'js>| ())?,
    )?;
    Ok(())
}

#[cfg(test)]
#[path = "pac_tests.rs"]
pub(crate) mod tests;
