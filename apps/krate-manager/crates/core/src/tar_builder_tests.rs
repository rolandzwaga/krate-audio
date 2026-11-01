//! The archive builders the extract and install tests share (spec FR-114; plan §4.4): hashes, payloads, gzip and a
//! hand-made ustar writer, moved unchanged from `extract_tests.rs`. Test-only (`#[cfg(test)]` in `lib.rs`).

use crate::feed::Payload;
use crate::verify::Sha256Stream;
use std::io::{Read as _, Write as _};

pub(crate) fn sha(bytes: &[u8]) -> [u8; 32] {
    let mut s = Sha256Stream::new();
    s.update(bytes);
    s.finish()
}

pub(crate) fn unhex(text: &str) -> [u8; 32] {
    let mut out = [0u8; 32];
    for (i, b) in out.iter_mut().enumerate() {
        *b = u8::from_str_radix(&text[2 * i..2 * i + 2], 16).expect("hex digits");
    }
    out
}

/// A payload from a listing written in the test (FR-085), hashed with `Sha256Stream`.
pub(crate) fn literal_payload<S: AsRef<str>>(lines: &[S]) -> Payload {
    let text: String = lines.iter().map(|l| format!("{}\n", l.as_ref())).collect();
    Payload {
        files: lines.len() as u64,
        sha256: sha(text.as_bytes()),
    }
}

pub(crate) fn gz(bytes: &[u8]) -> Vec<u8> {
    let mut encoder = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
    encoder.write_all(bytes).expect("gzip write");
    encoder.finish().expect("gzip finish")
}

pub(crate) fn gunzip(bytes: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    flate2::read::MultiGzDecoder::new(bytes)
        .read_to_end(&mut out)
        .expect("gunzip");
    out
}

pub(crate) fn moduleinfo(version: &str) -> Vec<u8> {
    format!(
        r#"{{"Name": "Alpha", "Version": "{version}", "Classes": [{{"Name": "Alpha", "Version": "{version}"}}]}}"#
    )
    .into_bytes()
}

// ---- the ustar builder (FR-085) ------------------------------------------------------------------------------------

pub(crate) fn octal_field(field: &mut [u8], value: u64) {
    let width = field.len() - 1;
    let text = format!("{value:0width$o}");
    field[..width].copy_from_slice(text.as_bytes());
    field[width] = 0;
}

/// Recomputes the checksum of the header at `off` (`%06o\0 `).
pub(crate) fn reseal(tar: &mut [u8], off: usize) {
    tar[off + 148..off + 156].copy_from_slice(b"        ");
    let sum: u32 = tar[off..off + 512].iter().map(|&b| u32::from(b)).sum();
    let text = format!("{sum:06o}");
    tar[off + 148..off + 154].copy_from_slice(text.as_bytes());
    tar[off + 154] = 0;
    tar[off + 155] = b' ';
}

/// Hand-made POSIX ustar records: 512-byte headers with correct checksums, optional GNU `L`, pax `x` / `g`.
pub(crate) struct Tar {
    pub(crate) bytes: Vec<u8>,
    // pub(crate): extract::tests::special_bits_and_mtime_not_applied_unix sets it (cfg(unix), so Windows never sees it)
    pub(crate) mtime: u64,
}

impl Tar {
    pub(crate) fn new() -> Tar {
        Tar {
            bytes: Vec::new(),
            mtime: 1_793_232_000,
        }
    }

    pub(crate) fn record(
        &mut self,
        name: &[u8],
        flag: u8,
        mode: u32,
        data: &[u8],
        link: &[u8],
    ) -> &mut Tar {
        let mut h = [0u8; 512];
        let cut = name.len().min(100);
        h[..cut].copy_from_slice(&name[..cut]);
        octal_field(&mut h[100..108], u64::from(mode));
        octal_field(&mut h[108..116], 0);
        octal_field(&mut h[116..124], 0);
        octal_field(&mut h[124..136], data.len() as u64);
        octal_field(&mut h[136..148], self.mtime);
        h[156] = flag;
        h[157..157 + link.len()].copy_from_slice(link);
        h[257..263].copy_from_slice(b"ustar\0");
        h[263..265].copy_from_slice(b"00");
        let off = self.bytes.len();
        self.bytes.extend_from_slice(&h);
        reseal(&mut self.bytes, off);
        self.bytes.extend_from_slice(data);
        let pad = (512 - data.len() % 512) % 512;
        self.bytes.extend(std::iter::repeat_n(0u8, pad));
        self
    }

    pub(crate) fn file(&mut self, path: &str, mode: u32, data: &[u8]) -> &mut Tar {
        self.record(path.as_bytes(), b'0', mode, data, b"")
    }

    pub(crate) fn dir(&mut self, path: &str) -> &mut Tar {
        self.record(format!("{path}/").as_bytes(), b'5', 0o755, b"", b"")
    }

    pub(crate) fn link(&mut self, path: &str, target: &str) -> &mut Tar {
        self.record(path.as_bytes(), b'2', 0o777, b"", target.as_bytes())
    }

    /// A GNU `L` record carrying `name` (then a NUL), followed by a file header that uses it.
    pub(crate) fn long_file(&mut self, name: &[u8], data: &[u8]) -> &mut Tar {
        let mut long = name.to_vec();
        long.push(0);
        self.record(b"././@LongLink", b'L', 0o644, &long, b"");
        self.record(name, b'0', 0o644, data, b"")
    }

    pub(crate) fn pax(&mut self, flag: u8, records: &[(&str, &[u8])]) -> &mut Tar {
        let mut data = Vec::new();
        for (key, value) in records {
            let base = key.len() + value.len() + 3;
            let mut len = base + 1;
            while len.to_string().len() + base != len {
                len += 1;
            }
            data.extend_from_slice(format!("{len} {key}=").as_bytes());
            data.extend_from_slice(value);
            data.push(b'\n');
        }
        self.record(b"PaxHeaders/x", flag, 0o644, &data, b"")
    }

    /// The bytes so far plus the two zero blocks of the end of the archive.
    pub(crate) fn finish(&self) -> Vec<u8> {
        let mut out = self.bytes.clone();
        out.extend(std::iter::repeat_n(0u8, 1024));
        out
    }
}
