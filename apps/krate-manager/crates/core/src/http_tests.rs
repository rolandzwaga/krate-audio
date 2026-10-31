//! http tests (plan §4.5, §4.5.1, §7.1 rows SC-012, SC-018, SC-026, SC-028, SC-030, SC-034 to SC-036, SC-041). Every
//! socket is a loopback `FixtureServer`; every PAC host is a `FakePacHost` (TA-12); no test resolves a real name.

use super::*;
use crate::context::AppContext;
use crate::feed::{self, FetchMode, MAX_FEED_BYTES, SnapshotSource};
use crate::fixture_server::{FixtureServer, Reply};
use crate::pac::tests::{FakePacHost, OsAnswer};
use crate::pac::{OsProxyEntry, OsProxyKind};
use crate::settings::Settings;
use crate::test_support::{fresh_temp_roots, install_capture, take_mine};
use std::io::Write;
use std::sync::mpsc;
use std::thread;

const SEC: Duration = Duration::from_secs(1);
const FEED: &[u8] =
    include_bytes!("../../../../../tools/feed-contract/fixtures/feeds/valid/v1-only.json");

fn u(s: &str) -> url::Url {
    url::Url::parse(s).expect("a test URL parses")
}

fn code_of(r: &Result<Vec<u8>, HttpError>) -> &'static str {
    match r {
        Ok(_) => "ok",
        Err(e) => e.code(),
    }
}

fn ok(body: &[u8]) -> Reply {
    Reply::Ok {
        body: body.to_vec(),
        content_type: "application/json",
    }
}

fn script(text: &str) -> Reply {
    Reply::Ok {
        body: text.as_bytes().to_vec(),
        content_type: "application/x-ns-proxy-autoconfig",
    }
}

fn serve(routes: Vec<(&str, Reply)>) -> FixtureServer {
    FixtureServer::start(routes.into_iter().map(|(p, r)| (p.to_owned(), r)).collect())
}

fn authority(s: &FixtureServer) -> String {
    format!("127.0.0.1:{}", s.port())
}

fn connect_line(target: &FixtureServer) -> String {
    format!("CONNECT {}", authority(target))
}

/// "METHOD path" per recorded request.
fn lines(s: &FixtureServer) -> Vec<String> {
    s.requests()
        .iter()
        .map(|r| format!("{} {}", r.method, r.path))
        .collect()
}

fn proxy_config(s: &FixtureServer) -> ProxyConfig {
    ProxyConfig {
        url: format!("http://127.0.0.1:{}", s.port()),
        no_proxy: Vec::new(),
    }
}

/// A proxy whose `CONNECT` to `target` is refused with 407.
fn refusing_proxy(target: &FixtureServer) -> FixtureServer {
    serve(vec![(&authority(target), Reply::ConnectRefused(407))])
}

/// A proxy that tunnels to `target` and answers the tunnelled `/versions.json` with `reply`.
fn tunnel_proxy(target: &FixtureServer, reply: Reply) -> FixtureServer {
    serve(vec![
        (&authority(target), Reply::ConnectOk),
        ("/versions.json", reply),
    ])
}

/// The calling thread's captured `warn!` messages, removed from the store.
fn warns() -> Vec<String> {
    take_mine()
        .into_iter()
        .filter(|(_, level, _)| *level == log::Level::Warn)
        .map(|(_, _, message)| message)
        .collect()
}

fn os_http(port: u16) -> OsProxyEntry {
    OsProxyEntry {
        kind: OsProxyKind::Http,
        host: "127.0.0.1".to_owned(),
        port,
    }
}

fn os_direct() -> OsProxyEntry {
    OsProxyEntry {
        kind: OsProxyKind::Direct,
        host: String::new(),
        port: 0,
    }
}

fn gzip(bytes: &[u8]) -> Vec<u8> {
    let mut encoder = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
    encoder.write_all(bytes).expect("gzip write");
    encoder.finish().expect("gzip finish")
}

/// Polls `done` every 5 ms for at most `within`.
fn eventually(within: Duration, done: impl Fn() -> bool) -> bool {
    let start = Instant::now();
    while start.elapsed() < within {
        if done() {
            return true;
        }
        thread::sleep(Duration::from_millis(5));
    }
    done()
}

/// A context whose cache lives in a fresh temp state dir (Rule 1).
fn context(roots: crate::roots::Roots, feed_url: String, http: HttpConfig) -> AppContext {
    AppContext {
        roots,
        settings: Settings::default(),
        feed_url,
        http,
    }
}

// ---- pure, every host ---------------------------------------------------------------------------------------------

#[test]
fn production_values() {
    assert_eq!(CONNECT_TIMEOUT, Duration::from_secs(10));
    assert_eq!(TOTAL_TIMEOUT, Duration::from_secs(30));
    assert_eq!(
        HttpConfig::production(),
        HttpConfig {
            policy: UrlPolicy::HttpsOnly,
            connect_timeout: Duration::from_secs(10),
            total_timeout: Duration::from_secs(30),
            proxy: None,
            pac: None,
            pac_host: None,
        }
    );
}

#[test]
fn url_policy_cases() {
    assert_eq!(
        check_url(UrlPolicy::HttpsOnly, &u("https://example.invalid/x")),
        Ok(())
    );
    for refused in [
        "http://127.0.0.1/",
        "ftp://example.invalid/x",
        "file:///x",
        "https://u:p@host.invalid/",
    ] {
        let e = check_url(UrlPolicy::HttpsOnly, &u(refused)).expect_err(refused);
        assert_eq!(e.code(), "E_FEED_URL", "{refused}");
        assert!(!e.to_string().contains("u:p"), "{e}");
    }
    for accepted in [
        "http://127.0.0.1:9/x",
        "http://[::1]:9/x",
        "https://127.0.0.1/x",
    ] {
        assert_eq!(
            check_url(UrlPolicy::LoopbackOnly, &u(accepted)),
            Ok(()),
            "{accepted}"
        );
    }
    for refused in [
        "http://localhost/",
        "https://example.invalid/",
        "http://10.0.0.1/",
        "http://127.0.0.2/",
        "ftp://127.0.0.1/",
        "http://u:p@127.0.0.1/",
    ] {
        let e = check_url(UrlPolicy::LoopbackOnly, &u(refused)).expect_err(refused);
        assert_eq!(e.code(), "E_FEED_URL", "{refused}");
    }
}

#[test]
fn next_hop_cases() {
    let a = u("https://a.invalid/");
    let hop = |status, location| next_hop(UrlPolicy::HttpsOnly, &a, status, location);
    assert_eq!(
        hop(302, Some("http://b.invalid/")).map_err(|e| e.code()),
        Err("E_FEED_URL")
    );
    assert_eq!(
        hop(302, Some("/v2.json")),
        Ok(Hop::Follow(u("https://a.invalid/v2.json")))
    );
    assert_eq!(
        hop(302, Some("https://b.invalid/x")),
        Ok(Hop::Follow(u("https://b.invalid/x")))
    );
    assert_eq!(hop(302, None).map_err(|e| e.code()), Err("E_FEED_REDIRECT"));
    assert_eq!(hop(200, None), Ok(Hop::Done));
    assert_eq!(hop(404, Some("https://b.invalid/x")), Ok(Hop::Done));
    for status in [301, 303, 307, 308] {
        assert_eq!(
            hop(status, Some("/x")),
            Ok(Hop::Follow(u("https://a.invalid/x"))),
            "{status}"
        );
    }
    assert_eq!(
        next_hop(
            UrlPolicy::LoopbackOnly,
            &u("http://127.0.0.1:9/"),
            307,
            Some("http://example.invalid/")
        )
        .map_err(|e| e.code()),
        Err("E_FEED_URL")
    );
}

#[test]
fn proxy_scheme_cases() {
    let proxy = |url: String| ProxyConfig {
        url,
        no_proxy: Vec::new(),
    };
    for scheme in ["http", "https", "socks4", "socks5"] {
        let p = proxy(format!("{scheme}://proxy.example.invalid:3128"));
        assert_eq!(check_proxy(UrlPolicy::HttpsOnly, &p), Ok(()), "{scheme}");
    }
    for scheme in ["ftp", "socks4a", "socks5h"] {
        let p = proxy(format!("{scheme}://proxy.example.invalid:3128"));
        assert_eq!(
            check_proxy(UrlPolicy::HttpsOnly, &p).map_err(|e| e.code()),
            Err("E_FEED_URL"),
            "{scheme}"
        );
    }
    // The loopback host rule holds for the non-special schemes too (an opaque `127.0.0.1` host).
    for url in [
        "socks5://127.0.0.1:1080",
        "socks4://127.0.0.1:1080",
        "http://[::1]:1",
    ] {
        assert_eq!(
            check_proxy(UrlPolicy::LoopbackOnly, &proxy(url.to_owned())),
            Ok(()),
            "{url}"
        );
    }
    for url in ["socks5://localhost:1080", "http://10.0.0.1:3128"] {
        assert_eq!(
            check_proxy(UrlPolicy::LoopbackOnly, &proxy(url.to_owned())).map_err(|e| e.code()),
            Err("E_FEED_URL"),
            "{url}"
        );
    }
}

#[test]
fn agent_trusts_the_os_store() {
    #[cfg(windows)]
    let purposes = [AgentPurpose::Feed];
    #[cfg(not(windows))]
    let purposes = [AgentPurpose::Feed, AgentPurpose::PacFetch];
    for config in [HttpConfig::production(), HttpConfig::loopback(SEC, SEC)] {
        for purpose in &purposes {
            let agent = agent_config(&config, None, *purpose);
            assert!(
                matches!(
                    agent.tls_config().root_certs(),
                    ureq::tls::RootCerts::PlatformVerifier
                ),
                "{:?} {purpose:?}",
                config.policy
            );
        }
    }
    assert!(agent_config(&HttpConfig::production(), None, AgentPurpose::Feed).https_only());
    #[cfg(not(windows))]
    assert!(!agent_config(&HttpConfig::production(), None, AgentPurpose::PacFetch).https_only());
    assert!(!agent_config(&HttpConfig::loopback(SEC, SEC), None, AgentPurpose::Feed).https_only());
    let agent = agent_config(
        &HttpConfig::loopback(SEC, 2 * SEC),
        None,
        AgentPurpose::Feed,
    );
    assert_eq!(agent.max_redirects(), 0);
    assert!(!agent.http_status_as_error());
    assert!(agent.proxy().is_none());
}

#[test]
fn config_for_dev_url_cases() {
    let static_proxy = ProxyResolution {
        proxy: Some(ProxyConfig {
            url: "http://127.0.0.1:3128".to_owned(),
            no_proxy: vec!["a.invalid".to_owned()],
        }),
        pac: None,
    };
    let pac_source = ProxyResolution {
        proxy: None,
        pac: Some(PacSource {
            auto_detect: true,
            url: Some("http://127.0.0.1:1/p.pac".to_owned()),
        }),
    };
    let (_fake, handle) = FakePacHost::new().handle();
    let (_other, other) = FakePacHost::new().handle();
    for (resolution, host) in [(static_proxy, None), (pac_source, Some(handle.clone()))] {
        for loopback in [
            "http://127.0.0.1:8080/versions.json",
            "http://[::1]:8080/versions.json",
        ] {
            let c = config_for_dev_url(loopback, resolution.clone(), host.clone());
            assert_eq!(
                c,
                HttpConfig::loopback(CONNECT_TIMEOUT, TOTAL_TIMEOUT),
                "{loopback}"
            );
            assert!(c.proxy.is_none() && c.pac.is_none() && c.pac_host.is_none());
        }
        let kept = config_for_dev_url(
            "https://feed.example.invalid/versions.json",
            resolution.clone(),
            host.clone(),
        );
        assert_eq!(
            kept,
            HttpConfig {
                proxy: resolution.proxy.clone(),
                pac: resolution.pac.clone(),
                pac_host: host.clone(),
                ..HttpConfig::production()
            }
        );
        if host.is_some() {
            assert_eq!(kept.pac_host, Some(handle.clone()));
            assert_ne!(kept.pac_host, Some(other.clone()));
        }
    }
    let localhost = config_for_dev_url("http://localhost:8080/", ProxyResolution::default(), None);
    assert_eq!(localhost, HttpConfig::production());
    assert_eq!(
        code_of(&get(&localhost, "http://localhost:8080/", &[], 16)),
        "E_FEED_URL"
    );
}

#[test]
fn resolve_proxy_cases() {
    install_capture();
    take_mine();
    let target = "https://feed.example.invalid/versions.json";
    let env = |https: Option<&str>, all: Option<&str>, no: Option<&str>| ProxyEnv {
        https_proxy: https.map(str::to_owned),
        all_proxy: all.map(str::to_owned),
        no_proxy: no.map(str::to_owned),
    };
    let pc = |url: &str, no: &[&str]| ProxyConfig {
        url: url.to_owned(),
        no_proxy: no.iter().map(|s| (*s).to_owned()).collect(),
    };
    let system =
        |proxy: Option<ProxyConfig>, pac_url: Option<&str>, auto_detect: bool| SystemProxy {
            proxy,
            pac_url: pac_url.map(str::to_owned),
            auto_detect,
        };
    let none = SystemProxy::default();
    let os_pac_url = "http://pac.example.invalid/p.pac";
    let full_os = system(
        Some(pc("http://os.example.invalid:8080", &["corp.invalid"])),
        Some(os_pac_url),
        true,
    );

    // The environment (FR-048 (a)).
    assert_eq!(
        resolve_proxy(&env(Some("http://e1.invalid:1"), None, None), &none, target),
        ProxyResolution {
            proxy: Some(pc("http://e1.invalid:1", &[])),
            pac: None
        }
    );
    assert_eq!(
        resolve_proxy(
            &env(None, Some("socks5://e2.invalid:2"), None),
            &none,
            target
        ),
        ProxyResolution {
            proxy: Some(pc("socks5://e2.invalid:2", &[])),
            pac: None
        }
    );
    assert_eq!(
        resolve_proxy(
            &env(
                Some("http://e1.invalid:1"),
                Some("http://e2.invalid:2"),
                None
            ),
            &none,
            target
        )
        .proxy,
        Some(pc("http://e1.invalid:1", &[]))
    );
    // An environment proxy wins over every OS setting, PAC included.
    assert_eq!(
        resolve_proxy(
            &env(
                Some("http://e1.invalid:1"),
                None,
                Some("a.invalid, b.invalid")
            ),
            &full_os,
            target
        ),
        ProxyResolution {
            proxy: Some(pc("http://e1.invalid:1", &["a.invalid", "b.invalid"])),
            pac: None
        }
    );
    // A schemeless environment value is an http proxy.
    assert_eq!(
        resolve_proxy(&env(Some("u:p@e3.invalid:3128"), None, None), &none, target).proxy,
        Some(pc("http://u:p@e3.invalid:3128", &[]))
    );
    assert!(warns().is_empty());

    // The OS settings (FR-048 (b), FR-069).
    assert_eq!(
        resolve_proxy(
            &ProxyEnv::default(),
            &system(
                Some(pc("http://os.example.invalid:8080", &["x.invalid"])),
                None,
                false
            ),
            target
        ),
        ProxyResolution {
            proxy: Some(pc("http://os.example.invalid:8080", &["x.invalid"])),
            pac: None
        }
    );
    assert_eq!(
        resolve_proxy(
            &env(None, None, Some("y.invalid")),
            &system(
                Some(pc("http://os.example.invalid:8080", &["x.invalid"])),
                None,
                false
            ),
            target
        )
        .proxy,
        Some(pc(
            "http://os.example.invalid:8080",
            &["x.invalid", "y.invalid"]
        ))
    );
    assert_eq!(
        resolve_proxy(&ProxyEnv::default(), &full_os, target),
        ProxyResolution {
            proxy: Some(pc("http://os.example.invalid:8080", &["corp.invalid"])),
            pac: Some(PacSource {
                auto_detect: true,
                url: Some(os_pac_url.to_owned())
            }),
        }
    );
    assert!(
        warns().is_empty(),
        "a static proxy beside a PAC source is the normal case"
    );
    // The OS bypass list applies to the static proxy only (FR-048 (c)).
    assert_eq!(
        resolve_proxy(
            &ProxyEnv::default(),
            &full_os,
            "https://www.corp.invalid/versions.json"
        ),
        ProxyResolution {
            proxy: Some(pc("http://os.example.invalid:8080", &["corp.invalid"])),
            pac: Some(PacSource {
                auto_detect: true,
                url: Some(os_pac_url.to_owned())
            }),
        },
        "an exact entry does not match a subdomain"
    );
    assert_eq!(
        resolve_proxy(
            &ProxyEnv::default(),
            &full_os,
            "https://corp.invalid/versions.json"
        ),
        ProxyResolution {
            proxy: None,
            pac: Some(PacSource {
                auto_detect: true,
                url: Some(os_pac_url.to_owned())
            }),
        }
    );
    let local = system(
        Some(pc("http://os.example.invalid:8080", &["<local>"])),
        None,
        false,
    );
    assert_eq!(
        resolve_proxy(
            &ProxyEnv::default(),
            &local,
            "https://intranet/versions.json"
        )
        .proxy,
        None
    );
    assert!(
        resolve_proxy(&ProxyEnv::default(), &local, target)
            .proxy
            .is_some()
    );
    // PAC source only: url only, auto-detect only, both.
    for (pac_url, auto_detect) in [
        (Some(os_pac_url), false),
        (None, true),
        (Some(os_pac_url), true),
    ] {
        assert_eq!(
            resolve_proxy(
                &ProxyEnv::default(),
                &system(None, pac_url, auto_detect),
                target
            ),
            ProxyResolution {
                proxy: None,
                pac: Some(PacSource {
                    auto_detect,
                    url: pac_url.map(str::to_owned)
                }),
            }
        );
    }
    assert_eq!(
        resolve_proxy(&ProxyEnv::default(), &none, target),
        ProxyResolution::default()
    );

    // A no-proxy list matching the target ends every source, PAC included (FR-048 (c), FR-069 (3)).
    for (no_proxy, host_target) in [
        ("feed.example.invalid", target),
        (".example.invalid", target),
        ("*.example.invalid", target),
        ("10.1.2.3", "https://10.1.2.3/versions.json"),
        ("10.0.0.0/8", "https://10.1.2.3/versions.json"),
        ("10.*", "https://10.1.2.3/versions.json"),
        ("*", target),
    ] {
        assert_eq!(
            resolve_proxy(
                &env(Some("http://e1.invalid:1"), None, Some(no_proxy)),
                &full_os,
                host_target
            ),
            ProxyResolution::default(),
            "{no_proxy}"
        );
        assert_eq!(
            resolve_proxy(&env(None, None, Some(no_proxy)), &full_os, host_target),
            ProxyResolution::default(),
            "{no_proxy} with a PAC source"
        );
    }
    assert!(warns().is_empty());

    // A malformed proxy value is dropped with one warn, then the next stage applies.
    assert_eq!(
        resolve_proxy(&env(Some("ftp://e.invalid:1"), None, None), &none, target),
        ProxyResolution::default()
    );
    assert_eq!(warns().len(), 1);
    assert_eq!(
        resolve_proxy(
            &env(Some("socks5h://u:p@e.invalid:1"), None, None),
            &system(Some(pc("http://os.example.invalid:8080", &[])), None, false),
            target
        )
        .proxy,
        Some(pc("http://os.example.invalid:8080", &[]))
    );
    let w = warns();
    assert_eq!(w.len(), 1);
    assert!(w[0].contains("***") && !w[0].contains("u:p"), "{w:?}");
    assert_eq!(
        resolve_proxy(
            &ProxyEnv::default(),
            &system(Some(pc("gopher://os.example.invalid:70", &[])), None, false),
            target
        ),
        ProxyResolution::default()
    );
    assert_eq!(warns().len(), 1);
    // An unparsable pac_url is dropped with one warn; auto_detect still applies.
    assert_eq!(
        resolve_proxy(
            &ProxyEnv::default(),
            &system(None, Some("::not a url"), true),
            target
        ),
        ProxyResolution {
            proxy: None,
            pac: Some(PacSource {
                auto_detect: true,
                url: None
            })
        }
    );
    assert_eq!(warns().len(), 1);
    // An unparsable target is direct, with one warn.
    assert_eq!(
        resolve_proxy(
            &env(Some("http://e1.invalid:1"), None, None),
            &full_os,
            "not a url"
        ),
        ProxyResolution::default()
    );
    assert_eq!(warns().len(), 1);

    // PD-18's bypass grammar.
    let list = |entries: &[&str]| entries.iter().map(|s| (*s).to_owned()).collect::<Vec<_>>();
    for (entries, host, expected) in [
        (list(&["*"]), "anything.invalid", true),
        (list(&["<local>"]), "intranet", true),
        (list(&["<LOCAL>"]), "intranet.corp", false),
        (list(&["10.1.2.3"]), "10.1.2.3", true),
        (list(&["10.1.2.3"]), "10.1.2.4", false),
        (list(&["10.0.0.0/8"]), "10.200.0.1", true),
        (list(&["10.0.0.0/8"]), "11.0.0.1", false),
        (list(&["169.254/16"]), "169.254.3.4", true),
        (list(&["192.168.*"]), "192.168.7.7", true),
        (list(&["192.168.*"]), "192.169.0.1", false),
        (list(&["::1/128"]), "[::1]", true),
        (list(&["fd00::/8"]), "fd12::1", true),
        (list(&["::1"]), "[::1]", true),
        (list(&["[::1]:8080"]), "::1", true),
        (list(&["*.corp.invalid"]), "a.corp.invalid", true),
        (list(&["*.corp.invalid"]), "corp.invalid", false),
        (list(&[".corp.invalid"]), "b.a.corp.invalid", true),
        (list(&[".corp.invalid"]), "corp.invalid", false),
        (list(&["Host.Invalid"]), "host.invalid", true),
        (list(&["host.invalid:8080"]), "HOST.invalid", true),
        (list(&["host.invalid"]), "ahost.invalid", false),
        (list(&["", " ", "x.invalid"]), "x.invalid", true),
        (list(&[]), "x.invalid", false),
    ] {
        assert_eq!(bypasses(&entries, host), expected, "{entries:?} {host}");
    }

    // Redaction (PD-19).
    let source = PacSource {
        auto_detect: false,
        url: Some("http://u:p@127.0.0.1/x.pac?token=s".to_owned()),
    };
    let shown = format!("{source:?}");
    assert!(
        !shown.contains("u:p") && !shown.contains("token"),
        "{shown}"
    );
    let secret = pc("http://u:p@127.0.0.1:3128", &[]);
    for shown in [format!("{secret:?}"), format!("{secret}")] {
        assert!(shown.contains("***") && !shown.contains("u:p"), "{shown}");
    }
}

#[test]
fn proxy_env_from_vars_cases() {
    let s = |v: &str| Some(v.to_owned());
    let upper_only = ProxyVars {
        https_upper: s("http://u.invalid:1"),
        ..ProxyVars::default()
    };
    assert_eq!(
        ProxyEnv::from_vars(&upper_only).https_proxy,
        s("http://u.invalid:1")
    );
    let lower_only = ProxyVars {
        https_lower: s("http://l.invalid:1"),
        ..ProxyVars::default()
    };
    assert_eq!(
        ProxyEnv::from_vars(&lower_only).https_proxy,
        s("http://l.invalid:1")
    );
    let both = ProxyVars {
        https_upper: s("http://u.invalid:1"),
        https_lower: s("http://l.invalid:1"),
        ..ProxyVars::default()
    };
    assert_eq!(
        ProxyEnv::from_vars(&both).https_proxy,
        s("http://u.invalid:1")
    );
    let empty_upper = ProxyVars {
        https_upper: s(""),
        https_lower: s("http://l.invalid:1"),
        ..ProxyVars::default()
    };
    assert_eq!(
        ProxyEnv::from_vars(&empty_upper).https_proxy,
        s("http://l.invalid:1")
    );
    let both_empty = ProxyVars {
        https_upper: s(""),
        https_lower: s(""),
        ..ProxyVars::default()
    };
    assert_eq!(ProxyEnv::from_vars(&both_empty), ProxyEnv::default());
    let all_only = ProxyVars {
        all_lower: s("socks5://a.invalid:1"),
        ..ProxyVars::default()
    };
    let merged = ProxyEnv::from_vars(&all_only);
    assert_eq!(
        merged,
        ProxyEnv {
            all_proxy: s("socks5://a.invalid:1"),
            ..ProxyEnv::default()
        }
    );
    assert_eq!(
        resolve_proxy(
            &merged,
            &SystemProxy::default(),
            "https://feed.example.invalid/"
        )
        .proxy,
        Some(ProxyConfig {
            url: "socks5://a.invalid:1".to_owned(),
            no_proxy: Vec::new()
        })
    );
    for (upper, lower, expected) in [
        (s("a.invalid"), None, s("a.invalid")),
        (None, s("b.invalid"), s("b.invalid")),
        (s("a.invalid"), s("b.invalid"), s("a.invalid")),
        (s(""), s("b.invalid"), s("b.invalid")),
        (s(""), s(""), None),
    ] {
        let vars = ProxyVars {
            no_upper: upper,
            no_lower: lower,
            ..ProxyVars::default()
        };
        assert_eq!(ProxyEnv::from_vars(&vars).no_proxy, expected);
    }
    for secret in ["http://u:p@127.0.0.1:1", "u:p@127.0.0.1:1"] {
        let vars = ProxyVars {
            https_upper: s(secret),
            all_lower: s(secret),
            no_upper: s(secret),
            ..ProxyVars::default()
        };
        let env = ProxyEnv::from_vars(&vars);
        for shown in [format!("{vars:?}"), format!("{env:?}")] {
            assert!(shown.contains("***") && !shown.contains("u:p"), "{shown}");
        }
    }
    let system = SystemProxy {
        proxy: None,
        pac_url: s("http://u:p@127.0.0.1/x.pac"),
        auto_detect: false,
    };
    let shown = format!("{system:?}");
    assert!(shown.contains("***") && !shown.contains("u:p"), "{shown}");
}

#[test]
fn no_response_class_cases() {
    use ureq::Error as E;
    fn rustls_io() -> io::Error {
        io::Error::new(
            io::ErrorKind::InvalidData,
            rustls::Error::General("bad certificate".to_owned()),
        )
    }
    /// (error, deadline_left, advances, code through map_error(_, Call))
    type Row = (fn() -> E, bool, bool, &'static str);
    let rows: Vec<Row> = vec![
        (|| E::HostNotFound, true, true, "E_FEED_OFFLINE"),
        (|| E::ConnectionFailed, true, true, "E_FEED_OFFLINE"),
        (
            || E::ConnectProxyFailed("proxy server responded 407".to_owned()),
            true,
            true,
            "E_FEED_OFFLINE",
        ),
        // A connect-phase timeout advances only while time is left; it is still FR-044's E_FEED_TIMEOUT.
        (
            || E::Timeout(ureq::Timeout::Connect),
            true,
            true,
            "E_FEED_TIMEOUT",
        ),
        (
            || E::Timeout(ureq::Timeout::Connect),
            false,
            false,
            "E_FEED_TIMEOUT",
        ),
        (
            || E::Timeout(ureq::Timeout::Resolve),
            true,
            true,
            "E_FEED_TIMEOUT",
        ),
        (
            || E::Timeout(ureq::Timeout::Global),
            true,
            false,
            "E_FEED_TIMEOUT",
        ),
        (
            || E::Io(io::Error::from(io::ErrorKind::ConnectionRefused)),
            true,
            true,
            "E_FEED_OFFLINE",
        ),
        (
            || E::Io(io::Error::from(io::ErrorKind::ConnectionReset)),
            true,
            true,
            "E_FEED_OFFLINE",
        ),
        (|| E::Io(rustls_io()), true, false, "E_FEED_TLS"),
        (
            || {
                E::Io(io::Error::new(
                    io::ErrorKind::ConnectionReset,
                    rustls::Error::General("x".to_owned()),
                ))
            },
            true,
            false,
            "E_FEED_TLS",
        ),
        (
            || E::Rustls(rustls::Error::General("x".to_owned())),
            true,
            false,
            "E_FEED_TLS",
        ),
        (|| E::Tls("tls"), true, false, "E_FEED_TLS"),
        (|| E::StatusCode(503), true, false, "E_FEED_OFFLINE"),
        (
            || E::RequireHttpsOnly("http://u:p@a.invalid/".to_owned()),
            true,
            false,
            "E_FEED_URL",
        ),
    ];
    for (make, left, advances, code) in rows {
        let e = make();
        assert_eq!(is_no_response(&e, left), advances, "{e:?} left={left}");
        let mapped = map_error(make(), Phase::Call);
        assert_eq!(mapped.code(), code, "{e:?}");
        assert!(!mapped.to_string().contains("u:p"), "{mapped}");
        if advances && !matches!(e, E::Timeout(_)) {
            assert_eq!(
                mapped.code(),
                "E_FEED_OFFLINE",
                "advance and offline agree: {e:?}"
            );
        }
    }
    // Reading a 200 body.
    assert_eq!(
        map_error(
            E::Io(io::Error::from(io::ErrorKind::UnexpectedEof)),
            Phase::Body
        )
        .code(),
        "E_FEED_BODY"
    );
    assert_eq!(map_error(E::BodyStalled, Phase::Body).code(), "E_FEED_BODY");
    assert_eq!(
        map_error(E::Timeout(ureq::Timeout::Global), Phase::Body).code(),
        "E_FEED_TIMEOUT"
    );
    assert_eq!(
        map_error(E::Io(rustls_io()), Phase::Body).code(),
        "E_FEED_TLS"
    );
    // The body reader's io error that wraps a ureq error is turned back into it (FR-044).
    let wrapped = E::Timeout(ureq::Timeout::Global).into_io();
    assert_eq!(
        map_error(E::from(wrapped), Phase::Body).code(),
        "E_FEED_TIMEOUT"
    );
}

#[test]
fn bounded_call_abandons_after_wait() {
    let start = Instant::now();
    assert_eq!(
        bounded(
            || {
                thread::sleep(2 * SEC);
                1
            },
            Duration::from_millis(100)
        ),
        None
    );
    assert!(start.elapsed() < SEC, "{:?}", start.elapsed());
    assert_eq!(bounded(|| 7, SEC), Some(7));
}

#[test]
fn pac_policy_cases() {
    for accepted in [
        "http://pac.example.invalid/p.pac",
        "https://pac.example.invalid/p.pac",
    ] {
        assert_eq!(
            check_pac_url(UrlPolicy::HttpsOnly, &u(accepted)),
            Ok(()),
            "{accepted}"
        );
    }
    for refused in [
        "file:///p.pac",
        "ftp://pac.example.invalid/p.pac",
        "data:,x",
        "http://u:p@pac.example.invalid/p.pac",
    ] {
        let e = check_pac_url(UrlPolicy::HttpsOnly, &u(refused)).expect_err(refused);
        assert_eq!(e.code(), "E_FEED_URL", "{refused}");
        assert!(!e.to_string().contains("u:p"), "{e}");
    }
    assert_eq!(
        check_pac_url(
            UrlPolicy::HttpsOnly,
            &u("http://pac.example.invalid/p.pac?token=s")
        ),
        Ok(())
    );
    for accepted in ["http://127.0.0.1:1/p.pac", "http://[::1]:1/p.pac"] {
        assert_eq!(
            check_pac_url(UrlPolicy::LoopbackOnly, &u(accepted)),
            Ok(()),
            "{accepted}"
        );
    }
    for refused in [
        "http://pac.example.invalid/p.pac",
        "http://localhost:1/p.pac",
        "file:///p.pac",
    ] {
        let e = check_pac_url(UrlPolicy::LoopbackOnly, &u(refused)).expect_err(refused);
        assert_eq!(e.code(), "E_FEED_URL", "{refused}");
    }
    let e = check_pac_url(
        UrlPolicy::LoopbackOnly,
        &u("http://example.invalid/p.pac?token=s"),
    )
    .expect_err("non-loopback");
    assert!(!e.to_string().contains("token"), "{e}");

    #[cfg(not(windows))]
    {
        install_capture();
        take_mine();
        // Pure rows over next_pac_hop: an https -> http PAC redirect is followed where next_hop refuses it.
        let pac = u("https://pac.example.invalid/p.pac");
        assert_eq!(
            next_pac_hop(
                UrlPolicy::HttpsOnly,
                &pac,
                302,
                Some("http://pac.example.invalid/q.pac")
            ),
            Ok(Hop::Follow(u("http://pac.example.invalid/q.pac")))
        );
        assert_eq!(
            next_hop(
                UrlPolicy::HttpsOnly,
                &pac,
                302,
                Some("http://pac.example.invalid/q.pac")
            )
            .map_err(|e| e.code()),
            Err("E_FEED_URL")
        );
        assert_eq!(
            next_pac_hop(UrlPolicy::HttpsOnly, &pac, 302, Some("file:///p.pac"))
                .map_err(|e| e.code()),
            Err("E_FEED_URL")
        );
        assert_eq!(
            next_pac_hop(
                UrlPolicy::LoopbackOnly,
                &u("http://127.0.0.1:1/p.pac"),
                302,
                Some("http://example.invalid/p.pac")
            )
            .map_err(|e| e.code()),
            Err("E_FEED_URL")
        );

        let c = serve(vec![("/versions.json", ok(b"direct"))]);
        let target = c.url("/versions.json");
        let config_for = |pac_url: String| {
            let (_fake, handle) = FakePacHost::new().handle();
            HttpConfig::loopback(SEC, 2 * SEC)
                .with_pac(Some(PacSource {
                    auto_detect: false,
                    url: Some(pac_url),
                }))
                .with_pac_host(Some(handle))
        };
        // A non-loopback PAC host under LoopbackOnly: refused, the server records nothing, the fallback is direct.
        let a = serve(vec![(
            "/p.pac",
            script("function FindProxyForURL(u, h) { return \"DIRECT\"; }"),
        )]);
        let start = Instant::now();
        assert_eq!(
            get(
                &config_for(format!("http://localhost:{}/p.pac", a.port())),
                &target,
                &[],
                64
            ),
            Ok(b"direct".to_vec())
        );
        assert!(start.elapsed() < 3 * SEC);
        assert!(a.requests().is_empty());
        let w = warns();
        assert_eq!(w.len(), 1, "{w:?}");
        assert!(w[0].starts_with("E_PAC_FETCH: "), "{w:?}");
        assert_eq!(lines(&c), ["GET /versions.json"]);
        // A script naming example.invalid:80 under LoopbackOnly: E_FEED_URL, no socket.
        let a = serve(vec![(
            "/p.pac",
            script("function FindProxyForURL(u, h) { return \"PROXY example.invalid:80\"; }"),
        )]);
        let before = c.requests().len();
        let start = Instant::now();
        assert_eq!(
            code_of(&get(&config_for(a.url("/p.pac")), &target, &[], 64)),
            "E_FEED_URL"
        );
        assert!(start.elapsed() < 3 * SEC);
        assert_eq!(c.requests().len(), before);
        take_mine();
        // (d) a PAC redirect to a non-loopback host: one E_PAC_FETCH, the fallback, nothing leaves.
        let a = serve(vec![(
            "/p.pac",
            Reply::Redirect {
                status: 302,
                location: "http://example.invalid/p.pac".to_owned(),
            },
        )]);
        let start = Instant::now();
        assert_eq!(
            get(&config_for(a.url("/p.pac")), &target, &[], 64),
            Ok(b"direct".to_vec())
        );
        assert!(start.elapsed() < 3 * SEC);
        assert_eq!(lines(&a), ["GET /p.pac"]);
        let w = warns();
        assert_eq!(w.len(), 1, "{w:?}");
        assert!(w[0].starts_with("E_PAC_FETCH: "), "{w:?}");
        // (e) a relative PAC redirect is followed and its script used.
        let b = tunnel_proxy(&c, ok(b"via b"));
        let a = serve(vec![
            (
                "/p.pac",
                Reply::Redirect {
                    status: 302,
                    location: "/q.pac".to_owned(),
                },
            ),
            (
                "/q.pac",
                script(&format!(
                    "function FindProxyForURL(u, h) {{ return \"PROXY 127.0.0.1:{}\"; }}",
                    b.port()
                )),
            ),
        ]);
        let start = Instant::now();
        assert_eq!(
            get(&config_for(a.url("/p.pac")), &target, &[], 64),
            Ok(b"via b".to_vec())
        );
        assert!(start.elapsed() < 3 * SEC);
        assert_eq!(lines(&a), ["GET /p.pac", "GET /q.pac"]);
        assert_eq!(lines(&b)[0], connect_line(&c));
        assert!(warns().iter().all(|w| !w.contains("E_PAC")));
    }
}

// ---- transport, every host ----------------------------------------------------------------------------------------

#[test]
fn redirects_are_rechecked() {
    let config = HttpConfig::loopback(SEC, SEC);
    let s = serve(vec![("/feed", ok(b"feed")), ("/v2.json", ok(b"v2"))]);
    let codes = [301, 302, 303, 307, 308];
    let mut previous = "/feed".to_owned();
    for (i, status) in codes.iter().enumerate().rev() {
        let path = format!("/r{i}");
        s.set_route(
            &path,
            Reply::Redirect {
                status: *status,
                location: s.url(&previous),
            },
        );
        previous = path;
    }
    // Five redirects of each kind end at the feed.
    assert_eq!(get(&config, &s.url("/r0"), &[], 64), Ok(b"feed".to_vec()));
    assert_eq!(
        lines(&s),
        [
            "GET /r0",
            "GET /r1",
            "GET /r2",
            "GET /r3",
            "GET /r4",
            "GET /feed"
        ]
    );
    // A sixth is too many.
    s.set_route(
        "/s",
        Reply::Redirect {
            status: 302,
            location: s.url("/r0"),
        },
    );
    assert_eq!(
        code_of(&get(&config, &s.url("/s"), &[], 64)),
        "E_FEED_REDIRECT"
    );
    // No Location.
    s.set_route("/nl", Reply::Status(302));
    assert_eq!(
        code_of(&get(&config, &s.url("/nl"), &[], 64)),
        "E_FEED_REDIRECT"
    );
    // A hop out of policy: refused, nothing further requested.
    s.set_route(
        "/out",
        Reply::Redirect {
            status: 302,
            location: "https://example.invalid/versions.json".to_owned(),
        },
    );
    let before = s.requests().len();
    assert_eq!(
        code_of(&get(&config, &s.url("/out"), &[], 64)),
        "E_FEED_URL"
    );
    assert_eq!(s.requests().len(), before + 1);
    // A relative Location is followed.
    s.set_route(
        "/rel",
        Reply::Redirect {
            status: 302,
            location: "/v2.json".to_owned(),
        },
    );
    assert_eq!(get(&config, &s.url("/rel"), &[], 64), Ok(b"v2".to_vec()));
}

#[test]
fn status_and_body_errors() {
    let config = HttpConfig::loopback(SEC, SEC);
    let max = usize::try_from(MAX_FEED_BYTES).expect("4 MiB fits");
    let zeros_max = gzip(&vec![0u8; max]);
    let zeros_over = gzip(&vec![0u8; max + 1]);
    assert!(zeros_max.len() < 64 * 1024 && zeros_over.len() < 64 * 1024);
    let s = serve(vec![
        ("/404", Reply::Status(404)),
        ("/500", Reply::Status(500)),
        ("/304", Reply::Status(304)),
        ("/exact", ok(&vec![b'a'; max])),
        ("/over", ok(&vec![b'a'; max + 1])),
        ("/gz-exact", Reply::Gzip { body: zeros_max }),
        ("/gz-over", Reply::Gzip { body: zeros_over }),
        (
            "/truncated",
            Reply::Truncated {
                declared_len: 100,
                sent: b"0123456789".to_vec(),
            },
        ),
        (
            "/stall",
            Reply::HeadersThenStall {
                declared_len: 100,
                sent: b"0123456789".to_vec(),
                stall: 30 * SEC,
            },
        ),
        (
            "/not-gzip",
            Reply::Gzip {
                body: b"not gzip".to_vec(),
            },
        ),
    ]);
    for status in [404u16, 500, 304] {
        assert_eq!(
            get(&config, &s.url(&format!("/{status}")), &[], MAX_FEED_BYTES),
            Err(HttpError::Status { status })
        );
    }
    assert_eq!(
        get(&config, &s.url("/exact"), &[], MAX_FEED_BYTES).map(|b| b.len()),
        Ok(max)
    );
    assert_eq!(
        get(&config, &s.url("/over"), &[], MAX_FEED_BYTES),
        Err(HttpError::TooLarge {
            max: MAX_FEED_BYTES
        })
    );
    let decoded =
        get(&config, &s.url("/gz-exact"), &[], MAX_FEED_BYTES).expect("exactly the cap, decoded");
    assert_eq!(decoded.len(), max);
    assert!(decoded.iter().all(|b| *b == 0));
    assert_eq!(
        code_of(&get(&config, &s.url("/gz-over"), &[], MAX_FEED_BYTES)),
        "E_FEED_TOO_LARGE"
    );
    assert_eq!(
        code_of(&get(&config, &s.url("/truncated"), &[], MAX_FEED_BYTES)),
        "E_FEED_BODY"
    );
    let start = Instant::now();
    assert_eq!(
        code_of(&get(&config, &s.url("/stall"), &[], MAX_FEED_BYTES)),
        "E_FEED_TIMEOUT"
    );
    let stalled = start.elapsed();
    assert!(stalled < 2 * SEC, "HeadersThenStall took {stalled:?}");
    println!("HeadersThenStall elapsed {stalled:?}");
    assert_eq!(
        code_of(&get(&config, &s.url("/not-gzip"), &[], MAX_FEED_BYTES)),
        "E_FEED_BODY"
    );
    let tls = get(
        &config,
        &format!("https://127.0.0.1:{}/", s.port()),
        &[],
        MAX_FEED_BYTES,
    );
    assert_eq!(code_of(&tls), "E_FEED_TLS", "{tls:?}");
}

#[test]
fn https_only_refuses_before_connecting() {
    let s = serve(vec![("/versions.json", ok(b"{}"))]);
    assert_eq!(
        code_of(&get(
            &HttpConfig::production(),
            &s.url("/versions.json"),
            &[],
            64
        )),
        "E_FEED_URL"
    );
    assert!(s.requests().is_empty());
}

#[test]
fn chain_shares_one_deadline() {
    let s = serve(vec![("/ok", ok(b"ok"))]);
    for (from, to) in [("/h1", "/h2"), ("/h2", "/h3"), ("/h3", "/ok")] {
        s.set_route(
            from,
            Reply::Delayed {
                after: Duration::from_millis(400),
                reply: Box::new(Reply::Redirect {
                    status: 302,
                    location: s.url(to),
                }),
            },
        );
    }
    let start = Instant::now();
    let result = get(&HttpConfig::loopback(SEC, SEC), &s.url("/h1"), &[], 64);
    let elapsed = start.elapsed();
    assert_eq!(code_of(&result), "E_FEED_TIMEOUT", "{result:?}");
    assert!(elapsed < Duration::from_millis(1500), "{elapsed:?}");
    println!("chain_shares_one_deadline elapsed {elapsed:?}");
    let recorded = lines(&s);
    assert!(recorded.len() <= 3, "{recorded:?}");
    assert!(!recorded.contains(&"GET /ok".to_owned()), "{recorded:?}");
}

// ---- proxy, every host --------------------------------------------------------------------------------------------

#[test]
fn loopback_proxy_only() {
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let b = tunnel_proxy(&c, ok(b"via b"));
    let target = c.url("/versions.json");
    for url in [
        format!("http://localhost:{}", b.port()),
        "http://10.0.0.1:3128".to_owned(),
    ] {
        let config = HttpConfig::loopback(SEC, SEC).with_proxy(Some(ProxyConfig {
            url,
            no_proxy: Vec::new(),
        }));
        assert_eq!(code_of(&get(&config, &target, &[], 64)), "E_FEED_URL");
    }
    assert!(b.requests().is_empty() && c.requests().is_empty());
    // PD-23: a proxy the url crate accepts and ureq refuses fails closed.
    let refused = ProxyConfig {
        url: format!("http://%FF@127.0.0.1:{}", b.port()),
        no_proxy: Vec::new(),
    };
    assert_eq!(check_proxy(UrlPolicy::LoopbackOnly, &refused), Ok(()));
    assert_eq!(
        to_ureq_proxy(&refused).map(|_| ()).map_err(|e| e.code()),
        Err("E_FEED_URL")
    );
    let config = HttpConfig::loopback(SEC, SEC).with_proxy(Some(refused));
    assert_eq!(code_of(&get(&config, &target, &[], 64)), "E_FEED_URL");
    assert!(b.requests().is_empty() && c.requests().is_empty());
}

#[test]
fn proxy_path_connect() {
    install_capture();
    take_mine();
    let c = serve(vec![("/versions.json", ok(b"never"))]);
    let b = serve(vec![(&authority(&c), Reply::ConnectOk)]);
    let target = format!("https://127.0.0.1:{}/versions.json", c.port());
    let config = HttpConfig::loopback(SEC, SEC).with_proxy(Some(proxy_config(&b)));
    let tls = get(&config, &target, &[], 64);
    assert_eq!(code_of(&tls), "E_FEED_TLS", "{tls:?}");
    let recorded = b.requests();
    let first = &recorded[0];
    assert_eq!(
        (first.method.as_str(), first.path.as_str()),
        ("CONNECT", authority(&c).as_str())
    );
    let host = first
        .headers
        .iter()
        .find(|(k, _)| k.eq_ignore_ascii_case("host"));
    assert_eq!(host.map(|(_, v)| v.as_str()), Some(authority(&c).as_str()));
    for reply in [Reply::ConnectRefused(407), Reply::Close] {
        b.set_route(&authority(&c), reply);
        assert_eq!(code_of(&get(&config, &target, &[], 64)), "E_FEED_OFFLINE");
    }
    assert!(c.requests().is_empty());
    // Credentials reach the proxy as a header and appear in no log line or Debug output.
    b.set_route(&authority(&c), Reply::ConnectRefused(407));
    let secret = ProxyConfig {
        url: format!("http://u:p@127.0.0.1:{}", b.port()),
        no_proxy: Vec::new(),
    };
    let config = HttpConfig::loopback(SEC, SEC).with_proxy(Some(secret));
    let result = get(&config, &target, &[], 64);
    assert_eq!(code_of(&result), "E_FEED_OFFLINE");
    let last = b.requests().pop().expect("the CONNECT");
    let auth = last
        .headers
        .iter()
        .find(|(k, _)| k.eq_ignore_ascii_case("proxy-authorization"));
    assert_eq!(auth.map(|(_, v)| v.as_str()), Some("Basic dTpw"));
    assert!(!format!("{config:?}").contains("u:p"));
    assert!(!format!("{result:?}").contains("u:p"));
    assert!(take_mine().iter().all(|(_, _, m)| !m.contains("u:p")));
}

#[test]
fn proxy_path_http() {
    let c = serve(vec![("/versions.json", ok(b"never"))]);
    let b = tunnel_proxy(&c, ok(b"tunnelled"));
    let config = HttpConfig::loopback(SEC, SEC).with_proxy(Some(proxy_config(&b)));
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Ok(b"tunnelled".to_vec())
    );
    assert_eq!(
        lines(&b),
        [connect_line(&c), "GET /versions.json".to_owned()]
    );
    assert!(c.requests().is_empty());
}

#[test]
fn no_proxy_bypass() {
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let b = tunnel_proxy(&c, ok(b"via b"));
    let bypassed = ProxyConfig {
        no_proxy: vec!["127.0.0.1".to_owned()],
        ..proxy_config(&b)
    };
    let config = HttpConfig::loopback(SEC, SEC).with_proxy(Some(bypassed));
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Ok(b"direct".to_vec())
    );
    assert!(b.requests().is_empty());
    assert_eq!(lines(&c), ["GET /versions.json"]);
    // Per hop (FR-046): the first hop goes through the proxy, the redirect hop to [::1] goes direct.
    let v6 = FixtureServer::start_v6(vec![("/b".to_owned(), ok(b"v6"))]);
    let p1 = serve(vec![("/a", ok(b"never"))]);
    let b2 = serve(vec![
        (&authority(&p1), Reply::ConnectOk),
        (
            "/a",
            Reply::Redirect {
                status: 302,
                location: v6.url("/b"),
            },
        ),
    ]);
    let per_hop = ProxyConfig {
        no_proxy: vec!["::1".to_owned()],
        ..proxy_config(&b2)
    };
    let config = HttpConfig::loopback(SEC, SEC).with_proxy(Some(per_hop));
    assert_eq!(get(&config, &p1.url("/a"), &[], 64), Ok(b"v6".to_vec()));
    assert_eq!(lines(&b2), [connect_line(&p1), "GET /a".to_owned()]);
    assert_eq!(lines(&v6), ["GET /b"]);
    assert!(p1.requests().is_empty());
}

// ---- the OS-evaluated PAC path, every host (SC-041) ---------------------------------------------------------------

#[test]
fn os_pac_walk() {
    install_capture();
    take_mine();
    let p = serve(vec![(
        "/p.pac",
        script("function FindProxyForURL(u, h) { return \"DIRECT\"; }"),
    )]);
    let pac = PacSource {
        auto_detect: true,
        url: Some(p.url("/p.pac?token=s")),
    };
    let config_for = |answers: Vec<OsAnswer>| {
        let fake = answers
            .into_iter()
            .fold(FakePacHost::new().os_mode(), FakePacHost::os_answer);
        let (fake, handle) = fake.handle();
        let config = HttpConfig::loopback(SEC, 2 * SEC)
            .with_pac(Some(pac.clone()))
            .with_pac_host(Some(handle));
        (fake, config)
    };
    let walk = |b1: &FixtureServer, b2: &FixtureServer| {
        OsAnswer::Entries(vec![os_http(b1.port()), os_http(b2.port()), os_direct()])
    };

    // B1 refuses, B2 serves.
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let target = c.url("/versions.json");
    let (b1, b2) = (refusing_proxy(&c), tunnel_proxy(&c, ok(b"via b2")));
    let (fake, config) = config_for(vec![walk(&b1, &b2)]);
    let start = Instant::now();
    assert_eq!(get(&config, &target, &[], 64), Ok(b"via b2".to_vec()));
    assert!(start.elapsed() < 3 * SEC);
    assert_eq!(lines(&b1), [connect_line(&c)]);
    assert_eq!(
        lines(&b2),
        [connect_line(&c), "GET /versions.json".to_owned()]
    );
    assert!(c.requests().is_empty());
    assert_eq!(fake.os_calls().len(), 1);
    // Both refuse: DIRECT reaches C.
    let (b1, b2) = (refusing_proxy(&c), refusing_proxy(&c));
    let (_fake, config) = config_for(vec![walk(&b1, &b2)]);
    assert_eq!(get(&config, &target, &[], 64), Ok(b"direct".to_vec()));
    assert_eq!(
        (lines(&b1), lines(&b2)),
        (vec![connect_line(&c)], vec![connect_line(&c)])
    );
    assert_eq!(lines(&c), ["GET /versions.json"]);
    // All fail: E_FEED_OFFLINE, no entry tried twice.
    let dead = serve(vec![("/versions.json", Reply::Close)]);
    let (b1, b2) = (refusing_proxy(&dead), refusing_proxy(&dead));
    let (_fake, config) = config_for(vec![walk(&b1, &b2)]);
    let start = Instant::now();
    assert_eq!(
        code_of(&get(&config, &dead.url("/versions.json"), &[], 64)),
        "E_FEED_OFFLINE"
    );
    assert!(start.elapsed() < 3 * SEC);
    assert_eq!(
        (lines(&b1), lines(&b2)),
        (vec![connect_line(&dead)], vec![connect_line(&dead)])
    );
    assert!(dead.requests().len() <= 1);

    // The non-advancing rows (FR-067): a TLS failure, a status and the total timeout end the walk.
    let rows: [(&str, Reply, &str); 3] = [
        (
            "https",
            Reply::Ok {
                body: b"plain".to_vec(),
                content_type: "application/json",
            },
            "E_FEED_TLS",
        ),
        ("http", Reply::Status(503), "E_FEED_HTTP_STATUS"),
        ("http", Reply::Stall(30 * SEC), "E_FEED_TIMEOUT"),
    ];
    for (scheme, reply, code) in rows {
        let c = serve(vec![("/versions.json", ok(b"direct"))]);
        let (b1, b2) = (tunnel_proxy(&c, reply), tunnel_proxy(&c, ok(b"via b2")));
        let (_fake, config) = config_for(vec![walk(&b1, &b2)]);
        let target = format!("{scheme}://127.0.0.1:{}/versions.json", c.port());
        let start = Instant::now();
        let result = get(&config, &target, &[], 64);
        assert!(start.elapsed() < 3 * SEC, "{code}: {:?}", start.elapsed());
        assert_eq!(code_of(&result), code, "{result:?}");
        if code == "E_FEED_HTTP_STATUS" {
            assert_eq!(result, Err(HttpError::Status { status: 503 }));
        }
        assert_eq!(lines(&b1)[0], connect_line(&c));
        assert!(
            b2.requests().is_empty() && c.requests().is_empty(),
            "{code}"
        );
    }

    // A working DIRECT answer is final, even with a static proxy configured.
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let b = tunnel_proxy(&c, ok(b"via b"));
    let (_fake, config) = config_for(vec![OsAnswer::Entries(vec![os_direct()])]);
    let config = HttpConfig {
        proxy: Some(proxy_config(&b)),
        ..config
    };
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Ok(b"direct".to_vec())
    );
    assert!(b.requests().is_empty());

    // Once per request hop, with each hop's URL (plain http hops are passed whole).
    let c2 = serve(vec![("/v2.json", ok(b"second hop"))]);
    let c = serve(vec![(
        "/versions.json",
        Reply::Redirect {
            status: 302,
            location: c2.url("/v2.json"),
        },
    )]);
    let (fake, config) = config_for(vec![
        OsAnswer::Entries(vec![os_direct()]),
        OsAnswer::Entries(vec![os_direct()]),
    ]);
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Ok(b"second hop".to_vec())
    );
    let urls: Vec<String> = fake.os_calls().into_iter().map(|(url, _)| url).collect();
    assert_eq!(urls, [c.url("/versions.json"), c2.url("/v2.json")]);

    // An https hop reaches the OS evaluator reduced to scheme://host:port/ (FR-064).
    let c = serve(vec![("/versions.json", ok(b"plain"))]);
    let (fake, config) = config_for(vec![OsAnswer::Entries(vec![os_direct()])]);
    let target = format!("https://127.0.0.1:{}/dir/versions.json?sig=s", c.port());
    assert_eq!(code_of(&get(&config, &target, &[], 64)), "E_FEED_TLS");
    let urls: Vec<String> = fake.os_calls().into_iter().map(|(url, _)| url).collect();
    assert_eq!(urls, [format!("https://127.0.0.1:{}/", c.port())]);

    // TA-23: an OS answer naming a non-loopback proxy under LoopbackOnly is E_FEED_URL before any socket.
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let named = OsProxyEntry {
        kind: OsProxyKind::Http,
        host: "example.invalid".to_owned(),
        port: 80,
    };
    let (_fake, config) = config_for(vec![OsAnswer::Entries(vec![named])]);
    assert_eq!(
        code_of(&get(&config, &c.url("/versions.json"), &[], 64)),
        "E_FEED_URL"
    );
    assert!(c.requests().is_empty());

    assert!(
        p.requests().is_empty(),
        "the manager fetches no PAC on the OS path"
    );
    let captured = take_mine();
    assert!(
        captured.iter().all(|(_, _, m)| !m.contains("token=s")),
        "{captured:?}"
    );

    // Last: all entries failing -> feed::check returns the seeded cache.
    let (_td, roots) = fresh_temp_roots();
    let feed_server = serve(vec![("/versions.json", ok(FEED))]);
    let seed_ctx = context(
        roots,
        feed_server.url("/versions.json"),
        HttpConfig::loopback(SEC, 2 * SEC),
    );
    let seeded = feed::check(&seed_ctx, FetchMode::Normal, 1_000);
    assert_eq!(
        seeded.snapshot.map(|s| s.source),
        Some(SnapshotSource::Fresh)
    );
    feed_server.set_route("/versions.json", Reply::Close);
    let (b1, b2) = (refusing_proxy(&feed_server), refusing_proxy(&feed_server));
    let (_fake, config) = config_for(vec![walk(&b1, &b2)]);
    let result = feed::check(
        &AppContext {
            http: config,
            ..seed_ctx
        },
        FetchMode::Normal,
        2_000,
    );
    assert_eq!(
        result.snapshot.map(|s| s.source),
        Some(SnapshotSource::Cached)
    );
    assert_eq!(
        result.error.as_ref().map(feed::FeedError::code),
        Some("E_FEED_OFFLINE")
    );
}

#[test]
fn os_pac_failure_cases() {
    install_capture();
    take_mine();
    let p = serve(vec![(
        "/p.pac",
        script("function FindProxyForURL(u, h) { return \"DIRECT\"; }"),
    )]);
    let pac = PacSource {
        auto_detect: true,
        url: Some(p.url("/p.pac?token=s")),
    };
    let config_for = |answer: OsAnswer| {
        let (fake, handle) = FakePacHost::new().os_mode().os_answer(answer).handle();
        let config = HttpConfig::loopback(SEC, 2 * SEC)
            .with_pac(Some(pac.clone()))
            .with_pac_host(Some(handle));
        (fake, config)
    };
    let failures = || {
        vec![
            (
                OsAnswer::Fail(PacError {
                    code: PacCode::Fetch,
                    detail: "WinHTTP error 12180".to_owned(),
                }),
                "E_PAC_FETCH",
            ),
            (
                OsAnswer::Fail(PacError {
                    code: PacCode::Script,
                    detail: "WinHTTP error 12166".to_owned(),
                }),
                "E_PAC_SCRIPT",
            ),
            (OsAnswer::Entries(Vec::new()), "E_PAC_RESULT"),
            (
                OsAnswer::Entries(vec![OsProxyEntry {
                    kind: OsProxyKind::Other(3),
                    host: "ftp".to_owned(),
                    port: 21,
                }]),
                "E_PAC_RESULT",
            ),
        ]
    };
    let check_the_warn = |code: &str| {
        let messages: Vec<String> = warns();
        assert!(
            messages.iter().all(|m| !m.contains("token=s")),
            "{messages:?}"
        );
        let pac_lines: Vec<&String> = messages.iter().filter(|m| m.contains("/p.pac")).collect();
        assert_eq!(pac_lines.len(), 1, "{messages:?}");
        assert!(
            pac_lines[0].starts_with(&format!("{code}: ")),
            "{pac_lines:?}"
        );
        assert!(!pac_lines[0].contains('?'), "{pac_lines:?}");
        pac_lines[0].clone()
    };

    for (answer, code) in failures() {
        // With a static proxy: the fallback goes through it.
        let c = serve(vec![("/versions.json", ok(b"direct"))]);
        let b = tunnel_proxy(&c, ok(b"via b"));
        let (_fake, config) = config_for(answer);
        let config = HttpConfig {
            proxy: Some(proxy_config(&b)),
            ..config
        };
        assert_eq!(
            get(&config, &c.url("/versions.json"), &[], 64),
            Ok(b"via b".to_vec()),
            "{code}"
        );
        assert_eq!(
            lines(&b),
            [connect_line(&c), "GET /versions.json".to_owned()]
        );
        assert!(c.requests().is_empty());
        assert!(check_the_warn(code).ends_with("; using the static proxy"));
    }
    for (answer, code) in failures() {
        // Without one: direct.
        let c = serve(vec![("/versions.json", ok(b"direct"))]);
        let (fake, config) = config_for(answer);
        assert_eq!(
            get(&config, &c.url("/versions.json"), &[], 64),
            Ok(b"direct".to_vec()),
            "{code}"
        );
        assert_eq!(lines(&c), ["GET /versions.json"]);
        assert!(check_the_warn(code).ends_with("; using a direct connection"));
        assert_eq!(fake.os_calls().len(), 1);
    }

    // A call that never returns: abandoned after the wait, never killed.
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let (release, blocked) = mpsc::channel::<()>();
    let (fake, config) = config_for(OsAnswer::Block(blocked));
    let start = Instant::now();
    let result = get(&config, &c.url("/versions.json"), &[], 64);
    let elapsed = start.elapsed();
    println!("os_pac_failure_cases blocking row elapsed {elapsed:?}: {result:?}");
    assert!(elapsed < 3 * SEC, "{elapsed:?}");
    let line = check_the_warn("E_PAC_FETCH");
    assert!(
        line.contains("no answer from the OS PAC evaluator"),
        "{line}"
    );
    let calls = fake.os_calls();
    assert_eq!(calls.len(), 1);
    assert!(
        calls[0].1 <= crate::pac::PAC_PHASE_BUDGET && calls[0].1 <= 2 * SEC,
        "{calls:?}"
    );
    assert_eq!(fake.os_finished(), 0);
    drop(release);
    assert!(eventually(2 * SEC, || fake.os_finished() == 1));
    assert!(
        p.requests().is_empty(),
        "the manager fetches no PAC on the OS path"
    );

    // Last: feed::check over each failing config -> Fresh, no error.
    for (answer, code) in failures() {
        let (_td, roots) = fresh_temp_roots();
        let feed_server = serve(vec![("/versions.json", ok(FEED))]);
        let (_fake, config) = config_for(answer);
        let result = feed::check(
            &context(roots, feed_server.url("/versions.json"), config),
            FetchMode::Normal,
            1_000,
        );
        assert_eq!(
            result.snapshot.map(|s| s.source),
            Some(SnapshotSource::Fresh),
            "{code}"
        );
        assert!(result.error.is_none(), "{code}: {:?}", result.error);
    }
}

// ---- the in-process PAC path, macOS and Linux (SC-034 to SC-036) --------------------------------------------------

#[cfg(not(windows))]
fn proxy_script(port: u16) -> String {
    format!("function FindProxyForURL(url, host) {{ return \"PROXY 127.0.0.1:{port}\"; }}")
}

#[cfg(not(windows))]
const DIRECT_SCRIPT: &str = "function FindProxyForURL(url, host) { return \"DIRECT\"; }";

#[cfg(not(windows))]
#[test]
fn pac_path_proxy() {
    let c = serve(vec![("/versions.json", ok(b"never"))]);
    let b = tunnel_proxy(&c, ok(b"tunnelled"));
    let a = serve(vec![("/p.pac", script(&proxy_script(b.port())))]);
    let (_fake, handle) = FakePacHost::new().handle();
    let config = HttpConfig::loopback(SEC, 2 * SEC)
        .with_pac(Some(PacSource {
            auto_detect: false,
            url: Some(a.url("/p.pac")),
        }))
        .with_pac_host(Some(handle));
    let start = Instant::now();
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Ok(b"tunnelled".to_vec())
    );
    assert!(start.elapsed() < 3 * SEC);
    assert_eq!(
        lines(&b),
        [connect_line(&c), "GET /versions.json".to_owned()]
    );
    assert!(c.requests().is_empty());
    assert_eq!(lines(&a), ["GET /p.pac"]);
}

#[cfg(not(windows))]
#[test]
fn pac_path_direct() {
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let b = tunnel_proxy(&c, ok(b"via b"));
    let a = serve(vec![("/p.pac", script(DIRECT_SCRIPT))]);
    let (_fake, handle) = FakePacHost::new().handle();
    let config = HttpConfig::loopback(SEC, 2 * SEC)
        .with_pac(Some(PacSource {
            auto_detect: false,
            url: Some(a.url("/p.pac")),
        }))
        .with_pac_host(Some(handle));
    let config = HttpConfig {
        proxy: Some(proxy_config(&b)),
        ..config
    };
    let start = Instant::now();
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Ok(b"direct".to_vec())
    );
    assert!(start.elapsed() < 3 * SEC);
    assert!(b.requests().is_empty());
    assert_eq!(lines(&c), ["GET /versions.json"]);
}

#[cfg(not(windows))]
#[test]
fn pac_list_walk() {
    let config_for = |a: &FixtureServer| {
        let (_fake, handle) = FakePacHost::new().handle();
        HttpConfig::loopback(SEC, 2 * SEC)
            .with_pac(Some(PacSource {
                auto_detect: false,
                url: Some(a.url("/p.pac")),
            }))
            .with_pac_host(Some(handle))
    };
    let walk_script = |b1: &FixtureServer, b2: &FixtureServer| {
        format!(
            "function FindProxyForURL(url, host) {{ return \"PROXY 127.0.0.1:{}; PROXY 127.0.0.1:{}; DIRECT\"; }}",
            b1.port(),
            b2.port()
        )
    };
    // B1 refuses, B2 serves.
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let (b1, b2) = (refusing_proxy(&c), tunnel_proxy(&c, ok(b"via b2")));
    let a = serve(vec![("/p.pac", script(&walk_script(&b1, &b2)))]);
    let start = Instant::now();
    assert_eq!(
        get(&config_for(&a), &c.url("/versions.json"), &[], 64),
        Ok(b"via b2".to_vec())
    );
    assert!(start.elapsed() < 3 * SEC);
    assert_eq!(lines(&b1), [connect_line(&c)]);
    assert_eq!(
        lines(&b2),
        [connect_line(&c), "GET /versions.json".to_owned()]
    );
    assert!(c.requests().is_empty());
    // Both refuse: DIRECT.
    let (b1, b2) = (refusing_proxy(&c), refusing_proxy(&c));
    let a = serve(vec![("/p.pac", script(&walk_script(&b1, &b2)))]);
    assert_eq!(
        get(&config_for(&a), &c.url("/versions.json"), &[], 64),
        Ok(b"direct".to_vec())
    );
    assert_eq!(
        (lines(&b1), lines(&b2)),
        (vec![connect_line(&c)], vec![connect_line(&c)])
    );
    // All fail.
    let dead = serve(vec![("/versions.json", Reply::Close)]);
    let (b1, b2) = (refusing_proxy(&dead), refusing_proxy(&dead));
    let a = serve(vec![("/p.pac", script(&walk_script(&b1, &b2)))]);
    let start = Instant::now();
    assert_eq!(
        code_of(&get(&config_for(&a), &dead.url("/versions.json"), &[], 64)),
        "E_FEED_OFFLINE"
    );
    assert!(start.elapsed() < 3 * SEC);
    assert_eq!(
        (lines(&b1), lines(&b2)),
        (vec![connect_line(&dead)], vec![connect_line(&dead)])
    );
    // The non-advancing rows (a), (b), (c).
    let rows: [(&str, Reply, &str); 3] = [
        ("https", ok(b"plain"), "E_FEED_TLS"),
        ("http", Reply::Status(503), "E_FEED_HTTP_STATUS"),
        ("http", Reply::Stall(30 * SEC), "E_FEED_TIMEOUT"),
    ];
    for (scheme, reply, code) in rows {
        let c = serve(vec![("/versions.json", ok(b"direct"))]);
        let (b1, b2) = (tunnel_proxy(&c, reply), tunnel_proxy(&c, ok(b"via b2")));
        let a = serve(vec![("/p.pac", script(&walk_script(&b1, &b2)))]);
        let target = format!("{scheme}://127.0.0.1:{}/versions.json", c.port());
        let start = Instant::now();
        let result = get(&config_for(&a), &target, &[], 64);
        let elapsed = start.elapsed();
        println!("pac_list_walk {code} elapsed {elapsed:?}");
        assert!(elapsed < 3 * SEC, "{code}: {elapsed:?}");
        assert_eq!(code_of(&result), code, "{result:?}");
        if code == "E_FEED_HTTP_STATUS" {
            assert_eq!(result, Err(HttpError::Status { status: 503 }));
        }
        assert_eq!(lines(&b1)[0], connect_line(&c));
        assert!(
            b2.requests().is_empty() && c.requests().is_empty(),
            "{code}"
        );
    }
    // Last: feed::check over the all-failing config returns the seeded cache.
    let (_td, roots) = fresh_temp_roots();
    let feed_server = serve(vec![("/versions.json", ok(FEED))]);
    let seed_ctx = context(
        roots,
        feed_server.url("/versions.json"),
        HttpConfig::loopback(SEC, 2 * SEC),
    );
    let seeded = feed::check(&seed_ctx, FetchMode::Normal, 1_000);
    assert_eq!(
        seeded.snapshot.map(|s| s.source),
        Some(SnapshotSource::Fresh)
    );
    feed_server.set_route("/versions.json", Reply::Close);
    let (b1, b2) = (refusing_proxy(&feed_server), refusing_proxy(&feed_server));
    let a = serve(vec![("/p.pac", script(&walk_script(&b1, &b2)))]);
    let result = feed::check(
        &AppContext {
            http: config_for(&a),
            ..seed_ctx
        },
        FetchMode::Normal,
        2_000,
    );
    assert_eq!(
        result.snapshot.map(|s| s.source),
        Some(SnapshotSource::Cached)
    );
    assert_eq!(
        result.error.as_ref().map(feed::FeedError::code),
        Some("E_FEED_OFFLINE")
    );
}

#[cfg(not(windows))]
#[test]
fn pac_per_hop() {
    let c2 = serve(vec![("/v2.json", ok(b"never direct"))]);
    let b = serve(vec![
        (&authority(&c2), Reply::ConnectOk),
        ("/v2.json", ok(b"second hop via b")),
    ]);
    let c = serve(vec![(
        "/versions.json",
        Reply::Redirect {
            status: 302,
            location: c2.url("/v2.json"),
        },
    )]);
    let text = format!(
        "function FindProxyForURL(url, host) {{ if (shExpMatch(url, \"*:{}/*\")) return \"PROXY 127.0.0.1:{}\"; return \"DIRECT\"; }}",
        c2.port(),
        b.port()
    );
    let a = serve(vec![("/p.pac", script(&text))]);
    let (_fake, handle) = FakePacHost::new().handle();
    let config = HttpConfig::loopback(SEC, 2 * SEC)
        .with_pac(Some(PacSource {
            auto_detect: false,
            url: Some(a.url("/p.pac")),
        }))
        .with_pac_host(Some(handle));
    let start = Instant::now();
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Ok(b"second hop via b".to_vec())
    );
    assert!(start.elapsed() < 3 * SEC);
    assert_eq!(lines(&c), ["GET /versions.json"]);
    assert_eq!(lines(&b), [connect_line(&c2), "GET /v2.json".to_owned()]);
    assert!(c2.requests().is_empty());
    assert_eq!(lines(&a), ["GET /p.pac"]);
}

#[cfg(not(windows))]
#[test]
fn pac_fetched_once_never_via_proxy() {
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let b = tunnel_proxy(&c, ok(b"via b"));
    let a = serve(vec![("/p.pac", script(DIRECT_SCRIPT))]);
    let (_fake, handle) = FakePacHost::new().handle();
    let config = HttpConfig::loopback(SEC, 2 * SEC)
        .with_pac(Some(PacSource {
            auto_detect: false,
            url: Some(a.url("/p.pac")),
        }))
        .with_pac_host(Some(handle));
    let config = HttpConfig {
        proxy: Some(proxy_config(&b)),
        ..config
    };
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Ok(b"direct".to_vec())
    );
    assert_eq!(lines(&a), ["GET /p.pac"]);
    c.set_route("/versions.json", Reply::Status(500));
    let start = Instant::now();
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Err(HttpError::Status { status: 500 })
    );
    assert!(start.elapsed() < 3 * SEC);
    assert_eq!(
        lines(&a),
        ["GET /p.pac", "GET /p.pac"],
        "one fetch per call, also when it fails"
    );
    assert!(b.requests().is_empty());
}

#[cfg(not(windows))]
#[test]
fn wpad_discovery_walk() {
    install_capture();
    take_mine();
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let run = |candidates: [String; 2], url: Option<String>| {
        let names: Vec<&str> = candidates.iter().map(String::as_str).collect();
        let (fake, handle) = FakePacHost::new().candidates(&names).handle();
        let config = HttpConfig::loopback(SEC, 2 * SEC)
            .with_pac(Some(PacSource {
                auto_detect: true,
                url,
            }))
            .with_pac_host(Some(handle));
        let start = Instant::now();
        let result = get(&config, &c.url("/versions.json"), &[], 64);
        assert!(start.elapsed() < 3 * SEC);
        let waits = fake.candidate_waits();
        assert_eq!(waits.len(), 1);
        assert!(
            waits.iter().all(|w| *w <= crate::pac::PAC_PHASE_BUDGET),
            "{waits:?}"
        );
        result
    };
    // [404, script]: the first is requested, then the second, whose script is used.
    let b = tunnel_proxy(&c, ok(b"via b"));
    let s = serve(vec![
        ("/w2.dat", script(&proxy_script(b.port()))),
        ("/u.pac", script(DIRECT_SCRIPT)),
    ]);
    assert_eq!(
        run([s.url("/w1.dat"), s.url("/w2.dat")], None),
        Ok(b"via b".to_vec())
    );
    assert_eq!(lines(&s), ["GET /w1.dat", "GET /w2.dat"]);
    assert!(warns().is_empty());
    // [404, 404], no url: E_PAC_FETCH and the fallback (direct).
    let s = serve(vec![("/u.pac", script(DIRECT_SCRIPT))]);
    assert_eq!(
        run([s.url("/w1.dat"), s.url("/w3.dat")], None),
        Ok(b"direct".to_vec())
    );
    let w = warns();
    assert_eq!(w.len(), 1, "{w:?}");
    assert!(
        w[0].starts_with("E_PAC_FETCH: ") && w[0].contains("/w3.dat"),
        "{w:?}"
    );
    // A working candidate wins: the url path is never requested.
    let b = tunnel_proxy(&c, ok(b"via b"));
    let s = serve(vec![
        ("/w2.dat", script(&proxy_script(b.port()))),
        ("/u.pac", script(DIRECT_SCRIPT)),
    ]);
    assert_eq!(
        run([s.url("/w1.dat"), s.url("/w2.dat")], Some(s.url("/u.pac"))),
        Ok(b"via b".to_vec())
    );
    assert_eq!(lines(&s), ["GET /w1.dat", "GET /w2.dat"]);
    // Every candidate fails: the url is requested next (FR-069 (2)).
    let s = serve(vec![("/u.pac", script(DIRECT_SCRIPT))]);
    assert_eq!(
        run([s.url("/w1.dat"), s.url("/w3.dat")], Some(s.url("/u.pac"))),
        Ok(b"direct".to_vec())
    );
    assert_eq!(lines(&s), ["GET /w1.dat", "GET /w3.dat", "GET /u.pac"]);
    assert!(warns().is_empty());
}

#[cfg(not(windows))]
#[test]
fn pac_fallback_to_static_proxy() {
    install_capture();
    take_mine();
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let b = tunnel_proxy(&c, ok(b"via b"));
    let a = serve(vec![("/ok.pac", script(DIRECT_SCRIPT))]);
    let system = SystemProxy {
        proxy: Some(proxy_config(&b)),
        pac_url: Some(a.url("/missing.pac")),
        auto_detect: false,
    };
    let resolution = resolve_proxy(&ProxyEnv::default(), &system, &c.url("/versions.json"));
    assert!(
        resolution.proxy.is_some() && resolution.pac.is_some(),
        "{resolution:?}"
    );
    let (_fake, handle) = FakePacHost::new().handle();
    let config = HttpConfig {
        proxy: resolution.proxy,
        pac: resolution.pac,
        ..HttpConfig::loopback(SEC, 2 * SEC).with_pac_host(Some(handle))
    };
    // A failing PAC with the static proxy: the PAC is tried first, then the proxy.
    let start = Instant::now();
    assert_eq!(
        get(&config, &c.url("/versions.json"), &[], 64),
        Ok(b"via b".to_vec())
    );
    assert!(start.elapsed() < 3 * SEC);
    assert_eq!(lines(&a), ["GET /missing.pac"]);
    assert_eq!(
        lines(&b),
        [connect_line(&c), "GET /versions.json".to_owned()]
    );
    let w = warns();
    assert_eq!(w.len(), 1, "{w:?}");
    assert!(
        w[0].starts_with("E_PAC_FETCH: ") && w[0].ends_with("; using the static proxy"),
        "{w:?}"
    );
    // Without a static proxy: direct.
    let direct = HttpConfig {
        proxy: None,
        ..config.clone()
    };
    assert_eq!(
        get(&direct, &c.url("/versions.json"), &[], 64),
        Ok(b"direct".to_vec())
    );
    assert!(warns()[0].ends_with("; using a direct connection"));
    // A working PAC answering DIRECT: direct, and B records nothing more.
    let working = HttpConfig {
        pac: Some(PacSource {
            auto_detect: false,
            url: Some(a.url("/ok.pac")),
        }),
        ..config
    };
    let before = b.requests().len();
    assert_eq!(
        get(&working, &c.url("/versions.json"), &[], 64),
        Ok(b"direct".to_vec())
    );
    assert_eq!(b.requests().len(), before);
    assert!(warns().is_empty());
}

#[cfg(not(windows))]
#[test]
fn pac_body_cap() {
    install_capture();
    take_mine();
    let head = "function FindProxyForURL(url, host) { return \"DIRECT\"; }\n//";
    let cap = usize::try_from(MAX_PAC_BYTES).expect("1 MiB fits");
    let exact = format!("{head}{}", "x".repeat(cap - head.len()));
    assert_eq!(exact.len(), cap);
    let over = format!("{exact}x");
    let c = serve(vec![("/versions.json", ok(b"direct"))]);
    let b = tunnel_proxy(&c, ok(b"via b"));
    let a = serve(vec![
        ("/exact.pac", script(&exact)),
        ("/over.pac", script(&over)),
    ]);
    let config_for = |path: &str| {
        let (_fake, handle) = FakePacHost::new().handle();
        let config = HttpConfig::loopback(SEC, 2 * SEC)
            .with_pac(Some(PacSource {
                auto_detect: false,
                url: Some(a.url(path)),
            }))
            .with_pac_host(Some(handle));
        HttpConfig {
            proxy: Some(proxy_config(&b)),
            ..config
        }
    };
    let start = Instant::now();
    assert_eq!(
        get(&config_for("/exact.pac"), &c.url("/versions.json"), &[], 64),
        Ok(b"direct".to_vec())
    );
    let elapsed = start.elapsed();
    println!("pac_body_cap exact elapsed {elapsed:?}");
    assert!(elapsed < 3 * SEC);
    assert!(b.requests().is_empty());
    assert!(warns().is_empty());
    let start = Instant::now();
    assert_eq!(
        get(&config_for("/over.pac"), &c.url("/versions.json"), &[], 64),
        Ok(b"via b".to_vec())
    );
    assert!(start.elapsed() < 3 * SEC);
    let w = warns();
    assert_eq!(w.len(), 1, "{w:?}");
    assert!(
        w[0].starts_with("E_PAC_FETCH: ") && w[0].contains("/over.pac"),
        "{w:?}"
    );
}
