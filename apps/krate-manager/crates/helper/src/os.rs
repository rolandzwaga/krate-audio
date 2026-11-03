//! The real seams (plan §4.1.12; PD-6, PD-7): `cfg(unix)` / `cfg(windows)`, never `cfg(target_os = "macos")`.
//! Constructed only in `main.rs` and `os_tests.rs` (FR-053, SI-25). The helper's only `Instant` home (FR-041).

use crate::seams::{ChildId, HelperIo, ProcessRunner, Registry, WriteSync};
use crate::validate::{Kind, LstatFacts};
use std::collections::BTreeMap;
use std::collections::hash_map::RandomState;
use std::ffi::OsString;
use std::fs::{File, OpenOptions};
use std::hash::BuildHasher;
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitStatus, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc;
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::{Duration, Instant};

/// `open(2)` flags for PD-6, from `libc-0.2.191` (the workspace has no `libc`): Apple `src/unix/bsd/mod.rs:275-276`.
#[cfg(all(unix, target_vendor = "apple"))]
const O_NONBLOCK: i32 = 0x4;
#[cfg(all(unix, target_vendor = "apple"))]
const O_NOFOLLOW: i32 = 0x100;
/// Linux x86_64: `src/unix/linux_like/linux/gnu/b64/x86_64/mod.rs:294`, `:448`.
#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
const O_NONBLOCK: i32 = 2048;
#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
const O_NOFOLLOW: i32 = 0x20000;
/// Linux aarch64: `src/unix/linux_like/linux/gnu/b64/aarch64/mod.rs:206`, `:359`.
#[cfg(all(target_os = "linux", target_arch = "aarch64"))]
const O_NONBLOCK: i32 = 2048;
#[cfg(all(target_os = "linux", target_arch = "aarch64"))]
const O_NOFOLLOW: i32 = 0x8000;
#[cfg(all(
    unix,
    not(target_vendor = "apple"),
    not(all(
        target_os = "linux",
        any(target_arch = "x86_64", target_arch = "aarch64")
    ))
))]
compile_error!(
    "os.rs: O_NONBLOCK / O_NOFOLLOW are defined for Apple and Linux x86_64 / aarch64 only (PD-6)"
);

/// How often `OsRunner` polls a child (plan §4.1.12).
const POLL: Duration = Duration::from_millis(100);

/// The process counter `random_hex` hashes (PD-19).
static NAME_COUNTER: AtomicU64 = AtomicU64::new(0);
/// The number of the next spawned child.
static NEXT_CHILD: AtomicU64 = AtomicU64::new(0);

/// The Windows known folders the production roots use (tasks TA-6); mapped to `FOLDERID_*` only here.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KnownFolder {
    ProgramFilesCommon,
    ProgramData,
    UserProfiles,
    Windows,
}

/// The known folder's path, `None` when the lookup fails (FR-051).
#[cfg(windows)]
pub fn known_folder(id: KnownFolder) -> Option<PathBuf> {
    windows::known_folder(id)
}

/// The refusal of a path that is not a regular file reached without a final link.
fn not_regular(path: &Path) -> io::Error {
    io::Error::new(
        io::ErrorKind::InvalidInput,
        format!(
            "not a regular file opened without a link: {}",
            path.display()
        ),
    )
}

/// PD-6: open `path` for reading without following a final link and without blocking; a regular file only.
#[cfg(unix)]
pub fn open_regular_no_follow(path: &Path) -> io::Result<File> {
    use std::os::unix::fs::{MetadataExt, OpenOptionsExt};
    let file = OpenOptions::new()
        .read(true)
        .custom_flags(O_NONBLOCK | O_NOFOLLOW)
        .open(path)?;
    let opened = file.metadata()?;
    let named = std::fs::symlink_metadata(path)?;
    if !opened.file_type().is_file() || opened.dev() != named.dev() || opened.ino() != named.ino() {
        return Err(not_regular(path));
    }
    Ok(file)
}

/// PD-6: open `path` for reading without following a final link; a regular file only.
#[cfg(windows)]
pub fn open_regular_no_follow(path: &Path) -> io::Result<File> {
    use std::os::windows::fs::OpenOptionsExt;
    let file = OpenOptions::new()
        .read(true)
        .custom_flags(windows::FILE_FLAG_OPEN_REPARSE_POINT)
        .open(path)?;
    let kind = file.metadata()?.file_type();
    if kind.is_symlink() || !kind.is_file() {
        return Err(not_regular(path));
    }
    Ok(file)
}

/// A created file whose bytes can be made durable.
struct SyncFile(File);

impl Write for SyncFile {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.0.write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.0.flush()
    }
}

impl WriteSync for SyncFile {
    fn sync_all(&mut self) -> io::Result<()> {
        self.0.sync_all()
    }
}

/// The real filesystem, clock and randomness.
pub struct OsHelperIo {
    origin: Instant,
}

impl OsHelperIo {
    pub fn new() -> OsHelperIo {
        OsHelperIo {
            origin: Instant::now(),
        }
    }
}

impl Default for OsHelperIo {
    fn default() -> OsHelperIo {
        OsHelperIo::new()
    }
}

/// `symlink_metadata`'s kind, a link first.
fn kind_of(meta: &std::fs::Metadata) -> Kind {
    let t = meta.file_type();
    if t.is_symlink() {
        Kind::Symlink
    } else if t.is_dir() {
        Kind::Dir
    } else if t.is_file() {
        Kind::File
    } else {
        Kind::Other
    }
}

/// The owner and the permission bits on Unix.
#[cfg(unix)]
fn owner_of(meta: &std::fs::Metadata) -> (Option<u32>, Option<u32>) {
    use std::os::unix::fs::MetadataExt;
    (Some(meta.uid()), Some(meta.mode() & 0o7777))
}

/// Windows reports no uid / mode (`LstatFacts`).
#[cfg(windows)]
fn owner_of(_meta: &std::fs::Metadata) -> (Option<u32>, Option<u32>) {
    (None, None)
}

/// A directory with `mode` (before the umask); fails on an existing name.
#[cfg(unix)]
fn create_dir_mode(path: &Path, mode: u32) -> io::Result<()> {
    use std::os::unix::fs::DirBuilderExt;
    std::fs::DirBuilder::new().mode(mode).create(path)
}

impl HelperIo for OsHelperIo {
    fn read_bounded(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        let mut bytes = Vec::new();
        open_regular_no_follow(path)?
            .take(max.saturating_add(1))
            .read_to_end(&mut bytes)?;
        Ok(bytes)
    }

    fn read_tail(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        let mut file = open_regular_no_follow(path)?;
        let len = file.metadata()?.len();
        file.seek(SeekFrom::Start(len.saturating_sub(max)))?;
        let mut bytes = Vec::new();
        file.take(max).read_to_end(&mut bytes)?;
        Ok(bytes)
    }

    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        std::fs::canonicalize(path)
    }

    fn lstat(&self, path: &Path) -> io::Result<Option<LstatFacts>> {
        let meta = match std::fs::symlink_metadata(path) {
            Ok(meta) => meta,
            Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
            Err(e) => return Err(e),
        };
        let (uid, mode) = owner_of(&meta);
        Ok(Some(LstatFacts {
            kind: kind_of(&meta),
            uid,
            mode,
        }))
    }

    fn open_source(&self, path: &Path) -> io::Result<Box<dyn Read + '_>> {
        Ok(Box::new(open_regular_no_follow(path)?))
    }

    fn create_new(&self, path: &Path) -> io::Result<Box<dyn WriteSync + '_>> {
        let file = OpenOptions::new().write(true).create_new(true).open(path)?;
        Ok(Box::new(SyncFile(file)))
    }

    fn create_helper_folder(&self, path: &Path) -> io::Result<()> {
        #[cfg(unix)]
        let made = create_dir_mode(path, 0o755);
        #[cfg(windows)]
        let made = std::fs::create_dir(path);
        match made {
            Err(e) if e.kind() == io::ErrorKind::AlreadyExists => Ok(()),
            other => other,
        }
    }

    #[cfg(unix)]
    fn create_run_dir(&self, path: &Path) -> io::Result<()> {
        create_dir_mode(path, 0o700)
    }

    #[cfg(windows)]
    fn create_run_dir(&self, path: &Path) -> io::Result<()> {
        windows::create_run_dir(path)
    }

    fn list_dir(&self, dir: &Path) -> io::Result<Vec<OsString>> {
        std::fs::read_dir(dir)?
            .map(|entry| entry.map(|e| e.file_name()))
            .collect()
    }

    fn remove_file(&self, path: &Path) -> io::Result<()> {
        std::fs::remove_file(path)
    }

    fn remove_dir(&self, path: &Path) -> io::Result<()> {
        std::fs::remove_dir(path)
    }

    #[cfg(windows)]
    fn in_use(&self, path: &Path) -> io::Result<bool> {
        windows::in_use(path)
    }

    /// Phase 12 FR-055: only Windows refuses to delete a file another process holds.
    #[cfg(not(windows))]
    fn in_use(&self, _path: &Path) -> io::Result<bool> {
        Ok(false)
    }

    fn random_hex(&self) -> String {
        let n = NAME_COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let high = RandomState::new().hash_one((n, pid));
        let low = RandomState::new().hash_one((pid, n));
        format!("{high:016x}{low:016x}")
    }

    fn sleep(&self, d: Duration) {
        std::thread::sleep(d);
    }

    fn now(&self) -> Duration {
        self.origin.elapsed()
    }
}

/// A child's exit code: its code, else `128 + signal` (Unix).
#[cfg(unix)]
fn exit_code(status: ExitStatus) -> i32 {
    use std::os::unix::process::ExitStatusExt;
    status
        .code()
        .or_else(|| status.signal().map(|s| 128 + s))
        .unwrap_or(-1)
}

/// A child's exit code (always present on Windows).
#[cfg(windows)]
fn exit_code(status: ExitStatus) -> i32 {
    status.code().unwrap_or(-1)
}

/// `argv[0]` with `argv[1..]`, never a shell; stdin and stderr null.
fn command(argv: &[OsString], stdout: Stdio) -> io::Result<Command> {
    let Some((program, args)) = argv.split_first() else {
        return Err(io::Error::new(io::ErrorKind::InvalidInput, "empty argv"));
    };
    let mut command = Command::new(program);
    command
        .args(args)
        .stdin(Stdio::null())
        .stdout(stdout)
        .stderr(Stdio::null());
    Ok(command)
}

/// Real child processes: `Command::new(argv[0])`, all stdio null, never a shell, never a kill (FR-042).
pub struct OsRunner {
    children: Mutex<BTreeMap<u64, Child>>,
}

impl OsRunner {
    pub fn new() -> OsRunner {
        OsRunner {
            children: Mutex::new(BTreeMap::new()),
        }
    }

    fn children(&self) -> MutexGuard<'_, BTreeMap<u64, Child>> {
        self.children.lock().unwrap_or_else(PoisonError::into_inner)
    }
}

impl Default for OsRunner {
    fn default() -> OsRunner {
        OsRunner::new()
    }
}

impl ProcessRunner for OsRunner {
    fn spawn(&self, argv: &[OsString]) -> io::Result<ChildId> {
        let child = command(argv, Stdio::null())?.spawn()?;
        let id = NEXT_CHILD.fetch_add(1, Ordering::Relaxed);
        self.children().insert(id, child);
        Ok(ChildId(id))
    }

    fn wait(&self, child: ChildId, timeout: Duration) -> io::Result<Option<i32>> {
        let start = Instant::now();
        loop {
            {
                let mut children = self.children();
                let Some(running) = children.get_mut(&child.0) else {
                    return Err(io::Error::new(io::ErrorKind::NotFound, "no such child"));
                };
                if let Some(status) = running.try_wait()? {
                    children.remove(&child.0);
                    return Ok(Some(exit_code(status)));
                }
            }
            let elapsed = start.elapsed();
            if elapsed >= timeout {
                return Ok(None);
            }
            std::thread::sleep(POLL.min(timeout - elapsed));
        }
    }

    fn output(&self, argv: &[OsString], timeout: Duration) -> io::Result<Option<(i32, Vec<u8>)>> {
        let start = Instant::now();
        let mut child = command(argv, Stdio::piped())?.spawn()?;
        let Some(mut stdout) = child.stdout.take() else {
            return Err(io::Error::other("the child's stdout was not piped"));
        };
        // The pipe is drained beside the wait, so a child that fills it never blocks; a reader left behind by a
        // timeout ends when the child closes its end.
        let (send, receive) = mpsc::channel();
        std::thread::spawn(move || {
            let mut bytes = Vec::new();
            let read = stdout.read_to_end(&mut bytes).map(|_| bytes);
            // The receiver is gone only after a timeout, when nobody wants the bytes.
            let _ = send.send(read);
        });
        let status = loop {
            if let Some(status) = child.try_wait()? {
                break status;
            }
            let elapsed = start.elapsed();
            if elapsed >= timeout {
                return Ok(None);
            }
            std::thread::sleep(POLL.min(timeout - elapsed));
        };
        let left = timeout.saturating_sub(start.elapsed());
        match receive.recv_timeout(left) {
            Ok(read) => Ok(Some((exit_code(status), read?))),
            Err(mpsc::RecvTimeoutError::Timeout) => Ok(None),
            Err(mpsc::RecvTimeoutError::Disconnected) => {
                Err(io::Error::other("the stdout reader ended without a result"))
            }
        }
    }
}

/// HKLM, 64-bit view, on Windows; `None` / `false` elsewhere.
pub struct OsRegistry;

/// The two values FR-031 reads from an uninstall key.
#[cfg(windows)]
const UNINSTALL_VALUES: [&str; 2] = ["UninstallString", "InstallLocation"];

impl Registry for OsRegistry {
    #[cfg(windows)]
    fn uninstall_values(&self, subkey: &str) -> Option<(Option<String>, Option<String>)> {
        let key = windows::OpenKey::open(subkey).ok()??;
        let [uninstall_string, install_location] = UNINSTALL_VALUES;
        Some((key.read_sz(uninstall_string), key.read_sz(install_location)))
    }

    #[cfg(not(windows))]
    fn uninstall_values(&self, _subkey: &str) -> Option<(Option<String>, Option<String>)> {
        None
    }

    /// A key that cannot be opened for a reason other than its absence counts as present, so the uninstall poll
    /// never reads a failure as a removal.
    #[cfg(windows)]
    fn key_exists(&self, subkey: &str) -> bool {
        !matches!(windows::OpenKey::open(subkey), Ok(None))
    }

    #[cfg(not(windows))]
    fn key_exists(&self, _subkey: &str) -> bool {
        false
    }
}

/// The Windows FFI of the real seams: the run directory's DACL, known folders, HKLM reads, the in-use check.
#[cfg(windows)]
mod windows {
    use super::KnownFolder;
    use crate::validate::RUN_DIR_SDDL;
    use std::ffi::OsString;
    use std::io;
    use std::os::windows::ffi::{OsStrExt, OsStringExt};
    use std::path::{Path, PathBuf};
    use std::ptr::{null, null_mut};
    use windows_sys::Win32::Foundation::{
        ERROR_FILE_NOT_FOUND, ERROR_LOCK_VIOLATION, ERROR_SHARING_VIOLATION, ERROR_SUCCESS,
        LocalFree,
    };
    use windows_sys::Win32::Security::Authorization::{
        ConvertStringSecurityDescriptorToSecurityDescriptorW, SDDL_REVISION_1,
    };
    use windows_sys::Win32::Security::{PSECURITY_DESCRIPTOR, SECURITY_ATTRIBUTES};
    use windows_sys::Win32::Storage::FileSystem::{
        CreateDirectoryW, FILE_SHARE_DELETE, FILE_SHARE_WRITE,
    };
    pub use windows_sys::Win32::Storage::FileSystem::{
        FILE_FLAG_OPEN_REPARSE_POINT, FILE_SHARE_READ,
    };
    use windows_sys::Win32::System::Com::CoTaskMemFree;
    use windows_sys::Win32::System::Registry::{
        HKEY, HKEY_LOCAL_MACHINE, KEY_READ, KEY_WOW64_64KEY, RRF_RT_REG_SZ, RegCloseKey,
        RegGetValueW, RegOpenKeyExW,
    };
    use windows_sys::Win32::UI::Shell::{
        FOLDERID_ProgramData, FOLDERID_ProgramFilesCommon, FOLDERID_UserProfiles, FOLDERID_Windows,
        KF_FLAG_DEFAULT, SHGetKnownFolderPath,
    };

    /// UTF-16 plus a NUL.
    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(Some(0)).collect()
    }

    /// A path as UTF-16 plus a NUL; an interior NUL would cut the path short, so it is refused.
    fn wide_path(path: &Path) -> io::Result<Vec<u16>> {
        let units: Vec<u16> = path.as_os_str().encode_wide().collect();
        if units.contains(&0) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "a NUL in the path",
            ));
        }
        Ok(units.into_iter().chain(Some(0)).collect())
    }

    /// Phase 12 FR-055: a write open (no truncation, nothing written) sharing everything; another process holding
    /// the file without write sharing refuses it with `ERROR_SHARING_VIOLATION`, and so does a loaded DLL's image
    /// mapping (measured 2026-11-03: a DLL loaded in a second process refused the write open and the delete, and
    /// allowed a read open and a rename). Absent → not in use.
    pub fn in_use(path: &Path) -> io::Result<bool> {
        use std::os::windows::fs::OpenOptionsExt;
        let opened = std::fs::OpenOptions::new()
            .write(true)
            .share_mode(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
            .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT)
            .open(path);
        match opened {
            Ok(_) => Ok(false),
            Err(e)
                if e.raw_os_error() == Some(ERROR_SHARING_VIOLATION as i32)
                    || e.raw_os_error() == Some(ERROR_LOCK_VIOLATION as i32) =>
            {
                Ok(true)
            }
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(false),
            Err(e) => Err(e),
        }
    }

    /// D7: the directory is created with `RUN_DIR_SDDL` as its DACL (protected: SYSTEM and Administrators only);
    /// it fails on an existing name.
    pub fn create_run_dir(path: &Path) -> io::Result<()> {
        let path = wide_path(path)?;
        let sddl = wide(RUN_DIR_SDDL);
        let mut descriptor: PSECURITY_DESCRIPTOR = null_mut();
        // SAFETY: `sddl` is NUL-terminated and outlives the call; `descriptor` is a valid out pointer that receives
        // a LocalAlloc'd descriptor, freed by the LocalFree below on every path after a success; the size out
        // pointer may be NULL (the ConvertStringSecurityDescriptorToSecurityDescriptorW page).
        let converted = unsafe {
            ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl.as_ptr(),
                SDDL_REVISION_1,
                &mut descriptor,
                null_mut(),
            )
        };
        if converted == 0 {
            return Err(io::Error::last_os_error());
        }
        let attributes = SECURITY_ATTRIBUTES {
            nLength: size_of::<SECURITY_ATTRIBUTES>() as u32,
            lpSecurityDescriptor: descriptor,
            bInheritHandle: 0,
        };
        // SAFETY: `path` is NUL-terminated and outlives the call; `attributes` points at the descriptor converted
        // above, which stays allocated until the LocalFree after this call.
        let created = unsafe { CreateDirectoryW(path.as_ptr(), &attributes) };
        let outcome = if created == 0 {
            Err(io::Error::last_os_error())
        } else {
            Ok(())
        };
        // SAFETY: `descriptor` is the LocalAlloc'd block the conversion returned, freed exactly once here and not
        // used afterwards; LocalFree's return (NULL on success) carries nothing to act on.
        unsafe { LocalFree(descriptor) };
        outcome
    }

    /// TA-6: `SHGetKnownFolderPath` for `id`; the returned buffer is freed with `CoTaskMemFree` on every path.
    pub fn known_folder(id: KnownFolder) -> Option<PathBuf> {
        let guid = match id {
            KnownFolder::ProgramFilesCommon => FOLDERID_ProgramFilesCommon,
            KnownFolder::ProgramData => FOLDERID_ProgramData,
            KnownFolder::UserProfiles => FOLDERID_UserProfiles,
            KnownFolder::Windows => FOLDERID_Windows,
        };
        let mut buffer: *mut u16 = null_mut();
        // SAFETY: `guid` outlives the call; a NULL token names the current user; `buffer` is a valid out pointer
        // that receives a CoTaskMemAlloc'd, NUL-terminated string, which the caller frees with CoTaskMemFree
        // whether the call succeeds or not (the SHGetKnownFolderPath page).
        let hr =
            unsafe { SHGetKnownFolderPath(&guid, KF_FLAG_DEFAULT as u32, null_mut(), &mut buffer) };
        let path = if hr >= 0 && !buffer.is_null() {
            let mut len = 0usize;
            // SAFETY: on success `buffer` is a NUL-terminated UTF-16 string, so every unit up to and including
            // the NUL is readable.
            while unsafe { *buffer.add(len) } != 0 {
                len += 1;
            }
            // SAFETY: the `len` units before the NUL were just read one by one; the buffer is not freed yet.
            let units = unsafe { std::slice::from_raw_parts(buffer, len) };
            Some(PathBuf::from(OsString::from_wide(units)))
        } else {
            None
        };
        // SAFETY: `buffer` is NULL or the block SHGetKnownFolderPath allocated; CoTaskMemFree accepts NULL, the
        // block is freed exactly once here, and `units` (the only view of it) is dropped above.
        unsafe { CoTaskMemFree(buffer.cast()) };
        path
    }

    /// An HKLM key opened in the 64-bit view, closed exactly once when dropped.
    pub struct OpenKey(HKEY);

    impl Drop for OpenKey {
        fn drop(&mut self) {
            // SAFETY: `self.0` is the handle a successful RegOpenKeyExW returned; only `OpenKey::open` builds an
            // `OpenKey`, once per open, and nothing uses the handle after this drop. The code carries nothing to
            // act on.
            unsafe { RegCloseKey(self.0) };
        }
    }

    impl OpenKey {
        /// `Ok(None)` when the key does not exist; `Err` for any other failure.
        pub fn open(subkey: &str) -> Result<Option<OpenKey>, u32> {
            let subkey = wide(subkey);
            let mut hkey: HKEY = null_mut();
            // SAFETY: `subkey` is NUL-terminated and outlives the call; `hkey` is a valid out pointer that
            // receives the opened handle, which `OpenKey` closes on drop (the RegOpenKeyExW page).
            let rc = unsafe {
                RegOpenKeyExW(
                    HKEY_LOCAL_MACHINE,
                    subkey.as_ptr(),
                    0,
                    KEY_READ | KEY_WOW64_64KEY,
                    &mut hkey,
                )
            };
            match rc {
                ERROR_SUCCESS => Ok(Some(OpenKey(hkey))),
                ERROR_FILE_NOT_FOUND => Ok(None),
                other => Err(other),
            }
        }

        /// A `REG_SZ` value; `None` when absent or unreadable.
        pub fn read_sz(&self, name: &str) -> Option<String> {
            let name = wide(name);
            let mut size: u32 = 0;
            // SAFETY: `self.0` is open; `name` is NUL-terminated and outlives the call; a NULL subkey reads the key
            // itself; with a NULL data pointer the call only stores the value's size in bytes in `size` (the
            // RegGetValueW page).
            let rc = unsafe {
                RegGetValueW(
                    self.0,
                    null(),
                    name.as_ptr(),
                    RRF_RT_REG_SZ,
                    null_mut(),
                    null_mut(),
                    &mut size,
                )
            };
            if rc != ERROR_SUCCESS {
                return None;
            }
            let mut data = vec![0u16; (size as usize).div_ceil(2)];
            // SAFETY: `data` holds at least `size` bytes and is writable for the call; `size` says so, and the call
            // writes at most that many bytes (it fails with ERROR_MORE_DATA instead of writing past it).
            let rc = unsafe {
                RegGetValueW(
                    self.0,
                    null(),
                    name.as_ptr(),
                    RRF_RT_REG_SZ,
                    null_mut(),
                    data.as_mut_ptr().cast(),
                    &mut size,
                )
            };
            if rc != ERROR_SUCCESS {
                return None;
            }
            let len = data
                .iter()
                .position(|&unit| unit == 0)
                .unwrap_or(data.len());
            Some(String::from_utf16_lossy(&data[..len]))
        }
    }
}

#[cfg(test)]
#[path = "os_tests.rs"]
mod tests;
