use super::*;
use crate::context::{AppState, assemble_with_os};
use crate::download::{DOWNLOADS_DIR, any_in_flight, hold_in_flight_for_test};
use crate::extract::FsExtractIo;
use crate::feed::PlatformKey;
use crate::fixture_server::{FixtureServer, HoldGate, Reply};
use crate::http::HttpConfig;
use crate::install::{self, FsInstallIo, NativeDrivers};
use crate::platform::{NativePlatform, Platform};
use crate::roots::RootKind;
use crate::self_update::InFlightGuard;
use crate::tar_builder_tests::{Tar, gz, literal_payload, moduleinfo, sha};
use crate::test_signer::TestSigner;
use crate::test_support::{
    FakeHostFacts, InstallRig, TestPlugin, fresh_temp_roots, kat_signing_key, test_feed_document,
};
use crate::verify::{SigningKey, hex_lower};
use crate::version::{Arch, OsVersion};
use std::ffi::OsString;
use std::fs;
use std::path::{Component, MAIN_SEPARATOR_STR};
use std::sync::{PoisonError, mpsc};
use std::thread;
use std::time::{Duration, SystemTime};

const D10_DEFAULT: &[u8] = b"{\n  \"schema_version\": 1,\n  \"linux_plugin_root\": null,\n  \"linux_graphics_workaround\": false\n}\n";

/// Every `SettingsStep` this OS runs (FR-034: six on Windows, seven on Unix).
const STEPS: &[SettingsStep] = &[
    SettingsStep::ReadExisting,
    SettingsStep::CreateDir,
    SettingsStep::CreateTemp,
    SettingsStep::Write,
    SettingsStep::SyncTemp,
    SettingsStep::Persist,
    #[cfg(unix)]
    SettingsStep::SyncDir,
];

/// An absolute, normal path that is never touched (SC-009's per-OS value).
fn custom_root() -> PathBuf {
    #[cfg(windows)]
    let p = PathBuf::from("C:\\vst");
    #[cfg(not(windows))]
    let p = PathBuf::from("/home/u/custom");
    p
}

fn custom_settings() -> Settings {
    Settings {
        linux_plugin_root: Some(custom_root()),
        linux_graphics_workaround: true,
    }
}

/// Recursive listing of `dir`: (relative path, is_dir, size in bytes), sorted.
fn listing(dir: &Path) -> Vec<(PathBuf, bool, u64)> {
    fn walk(root: &Path, dir: &Path, out: &mut Vec<(PathBuf, bool, u64)>) {
        for entry in fs::read_dir(dir).expect("read_dir") {
            let entry = entry.expect("dir entry");
            let path = entry.path();
            let meta = fs::symlink_metadata(&path).expect("symlink_metadata");
            let rel = path.strip_prefix(root).expect("strip_prefix").to_path_buf();
            out.push((rel, meta.is_dir(), meta.len()));
            if meta.is_dir() {
                walk(root, &path, out);
            }
        }
    }
    let mut out = Vec::new();
    walk(dir, dir, &mut out);
    out.sort();
    out
}

fn canonical_base(roots: &Roots) -> PathBuf {
    roots
        .state_dir()
        .parent()
        .expect("the state dir has a parent")
        .to_path_buf()
}

/// `<base><sep>a<sep>..<sep>b`, spelled as text so a `\\?\` base keeps the `..`.
fn dotdot_path(base: &Path) -> PathBuf {
    let mut s = OsString::from(base.as_os_str());
    for part in ["a", "..", "b"] {
        s.push(MAIN_SEPARATOR_STR);
        s.push(part);
    }
    let p = PathBuf::from(s);
    assert!(p.components().any(|c| c == Component::ParentDir));
    p
}

fn fs_root(base: &Path) -> PathBuf {
    base.ancestors()
        .last()
        .expect("a path has ancestors")
        .to_path_buf()
}

fn json_str(p: &Path) -> String {
    serde_json::to_string(p.to_str().expect("test paths are UTF-8")).expect("json string")
}

fn settings_path(roots: &Roots) -> PathBuf {
    roots.state_dir().join(SETTINGS_FILE)
}

fn bytes_and_mtime(path: &Path) -> (Vec<u8>, SystemTime) {
    let bytes = fs::read(path).expect("read settings.json");
    let mtime = fs::metadata(path)
        .expect("metadata")
        .modified()
        .expect("mtime");
    (bytes, mtime)
}

fn stray_temps(dir: &Path) -> usize {
    fs::read_dir(dir)
        .expect("read_dir")
        .filter(|e| {
            e.as_ref()
                .expect("dir entry")
                .file_name()
                .to_string_lossy()
                .starts_with(".settings.json.")
        })
        .count()
}

fn v1_file(root: &str, workaround: &str) -> String {
    format!(
        "{{\"schema_version\": 1, \"linux_plugin_root\": {root}, \"linux_graphics_workaround\": {workaround}}}"
    )
}

#[test]
fn round_trip_values() {
    for value in [Settings::default(), custom_settings()] {
        let (_td, roots) = fresh_temp_roots();
        save(&roots, &value).expect("save");
        let out = load(&roots);
        assert!(out.error.is_none(), "{:?}", out.error);
        assert_eq!(out.settings, value);
    }
}

#[test]
fn written_bytes_are_d10() {
    let (_td, roots) = fresh_temp_roots();
    save(&roots, &Settings::default()).expect("save");
    assert_eq!(fs::read(settings_path(&roots)).expect("read"), D10_DEFAULT);
    assert_eq!(encode(&Settings::default()).expect("encode"), D10_DEFAULT);
}

#[test]
fn missing_file_gives_defaults_and_writes_nothing() {
    let (_td, roots) = fresh_temp_roots();
    let before = listing(roots.state_dir());
    let out = load(&roots);
    assert_eq!(out.settings, Settings::default());
    assert!(out.error.is_none(), "{:?}", out.error);
    assert_eq!(listing(roots.state_dir()), before);
}

#[test]
fn plugin_root_or_default_uses_vst3_user() {
    let (_td, roots) = fresh_temp_roots();
    assert_eq!(
        Settings::default().linux_plugin_root_or_default(&roots),
        roots.vst3_user()
    );
    let custom = custom_settings();
    assert_eq!(
        custom.linux_plugin_root_or_default(&roots),
        custom_root().as_path()
    );
}

#[test]
fn load_rejections_keep_defaults_and_file() {
    let (_td, roots) = fresh_temp_roots();
    let base = canonical_base(&roots);
    let cases: Vec<(String, &str)> = vec![
        (
            "{\"schema_version\": 2, \"linux_plugin_root\": null, \"linux_graphics_workaround\": false}"
                .to_owned(),
            "E_SETTINGS_FUTURE_VERSION",
        ),
        (
            "{\"schema_version\": 0, \"linux_plugin_root\": null, \"linux_graphics_workaround\": false}"
                .to_owned(),
            "E_SETTINGS_INVALID",
        ),
        (
            "{\"schema_version\": -1, \"linux_plugin_root\": null, \"linux_graphics_workaround\": false}"
                .to_owned(),
            "E_SETTINGS_INVALID",
        ),
        (v1_file("\"relative/dir\"", "false"), "E_SETTINGS_INVALID"),
        (v1_file("\"\"", "false"), "E_SETTINGS_INVALID"),
        (
            v1_file(&json_str(&dotdot_path(&base)), "false"),
            "E_SETTINGS_INVALID",
        ),
        (
            v1_file(&json_str(&fs_root(&base)), "false"),
            "E_SETTINGS_INVALID",
        ),
        ("not json".to_owned(), "E_SETTINGS_CORRUPT"),
        ("[1, 2]".to_owned(), "E_SETTINGS_CORRUPT"),
        (v1_file("null", "\"yes\""), "E_SETTINGS_CORRUPT"),
        (
            "{\"schema_version\": 1, \"linux_plugin_root\": null, \"linux_graphics_workaround\": false, \"install_scope\": \"user\"}"
                .to_owned(),
            "E_SETTINGS_CORRUPT",
        ),
        (
            "{\"linux_plugin_root\": null, \"linux_graphics_workaround\": false}".to_owned(),
            "E_SETTINGS_CORRUPT",
        ),
    ];
    let path = settings_path(&roots);
    for (text, code) in cases {
        fs::write(&path, &text).expect("plant settings.json");
        let before = bytes_and_mtime(&path);
        let out = load(&roots);
        assert_eq!(out.settings, Settings::default(), "{text}");
        let err = out.error.unwrap_or_else(|| panic!("no error for {text}"));
        assert_eq!(err.code(), code, "{text}: {err}");
        assert_eq!(bytes_and_mtime(&path), before, "{text}");
    }
}

#[test]
fn save_rejects_invalid_values_and_writes_nothing() {
    #[cfg(unix)]
    let not_utf8 = {
        use std::os::unix::ffi::OsStrExt;
        PathBuf::from(std::ffi::OsStr::from_bytes(b"/x\xff"))
    };
    #[cfg(windows)]
    let not_utf8 = {
        use std::os::windows::ffi::OsStringExt;
        PathBuf::from(OsString::from_wide(&[0xD800]))
    };
    let (_td, roots) = fresh_temp_roots();
    let base = canonical_base(&roots);
    fs::remove_dir_all(roots.state_dir()).expect("remove the state dir");
    let cases = [
        (not_utf8, "E_SETTINGS_PATH_NOT_UTF8"),
        (PathBuf::from("relative/dir"), "E_SETTINGS_INVALID"),
        (PathBuf::new(), "E_SETTINGS_INVALID"),
        (dotdot_path(&base), "E_SETTINGS_INVALID"),
        (fs_root(&base), "E_SETTINGS_INVALID"),
    ];
    let before = listing(&base);
    for (root, code) in cases {
        let value = Settings {
            linux_plugin_root: Some(root.clone()),
            linux_graphics_workaround: true,
        };
        let err = save(&roots, &value).expect_err("an invalid value is refused");
        assert_eq!(err.code(), code, "{}: {err}", root.display());
        assert_eq!(listing(&base), before, "{}", root.display());
        assert!(!roots.state_dir().exists());
    }
}

#[test]
fn save_refuses_future_version_file() {
    let planted = [
        "{\"schema_version\": 2, \"linux_plugin_root\": null, \"linux_graphics_workaround\": false}",
        "{\"schema_version\": 2, \"linux_plugin_root\": null, \"linux_graphics_workaround\": false, \"install_scope\": \"user\"}",
    ];
    for text in planted {
        let (_td, roots) = fresh_temp_roots();
        let path = settings_path(&roots);
        fs::write(&path, text).expect("plant settings.json");
        let before = bytes_and_mtime(&path);
        let listed = listing(roots.state_dir());
        let err = save(&roots, &custom_settings()).expect_err("a newer file is refused");
        assert!(
            matches!(err, SettingsError::FutureVersion { found: 2 }),
            "{text}: {err}"
        );
        assert_eq!(err.code(), "E_SETTINGS_FUTURE_VERSION");
        assert_eq!(bytes_and_mtime(&path), before, "{text}");
        assert_eq!(listing(roots.state_dir()), listed, "{text}");
    }
}

#[test]
fn save_overwrites_corrupt_and_invalid_files() {
    let saved = custom_settings();
    let expected = encode(&saved).expect("encode");
    let planted: [Option<&str>; 3] = [
        Some("not json"),
        Some(
            "{\"schema_version\": 0, \"linux_plugin_root\": null, \"linux_graphics_workaround\": false}",
        ),
        Some(std::str::from_utf8(D10_DEFAULT).expect("utf8")),
    ];
    for text in planted.into_iter().chain([None]) {
        let (_td, roots) = fresh_temp_roots();
        match text {
            Some(t) => fs::write(settings_path(&roots), t).expect("plant settings.json"),
            None => {
                fs::remove_dir_all(roots.state_dir()).expect("remove the state dir");
                fs::create_dir(roots.state_dir()).expect("an empty state dir");
            }
        }
        save(&roots, &saved).unwrap_or_else(|e| panic!("{text:?}: {e}"));
        assert_eq!(
            fs::read(settings_path(&roots)).expect("read"),
            expected,
            "{text:?}"
        );
        assert_eq!(stray_temps(roots.state_dir()), 0, "{text:?}");
    }
}

#[derive(Clone, Copy)]
enum Mode {
    Fail,
    AbortAfter,
}

/// Delegates to `StdSettingsIo` except at `at` (PD-9): `Fail` errors without running the
/// step (a held temp is dropped, so removed); `AbortAfter` runs it, keeps any temp it
/// holds as a crashed process would leave it, then errors.
struct FaultIo {
    at: SettingsStep,
    mode: Mode,
}

fn injected(step: SettingsStep) -> io::Error {
    io::Error::other(format!("injected fault at {}", step.as_str()))
}

impl FaultIo {
    fn plain<T>(&self, step: SettingsStep, real: impl FnOnce() -> io::Result<T>) -> io::Result<T> {
        if step != self.at {
            return real();
        }
        match self.mode {
            Mode::Fail => Err(injected(step)),
            Mode::AbortAfter => {
                real()?;
                Err(injected(step))
            }
        }
    }

    fn holding(
        &self,
        step: SettingsStep,
        real: impl FnOnce() -> io::Result<NamedTempFile>,
    ) -> io::Result<NamedTempFile> {
        if step != self.at {
            return real();
        }
        match self.mode {
            Mode::Fail => Err(injected(step)),
            Mode::AbortAfter => {
                real()?.keep().expect("keep the temp file");
                Err(injected(step))
            }
        }
    }
}

impl SettingsIo for FaultIo {
    fn read_existing(&mut self, path: &Path) -> io::Result<Option<Vec<u8>>> {
        self.plain(SettingsStep::ReadExisting, || {
            StdSettingsIo.read_existing(path)
        })
    }
    fn create_dir(&mut self, dir: &Path) -> io::Result<()> {
        self.plain(SettingsStep::CreateDir, || StdSettingsIo.create_dir(dir))
    }
    fn create_temp(&mut self, dir: &Path) -> io::Result<NamedTempFile> {
        self.holding(SettingsStep::CreateTemp, || StdSettingsIo.create_temp(dir))
    }
    fn write(&mut self, tmp: NamedTempFile, bytes: &[u8]) -> io::Result<NamedTempFile> {
        self.holding(SettingsStep::Write, || StdSettingsIo.write(tmp, bytes))
    }
    fn sync_temp(&mut self, tmp: NamedTempFile) -> io::Result<NamedTempFile> {
        self.holding(SettingsStep::SyncTemp, || StdSettingsIo.sync_temp(tmp))
    }
    fn persist(&mut self, tmp: NamedTempFile, target: &Path) -> io::Result<()> {
        self.plain(SettingsStep::Persist, || StdSettingsIo.persist(tmp, target))
    }
    #[cfg(unix)]
    fn sync_dir(&mut self, dir: &Path) -> io::Result<()> {
        self.plain(SettingsStep::SyncDir, || StdSettingsIo.sync_dir(dir))
    }
}

fn assert_io_at(err: &SettingsError, step: SettingsStep) {
    assert_eq!(err.code(), "E_SETTINGS_IO", "{err}");
    assert!(
        matches!(err, SettingsError::Io { op, .. } if *op == step),
        "{err}"
    );
    assert!(err.to_string().contains(step.as_str()), "{err}");
}

#[test]
fn fault_at_each_step_keeps_previous_file() {
    let previous = custom_settings();
    let new = Settings::default();
    for &at in STEPS {
        let (_td, roots) = fresh_temp_roots();
        let path = settings_path(&roots);
        save(&roots, &previous).expect("save the previous file");
        let prev_bytes = fs::read(&path).expect("read");
        let listed = listing(roots.state_dir());
        let mut io = FaultIo {
            at,
            mode: Mode::Fail,
        };
        let err = save_with(&mut io, &roots, &new).expect_err("the fault surfaces");
        assert_io_at(&err, at);
        let now = fs::read(&path).expect("read");
        if at == SettingsStep::SyncDir {
            assert_eq!(now, encode(&new).expect("encode"));
        } else {
            assert_eq!(now, prev_bytes, "{}", at.as_str());
        }
        if at == SettingsStep::ReadExisting {
            assert_eq!(listing(roots.state_dir()), listed);
        }
        assert_eq!(stray_temps(roots.state_dir()), 0, "{}", at.as_str());
    }
}

#[test]
fn abort_leaves_previous_file_and_stray_temp_is_ignored() {
    let previous = custom_settings();
    for at in [SettingsStep::Write, SettingsStep::SyncTemp] {
        let (_td, roots) = fresh_temp_roots();
        let path = settings_path(&roots);
        save(&roots, &previous).expect("save the previous file");
        let prev_bytes = fs::read(&path).expect("read");
        let mut io = FaultIo {
            at,
            mode: Mode::AbortAfter,
        };
        let err = save_with(&mut io, &roots, &Settings::default()).expect_err("the abort surfaces");
        assert_io_at(&err, at);
        assert_eq!(
            fs::read(&path).expect("read"),
            prev_bytes,
            "{}",
            at.as_str()
        );
        assert_eq!(stray_temps(roots.state_dir()), 1, "{}", at.as_str());
        let out = load(&roots);
        assert!(out.error.is_none(), "{:?}", out.error);
        assert_eq!(out.settings, previous);
    }
}

// ---- Phase 12: the settings commands and the root gate (plan §7.1 SC-011, PD-9 to PD-11) ---------------------------

/// The feed URL of every state here that serves no feed: never contacted.
const NO_FEED: &str = "http://127.0.0.1:9/versions.json";
const FEED_ROUTE: &str = "/versions.json";
const NOW: u64 = 1_793_404_800;
const ALPHA: &str = "Alpha";
const BETA: &str = "Beta";
const V12: &str = "1.2.0";
const ASSET: &str = "Alpha-1.2.0-Linux-x64.tar.gz";
const WAIT: Duration = Duration::from_secs(5);
const BUSY_TEXT: &str =
    "E_SETTINGS_BUSY: an install, download or self-update is running; try again when it finishes";

/// The settings file's bytes (`None` when absent) and the running context: what a refusal leaves unchanged.
type Snapshot = (Option<Vec<u8>>, AppContext);

/// Temp roots and a state assembled over them with `Os::Linux` forced (its lock taken in the temp state dir).
struct Fx {
    roots: Roots,
    base: PathBuf,
    state: SharedState,
    _td: tempfile::TempDir,
}

impl Fx {
    fn new() -> Fx {
        Fx::with(NO_FEED, kat_signing_key(), |_| {})
    }

    /// `plant` runs on the fresh roots before the assembly.
    fn planted(plant: impl FnOnce(&Roots)) -> Fx {
        Fx::with(NO_FEED, kat_signing_key(), plant)
    }

    fn with(feed_url: &str, signing_key: SigningKey, plant: impl FnOnce(&Roots)) -> Fx {
        let (td, roots) = fresh_temp_roots();
        plant(&roots);
        let http = HttpConfig::loopback(WAIT, Duration::from_secs(30));
        let state = SharedState::new(assemble_with_os(
            Os::Linux,
            Ok(roots.clone()),
            feed_url,
            http,
            signing_key,
        ));
        let base = canonical_base(&roots);
        Fx {
            roots,
            base,
            state,
            _td: td,
        }
    }

    fn state_dir(&self) -> &Path {
        self.roots.state_dir()
    }

    fn ctx(&self) -> AppContext {
        self.state
            .read()
            .unwrap_or_else(PoisonError::into_inner)
            .context
            .clone()
            .expect("an Ok context")
    }

    fn file(&self) -> Option<Vec<u8>> {
        match fs::read(settings_path(&self.roots)) {
            Ok(bytes) => Some(bytes),
            Err(e) if e.kind() == io::ErrorKind::NotFound => None,
            Err(e) => panic!("read settings.json: {e}"),
        }
    }

    fn snapshot(&self) -> Snapshot {
        (self.file(), self.ctx())
    }

    fn assert_unchanged(&self, before: &Snapshot, label: &str) {
        assert_eq!(self.file(), before.0, "{label}: the settings file");
        assert_eq!(self.ctx(), before.1, "{label}: the running context");
    }

    fn save(
        &self,
        os: Os,
        root: Option<&Path>,
        graphics: bool,
    ) -> Result<SettingsView, SettingsCommandError> {
        save_for_command_with_os(os, &self.state, root.map(text), graphics)
    }

    fn plant_manifest(&self, stem: &str, body: &str) {
        let dir = self.state_dir().join(crate::detect::MANIFESTS_DIR);
        fs::create_dir_all(&dir).expect("the manifests dir");
        fs::write(dir.join(format!("{stem}.json")), body).expect("plant a manifest");
    }

    fn plant_journal(&self, stem: &str) {
        let dir = self.state_dir().join(install::JOURNAL_DIR);
        fs::create_dir_all(&dir).expect("the journal dir");
        fs::write(dir.join(format!("{stem}.json")), b"{}").expect("plant a journal");
    }
}

fn text(p: &Path) -> String {
    p.to_str().expect("test paths are UTF-8").to_owned()
}

/// The Phase 4 D10 bytes of `written_bytes_are_d10` for any value, spelled out (not through `encode`).
fn d10(root: Option<&Path>, graphics: bool) -> Vec<u8> {
    let root = root.map_or_else(|| "null".to_owned(), json_str);
    format!(
        "{{\n  \"schema_version\": 1,\n  \"linux_plugin_root\": {root},\n  \"linux_graphics_workaround\": {graphics}\n}}\n"
    )
    .into_bytes()
}

/// A Phase 6 manifest head of `plugin` (only the keys `manifest_heads` reads).
fn head(plugin: &str) -> String {
    format!("{{\"manifest_version\": 1, \"plugin\": \"{plugin}\", \"version\": \"{V12}\"}}")
}

fn has_configured(ctx: &AppContext, root: &Path) -> bool {
    ctx.roots
        .plugin_roots()
        .iter()
        .any(|r| r.kind == RootKind::Configured && r.path == root)
}

fn failed_state() -> SharedState {
    SharedState::new(AppState {
        context: Err(StartupError::Locked),
        settings_error: None,
        lock: None,
    })
}

/// A root change and a toggle-only save, each `E_SETTINGS_BUSY` with nothing changed.
fn assert_busy(fx: &Fx, label: &str) {
    let before = fx.snapshot();
    for root in [Some(fx.base.join("elsewhere")), None] {
        let err = fx
            .save(Os::Linux, root.as_deref(), true)
            .expect_err("a save while something runs is refused");
        assert!(
            matches!(err, SettingsCommandError::Busy),
            "{label}, {root:?}: {err}"
        );
        assert_eq!(err.code(), "E_SETTINGS_BUSY");
        assert_eq!(err.to_string(), BUSY_TEXT);
        fx.assert_unchanged(&before, label);
    }
}

#[test]
fn save_for_command_cases() {
    assert_eq!(d10(None, false), D10_DEFAULT, "the spelled-out D10 form");

    // A valid root: written with the D10 bytes, the context re-derived, the view returned.
    let fx = Fx::new();
    let root = fx.base.join("custom-vst3");
    let view = fx
        .save(Os::Linux, Some(&root), true)
        .expect("a valid root saves");
    assert_eq!(
        view,
        SettingsView {
            os: OsName::Linux,
            linux_plugin_root: text(&root),
            root_is_default: false,
            graphics_workaround: true,
            root_locked_by: Vec::new(),
            error: None,
        }
    );
    assert_eq!(fx.file(), Some(d10(Some(&root), true)));
    let ctx = fx.ctx();
    assert_eq!(
        ctx.settings.linux_plugin_root.as_deref(),
        Some(root.as_path())
    );
    assert!(
        has_configured(&ctx, &root),
        "{:?}",
        ctx.roots.plugin_roots()
    );

    // `Use default` (no root): the default bytes, the configured root gone.
    let view = fx.save(Os::Linux, None, false).expect("the default saves");
    assert!(view.root_is_default);
    assert_eq!(view.linux_plugin_root, text(fx.roots.vst3_user()));
    assert_eq!(fx.file().as_deref(), Some(D10_DEFAULT));
    assert!(
        !fx.ctx()
            .roots
            .plugin_roots()
            .iter()
            .any(|r| r.kind == RootKind::Configured)
    );

    // Invalid roots (host-appropriate: relative, `..`, the filesystem root, empty): nothing changes.
    for bad in [
        PathBuf::from("rel").join("vst3"),
        dotdot_path(&fx.base),
        fs_root(&fx.base),
        PathBuf::new(),
    ] {
        let before = fx.snapshot();
        let err = fx
            .save(Os::Linux, Some(&bad), true)
            .expect_err("an invalid root is refused");
        assert_eq!(err.code(), "E_SETTINGS_INVALID", "{}: {err}", bad.display());
        assert!(err.to_string().starts_with("E_SETTINGS_INVALID: "), "{err}");
        fx.assert_unchanged(&before, &bad.display().to_string());
    }

    // A manifest (one unparsable, named by its stem) and a journal of the same plugin: the root is locked, sorted and
    // de-duplicated; the toggle still saves.
    let fx = Fx::new();
    fx.plant_manifest(BETA, "not json");
    fx.plant_manifest(ALPHA, &head(ALPHA));
    fx.plant_journal(ALPHA);
    let before = fx.snapshot();
    let err = fx
        .save(Os::Linux, Some(&fx.base.join("other")), false)
        .expect_err("a root change is refused while plugins are installed");
    assert!(
        matches!(&err, SettingsCommandError::RootInUse { names } if names == &[ALPHA, BETA]),
        "{err}"
    );
    assert_eq!(err.code(), "E_SETTINGS_ROOT_IN_USE");
    assert_eq!(
        err.to_string(),
        "E_SETTINGS_ROOT_IN_USE: Remove Alpha, Beta before changing the plug-in folder"
    );
    fx.assert_unchanged(&before, "manifests");
    let view = fx
        .save(Os::Linux, None, true)
        .expect("the toggle alone saves");
    assert!(view.graphics_workaround);
    assert_eq!(view.root_locked_by, [ALPHA, BETA]);
    assert_eq!(fx.file(), Some(d10(None, true)));

    // A journal alone locks the root.
    let fx = Fx::new();
    fx.plant_journal(BETA);
    let before = fx.snapshot();
    let err = fx
        .save(Os::Linux, Some(&fx.base.join("other")), false)
        .expect_err("a pending journal locks the root");
    assert!(
        matches!(&err, SettingsCommandError::RootInUse { names } if names == &[BETA]),
        "{err}"
    );
    fx.assert_unchanged(&before, "journal");

    // Busy, in flight, self-updating: every save refused; once released, the root change goes through.
    let fx = Fx::new();
    let downloads = fx.roots.staging_dir().join(DOWNLOADS_DIR);
    {
        let _held = install::hold_busy_for_test(fx.state_dir(), ALPHA);
        assert_busy(&fx, "busy key");
    }
    {
        let _held = hold_in_flight_for_test(&downloads);
        assert_busy(&fx, "download in flight");
    }
    {
        let _held = InFlightGuard::enter(Some(fx.state_dir()));
        assert_busy(&fx, "self-update");
    }
    let other = fx.base.join("other");
    fx.save(Os::Linux, Some(&other), false)
        .expect("nothing runs any more");
    assert!(has_configured(&fx.ctx(), &other));

    // A newer file is never overwritten.
    let fx = Fx::new();
    fs::write(
        settings_path(&fx.roots),
        "{\"schema_version\": 2, \"linux_plugin_root\": null, \"linux_graphics_workaround\": false}",
    )
    .expect("plant a newer settings.json");
    let before = fx.snapshot();
    let err = fx
        .save(Os::Linux, Some(&fx.base.join("other")), true)
        .expect_err("a newer file is refused");
    assert_eq!(err.code(), "E_SETTINGS_FUTURE_VERSION", "{err}");
    assert!(err.to_string().starts_with("E_SETTINGS_FUTURE_VERSION: "));
    fx.assert_unchanged(&before, "future version");

    // Windows and macOS forced: settings exist on Linux only.
    let fx = Fx::new();
    for os in [Os::Windows, Os::Macos] {
        let before = fx.snapshot();
        let err = fx
            .save(os, Some(&fx.base.join("other")), true)
            .expect_err("off Linux every save is refused");
        assert!(matches!(err, SettingsCommandError::UnsupportedOs), "{err}");
        assert_eq!(err.code(), "E_SETTINGS_UNSUPPORTED_OS");
        assert_eq!(
            err.to_string(),
            "E_SETTINGS_UNSUPPORTED_OS: settings exist on Linux only"
        );
        fx.assert_unchanged(&before, &format!("{os:?}"));
    }

    // The native wrapper: this OS's rule.
    let native = save_for_command(&fx.state, None, true);
    match NativePlatform.os() {
        Os::Linux => {
            native.expect("Linux saves");
        }
        Os::Windows | Os::Macos => {
            let err = native.expect_err("off Linux refused");
            assert_eq!(err.code(), "E_SETTINGS_UNSUPPORTED_OS");
        }
    }

    // No startup context: the startup error, code first (SI-15).
    let err = save_for_command_with_os(Os::Linux, &failed_state(), None, true)
        .expect_err("no context, no save");
    assert_eq!(err.code(), "E_STARTUP_LOCKED");
    assert!(err.to_string().starts_with("E_STARTUP_LOCKED: "), "{err}");
}

#[test]
fn view_cases() {
    // Stored `None`: the default `~/.vst3` under the temp home.
    let fx = Fx::new();
    assert!(fx.roots.vst3_user().starts_with(&fx.base));
    let view = view_with_os(Os::Linux, &fx.state).expect("a view");
    assert_eq!(
        view,
        SettingsView {
            os: OsName::Linux,
            linux_plugin_root: text(fx.roots.vst3_user()),
            root_is_default: true,
            graphics_workaround: false,
            root_locked_by: Vec::new(),
            error: None,
        }
    );

    // A manifest and a journal present: named by the view alone, no save made.
    fx.plant_manifest(ALPHA, &head(ALPHA));
    fx.plant_journal(BETA);
    let view = view_with_os(Os::Linux, &fx.state).expect("a view");
    assert_eq!(view.root_locked_by, [ALPHA, BETA]);
    assert_eq!(fx.file(), None, "the view writes nothing");

    // Off Linux the view is still returned, naming the OS; the native wrapper names this one.
    for (os, name) in [(Os::Windows, OsName::Windows), (Os::Macos, OsName::Macos)] {
        assert_eq!(view_with_os(os, &fx.state).expect("a view").os, name);
    }
    let native = match NativePlatform.os() {
        Os::Windows => OsName::Windows,
        Os::Macos => OsName::Macos,
        Os::Linux => OsName::Linux,
    };
    assert_eq!(super::view(&fx.state).expect("a view").os, native);

    // Over 256 manifest entries: the bounded read fails and the root is named locked by "manifests".
    let fx = Fx::new();
    for n in 0..=install::MAX_MANIFEST_ENTRIES {
        fx.plant_manifest(&format!("m{n:03}"), &head(ALPHA));
    }
    let view = view_with_os(Os::Linux, &fx.state).expect("a view");
    assert_eq!(view.root_locked_by, ["manifests"]);

    // A stored root and toggle at assembly.
    let stored = Fx::planted(|roots| {
        let root = canonical_base(roots).join("stored");
        fs::write(settings_path(roots), d10(Some(&root), true)).expect("plant settings.json");
    });
    let root = stored.base.join("stored");
    let view = view_with_os(Os::Linux, &stored.state).expect("a view");
    assert_eq!(view.linux_plugin_root, text(&root));
    assert!(!view.root_is_default);
    assert!(view.graphics_workaround);
    assert_eq!(view.error, None);

    // A corrupt or newer file at assembly: the defaults, and the startup error's text, code first (SI-17).
    for (body, code) in [
        ("{ this is not json", "E_SETTINGS_CORRUPT"),
        (
            "{\"schema_version\": 2, \"linux_plugin_root\": null, \"linux_graphics_workaround\": true}",
            "E_SETTINGS_FUTURE_VERSION",
        ),
    ] {
        let fx = Fx::planted(|roots| {
            fs::write(settings_path(roots), body).expect("plant settings.json");
        });
        let view = view_with_os(Os::Linux, &fx.state).expect("a view");
        let error = view.error.clone().expect("the startup settings error");
        assert!(error.starts_with(&format!("{code}: ")), "{error}");
        assert!(view.root_is_default, "{code}");
        assert!(!view.graphics_workaround, "{code}");
    }

    // No startup context: the startup error.
    let err = view_with_os(Os::Linux, &failed_state()).expect_err("no context, no view");
    assert_eq!(err.code(), "E_STARTUP_LOCKED");
}

/// A minimal Linux release of `Alpha` 1.2.0 (the bundle, the two release text files, an empty `presets/`): the gzip'd archive and its C4 listing, sorted by path.
fn alpha_release() -> (Vec<u8>, Vec<String>) {
    let mut tar = Tar::new();
    tar.dir("Alpha.vst3")
        .dir("Alpha.vst3/Contents")
        .dir("Alpha.vst3/Contents/Resources")
        .file(
            "Alpha.vst3/Contents/Resources/moduleinfo.json",
            0o644,
            &moduleinfo(V12),
        )
        .dir("Alpha.vst3/Contents/x86_64-linux")
        .file(
            "Alpha.vst3/Contents/x86_64-linux/Alpha.so",
            0o644,
            b"Alpha 1.2.0 shared object",
        )
        .file("CHANGELOG.txt", 0o644, b"changes")
        .file("README.txt", 0o644, b"read me")
        .dir("presets");
    // Already in path order (the C4 listing is sorted by path, not by line).
    let lines: Vec<String> = [
        "d 0755 Alpha.vst3",
        "d 0755 Alpha.vst3/Contents",
        "d 0755 Alpha.vst3/Contents/Resources",
        "f 0644 Alpha.vst3/Contents/Resources/moduleinfo.json",
        "d 0755 Alpha.vst3/Contents/x86_64-linux",
        "f 0644 Alpha.vst3/Contents/x86_64-linux/Alpha.so",
        "f 0644 CHANGELOG.txt",
        "f 0644 README.txt",
        "d 0755 presets",
    ]
    .map(str::to_owned)
    .into();
    (gz(&tar.finish()), lines)
}

/// A contract-valid feed offering `archive` as `Alpha` 1.2.0 for `linux-x86_64`, signed by `signer`.
fn alpha_feed(signer: &TestSigner, archive: &[u8], lines: &[String]) -> Vec<u8> {
    let signature = signer.sign_c3(archive, ALPHA, V12, PlatformKey::LinuxX86_64, ASSET);
    let doc = test_feed_document(&[TestPlugin {
        name: ALPHA,
        version: V12,
        key: PlatformKey::LinuxX86_64,
        asset: ASSET,
        size: archive.len() as u64,
        sha256: sha(archive),
        signature,
        min_os: None,
    }]);
    let mut doc: serde_json::Value = serde_json::from_slice(&doc).expect("JSON");
    let payload = literal_payload(lines);
    doc["plugins"][ALPHA]["platforms"]["linux-x86_64"]["payload"] =
        serde_json::json!({ "files": payload.files, "sha256": hex_lower(&payload.sha256) });
    serde_json::to_vec(&doc).expect("serialises")
}

#[test]
fn root_gate_cases() {
    // (1) A plugin command holds its ticket from before its context clone until it returns: an install whose feed
    // request is held (its busy key not yet taken) makes a root-changing save `E_SETTINGS_BUSY`; released, it
    // finishes into the configured root and records it, after which the same save is `E_SETTINGS_ROOT_IN_USE`. The
    // artifact is staged as a verified cached download, so the install makes no asset request.
    let gate = HoldGate::new();
    let signer = TestSigner::new();
    let (archive, lines) = alpha_release();
    let server = FixtureServer::start(vec![(
        FEED_ROUTE.to_owned(),
        Reply::Held {
            gate: gate.clone(),
            reply: Box::new(Reply::Ok {
                body: alpha_feed(&signer, &archive, &lines),
                content_type: "application/json",
            }),
        },
    )]);
    let fx = Fx::with(&server.url(FEED_ROUTE), signer.key(), |_| {});
    let cached = fx
        .roots
        .staging_dir()
        .join(DOWNLOADS_DIR)
        .join(hex_lower(&sha(&archive)));
    fs::create_dir_all(&cached).expect("the cached download dir");
    fs::write(cached.join(ASSET), &archive).expect("the cached download");
    let other = fx.base.join("other");
    let before = fx.snapshot();
    let (held, busy_while_held, refused, after_refusal, installed) = thread::scope(|s| {
        let worker = s.spawn(|| {
            let (ticket, ctx) = command_context(&fx.state).expect("a context");
            let rig = InstallRig::new(ctx);
            let env = rig.command_env(
                Os::Linux,
                Arch::X86_64,
                &FsInstallIo,
                &FsExtractIo,
                &NativeDrivers,
            );
            let host = FakeHostFacts {
                os_version: OsVersion::parse("2.99"),
                ..FakeHostFacts::default()
            };
            let result = install::for_command(env, ALPHA, V12, "install", NOW, &host, &mut |_| {});
            drop(ticket);
            result
        });
        let held = gate.wait_open(1, WAIT);
        let busy_while_held = install::any_busy(fx.state_dir());
        let refused = fx.save(Os::Linux, Some(&other), false);
        let after_refusal = fx.snapshot();
        gate.release_all();
        let installed = worker.join().expect("the install thread does not panic");
        (held, busy_while_held, refused, after_refusal, installed)
    });
    assert!(held, "the install's feed request is held");
    assert!(
        !busy_while_held,
        "the window between the context clone and the busy key"
    );
    let err = refused.expect_err("a save while a ticket is held is refused");
    assert!(matches!(err, SettingsCommandError::Busy), "{err}");
    assert_eq!(after_refusal, before, "file and context unchanged");
    installed.expect("the install finishes");
    let plugin_root = fx.roots.vst3_user();
    assert!(plugin_root.join("Alpha.vst3").join("Contents").is_dir());
    let manifest = install::read_manifest_full(&FsInstallIo, fx.state_dir(), ALPHA, ALPHA)
        .expect("a readable manifest")
        .expect("the install's manifest");
    assert_eq!(manifest.plugin_root, text(plugin_root));
    let err = fx
        .save(Os::Linux, Some(&other), false)
        .expect_err("the installed plugin locks the root");
    assert!(
        matches!(&err, SettingsCommandError::RootInUse { names } if names == &[ALPHA]),
        "{err}"
    );
    fx.assert_unchanged(&before, "after the install");
    let paths: Vec<String> = server.requests().into_iter().map(|r| r.path).collect();
    assert_eq!(paths, [FEED_ROUTE], "one feed request, no asset request");
    drop(server);

    // (2) A save in progress: a second save is `E_SETTINGS_BUSY` (saves never interleave), and a plugin command's
    // `command_context` waits it out, returning once it ends.
    let fx = Fx::new();
    let held = hold_save_gate_for_test(fx.state_dir());
    let before = fx.snapshot();
    let second = fx.save(Os::Linux, None, true);
    let (early, late) = thread::scope(|s| {
        let (tx, rx) = mpsc::channel();
        let state = &fx.state;
        s.spawn(move || tx.send(command_context(state)).expect("the receiver waits"));
        let early = rx.recv_timeout(Duration::from_millis(300));
        drop(held);
        let late = rx.recv_timeout(WAIT);
        (early, late)
    });
    let err = second.expect_err("a save during a save is refused");
    assert!(matches!(err, SettingsCommandError::Busy), "{err}");
    fx.assert_unchanged(&before, "a save during a save");
    assert!(
        matches!(early, Err(mpsc::RecvTimeoutError::Timeout)),
        "command_context returned during a save"
    );
    let (ticket, ctx) = late
        .expect("command_context returns once the save ends")
        .expect("a context");
    assert_eq!(ctx, before.1);
    drop(ticket);
    fx.save(Os::Linux, None, true)
        .expect("the gate is free again");

    // (3) Tickets held, nothing busy and nothing in flight: a save is still `E_SETTINGS_BUSY` until the last drops.
    let fx = Fx::new();
    let first = command_context(&fx.state).expect("a context");
    let second = command_context(&fx.state).expect("two commands share the gate");
    assert!(!install::any_busy(fx.state_dir()));
    assert!(!any_in_flight(&fx.roots.staging_dir().join(DOWNLOADS_DIR)));
    assert_busy(&fx, "two tickets");
    drop(first);
    assert_busy(&fx, "one ticket");
    drop(second);
    fx.save(Os::Linux, Some(&fx.base.join("other")), false)
        .expect("no ticket held");

    // No startup context: no ticket, the startup error.
    let Err(err) = command_context(&failed_state()) else {
        panic!("no context, yet a ticket");
    };
    assert_eq!(err.code(), "E_STARTUP_LOCKED");
}
