//! The version model: core versions, installed-versus-available, platform resolution (FR-030 to FR-038;
//! plan §4.4).

use crate::feed::{PlatformAsset, PlatformKey, PluginEntry};
use crate::platform::{Os, Platform};
use std::cmp::Ordering;
use std::fmt;

/// `MAJOR.MINOR.PATCH`; the derived order is field order (FR-030).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct CoreVersion {
    pub major: u32,
    pub minor: u32,
    pub patch: u32,
}

/// One core part: `0` or `[1-9][0-9]{0,8}` (D9), so the value always fits a `u32`.
fn core_part(s: &str) -> Option<u32> {
    match s.as_bytes() {
        [b'0'] => Some(0),
        [first @ b'1'..=b'9', rest @ ..]
            if rest.len() <= 8 && rest.iter().all(u8::is_ascii_digit) =>
        {
            Some(rest.iter().fold(u32::from(first - b'0'), |acc, d| {
                acc * 10 + u32::from(d - b'0')
            }))
        }
        _ => None,
    }
}

impl CoreVersion {
    pub fn parse(s: &str) -> Option<Self> {
        let mut parts = s.split('.');
        let major = core_part(parts.next()?)?;
        let minor = core_part(parts.next()?)?;
        let patch = core_part(parts.next()?)?;
        if parts.next().is_some() {
            return None;
        }
        Some(CoreVersion {
            major,
            minor,
            patch,
        })
    }
}

impl fmt::Display for CoreVersion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}.{}.{}", self.major, self.minor, self.patch)
    }
}

/// FR-031: `X.Y.Z`, or `X.Y.Z.N` with `N` one to nine ASCII digits (leading zeros allowed), reduced to `X.Y.Z`.
pub fn reduce_class_version(s: &str) -> Option<CoreVersion> {
    if let Some(v) = CoreVersion::parse(s) {
        return Some(v);
    }
    let (head, fourth) = s.rsplit_once('.')?;
    if fourth.is_empty() || fourth.len() > 9 || !fourth.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    CoreVersion::parse(head)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Installed {
    No,
    VersionUnknown,
    Version(CoreVersion),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UpdateState {
    NotInstalled,
    UpToDate,
    UpdateAvailable,
    InstalledNewer,
    Unknown,
}

/// FR-032. An older available version is never an update.
pub fn compare(installed: Installed, available: Option<CoreVersion>) -> UpdateState {
    match (installed, available) {
        (Installed::No, _) => UpdateState::NotInstalled,
        (Installed::VersionUnknown, _) | (Installed::Version(_), None) => UpdateState::Unknown,
        (Installed::Version(have), Some(offered)) => match have.cmp(&offered) {
            Ordering::Equal => UpdateState::UpToDate,
            Ordering::Less => UpdateState::UpdateAvailable,
            Ordering::Greater => UpdateState::InstalledNewer,
        },
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arch {
    X86_64,
    Aarch64,
    Other,
}

impl Arch {
    /// The architecture this binary was built for.
    pub fn native() -> Arch {
        if cfg!(target_arch = "x86_64") {
            Arch::X86_64
        } else if cfg!(target_arch = "aarch64") {
            Arch::Aarch64
        } else {
            Arch::Other
        }
    }
}

/// An OS version of 2 to 4 components; missing trailing components compare as 0 (`11.0 == 11.0.0`).
#[derive(Clone, Copy, Debug)]
pub struct OsVersion {
    parts: [u32; 4],
    len: u8,
}

impl OsVersion {
    /// The C1 `min_os` grammar `^[0-9]+(\.[0-9]+){1,3}$`; a component that overflows `u32` fails (FR-037).
    pub fn parse(s: &str) -> Option<Self> {
        let mut parts = [0u32; 4];
        let mut len: u8 = 0;
        for run in s.split('.') {
            let slot = parts.get_mut(usize::from(len))?;
            if run.is_empty() || !run.bytes().all(|b| b.is_ascii_digit()) {
                return None;
            }
            *slot = run.parse::<u32>().ok()?;
            len += 1;
        }
        (len >= 2).then_some(OsVersion { parts, len })
    }

    pub fn components(&self) -> &[u32] {
        &self.parts[..usize::from(self.len)]
    }

    /// The components padded with 0 to four.
    fn padded(&self) -> [u32; 4] {
        let mut out = [0u32; 4];
        let own = self.components();
        out[..own.len()].copy_from_slice(own);
        out
    }
}

impl PartialEq for OsVersion {
    fn eq(&self, other: &Self) -> bool {
        self.cmp(other) == Ordering::Equal
    }
}

impl Eq for OsVersion {}

impl PartialOrd for OsVersion {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for OsVersion {
    fn cmp(&self, other: &Self) -> Ordering {
        self.padded().cmp(&other.padded())
    }
}

impl fmt::Display for OsVersion {
    /// As parsed: the parsed components, none added or dropped.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Some((first, rest)) = self.components().split_first() {
            write!(f, "{first}")?;
            for c in rest {
                write!(f, ".{c}")?;
            }
        }
        Ok(())
    }
}

/// Whether the manager offers an entry on this machine (FR-033 to FR-037).
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Availability<'a> {
    Available {
        key: PlatformKey,
        asset: &'a PlatformAsset,
    },
    NotThroughManager,
    NotForThisMachine,
    NeedsNewerOs {
        min_os: OsVersion,
    },
}

/// The FR-034 table.
pub fn platform_candidates(os: Os, arch: Arch) -> &'static [PlatformKey] {
    match (os, arch) {
        (Os::Windows, Arch::X86_64) => &[PlatformKey::WindowsX86_64],
        (Os::Macos, Arch::Aarch64) => &[PlatformKey::DarwinAarch64, PlatformKey::DarwinUniversal],
        (Os::Macos, Arch::X86_64) => &[PlatformKey::DarwinX86_64, PlatformKey::DarwinUniversal],
        (Os::Linux, Arch::X86_64) => &[PlatformKey::LinuxX86_64],
        _ => &[],
    }
}

/// FR-035, FR-037: the first present candidate is final; `running` of `None` never gates.
pub fn resolve<'a>(
    entry: &'a PluginEntry,
    os: Os,
    arch: Arch,
    running: Option<&OsVersion>,
) -> Availability<'a> {
    let Some(release) = entry.release.as_ref().filter(|r| !r.platforms.is_empty()) else {
        return Availability::NotThroughManager;
    };
    let chosen = platform_candidates(os, arch)
        .iter()
        .find_map(|key| release.platforms.get(key).map(|asset| (*key, asset)));
    let Some((key, asset)) = chosen else {
        return Availability::NotForThisMachine;
    };
    match (asset.min_os, running) {
        (Some(min_os), Some(running)) if min_os > *running => Availability::NeedsNewerOs { min_os },
        _ => Availability::Available { key, asset },
    }
}

/// The only call site of the two native values in this module (FR-036).
pub fn resolve_native<'a>(entry: &'a PluginEntry, running: Option<&OsVersion>) -> Availability<'a> {
    resolve(
        entry,
        crate::platform::NativePlatform.os(),
        Arch::native(),
        running,
    )
}

#[cfg(test)]
#[path = "version_tests.rs"]
mod tests;
