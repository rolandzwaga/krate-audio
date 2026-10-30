//! SC-018: the helper stub exits 2 with the one D20 stderr line, whatever its arguments.

use std::process::Command;

#[test]
fn helper_stub_exits_2_with_one_line() {
    let cases: [&[&str]; 2] = [&[], &["--anything"]];
    for args in cases {
        let output = Command::new(env!("CARGO_BIN_EXE_krate-manager-helper"))
            .args(args)
            .output()
            .expect("the helper binary runs");
        assert_eq!(output.status.code(), Some(2), "exit code for args {args:?}");
        assert!(
            output.stdout.is_empty(),
            "stdout is empty for args {args:?}"
        );
        assert_eq!(
            output.stderr, b"krate-manager-helper: no operations in this build\n",
            "stderr for args {args:?}"
        );
    }
}
