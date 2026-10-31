//! Temp-sandboxed roots for tests (FR-020, FR-022). Compiled only under
//! `cfg(any(test, feature = "test-support"))`.

use crate::context::AppContext;
use crate::detect::{HostFacts, InnoValues, PkgOutputs, RecordRead};
use crate::download::{
    CancelToken, DownloadEnv, DownloadPolicy, EntryKind, FsStagingIo, PartialFile, StagedEntry,
    StagingIo,
};
use crate::feed::{self, Feed, PlainName, PlatformKey};
use crate::platform::{HostIo, NativePlatform, Platform, RegistryRead, RunOutcome};
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, Roots, STAGING_DIR};
use crate::verify::{SigningKey, hex_lower};
use crate::version::{self, Arch, OsVersion};
use log::{Level, LevelFilter, Log, Metadata, Record};
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, OnceLock, PoisonError};
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

/// The guard first (panic before any directory exists), then every root as a subdirectory
/// of the canonical base, one `join` per component (PD-6), sandboxed to that base (PD-24).
fn temp_roots_in(base: &Path, temp_root: &Path) -> Roots {
    let canonical_base = match check_under_temp(base, temp_root) {
        Ok(b) => b,
        Err(e) => panic!(
            "temp_roots refused: base {} is not under temp root {}: {e}",
            base.display(),
            temp_root.display()
        ),
    };
    let plugins = canonical_base.join("plugins");
    let presets = canonical_base.join("presets");
    let state_dir = canonical_base.join("state");
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
        .sandboxed(canonical_base);
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

#[cfg(test)]
#[path = "test_support_tests.rs"]
mod tests;
