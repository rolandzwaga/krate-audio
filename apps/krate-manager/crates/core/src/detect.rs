//! Detection of installed copies and installer records (plan §4.5; spec FR-001 to FR-036).

use crate::context::AppContext;
use crate::feed::{InstallFacts, PlainName, PluginEntry};
use crate::platform::{HostIo, NativePlatform, Os, Platform, RegistryRead, RunOutcome, VENDOR_DIR};
use crate::roots::{PluginRoot, RootKind};
use crate::version::{self, CoreVersion, OsVersion};
use log::{debug, warn};
use serde_json::Value;
use std::borrow::Cow;
use std::collections::VecDeque;
use std::ffi::{OsStr, OsString};
use std::fs::{File, Metadata};
use std::io::{ErrorKind, Read};
use std::path::{Component, Path, PathBuf};

/// The moduleinfo bound, also the AU plist bound (FR-011, FR-020).
pub const MAX_MODULEINFO_BYTES: u64 = 1024 * 1024;
/// The Linux manifest bound (FR-028).
pub const MAX_MANIFEST_BYTES: u64 = 4 * 1024 * 1024;
/// The manifest folder under the state dir (FR-028).
pub const MANIFESTS_DIR: &str = "manifests";
/// The listing depth below each VST3 root (FR-006).
pub const LISTING_MAX_DEPTH: usize = 4;
/// The listing entry cap per root (FR-006).
pub const LISTING_MAX_ENTRIES: usize = 20_000;
pub(crate) const LOG_TARGET: &str = "krate_manager_core::detect";
/// The longest `InstallLocation` accepted, in UTF-16 units (FR-003).
const MAX_INSTALL_LOCATION_UNITS: usize = 32_767;

// Log and record codes (plan §3); `RecordRead::Failed` carries the record ones (FR-025).
pub(crate) const E_DETECT_MODULEINFO: &str = "E_DETECT_MODULEINFO";
pub(crate) const E_DETECT_CLASSES: &str = "E_DETECT_CLASSES";
pub(crate) const E_DETECT_TOP_LEVEL: &str = "E_DETECT_TOP_LEVEL";
#[cfg(target_os = "macos")]
pub(crate) const E_DETECT_PLIST: &str = "E_DETECT_PLIST";
pub(crate) const E_DETECT_LISTING_CAP: &str = "E_DETECT_LISTING_CAP";
pub(crate) const E_RECORD_READ: &str = "E_RECORD_READ";
pub(crate) const E_RECORD_TIMEOUT: &str = "E_RECORD_TIMEOUT";
pub(crate) const E_RECORD_PARSE: &str = "E_RECORD_PARSE";
pub(crate) const E_RECORD_FUTURE: &str = "E_RECORD_FUTURE";
pub(crate) const E_RECORD_MISMATCH: &str = "E_RECORD_MISMATCH";
pub(crate) const E_RECORD_LOCATION: &str = "E_RECORD_LOCATION";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CopyFormat {
    Vst3,
    Au,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Place {
    Root(RootKind),
    WindowsVendor(RootKind),
    InstallerLocation,
    Nested(RootKind),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Candidate {
    pub format: CopyFormat,
    pub place: Place,
    pub path: PathBuf,
    pub managed: bool,
    pub rank: Option<u8>,
}

/// One listing match; `root_index` indexes `plugin_roots` (PD-2).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ListedCopy {
    pub root: RootKind,
    pub root_index: usize,
    pub path: PathBuf,
}

pub struct CandidateInputs<'a> {
    pub plugin_roots: &'a [PluginRoot],
    pub facts: &'a InstallFacts,
    pub windows_install_location: Option<&'a Path>,
    pub linux_managed_root: &'a Path,
    pub nested: &'a [ListedCopy],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VersionSource {
    Classes,
    TopLevel,
    AuPlist,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UnknownReason {
    SingleFile,
    NoModuleInfo,
    TooLarge,
    Unreadable,
    Corrupt,
    ClassesDisagree,
    NoVersion,
    NotABundle,
    AuNoVersion,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CopyVersion {
    Known {
        version: CoreVersion,
        source: VersionSource,
    },
    Unknown(UnknownReason),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DetectedCopy {
    pub format: CopyFormat,
    pub place: Place,
    pub path: PathBuf,
    pub managed: bool,
    pub rank: Option<u8>,
    pub version: CopyVersion,
}

/// A host record read (FR-025); `Failed` carries its code.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RecordRead<T> {
    Absent,
    Found(T),
    Failed(&'static str),
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct InnoValues {
    pub display_version: Option<String>,
    pub install_location: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PkgOutputs {
    pub pkg_info: String,
    pub files: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PkgInfo {
    pub version_text: String,
    pub location: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum InstallerRecord {
    Inno {
        version_text: Option<String>,
        version: Option<CoreVersion>,
        install_location: Option<PathBuf>,
    },
    Pkg {
        version_text: String,
        version: Option<CoreVersion>,
        location: Option<String>,
        files: u64,
    },
    Manifest {
        version_text: String,
        version: Option<CoreVersion>,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Detection {
    NotChecked,
    Checked {
        copies: Vec<DetectedCopy>,
        record: Option<InstallerRecord>,
        record_error: Option<&'static str>,
        /// The managed VST3 candidate's rank (SI-13).
        managed_rank: Option<u8>,
    },
}

/// D7 layer 3: what detection and status consume.
pub trait HostFacts {
    fn os_version(&self) -> Option<OsVersion>;
    fn inno_values(&self, key: &PlainName) -> RecordRead<InnoValues>;
    fn pkg_outputs(&self, pkg_id: &PlainName) -> RecordRead<PkgOutputs>;
}

/// The one production `HostFacts`: delegates to `NativePlatform` with `io` (FR-025).
pub struct PlatformFacts<'a> {
    pub io: &'a dyn HostIo,
}

impl HostFacts for PlatformFacts<'_> {
    fn os_version(&self) -> Option<OsVersion> {
        NativePlatform.os_version(self.io)
    }

    fn inno_values(&self, key: &PlainName) -> RecordRead<InnoValues> {
        NativePlatform.inno_values(self.io, key)
    }

    fn pkg_outputs(&self, pkg_id: &PlainName) -> RecordRead<PkgOutputs> {
        NativePlatform.pkg_outputs(self.io, pkg_id)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ListingLimits {
    pub max_depth: usize,
    pub max_entries: usize,
}

impl ListingLimits {
    pub const PRODUCTION: ListingLimits = ListingLimits {
        max_depth: LISTING_MAX_DEPTH,
        max_entries: LISTING_MAX_ENTRIES,
    };
}

/// The bounded listing of the VST3 roots (FR-006, plan §4.5.4): every entry named like a checked entry's bundle,
/// in root order, then breadth first, then file-name byte order.
#[derive(Clone, Debug, Default)]
pub struct Listing {
    matches: Vec<ListedMatch>,
}

#[derive(Clone, Debug)]
struct ListedMatch {
    root: RootKind,
    root_index: usize,
    name: OsString,
    path: PathBuf,
}

impl Listing {
    /// Lists each `Configured`, `Vst3User` and `Vst3Global` root once (never an AU root). `names` is de-duplicated
    /// by the caller.
    pub fn scan(roots: &[PluginRoot], names: &[&str], limits: ListingLimits) -> Listing {
        let mut matches = Vec::new();
        for (index, root) in roots.iter().enumerate() {
            if matches!(
                root.kind,
                RootKind::Configured | RootKind::Vst3User | RootKind::Vst3Global
            ) {
                list_root(index, root, names, limits, &mut matches);
            }
        }
        Listing { matches }
    }

    /// This plugin's matches in scan order, filtered with the same name rule `scan` used (PD-3), never an exact
    /// compare of the on-disk name.
    pub fn copies_of(&self, bundle: &str) -> Vec<ListedCopy> {
        self.matches
            .iter()
            .filter(|m| name_matches(&m.name, bundle))
            .map(|m| ListedCopy {
                root: m.root,
                root_index: m.root_index,
                path: m.path.clone(),
            })
            .collect()
    }

    #[cfg(test)]
    pub(crate) fn paths(&self) -> Vec<&Path> {
        self.matches.iter().map(|m| m.path.as_path()).collect()
    }
}

/// FR-006: ASCII case-insensitive on a Windows host, exact elsewhere (the host's filesystem is listed, PD-3).
fn name_matches(name: &OsStr, wanted: &str) -> bool {
    let name = name.as_encoded_bytes();
    if cfg!(windows) {
        name.eq_ignore_ascii_case(wanted.as_bytes())
    } else {
        name == wanted.as_bytes()
    }
}

/// The one directory listing of the phase (FR-006, PD-17): breadth first from the root (depth 0), each directory
/// read whole and sorted by name bytes, at most `max_entries` entries examined, nothing below `max_depth` read. A
/// matched entry is not entered; a directory is entered only when its `DirEntry` type is a real directory (a
/// symlink or, on Windows, any name-surrogate reparse point is not); a directory that cannot be read is skipped.
fn list_root(
    index: usize,
    root: &PluginRoot,
    names: &[&str],
    limits: ListingLimits,
    out: &mut Vec<ListedMatch>,
) {
    let mut queue = VecDeque::from([(root.path.clone(), 0usize)]);
    let mut examined = 0usize;
    let mut deepest = 0usize;
    let mut capped = false;
    'walk: while let Some((dir, depth)) = queue.pop_front() {
        deepest = deepest.max(depth);
        let Ok(reader) = std::fs::read_dir(&dir) else {
            continue;
        };
        let mut entries: Vec<_> = reader.filter_map(Result::ok).collect();
        entries.sort_by_cached_key(|e| e.file_name().into_encoded_bytes());
        for entry in entries {
            if examined == limits.max_entries {
                capped = true;
                break 'walk;
            }
            examined += 1;
            let child_depth = depth + 1;
            let name = entry.file_name();
            if names.iter().any(|wanted| name_matches(&name, wanted)) {
                out.push(ListedMatch {
                    root: root.kind,
                    root_index: index,
                    name,
                    path: entry.path(),
                });
            } else if child_depth < limits.max_depth && entry.file_type().is_ok_and(|t| t.is_dir())
            {
                queue.push_back((entry.path(), child_depth));
            }
        }
    }
    debug!(target: LOG_TARGET, "listing: {:?}: {examined} entries examined", root.kind);
    if capped {
        warn!(
            target: LOG_TARGET,
            "{E_DETECT_LISTING_CAP} {:?}: stopped at {} entries, depth {deepest}",
            root.kind,
            limits.max_entries
        );
    }
}

/// D11: `Vst3User` 0; the n-th `Vst3Global` root in `plugin_roots` order n (1-based); anything else none.
fn rank_of(roots: &[PluginRoot], index: usize) -> Option<u8> {
    let root = roots.get(index)?;
    match root.kind {
        RootKind::Vst3User => Some(0),
        RootKind::Vst3Global => {
            let n = roots
                .iter()
                .take(index + 1)
                .filter(|r| r.kind == RootKind::Vst3Global)
                .count();
            Some(u8::try_from(n).unwrap_or(u8::MAX))
        }
        RootKind::Configured | RootKind::AuUser | RootKind::AuGlobal => None,
    }
}

fn vst3(place: Place, path: PathBuf, rank: Option<u8>) -> Candidate {
    Candidate {
        format: CopyFormat::Vst3,
        place,
        path,
        managed: false,
        rank,
    }
}

/// FR-001: the fixed candidate table for `os`, then the nested copies of FR-006 not already a candidate (PD-1).
/// Pure: no filesystem, no environment.
pub fn candidates(os: Os, inputs: &CandidateInputs<'_>) -> Vec<Candidate> {
    let roots = inputs.plugin_roots;
    let bundle = inputs.facts.vst3_bundle.as_str();
    let stem = bundle.strip_suffix(".vst3").unwrap_or(bundle);
    let mut out = Vec::new();
    match os {
        Os::Windows => {
            for (i, root) in roots.iter().enumerate() {
                if matches!(root.kind, RootKind::Vst3User | RootKind::Vst3Global) {
                    let rank = rank_of(roots, i);
                    out.push(vst3(Place::Root(root.kind), root.path.join(bundle), rank));
                    out.push(vst3(
                        Place::WindowsVendor(root.kind),
                        root.path.join(VENDOR_DIR).join(stem).join(bundle),
                        rank,
                    ));
                }
            }
            let managed = match inputs.windows_install_location {
                Some(loc) => {
                    let target = loc.join(bundle);
                    match out.iter().position(|c| same_path(&c.path, &target)) {
                        Some(i) => Some(i),
                        None => {
                            out.push(vst3(Place::InstallerLocation, target, None));
                            Some(out.len() - 1)
                        }
                    }
                }
                None => out
                    .iter()
                    .position(|c| c.place == Place::WindowsVendor(RootKind::Vst3Global)),
            };
            if let Some(c) = managed.and_then(|i| out.get_mut(i)) {
                c.managed = true;
            }
        }
        Os::Macos => {
            for (i, root) in roots.iter().enumerate() {
                if matches!(root.kind, RootKind::Vst3User | RootKind::Vst3Global) {
                    out.push(vst3(
                        Place::Root(root.kind),
                        root.path.join(bundle),
                        rank_of(roots, i),
                    ));
                }
            }
            if let Some(component) = &inputs.facts.au_component {
                for root in roots {
                    if matches!(root.kind, RootKind::AuUser | RootKind::AuGlobal) {
                        out.push(Candidate {
                            format: CopyFormat::Au,
                            place: Place::Root(root.kind),
                            path: root.path.join(component.as_str()),
                            managed: false,
                            rank: None,
                        });
                    }
                }
            }
            for (format, kind) in [
                (CopyFormat::Vst3, RootKind::Vst3Global),
                (CopyFormat::Au, RootKind::AuGlobal),
            ] {
                if let Some(c) = out
                    .iter_mut()
                    .find(|c| c.format == format && c.place == Place::Root(kind))
                {
                    c.managed = true;
                }
            }
        }
        Os::Linux => {
            let mut managed = None;
            for (i, root) in roots.iter().enumerate() {
                if matches!(
                    root.kind,
                    RootKind::Configured | RootKind::Vst3User | RootKind::Vst3Global
                ) {
                    if managed.is_none() && root.path == inputs.linux_managed_root {
                        managed = Some(out.len());
                    }
                    out.push(vst3(
                        Place::Root(root.kind),
                        root.path.join(bundle),
                        rank_of(roots, i),
                    ));
                }
            }
            debug_assert!(
                managed.is_some(),
                "the Linux managed root {} is not a plug-in root",
                inputs.linux_managed_root.display()
            );
            if let Some(c) = managed.and_then(|i| out.get_mut(i)) {
                c.managed = true;
            }
        }
    }
    for copy in inputs.nested {
        let seen = |c: &Candidate| match os {
            Os::Windows => same_path(&c.path, &copy.path),
            Os::Macos | Os::Linux => c.path == copy.path,
        };
        if out.iter().any(seen) {
            continue;
        }
        out.push(vst3(
            Place::Nested(copy.root),
            copy.path.clone(),
            rank_of(roots, copy.root_index),
        ));
    }
    out
}

/// FR-001: Windows path equality whichever host runs it: the same number of components, each pair equal under
/// ASCII case folding of their encoded bytes.
pub fn same_path(a: &Path, b: &Path) -> bool {
    a.components().count() == b.components().count()
        && a.components().zip(b.components()).all(|(x, y)| {
            x.as_os_str()
                .as_encoded_bytes()
                .eq_ignore_ascii_case(y.as_os_str().as_encoded_bytes())
        })
}

/// FR-003: trailing `\` and `/` trimmed; absolute; at least one normal component; no `..`; no NUL; at most 32,767
/// UTF-16 units.
pub fn usable_install_location(text: &str) -> Option<PathBuf> {
    if !location_loggable(text) {
        return None;
    }
    let path = Path::new(text.trim_end_matches(['\\', '/']));
    let mut normal = false;
    for component in path.components() {
        match component {
            Component::ParentDir => return None,
            Component::Normal(_) => normal = true,
            Component::Prefix(_) | Component::RootDir | Component::CurDir => {}
        }
    }
    (normal && path.is_absolute()).then(|| path.to_path_buf())
}

/// No NUL and within the length bound: the only `InstallLocation` values a log line may carry (FR-003).
fn location_loggable(text: &str) -> bool {
    !text.contains('\0') && text.encode_utf16().count() <= MAX_INSTALL_LOCATION_UNITS
}

/// FR-004: `None` when nothing is at the path; otherwise the copy, read by FR-010 (VST3) or FR-020 (AU). An AU
/// candidate off macOS is never probed (PD-7).
pub fn probe(candidate: &Candidate) -> Option<DetectedCopy> {
    let path = candidate.path.as_path();
    let version = match candidate.format {
        CopyFormat::Vst3 => match stat(path)? {
            Ok(meta) => vst3_version(path, &meta),
            Err(unreadable) => unreadable,
        },
        CopyFormat::Au => au_version(path)?,
    };
    Some(DetectedCopy {
        format: candidate.format,
        place: candidate.place,
        path: candidate.path.clone(),
        managed: candidate.managed,
        rank: candidate.rank,
        version,
    })
}

/// FR-004: `None` for `NotFound`; any other metadata error is an unreadable copy with one warn (symlinks
/// followed).
fn stat(path: &Path) -> Option<Result<Metadata, CopyVersion>> {
    match std::fs::metadata(path) {
        Ok(meta) => Some(Ok(meta)),
        Err(e) if e.kind() == ErrorKind::NotFound => None,
        Err(e) => {
            warn!(
                target: LOG_TARGET,
                "{E_DETECT_MODULEINFO} {}: metadata error: {}",
                path.display(),
                e.kind()
            );
            Some(Err(CopyVersion::Unknown(UnknownReason::Unreadable)))
        }
    }
}

/// FR-010, FR-011: a regular file is the single-file format; a directory is read through its moduleinfo.
fn vst3_version(bundle: &Path, meta: &Metadata) -> CopyVersion {
    if meta.is_file() {
        return CopyVersion::Unknown(UnknownReason::SingleFile);
    }
    if !meta.is_dir() {
        warn!(target: LOG_TARGET, "{E_DETECT_MODULEINFO} {}: not a file or directory", bundle.display());
        return CopyVersion::Unknown(UnknownReason::Unreadable);
    }
    let path = bundle
        .join("Contents")
        .join("Resources")
        .join("moduleinfo.json");
    let bytes = match read_bounded(&path, MAX_MODULEINFO_BYTES) {
        Ok(bytes) => bytes,
        Err(ReadFail::NotFound) => return CopyVersion::Unknown(UnknownReason::NoModuleInfo),
        Err(ReadFail::TooLarge) => {
            warn!(target: LOG_TARGET, "{E_DETECT_MODULEINFO} {}: too large", path.display());
            return CopyVersion::Unknown(UnknownReason::TooLarge);
        }
        Err(ReadFail::Io) => {
            warn!(target: LOG_TARGET, "{E_DETECT_MODULEINFO} {}: unreadable", path.display());
            return CopyVersion::Unknown(UnknownReason::Unreadable);
        }
    };
    let Ok(text) = String::from_utf8(bytes) else {
        warn!(target: LOG_TARGET, "{E_DETECT_MODULEINFO} {}: not UTF-8", path.display());
        return CopyVersion::Unknown(UnknownReason::Corrupt);
    };
    moduleinfo_version(text.strip_prefix('\u{feff}').unwrap_or(&text), &path)
}

#[cfg(target_os = "macos")]
fn au_version(bundle: &Path) -> Option<CopyVersion> {
    let meta = match stat(bundle)? {
        Ok(meta) => meta,
        Err(unreadable) => return Some(unreadable),
    };
    if meta.is_file() {
        return Some(CopyVersion::Unknown(UnknownReason::NotABundle));
    }
    if !meta.is_dir() {
        warn!(target: LOG_TARGET, "{E_DETECT_PLIST} {}: not a file or directory", bundle.display());
        return Some(CopyVersion::Unknown(UnknownReason::Unreadable));
    }
    Some(read_au_plist(bundle))
}

/// PD-7: off macOS an AU candidate is never probed (no copy, no log); production roots hold none there.
#[cfg(not(target_os = "macos"))]
fn au_version(_bundle: &Path) -> Option<CopyVersion> {
    None
}

enum ReadFail {
    NotFound,
    TooLarge,
    Io,
}

/// PD-22: the file type is checked before the open (a FIFO, socket or device is never opened), then at most
/// `max + 1` bytes are read.
fn read_bounded(path: &Path, max: u64) -> Result<Vec<u8>, ReadFail> {
    let fail = |e: std::io::Error| {
        if e.kind() == ErrorKind::NotFound {
            ReadFail::NotFound
        } else {
            ReadFail::Io
        }
    };
    let meta = std::fs::metadata(path).map_err(fail)?;
    if !meta.is_file() {
        return Err(ReadFail::Io);
    }
    let file = File::open(path).map_err(fail)?;
    let mut bytes = Vec::new();
    file.take(max.saturating_add(1))
        .read_to_end(&mut bytes)
        .map_err(|_| ReadFail::Io)?;
    if u64::try_from(bytes.len()).unwrap_or(u64::MAX) > max {
        return Err(ReadFail::TooLarge);
    }
    Ok(bytes)
}

/// FR-012 (as amended by SI-16): drops each `,` outside a string that is followed, after JSON whitespace, by `}`
/// or `]`, and preceded, ignoring JSON whitespace, by something other than `[`, `{` or `,`. Nothing else changes.
pub fn strip_trailing_commas(text: &str) -> Cow<'_, str> {
    let is_ws = |b: u8| matches!(b, b' ' | b'\t' | b'\n' | b'\r');
    let bytes = text.as_bytes();
    let mut dropped = Vec::new();
    let mut in_string = false;
    let mut escape = false;
    let mut last: Option<u8> = None;
    for (i, &b) in bytes.iter().enumerate() {
        if in_string {
            if escape {
                escape = false;
            } else if b == b'\\' {
                escape = true;
            } else if b == b'"' {
                in_string = false;
                last = Some(b);
            }
            continue;
        }
        match b {
            b'"' => in_string = true,
            b',' => {
                let next = bytes
                    .get(i + 1..)
                    .and_then(|rest| rest.iter().copied().find(|&c| !is_ws(c)));
                let closes = matches!(next, Some(b'}' | b']'));
                let after_element = !matches!(last, None | Some(b'[' | b'{' | b','));
                if closes && after_element {
                    dropped.push(i);
                } else {
                    last = Some(b);
                }
            }
            _ if is_ws(b) => {}
            _ => last = Some(b),
        }
    }
    if dropped.is_empty() {
        return Cow::Borrowed(text);
    }
    let mut out = String::with_capacity(text.len());
    let mut start = 0;
    for i in dropped {
        // Each dropped byte is an ASCII comma, so both slice ends are char boundaries.
        out.push_str(&text[start..i]);
        start = i + 1;
    }
    out.push_str(&text[start..]);
    Cow::Owned(out)
}

/// FR-012, FR-013: the version a parsed moduleinfo gives, with its logs. Only line and column of a parse error are
/// logged (plan §3), never the error's text.
pub(crate) fn moduleinfo_version(text: &str, path: &Path) -> CopyVersion {
    let doc: Value = match serde_json::from_str(&strip_trailing_commas(text)) {
        Ok(doc) => doc,
        Err(e) => {
            warn!(
                target: LOG_TARGET,
                "{E_DETECT_MODULEINFO} {}: parse error at line {} column {}",
                path.display(),
                e.line(),
                e.column()
            );
            return CopyVersion::Unknown(UnknownReason::Corrupt);
        }
    };
    let Some(root) = doc.as_object() else {
        warn!(target: LOG_TARGET, "{E_DETECT_MODULEINFO} {}: root not an object", path.display());
        return CopyVersion::Unknown(UnknownReason::Corrupt);
    };
    // (a) every class object with a `Version` key contributes it; a non-string value is unparseable.
    let class_values: Vec<&Value> = root
        .get("Classes")
        .and_then(Value::as_array)
        .map(|classes| {
            classes
                .iter()
                .filter_map(|c| c.as_object()?.get("Version"))
                .collect()
        })
        .unwrap_or_default();
    let top = root.get("Version");
    // (c) no class version: the top-level string.
    if class_values.is_empty() {
        return match top
            .and_then(Value::as_str)
            .and_then(version::reduce_class_version)
        {
            Some(version) => CopyVersion::Known {
                version,
                source: VersionSource::TopLevel,
            },
            None => CopyVersion::Unknown(UnknownReason::NoVersion),
        };
    }
    // (b) all class versions reduce, and to the same version.
    let reduced: Vec<Option<CoreVersion>> = class_values
        .iter()
        .map(|v| v.as_str().and_then(version::reduce_class_version))
        .collect();
    let agreed = match reduced.first() {
        Some(Some(first)) if reduced.iter().all(|r| *r == Some(*first)) => Some(*first),
        _ => None,
    };
    let Some(version) = agreed else {
        let shown: Vec<&str> = class_values
            .iter()
            .map(|v| v.as_str().unwrap_or("<non-string>"))
            .collect();
        warn!(
            target: LOG_TARGET,
            "{E_DETECT_CLASSES} {}: class versions disagree: {}",
            path.display(),
            shown.join(", ")
        );
        return CopyVersion::Unknown(UnknownReason::ClassesDisagree);
    };
    // (d) the top-level value never changes the result; a mismatch is logged.
    if let Some(top) = top {
        match top.as_str() {
            Some(t) if version::reduce_class_version(t) == Some(version) => {}
            Some("0.8.0") => debug!(
                target: LOG_TARGET,
                "{E_DETECT_TOP_LEVEL} {}: top-level 0.8.0, classes {version}",
                path.display()
            ),
            Some(t) => warn!(
                target: LOG_TARGET,
                "{E_DETECT_TOP_LEVEL} {}: top-level {t}, classes {version}",
                path.display()
            ),
            None => warn!(
                target: LOG_TARGET,
                "{E_DETECT_TOP_LEVEL} {}: top-level <non-string>, classes {version}",
                path.display()
            ),
        }
    }
    CopyVersion::Known {
        version,
        source: VersionSource::Classes,
    }
}

/// FR-020: the AU bundle's `Contents/Info.plist` (XML or binary), bounded like a moduleinfo; only
/// `CFBundleShortVersionString` is read, and only a three-part version is reported (ruled Q6).
#[cfg(target_os = "macos")]
fn read_au_plist(bundle: &Path) -> CopyVersion {
    let path = bundle.join("Contents").join("Info.plist");
    let bytes = match read_bounded(&path, MAX_MODULEINFO_BYTES) {
        Ok(bytes) => bytes,
        Err(ReadFail::TooLarge) => {
            warn!(target: LOG_TARGET, "{E_DETECT_PLIST} {}: too large", path.display());
            return CopyVersion::Unknown(UnknownReason::TooLarge);
        }
        Err(ReadFail::NotFound) => {
            warn!(target: LOG_TARGET, "{E_DETECT_PLIST} {}: missing", path.display());
            return CopyVersion::Unknown(UnknownReason::Unreadable);
        }
        Err(ReadFail::Io) => {
            warn!(target: LOG_TARGET, "{E_DETECT_PLIST} {}: unreadable", path.display());
            return CopyVersion::Unknown(UnknownReason::Unreadable);
        }
    };
    let Ok(value) = plist::Value::from_reader(std::io::Cursor::new(bytes)) else {
        warn!(target: LOG_TARGET, "{E_DETECT_PLIST} {}: parse error", path.display());
        return CopyVersion::Unknown(UnknownReason::Corrupt);
    };
    let Some(root) = value.as_dictionary() else {
        warn!(target: LOG_TARGET, "{E_DETECT_PLIST} {}: root not a dictionary", path.display());
        return CopyVersion::Unknown(UnknownReason::Corrupt);
    };
    match root
        .get("CFBundleShortVersionString")
        .and_then(plist::Value::as_string)
        .and_then(CoreVersion::parse)
    {
        Some(version) => CopyVersion::Known {
            version,
            source: VersionSource::AuPlist,
        },
        None => CopyVersion::Unknown(UnknownReason::AuNoVersion),
    }
}

/// FR-026 (pure): `DisplayVersion` through `CoreVersion::parse`, `InstallLocation` through FR-003.
pub fn inno_record(values: &InnoValues) -> InstallerRecord {
    InstallerRecord::Inno {
        version_text: values.display_version.clone(),
        version: values
            .display_version
            .as_deref()
            .and_then(CoreVersion::parse),
        install_location: values
            .install_location
            .as_deref()
            .and_then(usable_install_location),
    }
}

/// FR-027: `key: value` lines; `package-id` must be `expected` and `version` present; `location` kept; every other
/// key ignored.
pub fn parse_pkg_info(text: &str, expected: &PlainName) -> Result<PkgInfo, &'static str> {
    let mut id = None;
    let mut version_text = None;
    let mut location = None;
    for line in text.lines() {
        match line.split_once(": ") {
            Some(("package-id", v)) => id = Some(v),
            Some(("version", v)) => version_text = Some(v),
            Some(("location", v)) => location = Some(v),
            _ => {}
        }
    }
    if id != Some(expected.as_str()) {
        return Err(E_RECORD_PARSE);
    }
    Ok(PkgInfo {
        version_text: version_text.ok_or(E_RECORD_PARSE)?.to_owned(),
        location: location.map(str::to_owned),
    })
}

/// FR-027: the non-empty lines of `pkgutil --files` (a trailing `\r` is not content).
pub fn count_pkg_files(text: &str) -> u64 {
    let n = text.lines().filter(|line| !line.is_empty()).count();
    u64::try_from(n).unwrap_or(u64::MAX)
}

/// FR-027: the receipt record of both `pkgutil` outputs.
pub fn pkg_record(
    outputs: &PkgOutputs,
    expected: &PlainName,
) -> Result<InstallerRecord, &'static str> {
    let info = parse_pkg_info(&outputs.pkg_info, expected)?;
    Ok(InstallerRecord::Pkg {
        version: CoreVersion::parse(&info.version_text),
        version_text: info.version_text,
        location: info.location,
        files: count_pkg_files(&outputs.files),
    })
}

/// The three-key manifest check of `read_manifest` over bytes already read (Phase 9 PD-14).
pub(crate) fn parse_manifest(bytes: &[u8], plugin: &str) -> RecordRead<InstallerRecord> {
    let Ok(doc) = serde_json::from_slice::<Value>(bytes) else {
        return RecordRead::Failed(E_RECORD_PARSE);
    };
    let Some(root) = doc.as_object() else {
        return RecordRead::Failed(E_RECORD_PARSE);
    };
    match root.get("manifest_version").and_then(Value::as_u64) {
        Some(1) => {}
        Some(n) if n > 1 => return RecordRead::Failed(E_RECORD_FUTURE),
        _ => return RecordRead::Failed(E_RECORD_PARSE),
    }
    match root.get("plugin").and_then(Value::as_str) {
        Some(p) if p == plugin => {}
        Some(_) => return RecordRead::Failed(E_RECORD_MISMATCH),
        None => return RecordRead::Failed(E_RECORD_PARSE),
    }
    match root.get("version").and_then(Value::as_str) {
        Some(text) => RecordRead::Found(InstallerRecord::Manifest {
            version_text: text.to_owned(),
            version: CoreVersion::parse(text),
        }),
        None => RecordRead::Failed(E_RECORD_PARSE),
    }
}

/// FR-028 (as amended by SI-18): `<state_dir>/manifests/<stem>.json`, bounded to 4 MiB.
pub fn read_manifest(state_dir: &Path, stem: &str, plugin: &str) -> RecordRead<InstallerRecord> {
    let path = state_dir.join(MANIFESTS_DIR).join(format!("{stem}.json"));
    let bytes = match read_bounded(&path, MAX_MANIFEST_BYTES) {
        Ok(bytes) => bytes,
        Err(ReadFail::NotFound) => return RecordRead::Absent,
        Err(ReadFail::TooLarge | ReadFail::Io) => return RecordRead::Failed(E_RECORD_READ),
    };
    parse_manifest(&bytes, plugin)
}

/// FR-034.
pub fn inno_read_from(read: RegistryRead) -> RecordRead<InnoValues> {
    match read {
        RegistryRead::NoKey => RecordRead::Absent,
        RegistryRead::Values([display_version, install_location]) => {
            RecordRead::Found(InnoValues {
                display_version,
                install_location,
            })
        }
        RegistryRead::Failed => RecordRead::Failed(E_RECORD_READ),
    }
}

/// FR-034: `files` runs only after a good `--pkg-info`.
pub fn pkg_read_from(
    info: RunOutcome,
    files: impl FnOnce() -> RunOutcome,
) -> RecordRead<PkgOutputs> {
    let pkg_info = match info {
        RunOutcome::Output(text) => text,
        RunOutcome::NoOutput => return RecordRead::Absent,
        RunOutcome::Abandoned => return RecordRead::Failed(E_RECORD_TIMEOUT),
    };
    match files() {
        RunOutcome::Output(files) => RecordRead::Found(PkgOutputs { pkg_info, files }),
        RunOutcome::Abandoned => RecordRead::Failed(E_RECORD_TIMEOUT),
        RunOutcome::NoOutput => RecordRead::Failed(E_RECORD_READ),
    }
}

/// FR-034.
pub fn os_version_from_run(
    run: RunOutcome,
    parse: fn(&str) -> Option<OsVersion>,
) -> Option<OsVersion> {
    match run {
        RunOutcome::Output(text) => parse(&text),
        RunOutcome::NoOutput | RunOutcome::Abandoned => None,
    }
}

/// FR-035: the trimmed `sw_vers -productVersion` line.
pub fn parse_product_version(text: &str) -> Option<OsVersion> {
    OsVersion::parse(text.trim())
}

/// FR-035: the trimmed `getconf GNU_LIBC_VERSION` text, `glibc <version>`.
pub fn parse_glibc_version(text: &str) -> Option<OsVersion> {
    text.trim()
        .strip_prefix("glibc ")
        .and_then(OsVersion::parse)
}

/// FR-035: `major.minor.build`.
pub fn windows_version(major: u32, minor: u32, build: u32) -> Option<OsVersion> {
    OsVersion::parse(&format!("{major}.{minor}.{build}"))
}

/// FR-029, FR-042: the record first (its `InstallLocation` feeds FR-001), then every candidate probed in order.
pub fn detect_plugin(
    os: Os,
    ctx: &AppContext,
    entry: &PluginEntry,
    host: &dyn HostFacts,
    listing: &Listing,
) -> Detection {
    let Some(facts) = entry.release.as_ref().and_then(|r| r.install.as_ref()) else {
        return Detection::NotChecked;
    };
    let bundle = facts.vst3_bundle.as_str();
    let stem = bundle.strip_suffix(".vst3").unwrap_or(bundle);
    let mut raw_location = None;
    let read: Result<Option<InstallerRecord>, &'static str> = match os {
        Os::Windows => match &facts.windows_uninstall_key {
            None => Ok(None),
            Some(key) => match host.inno_values(key) {
                RecordRead::Absent => Ok(None),
                RecordRead::Failed(code) => Err(code),
                RecordRead::Found(values) => {
                    raw_location = values.install_location.clone();
                    Ok(Some(inno_record(&values)))
                }
            },
        },
        Os::Macos => match &facts.macos_pkg_id {
            None => Ok(None),
            Some(id) => match host.pkg_outputs(id) {
                RecordRead::Absent => Ok(None),
                RecordRead::Failed(code) => Err(code),
                RecordRead::Found(outputs) => pkg_record(&outputs, id).map(Some),
            },
        },
        Os::Linux => match read_manifest(ctx.roots.state_dir(), stem, &entry.name) {
            RecordRead::Absent => Ok(None),
            RecordRead::Failed(code) => Err(code),
            RecordRead::Found(record) => Ok(Some(record)),
        },
    };
    let (record, record_error) = match read {
        Ok(record) => (record, None),
        Err(code) => {
            warn!(target: LOG_TARGET, "{code} {}: installer record", entry.name);
            (None, Some(code))
        }
    };
    let install_location = match &record {
        Some(InstallerRecord::Inno {
            install_location, ..
        }) => install_location.clone(),
        _ => None,
    };
    // PD-6: a refused value is logged here, where the plugin is known; NUL or over-long values are not logged.
    if let Some(raw) = &raw_location
        && install_location.is_none()
    {
        if location_loggable(raw) {
            warn!(
                target: LOG_TARGET,
                "{E_RECORD_LOCATION} {}: install location refused: {raw}",
                entry.name
            );
        } else {
            warn!(target: LOG_TARGET, "{E_RECORD_LOCATION} {}: install location refused", entry.name);
        }
    }
    // FR-005: a fake record naming a real folder panics here, before anything is stat'ed.
    if let Some(loc) = &install_location {
        ctx.roots.check_sandboxed(loc);
    }
    let nested = listing.copies_of(bundle);
    let all = candidates(
        os,
        &CandidateInputs {
            plugin_roots: ctx.roots.plugin_roots(),
            facts,
            windows_install_location: install_location.as_deref(),
            linux_managed_root: ctx.settings.linux_plugin_root_or_default(&ctx.roots),
            nested: &nested,
        },
    );
    let managed_rank = all
        .iter()
        .find(|c| c.managed && c.format == CopyFormat::Vst3)
        .and_then(|c| c.rank);
    Detection::Checked {
        copies: all.iter().filter_map(probe).collect(),
        record,
        record_error,
        managed_rank,
    }
}

#[cfg(test)]
#[path = "detect_tests.rs"]
mod tests;
