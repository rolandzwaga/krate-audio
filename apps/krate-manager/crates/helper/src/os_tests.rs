//! `os::tests` (owner T008; plan §4.1.12, §7.1; SI-25): the real seams, unelevated, over a `TempDir`. The one test
//! source that may build `OsHelperIo` and `OsRunner`; no row names a production path.

use super::OsHelperIo;
use crate::seams::HelperIo;

/// A `TempDir` for one test.
fn temp() -> tempfile::TempDir {
    tempfile::tempdir().expect("a temp dir")
}

#[test]
fn read_bounded_stops_at_bound() {
    const MAX: u64 = 4096;
    let td = temp();
    let path = td.path().join("big.bin");
    let bytes: Vec<u8> = (0..2 * MAX).map(|i| (i % 251) as u8).collect();
    std::fs::write(&path, &bytes).expect("write the 2 x MAX file");
    let io = OsHelperIo::new();
    let max = usize::try_from(MAX).expect("MAX fits usize");

    let head = io.read_bounded(&path, MAX).expect("read_bounded");
    assert_eq!(head.len(), max + 1, "read_bounded reads MAX + 1 bytes");
    assert_eq!(head, bytes[..=max], "read_bounded reads from the start");

    let tail = io.read_tail(&path, MAX).expect("read_tail");
    assert_eq!(tail.len(), max, "read_tail reads MAX bytes");
    assert_eq!(tail, bytes[max..], "read_tail reads the last MAX bytes");

    let short = td.path().join("short.bin");
    std::fs::write(&short, b"abc").expect("write the short file");
    assert_eq!(io.read_bounded(&short, MAX).expect("short head"), b"abc");
    assert_eq!(io.read_tail(&short, MAX).expect("short tail"), b"abc");
}

#[test]
fn random_hex_is_32_lower_hex_and_varies() {
    let io = OsHelperIo::new();
    let first = io.random_hex();
    let second = io.random_hex();
    for name in [&first, &second] {
        assert_eq!(name.len(), 32, "{name:?} is 32 digits");
        assert!(
            name.bytes()
                .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b)),
            "{name:?} is lower-case hex"
        );
    }
    assert_ne!(first, second, "two calls differ");
}

#[test]
fn lstat_reports_kind_and_absence() {
    use crate::validate::Kind;
    let td = temp();
    let io = OsHelperIo::new();
    let file = td.path().join("f.bin");
    std::fs::write(&file, b"x").expect("write");
    let facts = io.lstat(&file).expect("lstat file").expect("present");
    assert_eq!(facts.kind, Kind::File);
    assert_eq!(facts.uid.is_some(), cfg!(unix), "uid only on Unix");
    assert_eq!(facts.mode.is_some(), cfg!(unix), "mode only on Unix");
    let dir = io.lstat(td.path()).expect("lstat dir").expect("present");
    assert_eq!(dir.kind, Kind::Dir);
    assert_eq!(
        io.lstat(&td.path().join("absent")).expect("lstat absent"),
        None
    );
}

#[cfg(windows)]
#[test]
fn open_source_refuses_link_windows() {
    use super::open_regular_no_follow;
    use std::io::Read;
    let td = temp();
    let target = td.path().join("target.bin");
    std::fs::write(&target, b"payload").expect("write the target");
    let link = td.path().join("link.bin");
    std::os::windows::fs::symlink_file(&target, &link).unwrap_or_else(|e| {
        panic!("symbolic-link privilege not held: enable Developer Mode ({e})")
    });
    let io = OsHelperIo::new();

    assert!(
        open_regular_no_follow(&link).is_err(),
        "the link is refused"
    );
    assert!(
        io.open_source(&link).is_err(),
        "open_source refuses the link"
    );
    assert!(
        io.read_bounded(&link, 64).is_err(),
        "read_bounded refuses the link"
    );
    assert!(
        io.read_tail(&link, 64).is_err(),
        "read_tail refuses the link"
    );
    assert!(
        open_regular_no_follow(td.path()).is_err(),
        "a directory is refused"
    );

    let mut opened = io.open_source(&target).expect("the target opens");
    let mut bytes = Vec::new();
    opened.read_to_end(&mut bytes).expect("read the target");
    assert_eq!(bytes, b"payload");
}

#[cfg(windows)]
#[test]
fn known_folder_resolves_each_windows() {
    use super::{KnownFolder, known_folder};
    for id in [
        KnownFolder::ProgramFilesCommon,
        KnownFolder::ProgramData,
        KnownFolder::UserProfiles,
        KnownFolder::Windows,
    ] {
        let path = known_folder(id).unwrap_or_else(|| panic!("{id:?} resolves"));
        assert!(path.is_absolute(), "{id:?} -> {path:?} is absolute");
    }
}

#[cfg(unix)]
#[test]
fn open_source_refuses_links_and_fifos_unix() {
    use super::open_regular_no_follow;
    use std::io::Read;
    use std::time::{Duration, Instant};
    let td = temp();
    let target = td.path().join("target.bin");
    std::fs::write(&target, b"payload").expect("write the target");
    let link = td.path().join("link.bin");
    std::os::unix::fs::symlink(&target, &link).expect("a link");
    let io = OsHelperIo::new();

    assert!(
        open_regular_no_follow(&link).is_err(),
        "the link is refused"
    );
    assert!(
        io.open_source(&link).is_err(),
        "open_source refuses the link"
    );
    assert!(
        io.read_bounded(&link, 64).is_err(),
        "read_bounded refuses the link"
    );

    let fifo = td.path().join("pipe");
    let made = std::process::Command::new("/usr/bin/mkfifo")
        .arg(&fifo)
        .status()
        .expect("mkfifo runs");
    assert!(made.success(), "mkfifo made the FIFO");
    let started = Instant::now();
    assert!(io.open_source(&fifo).is_err(), "the FIFO is refused");
    assert!(
        io.read_bounded(&fifo, 64).is_err(),
        "read_bounded refuses the FIFO"
    );
    assert!(
        started.elapsed() < Duration::from_secs(1),
        "the FIFO refusals returned in {:?}, under 1 s",
        started.elapsed()
    );

    let mut opened = io.open_source(&target).expect("the regular file opens");
    let mut bytes = Vec::new();
    opened.read_to_end(&mut bytes).expect("read the target");
    assert_eq!(bytes, b"payload");
}

#[cfg(unix)]
#[test]
fn run_dir_mode_unix() {
    use crate::validate::Kind;
    use std::os::unix::fs::MetadataExt;
    let td = temp();
    let io = OsHelperIo::new();
    let run = td.path().join("run-0123456789abcdef0123456789abcdef");
    io.create_run_dir(&run).expect("create_run_dir");
    let mode = std::fs::symlink_metadata(&run)
        .expect("lstat the run dir")
        .mode();
    assert_eq!(mode & 0o777, 0o700, "mode {mode:o}");
    let facts = io.lstat(&run).expect("lstat").expect("present");
    assert_eq!(facts.kind, Kind::Dir);
    assert_eq!(facts.mode.map(|m| m & 0o777), Some(0o700));
    assert!(
        io.create_run_dir(&run).is_err(),
        "a second call on the name fails"
    );

    let folder = td.path().join("helper-folder");
    io.create_helper_folder(&folder)
        .expect("create_helper_folder");
    io.create_helper_folder(&folder).expect("present is Ok");
    let mode = std::fs::symlink_metadata(&folder)
        .expect("lstat the folder")
        .mode();
    assert_eq!(
        mode & 0o022,
        0,
        "the helper folder is not group / other writable: {mode:o}"
    );
}

#[cfg(unix)]
#[test]
fn wait_timeout_leaves_child_unix() {
    use super::OsRunner;
    use crate::seams::ProcessRunner;
    use std::ffi::OsString;
    use std::time::Duration;
    let runner = OsRunner::new();
    let argv: Vec<OsString> = ["/bin/sleep", "30"]
        .into_iter()
        .map(OsString::from)
        .collect();
    let id = runner.spawn(&argv).expect("spawn sleep");
    assert_eq!(
        runner.wait(id, Duration::from_millis(200)).expect("wait"),
        None
    );
    assert_eq!(
        runner.wait(id, Duration::ZERO).expect("second wait"),
        None,
        "the child was not killed"
    );
    let mut child = runner
        .children
        .lock()
        .expect("the children map")
        .remove(&id.0)
        .expect("the child is still held");
    assert_eq!(child.try_wait().expect("try_wait"), None, "still running");
    child.kill().expect("kill the child");
    child.wait().expect("reap the child");
}

/// Phase 12 FR-055: the running test binary is a mapped image, which refuses a write open like a DAW's loaded
/// plug-in; a file held by a handle without write sharing is in use until the handle closes; nothing is written.
#[cfg(windows)]
#[test]
fn in_use_sees_a_mapped_image_and_a_held_file() {
    use super::windows::FILE_SHARE_READ;
    use std::os::windows::fs::OpenOptionsExt;
    let io = OsHelperIo::new();
    let exe = std::env::current_exe().expect("the test binary");
    assert!(
        io.in_use(&exe).expect("in_use on the test binary"),
        "a mapped image is in use"
    );

    let td = temp();
    let path = td.path().join("plugin.vst3");
    std::fs::write(&path, b"plugin binary").expect("write the file");
    assert!(
        !io.in_use(&path).expect("in_use, free"),
        "a free file is not in use"
    );
    let held = std::fs::OpenOptions::new()
        .read(true)
        .share_mode(FILE_SHARE_READ)
        .open(&path)
        .expect("hold the file without write sharing");
    assert!(
        io.in_use(&path).expect("in_use, held"),
        "a held file is in use"
    );
    drop(held);
    assert!(!io.in_use(&path).expect("in_use, released"), "released");
    assert_eq!(
        std::fs::read(&path).expect("read back"),
        b"plugin binary",
        "nothing written"
    );
    assert!(
        !io.in_use(&td.path().join("absent.vst3"))
            .expect("in_use, absent"),
        "absent"
    );
}

/// Phase 12 FR-055: off Windows nothing is in use (a held file can be deleted there).
#[cfg(not(windows))]
#[test]
fn in_use_is_false_off_windows() {
    let io = OsHelperIo::new();
    let exe = std::env::current_exe().expect("the test binary");
    assert!(!io.in_use(&exe).expect("in_use"));
}
