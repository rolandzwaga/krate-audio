//! Download and verify one release asset into the staging directory (Phase 7 D1 to D13,
//! FR-001 to FR-066; plan §4.4). Nothing leaves this module unverified.

use crate::context::AppContext;
use crate::detect::HostFacts;
use crate::feed::{self, Feed, FeedCheck, FeedError, FetchMode, PlatformAsset, PlatformKey};
use crate::http::{
    self, HeadAction, HttpError, ResponseHead, StreamEnd, StreamError, StreamLimits,
};
use crate::platform::{NativePlatform, Os, Platform};
use crate::verify::{Expected, VerifiedArtifact, VerifyError, hex_lower, verify_file};
use crate::version::{self, Arch, Availability, OsVersion};
use log::{info, warn};
use serde::Serialize;
use std::cell::RefCell;
use std::collections::BTreeSet;
use std::ffi::OsString;
use std::fmt;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write as _};
use std::path::{Component, Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::thread;
use std::time::{Duration, Instant};

/// The `log` target of every download line (FR-053).
pub const LOG_TARGET: &str = "krate_manager_core::download";
/// The staging subdirectory that holds partials and verified artifacts (FR-003).
pub const DOWNLOADS_DIR: &str = "downloads";
/// The byte step between `Progress::Bytes` events (FR-022).
pub const PROGRESS_STEP: u64 = 256 * 1024;
/// The production per-read stall bound (FR-018).
pub const DOWNLOAD_STALL: Duration = Duration::from_secs(30);
/// The production bound of one attempt (FR-018).
pub const DOWNLOAD_ATTEMPT_DEADLINE: Duration = Duration::from_secs(3600);
/// The longest sleep between two cancellation checks of a backoff wait (FR-017).
const BACKOFF_SLICE: Duration = Duration::from_millis(50);
pub use crate::http::CancelToken;

/// One resolved feed asset to obtain (FR-001); built only by `request_for`.
#[derive(Clone, Debug, PartialEq)]
pub struct DownloadRequest {
    name: String,
    version_text: String,
    key: PlatformKey,
    asset: PlatformAsset,
    /// The OS version the request was resolved with; the prune resolves the feed with it (PD-3).
    running: Option<OsVersion>,
    /// Phase 12: the OS and architecture the request was resolved for; the prune resolves the feed for them, so a
    /// batch for a forced OS never prunes another item's verified artifact (D6). Native in production.
    os: Os,
    arch: Arch,
    /// D13: the loopback URL a test fetches instead of the feed's `AssetUrl`.
    #[cfg(test)]
    fetch_url: Option<String>,
}

impl DownloadRequest {
    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn version_text(&self) -> &str {
        &self.version_text
    }

    pub fn key(&self) -> PlatformKey {
        self.key
    }

    pub fn asset(&self) -> &PlatformAsset {
        &self.asset
    }

    /// D13: fetch `url` instead of the feed's `AssetUrl` (logs and errors still name the feed URL). Only scheme
    /// `http`, host the IP literal 127.0.0.1 or ::1, an explicit port, and the asset name as the last segment.
    #[cfg(test)]
    pub(crate) fn with_fixture_url(self, url: &str) -> Result<DownloadRequest, &'static str> {
        let parsed = url::Url::parse(url).map_err(|_| "not a URL")?;
        if parsed.scheme() != "http" {
            return Err("the fixture URL is not plain http");
        }
        let loopback = match parsed.host() {
            Some(url::Host::Ipv4(a)) => a == std::net::Ipv4Addr::LOCALHOST,
            Some(url::Host::Ipv6(a)) => a == std::net::Ipv6Addr::LOCALHOST,
            _ => false,
        };
        if !loopback {
            return Err("the host is not the IP literal 127.0.0.1 or ::1");
        }
        if parsed.port().is_none() {
            return Err("the URL has no explicit non-default port");
        }
        let last = parsed.path_segments().and_then(|mut s| s.next_back());
        if last != Some(self.asset.asset_name.as_str()) {
            return Err("the last path segment is not the asset name");
        }
        Ok(DownloadRequest {
            fetch_url: Some(url.to_owned()),
            ..self
        })
    }

    /// The URL the transfer fetches: the feed's `AssetUrl` (FR-002).
    #[cfg(not(test))]
    fn fetch_target(&self) -> &str {
        self.asset.url.as_str()
    }

    /// D13: under test, the fixture URL when one is set.
    #[cfg(test)]
    fn fetch_target(&self) -> &str {
        self.fetch_url.as_deref().unwrap_or(self.asset.url.as_str())
    }
}

/// The staged paths of one artifact (FR-003).
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct StagedPaths {
    pub part: PathBuf,
    pub dir: PathBuf,
    pub verified: PathBuf,
}

/// FR-003's one join; pure. `hex` is 64 lower-case hex digits, `asset` one normal path component, and every joined
/// path a strict descendant of `downloads`; anything else is `E_DOWNLOAD_PATH` and nothing is touched.
pub(crate) fn staged_paths(
    downloads: &Path,
    hex: &str,
    asset: &str,
) -> Result<StagedPaths, DownloadError> {
    let refuse = |reason: &'static str| DownloadError::Path { reason };
    if !is_hex64(hex) {
        return Err(refuse("the hash is not 64 lower-case hex digits"));
    }
    let mut components = Path::new(asset).components();
    let one_normal = matches!(
        (components.next(), components.next()),
        (Some(Component::Normal(_)), None)
    );
    if asset.is_empty()
        || asset.contains(['/', '\\'])
        || asset == "."
        || asset == ".."
        || !one_normal
    {
        return Err(refuse("the asset name is not one plain path component"));
    }
    let part = downloads.join(format!("{hex}.part"));
    let dir = downloads.join(hex);
    let verified = dir.join(asset);
    let inside = |p: &Path| p != downloads && p.starts_with(downloads);
    if !(inside(&part) && inside(&dir) && inside(&verified)) {
        return Err(refuse("a staged path is not under the downloads directory"));
    }
    Ok(StagedPaths {
        part,
        dir,
        verified,
    })
}

/// PD-15: every whitespace-delimited token holding `://`, `/` or `?` becomes `<redacted>` (a `://` holds a `/`);
/// the whitespace is kept as it was.
pub(crate) fn scrub(detail: &str) -> String {
    detail
        .split_inclusive(char::is_whitespace)
        .map(|piece| {
            let token = piece.trim_end_matches(char::is_whitespace);
            if token.contains(['/', '?']) {
                format!("<redacted>{}", &piece[token.len()..])
            } else {
                piece.to_owned()
            }
        })
        .collect()
}

fn is_hex64(s: &str) -> bool {
    s.len() == 64 && s.bytes().all(|b| matches!(b, b'0'..=b'9' | b'a'..=b'f'))
}

/// Attempts, backoff, stall and per-attempt bounds of one `obtain_and` (FR-015, FR-018).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DownloadPolicy {
    pub attempts: u32,
    pub backoff: Vec<Duration>,
    pub stall: Duration,
    pub attempt_deadline: Duration,
}

impl DownloadPolicy {
    /// 4 attempts, backoff [1 s, 2 s, 4 s], 30 s stall, 3600 s per attempt (FR-018).
    pub fn production() -> Self {
        DownloadPolicy {
            attempts: 4,
            backoff: vec![
                Duration::from_secs(1),
                Duration::from_secs(2),
                Duration::from_secs(4),
            ],
            stall: DOWNLOAD_STALL,
            attempt_deadline: DOWNLOAD_ATTEMPT_DEADLINE,
        }
    }
}

/// The inputs every download entry point shares (Q-B, PD-11): the context (staging root `ctx.roots`, transport
/// `ctx.http`, release key `ctx.signing_key`), the staging I/O, the policy and the cancel token (R-1). Four borrows,
/// `Copy`, passed by value; a caller builds one per call (or one per thread) from parts it owns, and a variant is
/// struct-update syntax: `DownloadEnv { io: &FsStagingIo, ..env }`. Not `Debug` (`dyn StagingIo` is not) and not
/// required to be `Send`: a thread builds its own from shared parts.
#[derive(Clone, Copy)]
pub struct DownloadEnv<'a> {
    pub ctx: &'a AppContext,
    pub io: &'a dyn StagingIo,
    pub policy: &'a DownloadPolicy,
    pub cancel: &'a CancelToken,
}

/// Why a transfer started over from byte 0 (FR-020, PD-5).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RestartReason {
    RangeIgnored,
    RangeRefused,
    PartialTooLong,
    HashMismatchAfterResume,
    CachedInvalid,
}

/// One progress event (FR-020 to FR-023).
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Progress {
    Started {
        total: u64,
        resumed_from: u64,
    },
    Bytes {
        received: u64,
        on_disk: u64,
        total: u64,
    },
    Restarted {
        reason: RestartReason,
    },
    Retrying {
        attempt: u32,
        of: u32,
        code: String,
        wait_ms: u64,
    },
    Verifying,
    Verified,
}

/// What `obtain_and` returns with `next`'s value (FR-045).
pub struct Obtained<T> {
    pub value: T,
    pub resumed: bool,
    pub cached: bool,
    pub transferred: u64,
}

/// What the command returns to the webview (FR-062): no path, no URL, no hash.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct DownloadSummary {
    pub name: String,
    pub version: String,
    pub platform: String,
    pub asset: String,
    pub size: u64,
    pub resumed: bool,
}

/// How a response head relates to the bytes already on disk (FR-013).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HeadVerdict {
    Full,
    Continue,
    RestartFull,
}

/// The asset a transfer error names (D12): plugin, asset file name and the feed `AssetUrl`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Target {
    pub name: String,
    pub asset: String,
    pub url: String,
}

impl fmt::Display for Target {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} {} ({})", self.name, self.asset, self.url)
    }
}

/// Why the feed offers no request for this machine (FR-001).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Unavailability {
    NotThroughManager,
    NotForThisMachine,
    NeedsNewerOs { min_os: OsVersion },
}

impl fmt::Display for Unavailability {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Unavailability::NotThroughManager => f.write_str("not offered through the manager"),
            Unavailability::NotForThisMachine => f.write_str("no asset for this machine"),
            Unavailability::NeedsNewerOs { min_os } => write!(f, "needs OS {min_os} or newer"),
        }
    }
}

/// The `StagingIo` step a staging error happened in (FR-051).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StagingStep {
    CreateDir,
    Open,
    Write,
    Sync,
    Truncate,
    Rename,
    Remove,
}

impl StagingStep {
    pub fn as_str(self) -> &'static str {
        match self {
            StagingStep::CreateDir => "create_dir",
            StagingStep::Open => "open",
            StagingStep::Write => "write",
            StagingStep::Sync => "sync",
            StagingStep::Truncate => "truncate",
            StagingStep::Rename => "rename",
            StagingStep::Remove => "remove",
        }
    }
}

/// The code family of a staging error, from its kind (FR-051).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StagingCode {
    Full,
    Denied,
    Io,
}

impl StagingCode {
    /// FR-051: `StorageFull | QuotaExceeded` → `Full`, `PermissionDenied | ReadOnlyFilesystem` → `Denied`, else `Io`.
    fn of(kind: io::ErrorKind) -> StagingCode {
        match kind {
            io::ErrorKind::StorageFull | io::ErrorKind::QuotaExceeded => StagingCode::Full,
            io::ErrorKind::PermissionDenied | io::ErrorKind::ReadOnlyFilesystem => {
                StagingCode::Denied
            }
            _ => StagingCode::Io,
        }
    }
}

/// Why a download failed (plan §3). Every `Display` begins with its code; a transfer error
/// names its `Target` (the feed URL, never a redirect target, D12).
#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum DownloadError {
    #[error("E_DOWNLOAD_UNKNOWN_PLUGIN: the feed has no plugin {name}")]
    UnknownPlugin { name: String },
    #[error("E_DOWNLOAD_UNAVAILABLE: {name}: {why}")]
    Unavailable { name: String, why: Unavailability },
    #[error("E_DOWNLOAD_NO_FEED: {cause}: {detail}")]
    NoFeed { cause: &'static str, detail: String },
    #[error("E_DOWNLOAD_VERSION_CHANGED: {name}: expected {expected}, the feed now says {found}")]
    VersionChanged {
        name: String,
        expected: String,
        found: String,
    },
    #[error("E_DOWNLOAD_TASK: {detail}")]
    Task { detail: String },
    #[error("E_DOWNLOAD_PATH: {reason}")]
    Path { reason: &'static str },
    #[error("E_DOWNLOAD_BUSY: {target}: already being downloaded")]
    Busy { target: Box<Target> },
    #[error("E_DOWNLOAD_BUSY: {name} {asset}: being downloaded, not discarded")]
    DiscardBusy { name: String, asset: String },
    #[error("E_DOWNLOAD_CANCELLED: {target}: cancelled")]
    Cancelled { target: Box<Target> },
    #[error("E_DOWNLOAD_ENCODING: {target}: content encoding {encoding}")]
    Encoding {
        target: Box<Target>,
        encoding: String,
    },
    #[error("E_DOWNLOAD_SIZE: {target}: expected {expected} bytes, found {found:?}")]
    Size {
        target: Box<Target>,
        expected: u64,
        found: Option<u64>,
    },
    #[error("E_DOWNLOAD_OFFLINE: {target}: {detail}")]
    Offline { target: Box<Target>, detail: String },
    #[error("E_DOWNLOAD_TIMEOUT: {target}: the request did not complete in time")]
    Timeout { target: Box<Target> },
    #[error("E_DOWNLOAD_STALLED: {target}: no byte arrived within the stall bound")]
    Stalled { target: Box<Target> },
    #[error("E_DOWNLOAD_TLS: {target}: {detail}")]
    Tls { target: Box<Target>, detail: String },
    #[error("E_DOWNLOAD_URL: {target}: {reason}")]
    Url {
        target: Box<Target>,
        reason: &'static str,
    },
    #[error("E_DOWNLOAD_REDIRECT: {target}: {reason}")]
    Redirect {
        target: Box<Target>,
        reason: &'static str,
    },
    #[error("E_DOWNLOAD_HTTP_STATUS: {target}: HTTP status {status}")]
    HttpStatus { target: Box<Target>, status: u16 },
    #[error("E_DOWNLOAD_BODY: {target}: {detail}")]
    Body { target: Box<Target>, detail: String },
    #[error("{}: {target}: staging step {} failed: {kind:?}", staging_code(*.code), .step.as_str())]
    Staging {
        target: Box<Target>,
        code: StagingCode,
        step: StagingStep,
        kind: io::ErrorKind,
    },
    #[error("{}: {target}: {error}", .error.code())]
    Verify {
        target: Box<Target>,
        error: VerifyError,
    },
    // ---- Phase 11 (FR-018) ----
    #[error(transparent)]
    SelfUpdating(#[from] crate::self_update::SelfUpdating),
}

fn staging_code(code: StagingCode) -> &'static str {
    match code {
        StagingCode::Full => "E_STAGING_FULL",
        StagingCode::Denied => "E_STAGING_DENIED",
        StagingCode::Io => "E_STAGING_IO",
    }
}

impl DownloadError {
    pub fn code(&self) -> &'static str {
        match self {
            DownloadError::UnknownPlugin { .. } => "E_DOWNLOAD_UNKNOWN_PLUGIN",
            DownloadError::Unavailable { .. } => "E_DOWNLOAD_UNAVAILABLE",
            DownloadError::NoFeed { .. } => "E_DOWNLOAD_NO_FEED",
            DownloadError::VersionChanged { .. } => "E_DOWNLOAD_VERSION_CHANGED",
            DownloadError::Task { .. } => "E_DOWNLOAD_TASK",
            DownloadError::Path { .. } => "E_DOWNLOAD_PATH",
            DownloadError::Busy { .. } | DownloadError::DiscardBusy { .. } => "E_DOWNLOAD_BUSY",
            DownloadError::Cancelled { .. } => "E_DOWNLOAD_CANCELLED",
            DownloadError::Encoding { .. } => "E_DOWNLOAD_ENCODING",
            DownloadError::Size { .. } => "E_DOWNLOAD_SIZE",
            DownloadError::Offline { .. } => "E_DOWNLOAD_OFFLINE",
            DownloadError::Timeout { .. } => "E_DOWNLOAD_TIMEOUT",
            DownloadError::Stalled { .. } => "E_DOWNLOAD_STALLED",
            DownloadError::Tls { .. } => "E_DOWNLOAD_TLS",
            DownloadError::Url { .. } => "E_DOWNLOAD_URL",
            DownloadError::Redirect { .. } => "E_DOWNLOAD_REDIRECT",
            DownloadError::HttpStatus { .. } => "E_DOWNLOAD_HTTP_STATUS",
            DownloadError::Body { .. } => "E_DOWNLOAD_BODY",
            DownloadError::Staging { code, .. } => staging_code(*code),
            DownloadError::Verify { error, .. } => error.code(),
            DownloadError::SelfUpdating(_) => crate::self_update::SELF_UPDATING_CODE,
        }
    }

    /// `classify_head`'s placeholder target replaced by the real one (plan §4.4.6).
    fn with_target(self, target: &Target) -> DownloadError {
        let target = Box::new(target.clone());
        match self {
            DownloadError::Encoding { encoding, .. } => {
                DownloadError::Encoding { target, encoding }
            }
            DownloadError::Size {
                expected, found, ..
            } => DownloadError::Size {
                target,
                expected,
                found,
            },
            DownloadError::HttpStatus { status, .. } => {
                DownloadError::HttpStatus { target, status }
            }
            other => other,
        }
    }

    /// D5's retried codes: no response, a timeout or stall, a body that ended early, 408/429/500/502/503/504.
    fn retryable(&self) -> bool {
        match self {
            DownloadError::Offline { .. }
            | DownloadError::Timeout { .. }
            | DownloadError::Stalled { .. }
            | DownloadError::Body { .. } => true,
            DownloadError::HttpStatus { status, .. } => {
                matches!(status, 408 | 429 | 500 | 502 | 503 | 504)
            }
            _ => false,
        }
    }
}

/// A staging error of `step` with `kind`, its code from the kind (FR-051).
fn staging_error(target: &Target, step: StagingStep, kind: io::ErrorKind) -> DownloadError {
    DownloadError::Staging {
        target: Box::new(target.clone()),
        code: StagingCode::of(kind),
        step,
        kind,
    }
}

/// An open partial file (FR-050).
pub trait PartialFile {
    fn write_all(&mut self, bytes: &[u8]) -> io::Result<()>;
    fn sync(&mut self) -> io::Result<()>;
}

/// What a staged entry is, never following a link (PD-19): a symlink or junction is `Other`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EntryKind {
    Dir,
    File,
    Other,
}

impl EntryKind {
    /// From a `file_type()` that did not follow the link (PD-19): on Windows a junction is not `is_dir()`.
    fn of(file_type: fs::FileType) -> EntryKind {
        if file_type.is_dir() {
            EntryKind::Dir
        } else if file_type.is_file() {
            EntryKind::File
        } else {
            EntryKind::Other
        }
    }
}

/// One entry of `downloads/` (FR-050), from `DirEntry::file_type()` (no follow).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StagedEntry {
    pub name: OsString,
    pub kind: EntryKind,
}

/// Every filesystem step of the staging directory (FR-050, PD-6, SI-29).
pub trait StagingIo {
    /// Creates `dir`, then requires `symlink_metadata(dir)` to be a directory.
    fn create_dir_all(&self, dir: &Path) -> io::Result<()>;
    /// `NotFound` → `None`, else the kind, never following the final link.
    fn kind_of(&self, path: &Path) -> io::Result<Option<EntryKind>>;
    /// `NotFound` → `None`; not a regular file → `Err(InvalidInput)`.
    fn len_of(&self, path: &Path) -> io::Result<Option<u64>>;
    fn open_partial(&self, path: &Path, truncate: bool) -> io::Result<Box<dyn PartialFile>>;
    fn rename(&self, from: &Path, to: &Path) -> io::Result<()>;
    fn remove_file(&self, path: &Path) -> io::Result<()>;
    fn remove_dir_all(&self, dir: &Path) -> io::Result<()>;
    /// The one `read_dir` of the module (FR-082).
    fn read_dir_entries(&self, dir: &Path) -> io::Result<Vec<StagedEntry>>;
}

/// The real staging I/O, through `std::fs` (FR-050). Sets no mode, attribute or timestamp (FR-004).
pub struct FsStagingIo;

/// `FsStagingIo`'s partial file.
struct FsPartial(File);

impl PartialFile for FsPartial {
    fn write_all(&mut self, bytes: &[u8]) -> io::Result<()> {
        self.0.write_all(bytes)
    }

    fn sync(&mut self) -> io::Result<()> {
        self.0.sync_all()
    }
}

impl StagingIo for FsStagingIo {
    fn create_dir_all(&self, dir: &Path) -> io::Result<()> {
        fs::create_dir_all(dir)?;
        if fs::symlink_metadata(dir)?.file_type().is_dir() {
            Ok(())
        } else {
            Err(io::Error::from(io::ErrorKind::InvalidInput))
        }
    }

    fn kind_of(&self, path: &Path) -> io::Result<Option<EntryKind>> {
        match fs::symlink_metadata(path) {
            Ok(meta) => Ok(Some(EntryKind::of(meta.file_type()))),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e),
        }
    }

    fn len_of(&self, path: &Path) -> io::Result<Option<u64>> {
        match fs::symlink_metadata(path) {
            Ok(meta) if meta.file_type().is_file() => Ok(Some(meta.len())),
            Ok(_) => Err(io::Error::from(io::ErrorKind::InvalidInput)),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e),
        }
    }

    fn open_partial(&self, path: &Path, truncate: bool) -> io::Result<Box<dyn PartialFile>> {
        // std refuses `append` together with `truncate`.
        let file = if truncate {
            OpenOptions::new()
                .write(true)
                .create(true)
                .truncate(true)
                .open(path)?
        } else {
            OpenOptions::new().append(true).create(true).open(path)?
        };
        Ok(Box::new(FsPartial(file)))
    }

    fn rename(&self, from: &Path, to: &Path) -> io::Result<()> {
        fs::rename(from, to)
    }

    fn remove_file(&self, path: &Path) -> io::Result<()> {
        fs::remove_file(path)
    }

    fn remove_dir_all(&self, dir: &Path) -> io::Result<()> {
        // std's remove_dir_all does not follow a link: it removes the link itself.
        fs::remove_dir_all(dir)
    }

    fn read_dir_entries(&self, dir: &Path) -> io::Result<Vec<StagedEntry>> {
        fs::read_dir(dir)?
            .map(|item| {
                let item = item?;
                Ok(StagedEntry {
                    name: item.file_name(),
                    kind: EntryKind::of(item.file_type()?),
                })
            })
            .collect()
    }
}

/// `(downloads dir, sha256)` of every artifact in flight in this process (FR-006, D10).
static IN_FLIGHT: Mutex<BTreeSet<(PathBuf, [u8; 32])>> = Mutex::new(BTreeSet::new());

fn registry() -> MutexGuard<'static, BTreeSet<(PathBuf, [u8; 32])>> {
    IN_FLIGHT.lock().unwrap_or_else(PoisonError::into_inner)
}

/// One registry entry; dropping it (also while unwinding) removes the key (FR-006).
struct InFlight {
    key: (PathBuf, [u8; 32]),
}

impl InFlight {
    /// `None` when the key is already in flight. The guard is built only after the lock is released: dropping one
    /// for a busy key would re-lock the registry and remove the holder's key.
    fn enter(downloads: &Path, sha256: [u8; 32]) -> Option<InFlight> {
        let key = (downloads.to_path_buf(), sha256);
        let inserted = registry().insert(key.clone());
        inserted.then(|| InFlight { key })
    }
}

impl Drop for InFlight {
    fn drop(&mut self) {
        registry().remove(&self.key);
    }
}

/// Phase 11 FR-015: whether any artifact of `downloads` is in flight in this process.
pub fn any_in_flight(downloads: &Path) -> bool {
    registry().iter().any(|(d, _)| d == downloads)
}

/// Phase 11 tasks TA-8: a test hold of one in-flight key of `downloads`; dropping it removes the key.
#[cfg(any(test, feature = "test-support"))]
pub struct InFlightHeldForTest {
    _held: InFlight,
}

/// Phase 11 tasks TA-8: `(downloads, [0xA5; 32])` held in flight (tests only).
#[cfg(any(test, feature = "test-support"))]
pub fn hold_in_flight_for_test(downloads: &Path) -> InFlightHeldForTest {
    InFlightHeldForTest {
        _held: InFlight::enter(downloads, [0xA5; 32]).expect("in-flight key already held"),
    }
}

/// FR-005 under the registry lock for the whole pass (PD-12): an entry is kept only when its name is `<hex>.part`
/// (a regular file) or `<hex>` (a directory) for a live or in-flight `<hex>`; everything else is removed, a link
/// as the link itself (PD-19). A failed removal is logged and the download goes on.
fn prune(io: &dyn StagingIo, downloads: &Path, live: &BTreeSet<String>) {
    let in_flight = registry();
    let entries = match io.read_dir_entries(downloads) {
        Ok(entries) => entries,
        Err(e) => {
            warn!(
                target: LOG_TARGET,
                "{}: staging step {} failed for {DOWNLOADS_DIR}: {:?}",
                staging_code(StagingCode::of(e.kind())),
                StagingStep::Open.as_str(),
                e.kind()
            );
            return;
        }
    };
    for entry in entries {
        let keep = entry.name.to_str().is_some_and(|name| {
            let (hex, kind) = match name.strip_suffix(".part") {
                Some(hex) => (hex, EntryKind::File),
                None => (name, EntryKind::Dir),
            };
            is_hex64(hex)
                && entry.kind == kind
                && (live.contains(hex)
                    || in_flight
                        .iter()
                        .any(|(dir, sha)| dir == downloads && hex_lower(sha) == hex))
        });
        if keep {
            continue;
        }
        let path = downloads.join(&entry.name);
        let removed = match entry.kind {
            EntryKind::Dir => io.remove_dir_all(&path),
            EntryKind::File => io.remove_file(&path),
            EntryKind::Other => remove_link(io, &path),
        };
        if let Err(e) = removed
            && e.kind() != io::ErrorKind::NotFound
        {
            warn!(
                target: LOG_TARGET,
                "{}: staging step {} failed for {}: {:?}",
                staging_code(StagingCode::of(e.kind())),
                StagingStep::Remove.as_str(),
                entry.name.to_string_lossy(),
                e.kind()
            );
        }
    }
}

/// A link (or anything that is neither a file nor a directory) removed as itself.
#[cfg(not(windows))]
fn remove_link(io: &dyn StagingIo, path: &Path) -> io::Result<()> {
    io.remove_file(path)
}

/// Windows: std's `remove_file` cannot remove a directory symlink or junction, so a refusal is retried with
/// `remove_dir_all`, which removes the link itself without following it (PD-19).
#[cfg(windows)]
fn remove_link(io: &dyn StagingIo, path: &Path) -> io::Result<()> {
    io.remove_file(path).or_else(|_| io.remove_dir_all(path))
}

/// The lower-case hex of every feed entry's asset for the request's platform (native in production), resolved with
/// the request's `running` (FR-005).
fn live_hashes(feed: &Feed, request: &DownloadRequest) -> BTreeSet<String> {
    feed.plugins
        .values()
        .filter_map(|entry| {
            match version::resolve(entry, request.os, request.arch, request.running.as_ref()) {
                Availability::Available { asset, .. } => Some(hex_lower(&asset.sha256)),
                _ => None,
            }
        })
        .collect()
}

/// The one way to build a `DownloadRequest` (FR-001): the asset resolved for `os` and `arch` (Phase 9 PD-1).
pub fn request_for_platform(
    feed: &Feed,
    name: &str,
    os: Os,
    arch: Arch,
    running: Option<&OsVersion>,
) -> Result<DownloadRequest, DownloadError> {
    let entry = feed
        .plugins
        .get(name)
        .ok_or_else(|| DownloadError::UnknownPlugin {
            name: name.to_owned(),
        })?;
    let unavailable = |why: Unavailability| DownloadError::Unavailable {
        name: name.to_owned(),
        why,
    };
    match version::resolve(entry, os, arch, running) {
        Availability::Available { key, asset } => Ok(DownloadRequest {
            name: name.to_owned(),
            version_text: entry.version_text.clone(),
            key,
            asset: asset.clone(),
            running: running.copied(),
            os,
            arch,
            #[cfg(test)]
            fetch_url: None,
        }),
        Availability::NotThroughManager => Err(unavailable(Unavailability::NotThroughManager)),
        Availability::NotForThisMachine => Err(unavailable(Unavailability::NotForThisMachine)),
        Availability::NeedsNewerOs { min_os } => {
            Err(unavailable(Unavailability::NeedsNewerOs { min_os }))
        }
    }
}

/// `request_for_platform` for the native OS and architecture (FR-001).
pub fn request_for(
    feed: &Feed,
    name: &str,
    running: Option<&OsVersion>,
) -> Result<DownloadRequest, DownloadError> {
    request_for_platform(feed, name, NativePlatform.os(), Arch::native(), running)
}

/// The target a pure function's error carries until `obtain_and` replaces it.
fn no_target() -> Box<Target> {
    Box::new(Target {
        name: String::new(),
        asset: String::new(),
        url: String::new(),
    })
}

/// FR-013's table; pure (plan §4.4.6, PD-9, PD-17).
pub fn classify_head(
    requested_from: u64,
    size: u64,
    head: &ResponseHead,
) -> Result<HeadVerdict, DownloadError> {
    if let Some(encoding) = &head.content_encoding
        && !encoding.trim().eq_ignore_ascii_case("identity")
    {
        return Err(DownloadError::Encoding {
            target: no_target(),
            encoding: encoding.clone(),
        });
    }
    match head.status {
        200 => match head.content_length {
            Some(length) if length != size => Err(DownloadError::Size {
                target: no_target(),
                expected: size,
                found: Some(length),
            }),
            _ => Ok(HeadVerdict::Full),
        },
        206 if requested_from > 0 => {
            let exact = format!("bytes {requested_from}-{}/{size}", size.saturating_sub(1));
            let length_ok = head
                .content_length
                .is_none_or(|length| length == size.saturating_sub(requested_from));
            if head.content_range.as_deref() == Some(exact.as_str()) && length_ok {
                Ok(HeadVerdict::Continue)
            } else {
                Ok(HeadVerdict::RestartFull)
            }
        }
        206 => Ok(HeadVerdict::RestartFull),
        status => Err(DownloadError::HttpStatus {
            target: no_target(),
            status,
        }),
    }
}

/// The progress counters and the closure they report to (plan §4.4.7, D8, PD-20).
struct Meter<'p> {
    progress: &'p mut dyn FnMut(Progress),
    size: u64,
    /// The `.part`'s length.
    on_disk: u64,
    /// The largest `on_disk` so far: `received`.
    high_water: u64,
    /// Bytes written since the last `Bytes`.
    since_last: u64,
}

impl Meter<'_> {
    fn emit(&mut self, event: Progress) {
        (self.progress)(event);
    }

    /// `Started`; the meter starts where the file is (PD-20).
    fn started(&mut self, resumed_from: u64) {
        self.on_disk = resumed_from;
        self.high_water = resumed_from;
        self.since_last = 0;
        self.emit(Progress::Started {
            total: self.size,
            resumed_from,
        });
    }

    /// `n` bytes appended; a `Bytes` every `PROGRESS_STEP` and at `size` (FR-022).
    fn wrote(&mut self, n: u64) {
        self.on_disk += n;
        self.high_water = self.high_water.max(self.on_disk);
        self.since_last += n;
        if self.since_last >= PROGRESS_STEP || self.on_disk == self.size {
            self.since_last = 0;
            self.emit(Progress::Bytes {
                received: self.high_water,
                on_disk: self.on_disk,
                total: self.size,
            });
        }
    }

    /// The `.part` is empty again; `received` keeps its high-water mark.
    fn restarted(&mut self, reason: RestartReason) {
        self.emit(Progress::Restarted { reason });
        self.on_disk = 0;
        self.since_last = 0;
    }
}

/// The attempt state the head and sink closures share, borrowed in turn (plan §4.4.7).
struct Attempt<'p> {
    meter: Meter<'p>,
    file: Option<Box<dyn PartialFile>>,
    /// The kept `classify_head` result of this response (FR-013).
    verdict: Option<Result<HeadVerdict, DownloadError>>,
    /// The staging step a closure failed in, with its kind.
    failed: Option<(StagingStep, io::ErrorKind)>,
    /// The `.part` held bytes before this call's first request (PD-8).
    foreign: bool,
    /// Responses that wrote into the `.part` since it was last truncated (PD-8).
    writers: u32,
    /// This response wrote a byte.
    wrote: bool,
    /// Body bytes this call wrote, across all its responses.
    transferred: u64,
    requests: u32,
    budget: u32,
    /// D4's one full re-download is still available.
    redo_left: bool,
}

impl Attempt<'_> {
    /// PD-8: assembled from more than one response.
    fn assembled(&self) -> bool {
        self.foreign || self.writers >= 2
    }
}

/// What step 4 found at the verified path (FR-046).
enum Cached {
    Hit(VerifiedArtifact),
    Miss,
    Invalid,
}

/// The per-call parts of one `obtain_and` the private steps share (plan §4.4.8).
struct Job<'a> {
    env: DownloadEnv<'a>,
    request: &'a DownloadRequest,
    target: &'a Target,
    paths: &'a StagedPaths,
}

impl Job<'_> {
    fn size(&self) -> u64 {
        self.request.asset.size
    }

    fn boxed(&self) -> Box<Target> {
        Box::new(self.target.clone())
    }

    fn staging(&self, step: StagingStep, e: &io::Error) -> DownloadError {
        staging_error(self.target, step, e.kind())
    }

    /// Best effort (FR-014, FR-051): a failed removal is logged, not raised.
    fn discard_part(&self) {
        if let Err(e) = self.env.io.remove_file(&self.paths.part)
            && e.kind() != io::ErrorKind::NotFound
        {
            warn!(target: LOG_TARGET, "{}", self.staging(StagingStep::Remove, &e));
        }
    }

    fn verify(&self, path: &Path) -> Result<VerifiedArtifact, VerifyError> {
        verify_file(
            path,
            &Expected::from_request(self.request),
            &self.env.ctx.signing_key,
        )
    }

    /// Step 4: `kind_of(<hex>)` before anything under it is measured (SI-29), then the cached file re-verified.
    fn cached(&self, meter: &mut Meter<'_>) -> Result<Cached, DownloadError> {
        let io = self.env.io;
        match io
            .kind_of(&self.paths.dir)
            .map_err(|e| self.staging(StagingStep::Open, &e))?
        {
            None => return Ok(Cached::Miss),
            Some(EntryKind::Dir) => {}
            Some(_) => {
                return Err(staging_error(
                    self.target,
                    StagingStep::Open,
                    io::ErrorKind::InvalidInput,
                ));
            }
        }
        if io
            .len_of(&self.paths.verified)
            .map_err(|e| self.staging(StagingStep::Open, &e))?
            .is_none()
        {
            return Ok(Cached::Miss);
        }
        meter.started(self.size());
        meter.emit(Progress::Verifying);
        match self.verify(&self.paths.verified) {
            Ok(artifact) => Ok(Cached::Hit(artifact)),
            Err(_) => {
                io.remove_dir_all(&self.paths.dir)
                    .map_err(|e| self.staging(StagingStep::Remove, &e))?;
                self.discard_part();
                meter.restarted(RestartReason::CachedInvalid);
                Ok(Cached::Invalid)
            }
        }
    }

    /// Step 5: `Started` from the `.part`'s length; a `.part` longer than `size` is truncated. `true` when the
    /// kept `.part` holds bytes from before this call (PD-8).
    fn existing_partial(&self, meter: &mut Meter<'_>) -> Result<bool, DownloadError> {
        let size = self.size();
        let len = self
            .env
            .io
            .len_of(&self.paths.part)
            .map_err(|e| self.staging(StagingStep::Open, &e))?
            .unwrap_or(0);
        if len <= size {
            meter.started(len);
            return Ok(len > 0);
        }
        meter.started(0);
        self.truncate()?;
        meter.restarted(RestartReason::PartialTooLong);
        Ok(false)
    }

    /// `open_partial(part, true)` as a step of its own; a failure removes the `.part` (FR-051).
    fn truncate(&self) -> Result<(), DownloadError> {
        match self.env.io.open_partial(&self.paths.part, true) {
            Ok(file) => {
                drop(file);
                Ok(())
            }
            Err(e) => {
                self.discard_part();
                Err(self.staging(StagingStep::Truncate, &e))
            }
        }
    }

    /// Step 7: requests until the `.part` holds `size` bytes, within the budget (PD-7).
    fn transfer(&self, state: &RefCell<Attempt<'_>>) -> Result<(), DownloadError> {
        let policy = self.env.policy;
        let limits = StreamLimits {
            stall: policy.stall,
            deadline: policy.attempt_deadline,
        };
        loop {
            let from = {
                let mut st = state.borrow_mut();
                if st.meter.on_disk >= self.size() {
                    return Ok(());
                }
                if self.env.cancel.is_cancelled() {
                    return Err(DownloadError::Cancelled {
                        target: self.boxed(),
                    });
                }
                st.requests += 1;
                st.verdict = None;
                st.failed = None;
                st.wrote = false;
                st.meter.on_disk
            };
            let range = format!("bytes={from}-");
            let ranged = [("Range", range.as_str())];
            let headers: &[(&str, &str)] = if from > 0 { &ranged } else { &[] };
            let outcome = http::get_streamed(
                &self.env.ctx.http,
                self.request.fetch_target(),
                headers,
                limits,
                &mut |head| self.on_head(state, from, head),
                &mut |chunk| self.sink(state, chunk),
                self.env.cancel,
            );
            let mut st = state.borrow_mut();
            self.settle(&mut st, from, outcome)?;
        }
    }

    /// FR-013: classify the head, open (or truncate) the `.part`, and return the body limit (SI-27).
    fn on_head(&self, state: &RefCell<Attempt<'_>>, from: u64, head: &ResponseHead) -> HeadAction {
        let mut guard = state.borrow_mut();
        let st = &mut *guard;
        let size = self.size();
        let verdict = classify_head(from, size, head);
        let open = match verdict {
            // D4: a `200` to a ranged request is the whole body, written from byte 0.
            Ok(HeadVerdict::Full) if from > 0 => Some((StagingStep::Truncate, true)),
            Ok(HeadVerdict::Full | HeadVerdict::Continue) => Some((StagingStep::Open, false)),
            _ => None,
        };
        let action = match open {
            None => HeadAction::Stop,
            Some((step, truncate)) => match self.env.io.open_partial(&self.paths.part, truncate) {
                Err(e) => {
                    st.failed = Some((step, e.kind()));
                    HeadAction::Stop
                }
                Ok(file) => {
                    st.file = Some(file);
                    if truncate {
                        st.meter.restarted(RestartReason::RangeIgnored);
                        st.foreign = false;
                        st.writers = 0;
                    }
                    HeadAction::Read {
                        max_body: size - st.meter.on_disk,
                    }
                }
            },
        };
        st.verdict = Some(verdict);
        action
    }

    /// One body chunk into the `.part`, split at `PROGRESS_STEP` boundaries so every `Bytes` lands on one (D8).
    fn sink(&self, state: &RefCell<Attempt<'_>>, chunk: &[u8]) -> io::Result<()> {
        let mut guard = state.borrow_mut();
        let st = &mut *guard;
        let Some(file) = st.file.as_mut() else {
            return Err(io::Error::from(io::ErrorKind::NotConnected));
        };
        let mut rest = chunk;
        while !rest.is_empty() {
            let room = usize::try_from(PROGRESS_STEP - st.meter.since_last).unwrap_or(usize::MAX);
            let (piece, tail) = rest.split_at(rest.len().min(room));
            if let Err(e) = file.write_all(piece) {
                st.failed = Some((StagingStep::Write, e.kind()));
                return Err(e);
            }
            if !st.wrote {
                st.wrote = true;
                st.writers += 1;
            }
            let n = piece.len() as u64;
            st.transferred += n;
            st.meter.wrote(n);
            rest = tail;
        }
        Ok(())
    }

    /// The outcome of one request (FR-014 to FR-016, D4, D5): `Ok` to go on with the loop.
    fn settle(
        &self,
        st: &mut Attempt<'_>,
        from: u64,
        outcome: Result<StreamEnd, StreamError>,
    ) -> Result<(), DownloadError> {
        let size = self.size();
        // The handle is closed before any removal or rename (Windows).
        let file = st.file.take();
        let complete = outcome.is_ok() && st.meter.on_disk == size;
        let synced = match file {
            Some(mut file) if complete => file.sync(),
            _ => Ok(()),
        };
        let error = match outcome {
            Ok(_) if complete => {
                return synced.map_err(|e| {
                    self.discard_part();
                    self.staging(StagingStep::Sync, &e)
                });
            }
            Ok(_) => DownloadError::Body {
                target: self.boxed(),
                detail: format!("the body ended after {} of {size} bytes", st.meter.on_disk),
            },
            Err(StreamError::Cancelled) => {
                return Err(DownloadError::Cancelled {
                    target: self.boxed(),
                });
            }
            Err(StreamError::HeadStopped | StreamError::Sink { .. }) => {
                if let Some((step, kind)) = st.failed {
                    if matches!(step, StagingStep::Write | StagingStep::Truncate) {
                        self.discard_part();
                    }
                    return Err(staging_error(self.target, step, kind));
                }
                return match st.verdict.take() {
                    Some(Ok(HeadVerdict::RestartFull)) => self.refuse(st, 206),
                    Some(Err(e)) => {
                        if matches!(
                            e,
                            DownloadError::Size { .. } | DownloadError::Encoding { .. }
                        ) {
                            self.discard_part();
                        }
                        Err(e.with_target(self.target))
                    }
                    _ => Err(DownloadError::Body {
                        target: self.boxed(),
                        detail: "the transfer stopped at the response head".to_owned(),
                    }),
                };
            }
            Err(StreamError::Stalled) => DownloadError::Stalled {
                target: self.boxed(),
            },
            Err(StreamError::Http(HttpError::Status { status: 416 })) if from > 0 => {
                return self.refuse(st, 416);
            }
            Err(StreamError::Http(HttpError::TooLarge { .. })) => {
                self.discard_part();
                return Err(DownloadError::Size {
                    target: self.boxed(),
                    expected: size,
                    found: None,
                });
            }
            Err(StreamError::Http(e)) => self.mapped(e),
        };
        self.retry_or_fail(st, error)
    }

    /// FR-016's mapping of a transport error; every foreign detail scrubbed (PD-15), no hop URL kept.
    fn mapped(&self, e: HttpError) -> DownloadError {
        let target = self.boxed();
        match e {
            HttpError::Url { reason, .. } => DownloadError::Url { target, reason },
            HttpError::Redirect { reason } => DownloadError::Redirect { target, reason },
            HttpError::Status { status } => DownloadError::HttpStatus { target, status },
            HttpError::TooLarge { .. } => DownloadError::Size {
                target,
                expected: self.size(),
                found: None,
            },
            HttpError::Offline { detail } => DownloadError::Offline {
                target,
                detail: scrub(&detail),
            },
            HttpError::Timeout => DownloadError::Timeout { target },
            HttpError::Tls { detail } => DownloadError::Tls {
                target,
                detail: scrub(&detail),
            },
            HttpError::Body { detail } => DownloadError::Body {
                target,
                detail: scrub(&detail),
            },
        }
    }

    /// D4: a `416` or a `RestartFull` 206 empties the `.part` and the next request is a full one at once (no
    /// backoff, no `Retrying`), if a unit of the budget is left (PD-7).
    fn refuse(&self, st: &mut Attempt<'_>, status: u16) -> Result<(), DownloadError> {
        self.truncate()?;
        st.meter.restarted(RestartReason::RangeRefused);
        st.foreign = false;
        st.writers = 0;
        if st.requests >= st.budget {
            return Err(DownloadError::HttpStatus {
                target: self.boxed(),
                status,
            });
        }
        Ok(())
    }

    /// D5 / FR-015 / FR-023: a retryable error with a unit left emits one `Retrying`, then waits the backoff.
    fn retry_or_fail(
        &self,
        st: &mut Attempt<'_>,
        error: DownloadError,
    ) -> Result<(), DownloadError> {
        if !error.retryable() {
            return Err(error);
        }
        // A read already blocked in the transport when the token was set ends here: no Retrying (FR-017, FR-023).
        if self.env.cancel.is_cancelled() {
            return Err(DownloadError::Cancelled {
                target: self.boxed(),
            });
        }
        // The file is the truth after a partial write.
        let len = self
            .env
            .io
            .len_of(&self.paths.part)
            .map_err(|e| self.staging(StagingStep::Open, &e))?
            .unwrap_or(0);
        st.meter.on_disk = len;
        st.meter.high_water = st.meter.high_water.max(len);
        if st.requests >= st.budget {
            return Err(error);
        }
        let policy = self.env.policy;
        let index = usize::try_from(st.requests - 1)
            .unwrap_or(usize::MAX)
            .min(policy.backoff.len().saturating_sub(1));
        let wait = policy.backoff.get(index).copied().unwrap_or_default();
        st.meter.emit(Progress::Retrying {
            attempt: st.requests,
            of: policy.attempts,
            code: error.code().to_owned(),
            wait_ms: u64::try_from(wait.as_millis()).unwrap_or(u64::MAX),
        });
        if wait_or_cancel(wait, self.env.cancel) {
            return Err(DownloadError::Cancelled {
                target: self.boxed(),
            });
        }
        Ok(())
    }
}

/// Sleeps `wait` in `BACKOFF_SLICE` slices; `true` as soon as `cancel` is set (FR-017).
fn wait_or_cancel(wait: Duration, cancel: &CancelToken) -> bool {
    let end = Instant::now() + wait;
    loop {
        if cancel.is_cancelled() {
            return true;
        }
        let now = Instant::now();
        if now >= end {
            return false;
        }
        thread::sleep((end - now).min(BACKOFF_SLICE));
    }
}

/// Obtain the request's verified artifact and hand it to `next` (FR-045 to FR-047), then log once (FR-053).
pub fn obtain_and<T>(
    env: DownloadEnv<'_>,
    feed: &Feed,
    request: &DownloadRequest,
    progress: &mut dyn FnMut(Progress),
    next: impl FnOnce(VerifiedArtifact) -> T,
) -> Result<Obtained<T>, DownloadError> {
    let result = obtain_body(env, feed, request, progress, next);
    match &result {
        Ok(obtained) => info!(
            target: LOG_TARGET,
            "{} {} {}: verified, transferred={} resumed={} cached={}",
            request.name,
            request.version_text,
            request.key.as_str(),
            obtained.transferred,
            obtained.resumed,
            obtained.cached
        ),
        Err(e) => warn!(target: LOG_TARGET, "{e}"),
    }
    result
}

/// Plan §4.4.8 steps 1 to 8.
fn obtain_body<T>(
    env: DownloadEnv<'_>,
    feed: &Feed,
    request: &DownloadRequest,
    progress: &mut dyn FnMut(Progress),
    next: impl FnOnce(VerifiedArtifact) -> T,
) -> Result<Obtained<T>, DownloadError> {
    let asset = &request.asset;
    let target = Target {
        name: request.name.clone(),
        asset: asset.asset_name.as_str().to_owned(),
        url: asset.url.as_str().to_owned(),
    };
    // 1. Exclusion before any I/O or event (FR-006).
    let downloads = env.ctx.roots.staging_dir().join(DOWNLOADS_DIR);
    let Some(_flight) = InFlight::enter(&downloads, asset.sha256) else {
        return Err(DownloadError::Busy {
            target: Box::new(target),
        });
    };
    // 2. A token set before the call (FR-017).
    if env.cancel.is_cancelled() {
        return Err(DownloadError::Cancelled {
            target: Box::new(target),
        });
    }
    // 3. Paths, the downloads directory, the prune.
    let paths = staged_paths(
        &downloads,
        &hex_lower(&asset.sha256),
        asset.asset_name.as_str(),
    )?;
    env.ctx.roots.check_sandboxed(&paths.verified);
    let job = Job {
        env,
        request,
        target: &target,
        paths: &paths,
    };
    env.io
        .create_dir_all(&downloads)
        .map_err(|e| job.staging(StagingStep::CreateDir, &e))?;
    prune(env.io, &downloads, &live_hashes(feed, request));
    let mut meter = Meter {
        progress,
        size: asset.size,
        on_disk: 0,
        high_water: 0,
        since_last: 0,
    };
    // 4. A cached verified file (FR-046).
    let cache_failed = match job.cached(&mut meter)? {
        Cached::Hit(artifact) => {
            meter.emit(Progress::Verified);
            return Ok(Obtained {
                value: next(artifact),
                resumed: false,
                cached: true,
                transferred: 0,
            });
        }
        Cached::Miss => false,
        Cached::Invalid => true,
    };
    // 5. An existing partial.
    let foreign = !cache_failed && job.existing_partial(&mut meter)?;
    // 6. The budget (PD-7).
    let state = RefCell::new(Attempt {
        meter,
        file: None,
        verdict: None,
        failed: None,
        foreign,
        writers: 0,
        wrote: false,
        transferred: 0,
        requests: 0,
        budget: env.policy.attempts.max(1),
        redo_left: !cache_failed,
    });
    loop {
        // 7. The transfer.
        job.transfer(&state)?;
        // 8. Verify the `.part`, then rename it into place.
        let mut st = state.borrow_mut();
        st.meter.emit(Progress::Verifying);
        match job.verify(&paths.part) {
            Ok(artifact) => {
                env.io
                    .create_dir_all(&paths.dir)
                    .map_err(|e| job.staging(StagingStep::CreateDir, &e))?;
                env.io
                    .rename(&paths.part, &paths.verified)
                    .map_err(|e| job.staging(StagingStep::Rename, &e))?;
                let artifact = artifact.relocated(paths.verified.clone());
                st.meter.emit(Progress::Verified);
                let (resumed, transferred) = (st.assembled(), st.transferred);
                drop(st);
                return Ok(Obtained {
                    value: next(artifact),
                    resumed,
                    cached: false,
                    transferred,
                });
            }
            Err(VerifyError::Sha256 { .. }) if st.assembled() && st.redo_left => {
                // D4: one full re-download of a file assembled from more than one response.
                match env.io.remove_file(&paths.part) {
                    Err(e) if e.kind() != io::ErrorKind::NotFound => {
                        return Err(job.staging(StagingStep::Remove, &e));
                    }
                    _ => {}
                }
                st.meter.restarted(RestartReason::HashMismatchAfterResume);
                st.redo_left = false;
                st.budget += 1;
                st.foreign = false;
                st.writers = 0;
            }
            Err(error) => {
                job.discard_part();
                return Err(DownloadError::Verify {
                    target: Box::new(target.clone()),
                    error,
                });
            }
        }
    }
}

/// Remove a verified artifact's `<hex>/` directory after use (FR-049). The directory is re-derived from the
/// artifact's hash and asset, and the in-flight registry is held while it is removed (PD-18).
pub fn discard(artifact: VerifiedArtifact, io: &dyn StagingIo) -> Result<(), DownloadError> {
    let not_staged = || DownloadError::Path {
        reason: "the artifact is not at a staged path",
    };
    let downloads = artifact
        .path()
        .parent()
        .and_then(Path::parent)
        .ok_or_else(not_staged)?;
    let paths = staged_paths(
        downloads,
        &hex_lower(artifact.sha256()),
        artifact.asset().as_str(),
    )?;
    if paths.verified != artifact.path() {
        return Err(not_staged());
    }
    let Some(_flight) = InFlight::enter(downloads, *artifact.sha256()) else {
        return Err(DownloadError::DiscardBusy {
            name: artifact.name().to_owned(),
            asset: artifact.asset().as_str().to_owned(),
        });
    };
    match io.remove_dir_all(&paths.dir) {
        Err(e) if e.kind() != io::ErrorKind::NotFound => {
            // A `VerifiedArtifact` carries no feed URL (PD-18).
            let target = Target {
                name: artifact.name().to_owned(),
                asset: artifact.asset().as_str().to_owned(),
                url: String::new(),
            };
            Err(staging_error(&target, StagingStep::Remove, e.kind()))
        }
        _ => Ok(()),
    }
}

/// The command's feed check and request for `os` and `arch` (FR-060, FR-065; Phase 9 PD-1): no staging I/O
/// happens here.
pub fn command_request_for(
    ctx: &AppContext,
    name: &str,
    expected_version: &str,
    now_unix: u64,
    host: &dyn HostFacts,
    os: Os,
    arch: Arch,
) -> Result<(Feed, DownloadRequest), DownloadError> {
    let FeedCheck {
        snapshot, error, ..
    } = feed::check(ctx, FetchMode::Normal, now_unix);
    let Some(snapshot) = snapshot else {
        // `feed::check` returns no snapshot only together with its error.
        return Err(DownloadError::NoFeed {
            cause: error.as_ref().map(FeedError::code).unwrap_or_default(),
            detail: error.map(|e| scrub(&e.to_string())).unwrap_or_default(),
        });
    };
    let request = request_in_feed(&snapshot.feed, name, expected_version, host, os, arch)?;
    Ok((snapshot.feed, request))
}

/// The request for `name` at `expected_version` in a feed already checked (Phase 12 PD-2 (a)): the running OS
/// version, `request_for_platform`, the version compare. No feed request and no staging I/O happen here.
pub fn request_in_feed(
    feed: &Feed,
    name: &str,
    expected_version: &str,
    host: &dyn HostFacts,
    os: Os,
    arch: Arch,
) -> Result<DownloadRequest, DownloadError> {
    let running = host.os_version();
    let request = request_for_platform(feed, name, os, arch, running.as_ref())?;
    if request.version_text != expected_version {
        return Err(DownloadError::VersionChanged {
            name: name.to_owned(),
            expected: expected_version.to_owned(),
            found: request.version_text,
        });
    }
    Ok(request)
}

/// `command_request_for` for the native OS and architecture (FR-060, FR-065).
pub fn command_request(
    ctx: &AppContext,
    name: &str,
    expected_version: &str,
    now_unix: u64,
    host: &dyn HostFacts,
) -> Result<(Feed, DownloadRequest), DownloadError> {
    command_request_for(
        ctx,
        name,
        expected_version,
        now_unix,
        host,
        NativePlatform.os(),
        Arch::native(),
    )
}

/// `obtain_and` for the command, summarised (FR-060, FR-062).
pub fn run_command(
    env: DownloadEnv<'_>,
    feed: &Feed,
    request: &DownloadRequest,
    progress: &mut dyn FnMut(Progress),
) -> Result<DownloadSummary, DownloadError> {
    let obtained = obtain_and(env, feed, request, progress, |a| DownloadSummary {
        name: a.name().to_owned(),
        version: a.version().to_owned(),
        platform: a.platform().as_str().to_owned(),
        asset: a.asset().as_str().to_owned(),
        size: a.size(),
        resumed: false,
    })?;
    Ok(DownloadSummary {
        resumed: obtained.resumed,
        ..obtained.value
    })
}

/// `command_request` then `run_command`, nothing else (FR-060).
pub fn for_command(
    env: DownloadEnv<'_>,
    name: &str,
    expected_version: &str,
    now_unix: u64,
    host: &dyn HostFacts,
    progress: &mut dyn FnMut(Progress),
) -> Result<DownloadSummary, DownloadError> {
    crate::self_update::action_gate(env.ctx.roots.state_dir())?;
    let (feed, request) = command_request(env.ctx, name, expected_version, now_unix, host)?;
    run_command(env, &feed, &request, progress)
}

/// The `Channel` adapter (FR-064): each event to `send`, a `send` error dropped.
pub fn channel_progress<E>(
    mut send: impl FnMut(Progress) -> Result<(), E>,
) -> impl FnMut(Progress) {
    move |event: Progress| {
        // The webview may be gone; the download goes on (FR-064).
        let _ = send(event);
    }
}

#[cfg(test)]
#[path = "download_tests.rs"]
mod tests;
