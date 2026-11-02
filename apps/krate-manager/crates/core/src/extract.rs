//! Phase 8: safe extraction and staging of a verified Linux tar.gz (plan §4.3).
//!
//! `stage` is the one extraction entry point (FR-001): it binds the feed entry to the `VerifiedArtifact` (FR-002),
//! reads the archive twice from one handle (D3) through an in-crate gzip + tar reader that applies the C4 rules in
//! `listing.js`'s order (D1, FR-010 to FR-019), writes nothing before pass 1 accepts the whole archive, and proves
//! the written tree physically (FR-046, FR-047) before a `StagedTree` exists. Every filesystem step goes through
//! `ExtractIo` (D5); `FsExtractIo` is the only implementation that touches `std::fs`.

use crate::context::AppContext;
use crate::contract;
use crate::detect::{self, CopyVersion};
use crate::feed::{AssetFormat, Payload, PlainName, PluginEntry};
use crate::verify::{Sha256Stream, VerifiedArtifact, hex_lower};
use flate2::bufread::GzDecoder;
use log::{info, warn};
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};
use std::ffi::{OsStr, OsString};
use std::fs::{self, File};
use std::io::{self, BufRead, BufReader, Read};
use std::mem;
use std::path::{Component, Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};

/// The `log` target of every extraction line (FR-052).
pub const LOG_TARGET: &str = "krate_manager_core::extract";
/// The staging subdirectory that holds work directories and staged trees (D2).
pub const EXTRACT_DIR: &str = "extract";
const BLOCK: usize = 512;
/// The file copy and raw read buffer.
const CHUNK: usize = 64 * 1024;
/// `tempfile`'s random part of a work directory name (PD-9).
const RANDOM_LEN: usize = 6;
const ARROW: &str = " -> ";
/// The fixed words an error names instead of a disk path (plan §3).
const ARCHIVE: &str = "<archive>";
const WORK_DIR: &str = "<work dir>";
/// The staged version file, relative to the bundle (FR-057).
const MODULEINFO: &str = "Contents/Resources/moduleinfo.json";

// ---- error model (plan §3) -----------------------------------------------------------------------------------------

/// The C4 codes Phase 8 reuses from `listing.js` (D9, `CONTRACT.md:310-337`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ListingCode {
    Path,
    Absolute,
    Drive,
    Backslash,
    Dotdot,
    Type,
    RootSymlink,
    SymlinkForm,
    SymlinkEscape,
    ThroughSymlink,
    Duplicate,
    SymlinkChain,
    Checksum,
    Truncated,
    Corrupt,
}

impl ListingCode {
    pub fn as_str(self) -> &'static str {
        match self {
            ListingCode::Path => "E_LISTING_PATH",
            ListingCode::Absolute => "E_LISTING_ABSOLUTE",
            ListingCode::Drive => "E_LISTING_DRIVE",
            ListingCode::Backslash => "E_LISTING_BACKSLASH",
            ListingCode::Dotdot => "E_LISTING_DOTDOT",
            ListingCode::Type => "E_LISTING_TYPE",
            ListingCode::RootSymlink => "E_LISTING_ROOT_SYMLINK",
            ListingCode::SymlinkForm => "E_LISTING_SYMLINK_FORM",
            ListingCode::SymlinkEscape => "E_LISTING_SYMLINK_ESCAPE",
            ListingCode::ThroughSymlink => "E_LISTING_THROUGH_SYMLINK",
            ListingCode::Duplicate => "E_LISTING_DUPLICATE",
            ListingCode::SymlinkChain => "E_LISTING_SYMLINK_CHAIN",
            ListingCode::Checksum => "E_LISTING_CHECKSUM",
            ListingCode::Truncated => "E_LISTING_TRUNCATED",
            ListingCode::Corrupt => "E_LISTING_CORRUPT",
        }
    }
}

/// Why `stage` refused (plan §3). Every `Display` begins with its code (D9); `entry`, `path`, `item` and `line`
/// hold archive-relative listing paths or the fixed words `<archive>`, `<work dir>`, `extract`, never a disk path.
#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum ExtractError {
    #[error("E_EXTRACT_ENTRY_MISMATCH: the feed entry and the artifact differ in {field}")]
    EntryMismatch { field: &'static str },
    #[error("{}: {detail}", .code.as_str())]
    Listing { code: ListingCode, detail: String },
    #[error("E_LISTING_COUNT: the feed says {expected} listing lines, the archive has {found}")]
    Count { expected: u64, found: u64 },
    #[error("E_LISTING_DIGEST: expected {expected}, found {found}")]
    Digest { expected: String, found: String },
    #[error("E_LAYOUT: {item}")]
    Layout { item: String },
    #[error("E_EXTRACT_LIMIT: the {which} bound was crossed")]
    Limit { which: &'static str },
    #[error("E_EXTRACT_CHANGED: the archive differs from the verified bytes")]
    Changed,
    #[error("E_EXTRACT_UNSUPPORTED: {entry} is a symlink and this filesystem cannot hold one")]
    Unsupported { entry: String },
    #[error("E_EXTRACT_PATH: {reason}")]
    Path { reason: &'static str },
    #[error("E_EXTRACT_COLLISION: {entry} already exists in the work directory")]
    Collision { entry: String },
    #[error("E_EXTRACT_ESCAPE: {path} resolves outside the work directory")]
    Escape { path: String },
    #[error("E_EXTRACT_TREE_MISMATCH: first differing line {line:?}")]
    TreeMismatch { line: String },
    #[error("E_EXTRACT_VERSION: expected {expected}, the staged moduleinfo says {found}")]
    Version { expected: String, found: String },
    #[error("E_EXTRACT_FULL: step {step} on {entry}")]
    Full { step: &'static str, entry: String },
    #[error("E_EXTRACT_DENIED: step {step} on {entry}")]
    Denied { step: &'static str, entry: String },
    #[error("E_EXTRACT_IO: step {step} on {entry}: {kind:?}")]
    Io {
        step: &'static str,
        kind: io::ErrorKind,
        entry: String,
    },
}

impl ExtractError {
    /// The code the `Display` begins with.
    pub fn code(&self) -> &'static str {
        match self {
            ExtractError::EntryMismatch { .. } => "E_EXTRACT_ENTRY_MISMATCH",
            ExtractError::Listing { code, .. } => code.as_str(),
            ExtractError::Count { .. } => "E_LISTING_COUNT",
            ExtractError::Digest { .. } => "E_LISTING_DIGEST",
            ExtractError::Layout { .. } => "E_LAYOUT",
            ExtractError::Limit { .. } => "E_EXTRACT_LIMIT",
            ExtractError::Changed => "E_EXTRACT_CHANGED",
            ExtractError::Unsupported { .. } => "E_EXTRACT_UNSUPPORTED",
            ExtractError::Path { .. } => "E_EXTRACT_PATH",
            ExtractError::Collision { .. } => "E_EXTRACT_COLLISION",
            ExtractError::Escape { .. } => "E_EXTRACT_ESCAPE",
            ExtractError::TreeMismatch { .. } => "E_EXTRACT_TREE_MISMATCH",
            ExtractError::Version { .. } => "E_EXTRACT_VERSION",
            ExtractError::Full { .. } => "E_EXTRACT_FULL",
            ExtractError::Denied { .. } => "E_EXTRACT_DENIED",
            ExtractError::Io { .. } => "E_EXTRACT_IO",
        }
    }
}

fn listing_error(code: ListingCode, detail: impl Into<String>) -> ExtractError {
    ExtractError::Listing {
        code,
        detail: detail.into(),
    }
}

fn corrupt(detail: &str) -> ExtractError {
    listing_error(ListingCode::Corrupt, detail)
}

fn truncated(detail: &str) -> ExtractError {
    listing_error(ListingCode::Truncated, detail)
}

fn limit(which: &'static str) -> ExtractError {
    ExtractError::Limit { which }
}

/// FR-045's mapping, in `stage` only: no `ExtractIo` method produces an `E_*` code (D5).
fn io_error(step: &'static str, entry: &str, kind: io::ErrorKind) -> ExtractError {
    let entry = entry.to_owned();
    match kind {
        io::ErrorKind::StorageFull | io::ErrorKind::QuotaExceeded => {
            ExtractError::Full { step, entry }
        }
        io::ErrorKind::PermissionDenied | io::ErrorKind::ReadOnlyFilesystem => {
            ExtractError::Denied { step, entry }
        }
        io::ErrorKind::AlreadyExists if matches!(step, "create_dir" | "create_file") => {
            ExtractError::Collision { entry }
        }
        _ => ExtractError::Io { step, kind, entry },
    }
}

// ---- limits and the environment (plan §4.3.1) ----------------------------------------------------------------------

/// D8's bounds, injected so tests use small values.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ExtractLimits {
    pub max_headers: u64,
    pub max_tar_bytes: u64,
    pub max_meta_bytes: u64,
    pub max_path_bytes: usize,
    pub max_segment_bytes: usize,
    pub max_joined_bytes: usize,
    pub max_moduleinfo_bytes: u64,
}

impl ExtractLimits {
    pub const PRODUCTION: ExtractLimits = ExtractLimits {
        max_headers: 100_000,
        max_tar_bytes: 4 << 30,
        max_meta_bytes: 64 << 10,
        max_path_bytes: 4096,
        max_segment_bytes: 255,
        max_joined_bytes: 4095,
        max_moduleinfo_bytes: detect::MAX_MODULEINFO_BYTES,
    };
}

/// The inputs of `stage` and `prune` (D5): three borrows, `Copy`, like `DownloadEnv`. Not `Send`: a thread builds
/// its own from shared parts.
#[derive(Clone, Copy)]
pub struct ExtractEnv<'a> {
    pub ctx: &'a AppContext,
    pub io: &'a dyn ExtractIo,
    pub limits: &'a ExtractLimits,
}

// ---- ExtractIo and FsExtractIo (plan §4.3.2) -----------------------------------------------------------------------

/// A node's kind, never following a link.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NodeKind {
    Dir,
    File,
    Symlink,
    Other,
}

pub trait ArchiveRead: Read {
    fn rewind(&mut self) -> io::Result<()>;
}
pub trait ExtractFile {
    fn write_all(&mut self, bytes: &[u8]) -> io::Result<()>;
}

/// Every filesystem step of Phase 8 (D5). No method maps to an `E_*` code; `stage` does (FR-045).
pub trait ExtractIo {
    fn open_archive(&self, path: &Path) -> io::Result<Box<dyn ArchiveRead>>;
    /// `NotFound` → `Ok(None)`; the entries of a directory, never following links.
    fn list_dir(&self, dir: &Path) -> io::Result<Option<Vec<(OsString, NodeKind)>>>;
    /// Creates `extract` if absent, requires it to be a directory, then `<extract>/<prefix><6 random>`.
    fn create_work_dir(&self, extract: &Path, prefix: &str) -> io::Result<PathBuf>;
    fn create_dir(&self, path: &Path) -> io::Result<()>; // never create_dir_all
    fn create_file(&self, path: &Path) -> io::Result<Box<dyn ExtractFile>>; // write + create_new
    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()>; // no-op on non-Unix FsExtractIo
    fn symlink(&self, target: &str, link: &Path) -> io::Result<()>;
    fn supports_symlinks(&self) -> bool;
    fn remove_tree(&self, path: &Path) -> io::Result<()>;
    fn remove_entry(&self, path: &Path, kind: NodeKind) -> io::Result<()>; // a link as the link itself
    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>>; // symlink_metadata
    fn read_tree(&self, root: &Path) -> io::Result<Vec<ListingEntry>>; // relative names as bytes, no follow
    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf>;
    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>>; // reads at most max + 1 bytes
}

/// The real filesystem (D4, D5): modes and symlinks on Unix; on every other target no mode is written and
/// `supports_symlinks()` is false. Sets no owner, group, timestamp or extended attribute and syncs nothing (FR-043,
/// FR-055).
pub struct FsExtractIo;

/// `FsExtractIo`'s archive handle (TA-13: a private type).
struct FsArchive(File);

impl Read for FsArchive {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.0.read(buf)
    }
}

impl ArchiveRead for FsArchive {
    fn rewind(&mut self) -> io::Result<()> {
        io::Seek::rewind(&mut self.0)
    }
}

/// `FsExtractIo`'s staged file (TA-13: a private type).
struct FsFile(File);

impl ExtractFile for FsFile {
    fn write_all(&mut self, bytes: &[u8]) -> io::Result<()> {
        io::Write::write_all(&mut self.0, bytes)
    }
}

/// A `file_type()` that did not follow the link (on Windows a junction is a symlink, not `is_dir()`).
fn node_kind(file_type: fs::FileType) -> NodeKind {
    if file_type.is_symlink() {
        NodeKind::Symlink
    } else if file_type.is_dir() {
        NodeKind::Dir
    } else if file_type.is_file() {
        NodeKind::File
    } else {
        NodeKind::Other
    }
}

/// A name as bytes: the raw bytes on Unix.
#[cfg(unix)]
fn os_bytes(name: &OsStr) -> io::Result<Vec<u8>> {
    use std::os::unix::ffi::OsStrExt as _;
    Ok(name.as_bytes().to_vec())
}

/// A name as bytes: its UTF-8 text elsewhere; a name that is not UTF-8 is `InvalidData`.
#[cfg(not(unix))]
fn os_bytes(name: &OsStr) -> io::Result<Vec<u8>> {
    name.to_str()
        .map(|text| text.as_bytes().to_vec())
        .ok_or_else(|| io::Error::from(io::ErrorKind::InvalidData))
}

/// The permission bits on Unix.
#[cfg(unix)]
fn mode_bits(meta: &fs::Metadata) -> Option<u32> {
    use std::os::unix::fs::MetadataExt as _;
    Some(meta.mode() & 0o7777)
}

/// No POSIX modes elsewhere (D4): the tree listing takes the plan's mode.
#[cfg(not(unix))]
fn mode_bits(_meta: &fs::Metadata) -> Option<u32> {
    None
}

impl ExtractIo for FsExtractIo {
    fn open_archive(&self, path: &Path) -> io::Result<Box<dyn ArchiveRead>> {
        Ok(Box::new(FsArchive(File::open(path)?)))
    }

    fn list_dir(&self, dir: &Path) -> io::Result<Option<Vec<(OsString, NodeKind)>>> {
        let items = match fs::read_dir(dir) {
            Ok(items) => items,
            Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
            Err(e) => return Err(e),
        };
        items
            .map(|item| {
                let item = item?;
                Ok((item.file_name(), node_kind(item.file_type()?)))
            })
            .collect::<io::Result<Vec<_>>>()
            .map(Some)
    }

    fn create_work_dir(&self, extract: &Path, prefix: &str) -> io::Result<PathBuf> {
        fs::create_dir_all(extract)?;
        // PD-6: a link at `extract` is never created under or listed.
        if !fs::symlink_metadata(extract)?.file_type().is_dir() {
            return Err(io::Error::from(io::ErrorKind::InvalidInput));
        }
        // D2: `keep` at once, so no `TempDir` drop ever removes anything behind `ExtractIo`'s back.
        Ok(tempfile::Builder::new()
            .prefix(prefix)
            .tempdir_in(extract)?
            .keep())
    }

    fn create_dir(&self, path: &Path) -> io::Result<()> {
        fs::create_dir(path)
    }

    fn create_file(&self, path: &Path) -> io::Result<Box<dyn ExtractFile>> {
        let file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(path)?;
        Ok(Box::new(FsFile(file)))
    }

    #[cfg(unix)]
    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()> {
        use std::os::unix::fs::PermissionsExt as _;
        fs::set_permissions(path, fs::Permissions::from_mode(mode))
    }

    #[cfg(not(unix))]
    fn set_mode(&self, _path: &Path, _mode: u32) -> io::Result<()> {
        Ok(())
    }

    #[cfg(unix)]
    fn symlink(&self, target: &str, link: &Path) -> io::Result<()> {
        std::os::unix::fs::symlink(target, link)
    }

    #[cfg(not(unix))]
    fn symlink(&self, _target: &str, _link: &Path) -> io::Result<()> {
        Err(io::Error::from(io::ErrorKind::Unsupported))
    }

    fn supports_symlinks(&self) -> bool {
        cfg!(unix)
    }

    fn remove_tree(&self, path: &Path) -> io::Result<()> {
        // std's remove_dir_all does not follow a link: it removes the link itself.
        fs::remove_dir_all(path)
    }

    fn remove_entry(&self, path: &Path, kind: NodeKind) -> io::Result<()> {
        /// Windows: `remove_file` cannot remove a directory symlink or junction; `remove_dir_all` removes the
        /// link itself without following it (PD-7).
        #[cfg(windows)]
        fn remove_link(path: &Path) -> io::Result<()> {
            fs::remove_file(path).or_else(|_| fs::remove_dir_all(path))
        }
        #[cfg(not(windows))]
        fn remove_link(path: &Path) -> io::Result<()> {
            fs::remove_file(path)
        }
        match kind {
            NodeKind::Dir => fs::remove_dir_all(path),
            NodeKind::File => fs::remove_file(path),
            NodeKind::Symlink | NodeKind::Other => remove_link(path),
        }
    }

    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>> {
        match fs::symlink_metadata(path) {
            Ok(meta) => Ok(Some(node_kind(meta.file_type()))),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e),
        }
    }

    fn read_tree(&self, root: &Path) -> io::Result<Vec<ListingEntry>> {
        let mut out = Vec::new();
        let mut pending = vec![(root.to_path_buf(), Vec::<u8>::new())];
        while let Some((dir, rel)) = pending.pop() {
            for item in fs::read_dir(&dir)? {
                let name = item?.file_name();
                let mut path = rel.clone();
                if !path.is_empty() {
                    path.push(b'/');
                }
                path.extend_from_slice(&os_bytes(&name)?);
                let abs = dir.join(&name);
                let meta = fs::symlink_metadata(&abs)?;
                let mode = mode_bits(&meta);
                let kind = match node_kind(meta.file_type()) {
                    NodeKind::Symlink => {
                        let target = os_bytes(fs::read_link(&abs)?.as_os_str())?;
                        out.push(ListingEntry {
                            kind: EntryType::Symlink,
                            mode,
                            path,
                            target: Some(target),
                        });
                        continue;
                    }
                    NodeKind::Dir => {
                        pending.push((abs, path.clone()));
                        EntryType::Dir
                    }
                    NodeKind::File => EntryType::File,
                    NodeKind::Other => EntryType::Other(b'?'),
                };
                out.push(ListingEntry {
                    kind,
                    mode,
                    path,
                    target: None,
                });
            }
        }
        Ok(out)
    }

    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        fs::canonicalize(path)
    }

    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        let mut bytes = Vec::new();
        File::open(path)?
            .take(max.saturating_add(1))
            .read_to_end(&mut bytes)?;
        Ok(bytes)
    }
}

// ---- the raw reader and the gzip member loop (plan §4.3.3) ---------------------------------------------------------

/// The raw archive bytes as read: hashed (D6) and the first source error kind recorded (PD-4), so `stage` tells a
/// read failure (`read_archive`) from a gzip error (`E_LISTING_CORRUPT`).
struct Hashed<'h> {
    inner: &'h mut dyn ArchiveRead,
    sha: Sha256Stream,
    source_error: Option<io::ErrorKind>,
}

impl<'h> Hashed<'h> {
    fn new(inner: &'h mut dyn ArchiveRead) -> Hashed<'h> {
        Hashed {
            inner,
            sha: Sha256Stream::new(),
            source_error: None,
        }
    }

    fn finish(self) -> [u8; 32] {
        self.sha.finish()
    }
}

impl Read for Hashed<'_> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        loop {
            match self.inner.read(buf) {
                Ok(n) => {
                    self.sha.update(&buf[..n]);
                    return Ok(n);
                }
                Err(e) if e.kind() == io::ErrorKind::Interrupted => {}
                Err(e) => {
                    if self.source_error.is_none() {
                        self.source_error = Some(e.kind());
                    }
                    return Err(e);
                }
            }
        }
    }
}

enum GzState<R: BufRead> {
    Member(Box<GzDecoder<R>>),
    Done,
    Failed,
}

/// The decompressed tar stream (FR-010): `bufread::GzDecoder` member by member, a remainder of only zero bytes
/// accepted after a member, every byte (padding included) counted against `max_tar_bytes` before it is passed up
/// (D8). Any gzip error is `E_LISTING_CORRUPT`.
struct GzStream<R: BufRead> {
    state: GzState<R>,
    total: u64,
    max: u64,
}

impl<R: BufRead> GzStream<R> {
    fn new(inner: R, max: u64) -> GzStream<R> {
        GzStream {
            state: GzState::Member(Box::new(GzDecoder::new(inner))),
            total: 0,
            max,
        }
    }

    fn count(&mut self, n: usize) -> Result<(), ExtractError> {
        let n = u64::try_from(n).unwrap_or(u64::MAX);
        match self.total.checked_add(n) {
            Some(total) if total <= self.max => {
                self.total = total;
                Ok(())
            }
            _ => {
                self.state = GzState::Failed;
                Err(limit("tar_bytes"))
            }
        }
    }

    fn read(&mut self, buf: &mut [u8]) -> Result<usize, ExtractError> {
        if buf.is_empty() {
            return Ok(0);
        }
        loop {
            let GzState::Member(decoder) = &mut self.state else {
                return match self.state {
                    GzState::Done => Ok(0),
                    _ => Err(corrupt("gzip: the stream already failed")),
                };
            };
            match decoder.read(buf) {
                Ok(0) => {
                    // The member ended with a correct CRC and length.
                    if let GzState::Member(decoder) = mem::replace(&mut self.state, GzState::Failed)
                    {
                        self.after_member(decoder.into_inner())?;
                    }
                }
                Ok(n) => {
                    self.count(n)?;
                    return Ok(n);
                }
                Err(e) if e.kind() == io::ErrorKind::Interrupted => {}
                Err(_) => {
                    self.state = GzState::Failed;
                    return Err(corrupt("gzip: the member does not decode"));
                }
            }
        }
    }

    /// After a member: EOF ends the stream; a non-zero byte starts the next member; a zero byte starts a remainder
    /// that must hold only zero bytes (FR-010, Q8).
    fn after_member(&mut self, mut inner: R) -> Result<(), ExtractError> {
        let first = match inner.fill_buf() {
            Ok(bytes) => bytes.first().copied(),
            Err(_) => return Err(corrupt("gzip: the stream cannot be read past a member")),
        };
        match first {
            None => self.state = GzState::Done,
            Some(0) => {
                loop {
                    let bytes = inner
                        .fill_buf()
                        .map_err(|_| corrupt("gzip: the padding cannot be read"))?;
                    if bytes.is_empty() {
                        break;
                    }
                    let n = bytes.len();
                    if bytes.iter().any(|&b| b != 0) {
                        return Err(corrupt("gzip: trailing bytes after the last member"));
                    }
                    inner.consume(n);
                    self.count(n)?;
                }
                self.state = GzState::Done;
            }
            Some(_) => self.state = GzState::Member(Box::new(GzDecoder::new(inner))),
        }
        Ok(())
    }
}

// ---- the tar header reader (plan §4.3.4; a port of `listing.js:163-274`) ------------------------------------------

/// One f / d / l / other header after FR-013's precedence.
struct RawEntry {
    kind: EntryType,
    mode: u32,
    size: u64,
    path: Vec<u8>,
    target: Option<Vec<u8>>,
}

/// GNU `L` / `K` and pax `x` values waiting for the next header.
#[derive(Default)]
struct Pending {
    long_name: Option<Vec<u8>>,
    long_link: Option<Vec<u8>>,
    pax_path: Option<Vec<u8>>,
    pax_linkpath: Option<Vec<u8>>,
}

impl Pending {
    fn is_set(&self) -> bool {
        self.long_name.is_some()
            || self.long_link.is_some()
            || self.pax_path.is_some()
            || self.pax_linkpath.is_some()
    }
}

/// A data copy failed in the stream or in the sink the caller passed.
enum DataError {
    Stream(ExtractError),
    Sink(ExtractError),
}

type Sink<'s> = dyn FnMut(&[u8]) -> Result<(), ExtractError> + 's;

struct TarReader<R: BufRead> {
    src: GzStream<R>,
    limits: ExtractLimits,
    headers: u64,
    pending: Pending,
    ended: bool,
    scratch: Vec<u8>,
}

/// The bytes before the first NUL (`listing.js` `cstrBytes`).
fn cut_at_nul(bytes: &[u8]) -> &[u8] {
    bytes
        .iter()
        .position(|&b| b == 0)
        .map_or(bytes, |nul| &bytes[..nul])
}

/// `^ *([0-7]+)[\0 ]*$` over the field's bytes (latin-1), as a number.
fn octal_value(field: &[u8]) -> Option<u64> {
    let start = field.iter().position(|&b| b != b' ')?;
    let rest = &field[start..];
    let digits = rest
        .iter()
        .take_while(|b| (b'0'..=b'7').contains(*b))
        .count();
    if digits == 0 || !rest[digits..].iter().all(|&b| b == 0 || b == b' ') {
        return None;
    }
    rest[..digits].iter().try_fold(0u64, |acc, &d| {
        acc.checked_mul(8)?.checked_add(u64::from(d - b'0'))
    })
}

/// `listing.js` `octal`: a base-256 field or any other form is `E_LISTING_CORRUPT` (FR-012).
fn octal(field: &[u8], what: &str) -> Result<u64, ExtractError> {
    if field.first().is_some_and(|&b| b & 0x80 != 0) {
        return Err(corrupt(&format!("header {what} is a base-256 number")));
    }
    octal_value(field).ok_or_else(|| corrupt(&format!("header {what} is not octal")))
}

/// `listing.js` `checkChecksum`: the stored value equals the unsigned or the signed byte sum (FR-012).
fn check_checksum(header: &[u8; BLOCK]) -> Result<(), ExtractError> {
    let mut unsigned: i64 = 0;
    let mut signed: i64 = 0;
    for (i, &b) in header.iter().enumerate() {
        let b = if (148..156).contains(&i) { b' ' } else { b };
        unsigned += i64::from(b);
        signed += i64::from(b) - if b > 127 { 256 } else { 0 };
    }
    let stored = octal_value(&header[148..156]).and_then(|v| i64::try_from(v).ok());
    match stored {
        Some(s) if s == unsigned || s == signed => Ok(()),
        _ => Err(listing_error(
            ListingCode::Checksum,
            "a header checksum does not match",
        )),
    }
}

/// A pax `x` record set's `path` and `linkpath` values.
type PaxValues = (Option<Vec<u8>>, Option<Vec<u8>>);

/// `listing.js` `paxRecords`: `<len> <key>=<value>\n`; only `path` / `linkpath` are kept, the last value wins,
/// every other key (`SCHILY.xattr.*` included) is dropped here (FR-013).
fn pax_records(data: &[u8]) -> Result<PaxValues, ExtractError> {
    let bad_length = || corrupt("pax record length is not a decimal ending at an LF");
    let (mut path, mut linkpath) = (None, None);
    let mut pos = 0;
    while pos < data.len() {
        let sp = data[pos..]
            .iter()
            .position(|&b| b == b' ')
            .map(|i| pos + i)
            .ok_or_else(bad_length)?;
        let len_text = &data[pos..sp];
        if len_text.is_empty() || !len_text.iter().all(u8::is_ascii_digit) {
            return Err(bad_length());
        }
        let len = len_text
            .iter()
            .try_fold(0usize, |acc, &d| {
                acc.checked_mul(10)?.checked_add(usize::from(d - b'0'))
            })
            .ok_or_else(bad_length)?;
        let end = pos.checked_add(len).ok_or_else(bad_length)?;
        if end > data.len() || end <= sp + 1 || data[end - 1] != b'\n' {
            return Err(bad_length());
        }
        let record = &data[sp + 1..end - 1];
        let eq = record
            .iter()
            .position(|&b| b == b'=')
            .ok_or_else(|| corrupt("pax record without '='"))?;
        let (key, value) = (&record[..eq], &record[eq + 1..]);
        if key == b"path" {
            path = Some(value.to_vec());
        } else if key == b"linkpath" {
            linkpath = Some(value.to_vec());
        }
        pos = end;
    }
    Ok((path, linkpath))
}

impl<R: BufRead> TarReader<R> {
    fn new(inner: R, limits: ExtractLimits) -> TarReader<R> {
        TarReader {
            src: GzStream::new(inner, limits.max_tar_bytes),
            limits,
            headers: 0,
            pending: Pending::default(),
            ended: false,
            scratch: vec![0u8; CHUNK],
        }
    }

    /// Fills `buf` unless the stream ends first; returns the count read.
    fn read_full(&mut self, buf: &mut [u8]) -> Result<usize, ExtractError> {
        let mut n = 0;
        while n < buf.len() {
            let k = self.src.read(&mut buf[n..])?;
            if k == 0 {
                break;
            }
            n += k;
        }
        Ok(n)
    }

    /// The next f / d / l / other entry, or `None` after the end-of-archive block and the drain to EOF.
    fn next_entry(&mut self) -> Result<Option<RawEntry>, ExtractError> {
        if self.ended {
            return Ok(None);
        }
        loop {
            let mut header = [0u8; BLOCK];
            let n = self.read_full(&mut header)?;
            if n == 0 {
                return Err(truncated("the stream ends before the end-of-archive block"));
            }
            if n < BLOCK {
                return Err(truncated("the stream ends inside a header"));
            }
            if header.iter().all(|&b| b == 0) {
                self.ended = true;
                // Drained, never interpreted, so the last CRC is checked and the raw file is read to EOF (D6).
                while self.src.read(&mut self.scratch)? > 0 {}
                if self.pending.is_set() {
                    return Err(truncated("an L / K / x record has no following header"));
                }
                return Ok(None);
            }
            self.headers += 1;
            if self.headers > self.limits.max_headers {
                return Err(limit("headers"));
            }
            check_checksum(&header)?;
            let mode = octal(&header[100..108], "mode")?;
            let size = octal(&header[124..136], "size")?;
            let flag = header[156];
            if matches!(flag, b'L' | b'K' | b'x' | b'g') {
                if size > self.limits.max_meta_bytes {
                    return Err(limit("meta"));
                }
                let data = self.read_meta(size)?;
                match flag {
                    b'L' => self.pending.long_name = Some(cut_at_nul(&data).to_vec()),
                    b'K' => self.pending.long_link = Some(cut_at_nul(&data).to_vec()),
                    b'x' => {
                        let (path, linkpath) = pax_records(&data)?;
                        if path.is_some() {
                            self.pending.pax_path = path;
                        }
                        if linkpath.is_some() {
                            self.pending.pax_linkpath = linkpath;
                        }
                    }
                    // A pax global header is ignored (FR-013).
                    _ => {}
                }
                continue;
            }
            return self.entry(&header, mode, size, flag).map(Some);
        }
    }

    fn entry(
        &mut self,
        header: &[u8; BLOCK],
        mode: u64,
        size: u64,
        flag: u8,
    ) -> Result<RawEntry, ExtractError> {
        let mut name = cut_at_nul(&header[0..100]).to_vec();
        // The prefix field exists only in a POSIX ustar header (`listing.js:258-262`).
        let posix = &header[257..263] == b"ustar\0" && &header[263..265] == b"00";
        let prefix = cut_at_nul(&header[345..500]);
        if posix && !prefix.is_empty() {
            let mut joined = prefix.to_vec();
            joined.push(b'/');
            joined.extend_from_slice(&name);
            name = joined;
        }
        let pending = mem::take(&mut self.pending);
        let kind = match flag {
            b'0' | 0 => EntryType::File,
            b'5' => EntryType::Dir,
            b'2' => EntryType::Symlink,
            other => EntryType::Other(other),
        };
        // Presence, not non-emptiness, decides (JS `||` on a Buffer, `listing.js:264-265`).
        let path = pending.pax_path.or(pending.long_name).unwrap_or(name);
        let target = (kind == EntryType::Symlink).then(|| {
            pending
                .pax_linkpath
                .or(pending.long_link)
                .unwrap_or_else(|| cut_at_nul(&header[157..257]).to_vec())
        });
        if path.len() > self.limits.max_path_bytes
            || target
                .as_ref()
                .is_some_and(|t| t.len() > self.limits.max_path_bytes)
        {
            return Err(limit("path"));
        }
        if path
            .split(|&b| b == b'/')
            .any(|segment| segment.len() > self.limits.max_segment_bytes)
        {
            return Err(limit("segment"));
        }
        let mode = u32::try_from(mode).map_err(|_| corrupt("header mode is out of range"))?;
        Ok(RawEntry {
            kind,
            mode,
            size,
            path,
            target,
        })
    }

    /// A metadata record's data (at most `max_meta_bytes`, checked by the caller), then its padding.
    fn read_meta(&mut self, size: u64) -> Result<Vec<u8>, ExtractError> {
        let len = usize::try_from(size).map_err(|_| limit("meta"))?;
        let mut data = vec![0u8; len];
        if self.read_full(&mut data)? < len {
            return Err(truncated("a record runs past the end of the stream"));
        }
        self.skip_padding(size)?;
        Ok(data)
    }

    /// The padding to the next 512-byte boundary; an EOF here is left to the next header read (`Truncated`).
    fn skip_padding(&mut self, size: u64) -> Result<(), ExtractError> {
        let rem = usize::try_from(size % BLOCK as u64).unwrap_or(0);
        if rem == 0 {
            return Ok(());
        }
        let mut pad = [0u8; BLOCK];
        self.read_full(&mut pad[..BLOCK - rem]).map(|_| ())
    }

    /// Exactly `size` data bytes to `sink` in `CHUNK` slices, then the padding.
    fn copy_data(&mut self, size: u64, sink: &mut Sink<'_>) -> Result<(), DataError> {
        let mut left = size;
        while left > 0 {
            let want = usize::try_from(left).map_or(CHUNK, |l| l.min(CHUNK));
            let n = self
                .src
                .read(&mut self.scratch[..want])
                .map_err(DataError::Stream)?;
            if n == 0 {
                return Err(DataError::Stream(truncated(
                    "entry data runs past the end of the stream",
                )));
            }
            sink(&self.scratch[..n]).map_err(DataError::Sink)?;
            left -= u64::try_from(n).unwrap_or(left);
        }
        self.skip_padding(size).map_err(DataError::Stream)
    }

    fn skip_data(&mut self, size: u64) -> Result<(), ExtractError> {
        match self.copy_data(size, &mut |_| Ok(())) {
            Ok(()) => Ok(()),
            Err(DataError::Stream(e) | DataError::Sink(e)) => Err(e),
        }
    }
}

// ---- the rule engine and the canonical listing (plan §4.3.5; PD-11) ------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EntryType {
    File,
    Dir,
    Symlink,
    Other(u8),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ListingEntry {
    pub kind: EntryType,
    pub mode: Option<u32>,
    pub path: Vec<u8>,
    pub target: Option<Vec<u8>>,
}

/// The C4 canonical listing (FR-020): lines without terminators, sorted by the UTF-8 bytes of the path.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CanonicalListing {
    lines: Vec<String>,
}

impl CanonicalListing {
    /// `listing.js` `listingFromEntries` (FR-021): rule 1 for every entry, then rule 2 for every link, then rule 3.
    pub fn from_entries(entries: &[ListingEntry]) -> Result<CanonicalListing, ExtractError> {
        let mut items = Vec::new();
        for e in entries {
            if let Some(path) = canonical_path(e.kind, &e.path)? {
                items.push((
                    Item {
                        kind: e.kind,
                        mode: e.mode.unwrap_or(0),
                        path,
                        target: None,
                        resolved: None,
                    },
                    e.target.as_deref(),
                ));
            }
        }
        for (item, raw_target) in &mut items {
            if item.kind == EntryType::Symlink {
                let (target, resolved) = check_link(&item.path, *raw_target)?;
                item.target = Some(target);
                item.resolved = Some(resolved);
            }
        }
        let items: Vec<Item> = items.into_iter().map(|(item, _)| item).collect();
        Ok(CanonicalListing {
            lines: rule3(&items)?,
        })
    }

    pub fn lines(&self) -> &[String] {
        &self.lines
    }

    /// Every line followed by `'\n'`.
    pub fn text(&self) -> String {
        self.lines.iter().fold(String::new(), |mut text, line| {
            text.push_str(line);
            text.push('\n');
            text
        })
    }

    /// The line count.
    pub fn files(&self) -> u64 {
        self.lines.len() as u64
    }

    /// `Sha256Stream` over `text()`'s bytes.
    pub fn digest(&self) -> [u8; 32] {
        let mut sha = Sha256Stream::new();
        sha.update(self.text().as_bytes());
        sha.finish()
    }
}

/// One entry that passed rule 1 (and, for a link, rule 2).
struct Item {
    kind: EntryType,
    mode: u32,
    path: String,
    target: Option<String>,
    resolved: Option<String>,
}

/// `listing.js` `decode`: fatal UTF-8, a BOM kept.
fn decode<'b>(bytes: &'b [u8], what: &str) -> Result<&'b str, ExtractError> {
    std::str::from_utf8(bytes)
        .map_err(|_| listing_error(ListingCode::Path, format!("{what}: invalid UTF-8")))
}

/// `listing.js` `checkCharacters`, shared by entry paths and link targets.
fn check_characters(s: &str, what: &str) -> Result<(), ExtractError> {
    if s.is_empty() {
        return Err(listing_error(ListingCode::Path, format!("{what}: empty")));
    }
    if s.contains(['\0', '\r', '\n']) {
        return Err(listing_error(
            ListingCode::Path,
            format!("{what}: holds a NUL, CR or LF: {s:?}"),
        ));
    }
    if s.starts_with('/') {
        return Err(listing_error(
            ListingCode::Absolute,
            format!("{what}: absolute: {s:?}"),
        ));
    }
    let mut chars = s.chars();
    if chars.next().is_some_and(|c| c.is_ascii_alphabetic()) && chars.next() == Some(':') {
        return Err(listing_error(
            ListingCode::Drive,
            format!("{what}: drive prefix: {s:?}"),
        ));
    }
    if s.contains('\\') {
        return Err(listing_error(
            ListingCode::Backslash,
            format!("{what}: holds a backslash: {s:?}"),
        ));
    }
    Ok(())
}

/// Rule 1 (`listing.js` `canonicalPath`, FR-017): the canonical path, or `None` for the archive root.
fn canonical_path(kind: EntryType, raw: &[u8]) -> Result<Option<String>, ExtractError> {
    let raw = decode(raw, "entry path")?;
    if raw.contains(ARROW) {
        return Err(listing_error(
            ListingCode::Path,
            format!("entry path holds \" -> \": {raw:?}"),
        ));
    }
    check_characters(raw, "entry path")?;
    let mut p = raw.strip_prefix("./").unwrap_or(raw);
    if kind == EntryType::Dir {
        if p.is_empty() || p == "." {
            return Ok(None);
        }
        p = p.strip_suffix('/').unwrap_or(p);
    }
    if p.split('/').any(|s| s.is_empty() || s == "." || s == "..") {
        return Err(listing_error(
            ListingCode::Dotdot,
            format!("entry path has an empty, . or .. segment: {raw:?}"),
        ));
    }
    if let EntryType::Other(flag) = kind {
        return Err(listing_error(
            ListingCode::Type,
            format!(
                "entry {p:?}: type not allowed (typeflag {:?})",
                char::from(flag)
            ),
        ));
    }
    Ok(Some(p.to_owned()))
}

/// Rule 2 (`listing.js` `checkLink`, FR-018): the target and its lexical resolution from the link's parent.
fn check_link(path: &str, raw_target: Option<&[u8]>) -> Result<(String, String), ExtractError> {
    let segs: Vec<&str> = path.split('/').collect();
    if segs.len() == 1 {
        return Err(listing_error(
            ListingCode::RootSymlink,
            format!("symlink at the archive root: {path:?}"),
        ));
    }
    let what = format!("link target of {path}");
    let target = match raw_target {
        Some(raw) => decode(raw, &what)?,
        None => return Err(listing_error(ListingCode::Path, format!("{what}: missing"))),
    };
    check_characters(target, &what)?;
    let t: Vec<&str> = target.split('/').collect();
    let ups = t.iter().take_while(|s| **s == "..").count();
    if ups == t.len()
        || t[ups..]
            .iter()
            .any(|s| s.is_empty() || *s == "." || *s == "..")
    {
        return Err(listing_error(
            ListingCode::SymlinkForm,
            format!("link {path:?}: target is not leading .. then plain segments: {target:?}"),
        ));
    }
    // posix.normalize(posix.join(posix.dirname(path), target)).
    let mut stack: Vec<&str> = segs[..segs.len() - 1].to_vec();
    for &s in &t {
        if s == ".." {
            if stack.last().is_some_and(|top| *top != "..") {
                stack.pop();
            } else {
                stack.push("..");
            }
        } else {
            stack.push(s);
        }
    }
    let resolved = if stack.is_empty() {
        ".".to_owned()
    } else {
        stack.join("/")
    };
    if resolved == "."
        || resolved == ".."
        || resolved.starts_with("../")
        || stack.first() != segs.first()
        || stack.len() < 2
    {
        return Err(listing_error(
            ListingCode::SymlinkEscape,
            format!(
                "link {path:?} -> {target:?} resolves to {resolved:?}, not strictly inside {:?}",
                segs[0]
            ),
        ));
    }
    Ok((target.to_owned(), resolved))
}

/// `listing.js` `properPrefixes`: `a/b/c` → `a`, `a/b`.
fn proper_prefixes(p: &str) -> impl Iterator<Item = &str> {
    p.match_indices('/').map(move |(i, _)| &p[..i])
}

/// `listing.js` `modeOf`; a `None` mode on a file arrives here as 0, so `0644`.
fn mode_text(kind: EntryType, mode: u32) -> &'static str {
    match kind {
        EntryType::Dir => "0755",
        EntryType::Symlink => "0777",
        _ if mode & 0o111 != 0 => "0755",
        _ => "0644",
    }
}

/// Rule 3 (`listing.js:121-158`, FR-019): through-symlink, duplicates then implied ancestors, chains; then the lines
/// sorted by `String` order (byte order, as `Buffer.compare`).
fn rule3(items: &[Item]) -> Result<Vec<String>, ExtractError> {
    let links: BTreeSet<&str> = items
        .iter()
        .filter(|it| it.kind == EntryType::Symlink)
        .map(|it| it.path.as_str())
        .collect();
    for it in items {
        if let Some(via) = proper_prefixes(&it.path).find(|q| links.contains(q)) {
            return Err(listing_error(
                ListingCode::ThroughSymlink,
                format!("entry {:?} lies under the symlink {via:?}", it.path),
            ));
        }
    }
    let mut by_path: BTreeMap<&str, (EntryType, u32, Option<&str>)> = BTreeMap::new();
    for it in items {
        if by_path
            .insert(&it.path, (it.kind, it.mode, it.target.as_deref()))
            .is_some()
        {
            return Err(listing_error(
                ListingCode::Duplicate,
                format!("path listed twice: {:?}", it.path),
            ));
        }
    }
    for it in items {
        for q in proper_prefixes(&it.path) {
            match by_path.get(q) {
                None => {
                    by_path.insert(q, (EntryType::Dir, 0o755, None));
                }
                Some((kind, _, _)) if *kind != EntryType::Dir => {
                    return Err(listing_error(
                        ListingCode::Duplicate,
                        format!("{q:?} is a file and also the directory of {:?}", it.path),
                    ));
                }
                Some(_) => {}
            }
        }
    }
    for it in items {
        let Some(resolved) = it.resolved.as_deref() else {
            continue;
        };
        let mut candidates = proper_prefixes(resolved).chain(std::iter::once(resolved));
        if let Some(hit) = candidates.find(|q| links.contains(q)) {
            return Err(listing_error(
                ListingCode::SymlinkChain,
                format!("link {:?} resolves to or through the link {hit:?}", it.path),
            ));
        }
    }
    Ok(by_path
        .into_iter()
        .map(|(path, (kind, mode, target))| {
            let letter = match kind {
                EntryType::Dir => 'd',
                EntryType::Symlink => 'l',
                _ => 'f',
            };
            let head = format!("{letter} {} {path}", mode_text(kind, mode));
            match target {
                Some(target) => format!("{head}{ARROW}{target}"),
                None => head,
            }
        })
        .collect())
}

/// One canonical listing line, split.
struct Line<'l> {
    kind: char,
    mode: &'l str,
    path: &'l str,
}

/// `<t> <mmmm> <path>[ -> <target>]`; a path holds no ` -> ` (rule 1).
fn parse_line(line: &str) -> Line<'_> {
    let kind = line.chars().next().unwrap_or(' ');
    let rest = line.get(7..).unwrap_or_default();
    let path = match rest.split_once(ARROW) {
        Some((path, _)) if kind == 'l' => path,
        _ => rest,
    };
    Line {
        kind,
        mode: line.get(2..6).unwrap_or_default(),
        path,
    }
}

// ---- pass 1: the plan (plan §4.3.6; FR-009, FR-015; PD-1) ----------------------------------------------------------

/// Pass 1's result: the counted entries in archive order and the canonical listing.
pub(crate) struct ArchivePlan {
    pub(crate) entries: Vec<PlanEntry>,
    pub(crate) listing: CanonicalListing,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct PlanEntry {
    pub(crate) path: String,
    pub(crate) kind: PlanKind,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum PlanKind {
    File { size: u64, exec: bool },
    Dir,
    Symlink { target: String },
}

/// Why one entry was not accepted: rule 1 or rule 2 (recorded, PD-1).
enum Rejected {
    Rule1(ExtractError),
    Rule2(ExtractError),
}

/// Rules 1 and 2 on one entry (PD-11: the same functions as `from_entries`); `None` for the archive root.
fn accept(raw: &RawEntry) -> Result<Option<(Item, PlanEntry)>, Rejected> {
    let Some(path) = canonical_path(raw.kind, &raw.path).map_err(Rejected::Rule1)? else {
        return Ok(None);
    };
    let (kind, target, resolved) = match raw.kind {
        EntryType::File => (
            PlanKind::File {
                size: raw.size,
                exec: raw.mode & 0o111 != 0,
            },
            None,
            None,
        ),
        EntryType::Dir => (PlanKind::Dir, None, None),
        EntryType::Symlink => {
            let (target, resolved) =
                check_link(&path, raw.target.as_deref()).map_err(Rejected::Rule2)?;
            (
                PlanKind::Symlink {
                    target: target.clone(),
                },
                Some(target),
                Some(resolved),
            )
        }
        // `canonical_path` refuses every other type first.
        EntryType::Other(_) => {
            return Err(Rejected::Rule1(listing_error(
                ListingCode::Type,
                "type not allowed",
            )));
        }
    };
    let item = Item {
        kind: raw.kind,
        mode: raw.mode,
        path: path.clone(),
        target,
        resolved,
    };
    Ok(Some((item, PlanEntry { path, kind })))
}

/// Pass 1 (FR-009): FR-010 to FR-019 and the listing; no `ExtractIo`, no payload, no layout. A header-level archive
/// error stops reading at once; the first rule-1 and the first rule-2 failure are recorded and reading goes on; a
/// bound stops reading at once. Resolution (PD-1): the stopping archive error, else rule 1, else rule 2, else the
/// bound, else rule 3, else the plan.
pub(crate) fn plan_archive(
    reader: impl Read,
    limits: &ExtractLimits,
    max_entries: u64,
) -> Result<ArchivePlan, ExtractError> {
    let mut tar = TarReader::new(BufReader::with_capacity(CHUNK, reader), *limits);
    let (mut first_rule1, mut first_rule2) = (None, None);
    let mut items = Vec::new();
    let mut entries = Vec::new();
    let mut counted: u64 = 0;
    let stopped = loop {
        let raw = match tar.next_entry() {
            Ok(Some(raw)) => raw,
            Ok(None) => break None,
            Err(e) => break Some(e),
        };
        match accept(&raw) {
            Ok(Some((item, entry))) => {
                counted += 1;
                if counted > max_entries {
                    break Some(limit("entries"));
                }
                items.push(item);
                entries.push(entry);
            }
            Ok(None) => {}
            Err(Rejected::Rule1(e)) => {
                if first_rule1.is_none() {
                    first_rule1 = Some(e);
                }
            }
            Err(Rejected::Rule2(e)) => {
                if first_rule2.is_none() {
                    first_rule2 = Some(e);
                }
            }
        }
        if let Err(e) = tar.skip_data(raw.size) {
            break Some(e);
        }
    };
    if let Some(e) = &stopped
        && !matches!(e, ExtractError::Limit { .. })
    {
        return Err(e.clone());
    }
    if let Some(e) = first_rule1.or(first_rule2).or(stopped) {
        return Err(e);
    }
    Ok(ArchivePlan {
        entries,
        listing: CanonicalListing {
            lines: rule3(&items)?,
        },
    })
}

// ---- the staged-layout check (plan §4.3.7; FR-030 to FR-033) -------------------------------------------------------

/// The payload count and digest, then the C4 Linux layout for `vst3_bundle` (`listing.js:359-382`).
pub(crate) fn check_staged_layout(
    listing: &CanonicalListing,
    payload: &Payload,
    vst3_bundle: &PlainName,
) -> Result<(), ExtractError> {
    if listing.files() != payload.files {
        return Err(ExtractError::Count {
            expected: payload.files,
            found: listing.files(),
        });
    }
    let digest = listing.digest();
    if digest != payload.sha256 {
        return Err(ExtractError::Digest {
            expected: hex_lower(&payload.sha256),
            found: hex_lower(&digest),
        });
    }
    let bundle = vst3_bundle.as_str();
    let layout = |item: &str| ExtractError::Layout {
        item: item.to_owned(),
    };
    let Some(stem) = bundle.strip_suffix(".vst3").filter(|s| !s.is_empty()) else {
        return Err(layout(bundle));
    };
    let by_path: BTreeMap<&str, char> = listing
        .lines()
        .iter()
        .map(|line| {
            let line = parse_line(line);
            (line.path, line.kind)
        })
        .collect();
    let required = [
        (bundle.to_owned(), 'd'),
        ("presets".to_owned(), 'd'),
        ("README.txt".to_owned(), 'f'),
        ("CHANGELOG.txt".to_owned(), 'f'),
        (format!("{bundle}/{MODULEINFO}"), 'f'),
        (format!("{bundle}/Contents/x86_64-linux/{stem}.so"), 'f'),
    ];
    for (item, kind) in &required {
        if by_path.get(item.as_str()) != Some(kind) {
            return Err(layout(item));
        }
    }
    if let Some(extra) = by_path
        .keys()
        .find(|p| !p.contains('/') && !required[..4].iter().any(|(item, _)| item == *p))
    {
        return Err(layout(extra));
    }
    Ok(())
}

// ---- the one join and the joined bound (plan §4.3.8; FR-041, FR-059; PD-9) -----------------------------------------

/// FR-041: every write target is `work` joined with the checked `/`-segments of `rel`, a strict descendant of `work`.
/// On Windows a reserved device stem or a trailing dot / space is refused too: Win32 would create another name.
pub(crate) fn entry_path(work: &Path, rel: &str) -> Result<PathBuf, ExtractError> {
    let refuse = |reason: &'static str| Err(ExtractError::Path { reason });
    if rel.is_empty() {
        return refuse("an empty entry path");
    }
    let mut path = work.to_path_buf();
    for segment in rel.split('/') {
        if segment.is_empty() || segment == "." || segment == ".." {
            return refuse("an empty, . or .. segment");
        }
        if segment.contains(['\\', ':', '\0']) {
            return refuse("a segment holds a backslash, a colon or a NUL");
        }
        let mut components = Path::new(segment).components();
        if !matches!(
            (components.next(), components.next()),
            (Some(Component::Normal(_)), None)
        ) {
            return refuse("a segment is not one normal path component");
        }
        #[cfg(windows)]
        if contract::reserved_stem_re().is_match(segment)
            || segment.ends_with('.')
            || segment.ends_with(' ')
        {
            return refuse("a segment Windows maps to a device or another name");
        }
        path.push(segment);
    }
    if path == work || !path.starts_with(work) {
        return refuse("the joined path is not under the work directory");
    }
    Ok(path)
}

/// FR-059: the encoded length of `staging/extract/<hex>.<6 x>/<rel>`, built by `entry_path`.
fn joined_len(staging: &Path, hex: &str, rel: &str) -> Result<usize, ExtractError> {
    let work = staging
        .join(EXTRACT_DIR)
        .join(format!("{hex}.{}", "x".repeat(RANDOM_LEN)));
    Ok(entry_path(&work, rel)?.as_os_str().as_encoded_bytes().len())
}

// ---- the live registry and prune (plan §4.3.9; D2, FR-049, FR-058; PD-2, PD-6, PD-7) -------------------------------

/// Every in-progress work dir and every finished `StagedTree` root of this process (D2, FR-049).
static LIVE: Mutex<BTreeSet<PathBuf>> = Mutex::new(BTreeSet::new());

fn live() -> MutexGuard<'static, BTreeSet<PathBuf>> {
    LIVE.lock().unwrap_or_else(PoisonError::into_inner)
}

/// One registry entry; dropping it (also while unwinding) deregisters the root (FR-049, FR-051).
#[derive(Debug)]
struct LiveRoot {
    path: PathBuf,
}

impl Drop for LiveRoot {
    fn drop(&mut self) {
        live().remove(&self.path);
    }
}

/// Removes every non-live entry of `extract/` by kind (FR-058) while the caller holds the registry lock. A linked or
/// non-directory `extract/` is never listed (PD-6); a failure is logged with the entry's name and skipped.
fn prune_locked(io: &dyn ExtractIo, extract: &Path, live: &BTreeSet<PathBuf>) -> usize {
    match io.kind_of(extract) {
        Ok(None) => return 0,
        Ok(Some(NodeKind::Dir)) => {}
        Ok(Some(kind)) => {
            warn!(
                target: LOG_TARGET,
                "E_EXTRACT_ESCAPE: {EXTRACT_DIR} is a {kind:?}, not a directory; nothing pruned"
            );
            return 0;
        }
        Err(e) => {
            warn!(target: LOG_TARGET, "E_EXTRACT_IO: step kind_of on {EXTRACT_DIR}: {:?}", e.kind());
            return 0;
        }
    }
    let entries = match io.list_dir(extract) {
        Ok(Some(entries)) => entries,
        Ok(None) => return 0,
        Err(e) => {
            warn!(target: LOG_TARGET, "E_EXTRACT_IO: step list_dir on {EXTRACT_DIR}: {:?}", e.kind());
            return 0;
        }
    };
    let mut removed = 0;
    for (name, kind) in entries {
        let path = extract.join(&name);
        if live.contains(&path) {
            continue;
        }
        let result = match kind {
            NodeKind::Dir => io.remove_tree(&path),
            other => io.remove_entry(&path, other),
        };
        match result {
            Ok(()) => removed += 1,
            Err(e) if e.kind() == io::ErrorKind::NotFound => {}
            Err(e) => warn!(
                target: LOG_TARGET,
                "E_EXTRACT_IO: step remove on {EXTRACT_DIR}/{} ({kind:?}): {:?}",
                name.to_string_lossy(),
                e.kind()
            ),
        }
    }
    removed
}

/// FR-058: removes every `extract/` entry that is not live; returns the count removed. Creates nothing.
pub fn prune(env: ExtractEnv<'_>) -> usize {
    let extract = env.ctx.roots.staging_dir().join(EXTRACT_DIR);
    env.ctx.roots.check_sandboxed(&extract);
    let registry = live();
    prune_locked(env.io, &extract, &registry)
}

// ---- stage (plan §4.3.10; FR-001 to FR-059) ------------------------------------------------------------------------

/// The one extraction entry point (FR-001): a staged tree of `artifact`, bound to `entry`, or the first refusal.
/// One `info!` line per call (FR-052).
pub fn stage(
    env: ExtractEnv<'_>,
    entry: &PluginEntry,
    artifact: &VerifiedArtifact,
) -> Result<StagedTree, ExtractError> {
    let result = stage_body(env, entry, artifact);
    let who = format!(
        "{} {} {}",
        artifact.name(),
        artifact.version(),
        artifact.asset().as_str()
    );
    match &result {
        Ok(tree) => info!(
            target: LOG_TARGET,
            "{who}: staged, {} listing lines",
            tree.listing.files()
        ),
        Err(e) => info!(target: LOG_TARGET, "{who}: refused, {}", e.code()),
    }
    result
}

/// FR-002, no I/O: the entry describes exactly this artifact; returns its payload and checked bundle name.
fn bind(
    entry: &PluginEntry,
    artifact: &VerifiedArtifact,
) -> Result<(Payload, PlainName), ExtractError> {
    let mismatch = |field: &'static str| ExtractError::EntryMismatch { field };
    if artifact.format() != AssetFormat::TarGz {
        return Err(mismatch("format"));
    }
    if entry.name != artifact.name() {
        return Err(mismatch("name"));
    }
    if entry.version_text != artifact.version() {
        return Err(mismatch("version"));
    }
    let release = entry.release.as_ref().ok_or_else(|| mismatch("release"))?;
    let asset = release
        .platforms
        .get(&artifact.platform())
        .ok_or_else(|| mismatch("platform"))?;
    for (field, same) in [
        ("asset", asset.asset_name == *artifact.asset()),
        ("size", asset.size == artifact.size()),
        ("sha256", asset.sha256 == *artifact.sha256()),
        ("format", asset.format == artifact.format()),
    ] {
        if !same {
            return Err(mismatch(field));
        }
    }
    let payload = asset.payload.ok_or_else(|| mismatch("payload"))?;
    let install = release
        .install
        .as_ref()
        .ok_or_else(|| mismatch("install"))?;
    let bundle = install.vst3_bundle.as_str();
    let mut components = Path::new(bundle).components();
    let one_normal = matches!(
        (components.next(), components.next()),
        (Some(Component::Normal(_)), None)
    );
    // Reader rule 8 (`CONTRACT.md:141-142`): re-checked before the name is joined to anything.
    if !one_normal
        || contract::check_install_value("vst3_bundle", &Value::String(bundle.to_owned())).is_err()
    {
        return Err(mismatch("install"));
    }
    Ok((payload, install.vst3_bundle.clone()))
}

fn stage_body(
    env: ExtractEnv<'_>,
    entry: &PluginEntry,
    artifact: &VerifiedArtifact,
) -> Result<StagedTree, ExtractError> {
    let io = env.io;
    // 1. Binding, before any I/O.
    let (payload, bundle) = bind(entry, artifact)?;
    let hex = hex_lower(artifact.sha256());
    // 2. Open, once.
    let mut archive = io
        .open_archive(artifact.path())
        .map_err(|e| io_error("open_archive", ARCHIVE, e.kind()))?;
    // 3. Pass 1 with the raw hash (D6, FR-044).
    let mut hashed = Hashed::new(&mut *archive);
    let planned = plan_archive(&mut hashed, env.limits, payload.files);
    if let Some(kind) = hashed.source_error {
        return Err(io_error("read_archive", ARCHIVE, kind));
    }
    let plan = planned?;
    if hashed.finish() != *artifact.sha256() {
        return Err(ExtractError::Changed);
    }
    // 4. Payload and layout (FR-030 to FR-033).
    check_staged_layout(&plan.listing, &payload, &bundle)?;
    // 5. Symlink support (FR-042, D4).
    if !io.supports_symlinks()
        && let Some(link) = plan
            .entries
            .iter()
            .find(|e| matches!(e.kind, PlanKind::Symlink { .. }))
    {
        return Err(ExtractError::Unsupported {
            entry: link.path.clone(),
        });
    }
    // 6. The joined bound (FR-059), through the same join as every write.
    let staging = env.ctx.roots.staging_dir();
    let mut longest = 0;
    for line in plan.listing.lines() {
        longest = longest.max(joined_len(staging, &hex, parse_line(line).path)?);
    }
    if longest > env.limits.max_joined_bytes {
        return Err(limit("joined"));
    }
    // 7. Sandbox, then prune, create and register in one critical section (PD-2). Nothing before this writes.
    let extract = staging.join(EXTRACT_DIR);
    env.ctx.roots.check_sandboxed(&extract);
    let work = {
        let mut registry = live();
        prune_locked(io, &extract, &registry);
        let work = io
            .create_work_dir(&extract, &format!("{hex}."))
            .map_err(|e| io_error("create_work_dir", EXTRACT_DIR, e.kind()))?;
        registry.insert(work.clone());
        work
    };
    let guard = LiveRoot { path: work.clone() };
    // 8 to 14; from here every error removes the work dir and returns the original error.
    let filled = fill_work_dir(
        env,
        entry,
        &plan,
        &mut *archive,
        &work,
        &bundle,
        artifact.sha256(),
    );
    match filled {
        Ok(file_hashes) => Ok(StagedTree {
            root: work,
            extract_dir: extract,
            name: artifact.name().to_owned(),
            version: artifact.version().to_owned(),
            vst3_bundle: bundle,
            listing: plan.listing,
            archive_sha256: *artifact.sha256(),
            file_hashes,
            live: guard,
        }),
        Err(e) => {
            cleanup(io, &work);
            Err(e)
        }
    }
}

/// Best effort (FR-045): a failure is one `warn!` naming the step and the kind; the leftover is a non-live
/// `extract/` entry the next prune removes.
fn cleanup(io: &dyn ExtractIo, work: &Path) {
    if let Err(e) = io.remove_tree(work)
        && e.kind() != io::ErrorKind::NotFound
    {
        warn!(
            target: LOG_TARGET,
            "E_EXTRACT_IO: step remove of the {WORK_DIR} failed: {:?}; the next prune removes it",
            e.kind()
        );
    }
}

fn inside(outer: &Path, path: &Path) -> bool {
    path != outer && path.starts_with(outer)
}

/// Steps 8 to 14 of plan §4.3.10; returns the per-file hashes.
fn fill_work_dir(
    env: ExtractEnv<'_>,
    entry: &PluginEntry,
    plan: &ArchivePlan,
    archive: &mut dyn ArchiveRead,
    work: &Path,
    bundle: &PlainName,
    sha256: &[u8; 32],
) -> Result<Vec<(String, [u8; 32])>, ExtractError> {
    let io = env.io;
    let escape = |path: &str| ExtractError::Escape {
        path: path.to_owned(),
    };
    let canonical = |path: &Path, entry: &str| {
        io.canonicalize(path)
            .map_err(|e| io_error("canonicalize", entry, e.kind()))
    };
    // 8. The work dir is a real directory whose canonical path is under the canonical staging dir (FR-040).
    match io.kind_of(work) {
        Ok(Some(NodeKind::Dir)) => {}
        Ok(_) => return Err(escape(WORK_DIR)),
        Err(e) => return Err(io_error("kind_of", WORK_DIR, e.kind())),
    }
    let cw = canonical(work, WORK_DIR)?;
    let cs = canonical(env.ctx.roots.staging_dir(), WORK_DIR)?;
    if !inside(&cs, &cw) {
        return Err(escape(WORK_DIR));
    }
    // 9. Every directory in listing order, parents first (FR-042, FR-043).
    for line in plan.listing.lines() {
        let line = parse_line(line);
        if line.kind == 'd' {
            let path = entry_path(work, line.path)?;
            io.create_dir(&path)
                .map_err(|e| io_error("create_dir", line.path, e.kind()))?;
            io.set_mode(&path, 0o755)
                .map_err(|e| io_error("set_mode", line.path, e.kind()))?;
        }
    }
    // 10. Pass 2 (FR-044, FR-056).
    archive
        .rewind()
        .map_err(|e| io_error("read_archive", ARCHIVE, e.kind()))?;
    let mut hashed = Hashed::new(archive);
    let written = write_files(io, &mut hashed, env.limits, plan, work);
    if let Some(kind) = hashed.source_error {
        return Err(io_error("read_archive", ARCHIVE, kind));
    }
    let file_hashes = written?;
    if hashed.finish() != *sha256 {
        return Err(ExtractError::Changed);
    }
    // 11. Symlinks last (FR-042).
    for e in &plan.entries {
        if let PlanKind::Symlink { target } = &e.kind {
            let link = entry_path(work, &e.path)?;
            io.symlink(target, &link)
                .map_err(|err| io_error("symlink", &e.path, err.kind()))?;
        }
    }
    // 12. The physical post-check (FR-046): the work dir canonicalised once (step 8), each path or parent once.
    for line in plan.listing.lines() {
        let line = parse_line(line);
        let path = entry_path(work, line.path)?;
        if line.kind == 'l' {
            let parent = path.parent().ok_or_else(|| escape(line.path))?;
            if !inside(&cw, &canonical(parent, line.path)?) {
                return Err(escape(line.path));
            }
            let top = line.path.split('/').next().unwrap_or_default();
            match io.canonicalize(&path) {
                Ok(resolved) if !inside(&cw.join(top), &resolved) => {
                    return Err(escape(line.path));
                }
                // A dangling in-bundle link is checked through its parent only.
                Ok(_) => {}
                Err(e) if e.kind() == io::ErrorKind::NotFound => {}
                Err(e) => return Err(io_error("canonicalize", line.path, e.kind())),
            }
        } else if !inside(&cw, &canonical(&path, line.path)?) {
            return Err(escape(line.path));
        }
    }
    // 13. The tree on disk, never following a link, equals the plan (FR-047).
    check_tree(io, work, &plan.listing)?;
    // 14. The staged version (FR-057, PD-5).
    check_version(io, work, bundle, plan, entry, env.limits)?;
    Ok(file_hashes)
}

/// Pass 2's stream: each entry compared with the plan on its header, before anything is created for it; files
/// written in `CHUNK` slices and hashed in the same loop. Any parse error or plan difference is `E_EXTRACT_CHANGED`.
fn write_files(
    io: &dyn ExtractIo,
    raw: &mut Hashed<'_>,
    limits: &ExtractLimits,
    plan: &ArchivePlan,
    work: &Path,
) -> Result<Vec<(String, [u8; 32])>, ExtractError> {
    let changed = |_| ExtractError::Changed;
    let mut tar = TarReader::new(BufReader::with_capacity(CHUNK, raw), *limits);
    let mut planned = plan.entries.iter();
    let mut hashes = Vec::new();
    while let Some(header) = tar.next_entry().map_err(changed)? {
        let accepted = accept(&header).map_err(|_| ExtractError::Changed)?;
        if let Some((_, found)) = accepted {
            if planned.next() != Some(&found) {
                return Err(ExtractError::Changed);
            }
            if let PlanKind::File { size, exec } = found.kind {
                let rel = found.path;
                let path = entry_path(work, &rel)?;
                let mut file = io
                    .create_file(&path)
                    .map_err(|e| io_error("create_file", &rel, e.kind()))?;
                let mut sha = Sha256Stream::new();
                let copied = tar.copy_data(size, &mut |chunk| {
                    file.write_all(chunk)
                        .map_err(|e| io_error("write", &rel, e.kind()))?;
                    sha.update(chunk);
                    Ok(())
                });
                match copied {
                    Ok(()) => {}
                    Err(DataError::Stream(_)) => return Err(ExtractError::Changed),
                    Err(DataError::Sink(e)) => return Err(e),
                }
                drop(file);
                io.set_mode(&path, if exec { 0o755 } else { 0o644 })
                    .map_err(|e| io_error("set_mode", &rel, e.kind()))?;
                hashes.push((rel, sha.finish()));
                continue;
            }
        }
        tar.skip_data(header.size).map_err(changed)?;
    }
    if planned.next().is_some() {
        return Err(ExtractError::Changed);
    }
    // Listing order: the listing is sorted by the path's bytes.
    hashes.sort_by(|a, b| a.0.cmp(&b.0));
    Ok(hashes)
}

/// FR-047: `read_tree` through FR-020's builder, a `None` mode taking the plan's; the first differing line names
/// the plan's line, or the longer side's first extra line.
fn check_tree(
    io: &dyn ExtractIo,
    work: &Path,
    listing: &CanonicalListing,
) -> Result<(), ExtractError> {
    let modes: BTreeMap<&str, u32> = listing
        .lines()
        .iter()
        .filter_map(|line| {
            let line = parse_line(line);
            u32::from_str_radix(line.mode, 8)
                .ok()
                .map(|mode| (line.path, mode))
        })
        .collect();
    let mut entries = io
        .read_tree(work)
        .map_err(|e| io_error("read_tree", WORK_DIR, e.kind()))?;
    for e in &mut entries {
        if e.mode.is_none() {
            e.mode = std::str::from_utf8(&e.path)
                .ok()
                .and_then(|p| modes.get(p).copied());
        }
    }
    let tree =
        CanonicalListing::from_entries(&entries).map_err(|e| ExtractError::TreeMismatch {
            line: match e {
                ExtractError::Listing { detail, .. } => detail,
                other => other.to_string(),
            },
        })?;
    let (want, got) = (listing.lines(), tree.lines());
    if want == got {
        return Ok(());
    }
    let first = want
        .iter()
        .zip(got)
        .position(|(a, b)| a != b)
        .unwrap_or(want.len().min(got.len()));
    let line = want
        .get(first)
        .or_else(|| got.get(first))
        .cloned()
        .unwrap_or_default();
    Err(ExtractError::TreeMismatch { line })
}

/// FR-057, PD-5: the staged moduleinfo, bounded, decoded, one BOM stripped, read by Phase 6's reader with the
/// relative path; its version must equal the entry's.
fn check_version(
    io: &dyn ExtractIo,
    work: &Path,
    bundle: &PlainName,
    plan: &ArchivePlan,
    entry: &PluginEntry,
    limits: &ExtractLimits,
) -> Result<(), ExtractError> {
    let rel = format!("{}/{MODULEINFO}", bundle.as_str());
    let path = entry_path(work, &rel)?;
    let planned = plan
        .entries
        .iter()
        .find_map(|e| match e.kind {
            PlanKind::File { size, .. } if e.path == rel => Some(size),
            _ => None,
        })
        .unwrap_or(0);
    if planned > limits.max_moduleinfo_bytes {
        return Err(limit("moduleinfo"));
    }
    let bytes = io
        .read_file(&path, limits.max_moduleinfo_bytes)
        .map_err(|e| io_error("read_file", &rel, e.kind()))?;
    if u64::try_from(bytes.len()).unwrap_or(u64::MAX) > limits.max_moduleinfo_bytes {
        return Err(limit("moduleinfo"));
    }
    let found = match String::from_utf8(bytes) {
        Err(_) => "unknown".to_owned(),
        Ok(text) => {
            let text = text.strip_prefix('\u{feff}').unwrap_or(&text);
            match detect::moduleinfo_version(text, Path::new(MODULEINFO)) {
                CopyVersion::Known { version, .. } if entry.version == Some(version) => {
                    return Ok(());
                }
                CopyVersion::Known { version, .. } => version.to_string(),
                CopyVersion::Unknown(_) => "unknown".to_owned(),
            }
        }
    };
    Err(ExtractError::Version {
        expected: entry.version_text.clone(),
        found,
    })
}

// ---- the result (plan §4.3.11; FR-050, FR-051, FR-054; PD-8) -------------------------------------------------------

/// A staged, physically checked tree (FR-048, FR-050): private fields, no public constructor, not `Clone`; a
/// read-only source Phase 9 copies out of (FR-054).
///
/// ```compile_fail
/// let _: krate_manager_core::extract::StagedTree = Default::default();
/// ```
/// ```compile_fail
/// fn forge() -> krate_manager_core::extract::StagedTree {
///     krate_manager_core::extract::StagedTree {
///         root: std::path::PathBuf::new(), extract_dir: std::path::PathBuf::new(),
///         name: String::new(), version: String::new(), vst3_bundle: panic!(), listing: panic!(),
///         archive_sha256: [0; 32], file_hashes: Vec::new(), live: panic!(),
///     }
/// }
/// ```
/// ```no_run
/// fn read(t: &krate_manager_core::extract::StagedTree) {
///     let _ = (t.root(), t.bundle(), t.presets(), t.name(), t.version(), t.vst3_bundle(),
///              t.listing(), t.archive_sha256(), t.file_hashes());
/// }
/// ```
#[derive(Debug)]
pub struct StagedTree {
    root: PathBuf,
    extract_dir: PathBuf,
    name: String,
    version: String,
    vst3_bundle: PlainName,
    listing: CanonicalListing,
    archive_sha256: [u8; 32],
    file_hashes: Vec<(String, [u8; 32])>,
    live: LiveRoot,
}

impl StagedTree {
    /// The work directory.
    pub fn root(&self) -> &Path {
        &self.root
    }

    /// `root/<vst3_bundle>`.
    pub fn bundle(&self) -> PathBuf {
        self.root.join(self.vst3_bundle.as_str())
    }

    /// `root/presets`.
    pub fn presets(&self) -> PathBuf {
        self.root.join("presets")
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn version(&self) -> &str {
        &self.version
    }

    pub fn vst3_bundle(&self) -> &PlainName {
        &self.vst3_bundle
    }

    pub fn listing(&self) -> &CanonicalListing {
        &self.listing
    }

    pub fn archive_sha256(&self) -> &[u8; 32] {
        &self.archive_sha256
    }

    /// One `(listing path, SHA-256 of the bytes written)` per planned file, in listing order (FR-056).
    pub fn file_hashes(&self) -> &[(String, [u8; 32])] {
        &self.file_hashes
    }
}

/// `<64 lower-case hex>.<6 alphanumerics>` (PD-8).
fn is_work_name(name: &str) -> bool {
    name.split_once('.').is_some_and(|(hex, rest)| {
        hex.len() == 64
            && hex.bytes().all(|b| matches!(b, b'0'..=b'9' | b'a'..=b'f'))
            && rest.len() == RANDOM_LEN
            && rest.bytes().all(|b| b.is_ascii_alphanumeric())
    })
}

/// FR-051: removes the tree's root after PD-8's checks (a direct child of its `extract/`, a work-dir name, the root
/// it registered and still live); a refusal removes nothing. The tree, and its registry entry, drop afterwards.
pub fn discard(tree: StagedTree, io: &dyn ExtractIo) -> Result<(), ExtractError> {
    let refuse = |reason: &'static str| Err(ExtractError::Path { reason });
    if tree.root.parent() != Some(tree.extract_dir.as_path()) {
        return refuse("the tree root is not a direct child of the extract directory");
    }
    if !tree
        .root
        .file_name()
        .and_then(OsStr::to_str)
        .is_some_and(is_work_name)
    {
        return refuse("the tree root is not a work directory name");
    }
    if tree.live.path != tree.root || !live().contains(&tree.root) {
        return refuse("the tree root is not a live staged root");
    }
    match io.remove_tree(&tree.root) {
        Err(e) if e.kind() != io::ErrorKind::NotFound => {
            Err(io_error("remove", WORK_DIR, e.kind()))
        }
        _ => Ok(()),
    }
}

#[cfg(test)]
#[path = "extract_tests.rs"]
mod tests;
