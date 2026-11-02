//! A throwaway minisign signer for tests (Phase 7 D1, FR-083; plan §4.6). `#[cfg(test)]` only:
//! keys live in memory and are never written to any file.

use crate::feed::PlatformKey;
use crate::verify::SigningKey;
use base64::Engine as _;
use base64::engine::general_purpose::STANDARD;

/// A freshly generated, unencrypted key pair.
pub(crate) struct TestSigner {
    pair: minisign::KeyPair,
}

impl TestSigner {
    /// `generate_unencrypted_keypair()`, in memory only.
    pub(crate) fn new() -> TestSigner {
        let pair = minisign::KeyPair::generate_unencrypted_keypair()
            .unwrap_or_else(|e| panic!("cannot generate a test key pair: {e}"));
        TestSigner { pair }
    }

    /// The key id, as the signature blob carries it (bytes 2..10).
    pub(crate) fn key_id(&self) -> [u8; 8] {
        let mut id = [0u8; 8];
        id.copy_from_slice(self.pair.pk.keynum());
        id
    }

    /// The public key in `minisign.pub` form.
    pub(crate) fn public_text(&self) -> String {
        format!(
            "untrusted comment: minisign public key: {:016X}\n{}\n",
            u64::from_le_bytes(self.key_id()),
            self.pair.pk.to_base64()
        )
    }

    /// The public key as a `SigningKey`.
    pub(crate) fn key(&self) -> SigningKey {
        SigningKey::from_text(&self.public_text())
            .unwrap_or_else(|e| panic!("a test public key does not parse: {e}"))
    }

    /// A prehashed (`ED`) signature over `data` with `trusted_comment`, as four LF lines.
    pub(crate) fn sign(&self, data: &[u8], trusted_comment: &str) -> String {
        minisign::sign(
            Some(&self.pair.pk),
            &self.pair.sk,
            data,
            Some(trusted_comment),
            None,
        )
        .unwrap_or_else(|e| panic!("cannot sign with a test key: {e}"))
        .to_string()
    }

    /// `sign` with the C3 trusted comment of the four fields.
    pub(crate) fn sign_c3(
        &self,
        data: &[u8],
        name: &str,
        version: &str,
        key: PlatformKey,
        asset: &str,
    ) -> String {
        self.sign(data, &c3_comment(name, version, key, asset))
    }
}

/// `krate:1:<name>:<version>:<platform-key>:<asset>` (C3).
pub(crate) fn c3_comment(name: &str, version: &str, key: PlatformKey, asset: &str) -> String {
    format!("krate:1:{name}:{version}:{}:{asset}", key.as_str())
}

/// Line 2's bytes 2..10 replaced by `id`, re-encoded with base64 STANDARD (canonical); for SC-003 g.
pub(crate) fn with_key_id(signature: &str, id: [u8; 8]) -> String {
    let mut lines: Vec<String> = signature.split('\n').map(str::to_owned).collect();
    let line2 = lines
        .get_mut(1)
        .unwrap_or_else(|| panic!("a signature without line 2: {signature:?}"));
    let mut blob = STANDARD
        .decode(line2.as_bytes())
        .unwrap_or_else(|e| panic!("line 2 is not base64: {e}"));
    assert!(blob.len() >= 10, "line 2 decodes to {} bytes", blob.len());
    blob[2..10].copy_from_slice(&id);
    *line2 = STANDARD.encode(&blob);
    lines.join("\n")
}
