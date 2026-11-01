//! Phase 10 SC-015 (FR-091; plan §7.1): `install::windows_parameters` checked by the real Windows argument parser
//! instead of a hand-written one. The quoted string goes onto the command line of the never-shipped `argv-echo`
//! target through `CommandExt::raw_arg` ("without any quoting or escaping", so the child's runtime parses it as
//! `ShellExecuteExW`'s `lpParameters` would be), and the argv it echoes must equal the input. Lane `cargo.windows`;
//! the one process spawned is that echo binary, unelevated; no path below is ever opened.
#![cfg(windows)]

use krate_manager_core::install::windows_parameters;
use std::os::windows::process::CommandExt;
use std::process::Command;

/// A `--job-sha256` value: 64 lower-case hex digits (FR-005).
const JOB_SHA256: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

/// The helper's four arguments (FR-076) around one `--job` path.
fn helper_args(job: &str) -> Vec<String> {
    ["--job", job, "--job-sha256", JOB_SHA256]
        .into_iter()
        .map(str::to_owned)
        .collect()
}

/// The five SC-015 inputs: a path with a space, its verbatim `\\?\` form (FR-027), a quoted path ending in `\` and
/// in `\\` (the trailing backslashes the MS C rules double before the closing quote), and a path with a tab.
fn cases() -> Vec<Vec<String>> {
    let tabbed = [r"C:\Users\John", "\t", r"Smith\job-0123abcd.json"].concat();
    [
        r"C:\Users\John Smith\AppData\Local\com.krateaudio.krate-manager\jobs\job-0123abcd.json",
        r"\\?\C:\Users\John Smith\AppData\Local\com.krateaudio.krate-manager\jobs\job-0123abcd.json",
        r"C:\Users\John Smith\jobs\",
        r"C:\Users\John Smith\jobs\\",
        tabbed.as_str(),
    ]
    .into_iter()
    .map(helper_args)
    .collect()
}

#[test]
fn windows_parameters_parse_back_windows() {
    for args in cases() {
        let parameters =
            windows_parameters(&args).unwrap_or_else(|e| panic!("{args:?} refused: {e}"));
        let output = Command::new(env!("CARGO_BIN_EXE_argv-echo"))
            .raw_arg(&parameters)
            .output()
            .unwrap_or_else(|e| panic!("cannot run argv-echo: {e}"));
        assert!(
            output.status.success(),
            "argv-echo {parameters:?}: {:?}, stderr {}",
            output.status,
            String::from_utf8_lossy(&output.stderr)
        );
        let echoed: Vec<String> = serde_json::from_slice(&output.stdout).unwrap_or_else(|e| {
            panic!(
                "argv-echo {parameters:?} printed no JSON array ({e}): {}",
                String::from_utf8_lossy(&output.stdout)
            )
        });
        assert_eq!(echoed, args, "lpParameters {parameters:?}");
    }
}
