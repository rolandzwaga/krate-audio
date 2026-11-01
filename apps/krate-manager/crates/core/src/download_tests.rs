//! `download` unit tests (Phase 7 plan §7, §7.1 rows SC-002 to SC-037). Every request goes to the test's own loopback
//! `FixtureServer` through D13's seam; every environment comes from `test_support::DownloadRig`; every feed from
//! `test_support::test_feed`; every signature from a throwaway `TestSigner`.

use super::*;
use crate::feed::{self, AssetFormat, Feed};
use crate::fixture_server::{FixtureServer, RecordedRequest, Reply};
use crate::http::{self, HeadAction, HttpConfig, StreamError, StreamLimits};
use crate::roots::Roots;
use crate::settings::Settings;
use crate::test_signer::{TestSigner, c3_comment, with_key_id};
use crate::test_support::{
    DownloadRig, FakeHostFacts, FaultyStagingIo, Op, TestPlugin, fresh_temp_roots, install_capture,
    native_key, take_mine, test_feed, test_feed_document, test_policy,
};
use crate::verify::{Expected, SigningKey, VerifyError, hex_lower, sha256_of, verify_file};
use log::Level;
use std::cell::{Cell, RefCell};
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::mpsc;
use std::thread;
use std::time::Instant;

const SEC: Duration = Duration::from_secs(1);
const VERSION: &str = "1.2.3";
/// 1 MiB + 17: not a multiple of 64 KiB.
const SIZE: usize = 1_048_593;
const NOW: u64 = 1_793_404_800;
const OCTET: &str = "application/octet-stream";
const FEED_ROUTE: &str = "/versions.json";
const CDN: &str = "/cdn/a.bin?token=SECRET-TOKEN";
const WAIT: Duration = Duration::from_secs(20);
const MIXED: &[u8] =
    include_bytes!("../../../../../tools/feed-contract/fixtures/feeds/valid/mixed.json");
const V1_ONLY: &[u8] =
    include_bytes!("../../../../../tools/feed-contract/fixtures/feeds/valid/v1-only.json");

// ---- helpers -------------------------------------------------------------------------------------------------------

/// Seeded xorshift64 bytes.
fn seeded(seed: u64, len: usize) -> Vec<u8> {
    let mut x = seed.wrapping_mul(0x9E37_79B9_7F4A_7C15) | 1;
    (0..len)
        .map(|_| {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            x.to_le_bytes()[3]
        })
        .collect()
}

fn gzip(bytes: &[u8]) -> Vec<u8> {
    let mut encoder = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
    encoder.write_all(bytes).expect("gzip write");
    encoder.finish().expect("gzip finish")
}

/// The bytes a test serves for one plugin.
#[derive(Clone)]
struct Asset {
    name: &'static str,
    file: String,
    bytes: Vec<u8>,
}

impl Asset {
    fn new(name: &'static str, seed: u64, len: usize) -> Asset {
        Asset {
            name,
            file: format!("{name}-{VERSION}-test.bin"),
            bytes: seeded(seed, len),
        }
    }

    fn alpha() -> Asset {
        Asset::new("Alpha", 1, SIZE)
    }

    fn route(&self) -> String {
        format!("/{}", self.file)
    }

    fn ranged(&self) -> Reply {
        Reply::Ranged {
            body: self.bytes.clone(),
        }
    }

    fn hex(&self) -> String {
        hex_lower(&sha256_of(&self.bytes))
    }
}

/// One feed entry of a test feed, every field editable by a row.
struct Entry {
    name: &'static str,
    version: &'static str,
    key: PlatformKey,
    file: String,
    size: u64,
    sha256: [u8; 32],
    signature: String,
    min_os: Option<&'static str>,
}

/// The entry that describes `asset` truthfully, signed by `signer`.
fn entry(asset: &Asset, signer: &TestSigner) -> Entry {
    Entry {
        name: asset.name,
        version: VERSION,
        key: native_key(),
        file: asset.file.clone(),
        size: asset.bytes.len() as u64,
        sha256: sha256_of(&asset.bytes),
        signature: signer.sign_c3(&asset.bytes, asset.name, VERSION, native_key(), &asset.file),
        min_os: None,
    }
}

fn plugins(entries: &[Entry]) -> Vec<TestPlugin<'_>> {
    entries
        .iter()
        .map(|e| TestPlugin {
            name: e.name,
            version: e.version,
            key: e.key,
            asset: &e.file,
            size: e.size,
            sha256: e.sha256,
            signature: e.signature.clone(),
            min_os: e.min_os,
        })
        .collect()
}

fn feed_of(entries: &[Entry]) -> Feed {
    test_feed(&plugins(entries))
}

fn doc_of(entries: &[Entry]) -> Vec<u8> {
    test_feed_document(&plugins(entries))
}

/// A context over temp roots (Rule 1): the loopback policy and the given key.
fn context(roots: Roots, signing_key: SigningKey, feed_url: String) -> AppContext {
    AppContext {
        roots,
        settings: Settings::default(),
        feed_url,
        http: HttpConfig::loopback(SEC, SEC),
        signing_key,
    }
}

fn downloads_of(ctx: &AppContext) -> PathBuf {
    ctx.roots.staging_dir().join(DOWNLOADS_DIR)
}

/// A platform key this machine does not accept.
fn other_key() -> PlatformKey {
    [PlatformKey::WindowsX86_64, PlatformKey::LinuxX86_64]
        .into_iter()
        .find(|k| *k != native_key())
        .expect("one of the two is foreign")
}

fn header<'a>(request: &'a RecordedRequest, name: &str) -> Option<&'a str> {
    request
        .headers
        .iter()
        .find(|(n, _)| n.eq_ignore_ascii_case(name))
        .map(|(_, v)| v.as_str())
}

fn range_of(request: &RecordedRequest) -> Option<&str> {
    header(request, "Range")
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

fn exists(path: &Path) -> bool {
    fs::symlink_metadata(path).is_ok()
}

/// The (code, step) of a staging error.
fn staging_of(error: &DownloadError) -> (&'static str, &'static str) {
    match error {
        DownloadError::Staging { step, .. } => (error.code(), step.as_str()),
        other => panic!("not a staging error: {other}"),
    }
}

fn retryings(events: &[Progress]) -> Vec<(u32, u32, String, u64)> {
    events
        .iter()
        .filter_map(|e| match e {
            Progress::Retrying {
                attempt,
                of,
                code,
                wait_ms,
            } => Some((*attempt, *of, code.clone(), *wait_ms)),
            _ => None,
        })
        .collect()
}

fn restarts(events: &[Progress]) -> Vec<RestartReason> {
    events
        .iter()
        .filter_map(|e| match e {
            Progress::Restarted { reason } => Some(*reason),
            _ => None,
        })
        .collect()
}

fn bytes_events(events: &[Progress]) -> Vec<(u64, u64)> {
    events
        .iter()
        .filter_map(|e| match e {
            Progress::Bytes {
                received, on_disk, ..
            } => Some((*received, *on_disk)),
            _ => None,
        })
        .collect()
}

/// Plan §7's shared progress check (SI-24), on every recorded run. A failed run that ended before `Started` (FR-006
/// busy, FR-017 cancelled before the call, a refusal at steps 3-4) records nothing.
fn assert_progress_invariants(events: &[Progress], size: u64, ok: bool) {
    if events.is_empty() {
        assert!(!ok, "a successful run emits events");
        return;
    }
    let starts: Vec<usize> = (0..events.len())
        .filter(|&i| matches!(events[i], Progress::Started { .. }))
        .collect();
    assert_eq!(starts, vec![0], "exactly one Started, first: {events:?}");
    let Progress::Started {
        total,
        resumed_from,
    } = events[0]
    else {
        unreachable!("checked above")
    };
    assert_eq!(total, size, "Started.total");
    let verifying = events.iter().filter(|e| **e == Progress::Verifying).count();
    assert!(verifying <= 2, "at most two Verifying: {events:?}");
    let verified = events.iter().filter(|e| **e == Progress::Verified).count();
    if ok {
        assert_eq!(verified, 1, "one Verified: {events:?}");
        assert_eq!(events.last(), Some(&Progress::Verified), "Verified last");
        assert_eq!(
            events[events.len() - 2],
            Progress::Verifying,
            "Verified right after a Verifying"
        );
    } else {
        assert_eq!(verified, 0, "a failed run has no Verified: {events:?}");
    }
    let restarted = events
        .iter()
        .any(|e| matches!(e, Progress::Restarted { .. }));
    let mut previous: Option<u64> = None;
    let mut seen_restart = false;
    let mut after_restart = false;
    for event in events {
        match event {
            Progress::Restarted { .. } => {
                seen_restart = true;
                after_restart = true;
            }
            Progress::Bytes {
                received,
                on_disk,
                total,
            } => {
                assert_eq!(*total, size, "Bytes.total");
                match previous {
                    // FR-022: the first Bytes after a resume is past `resumed_from` (after a CachedInvalid restart
                    // `received` holds at its high-water mark, `size`, until `on_disk` passes it).
                    None if !seen_restart => assert!(
                        *received > resumed_from,
                        "first Bytes {received} > resumed_from {resumed_from}"
                    ),
                    None => {}
                    Some(prev) if restarted => {
                        assert!(*received >= prev, "received never decreases: {events:?}")
                    }
                    Some(prev) => assert!(
                        *received > prev,
                        "received strictly increases without a Restarted: {events:?}"
                    ),
                }
                if !restarted {
                    assert_eq!(received, on_disk, "on_disk == received without a Restarted");
                }
                if after_restart {
                    assert!(
                        *on_disk <= PROGRESS_STEP,
                        "on_disk restarts at <= PROGRESS_STEP: {events:?}"
                    );
                    after_restart = false;
                }
                previous = Some(*received);
            }
            _ => {}
        }
    }
    let last = bytes_events(events).last().copied();
    if ok {
        match last {
            Some((received, on_disk)) => {
                assert_eq!((received, on_disk), (size, size), "last Bytes at size");
                assert!(
                    resumed_from < size || restarted,
                    "a run that transferred nothing emits no Bytes: {events:?}"
                );
            }
            None => assert!(
                resumed_from == size && !restarted,
                "a successful run with no Bytes transferred nothing: {events:?}"
            ),
        }
    }
}

/// One recorded `obtain_and` run.
struct Recorded<T> {
    result: Result<Obtained<T>, DownloadError>,
    events: Vec<Progress>,
    nexts: usize,
}

impl<T> Recorded<T> {
    fn ok(&self) -> &Obtained<T> {
        match &self.result {
            Ok(o) => o,
            Err(e) => panic!("expected Ok, got {e}"),
        }
    }

    fn err(&self) -> &DownloadError {
        match &self.result {
            Ok(_) => panic!("expected an error, got Ok"),
            Err(e) => e,
        }
    }
}

/// `obtain_and` with a recording progress closure and a counting `next`, then the shared progress check.
fn record<T>(
    env: DownloadEnv<'_>,
    feed: &Feed,
    request: &DownloadRequest,
    next: impl FnOnce(VerifiedArtifact) -> T,
) -> Recorded<T> {
    let mut events = Vec::new();
    let nexts = Cell::new(0);
    let result = obtain_and(env, feed, request, &mut |e| events.push(e), |a| {
        nexts.set(nexts.get() + 1);
        next(a)
    });
    assert_progress_invariants(&events, request.asset().size, result.is_ok());
    Recorded {
        result,
        events,
        nexts: nexts.get(),
    }
}

/// One plugin served by one fixture server in one temp root.
struct Fx {
    _td: tempfile::TempDir,
    asset: Asset,
    feed: Feed,
    server: FixtureServer,
    request: DownloadRequest,
    rig: DownloadRig,
}

impl Fx {
    /// Alpha's asset, truthfully described and signed, answered by `reply` at its route.
    fn new(reply: impl FnOnce(&Asset) -> Reply) -> Fx {
        Fx::build(Asset::alpha(), TestSigner::new(), |_| {}, reply)
    }

    fn build(
        asset: Asset,
        signer: TestSigner,
        edit: impl FnOnce(&mut Entry),
        reply: impl FnOnce(&Asset) -> Reply,
    ) -> Fx {
        let (td, roots) = fresh_temp_roots();
        let mut described = entry(&asset, &signer);
        edit(&mut described);
        let feed = feed_of(&[described]);
        let server = FixtureServer::start(vec![(asset.route(), reply(&asset))]);
        let request = request_for(&feed, asset.name, None)
            .expect("the test feed offers the asset")
            .with_fixture_url(&server.url(&asset.route()))
            .expect("a fixture URL");
        let rig = DownloadRig::new(context(roots, signer.key(), String::new()));
        Fx {
            _td: td,
            asset,
            feed,
            server,
            request,
            rig,
        }
    }

    fn base(&self) -> PathBuf {
        self.rig
            .ctx
            .roots
            .state_dir()
            .parent()
            .expect("the state dir has a parent")
            .to_path_buf()
    }

    fn downloads(&self) -> PathBuf {
        downloads_of(&self.rig.ctx)
    }

    fn hex(&self) -> String {
        hex_lower(&self.request.asset().sha256)
    }

    fn part(&self) -> PathBuf {
        self.downloads().join(format!("{}.part", self.hex()))
    }

    fn dir(&self) -> PathBuf {
        self.downloads().join(self.hex())
    }

    fn verified(&self) -> PathBuf {
        self.dir().join(&self.asset.file)
    }

    fn size(&self) -> u64 {
        self.request.asset().size
    }

    fn requests(&self) -> Vec<RecordedRequest> {
        self.server.requests()
    }

    fn run(&self) -> Recorded<VerifiedArtifact> {
        self.run_env(self.rig.env())
    }

    fn run_env(&self, env: DownloadEnv<'_>) -> Recorded<VerifiedArtifact> {
        record(env, &self.feed, &self.request, |a| a)
    }

    fn seed_part(&self, bytes: &[u8]) {
        fs::create_dir_all(self.downloads()).expect("create downloads");
        fs::write(self.part(), bytes).expect("seed the .part");
    }

    fn expected(&self) -> Expected<'_> {
        Expected::from_request(&self.request)
    }
}

/// Several plugins of one feed, each `Ranged` at its route, in one temp root.
struct Many {
    _td: tempfile::TempDir,
    assets: Vec<Asset>,
    feed: Feed,
    server: FixtureServer,
    rig: DownloadRig,
}

impl Many {
    fn new(assets: Vec<Asset>) -> Many {
        let signer = TestSigner::new();
        let (td, roots) = fresh_temp_roots();
        let entries: Vec<Entry> = assets.iter().map(|a| entry(a, &signer)).collect();
        let feed = feed_of(&entries);
        let server = FixtureServer::start(assets.iter().map(|a| (a.route(), a.ranged())).collect());
        let rig = DownloadRig::new(context(roots, signer.key(), String::new()));
        Many {
            _td: td,
            assets,
            feed,
            server,
            rig,
        }
    }

    fn request(&self, i: usize) -> DownloadRequest {
        let asset = &self.assets[i];
        request_for(&self.feed, asset.name, None)
            .expect("offered")
            .with_fixture_url(&self.server.url(&asset.route()))
            .expect("a fixture URL")
    }

    fn obtain(&self, i: usize) -> Recorded<VerifiedArtifact> {
        record(self.rig.env(), &self.feed, &self.request(i), |a| a)
    }

    fn downloads(&self) -> PathBuf {
        downloads_of(&self.rig.ctx)
    }
}

/// Every path under `base` (relative) with its kind, length and SHA-256.
fn tree(base: &Path) -> BTreeMap<PathBuf, (char, u64, [u8; 32])> {
    fn walk(base: &Path, dir: &Path, out: &mut BTreeMap<PathBuf, (char, u64, [u8; 32])>) {
        for item in fs::read_dir(dir).expect("read_dir") {
            let path = item.expect("dir entry").path();
            let meta = fs::symlink_metadata(&path).expect("symlink_metadata");
            let rel = path.strip_prefix(base).expect("under base").to_path_buf();
            if meta.file_type().is_symlink() {
                out.insert(rel, ('l', 0, [0; 32]));
            } else if meta.is_dir() {
                out.insert(rel, ('d', 0, [0; 32]));
                walk(base, &path, out);
            } else {
                let bytes = fs::read(&path).expect("read");
                out.insert(rel, ('f', meta.len(), sha256_of(&bytes)));
            }
        }
    }
    let mut out = BTreeMap::new();
    walk(base, base, &mut out);
    out
}

/// Every change between two snapshots lies under `inside` (relative to the snapshot base).
fn assert_changes_only_under(
    label: &str,
    before: &BTreeMap<PathBuf, (char, u64, [u8; 32])>,
    after: &BTreeMap<PathBuf, (char, u64, [u8; 32])>,
    inside: &Path,
) {
    for (path, what) in after {
        if before.get(path) != Some(what) {
            assert!(
                path.starts_with(inside),
                "{label}: {} changed outside {}",
                path.display(),
                inside.display()
            );
        }
    }
    for path in before.keys() {
        if !after.contains_key(path) {
            assert!(
                path.starts_with(inside),
                "{label}: {} removed outside {}",
                path.display(),
                inside.display()
            );
        }
    }
}

fn head(
    status: u16,
    length: Option<u64>,
    range: Option<&str>,
    encoding: Option<&str>,
) -> ResponseHead {
    ResponseHead {
        status,
        content_length: length,
        content_range: range.map(str::to_owned),
        content_encoding: encoding.map(str::to_owned),
    }
}

// ---- pure, no socket -----------------------------------------------------------------------------------------------

#[test]
fn classify_head_table() {
    const S: u64 = 1000;
    let exact = "bytes 400-999/1000";
    let ok = |v: HeadVerdict| -> Result<HeadVerdict, &'static str> { Ok(v) };
    let rows: [(u64, ResponseHead, Result<HeadVerdict, &'static str>); 15] = [
        (0, head(200, None, None, None), ok(HeadVerdict::Full)),
        (0, head(200, Some(S), None, None), ok(HeadVerdict::Full)),
        (
            0,
            head(200, Some(S + 1), None, None),
            Err("E_DOWNLOAD_SIZE"),
        ),
        (
            400,
            head(200, Some(S), Some(exact), None),
            ok(HeadVerdict::Full),
        ),
        (400, head(200, None, None, None), ok(HeadVerdict::Full)),
        (
            400,
            head(200, Some(S - 400), None, None),
            Err("E_DOWNLOAD_SIZE"),
        ),
        (
            400,
            head(206, None, Some(exact), None),
            ok(HeadVerdict::Continue),
        ),
        (
            400,
            head(206, Some(S - 400), Some(exact), None),
            ok(HeadVerdict::Continue),
        ),
        (
            400,
            head(206, Some(S), Some(exact), None),
            ok(HeadVerdict::RestartFull),
        ),
        (
            400,
            head(206, Some(S - 400), Some("bytes 0-999/1000"), None),
            ok(HeadVerdict::RestartFull),
        ),
        (
            400,
            head(206, Some(S - 400), Some("bytes 400-999/1001"), None),
            ok(HeadVerdict::RestartFull),
        ),
        (
            400,
            head(206, Some(S - 400), None, None),
            ok(HeadVerdict::RestartFull),
        ),
        (
            0,
            head(206, Some(S), Some("bytes 0-999/1000"), None),
            ok(HeadVerdict::RestartFull),
        ),
        (
            0,
            head(404, None, None, None),
            Err("E_DOWNLOAD_HTTP_STATUS"),
        ),
        (
            400,
            head(416, None, None, None),
            Err("E_DOWNLOAD_HTTP_STATUS"),
        ),
    ];
    for (from, h, expected) in rows {
        let got = classify_head(from, S, &h).map_err(|e| e.code());
        assert_eq!(got, expected, "from {from}, {h:?}");
    }
    match classify_head(0, S, &head(200, Some(7), None, None)) {
        Err(DownloadError::Size {
            expected, found, ..
        }) => assert_eq!((expected, found), (S, Some(7))),
        other => panic!("a wrong Content-Length: {other:?}"),
    }
    for encoding in [
        "gzip", "br", "deflate", "zstd", "x-gzip", "GZIP", "compress",
    ] {
        match classify_head(0, S, &head(200, Some(S), None, Some(encoding))) {
            Err(DownloadError::Encoding { encoding: e, .. }) => assert_eq!(e, encoding),
            other => panic!("{encoding}: {other:?}"),
        }
        match classify_head(400, S, &head(206, None, Some(exact), Some(encoding))) {
            Err(e) => assert_eq!(e.code(), "E_DOWNLOAD_ENCODING", "{encoding} on a 206"),
            Ok(v) => panic!("{encoding} on a 206: {v:?}"),
        }
    }
    for encoding in ["identity", "IDENTITY", " identity "] {
        assert_eq!(
            classify_head(0, S, &head(200, Some(S), None, Some(encoding))).map_err(|e| e.code()),
            Ok(HeadVerdict::Full),
            "{encoding:?}"
        );
    }
}

#[test]
fn staged_paths_cases() {
    let (_td, roots) = fresh_temp_roots();
    let downloads = roots.staging_dir().join(DOWNLOADS_DIR);
    fs::create_dir_all(&downloads).expect("create downloads");
    let hex = "0123456789abcdef".repeat(4);
    let paths = staged_paths(&downloads, &hex, "Alpha-1.2.3-test.bin").expect("a valid triple");
    assert_eq!(paths.part, downloads.join(format!("{hex}.part")));
    assert_eq!(paths.dir, downloads.join(&hex));
    assert_eq!(
        paths.verified,
        downloads.join(&hex).join("Alpha-1.2.3-test.bin")
    );

    let mut bad_assets = vec!["a/b", "a\\b", "..", ".", "", "/abs"];
    if cfg!(windows) {
        bad_assets.extend(["C:x", "C:\\x"]);
    }
    for asset in bad_assets {
        match staged_paths(&downloads, &hex, asset) {
            Err(e) => assert_eq!(e.code(), "E_DOWNLOAD_PATH", "asset {asset:?}"),
            Ok(p) => panic!("asset {asset:?} accepted: {p:?}"),
        }
    }
    let upper = "0123456789ABCDEF".repeat(4);
    let not_hex = format!("{}g", &hex[..63]);
    let longer = format!("{hex}0");
    for bad in [
        &hex[..63],
        longer.as_str(),
        upper.as_str(),
        not_hex.as_str(),
        "",
    ] {
        match staged_paths(&downloads, bad, "a.bin") {
            Err(e) => assert_eq!(e.code(), "E_DOWNLOAD_PATH", "hex {bad:?}"),
            Ok(p) => panic!("hex {bad:?} accepted: {p:?}"),
        }
    }
    let left: Vec<_> = fs::read_dir(&downloads)
        .expect("read_dir")
        .map(|e| e.expect("entry").file_name())
        .collect();
    assert!(left.is_empty(), "staged_paths creates nothing: {left:?}");
}

#[test]
fn fixture_url_seam_cases() {
    let server = FixtureServer::start(vec![]);
    let port = server.port();
    let asset = Asset::new("Alpha", 1, 1000);
    let feed = feed_of(&[entry(&asset, &TestSigner::new())]);
    let request = request_for(&feed, "Alpha", None).expect("offered");
    assert_eq!(
        request.fetch_target(),
        request.asset().url.as_str(),
        "no seam: the feed URL"
    );
    let file = &asset.file;
    for url in [
        server.url(&format!("/{file}")),
        format!("http://[::1]:{port}/x/{file}"),
    ] {
        let seamed = request
            .clone()
            .with_fixture_url(&url)
            .unwrap_or_else(|e| panic!("{url}: {e}"));
        assert_eq!(seamed.fetch_target(), url);
        assert_eq!(
            seamed.asset(),
            request.asset(),
            "the feed asset is unchanged"
        );
        assert_eq!(seamed.name(), request.name());
    }
    for (label, url) in [
        (
            "non-loopback host",
            format!("http://10.0.0.1:{port}/{file}"),
        ),
        ("https", format!("https://127.0.0.1:{port}/{file}")),
        ("localhost", format!("http://localhost:{port}/{file}")),
        ("no port", format!("http://127.0.0.1/{file}")),
        ("the default port", format!("http://127.0.0.1:80/{file}")),
        (
            "another last segment",
            format!("http://127.0.0.1:{port}/other.bin"),
        ),
        (
            "the asset not last",
            format!("http://127.0.0.1:{port}/{file}/x"),
        ),
        ("not a URL", "not a url".to_owned()),
    ] {
        assert!(
            request.clone().with_fixture_url(&url).is_err(),
            "{label}: {url} accepted"
        );
    }
}

#[test]
fn request_for_cases() {
    let v1 = feed::parse(V1_ONLY).expect("v1-only.json parses");
    for name in v1.plugins.keys() {
        match request_for(&v1, name, None) {
            Err(DownloadError::Unavailable { why, .. }) => {
                assert_eq!(why, Unavailability::NotThroughManager, "{name}")
            }
            other => panic!("{name}: {other:?}"),
        }
    }
    let mixed = feed::parse(MIXED).expect("mixed.json parses");
    match request_for(&mixed, "Delta", None) {
        Err(DownloadError::Unavailable { name, why }) => {
            assert_eq!(
                (name.as_str(), why),
                ("Delta", Unavailability::NotThroughManager)
            )
        }
        other => panic!("Delta: {other:?}"),
    }
    let alpha = &mixed.plugins["Alpha"];
    let platforms = &alpha
        .release
        .as_ref()
        .expect("Alpha has a release")
        .platforms;
    match (
        request_for(&mixed, "Alpha", None),
        platforms.get(&native_key()),
    ) {
        (Ok(request), Some(asset)) => {
            assert_eq!(request.name(), "Alpha");
            assert_eq!(request.version_text(), alpha.version_text);
            assert_eq!(request.key(), native_key());
            assert_eq!(request.asset(), asset);
        }
        (Err(DownloadError::Unavailable { why, .. }), None) => {
            assert_eq!(why, Unavailability::NotForThisMachine)
        }
        (other, asset) => panic!("Alpha: {other:?} with native asset {asset:?}"),
    }
    match request_for(&mixed, "Zulu", None) {
        Err(DownloadError::UnknownPlugin { name }) => assert_eq!(name, "Zulu"),
        other => panic!("Zulu: {other:?}"),
    }

    let signer = TestSigner::new();
    let asset = Asset::new("Alpha", 3, 1000);
    let native = entry(&asset, &signer);
    let foreign = Entry {
        name: "Bravo",
        key: other_key(),
        ..entry(&Asset::new("Bravo", 4, 1000), &signer)
    };
    let newer = Entry {
        name: "Charlie",
        min_os: Some("99.0"),
        ..entry(&Asset::new("Charlie", 5, 1000), &signer)
    };
    let feed = feed_of(&[native, foreign, newer]);
    let host = FakeHostFacts {
        os_version: OsVersion::parse("15.0"),
        ..FakeHostFacts::default()
    };
    let running = host.os_version();
    let request = request_for(&feed, "Alpha", running.as_ref()).expect("a native entry");
    let offered = &feed.plugins["Alpha"]
        .release
        .as_ref()
        .expect("v2")
        .platforms[&native_key()];
    assert_eq!(
        (request.name(), request.version_text(), request.key()),
        ("Alpha", VERSION, native_key())
    );
    assert_eq!(request.asset(), offered);
    assert_eq!(
        request.running, running,
        "the request keeps `running` (PD-3)"
    );
    match request_for(&feed, "Bravo", running.as_ref()) {
        Err(DownloadError::Unavailable { why, .. }) => {
            assert_eq!(why, Unavailability::NotForThisMachine)
        }
        other => panic!("a non-native key: {other:?}"),
    }
    match request_for(&feed, "Charlie", running.as_ref()) {
        Err(DownloadError::Unavailable {
            why: Unavailability::NeedsNewerOs { min_os },
            ..
        }) => assert_eq!(Some(min_os), OsVersion::parse("99.0")),
        other => panic!("a min_os above the running OS: {other:?}"),
    }
}

/// Phase 9 PD-1 (tasks TA-6): the asset is resolved for the given OS and architecture; `request_for` is the native
/// pair.
#[test]
fn request_for_platform_cases() {
    let signer = TestSigner::new();
    let linux = Entry {
        key: PlatformKey::LinuxX86_64,
        ..entry(&Asset::new("Alpha", 6, 1000), &signer)
    };
    let feed = feed_of(&[linux]);
    let running = OsVersion::parse("2.99");
    let request = request_for_platform(&feed, "Alpha", Os::Linux, Arch::X86_64, running.as_ref())
        .expect("the Linux asset");
    assert_eq!(request.key(), PlatformKey::LinuxX86_64);
    match request_for_platform(&feed, "Alpha", Os::Windows, Arch::X86_64, running.as_ref()) {
        Err(e) => assert_eq!(e.code(), "E_DOWNLOAD_UNAVAILABLE", "{e}"),
        Ok(r) => panic!("no Windows asset: {r:?}"),
    }
    match request_for_platform(&feed, "Zulu", Os::Linux, Arch::X86_64, running.as_ref()) {
        Err(e) => assert_eq!(e.code(), "E_DOWNLOAD_UNKNOWN_PLUGIN", "{e}"),
        Ok(r) => panic!("an unknown name: {r:?}"),
    }

    let native = feed_of(&[entry(&Asset::new("Alpha", 7, 1000), &signer)]);
    let delegated = request_for(&native, "Alpha", running.as_ref()).expect("the native asset");
    let explicit = request_for_platform(
        &native,
        "Alpha",
        NativePlatform.os(),
        Arch::native(),
        running.as_ref(),
    )
    .expect("the native pair");
    assert_eq!(
        (delegated.key(), delegated.asset()),
        (explicit.key(), explicit.asset())
    );
}

// ---- SC-002, SC-005: a good artifact ------------------------------------------------------------------------------

#[test]
fn good_artifact_verifies() {
    let fx = Fx::new(Asset::ranged);
    let mut events = Vec::new();
    let nexts = Cell::new(0);
    let result = obtain_and(
        fx.rig.env(),
        &fx.feed,
        &fx.request,
        &mut |e| events.push(e),
        |a| {
            nexts.set(nexts.get() + 1);
            assert!(
                fs::metadata(a.path()).is_ok_and(|m| m.is_file()),
                "the verified file exists inside next"
            );
            assert!(!exists(&fx.part()), "no .part inside next");
            a
        },
    );
    assert_progress_invariants(&events, fx.size(), result.is_ok());
    let obtained = match result {
        Ok(o) => o,
        Err(e) => panic!("{e}"),
    };
    assert_eq!(nexts.get(), 1);
    assert_eq!(
        (obtained.resumed, obtained.cached, obtained.transferred),
        (false, false, SIZE as u64)
    );
    let artifact = &obtained.value;
    assert_eq!(artifact.size(), SIZE as u64);
    assert_eq!(*artifact.sha256(), sha256_of(&fx.asset.bytes));
    assert_eq!(artifact.name(), "Alpha");
    assert_eq!(artifact.version(), VERSION);
    assert_eq!(artifact.platform(), native_key());
    assert_eq!(artifact.asset().as_str(), fx.asset.file);
    assert_eq!(artifact.path(), fx.verified());
    assert_eq!(fs::read(artifact.path()).expect("read"), fx.asset.bytes);
    assert!(!exists(&fx.part()));
    let requests = fx.requests();
    assert_eq!(requests.len(), 1, "{requests:?}");
    assert_eq!(header(&requests[0], "Accept-Encoding"), Some("identity"));
    assert_eq!(header(&requests[0], "Accept"), Some(OCTET));
    assert_eq!(
        header(&requests[0], "User-Agent"),
        Some(concat!("krate-manager/", env!("CARGO_PKG_VERSION")))
    );
    assert_eq!(range_of(&requests[0]), None);
}

#[test]
fn progress_is_monotonic_and_ends_at_size() {
    let fx = Fx::new(Asset::ranged);
    let run = fx.run();
    run.ok();
    let size = SIZE as u64;
    assert_eq!(
        run.events.first(),
        Some(&Progress::Started {
            total: size,
            resumed_from: 0
        })
    );
    let n = run.events.len();
    assert_eq!(
        &run.events[n - 2..],
        &[Progress::Verifying, Progress::Verified]
    );
    let bytes = bytes_events(&run.events);
    assert_eq!(
        bytes.len(),
        n - 3,
        "only Bytes between Started and Verifying: {:?}",
        run.events
    );
    assert!(bytes.len() >= 4, "{bytes:?}");
    let mut previous = 0;
    for (i, &(received, on_disk)) in bytes.iter().enumerate() {
        assert_eq!(received, on_disk);
        assert!(received > previous);
        if i + 1 < bytes.len() {
            assert!(received - previous >= PROGRESS_STEP, "step {i}: {bytes:?}");
        }
        previous = received;
    }
    assert_eq!(bytes.last(), Some(&(size, size)));
    assert!(restarts(&run.events).is_empty());
    assert!(retryings(&run.events).is_empty());
}

// ---- SC-003, SC-031: rejections -----------------------------------------------------------------------------------

/// What a rejection row asserts beyond the code.
enum Check {
    Plain,
    NothingOpened,
    WrittenAtMostSize,
    KeyIds {
        expected: String,
        found: String,
    },
    Binding {
        field: &'static str,
        signed: String,
        expected: String,
    },
}

struct Row {
    label: &'static str,
    code: &'static str,
    fx: Fx,
    check: Check,
}

/// SC-003's twelve rows (shared with SC-031), each in its own temp root with its own feed and server.
fn rejection_rows() -> Vec<Row> {
    let alpha = Asset::alpha();
    let flipped = {
        let mut b = alpha.bytes.clone();
        b[SIZE / 2] ^= 0x5A;
        b
    };
    let serve = |bytes: &[u8]| {
        let body = bytes.to_vec();
        move |_: &Asset| Reply::Ranged { body }
    };
    let signed = |signer: &TestSigner, name: &str, version: &str, key: PlatformKey, file: &str| {
        signer.sign_c3(&alpha.bytes, name, version, key, file)
    };
    let native = native_key();
    let mut rows = Vec::new();

    rows.push(Row {
        label: "a: one byte flipped, the feed hash of the original",
        code: "E_VERIFY_SHA256",
        fx: Fx::build(alpha.clone(), TestSigner::new(), |_| {}, serve(&flipped)),
        check: Check::Plain,
    });
    rows.push(Row {
        label: "b: one byte flipped, the feed hash of the flipped bytes",
        code: "E_VERIFY_SIGNATURE",
        fx: Fx::build(
            alpha.clone(),
            TestSigner::new(),
            |e| e.sha256 = sha256_of(&flipped),
            serve(&flipped),
        ),
        check: Check::Plain,
    });
    rows.push(Row {
        label: "c: the feed size one more than served",
        code: "E_DOWNLOAD_SIZE",
        fx: Fx::build(
            alpha.clone(),
            TestSigner::new(),
            |e| e.size += 1,
            Asset::ranged,
        ),
        check: Check::NothingOpened,
    });
    let close_delimited = [
        b"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n"
            .as_slice(),
        &alpha.bytes,
        &[0x42],
    ]
    .concat();
    rows.push(Row {
        label: "d: a close-delimited body one byte longer than size",
        code: "E_DOWNLOAD_SIZE",
        fx: Fx::build(
            alpha.clone(),
            TestSigner::new(),
            |_| {},
            |_| Reply::Garbage(close_delimited),
        ),
        check: Check::WrittenAtMostSize,
    });
    rows.push(Row {
        label: "e: the feed hash of other bytes",
        code: "E_VERIFY_SHA256",
        fx: Fx::build(
            alpha.clone(),
            TestSigner::new(),
            |e| e.sha256 = sha256_of(&seeded(99, SIZE)),
            Asset::ranged,
        ),
        check: Check::Plain,
    });
    {
        let ctx_signer = TestSigner::new();
        let other = TestSigner::new();
        let check = Check::KeyIds {
            expected: ctx_signer.key().key_id_hex(),
            found: other.key().key_id_hex(),
        };
        let signature = signed(&other, "Alpha", VERSION, native, &alpha.file);
        rows.push(Row {
            label: "f: signed by another key",
            code: "E_VERIFY_KEY_ID",
            fx: Fx::build(
                alpha.clone(),
                ctx_signer,
                |e| e.signature = signature,
                Asset::ranged,
            ),
            check,
        });
    }
    {
        let ctx_signer = TestSigner::new();
        let other = TestSigner::new();
        let signature = with_key_id(
            &signed(&other, "Alpha", VERSION, native, &alpha.file),
            ctx_signer.key_id(),
        );
        rows.push(Row {
            label: "g: another key carrying the context key's id",
            code: "E_VERIFY_SIGNATURE",
            fx: Fx::build(
                alpha.clone(),
                ctx_signer,
                |e| e.signature = signature,
                Asset::ranged,
            ),
            check: Check::Plain,
        });
    }
    let other_file = format!("Alpha-{VERSION}-other.bin");
    let bindings = [
        (
            "h: bound to plugin Bravo",
            "name",
            "Bravo",
            VERSION,
            native,
            &alpha.file,
            "Bravo".to_owned(),
            "Alpha".to_owned(),
        ),
        (
            "i: bound to version 1.2.2",
            "version",
            "Alpha",
            "1.2.2",
            native,
            &alpha.file,
            "1.2.2".to_owned(),
            VERSION.to_owned(),
        ),
        (
            "j: bound to another platform key",
            "platform",
            "Alpha",
            VERSION,
            other_key(),
            &alpha.file,
            other_key().as_str().to_owned(),
            native.as_str().to_owned(),
        ),
        (
            "k: bound to another asset",
            "asset",
            "Alpha",
            VERSION,
            native,
            &other_file,
            other_file.clone(),
            alpha.file.clone(),
        ),
    ];
    for (label, field, name, version, key, file, signed_value, expected) in bindings {
        let signer = TestSigner::new();
        let signature = signed(&signer, name, version, key, file);
        rows.push(Row {
            label,
            code: "E_TC_BINDING",
            fx: Fx::build(
                alpha.clone(),
                signer,
                |e| e.signature = signature,
                Asset::ranged,
            ),
            check: Check::Binding {
                field,
                signed: signed_value,
                expected,
            },
        });
    }
    {
        let signer = TestSigner::new();
        let good = signed(&signer, "Alpha", VERSION, native, &alpha.file);
        let mut lines: Vec<String> = good.split('\n').map(str::to_owned).collect();
        assert!(
            lines[2].starts_with("trusted comment: "),
            "line 3: {:?}",
            lines[2]
        );
        lines[2] = format!(
            "trusted comment: {}",
            c3_comment("Alpha", "1.2.4", native, &alpha.file)
        );
        let edited = lines.join("\n");
        rows.push(Row {
            label: "m: the trusted comment replaced after signing",
            code: "E_VERIFY_SIGNATURE",
            fx: Fx::build(
                alpha.clone(),
                signer,
                |e| e.signature = edited,
                Asset::ranged,
            ),
            check: Check::Plain,
        });
    }
    rows
}

#[test]
fn rejections_never_reach_next_stage() {
    let rows = rejection_rows();
    assert_eq!(rows.len(), 12);
    for row in rows {
        let label = row.label;
        let fx = &row.fx;
        let extract = Cell::new(0);
        let install = Cell::new(0);
        let mut events = Vec::new();
        let result = obtain_and(
            fx.rig.env(),
            &fx.feed,
            &fx.request,
            &mut |e| events.push(e),
            |a| {
                if a.format() == AssetFormat::TarGz {
                    extract.set(extract.get() + 1);
                } else {
                    install.set(install.get() + 1);
                }
            },
        );
        assert_progress_invariants(&events, fx.size(), result.is_ok());
        let Err(error) = result else {
            panic!("{label}: verified")
        };
        assert_eq!(error.code(), row.code, "{label}: {error}");
        assert_eq!((extract.get(), install.get()), (0, 0), "{label}");
        assert!(!exists(&fx.verified()), "{label}: a verified file");
        assert_eq!(
            FsStagingIo.len_of(&fx.part()).ok(),
            Some(None),
            "{label}: a .part left"
        );
        assert!(!events.contains(&Progress::Verified), "{label}");
        match row.check {
            Check::Plain => {}
            Check::NothingOpened => {
                assert_eq!(fx.rig.io.written(), 0, "{label}");
                assert_eq!(
                    fx.rig.io.calls_of(Op::Open) + fx.rig.io.calls_of(Op::Truncate),
                    0,
                    "{label}: a .part opened"
                );
            }
            Check::WrittenAtMostSize => {
                assert!(
                    fx.rig.io.written() <= fx.size(),
                    "{label}: {}",
                    fx.rig.io.written()
                );
            }
            Check::KeyIds { expected, found } => match &error {
                DownloadError::Verify {
                    error:
                        VerifyError::KeyId {
                            expected: e,
                            found: f,
                        },
                    ..
                } => {
                    assert_eq!((e, f), (&expected, &found), "{label}");
                    assert_ne!(e, f, "{label}");
                    for id in [e, f] {
                        assert!(
                            id.len() == 16
                                && id
                                    .bytes()
                                    .all(|b| b.is_ascii_digit() || (b'A'..=b'F').contains(&b)),
                            "{label}: {id}"
                        );
                    }
                }
                other => panic!("{label}: {other}"),
            },
            Check::Binding {
                field,
                signed,
                expected,
            } => match &error {
                DownloadError::Verify {
                    error:
                        VerifyError::Binding {
                            field: f,
                            signed: s,
                            expected: x,
                        },
                    ..
                } => assert_eq!((*f, s, x), (field, &signed, &expected), "{label}"),
                other => panic!("{label}: {other}"),
            },
        }
    }
}

#[test]
fn writes_nothing_outside_downloads() {
    let good = Fx::new(Asset::ranged);
    let mut cases = vec![("SC-002", good)];
    cases.extend(rejection_rows().into_iter().map(|row| (row.label, row.fx)));
    for (label, fx) in cases {
        let base = fx.base();
        let inside = fx
            .downloads()
            .strip_prefix(&base)
            .expect("downloads under base")
            .to_path_buf();
        let before = tree(&base);
        let _ = fx.run();
        let after = tree(&base);
        assert_changes_only_under(label, &before, &after, &inside);
    }
}

#[cfg(target_os = "macos")]
#[test]
fn verified_file_has_no_quarantine_macos() {
    let fx = Fx::new(Asset::ranged);
    let run = fx.run();
    let path = run.ok().value.path().to_path_buf();
    let out = std::process::Command::new("/usr/bin/xattr")
        .arg("-l")
        .arg(&path)
        .output()
        .expect("run xattr");
    assert!(out.status.success(), "xattr -l failed: {out:?}");
    let listing = String::from_utf8_lossy(&out.stdout);
    assert!(
        !listing
            .lines()
            .any(|l| l.starts_with("com.apple.quarantine")),
        "quarantine on the verified file: {listing}"
    );
}

#[cfg(windows)]
#[test]
fn verified_file_has_no_zone_identifier_windows() {
    let fx = Fx::new(Asset::ranged);
    let run = fx.run();
    let path = run.ok().value.path().to_path_buf();
    let stream = PathBuf::from(format!("{}:Zone.Identifier", path.display()));
    match fs::File::open(&stream) {
        Err(e) => assert_eq!(e.kind(), io::ErrorKind::NotFound, "{e}"),
        Ok(_) => panic!("{} exists", stream.display()),
    }
}

// ---- SC-006, SC-007, SC-008: resume and range edge cases -----------------------------------------------------------

#[test]
fn interrupted_download_resumes() {
    let fx = Fx::new(|a| {
        Reply::Script(vec![
            Reply::Truncated {
                declared_len: a.bytes.len(),
                sent: a.bytes[..400_000].to_vec(),
            },
            a.ranged(),
        ])
    });
    let start = Instant::now();
    let run = fx.run();
    assert!(
        start.elapsed() < Duration::from_secs(5),
        "{:?}",
        start.elapsed()
    );
    let obtained = run.ok();
    assert!(obtained.resumed);
    assert_eq!(
        obtained.transferred, SIZE as u64,
        "the server sent size body bytes in total"
    );
    assert_eq!(
        fs::read(obtained.value.path()).expect("read"),
        fx.asset.bytes
    );
    let requests = fx.requests();
    assert_eq!(requests.len(), 2, "{requests:?}");
    assert_eq!(range_of(&requests[0]), None);
    assert_eq!(range_of(&requests[1]), Some("bytes=400000-"));
    assert!(restarts(&run.events).is_empty(), "{:?}", run.events);
    assert_eq!(
        retryings(&run.events),
        vec![(1, 4, "E_DOWNLOAD_BODY".to_owned(), 0)]
    );
    let at = run
        .events
        .iter()
        .position(|e| matches!(e, Progress::Retrying { .. }))
        .expect("a Retrying");
    assert!(
        run.events[..at]
            .iter()
            .any(|e| matches!(e, Progress::Bytes { .. }))
    );
    assert!(
        run.events[at..]
            .iter()
            .any(|e| matches!(e, Progress::Bytes { .. }))
    );
}

#[test]
fn partial_from_a_previous_run_resumes() {
    let fx = Fx::new(Asset::ranged);
    fx.seed_part(&fx.asset.bytes[..400_000]);
    let start = Instant::now();
    let run = fx.run();
    assert!(
        start.elapsed() < Duration::from_secs(5),
        "{:?}",
        start.elapsed()
    );
    let obtained = run.ok();
    assert_eq!(obtained.transferred, SIZE as u64 - 400_000);
    assert!(obtained.resumed);
    assert_eq!(
        fs::read(obtained.value.path()).expect("read"),
        fx.asset.bytes
    );
    let requests = fx.requests();
    assert_eq!(range_of(&requests[0]), Some("bytes=400000-"));
    assert_eq!(
        run.events[0],
        Progress::Started {
            total: SIZE as u64,
            resumed_from: 400_000
        }
    );
    let bytes = bytes_events(&run.events);
    assert!(bytes[0].1 > 400_000, "{bytes:?}");
    assert!(bytes.iter().all(|(r, d)| r == d), "{bytes:?}");
    assert_eq!(bytes.last(), Some(&(SIZE as u64, SIZE as u64)));
}

#[test]
fn stall_resumes() {
    let mut fx = Fx::new(|a| {
        Reply::Script(vec![
            Reply::HeadersThenStall {
                declared_len: a.bytes.len(),
                sent: a.bytes[..300_000].to_vec(),
                stall: Duration::from_secs(5),
            },
            a.ranged(),
        ])
    });
    fx.rig.policy = DownloadPolicy {
        stall: Duration::from_millis(200),
        ..test_policy()
    };
    let start = Instant::now();
    let run = fx.run();
    assert!(
        start.elapsed() < Duration::from_secs(5),
        "{:?}",
        start.elapsed()
    );
    assert_eq!(
        fs::read(run.ok().value.path()).expect("read"),
        fx.asset.bytes
    );
    let retried = retryings(&run.events);
    assert_eq!(
        retried.first().map(|r| r.2.as_str()),
        Some("E_DOWNLOAD_STALLED"),
        "{retried:?}"
    );
    let requests = fx.requests();
    assert!(
        requests.len() >= 2 && requests.len() <= fx.rig.policy.attempts as usize,
        "{requests:?}"
    );
    assert_eq!(range_of(&requests[1]), Some("bytes=300000-"));
}

#[test]
fn ignored_range_falls_back_to_full() {
    let fx = Fx::new(|a| {
        Reply::Script(vec![
            Reply::Truncated {
                declared_len: a.bytes.len(),
                sent: a.bytes[..400_000].to_vec(),
            },
            Reply::Ok {
                body: a.bytes.clone(),
                content_type: OCTET,
            },
        ])
    });
    let run = fx.run();
    let obtained = run.ok();
    assert_eq!(obtained.transferred, 400_000 + SIZE as u64);
    assert_eq!(
        fs::read(obtained.value.path()).expect("read"),
        fx.asset.bytes,
        "no duplicated prefix"
    );
    assert_eq!(retryings(&run.events).len(), 1);
    assert_eq!(restarts(&run.events), vec![RestartReason::RangeIgnored]);
    let requests = fx.requests();
    assert_eq!(requests.len(), 2);
    assert_eq!(range_of(&requests[1]), Some("bytes=400000-"));
    let at = run
        .events
        .iter()
        .position(|e| matches!(e, Progress::Restarted { .. }))
        .expect("a Restarted");
    let after = bytes_events(&run.events[at..]);
    assert!(after[0].1 <= PROGRESS_STEP, "{after:?}");
    assert_eq!(after.last(), Some(&(SIZE as u64, SIZE as u64)));
}

#[test]
fn truncate_failure_after_ignored_range() {
    let mut fx = Fx::new(|a| {
        Reply::Script(vec![
            Reply::Truncated {
                declared_len: a.bytes.len(),
                sent: a.bytes[..400_000].to_vec(),
            },
            Reply::Ok {
                body: a.bytes.clone(),
                content_type: OCTET,
            },
        ])
    });
    fx.rig.io = FaultyStagingIo::failing(Op::Truncate, 1, io::ErrorKind::StorageFull);
    let run = fx.run();
    assert_eq!(staging_of(run.err()), ("E_STAGING_FULL", "truncate"));
    assert!(!exists(&fx.part()), "the .part is removed");
    assert_eq!(run.nexts, 0);
    assert!(!exists(&fx.verified()));
}

#[test]
fn range_edge_cases() {
    let size = SIZE as u64;
    // (a) 416 to the ranged request: one full request.
    {
        let fx = Fx::new(|a| Reply::Script(vec![Reply::Status(416), a.ranged()]));
        fx.seed_part(&fx.asset.bytes[..400_000]);
        let run = fx.run();
        assert_eq!(
            fs::read(run.ok().value.path()).expect("read"),
            fx.asset.bytes,
            "a"
        );
        assert_eq!(
            restarts(&run.events),
            vec![RestartReason::RangeRefused],
            "a"
        );
        let requests = fx.requests();
        assert_eq!(requests.len(), 2, "a");
        assert_eq!(range_of(&requests[0]), Some("bytes=400000-"), "a");
        assert_eq!(range_of(&requests[1]), None, "a");
    }
    // (b) a 206 with the wrong range: no byte of it written, then a full request.
    {
        let fx = Fx::new(|a| {
            let n = a.bytes.len();
            let wrong = format!(
                "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-{}/{n}\r\nContent-Length: {n}\r\nConnection: close\r\n\r\n",
                n - 1
            );
            Reply::Script(vec![
                Reply::Truncated {
                    declared_len: n,
                    sent: a.bytes[..400_000].to_vec(),
                },
                Reply::Garbage([wrong.as_bytes(), &a.bytes].concat()),
                a.ranged(),
            ])
        });
        let run = fx.run();
        assert_eq!(
            fs::read(run.ok().value.path()).expect("read"),
            fx.asset.bytes,
            "b"
        );
        assert_eq!(
            restarts(&run.events),
            vec![RestartReason::RangeRefused],
            "b"
        );
        assert_eq!(
            fx.rig.io.written(),
            400_000 + size,
            "b: no byte of the 206 written"
        );
        let requests = fx.requests();
        assert_eq!(requests.len(), 3, "b");
        assert_eq!(range_of(&requests[1]), Some("bytes=400000-"), "b");
        assert_eq!(range_of(&requests[2]), None, "b");
    }
    // (c) a complete correct .part: verification only.
    {
        let fx = Fx::new(Asset::ranged);
        fx.seed_part(&fx.asset.bytes);
        let run = fx.run();
        assert_eq!(run.ok().value.path(), fx.verified(), "c");
        assert!(fx.requests().is_empty(), "c");
        assert!(!exists(&fx.part()), "c");
        assert_eq!(
            run.events,
            vec![
                Progress::Started {
                    total: size,
                    resumed_from: size
                },
                Progress::Verifying,
                Progress::Verified
            ],
            "c"
        );
    }
    // (d) a .part longer than size: discarded.
    {
        let fx = Fx::new(Asset::ranged);
        fx.seed_part(&[fx.asset.bytes.as_slice(), &[0u8; 10]].concat());
        let run = fx.run();
        assert_eq!(
            fs::read(run.ok().value.path()).expect("read"),
            fx.asset.bytes,
            "d"
        );
        assert_eq!(
            restarts(&run.events),
            vec![RestartReason::PartialTooLong],
            "d"
        );
        let requests = fx.requests();
        assert_eq!(requests.len(), 1, "d");
        assert_eq!(range_of(&requests[0]), None, "d");
    }
    // (e) a wrong prefix: one full re-download after the hash mismatch.
    {
        let fx = Fx::new(Asset::ranged);
        fx.seed_part(&seeded(77, 400_000));
        let run = fx.run();
        assert_eq!(
            fs::read(run.ok().value.path()).expect("read"),
            fx.asset.bytes,
            "e"
        );
        assert_eq!(
            restarts(&run.events),
            vec![RestartReason::HashMismatchAfterResume],
            "e"
        );
        let requests = fx.requests();
        assert_eq!(requests.len(), 2, "e");
        assert_eq!(range_of(&requests[0]), Some("bytes=400000-"), "e");
        assert_eq!(range_of(&requests[1]), None, "e");
    }
    // (f) as (e), and the full request serves wrong bytes too: final.
    {
        let fx = Fx::new(|a| {
            Reply::Script(vec![
                a.ranged(),
                Reply::Ok {
                    body: seeded(78, a.bytes.len()),
                    content_type: OCTET,
                },
            ])
        });
        fx.seed_part(&seeded(77, 400_000));
        let run = fx.run();
        assert_eq!(run.err().code(), "E_VERIFY_SHA256", "f");
        assert_eq!(fx.requests().len(), 2, "f");
        assert_eq!(run.nexts, 0, "f");
        assert!(!exists(&fx.verified()), "f");
    }
}

// ---- SC-009: retries -----------------------------------------------------------------------------------------------

/// A recorded run whose progress closure also notes the server's request count at each `Retrying`.
fn run_noting_requests(fx: &Fx) -> (Recorded<VerifiedArtifact>, Vec<usize>) {
    let mut events = Vec::new();
    let mut seen = Vec::new();
    let result = obtain_and(
        fx.rig.env(),
        &fx.feed,
        &fx.request,
        &mut |e| {
            if matches!(e, Progress::Retrying { .. }) {
                seen.push(fx.requests().len());
            }
            events.push(e);
        },
        |a| a,
    );
    assert_progress_invariants(&events, fx.size(), result.is_ok());
    (
        Recorded {
            result,
            events,
            nexts: 0,
        },
        seen,
    )
}

fn retried(code: &str, waits: &[u64]) -> Vec<(u32, u32, String, u64)> {
    (1u32..)
        .zip(waits)
        .map(|(attempt, wait)| (attempt, 4, code.to_owned(), *wait))
        .collect()
}

#[test]
fn retries_are_bounded() {
    // (a) Close on every request.
    {
        let fx = Fx::new(|_| Reply::Close);
        let (run, seen) = run_noting_requests(&fx);
        assert_eq!(run.err().code(), "E_DOWNLOAD_OFFLINE", "a");
        assert_eq!(fx.requests().len(), 4, "a");
        assert_eq!(
            retryings(&run.events),
            retried("E_DOWNLOAD_OFFLINE", &[0, 0, 0]),
            "a"
        );
        assert_eq!(
            seen,
            vec![1, 2, 3],
            "a: each Retrying before the next request"
        );
    }
    // (a) with backoff [50 ms]: three waits.
    {
        let mut fx = Fx::new(|_| Reply::Close);
        fx.rig.policy = DownloadPolicy {
            backoff: vec![Duration::from_millis(50)],
            ..test_policy()
        };
        let start = Instant::now();
        let (run, _) = run_noting_requests(&fx);
        assert!(
            start.elapsed() >= Duration::from_millis(150),
            "{:?}",
            start.elapsed()
        );
        assert_eq!(run.err().code(), "E_DOWNLOAD_OFFLINE");
        assert_eq!(
            retryings(&run.events),
            retried("E_DOWNLOAD_OFFLINE", &[50, 50, 50])
        );
    }
    // (b) two 503s, then the asset.
    {
        let fx =
            Fx::new(|a| Reply::Script(vec![Reply::Status(503), Reply::Status(503), a.ranged()]));
        let (run, seen) = run_noting_requests(&fx);
        assert_eq!(
            fs::read(run.ok().value.path()).expect("read"),
            fx.asset.bytes,
            "b"
        );
        assert_eq!(fx.requests().len(), 3, "b");
        assert_eq!(
            retryings(&run.events),
            retried("E_DOWNLOAD_HTTP_STATUS", &[0, 0]),
            "b"
        );
        assert_eq!(seen, vec![1, 2], "b");
    }
    // (c) 404: not retried.
    {
        let fx = Fx::new(|_| Reply::Status(404));
        let (run, _) = run_noting_requests(&fx);
        match run.err() {
            DownloadError::HttpStatus { status, .. } => assert_eq!(*status, 404, "c"),
            other => panic!("c: {other}"),
        }
        assert_eq!(fx.requests().len(), 1, "c");
        assert!(retryings(&run.events).is_empty(), "c");
    }
    // (d) 429 four times.
    {
        let fx = Fx::new(|_| Reply::Status(429));
        let (run, seen) = run_noting_requests(&fx);
        assert_eq!(run.err().code(), "E_DOWNLOAD_HTTP_STATUS", "d");
        assert_eq!(fx.requests().len(), 4, "d");
        assert_eq!(
            retryings(&run.events),
            retried("E_DOWNLOAD_HTTP_STATUS", &[0, 0, 0]),
            "d"
        );
        assert_eq!(seen, vec![1, 2, 3], "d");
    }
    // (e1) a coding ureq leaves visible.
    {
        let fx = Fx::new(|a| {
            let h = format!(
                "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Encoding: br\r\nConnection: close\r\n\r\n",
                a.bytes.len()
            );
            Reply::Garbage([h.as_bytes(), &a.bytes].concat())
        });
        let (run, _) = run_noting_requests(&fx);
        match run.err() {
            DownloadError::Encoding { encoding, .. } => assert_eq!(encoding, "br", "e1"),
            other => panic!("e1: {other}"),
        }
        assert_eq!(fx.requests().len(), 1, "e1");
        assert_eq!(fx.rig.io.written(), 0, "e1");
        assert!(!exists(&fx.part()), "e1");
        assert!(retryings(&run.events).is_empty(), "e1");
    }
    // (e2) gzip of the signed bytes: ureq decodes it, the bytes verify.
    {
        let fx = Fx::new(|a| Reply::Gzip {
            body: gzip(&a.bytes),
        });
        let (run, _) = run_noting_requests(&fx);
        assert_eq!(
            fs::read(run.ok().value.path()).expect("read"),
            fx.asset.bytes,
            "e2"
        );
        assert_eq!(fx.requests().len(), 1, "e2");
        assert!(retryings(&run.events).is_empty(), "e2");
    }
    // (e3) gzip of other bytes of the same length: a single-response hash mismatch is final.
    {
        let fx = Fx::new(|a| Reply::Gzip {
            body: gzip(&seeded(5, a.bytes.len())),
        });
        let (run, _) = run_noting_requests(&fx);
        assert_eq!(run.err().code(), "E_VERIFY_SHA256", "e3");
        assert_eq!(fx.requests().len(), 1, "e3");
        assert!(!exists(&fx.part()), "e3");
        assert!(!exists(&fx.verified()), "e3");
        assert!(retryings(&run.events).is_empty(), "e3");
    }
    // (e4) gzip of size + 1 bytes: the body limit.
    {
        let fx = Fx::new(|a| Reply::Gzip {
            body: gzip(&seeded(6, a.bytes.len() + 1)),
        });
        let (run, _) = run_noting_requests(&fx);
        assert_eq!(run.err().code(), "E_DOWNLOAD_SIZE", "e4");
        assert_eq!(fx.requests().len(), 1, "e4");
        assert!(fx.rig.io.written() <= fx.size(), "e4");
        assert!(!exists(&fx.part()), "e4");
        assert!(retryings(&run.events).is_empty(), "e4");
    }
}

/// `HeadersThenStall` of 100,000 bytes on every request (SC-009 f / g, SC-037).
fn stalling(a: &Asset) -> Reply {
    Reply::HeadersThenStall {
        declared_len: a.bytes.len(),
        sent: a.bytes[..100_000].to_vec(),
        stall: Duration::from_secs(5),
    }
}

#[test]
fn stall_and_deadline_are_named() {
    // (f) the per-read stall bound.
    {
        let mut fx = Fx::new(stalling);
        fx.rig.policy = DownloadPolicy {
            stall: Duration::from_millis(100),
            attempt_deadline: Duration::from_secs(2),
            ..test_policy()
        };
        let start = Instant::now();
        let run = fx.run();
        assert!(
            start.elapsed() < Duration::from_secs(3),
            "f: {:?}",
            start.elapsed()
        );
        assert_eq!(run.err().code(), "E_DOWNLOAD_STALLED", "f");
        assert_eq!(fx.requests().len(), 4, "f");
    }
    // (g) the attempt deadline, long before the stall bound.
    {
        let mut fx = Fx::new(stalling);
        fx.rig.policy = DownloadPolicy {
            stall: Duration::from_secs(2),
            attempt_deadline: Duration::from_millis(300),
            ..test_policy()
        };
        let start = Instant::now();
        let run = fx.run();
        assert!(
            start.elapsed() < Duration::from_secs(3),
            "g: {:?}",
            start.elapsed()
        );
        assert_eq!(run.err().code(), "E_DOWNLOAD_TIMEOUT", "g");
        assert_eq!(fx.requests().len(), 4, "g");
    }
}

#[test]
fn non_retryable_transport_errors() {
    let assert_row = |label: &str, fx: &Fx, run: &Recorded<VerifiedArtifact>, code: &str| {
        let error = run.err();
        assert_eq!(error.code(), code, "{label}: {error}");
        let on_asset = fx
            .requests()
            .iter()
            .filter(|r| r.path == fx.asset.route())
            .count();
        assert_eq!(on_asset, 1, "{label}: asset-route requests");
        assert!(retryings(&run.events).is_empty(), "{label}");
        assert!(!exists(&fx.part()), "{label}");
        assert_eq!(run.nexts, 0, "{label}");
        let text = error.to_string();
        assert!(
            text.contains(fx.request.asset().url.as_str()),
            "{label}: {text}"
        );
    };
    // (1) a redirect the loopback policy refuses.
    {
        let fx = Fx::new(|_| Reply::Redirect {
            status: 302,
            location: "https://example.invalid/x".to_owned(),
        });
        let run = fx.run();
        assert_row("1", &fx, &run, "E_DOWNLOAD_URL");
        assert!(
            !run.err().to_string().contains("example.invalid/x"),
            "1: no redirect target"
        );
    }
    // (2) one redirect more than MAX_REDIRECTS.
    {
        let fx = Fx::new(|_| Reply::Redirect {
            status: 302,
            location: "/r1".to_owned(),
        });
        for i in 1..=6 {
            fx.server.set_route(
                &format!("/r{i}"),
                Reply::Redirect {
                    status: 302,
                    location: format!("/r{}", i + 1),
                },
            );
        }
        let run = fx.run();
        assert_row("2", &fx, &run, "E_DOWNLOAD_REDIRECT");
        let requests = fx.requests();
        for i in 1..=6 {
            let route = format!("/r{i}");
            let n = requests.iter().filter(|r| r.path == route).count();
            // asset → r1 … r5 → r6 is the sixth redirect: refused before r6 is requested.
            assert_eq!(n, usize::from(i <= 5), "2: {route}");
        }
    }
    // (3) a redirect to https on the plain-HTTP fixture server: a TLS failure.
    {
        let fx = Fx::new(|_| Reply::Status(500));
        let tls = fx.server.url("/x").replacen("http", "https", 1);
        fx.server.set_route(
            &fx.asset.route(),
            Reply::Redirect {
                status: 302,
                location: tls,
            },
        );
        let run = fx.run();
        assert_row("3", &fx, &run, "E_DOWNLOAD_TLS");
    }
}

// ---- SC-011 to SC-014: staging I/O, prune, exclusion ---------------------------------------------------------------

#[test]
fn staging_errors_are_named() {
    use io::ErrorKind::{PermissionDenied, QuotaExceeded, StorageFull};
    let rows = [
        (Op::Write, 3, StorageFull, "E_STAGING_FULL", "write"),
        (Op::Sync, 1, StorageFull, "E_STAGING_FULL", "sync"),
        (Op::Rename, 1, StorageFull, "E_STAGING_FULL", "rename"),
        (Op::Write, 1, QuotaExceeded, "E_STAGING_FULL", "write"),
        (
            Op::CreateDir,
            1,
            PermissionDenied,
            "E_STAGING_DENIED",
            "create_dir",
        ),
        (Op::Open, 1, PermissionDenied, "E_STAGING_DENIED", "open"),
    ];
    for (op, nth, kind, code, step) in rows {
        let label = format!("{op:?} #{nth} {kind:?}");
        let mut fx = Fx::new(Asset::ranged);
        fx.rig.io = FaultyStagingIo::failing(op, nth, kind);
        let run = fx.run();
        assert_eq!(staging_of(run.err()), (code, step), "{label}");
        assert_eq!(run.nexts, 0, "{label}");
        assert!(!exists(&fx.verified()), "{label}");
        if op == Op::Rename {
            assert_eq!(
                fs::metadata(fx.part()).map(|m| m.len()).ok(),
                Some(SIZE as u64),
                "{label}: kept"
            );
            let before = fx.requests().len();
            let again = fx.run_env(DownloadEnv {
                io: &FsStagingIo,
                ..fx.rig.env()
            });
            assert_eq!(
                fs::read(again.ok().value.path()).expect("read"),
                fx.asset.bytes,
                "{label}"
            );
            assert_eq!(fx.requests().len(), before, "{label}: zero requests");
        } else {
            assert!(!exists(&fx.part()), "{label}: the .part is gone");
        }
    }
}

#[test]
fn crash_after_each_step_recovers() {
    let ignored_range = |a: &Asset| {
        Reply::Script(vec![
            Reply::Truncated {
                declared_len: a.bytes.len(),
                sent: a.bytes[..400_000].to_vec(),
            },
            Reply::Ok {
                body: a.bytes.clone(),
                content_type: OCTET,
            },
        ])
    };
    for (label, ignored) in [("ranged", false), ("ignored range", true)] {
        let reply = |a: &Asset| {
            if ignored {
                ignored_range(a)
            } else {
                a.ranged()
            }
        };
        let clean = Fx::new(reply);
        clean.run().ok();
        let n = clean.rig.io.calls();
        assert!(n > 5, "{label}: {n} calls");
        for k in 1..=n {
            let mut fx = Fx::new(reply);
            fx.rig.io = FaultyStagingIo::panicking_after(k);
            let crashed = catch_unwind(AssertUnwindSafe(|| {
                obtain_and(fx.rig.env(), &fx.feed, &fx.request, &mut |_| {}, |a| a).is_ok()
            }));
            if crashed.is_ok() {
                assert!(
                    fx.rig.io.calls() < k,
                    "{label} k={k}: no panic after call {k}"
                );
            }
            if exists(&fx.verified()) {
                assert!(
                    verify_file(&fx.verified(), &fx.expected(), &fx.rig.ctx.signing_key).is_ok(),
                    "{label} k={k}: an unverified file at the verified path"
                );
            }
            fx.server.set_route(&fx.asset.route(), fx.asset.ranged());
            let rerun = fx.run_env(DownloadEnv {
                io: &FsStagingIo,
                ..fx.rig.env()
            });
            match &rerun.result {
                Ok(o) => assert_eq!(
                    fs::read(o.value.path()).expect("read"),
                    fx.asset.bytes,
                    "{label} k={k}"
                ),
                Err(e) => panic!("{label} k={k}: {e}"),
            }
        }
    }
}

#[cfg(unix)]
#[test]
fn readonly_staging_dir_unix() {
    use std::os::unix::fs::{MetadataExt, PermissionsExt};
    let fx = Fx::new(Asset::ranged);
    let probe = fx.base().join("probe");
    fs::write(&probe, b"probe").expect("write the probe");
    if fs::metadata(&probe).expect("probe metadata").uid() == 0 {
        panic!("cannot prove a permission error as root");
    }
    let downloads = fx.downloads();
    fs::create_dir_all(&downloads).expect("create downloads");
    fs::set_permissions(&downloads, fs::Permissions::from_mode(0o500)).expect("chmod 0500");
    let run = fx.run();
    fs::set_permissions(&downloads, fs::Permissions::from_mode(0o700)).expect("chmod 0700");
    assert_eq!(staging_of(run.err()), ("E_STAGING_DENIED", "open"));
    assert_eq!(fx.rig.io.written(), 0);
    assert_eq!(run.nexts, 0);
}

#[cfg(windows)]
#[test]
fn prune_skips_a_file_open_without_share_delete_windows() {
    use std::os::windows::fs::OpenOptionsExt;
    install_capture();
    let fx = Fx::new(Asset::ranged);
    fs::create_dir_all(fx.downloads()).expect("create downloads");
    let stale = fx
        .downloads()
        .join(format!("{}.part", hex_lower(&[7u8; 32])));
    fs::write(&stale, b"stale").expect("write the stale .part");
    let _ = take_mine();
    // FILE_SHARE_READ (core has no windows-sys): no delete sharing.
    let handle = fs::OpenOptions::new()
        .read(true)
        .share_mode(0x1)
        .open(&stale)
        .expect("open without delete sharing");
    fx.run().ok();
    assert!(exists(&stale), "the held file stays");
    let warns: Vec<String> = take_mine()
        .into_iter()
        .filter(|(target, level, _)| target == LOG_TARGET && *level == Level::Warn)
        .map(|(_, _, message)| message)
        .collect();
    assert_eq!(warns.len(), 1, "{warns:?}");
    assert!(
        warns[0].contains("E_STAGING_IO") && warns[0].contains("remove"),
        "{warns:?}"
    );
    drop(handle);
    fx.run().ok();
    assert!(!exists(&stale), "removed once the handle is gone");
}

/// The relative paths under `dir`, recursively.
fn listing(dir: &Path) -> BTreeSet<PathBuf> {
    tree(dir).into_keys().collect()
}

#[test]
fn prune_keeps_only_live_artifacts() {
    let many = Many::new(vec![
        Asset::new("Alpha", 21, SIZE),
        Asset::new("Bravo", 22, 70_001),
    ]);
    let downloads = many.downloads();
    let dead = hex_lower(&[9u8; 32]);
    let (alpha, bravo) = (&many.assets[0], &many.assets[1]);
    fs::create_dir_all(downloads.join(&dead)).expect("dead dir");
    fs::write(downloads.join(format!("{dead}.part")), b"dead").expect("dead part");
    fs::write(downloads.join(&dead).join("x.bin"), b"dead").expect("dead file");
    fs::write(downloads.join("notes.txt"), b"notes").expect("notes");
    fs::create_dir_all(downloads.join(format!("{}.part", alpha.hex())))
        .expect("a .part of the wrong kind");
    fs::create_dir_all(downloads.join(bravo.hex())).expect("live dir");
    fs::write(downloads.join(bravo.hex()).join(&bravo.file), &bravo.bytes).expect("live file");
    many.obtain(0).ok();
    let expected: BTreeSet<PathBuf> = [
        PathBuf::from(alpha.hex()),
        Path::new(&alpha.hex()).join(&alpha.file),
        PathBuf::from(bravo.hex()),
        Path::new(&bravo.hex()).join(&bravo.file),
    ]
    .into_iter()
    .collect();
    assert_eq!(listing(&downloads), expected);
}

#[test]
fn same_artifact_twice_is_busy() {
    {
        let fx = &Fx::new(Asset::ranged);
        let (ready_tx, ready_rx) = mpsc::channel::<()>();
        let (go_tx, go_rx) = mpsc::channel::<()>();
        thread::scope(|s| {
            let a = s.spawn(move || {
                let mut events = Vec::new();
                let result = obtain_and(
                    fx.rig.env(),
                    &fx.feed,
                    &fx.request,
                    &mut |e| {
                        if matches!(e, Progress::Started { .. }) {
                            ready_tx.send(()).expect("ready");
                            go_rx.recv().expect("go");
                        }
                        events.push(e);
                    },
                    |a| a,
                );
                assert_progress_invariants(&events, fx.size(), result.is_ok());
                result.is_ok()
            });
            ready_rx.recv_timeout(WAIT).expect("A is in flight");
            let b = fx.run();
            match b.err() {
                DownloadError::Busy { .. } => {}
                other => panic!("B: {other}"),
            }
            assert!(b.events.is_empty(), "B emitted {:?}", b.events);
            assert!(fx.requests().is_empty(), "B made a request");
            go_tx.send(()).expect("go");
            assert!(a.join().expect("A joins"), "A ends Ok");
        });
        assert_eq!(fx.requests().len(), 1);
    }
    // Two different artifacts held at Started together, then both released.
    {
        let many = &Many::new(vec![
            Asset::new("Alpha", 31, SIZE),
            Asset::new("Bravo", 32, 300_001),
        ]);
        thread::scope(|s| {
            let mut held = Vec::new();
            for i in 0..2 {
                let (ready_tx, ready_rx) = mpsc::channel::<()>();
                let (go_tx, go_rx) = mpsc::channel::<()>();
                let handle = s.spawn(move || {
                    let request = many.request(i);
                    let result = obtain_and(
                        many.rig.env(),
                        &many.feed,
                        &request,
                        &mut |e| {
                            if matches!(e, Progress::Started { .. }) {
                                ready_tx.send(()).expect("ready");
                                go_rx.recv().expect("go");
                            }
                        },
                        |a| a,
                    );
                    result.is_ok()
                });
                held.push((ready_rx, go_tx, handle));
            }
            for (ready_rx, _, _) in &held {
                ready_rx.recv_timeout(WAIT).expect("both in flight");
            }
            for (_, go_tx, handle) in held {
                go_tx.send(()).expect("go");
                assert!(handle.join().expect("joins"), "both end Ok");
            }
        });
    }
}

#[test]
fn busy_registry_is_released_on_error() {
    let fx = Fx::new(|_| Reply::Status(404));
    assert_eq!(fx.run().err().code(), "E_DOWNLOAD_HTTP_STATUS");
    fx.server.set_route(&fx.asset.route(), fx.asset.ranged());
    assert_eq!(
        fs::read(fx.run().ok().value.path()).expect("read"),
        fx.asset.bytes
    );
}

#[test]
fn prune_spares_in_flight_artifact() {
    let signer = TestSigner::new();
    let x = Asset::new("Alpha", 41, SIZE);
    let y = Asset::new("Bravo", 42, 200_001);
    let (_td, roots) = fresh_temp_roots();
    let f1 = feed_of(&[entry(&x, &signer)]);
    let f2 = feed_of(&[entry(&y, &signer)]);
    let server = FixtureServer::start(vec![(x.route(), x.ranged()), (y.route(), y.ranged())]);
    let rig = DownloadRig::new(context(roots, signer.key(), String::new()));
    let seam = |feed: &Feed, asset: &Asset| {
        request_for(feed, asset.name, None)
            .expect("offered")
            .with_fixture_url(&server.url(&asset.route()))
            .expect("a fixture URL")
    };
    let rx = seam(&f1, &x);
    let ry = seam(&f2, &y);
    let downloads = downloads_of(&rig.ctx);
    let x_part = downloads.join(format!("{}.part", x.hex()));
    let (rig, f1, f2, rx, ry, x_bytes) = (&rig, &f1, &f2, &rx, &ry, &x.bytes);
    let (ready_tx, ready_rx) = mpsc::channel::<u64>();
    let (go_tx, go_rx) = mpsc::channel::<()>();
    thread::scope(|s| {
        let a = s.spawn(move || {
            let mut held = false;
            let mut events = Vec::new();
            let result = obtain_and(
                rig.env(),
                f1,
                rx,
                &mut |e| {
                    if let Progress::Bytes { on_disk, .. } = e
                        && !held
                    {
                        held = true;
                        ready_tx.send(on_disk).expect("ready");
                        go_rx.recv().expect("go");
                    }
                    events.push(e);
                },
                |a| a,
            );
            assert_progress_invariants(&events, rx.asset().size, result.is_ok());
            match result {
                Ok(o) => fs::read(o.value.path()).expect("read") == *x_bytes,
                Err(e) => panic!("A: {e}"),
            }
        });
        let on_disk = ready_rx
            .recv_timeout(WAIT)
            .expect("A holds at its first Bytes");
        record(rig.env(), f2, ry, |a| a).ok();
        let len = fs::metadata(&x_part)
            .map(|m| m.len())
            .expect("A's .part survives the prune");
        assert!(len >= on_disk, "{len} < {on_disk}");
        go_tx.send(()).expect("go");
        assert!(
            a.join().expect("A joins"),
            "A's file equals the served bytes"
        );
    });
    // Control: once A is out of flight, a prune under F2 removes X's directory.
    let before = server.requests().len();
    let third = record(rig.env(), f2, ry, |a| a);
    assert!(third.ok().cached, "the cached path");
    assert_eq!(server.requests().len(), before, "no request");
    assert!(!exists(&downloads.join(x.hex())), "X's directory removed");
}

// ---- SC-026 to SC-030 ----------------------------------------------------------------------------------------------

#[test]
fn two_artifacts_reach_one_consumer() {
    let many = Many::new(vec![
        Asset::new("Alpha", 51, SIZE),
        Asset::new("Bravo", 52, 500_001),
    ]);
    let alpha = many
        .obtain(0)
        .result
        .unwrap_or_else(|e| panic!("Alpha: {e}"))
        .value;
    let bravo = many
        .obtain(1)
        .result
        .unwrap_or_else(|e| panic!("Bravo: {e}"))
        .value;
    assert_eq!(
        fs::read(alpha.path()).expect("Alpha survives"),
        many.assets[0].bytes
    );
    let downloads = many.downloads();
    let calls = Cell::new(0);
    let batch = |items: &[VerifiedArtifact]| -> usize {
        calls.set(calls.get() + 1);
        for item in items {
            let hex = hex_lower(item.sha256());
            assert_eq!(
                item.path(),
                downloads.join(&hex).join(item.asset().as_str())
            );
            assert_eq!(
                sha256_of(&fs::read(item.path()).expect("read")),
                *item.sha256()
            );
        }
        items.len()
    };
    assert_eq!(batch(&[alpha, bravo]), 2);
    assert_eq!(calls.get(), 1);
}

#[test]
fn channel_adapter_forwards_in_order() {
    let plain = Fx::new(Asset::ranged).run();
    plain.ok();
    let expected = plain.events;
    assert_eq!(expected.last(), Some(&Progress::Verified));

    let forwarded = RefCell::new(Vec::new());
    let fx = Fx::new(Asset::ranged);
    let result = obtain_and(
        fx.rig.env(),
        &fx.feed,
        &fx.request,
        &mut channel_progress(|e| -> Result<(), ()> {
            forwarded.borrow_mut().push(e);
            Ok(())
        }),
        |a| a,
    );
    assert!(result.is_ok());
    assert_eq!(*forwarded.borrow(), expected);

    let calls = Cell::new(0);
    let fx = Fx::new(Asset::ranged);
    let result = obtain_and(
        fx.rig.env(),
        &fx.feed,
        &fx.request,
        &mut channel_progress(|_| -> Result<(), ()> {
            calls.set(calls.get() + 1);
            Err(())
        }),
        |a| a,
    );
    assert!(result.is_ok(), "a failing send never aborts the download");
    assert_eq!(calls.get(), expected.len());

    let delivered = RefCell::new(Vec::new());
    let n = Cell::new(0);
    let fx = Fx::new(Asset::ranged);
    let result = obtain_and(
        fx.rig.env(),
        &fx.feed,
        &fx.request,
        &mut channel_progress(|e| -> Result<(), ()> {
            n.set(n.get() + 1);
            if n.get() == 2 {
                return Err(());
            }
            delivered.borrow_mut().push(e);
            Ok(())
        }),
        |a| a,
    );
    assert!(result.is_ok());
    let mut later = expected.clone();
    later.remove(1);
    assert_eq!(*delivered.borrow(), later, "every later event delivered");
}

/// A rig whose feed URL is the server's `/versions.json`, the server answering `feed` there and `assets` at theirs.
fn command_rig(
    feed: Reply,
    assets: &[&Asset],
    key: SigningKey,
) -> (tempfile::TempDir, FixtureServer, DownloadRig) {
    let (td, roots) = fresh_temp_roots();
    let mut routes = vec![(FEED_ROUTE.to_owned(), feed)];
    routes.extend(assets.iter().map(|a| (a.route(), a.ranged())));
    let server = FixtureServer::start(routes);
    let rig = DownloadRig::new(context(roots, key, server.url(FEED_ROUTE)));
    (td, server, rig)
}

fn json(bytes: Vec<u8>) -> Reply {
    Reply::Ok {
        body: bytes,
        content_type: "application/json",
    }
}

fn paths_of(server: &FixtureServer) -> Vec<String> {
    server.requests().into_iter().map(|r| r.path).collect()
}

#[test]
fn command_flow_cases() {
    let host = || FakeHostFacts {
        os_version: OsVersion::parse("99.0"),
        ..FakeHostFacts::default()
    };
    let signer = TestSigner::new();
    let alpha = Asset::new("Alpha", 61, 300_001);
    // Built once: every signing call makes a new signature text.
    let document = doc_of(&[entry(&alpha, &signer)]);
    let doc = || document.clone();
    let no_events =
        |label: &str, events: &[Progress]| assert!(events.is_empty(), "{label}: {events:?}");

    // (a) no feed.
    {
        let (_td, server, rig) = command_rig(Reply::Status(404), &[&alpha], signer.key());
        let mut events = Vec::new();
        let result = for_command(rig.env(), "Alpha", VERSION, NOW, &host(), &mut |e| {
            events.push(e)
        });
        match result {
            Err(DownloadError::NoFeed { cause, .. }) => {
                assert_eq!(cause, "E_FEED_HTTP_STATUS", "a")
            }
            other => panic!("a: {other:?}"),
        }
        assert_eq!(paths_of(&server), vec![FEED_ROUTE], "a");
        no_events("a", &events);
    }
    // (b) not a feed key.
    {
        let (_td, server, rig) = command_rig(json(doc()), &[&alpha], signer.key());
        let facts = host();
        let mut events = Vec::new();
        match for_command(rig.env(), "Zulu", VERSION, NOW, &facts, &mut |e| {
            events.push(e)
        }) {
            Err(DownloadError::UnknownPlugin { name }) => assert_eq!(name, "Zulu", "b"),
            other => panic!("b: {other:?}"),
        }
        assert_eq!(*facts.calls.borrow(), vec!["os_version".to_owned()], "b");
        assert_eq!(paths_of(&server), vec![FEED_ROUTE], "b");
        no_events("b", &events);
    }
    // (c) a v1-only entry, mixed.json read in place.
    {
        let (_td, _server, rig) = command_rig(json(MIXED.to_vec()), &[], signer.key());
        let mut events = Vec::new();
        match for_command(rig.env(), "Delta", "3.0.0", NOW, &host(), &mut |e| {
            events.push(e)
        }) {
            Err(DownloadError::Unavailable { why, .. }) => {
                assert_eq!(why, Unavailability::NotThroughManager, "c")
            }
            other => panic!("c: {other:?}"),
        }
        no_events("c", &events);
    }
    // (e) the feed moved on: no prune, no staging I/O, no event, no asset request.
    {
        let (_td, server, rig) = command_rig(json(doc()), &[&alpha], signer.key());
        let downloads = downloads_of(&rig.ctx);
        let stale = downloads.join(format!("{}.part", hex_lower(&[3u8; 32])));
        let live = downloads.join(alpha.hex()).join(&alpha.file);
        fs::create_dir_all(live.parent().expect("parent")).expect("live dir");
        fs::write(&stale, b"stale").expect("stale");
        fs::write(&live, b"not yet verified").expect("live");
        let mut events = Vec::new();
        match for_command(rig.env(), "Alpha", "1.2.2", NOW, &host(), &mut |e| {
            events.push(e)
        }) {
            Err(DownloadError::VersionChanged {
                name,
                expected,
                found,
            }) => assert_eq!(
                (name.as_str(), expected.as_str(), found.as_str()),
                ("Alpha", "1.2.2", VERSION),
                "e"
            ),
            other => panic!("e: {other:?}"),
        }
        match for_command(rig.env(), "Zulu", "0.0.1", NOW, &host(), &mut |e| {
            events.push(e)
        }) {
            Err(e) => assert_eq!(
                e.code(),
                "E_DOWNLOAD_UNKNOWN_PLUGIN",
                "e: the name is decided first"
            ),
            Ok(s) => panic!("e: {s:?}"),
        }
        assert!(
            paths_of(&server).iter().all(|p| p == FEED_ROUTE),
            "e: an asset request"
        );
        assert!(exists(&stale) && exists(&live), "e: no prune ran");
        no_events("e", &events);
    }
    // (d) the good path, split: command_request then run_command over the seam.
    {
        let (_td, server, rig) = command_rig(json(doc()), &[&alpha], signer.key());
        let (feed, request) = command_request(&rig.ctx, "Alpha", VERSION, NOW, &host())
            .unwrap_or_else(|e| panic!("d: {e}"));
        let running = OsVersion::parse("99.0");
        assert_eq!(feed, feed::parse(&doc()).expect("parses"), "d: the feed");
        assert_eq!(
            request,
            request_for(&feed, "Alpha", running.as_ref()).expect("offered"),
            "d"
        );
        let seamed = request
            .with_fixture_url(&server.url(&alpha.route()))
            .expect("a fixture URL");
        let mut events = Vec::new();
        let summary = run_command(rig.env(), &feed, &seamed, &mut |e| events.push(e))
            .unwrap_or_else(|e| panic!("d: {e}"));
        assert_progress_invariants(&events, alpha.bytes.len() as u64, true);
        assert_eq!(
            summary,
            DownloadSummary {
                name: "Alpha".to_owned(),
                version: VERSION.to_owned(),
                platform: native_key().as_str().to_owned(),
                asset: alpha.file.clone(),
                size: alpha.bytes.len() as u64,
                resumed: false,
            }
        );
        let value = serde_json::to_value(&summary).expect("serialises");
        let object = value.as_object().expect("an object");
        let keys: BTreeSet<&str> = object.keys().map(String::as_str).collect();
        assert_eq!(
            keys,
            BTreeSet::from(["name", "version", "platform", "asset", "size", "resumed"])
        );
        for (key, v) in object {
            if let Some(text) = v.as_str() {
                assert!(
                    !text.contains('/') && !text.contains('\\') && !text.contains("://"),
                    "d: {key} = {text}"
                );
                let longest_hex = text
                    .split(|c: char| !c.is_ascii_hexdigit())
                    .map(str::len)
                    .max()
                    .unwrap_or(0);
                assert!(longest_hex < 64, "d: {key} holds a hash");
            }
        }
    }
    // (f) for_command reaches run_command with the env it was given: the feed URL is refused by the loopback policy.
    {
        let (_td, server, rig) = command_rig(json(doc()), &[&alpha], signer.key());
        let downloads = downloads_of(&rig.ctx);
        fs::create_dir_all(&downloads).expect("downloads");
        let stale = downloads.join(format!("{}.part", hex_lower(&[4u8; 32])));
        fs::write(&stale, b"stale").expect("stale");
        let mut events = Vec::new();
        match for_command(rig.env(), "Alpha", VERSION, NOW, &host(), &mut |e| {
            events.push(e)
        }) {
            Err(e) => assert_eq!(e.code(), "E_DOWNLOAD_URL", "f: {e}"),
            Ok(s) => panic!("f: {s:?}"),
        }
        assert_eq!(
            paths_of(&server),
            vec![FEED_ROUTE],
            "f: the feed request only"
        );
        let started = events
            .iter()
            .filter(|e| matches!(e, Progress::Started { .. }))
            .count();
        assert_eq!(started, 1, "f: {events:?}");
        assert!(retryings(&events).is_empty(), "f");
        assert!(!exists(&stale), "f: the prune ran");
    }
}

#[test]
fn cached_verified_file_is_reverified() {
    let size = SIZE as u64;
    // (a) the second run re-verifies with no request.
    {
        let fx = Fx::new(Asset::ranged);
        fx.run().ok();
        let before = fx.requests().len();
        let run = fx.run();
        let obtained = run.ok();
        assert_eq!(
            (obtained.resumed, obtained.cached, obtained.transferred),
            (false, true, 0),
            "a"
        );
        assert_eq!(obtained.value.path(), fx.verified(), "a");
        assert_eq!(run.nexts, 1, "a");
        assert_eq!(fx.requests().len(), before, "a: no request");
        assert_eq!(
            run.events,
            vec![
                Progress::Started {
                    total: size,
                    resumed_from: size
                },
                Progress::Verifying,
                Progress::Verified
            ],
            "a"
        );
    }
    // (b) a flipped byte: the directory is removed and one full request made.
    {
        let fx = Fx::new(Asset::ranged);
        fx.run().ok();
        let mut tampered = fs::read(fx.verified()).expect("read");
        tampered[1234] ^= 0x01;
        fs::write(fx.verified(), &tampered).expect("tamper");
        let before = fx.requests().len();
        let sha = sha256_of(&fx.asset.bytes);
        let run = record(fx.rig.env(), &fx.feed, &fx.request, |a| {
            assert_eq!(
                sha256_of(&fs::read(a.path()).expect("read")),
                sha,
                "b: next sees the served bytes"
            );
        });
        assert!(run.result.is_ok(), "b");
        assert_eq!(run.nexts, 1, "b");
        let requests = fx.requests();
        assert_eq!(requests.len(), before + 1, "b");
        assert_eq!(range_of(&requests[before]), None, "b");
        assert!(
            restarts(&run.events).contains(&RestartReason::CachedInvalid),
            "b: {:?}",
            run.events
        );
        assert_eq!(
            &run.events[..3],
            &[
                Progress::Started {
                    total: size,
                    resumed_from: size
                },
                Progress::Verifying,
                Progress::Restarted {
                    reason: RestartReason::CachedInvalid
                }
            ]
        );
    }
}

#[test]
fn redirect_target_never_logged() {
    // The socket-less scrub row (PD-15).
    let scrubbed = scrub("io: /cdn/a.bin?token=SECRET-TOKEN failed at https://x.invalid/y");
    for banned in ["SECRET-TOKEN", "/cdn/", "://"] {
        assert!(!scrubbed.contains(banned), "{scrubbed}");
    }
    assert_eq!(
        scrub("connection reset (os error 10054)"),
        "connection reset (os error 10054)"
    );

    install_capture();
    let redirect = |_: &Asset| Reply::Redirect {
        status: 302,
        location: CDN.to_owned(),
    };
    let alpha = Asset::alpha();
    let id_signer = TestSigner::new();
    let id = id_signer.key_id();
    let foreign = with_key_id(
        &TestSigner::new().sign_c3(&alpha.bytes, "Alpha", VERSION, native_key(), &alpha.file),
        id,
    );
    let cases: Vec<(&str, Fx, Reply)> = vec![
        ("a", Fx::new(redirect), alpha.ranged()),
        ("b", Fx::new(redirect), Reply::Status(500)),
        (
            "c",
            Fx::build(
                alpha.clone(),
                id_signer,
                |e| e.signature = foreign,
                redirect,
            ),
            alpha.ranged(),
        ),
        (
            "d",
            Fx::new(redirect),
            Reply::Truncated {
                declared_len: SIZE,
                sent: alpha.bytes[..1000].to_vec(),
            },
        ),
    ];
    for (label, fx, cdn) in cases {
        fx.server.set_route(CDN, cdn);
        let _ = take_mine();
        let run = fx.run();
        let records = take_mine();
        let feed_url = fx.request.asset().url.as_str();
        for (_, _, message) in &records {
            assert!(
                !message.contains("SECRET-TOKEN") && !message.contains("/cdn/"),
                "{label}: {message}"
            );
        }
        let mine: Vec<(Level, &str)> = records
            .iter()
            .filter(|(target, _, _)| target == LOG_TARGET)
            .map(|(_, level, message)| (*level, message.as_str()))
            .collect();
        let infos: Vec<&str> = mine
            .iter()
            .filter(|(l, _)| *l == Level::Info)
            .map(|(_, m)| *m)
            .collect();
        let warns: Vec<&str> = mine
            .iter()
            .filter(|(l, _)| *l == Level::Warn)
            .map(|(_, m)| *m)
            .collect();
        let on_cdn = fx.requests().iter().filter(|r| r.path == CDN).count();
        if label == "a" {
            run.ok();
            assert_eq!((infos.len(), warns.len()), (1, 0), "a: {mine:?}");
            let line = infos[0];
            for part in [
                "Alpha",
                VERSION,
                native_key().as_str(),
                &SIZE.to_string(),
                "resumed=false",
                "cached=false",
            ] {
                assert!(line.contains(part), "a: {part} not in {line}");
            }
            continue;
        }
        let error = run.err();
        let text = error.to_string();
        assert!(
            !text.contains("SECRET-TOKEN") && !text.contains("/cdn/"),
            "{label}: {text}"
        );
        assert!(text.contains(feed_url), "{label}: {text}");
        assert_eq!((infos.len(), warns.len()), (0, 1), "{label}: {mine:?}");
        assert!(warns[0].contains(feed_url), "{label}: {}", warns[0]);
        match label {
            "b" => {
                assert_eq!(error.code(), "E_DOWNLOAD_HTTP_STATUS", "b");
                assert_eq!(on_cdn, 4, "b");
            }
            "c" => assert_eq!(error.code(), "E_VERIFY_SIGNATURE", "c"),
            _ => {
                assert_eq!(error.code(), "E_DOWNLOAD_BODY", "d");
                assert_eq!(on_cdn, 4, "d");
            }
        }
    }
}

// ---- SC-031 / FR-050: links in staging ----------------------------------------------------------------------------

#[cfg(unix)]
#[test]
fn staged_symlinks_are_not_followed() {
    use std::os::unix::fs::symlink;
    let outside_with = |fx: &Fx| {
        let outside = fx.base().join("outside");
        fs::create_dir_all(outside.join("d")).expect("outside/d");
        fs::write(outside.join("f"), seeded(8, 2 * SIZE)).expect("outside/f");
        fs::write(outside.join("d").join("keep.txt"), b"keep").expect("outside/d/keep.txt");
        outside
    };
    // (1) a .part link and a <hex> link: both removed, the download verified, nothing outside touched.
    {
        let fx = Fx::new(Asset::ranged);
        let outside = outside_with(&fx);
        fs::create_dir_all(fx.downloads()).expect("downloads");
        symlink(outside.join("f"), fx.part()).expect("link the .part");
        symlink(outside.join("d"), fx.dir()).expect("link the <hex>");
        let snapshot = tree(&outside);
        let run = fx.run();
        let path = run.ok().value.path().to_path_buf();
        let requests = fx.requests();
        assert_eq!(requests.len(), 1, "1");
        assert_eq!(
            range_of(&requests[0]),
            None,
            "1: the link was not a partial"
        );
        assert_eq!(tree(&outside), snapshot, "1: outside unchanged");
        assert!(!exists(&fx.part()), "1: the .part link is gone");
        let dir = fs::symlink_metadata(fx.dir()).expect("<hex>");
        assert!(
            dir.is_dir() && !dir.file_type().is_symlink(),
            "1: <hex> is a real directory"
        );
        assert!(
            fs::symlink_metadata(&path).expect("artifact").is_file(),
            "1"
        );
        let canonical = fs::canonicalize(fx.downloads()).expect("canonical downloads");
        assert!(
            fs::canonicalize(&path)
                .expect("canonical artifact")
                .starts_with(canonical),
            "1"
        );
    }
    // (2) downloads itself a link.
    {
        let fx = Fx::new(Asset::ranged);
        let dl = fx.base().join("outside").join("dl");
        fs::create_dir_all(&dl).expect("outside/dl");
        symlink(&dl, fx.downloads()).expect("link downloads");
        let run = fx.run();
        assert_eq!(staging_of(run.err()), ("E_STAGING_IO", "create_dir"), "2");
        assert!(
            fs::read_dir(&dl).expect("read outside/dl").next().is_none(),
            "2: outside/dl empty"
        );
        assert!(fx.requests().is_empty(), "2");
    }
    // (3) socket-less: verify_file refuses a final-component link to a correct file.
    {
        let fx = Fx::new(Asset::ranged);
        let real = fx.base().join("real.bin");
        fs::write(&real, &fx.asset.bytes).expect("real");
        let link = fx.base().join("link.bin");
        symlink(&real, &link).expect("link");
        match verify_file(&link, &fx.expected(), &fx.rig.ctx.signing_key) {
            Err(VerifyError::Io { kind }) => assert_eq!(kind, io::ErrorKind::InvalidInput, "3"),
            other => panic!("3: {other:?}"),
        }
    }
    // (4) a <hex> link the prune cannot remove: step 4 refuses before measuring anything under it.
    {
        let mut fx = Fx::new(Asset::ranged);
        let outside = outside_with(&fx);
        fs::write(outside.join("d").join(&fx.asset.file), &fx.asset.bytes)
            .expect("correct bytes outside");
        fs::create_dir_all(fx.downloads()).expect("downloads");
        symlink(outside.join("d"), fx.dir()).expect("link the <hex>");
        fx.rig.io = FaultyStagingIo::failing(Op::RemoveFile, 1, io::ErrorKind::PermissionDenied);
        let snapshot = tree(&outside);
        let run = fx.run();
        assert_eq!(staging_of(run.err()), ("E_STAGING_IO", "open"), "4");
        assert_eq!(
            fx.rig.io.calls_of(Op::LenOf),
            0,
            "4: nothing measured under <hex>"
        );
        assert!(fx.requests().is_empty(), "4");
        assert_eq!(run.nexts, 0, "4");
        assert!(run.events.is_empty(), "4: {:?}", run.events);
        assert_eq!(tree(&outside), snapshot, "4: outside unchanged");
    }
}

#[cfg(windows)]
#[test]
fn staged_links_are_not_followed_windows() {
    // mklink takes plain drive paths, not the verbatim form the canonical temp base has.
    let plain = |p: &Path| {
        let s = p.to_str().expect("UTF-8");
        s.strip_prefix(r"\\?\").unwrap_or(s).to_owned()
    };
    let junction = |link: &Path, target: &Path| {
        let made = std::process::Command::new("cmd")
            .args(["/C", "mklink", "/J"])
            .arg(plain(link))
            .arg(plain(target))
            .output()
            .expect("run cmd");
        assert!(made.status.success(), "mklink /J failed: {made:?}");
    };
    // (1) junctions at <hex>.part and <hex>: both removed, the download verified, nothing outside touched.
    {
        let fx = Fx::new(Asset::ranged);
        let outside = fx.base().join("outside");
        fs::create_dir_all(outside.join("p")).expect("outside/p");
        fs::write(outside.join("p").join("keep.txt"), b"keep").expect("outside/p/keep.txt");
        fs::create_dir_all(outside.join("d")).expect("outside/d");
        fs::write(outside.join("d").join(&fx.asset.file), &fx.asset.bytes)
            .expect("outside/d/<asset>");
        fs::create_dir_all(fx.downloads()).expect("downloads");
        junction(&fx.part(), &outside.join("p"));
        junction(&fx.dir(), &outside.join("d"));
        let snapshot = tree(&outside);
        let run = fx.run();
        let path = match &run.result {
            Ok(o) => o.value.path().to_path_buf(),
            Err(e) => {
                for link in [fx.part(), fx.dir()] {
                    let _ = fs::remove_dir(link);
                }
                panic!("1: {e}");
            }
        };
        let requests = fx.requests();
        assert_eq!(requests.len(), 1, "1");
        assert_eq!(
            range_of(&requests[0]),
            None,
            "1: the junction was not a partial"
        );
        assert_eq!(tree(&outside), snapshot, "1: outside unchanged");
        assert!(!exists(&fx.part()), "1: the .part junction is gone");
        let dir = fs::symlink_metadata(fx.dir()).expect("<hex>");
        assert!(
            dir.is_dir() && !dir.file_type().is_symlink(),
            "1: <hex> is a real directory"
        );
        assert!(
            fs::symlink_metadata(&path).expect("artifact").is_file(),
            "1"
        );
        let canonical = fs::canonicalize(fx.downloads()).expect("canonical downloads");
        assert!(
            fs::canonicalize(&path)
                .expect("canonical artifact")
                .starts_with(canonical),
            "1"
        );
    }
    // (2) a <hex> junction the prune cannot remove: step 4 refuses before measuring anything under it.
    {
        let mut fx = Fx::new(Asset::ranged);
        let outside = fx.base().join("outside");
        fs::create_dir_all(outside.join("d")).expect("outside/d");
        let kept = outside.join("d").join(&fx.asset.file);
        fs::write(&kept, &fx.asset.bytes).expect("outside/d/<asset>");
        fs::create_dir_all(fx.downloads()).expect("downloads");
        junction(&fx.dir(), &outside.join("d"));
        fx.rig.io = FaultyStagingIo::failing(Op::RemoveDir, 1, io::ErrorKind::PermissionDenied);
        let run = fx.run();
        let _ = fs::remove_dir(fx.dir());
        assert_eq!(staging_of(run.err()), ("E_STAGING_IO", "open"), "2");
        assert_eq!(
            fx.rig.io.calls_of(Op::LenOf),
            0,
            "2: nothing measured under <hex>"
        );
        assert!(fx.requests().is_empty(), "2");
        assert_eq!(run.nexts, 0, "2");
        assert!(run.events.is_empty(), "2: {:?}", run.events);
        assert_eq!(
            fs::read(&kept).expect("outside/d/<asset> present"),
            fx.asset.bytes,
            "2"
        );
    }
    // (3) downloads itself a junction.
    {
        let fx = Fx::new(Asset::ranged);
        let dl = fx.base().join("outside").join("dl");
        fs::create_dir_all(&dl).expect("outside/dl");
        junction(&fx.downloads(), &dl);
        let run = fx.run();
        let _ = fs::remove_dir(fx.downloads());
        assert_eq!(staging_of(run.err()), ("E_STAGING_IO", "create_dir"), "3");
        assert!(
            fs::read_dir(&dl).expect("read outside/dl").next().is_none(),
            "3: outside/dl empty"
        );
        assert!(fx.requests().is_empty(), "3");
    }
}

// ---- SC-034 to SC-037 ----------------------------------------------------------------------------------------------

#[test]
fn cancellation_cases() {
    let size = SIZE as u64;
    // (a) set before the call.
    {
        let fx = Fx::new(Asset::ranged);
        fx.rig.cancel.cancel();
        let run = fx.run();
        assert_eq!(run.err().code(), "E_DOWNLOAD_CANCELLED", "a");
        assert!(fx.requests().is_empty(), "a");
        assert!(!exists(&fx.part()), "a");
        assert!(run.events.is_empty(), "a: {:?}", run.events);
        assert_eq!(run.nexts, 0, "a");
    }
    // (b) set on the 2nd Bytes; then (c) a fresh token resumes.
    {
        let fx = Fx::new(Asset::ranged);
        let mut events = Vec::new();
        let mut seen = 0;
        let nexts = Cell::new(0);
        let start = Instant::now();
        let result = obtain_and(
            fx.rig.env(),
            &fx.feed,
            &fx.request,
            &mut |e| {
                if matches!(e, Progress::Bytes { .. }) {
                    seen += 1;
                    if seen == 2 {
                        fx.rig.cancel.cancel();
                    }
                }
                events.push(e);
            },
            |a| {
                nexts.set(nexts.get() + 1);
                a
            },
        );
        assert!(
            start.elapsed() < Duration::from_secs(2),
            "b: {:?}",
            start.elapsed()
        );
        assert_progress_invariants(&events, size, result.is_ok());
        match result {
            Err(e) => assert_eq!(e.code(), "E_DOWNLOAD_CANCELLED", "b"),
            Ok(_) => panic!("b: verified"),
        }
        assert_eq!(fx.requests().len(), 1, "b");
        assert!(
            !events.iter().any(|e| matches!(
                e,
                Progress::Retrying { .. } | Progress::Verifying | Progress::Verified
            )),
            "b: {events:?}"
        );
        assert_eq!(nexts.get(), 0, "b");
        let kept = fs::read(fx.part()).expect("b: the .part stays");
        assert!(
            (1..size).contains(&(kept.len() as u64)),
            "b: {}",
            kept.len()
        );
        assert_eq!(kept, fx.asset.bytes[..kept.len()], "b: the served prefix");
        assert!(!exists(&fx.verified()), "b");

        let len = kept.len() as u64;
        let run = fx.run_env(DownloadEnv {
            cancel: &CancelToken::new(),
            ..fx.rig.env()
        });
        assert_eq!(
            fs::read(run.ok().value.path()).expect("read"),
            fx.asset.bytes,
            "c"
        );
        let requests = fx.requests();
        assert_eq!(
            range_of(&requests[1]),
            Some(format!("bytes={len}-").as_str()),
            "c"
        );
        assert_eq!(
            run.events[0],
            Progress::Started {
                total: size,
                resumed_from: len
            },
            "c"
        );
        let bytes = bytes_events(&run.events);
        assert!(bytes[0].1 > len, "c: {bytes:?}");
        assert!(bytes.iter().all(|(r, d)| r == d), "c: {bytes:?}");
        assert_eq!(bytes.last(), Some(&(size, size)), "c");
    }
    // (d) cancelled during a backoff wait.
    {
        let mut fx = Fx::new(|a| Reply::Script(vec![Reply::Status(503), a.ranged()]));
        fx.rig.policy = DownloadPolicy {
            backoff: vec![Duration::from_secs(5)],
            ..test_policy()
        };
        let fx = &fx;
        let start = Instant::now();
        let run = thread::scope(|s| {
            s.spawn(move || {
                assert!(
                    eventually(WAIT, || !fx.requests().is_empty()),
                    "d: the first request"
                );
                thread::sleep(Duration::from_millis(100));
                fx.rig.cancel.cancel();
            });
            fx.run()
        });
        assert!(start.elapsed() < SEC, "d: {:?}", start.elapsed());
        assert_eq!(run.err().code(), "E_DOWNLOAD_CANCELLED", "d");
        assert_eq!(fx.requests().len(), 1, "d");
        assert_eq!(
            retryings(&run.events).len(),
            1,
            "d: the Retrying precedes the wait"
        );
    }
    // (g) set while a read is blocked in the transport: the stalled read returns, and no Retrying follows.
    {
        let mut fx = Fx::new(stalling);
        fx.rig.policy = DownloadPolicy {
            backoff: vec![Duration::from_secs(5)],
            stall: Duration::from_millis(500),
            ..test_policy()
        };
        let fx = &fx;
        let start = Instant::now();
        let run = thread::scope(|s| {
            s.spawn(move || {
                assert!(
                    eventually(WAIT, || fs::metadata(fx.part())
                        .is_ok_and(|m| m.len() == 100_000)),
                    "g: the sent bytes on disk"
                );
                thread::sleep(Duration::from_millis(100));
                fx.rig.cancel.cancel();
            });
            fx.run()
        });
        assert!(start.elapsed() < SEC, "g: {:?}", start.elapsed());
        assert_progress_invariants(&run.events, size, false);
        assert_eq!(run.err().code(), "E_DOWNLOAD_CANCELLED", "g");
        assert_eq!(fx.requests().len(), 1, "g");
        assert!(retryings(&run.events).is_empty(), "g: {:?}", run.events);
        assert_eq!(run.nexts, 0, "g");
        assert_eq!(
            fs::metadata(fx.part()).expect("g: the .part stays").len(),
            100_000,
            "g"
        );
    }
    // (e) get_streamed with a sink that cancels.
    {
        let body = seeded(9, 400_000);
        let server = FixtureServer::start(vec![(
            "/x".to_owned(),
            Reply::Ok {
                body: body.clone(),
                content_type: OCTET,
            },
        )]);
        let token = CancelToken::new();
        let mut calls = 0;
        let result = http::get_streamed(
            &HttpConfig::loopback(SEC, SEC),
            &server.url("/x"),
            &[],
            StreamLimits {
                stall: Duration::from_secs(2),
                deadline: Duration::from_secs(5),
            },
            &mut |_| HeadAction::Read {
                max_body: body.len() as u64,
            },
            &mut |_| {
                calls += 1;
                token.cancel();
                Ok(())
            },
            &token,
        );
        assert_eq!(result, Err(StreamError::Cancelled), "e");
        assert_eq!(calls, 1, "e");
    }
}

#[test]
fn discard_cases() {
    // (a) discard, then nothing is cached.
    {
        let fx = Fx::new(Asset::ranged);
        let artifact = fx.run().result.unwrap_or_else(|e| panic!("a: {e}")).value;
        discard(artifact, &FsStagingIo).unwrap_or_else(|e| panic!("a: {e}"));
        assert!(!exists(&fx.dir()), "a");
        let before = fx.requests().len();
        let run = fx.run();
        assert!(!run.ok().cached, "a");
        assert_eq!(fx.requests().len(), before + 1, "a");
    }
    // (b), (c), (e): two held artifacts; discarding one touches nothing else.
    {
        let many = Many::new(vec![
            Asset::new("Alpha", 71, SIZE),
            Asset::new("Bravo", 72, 90_001),
        ]);
        let alpha = many
            .obtain(0)
            .result
            .unwrap_or_else(|e| panic!("b: {e}"))
            .value;
        let bravo = many
            .obtain(1)
            .result
            .unwrap_or_else(|e| panic!("b: {e}"))
            .value;
        assert!(
            exists(alpha.path()),
            "e: the prune keeps an offered artifact without any discard"
        );
        let stale = many
            .downloads()
            .join(format!("{}.part", hex_lower(&[5u8; 32])));
        fs::write(&stale, b"stale").expect("stale");
        discard(alpha.clone(), &FsStagingIo).unwrap_or_else(|e| panic!("b: {e}"));
        assert!(!exists(alpha.path().parent().expect("dir")), "b");
        let request = many.request(1);
        assert!(
            verify_file(
                bravo.path(),
                &Expected::from_request(&request),
                &many.rig.ctx.signing_key
            )
            .is_ok(),
            "b: Bravo still verifies"
        );
        assert!(exists(&stale), "b: the stale .part untouched");
        discard(alpha, &FsStagingIo).unwrap_or_else(|e| panic!("c: {e}"));
    }
    // (d) a removal that fails is returned.
    {
        let fx = Fx::new(Asset::ranged);
        let artifact = fx.run().result.unwrap_or_else(|e| panic!("d: {e}")).value;
        for (kind, code) in [
            (io::ErrorKind::PermissionDenied, "E_STAGING_DENIED"),
            (io::ErrorKind::StorageFull, "E_STAGING_FULL"),
        ] {
            match discard(
                artifact.clone(),
                &FaultyStagingIo::failing(Op::RemoveDir, 1, kind),
            ) {
                Err(e) => assert_eq!(staging_of(&e), (code, "remove"), "d: {kind:?}"),
                Ok(()) => panic!("d: {kind:?} discarded"),
            }
        }
        assert!(exists(&fx.dir()), "d: nothing removed");
    }
    // (f) an in-flight artifact is unaffected (PD-18).
    {
        let many = &Many::new(vec![
            Asset::new("Alpha", 73, SIZE),
            Asset::new("Bravo", 74, 90_001),
        ]);
        let old_x = many
            .obtain(0)
            .result
            .unwrap_or_else(|e| panic!("f: {e}"))
            .value;
        let other = many
            .obtain(1)
            .result
            .unwrap_or_else(|e| panic!("f: {e}"))
            .value;
        let dir_x = old_x.path().parent().expect("dir").to_path_buf();
        let (ready_tx, ready_rx) = mpsc::channel::<()>();
        let (go_tx, go_rx) = mpsc::channel::<()>();
        thread::scope(|s| {
            let b = s.spawn(move || {
                let request = many.request(0);
                obtain_and(
                    many.rig.env(),
                    &many.feed,
                    &request,
                    &mut |e| {
                        if matches!(e, Progress::Started { .. }) {
                            ready_tx.send(()).expect("ready");
                            go_rx.recv().expect("go");
                        }
                    },
                    |a| a,
                )
                .map(|o| o.value)
            });
            ready_rx.recv_timeout(WAIT).expect("B in flight");
            match discard(old_x.clone(), &FsStagingIo) {
                Err(e @ DownloadError::DiscardBusy { .. }) => {
                    assert_eq!(e.code(), "E_DOWNLOAD_BUSY", "f")
                }
                other => panic!("f: {other:?}"),
            }
            assert!(exists(&dir_x), "f: <hex>/ still present");
            discard(other, &FsStagingIo).unwrap_or_else(|e| panic!("f: another hash: {e}"));
            go_tx.send(()).expect("go");
            let held = b
                .join()
                .expect("B joins")
                .unwrap_or_else(|e| panic!("f: B: {e}"));
            assert!(exists(held.path()), "f: B's artifact exists");
            discard(held, &FsStagingIo).unwrap_or_else(|e| panic!("f: {e}"));
            assert!(!exists(&dir_x), "f: <hex>/ gone");
        });
    }
}

#[test]
fn retrying_events_are_ordered_and_serialize() {
    let mut kinds = BTreeSet::new();
    let mut note = |events: &[Progress]| {
        for e in events {
            let value = serde_json::to_value(e).expect("serialises");
            kinds.insert(value["kind"].as_str().expect("a kind").to_owned());
        }
    };
    {
        let mut fx = Fx::new(|_| Reply::Close);
        fx.rig.policy = DownloadPolicy {
            backoff: vec![
                Duration::from_millis(10),
                Duration::from_millis(20),
                Duration::from_millis(40),
            ],
            ..test_policy()
        };
        let (run, seen) = run_noting_requests(&fx);
        assert_eq!(run.err().code(), "E_DOWNLOAD_OFFLINE");
        let mut expected = vec![Progress::Started {
            total: SIZE as u64,
            resumed_from: 0,
        }];
        for (attempt, wait_ms) in [(1, 10), (2, 20), (3, 40)] {
            expected.push(Progress::Retrying {
                attempt,
                of: 4,
                code: "E_DOWNLOAD_OFFLINE".to_owned(),
                wait_ms,
            });
        }
        assert_eq!(run.events, expected, "nothing after the 4th failed request");
        assert_eq!(seen, vec![1, 2, 3], "attempt k's event sees k requests");
        let value = serde_json::to_value(&run.events[1]).expect("serialises");
        let keys: BTreeSet<&str> = value
            .as_object()
            .expect("an object")
            .keys()
            .map(String::as_str)
            .collect();
        assert_eq!(
            keys,
            BTreeSet::from(["kind", "attempt", "of", "code", "wait_ms"])
        );
        assert_eq!(value["kind"], "retrying");
        note(&run.events);
    }
    {
        let mut fx = Fx::new(|a| {
            Reply::Script(vec![
                Reply::HeadersThenStall {
                    declared_len: a.bytes.len(),
                    sent: a.bytes[..300_000].to_vec(),
                    stall: Duration::from_secs(5),
                },
                a.ranged(),
            ])
        });
        fx.rig.policy = DownloadPolicy {
            stall: Duration::from_millis(200),
            ..test_policy()
        };
        let (run, seen) = run_noting_requests(&fx);
        run.ok();
        let retried = retryings(&run.events);
        assert_eq!(
            retried.first().map(|r| r.2.as_str()),
            Some("E_DOWNLOAD_STALLED"),
            "{retried:?}"
        );
        let attempts: Vec<usize> = retried.iter().map(|r| r.0 as usize).collect();
        assert_eq!(seen, attempts, "attempt k's event sees k requests");
        note(&run.events);
    }
    {
        let fx = Fx::new(|a| {
            Reply::Script(vec![
                Reply::Truncated {
                    declared_len: a.bytes.len(),
                    sent: a.bytes[..400_000].to_vec(),
                },
                Reply::Ok {
                    body: a.bytes.clone(),
                    content_type: OCTET,
                },
            ])
        });
        note(&fx.run().events);
    }
    let six: BTreeSet<String> = [
        "started",
        "bytes",
        "restarted",
        "retrying",
        "verifying",
        "verified",
    ]
    .into_iter()
    .map(str::to_owned)
    .collect();
    assert!(kinds.is_subset(&six), "{kinds:?}");
}

#[test]
fn policy_has_no_overall_budget() {
    assert_eq!(
        DownloadPolicy::production(),
        DownloadPolicy {
            attempts: 4,
            backoff: vec![
                Duration::from_secs(1),
                Duration::from_secs(2),
                Duration::from_secs(4)
            ],
            stall: Duration::from_secs(30),
            attempt_deadline: Duration::from_secs(3600),
        }
    );
    // Every attempt runs to its own deadline: nothing cuts the run short.
    {
        let mut fx = Fx::new(stalling);
        fx.rig.policy = DownloadPolicy {
            backoff: vec![Duration::from_millis(100)],
            stall: Duration::from_secs(2),
            attempt_deadline: Duration::from_millis(300),
            ..test_policy()
        };
        let start = Instant::now();
        let run = fx.run();
        let elapsed = start.elapsed();
        assert_eq!(run.err().code(), "E_DOWNLOAD_TIMEOUT");
        assert_eq!(fx.requests().len(), 4);
        assert!(elapsed >= Duration::from_millis(1500), "{elapsed:?}");
        assert!(elapsed < Duration::from_secs(3), "{elapsed:?}");
    }
}

#[test]
fn slow_moving_download_is_not_an_error() {
    // Ten chunks 150 ms apart: longer than the stall bound in all (SC-037).
    {
        let mut fx = Fx::new(|a| Reply::Paced {
            body: a.bytes.clone(),
            chunk: a.bytes.len().div_ceil(10),
            interval: Duration::from_millis(150),
        });
        fx.rig.policy = DownloadPolicy {
            stall: SEC,
            attempt_deadline: Duration::from_secs(5),
            ..test_policy()
        };
        let start = Instant::now();
        let run = fx.run();
        let elapsed = start.elapsed();
        assert_eq!(
            fs::read(run.ok().value.path()).expect("read"),
            fx.asset.bytes
        );
        let retried = retryings(&run.events);
        if !retried.is_empty() {
            println!("slow_moving_download_is_not_an_error: a harmless retry: {retried:?}");
        }
        assert!(fx.requests().len() <= fx.rig.policy.attempts as usize);
        assert!(elapsed > fx.rig.policy.stall, "{elapsed:?}");
        assert!(elapsed >= Duration::from_millis(1350), "{elapsed:?}");
        assert!(elapsed < Duration::from_secs(3), "{elapsed:?}");
    }
}
