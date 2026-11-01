//! SC-023 (FR-005, D15): the built helper refuses every malformed argv with exit 2, empty stdout, the one usage line
//! on stderr, and writes nothing. No row passes a valid argv, so no job is ever read and nothing runs.

use krate_manager_helper::cli::USAGE;
use std::fs;
use std::process::Command;

const HEX: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

#[test]
fn helper_usage_exits_2() {
    let cases: [&[&str]; 4] = [
        &[],
        &["--anything"],
        &["--job", "job.json"],
        &["--job", "a.json", "--job", "b.json", "--job-sha256", HEX],
    ];
    for args in cases {
        let dir = tempfile::TempDir::new().expect("a temp working directory");
        let output = Command::new(env!("CARGO_BIN_EXE_krate-manager-helper"))
            .args(args)
            .current_dir(dir.path())
            .output()
            .expect("the helper binary runs");
        assert_eq!(output.status.code(), Some(2), "exit code for {args:?}");
        assert!(output.stdout.is_empty(), "stdout is empty for {args:?}");
        assert_eq!(
            String::from_utf8_lossy(&output.stderr),
            format!("{USAGE}\n"),
            "exactly one usage line on stderr for {args:?}"
        );
        let left = fs::read_dir(dir.path())
            .expect("the working directory lists")
            .count();
        assert_eq!(
            left, 0,
            "nothing created in the working directory for {args:?}"
        );
    }
}
