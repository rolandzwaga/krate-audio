//! Scenario keys (FR-081 (d), SI-23): every key a scenario uses comes from `scenario_key` and passes
//! `refuse_committed` before any fixture server starts. Keys live in memory only.

use crate::{HarnessError, Plant};
use base64::Engine;
use base64::engine::general_purpose::STANDARD;
use minisign::{KeyPair, SecretKey};
use std::io::Cursor;

/// The committed release public key, the same file `crates/core/src/verify.rs:15` includes.
const COMMITTED_KEY_TEXT: &str =
    include_str!("../../../../../../tools/feed-contract/keys/krate-signing.pub");

/// One scenario key: its public key box text and, for a generated key, the signer.
pub struct ScenarioKey {
    pub public_box_text: String,
    pub signer: Option<SecretKey>,
}

/// A fresh throwaway key pair, or under `--plant committed-key` the committed public key and no signer.
pub fn scenario_key(plant: Option<Plant>) -> ScenarioKey {
    if plant == Some(Plant::CommittedKey) {
        return ScenarioKey {
            public_box_text: COMMITTED_KEY_TEXT.to_owned(),
            signer: None,
        };
    }
    let KeyPair { pk, sk } =
        KeyPair::generate_unencrypted_keypair().expect("generate a throwaway key pair");
    let public_box_text = pk.to_box().expect("public key box").to_string();
    ScenarioKey {
        public_box_text,
        signer: Some(sk),
    }
}

/// The key line (the second line) of a minisign public key box, trimmed.
fn key_line(text: &str) -> Option<&str> {
    text.lines().nth(1).map(str::trim)
}

/// FR-081 (d): `E_HARNESS_KEY` when `public_box_text` is the committed release key.
pub fn refuse_committed_text(public_box_text: &str) -> Result<(), HarnessError> {
    match (key_line(public_box_text), key_line(COMMITTED_KEY_TEXT)) {
        (Some(a), Some(b)) if a == b => Err(HarnessError::Key),
        _ => Ok(()),
    }
}

/// FR-081 (d) for a scenario key.
pub fn refuse_committed(key: &ScenarioKey) -> Result<(), HarnessError> {
    refuse_committed_text(&key.public_box_text)
}

/// The updater config's `pubkey`: base64 of the public key box text (the Tauri form).
pub fn tauri_pubkey(public_box_text: &str) -> String {
    STANDARD.encode(public_box_text)
}

/// A Tauri-form signature (base64 of the signature box) over `bytes`, the trusted comment
/// `timestamp:0\tfile:pkg\tversion:<v>` or, with `version` `None`, without the `version:` field.
pub fn sign(key: &ScenarioKey, bytes: &[u8], version: Option<&str>) -> String {
    let sk = key
        .signer
        .as_ref()
        .expect("a generated scenario key has a signer");
    let tc = match version {
        Some(v) => format!("timestamp:0\tfile:pkg\tversion:{v}"),
        None => "timestamp:0\tfile:pkg".to_owned(),
    };
    let sig = minisign::sign(None, sk, Cursor::new(bytes), Some(&tc), Some("harness"))
        .expect("sign the scenario package");
    STANDARD.encode(sig.to_string())
}
