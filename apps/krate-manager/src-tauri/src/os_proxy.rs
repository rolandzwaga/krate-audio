//! The real OS proxy readers and the real PAC host (FR-049, FR-066, FR-073, FR-092; plan §4.10, PD-1, PD-32,
//! PD-35). Each reader is one OS call plus core seams; `OsPacHost` is never constructed by a test (FR-087).
//! Everything here reads the OS and never writes it, and never kills a process (PD-17). A reader failure logs one
//! `warn!` that names the step and its numeric code, never a value; a PAC host failure is returned to core, which
//! logs its one `warn!` (FR-068, FR-095).

use crate::proxy_env::ProxyInputs;
use krate_manager_core::pac::{PacHost, PacHostHandle};
use krate_manager_core::platform::NativePlatform;
use krate_manager_core::sysproxy::{self, SystemProxy};
use std::sync::Arc;
use std::time::Duration;

#[cfg(windows)]
use krate_manager_core::http::PacSource;
#[cfg(windows)]
use krate_manager_core::pac::{OsProxyEntry, OsProxyKind, PacCode, PacError};
#[cfg(windows)]
use krate_manager_core::sysproxy::WinIeRaw;
#[cfg(windows)]
use std::ffi::c_void;
#[cfg(windows)]
use std::ptr::{null, null_mut};
#[cfg(windows)]
use std::sync::mpsc::{RecvTimeoutError, SyncSender, sync_channel};
#[cfg(windows)]
use windows_sys::Win32::Foundation::{ERROR_IO_PENDING, GlobalFree};
#[cfg(windows)]
use windows_sys::Win32::Networking::WinHttp::{
    ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_ASYNC_RESULT,
    WINHTTP_AUTO_DETECT_TYPE_DHCP, WINHTTP_AUTO_DETECT_TYPE_DNS_A, WINHTTP_AUTOPROXY_AUTO_DETECT,
    WINHTTP_AUTOPROXY_CONFIG_URL, WINHTTP_AUTOPROXY_OPTIONS,
    WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE, WINHTTP_CALLBACK_FLAG_REQUEST_ERROR,
    WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE, WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING,
    WINHTTP_CALLBACK_STATUS_HANDLE_CREATED, WINHTTP_CALLBACK_STATUS_REQUEST_ERROR,
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG, WINHTTP_FLAG_ASYNC, WINHTTP_INTERNET_SCHEME_HTTP,
    WINHTTP_INTERNET_SCHEME_HTTPS, WINHTTP_INTERNET_SCHEME_SOCKS, WINHTTP_OPTION_CONTEXT_VALUE,
    WINHTTP_PROXY_RESULT, WinHttpCloseHandle, WinHttpCreateProxyResolver, WinHttpFreeProxyResult,
    WinHttpGetIEProxyConfigForCurrentUser, WinHttpGetProxyForUrlEx, WinHttpGetProxyResult,
    WinHttpOpen, WinHttpSetOption, WinHttpSetStatusCallback,
};

#[cfg(not(windows))]
use krate_manager_core::http;
#[cfg(not(windows))]
use krate_manager_core::pac::{self, PacClock};
#[cfg(not(windows))]
use std::net::IpAddr;
#[cfg(not(windows))]
use std::time::{SystemTime, UNIX_EPOCH};

#[cfg(target_os = "linux")]
use krate_manager_core::sysproxy::GnomeRaw;
#[cfg(target_os = "linux")]
use std::process::Command;

#[cfg(target_os = "macos")]
use krate_manager_core::sysproxy_cf;
#[cfg(target_os = "macos")]
use system_configuration::dynamic_store::SCDynamicStoreBuilder;

/// Linux: every child (the five GNOME reads, `date +%z`) is waited for at most this long in total (PD-17).
#[cfg(target_os = "linux")]
const CHILD_BOUND: Duration = Duration::from_secs(2);

/// GNOME's proxy keys (plan §1.2), in `GnomeRaw`'s field order.
#[cfg(target_os = "linux")]
const GNOME_KEYS: [(&str, &str); 5] = [
    ("org.gnome.system.proxy", "mode"),
    ("org.gnome.system.proxy", "autoconfig-url"),
    ("org.gnome.system.proxy", "ignore-hosts"),
    ("org.gnome.system.proxy.https", "host"),
    ("org.gnome.system.proxy.https", "port"),
];

pub trait SystemProxyFromOs {
    fn system_proxy(&self, inputs: &ProxyInputs) -> SystemProxy;
}

/// Windows: the WinHTTP current-user settings (R-2).
#[cfg(windows)]
impl SystemProxyFromOs for NativePlatform {
    fn system_proxy(&self, _inputs: &ProxyInputs) -> SystemProxy {
        // windows-sys's Default for this struct is all-zero: every string pointer null.
        let mut cfg = WINHTTP_CURRENT_USER_IE_PROXY_CONFIG::default();
        // SAFETY: `cfg` is a valid, writable WINHTTP_CURRENT_USER_IE_PROXY_CONFIG for the duration of the call; on
        // success WinHTTP fills its three strings, which the caller owns and frees with GlobalFree (the
        // WinHttpGetIEProxyConfigForCurrentUser page, plan §1.2).
        if unsafe { WinHttpGetIEProxyConfigForCurrentUser(&mut cfg) } == 0 {
            log::warn!(
                "system proxy: WinHttpGetIEProxyConfigForCurrentUser failed with code {}; no system proxy",
                last_error()
            );
            return SystemProxy::default();
        }
        let raw = WinIeRaw {
            auto_detect: cfg.fAutoDetect != 0,
            auto_config_url: cfg.lpszAutoConfigUrl,
            proxy: cfg.lpszProxy,
            proxy_bypass: cfg.lpszProxyBypass,
        };
        let mut free = |p: *mut u16| {
            // SAFETY: `take_win_ie` passes each non-NULL string WinHTTP returned above exactly once; the caller
            // owns it and frees it with GlobalFree (Rule 16). The returned handle is NULL on success and carries
            // nothing to act on otherwise.
            unsafe { GlobalFree(p.cast()) };
        };
        // SAFETY: each non-NULL pointer is a NUL-terminated UTF-16 string WinHTTP allocated for this call, valid
        // until `free` (GlobalFree) releases it, which `take_win_ie` does once per pointer after reading it.
        let ie = unsafe { sysproxy::take_win_ie(raw, &mut free) };
        sysproxy::from_windows(&ie)
    }
}

/// macOS: SystemConfiguration's proxy dictionary (R-3, R-6).
#[cfg(target_os = "macos")]
impl SystemProxyFromOs for NativePlatform {
    fn system_proxy(&self, _inputs: &ProxyInputs) -> SystemProxy {
        let Some(store) = SCDynamicStoreBuilder::new("krate-manager").build() else {
            log::warn!("system proxy: SCDynamicStoreCreate failed; no system proxy");
            return SystemProxy::default();
        };
        let Some(dict) = store.get_proxies() else {
            log::warn!("system proxy: SCDynamicStoreCopyProxies returned nothing; no system proxy");
            return SystemProxy::default();
        };
        sysproxy::from_macos(&sysproxy_cf::from_cf_proxies(&dict))
    }
}

/// Linux: KDE's `kioslaverc` when the desktop is KDE, else GNOME's settings (R-4, FR-074).
#[cfg(target_os = "linux")]
impl SystemProxyFromOs for NativePlatform {
    fn system_proxy(&self, inputs: &ProxyInputs) -> SystemProxy {
        if sysproxy::linux_uses_kde(inputs.desktop.as_deref()) {
            let path =
                sysproxy::kioslaverc_path(inputs.config_home.as_deref(), inputs.home.as_deref());
            let Some(path) = path else {
                log::warn!("system proxy: no kioslaverc path; no system proxy");
                return SystemProxy::default();
            };
            return sysproxy::read_kde(&path);
        }
        let commands = GNOME_KEYS
            .into_iter()
            .map(|(schema, key)| {
                let mut command = Command::new("gsettings");
                command.args(["get", schema, key]);
                command
            })
            .collect();
        let run = sysproxy::run_bounded(commands, CHILD_BOUND);
        match run.outputs.as_deref() {
            Some([mode, autoconfig_url, ignore_hosts, https_host, https_port]) => {
                sysproxy::from_gnome(&GnomeRaw {
                    mode: mode.clone(),
                    autoconfig_url: autoconfig_url.clone(),
                    ignore_hosts: ignore_hosts.clone(),
                    https_host: https_host.clone(),
                    https_port: https_port.clone(),
                })
            }
            _ => {
                log::warn!(
                    "system proxy: the GNOME settings read failed ({} still running); no system proxy",
                    run.abandoned.len()
                );
                SystemProxy::default()
            }
        }
    }
}

/// Stateless; every call reads the OS afresh.
pub struct OsPacHost;

pub fn pac_host() -> PacHostHandle {
    PacHostHandle::new(Arc::new(OsPacHost))
}

/// macOS, Linux: the in-process engine's machine access (PD-29 to PD-32).
#[cfg(not(windows))]
impl PacHost for OsPacHost {
    fn resolve(&self, name: &str, wait: Duration) -> Option<IpAddr> {
        http::std_resolve(name, wait)
    }

    fn my_ip(&self) -> IpAddr {
        http::std_my_ip()
    }

    fn now(&self) -> PacClock {
        let unix_secs = match SystemTime::now().duration_since(UNIX_EPOCH) {
            Ok(after) => i64::try_from(after.as_secs()).unwrap_or(i64::MAX),
            Err(before) => -i64::try_from(before.duration().as_secs()).unwrap_or(i64::MAX),
        };
        PacClock {
            unix_secs,
            local_offset_secs: local_offset(),
        }
    }

    fn wpad_candidates(&self, _wait: Duration) -> Vec<String> {
        pac::wpad_dns_candidates()
    }
}

/// The local zone's offset from UTC in seconds (PD-31, R-13); `None` makes the session use UTC, and
/// `pac::clock_civil` logs the session's one `warn!` (FR-095).
#[cfg(target_os = "macos")]
fn local_offset() -> Option<i32> {
    sysproxy_cf::system_utc_offset()
}

/// The local zone's offset from UTC in seconds (PD-31, R-13); `None` makes the session use UTC, and
/// `pac::clock_civil` logs the session's one `warn!` (FR-095).
#[cfg(target_os = "linux")]
fn local_offset() -> Option<i32> {
    let mut date = Command::new("date");
    date.arg("+%z");
    sysproxy::run_bounded(vec![date], CHILD_BOUND)
        .outputs
        .as_deref()
        .and_then(<[String]>::first)
        .and_then(|text| sysproxy::parse_utc_offset(text))
}

/// Windows: the OS evaluates (R-15, R-16, PD-35).
#[cfg(windows)]
impl PacHost for OsPacHost {
    fn os_evaluates(&self) -> bool {
        true
    }

    fn os_find_proxy(
        &self,
        source: &PacSource,
        url: &str,
        wait: Duration,
    ) -> Result<Vec<OsProxyEntry>, PacError> {
        win_find_proxy(source, url, wait)
    }
}

/// winhttp.h's own definition; windows-sys 0.61.2 does not export it (plan §1.2, Rule 14).
#[cfg(windows)]
const WINHTTP_CALLBACK_FLAG_HANDLES: u32 =
    WINHTTP_CALLBACK_STATUS_HANDLE_CREATED | WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING;

/// WinHttpSetStatusCallback's failure value, `(WINHTTP_STATUS_CALLBACK)(-1L)` in winhttp.h.
#[cfg(windows)]
const INVALID_STATUS_CALLBACK: usize = usize::MAX;

/// The resolver's context (PD-35): bound to the resolver handle and freed only by `on_status` on that handle's
/// `HANDLE_CLOSING`, the last callback WinHTTP makes for it. It also owns every buffer the asynchronous call reads
/// (the URL, the auto-config URL, the options), so none of them can be freed while WinHTTP may still read it.
#[cfg(windows)]
struct CallbackCtx {
    tx: SyncSender<(u32, u32)>,
    url: Vec<u16>,
    _config_url: Option<Vec<u16>>,
    options: WINHTTP_AUTOPROXY_OPTIONS,
}

/// UTF-16 plus a NUL.
#[cfg(windows)]
fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// The calling thread's last OS error code.
#[cfg(windows)]
fn last_error() -> u32 {
    std::io::Error::last_os_error()
        .raw_os_error()
        .unwrap_or(0)
        .cast_unsigned()
}

/// PD-35's error classes; the detail names the step and the code, and core's `warn_pac_failure` logs it as the one
/// `warn!` of the failure (FR-068).
#[cfg(windows)]
fn os_error(step: &str, code: u32) -> PacError {
    let class = if code == ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT {
        PacCode::Script
    } else {
        PacCode::Fetch
    };
    PacError {
        code: class,
        detail: format!("{step} failed with code {code}"),
    }
}

/// Closes one WinHTTP handle.
///
/// # Safety
///
/// `handle` is a live WinHTTP handle that the caller opened and closes exactly once, and does not use afterwards.
#[cfg(windows)]
unsafe fn close_handle(handle: *mut c_void) {
    // SAFETY: the caller's contract above; WinHttpCloseHandle is also the documented cancel of an outstanding
    // resolver operation (the WinHttpCreateProxyResolver page). Its BOOL carries nothing to act on.
    unsafe { WinHttpCloseHandle(handle) };
}

/// PD-35 and plan §4.10, one OS step at a time; every path after the evaluation call closes the resolver, then the
/// session, and never frees the context.
#[cfg(windows)]
fn win_find_proxy(
    source: &PacSource,
    url: &str,
    wait: Duration,
) -> Result<Vec<OsProxyEntry>, PacError> {
    // (1) A session whose own requests would go direct; it sends none.
    let agent = wide("krate-manager");
    // SAFETY: `agent` is NUL-terminated and outlives the call; WINHTTP_ACCESS_TYPE_NO_PROXY takes NULL proxy
    // and bypass strings (the WinHttpOpen page). The returned handle is closed below on every path.
    let session = unsafe {
        WinHttpOpen(
            agent.as_ptr(),
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            null(),
            null(),
            WINHTTP_FLAG_ASYNC,
        )
    };
    if session.is_null() {
        return Err(os_error("WinHttpOpen", last_error()));
    }

    // (2) The callback, set on the session before the resolver exists, so the resolver inherits it.
    // SAFETY: `session` is the live handle opened above; `on_status` is a 'static function that obeys the
    // WinHTTP callback contract (it only reads its context, see there).
    let previous = unsafe {
        WinHttpSetStatusCallback(
            session,
            Some(on_status),
            WINHTTP_CALLBACK_FLAG_REQUEST_ERROR
                | WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE
                | WINHTTP_CALLBACK_FLAG_HANDLES,
            0,
        )
    };
    if previous.map_or(0, |f| f as usize) == INVALID_STATUS_CALLBACK {
        let code = last_error();
        // SAFETY: `session` is live, opened above, closed once here and not used again.
        unsafe { close_handle(session) };
        return Err(os_error("WinHttpSetStatusCallback", code));
    }

    // (3) The resolver; nothing is allocated yet.
    let mut resolver: *mut c_void = null_mut();
    // SAFETY: `session` was opened with WINHTTP_FLAG_ASYNC, as the WinHttpCreateProxyResolver page requires;
    // `resolver` is a valid out pointer.
    let rc = unsafe { WinHttpCreateProxyResolver(session, &mut resolver) };
    if rc != 0 {
        // SAFETY: `session` is live, opened above, closed once here and not used again.
        unsafe { close_handle(session) };
        return Err(os_error("WinHttpCreateProxyResolver", rc));
    }

    // (3b) The context, bound to the resolver so that its HANDLE_CLOSING is delivered with it.
    let (tx, rx) = sync_channel::<(u32, u32)>(1);
    let config_url = source.url.as_deref().map(wide);
    let mut options = WINHTTP_AUTOPROXY_OPTIONS {
        dwFlags: 0,
        dwAutoDetectFlags: 0,
        lpszAutoConfigUrl: config_url.as_ref().map_or(null(), |w| w.as_ptr()),
        lpvReserved: null_mut(),
        dwReserved: 0,
        // R-17: no domain credentials are offered to a PAC server.
        fAutoLogonIfChallenged: 0,
    };
    if source.auto_detect {
        options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
        options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
    }
    if config_url.is_some() {
        options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
    }
    // Moving a Vec into the box moves its header, not its heap buffer, so `lpszAutoConfigUrl` stays valid.
    let ctx = Box::into_raw(Box::new(CallbackCtx {
        tx,
        url: wide(url),
        _config_url: config_url,
        options,
    }));
    let ctx_value: usize = ctx.expose_provenance();
    // SAFETY: `resolver` is the live handle created above; the buffer is one pointer-sized value that lives for the
    // call (WinHTTP copies it). From here on the waiting thread never frees `ctx` (PD-35).
    let bound = unsafe {
        WinHttpSetOption(
            resolver,
            WINHTTP_OPTION_CONTEXT_VALUE,
            (&raw const ctx_value).cast::<c_void>(),
            size_of::<usize>() as u32,
        )
    };
    if bound == 0 {
        // PD-35: the context is deliberately leaked for this call (one box per hop); the call proceeds.
        log::warn!(
            "OS PAC evaluation: WinHttpSetOption(CONTEXT_VALUE) failed with code {}; its context is leaked",
            last_error()
        );
    }

    // (4) The evaluation call.
    // SAFETY: `ctx` is live: it is freed only on the resolver's HANDLE_CLOSING, and the resolver is still open. The
    // URL and the options it points into are owned by `ctx`, so they outlive every read WinHTTP makes of them.
    let rc = unsafe {
        WinHttpGetProxyForUrlEx(
            resolver,
            (*ctx).url.as_ptr(),
            &raw const (*ctx).options,
            ctx_value,
        )
    };
    if rc != ERROR_IO_PENDING {
        // No completion callback follows (the WinHttpGetProxyForUrlEx page).
        // SAFETY: both handles are live, opened above, closed once each here, resolver first, and not used again;
        // the resolver's HANDLE_CLOSING frees `ctx`.
        unsafe {
            close_handle(resolver);
            close_handle(session);
        }
        return Err(os_error("WinHttpGetProxyForUrlEx", rc));
    }

    // (5) The answer, or none within `wait`.
    let result = match rx.recv_timeout(wait) {
        Ok((WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE, _)) => read_result(resolver),
        Ok((WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, code)) => {
            Err(os_error("WinHttpGetProxyForUrlEx", code))
        }
        Ok((status, _)) => Err(os_error(
            "WinHttpGetProxyForUrlEx (unexpected status)",
            status,
        )),
        Err(RecvTimeoutError::Timeout | RecvTimeoutError::Disconnected) => Err(PacError {
            code: PacCode::Fetch,
            detail: format!("no answer from the OS PAC evaluator within {wait:?}"),
        }),
    };

    // (6) On every path: the resolver (the cancel, on the timeout path), then the session; `ctx` is never freed here.
    // SAFETY: both handles are live, opened above, closed once each here, resolver first, and not used again; the
    // resolver's HANDLE_CLOSING frees `ctx`, and `rx` dropping after this only makes a late `try_send` fail.
    unsafe {
        close_handle(resolver);
        close_handle(session);
    }
    result
}

/// The completed call's entries as core data (PD-35's mapping), then the result is freed.
#[cfg(windows)]
fn read_result(resolver: *mut c_void) -> Result<Vec<OsProxyEntry>, PacError> {
    let mut result = WINHTTP_PROXY_RESULT::default();
    // SAFETY: `resolver` is live and its call has delivered GETPROXYFORURL_COMPLETE; `result` is a valid out
    // structure that WinHTTP fills and WinHttpFreeProxyResult frees below.
    let rc = unsafe { WinHttpGetProxyResult(resolver, &mut result) };
    if rc != 0 {
        return Err(os_error("WinHttpGetProxyResult", rc));
    }
    let entries = if result.pEntries.is_null() {
        &[][..]
    } else {
        // SAFETY: on success `pEntries` points at `cEntries` initialised entries, valid until
        // WinHttpFreeProxyResult, which runs only after this slice's last use.
        unsafe { std::slice::from_raw_parts(result.pEntries, result.cEntries as usize) }
    };
    let out = entries
        .iter()
        .map(|entry| {
            let kind = if entry.fProxy == 0 {
                OsProxyKind::Direct
            } else {
                match entry.ProxyScheme {
                    WINHTTP_INTERNET_SCHEME_HTTP => OsProxyKind::Http,
                    WINHTTP_INTERNET_SCHEME_HTTPS => OsProxyKind::Https,
                    WINHTTP_INTERNET_SCHEME_SOCKS => OsProxyKind::Socks,
                    other => OsProxyKind::Other(other),
                }
            };
            // SAFETY: `pwszProxy` is NULL or a NUL-terminated UTF-16 string inside the result, valid until
            // WinHttpFreeProxyResult; `read_wide` copies it and frees nothing.
            let host = unsafe { sysproxy::read_wide(entry.pwszProxy) }.unwrap_or_default();
            OsProxyEntry {
                kind,
                host,
                port: entry.ProxyPort,
            }
        })
        .collect();
    // SAFETY: `result` was filled by the successful WinHttpGetProxyResult above and is freed exactly once; nothing
    // read from it is used after this point (every string was copied).
    unsafe { WinHttpFreeProxyResult(&mut result) };
    Ok(out)
}

/// The WinHTTP status callback (PD-35). It runs on a WinHTTP thread: it allocates, logs and panics nothing; its one
/// deallocation is the context's, on the resolver's HANDLE_CLOSING.
///
/// # Safety
///
/// Called by WinHTTP only, with `context` 0 or the `ctx` pointer `win_find_proxy` bound to the resolver.
#[cfg(windows)]
unsafe extern "system" fn on_status(
    _handle: *mut c_void,
    context: usize,
    status: u32,
    info: *mut c_void,
    _info_len: u32,
) {
    // The session carries no context value, and the resolver's HANDLE_CREATED arrives before it is bound.
    if context == 0 {
        return;
    }
    let ctx = std::ptr::with_exposed_provenance_mut::<CallbackCtx>(context);
    if status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING {
        // SAFETY: HANDLE_CLOSING is the last callback WinHTTP makes for the resolver (the WinHttpCloseHandle
        // remarks), so no other callback can still hold `ctx`, and the waiting thread never frees it; it came from
        // `Box::into_raw` in `win_find_proxy` and is reclaimed exactly once, here.
        drop(unsafe { Box::from_raw(ctx) });
        return;
    }
    let code = if status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && !info.is_null() {
        // SAFETY: for REQUEST_ERROR, `info` points at a WINHTTP_ASYNC_RESULT valid for this callback (the
        // WINHTTP_STATUS_CALLBACK page).
        unsafe { (*info.cast::<WINHTTP_ASYNC_RESULT>()).dwError }
    } else {
        0
    };
    // SAFETY: `ctx` is alive until the resolver's HANDLE_CLOSING, the last callback for the handle (the
    // WinHttpCloseHandle remarks), and the waiting thread never frees it; only a shared reference to the sender
    // is taken.
    let tx = unsafe { &(*ctx).tx };
    // A full channel or a waiter that already returned is a late callback, not an error.
    let _ = tx.try_send((status, code));
}
