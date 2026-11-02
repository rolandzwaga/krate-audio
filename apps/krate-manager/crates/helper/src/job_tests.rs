//! `job::tests` (owner T003; plan §7.1): FR-001's key sets, FR-002's duplicate keys, FR-036's target OS.

use super::*;
use crate::error;

const HEX: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

/// `{"job_version":1,"ops":[<ops>]}`, spelled by hand so a test can repeat a key.
fn job(ops: &[String]) -> String {
    format!("{{\"job_version\":1,\"ops\":[{}]}}", ops.join(","))
}

fn inno_installer() -> String {
    format!("{{\"op\":\"run_inno_installer\",\"file\":\"staging/a.exe\",\"sha256\":\"{HEX}\"}}")
}

fn inno_uninstaller() -> String {
    "{\"op\":\"run_inno_uninstaller\",\"app_id\":\"{0A1B2C3D-0000-4000-8000-00000000A1FA}\"}"
        .to_owned()
}

fn pkg_installer() -> String {
    format!("{{\"op\":\"run_pkg_installer\",\"file\":\"staging/a.pkg\",\"sha256\":\"{HEX}\"}}")
}

fn uninstall_pkg() -> String {
    "{\"op\":\"uninstall_pkg\",\"pkg_id\":\"com.krateaudio.alpha\"}".to_owned()
}

/// Each kind's valid op text, the OS it targets and the op it parses to.
fn valid_ops() -> Vec<(String, HelperOs, Op)> {
    let sha = [
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd,
        0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
        0xcd, 0xef,
    ];
    vec![
        (
            inno_installer(),
            HelperOs::Windows,
            Op::RunInnoInstaller {
                file: "staging/a.exe".to_owned(),
                sha256: sha,
            },
        ),
        (
            inno_uninstaller(),
            HelperOs::Windows,
            Op::RunInnoUninstaller {
                app_id: "{0A1B2C3D-0000-4000-8000-00000000A1FA}".to_owned(),
            },
        ),
        (
            pkg_installer(),
            HelperOs::Macos,
            Op::RunPkgInstaller {
                file: "staging/a.pkg".to_owned(),
                sha256: sha,
            },
        ),
        (
            uninstall_pkg(),
            HelperOs::Macos,
            Op::UninstallPkg {
                pkg_id: "com.krateaudio.alpha".to_owned(),
            },
        ),
    ]
}

/// `parse` must refuse `text` for `os` with `E_HELPER_JOB_PARSE`.
fn refused(text: &str, os: HelperOs, why: &str) {
    assert_eq!(
        parse(text.as_bytes(), os),
        Err(error::JOB_PARSE),
        "{why}: {text}"
    );
}

/// `op_text` with `key: value` added before its closing brace.
fn with_key(op_text: &str, key: &str, value: &str) -> String {
    let body = op_text.strip_suffix('}').expect("an op is an object");
    format!("{body},\"{key}\":{value}}}")
}

#[test]
fn no_field_adds_a_root() {
    // FR-001's four key sets, exactly.
    let expected: [(&str, &[&str]); 4] = [
        ("run_inno_installer", &["op", "file", "sha256"]),
        ("run_inno_uninstaller", &["op", "app_id"]),
        ("run_pkg_installer", &["op", "file", "sha256"]),
        ("uninstall_pkg", &["op", "pkg_id"]),
    ];
    assert_eq!(KEYS, expected);

    for (text, os, op) in valid_ops() {
        // The key set parses, and its kind is the one KEYS names.
        assert_eq!(
            parse(job(std::slice::from_ref(&text)).as_bytes(), os),
            Ok(vec![op.clone()])
        );
        let keys = KEYS
            .iter()
            .find(|(kind, _)| *kind == op.kind())
            .map(|(_, keys)| *keys)
            .expect("every kind has a key set");

        // No key that would carry a root, a destination, arguments or a log path is accepted.
        for (key, value) in [
            ("roots", "[]"),
            ("settings_roots", "[\"C:\\\\Windows\"]"),
            ("dest", "\"/etc\""),
            ("args", "[\"/DIR=x\"]"),
            ("log", "\"x.log\""),
        ] {
            refused(&job(&[with_key(&text, key, value)]), os, "an extra key");
        }

        // Every key of the set is required.
        let value: serde_json::Value = serde_json::from_str(&text).expect("valid op text");
        for missing in keys {
            let mut object = value.as_object().expect("an object").clone();
            object.remove(*missing);
            let text = serde_json::to_string(&object).expect("serializes");
            refused(&job(&[text]), os, "a missing key");
        }

        // Every other kind's keys are refused on this kind.
        for (_, others) in KEYS {
            for other in others.iter().filter(|k| !keys.contains(k)) {
                refused(
                    &job(&[with_key(&text, other, "\"x\"")]),
                    os,
                    "another kind's key",
                );
                refused(
                    &job(&[with_key(&text, other, "null")]),
                    os,
                    "another kind's key, null",
                );
            }
        }
    }
}

#[test]
fn parse_refuses_malformed_jobs() {
    let win = HelperOs::Windows;
    // PD-2, FR-002: a repeated key, `op` included, is refused, wherever it sits.
    refused(
        &job(&[format!(
            "{{\"op\":\"run_pkg_installer\",\"op\":\"uninstall_pkg\",\"file\":\"a.pkg\",\"sha256\":\"{HEX}\"}}"
        )]),
        HelperOs::Macos,
        "a duplicate op key",
    );
    refused(
        &job(&[format!(
            "{{\"op\":\"run_inno_installer\",\"file\":\"a.exe\",\"file\":\"b.exe\",\"sha256\":\"{HEX}\"}}"
        )]),
        win,
        "a duplicate file key",
    );
    refused(
        &format!(
            "{{\"job_version\":1,\"job_version\":1,\"ops\":[{}]}}",
            inno_installer()
        ),
        win,
        "a duplicate job_version",
    );
    refused(
        &format!(
            "{{\"job_version\":1,\"ops\":[{}],\"ops\":[{}]}}",
            inno_installer(),
            inno_installer()
        ),
        win,
        "a duplicate ops key",
    );

    // job_version is the integer 1.
    for version in ["0", "2", "1.0", "\"1\"", "null", "-1"] {
        refused(
            &format!(
                "{{\"job_version\":{version},\"ops\":[{}]}}",
                inno_installer()
            ),
            win,
            "a job_version other than 1",
        );
    }
    refused(
        &format!("{{\"ops\":[{}]}}", inno_installer()),
        win,
        "no job_version",
    );
    refused("{\"job_version\":1}", win, "no ops");
    refused(
        &format!(
            "{{\"job_version\":1,\"ops\":[{}],\"roots\":[]}}",
            inno_installer()
        ),
        win,
        "an extra top-level key",
    );

    // 1 to 32 ops.
    refused(&job(&[]), win, "0 ops");
    let thirty_three = vec![inno_installer(); 33];
    refused(&job(&thirty_three), win, "33 ops");
    let thirty_two = vec![inno_installer(); 32];
    assert_eq!(
        parse(job(&thirty_two).as_bytes(), win).map(|ops| ops.len()),
        Ok(MAX_OPS)
    );

    // An explicit null is a present key of the wrong type, not an absent one.
    refused(
        &job(&["{\"op\":\"run_inno_uninstaller\",\"app_id\":null}".to_owned()]),
        win,
        "app_id null",
    );
    // Wrong types.
    refused(
        &job(&[format!(
            "{{\"op\":\"run_inno_installer\",\"file\":7,\"sha256\":\"{HEX}\"}}"
        )]),
        win,
        "a number file",
    );
    refused(
        &job(&["{\"op\":\"uninstall_pkg\",\"pkg_id\":[\"com.krateaudio.alpha\"]}".to_owned()]),
        HelperOs::Macos,
        "an array pkg_id",
    );
    refused(
        &job(&["{\"op\":7,\"pkg_id\":\"com.krateaudio.alpha\"}".to_owned()]),
        HelperOs::Macos,
        "a number op",
    );
    // An unknown op value.
    refused(
        &job(&["{\"op\":\"run_command\",\"app_id\":\"x\"}".to_owned()]),
        win,
        "an unknown op",
    );
    refused(
        &job(&["{\"op\":\"Uninstall_Pkg\",\"pkg_id\":\"com.krateaudio.alpha\"}".to_owned()]),
        HelperOs::Macos,
        "an op value in the wrong case",
    );

    // sha256: 64 lower-case hex only.
    let upper = HEX.to_ascii_uppercase();
    refused(
        &job(&[format!(
            "{{\"op\":\"run_inno_installer\",\"file\":\"a.exe\",\"sha256\":\"{upper}\"}}"
        )]),
        win,
        "upper-case hex",
    );
    refused(
        &job(&[format!(
            "{{\"op\":\"run_pkg_installer\",\"file\":\"a.pkg\",\"sha256\":\"{}\"}}",
            &HEX[1..]
        )]),
        HelperOs::Macos,
        "63 hex digits",
    );

    // FR-001: a JSON object, not the array form serde would also accept for a struct.
    refused(
        &format!("[1,[[\"run_inno_installer\",\"a.exe\",\"{HEX}\"]]]"),
        win,
        "the whole job as an array",
    );
    refused(
        &job(&[format!("[\"run_inno_installer\",\"a.exe\",\"{HEX}\"]")]),
        win,
        "an op as an array",
    );
    refused(
        &format!("{} {{}}", job(&[inno_installer()])),
        win,
        "trailing data",
    );
    refused("", win, "empty bytes");
    refused("not json", win, "not JSON");
    refused(
        &format!("\u{feff}{}", job(&[inno_installer()])),
        win,
        "a byte-order mark",
    );
}

#[test]
fn parse_checks_the_target_os() {
    for (text, target, _) in valid_ops() {
        // FR-036: Linux refuses every op.
        refused(
            &job(std::slice::from_ref(&text)),
            HelperOs::Linux,
            "an op on Linux",
        );
        // FR-001: an op of the other OS refuses the whole job.
        let other = if target == HelperOs::Windows {
            HelperOs::Macos
        } else {
            HelperOs::Windows
        };
        refused(
            &job(std::slice::from_ref(&text)),
            other,
            "an op of the other OS",
        );
    }
    // An Inno op with os = Macos, even beside valid macOS ops.
    refused(
        &job(&[pkg_installer(), inno_installer(), uninstall_pkg()]),
        HelperOs::Macos,
        "an Inno op in a macOS job",
    );
    refused(
        &job(&[inno_uninstaller(), uninstall_pkg()]),
        HelperOs::Windows,
        "a pkg op in a Windows job",
    );
    // Both kinds of one OS, in job order.
    let ops = parse(
        job(&[inno_uninstaller(), inno_installer(), inno_uninstaller()]).as_bytes(),
        HelperOs::Windows,
    )
    .expect("a Windows job parses");
    assert_eq!(
        ops.iter().map(Op::kind).collect::<Vec<_>>(),
        [
            "run_inno_uninstaller",
            "run_inno_installer",
            "run_inno_uninstaller"
        ]
    );
    // The contents of file, app_id and pkg_id are not checked here (FR-020, FR-031, FR-033 refuse per op).
    assert_eq!(
        parse(
            job(&["{\"op\":\"uninstall_pkg\",\"pkg_id\":\"\"}".to_owned()]).as_bytes(),
            HelperOs::Macos
        ),
        Ok(vec![Op::UninstallPkg {
            pkg_id: String::new()
        }])
    );
}

/// FR-002 (SI-2): core caps a batch at the helper's op cap, so a job core writes is never refused after the prompt.
#[test]
fn core_and_helper_op_caps_agree() {
    assert_eq!(
        krate_manager_core::install::job::MAX_OPS,
        crate::job::MAX_OPS
    );
    assert_eq!(crate::job::MAX_OPS, 32);
}
