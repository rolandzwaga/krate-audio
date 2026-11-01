//! The allowed roots (plan §4.1.6; D3, D7, D17, FR-026, FR-051; PD-5).

use crate::error::{self, HelperError};
use crate::seams::HelperIo;
use std::ffi::OsStr;
use std::io::ErrorKind;
use std::path::{Component, Path, PathBuf};

/// The last component of the manager's state dir; equal to the core's `APP_IDENTIFIER` (SC-011).
pub const STATE_DIR_NAME: &str = "com.krateaudio.krate-manager";
/// The macOS helper folder under `/Library/Application Support` (D7, PD-26).
pub const HELPER_FOLDER: &str = "com.krateaudio.krate-manager-helper";
/// The vendor folder below the plug-in and preset roots (D3).
#[cfg(any(windows, target_os = "macos"))]
const VENDOR: &str = "Krate Audio";

/// How the job's state dir `<S>` is accepted (FR-025, D17).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StateDirRule {
    UnderProfiles(PathBuf),
    SameOwnerNonRoot,
    Exact(PathBuf),
}

/// The Windows roots (D3, D7).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct WindowsRoots {
    pub vst3_vendor: PathBuf,
    pub preset_vendor: PathBuf,
    pub run_base: PathBuf,
}

/// The macOS roots (D3, D7, PD-5).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MacosRoots {
    pub vst3: PathBuf,
    pub components: PathBuf,
    pub presets: PathBuf,
    pub applications: PathBuf,
    pub volume: PathBuf,
    pub run_base: PathBuf,
    pub run_ancestors: Vec<PathBuf>,
}

/// The per-OS roots; `None` on Linux (every op refused, FR-036).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum OsRoots {
    Windows(WindowsRoots),
    Macos(MacosRoots),
    None,
}

/// Every root the helper may act under (PD-5).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AllowedRoots {
    pub os: OsRoots,
    pub state_dir: StateDirRule,
}

impl AllowedRoots {
    /// The production roots; takes no argument and reads no job, settings file or environment (FR-051, SC-007).
    /// The only `target_os` branch of the helper (PD-7).
    pub fn production() -> Result<AllowedRoots, HelperError> {
        production_roots()
    }
}

/// Windows (D3, D7, D17): the known folders, `E_HELPER_ROOTS` when a lookup fails (FR-051, tasks TA-6).
#[cfg(windows)]
fn production_roots() -> Result<AllowedRoots, HelperError> {
    use crate::os::{KnownFolder, known_folder};
    let folder = |id| known_folder(id).ok_or(error::ROOTS);
    Ok(AllowedRoots {
        os: OsRoots::Windows(WindowsRoots {
            vst3_vendor: folder(KnownFolder::ProgramFilesCommon)?
                .join("VST3")
                .join(VENDOR),
            preset_vendor: folder(KnownFolder::ProgramData)?.join(VENDOR),
            run_base: folder(KnownFolder::Windows)?.join("Temp"),
        }),
        state_dir: StateDirRule::UnderProfiles(folder(KnownFolder::UserProfiles)?),
    })
}

/// macOS (D3, D7, D17, PD-5): literals only.
#[cfg(target_os = "macos")]
fn production_roots() -> Result<AllowedRoots, HelperError> {
    let library = Path::new("/Library");
    let support = library.join("Application Support");
    let plug_ins = library.join("Audio").join("Plug-Ins");
    Ok(AllowedRoots {
        os: OsRoots::Macos(MacosRoots {
            vst3: plug_ins.join("VST3"),
            components: plug_ins.join("Components"),
            presets: support.join(VENDOR),
            applications: PathBuf::from("/Applications"),
            volume: PathBuf::from("/"),
            run_base: support.join(HELPER_FOLDER),
            run_ancestors: vec![library.to_path_buf(), support],
        }),
        state_dir: StateDirRule::SameOwnerNonRoot,
    })
}

/// Linux and every other target: no roots, every op refused (D18, FR-036).
#[cfg(not(any(windows, target_os = "macos")))]
fn production_roots() -> Result<AllowedRoots, HelperError> {
    Ok(AllowedRoots {
        os: OsRoots::None,
        state_dir: StateDirRule::SameOwnerNonRoot,
    })
}

/// The `--ci-roots` file's read bound.
#[cfg(feature = "ci-temp-roots")]
const CI_ROOTS_MAX_BYTES: u64 = 64 * 1024;

/// The Windows `--ci-roots` file (FR-052).
#[cfg(feature = "ci-temp-roots")]
#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
struct CiWindows {
    state_dir: String,
    vst3_vendor: String,
    preset_vendor: String,
}

/// The macOS `--ci-roots` file (FR-052).
#[cfg(feature = "ci-temp-roots")]
#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
struct CiMacos {
    state_dir: String,
    vst3: String,
    components: String,
    presets: String,
    applications: String,
}

/// The `--ci-roots` file where production has no roots (Linux): the state dir only.
#[cfg(feature = "ci-temp-roots")]
#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
struct CiNone {
    state_dir: String,
}

/// The roots a `--ci-roots` file names (FR-052): the plug-in, preset and application roots and the state dir
/// (`Exact`); `run_base`, `run_ancestors` and `volume` stay production (PD-5). Every path must be absolute and its
/// canonical form (FR-026) must lie strictly under the canonical directory holding the file; the paths are returned
/// as spelled, so a linked spelling reaches `run`'s own canonicalization. Any defect is `E_HELPER_ROOTS`.
#[cfg(feature = "ci-temp-roots")]
pub fn from_ci_file(io: &dyn HelperIo, file: &Path) -> Result<AllowedRoots, HelperError> {
    let bytes = io
        .read_bounded(file, CI_ROOTS_MAX_BYTES)
        .map_err(|_| error::ROOTS)?;
    if bytes.len() as u64 > CI_ROOTS_MAX_BYTES {
        return Err(error::ROOTS);
    }
    let dir = file
        .parent()
        .filter(|d| !d.as_os_str().is_empty())
        .ok_or(error::ROOTS)?;
    let dir = io.canonicalize(dir).map_err(|_| error::ROOTS)?;
    let bounded = |spelled: String| -> Result<PathBuf, HelperError> {
        let path = PathBuf::from(spelled);
        let canon = canonical_path(io, &path)?;
        if canon != dir && canon.starts_with(&dir) {
            Ok(path)
        } else {
            Err(error::ROOTS)
        }
    };
    let (os, state_dir) = match AllowedRoots::production()?.os {
        OsRoots::Windows(w) => {
            let f: CiWindows = serde_json::from_slice(&bytes).map_err(|_| error::ROOTS)?;
            let os = OsRoots::Windows(WindowsRoots {
                vst3_vendor: bounded(f.vst3_vendor)?,
                preset_vendor: bounded(f.preset_vendor)?,
                run_base: w.run_base,
            });
            (os, f.state_dir)
        }
        OsRoots::Macos(m) => {
            let f: CiMacos = serde_json::from_slice(&bytes).map_err(|_| error::ROOTS)?;
            let os = OsRoots::Macos(MacosRoots {
                vst3: bounded(f.vst3)?,
                components: bounded(f.components)?,
                presets: bounded(f.presets)?,
                applications: bounded(f.applications)?,
                volume: m.volume,
                run_base: m.run_base,
                run_ancestors: m.run_ancestors,
            });
            (os, f.state_dir)
        }
        OsRoots::None => {
            let f: CiNone = serde_json::from_slice(&bytes).map_err(|_| error::ROOTS)?;
            (OsRoots::None, f.state_dir)
        }
    };
    Ok(AllowedRoots {
        os,
        state_dir: StateDirRule::Exact(bounded(state_dir)?),
    })
}

/// `AllowedRoots` with every plug-in, preset and application root and the state-dir path in canonical form
/// (FR-026). `run_base`, `run_ancestors` and `volume` stay as given: D7 builds the run directory from the known
/// folder or the literal and never canonicalizes it (its plain form goes onto the installer's argv, FR-027), and the
/// ancestors are `lstat`ed, never followed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CanonicalRoots {
    pub roots: AllowedRoots,
}

/// Each root canonicalized once; a missing root as its deepest existing ancestor plus the plain missing components
/// (FR-026); failure `E_HELPER_ROOTS`.
pub fn canonical(io: &dyn HelperIo, roots: &AllowedRoots) -> Result<CanonicalRoots, HelperError> {
    let canon = |p: &Path| canonical_path(io, p);
    let os = match &roots.os {
        OsRoots::Windows(w) => OsRoots::Windows(WindowsRoots {
            vst3_vendor: canon(&w.vst3_vendor)?,
            preset_vendor: canon(&w.preset_vendor)?,
            run_base: w.run_base.clone(),
        }),
        OsRoots::Macos(m) => OsRoots::Macos(MacosRoots {
            vst3: canon(&m.vst3)?,
            components: canon(&m.components)?,
            presets: canon(&m.presets)?,
            applications: canon(&m.applications)?,
            volume: m.volume.clone(),
            run_base: m.run_base.clone(),
            run_ancestors: m.run_ancestors.clone(),
        }),
        OsRoots::None => OsRoots::None,
    };
    let state_dir = match &roots.state_dir {
        StateDirRule::UnderProfiles(p) => StateDirRule::UnderProfiles(canon(p)?),
        StateDirRule::SameOwnerNonRoot => StateDirRule::SameOwnerNonRoot,
        StateDirRule::Exact(d) => StateDirRule::Exact(canon(d)?),
    };
    Ok(CanonicalRoots {
        roots: AllowedRoots { os, state_dir },
    })
}

/// FR-026 for one path: absolute, no `..` / `.` component, then the canonical form of its longest existing ancestor
/// with the missing components re-joined. Only `NotFound` walks up; any other error, or no existing ancestor, is
/// `E_HELPER_ROOTS`.
fn canonical_path(io: &dyn HelperIo, path: &Path) -> Result<PathBuf, HelperError> {
    let plain = path
        .components()
        .all(|c| !matches!(c, Component::ParentDir | Component::CurDir));
    if !path.is_absolute() || !plain {
        return Err(error::ROOTS);
    }
    let mut existing = path;
    let mut missing: Vec<&OsStr> = Vec::new();
    loop {
        match io.canonicalize(existing) {
            Ok(canon) => return Ok(missing.iter().rev().fold(canon, |p, c| p.join(c))),
            Err(e) if e.kind() == ErrorKind::NotFound => {
                let (Some(name), Some(parent)) = (existing.file_name(), existing.parent()) else {
                    return Err(error::ROOTS);
                };
                missing.push(name);
                existing = parent;
            }
            Err(_) => return Err(error::ROOTS),
        }
    }
}

#[cfg(test)]
#[path = "roots_tests.rs"]
mod tests;
