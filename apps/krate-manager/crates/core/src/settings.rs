//! Settings schema v1 and its atomic persistence (FR-030 to FR-036, D10; plan §4.5).

use crate::context::{self, AppContext, AppState, SharedState, StartupError};
use crate::extract::NodeKind;
use crate::install::{self, InstallIo as _};
use crate::platform::{NativePlatform, Os, Platform as _};
use crate::roots::Roots;
use crate::status::OsName;
use crate::{detect, download, self_update};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::io::{self, Write as _};
use std::path::{Component, Path, PathBuf};
use std::sync::{Condvar, Mutex, MutexGuard, PoisonError, RwLockReadGuard};
use tempfile::NamedTempFile;

pub const SETTINGS_FILE: &str = "settings.json";
pub const SCHEMA_VERSION: u64 = 1;
const TEMP_PREFIX: &str = ".settings.json.";

/// The exact key set of a v1 file, in file order (PD-7).
const V1_KEYS: [&str; 3] = [
    "schema_version",
    "linux_plugin_root",
    "linux_graphics_workaround",
];

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Settings {
    pub linux_plugin_root: Option<PathBuf>,
    pub linux_graphics_workaround: bool,
}

impl Settings {
    /// The stored path, or the `Vst3User` root when none is stored.
    pub fn linux_plugin_root_or_default<'a>(&'a self, roots: &'a Roots) -> &'a Path {
        self.linux_plugin_root
            .as_deref()
            .unwrap_or_else(|| roots.vst3_user())
    }
}

/// The IO step a save failed at (FR-034). `SyncDir` runs on Unix only.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SettingsStep {
    ReadExisting,
    CreateDir,
    CreateTemp,
    Write,
    SyncTemp,
    Persist,
    SyncDir,
}

impl SettingsStep {
    pub fn as_str(self) -> &'static str {
        match self {
            SettingsStep::ReadExisting => "read-existing",
            SettingsStep::CreateDir => "create-dir",
            SettingsStep::CreateTemp => "create-temp",
            SettingsStep::Write => "write",
            SettingsStep::SyncTemp => "sync-temp",
            SettingsStep::Persist => "persist",
            SettingsStep::SyncDir => "sync-dir",
        }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum SettingsError {
    #[error("E_SETTINGS_CORRUPT: {detail}")]
    Corrupt { detail: String },
    #[error("E_SETTINGS_FUTURE_VERSION: schema_version {found} is newer than {SCHEMA_VERSION}")]
    FutureVersion { found: u64 },
    #[error("E_SETTINGS_INVALID: {reason}")]
    Invalid { reason: &'static str },
    #[error("E_SETTINGS_PATH_NOT_UTF8: linux_plugin_root is not valid UTF-8")]
    NotUtf8,
    #[error("E_SETTINGS_IO: write failed at {}: {source}", .op.as_str())]
    Io {
        op: SettingsStep,
        #[source]
        source: io::Error,
    },
}

impl SettingsError {
    pub fn code(&self) -> &'static str {
        match self {
            SettingsError::Corrupt { .. } => "E_SETTINGS_CORRUPT",
            SettingsError::FutureVersion { .. } => "E_SETTINGS_FUTURE_VERSION",
            SettingsError::Invalid { .. } => "E_SETTINGS_INVALID",
            SettingsError::NotUtf8 => "E_SETTINGS_PATH_NOT_UTF8",
            SettingsError::Io { .. } => "E_SETTINGS_IO",
        }
    }
}

fn invalid(reason: &'static str) -> SettingsError {
    SettingsError::Invalid { reason }
}

fn corrupt(detail: impl Into<String>) -> SettingsError {
    SettingsError::Corrupt {
        detail: detail.into(),
    }
}

/// UTF-8 first (TA-7), then empty, relative, `..`, filesystem root (PD-26). Lexical only.
pub fn validate(settings: &Settings) -> Result<(), SettingsError> {
    let Some(root) = &settings.linux_plugin_root else {
        return Ok(());
    };
    if root.to_str().is_none() {
        return Err(SettingsError::NotUtf8);
    }
    if root.as_os_str().is_empty() {
        return Err(invalid("linux_plugin_root is empty"));
    }
    if !root.is_absolute() {
        return Err(invalid("linux_plugin_root is relative"));
    }
    if root.components().any(|c| c == Component::ParentDir) {
        return Err(invalid("linux_plugin_root is not normalised"));
    }
    if !root.components().any(|c| matches!(c, Component::Normal(_))) {
        return Err(invalid("linux_plugin_root is a filesystem root"));
    }
    Ok(())
}

#[derive(Debug)]
pub struct LoadOutcome {
    pub settings: Settings,
    pub error: Option<SettingsError>,
}

/// FR-032: reads `roots.state_dir()/settings.json` and never writes. No file → defaults
/// and no error; any rejection → defaults and the error, the file untouched. Stray
/// `.settings.json.*` temps are never looked at.
pub fn load(roots: &Roots) -> LoadOutcome {
    let path = roots.state_dir().join(SETTINGS_FILE);
    let result = match StdSettingsIo.read_existing(&path) {
        Ok(None) => Ok(Settings::default()),
        Ok(Some(bytes)) => classify(&bytes),
        Err(source) => Err(SettingsError::Io {
            op: SettingsStep::ReadExisting,
            source,
        }),
    };
    match result {
        Ok(settings) => LoadOutcome {
            settings,
            error: None,
        },
        Err(error) => LoadOutcome {
            settings: Settings::default(),
            error: Some(error),
        },
    }
}

/// FR-031: `save_with` over the real filesystem.
pub fn save(roots: &Roots, settings: &Settings) -> Result<(), SettingsError> {
    save_with(&mut StdSettingsIo, roots, settings)
}

/// The on-disk shape; field order is key order (FR-030).
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct SettingsFileV1 {
    schema_version: u64,
    linux_plugin_root: Option<String>,
    linux_graphics_workaround: bool,
}

/// PD-8 order: not JSON / not an object → `Corrupt`; `schema_version` missing or not an
/// integer → `Corrupt`; `> 1` → `FutureVersion` (before any other field is looked at);
/// `< 1` → `Invalid`; `1` → key set (PD-7), typed parse, path check.
pub(crate) fn classify(bytes: &[u8]) -> Result<Settings, SettingsError> {
    let value: serde_json::Value =
        serde_json::from_slice(bytes).map_err(|e| corrupt(e.to_string()))?;
    let Some(object) = value.as_object() else {
        return Err(corrupt("not a JSON object"));
    };
    let Some(version) = object.get("schema_version") else {
        return Err(corrupt("schema_version is missing"));
    };
    match (version.as_u64(), version.as_i64()) {
        (Some(found), _) if found > SCHEMA_VERSION => {
            return Err(SettingsError::FutureVersion { found });
        }
        (Some(SCHEMA_VERSION), _) => {}
        (Some(_), _) | (None, Some(_)) => return Err(invalid("schema_version below 1")),
        (None, None) => return Err(corrupt("schema_version is not an integer")),
    }
    if object.len() != V1_KEYS.len() || !V1_KEYS.iter().all(|k| object.contains_key(*k)) {
        return Err(corrupt(
            "the keys are not exactly schema_version, linux_plugin_root, linux_graphics_workaround",
        ));
    }
    let file: SettingsFileV1 = serde_json::from_value(value).map_err(|e| corrupt(e.to_string()))?;
    let settings = Settings {
        linux_plugin_root: file.linux_plugin_root.map(PathBuf::from),
        linux_graphics_workaround: file.linux_graphics_workaround,
    };
    validate(&settings)?;
    Ok(settings)
}

/// The D10 bytes: `serde_json` pretty printer (two-space indent) plus a final LF.
pub(crate) fn encode(settings: &Settings) -> Result<Vec<u8>, SettingsError> {
    let linux_plugin_root = match &settings.linux_plugin_root {
        Some(p) => Some(p.to_str().ok_or(SettingsError::NotUtf8)?.to_owned()),
        None => None,
    };
    let file = SettingsFileV1 {
        schema_version: SCHEMA_VERSION,
        linux_plugin_root,
        linux_graphics_workaround: settings.linux_graphics_workaround,
    };
    let mut bytes = serde_json::to_vec_pretty(&file).map_err(|e| corrupt(e.to_string()))?;
    bytes.push(b'\n');
    Ok(bytes)
}

/// Every filesystem step of `save_with`, each a fault point (FR-034). Steps that hold the
/// temp file take it by value and hand it back (PD-9).
pub(crate) trait SettingsIo {
    /// `NotFound` → `Ok(None)`.
    fn read_existing(&mut self, path: &Path) -> io::Result<Option<Vec<u8>>>;
    fn create_dir(&mut self, dir: &Path) -> io::Result<()>;
    fn create_temp(&mut self, dir: &Path) -> io::Result<NamedTempFile>;
    fn write(&mut self, tmp: NamedTempFile, bytes: &[u8]) -> io::Result<NamedTempFile>;
    fn sync_temp(&mut self, tmp: NamedTempFile) -> io::Result<NamedTempFile>;
    fn persist(&mut self, tmp: NamedTempFile, target: &Path) -> io::Result<()>;
    /// Persist does not sync the directory (tempfile docs), so Unix does it here.
    #[cfg(unix)]
    fn sync_dir(&mut self, dir: &Path) -> io::Result<()>;
}

pub(crate) struct StdSettingsIo;

impl SettingsIo for StdSettingsIo {
    fn read_existing(&mut self, path: &Path) -> io::Result<Option<Vec<u8>>> {
        match std::fs::read(path) {
            Ok(bytes) => Ok(Some(bytes)),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e),
        }
    }

    fn create_dir(&mut self, dir: &Path) -> io::Result<()> {
        std::fs::create_dir_all(dir)
    }

    fn create_temp(&mut self, dir: &Path) -> io::Result<NamedTempFile> {
        tempfile::Builder::new()
            .prefix(TEMP_PREFIX)
            .tempfile_in(dir)
    }

    fn write(&mut self, mut tmp: NamedTempFile, bytes: &[u8]) -> io::Result<NamedTempFile> {
        tmp.write_all(bytes)?;
        Ok(tmp)
    }

    fn sync_temp(&mut self, tmp: NamedTempFile) -> io::Result<NamedTempFile> {
        tmp.as_file().sync_all()?;
        Ok(tmp)
    }

    fn persist(&mut self, tmp: NamedTempFile, target: &Path) -> io::Result<()> {
        tmp.persist(target).map(|_| ()).map_err(|e| e.error)
    }

    #[cfg(unix)]
    fn sync_dir(&mut self, dir: &Path) -> io::Result<()> {
        std::fs::File::open(dir)?.sync_all()
    }
}

fn at(op: SettingsStep) -> impl FnOnce(io::Error) -> SettingsError {
    move |source| SettingsError::Io { op, source }
}

/// FR-031, FR-034, FR-036: validate → read existing → refuse over a newer file →
/// create dir → create temp → write → sync temp → persist → (Unix) sync dir.
pub(crate) fn save_with(
    io: &mut dyn SettingsIo,
    roots: &Roots,
    settings: &Settings,
) -> Result<(), SettingsError> {
    validate(settings)?;
    let dir = roots.state_dir();
    let target = dir.join(SETTINGS_FILE);
    let existing = io
        .read_existing(&target)
        .map_err(at(SettingsStep::ReadExisting))?;
    if let Some(bytes) = existing
        && let Err(SettingsError::FutureVersion { found }) = classify(&bytes)
    {
        return Err(SettingsError::FutureVersion { found });
    }
    let bytes = encode(settings)?;
    io.create_dir(dir).map_err(at(SettingsStep::CreateDir))?;
    let tmp = io.create_temp(dir).map_err(at(SettingsStep::CreateTemp))?;
    let tmp = io.write(tmp, &bytes).map_err(at(SettingsStep::Write))?;
    let tmp = io.sync_temp(tmp).map_err(at(SettingsStep::SyncTemp))?;
    io.persist(tmp, &target)
        .map_err(at(SettingsStep::Persist))?;
    #[cfg(unix)]
    io.sync_dir(dir).map_err(at(SettingsStep::SyncDir))?;
    Ok(())
}

// ---- the settings commands and the root gate (Phase 12 plan §4.4; FR-040 to FR-044; PD-9 to PD-11) ---------------

/// What the settings section shows (FR-040): the effective Linux plug-in root as text, whether it is the default, the
/// graphics toggle, the plugins that lock the root (FR-041) and the startup settings error's text (SI-17).
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct SettingsView {
    pub os: OsName,
    pub linux_plugin_root: String,
    pub root_is_default: bool,
    pub graphics_workaround: bool,
    pub root_locked_by: Vec<String>,
    pub error: Option<String>,
}

/// Why a settings command was refused (FR-041 to FR-044, PD-10). Every `Display` begins with its code.
#[derive(Debug, thiserror::Error)]
pub enum SettingsCommandError {
    #[error("E_SETTINGS_ROOT_IN_USE: Remove {} before changing the plug-in folder", .names.join(", "))]
    RootInUse { names: Vec<String> },
    #[error(
        "E_SETTINGS_BUSY: an install, download or self-update is running; try again when it finishes"
    )]
    Busy,
    #[error("E_SETTINGS_UNSUPPORTED_OS: settings exist on Linux only")]
    UnsupportedOs,
    #[error(transparent)]
    Settings(#[from] SettingsError),
    #[error(transparent)]
    Startup(StartupError),
}

impl SettingsCommandError {
    /// The code the `Display` begins with; the inner code for the two wrappers.
    pub fn code(&self) -> &'static str {
        match self {
            SettingsCommandError::RootInUse { .. } => "E_SETTINGS_ROOT_IN_USE",
            SettingsCommandError::Busy => "E_SETTINGS_BUSY",
            SettingsCommandError::UnsupportedOs => "E_SETTINGS_UNSUPPORTED_OS",
            SettingsCommandError::Settings(e) => e.code(),
            SettingsCommandError::Startup(e) => e.code(),
        }
    }
}

/// The `settings_view` command on this OS (FR-040).
pub fn view(state: &SharedState) -> Result<SettingsView, SettingsCommandError> {
    view_with_os(NativePlatform.os(), state)
}

/// `view` for a forced OS (plan §4.4): the stored values read under the read guard, then the root lock (FR-041),
/// computed even when nothing changes. Off Linux the view is still returned.
pub(crate) fn view_with_os(
    os: Os,
    state: &SharedState,
) -> Result<SettingsView, SettingsCommandError> {
    let (state_dir, view) = {
        let guard = read(state);
        let ctx = guard
            .context
            .as_ref()
            .map_err(|e| SettingsCommandError::Startup(e.clone()))?;
        let view = SettingsView {
            os: os_name(os),
            linux_plugin_root: ctx
                .settings
                .linux_plugin_root_or_default(&ctx.roots)
                .to_string_lossy()
                .into_owned(),
            root_is_default: ctx.settings.linux_plugin_root.is_none(),
            graphics_workaround: ctx.settings.linux_graphics_workaround,
            root_locked_by: Vec::new(),
            error: guard.settings_error.as_ref().map(ToString::to_string),
        };
        (ctx.roots.state_dir().to_path_buf(), view)
    };
    Ok(SettingsView {
        root_locked_by: root_locked_by(&state_dir),
        ..view
    })
}

/// The `settings_save` command on this OS (FR-041 to FR-043, PD-11).
pub fn save_for_command(
    state: &SharedState,
    root: Option<String>,
    graphics: bool,
) -> Result<SettingsView, SettingsCommandError> {
    save_for_command_with_os(NativePlatform.os(), state, root, graphics)
}

/// `save_for_command` for a forced OS (PD-11's eleven steps, in order). Steps 4 to 8 make no write, so a refusal at
/// any step leaves the file and the running context unchanged.
pub(crate) fn save_for_command_with_os(
    os: Os,
    state: &SharedState,
    root: Option<String>,
    graphics: bool,
) -> Result<SettingsView, SettingsCommandError> {
    // (1) Linux only.
    if !matches!(os, Os::Linux) {
        return Err(SettingsCommandError::UnsupportedOs);
    }
    // (2) The state dir, or the startup error (SI-15).
    let state_dir = state_dir_of(state).map_err(SettingsCommandError::Startup)?;
    // (3) The save gate: no plugin command holds a ticket and no other save runs, until this returns.
    let Some(_gate) = SaveGate::try_enter(&state_dir) else {
        return Err(SettingsCommandError::Busy);
    };
    // (4) The authoritative context, read under the gate.
    let ctx = read(state)
        .context
        .clone()
        .map_err(SettingsCommandError::Startup)?;
    // (5) Nothing running (FR-042): the self-update gate, a busy key, a download in flight.
    if something_runs(&ctx) {
        return Err(SettingsCommandError::Busy);
    }
    // (6) The value to store.
    let next = Settings {
        linux_plugin_root: root.map(PathBuf::from),
        linux_graphics_workaround: graphics,
    };
    // (7) The root lock, only when the root changes, compared as stored values (FR-041, FR-043).
    if next.linux_plugin_root != ctx.settings.linux_plugin_root {
        let names = root_locked_by(&state_dir);
        if !names.is_empty() {
            return Err(SettingsCommandError::RootInUse { names });
        }
    }
    // (8) to (10) Validate, write atomically (refusing over a newer file), swap the context in.
    validate(&next)?;
    save(&ctx.roots, &next)?;
    context::apply_settings_with_os(os, state, next);
    // (11) What the section shows now.
    view_with_os(os, state)
}

fn read(state: &SharedState) -> RwLockReadGuard<'_, AppState> {
    state.read().unwrap_or_else(PoisonError::into_inner)
}

/// The context's state dir, or a clone of the startup error; the read guard is dropped on return.
fn state_dir_of(state: &SharedState) -> Result<PathBuf, StartupError> {
    match &read(state).context {
        Ok(ctx) => Ok(ctx.roots.state_dir().to_path_buf()),
        Err(e) => Err(e.clone()),
    }
}

fn os_name(os: Os) -> OsName {
    match os {
        Os::Windows => OsName::Windows,
        Os::Macos => OsName::Macos,
        Os::Linux => OsName::Linux,
    }
}

/// FR-042 (PD-11 step 5): the manager updating itself, a plugin action, or a download of this instance in flight (the
/// in-flight path of `self_update::self_update_busy`).
fn something_runs(ctx: &AppContext) -> bool {
    let state_dir = ctx.roots.state_dir();
    self_update::action_gate(state_dir).is_err()
        || install::any_busy(state_dir)
        || download::any_in_flight(&ctx.roots.staging_dir().join(download::DOWNLOADS_DIR))
}

/// FR-041: the manifests' `plugin` values (a manifest without one named by its stem), then the stems of the pending
/// journals, sorted and de-duplicated; an unreadable directory is named by itself, so it still locks the root.
fn root_locked_by(state_dir: &Path) -> Vec<String> {
    let io = &install::FsInstallIo;
    let mut names: Vec<String> = match install::manifest_heads(io, state_dir) {
        Ok(heads) => heads
            .into_iter()
            .map(|head| head.plugin.unwrap_or(head.stem))
            .collect(),
        Err(_) => vec![detect::MANIFESTS_DIR.to_owned()],
    };
    let journal = state_dir.join(install::JOURNAL_DIR);
    let entries = match io.kind_of(&journal) {
        Ok(None) => Ok(Vec::new()),
        Ok(Some(NodeKind::Dir)) => io.read_dir(&journal).map_err(|_| ()),
        Ok(Some(_)) | Err(_) => Err(()),
    };
    match entries {
        Ok(entries) => names.extend(entries.into_iter().filter_map(|(name, kind)| {
            let name = name.into_string().ok()?;
            let stem = name.strip_suffix(".json")?;
            (kind == NodeKind::File && !stem.is_empty()).then(|| stem.to_owned())
        })),
        Err(()) => names.push(install::JOURNAL_DIR.to_owned()),
    }
    names.sort();
    names.dedup();
    names
}

/// PD-11: per state dir, the plugin commands holding a ticket and whether a save runs.
#[derive(Default)]
struct Gate {
    commands: usize,
    saving: bool,
}

/// PD-11: the root gate of every state dir of this process, keyed as `install`'s busy registry is; an idle gate is
/// removed.
static ROOT_GATE: Mutex<BTreeMap<PathBuf, Gate>> = Mutex::new(BTreeMap::new());
/// PD-11: signalled whenever a gate changes, so a waiting `command_context` re-checks.
static ROOT_GATE_FREE: Condvar = Condvar::new();

fn root_gate() -> MutexGuard<'static, BTreeMap<PathBuf, Gate>> {
    ROOT_GATE.lock().unwrap_or_else(PoisonError::into_inner)
}

/// Applies `change` to `state_dir`'s gate, removes it when idle, then wakes every waiter.
fn release_gate(state_dir: &Path, change: impl FnOnce(&mut Gate)) {
    let mut gates = root_gate();
    if let Some(gate) = gates.get_mut(state_dir) {
        change(gate);
        if gate.commands == 0 && !gate.saving {
            gates.remove(state_dir);
        }
    }
    drop(gates);
    ROOT_GATE_FREE.notify_all();
}

/// PD-11: a plugin command's hold on its state dir's root gate, from before its context clone until its core call
/// returns; a settings save is `E_SETTINGS_BUSY` while one is held.
#[must_use]
pub struct RootTicket {
    state_dir: PathBuf,
}

impl Drop for RootTicket {
    fn drop(&mut self) {
        release_gate(&self.state_dir, |gate| {
            gate.commands = gate.commands.saturating_sub(1);
        });
    }
}

/// PD-11: a ticket on the root gate (waiting out a running save), then the context clone.
pub fn command_context(state: &SharedState) -> Result<(RootTicket, AppContext), StartupError> {
    // (i) The state dir; the read guard is dropped before the gate is waited on.
    let state_dir = state_dir_of(state)?;
    // (ii) Wait out a running save, then count this command in.
    let gates = root_gate();
    let mut gates = ROOT_GATE_FREE
        .wait_while(gates, |gates| {
            gates.get(&state_dir).is_some_and(|gate| gate.saving)
        })
        .unwrap_or_else(PoisonError::into_inner);
    gates.entry(state_dir.clone()).or_default().commands += 1;
    drop(gates);
    // Built only after the registry lock is released: its `Drop` locks the registry again.
    let ticket = RootTicket { state_dir };
    // (iii) The context, cloned under the ticket (an `Err` drops the ticket).
    let ctx = read(state).context.clone()?;
    Ok((ticket, ctx))
}

/// PD-11 step 3: a running save of one state dir; dropping it (also while unwinding) ends the save.
struct SaveGate {
    state_dir: PathBuf,
}

impl SaveGate {
    /// `None` while a plugin command holds a ticket or another save runs. The guard is built only after the registry
    /// lock is released: its `Drop` locks the registry again.
    fn try_enter(state_dir: &Path) -> Option<SaveGate> {
        let mut gates = root_gate();
        let gate = gates.entry(state_dir.to_path_buf()).or_default();
        let free = gate.commands == 0 && !gate.saving;
        if free {
            gate.saving = true;
        }
        drop(gates);
        free.then(|| SaveGate {
            state_dir: state_dir.to_path_buf(),
        })
    }
}

impl Drop for SaveGate {
    fn drop(&mut self) {
        release_gate(&self.state_dir, |gate| gate.saving = false);
    }
}

/// PD-11: a test hold of a state dir's save gate (a save in progress); dropping it releases the gate.
#[cfg(any(test, feature = "test-support"))]
pub struct SaveGateHeldForTest {
    _gate: SaveGate,
}

/// PD-11: `state_dir`'s save gate held as a running save (tests only), as `install::hold_busy_for_test`.
#[cfg(any(test, feature = "test-support"))]
pub fn hold_save_gate_for_test(state_dir: &Path) -> SaveGateHeldForTest {
    SaveGateHeldForTest {
        _gate: SaveGate::try_enter(state_dir).expect("the save gate is not free"),
    }
}

#[cfg(test)]
#[path = "settings_tests.rs"]
mod tests;
