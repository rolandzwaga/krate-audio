use super::*;
use crate::contract::{parse_minisig_text, parse_trusted_comment};
use crate::feed::AssetUrl;
use crate::test_signer::{TestSigner, c3_comment, with_key_id};
use crate::test_support::{fresh_temp_roots, native_key};

/// `tools/feed-contract/<rel>`, read in place (never copied).
fn feed_contract(rel: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../../tools/feed-contract")
        .join(rel)
}

fn read_text(rel: &str) -> String {
    let path = feed_contract(rel);
    std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()))
}

const KAT_PUB: &str = "fixtures/kat/kat.pub";
const KAT_SECRET_PUB: &str = "fixtures/phase2/kat-secret/kat.pub";
const RELEASE_PUB: &str = "keys/krate-signing.pub";

/// Line 2 of a `minisign.pub` text, decoded.
fn key_bytes(text: &str) -> Vec<u8> {
    let line2 = text.lines().nth(1).expect("a second line");
    STANDARD.decode(line2).expect("line 2 is base64")
}

#[test]
fn signing_key_text_cases() {
    let forty_one = {
        let mut bytes = vec![0x45, 0x64];
        bytes.resize(41, 7);
        format!(
            "untrusted comment: minisign public key: 0\n{}\n",
            STANDARD.encode(&bytes)
        )
    };
    for (label, text) in [
        ("empty", String::new()),
        (
            "one line",
            "untrusted comment: minisign public key: 440FA75168B852F7\n".to_owned(),
        ),
        (
            "line 2 not base64",
            "untrusted comment: minisign public key: 0\n!!! not base64 !!!\n".to_owned(),
        ),
        ("a 41-byte key", forty_one),
    ] {
        let error = SigningKey::from_text(&text).expect_err(label);
        assert_eq!(error.code(), "E_VERIFY_KEY_TEXT", "{label}: {error}");
        assert!(
            error.to_string().starts_with("E_VERIFY_KEY_TEXT: "),
            "{label}"
        );
    }

    let lf = read_text(KAT_PUB);
    assert!(!lf.contains('\r'), "kat.pub is LF in the tree");
    let crlf = lf.replace('\n', "\r\n");
    let from_lf = SigningKey::from_text(&lf).expect("the KAT key, LF");
    let from_crlf = SigningKey::from_text(&crlf).expect("the KAT key, CRLF");
    assert_eq!(from_lf, from_crlf, "LF and CRLF of one key are equal");
    let again = SigningKey::from_text(&read_text(KAT_PUB)).expect("the KAT key, second read");
    assert_eq!(from_lf, again, "two reads of one key are equal");
    let other = SigningKey::from_text(&read_text(KAT_SECRET_PUB)).expect("the Phase 2 KAT key");
    assert_ne!(from_lf, other, "two different keys differ");
    assert_eq!(from_lf.key_id_hex(), "440FA75168B852F7");
}

#[test]
fn release_key_is_the_committed_key_and_not_a_test_key() -> Result<(), VerifyError> {
    let file = read_text(RELEASE_PUB);
    let release = SigningKey::release();
    assert_eq!(release, SigningKey::from_text(&file)?);
    let kat = read_text(KAT_PUB);
    let kat_secret = read_text(KAT_SECRET_PUB);
    assert_ne!(release, SigningKey::from_text(&kat)?);
    assert_ne!(release, SigningKey::from_text(&kat_secret)?);
    assert_eq!(release.key_id_hex(), "9FEB36F9114273BC");
    assert!(file.starts_with("untrusted comment: minisign public key: 9FEB36F9114273BC\n"));
    for test_id in ["440FA75168B852F7", "63861317F7D60239"] {
        assert_ne!(release.key_id_hex(), test_id);
    }

    let committed = key_bytes(RELEASE_KEY_TEXT);
    assert_eq!(committed.len(), 42);
    assert_eq!(
        committed,
        key_bytes(&file),
        "the embedded text is the file's"
    );
    for (label, text) in [("kat", &kat), ("kat-secret", &kat_secret)] {
        let test_key = key_bytes(text);
        assert_eq!(test_key.len(), 42, "{label}");
        assert_ne!(committed[10..42], test_key[10..42], "{label}");
    }
    Ok(())
}

#[test]
fn digest_helpers_cases() {
    assert_eq!(
        hex_lower(&sha256_of(b"")),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    );
    assert_eq!(
        hex_lower(&sha256_of(b"abc")),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
    assert_eq!(hex_lower(&[0x00, 0xab, 0xff]), "00abff");
    assert_eq!(hex_lower(&[]), "");
}

/// Phase 8 (tasks TA-6): `Sha256Stream` equals `sha256_of` however the bytes are sliced.
#[test]
fn sha256_stream_cases() {
    let empty = Sha256Stream::new().finish();
    assert_eq!(empty, sha256_of(b""));
    assert_eq!(
        hex_lower(&empty),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    );

    let mut abc = Sha256Stream::new();
    for part in [&b"a"[..], b"", b"bc"] {
        abc.update(part);
    }
    let abc = abc.finish();
    assert_eq!(abc, sha256_of(b"abc"));
    assert_eq!(
        hex_lower(&abc),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );

    let big: Vec<u8> = (0..1usize << 20).map(|i| (i % 251) as u8).collect();
    let mut sliced = Sha256Stream::new();
    for chunk in big.chunks(7) {
        sliced.update(chunk);
    }
    assert_eq!(sliced.finish(), sha256_of(&big));
}

#[test]
fn test_signer_round_trip() {
    let signer = TestSigner::new();
    let other = TestSigner::new();
    assert_ne!(signer.key_id(), other.key_id(), "two fresh keys differ");
    assert_eq!(
        signer.key().key_id_hex(),
        format!("{:016X}", u64::from_le_bytes(signer.key_id()))
    );
    assert_ne!(signer.key(), other.key());

    let data = b"a test asset";
    let key = native_key();
    let asset = "Alpha-1.2.3-test.bin";
    let sig = signer.sign_c3(data, "Alpha", "1.2.3", key, asset);
    assert!(
        sig.ends_with('\n') && !sig.contains('\r'),
        "LF lines, final LF"
    );
    assert_eq!(sig.lines().count(), 4);
    let parsed = parse_minisig_text(&sig).expect("a contract-valid signature text");
    assert_eq!(parsed.key_id, signer.key_id());
    assert_eq!(
        parsed.trusted_comment,
        c3_comment("Alpha", "1.2.3", key, asset)
    );
    let tc = parse_trusted_comment(&parsed.trusted_comment).expect("a C3 trusted comment");
    assert_eq!(
        (
            tc.name.as_str(),
            tc.version.as_str(),
            tc.platform.as_str(),
            tc.asset.as_str()
        ),
        ("Alpha", "1.2.3", key.as_str(), asset)
    );

    let moved = with_key_id(&sig, other.key_id());
    let reparsed = parse_minisig_text(&moved).expect("still contract-valid");
    assert_eq!(reparsed.key_id, other.key_id());
    let (a, b): (Vec<&str>, Vec<&str>) = (sig.lines().collect(), moved.lines().collect());
    assert_eq!(a.len(), b.len());
    for (i, (x, y)) in a.iter().zip(&b).enumerate() {
        if i == 1 {
            assert_ne!(x, y, "line 2 carries the new id");
        } else {
            assert_eq!(x, y, "line {} unchanged", i + 1);
        }
    }
    assert!(moved.ends_with('\n'));
}

const KAT_ARCHIVE: &str = "fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz";
const KAT_MINISIG: &str = "fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.minisig";
/// `tools/feed-contract/fixtures/kat/README.md`'s constant (SI-1: the `.sha256` file is not read).
const KAT_SHA256: &str = "5b980e5b8ae7811a064966a1ec1380df016081a12bdeaf643486026007b62890";
const KAT_MESSAGE: &str = "fixtures/phase2/kat-secret/message.txt";
const KAT_MESSAGE_MINISIG: &str = "fixtures/phase2/kat-secret/message.txt.minisig";

/// The asset name of a C1 URL, as the feed derives it.
fn asset_name(url: &str) -> AssetName {
    let url = AssetUrl::checked(url).unwrap_or_else(|| panic!("not a C1 url: {url}"));
    AssetName::from_url(&url).unwrap_or_else(|| panic!("no safe asset name in {}", url.as_str()))
}

fn read_bytes(path: &Path) -> Vec<u8> {
    std::fs::read(path).unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()))
}

fn write_file(path: &Path, bytes: &[u8]) {
    std::fs::write(path, bytes).unwrap_or_else(|e| panic!("cannot write {}: {e}", path.display()));
}

/// `text` with line `index` (0-based, split on LF) replaced by `edit(line)`.
fn edit_line(text: &str, index: usize, edit: impl FnOnce(&str) -> String) -> String {
    let mut lines: Vec<String> = text.split('\n').map(str::to_owned).collect();
    let line = lines
        .get_mut(index)
        .unwrap_or_else(|| panic!("no line {index} in {text:?}"));
    *line = edit(line);
    lines.join("\n")
}

#[test]
fn kat_verifies_and_one_byte_changes_fail() -> Result<(), VerifyError> {
    let archive_path = feed_contract(KAT_ARCHIVE);
    let archive = read_bytes(&archive_path);
    let sha256 = sha256_of(&archive);
    assert_eq!(hex_lower(&sha256), KAT_SHA256);
    let signature = read_text(KAT_MINISIG);
    let key = SigningKey::from_text(&read_text(KAT_PUB))?;
    let asset = asset_name("https://assets.example.invalid/kat/KatPlugin-1.2.3-Linux-x64.tar.gz");
    let expected = Expected {
        name: "KatPlugin",
        version: "1.2.3",
        platform: PlatformKey::LinuxX86_64,
        asset: &asset,
        format: AssetFormat::TarGz,
        size: 452,
        sha256,
        signature: &signature,
    };

    let artifact = verify_file(&archive_path, &expected, &key)?;
    assert_eq!(artifact.path(), archive_path.as_path());
    assert_eq!(artifact.name(), "KatPlugin");
    assert_eq!(artifact.version(), "1.2.3");
    assert_eq!(artifact.platform(), PlatformKey::LinuxX86_64);
    assert_eq!(artifact.asset(), &asset);
    assert_eq!(artifact.format(), AssetFormat::TarGz);
    assert_eq!(artifact.size(), 452);
    assert_eq!(artifact.sha256(), &sha256);

    let (td, _roots) = fresh_temp_roots();
    let flipped_path = td.path().join("KatPlugin-1.2.3-Linux-x64.tar.gz");
    let mut flipped = archive.clone();
    flipped[200] ^= 0x01;
    write_file(&flipped_path, &flipped);
    let error = verify_file(&flipped_path, &expected, &key).expect_err("a flipped byte");
    assert_eq!(error.code(), "E_VERIFY_SHA256", "{error}");
    assert_eq!(
        error,
        VerifyError::Sha256 {
            expected: KAT_SHA256.to_owned(),
            found: hex_lower(&sha256_of(&flipped)),
        }
    );
    let rehashed = Expected {
        sha256: sha256_of(&flipped),
        ..expected
    };
    let error = verify_file(&flipped_path, &rehashed, &key).expect_err("a flipped byte, rehashed");
    assert_eq!(error, VerifyError::Signature, "{error}");

    // Line 2, character 50 (0-based) replaced by another base64 character: inside the
    // 64-byte signature (bytes 10..74), so the key id is unchanged and step 6 decides.
    let changed_sig = edit_line(&signature, 1, |line| {
        let mut chars: Vec<char> = line.chars().collect();
        chars[50] = if chars[50] == 'A' { 'B' } else { 'A' };
        chars.into_iter().collect()
    });
    assert_ne!(changed_sig, signature);
    let error = verify_file(
        &archive_path,
        &Expected {
            signature: &changed_sig,
            ..expected
        },
        &key,
    )
    .expect_err("a changed signature line");
    assert_eq!(error, VerifyError::Signature, "{error}");

    let changed_tc = edit_line(&signature, 2, |line| line.replace(":1.2.3:", ":1.2.4:"));
    assert!(changed_tc.contains("krate:1:KatPlugin:1.2.4:linux-x86_64:"));
    let error = verify_file(
        &archive_path,
        &Expected {
            signature: &changed_tc,
            ..expected
        },
        &key,
    )
    .expect_err("an edited trusted comment");
    assert_eq!(error, VerifyError::Signature, "{error}");
    Ok(())
}

#[test]
fn kat_secret_message_verifies() -> Result<(), VerifyError> {
    let path = feed_contract(KAT_MESSAGE);
    let message = read_bytes(&path);
    assert_eq!(message.len(), 25);
    let sha256 = sha256_of(&message);
    let signature = read_text(KAT_MESSAGE_MINISIG);
    let key = SigningKey::from_text(&read_text(KAT_SECRET_PUB))?;
    let asset = asset_name("https://assets.example.invalid/kat-secret/message.txt");
    let expected = Expected {
        name: "KatPlugin",
        version: "1.2.3",
        platform: PlatformKey::LinuxX86_64,
        asset: &asset,
        format: AssetFormat::TarGz,
        size: 25,
        sha256,
        signature: &signature,
    };
    let artifact = verify_file(&path, &expected, &key)?;
    assert_eq!(artifact.path(), path.as_path());
    assert_eq!(
        (artifact.name(), artifact.version(), artifact.platform()),
        ("KatPlugin", "1.2.3", PlatformKey::LinuxX86_64)
    );
    assert_eq!(artifact.asset().as_str(), "message.txt");
    assert_eq!(artifact.size(), 25);
    assert_eq!(artifact.sha256(), &sha256);
    Ok(())
}

/// A temp file holding `data`, with `Expected` fields `Alpha`, `1.2.3`, `linux-x86_64`, `a.bin`.
struct Bench {
    _td: tempfile::TempDir,
    path: PathBuf,
    data: Vec<u8>,
    asset: AssetName,
}

impl Bench {
    fn new() -> Bench {
        let (td, _roots) = fresh_temp_roots();
        let path = td.path().join("a.bin");
        let data = b"a test asset for verify_file".to_vec();
        write_file(&path, &data);
        Bench {
            _td: td,
            path,
            data,
            asset: asset_name("https://assets.example.invalid/t/a.bin"),
        }
    }

    fn expected<'a>(&'a self, signature: &'a str) -> Expected<'a> {
        Expected {
            name: "Alpha",
            version: "1.2.3",
            platform: PlatformKey::LinuxX86_64,
            asset: &self.asset,
            format: AssetFormat::TarGz,
            size: self.data.len() as u64,
            sha256: sha256_of(&self.data),
            signature,
        }
    }
}

#[test]
fn invalid_trusted_comment_is_named() {
    let bench = Bench::new();
    let signer = TestSigner::new();
    for (comment, code) in [
        ("timestamp:1", "E_TC_FIELDS"),
        ("krate:1:alpha:1.2.3:linux-x86_64:a.bin", "E_TC_NAME"),
    ] {
        let signature = signer.sign(&bench.data, comment);
        let error = verify_file(&bench.path, &bench.expected(&signature), &signer.key())
            .expect_err(comment);
        assert_eq!(
            error.code(),
            "E_VERIFY_TRUSTED_COMMENT",
            "{comment}: {error}"
        );
        let VerifyError::TrustedComment { code: found, .. } = &error else {
            panic!("{comment}: not a TrustedComment: {error:?}");
        };
        assert_eq!(*found, code, "{comment}");
    }
}

#[test]
fn verify_steps_stop_at_first_failure() {
    let bench = Bench::new();
    let signer = TestSigner::new();
    let other = TestSigner::new();
    let good = signer.sign_c3(
        &bench.data,
        "Alpha",
        "1.2.3",
        PlatformKey::LinuxX86_64,
        "a.bin",
    );
    let size = bench.data.len() as u64;

    // Step 1 before step 2: a wrong size with a malformed signature reports the size.
    let malformed = "not a minisign signature";
    let wrong_size = Expected {
        size: size + 1,
        ..bench.expected(malformed)
    };
    let error = verify_file(&bench.path, &wrong_size, &signer.key()).expect_err("wrong size");
    assert_eq!(
        error,
        VerifyError::Size {
            expected: size + 1,
            found: size,
        }
    );
    let error = verify_file(&bench.path, &bench.expected(malformed), &signer.key())
        .expect_err("malformed signature, right size");
    assert_eq!(error.code(), "E_VERIFY_SIGNATURE_TEXT", "{error}");

    // Step 3: signed by `signer`, checked with `other`'s key.
    let error =
        verify_file(&bench.path, &bench.expected(&good), &other.key()).expect_err("another key");
    let VerifyError::KeyId { expected, found } = &error else {
        panic!("not a KeyId: {error:?}");
    };
    assert_eq!(expected, &other.key().key_id_hex());
    assert_eq!(found, &signer.key().key_id_hex());
    assert_ne!(expected, found);
    for id in [expected, found] {
        assert_eq!(id.len(), 16, "{id}");
        assert!(
            id.chars()
                .all(|c| c.is_ascii_digit() || ('A'..='F').contains(&c)),
            "{id}"
        );
    }

    // Step 2: a legacy (`Ed`) signature text.
    let legacy = edit_line(&good, 1, |line| {
        let mut blob = STANDARD.decode(line).expect("line 2 is base64");
        blob[1] = b'd';
        STANDARD.encode(&blob)
    });
    let error = verify_file(&bench.path, &bench.expected(&legacy), &signer.key())
        .expect_err("a legacy signature");
    assert_eq!(error.code(), "E_VERIFY_SIGNATURE_TEXT", "{error}");

    // Step 1: a path that does not exist, and one that is not a regular file (PD-19).
    let missing = bench.path.with_file_name("missing.bin");
    let error =
        verify_file(&missing, &bench.expected(&good), &signer.key()).expect_err("a missing file");
    assert_eq!(
        error,
        VerifyError::Io {
            kind: io::ErrorKind::NotFound
        }
    );
    let dir = bench.path.parent().expect("a parent");
    let error = verify_file(dir, &bench.expected(&good), &signer.key()).expect_err("a directory");
    assert_eq!(
        error,
        VerifyError::Io {
            kind: io::ErrorKind::InvalidInput
        }
    );

    // Step 8, FR-038's order: the first differing field is the one reported.
    for (label, name, version, platform, asset, field, signed, wanted) in [
        (
            "name and version",
            "Beta",
            "1.2.4",
            PlatformKey::LinuxX86_64,
            "a.bin",
            "name",
            "Beta",
            "Alpha",
        ),
        (
            "version and asset",
            "Alpha",
            "1.2.4",
            PlatformKey::LinuxX86_64,
            "b.bin",
            "version",
            "1.2.4",
            "1.2.3",
        ),
        (
            "platform and asset",
            "Alpha",
            "1.2.3",
            PlatformKey::WindowsX86_64,
            "b.bin",
            "platform",
            "windows-x86_64",
            "linux-x86_64",
        ),
        (
            "asset only",
            "Alpha",
            "1.2.3",
            PlatformKey::LinuxX86_64,
            "b.bin",
            "asset",
            "b.bin",
            "a.bin",
        ),
    ] {
        let signature = signer.sign_c3(&bench.data, name, version, platform, asset);
        let error =
            verify_file(&bench.path, &bench.expected(&signature), &signer.key()).expect_err(label);
        assert_eq!(
            error,
            VerifyError::Binding {
                field,
                signed: signed.to_owned(),
                expected: wanted.to_owned(),
            },
            "{label}"
        );
    }

    // The same bench verifies with the good signature, so each row above failed at its own step.
    let artifact = verify_file(&bench.path, &bench.expected(&good), &signer.key())
        .expect("the good signature verifies");
    assert_eq!(artifact.asset(), &bench.asset);
}
