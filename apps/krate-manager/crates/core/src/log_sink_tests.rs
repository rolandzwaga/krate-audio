use super::*;
use crate::test_support::fresh_temp_roots;
use log::Level;
use std::fs;
use std::path::Path;

/// The logs dir's entries: (file name, is_dir), sorted.
fn entries(dir: &Path) -> Vec<(String, bool)> {
    let mut out: Vec<(String, bool)> = fs::read_dir(dir)
        .expect("read the logs dir")
        .map(|entry| {
            let entry = entry.expect("dir entry");
            let name = entry.file_name().into_string().expect("UTF-8 file name");
            let is_dir = entry.file_type().expect("file type").is_dir();
            (name, is_dir)
        })
        .collect();
    out.sort();
    out
}

fn files(names: &[&str]) -> Vec<(String, bool)> {
    names.iter().map(|n| ((*n).to_string(), false)).collect()
}

/// A line of exactly `len` bytes, LF included, tagged with `n`.
fn fixed_line(n: usize, len: usize) -> String {
    let mut line = format!("line-{n:03} ");
    while line.len() < len - 1 {
        line.push('x');
    }
    line.push('\n');
    assert_eq!(line.len(), len);
    line
}

#[test]
fn format_line_cases() {
    // The form, level and target present, one LF.
    let line = format_line(1_700_000_000, Level::Info, "km::test", "hello");
    assert_eq!(line, "1700000000 INFO km::test: hello\n");
    let warn = format_line(7, Level::Warn, "km::other", "w");
    assert_eq!(warn, "7 WARN km::other: w\n");

    // CR, LF, tab and every other control character become spaces: one record, one line.
    let line = format_line(1, Level::Error, "t", "a\r\nb\tc\u{0}d\u{1b}e\u{7f}f\u{85}g");
    assert_eq!(line, "1 ERROR t: a  b c d e f g\n");
    assert_eq!(line.matches('\n').count(), 1, "exactly one LF");
    assert!(line.ends_with('\n'), "LF-terminated");

    // A 10 000-byte ASCII message fills the line exactly to MAX_LINE_BYTES, LF included.
    let prefix = "1700000000 INFO km::test: ";
    let long = "a".repeat(10_000);
    let line = format_line(1_700_000_000, Level::Info, "km::test", &long);
    assert_eq!(line.len(), MAX_LINE_BYTES, "cut to MAX_LINE_BYTES");
    assert!(line.starts_with(prefix));
    assert!(line.ends_with("a\n"));
    assert_eq!(line.matches('\n').count(), 1);

    // A 10 000-byte message of two-byte characters whose cut falls inside one.
    let wide = "é".repeat(5_000);
    assert_eq!(wide.len(), 10_000);
    let uncut = format!("{prefix}{wide}");
    assert!(
        !uncut.is_char_boundary(MAX_LINE_BYTES - 1),
        "the test vector must put the cut inside a character"
    );
    let line = format_line(1_700_000_000, Level::Info, "km::test", &wide);
    assert!(line.len() <= MAX_LINE_BYTES, "len {}", line.len());
    assert_eq!(line.len(), MAX_LINE_BYTES - 1, "stepped back one byte");
    assert!(line.starts_with(prefix));
    assert!(line.ends_with("é\n"));
    assert_eq!(line.matches('\n').count(), 1);
    assert!(String::from_utf8(line.into_bytes()).is_ok(), "valid UTF-8");
}

#[test]
fn rotating_log_cases() {
    const CAP: u64 = 200;

    // The logs dir is absent at first and created by open.
    {
        let (_td, roots) = fresh_temp_roots();
        let dir = roots.logs_dir();
        assert!(!dir.exists(), "the logs dir is absent before open");
        let log = RotatingLog::open_with_cap(&roots, CAP).expect("open");
        assert!(dir.is_dir(), "open creates the logs dir");
        assert_eq!(entries(&dir), files(&[LOG_FILE]));
        drop(log);
    }

    // Rotation: a model of both files checked after every write, through three rotations.
    {
        let (_td, roots) = fresh_temp_roots();
        let dir = roots.logs_dir();
        let mut log = RotatingLog::open_with_cap(&roots, CAP).expect("open");
        let mut current: Vec<u8> = Vec::new();
        let mut rotated: Option<Vec<u8>> = None;
        let mut generations: Vec<Vec<u8>> = Vec::new();
        let mut rotations = 0;
        let mut n = 0;
        while rotations < 3 {
            let line = fixed_line(n, 45);
            n += 1;
            if !current.is_empty() && current.len() + line.len() > CAP as usize {
                generations.push(current.clone());
                rotated = Some(std::mem::take(&mut current));
                rotations += 1;
            }
            current.extend_from_slice(line.as_bytes());
            log.write_line(&line).expect("write_line");

            let actual = fs::read(dir.join(LOG_FILE)).expect("read the log");
            assert_eq!(actual, current, "the log after line {}", n - 1);
            assert!(actual.len() as u64 <= CAP);
            assert!(
                actual.ends_with(line.as_bytes()),
                "the newest line is in the log"
            );
            match &rotated {
                None => assert_eq!(entries(&dir), files(&[LOG_FILE])),
                Some(previous) => {
                    assert_eq!(entries(&dir), files(&[LOG_FILE, LOG_FILE_ROTATED]));
                    let one = fs::read(dir.join(LOG_FILE_ROTATED)).expect("read .1");
                    assert_eq!(&one, previous, ".1 holds the generation before the log");
                    assert!(one.len() as u64 <= CAP);
                }
            }
        }
        // Each rotation replaced .1: after the third, .1 holds the third generation and no
        // earlier one survives anywhere.
        assert_eq!(rotations, 3);
        assert_eq!(generations.len(), 3);
        let one = fs::read(dir.join(LOG_FILE_ROTATED)).expect("read .1");
        assert_eq!(one, generations[2]);
        assert_ne!(one, generations[1]);
        assert_ne!(one, generations[0]);
        assert_eq!(entries(&dir), files(&[LOG_FILE, LOG_FILE_ROTATED]));
    }

    // Reopening appends rather than truncates, and the size comes from the file.
    {
        let (_td, roots) = fresh_temp_roots();
        let dir = roots.logs_dir();
        let lines: Vec<String> = (0..5).map(|n| fixed_line(n, 45)).collect();
        let mut log = RotatingLog::open_with_cap(&roots, CAP).expect("open");
        log.write_line(&lines[0]).expect("write 0");
        log.write_line(&lines[1]).expect("write 1");
        drop(log);
        let mut log = RotatingLog::open_with_cap(&roots, CAP).expect("reopen");
        log.write_line(&lines[2]).expect("write 2");
        log.write_line(&lines[3]).expect("write 3");
        let four = lines[..4].concat();
        assert_eq!(fs::read_to_string(dir.join(LOG_FILE)).expect("read"), four);
        assert_eq!(entries(&dir), files(&[LOG_FILE]));
        // 180 bytes on disk, 90 of them written before the reopen: the fifth line rotates.
        log.write_line(&lines[4]).expect("write 4");
        assert_eq!(entries(&dir), files(&[LOG_FILE, LOG_FILE_ROTATED]));
        assert_eq!(
            fs::read_to_string(dir.join(LOG_FILE_ROTATED)).expect("read .1"),
            four
        );
        assert_eq!(
            fs::read_to_string(dir.join(LOG_FILE)).expect("read"),
            lines[4]
        );
    }

    // PD-10: a line over the cap, cut inside a multi-byte character, after a short line.
    {
        let (_td, roots) = fresh_temp_roots();
        let dir = roots.logs_dir();
        let long = format!("{}{}\n", "a".repeat(101), "€".repeat(66));
        assert_eq!(long.len(), 300);
        assert!(
            !long.is_char_boundary(CAP as usize - 1),
            "the test vector must put the cut inside a character"
        );
        let mut log = RotatingLog::open_with_cap(&roots, CAP).expect("open");
        log.write_line("short\n").expect("write the short line");
        log.write_line(&long).expect("write the long line");
        assert_eq!(entries(&dir), files(&[LOG_FILE, LOG_FILE_ROTATED]));
        for name in [LOG_FILE, LOG_FILE_ROTATED] {
            let bytes = fs::read(dir.join(name)).expect("read");
            assert!(bytes.len() as u64 <= CAP, "{name} is {} bytes", bytes.len());
            assert!(String::from_utf8(bytes).is_ok(), "{name} is valid UTF-8");
        }
        assert_eq!(
            fs::read_to_string(dir.join(LOG_FILE_ROTATED)).expect("read .1"),
            "short\n"
        );
        let expected = format!("{}{}\n", "a".repeat(101), "€".repeat(32));
        assert_eq!(
            fs::read_to_string(dir.join(LOG_FILE)).expect("read"),
            expected
        );
    }

    // PD-10: a failed rotation rename writes nothing; the next write after the cause is gone
    // rotates, which shows the handle was reopened.
    {
        let (_td, roots) = fresh_temp_roots();
        let dir = roots.logs_dir();
        let lines: Vec<String> = (0..6).map(|n| fixed_line(n, 45)).collect();
        let mut log = RotatingLog::open_with_cap(&roots, CAP).expect("open");
        for line in &lines[..4] {
            log.write_line(line).expect("write before the block");
        }
        let blocker = dir.join(LOG_FILE_ROTATED);
        fs::create_dir(&blocker).expect("make .1 a directory");
        fs::write(blocker.join("held"), b"x").expect("a file inside the blocker");
        let before = fs::read(dir.join(LOG_FILE)).expect("read before");

        let result = log.write_line(&lines[4]);
        assert!(
            result.is_err(),
            "a rename over a directory fails: {result:?}"
        );
        let after = fs::read(dir.join(LOG_FILE)).expect("read after");
        assert_eq!(
            after, before,
            "the log is byte-identical after the failed rotation"
        );
        assert!(after.len() as u64 <= CAP);
        assert_eq!(
            entries(&dir),
            vec![
                (LOG_FILE.to_string(), false),
                (LOG_FILE_ROTATED.to_string(), true)
            ]
        );

        fs::remove_dir_all(&blocker).expect("remove the blocker");
        log.write_line(&lines[5])
            .expect("the next write rotates and succeeds");
        assert_eq!(entries(&dir), files(&[LOG_FILE, LOG_FILE_ROTATED]));
        assert_eq!(
            fs::read(dir.join(LOG_FILE_ROTATED)).expect("read .1"),
            before
        );
        assert_eq!(
            fs::read_to_string(dir.join(LOG_FILE)).expect("read"),
            lines[5]
        );
    }
}
