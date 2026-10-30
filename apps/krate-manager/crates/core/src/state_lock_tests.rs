use super::*;
use crate::test_support::fresh_temp_roots;
use std::fs;
use std::path::{Path, PathBuf};

/// Recursive listing of `dir`: (relative path, is_dir, size in bytes), sorted. A directory's
/// size is recorded as 0: its `len()` is filesystem-dependent (4096 on ext4).
fn listing(dir: &Path) -> Vec<(PathBuf, bool, u64)> {
    fn walk(root: &Path, dir: &Path, out: &mut Vec<(PathBuf, bool, u64)>) {
        for entry in fs::read_dir(dir).expect("read_dir") {
            let entry = entry.expect("dir entry");
            let path = entry.path();
            let meta = fs::symlink_metadata(&path).expect("symlink_metadata");
            let rel = path.strip_prefix(root).expect("strip_prefix").to_path_buf();
            let size = if meta.is_dir() { 0 } else { meta.len() };
            out.push((rel, meta.is_dir(), size));
            if meta.is_dir() {
                walk(root, &path, out);
            }
        }
    }
    let mut out = Vec::new();
    walk(dir, dir, &mut out);
    out.sort();
    out
}

fn base_of(roots: &Roots) -> PathBuf {
    roots
        .state_dir()
        .parent()
        .expect("the state dir has a parent")
        .to_path_buf()
}

#[test]
fn second_acquire_is_held_while_first_lives() {
    let (_td, roots) = fresh_temp_roots();
    let first = StateLock::acquire(&roots).expect("the first acquire succeeds");
    let second = StateLock::acquire(&roots);
    assert!(
        matches!(second, Err(LockError::Held)),
        "the second acquire must be Held, got {second:?}"
    );
    drop(first);
    let third = StateLock::acquire(&roots);
    assert!(
        third.is_ok(),
        "after the first is dropped a new acquire succeeds, got {third:?}"
    );
}

#[test]
fn acquire_creates_state_dir_and_lock_file_only() {
    let (_td, roots) = fresh_temp_roots();
    let base = base_of(&roots);
    fs::remove_dir_all(roots.state_dir()).expect("remove the state dir");
    let before = listing(&base);
    let lock = StateLock::acquire(&roots).expect("acquire over an absent state dir");
    let after = listing(&base);
    let added: Vec<_> = after
        .iter()
        .filter(|e| !before.contains(e))
        .cloned()
        .collect();
    let removed: Vec<_> = before.iter().filter(|e| !after.contains(e)).collect();
    assert!(removed.is_empty(), "nothing disappears: {removed:?}");
    let state = PathBuf::from("state");
    assert_eq!(
        added,
        vec![(state.clone(), true, 0), (state.join(LOCK_FILE), false, 0)],
        "exactly state/ and a zero-byte state/manager.lock appear"
    );
    drop(lock);
}

#[test]
fn lock_file_survives_drop() {
    let (_td, roots) = fresh_temp_roots();
    let path = roots.state_dir().join(LOCK_FILE);
    let lock = StateLock::acquire(&roots).expect("acquire");
    drop(lock);
    let meta = fs::metadata(&path).expect("the lock file still exists after the drop");
    assert!(meta.is_file(), "the lock file is a regular file");
    assert_eq!(meta.len(), 0, "the lock file is never written");
}

#[test]
fn acquire_io_error_is_named() {
    let (_td, roots) = fresh_temp_roots();
    fs::remove_dir_all(roots.state_dir()).expect("remove the state dir");
    fs::write(roots.state_dir(), b"not a directory").expect("plant a regular file");
    let result = StateLock::acquire(&roots);
    assert!(
        matches!(
            result,
            Err(LockError::Io {
                op: LockStep::CreateDir,
                ..
            })
        ),
        "a regular file at the state dir is Io at create-dir, got {result:?}"
    );
    assert_eq!(LockStep::CreateDir.as_str(), "create-dir");
    assert_eq!(
        fs::read(roots.state_dir()).expect("the planted file is untouched"),
        b"not a directory"
    );
}
