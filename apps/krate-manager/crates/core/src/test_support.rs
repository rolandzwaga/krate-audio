//! Temp-sandboxed roots for tests (FR-020, FR-022). Compiled only under
//! `cfg(any(test, feature = "test-support"))`.

use crate::context::AppContext;
use crate::detect::{HostFacts, InnoValues, PkgOutputs, RecordRead};
use crate::download::{
    CancelToken, DownloadEnv, DownloadPolicy, EntryKind, FsStagingIo, PartialFile, StagedEntry,
    StagingIo,
};
use crate::extract::{
    ArchiveRead, EntryType, ExtractEnv, ExtractFile, ExtractIo, ExtractLimits, FsExtractIo,
    ListingEntry, NodeKind,
};
use crate::feed::{self, Feed, PlainName, PlatformKey};
use crate::install::{
    Action, ActionKind, ActionResult, CommandEnv, DriverResolver, ElevatedCommand, Elevation,
    ElevationGuard, FsInstallIo, InstallDriver, InstallEnv, InstallIo, KeptPath, LaunchOutcome,
    Outcome,
};
use crate::platform::{
    APP_IDENTIFIER, HostIo, NativePlatform, Os, Platform, RegistryRead, RunOutcome,
};
use crate::roots::{PluginRoot, PresetRoots, RootKind, RootPaths, Roots, STAGING_DIR};
use crate::verify::{Sha256Stream, SigningKey, hex_lower};
use crate::version::{self, Arch, OsVersion};
use log::{Level, LevelFilter, Log, Metadata, Record};
use std::cell::RefCell;
use std::collections::btree_map::Entry;
use std::collections::{BTreeMap, VecDeque};
use std::ffi::{OsStr, OsString};
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
    roots_at_with(base, None)
}

/// Every directory `roots_at` creates: the plug-in roots, both preset roots, the state and staging dirs.
fn root_dirs(roots: &Roots) -> Vec<&Path> {
    roots
        .plugin_roots()
        .iter()
        .map(|r| r.path.as_path())
        .chain([
            roots.presets().factory.as_path(),
            roots.presets().user.as_path(),
            roots.state_dir(),
            roots.staging_dir(),
        ])
        .collect()
}

/// `roots_at`, with `presets().user` spelled `<base>/presets/<leaf>` and not created when `user_leaf` is `Some`
/// (SI-15).
fn roots_at_with(base: &Path, user_leaf: Option<&OsStr>) -> Roots {
    roots_at_state(base, user_leaf, OsStr::new("state"))
}

/// `roots_at_with`, with the state dir spelled `<base>/<state_leaf>` (Phase 10 SI-21).
fn roots_at_state(base: &Path, user_leaf: Option<&OsStr>, state_leaf: &OsStr) -> Roots {
    let plugins = base.join("plugins");
    let presets = base.join("presets");
    let state_dir = base.join(state_leaf);
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
            user: presets.join(user_leaf.unwrap_or(OsStr::new("user"))),
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
    for dir in root_dirs(&roots) {
        if user_leaf.is_some() && dir == roots.presets().user {
            continue;
        }
        std::fs::create_dir_all(dir)
            .unwrap_or_else(|e| panic!("temp_roots cannot create {}: {e}", dir.display()));
    }
    roots
}

/// SC-023 (SI-15): roots on `td` whose `presets().user` is `<canonical td>/presets/<leaf>`, sandboxed to the
/// canonical `td`; that one root is not created. `td` is checked first (panic before anything is created).
#[cfg(unix)]
pub fn temp_roots_with_user_presets(td: &Path, leaf: &OsStr) -> Roots {
    let canonical = check_under_temp(td, &canonical_temp_dir())
        .unwrap_or_else(|e| panic!("temp_roots_with_user_presets refused: {e}"));
    roots_at_with(&canonical, Some(leaf))
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

/// Phase 10 SI-21: `fresh_temp_roots`' layout with the state dir `<base>/<APP_IDENTIFIER>` (the last component the
/// helper requires, FR-021) and staging under it; keep the `TempDir` alive while the roots are used.
pub fn fresh_elevated_roots() -> (tempfile::TempDir, Roots) {
    let td = tempfile::TempDir::new().expect("cannot create a TempDir");
    let canonical = check_under_temp(td.path(), &canonical_temp_dir())
        .unwrap_or_else(|e| panic!("fresh_elevated_roots refused: {e}"));
    let roots = roots_at_state(&canonical, None, OsStr::new(APP_IDENTIFIER));
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

/// Phase 10 (plan §4.4): `HostFacts` whose records a loopback effect can change; `Sync`, keyed by the `PlainName`
/// string; an unlisted key is `Absent`, as in `FakeHostFacts`.
pub struct LiveHostFacts {
    pub os_version: Option<OsVersion>,
    pub inno: Mutex<BTreeMap<String, RecordRead<InnoValues>>>,
    pub pkg: Mutex<BTreeMap<String, RecordRead<PkgOutputs>>>,
}

impl LiveHostFacts {
    pub fn new(os_version: Option<OsVersion>) -> LiveHostFacts {
        LiveHostFacts {
            os_version,
            inno: Mutex::new(BTreeMap::new()),
            pkg: Mutex::new(BTreeMap::new()),
        }
    }
}

impl HostFacts for LiveHostFacts {
    fn os_version(&self) -> Option<OsVersion> {
        self.os_version
    }

    fn inno_values(&self, key: &PlainName) -> RecordRead<InnoValues> {
        self.inno
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .get(key.as_str())
            .cloned()
            .unwrap_or(RecordRead::Absent)
    }

    fn pkg_outputs(&self, pkg_id: &PlainName) -> RecordRead<PkgOutputs> {
        self.pkg
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .get(pkg_id.as_str())
            .cloned()
            .unwrap_or(RecordRead::Absent)
    }
}

/// What a scripted launch writes as `<job stem>.result.json` beside the job, given the job's path (`None`: nothing).
pub type ScriptedResult = Box<dyn Fn(&Path) -> Option<Vec<u8>> + Send>;

/// One scripted launch of `FakeElevation` (plan §4.4).
pub struct Scripted {
    pub outcome: LaunchOutcome,
    pub result: Option<ScriptedResult>,
}

/// Phase 10 (plan §4.4, PD-9): an `Elevation` that never starts a process. Every `launch` reads the job named by
/// `args[1]` into `jobs`, records the command, then answers with the next scripted launch (writing its result file),
/// or with `otherwise` when none is left. Its guard is its own, so parallel tests never share one.
pub struct FakeElevation {
    pub helper: Option<PathBuf>,
    pub script: Mutex<VecDeque<Scripted>>,
    pub launches: Mutex<Vec<(ElevatedCommand, Duration)>>,
    pub jobs: Mutex<Vec<Vec<u8>>>,
    /// The outcome of a launch with no scripted entry left.
    pub otherwise: LaunchOutcome,
    guard: ElevationGuard,
}

/// The helper path a `FakeElevation` reports by default: a bare name, never executed.
const FAKE_HELPER: &str = "fake-krate-manager-helper";

impl FakeElevation {
    fn with_outcome(helper: Option<PathBuf>, otherwise: LaunchOutcome) -> FakeElevation {
        FakeElevation {
            helper,
            script: Mutex::new(VecDeque::new()),
            launches: Mutex::new(Vec::new()),
            jobs: Mutex::new(Vec::new()),
            otherwise,
            guard: ElevationGuard::new(),
        }
    }

    /// Every launch is `Refused`.
    pub fn refusing() -> FakeElevation {
        FakeElevation::with_outcome(Some(PathBuf::from(FAKE_HELPER)), LaunchOutcome::Refused)
    }

    /// Every unscripted launch is `outcome`.
    pub fn returning(outcome: LaunchOutcome) -> FakeElevation {
        FakeElevation::with_outcome(Some(PathBuf::from(FAKE_HELPER)), outcome)
    }

    /// No helper beside the manager (`helper()` is `None`).
    pub fn missing() -> FakeElevation {
        FakeElevation::with_outcome(None, LaunchOutcome::Failed("E_ELEVATION_NONE"))
    }

    pub fn with_helper(self, path: PathBuf) -> FakeElevation {
        FakeElevation {
            helper: Some(path),
            ..self
        }
    }

    pub fn push(&self, s: Scripted) {
        self.script
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push_back(s);
    }

    pub fn launches(&self) -> Vec<(ElevatedCommand, Duration)> {
        self.launches
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .clone()
    }

    pub fn jobs(&self) -> Vec<Vec<u8>> {
        self.jobs
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .clone()
    }
}

impl Elevation for FakeElevation {
    fn helper(&self) -> Option<&Path> {
        self.helper.as_deref()
    }

    fn launch(&self, command: &ElevatedCommand, timeout: Duration) -> LaunchOutcome {
        let job = command
            .args
            .get(1)
            .map(PathBuf::from)
            .unwrap_or_else(|| panic!("FakeElevation: no job argument in {:?}", command.args));
        let bytes = std::fs::read(&job)
            .unwrap_or_else(|e| panic!("FakeElevation cannot read the job {}: {e}", job.display()));
        self.jobs
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push(bytes);
        self.launches
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push((command.clone(), timeout));
        let next = self
            .script
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .pop_front();
        let Some(scripted) = next else {
            return self.otherwise;
        };
        if let Some(result) = &scripted.result
            && let Some(bytes) = result(&job)
        {
            let stem = job
                .file_stem()
                .map(|s| s.to_string_lossy().into_owned())
                .unwrap_or_default();
            let path = job.with_file_name(format!("{stem}.result.json"));
            std::fs::write(&path, bytes)
                .unwrap_or_else(|e| panic!("FakeElevation cannot write {}: {e}", path.display()));
        }
        scripted.outcome
    }

    fn guard(&self) -> &ElevationGuard {
        &self.guard
    }
}

/// Phase 10: a `FakeHostIo` shared across threads (the `InstallRig` field); each call locks it.
impl HostIo for Mutex<FakeHostIo> {
    fn run(&self, argv: &[&str], bound: Duration) -> RunOutcome {
        self.lock()
            .unwrap_or_else(PoisonError::into_inner)
            .run(argv, bound)
    }

    fn registry_values(&self, subkey: &str, names: [&str; 2]) -> RegistryRead {
        self.lock()
            .unwrap_or_else(PoisonError::into_inner)
            .registry_values(subkey, names)
    }

    fn rtl_version(&self) -> Option<(u32, u32, u32)> {
        self.lock()
            .unwrap_or_else(PoisonError::into_inner)
            .rtl_version()
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

/// What a resolver sees at one key, never following a link.
enum Seen<'t> {
    Dir,
    File,
    Link(&'t str),
}

/// `mem_kind`'s rule, with a link's target: a proper ancestor of `base` is a directory; any other key outside
/// `base` is absent.
fn mem_seen<'t>(base: &Path, tree: &'t MemTree, key: &Path) -> Option<Seen<'t>> {
    if is_ancestor(base, key) {
        return Some(Seen::Dir);
    }
    if !key.starts_with(base) {
        return None;
    }
    tree.get(key).map(|node| match node {
        MemNode::Dir { .. } => Seen::Dir,
        MemNode::File { .. } => Seen::File,
        MemNode::Link { target } => Seen::Link(target.as_str()),
    })
}

/// The children of the directory `key`, by name: the stored nodes inside `base` plus, for a proper ancestor of
/// `base`, the next directory on the way down to it.
fn mem_children(base: &Path, tree: &MemTree, key: &Path) -> BTreeMap<OsString, NodeKind> {
    let mut children = BTreeMap::new();
    for (path, node) in tree.iter() {
        if path.parent() == Some(key)
            && path.starts_with(base)
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
    if is_ancestor(base, key)
        && let Some(next) = base
            .strip_prefix(key)
            .ok()
            .and_then(|rest| rest.components().next())
    {
        children.insert(next.as_os_str().to_owned(), NodeKind::Dir);
    }
    children
}

/// Puts `node` at `path` as spelled (replacing any node there), with any missing parent directory under `base`
/// as mode `0o755`.
fn mem_plant(base: &Path, tree: &mut MemTree, path: &Path, node: MemNode) {
    for ancestor in path.ancestors().skip(1) {
        if ancestor == base || !ancestor.starts_with(base) {
            break;
        }
        tree.entry(ancestor.to_path_buf())
            .or_insert(MemNode::Dir { mode: 0o755 });
    }
    tree.insert(path.to_path_buf(), node);
}

/// One component of a path being resolved.
enum Step {
    Up,
    Name(OsString),
}

/// `realpath` over the tree: links resolved component by component (`/`-separated targets,
/// relative to the link's directory); an absent component, a dangling link included, is `NotFound`.
fn mem_resolve(base: &Path, tree: &MemTree, path: &Path) -> io::Result<PathBuf> {
    resolve_with(path, &|key| mem_seen(base, tree, key))
}

/// `mem_resolve` over any lookup (`look` answers one key, never following a link).
fn resolve_with<'t>(path: &Path, look: &dyn Fn(&Path) -> Option<Seen<'t>>) -> io::Result<PathBuf> {
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
        match look(&candidate) {
            None => return Err(not_found()),
            Some(Seen::Dir) => out = candidate,
            Some(Seen::File) => {
                if !rest.is_empty() {
                    return Err(not_a_directory());
                }
                out = candidate;
            }
            Some(Seen::Link(target)) => {
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
        mem_plant(&self.base, &mut self.lock(), path, node);
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
        Ok(Some(
            mem_children(&self.base, &tree, &key).into_iter().collect(),
        ))
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

// Phase 9 test doubles (plan §4.5, FR-115; tasks TA-3, TA-5). The surface is T001's; the bodies are T004's.

/// The kind of one recorded `InstallIo` call (`supports_symlinks` is not recorded).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IKind {
    KindOf,
    ReadLink,
    HashFile,
    ReadDir,
    ReadFile,
    Canonicalize,
    CreateDirAll,
    CreateDir,
    CopyNew,
    WriteNew,
    Symlink,
    SetMode,
    SyncFile,
    SyncDir,
    Rename,
    RemoveFile,
    RemoveDir,
    RemoveTree,
}

impl IKind {
    /// The twelve FR-028 mutating kinds, `CreateDirAll` to `RemoveTree`.
    pub fn is_mutating(self) -> bool {
        !matches!(
            self,
            IKind::KindOf
                | IKind::ReadLink
                | IKind::HashFile
                | IKind::ReadDir
                | IKind::ReadFile
                | IKind::Canonicalize
        )
    }
}

/// One recorded `InstallIo` call with its arguments (plan §4.5).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum IOp {
    KindOf(PathBuf),
    ReadLink(PathBuf),
    HashFile(PathBuf),
    ReadDir(PathBuf),
    ReadFile(PathBuf),
    Canonicalize(PathBuf),
    CreateDirAll(PathBuf),
    CreateDir(PathBuf),
    CopyNew { src: PathBuf, dest: PathBuf },
    WriteNew(PathBuf),
    Symlink { target: String, link: PathBuf },
    SetMode(PathBuf, u32),
    SyncFile(PathBuf),
    SyncDir(PathBuf),
    Rename { from: PathBuf, to: PathBuf },
    RemoveFile(PathBuf),
    RemoveDir(PathBuf),
    RemoveTree(PathBuf),
}

impl IOp {
    /// The call's kind.
    pub fn kind(&self) -> IKind {
        match self {
            IOp::KindOf(_) => IKind::KindOf,
            IOp::ReadLink(_) => IKind::ReadLink,
            IOp::HashFile(_) => IKind::HashFile,
            IOp::ReadDir(_) => IKind::ReadDir,
            IOp::ReadFile(_) => IKind::ReadFile,
            IOp::Canonicalize(_) => IKind::Canonicalize,
            IOp::CreateDirAll(_) => IKind::CreateDirAll,
            IOp::CreateDir(_) => IKind::CreateDir,
            IOp::CopyNew { .. } => IKind::CopyNew,
            IOp::WriteNew(_) => IKind::WriteNew,
            IOp::Symlink { .. } => IKind::Symlink,
            IOp::SetMode(..) => IKind::SetMode,
            IOp::SyncFile(_) => IKind::SyncFile,
            IOp::SyncDir(_) => IKind::SyncDir,
            IOp::Rename { .. } => IKind::Rename,
            IOp::RemoveFile(_) => IKind::RemoveFile,
            IOp::RemoveDir(_) => IKind::RemoveDir,
            IOp::RemoveTree(_) => IKind::RemoveTree,
        }
    }

    /// True for the twelve FR-028 mutating kinds.
    pub fn is_mutating(&self) -> bool {
        self.kind().is_mutating()
    }
}

/// The number of `IKind` variants (one counter each in an `InstallLog`).
const IKIND_COUNT: usize = 18;

/// The call record of one Phase 9 double.
#[derive(Default)]
struct InstallLog {
    calls: Vec<IOp>,
    per_kind: [usize; IKIND_COUNT],
    mutating: usize,
}

/// Records every call of one `InstallIo` double, in order.
#[derive(Default)]
struct InstallRecorder {
    log: Mutex<InstallLog>,
}

impl InstallRecorder {
    /// Records `op`; returns its 1-based number among the calls of its kind and, for a mutating call, among the
    /// mutating calls.
    fn record(&self, op: IOp) -> (usize, Option<usize>) {
        let kind = op.kind();
        let mut log = self.log.lock().unwrap_or_else(PoisonError::into_inner);
        log.calls.push(op);
        log.per_kind[kind as usize] += 1;
        let nth = log.per_kind[kind as usize];
        let mutating = kind.is_mutating().then(|| {
            log.mutating += 1;
            log.mutating
        });
        (nth, mutating)
    }

    fn calls(&self) -> Vec<IOp> {
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
            .mutating
    }
}

/// The error of a mutating `MemInstallIo` call on the staged tree.
fn staged_read_only() -> io::Error {
    io::Error::new(
        io::ErrorKind::PermissionDenied,
        "the staged tree is read-only",
    )
}

/// What a `MemInstallIo` sees: its own tree, and the staged tree for every key strictly under the staging dir.
struct InstallView<'t> {
    base: &'t Path,
    tree: &'t MemTree,
    staging: &'t Path,
    staged_base: &'t Path,
    staged: &'t MemTree,
}

impl<'t> InstallView<'t> {
    /// The base and tree that answer for `key`.
    fn region(&self, key: &Path) -> (&'t Path, &'t MemTree) {
        if key != self.staging && key.starts_with(self.staging) {
            (self.staged_base, self.staged)
        } else {
            (self.base, self.tree)
        }
    }

    fn seen(&self, key: &Path) -> Option<Seen<'t>> {
        let (base, tree) = self.region(key);
        mem_seen(base, tree, key)
    }

    fn kind(&self, key: &Path) -> Option<NodeKind> {
        self.seen(key).map(|seen| match seen {
            Seen::Dir => NodeKind::Dir,
            Seen::File => NodeKind::File,
            Seen::Link(_) => NodeKind::Symlink,
        })
    }

    /// The stored node at `key` (none for a proper ancestor of a base).
    fn node(&self, key: &Path) -> Option<&'t MemNode> {
        let (base, tree) = self.region(key);
        if key.starts_with(base) {
            tree.get(key)
        } else {
            None
        }
    }

    /// The children of the directory `key`; every child of the staging dir or below is a staged key.
    fn children(&self, key: &Path) -> BTreeMap<OsString, NodeKind> {
        let (base, tree) = if key.starts_with(self.staging) {
            (self.staged_base, self.staged)
        } else {
            (self.base, self.tree)
        };
        mem_children(base, tree, key)
    }

    /// `realpath`: every link followed.
    fn resolve(&self, path: &Path) -> io::Result<PathBuf> {
        resolve_with(path, &|key| self.seen(key))
    }

    /// The key of `path`'s last component with every parent link resolved (`symlink_metadata`).
    fn locate(&self, path: &Path) -> io::Result<PathBuf> {
        match (path.parent(), path.file_name()) {
            (Some(parent), Some(name)) => Ok(self.resolve(parent)?.join(name)),
            _ => self.resolve(path),
        }
    }

    /// The bytes of the regular file at the resolved `key`.
    fn file_bytes(&self, key: &Path) -> io::Result<&'t [u8]> {
        match self.node(key) {
            Some(MemNode::File { bytes, .. }) => Ok(bytes.as_slice()),
            _ if self.kind(key) == Some(NodeKind::Dir) => {
                Err(io::Error::from(io::ErrorKind::IsADirectory))
            }
            _ => Err(not_found()),
        }
    }
}

/// An in-memory `InstallIo` (plan §4.5, D12; tasks TA-5) over `MemNode`s; reads under the roots' staging dir are
/// served by a `MemExtractIo`.
///
/// Every lookup follows a link in a non-final component and leaves the final one unfollowed, as `symlink_metadata`
/// does; `canonicalize`, `read_file`, `read_dir`, `set_mode`, `sync_*` and the `copy_new` source follow every link.
/// New directories get mode `0o755`, new files `0o644`. `rename` and `remove_dir` keep the Unix rules (a directory
/// target must be empty, `DirectoryNotEmpty`). Every mutating call on the staged tree (the staging dir, anything
/// under it, or a directory above it for `rename` / `remove_tree`) is `PermissionDenied` and changes nothing. The
/// staged tree is read without calling the `MemExtractIo`, so its record stays the extraction's.
pub struct MemInstallIo<'a> {
    base: PathBuf,
    staging: PathBuf,
    staged: &'a MemExtractIo,
    tree: Mutex<MemTree>,
    rec: InstallRecorder,
}

impl<'a> MemInstallIo<'a> {
    /// Plants `base` and every directory `roots_at` creates under it; reads under `roots.staging_dir()` are
    /// served by `staged` (read-only); proper ancestors of `base` exist and canonicalise to themselves (TA-5).
    pub fn new(base: &Path, roots: &Roots, staged: &'a MemExtractIo) -> MemInstallIo<'a> {
        let io = MemInstallIo {
            base: base.to_path_buf(),
            staging: roots.staging_dir().to_path_buf(),
            staged,
            tree: Mutex::new(MemTree::from([(
                base.to_path_buf(),
                MemNode::Dir { mode: 0o755 },
            )])),
            rec: InstallRecorder::default(),
        };
        for dir in root_dirs(roots) {
            io.plant_dir(dir, 0o755);
        }
        io
    }

    fn lock(&self) -> MutexGuard<'_, MemTree> {
        self.tree.lock().unwrap_or_else(PoisonError::into_inner)
    }

    fn view<'t>(&'t self, tree: &'t MemTree, staged: &'t MemTree) -> InstallView<'t> {
        InstallView {
            base: &self.base,
            tree,
            staging: &self.staging,
            staged_base: &self.staged.base,
            staged,
        }
    }

    /// True for the staging dir and every path under it.
    fn read_only(&self, path: &Path) -> bool {
        path.starts_with(&self.staging)
    }

    /// `read_only`, or a directory above the staging dir (a subtree move or removal would take it along).
    fn holds_staged(&self, path: &Path) -> bool {
        self.read_only(path) || self.staging.starts_with(path)
    }

    /// Puts `node` at `path` as spelled, with any missing parent directory as mode `0o755`. Not recorded.
    fn plant(&self, path: &Path, node: MemNode) {
        assert!(
            path.starts_with(&self.base),
            "MemInstallIo: {} is outside the base {}",
            path.display(),
            self.base.display()
        );
        assert!(
            path == self.staging || !self.read_only(path),
            "MemInstallIo: plant {} in the MemExtractIo, the staged tree is its",
            path.display()
        );
        mem_plant(&self.base, &mut self.lock(), path, node);
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

    /// The node at `path` as spelled, no link resolution (a staged path answers from the staged tree).
    pub fn node(&self, path: &Path) -> Option<MemNode> {
        if path != self.staging && self.read_only(path) {
            self.staged.node(path)
        } else {
            self.lock().get(path).cloned()
        }
    }

    /// Every path strictly under `root`, sorted (staged paths included).
    pub fn paths_under(&self, root: &Path) -> Vec<PathBuf> {
        let tree = self.lock();
        let staged = self.staged.lock();
        let mut paths: Vec<PathBuf> = tree
            .keys()
            .chain(
                staged
                    .keys()
                    .filter(|key| key.as_path() != self.staging && self.read_only(key)),
            )
            .filter(|key| key.as_path() != root && key.starts_with(root))
            .cloned()
            .collect();
        paths.sort();
        paths.dedup();
        paths
    }

    pub fn calls(&self) -> Vec<IOp> {
        self.rec.calls()
    }

    pub fn mutating_calls(&self) -> usize {
        self.rec.mutating_calls()
    }

    /// Inserts `node` at `path`: the name must be absent (a dangling link included, `create_new`), inside `base`
    /// and off the staged tree, with a parent directory.
    fn create(
        &self,
        tree: &mut MemTree,
        staged: &MemTree,
        path: &Path,
        node: MemNode,
    ) -> io::Result<()> {
        let view = self.view(tree, staged);
        let key = view.locate(path)?;
        if self.read_only(&key) {
            return Err(staged_read_only());
        }
        if view.seen(&key).is_some() {
            return Err(io::Error::from(io::ErrorKind::AlreadyExists));
        }
        if !key.starts_with(&self.base) {
            return Err(not_found());
        }
        let parent = key.parent().ok_or_else(not_found)?;
        if view.kind(parent) != Some(NodeKind::Dir) {
            return Err(not_a_directory());
        }
        tree.insert(key, node);
        Ok(())
    }

    /// `create_dir_all` over the view: an existing directory (through links) is fine, anything else in the way is
    /// `AlreadyExists`.
    fn create_dirs(&self, tree: &mut MemTree, staged: &MemTree, path: &Path) -> io::Result<()> {
        let resolved = self.view(tree, staged).resolve(path);
        match resolved {
            Ok(key) if self.view(tree, staged).kind(&key) == Some(NodeKind::Dir) => Ok(()),
            Ok(_) => Err(io::Error::from(io::ErrorKind::AlreadyExists)),
            Err(e) if e.kind() == io::ErrorKind::NotFound => {
                if let Some(parent) = path.parent() {
                    self.create_dirs(tree, staged, parent)?;
                }
                self.create(tree, staged, path, MemNode::Dir { mode: 0o755 })
            }
            Err(e) => Err(e),
        }
    }

    /// The resolved key of an existing `path` that a mutating call may act on.
    fn resolve_writable(
        &self,
        tree: &MemTree,
        staged: &MemTree,
        path: &Path,
    ) -> io::Result<PathBuf> {
        if self.read_only(path) {
            return Err(staged_read_only());
        }
        let key = self.view(tree, staged).resolve(path)?;
        if self.read_only(&key) {
            return Err(staged_read_only());
        }
        Ok(key)
    }

    /// The located key (final link unfollowed) of `path` for a removal or rename, refused on the staged tree.
    fn locate_writable(
        &self,
        tree: &MemTree,
        staged: &MemTree,
        path: &Path,
    ) -> io::Result<PathBuf> {
        if self.holds_staged(path) {
            return Err(staged_read_only());
        }
        let key = self.view(tree, staged).locate(path)?;
        if self.holds_staged(&key) {
            return Err(staged_read_only());
        }
        Ok(key)
    }
}

impl InstallIo for MemInstallIo<'_> {
    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>> {
        self.rec.record(IOp::KindOf(path.to_path_buf()));
        let (tree, staged) = (self.lock(), self.staged.lock());
        let view = self.view(&tree, &staged);
        match view.locate(path) {
            Ok(key) => Ok(view.kind(&key)),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e),
        }
    }

    fn read_link(&self, path: &Path) -> io::Result<PathBuf> {
        self.rec.record(IOp::ReadLink(path.to_path_buf()));
        let (tree, staged) = (self.lock(), self.staged.lock());
        let view = self.view(&tree, &staged);
        let key = view.locate(path)?;
        match view.seen(&key) {
            Some(Seen::Link(target)) => Ok(PathBuf::from(target)),
            Some(_) => Err(io::Error::from(io::ErrorKind::InvalidInput)),
            None => Err(not_found()),
        }
    }

    /// A regular file only, never through a final link (`InvalidInput`).
    fn hash_file(&self, path: &Path) -> io::Result<[u8; 32]> {
        self.rec.record(IOp::HashFile(path.to_path_buf()));
        let (tree, staged) = (self.lock(), self.staged.lock());
        let view = self.view(&tree, &staged);
        let key = view.locate(path)?;
        match (view.node(&key), view.kind(&key)) {
            (Some(MemNode::File { bytes, .. }), _) => {
                let mut sha = Sha256Stream::new();
                sha.update(bytes);
                Ok(sha.finish())
            }
            (_, Some(_)) => Err(io::Error::from(io::ErrorKind::InvalidInput)),
            (_, None) => Err(not_found()),
        }
    }

    fn read_dir(&self, dir: &Path) -> io::Result<Vec<(OsString, NodeKind)>> {
        self.rec.record(IOp::ReadDir(dir.to_path_buf()));
        let (tree, staged) = (self.lock(), self.staged.lock());
        let view = self.view(&tree, &staged);
        let key = view.resolve(dir)?;
        if view.kind(&key) != Some(NodeKind::Dir) {
            return Err(not_a_directory());
        }
        Ok(view.children(&key).into_iter().collect())
    }

    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        self.rec.record(IOp::ReadFile(path.to_path_buf()));
        let (tree, staged) = (self.lock(), self.staged.lock());
        let view = self.view(&tree, &staged);
        let bytes = view.file_bytes(&view.resolve(path)?)?;
        let limit = usize::try_from(max.saturating_add(1)).unwrap_or(usize::MAX);
        Ok(bytes[..bytes.len().min(limit)].to_vec())
    }

    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        self.rec.record(IOp::Canonicalize(path.to_path_buf()));
        let (tree, staged) = (self.lock(), self.staged.lock());
        self.view(&tree, &staged).resolve(path)
    }

    fn supports_symlinks(&self) -> bool {
        true
    }

    /// Then the final component, as spelled, must be a directory (a link to one is `InvalidInput`, the
    /// `FsInstallIo` rule).
    fn create_dir_all(&self, path: &Path) -> io::Result<()> {
        self.rec.record(IOp::CreateDirAll(path.to_path_buf()));
        if self.read_only(path) {
            return Err(staged_read_only());
        }
        let (mut tree, staged) = (self.lock(), self.staged.lock());
        self.create_dirs(&mut tree, &staged, path)?;
        let view = self.view(&tree, &staged);
        if view.kind(&view.locate(path)?) != Some(NodeKind::Dir) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "not a directory after create_dir_all",
            ));
        }
        Ok(())
    }

    fn create_dir(&self, path: &Path) -> io::Result<()> {
        self.rec.record(IOp::CreateDir(path.to_path_buf()));
        if self.read_only(path) {
            return Err(staged_read_only());
        }
        let (mut tree, staged) = (self.lock(), self.staged.lock());
        self.create(&mut tree, &staged, path, MemNode::Dir { mode: 0o755 })
    }

    /// The source is read through every link (staged or not); the bytes reach `sink` in 64 KiB chunks.
    fn copy_new(&self, src: &Path, dest: &Path, sink: &mut dyn FnMut(&[u8])) -> io::Result<u64> {
        self.rec.record(IOp::CopyNew {
            src: src.to_path_buf(),
            dest: dest.to_path_buf(),
        });
        if self.read_only(dest) {
            return Err(staged_read_only());
        }
        let bytes = {
            let (mut tree, staged) = (self.lock(), self.staged.lock());
            let view = self.view(&tree, &staged);
            let bytes = view.file_bytes(&view.resolve(src)?)?.to_vec();
            self.create(
                &mut tree,
                &staged,
                dest,
                MemNode::File {
                    bytes: bytes.clone(),
                    mode: 0o644,
                },
            )?;
            bytes
        };
        for chunk in bytes.chunks(64 << 10) {
            sink(chunk);
        }
        Ok(u64::try_from(bytes.len()).unwrap_or(u64::MAX))
    }

    fn write_new(&self, path: &Path, bytes: &[u8]) -> io::Result<()> {
        self.rec.record(IOp::WriteNew(path.to_path_buf()));
        if self.read_only(path) {
            return Err(staged_read_only());
        }
        let (mut tree, staged) = (self.lock(), self.staged.lock());
        let node = MemNode::File {
            bytes: bytes.to_vec(),
            mode: 0o644,
        };
        self.create(&mut tree, &staged, path, node)
    }

    fn symlink(&self, target: &str, link: &Path) -> io::Result<()> {
        self.rec.record(IOp::Symlink {
            target: target.to_owned(),
            link: link.to_path_buf(),
        });
        if self.read_only(link) {
            return Err(staged_read_only());
        }
        let (mut tree, staged) = (self.lock(), self.staged.lock());
        let node = MemNode::Link {
            target: target.to_owned(),
        };
        self.create(&mut tree, &staged, link, node)
    }

    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()> {
        self.rec.record(IOp::SetMode(path.to_path_buf(), mode));
        let (mut tree, staged) = (self.lock(), self.staged.lock());
        let key = self.resolve_writable(&tree, &staged, path)?;
        match tree.get_mut(&key) {
            Some(MemNode::Dir { mode: m } | MemNode::File { mode: m, .. }) => {
                *m = mode;
                Ok(())
            }
            _ => Err(not_found()),
        }
    }

    /// A regular file (through links); nothing changes.
    fn sync_file(&self, path: &Path) -> io::Result<()> {
        self.rec.record(IOp::SyncFile(path.to_path_buf()));
        let (tree, staged) = (self.lock(), self.staged.lock());
        let key = self.resolve_writable(&tree, &staged, path)?;
        self.view(&tree, &staged).file_bytes(&key).map(|_| ())
    }

    /// An existing directory (through links); nothing changes.
    fn sync_dir(&self, dir: &Path) -> io::Result<()> {
        self.rec.record(IOp::SyncDir(dir.to_path_buf()));
        let (tree, staged) = (self.lock(), self.staged.lock());
        let key = self.resolve_writable(&tree, &staged, dir)?;
        match self.view(&tree, &staged).kind(&key) {
            Some(NodeKind::Dir) => Ok(()),
            _ => Err(not_a_directory()),
        }
    }

    /// The Unix rules: neither name's final link is followed; a directory moves with its subtree and replaces only
    /// an empty directory (`DirectoryNotEmpty`), never a non-directory (`NotADirectory`); a non-directory never
    /// replaces a directory (`IsADirectory`); a directory never moves into itself (`InvalidInput`).
    fn rename(&self, from: &Path, to: &Path) -> io::Result<()> {
        self.rec.record(IOp::Rename {
            from: from.to_path_buf(),
            to: to.to_path_buf(),
        });
        let (mut tree, staged) = (self.lock(), self.staged.lock());
        let from_key = self.locate_writable(&tree, &staged, from)?;
        let to_key = self.locate_writable(&tree, &staged, to)?;
        let view = self.view(&tree, &staged);
        let from_kind = view.kind(&from_key).ok_or_else(not_found)?;
        if !from_key.starts_with(&self.base) || !to_key.starts_with(&self.base) {
            return Err(not_found());
        }
        if from_key == to_key {
            return Ok(());
        }
        if view.kind(to_key.parent().ok_or_else(not_found)?) != Some(NodeKind::Dir) {
            return Err(not_a_directory());
        }
        match (from_kind, view.kind(&to_key)) {
            (NodeKind::Dir, Some(NodeKind::Dir)) if !view.children(&to_key).is_empty() => {
                return Err(io::Error::from(io::ErrorKind::DirectoryNotEmpty));
            }
            (NodeKind::Dir, Some(NodeKind::Dir)) | (_, None) => {}
            (NodeKind::Dir, Some(_)) => return Err(not_a_directory()),
            (_, Some(NodeKind::Dir)) => return Err(io::Error::from(io::ErrorKind::IsADirectory)),
            (_, Some(_)) => {}
        }
        if from_kind == NodeKind::Dir && to_key.starts_with(&from_key) {
            return Err(io::Error::from(io::ErrorKind::InvalidInput));
        }
        tree.remove(&to_key);
        let moving: Vec<PathBuf> = tree
            .keys()
            .filter(|key| key.starts_with(&from_key))
            .cloned()
            .collect();
        for key in moving {
            if let (Some(node), Ok(rel)) = (tree.remove(&key), key.strip_prefix(&from_key)) {
                let new_key = if rel.as_os_str().is_empty() {
                    to_key.clone()
                } else {
                    to_key.join(rel)
                };
                tree.insert(new_key, node);
            }
        }
        Ok(())
    }

    /// A file, or a link as the link itself; a directory is `IsADirectory`.
    fn remove_file(&self, path: &Path) -> io::Result<()> {
        self.rec.record(IOp::RemoveFile(path.to_path_buf()));
        let (mut tree, staged) = (self.lock(), self.staged.lock());
        let key = self.locate_writable(&tree, &staged, path)?;
        match self.view(&tree, &staged).kind(&key) {
            None => Err(not_found()),
            Some(NodeKind::Dir) => Err(io::Error::from(io::ErrorKind::IsADirectory)),
            Some(_) => {
                tree.remove(&key);
                Ok(())
            }
        }
    }

    /// An empty directory only (`DirectoryNotEmpty`); a link or a file is `NotADirectory`.
    fn remove_dir(&self, path: &Path) -> io::Result<()> {
        self.rec.record(IOp::RemoveDir(path.to_path_buf()));
        let (mut tree, staged) = (self.lock(), self.staged.lock());
        let key = self.locate_writable(&tree, &staged, path)?;
        let view = self.view(&tree, &staged);
        match view.kind(&key) {
            None => Err(not_found()),
            Some(NodeKind::Dir) if !view.children(&key).is_empty() => {
                Err(io::Error::from(io::ErrorKind::DirectoryNotEmpty))
            }
            Some(NodeKind::Dir) => tree.remove(&key).map(|_| ()).ok_or_else(not_found),
            Some(_) => Err(not_a_directory()),
        }
    }

    /// By kind (PD-19): a directory with its subtree, a file or a link as itself, an absent path is `Ok`.
    fn remove_tree(&self, path: &Path) -> io::Result<()> {
        self.rec.record(IOp::RemoveTree(path.to_path_buf()));
        let (mut tree, staged) = (self.lock(), self.staged.lock());
        let key = self.locate_writable(&tree, &staged, path)?;
        match self.view(&tree, &staged).kind(&key) {
            None => Ok(()),
            Some(_) if !key.starts_with(&self.base) => Err(not_found()),
            Some(NodeKind::Dir) => {
                tree.retain(|k, _| !k.starts_with(&key));
                Ok(())
            }
            Some(_) => {
                tree.remove(&key);
                Ok(())
            }
        }
    }
}

/// A `FaultyInstallIo::on_call` hook (a named type for `clippy::type_complexity`).
type InstallHook<'a> = Box<dyn Fn(&dyn InstallIo) + Send + Sync + 'a>;

/// One planned failure of a `FaultyInstallIo`: the `nth` (1-based) call of `kind`, or every call from it on.
struct PlannedFailure {
    kind: IKind,
    nth: usize,
    error: io::ErrorKind,
    persistent: bool,
}

/// An `InstallIo` wrapper with planned failures, crash cuts and hooks (plan §4.5, PD-8); every call is recorded.
pub struct FaultyInstallIo<'a> {
    inner: &'a dyn InstallIo,
    failures: Vec<PlannedFailure>,
    crash_after: Option<usize>,
    hooks: Vec<(IKind, usize, InstallHook<'a>)>,
    symlinks: bool,
    rec: InstallRecorder,
}

impl<'a> FaultyInstallIo<'a> {
    pub fn new(inner: &'a dyn InstallIo) -> FaultyInstallIo<'a> {
        FaultyInstallIo {
            inner,
            failures: Vec::new(),
            crash_after: None,
            hooks: Vec::new(),
            symlinks: true,
            rec: InstallRecorder::default(),
        }
    }

    /// The `nth` call (1-based) of `kind` fails with `error`, once.
    pub fn fail(mut self, kind: IKind, nth: usize, error: io::ErrorKind) -> Self {
        self.failures.push(PlannedFailure {
            kind,
            nth,
            error,
            persistent: false,
        });
        self
    }

    /// Every call of `kind` from the `nth` (1-based) on fails with `error`.
    pub fn fail_from(mut self, kind: IKind, nth: usize, error: io::ErrorKind) -> Self {
        self.failures.push(PlannedFailure {
            kind,
            nth,
            error,
            persistent: true,
        });
        self
    }

    /// Every mutating call after the `k`-th returns `io::Error::other("crash")` without being forwarded.
    pub fn crash_after(mut self, k: usize) -> Self {
        self.crash_after = Some(k);
        self
    }

    /// Runs `hook` with the inner I/O just before the `nth` call of `kind` is forwarded.
    pub fn on_call(
        mut self,
        kind: IKind,
        nth: usize,
        hook: impl Fn(&dyn InstallIo) + Send + Sync + 'a,
    ) -> Self {
        self.hooks.push((kind, nth, Box::new(hook)));
        self
    }

    /// `supports_symlinks()` returns false; everything else is forwarded.
    pub fn without_symlinks(mut self) -> Self {
        self.symlinks = false;
        self
    }

    pub fn calls(&self) -> Vec<IOp> {
        self.rec.calls()
    }

    pub fn mutating_calls(&self) -> usize {
        self.rec.mutating_calls()
    }

    /// Records `op`; then the crash cut, then a planned failure, each returned without forwarding; then the hooks
    /// of this call. `Ok` means: forward it.
    fn gate(&self, op: IOp) -> io::Result<()> {
        let kind = op.kind();
        let (nth, mutating) = self.rec.record(op);
        if let (Some(k), Some(m)) = (self.crash_after, mutating)
            && m > k
        {
            return Err(io::Error::other("crash"));
        }
        if let Some(failure) = self
            .failures
            .iter()
            .find(|f| f.kind == kind && (nth == f.nth || (f.persistent && nth > f.nth)))
        {
            return Err(io::Error::from(failure.error));
        }
        for (hook_kind, hook_nth, hook) in &self.hooks {
            if *hook_kind == kind && *hook_nth == nth {
                hook(self.inner);
            }
        }
        Ok(())
    }
}

impl InstallIo for FaultyInstallIo<'_> {
    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>> {
        self.gate(IOp::KindOf(path.to_path_buf()))?;
        self.inner.kind_of(path)
    }

    fn read_link(&self, path: &Path) -> io::Result<PathBuf> {
        self.gate(IOp::ReadLink(path.to_path_buf()))?;
        self.inner.read_link(path)
    }

    fn hash_file(&self, path: &Path) -> io::Result<[u8; 32]> {
        self.gate(IOp::HashFile(path.to_path_buf()))?;
        self.inner.hash_file(path)
    }

    fn read_dir(&self, dir: &Path) -> io::Result<Vec<(OsString, NodeKind)>> {
        self.gate(IOp::ReadDir(dir.to_path_buf()))?;
        self.inner.read_dir(dir)
    }

    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        self.gate(IOp::ReadFile(path.to_path_buf()))?;
        self.inner.read_file(path, max)
    }

    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        self.gate(IOp::Canonicalize(path.to_path_buf()))?;
        self.inner.canonicalize(path)
    }

    fn supports_symlinks(&self) -> bool {
        self.symlinks && self.inner.supports_symlinks()
    }

    fn create_dir_all(&self, path: &Path) -> io::Result<()> {
        self.gate(IOp::CreateDirAll(path.to_path_buf()))?;
        self.inner.create_dir_all(path)
    }

    fn create_dir(&self, path: &Path) -> io::Result<()> {
        self.gate(IOp::CreateDir(path.to_path_buf()))?;
        self.inner.create_dir(path)
    }

    fn copy_new(&self, src: &Path, dest: &Path, sink: &mut dyn FnMut(&[u8])) -> io::Result<u64> {
        self.gate(IOp::CopyNew {
            src: src.to_path_buf(),
            dest: dest.to_path_buf(),
        })?;
        self.inner.copy_new(src, dest, sink)
    }

    fn write_new(&self, path: &Path, bytes: &[u8]) -> io::Result<()> {
        self.gate(IOp::WriteNew(path.to_path_buf()))?;
        self.inner.write_new(path, bytes)
    }

    fn symlink(&self, target: &str, link: &Path) -> io::Result<()> {
        self.gate(IOp::Symlink {
            target: target.to_owned(),
            link: link.to_path_buf(),
        })?;
        self.inner.symlink(target, link)
    }

    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()> {
        self.gate(IOp::SetMode(path.to_path_buf(), mode))?;
        self.inner.set_mode(path, mode)
    }

    fn sync_file(&self, path: &Path) -> io::Result<()> {
        self.gate(IOp::SyncFile(path.to_path_buf()))?;
        self.inner.sync_file(path)
    }

    fn sync_dir(&self, dir: &Path) -> io::Result<()> {
        self.gate(IOp::SyncDir(dir.to_path_buf()))?;
        self.inner.sync_dir(dir)
    }

    fn rename(&self, from: &Path, to: &Path) -> io::Result<()> {
        self.gate(IOp::Rename {
            from: from.to_path_buf(),
            to: to.to_path_buf(),
        })?;
        self.inner.rename(from, to)
    }

    fn remove_file(&self, path: &Path) -> io::Result<()> {
        self.gate(IOp::RemoveFile(path.to_path_buf()))?;
        self.inner.remove_file(path)
    }

    fn remove_dir(&self, path: &Path) -> io::Result<()> {
        self.gate(IOp::RemoveDir(path.to_path_buf()))?;
        self.inner.remove_dir(path)
    }

    fn remove_tree(&self, path: &Path) -> io::Result<()> {
        self.gate(IOp::RemoveTree(path.to_path_buf()))?;
        self.inner.remove_tree(path)
    }
}

/// An `InstallIo` wrapper that records every call (FR-115): `forwarding(inner)` delegates every call;
/// `fail_closed()` refuses every mutating call and forwards reads to `FsInstallIo`.
pub struct RecordingInstallIo<'a> {
    inner: &'a dyn InstallIo,
    fail_closed: bool,
    rec: InstallRecorder,
}

impl<'a> RecordingInstallIo<'a> {
    pub fn forwarding(inner: &'a dyn InstallIo) -> RecordingInstallIo<'a> {
        RecordingInstallIo {
            inner,
            fail_closed: false,
            rec: InstallRecorder::default(),
        }
    }

    pub fn calls(&self) -> Vec<IOp> {
        self.rec.calls()
    }

    pub fn mutating_calls(&self) -> usize {
        self.rec.mutating_calls()
    }

    /// Records a mutating call; fail-closed refuses it without forwarding (FR-115).
    fn mutate(&self, op: IOp) -> io::Result<()> {
        self.rec.record(op);
        if self.fail_closed {
            Err(io::Error::other("fail-closed"))
        } else {
            Ok(())
        }
    }
}

impl RecordingInstallIo<'static> {
    /// FR-115: every mutating call is recorded and returns `fail-closed`; reads forward to `FsInstallIo`.
    pub fn fail_closed() -> RecordingInstallIo<'static> {
        RecordingInstallIo {
            inner: &FsInstallIo,
            fail_closed: true,
            rec: InstallRecorder::default(),
        }
    }
}

impl InstallIo for RecordingInstallIo<'_> {
    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>> {
        self.rec.record(IOp::KindOf(path.to_path_buf()));
        self.inner.kind_of(path)
    }

    fn read_link(&self, path: &Path) -> io::Result<PathBuf> {
        self.rec.record(IOp::ReadLink(path.to_path_buf()));
        self.inner.read_link(path)
    }

    fn hash_file(&self, path: &Path) -> io::Result<[u8; 32]> {
        self.rec.record(IOp::HashFile(path.to_path_buf()));
        self.inner.hash_file(path)
    }

    fn read_dir(&self, dir: &Path) -> io::Result<Vec<(OsString, NodeKind)>> {
        self.rec.record(IOp::ReadDir(dir.to_path_buf()));
        self.inner.read_dir(dir)
    }

    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        self.rec.record(IOp::ReadFile(path.to_path_buf()));
        self.inner.read_file(path, max)
    }

    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        self.rec.record(IOp::Canonicalize(path.to_path_buf()));
        self.inner.canonicalize(path)
    }

    fn supports_symlinks(&self) -> bool {
        self.inner.supports_symlinks()
    }

    fn create_dir_all(&self, path: &Path) -> io::Result<()> {
        self.mutate(IOp::CreateDirAll(path.to_path_buf()))?;
        self.inner.create_dir_all(path)
    }

    fn create_dir(&self, path: &Path) -> io::Result<()> {
        self.mutate(IOp::CreateDir(path.to_path_buf()))?;
        self.inner.create_dir(path)
    }

    fn copy_new(&self, src: &Path, dest: &Path, sink: &mut dyn FnMut(&[u8])) -> io::Result<u64> {
        self.mutate(IOp::CopyNew {
            src: src.to_path_buf(),
            dest: dest.to_path_buf(),
        })?;
        self.inner.copy_new(src, dest, sink)
    }

    fn write_new(&self, path: &Path, bytes: &[u8]) -> io::Result<()> {
        self.mutate(IOp::WriteNew(path.to_path_buf()))?;
        self.inner.write_new(path, bytes)
    }

    fn symlink(&self, target: &str, link: &Path) -> io::Result<()> {
        self.mutate(IOp::Symlink {
            target: target.to_owned(),
            link: link.to_path_buf(),
        })?;
        self.inner.symlink(target, link)
    }

    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()> {
        self.mutate(IOp::SetMode(path.to_path_buf(), mode))?;
        self.inner.set_mode(path, mode)
    }

    fn sync_file(&self, path: &Path) -> io::Result<()> {
        self.mutate(IOp::SyncFile(path.to_path_buf()))?;
        self.inner.sync_file(path)
    }

    fn sync_dir(&self, dir: &Path) -> io::Result<()> {
        self.mutate(IOp::SyncDir(dir.to_path_buf()))?;
        self.inner.sync_dir(dir)
    }

    fn rename(&self, from: &Path, to: &Path) -> io::Result<()> {
        self.mutate(IOp::Rename {
            from: from.to_path_buf(),
            to: to.to_path_buf(),
        })?;
        self.inner.rename(from, to)
    }

    fn remove_file(&self, path: &Path) -> io::Result<()> {
        self.mutate(IOp::RemoveFile(path.to_path_buf()))?;
        self.inner.remove_file(path)
    }

    fn remove_dir(&self, path: &Path) -> io::Result<()> {
        self.mutate(IOp::RemoveDir(path.to_path_buf()))?;
        self.inner.remove_dir(path)
    }

    fn remove_tree(&self, path: &Path) -> io::Result<()> {
        self.mutate(IOp::RemoveTree(path.to_path_buf()))?;
        self.inner.remove_tree(path)
    }
}

/// An `InstallDriver` wrapper that counts its runs (plan §4.5).
pub struct CountingDriver<'a> {
    inner: &'a dyn InstallDriver,
    runs: AtomicUsize,
}

impl<'a> CountingDriver<'a> {
    pub fn new(inner: &'a dyn InstallDriver) -> CountingDriver<'a> {
        CountingDriver {
            inner,
            runs: AtomicUsize::new(0),
        }
    }

    pub fn runs(&self) -> usize {
        self.runs.load(Ordering::SeqCst)
    }
}

impl InstallDriver for CountingDriver<'_> {
    fn os(&self) -> Os {
        self.inner.os()
    }

    fn run(&self, env: InstallEnv<'_>, batch: Vec<Action>) -> Vec<ActionResult> {
        self.runs.fetch_add(1, Ordering::SeqCst);
        self.inner.run(env, batch)
    }
}

/// An `InstallDriver` that records each batch as `(kind, name)` pairs and returns `Ok` outcomes (plan §4.5): the
/// artifact's version for an install or update, `None` for a remove, nothing placed or removed, `kept` as given.
pub struct RecordingDriver {
    os: Os,
    kept: Vec<KeptPath>,
    batches: Mutex<Vec<Vec<(ActionKind, String)>>>,
}

impl RecordingDriver {
    pub fn new(os: Os, kept: Vec<KeptPath>) -> RecordingDriver {
        RecordingDriver {
            os,
            kept,
            batches: Mutex::new(Vec::new()),
        }
    }

    pub fn batches(&self) -> Vec<Vec<(ActionKind, String)>> {
        self.batches
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .clone()
    }
}

impl InstallDriver for RecordingDriver {
    fn os(&self) -> Os {
        self.os
    }

    fn run(&self, _env: InstallEnv<'_>, batch: Vec<Action>) -> Vec<ActionResult> {
        self.batches
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push(
                batch
                    .iter()
                    .map(|action| (action.kind(), action.entry().name.clone()))
                    .collect(),
            );
        batch
            .into_iter()
            .map(|action| {
                let version = match &action {
                    Action::Install { artifact, .. } | Action::Update { artifact, .. } => {
                        Some(artifact.version().to_owned())
                    }
                    Action::Uninstall { .. } => None,
                };
                ActionResult {
                    name: action.entry().name.clone(),
                    action: action.kind(),
                    outcome: Ok(Outcome {
                        version,
                        placed: 0,
                        removed: 0,
                        kept: self.kept.clone(),
                    }),
                }
            })
            .collect()
    }
}

/// A resolver offering one driver, for the OS it was built for only (plan §4.5, PD-5).
pub struct OneDriver<'a>(pub Option<&'a dyn InstallDriver>);

impl DriverResolver for OneDriver<'_> {
    fn driver_for(&self, os: Os) -> Option<&dyn InstallDriver> {
        self.0.filter(|driver| driver.os() == os)
    }
}

/// The one place a test builds a `CommandEnv` (plan §4.5, PD-30; the `DownloadRig` model): the parts it borrows,
/// owned.
pub struct InstallRig {
    pub ctx: AppContext,
    pub staging: FaultyStagingIo,
    pub policy: DownloadPolicy,
    pub cancel: CancelToken,
    pub extract_limits: ExtractLimits,
    /// Phase 10: `FakeElevation::missing()` by default.
    pub elevation: FakeElevation,
    /// Phase 10: an empty `FakeHostIo` by default, behind a `Mutex` so the rig stays `Sync`.
    pub host_io: Mutex<FakeHostIo>,
}

impl InstallRig {
    /// `staging: FaultyStagingIo::healthy()`, `policy: test_policy()`, `cancel: CancelToken::new()`,
    /// `extract_limits: ExtractLimits::PRODUCTION`.
    pub fn new(ctx: AppContext) -> InstallRig {
        InstallRig {
            ctx,
            staging: FaultyStagingIo::healthy(),
            policy: test_policy(),
            cancel: CancelToken::new(),
            extract_limits: ExtractLimits::PRODUCTION,
            elevation: FakeElevation::missing(),
            host_io: Mutex::new(FakeHostIo::default()),
        }
    }

    pub fn download_env(&self) -> DownloadEnv<'_> {
        DownloadEnv {
            ctx: &self.ctx,
            io: &self.staging,
            policy: &self.policy,
            cancel: &self.cancel,
        }
    }

    pub fn extract_env<'a>(&'a self, io: &'a dyn ExtractIo) -> ExtractEnv<'a> {
        ExtractEnv {
            ctx: &self.ctx,
            io,
            limits: &self.extract_limits,
        }
    }

    /// Seam-free (PD-30): a test that needs the fixture server sets the seam itself. Borrows the rig's own
    /// `elevation` and `host_io`.
    pub fn command_env<'a>(
        &'a self,
        os: Os,
        arch: Arch,
        io: &'a dyn InstallIo,
        extract_io: &'a dyn ExtractIo,
        drivers: &'a dyn DriverResolver,
    ) -> CommandEnv<'a> {
        self.command_env_with(
            os,
            arch,
            io,
            extract_io,
            drivers,
            (&self.elevation, &self.host_io),
        )
    }

    /// `command_env` with the Phase 10 launcher and host I/O given (FR-071), as one `(elevation, host_io)` pair:
    /// a seventh parameter would trip `clippy::too_many_arguments`, which `injection.rs` never lets be suppressed.
    pub fn command_env_with<'a>(
        &'a self,
        os: Os,
        arch: Arch,
        io: &'a dyn InstallIo,
        extract_io: &'a dyn ExtractIo,
        drivers: &'a dyn DriverResolver,
        (elevation, host_io): (&'a dyn Elevation, &'a dyn HostIo),
    ) -> CommandEnv<'a> {
        CommandEnv {
            os,
            arch,
            ctx: &self.ctx,
            io,
            extract: self.extract_env(extract_io),
            download: self.download_env(),
            drivers,
            elevation,
            host_io,
            #[cfg(test)]
            request_seam: None,
            #[cfg(test)]
            stage_seam: None,
        }
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
