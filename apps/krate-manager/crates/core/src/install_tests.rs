//! install::tests (T003): every Phase 9 SC row (plan §7.1). Every test builds its roots with `fresh_temp_roots()` (or,
//! for SC-023, `temp_roots_with_user_presets`) and writes only inside that `TempDir`; the only network peer is the
//! loopback `FixtureServer`, reached for assets through `request_seam` (PD-2, PD-30); every refusal row runs over
//! `RecordingInstallIo::fail_closed()` and asserts zero mutating calls (FR-115). Archives are built here at run time
//! with a payload from the listing this file writes for them (FR-114, plan §4.4).

use super::*;
use crate::download::{DOWNLOADS_DIR, DownloadRequest};
use crate::extract::{EXTRACT_DIR, FsExtractIo};
use crate::feed::PlatformKey;
use crate::fixture_server::{FixtureServer, Reply};
use crate::http::HttpConfig;
use crate::roots::Roots;
use crate::settings::Settings;
use crate::status::{InstalledView, RecordKind, UpdateView};
use crate::tar_builder_tests::{Tar, gz, literal_payload, sha};
use crate::test_signer::TestSigner;
use crate::test_support::{
    CountingDriver, FakeHostFacts, FaultyInstallIo, FaultyStagingIo, IKind, IOp, InstallRig,
    MemExtractIo, MemInstallIo, MemNode, OneDriver, Op, RecordingDriver, RecordingInstallIo,
    fresh_temp_roots, install_capture, native_key, snapshot, take_mine, test_feed_document,
};
use crate::verify::{Expected, hex_lower, verify_file};
use crate::version::OsVersion;
use log::Level;
use serde_json::{Value, json};
use std::collections::BTreeMap;
use std::fs;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc;
use std::time::Duration;
use tempfile::TempDir;

const NOW: u64 = 1_793_404_800;
const FEED_ROUTE: &str = "/versions.json";
const SEC: Duration = Duration::from_secs(1);
const V12: &str = "1.2.0";
const V13: &str = "1.3.0";
const ALPHA: &str = "Alpha";
const BETA: &str = "Beta";

// ---- archives (plan §4.4) ------------------------------------------------------------------------------------------

/// One archive entry: a directory, a file (mode, bytes) or a link (target).
#[derive(Clone, Debug)]
enum Ent {
    Dir(String),
    File(String, u32, Vec<u8>),
    Link(String, String),
}

impl Ent {
    fn path(&self) -> &str {
        match self {
            Ent::Dir(p) | Ent::File(p, ..) | Ent::Link(p, _) => p,
        }
    }

    /// The C4 line (`<t> <mode> <path>[ -> <target>]`).
    fn line(&self) -> String {
        match self {
            Ent::Dir(p) => format!("d 0755 {p}"),
            Ent::File(p, mode, _) if mode & 0o111 != 0 => format!("f 0755 {p}"),
            Ent::File(p, ..) => format!("f 0644 {p}"),
            Ent::Link(p, t) => format!("l 0777 {p} -> {t}"),
        }
    }
}

fn preset(path: &str, bytes: &[u8]) -> (String, Vec<u8>) {
    (path.to_owned(), bytes.to_vec())
}

/// 1.2.0's factory presets.
fn presets_v12() -> Vec<(String, Vec<u8>)> {
    vec![
        preset("Init.vstpreset", b"init 1.2.0"),
        preset("Old.vstpreset", b"an old preset"),
        preset("Warm Pads/Night Air.vstpreset", b"night air"),
    ]
}

/// 1.3.0's: one added (`New`), one removed (`Old`), one changed (`Init`), one the same (`Night Air`).
fn presets_v13() -> Vec<(String, Vec<u8>)> {
    vec![
        preset("Init.vstpreset", b"init 1.3.0, changed"),
        preset("New.vstpreset", b"a new preset"),
        preset("Warm Pads/Night Air.vstpreset", b"night air"),
    ]
}

/// `{"Name", "Version", "Classes": [{"Name", "Version"}]}` for any plugin (the moved `moduleinfo` names Alpha only).
fn moduleinfo_for(name: &str, version: &str) -> Vec<u8> {
    format!(
        r#"{{"Name": "{name}", "Version": "{version}", "Classes": [{{"Name": "{name}", "Version": "{version}"}}]}}"#
    )
    .into_bytes()
}

/// One versioned release of a plugin: what its archive holds (plan §4.4).
#[derive(Clone, Debug)]
struct Release {
    name: String,
    version: String,
    presets: Vec<(String, Vec<u8>)>,
    subdir: Option<String>,
    exec: bool,
    link: bool,
    moduleinfo: bool,
    extra: Vec<Ent>,
}

impl Release {
    fn of(name: &str, version: &str) -> Release {
        Release {
            name: name.to_owned(),
            version: version.to_owned(),
            presets: if version == V12 {
                presets_v12()
            } else {
                presets_v13()
            },
            subdir: None,
            exec: false,
            link: false,
            moduleinfo: true,
            extra: Vec::new(),
        }
    }

    fn kits(mut self) -> Release {
        self.subdir = Some("Kits".to_owned());
        self
    }

    /// The exec bit on the `.so` and the in-bundle link (the Unix and Mem rows).
    fn unix(mut self) -> Release {
        self.exec = true;
        self.link = true;
        self
    }

    fn with_presets(mut self, presets: Vec<(String, Vec<u8>)>) -> Release {
        self.presets = presets;
        self
    }

    fn bundle(&self) -> String {
        format!("{}.vst3", self.name)
    }

    fn asset(&self) -> String {
        format!("{}-{}-Linux-x64.tar.gz", self.name, self.version)
    }

    fn route(&self) -> String {
        format!(
            "/{}/v{}/{}",
            self.name.to_ascii_lowercase(),
            self.version,
            self.asset()
        )
    }

    /// `<Name>[/<subdir>]`, relative to the user preset root.
    fn preset_rel(&self) -> String {
        match &self.subdir {
            Some(s) => format!("{}/{s}", self.name),
            None => self.name.clone(),
        }
    }

    /// Every entry, in archive order (directories before what they hold).
    fn entries(&self) -> Vec<Ent> {
        let b = self.bundle();
        let so = format!("{b}/Contents/x86_64-linux/{}.so", self.name);
        let mut out = vec![
            Ent::Dir(b.clone()),
            Ent::Dir(format!("{b}/Contents")),
            Ent::Dir(format!("{b}/Contents/Resources")),
        ];
        if self.moduleinfo {
            out.push(Ent::File(
                format!("{b}/Contents/Resources/moduleinfo.json"),
                0o644,
                moduleinfo_for(&self.name, &self.version),
            ));
        }
        out.push(Ent::Dir(format!("{b}/Contents/x86_64-linux")));
        out.push(Ent::File(
            so,
            if self.exec { 0o755 } else { 0o644 },
            format!("{} {} shared object", self.name, self.version).into_bytes(),
        ));
        if self.link {
            out.push(Ent::Link(
                format!(
                    "{b}/Contents/x86_64-linux/lib{}.so",
                    self.name.to_ascii_lowercase()
                ),
                format!("{}.so", self.name),
            ));
        }
        out.push(Ent::File(
            "CHANGELOG.txt".to_owned(),
            0o644,
            b"changes".to_vec(),
        ));
        out.push(Ent::File(
            "README.txt".to_owned(),
            0o644,
            b"read me".to_vec(),
        ));
        out.push(Ent::Dir("presets".to_owned()));
        let mut dirs: Vec<String> = Vec::new();
        for (p, _) in &self.presets {
            let segments: Vec<&str> = p.split('/').collect();
            for n in 1..segments.len() {
                let dir = format!("presets/{}", segments[..n].join("/"));
                if !dirs.contains(&dir) {
                    dirs.push(dir);
                }
            }
        }
        out.extend(dirs.into_iter().map(Ent::Dir));
        for (p, bytes) in &self.presets {
            out.push(Ent::File(format!("presets/{p}"), 0o644, bytes.clone()));
        }
        out.extend(self.extra.iter().cloned());
        out
    }

    /// The C4 listing this test writes for the archive, sorted by path (the payload's source, FR-114).
    fn lines(&self) -> Vec<String> {
        let mut entries = self.entries();
        entries.sort_by(|a, b| a.path().cmp(b.path()));
        entries.iter().map(Ent::line).collect()
    }

    /// The gzip'd ustar archive.
    fn archive(&self) -> Vec<u8> {
        let mut t = Tar::new();
        for e in self.entries() {
            match e {
                // A directory over the ustar name field is left implied (rule 3 adds it, mode 0755).
                Ent::Dir(p) if p.len() >= 100 => &mut t,
                Ent::Dir(p) => t.dir(&p),
                Ent::File(p, mode, bytes) if p.len() > 100 => t.long_file_mode(&p, mode, &bytes),
                Ent::File(p, mode, bytes) => t.file(&p, mode, &bytes),
                Ent::Link(p, target) => t.link(&p, &target),
            };
        }
        gz(&t.finish())
    }

    /// The installed bundle as `tree_of` reads it, relative to the target.
    fn bundle_tree(&self) -> Vec<(String, String)> {
        let prefix = format!("{}/", self.bundle());
        let mut out: Vec<(String, String)> = self
            .entries()
            .iter()
            .filter_map(|e| {
                let rel = e.path().strip_prefix(&prefix)?.to_owned();
                Some((rel, describe(e)))
            })
            .collect();
        out.sort();
        out
    }

    /// The factory preset files, relative to the user preset root.
    fn preset_files(&self) -> Vec<(String, Vec<u8>)> {
        self.presets
            .iter()
            .map(|(p, b)| (format!("{}/{p}", self.preset_rel()), b.clone()))
            .collect()
    }

    /// `snapshot` rows of the bundle under the plug-in root (directories as length 0).
    fn plugin_rows(&self) -> Vec<(String, char, u64)> {
        let bundle = self.bundle();
        let mut out: Vec<(String, char, u64)> = self
            .entries()
            .iter()
            .filter(|e| e.path() == bundle || e.path().starts_with(&format!("{bundle}/")))
            .map(row)
            .collect();
        out.sort();
        out
    }

    /// `snapshot` rows of the factory presets under the user preset root, chain directories included.
    fn preset_rows(&self) -> Vec<(String, char, u64)> {
        let base = self.preset_rel();
        let mut out = chain_rows(&base);
        for e in self.entries() {
            let Some(rest) = e.path().strip_prefix("presets/") else {
                continue;
            };
            let moved = match &e {
                Ent::Dir(_) => Ent::Dir(format!("{base}/{rest}")),
                Ent::File(_, m, b) => Ent::File(format!("{base}/{rest}"), *m, b.clone()),
                Ent::Link(_, t) => Ent::Link(format!("{base}/{rest}"), t.clone()),
            };
            out.push(row(&moved));
        }
        out.sort();
        out.dedup();
        out
    }
}

/// Tar records for names over 100 bytes, with a mode.
trait LongMode {
    fn long_file_mode(&mut self, name: &str, mode: u32, data: &[u8]) -> &mut Tar;
}

impl LongMode for Tar {
    fn long_file_mode(&mut self, name: &str, mode: u32, data: &[u8]) -> &mut Tar {
        let mut long = name.as_bytes().to_vec();
        long.push(0);
        self.record(b"././@LongLink", b'L', 0o644, &long, b"");
        self.record(name.as_bytes(), b'0', mode, data, b"")
    }
}

/// `tree_of`'s description of an entry.
fn describe(e: &Ent) -> String {
    match e {
        Ent::Dir(_) => "d".to_owned(),
        Ent::File(_, _, b) => format!("f {}", hex_lower(&sha(b))),
        Ent::Link(_, t) => format!("l {t}"),
    }
}

/// A `snapshot` row of an entry (directories as length 0).
fn row(e: &Ent) -> (String, char, u64) {
    match e {
        Ent::Dir(p) => (p.clone(), 'd', 0),
        Ent::File(p, _, b) => (p.clone(), 'f', b.len() as u64),
        Ent::Link(p, t) => (p.clone(), 'l', t.len() as u64),
    }
}

/// The directory rows of `a`, `a/b`, … for `a/b/…`.
fn chain_rows(rel: &str) -> Vec<(String, char, u64)> {
    let segments: Vec<&str> = rel.split('/').collect();
    (1..=segments.len())
        .map(|n| (segments[..n].join("/"), 'd', 0))
        .collect()
}

/// `bundle_archive` of plan §4.4: the archive and the listing it was built from.
fn bundle_archive(r: &Release) -> (Vec<u8>, Vec<String>) {
    (r.archive(), r.lines())
}

// ---- reading trees -------------------------------------------------------------------------------------------------

/// Every node under `root` through `io`, never following a link: (relative path, `d` | `f <sha256>` | `l <target>`),
/// sorted; empty when `root` is absent.
fn tree_of(io: &dyn InstallIo, root: &Path) -> Vec<(String, String)> {
    fn walk(io: &dyn InstallIo, dir: &Path, prefix: &str, out: &mut Vec<(String, String)>) {
        for (name, kind) in io.read_dir(dir).expect("read_dir") {
            let name = name.to_string_lossy().into_owned();
            let rel = if prefix.is_empty() {
                name.clone()
            } else {
                format!("{prefix}/{name}")
            };
            let path = dir.join(&name);
            match kind {
                NodeKind::Dir => {
                    out.push((rel.clone(), "d".to_owned()));
                    walk(io, &path, &rel, out);
                }
                NodeKind::File => {
                    let h = io.hash_file(&path).expect("hash_file");
                    out.push((rel, format!("f {}", hex_lower(&h))));
                }
                NodeKind::Symlink => {
                    let t = io.read_link(&path).expect("read_link");
                    out.push((rel, format!("l {}", t.to_string_lossy())));
                }
                NodeKind::Other => out.push((rel, "o".to_owned())),
            }
        }
    }
    let mut out = Vec::new();
    if io.kind_of(root).expect("kind_of").is_some() {
        walk(io, root, "", &mut out);
    }
    out.sort();
    out
}

/// `snapshot` with directory lengths zeroed (filesystem-dependent).
fn snap(root: &Path) -> Vec<(String, char, u64)> {
    snapshot(root)
        .into_iter()
        .map(|(p, k, len)| (p, k, if k == 'd' { 0 } else { len }))
        .collect()
}

/// TA-13: the permission bits under `root` (`symlink_metadata`, never following a link), sorted. Links are left out:
/// a link has no mode of its own to set (FR-015), and its lstat bits differ by OS.
#[cfg(unix)]
fn modes(root: &Path) -> Vec<(String, u32)> {
    use std::os::unix::fs::PermissionsExt as _;
    let mut out: Vec<(String, u32)> = snapshot(root)
        .into_iter()
        .filter(|(_, kind, _)| *kind != 'l')
        .map(|(p, _, _)| {
            let meta = fs::symlink_metadata(root.join(&p)).expect("symlink_metadata");
            (p, meta.permissions().mode() & 0o777)
        })
        .collect();
    out.sort();
    out
}

/// Every path under `root` (through `io`) whose name is a crash leftover (FR-075).
fn leftovers(io: &dyn InstallIo, root: &Path) -> Vec<String> {
    tree_of(io, root)
        .into_iter()
        .map(|(p, _)| p)
        .filter(|p| {
            let name = p.rsplit('/').next().unwrap_or_default();
            name.ends_with(NEW_SUFFIX)
                || name.ends_with(OLD_SUFFIX)
                || (name.starts_with('.') && name.ends_with(".json.tmp"))
        })
        .collect()
}

fn absent(p: &Path) -> bool {
    matches!(fs::symlink_metadata(p), Err(e) if e.kind() == io::ErrorKind::NotFound)
}

fn host() -> FakeHostFacts {
    FakeHostFacts {
        os_version: OsVersion::parse("2.99"),
        ..FakeHostFacts::default()
    }
}

fn code_of<T: std::fmt::Debug>(r: &Result<T, InstallError>) -> &'static str {
    match r {
        Ok(v) => panic!("expected an error, got {v:?}"),
        Err(e) => e.code(),
    }
}

fn presets_kept(paths: &[&str]) -> Vec<KeptPath> {
    paths
        .iter()
        .map(|p| KeptPath {
            root: RootTag::Presets,
            path: (*p).to_owned(),
        })
        .collect()
}

// ---- the rig (plan §7) ---------------------------------------------------------------------------------------------

type Seam<'a> = dyn Fn(DownloadRequest) -> DownloadRequest + 'a;

/// Temp roots, the fixture server (feed + assets), a throwaway signer and an `InstallRig` over them.
struct Fx {
    server: FixtureServer,
    rig: InstallRig,
    signer: TestSigner,
    base: PathBuf,
    next: AtomicUsize,
    _td: TempDir,
}

impl Fx {
    fn new() -> Fx {
        let (td, roots) = fresh_temp_roots();
        Fx::over(td, roots, None)
    }

    /// The Linux plug-in root configured at `<base>/<rel>` (folded into the roots, as `context` does).
    fn configured(rel: &str) -> Fx {
        let (td, roots) = fresh_temp_roots();
        Fx::over(td, roots, Some(rel))
    }

    fn over(td: TempDir, roots: Roots, configured: Option<&str>) -> Fx {
        let base = roots
            .state_dir()
            .parent()
            .expect("the roots' base")
            .to_path_buf();
        let server = FixtureServer::start(vec![(FEED_ROUTE.to_owned(), Reply::Status(404))]);
        let signer = TestSigner::new();
        let (roots, settings) = match configured {
            Some(rel) => {
                let mut path = base.clone();
                for segment in rel.split('/') {
                    path.push(segment);
                }
                (
                    roots
                        .with_configured_plugin_root(Some(&path))
                        .expect("a valid configured root"),
                    Settings {
                        linux_plugin_root: Some(path),
                        ..Settings::default()
                    },
                )
            }
            None => (roots, Settings::default()),
        };
        let ctx = AppContext {
            roots,
            settings,
            feed_url: server.url(FEED_ROUTE),
            http: HttpConfig::loopback(SEC, SEC),
            signing_key: signer.key(),
        };
        Fx {
            server,
            rig: InstallRig::new(ctx),
            signer,
            base,
            next: AtomicUsize::new(0),
            _td: td,
        }
    }

    fn ctx(&self) -> &AppContext {
        &self.rig.ctx
    }

    fn plugin_root(&self) -> PathBuf {
        self.ctx()
            .settings
            .linux_plugin_root_or_default(&self.ctx().roots)
            .to_path_buf()
    }

    fn user(&self) -> PathBuf {
        self.ctx().roots.presets().user.clone()
    }

    fn state(&self) -> PathBuf {
        self.ctx().roots.state_dir().to_path_buf()
    }

    fn target(&self, name: &str) -> PathBuf {
        self.plugin_root().join(format!("{name}.vst3"))
    }

    fn journal(&self, stem: &str) -> PathBuf {
        self.state().join(JOURNAL_DIR).join(format!("{stem}.json"))
    }

    fn manifest_path(&self, stem: &str) -> PathBuf {
        self.state()
            .join(detect::MANIFESTS_DIR)
            .join(format!("{stem}.json"))
    }

    fn extract(&self) -> PathBuf {
        self.ctx().roots.staging_dir().join(EXTRACT_DIR)
    }

    /// The (feed, asset) requests the server has seen so far.
    fn counts(&self) -> (usize, usize) {
        let paths: Vec<String> = self.server.requests().into_iter().map(|r| r.path).collect();
        let feed = paths.iter().filter(|p| *p == FEED_ROUTE).count();
        (feed, paths.len() - feed)
    }

    /// PD-2: each request fetched from the fixture server at its feed URL's path.
    fn seam(&self) -> impl Fn(DownloadRequest) -> DownloadRequest + '_ {
        move |request: DownloadRequest| {
            let path = url::Url::parse(request.asset().url.as_str())
                .expect("an asset URL")
                .path()
                .to_owned();
            request
                .with_fixture_url(&self.server.url(&path))
                .expect("a fixture URL")
        }
    }

    /// The rig's seam-free `CommandEnv` with the seam set here (PD-30).
    fn env<'a>(
        &'a self,
        os: Os,
        arch: Arch,
        io: &'a dyn InstallIo,
        drivers: &'a dyn DriverResolver,
        seam: &'a Seam<'a>,
    ) -> CommandEnv<'a> {
        let mut env = self.rig.command_env(os, arch, io, &FsExtractIo, drivers);
        env.request_seam = Some(seam);
        env
    }

    /// `for_command` for `(Os::Linux, Arch::X86_64)` over `io` and the native resolver.
    fn act(
        &self,
        io: &dyn InstallIo,
        name: &str,
        version: &str,
        action: &str,
    ) -> Result<ActionSummary, InstallError> {
        let seam = self.seam();
        let env = self.env(Os::Linux, Arch::X86_64, io, &NativeDrivers, &seam);
        for_command(env, name, version, action, NOW, &host(), &mut |_| {})
    }

    fn install(&self, name: &str, version: &str) -> ActionSummary {
        self.act(&FsInstallIo, name, version, "install")
            .unwrap_or_else(|e| panic!("install {name} {version}: {e}"))
    }

    fn update(&self, name: &str, version: &str) -> ActionSummary {
        self.act(&FsInstallIo, name, version, "update")
            .unwrap_or_else(|e| panic!("update {name} {version}: {e}"))
    }

    /// `remove_for_command` for `Os::Linux` over `io`.
    fn remove(&self, io: &dyn InstallIo, name: &str) -> Result<ActionSummary, InstallError> {
        let seam = self.seam();
        let env = self.env(Os::Linux, Arch::X86_64, io, &NativeDrivers, &seam);
        remove_for_command(env, name, NOW, &mut |_| {})
    }

    /// A contract-valid feed of `releases` (one `linux-x86_64` asset each, SI-1), each signed as given, then `patch`.
    fn doc_signed(
        &self,
        releases: &[(&Release, String)],
        patch: impl FnOnce(&mut Value),
    ) -> Vec<u8> {
        let built: Vec<(Vec<u8>, Vec<String>, String)> = releases
            .iter()
            .map(|(r, sig)| {
                let (bytes, lines) = bundle_archive(r);
                (bytes, lines, sig.clone())
            })
            .collect();
        let assets: Vec<String> = releases.iter().map(|(r, _)| r.asset()).collect();
        let plugins: Vec<crate::test_support::TestPlugin<'_>> = releases
            .iter()
            .zip(&built)
            .zip(&assets)
            .map(
                |(((r, _), (bytes, _, sig)), asset)| crate::test_support::TestPlugin {
                    name: &r.name,
                    version: &r.version,
                    key: PlatformKey::LinuxX86_64,
                    asset,
                    size: bytes.len() as u64,
                    sha256: sha(bytes),
                    signature: sig.clone(),
                    min_os: None,
                },
            )
            .collect();
        let mut doc: Value = serde_json::from_slice(&test_feed_document(&plugins)).expect("JSON");
        for ((r, _), (_, lines, _)) in releases.iter().zip(&built) {
            let entry = &mut doc["plugins"][r.name.as_str()];
            let payload = literal_payload(lines);
            entry["platforms"]["linux-x86_64"]["payload"] =
                json!({ "files": payload.files, "sha256": hex_lower(&payload.sha256) });
            if let Some(sub) = &r.subdir {
                entry["install"]["preset_subdir"] = json!(sub);
            }
        }
        patch(&mut doc);
        serde_json::to_vec(&doc).expect("serialises")
    }

    fn sign(&self, r: &Release) -> String {
        self.signer.sign_c3(
            &r.archive(),
            &r.name,
            &r.version,
            PlatformKey::LinuxX86_64,
            &r.asset(),
        )
    }

    fn doc(&self, releases: &[&Release]) -> Vec<u8> {
        let signed: Vec<(&Release, String)> = releases.iter().map(|r| (*r, self.sign(r))).collect();
        self.doc_signed(&signed, |_| {})
    }

    /// Serve `doc` as the feed and each release's archive at its route.
    fn serve(&self, doc: Vec<u8>, releases: &[&Release]) {
        self.server.set_route(
            FEED_ROUTE,
            Reply::Ok {
                body: doc,
                content_type: "application/json",
            },
        );
        for r in releases {
            self.server
                .set_route(&r.route(), Reply::Ranged { body: r.archive() });
        }
    }

    fn offer(&self, releases: &[&Release]) {
        self.serve(self.doc(releases), releases);
    }

    /// The parsed feed entry of `name` in `doc`.
    fn entry_in(doc: &[u8], name: &str) -> PluginEntry {
        feed::parse(doc)
            .expect("a valid feed")
            .plugins
            .get(name)
            .expect("the entry")
            .clone()
    }

    /// A real `VerifiedArtifact` of `r` (FR-113): written into the `TempDir`, signed in memory, `verify_file`d.
    fn artifact(&self, r: &Release) -> (PluginEntry, VerifiedArtifact) {
        let signature = self.sign(r);
        let doc = self.doc_signed(&[(r, signature.clone())], |_| {});
        let entry = Fx::entry_in(&doc, &r.name);
        let artifact = self.verified(
            &entry,
            PlatformKey::LinuxX86_64,
            &r.asset(),
            &r.archive(),
            &signature,
        );
        (entry, artifact)
    }

    fn verified(
        &self,
        entry: &PluginEntry,
        key: PlatformKey,
        asset: &str,
        bytes: &[u8],
        signature: &str,
    ) -> VerifiedArtifact {
        let dir = self
            .base
            .join(format!("art{}", self.next.fetch_add(1, Ordering::SeqCst)));
        fs::create_dir_all(&dir).expect("the artifact dir");
        let path = dir.join(asset);
        fs::write(&path, bytes).expect("write the artifact");
        let platform = entry
            .release
            .as_ref()
            .and_then(|r| r.platforms.get(&key))
            .expect("the asset")
            .clone();
        let expected = Expected {
            name: &entry.name,
            version: &entry.version_text,
            platform: key,
            asset: &platform.asset_name,
            format: platform.format,
            size: platform.size,
            sha256: platform.sha256,
            signature,
        };
        verify_file(&path, &expected, &self.signer.key()).expect("the artifact verifies")
    }

    /// One `LinuxDriver::run` over `io` / `xio`.
    fn drive(
        &self,
        io: &dyn InstallIo,
        xio: &dyn ExtractIoRef,
        batch: Vec<Action>,
    ) -> Vec<ActionResult> {
        LinuxDriver.run(
            InstallEnv {
                ctx: self.ctx(),
                io,
                extract: self.rig.extract_env(xio.as_extract()),
                progress: &mut |_| {},
            },
            batch,
        )
    }

    /// One action through the driver; its one outcome.
    fn drive_one(
        &self,
        io: &dyn InstallIo,
        xio: &dyn ExtractIoRef,
        action: Action,
    ) -> Result<Outcome, InstallError> {
        let mut results = self.drive(io, xio, vec![action]);
        assert_eq!(results.len(), 1, "one ActionResult");
        results.remove(0).outcome
    }

    fn recover_over(&self, io: &dyn InstallIo, xio: &dyn ExtractIoRef) -> RecoveryReport {
        recover(self.ctx(), io, self.rig.extract_env(xio.as_extract()))
    }

    fn manifest(&self, io: &dyn InstallIo, name: &str) -> Option<Manifest> {
        read_manifest_full(io, &self.state(), name, name).expect("a readable manifest")
    }

    /// Plants `files` (relative to the user preset root) on disk.
    fn plant(&self, files: &[(String, Vec<u8>)]) {
        for (rel, bytes) in files {
            let path = dest_path(&self.user(), rel).expect("a plain path");
            fs::create_dir_all(path.parent().expect("a parent")).expect("a parent dir");
            fs::write(&path, bytes).expect("plant");
        }
    }
}

/// The extract I/O a driver run uses: the real one or a Mem one.
trait ExtractIoRef {
    fn as_extract(&self) -> &dyn crate::extract::ExtractIo;
}

impl ExtractIoRef for FsExtractIo {
    fn as_extract(&self) -> &dyn crate::extract::ExtractIo {
        self
    }
}

impl ExtractIoRef for MemExtractIo {
    fn as_extract(&self) -> &dyn crate::extract::ExtractIo {
        self
    }
}

fn install_of(entry: &PluginEntry, artifact: &VerifiedArtifact) -> Action {
    Action::Install {
        entry: entry.clone(),
        artifact: artifact.clone(),
    }
}

fn update_of(entry: &PluginEntry, artifact: &VerifiedArtifact) -> Action {
    Action::Update {
        entry: entry.clone(),
        artifact: artifact.clone(),
    }
}

fn uninstall_of(entry: &PluginEntry) -> Action {
    Action::Uninstall {
        entry: entry.clone(),
    }
}

/// Asserts the target holds exactly `side`'s bundle (by content) and the preset folder exactly its factory set,
/// `other`'s factory-only files absent; `None` asserts both absent.
fn assert_side(io: &dyn InstallIo, fx: &Fx, side: Option<&Release>, other: &Release, label: &str) {
    let target = fx.target(&other.name);
    match side {
        Some(r) => {
            assert_eq!(tree_of(io, &target), r.bundle_tree(), "{label}: the bundle");
            for (rel, bytes) in r.preset_files() {
                let path = dest_path(&fx.user(), &rel).expect("plain");
                assert_eq!(
                    io.hash_file(&path).ok(),
                    Some(sha(&bytes)),
                    "{label}: preset {rel}"
                );
            }
            for (rel, _) in other.preset_files() {
                if !r.preset_files().iter().any(|(p, _)| *p == rel) {
                    let path = dest_path(&fx.user(), &rel).expect("plain");
                    assert_eq!(io.kind_of(&path).expect("kind_of"), None, "{label}: {rel}");
                }
            }
        }
        None => {
            assert_eq!(
                io.kind_of(&target).expect("kind_of"),
                None,
                "{label}: no bundle"
            );
            for (rel, _) in other.preset_files() {
                let path = dest_path(&fx.user(), &rel).expect("plain");
                assert_eq!(io.kind_of(&path).expect("kind_of"), None, "{label}: {rel}");
            }
        }
    }
}

// ---- crash cuts (plan §4.5) ----------------------------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
enum Cut {
    JournalBegun,
    FirstBundleFile,
    BundleStaged,
    FirstPresetFile,
    PresetsStaged,
    JournalStaged,
    OldAside,
    NewIn,
    PresetsIn,
    ManifestWritten,
    StaleRemoved,
    JournalDone,
    OldDeleted,
    JournalRemoved,
}

const CUTS: [Cut; 14] = [
    Cut::JournalBegun,
    Cut::FirstBundleFile,
    Cut::BundleStaged,
    Cut::FirstPresetFile,
    Cut::PresetsStaged,
    Cut::JournalStaged,
    Cut::OldAside,
    Cut::NewIn,
    Cut::PresetsIn,
    Cut::ManifestWritten,
    Cut::StaleRemoved,
    Cut::JournalDone,
    Cut::OldDeleted,
    Cut::JournalRemoved,
];

/// The FR-029 step a cut's calls belong to.
fn cut_step(cut: Cut) -> Step {
    match cut {
        Cut::JournalBegun => Step::JournalBegun,
        Cut::FirstBundleFile | Cut::BundleStaged => Step::BundleStaged,
        Cut::FirstPresetFile | Cut::PresetsStaged => Step::PresetsStaged,
        Cut::JournalStaged => Step::JournalStaged,
        Cut::OldAside => Step::OldAside,
        Cut::NewIn => Step::NewIn,
        Cut::PresetsIn => Step::PresetsIn,
        Cut::ManifestWritten => Step::ManifestWritten,
        Cut::StaleRemoved => Step::StaleRemoved,
        Cut::JournalDone => Step::JournalDone,
        Cut::OldDeleted => Step::OldDeleted,
        Cut::JournalRemoved => Step::JournalRemoved,
    }
}

/// The primary path of a mutating call.
fn op_path(op: &IOp) -> Option<&Path> {
    Some(match op {
        IOp::CreateDirAll(p)
        | IOp::CreateDir(p)
        | IOp::WriteNew(p)
        | IOp::SetMode(p, _)
        | IOp::SyncFile(p)
        | IOp::SyncDir(p)
        | IOp::RemoveFile(p)
        | IOp::RemoveDir(p)
        | IOp::RemoveTree(p) => p,
        IOp::CopyNew { dest, .. } => dest,
        IOp::Symlink { link, .. } => link,
        IOp::Rename { from, .. } => from,
        _ => return None,
    })
}

/// Plan §4.5: each cut's index among the mutating calls of a fault-free recording (the count `crash_after` lets
/// through); a step the recording lacks is the previous cut.
fn boundaries(calls: &[IOp], ctx: &AppContext, stem: &str) -> BTreeMap<Cut, usize> {
    let muts: Vec<&IOp> = calls.iter().filter(|c| c.is_mutating()).collect();
    let state = ctx.roots.state_dir();
    let journal_dir = state.join(JOURNAL_DIR);
    let journal = journal_dir.join(format!("{stem}.json"));
    let manifests = state.join(detect::MANIFESTS_DIR);
    let manifest = manifests.join(format!("{stem}.json"));
    let plugin_root = ctx
        .settings
        .linux_plugin_root_or_default(&ctx.roots)
        .to_path_buf();
    let target = plugin_root.join(format!("{stem}.vst3"));
    let new = plugin_root.join(format!("{stem}.vst3{NEW_SUFFIX}"));
    let old = plugin_root.join(format!("{stem}.vst3{OLD_SUFFIX}"));
    let find = |pred: &dyn Fn(&IOp) -> bool| muts.iter().position(|c| pred(c));
    let all = |pred: &dyn Fn(&IOp) -> bool| -> Vec<usize> {
        muts.iter()
            .enumerate()
            .filter(|(_, c)| pred(c))
            .map(|(i, _)| i)
            .collect()
    };
    // 0-based index `i` of a rename, plus its directory sync when the next call is one.
    let with_sync = |i: usize, dir: &Path| {
        if muts.get(i + 1) == Some(&&IOp::SyncDir(dir.to_path_buf())) {
            i + 2
        } else {
            i + 1
        }
    };
    let journal_renames = all(&|c| matches!(c, IOp::Rename { to, .. } if *to == journal));
    let journal_writes =
        all(&|c| matches!(c, IOp::WriteNew(p) if p.parent() == Some(journal_dir.as_path())));
    let mut out = BTreeMap::new();
    out.insert(
        Cut::JournalBegun,
        with_sync(journal_renames[0], &journal_dir),
    );
    let first_bundle = find(&|c| matches!(c, IOp::CopyNew { dest, .. } if dest.starts_with(&new)))
        .expect("a bundle file");
    out.insert(Cut::FirstBundleFile, first_bundle + 1);
    let staged_write = journal_writes[1];
    let last_bundle = muts[..staged_write]
        .iter()
        .rposition(|c| op_path(c).is_some_and(|p| p.starts_with(&new)))
        .expect("a bundle call");
    out.insert(Cut::BundleStaged, last_bundle + 1);
    let first_preset = find(&|c| {
        matches!(c, IOp::CopyNew { dest, .. } if !dest.starts_with(&new)
            && dest.to_string_lossy().ends_with(NEW_SUFFIX))
    })
    .expect("a preset sibling");
    out.insert(Cut::FirstPresetFile, first_preset + 1);
    out.insert(Cut::PresetsStaged, staged_write);
    out.insert(
        Cut::JournalStaged,
        with_sync(journal_renames[1], &journal_dir),
    );
    let aside = find(&|c| matches!(c, IOp::Rename { from, to } if *from == target && *to == old));
    out.insert(
        Cut::OldAside,
        aside.map_or(out[&Cut::JournalStaged], |i| i + 1),
    );
    let new_in = find(&|c| matches!(c, IOp::Rename { from, to } if *from == new && *to == target))
        .expect("the new_in rename");
    out.insert(Cut::NewIn, with_sync(new_in, &plugin_root));
    let manifest_first =
        find(&|c| op_path(c).is_some_and(|p| p.starts_with(&manifests))).expect("a manifest call");
    out.insert(Cut::PresetsIn, manifest_first);
    let manifest_rename = find(&|c| matches!(c, IOp::Rename { to, .. } if *to == manifest))
        .expect("the manifest rename");
    out.insert(Cut::ManifestWritten, with_sync(manifest_rename, &manifests));
    out.insert(Cut::StaleRemoved, journal_writes[2]);
    out.insert(
        Cut::JournalDone,
        with_sync(journal_renames[2], &journal_dir),
    );
    let deleted = find(&|c| *c == IOp::RemoveTree(old.clone()));
    out.insert(
        Cut::OldDeleted,
        deleted.map_or(out[&Cut::JournalDone], |i| i + 1),
    );
    let removed = find(&|c| *c == IOp::RemoveFile(journal.clone())).expect("the journal removal");
    out.insert(Cut::JournalRemoved, removed + 1);
    out
}

/// The FR-024 step of the mutating call at 1-based index `index`, read from the boundaries.
fn step_at(bounds: &BTreeMap<Cut, usize>, index: usize) -> Step {
    CUTS.iter()
        .find(|c| bounds[c] >= index)
        .map_or(Step::JournalRemoved, |c| cut_step(*c))
}

/// The 1-based mutating index of the `nth` call of `kind` in `calls`.
fn mutating_index(calls: &[IOp], kind: IKind, nth: usize) -> usize {
    let mut seen = 0;
    let mut index = 0;
    for c in calls.iter().filter(|c| c.is_mutating()) {
        index += 1;
        if c.kind() == kind {
            seen += 1;
            if seen == nth {
                return index;
            }
        }
    }
    panic!("no call {nth} of {kind:?}")
}

/// The 1-based number, among the calls of `kind`, of the first call matching `pred`.
fn nth_of(calls: &[IOp], kind: IKind, pred: impl Fn(&IOp) -> bool) -> usize {
    calls
        .iter()
        .filter(|c| c.kind() == kind)
        .position(pred)
        .map(|i| i + 1)
        .unwrap_or_else(|| panic!("no matching {kind:?} call"))
}

// ---- SC-001 --------------------------------------------------------------------------------------------------------

/// The user presets planted before the install, relative to the preset folder (one beside, one in a factory folder).
fn user_presets(r: &Release) -> Vec<(String, Vec<u8>)> {
    vec![
        (
            format!("{}/Mine.vstpreset", r.preset_rel()),
            b"my own preset".to_vec(),
        ),
        (
            format!("{}/Warm Pads/Mine2.vstpreset", r.preset_rel()),
            b"my own preset, two".to_vec(),
        ),
    ]
}

/// The expected user preset tree: the factory rows of `side` (or only the chain) plus the user presets.
fn expected_presets(
    side: Option<&Release>,
    base: &Release,
    users: &[(String, Vec<u8>)],
) -> Vec<(String, char, u64)> {
    let mut out = match side {
        Some(r) => r.preset_rows(),
        None => chain_rows(&base.preset_rel()),
    };
    for (rel, bytes) in users {
        out.push((rel.clone(), 'f', bytes.len() as u64));
        let parent = rel.rsplit_once('/').map(|(p, _)| p).unwrap_or_default();
        out.extend(chain_rows(parent));
    }
    out.sort();
    out.dedup();
    out
}

fn cycle(make: impl Fn(&str) -> Release, label: &str) {
    for subdir in [false, true] {
        let fx = Fx::configured("plugins/fresh");
        let v12 = if subdir { make(V12).kits() } else { make(V12) };
        let v13 = if subdir { make(V13).kits() } else { make(V13) };
        let root = fx.plugin_root();
        assert!(
            absent(&root),
            "{label}: the configured root does not exist yet"
        );
        let users = user_presets(&v12);
        fx.plant(&users);
        let user_bytes = || -> Vec<Vec<u8>> {
            users
                .iter()
                .map(|(rel, _)| {
                    fs::read(dest_path(&fx.user(), rel).expect("plain")).expect("a user preset")
                })
                .collect()
        };
        let before = user_bytes();
        fx.offer(&[&v12]);
        let installed = fx.install(ALPHA, V12);
        assert_eq!(
            installed.notice,
            Some(Notice::RescanDaw),
            "{label}: install notice"
        );
        assert_eq!(
            snap(&root),
            v12.plugin_rows(),
            "{label}/{subdir}: plug-ins after install"
        );
        assert_eq!(
            snap(&fx.user()),
            expected_presets(Some(&v12), &v12, &users),
            "{label}/{subdir}: presets after install"
        );
        assert_eq!(user_bytes(), before, "{label}: user presets after install");
        #[cfg(unix)]
        check_modes(&root, &v12);
        fx.offer(&[&v13]);
        let updated = fx.update(ALPHA, V13);
        assert_eq!(
            updated.notice,
            Some(Notice::RescanDaw),
            "{label}: update notice"
        );
        assert_eq!(
            snap(&root),
            v13.plugin_rows(),
            "{label}/{subdir}: plug-ins after update"
        );
        assert_eq!(
            snap(&fx.user()),
            expected_presets(Some(&v13), &v13, &users),
            "{label}/{subdir}: presets after update"
        );
        assert_eq!(user_bytes(), before, "{label}: user presets after update");
        #[cfg(unix)]
        check_modes(&root, &v13);
        let removed = fx.remove(&FsInstallIo, ALPHA).expect("remove");
        assert_eq!(removed.notice, None, "{label}: remove notice");
        assert_eq!(snap(&root), Vec::new(), "{label}: only the root remains");
        assert!(!absent(&root), "{label}: the root itself stays");
        assert_eq!(
            snap(&fx.user()),
            expected_presets(None, &v13, &users),
            "{label}/{subdir}: presets after remove"
        );
        assert_eq!(user_bytes(), before, "{label}: user presets after remove");
    }
}

#[cfg(unix)]
fn check_modes(root: &Path, r: &Release) {
    let mut want: Vec<(String, u32)> = r
        .entries()
        .iter()
        .filter(|e| e.path().starts_with(&r.bundle()))
        .filter_map(|e| match e {
            Ent::Dir(p) => Some((p.clone(), 0o755)),
            Ent::File(p, m, _) => Some((p.clone(), if m & 0o111 != 0 { 0o755 } else { 0o644 })),
            Ent::Link(..) => None,
        })
        .collect();
    want.sort();
    assert_eq!(modes(root), want, "modes");
}

#[test]
fn install_update_remove_cycle() {
    cycle(|v| Release::of(ALPHA, v), "fs");
}

#[cfg(unix)]
#[test]
fn install_update_remove_cycle_unix() {
    cycle(|v| Release::of(ALPHA, v).unix(), "unix");
}

// ---- SC-002 --------------------------------------------------------------------------------------------------------

/// One SC-002 case on Mem: `setup` installs 1.2.0 when `update`; then `run` over `crash_after(k)`; then `recover`.
struct MemCase {
    fx: Fx,
    staged: MemExtractIo,
}

impl MemCase {
    fn new() -> MemCase {
        let fx = Fx::new();
        let staged = MemExtractIo::new(fx.ctx().roots.staging_dir());
        MemCase { fx, staged }
    }

    fn mem(&self) -> MemInstallIo<'_> {
        MemInstallIo::new(&self.fx.base, &self.fx.ctx().roots, &self.staged)
    }
}

/// The SC-002 table: `rows` decisions in cut order, for `update` (`had_old` true) or a first install.
fn expected_decision(cut: Cut) -> Option<Decision> {
    match cut {
        Cut::JournalRemoved => None,
        c if c >= Cut::NewIn => Some(Decision::Completed),
        _ => Some(Decision::RolledBack),
    }
}

/// SC-002 rows 12-13 qualifier: a cut after `journal_done` leaves the journal on disk at step `done` (read before
/// `recover`, which removes it).
fn check_journal_step(io: &dyn InstallIo, fx: &Fx, cut: Cut, label: &str) {
    if !matches!(cut, Cut::JournalDone | Cut::OldDeleted) {
        return;
    }
    let path = fx.state().join(JOURNAL_DIR).join(format!("{ALPHA}.json"));
    let bytes = io
        .read_file(&path, MAX_JOURNAL_BYTES)
        .expect("journal readable");
    let journal: Journal = serde_json::from_slice(&bytes).expect("journal parses");
    assert_eq!(journal.step, JournalStep::Done, "{label}: journal step");
}

fn check_recovered(
    io: &dyn InstallIo,
    fx: &Fx,
    report: &RecoveryReport,
    cut: Cut,
    old: Option<&Release>,
    new: &Release,
    label: &str,
) {
    match expected_decision(cut) {
        None => assert!(
            report.journals.is_empty(),
            "{label}: no journal: {report:?}"
        ),
        Some(d) => {
            assert_eq!(report.journals.len(), 1, "{label}: {report:?}");
            assert_eq!(report.journals[0].decision, d, "{label}: {report:?}");
        }
    }
    let side = if cut >= Cut::NewIn { Some(new) } else { old };
    assert_side(io, fx, side, new, label);
    let version = fx.manifest(io, &new.name).map(|m| m.version);
    assert_eq!(
        version.as_deref(),
        side.map(|r| r.version.as_str()),
        "{label}: manifest version"
    );
    let journal_dir = fx.state().join(JOURNAL_DIR);
    if io.kind_of(&journal_dir).expect("kind_of").is_some() {
        assert!(
            io.read_dir(&journal_dir).expect("read_dir").is_empty(),
            "{label}: journal/ empty"
        );
    }
    assert_eq!(
        leftovers(io, &fx.base),
        Vec::<String>::new(),
        "{label}: leftovers"
    );
}

#[test]
fn crash_after_each_step_recovers() {
    for update in [false, true] {
        let v12 = Release::of(ALPHA, V12).unix();
        let v13 = Release::of(ALPHA, V13).unix();
        let prepare = |case: &MemCase, mem: &MemInstallIo<'_>| -> (PluginEntry, VerifiedArtifact) {
            if update {
                let (entry, art) = case.fx.artifact(&v12);
                case.fx
                    .drive_one(mem, &case.staged, install_of(&entry, &art))
                    .expect("install 1.2.0");
            }
            case.fx.artifact(&v13)
        };
        let action = |entry: &PluginEntry, art: &VerifiedArtifact| {
            if update {
                update_of(entry, art)
            } else {
                install_of(entry, art)
            }
        };
        let bounds = {
            let case = MemCase::new();
            let mem = case.mem();
            let (entry, art) = prepare(&case, &mem);
            let rec = FaultyInstallIo::new(&mem);
            case.fx
                .drive_one(&rec, &case.staged, action(&entry, &art))
                .expect("the fault-free recording");
            boundaries(&rec.calls(), case.fx.ctx(), ALPHA)
        };
        for cut in CUTS {
            let label = format!("mem update={update} {cut:?}");
            let case = MemCase::new();
            let mem = case.mem();
            let (entry, art) = prepare(&case, &mem);
            let faulty = FaultyInstallIo::new(&mem).crash_after(bounds[&cut]);
            let _ = case
                .fx
                .drive_one(&faulty, &case.staged, action(&entry, &art));
            check_journal_step(&mem, &case.fx, cut, &label);
            let report = case.fx.recover_over(&mem, &case.staged);
            check_recovered(
                &mem,
                &case.fx,
                &report,
                cut,
                update.then_some(&v12),
                &v13,
                &label,
            );
        }
    }
}

#[cfg(unix)]
#[test]
fn crash_after_each_step_recovers_unix() {
    for update in [false, true] {
        let v12 = Release::of(ALPHA, V12).unix();
        let v13 = Release::of(ALPHA, V13).unix();
        let prepare = |fx: &Fx| -> (PluginEntry, VerifiedArtifact) {
            if update {
                let (entry, art) = fx.artifact(&v12);
                fx.drive_one(&FsInstallIo, &FsExtractIo, install_of(&entry, &art))
                    .expect("install 1.2.0");
            }
            fx.artifact(&v13)
        };
        let action = |entry: &PluginEntry, art: &VerifiedArtifact| {
            if update {
                update_of(entry, art)
            } else {
                install_of(entry, art)
            }
        };
        let bounds = {
            let fx = Fx::new();
            let (entry, art) = prepare(&fx);
            let rec = FaultyInstallIo::new(&FsInstallIo);
            fx.drive_one(&rec, &FsExtractIo, action(&entry, &art))
                .expect("the fault-free recording");
            boundaries(&rec.calls(), fx.ctx(), ALPHA)
        };
        for cut in CUTS {
            let label = format!("fs update={update} {cut:?}");
            let fx = Fx::new();
            let (entry, art) = prepare(&fx);
            let faulty = FaultyInstallIo::new(&FsInstallIo).crash_after(bounds[&cut]);
            let _ = fx.drive_one(&faulty, &FsExtractIo, action(&entry, &art));
            check_journal_step(&FsInstallIo, &fx, cut, &label);
            let report = fx.recover_over(&FsInstallIo, &FsExtractIo);
            check_recovered(
                &FsInstallIo,
                &fx,
                &report,
                cut,
                update.then_some(&v12),
                &v13,
                &label,
            );
        }
    }
}

// ---- SC-003 --------------------------------------------------------------------------------------------------------

/// After a real install, rewrite the manifest with `edit`; Remove and Update each refuse with `code` over the
/// fail-closed recorder, and `outside/` is byte-identical.
fn manifest_row(fx: &Fx, edit: impl Fn(&mut Value), code: &str, label: &str) {
    let path = fx.manifest_path(ALPHA);
    let mut doc: Value =
        serde_json::from_slice(&fs::read(&path).expect("the manifest")).expect("JSON");
    edit(&mut doc);
    fs::write(&path, serde_json::to_vec_pretty(&doc).expect("serialises")).expect("rewrite");
    let outside = fx.base.join("outside");
    let before = snap(&outside);
    let rec = RecordingInstallIo::fail_closed();
    assert_eq!(code_of(&fx.remove(&rec, ALPHA)), code, "{label}: remove");
    assert_eq!(
        code_of(&fx.act(&rec, ALPHA, V13, "update")),
        code,
        "{label}: update"
    );
    assert_eq!(rec.mutating_calls(), 0, "{label}: no write");
    assert_eq!(snap(&outside), before, "{label}: outside untouched");
}

fn installed_fx() -> Fx {
    let fx = Fx::new();
    fs::create_dir_all(fx.base.join("outside")).expect("outside");
    fs::write(fx.base.join("outside").join("keep.txt"), b"keep").expect("a canary");
    fx.offer(&[&Release::of(ALPHA, V12)]);
    fx.install(ALPHA, V12);
    fx.offer(&[&Release::of(ALPHA, V13)]);
    fx
}

#[test]
fn manifest_outside_roots_refused() {
    let first_preset = |doc: &mut Value| -> usize {
        doc["files"]
            .as_array()
            .expect("files")
            .iter()
            .position(|e| e["root"] == "presets" && e["type"] == "f")
            .expect("a preset file entry")
    };
    {
        let fx = installed_fx();
        let abs = text(&fx.base.join("outside").join("keep.txt"));
        manifest_row(
            &fx,
            |doc| {
                let i = first_preset(doc);
                doc["files"][i]["path"] = json!(abs.replace('\\', "/"));
            },
            "E_INSTALL_MANIFEST",
            "absolute path",
        );
    }
    {
        let fx = installed_fx();
        manifest_row(
            &fx,
            |doc| {
                let i = first_preset(doc);
                doc["files"][i]["path"] = json!("Alpha/../../../outside/keep.txt");
            },
            "E_INSTALL_MANIFEST",
            "dot-dot",
        );
    }
    {
        let fx = installed_fx();
        let other = text(&fx.base.join("plugins").join("other"));
        manifest_row(
            &fx,
            |doc| doc["plugin_root"] = json!(other),
            "E_INSTALL_ROOT_CHANGED",
            "plugin root",
        );
        let gate = action_gate(
            Os::Linux,
            fx.ctx(),
            &FsInstallIo,
            &Fx::entry_in(&fx.doc(&[&Release::of(ALPHA, V13)]), ALPHA),
        );
        assert!(
            matches!(
                gate,
                ActionGate::Blocked(InstallError::RootChanged { which: "plugin" })
            ),
            "{gate:?}"
        );
    }
    {
        let fx = installed_fx();
        let other = text(&fx.base.join("presets").join("other"));
        manifest_row(
            &fx,
            |doc| doc["preset_root"] = json!(other),
            "E_INSTALL_ROOT_CHANGED",
            "preset root",
        );
        let gate = action_gate(
            Os::Linux,
            fx.ctx(),
            &FsInstallIo,
            &Fx::entry_in(&fx.doc(&[&Release::of(ALPHA, V13)]), ALPHA),
        );
        assert!(
            matches!(
                gate,
                ActionGate::Blocked(InstallError::RootChanged { which: "presets" })
            ),
            "{gate:?}"
        );
    }
}

#[cfg(unix)]
#[test]
fn manifest_outside_roots_refused_unix() {
    let fx = installed_fx();
    let link = fx.user().join(ALPHA).join("Escape");
    std::os::unix::fs::symlink(fx.base.join("outside"), &link).expect("a link to outside");
    let hash = hex_lower(&sha(b"keep"));
    manifest_row(
        &fx,
        |doc| {
            doc["files"]
                .as_array_mut()
                .expect("files")
                .push(json!({"root": "presets", "type": "f", "path": "Alpha/Escape/keep.txt", "sha256": hash}));
        },
        "E_INSTALL_OUTSIDE_ROOT",
        "through a link",
    );
    let gate = action_gate(
        Os::Linux,
        fx.ctx(),
        &FsInstallIo,
        &Fx::entry_in(&fx.doc(&[&Release::of(ALPHA, V13)]), ALPHA),
    );
    match gate {
        ActionGate::Blocked(InstallError::OutsideRoot { path }) => assert_eq!(
            path,
            KeptPath {
                root: RootTag::Presets,
                path: "Alpha/Escape/keep.txt".to_owned()
            }
        ),
        other => panic!("{other:?}"),
    }
    assert_eq!(
        fs::read(fx.base.join("outside").join("keep.txt")).expect("canary"),
        b"keep"
    );
}

// ---- SC-004 and SC-030 ---------------------------------------------------------------------------------------------

#[test]
fn remove_keeps_user_files() {
    let fx = Fx::new();
    let v12 = Release::of(ALPHA, V12);
    fx.offer(&[&v12]);
    fx.install(ALPHA, V12);
    let edited = fx
        .user()
        .join(ALPHA)
        .join("Warm Pads")
        .join("Night Air.vstpreset");
    fs::write(&edited, b"my edit").expect("edit a placed preset");
    let saved = fx
        .user()
        .join(ALPHA)
        .join("Warm Pads")
        .join("Mine.vstpreset");
    fs::write(&saved, b"my own").expect("a user preset in a factory folder");
    fs::write(fx.target(ALPHA).join("extra.txt"), b"added").expect("a file in the bundle");
    let summary = fx.remove(&FsInstallIo, ALPHA).expect("remove");
    assert_eq!(
        summary.kept,
        presets_kept(&["Alpha/Warm Pads/Night Air.vstpreset"])
    );
    assert_eq!(
        FsInstallIo.kind_of(&fx.target(ALPHA)).expect("kind_of"),
        None,
        "the bundle is gone whole"
    );
    let entry = Fx::entry_in(&fx.doc(&[&v12]), ALPHA);
    let listing = Listing::scan(
        fx.ctx().roots.plugin_roots(),
        &["Alpha.vst3"],
        ListingLimits::PRODUCTION,
    );
    match detect::detect_plugin(Os::Linux, fx.ctx(), &entry, &host(), &listing) {
        Detection::Checked { copies, record, .. } => {
            assert!(!copies.iter().any(|c| c.managed), "{copies:?}");
            assert_eq!(record, None);
        }
        other => panic!("{other:?}"),
    }
    assert_eq!(
        detect::read_manifest(&fx.state(), ALPHA, ALPHA),
        detect::RecordRead::Absent
    );
    assert_eq!(fs::read(&edited).expect("kept"), b"my edit");
    assert_eq!(fs::read(&saved).expect("kept"), b"my own");
    assert!(
        !absent(&fx.user().join(ALPHA).join("Warm Pads")),
        "the folder holding a user preset stays"
    );
    assert!(
        absent(&fx.user().join(ALPHA).join("Init.vstpreset")),
        "an unchanged placed preset goes"
    );
}

#[test]
fn remove_deletes_bundle_whole() {
    let fx = Fx::new();
    fx.offer(&[&Release::of(ALPHA, V12)]);
    fx.install(ALPHA, V12);
    fs::write(fx.target(ALPHA).join("Contents").join("user.txt"), b"mine")
        .expect("a file in the bundle");
    let rec = RecordingInstallIo::forwarding(&FsInstallIo);
    let summary = fx.remove(&rec, ALPHA).expect("remove");
    let target = fx.target(ALPHA);
    let calls = rec.calls();
    assert_eq!(
        calls
            .iter()
            .filter(|c| matches!(c, IOp::RemoveTree(p) if *p == target))
            .count(),
        1,
        "exactly one remove_tree of the bundle"
    );
    assert!(
        !calls
            .iter()
            .any(|c| matches!(c, IOp::RemoveFile(p) | IOp::RemoveDir(p) if p.starts_with(&target))),
        "no per-file removal under the bundle"
    );
    assert!(absent(&target));
    assert!(summary.kept.iter().all(|k| k.root == RootTag::Presets));
}

#[cfg(unix)]
#[test]
fn remove_bundle_link_unix() {
    let fx = Fx::new();
    fx.offer(&[&Release::of(ALPHA, V12)]);
    fx.install(ALPHA, V12);
    let canary = fx.base.join("canary");
    fs::create_dir_all(&canary).expect("canary");
    fs::write(canary.join("keep.txt"), b"keep").expect("canary file");
    let link = fx.target(ALPHA).join("Contents").join("escape");
    std::os::unix::fs::symlink(&canary, &link).expect("a link in the bundle");
    fx.remove(&FsInstallIo, ALPHA).expect("remove");
    assert!(
        absent(&link) && absent(&fx.target(ALPHA)),
        "the link went with the bundle"
    );
    assert_eq!(fs::read(canary.join("keep.txt")).expect("intact"), b"keep");
}

/// FR-061: another kind at a preset directory entry's path is left and reported; its missing files count as removed.
#[test]
fn remove_reports_another_kind_at_a_directory_entry() {
    let fx = Fx::new();
    fx.offer(&[&Release::of(ALPHA, V12)]);
    fx.install(ALPHA, V12);
    let pads = fx.user().join(ALPHA).join("Warm Pads");
    fs::remove_dir_all(&pads).expect("the factory folder goes");
    fs::write(&pads, b"a file where the folder was").expect("another kind");
    let summary = fx.remove(&FsInstallIo, ALPHA).expect("remove");
    assert_eq!(summary.kept, presets_kept(&["Alpha/Warm Pads"]));
    assert_eq!(
        fs::read(&pads).expect("left"),
        b"a file where the folder was"
    );
    assert!(absent(&fx.user().join(ALPHA).join("Init.vstpreset")));
    assert!(absent(&fx.manifest_path(ALPHA)), "the manifest is removed");
}

#[test]
fn cut_before_new_in_rolls_back_in_process() {
    for update in [false, true] {
        let v12 = Release::of(ALPHA, V12);
        let v13 = Release::of(ALPHA, V13);
        let fresh = || {
            let fx = Fx::new();
            if update {
                let (e, a) = fx.artifact(&v12);
                fx.drive_one(&FsInstallIo, &FsExtractIo, install_of(&e, &a))
                    .expect("1.2.0");
            }
            fx
        };
        let action = |e: &PluginEntry, a: &VerifiedArtifact| {
            if update {
                update_of(e, a)
            } else {
                install_of(e, a)
            }
        };
        let nth = {
            let fx = fresh();
            let (e, a) = fx.artifact(&v13);
            let rec = FaultyInstallIo::new(&FsInstallIo);
            fx.drive_one(&rec, &FsExtractIo, action(&e, &a))
                .expect("recording");
            let (new, target) = (
                fx.plugin_root().join(format!("Alpha.vst3{NEW_SUFFIX}")),
                fx.target(ALPHA),
            );
            nth_of(
                &rec.calls(),
                IKind::Rename,
                |c| matches!(c, IOp::Rename { from, to } if *from == new && *to == target),
            )
        };
        let fx = fresh();
        let manifest_before = fs::read(fx.manifest_path(ALPHA)).ok();
        let (e, a) = fx.artifact(&v13);
        let faulty =
            FaultyInstallIo::new(&FsInstallIo).fail(IKind::Rename, nth, io::ErrorKind::Other);
        let r = fx.drive_one(&faulty, &FsExtractIo, action(&e, &a));
        assert_eq!(
            r,
            Err(InstallError::Io {
                step: Step::NewIn,
                kind: io::ErrorKind::Other
            }),
            "update={update}"
        );
        assert_side(
            &FsInstallIo,
            &fx,
            update.then_some(&v12),
            &v13,
            &format!("update={update}"),
        );
        assert_eq!(
            fs::read(fx.manifest_path(ALPHA)).ok(),
            manifest_before,
            "the manifest as before"
        );
        assert!(absent(&fx.journal(ALPHA)), "no journal");
        assert_eq!(leftovers(&FsInstallIo, &fx.base), Vec::<String>::new());
    }
}

// ---- SC-005 --------------------------------------------------------------------------------------------------------

#[test]
fn update_preset_rules() {
    let v12 = Release::of(ALPHA, V12).with_presets(vec![
        preset("Init.vstpreset", b"init 1.2.0"),
        preset("Old.vstpreset", b"old"),
        preset("Gone.vstpreset", b"gone"),
        preset("Warm Pads/Night Air.vstpreset", b"night air"),
    ]);
    let v13 = Release::of(ALPHA, V13).with_presets(vec![
        preset("Dir.vstpreset", b"a directory in the way"),
        preset("Init.vstpreset", b"init 1.3.0"),
        preset("Linked.vstpreset", b"linked"),
        preset("New.vstpreset", b"new"),
        preset("Other.vstpreset", b"other 1.3.0"),
        preset("Warm Pads/Night Air.vstpreset", b"night air"),
    ]);
    // Mem rows (every OS).
    {
        let case = MemCase::new();
        let mem = case.mem();
        let fx = &case.fx;
        let (e12, a12) = fx.artifact(&v12);
        fx.drive_one(&mem, &case.staged, install_of(&e12, &a12))
            .expect("1.2.0");
        let at = |rel: &str| dest_path(&fx.user(), rel).expect("plain");
        mem.plant_file(&at("Alpha/Gone.vstpreset"), b"gone, edited", 0o644);
        mem.plant_file(
            &at("Alpha/Warm Pads/Night Air.vstpreset"),
            b"night air, edited",
            0o644,
        );
        mem.plant_file(&at("Alpha/New.vstpreset"), b"new", 0o644);
        mem.plant_file(&at("Alpha/Other.vstpreset"), b"the user's other", 0o644);
        mem.plant_link(&at("Alpha/Linked.vstpreset"), "../../secret-target");
        mem.plant_dir(&at("Alpha/Dir.vstpreset"), 0o755);
        let rec = FaultyInstallIo::new(&mem);
        let (e13, a13) = fx.artifact(&v13);
        let outcome = fx
            .drive_one(&rec, &case.staged, update_of(&e13, &a13))
            .expect("the update");
        let mut kept: Vec<String> = outcome.kept.iter().map(|k| k.path.clone()).collect();
        kept.sort();
        assert_eq!(
            kept,
            vec![
                "Alpha/Dir.vstpreset",
                "Alpha/Gone.vstpreset",
                "Alpha/Linked.vstpreset",
                "Alpha/Other.vstpreset",
                "Alpha/Warm Pads/Night Air.vstpreset",
            ]
        );
        assert_eq!(
            mem.kind_of(&at("Alpha/Old.vstpreset")).expect("kind_of"),
            None,
            "stale unchanged removed"
        );
        assert_eq!(
            mem.hash_file(&at("Alpha/Gone.vstpreset")).ok(),
            Some(sha(b"gone, edited"))
        );
        assert_eq!(
            mem.hash_file(&at("Alpha/Warm Pads/Night Air.vstpreset"))
                .ok(),
            Some(sha(b"night air, edited"))
        );
        assert_eq!(
            mem.hash_file(&at("Alpha/Other.vstpreset")).ok(),
            Some(sha(b"the user's other"))
        );
        assert_eq!(
            mem.hash_file(&at("Alpha/Init.vstpreset")).ok(),
            Some(sha(b"init 1.3.0")),
            "replaced"
        );
        assert_eq!(
            mem.node(&at("Alpha/Linked.vstpreset")),
            Some(MemNode::Link {
                target: "../../secret-target".to_owned()
            })
        );
        assert!(matches!(
            mem.node(&at("Alpha/Dir.vstpreset")),
            Some(MemNode::Dir { .. })
        ));
        let new = at("Alpha/New.vstpreset");
        let new_sibling = at(&format!("Alpha/New.vstpreset{NEW_SUFFIX}"));
        let calls = rec.calls();
        assert!(
            !calls.iter().any(
                |c| matches!(c, IOp::CopyNew { dest, .. } if *dest == new || *dest == new_sibling)
                    || matches!(c, IOp::Rename { to, .. } if *to == new)
            ),
            "the identical destination is adopted without a write"
        );
        assert!(
            !calls.iter().any(|c| op_path(c)
                .or(match c {
                    IOp::HashFile(p) | IOp::ReadFile(p) | IOp::ReadLink(p) | IOp::KindOf(p) =>
                        Some(p.as_path()),
                    _ => None,
                })
                .is_some_and(|p| p.to_string_lossy().contains("secret-target"))),
            "the link is never followed"
        );
        let m = fx.manifest(&mem, ALPHA).expect("a manifest");
        let recorded: Vec<&str> = m
            .files
            .iter()
            .filter(|e| e.root == RootTag::Presets)
            .map(|e| e.path.as_str())
            .collect();
        assert!(
            recorded.contains(&"Alpha/New.vstpreset"),
            "adopted and recorded"
        );
        assert!(
            !recorded.contains(&"Alpha/Warm Pads/Night Air.vstpreset"),
            "the edited current one is not recorded"
        );
        assert!(!recorded.contains(&"Alpha/Other.vstpreset"));
    }
    // The link row on a real filesystem.
    #[cfg(unix)]
    {
        let fx = Fx::new();
        let (e12, a12) = fx.artifact(&v12);
        fx.drive_one(&FsInstallIo, &FsExtractIo, install_of(&e12, &a12))
            .expect("1.2.0");
        let link = fx.user().join(ALPHA).join("Linked.vstpreset");
        std::os::unix::fs::symlink("secret-target", &link).expect("a link");
        let (e13, a13) = fx.artifact(&v13);
        let outcome = fx
            .drive_one(&FsInstallIo, &FsExtractIo, update_of(&e13, &a13))
            .expect("update");
        assert!(outcome.kept.contains(&KeptPath {
            root: RootTag::Presets,
            path: "Alpha/Linked.vstpreset".to_owned()
        }));
        assert_eq!(
            fs::read_link(&link).expect("still a link"),
            Path::new("secret-target")
        );
    }
    // A manual README install (no manifest) updated to 1.3.0.
    {
        let fx = Fx::new();
        let v12 = Release::of(ALPHA, V12);
        let v13 = Release::of(ALPHA, V13);
        for e in v12.entries() {
            let Some(rel) = e.path().strip_prefix(&format!("{}/", v12.bundle())) else {
                continue;
            };
            let path = dest_path(&fx.target(ALPHA), rel).expect("plain");
            match e {
                Ent::Dir(_) => fs::create_dir_all(&path).expect("dir"),
                Ent::File(_, _, b) => fs::write(&path, b).expect("file"),
                Ent::Link(..) => {}
            }
        }
        let mut copied = v12.preset_files();
        copied[0].1 = b"init, edited by hand".to_vec();
        fx.plant(&copied);
        let (e13, a13) = fx.artifact(&v13);
        let outcome = fx
            .drive_one(&FsInstallIo, &FsExtractIo, update_of(&e13, &a13))
            .expect("update a manual install");
        assert_eq!(
            tree_of(&FsInstallIo, &fx.target(ALPHA)),
            v13.bundle_tree(),
            "the bundle is swapped"
        );
        assert_eq!(outcome.kept, presets_kept(&["Alpha/Init.vstpreset"]));
        let m = fx
            .manifest(&FsInstallIo, ALPHA)
            .expect("the first manifest");
        assert!(
            m.files
                .iter()
                .any(|e| e.path == "Alpha/Warm Pads/Night Air.vstpreset"),
            "identical adopted"
        );
        assert!(
            !m.files.iter().any(|e| e.path == "Alpha/Init.vstpreset"),
            "edited not recorded"
        );
        assert_eq!(
            detect::read_manifest(&fx.state(), ALPHA, ALPHA),
            detect::RecordRead::Found(detect::InstallerRecord::Manifest {
                version_text: V13.to_owned(),
                version: CoreVersion::parse(V13),
            })
        );
    }
    // PD-26: a user file at a Place destination and an edit of a Replace destination just before step 7.
    {
        let v12 = Release::of(ALPHA, V12);
        let v13 = Release::of(ALPHA, V13);
        let nth = {
            let fx = Fx::new();
            let (e, a) = fx.artifact(&v12);
            fx.drive_one(&FsInstallIo, &FsExtractIo, install_of(&e, &a))
                .expect("1.2.0");
            let rec = FaultyInstallIo::new(&FsInstallIo);
            let (e, a) = fx.artifact(&v13);
            fx.drive_one(&rec, &FsExtractIo, update_of(&e, &a))
                .expect("recording");
            let new = fx.plugin_root().join(format!("Alpha.vst3{NEW_SUFFIX}"));
            nth_of(
                &rec.calls(),
                IKind::Rename,
                |c| matches!(c, IOp::Rename { from, .. } if *from == new),
            )
        };
        let fx = Fx::new();
        let (e, a) = fx.artifact(&v12);
        fx.drive_one(&FsInstallIo, &FsExtractIo, install_of(&e, &a))
            .expect("1.2.0");
        let place = fx.user().join(ALPHA).join("New.vstpreset");
        let replace = fx.user().join(ALPHA).join("Init.vstpreset");
        let (p, r) = (place.clone(), replace.clone());
        let faulty = FaultyInstallIo::new(&FsInstallIo).on_call(
            IKind::Rename,
            nth,
            move |inner: &dyn InstallIo| {
                inner
                    .write_new(&p, b"saved by the user")
                    .expect("plant at a Place destination");
                inner.remove_file(&r).expect("edit a Replace destination");
                inner.write_new(&r, b"edited by the user").expect("edit");
            },
        );
        let (e, a) = fx.artifact(&v13);
        let outcome = fx
            .drive_one(&faulty, &FsExtractIo, update_of(&e, &a))
            .expect("the update stays Ok");
        assert_eq!(fs::read(&place).expect("kept"), b"saved by the user");
        assert_eq!(fs::read(&replace).expect("kept"), b"edited by the user");
        for rel in ["Alpha/New.vstpreset", "Alpha/Init.vstpreset"] {
            assert!(
                outcome.kept.iter().any(|k| k.path == rel),
                "{rel} kept: {:?}",
                outcome.kept
            );
        }
        let m = fx.manifest(&FsInstallIo, ALPHA).expect("manifest");
        assert!(
            !m.files
                .iter()
                .any(|e| e.path == "Alpha/New.vstpreset" || e.path == "Alpha/Init.vstpreset")
        );
        assert_eq!(leftovers(&FsInstallIo, &fx.base), Vec::<String>::new());
    }
}

/// FR-020: the planner, read-only, decides every stale file (`remove` / `kept`, deepest first) and lists the stale
/// directories, before the first write.
#[test]
fn planner_decides_stale_files() {
    let v12 = Release::of(ALPHA, V12).with_presets(vec![
        preset("Init.vstpreset", b"init 1.2.0"),
        preset("Old.vstpreset", b"old"),
        preset("Gone.vstpreset", b"gone"),
        preset("Retired/Pad.vstpreset", b"pad"),
    ]);
    let v13 = Release::of(ALPHA, V13).with_presets(vec![preset("Init.vstpreset", b"init 1.3.0")]);
    let fx = Fx::new();
    let (e12, a12) = fx.artifact(&v12);
    fx.drive_one(&FsInstallIo, &FsExtractIo, install_of(&e12, &a12))
        .expect("1.2.0");
    fs::write(
        fx.user().join(ALPHA).join("Gone.vstpreset"),
        b"gone, edited",
    )
    .expect("edit a stale preset");
    let (e13, a13) = fx.artifact(&v13);
    let dest = Destinations::of(fx.ctx(), &e13).expect("destinations");
    let previous = fx.manifest(&FsInstallIo, ALPHA);
    let tree = extract::stage(fx.rig.extract_env(&FsExtractIo), &e13, &a13).expect("staged");
    let rec = RecordingInstallIo::forwarding(&FsInstallIo);
    let planned = plan(
        &rec,
        fx.ctx(),
        &dest,
        &tree,
        &e13,
        ActionKind::Update,
        previous.as_ref(),
    );
    extract::discard(tree, &FsExtractIo).expect("discard");
    let planned = planned.expect("a plan");
    assert_eq!(rec.mutating_calls(), 0, "the planner is read-only");
    let decisions: Vec<(&str, &str)> = planned
        .stale
        .iter()
        .map(|op| match op {
            StaleOp::Remove { entry, .. } => (entry.path.as_str(), "remove"),
            StaleOp::Kept { rel } => (rel.as_str(), "kept"),
        })
        .collect();
    assert_eq!(
        decisions,
        vec![
            ("Alpha/Retired/Pad.vstpreset", "remove"),
            ("Alpha/Old.vstpreset", "remove"),
            ("Alpha/Gone.vstpreset", "kept"),
        ]
    );
    assert_eq!(
        planned.stale_dirs,
        vec![dest_path(&fx.user(), "Alpha/Retired").expect("plain")]
    );
    assert_eq!(planned.kept, presets_kept(&["Alpha/Gone.vstpreset"]));
    // The decisions are what the update then does.
    let outcome = fx
        .drive_one(&FsInstallIo, &FsExtractIo, update_of(&e13, &a13))
        .expect("the update");
    assert_eq!(outcome.kept, presets_kept(&["Alpha/Gone.vstpreset"]));
    assert_eq!(outcome.removed, 2);
    for gone in ["Alpha/Retired", "Alpha/Old.vstpreset"] {
        assert!(
            absent(&dest_path(&fx.user(), gone).expect("plain")),
            "{gone}"
        );
    }
    assert_eq!(
        fs::read(fx.user().join(ALPHA).join("Gone.vstpreset")).expect("kept"),
        b"gone, edited"
    );
}

// ---- SC-006 --------------------------------------------------------------------------------------------------------

fn card(view: &status::StatusView, name: &str) -> status::PluginView {
    view.plugins
        .iter()
        .find(|p| p.name == name)
        .cloned()
        .unwrap_or_else(|| panic!("no card for {name}"))
}

fn build_linux(fx: &Fx) -> status::StatusView {
    status::build_with_os(
        Os::Linux,
        Arch::X86_64,
        fx.ctx(),
        FetchMode::Normal,
        NOW,
        &host(),
        &FsInstallIo,
    )
}

#[test]
fn manifest_reads_back_through_detect() {
    let fx = Fx::new();
    let (v12, v13) = (Release::of(ALPHA, V12), Release::of(ALPHA, V13));
    fx.offer(&[&v12]);
    fx.install(ALPHA, V12);
    assert_eq!(
        detect::read_manifest(&fx.state(), ALPHA, ALPHA),
        detect::RecordRead::Found(detect::InstallerRecord::Manifest {
            version_text: V12.to_owned(),
            version: CoreVersion::parse(V12),
        })
    );
    let c = card(&build_linux(&fx), ALPHA);
    assert_eq!(c.installed, InstalledView::Version);
    assert_eq!(c.installed_version.as_deref(), Some(V12));
    let managed = c
        .copies
        .iter()
        .find(|k| k.managed)
        .expect("the managed copy");
    assert_eq!(Path::new(&managed.path), fx.target(ALPHA).as_path());
    assert_eq!(
        c.record.as_ref().map(|r| r.kind),
        Some(RecordKind::Manifest)
    );
    assert_eq!(c.actions, vec![status::ActionView::Remove]);
    fx.offer(&[&v13]);
    let c = card(&build_linux(&fx), ALPHA);
    assert_eq!(c.update, Some(UpdateView::UpdateAvailable));
    assert_eq!(
        c.actions,
        vec![status::ActionView::Update, status::ActionView::Remove]
    );
    fx.update(ALPHA, V13);
    let c = card(&build_linux(&fx), ALPHA);
    assert_eq!(c.update, Some(UpdateView::UpToDate));
    assert_eq!(c.actions, vec![status::ActionView::Remove]);
}

// ---- SC-007 --------------------------------------------------------------------------------------------------------

#[test]
fn unverified_never_installed() {
    let r = Release::of(ALPHA, V12);
    let bytes = r.archive();
    let other = TestSigner::new();
    let mut tampered = bytes.clone();
    let last = tampered.len() - 1;
    tampered[last] ^= 0x01;
    for code in ["E_VERIFY_SHA256", "E_VERIFY_KEY_ID", "E_TC_BINDING"] {
        let fx = Fx::new();
        let key = PlatformKey::LinuxX86_64;
        let (signature, served) = match code {
            "E_VERIFY_SHA256" => (fx.sign(&r), tampered.clone()),
            "E_VERIFY_KEY_ID" => (
                other.sign_c3(&bytes, ALPHA, V12, key, &r.asset()),
                bytes.clone(),
            ),
            _ => (
                fx.signer.sign_c3(&bytes, BETA, V12, key, &r.asset()),
                bytes.clone(),
            ),
        };
        let doc = fx.doc_signed(&[(&r, signature)], |_| {});
        fx.serve(doc, &[]);
        fx.server
            .set_route(&r.route(), Reply::Ranged { body: served });
        let before = (
            snap(&fx.base.join("plugins")),
            snap(&fx.base.join("presets")),
        );
        let counting = CountingDriver::new(&LinuxDriver);
        let drivers = OneDriver(Some(&counting));
        let seam = fx.seam();
        let env = fx.env(Os::Linux, Arch::X86_64, &FsInstallIo, &drivers, &seam);
        let result = for_command(env, ALPHA, V12, "install", NOW, &host(), &mut |_| {});
        assert_eq!(code_of(&result), code);
        assert_eq!(counting.runs(), 0, "{code}: the driver never ran");
        assert!(absent(&fx.extract()), "{code}: stage never ran");
        assert_eq!(
            (
                snap(&fx.base.join("plugins")),
                snap(&fx.base.join("presets"))
            ),
            before,
            "{code}"
        );
    }
}

// ---- SC-008 --------------------------------------------------------------------------------------------------------

/// One refusal row over the fail-closed recorder: the code and the (feed, asset) requests it made.
fn refused_row(
    fx: &Fx,
    call: impl FnOnce(&RecordingInstallIo<'static>) -> Result<ActionSummary, InstallError>,
) -> (&'static str, (usize, usize)) {
    let rec = RecordingInstallIo::fail_closed();
    let before = fx.counts();
    let result = call(&rec);
    let after = fx.counts();
    assert_eq!(rec.mutating_calls(), 0, "no write: {:?}", rec.calls());
    (code_of(&result), (after.0 - before.0, after.1 - before.1))
}

#[test]
fn state_checks_before_download() {
    let (v12, v13) = (Release::of(ALPHA, V12), Release::of(ALPHA, V13));
    // Not installed.
    {
        let fx = Fx::new();
        fx.offer(&[&v12]);
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, V12, "reinstall")),
            ("E_INSTALL_ACTION", (0, 0))
        );
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, "1.1.0", "install")),
            ("E_DOWNLOAD_VERSION_CHANGED", (1, 0))
        );
        let rec = RecordingInstallIo::fail_closed();
        assert_eq!(
            fx.act(&rec, ALPHA, V12, "update").err(),
            Some(InstallError::StateChanged {
                shown: "update",
                current: "not_installed"
            })
        );
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, V12, "update")).1,
            (1, 0)
        );
        assert_eq!(
            refused_row(&fx, |io| fx.remove(io, ALPHA)),
            ("E_INSTALL_NOT_MANAGED", (1, 0))
        );
        assert_eq!(
            refused_row(&fx, |io| fx.remove(io, "Zulu")),
            ("E_INSTALL_UNKNOWN_PLUGIN", (1, 0))
        );
        fs::create_dir_all(fx.journal(ALPHA).parent().expect("journal dir")).expect("journal dir");
        fs::write(fx.journal(ALPHA), b"{}").expect("a pending journal");
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, V12, "install")),
            ("E_INSTALL_JOURNAL_PENDING", (1, 0))
        );
        assert_eq!(
            refused_row(&fx, |io| fx.remove(io, ALPHA)),
            ("E_INSTALL_JOURNAL_PENDING", (1, 0))
        );
    }
    // 1.2.0 managed, the feed at 1.3.0 / 1.2.0.
    {
        let fx = Fx::new();
        fx.offer(&[&v12]);
        fx.install(ALPHA, V12);
        fx.offer(&[&v13]);
        let rec = RecordingInstallIo::fail_closed();
        assert_eq!(
            fx.act(&rec, ALPHA, V13, "install").err(),
            Some(InstallError::StateChanged {
                shown: "install",
                current: "update_available"
            })
        );
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, V13, "install")).1,
            (1, 0)
        );
        fx.offer(&[&v12]);
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, V12, "install")),
            ("E_INSTALL_NOT_NEWER", (1, 0))
        );
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, V12, "update")),
            ("E_INSTALL_NOT_NEWER", (1, 0))
        );
    }
    // 1.3.0 managed, the feed at 1.2.0 (installed newer).
    {
        let fx = Fx::new();
        fx.offer(&[&v13]);
        fx.install(ALPHA, V13);
        fx.offer(&[&v12]);
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, V12, "update")),
            ("E_INSTALL_NOT_NEWER", (1, 0))
        );
    }
    // The feed answers 500 and nothing is cached.
    {
        let fx = Fx::new();
        fx.server.set_route(FEED_ROUTE, Reply::Status(500));
        assert_eq!(
            refused_row(&fx, |io| fx.remove(io, ALPHA)),
            ("E_INSTALL_NO_FEED", (1, 0))
        );
    }
    // Row 13 (SI-26): a schema-1-shaped entry, no release and so no install block.
    {
        let fx = Fx::new();
        let doc = fx.doc_signed(&[(&v12, fx.sign(&v12))], |doc| {
            let entry = doc["plugins"][ALPHA].as_object_mut().expect("an entry");
            entry.remove("platforms");
            entry.remove("install");
        });
        fx.serve(doc, &[]);
        assert_eq!(
            refused_row(&fx, |io| fx.remove(io, ALPHA)),
            ("E_INSTALL_NOT_MANAGEABLE", (1, 0))
        );
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, V12, "install")),
            ("E_DOWNLOAD_UNAVAILABLE", (1, 0))
        );
    }
}

#[test]
fn driver_rechecks_state() {
    let (v12, v13) = (Release::of(ALPHA, V12), Release::of(ALPHA, V13));
    let plant_bundle = |fx: &Fx, info: Option<&[u8]>| {
        let contents = fx.target(ALPHA).join("Contents").join("Resources");
        fs::create_dir_all(&contents).expect("a manual bundle");
        if let Some(info) = info {
            fs::write(contents.join("moduleinfo.json"), info).expect("moduleinfo");
        }
    };
    let refuse = |fx: &Fx, action: Action| -> InstallError {
        let rec = RecordingInstallIo::fail_closed();
        let results = fx.drive(&rec, &FsExtractIo, vec![action]);
        assert_eq!(results.len(), 1, "one ActionResult");
        assert_eq!(rec.mutating_calls(), 0, "no write: {:?}", rec.calls());
        results
            .into_iter()
            .next()
            .map(|r| r.outcome)
            .expect("one")
            .expect_err("a refusal")
    };
    // (a) Install while a target bundle exists.
    {
        let fx = Fx::new();
        plant_bundle(&fx, Some(&moduleinfo_for(ALPHA, V12)));
        let (e, a) = fx.artifact(&v13);
        assert_eq!(
            refuse(&fx, install_of(&e, &a)),
            InstallError::StateChanged {
                shown: "install",
                current: "installed"
            }
        );
    }
    // (b) Update with no target.
    {
        let fx = Fx::new();
        let (e, a) = fx.artifact(&v13);
        assert_eq!(
            refuse(&fx, update_of(&e, &a)),
            InstallError::StateChanged {
                shown: "update",
                current: "not_installed"
            }
        );
    }
    // (c), (d) the manifest records 1.3.0.
    for (offered, label) in [(&v13, "c"), (&v12, "d")] {
        let fx = Fx::new();
        let (e, a) = fx.artifact(&v13);
        fx.drive_one(&FsInstallIo, &FsExtractIo, install_of(&e, &a))
            .expect("1.3.0");
        let (e, a) = fx.artifact(offered);
        assert_eq!(
            refuse(&fx, update_of(&e, &a)),
            InstallError::NotNewer {
                installed: V13.to_owned(),
                offered: offered.version.clone()
            },
            "{label}"
        );
    }
    // (e) a manual bundle at 1.4.0; (f) a manual bundle without a moduleinfo.
    {
        let fx = Fx::new();
        plant_bundle(&fx, Some(&moduleinfo_for(ALPHA, "1.4.0")));
        let (e, a) = fx.artifact(&v13);
        assert_eq!(
            refuse(&fx, update_of(&e, &a)),
            InstallError::NotNewer {
                installed: "1.4.0".to_owned(),
                offered: V13.to_owned()
            }
        );
    }
    {
        let fx = Fx::new();
        plant_bundle(&fx, None);
        let (e, a) = fx.artifact(&v13);
        assert_eq!(
            refuse(&fx, update_of(&e, &a)),
            InstallError::StateChanged {
                shown: "update",
                current: "unknown"
            }
        );
    }
    // (g) an inno-exe artifact (FR-002, FR-113): refused before stage.
    {
        let fx = Fx::new();
        let asset = "Alpha-1.2.0-Windows-x64.exe";
        let bytes = b"an installer".to_vec();
        let signature = fx
            .signer
            .sign_c3(&bytes, ALPHA, V12, PlatformKey::WindowsX86_64, asset);
        let doc = test_feed_document(&[crate::test_support::TestPlugin {
            name: ALPHA,
            version: V12,
            key: PlatformKey::WindowsX86_64,
            asset,
            size: bytes.len() as u64,
            sha256: sha(&bytes),
            signature: signature.clone(),
            min_os: None,
        }]);
        let entry = Fx::entry_in(&doc, ALPHA);
        let art = fx.verified(
            &entry,
            PlatformKey::WindowsX86_64,
            asset,
            &bytes,
            &signature,
        );
        assert_eq!(art.format(), AssetFormat::InnoExe);
        let (linux_entry, _) = fx.artifact(&v12);
        assert_eq!(
            refuse(&fx, install_of(&linux_entry, &art)),
            InstallError::Format
        );
        assert!(absent(&fx.extract()), "g: refused before stage");
    }
    // (h) a tree with a link on a filesystem without links.
    {
        let case = MemCase::new();
        let mem = case.mem();
        let faulty = FaultyInstallIo::new(&mem).without_symlinks();
        let (e, a) = case.fx.artifact(&v12.clone().unix());
        assert_eq!(
            case.fx.drive_one(&faulty, &case.staged, install_of(&e, &a)),
            Err(InstallError::Unsupported)
        );
        assert_eq!(faulty.mutating_calls(), 0, "h");
    }
    // (i) PD-21: a pending manifest over MAX_MANIFEST_BYTES.
    {
        let case = MemCase::new();
        let mem = case.mem();
        let deep: Vec<String> = (0..15)
            .map(|i| format!("{i:02}{}", "d".repeat(238)))
            .collect();
        let presets: Vec<(String, Vec<u8>)> = (0..1100)
            .map(|i| {
                (
                    format!("{}/{i:04}{}.vstpreset", deep.join("/"), "p".repeat(240)),
                    Vec::new(),
                )
            })
            .collect();
        let big = Release::of(ALPHA, V12).with_presets(presets);
        let faulty = FaultyInstallIo::new(&mem);
        let (e, a) = case.fx.artifact(&big);
        assert_eq!(
            case.fx.drive_one(&faulty, &case.staged, install_of(&e, &a)),
            Err(InstallError::Manifest {
                reason: "too large"
            })
        );
        assert_eq!(faulty.mutating_calls(), 0, "i");
    }
    // (j) FR-003: an entry whose release has no install block.
    {
        let fx = Fx::new();
        let (mut e, a) = fx.artifact(&v12);
        if let Some(release) = e.release.as_mut() {
            release.install = None;
        }
        assert_eq!(
            refuse(&fx, install_of(&e, &a)),
            InstallError::NotManageable {
                name: ALPHA.to_owned()
            }
        );
        assert!(absent(&fx.extract()), "j: refused before stage");
    }
}

// ---- SC-009, SC-010 ------------------------------------------------------------------------------------------------

/// 1.2.0 installed through the command, the feed at 1.3.0, the manifest bytes before the update.
fn updatable() -> (Fx, Vec<u8>) {
    let fx = Fx::new();
    fx.offer(&[&Release::of(ALPHA, V12)]);
    fx.install(ALPHA, V12);
    fx.offer(&[&Release::of(ALPHA, V13)]);
    let manifest = fs::read(fx.manifest_path(ALPHA)).expect("the 1.2.0 manifest");
    (fx, manifest)
}

/// The fault-free update's calls and boundaries.
fn update_recording() -> (Vec<IOp>, BTreeMap<Cut, usize>) {
    let (fx, _) = updatable();
    let rec = RecordingInstallIo::forwarding(&FsInstallIo);
    fx.act(&rec, ALPHA, V13, "update")
        .expect("the fault-free update");
    let calls = rec.calls();
    let bounds = boundaries(&calls, fx.ctx(), ALPHA);
    (calls, bounds)
}

/// SC-009's "old, as above".
fn assert_old(fx: &Fx, manifest: &[u8], label: &str) {
    assert_side(
        &FsInstallIo,
        fx,
        Some(&Release::of(ALPHA, V12)),
        &Release::of(ALPHA, V13),
        label,
    );
    assert_eq!(
        fs::read(fx.manifest_path(ALPHA)).expect("manifest"),
        manifest,
        "{label}: manifest"
    );
    assert!(absent(&fx.journal(ALPHA)), "{label}: no journal");
    assert_eq!(
        leftovers(&FsInstallIo, &fx.base),
        Vec::<String>::new(),
        "{label}"
    );
}

/// A pre-commit row: `kind`'s `nth` call fails once with each of the three error kinds.
fn pre_commit_row(
    calls: &[IOp],
    bounds: &BTreeMap<Cut, usize>,
    kind: IKind,
    nth: usize,
    label: &str,
) {
    let step = step_at(bounds, mutating_index(calls, kind, nth));
    assert!(
        step_at(bounds, mutating_index(calls, kind, nth)) != Step::PresetsIn,
        "{label}: pre-commit"
    );
    for (error, want) in [
        (io::ErrorKind::StorageFull, InstallError::Full { step }),
        (
            io::ErrorKind::PermissionDenied,
            InstallError::Denied { step },
        ),
        (
            io::ErrorKind::Other,
            InstallError::Io {
                step,
                kind: io::ErrorKind::Other,
            },
        ),
    ] {
        let (fx, manifest) = updatable();
        let faulty = FaultyInstallIo::new(&FsInstallIo).fail(kind, nth, error);
        assert_eq!(
            fx.act(&faulty, ALPHA, V13, "update").err(),
            Some(want),
            "{label} {error:?}"
        );
        assert_old(&fx, &manifest, &format!("{label} {error:?}"));
        let before = fx.counts();
        fx.update(ALPHA, V13);
        let after = fx.counts();
        assert_eq!(
            (after.0 - before.0, after.1 - before.1),
            (1, 0),
            "{label}: the retry uses the cached artifact"
        );
    }
}

/// The calls of `kind` before the `new_in` rename.
fn before_new_in(calls: &[IOp], bounds: &BTreeMap<Cut, usize>, kind: IKind) -> usize {
    let limit = bounds[&Cut::OldAside];
    calls
        .iter()
        .filter(|c| c.is_mutating())
        .take(limit)
        .filter(|c| c.kind() == kind)
        .count()
}

fn io_rows() {
    let (calls, bounds) = update_recording();
    let middle = |kind| before_new_in(&calls, &bounds, kind).div_ceil(2);
    pre_commit_row(&calls, &bounds, IKind::WriteNew, 1, "write_new 1st");
    pre_commit_row(&calls, &bounds, IKind::Rename, 1, "rename 1st");
    pre_commit_row(&calls, &bounds, IKind::CreateDir, 1, "create_dir 1st");
    pre_commit_row(
        &calls,
        &bounds,
        IKind::CreateDir,
        middle(IKind::CreateDir),
        "create_dir middle",
    );
    pre_commit_row(&calls, &bounds, IKind::CopyNew, 1, "copy_new 1st");
    pre_commit_row(
        &calls,
        &bounds,
        IKind::CopyNew,
        middle(IKind::CopyNew),
        "copy_new middle",
    );
    pre_commit_row(
        &calls,
        &bounds,
        IKind::CopyNew,
        before_new_in(&calls, &bounds, IKind::CopyNew),
        "copy_new last",
    );
    pre_commit_row(&calls, &bounds, IKind::SyncFile, 1, "sync_file 1st");
    pre_commit_row(
        &calls,
        &bounds,
        IKind::SyncFile,
        middle(IKind::SyncFile),
        "sync_file middle",
    );
    pre_commit_row(&calls, &bounds, IKind::WriteNew, 2, "write_new 2nd");
    let old_name = format!("Alpha.vst3{OLD_SUFFIX}");
    let new_name = format!("Alpha.vst3{NEW_SUFFIX}");
    let aside = nth_of(
        &calls,
        IKind::Rename,
        |c| matches!(c, IOp::Rename { from, to } if from.ends_with("Alpha.vst3") && to.ends_with(&old_name)),
    );
    let new_in = nth_of(
        &calls,
        IKind::Rename,
        |c| matches!(c, IOp::Rename { from, .. } if from.ends_with(&new_name)),
    );
    pre_commit_row(&calls, &bounds, IKind::Rename, aside, "rename old_aside");
    pre_commit_row(&calls, &bounds, IKind::Rename, new_in, "rename new_in");
    // new_in and the rename back both fail: the journal stays at staged; the next start rolls back.
    for error in [
        io::ErrorKind::StorageFull,
        io::ErrorKind::PermissionDenied,
        io::ErrorKind::Other,
    ] {
        let (fx, manifest) = updatable();
        let faulty = FaultyInstallIo::new(&FsInstallIo)
            .fail(IKind::Rename, new_in, error)
            .fail(IKind::Rename, new_in + 1, error);
        let r = fx.act(&faulty, ALPHA, V13, "update");
        assert!(
            matches!(&r, Err(e) if matches!(e, InstallError::Full { step: Step::NewIn } | InstallError::Denied { step: Step::NewIn } | InstallError::Io { step: Step::NewIn, .. })),
            "{r:?}"
        );
        let journal: Value =
            serde_json::from_slice(&fs::read(fx.journal(ALPHA)).expect("the journal stays"))
                .expect("JSON");
        assert_eq!(journal["step"], "staged");
        let report = fx.recover_over(&FsInstallIo, &FsExtractIo);
        assert_eq!(
            report
                .journals
                .iter()
                .map(|j| j.decision)
                .collect::<Vec<_>>(),
            vec![Decision::RolledBack]
        );
        assert_old(&fx, &manifest, &format!("new_in and back {error:?}"));
    }
    // Post-commit, persistent: write_new from the 3rd on; rename from the first presets_in call on.
    let first_sibling = nth_of(
        &calls,
        IKind::Rename,
        |c| matches!(c, IOp::Rename { to, .. } if to.extension().is_some_and(|x| x == "vstpreset")),
    );
    for (kind, nth, step) in [
        (IKind::WriteNew, 3, Step::ManifestWritten),
        (IKind::Rename, first_sibling, Step::PresetsIn),
    ] {
        let (fx, _) = updatable();
        let faulty =
            FaultyInstallIo::new(&FsInstallIo).fail_from(kind, nth, io::ErrorKind::StorageFull);
        assert_eq!(
            fx.act(&faulty, ALPHA, V13, "update").err(),
            Some(InstallError::Full { step }),
            "{kind:?}"
        );
        let journal: Value =
            serde_json::from_slice(&fs::read(fx.journal(ALPHA)).expect("the journal stays"))
                .expect("JSON");
        assert_eq!(journal["step"], "staged");
        assert_eq!(journal["pending_manifest"]["version"], V13);
        assert_eq!(
            tree_of(&FsInstallIo, &fx.target(ALPHA)),
            Release::of(ALPHA, V13).bundle_tree()
        );
        let report = fx.recover_over(&FsInstallIo, &FsExtractIo);
        assert_eq!(
            report
                .journals
                .iter()
                .map(|j| j.decision)
                .collect::<Vec<_>>(),
            vec![Decision::Completed]
        );
        assert_side(
            &FsInstallIo,
            &fx,
            Some(&Release::of(ALPHA, V13)),
            &Release::of(ALPHA, V13),
            "rolled forward",
        );
        assert_eq!(
            fx.manifest(&FsInstallIo, ALPHA)
                .map(|m| m.version)
                .as_deref(),
            Some(V13)
        );
        assert!(absent(&fx.journal(ALPHA)));
        assert_eq!(leftovers(&FsInstallIo, &fx.base), Vec::<String>::new());
    }
    // The manifest temp only, one-shot: the in-process roll-forward completes with one warn.
    {
        install_capture();
        let _ = take_mine();
        let (fx, _) = updatable();
        let faulty =
            FaultyInstallIo::new(&FsInstallIo).fail(IKind::WriteNew, 3, io::ErrorKind::StorageFull);
        fx.act(&faulty, ALPHA, V13, "update")
            .expect("rolled forward in process");
        let warns: Vec<String> = take_mine()
            .into_iter()
            .filter(|(t, l, m)| {
                t == LOG_TARGET && *l == Level::Warn && m.contains("E_INSTALL_FULL")
            })
            .map(|(_, _, m)| m)
            .collect();
        assert_eq!(warns.len(), 1, "{warns:?}");
        assert_eq!(
            fx.manifest(&FsInstallIo, ALPHA)
                .map(|m| m.version)
                .as_deref(),
            Some(V13)
        );
        assert!(absent(&fx.journal(ALPHA)));
        assert_eq!(leftovers(&FsInstallIo, &fx.base), Vec::<String>::new());
    }
    // old_deleted fails (persistent): Ok, the journal at done, .krate-old present; then recover completes.
    {
        install_capture();
        let _ = take_mine();
        let (fx, _) = updatable();
        let nth = nth_of(
            &calls,
            IKind::RemoveTree,
            |c| matches!(c, IOp::RemoveTree(p) if p.ends_with(&old_name)),
        );
        let faulty = FaultyInstallIo::new(&FsInstallIo).fail_from(
            IKind::RemoveTree,
            nth,
            io::ErrorKind::PermissionDenied,
        );
        fx.act(&faulty, ALPHA, V13, "update")
            .expect("Ok after journal_done");
        let warns = take_mine()
            .into_iter()
            .filter(|(t, l, _)| t == LOG_TARGET && *l == Level::Warn)
            .count();
        assert_eq!(warns, 1);
        assert_eq!(
            fx.manifest(&FsInstallIo, ALPHA)
                .map(|m| m.version)
                .as_deref(),
            Some(V13)
        );
        let journal: Value =
            serde_json::from_slice(&fs::read(fx.journal(ALPHA)).expect("the journal stays"))
                .expect("JSON");
        assert_eq!(journal["step"], "done");
        assert!(!absent(
            &fx.plugin_root().join(format!("Alpha.vst3{OLD_SUFFIX}"))
        ));
        let report = fx.recover_over(&FsInstallIo, &FsExtractIo);
        assert_eq!(
            report
                .journals
                .iter()
                .map(|j| j.decision)
                .collect::<Vec<_>>(),
            vec![Decision::Completed]
        );
        assert_eq!(leftovers(&FsInstallIo, &fx.base), Vec::<String>::new());
        assert!(absent(&fx.journal(ALPHA)));
    }
}

#[test]
fn io_failures_roll_back() {
    io_rows();
}

#[cfg(unix)]
#[test]
fn io_failures_roll_back_unix() {
    let (calls, bounds) = {
        let fx = Fx::new();
        let (v12, v13) = (
            Release::of(ALPHA, V12).unix(),
            Release::of(ALPHA, V13).unix(),
        );
        fx.offer(&[&v12]);
        fx.install(ALPHA, V12);
        fx.offer(&[&v13]);
        let rec = RecordingInstallIo::forwarding(&FsInstallIo);
        fx.act(&rec, ALPHA, V13, "update").expect("recording");
        let calls = rec.calls();
        let bounds = boundaries(&calls, fx.ctx(), ALPHA);
        (calls, bounds)
    };
    for (kind, label) in [
        (IKind::Symlink, "symlink"),
        (IKind::SetMode, "set_mode"),
        (IKind::SyncDir, "sync_dir"),
    ] {
        let step = step_at(&bounds, mutating_index(&calls, kind, 1));
        let fx = Fx::new();
        let (v12, v13) = (
            Release::of(ALPHA, V12).unix(),
            Release::of(ALPHA, V13).unix(),
        );
        fx.offer(&[&v12]);
        fx.install(ALPHA, V12);
        fx.offer(&[&v13]);
        let manifest = fs::read(fx.manifest_path(ALPHA)).expect("manifest");
        let faulty = FaultyInstallIo::new(&FsInstallIo).fail(kind, 1, io::ErrorKind::StorageFull);
        assert_eq!(
            fx.act(&faulty, ALPHA, V13, "update").err(),
            Some(InstallError::Full { step }),
            "{label}"
        );
        assert_side(&FsInstallIo, &fx, Some(&v12), &v13, label);
        assert_eq!(
            fs::read(fx.manifest_path(ALPHA)).expect("manifest"),
            manifest
        );
        assert!(absent(&fx.journal(ALPHA)));
        assert_eq!(leftovers(&FsInstallIo, &fx.base), Vec::<String>::new());
    }
}

#[test]
fn changed_staged_file_refused() {
    let (calls, _) = update_recording();
    let nth = nth_of(
        &calls,
        IKind::CopyNew,
        |c| matches!(c, IOp::CopyNew { dest, .. } if dest.ends_with("Alpha.so")),
    );
    let (fx, manifest) = updatable();
    let extract = fx.extract();
    let faulty = FaultyInstallIo::new(&FsInstallIo).on_call(
        IKind::CopyNew,
        nth,
        move |_inner: &dyn InstallIo| {
            let work = fs::read_dir(&extract)
                .expect("extract")
                .next()
                .expect("the staged tree")
                .expect("entry")
                .path();
            let so = work
                .join("Alpha.vst3")
                .join("Contents")
                .join("x86_64-linux")
                .join("Alpha.so");
            fs::write(&so, b"rewritten after staging").expect("rewrite the staged file");
        },
    );
    assert_eq!(
        fx.act(&faulty, ALPHA, V13, "update").err(),
        Some(InstallError::Changed {
            path: KeptPath {
                root: RootTag::Plugin,
                path: "Alpha.vst3/Contents/x86_64-linux/Alpha.so".to_owned()
            }
        })
    );
    assert_old(&fx, &manifest, "changed");
}

// ---- SC-011 --------------------------------------------------------------------------------------------------------

/// A driver-level refusal over the fail-closed recorder.
fn collision_of(fx: &Fx, action: Action) -> InstallError {
    let rec = RecordingInstallIo::fail_closed();
    let r = fx.drive_one(&rec, &FsExtractIo, action);
    assert_eq!(rec.mutating_calls(), 0, "no write: {:?}", rec.calls());
    r.expect_err("a refusal")
}

fn collision(root: RootTag, path: &str) -> InstallError {
    InstallError::Collision {
        path: KeptPath {
            root,
            path: path.to_owned(),
        },
    }
}

#[test]
fn collisions_refused_before_write() {
    let (v12, v13) = (Release::of(ALPHA, V12), Release::of(ALPHA, V13));
    // FR-012's one join.
    for bad in [
        "", ".", "..", "a/../b", "C:", "a\\b", "a\0b", "a/", "/a", "a//b",
    ] {
        assert!(dest_path(Path::new("r"), bad).is_err(), "{bad:?}");
    }
    assert_eq!(
        dest_path(Path::new("r"), "a/b").expect("plain"),
        Path::new("r").join("a").join("b")
    );
    // The target is a file.
    {
        let fx = Fx::new();
        fs::write(fx.target(ALPHA), b"a file").expect("plant");
        let (e, a) = fx.artifact(&v12);
        assert_eq!(
            collision_of(&fx, install_of(&e, &a)),
            collision(RootTag::Plugin, "Alpha.vst3")
        );
        assert_eq!(fs::read(fx.target(ALPHA)).expect("untouched"), b"a file");
    }
    // Strays with no journal: .krate-new on Install; .krate-old on Install and on Update.
    for (suffix, update) in [(NEW_SUFFIX, false), (OLD_SUFFIX, false), (OLD_SUFFIX, true)] {
        let fx = Fx::new();
        if update {
            let (e, a) = fx.artifact(&v12);
            fx.drive_one(&FsInstallIo, &FsExtractIo, install_of(&e, &a))
                .expect("1.2.0");
        }
        let stray = fx.plugin_root().join(format!("Alpha.vst3{suffix}"));
        fs::create_dir_all(stray.join("Contents")).expect("a stray");
        let before = snap(&stray);
        let (e, a) = fx.artifact(&v13);
        let action = if update {
            update_of(&e, &a)
        } else {
            install_of(&e, &a)
        };
        assert_eq!(
            collision_of(&fx, action),
            collision(RootTag::Plugin, &format!("Alpha.vst3{suffix}"))
        );
        assert_eq!(snap(&stray), before, "the stray is untouched");
    }
    // A preset directory destination that is a file; an existing `<dest>.krate-new`.
    {
        let fx = Fx::new();
        fx.plant(&[("Alpha/Warm Pads".to_owned(), b"a file".to_vec())]);
        let (e, a) = fx.artifact(&v12);
        assert_eq!(
            collision_of(&fx, install_of(&e, &a)),
            collision(RootTag::Presets, "Alpha/Warm Pads")
        );
    }
    {
        let fx = Fx::new();
        let stray = format!("Alpha/Init.vstpreset{NEW_SUFFIX}");
        fx.plant(&[(stray.clone(), b"a stray".to_vec())]);
        let (e, a) = fx.artifact(&v12);
        assert_eq!(
            collision_of(&fx, install_of(&e, &a)),
            collision(RootTag::Presets, &stray)
        );
        assert_eq!(
            fs::read(
                fx.user()
                    .join(ALPHA)
                    .join(format!("Init.vstpreset{NEW_SUFFIX}"))
            )
            .expect("kept"),
            b"a stray"
        );
    }
    // The plug-in root is a file (SI-13).
    {
        let fx = Fx::configured("plugins/as-file");
        fs::write(fx.plugin_root(), b"a file").expect("plant");
        let (e, a) = fx.artifact(&v12);
        assert_eq!(
            collision_of(&fx, install_of(&e, &a)),
            collision(RootTag::Plugin, "")
        );
    }
    // Mem rows (every OS): PD-24's symlinked ancestors, PD-29's link targets.
    for (kits, label) in [
        (true, "Kits under a linked Alpha"),
        (false, "Alpha itself a link"),
    ] {
        let case = MemCase::new();
        let mem = case.mem();
        let outside = case.fx.base.join("outside");
        mem.plant_dir(&outside, 0o755);
        mem.plant_link(&case.fx.user().join(ALPHA), &text(&outside));
        let r = if kits {
            v12.clone().kits()
        } else {
            v12.clone()
        };
        let faulty = FaultyInstallIo::new(&mem);
        let (e, a) = case.fx.artifact(&r);
        assert_eq!(
            case.fx.drive_one(&faulty, &case.staged, install_of(&e, &a)),
            Err(collision(RootTag::Presets, "Alpha")),
            "{label}"
        );
        assert_eq!(faulty.mutating_calls(), 0, "{label}");
        assert_eq!(
            mem.paths_under(&outside),
            Vec::<PathBuf>::new(),
            "{label}: outside untouched"
        );
    }
    for extra in [
        vec![
            Ent::Dir("presets/a".to_owned()),
            Ent::Dir("presets/b".to_owned()),
            Ent::Link("presets/a/l".to_owned(), "../../presets/b".to_owned()),
        ],
        vec![
            Ent::File("Alpha.vst3/Contents/x".to_owned(), 0o644, b"x".to_vec()),
            Ent::Link(
                "Alpha.vst3/Contents/l".to_owned(),
                "../../Alpha.vst3/Contents/x".to_owned(),
            ),
        ],
    ] {
        let case = MemCase::new();
        let mem = case.mem();
        let mut r = v12.clone();
        r.extra = extra;
        let faulty = FaultyInstallIo::new(&mem);
        let (e, a) = case.fx.artifact(&r);
        assert_eq!(
            case.fx.drive_one(&faulty, &case.staged, install_of(&e, &a)),
            Err(InstallError::Path {
                reason: "link target leaves its top-level entry"
            })
        );
        assert_eq!(faulty.mutating_calls(), 0);
    }
}

#[cfg(unix)]
#[test]
fn collisions_refused_before_write_unix() {
    use std::os::unix::fs::symlink;
    let v12 = Release::of(ALPHA, V12);
    // The target is a symlink to a directory.
    {
        let fx = Fx::new();
        let real = fx.base.join("real-bundle");
        fs::create_dir_all(&real).expect("real");
        symlink(&real, fx.target(ALPHA)).expect("a linked target");
        let (e, a) = fx.artifact(&v12);
        assert_eq!(
            collision_of(&fx, install_of(&e, &a)),
            collision(RootTag::Plugin, "Alpha.vst3")
        );
        assert!(
            fs::symlink_metadata(fx.target(ALPHA))
                .expect("still")
                .file_type()
                .is_symlink()
        );
    }
    // PD-24 on a real filesystem.
    for kits in [true, false] {
        let fx = Fx::new();
        let outside = fx.base.join("outside");
        fs::create_dir_all(&outside).expect("outside");
        symlink(&outside, fx.user().join(ALPHA)).expect("a linked preset folder");
        let r = if kits {
            v12.clone().kits()
        } else {
            v12.clone()
        };
        let (e, a) = fx.artifact(&r);
        assert_eq!(
            collision_of(&fx, install_of(&e, &a)),
            collision(RootTag::Presets, "Alpha")
        );
        assert_eq!(snap(&outside), Vec::new());
    }
    // PD-28: a linked state subdirectory, through the command.
    for (dir, code) in [
        (JOURNAL_DIR, "E_INSTALL_JOURNAL"),
        (detect::MANIFESTS_DIR, "E_INSTALL_MANIFEST"),
    ] {
        let fx = Fx::new();
        let elsewhere = fx.base.join("elsewhere-state");
        fs::create_dir_all(&elsewhere).expect("elsewhere");
        symlink(&elsewhere, fx.state().join(dir)).expect("a linked state dir");
        fx.offer(&[&v12]);
        let rec = RecordingInstallIo::fail_closed();
        let before = fx.counts();
        let r = fx.act(&rec, ALPHA, V12, "install");
        let after = fx.counts();
        match &r {
            Err(InstallError::Journal { reason }) | Err(InstallError::Manifest { reason }) => {
                assert_eq!(*reason, "state directory is not a directory");
            }
            other => panic!("{dir}: {other:?}"),
        }
        assert_eq!(code_of(&r), code);
        assert_eq!((after.0 - before.0, after.1 - before.1), (1, 0));
        assert_eq!(rec.mutating_calls(), 0);
        assert_eq!(snap(&elsewhere), Vec::new());
    }
}

// ---- SC-012 --------------------------------------------------------------------------------------------------------

#[test]
fn remove_crash_resumes() {
    let setup = || {
        let fx = Fx::new();
        fx.offer(&[&Release::of(ALPHA, V12)]);
        fx.install(ALPHA, V12);
        fx
    };
    let (reference, cuts) = {
        let fx = setup();
        let rec = RecordingInstallIo::forwarding(&FsInstallIo);
        fx.remove(&rec, ALPHA).expect("the fault-free remove");
        let muts: Vec<IOp> = rec
            .calls()
            .into_iter()
            .filter(|c| c.is_mutating())
            .collect();
        let journal = fx.journal(ALPHA);
        let journal_rename = muts
            .iter()
            .position(|c| matches!(c, IOp::Rename { to, .. } if *to == journal))
            .expect("the journal");
        let after_journal = if matches!(muts.get(journal_rename + 1), Some(IOp::SyncDir(_))) {
            journal_rename + 2
        } else {
            journal_rename + 1
        };
        let removals: Vec<usize> = muts
            .iter()
            .enumerate()
            .filter(|(_, c)| matches!(c, IOp::RemoveTree(p) | IOp::RemoveFile(p) | IOp::RemoveDir(p) if !p.starts_with(fx.state())))
            .map(|(i, _)| i + 1)
            .collect();
        let half = removals[removals.len() / 2 - 1];
        (
            (snap(&fx.plugin_root()), snap(&fx.user())),
            vec![after_journal, half],
        )
    };
    for k in cuts {
        let fx = setup();
        let faulty = FaultyInstallIo::new(&FsInstallIo).crash_after(k);
        assert!(fx.remove(&faulty, ALPHA).is_err(), "cut {k}");
        let report = fx.recover_over(&FsInstallIo, &FsExtractIo);
        assert_eq!(
            report
                .journals
                .iter()
                .map(|j| j.decision)
                .collect::<Vec<_>>(),
            vec![Decision::Completed],
            "cut {k}"
        );
        assert_eq!(
            (snap(&fx.plugin_root()), snap(&fx.user())),
            reference,
            "cut {k}"
        );
        let rec = RecordingInstallIo::fail_closed();
        assert_eq!(code_of(&fx.remove(&rec, ALPHA)), "E_INSTALL_NOT_MANAGED");
        assert_eq!(rec.mutating_calls(), 0);
    }
}

// ---- SC-013 --------------------------------------------------------------------------------------------------------

/// A journal JSON for `name` naming `target` (and `preset_subdir`, `siblings`), as recovery reads it.
fn planted_journal(fx: &Fx, name: &str, target: &Path, subdir: Option<&str>, siblings: Value) {
    let root = target.parent().map(Path::to_path_buf).unwrap_or_default();
    let bundle = format!("{name}.vst3");
    let doc = json!({
        "journal_version": 1, "op": "install", "plugin": name, "stem": name, "vst3_bundle": bundle,
        "preset_subdir": subdir, "step": "staged",
        "target": text(target),
        "new": text(&root.join(format!("{bundle}{NEW_SUFFIX}"))),
        "old": text(&root.join(format!("{bundle}{OLD_SUFFIX}"))),
        "had_old": false, "siblings": siblings, "created_dirs": [],
        "pending_manifest": null, "previous_manifest": null,
    });
    fs::create_dir_all(fx.state().join(JOURNAL_DIR)).expect("journal dir");
    fs::write(fx.journal(name), serde_json::to_vec(&doc).expect("JSON")).expect("plant");
}

/// `assemble_with_os(Os::Linux, …)` over the rig's roots; the refused journal of `refused` stays, is logged once
/// with `E_INSTALL_JOURNAL`, blocks its card and its commands.
fn assemble_and_check_refused(fx: &Fx, refused: &str) {
    install_capture();
    let _ = take_mine();
    let before = (snap(&fx.plugin_root()), snap(&fx.user()));
    let journal_before = fs::read(fx.journal(refused)).expect("the journal");
    let state = crate::context::assemble_with_os(
        Os::Linux,
        Ok(fx.ctx().roots.clone()),
        &fx.ctx().feed_url,
        fx.ctx().http.clone(),
        fx.ctx().signing_key.clone(),
    );
    assert!(state.context.is_ok(), "{:?}", state.context);
    drop(state);
    let records: Vec<String> = take_mine()
        .into_iter()
        .filter(|(t, _, m)| {
            t == LOG_TARGET && m.contains("E_INSTALL_JOURNAL") && m.contains(refused)
        })
        .map(|(_, _, m)| m)
        .collect();
    assert_eq!(records.len(), 1, "{refused}: {records:?}");
    assert_eq!(
        fs::read(fx.journal(refused)).expect("the journal stays"),
        journal_before
    );
    let after = (snap(&fx.plugin_root()), snap(&fx.user()));
    let of = |rows: &Vec<(String, char, u64)>| -> Vec<(String, char, u64)> {
        rows.iter()
            .filter(|r| r.0.starts_with(refused))
            .cloned()
            .collect()
    };
    assert_eq!(
        (of(&after.0), of(&after.1)),
        (of(&before.0), of(&before.1)),
        "{refused}: untouched"
    );
    assert_eq!(
        card(&build_linux(fx), refused).actions,
        Vec::new(),
        "{refused}: no action offered"
    );
    assert_eq!(
        code_of(&fx.act(&FsInstallIo, refused, V12, "install")),
        "E_INSTALL_JOURNAL_PENDING"
    );
    assert_eq!(
        code_of(&fx.remove(&FsInstallIo, refused)),
        "E_INSTALL_JOURNAL_PENDING"
    );
}

#[test]
fn startup_recovery() {
    let (v12, v13, beta) = (
        Release::of(ALPHA, V12),
        Release::of(ALPHA, V13),
        Release::of(BETA, V12),
    );
    let new_in = {
        let (calls, _) = update_recording();
        nth_of(
            &calls,
            IKind::Rename,
            |c| matches!(c, IOp::Rename { from, .. } if from.to_string_lossy().ends_with(NEW_SUFFIX) && from.file_name().is_some_and(|n| n == "Alpha.vst3.krate-new")),
        )
    };
    // Alpha's update cut after old_aside (new_in and the rename back fail); Beta's journal names a path outside.
    {
        let fx = Fx::new();
        fx.offer(&[&v12, &beta]);
        fx.install(ALPHA, V12);
        fx.offer(&[&v13, &beta]);
        let faulty = FaultyInstallIo::new(&FsInstallIo)
            .fail(IKind::Rename, new_in, io::ErrorKind::Other)
            .fail(IKind::Rename, new_in + 1, io::ErrorKind::Other);
        assert!(fx.act(&faulty, ALPHA, V13, "update").is_err());
        assert!(!absent(&fx.journal(ALPHA)), "the cut left a staged journal");
        let elsewhere = fx.base.join("..").join("elsewhere").join("Beta.vst3");
        planted_journal(&fx, BETA, &elsewhere, None, json!([]));
        let temp = fx
            .state()
            .join(detect::MANIFESTS_DIR)
            .join(".Alpha.json.tmp");
        fs::write(&temp, b"{ partial").expect("a temp leftover");
        let work = fx.extract().join(format!("{}.abcdef", "b".repeat(64)));
        fs::create_dir_all(&work).expect("an extract leftover");
        assemble_and_check_refused(&fx, BETA);
        assert_side(&FsInstallIo, &fx, Some(&v12), &v13, "Alpha rolled back");
        assert!(absent(&fx.journal(ALPHA)));
        assert!(absent(&temp) && absent(&work), "the leftovers are gone");
        assert_eq!(
            leftovers(&FsInstallIo, &fx.plugin_root()),
            Vec::<String>::new()
        );
    }
    // PD-25: a journal whose preset_subdir is `..`; one whose sibling lies outside its preset folder.
    for (subdir, sibling) in [("..", None), ("Kits", Some("Other"))] {
        let fx = Fx::new();
        fx.offer(&[&beta]);
        let siblings = match sibling {
            Some(other) => {
                let dest = fx.user().join(BETA).join(other).join("x.vstpreset");
                json!([{ "dest": text(&dest), "new": format!("{}{NEW_SUFFIX}", text(&dest)), "pre": "absent" }])
            }
            None => json!([]),
        };
        planted_journal(&fx, BETA, &fx.target(BETA), Some(subdir), siblings);
        assemble_and_check_refused(&fx, BETA);
    }
}

#[test]
fn recovery_table_rows() {
    let v12 = Release::of(ALPHA, V12).unix();
    let v13 = Release::of(ALPHA, V13).unix();
    let staged_cut = {
        let case = MemCase::new();
        let mem = case.mem();
        let (e, a) = case.fx.artifact(&v12);
        case.fx
            .drive_one(&mem, &case.staged, install_of(&e, &a))
            .expect("1.2.0");
        let rec = FaultyInstallIo::new(&mem);
        let (e, a) = case.fx.artifact(&v13);
        case.fx
            .drive_one(&rec, &case.staged, update_of(&e, &a))
            .expect("recording");
        boundaries(&rec.calls(), case.fx.ctx(), ALPHA)[&Cut::JournalStaged]
    };
    for row in 1..=4 {
        let case = MemCase::new();
        let mem = case.mem();
        let fx = &case.fx;
        let (e, a) = fx.artifact(&v12);
        fx.drive_one(&mem, &case.staged, install_of(&e, &a))
            .expect("1.2.0");
        let (e13, a13) = fx.artifact(&v13);
        let faulty = FaultyInstallIo::new(&mem).crash_after(staged_cut);
        let _ = fx.drive_one(&faulty, &case.staged, update_of(&e13, &a13));
        let (target, new, old) = (
            fx.target(ALPHA),
            fx.plugin_root().join(format!("Alpha.vst3{NEW_SUFFIX}")),
            fx.plugin_root().join(format!("Alpha.vst3{OLD_SUFFIX}")),
        );
        let place = fx.user().join(ALPHA).join("New.vstpreset");
        match row {
            1 => {
                mem.rename(&target, &old).expect("plant");
                mem.remove_tree(&new).expect("plant");
            }
            2 => {
                mem.remove_tree(&new).expect("plant");
                mem.remove_tree(&target).expect("plant");
            }
            3 => mem.remove_tree(&new).expect("plant"),
            _ => {
                mem.rename(&target, &old).expect("plant");
                mem.rename(&new, &target).expect("plant");
                mem.plant_file(&place, b"saved by the user", 0o644);
            }
        }
        let before: Vec<(PathBuf, Option<MemNode>)> = mem
            .paths_under(&fx.base)
            .into_iter()
            .map(|p| {
                let n = mem.node(&p);
                (p, n)
            })
            .collect();
        install_capture();
        let _ = take_mine();
        let report = fx.recover_over(&mem, &case.staged);
        let decisions: Vec<Decision> = report.journals.iter().map(|j| j.decision).collect();
        match row {
            1 => {
                assert_eq!(decisions, vec![Decision::RolledBack], "row 1");
                assert_eq!(
                    tree_of(&mem, &target),
                    v12.bundle_tree(),
                    "row 1: the old bundle"
                );
                assert_eq!(leftovers(&mem, &fx.plugin_root()), Vec::<String>::new());
                assert_eq!(mem.kind_of(&fx.journal(ALPHA)).expect("kind_of"), None);
            }
            2 | 3 => {
                assert_eq!(decisions, vec![Decision::Refused], "row {row}");
                let after: Vec<(PathBuf, Option<MemNode>)> = mem
                    .paths_under(&fx.base)
                    .into_iter()
                    .map(|p| {
                        let n = mem.node(&p);
                        (p, n)
                    })
                    .collect();
                assert_eq!(after, before, "row {row}: nothing touched");
                assert!(mem.kind_of(&fx.journal(ALPHA)).expect("kind_of").is_some());
                assert!(matches!(
                    action_gate(Os::Linux, fx.ctx(), &mem, &e13),
                    ActionGate::Blocked(InstallError::JournalPending { .. })
                ));
            }
            _ => {
                assert_eq!(decisions, vec![Decision::Completed], "row 4");
                assert_eq!(mem.hash_file(&place).ok(), Some(sha(b"saved by the user")));
                let m = fx.manifest(&mem, ALPHA).expect("written");
                assert!(!m.files.iter().any(|e| e.path == "Alpha/New.vstpreset"));
                let warns: Vec<String> = take_mine()
                    .into_iter()
                    .filter(|(t, l, m)| {
                        t == LOG_TARGET
                            && *l == Level::Warn
                            && m.contains("presets:Alpha/New.vstpreset")
                    })
                    .map(|(_, _, m)| m)
                    .collect();
                assert_eq!(warns.len(), 1, "{warns:?}");
                assert_eq!(leftovers(&mem, &fx.base), Vec::<String>::new());
            }
        }
    }
}

// ---- SC-014 --------------------------------------------------------------------------------------------------------

#[test]
fn concurrent_actions() {
    let (alpha, beta) = (Release::of(ALPHA, V12), Release::of(BETA, V12));
    let fx = Fx::new();
    fx.offer(&[&alpha, &beta]);
    let ctx = fx.ctx().clone();
    let server = &fx.server;
    let seam_for = |request: DownloadRequest| {
        let path = url::Url::parse(request.asset().url.as_str())
            .expect("URL")
            .path()
            .to_owned();
        request
            .with_fixture_url(&server.url(&path))
            .expect("fixture URL")
    };
    // One held install: `ready` on its first Staging event, then it waits for `go`.
    let held = |name: &'static str, ready: mpsc::Sender<()>, go: mpsc::Receiver<()>| {
        let rig = InstallRig::new(ctx.clone());
        let mut env = rig.command_env(
            Os::Linux,
            Arch::X86_64,
            &FsInstallIo,
            &FsExtractIo,
            &NativeDrivers,
        );
        env.request_seam = Some(&seam_for);
        let mut first = true;
        for_command(env, name, V12, "install", NOW, &host(), &mut |e| {
            if first && e == InstallProgress::Staging {
                first = false;
                ready.send(()).expect("ready");
                go.recv().expect("go");
            }
        })
    };
    // Same plugin: the second call is BUSY at once.
    std::thread::scope(|s| {
        let (ready_tx, ready_rx) = mpsc::channel();
        let (go_tx, go_rx) = mpsc::channel();
        let a = s.spawn(|| held(ALPHA, ready_tx, go_rx));
        ready_rx.recv().expect("A holds the guard");
        assert_eq!(
            refused_row(&fx, |io| fx.act(io, ALPHA, V12, "install")),
            ("E_INSTALL_BUSY", (1, 0))
        );
        assert_eq!(
            refused_row(&fx, |io| fx.remove(io, ALPHA)),
            ("E_INSTALL_BUSY", (1, 0))
        );
        go_tx.send(()).expect("go");
        a.join().expect("A").expect("A ends Ok");
    });
    // Two plugins in flight at once.
    let fx2 = Fx::new();
    fx2.offer(&[&alpha, &beta]);
    let ctx2 = fx2.ctx().clone();
    let server2 = &fx2.server;
    let seam2 = |request: DownloadRequest| {
        let path = url::Url::parse(request.asset().url.as_str())
            .expect("URL")
            .path()
            .to_owned();
        request
            .with_fixture_url(&server2.url(&path))
            .expect("fixture URL")
    };
    let held2 = |name: &'static str, ready: mpsc::Sender<()>, go: mpsc::Receiver<()>| {
        let rig = InstallRig::new(ctx2.clone());
        let mut env = rig.command_env(
            Os::Linux,
            Arch::X86_64,
            &FsInstallIo,
            &FsExtractIo,
            &NativeDrivers,
        );
        env.request_seam = Some(&seam2);
        let mut first = true;
        for_command(env, name, V12, "install", NOW, &host(), &mut |e| {
            if first && e == InstallProgress::Staging {
                first = false;
                ready.send(()).expect("ready");
                go.recv().expect("go");
            }
        })
    };
    std::thread::scope(|s| {
        let (ready_tx, ready_rx) = mpsc::channel();
        let (go_a, go_a_rx) = mpsc::channel();
        let (go_b, go_b_rx) = mpsc::channel();
        let ready_b = ready_tx.clone();
        let a = s.spawn(|| held2(ALPHA, ready_tx, go_a_rx));
        let b = s.spawn(|| held2(BETA, ready_b, go_b_rx));
        ready_rx.recv().expect("one ready");
        ready_rx.recv().expect("both ready");
        go_a.send(()).expect("go");
        go_b.send(()).expect("go");
        a.join().expect("A").expect("Alpha Ok");
        b.join().expect("B").expect("Beta Ok");
    });
    for name in [ALPHA, BETA] {
        assert!(
            matches!(
                detect::read_manifest(&fx2.state(), name, name),
                detect::RecordRead::Found(_)
            ),
            "{name}"
        );
    }
    // The guard is released after a failure.
    let fx3 = Fx::new();
    fx3.offer(&[&alpha]);
    let faulty =
        FaultyInstallIo::new(&FsInstallIo).fail(IKind::CopyNew, 1, io::ErrorKind::StorageFull);
    assert_eq!(
        code_of(&fx3.act(&faulty, ALPHA, V12, "install")),
        "E_INSTALL_FULL"
    );
    fx3.install(ALPHA, V12);
}

/// SC-014: two plugins in flight both see no `<state>/journal` and both create it; the one that loses the race
/// finds the directory made and goes on (the race `concurrent_actions` hit as `Io { JournalBegun, AlreadyExists }`).
#[test]
fn state_dir_made_by_another_action() {
    let fx = Fx::new();
    let alpha = Release::of(ALPHA, V12);
    let journal_dir = fx.state().join(JOURNAL_DIR);
    let nth = {
        let probe = Fx::new();
        let rec = FaultyInstallIo::new(&FsInstallIo);
        let (e, a) = probe.artifact(&alpha);
        probe
            .drive_one(&rec, &FsExtractIo, install_of(&e, &a))
            .expect("recording");
        let dir = probe.state().join(JOURNAL_DIR);
        nth_of(
            &rec.calls(),
            IKind::CreateDir,
            |c| matches!(c, IOp::CreateDir(p) if *p == dir),
        )
    };
    let made = journal_dir.clone();
    let faulty = FaultyInstallIo::new(&FsInstallIo).on_call(
        IKind::CreateDir,
        nth,
        move |inner: &dyn InstallIo| {
            inner
                .create_dir(&made)
                .expect("the other action makes the journal folder");
        },
    );
    let (e, a) = fx.artifact(&alpha);
    fx.drive_one(&faulty, &FsExtractIo, install_of(&e, &a))
        .expect("the install goes on");
    assert!(matches!(
        detect::read_manifest(&fx.state(), ALPHA, ALPHA),
        detect::RecordRead::Found(_)
    ));
    assert!(absent(&fx.journal(ALPHA)), "no journal left");
}

// ---- SC-015, SC-017 ------------------------------------------------------------------------------------------------

#[test]
fn sync_before_commit() {
    let (fx, _) = updatable();
    let rec = RecordingInstallIo::forwarding(&FsInstallIo);
    fx.act(&rec, ALPHA, V13, "update").expect("update");
    let calls = rec.calls();
    let journal_dir = fx.state().join(JOURNAL_DIR);
    let staged_write = calls
        .iter()
        .enumerate()
        .filter(|(_, c)| matches!(c, IOp::WriteNew(p) if p.parent() == Some(journal_dir.as_path())))
        .nth(1)
        .map(|(i, _)| i)
        .expect("the staged journal write");
    let new = fx.plugin_root().join(format!("Alpha.vst3{NEW_SUFFIX}"));
    let mut parents = Vec::new();
    for (i, c) in calls[..staged_write].iter().enumerate() {
        if let IOp::CopyNew { dest, .. } = c {
            assert!(
                calls[i + 1..staged_write].contains(&IOp::SyncFile(dest.clone())),
                "{dest:?} is synced before the staged journal"
            );
            if !dest.starts_with(&new) {
                parents.push(dest.parent().expect("a parent").to_path_buf());
            }
        }
    }
    let sync_dirs: Vec<&IOp> = calls
        .iter()
        .filter(|c| c.kind() == IKind::SyncDir)
        .collect();
    if cfg!(unix) {
        for (i, c) in calls[..staged_write].iter().enumerate() {
            if let IOp::CreateDir(dir) = c
                && dir.starts_with(fx.plugin_root())
            {
                assert!(
                    calls[i + 1..staged_write].contains(&IOp::SyncDir(dir.clone())),
                    "{dir:?}"
                );
            }
        }
        for p in parents {
            assert!(
                calls[..staged_write].contains(&IOp::SyncDir(p.clone())),
                "sibling parent {p:?}"
            );
        }
        let new_in = calls
            .iter()
            .position(|c| matches!(c, IOp::Rename { from, .. } if *from == new))
            .expect("new_in");
        assert_eq!(
            calls[new_in + 1],
            IOp::SyncDir(fx.plugin_root()),
            "the root after new_in"
        );
    } else {
        assert!(
            sync_dirs.is_empty(),
            "no directory sync on Windows: {sync_dirs:?}"
        );
    }
}

#[test]
fn progress_events() {
    let (fx, _) = updatable();
    let seam = fx.seam();
    let env = fx.env(Os::Linux, Arch::X86_64, &FsInstallIo, &NativeDrivers, &seam);
    let mut events = Vec::new();
    for_command(env, ALPHA, V13, "update", NOW, &host(), &mut |e| {
        events.push(e)
    })
    .expect("update");
    let staging = events
        .iter()
        .position(|e| *e == InstallProgress::Staging)
        .expect("Staging");
    assert!(
        events[..staging]
            .iter()
            .all(|e| matches!(e, InstallProgress::Download { .. })),
        "{events:?}"
    );
    let placing: Vec<(u64, u64)> = events
        .iter()
        .filter_map(|e| match e {
            InstallProgress::Placing { done, total } => Some((*done, *total)),
            _ => None,
        })
        .collect();
    let total = placing.last().map(|p| p.1).expect("Placing events");
    // Two bundle files, the changed Init and the added New (Night Air is adopted).
    assert_eq!(total, 4);
    assert_eq!(placing, (1..=total).map(|d| (d, total)).collect::<Vec<_>>());
    let n = placing.len();
    assert_eq!(&events[staging + 1..staging + 1 + n].len(), &n);
    assert_eq!(
        events[staging + 1 + n..],
        [InstallProgress::Committing, InstallProgress::Done]
    );
    let manifest = fx.manifest(&FsInstallIo, ALPHA).expect("manifest");
    let presets = manifest
        .files
        .iter()
        .filter(|e| e.root == RootTag::Presets)
        .count() as u64;
    let env = fx.env(Os::Linux, Arch::X86_64, &FsInstallIo, &NativeDrivers, &seam);
    let mut events = Vec::new();
    remove_for_command(env, ALPHA, NOW, &mut |e| events.push(e)).expect("remove");
    let total = 1 + presets;
    let mut want: Vec<InstallProgress> = (0..=total)
        .map(|done| InstallProgress::Removing { done, total })
        .collect();
    want.push(InstallProgress::Done);
    assert_eq!(events, want);
}

// ---- SC-016, SC-029 ------------------------------------------------------------------------------------------------

/// Adds the asset the schema binds to `key` (inno-exe / pkg) and its install facts to `name`'s entry (§7).
fn add_platform(fx: &Fx, doc: &mut Value, name: &str, key: PlatformKey) {
    let lower = name.to_ascii_lowercase();
    let asset = format!("{name}-{V12}-{}", key.as_str());
    let signature = fx.signer.sign_c3(b"x", name, V12, key, &asset);
    let mut platform = json!({
        "url": format!("https://assets.example.invalid/{lower}/v{V12}/{asset}"),
        "size": 1,
        "sha256": hex_lower(&sha(b"x")),
        "signature": signature,
    });
    let entry = &mut doc["plugins"][name];
    if key.as_str().starts_with("darwin") {
        platform["format"] = json!("pkg");
        platform["min_os"] = json!("15.0");
        entry["install"]["au_component"] = json!(format!("{name}.component"));
        entry["install"]["macos_pkg_id"] = json!(format!("com.krateaudio.{lower}"));
    } else {
        platform["format"] = json!("inno-exe");
        entry["install"]["windows_uninstall_key"] =
            json!("{00000001-0000-4000-8000-000000000001}_is1");
    }
    entry["platforms"][key.as_str()] = platform;
}

#[test]
fn no_driver_off_linux() {
    assert!(driver_for(Os::Windows).is_none());
    assert!(driver_for(Os::Macos).is_none());
    assert!(
        NativeDrivers.driver_for(Os::Windows).is_none()
            && NativeDrivers.driver_for(Os::Macos).is_none()
    );
    let r = Release::of(ALPHA, V12);
    for (os, arch, key, running) in [
        (
            Os::Windows,
            Arch::X86_64,
            PlatformKey::WindowsX86_64,
            "10.0",
        ),
        (Os::Macos, Arch::Aarch64, PlatformKey::DarwinAarch64, "15.0"),
    ] {
        let fx = Fx::new();
        let doc = fx.doc_signed(&[(&r, fx.sign(&r))], |doc| {
            add_platform(&fx, doc, ALPHA, key)
        });
        fx.serve(doc, &[&r]);
        let facts = FakeHostFacts {
            os_version: OsVersion::parse(running),
            ..FakeHostFacts::default()
        };
        let seam = fx.seam();
        let (code, counts) = refused_row(&fx, |io| {
            let env = fx.env(os, arch, io, &NativeDrivers, &seam);
            for_command(env, ALPHA, V12, "install", NOW, &facts, &mut |_| {})
        });
        assert_eq!(
            (code, counts),
            ("E_INSTALL_NO_DRIVER", (1, 0)),
            "{os:?} install"
        );
        let (code, counts) = refused_row(&fx, |io| {
            let env = fx.env(os, arch, io, &NativeDrivers, &seam);
            remove_for_command(env, ALPHA, NOW, &mut |_| {})
        });
        assert_eq!(
            (code, counts),
            ("E_INSTALL_NO_DRIVER", (1, 0)),
            "{os:?} remove"
        );
    }
}

/// The native wiring: `status::build`, `native_env` with the seam set here (PD-30).
fn native_fx(running: &str) -> (Fx, FakeHostFacts) {
    let fx = Fx::new();
    let r = Release::of(ALPHA, V12);
    let key = native_key();
    let doc = fx.doc_signed(&[(&r, fx.sign(&r))], |doc| {
        if key != PlatformKey::LinuxX86_64 {
            add_platform(&fx, doc, ALPHA, key);
        }
    });
    fx.serve(doc, &[&r]);
    let facts = FakeHostFacts {
        os_version: OsVersion::parse(running),
        ..FakeHostFacts::default()
    };
    (fx, facts)
}

// Only the Windows and macOS no-driver tests call these two; on Linux they would be dead code.
#[cfg(any(windows, target_os = "macos"))]
fn native_commands(
    fx: &Fx,
    facts: &FakeHostFacts,
) -> (
    Result<ActionSummary, InstallError>,
    Result<ActionSummary, InstallError>,
) {
    let seam = fx.seam();
    let extract = fx.rig.extract_env(&FsExtractIo);
    let mut env = native_env(fx.ctx(), &FsInstallIo, extract, fx.rig.download_env());
    env.request_seam = Some(&seam);
    let install = for_command(env, ALPHA, V12, "install", NOW, facts, &mut |_| {});
    let remove = remove_for_command(env, ALPHA, NOW, &mut |_| {});
    (install, remove)
}

#[cfg(any(windows, target_os = "macos"))]
fn offers_nothing(running: &str) {
    let (fx, facts) = native_fx(running);
    let before = (
        snap(&fx.base.join("plugins")),
        snap(&fx.base.join("presets")),
    );
    let view = status::build(fx.ctx(), FetchMode::Normal, NOW, &facts);
    assert!(!view.plugins.is_empty());
    assert!(
        view.plugins.iter().all(|p| p.actions.is_empty()),
        "every card []"
    );
    let (install, remove) = native_commands(&fx, &facts);
    assert_eq!(code_of(&install), "E_INSTALL_NO_DRIVER");
    assert_eq!(code_of(&remove), "E_INSTALL_NO_DRIVER");
    assert_eq!(
        (
            snap(&fx.base.join("plugins")),
            snap(&fx.base.join("presets"))
        ),
        before
    );
}

#[cfg(windows)]
#[test]
fn native_wiring_offers_nothing_windows() {
    offers_nothing("10.0");
}

#[cfg(target_os = "macos")]
#[test]
fn native_wiring_offers_nothing_macos() {
    offers_nothing("15.0");
}

#[cfg(target_os = "linux")]
#[test]
fn native_wiring_installs_linux() {
    let (fx, facts) = native_fx("2.99");
    let view = status::build(fx.ctx(), FetchMode::Normal, NOW, &facts);
    assert_eq!(
        card(&view, ALPHA).actions,
        vec![status::ActionView::Install]
    );
    let seam = fx.seam();
    let mut env = native_env(
        fx.ctx(),
        &FsInstallIo,
        fx.rig.extract_env(&FsExtractIo),
        fx.rig.download_env(),
    );
    env.request_seam = Some(&seam);
    for_command(env, ALPHA, V12, "install", NOW, &facts, &mut |_| {}).expect("installs");
    let view = status::build(fx.ctx(), FetchMode::Normal, NOW, &facts);
    assert_eq!(card(&view, ALPHA).actions, vec![status::ActionView::Remove]);
}

// ---- SC-020 to SC-022 ----------------------------------------------------------------------------------------------

fn panic_text(payload: Box<dyn std::any::Any + Send>) -> String {
    payload
        .downcast_ref::<String>()
        .cloned()
        .or_else(|| payload.downcast_ref::<&str>().map(|s| (*s).to_owned()))
        .unwrap_or_default()
}

#[test]
fn sandbox_guard_panics() {
    let fx = Fx::new();
    fx.offer(&[&Release::of(ALPHA, V12)]);
    let outside = fx
        .base
        .parent()
        .expect("the temp dir")
        .join("km9-never-created");
    // (a) the context fold.
    {
        let roots = fx.ctx().roots.clone();
        crate::settings::save(
            &roots,
            &Settings {
                linux_plugin_root: Some(outside.clone()),
                ..Settings::default()
            },
        )
        .expect("settings");
        let ctx = fx.ctx().clone();
        let result = catch_unwind(AssertUnwindSafe(|| {
            crate::context::assemble_with_os(
                Os::Linux,
                Ok(roots),
                &ctx.feed_url,
                ctx.http.clone(),
                ctx.signing_key.clone(),
            )
        }));
        let payload = result.expect_err("the fold panics");
        assert!(panic_text(payload).contains("outside the test sandbox"));
        fs::remove_file(fx.state().join(crate::settings::SETTINGS_FILE))
            .expect("restore the settings");
    }
    // (b) an unfolded context whose Linux root lies outside: the destinations panic before any InstallIo call.
    {
        let mut ctx = fx.ctx().clone();
        ctx.settings.linux_plugin_root = Some(outside.clone());
        let rig = InstallRig::new(ctx);
        let rec = RecordingInstallIo::fail_closed();
        let seam = fx.seam();
        let before = fx.counts();
        let result = catch_unwind(AssertUnwindSafe(|| {
            let mut env =
                rig.command_env(Os::Linux, Arch::X86_64, &rec, &FsExtractIo, &NativeDrivers);
            env.request_seam = Some(&seam);
            for_command(env, ALPHA, V12, "install", NOW, &host(), &mut |_| {})
        }));
        let after = fx.counts();
        let payload = result.expect_err("check_sandboxed panics");
        assert!(panic_text(payload).contains("outside the test sandbox"));
        assert_eq!(
            rec.calls(),
            Vec::new(),
            "no InstallIo call, read or mutating"
        );
        assert_eq!((after.0 - before.0, after.1 - before.1), (1, 0));
    }
    // The guard taken before the panic was released on unwind.
    let r = fx.act(&FsInstallIo, ALPHA, V12, "install");
    assert!(!matches!(r, Err(InstallError::Busy { .. })), "{r:?}");
}

#[test]
fn artifact_released_after_success() {
    let r = Release::of(ALPHA, V12);
    let hex = hex_lower(&sha(&r.archive()));
    let downloads = |fx: &Fx| fx.ctx().roots.staging_dir().join(DOWNLOADS_DIR).join(&hex);
    let extract_empty = |fx: &Fx| {
        absent(&fx.extract())
            || fs::read_dir(fx.extract())
                .expect("extract")
                .next()
                .is_none()
    };
    {
        let fx = Fx::new();
        fx.offer(&[&r]);
        fx.install(ALPHA, V12);
        assert!(absent(&downloads(&fx)), "the artifact is released");
        assert!(extract_empty(&fx), "the staged tree is released");
    }
    {
        let fx = Fx::new();
        fx.offer(&[&r]);
        let faulty =
            FaultyInstallIo::new(&FsInstallIo).fail(IKind::CopyNew, 1, io::ErrorKind::StorageFull);
        assert_eq!(
            code_of(&fx.act(&faulty, ALPHA, V12, "install")),
            "E_INSTALL_FULL"
        );
        assert!(!absent(&downloads(&fx)), "the artifact stays for a retry");
        assert!(extract_empty(&fx), "the staged tree is released");
    }
    // PD-3: the discard fails; the action stays Ok and is recorded.
    let n = {
        let fx = Fx::new();
        fx.offer(&[&r]);
        fx.install(ALPHA, V12);
        fx.rig.staging.calls_of(Op::RemoveDir)
    };
    install_capture();
    let _ = take_mine();
    let mut fx = Fx::new();
    fx.rig.staging = FaultyStagingIo::failing(Op::RemoveDir, n, io::ErrorKind::PermissionDenied);
    fx.offer(&[&r]);
    fx.install(ALPHA, V12);
    let warns: Vec<String> = take_mine()
        .into_iter()
        .filter(|(t, l, m)| t == LOG_TARGET && *l == Level::Warn && m.contains("E_STAGING_DENIED"))
        .map(|(_, _, m)| m)
        .collect();
    assert_eq!(warns.len(), 1, "{warns:?}");
    assert!(matches!(
        detect::read_manifest(&fx.state(), ALPHA, ALPHA),
        detect::RecordRead::Found(_)
    ));
}

#[test]
fn logs_one_line_per_action() {
    install_capture();
    let _ = take_mine();
    let fx = Fx::new();
    fx.offer(&[&Release::of(ALPHA, V12)]);
    fx.install(ALPHA, V12);
    fx.offer(&[&Release::of(ALPHA, V13)]);
    fx.update(ALPHA, V13);
    assert_eq!(
        code_of(&fx.act(&FsInstallIo, ALPHA, V13, "update")),
        "E_INSTALL_NOT_NEWER"
    );
    let mine: Vec<(Level, String)> = take_mine()
        .into_iter()
        .filter(|(t, _, _)| t == LOG_TARGET)
        .map(|(_, l, m)| (l, m))
        .collect();
    let infos: Vec<&String> = mine
        .iter()
        .filter(|(l, _)| *l == Level::Info)
        .map(|(_, m)| m)
        .collect();
    assert_eq!(infos.len(), 3, "{infos:?}");
    for (line, (version, action, outcome)) in infos.iter().zip([
        (V12, "install", "ok"),
        (V13, "update", "ok"),
        (V13, "update", "E_INSTALL_NOT_NEWER"),
    ]) {
        for part in [ALPHA, version, action, outcome] {
            assert!(line.contains(part), "{line:?} names {part}");
        }
    }
    let raw = text(&fx.base);
    let canonical = fs::canonicalize(&fx.base)
        .map(|p| text(&p))
        .unwrap_or_default();
    for (_, m) in &mine {
        assert!(
            !m.contains(&raw) && (canonical.is_empty() || !m.contains(&canonical)),
            "a path in {m:?}"
        );
    }
}

// ---- SC-027 --------------------------------------------------------------------------------------------------------

#[test]
fn commands_run_over_the_driver() {
    let fx = Fx::new();
    fx.offer(&[&Release::of(ALPHA, V12)]);
    let kept = presets_kept(&["Alpha/Night Air.vstpreset"]);
    let recording = RecordingDriver::new(Os::Linux, kept.clone());
    let drivers = OneDriver(Some(&recording));
    let seam = fx.seam();
    let env = fx.env(Os::Linux, Arch::X86_64, &FsInstallIo, &drivers, &seam);
    let install =
        for_command(env, ALPHA, V12, "install", NOW, &host(), &mut |_| {}).expect("install");
    assert_eq!(install.notice, Some(Notice::RescanDaw));
    fx.install(ALPHA, V12);
    fx.offer(&[&Release::of(ALPHA, V13)]);
    let update = for_command(env, ALPHA, V13, "update", NOW, &host(), &mut |_| {}).expect("update");
    assert_eq!(update.notice, Some(Notice::RescanDaw));
    let before = fx.counts();
    let remove = remove_for_command(env, ALPHA, NOW, &mut |_| {}).expect("remove");
    let after = fx.counts();
    assert_eq!(remove.notice, None);
    assert_eq!(remove.kept, kept, "the driver's kept");
    assert_eq!(after.1, before.1, "Remove makes no asset request");
    assert_eq!(
        recording.batches(),
        vec![
            vec![(ActionKind::Install, ALPHA.to_owned())],
            vec![(ActionKind::Update, ALPHA.to_owned())],
            vec![(ActionKind::Remove, ALPHA.to_owned())],
        ]
    );
}

#[test]
fn batch_runs_every_action() {
    let fx = Fx::new();
    let (alpha, beta) = (Release::of(ALPHA, V12), Release::of(BETA, V12));
    let (ea, aa) = fx.artifact(&alpha);
    let (eb, ab) = fx.artifact(&beta);
    let faulty =
        FaultyInstallIo::new(&FsInstallIo).fail(IKind::CopyNew, 1, io::ErrorKind::StorageFull);
    let results = fx.drive(
        &faulty,
        &FsExtractIo,
        vec![
            install_of(&ea, &aa),
            install_of(&eb, &ab),
            uninstall_of(&ea),
        ],
    );
    let shape: Vec<(String, ActionKind)> =
        results.iter().map(|r| (r.name.clone(), r.action)).collect();
    assert_eq!(
        shape,
        vec![
            (ALPHA.to_owned(), ActionKind::Install),
            (BETA.to_owned(), ActionKind::Install),
            (ALPHA.to_owned(), ActionKind::Remove),
        ]
    );
    assert_eq!(
        results[0].outcome,
        Err(InstallError::Full {
            step: Step::BundleStaged
        })
    );
    assert!(results[1].outcome.is_ok(), "{:?}", results[1].outcome);
    assert_eq!(code_of(&results[2].outcome), "E_INSTALL_NOT_MANAGED");
    assert_eq!(
        detect::read_manifest(&fx.state(), BETA, BETA),
        detect::RecordRead::Found(detect::InstallerRecord::Manifest {
            version_text: V12.to_owned(),
            version: CoreVersion::parse(V12)
        })
    );
    assert!(
        absent(&fx.target(ALPHA)) && absent(&fx.user().join(ALPHA)),
        "nothing of Alpha"
    );
}

// ---- the Unix-only rows --------------------------------------------------------------------------------------------

#[cfg(unix)]
#[test]
fn non_utf8_root_refused_unix() {
    use std::ffi::OsStr;
    use std::os::unix::ffi::OsStrExt as _;
    let fx = Fx::new();
    let roots =
        crate::test_support::temp_roots_with_user_presets(&fx.base, OsStr::from_bytes(b"u\xff"));
    let mut ctx = fx.ctx().clone();
    ctx.roots = roots;
    let rig = InstallRig::new(ctx);
    let (e, a) = fx.artifact(&Release::of(ALPHA, V12));
    let rec = RecordingInstallIo::fail_closed();
    let mut results = LinuxDriver.run(
        InstallEnv {
            ctx: &rig.ctx,
            io: &rec,
            extract: rig.extract_env(&FsExtractIo),
            progress: &mut |_| {},
        },
        vec![install_of(&e, &a)],
    );
    assert_eq!(
        results.remove(0).outcome,
        Err(InstallError::PathNotUtf8 { which: "presets" })
    );
    assert_eq!(rec.mutating_calls(), 0);
}

#[cfg(unix)]
#[test]
fn symlinked_plugin_root_unix() {
    let (td, roots) = fresh_temp_roots();
    let base = roots.state_dir().parent().expect("base").to_path_buf();
    let real = base.join("vst3-real");
    fs::create_dir_all(&real).expect("real");
    std::os::unix::fs::symlink(&real, base.join("vst3-link")).expect("the linked root");
    let fx = Fx::over(td, roots, Some("vst3-link"));
    let (v12, v13) = (Release::of(ALPHA, V12), Release::of(ALPHA, V13));
    fx.offer(&[&v12]);
    fx.install(ALPHA, V12);
    assert_eq!(snap(&real), v12.plugin_rows());
    fx.offer(&[&v13]);
    let rec = RecordingInstallIo::forwarding(&FsInstallIo);
    fx.act(&rec, ALPHA, V13, "update")
        .expect("update, not ROOT_CHANGED");
    assert_eq!(snap(&real), v13.plugin_rows());
    for c in rec.calls() {
        let paths: Vec<PathBuf> = match c {
            IOp::CopyNew { dest, .. } => vec![dest],
            IOp::Rename { from, to } => vec![from, to],
            _ => continue,
        };
        for p in paths {
            let name = p.to_string_lossy().into_owned();
            if name.contains(".vst3.krate-") {
                let sibling = p
                    .ancestors()
                    .find(|a| {
                        a.file_name().is_some_and(|n| {
                            n.to_string_lossy().ends_with(NEW_SUFFIX)
                                || n.to_string_lossy().ends_with(OLD_SUFFIX)
                        })
                    })
                    .expect("a sibling");
                assert_eq!(
                    sibling.parent(),
                    Some(base.join("vst3-link").as_path()),
                    "{p:?}"
                );
            }
        }
    }
    fx.remove(&FsInstallIo, ALPHA)
        .expect("remove, not ROOT_CHANGED");
    assert_eq!(snap(&real), Vec::new());
    assert_eq!(
        fs::read_link(base.join("vst3-link")).expect("still the link"),
        real
    );
}
