//! `classify::tests` (owner T003; plan §7.1 SC-016 row): the provisional rule until M-1 (D10).

use super::*;

#[test]
fn provisional_rule() {
    let logs: [Vec<String>; 3] = [
        Vec::new(),
        vec![
            "Log opened.".to_owned(),
            String::new(),
            "Installation process succeeded.".to_owned(),
        ],
        vec![
            "The process cannot access the file because it is being used by another process."
                .to_owned(),
            "Rolling back changes.".to_owned(),
        ],
    ];
    for log in &logs {
        assert_eq!(
            classify_inno(0, log),
            InnoVerdict::Installed,
            "exit 0, log {log:?}"
        );
        for code in (1..=8).chain([99]) {
            assert_eq!(
                classify_inno(code, log),
                InnoVerdict::Failed,
                "exit {code}, log {log:?}"
            );
        }
        // Never Locked before M-1, whatever the code and the log.
        for code in (-300..=300).chain([i32::MIN, i32::MAX]) {
            assert_ne!(classify_inno(code, log), InnoVerdict::Locked, "exit {code}");
        }
    }
}
