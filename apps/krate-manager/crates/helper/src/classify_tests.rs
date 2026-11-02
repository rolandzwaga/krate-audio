//! `classify::tests` (plan §7.1 SC-016 row): the measured rule (M-1, FR-062) over the recorded fixtures.

use super::*;
use crate::result;
use std::fs;
use std::path::{Path, PathBuf};

fn inno_locked(file: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/helper/inno-locked")
        .join(file)
}

fn read(file: &str) -> String {
    fs::read_to_string(inno_locked(file)).unwrap_or_else(|e| panic!("{file}: {e}"))
}

#[test]
fn recorded_locked_case() {
    assert_eq!(
        LOCKED_LINE,
        read("locked_line.txt"),
        "LOCKED_LINE equals locked_line.txt (SI-26)"
    );
    let exit: i32 = read("exit_code.txt")
        .trim()
        .parse()
        .expect("exit_code.txt is an integer");
    assert_eq!(LOCKED_EXIT, exit);
    let log = result::log_lines(read("log.txt").as_bytes());
    assert_eq!(
        classify_inno(exit, &log),
        InnoVerdict::Locked,
        "the recorded run is Locked"
    );

    // The same code without the recorded line is a plain failure; the line with another code too.
    let without: Vec<String> = log
        .iter()
        .filter(|l| !l.contains(LOCKED_LINE))
        .cloned()
        .collect();
    assert_eq!(
        without.len(),
        log.len() - 4,
        "the recorded log holds the line four times"
    );
    assert_eq!(classify_inno(exit, &without), InnoVerdict::Failed);
    for code in (1..=8).chain([99]).filter(|&c| c != LOCKED_EXIT) {
        assert_eq!(
            classify_inno(code, &log),
            InnoVerdict::Failed,
            "exit {code}"
        );
    }
    assert_eq!(classify_inno(0, &log), InnoVerdict::Installed);
    assert_eq!(classify_inno(0, &[]), InnoVerdict::Installed);

    // 500 rollback lines after the recorded line: the bounded tail still holds it (SI-28).
    let mut long = read("log.txt");
    for n in 0..500 {
        long.push_str(&format!(
            "2026-11-01 19:37:25.{n:03}   Rolling back changes, entry {n}.\r\n"
        ));
    }
    assert_eq!(
        classify_inno(exit, &result::log_lines(long.as_bytes())),
        InnoVerdict::Locked
    );
}
