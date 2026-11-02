//! Tests of `self_update` (Phase 11 SC-001 to SC-005, SC-023 to SC-025 core rows; plan §7.1). Every socket is a
//! loopback `FixtureServer`; every root is a temp root; every URL naming a public host is data, never requested.

use super::*;
use crate::context::{AppState, LockRelease, StartupError, assemble_context, release_for_restart};
use crate::download::{self, DOWNLOADS_DIR};
use crate::fixture_server::{FixtureServer, Reply};
use crate::http::{PacSource, ProxyConfig};
use crate::install;
use crate::test_support::{FakeElevation, fresh_temp_roots, kat_signing_key};
use base64::Engine as _;
use base64::engine::general_purpose::STANDARD;
use serde_json::json;
use std::sync::RwLock;

const SEC: Duration = Duration::from_secs(1);
/// Never contacted (as `context_tests.rs:17`).
const FEED: &str = "http://127.0.0.1:9/versions.json";
const URL_TEXT: &str = "The Krate Manager update points to an address the manager does not use.";

fn u(s: &str) -> Url {
    Url::parse(s).expect("a test URL parses")
}

fn refusal(reason: UrlRefusal) -> Result<(), SelfUpdateError> {
    Err(SelfUpdateError::Url { reason })
}

/// The loopback config every assembled context carries (as `context_tests.rs:21-23`): never used to connect.
fn http() -> HttpConfig {
    HttpConfig::loopback(SEC, SEC)
}

/// The per-tag package URL of `MANAGER_REPO` for `tag` under `base` (`https://github.com` or a loopback origin).
fn per_tag(base: &str, tag: &str) -> String {
    format!("{base}/{MANAGER_REPO}/releases/download/manager/{tag}/x")
}

// ---- SC-001 -------------------------------------------------------------------------------------------------------

#[test]
fn url_policy() {
    use UrlPolicy::{HttpsOnly, LoopbackOnly};
    use UrlRefusal::{ApiHost, Host, LatestPath, Policy};

    let update_rows: [(UrlPolicy, &str, Result<(), SelfUpdateError>); 12] = [
        (
            HttpsOnly,
            "https://krateaudio.com/manager/latest.json",
            Ok(()),
        ),
        (
            HttpsOnly,
            "https://github.com/o/r/releases/download/manager/v1.2.3/x",
            Ok(()),
        ),
        (HttpsOnly, "http://krateaudio.com/x", refusal(Policy)),
        (
            HttpsOnly,
            "https://api.github.com/repos/o/r/releases/assets/1",
            refusal(ApiHost),
        ),
        (HttpsOnly, "https://API.GITHUB.COM./x", refusal(ApiHost)),
        (
            HttpsOnly,
            "https://github.com/o/r/releases/latest/download/x",
            refusal(LatestPath),
        ),
        (HttpsOnly, "https://u:p@github.com/x", refusal(Policy)),
        (LoopbackOnly, "http://127.0.0.1:9/x", Ok(())),
        (LoopbackOnly, "http://example.com/x", refusal(Policy)),
        (LoopbackOnly, "https://api.github.com/x", refusal(ApiHost)),
        (
            LoopbackOnly,
            "http://127.0.0.1:9/o/r/releases/latest/download/x",
            refusal(LatestPath),
        ),
        (LoopbackOnly, "http://u:p@127.0.0.1:9/x", refusal(Policy)),
    ];
    for (policy, url, expected) in update_rows {
        let got = check_update_url(policy, &u(url));
        assert_eq!(got, expected, "check_update_url({policy:?}, {url})");
        if let Err(e) = got {
            assert_eq!(e.code(), "E_SELF_UPDATE_URL", "{url}");
        }
    }

    let gh = "https://github.com";
    let lo = "http://127.0.0.1:9";
    let good = per_tag(gh, "v0.2.0");
    let package_rows: Vec<(UrlPolicy, String, &str, Result<(), SelfUpdateError>)> = vec![
        (HttpsOnly, good.clone(), "0.2.0", Ok(())),
        (LoopbackOnly, per_tag(lo, "v0.2.0"), "0.2.0", Ok(())),
        (
            HttpsOnly,
            "https://example.org/pkg".into(),
            "0.2.0",
            refusal(Host),
        ),
        (
            HttpsOnly,
            format!("{gh}/{MANAGER_REPO}/archive/x"),
            "0.2.0",
            refusal(Host),
        ),
        (HttpsOnly, format!("{good}?a=1"), "0.2.0", refusal(Host)),
        (HttpsOnly, format!("{good}#f"), "0.2.0", refusal(Host)),
        (HttpsOnly, per_tag(gh, "v0.2"), "0.2.0", refusal(Host)),
        (HttpsOnly, format!("{good}/extra"), "0.2.0", refusal(Host)),
        (
            HttpsOnly,
            format!("{gh}/attacker/x/releases/download/manager/v0.2.0/a"),
            "0.2.0",
            refusal(Host),
        ),
        (
            LoopbackOnly,
            format!("{lo}/attacker/x/releases/download/manager/v0.2.0/a"),
            "0.2.0",
            refusal(Host),
        ),
        (
            HttpsOnly,
            format!("{gh}/o/r/releases/download/manager/v0.2.0/x"),
            "0.2.0",
            refusal(Host),
        ),
        (
            HttpsOnly,
            format!("{gh}/RolandZwaga/krate-audio/releases/download/manager/v0.2.0/x"),
            "0.2.0",
            refusal(Host),
        ),
        (HttpsOnly, per_tag(gh, "v0.1.9"), "0.2.0", refusal(Host)),
        (HttpsOnly, per_tag(gh, "v9.9.9"), "0.2.0", refusal(Host)),
        (HttpsOnly, good.clone(), "0.2.0-rc.1", refusal(Host)),
        (
            LoopbackOnly,
            format!("{lo}/any/path"),
            "0.2.0",
            refusal(Host),
        ),
        // The URL rule runs first: its reasons pass through unchanged.
        (
            HttpsOnly,
            per_tag("http://github.com", "v0.2.0"),
            "0.2.0",
            refusal(Policy),
        ),
        (
            LoopbackOnly,
            per_tag("https://github.com", "v0.2.0"),
            "0.2.0",
            refusal(Policy),
        ),
    ];
    for (policy, url, announced, expected) in package_rows {
        let got = check_update_package_url(policy, &u(&url), announced);
        assert_eq!(
            got, expected,
            "check_update_package_url({policy:?}, {url}, {announced})"
        );
        if let Err(e) = got {
            assert_eq!(e.code(), "E_SELF_UPDATE_URL", "{url}");
        }
    }

    // MANAGER_REPO has the REPO_RE shape of tools/feed-publish.js:66, `^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$`.
    let (owner, repo) = MANAGER_REPO
        .split_once('/')
        .expect("MANAGER_REPO is owner/repo");
    assert!(
        !owner.is_empty() && owner.chars().all(|c| c.is_ascii_alphanumeric() || c == '-'),
        "owner {owner:?}"
    );
    assert!(
        !repo.is_empty()
            && repo
                .chars()
                .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-')),
        "repo {repo:?}"
    );
}

#[test]
fn redirect_hops() {
    use UrlRefusal::{ApiHost, Host, Policy};
    let rules = HopRules {
        policy: UrlPolicy::HttpsOnly,
        endpoint_host: "update.example".into(),
    };
    let gh = u("https://github.com/o/r/releases/download/manager/v0.2.0/x");
    let asset = u(&format!("https://{RELEASE_ASSET_HOST}/blob/1"));
    let endpoint = u("https://update.example/latest.json");

    let hop_rows: [(HopKind, &Url, Url, Result<(), SelfUpdateError>); 10] = [
        (HopKind::Package, &gh, asset.clone(), Ok(())),
        (HopKind::Package, &gh, u("https://github.com/o/r/y"), Ok(())),
        (
            HopKind::Package,
            &asset,
            u("https://github.com/o/r/y"),
            Ok(()),
        ),
        (
            HopKind::Package,
            &gh,
            u("http://github.com/o/r/y"),
            refusal(Policy),
        ),
        (
            HopKind::Endpoint,
            &endpoint,
            u("http://update.example/x"),
            refusal(Policy),
        ),
        (
            HopKind::Package,
            &gh,
            u("https://api.github.com/x"),
            refusal(ApiHost),
        ),
        (
            HopKind::Endpoint,
            &endpoint,
            u("https://api.github.com/x"),
            refusal(ApiHost),
        ),
        (
            HopKind::Package,
            &gh,
            u("https://example.org/x"),
            refusal(Host),
        ),
        (
            HopKind::Package,
            &u("https://example.org/x"),
            asset.clone(),
            refusal(Host),
        ),
        (
            HopKind::Endpoint,
            &endpoint,
            u("https://update.example/other.json"),
            Ok(()),
        ),
    ];
    for (kind, from, to, expected) in hop_rows {
        assert_eq!(
            check_update_hop(&rules, kind, from, &to),
            expected,
            "{kind:?} {from} -> {to}"
        );
    }
    assert_eq!(
        check_update_hop(
            &rules,
            HopKind::Endpoint,
            &endpoint,
            &u("https://other.example/x")
        ),
        refusal(Host)
    );
    assert_eq!(
        check_update_hop(&rules, HopKind::Endpoint, &endpoint, &asset),
        refusal(Host),
        "the endpoint rule admits only the endpoint host"
    );

    // hop_kind.
    assert_eq!(hop_kind(&endpoint, &endpoint), HopKind::Endpoint);
    assert_eq!(hop_kind(&gh, &endpoint), HopKind::Package);

    // redirect_decision: more than MAX_REDIRECTS previous URLs is Policy whatever `to` is.
    let six: Vec<Url> = (0..6)
        .map(|i| u(&format!("https://github.com/o/r/{i}")))
        .collect();
    assert_eq!(
        redirect_decision(&rules, &endpoint, &six, &asset),
        Err(Policy)
    );
    assert_eq!(
        redirect_decision(&rules, &endpoint, &six, &u("https://example.org/x")),
        Err(Policy)
    );
    // Five: the hop rule decides.
    let five = &six[..5];
    assert_eq!(redirect_decision(&rules, &endpoint, five, &asset), Ok(()));
    assert_eq!(
        redirect_decision(&rules, &endpoint, five, &u("https://example.org/x")),
        Err(Host)
    );
    assert_eq!(
        redirect_decision(&rules, &endpoint, five, &u("https://api.github.com/x")),
        Err(ApiHost)
    );
    // A chain starting at the endpoint is judged by the endpoint host rule.
    let from_endpoint = [endpoint.clone(), u("https://update.example/a")];
    assert_eq!(
        redirect_decision(
            &rules,
            &endpoint,
            &from_endpoint,
            &u("https://update.example/b")
        ),
        Ok(())
    );
    assert_eq!(
        redirect_decision(&rules, &endpoint, &from_endpoint, &asset),
        Err(Host)
    );
    // The same endpoint-host hop inside a package chain is refused by the package rule.
    let from_package = [gh.clone()];
    assert_eq!(
        redirect_decision(
            &rules,
            &endpoint,
            &from_package,
            &u("https://update.example/b")
        ),
        Err(Host)
    );

    // LoopbackOnly: any loopback hop is accepted, for both kinds.
    let loopback = HopRules {
        policy: UrlPolicy::LoopbackOnly,
        endpoint_host: "127.0.0.1".into(),
    };
    let lo_endpoint = u("http://127.0.0.1:9/latest.json");
    let lo_from = u("http://127.0.0.1:9/a");
    for kind in [HopKind::Endpoint, HopKind::Package] {
        assert_eq!(
            check_update_hop(&loopback, kind, &lo_from, &u("http://127.0.0.1:10/b")),
            Ok(()),
            "{kind:?}"
        );
        assert_eq!(
            check_update_hop(&loopback, kind, &lo_from, &u("http://[::1]:10/b")),
            Ok(()),
            "{kind:?}"
        );
        assert_eq!(
            check_update_hop(&loopback, kind, &lo_from, &u("http://example.org/b")),
            refusal(Policy),
            "{kind:?}"
        );
    }
    assert_eq!(
        redirect_decision(
            &loopback,
            &lo_endpoint,
            std::slice::from_ref(&lo_endpoint),
            &u("http://127.0.0.1:11/c")
        ),
        Ok(())
    );
}

// ---- SC-002 -------------------------------------------------------------------------------------------------------

fn ok(body: &[u8]) -> Reply {
    Reply::Ok {
        body: body.to_vec(),
        content_type: "application/json",
    }
}

fn serve(routes: Vec<(String, Reply)>) -> FixtureServer {
    FixtureServer::start(routes)
}

/// A proxy that tunnels to `target` and answers the tunnelled `/versions.json` (as `http_tests.rs:77-83`).
fn tunnel_proxy(target: &FixtureServer) -> FixtureServer {
    serve(vec![
        (format!("127.0.0.1:{}", target.port()), Reply::ConnectOk),
        ("/versions.json".to_owned(), ok(b"via proxy")),
    ])
}

/// The PAC server and config the Phase 5 fixtures build. The served script answers `PROXY 127.0.0.1:<proxy>`
/// (`http_tests.rs:1808-1811` `proxy_script`). On macOS / Linux the in-process engine fetches and runs it with a
/// plain `FakePacHost` (`http_tests.rs:1816-1841` `pac_path_proxy`); on Windows the OS evaluates PAC, so a
/// `FakePacHost::os_mode()` answers one `Http` entry for the proxy (`http_tests.rs:1465-1485` `os_pac_walk`), queued
/// twice: once for `proxy_choice`, once for `http::get`.
fn pac_config(
    proxy: &FixtureServer,
) -> (
    FixtureServer,
    std::sync::Arc<crate::pac::tests::FakePacHost>,
    HttpConfig,
) {
    use crate::pac::tests::FakePacHost;
    let pac_server = serve(vec![(
        "/p.pac".to_owned(),
        Reply::Ok {
            body: format!(
                "function FindProxyForURL(url, host) {{ return \"PROXY 127.0.0.1:{}\"; }}",
                proxy.port()
            )
            .into_bytes(),
            content_type: "application/x-ns-proxy-autoconfig",
        },
    )]);
    #[cfg(not(windows))]
    let (fake, handle) = FakePacHost::new().handle();
    #[cfg(not(windows))]
    let source = PacSource {
        auto_detect: false,
        url: Some(pac_server.url("/p.pac")),
    };
    #[cfg(windows)]
    let (fake, handle) = {
        use crate::pac::tests::OsAnswer;
        use crate::pac::{OsProxyEntry, OsProxyKind};
        let entry = || {
            OsAnswer::Entries(vec![OsProxyEntry {
                kind: OsProxyKind::Http,
                host: "127.0.0.1".to_owned(),
                port: proxy.port(),
            }])
        };
        FakePacHost::new()
            .os_mode()
            .os_answer(entry())
            .os_answer(entry())
            .handle()
    };
    #[cfg(windows)]
    let source = PacSource {
        auto_detect: true,
        url: Some(pac_server.url("/p.pac")),
    };
    let config = HttpConfig::loopback(SEC, 2 * SEC)
        .with_pac(Some(source))
        .with_pac_host(Some(handle));
    (pac_server, fake, config)
}

#[test]
fn proxy_choice_matches_core() {
    let target = serve(vec![("/versions.json".to_owned(), ok(b"direct"))]);
    let proxy = tunnel_proxy(&target);
    let target_url = target.url("/versions.json");
    let parsed = u(&target_url);
    let static_proxy = ProxyConfig {
        url: format!("http://127.0.0.1:{}", proxy.port()),
        no_proxy: Vec::new(),
    };
    let base = HttpConfig::loopback(SEC, 2 * SEC);
    let (_pac_server, _fake, pac) = pac_config(&proxy);
    let configs: [(&str, HttpConfig, bool); 4] = [
        (
            "static proxy",
            base.clone().with_proxy(Some(static_proxy.clone())),
            true,
        ),
        (
            "static proxy, target bypassed",
            base.clone().with_proxy(Some(ProxyConfig {
                no_proxy: vec!["127.0.0.1".to_owned()],
                ..static_proxy.clone()
            })),
            false,
        ),
        ("no proxy", base.clone(), false),
        ("PAC", pac, true),
    ];

    let mut matched = 0;
    for (name, config, via_proxy) in configs {
        let before = (target.requests().len(), proxy.requests().len());
        let choice = proxy_choice(&config, &parsed);
        let body = crate::http::get(&config, &target_url, &[], 64);
        let after = (target.requests().len(), proxy.requests().len());
        let (target_grew, proxy_grew) = (after.0 > before.0, after.1 > before.1);
        match &choice {
            Ok(ProxyChoice::Proxy(p)) => {
                assert_eq!(
                    u(&p.url).port(),
                    Some(proxy.port()),
                    "{name}: the named proxy is the proxy server"
                );
                assert!(via_proxy, "{name}: expected Direct, got {choice:?}");
                assert_eq!(body, Ok(b"via proxy".to_vec()), "{name}");
                assert!(
                    proxy_grew && !target_grew,
                    "{name}: {before:?} -> {after:?}"
                );
            }
            Ok(ProxyChoice::Direct) => {
                assert!(!via_proxy, "{name}: expected Proxy, got Direct");
                assert_eq!(body, Ok(b"direct".to_vec()), "{name}");
                assert!(
                    target_grew && !proxy_grew,
                    "{name}: {before:?} -> {after:?}"
                );
            }
            Err(e) => panic!("{name}: proxy_choice refused: {e}"),
        }
        matched += 1;
    }

    // A static proxy the core refuses (scheme ftp): Network, and no request is made by either path.
    let ftp = base.with_proxy(Some(ProxyConfig {
        url: format!("ftp://127.0.0.1:{}", proxy.port()),
        no_proxy: Vec::new(),
    }));
    let before = (target.requests().len(), proxy.requests().len());
    let choice = proxy_choice(&ftp, &parsed);
    assert!(
        matches!(choice, Err(SelfUpdateError::Network { .. })),
        "ftp: {choice:?}"
    );
    if let Err(e) = &choice {
        assert_eq!(e.code(), "E_SELF_UPDATE_NETWORK");
    }
    assert!(crate::http::get(&ftp, &target_url, &[], 64).is_err());
    assert_eq!(
        (target.requests().len(), proxy.requests().len()),
        before,
        "ftp: both servers record zero requests"
    );
    matched += 1;
    assert_eq!(matched, 5, "threshold 5 / 5");
}

// ---- SC-003 -------------------------------------------------------------------------------------------------------

fn one_of_each() -> [SelfUpdateError; 8] {
    [
        SelfUpdateError::Network { detail: "n".into() },
        SelfUpdateError::Manifest { detail: "m".into() },
        SelfUpdateError::Signature { detail: "s".into() },
        SelfUpdateError::Url {
            reason: UrlRefusal::Host,
        },
        SelfUpdateError::Busy,
        SelfUpdateError::None,
        SelfUpdateError::Install { detail: "i".into() },
        SelfUpdateError::Unsupported,
    ]
}

#[test]
fn codes_and_sentences() {
    let distinct: BTreeSet<&str> = CODES.iter().copied().collect();
    assert_eq!(distinct.len(), 8);
    assert!(CODES.iter().all(|c| c.starts_with("E_SELF_UPDATE_")));

    // plan §3.1, the sentence table (exact).
    let sentences: [(&str, &str); 8] = [
        (
            "E_SELF_UPDATE_NETWORK",
            "Could not reach the Krate Manager update server.",
        ),
        (
            "E_SELF_UPDATE_MANIFEST",
            "The Krate Manager update information is incomplete or has no entry for this computer.",
        ),
        (
            "E_SELF_UPDATE_SIGNATURE",
            "The Krate Manager update failed its signature check and was not installed.",
        ),
        ("E_SELF_UPDATE_URL", URL_TEXT),
        (
            "E_SELF_UPDATE_BUSY",
            "Finish the plugin installs and removals first, then update Krate Manager.",
        ),
        (
            "E_SELF_UPDATE_NONE",
            "There is no Krate Manager update to install. Check for updates first.",
        ),
        (
            "E_SELF_UPDATE_INSTALL",
            "Krate Manager could not install its update. It keeps running the current version.",
        ),
        (
            "E_SELF_UPDATE_UNSUPPORTED",
            "Krate Manager cannot update itself on this computer. Install the new version from its release page.",
        ),
    ];
    let mut reached = BTreeSet::new();
    for e in one_of_each() {
        let code = e.code();
        assert!(CODES.contains(&code), "{code}");
        reached.insert(code);
        let expected = sentences
            .iter()
            .find(|(c, _)| *c == code)
            .map(|(_, s)| *s)
            .expect("every code has a sentence row");
        assert_eq!(e.sentence(), expected, "{code}");
        assert_eq!(e.to_string(), format!("{code}: {expected}"));
        let view = e.view();
        assert_eq!((view.code, view.sentence), (code, expected));
    }
    assert_eq!(reached, distinct, "all 8 codes reached");

    assert_eq!(
        SelfUpdating.to_string(),
        "E_INSTALL_SELF_UPDATING: Krate Manager is updating itself. Try again after it restarts."
    );
    assert_eq!(SELF_UPDATING_CODE, "E_INSTALL_SELF_UPDATING");

    let url = SelfUpdateError::Url {
        reason: UrlRefusal::LatestPath,
    };
    assert_eq!(
        serde_json::to_value(url.view()).expect("serialize"),
        json!({"code": "E_SELF_UPDATE_URL", "sentence": URL_TEXT, "reason": "latest_path"})
    );
    let busy = serde_json::to_value(SelfUpdateError::Busy.view()).expect("serialize");
    let keys: BTreeSet<&str> = busy
        .as_object()
        .expect("an object")
        .keys()
        .map(String::as_str)
        .collect();
    assert_eq!(
        keys,
        BTreeSet::from(["code", "sentence"]),
        "Busy has no reason or detail key"
    );
    assert_eq!(
        serde_json::to_value(SelfUpdateError::Install { detail: "d".into() }.view())
            .expect("serialize")["detail"],
        json!("d")
    );

    let r = redacted("https://u:p@h/x");
    assert!(!r.contains("u:p"), "{r}");
    assert!(r.contains("h/x"), "{r}");
    let r = redacted("error sending request for url (https://u:p@h/x) via http://a:b@p:1/");
    assert!(!r.contains("u:p") && !r.contains("a:b"), "{r}");
    assert_eq!(redacted("no address here"), "no address here");
}

/// Whether a `<` occurs anywhere in `v` other than inside the value of a `text` key (an object key included).
fn lt_outside_text(v: &serde_json::Value) -> bool {
    match v {
        serde_json::Value::String(s) => s.contains('<'),
        serde_json::Value::Array(items) => items.iter().any(lt_outside_text),
        serde_json::Value::Object(map) => map
            .iter()
            .any(|(k, item)| k.contains('<') || (k != "text" && lt_outside_text(item))),
        _ => false,
    }
}

#[test]
fn view_from_update() {
    let body = "# New\n\n<b>bold</b> text\n- a";
    let page = format!("https://github.com/{MANAGER_REPO}/releases/tag/manager/v0.2.0");
    for (os, name) in [
        (Os::Windows, OsName::Windows),
        (Os::Macos, OsName::Macos),
        (Os::Linux, OsName::Linux),
    ] {
        let facts = UpdateFacts {
            version: "0.2.0",
            date_rfc3339: Some("2026-11-01T00:00:00Z".into()),
            body: Some(body),
            action: UpdateAction::InstallAndRestart,
        };
        let v = view(os, "0.1.0", Some(facts), None);
        assert_eq!(v.os, name);
        assert_eq!(v.current, "0.1.0");
        assert_eq!(v.error, None);
        let available = v.available.as_ref().expect("an available update");
        assert_eq!(available.version, "0.2.0");
        assert_eq!(available.date.as_deref(), Some("2026-11-01T00:00:00Z"));
        assert_eq!(available.notes, notes::blocks(body));
        assert_eq!(available.action, UpdateAction::InstallAndRestart);
        // notes::blocks keeps inline HTML as literal text (notes_tests.rs:169-171, "inline html is literal text"),
        // which the UI sets as text, never as markup: so a `<` may occur only inside a `text` value.
        let notes_json = serde_json::to_value(&available.notes).expect("serialize");
        assert!(!lt_outside_text(&notes_json), "{notes_json}");
        assert!(
            notes_json
                .to_string()
                .contains(r#""text":"<b>bold</b> text""#),
            "{notes_json}"
        );
        let whole = serde_json::to_value(&v).expect("serialize");
        assert_eq!(
            whole["available"]["action"],
            json!({"kind": "install_and_restart"})
        );

        let facts = UpdateFacts {
            version: "0.2.0",
            date_rfc3339: None,
            body: None,
            action: UpdateAction::OpenReleasePage { url: page.clone() },
        };
        let v = view(os, "0.1.0", Some(facts), None);
        let available = v.available.as_ref().expect("an available update");
        assert_eq!(available.date, None);
        assert_eq!(
            serde_json::to_value(&v).expect("serialize")["available"]["action"],
            json!({"kind": "open_release_page", "url": page})
        );

        let none = view(os, "0.1.0", None, None);
        assert_eq!(none.available, None);
        assert_eq!(none.error, None);
        assert_eq!(none.os, name);

        let e = SelfUpdateError::Network { detail: "d".into() };
        let failed = view(os, "0.1.0", None, Some(e.clone()));
        assert_eq!(failed.available, None);
        assert_eq!(failed.error, Some(e.view()));
    }
}

/// One `update_action` row: packaging, policy, download URL, announced version, expected result.
type ActionRow<'a> = (
    Packaging,
    UrlPolicy,
    String,
    &'a str,
    Result<UpdateAction, SelfUpdateError>,
);

#[test]
fn packaging_rule() {
    use Packaging::{SelfContained, SystemPackage};
    use UrlPolicy::{HttpsOnly, LoopbackOnly};

    let unsupported = self_install_supported(SystemPackage);
    assert_eq!(unsupported, Err(SelfUpdateError::Unsupported));
    if let Err(e) = unsupported {
        assert_eq!(e.code(), "E_SELF_UPDATE_UNSUPPORTED");
        assert_eq!(
            e.sentence(),
            "Krate Manager cannot update itself on this computer. Install the new version from its release page."
        );
    }
    assert_eq!(self_install_supported(SelfContained), Ok(()));

    let gh = "https://github.com";
    let lo = "http://127.0.0.1:9";
    let page = format!("https://github.com/{MANAGER_REPO}/releases/tag/manager/v0.2.0");
    let host = || {
        Err(SelfUpdateError::Url {
            reason: UrlRefusal::Host,
        })
    };
    let rows: Vec<ActionRow<'_>> = vec![
        (
            SelfContained,
            HttpsOnly,
            per_tag(gh, "v0.2.0"),
            "0.2.0",
            Ok(UpdateAction::InstallAndRestart),
        ),
        (
            SelfContained,
            HttpsOnly,
            format!("{gh}/attacker/x/releases/download/manager/v0.2.0/a"),
            "0.2.0",
            host(),
        ),
        (
            SelfContained,
            HttpsOnly,
            per_tag(gh, "v0.1.9"),
            "0.2.0",
            host(),
        ),
        (
            SystemPackage,
            HttpsOnly,
            per_tag(gh, "v0.2.0"),
            "0.2.0",
            Ok(UpdateAction::OpenReleasePage { url: page.clone() }),
        ),
        (
            SystemPackage,
            LoopbackOnly,
            per_tag(lo, "v0.2.0"),
            "0.2.0",
            Ok(UpdateAction::OpenReleasePage { url: page.clone() }),
        ),
        (
            SystemPackage,
            HttpsOnly,
            format!("{gh}/attacker/x/releases/download/manager/v9.9.9/a"),
            "9.9.9",
            host(),
        ),
        (
            SystemPackage,
            HttpsOnly,
            per_tag(gh, "v0.1.9"),
            "0.2.0",
            host(),
        ),
        (
            SystemPackage,
            HttpsOnly,
            "https://example.org/pkg".into(),
            "0.2.0",
            host(),
        ),
        (
            SystemPackage,
            HttpsOnly,
            format!("{gh}/{MANAGER_REPO}/archive/x"),
            "0.2.0",
            host(),
        ),
        (
            SystemPackage,
            LoopbackOnly,
            format!("{lo}/x"),
            "0.2.0",
            host(),
        ),
    ];
    for (packaging, policy, url, announced, expected) in rows {
        let got = update_action(packaging, policy, &u(&url), announced);
        assert_eq!(got, expected, "{packaging:?} {policy:?} {url} {announced}");
        if let Ok(UpdateAction::OpenReleasePage { url }) = &got {
            assert_eq!(crate::status::release_page_url(url), Some(url.clone()));
        }
    }
}

#[test]
fn progress_events_serialize() {
    assert_eq!(
        serde_json::to_value(SelfUpdateProgress::Downloading).expect("serialize"),
        json!({"event": "downloading"})
    );
    assert_eq!(
        serde_json::to_value(SelfUpdateProgress::Installing).expect("serialize"),
        json!({"event": "installing"})
    );
}

// ---- SC-004 -------------------------------------------------------------------------------------------------------

/// The key file's bytes carry no carriage return (a CRLF checkout would change the compiled key text).
fn crlf_free(bytes: &[u8]) -> Result<(), usize> {
    match bytes.iter().filter(|b| **b == b'\r').count() {
        0 => Ok(()),
        n => Err(n),
    }
}

#[test]
fn endpoint_matches_feed_tool() {
    let manifest = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let conf_text = std::fs::read_to_string(manifest.join("../../src-tauri/tauri.conf.json"))
        .expect("read tauri.conf.json");
    let conf: serde_json::Value = serde_json::from_str(&conf_text).expect("tauri.conf.json parses");
    let tool = std::fs::read_to_string(manifest.join("../../../../tools/feed-publish.js"))
        .expect("read tools/feed-publish.js");

    let needle = "const MANAGER_LATEST_URL = '";
    let start = tool.find(needle).expect("MANAGER_LATEST_URL is declared") + needle.len();
    let len = tool[start..].find('\'').expect("the literal closes");
    let latest = &tool[start..start + len];
    assert!(latest.starts_with("https://"), "{latest}");
    assert_eq!(conf["plugins"]["updater"]["endpoints"], json!([latest]));

    let pubkey = conf["plugins"]["updater"]["pubkey"]
        .as_str()
        .expect("pubkey is a string");
    let decoded = STANDARD.decode(pubkey).expect("pubkey is base64");
    assert_eq!(
        String::from_utf8(decoded).expect("the key text is UTF-8"),
        crate::verify::RELEASE_KEY_TEXT
    );

    let key =
        std::fs::read(manifest.join("../../../../tools/feed-contract/keys/krate-signing.pub"))
            .expect("read the key file");
    assert_eq!(crlf_free(&key), Ok(()));
    let crlf = crate::verify::RELEASE_KEY_TEXT.replace('\n', "\r\n");
    assert!(
        crlf_free(crlf.as_bytes()).is_err(),
        "the check reports a CRLF copy"
    );
}

// ---- SC-005 -------------------------------------------------------------------------------------------------------

#[test]
fn busy_rule() {
    let (td, roots) = fresh_temp_roots();
    // As context_tests.rs:132 assembles it.
    let state = assemble_context(Ok(roots.clone()), FEED, http(), kat_signing_key());
    let ctx = state.context.as_ref().ok();
    assert!(ctx.is_some(), "an assembled temp context");

    // None at all.
    let fake = FakeElevation::refusing();
    assert!(!self_update_busy(&fake, ctx));
    assert!(!self_update_busy(&fake, None), "no context, a free guard");

    // A running batch holds the guard: busy with and without a context.
    {
        let fake = FakeElevation::refusing();
        let held = fake.guard().hold_for_test();
        assert!(self_update_busy(&fake, ctx));
        assert!(self_update_busy(&fake, None));
        drop(held);
        assert!(!self_update_busy(&fake, ctx));
        assert!(!self_update_busy(&fake, None));
    }

    // A timed-out batch's stale hold.
    {
        let fake = FakeElevation::refusing();
        let held = fake
            .guard()
            .hold_stale_for_test(td.path().join("job.json"), td.path().join("result.json"));
        assert!(self_update_busy(&fake, ctx));
        assert!(self_update_busy(&fake, None));
        drop(held);
        assert!(!self_update_busy(&fake, ctx));
    }

    // A plugin action of this state dir.
    {
        let fake = FakeElevation::refusing();
        let held = install::hold_busy_for_test(roots.state_dir(), "alpha");
        assert!(self_update_busy(&fake, ctx));
        assert!(
            !self_update_busy(&fake, None),
            "no context: only the guard counts"
        );
        drop(held);
        assert!(!self_update_busy(&fake, ctx));
    }

    // A plugin download of this staging dir.
    {
        let fake = FakeElevation::refusing();
        let held = download::hold_in_flight_for_test(&roots.staging_dir().join(DOWNLOADS_DIR));
        assert!(self_update_busy(&fake, ctx));
        assert!(!self_update_busy(&fake, None));
        drop(held);
        assert!(!self_update_busy(&fake, ctx));
    }

    // Another state dir's action and download never make this one busy.
    {
        let (_other_td, other) = fresh_temp_roots();
        let fake = FakeElevation::refusing();
        let _busy = install::hold_busy_for_test(other.state_dir(), "alpha");
        let _download = download::hold_in_flight_for_test(&other.staging_dir().join(DOWNLOADS_DIR));
        assert!(!self_update_busy(&fake, ctx));
    }
}

// ---- SC-023 (core) ------------------------------------------------------------------------------------------------

#[test]
fn in_flight_flag() {
    let (_td, roots) = fresh_temp_roots();
    let (_td2, second) = fresh_temp_roots();
    let (_td3, third) = fresh_temp_roots();
    let dir = roots.state_dir();
    assert!(!in_flight(dir), "false at start");
    assert_eq!(action_gate(dir), Ok(()));

    let guard = InFlightGuard::enter(Some(dir));
    assert!(in_flight(dir));
    assert_eq!(action_gate(dir), Err(SelfUpdating));
    assert!(
        !in_flight(second.state_dir()),
        "another state dir is unaffected"
    );
    assert_eq!(action_gate(second.state_dir()), Ok(()));

    let none = InFlightGuard::enter(None);
    assert!(!in_flight(third.state_dir()), "enter(None) affects no dir");
    assert!(!in_flight(second.state_dir()));
    drop(none);

    drop(guard);
    assert!(!in_flight(dir), "dropping clears");
    assert_eq!(action_gate(dir), Ok(()));
}

// ---- SC-024 -------------------------------------------------------------------------------------------------------

#[test]
fn timeouts() {
    assert_eq!(CHECK_TIMEOUT, Duration::from_secs(30));
    assert_eq!(DOWNLOAD_TIMEOUT, Duration::from_secs(3600));
    assert_eq!(STALL_BOUND, Duration::from_secs(30));
    assert_eq!(
        UpdateTimeouts::production(),
        UpdateTimeouts {
            check: CHECK_TIMEOUT,
            download: DOWNLOAD_TIMEOUT,
            stall: STALL_BOUND,
        }
    );
}

// ---- SC-025 -------------------------------------------------------------------------------------------------------

#[test]
fn no_context_defaults() {
    // Never contacted: only its port names the fallback's proxy.
    let proxy = serve(Vec::new());
    // As src-tauri/src/lib.rs:64-67 builds the no-context fallback.
    let fallback = HttpConfig::production()
        .with_proxy(Some(ProxyConfig {
            url: format!("http://127.0.0.1:{}", proxy.port()),
            no_proxy: Vec::new(),
        }))
        .with_pac(None)
        .with_pac_host(None);
    assert_eq!(self_update_http(None, &fallback), fallback);
    assert_ne!(self_update_http(None, &fallback), HttpConfig::production());

    let (_td, roots) = fresh_temp_roots();
    let state = assemble_context(Ok(roots), FEED, http(), kat_signing_key());
    let ctx = state.context.as_ref().expect("an assembled temp context");
    assert_eq!(self_update_http(Some(ctx), &fallback), ctx.http);
    assert_ne!(ctx.http, fallback);

    assert!(!self_update_busy(&FakeElevation::refusing(), None));

    let no_context: crate::context::SharedState = RwLock::new(AppState {
        context: Err(StartupError::Locked),
        settings_error: None,
        lock: None,
    });
    assert_eq!(release_for_restart(&no_context), LockRelease::NoLock);
    assert!(proxy.requests().is_empty());
}
