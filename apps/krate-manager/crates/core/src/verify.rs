//! Verification of a downloaded artifact against the feed and the release key (Phase 7 D6, D7,
//! FR-030 to FR-043; plan §4.3). Only a verified file becomes a `VerifiedArtifact`.

use crate::contract::{self, ContractError};
use crate::download::DownloadRequest;
use crate::feed::{AssetFormat, AssetName, PlatformKey};
use base64::Engine as _;
use base64::engine::general_purpose::STANDARD;
use sha2::{Digest, Sha256};
use std::fs::{self, File};
use std::io::{self, Read};
use std::path::{Path, PathBuf};

/// The committed release public key (FR-042), read at build time; never read from disk at run time.
pub const RELEASE_KEY_TEXT: &str =
    include_str!("../../../../../tools/feed-contract/keys/krate-signing.pub");
/// Step 4's read size (FR-034): one heap buffer, the file is never read whole.
const CHUNK: usize = 64 * 1024;

/// A minisign public key and its key id (FR-040). Equal when decoded from the same key.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SigningKey {
    public: minisign_verify::PublicKey,
    key_id: [u8; 8],
}

impl SigningKey {
    /// `PublicKey::decode`, then line 2 base64-decoded (`STANDARD`) for bytes 2..10.
    /// Crate-private (SI-28): no other crate can turn a key it made into a `SigningKey`.
    pub(crate) fn from_text(text: &str) -> Result<SigningKey, VerifyError> {
        let key_text = |detail: String| VerifyError::KeyText { detail };
        let public = minisign_verify::PublicKey::decode(text)
            .map_err(|e| key_text(format!("not a minisign public key: {e}")))?;
        let line2 = text
            .lines()
            .nth(1)
            .ok_or_else(|| key_text("no second line".to_owned()))?;
        let bytes = STANDARD
            .decode(line2)
            .map_err(|e| key_text(format!("line 2 is not base64: {e}")))?;
        let id = bytes
            .get(2..10)
            .ok_or_else(|| key_text(format!("line 2 decodes to {} bytes", bytes.len())))?;
        let mut key_id = [0u8; 8];
        key_id.copy_from_slice(id);
        Ok(SigningKey { public, key_id })
    }

    /// The committed release key. Panics on failure: a build input SC-016 proves parses.
    pub fn release() -> SigningKey {
        SigningKey::from_text(RELEASE_KEY_TEXT)
            .unwrap_or_else(|e| panic!("the committed release key does not parse: {e}"))
    }

    /// As minisign prints it: `u64::from_le_bytes(key_id)` in `{:016X}` (KAT: `440FA75168B852F7`).
    pub fn key_id_hex(&self) -> String {
        format!("{:016X}", u64::from_le_bytes(self.key_id))
    }
}

/// What a file must be to verify (FR-030, PD-4). Crate-private, fields too (SI-28).
pub(crate) struct Expected<'a> {
    pub(crate) name: &'a str,
    pub(crate) version: &'a str,
    pub(crate) platform: PlatformKey,
    pub(crate) asset: &'a AssetName,
    pub(crate) format: AssetFormat,
    pub(crate) size: u64,
    pub(crate) sha256: [u8; 32],
    pub(crate) signature: &'a str,
}

impl<'a> Expected<'a> {
    /// The feed-derived expectation of a request: the only production source (SI-28).
    pub(crate) fn from_request(request: &'a DownloadRequest) -> Expected<'a> {
        let asset = request.asset();
        Expected {
            name: request.name(),
            version: request.version_text(),
            platform: request.key(),
            asset: &asset.asset_name,
            format: asset.format,
            size: asset.size,
            sha256: asset.sha256,
            signature: &asset.signature,
        }
    }
}

/// A file that passed every check of `verify_file` (FR-039). No public constructor (FR-043).
///
/// ```compile_fail
/// use krate_manager_core::feed::{AssetFormat, AssetName, PlatformKey};
/// use krate_manager_core::verify::VerifiedArtifact;
/// fn forge(asset: &AssetName) -> VerifiedArtifact {
///     VerifiedArtifact {
///         path: std::path::PathBuf::new(), name: String::new(), version: String::new(),
///         platform: PlatformKey::LinuxX86_64, asset: asset.clone(), size: 0,
///         sha256: [0; 32], format: AssetFormat::TarGz,
///     }
/// }
/// ```
/// ```compile_fail
/// let _: krate_manager_core::verify::VerifiedArtifact = Default::default();
/// ```
/// ```no_run
/// use krate_manager_core::feed::{AssetFormat, AssetName, PlatformKey};
/// use krate_manager_core::verify::VerifiedArtifact;
/// fn read(a: &VerifiedArtifact, asset: &AssetName) -> (PlatformKey, AssetFormat, bool) {
///     (a.platform(), a.format(), a.asset() == asset)
/// }
/// ```
#[derive(Clone, Debug)]
pub struct VerifiedArtifact {
    path: PathBuf,
    name: String,
    version: String,
    platform: PlatformKey,
    asset: AssetName,
    size: u64,
    sha256: [u8; 32],
    format: AssetFormat,
}

impl VerifiedArtifact {
    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn version(&self) -> &str {
        &self.version
    }

    pub fn platform(&self) -> PlatformKey {
        self.platform
    }

    pub fn asset(&self) -> &AssetName {
        &self.asset
    }

    pub fn size(&self) -> u64 {
        self.size
    }

    pub fn sha256(&self) -> &[u8; 32] {
        &self.sha256
    }

    pub fn format(&self) -> AssetFormat {
        self.format
    }

    /// The same artifact at its new path after the rename (called only by `download.rs`, FR-043).
    pub(crate) fn relocated(self, to: PathBuf) -> VerifiedArtifact {
        VerifiedArtifact { path: to, ..self }
    }
}

/// D6's checks in order, stopping at the first failure (FR-031 to FR-038). Crate-private (SI-28).
pub(crate) fn verify_file(
    path: &Path,
    expected: &Expected<'_>,
    key: &SigningKey,
) -> Result<VerifiedArtifact, VerifyError> {
    let io_error = |e: io::Error| VerifyError::Io { kind: e.kind() };

    // 1. A regular file (PD-19) of the expected length, nothing read (FR-031).
    let meta = fs::symlink_metadata(path).map_err(io_error)?;
    if !meta.file_type().is_file() {
        return Err(VerifyError::Io {
            kind: io::ErrorKind::InvalidInput,
        });
    }
    let mut file = File::open(path).map_err(io_error)?;
    let found = file.metadata().map_err(io_error)?.len();
    if found != expected.size {
        return Err(VerifyError::Size {
            expected: expected.size,
            found,
        });
    }

    // 2. The signature text: the contract's rules, then minisign's decoder (FR-032).
    let text = contract::parse_minisig_text(expected.signature).map_err(|e| {
        VerifyError::SignatureText {
            detail: e.to_string(),
        }
    })?;
    let signature = minisign_verify::Signature::decode(expected.signature).map_err(|e| {
        VerifyError::SignatureText {
            detail: format!("minisign cannot decode the signature: {e}"),
        }
    })?;

    // 3. The key id, nothing read (FR-033).
    let mut verifier = key.public.verify_stream(&signature).map_err(|e| match e {
        minisign_verify::Error::UnexpectedKeyId => VerifyError::KeyId {
            expected: key.key_id_hex(),
            found: format!("{:016X}", u64::from_le_bytes(text.key_id)),
        },
        // `UnsupportedLegacyMode`: unreachable after step 2, which requires `ED`.
        other => VerifyError::SignatureText {
            detail: other.to_string(),
        },
    })?;

    // 4. One sequential read feeding both digests (FR-034).
    let mut sha = Sha256::new();
    let mut buf = vec![0u8; CHUNK];
    let mut total: u64 = 0;
    loop {
        let n = match file.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(io_error(e)),
        };
        let chunk = &buf[..n];
        sha.update(chunk);
        verifier.update(chunk);
        total += n as u64;
    }
    if total != expected.size {
        return Err(VerifyError::Size {
            expected: expected.size,
            found: total,
        });
    }

    // 5. The feed's SHA-256 (FR-035).
    let digest: [u8; 32] = sha.finalize().into();
    if digest != expected.sha256 {
        return Err(VerifyError::Sha256 {
            expected: hex_lower(&expected.sha256),
            found: hex_lower(&digest),
        });
    }

    // 6. The signature and the global signature over the trusted comment (FR-036).
    verifier.finalize().map_err(|_| VerifyError::Signature)?;

    // 7. The trusted comment's C3 form (FR-037).
    let tc = contract::parse_trusted_comment(signature.trusted_comment()).map_err(|e| match e {
        ContractError::Rule { code, message } => VerifyError::TrustedComment {
            code: code.as_str(),
            detail: message,
        },
        // `parse_trusted_comment` raises only `Rule`; mapped rather than panicking.
        other @ ContractError::Unsupported { .. } => VerifyError::TrustedComment {
            code: "E_SCHEMA_UNSUPPORTED",
            detail: other.to_string(),
        },
    })?;

    // 8. The binding, in FR-038's order, byte for byte.
    for (field, signed, wanted) in [
        ("name", tc.name, expected.name),
        ("version", tc.version, expected.version),
        ("platform", tc.platform, expected.platform.as_str()),
        ("asset", tc.asset, expected.asset.as_str()),
    ] {
        if signed != wanted {
            return Err(VerifyError::Binding {
                field,
                signed,
                expected: wanted.to_owned(),
            });
        }
    }

    // 9. Verified.
    Ok(VerifiedArtifact {
        path: path.to_path_buf(),
        name: expected.name.to_owned(),
        version: expected.version.to_owned(),
        platform: expected.platform,
        asset: expected.asset.clone(),
        size: expected.size,
        sha256: digest,
        format: expected.format,
    })
}

/// Lower-case hex, two digits per byte.
pub(crate) fn hex_lower(bytes: &[u8]) -> String {
    use std::fmt::Write as _;
    bytes
        .iter()
        .fold(String::with_capacity(bytes.len() * 2), |mut s, b| {
            // Writing to a String cannot fail.
            let _ = write!(s, "{b:02x}");
            s
        })
}

/// A streaming SHA-256 for `extract.rs` (Phase 8 FR-082): `sha2` stays confined to this file.
pub(crate) struct Sha256Stream(Sha256);

impl Sha256Stream {
    pub(crate) fn new() -> Sha256Stream {
        Sha256Stream(Sha256::new())
    }

    pub(crate) fn update(&mut self, bytes: &[u8]) {
        self.0.update(bytes);
    }

    pub(crate) fn finish(self) -> [u8; 32] {
        self.0.finalize().into()
    }
}

/// The SHA-256 of `bytes`, for tests (PD-13): `sha2` stays confined to this file (FR-082).
#[cfg(test)]
pub(crate) fn sha256_of(bytes: &[u8]) -> [u8; 32] {
    let mut sha = Sha256::new();
    sha.update(bytes);
    sha.finalize().into()
}

/// Why a file did not verify (plan §3). Every `Display` begins with its code.
#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum VerifyError {
    #[error("E_VERIFY_IO: the file cannot be read: {kind:?}")]
    Io { kind: io::ErrorKind },
    #[error("E_VERIFY_SIZE: expected {expected} bytes, found {found}")]
    Size { expected: u64, found: u64 },
    #[error("E_VERIFY_SIGNATURE_TEXT: {detail}")]
    SignatureText { detail: String },
    #[error("E_VERIFY_KEY_ID: signed by key {found}, expected key {expected}")]
    KeyId { expected: String, found: String },
    #[error("E_VERIFY_SHA256: expected {expected}, found {found}")]
    Sha256 { expected: String, found: String },
    #[error("E_VERIFY_SIGNATURE: the signature does not match the file")]
    Signature,
    #[error("E_VERIFY_TRUSTED_COMMENT: {code}: {detail}")]
    TrustedComment { code: &'static str, detail: String },
    #[error("E_TC_BINDING: the signed {field} is {signed:?}, the feed says {expected:?}")]
    Binding {
        field: &'static str,
        signed: String,
        expected: String,
    },
    #[error("E_VERIFY_KEY_TEXT: {detail}")]
    KeyText { detail: String },
}

impl VerifyError {
    pub fn code(&self) -> &'static str {
        match self {
            VerifyError::Io { .. } => "E_VERIFY_IO",
            VerifyError::Size { .. } => "E_VERIFY_SIZE",
            VerifyError::SignatureText { .. } => "E_VERIFY_SIGNATURE_TEXT",
            VerifyError::KeyId { .. } => "E_VERIFY_KEY_ID",
            VerifyError::Sha256 { .. } => "E_VERIFY_SHA256",
            VerifyError::Signature => "E_VERIFY_SIGNATURE",
            VerifyError::TrustedComment { .. } => "E_VERIFY_TRUSTED_COMMENT",
            VerifyError::Binding { .. } => "E_TC_BINDING",
            VerifyError::KeyText { .. } => "E_VERIFY_KEY_TEXT",
        }
    }
}

#[cfg(test)]
#[path = "verify_tests.rs"]
mod tests;
