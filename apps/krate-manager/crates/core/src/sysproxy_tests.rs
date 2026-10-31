//! sysproxy tests (plan §4.6, §7.1 rows SC-029, SC-039, SC-043).

use super::*;
use crate::test_support::{install_capture, take_mine};
use log::Level;

const TARGET: &str = "krate_manager_core::sysproxy";

/// Clears this thread's captured records before a call.
fn start() {
    install_capture();
    let _ = take_mine();
}

/// This thread's `warn!` messages from the sysproxy target since the last `start`/`warns`.
fn warns() -> Vec<String> {
    take_mine()
        .into_iter()
        .filter(|(target, level, _)| target == TARGET && *level == Level::Warn)
        .map(|(_, _, message)| message)
        .collect()
}

fn proxy(url: &str, no_proxy: &[&str]) -> Option<ProxyConfig> {
    Some(ProxyConfig {
        url: url.to_owned(),
        no_proxy: no_proxy.iter().map(|s| (*s).to_owned()).collect(),
    })
}

fn sys(proxy: Option<ProxyConfig>, pac_url: Option<&str>, auto_detect: bool) -> SystemProxy {
    SystemProxy {
        proxy,
        pac_url: pac_url.map(str::to_owned),
        auto_detect,
    }
}

fn win(proxy: Option<&str>, bypass: Option<&str>) -> WinIeProxyConfig {
    WinIeProxyConfig {
        auto_detect: false,
        auto_config_url: None,
        proxy: proxy.map(str::to_owned),
        proxy_bypass: bypass.map(str::to_owned),
    }
}

/// Asserts the parse result and the number of warnings; a warning never carries `secret`.
fn check<T: fmt::Debug>(
    name: &str,
    input: &T,
    parse: impl Fn(&T) -> SystemProxy,
    want: &SystemProxy,
    want_warns: usize,
) {
    start();
    let got = parse(input);
    let w = warns();
    assert_eq!(&got, want, "{name}: {input:?}");
    assert_eq!(w.len(), want_warns, "{name}: warnings {w:?}");
    for m in &w {
        assert!(!m.contains("secret"), "{name}: raw value in {m}");
    }
}

#[test]
fn windows_proxy_parser_cases() {
    let pac = "http://wpad.corp/proxy.pac";
    let cases: Vec<(&str, WinIeProxyConfig, SystemProxy, usize)> = vec![
        (
            "all off",
            WinIeProxyConfig::default(),
            SystemProxy::default(),
            0,
        ),
        (
            "empty proxy string",
            win(Some(""), None),
            SystemProxy::default(),
            0,
        ),
        (
            "single proxy",
            win(Some("proxy:3128"), None),
            sys(proxy("http://proxy:3128", &[]), None, false),
            0,
        ),
        (
            "single proxy with a scheme",
            win(Some("https://proxy:3128"), None),
            sys(proxy("https://proxy:3128", &[]), None, false),
            0,
        ),
        (
            "per-scheme list takes https",
            win(Some("http=a:1;https=b:2"), None),
            sys(proxy("http://b:2", &[]), None, false),
            0,
        ),
        (
            "per-scheme list, whitespace separated, key case-insensitive",
            win(Some("http=a:1 HTTPS=b:2"), None),
            sys(proxy("http://b:2", &[]), None, false),
            0,
        ),
        (
            "http only: no static proxy",
            win(Some("http=a:1"), None),
            SystemProxy::default(),
            0,
        ),
        (
            "bypass list",
            win(Some("proxy:3128"), Some("<local>;*.corp;10.*")),
            sys(
                proxy("http://proxy:3128", &["<local>", "*.corp", "10.*"]),
                None,
                false,
            ),
            0,
        ),
        (
            "bypass list split on whitespace too",
            win(Some("proxy:3128"), Some(" <local>; *.corp\t10.* ;")),
            sys(
                proxy("http://proxy:3128", &["<local>", "*.corp", "10.*"]),
                None,
                false,
            ),
            0,
        ),
        (
            "auto config url",
            WinIeProxyConfig {
                auto_config_url: Some(pac.to_owned()),
                ..WinIeProxyConfig::default()
            },
            sys(None, Some(pac), false),
            0,
        ),
        (
            "auto detect",
            WinIeProxyConfig {
                auto_detect: true,
                ..WinIeProxyConfig::default()
            },
            sys(None, None, true),
            0,
        ),
        (
            "static proxy and PAC sources kept side by side",
            WinIeProxyConfig {
                auto_detect: true,
                auto_config_url: Some(pac.to_owned()),
                proxy: Some("proxy:3128".to_owned()),
                proxy_bypass: Some("<local>".to_owned()),
            },
            sys(proxy("http://proxy:3128", &["<local>"]), Some(pac), true),
            0,
        ),
        (
            "malformed: port out of range",
            WinIeProxyConfig {
                auto_detect: true,
                auto_config_url: Some(pac.to_owned()),
                proxy: Some("user:secret@proxy:99999".to_owned()),
                proxy_bypass: None,
            },
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: list entry without '='",
            win(Some("http=a:1;secret"), None),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: empty https entry",
            win(Some("http=a:1;https="), None),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: host with a space",
            win(Some("secret host:1"), None),
            SystemProxy::default(),
            1,
        ),
    ];
    for (name, input, want, n) in &cases {
        check(name, input, from_windows, want, *n);
    }
}

/// SystemConfiguration key/value pairs as `from_macos` takes them.
type Dict = Vec<(String, ScValue)>;

fn dict(pairs: &[(&str, ScValue)]) -> Dict {
    pairs
        .iter()
        .map(|(k, v)| ((*k).to_owned(), v.clone()))
        .collect()
}

fn text(s: &str) -> ScValue {
    ScValue::Text(s.to_owned())
}

#[test]
fn macos_proxy_parser_cases() {
    use ScValue::{Bool, Int, List};
    let pac = "http://wpad.corp/proxy.pac";
    let on = [Int(1), Bool(true)];
    let off = [Int(0), Bool(false), text("1")];

    // Every `…Enable` key: on as Int(1) and Bool(true), off as Int(0) / Bool(false), absent as Text("1").
    for v in &on {
        let d = dict(&[
            ("HTTPSEnable", v.clone()),
            ("HTTPSProxy", text("proxy.corp")),
            ("HTTPSPort", Int(3128)),
        ]);
        check(
            "HTTPSEnable on",
            &d,
            |d| from_macos(d),
            &sys(proxy("http://proxy.corp:3128", &[]), None, false),
            0,
        );
        let d = dict(&[
            ("ProxyAutoConfigEnable", v.clone()),
            ("ProxyAutoConfigURLString", text(pac)),
        ]);
        check(
            "ProxyAutoConfigEnable on",
            &d,
            |d| from_macos(d),
            &sys(None, Some(pac), false),
            0,
        );
        let d = dict(&[("ProxyAutoDiscoveryEnable", v.clone())]);
        check(
            "ProxyAutoDiscoveryEnable on",
            &d,
            |d| from_macos(d),
            &sys(None, None, true),
            0,
        );
    }
    for v in &off {
        let d = dict(&[
            ("HTTPSEnable", v.clone()),
            ("HTTPSProxy", text("proxy.corp")),
            ("HTTPSPort", Int(3128)),
        ]);
        check(
            "HTTPSEnable off",
            &d,
            |d| from_macos(d),
            &SystemProxy::default(),
            0,
        );
        let d = dict(&[
            ("ProxyAutoConfigEnable", v.clone()),
            ("ProxyAutoConfigURLString", text(pac)),
        ]);
        check(
            "ProxyAutoConfigEnable off",
            &d,
            |d| from_macos(d),
            &SystemProxy::default(),
            0,
        );
        let d = dict(&[("ProxyAutoDiscoveryEnable", v.clone())]);
        check(
            "ProxyAutoDiscoveryEnable off",
            &d,
            |d| from_macos(d),
            &SystemProxy::default(),
            0,
        );
    }

    let cases: Vec<(&str, Dict, SystemProxy, usize)> = vec![
        ("empty dictionary", dict(&[]), SystemProxy::default(), 0),
        (
            "no port: 443",
            dict(&[("HTTPSEnable", Int(1)), ("HTTPSProxy", text("proxy.corp"))]),
            sys(proxy("http://proxy.corp:443", &[]), None, false),
            0,
        ),
        (
            "a wrongly typed port counts as absent",
            dict(&[
                ("HTTPSEnable", Int(1)),
                ("HTTPSProxy", text("proxy.corp")),
                ("HTTPSPort", text("3128")),
            ]),
            sys(proxy("http://proxy.corp:443", &[]), None, false),
            0,
        ),
        (
            "a wrongly typed host counts as absent",
            dict(&[
                ("HTTPSEnable", Int(1)),
                ("HTTPSProxy", Int(5)),
                ("HTTPSPort", Int(3128)),
            ]),
            SystemProxy::default(),
            0,
        ),
        (
            "an empty host: no static proxy",
            dict(&[
                ("HTTPSEnable", Int(1)),
                ("HTTPSProxy", text("")),
                ("HTTPSPort", Int(3128)),
            ]),
            SystemProxy::default(),
            0,
        ),
        (
            "exceptions become the bypass list",
            dict(&[
                ("HTTPSEnable", Int(1)),
                ("HTTPSProxy", text("proxy.corp")),
                ("HTTPSPort", Int(3128)),
                (
                    "ExceptionsList",
                    List(vec!["*.local".to_owned(), "169.254/16".to_owned()]),
                ),
            ]),
            sys(
                proxy("http://proxy.corp:3128", &["*.local", "169.254/16"]),
                None,
                false,
            ),
            0,
        ),
        (
            "a wrongly typed exceptions list counts as absent",
            dict(&[
                ("HTTPSEnable", Int(1)),
                ("HTTPSProxy", text("proxy.corp")),
                ("HTTPSPort", Int(3128)),
                ("ExceptionsList", text("*.local")),
            ]),
            sys(proxy("http://proxy.corp:3128", &[]), None, false),
            0,
        ),
        (
            "PAC enabled with an empty URL: no pac_url",
            dict(&[
                ("ProxyAutoConfigEnable", Int(1)),
                ("ProxyAutoConfigURLString", text("")),
            ]),
            SystemProxy::default(),
            0,
        ),
        (
            "PAC enabled with a wrongly typed URL: no pac_url",
            dict(&[
                ("ProxyAutoConfigEnable", Int(1)),
                ("ProxyAutoConfigURLString", Int(1)),
            ]),
            SystemProxy::default(),
            0,
        ),
        (
            "static proxy beside a PAC URL and discovery: all kept",
            dict(&[
                ("HTTPSEnable", Int(1)),
                ("HTTPSProxy", text("proxy.corp")),
                ("HTTPSPort", Int(3128)),
                ("ProxyAutoConfigEnable", Bool(true)),
                ("ProxyAutoConfigURLString", text(pac)),
                ("ProxyAutoDiscoveryEnable", Int(1)),
            ]),
            sys(proxy("http://proxy.corp:3128", &[]), Some(pac), true),
            0,
        ),
        (
            "an IPv6 host is bracketed",
            dict(&[
                ("HTTPSEnable", Int(1)),
                ("HTTPSProxy", text("::1")),
                ("HTTPSPort", Int(3128)),
            ]),
            sys(proxy("http://[::1]:3128", &[]), None, false),
            0,
        ),
        (
            "malformed: port out of range",
            dict(&[
                ("HTTPSEnable", Int(1)),
                ("HTTPSProxy", text("proxy.corp")),
                ("HTTPSPort", Int(70_000)),
                ("ProxyAutoDiscoveryEnable", Int(1)),
            ]),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: host with a space",
            dict(&[
                ("HTTPSEnable", Int(1)),
                ("HTTPSProxy", text("secret host")),
                ("HTTPSPort", Int(3128)),
            ]),
            SystemProxy::default(),
            1,
        ),
    ];
    for (name, input, want, n) in &cases {
        check(name, input, |d| from_macos(d), want, *n);
    }
}

fn gnome(mode: &str, autoconfig_url: &str, ignore_hosts: &str, host: &str, port: &str) -> GnomeRaw {
    GnomeRaw {
        mode: mode.to_owned(),
        autoconfig_url: autoconfig_url.to_owned(),
        ignore_hosts: ignore_hosts.to_owned(),
        https_host: host.to_owned(),
        https_port: port.to_owned(),
    }
}

#[test]
fn linux_proxy_parser_cases() {
    let pac = "http://wpad.corp/proxy.pac";
    let quoted_pac = format!("'{pac}'\n");
    let none = "@as []\n";
    let gnome_cases: Vec<(&str, GnomeRaw, SystemProxy, usize)> = vec![
        (
            "mode none",
            gnome("'none'\n", "''\n", none, "''\n", "0\n"),
            SystemProxy::default(),
            0,
        ),
        (
            "manual with host and port",
            gnome("'manual'\n", "''\n", none, "'proxy.corp'\n", "3128\n"),
            sys(proxy("http://proxy.corp:3128", &[]), None, false),
            0,
        ),
        (
            "manual, port 0: no static proxy",
            gnome("'manual'\n", "''\n", none, "'proxy.corp'\n", "0\n"),
            SystemProxy::default(),
            0,
        ),
        (
            "manual, empty host: no static proxy",
            gnome("'manual'\n", "''\n", none, "''\n", "3128\n"),
            SystemProxy::default(),
            0,
        ),
        (
            "auto with an autoconfig url",
            gnome("'auto'\n", &quoted_pac, none, "''\n", "0\n"),
            sys(None, Some(pac), false),
            0,
        ),
        (
            "auto without an autoconfig url",
            gnome("'auto'\n", "''\n", none, "''\n", "0\n"),
            sys(None, None, true),
            0,
        ),
        (
            "ignore-hosts list",
            gnome(
                "'manual'\n",
                "''\n",
                "['localhost', '127.0.0.0/8', '::1']\n",
                "'proxy.corp'\n",
                "3128\n",
            ),
            sys(
                proxy(
                    "http://proxy.corp:3128",
                    &["localhost", "127.0.0.0/8", "::1"],
                ),
                None,
                false,
            ),
            0,
        ),
        (
            "ignore-hosts with a double-quoted and an escaped item",
            gnome(
                "'manual'\n",
                "''\n",
                "[\"it's.corp\", 'a\\'b']\n",
                "'proxy.corp'\n",
                "3128\n",
            ),
            sys(
                proxy("http://proxy.corp:3128", &["it's.corp", "a'b"]),
                None,
                false,
            ),
            0,
        ),
        (
            "malformed: unquoted mode",
            gnome("manual\n", "''\n", none, "'proxy.corp'\n", "3128\n"),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: unknown mode",
            gnome("'secret'\n", "''\n", none, "''\n", "0\n"),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: unterminated host",
            gnome("'manual'\n", "''\n", none, "'secret\n", "3128\n"),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: non-numeric port",
            gnome("'manual'\n", "''\n", none, "'proxy.corp'\n", "secret\n"),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: unclosed ignore-hosts list",
            gnome(
                "'manual'\n",
                "''\n",
                "['secret'\n",
                "'proxy.corp'\n",
                "3128\n",
            ),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: trailing text after the autoconfig url",
            gnome("'auto'\n", "'secret' x\n", none, "''\n", "0\n"),
            SystemProxy::default(),
            1,
        ),
    ];
    for (name, input, want, n) in &gnome_cases {
        check(name, input, from_gnome, want, *n);
    }

    let group = "[Proxy Settings]\n";
    let kde_cases: Vec<(&str, String, SystemProxy, usize)> = vec![
        ("empty file", String::new(), SystemProxy::default(), 0),
        (
            "ProxyType 0",
            format!("{group}ProxyType=0\nhttpsProxy=http://host 8080\n"),
            SystemProxy::default(),
            0,
        ),
        (
            "ProxyType 1, KDE host-space-port form",
            format!("{group}ProxyType=1\nhttpsProxy=http://host 8080\n"),
            sys(proxy("http://host:8080", &[]), None, false),
            0,
        ),
        (
            "ProxyType 1, schemeless value gets http://",
            format!("{group}ProxyType=1\nhttpsProxy=host 8080\n"),
            sys(proxy("http://host:8080", &[]), None, false),
            0,
        ),
        (
            "ProxyType 1, host:port form",
            format!("{group}ProxyType=1\nhttpsProxy=host:8080\n"),
            sys(proxy("http://host:8080", &[]), None, false),
            0,
        ),
        (
            "ProxyType 1 with NoProxyFor",
            format!("{group}ProxyType=1\nhttpsProxy=http://host 8080\nNoProxyFor=a,b\n"),
            sys(proxy("http://host:8080", &["a", "b"]), None, false),
            0,
        ),
        (
            "ProxyType 2: the config script",
            format!("{group}ProxyType=2\nProxy Config Script={pac}\n"),
            sys(None, Some(pac), false),
            0,
        ),
        (
            "ProxyType 3: auto detect",
            format!("[General]\nx=1\n\n{group}ProxyType=3\n"),
            sys(None, None, true),
            0,
        ),
        (
            "ProxyType 4: environment, none here",
            format!("{group}ProxyType=4\nhttpsProxy=http://host 8080\n"),
            SystemProxy::default(),
            0,
        ),
        (
            "keys of another group are ignored",
            "[Other]\nProxyType=1\nhttpsProxy=http://host 8080\n".to_owned(),
            SystemProxy::default(),
            0,
        ),
        (
            "ReversedException=true: list ignored, one warning",
            format!(
                "{group}ProxyType=1\nhttpsProxy=http://host 8080\nNoProxyFor=a,b\nReversedException=true\n"
            ),
            sys(proxy("http://host:8080", &[]), None, false),
            1,
        ),
        (
            "malformed: ProxyType not a number",
            format!("{group}ProxyType=secret\n"),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: ProxyType out of range",
            format!("{group}ProxyType=9\n"),
            SystemProxy::default(),
            1,
        ),
        (
            "malformed: proxy port not a number",
            format!("{group}ProxyType=1\nhttpsProxy=http://host secret\n"),
            SystemProxy::default(),
            1,
        ),
    ];
    for (name, input, want, n) in &kde_cases {
        check(name, input, |t| from_kde(t), want, *n);
    }
}

fn wide(s: &[u16]) -> Vec<u16> {
    let mut v = s.to_vec();
    v.push(0);
    v
}

#[test]
fn win_ie_strings_are_read_and_freed_once() {
    let mut ascii = wide(&"proxy:3128".encode_utf16().collect::<Vec<_>>());
    let mut non_bmp = wide(&"a\u{1D11E}b".encode_utf16().collect::<Vec<_>>());
    let mut lone = wide(&[u16::from(b'x'), 0xD800, u16::from(b'y')]);
    let ascii_p = ascii.as_mut_ptr();
    let non_bmp_p = non_bmp.as_mut_ptr();
    let lone_p = lone.as_mut_ptr();

    let mut freed: Vec<*mut u16> = Vec::new();
    let raw = WinIeRaw {
        auto_detect: true,
        auto_config_url: non_bmp_p,
        proxy: ascii_p,
        proxy_bypass: lone_p,
    };
    // SAFETY: the three buffers are NUL-terminated and outlive the call; `free` only records.
    let got = unsafe { take_win_ie(raw, &mut |p| freed.push(p)) };
    assert_eq!(
        got,
        WinIeProxyConfig {
            auto_detect: true,
            auto_config_url: Some("a\u{1D11E}b".to_owned()),
            proxy: Some("proxy:3128".to_owned()),
            proxy_bypass: Some("x\u{FFFD}y".to_owned()),
        }
    );
    for p in [ascii_p, non_bmp_p, lone_p] {
        assert_eq!(freed.iter().filter(|f| **f == p).count(), 1, "{p:?}");
    }
    assert_eq!(freed.len(), 3);

    let mut freed: Vec<*mut u16> = Vec::new();
    let raw = WinIeRaw {
        auto_detect: false,
        auto_config_url: std::ptr::null_mut(),
        proxy: ascii_p,
        proxy_bypass: std::ptr::null_mut(),
    };
    // SAFETY: as above; the null pointers are never read.
    let got = unsafe { take_win_ie(raw, &mut |p| freed.push(p)) };
    assert_eq!(got, win(Some("proxy:3128"), None));
    assert_eq!(freed, [ascii_p], "the null pointers are never freed");

    // read_wide: the string, nothing freed (it takes no free), a null pointer → None.
    // SAFETY: each buffer is NUL-terminated and alive.
    let a = unsafe { read_wide(ascii.as_ptr()) };
    // SAFETY: as above.
    let b = unsafe { read_wide(non_bmp.as_ptr()) };
    // SAFETY: as above.
    let c = unsafe { read_wide(lone.as_ptr()) };
    // SAFETY: a null pointer is allowed and never read.
    let n = unsafe { read_wide(std::ptr::null()) };
    assert_eq!(a.as_deref(), Some("proxy:3128"));
    assert_eq!(b.as_deref(), Some("a\u{1D11E}b"));
    assert_eq!(c.as_deref(), Some("x\u{FFFD}y"));
    assert_eq!(n, None);
    // SAFETY: an empty string is a lone NUL.
    let e = unsafe { read_wide([0u16].as_ptr()) };
    assert_eq!(e.as_deref(), Some(""));
}

#[test]
fn linux_desktop_choice_cases() {
    for d in ["KDE", "X-Cinnamon:KDE", "KDE:plasma"] {
        assert!(linux_uses_kde(Some(d)), "{d}");
    }
    for d in ["GNOME", "ubuntu:GNOME", "XFCE", "KDEPlasma", "kde", ""] {
        assert!(!linux_uses_kde(Some(d)), "{d}");
    }
    assert!(!linux_uses_kde(None));
}

#[test]
fn kioslaverc_path_cases() {
    let t = tempfile::tempdir().expect("tempdir");
    let home = t.path();
    let via_home = home.join(".config").join("kioslaverc");
    assert_eq!(
        kioslaverc_path(Some(&home.join("cfg")), Some(home)),
        Some(home.join("cfg").join("kioslaverc"))
    );
    assert_eq!(
        kioslaverc_path(Some(Path::new("rel")), Some(home)),
        Some(via_home.clone())
    );
    assert_eq!(kioslaverc_path(None, Some(home)), Some(via_home));
    assert_eq!(kioslaverc_path(None, None), None);
    assert_eq!(kioslaverc_path(Some(Path::new("rel")), None), None);
}

#[test]
fn kde_file_read_cases() {
    let t = tempfile::tempdir().expect("tempdir");
    let path = t.path().join("kioslaverc");

    start();
    assert_eq!(read_kde(&path), SystemProxy::default());
    assert!(warns().is_empty(), "a missing file is not a warning");

    std::fs::write(
        &path,
        "[Proxy Settings]\nProxyType=1\nhttpsProxy=http://host 8080\n",
    )
    .expect("write kioslaverc");
    start();
    assert_eq!(
        read_kde(&path),
        sys(proxy("http://host:8080", &[]), None, false)
    );
    assert!(warns().is_empty());

    // A read error other than "not found" (here: the path is a directory) is the default with one warning.
    start();
    assert_eq!(read_kde(t.path()), SystemProxy::default());
    assert_eq!(warns().len(), 1);
}

#[test]
fn parse_utc_offset_cases() {
    for (s, want) in [("+0130", 5400), ("-0800", -28_800), ("+0000", 0)] {
        assert_eq!(parse_utc_offset(s), Some(want), "{s:?}");
        let lf = format!("{s}\n");
        assert_eq!(parse_utc_offset(&lf), Some(want), "{lf:?}");
    }
    for s in [
        "",
        "garbage",
        "+2500",
        "0100",
        "+01:00",
        "+0130\n\n",
        "+0160",
        "+013",
        "+01300",
    ] {
        assert_eq!(parse_utc_offset(s), None, "{s:?}");
    }
}

#[test]
fn offset_range_cases() {
    assert_eq!(cf_offset_secs(3600.4), Some(3600));
    assert_eq!(cf_offset_secs(-50_400.0), Some(-50_400));
    assert_eq!(cf_offset_secs(50_400.0), Some(50_400));
    assert_eq!(cf_offset_secs(50_401.0), None);
    assert_eq!(cf_offset_secs(-50_401.0), None);
    assert_eq!(cf_offset_secs(f64::NAN), None);
    assert_eq!(cf_offset_secs(f64::INFINITY), None);
    assert_eq!(cf_offset_secs(f64::NEG_INFINITY), None);
}

#[cfg(unix)]
#[test]
fn bounded_run_abandons_and_reaps() {
    use std::time::Instant;

    let mut printf = Command::new("printf");
    printf.arg("'manual'");
    let run = run_bounded(vec![printf], Duration::from_secs(2));
    assert_eq!(run.outputs, Some(vec!["'manual'".to_owned()]));
    assert!(run.abandoned.is_empty());

    let mut sleep = Command::new("sleep");
    sleep.arg("1");
    let started = Instant::now();
    let run = run_bounded(vec![sleep], Duration::from_millis(300));
    let elapsed = started.elapsed();
    assert_eq!(run.outputs, None);
    assert!(
        elapsed < Duration::from_millis(900),
        "returned after {elapsed:?}"
    );
    assert_eq!(run.abandoned.len(), 1);

    #[cfg(target_os = "linux")]
    {
        let proc_dir = Path::new("/proc").join(run.abandoned[0].to_string());
        let deadline = Instant::now() + Duration::from_secs(3);
        while proc_dir.exists() && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(50));
        }
        assert!(!proc_dir.exists(), "the abandoned child was not reaped");
    }
}
