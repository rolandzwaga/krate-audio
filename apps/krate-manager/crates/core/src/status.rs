//! The status view the screen renders (plan §4.6; spec FR-040 to FR-049).

use crate::context::AppContext;
use crate::detect::{
    self, CopyFormat, CopyVersion, DetectedCopy, Detection, HostFacts, InstallerRecord, Listing,
    ListingLimits, Place, UnknownReason, VersionSource,
};
use crate::feed::{self, FeedCheck, FetchMode, PluginEntry, SnapshotSource};
use crate::install::{self, ActionGate, ActionKind, DriverResolver, InstallIo};
use crate::notes;
use crate::platform::{NativePlatform, Os, Platform};
use crate::roots::RootKind;
use crate::version::{self, Arch, Availability, CoreVersion, Installed, OsVersion, UpdateState};
use log::info;
use serde::Serialize;
use std::collections::BTreeMap;

/// The only host a release page may be opened on (FR-049).
pub const RELEASE_PAGE_HOST: &str = "github.com";
/// The only host a plugin page may be opened on (Phase 12 FR-088).
pub const PLUGIN_PAGE_HOST: &str = "krateaudio.com";
/// The longest release-page or plugin-page URL accepted, in bytes (FR-049).
pub const MAX_RELEASE_PAGE_URL_BYTES: usize = 2048;
pub(crate) const LOG_TARGET: &str = "krate_manager_core::status";

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct StatusView {
    pub os: OsName,
    pub edge_max_age_secs: u64,
    pub running_os: Option<String>,
    pub feed: FeedView,
    pub plugins: Vec<PluginView>,
    /// Phase 12 FR-052: plugins the manager installed that the feed no longer offers (Linux only).
    pub orphans: Vec<OrphanView>,
}

/// Phase 12 FR-052 (PD-13): one orphan card, named by its manifest.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct OrphanView {
    pub name: String,
    pub version: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum OsName {
    Windows,
    Macos,
    Linux,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct FeedView {
    pub source: FeedSource,
    pub fetched_at_unix: Option<u64>,
    pub schema_version: Option<u8>,
    pub error: Option<String>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum FeedSource {
    Fresh,
    Cached,
    None,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct PluginView {
    pub name: String,
    pub latest: String,
    pub availability: AvailabilityView,
    pub min_os: Option<String>,
    pub installed: InstalledView,
    pub installed_version: Option<String>,
    pub update: Option<UpdateView>,
    pub copies: Vec<CopyView>,
    pub record: Option<RecordView>,
    pub record_error: Option<String>,
    pub record_without_files: bool,
    pub record_version_differs: bool,
    pub release_page_url: Option<String>,
    /// Phase 12 FR-088: the feed's `download_url`, the plugin's krateaudio.com page.
    pub plugin_page_url: Option<String>,
    pub notes: Vec<notes::Block>,
    pub actions: Vec<ActionView>,
    /// Phase 10 FR-083: `Some(WindowsSettings)` iff the gate's Remove basis is `OutsideRoot`.
    pub remove_hint: Option<RemoveHint>,
}

/// Where a card points the user to remove a copy the manager may not remove (Phase 10 FR-083, FR-134).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RemoveHint {
    WindowsSettings,
}

/// One action a card offers (FR-100).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ActionView {
    Install,
    Update,
    Remove,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum AvailabilityView {
    Available,
    NotThroughManager,
    NotForThisMachine,
    NeedsNewerOs,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum InstalledView {
    NotChecked,
    NotInstalled,
    Elsewhere,
    VersionUnknown,
    Version,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum UpdateView {
    NotInstalled,
    UpToDate,
    UpdateAvailable,
    InstalledNewer,
    Unknown,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct CopyView {
    pub format: FormatView,
    pub place: PlaceView,
    pub vendor_folder: bool,
    pub nested: bool,
    pub path: String,
    pub managed: bool,
    pub loads_before_managed: bool,
    pub version: Option<String>,
    pub version_source: Option<VersionSourceView>,
    pub unknown_reason: Option<UnknownReasonView>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum FormatView {
    Vst3,
    Au,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PlaceView {
    Configured,
    Vst3User,
    Vst3Global,
    AuUser,
    AuGlobal,
    InstallerLocation,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum VersionSourceView {
    Classes,
    TopLevel,
    AuPlist,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum UnknownReasonView {
    SingleFile,
    /// Spelled so `rename_all = "snake_case"` yields FR-046's `no_moduleinfo` (`NoModuleInfo` would give
    /// `no_module_info`); FR-084 bans per-variant renames in this file (PD-21).
    NoModuleinfo,
    TooLarge,
    Unreadable,
    Corrupt,
    ClassesDisagree,
    NoVersion,
    NotABundle,
    AuNoVersion,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct RecordView {
    pub kind: RecordKind,
    pub version: Option<String>,
    pub files: Option<u64>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RecordKind {
    Inno,
    Pkg,
    Manifest,
}

/// FR-040: `build_with_os` for the native OS and architecture over the real filesystem.
pub fn build(ctx: &AppContext, mode: FetchMode, now_unix: u64, host: &dyn HostFacts) -> StatusView {
    build_with_os(
        NativePlatform.os(),
        Arch::native(),
        ctx,
        mode,
        now_unix,
        host,
        &install::FsInstallIo,
    )
}

/// FR-040: one feed check, one OS-version read, one listing, one detection per entry in feed order on the calling
/// thread, then `assemble_with_gates` and one `info!`. No lock, no write, no thread (the feed-cache write is Phase
/// 5's). Only when `os` has an install driver, one read-only `install::action_gate` per entry with install facts
/// (Phase 9 FR-100, PD-15). On Linux with a snapshot, one bounded manifest listing through `io` (Phase 12 FR-052).
pub fn build_with_os(
    os: Os,
    arch: Arch,
    ctx: &AppContext,
    mode: FetchMode,
    now_unix: u64,
    host: &dyn HostFacts,
    io: &dyn InstallIo,
) -> StatusView {
    let check = feed::check(ctx, mode, now_unix);
    let running = host.os_version();
    let driver_present = install::NativeDrivers.driver_for(os).is_some();
    let mut detections = BTreeMap::new();
    let mut gates = BTreeMap::new();
    if let Some(snapshot) = &check.snapshot {
        let mut names: Vec<&str> = Vec::new();
        for entry in snapshot.feed.plugins.values() {
            if let Some(facts) = entry.release.as_ref().and_then(|r| r.install.as_ref()) {
                let name = facts.vst3_bundle.as_str();
                if !names.contains(&name) {
                    names.push(name);
                }
            }
        }
        let listing = Listing::scan(ctx.roots.plugin_roots(), &names, ListingLimits::PRODUCTION);
        for (key, entry) in &snapshot.feed.plugins {
            let detection = detect::detect_plugin(os, ctx, entry, host, &listing);
            let has_install = entry.release.as_ref().is_some_and(|r| r.install.is_some());
            if driver_present && has_install {
                gates.insert(
                    key.clone(),
                    install::action_gate(os, ctx, io, entry, &detection),
                );
            }
            detections.insert(key.clone(), detection);
        }
    }
    let mut view = assemble_with_gates(
        os,
        arch,
        &check,
        running,
        &detections,
        &gates,
        driver_present,
    );
    view.orphans = orphans(os, &check, ctx, io);
    let copies: usize = view.plugins.iter().map(|p| p.copies.len()).sum();
    let source = match view.feed.source {
        FeedSource::Fresh => "fresh",
        FeedSource::Cached => "cached",
        FeedSource::None => "none",
    };
    info!(
        target: LOG_TARGET,
        "status: {} plugins, {copies} copies, feed {source}",
        view.plugins.len()
    );
    view
}

/// Phase 12 FR-052, FR-054 (PD-13): Linux only, over a snapshot; every manifest whose `plugin` is no snapshot key and
/// whose `version` is a string, sorted by name. Any `manifest_heads` error (over 256 entries among them) lists none.
fn orphans(os: Os, check: &FeedCheck, ctx: &AppContext, io: &dyn InstallIo) -> Vec<OrphanView> {
    let (Os::Linux, Some(snapshot)) = (os, &check.snapshot) else {
        return Vec::new();
    };
    let Ok(heads) = install::manifest_heads(io, ctx.roots.state_dir()) else {
        return Vec::new();
    };
    let mut orphans: Vec<OrphanView> = heads
        .into_iter()
        .filter_map(|head| {
            let name = head.plugin?;
            let version = head.version?;
            (!snapshot.feed.plugins.contains_key(&name)).then_some(OrphanView { name, version })
        })
        .collect();
    orphans.sort_by(|a, b| a.name.cmp(&b.name));
    orphans
}

/// FR-041: `assemble_with_gates` with no gate and no driver, so every card's `actions` is empty.
pub fn assemble(
    os: Os,
    arch: Arch,
    check: &FeedCheck,
    running: Option<OsVersion>,
    detections: &BTreeMap<String, Detection>,
) -> StatusView {
    assemble_with_gates(
        os,
        arch,
        check,
        running,
        detections,
        &BTreeMap::new(),
        false,
    )
}

/// FR-041: pure. The availability through `version::resolve`, the installed state of FR-043, the flags of FR-044;
/// each card's `actions` from `install::allowed_actions` over its gate, empty for an entry without one (Phase 9
/// FR-100, D13).
pub fn assemble_with_gates(
    os: Os,
    arch: Arch,
    check: &FeedCheck,
    running: Option<OsVersion>,
    detections: &BTreeMap<String, Detection>,
    gates: &BTreeMap<String, ActionGate>,
    driver_present: bool,
) -> StatusView {
    let plugins = check
        .snapshot
        .as_ref()
        .map(|snapshot| {
            snapshot
                .feed
                .plugins
                .iter()
                .map(|(key, entry)| {
                    plugin_view(
                        os,
                        arch,
                        running.as_ref(),
                        entry,
                        detections.get(key),
                        gates.get(key),
                        driver_present,
                    )
                })
                .collect()
        })
        .unwrap_or_default();
    let feed = match &check.snapshot {
        Some(snapshot) => FeedView {
            source: match snapshot.source {
                SnapshotSource::Fresh => FeedSource::Fresh,
                SnapshotSource::Cached => FeedSource::Cached,
            },
            fetched_at_unix: Some(snapshot.fetched_at_unix),
            schema_version: Some(snapshot.feed.schema_version),
            error: check.error.as_ref().map(ToString::to_string),
        },
        None => FeedView {
            source: FeedSource::None,
            fetched_at_unix: None,
            schema_version: None,
            error: check.error.as_ref().map(ToString::to_string),
        },
    };
    StatusView {
        os: os_name(os),
        edge_max_age_secs: feed::EDGE_MAX_AGE_SECS,
        running_os: running.map(|v| v.to_string()),
        feed,
        plugins,
        orphans: Vec::new(),
    }
}

/// FR-049: an `https` URL on `RELEASE_PAGE_HOST`, no user name, no password, no port, at most 2048 bytes; the
/// parser's serialisation (lower-cased host, default port dropped).
pub fn release_page_url(text: &str) -> Option<String> {
    page_url(text, RELEASE_PAGE_HOST)
}

/// Phase 12 FR-088: FR-049's rule on `PLUGIN_PAGE_HOST`.
pub fn plugin_page_url(text: &str) -> Option<String> {
    page_url(text, PLUGIN_PAGE_HOST)
}

/// What the open command opens: a release page or a plugin page (FR-049, Phase 12 FR-088).
pub fn open_url(text: &str) -> Option<String> {
    release_page_url(text).or_else(|| plugin_page_url(text))
}

fn page_url(text: &str, host: &str) -> Option<String> {
    if text.len() > MAX_RELEASE_PAGE_URL_BYTES {
        return None;
    }
    let url = url::Url::parse(text).ok()?;
    let accepted = url.scheme() == "https"
        && url.host_str().is_some_and(|h| h.eq_ignore_ascii_case(host))
        && url.username().is_empty()
        && url.password().is_none()
        && url.port().is_none();
    accepted.then(|| url.as_str().to_owned())
}

fn os_name(os: Os) -> OsName {
    match os {
        Os::Windows => OsName::Windows,
        Os::Macos => OsName::Macos,
        Os::Linux => OsName::Linux,
    }
}

/// One card. `detection` absent is `NotChecked`; `gate` absent offers no action (Phase 9 FR-100).
fn plugin_view(
    os: Os,
    arch: Arch,
    running: Option<&OsVersion>,
    entry: &PluginEntry,
    detection: Option<&Detection>,
    gate: Option<&ActionGate>,
    driver_present: bool,
) -> PluginView {
    let resolved = version::resolve(entry, os, arch, running);
    let (availability, min_os) = match &resolved {
        Availability::Available { .. } => (AvailabilityView::Available, None),
        Availability::NotThroughManager => (AvailabilityView::NotThroughManager, None),
        Availability::NotForThisMachine => (AvailabilityView::NotForThisMachine, None),
        Availability::NeedsNewerOs { min_os } => {
            (AvailabilityView::NeedsNewerOs, Some(min_os.to_string()))
        }
    };
    let update = update_state(entry, detection);
    let actions = gate
        .map(|gate| {
            install::allowed_actions(driver_present, gate, &resolved, update)
                .into_iter()
                .map(action_view)
                .collect()
        })
        .unwrap_or_default();
    let release = entry.release.as_ref();
    let mut view = PluginView {
        name: entry.name.clone(),
        latest: entry.version_text.clone(),
        availability,
        min_os,
        installed: InstalledView::NotChecked,
        installed_version: None,
        update: None,
        copies: Vec::new(),
        record: None,
        record_error: None,
        record_without_files: false,
        record_version_differs: false,
        release_page_url: release.and_then(|r| release_page_url(&r.release_notes_url)),
        plugin_page_url: entry
            .download_url
            .as_ref()
            .and_then(|u| plugin_page_url(u.as_str())),
        notes: release
            .and_then(|r| r.notes_markdown.as_deref())
            .map(notes::blocks)
            .unwrap_or_default(),
        actions,
        remove_hint: match gate {
            Some(ActionGate::Open {
                remove: install::RemoveBasis::OutsideRoot,
                ..
            }) => Some(RemoveHint::WindowsSettings),
            _ => None,
        },
    };
    let Some(Detection::Checked {
        copies,
        record,
        record_error,
        managed_rank,
    }) = detection
    else {
        return view;
    };
    let ManagedState {
        managed,
        installed,
        installed_version,
        ..
    } = managed_state(copies);
    let managed_known = managed.and_then(|c| known_version(c.version));
    let record_version = record.as_ref().and_then(record_core_version);
    view.installed = installed;
    view.installed_version = installed_version.map(|v| v.to_string());
    view.update = update.map(update_view);
    view.copies = copies.iter().map(|c| copy_view(c, *managed_rank)).collect();
    view.record = record.as_ref().map(record_view);
    view.record_error = record_error.map(str::to_owned);
    view.record_without_files = record.is_some() && managed.is_none();
    view.record_version_differs =
        matches!((record_version, managed_known), (Some(r), Some(m)) if r != m);
    view
}

/// What the managed-copy rule decides over one detection's copies (FR-043).
struct ManagedState<'a> {
    managed: Option<&'a DetectedCopy>,
    installed: InstalledView,
    installed_version: Option<CoreVersion>,
    compared: Installed,
}

/// FR-043: the managed VST3 copy decides; AU copies never do.
fn managed_state(copies: &[DetectedCopy]) -> ManagedState<'_> {
    let managed = copies
        .iter()
        .find(|c| c.format == CopyFormat::Vst3 && c.managed);
    let (installed, installed_version, compared) = match managed {
        Some(copy) => match copy.version {
            CopyVersion::Known { version, .. } => (
                InstalledView::Version,
                Some(version),
                Installed::Version(version),
            ),
            CopyVersion::Unknown(_) => (
                InstalledView::VersionUnknown,
                None,
                Installed::VersionUnknown,
            ),
        },
        None => {
            let mut others = copies
                .iter()
                .filter(|c| c.format == CopyFormat::Vst3 && !c.managed)
                .peekable();
            if others.peek().is_some() {
                let first_known = others.find_map(|c| known_version(c.version));
                (InstalledView::Elsewhere, first_known, Installed::No)
            } else {
                (InstalledView::NotInstalled, None, Installed::No)
            }
        }
    };
    ManagedState {
        managed,
        installed,
        installed_version,
        compared,
    }
}

/// The update state the card shows and the command re-checks: the managed-copy rule against the feed version; `None`
/// when the plugin was not checked (Phase 9 PD-13, D13).
pub(crate) fn update_state(
    entry: &PluginEntry,
    detection: Option<&Detection>,
) -> Option<UpdateState> {
    let Some(Detection::Checked { copies, .. }) = detection else {
        return None;
    };
    Some(version::compare(
        managed_state(copies).compared,
        entry.version,
    ))
}

/// Exhaustive, so a new action fails to compile here (Phase 9 FR-100; the PD-21 pattern).
fn action_view(action: ActionKind) -> ActionView {
    match action {
        ActionKind::Install => ActionView::Install,
        ActionKind::Update => ActionView::Update,
        ActionKind::Remove => ActionView::Remove,
    }
}

fn known_version(version: CopyVersion) -> Option<CoreVersion> {
    match version {
        CopyVersion::Known { version, .. } => Some(version),
        CopyVersion::Unknown(_) => None,
    }
}

fn update_view(state: UpdateState) -> UpdateView {
    match state {
        UpdateState::NotInstalled => UpdateView::NotInstalled,
        UpdateState::UpToDate => UpdateView::UpToDate,
        UpdateState::UpdateAvailable => UpdateView::UpdateAvailable,
        UpdateState::InstalledNewer => UpdateView::InstalledNewer,
        UpdateState::Unknown => UpdateView::Unknown,
    }
}

/// FR-044: `loads_before_managed` when both ranks exist and the copy's is lower.
fn copy_view(copy: &DetectedCopy, managed_rank: Option<u8>) -> CopyView {
    let (place, vendor_folder, nested) = match copy.place {
        Place::Root(kind) => (place_view(kind), false, false),
        Place::WindowsVendor(kind) => (place_view(kind), true, false),
        Place::InstallerLocation => (PlaceView::InstallerLocation, false, false),
        Place::Nested(kind) => (place_view(kind), false, true),
    };
    let (version, version_source, unknown_reason) = match copy.version {
        CopyVersion::Known { version, source } => (
            Some(version.to_string()),
            Some(version_source_view(source)),
            None,
        ),
        CopyVersion::Unknown(reason) => (None, None, Some(unknown_reason_view(reason))),
    };
    CopyView {
        format: match copy.format {
            CopyFormat::Vst3 => FormatView::Vst3,
            CopyFormat::Au => FormatView::Au,
        },
        place,
        vendor_folder,
        nested,
        path: copy.path.display().to_string(),
        managed: copy.managed,
        loads_before_managed: matches!((copy.rank, managed_rank), (Some(c), Some(m)) if c < m),
        version,
        version_source,
        unknown_reason,
    }
}

fn place_view(kind: RootKind) -> PlaceView {
    match kind {
        RootKind::Configured => PlaceView::Configured,
        RootKind::Vst3User => PlaceView::Vst3User,
        RootKind::Vst3Global => PlaceView::Vst3Global,
        RootKind::AuUser => PlaceView::AuUser,
        RootKind::AuGlobal => PlaceView::AuGlobal,
    }
}

fn version_source_view(source: VersionSource) -> VersionSourceView {
    match source {
        VersionSource::Classes => VersionSourceView::Classes,
        VersionSource::TopLevel => VersionSourceView::TopLevel,
        VersionSource::AuPlist => VersionSourceView::AuPlist,
    }
}

/// PD-21: exhaustive, so a new core reason fails to compile here.
fn unknown_reason_view(reason: UnknownReason) -> UnknownReasonView {
    match reason {
        UnknownReason::SingleFile => UnknownReasonView::SingleFile,
        UnknownReason::NoModuleInfo => UnknownReasonView::NoModuleinfo,
        UnknownReason::TooLarge => UnknownReasonView::TooLarge,
        UnknownReason::Unreadable => UnknownReasonView::Unreadable,
        UnknownReason::Corrupt => UnknownReasonView::Corrupt,
        UnknownReason::ClassesDisagree => UnknownReasonView::ClassesDisagree,
        UnknownReason::NoVersion => UnknownReasonView::NoVersion,
        UnknownReason::NotABundle => UnknownReasonView::NotABundle,
        UnknownReason::AuNoVersion => UnknownReasonView::AuNoVersion,
    }
}

fn record_view(record: &InstallerRecord) -> RecordView {
    match record {
        InstallerRecord::Inno { version_text, .. } => RecordView {
            kind: RecordKind::Inno,
            version: version_text.clone(),
            files: None,
        },
        InstallerRecord::Pkg {
            version_text,
            files,
            ..
        } => RecordView {
            kind: RecordKind::Pkg,
            version: Some(version_text.clone()),
            files: Some(*files),
        },
        InstallerRecord::Manifest { version_text, .. } => RecordView {
            kind: RecordKind::Manifest,
            version: Some(version_text.clone()),
            files: None,
        },
    }
}

fn record_core_version(record: &InstallerRecord) -> Option<CoreVersion> {
    match record {
        InstallerRecord::Inno { version, .. }
        | InstallerRecord::Pkg { version, .. }
        | InstallerRecord::Manifest { version, .. } => *version,
    }
}

#[cfg(test)]
#[path = "status_tests.rs"]
mod tests;
