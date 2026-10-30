use super::*;
use crate::test_support::fresh_temp_roots;
use std::ffi::OsString;
use std::fs;
use std::path::{Component, MAIN_SEPARATOR_STR};
use std::time::SystemTime;

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
