use super::*;
use crate::platform::{NativePlatform, OsEnv, Platform};
use crate::roots::{PluginRoot, RootKind};
use crate::settings::SETTINGS_FILE;
use crate::state_lock::{LOCK_FILE, LockError};
use crate::test_support::{canonical_temp_dir, fresh_temp_roots};
use log::{Level, LevelFilter, Log, Metadata, Record};
use std::fs;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::{Path, PathBuf};
use std::sync::{Mutex, PoisonError};
use std::thread::{self, ThreadId};

/// Never contacted: nothing in this module opens a socket.
const FEED: &str = "http://127.0.0.1:9/versions.json";
const CORRUPT: &[u8] = b"{ this is not json";

/// Recursive listing of `dir`: (relative path, is_dir, size in bytes), sorted. A directory's
/// size is recorded as 0: its `len()` is filesystem-dependent.
fn listing(dir: &Path) -> Vec<(PathBuf, bool, u64)> {
    fn walk(root: &Path, dir: &Path, out: &mut Vec<(PathBuf, bool, u64)>) {
        for entry in fs::read_dir(dir).expect("read_dir") {
            let entry = entry.expect("dir entry");
            let path = entry.path();
            let meta = fs::symlink_metadata(&path).expect("symlink_metadata");
            let rel = path.strip_prefix(root).expect("strip_prefix").to_path_buf();
            let size = if meta.is_dir() { 0 } else { meta.len() };
            out.push((rel, meta.is_dir(), size));
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

/// Entries of `after` that `before` lacks, and entries of `before` that `after` lacks.
type Entries = Vec<(PathBuf, bool, u64)>;
fn diff(before: &Entries, after: &Entries) -> (Entries, Entries) {
    let added = after
        .iter()
        .filter(|e| !before.contains(e))
        .cloned()
        .collect();
    let removed = before
        .iter()
        .filter(|e| !after.contains(e))
        .cloned()
        .collect();
    (added, removed)
}

/// The canonical temp base `fresh_temp_roots` built the roots under.
fn base_of(roots: &Roots) -> PathBuf {
    roots
        .state_dir()
        .parent()
        .expect("the state dir has a parent")
        .to_path_buf()
}

/// A v1 `settings.json` naming `root` (JSON-escaped, so Windows backslashes survive).
fn plant_settings(roots: &Roots, root: Option<&Path>) {
    let value = match root {
        Some(p) => serde_json::to_string(p.to_str().expect("UTF-8 test path")).expect("encode"),
        None => "null".to_owned(),
    };
    let text = format!(
        "{{\"schema_version\": 1, \"linux_plugin_root\": {value}, \"linux_graphics_workaround\": false}}"
    );
    fs::write(roots.state_dir().join(SETTINGS_FILE), text).expect("plant settings.json");
}

fn plant_corrupt(roots: &Roots) -> PathBuf {
    let path = roots.state_dir().join(SETTINGS_FILE);
    fs::write(&path, CORRUPT).expect("plant a corrupt settings.json");
    path
}

fn roots_error() -> RootsError {
    NativePlatform
        .default_roots(&OsEnv::from_pairs([]))
        .expect_err("an empty OsEnv cannot give roots")
}

fn ctx(state: &AppState) -> &AppContext {
    state.context.as_ref().expect("an Ok context")
}

fn panic_message(payload: Box<dyn std::any::Any + Send>) -> String {
    payload
        .downcast_ref::<String>()
        .cloned()
        .or_else(|| payload.downcast_ref::<&str>().map(|s| (*s).to_owned()))
        .expect("a string panic payload")
}

#[test]
fn assemble_cases() {
    // (a) No roots: no lock, no settings, the D9 variable named.
    let e = roots_error();
    let state = assemble_context(Err(e), FEED);
    #[cfg(windows)]
    let first_var = "LOCALAPPDATA";
    #[cfg(not(windows))]
    let first_var = "HOME";
    match &state.context {
        Err(err @ StartupError::Roots(RootsError::Env { var })) => {
            assert_eq!(*var, first_var);
            assert_eq!(err.code(), "E_STARTUP_ROOTS");
        }
        other => panic!("expected Roots(Env), got {other:?}"),
    }
    assert!(state.settings_error.is_none());
    assert!(state.lock.is_none());

    // (b) Ok roots over an existing state dir: exactly one new file, state/manager.lock.
    {
        let (_td, roots) = fresh_temp_roots();
        let base = base_of(&roots);
        let before = listing(&base);
        let state = assemble_context(Ok(roots.clone()), FEED);
        let after = listing(&base);
        let c = ctx(&state);
        assert_eq!(c.roots, roots, "no Configured entry, roots equal the input");
        assert_eq!(c.settings, Settings::default());
        assert_eq!(c.feed_url, FEED);
        assert!(state.settings_error.is_none());
        assert!(state.lock.is_some());
        let (added, removed) = diff(&before, &after);
        assert!(removed.is_empty(), "nothing disappears: {removed:?}");
        assert_eq!(
            added,
            vec![(PathBuf::from("state").join(LOCK_FILE), false, 0)],
            "exactly state/manager.lock appears"
        );
    }

    // (b2) State dir removed first: exactly state/ and state/manager.lock appear.
    {
        let (_td, roots) = fresh_temp_roots();
        let base = base_of(&roots);
        fs::remove_dir_all(roots.state_dir()).expect("remove the state dir");
        let before = listing(&base);
        let state = assemble_context(Ok(roots.clone()), FEED);
        let after = listing(&base);
        assert!(state.context.is_ok(), "got {:?}", state.context);
        assert!(state.lock.is_some());
        let (added, removed) = diff(&before, &after);
        assert!(removed.is_empty(), "nothing disappears: {removed:?}");
        let dir = PathBuf::from("state");
        assert_eq!(
            added,
            vec![(dir.clone(), true, 0), (dir.join(LOCK_FILE), false, 0)],
            "exactly state/ and state/manager.lock appear"
        );
    }

    // (c) A corrupt settings.json: Ok, defaults, E_SETTINGS_CORRUPT, the file untouched.
    {
        let (_td, roots) = fresh_temp_roots();
        let path = plant_corrupt(&roots);
        let state = assemble_context(Ok(roots.clone()), FEED);
        assert_eq!(ctx(&state).settings, Settings::default());
        let error = state.settings_error.as_ref().expect("a settings error");
        assert_eq!(error.code(), "E_SETTINGS_CORRUPT");
        assert!(state.lock.is_some());
        assert_eq!(fs::read(&path).expect("read back"), CORRUPT);
    }
}

#[test]
fn assemble_locked_instance() {
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    let held = StateLock::acquire(&roots).expect("the first instance holds the lock");
    let path = plant_corrupt(&roots);
    let before = listing(&base);
    let state = assemble_context(Ok(roots.clone()), FEED);
    let after = listing(&base);
    match &state.context {
        Err(err @ StartupError::Locked) => assert_eq!(err.code(), "E_STARTUP_LOCKED"),
        other => panic!("expected Locked, got {other:?}"),
    }
    assert!(
        state.settings_error.is_none(),
        "settings were not loaded: {:?}",
        state.settings_error
    );
    assert!(state.lock.is_none());
    assert_eq!(fs::read(&path).expect("read back"), CORRUPT);
    assert_eq!(before, after, "no new file");
    drop(held);
}

#[test]
fn startup_notice_cases() {
    assert_eq!(LOCKED_EXIT_DELAY, std::time::Duration::from_secs(5));
    assert_eq!(LOCKED_NOTICE, "Krate Manager is already running");

    // Locked.
    {
        let (_td, roots) = fresh_temp_roots();
        let held = StateLock::acquire(&roots).expect("hold the lock");
        plant_corrupt(&roots);
        let state = assemble_context(Ok(roots.clone()), FEED);
        assert_eq!(
            startup_notice(&state),
            Some("Krate Manager is already running")
        );
        drop(held);
    }

    // Healthy.
    {
        let (_td, roots) = fresh_temp_roots();
        let state = assemble_context(Ok(roots.clone()), FEED);
        assert!(state.context.is_ok());
        assert_eq!(startup_notice(&state), None);
    }

    // Roots error.
    let state = assemble_context(Err(roots_error()), FEED);
    assert_eq!(startup_notice(&state), None);

    // E_STARTUP_LOCK_IO: a regular file where the state dir should be.
    {
        let (_td, roots) = fresh_temp_roots();
        fs::remove_dir_all(roots.state_dir()).expect("remove the state dir");
        fs::write(roots.state_dir(), b"not a directory").expect("plant a regular file");
        let state = assemble_context(Ok(roots.clone()), FEED);
        match &state.context {
            Err(
                err @ StartupError::LockIo {
                    op: LockStep::CreateDir,
                    ..
                },
            ) => assert_eq!(err.code(), "E_STARTUP_LOCK_IO"),
            other => panic!("expected LockIo at create-dir, got {other:?}"),
        }
        assert!(state.lock.is_none());
        assert_eq!(startup_notice(&state), None);
    }
}

#[test]
fn configured_root_cases() {
    let (_td, roots) = fresh_temp_roots();
    let x = base_of(&roots).join("configured");
    let unchanged = |r: &Roots| {
        assert_eq!(r.presets(), roots.presets());
        assert_eq!(r.state_dir(), roots.state_dir());
        assert_eq!(r.staging_dir(), roots.staging_dir());
    };

    // Linux with X: X first, the rest equal the input in order (Vst3User included).
    plant_settings(&roots, Some(&x));
    {
        let state = assemble_with_os(Os::Linux, Ok(roots.clone()), FEED);
        let c = ctx(&state);
        assert_eq!(
            c.roots.plugin_roots()[0],
            PluginRoot {
                kind: RootKind::Configured,
                path: x.clone(),
            }
        );
        assert_eq!(&c.roots.plugin_roots()[1..], roots.plugin_roots());
        assert_eq!(c.settings.linux_plugin_root.as_deref(), Some(x.as_path()));
        unchanged(&c.roots);
    }

    // Windows and macOS ignore the stored value.
    for os in [Os::Windows, Os::Macos] {
        let state = assemble_with_os(os, Ok(roots.clone()), FEED);
        assert_eq!(ctx(&state).roots, roots, "{os:?} ignores linux_plugin_root");
    }

    // Linux with null equals the input.
    plant_settings(&roots, None);
    {
        let state = assemble_with_os(Os::Linux, Ok(roots.clone()), FEED);
        assert_eq!(ctx(&state).roots, roots);
    }

    // X equal to the Vst3User path adds no entry.
    plant_settings(&roots, Some(roots.vst3_user()));
    {
        let state = assemble_with_os(Os::Linux, Ok(roots.clone()), FEED);
        let c = ctx(&state);
        assert_eq!(c.roots.plugin_roots(), roots.plugin_roots());
        unchanged(&c.roots);
    }
}

#[test]
fn reassemble_cases() {
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    let x = base.join("configured");
    let prev = assemble_with_os(Os::Linux, Ok(roots.clone()), FEED);
    let prev_lock = prev
        .lock
        .clone()
        .expect("the assembled state holds the lock");
    let before = listing(&base);

    // New settings: the Configured entry first, the new settings, the same URL and lock.
    let with_x = Settings {
        linux_plugin_root: Some(x.clone()),
        linux_graphics_workaround: true,
    };
    let next = reassemble_with_os(Os::Linux, &prev, with_x.clone());
    let c = ctx(&next);
    assert_eq!(
        c.roots.plugin_roots()[0],
        PluginRoot {
            kind: RootKind::Configured,
            path: x.clone(),
        }
    );
    assert_eq!(&c.roots.plugin_roots()[1..], roots.plugin_roots());
    assert_eq!(c.settings, with_x);
    assert_eq!(c.feed_url, ctx(&prev).feed_url);
    assert!(next.settings_error.is_none());
    let next_lock = next.lock.clone().expect("the lock is carried");
    assert!(Arc::ptr_eq(&prev_lock, &next_lock), "the same lock");
    assert!(
        matches!(StateLock::acquire(&roots), Err(LockError::Held)),
        "the lock is still held"
    );
    assert_eq!(listing(&base), before, "no I/O");

    // Back to None removes the entry.
    let cleared = reassemble_with_os(Os::Linux, &next, Settings::default());
    assert_eq!(ctx(&cleared).roots, roots);
    assert_eq!(ctx(&cleared).settings, Settings::default());

    // A relative root: the previous context unchanged, E_SETTINGS_INVALID.
    let relative = Settings {
        linux_plugin_root: Some(PathBuf::from("relative").join("dir")),
        linux_graphics_workaround: false,
    };
    let refused = reassemble_with_os(Os::Linux, &next, relative);
    assert_eq!(ctx(&refused), ctx(&next), "the previous context unchanged");
    assert_eq!(
        refused
            .settings_error
            .as_ref()
            .expect("a settings error")
            .code(),
        "E_SETTINGS_INVALID"
    );
    assert!(Arc::ptr_eq(
        &prev_lock,
        refused.lock.as_ref().expect("the lock is carried")
    ));
    assert_eq!(listing(&base), before, "no I/O");

    // An Err context comes back unchanged.
    let failed = assemble_context(Err(roots_error()), FEED);
    let again = reassemble_with_os(Os::Linux, &failed, with_x.clone());
    assert_eq!(
        format!("{:?}", again.context),
        format!("{:?}", failed.context)
    );
    assert!(again.settings_error.is_none());
    assert!(again.lock.is_none());

    // apply_settings swaps in the reassembled state; a later read sees it.
    drop(cleared);
    drop(refused);
    drop(next);
    let shared = SharedState::new(prev);
    apply_settings(&shared, with_x.clone());
    let guard = shared.read().unwrap_or_else(PoisonError::into_inner);
    let expected_roots = if NativePlatform.os() == Os::Linux {
        roots
            .with_configured_plugin_root(Some(&x))
            .expect("a normal configured root")
    } else {
        roots.clone()
    };
    assert_eq!(ctx(&guard).roots, expected_roots);
    assert_eq!(ctx(&guard).settings, with_x);
    assert!(Arc::ptr_eq(
        &prev_lock,
        guard.lock.as_ref().expect("the lock is carried")
    ));
    assert_eq!(listing(&base), before, "no I/O");
}

#[test]
fn sandbox_holds_through_assemble_and_reassemble() {
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    let outside = canonical_temp_dir().join("km-outside-sandbox");
    assert!(!outside.exists());
    let outside_state = |entries: Entries| -> Entries {
        entries
            .into_iter()
            .filter(|(p, _, _)| !p.starts_with("state"))
            .collect()
    };
    let before = outside_state(listing(&base));

    // (a) Through assemble_with_os reading a planted settings.json.
    plant_settings(&roots, Some(&outside));
    let result = catch_unwind(AssertUnwindSafe(|| {
        assemble_with_os(Os::Linux, Ok(roots.clone()), FEED)
    }));
    let message = panic_message(result.expect_err("an outside configured root panics"));
    assert!(
        message.contains("outside the test sandbox"),
        "message: {message}"
    );

    // (b) Through reassemble_with_os.
    plant_settings(&roots, None);
    let prev = assemble_with_os(Os::Linux, Ok(roots.clone()), FEED);
    assert!(prev.context.is_ok());
    let result = catch_unwind(AssertUnwindSafe(|| {
        reassemble_with_os(
            Os::Linux,
            &prev,
            Settings {
                linux_plugin_root: Some(outside.clone()),
                ..Settings::default()
            },
        )
    }));
    let message = panic_message(result.expect_err("an outside configured root panics"));
    assert!(
        message.contains("outside the test sandbox"),
        "message: {message}"
    );

    assert_eq!(outside_state(listing(&base)), before);
    assert!(!outside.exists());
}

/// Records on `STARTUP_LOG_TARGET`: (thread, level, message).
struct Capture {
    records: Mutex<Vec<(ThreadId, Level, String)>>,
}

impl Log for Capture {
    fn enabled(&self, _metadata: &Metadata) -> bool {
        true
    }

    fn log(&self, record: &Record) {
        if record.target() == STARTUP_LOG_TARGET {
            self.records
                .lock()
                .unwrap_or_else(PoisonError::into_inner)
                .push((
                    thread::current().id(),
                    record.level(),
                    record.args().to_string(),
                ));
        }
    }

    fn flush(&self) {}
}

static CAPTURE: Capture = Capture {
    records: Mutex::new(Vec::new()),
};

/// Removes and returns this thread's captured records.
fn take_mine() -> Vec<(Level, String)> {
    let me = thread::current().id();
    let mut all = CAPTURE
        .records
        .lock()
        .unwrap_or_else(PoisonError::into_inner);
    let (mine, rest): (Vec<_>, Vec<_>) = all.drain(..).partition(|r| r.0 == me);
    *all = rest;
    mine.into_iter()
        .map(|(_, level, msg)| (level, msg))
        .collect()
}

fn assert_one_error(records: &[(Level, String)], code: &str) {
    assert_eq!(records.len(), 1, "one record: {records:?}");
    assert_eq!(records[0].0, Level::Error);
    assert!(records[0].1.contains(code), "{code} in {:?}", records[0].1);
}

#[test]
fn startup_errors_are_logged() {
    log::set_logger(&CAPTURE).expect("the only test that installs a logger");
    log::set_max_level(LevelFilter::Trace);
    take_mine();

    // Roots error.
    log_startup_errors(&assemble_context(Err(roots_error()), FEED));
    assert_one_error(&take_mine(), "E_STARTUP_ROOTS");

    // Locked.
    {
        let (_td, roots) = fresh_temp_roots();
        let held = StateLock::acquire(&roots).expect("hold the lock");
        log_startup_errors(&assemble_context(Ok(roots.clone()), FEED));
        assert_one_error(&take_mine(), "E_STARTUP_LOCKED");
        drop(held);
    }

    // Corrupt settings.
    {
        let (_td, roots) = fresh_temp_roots();
        plant_corrupt(&roots);
        log_startup_errors(&assemble_context(Ok(roots.clone()), FEED));
        assert_one_error(&take_mine(), "E_SETTINGS_CORRUPT");
    }

    // Healthy: nothing.
    {
        let (_td, roots) = fresh_temp_roots();
        let state = assemble_context(Ok(roots.clone()), FEED);
        assert!(state.context.is_ok() && state.settings_error.is_none());
        log_startup_errors(&state);
        let records = take_mine();
        assert!(records.is_empty(), "no record: {records:?}");
    }
}

#[test]
fn parse_dev_feed_url_cases() {
    assert_eq!(
        parse_dev_feed_url("http://127.0.0.1:8080/versions.json\n"),
        Some("http://127.0.0.1:8080/versions.json".to_owned())
    );
    assert!(parse_dev_feed_url("https://example.invalid/v.json").is_some());
    let over = format!("http://h/{}", "a".repeat(2048));
    assert!(over.len() > 2048);
    for refused in [
        "",
        "   ",
        "ftp://x/y",
        "file:///x",
        "not a url",
        "http://",
        "http://a/one\nhttp://b/two",
        over.as_str(),
    ] {
        assert_eq!(parse_dev_feed_url(refused), None, "{refused:?}");
    }
}
