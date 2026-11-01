//! SHA-256 and hex (plan §4.1.8): the helper's only `sha2` user (SI-6).

use sha2::{Digest, Sha256};

/// A streaming SHA-256 (FR-035's hash-while-copying).
pub struct Sha256Stream(Sha256);

impl Sha256Stream {
    pub fn new() -> Sha256Stream {
        Sha256Stream(Sha256::new())
    }

    pub fn update(&mut self, bytes: &[u8]) {
        self.0.update(bytes);
    }

    pub fn finish(self) -> [u8; 32] {
        self.0.finalize().into()
    }
}

impl Default for Sha256Stream {
    fn default() -> Sha256Stream {
        Sha256Stream::new()
    }
}

/// The SHA-256 of `bytes`.
pub fn sha256(bytes: &[u8]) -> [u8; 32] {
    let mut stream = Sha256Stream::new();
    stream.update(bytes);
    stream.finish()
}

const HEX: &[u8; 16] = b"0123456789abcdef";

/// Lower-case hex, two digits per byte.
pub fn hex(digest: &[u8; 32]) -> String {
    let mut s = String::with_capacity(64);
    for b in digest {
        s.push(char::from(HEX[usize::from(b >> 4)]));
        s.push(char::from(HEX[usize::from(b & 0x0f)]));
    }
    s
}

/// The value of one lower-case hex digit.
fn nibble(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'a'..=b'f' => Some(c - b'a' + 10),
        _ => None,
    }
}

/// Exactly 64 lower-case hex digits, else `None` (FR-005, PD-25).
pub fn parse_hex64(s: &str) -> Option<[u8; 32]> {
    let bytes = s.as_bytes();
    if bytes.len() != 64 {
        return None;
    }
    let mut out = [0u8; 32];
    for (i, pair) in bytes.as_chunks::<2>().0.iter().enumerate() {
        out[i] = (nibble(pair[0])? << 4) | nibble(pair[1])?;
    }
    Some(out)
}
