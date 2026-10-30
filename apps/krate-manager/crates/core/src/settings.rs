//! Settings schema v1 and its atomic persistence (FR-030 to FR-036, D10; plan §4.5).

use crate::roots::Roots;
use serde::{Deserialize, Serialize};
use std::io::{self, Write as _};
use std::path::{Component, Path, PathBuf};
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

#[cfg(test)]
#[path = "settings_tests.rs"]
mod tests;
