//! The seams every OS effect goes through (plan §4.1.9; FR-053, SI-23) and their test fakes (`fake`, PD-8).

use crate::classify::InnoVerdict;
use crate::validate::LstatFacts;
use std::ffi::OsString;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::time::Duration;

/// A writer whose bytes can be made durable.
pub trait WriteSync: io::Write {
    fn sync_all(&mut self) -> io::Result<()>;
}

/// Every filesystem, clock and randomness step of the helper.
pub trait HelperIo {
    /// From the start, at most `max + 1` bytes (PD-6 open).
    fn read_bounded(&self, path: &Path, max: u64) -> io::Result<Vec<u8>>;
    /// The last `max` bytes (SI-28).
    fn read_tail(&self, path: &Path, max: u64) -> io::Result<Vec<u8>>;
    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf>;
    /// `None` = absent; `uid` / `mode` `None` on Windows.
    fn lstat(&self, path: &Path) -> io::Result<Option<LstatFacts>>;
    /// PD-6: no final link, no blocking, a regular file only.
    fn open_source(&self, path: &Path) -> io::Result<Box<dyn Read + '_>>;
    fn create_new(&self, path: &Path) -> io::Result<Box<dyn WriteSync + '_>>;
    /// PD-26 (macOS); `Ok` when present.
    fn create_helper_folder(&self, path: &Path) -> io::Result<()>;
    /// D7: fails on an existing name.
    fn create_run_dir(&self, path: &Path) -> io::Result<()>;
    fn list_dir(&self, dir: &Path) -> io::Result<Vec<OsString>>;
    fn remove_file(&self, path: &Path) -> io::Result<()>;
    fn remove_dir(&self, path: &Path) -> io::Result<()>;
    /// Phase 12 FR-055: another process holds the file without write sharing (a loaded DLL included); `Ok(false)`
    /// when absent, and always off Windows. Writes nothing.
    fn in_use(&self, path: &Path) -> io::Result<bool>;
    /// 32 lower-case hex digits (PD-19).
    fn random_hex(&self) -> String;
    fn sleep(&self, d: Duration);
    /// Monotonic since an arbitrary origin (FR-041).
    fn now(&self) -> Duration;
}

/// A spawned child, by number.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ChildId(pub u64);

/// Child processes, never through a shell (FR-030 to FR-033).
pub trait ProcessRunner {
    fn spawn(&self, argv: &[OsString]) -> io::Result<ChildId>;
    /// `None` = still running at the timeout; the child is never killed (FR-042).
    fn wait(&self, child: ChildId, timeout: Duration) -> io::Result<Option<i32>>;
    fn output(&self, argv: &[OsString], timeout: Duration) -> io::Result<Option<(i32, Vec<u8>)>>;
}

/// HKLM reads, 64-bit view (FR-031).
pub trait Registry {
    /// `(UninstallString, InstallLocation)`; `None` = no key.
    fn uninstall_values(&self, subkey: &str) -> Option<(Option<String>, Option<String>)>;
    fn key_exists(&self, subkey: &str) -> bool;
}

/// The seams of one run (FR-053).
pub struct Seams<'a> {
    pub io: &'a dyn HelperIo,
    pub runner: &'a dyn ProcessRunner,
    pub registry: &'a dyn Registry,
    pub classify: fn(i32, &[String]) -> InnoVerdict,
}

/// The test fakes (plan §4.1.9, tasks TA-3): one shared `World` with a fake clock, the real filesystem under a
/// sandbox base, a scripted runner and a map registry.
#[cfg(any(test, feature = "test-support"))]
pub mod fake {
    use super::{ChildId, HelperIo, ProcessRunner, Registry, WriteSync};
    use crate::os::open_regular_no_follow;
    use crate::validate::{Kind, LstatFacts};
    use std::collections::BTreeMap;
    use std::ffi::OsString;
    use std::fs::{self, File, Metadata};
    use std::io::{self, Read, Seek, SeekFrom, Write};
    use std::path::{Component, Path, PathBuf};
    use std::sync::{Arc, Mutex, MutexGuard, PoisonError};
    use std::time::Duration;

    /// The mode `root_owned` gives a run directory created under it (D7).
    const RUN_DIR_MODE: u32 = 0o700;
    /// The mode `root_owned` gives its directory and everything else created under it.
    const ROOT_MODE: u32 = 0o755;

    /// One fake key's `(UninstallString, InstallLocation)`.
    type KeyValues = (Option<String>, Option<String>);

    /// A world change that fires when the clock reaches its time.
    #[derive(Clone, Debug, PartialEq, Eq)]
    pub enum Event {
        RemovePath(PathBuf),
        RemoveKey(String),
    }

    /// An injected I/O failure; `name` is the final path component.
    #[derive(Clone, Debug, PartialEq, Eq)]
    pub enum Fault {
        CreateRunDir(io::ErrorKind),
        CreateNew {
            name: OsString,
            kind: io::ErrorKind,
        },
        WriteAfter {
            name: OsString,
            bytes: u64,
            kind: io::ErrorKind,
        },
        RemoveFile(PathBuf),
        /// `in_use` answers true for this path (a DAW holding the plug-in binary).
        InUse(PathBuf),
    }

    /// A change to a source around its copy.
    #[derive(Clone, Debug, PartialEq, Eq)]
    pub enum Hook {
        RewriteAfterCopy {
            src: PathBuf,
            bytes: Vec<u8>,
        },
        RewriteDuringCopy {
            src: PathBuf,
            bytes: Vec<u8>,
        },
        LinkBeforeOpen {
            src: PathBuf,
            target: PathBuf,
        },
        #[cfg(unix)]
        FifoBeforeOpen {
            src: PathBuf,
        },
    }

    impl Hook {
        fn src(&self) -> &Path {
            match self {
                Hook::RewriteAfterCopy { src, .. }
                | Hook::RewriteDuringCopy { src, .. }
                | Hook::LinkBeforeOpen { src, .. } => src,
                #[cfg(unix)]
                Hook::FifoBeforeOpen { src } => src,
            }
        }

        /// Fires before the open (a link or a FIFO in place of the source), not around the copy.
        fn before_open(&self) -> bool {
            match self {
                Hook::LinkBeforeOpen { .. } => true,
                #[cfg(unix)]
                Hook::FifoBeforeOpen { .. } => true,
                Hook::RewriteAfterCopy { .. } | Hook::RewriteDuringCopy { .. } => false,
            }
        }
    }

    /// One recorded seam call.
    #[derive(Clone, Debug, PartialEq, Eq)]
    pub enum Call {
        Spawn(Vec<OsString>),
        Wait { child: ChildId, timeout: Duration },
        Output(Vec<OsString>),
        Sleep(Duration),
        CreateNew(PathBuf),
        CreateHelperFolder(PathBuf),
        CreateRunDir(PathBuf),
        RemoveFile(PathBuf),
        RemoveDir(PathBuf),
        UninstallValues(String),
        KeyExists(String),
        InUse(PathBuf),
    }

    /// A lock that survives a panic in another holder (the sandbox rows panic on purpose).
    fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
        m.lock().unwrap_or_else(PoisonError::into_inner)
    }

    /// The spelling the world keys overrides, faults and hooks by: the canonical parent plus the final name (the final
    /// component is never followed), so a temp path and its canonical form (`\\?\C:\…` on Windows, `/private/var/…` on
    /// macOS) meet; the path as given when the parent does not resolve.
    fn key(path: &Path) -> PathBuf {
        match (path.parent(), path.file_name()) {
            (Some(parent), Some(name)) if !parent.as_os_str().is_empty() => {
                fs::canonicalize(parent).map_or_else(|_| path.to_path_buf(), |p| p.join(name))
            }
            _ => fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf()),
        }
    }

    /// Lexical strict descendant, as `Roots::check_sandboxed` (`crates/core/src/roots.rs:173-184`); never through `..`.
    fn strictly_under(path: &Path, base: &Path) -> bool {
        path != base
            && path.starts_with(base)
            && !path.components().any(|c| c == Component::ParentDir)
    }

    fn injected(kind: io::ErrorKind) -> io::Error {
        io::Error::new(kind, "seams::fake injected fault")
    }

    /// The shared state of one test: clock, events, owner overrides, faults, hooks, calls, the registry map.
    #[derive(Default)]
    pub struct World {
        clock: Mutex<Duration>,
        events: Mutex<Vec<(Duration, Event)>>,
        /// (uid, mode) by `key`.
        owners: Mutex<BTreeMap<PathBuf, (u32, u32)>>,
        /// The `root_owned` directories, by `key`.
        root_dirs: Mutex<Vec<PathBuf>>,
        faults: Mutex<Vec<Fault>>,
        hooks: Mutex<Vec<Hook>>,
        calls: Mutex<Vec<Call>>,
        /// `(UninstallString, InstallLocation)` by full subkey (TA-17).
        registry: Mutex<BTreeMap<String, KeyValues>>,
        /// Every `TempIo` base, as given and canonical: where `Event::RemovePath` may act.
        bases: Mutex<Vec<PathBuf>>,
        hex: Mutex<u128>,
    }

    impl World {
        pub fn new() -> World {
            World::default()
        }

        pub fn now(&self) -> Duration {
            *lock(&self.clock)
        }

        /// Moves the clock and fires every event whose time it has reached, earliest first.
        pub fn advance(&self, d: Duration) {
            let now = {
                let mut clock = lock(&self.clock);
                *clock += d;
                *clock
            };
            self.fire_due(now);
        }

        /// Fires at the first clock move that reaches `when`.
        pub fn at(&self, when: Duration, event: Event) {
            lock(&self.events).push((when, event));
        }

        pub fn owner(&self, path: &Path, uid: u32, mode: u32) {
            let k = key(path);
            lock(&self.owners).insert(k, (uid, mode));
        }

        /// `dir` and everything `TempIo` later creates under it report uid 0: `0o700` for a run directory, else
        /// `0o755`.
        pub fn root_owned(&self, dir: &Path) {
            let k = key(dir);
            lock(&self.root_dirs).push(k.clone());
            lock(&self.owners).insert(k, (0, ROOT_MODE));
        }

        /// Holds until `clear_faults`.
        pub fn fault(&self, fault: Fault) {
            lock(&self.faults).push(fault);
        }

        pub fn clear_faults(&self) {
            lock(&self.faults).clear();
        }

        /// Fires once, at the next open of its source.
        pub fn hook(&self, hook: Hook) {
            lock(&self.hooks).push(hook);
        }

        pub fn calls(&self) -> Vec<Call> {
            lock(&self.calls).clone()
        }

        /// The argv of every `spawn` call, in order.
        pub fn spawns(&self) -> Vec<Vec<OsString>> {
            lock(&self.calls)
                .iter()
                .filter_map(|c| match c {
                    Call::Spawn(argv) => Some(argv.clone()),
                    _ => None,
                })
                .collect()
        }

        /// The path of every `remove_file` and `remove_dir` call, in order.
        pub fn removals(&self) -> Vec<PathBuf> {
            lock(&self.calls)
                .iter()
                .filter_map(|c| match c {
                    Call::RemoveFile(p) | Call::RemoveDir(p) => Some(p.clone()),
                    _ => None,
                })
                .collect()
        }

        fn record(&self, call: Call) {
            lock(&self.calls).push(call);
        }

        fn fire_due(&self, now: Duration) {
            let due = {
                let mut events = lock(&self.events);
                let all = std::mem::take(&mut *events);
                let (mut due, keep): (Vec<_>, Vec<_>) =
                    all.into_iter().partition(|(when, _)| *when <= now);
                *events = keep;
                due.sort_by_key(|(when, _)| *when);
                due
            };
            for (_, event) in due {
                match event {
                    Event::RemovePath(path) => self.remove_real(&path),
                    Event::RemoveKey(subkey) => {
                        lock(&self.registry).remove(&subkey);
                    }
                }
            }
        }

        fn remove_real(&self, path: &Path) {
            let inside = lock(&self.bases).iter().any(|b| strictly_under(path, b));
            if !inside {
                panic!(
                    "Event::RemovePath: {} is outside the test sandbox",
                    path.display()
                );
            }
            let removed = match fs::symlink_metadata(path) {
                Err(e) if e.kind() == io::ErrorKind::NotFound => return,
                Err(e) => Err(e),
                Ok(meta) if meta.is_dir() => fs::remove_dir_all(path),
                Ok(_) => fs::remove_file(path),
            };
            if let Err(e) = removed {
                panic!("Event::RemovePath {}: {e}", path.display());
            }
        }

        fn mark_created(&self, path: &Path, mode: u32) {
            let k = key(path);
            let under = lock(&self.root_dirs)
                .iter()
                .any(|root| strictly_under(&k, root));
            if under {
                lock(&self.owners).insert(k, (0, mode));
            }
        }

        fn owner_of(&self, path: &Path) -> Option<(u32, u32)> {
            let k = key(path);
            lock(&self.owners).get(&k).copied()
        }

        fn find_fault<T>(&self, f: impl Fn(&Fault) -> Option<T>) -> Option<T> {
            lock(&self.faults).iter().find_map(f)
        }

        /// Removes and returns the hooks on `path` that `pick` selects.
        fn take_hooks(&self, path: &Path, pick: impl Fn(&Hook) -> bool) -> Vec<Hook> {
            let k = key(path);
            let mut hooks = lock(&self.hooks);
            let all = std::mem::take(&mut *hooks);
            let (taken, keep): (Vec<_>, Vec<_>) =
                all.into_iter().partition(|h| pick(h) && key(h.src()) == k);
            *hooks = keep;
            taken
        }

        fn next_hex(&self) -> String {
            let mut n = lock(&self.hex);
            *n += 1;
            format!("{:032x}", *n)
        }
    }

    /// The real filesystem under `base` plus the world's overrides; panics "outside the test sandbox".
    pub struct TempIo<'w> {
        world: &'w World,
        /// `base` as given, and canonical when that differs.
        bases: Vec<PathBuf>,
    }

    impl<'w> TempIo<'w> {
        pub fn new(world: &'w World, base: &Path) -> TempIo<'w> {
            let mut bases = vec![base.to_path_buf()];
            if let Ok(canonical) = fs::canonicalize(base)
                && canonical != base
            {
                bases.push(canonical);
            }
            lock(&world.bases).extend(bases.iter().cloned());
            TempIo { world, bases }
        }

        fn check(&self, op: &str, path: &Path) {
            if !self.bases.iter().any(|b| strictly_under(path, b)) {
                panic!(
                    "TempIo::{op}: {} is outside the test sandbox {}",
                    path.display(),
                    self.bases[0].display()
                );
            }
        }

        /// The sandbox check, then the before-open hooks; the open itself is the production one (PD-6).
        fn open(&self, op: &str, path: &Path) -> io::Result<File> {
            self.check(op, path);
            for hook in self.world.take_hooks(path, Hook::before_open) {
                match hook {
                    Hook::LinkBeforeOpen { src, target } => replace_with_link(&src, &target),
                    #[cfg(unix)]
                    Hook::FifoBeforeOpen { src } => replace_with_fifo(&src),
                    Hook::RewriteAfterCopy { .. } | Hook::RewriteDuringCopy { .. } => {}
                }
            }
            open_regular_no_follow(path)
        }
    }

    fn remove_if_present(path: &Path) {
        match fs::remove_file(path) {
            Ok(()) => {}
            Err(e) if e.kind() == io::ErrorKind::NotFound => {}
            Err(e) => panic!("seams::fake hook: cannot remove {}: {e}", path.display()),
        }
    }

    fn replace_with_link(src: &Path, target: &Path) {
        remove_if_present(src);
        #[cfg(unix)]
        let made = std::os::unix::fs::symlink(target, src);
        #[cfg(windows)]
        let made = if target.is_dir() {
            std::os::windows::fs::symlink_dir(target, src)
        } else {
            std::os::windows::fs::symlink_file(target, src)
        };
        if let Err(e) = made {
            panic!("Hook::LinkBeforeOpen: cannot link {}: {e}", src.display());
        }
    }

    #[cfg(unix)]
    fn replace_with_fifo(src: &Path) {
        remove_if_present(src);
        let status = std::process::Command::new("/usr/bin/mkfifo")
            .arg(src)
            .status();
        if !status.as_ref().is_ok_and(std::process::ExitStatus::success) {
            panic!("Hook::FifoBeforeOpen: mkfifo {}: {status:?}", src.display());
        }
    }

    #[cfg(unix)]
    fn real_owner(meta: &Metadata) -> (Option<u32>, Option<u32>) {
        use std::os::unix::fs::MetadataExt;
        (Some(meta.uid()), Some(meta.mode()))
    }

    #[cfg(not(unix))]
    fn real_owner(_meta: &Metadata) -> (Option<u32>, Option<u32>) {
        (None, None)
    }

    #[cfg(unix)]
    fn make_run_dir(path: &Path) -> io::Result<()> {
        use std::os::unix::fs::DirBuilderExt;
        fs::DirBuilder::new().mode(RUN_DIR_MODE).create(path)
    }

    #[cfg(not(unix))]
    fn make_run_dir(path: &Path) -> io::Result<()> {
        fs::create_dir(path)
    }

    /// The source being copied, with the copy hooks: a rewrite after the first chunk, a rewrite at end of file.
    struct CopyReader {
        file: File,
        src: PathBuf,
        during: Option<Vec<u8>>,
        after: Option<Vec<u8>>,
    }

    impl Read for CopyReader {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            if buf.is_empty() {
                return Ok(0);
            }
            let n = self.file.read(buf)?;
            let rewrite = if n > 0 {
                self.during.take()
            } else {
                self.after.take()
            };
            if let Some(bytes) = rewrite
                && let Err(e) = fs::write(&self.src, bytes)
            {
                panic!(
                    "seams::fake copy hook: cannot rewrite {}: {e}",
                    self.src.display()
                );
            }
            Ok(n)
        }
    }

    /// A created file; with a `WriteAfter` fault, every write past its budget fails.
    struct FaultWriter {
        file: File,
        budget: Option<(u64, io::ErrorKind)>,
    }

    impl Write for FaultWriter {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            let Some((left, kind)) = self.budget else {
                return self.file.write(buf);
            };
            if buf.is_empty() {
                return Ok(0);
            }
            if left == 0 {
                return Err(injected(kind));
            }
            let n = usize::try_from(left).map_or(buf.len(), |l| l.min(buf.len()));
            let written = self.file.write(&buf[..n])?;
            self.budget = Some((left - written as u64, kind));
            Ok(written)
        }

        fn flush(&mut self) -> io::Result<()> {
            self.file.flush()
        }
    }

    impl WriteSync for FaultWriter {
        fn sync_all(&mut self) -> io::Result<()> {
            self.file.sync_all()
        }
    }

    impl HelperIo for TempIo<'_> {
        fn read_bounded(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
            let file = self.open("read_bounded", path)?;
            let mut out = Vec::new();
            file.take(max.saturating_add(1)).read_to_end(&mut out)?;
            Ok(out)
        }

        fn read_tail(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
            let mut file = self.open("read_tail", path)?;
            let len = file.metadata()?.len();
            file.seek(SeekFrom::Start(len.saturating_sub(max)))?;
            let mut out = Vec::new();
            file.read_to_end(&mut out)?;
            Ok(out)
        }

        fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
            fs::canonicalize(path)
        }

        fn lstat(&self, path: &Path) -> io::Result<Option<LstatFacts>> {
            let meta = match fs::symlink_metadata(path) {
                Ok(meta) => meta,
                Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
                Err(e) => return Err(e),
            };
            let ft = meta.file_type();
            let kind = if ft.is_symlink() {
                Kind::Symlink
            } else if ft.is_dir() {
                Kind::Dir
            } else if ft.is_file() {
                Kind::File
            } else {
                Kind::Other
            };
            let (uid, mode) = match self.world.owner_of(path) {
                Some((uid, mode)) => (Some(uid), Some(mode)),
                None => real_owner(&meta),
            };
            Ok(Some(LstatFacts { kind, uid, mode }))
        }

        fn open_source(&self, path: &Path) -> io::Result<Box<dyn Read + '_>> {
            let file = self.open("open_source", path)?;
            let mut during = None;
            let mut after = None;
            for hook in self.world.take_hooks(path, |h| !h.before_open()) {
                match hook {
                    Hook::RewriteDuringCopy { bytes, .. } => during = Some(bytes),
                    Hook::RewriteAfterCopy { bytes, .. } => after = Some(bytes),
                    Hook::LinkBeforeOpen { .. } => {}
                    #[cfg(unix)]
                    Hook::FifoBeforeOpen { .. } => {}
                }
            }
            Ok(Box::new(CopyReader {
                file,
                src: path.to_path_buf(),
                during,
                after,
            }))
        }

        fn create_new(&self, path: &Path) -> io::Result<Box<dyn WriteSync + '_>> {
            self.check("create_new", path);
            self.world.record(Call::CreateNew(path.to_path_buf()));
            let name = path.file_name();
            if let Some(kind) = self.world.find_fault(|f| match f {
                Fault::CreateNew { name: n, kind } if name == Some(n.as_os_str()) => Some(*kind),
                _ => None,
            }) {
                return Err(injected(kind));
            }
            let budget = self.world.find_fault(|f| match f {
                Fault::WriteAfter {
                    name: n,
                    bytes,
                    kind,
                } if name == Some(n.as_os_str()) => Some((*bytes, *kind)),
                _ => None,
            });
            let file = fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(path)?;
            self.world.mark_created(path, ROOT_MODE);
            Ok(Box::new(FaultWriter { file, budget }))
        }

        fn create_helper_folder(&self, path: &Path) -> io::Result<()> {
            self.check("create_helper_folder", path);
            self.world
                .record(Call::CreateHelperFolder(path.to_path_buf()));
            match fs::create_dir(path) {
                Ok(()) => {}
                Err(e) if e.kind() == io::ErrorKind::AlreadyExists && path.is_dir() => {}
                Err(e) => return Err(e),
            }
            self.world.mark_created(path, ROOT_MODE);
            Ok(())
        }

        fn create_run_dir(&self, path: &Path) -> io::Result<()> {
            self.check("create_run_dir", path);
            self.world.record(Call::CreateRunDir(path.to_path_buf()));
            if let Some(kind) = self.world.find_fault(|f| match f {
                Fault::CreateRunDir(kind) => Some(*kind),
                _ => None,
            }) {
                return Err(injected(kind));
            }
            make_run_dir(path)?;
            self.world.mark_created(path, RUN_DIR_MODE);
            Ok(())
        }

        fn list_dir(&self, dir: &Path) -> io::Result<Vec<OsString>> {
            let mut names = fs::read_dir(dir)?
                .map(|entry| entry.map(|e| e.file_name()))
                .collect::<io::Result<Vec<_>>>()?;
            names.sort();
            Ok(names)
        }

        fn remove_file(&self, path: &Path) -> io::Result<()> {
            self.check("remove_file", path);
            self.world.record(Call::RemoveFile(path.to_path_buf()));
            let k = key(path);
            let faulted = self.world.find_fault(|f| match f {
                Fault::RemoveFile(p) if key(p) == k => Some(()),
                _ => None,
            });
            if faulted.is_some() {
                return Err(injected(io::ErrorKind::PermissionDenied));
            }
            fs::remove_file(path)
        }

        fn remove_dir(&self, path: &Path) -> io::Result<()> {
            self.check("remove_dir", path);
            self.world.record(Call::RemoveDir(path.to_path_buf()));
            fs::remove_dir(path)
        }

        fn in_use(&self, path: &Path) -> io::Result<bool> {
            self.check("in_use", path);
            self.world.record(Call::InUse(path.to_path_buf()));
            let k = key(path);
            let held = self.world.find_fault(|f| match f {
                Fault::InUse(p) if key(p) == k => Some(()),
                _ => None,
            });
            Ok(held.is_some())
        }

        fn random_hex(&self) -> String {
            self.world.next_hex()
        }

        fn sleep(&self, d: Duration) {
            self.world.record(Call::Sleep(d));
            self.world.advance(d);
        }

        fn now(&self) -> Duration {
            self.world.now()
        }
    }

    /// Which calls a script answers: an argv prefix, the n-th `spawn` / `output` call (0-based), or any.
    #[derive(Clone, Debug, PartialEq, Eq)]
    pub enum Match {
        Prefix(Vec<OsString>),
        Nth(usize),
        Any,
    }

    /// A script's side effect on the world, given the argv.
    pub type Effect<'w> = Box<dyn Fn(&[OsString]) + Send + Sync + 'w>;

    /// One scripted child; `Default` exits 0 at once with no output and no effect.
    #[derive(Default)]
    pub struct Script<'w> {
        pub exit: i32,
        pub takes: Duration,
        pub never_exits: bool,
        pub spawn_error: Option<io::ErrorKind>,
        pub stdout: Vec<u8>,
        pub effect: Option<Effect<'w>>,
    }

    /// One spawned child: its argv, its script (`None` = no rule), the time it still takes.
    struct Child<'w> {
        argv: Vec<OsString>,
        script: Option<Arc<Script<'w>>>,
        left: Duration,
        exited: bool,
    }

    /// A `ProcessRunner` over scripts; the first matching rule in insertion order wins.
    pub struct ScriptedRunner<'w> {
        world: &'w World,
        rules: Mutex<Vec<(Match, Arc<Script<'w>>)>>,
        /// `spawn` and `output` calls so far (`Match::Nth`).
        count: Mutex<usize>,
        children: Mutex<BTreeMap<u64, Child<'w>>>,
    }

    impl<'w> ScriptedRunner<'w> {
        pub fn new(world: &'w World) -> Self {
            ScriptedRunner {
                world,
                rules: Mutex::new(Vec::new()),
                count: Mutex::new(0),
                children: Mutex::new(BTreeMap::new()),
            }
        }

        pub fn on(&self, m: Match, s: Script<'w>) {
            lock(&self.rules).push((m, Arc::new(s)));
        }

        fn pick(&self, argv: &[OsString]) -> Option<Arc<Script<'w>>> {
            let n = {
                let mut count = lock(&self.count);
                let n = *count;
                *count += 1;
                n
            };
            lock(&self.rules)
                .iter()
                .find(|(m, _)| match m {
                    Match::Prefix(prefix) => argv.starts_with(prefix),
                    Match::Nth(i) => *i == n,
                    Match::Any => true,
                })
                .map(|(_, s)| Arc::clone(s))
        }
    }

    impl ProcessRunner for ScriptedRunner<'_> {
        fn spawn(&self, argv: &[OsString]) -> io::Result<ChildId> {
            self.world.record(Call::Spawn(argv.to_vec()));
            let script = self.pick(argv);
            if let Some(kind) = script.as_ref().and_then(|s| s.spawn_error) {
                return Err(injected(kind));
            }
            let mut children = lock(&self.children);
            let id = children.len() as u64 + 1;
            children.insert(
                id,
                Child {
                    argv: argv.to_vec(),
                    left: script.as_ref().map_or(Duration::ZERO, |s| s.takes),
                    script,
                    exited: false,
                },
            );
            Ok(ChildId(id))
        }

        /// Advances the clock by what the child still takes (all of `timeout` when it never exits); the effect runs
        /// when it exits.
        fn wait(&self, child: ChildId, timeout: Duration) -> io::Result<Option<i32>> {
            self.world.record(Call::Wait { child, timeout });
            let (step, outcome, finished) = {
                let mut children = lock(&self.children);
                let Some(c) = children.get_mut(&child.0) else {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "seams::fake: unknown child",
                    ));
                };
                let exit = c.script.as_ref().map_or(0, |s| s.exit);
                if c.exited {
                    return Ok(Some(exit));
                }
                if c.script.as_ref().is_some_and(|s| s.never_exits) {
                    (timeout, None, None)
                } else if c.left <= timeout {
                    let step = c.left;
                    c.left = Duration::ZERO;
                    c.exited = true;
                    let finished = c.script.clone().map(|s| (s, c.argv.clone()));
                    (step, Some(exit), finished)
                } else {
                    c.left -= timeout;
                    (timeout, None, None)
                }
            };
            self.world.advance(step);
            if let Some((script, argv)) = finished
                && let Some(effect) = &script.effect
            {
                effect(&argv);
            }
            Ok(outcome)
        }

        fn output(
            &self,
            argv: &[OsString],
            timeout: Duration,
        ) -> io::Result<Option<(i32, Vec<u8>)>> {
            self.world.record(Call::Output(argv.to_vec()));
            let Some(script) = self.pick(argv) else {
                return Ok(Some((0, Vec::new())));
            };
            if let Some(kind) = script.spawn_error {
                return Err(injected(kind));
            }
            if script.never_exits || script.takes > timeout {
                self.world.advance(timeout);
                return Ok(None);
            }
            self.world.advance(script.takes);
            if let Some(effect) = &script.effect {
                effect(argv);
            }
            Ok(Some((script.exit, script.stdout.clone())))
        }
    }

    /// A `Registry` over the world's map (tasks TA-17: `subkey` is the full subkey).
    pub struct FakeRegistry<'w> {
        world: &'w World,
    }

    impl<'w> FakeRegistry<'w> {
        pub fn new(world: &'w World) -> Self {
            FakeRegistry { world }
        }

        pub fn set(
            &self,
            subkey: &str,
            uninstall_string: Option<&str>,
            install_location: Option<&str>,
        ) {
            lock(&self.world.registry).insert(
                subkey.to_owned(),
                (
                    uninstall_string.map(str::to_owned),
                    install_location.map(str::to_owned),
                ),
            );
        }
    }

    impl Registry for FakeRegistry<'_> {
        fn uninstall_values(&self, subkey: &str) -> Option<(Option<String>, Option<String>)> {
            self.world.record(Call::UninstallValues(subkey.to_owned()));
            lock(&self.world.registry).get(subkey).cloned()
        }

        fn key_exists(&self, subkey: &str) -> bool {
            self.world.record(Call::KeyExists(subkey.to_owned()));
            lock(&self.world.registry).contains_key(subkey)
        }
    }
}

#[cfg(test)]
#[path = "seams_tests.rs"]
mod tests;
