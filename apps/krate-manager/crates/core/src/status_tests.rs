//! `status` unit tests (plan §7.1 rows 009, 010, 012, 018, 030, 032; Phase 9 SC-016's status half).
//!
//! Feeds are fixture files read in place (`tools/feed-contract/fixtures/feeds/valid/`) and served by a loopback
//! `FixtureServer`; every build runs on its own `fresh_temp_roots()` with a `FakeHostFacts`. The golden views are
//! compared byte for byte with `apps/krate-manager/tests/fixtures/status/<scenario>.json`, which no test writes
//! (PD-10): a mismatch prints the expected text for review.

use super::*;
use crate::detect::{InnoValues, PkgOutputs, RecordRead};
use crate::extract::FsExtractIo;
use crate::feed::{Feed, FeedError, FeedSnapshot, PlatformKey};
use crate::fixture_server::{FixtureServer, Reply};
use crate::http::{HttpConfig, HttpError};
use crate::install::{EntryTag, FsInstallIo, Manifest, ManifestEntry, NativeDrivers, RootTag};
use crate::platform::VENDOR_DIR;
use crate::roots::Roots;
use crate::settings::Settings;
use crate::test_signer::TestSigner;
use crate::test_support::{
    FakeElevation, FakeHostFacts, InstallRig, RecordingInstallIo, TestPlugin, fresh_temp_roots,
    install_capture, kat_signing_key, take_mine, test_feed_document,
};
use crate::verify::{hex_lower, sha256_of};
use log::Level;
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{MAIN_SEPARATOR_STR, Path, PathBuf};
use std::sync::Barrier;
use std::thread;
use std::time::Duration;

const SEC: Duration = Duration::from_secs(1);
const FEED_PATH: &str = "/versions.json";
const NOW: u64 = 1_700_000_000;
const FULL: &str = "full-v2.json";
const MIXED: &str = "mixed.json";
const TENTH: &str = "tenth-plugin.json";
const V1_ONLY: &str = "v1-only.json";
/// The detect module's `log` target (plan §4.5.1).
const DETECT_TARGET: &str = "krate_manager_core::detect";

/// The 13 SC-012 scenarios, in the spec's order.
const SCENARIOS: [&str; 13] = [
    "fresh-all-states",
    "cached-stale",
    "no-feed",
    "future-schema",
    "macos-intel-not-for-this-mac",
    "not-for-this-computer-linux",
    "needs-newer-os-macos",
    "needs-newer-os-linux",
    "needs-newer-os-windows",
    "macos-au-copies",
    "record-notices",
    "notes-rendering",
    "installed-elsewhere",
];

// ---- fixtures and small helpers -----------------------------------------------------------------------------------

fn feed_fixture(name: &str) -> Vec<u8> {
    // crates/core -> crates -> krate-manager -> apps -> the repo root
    let path = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../../tools/feed-contract/fixtures/feeds/valid")
        .join(name);
    fs::read(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()))
}

fn parsed(name: &str) -> Feed {
    feed::parse(&feed_fixture(name)).unwrap_or_else(|e| panic!("parse {name}: {e}"))
}

fn goldens_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/status")
}

fn ok(body: Vec<u8>) -> Reply {
    Reply::Ok {
        body,
        content_type: "application/json",
    }
}

/// A context whose cache lives in the given temp state dir (Rule 1), loopback 1 s / 1 s (as `feed_tests.rs`).
fn context(roots: &Roots, feed_url: String) -> AppContext {
    AppContext {
        roots: roots.clone(),
        settings: Settings::default(),
        feed_url,
        http: HttpConfig::loopback(SEC, SEC),
        signing_key: kat_signing_key(),
    }
}

fn os_version(text: &str) -> OsVersion {
    OsVersion::parse(text).unwrap_or_else(|| panic!("{text} is an OS version"))
}

fn core(text: &str) -> CoreVersion {
    CoreVersion::parse(text).unwrap_or_else(|| panic!("{text} is a core version"))
}

fn plugin<'a>(view: &'a StatusView, name: &str) -> &'a PluginView {
    view.plugins
        .iter()
        .find(|p| p.name == name)
        .unwrap_or_else(|| panic!("no card {name}"))
}

/// The first root of `kind` in the context's roots.
fn root_of(roots: &Roots, kind: RootKind) -> PathBuf {
    roots
        .plugin_roots()
        .iter()
        .find(|r| r.kind == kind)
        .map(|r| r.path.clone())
        .unwrap_or_else(|| panic!("no {kind:?} root"))
}

/// The managed VST3 location of a plugin without an install location, on this host (FR-001).
fn managed_bundle(roots: &Roots, settings: &Settings, bundle: &str) -> PathBuf {
    match NativePlatform.os() {
        Os::Windows => {
            let stem = bundle.strip_suffix(".vst3").unwrap_or(bundle);
            root_of(roots, RootKind::Vst3Global)
                .join(VENDOR_DIR)
                .join(stem)
                .join(bundle)
        }
        Os::Macos => root_of(roots, RootKind::Vst3Global).join(bundle),
        Os::Linux => settings.linux_plugin_root_or_default(roots).join(bundle),
    }
}

/// A bundle directory with a moduleinfo of the given top-level and class versions.
fn write_bundle(bundle: &Path, name: &str, top: &str, class: &str) {
    let resources = bundle.join("Contents").join("Resources");
    fs::create_dir_all(&resources)
        .unwrap_or_else(|e| panic!("create {}: {e}", resources.display()));
    let text = format!(
        "{{\n  \"Name\": \"{name}\",\n  \"Version\": \"{top}\",\n  \"Classes\": [\n    {{\n      \"Name\": \"{name}\",\n      \"Version\": \"{class}\"\n    }}\n  ]\n}}\n"
    );
    let file = resources.join("moduleinfo.json");
    fs::write(&file, text).unwrap_or_else(|e| panic!("write {}: {e}", file.display()));
}

/// The calling thread's captured `info!` lines on the status target.
fn status_infos(captured: &[(String, Level, String)]) -> Vec<String> {
    captured
        .iter()
        .filter(|(target, level, _)| target == LOG_TARGET && *level == Level::Info)
        .map(|(_, _, message)| message.clone())
        .collect()
}

fn header<'a>(r: &'a crate::fixture_server::RecordedRequest, name: &str) -> Option<&'a str> {
    r.headers
        .iter()
        .find(|(k, _)| k.eq_ignore_ascii_case(name))
        .map(|(_, v)| v.as_str())
}

// ---- synthetic inputs for `assemble` ------------------------------------------------------------------------------

fn fresh(feed: Feed) -> FeedCheck {
    FeedCheck {
        snapshot: Some(FeedSnapshot {
            feed,
            fetched_at_unix: NOW,
            source: SnapshotSource::Fresh,
        }),
        error: None,
        cache_error: None,
    }
}

fn cached(feed: Feed, fetched_at_unix: u64, error: FeedError) -> FeedCheck {
    FeedCheck {
        snapshot: Some(FeedSnapshot {
            feed,
            fetched_at_unix,
            source: SnapshotSource::Cached,
        }),
        error: Some(error),
        cache_error: None,
    }
}

fn no_feed(error: FeedError) -> FeedCheck {
    FeedCheck {
        snapshot: None,
        error: Some(error),
        cache_error: None,
    }
}

fn offline() -> FeedError {
    FeedError::Http(HttpError::Offline {
        detail: "connection refused".to_owned(),
    })
}

fn known(text: &str, source: VersionSource) -> CopyVersion {
    CopyVersion::Known {
        version: core(text),
        source,
    }
}

fn unknown(reason: UnknownReason) -> CopyVersion {
    CopyVersion::Unknown(reason)
}

/// One copy whose path is the literal, never joined, so `Path::display()` is the same on every host (SC-012).
fn copy(
    format: CopyFormat,
    place: Place,
    path: &str,
    managed: bool,
    rank: Option<u8>,
    version: CopyVersion,
) -> DetectedCopy {
    DetectedCopy {
        format,
        place,
        path: PathBuf::from(path),
        managed,
        rank,
        version,
    }
}

fn vst3(place: Place, path: &str, managed: bool, rank: Option<u8>, v: CopyVersion) -> DetectedCopy {
    copy(CopyFormat::Vst3, place, path, managed, rank, v)
}

fn au(place: Place, path: &str, managed: bool, v: CopyVersion) -> DetectedCopy {
    copy(CopyFormat::Au, place, path, managed, None, v)
}

/// Windows: `/fixture/vst3-user/<P>.vst3` (rank 0).
fn win_user(p: &str, v: CopyVersion) -> DetectedCopy {
    vst3(
        Place::Root(RootKind::Vst3User),
        &format!("/fixture/vst3-user/{p}.vst3"),
        false,
        Some(0),
        v,
    )
}

/// Windows: `/fixture/vst3-global/<P>.vst3` (rank 1, not the managed location).
fn win_global_root(p: &str, v: CopyVersion) -> DetectedCopy {
    vst3(
        Place::Root(RootKind::Vst3Global),
        &format!("/fixture/vst3-global/{p}.vst3"),
        false,
        Some(1),
        v,
    )
}

/// Windows: the vendor folder of the first global root (rank 1), managed unless an install location is recorded.
fn win_vendor(p: &str, managed: bool, v: CopyVersion) -> DetectedCopy {
    vst3(
        Place::WindowsVendor(RootKind::Vst3Global),
        &format!("/fixture/vst3-global/{VENDOR_DIR}/{p}/{p}.vst3"),
        managed,
        Some(1),
        v,
    )
}

/// macOS / Linux: `/fixture/<root>/<P>.vst3`.
fn unix_root(
    kind: RootKind,
    root: &str,
    p: &str,
    managed: bool,
    rank: u8,
    v: CopyVersion,
) -> DetectedCopy {
    vst3(
        Place::Root(kind),
        &format!("/fixture/{root}/{p}.vst3"),
        managed,
        Some(rank),
        v,
    )
}

fn checked(
    copies: Vec<DetectedCopy>,
    record: Option<InstallerRecord>,
    record_error: Option<&'static str>,
    managed_rank: Option<u8>,
) -> Detection {
    Detection::Checked {
        copies,
        record,
        record_error,
        managed_rank,
    }
}

fn inno(version_text: &str, install_location: Option<&str>) -> InstallerRecord {
    InstallerRecord::Inno {
        version_text: Some(version_text.to_owned()),
        version: CoreVersion::parse(version_text),
        install_location: install_location.map(PathBuf::from),
    }
}

fn pkg(version_text: &str, files: u64) -> InstallerRecord {
    InstallerRecord::Pkg {
        version_text: version_text.to_owned(),
        version: CoreVersion::parse(version_text),
        location: Some("/".to_owned()),
        files,
    }
}

fn manifest(version_text: &str) -> InstallerRecord {
    InstallerRecord::Manifest {
        version_text: version_text.to_owned(),
        version: CoreVersion::parse(version_text),
    }
}

fn detections(list: Vec<(&str, Detection)>) -> BTreeMap<String, Detection> {
    list.into_iter()
        .map(|(name, d)| (name.to_owned(), d))
        .collect()
}

/// The Markdown of `notes-rendering` (plan §4.11, `plan.md:876-882`): no URL anywhere.
const NOTES_MARKDOWN: &str = "# Heading one\n\n##### Heading five\n\n3. Third item\n4. Fourth item\n\n- Outer item\n  - Inner item\n\n> A quoted line.\n\n```\nlet x = 1;\n```\n\n---\n\nA paragraph with **strong**, *emphasis* and `inline code`, then a hard break  \nafter two spaces.\n";

/// One scenario: its name and the view `assemble` makes from its synthetic inputs (SC-012).
fn scenario(name: &str) -> StatusView {
    let win = |check: &FeedCheck, running: &str, d: &BTreeMap<String, Detection>| {
        assemble(
            Os::Windows,
            Arch::X86_64,
            check,
            Some(os_version(running)),
            d,
        )
    };
    match name {
        // Every installed value, every update value, the seven VST3 unknown reasons, a `loads_before_managed` copy.
        // Juliet's release is reduced to the shape `feed::parse` gives an entry whose install names it refuses (no
        // install facts, no platforms), so its card is `not_checked`, as the product shows such an entry.
        "fresh-all-states" => {
            let mut feed = parsed(TENTH);
            let juliet = feed
                .plugins
                .get_mut("Juliet")
                .and_then(|e| e.release.as_mut())
                .expect("tenth-plugin.json has Juliet with a release");
            juliet.install = None;
            juliet.platforms.clear();
            let d = detections(vec![
                (
                    "Alpha",
                    checked(
                        vec![
                            win_user("Alpha", known("1.1.0", VersionSource::TopLevel)),
                            win_vendor("Alpha", true, known("1.2.0", VersionSource::Classes)),
                        ],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                (
                    "Bravo",
                    checked(
                        vec![win_vendor(
                            "Bravo",
                            true,
                            known("1.0.0", VersionSource::Classes),
                        )],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                (
                    "Charlie",
                    checked(
                        vec![win_vendor(
                            "Charlie",
                            true,
                            unknown(UnknownReason::SingleFile),
                        )],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                (
                    "Delta",
                    checked(
                        vec![win_vendor(
                            "Delta",
                            true,
                            known("0.2.0", VersionSource::Classes),
                        )],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                ("Echo", checked(vec![], None, None, Some(1))),
                (
                    "Foxtrot",
                    checked(
                        vec![
                            win_user("Foxtrot", unknown(UnknownReason::NoModuleInfo)),
                            win_global_root("Foxtrot", known("0.3.0", VersionSource::Classes)),
                        ],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                (
                    "Golf",
                    checked(
                        vec![
                            win_user("Golf", unknown(UnknownReason::ClassesDisagree)),
                            win_vendor("Golf", true, unknown(UnknownReason::TooLarge)),
                        ],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                (
                    "Hotel",
                    checked(
                        vec![
                            win_user("Hotel", unknown(UnknownReason::NoVersion)),
                            win_vendor("Hotel", true, unknown(UnknownReason::Unreadable)),
                        ],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                (
                    "India",
                    checked(
                        vec![win_vendor("India", true, unknown(UnknownReason::Corrupt))],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                ("Juliet", Detection::NotChecked),
            ]);
            win(&fresh(feed), "10.0.26200", &d)
        }
        // Linux: the refresh failed, the cached copy of three days ago is shown; the managed root is `vst3-user`.
        "cached-stale" => {
            let d = detections(vec![
                (
                    "Alpha",
                    checked(
                        vec![
                            unix_root(
                                RootKind::Vst3User,
                                "vst3-user",
                                "Alpha",
                                true,
                                0,
                                known("1.2.0", VersionSource::Classes),
                            ),
                            unix_root(
                                RootKind::Vst3Global,
                                "vst3-global",
                                "Alpha",
                                false,
                                1,
                                known("1.1.0", VersionSource::Classes),
                            ),
                        ],
                        Some(manifest("1.2.0")),
                        None,
                        Some(0),
                    ),
                ),
                (
                    "Bravo",
                    checked(
                        vec![unix_root(
                            RootKind::Vst3User,
                            "vst3-user",
                            "Bravo",
                            true,
                            0,
                            known("2.1.0", VersionSource::Classes),
                        )],
                        None,
                        None,
                        Some(0),
                    ),
                ),
                ("Charlie", checked(vec![], None, None, Some(0))),
            ]);
            assemble(
                Os::Linux,
                Arch::X86_64,
                &cached(parsed(FULL), NOW - 3 * 86_400, offline()),
                Some(os_version("2.39")),
                &d,
            )
        }
        // Windows, first run while offline: no cache, no cards.
        "no-feed" => win(&no_feed(offline()), "10.0.26200", &BTreeMap::new()),
        // macOS: the feed needs a newer manager and no cached copy exists.
        "future-schema" => assemble(
            Os::Macos,
            Arch::Aarch64,
            &no_feed(FeedError::FutureSchema { found: 3 }),
            Some(os_version("15.7.9")),
            &BTreeMap::new(),
        ),
        // An Intel Mac: Alpha ships `darwin-aarch64` only; Bravo `darwin-x86_64`, Charlie universal.
        "macos-intel-not-for-this-mac" => {
            let d = detections(vec![
                ("Alpha", checked(vec![], None, None, Some(1))),
                (
                    "Bravo",
                    checked(
                        vec![
                            unix_root(
                                RootKind::Vst3Global,
                                "vst3-global",
                                "Bravo",
                                true,
                                1,
                                known("2.0.1", VersionSource::Classes),
                            ),
                            au(
                                Place::Root(RootKind::AuGlobal),
                                "/fixture/au-global/Bravo.component",
                                true,
                                known("2.0.1", VersionSource::AuPlist),
                            ),
                        ],
                        Some(pkg("2.0.1", 12)),
                        None,
                        Some(1),
                    ),
                ),
                ("Charlie", checked(vec![], None, None, Some(1))),
            ]);
            assemble(
                Os::Macos,
                Arch::X86_64,
                &fresh(parsed(FULL)),
                Some(os_version("15.7.9")),
                &d,
            )
        }
        // Linux on aarch64: no `linux-aarch64` asset exists, so nothing is offered; detection still reports Alpha.
        "not-for-this-computer-linux" => {
            let d = detections(vec![
                (
                    "Alpha",
                    checked(
                        vec![unix_root(
                            RootKind::Vst3User,
                            "vst3-user",
                            "Alpha",
                            true,
                            0,
                            known("1.2.0", VersionSource::Classes),
                        )],
                        None,
                        None,
                        Some(0),
                    ),
                ),
                ("Bravo", checked(vec![], None, None, Some(0))),
                ("Charlie", checked(vec![], None, None, Some(0))),
            ]);
            assemble(
                Os::Linux,
                Arch::Aarch64,
                &fresh(parsed(FULL)),
                Some(os_version("2.39")),
                &d,
            )
        }
        // Apple silicon on 10.15: Alpha and Charlie need macOS 11.0; Bravo ships for Intel only.
        "needs-newer-os-macos" => {
            let d = detections(vec![
                (
                    "Alpha",
                    checked(
                        vec![unix_root(
                            RootKind::Vst3Global,
                            "vst3-global",
                            "Alpha",
                            true,
                            1,
                            known("1.1.0", VersionSource::Classes),
                        )],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                ("Bravo", checked(vec![], None, None, Some(1))),
                ("Charlie", checked(vec![], None, None, Some(1))),
            ]);
            assemble(
                Os::Macos,
                Arch::Aarch64,
                &fresh(parsed(FULL)),
                Some(os_version("10.15")),
                &d,
            )
        }
        // glibc 2.27 against every asset's 2.31.
        "needs-newer-os-linux" => {
            let d = detections(vec![
                ("Alpha", checked(vec![], None, None, Some(0))),
                ("Bravo", checked(vec![], None, None, Some(0))),
                ("Charlie", checked(vec![], None, None, Some(0))),
            ]);
            assemble(
                Os::Linux,
                Arch::X86_64,
                &fresh(parsed(FULL)),
                Some(os_version("2.27")),
                &d,
            )
        }
        // Windows 6.1: only Charlie's asset carries a `min_os` (10.0).
        "needs-newer-os-windows" => {
            let d = detections(vec![
                ("Alpha", checked(vec![], None, None, Some(1))),
                ("Bravo", checked(vec![], None, None, Some(1))),
                ("Charlie", checked(vec![], None, None, Some(1))),
            ]);
            win(&fresh(parsed(FULL)), "6.1.7601", &d)
        }
        // AU copies beside VST3: a known `au_plist` version, `not_a_bundle`, `au_no_version`; an AU copy alone never
        // sets `installed` (Bravo).
        "macos-au-copies" => {
            let d = detections(vec![
                (
                    "Alpha",
                    checked(
                        vec![
                            unix_root(
                                RootKind::Vst3Global,
                                "vst3-global",
                                "Alpha",
                                true,
                                1,
                                known("1.2.0", VersionSource::Classes),
                            ),
                            au(
                                Place::Root(RootKind::AuUser),
                                "/fixture/au-user/Alpha.component",
                                false,
                                unknown(UnknownReason::NotABundle),
                            ),
                            au(
                                Place::Root(RootKind::AuGlobal),
                                "/fixture/au-global/Alpha.component",
                                true,
                                known("1.2.0", VersionSource::AuPlist),
                            ),
                        ],
                        Some(pkg("1.2.0", 3)),
                        None,
                        Some(1),
                    ),
                ),
                (
                    "Bravo",
                    checked(
                        vec![au(
                            Place::Root(RootKind::AuGlobal),
                            "/fixture/au-global/Bravo.component",
                            true,
                            unknown(UnknownReason::AuNoVersion),
                        )],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                ("Charlie", checked(vec![], None, None, Some(1))),
            ]);
            assemble(
                Os::Macos,
                Arch::Aarch64,
                &fresh(parsed(FULL)),
                Some(os_version("15.7.9")),
                &d,
            )
        }
        // Inno records: files missing (Alpha), record and files disagree with the files at a recorded install location
        // (Bravo; the default vendor copy is an ordinary listed copy), a record that could not be read (Charlie).
        "record-notices" => {
            let d = detections(vec![
                (
                    "Alpha",
                    checked(vec![], Some(inno("1.2.0", None)), None, Some(1)),
                ),
                (
                    "Bravo",
                    checked(
                        vec![
                            win_vendor("Bravo", false, known("1.9.0", VersionSource::Classes)),
                            vst3(
                                Place::InstallerLocation,
                                "/fixture/custom/Bravo/Bravo.vst3",
                                true,
                                None,
                                known("2.0.1", VersionSource::Classes),
                            ),
                        ],
                        Some(inno("2.0.0", Some("/fixture/custom/Bravo"))),
                        None,
                        None,
                    ),
                ),
                (
                    "Charlie",
                    checked(
                        vec![win_vendor(
                            "Charlie",
                            true,
                            known("0.9.0", VersionSource::Classes),
                        )],
                        None,
                        Some("E_RECORD_READ"),
                        Some(1),
                    ),
                ),
            ]);
            win(&fresh(parsed(FULL)), "10.0.26200", &d)
        }
        // Linux: Alpha's notes hold every block and inline kind; Bravo and Charlie have none.
        "notes-rendering" => {
            let mut feed = parsed(FULL);
            feed.plugins
                .get_mut("Alpha")
                .and_then(|e| e.release.as_mut())
                .expect("full-v2.json has Alpha with a release")
                .notes_markdown = Some(NOTES_MARKDOWN.to_owned());
            let d = detections(vec![
                ("Alpha", checked(vec![], None, None, Some(0))),
                ("Bravo", checked(vec![], None, None, Some(0))),
                ("Charlie", checked(vec![], None, None, Some(0))),
            ]);
            assemble(
                Os::Linux,
                Arch::X86_64,
                &fresh(feed),
                Some(os_version("2.39")),
                &d,
            )
        }
        // Windows, managed copies absent: Alpha elsewhere with a known version and a nested copy beside the fixed
        // one; Bravo elsewhere with no known version; Charlie's release page is not on github.com, so it has none.
        "installed-elsewhere" => {
            let mut feed = parsed(FULL);
            feed.plugins
                .get_mut("Charlie")
                .and_then(|e| e.release.as_mut())
                .expect("full-v2.json has Charlie with a release")
                .release_notes_url = "https://example.test/charlie/notes".to_owned();
            let d = detections(vec![
                (
                    "Alpha",
                    checked(
                        vec![
                            win_user("Alpha", known("1.1.0", VersionSource::Classes)),
                            vst3(
                                Place::Nested(RootKind::Vst3Global),
                                "/fixture/vst3-global/Old/Alpha.vst3",
                                false,
                                Some(1),
                                known("1.0.0", VersionSource::Classes),
                            ),
                        ],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                (
                    "Bravo",
                    checked(
                        vec![win_user("Bravo", unknown(UnknownReason::Corrupt))],
                        None,
                        None,
                        Some(1),
                    ),
                ),
                ("Charlie", checked(vec![], None, None, Some(1))),
            ]);
            win(&fresh(feed), "10.0.26200", &d)
        }
        other => panic!("no scenario {other}"),
    }
}

/// SC-012's OS-consistency rule over one golden: the findings, empty when the file is a state the product can produce.
fn os_consistency(name: &str, doc: &Value) -> Vec<String> {
    let mut out = Vec::new();
    let os = doc["os"].as_str().unwrap_or("");
    if !["windows", "macos", "linux"].contains(&os) {
        out.push(format!("{name}: os {os:?} is not an OS"));
    }
    let plugins = doc["plugins"].as_array().cloned().unwrap_or_default();
    for p in &plugins {
        let card = p["name"].as_str().unwrap_or("?");
        match p["record"]["kind"].as_str() {
            Some("pkg") if os != "macos" => {
                out.push(format!("{name}: {card}: pkg record outside macos"))
            }
            Some("manifest") if os != "linux" => {
                out.push(format!("{name}: {card}: manifest record outside linux"));
            }
            Some("inno") if os != "windows" => {
                out.push(format!("{name}: {card}: inno record outside windows"));
            }
            _ => {}
        }
        for c in p["copies"].as_array().cloned().unwrap_or_default() {
            let format = c["format"].as_str().unwrap_or("");
            let place = c["place"].as_str().unwrap_or("");
            if format == "au" && os != "macos" {
                out.push(format!("{name}: {card}: au copy outside macos"));
            }
            if ["au_user", "au_global"].contains(&place) && os != "macos" {
                out.push(format!("{name}: {card}: {place} outside macos"));
            }
            if (format == "au") != ["au_user", "au_global"].contains(&place) {
                out.push(format!("{name}: {card}: format {format} at place {place}"));
            }
            if place == "installer_location" && os != "windows" {
                out.push(format!(
                    "{name}: {card}: installer_location outside windows"
                ));
            }
            if place == "configured" && os != "linux" {
                out.push(format!("{name}: {card}: configured root outside linux"));
            }
            if c["vendor_folder"] == Value::Bool(true) && os != "windows" {
                out.push(format!("{name}: {card}: vendor_folder outside windows"));
            }
            if c["nested"] == Value::Bool(true) && format != "vst3" {
                out.push(format!("{name}: {card}: nested {format} copy"));
            }
        }
    }
    out
}

/// `serde_json::to_string` of one value.
fn json<T: Serialize>(value: &T) -> String {
    serde_json::to_string(value).expect("a view value serialises")
}

// ---- SC-009 -------------------------------------------------------------------------------------------------------

#[test]
fn feed_driven_detection() {
    let server = FixtureServer::start(vec![(FEED_PATH.to_owned(), ok(feed_fixture(TENTH)))]);
    let os = NativePlatform.os();
    for fixture in [TENTH, V1_ONLY, MIXED] {
        server.set_route(FEED_PATH, ok(feed_fixture(fixture)));
        let (_td, roots) = fresh_temp_roots();
        let ctx = context(&roots, server.url(FEED_PATH));
        write_bundle(
            &managed_bundle(&ctx.roots, &ctx.settings, "Juliet.vst3"),
            "Juliet",
            "0.7.0",
            "0.7.0.1",
        );
        let host = FakeHostFacts::default();
        let view = build(&ctx, FetchMode::Normal, NOW, &host);
        assert_eq!(
            view.feed.source,
            FeedSource::Fresh,
            "{fixture}: {:?}",
            view.feed.error
        );
        let calls = host.calls.borrow().clone();
        assert_eq!(
            calls.iter().filter(|c| *c == "os_version").count(),
            1,
            "{fixture}: {calls:?}"
        );
        assert_eq!(
            calls.first().map(String::as_str),
            Some("os_version"),
            "{fixture}"
        );
        match fixture {
            TENTH => {
                assert_eq!(view.plugins.len(), 10, "{fixture}");
                let juliet = plugin(&view, "Juliet");
                assert_eq!(juliet.installed, InstalledView::Version, "{juliet:?}");
                assert_eq!(juliet.installed_version.as_deref(), Some("0.7.0"));
                assert_eq!(juliet.update, Some(UpdateView::UpToDate));
                let expected = match os {
                    Os::Linux => AvailabilityView::Available,
                    Os::Windows | Os::Macos => AvailabilityView::NotForThisMachine,
                };
                assert_eq!(juliet.availability, expected, "{juliet:?}");
                assert_eq!(juliet.copies.len(), 1, "{juliet:?}");
                assert!(juliet.copies[0].managed, "{juliet:?}");
            }
            V1_ONLY => {
                assert_eq!(view.plugins.len(), 3);
                for p in &view.plugins {
                    assert_eq!(p.installed, InstalledView::NotChecked, "{p:?}");
                    assert_eq!(p.availability, AvailabilityView::NotThroughManager, "{p:?}");
                    assert_eq!(p.update, None, "{p:?}");
                    assert!(p.copies.is_empty(), "{p:?}");
                }
                assert_eq!(calls, vec!["os_version".to_owned()]);
            }
            _ => {
                let delta = plugin(&view, "Delta");
                assert_eq!(delta.installed, InstalledView::NotChecked, "{delta:?}");
                assert_eq!(
                    delta.availability,
                    AvailabilityView::NotThroughManager,
                    "{delta:?}"
                );
                assert_eq!(delta.update, None, "{delta:?}");
                assert!(delta.copies.is_empty(), "{delta:?}");
            }
        }
    }
}

// ---- SC-010 -------------------------------------------------------------------------------------------------------

#[test]
fn running_os_gates() {
    let check = fresh(parsed(FULL));
    let none = BTreeMap::new();
    let cases = [
        (
            Some(os_version("10.15")),
            AvailabilityView::NeedsNewerOs,
            Some("11.0"),
        ),
        (
            Some(os_version("15.7.9")),
            AvailabilityView::Available,
            None,
        ),
        (None, AvailabilityView::Available, None),
    ];
    for (running, availability, min_os) in cases {
        let view = assemble(Os::Macos, Arch::Aarch64, &check, running, &none);
        let alpha = plugin(&view, "Alpha");
        assert_eq!(alpha.availability, availability, "{running:?}");
        assert_eq!(alpha.min_os.as_deref(), min_os, "{running:?}");
        assert_eq!(view.running_os, running.map(|v| v.to_string()));
    }
}

// ---- SC-012 -------------------------------------------------------------------------------------------------------

#[test]
fn golden_views() {
    let dir = goldens_dir();
    let mut problems: Vec<String> = Vec::new();
    let mut expected_texts: Vec<String> = Vec::new();
    for name in SCENARIOS {
        let view = scenario(name);
        let mut expected = serde_json::to_string_pretty(&view).expect("a view serialises");
        expected.push('\n');
        let file = dir.join(format!("{name}.json"));
        let mismatch = match fs::read(&file) {
            Ok(bytes) if bytes == expected.as_bytes() => None,
            Ok(_) => Some("differs"),
            Err(_) => Some("missing"),
        };
        if let Some(why) = mismatch {
            problems.push(format!("{name}.json: {why}"));
            expected_texts.push(format!(
                "----- BEGIN {name}.json -----\n{expected}----- END {name}.json -----"
            ));
        }
        // The rule holds for the view itself, so a golden written from the printed text holds it too.
        let doc: Value = serde_json::from_str(&expected).expect("the expected text is JSON");
        problems.extend(
            os_consistency(name, &doc)
                .into_iter()
                .map(|p| format!("view {p}")),
        );
    }
    // Every committed file is OS-consistent, and the directory holds exactly the 13 scenarios.
    let mut listed = BTreeSet::new();
    if let Ok(entries) = fs::read_dir(&dir) {
        for entry in entries.flatten() {
            let file_name = entry.file_name().to_string_lossy().into_owned();
            if let Ok(text) = fs::read_to_string(entry.path()) {
                match serde_json::from_str::<Value>(&text) {
                    Ok(doc) => problems.extend(os_consistency(&file_name, &doc)),
                    Err(e) => problems.push(format!("{file_name}: not JSON: {e}")),
                }
            }
            listed.insert(file_name);
        }
    }
    let wanted: BTreeSet<String> = SCENARIOS.iter().map(|s| format!("{s}.json")).collect();
    for extra in listed.difference(&wanted) {
        problems.push(format!("{extra}: not a scenario"));
    }
    problems.extend(enum_spelling_problems());
    assert!(
        problems.is_empty(),
        "golden views ({} problem(s)):\n{}\n\nexpected texts:\n{}",
        problems.len(),
        problems.join("\n"),
        expected_texts.join("\n")
    );
}

/// PD-21 (part of `golden_views`): every variant of every view enum against its FR-046 spelling, written as a
/// literal; one finding per mismatch.
fn enum_spelling_problems() -> Vec<String> {
    let os = [
        (json(&OsName::Windows), "\"windows\""),
        (json(&OsName::Macos), "\"macos\""),
        (json(&OsName::Linux), "\"linux\""),
    ];
    let source = [
        (json(&FeedSource::Fresh), "\"fresh\""),
        (json(&FeedSource::Cached), "\"cached\""),
        (json(&FeedSource::None), "\"none\""),
    ];
    let availability = [
        (json(&AvailabilityView::Available), "\"available\""),
        (
            json(&AvailabilityView::NotThroughManager),
            "\"not_through_manager\"",
        ),
        (
            json(&AvailabilityView::NotForThisMachine),
            "\"not_for_this_machine\"",
        ),
        (json(&AvailabilityView::NeedsNewerOs), "\"needs_newer_os\""),
    ];
    let installed = [
        (json(&InstalledView::NotChecked), "\"not_checked\""),
        (json(&InstalledView::NotInstalled), "\"not_installed\""),
        (json(&InstalledView::Elsewhere), "\"elsewhere\""),
        (json(&InstalledView::VersionUnknown), "\"version_unknown\""),
        (json(&InstalledView::Version), "\"version\""),
    ];
    let update = [
        (json(&UpdateView::NotInstalled), "\"not_installed\""),
        (json(&UpdateView::UpToDate), "\"up_to_date\""),
        (json(&UpdateView::UpdateAvailable), "\"update_available\""),
        (json(&UpdateView::InstalledNewer), "\"installed_newer\""),
        (json(&UpdateView::Unknown), "\"unknown\""),
    ];
    let format = [
        (json(&FormatView::Vst3), "\"vst3\""),
        (json(&FormatView::Au), "\"au\""),
    ];
    let place = [
        (json(&PlaceView::Configured), "\"configured\""),
        (json(&PlaceView::Vst3User), "\"vst3_user\""),
        (json(&PlaceView::Vst3Global), "\"vst3_global\""),
        (json(&PlaceView::AuUser), "\"au_user\""),
        (json(&PlaceView::AuGlobal), "\"au_global\""),
        (
            json(&PlaceView::InstallerLocation),
            "\"installer_location\"",
        ),
    ];
    let version_source = [
        (json(&VersionSourceView::Classes), "\"classes\""),
        (json(&VersionSourceView::TopLevel), "\"top_level\""),
        (json(&VersionSourceView::AuPlist), "\"au_plist\""),
    ];
    let reason = [
        (json(&UnknownReasonView::SingleFile), "\"single_file\""),
        (json(&UnknownReasonView::NoModuleinfo), "\"no_moduleinfo\""),
        (json(&UnknownReasonView::TooLarge), "\"too_large\""),
        (json(&UnknownReasonView::Unreadable), "\"unreadable\""),
        (json(&UnknownReasonView::Corrupt), "\"corrupt\""),
        (
            json(&UnknownReasonView::ClassesDisagree),
            "\"classes_disagree\"",
        ),
        (json(&UnknownReasonView::NoVersion), "\"no_version\""),
        (json(&UnknownReasonView::NotABundle), "\"not_a_bundle\""),
        (json(&UnknownReasonView::AuNoVersion), "\"au_no_version\""),
    ];
    let kind = [
        (json(&RecordKind::Inno), "\"inno\""),
        (json(&RecordKind::Pkg), "\"pkg\""),
        (json(&RecordKind::Manifest), "\"manifest\""),
    ];
    let all: Vec<&(String, &str)> = os
        .iter()
        .chain(&source)
        .chain(&availability)
        .chain(&installed)
        .chain(&update)
        .chain(&format)
        .chain(&place)
        .chain(&version_source)
        .chain(&reason)
        .chain(&kind)
        .collect();
    let mut out = Vec::new();
    if all.len() != 43 {
        out.push(format!("{} enum spellings checked, expected 43", all.len()));
    }
    for (got, want) in all {
        if got != want {
            out.push(format!("enum spelling {got}, expected {want}"));
        }
    }
    // The core reasons reach the view through one exhaustive `match`, the nine FR-046 values in order.
    let core_reasons = [
        UnknownReason::SingleFile,
        UnknownReason::NoModuleInfo,
        UnknownReason::TooLarge,
        UnknownReason::Unreadable,
        UnknownReason::Corrupt,
        UnknownReason::ClassesDisagree,
        UnknownReason::NoVersion,
        UnknownReason::NotABundle,
        UnknownReason::AuNoVersion,
    ];
    let mapped: Vec<String> = core_reasons
        .iter()
        .map(|r| json(&unknown_reason_view(*r)))
        .collect();
    let spelled: Vec<String> = reason.iter().map(|(_, want)| (*want).to_owned()).collect();
    if mapped != spelled {
        out.push(format!(
            "core reasons map to {mapped:?}, expected {spelled:?}"
        ));
    }
    out
}

// ---- SC-018 -------------------------------------------------------------------------------------------------------

#[test]
fn build_cases() {
    install_capture();
    let _ = take_mine();
    let server = FixtureServer::start(vec![(FEED_PATH.to_owned(), ok(feed_fixture(FULL)))]);

    // A served feed → fresh; `Normal` sends no `Cache-Control`.
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    let view = build(&ctx, FetchMode::Normal, NOW, &FakeHostFacts::default());
    assert_eq!(view.feed.source, FeedSource::Fresh, "{:?}", view.feed.error);
    assert_eq!(view.feed.fetched_at_unix, Some(NOW));
    assert_eq!(view.feed.schema_version, Some(2));
    assert_eq!(view.feed.error, None);
    assert_eq!(view.plugins.len(), 3);
    let last = server.requests().last().cloned().expect("one request");
    assert_eq!(header(&last, "Cache-Control"), None);
    assert_eq!(
        status_infos(&take_mine()),
        vec!["status: 3 plugins, 0 copies, feed fresh".to_owned()]
    );

    // `CheckNow` sends `Cache-Control: no-cache`.
    let view = build(
        &ctx,
        FetchMode::CheckNow,
        NOW + 1,
        &FakeHostFacts::default(),
    );
    assert_eq!(view.feed.source, FeedSource::Fresh, "{:?}", view.feed.error);
    let last = server.requests().last().cloned().expect("a second request");
    assert_eq!(header(&last, "Cache-Control"), Some("no-cache"));
    assert_eq!(status_infos(&take_mine()).len(), 1);

    // The feed fails after an accepted fetch → cached, with the error text, and detection still runs over it.
    write_bundle(
        &managed_bundle(&ctx.roots, &ctx.settings, "Alpha.vst3"),
        "Alpha",
        "1.2.0",
        "1.2.0.1",
    );
    server.set_route(FEED_PATH, Reply::Close);
    let view = build(&ctx, FetchMode::Normal, NOW + 2, &FakeHostFacts::default());
    assert_eq!(view.feed.source, FeedSource::Cached);
    assert_eq!(view.feed.fetched_at_unix, Some(NOW + 1));
    let error = view.feed.error.clone().expect("the error text");
    assert!(error.starts_with("E_FEED_"), "{error}");
    assert_eq!(view.plugins.len(), 3);
    let alpha = plugin(&view, "Alpha");
    assert_eq!(alpha.installed, InstalledView::Version, "{alpha:?}");
    assert_eq!(alpha.installed_version.as_deref(), Some("1.2.0"));
    assert_eq!(
        status_infos(&take_mine()),
        vec!["status: 3 plugins, 1 copies, feed cached".to_owned()]
    );

    // No cache and the feed fails → none, no cards.
    let (_td2, roots2) = fresh_temp_roots();
    let ctx2 = context(&roots2, server.url(FEED_PATH));
    let view = build(&ctx2, FetchMode::Normal, NOW, &FakeHostFacts::default());
    assert_eq!(view.feed.source, FeedSource::None);
    assert_eq!(view.feed.fetched_at_unix, None);
    assert_eq!(view.feed.schema_version, None);
    assert!(view.feed.error.is_some());
    assert!(view.plugins.is_empty());
    assert_eq!(
        status_infos(&take_mine()),
        vec!["status: 0 plugins, 0 copies, feed none".to_owned()]
    );
}

/// FR-048: two builds at once over one tree, each with its own fake, each equal to a single build.
#[test]
fn concurrent_builds() {
    let server = FixtureServer::start(vec![(FEED_PATH.to_owned(), ok(feed_fixture(FULL)))]);
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    write_bundle(
        &managed_bundle(&ctx.roots, &ctx.settings, "Alpha.vst3"),
        "Alpha",
        "1.2.0",
        "1.2.0.1",
    );
    let single_host = FakeHostFacts::default();
    let single = build(&ctx, FetchMode::Normal, NOW, &single_host);
    assert_eq!(
        single.feed.source,
        FeedSource::Fresh,
        "{:?}",
        single.feed.error
    );
    let single_calls = single_host.calls.borrow().clone();
    let barrier = Barrier::new(2);
    let results: Vec<(StatusView, Vec<String>)> = thread::scope(|s| {
        let handles: Vec<_> = (0..2)
            .map(|_| {
                let ctx = ctx.clone();
                let barrier = &barrier;
                s.spawn(move || {
                    let host = FakeHostFacts::default();
                    barrier.wait();
                    let view = build(&ctx, FetchMode::Normal, NOW, &host);
                    let calls = host.calls.borrow().clone();
                    (view, calls)
                })
            })
            .collect();
        handles
            .into_iter()
            .map(|h| h.join().expect("a build thread"))
            .collect()
    });
    for (view, calls) in results {
        assert_eq!(view, single);
        assert_eq!(calls, single_calls);
    }
}

// ---- SC-030 -------------------------------------------------------------------------------------------------------

#[test]
fn listing_runs_once_per_root() {
    install_capture();
    let _ = take_mine();
    let server = FixtureServer::start(vec![(FEED_PATH.to_owned(), ok(feed_fixture(TENTH)))]);
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));
    let view = build(&ctx, FetchMode::Normal, NOW, &FakeHostFacts::default());
    assert_eq!(view.plugins.len(), 10, "{:?}", view.feed.error);
    let listing_lines: Vec<String> = take_mine()
        .into_iter()
        .filter(|(target, level, message)| {
            target == DETECT_TARGET && *level == Level::Debug && message.starts_with("listing:")
        })
        .map(|(_, _, message)| message)
        .collect();
    let listed: Vec<RootKind> = roots
        .plugin_roots()
        .iter()
        .map(|r| r.kind)
        .filter(|k| !matches!(k, RootKind::AuUser | RootKind::AuGlobal))
        .collect();
    assert_eq!(listing_lines.len(), listed.len(), "{listing_lines:?}");
    for kind in listed {
        let prefix = format!("listing: {kind:?}:");
        assert_eq!(
            listing_lines
                .iter()
                .filter(|l| l.starts_with(&prefix))
                .count(),
            1,
            "{prefix} in {listing_lines:?}"
        );
    }
}

// ---- SC-032 -------------------------------------------------------------------------------------------------------

#[test]
fn release_page_url_cases() {
    let accepted = [
        (
            "https://github.com/example/repo/releases/tag/alpha/v1.2.0",
            "https://github.com/example/repo/releases/tag/alpha/v1.2.0",
        ),
        (
            "https://GitHub.com/example/x",
            "https://github.com/example/x",
        ),
        (
            "https://github.com:443/example/x",
            "https://github.com/example/x",
        ),
    ];
    for (text, want) in accepted {
        assert_eq!(release_page_url(text).as_deref(), Some(want), "{text}");
    }
    let api_host = format!("https://api.{}/x", RELEASE_PAGE_HOST);
    let long = format!(
        "https://github.com/{}",
        "a".repeat(2049 - "https://github.com/".len())
    );
    assert_eq!(long.len(), 2049);
    let at_limit = format!(
        "https://github.com/{}",
        "a".repeat(2048 - "https://github.com/".len())
    );
    assert_eq!(
        release_page_url(&at_limit).as_deref(),
        Some(at_limit.as_str())
    );
    let refused = [
        "http://github.com/example/x",
        "https://github.com.example.test/x",
        "https://example.test/github.com",
        api_host.as_str(),
        "https://user@github.com/x",
        "https://user:secret@github.com/x",
        "https://github.com:8443/x",
        "javascript:alert(1)",
        "file:///x",
        "//github.com/x",
        "",
        "not a url",
        long.as_str(),
    ];
    for text in refused {
        assert_eq!(release_page_url(text), None, "{text}");
    }

    // Through `assemble`: Alpha's page is its `release_notes_url`; a v1 entry has none.
    let full = parsed(FULL);
    let notes_url = full.plugins["Alpha"]
        .release
        .as_ref()
        .map(|r| r.release_notes_url.clone())
        .expect("Alpha has a release");
    let view = assemble(
        Os::Windows,
        Arch::X86_64,
        &fresh(full),
        None,
        &BTreeMap::new(),
    );
    assert_eq!(plugin(&view, "Alpha").release_page_url, Some(notes_url));
    let view = assemble(
        Os::Windows,
        Arch::X86_64,
        &fresh(parsed(V1_ONLY)),
        None,
        &BTreeMap::new(),
    );
    assert_eq!(view.plugins.len(), 3);
    for p in &view.plugins {
        assert_eq!(p.release_page_url, None, "{p:?}");
    }
}

// ---- Phase 12 SC-035 ----------------------------------------------------------------------------------------------

#[test]
fn plugin_page_url_cases() {
    let accepted = [
        (
            "https://krateaudio.com/vorago/",
            "https://krateaudio.com/vorago/",
        ),
        ("https://KrateAudio.com/x", "https://krateaudio.com/x"),
        ("https://krateaudio.com:443/x", "https://krateaudio.com/x"),
    ];
    for (text, want) in accepted {
        assert_eq!(plugin_page_url(text).as_deref(), Some(want), "{text}");
    }
    let long = format!(
        "https://krateaudio.com/{}",
        "a".repeat(2049 - "https://krateaudio.com/".len())
    );
    assert_eq!(long.len(), 2049);
    let refused = [
        "http://krateaudio.com/x",
        "https://krateaudio.com.example.test/x",
        "https://www.krateaudio.com/x",
        "https://example.test/krateaudio.com",
        "https://user@krateaudio.com/x",
        "https://user:secret@krateaudio.com/x",
        "https://krateaudio.com:8443/x",
        "javascript:alert(1)",
        "",
        long.as_str(),
    ];
    for text in refused {
        assert_eq!(plugin_page_url(text), None, "{text}");
        assert_ne!(
            open_url(text).as_deref(),
            Some(text),
            "open_url passes {text}"
        );
    }
    assert_eq!(plugin_page_url("https://github.com/example/x"), None);
    // The open command takes either page; nothing else.
    for page in [
        "https://krateaudio.com/vorago/",
        "https://github.com/example/repo/releases/tag/alpha/v1.2.0",
    ] {
        assert_eq!(open_url(page).as_deref(), Some(page), "{page}");
    }
    assert_eq!(open_url("https://example.test/x"), None);

    // Through `assemble`: each card's page is its `download_url`, in a v2 and a v1 feed alike.
    for fixture in [FULL, V1_ONLY] {
        let view = assemble(
            Os::Windows,
            Arch::X86_64,
            &fresh(parsed(fixture)),
            None,
            &BTreeMap::new(),
        );
        assert_eq!(view.plugins.len(), 3, "{fixture}");
        for p in &view.plugins {
            let want = format!("https://krateaudio.com/{}/", p.name.to_lowercase());
            assert_eq!(p.plugin_page_url, Some(want), "{fixture}: {p:?}");
        }
    }
}

// ---- Phase 9 SC-016 -----------------------------------------------------------------------------------------------

/// The feed version of every `actions_per_state` row.
const LATEST: &str = "1.2.0";
/// The running glibc; a row's `min_os` above it is `needs_newer_os`.
const RUNNING_GLIBC: &str = "2.31";
/// The bytes every row's asset is signed over; no row ever fetches its asset.
const NEVER_FETCHED: &[u8] = b"never fetched";

const NOT_NEWER: &str = "E_INSTALL_NOT_NEWER";
const STATE_CHANGED: &str = "E_INSTALL_STATE_CHANGED";
const NOT_MANAGED: &str = "E_INSTALL_NOT_MANAGED";
const UNAVAILABLE: &str = "E_DOWNLOAD_UNAVAILABLE";
const JOURNAL_PENDING: &str = "E_INSTALL_JOURNAL_PENDING";
const BAD_MANIFEST: &str = "E_INSTALL_MANIFEST";
const ROOT_CHANGED: &str = "E_INSTALL_ROOT_CHANGED";

/// The bundle a row plants before the builds.
#[derive(Clone, Copy)]
enum RowDisk {
    Absent,
    /// At the managed Linux location, with a moduleinfo of this version.
    Managed(&'static str),
    /// At the managed Linux location, without a moduleinfo (version unknown).
    ManagedNoModuleinfo,
    /// Under the `Vst3Global` root only, with a moduleinfo of this version (FR-124's `elsewhere`).
    Elsewhere(&'static str),
}

/// The manifest a row plants at `<state>/manifests/<stem>.json`, recording this version.
#[derive(Clone, Copy)]
enum RowManifest {
    Absent,
    Valid(&'static str),
    /// One entry path with a `..` segment (FR-041).
    DotDot(&'static str),
    /// `plugin_root` names the `Vst3Global` root, not the configured one (FR-064).
    OtherRoot(&'static str),
}

/// One plugin of the SC-016 table: its feed asset, what is on disk, what the Linux card shows, and the code each
/// action the card omits is refused with (`install`, `update`, `remove` order).
struct StateRow {
    name: &'static str,
    key: PlatformKey,
    min_os: Option<&'static str>,
    disk: RowDisk,
    manifest: RowManifest,
    journal: bool,
    availability: AvailabilityView,
    installed: InstalledView,
    update: UpdateView,
    actions: &'static [ActionView],
    refusals: &'static [(&'static str, &'static str)],
}

/// The Phase 6 fixture states rebuilt on disk, plus the three gate rows (spec SC-016, FR-093, FR-094).
const STATE_ROWS: [StateRow; 14] = [
    StateRow {
        name: "Alpha",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Absent,
        manifest: RowManifest::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::NotInstalled,
        update: UpdateView::NotInstalled,
        actions: &[ActionView::Install],
        refusals: &[("update", STATE_CHANGED), ("remove", NOT_MANAGED)],
    },
    StateRow {
        name: "Bravo",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Elsewhere("1.1.0"),
        manifest: RowManifest::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Elsewhere,
        update: UpdateView::NotInstalled,
        actions: &[ActionView::Install],
        refusals: &[("update", STATE_CHANGED), ("remove", NOT_MANAGED)],
    },
    StateRow {
        name: "Charlie",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Managed("1.2.0"),
        manifest: RowManifest::Valid("1.2.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpToDate,
        actions: &[ActionView::Remove],
        refusals: &[("install", NOT_NEWER), ("update", NOT_NEWER)],
    },
    StateRow {
        name: "Delta",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Managed("1.2.0"),
        manifest: RowManifest::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpToDate,
        actions: &[],
        refusals: &[
            ("install", NOT_NEWER),
            ("update", NOT_NEWER),
            ("remove", NOT_MANAGED),
        ],
    },
    StateRow {
        name: "Echo",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Managed("1.1.0"),
        manifest: RowManifest::Valid("1.1.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpdateAvailable,
        actions: &[ActionView::Update, ActionView::Remove],
        refusals: &[("install", STATE_CHANGED)],
    },
    StateRow {
        name: "Foxtrot",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Managed("1.1.0"),
        manifest: RowManifest::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpdateAvailable,
        actions: &[ActionView::Update],
        refusals: &[("install", STATE_CHANGED), ("remove", NOT_MANAGED)],
    },
    StateRow {
        name: "Golf",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Managed("1.3.0"),
        manifest: RowManifest::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::InstalledNewer,
        actions: &[],
        refusals: &[
            ("install", NOT_NEWER),
            ("update", NOT_NEWER),
            ("remove", NOT_MANAGED),
        ],
    },
    StateRow {
        name: "Hotel",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Managed("1.3.0"),
        manifest: RowManifest::Valid("1.3.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::InstalledNewer,
        actions: &[ActionView::Remove],
        refusals: &[("install", NOT_NEWER), ("update", NOT_NEWER)],
    },
    StateRow {
        name: "India",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::ManagedNoModuleinfo,
        manifest: RowManifest::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::VersionUnknown,
        update: UpdateView::Unknown,
        actions: &[],
        refusals: &[
            ("install", STATE_CHANGED),
            ("update", STATE_CHANGED),
            ("remove", NOT_MANAGED),
        ],
    },
    StateRow {
        name: "Juliet",
        key: PlatformKey::WindowsX86_64,
        min_os: None,
        disk: RowDisk::Absent,
        manifest: RowManifest::Absent,
        journal: false,
        availability: AvailabilityView::NotForThisMachine,
        installed: InstalledView::NotInstalled,
        update: UpdateView::NotInstalled,
        actions: &[],
        refusals: &[
            ("install", UNAVAILABLE),
            ("update", UNAVAILABLE),
            ("remove", NOT_MANAGED),
        ],
    },
    StateRow {
        name: "Kilo",
        key: PlatformKey::LinuxX86_64,
        min_os: Some("2.99"),
        disk: RowDisk::Absent,
        manifest: RowManifest::Absent,
        journal: false,
        availability: AvailabilityView::NeedsNewerOs,
        installed: InstalledView::NotInstalled,
        update: UpdateView::NotInstalled,
        actions: &[],
        refusals: &[
            ("install", UNAVAILABLE),
            ("update", UNAVAILABLE),
            ("remove", NOT_MANAGED),
        ],
    },
    // The three gate rows sit on an update-available managed install with a manifest, which would offer
    // `["update", "remove"]` if the gate were open (Echo).
    StateRow {
        name: "Lima",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Managed("1.1.0"),
        manifest: RowManifest::Valid("1.1.0"),
        journal: true,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpdateAvailable,
        actions: &[],
        refusals: &[
            ("install", JOURNAL_PENDING),
            ("update", JOURNAL_PENDING),
            ("remove", JOURNAL_PENDING),
        ],
    },
    StateRow {
        name: "Mike",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Managed("1.1.0"),
        manifest: RowManifest::DotDot("1.1.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpdateAvailable,
        actions: &[],
        refusals: &[
            ("install", BAD_MANIFEST),
            ("update", BAD_MANIFEST),
            ("remove", BAD_MANIFEST),
        ],
    },
    StateRow {
        name: "November",
        key: PlatformKey::LinuxX86_64,
        min_os: None,
        disk: RowDisk::Managed("1.1.0"),
        manifest: RowManifest::OtherRoot("1.1.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpdateAvailable,
        actions: &[],
        refusals: &[
            ("install", ROOT_CHANGED),
            ("update", ROOT_CHANGED),
            ("remove", ROOT_CHANGED),
        ],
    },
];

/// The asset file name of a row, as the release pipeline names it.
fn row_asset(row: &StateRow) -> String {
    asset_name(row.name, row.key)
}

/// The asset file name of `name`'s one asset under `key`, as the release pipeline names it.
fn asset_name(name: &str, key: PlatformKey) -> String {
    match key {
        PlatformKey::WindowsX86_64 => format!("{name}-{LATEST}-Windows-x64.exe"),
        PlatformKey::DarwinAarch64 | PlatformKey::DarwinX86_64 | PlatformKey::DarwinUniversal => {
            format!("{name}-{LATEST}-macOS.pkg")
        }
        PlatformKey::LinuxX86_64 => format!("{name}-{LATEST}-Linux-x64.tar.gz"),
    }
}

/// The contract-valid feed of every row, each with its one asset signed by `signer`.
fn rows_feed(signer: &TestSigner) -> Vec<u8> {
    let assets: Vec<String> = STATE_ROWS.iter().map(row_asset).collect();
    let plugins: Vec<TestPlugin<'_>> = STATE_ROWS
        .iter()
        .zip(&assets)
        .map(|(row, asset)| TestPlugin {
            name: row.name,
            version: LATEST,
            key: row.key,
            asset,
            size: NEVER_FETCHED.len() as u64,
            sha256: sha256_of(NEVER_FETCHED),
            signature: signer.sign_c3(NEVER_FETCHED, row.name, LATEST, row.key, asset),
            min_os: row.min_os,
        })
        .collect();
    test_feed_document(&plugins)
}

/// A temp root as the manifest records it (FR-040: absolute UTF-8).
fn root_text(path: &Path) -> String {
    path.to_str()
        .unwrap_or_else(|| panic!("a temp root is not UTF-8: {}", path.display()))
        .to_owned()
}

/// Plants a row's bundle, pending journal (`<state>/journal/<stem>.json`, FR-052) and manifest, only inside the
/// context's temp roots.
fn plant_row(ctx: &AppContext, row: &StateRow) {
    let managed_root = ctx.settings.linux_plugin_root_or_default(&ctx.roots);
    let bundle = format!("{}.vst3", row.name);
    match row.disk {
        RowDisk::Absent => {}
        RowDisk::Managed(v) => {
            write_bundle(&managed_root.join(&bundle), row.name, v, &format!("{v}.1"))
        }
        RowDisk::ManagedNoModuleinfo => {
            let contents = managed_root.join(&bundle).join("Contents");
            fs::create_dir_all(&contents)
                .unwrap_or_else(|e| panic!("create {}: {e}", contents.display()));
        }
        RowDisk::Elsewhere(v) => write_bundle(
            &root_of(&ctx.roots, RootKind::Vst3Global).join(&bundle),
            row.name,
            v,
            &format!("{v}.1"),
        ),
    }
    if row.journal {
        plant_journal(ctx, row.name);
    }
    let (version, recorded_root, dot_dot) = match row.manifest {
        RowManifest::Absent => return,
        RowManifest::Valid(v) => (v, managed_root.to_path_buf(), false),
        RowManifest::DotDot(v) => (v, managed_root.to_path_buf(), true),
        RowManifest::OtherRoot(v) => (v, root_of(&ctx.roots, RootKind::Vst3Global), false),
    };
    // The bundle's full listing as `write_bundle` placed it (FR-040).
    let moduleinfo = format!("{bundle}/Contents/Resources/moduleinfo.json");
    let bytes = fs::read(managed_root.join(&moduleinfo))
        .unwrap_or_else(|e| panic!("read the {} moduleinfo: {e}", row.name));
    let dir = |path: String| ManifestEntry {
        root: RootTag::Plugin,
        kind: EntryTag::D,
        path,
        sha256: None,
        target: None,
    };
    let file = |path: String| ManifestEntry {
        root: RootTag::Plugin,
        kind: EntryTag::F,
        path,
        sha256: Some(hex_lower(&sha256_of(&bytes))),
        target: None,
    };
    let mut files = vec![
        dir(bundle.clone()),
        dir(format!("{bundle}/Contents")),
        dir(format!("{bundle}/Contents/Resources")),
        file(moduleinfo),
    ];
    if dot_dot {
        files.push(file(format!("{bundle}/../{}.txt", row.name)));
    }
    let manifest = Manifest {
        manifest_version: 1,
        plugin: row.name.to_owned(),
        version: version.to_owned(),
        vst3_bundle: bundle,
        archive_sha256: hex_lower(&sha256_of(NEVER_FETCHED)),
        plugin_root: root_text(&recorded_root),
        preset_root: root_text(&ctx.roots.presets().user),
        files,
    };
    let manifests = ctx.roots.state_dir().join(detect::MANIFESTS_DIR);
    fs::create_dir_all(&manifests)
        .unwrap_or_else(|e| panic!("create {}: {e}", manifests.display()));
    let path = manifests.join(format!("{}.json", row.name));
    let text = serde_json::to_vec_pretty(&manifest).expect("a manifest serialises");
    fs::write(&path, text).unwrap_or_else(|e| panic!("write {}: {e}", path.display()));
}

/// A pending journal for `name` (`<state>/journal/<stem>.json`, FR-052), inside the context's temp state dir.
fn plant_journal(ctx: &AppContext, name: &str) {
    let dir = ctx.roots.state_dir().join(install::JOURNAL_DIR);
    fs::create_dir_all(&dir).unwrap_or_else(|e| panic!("create {}: {e}", dir.display()));
    let path = dir.join(format!("{name}.json"));
    fs::write(&path, b"{}").unwrap_or_else(|e| panic!("write {}: {e}", path.display()));
}

// ---- Phase 10 SC-013 (FR-080 to FR-083) ---------------------------------------------------------------------------

/// The running Windows build; a row's `min_os` above it is `needs_newer_os`.
const WINDOWS_RUNNING: &str = "10.0.19045";
const WINDOWS_NEWER: &str = "10.0.26100";
/// The running macOS; the test feed's default `min_os` for a `darwin-*` asset is 15.0.
const MACOS_RUNNING: &str = "15.7.9";
const MACOS_NEWER: &str = "26.0";
const REMOVE_ELSEWHERE: &str = "E_INSTALL_REMOVE_ELSEWHERE";

/// Which asset an elevated row's feed entry carries.
#[derive(Clone, Copy)]
enum Offer {
    /// This machine's platform key.
    Native,
    /// Another machine's key only (`not_for_this_machine`).
    Foreign,
    /// This machine's key with a `min_os` above the running OS (`needs_newer_os`).
    NeedsNewer,
}

/// The bundle an elevated row plants before the builds.
#[derive(Clone, Copy)]
enum ElevatedDisk {
    Absent,
    /// At the managed location (Windows `<Vst3Global>/<vendor>/<Name>/`, macOS `<Vst3Global>/`, FR-001), with a
    /// moduleinfo of this version.
    Managed(&'static str),
    /// At the managed location, without a moduleinfo (version unknown).
    ManagedNoModuleinfo,
    /// Under the `Vst3User` root only, with a moduleinfo of this version.
    Elsewhere(&'static str),
    /// In `<sandbox>/elsewhere/<Name>/` (the outside record's folder, SC-012), with a moduleinfo of this version.
    Outside(&'static str),
}

/// The installer record an elevated row's `FakeHostFacts` answers with, recording this version.
#[derive(Clone, Copy)]
enum ElevatedRecord {
    Absent,
    /// Windows: an Inno record whose `InstallLocation` is the managed folder; macOS: a pkg receipt.
    Managed(&'static str),
    /// Windows only: an Inno record whose `InstallLocation` is `<sandbox>/elsewhere/<Name>/`, inside the temp
    /// sandbox and outside `<Vst3Global>/<vendor>` (SC-012), never a real path.
    Outside(&'static str),
}

/// One plugin of the SC-013 table on Windows and macOS: its asset, what is on disk, its record, what the card shows,
/// and the code each action the card omits is refused with (`install`, `update`, `remove` order).
struct ElevatedRow {
    name: &'static str,
    offer: Offer,
    disk: ElevatedDisk,
    record: ElevatedRecord,
    journal: bool,
    availability: AvailabilityView,
    installed: InstalledView,
    update: UpdateView,
    actions: &'static [ActionView],
    /// `remove_hint` is `"windows_settings"` (FR-083).
    windows_settings: bool,
    refusals: &'static [(&'static str, &'static str)],
}

/// Phase 6's fixture states with a Windows / macOS record where the state has one (spec SC-013), the journal gate,
/// and the two outside-root rows (Windows only).
const ELEVATED_ROWS: [ElevatedRow; 16] = [
    ElevatedRow {
        name: "Alpha",
        offer: Offer::Native,
        disk: ElevatedDisk::Absent,
        record: ElevatedRecord::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::NotInstalled,
        update: UpdateView::NotInstalled,
        actions: &[ActionView::Install],
        windows_settings: false,
        refusals: &[("update", STATE_CHANGED), ("remove", NOT_MANAGED)],
    },
    ElevatedRow {
        name: "Bravo",
        offer: Offer::Native,
        disk: ElevatedDisk::Elsewhere("1.1.0"),
        record: ElevatedRecord::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Elsewhere,
        update: UpdateView::NotInstalled,
        actions: &[ActionView::Install],
        windows_settings: false,
        refusals: &[("update", STATE_CHANGED), ("remove", NOT_MANAGED)],
    },
    ElevatedRow {
        name: "Charlie",
        offer: Offer::Native,
        disk: ElevatedDisk::Managed("1.2.0"),
        record: ElevatedRecord::Managed("1.2.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpToDate,
        actions: &[ActionView::Remove],
        windows_settings: false,
        refusals: &[("install", NOT_NEWER), ("update", NOT_NEWER)],
    },
    ElevatedRow {
        name: "Delta",
        offer: Offer::Native,
        disk: ElevatedDisk::Managed("1.2.0"),
        record: ElevatedRecord::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpToDate,
        actions: &[],
        windows_settings: false,
        refusals: &[
            ("install", NOT_NEWER),
            ("update", NOT_NEWER),
            ("remove", NOT_MANAGED),
        ],
    },
    ElevatedRow {
        name: "Echo",
        offer: Offer::Native,
        disk: ElevatedDisk::Managed("1.1.0"),
        record: ElevatedRecord::Managed("1.1.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpdateAvailable,
        actions: &[ActionView::Update, ActionView::Remove],
        windows_settings: false,
        refusals: &[("install", STATE_CHANGED)],
    },
    ElevatedRow {
        name: "Foxtrot",
        offer: Offer::Native,
        disk: ElevatedDisk::Managed("1.1.0"),
        record: ElevatedRecord::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpdateAvailable,
        actions: &[ActionView::Update],
        windows_settings: false,
        refusals: &[("install", STATE_CHANGED), ("remove", NOT_MANAGED)],
    },
    ElevatedRow {
        name: "Golf",
        offer: Offer::Native,
        disk: ElevatedDisk::Managed("1.3.0"),
        record: ElevatedRecord::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::InstalledNewer,
        actions: &[],
        windows_settings: false,
        refusals: &[
            ("install", NOT_NEWER),
            ("update", NOT_NEWER),
            ("remove", NOT_MANAGED),
        ],
    },
    ElevatedRow {
        name: "Hotel",
        offer: Offer::Native,
        disk: ElevatedDisk::Managed("1.3.0"),
        record: ElevatedRecord::Managed("1.3.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::InstalledNewer,
        actions: &[ActionView::Remove],
        windows_settings: false,
        refusals: &[("install", NOT_NEWER), ("update", NOT_NEWER)],
    },
    ElevatedRow {
        name: "India",
        offer: Offer::Native,
        disk: ElevatedDisk::ManagedNoModuleinfo,
        record: ElevatedRecord::Absent,
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::VersionUnknown,
        update: UpdateView::Unknown,
        actions: &[],
        windows_settings: false,
        refusals: &[
            ("install", STATE_CHANGED),
            ("update", STATE_CHANGED),
            ("remove", NOT_MANAGED),
        ],
    },
    ElevatedRow {
        name: "Juliet",
        offer: Offer::Foreign,
        disk: ElevatedDisk::Absent,
        record: ElevatedRecord::Absent,
        journal: false,
        availability: AvailabilityView::NotForThisMachine,
        installed: InstalledView::NotInstalled,
        update: UpdateView::NotInstalled,
        actions: &[],
        windows_settings: false,
        refusals: &[
            ("install", UNAVAILABLE),
            ("update", UNAVAILABLE),
            ("remove", NOT_MANAGED),
        ],
    },
    ElevatedRow {
        name: "Kilo",
        offer: Offer::NeedsNewer,
        disk: ElevatedDisk::Absent,
        record: ElevatedRecord::Absent,
        journal: false,
        availability: AvailabilityView::NeedsNewerOs,
        installed: InstalledView::NotInstalled,
        update: UpdateView::NotInstalled,
        actions: &[],
        windows_settings: false,
        refusals: &[
            ("install", UNAVAILABLE),
            ("update", UNAVAILABLE),
            ("remove", NOT_MANAGED),
        ],
    },
    // The journal gate sits on an update-available managed install with a record, which would offer
    // `["update", "remove"]` if the gate were open (Echo).
    ElevatedRow {
        name: "Lima",
        offer: Offer::Native,
        disk: ElevatedDisk::Managed("1.1.0"),
        record: ElevatedRecord::Managed("1.1.0"),
        journal: true,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpdateAvailable,
        actions: &[],
        windows_settings: false,
        refusals: &[
            ("install", JOURNAL_PENDING),
            ("update", JOURNAL_PENDING),
            ("remove", JOURNAL_PENDING),
        ],
    },
    // Not available / needs a newer OS with a record: `remove` only.
    ElevatedRow {
        name: "Mike",
        offer: Offer::Foreign,
        disk: ElevatedDisk::Absent,
        record: ElevatedRecord::Managed("1.1.0"),
        journal: false,
        availability: AvailabilityView::NotForThisMachine,
        installed: InstalledView::NotInstalled,
        update: UpdateView::NotInstalled,
        actions: &[ActionView::Remove],
        windows_settings: false,
        refusals: &[("install", UNAVAILABLE), ("update", UNAVAILABLE)],
    },
    ElevatedRow {
        name: "November",
        offer: Offer::NeedsNewer,
        disk: ElevatedDisk::Absent,
        record: ElevatedRecord::Managed("1.1.0"),
        journal: false,
        availability: AvailabilityView::NeedsNewerOs,
        installed: InstalledView::NotInstalled,
        update: UpdateView::NotInstalled,
        actions: &[ActionView::Remove],
        windows_settings: false,
        refusals: &[("install", UNAVAILABLE), ("update", UNAVAILABLE)],
    },
    // Windows only: a record outside the root (FR-080 `OutsideRoot`) offers no `remove` and points to Settings.
    ElevatedRow {
        name: "Oscar",
        offer: Offer::Native,
        disk: ElevatedDisk::Outside("1.1.0"),
        record: ElevatedRecord::Outside("1.1.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpdateAvailable,
        actions: &[ActionView::Update],
        windows_settings: true,
        refusals: &[("install", STATE_CHANGED), ("remove", REMOVE_ELSEWHERE)],
    },
    ElevatedRow {
        name: "Papa",
        offer: Offer::Native,
        disk: ElevatedDisk::Outside("1.2.0"),
        record: ElevatedRecord::Outside("1.2.0"),
        journal: false,
        availability: AvailabilityView::Available,
        installed: InstalledView::Version,
        update: UpdateView::UpToDate,
        actions: &[],
        windows_settings: true,
        refusals: &[
            ("install", NOT_NEWER),
            ("update", NOT_NEWER),
            ("remove", REMOVE_ELSEWHERE),
        ],
    },
];

/// The rows `os` runs: every row on Windows; on macOS all but the outside-root rows (a pkg record has no
/// `InstallLocation`).
fn elevated_rows(os: Os) -> Vec<&'static ElevatedRow> {
    ELEVATED_ROWS
        .iter()
        .filter(|row| os == Os::Windows || !matches!(row.record, ElevatedRecord::Outside(_)))
        .collect()
}

/// The running OS version of `os` and the `min_os` a `NeedsNewer` row's asset carries.
fn elevated_os(os: Os) -> (&'static str, &'static str) {
    match os {
        Os::Windows => (WINDOWS_RUNNING, WINDOWS_NEWER),
        _ => (MACOS_RUNNING, MACOS_NEWER),
    }
}

/// The platform key of a row's one asset on `os` (Windows x86_64 or macOS aarch64, `version::platform_candidates`).
fn elevated_key(os: Os, offer: Offer) -> PlatformKey {
    match (os, offer) {
        (Os::Windows, Offer::Foreign) => PlatformKey::LinuxX86_64,
        (Os::Windows, _) => PlatformKey::WindowsX86_64,
        (_, Offer::Foreign) => PlatformKey::DarwinX86_64,
        _ => PlatformKey::DarwinAarch64,
    }
}

/// The contract-valid feed of `os`'s rows, each with its one asset signed by `signer`. On Windows a `Foreign` row
/// (a Linux asset, which the test feed gives no uninstall key) gets a `windows_uninstall_key` of its own, outside the
/// builder's key family, so its record can be found.
fn elevated_feed(os: Os, signer: &TestSigner) -> Vec<u8> {
    let rows = elevated_rows(os);
    let (_, newer) = elevated_os(os);
    let assets: Vec<String> = rows
        .iter()
        .map(|row| asset_name(row.name, elevated_key(os, row.offer)))
        .collect();
    let plugins: Vec<TestPlugin<'_>> = rows
        .iter()
        .zip(&assets)
        .map(|(row, asset)| {
            let key = elevated_key(os, row.offer);
            TestPlugin {
                name: row.name,
                version: LATEST,
                key,
                asset,
                size: NEVER_FETCHED.len() as u64,
                sha256: sha256_of(NEVER_FETCHED),
                signature: signer.sign_c3(NEVER_FETCHED, row.name, LATEST, key, asset),
                min_os: matches!(row.offer, Offer::NeedsNewer).then_some(newer),
            }
        })
        .collect();
    let bytes = test_feed_document(&plugins);
    if os != Os::Windows {
        return bytes;
    }
    let mut doc: Value = serde_json::from_slice(&bytes).expect("the test feed is JSON");
    for (n, row) in rows.iter().enumerate() {
        if matches!(row.offer, Offer::Foreign) {
            doc["plugins"][row.name]["install"]["windows_uninstall_key"] =
                Value::String(format!("{{FFFFFFFF-0000-4000-8000-{:012X}}}_is1", n + 1));
        }
    }
    serde_json::to_vec(&doc).expect("the test feed serialises")
}

/// `<Vst3Global>/<vendor>/<Name>`: the folder a Windows managed install and its Inno record name (FR-001, FR-080).
fn vendor_folder(ctx: &AppContext, name: &str) -> PathBuf {
    root_of(&ctx.roots, RootKind::Vst3Global)
        .join(VENDOR_DIR)
        .join(name)
}

/// `<sandbox>/elsewhere/<Name>`: inside the `fresh_temp_roots()` sandbox (the state dir's parent), outside every
/// root, never a real path (SC-012).
fn outside_folder(ctx: &AppContext, name: &str) -> PathBuf {
    ctx.roots
        .state_dir()
        .parent()
        .unwrap_or_else(|| panic!("the temp state dir has no parent"))
        .join("elsewhere")
        .join(name)
}

/// Plants an elevated row's bundle and pending journal, only inside the context's temp roots.
fn plant_elevated(os: Os, ctx: &AppContext, row: &ElevatedRow) {
    let bundle = format!("{}.vst3", row.name);
    let managed = match os {
        Os::Windows => vendor_folder(ctx, row.name).join(&bundle),
        _ => root_of(&ctx.roots, RootKind::Vst3Global).join(&bundle),
    };
    let class = |v: &str| format!("{v}.1");
    match row.disk {
        ElevatedDisk::Absent => {}
        ElevatedDisk::Managed(v) => write_bundle(&managed, row.name, v, &class(v)),
        ElevatedDisk::ManagedNoModuleinfo => {
            let contents = managed.join("Contents");
            fs::create_dir_all(&contents)
                .unwrap_or_else(|e| panic!("create {}: {e}", contents.display()));
        }
        ElevatedDisk::Elsewhere(v) => write_bundle(
            &root_of(&ctx.roots, RootKind::Vst3User).join(&bundle),
            row.name,
            v,
            &class(v),
        ),
        ElevatedDisk::Outside(v) => write_bundle(
            &outside_folder(ctx, row.name).join(&bundle),
            row.name,
            v,
            &class(v),
        ),
    }
    if row.journal {
        plant_journal(ctx, row.name);
    }
}

/// The `FakeHostFacts` of `os`'s rows: the running OS and each row's record under the key or package id `document`
/// names. Windows: `DisplayVersion` and an `InstallLocation` with Inno's trailing separator; macOS: a receipt.
fn elevated_host(os: Os, ctx: &AppContext, document: &[u8]) -> FakeHostFacts {
    let parsed = feed::parse(document).unwrap_or_else(|e| panic!("the {os:?} feed: {e:?}"));
    let (running, _) = elevated_os(os);
    let mut host = FakeHostFacts {
        os_version: Some(os_version(running)),
        ..FakeHostFacts::default()
    };
    for row in elevated_rows(os) {
        let (version, folder) = match row.record {
            ElevatedRecord::Absent => continue,
            ElevatedRecord::Managed(v) => (v, vendor_folder(ctx, row.name)),
            ElevatedRecord::Outside(v) => (v, outside_folder(ctx, row.name)),
        };
        let install = parsed.plugins[row.name]
            .release
            .as_ref()
            .and_then(|r| r.install.as_ref())
            .unwrap_or_else(|| panic!("{}: no install facts", row.name));
        if os == Os::Windows {
            let key = install
                .windows_uninstall_key
                .as_ref()
                .unwrap_or_else(|| panic!("{}: no uninstall key", row.name));
            host.inno.insert(
                key.as_str().to_owned(),
                RecordRead::Found(InnoValues {
                    display_version: Some(version.to_owned()),
                    install_location: Some(format!("{}{MAIN_SEPARATOR_STR}", root_text(&folder))),
                }),
            );
        } else {
            let id = install
                .macos_pkg_id
                .as_ref()
                .unwrap_or_else(|| panic!("{}: no pkg id", row.name));
            host.pkg.insert(
                id.as_str().to_owned(),
                RecordRead::Found(PkgOutputs {
                    pkg_info: format!("package-id: {}\nversion: {version}\n", id.as_str()),
                    files: format!("{0}.vst3\n{0}.vst3/Contents\n", row.name),
                }),
            );
        }
    }
    host
}

/// SC-013 on one elevated OS: the card actions and `remove_hint` equal the table (a read-only build), and every
/// action a card omits is refused by the command with its code, with no mutating `InstallIo` call, no launch and no
/// asset request.
fn elevated_actions_per_state(os: Os, arch: Arch) {
    let signer = TestSigner::new();
    let feed = elevated_feed(os, &signer);
    let server = FixtureServer::start(vec![(FEED_PATH.to_owned(), ok(feed.clone()))]);
    let (_td, roots) = fresh_temp_roots();
    let rig = InstallRig {
        elevation: FakeElevation::refusing(),
        ..InstallRig::new(context(&roots, server.url(FEED_PATH)))
    };
    let ctx = &rig.ctx;
    let rows = elevated_rows(os);
    for row in &rows {
        plant_elevated(os, ctx, row);
    }
    let host = elevated_host(os, ctx, &feed);

    let io = RecordingInstallIo::forwarding(&FsInstallIo);
    let view = build_with_os(os, arch, ctx, FetchMode::Normal, NOW, &host, &io);
    assert_eq!(
        view.feed.source,
        FeedSource::Fresh,
        "{os:?}: {:?}",
        view.feed.error
    );
    assert_eq!(view.plugins.len(), rows.len(), "{os:?}: {view:?}");
    for row in &rows {
        let card = plugin(&view, row.name);
        assert_eq!(
            (card.availability, card.installed, card.update),
            (row.availability, row.installed, Some(row.update)),
            "{os:?} {}: the planted state: {card:?}",
            row.name
        );
        assert_eq!(card.actions, row.actions, "{os:?} {}: {card:?}", row.name);
        let hint = if row.windows_settings {
            "\"windows_settings\""
        } else {
            "null"
        };
        assert_eq!(
            json(&card.remove_hint),
            hint,
            "{os:?} {}: {card:?}",
            row.name
        );
    }
    assert_eq!(io.mutating_calls(), 0, "{os:?}: {:?}", io.calls());

    for row in &rows {
        let offered: Vec<&str> = row.actions.iter().map(|a| action_text(*a)).collect();
        let omitted: Vec<&str> = ["install", "update", "remove"]
            .into_iter()
            .filter(|a| !offered.contains(a))
            .collect();
        let refused: Vec<&str> = row.refusals.iter().map(|(a, _)| *a).collect();
        assert_eq!(
            refused, omitted,
            "{os:?} {}: refusals cover the omitted actions",
            row.name
        );
        for &(action, code) in row.refusals {
            let io = RecordingInstallIo::fail_closed();
            let env = rig.command_env(os, arch, &io, &FsExtractIo, &NativeDrivers);
            let result = if action == "remove" {
                install::remove_for_command(env, row.name, NOW, &host, &mut |_| {})
            } else {
                install::for_command(env, row.name, LATEST, action, NOW, &host, &mut |_| {})
            };
            match result {
                Ok(summary) => panic!("{os:?} {} {action}: accepted: {summary:?}", row.name),
                Err(e) => assert_eq!(e.code(), code, "{os:?} {} {action}: {e}", row.name),
            }
            assert_eq!(
                io.mutating_calls(),
                0,
                "{os:?} {} {action}: {:?}",
                row.name,
                io.calls()
            );
            assert!(
                rig.elevation.launches().is_empty(),
                "{os:?} {} {action}: launched",
                row.name
            );
        }
    }
    let assets: Vec<String> = server
        .requests()
        .into_iter()
        .filter(|r| r.path != FEED_PATH)
        .map(|r| r.path)
        .collect();
    assert!(assets.is_empty(), "{os:?}: asset requests: {assets:?}");
}

/// The command spelling of a card action (FR-090 step 1).
fn action_text(action: ActionView) -> &'static str {
    match action {
        ActionView::Install => "install",
        ActionView::Update => "update",
        ActionView::Remove => "remove",
    }
}

/// SC-016 (status half; FR-093, FR-094, FR-100, D13; plan PD-15, PD-16): the Linux card actions per state equal the
/// table, and every action a Linux card omits is refused by the command with its code, with no mutating `InstallIo`
/// call and no asset request. Phase 10 SC-013 (FR-080 to FR-083): the same over `(Os::Windows, Arch::X86_64)` and
/// `(Os::Macos, Arch::Aarch64)` with their records, `remove_hint` included, and no launch.
#[test]
fn actions_per_state() {
    let signer = TestSigner::new();
    let server = FixtureServer::start(vec![(FEED_PATH.to_owned(), ok(rows_feed(&signer)))]);
    let (_td, roots) = fresh_temp_roots();
    let rig = InstallRig::new(context(&roots, server.url(FEED_PATH)));
    let ctx = &rig.ctx;
    for row in &STATE_ROWS {
        plant_row(ctx, row);
    }
    let host = FakeHostFacts {
        os_version: Some(os_version(RUNNING_GLIBC)),
        ..FakeHostFacts::default()
    };

    // Linux: the table.
    let view = build_with_os(
        Os::Linux,
        Arch::X86_64,
        ctx,
        FetchMode::Normal,
        NOW,
        &host,
        &FsInstallIo,
    );
    assert_eq!(view.feed.source, FeedSource::Fresh, "{:?}", view.feed.error);
    assert_eq!(view.plugins.len(), STATE_ROWS.len(), "{view:?}");
    for row in &STATE_ROWS {
        let card = plugin(&view, row.name);
        assert_eq!(
            (card.availability, card.installed, card.update),
            (row.availability, row.installed, Some(row.update)),
            "{}: the planted state: {card:?}",
            row.name
        );
        assert_eq!(card.actions, row.actions, "{}: {card:?}", row.name);
        assert_eq!(card.remove_hint, None, "{}: {card:?}", row.name);
    }

    // Linux: every omitted action is refused with its code before any asset request, nothing written (D13).
    for row in &STATE_ROWS {
        let offered: Vec<&str> = row.actions.iter().map(|a| action_text(*a)).collect();
        let omitted: Vec<&str> = ["install", "update", "remove"]
            .into_iter()
            .filter(|a| !offered.contains(a))
            .collect();
        let refused: Vec<&str> = row.refusals.iter().map(|(a, _)| *a).collect();
        assert_eq!(
            refused, omitted,
            "{}: refusals cover the omitted actions",
            row.name
        );
        for &(action, code) in row.refusals {
            let io = RecordingInstallIo::fail_closed();
            let env = rig.command_env(Os::Linux, Arch::X86_64, &io, &FsExtractIo, &NativeDrivers);
            let result = if action == "remove" {
                install::remove_for_command(env, row.name, NOW, &host, &mut |_| {})
            } else {
                install::for_command(env, row.name, LATEST, action, NOW, &host, &mut |_| {})
            };
            match result {
                Ok(summary) => panic!("{} {action}: accepted: {summary:?}", row.name),
                Err(e) => assert_eq!(e.code(), code, "{} {action}: {e}", row.name),
            }
            assert_eq!(
                io.mutating_calls(),
                0,
                "{} {action}: {:?}",
                row.name,
                io.calls()
            );
        }
    }
    let assets: Vec<String> = server
        .requests()
        .into_iter()
        .filter(|r| r.path != FEED_PATH)
        .map(|r| r.path)
        .collect();
    assert!(assets.is_empty(), "asset requests: {assets:?}");

    // Windows and macOS: their own table, each on its own feed and temp roots (Phase 10 SC-013).
    elevated_actions_per_state(Os::Windows, Arch::X86_64);
    elevated_actions_per_state(Os::Macos, Arch::Aarch64);
}

// ---- Phase 12 SC-015, SC-031 (FR-052, FR-054) ---------------------------------------------------------------------

/// Writes `<state>/manifests/<stem>.json` with the given text.
fn plant_manifest(roots: &Roots, stem: &str, text: &str) {
    let dir = roots.state_dir().join(detect::MANIFESTS_DIR);
    fs::create_dir_all(&dir).unwrap_or_else(|e| panic!("create {}: {e}", dir.display()));
    let file = dir.join(format!("{stem}.json"));
    fs::write(&file, text).unwrap_or_else(|e| panic!("write {}: {e}", file.display()));
}

fn head_text(plugin: &str, version: &str) -> String {
    format!("{{\"manifest_version\": 1, \"plugin\": \"{plugin}\", \"version\": \"{version}\"}}")
}

fn orphan(name: &str, version: &str) -> OrphanView {
    OrphanView {
        name: name.to_owned(),
        version: version.to_owned(),
    }
}

fn build_as(os: Os, ctx: &AppContext, now_unix: u64) -> StatusView {
    build_with_os(
        os,
        Arch::X86_64,
        ctx,
        FetchMode::Normal,
        now_unix,
        &FakeHostFacts::default(),
        &FsInstallIo,
    )
}

#[test]
fn orphans_cases() {
    let server = FixtureServer::start(vec![(FEED_PATH.to_owned(), ok(feed_fixture(MIXED)))]);
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots, server.url(FEED_PATH));

    // A served feed first, so the cache holds it; then the feed fails, so every build below runs on the cached copy.
    let first = build_as(Os::Linux, &ctx, NOW);
    assert_eq!(
        first.feed.source,
        FeedSource::Fresh,
        "{:?}",
        first.feed.error
    );
    assert_eq!(first.orphans, Vec::new(), "no manifest yet");
    server.set_route(FEED_PATH, Reply::Close);

    // Stems sort the other way from the names, so the listing is proven sorted by name.
    plant_manifest(&roots, "aaa", &head_text("Zulu", "2.0.0"));
    plant_manifest(&roots, "zzz", &head_text("Mike", "0.9.1"));
    // A feed plugin with an `install` block (`Alpha`) and one without (`Delta`, FR-054) are never orphans.
    plant_manifest(&roots, "Alpha", &head_text("Alpha", "1.0.0"));
    plant_manifest(&roots, "Delta", &head_text("Delta", "1.0.0"));
    // No string `version` → not listed.
    plant_manifest(
        &roots,
        "Number",
        "{\"manifest_version\": 1, \"plugin\": \"Number\", \"version\": 3}",
    );
    plant_manifest(
        &roots,
        "Absent",
        "{\"manifest_version\": 1, \"plugin\": \"Absent\"}",
    );
    // Not manifest version 1, or not JSON → no plugin name, not listed.
    plant_manifest(
        &roots,
        "Two",
        "{\"manifest_version\": 2, \"plugin\": \"Two\", \"version\": \"1.0.0\"}",
    );
    plant_manifest(&roots, "Corrupt", "{ not json");

    let view = build_as(Os::Linux, &ctx, NOW + 1);
    assert_eq!(
        view.feed.source,
        FeedSource::Cached,
        "{:?}",
        view.feed.error
    );
    assert_eq!(view.plugins.len(), 4, "{view:?}");
    assert!(
        view.plugins.iter().any(|p| p.name == "Delta"),
        "Delta is a snapshot key: {view:?}"
    );
    assert_eq!(
        view.orphans,
        vec![orphan("Mike", "0.9.1"), orphan("Zulu", "2.0.0")]
    );

    // The view serialises `orphans` as its last key, each `{ name, version }`.
    let text = json(&view);
    assert!(
        text.ends_with(
            ",\"orphans\":[{\"name\":\"Mike\",\"version\":\"0.9.1\"},{\"name\":\"Zulu\",\"version\":\"2.0.0\"}]}"
        ),
        "{text}"
    );

    // Windows and macOS forced → empty over the same cached feed and manifests.
    for os in [Os::Windows, Os::Macos] {
        let view = build_as(os, &ctx, NOW + 2);
        assert_eq!(view.feed.source, FeedSource::Cached, "{os:?}");
        assert_eq!(view.orphans, Vec::new(), "{os:?}");
    }

    // 257 manifest entries → empty, and the view is still built.
    let present = 8;
    for i in present..=install::MAX_MANIFEST_ENTRIES {
        plant_manifest(&roots, &format!("filler-{i:03}"), "{}");
    }
    let view = build_as(Os::Linux, &ctx, NOW + 3);
    assert_eq!(
        view.feed.source,
        FeedSource::Cached,
        "{:?}",
        view.feed.error
    );
    assert_eq!(view.plugins.len(), 4, "{view:?}");
    assert_eq!(view.orphans, Vec::new());

    // No snapshot (no cache, the feed fails) → empty, even with a manifest on disk.
    let (_td2, roots2) = fresh_temp_roots();
    let ctx2 = context(&roots2, server.url(FEED_PATH));
    plant_manifest(&roots2, "Zulu", &head_text("Zulu", "2.0.0"));
    let view = build_as(Os::Linux, &ctx2, NOW);
    assert_eq!(view.feed.source, FeedSource::None);
    assert!(view.plugins.is_empty());
    assert_eq!(view.orphans, Vec::new());
}
