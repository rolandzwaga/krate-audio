//! `result::tests` (owner T003; plan §7.1 SC-006 row): FR-003's file and the bounded log tail.

use super::*;
use std::io::Write;

/// What a `WriteSync` was asked to do, in order.
#[derive(Debug, PartialEq, Eq)]
enum Step {
    Write(Vec<u8>),
    Flush,
    Sync,
}

#[derive(Default)]
struct Recorder {
    steps: Vec<Step>,
}

impl Write for Recorder {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.steps.push(Step::Write(buf.to_vec()));
        Ok(buf.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        self.steps.push(Step::Flush);
        Ok(())
    }
}

impl WriteSync for Recorder {
    fn sync_all(&mut self) -> io::Result<()> {
        self.steps.push(Step::Sync);
        Ok(())
    }
}

/// `n` numbered lines, each ended by `\n`.
fn numbered(n: usize) -> Vec<u8> {
    (0..n)
        .map(|i| format!("line {i}\n"))
        .collect::<String>()
        .into_bytes()
}

#[test]
fn log_tail_bounds() {
    // 45 lines keep the last 40, in order.
    let tail = log_tail(&numbered(45));
    assert_eq!(tail.len(), 40);
    assert_eq!(tail.first().map(String::as_str), Some("line 5"));
    assert_eq!(tail.last().map(String::as_str), Some("line 44"));
    // The spec's row: 100 lines keep the last 40.
    let tail = log_tail(&numbered(100));
    assert_eq!(
        tail,
        (60..100).map(|i| format!("line {i}")).collect::<Vec<_>>()
    );
    // Fewer than 40 lines are kept whole; the last line needs no newline.
    assert_eq!(log_tail(b"a\r\nb"), ["a", "b"]);

    // 45 lines interleaved with empty lines: the empty lines count.
    let interleaved: String = (0..45).map(|i| format!("line {i}\n\n")).collect();
    let tail = log_tail(interleaved.as_bytes());
    assert_eq!(tail.len(), 40);
    let expected: Vec<String> = (25..45)
        .flat_map(|i| [format!("line {i}"), String::new()])
        .collect();
    assert_eq!(tail, expected);

    // A 600-byte line is cut to at most 512 bytes at a char boundary: 'a' then 300 two-byte chars puts byte 512
    // inside a char, so the cut lands at 511.
    let long = format!("a{}", "\u{e9}".repeat(300));
    assert_eq!(long.len(), 601);
    let tail = log_tail(long.as_bytes());
    assert_eq!(tail.len(), 1);
    assert_eq!(tail[0].len(), 511);
    assert!(long.starts_with(&tail[0]));
    // The spec's row: a 2000-byte ASCII line is cut to exactly 512 bytes.
    let ascii = "x".repeat(2000);
    assert_eq!(log_tail(ascii.as_bytes()), ["x".repeat(512)]);
    // A line of exactly 512 bytes is kept whole.
    assert_eq!(log_tail("y".repeat(512).as_bytes()), ["y".repeat(512)]);

    // Invalid UTF-8 becomes U+FFFD.
    assert_eq!(
        log_tail(b"ok\n\xff\xfe bad\n"),
        ["ok", "\u{fffd}\u{fffd} bad"]
    );
    // An empty input (an empty or missing log) is no lines.
    assert_eq!(log_tail(b""), Vec::<String>::new());

    // log_lines: every line, a trailing \r stripped, lossy UTF-8; a final newline adds no line.
    let lines = log_lines(&numbered(45));
    assert_eq!(lines.len(), 45);
    assert_eq!(lines[0], "line 0");
    assert_eq!(lines[44], "line 44");
    assert_eq!(
        log_lines(b"one\r\n\r\ntwo\r\nthree\r\r\n\xc3\n"),
        ["one", "", "two", "three\r", "\u{fffd}"]
    );
    assert_eq!(log_lines(b""), Vec::<String>::new());
    assert_eq!(log_lines(b"\n"), [""]);
    // log_lines does not cut long lines: classify sees the whole line (SI-28).
    assert_eq!(log_lines(ascii.as_bytes()), [ascii.as_str()]);

    // write: FR-003's keys in order, then sync_all exactly once, after the bytes.
    let file = ResultFile {
        result_version: 1,
        job_sha256: "ab".repeat(32),
        job_error: None,
        ops: vec![
            OpEntry {
                index: 0,
                op: "run_inno_installer",
                outcome: OpOutcome::Done,
                code: None,
                exit_code: Some(0),
                seconds: 1.5,
                log_tail: vec!["done".to_owned()],
            },
            OpEntry {
                index: 1,
                op: "run_inno_uninstaller",
                outcome: OpOutcome::NotRun,
                code: Some("E_HELPER_AFTER_TIMEOUT"),
                exit_code: None,
                seconds: 0.0,
                log_tail: Vec::new(),
            },
        ],
    };
    let mut out = Recorder::default();
    write(&mut out, &file).expect("the write succeeds");
    let syncs = out.steps.iter().filter(|s| **s == Step::Sync).count();
    assert_eq!(syncs, 1, "sync_all once: {:?}", out.steps);
    assert_eq!(out.steps.last(), Some(&Step::Sync), "sync_all is last");
    let bytes: Vec<u8> = out
        .steps
        .iter()
        .filter_map(|s| match s {
            Step::Write(b) => Some(b.clone()),
            Step::Flush | Step::Sync => None,
        })
        .flatten()
        .collect();
    let expected = format!(
        "{{\"result_version\":1,\"job_sha256\":\"{}\",\"job_error\":null,\"ops\":[\
         {{\"index\":0,\"op\":\"run_inno_installer\",\"outcome\":\"done\",\"code\":null,\"exit_code\":0,\
         \"seconds\":1.5,\"log_tail\":[\"done\"]}},\
         {{\"index\":1,\"op\":\"run_inno_uninstaller\",\"outcome\":\"not_run\",\"code\":\"E_HELPER_AFTER_TIMEOUT\",\
         \"exit_code\":null,\"seconds\":0.0,\"log_tail\":[]}}]}}",
        "ab".repeat(32)
    );
    assert_eq!(String::from_utf8(bytes).expect("UTF-8"), expected);

    // An exit-3 file (SI-11): job_error set, ops empty.
    let refused = ResultFile {
        result_version: 1,
        job_sha256: "cd".repeat(32),
        job_error: Some("E_HELPER_JOB_HASH"),
        ops: Vec::new(),
    };
    let mut out = Recorder::default();
    write(&mut out, &refused).expect("the write succeeds");
    assert_eq!(out.steps.last(), Some(&Step::Sync));
    let bytes: Vec<u8> = out
        .steps
        .iter()
        .filter_map(|s| match s {
            Step::Write(b) => Some(b.clone()),
            Step::Flush | Step::Sync => None,
        })
        .flatten()
        .collect();
    assert_eq!(
        String::from_utf8(bytes).expect("UTF-8"),
        format!(
            "{{\"result_version\":1,\"job_sha256\":\"{}\",\"job_error\":\"E_HELPER_JOB_HASH\",\"ops\":[]}}",
            "cd".repeat(32)
        )
    );
}

/// A writer whose `write` fails: `write` reports it and never syncs.
struct Failing {
    synced: bool,
}

impl Write for Failing {
    fn write(&mut self, _buf: &[u8]) -> io::Result<usize> {
        Err(io::Error::from(io::ErrorKind::StorageFull))
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl WriteSync for Failing {
    fn sync_all(&mut self) -> io::Result<()> {
        self.synced = true;
        Ok(())
    }
}

#[test]
fn write_error_is_reported() {
    let file = ResultFile {
        result_version: 1,
        job_sha256: "ab".repeat(32),
        job_error: None,
        ops: Vec::new(),
    };
    let mut out = Failing { synced: false };
    let err = write(&mut out, &file).expect_err("the write fails");
    assert_eq!(err.kind(), io::ErrorKind::StorageFull);
    assert!(!out.synced, "no sync after a failed write");
}
