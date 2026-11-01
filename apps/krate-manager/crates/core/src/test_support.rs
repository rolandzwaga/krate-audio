//! Temp-sandboxed roots for tests (FR-020, FR-022). Compiled only under
//! `cfg(any(test, feature = "test-support"))`.

use crate::context::AppContext;
use crate::detect::{HostFacts, InnoValues, PkgOutputs, RecordRead};
use crate::download::{
    CancelToken, DownloadEnv, DownloadPolicy, EntryKind, FsStagingIo, PartialFile, StagedEntry,
    StagingIo,
};
use crate::extract::{
    ArchiveRead, EntryType, ExtractFile, ExtractIo, FsExtractIo, ListingEntry, NodeKind,
};
use crate::feed::{self, Feed, PlainName, PlatformKey};
use crate::platform::{HostIo, NativePlatform, Platform, RegistryRead, RunOutcome};
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, Roots, STAGING_DIR};
use crate::verify::{SigningKey, hex_lower};
use crate::version::{self, Arch, OsVersion};
use log::{Level, LevelFilter, Log, Metadata, Record};
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::collections::btree_map::Entry;
use std::ffi::OsString;
use std::io::{self, Read};
use std::path::{Component, Path, PathBuf};
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock, PoisonError};
use std::thread::{self, ThreadId};
use std::time::Duration;

#[derive(Debug, thiserror::Error)]
pub enum GuardError {
    #[error("cannot canonicalize {which}: {source}")]
    Canonicalize {
        which: &'static str,
        #[source]
        source: std::io::Error,
    },
    #[error("{} is not strictly under the temp root {}", .base.display(), .temp_root.display())]
    Outside { base: PathBuf, temp_root: PathBuf },
}

/// The plug-in roots `temp_roots_in` makes under `<base>/plugins` (D8 kinds; AU on macOS only).
const PLUGIN_DIRS: &[(RootKind, &str)] = &[
    (RootKind::Vst3User, "vst3-user"),
    (RootKind::Vst3Global, "vst3-global"),
    #[cfg(target_os = "macos")]
    (RootKind::AuUser, "au-user"),
    #[cfg(target_os = "macos")]
    (RootKind::AuGlobal, "au-global"),
];

/// `std::env::temp_dir()` canonicalized; panics if canonicalization fails.
pub fn canonical_temp_dir() -> PathBuf {
    #[expect(
        clippy::disallowed_methods,
        reason = "the one temp-dir read, for the test guard"
    )]
    let raw = std::env::temp_dir();
    std::fs::canonicalize(&raw)
        .unwrap_or_else(|e| panic!("cannot canonicalize the temp dir {}: {e}", raw.display()))
}

/// The canonical `base` when it is a strict descendant of the canonical `temp_root`.
/// Reads metadata only; creates nothing.
pub fn check_under_temp(base: &Path, temp_root: &Path) -> Result<PathBuf, GuardError> {
    let canonical_base =
        std::fs::canonicalize(base).map_err(|source| GuardError::Canonicalize {
            which: "base",
            source,
        })?;
    let canonical_temp =
        std::fs::canonicalize(temp_root).map_err(|source| GuardError::Canonicalize {
            which: "temp_root",
            source,
        })?;
    if canonical_base != canonical_temp && canonical_base.starts_with(&canonical_temp) {
        Ok(canonical_base)
    } else {
        Err(GuardError::Outside {
            base: canonical_base,
            temp_root: canonical_temp,
        })
    }
}

/// The guard first (panic before any directory exists), then `roots_at` the canonical base.
fn temp_roots_in(base: &Path, temp_root: &Path) -> Roots {
    let canonical_base = match check_under_temp(base, temp_root) {
        Ok(b) => b,
        Err(e) => panic!(
            "temp_roots refused: base {} is not under temp root {}: {e}",
            base.display(),
            temp_root.display()
        ),
    };
    roots_at(&canonical_base)
}

/// Every root as a subdirectory of `base` as spelled, one `join` per component (PD-6),
/// sandboxed to `base` (PD-24), created. The caller has checked `base`.
fn roots_at(base: &Path) -> Roots {
    let plugins = base.join("plugins");
    let presets = base.join("presets");
    let state_dir = base.join("state");
    let paths = RootPaths {
        plugin_roots: PLUGIN_DIRS
            .iter()
            .map(|&(kind, dir)| PluginRoot {
                kind,
                path: plugins.join(dir),
            })
            .collect(),
        presets: PresetRoots {
            factory: presets.join("factory"),
            user: presets.join("user"),
        },
        staging_dir: state_dir.join(STAGING_DIR),
        state_dir,
    };
    #[expect(
        clippy::disallowed_methods,
        reason = "temp_roots is the test constructor"
    )]
    let roots = Roots::new(paths)
        .expect("temp_roots paths are valid by construction")
        .sandboxed(base.to_path_buf());
    let dirs = roots
        .plugin_roots()
        .iter()
        .map(|r| r.path.as_path())
        .chain([
            roots.presets().factory.as_path(),
            roots.presets().user.as_path(),
            roots.state_dir(),
            roots.staging_dir(),
        ]);
    for dir in dirs {
        std::fs::create_dir_all(dir)
            .unwrap_or_else(|e| panic!("temp_roots cannot create {}: {e}", dir.display()));
    }
    roots
}

/// FR-089: roots spelled through a symlinked parent, `<canonical td>/via -> real`, sandboxed to
/// `<canonical td>/via`; `td` is checked first (panic before anything is created).
#[cfg(unix)]
pub fn temp_roots_via_link(td: &Path) -> Roots {
    let canonical = check_under_temp(td, &canonical_temp_dir())
        .unwrap_or_else(|e| panic!("temp_roots_via_link refused: {e}"));
    let real = canonical.join("real");
    std::fs::create_dir(&real)
        .unwrap_or_else(|e| panic!("temp_roots_via_link cannot create {}: {e}", real.display()));
    let via = canonical.join("via");
    std::os::unix::fs::symlink("real", &via)
        .unwrap_or_else(|e| panic!("temp_roots_via_link cannot link {}: {e}", via.display()));
    roots_at(&via)
}

/// FR-089: roots spelled on `td` exactly as `TempDir::new()` returned it (the raw temp-dir
/// spelling), sandboxed to `td`; the canonical form is checked first.
#[cfg(target_os = "macos")]
pub fn temp_roots_raw(td: &Path) -> Roots {
    if let Err(e) = check_under_temp(td, &canonical_temp_dir()) {
        panic!("temp_roots_raw refused: {e}");
    }
    roots_at(td)
}

/// Roots under `base`, which must be a strict descendant of the system temp dir.
pub fn temp_roots(base: &Path) -> Roots {
    temp_roots_in(base, &canonical_temp_dir())
}

/// A new `TempDir` and `temp_roots` on it; keep the `TempDir` alive while the roots are used.
pub fn fresh_temp_roots() -> (tempfile::TempDir, Roots) {
    let td = tempfile::TempDir::new().expect("cannot create a TempDir");
    let roots = temp_roots(td.path());
    (td, roots)
}

/// The crate's `log` targets all start with this; the capture logger records only those.
const CAPTURE_TARGET_PREFIX: &str = "krate_manager_core";

/// One captured record per `log` call: (thread, target, level, message).
type CapturedRecord = (ThreadId, String, Level, String);

/// The shared capture store (FR-088). The only `log::set_logger` in the workspace's tests.
struct Capture {
    records: Mutex<Vec<CapturedRecord>>,
}

impl Log for Capture {
    fn enabled(&self, metadata: &Metadata) -> bool {
        metadata.target().starts_with(CAPTURE_TARGET_PREFIX)
    }

    fn log(&self, record: &Record) {
        if self.enabled(record.metadata()) {
            self.records
                .lock()
                .unwrap_or_else(PoisonError::into_inner)
                .push((
                    thread::current().id(),
                    record.target().to_owned(),
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
static INSTALLED: OnceLock<()> = OnceLock::new();

/// Installs the one process-wide capture logger (FR-088); idempotent, safe from any number of
/// test threads at once.
pub fn install_capture() {
    INSTALLED.get_or_init(|| {
        log::set_logger(&CAPTURE).expect("the capture logger is the only logger a test installs");
        log::set_max_level(LevelFilter::Trace);
    });
}

/// The calling thread's captured `(target, level, message)` records, removed from the store.
pub fn take_mine() -> Vec<(String, Level, String)> {
    let me = thread::current().id();
    let mut all = CAPTURE
        .records
        .lock()
        .unwrap_or_else(PoisonError::into_inner);
    let (mine, rest): (Vec<_>, Vec<_>) = all.drain(..).partition(|r| r.0 == me);
    *all = rest;
    mine.into_iter()
        .map(|(_, target, level, message)| (target, level, message))
        .collect()
}

/// The test `HostFacts` (FR-083): records every call, then answers from its maps; an unlisted key is `Absent`.
/// Starts no process and reads no registry; `RefCell` keeps it per thread.
#[derive(Default)]
pub struct FakeHostFacts {
    pub os_version: Option<OsVersion>,
    pub inno: BTreeMap<String, RecordRead<InnoValues>>,
    pub pkg: BTreeMap<String, RecordRead<PkgOutputs>>,
    /// `os_version`, `inno_values <key>`, `pkg_outputs <id>`, in call order.
    pub calls: RefCell<Vec<String>>,
}

impl HostFacts for FakeHostFacts {
    fn os_version(&self) -> Option<OsVersion> {
        self.calls.borrow_mut().push("os_version".to_owned());
        self.os_version
    }

    fn inno_values(&self, key: &PlainName) -> RecordRead<InnoValues> {
        self.calls
            .borrow_mut()
            .push(format!("inno_values {}", key.as_str()));
        self.inno
            .get(key.as_str())
            .cloned()
            .unwrap_or(RecordRead::Absent)
    }

    fn pkg_outputs(&self, pkg_id: &PlainName) -> RecordRead<PkgOutputs> {
        self.calls
            .borrow_mut()
            .push(format!("pkg_outputs {}", pkg_id.as_str()));
        self.pkg
            .get(pkg_id.as_str())
            .cloned()
            .unwrap_or(RecordRead::Absent)
    }
}

/// The test `HostIo` (FR-083): records every call, then answers from its maps; an unlisted argv is `NoOutput`,
/// an unlisted subkey `NoKey`. Starts no process and reads no registry; `RefCell` keeps it per thread.
#[derive(Default)]
pub struct FakeHostIo {
    pub runs: BTreeMap<Vec<String>, RunOutcome>,
    pub registry: BTreeMap<String, RegistryRead>,
    pub rtl: Option<(u32, u32, u32)>,
    /// `run <argv joined by ' '>`, `registry <subkey> <n1> <n2>`, `rtl_version`, in call order.
    pub calls: RefCell<Vec<String>>,
}

impl HostIo for FakeHostIo {
    fn run(&self, argv: &[&str], _bound: Duration) -> RunOutcome {
        self.calls
            .borrow_mut()
            .push(format!("run {}", argv.join(" ")));
        let key: Vec<String> = argv.iter().map(|arg| (*arg).to_owned()).collect();
        self.runs.get(&key).cloned().unwrap_or(RunOutcome::NoOutput)
    }

    fn registry_values(&self, subkey: &str, names: [&str; 2]) -> RegistryRead {
        let [first, second] = names;
        self.calls
            .borrow_mut()
            .push(format!("registry {subkey} {first} {second}"));
        self.registry
            .get(subkey)
            .cloned()
            .unwrap_or(RegistryRead::NoKey)
    }

    fn rtl_version(&self) -> Option<(u32, u32, u32)> {
        self.calls.borrow_mut().push("rtl_version".to_owned());
        self.rtl
    }
}

/// The KAT public key (`tools/feed-contract/fixtures/kat/kat.pub`, PD-14): what every Phase 4-6 test context
/// carries. Never the release key (FR-087).
pub fn kat_signing_key() -> SigningKey {
    SigningKey::from_text(include_str!(
        "../../../../../tools/feed-contract/fixtures/kat/kat.pub"
    ))
    .unwrap_or_else(|e| panic!("the KAT key does not parse: {e}"))
}

/// The first platform key this machine accepts (FR-034); panics on an empty candidate list.
pub fn native_key() -> PlatformKey {
    version::platform_candidates(NativePlatform.os(), Arch::native())
        .first()
        .copied()
        .unwrap_or_else(|| panic!("this machine has no platform key"))
}

/// One plugin of a `test_feed` (plan §4.7): one `platforms` entry under `key`.
pub struct TestPlugin<'a> {
    pub name: &'a str,
    pub version: &'a str,
    pub key: PlatformKey,
    pub asset: &'a str,
    pub size: u64,
    pub sha256: [u8; 32],
    pub signature: String,
    pub min_os: Option<&'a str>,
}

/// A contract-valid v2 document (TA-14) of `plugins`, each on `https://assets.example.invalid/<lower>/`, as bytes.
pub fn test_feed_document(plugins: &[TestPlugin<'_>]) -> Vec<u8> {
    let mut entries = serde_json::Map::new();
    for (index, p) in plugins.iter().enumerate() {
        let n = index + 1;
        let lower = p.name.to_ascii_lowercase();
        let base = format!("https://assets.example.invalid/{lower}/");
        let prefix = p.key.as_str().split('-').next().unwrap_or_default();
        let mut platform = serde_json::json!({
            "url": format!("{base}v{}/{}", p.version, p.asset),
            "size": p.size,
            "sha256": hex_lower(&p.sha256),
            "signature": p.signature,
        });
        let (format, default_min_os) = match prefix {
            "darwin" => ("pkg", Some("15.0")),
            "linux" => ("tar.gz", Some("2.31")),
            _ => ("inno-exe", None),
        };
        platform["format"] = serde_json::json!(format);
        if let Some(min_os) = p.min_os.or(default_min_os) {
            platform["min_os"] = serde_json::json!(min_os);
        }
        if format == "tar.gz" {
            platform["payload"] = serde_json::json!({ "files": 1, "sha256": hex_lower(&p.sha256) });
        }
        let mut install = serde_json::json!({ "vst3_bundle": format!("{}.vst3", p.name) });
        match prefix {
            "darwin" => {
                install["au_component"] = serde_json::json!(format!("{}.component", p.name));
                install["macos_pkg_id"] = serde_json::json!(format!("com.krateaudio.{lower}"));
            }
            "windows" => {
                install["windows_uninstall_key"] =
                    serde_json::json!(format!("{{{n:08X}-0000-4000-8000-{n:012X}}}_is1"));
            }
            _ => {}
        }
        let mut platforms = serde_json::Map::new();
        platforms.insert(p.key.as_str().to_owned(), platform);
        entries.insert(
            p.name.to_owned(),
            serde_json::json!({
                "version": p.version,
                "download_url": base,
                "release_notes": "",
                "tag": format!("{lower}/v{}", p.version),
                "pub_date": "2026-10-31T00:00:00Z",
                "channel": "stable",
                "release_notes_url": base,
                "platforms": platforms,
                "install": install,
            }),
        );
    }
    let doc = serde_json::json!({ "schema_version": 2, "plugins": entries });
    serde_json::to_vec(&doc).unwrap_or_else(|e| panic!("a test feed does not serialise: {e}"))
}

/// `feed::parse` of `test_feed_document(plugins)`; panics with the findings on a rejection (FR-085).
pub fn test_feed(plugins: &[TestPlugin<'_>]) -> Feed {
    feed::parse(&test_feed_document(plugins))
        .unwrap_or_else(|e| panic!("a test feed fails the contract: {e:?}"))
}

/// One `StagingIo` / `PartialFile` operation, for `FaultyStagingIo` (plan §4.7).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Op {
    CreateDir,
    KindOf,
    LenOf,
    /// `open_partial(_, false)`.
    Open,
    /// `open_partial(_, true)`.
    Truncate,
    Write,
    Sync,
    Rename,
    RemoveFile,
    RemoveDir,
    ReadDir,
}

/// The counters a `FaultyStagingIo` shares with the partial files it opens.
#[derive(Default)]
struct OpCounters {
    calls: AtomicUsize,
    written: AtomicU64,
    per_op: [AtomicUsize; 11],
}

/// The fault plan and counters of one `FaultyStagingIo`, shared with its open partial files.
#[derive(Clone)]
struct Faults {
    /// Fail the n-th call (1-based) of the op with the kind, without delegating.
    fail: Option<(Op, usize, io::ErrorKind)>,
    /// Panic right after the k-th call (1-based, every op counted) returns.
    panic_after: Option<usize>,
    counters: Arc<OpCounters>,
}

impl Faults {
    /// Counts the call, then fails it as planned or runs `real`, then panics as planned.
    fn run<T>(&self, op: Op, real: impl FnOnce() -> io::Result<T>) -> io::Result<T> {
        let nth = self.counters.per_op[op as usize].fetch_add(1, Ordering::SeqCst) + 1;
        let total = self.counters.calls.fetch_add(1, Ordering::SeqCst) + 1;
        let result = match self.fail {
            Some((fail_op, fail_nth, kind)) if fail_op == op && fail_nth == nth => {
                Err(io::Error::from(kind))
            }
            _ => real(),
        };
        if self.panic_after == Some(total) {
            panic!("FaultyStagingIo: planned panic after call {total} ({op:?})");
        }
        result
    }
}

/// A `StagingIo` that delegates to `FsStagingIo`, with one planned failure or panic and call counters (plan §4.7).
pub struct FaultyStagingIo {
    faults: Faults,
}

impl FaultyStagingIo {
    fn with(fail: Option<(Op, usize, io::ErrorKind)>, panic_after: Option<usize>) -> Self {
        FaultyStagingIo {
            faults: Faults {
                fail,
                panic_after,
                counters: Arc::new(OpCounters::default()),
            },
        }
    }

    /// No fault; delegates every call.
    pub fn healthy() -> FaultyStagingIo {
        FaultyStagingIo::with(None, None)
    }

    /// The `nth` call (1-based) of `op` returns `kind` without touching the disk.
    pub fn failing(op: Op, nth: usize, kind: io::ErrorKind) -> FaultyStagingIo {
        FaultyStagingIo::with(Some((op, nth, kind)), None)
    }

    /// Panics right after the `k`-th call (1-based, every method and partial-file call counted) returns.
    pub fn panicking_after(k: usize) -> FaultyStagingIo {
        FaultyStagingIo::with(None, Some(k))
    }

    /// Every call so far, of every op.
    pub fn calls(&self) -> usize {
        self.faults.counters.calls.load(Ordering::SeqCst)
    }

    /// The calls of `op` so far.
    pub fn calls_of(&self, op: Op) -> usize {
        self.faults.counters.per_op[op as usize].load(Ordering::SeqCst)
    }

    /// The bytes passed to `write_all` so far.
    pub fn written(&self) -> u64 {
        self.faults.counters.written.load(Ordering::SeqCst)
    }
}

/// The partial file `FaultyStagingIo::open_partial` returns: counts and faults `write_all` / `sync`.
struct CountingPartial {
    inner: Box<dyn PartialFile>,
    faults: Faults,
}

impl PartialFile for CountingPartial {
    fn write_all(&mut self, bytes: &[u8]) -> io::Result<()> {
        let len = u64::try_from(bytes.len()).unwrap_or(u64::MAX);
        self.faults
            .counters
            .written
            .fetch_add(len, Ordering::SeqCst);
        let inner = &mut self.inner;
        self.faults.run(Op::Write, || inner.write_all(bytes))
    }

    fn sync(&mut self) -> io::Result<()> {
        let inner = &mut self.inner;
        self.faults.run(Op::Sync, || inner.sync())
    }
}

impl StagingIo for FaultyStagingIo {
    fn create_dir_all(&self, dir: &Path) -> io::Result<()> {
        self.faults
            .run(Op::CreateDir, || FsStagingIo.create_dir_all(dir))
    }

    fn kind_of(&self, path: &Path) -> io::Result<Option<EntryKind>> {
        self.faults.run(Op::KindOf, || FsStagingIo.kind_of(path))
    }

    fn len_of(&self, path: &Path) -> io::Result<Option<u64>> {
        self.faults.run(Op::LenOf, || FsStagingIo.len_of(path))
    }

    fn open_partial(&self, path: &Path, truncate: bool) -> io::Result<Box<dyn PartialFile>> {
        let op = if truncate { Op::Truncate } else { Op::Open };
        let inner = self
            .faults
            .run(op, || FsStagingIo.open_partial(path, truncate))?;
        Ok(Box::new(CountingPartial {
            inner,
            faults: self.faults.clone(),
        }))
    }

    fn rename(&self, from: &Path, to: &Path) -> io::Result<()> {
        self.faults.run(Op::Rename, || FsStagingIo.rename(from, to))
    }

    fn remove_file(&self, path: &Path) -> io::Result<()> {
        self.faults
            .run(Op::RemoveFile, || FsStagingIo.remove_file(path))
    }

    fn remove_dir_all(&self, dir: &Path) -> io::Result<()> {
        self.faults
            .run(Op::RemoveDir, || FsStagingIo.remove_dir_all(dir))
    }

    fn read_dir_entries(&self, dir: &Path) -> io::Result<Vec<StagedEntry>> {
        self.faults
            .run(Op::ReadDir, || FsStagingIo.read_dir_entries(dir))
    }
}

/// The test policy (plan §7): 4 attempts, no backoff wait, 2 s stall, 10 s per attempt.
pub fn test_policy() -> DownloadPolicy {
    DownloadPolicy {
        attempts: 4,
        backoff: vec![Duration::ZERO],
        stall: Duration::from_secs(2),
        attempt_deadline: Duration::from_secs(10),
    }
}

/// The one place a test builds a `DownloadEnv` (Q-B, PD-11): the parts it borrows, owned.
pub struct DownloadRig {
    pub ctx: AppContext,
    pub io: FaultyStagingIo,
    pub policy: DownloadPolicy,
    pub cancel: CancelToken,
}

impl DownloadRig {
    /// `io: FaultyStagingIo::healthy()`, `policy: test_policy()`, `cancel: CancelToken::new()`.
    pub fn new(ctx: AppContext) -> DownloadRig {
        DownloadRig {
            ctx,
            io: FaultyStagingIo::healthy(),
            policy: test_policy(),
            cancel: CancelToken::new(),
        }
    }

    /// `DownloadEnv { ctx: &self.ctx, io: &self.io, policy: &self.policy, cancel: &self.cancel }`.
    pub fn env(&self) -> DownloadEnv<'_> {
        DownloadEnv {
            ctx: &self.ctx,
            io: &self.io,
            policy: &self.policy,
            cancel: &self.cancel,
        }
    }
}

// Phase 8 test doubles (plan §4.5, FR-087; tasks TA-3, TA-4, TA-13).

/// The kind of one recorded `ExtractIo` call.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum XKind {
    OpenArchive,
    Rewind,
    ListDir,
    CreateWorkDir,
    CreateDir,
    CreateFile,
    Write,
    SetMode,
    Symlink,
    RemoveTree,
    RemoveEntry,
    KindOf,
    ReadTree,
    Canonicalize,
    ReadFile,
}

/// One recorded `ExtractIo` call with its path; `Write` carries the exact slice passed to
/// `ExtractFile::write_all`, under the path `create_file` was given.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum XOp {
    OpenArchive(PathBuf),
    Rewind,
    ListDir(PathBuf),
    CreateWorkDir { extract: PathBuf, prefix: String },
    CreateDir(PathBuf),
    CreateFile(PathBuf),
    Write { path: PathBuf, bytes: Vec<u8> },
    SetMode(PathBuf, u32),
    Symlink { target: String, link: PathBuf },
    RemoveTree(PathBuf),
    RemoveEntry(PathBuf, NodeKind),
    KindOf(PathBuf),
    ReadTree(PathBuf),
    Canonicalize(PathBuf),
    ReadFile(PathBuf),
}

impl XOp {
    pub fn kind(&self) -> XKind {
        match self {
            XOp::OpenArchive(_) => XKind::OpenArchive,
            XOp::Rewind => XKind::Rewind,
            XOp::ListDir(_) => XKind::ListDir,
            XOp::CreateWorkDir { .. } => XKind::CreateWorkDir,
            XOp::CreateDir(_) => XKind::CreateDir,
            XOp::CreateFile(_) => XKind::CreateFile,
            XOp::Write { .. } => XKind::Write,
            XOp::SetMode(..) => XKind::SetMode,
            XOp::Symlink { .. } => XKind::Symlink,
            XOp::RemoveTree(_) => XKind::RemoveTree,
            XOp::RemoveEntry(..) => XKind::RemoveEntry,
            XOp::KindOf(_) => XKind::KindOf,
            XOp::ReadTree(_) => XKind::ReadTree,
            XOp::Canonicalize(_) => XKind::Canonicalize,
            XOp::ReadFile(_) => XKind::ReadFile,
        }
    }

    /// The call's path: `None` only for `Rewind`; `extract` for `CreateWorkDir`, `link` for `Symlink`.
    pub fn path(&self) -> Option<&Path> {
        match self {
            XOp::Rewind => None,
            XOp::OpenArchive(path)
            | XOp::ListDir(path)
            | XOp::CreateDir(path)
            | XOp::CreateFile(path)
            | XOp::SetMode(path, _)
            | XOp::RemoveTree(path)
            | XOp::RemoveEntry(path, _)
            | XOp::KindOf(path)
            | XOp::ReadTree(path)
            | XOp::Canonicalize(path)
            | XOp::ReadFile(path)
            | XOp::Write { path, .. } => Some(path),
            XOp::CreateWorkDir { extract, .. } => Some(extract),
            XOp::Symlink { link, .. } => Some(link),
        }
    }

    /// True for exactly FR-087's eight mutating kinds.
    pub fn is_mutating(&self) -> bool {
        matches!(
            self.kind(),
            XKind::CreateWorkDir
                | XKind::CreateDir
                | XKind::CreateFile
                | XKind::Write
                | XKind::SetMode
                | XKind::Symlink
                | XKind::RemoveTree
                | XKind::RemoveEntry
        )
    }
}

/// One node of a `MemExtractIo` tree.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum MemNode {
    Dir { mode: u32 },
    File { bytes: Vec<u8>, mode: u32 },
    Link { target: String },
}

/// What `FaultyExtractIo::remove_tree` does: delegate, succeed removing nothing, or fail.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RemoveTreeMode {
    Forward,
    Noop,
    Fail(io::ErrorKind),
}

/// The archive handle's read fails with `kind` once `after` bytes of `pass` were served (pass 1 is
/// before the first rewind).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReadFault {
    pub pass: u8,
    pub after: u64,
    pub kind: io::ErrorKind,
}

/// An `ArchiveRead` serving `first` until the first `rewind`, then `after_rewind`.
pub struct TwoPhaseArchive {
    first: Vec<u8>,
    after_rewind: Vec<u8>,
    pos: usize,
    rewound: bool,
}

impl TwoPhaseArchive {
    pub fn new(first: Vec<u8>, after_rewind: Vec<u8>) -> TwoPhaseArchive {
        TwoPhaseArchive {
            first,
            after_rewind,
            pos: 0,
            rewound: false,
        }
    }
}

impl Read for TwoPhaseArchive {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let source = if self.rewound {
            &self.after_rewind
        } else {
            &self.first
        };
        let rest = source.get(self.pos..).unwrap_or_default();
        let n = rest.len().min(buf.len());
        buf[..n].copy_from_slice(&rest[..n]);
        self.pos += n;
        Ok(n)
    }
}

impl ArchiveRead for TwoPhaseArchive {
    fn rewind(&mut self) -> io::Result<()> {
        self.pos = 0;
        self.rewound = true;
        Ok(())
    }
}

/// The number of `XKind` variants (one counter each in a `CallLog`).
const XKIND_COUNT: usize = 15;

/// The call record and per-kind counters a double shares with the handles it returns.
#[derive(Default)]
struct CallLog {
    calls: Vec<XOp>,
    per_kind: [usize; XKIND_COUNT],
}

/// Records every call of one double and its handles; fails the planned calls (`fails`: the n-th
/// call, 1-based, of a kind returns the kind's error).
#[derive(Clone, Default)]
struct Recorder {
    log: Arc<Mutex<CallLog>>,
    fails: Vec<(XKind, usize, io::ErrorKind)>,
}

impl Recorder {
    /// Records `op`, then returns its planned error, if any.
    fn record(&self, op: XOp) -> io::Result<()> {
        let kind = op.kind();
        let nth = {
            let mut log = self.log.lock().unwrap_or_else(PoisonError::into_inner);
            log.calls.push(op);
            let count = &mut log.per_kind[kind as usize];
            *count += 1;
            *count
        };
        match self.fails.iter().find(|&&(k, n, _)| k == kind && n == nth) {
            Some(&(_, _, error)) => Err(io::Error::from(error)),
            None => Ok(()),
        }
    }

    fn calls(&self) -> Vec<XOp> {
        self.log
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .calls
            .clone()
    }

    fn mutating_calls(&self) -> usize {
        self.log
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .calls
            .iter()
            .filter(|op| op.is_mutating())
            .count()
    }

    /// `inner`, recording each `rewind` and failing a read as `fault` plans.
    fn archive(
        &self,
        inner: Box<dyn ArchiveRead>,
        fault: Option<ReadFault>,
    ) -> Box<dyn ArchiveRead> {
        Box::new(WatchedArchive {
            inner,
            rec: self.clone(),
            fault,
            pass: 1,
            served: 0,
        })
    }

    /// `inner`, recording each `write_all` as `Write` under `path` (the `create_file` argument).
    fn file(&self, inner: Box<dyn ExtractFile>, path: &Path) -> Box<dyn ExtractFile> {
        Box::new(WatchedFile {
            inner,
            rec: self.clone(),
            path: path.to_path_buf(),
        })
    }
}

/// The archive handle every double returns: records `rewind`, counts the bytes of each pass.
struct WatchedArchive {
    inner: Box<dyn ArchiveRead>,
    rec: Recorder,
    fault: Option<ReadFault>,
    /// 1 before the first rewind.
    pass: u8,
    /// Bytes served in this pass.
    served: u64,
}

impl Read for WatchedArchive {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let mut limit = buf.len();
        if let Some(fault) = self.fault
            && fault.pass == self.pass
        {
            let left = fault.after.saturating_sub(self.served);
            if left == 0 {
                return Err(io::Error::from(fault.kind));
            }
            limit = limit.min(usize::try_from(left).unwrap_or(usize::MAX));
        }
        let n = self.inner.read(&mut buf[..limit])?;
        self.served = self
            .served
            .saturating_add(u64::try_from(n).unwrap_or(u64::MAX));
        Ok(n)
    }
}

impl ArchiveRead for WatchedArchive {
    fn rewind(&mut self) -> io::Result<()> {
        self.rec.record(XOp::Rewind)?;
        self.inner.rewind()?;
        self.pass = self.pass.saturating_add(1);
        self.served = 0;
        Ok(())
    }
}

/// The staged-file handle every double returns: records each slice, then delegates.
struct WatchedFile {
    inner: Box<dyn ExtractFile>,
    rec: Recorder,
    path: PathBuf,
}

impl ExtractFile for WatchedFile {
    fn write_all(&mut self, bytes: &[u8]) -> io::Result<()> {
        self.rec.record(XOp::Write {
            path: self.path.clone(),
            bytes: bytes.to_vec(),
        })?;
        self.inner.write_all(bytes)
    }
}

/// A `MemExtractIo` tree: absolute keys, spelled as the test's roots.
type MemTree = BTreeMap<PathBuf, MemNode>;

/// The links one `canonicalize` follows before it gives up (Linux's `MAXSYMLINKS`).
const MAX_LINK_HOPS: usize = 40;

fn not_found() -> io::Error {
    io::Error::from(io::ErrorKind::NotFound)
}

fn not_a_directory() -> io::Error {
    io::Error::from(io::ErrorKind::NotADirectory)
}

/// True for a proper ancestor of `base` (an existing directory that canonicalises to itself).
fn is_ancestor(base: &Path, path: &Path) -> bool {
    path != base && base.starts_with(path)
}

/// The node at `key`, never following a link: a proper ancestor of `base` is a directory; any
/// other key outside `base` is absent.
fn mem_kind(base: &Path, tree: &MemTree, key: &Path) -> Option<NodeKind> {
    if is_ancestor(base, key) {
        return Some(NodeKind::Dir);
    }
    if !key.starts_with(base) {
        return None;
    }
    tree.get(key).map(|node| match node {
        MemNode::Dir { .. } => NodeKind::Dir,
        MemNode::File { .. } => NodeKind::File,
        MemNode::Link { .. } => NodeKind::Symlink,
    })
}

/// One component of a path being resolved.
enum Step {
    Up,
    Name(OsString),
}

/// `realpath` over the tree: links resolved component by component (`/`-separated targets,
/// relative to the link's directory); an absent component, a dangling link included, is `NotFound`.
fn mem_resolve(base: &Path, tree: &MemTree, path: &Path) -> io::Result<PathBuf> {
    let mut root = PathBuf::new();
    let mut rest = Vec::new();
    for component in path.components() {
        match component {
            Component::Prefix(_) | Component::RootDir => root.push(component.as_os_str()),
            Component::CurDir => {}
            Component::ParentDir => rest.push(Step::Up),
            Component::Normal(name) => rest.push(Step::Name(name.to_owned())),
        }
    }
    rest.reverse();
    let mut out = root.clone();
    let mut hops = 0;
    while let Some(step) = rest.pop() {
        let name = match step {
            Step::Up => {
                out.pop();
                continue;
            }
            Step::Name(name) => name,
        };
        let candidate = out.join(&name);
        if is_ancestor(base, &candidate) {
            out = candidate;
            continue;
        }
        let node = if candidate.starts_with(base) {
            tree.get(&candidate)
        } else {
            None
        };
        match node {
            None => return Err(not_found()),
            Some(MemNode::Dir { .. }) => out = candidate,
            Some(MemNode::File { .. }) => {
                if !rest.is_empty() {
                    return Err(not_a_directory());
                }
                out = candidate;
            }
            Some(MemNode::Link { target }) => {
                hops += 1;
                if hops > MAX_LINK_HOPS {
                    return Err(io::Error::other("too many levels of symbolic links"));
                }
                if target.starts_with('/') {
                    out = root.clone();
                }
                for piece in target.split('/').rev() {
                    match piece {
                        "" | "." => {}
                        ".." => rest.push(Step::Up),
                        _ => rest.push(Step::Name(OsString::from(piece))),
                    }
                }
            }
        }
    }
    Ok(out)
}

/// The key of `path`'s last component with every parent link resolved (`symlink_metadata`).
fn mem_locate(base: &Path, tree: &MemTree, path: &Path) -> io::Result<PathBuf> {
    match (path.parent(), path.file_name()) {
        (Some(parent), Some(name)) => Ok(mem_resolve(base, tree, parent)?.join(name)),
        _ => mem_resolve(base, tree, path),
    }
}

/// Inserts `node` at `path`: the name must be absent (a dangling link included, `create_new`),
/// inside `base`, with a parent directory. Returns the key.
fn mem_create(base: &Path, tree: &mut MemTree, path: &Path, node: MemNode) -> io::Result<PathBuf> {
    let key = mem_locate(base, tree, path)?;
    if mem_kind(base, tree, &key).is_some() {
        return Err(io::Error::from(io::ErrorKind::AlreadyExists));
    }
    if !key.starts_with(base) {
        return Err(not_found());
    }
    let parent = key.parent().ok_or_else(not_found)?;
    if mem_kind(base, tree, parent) != Some(NodeKind::Dir) {
        return Err(not_a_directory());
    }
    tree.insert(key.clone(), node);
    Ok(key)
}

/// `create_dir_all` over the tree: new directories get mode `0o755`.
fn mem_create_dir_all(base: &Path, tree: &mut MemTree, path: &Path) -> io::Result<()> {
    match mem_resolve(base, tree, path) {
        Ok(key) if mem_kind(base, tree, &key) == Some(NodeKind::Dir) => Ok(()),
        Ok(_) => Err(io::Error::from(io::ErrorKind::AlreadyExists)),
        Err(e) if e.kind() == io::ErrorKind::NotFound => {
            if let Some(parent) = path.parent() {
                mem_create_dir_all(base, tree, parent)?;
            }
            mem_create(base, tree, path, MemNode::Dir { mode: 0o755 })?;
            Ok(())
        }
        Err(e) => Err(e),
    }
}

/// A file `MemExtractIo::create_file` made: appends each slice to its node.
struct MemFile {
    tree: Arc<Mutex<MemTree>>,
    key: PathBuf,
}

impl ExtractFile for MemFile {
    fn write_all(&mut self, bytes: &[u8]) -> io::Result<()> {
        let mut tree = self.tree.lock().unwrap_or_else(PoisonError::into_inner);
        match tree.get_mut(&self.key) {
            Some(MemNode::File { bytes: content, .. }) => {
                content.extend_from_slice(bytes);
                Ok(())
            }
            _ => Err(not_found()),
        }
    }
}

/// An in-memory `ExtractIo` (plan §4.5; tasks TA-3, TA-4): `new(base)` plants `base` as a
/// directory and treats every proper ancestor of `base` as an existing directory that
/// canonicalises to itself; every other path outside `base` is absent (`NotFound`). New
/// directories get mode `0o755`, new files `0o644`, work dirs `0o700`; `canonicalize` resolves
/// links component by component. Only `open_archive` touches the disk, reading the archive file.
pub struct MemExtractIo {
    base: PathBuf,
    tree: Arc<Mutex<MemTree>>,
    rec: Recorder,
    /// The last work-dir number handed out.
    counter: AtomicUsize,
}

impl MemExtractIo {
    pub fn new(base: &Path) -> MemExtractIo {
        let tree = MemTree::from([(base.to_path_buf(), MemNode::Dir { mode: 0o755 })]);
        MemExtractIo {
            base: base.to_path_buf(),
            tree: Arc::new(Mutex::new(tree)),
            rec: Recorder::default(),
            counter: AtomicUsize::new(0),
        }
    }

    fn lock(&self) -> MutexGuard<'_, MemTree> {
        self.tree.lock().unwrap_or_else(PoisonError::into_inner)
    }

    /// Puts `node` at `path` as spelled (replacing any node there), with any missing parent
    /// directory under `base` as mode `0o755`. Not recorded.
    fn plant(&self, path: &Path, node: MemNode) {
        let mut tree = self.lock();
        for ancestor in path.ancestors().skip(1) {
            if ancestor == self.base || !ancestor.starts_with(&self.base) {
                break;
            }
            tree.entry(ancestor.to_path_buf())
                .or_insert(MemNode::Dir { mode: 0o755 });
        }
        tree.insert(path.to_path_buf(), node);
    }

    pub fn plant_dir(&self, path: &Path, mode: u32) {
        self.plant(path, MemNode::Dir { mode });
    }

    pub fn plant_file(&self, path: &Path, bytes: &[u8], mode: u32) {
        self.plant(
            path,
            MemNode::File {
                bytes: bytes.to_vec(),
                mode,
            },
        );
    }

    pub fn plant_link(&self, path: &Path, target: &str) {
        self.plant(
            path,
            MemNode::Link {
                target: target.to_owned(),
            },
        );
    }

    /// The node stored at `path` as spelled (no link resolution).
    pub fn node(&self, path: &Path) -> Option<MemNode> {
        self.lock().get(path).cloned()
    }

    /// Every stored path strictly under `root`, sorted.
    pub fn paths_under(&self, root: &Path) -> Vec<PathBuf> {
        self.lock()
            .keys()
            .filter(|key| key.as_path() != root && key.starts_with(root))
            .cloned()
            .collect()
    }

    pub fn calls(&self) -> Vec<XOp> {
        self.rec.calls()
    }

    pub fn mutating_calls(&self) -> usize {
        self.rec.mutating_calls()
    }
}

impl ExtractIo for MemExtractIo {
    fn open_archive(&self, path: &Path) -> io::Result<Box<dyn ArchiveRead>> {
        self.rec.record(XOp::OpenArchive(path.to_path_buf()))?;
        let bytes = std::fs::read(path)?;
        Ok(self
            .rec
            .archive(Box::new(TwoPhaseArchive::new(bytes.clone(), bytes)), None))
    }

    fn list_dir(&self, dir: &Path) -> io::Result<Option<Vec<(OsString, NodeKind)>>> {
        self.rec.record(XOp::ListDir(dir.to_path_buf()))?;
        let tree = self.lock();
        let key = match mem_resolve(&self.base, &tree, dir) {
            Ok(key) => key,
            Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
            Err(e) => return Err(e),
        };
        if mem_kind(&self.base, &tree, &key) != Some(NodeKind::Dir) {
            return Err(not_a_directory());
        }
        let mut children = BTreeMap::new();
        for (path, node) in tree.iter() {
            if path.parent() == Some(key.as_path())
                && path.starts_with(&self.base)
                && let Some(name) = path.file_name()
            {
                let kind = match node {
                    MemNode::Dir { .. } => NodeKind::Dir,
                    MemNode::File { .. } => NodeKind::File,
                    MemNode::Link { .. } => NodeKind::Symlink,
                };
                children.insert(name.to_owned(), kind);
            }
        }
        if is_ancestor(&self.base, &key)
            && let Some(next) = self
                .base
                .strip_prefix(&key)
                .ok()
                .and_then(|rest| rest.components().next())
        {
            children.insert(next.as_os_str().to_owned(), NodeKind::Dir);
        }
        Ok(Some(children.into_iter().collect()))
    }

    fn create_work_dir(&self, extract: &Path, prefix: &str) -> io::Result<PathBuf> {
        self.rec.record(XOp::CreateWorkDir {
            extract: extract.to_path_buf(),
            prefix: prefix.to_owned(),
        })?;
        let mut tree = self.lock();
        mem_create_dir_all(&self.base, &mut tree, extract)?;
        let key = mem_locate(&self.base, &tree, extract)?;
        if mem_kind(&self.base, &tree, &key) != Some(NodeKind::Dir) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "the extract dir is not a directory",
            ));
        }
        if !key.starts_with(&self.base) {
            return Err(not_found());
        }
        loop {
            let n = self.counter.fetch_add(1, Ordering::SeqCst) + 1;
            let name = format!("{prefix}{n:06}");
            if let Entry::Vacant(slot) = tree.entry(key.join(&name)) {
                slot.insert(MemNode::Dir { mode: 0o700 });
                return Ok(extract.join(name));
            }
        }
    }

    fn create_dir(&self, path: &Path) -> io::Result<()> {
        self.rec.record(XOp::CreateDir(path.to_path_buf()))?;
        mem_create(
            &self.base,
            &mut self.lock(),
            path,
            MemNode::Dir { mode: 0o755 },
        )?;
        Ok(())
    }

    fn create_file(&self, path: &Path) -> io::Result<Box<dyn ExtractFile>> {
        self.rec.record(XOp::CreateFile(path.to_path_buf()))?;
        let key = mem_create(
            &self.base,
            &mut self.lock(),
            path,
            MemNode::File {
                bytes: Vec::new(),
                mode: 0o644,
            },
        )?;
        let file = MemFile {
            tree: Arc::clone(&self.tree),
            key,
        };
        Ok(self.rec.file(Box::new(file), path))
    }

    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()> {
        self.rec.record(XOp::SetMode(path.to_path_buf(), mode))?;
        let mut tree = self.lock();
        let key = mem_resolve(&self.base, &tree, path)?;
        match tree.get_mut(&key) {
            Some(MemNode::Dir { mode: m } | MemNode::File { mode: m, .. }) => {
                *m = mode;
                Ok(())
            }
            _ => Err(not_found()),
        }
    }

    fn symlink(&self, target: &str, link: &Path) -> io::Result<()> {
        self.rec.record(XOp::Symlink {
            target: target.to_owned(),
            link: link.to_path_buf(),
        })?;
        mem_create(
            &self.base,
            &mut self.lock(),
            link,
            MemNode::Link {
                target: target.to_owned(),
            },
        )?;
        Ok(())
    }

    fn supports_symlinks(&self) -> bool {
        true
    }

    /// A directory with every descendant, or a link as the link itself; a file is `NotADirectory`.
    fn remove_tree(&self, path: &Path) -> io::Result<()> {
        self.rec.record(XOp::RemoveTree(path.to_path_buf()))?;
        let mut tree = self.lock();
        let key = mem_locate(&self.base, &tree, path)?;
        match tree.get(&key) {
            None => Err(not_found()),
            Some(MemNode::File { .. }) => Err(not_a_directory()),
            Some(_) => {
                tree.retain(|k, _| !k.starts_with(&key));
                Ok(())
            }
        }
    }

    /// By the stored node: a directory as a tree, anything else (a link as itself) as one entry.
    fn remove_entry(&self, path: &Path, kind: NodeKind) -> io::Result<()> {
        self.rec
            .record(XOp::RemoveEntry(path.to_path_buf(), kind))?;
        let mut tree = self.lock();
        let key = mem_locate(&self.base, &tree, path)?;
        match tree.get(&key) {
            None => Err(not_found()),
            Some(MemNode::Dir { .. }) => {
                tree.retain(|k, _| !k.starts_with(&key));
                Ok(())
            }
            Some(_) => {
                tree.remove(&key);
                Ok(())
            }
        }
    }

    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>> {
        self.rec.record(XOp::KindOf(path.to_path_buf()))?;
        let tree = self.lock();
        match mem_locate(&self.base, &tree, path) {
            Ok(key) => Ok(mem_kind(&self.base, &tree, &key)),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e),
        }
    }

    /// Every node under `root` in key order: `/`-joined relative bytes, `mode: Some(..)` (links
    /// `0o777`), a link's target bytes.
    fn read_tree(&self, root: &Path) -> io::Result<Vec<ListingEntry>> {
        self.rec.record(XOp::ReadTree(root.to_path_buf()))?;
        let tree = self.lock();
        let key = mem_resolve(&self.base, &tree, root)?;
        if mem_kind(&self.base, &tree, &key) != Some(NodeKind::Dir) {
            return Err(not_a_directory());
        }
        let mut entries = Vec::new();
        for (path, node) in tree.iter() {
            let Ok(rel) = path.strip_prefix(&key) else {
                continue;
            };
            if rel.as_os_str().is_empty() || !path.starts_with(&self.base) {
                continue;
            }
            let mut bytes = Vec::new();
            for (i, component) in rel.components().enumerate() {
                if i > 0 {
                    bytes.push(b'/');
                }
                bytes.extend_from_slice(component.as_os_str().as_encoded_bytes());
            }
            let (kind, mode, target) = match node {
                MemNode::Dir { mode } => (EntryType::Dir, *mode, None),
                MemNode::File { mode, .. } => (EntryType::File, *mode, None),
                MemNode::Link { target } => {
                    (EntryType::Symlink, 0o777, Some(target.as_bytes().to_vec()))
                }
            };
            entries.push(ListingEntry {
                kind,
                mode: Some(mode),
                path: bytes,
                target,
            });
        }
        Ok(entries)
    }

    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        self.rec.record(XOp::Canonicalize(path.to_path_buf()))?;
        mem_resolve(&self.base, &self.lock(), path)
    }

    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        self.rec.record(XOp::ReadFile(path.to_path_buf()))?;
        let tree = self.lock();
        let key = mem_resolve(&self.base, &tree, path)?;
        match tree.get(&key) {
            Some(MemNode::File { bytes, .. }) => {
                let limit = usize::try_from(max.saturating_add(1)).unwrap_or(usize::MAX);
                Ok(bytes[..bytes.len().min(limit)].to_vec())
            }
            Some(MemNode::Dir { .. }) => Err(io::Error::from(io::ErrorKind::IsADirectory)),
            _ => Err(not_found()),
        }
    }
}

/// An `ExtractIo` wrapper that fails chosen calls (plan §4.5, the `FaultyStagingIo` model): every
/// call is recorded; a planned failure returns its error without delegating.
pub struct FaultyExtractIo<'a> {
    inner: &'a (dyn ExtractIo + Sync),
    rec: Recorder,
    remove_tree: RemoveTreeMode,
    read_fault: Option<ReadFault>,
}

impl<'a> FaultyExtractIo<'a> {
    pub fn new(inner: &'a (dyn ExtractIo + Sync)) -> FaultyExtractIo<'a> {
        FaultyExtractIo {
            inner,
            rec: Recorder::default(),
            remove_tree: RemoveTreeMode::Forward,
            read_fault: None,
        }
    }

    /// Fails the `nth` (1-based) call of `kind` with `error`; `Write` and `Rewind` calls of every
    /// handle count too.
    pub fn with_failure(mut self, kind: XKind, nth: usize, error: io::ErrorKind) -> Self {
        self.rec.fails.push((kind, nth, error));
        self
    }

    pub fn with_remove_tree(mut self, mode: RemoveTreeMode) -> Self {
        self.remove_tree = mode;
        self
    }

    pub fn with_read_fault(mut self, fault: ReadFault) -> Self {
        self.read_fault = Some(fault);
        self
    }

    pub fn calls(&self) -> Vec<XOp> {
        self.rec.calls()
    }

    pub fn mutating_calls(&self) -> usize {
        self.rec.mutating_calls()
    }
}

impl ExtractIo for FaultyExtractIo<'_> {
    fn open_archive(&self, path: &Path) -> io::Result<Box<dyn ArchiveRead>> {
        self.rec.record(XOp::OpenArchive(path.to_path_buf()))?;
        let inner = self.inner.open_archive(path)?;
        Ok(self.rec.archive(inner, self.read_fault))
    }

    fn list_dir(&self, dir: &Path) -> io::Result<Option<Vec<(OsString, NodeKind)>>> {
        self.rec.record(XOp::ListDir(dir.to_path_buf()))?;
        self.inner.list_dir(dir)
    }

    fn create_work_dir(&self, extract: &Path, prefix: &str) -> io::Result<PathBuf> {
        self.rec.record(XOp::CreateWorkDir {
            extract: extract.to_path_buf(),
            prefix: prefix.to_owned(),
        })?;
        self.inner.create_work_dir(extract, prefix)
    }

    fn create_dir(&self, path: &Path) -> io::Result<()> {
        self.rec.record(XOp::CreateDir(path.to_path_buf()))?;
        self.inner.create_dir(path)
    }

    fn create_file(&self, path: &Path) -> io::Result<Box<dyn ExtractFile>> {
        self.rec.record(XOp::CreateFile(path.to_path_buf()))?;
        let inner = self.inner.create_file(path)?;
        Ok(self.rec.file(inner, path))
    }

    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()> {
        self.rec.record(XOp::SetMode(path.to_path_buf(), mode))?;
        self.inner.set_mode(path, mode)
    }

    fn symlink(&self, target: &str, link: &Path) -> io::Result<()> {
        self.rec.record(XOp::Symlink {
            target: target.to_owned(),
            link: link.to_path_buf(),
        })?;
        self.inner.symlink(target, link)
    }

    fn supports_symlinks(&self) -> bool {
        self.inner.supports_symlinks()
    }

    fn remove_tree(&self, path: &Path) -> io::Result<()> {
        self.rec.record(XOp::RemoveTree(path.to_path_buf()))?;
        match self.remove_tree {
            RemoveTreeMode::Forward => self.inner.remove_tree(path),
            RemoveTreeMode::Noop => Ok(()),
            RemoveTreeMode::Fail(kind) => Err(io::Error::from(kind)),
        }
    }

    fn remove_entry(&self, path: &Path, kind: NodeKind) -> io::Result<()> {
        self.rec
            .record(XOp::RemoveEntry(path.to_path_buf(), kind))?;
        self.inner.remove_entry(path, kind)
    }

    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>> {
        self.rec.record(XOp::KindOf(path.to_path_buf()))?;
        self.inner.kind_of(path)
    }

    fn read_tree(&self, root: &Path) -> io::Result<Vec<ListingEntry>> {
        self.rec.record(XOp::ReadTree(root.to_path_buf()))?;
        self.inner.read_tree(root)
    }

    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        self.rec.record(XOp::Canonicalize(path.to_path_buf()))?;
        self.inner.canonicalize(path)
    }

    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        self.rec.record(XOp::ReadFile(path.to_path_buf()))?;
        self.inner.read_file(path, max)
    }
}

/// `RecordingExtractIo::with_open_archive`'s override (a named type for `clippy::type_complexity`).
pub type OpenArchiveOverride<'a> =
    Box<dyn Fn(&Path) -> io::Result<Box<dyn ArchiveRead>> + Send + Sync + 'a>;
/// `RecordingExtractIo::with_read_tree`'s override.
pub type ReadTreeOverride<'a> = Box<dyn Fn(&Path, &mut Vec<ListingEntry>) + Send + Sync + 'a>;
/// `RecordingExtractIo::with_canonicalize`'s override.
pub type CanonicalizeOverride<'a> =
    Box<dyn Fn(&Path, io::Result<PathBuf>) -> io::Result<PathBuf> + Send + Sync + 'a>;
/// `RecordingExtractIo::with_kind_of`'s override.
pub type KindOfOverride<'a> = Box<
    dyn Fn(&Path, io::Result<Option<NodeKind>>) -> io::Result<Option<NodeKind>> + Send + Sync + 'a,
>;

/// An `ExtractIo` wrapper that records every call (plan §4.5, FR-087): `forwarding(inner)` delegates
/// every call; `fail_closed()` refuses every mutating call without touching the filesystem.
pub struct RecordingExtractIo<'a> {
    inner: &'a (dyn ExtractIo + Sync),
    /// FR-087: the eight mutating kinds are recorded and refused.
    fail_closed: bool,
    rec: Recorder,
    on_open_archive: Option<OpenArchiveOverride<'a>>,
    on_read_tree: Option<ReadTreeOverride<'a>>,
    on_canonicalize: Option<CanonicalizeOverride<'a>>,
    on_kind_of: Option<KindOfOverride<'a>>,
}

impl<'a> RecordingExtractIo<'a> {
    fn with_inner(inner: &'a (dyn ExtractIo + Sync), fail_closed: bool) -> RecordingExtractIo<'a> {
        RecordingExtractIo {
            inner,
            fail_closed,
            rec: Recorder::default(),
            on_open_archive: None,
            on_read_tree: None,
            on_canonicalize: None,
            on_kind_of: None,
        }
    }

    pub fn forwarding(inner: &'a (dyn ExtractIo + Sync)) -> RecordingExtractIo<'a> {
        RecordingExtractIo::with_inner(inner, false)
    }

    /// Replaces the inner archive handle.
    pub fn with_open_archive(mut self, f: OpenArchiveOverride<'a>) -> Self {
        self.on_open_archive = Some(f);
        self
    }

    /// Mutates the inner `read_tree` result before it is returned.
    pub fn with_read_tree(mut self, f: ReadTreeOverride<'a>) -> Self {
        self.on_read_tree = Some(f);
        self
    }

    /// Maps the inner `canonicalize` result.
    pub fn with_canonicalize(mut self, f: CanonicalizeOverride<'a>) -> Self {
        self.on_canonicalize = Some(f);
        self
    }

    /// Maps the inner `kind_of` result.
    pub fn with_kind_of(mut self, f: KindOfOverride<'a>) -> Self {
        self.on_kind_of = Some(f);
        self
    }

    pub fn calls(&self) -> Vec<XOp> {
        self.rec.calls()
    }

    pub fn mutating_calls(&self) -> usize {
        self.rec.mutating_calls()
    }

    /// The `canonicalize` arguments, in call order.
    pub fn canonicalized(&self) -> Vec<PathBuf> {
        self.rec
            .calls()
            .into_iter()
            .filter_map(|op| match op {
                XOp::Canonicalize(path) => Some(path),
                _ => None,
            })
            .collect()
    }

    /// Records a mutating call; fail-closed refuses it (FR-087).
    fn mutate(&self, op: XOp) -> io::Result<()> {
        self.rec.record(op)?;
        if self.fail_closed {
            Err(io::Error::other("fail-closed"))
        } else {
            Ok(())
        }
    }
}

impl RecordingExtractIo<'static> {
    /// FR-087: the mutating methods record and return `fail-closed` without reaching `std::fs`;
    /// `open_archive`, the read-only methods and `supports_symlinks` forward to `FsExtractIo`.
    pub fn fail_closed() -> RecordingExtractIo<'static> {
        RecordingExtractIo::with_inner(&FsExtractIo, true)
    }
}

impl ExtractIo for RecordingExtractIo<'_> {
    fn open_archive(&self, path: &Path) -> io::Result<Box<dyn ArchiveRead>> {
        self.rec.record(XOp::OpenArchive(path.to_path_buf()))?;
        let handle = match &self.on_open_archive {
            Some(f) => f(path)?,
            None => self.inner.open_archive(path)?,
        };
        Ok(self.rec.archive(handle, None))
    }

    fn list_dir(&self, dir: &Path) -> io::Result<Option<Vec<(OsString, NodeKind)>>> {
        self.rec.record(XOp::ListDir(dir.to_path_buf()))?;
        self.inner.list_dir(dir)
    }

    fn create_work_dir(&self, extract: &Path, prefix: &str) -> io::Result<PathBuf> {
        self.mutate(XOp::CreateWorkDir {
            extract: extract.to_path_buf(),
            prefix: prefix.to_owned(),
        })?;
        self.inner.create_work_dir(extract, prefix)
    }

    fn create_dir(&self, path: &Path) -> io::Result<()> {
        self.mutate(XOp::CreateDir(path.to_path_buf()))?;
        self.inner.create_dir(path)
    }

    fn create_file(&self, path: &Path) -> io::Result<Box<dyn ExtractFile>> {
        self.mutate(XOp::CreateFile(path.to_path_buf()))?;
        let inner = self.inner.create_file(path)?;
        Ok(self.rec.file(inner, path))
    }

    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()> {
        self.mutate(XOp::SetMode(path.to_path_buf(), mode))?;
        self.inner.set_mode(path, mode)
    }

    fn symlink(&self, target: &str, link: &Path) -> io::Result<()> {
        self.mutate(XOp::Symlink {
            target: target.to_owned(),
            link: link.to_path_buf(),
        })?;
        self.inner.symlink(target, link)
    }

    fn supports_symlinks(&self) -> bool {
        self.inner.supports_symlinks()
    }

    fn remove_tree(&self, path: &Path) -> io::Result<()> {
        self.mutate(XOp::RemoveTree(path.to_path_buf()))?;
        self.inner.remove_tree(path)
    }

    fn remove_entry(&self, path: &Path, kind: NodeKind) -> io::Result<()> {
        self.mutate(XOp::RemoveEntry(path.to_path_buf(), kind))?;
        self.inner.remove_entry(path, kind)
    }

    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>> {
        self.rec.record(XOp::KindOf(path.to_path_buf()))?;
        let inner = self.inner.kind_of(path);
        match &self.on_kind_of {
            Some(f) => f(path, inner),
            None => inner,
        }
    }

    fn read_tree(&self, root: &Path) -> io::Result<Vec<ListingEntry>> {
        self.rec.record(XOp::ReadTree(root.to_path_buf()))?;
        let mut entries = self.inner.read_tree(root)?;
        if let Some(f) = &self.on_read_tree {
            f(root, &mut entries);
        }
        Ok(entries)
    }

    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        self.rec.record(XOp::Canonicalize(path.to_path_buf()))?;
        let inner = self.inner.canonicalize(path);
        match &self.on_canonicalize {
            Some(f) => f(path, inner),
            None => inner,
        }
    }

    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        self.rec.record(XOp::ReadFile(path.to_path_buf()))?;
        self.inner.read_file(path, max)
    }
}

/// Every node under `root` as (relative path, `f` / `d` / `l`, length), never following a link,
/// sorted (SC-006).
pub fn snapshot(root: &Path) -> Vec<(String, char, u64)> {
    fn walk(dir: &Path, prefix: &str, out: &mut Vec<(String, char, u64)>) {
        let entries = std::fs::read_dir(dir)
            .unwrap_or_else(|e| panic!("snapshot cannot list {}: {e}", dir.display()));
        for entry in entries {
            let entry =
                entry.unwrap_or_else(|e| panic!("snapshot cannot read in {}: {e}", dir.display()));
            let path = entry.path();
            let meta = std::fs::symlink_metadata(&path)
                .unwrap_or_else(|e| panic!("snapshot cannot stat {}: {e}", path.display()));
            let name = entry.file_name().to_string_lossy().into_owned();
            let rel = if prefix.is_empty() {
                name
            } else {
                format!("{prefix}/{name}")
            };
            let file_type = meta.file_type();
            if file_type.is_symlink() {
                out.push((rel, 'l', meta.len()));
            } else if file_type.is_dir() {
                out.push((rel.clone(), 'd', meta.len()));
                walk(&path, &rel, out);
            } else {
                out.push((rel, 'f', meta.len()));
            }
        }
    }
    let mut out = Vec::new();
    walk(root, "", &mut out);
    out.sort();
    out
}

#[cfg(test)]
#[path = "test_support_tests.rs"]
mod tests;
