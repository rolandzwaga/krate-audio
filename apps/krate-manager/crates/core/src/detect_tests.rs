//! detect tests (plan §7.1 rows 002-008, 010, 013-017, 025-027, 029-031). Every tree is built under its own
//! `fresh_temp_roots()` with `Path::join` from the sandbox base; every host fact comes from `FakeHostFacts` or
//! `FakeHostIo`; committed inputs are read in place from `tests/fixtures/detect/` and the feed-contract fixtures.

use super::*;
use crate::feed::{self, Feed, FeedCheck, FeedSnapshot, FetchMode, SnapshotSource};
use crate::fixture_server::{FixtureServer, Reply};
use crate::http::HttpConfig;
use crate::notes;
use crate::platform;
use crate::roots::Roots;
use crate::settings::Settings;
use crate::status::{
    self, AvailabilityView, CopyView, FeedSource, FormatView, InstalledView, PlaceView, PluginView,
    RecordKind, RecordView, UpdateView, VersionSourceView,
};
use crate::test_support::{
    FakeHostFacts, FakeHostIo, fresh_temp_roots, install_capture, kat_signing_key, take_mine,
};
use crate::version::Arch;
use log::Level;
use serde_json::{Value, json};
use std::collections::BTreeMap;
use std::fs;
use std::time::{Duration, SystemTime};

const SEC: Duration = Duration::from_secs(1);
const NOW: u64 = 1_700_000_000;
const ALPHA_KEY: &str = "{0A1F0000-0000-4000-8000-000000000001}_is1";
const ALPHA_PKG: &str = "com.krateaudio.alpha";
const MARKER: &str = "KM-CONTENT-MARKER";
const OSES: [Os; 3] = [Os::Windows, Os::Macos, Os::Linux];
const FEED_PATH: &str = "/versions.json";

// ---- helpers ------------------------------------------------------------------------------------------------------

fn detect_fixtures() -> PathBuf {
    // crates/core -> crates -> krate-manager
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/detect")
}

fn fixture(rel: &str) -> Vec<u8> {
    let path = detect_fixtures().join(rel);
    fs::read(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()))
}

fn fixture_text(rel: &str) -> String {
    String::from_utf8(fixture(rel)).expect("a detect fixture is UTF-8")
}

fn feed_bytes(name: &str) -> Vec<u8> {
    // crates/core -> crates -> krate-manager -> apps -> the repo root
    let path = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../../tools/feed-contract/fixtures/feeds/valid")
        .join(name);
    fs::read(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()))
}

fn feed_fixture(name: &str) -> Feed {
    feed::parse(&feed_bytes(name)).unwrap_or_else(|e| panic!("{name}: {e}"))
}

fn entry(feed: &Feed, name: &str) -> PluginEntry {
    feed.plugins
        .get(name)
        .cloned()
        .unwrap_or_else(|| panic!("the fixture feed has {name}"))
}

fn facts(entry: &PluginEntry) -> InstallFacts {
    entry
        .release
        .as_ref()
        .and_then(|r| r.install.clone())
        .expect("the entry has install facts")
}

fn plain(field: &str, value: &str) -> PlainName {
    PlainName::checked(field, &json!(value)).unwrap_or_else(|| panic!("{field} {value}"))
}

/// The canonical sandbox base of `fresh_temp_roots()` (`state_dir` is `<base>/state`).
fn base_of(roots: &Roots) -> PathBuf {
    roots
        .state_dir()
        .parent()
        .expect("state_dir has a parent")
        .to_path_buf()
}

fn root(roots: &Roots, kind: RootKind) -> PathBuf {
    roots
        .plugin_roots()
        .iter()
        .find(|r| r.kind == kind)
        .map(|r| r.path.clone())
        .unwrap_or_else(|| panic!("no {kind:?} root"))
}

fn context(roots: &Roots) -> AppContext {
    AppContext {
        roots: roots.clone(),
        settings: Settings::default(),
        feed_url: String::new(),
        http: HttpConfig::loopback(SEC, SEC),
        signing_key: kat_signing_key(),
    }
}

fn write(path: &Path, bytes: &[u8]) {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).unwrap_or_else(|e| panic!("mkdir {}: {e}", parent.display()));
    }
    fs::write(path, bytes).unwrap_or_else(|e| panic!("write {}: {e}", path.display()));
}

fn mkdirs(path: &Path) {
    fs::create_dir_all(path).unwrap_or_else(|e| panic!("mkdir {}: {e}", path.display()));
}

fn moduleinfo_path(bundle: &Path) -> PathBuf {
    bundle
        .join("Contents")
        .join("Resources")
        .join("moduleinfo.json")
}

/// A VST3 bundle directory at `path` holding this moduleinfo.
fn bundle(path: &Path, moduleinfo: &[u8]) {
    write(&moduleinfo_path(path), moduleinfo);
}

/// A moduleinfo whose classes carry these `Version` values (`None`: a class without the key) and an optional
/// top-level `Version`; the marker sits in an ignored key.
fn moduleinfo(classes: &[Option<Value>], top: Option<Value>) -> Vec<u8> {
    let classes: Vec<Value> = classes
        .iter()
        .map(|v| match v {
            Some(v) => json!({ "Name": "Alpha", "Version": v }),
            None => json!({ "Name": "Alpha" }),
        })
        .collect();
    let mut doc = json!({ "Name": "Alpha", "KMContent": MARKER, "Classes": classes });
    if let Some(top) = top {
        doc["Version"] = top;
    }
    serde_json::to_vec_pretty(&doc).expect("a Value serialises")
}

/// A release bundle's moduleinfo: top `v`, one class `v.1`.
fn release(v: &str) -> Vec<u8> {
    moduleinfo(&[Some(json!(format!("{v}.1")))], Some(json!(v)))
}

fn known(text: &str, source: VersionSource) -> CopyVersion {
    CopyVersion::Known {
        version: CoreVersion::parse(text).expect("a core version"),
        source,
    }
}

/// The calling thread's captured `krate_manager_core::detect` lines.
fn detect_lines() -> Vec<(Level, String)> {
    take_mine()
        .into_iter()
        .filter(|(target, _, _)| target == LOG_TARGET)
        .map(|(_, level, message)| (level, message))
        .collect()
}

fn warns(lines: &[(Level, String)]) -> Vec<&str> {
    lines
        .iter()
        .filter(|(l, _)| *l == Level::Warn)
        .map(|(_, m)| m.as_str())
        .collect()
}

fn inputs<'a>(
    plugin_roots: &'a [PluginRoot],
    facts: &'a InstallFacts,
    windows_install_location: Option<&'a Path>,
    linux_managed_root: &'a Path,
    nested: &'a [ListedCopy],
) -> CandidateInputs<'a> {
    CandidateInputs {
        plugin_roots,
        facts,
        windows_install_location,
        linux_managed_root,
        nested,
    }
}

fn cand(
    format: CopyFormat,
    place: Place,
    path: PathBuf,
    managed: bool,
    rank: Option<u8>,
) -> Candidate {
    Candidate {
        format,
        place,
        path,
        managed,
        rank,
    }
}

/// The managed VST3 candidate path of Alpha-shaped `facts` for `os` over these roots, with no install location.
fn managed_vst3(os: Os, ctx: &AppContext, facts: &InstallFacts) -> PathBuf {
    let managed_root = ctx.settings.linux_plugin_root_or_default(&ctx.roots);
    candidates(
        os,
        &inputs(ctx.roots.plugin_roots(), facts, None, managed_root, &[]),
    )
    .into_iter()
    .find(|c| c.managed && c.format == CopyFormat::Vst3)
    .map(|c| c.path)
    .expect("a managed VST3 candidate")
}

fn copies(detection: &Detection) -> &[DetectedCopy] {
    match detection {
        Detection::Checked { copies, .. } => copies,
        Detection::NotChecked => panic!("expected a checked detection"),
    }
}

fn managed_copy(detection: &Detection) -> Option<&DetectedCopy> {
    copies(detection)
        .iter()
        .find(|c| c.managed && c.format == CopyFormat::Vst3)
}

fn check_of(feed: Feed) -> FeedCheck {
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

fn arch_for(os: Os) -> Arch {
    match os {
        Os::Macos => Arch::Aarch64,
        Os::Windows | Os::Linux => Arch::X86_64,
    }
}

/// `detect_plugin` for every entry of `feed`, then `status::assemble`; the view of `name`.
fn assembled(
    os: Os,
    feed: &Feed,
    detections: &BTreeMap<String, Detection>,
    name: &str,
) -> PluginView {
    status::assemble(os, arch_for(os), &check_of(feed.clone()), None, detections)
        .plugins
        .into_iter()
        .find(|p| p.name == name)
        .unwrap_or_else(|| panic!("the view has {name}"))
}

fn detect_all(
    os: Os,
    ctx: &AppContext,
    feed: &Feed,
    host: &dyn HostFacts,
    listing: &Listing,
) -> BTreeMap<String, Detection> {
    feed.plugins
        .iter()
        .map(|(name, e)| (name.clone(), detect_plugin(os, ctx, e, host, listing)))
        .collect()
}

fn view_copy(
    place: PlaceView,
    vendor_folder: bool,
    path: &Path,
    managed: bool,
    loads_before_managed: bool,
    version: &str,
) -> CopyView {
    CopyView {
        format: FormatView::Vst3,
        place,
        vendor_folder,
        nested: false,
        path: path.display().to_string(),
        managed,
        loads_before_managed,
        version: Some(version.to_owned()),
        version_source: Some(VersionSourceView::Classes),
        unknown_reason: None,
    }
}

fn notes_of(entry: &PluginEntry) -> Vec<notes::Block> {
    entry
        .release
        .as_ref()
        .and_then(|r| r.notes_markdown.as_deref())
        .map(notes::blocks)
        .unwrap_or_default()
}

fn release_url(entry: &PluginEntry) -> Option<String> {
    entry.release.as_ref().map(|r| r.release_notes_url.clone())
}

fn inno_host(values: InnoValues) -> FakeHostFacts {
    FakeHostFacts {
        inno: BTreeMap::from([(ALPHA_KEY.to_owned(), RecordRead::Found(values))]),
        ..FakeHostFacts::default()
    }
}

// ---- SC-002: the candidate table ------------------------------------------------------------------------------------

#[test]
fn candidate_table() {
    use CopyFormat::{Au, Vst3};
    use RootKind::{AuGlobal, AuUser, Configured, Vst3Global, Vst3User};
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    let full = feed_fixture("full-v2.json");
    let tenth = feed_fixture("tenth-plugin.json");
    let plugins = [
        facts(&entry(&full, "Alpha")),
        facts(&entry(&full, "Bravo")),
        facts(&entry(&tenth, "Delta")),
    ];
    let user = base.join("vst3-user");
    let global = base.join("vst3-global");
    let au_user = base.join("au-user");
    let au_global = base.join("au-global");
    let configured = base.join("configured");
    let globals: Vec<PathBuf> = (1..=4)
        .map(|n| base.join(format!("vst3-global-{n}")))
        .collect();
    let pr = |kind: RootKind, path: &Path| PluginRoot {
        kind,
        path: path.to_path_buf(),
    };
    let windows_roots = vec![pr(Vst3User, &user), pr(Vst3Global, &global)];
    let macos_roots = vec![
        pr(Vst3User, &user),
        pr(Vst3Global, &global),
        pr(AuUser, &au_user),
        pr(AuGlobal, &au_global),
    ];
    let mut linux_roots = vec![pr(Vst3User, &user)];
    linux_roots.extend(globals.iter().map(|g| pr(Vst3Global, g)));
    let mut linux_configured = vec![pr(Configured, &configured)];
    linux_configured.extend(linux_roots.iter().cloned());

    for f in &plugins {
        let b = f.vst3_bundle.as_str();
        let stem = b.strip_suffix(".vst3").expect("a .vst3 bundle");
        let vendor = |r: &Path| r.join(VENDOR_DIR).join(stem).join(b);

        // Windows, no install location: the default vendor candidate of the first Vst3Global root is managed.
        let fixed = vec![
            cand(Vst3, Place::Root(Vst3User), user.join(b), false, Some(0)),
            cand(
                Vst3,
                Place::WindowsVendor(Vst3User),
                vendor(&user),
                false,
                Some(0),
            ),
            cand(
                Vst3,
                Place::Root(Vst3Global),
                global.join(b),
                false,
                Some(1),
            ),
            cand(
                Vst3,
                Place::WindowsVendor(Vst3Global),
                vendor(&global),
                true,
                Some(1),
            ),
        ];
        let win = |loc: Option<&Path>, nested: &[ListedCopy]| {
            candidates(Os::Windows, &inputs(&windows_roots, f, loc, &user, nested))
        };
        assert_eq!(win(None, &[]), fixed, "{b}: windows, no location");

        // (a) the default vendor folder in other letter case: no extra candidate, the default vendor one managed.
        let loc_a = global
            .join(VENDOR_DIR.to_ascii_lowercase())
            .join(stem.to_ascii_uppercase());
        assert_eq!(win(Some(&loc_a), &[]), fixed, "{b}: windows (a)");

        // (b) and (c): an extra InstallerLocation candidate, managed, rank None; the vendor candidate not managed.
        for loc in [
            global.join(VENDOR_DIR).join("Custom"),
            base.join("elsewhere"),
        ] {
            let mut want = fixed.clone();
            want[3].managed = false;
            want.push(cand(
                Vst3,
                Place::InstallerLocation,
                loc.join(b),
                true,
                None,
            ));
            assert_eq!(win(Some(&loc), &[]), want, "{b}: windows {}", loc.display());
        }

        // Nested copies (PD-1): appended after the fixed ones with their root's rank; a copy `same_path` as a fixed
        // candidate (any letter case on the Windows table) or as an earlier nested copy is dropped.
        let old = user.join("Old").join(b);
        let nested = [
            ListedCopy {
                root: Vst3User,
                root_index: 0,
                path: old.clone(),
            },
            ListedCopy {
                root: Vst3Global,
                root_index: 1,
                path: global
                    .join(VENDOR_DIR.to_ascii_lowercase())
                    .join(stem)
                    .join(b.to_ascii_lowercase()),
            },
            ListedCopy {
                root: Vst3User,
                root_index: 0,
                path: old.clone(),
            },
            ListedCopy {
                root: Vst3Global,
                root_index: 1,
                path: global.join("a").join(b),
            },
        ];
        let mut want = fixed.clone();
        want.push(cand(
            Vst3,
            Place::Nested(Vst3User),
            old.clone(),
            false,
            Some(0),
        ));
        want.push(cand(
            Vst3,
            Place::Nested(Vst3Global),
            global.join("a").join(b),
            false,
            Some(1),
        ));
        assert_eq!(win(None, &nested), want, "{b}: windows nested");

        // macOS: VST3 user / global, then AU user / global when the entry has an AU component.
        let mut want = vec![
            cand(Vst3, Place::Root(Vst3User), user.join(b), false, Some(0)),
            cand(Vst3, Place::Root(Vst3Global), global.join(b), true, Some(1)),
        ];
        if let Some(c) = &f.au_component {
            want.push(cand(
                Au,
                Place::Root(AuUser),
                au_user.join(c.as_str()),
                false,
                None,
            ));
            want.push(cand(
                Au,
                Place::Root(AuGlobal),
                au_global.join(c.as_str()),
                true,
                None,
            ));
        }
        let mac = candidates(Os::Macos, &inputs(&macos_roots, f, None, &user, &[]));
        assert_eq!(mac, want, "{b}: macos");
        if f.au_component.is_none() {
            assert!(mac.iter().all(|c| c.format == Vst3), "{b}: no AU candidate");
        }

        // Linux with a configured root: it is managed (rank None); the n-th Vst3Global root has rank n.
        let mut want = vec![cand(
            Vst3,
            Place::Root(Configured),
            configured.join(b),
            true,
            None,
        )];
        want.push(cand(
            Vst3,
            Place::Root(Vst3User),
            user.join(b),
            false,
            Some(0),
        ));
        for (n, g) in (1u8..).zip(&globals) {
            want.push(cand(
                Vst3,
                Place::Root(Vst3Global),
                g.join(b),
                false,
                Some(n),
            ));
        }
        let lin = candidates(
            Os::Linux,
            &inputs(&linux_configured, f, None, &configured, &[]),
        );
        assert_eq!(lin, want, "{b}: linux configured");

        // Linux without one: the Vst3User root is managed.
        let mut want = vec![cand(
            Vst3,
            Place::Root(Vst3User),
            user.join(b),
            true,
            Some(0),
        )];
        for (n, g) in (1u8..).zip(&globals) {
            want.push(cand(
                Vst3,
                Place::Root(Vst3Global),
                g.join(b),
                false,
                Some(n),
            ));
        }
        assert_eq!(
            candidates(Os::Linux, &inputs(&linux_roots, f, None, &user, &[])),
            want,
            "{b}: linux"
        );

        // Linux nested (PD-2, PD-3): under the third Vst3Global root (index 3) → rank 3; an exact duplicate of a
        // fixed candidate is dropped; another letter case is a different path there.
        let third = globals[2].join("x").join(b);
        let other_case = globals[0].join(b.to_ascii_lowercase());
        let nested = [
            ListedCopy {
                root: Vst3Global,
                root_index: 3,
                path: third.clone(),
            },
            ListedCopy {
                root: Vst3User,
                root_index: 0,
                path: user.join(b),
            },
            ListedCopy {
                root: Vst3Global,
                root_index: 1,
                path: other_case.clone(),
            },
        ];
        let mut with_nested = want.clone();
        with_nested.push(cand(Vst3, Place::Nested(Vst3Global), third, false, Some(3)));
        with_nested.push(cand(
            Vst3,
            Place::Nested(Vst3Global),
            other_case,
            false,
            Some(1),
        ));
        assert_eq!(
            candidates(Os::Linux, &inputs(&linux_roots, f, None, &user, &nested)),
            with_nested,
            "{b}: linux nested"
        );
    }

    // `same_path`: component-wise, ASCII case-insensitive, equal component counts.
    let a = base.join("Vst3").join("Alpha.vst3");
    assert!(same_path(&a, &base.join("VST3").join("alpha.VST3")));
    assert!(same_path(&a, &a));
    assert!(!same_path(&a, &base.join("Vst3")));
    assert!(!same_path(
        &a,
        &base.join("Vst3").join("Alpha.vst3").join("x")
    ));
    assert!(!same_path(&a, &base.join("Vst3").join("Bravo.vst3")));
}

// ---- SC-003: the native layout --------------------------------------------------------------------------------------

#[cfg(target_os = "windows")]
#[test]
fn native_layout_windows() {
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots);
    let alpha = facts(&entry(&feed_fixture("full-v2.json"), "Alpha"));
    let managed: Vec<PathBuf> = candidates(
        NativePlatform.os(),
        &inputs(roots.plugin_roots(), &alpha, None, roots.vst3_user(), &[]),
    )
    .into_iter()
    .filter(|c| c.managed)
    .map(|c| c.path)
    .collect();
    let global = root(&roots, RootKind::Vst3Global);
    assert_eq!(
        managed,
        [global.join(VENDOR_DIR).join("Alpha").join("Alpha.vst3")]
    );
    assert_eq!(managed_vst3(Os::Windows, &ctx, &alpha), managed[0]);
}

#[cfg(target_os = "macos")]
#[test]
fn native_layout_macos() {
    let (_td, roots) = fresh_temp_roots();
    let alpha = facts(&entry(&feed_fixture("full-v2.json"), "Alpha"));
    let managed: Vec<PathBuf> = candidates(
        NativePlatform.os(),
        &inputs(roots.plugin_roots(), &alpha, None, roots.vst3_user(), &[]),
    )
    .into_iter()
    .filter(|c| c.managed)
    .map(|c| c.path)
    .collect();
    assert_eq!(
        managed,
        [
            root(&roots, RootKind::Vst3Global).join("Alpha.vst3"),
            root(&roots, RootKind::AuGlobal).join("Alpha.component"),
        ]
    );
}

#[cfg(target_os = "linux")]
#[test]
fn native_layout_linux() {
    use crate::context::{AppState, reassemble_with_os};
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    let alpha = facts(&entry(&feed_fixture("full-v2.json"), "Alpha"));
    let ctx = context(&roots);
    let managed_of = |ctx: &AppContext| -> Vec<PathBuf> {
        candidates(
            NativePlatform.os(),
            &inputs(
                ctx.roots.plugin_roots(),
                &alpha,
                None,
                ctx.settings.linux_plugin_root_or_default(&ctx.roots),
                &[],
            ),
        )
        .into_iter()
        .filter(|c| c.managed)
        .map(|c| c.path)
        .collect()
    };
    assert_eq!(
        managed_of(&ctx),
        [root(&roots, RootKind::Vst3User).join("Alpha.vst3")]
    );

    let custom = base.join("custom");
    let prev = AppState {
        context: Ok(ctx),
        settings_error: None,
        lock: None,
    };
    let next = reassemble_with_os(
        Os::Linux,
        &prev,
        Settings {
            linux_plugin_root: Some(custom.clone()),
            ..Settings::default()
        },
    );
    let configured = next
        .context
        .as_ref()
        .expect("valid settings keep a context");
    assert_eq!(
        configured.roots.plugin_roots()[0].kind,
        RootKind::Configured
    );
    assert_eq!(managed_of(configured), [custom.join("Alpha.vst3")]);
}

// ---- SC-004: the moduleinfo grammar ---------------------------------------------------------------------------------

#[test]
fn trailing_comma_cases() {
    let parses = |s: &str| serde_json::from_str::<Value>(&strip_trailing_commas(s)).is_ok();
    assert_eq!(strip_trailing_commas(r#"{"a":[1,2,],}"#), r#"{"a":[1,2]}"#);
    assert!(parses(r#"{"a":[1,2,],}"#));
    assert_eq!(
        strip_trailing_commas("{\"a\": [1 ,\n ] ,\r\n\t}"),
        "{\"a\": [1 \n ] \r\n\t}"
    );
    // A `,]` or `,}` inside a string is unchanged, and nothing is copied.
    let inside = r#"{"a":",]","b":"x, }"}"#;
    assert!(matches!(strip_trailing_commas(inside), Cow::Borrowed(t) if t == inside));
    // Escapes are honoured: `\"` does not end the string, `\\` does not escape the closing quote.
    assert_eq!(
        strip_trailing_commas(r#"{"a":"q\",]",}"#),
        r#"{"a":"q\",]"}"#
    );
    assert_eq!(strip_trailing_commas(r#"{"a":"\\",}"#), r#"{"a":"\\"}"#);
    // A comma right after `[`, `{` or `,` is kept (SI-16), so these stay invalid.
    for kept in ["[,]", "{,}", "[1,,]", "{\"a\":1,,}"] {
        assert_eq!(strip_trailing_commas(kept), kept);
        assert!(!parses(kept), "{kept}");
    }

    let path = Path::new("moduleinfo.json");
    for corrupt in [
        "[,]",
        "{,}",
        "[1,,]",
        r#"{"Classes":[,]}"#,
        r#"{"Classes":{,}}"#,
        r#"{"Classes":[1,,]}"#,
        "{\"Version\": \"1.0.0\" // a comment\n}",
        "{'Version': '1.0.0'}",
        "{Version: \"1.0.0\"}",
    ] {
        assert_eq!(
            moduleinfo_version(corrupt, path),
            CopyVersion::Unknown(UnknownReason::Corrupt),
            "{corrupt}"
        );
    }
    // The committed SDK-shaped fixture (top 1.0.0, classes 1.0.0.1, the writer's trailing commas) parses.
    let sdk = fixture_text("moduleinfo/sdk-shape.json");
    assert!(
        sdk.contains("],\n}"),
        "the fixture keeps the SDK writer's trailing commas"
    );
    assert_eq!(
        moduleinfo_version(&sdk, path),
        known("1.0.0", VersionSource::Classes)
    );
}

// ---- SC-005: the installed-state table ------------------------------------------------------------------------------

enum Input {
    Fixture(&'static str),
    Bytes(Vec<u8>),
    NoModuleinfo,
    SingleFile,
    DirModuleinfo,
}

struct StateRow {
    label: &'static str,
    input: Input,
    want: CopyVersion,
    /// The one expected `warn!`: its code and texts it names.
    warn: Option<(&'static str, Vec<&'static str>)>,
    /// The one expected `debug!` `E_DETECT_TOP_LEVEL`: the texts it names.
    debug: Option<Vec<&'static str>>,
}

#[test]
fn installed_state_table() {
    use UnknownReason as R;
    let unknown = CopyVersion::Unknown;
    let classes = VersionSource::Classes;
    let mut too_large = release("1.2.0");
    too_large.resize(
        usize::try_from(MAX_MODULEINFO_BYTES + 1).expect("fits"),
        b' ',
    );
    let mut bom = vec![0xEF, 0xBB, 0xBF];
    bom.extend(fixture("moduleinfo/current.json"));
    let rows = vec![
        StateRow {
            label: "current",
            input: Input::Fixture("moduleinfo/current.json"),
            want: known("1.2.0", classes),
            warn: None,
            debug: None,
        },
        StateRow {
            label: "legacy 0.8.0 top",
            input: Input::Fixture("moduleinfo/legacy-080.json"),
            want: known("1.0.4", classes),
            warn: None,
            debug: Some(vec!["0.8.0", "1.0.4"]),
        },
        StateRow {
            label: "top 1.0.3, class 1.0.4.0",
            input: Input::Fixture("moduleinfo/top-103-class-104.json"),
            want: known("1.0.4", classes),
            warn: Some((E_DETECT_TOP_LEVEL, vec!["1.0.3", "1.0.4"])),
            debug: None,
        },
        StateRow {
            label: "a real 0.8.0 release",
            input: Input::Fixture("moduleinfo/real-080.json"),
            want: known("0.8.0", classes),
            warn: None,
            debug: None,
        },
        StateRow {
            label: "classes disagree",
            input: Input::Fixture("moduleinfo/classes-disagree.json"),
            want: unknown(R::ClassesDisagree),
            warn: Some((E_DETECT_CLASSES, vec!["1.0.4.0", "1.0.5.0"])),
            debug: None,
        },
        StateRow {
            label: "class 1.0.4.x",
            input: Input::Fixture("moduleinfo/class-x.json"),
            want: unknown(R::ClassesDisagree),
            warn: Some((E_DETECT_CLASSES, vec!["1.0.4.x"])),
            debug: None,
        },
        StateRow {
            label: "no class version, top 1.2.3",
            input: Input::Fixture("moduleinfo/no-class-top-123.json"),
            want: known("1.2.3", VersionSource::TopLevel),
            warn: None,
            debug: None,
        },
        StateRow {
            label: "no version at all",
            input: Input::Fixture("moduleinfo/no-version.json"),
            want: unknown(R::NoVersion),
            warn: None,
            debug: None,
        },
        StateRow {
            label: "truncated",
            input: Input::Fixture("moduleinfo/truncated.json"),
            want: unknown(R::Corrupt),
            warn: Some((E_DETECT_MODULEINFO, vec!["parse error"])),
            debug: None,
        },
        StateRow {
            label: "1 MiB + 1 byte",
            input: Input::Bytes(too_large),
            want: unknown(R::TooLarge),
            warn: Some((E_DETECT_MODULEINFO, vec!["too large"])),
            debug: None,
        },
        StateRow {
            label: "no moduleinfo",
            input: Input::NoModuleinfo,
            want: unknown(R::NoModuleInfo),
            warn: None,
            debug: None,
        },
        StateRow {
            label: "single file",
            input: Input::SingleFile,
            want: unknown(R::SingleFile),
            warn: None,
            debug: None,
        },
        StateRow {
            label: "UTF-8 BOM",
            input: Input::Bytes(bom),
            want: known("1.2.0", classes),
            warn: None,
            debug: None,
        },
        StateRow {
            label: "not UTF-8",
            input: Input::Bytes(vec![0xFF, 0xFE, b'{', b'}']),
            want: unknown(R::Corrupt),
            warn: Some((E_DETECT_MODULEINFO, vec!["not UTF-8"])),
            debug: None,
        },
        StateRow {
            label: "root not an object",
            input: Input::Bytes(serde_json::to_vec(&json!([MARKER])).expect("serialises")),
            want: unknown(R::Corrupt),
            warn: Some((E_DETECT_MODULEINFO, vec!["root not an object"])),
            debug: None,
        },
        StateRow {
            label: "class Version 1",
            input: Input::Bytes(moduleinfo(&[Some(json!(1))], None)),
            want: unknown(R::ClassesDisagree),
            warn: Some((E_DETECT_CLASSES, vec!["<non-string>"])),
            debug: None,
        },
        StateRow {
            label: "no class version, top Version 1",
            input: Input::Bytes(moduleinfo(&[None], Some(json!(1)))),
            want: unknown(R::NoVersion),
            warn: None,
            debug: None,
        },
        StateRow {
            label: "classes 1.2.0.1, no top",
            input: Input::Bytes(moduleinfo(&[Some(json!("1.2.0.1"))], None)),
            want: known("1.2.0", classes),
            warn: None,
            debug: None,
        },
        StateRow {
            label: "classes 1.2.0.1, top Version 1",
            input: Input::Bytes(moduleinfo(&[Some(json!("1.2.0.1"))], Some(json!(1)))),
            want: known("1.2.0", classes),
            warn: Some((E_DETECT_TOP_LEVEL, vec!["1.2.0"])),
            debug: None,
        },
        StateRow {
            label: "moduleinfo.json is a directory",
            input: Input::DirModuleinfo,
            want: unknown(R::Unreadable),
            warn: Some((E_DETECT_MODULEINFO, vec!["unreadable"])),
            debug: None,
        },
    ];
    install_capture();
    let alpha = entry(&feed_fixture("full-v2.json"), "Alpha");
    let alpha_facts = facts(&alpha);
    let os = NativePlatform.os();
    for row in rows {
        let (_td, roots) = fresh_temp_roots();
        let ctx = context(&roots);
        let managed = managed_vst3(os, &ctx, &alpha_facts);
        match &row.input {
            Input::Fixture(rel) => bundle(&managed, &fixture(rel)),
            Input::Bytes(bytes) => bundle(&managed, bytes),
            Input::NoModuleinfo => mkdirs(&managed.join("Contents")),
            Input::SingleFile => write(&managed, b"a single-file VST3 binary"),
            Input::DirModuleinfo => mkdirs(&moduleinfo_path(&managed)),
        }
        let _ = take_mine();
        let detection = detect_plugin(
            os,
            &ctx,
            &alpha,
            &FakeHostFacts::default(),
            &Listing::default(),
        );
        let lines = detect_lines();
        let copy =
            managed_copy(&detection).unwrap_or_else(|| panic!("{}: a managed copy", row.label));
        assert_eq!(copy.version, row.want, "{}", row.label);
        assert_eq!(copy.path, managed, "{}", row.label);

        let warned = warns(&lines);
        match &row.warn {
            None => assert!(warned.is_empty(), "{}: {warned:?}", row.label),
            Some((code, texts)) => {
                assert_eq!(warned.len(), 1, "{}: {warned:?}", row.label);
                assert!(warned[0].starts_with(code), "{}: {}", row.label, warned[0]);
                for t in texts {
                    assert!(
                        warned[0].contains(t),
                        "{}: {} lacks {t}",
                        row.label,
                        warned[0]
                    );
                }
            }
        }
        let debugs: Vec<&str> = lines
            .iter()
            .filter(|(l, m)| *l == Level::Debug && m.starts_with(E_DETECT_TOP_LEVEL))
            .map(|(_, m)| m.as_str())
            .collect();
        match &row.debug {
            None => assert!(debugs.is_empty(), "{}: {debugs:?}", row.label),
            Some(texts) => {
                assert_eq!(debugs.len(), 1, "{}: {debugs:?}", row.label);
                for t in texts {
                    assert!(
                        debugs[0].contains(t),
                        "{}: {} lacks {t}",
                        row.label,
                        debugs[0]
                    );
                }
            }
        }
        assert!(
            lines.iter().all(|(_, m)| !m.contains(MARKER)),
            "{}: a log line carries file content: {lines:?}",
            row.label
        );
    }
}

// ---- SC-006: duplicates and shadowing -------------------------------------------------------------------------------

#[test]
fn duplicates_in_two_roots() {
    use PlaceView::{Vst3Global, Vst3User};
    let feed = feed_fixture("full-v2.json");
    let alpha = entry(&feed, "Alpha");
    let bravo = entry(&feed, "Bravo");
    for os in OSES {
        let (_td, roots) = fresh_temp_roots();
        let ctx = context(&roots);
        let user = root(&roots, RootKind::Vst3User);
        let global = root(&roots, RootKind::Vst3Global);
        let (alpha_managed, alpha_other, bravo_copy) = match os {
            Os::Windows => (
                global.join(VENDOR_DIR).join("Alpha").join("Alpha.vst3"),
                user.join("Alpha.vst3"),
                user.join("Bravo.vst3"),
            ),
            Os::Macos => (
                global.join("Alpha.vst3"),
                user.join("Alpha.vst3"),
                user.join("Bravo.vst3"),
            ),
            Os::Linux => (
                user.join("Alpha.vst3"),
                global.join("Alpha.vst3"),
                global.join("Bravo.vst3"),
            ),
        };
        bundle(&alpha_managed, &release("1.2.0"));
        bundle(&alpha_other, &release("1.1.0"));
        bundle(&bravo_copy, &release("2.0.0"));
        let listing = Listing::scan(
            roots.plugin_roots(),
            &["Alpha.vst3", "Bravo.vst3", "Charlie.vst3"],
            ListingLimits::PRODUCTION,
        );
        let detections = detect_all(os, &ctx, &feed, &FakeHostFacts::default(), &listing);

        // The other copy ranks before the managed location on Windows and macOS (user 0 < global 1), after it on
        // Linux, where the managed root is vst3-user (global 1 > user 0).
        let shadows = os != Os::Linux;
        let (managed_place, other_place) = if shadows {
            (Vst3Global, Vst3User)
        } else {
            (Vst3User, Vst3Global)
        };
        let managed_view = view_copy(
            managed_place,
            os == Os::Windows,
            &alpha_managed,
            true,
            false,
            "1.2.0",
        );
        let other_view = view_copy(other_place, false, &alpha_other, false, shadows, "1.1.0");
        let alpha_copies = if shadows {
            vec![other_view, managed_view]
        } else {
            vec![managed_view, other_view]
        };
        let want_alpha = PluginView {
            name: "Alpha".to_owned(),
            latest: "1.2.0".to_owned(),
            availability: AvailabilityView::Available,
            min_os: None,
            installed: InstalledView::Version,
            installed_version: Some("1.2.0".to_owned()),
            update: Some(UpdateView::UpToDate),
            copies: alpha_copies,
            record: None,
            record_error: None,
            record_without_files: false,
            record_version_differs: false,
            release_page_url: release_url(&alpha),
            notes: notes_of(&alpha),
            actions: Vec::new(),
        };
        let want_bravo = PluginView {
            name: "Bravo".to_owned(),
            latest: "2.0.1".to_owned(),
            availability: if os == Os::Macos {
                AvailabilityView::NotForThisMachine
            } else {
                AvailabilityView::Available
            },
            min_os: None,
            installed: InstalledView::Elsewhere,
            installed_version: Some("2.0.0".to_owned()),
            update: Some(UpdateView::NotInstalled),
            copies: vec![view_copy(
                other_place,
                false,
                &bravo_copy,
                false,
                shadows,
                "2.0.0",
            )],
            record: None,
            record_error: None,
            record_without_files: false,
            record_version_differs: false,
            release_page_url: release_url(&bravo),
            notes: notes_of(&bravo),
            actions: Vec::new(),
        };
        assert_eq!(
            assembled(os, &feed, &detections, "Alpha"),
            want_alpha,
            "{os:?}"
        );
        assert_eq!(
            assembled(os, &feed, &detections, "Bravo"),
            want_bravo,
            "{os:?}"
        );
    }
}

// ---- SC-007: installer records, pure --------------------------------------------------------------------------------

#[test]
fn record_parsers() {
    install_capture();
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    let ctx = context(&roots);
    let alpha = entry(&feed_fixture("full-v2.json"), "Alpha");
    let custom = base.join(VENDOR_DIR).join("Custom");
    let custom_text = custom.to_str().expect("UTF-8").to_owned();
    let v120 = CoreVersion::parse("1.2.0");
    let inno = |d: Option<&str>, l: Option<&str>| {
        inno_record(&InnoValues {
            display_version: d.map(str::to_owned),
            install_location: l.map(str::to_owned),
        })
    };
    let rec = |t: Option<&str>, l: Option<PathBuf>| InstallerRecord::Inno {
        version_text: t.map(str::to_owned),
        version: t.and_then(CoreVersion::parse),
        install_location: l,
    };

    // Both values, one, none.
    assert_eq!(
        inno(Some("1.2.0"), Some(&custom_text)),
        rec(Some("1.2.0"), Some(custom.clone()))
    );
    assert_eq!(inno(Some("1.2.0"), None), rec(Some("1.2.0"), None));
    assert_eq!(
        inno(None, Some(&custom_text)),
        rec(None, Some(custom.clone()))
    );
    assert_eq!(inno(None, None), rec(None, None));
    assert_eq!(inno(Some("1.2"), None), rec(Some("1.2"), None));
    // Trailing separators are trimmed.
    for trailing in [
        format!("{custom_text}\\"),
        format!("{custom_text}/"),
        format!("{custom_text}\\/"),
    ] {
        assert_eq!(
            usable_install_location(&trailing),
            Some(custom.clone()),
            "{trailing}"
        );
    }

    // Refused locations: relative, `..`, NUL, over-long. Each through `detect_plugin` writes one
    // `E_RECORD_LOCATION` naming the plugin; the NUL and the over-long value are not logged.
    let relative = Path::new("relative").join("dir");
    let relative_text = relative.to_str().expect("UTF-8").to_owned();
    // Written as text: `PathBuf::push` folds `..` on a verbatim (`\\?\`) base, as the canonical Windows temp base is.
    let sep = std::path::MAIN_SEPARATOR;
    let dotdot_text = format!("{}{sep}..{sep}b", base.join("a").display());
    let nul = format!("{custom_text}\0x");
    let long = format!("{custom_text}{}", "a".repeat(32_767));
    for (bad, logged) in [
        (&relative_text, true),
        (&dotdot_text, true),
        (&nul, false),
        (&long, false),
    ] {
        assert_eq!(usable_install_location(bad), None, "{bad:?}");
        assert_eq!(inno(Some("1.2.0"), Some(bad)), rec(Some("1.2.0"), None));
        let host = inno_host(InnoValues {
            display_version: Some("1.2.0".to_owned()),
            install_location: Some(bad.clone()),
        });
        let _ = take_mine();
        let detection = detect_plugin(Os::Windows, &ctx, &alpha, &host, &Listing::default());
        let lines = detect_lines();
        let warned = warns(&lines);
        assert_eq!(warned.len(), 1, "{bad:?}: {warned:?}");
        assert!(
            warned[0].starts_with("E_RECORD_LOCATION Alpha"),
            "{}",
            warned[0]
        );
        if logged {
            assert!(warned[0].contains(bad.as_str()), "{}", warned[0]);
        } else {
            assert!(!warned[0].contains(&custom_text), "{}", warned[0]);
            assert!(!warned[0].contains('\0'), "{}", warned[0]);
        }
        match &detection {
            Detection::Checked {
                record,
                record_error,
                ..
            } => {
                assert_eq!(record.as_ref(), Some(&rec(Some("1.2.0"), None)));
                assert_eq!(*record_error, None);
            }
            Detection::NotChecked => panic!("Alpha has install facts"),
        }
    }

    // pkgutil --pkg-info: the committed text, another package-id, a missing version, extra keys ignored.
    let alpha_pkg = plain("macos_pkg_id", ALPHA_PKG);
    let info = fixture_text("pkgutil/alpha-pkg-info.txt");
    assert!(info.contains(MARKER));
    assert_eq!(
        parse_pkg_info(&info, &alpha_pkg),
        Ok(PkgInfo {
            version_text: "1.2.0".to_owned(),
            location: Some("/".to_owned()),
        })
    );
    assert_eq!(
        parse_pkg_info(&info, &plain("macos_pkg_id", "com.krateaudio.bravo")),
        Err(E_RECORD_PARSE)
    );
    let no_version: String = info
        .lines()
        .filter(|l| !l.starts_with("version: "))
        .map(|l| format!("{l}\n"))
        .collect();
    assert_eq!(parse_pkg_info(&no_version, &alpha_pkg), Err(E_RECORD_PARSE));
    assert_eq!(
        parse_pkg_info(
            &format!("version: 1.2.0\nother: x\npackage-id: {ALPHA_PKG}\n"),
            &alpha_pkg
        ),
        Ok(PkgInfo {
            version_text: "1.2.0".to_owned(),
            location: None,
        })
    );
    let files = fixture_text("pkgutil/alpha-files.txt");
    assert_eq!(count_pkg_files(&files), 3);
    assert_eq!(count_pkg_files(""), 0);
    assert_eq!(count_pkg_files("a\r\n\r\nb\n"), 2);
    assert_eq!(
        pkg_record(
            &PkgOutputs {
                pkg_info: info.clone(),
                files: files.clone(),
            },
            &alpha_pkg
        ),
        Ok(InstallerRecord::Pkg {
            version_text: "1.2.0".to_owned(),
            version: v120,
            location: Some("/".to_owned()),
            files: 3,
        })
    );

    // The Linux manifest at <state>/manifests/Alpha.json.
    let state = roots.state_dir();
    let manifest = state.join(MANIFESTS_DIR).join("Alpha.json");
    assert_eq!(read_manifest(state, "Alpha", "Alpha"), RecordRead::Absent);
    for (rel, want) in [
        (
            "manifest/alpha-v1.json",
            RecordRead::Found(InstallerRecord::Manifest {
                version_text: "1.2.0".to_owned(),
                version: v120,
            }),
        ),
        (
            "manifest/alpha-v2.json",
            RecordRead::Failed(E_RECORD_FUTURE),
        ),
        (
            "manifest/other-plugin.json",
            RecordRead::Failed(E_RECORD_MISMATCH),
        ),
        (
            "manifest/wrong-types.json",
            RecordRead::Failed(E_RECORD_PARSE),
        ),
    ] {
        write(&manifest, &fixture(rel));
        assert_eq!(read_manifest(state, "Alpha", "Alpha"), want, "{rel}");
    }
    let mut big = fixture("manifest/alpha-v1.json");
    big.resize(usize::try_from(MAX_MANIFEST_BYTES + 1).expect("fits"), b' ');
    write(&manifest, &big);
    assert_eq!(
        read_manifest(state, "Alpha", "Alpha"),
        RecordRead::Failed(E_RECORD_READ),
        "4 MiB + 1 byte"
    );
    fs::remove_file(&manifest).expect("remove the manifest");
    mkdirs(&manifest);
    assert_eq!(
        read_manifest(state, "Alpha", "Alpha"),
        RecordRead::Failed(E_RECORD_READ),
        "a directory named Alpha.json"
    );
    let lines = detect_lines();
    assert!(lines.iter().all(|(_, m)| !m.contains(MARKER)), "{lines:?}");
}

// ---- SC-008: records through the fake host --------------------------------------------------------------------------

#[test]
fn records_through_fake_host() {
    install_capture();
    let full = feed_fixture("full-v2.json");
    let alpha = entry(&full, "Alpha");
    let alpha_facts = facts(&alpha);
    let tenth = feed_fixture("tenth-plugin.json");
    let delta = entry(&tenth, "Delta");

    // Delta has no record key: no record lookup on any OS.
    for os in OSES {
        let (_td, roots) = fresh_temp_roots();
        let host = FakeHostFacts::default();
        let d = detect_plugin(os, &context(&roots), &delta, &host, &Listing::default());
        assert!(
            matches!(
                d,
                Detection::Checked {
                    record: None,
                    record_error: None,
                    ..
                }
            ),
            "{os:?}"
        );
        assert!(
            host.calls.borrow().is_empty(),
            "{os:?}: {:?}",
            host.calls.borrow()
        );
    }

    // A record 1.0.3 per OS: without the managed copy, then against files 1.0.4.
    for os in OSES {
        for with_files in [false, true] {
            let (_td, roots) = fresh_temp_roots();
            let ctx = context(&roots);
            let host = match os {
                Os::Windows => inno_host(InnoValues {
                    display_version: Some("1.0.3".to_owned()),
                    install_location: None,
                }),
                Os::Macos => FakeHostFacts {
                    pkg: BTreeMap::from([(
                        ALPHA_PKG.to_owned(),
                        RecordRead::Found(PkgOutputs {
                            pkg_info: format!("package-id: {ALPHA_PKG}\nversion: 1.0.3\n"),
                            files: String::new(),
                        }),
                    )]),
                    ..FakeHostFacts::default()
                },
                Os::Linux => {
                    write(
                        &roots.state_dir().join(MANIFESTS_DIR).join("Alpha.json"),
                        br#"{"manifest_version": 1, "plugin": "Alpha", "version": "1.0.3"}"#,
                    );
                    FakeHostFacts::default()
                }
            };
            if with_files {
                bundle(
                    &managed_vst3(os, &ctx, &alpha_facts),
                    &moduleinfo(&[Some(json!("1.0.4.0"))], None),
                );
            }
            let detections = BTreeMap::from([(
                "Alpha".to_owned(),
                detect_plugin(os, &ctx, &alpha, &host, &Listing::default()),
            )]);
            let expected_calls: Vec<String> = match os {
                Os::Windows => vec![format!("inno_values {ALPHA_KEY}")],
                Os::Macos => vec![format!("pkg_outputs {ALPHA_PKG}")],
                Os::Linux => vec![],
            };
            assert_eq!(*host.calls.borrow(), expected_calls, "{os:?}");
            let view = assembled(os, &full, &detections, "Alpha");
            let (kind, files) = match os {
                Os::Windows => (RecordKind::Inno, None),
                Os::Macos => (RecordKind::Pkg, Some(0)),
                Os::Linux => (RecordKind::Manifest, None),
            };
            assert_eq!(
                view.record,
                Some(RecordView {
                    kind,
                    version: Some("1.0.3".to_owned()),
                    files,
                }),
                "{os:?}"
            );
            assert_eq!(
                view.record_without_files, !with_files,
                "{os:?} {with_files}"
            );
            assert_eq!(
                view.record_version_differs, with_files,
                "{os:?} {with_files}"
            );
            let _ = take_mine();
        }
    }

    // A failed read: `record_error` and one warn naming the code and the plugin.
    for (os, host, code) in [
        (
            Os::Macos,
            FakeHostFacts {
                pkg: BTreeMap::from([(ALPHA_PKG.to_owned(), RecordRead::Failed(E_RECORD_TIMEOUT))]),
                ..FakeHostFacts::default()
            },
            E_RECORD_TIMEOUT,
        ),
        (
            Os::Windows,
            FakeHostFacts {
                inno: BTreeMap::from([(ALPHA_KEY.to_owned(), RecordRead::Failed(E_RECORD_READ))]),
                ..FakeHostFacts::default()
            },
            E_RECORD_READ,
        ),
    ] {
        let (_td, roots) = fresh_temp_roots();
        let _ = take_mine();
        let d = detect_plugin(os, &context(&roots), &alpha, &host, &Listing::default());
        let lines = detect_lines();
        assert!(
            matches!(&d, Detection::Checked { record: None, record_error: Some(c), .. } if *c == code),
            "{os:?}: {d:?}"
        );
        let warned = warns(&lines);
        assert_eq!(
            warned,
            [format!("{code} Alpha: installer record")],
            "{os:?}"
        );
        let detections = BTreeMap::from([("Alpha".to_owned(), d)]);
        assert_eq!(
            assembled(os, &full, &detections, "Alpha").record_error,
            Some(code.to_owned())
        );
    }

    // Windows install locations: under the vendor folder, and outside every root (ruled Q2, D1).
    for in_vendor in [true, false] {
        let (_td, roots) = fresh_temp_roots();
        let base = base_of(&roots);
        let ctx = context(&roots);
        let user = root(&roots, RootKind::Vst3User);
        let global = root(&roots, RootKind::Vst3Global);
        let loc = if in_vendor {
            global.join(VENDOR_DIR).join("Custom")
        } else {
            base.join("elsewhere")
        };
        let vendor = global.join(VENDOR_DIR).join("Alpha").join("Alpha.vst3");
        bundle(&loc.join("Alpha.vst3"), &release("1.2.0"));
        bundle(&vendor, &release("1.1.0"));
        bundle(&user.join("Alpha.vst3"), &release("1.0.0"));
        let host = inno_host(InnoValues {
            display_version: Some("1.2.0".to_owned()),
            install_location: Some(loc.to_str().expect("UTF-8").to_owned()),
        });
        let d = detect_plugin(Os::Windows, &ctx, &alpha, &host, &Listing::default());
        let got: Vec<(Place, &Path, bool, Option<u8>)> = copies(&d)
            .iter()
            .map(|c| (c.place, c.path.as_path(), c.managed, c.rank))
            .collect();
        let loc_bundle = loc.join("Alpha.vst3");
        let user_bundle = user.join("Alpha.vst3");
        assert_eq!(
            got,
            [
                (
                    Place::Root(RootKind::Vst3User),
                    user_bundle.as_path(),
                    false,
                    Some(0)
                ),
                (
                    Place::WindowsVendor(RootKind::Vst3Global),
                    vendor.as_path(),
                    false,
                    Some(1)
                ),
                (Place::InstallerLocation, loc_bundle.as_path(), true, None),
            ],
            "in vendor folder: {in_vendor}"
        );
        assert!(matches!(
            d,
            Detection::Checked {
                managed_rank: None,
                ..
            }
        ));
        let detections = BTreeMap::from([("Alpha".to_owned(), d)]);
        let view = assembled(Os::Windows, &full, &detections, "Alpha");
        assert_eq!(view.installed, InstalledView::Version);
        assert_eq!(view.installed_version.as_deref(), Some("1.2.0"));
        assert!(
            view.copies.iter().all(|c| !c.loads_before_managed),
            "{:?}",
            view.copies
        );
        let _ = take_mine();
    }
}

#[test]
#[should_panic(expected = "outside the test sandbox")]
fn outside_install_location_panics() {
    let (_td, roots) = fresh_temp_roots();
    let outside = base_of(&roots)
        .parent()
        .expect("the base has a parent")
        .join("outside-sandbox");
    let host = inno_host(InnoValues {
        display_version: Some("1.2.0".to_owned()),
        install_location: Some(outside.to_str().expect("UTF-8").to_owned()),
    });
    let alpha = entry(&feed_fixture("full-v2.json"), "Alpha");
    let _ = detect_plugin(
        Os::Windows,
        &context(&roots),
        &alpha,
        &host,
        &Listing::default(),
    );
}

// ---- SC-010 (first half): the OS-version parsers --------------------------------------------------------------------

#[test]
fn os_version_parsers() {
    let text = |v: Option<OsVersion>| v.map(|v| v.to_string());
    for (input, want) in [
        ("15.7.9", Some("15.7.9")),
        ("26.0", Some("26.0")),
        ("26.6.2\n", Some("26.6.2")),
        ("", None),
        ("abc", None),
        ("15", None),
    ] {
        assert_eq!(
            text(parse_product_version(input)),
            want.map(str::to_owned),
            "{input:?}"
        );
    }
    for (input, want) in [
        ("glibc 2.39\n", Some("2.39")),
        ("glibc 2.35", Some("2.35")),
        ("musl 1.2", None),
        ("2.39", None),
        ("glibc", None),
    ] {
        assert_eq!(
            text(parse_glibc_version(input)),
            want.map(str::to_owned),
            "{input:?}"
        );
    }
    assert_eq!(
        text(windows_version(10, 0, 26200)).as_deref(),
        Some("10.0.26200")
    );
}

// ---- SC-013, SC-014: macOS ------------------------------------------------------------------------------------------

#[cfg(target_os = "macos")]
#[test]
fn au_plist_cases_macos() {
    use UnknownReason as R;
    install_capture();
    let (_td, roots) = fresh_temp_roots();
    let au_global = root(&roots, RootKind::AuGlobal);
    let info_plist = |b: &Path| b.join("Contents").join("Info.plist");
    let au = |path: PathBuf| Candidate {
        format: CopyFormat::Au,
        place: Place::Root(RootKind::AuGlobal),
        path,
        managed: true,
        rank: None,
    };
    let mut binary = Vec::new();
    plist::Value::from_reader(std::io::Cursor::new(fixture("plist/short-120.plist")))
        .expect("the fixture parses")
        .to_writer_binary(&mut binary)
        .expect("binary plist");
    let mut too_large = fixture("plist/template.plist");
    too_large.resize(
        usize::try_from(MAX_MODULEINFO_BYTES + 1).expect("fits"),
        b' ',
    );
    // (name, Info.plist bytes or None for no file, expected, expected E_DETECT_PLIST warn count)
    type PlistCase = (&'static str, Option<Vec<u8>>, CopyVersion, usize);
    let cases: Vec<PlistCase> = vec![
        (
            "template",
            Some(fixture("plist/template.plist")),
            CopyVersion::Unknown(R::AuNoVersion),
            0,
        ),
        (
            "short 1.2.0 xml",
            Some(fixture("plist/short-120.plist")),
            known("1.2.0", VersionSource::AuPlist),
            0,
        ),
        (
            "short 1.2.0 binary",
            Some(binary),
            known("1.2.0", VersionSource::AuPlist),
            0,
        ),
        (
            "short 1.2",
            Some(fixture("plist/short-12.plist")),
            CopyVersion::Unknown(R::AuNoVersion),
            0,
        ),
        (
            "short 1.2.3.4",
            Some(fixture("plist/short-1234.plist")),
            CopyVersion::Unknown(R::AuNoVersion),
            0,
        ),
        (
            "corrupt",
            Some(fixture("plist/corrupt.plist")),
            CopyVersion::Unknown(R::Corrupt),
            1,
        ),
        (
            "no Info.plist",
            None,
            CopyVersion::Unknown(R::Unreadable),
            1,
        ),
        (
            "1 MiB + 1 byte",
            Some(too_large),
            CopyVersion::Unknown(R::TooLarge),
            1,
        ),
    ];
    for (i, (name, bytes, want, warn_count)) in cases.into_iter().enumerate() {
        let b = au_global.join(format!("Case{i}.component"));
        match bytes {
            Some(bytes) => write(&info_plist(&b), &bytes),
            None => mkdirs(&b.join("Contents")),
        }
        let _ = take_mine();
        let copy = probe(&au(b)).unwrap_or_else(|| panic!("{name}: a copy"));
        let lines = detect_lines();
        assert_eq!(copy.version, want, "{name}");
        let warned = warns(&lines);
        assert_eq!(warned.len(), warn_count, "{name}: {warned:?}");
        assert!(
            warned.iter().all(|w| w.starts_with(E_DETECT_PLIST)),
            "{name}: {warned:?}"
        );
        assert!(
            lines.iter().all(|(_, m)| !m.contains(MARKER)),
            "{name}: {lines:?}"
        );
    }
    let file = au_global.join("File.component");
    write(&file, b"not a bundle");
    assert_eq!(
        probe(&au(file)).map(|c| c.version),
        Some(CopyVersion::Unknown(R::NotABundle))
    );

    // An AU copy never sets the installed state.
    let (_td2, roots) = fresh_temp_roots();
    let ctx = context(&roots);
    let full = feed_fixture("full-v2.json");
    write(
        &info_plist(&root(&roots, RootKind::AuGlobal).join("Alpha.component")),
        &fixture("plist/short-120.plist"),
    );
    let detections = detect_all(
        Os::Macos,
        &ctx,
        &full,
        &FakeHostFacts::default(),
        &Listing::default(),
    );
    let view = assembled(Os::Macos, &full, &detections, "Alpha");
    assert_eq!(view.installed, InstalledView::NotInstalled);
    assert_eq!(view.copies.len(), 1);
    assert_eq!(view.copies[0].format, FormatView::Au);
}

#[cfg(target_os = "macos")]
#[test]
fn pkg_receipt_macos() {
    let argv = |flag: &str| -> Vec<String> {
        [platform::PKGUTIL, flag, ALPHA_PKG]
            .iter()
            .map(|s| (*s).to_owned())
            .collect()
    };
    let call = |flag: &str| format!("run {} {flag} {ALPHA_PKG}", platform::PKGUTIL);
    let full = feed_fixture("full-v2.json");
    let alpha = entry(&full, "Alpha");
    for (bundle_version, differs) in [("1.2.0", false), ("1.1.0", true)] {
        let (_td, roots) = fresh_temp_roots();
        let ctx = context(&roots);
        bundle(
            &root(&roots, RootKind::Vst3Global).join("Alpha.vst3"),
            &release(bundle_version),
        );
        write(
            &root(&roots, RootKind::AuGlobal)
                .join("Alpha.component")
                .join("Contents")
                .join("Info.plist"),
            &fixture("plist/short-120.plist"),
        );
        let fake = FakeHostIo {
            runs: BTreeMap::from([
                (
                    argv(platform::PKG_INFO_FLAG),
                    RunOutcome::Output(fixture_text("pkgutil/alpha-pkg-info.txt")),
                ),
                (
                    argv(platform::PKG_FILES_FLAG),
                    RunOutcome::Output(fixture_text("pkgutil/alpha-files.txt")),
                ),
            ]),
            ..FakeHostIo::default()
        };
        let host = PlatformFacts { io: &fake };
        let d = detect_plugin(Os::Macos, &ctx, &alpha, &host, &Listing::default());
        assert!(matches!(
            &d,
            Detection::Checked { record: Some(InstallerRecord::Pkg { version_text, files: 3, .. }), .. }
                if version_text == "1.2.0"
        ));
        assert_eq!(
            *fake.calls.borrow(),
            [
                call(platform::PKG_INFO_FLAG),
                call(platform::PKG_FILES_FLAG)
            ]
        );
        let detections = BTreeMap::from([("Alpha".to_owned(), d)]);
        let view = assembled(Os::Macos, &full, &detections, "Alpha");
        assert_eq!(
            view.record,
            Some(RecordView {
                kind: RecordKind::Pkg,
                version: Some("1.2.0".to_owned()),
                files: Some(3),
            })
        );
        assert_eq!(view.record_version_differs, differs, "{bundle_version}");
    }

    let (_td, roots) = fresh_temp_roots();
    let fake = FakeHostIo::default();
    let host = PlatformFacts { io: &fake };
    let d = detect_plugin(
        Os::Macos,
        &context(&roots),
        &alpha,
        &host,
        &Listing::default(),
    );
    assert!(matches!(
        d,
        Detection::Checked {
            record: None,
            record_error: None,
            ..
        }
    ));
    assert_eq!(*fake.calls.borrow(), [call(platform::PKG_INFO_FLAG)]);
}

// ---- SC-015: Linux --------------------------------------------------------------------------------------------------

#[cfg(target_os = "linux")]
#[test]
fn linux_detection_linux() {
    use crate::context::{AppState, reassemble_with_os};
    let full = feed_fixture("full-v2.json");
    let alpha = entry(&full, "Alpha");
    let manifest_v120 = fixture("manifest/alpha-v1.json");

    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots);
    let user = root(&roots, RootKind::Vst3User);
    let global = root(&roots, RootKind::Vst3Global);
    bundle(&user.join("Alpha.vst3"), &release("1.2.0"));
    bundle(&global.join("Alpha.vst3"), &release("1.1.0"));
    write(
        &roots.state_dir().join(MANIFESTS_DIR).join("Alpha.json"),
        &manifest_v120,
    );
    let detections = detect_all(
        Os::Linux,
        &ctx,
        &full,
        &FakeHostFacts::default(),
        &Listing::default(),
    );
    let view = assembled(Os::Linux, &full, &detections, "Alpha");
    assert_eq!(view.installed, InstalledView::Version);
    assert_eq!(view.installed_version.as_deref(), Some("1.2.0"));
    assert_eq!(
        view.record,
        Some(RecordView {
            kind: RecordKind::Manifest,
            version: Some("1.2.0".to_owned()),
            files: None,
        })
    );
    assert!(!view.record_without_files);
    assert_eq!(view.copies.len(), 2);
    assert!(view.copies.iter().all(|c| !c.loads_before_managed));

    // The configured root (built through reassemble_with_os) is the managed one.
    let custom = base_of(&roots).join("custom");
    bundle(&custom.join("Alpha.vst3"), &release("1.0.0"));
    let prev = AppState {
        context: Ok(ctx),
        settings_error: None,
        lock: None,
    };
    let next = reassemble_with_os(
        Os::Linux,
        &prev,
        Settings {
            linux_plugin_root: Some(custom.clone()),
            ..Settings::default()
        },
    );
    let configured = next
        .context
        .as_ref()
        .expect("valid settings keep a context");
    let d = detect_plugin(
        Os::Linux,
        configured,
        &alpha,
        &FakeHostFacts::default(),
        &Listing::default(),
    );
    let managed = managed_copy(&d).expect("the configured copy");
    assert_eq!(managed.path, custom.join("Alpha.vst3"));
    assert_eq!(managed.place, Place::Root(RootKind::Configured));

    // A manifest with no bundle.
    let (_td2, roots) = fresh_temp_roots();
    write(
        &roots.state_dir().join(MANIFESTS_DIR).join("Alpha.json"),
        &manifest_v120,
    );
    let detections = detect_all(
        Os::Linux,
        &context(&roots),
        &full,
        &FakeHostFacts::default(),
        &Listing::default(),
    );
    assert!(assembled(Os::Linux, &full, &detections, "Alpha").record_without_files);
}

// ---- SC-016: a locked moduleinfo (Windows) --------------------------------------------------------------------------

#[cfg(windows)]
#[test]
fn locked_moduleinfo_windows() {
    use std::os::windows::fs::OpenOptionsExt;
    install_capture();
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots);
    let full = feed_fixture("full-v2.json");
    let alpha = entry(&full, "Alpha");
    let bravo = entry(&full, "Bravo");
    let alpha_path = managed_vst3(Os::Windows, &ctx, &facts(&alpha));
    let bravo_path = managed_vst3(Os::Windows, &ctx, &facts(&bravo));
    bundle(&alpha_path, &release("1.2.0"));
    bundle(&bravo_path, &release("2.0.1"));
    let held = fs::OpenOptions::new()
        .read(true)
        .share_mode(0)
        .open(moduleinfo_path(&alpha_path))
        .expect("open the moduleinfo exclusively");
    let _ = take_mine();
    let host = FakeHostFacts::default();
    let a = detect_plugin(Os::Windows, &ctx, &alpha, &host, &Listing::default());
    let b = detect_plugin(Os::Windows, &ctx, &bravo, &host, &Listing::default());
    let lines = detect_lines();
    drop(held);
    assert_eq!(
        managed_copy(&a).map(|c| c.version),
        Some(CopyVersion::Unknown(UnknownReason::Unreadable))
    );
    assert_eq!(
        managed_copy(&b).map(|c| c.version),
        Some(known("2.0.1", VersionSource::Classes))
    );
    let warned = warns(&lines);
    assert_eq!(warned.len(), 1, "{warned:?}");
    assert!(warned[0].starts_with(E_DETECT_MODULEINFO), "{}", warned[0]);
}

// ---- SC-017: detection writes nothing -------------------------------------------------------------------------------

/// Relative path → (kind, size, modification time) of everything under `base`, links not followed.
fn snapshot(base: &Path) -> BTreeMap<PathBuf, (&'static str, u64, SystemTime)> {
    fn walk(base: &Path, dir: &Path, out: &mut BTreeMap<PathBuf, (&'static str, u64, SystemTime)>) {
        for item in fs::read_dir(dir).unwrap_or_else(|e| panic!("list {}: {e}", dir.display())) {
            let path = item.expect("a directory entry").path();
            let meta = fs::symlink_metadata(&path).expect("symlink_metadata");
            let kind = if meta.is_dir() {
                "dir"
            } else if meta.is_file() {
                "file"
            } else {
                "other"
            };
            let rel = path
                .strip_prefix(base)
                .expect("under the base")
                .to_path_buf();
            out.insert(rel, (kind, meta.len(), meta.modified().expect("mtime")));
            if meta.is_dir() {
                walk(base, &path, out);
            }
        }
    }
    let mut out = BTreeMap::new();
    walk(base, base, &mut out);
    out
}

#[test]
fn detection_leaves_the_tree_unchanged() {
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    let feed = feed_fixture("full-v2.json");
    let os = NativePlatform.os();
    let ctx0 = context(&roots);
    bundle(
        &managed_vst3(os, &ctx0, &facts(&entry(&feed, "Alpha"))),
        &release("1.2.0"),
    );
    bundle(
        &root(&roots, RootKind::Vst3User).join("Bravo.vst3"),
        &release("2.0.0"),
    );
    write(
        &root(&roots, RootKind::Vst3Global)
            .join("Old")
            .join("Charlie.vst3"),
        b"a single-file copy",
    );
    let server = FixtureServer::start(vec![(
        FEED_PATH.to_owned(),
        Reply::Ok {
            body: feed_bytes("full-v2.json"),
            content_type: "application/json",
        },
    )]);
    let ctx = AppContext {
        feed_url: server.url(FEED_PATH),
        ..ctx0
    };
    let before = snapshot(&base);
    let view = status::build(&ctx, FetchMode::Normal, NOW, &FakeHostFacts::default());
    assert_eq!(view.feed.source, FeedSource::Fresh);
    assert_eq!(view.plugins.len(), 3);
    let after = snapshot(&base);

    let state = PathBuf::from("state");
    let cache = state.join(feed::FEED_CACHE_FILE);
    assert!(after.contains_key(&cache), "the feed cache was written");
    let keys: std::collections::BTreeSet<&PathBuf> = before.keys().chain(after.keys()).collect();
    for key in keys {
        if *key == state || *key == cache {
            continue;
        }
        assert_eq!(before.get(key), after.get(key), "{}", key.display());
    }
}

// ---- SC-025: the per-OS Platform host methods -----------------------------------------------------------------------

#[cfg(target_os = "windows")]
#[test]
fn host_facts_through_fake_io_windows() {
    let key = plain("windows_uninstall_key", ALPHA_KEY);
    let subkey = format!("{}\\{}", platform::UNINSTALL_KEY, ALPHA_KEY);
    let call = format!(
        "registry {subkey} {} {}",
        platform::DISPLAY_VERSION,
        platform::INSTALL_LOCATION
    );
    let cases = [
        (None, RecordRead::Absent),
        (
            Some(RegistryRead::Values([Some("1.2.0".to_owned()), None])),
            RecordRead::Found(InnoValues {
                display_version: Some("1.2.0".to_owned()),
                install_location: None,
            }),
        ),
        (
            Some(RegistryRead::Values([None, None])),
            RecordRead::Found(InnoValues::default()),
        ),
        (
            Some(RegistryRead::Failed),
            RecordRead::Failed(E_RECORD_READ),
        ),
    ];
    for (read, want) in cases {
        let io = FakeHostIo {
            registry: read
                .map(|r| BTreeMap::from([(subkey.clone(), r)]))
                .unwrap_or_default(),
            ..FakeHostIo::default()
        };
        assert_eq!(NativePlatform.inno_values(&io, &key), want);
        assert_eq!(io.calls.borrow().as_slice(), [call.as_str()]);
    }
    for (rtl, want) in [(Some((10, 0, 26200)), Some("10.0.26200")), (None, None)] {
        let io = FakeHostIo {
            rtl,
            ..FakeHostIo::default()
        };
        assert_eq!(
            NativePlatform
                .os_version(&io)
                .map(|v| v.to_string())
                .as_deref(),
            want
        );
        assert_eq!(*io.calls.borrow(), ["rtl_version"]);
    }
    let io = FakeHostIo::default();
    assert_eq!(
        NativePlatform.pkg_outputs(&io, &plain("macos_pkg_id", ALPHA_PKG)),
        RecordRead::Absent
    );
    assert!(io.calls.borrow().is_empty());
}

#[cfg(target_os = "macos")]
#[test]
fn host_facts_through_fake_io_macos() {
    let id = plain("macos_pkg_id", ALPHA_PKG);
    let argv = |flag: &str| -> Vec<String> {
        [platform::PKGUTIL, flag, ALPHA_PKG]
            .iter()
            .map(|s| (*s).to_owned())
            .collect()
    };
    let call = |flag: &str| format!("run {} {flag} {ALPHA_PKG}", platform::PKGUTIL);
    let info = fixture_text("pkgutil/alpha-pkg-info.txt");
    let files = fixture_text("pkgutil/alpha-files.txt");
    let out = |s: &str| RunOutcome::Output(s.to_owned());
    let cases = [
        (
            Some(out(&info)),
            Some(out(&files)),
            RecordRead::Found(PkgOutputs {
                pkg_info: info.clone(),
                files: files.clone(),
            }),
            2,
        ),
        (None, None, RecordRead::Absent, 1),
        (
            Some(RunOutcome::Abandoned),
            None,
            RecordRead::Failed(E_RECORD_TIMEOUT),
            1,
        ),
        (Some(out(&info)), None, RecordRead::Failed(E_RECORD_READ), 2),
        (
            Some(out(&info)),
            Some(RunOutcome::Abandoned),
            RecordRead::Failed(E_RECORD_TIMEOUT),
            2,
        ),
    ];
    for (info_run, files_run, want, n) in cases {
        let mut runs = BTreeMap::new();
        if let Some(r) = info_run {
            runs.insert(argv(platform::PKG_INFO_FLAG), r);
        }
        if let Some(r) = files_run {
            runs.insert(argv(platform::PKG_FILES_FLAG), r);
        }
        let io = FakeHostIo {
            runs,
            ..FakeHostIo::default()
        };
        assert_eq!(NativePlatform.pkg_outputs(&io, &id), want);
        let expected: Vec<String> = [
            call(platform::PKG_INFO_FLAG),
            call(platform::PKG_FILES_FLAG),
        ]
        .into_iter()
        .take(n)
        .collect();
        assert_eq!(*io.calls.borrow(), expected);
    }
    let sw = vec![
        platform::SW_VERS.to_owned(),
        platform::SW_VERS_FLAG.to_owned(),
    ];
    let sw_call = format!("run {} {}", platform::SW_VERS, platform::SW_VERS_FLAG);
    for (run, want) in [(Some(out("15.7.9\n")), Some("15.7.9")), (None, None)] {
        let io = FakeHostIo {
            runs: run
                .map(|r| BTreeMap::from([(sw.clone(), r)]))
                .unwrap_or_default(),
            ..FakeHostIo::default()
        };
        assert_eq!(
            NativePlatform
                .os_version(&io)
                .map(|v| v.to_string())
                .as_deref(),
            want
        );
        assert_eq!(io.calls.borrow().as_slice(), [sw_call.as_str()]);
    }
    let io = FakeHostIo::default();
    assert_eq!(
        NativePlatform.inno_values(&io, &plain("windows_uninstall_key", ALPHA_KEY)),
        RecordRead::Absent
    );
    assert!(io.calls.borrow().is_empty());
}

#[cfg(target_os = "linux")]
#[test]
fn host_facts_through_fake_io_linux() {
    let argv = vec![
        platform::GETCONF.to_owned(),
        platform::GLIBC_NAME.to_owned(),
    ];
    let call = format!("run {} {}", platform::GETCONF, platform::GLIBC_NAME);
    for (run, want) in [
        (
            Some(RunOutcome::Output("glibc 2.39\n".to_owned())),
            Some("2.39"),
        ),
        (Some(RunOutcome::Abandoned), None),
        (None, None),
    ] {
        let io = FakeHostIo {
            runs: run
                .map(|r| BTreeMap::from([(argv.clone(), r)]))
                .unwrap_or_default(),
            ..FakeHostIo::default()
        };
        assert_eq!(
            NativePlatform
                .os_version(&io)
                .map(|v| v.to_string())
                .as_deref(),
            want
        );
        assert_eq!(io.calls.borrow().as_slice(), [call.as_str()]);
    }
    let io = FakeHostIo::default();
    assert_eq!(
        NativePlatform.inno_values(&io, &plain("windows_uninstall_key", ALPHA_KEY)),
        RecordRead::Absent
    );
    assert_eq!(
        NativePlatform.pkg_outputs(&io, &plain("macos_pkg_id", ALPHA_PKG)),
        RecordRead::Absent
    );
    assert!(io.calls.borrow().is_empty());
}

// ---- SC-026: the pure host mappers ----------------------------------------------------------------------------------

#[test]
fn host_read_mappers() {
    use std::cell::Cell;
    assert_eq!(inno_read_from(RegistryRead::NoKey), RecordRead::Absent);
    assert_eq!(
        inno_read_from(RegistryRead::Values([
            Some("1.2.0".to_owned()),
            Some("loc".to_owned())
        ])),
        RecordRead::Found(InnoValues {
            display_version: Some("1.2.0".to_owned()),
            install_location: Some("loc".to_owned()),
        })
    );
    assert_eq!(
        inno_read_from(RegistryRead::Values([None, None])),
        RecordRead::Found(InnoValues::default())
    );
    assert_eq!(
        inno_read_from(RegistryRead::Failed),
        RecordRead::Failed(E_RECORD_READ)
    );

    let out = |s: &str| RunOutcome::Output(s.to_owned());
    let found = RecordRead::Found(PkgOutputs {
        pkg_info: "i".to_owned(),
        files: "f".to_owned(),
    });
    for (info, files, want, calls) in [
        (RunOutcome::NoOutput, out("f"), RecordRead::Absent, 0),
        (
            RunOutcome::Abandoned,
            out("f"),
            RecordRead::Failed(E_RECORD_TIMEOUT),
            0,
        ),
        (out("i"), out("f"), found, 1),
        (
            out("i"),
            RunOutcome::Abandoned,
            RecordRead::Failed(E_RECORD_TIMEOUT),
            1,
        ),
        (
            out("i"),
            RunOutcome::NoOutput,
            RecordRead::Failed(E_RECORD_READ),
            1,
        ),
    ] {
        let count = Cell::new(0);
        let got = pkg_read_from(info, || {
            count.set(count.get() + 1);
            files
        });
        assert_eq!(got, want);
        assert_eq!(count.get(), calls);
    }

    let text = |v: Option<OsVersion>| v.map(|v| v.to_string());
    assert_eq!(
        text(os_version_from_run(out("15.7.9\n"), parse_product_version)).as_deref(),
        Some("15.7.9")
    );
    assert_eq!(
        text(os_version_from_run(out("glibc 2.39"), parse_glibc_version)).as_deref(),
        Some("2.39")
    );
    assert_eq!(os_version_from_run(out("x"), parse_product_version), None);
    assert_eq!(
        os_version_from_run(RunOutcome::NoOutput, parse_product_version),
        None
    );
    assert_eq!(
        os_version_from_run(RunOutcome::Abandoned, parse_product_version),
        None
    );
}

// ---- SC-027: a metadata error that is not NotFound (Unix) -----------------------------------------------------------

#[cfg(unix)]
#[test]
fn probe_metadata_error_unix() {
    use std::sync::mpsc;
    install_capture();
    let (_td, roots) = fresh_temp_roots();
    let global = root(&roots, RootKind::Vst3Global);
    let user = root(&roots, RootKind::Vst3User);

    // ENOTDIR: the parent of the candidate is a regular file.
    fs::remove_dir(&global).expect("remove vst3-global");
    write(&global, b"a file where a directory was");
    let candidate = cand(
        CopyFormat::Vst3,
        Place::Root(RootKind::Vst3Global),
        global.join("Alpha.vst3"),
        true,
        Some(1),
    );
    let _ = take_mine();
    let copy = probe(&candidate).expect("a metadata error is a copy");
    let lines = detect_lines();
    assert_eq!(
        copy.version,
        CopyVersion::Unknown(UnknownReason::Unreadable)
    );
    let warned = warns(&lines);
    assert_eq!(warned.len(), 1, "{warned:?}");
    assert!(warned[0].starts_with(E_DETECT_MODULEINFO), "{}", warned[0]);

    // A FIFO named moduleinfo.json is never opened (PD-22).
    let fifo_bundle = user.join("Alpha.vst3");
    let fifo = moduleinfo_path(&fifo_bundle);
    mkdirs(fifo.parent().expect("Resources"));
    let made = std::process::Command::new("mkfifo")
        .arg(&fifo)
        .status()
        .expect("run mkfifo");
    assert!(made.success(), "mkfifo exited {made}");
    let candidate = cand(
        CopyFormat::Vst3,
        Place::Root(RootKind::Vst3User),
        fifo_bundle,
        false,
        Some(0),
    );
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        install_capture();
        let _ = take_mine();
        let copy = probe(&candidate);
        let _ = tx.send((copy, detect_lines()));
    });
    let (copy, lines) = rx
        .recv_timeout(Duration::from_secs(5))
        .expect("probe returns instead of blocking on the FIFO");
    assert_eq!(
        copy.map(|c| c.version),
        Some(CopyVersion::Unknown(UnknownReason::Unreadable))
    );
    let warned = warns(&lines);
    assert_eq!(warned.len(), 1, "{warned:?}");
    assert!(warned[0].starts_with(E_DETECT_MODULEINFO), "{}", warned[0]);
}

// ---- SC-029: nested copies ------------------------------------------------------------------------------------------

#[test]
fn nested_copies_listed() {
    use RootKind::{Vst3Global, Vst3User};
    let feed = feed_fixture("tenth-plugin.json");
    let alpha = entry(&feed, "Alpha");
    let delta = entry(&feed, "Delta");
    let (_td, roots) = fresh_temp_roots();
    let ctx = context(&roots);
    let user = root(&roots, Vst3User);
    let global = root(&roots, Vst3Global);
    let old = user.join("Old").join("Alpha.vst3");
    let in_vendor = global.join(VENDOR_DIR).join("Alpha.vst3");
    let deep = global.join("a").join("b").join("c").join("Alpha.vst3");
    let fixed_global = global.join("Alpha.vst3");
    for b in [&old, &in_vendor, &deep, &fixed_global] {
        bundle(b, &release("1.1.0"));
    }
    // Not matches: depth 5, another name, inside a matched bundle.
    bundle(
        &global
            .join("a")
            .join("b")
            .join("c")
            .join("d")
            .join("Alpha.vst3"),
        &release("1.0.0"),
    );
    bundle(&user.join("Old").join("Other.vst3"), &release("1.0.0"));
    bundle(&old.join("Contents").join("Alpha.vst3"), &release("1.0.0"));

    let listing = Listing::scan(
        roots.plugin_roots(),
        &["Alpha.vst3", "Delta.vst3"],
        ListingLimits::PRODUCTION,
    );
    assert_eq!(
        listing.copies_of("Alpha.vst3"),
        [
            ListedCopy {
                root: Vst3User,
                root_index: 0,
                path: old.clone()
            },
            ListedCopy {
                root: Vst3Global,
                root_index: 1,
                path: fixed_global.clone()
            },
            ListedCopy {
                root: Vst3Global,
                root_index: 1,
                path: in_vendor.clone()
            },
            ListedCopy {
                root: Vst3Global,
                root_index: 1,
                path: deep.clone()
            },
        ]
    );
    assert!(listing.copies_of("Delta.vst3").is_empty());

    for os in OSES {
        let d = detect_plugin(os, &ctx, &alpha, &FakeHostFacts::default(), &listing);
        let got: Vec<(Place, &Path, bool, Option<u8>)> = copies(&d)
            .iter()
            .map(|c| (c.place, c.path.as_path(), c.managed, c.rank))
            .collect();
        // The fixed global copy once, with the fixed candidate's flags (managed on macOS only), then the three
        // nested copies in listing order.
        let fixed_managed = os == Os::Macos;
        assert_eq!(
            got,
            [
                (
                    Place::Root(Vst3Global),
                    fixed_global.as_path(),
                    fixed_managed,
                    Some(1)
                ),
                (Place::Nested(Vst3User), old.as_path(), false, Some(0)),
                (
                    Place::Nested(Vst3Global),
                    in_vendor.as_path(),
                    false,
                    Some(1)
                ),
                (Place::Nested(Vst3Global), deep.as_path(), false, Some(1)),
            ],
            "{os:?}"
        );
        let d_delta = detect_plugin(os, &ctx, &delta, &FakeHostFacts::default(), &listing);
        assert!(copies(&d_delta).is_empty(), "{os:?}");

        let detections = BTreeMap::from([("Alpha".to_owned(), d), ("Delta".to_owned(), d_delta)]);
        let view = assembled(os, &feed, &detections, "Alpha");
        let user_copy = view
            .copies
            .iter()
            .find(|c| c.path == old.display().to_string())
            .expect("the user copy");
        assert!(user_copy.nested);
        assert_eq!(user_copy.place, PlaceView::Vst3User);
        assert_eq!(user_copy.loads_before_managed, os != Os::Linux, "{os:?}");
        assert_eq!(view.copies.iter().filter(|c| c.nested).count(), 3, "{os:?}");
    }

    // Two nested copies of one root, in name order.
    let (_td2, roots) = fresh_temp_roots();
    let user = root(&roots, Vst3User);
    let b_copy = user.join("b").join("Alpha.vst3");
    let a_copy = user.join("a").join("Alpha.vst3");
    bundle(&b_copy, &release("1.0.0"));
    bundle(&a_copy, &release("1.0.0"));
    let listing = Listing::scan(
        roots.plugin_roots(),
        &["Alpha.vst3"],
        ListingLimits::PRODUCTION,
    );
    let paths: Vec<PathBuf> = listing
        .copies_of("Alpha.vst3")
        .into_iter()
        .map(|c| c.path)
        .collect();
    assert_eq!(paths, [a_copy, b_copy]);
}

#[cfg(windows)]
#[test]
fn listing_case_windows() {
    let (_td, roots) = fresh_temp_roots();
    let copy = root(&roots, RootKind::Vst3User)
        .join("Old")
        .join("alpha.vst3");
    bundle(&copy, &release("1.0.0"));
    let listing = Listing::scan(
        roots.plugin_roots(),
        &["Alpha.vst3"],
        ListingLimits::PRODUCTION,
    );
    assert_eq!(listing.paths(), [copy.as_path()]);
    assert_eq!(
        listing.copies_of("Alpha.vst3"),
        [ListedCopy {
            root: RootKind::Vst3User,
            root_index: 0,
            path: copy.clone(),
        }]
    );
}

#[cfg(unix)]
#[test]
fn listing_case_exact_unix() {
    let (_td, roots) = fresh_temp_roots();
    let copy = root(&roots, RootKind::Vst3User)
        .join("Old")
        .join("alpha.vst3");
    bundle(&copy, &release("1.0.0"));
    let listing = Listing::scan(
        roots.plugin_roots(),
        &["Alpha.vst3"],
        ListingLimits::PRODUCTION,
    );
    assert!(listing.paths().is_empty(), "{:?}", listing.paths());
    assert!(listing.copies_of("Alpha.vst3").is_empty());
}

#[cfg(unix)]
#[test]
fn listing_skips_symlinked_dirs_unix() {
    use std::os::unix::fs::{PermissionsExt, symlink};
    /// Restores the directory's mode before the `TempDir` drops, even when an assertion fails.
    struct Restore(PathBuf);
    impl Drop for Restore {
        fn drop(&mut self) {
            let _ = fs::set_permissions(&self.0, fs::Permissions::from_mode(0o755));
        }
    }
    install_capture();
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    let user = root(&roots, RootKind::Vst3User);
    let outside = base.join("outside");
    bundle(&outside.join("Alpha.vst3"), &release("1.0.0"));
    symlink(&outside, user.join("link")).expect("a directory symlink");
    mkdirs(&user.join("a"));
    symlink("..", user.join("a").join("loop")).expect("a link loop");
    let locked = user.join("locked");
    bundle(&locked.join("Alpha.vst3"), &release("1.0.0"));
    fs::set_permissions(&locked, fs::Permissions::from_mode(0o000)).expect("chmod 000");
    let _restore = Restore(locked.clone());
    assert!(
        fs::read_dir(&locked).is_err(),
        "a 0o000 directory must be unreadable (is the test running as root?)"
    );

    let _ = take_mine();
    let listing = Listing::scan(
        roots.plugin_roots(),
        &["Alpha.vst3"],
        ListingLimits::PRODUCTION,
    );
    let lines = detect_lines();
    assert!(listing.paths().is_empty(), "{:?}", listing.paths());
    assert!(warns(&lines).is_empty(), "{lines:?}");
    // vst3-user: a, link, locked, a/loop.
    assert!(
        lines
            .iter()
            .any(|(l, m)| *l == Level::Debug && m == "listing: Vst3User: 4 entries examined"),
        "{lines:?}"
    );
}

#[cfg(windows)]
#[test]
fn listing_skips_junction_windows() {
    install_capture();
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    let user = root(&roots, RootKind::Vst3User);
    let outside = base.join("outside");
    bundle(&outside.join("Alpha.vst3"), &release("1.0.0"));
    let a = user.join("a");
    mkdirs(&a);
    // mklink takes plain drive paths, not the verbatim form the canonical temp base has.
    let plain_path = |p: &Path| {
        let s = p.to_str().expect("UTF-8");
        s.strip_prefix(r"\\?\").unwrap_or(s).to_owned()
    };
    for (link, target) in [
        (a.join("loop"), a.clone()),
        (user.join("out"), outside.clone()),
    ] {
        let made = std::process::Command::new("cmd")
            .args(["/C", "mklink", "/J"])
            .arg(plain_path(&link))
            .arg(plain_path(&target))
            .output()
            .expect("run cmd");
        assert!(made.status.success(), "mklink /J failed: {made:?}");
    }
    let _ = take_mine();
    let listing = Listing::scan(
        roots.plugin_roots(),
        &["Alpha.vst3"],
        ListingLimits::PRODUCTION,
    );
    let lines = detect_lines();
    assert!(listing.paths().is_empty(), "{:?}", listing.paths());
    // vst3-user: a, out, a/loop; neither junction is entered.
    assert!(
        lines
            .iter()
            .any(|(l, m)| *l == Level::Debug && m == "listing: Vst3User: 3 entries examined"),
        "{lines:?}"
    );
    assert!(warns(&lines).is_empty(), "{lines:?}");
}

// ---- SC-030: the listing caps ---------------------------------------------------------------------------------------

#[test]
fn listing_caps_honoured() {
    install_capture();
    let (_td, roots) = fresh_temp_roots();
    let user = root(&roots, RootKind::Vst3User);
    let global = root(&roots, RootKind::Vst3Global);
    // vst3-user in name order: 0a 0b 0c 0d Alpha.vst3 | Bravo.vst3 (the sixth entry, past the cap).
    for name in ["0a", "0b", "0c", "0d"] {
        write(&user.join(name), b"x");
    }
    bundle(&user.join("Alpha.vst3"), &release("1.0.0"));
    bundle(&user.join("Bravo.vst3"), &release("1.0.0"));
    // vst3-global: x (depth 1), x/Bravo.vst3 (depth 2, reported), x/y (depth 2, not entered), x/y/Alpha.vst3
    // (depth 3, not reported).
    bundle(&global.join("x").join("Bravo.vst3"), &release("1.0.0"));
    bundle(
        &global.join("x").join("y").join("Alpha.vst3"),
        &release("1.0.0"),
    );
    let limits = ListingLimits {
        max_depth: 2,
        max_entries: 5,
    };
    let _ = take_mine();
    let listing = Listing::scan(roots.plugin_roots(), &["Alpha.vst3", "Bravo.vst3"], limits);
    let lines = detect_lines();
    let alpha = user.join("Alpha.vst3");
    let bravo = global.join("x").join("Bravo.vst3");
    assert_eq!(listing.paths(), [alpha.as_path(), bravo.as_path()]);
    let warned = warns(&lines);
    assert_eq!(
        warned,
        ["E_DETECT_LISTING_CAP Vst3User: stopped at 5 entries, depth 0"]
    );
    let debugs: Vec<&str> = lines
        .iter()
        .filter(|(l, _)| *l == Level::Debug)
        .map(|(_, m)| m.as_str())
        .collect();
    assert!(
        debugs.contains(&"listing: Vst3User: 5 entries examined"),
        "{debugs:?}"
    );
    assert!(
        debugs.contains(&"listing: Vst3Global: 3 entries examined"),
        "{debugs:?}"
    );
}

#[test]
fn listing_production_limits() {
    assert_eq!(LISTING_MAX_DEPTH, 4);
    assert_eq!(LISTING_MAX_ENTRIES, 20_000);
    assert_eq!(
        ListingLimits::PRODUCTION,
        ListingLimits {
            max_depth: 4,
            max_entries: 20_000,
        }
    );
}

// ---- SC-031: installed elsewhere ------------------------------------------------------------------------------------

/// (label, copies, installed, installed version, update).
type ElsewhereCase<'a> = (
    &'a str,
    Vec<DetectedCopy>,
    InstalledView,
    Option<&'a str>,
    UpdateView,
);

#[test]
fn installed_elsewhere() {
    let feed = feed_fixture("full-v2.json");
    let v = |t: &str| known(t, VersionSource::Classes);
    let unknown = CopyVersion::Unknown(UnknownReason::Corrupt);
    let copy = |format: CopyFormat,
                place: Place,
                path: &str,
                managed: bool,
                rank: Option<u8>,
                version: CopyVersion| {
        DetectedCopy {
            format,
            place,
            path: PathBuf::from(path),
            managed,
            rank,
            version,
        }
    };
    for os in OSES {
        let (managed_place, managed_rank, other_place, other_rank) = match os {
            Os::Windows => (
                Place::WindowsVendor(RootKind::Vst3Global),
                Some(1),
                Place::Root(RootKind::Vst3User),
                Some(0),
            ),
            Os::Macos => (
                Place::Root(RootKind::Vst3Global),
                Some(1),
                Place::Root(RootKind::Vst3User),
                Some(0),
            ),
            Os::Linux => (
                Place::Root(RootKind::Vst3User),
                Some(0),
                Place::Root(RootKind::Vst3Global),
                Some(1),
            ),
        };
        let managed = |version| {
            copy(
                CopyFormat::Vst3,
                managed_place,
                "/fixture/managed/Alpha.vst3",
                true,
                managed_rank,
                version,
            )
        };
        let other = |path: &str, version| {
            copy(
                CopyFormat::Vst3,
                other_place,
                path,
                false,
                other_rank,
                version,
            )
        };
        let nested = copy(
            CopyFormat::Vst3,
            Place::Nested(RootKind::Vst3User),
            "/fixture/vst3-user/Old/Alpha.vst3",
            false,
            Some(0),
            v("1.1.0"),
        );
        let mut cases: Vec<ElsewhereCase<'_>> = vec![
            (
                "one other copy",
                vec![other("/fixture/o1/Alpha.vst3", v("1.1.0"))],
                InstalledView::Elsewhere,
                Some("1.1.0"),
                UpdateView::NotInstalled,
            ),
            (
                "first known in candidate order",
                vec![
                    other("/fixture/o1/Alpha.vst3", unknown),
                    other("/fixture/o2/Alpha.vst3", v("1.0.0")),
                ],
                InstalledView::Elsewhere,
                Some("1.0.0"),
                UpdateView::NotInstalled,
            ),
            (
                "only unknown",
                vec![other("/fixture/o1/Alpha.vst3", unknown)],
                InstalledView::Elsewhere,
                None,
                UpdateView::NotInstalled,
            ),
            (
                "a nested copy alone",
                vec![nested.clone()],
                InstalledView::Elsewhere,
                Some("1.1.0"),
                UpdateView::NotInstalled,
            ),
            (
                "no copy",
                vec![],
                InstalledView::NotInstalled,
                None,
                UpdateView::NotInstalled,
            ),
            (
                "the managed copy beside others",
                vec![
                    other("/fixture/o1/Alpha.vst3", v("1.1.0")),
                    managed(v("1.2.0")),
                    nested.clone(),
                ],
                InstalledView::Version,
                Some("1.2.0"),
                UpdateView::UpToDate,
            ),
        ];
        if os == Os::Macos {
            cases.push((
                "AU copies only",
                vec![
                    copy(
                        CopyFormat::Au,
                        Place::Root(RootKind::AuUser),
                        "/fixture/au-user/Alpha.component",
                        false,
                        None,
                        known("1.2.0", VersionSource::AuPlist),
                    ),
                    copy(
                        CopyFormat::Au,
                        Place::Root(RootKind::AuGlobal),
                        "/fixture/au-global/Alpha.component",
                        true,
                        None,
                        known("1.2.0", VersionSource::AuPlist),
                    ),
                ],
                InstalledView::NotInstalled,
                None,
                UpdateView::NotInstalled,
            ));
        }
        for (label, copies, installed, version, update) in cases {
            let detections = BTreeMap::from([(
                "Alpha".to_owned(),
                Detection::Checked {
                    copies,
                    record: None,
                    record_error: None,
                    managed_rank,
                },
            )]);
            let view = assembled(os, &feed, &detections, "Alpha");
            assert_eq!(view.installed, installed, "{os:?} {label}");
            assert_eq!(view.installed_version.as_deref(), version, "{os:?} {label}");
            assert_eq!(view.update, Some(update), "{os:?} {label}");
        }
    }
}

/// Phase 9 PD-14 (tasks TA-6): `parse_manifest` is `read_manifest` without the bounded read.
#[test]
fn parse_manifest_cases() {
    let (_td, roots) = fresh_temp_roots();
    let state = roots.state_dir();
    let manifest = state.join(MANIFESTS_DIR).join("Alpha.json");
    let phase9 = br#"{"manifest_version": 1, "plugin": "Alpha", "version": "1.2.0", "vst3_bundle": "Alpha.vst3", "archive_sha256": "", "plugin_root": "/fixture/plugins", "preset_root": "/fixture/presets", "files": []}"#;
    let cases: [(&str, &[u8], RecordRead<InstallerRecord>); 5] = [
        (
            "a Phase 9 manifest with extra keys",
            phase9,
            RecordRead::Found(InstallerRecord::Manifest {
                version_text: "1.2.0".to_owned(),
                version: CoreVersion::parse("1.2.0"),
            }),
        ),
        (
            "manifest_version 2",
            br#"{"manifest_version": 2, "plugin": "Alpha", "version": "1.2.0"}"#,
            RecordRead::Failed(E_RECORD_FUTURE),
        ),
        (
            "another plugin",
            br#"{"manifest_version": 1, "plugin": "Beta", "version": "1.2.0"}"#,
            RecordRead::Failed(E_RECORD_MISMATCH),
        ),
        ("not JSON", b"not json", RecordRead::Failed(E_RECORD_PARSE)),
        (
            "no version",
            br#"{"manifest_version": 1, "plugin": "Alpha"}"#,
            RecordRead::Failed(E_RECORD_PARSE),
        ),
    ];
    for (label, bytes, want) in cases {
        write(&manifest, bytes);
        let read = read_manifest(state, "Alpha", "Alpha");
        assert_eq!(parse_manifest(bytes, "Alpha"), read, "{label}");
        assert_eq!(read, want, "{label}");
    }
}
