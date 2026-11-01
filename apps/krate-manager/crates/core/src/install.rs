//! Install, update and uninstall of one plugin's Linux `tar.gz` (Phase 9, spec FR-001 to FR-124; plan §3, §4.1).
//! Every filesystem step goes through `InstallIo` (FR-028); only `driver_for` decides where a driver is offered.

use crate::context::AppContext;
use crate::detect::{self, CopyFormat, CopyVersion, Detection, HostFacts, Listing, ListingLimits};
use crate::download::{self, DownloadEnv, DownloadError, Progress};
use crate::extract::{self, ExtractEnv, ExtractError, NodeKind, StagedTree};
use crate::feed::{self, AssetFormat, FetchMode, PluginEntry};
use crate::platform::{NativePlatform, Os, Platform};
use crate::status;
use crate::verify::{self, Sha256Stream, VerifiedArtifact};
use crate::version::{self, Arch, Availability, CoreVersion, Installed, UpdateState};
use log::{info, warn};
use serde::{Deserialize, Serialize};
use std::cell::{Cell, RefCell};
use std::collections::BTreeSet;
use std::ffi::OsString;
use std::io;
use std::path::{Component, Path, PathBuf};
use std::sync::{Mutex, PoisonError};

/// The `log` target of every install line (FR-117).
pub const LOG_TARGET: &str = "krate_manager_core::install";
/// The journal folder under the state dir (FR-050).
pub const JOURNAL_DIR: &str = "journal";
/// The suffix of a sibling staged beside its destination (FR-022).
pub const NEW_SUFFIX: &str = ".krate-new";
/// The suffix of the previous bundle while an update commits (FR-024).
pub const OLD_SUFFIX: &str = ".krate-old";
/// The journal read bound: two manifests and 64 KiB of journal fields (FR-050, plan PD-21).
pub const MAX_JOURNAL_BYTES: u64 = 2 * detect::MAX_MANIFEST_BYTES + (64 << 10);

/// The copy and hash chunk (plan §4.1.2).
const CHUNK: usize = 64 << 10;
/// The reason of a state subdirectory that exists as another kind (plan PD-28).
const NOT_A_DIR: &str = "state directory is not a directory";

// ---- the error model (plan §3) -------------------------------------------------------------------------------------

/// The root a relative path belongs to (FR-040, FR-091).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RootTag {
    Plugin,
    Presets,
}

/// A path the action kept or refers to: the root kind and a relative POSIX path only (FR-091).
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct KeptPath {
    pub root: RootTag,
    pub path: String,
}

/// The FR-024 / FR-061 step a mutating call belongs to (FR-029).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Step {
    JournalBegun,
    BundleStaged,
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
    RemoveBegun,
    RemoveEntries,
    ManifestRemoved,
}

impl Step {
    /// The step's snake_case name, as the error `Display` and the logs spell it (FR-029).
    pub fn as_str(self) -> &'static str {
        match self {
            Step::JournalBegun => "journal_begun",
            Step::BundleStaged => "bundle_staged",
            Step::PresetsStaged => "presets_staged",
            Step::JournalStaged => "journal_staged",
            Step::OldAside => "old_aside",
            Step::NewIn => "new_in",
            Step::PresetsIn => "presets_in",
            Step::ManifestWritten => "manifest_written",
            Step::StaleRemoved => "stale_removed",
            Step::JournalDone => "journal_done",
            Step::OldDeleted => "old_deleted",
            Step::JournalRemoved => "journal_removed",
            Step::RemoveBegun => "remove_begun",
            Step::RemoveEntries => "remove_entries",
            Step::ManifestRemoved => "manifest_removed",
        }
    }
}

/// Why an action was refused or failed (FR-029). Every `Display` begins with its code; no variant holds an absolute
/// path (FR-091, FR-117).
#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum InstallError {
    #[error("E_INSTALL_ACTION: {given:?} is not install or update")]
    Action { given: String },
    #[error("E_INSTALL_NO_DRIVER: no install driver on this system")]
    NoDriver,
    #[error("E_INSTALL_BUSY: {name} has an action running")]
    Busy { name: String },
    #[error("E_INSTALL_NO_FEED: {cause}")]
    NoFeed { cause: &'static str },
    #[error("E_INSTALL_UNKNOWN_PLUGIN: the feed has no plugin {name}")]
    UnknownPlugin { name: String },
    #[error("E_INSTALL_NOT_MANAGEABLE: {name} is not available through the manager")]
    NotManageable { name: String },
    #[error("E_INSTALL_STATE_CHANGED: shown {shown}, now {current}")]
    StateChanged {
        shown: &'static str,
        current: &'static str,
    },
    #[error("E_INSTALL_NOT_NEWER: installed {installed}, offered {offered}")]
    NotNewer { installed: String, offered: String },
    #[error("E_INSTALL_NOT_MANAGED: {name} was not installed by the manager")]
    NotManaged { name: String },
    #[error("E_INSTALL_JOURNAL_PENDING: {name} has an unfinished install record")]
    JournalPending { name: String },
    #[error("E_INSTALL_JOURNAL: {reason}")]
    Journal { reason: &'static str },
    #[error("E_INSTALL_MANIFEST: {reason}")]
    Manifest { reason: &'static str },
    #[error(
        "E_INSTALL_ROOT_CHANGED: installed under another {which} root; set the Linux plug-in root back to the one it was installed under"
    )]
    RootChanged { which: &'static str },
    #[error("E_INSTALL_OUTSIDE_ROOT: {path:?} resolves outside its root")]
    OutsideRoot { path: KeptPath },
    #[error("E_INSTALL_PATH: {reason}")]
    Path { reason: &'static str },
    #[error("E_INSTALL_PATH_NOT_UTF8: a {which} root is not UTF-8")]
    PathNotUtf8 { which: &'static str },
    #[error("E_INSTALL_COLLISION: {path:?} is in the way")]
    Collision { path: KeptPath },
    #[error("E_INSTALL_UNSUPPORTED: this filesystem cannot hold a symlink")]
    Unsupported,
    #[error("E_INSTALL_FORMAT: the artifact is not a tar.gz")]
    Format,
    #[error("E_INSTALL_CHANGED: {path:?} changed after staging")]
    Changed { path: KeptPath },
    #[error("E_INSTALL_FULL: step {}", .step.as_str())]
    Full { step: Step },
    #[error("E_INSTALL_DENIED: step {}", .step.as_str())]
    Denied { step: Step },
    #[error("E_INSTALL_IO: step {}: {kind:?}", .step.as_str())]
    Io { step: Step, kind: io::ErrorKind },
    /// A Phase 6 `E_RECORD_*` code (FR-041).
    #[error("{0}")]
    Record(&'static str),
    /// A Phase 7 `E_DOWNLOAD_*` error (FR-090), boxed to keep the `Result` small.
    #[error("{0}")]
    Download(Box<DownloadError>),
    /// A Phase 8 `E_EXTRACT_*` / `E_LISTING_*` / `E_LAYOUT` error (FR-002).
    #[error("{0}")]
    Extract(ExtractError),
}

impl InstallError {
    /// The code the `Display` begins with; the inner code for the three wrappers (FR-029).
    pub fn code(&self) -> &'static str {
        match self {
            InstallError::Action { .. } => "E_INSTALL_ACTION",
            InstallError::NoDriver => "E_INSTALL_NO_DRIVER",
            InstallError::Busy { .. } => "E_INSTALL_BUSY",
            InstallError::NoFeed { .. } => "E_INSTALL_NO_FEED",
            InstallError::UnknownPlugin { .. } => "E_INSTALL_UNKNOWN_PLUGIN",
            InstallError::NotManageable { .. } => "E_INSTALL_NOT_MANAGEABLE",
            InstallError::StateChanged { .. } => "E_INSTALL_STATE_CHANGED",
            InstallError::NotNewer { .. } => "E_INSTALL_NOT_NEWER",
            InstallError::NotManaged { .. } => "E_INSTALL_NOT_MANAGED",
            InstallError::JournalPending { .. } => "E_INSTALL_JOURNAL_PENDING",
            InstallError::Journal { .. } => "E_INSTALL_JOURNAL",
            InstallError::Manifest { .. } => "E_INSTALL_MANIFEST",
            InstallError::RootChanged { .. } => "E_INSTALL_ROOT_CHANGED",
            InstallError::OutsideRoot { .. } => "E_INSTALL_OUTSIDE_ROOT",
            InstallError::Path { .. } => "E_INSTALL_PATH",
            InstallError::PathNotUtf8 { .. } => "E_INSTALL_PATH_NOT_UTF8",
            InstallError::Collision { .. } => "E_INSTALL_COLLISION",
            InstallError::Unsupported => "E_INSTALL_UNSUPPORTED",
            InstallError::Format => "E_INSTALL_FORMAT",
            InstallError::Changed { .. } => "E_INSTALL_CHANGED",
            InstallError::Full { .. } => "E_INSTALL_FULL",
            InstallError::Denied { .. } => "E_INSTALL_DENIED",
            InstallError::Io { .. } => "E_INSTALL_IO",
            InstallError::Record(code) => code,
            InstallError::Download(e) => e.code(),
            InstallError::Extract(e) => e.code(),
        }
    }
}

/// PD-10: the Phase 7 / 8 kind mapping (FR-029).
fn io_err(step: Step, e: &io::Error) -> InstallError {
    match e.kind() {
        io::ErrorKind::StorageFull | io::ErrorKind::QuotaExceeded => InstallError::Full { step },
        io::ErrorKind::PermissionDenied | io::ErrorKind::ReadOnlyFilesystem => {
            InstallError::Denied { step }
        }
        kind => InstallError::Io { step, kind },
    }
}

/// `io_err` for one step, as a `map_err` argument.
fn at(step: Step) -> impl Fn(io::Error) -> InstallError {
    move |e| io_err(step, &e)
}

// ---- constants and public types (plan §4.1.1) ----------------------------------------------------------------------

/// The three actions (FR-001).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ActionKind {
    Install,
    Update,
    Remove,
}

impl ActionKind {
    fn as_str(self) -> &'static str {
        match self {
            ActionKind::Install => "install",
            ActionKind::Update => "update",
            ActionKind::Remove => "remove",
        }
    }
}

/// One action of a batch (FR-001). No variant can be built without a `VerifiedArtifact`, which only verification
/// makes (SC-007):
///
/// ```compile_fail
/// use krate_manager_core::feed::{AssetFormat, AssetName, PlatformKey, PluginEntry};
/// use krate_manager_core::install::Action;
/// use krate_manager_core::verify::VerifiedArtifact;
/// fn forge(entry: PluginEntry, asset: AssetName) -> Action {
///     Action::Install {
///         entry,
///         artifact: VerifiedArtifact {
///             path: std::path::PathBuf::new(), name: String::new(), version: String::new(),
///             platform: PlatformKey::LinuxX86_64, asset, size: 0, sha256: [0; 32],
///             format: AssetFormat::TarGz,
///         },
///     }
/// }
/// ```
#[derive(Debug)]
pub enum Action {
    Install {
        entry: PluginEntry,
        artifact: VerifiedArtifact,
    },
    Update {
        entry: PluginEntry,
        artifact: VerifiedArtifact,
    },
    Uninstall {
        entry: PluginEntry,
    },
}

impl Action {
    /// The action's kind; `Uninstall` is `Remove` (FR-001).
    pub fn kind(&self) -> ActionKind {
        match self {
            Action::Install { .. } => ActionKind::Install,
            Action::Update { .. } => ActionKind::Update,
            Action::Uninstall { .. } => ActionKind::Remove,
        }
    }

    /// The feed entry every per-plugin fact comes from (FR-003).
    pub fn entry(&self) -> &PluginEntry {
        match self {
            Action::Install { entry, .. }
            | Action::Update { entry, .. }
            | Action::Uninstall { entry } => entry,
        }
    }
}

/// What one successful action did (FR-001, FR-091).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Outcome {
    pub version: Option<String>,
    pub placed: u64,
    pub removed: u64,
    pub kept: Vec<KeptPath>,
}

/// One result per action of a batch, in batch order (FR-001).
#[derive(Debug)]
pub struct ActionResult {
    pub name: String,
    pub action: ActionKind,
    pub outcome: Result<Outcome, InstallError>,
}

/// The notice a card shows after a successful install or update (FR-095).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Notice {
    RescanDaw,
}

/// What a command returns to the webview: no absolute path (FR-091).
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct ActionSummary {
    pub name: String,
    pub action: ActionKind,
    pub version: Option<String>,
    pub kept: Vec<KeptPath>,
    pub notice: Option<Notice>,
}

/// One progress event of a command, serialized for the `Channel` (FR-092, plan PD-11).
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum InstallProgress {
    Download { event: Progress },
    Staging,
    Placing { done: u64, total: u64 },
    Committing,
    Removing { done: u64, total: u64 },
    Done,
}

/// An OS's install driver: runs a batch in order, one result per action (FR-001, D10).
pub trait InstallDriver: Sync {
    fn os(&self) -> Os;
    fn run(&self, env: InstallEnv<'_>, batch: Vec<Action>) -> Vec<ActionResult>;
}

/// Which driver an OS has (FR-004, plan PD-5).
pub trait DriverResolver {
    fn driver_for(&self, os: Os) -> Option<&dyn InstallDriver>;
}

/// The production resolver: `install::driver_for` (FR-096, plan PD-5).
pub struct NativeDrivers;

impl DriverResolver for NativeDrivers {
    fn driver_for(&self, os: Os) -> Option<&dyn InstallDriver> {
        driver_for(os)
    }
}

/// The unprivileged Linux driver (FR-002); its logic is OS-neutral, `driver_for` offers it on Linux only (FR-004).
pub struct LinuxDriver;

impl InstallDriver for LinuxDriver {
    fn os(&self) -> Os {
        Os::Linux
    }

    /// The batch in order; a failure never stops the next action; one `info!` per result (FR-001, FR-117).
    fn run(&self, env: InstallEnv<'_>, batch: Vec<Action>) -> Vec<ActionResult> {
        let InstallEnv {
            ctx,
            io,
            extract,
            progress,
        } = env;
        batch
            .into_iter()
            .map(|action| {
                let name = action.entry().name.clone();
                let version = action.entry().version_text.clone();
                let kind = action.kind();
                let outcome = match action {
                    Action::Install { entry, artifact } => run_install(
                        ctx,
                        io,
                        extract,
                        &mut *progress,
                        &entry,
                        &artifact,
                        ActionKind::Install,
                    ),
                    Action::Update { entry, artifact } => run_install(
                        ctx,
                        io,
                        extract,
                        &mut *progress,
                        &entry,
                        &artifact,
                        ActionKind::Update,
                    ),
                    Action::Uninstall { entry } => run_uninstall(ctx, io, &mut *progress, &entry),
                };
                log_result(&name, &version, kind, &outcome);
                ActionResult {
                    name,
                    action: kind,
                    outcome,
                }
            })
            .collect()
    }
}

static LINUX: LinuxDriver = LinuxDriver;

/// The Linux driver for `Os::Linux`, none elsewhere until Phase 10 (FR-004).
pub fn driver_for(os: Os) -> Option<&'static dyn InstallDriver> {
    matches!(os, Os::Linux).then_some(&LINUX)
}

/// What a driver run borrows (FR-001).
pub struct InstallEnv<'a> {
    pub ctx: &'a AppContext,
    pub io: &'a dyn InstallIo,
    pub extract: ExtractEnv<'a>,
    pub progress: &'a mut dyn FnMut(InstallProgress),
}

/// What a command borrows (FR-004, FR-090): the OS and architecture it acts for, the context, the I/O seams and the
/// driver resolver. `Copy` (tasks TA-4).
#[derive(Clone, Copy)]
pub struct CommandEnv<'a> {
    pub os: Os,
    pub arch: Arch,
    pub ctx: &'a AppContext,
    pub io: &'a dyn InstallIo,
    pub extract: ExtractEnv<'a>,
    pub download: DownloadEnv<'a>,
    pub drivers: &'a dyn DriverResolver,
    #[cfg(test)]
    pub(crate) request_seam:
        Option<&'a dyn Fn(crate::download::DownloadRequest) -> crate::download::DownloadRequest>,
}

/// The one production `CommandEnv`: the native OS and architecture and `NativeDrivers` (FR-096, plan PD-6).
pub fn native_env<'a>(
    ctx: &'a AppContext,
    io: &'a dyn InstallIo,
    extract: ExtractEnv<'a>,
    download: DownloadEnv<'a>,
) -> CommandEnv<'a> {
    CommandEnv {
        os: NativePlatform.os(),
        arch: Arch::native(),
        ctx,
        io,
        extract,
        download,
        drivers: &NativeDrivers,
        #[cfg(test)]
        request_seam: None,
    }
}

/// The `Channel` adapter (FR-092, plan PD-12): each event to `send`, a `send` error dropped.
pub fn channel_progress<E>(
    mut send: impl FnMut(InstallProgress) -> Result<(), E>,
) -> impl FnMut(InstallProgress) {
    move |event: InstallProgress| {
        // The webview may be gone; the action goes on (FR-092).
        let _ = send(event);
    }
}

// ---- InstallIo and FsInstallIo (plan §4.1.2) -----------------------------------------------------------------------

/// Every filesystem step of Phase 9 (FR-028): the only way this module touches the filesystem.
pub trait InstallIo: Sync {
    // read
    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>>;
    fn read_link(&self, path: &Path) -> io::Result<PathBuf>;
    fn hash_file(&self, path: &Path) -> io::Result<[u8; 32]>;
    fn read_dir(&self, dir: &Path) -> io::Result<Vec<(OsString, NodeKind)>>;
    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>>;
    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf>;
    fn supports_symlinks(&self) -> bool;
    // mutating
    fn create_dir_all(&self, path: &Path) -> io::Result<()>;
    fn create_dir(&self, path: &Path) -> io::Result<()>;
    fn copy_new(&self, src: &Path, dest: &Path, sink: &mut dyn FnMut(&[u8])) -> io::Result<u64>;
    fn write_new(&self, path: &Path, bytes: &[u8]) -> io::Result<()>;
    fn symlink(&self, target: &str, link: &Path) -> io::Result<()>;
    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()>;
    fn sync_file(&self, path: &Path) -> io::Result<()>;
    fn sync_dir(&self, dir: &Path) -> io::Result<()>;
    fn rename(&self, from: &Path, to: &Path) -> io::Result<()>;
    fn remove_file(&self, path: &Path) -> io::Result<()>;
    fn remove_dir(&self, path: &Path) -> io::Result<()>;
    fn remove_tree(&self, path: &Path) -> io::Result<()>;
}

/// The real filesystem (FR-028): `std::fs` only here (plan PD-20); modes, symlinks and directory syncs on Unix.
pub struct FsInstallIo;

impl InstallIo for FsInstallIo {
    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>> {
        match std::fs::symlink_metadata(path) {
            Ok(meta) => {
                let t = meta.file_type();
                Ok(Some(if t.is_symlink() {
                    NodeKind::Symlink
                } else if t.is_dir() {
                    NodeKind::Dir
                } else if t.is_file() {
                    NodeKind::File
                } else {
                    NodeKind::Other
                }))
            }
            // NotADirectory: an ancestor is not a directory (Linux ENOTDIR; Windows reports NotFound), so the path
            // does not exist either (a preset folder replaced by a file, FR-061).
            Err(e)
                if matches!(
                    e.kind(),
                    io::ErrorKind::NotFound | io::ErrorKind::NotADirectory
                ) =>
            {
                Ok(None)
            }
            Err(e) => Err(e),
        }
    }

    fn read_link(&self, path: &Path) -> io::Result<PathBuf> {
        std::fs::read_link(path)
    }

    fn hash_file(&self, path: &Path) -> io::Result<[u8; 32]> {
        use std::io::Read as _;
        if !std::fs::symlink_metadata(path)?.file_type().is_file() {
            return Err(io::Error::from(io::ErrorKind::InvalidInput));
        }
        let mut file = std::fs::File::open(path)?;
        let mut sha = Sha256Stream::new();
        let mut buf = vec![0u8; CHUNK];
        loop {
            let n = file.read(&mut buf)?;
            if n == 0 {
                return Ok(sha.finish());
            }
            sha.update(&buf[..n]);
        }
    }

    fn read_dir(&self, dir: &Path) -> io::Result<Vec<(OsString, NodeKind)>> {
        let mut out = Vec::new();
        for item in std::fs::read_dir(dir)? {
            let item = item?;
            let t = item.file_type()?;
            let kind = if t.is_symlink() {
                NodeKind::Symlink
            } else if t.is_dir() {
                NodeKind::Dir
            } else if t.is_file() {
                NodeKind::File
            } else {
                NodeKind::Other
            };
            out.push((item.file_name(), kind));
        }
        out.sort_by(|a, b| a.0.cmp(&b.0));
        Ok(out)
    }

    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        use std::io::Read as _;
        let mut out = Vec::new();
        std::fs::File::open(path)?
            .take(max.saturating_add(1))
            .read_to_end(&mut out)?;
        Ok(out)
    }

    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        std::fs::canonicalize(path)
    }

    fn supports_symlinks(&self) -> bool {
        cfg!(unix)
    }

    fn create_dir_all(&self, path: &Path) -> io::Result<()> {
        std::fs::create_dir_all(path)?;
        if std::fs::symlink_metadata(path)?.is_dir() {
            Ok(())
        } else {
            Err(io::Error::from(io::ErrorKind::InvalidInput))
        }
    }

    fn create_dir(&self, path: &Path) -> io::Result<()> {
        std::fs::create_dir(path)
    }

    fn copy_new(&self, src: &Path, dest: &Path, sink: &mut dyn FnMut(&[u8])) -> io::Result<u64> {
        use std::io::{Read as _, Write as _};
        let mut from = std::fs::File::open(src)?;
        let mut to = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(dest)?;
        let mut buf = vec![0u8; CHUNK];
        let mut total = 0u64;
        loop {
            let n = from.read(&mut buf)?;
            if n == 0 {
                return Ok(total);
            }
            sink(&buf[..n]);
            to.write_all(&buf[..n])?;
            total += n as u64;
        }
    }

    fn write_new(&self, path: &Path, bytes: &[u8]) -> io::Result<()> {
        use std::io::Write as _;
        std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(path)?
            .write_all(bytes)
    }

    fn symlink(&self, target: &str, link: &Path) -> io::Result<()> {
        #[cfg(unix)]
        {
            std::os::unix::fs::symlink(target, link)
        }
        #[cfg(not(unix))]
        {
            let _ = (target, link);
            Err(io::Error::from(io::ErrorKind::Unsupported))
        }
    }

    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()> {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt as _;
            std::fs::set_permissions(path, std::fs::Permissions::from_mode(mode))
        }
        #[cfg(not(unix))]
        {
            let _ = (path, mode);
            Ok(())
        }
    }

    fn sync_file(&self, path: &Path) -> io::Result<()> {
        // PD-9: FlushFileBuffers needs a writable handle; no create, no truncate.
        std::fs::OpenOptions::new()
            .write(true)
            .open(path)?
            .sync_all()
    }

    fn sync_dir(&self, dir: &Path) -> io::Result<()> {
        #[cfg(unix)]
        {
            std::fs::File::open(dir)?.sync_all()
        }
        #[cfg(not(unix))]
        {
            let _ = dir;
            Ok(())
        }
    }

    fn rename(&self, from: &Path, to: &Path) -> io::Result<()> {
        std::fs::rename(from, to)
    }

    fn remove_file(&self, path: &Path) -> io::Result<()> {
        std::fs::remove_file(path)
    }

    fn remove_dir(&self, path: &Path) -> io::Result<()> {
        std::fs::remove_dir(path)
    }

    fn remove_tree(&self, path: &Path) -> io::Result<()> {
        // PD-19: by kind, never following a link.
        match std::fs::symlink_metadata(path) {
            Ok(meta) if meta.file_type().is_dir() => std::fs::remove_dir_all(path),
            Ok(_) => std::fs::remove_file(path),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(()),
            Err(e) => Err(e),
        }
    }
}

// ---- small helpers --------------------------------------------------------------------------------------------------

fn hex(bytes: &[u8; 32]) -> String {
    verify::hex_lower(bytes)
}

fn is_hex64(s: &str) -> bool {
    s.len() == 64 && s.bytes().all(|b| matches!(b, b'0'..=b'9' | b'a'..=b'f'))
}

fn text(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

fn kept(root: RootTag, path: &str) -> KeptPath {
    KeptPath {
        root,
        path: path.to_owned(),
    }
}

/// `plugin:` / `presets:` plus the relative path: the only way a path reaches a log line (FR-117).
fn shown(k: &KeptPath) -> String {
    let root = match k.root {
        RootTag::Plugin => "plugin",
        RootTag::Presets => "presets",
    };
    format!("{root}:{}", k.path)
}

/// `path` below `root` as a `/`-joined relative string (`None` when not below, or not UTF-8).
fn rel_of(root: &Path, path: &Path) -> Option<String> {
    let rest = path.strip_prefix(root).ok()?;
    let mut parts = Vec::new();
    for c in rest.components() {
        match c {
            Component::Normal(s) => parts.push(s.to_str()?),
            _ => return None,
        }
    }
    (!parts.is_empty()).then(|| parts.join("/"))
}

/// Segment count, for "deepest first".
fn depth(rel: &str) -> usize {
    rel.split('/').count()
}

fn is_real_dir(kind: Option<NodeKind>) -> bool {
    kind == Some(NodeKind::Dir)
}

// ---- destinations, dest_path and the listing reader (plan §4.1.3) --------------------------------------------------

/// Every path one plugin's action may touch, checked (FR-010 to FR-016, FR-110; plan PD-23, PD-25).
#[derive(Clone, Debug)]
pub(crate) struct Destinations {
    plugin_root: PathBuf,
    target: PathBuf,
    new: PathBuf,
    old: PathBuf,
    preset_root: PathBuf,
    preset_dir: PathBuf,
    stem: String,
    name: String,
    vst3_bundle: String,
    preset_subdir: Option<String>,
    journal_dir: PathBuf,
    journal: PathBuf,
    manifests_dir: PathBuf,
    manifest: PathBuf,
}

impl Destinations {
    /// The entry's install facts (FR-003), then `from_parts` (PD-25). Pure: no `InstallIo` call (PD-23).
    pub(crate) fn of(ctx: &AppContext, entry: &PluginEntry) -> Result<Destinations, InstallError> {
        let Some(facts) = entry.release.as_ref().and_then(|r| r.install.as_ref()) else {
            return Err(InstallError::NotManageable {
                name: entry.name.clone(),
            });
        };
        Destinations::from_parts(
            ctx,
            &entry.name,
            facts.vst3_bundle.as_str(),
            facts.preset_subdir.as_ref().map(|s| s.as_str()),
        )
    }

    /// One constructor for feed entries and journals (PD-25): every string one segment before any join (FR-012),
    /// every path through `check_sandboxed` (FR-110), the three roots UTF-8 (FR-016).
    pub(crate) fn from_parts(
        ctx: &AppContext,
        name: &str,
        vst3_bundle: &str,
        preset_subdir: Option<&str>,
    ) -> Result<Destinations, InstallError> {
        let path = |reason| Err(InstallError::Path { reason });
        if !one_segment(name) {
            return path("the plugin name is not one path segment");
        }
        if !one_segment(vst3_bundle) {
            return path("the bundle name is not one path segment");
        }
        if preset_subdir.is_some_and(|s| !one_segment(s)) {
            return path("the preset subfolder is not one path segment");
        }
        let Some(stem) = vst3_bundle.strip_suffix(".vst3").filter(|s| !s.is_empty()) else {
            return path("the bundle name does not end in .vst3");
        };
        let plugin_root = ctx
            .settings
            .linux_plugin_root_or_default(&ctx.roots)
            .to_path_buf();
        let preset_root = ctx.roots.presets().user.clone();
        let mut preset_dir = preset_root.join(name);
        if let Some(sub) = preset_subdir {
            preset_dir.push(sub);
        }
        let state = ctx.roots.state_dir();
        let journal_dir = state.join(JOURNAL_DIR);
        let manifests_dir = state.join(detect::MANIFESTS_DIR);
        let dest = Destinations {
            target: plugin_root.join(vst3_bundle),
            new: plugin_root.join(format!("{vst3_bundle}{NEW_SUFFIX}")),
            old: plugin_root.join(format!("{vst3_bundle}{OLD_SUFFIX}")),
            journal: journal_dir.join(format!("{stem}.json")),
            manifest: manifests_dir.join(format!("{stem}.json")),
            plugin_root,
            preset_root,
            preset_dir,
            stem: stem.to_owned(),
            name: name.to_owned(),
            vst3_bundle: vst3_bundle.to_owned(),
            preset_subdir: preset_subdir.map(str::to_owned),
            journal_dir,
            manifests_dir,
        };
        for p in [
            &dest.plugin_root,
            &dest.target,
            &dest.new,
            &dest.old,
            &dest.preset_root,
            &dest.preset_dir,
            &dest.journal_dir,
            &dest.journal,
            &dest.manifests_dir,
            &dest.manifest,
        ] {
            ctx.roots.check_sandboxed(p);
        }
        if dest.plugin_root.to_str().is_none() {
            return Err(InstallError::PathNotUtf8 { which: "plugin" });
        }
        if dest.preset_root.to_str().is_none() || dest.preset_dir.to_str().is_none() {
            return Err(InstallError::PathNotUtf8 { which: "presets" });
        }
        Ok(dest)
    }

    fn root(&self, tag: RootTag) -> &Path {
        match tag {
            RootTag::Plugin => &self.plugin_root,
            RootTag::Presets => &self.preset_root,
        }
    }

    /// `preset_dir` relative to `preset_root`: `<Name>[/<preset_subdir>]`.
    fn preset_rel(&self) -> String {
        match &self.preset_subdir {
            Some(sub) => format!("{}/{sub}", self.name),
            None => self.name.clone(),
        }
    }
}

/// FR-012: the only join of an archive, manifest or journal string: `/`-split, every segment one normal component,
/// the result a strict descendant of `root`.
pub(crate) fn dest_path(root: &Path, rel: &str) -> Result<PathBuf, InstallError> {
    if rel.is_empty() || !rel.split('/').all(one_segment) {
        return Err(InstallError::Path {
            reason: "a relative path is not plain segments",
        });
    }
    let mut out = root.to_path_buf();
    for segment in rel.split('/') {
        out.push(segment);
    }
    Ok(out)
}

/// One `Component::Normal`: no `/`, `\`, `:`, NUL, not empty, not `.` or `..` (FR-012).
fn one_segment(s: &str) -> bool {
    if s.is_empty() || s == "." || s == ".." || s.contains(['/', '\\', ':', '\0']) {
        return false;
    }
    let mut parts = Path::new(s).components();
    matches!(parts.next(), Some(Component::Normal(_))) && parts.next().is_none()
}

/// One canonical listing line, split.
struct Line<'a> {
    kind: char,
    mode: u32,
    path: &'a str,
    target: Option<&'a str>,
}

/// `<t> <4 octal> <path>[ -> <target>]` (C4); the ` -> ` split is unambiguous for `l` (`CONTRACT.md:311`).
fn parse_line(line: &str) -> Result<Line<'_>, InstallError> {
    let bad = || InstallError::Path {
        reason: "a listing line is malformed",
    };
    let kind = line.chars().next().ok_or_else(bad)?;
    if !matches!(kind, 'f' | 'd' | 'l')
        || line.as_bytes().get(1) != Some(&b' ')
        || line.as_bytes().get(6) != Some(&b' ')
    {
        return Err(bad());
    }
    let mode = line
        .get(2..6)
        .and_then(|m| u32::from_str_radix(m, 8).ok())
        .ok_or_else(bad)?;
    let rest = line.get(7..).ok_or_else(bad)?;
    let (path, target) = if kind == 'l' {
        let (path, target) = rest.split_once(" -> ").ok_or_else(bad)?;
        (path, Some(target))
    } else {
        (rest, None)
    };
    Ok(Line {
        kind,
        mode,
        path,
        target,
    })
}

/// The canonical form of `root`, when it exists (a missing root makes every check under it vacuous, PD-24).
fn canonical_root(io: &dyn InstallIo, root: &Path) -> Result<Option<PathBuf>, io::Error> {
    match io.kind_of(root)? {
        None => Ok(None),
        Some(_) => io.canonicalize(root).map(Some),
    }
}

/// PD-17: the deepest existing ancestor of `parent` (inclusive), canonicalized, lies inside `root`; `true` when the
/// root does not exist. A canonicalize failure is "outside".
fn inside(io: &dyn InstallIo, root: Option<&Path>, parent: &Path) -> io::Result<bool> {
    let Some(root) = root else {
        return Ok(true);
    };
    let mut probe = parent;
    loop {
        if io.kind_of(probe)?.is_some() {
            return Ok(io.canonicalize(probe).is_ok_and(|c| c.starts_with(root)));
        }
        match probe.parent() {
            Some(up) => probe = up,
            None => return Ok(false),
        }
    }
}

// ---- the manifest (plan §4.1.4) ------------------------------------------------------------------------------------

/// The Phase 9 manifest, `<state>/manifests/<stem>.json` (FR-040); the three Phase 6 keys come first.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Manifest {
    pub manifest_version: u64,
    pub plugin: String,
    pub version: String,
    pub vst3_bundle: String,
    pub archive_sha256: String,
    pub plugin_root: String,
    pub preset_root: String,
    pub files: Vec<ManifestEntry>,
}

/// One placed node, relative to its root (FR-040).
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ManifestEntry {
    pub root: RootTag,
    #[serde(rename = "type")]
    pub kind: EntryTag,
    pub path: String,
    #[serde(skip_serializing_if = "Option::is_none", default)]
    pub sha256: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none", default)]
    pub target: Option<String>,
}

/// A manifest entry's type: file, directory or link (FR-040).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum EntryTag {
    #[serde(rename = "f")]
    F,
    #[serde(rename = "d")]
    D,
    #[serde(rename = "l")]
    L,
}

/// The whole manifest of `plugin`, read through `io` and Phase 6's three-key reader (FR-041, plan PD-14).
pub fn read_manifest_full(
    io: &dyn InstallIo,
    state_dir: &Path,
    stem: &str,
    plugin: &str,
) -> Result<Option<Manifest>, InstallError> {
    let dir = state_dir.join(detect::MANIFESTS_DIR);
    match io.kind_of(&dir) {
        Ok(None) => return Ok(None),
        Ok(Some(NodeKind::Dir)) => {}
        Ok(Some(_)) => return Err(InstallError::Manifest { reason: NOT_A_DIR }),
        Err(_) => return Err(InstallError::Record("E_RECORD_READ")),
    }
    let bytes = match io.read_file(
        &dir.join(format!("{stem}.json")),
        detect::MAX_MANIFEST_BYTES,
    ) {
        Ok(bytes) if bytes.len() as u64 <= detect::MAX_MANIFEST_BYTES => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        _ => return Err(InstallError::Record("E_RECORD_READ")),
    };
    match detect::parse_manifest(&bytes, plugin) {
        detect::RecordRead::Found(_) => {}
        detect::RecordRead::Failed(code) => return Err(InstallError::Record(code)),
        detect::RecordRead::Absent => return Ok(None),
    }
    let manifest: Manifest =
        serde_json::from_slice(&bytes).map_err(|_| InstallError::Manifest {
            reason: "a Phase 9 key is missing or has the wrong type",
        })?;
    check_manifest(&manifest, &format!("{stem}.vst3"))?;
    Ok(Some(manifest))
}

/// An absolute, normal path text (FR-041).
fn absolute_normal(s: &str) -> bool {
    let p = Path::new(s);
    p.is_absolute()
        && p.components().all(|c| {
            matches!(
                c,
                Component::Prefix(_) | Component::RootDir | Component::Normal(_)
            )
        })
}

/// FR-041's Phase 9 checks.
fn check_manifest(m: &Manifest, vst3_bundle: &str) -> Result<(), InstallError> {
    let bad = |reason| Err(InstallError::Manifest { reason });
    if m.vst3_bundle != vst3_bundle {
        return bad("the bundle is not the plugin's");
    }
    if !absolute_normal(&m.plugin_root) || !absolute_normal(&m.preset_root) {
        return bad("a root is not an absolute normal path");
    }
    if !is_hex64(&m.archive_sha256) {
        return bad("archive_sha256 is not 64 lower-case hex digits");
    }
    for e in &m.files {
        if dest_path(Path::new("r"), &e.path).is_err() {
            return bad("an entry path is not relative plain segments");
        }
        if e.root == RootTag::Plugin
            && e.path != m.vst3_bundle
            && !e.path.starts_with(&format!("{}/", m.vst3_bundle))
        {
            return bad("a plug-in entry lies outside the bundle");
        }
        let shape = match e.kind {
            EntryTag::F => e.sha256.as_deref().is_some_and(is_hex64) && e.target.is_none(),
            EntryTag::L => e.target.is_some() && e.sha256.is_none(),
            EntryTag::D => e.target.is_none() && e.sha256.is_none(),
        };
        if !shape {
            return bad("an entry's sha256 / target does not fit its type");
        }
    }
    Ok(())
}

/// `to_vec_pretty` plus LF (key order = field order).
fn json_bytes<T: Serialize>(value: &T, reason: &'static str) -> Result<Vec<u8>, InstallError> {
    let mut bytes =
        serde_json::to_vec_pretty(value).map_err(|_| InstallError::Journal { reason })?;
    bytes.push(b'\n');
    Ok(bytes)
}

/// FR-064 (PD-17): equal strings, else both canonical and equal; the recorded root goes through the sandbox
/// guard before it is read (FR-110).
fn roots_match(
    ctx: &AppContext,
    io: &dyn InstallIo,
    dest: &Destinations,
    m: &Manifest,
) -> Result<(), InstallError> {
    for (recorded, configured, which) in [
        (&m.plugin_root, &dest.plugin_root, "plugin"),
        (&m.preset_root, &dest.preset_root, "presets"),
    ] {
        if Path::new(recorded) == configured.as_path() {
            continue;
        }
        ctx.roots.check_sandboxed(Path::new(recorded));
        let same = match (
            io.canonicalize(Path::new(recorded)),
            io.canonicalize(configured),
        ) {
            (Ok(a), Ok(b)) => a == b,
            _ => false,
        };
        if !same {
            return Err(InstallError::RootChanged { which });
        }
    }
    Ok(())
}

/// FR-063: every entry joined to its root, its parent's deepest existing ancestor canonical inside the root.
fn resolve_entries(
    io: &dyn InstallIo,
    dest: &Destinations,
    m: &Manifest,
) -> Result<(), InstallError> {
    let read = at(Step::JournalBegun);
    let plugin = canonical_root(io, &dest.plugin_root).map_err(&read)?;
    let presets = canonical_root(io, &dest.preset_root).map_err(&read)?;
    for e in &m.files {
        let path = dest_path(dest.root(e.root), &e.path)?;
        let root = match e.root {
            RootTag::Plugin => plugin.as_deref(),
            RootTag::Presets => presets.as_deref(),
        };
        let parent = path.parent().unwrap_or(&path);
        if !inside(io, root, parent).map_err(&read)? {
            return Err(InstallError::OutsideRoot {
                path: kept(e.root, &e.path),
            });
        }
    }
    Ok(())
}

// ---- atomic write (plan §4.1.5) ------------------------------------------------------------------------------------

/// FR-044 through `InstallIo`: the directory (created when absent, refused when not a real one, PD-28), the temp
/// `.<name>.tmp`, sync, rename, and on Unix the directory sync.
fn write_atomic(
    io: &dyn InstallIo,
    dir: &Path,
    name: &str,
    bytes: &[u8],
    step: Step,
) -> Result<(), InstallError> {
    let err = at(step);
    match io.kind_of(dir).map_err(&err)? {
        // Another plugin's action may make it between the two calls (SC-014): a real directory then is fine.
        None => match io.create_dir(dir) {
            Ok(()) => {}
            Err(e)
                if e.kind() == io::ErrorKind::AlreadyExists
                    && io.kind_of(dir).map_err(&err)? == Some(NodeKind::Dir) => {}
            Err(e) => return Err(err(e)),
        },
        Some(NodeKind::Dir) => {}
        Some(_) => return Err(not_a_dir(dir)),
    }
    let temp = dir.join(format!(".{name}.tmp"));
    if io.kind_of(&temp).map_err(&err)? == Some(NodeKind::File) {
        io.remove_file(&temp).map_err(&err)?;
    }
    io.write_new(&temp, bytes).map_err(&err)?;
    let rest = io
        .sync_file(&temp)
        .and_then(|()| io.rename(&temp, &dir.join(name)))
        .and_then(|()| if cfg!(unix) { io.sync_dir(dir) } else { Ok(()) });
    if let Err(e) = rest {
        if io.kind_of(&temp).ok().flatten().is_some()
            && let Err(cleanup) = io.remove_file(&temp)
        {
            warn!(target: LOG_TARGET, "{}: a temp file was left", io_err(step, &cleanup).code());
        }
        return Err(io_err(step, &e));
    }
    Ok(())
}

/// PD-28's error, by the directory's name.
fn not_a_dir(dir: &Path) -> InstallError {
    if dir.file_name().is_some_and(|n| n == JOURNAL_DIR) {
        InstallError::Journal { reason: NOT_A_DIR }
    } else {
        InstallError::Manifest { reason: NOT_A_DIR }
    }
}

/// PD-28 for both state subdirectories, before any write.
fn check_state_dirs(io: &dyn InstallIo, dest: &Destinations) -> Result<(), InstallError> {
    for dir in [&dest.journal_dir, &dest.manifests_dir] {
        match io.kind_of(dir).map_err(at(Step::JournalBegun))? {
            None | Some(NodeKind::Dir) => {}
            Some(_) => return Err(not_a_dir(dir)),
        }
    }
    Ok(())
}

// ---- the journal (plan §4.1.6) -------------------------------------------------------------------------------------

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct Journal {
    journal_version: u64,
    op: JournalOp,
    plugin: String,
    stem: String,
    vst3_bundle: String,
    preset_subdir: Option<String>,
    step: JournalStep,
    target: String,
    new: String,
    old: String,
    had_old: bool,
    siblings: Vec<Sibling>,
    created_dirs: Vec<String>,
    pending_manifest: Option<Manifest>,
    previous_manifest: Option<Manifest>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
enum JournalOp {
    Install,
    Update,
    Remove,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
enum JournalStep {
    Begun,
    Staged,
    Done,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct Sibling {
    dest: String,
    new: String,
    pre: SiblingPre,
}

/// PD-26: what `dest` held when the sibling was planned.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
enum SiblingPre {
    Absent,
    Sha256(String),
}

impl Journal {
    fn empty(dest: &Destinations, op: JournalOp) -> Journal {
        Journal {
            journal_version: 1,
            op,
            plugin: dest.name.clone(),
            stem: dest.stem.clone(),
            vst3_bundle: dest.vst3_bundle.clone(),
            preset_subdir: dest.preset_subdir.clone(),
            step: JournalStep::Begun,
            target: text(&dest.target),
            new: text(&dest.new),
            old: text(&dest.old),
            had_old: false,
            siblings: Vec::new(),
            created_dirs: Vec::new(),
            pending_manifest: None,
            previous_manifest: None,
        }
    }
}

/// FR-043: only through `write_atomic`, into `<state>/journal/<stem>.json`.
fn write_journal(
    io: &dyn InstallIo,
    dest: &Destinations,
    journal: &Journal,
    step: Step,
) -> Result<(), InstallError> {
    let bytes = json_bytes(journal, "the journal does not serialise")?;
    write_atomic(
        io,
        &dest.journal_dir,
        &format!("{}.json", dest.stem),
        &bytes,
        step,
    )
}

/// FR-051, FR-073 (PD-25, PD-26): the journal names only this plugin's own paths under the current roots.
fn check_journal(
    io: &dyn InstallIo,
    j: &Journal,
    file_stem: &str,
    dest: &Destinations,
) -> Result<(), &'static str> {
    if j.stem != file_stem || j.stem != dest.stem {
        return Err("the journal is not this plugin's");
    }
    for m in [&j.pending_manifest, &j.previous_manifest]
        .into_iter()
        .flatten()
    {
        if m.vst3_bundle != j.vst3_bundle || m.plugin != j.plugin {
            return Err("a recorded manifest is not this plugin's");
        }
    }
    if j.target != text(&dest.target) || j.new != text(&dest.new) || j.old != text(&dest.old) {
        return Err("the bundle paths are not under the configured plug-in root");
    }
    let presets =
        canonical_root(io, &dest.preset_root).map_err(|_| "the preset root cannot be read")?;
    let mut parents = Vec::new();
    for s in &j.siblings {
        let d = PathBuf::from(&s.dest);
        let ok = rel_of(&dest.preset_dir, &d).is_some()
            && rel_of(&dest.preset_root, &d)
                .is_some_and(|rel| dest_path(&dest.preset_root, &rel).is_ok_and(|p| p == d))
            && s.new == format!("{}{NEW_SUFFIX}", s.dest)
            && match &s.pre {
                SiblingPre::Absent => true,
                SiblingPre::Sha256(h) => is_hex64(h),
            };
        if !ok {
            return Err("a sibling is not under the preset folder");
        }
        parents.push(d.parent().map(Path::to_path_buf).unwrap_or_default());
    }
    for c in &j.created_dirs {
        let d = PathBuf::from(c);
        let ok = rel_of(&dest.preset_root, &d)
            .is_some_and(|rel| dest_path(&dest.preset_root, &rel).is_ok_and(|p| p == d))
            && (dest.preset_dir.starts_with(&d) || d.starts_with(&dest.preset_dir));
        if !ok {
            return Err("a created directory is not on the preset folder's chain");
        }
        parents.push(d.parent().map(Path::to_path_buf).unwrap_or_default());
    }
    for parent in parents {
        if !inside(io, presets.as_deref(), &parent).unwrap_or(false) {
            return Err("a recorded path resolves outside the preset root");
        }
    }
    Ok(())
}

// ---- the planner (plan §4.1.7) -------------------------------------------------------------------------------------

/// One bundle node to make under `.krate-new`, in listing order.
enum BundleOp {
    Dir {
        dst: PathBuf,
    },
    File {
        src: PathBuf,
        dst: PathBuf,
        rel: String,
        sha256: [u8; 32],
        mode: u32,
    },
    Link {
        dst: PathBuf,
        target: String,
    },
}

/// One `Place` / `Replace` preset: a file copied, or a link made, at `<dest>.krate-new` (FR-022, PD-29).
struct SiblingOp {
    src: Option<PathBuf>,
    link: Option<String>,
    dest: PathBuf,
    new: PathBuf,
    rel: String,
    sha256: Option<[u8; 32]>,
    mode: u32,
    pre: SiblingPre,
}

/// A previous-manifest preset file or link the new layout lacks, with its D5 decision (FR-020).
enum StaleOp {
    /// Still what was recorded: removed at step 9 after a re-check (`matches_entry`).
    Remove { path: PathBuf, entry: ManifestEntry },
    /// Changed, or another kind: left and reported.
    Kept { rel: String },
}

struct InstallPlan {
    had_old: bool,
    create_plugin_root: bool,
    create_preset_root: bool,
    bundle: Vec<BundleOp>,
    siblings: Vec<SiblingOp>,
    create_dirs: Vec<PathBuf>,
    stale: Vec<StaleOp>,
    stale_dirs: Vec<PathBuf>,
    pending: Manifest,
    kept: Vec<KeptPath>,
    placed_files: u64,
}

fn collision(root: RootTag, path: &str) -> InstallError {
    InstallError::Collision {
        path: kept(root, path),
    }
}

/// The staged version ranked against an installed one: `Ok` only when the installed one is older (FR-025).
fn must_be_older(installed: &str, offered: &str) -> Result<(), InstallError> {
    let state = version::compare(
        CoreVersion::parse(installed).map_or(Installed::VersionUnknown, Installed::Version),
        CoreVersion::parse(offered),
    );
    if state == UpdateState::UpdateAvailable {
        Ok(())
    } else {
        Err(InstallError::NotNewer {
            installed: installed.to_owned(),
            offered: offered.to_owned(),
        })
    }
}

/// D4: read-only, every refusal before the first write (FR-014, FR-020, FR-021, FR-025, FR-063; PD-24 to PD-29).
fn plan(
    io: &dyn InstallIo,
    ctx: &AppContext,
    dest: &Destinations,
    tree: &StagedTree,
    entry: &PluginEntry,
    kind: ActionKind,
    previous: Option<&Manifest>,
) -> Result<InstallPlan, InstallError> {
    let read = at(Step::JournalBegun);
    let bundle_name = dest.vst3_bundle.as_str();
    // 0. State subdirectories (PD-28) and the one journal per plugin (FR-052).
    check_state_dirs(io, dest)?;
    if io.kind_of(&dest.journal).map_err(&read)?.is_some() {
        return Err(InstallError::JournalPending {
            name: dest.name.clone(),
        });
    }
    // 1. The plug-in root (FR-010, SI-13, SC-028).
    let create_plugin_root = match io.kind_of(&dest.plugin_root).map_err(&read)? {
        None => true,
        Some(NodeKind::Dir) => false,
        Some(NodeKind::Symlink)
            if io
                .canonicalize(&dest.plugin_root)
                .ok()
                .is_some_and(|c| is_real_dir(io.kind_of(&c).ok().flatten())) =>
        {
            false
        }
        Some(_) => return Err(collision(RootTag::Plugin, "")),
    };
    // 2. The target and FR-025's structural re-check (PD-27).
    let had_old = match io.kind_of(&dest.target).map_err(&read)? {
        None => false,
        Some(NodeKind::Dir) => true,
        Some(_) => return Err(collision(RootTag::Plugin, bundle_name)),
    };
    let offered = entry.version_text.as_str();
    match (kind, had_old) {
        (ActionKind::Install, true) => {
            return Err(InstallError::StateChanged {
                shown: "install",
                current: "installed",
            });
        }
        (ActionKind::Update, false) => {
            return Err(InstallError::StateChanged {
                shown: "update",
                current: "not_installed",
            });
        }
        (ActionKind::Update, true) => match previous {
            Some(m) => must_be_older(&m.version, offered)?,
            None => {
                let info = dest_path(&dest.target, "Contents/Resources/moduleinfo.json")?;
                let version = io
                    .read_file(&info, detect::MAX_MODULEINFO_BYTES)
                    .ok()
                    .filter(|b| b.len() as u64 <= detect::MAX_MODULEINFO_BYTES)
                    .and_then(|b| String::from_utf8(b).ok())
                    .map(|t| detect::moduleinfo_version(&t, &info));
                match version {
                    Some(CopyVersion::Known { version, .. }) => {
                        must_be_older(&version.to_string(), offered)?;
                    }
                    _ => {
                        return Err(InstallError::StateChanged {
                            shown: "update",
                            current: "unknown",
                        });
                    }
                }
            }
        },
        _ => {}
    }
    // 3. `.krate-new` / `.krate-old` strays (FR-014).
    for (path, suffix) in [(&dest.new, NEW_SUFFIX), (&dest.old, OLD_SUFFIX)] {
        if io.kind_of(path).map_err(&read)?.is_some() {
            return Err(collision(
                RootTag::Plugin,
                &format!("{bundle_name}{suffix}"),
            ));
        }
    }
    let lines: Vec<Line<'_>> = tree
        .listing()
        .lines()
        .iter()
        .map(|l| parse_line(l))
        .collect::<Result<_, _>>()?;
    // 4. Links on a filesystem without them (FR-015).
    if !io.supports_symlinks() && lines.iter().any(|l| l.kind == 'l') {
        return Err(InstallError::Unsupported);
    }
    // 5. The previous manifest's roots and entries (FR-063, FR-064, PD-17).
    if let Some(m) = previous {
        roots_match(ctx, io, dest, m)?;
        resolve_entries(io, dest, m)?;
    }
    // 5a. Link targets stay inside their top-level entry (PD-29).
    for l in lines.iter().filter(|l| l.kind == 'l') {
        let between = depth(l.path).saturating_sub(2);
        let ups = l
            .target
            .unwrap_or_default()
            .split('/')
            .take_while(|s| *s == "..")
            .count();
        if ups > between {
            return Err(InstallError::Path {
                reason: "link target leaves its top-level entry",
            });
        }
    }
    // 5b (a). The chain preset_root (exclusive) → preset_dir (inclusive) is real directories (PD-24).
    let preset_rel = dest.preset_rel();
    let preset_root_kind = io.kind_of(&dest.preset_root).map_err(&read)?;
    let mut chain = Vec::new();
    {
        let mut rel = String::new();
        for segment in preset_rel.split('/') {
            if !rel.is_empty() {
                rel.push('/');
            }
            rel.push_str(segment);
            chain.push(rel.clone());
        }
    }
    let mut chain_missing = Vec::new();
    for rel in &chain {
        let path = dest_path(&dest.preset_root, rel)?;
        if !chain_missing.is_empty() {
            chain_missing.push((rel.clone(), path));
            continue;
        }
        match io.kind_of(&path).map_err(&read)? {
            None => chain_missing.push((rel.clone(), path)),
            Some(NodeKind::Dir) => {}
            Some(_) => return Err(collision(RootTag::Presets, rel)),
        }
    }
    let previously = |root: RootTag, path: &str, tag: EntryTag| {
        previous.and_then(|m| {
            m.files
                .iter()
                .find(|e| e.root == root && e.path == path && e.kind == tag)
        })
    };
    let hashes: std::collections::BTreeMap<&str, [u8; 32]> = tree
        .file_hashes()
        .iter()
        .map(|(p, h)| (p.as_str(), *h))
        .collect();
    let staged_root = tree.root();
    let mut files = Vec::new();
    let mut bundle = Vec::new();
    let mut placed_files = 0u64;
    // 6. Bundle lines into `.krate-new`.
    let new_name = format!("{bundle_name}{NEW_SUFFIX}");
    for l in &lines {
        let inner = if l.path == bundle_name {
            None
        } else if let Some(rest) = l.path.strip_prefix(&format!("{bundle_name}/")) {
            Some(rest)
        } else {
            continue;
        };
        let dst = match inner {
            None => dest_path(&dest.plugin_root, &new_name)?,
            Some(rest) => dest_path(&dest.plugin_root, &format!("{new_name}/{rest}"))?,
        };
        let mut record = ManifestEntry {
            root: RootTag::Plugin,
            kind: EntryTag::D,
            path: l.path.to_owned(),
            sha256: None,
            target: None,
        };
        match l.kind {
            'd' => bundle.push(BundleOp::Dir { dst }),
            'f' => {
                let sha256 = *hashes.get(l.path).ok_or(InstallError::Path {
                    reason: "a staged file has no hash",
                })?;
                record.kind = EntryTag::F;
                record.sha256 = Some(hex(&sha256));
                bundle.push(BundleOp::File {
                    src: dest_path(staged_root, l.path)?,
                    dst,
                    rel: l.path.to_owned(),
                    sha256,
                    mode: if l.mode & 0o111 != 0 { 0o755 } else { 0o644 },
                });
                placed_files += 1;
            }
            _ => {
                let target = l.target.unwrap_or_default().to_owned();
                record.kind = EntryTag::L;
                record.target = Some(target.clone());
                bundle.push(BundleOp::Link { dst, target });
            }
        }
        files.push(record);
    }
    // 8. (planned before 7 so the preset entries list the chain first) The FR-011 chain.
    let create_preset_root = preset_root_kind.is_none();
    let mut create_dirs = Vec::new();
    let mut preset_files = Vec::new();
    for rel in &chain {
        let missing = chain_missing.iter().any(|(r, _)| r == rel);
        if missing || previously(RootTag::Presets, rel, EntryTag::D).is_some() {
            preset_files.push(ManifestEntry {
                root: RootTag::Presets,
                kind: EntryTag::D,
                path: rel.clone(),
                sha256: None,
                target: None,
            });
        }
    }
    create_dirs.extend(chain_missing.iter().map(|(_, p)| p.clone()));
    let mut created: BTreeSet<PathBuf> = create_dirs.iter().cloned().collect();
    // 7. Preset lines, D5 (PD-18, PD-26).
    let mut siblings = Vec::new();
    let mut kept_paths = Vec::new();
    for l in &lines {
        let Some(rest) = l.path.strip_prefix("presets/") else {
            continue;
        };
        let rel = format!("{preset_rel}/{rest}");
        let path = dest_path(&dest.preset_root, &rel)?;
        let parent_new = path.parent().is_some_and(|p| created.contains(p));
        let current = if parent_new {
            None
        } else {
            io.kind_of(&path).map_err(&read)?
        };
        match l.kind {
            'd' => match current {
                None => {
                    created.insert(path.clone());
                    create_dirs.push(path);
                    preset_files.push(ManifestEntry {
                        root: RootTag::Presets,
                        kind: EntryTag::D,
                        path: rel,
                        sha256: None,
                        target: None,
                    });
                }
                Some(NodeKind::Dir) => {
                    if previously(RootTag::Presets, &rel, EntryTag::D).is_some() {
                        preset_files.push(ManifestEntry {
                            root: RootTag::Presets,
                            kind: EntryTag::D,
                            path: rel,
                            sha256: None,
                            target: None,
                        });
                    }
                }
                Some(_) => return Err(collision(RootTag::Presets, &rel)),
            },
            'f' => {
                let staged = *hashes.get(l.path).ok_or(InstallError::Path {
                    reason: "a staged file has no hash",
                })?;
                let new = dest_path(&dest.preset_root, &format!("{rel}{NEW_SUFFIX}"))?;
                if !parent_new && io.kind_of(&new).map_err(&read)?.is_some() {
                    return Err(collision(RootTag::Presets, &format!("{rel}{NEW_SUFFIX}")));
                }
                let record = ManifestEntry {
                    root: RootTag::Presets,
                    kind: EntryTag::F,
                    path: rel.clone(),
                    sha256: Some(hex(&staged)),
                    target: None,
                };
                let mode = if l.mode & 0o111 != 0 { 0o755 } else { 0o644 };
                let place = |pre| SiblingOp {
                    src: dest_path(staged_root, l.path).ok(),
                    link: None,
                    dest: path.clone(),
                    new: new.clone(),
                    rel: rel.clone(),
                    sha256: Some(staged),
                    mode,
                    pre,
                };
                match current {
                    None => {
                        siblings.push(place(SiblingPre::Absent));
                        preset_files.push(record);
                        placed_files += 1;
                    }
                    Some(NodeKind::File) => {
                        let now = io.hash_file(&path).map_err(&read)?;
                        let recorded = previously(RootTag::Presets, &rel, EntryTag::F)
                            .and_then(|e| e.sha256.clone());
                        if now == staged {
                            preset_files.push(record);
                        } else if recorded.as_deref() == Some(hex(&now).as_str()) {
                            siblings.push(place(SiblingPre::Sha256(hex(&now))));
                            preset_files.push(record);
                            placed_files += 1;
                        } else {
                            kept_paths.push(kept(RootTag::Presets, &rel));
                        }
                    }
                    Some(_) => kept_paths.push(kept(RootTag::Presets, &rel)),
                }
            }
            _ => {
                let target = l.target.unwrap_or_default().to_owned();
                let new = dest_path(&dest.preset_root, &format!("{rel}{NEW_SUFFIX}"))?;
                if !parent_new && io.kind_of(&new).map_err(&read)?.is_some() {
                    return Err(collision(RootTag::Presets, &format!("{rel}{NEW_SUFFIX}")));
                }
                let record = ManifestEntry {
                    root: RootTag::Presets,
                    kind: EntryTag::L,
                    path: rel.clone(),
                    sha256: None,
                    target: Some(target.clone()),
                };
                match current {
                    None => {
                        siblings.push(SiblingOp {
                            src: None,
                            link: Some(target),
                            dest: path,
                            new,
                            rel,
                            sha256: None,
                            mode: 0o777,
                            pre: SiblingPre::Absent,
                        });
                        preset_files.push(record);
                    }
                    Some(NodeKind::Symlink)
                        if io.read_link(&path).map_err(&read)? == Path::new(&target) =>
                    {
                        preset_files.push(record);
                    }
                    Some(_) => kept_paths.push(kept(RootTag::Presets, &rel)),
                }
            }
        }
    }
    files.extend(preset_files);
    // 5b (b). Every planned placement's deepest existing ancestor lies inside its root (PD-24).
    let plugin_canon = canonical_root(io, &dest.plugin_root).map_err(&read)?;
    let preset_canon = canonical_root(io, &dest.preset_root).map_err(&read)?;
    if !inside(io, plugin_canon.as_deref(), &dest.plugin_root).map_err(&read)? {
        return Err(InstallError::OutsideRoot {
            path: kept(RootTag::Plugin, bundle_name),
        });
    }
    let mut seen = BTreeSet::new();
    let preset_places = siblings
        .iter()
        .map(|s| (s.new.clone(), s.rel.clone()))
        .chain(
            create_dirs
                .iter()
                .filter_map(|d| rel_of(&dest.preset_root, d).map(|r| (d.clone(), r))),
        );
    for (path, rel) in preset_places {
        let parent = path.parent().map(Path::to_path_buf).unwrap_or_default();
        if seen.insert(parent.clone())
            && !inside(io, preset_canon.as_deref(), &parent).map_err(&read)?
        {
            return Err(InstallError::OutsideRoot {
                path: kept(RootTag::Presets, &rel),
            });
        }
    }
    // 10. The pending manifest, bounded (PD-21).
    let pending = Manifest {
        manifest_version: 1,
        plugin: dest.name.clone(),
        version: entry.version_text.clone(),
        vst3_bundle: dest.vst3_bundle.clone(),
        archive_sha256: hex(tree.archive_sha256()),
        plugin_root: text(&dest.plugin_root),
        preset_root: text(&dest.preset_root),
        files,
    };
    if json_bytes(&pending, "the manifest does not serialise")?.len() as u64
        > detect::MAX_MANIFEST_BYTES
    {
        return Err(InstallError::Manifest {
            reason: "too large",
        });
    }
    // 11. The stale files and directories of the previous manifest, decided (FR-020, D5).
    let (stale, stale_dirs) = stale_ops(io, dest, previous, &pending, Step::JournalBegun)?;
    for op in &stale {
        if let StaleOp::Kept { rel } = op {
            let k = kept(RootTag::Presets, rel);
            if !kept_paths.contains(&k) {
                kept_paths.push(k);
            }
        }
    }
    Ok(InstallPlan {
        had_old,
        create_plugin_root,
        create_preset_root,
        bundle,
        siblings,
        create_dirs,
        stale,
        stale_dirs,
        pending,
        kept: kept_paths,
        placed_files,
    })
}

// ---- executing an install or update (plan §4.1.8) ------------------------------------------------------------------

/// FR-022 to FR-027: format, stage, plan, the twelve steps, roll back or forward; the tree is always discarded.
fn run_install(
    ctx: &AppContext,
    io: &dyn InstallIo,
    extract: ExtractEnv<'_>,
    progress: &mut dyn FnMut(InstallProgress),
    entry: &PluginEntry,
    artifact: &VerifiedArtifact,
    kind: ActionKind,
) -> Result<Outcome, InstallError> {
    if artifact.format() != AssetFormat::TarGz {
        return Err(InstallError::Format);
    }
    let dest = Destinations::of(ctx, entry)?;
    progress(InstallProgress::Staging);
    let tree = extract::stage(extract, entry, artifact).map_err(InstallError::Extract)?;
    let result = read_manifest_full(io, ctx.roots.state_dir(), &dest.stem, &dest.name).and_then(
        |previous| {
            let plan = plan(io, ctx, &dest, &tree, entry, kind, previous.as_ref())?;
            execute(io, &dest, plan, previous, kind, progress)
        },
    );
    if let Err(e) = extract::discard(tree, extract.io) {
        warn!(target: LOG_TARGET, "{} {}: the staged tree was not released", e.code(), dest.name);
    }
    result
}

/// Steps 1 to 12 of FR-024 over a plan.
fn execute(
    io: &dyn InstallIo,
    dest: &Destinations,
    plan: InstallPlan,
    previous: Option<Manifest>,
    kind: ActionKind,
    progress: &mut dyn FnMut(InstallProgress),
) -> Result<Outcome, InstallError> {
    let mut journal = Journal::empty(
        dest,
        if kind == ActionKind::Install {
            JournalOp::Install
        } else {
            JournalOp::Update
        },
    );
    journal.had_old = plan.had_old;
    journal.siblings = plan
        .siblings
        .iter()
        .map(|s| Sibling {
            dest: text(&s.dest),
            new: text(&s.new),
            pre: s.pre.clone(),
        })
        .collect();
    journal.created_dirs = plan.create_dirs.iter().map(|d| text(d)).collect();
    journal.pending_manifest = Some(plan.pending.clone());
    journal.previous_manifest = previous;
    let mut old_aside = false;
    if let Err(first) = stage_and_commit(io, dest, &plan, &mut journal, &mut old_aside, progress) {
        if let Err(back) = roll_back(io, dest, &journal, old_aside) {
            warn!(target: LOG_TARGET, "{} {}: roll-back stopped, the journal stays", back.code(), dest.name);
        }
        return Err(first);
    }
    // From the commit point on: roll forward (FR-027).
    let mut kept_paths = plan.kept.clone();
    let stale = (plan.stale.as_slice(), plan.stale_dirs.as_slice());
    let removed = match finish(io, dest, &journal, stale, &mut kept_paths) {
        Ok(removed) => removed,
        Err(first) => {
            let mut again = kept_paths.clone();
            match finish(io, dest, &journal, stale, &mut again) {
                Ok(removed) => {
                    warn!(target: LOG_TARGET, "{} {}: completed on the second pass", first.code(), dest.name);
                    kept_paths = again;
                    removed
                }
                Err(_) => return Err(first),
            }
        }
    };
    if let Err(e) = close(io, dest, &journal) {
        warn!(target: LOG_TARGET, "{} {}: the cleanup waits for the next start", e.code(), dest.name);
    }
    progress(InstallProgress::Done);
    Ok(Outcome {
        version: Some(plan.pending.version.clone()),
        placed: plan.placed_files,
        removed,
        kept: kept_paths,
    })
}

/// Steps 1 to 6: the journal, the copies and syncs, `staged`, the swap up to the commit point.
fn stage_and_commit(
    io: &dyn InstallIo,
    dest: &Destinations,
    plan: &InstallPlan,
    journal: &mut Journal,
    old_aside: &mut bool,
    progress: &mut dyn FnMut(InstallProgress),
) -> Result<(), InstallError> {
    // 1. journal_begun.
    write_journal(io, dest, journal, Step::JournalBegun)?;
    let total = plan.placed_files;
    let mut done = 0u64;
    // 2. bundle_staged.
    let bundle = at(Step::BundleStaged);
    if plan.create_plugin_root {
        io.create_dir_all(&dest.plugin_root).map_err(&bundle)?;
    }
    let mut made = Vec::new();
    for op in &plan.bundle {
        match op {
            BundleOp::Dir { dst } => {
                io.create_dir(dst).map_err(&bundle)?;
                io.set_mode(dst, 0o755).map_err(&bundle)?;
                made.push(dst.clone());
            }
            BundleOp::File {
                src,
                dst,
                rel,
                sha256,
                mode,
            } => {
                copy_checked(
                    io,
                    src,
                    dst,
                    sha256,
                    RootTag::Plugin,
                    rel,
                    Step::BundleStaged,
                )?;
                io.set_mode(dst, *mode).map_err(&bundle)?;
                io.sync_file(dst).map_err(&bundle)?;
                done += 1;
                progress(InstallProgress::Placing { done, total });
            }
            BundleOp::Link { dst, target } => io.symlink(target, dst).map_err(&bundle)?,
        }
    }
    if cfg!(unix) {
        for dir in made.iter().rev() {
            io.sync_dir(dir).map_err(&bundle)?;
        }
    }
    // 3. presets_staged.
    let presets = at(Step::PresetsStaged);
    if plan.create_preset_root {
        io.create_dir_all(&dest.preset_root).map_err(&presets)?;
    }
    for dir in &plan.create_dirs {
        io.create_dir(dir).map_err(&presets)?;
        io.set_mode(dir, 0o755).map_err(&presets)?;
    }
    let mut parents = BTreeSet::new();
    for s in &plan.siblings {
        match (&s.src, &s.link, &s.sha256) {
            (Some(src), _, Some(sha256)) => {
                copy_checked(
                    io,
                    src,
                    &s.new,
                    sha256,
                    RootTag::Presets,
                    &s.rel,
                    Step::PresetsStaged,
                )?;
                io.set_mode(&s.new, s.mode).map_err(&presets)?;
                io.sync_file(&s.new).map_err(&presets)?;
                done += 1;
                progress(InstallProgress::Placing { done, total });
            }
            (_, Some(target), _) => io.symlink(target, &s.new).map_err(&presets)?,
            _ => {
                return Err(InstallError::Path {
                    reason: "a preset sibling has no source",
                });
            }
        }
        if let Some(parent) = s.new.parent() {
            parents.insert(parent.to_path_buf());
        }
    }
    if cfg!(unix) {
        for dir in plan.create_dirs.iter().rev() {
            io.sync_dir(dir).map_err(&presets)?;
        }
        for parent in &parents {
            if !plan.create_dirs.contains(parent) {
                io.sync_dir(parent).map_err(&presets)?;
            }
        }
    }
    // 4. journal_staged.
    journal.step = JournalStep::Staged;
    write_journal(io, dest, journal, Step::JournalStaged)?;
    // 5. old_aside.
    if plan.had_old {
        io.rename(&dest.target, &dest.old)
            .map_err(at(Step::OldAside))?;
        *old_aside = true;
    }
    // 6. new_in: the commit point (D7).
    progress(InstallProgress::Committing);
    io.rename(&dest.new, &dest.target)
        .map_err(at(Step::NewIn))?;
    if cfg!(unix)
        && let Err(e) = io.sync_dir(&dest.plugin_root)
    {
        // Committed: the sync failure is reported but the swap stands (FR-027, steps from new_in on).
        warn!(target: LOG_TARGET, "{} {}: the plug-in root was not synced", io_err(Step::NewIn, &e).code(), dest.name);
    }
    Ok(())
}

/// `copy_new` with the bytes hashed while they pass (FR-022): a difference from the staged hash is `Changed`.
fn copy_checked(
    io: &dyn InstallIo,
    src: &Path,
    dst: &Path,
    sha256: &[u8; 32],
    root: RootTag,
    rel: &str,
    step: Step,
) -> Result<(), InstallError> {
    let mut sha = Sha256Stream::new();
    io.copy_new(src, dst, &mut |chunk| sha.update(chunk))
        .map_err(at(step))?;
    if sha.finish() != *sha256 {
        return Err(InstallError::Changed {
            path: kept(root, rel),
        });
    }
    Ok(())
}

/// FR-027 before the commit point (and FR-071's roll-back rows): the old bundle back first when it was moved aside,
/// then `.krate-new`, the siblings, the created directories (empty only), the journal temp and the journal. The first
/// failing step stops it.
fn roll_back(
    io: &dyn InstallIo,
    dest: &Destinations,
    j: &Journal,
    rename_back: bool,
) -> Result<(), InstallError> {
    if rename_back {
        io.rename(&dest.old, &dest.target)
            .map_err(at(Step::OldAside))?;
    }
    let staged = at(Step::BundleStaged);
    if io.kind_of(&dest.new).map_err(&staged)?.is_some() {
        io.remove_tree(&dest.new).map_err(&staged)?;
    }
    let presets = at(Step::PresetsStaged);
    for s in j.siblings.iter().rev() {
        let new = PathBuf::from(&s.new);
        if io.kind_of(&new).map_err(&presets)?.is_some() {
            io.remove_file(&new).map_err(&presets)?;
        }
    }
    for d in j.created_dirs.iter().rev() {
        let dir = PathBuf::from(d);
        if is_real_dir(io.kind_of(&dir).map_err(&presets)?)
            && io.read_dir(&dir).map_err(&presets)?.is_empty()
        {
            io.remove_dir(&dir).map_err(&presets)?;
        }
    }
    remove_journal(io, dest)
}

/// The journal temp (if any) and the journal file.
fn remove_journal(io: &dyn InstallIo, dest: &Destinations) -> Result<(), InstallError> {
    let err = at(Step::JournalRemoved);
    let temp = dest.journal_dir.join(format!(".{}.json.tmp", dest.stem));
    for path in [&temp, &dest.journal] {
        if io.kind_of(path).map_err(&err)?.is_some() {
            io.remove_file(path).map_err(&err)?;
        }
    }
    Ok(())
}

/// PD-26: `dest` still holds what the sibling was planned against.
fn pre_matches(io: &dyn InstallIo, dest: &Path, pre: &SiblingPre) -> io::Result<bool> {
    Ok(match pre {
        SiblingPre::Absent => io.kind_of(dest)?.is_none(),
        SiblingPre::Sha256(h) => {
            io.kind_of(dest)? == Some(NodeKind::File) && hex(&io.hash_file(dest)?) == *h
        }
    })
}

/// A node equals its manifest entry: a regular file of the hash, a link of the target, or a real directory.
fn matches_entry(io: &dyn InstallIo, path: &Path, e: &ManifestEntry) -> io::Result<bool> {
    let kind = io.kind_of(path)?;
    Ok(match e.kind {
        EntryTag::F => {
            kind == Some(NodeKind::File)
                && e.sha256.as_deref() == Some(hex(&io.hash_file(path)?).as_str())
        }
        EntryTag::L => {
            kind == Some(NodeKind::Symlink)
                && e.target
                    .as_deref()
                    .is_some_and(|t| io.read_link(path).is_ok_and(|l| l == Path::new(t)))
        }
        EntryTag::D => kind == Some(NodeKind::Dir),
    })
}

/// FR-024 steps 7 to 9 from the journal (the in-process path and FR-071's roll-forward, one function): siblings
/// renamed in over their planned pre-state (PD-26), the manifest written, the stale decisions (the plan's, or
/// `journal_stale`'s in recovery) carried out. Returns the count of stale files removed.
fn finish(
    io: &dyn InstallIo,
    dest: &Destinations,
    j: &Journal,
    (stale, stale_dirs): (&[StaleOp], &[PathBuf]),
    kept_paths: &mut Vec<KeptPath>,
) -> Result<u64, InstallError> {
    let Some(planned) = &j.pending_manifest else {
        return Err(InstallError::Journal {
            reason: "the journal holds no pending manifest",
        });
    };
    let mut pending = planned.clone();
    let step = at(Step::PresetsIn);
    let mut parents = BTreeSet::new();
    for s in &j.siblings {
        let target = PathBuf::from(&s.dest);
        let new = PathBuf::from(&s.new);
        let rel = rel_of(&dest.preset_root, &target).unwrap_or_default();
        let entry = pending
            .files
            .iter()
            .find(|e| e.root == RootTag::Presets && e.path == rel)
            .cloned();
        let keep = if io.kind_of(&new).map_err(&step)?.is_some() {
            if pre_matches(io, &target, &s.pre).map_err(&step)? {
                io.rename(&new, &target).map_err(&step)?;
                if let Some(parent) = target.parent() {
                    parents.insert(parent.to_path_buf());
                }
                false
            } else {
                io.remove_file(&new).map_err(&step)?;
                let k = kept(RootTag::Presets, &rel);
                warn!(target: LOG_TARGET, "{}: kept {}: changed since it was planned", dest.name, shown(&k));
                if !kept_paths.contains(&k) {
                    kept_paths.push(k);
                }
                true
            }
        } else {
            // Already renamed in, or gone: recorded only when it is what was placed.
            match &entry {
                Some(e) => !matches_entry(io, &target, e).map_err(&step)?,
                None => false,
            }
        };
        if keep {
            pending
                .files
                .retain(|e| !(e.root == RootTag::Presets && e.path == rel));
        }
    }
    if cfg!(unix) {
        for parent in &parents {
            io.sync_dir(parent).map_err(&step)?;
        }
    }
    let bytes = json_bytes(&pending, "the manifest does not serialise")?;
    write_atomic(
        io,
        &dest.manifests_dir,
        &format!("{}.json", dest.stem),
        &bytes,
        Step::ManifestWritten,
    )?;
    remove_stale(io, stale, stale_dirs, kept_paths)
}

/// Recovery's stale decisions (FR-071 roll-forward): `stale_ops` over the journal's previous and pending manifests.
fn journal_stale(
    io: &dyn InstallIo,
    dest: &Destinations,
    j: &Journal,
) -> Result<(Vec<StaleOp>, Vec<PathBuf>), InstallError> {
    let Some(planned) = &j.pending_manifest else {
        return Err(InstallError::Journal {
            reason: "the journal holds no pending manifest",
        });
    };
    stale_ops(
        io,
        dest,
        j.previous_manifest.as_ref(),
        planned,
        Step::StaleRemoved,
    )
}

/// The planner's stale decisions (FR-020, D5), read-only: every previous preset entry the new layout lacks, deepest
/// first; a present file or link is `Remove` when it is what was recorded, else `Kept`; a missing one is already
/// removed; the directories go to the second list, deepest first. Recovery recomputes them from the journal.
fn stale_ops(
    io: &dyn InstallIo,
    dest: &Destinations,
    previous: Option<&Manifest>,
    planned: &Manifest,
    on: Step,
) -> Result<(Vec<StaleOp>, Vec<PathBuf>), InstallError> {
    let Some(previous) = previous else {
        return Ok((Vec::new(), Vec::new()));
    };
    let step = at(on);
    let current: BTreeSet<&str> = planned
        .files
        .iter()
        .filter(|e| e.root == RootTag::Presets)
        .map(|e| e.path.as_str())
        .collect();
    let mut stale: Vec<&ManifestEntry> = previous
        .files
        .iter()
        .filter(|e| e.root == RootTag::Presets && !current.contains(e.path.as_str()))
        .collect();
    stale.sort_by(|a, b| {
        depth(&b.path)
            .cmp(&depth(&a.path))
            .then(b.path.cmp(&a.path))
    });
    let mut ops = Vec::new();
    let mut dirs = Vec::new();
    for e in stale {
        let path = dest_path(&dest.preset_root, &e.path)?;
        if e.kind == EntryTag::D {
            dirs.push(path);
            continue;
        }
        if io.kind_of(&path).map_err(&step)?.is_none() {
            continue;
        }
        if matches_entry(io, &path, e).map_err(&step)? {
            ops.push(StaleOp::Remove {
                path,
                entry: e.clone(),
            });
        } else {
            ops.push(StaleOp::Kept {
                rel: e.path.clone(),
            });
        }
    }
    Ok((ops, dirs))
}

/// Step 9 over the planner's decisions (FR-024): a `Remove` file or link only when it is still what was recorded
/// (else kept and reported), then each stale directory, deepest first, only when it is an empty real directory.
fn remove_stale(
    io: &dyn InstallIo,
    stale: &[StaleOp],
    stale_dirs: &[PathBuf],
    kept_paths: &mut Vec<KeptPath>,
) -> Result<u64, InstallError> {
    let step = at(Step::StaleRemoved);
    let mut keep = |rel: &str| {
        let k = kept(RootTag::Presets, rel);
        if !kept_paths.contains(&k) {
            kept_paths.push(k);
        }
    };
    let mut removed = 0;
    for op in stale {
        match op {
            StaleOp::Remove { path, entry } => {
                if io.kind_of(path).map_err(&step)?.is_none() {
                    continue;
                }
                if matches_entry(io, path, entry).map_err(&step)? {
                    io.remove_file(path).map_err(&step)?;
                    removed += 1;
                } else {
                    keep(&entry.path);
                }
            }
            StaleOp::Kept { rel } => keep(rel),
        }
    }
    for dir in stale_dirs {
        let kind = io.kind_of(dir).map_err(&step)?;
        if is_real_dir(kind) && io.read_dir(dir).map_err(&step)?.is_empty() {
            remove_empty_dir(io, dir).map_err(&step)?;
        }
    }
    Ok(removed)
}

/// `remove_dir`, where "not empty" (`DirectoryNotEmpty` / `AlreadyExists`) leaves it and is not an error.
fn remove_empty_dir(io: &dyn InstallIo, path: &Path) -> io::Result<()> {
    match io.remove_dir(path) {
        Err(e)
            if matches!(
                e.kind(),
                io::ErrorKind::DirectoryNotEmpty | io::ErrorKind::AlreadyExists
            ) =>
        {
            Ok(())
        }
        other => other,
    }
}

/// Steps 10 to 12: the journal `done`, the old bundle removed, the journal removed.
fn close(io: &dyn InstallIo, dest: &Destinations, j: &Journal) -> Result<(), InstallError> {
    let mut done = j.clone();
    done.step = JournalStep::Done;
    write_journal(io, dest, &done, Step::JournalDone)?;
    let old = at(Step::OldDeleted);
    if io.kind_of(&dest.old).map_err(&old)?.is_some() {
        io.remove_tree(&dest.old).map_err(&old)?;
    }
    let err = at(Step::JournalRemoved);
    io.remove_file(&dest.journal).map_err(&err)
}

// ---- recovery (plan §4.1.9) ----------------------------------------------------------------------------------------

/// What recovery did with one journal (FR-071).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Decision {
    RolledBack,
    Completed,
    Refused,
}

/// One journal's recovery: its plugin, the decision and the code of a refusal or failure (FR-074).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Recovered {
    pub plugin: String,
    pub decision: Decision,
    pub code: Option<&'static str>,
}

/// What one start-up recovery pass did (FR-070).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RecoveryReport {
    pub journals: Vec<Recovered>,
    pub temp_removed: usize,
    pub extract_pruned: usize,
}

/// The start-up recovery pass over every journal; never fails, never panics on input (FR-070 to FR-075, D14).
/// Creates nothing.
pub fn recover(ctx: &AppContext, io: &dyn InstallIo, extract: ExtractEnv<'_>) -> RecoveryReport {
    let state = ctx.roots.state_dir();
    let journal_dir = state.join(JOURNAL_DIR);
    let manifests_dir = state.join(detect::MANIFESTS_DIR);
    ctx.roots.check_sandboxed(&journal_dir);
    ctx.roots.check_sandboxed(&manifests_dir);
    let mut report = RecoveryReport::default();
    match io.kind_of(&journal_dir) {
        Ok(Some(NodeKind::Dir)) => {
            let names = io.read_dir(&journal_dir).unwrap_or_default();
            for (name, kind) in names {
                let Some(stem) = name
                    .to_str()
                    .filter(|n| !n.starts_with('.'))
                    .and_then(|n| n.strip_suffix(".json"))
                else {
                    continue;
                };
                if kind != NodeKind::File {
                    continue;
                }
                let path = journal_dir.join(format!("{stem}.json"));
                report.journals.push(recover_one(ctx, io, &path, stem));
            }
        }
        Ok(None) => {}
        Ok(Some(_)) | Err(_) => {
            warn!(target: LOG_TARGET, "E_INSTALL_JOURNAL: the journal folder is not a directory; nothing recovered");
        }
    }
    for dir in [&manifests_dir, &journal_dir] {
        if !is_real_dir(io.kind_of(dir).ok().flatten()) {
            continue;
        }
        for (name, kind) in io.read_dir(dir).unwrap_or_default() {
            let leftover = name
                .to_str()
                .is_some_and(|n| n.starts_with('.') && n.ends_with(".json.tmp"));
            if leftover && kind == NodeKind::File && io.remove_file(&dir.join(&name)).is_ok() {
                report.temp_removed += 1;
            }
        }
    }
    report.extract_pruned = extract::prune(extract);
    report
}

/// FR-071 to FR-074 for one journal file.
fn recover_one(ctx: &AppContext, io: &dyn InstallIo, path: &Path, stem: &str) -> Recovered {
    let refused = |plugin: &str, code: &'static str| Recovered {
        plugin: plugin.to_owned(),
        decision: Decision::Refused,
        code: Some(code),
    };
    let Some(j) = io
        .read_file(path, MAX_JOURNAL_BYTES)
        .ok()
        .filter(|b| b.len() as u64 <= MAX_JOURNAL_BYTES)
        .and_then(|b| serde_json::from_slice::<Journal>(&b).ok())
        .filter(|j| j.journal_version == 1)
    else {
        return refused(stem, "E_INSTALL_JOURNAL");
    };
    let Ok(dest) =
        Destinations::from_parts(ctx, &j.plugin, &j.vst3_bundle, j.preset_subdir.as_deref())
    else {
        return refused(&j.plugin, "E_INSTALL_JOURNAL");
    };
    if let Err(reason) = check_journal(io, &j, stem, &dest) {
        warn!(target: LOG_TARGET, "{}: journal check: {reason}", dest.name);
        return refused(&j.plugin, "E_INSTALL_JOURNAL");
    }
    let done = |decision| Recovered {
        plugin: j.plugin.clone(),
        decision,
        code: None,
    };
    let result = if j.op == JournalOp::Remove {
        resume_remove(ctx, io, &dest).map(|()| Decision::Completed)
    } else {
        match j.step {
            JournalStep::Begun => roll_back(io, &dest, &j, false).map(|()| Decision::RolledBack),
            JournalStep::Done => close_done(io, &dest).map(|()| Decision::Completed),
            JournalStep::Staged => {
                let present = |p: &Path| io.kind_of(p).ok().flatten().is_some();
                let (new, target, old) = (
                    present(&dest.new),
                    present(&dest.target),
                    present(&dest.old),
                );
                match (new, target, j.had_old, old) {
                    (true, true, _, _) | (true, false, false, _) => {
                        roll_back(io, &dest, &j, false).map(|()| Decision::RolledBack)
                    }
                    (true, false, true, true) | (false, false, _, true) => {
                        roll_back(io, &dest, &j, true).map(|()| Decision::RolledBack)
                    }
                    (false, true, false, _) | (false, true, true, true) => {
                        let mut kept_paths = Vec::new();
                        journal_stale(io, &dest, &j)
                            .and_then(|(ops, dirs)| {
                                finish(io, &dest, &j, (&ops, &dirs), &mut kept_paths)
                            })
                            .and_then(|_| close(io, &dest, &j))
                            .map(|()| Decision::Completed)
                    }
                    _ => return refused(&j.plugin, "E_INSTALL_JOURNAL"),
                }
            }
        }
    };
    match result {
        Ok(decision) => done(decision),
        Err(e) => refused(&j.plugin, e.code()),
    }
}

/// FR-071 step `done`: the old bundle if present, then the journal.
fn close_done(io: &dyn InstallIo, dest: &Destinations) -> Result<(), InstallError> {
    let old = at(Step::OldDeleted);
    if io.kind_of(&dest.old).map_err(&old)?.is_some() {
        io.remove_tree(&dest.old).map_err(&old)?;
    }
    remove_journal(io, dest)
}

/// FR-072: Remove re-run from the manifest on disk; no manifest → the journal is removed.
fn resume_remove(
    ctx: &AppContext,
    io: &dyn InstallIo,
    dest: &Destinations,
) -> Result<(), InstallError> {
    match read_manifest_full(io, ctx.roots.state_dir(), &dest.stem, &dest.name)? {
        None => remove_journal(io, dest),
        Some(m) => {
            roots_match(ctx, io, dest, &m)?;
            resolve_entries(io, dest, &m)?;
            let mut kept_paths = Vec::new();
            remove_entries(io, dest, &m, &mut |_| {}, &mut kept_paths).map(|_| ())
        }
    }
}

/// One log line per recovered journal (FR-074, FR-117); a refusal names `E_INSTALL_JOURNAL` once.
pub(crate) fn log_recovery(report: &RecoveryReport) {
    for r in &report.journals {
        match r.decision {
            Decision::Refused => info!(
                target: LOG_TARGET,
                "E_INSTALL_JOURNAL {}: recovery refused, the journal stays ({})",
                r.plugin,
                r.code.unwrap_or("E_INSTALL_JOURNAL")
            ),
            Decision::RolledBack => info!(target: LOG_TARGET, "{}: recovery rolled_back", r.plugin),
            Decision::Completed => info!(target: LOG_TARGET, "{}: recovery completed", r.plugin),
        }
    }
    if report.temp_removed > 0 || report.extract_pruned > 0 {
        info!(
            target: LOG_TARGET,
            "recovery: {} temp files removed, {} staged trees pruned",
            report.temp_removed,
            report.extract_pruned
        );
    }
}

// ---- uninstall (plan §4.1.10) --------------------------------------------------------------------------------------

/// FR-060 to FR-065, FR-122: the manifest, its roots and entries, the journal, the bundle whole, the preset entries
/// deepest first, the manifest, the journal.
fn run_uninstall(
    ctx: &AppContext,
    io: &dyn InstallIo,
    progress: &mut dyn FnMut(InstallProgress),
    entry: &PluginEntry,
) -> Result<Outcome, InstallError> {
    let dest = Destinations::of(ctx, entry)?;
    check_state_dirs(io, &dest)?;
    if io
        .kind_of(&dest.journal)
        .map_err(at(Step::RemoveBegun))?
        .is_some()
    {
        return Err(InstallError::JournalPending {
            name: dest.name.clone(),
        });
    }
    let Some(m) = read_manifest_full(io, ctx.roots.state_dir(), &dest.stem, &dest.name)? else {
        return Err(InstallError::NotManaged {
            name: dest.name.clone(),
        });
    };
    roots_match(ctx, io, &dest, &m)?;
    resolve_entries(io, &dest, &m)?;
    let mut journal = Journal::empty(&dest, JournalOp::Remove);
    journal.previous_manifest = Some(m.clone());
    let total = 1 + m
        .files
        .iter()
        .filter(|e| e.root == RootTag::Presets)
        .count() as u64;
    progress(InstallProgress::Removing { done: 0, total });
    write_journal(io, &dest, &journal, Step::RemoveBegun)?;
    let mut kept_paths = Vec::new();
    let removed = remove_entries(io, &dest, &m, progress, &mut kept_paths)?;
    progress(InstallProgress::Done);
    Ok(Outcome {
        version: Some(m.version.clone()),
        placed: 0,
        removed,
        kept: kept_paths,
    })
}

/// FR-061 from `remove_entries` on, idempotent (FR-065): returns the count of nodes removed.
fn remove_entries(
    io: &dyn InstallIo,
    dest: &Destinations,
    m: &Manifest,
    progress: &mut dyn FnMut(InstallProgress),
    kept_paths: &mut Vec<KeptPath>,
) -> Result<u64, InstallError> {
    let step = at(Step::RemoveEntries);
    let mut presets: Vec<&ManifestEntry> = m
        .files
        .iter()
        .filter(|e| e.root == RootTag::Presets)
        .collect();
    presets.sort_by(|a, b| {
        depth(&b.path)
            .cmp(&depth(&a.path))
            .then(b.path.cmp(&a.path))
    });
    let total = 1 + presets.len() as u64;
    let mut removed = 0;
    if io.kind_of(&dest.target).map_err(&step)?.is_some() {
        io.remove_tree(&dest.target).map_err(&step)?;
        removed += 1;
    }
    progress(InstallProgress::Removing { done: 1, total });
    for (e, done) in presets.into_iter().zip(2u64..) {
        let path = dest_path(&dest.preset_root, &e.path)?;
        let kind = io.kind_of(&path).map_err(&step)?;
        if kind.is_some() {
            let ours = match e.kind {
                EntryTag::D => is_real_dir(kind) && io.read_dir(&path).map_err(&step)?.is_empty(),
                _ => matches_entry(io, &path, e).map_err(&step)?,
            };
            if ours {
                if e.kind == EntryTag::D {
                    io.remove_dir(&path).map_err(&step)?;
                } else {
                    io.remove_file(&path).map_err(&step)?;
                }
                removed += 1;
            } else if e.kind != EntryTag::D || !is_real_dir(kind) {
                // Changed content or another kind is reported (FR-061); a non-empty real directory stays silently
                // (SC-004: `kept` is the edited preset only, FR-062).
                kept_paths.push(kept(RootTag::Presets, &e.path));
            }
        }
        progress(InstallProgress::Removing { done, total });
    }
    let manifest = at(Step::ManifestRemoved);
    if io.kind_of(&dest.manifest).map_err(&manifest)?.is_some() {
        io.remove_file(&dest.manifest).map_err(&manifest)?;
    }
    let journal = at(Step::JournalRemoved);
    io.remove_file(&dest.journal).map_err(&journal)?;
    Ok(removed)
}

// ---- the gate and the action table (plan §4.1.11) -----------------------------------------------------------------

/// Whether a plugin's actions may run, and its manifest when they may (FR-093).
#[derive(Debug)]
pub enum ActionGate {
    Blocked(InstallError),
    Open { manifest: Option<Manifest> },
}

/// The read-only gate of one plugin: destinations, journal, and on Linux the manifest and its roots (FR-093).
pub fn action_gate(
    os: Os,
    ctx: &AppContext,
    io: &dyn InstallIo,
    entry: &PluginEntry,
) -> ActionGate {
    match gate(os, ctx, io, entry) {
        Ok(manifest) => ActionGate::Open { manifest },
        Err(e) => ActionGate::Blocked(e),
    }
}

fn gate(
    os: Os,
    ctx: &AppContext,
    io: &dyn InstallIo,
    entry: &PluginEntry,
) -> Result<Option<Manifest>, InstallError> {
    let dest = Destinations::of(ctx, entry)?;
    let read = at(Step::JournalBegun);
    match io.kind_of(&dest.journal_dir).map_err(&read)? {
        None | Some(NodeKind::Dir) => {}
        Some(_) => return Err(InstallError::Journal { reason: NOT_A_DIR }),
    }
    if io.kind_of(&dest.journal).map_err(&read)?.is_some() {
        return Err(InstallError::JournalPending {
            name: dest.name.clone(),
        });
    }
    if os != Os::Linux {
        return Ok(None);
    }
    let manifest = read_manifest_full(io, ctx.roots.state_dir(), &dest.stem, &dest.name)?;
    if let Some(m) = &manifest {
        roots_match(ctx, io, &dest, m)?;
        resolve_entries(io, &dest, m)?;
    }
    Ok(manifest)
}

/// The actions a card offers (FR-093, FR-100, D13): none without a driver or behind a blocked gate; else `Install`
/// iff available and not installed, `Update` iff available and an update, `Remove` iff a manifest, in that order.
pub fn allowed_actions(
    driver_present: bool,
    gate: &ActionGate,
    availability: &Availability<'_>,
    update: Option<UpdateState>,
) -> Vec<ActionKind> {
    let ActionGate::Open { manifest } = gate else {
        return Vec::new();
    };
    if !driver_present {
        return Vec::new();
    }
    let available = matches!(availability, Availability::Available { .. });
    let mut actions = Vec::new();
    if available && update == Some(UpdateState::NotInstalled) {
        actions.push(ActionKind::Install);
    }
    if available && update == Some(UpdateState::UpdateAvailable) {
        actions.push(ActionKind::Update);
    }
    if manifest.is_some() {
        actions.push(ActionKind::Remove);
    }
    actions
}

/// The `UpdateView` spelling (plan §3).
fn update_word(state: Option<UpdateState>) -> &'static str {
    match state {
        Some(UpdateState::NotInstalled) => "not_installed",
        Some(UpdateState::UpToDate) => "up_to_date",
        Some(UpdateState::UpdateAvailable) => "update_available",
        Some(UpdateState::InstalledNewer) => "installed_newer",
        Some(UpdateState::Unknown) | None => "unknown",
    }
}

/// FR-093's (action × state) table: the code of a refused install or update; `None` for an accepted pair.
fn refusal(
    action: ActionKind,
    update: Option<UpdateState>,
    installed: Option<&str>,
    offered: &str,
) -> Option<InstallError> {
    match (action, update) {
        (ActionKind::Install, Some(UpdateState::NotInstalled))
        | (ActionKind::Update, Some(UpdateState::UpdateAvailable))
        | (ActionKind::Remove, _) => None,
        (_, Some(UpdateState::UpToDate | UpdateState::InstalledNewer)) => {
            Some(InstallError::NotNewer {
                installed: installed.unwrap_or("unknown").to_owned(),
                offered: offered.to_owned(),
            })
        }
        (_, state) => Some(InstallError::StateChanged {
            shown: action.as_str(),
            current: update_word(state),
        }),
    }
}

/// The managed copy's version text, for `NotNewer` (FR-093).
fn managed_version(detection: &Detection) -> Option<String> {
    let Detection::Checked { copies, .. } = detection else {
        return None;
    };
    copies
        .iter()
        .find(|c| c.format == CopyFormat::Vst3 && c.managed)
        .and_then(|c| match c.version {
            CopyVersion::Known { version, .. } => Some(version.to_string()),
            CopyVersion::Unknown(_) => None,
        })
}

// ---- the per-plugin registry (plan §4.1.12) ------------------------------------------------------------------------

/// FR-085: one in-process action per `(state_dir, stem)`.
static BUSY: Mutex<BTreeSet<(PathBuf, String)>> = Mutex::new(BTreeSet::new());

/// The registry entry of one running action, released in `Drop` (so on unwind too).
struct Busy {
    key: (PathBuf, String),
}

impl Busy {
    fn enter(state_dir: &Path, stem: &str) -> Option<Busy> {
        let key = (state_dir.to_path_buf(), stem.to_owned());
        let inserted = BUSY
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .insert(key.clone());
        // `then`, not `then_some`: a guard built eagerly would drop at once and release the running action's key.
        inserted.then(|| Busy { key })
    }
}

impl Drop for Busy {
    fn drop(&mut self) {
        BUSY.lock()
            .unwrap_or_else(PoisonError::into_inner)
            .remove(&self.key);
    }
}

// ---- the commands (plan §4.1.13) -----------------------------------------------------------------------------------

/// The `install_plugin` command: re-check, download, verify, then one driver run (FR-090).
pub fn for_command(
    env: CommandEnv<'_>,
    name: &str,
    expected_version: &str,
    action: &str,
    now_unix: u64,
    host: &dyn HostFacts,
    progress: &mut dyn FnMut(InstallProgress),
) -> Result<ActionSummary, InstallError> {
    let ran = Cell::new(false);
    let result = install_command(env, name, expected_version, action, now_unix, host)
        .and_then(|checked| obtain_and_run(env, checked, progress, &ran));
    if !ran.get() {
        log_refusal(name, expected_version, action, &result);
    }
    result
}

/// What steps 1 to 4 of FR-090 resolved: the feed, the request, the entry, the driver, the held guard.
struct Checked<'a> {
    kind: ActionKind,
    feed: feed::Feed,
    request: download::DownloadRequest,
    entry: PluginEntry,
    driver: &'a dyn InstallDriver,
    _busy: Busy,
}

/// FR-090 steps 1 to 4: no asset request, no mutating `InstallIo` call.
fn install_command<'a>(
    env: CommandEnv<'a>,
    name: &str,
    expected_version: &str,
    action: &str,
    now_unix: u64,
    host: &dyn HostFacts,
) -> Result<Checked<'a>, InstallError> {
    // 1. The action word.
    let kind = match action {
        "install" => ActionKind::Install,
        "update" => ActionKind::Update,
        other => {
            return Err(InstallError::Action {
                given: other.to_owned(),
            });
        }
    };
    // 2. One feed check and the request for (os, arch) (PD-1); the install facts; the guard; the destinations.
    let (feed, request) = download::command_request_for(
        env.ctx,
        name,
        expected_version,
        now_unix,
        host,
        env.os,
        env.arch,
    )
    .map_err(|e| InstallError::Download(Box::new(e)))?;
    let Some(entry) = feed.plugins.get(request.name()).cloned() else {
        return Err(InstallError::UnknownPlugin {
            name: name.to_owned(),
        });
    };
    let Some(facts) = entry.release.as_ref().and_then(|r| r.install.as_ref()) else {
        return Err(InstallError::NotManageable {
            name: entry.name.clone(),
        });
    };
    let bundle = facts.vst3_bundle.as_str().to_owned();
    let stem = bundle.strip_suffix(".vst3").unwrap_or(&bundle).to_owned();
    let Some(busy) = Busy::enter(env.ctx.roots.state_dir(), &stem) else {
        return Err(InstallError::Busy {
            name: entry.name.clone(),
        });
    };
    Destinations::of(env.ctx, &entry)?;
    // 3. The driver.
    let driver = env
        .drivers
        .driver_for(env.os)
        .ok_or(InstallError::NoDriver)?;
    // 4. The gate, the detection and the action table (D13).
    let gate = action_gate(env.os, env.ctx, env.io, &entry);
    let listing = Listing::scan(
        env.ctx.roots.plugin_roots(),
        &[bundle.as_str()],
        ListingLimits::PRODUCTION,
    );
    let detection = detect::detect_plugin(env.os, env.ctx, &entry, host, &listing);
    let update = status::update_state(&entry, Some(&detection));
    let running = host.os_version();
    let availability = version::resolve(&entry, env.os, env.arch, running.as_ref());
    if !allowed_actions(true, &gate, &availability, update).contains(&kind) {
        return Err(match gate {
            ActionGate::Blocked(e) => e,
            ActionGate::Open { .. } => refusal(
                kind,
                update,
                managed_version(&detection).as_deref(),
                &entry.version_text,
            )
            .unwrap_or(InstallError::StateChanged {
                shown: kind.as_str(),
                current: update_word(update),
            }),
        });
    }
    Ok(Checked {
        kind,
        feed,
        request,
        entry,
        driver,
        _busy: busy,
    })
}

/// FR-090 steps 5 and 6: download and verify, one driver run inside `next`, the artifact released after
/// `obtain_and` returned (PD-3, PD-4).
fn obtain_and_run(
    env: CommandEnv<'_>,
    checked: Checked<'_>,
    progress: &mut dyn FnMut(InstallProgress),
    ran: &Cell<bool>,
) -> Result<ActionSummary, InstallError> {
    let Checked {
        kind,
        feed,
        request,
        entry,
        driver,
        _busy,
    } = checked;
    #[cfg(test)]
    let request = match env.request_seam {
        Some(seam) => seam(request),
        None => request,
    };
    let sink: RefCell<&mut dyn FnMut(InstallProgress)> = RefCell::new(progress);
    let obtained = download::obtain_and(
        env.download,
        &feed,
        &request,
        &mut |p| (*sink.borrow_mut())(InstallProgress::Download { event: p }),
        |artifact| {
            ran.set(true);
            let keep = artifact.clone();
            let action = match kind {
                ActionKind::Install => Action::Install {
                    entry: entry.clone(),
                    artifact,
                },
                _ => Action::Update {
                    entry: entry.clone(),
                    artifact,
                },
            };
            let mut results = driver.run(
                InstallEnv {
                    ctx: env.ctx,
                    io: env.io,
                    extract: env.extract,
                    progress: &mut |e| (*sink.borrow_mut())(e),
                },
                vec![action],
            );
            let result = if results.is_empty() {
                Err(InstallError::NoDriver)
            } else {
                results.swap_remove(0).outcome
            };
            (result, keep)
        },
    )
    .map_err(|e| InstallError::Download(Box::new(e)))?;
    let (result, keep) = obtained.value;
    let outcome = result?;
    if let Err(e) = download::discard(keep, env.download.io) {
        warn!(target: LOG_TARGET, "{} {}: the verified artifact was not released", e.code(), entry.name);
    }
    Ok(ActionSummary {
        name: entry.name.clone(),
        action: kind,
        version: outcome.version,
        kept: outcome.kept,
        notice: Some(Notice::RescanDaw),
    })
}

/// The `remove_plugin` command: feed, gate, then one driver run of `Uninstall`; no asset request (FR-094).
pub fn remove_for_command(
    env: CommandEnv<'_>,
    name: &str,
    now_unix: u64,
    progress: &mut dyn FnMut(InstallProgress),
) -> Result<ActionSummary, InstallError> {
    let ran = std::cell::Cell::new(false);
    let result = command_remove(env, name, now_unix, progress, &ran);
    if !ran.get() {
        log_refusal(name, "-", "remove", &result);
    }
    result
}

fn command_remove(
    env: CommandEnv<'_>,
    name: &str,
    now_unix: u64,
    progress: &mut dyn FnMut(InstallProgress),
    ran: &std::cell::Cell<bool>,
) -> Result<ActionSummary, InstallError> {
    // 1. One feed check.
    let check = feed::check(env.ctx, FetchMode::Normal, now_unix);
    let Some(snapshot) = check.snapshot else {
        return Err(InstallError::NoFeed {
            cause: check
                .error
                .as_ref()
                .map(feed::FeedError::code)
                .unwrap_or_default(),
        });
    };
    let Some(entry) = snapshot.feed.plugins.get(name).cloned() else {
        return Err(InstallError::UnknownPlugin {
            name: name.to_owned(),
        });
    };
    // 2. The install facts, the guard, the destinations.
    let Some(facts) = entry.release.as_ref().and_then(|r| r.install.as_ref()) else {
        return Err(InstallError::NotManageable {
            name: entry.name.clone(),
        });
    };
    let bundle = facts.vst3_bundle.as_str();
    let stem = bundle.strip_suffix(".vst3").unwrap_or(bundle).to_owned();
    let Some(_busy) = Busy::enter(env.ctx.roots.state_dir(), &stem) else {
        return Err(InstallError::Busy {
            name: entry.name.clone(),
        });
    };
    Destinations::of(env.ctx, &entry)?;
    // 3. The driver; 4. the gate.
    let driver = env
        .drivers
        .driver_for(env.os)
        .ok_or(InstallError::NoDriver)?;
    match action_gate(env.os, env.ctx, env.io, &entry) {
        ActionGate::Blocked(e) => return Err(e),
        ActionGate::Open { manifest: None } => {
            return Err(InstallError::NotManaged {
                name: entry.name.clone(),
            });
        }
        ActionGate::Open { manifest: Some(_) } => {}
    }
    // 5. Exactly one run of `Uninstall`.
    ran.set(true);
    let entry_name = entry.name.clone();
    let mut results = driver.run(
        InstallEnv {
            ctx: env.ctx,
            io: env.io,
            extract: env.extract,
            progress,
        },
        vec![Action::Uninstall { entry }],
    );
    if results.is_empty() {
        return Err(InstallError::NoDriver);
    }
    let outcome = results.swap_remove(0).outcome?;
    Ok(ActionSummary {
        name: entry_name,
        action: ActionKind::Remove,
        version: outcome.version,
        kept: outcome.kept,
        notice: None,
    })
}

// ---- logging (plan §4.1.14) ----------------------------------------------------------------------------------------

/// FR-117: one `info!` per action result; paths never appear.
fn log_result(
    name: &str,
    version: &str,
    kind: ActionKind,
    outcome: &Result<Outcome, InstallError>,
) {
    match outcome {
        Ok(o) => info!(
            target: LOG_TARGET,
            "{name} {version} {}: ok, placed {}, removed {}, kept {}",
            kind.as_str(),
            o.placed,
            o.removed,
            o.kept.len()
        ),
        Err(e) => info!(
            target: LOG_TARGET,
            "{name} {version} {}: {}, placed 0, removed 0, kept 0",
            kind.as_str(),
            e.code()
        ),
    }
}

/// FR-117 for a command that ended before its driver ran: one `info!` with the code.
fn log_refusal(
    name: &str,
    version: &str,
    action: &str,
    result: &Result<ActionSummary, InstallError>,
) {
    if let Err(e) = result {
        let shown = match action {
            "install" | "update" | "remove" => action,
            _ => "action",
        };
        info!(
            target: LOG_TARGET,
            "{name} {version} {shown}: {}, placed 0, removed 0, kept 0",
            e.code()
        );
    }
}

#[cfg(test)]
#[path = "install_tests.rs"]
mod tests;
