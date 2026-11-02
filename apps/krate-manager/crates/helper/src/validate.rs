//! Validation of the job's location, sources, uninstaller and pkg receipt (plan §4.1.7; FR-020 to FR-027, FR-031,
//! FR-034, D7). Byte matchers, no regex crate; every path comparison is component-wise (`same_path`, `under`).

use crate::HelperOs;
use crate::error::{self, HelperError};
use crate::roots::{CanonicalRoots, MacosRoots, STATE_DIR_NAME, StateDirRule, WindowsRoots};
use crate::seams::HelperIo;
use std::cmp::Reverse;
use std::ffi::OsStr;
use std::io;
use std::mem::discriminant;
use std::path::{Component, Path, PathBuf};

/// A node's kind as `symlink_metadata` reports it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    File,
    Dir,
    Symlink,
    Other,
}

/// What one `lstat` reports; `uid` / `mode` are `None` on Windows.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LstatFacts {
    pub kind: Kind,
    pub uid: Option<u32>,
    pub mode: Option<u32>,
}

/// The run directory's DACL, given at creation (D7).
pub const RUN_DIR_SDDL: &str = "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";

/// Where the job lives (FR-021, FR-025): its stem, its result file and the staging root beside it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct JobPlace {
    pub stem: String,
    pub result_path: PathBuf,
    pub staging_root: PathBuf,
}

/// The validated uninstaller (FR-031): `exe` is the registry's own unquoted path string (FR-027), `install_dir` the
/// canonical install directory.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Uninstaller {
    pub exe: PathBuf,
    pub install_dir: PathBuf,
}

/// The validated receipt (FR-033, FR-034, D16): files, and the removable directories deepest first.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PkgPlan {
    pub files: Vec<PathBuf>,
    pub dirs: Vec<PathBuf>,
}

/// The job directory's name under `<S>` (FR-021).
const JOBS_DIR: &str = "jobs";
/// The staging directory's name under `<S>` (FR-021).
const STAGING_DIR: &str = "staging";
/// The two components between a profile and `<S>` (FR-025, D17).
const LOCAL_APP_DATA: [&str; 2] = ["AppData", "Local"];
/// The suffix of an AUv3 app under `applications` (FR-034).
const AUV3_SUFFIX: &str = " AUv3.app";

/// D7's ancestor chain: each a directory, owned by uid 0, `mode & 0o022 == 0`; an empty chain is refused.
pub fn run_dir_ancestors_ok(facts: &[LstatFacts]) -> Result<(), HelperError> {
    let ok = |f: &LstatFacts| {
        f.kind == Kind::Dir && f.uid == Some(0) && f.mode.is_some_and(|m| m & 0o022 == 0)
    };
    if !facts.is_empty() && facts.iter().all(ok) {
        Ok(())
    } else {
        Err(error::RUN_DIR)
    }
}

/// FR-021, FR-025, FR-027: the job's place, else `E_HELPER_JOB_LOCATION`.
pub fn job_location(
    io: &dyn HelperIo,
    job: &Path,
    roots: &CanonicalRoots,
    os: HelperOs,
) -> Result<JobPlace, HelperError> {
    let refuse = error::JOB_LOCATION;
    let canonical = io.canonicalize(job).map_err(|_| refuse)?;
    if !same_path(&canonical, job, os) {
        return Err(refuse);
    }
    let name = job.file_name().and_then(OsStr::to_str).ok_or(refuse)?;
    if !is_job_name(name) {
        return Err(refuse);
    }
    let jobs = job.parent().ok_or(refuse)?;
    let state = jobs.parent().ok_or(refuse)?;
    if !named(jobs, JOBS_DIR, os) || !named(state, STATE_DIR_NAME, os) {
        return Err(refuse);
    }
    if !state_dir_ok(io, state, job, &roots.roots.state_dir, os) {
        return Err(refuse);
    }
    let stem = &name[..name.len() - ".json".len()];
    Ok(JobPlace {
        stem: stem.to_owned(),
        result_path: jobs.join(format!("{stem}.result.json")),
        staging_root: state.join(STAGING_DIR),
    })
}

/// FR-025 (D17): `<S>` under the rule.
fn state_dir_ok(
    io: &dyn HelperIo,
    state: &Path,
    job: &Path,
    rule: &StateDirRule,
    os: HelperOs,
) -> bool {
    match rule {
        StateDirRule::UnderProfiles(profiles) => {
            let Some(local) = state.parent() else {
                return false;
            };
            let Some(app_data) = local.parent() else {
                return false;
            };
            let Some(profile) = app_data.parent() else {
                return false;
            };
            named(app_data, LOCAL_APP_DATA[0], os)
                && named(local, LOCAL_APP_DATA[1], os)
                && profile.file_name().is_some()
                && profile.parent().is_some_and(|p| same_path(p, profiles, os))
        }
        StateDirRule::SameOwnerNonRoot => {
            let Some(parent) = state.parent() else {
                return false;
            };
            let jobs = state.join(JOBS_DIR);
            let staging = state.join(STAGING_DIR);
            // `staging` may be absent (nothing downloaded yet): it then owns nothing and holds no source FR-020 could
            // accept. Present, it is held to the same owner as the rest.
            let chain = [
                (parent, Kind::Dir, false),
                (state, Kind::Dir, false),
                (jobs.as_path(), Kind::Dir, false),
                (staging.as_path(), Kind::Dir, true),
                (job, Kind::File, false),
            ];
            let mut owner = None;
            for (path, kind, may_be_absent) in chain {
                let facts = match io.lstat(path) {
                    Ok(Some(facts)) => facts,
                    Ok(None) if may_be_absent => continue,
                    _ => return false,
                };
                let Some(uid) = facts.uid else {
                    return false;
                };
                if facts.kind != kind || uid == 0 || owner.is_some_and(|o| o != uid) {
                    return false;
                }
                owner = Some(uid);
            }
            true
        }
        StateDirRule::Exact(dir) => same_path(state, dir, os),
    }
}

/// FR-020, FR-027: an installer source strictly under staging, else `E_HELPER_SOURCE`. `ext` is the op's extension,
/// with or without its leading dot.
pub fn source(
    io: &dyn HelperIo,
    file: &str,
    place: &JobPlace,
    os: HelperOs,
    ext: &str,
) -> Result<PathBuf, HelperError> {
    let refuse = error::SOURCE;
    let path = Path::new(file);
    // `..` refused both as a parsed component and as raw text between either separator (a verbatim path does not
    // split on `/`).
    if !path.is_absolute()
        || file.contains('\0')
        || file.split(['/', '\\']).any(|s| s == "..")
        || path.components().any(|c| c == Component::ParentDir)
    {
        return Err(refuse);
    }
    let canonical = io.canonicalize(path).map_err(|_| refuse)?;
    if !same_path(&canonical, path, os) || !under(path, &place.staging_root, os) {
        return Err(refuse);
    }
    match io.lstat(path) {
        Ok(Some(facts)) if facts.kind == Kind::File => {}
        _ => return Err(refuse),
    }
    let want = ext.strip_prefix('.').unwrap_or(ext);
    match path.extension() {
        Some(e) if e.as_encoded_bytes().eq_ignore_ascii_case(want.as_bytes()) => {
            Ok(path.to_path_buf())
        }
        _ => Err(refuse),
    }
}

/// FR-031: the registry values `(UninstallString, InstallLocation)` against the canonical Windows roots, else
/// `E_HELPER_UNINSTALLER`.
pub fn uninstaller(
    io: &dyn HelperIo,
    roots: &WindowsRoots,
    values: (Option<String>, Option<String>),
) -> Result<Uninstaller, HelperError> {
    let refuse = error::UNINSTALLER;
    let os = HelperOs::Windows;
    let (Some(command), Some(location)) = values else {
        return Err(refuse);
    };
    let location = Path::new(location.strip_suffix('\\').unwrap_or(&location));
    if !location.is_absolute() {
        return Err(refuse);
    }
    let install_dir = io.canonicalize(location).map_err(|_| refuse)?;
    if !install_dir
        .parent()
        .is_some_and(|p| same_path(p, &roots.vst3_vendor, os))
        || !is_kind(io, &install_dir, Kind::Dir)
    {
        return Err(refuse);
    }
    let exe = Path::new(unquote(&command).ok_or(refuse)?);
    if !exe.is_absolute() {
        return Err(refuse);
    }
    let canonical = io.canonicalize(exe).map_err(|_| refuse)?;
    if !canonical
        .parent()
        .is_some_and(|p| same_path(p, &install_dir, os))
        || !canonical
            .file_name()
            .and_then(OsStr::to_str)
            .is_some_and(is_unins_name)
        || !is_kind(io, &canonical, Kind::File)
    {
        return Err(refuse);
    }
    Ok(Uninstaller {
        exe: exe.to_path_buf(),
        install_dir,
    })
}

/// One pair of surrounding double quotes removed, nothing after the closing quote; an unquoted value holds none.
fn unquote(s: &str) -> Option<&str> {
    let inner = match s.strip_prefix('"') {
        Some(rest) => rest.strip_suffix('"')?,
        None => s,
    };
    (!inner.is_empty() && !inner.contains('"')).then_some(inner)
}

/// FR-033, FR-034, D16: the receipt's lines against the canonical macOS roots, else `E_HELPER_PKG_PATH`.
pub fn pkg_plan(
    io: &dyn HelperIo,
    roots: &MacosRoots,
    location: &str,
    files: &[u8],
    dirs: &[u8],
) -> Result<PkgPlan, HelperError> {
    let refuse = error::PKG_PATH;
    let os = HelperOs::Macos;
    let location = location.strip_prefix('/').unwrap_or(location);
    let location = location.strip_suffix('/').unwrap_or(location);
    let base = if location.is_empty() {
        roots.volume.clone()
    } else {
        join_plain(&roots.volume, location).ok_or(refuse)?
    };
    let file_roots = [&roots.vst3, &roots.components, &roots.presets];
    let in_file_root = |p: &Path| file_roots.iter().any(|r| under(p, r, os));
    let mut plan = PkgPlan {
        files: Vec::new(),
        dirs: Vec::new(),
    };
    for line in lines(files).ok_or(refuse)? {
        let entry = canonical_entry(io, &join_plain(&base, line).ok_or(refuse)?).ok_or(refuse)?;
        if !(in_file_root(&entry) || in_auv3(&entry, &roots.applications, true)) {
            return Err(refuse);
        }
        plan.files.push(entry);
    }
    for line in lines(dirs).ok_or(refuse)? {
        let entry = canonical_entry(io, &join_plain(&base, line).ok_or(refuse)?).ok_or(refuse)?;
        if in_file_root(&entry) || in_auv3(&entry, &roots.applications, false) {
            plan.dirs.push(entry);
        } else if !file_roots
            .into_iter()
            .chain([&roots.applications])
            .any(|r| same_path(&entry, r, os) || under(r, &entry, os))
        {
            return Err(refuse);
        }
    }
    plan.dirs.sort_by_key(|d| Reverse(d.components().count()));
    Ok(plan)
}

/// Every non-empty line of a `pkgutil` listing, a trailing `\r` stripped; `None` when one is not UTF-8.
fn lines(bytes: &[u8]) -> Option<Vec<&str>> {
    bytes
        .split(|b| *b == b'\n')
        .map(|l| l.strip_suffix(b"\r").unwrap_or(l))
        .filter(|l| !l.is_empty())
        .map(|l| std::str::from_utf8(l).ok())
        .collect()
}

/// `base` + each `/`-separated segment of `rel`, each plain (no `..`, `.`, empty, NUL) and each adding exactly one
/// component (FR-033's plain-segment join).
fn join_plain(base: &Path, rel: &str) -> Option<PathBuf> {
    let segments: Vec<&str> = rel.split('/').collect();
    if segments
        .iter()
        .any(|s| s.is_empty() || *s == "." || *s == ".." || s.contains('\0'))
    {
        return None;
    }
    let mut out = base.to_path_buf();
    for s in &segments {
        out.push(s);
    }
    (out.starts_with(base)
        && out.components().count() == base.components().count() + segments.len())
    .then_some(out)
}

/// FR-034: the entry's parent canonicalized (its deepest existing ancestor plus the missing components, FR-026), the
/// last component re-joined, never followed.
fn canonical_entry(io: &dyn HelperIo, entry: &Path) -> Option<PathBuf> {
    let name = entry.file_name()?;
    let mut missing = Vec::new();
    let mut cur = entry.parent()?;
    loop {
        match io.canonicalize(cur) {
            Ok(mut out) => {
                for m in missing.iter().rev() {
                    out.push(m);
                }
                out.push(name);
                return Some(out);
            }
            Err(e) if e.kind() == io::ErrorKind::NotFound => {
                missing.push(cur.file_name()?);
                cur = cur.parent()?;
            }
            Err(_) => return None,
        }
    }
}

/// `p` is `applications/<N> AUv3.app` with `is_plugin_name(N)` or lies under it; `strict` requires under.
fn in_auv3(p: &Path, applications: &Path, strict: bool) -> bool {
    if !under(p, applications, HelperOs::Macos) {
        return false;
    }
    let mut rest = p.components().skip(applications.components().count());
    let app = match rest.next() {
        Some(Component::Normal(n)) => n.to_str(),
        _ => None,
    };
    app.and_then(|a| a.strip_suffix(AUV3_SUFFIX))
        .is_some_and(is_plugin_name)
        && (!strict || rest.next().is_some())
}

/// `path`'s `lstat` kind is `kind`.
fn is_kind(io: &dyn HelperIo, path: &Path, kind: Kind) -> bool {
    matches!(io.lstat(path), Ok(Some(f)) if f.kind == kind)
}

/// `path`'s last component is `name` (ASCII case-insensitive with `os == Windows`).
fn named(path: &Path, name: &str, os: HelperOs) -> bool {
    path.file_name()
        .is_some_and(|n| os_eq(n, OsStr::new(name), os))
}

/// Byte equality; ASCII case-insensitive with `os == Windows`.
fn os_eq(a: &OsStr, b: &OsStr, os: HelperOs) -> bool {
    let (a, b) = (a.as_encoded_bytes(), b.as_encoded_bytes());
    if os == HelperOs::Windows {
        a.eq_ignore_ascii_case(b)
    } else {
        a == b
    }
}

/// One component each, the same kind and text under `os_eq`.
fn component_eq(a: Component<'_>, b: Component<'_>, os: HelperOs) -> bool {
    discriminant(&a) == discriminant(&b) && os_eq(a.as_os_str(), b.as_os_str(), os)
}

/// Component-wise equality; ASCII case-insensitive with `os == Windows`, the `Prefix` included.
pub fn same_path(a: &Path, b: &Path, os: HelperOs) -> bool {
    let (mut a, mut b) = (a.components(), b.components());
    loop {
        match (a.next(), b.next()) {
            (None, None) => return true,
            (Some(x), Some(y)) if component_eq(x, y, os) => {}
            _ => return false,
        }
    }
}

/// `child` a strict descendant of `root`, under `same_path`'s rule.
pub fn under(child: &Path, root: &Path, os: HelperOs) -> bool {
    let mut c = child.components();
    for r in root.components() {
        match c.next() {
            Some(x) if component_eq(x, r, os) => {}
            _ => return false,
        }
    }
    c.next().is_some()
}

/// `bytes` all in `set`.
fn all_in(bytes: &[u8], set: fn(&u8) -> bool) -> bool {
    bytes.iter().all(set)
}

/// `^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}$` (D4).
pub fn is_app_id(s: &str) -> bool {
    let Some(inner) = s.strip_prefix('{').and_then(|r| r.strip_suffix('}')) else {
        return false;
    };
    let groups: Vec<&[u8]> = inner.as_bytes().split(|b| *b == b'-').collect();
    let upper_hex = |b: &u8| b.is_ascii_digit() || (b'A'..=b'F').contains(b);
    groups.len() == 5
        && groups
            .iter()
            .zip([8, 4, 4, 4, 12])
            .all(|(g, n)| g.len() == n && all_in(g, upper_hex))
}

/// `^com\.krateaudio\.[a-z0-9]+$` (FR-033).
pub fn is_pkg_id(s: &str) -> bool {
    s.strip_prefix("com.krateaudio.").is_some_and(|rest| {
        !rest.is_empty()
            && all_in(rest.as_bytes(), |b| {
                b.is_ascii_lowercase() || b.is_ascii_digit()
            })
    })
}

/// `^[A-Z][A-Za-z0-9]{0,63}$` (FR-034).
pub fn is_plugin_name(s: &str) -> bool {
    match s.as_bytes().split_first() {
        Some((first, rest)) => {
            first.is_ascii_uppercase()
                && rest.len() <= 63
                && all_in(rest, u8::is_ascii_alphanumeric)
        }
        None => false,
    }
}

/// `^unins[0-9]{3}\.exe$`, case-insensitive (FR-031).
pub fn is_unins_name(s: &str) -> bool {
    let b = s.as_bytes();
    b.len() == 12
        && b[..5].eq_ignore_ascii_case(b"unins")
        && all_in(&b[5..8], u8::is_ascii_digit)
        && b[8..].eq_ignore_ascii_case(b".exe")
}

/// `^job-[0-9a-f]{32}\.json$` (FR-021).
pub fn is_job_name(s: &str) -> bool {
    s.strip_prefix("job-")
        .and_then(|r| r.strip_suffix(".json"))
        .is_some_and(|hex| {
            hex.len() == 32
                && all_in(hex.as_bytes(), |b| {
                    b.is_ascii_digit() || (b'a'..=b'f').contains(b)
                })
        })
}

#[cfg(test)]
#[path = "validate_tests.rs"]
mod tests;
