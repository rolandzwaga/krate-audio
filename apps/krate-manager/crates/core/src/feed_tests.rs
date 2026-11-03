//! feed tests (plan §4.3, §7.1 rows SC-002 to SC-005, SC-009 to SC-011, SC-013, SC-014, SC-022, SC-024, SC-030 to
//! SC-032, SC-036, SC-037). Fixtures are read in place from `tools/feed-contract/fixtures/`; every check test starts its
//! own loopback `FixtureServer` and its own `fresh_temp_roots()` state dir, and asserts its log lines through the shared
//! capture logger (`install_capture()` / `take_mine()`).

use super::*;
use crate::contract::{self, DuplicateSides, Schema};
use crate::fixture_server::{FixtureServer, Reply};
use crate::http::{HttpConfig, HttpError, ProxyConfig};
use crate::platform::Os;
use crate::roots::Roots;
use crate::settings::Settings;
use crate::test_support::{fresh_temp_roots, install_capture, kat_signing_key, take_mine};
use crate::version::{self, Arch, Availability};
use base64::Engine as _;
use log::Level;
use serde_json::{Value, json};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Barrier;
use std::thread;
use std::time::{Duration, Instant, SystemTime};

const SEC: Duration = Duration::from_secs(1);
const FEED_PATH: &str = "/versions.json";
const NOW: u64 = 1_700_000_000;
/// The four `(Os, Arch)` pairs of FR-034.
const PAIRS: [(Os, Arch); 4] = [
    (Os::Windows, Arch::X86_64),
    (Os::Macos, Arch::Aarch64),
    (Os::Macos, Arch::X86_64),
    (Os::Linux, Arch::X86_64),
];
const STEPS: &[CacheStep] = &[
    CacheStep::CreateDir,
    CacheStep::CreateTemp,
    CacheStep::Write,
    CacheStep::SyncTemp,
    CacheStep::Persist,
    #[cfg(unix)]
    CacheStep::SyncDir,
];
const FULL: &str = "valid/full-v2.json";
const MIXED: &str = "valid/mixed.json";
/// The fixture asset URL prefix of `perTagUrl` (fixtures README "Synthetic values").
const DOWNLOAD: &str = "https://github.com/rolandzwaga/krate-audio/releases/download";

// ---- fixtures and small helpers -----------------------------------------------------------------------------------

fn fixtures() -> PathBuf {
    // crates/core -> crates -> krate-manager -> apps -> the repo root
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../../tools/feed-contract/fixtures")
}

/// A file under `fixtures/feeds/`, as bytes.
fn read(rel: &str) -> Vec<u8> {
    let path = fixtures().join("feeds").join(rel);
    fs::read(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()))
}

fn text(bytes: &[u8]) -> &str {
    std::str::from_utf8(bytes).expect("a fixture is UTF-8")
}

fn doc(rel: &str) -> Value {
    serde_json::from_slice(&read(rel)).unwrap_or_else(|e| panic!("{rel} is not JSON: {e}"))
}

fn bytes_of(doc: &Value) -> Vec<u8> {
    serde_json::to_vec_pretty(doc).expect("a Value serialises")
}

fn parsed(bytes: &[u8]) -> Feed {
    parse(bytes).unwrap_or_else(|e| panic!("parse: {e}"))
}

fn full() -> Feed {
    parsed(&read(FULL))
}

fn doc_keys(doc: &Value) -> Vec<String> {
    doc["plugins"]
        .as_object()
        .expect("plugins is an object")
        .keys()
        .cloned()
        .collect()
}

fn keys(feed: &Feed) -> Vec<String> {
    feed.plugins.keys().cloned().collect()
}

fn release<'a>(feed: &'a Feed, name: &str) -> &'a StableRelease {
    feed.plugins
        .get(name)
        .and_then(|e| e.release.as_ref())
        .unwrap_or_else(|| panic!("{name} has no stable release in the model"))
}

/// The modified document is contract-valid: no findings.
fn assert_valid(d: &Value, schema: Schema, what: &str) {
    let findings = contract::validate(d, schema);
    assert!(findings.is_empty(), "{what}: {findings:?}");
}

fn assert_not_through_manager(entry: &PluginEntry, what: &str) {
    for (os, arch) in PAIRS {
        assert_eq!(
            version::resolve(entry, os, arch, None),
            Availability::NotThroughManager,
            "{what}: {} on {os:?} {arch:?}",
            entry.name
        );
    }
}

fn hex(bytes: &[u8; 32]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// The test's own decoder (the model's is under test).
fn hex32(s: &str) -> [u8; 32] {
    let mut out = [0u8; 32];
    assert_eq!(s.len(), 64, "{s}");
    for (i, slot) in out.iter_mut().enumerate() {
        *slot = u8::from_str_radix(&s[2 * i..2 * i + 2], 16).expect("hex");
    }
    out
}

/// Every model value equals the document's, so a builder defect (a refused decode, name, URL or format, a dropped
/// asset or install) fails the caller (SC-002 client half).
fn assert_mirrors(feed: &Feed, document: &Value, what: &str) {
    let v2 = feed.schema_version == 2;
    for (name, entry) in document["plugins"].as_object().expect("plugins") {
        let model = &feed.plugins[name];
        assert_eq!(model.name, *name, "{what}");
        assert_eq!(model.version_text, entry["version"], "{what} {name}");
        assert_eq!(
            model.download_url.as_ref().map(PageUrl::as_str),
            entry["download_url"].as_str(),
            "{what} {name}"
        );
        if !v2 {
            assert!(model.release.is_none(), "{what} {name}");
            continue;
        }
        assert!(model.version.is_some(), "{what} {name}");
        let rel = model.release.as_ref().expect("a v2 entry has a release");
        assert_eq!(rel.tag, entry["tag"], "{what} {name}");
        assert_eq!(rel.pub_date, entry["pub_date"], "{what} {name}");
        assert_eq!(rel.release_notes_url, entry["release_notes_url"], "{what}");
        assert_eq!(
            rel.notes_markdown.as_deref(),
            entry["notes_markdown"].as_str(),
            "{what}"
        );
        let mut doc_platforms: Vec<(PlatformKey, &Value)> = entry
            .get("platforms")
            .and_then(Value::as_object)
            .map(|p| {
                p.iter()
                    .filter_map(|(k, v)| PlatformKey::parse(k).map(|key| (key, v)))
                    .collect()
            })
            .unwrap_or_default();
        doc_platforms.sort_by_key(|(k, _)| *k);
        assert_eq!(
            rel.platforms.keys().copied().collect::<Vec<_>>(),
            doc_platforms.iter().map(|(k, _)| *k).collect::<Vec<_>>(),
            "{what} {name}"
        );
        for (key, p) in doc_platforms {
            let asset = &rel.platforms[&key];
            assert_eq!(asset.url.as_str(), p["url"], "{what} {name}");
            assert!(
                p["url"]
                    .as_str()
                    .is_some_and(|u| u.ends_with(&format!("/{}", asset.asset_name.as_str()))),
                "{what} {name}"
            );
            assert_eq!(Some(asset.size), p["size"].as_u64(), "{what} {name}");
            assert_eq!(hex(&asset.sha256), p["sha256"], "{what} {name}");
            assert_eq!(asset.signature, p["signature"], "{what} {name}");
            let format = match asset.format {
                AssetFormat::InnoExe => "inno-exe",
                AssetFormat::Pkg => "pkg",
                AssetFormat::TarGz => "tar.gz",
            };
            assert_eq!(format, p["format"], "{what} {name}");
            assert_eq!(
                asset.min_os.map(|v| v.to_string()).as_deref(),
                p["min_os"].as_str(),
                "{what} {name}"
            );
            assert_eq!(
                asset.payload.map(|pl| (pl.files, hex(&pl.sha256))),
                p.get("payload").map(|pl| (
                    pl["files"].as_u64().expect("files"),
                    pl["sha256"].as_str().expect("sha256").to_owned()
                )),
                "{what} {name}"
            );
        }
        match (&rel.install, entry.get("install")) {
            (None, None) => {}
            (Some(i), Some(d)) => {
                let fields = [
                    ("vst3_bundle", Some(&i.vst3_bundle)),
                    ("au_component", i.au_component.as_ref()),
                    ("preset_subdir", i.preset_subdir.as_ref()),
                    ("windows_uninstall_key", i.windows_uninstall_key.as_ref()),
                    ("macos_pkg_id", i.macos_pkg_id.as_ref()),
                ];
                for (field, value) in fields {
                    assert_eq!(
                        value.map(PlainName::as_str),
                        d[field].as_str(),
                        "{what} {name} {field}"
                    );
                }
            }
            (model, document) => panic!("{what} {name}: install {model:?} vs {document:?}"),
        }
    }
    let manager = feed.manager.as_ref().map(|m| m.latest_json_url.as_str());
    let in_doc = if v2 {
        document["manager"]["latest_json_url"].as_str()
    } else {
        None
    };
    assert_eq!(manager, in_doc, "{what}");
}

fn ok(body: &[u8]) -> Reply {
    Reply::Ok {
        body: body.to_vec(),
        content_type: "application/json",
    }
}

fn serve(reply: Reply) -> FixtureServer {
    FixtureServer::start(vec![(FEED_PATH.to_owned(), reply)])
}

/// A context whose cache lives in the given temp state dir (Rule 1), loopback 1 s / 1 s.
fn context(roots: &Roots, feed_url: String) -> AppContext {
    AppContext {
        roots: roots.clone(),
        settings: Settings::default(),
        feed_url,
        http: HttpConfig::loopback(SEC, SEC),
        signing_key: kat_signing_key(),
    }
}

fn cache_path(roots: &Roots) -> PathBuf {
    roots.state_dir().join(FEED_CACHE_FILE)
}

/// The file's bytes and modification time.
fn stamp(path: &Path) -> (Vec<u8>, SystemTime) {
    let bytes = fs::read(path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()));
    let mtime = fs::metadata(path)
        .and_then(|m| m.modified())
        .expect("modified time");
    (bytes, mtime)
}

/// The D8 text, written out (FR-052): four keys in order, two-space indent, final LF.
fn cache_text(fetched_at_unix: u64, source_url: &str, body: &[u8]) -> String {
    let body = serde_json::to_string(text(body)).expect("a string serialises");
    format!(
        "{{\n  \"cache_version\": 1,\n  \"fetched_at_unix\": {fetched_at_unix},\n  \"source_url\": \"{source_url}\",\n  \"body\": {body}\n}}\n"
    )
}

/// The calling thread's captured `warn!` messages; every captured record is removed.
fn warns() -> Vec<String> {
    take_mine()
        .into_iter()
        .filter(|(_, level, _)| *level == Level::Warn)
        .map(|(_, _, message)| message)
        .collect()
}

fn code_of(error: Option<&FeedError>) -> Option<&'static str> {
    error.map(FeedError::code)
}

fn source_of(c: &FeedCheck) -> Option<SnapshotSource> {
    c.snapshot.as_ref().map(|s| s.source)
}

fn feed_of(c: &FeedCheck) -> &Feed {
    &c.snapshot.as_ref().expect("a snapshot").feed
}

fn feed_requests(s: &FixtureServer) -> usize {
    s.requests().iter().filter(|r| r.path == FEED_PATH).count()
}

fn header<'a>(r: &'a crate::fixture_server::RecordedRequest, name: &str) -> Option<&'a str> {
    r.headers
        .iter()
        .find(|(k, _)| k.eq_ignore_ascii_case(name))
        .map(|(_, v)| v.as_str())
}

fn stray_temps(dir: &Path) -> usize {
    fs::read_dir(dir)
        .expect("read_dir")
        .filter(|e| {
            e.as_ref()
                .expect("dir entry")
                .file_name()
                .to_string_lossy()
                .starts_with(".feed-cache.json.")
        })
        .count()
}

fn names(dir: &Path) -> Vec<String> {
    let mut out: Vec<String> = fs::read_dir(dir)
        .expect("read_dir")
        .map(|e| {
            e.expect("dir entry")
                .file_name()
                .to_string_lossy()
                .into_owned()
        })
        .collect();
    out.sort();
    out
}

/// A seeding `Normal` check that must come back fresh.
fn seed(ctx: &AppContext, now: u64) {
    let seeded = check(ctx, FetchMode::Normal, now);
    assert_eq!(
        source_of(&seeded),
        Some(SnapshotSource::Fresh),
        "{:?}",
        seeded.error
    );
    assert!(seeded.cache_error.is_none(), "{:?}", seeded.cache_error);
}

// ---- parse and the model ------------------------------------------------------------------------------------------

#[test]
fn accept_rows_parse_through_the_client() {
    install_capture();
    take_mine();
    let cases = doc("cases.json");
    let rows = cases.as_array().expect("cases.json is an array");
    let mut accepted = 0usize;
    for row in rows {
        let expect = row["expect"].as_str();
        if !matches!(expect, Some("accept" | "accept-without-platforms")) {
            continue;
        }
        accepted += 1;
        let file = row["file"].as_str().expect("row.file");
        let document = doc(file);
        let feed = parse(&read(file)).unwrap_or_else(|e| panic!("{file}: {e}"));
        assert_eq!(keys(&feed), doc_keys(&document), "{file}");
        assert_eq!(
            Some(u64::from(feed.schema_version)),
            document["schema_version"].as_u64(),
            "{file}"
        );
        assert_mirrors(&feed, &document, file);
        let logged = take_mine();
        assert!(logged.is_empty(), "{file}: {logged:?}");
        if expect == Some("accept-without-platforms") {
            let name = row["plugin"].as_str().expect("row.plugin");
            assert_not_through_manager(&feed.plugins[name], file);
        }
    }
    println!("accept_rows_parse_through_the_client: {accepted} accept rows");
    assert!(accepted >= 1, "cases.json has no accept row");
}

#[test]
fn gen_feed_outputs_parse() {
    let root = fixtures().join("gen-feed");
    let mut dirs: Vec<PathBuf> = fs::read_dir(&root)
        .unwrap_or_else(|e| panic!("read {}: {e}", root.display()))
        .map(|e| e.expect("dir entry").path())
        .filter(|p| p.is_dir())
        .collect();
    dirs.sort();
    let (mut v2, mut legacy) = (0usize, 0usize);
    for dir in &dirs {
        for (name, schema_version, count) in [
            ("expected.v2.json", 2u8, &mut v2),
            ("expected.legacy.json", 1u8, &mut legacy),
        ] {
            let path = dir.join(name);
            if !path.is_file() {
                continue;
            }
            *count += 1;
            let bytes = fs::read(&path).expect("read");
            let what = path.display().to_string();
            let feed = parse(&bytes).unwrap_or_else(|e| panic!("{what}: {e}"));
            let document: Value = serde_json::from_slice(&bytes).expect("JSON");
            assert_eq!(feed.schema_version, schema_version, "{what}");
            assert_eq!(keys(&feed), doc_keys(&document), "{what}");
            assert_mirrors(&feed, &document, &what);
        }
    }
    println!("gen_feed_outputs_parse: {v2} expected.v2.json, {legacy} expected.legacy.json");
    assert!(v2 >= 1 && legacy >= 1, "{v2} / {legacy}");
}

#[test]
fn schema_version_dispatch() {
    let base = doc(FULL);
    let with = |sv: Option<Value>| {
        let mut d = base.clone();
        let obj = d.as_object_mut().expect("an object");
        match sv {
            Some(v) => obj.insert("schema_version".to_owned(), v),
            None => obj.remove("schema_version"),
        };
        parse(&bytes_of(&d))
    };
    for sv in [None, Some(json!(1))] {
        let feed = with(sv.clone()).unwrap_or_else(|e| panic!("{sv:?}: {e}"));
        assert_eq!(feed.schema_version, 1, "{sv:?}");
        assert_eq!(keys(&feed), doc_keys(&base), "{sv:?}");
        assert!(feed.manager.is_none(), "{sv:?}");
        assert!(feed.plugins.values().all(|e| e.release.is_none()), "{sv:?}");
    }
    for sv in [json!(2), json!(2.0)] {
        let feed = with(Some(sv.clone())).unwrap_or_else(|e| panic!("{sv}: {e}"));
        assert_eq!(feed, full(), "{sv}");
    }
    match with(Some(json!(3))) {
        Err(FeedError::FutureSchema { found: 3 }) => {}
        other => panic!("3: {other:?}"),
    }
    for sv in [json!(0), json!(-1), json!(2.5), json!("2"), Value::Null] {
        let got = with(Some(sv.clone()));
        assert!(
            matches!(got, Err(FeedError::SchemaVersion)),
            "{sv}: {got:?}"
        );
        assert_eq!(
            got.err()
                .map(|e| e.to_string().starts_with("E_FEED_SCHEMA_VERSION")),
            Some(true)
        );
    }
    // The const row says 1: the client reads it as v1 (D3).
    let feed = parsed(&read("invalid/schema_version-const.json"));
    assert_eq!(feed.plugins.len(), 3);
    for entry in feed.plugins.values() {
        assert_not_through_manager(entry, "schema_version-const");
    }
    let full_bytes = read(FULL);
    let html = b"<html><head><title>Sign in</title></head><body>portal</body></html>".to_vec();
    let inputs: [(&str, Vec<u8>); 4] = [
        ("malformed-json", read("invalid/malformed-json.json")),
        (
            "BOM",
            [b"\xEF\xBB\xBF".as_slice(), full_bytes.as_slice()].concat(),
        ),
        ("0xFF", [full_bytes.as_slice(), b"\xFF"].concat()),
        ("HTML", html),
    ];
    for (what, bytes) in inputs {
        let got = parse(&bytes);
        assert!(
            matches!(got, Err(FeedError::Input { .. })),
            "{what}: {got:?}"
        );
        assert_eq!(got.err().map(|e| e.code()), Some("E_INPUT"), "{what}");
    }
}

/// `fixtureSignature(formatTrustedComment(name, version, key, asset))` (fixtures README "Synthetic values").
fn fixture_signature(name: &str, version: &str, key: &str, asset: &str) -> String {
    let b64 = base64::engine::general_purpose::STANDARD;
    let mut blob = vec![b'E', b'D'];
    blob.extend([0u8; 72]);
    format!(
        "untrusted comment: krate fixture signature (not cryptographic)\n{}\ntrusted comment: krate:1:{name}:{version}:{key}:{asset}\n{}\n",
        b64.encode(&blob),
        b64.encode([0u8; 64])
    )
}

/// The expected full-v2 asset from the README facts; the two hashes come from the document.
fn expected_asset(document: &Value, name: &str, version: &str, key: PlatformKey) -> PlatformAsset {
    let tag = format!("{}/v{version}", name.to_lowercase());
    let (suffix, format, min_os) = match key {
        PlatformKey::WindowsX86_64 => (
            "Windows-x64.exe",
            AssetFormat::InnoExe,
            (name == "Charlie").then_some("10.0"),
        ),
        PlatformKey::LinuxX86_64 => ("Linux-x64.tar.gz", AssetFormat::TarGz, Some("2.31")),
        _ => ("macOS.pkg", AssetFormat::Pkg, Some("11.0")),
    };
    let asset = format!("{name}-{version}-{suffix}");
    let p = &document["plugins"][name]["platforms"][key.as_str()];
    PlatformAsset {
        url: AssetUrl(format!("{DOWNLOAD}/{tag}/{asset}")),
        asset_name: AssetName(asset.clone()),
        size: format!("krate fixture asset {asset}\n").len() as u64,
        sha256: hex32(p["sha256"].as_str().expect("sha256")),
        signature: fixture_signature(name, version, key.as_str(), &asset),
        format,
        min_os: min_os.map(|s| OsVersion::parse(s).expect("min_os")),
        payload: (key == PlatformKey::LinuxX86_64).then(|| Payload {
            files: 3,
            sha256: hex32(p["payload"]["sha256"].as_str().expect("payload sha256")),
        }),
    }
}

#[test]
fn model_of_full_v2() {
    let document = doc(FULL);
    let plain = |s: &str| PlainName(s.to_owned());
    let entry = |name: &str, version: &str, keys: [PlatformKey; 3], n: u8| {
        let lower = name.to_lowercase();
        let tag = format!("{lower}/v{version}");
        PluginEntry {
            name: name.to_owned(),
            version_text: version.to_owned(),
            version: CoreVersion::parse(version),
            download_url: Some(PageUrl(format!("https://krateaudio.com/{lower}/"))),
            description: None,
            release: Some(StableRelease {
                pub_date: "2026-10-29T00:00:00Z".to_owned(),
                release_notes_url: format!(
                    "https://github.com/rolandzwaga/krate-audio/releases/tag/{tag}"
                ),
                tag,
                notes_markdown: (name == "Alpha")
                    .then(|| "### Fixed\n\n- A fixture note.".to_owned()),
                platforms: keys
                    .into_iter()
                    .map(|k| (k, expected_asset(&document, name, version, k)))
                    .collect(),
                install: Some(InstallFacts {
                    vst3_bundle: plain(&format!("{name}.vst3")),
                    au_component: Some(plain(&format!("{name}.component"))),
                    preset_subdir: (name == "Bravo").then(|| plain("Kits")),
                    windows_uninstall_key: Some(plain(&format!(
                        "{{0A1F0000-0000-4000-8000-00000000000{n}}}_is1"
                    ))),
                    macos_pkg_id: Some(plain(&format!("com.krateaudio.{lower}"))),
                }),
            }),
        }
    };
    let expected = Feed {
        schema_version: 2,
        plugins: [
            entry(
                "Alpha",
                "1.2.0",
                [
                    PlatformKey::WindowsX86_64,
                    PlatformKey::DarwinAarch64,
                    PlatformKey::LinuxX86_64,
                ],
                1,
            ),
            entry(
                "Bravo",
                "2.0.1",
                [
                    PlatformKey::WindowsX86_64,
                    PlatformKey::DarwinX86_64,
                    PlatformKey::LinuxX86_64,
                ],
                2,
            ),
            entry(
                "Charlie",
                "0.9.0",
                [
                    PlatformKey::WindowsX86_64,
                    PlatformKey::DarwinUniversal,
                    PlatformKey::LinuxX86_64,
                ],
                3,
            ),
        ]
        .into_iter()
        .map(|e| (e.name.clone(), e))
        .collect(),
        manager: Some(ManagerInfo {
            latest_json_url: "https://rolandzwaga.github.io/krate-audio/manager/latest.json"
                .to_owned(),
        }),
    };
    let feed = full();
    assert_eq!(keys(&feed), ["Alpha", "Bravo", "Charlie"]);
    assert_eq!(feed, expected);
    let alpha_windows = &release(&feed, "Alpha").platforms[&PlatformKey::WindowsX86_64];
    assert_eq!(
        alpha_windows.asset_name.as_str(),
        "Alpha-1.2.0-Windows-x64.exe"
    );
}

#[test]
fn unknown_keys_do_not_change_the_model() {
    assert_eq!(parsed(&read("valid/unknown-extra-keys.json")), full());
}

#[test]
fn prerelease_sibling_is_ignored() {
    let base = full();
    for file in [
        "valid/prerelease-plain.json",
        "valid/prerelease-suffixed.json",
    ] {
        assert!(
            doc(file)["plugins"]["Alpha"]["prerelease"].is_object(),
            "{file}"
        );
        let feed = parsed(&read(file));
        let alpha = &feed.plugins["Alpha"];
        assert_eq!(alpha.version_text, "1.2.0", "{file}");
        assert_eq!(release(&feed, "Alpha").tag, "alpha/v1.2.0", "{file}");
        assert_eq!(alpha, &base.plugins["Alpha"], "{file}");
        assert_eq!(feed, base, "{file}");
    }
}

#[test]
fn asset_name_cases() {
    install_capture();
    take_mine();
    let base = full();
    let prefix = format!("{DOWNLOAD}/alpha/v1.2.0");
    let rows: [(&str, Option<&str>); 7] = [
        ("/a/", None),
        ("/x.exe:ads", None),
        ("/CON.exe", None),
        ("/%2e%2e", None),
        ("/a.exe?x=/..", Some("a.exe")),
        ("/a/..", None),
        (
            "/Alpha-1.0.0-Windows-x64.exe",
            Some("Alpha-1.0.0-Windows-x64.exe"),
        ),
    ];
    for (suffix, kept) in rows {
        let url = format!("{prefix}{suffix}");
        let mut d = doc(FULL);
        d["plugins"]["Alpha"]["platforms"]["windows-x86_64"]["url"] = json!(url);
        assert_valid(&d, Schema::V2, suffix);
        take_mine();
        let feed = parsed(&bytes_of(&d));
        let w = warns();
        let platforms = &release(&feed, "Alpha").platforms;
        match kept {
            Some(name) => {
                let asset = &platforms[&PlatformKey::WindowsX86_64];
                assert_eq!(asset.asset_name.as_str(), name, "{suffix}");
                assert_eq!(asset.url.as_str(), url, "{suffix}");
                assert!(w.is_empty(), "{suffix}: {w:?}");
            }
            None => {
                assert!(
                    !platforms.contains_key(&PlatformKey::WindowsX86_64),
                    "{suffix}"
                );
                assert_eq!(
                    w,
                    ["E_MODEL_ASSET_NAME /plugins/Alpha/platforms/windows-x86_64/url"],
                    "{suffix}"
                );
                let mut want = base.clone();
                want.plugins
                    .get_mut("Alpha")
                    .and_then(|e| e.release.as_mut())
                    .expect("Alpha's release")
                    .platforms
                    .remove(&PlatformKey::WindowsX86_64);
                assert_eq!(feed, want, "{suffix}");
            }
        }
    }
}

#[test]
fn v1_entries_are_sanitized() {
    install_capture();
    take_mine();
    let document = json!({
        "schema_version": 1,
        "plugins": {
            "Alpha": { "version": "1.0.0", "download_url": "file:///etc/passwd", "release_notes": "" },
            "Bravo": { "version": "1.0.1", "download_url": "javascript:x" },
            "Charlie": { "version": "1.1.0", "download_url": "https://example.invalid/charlie/" },
            "Delta": { "version": "1.2.0" },
            "../X": { "version": "1.0.0" }
        }
    });
    assert_valid(&document, Schema::V1, "v1 sanitising");
    let feed = parsed(&bytes_of(&document));
    let mut w = warns();
    w.sort();
    assert_eq!(
        w,
        [
            "E_MODEL_V1_ENTRY /plugins/..~1X",
            "E_MODEL_V1_ENTRY /plugins/Alpha/download_url",
            "E_MODEL_V1_ENTRY /plugins/Bravo/download_url",
        ]
    );
    assert_eq!(keys(&feed), ["Charlie", "Delta"]);
    let charlie = &feed.plugins["Charlie"];
    assert_eq!(
        charlie.download_url.as_ref().map(PageUrl::as_str),
        Some("https://example.invalid/charlie/")
    );
    assert_eq!(feed.plugins["Delta"].download_url, None);
    let kept_only = json!({
        "schema_version": 1,
        "plugins": {
            "Charlie": document["plugins"]["Charlie"].clone(),
            "Delta": document["plugins"]["Delta"].clone()
        }
    });
    assert_eq!(feed, parsed(&bytes_of(&kept_only)));
    assert!(warns().is_empty());
}

#[test]
fn model_range_cases() {
    install_capture();
    take_mine();
    let base = full();
    let without = |plugin: &str, keys: &[PlatformKey]| {
        let mut want = base.clone();
        let platforms = &mut want
            .plugins
            .get_mut(plugin)
            .and_then(|e| e.release.as_mut())
            .expect("a release")
            .platforms;
        for key in keys {
            platforms.remove(key);
        }
        want
    };
    // Row 1: payload.files 1e300.
    let mut row1 = doc(FULL);
    row1["plugins"]["Alpha"]["platforms"]["linux-x86_64"]["payload"]["files"] = json!(1e300);
    // Row 2: size 2.0.
    let mut row2 = doc(FULL);
    row2["plugins"]["Alpha"]["platforms"]["windows-x86_64"]["size"] = json!(2.0);
    // Row 3: Charlie gains a darwin-aarch64 copy of its darwin-universal asset whose min_os overflows u32.
    let mut row3 = doc(FULL);
    let mut copied = row3["plugins"]["Charlie"]["platforms"]["darwin-universal"].clone();
    copied["min_os"] = json!("11.4294967296");
    row3["plugins"]["Charlie"]["platforms"]["darwin-aarch64"] = copied;
    // Row 4: every Alpha asset dropped.
    let mut row4 = doc(FULL);
    let alpha = &mut row4["plugins"]["Alpha"]["platforms"];
    alpha["darwin-aarch64"]["min_os"] = json!("11.4294967296");
    alpha["linux-x86_64"]["payload"]["files"] = json!(1e300);
    alpha["windows-x86_64"]["url"] = json!(format!("{DOWNLOAD}/alpha/v1.2.0/CON.exe"));
    // Row 5: an asset name that is an NTFS stream.
    let mut row5 = doc(FULL);
    row5["plugins"]["Bravo"]["platforms"]["windows-x86_64"]["url"] =
        json!(format!("{DOWNLOAD}/bravo/v2.0.1/x.exe:ads"));
    // Row 6: a v1 entry whose download_url fails the C1 url rule.
    let mut row6 = doc("valid/v1-only.json");
    row6["plugins"]["Bravo"]["download_url"] = json!("javascript:x");

    for (n, d, schema) in [
        (1, &row1, Schema::V2),
        (2, &row2, Schema::V2),
        (3, &row3, Schema::V2),
        (4, &row4, Schema::V2),
        (5, &row5, Schema::V2),
        (6, &row6, Schema::V1),
    ] {
        assert_valid(d, schema, &format!("row {n}"));
    }

    let parse_row = |d: &Value| {
        take_mine();
        let feed = parsed(&bytes_of(d));
        (feed, warns())
    };
    let (feed, w) = parse_row(&row1);
    assert_eq!(feed, without("Alpha", &[PlatformKey::LinuxX86_64]));
    assert_eq!(
        w,
        ["E_MODEL_RANGE /plugins/Alpha/platforms/linux-x86_64/payload/files"]
    );

    let (feed, w) = parse_row(&row2);
    assert_eq!(
        release(&feed, "Alpha").platforms[&PlatformKey::WindowsX86_64].size,
        2
    );
    assert!(w.is_empty(), "{w:?}");

    let (feed, w) = parse_row(&row3);
    assert_eq!(feed, base);
    assert_eq!(
        w,
        ["E_MODEL_RANGE /plugins/Charlie/platforms/darwin-aarch64/min_os"]
    );
    let charlie = &feed.plugins["Charlie"];
    assert!(
        matches!(
            version::resolve(charlie, Os::Macos, Arch::Aarch64, None),
            Availability::Available {
                key: PlatformKey::DarwinUniversal,
                ..
            }
        ),
        "{:?}",
        version::resolve(charlie, Os::Macos, Arch::Aarch64, None)
    );

    let (feed, mut w) = parse_row(&row4);
    assert_eq!(keys(&feed), ["Alpha", "Bravo", "Charlie"]);
    assert!(release(&feed, "Alpha").platforms.is_empty());
    assert_eq!(
        feed,
        without(
            "Alpha",
            &[
                PlatformKey::WindowsX86_64,
                PlatformKey::DarwinAarch64,
                PlatformKey::LinuxX86_64
            ]
        )
    );
    w.sort();
    assert_eq!(
        w,
        [
            "E_MODEL_ASSET_NAME /plugins/Alpha/platforms/windows-x86_64/url",
            "E_MODEL_RANGE /plugins/Alpha/platforms/darwin-aarch64/min_os",
            "E_MODEL_RANGE /plugins/Alpha/platforms/linux-x86_64/payload/files",
        ]
    );
    assert_not_through_manager(&feed.plugins["Alpha"], "row 4");

    let (feed, w) = parse_row(&row5);
    assert_eq!(feed, without("Bravo", &[PlatformKey::WindowsX86_64]));
    assert_eq!(
        w,
        ["E_MODEL_ASSET_NAME /plugins/Bravo/platforms/windows-x86_64/url"]
    );

    let (feed, w) = parse_row(&row6);
    assert_eq!(keys(&feed), ["Alpha", "Charlie"]);
    let v1 = parsed(&read("valid/v1-only.json"));
    assert_eq!(feed.plugins["Alpha"], v1.plugins["Alpha"]);
    assert_eq!(feed.plugins["Charlie"], v1.plugins["Charlie"]);
    assert_eq!(w, ["E_MODEL_V1_ENTRY /plugins/Bravo/download_url"]);

    // Row 7: check over a fixture server for rows 1, 3 and 4.
    for (n, d) in [(1, &row1), (3, &row3), (4, &row4)] {
        let body = bytes_of(d);
        let server = serve(ok(&body));
        let (_td, roots) = fresh_temp_roots();
        let ctx = context(&roots, server.url(FEED_PATH));
        let result = check(&ctx, FetchMode::Normal, NOW);
        assert_eq!(source_of(&result), Some(SnapshotSource::Fresh), "row {n}");
        assert!(result.error.is_none(), "row {n}: {:?}", result.error);
        let cache: Value = serde_json::from_slice(&fs::read(cache_path(&roots)).expect("cache"))
            .expect("the cache is JSON");
        assert_eq!(cache["body"].as_str(), Some(text(&body)), "row {n}");
    }
}

#[test]
fn roster_follows_the_feed() {
    for (file, count) in [
        ("valid/tenth-plugin.json", 10usize),
        ("valid/one-v2-eight-v1.json", 9),
        ("valid/v1-only.json", 3),
    ] {
        let feed = parsed(&read(file));
        assert_eq!(feed.plugins.len(), count, "{file}");
        assert_eq!(keys(&feed), doc_keys(&doc(file)), "{file}");
    }
    let mut d = doc("valid/one-v2-eight-v1.json");
    let first = doc_keys(&d)[0].clone();
    d["plugins"]
        .as_object_mut()
        .expect("plugins")
        .remove(&first);
    assert_valid(&d, Schema::V2, "eight");
    let feed = parsed(&bytes_of(&d));
    assert_eq!(feed.plugins.len(), 8);
    assert_eq!(keys(&feed), doc_keys(&d));
    assert!(!feed.plugins.contains_key(&first));
}

fn finding(path: &str) -> Finding {
    Finding {
        code: "E_SCHEMA",
        instance_path: path.to_owned(),
        keyword: Some("pattern"),
        message: String::new(),
        duplicate: None,
    }
}

fn duplicate(first: &str, second: &str) -> Finding {
    Finding {
        code: "E_INSTALL_DUPLICATE",
        instance_path: second.to_owned(),
        keyword: None,
        message: String::new(),
        duplicate: Some(DuplicateSides {
            field: "install.vst3_bundle",
            first: first.to_owned(),
            second: second.to_owned(),
        }),
    }
}

#[test]
fn sibling_prefix_filter() {
    let dropped = [
        finding("/plugins/Bravo/prerelease"),
        finding("/plugins/Bravo/prerelease/tag"),
        finding("/plugins/Bravo/prerelease/platforms/windows-x86_64/url"),
        finding("/plugins/Alpha/prerelease/tag"),
        // PD-9, both key orders: the sibling's plugin sorts before, then after, the stable one's.
        duplicate(
            "/plugins/Alpha/prerelease/install/vst3_bundle",
            "/plugins/Bravo/install/vst3_bundle",
        ),
        duplicate(
            "/plugins/Bravo/install/vst3_bundle",
            "/plugins/Charlie/prerelease/install/vst3_bundle",
        ),
    ];
    let kept = [
        finding("/plugins/Bravo"),
        finding("/plugins/Bravo/version"),
        finding("/plugins/Bravo/prereleaseX/tag"),
        finding("/prerelease"),
        finding("/plugins/prerelease/tag"),
        finding(""),
        duplicate(
            "/plugins/Alpha/install/vst3_bundle",
            "/plugins/Bravo/install/vst3_bundle",
        ),
    ];
    let mixed: Vec<Finding> = kept
        .iter()
        .cloned()
        .zip(dropped.iter().cloned())
        .flat_map(|(k, d)| [d, k])
        .chain(kept[dropped.len()..].iter().cloned())
        .collect();
    assert_eq!(mixed.len(), kept.len() + dropped.len());
    let (k, d) = drop_sibling_findings(mixed);
    assert_eq!(k, kept);
    assert_eq!(d, dropped);
}

/// `"{code} {instance_path} {keyword}"`, as `parse` logs a dropped finding.
fn dropped_line(f: &Finding) -> String {
    match f.keyword {
        Some(k) => format!("{} {} {k}", f.code, f.instance_path),
        None => format!("{} {}", f.code, f.instance_path),
    }
}

#[test]
fn broken_sibling_never_blocks_stable() {
    install_capture();
    take_mine();
    let base = full();
    let cases = doc("cases.json");
    let rows: Vec<(String, String)> = cases
        .as_array()
        .expect("rows")
        .iter()
        .filter_map(|r| {
            let file = r["file"].as_str()?;
            file.starts_with("invalid/sibling-").then(|| {
                (
                    file.to_owned(),
                    r["expect"]["error"]
                        .as_str()
                        .expect("an error row")
                        .to_owned(),
                )
            })
        })
        .collect();
    assert_eq!(rows.len(), 7, "{rows:?}");
    let server = serve(ok(&read(FULL)));
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    for (file, pair) in &rows {
        let bytes = read(file);
        let findings = contract::validate(&doc(file), Schema::V2);
        assert!(!findings.is_empty(), "{file}");
        take_mine();
        let feed = parse(&bytes).unwrap_or_else(|e| panic!("{file}: {e}"));
        assert_eq!(feed.plugins["Bravo"], base.plugins["Bravo"], "{file}");
        let mut w = warns();
        w.sort();
        let mut want: Vec<String> = findings.iter().map(dropped_line).collect();
        want.sort();
        assert_eq!(w, want, "{file}");
        assert!(
            w.iter().any(|l| l.ends_with(pair.as_str())),
            "{file}: {pair} in {w:?}"
        );

        seed(&ctx, NOW);
        server.set_route(FEED_PATH, ok(&bytes));
        let result = check(&ctx, FetchMode::Normal, NOW + 1);
        assert_eq!(source_of(&result), Some(SnapshotSource::Fresh), "{file}");
        assert!(result.error.is_none(), "{file}: {:?}", result.error);
        assert_eq!(feed_of(&result).plugins["Bravo"], base.plugins["Bravo"]);
        assert_eq!(
            fs::read_to_string(cache_path(&roots)).expect("cache"),
            cache_text(NOW + 1, &ctx.feed_url, &bytes),
            "{file}"
        );
        server.set_route(FEED_PATH, ok(&read(FULL)));
    }

    // A sibling finding and a stable finding: still rejected, with only the stable one.
    let plain = doc("invalid/install-dotdot.json");
    let mut both = plain.clone();
    both["plugins"]["Bravo"]["prerelease"] =
        doc("invalid/sibling-version-forbidden.json")["plugins"]["Bravo"]["prerelease"].clone();
    match parse(&bytes_of(&both)) {
        Err(FeedError::Invalid { findings }) => {
            assert_eq!(findings, contract::validate(&plain, Schema::V2));
            assert!(!findings.is_empty());
            assert!(
                findings
                    .iter()
                    .all(|f| !f.instance_path.contains("/prerelease")),
                "{findings:?}"
            );
        }
        other => panic!("install-dotdot with a broken sibling: {other:?}"),
    }

    // PD-9 over real documents: a sibling install clashing with a stable one, both key orders.
    let sibling = doc("valid/prerelease-plain.json")["plugins"]["Alpha"]["prerelease"].clone();
    let mut before = doc(FULL);
    let mut s = sibling.clone();
    s["install"] = before["plugins"]["Alpha"]["install"].clone();
    s["install"]["vst3_bundle"] = json!("Bravo.vst3");
    before["plugins"]["Alpha"]["prerelease"] = s;
    let mut after = doc(FULL);
    let mut s = sibling;
    s["install"] = after["plugins"]["Charlie"]["install"].clone();
    s["install"]["vst3_bundle"] = json!("Bravo.vst3");
    after["plugins"]["Charlie"]["prerelease"] = s;
    for (what, d, second) in [
        (
            "sibling first",
            &before,
            "/plugins/Bravo/install/vst3_bundle",
        ),
        (
            "sibling second",
            &after,
            "/plugins/Charlie/prerelease/install/vst3_bundle",
        ),
    ] {
        let findings = contract::validate(d, Schema::V2);
        assert_eq!(findings.len(), 1, "{what}: {findings:?}");
        assert_eq!(findings[0].code, "E_INSTALL_DUPLICATE", "{what}");
        assert_eq!(findings[0].instance_path, second, "{what}");
        take_mine();
        let feed = parse(&bytes_of(d)).unwrap_or_else(|e| panic!("{what}: {e}"));
        assert_eq!(feed, base, "{what}");
        assert_eq!(warns(), [dropped_line(&findings[0])], "{what}");
    }
}

// ---- check and the cache ------------------------------------------------------------------------------------------

#[test]
fn check_fresh_writes_cache() {
    let body = read(FULL);
    let server = serve(ok(&body));
    let (_td, roots) = fresh_temp_roots();
    let url = server.url(FEED_PATH);
    let result = check(&context(&roots, url.clone()), FetchMode::Normal, NOW);
    assert!(result.error.is_none(), "{:?}", result.error);
    assert!(result.cache_error.is_none(), "{:?}", result.cache_error);
    let snapshot = result.snapshot.expect("a snapshot");
    assert_eq!(snapshot.source, SnapshotSource::Fresh);
    assert_eq!(snapshot.fetched_at_unix, NOW);
    assert_eq!(snapshot.feed, full());
    let written = fs::read_to_string(cache_path(&roots)).expect("the cache file");
    assert_eq!(
        written,
        format!(
            "{{\n  \"cache_version\": 1,\n  \"fetched_at_unix\": 1700000000,\n  \"source_url\": \"{url}\",\n  \"body\": {}\n}}\n",
            serde_json::to_string(text(&body)).expect("a string serialises")
        )
    );
    let requests = server.requests();
    assert_eq!(requests.len(), 1, "{requests:?}");
    let r = &requests[0];
    assert_eq!((r.method.as_str(), r.path.as_str()), ("GET", FEED_PATH));
    let agent = format!("krate-manager/{}", env!("CARGO_PKG_VERSION"));
    assert_eq!(header(r, "user-agent"), Some(agent.as_str()));
    assert_eq!(header(r, "accept"), Some("application/json"));
    assert_eq!(header(r, "cache-control"), None);
    assert_eq!(header(r, "pragma"), None);
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../version.json");
    let version: Value =
        serde_json::from_slice(&fs::read(&manifest).expect("version.json")).expect("JSON");
    assert_eq!(version["version"].as_str(), Some(env!("CARGO_PKG_VERSION")));
}

#[test]
fn check_now_sends_no_cache() {
    assert_eq!(EDGE_MAX_AGE_SECS, 600);
    let server = serve(ok(&read(FULL)));
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    for mode in [FetchMode::Normal, FetchMode::CheckNow] {
        let result = check(&ctx, mode, NOW);
        assert_eq!(source_of(&result), Some(SnapshotSource::Fresh), "{mode:?}");
    }
    let requests = server.requests();
    assert_eq!(requests.len(), 2, "{requests:?}");
    assert!(
        requests
            .iter()
            .all(|r| r.method == "GET" && r.path == FEED_PATH)
    );
    assert_eq!(header(&requests[0], "cache-control"), None);
    assert_eq!(header(&requests[0], "pragma"), None);
    assert_eq!(header(&requests[1], "cache-control"), Some("no-cache"));
    assert_eq!(header(&requests[1], "pragma"), Some("no-cache"));
}

#[test]
fn rejected_feed_keeps_last_good_copy() {
    let base = full();
    let server = serve(ok(&read(FULL)));
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    seed(&ctx, NOW);
    let seeded = stamp(&cache_path(&roots));
    let cases = doc("cases.json");
    let mut rejected = 0usize;
    for row in cases.as_array().expect("rows") {
        let file = row["file"].as_str().expect("row.file");
        // The client's verdict follows the document (D3, FR-024): these error rows are accepted.
        if !row["expect"].is_object()
            || file.starts_with("invalid/sibling-")
            || file == "invalid/schema_version-const.json"
        {
            continue;
        }
        rejected += 1;
        let bytes = read(file);
        server.set_route(FEED_PATH, ok(&bytes));
        let result = check(&ctx, FetchMode::Normal, NOW + 5);
        let snapshot = result.snapshot.as_ref().expect("the cached copy");
        assert_eq!(snapshot.source, SnapshotSource::Cached, "{file}");
        assert_eq!(snapshot.fetched_at_unix, NOW, "{file}");
        assert_eq!(snapshot.feed, base, "{file}");
        let error = result.error.as_ref().expect("an error");
        if file == "invalid/malformed-json.json" {
            assert_eq!(error.code(), "E_INPUT", "{file}");
        } else {
            let document: Value = serde_json::from_slice(&bytes).expect("JSON");
            let schema = classify_schema_version(&document).expect("a known schema");
            let (kept, _) = drop_sibling_findings(contract::validate(&document, schema));
            let shown = error.to_string();
            assert!(shown.starts_with("E_FEED_INVALID"), "{file}: {shown}");
            assert!(
                shown.contains(&format!("{} finding(s)", kept.len())),
                "{file}: {shown}"
            );
            assert!(shown.contains(&kept[0].instance_path), "{file}: {shown}");
        }
        assert_eq!(stamp(&cache_path(&roots)), seeded, "{file}");
    }
    println!("rejected_feed_keeps_last_good_copy: {rejected} rejection rows");
    assert!(rejected >= 1);
}

#[test]
fn offline_returns_cached_flagged_stale() {
    let start = Instant::now();
    let base = full();
    let server = serve(ok(&read(FULL)));
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    seed(&ctx, NOW);
    let seeded = stamp(&cache_path(&roots));
    let (_td2, empty) = fresh_temp_roots();
    let bare = context(&empty, server.url(FEED_PATH));
    let replies = [
        (Reply::Close, "E_FEED_OFFLINE"),
        (
            Reply::Garbage(b"not http at all\r\n\r\n".to_vec()),
            "E_FEED_OFFLINE",
        ),
        (Reply::Stall(30 * SEC), "E_FEED_TIMEOUT"),
    ];
    for (reply, code) in replies {
        server.set_route(FEED_PATH, reply.clone());
        for (c, cached) in [(&ctx, true), (&bare, false)] {
            let before = feed_requests(&server);
            let result = check(c, FetchMode::Normal, NOW + 60);
            assert_eq!(feed_requests(&server) - before, 1, "{reply:?}");
            assert_eq!(code_of(result.error.as_ref()), Some(code), "{reply:?}");
            if cached {
                let snapshot = result.snapshot.as_ref().expect("the cached copy");
                assert_eq!(snapshot.source, SnapshotSource::Cached);
                assert_eq!(snapshot.fetched_at_unix, NOW);
                assert_eq!(snapshot.feed, base);
                assert_eq!(stamp(&cache_path(&roots)), seeded, "{reply:?}");
            } else {
                assert!(result.snapshot.is_none(), "{reply:?}");
                assert!(!cache_path(&empty).exists(), "{reply:?}");
            }
        }
    }
    drop(server);
    let elapsed = start.elapsed();
    println!("offline_returns_cached_flagged_stale: {elapsed:?}");
    assert!(elapsed < 5 * SEC, "{elapsed:?}");
}

#[test]
fn cache_rules() {
    install_capture();
    let body = read(FULL);
    let server = serve(Reply::Close);
    let url = server.url(FEED_PATH);
    let shaped = |source_url: &str| {
        json!({
            "cache_version": 1,
            "fetched_at_unix": NOW,
            "source_url": source_url,
            "body": text(&body),
        })
    };
    let edited = |edit: &dyn Fn(&mut Value)| {
        let mut v = shaped(&url);
        edit(&mut v);
        bytes_of(&v)
    };
    let dotdot = read("invalid/install-dotdot.json");
    let planted: [(&str, Vec<u8>, bool); 6] = [
        ("control", bytes_of(&shaped(&url)), true),
        (
            "another source_url",
            bytes_of(&shaped("http://127.0.0.1:9/versions.json")),
            false,
        ),
        (
            "cache_version 2",
            edited(&|v| v["cache_version"] = json!(2)),
            false,
        ),
        ("not JSON", b"not json".to_vec(), false),
        ("a fifth key", edited(&|v| v["extra"] = json!(true)), false),
        (
            "an invalid body",
            edited(&|v| v["body"] = json!(text(&dotdot))),
            false,
        ),
    ];
    for (what, bytes, usable) in planted {
        let (_td, roots) = fresh_temp_roots();
        let path = cache_path(&roots);
        fs::write(&path, &bytes).expect("plant the cache");
        let before = stamp(&path);
        let ctx = context(&roots, url.clone());
        server.set_route(FEED_PATH, Reply::Close);
        take_mine();
        let result = check(&ctx, FetchMode::Normal, NOW + 1);
        assert_eq!(
            code_of(result.error.as_ref()),
            Some("E_FEED_OFFLINE"),
            "{what}"
        );
        let w = warns();
        if usable {
            assert_eq!(source_of(&result), Some(SnapshotSource::Cached), "{what}");
            assert_eq!(w.len(), 1, "{what}: {w:?}");
        } else {
            assert!(result.snapshot.is_none(), "{what}");
            assert_eq!(w.len(), 2, "{what}: {w:?}");
            assert_eq!(
                w.iter()
                    .filter(|l| l.starts_with("feed cache ignored"))
                    .count(),
                1,
                "{what}: {w:?}"
            );
        }
        assert_eq!(stamp(&path), before, "{what}: a read never writes");
        server.set_route(FEED_PATH, ok(&body));
        let result = check(&ctx, FetchMode::Normal, NOW + 2);
        assert_eq!(source_of(&result), Some(SnapshotSource::Fresh), "{what}");
        assert_eq!(
            fs::read_to_string(&path).expect("cache"),
            cache_text(NOW + 2, &url, &body),
            "{what}"
        );
    }
}

fn injected(step: CacheStep) -> io::Error {
    io::Error::other(format!("injected fault at {}", step.as_str()))
}

/// Delegates to `StdCacheIo` except at `at`, where it fails without running the step (a held temp is dropped, so
/// removed), as `settings_tests.rs` `FaultIo` in `Mode::Fail` does.
struct FaultCacheIo {
    at: CacheStep,
}

impl FaultCacheIo {
    fn step<T>(&self, step: CacheStep, real: impl FnOnce() -> io::Result<T>) -> io::Result<T> {
        if step == self.at {
            Err(injected(step))
        } else {
            real()
        }
    }
}

impl CacheIo for FaultCacheIo {
    fn read(&mut self, path: &Path) -> io::Result<Option<Vec<u8>>> {
        StdCacheIo.read(path)
    }
    fn create_dir(&mut self, dir: &Path) -> io::Result<()> {
        self.step(CacheStep::CreateDir, || StdCacheIo.create_dir(dir))
    }
    fn create_temp(&mut self, dir: &Path) -> io::Result<NamedTempFile> {
        self.step(CacheStep::CreateTemp, || StdCacheIo.create_temp(dir))
    }
    fn write(&mut self, tmp: NamedTempFile, bytes: &[u8]) -> io::Result<NamedTempFile> {
        self.step(CacheStep::Write, || StdCacheIo.write(tmp, bytes))
    }
    fn sync_temp(&mut self, tmp: NamedTempFile) -> io::Result<NamedTempFile> {
        self.step(CacheStep::SyncTemp, || StdCacheIo.sync_temp(tmp))
    }
    fn persist(&mut self, tmp: NamedTempFile, target: &Path) -> io::Result<()> {
        self.step(CacheStep::Persist, || StdCacheIo.persist(tmp, target))
    }
    #[cfg(unix)]
    fn sync_dir(&mut self, dir: &Path) -> io::Result<()> {
        self.step(CacheStep::SyncDir, || StdCacheIo.sync_dir(dir))
    }
}

#[test]
fn cache_write_fault_keeps_previous() {
    install_capture();
    let first = read(FULL);
    let second = read(MIXED);
    for &at in STEPS {
        let server = serve(ok(&first));
        let (_td, roots) = fresh_temp_roots();
        let ctx = context(&roots, server.url(FEED_PATH));
        let path = cache_path(&roots);
        seed(&ctx, NOW);
        let previous = fs::read(&path).expect("the seeded cache");
        server.set_route(FEED_PATH, ok(&second));
        take_mine();
        let result = check_with(&mut FaultCacheIo { at }, &ctx, FetchMode::Normal, NOW + 1);
        let logged = take_mine();
        assert_eq!(source_of(&result), Some(SnapshotSource::Fresh), "{at:?}");
        assert_eq!(*feed_of(&result), parsed(&second), "{at:?}");
        assert!(result.error.is_none(), "{at:?}: {:?}", result.error);
        let cache_error = result.cache_error.as_ref().expect("the cache error");
        assert!(
            matches!(cache_error, FeedError::CacheIo { step, .. } if *step == at),
            "{cache_error}"
        );
        assert!(
            cache_error.to_string().starts_with("E_FEED_CACHE_IO"),
            "{cache_error}"
        );
        let w: Vec<&String> = logged
            .iter()
            .filter(|(_, l, _)| *l == Level::Warn)
            .map(|(_, _, m)| m)
            .collect();
        assert_eq!(w.len(), 1, "{at:?}: {logged:?}");
        assert!(
            w[0].contains("E_FEED_CACHE_IO") && w[0].contains(at.as_str()),
            "{w:?}"
        );
        assert_eq!(
            logged.iter().filter(|(_, l, _)| *l == Level::Info).count(),
            1,
            "{logged:?}"
        );
        // A sync-dir fault comes after the rename (FR-052): the new file is in place.
        let renamed = at.as_str() == "sync-dir";
        let now = fs::read(&path).expect("the cache");
        if renamed {
            assert_eq!(text(&now), cache_text(NOW + 1, &ctx.feed_url, &second));
        } else {
            assert_eq!(now, previous, "{at:?}");
        }
        assert_eq!(stray_temps(roots.state_dir()), 0, "{at:?}");
        server.set_route(FEED_PATH, Reply::Close);
        let offline = check(&ctx, FetchMode::Normal, NOW + 2);
        assert_eq!(source_of(&offline), Some(SnapshotSource::Cached), "{at:?}");
        let want = if renamed { parsed(&second) } else { full() };
        assert_eq!(*feed_of(&offline), want, "{at:?}");
    }
}

#[test]
fn check_logs_one_line() {
    install_capture();
    take_mine();
    let server = serve(ok(&read(FULL)));
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    let _ = check(&ctx, FetchMode::Normal, NOW);
    let fresh = take_mine();
    assert_eq!(fresh.len(), 1, "{fresh:?}");
    assert_eq!(fresh[0].0, LOG_TARGET);
    assert_eq!(fresh[0].1, Level::Info);
    assert_eq!(fresh[0].2, "feed ok: 3 plugins, schema 2");

    server.set_route(FEED_PATH, Reply::Close);
    let _ = check(&ctx, FetchMode::Normal, NOW + 1);
    let offline = take_mine();
    assert_eq!(offline.len(), 1, "{offline:?}");
    assert_eq!(offline[0].0, LOG_TARGET);
    assert_eq!(offline[0].1, Level::Warn);
    assert!(
        offline[0].2.starts_with("E_FEED_OFFLINE")
            && offline[0]
                .2
                .ends_with("; using cached copy from 1700000000"),
        "{offline:?}"
    );

    fs::write(cache_path(&roots), b"{}").expect("plant an unusable cache");
    let _ = check(&ctx, FetchMode::Normal, NOW + 2);
    let ignored = take_mine();
    assert_eq!(ignored.len(), 2, "{ignored:?}");
    assert!(
        ignored
            .iter()
            .all(|(t, l, _)| t == LOG_TARGET && *l == Level::Warn)
    );
    assert!(
        ignored[0].2.starts_with("feed cache ignored"),
        "{ignored:?}"
    );
    assert!(
        ignored[1].2.starts_with("E_FEED_OFFLINE") && ignored[1].2.ends_with("; no cached copy"),
        "{ignored:?}"
    );
}

#[test]
fn concurrent_checks_leave_one_whole_file() {
    let bodies = [read(FULL), read(MIXED)];
    let servers = [serve(ok(&bodies[0])), serve(ok(&bodies[1]))];
    let urls = [servers[0].url(FEED_PATH), servers[1].url(FEED_PATH)];
    let (_td, roots) = fresh_temp_roots();
    let before = names(roots.state_dir());
    let barrier = Barrier::new(8);
    let results: Vec<FeedCheck> = thread::scope(|s| {
        let handles: Vec<_> = (0..8u64)
            .map(|i| {
                let ctx = context(&roots, urls[(i % 2) as usize].clone());
                let barrier = &barrier;
                s.spawn(move || {
                    barrier.wait();
                    check(&ctx, FetchMode::Normal, NOW + i)
                })
            })
            .collect();
        handles
            .into_iter()
            .map(|h| h.join().expect("a check thread panicked"))
            .collect()
    });
    for r in &results {
        assert_eq!(source_of(r), Some(SnapshotSource::Fresh), "{:?}", r.error);
        assert!(r.error.is_none(), "{:?}", r.error);
        if let Some(e) = &r.cache_error {
            assert!(
                matches!(
                    e,
                    FeedError::CacheIo {
                        step: CacheStep::Persist,
                        ..
                    }
                ),
                "{e}"
            );
        }
    }
    let raced = results.iter().filter(|r| r.cache_error.is_some()).count();
    println!("concurrent_checks_leave_one_whole_file: {raced} of 8 lost the rename race");
    assert!(results.iter().any(|r| r.cache_error.is_none()));
    let file: Value = serde_json::from_slice(&fs::read(cache_path(&roots)).expect("the cache"))
        .expect("the cache is whole JSON");
    let obj = file.as_object().expect("an object");
    assert_eq!(
        obj.keys().map(String::as_str).collect::<Vec<_>>(),
        ["body", "cache_version", "fetched_at_unix", "source_url"]
    );
    let body = file["body"].as_str().expect("body");
    let url = file["source_url"].as_str().expect("source_url");
    let served = (0..2).find(|&i| body == text(&bodies[i]));
    let served = served.expect("the body is one served file");
    assert_eq!(url, urls[served]);
    let mut after = before;
    after.push(FEED_CACHE_FILE.to_owned());
    after.sort();
    assert_eq!(names(roots.state_dir()), after);
}

#[test]
fn fresh_lower_version_replaces_cache() {
    install_capture();
    take_mine();
    let first = read(FULL);
    let mut lowered = doc(FULL);
    lowered["plugins"]["Alpha"]["version"] = json!("1.1.0");
    lowered["plugins"]["Alpha"]["tag"] = json!("alpha/v1.1.0");
    assert_valid(&lowered, Schema::V2, "lowered");
    let second = bytes_of(&lowered);
    let server = serve(ok(&first));
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    seed(&ctx, NOW);
    server.set_route(FEED_PATH, ok(&second));
    take_mine();
    let result = check(&ctx, FetchMode::Normal, NOW + 1);
    assert!(warns().is_empty(), "the lowering is not an anomaly");
    assert_eq!(source_of(&result), Some(SnapshotSource::Fresh));
    assert!(result.error.is_none(), "{:?}", result.error);
    let alpha = &feed_of(&result).plugins["Alpha"];
    assert_eq!(alpha.version_text, "1.1.0");
    assert_eq!(*feed_of(&result), parsed(&second));
    let cache: Value =
        serde_json::from_slice(&fs::read(cache_path(&roots)).expect("cache")).expect("JSON");
    assert_eq!(cache["body"].as_str(), Some(text(&second)));
    server.set_route(FEED_PATH, Reply::Close);
    let offline = check(&ctx, FetchMode::Normal, NOW + 2);
    assert_eq!(source_of(&offline), Some(SnapshotSource::Cached));
    assert_eq!(*feed_of(&offline), parsed(&second));
    server.set_route(FEED_PATH, ok(&first));
    let restored = check(&ctx, FetchMode::Normal, NOW + 3);
    assert_eq!(source_of(&restored), Some(SnapshotSource::Fresh));
    assert_eq!(feed_of(&restored).plugins["Alpha"].version_text, "1.2.0");
}

#[test]
fn one_attempt_per_check() {
    let base = full();
    let server = serve(ok(&read(FULL)));
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    seed(&ctx, NOW);
    let seeded = stamp(&cache_path(&roots));
    let (_td2, empty) = fresh_temp_roots();
    let bare = context(&empty, server.url(FEED_PATH));
    let failures = [
        Reply::Status(503),
        Reply::Status(500),
        Reply::Close,
        Reply::Garbage(b"\x00\x01 not a response\r\n\r\n".to_vec()),
        Reply::Stall(30 * SEC),
    ];
    let expect_code = |reply: &Reply, e: Option<&FeedError>| match reply {
        Reply::Status(want) => matches!(
            e,
            Some(FeedError::Http(HttpError::Status { status })) if status == want
        ),
        Reply::Stall(_) => code_of(e) == Some("E_FEED_TIMEOUT"),
        _ => code_of(e) == Some("E_FEED_OFFLINE"),
    };
    for reply in failures {
        server.set_route(FEED_PATH, reply.clone());
        for (c, cached) in [(&ctx, true), (&bare, false)] {
            let before = feed_requests(&server);
            let start = Instant::now();
            let result = check(c, FetchMode::Normal, NOW + 9);
            let elapsed = start.elapsed();
            assert!(elapsed < SEC + SEC, "{reply:?}: {elapsed:?}");
            assert_eq!(feed_requests(&server) - before, 1, "{reply:?}");
            assert!(
                expect_code(&reply, result.error.as_ref()),
                "{reply:?}: {:?}",
                result.error
            );
            if cached {
                let snapshot = result.snapshot.as_ref().expect("the cached copy");
                assert_eq!(snapshot.source, SnapshotSource::Cached);
                assert_eq!(snapshot.fetched_at_unix, NOW);
                assert_eq!(snapshot.feed, base);
                assert_eq!(stamp(&cache_path(&roots)), seeded, "{reply:?}");
            } else {
                assert!(result.snapshot.is_none(), "{reply:?}");
            }
        }
    }
    // A two-hop redirect chain to a 503 is one chain: three requests, no second chain.
    server.set_route(
        FEED_PATH,
        Reply::Redirect {
            status: 302,
            location: "/h2".to_owned(),
        },
    );
    server.set_route(
        "/h2",
        Reply::Redirect {
            status: 302,
            location: "/h3".to_owned(),
        },
    );
    server.set_route("/h3", Reply::Status(503));
    let before = server.requests().len();
    let start = Instant::now();
    let result = check(&ctx, FetchMode::Normal, NOW + 10);
    assert!(start.elapsed() < SEC + SEC);
    let paths: Vec<String> = server.requests()[before..]
        .iter()
        .map(|r| r.path.clone())
        .collect();
    assert_eq!(paths, [FEED_PATH, "/h2", "/h3"]);
    assert!(matches!(
        result.error,
        Some(FeedError::Http(HttpError::Status { status: 503 }))
    ));
    assert_eq!(source_of(&result), Some(SnapshotSource::Cached));
    assert_eq!(stamp(&cache_path(&roots)), seeded);
}

#[test]
fn check_through_proxy_writes_cache() {
    let body = read(FULL);
    // The target is never reached: the proxy tunnels to it and answers the tunnelled GET itself.
    let target = serve(ok(b"never"));
    let authority = format!("127.0.0.1:{}", target.port());
    let proxy = FixtureServer::start(vec![
        (authority.clone(), Reply::ConnectOk),
        (FEED_PATH.to_owned(), ok(&body)),
    ]);
    let (_td, roots) = fresh_temp_roots();
    let url = target.url(FEED_PATH);
    let ctx = AppContext {
        http: HttpConfig::loopback(SEC, SEC).with_proxy(Some(ProxyConfig {
            url: format!("http://127.0.0.1:{}", proxy.port()),
            no_proxy: Vec::new(),
        })),
        ..context(&roots, url.clone())
    };
    let result = check(&ctx, FetchMode::Normal, NOW);
    assert_eq!(
        source_of(&result),
        Some(SnapshotSource::Fresh),
        "{:?}",
        result.error
    );
    assert_eq!(*feed_of(&result), full());
    assert_eq!(
        fs::read_to_string(cache_path(&roots)).expect("cache"),
        cache_text(NOW, &url, &body)
    );
    let lines: Vec<String> = proxy
        .requests()
        .iter()
        .map(|r| format!("{} {}", r.method, r.path))
        .collect();
    assert_eq!(
        lines,
        [format!("CONNECT {authority}"), format!("GET {FEED_PATH}")]
    );
    assert!(target.requests().is_empty());
    let seeded = stamp(&cache_path(&roots));
    for reply in [Reply::ConnectRefused(407), Reply::Close] {
        proxy.set_route(&authority, reply.clone());
        let result = check(&ctx, FetchMode::Normal, NOW + 1);
        assert_eq!(
            source_of(&result),
            Some(SnapshotSource::Cached),
            "{reply:?}"
        );
        assert_eq!(*feed_of(&result), full(), "{reply:?}");
        assert_eq!(
            code_of(result.error.as_ref()),
            Some("E_FEED_OFFLINE"),
            "{reply:?}"
        );
        assert_eq!(stamp(&cache_path(&roots)), seeded, "{reply:?}");
    }
    assert!(target.requests().is_empty());
}

#[cfg(not(windows))]
#[test]
fn check_survives_pac_failure() {
    use crate::http::PacSource;
    use crate::pac::tests::FakePacHost;
    install_capture();
    let body = read(FULL);
    let looping = "function FindProxyForURL(url, host) { while (true) {} }";
    let direct = "function FindProxyForURL(url, host) { return \"DIRECT\"; }";
    let script = |s: &str| Reply::Ok {
        body: s.as_bytes().to_vec(),
        content_type: "application/x-ns-proxy-autoconfig",
    };
    let pac = FixtureServer::start(vec![
        ("/loop.pac?k=v".to_owned(), script(looping)),
        ("/good.pac?k=v".to_owned(), script(direct)),
    ]);
    // (PAC path, the warn's code or none); the missing path is the server's default 404.
    let rows = [
        ("/missing.pac", Some("E_PAC_FETCH")),
        ("/loop.pac", Some("E_PAC_SCRIPT")),
        ("/good.pac", None),
    ];
    for (path, code) in rows {
        let server = serve(ok(&body));
        let (_td, roots) = fresh_temp_roots();
        let (_fake, handle) = FakePacHost::new().handle();
        // The endless loop is interrupted after PAC_EVAL_BUDGET (2 s), so the total timeout must outlast it.
        let http = HttpConfig::loopback(SEC, 4 * SEC)
            .with_pac(Some(PacSource {
                auto_detect: false,
                url: Some(pac.url(&format!("{path}?k=v"))),
            }))
            .with_pac_host(Some(handle));
        let ctx = AppContext {
            http,
            ..context(&roots, server.url(FEED_PATH))
        };
        take_mine();
        let start = Instant::now();
        let result = check(&ctx, FetchMode::Normal, NOW);
        let elapsed = start.elapsed();
        let w = warns();
        assert_eq!(source_of(&result), Some(SnapshotSource::Fresh), "{path}");
        assert!(result.error.is_none(), "{path}: {:?}", result.error);
        assert_eq!(
            fs::read_to_string(cache_path(&roots)).expect("cache"),
            cache_text(NOW, &ctx.feed_url, &body),
            "{path}"
        );
        assert!(
            elapsed < crate::pac::PAC_EVAL_BUDGET + SEC,
            "{path}: {elapsed:?}"
        );
        match code {
            Some(code) => {
                assert_eq!(w.len(), 1, "{path}: {w:?}");
                assert!(w[0].starts_with(code), "{path}: {w:?}");
                assert!(w[0].contains(path), "{path}: {w:?}");
                assert!(!w[0].contains("k=v"), "{path}: {w:?}");
            }
            None => assert!(w.is_empty(), "{path}: {w:?}"),
        }
        assert_eq!(feed_requests(&server), 1, "{path}");
    }
}
