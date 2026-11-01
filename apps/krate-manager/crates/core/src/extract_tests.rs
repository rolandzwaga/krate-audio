//! Phase 8 extract tests (plan §7; T003). Every test stages under a fresh `TempDir` (Rule 1); every refusal row stages
//! over `RecordingExtractIo::fail_closed()` (FR-087); every artifact is a real `VerifiedArtifact` from `verify_file`
//! (FR-084): fixture archives are signed in memory by a `TestSigner`, the KAT archive is checked with the KAT key.
//! Runtime archives are built here by a ustar builder into memory and written only into the test's `TempDir`
//! (FR-085); a runtime archive that must stage carries a payload hashed from a listing written in the test.

use super::*;
use crate::feed::{PlatformAsset, PlatformKey};
use crate::http::HttpConfig;
use crate::roots::Roots;
use crate::settings::Settings;
use crate::test_signer::TestSigner;
use crate::test_support::{
    FaultyExtractIo, MemExtractIo, MemNode, ReadFault, RecordingExtractIo, RemoveTreeMode,
    TestPlugin, TwoPhaseArchive, XKind, XOp, fresh_temp_roots, install_capture, kat_signing_key,
    snapshot, take_mine, test_feed,
};
use crate::verify::{Expected, SigningKey, verify_file};
use log::Level;
use std::io::Write as _;
use std::sync::Barrier;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::thread;
use std::time::Duration;
use tempfile::TempDir;

const V: &str = "1.2.0";
const ALPHA: &str = "Alpha";
const SEC: Duration = Duration::from_secs(1);
const LIMITS: ExtractLimits = ExtractLimits::PRODUCTION;
const KAT_ASSET: &str = "KatPlugin-1.2.3-Linux-x64.tar.gz";
const KAT_DIGEST: &str = "db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6";
const GOOD: [&str; 9] = [
    "dot-slash",
    "exec-and-symlink",
    "implicit-dirs",
    "long-link",
    "long-name",
    "pax-global",
    "pax-path",
    "real-shaped",
    "ustar-prefix",
];
const BAD: [&str; 22] = [
    "absolute-path",
    "backslash-path",
    "bad-checksum",
    "base256-size",
    "char-device",
    "dotdot-path",
    "drive-path",
    "duplicate-entry",
    "file-dir-clash",
    "hard-link",
    "layout-extra-root-file",
    "layout-missing-presets",
    "no-end-block",
    "pax-malformed",
    "root-symlink",
    "symlink-chain",
    "symlink-dotdot-chain",
    "symlink-escape-bundle",
    "symlink-escape-root",
    "symlink-self",
    "through-symlink",
    "truncated",
];
/// The listing of the runtime base tree (`base`), written out (FR-085).
const BASE_LINES: [&str; 10] = [
    "d 0755 Alpha.vst3",
    "d 0755 Alpha.vst3/Contents",
    "d 0755 Alpha.vst3/Contents/Resources",
    "f 0644 Alpha.vst3/Contents/Resources/moduleinfo.json",
    "d 0755 Alpha.vst3/Contents/x86_64-linux",
    "f 0644 Alpha.vst3/Contents/x86_64-linux/Alpha.so",
    "f 0644 CHANGELOG.txt",
    "f 0644 README.txt",
    "d 0755 presets",
    "f 0644 presets/Init.vstpreset",
];
const SO: &str = "Alpha.vst3/Contents/x86_64-linux/Alpha.so";
const LIBALPHA: &str = "Alpha.vst3/Contents/x86_64-linux/libalpha.so";

// ---- fixtures, hashes, payloads ------------------------------------------------------------------------------------

/// `tools/feed-contract/fixtures/<rel>`, read in place (FR-085).
fn read_fixture(rel: &str) -> Vec<u8> {
    let path = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../../tools/feed-contract/fixtures")
        .join(rel);
    fs::read(&path).unwrap_or_else(|e| panic!("cannot read the fixture {rel}: {e}"))
}

fn good(name: &str) -> Vec<u8> {
    read_fixture(&format!("listings/good/{name}.tar.gz"))
}

fn bad(name: &str) -> Vec<u8> {
    read_fixture(&format!("listings/bad/{name}.tar.gz"))
}

fn good_listing(name: &str) -> String {
    String::from_utf8(read_fixture(&format!("listings/good/{name}.listing"))).expect("UTF-8")
}

fn sha(bytes: &[u8]) -> [u8; 32] {
    let mut s = Sha256Stream::new();
    s.update(bytes);
    s.finish()
}

fn unhex(text: &str) -> [u8; 32] {
    let mut out = [0u8; 32];
    for (i, b) in out.iter_mut().enumerate() {
        *b = u8::from_str_radix(&text[2 * i..2 * i + 2], 16).expect("hex digits");
    }
    out
}

/// The committed `.listing` line count and `.sha256` digest of a good fixture.
fn good_payload(name: &str) -> Payload {
    let digest =
        String::from_utf8(read_fixture(&format!("listings/good/{name}.sha256"))).expect("UTF-8");
    Payload {
        files: good_listing(name).lines().count() as u64,
        sha256: unhex(&digest[..64]),
    }
}

/// A payload from a listing written in the test (FR-085), hashed with `Sha256Stream`.
fn literal_payload<S: AsRef<str>>(lines: &[S]) -> Payload {
    let text: String = lines.iter().map(|l| format!("{}\n", l.as_ref())).collect();
    Payload {
        files: lines.len() as u64,
        sha256: sha(text.as_bytes()),
    }
}

/// `BASE_LINES` plus `extra`, sorted by the test by path (byte order, as C4).
fn lines_with(extra: &[&str]) -> Vec<String> {
    let mut lines: Vec<String> = BASE_LINES
        .iter()
        .chain(extra)
        .map(|l| (*l).to_owned())
        .collect();
    lines.sort_by(|a, b| a[7..].cmp(&b[7..]));
    lines
}

fn plain_name(s: &str) -> PlainName {
    PlainName::checked("vst3_bundle", &Value::String(s.to_owned())).expect("a valid bundle name")
}

fn gz(bytes: &[u8]) -> Vec<u8> {
    let mut encoder = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
    encoder.write_all(bytes).expect("gzip write");
    encoder.finish().expect("gzip finish")
}

fn gunzip(bytes: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    flate2::read::MultiGzDecoder::new(bytes)
        .read_to_end(&mut out)
        .expect("gunzip");
    out
}

fn moduleinfo(version: &str) -> Vec<u8> {
    format!(
        r#"{{"Name": "Alpha", "Version": "{version}", "Classes": [{{"Name": "Alpha", "Version": "{version}"}}]}}"#
    )
    .into_bytes()
}

// ---- the ustar builder (FR-085) ------------------------------------------------------------------------------------

fn octal_field(field: &mut [u8], value: u64) {
    let width = field.len() - 1;
    let text = format!("{value:0width$o}");
    field[..width].copy_from_slice(text.as_bytes());
    field[width] = 0;
}

/// Recomputes the checksum of the header at `off` (`%06o\0 `).
fn reseal(tar: &mut [u8], off: usize) {
    tar[off + 148..off + 156].copy_from_slice(b"        ");
    let sum: u32 = tar[off..off + 512].iter().map(|&b| u32::from(b)).sum();
    let text = format!("{sum:06o}");
    tar[off + 148..off + 154].copy_from_slice(text.as_bytes());
    tar[off + 154] = 0;
    tar[off + 155] = b' ';
}

/// Hand-made POSIX ustar records: 512-byte headers with correct checksums, optional GNU `L`, pax `x` / `g`.
struct Tar {
    bytes: Vec<u8>,
    mtime: u64,
}

impl Tar {
    fn new() -> Tar {
        Tar {
            bytes: Vec::new(),
            mtime: 1_793_232_000,
        }
    }

    fn record(&mut self, name: &[u8], flag: u8, mode: u32, data: &[u8], link: &[u8]) -> &mut Tar {
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

    fn file(&mut self, path: &str, mode: u32, data: &[u8]) -> &mut Tar {
        self.record(path.as_bytes(), b'0', mode, data, b"")
    }

    fn dir(&mut self, path: &str) -> &mut Tar {
        self.record(format!("{path}/").as_bytes(), b'5', 0o755, b"", b"")
    }

    fn link(&mut self, path: &str, target: &str) -> &mut Tar {
        self.record(path.as_bytes(), b'2', 0o777, b"", target.as_bytes())
    }

    /// A GNU `L` record carrying `name` (then a NUL), followed by a file header that uses it.
    fn long_file(&mut self, name: &[u8], data: &[u8]) -> &mut Tar {
        let mut long = name.to_vec();
        long.push(0);
        self.record(b"././@LongLink", b'L', 0o644, &long, b"");
        self.record(name, b'0', 0o644, data, b"")
    }

    fn pax(&mut self, flag: u8, records: &[(&str, &[u8])]) -> &mut Tar {
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
    fn finish(&self) -> Vec<u8> {
        let mut out = self.bytes.clone();
        out.extend(std::iter::repeat_n(0u8, 1024));
        out
    }
}

/// The 10-entry C4 base tree (`BASE_LINES`) with the given `moduleinfo.json`.
fn base(t: &mut Tar, info: &[u8]) {
    t.dir("Alpha.vst3")
        .dir("Alpha.vst3/Contents")
        .dir("Alpha.vst3/Contents/Resources")
        .file("Alpha.vst3/Contents/Resources/moduleinfo.json", 0o644, info)
        .dir("Alpha.vst3/Contents/x86_64-linux")
        .file(SO, 0o644, b"alpha shared object")
        .dir("presets")
        .file("presets/Init.vstpreset", 0o644, b"init preset")
        .file("README.txt", 0o644, b"read me")
        .file("CHANGELOG.txt", 0o644, b"changes");
}

/// `gz` of `before`, the base tree, then `after`.
fn archive(before: impl FnOnce(&mut Tar), after: impl FnOnce(&mut Tar)) -> Vec<u8> {
    let mut t = Tar::new();
    before(&mut t);
    base(&mut t, &moduleinfo(V));
    after(&mut t);
    gz(&t.finish())
}

fn base_with(after: impl FnOnce(&mut Tar)) -> Vec<u8> {
    archive(|_| {}, after)
}

fn cstr(bytes: &[u8]) -> String {
    String::from_utf8_lossy(cut_at_nul(bytes)).into_owned()
}

/// `(header offset, path, typeflag, size)` of every entry header of an uncompressed tar; the test's own walk.
fn tar_headers(tar: &[u8]) -> Vec<(usize, String, u8, usize)> {
    let mut out = Vec::new();
    let mut off = 0;
    let mut long: Option<String> = None;
    while off + 512 <= tar.len() {
        let h = &tar[off..off + 512];
        if h.iter().all(|&b| b == 0) {
            break;
        }
        let size = usize::try_from(octal_value(&h[124..136]).expect("octal size")).expect("size");
        let flag = h[156];
        let data = &tar[off + 512..off + 512 + size];
        let mut name = cstr(&h[0..100]);
        let prefix = cstr(&h[345..500]);
        if &h[257..263] == b"ustar\0" && !prefix.is_empty() {
            name = format!("{prefix}/{name}");
        }
        match flag {
            b'L' => long = Some(cstr(data)),
            b'K' | b'x' | b'g' => {}
            _ => {
                let path = long.take().unwrap_or(name);
                let path = path.strip_prefix("./").unwrap_or(&path).to_owned();
                out.push((off, path, flag, size));
            }
        }
        off += 512 + size.div_ceil(512) * 512;
    }
    out
}

/// The offset of the end-of-archive block.
fn end_block(tar: &[u8]) -> usize {
    let (off, _, _, size) = tar_headers(tar).last().cloned().expect("an entry");
    off + 512 + size.div_ceil(512) * 512
}

/// Every regular file of an uncompressed tar with its bytes.
fn tar_files(tar: &[u8]) -> BTreeMap<String, Vec<u8>> {
    tar_headers(tar)
        .into_iter()
        .filter(|(_, _, flag, _)| matches!(flag, b'0' | 0))
        .map(|(off, path, _, size)| (path, tar[off + 512..off + 512 + size].to_vec()))
        .collect()
}

// ---- the rig ----------------------------------------------------------------------------------------------------------

/// A context over temp roots (Rule 1): loopback HTTP that is never used, a feed URL that is never fetched.
fn context(roots: Roots, signing_key: SigningKey) -> AppContext {
    AppContext {
        roots,
        settings: Settings::default(),
        feed_url: "https://feed.example.invalid/versions.json".to_owned(),
        http: HttpConfig::loopback(SEC, SEC),
        signing_key,
    }
}

/// One verified artifact and the feed entry that describes it.
struct Art {
    entry: PluginEntry,
    artifact: VerifiedArtifact,
}

struct Rig {
    td: TempDir,
    ctx: AppContext,
    /// The roots' base inside the `TempDir`, spelled as the roots are.
    base: PathBuf,
    next: AtomicU32,
}

impl Rig {
    fn new() -> Rig {
        let (td, roots) = fresh_temp_roots();
        Rig::over(td, roots)
    }

    fn over(td: TempDir, roots: Roots) -> Rig {
        let base = roots.state_dir().parent().expect("a base").to_path_buf();
        Rig {
            td,
            ctx: context(roots, kat_signing_key()),
            base,
            next: AtomicU32::new(0),
        }
    }

    fn staging(&self) -> &Path {
        self.ctx.roots.staging_dir()
    }

    fn extract(&self) -> PathBuf {
        self.staging().join(EXTRACT_DIR)
    }

    fn env<'a>(&'a self, io: &'a dyn ExtractIo, limits: &'a ExtractLimits) -> ExtractEnv<'a> {
        ExtractEnv {
            ctx: &self.ctx,
            io,
            limits,
        }
    }

    /// A fresh `<base>/art<n>/<asset>` outside the staging dir.
    fn art_path(&self, asset: &str) -> PathBuf {
        let n = self.next.fetch_add(1, Ordering::SeqCst);
        let dir = self.base.join(format!("art{n}"));
        fs::create_dir_all(&dir).expect("the artifact dir");
        dir.join(asset)
    }

    /// `bytes` written into the `TempDir`, signed in memory and verified (FR-084), with its feed entry.
    fn signed_as(
        &self,
        name: &str,
        key: PlatformKey,
        asset: &str,
        bytes: &[u8],
        version: &str,
    ) -> Art {
        let path = self.art_path(asset);
        fs::write(&path, bytes).expect("write the artifact");
        let signer = TestSigner::new();
        let signature = signer.sign_c3(bytes, name, version, key, asset);
        let digest = sha(bytes);
        let feed = test_feed(&[TestPlugin {
            name,
            version,
            key,
            asset,
            size: bytes.len() as u64,
            sha256: digest,
            signature: signature.clone(),
            min_os: None,
        }]);
        let entry = feed.plugins.get(name).expect("the entry").clone();
        let platform = entry
            .release
            .as_ref()
            .and_then(|r| r.platforms.get(&key))
            .expect("the asset")
            .clone();
        let expected = Expected {
            name,
            version,
            platform: key,
            asset: &platform.asset_name,
            format: platform.format,
            size: platform.size,
            sha256: digest,
            signature: &signature,
        };
        let artifact = verify_file(&path, &expected, &signer.key()).expect("the artifact verifies");
        Art { entry, artifact }
    }

    /// An Alpha Linux artifact of `bytes` at `version` whose entry carries `payload`.
    fn signed(&self, bytes: &[u8], version: &str, payload: Payload) -> Art {
        let asset = format!("Alpha-{version}-Linux-x64.tar.gz");
        let mut art = self.signed_as(ALPHA, PlatformKey::LinuxX86_64, &asset, bytes, version);
        set_payload(&mut art.entry, Some(payload));
        art
    }

    fn signed_good(&self, name: &str) -> Art {
        self.signed(&good(name), V, good_payload(name))
    }

    /// The KAT archive copied into the `TempDir` and verified with the KAT key (FR-084).
    fn kat(&self) -> Art {
        let bytes = read_fixture(&format!("kat/{KAT_ASSET}"));
        let signature =
            String::from_utf8(read_fixture(&format!("kat/{KAT_ASSET}.minisig"))).expect("UTF-8");
        let path = self.art_path(KAT_ASSET);
        fs::write(&path, &bytes).expect("copy the KAT archive");
        let key = PlatformKey::LinuxX86_64;
        let digest = sha(&bytes);
        let feed = test_feed(&[TestPlugin {
            name: "KatPlugin",
            version: "1.2.3",
            key,
            asset: KAT_ASSET,
            size: bytes.len() as u64,
            sha256: digest,
            signature: signature.clone(),
            min_os: None,
        }]);
        let mut entry = feed.plugins.get("KatPlugin").expect("KatPlugin").clone();
        let platform = entry
            .release
            .as_ref()
            .and_then(|r| r.platforms.get(&key))
            .expect("the asset")
            .clone();
        let expected = Expected {
            name: "KatPlugin",
            version: "1.2.3",
            platform: key,
            asset: &platform.asset_name,
            format: platform.format,
            size: platform.size,
            sha256: digest,
            signature: &signature,
        };
        let artifact =
            verify_file(&path, &expected, &kat_signing_key()).expect("the KAT archive verifies");
        set_payload(
            &mut entry,
            Some(Payload {
                files: 10,
                sha256: unhex(KAT_DIGEST),
            }),
        );
        Art { entry, artifact }
    }

    /// The names in `extract/` on disk (empty when it is absent).
    fn extract_names(&self) -> Vec<String> {
        match fs::read_dir(self.extract()) {
            Ok(items) => items
                .map(|i| {
                    i.expect("an entry")
                        .file_name()
                        .to_string_lossy()
                        .into_owned()
                })
                .collect(),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Vec::new(),
            Err(e) => panic!("cannot list extract: {e}"),
        }
    }

    /// The direct children of `extract/` in a Mem tree.
    fn mem_children(&self, mem: &MemExtractIo) -> Vec<PathBuf> {
        let extract = self.extract();
        mem.paths_under(&extract)
            .into_iter()
            .filter(|p| p.parent() == Some(extract.as_path()))
            .collect()
    }
}

fn platform_mut(entry: &mut PluginEntry) -> &mut PlatformAsset {
    entry
        .release
        .as_mut()
        .and_then(|r| r.platforms.get_mut(&PlatformKey::LinuxX86_64))
        .expect("a Linux asset")
}

fn set_payload(entry: &mut PluginEntry, payload: Option<Payload>) {
    platform_mut(entry).payload = payload;
}

fn err_of(result: Result<StagedTree, ExtractError>) -> ExtractError {
    match result {
        Ok(tree) => panic!("staged at {:?}, expected a refusal", tree.root()),
        Err(e) => e,
    }
}

fn at(root: &Path, rel: &str) -> PathBuf {
    entry_path(root, rel).expect("a valid relative path")
}

fn write_records(calls: &[XOp], path: &Path) -> Vec<u8> {
    calls
        .iter()
        .filter_map(|op| match op {
            XOp::Write { path: p, bytes } if p == path => Some(bytes.clone()),
            _ => None,
        })
        .flatten()
        .collect()
}

fn created_files(calls: &[XOp]) -> Vec<PathBuf> {
    calls
        .iter()
        .filter_map(|op| match op {
            XOp::CreateFile(p) => Some(p.clone()),
            _ => None,
        })
        .collect()
}

fn removed_trees(calls: &[XOp]) -> Vec<PathBuf> {
    calls
        .iter()
        .filter_map(|op| match op {
            XOp::RemoveTree(p) => Some(p.clone()),
            _ => None,
        })
        .collect()
}

/// A row's expected code and, for `E_EXTRACT_LIMIT`, its bound.
type Want = (&'static str, Option<&'static str>);
/// A `read_tree` tamper of SC-019.
type Tamper = Box<dyn Fn(&mut Vec<ListingEntry>) + Send + Sync>;

/// The code and, for `E_EXTRACT_LIMIT`, the bound.
fn code_of(e: &ExtractError) -> Want {
    match e {
        ExtractError::Limit { which } => (e.code(), Some(*which)),
        _ => (e.code(), None),
    }
}

fn f(p: &[u8]) -> ListingEntry {
    ListingEntry {
        kind: EntryType::File,
        mode: Some(0o644),
        path: p.to_vec(),
        target: None,
    }
}

fn fm(p: &[u8], mode: u32) -> ListingEntry {
    ListingEntry {
        mode: Some(mode),
        ..f(p)
    }
}

fn d(p: &[u8], mode: u32) -> ListingEntry {
    ListingEntry {
        kind: EntryType::Dir,
        mode: Some(mode),
        path: p.to_vec(),
        target: None,
    }
}

fn l(p: &[u8], t: &[u8]) -> ListingEntry {
    ListingEntry {
        kind: EntryType::Symlink,
        mode: Some(0o777),
        path: p.to_vec(),
        target: Some(t.to_vec()),
    }
}

/// `presets/b…/b…/c…` of exactly `total` bytes, segments of at most 200 bytes.
fn long_path(total: usize) -> String {
    let mut p = String::from("presets");
    while total - p.len() > 202 {
        p.push('/');
        p.push_str(&"b".repeat(200));
    }
    let rest = total - p.len() - 1;
    p.push('/');
    p.push_str(&"c".repeat(rest));
    p
}

/// The listing lines a file at `path` (under `presets`) adds: its implied directories and itself.
fn file_lines(path: &str) -> Vec<String> {
    let mut lines: Vec<String> = path
        .match_indices('/')
        .map(|(i, _)| &path[..i])
        .filter(|q| *q != "presets")
        .map(|q| format!("d 0755 {q}"))
        .collect();
    lines.push(format!("f 0644 {path}"));
    lines
}

fn sorted_with(extra: Vec<String>) -> Vec<String> {
    let mut lines: Vec<String> = BASE_LINES.iter().map(|l| (*l).to_owned()).collect();
    lines.extend(extra);
    lines.sort_by(|a, b| a[7..].cmp(&b[7..]));
    lines
}

/// One refusal row (SC-006): fail-closed, then (rows inside the `TempDir`) over the real filesystem too.
fn refused(
    label: &str,
    bytes: &[u8],
    payload: Payload,
    limits: &ExtractLimits,
    want: (&str, Option<&str>),
    inside_temp: bool,
) {
    let rig = Rig::new();
    let art = rig.signed(bytes, V, payload);
    let before = snapshot(rig.td.path());
    let rec = RecordingExtractIo::fail_closed();
    let e = err_of(stage(rig.env(&rec, limits), &art.entry, &art.artifact));
    assert_eq!(code_of(&e), want, "{label}: {e}");
    assert_eq!(rec.mutating_calls(), 0, "{label}: {:?}", rec.calls());
    assert_eq!(snapshot(rig.td.path()), before, "{label}");
    assert!(rig.extract_names().is_empty(), "{label}");
    if inside_temp {
        let fwd = RecordingExtractIo::forwarding(&FsExtractIo);
        let e = err_of(stage(rig.env(&fwd, limits), &art.entry, &art.artifact));
        assert_eq!(code_of(&e), want, "{label} (forwarding): {e}");
        assert_eq!(fwd.mutating_calls(), 0, "{label} (forwarding)");
        assert_eq!(snapshot(rig.td.path()), before, "{label} (forwarding)");
        assert!(rig.extract_names().is_empty(), "{label} (forwarding)");
    }
}

const NO_PAYLOAD: Payload = Payload {
    files: 64,
    sha256: [0; 32],
};

// ---- SC-002, SC-003 --------------------------------------------------------------------------------------------------

#[test]
fn good_fixtures_match_committed_listings() {
    for name in GOOD {
        let listing = good_listing(name);
        let lines = listing.lines().count() as u64;
        let plan =
            plan_archive(&good(name)[..], &LIMITS, lines).unwrap_or_else(|e| panic!("{name}: {e}"));
        assert_eq!(plan.listing.text(), listing, "{name}.listing");
        let payload = good_payload(name);
        assert_eq!(plan.listing.digest(), payload.sha256, "{name}.sha256");
        check_staged_layout(&plan.listing, &payload, &plain_name("Alpha.vst3"))
            .unwrap_or_else(|e| panic!("{name}: {e}"));
    }
    let kat = read_fixture(&format!("kat/{KAT_ASSET}"));
    let listing =
        String::from_utf8(read_fixture(&format!("kat/{KAT_ASSET}.listing"))).expect("UTF-8");
    let plan = plan_archive(&kat[..], &LIMITS, 10).expect("the KAT plan");
    assert_eq!(plan.listing.text(), listing, "KAT listing");
    assert_eq!(hex_lower(&plan.listing.digest()), KAT_DIGEST, "KAT digest");
    let payload = Payload {
        files: 10,
        sha256: unhex(KAT_DIGEST),
    };
    check_staged_layout(&plan.listing, &payload, &plain_name("KatPlugin.vst3"))
        .expect("KAT layout");
}

#[test]
fn bad_fixtures_fail_with_expected_code() {
    // (1) The 22 committed bad archives.
    for name in BAD {
        let expected =
            String::from_utf8(read_fixture(&format!("listings/bad/{name}.expected-error")))
                .expect("UTF-8");
        let expected = expected.trim();
        let planned = plan_archive(&bad(name)[..], &LIMITS, 64);
        if let Some(item) = match name {
            "layout-extra-root-file" => Some(("NOTES.txt", 11)),
            "layout-missing-presets" => Some(("presets", 8)),
            _ => None,
        } {
            let plan = planned.unwrap_or_else(|e| panic!("{name}: {e}"));
            assert_eq!(plan.listing.files(), item.1, "{name}: lines");
            let own = Payload {
                files: plan.listing.files(),
                sha256: plan.listing.digest(),
            };
            let e = check_staged_layout(&plan.listing, &own, &plain_name("Alpha.vst3"))
                .expect_err(name);
            assert_eq!(e.code(), expected, "{name}");
            assert_eq!(
                e,
                ExtractError::Layout {
                    item: item.0.to_owned()
                },
                "{name}"
            );
        } else {
            match planned {
                Ok(_) => panic!("{name}: planned"),
                Err(e) => assert_eq!(e.code(), expected, "{name}: {e}"),
            }
        }
    }
    // (2) The rule table of `c4-listing-tar.test.js:196-231`, ported to `ListingEntry` values.
    let rejects: Vec<(&str, Vec<ListingEntry>, &str)> = vec![
        (
            "invalid UTF-8 path",
            vec![f(&[0xc3, 0x28])],
            "E_LISTING_PATH",
        ),
        (
            "invalid UTF-8 target",
            vec![l(b"A/x", &[0xc3, 0x28])],
            "E_LISTING_PATH",
        ),
        ("NUL in a path", vec![f(b"A/a\0b")], "E_LISTING_PATH"),
        ("CR in a path", vec![f(b"A/a\rb")], "E_LISTING_PATH"),
        ("LF in a path", vec![f(b"A/a\nb")], "E_LISTING_PATH"),
        ("\" -> \" in a path", vec![f(b"A/a -> b")], "E_LISTING_PATH"),
        ("empty path", vec![f(b"")], "E_LISTING_PATH"),
        ("empty target", vec![l(b"A/x", b"")], "E_LISTING_PATH"),
        ("absolute path", vec![f(b"/A/x")], "E_LISTING_ABSOLUTE"),
        (
            "absolute target /etc/x",
            vec![l(b"A/x", b"/etc/x")],
            "E_LISTING_ABSOLUTE",
        ),
        (
            "drive target C:x",
            vec![l(b"A/x", b"C:x")],
            "E_LISTING_DRIVE",
        ),
        (
            "backslash target a\\b",
            vec![l(b"A/x", b"a\\b")],
            "E_LISTING_BACKSLASH",
        ),
        ("path a//b", vec![f(b"a//b")], "E_LISTING_DOTDOT"),
        ("path a/./b", vec![f(b"a/./b")], "E_LISTING_DOTDOT"),
        ("path A/../b", vec![f(b"A/../b")], "E_LISTING_DOTDOT"),
        (
            "target a/../b",
            vec![l(b"A/x", b"a/../b")],
            "E_LISTING_SYMLINK_FORM",
        ),
        (
            "target ./a",
            vec![l(b"A/x", b"./a")],
            "E_LISTING_SYMLINK_FORM",
        ),
        (
            "target ..",
            vec![l(b"A/b/x", b"..")],
            "E_LISTING_SYMLINK_FORM",
        ),
        (
            "target a//b",
            vec![l(b"A/x", b"a//b")],
            "E_LISTING_SYMLINK_FORM",
        ),
        (
            "target with a trailing /",
            vec![l(b"A/x", b"b/")],
            "E_LISTING_SYMLINK_FORM",
        ),
        (
            "FIFO (typeflag 6)",
            vec![ListingEntry {
                kind: EntryType::Other(b'6'),
                ..f(b"A/fifo")
            }],
            "E_LISTING_TYPE",
        ),
        (
            "root-level symlink",
            vec![l(b"x", b"A")],
            "E_LISTING_ROOT_SYMLINK",
        ),
        (
            "target above the root",
            vec![l(b"A/x", b"../../y")],
            "E_LISTING_SYMLINK_ESCAPE",
        ),
        (
            "target leaving its top-level entry",
            vec![l(b"A/b/x", b"../../B/y")],
            "E_LISTING_SYMLINK_ESCAPE",
        ),
        (
            "target resolving to its top-level entry itself",
            vec![l(b"A/b/x", b"../../A")],
            "E_LISTING_SYMLINK_ESCAPE",
        ),
        (
            "duplicate path",
            vec![f(b"A/x"), f(b"A/x")],
            "E_LISTING_DUPLICATE",
        ),
        (
            "file that is also an implied directory",
            vec![f(b"A/x"), f(b"A/x/y")],
            "E_LISTING_DUPLICATE",
        ),
        (
            "chain: a link through a link",
            vec![l(b"A/b/c/s", b"../../d"), l(b"A/b/c/t", b"s/x")],
            "E_LISTING_SYMLINK_CHAIN",
        ),
        (
            "chain: a link to a link",
            vec![l(b"A/s", b"d"), l(b"A/t", b"s")],
            "E_LISTING_SYMLINK_CHAIN",
        ),
        (
            "self-link",
            vec![l(b"A/s", b"s")],
            "E_LISTING_SYMLINK_CHAIN",
        ),
        (
            "dotdot chain (PD-12 review case)",
            vec![l(b"A/b/c/s", b"../../d"), l(b"A/b/c/t", b"s/../../../x")],
            "E_LISTING_SYMLINK_FORM",
        ),
        (
            "an entry under a link",
            vec![l(b"A/t", b"b"), f(b"A/t/evil")],
            "E_LISTING_THROUGH_SYMLINK",
        ),
        (
            "a link under a link",
            vec![l(b"A/t", b"b"), l(b"A/t/u", b"../c")],
            "E_LISTING_THROUGH_SYMLINK",
        ),
    ];
    assert_eq!(rejects.len(), 33);
    for (label, entries, code) in &rejects {
        match CanonicalListing::from_entries(entries) {
            Ok(listing) => panic!("{label}: accepted {:?}", listing.lines()),
            Err(e) => assert_eq!(e.code(), *code, "{label}: {e}"),
        }
    }
    // The accept rows of `c4-listing-tar.test.js:236-280`.
    let accepts: Vec<(&str, Vec<ListingEntry>, Vec<&str>)> = vec![
        (
            "in-bundle links",
            vec![
                f(b"Alpha.vst3/Contents/x86_64-linux/Alpha.so"),
                l(b"Alpha.vst3/Contents/x86_64-linux/a", b"Alpha.so"),
                f(b"Alpha.vst3/Contents/Resources/moduleinfo.json"),
                l(
                    b"Alpha.vst3/Contents/x86_64-linux/sub/b",
                    b"../../Resources/moduleinfo.json",
                ),
            ],
            vec![
                "d 0755 Alpha.vst3",
                "d 0755 Alpha.vst3/Contents",
                "d 0755 Alpha.vst3/Contents/Resources",
                "f 0644 Alpha.vst3/Contents/Resources/moduleinfo.json",
                "d 0755 Alpha.vst3/Contents/x86_64-linux",
                "f 0644 Alpha.vst3/Contents/x86_64-linux/Alpha.so",
                "l 0777 Alpha.vst3/Contents/x86_64-linux/a -> Alpha.so",
                "d 0755 Alpha.vst3/Contents/x86_64-linux/sub",
                "l 0777 Alpha.vst3/Contents/x86_64-linux/sub/b -> ../../Resources/moduleinfo.json",
            ],
        ),
        (
            "D3 normalisation",
            vec![
                d(b"./", 0o700),
                d(b"./A/", 0o700),
                fm(b"./A/x", 0o750),
                fm(b"./A/y", 0o640),
                ListingEntry {
                    mode: Some(0o755),
                    ..l(b"./A/l", b"x")
                },
                fm(b"B/c/d.txt", 0o600),
            ],
            vec![
                "d 0755 A",
                "l 0777 A/l -> x",
                "f 0755 A/x",
                "f 0644 A/y",
                "d 0755 B",
                "d 0755 B/c",
                "f 0644 B/c/d.txt",
            ],
        ),
        (
            "UTF-8 byte sort",
            vec![f(b"A/b"), f(b"A-c"), f("A/\u{e9}".as_bytes()), f(b"A/Z")],
            vec![
                "d 0755 A",
                "f 0644 A-c",
                "f 0644 A/Z",
                "f 0644 A/b",
                "f 0644 A/\u{e9}",
            ],
        ),
    ];
    for (label, entries, want) in &accepts {
        let listing =
            CanonicalListing::from_entries(entries).unwrap_or_else(|e| panic!("{label}: {e}"));
        assert_eq!(listing.lines(), want.as_slice(), "{label}");
    }
    // (3) Streaming rows: the same per-entry functions through `plan_archive` (PD-11).
    let streaming: [(&str, Vec<u8>, &str); 3] = [
        (
            "name bytes c3 28",
            base_with(|t| {
                t.record(b"Alpha.vst3/\xc3\x28", b'0', 0o644, b"x", b"");
            }),
            "E_LISTING_PATH",
        ),
        (
            "name with \" -> \"",
            base_with(|t| {
                t.file("Alpha.vst3/a -> b", 0o644, b"x");
            }),
            "E_LISTING_PATH",
        ),
        (
            "typeflag 6",
            base_with(|t| {
                t.record(b"Alpha.vst3/fifo", b'6', 0o644, b"", b"");
            }),
            "E_LISTING_TYPE",
        ),
    ];
    for (label, bytes, code) in &streaming {
        match plan_archive(&bytes[..], &LIMITS, 64) {
            Ok(_) => panic!("{label}: planned"),
            Err(e) => assert_eq!(e.code(), *code, "{label}: {e}"),
        }
    }
}

// ---- SC-004, SC-005 --------------------------------------------------------------------------------------------------

#[test]
fn kat_archive_stages() {
    let rig = Rig::new();
    let art = rig.kat();
    let tree = stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
    assert_eq!(hex_lower(&tree.listing().digest()), KAT_DIGEST);
    assert_eq!(tree.root().parent(), Some(rig.extract().as_path()));
    let files = tar_files(&gunzip(&read_fixture(&format!("kat/{KAT_ASSET}"))));
    for rel in [
        "Contents/Resources/moduleinfo.json",
        "Contents/x86_64-linux/KatPlugin.so",
    ] {
        let staged = fs::read(at(&tree.bundle(), rel)).expect("a staged file");
        assert_eq!(
            Some(&staged),
            files.get(&format!("KatPlugin.vst3/{rel}")),
            "{rel}"
        );
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt as _;
        let mode = |rel: &str| {
            fs::symlink_metadata(at(tree.root(), rel))
                .expect("metadata")
                .permissions()
                .mode()
                & 0o7777
        };
        for rel in [
            "KatPlugin.vst3/Contents/Resources/moduleinfo.json",
            "KatPlugin.vst3/Contents/x86_64-linux/KatPlugin.so",
        ] {
            assert_eq!(mode(rel), 0o644, "{rel}");
        }
        for rel in [
            "KatPlugin.vst3",
            "KatPlugin.vst3/Contents",
            "KatPlugin.vst3/Contents/Resources",
            "KatPlugin.vst3/Contents/x86_64-linux",
        ] {
            assert_eq!(mode(rel), 0o755, "{rel}");
        }
    }
}

#[test]
fn real_shaped_round_trip() {
    let rig = Rig::new();
    let art = rig.signed_good("real-shaped");
    let tree = stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
    assert_eq!(tree.listing().text(), good_listing("real-shaped"));
    assert_eq!(rig.extract_names().len(), 1);
}

#[test]
fn exec_and_symlink_round_trip_logic() {
    let rig = Rig::new();
    let mem = MemExtractIo::new(rig.staging());
    let art = rig.signed_good("exec-and-symlink");
    let tree = stage(rig.env(&mem, &LIMITS), &art.entry, &art.artifact).expect("stages");
    assert_eq!(tree.listing().text(), good_listing("exec-and-symlink"));
    match mem.node(&at(tree.root(), SO)) {
        Some(MemNode::File { mode, .. }) => assert_eq!(mode, 0o755),
        other => panic!("Alpha.so: {other:?}"),
    }
    assert_eq!(
        mem.node(&at(tree.root(), LIBALPHA)),
        Some(MemNode::Link {
            target: "Alpha.so".to_owned()
        })
    );
}

#[test]
fn dot_slash_round_trip() {
    let rig = Rig::new();
    let art = rig.signed_good("dot-slash");
    assert_eq!(good_payload("dot-slash").files, 10);
    let tree = stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
    assert_eq!(tree.listing().text(), good_listing("dot-slash"));
}

// ---- SC-006, SC-007, SC-008, SC-009 ----------------------------------------------------------------------------------

#[test]
fn crafted_archives_are_refused() {
    // The 20 malformed committed archives (the two layout archives are SC-007's).
    for name in BAD.iter().filter(|n| !n.starts_with("layout-")) {
        let expected =
            String::from_utf8(read_fixture(&format!("listings/bad/{name}.expected-error")))
                .expect("UTF-8");
        let code: &str = expected.trim();
        let inside_temp = matches!(
            *name,
            "dotdot-path" | "symlink-escape-root" | "symlink-escape-bundle"
        );
        refused(
            name,
            &bad(name),
            NO_PAYLOAD,
            &LIMITS,
            (code, None),
            inside_temp,
        );
    }
    let small = |f: fn(&mut ExtractLimits)| {
        let mut limits = LIMITS;
        f(&mut limits);
        limits
    };
    // The FR-085 runtime rows.
    type Row = (&'static str, Vec<u8>, Payload, ExtractLimits, Want, bool);
    let rows: Vec<Row> = vec![
        (
            "../ in a pax path",
            base_with(|t| {
                t.pax(b'x', &[("path", &b"../evil"[..])])
                    .file("placeholder", 0o644, b"x");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_DOTDOT", None),
            true,
        ),
        (
            "/etc/evil",
            base_with(|t| {
                t.file("/etc/evil", 0o644, b"x");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_ABSOLUTE", None),
            false,
        ),
        (
            "C:\\evil",
            base_with(|t| {
                t.file("C:\\evil", 0o644, b"x");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_DRIVE", None),
            false,
        ),
        (
            "C:/evil",
            base_with(|t| {
                t.file("C:/evil", 0o644, b"x");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_DRIVE", None),
            false,
        ),
        (
            "\\\\server\\share\\evil",
            base_with(|t| {
                t.file("\\\\server\\share\\evil", 0o644, b"x");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_BACKSLASH", None),
            false,
        ),
        (
            "//server/share/evil",
            base_with(|t| {
                t.file("//server/share/evil", 0o644, b"x");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_ABSOLUTE", None),
            false,
        ),
        (
            "an escaping symlink",
            base_with(|t| {
                t.link("Alpha.vst3/Contents/x", "../../../evil");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_SYMLINK_ESCAPE", None),
            true,
        ),
        (
            "a symlink chain",
            base_with(|t| {
                t.link("Alpha.vst3/Contents/s", "Resources")
                    .link("Alpha.vst3/Contents/t", "s/moduleinfo.json");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_SYMLINK_CHAIN", None),
            true,
        ),
        (
            "a duplicate entry",
            base_with(|t| {
                t.file("README.txt", 0o644, b"again");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_DUPLICATE", None),
            false,
        ),
        (
            "an oversized entry count",
            base_with(|_| {}),
            Payload {
                files: 9,
                sha256: [0; 32],
            },
            LIMITS,
            ("E_EXTRACT_LIMIT", Some("entries")),
            false,
        ),
        (
            "a header flood",
            archive(
                |t| {
                    for _ in 0..20 {
                        t.pax(b'g', &[("comment", &b"flood"[..])]);
                    }
                },
                |_| {},
            ),
            NO_PAYLOAD,
            small(|l| l.max_headers = 16),
            ("E_EXTRACT_LIMIT", Some("headers")),
            false,
        ),
        (
            "a 64 KiB + 1 GNU L record",
            base_with(|t| {
                t.record(b"././@LongLink", b'L', 0o644, &vec![b'm'; 65_537], b"");
                t.file("presets/after.vstpreset", 0o644, b"x");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_EXTRACT_LIMIT", Some("meta")),
            false,
        ),
        (
            "a 4097-byte path in a GNU L record",
            base_with(|t| {
                t.long_file(long_path(4097).as_bytes(), b"x");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_EXTRACT_LIMIT", Some("path")),
            false,
        ),
        (
            "a 256-byte segment",
            base_with(|t| {
                t.long_file(format!("presets/{}", "s".repeat(256)).as_bytes(), b"x");
            }),
            NO_PAYLOAD,
            LIMITS,
            ("E_EXTRACT_LIMIT", Some("segment")),
            false,
        ),
        (
            "a 4000-byte path whose joined length exceeds 4095",
            base_with(|t| {
                t.long_file(long_path(4000).as_bytes(), b"x");
            }),
            literal_payload(&sorted_with(file_lines(&long_path(4000)))),
            LIMITS,
            ("E_EXTRACT_LIMIT", Some("joined")),
            false,
        ),
        (
            "a gzip bomb",
            base_with(|t| {
                t.file("presets/bomb.vstpreset", 0o644, &vec![0u8; 2 << 20]);
            }),
            NO_PAYLOAD,
            small(|l| l.max_tar_bytes = 1 << 20),
            ("E_EXTRACT_LIMIT", Some("tar_bytes")),
            false,
        ),
        (
            "trailing gzip garbage",
            {
                let mut bytes = base_with(|_| {});
                bytes.extend_from_slice(b"garbage!");
                bytes
            },
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_CORRUPT", None),
            false,
        ),
        // SI-5 / PD-1: the first code equals `listing.js`'s on two-defect archives.
        (
            "../x then a bad checksum",
            archive(
                |t| {
                    t.file("../x", 0o644, b"x");
                },
                |t| {
                    t.file("presets/late.vstpreset", 0o644, b"x");
                    let off = t.bytes.len() - 1024;
                    t.bytes[off + 148] ^= 0x01;
                },
            ),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_CHECKSUM", None),
            false,
        ),
        (
            "an escaping link then an absolute path",
            archive(
                |t| {
                    t.link("Alpha.vst3/Contents/x", "../../../etc")
                        .file("/etc/evil", 0o644, b"x");
                },
                |_| {},
            ),
            NO_PAYLOAD,
            LIMITS,
            ("E_LISTING_ABSOLUTE", None),
            false,
        ),
        (
            "an absolute path then payload.files crossed",
            archive(
                |t| {
                    t.file("/etc/evil", 0o644, b"x");
                },
                |_| {},
            ),
            Payload {
                files: 9,
                sha256: [0; 32],
            },
            LIMITS,
            ("E_LISTING_ABSOLUTE", None),
            false,
        ),
    ];
    for (label, bytes, payload, limits, want, inside_temp) in &rows {
        refused(label, bytes, *payload, limits, *want, *inside_temp);
    }
}

#[test]
fn payload_and_layout_mismatches() {
    let real = good_payload("real-shaped");
    let flipped = {
        let mut p = real;
        p.sha256[0] ^= 0x01;
        p
    };
    let rows: [(&str, &str, Payload, Want); 4] = [
        (
            "(a) files + 1",
            "real-shaped",
            Payload { files: 15, ..real },
            ("E_LISTING_COUNT", None),
        ),
        (
            "(a2) implicit-dirs files - 1",
            "implicit-dirs",
            Payload {
                files: 9,
                ..good_payload("implicit-dirs")
            },
            ("E_LISTING_COUNT", None),
        ),
        (
            "(a3) files - 1",
            "real-shaped",
            Payload { files: 13, ..real },
            ("E_EXTRACT_LIMIT", Some("entries")),
        ),
        (
            "(b) digest",
            "real-shaped",
            flipped,
            ("E_LISTING_DIGEST", None),
        ),
    ];
    for (label, name, payload, want) in rows {
        let rig = Rig::new();
        let art = rig.signed(&good(name), V, payload);
        let rec = RecordingExtractIo::fail_closed();
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(code_of(&e), want, "{label}: {e}");
        match (label, &e) {
            ("(a) files + 1", ExtractError::Count { expected, found }) => {
                assert_eq!((*expected, *found), (15, 14), "{label}");
            }
            ("(a2) implicit-dirs files - 1", ExtractError::Count { expected, found }) => {
                assert_eq!((*expected, *found), (9, 10), "{label}");
            }
            ("(b) digest", ExtractError::Digest { expected, found }) => {
                assert_eq!(*expected, hex_lower(&flipped.sha256), "{label}");
                assert_eq!(*found, hex_lower(&real.sha256), "{label}");
            }
            _ => {}
        }
        assert_eq!(rec.mutating_calls(), 0, "{label}");
    }
    // (c) Another bundle name with the matching payload.
    {
        let rig = Rig::new();
        let mut art = rig.signed_good("real-shaped");
        if let Some(install) = art.entry.release.as_mut().and_then(|r| r.install.as_mut()) {
            install.vst3_bundle = plain_name("Bravo.vst3");
        }
        let rec = RecordingExtractIo::fail_closed();
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(e.code(), "E_LAYOUT", "(c): {e}");
        assert_eq!(rec.mutating_calls(), 0, "(c)");
    }
    // (d) The two committed layout archives with the payload of their own listing.
    for (name, item) in [
        ("layout-extra-root-file", "NOTES.txt"),
        ("layout-missing-presets", "presets"),
    ] {
        let rig = Rig::new();
        let plan = plan_archive(&bad(name)[..], &LIMITS, 64).expect(name);
        let own = Payload {
            files: plan.listing.files(),
            sha256: plan.listing.digest(),
        };
        let art = rig.signed(&bad(name), V, own);
        let rec = RecordingExtractIo::fail_closed();
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(
            e,
            ExtractError::Layout {
                item: item.to_owned()
            },
            "(d) {name}"
        );
        assert_eq!(rec.mutating_calls(), 0, "(d) {name}");
    }
}

#[test]
fn entry_binding_cases() {
    let rig = Rig::new();
    let bytes = good("real-shaped");
    let exe = rig.signed_as(
        ALPHA,
        PlatformKey::WindowsX86_64,
        "Alpha-1.2.0-Windows-x64.exe",
        b"an installer",
        V,
    );
    let pkg = rig.signed_as(
        ALPHA,
        PlatformKey::DarwinAarch64,
        "Alpha-1.2.0-macOS.pkg",
        b"a package",
        V,
    );
    let linux = rig.signed(&bytes, V, good_payload("real-shaped"));
    let other_asset = rig.signed_as(
        ALPHA,
        PlatformKey::LinuxX86_64,
        "Other-1.2.0-Linux-x64.tar.gz",
        &bytes,
        V,
    );
    let edited = |f: &dyn Fn(&mut PluginEntry)| {
        let mut entry = linux.entry.clone();
        f(&mut entry);
        entry
    };
    let rows: Vec<(&str, PluginEntry, &VerifiedArtifact, &str)> = vec![
        (".exe artifact", exe.entry.clone(), &exe.artifact, "format"),
        (".pkg artifact", pkg.entry.clone(), &pkg.artifact, "format"),
        (
            "another name",
            edited(&|e| e.name = "Bravo".to_owned()),
            &linux.artifact,
            "name",
        ),
        (
            "another version",
            edited(&|e| e.version_text = "1.2.1".to_owned()),
            &linux.artifact,
            "version",
        ),
        (
            "a v1-only entry",
            edited(&|e| e.release = None),
            &linux.artifact,
            "release",
        ),
        (
            "no platform for the key",
            edited(&|e| {
                if let Some(r) = e.release.as_mut() {
                    r.platforms.remove(&PlatformKey::LinuxX86_64);
                }
            }),
            &linux.artifact,
            "platform",
        ),
        (
            "another asset name",
            edited(&|e| {
                platform_mut(e).asset_name = other_asset.artifact.asset().clone();
            }),
            &linux.artifact,
            "asset",
        ),
        (
            "another size",
            edited(&|e| platform_mut(e).size += 1),
            &linux.artifact,
            "size",
        ),
        (
            "another SHA-256",
            edited(&|e| platform_mut(e).sha256[0] ^= 0x01),
            &linux.artifact,
            "sha256",
        ),
        (
            "another asset format",
            edited(&|e| platform_mut(e).format = AssetFormat::InnoExe),
            &linux.artifact,
            "format",
        ),
        (
            "no payload",
            edited(&|e| set_payload(e, None)),
            &linux.artifact,
            "payload",
        ),
        (
            "no install",
            edited(&|e| {
                if let Some(r) = e.release.as_mut() {
                    r.install = None;
                }
            }),
            &linux.artifact,
            "install",
        ),
    ];
    for (label, entry, artifact, field) in rows {
        let rec = RecordingExtractIo::fail_closed();
        let e = err_of(stage(rig.env(&rec, &LIMITS), &entry, artifact));
        assert_eq!(e, ExtractError::EntryMismatch { field }, "{label}");
        assert!(rec.calls().is_empty(), "{label}: {:?}", rec.calls());
    }
}

#[test]
fn changed_archive_is_refused() {
    let original = good("real-shaped");
    let tar = gunzip(&original);
    let headers = tar_headers(&tar);
    let readme = headers
        .iter()
        .find(|(_, p, _, _)| p == "README.txt")
        .cloned()
        .expect("README.txt");
    // (a) one data byte of README.txt flipped, re-gzipped.
    let data_flipped = {
        let mut t = tar.clone();
        t[readme.0 + 512] ^= 0x01;
        gz(&t)
    };
    // (b) the first header's first name byte flipped.
    let name_flipped = {
        let mut t = tar.clone();
        t[0] ^= 0x01;
        gz(&t)
    };
    // (c) the first MTIME byte of the gzip header flipped.
    let mtime_flipped = {
        let mut b = original.clone();
        b[4] ^= 0x01;
        b
    };
    let rows: [(&str, &Vec<u8>, &str); 3] = [
        ("(a)", &data_flipped, "E_EXTRACT_CHANGED"),
        ("(b)", &name_flipped, "E_LISTING_CHECKSUM"),
        ("(c)", &mtime_flipped, "E_EXTRACT_CHANGED"),
    ];
    for (label, replaced, code) in rows {
        let rig = Rig::new();
        let art = rig.signed_good("real-shaped");
        fs::write(art.artifact.path(), replaced).expect("rewrite after verification");
        let rec = RecordingExtractIo::fail_closed();
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(e.code(), code, "{label}: {e}");
        assert_eq!(rec.mutating_calls(), 0, "{label}");
    }
    // (e1) one more well-formed file inserted before the end-of-archive block.
    let inserted = {
        let end = end_block(&tar);
        let mut t = tar[..end].to_vec();
        let mut extra = Tar::new();
        extra.file("presets/Extra.vstpreset", 0o644, b"extra");
        t.extend_from_slice(&extra.bytes);
        t.extend_from_slice(&tar[end..]);
        gz(&t)
    };
    // (e2) README.txt's size one larger, checksum recomputed.
    let grown = {
        let mut t = tar.clone();
        octal_field(
            &mut t[readme.0 + 124..readme.0 + 136],
            (readme.3 + 1) as u64,
        );
        reseal(&mut t, readme.0);
        gz(&t)
    };
    // (f) entry 5's header checksum field flipped.
    let fifth = headers[4].clone();
    let bad_fifth = {
        let mut t = tar.clone();
        t[fifth.0 + 148] ^= 0x01;
        gz(&t)
    };
    let rows: [(&str, &Vec<u8>); 4] = [
        ("(d)", &data_flipped),
        ("(e1)", &inserted),
        ("(e2)", &grown),
        ("(f)", &bad_fifth),
    ];
    let plan = plan_archive(&original[..], &LIMITS, 64).expect("the plan");
    for (label, after) in rows {
        let rig = Rig::new();
        let art = rig.signed_good("real-shaped");
        let (first, after) = (original.clone(), after.clone());
        let rec = RecordingExtractIo::forwarding(&FsExtractIo).with_open_archive(Box::new(
            move |_: &Path| {
                let archive: Box<dyn ArchiveRead> =
                    Box::new(TwoPhaseArchive::new(first.clone(), after.clone()));
                Ok(archive)
            },
        ));
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(e, ExtractError::Changed, "{label}");
        let calls = rec.calls();
        let created = created_files(&calls);
        match label {
            "(e1)" => {
                let extra = Path::new("presets").join("Extra.vstpreset");
                assert!(!created.iter().any(|p| p.ends_with(&extra)), "{label}");
                assert!(write_records(&calls, &extra).is_empty(), "{label}");
            }
            "(e2)" => {
                assert!(
                    !created.iter().any(|p| p.ends_with("README.txt")),
                    "{label}"
                );
                for e in &plan.entries {
                    if let PlanKind::File { size, .. } = e.kind {
                        let written: usize = created
                            .iter()
                            .filter(|p| p.ends_with(Path::new(&e.path)))
                            .map(|p| write_records(&calls, p).len())
                            .sum();
                        assert!(written as u64 <= size, "{label}: {}", e.path);
                    }
                }
            }
            "(f)" => {
                for (_, path, _, _) in &headers[4..] {
                    assert!(
                        !created.iter().any(|p| p.ends_with(Path::new(path))),
                        "{label}: {path}"
                    );
                }
            }
            _ => {}
        }
        assert!(
            rig.extract_names().is_empty(),
            "{label}: the work dir is gone"
        );
    }
}

// ---- SC-010 -----------------------------------------------------------------------------------------------------------

#[test]
fn io_failures_leave_no_tree() {
    use io::ErrorKind::{PermissionDenied, StorageFull, TimedOut, Unsupported};
    // The mapped error, the work dir gone.
    {
        let rig = Rig::new();
        let art = rig.signed_good("real-shaped");
        let faulty = FaultyExtractIo::new(&FsExtractIo).with_failure(XKind::Write, 1, StorageFull);
        let e = err_of(stage(rig.env(&faulty, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(e.code(), "E_EXTRACT_FULL", "{e}");
        assert!(matches!(e, ExtractError::Full { step: "write", .. }), "{e}");
        assert!(rig.extract_names().is_empty(), "write");
        let faulty =
            FaultyExtractIo::new(&FsExtractIo).with_failure(XKind::CreateDir, 1, PermissionDenied);
        let e = err_of(stage(rig.env(&faulty, &LIMITS), &art.entry, &art.artifact));
        assert!(
            matches!(
                e,
                ExtractError::Denied {
                    step: "create_dir",
                    ..
                }
            ),
            "{e}"
        );
        assert!(rig.extract_names().is_empty(), "create_dir");
    }
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        let art = rig.signed_good("exec-and-symlink");
        let faulty = FaultyExtractIo::new(&mem).with_failure(XKind::Symlink, 1, Unsupported);
        let e = err_of(stage(rig.env(&faulty, &LIMITS), &art.entry, &art.artifact));
        assert!(
            matches!(
                e,
                ExtractError::Io {
                    step: "symlink",
                    kind: Unsupported,
                    ..
                }
            ),
            "{e}"
        );
        assert!(rig.mem_children(&mem).is_empty(), "symlink");
    }
    // (i) Cleanup that never runs, each write step, over Mem (every OS): the partial tree is left and the next stage
    // prunes it.
    for (kind, nth, step) in [
        (XKind::CreateDir, 2, "create_dir"),
        (XKind::CreateFile, 2, "create_file"),
        (XKind::Write, 2, "write"),
        (XKind::SetMode, 3, "set_mode"),
        (XKind::Symlink, 1, "symlink"),
    ] {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        let art = rig.signed_good("exec-and-symlink");
        let faulty = FaultyExtractIo::new(&mem)
            .with_failure(kind, nth, StorageFull)
            .with_remove_tree(RemoveTreeMode::Noop);
        let e = err_of(stage(rig.env(&faulty, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(
            e,
            ExtractError::Full {
                step,
                entry: e_entry(&e)
            },
            "(i) {step}"
        );
        let left = rig.mem_children(&mem);
        assert_eq!(left.len(), 1, "(i) {step}: the leftover");
        assert!(
            !mem.paths_under(&left[0]).is_empty(),
            "(i) {step}: partial content"
        );
        let tree = stage(rig.env(&mem, &LIMITS), &art.entry, &art.artifact).expect("restages");
        assert_eq!(
            rig.mem_children(&mem),
            vec![tree.root().to_path_buf()],
            "(i) {step}"
        );
    }
    // (i) over the real filesystem for the steps every OS performs.
    for (kind, step) in [
        (XKind::CreateDir, "create_dir"),
        (XKind::CreateFile, "create_file"),
        (XKind::Write, "write"),
    ] {
        let rig = Rig::new();
        let art = rig.signed_good("real-shaped");
        let faulty = FaultyExtractIo::new(&FsExtractIo)
            .with_failure(kind, 2, StorageFull)
            .with_remove_tree(RemoveTreeMode::Noop);
        let e = err_of(stage(rig.env(&faulty, &LIMITS), &art.entry, &art.artifact));
        assert!(
            matches!(&e, ExtractError::Full { step: s, .. } if *s == step),
            "{e}"
        );
        assert_eq!(rig.extract_names().len(), 1, "(i) Fs {step}: the leftover");
        let tree =
            stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("restages");
        let name = tree
            .root()
            .file_name()
            .expect("a name")
            .to_string_lossy()
            .into_owned();
        assert_eq!(rig.extract_names(), vec![name], "(i) Fs {step}");
    }
    // (ii) A planted leftover with a partial bundle is pruned.
    {
        let rig = Rig::new();
        let leftover = rig.extract().join(format!("{}.abcdef", "a".repeat(64)));
        fs::create_dir_all(leftover.join("Alpha.vst3").join("Contents")).expect("plant");
        let art = rig.signed_good("real-shaped");
        let tree =
            stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
        let name = tree
            .root()
            .file_name()
            .expect("a name")
            .to_string_lossy()
            .into_owned();
        assert_eq!(rig.extract_names(), vec![name], "(ii)");
    }
    // (iii) A failing cleanup: the original error wins, one warning, the leftover pruned next time.
    {
        install_capture();
        let _ = take_mine();
        let rig = Rig::new();
        let art = rig.signed_good("real-shaped");
        let faulty = FaultyExtractIo::new(&FsExtractIo)
            .with_failure(XKind::Write, 1, StorageFull)
            .with_remove_tree(RemoveTreeMode::Fail(PermissionDenied));
        let e = err_of(stage(rig.env(&faulty, &LIMITS), &art.entry, &art.artifact));
        assert!(
            matches!(e, ExtractError::Full { step: "write", .. }),
            "(iii) {e}"
        );
        let warns: Vec<String> = take_mine()
            .into_iter()
            .filter(|(target, level, _)| target == LOG_TARGET && *level == Level::Warn)
            .map(|(_, _, message)| message)
            .collect();
        assert_eq!(warns.len(), 1, "(iii) {warns:?}");
        assert!(warns[0].contains("remove") && warns[0].contains("PermissionDenied"));
        for path in [rig.td.path(), rig.base.as_path()] {
            assert!(
                !warns[0].contains(&*path.to_string_lossy()),
                "(iii) {}",
                warns[0]
            );
        }
        assert_eq!(
            rig.extract_names().len(),
            1,
            "(iii) the work dir is still present"
        );
        let tree =
            stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("restages");
        let name = tree
            .root()
            .file_name()
            .expect("a name")
            .to_string_lossy()
            .into_owned();
        assert_eq!(rig.extract_names(), vec![name], "(iii) pruned");
    }
    // (iv) Archive read failures below the decoder (PD-4). `after` is 256: the archive is 554 bytes.
    for (pass, kind, code) in [
        (1, PermissionDenied, "E_EXTRACT_DENIED"),
        (1, TimedOut, "E_EXTRACT_IO"),
        (2, PermissionDenied, "E_EXTRACT_DENIED"),
    ] {
        let rig = Rig::new();
        let art = rig.signed_good("real-shaped");
        let faulty = FaultyExtractIo::new(&FsExtractIo).with_read_fault(ReadFault {
            pass,
            after: 256,
            kind,
        });
        let e = err_of(stage(rig.env(&faulty, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(e.code(), code, "(iv) pass {pass} {kind:?}: {e}");
        match &e {
            ExtractError::Denied { step, .. } => assert_eq!(*step, "read_archive"),
            ExtractError::Io { step, kind: k, .. } => {
                assert_eq!((*step, *k), ("read_archive", kind));
            }
            other => panic!("(iv): {other}"),
        }
        if pass == 1 {
            assert_eq!(faulty.mutating_calls(), 0, "(iv) pass 1");
        }
        assert!(rig.extract_names().is_empty(), "(iv) pass {pass}");
    }
}

/// The `entry` of a `Full` error (the file a step failed on).
fn e_entry(e: &ExtractError) -> String {
    match e {
        ExtractError::Full { entry, .. } => entry.clone(),
        other => panic!("not E_EXTRACT_FULL: {other}"),
    }
}

// ---- SC-013, SC-014, SC-015 ------------------------------------------------------------------------------------------

/// The base tree with a pax `SCHILY.xattr.com.apple.quarantine` record on `README.txt`.
fn quarantined() -> Vec<u8> {
    let mut t = Tar::new();
    t.dir("Alpha.vst3")
        .dir("Alpha.vst3/Contents")
        .dir("Alpha.vst3/Contents/Resources")
        .file(
            "Alpha.vst3/Contents/Resources/moduleinfo.json",
            0o644,
            &moduleinfo(V),
        )
        .dir("Alpha.vst3/Contents/x86_64-linux")
        .file(SO, 0o644, b"alpha shared object")
        .dir("presets")
        .file("presets/Init.vstpreset", 0o644, b"init preset")
        .pax(
            b'x',
            &[(
                "SCHILY.xattr.com.apple.quarantine",
                &b"0081;00000000;Safari;"[..],
            )],
        )
        .file("README.txt", 0o644, b"read me")
        .file("CHANGELOG.txt", 0o644, b"changes");
    gz(&t.finish())
}

#[test]
fn pax_xattr_records_are_ignored() {
    let bytes = quarantined();
    let plan = plan_archive(&bytes[..], &LIMITS, 64).expect("plans");
    assert_eq!(plan.listing.lines(), BASE_LINES.as_slice());
    let rig = Rig::new();
    let mem = MemExtractIo::new(rig.staging());
    let rec = RecordingExtractIo::forwarding(&mem);
    let art = rig.signed(&bytes, V, literal_payload(&BASE_LINES));
    let tree = stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact).expect("stages");
    // The record has no attribute operation (the trait has none): only the fifteen kinds, and README.txt holds its
    // archive bytes and nothing else.
    assert!(
        rec.calls()
            .iter()
            .all(|op| op.path().is_some() || op.kind() == XKind::Rewind)
    );
    assert_eq!(
        mem.node(&at(tree.root(), "README.txt")),
        Some(MemNode::File {
            bytes: b"read me".to_vec(),
            mode: 0o644
        })
    );
}

#[test]
fn case_fold_collision() {
    let lines = lines_with(&["f 0644 presets/A.vstpreset", "f 0644 presets/a.vstpreset"]);
    let bytes = base_with(|t| {
        t.file("presets/a.vstpreset", 0o644, b"lower")
            .file("presets/A.vstpreset", 0o644, b"upper");
    });
    let rig = Rig::new();
    // The probe, in its own directory of the TempDir.
    let probe = rig.base.join("probe");
    fs::create_dir_all(&probe).expect("probe dir");
    fs::write(probe.join("x"), b"x").expect("probe x");
    let insensitive = fs::symlink_metadata(probe.join("X")).is_ok();
    let art = rig.signed(&bytes, V, literal_payload(&lines));
    let result = stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact);
    if cfg!(target_os = "linux") {
        assert!(
            !insensitive,
            "temp volume expected case-sensitive on this target"
        );
        let tree = result.expect("both files stage");
        assert_eq!(
            fs::read(at(tree.root(), "presets/a.vstpreset")).expect("a"),
            b"lower"
        );
        assert_eq!(
            fs::read(at(tree.root(), "presets/A.vstpreset")).expect("A"),
            b"upper"
        );
    } else {
        assert!(
            insensitive,
            "temp volume expected case-insensitive on this target"
        );
        assert_eq!(err_of(result).code(), "E_EXTRACT_COLLISION");
        assert!(rig.extract_names().is_empty());
    }
}

#[test]
fn discard_and_prune() {
    let rig = Rig::new();
    let art = rig.signed_good("real-shaped");
    let env = rig.env(&FsExtractIo, &LIMITS);
    let first = stage(env, &art.entry, &art.artifact).expect("first");
    let second = stage(env, &art.entry, &art.artifact).expect("second");
    assert_ne!(first.root(), second.root());
    // SC-025: a copy out of `bundle()` with plain reads.
    let copied = fs::read(
        second
            .bundle()
            .join("Contents")
            .join("Resources")
            .join("moduleinfo.json"),
    )
    .expect("read out of the bundle");
    assert_eq!(copied, moduleinfo_of_real_shaped());
    let first_root = first.root().to_path_buf();
    discard(first, &FsExtractIo).expect("discard");
    assert!(
        fs::symlink_metadata(&first_root).is_err(),
        "the first root is gone"
    );
    assert!(second.root().is_dir());
    let third = stage(env, &art.entry, &art.artifact).expect("third");
    assert!(
        second.root().is_dir(),
        "the live second tree survives the third stage"
    );
    let second_root = second.root().to_path_buf();
    drop(second);
    let fourth = stage(env, &art.entry, &art.artifact).expect("fourth");
    assert!(
        fs::symlink_metadata(&second_root).is_err(),
        "the dropped tree is pruned"
    );
    // Decoys: (i) a sibling outside extract/, (ii) extract/ itself, (iii) a grandchild of extract/.
    let victim = rig.base.join("plugins").join("vst3-user").join("victim");
    fs::create_dir_all(&victim).expect("victim");
    fs::write(victim.join("canary.txt"), b"canary").expect("canary");
    for (label, decoy) in [
        ("(i)", victim.clone()),
        ("(ii)", rig.extract()),
        ("(iii)", fourth.root().join("Alpha.vst3")),
    ] {
        let mut tree = stage(env, &art.entry, &art.artifact).expect("a decoy tree");
        tree.root = decoy;
        let before = snapshot(rig.td.path());
        let e = discard(tree, &FsExtractIo).expect_err(label);
        assert_eq!(e.code(), "E_EXTRACT_PATH", "{label}");
        assert_eq!(snapshot(rig.td.path()), before, "{label}");
        assert_eq!(
            fs::read(victim.join("canary.txt")).expect("canary"),
            b"canary"
        );
        assert!(
            fourth.root().join("Alpha.vst3").is_dir(),
            "{label}: the live tree"
        );
        assert!(third.root().is_dir(), "{label}");
    }
}

fn moduleinfo_of_real_shaped() -> Vec<u8> {
    tar_files(&gunzip(&good("real-shaped")))
        .remove("Alpha.vst3/Contents/Resources/moduleinfo.json")
        .expect("moduleinfo.json")
}

// ---- SC-019, SC-020, SC-021 ------------------------------------------------------------------------------------------

#[test]
fn tree_mismatch_is_refused() {
    type TamperFn = fn(&mut Vec<ListingEntry>);
    let rows: [(&str, TamperFn, &str); 4] = [
        (
            "(a) an extra file",
            |entries| entries.push(f(b"presets/extra.vstpreset")),
            "f 0644 presets/extra.vstpreset",
        ),
        (
            "(b) README.txt missing",
            |entries| entries.retain(|e| e.path != b"README.txt"),
            "f 0644 README.txt",
        ),
        (
            "(c) Alpha.so 0644",
            |entries| {
                for e in entries.iter_mut().filter(|e| e.path == SO.as_bytes()) {
                    e.mode = Some(0o644);
                }
            },
            "f 0755 Alpha.vst3/Contents/x86_64-linux/Alpha.so",
        ),
        (
            "(d) libalpha.so -> Other.so",
            |entries| {
                for e in entries.iter_mut().filter(|e| e.path == LIBALPHA.as_bytes()) {
                    e.target = Some(b"Other.so".to_vec());
                }
            },
            "l 0777 Alpha.vst3/Contents/x86_64-linux/libalpha.so -> Alpha.so",
        ),
    ];
    let nfc = "presets/Caf\u{e9}.vstpreset";
    let nfd = "presets/Cafe\u{301}.vstpreset";
    let nfc_line = format!("f 0644 {nfc}");
    let mut cases: Vec<(String, Vec<u8>, Payload, Tamper, String)> = rows
        .iter()
        .map(|(label, tamper, line)| {
            let tamper = *tamper;
            (
                (*label).to_owned(),
                good("exec-and-symlink"),
                good_payload("exec-and-symlink"),
                Box::new(move |entries: &mut Vec<ListingEntry>| tamper(entries)) as Tamper,
                (*line).to_owned(),
            )
        })
        .collect();
    cases.push((
        "NFC read back as NFD".to_owned(),
        base_with(|t| {
            t.file(nfc, 0o644, b"cafe");
        }),
        literal_payload(&lines_with(&[nfc_line.as_str()])),
        Box::new(move |entries: &mut Vec<ListingEntry>| {
            for e in entries.iter_mut().filter(|e| e.path == nfc.as_bytes()) {
                e.path = nfd.as_bytes().to_vec();
            }
        }),
        nfc_line.clone(),
    ));
    for (label, bytes, payload, tamper, line) in cases {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        let rec = RecordingExtractIo::forwarding(&mem).with_read_tree(Box::new(
            move |_: &Path, entries: &mut Vec<ListingEntry>| tamper(entries),
        ));
        let art = rig.signed(&bytes, V, payload);
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(e, ExtractError::TreeMismatch { line }, "{label}");
        let removed = removed_trees(&rec.calls());
        assert!(
            removed
                .iter()
                .any(|p| p.parent() == Some(rig.extract().as_path())),
            "{label}: remove_tree of the work dir"
        );
        assert!(rig.mem_children(&mem).is_empty(), "{label}");
    }
}

#[test]
fn entry_path_rejects_every_bad_form() {
    let (_td, roots) = fresh_temp_roots();
    let work = roots.staging_dir().join(EXTRACT_DIR).join("w");
    for bad in [
        "", "/abs", "a//b", "a/./b", "a/../b", "..", ".", "a\\b", "C:x", "a:b", "a\0b", "a/", "C:",
    ] {
        match entry_path(&work, bad) {
            Err(ExtractError::Path { .. }) => {}
            other => panic!("{bad:?}: {other:?}"),
        }
    }
    let joined = entry_path(&work, SO).expect("the good input");
    let mut want = work.clone();
    for s in ["Alpha.vst3", "Contents", "x86_64-linux", "Alpha.so"] {
        want.push(s);
    }
    assert_eq!(joined, want);
    assert!(joined != work && joined.starts_with(&work));
    let device = [
        "aux.txt",
        "Alpha.vst3/CON",
        "presets/com1.json",
        "presets/Nul",
        "presets/x.",
        "presets/x ",
    ];
    let lines = lines_with(&["f 0644 presets/aux.vstpreset"]);
    let bytes = base_with(|t| {
        t.file("presets/aux.vstpreset", 0o644, b"aux");
    });
    #[cfg(windows)]
    {
        for rel in device {
            match entry_path(&work, rel) {
                Err(ExtractError::Path { .. }) => {}
                other => panic!("{rel:?}: {other:?}"),
            }
        }
        let rig = Rig::new();
        let art = rig.signed(&bytes, V, literal_payload(&lines));
        let rec = RecordingExtractIo::fail_closed();
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(e.code(), "E_EXTRACT_PATH", "{e}");
        assert_eq!(rec.mutating_calls(), 0);
    }
    #[cfg(not(windows))]
    {
        for rel in device {
            let mut want = work.clone();
            for s in rel.split('/') {
                want.push(s);
            }
            assert_eq!(entry_path(&work, rel).expect(rel), want, "{rel:?}");
        }
        let rig = Rig::new();
        let art = rig.signed(&bytes, V, literal_payload(&lines));
        let rec = RecordingExtractIo::forwarding(&FsExtractIo);
        stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact).expect("stages");
    }
}

#[test]
fn work_dir_must_be_inside_staging() {
    // (a) kind_of(work dir) reports a file; (b) canonicalize(work dir) returns a sibling of staging/.
    for label in ["(a)", "(b)"] {
        let rig = Rig::new();
        let art = rig.signed_good("real-shaped");
        let extract = rig.extract();
        let sibling = rig.base.join("plugins").join("vst3-user");
        let rec = if label == "(a)" {
            RecordingExtractIo::forwarding(&FsExtractIo).with_kind_of(Box::new(
                move |path: &Path, result: io::Result<Option<NodeKind>>| {
                    if path.parent() == Some(extract.as_path()) {
                        Ok(Some(NodeKind::File))
                    } else {
                        result
                    }
                },
            ))
        } else {
            RecordingExtractIo::forwarding(&FsExtractIo).with_canonicalize(Box::new(
                move |path: &Path, result: io::Result<PathBuf>| {
                    if path.parent() == Some(extract.as_path()) {
                        Ok(sibling.clone())
                    } else {
                        result
                    }
                },
            ))
        };
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(
            e,
            ExtractError::Escape {
                path: "<work dir>".to_owned()
            },
            "{label}"
        );
        let calls = rec.calls();
        let made = calls
            .iter()
            .position(|op| op.kind() == XKind::CreateWorkDir)
            .expect("create_work_dir");
        assert!(
            !calls[made..]
                .iter()
                .any(|op| matches!(op.kind(), XKind::CreateDir | XKind::CreateFile)),
            "{label}"
        );
        assert!(
            rig.extract_names().is_empty(),
            "{label}: the work dir removed"
        );
    }
    // SI-9 (Mem, logic evidence): a linked extract/ → create_work_dir refuses, nothing created in the target.
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        let target = rig.staging().join("t");
        mem.plant_dir(&target, 0o755);
        mem.plant_link(&rig.extract(), "t");
        let art = rig.signed_good("real-shaped");
        let e = err_of(stage(rig.env(&mem, &LIMITS), &art.entry, &art.artifact));
        assert!(
            matches!(
                e,
                ExtractError::Io {
                    step: "create_work_dir",
                    kind: io::ErrorKind::InvalidInput,
                    ..
                }
            ),
            "{e}"
        );
        assert!(mem.paths_under(&target).is_empty());
    }
    // FR-046 (e): a dangling in-bundle link stages over Mem.
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        let missing = "Alpha.vst3/Contents/x86_64-linux/missing.so";
        let lines =
            lines_with(&["l 0777 Alpha.vst3/Contents/x86_64-linux/missing.so -> nothere.so"]);
        let bytes = base_with(|t| {
            t.link(missing, "nothere.so");
        });
        let art = rig.signed(&bytes, V, literal_payload(&lines));
        let tree = stage(rig.env(&mem, &LIMITS), &art.entry, &art.artifact).expect("stages");
        assert_eq!(
            mem.node(&at(tree.root(), missing)),
            Some(MemNode::Link {
                target: "nothere.so".to_owned()
            })
        );
    }
    // FR-046 (f): the link itself canonicalised inside the work dir but outside its top-level entry.
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        let rec = RecordingExtractIo::forwarding(&mem).with_canonicalize(Box::new(
            |path: &Path, result: io::Result<PathBuf>| {
                if path.ends_with(Path::new(LIBALPHA)) {
                    let work = path.ancestors().nth(4).expect("the work dir");
                    Ok(work.join("presets").join("x"))
                } else {
                    result
                }
            },
        ));
        let art = rig.signed_good("exec-and-symlink");
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(
            e,
            ExtractError::Escape {
                path: LIBALPHA.to_owned()
            }
        );
        assert!(rig.mem_children(&mem).is_empty());
    }
}

#[test]
fn stage_logs_one_line_per_outcome() {
    install_capture();
    let _ = take_mine();
    let rig = Rig::new();
    let good_art = rig.signed_good("real-shaped");
    stage(
        rig.env(&FsExtractIo, &LIMITS),
        &good_art.entry,
        &good_art.artifact,
    )
    .expect("stages");
    let staged = take_mine();
    let bad_art = rig.signed(&bad("absolute-path"), V, NO_PAYLOAD);
    let rec = RecordingExtractIo::fail_closed();
    let e = err_of(stage(
        rig.env(&rec, &LIMITS),
        &bad_art.entry,
        &bad_art.artifact,
    ));
    assert_eq!(e.code(), "E_LISTING_ABSOLUTE");
    let refused = take_mine();
    for (label, records, art, needle) in [
        ("staged", &staged, &good_art, "14"),
        ("refused", &refused, &bad_art, "E_LISTING_ABSOLUTE"),
    ] {
        let mine: Vec<&(String, Level, String)> =
            records.iter().filter(|(t, _, _)| t == LOG_TARGET).collect();
        assert_eq!(mine.len(), 1, "{label}: {records:?}");
        let (_, level, message) = mine[0];
        assert_eq!(*level, Level::Info, "{label}");
        for part in [ALPHA, V, art.artifact.asset().as_str(), needle] {
            assert!(message.contains(part), "{label}: {message} lacks {part}");
        }
        for (_, _, message) in records {
            for path in [rig.td.path(), rig.base.as_path(), rig.staging()] {
                assert!(
                    !message.contains(&*path.to_string_lossy()),
                    "{label}: {message}"
                );
            }
        }
    }
}

// ---- SC-024 -----------------------------------------------------------------------------------------------------------

/// A wrapper that holds the first `create_file` until the test releases it (a `Barrier` pair, local to the test).
struct Gate<'a> {
    inner: &'a (dyn ExtractIo + Sync),
    armed: AtomicBool,
    arrived: Barrier,
    release: Barrier,
}

impl<'a> Gate<'a> {
    fn new(inner: &'a (dyn ExtractIo + Sync)) -> Gate<'a> {
        Gate {
            inner,
            armed: AtomicBool::new(true),
            arrived: Barrier::new(2),
            release: Barrier::new(2),
        }
    }
}

impl ExtractIo for Gate<'_> {
    fn open_archive(&self, path: &Path) -> io::Result<Box<dyn ArchiveRead>> {
        self.inner.open_archive(path)
    }
    fn list_dir(&self, dir: &Path) -> io::Result<Option<Vec<(OsString, NodeKind)>>> {
        self.inner.list_dir(dir)
    }
    fn create_work_dir(&self, extract: &Path, prefix: &str) -> io::Result<PathBuf> {
        self.inner.create_work_dir(extract, prefix)
    }
    fn create_dir(&self, path: &Path) -> io::Result<()> {
        self.inner.create_dir(path)
    }
    fn create_file(&self, path: &Path) -> io::Result<Box<dyn ExtractFile>> {
        if self.armed.swap(false, Ordering::SeqCst) {
            self.arrived.wait();
            self.release.wait();
        }
        self.inner.create_file(path)
    }
    fn set_mode(&self, path: &Path, mode: u32) -> io::Result<()> {
        self.inner.set_mode(path, mode)
    }
    fn symlink(&self, target: &str, link: &Path) -> io::Result<()> {
        self.inner.symlink(target, link)
    }
    fn supports_symlinks(&self) -> bool {
        self.inner.supports_symlinks()
    }
    fn remove_tree(&self, path: &Path) -> io::Result<()> {
        self.inner.remove_tree(path)
    }
    fn remove_entry(&self, path: &Path, kind: NodeKind) -> io::Result<()> {
        self.inner.remove_entry(path, kind)
    }
    fn kind_of(&self, path: &Path) -> io::Result<Option<NodeKind>> {
        self.inner.kind_of(path)
    }
    fn read_tree(&self, root: &Path) -> io::Result<Vec<ListingEntry>> {
        self.inner.read_tree(root)
    }
    fn canonicalize(&self, path: &Path) -> io::Result<PathBuf> {
        self.inner.canonicalize(path)
    }
    fn read_file(&self, path: &Path, max: u64) -> io::Result<Vec<u8>> {
        self.inner.read_file(path, max)
    }
}

/// One SC-024 variant over `io`: `children` lists `extract/`'s direct children, `is_dir` reads a node's kind.
fn concurrent_variant(
    label: &str,
    rig: &Rig,
    io: &(dyn ExtractIo + Sync),
    b_name: &str,
    children: &dyn Fn() -> Vec<PathBuf>,
    is_dir: &dyn Fn(&Path) -> bool,
) {
    let a_art = rig.signed_good("real-shaped");
    let b_art = rig.signed_good(b_name);
    let gate = Gate::new(io);
    thread::scope(|s| {
        let a = s.spawn(|| {
            let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                stage(rig.env(&gate, &LIMITS), &a_art.entry, &a_art.artifact)
            }));
            // A ended before its first create_file (a regression): trip the gate itself so the test fails, never
            // hangs.
            if gate.armed.swap(false, Ordering::SeqCst) {
                gate.arrived.wait();
                gate.release.wait();
            }
            result
        });
        gate.arrived.wait();
        // While A is held: observe only, so the release below is always reached.
        let b = stage(rig.env(io, &LIMITS), &b_art.entry, &b_art.artifact);
        let held: Vec<PathBuf> = children()
            .into_iter()
            .filter(|p| b.as_ref().ok().is_none_or(|b| p != b.root()))
            .collect();
        let held_dirs = held
            .iter()
            .all(|h| is_dir(&h.join("Alpha.vst3").join("Contents")));
        let pruned_while_held = prune(rig.env(io, &LIMITS));
        gate.release.wait();
        let a_tree = match a.join().expect("A joins") {
            Ok(result) => result.unwrap_or_else(|e| panic!("{label}: A: {e}")),
            Err(panic) => std::panic::resume_unwind(panic),
        };
        let b = b.unwrap_or_else(|e| panic!("{label}: B: {e}"));
        assert_eq!(held.len(), 1, "{label}: A's work dir is present: {held:?}");
        assert!(held_dirs, "{label}: A's dirs");
        assert_eq!(pruned_while_held, 0, "{label}: nothing live removed");
        assert_eq!(a_tree.root(), held[0], "{label}");
        assert_ne!(a_tree.root(), b.root(), "{label}");
        // Eight at once, one artifact.
        let trees: Vec<StagedTree> = thread::scope(|s8| {
            let handles: Vec<_> = (0..8)
                .map(|_| s8.spawn(|| stage(rig.env(io, &LIMITS), &a_art.entry, &a_art.artifact)))
                .collect();
            handles
                .into_iter()
                .map(|h| {
                    h.join()
                        .expect("joins")
                        .unwrap_or_else(|e| panic!("{label}: {e}"))
                })
                .collect()
        });
        let roots: BTreeSet<&Path> = trees.iter().map(StagedTree::root).collect();
        assert_eq!(roots.len(), 8, "{label}: distinct roots");
        assert!(roots.iter().all(|r| is_dir(r)), "{label}: all present");
        // The failing-A twin: deregistered, then pruned (its cleanup was a no-op).
        let failing = FaultyExtractIo::new(io)
            .with_failure(XKind::CreateDir, 1, io::ErrorKind::PermissionDenied)
            .with_remove_tree(RemoveTreeMode::Noop);
        let e = err_of(stage(
            rig.env(&failing, &LIMITS),
            &a_art.entry,
            &a_art.artifact,
        ));
        assert_eq!(e.code(), "E_EXTRACT_DENIED", "{label}");
        assert_eq!(children().len(), 11, "{label}: the twin's leftover");
        assert_eq!(prune(rig.env(io, &LIMITS)), 1, "{label}: the twin pruned");
        assert_eq!(children().len(), 10, "{label}");
        drop((trees, a_tree, b));
    });
}

#[test]
fn concurrent_stage_is_safe() {
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        concurrent_variant(
            "Mem",
            &rig,
            &mem,
            "exec-and-symlink",
            &|| rig.mem_children(&mem),
            &|p| matches!(mem.node(p), Some(MemNode::Dir { .. })),
        );
    }
    {
        let rig = Rig::new();
        let children = || {
            rig.extract_names()
                .into_iter()
                .map(|n| rig.extract().join(n))
                .collect()
        };
        concurrent_variant("Fs", &rig, &FsExtractIo, "ustar-prefix", &children, &|p| {
            fs::symlink_metadata(p).is_ok_and(|m| m.is_dir())
        });
    }
}

// ---- SC-027, SC-028, SC-029, SC-030, SC-031 --------------------------------------------------------------------------

#[test]
fn file_hashes_match_written_bytes() {
    let file_lines_of = |listing: &CanonicalListing| -> Vec<String> {
        listing
            .lines()
            .iter()
            .filter_map(|l| l.strip_prefix("f ").map(|rest| rest[5..].to_owned()))
            .collect()
    };
    // real-shaped over the real filesystem: each hash is the SHA-256 of the archive's bytes for that file.
    {
        let rig = Rig::new();
        let art = rig.signed_good("real-shaped");
        let tree =
            stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
        let files = tar_files(&gunzip(&good("real-shaped")));
        let keys: Vec<String> = tree.file_hashes().iter().map(|(p, _)| p.clone()).collect();
        assert_eq!(keys, file_lines_of(tree.listing()));
        for (path, hash) in tree.file_hashes() {
            assert_eq!(
                *hash,
                sha(files.get(path).expect("in the archive")),
                "{path}"
            );
        }
    }
    // exec-and-symlink over a recording wrapper around Mem: each hash is the SHA-256 of the recorded writes.
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        let rec = RecordingExtractIo::forwarding(&mem);
        let art = rig.signed_good("exec-and-symlink");
        let tree = stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact).expect("stages");
        let keys: Vec<String> = tree.file_hashes().iter().map(|(p, _)| p.clone()).collect();
        assert_eq!(keys, file_lines_of(tree.listing()));
        assert!(!keys.iter().any(|k| k == LIBALPHA), "no hash for a link");
        let calls = rec.calls();
        for (path, hash) in tree.file_hashes() {
            assert_eq!(
                *hash,
                sha(&write_records(&calls, &at(tree.root(), path))),
                "{path}"
            );
        }
        let reads: Vec<&XOp> = calls
            .iter()
            .filter(|op| op.kind() == XKind::ReadFile)
            .collect();
        assert_eq!(reads.len(), 1, "{reads:?}");
        assert!(
            reads[0]
                .path()
                .is_some_and(|p| p.ends_with(Path::new("Contents/Resources/moduleinfo.json")))
        );
        assert!(!calls.iter().any(|op| op.kind() == XKind::OpenArchive
            && op.path().is_some_and(|p| p.starts_with(tree.root()))));
    }
    // A zero-length file.
    {
        let rig = Rig::new();
        let lines = lines_with(&["f 0644 presets/Empty.vstpreset"]);
        let bytes = base_with(|t| {
            t.file("presets/Empty.vstpreset", 0o644, b"");
        });
        let art = rig.signed(&bytes, V, literal_payload(&lines));
        let tree =
            stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
        let empty = tree
            .file_hashes()
            .iter()
            .find(|(p, _)| p == "presets/Empty.vstpreset")
            .expect("a hash");
        assert_eq!(empty.1, sha(b""));
    }
}

#[test]
fn moduleinfo_version_must_match() {
    // (a) The fixture's class Version 1.2.0 matches.
    {
        let rig = Rig::new();
        let art = rig.signed_good("real-shaped");
        stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("(a) stages");
    }
    let padded = {
        let mut info = moduleinfo(V);
        info.resize(65, b' ');
        info
    };
    let mut bom = "\u{feff}".as_bytes().to_vec();
    bom.extend_from_slice(&moduleinfo(V));
    let small = ExtractLimits {
        max_moduleinfo_bytes: 64,
        ..LIMITS
    };
    // (b) to (e), (g): refused, the work dir removed, extract/ empty.
    let rows: Vec<(&str, Vec<u8>, &str, ExtractLimits, ExtractError)> = vec![
        (
            "(b) another version",
            good("real-shaped"),
            "1.2.1",
            LIMITS,
            ExtractError::Version {
                expected: "1.2.1".to_owned(),
                found: "1.2.0".to_owned(),
            },
        ),
        (
            "(c) not JSON",
            archive_with_moduleinfo(b"{not json"),
            V,
            LIMITS,
            ExtractError::Version {
                expected: V.to_owned(),
                found: "unknown".to_owned(),
            },
        ),
        (
            "(d) no class Version",
            archive_with_moduleinfo(br#"{"Name": "Alpha", "Classes": [{"Name": "Alpha"}]}"#),
            V,
            LIMITS,
            ExtractError::Version {
                expected: V.to_owned(),
                found: "unknown".to_owned(),
            },
        ),
        (
            "(e) over the bound",
            archive_with_moduleinfo(&padded),
            V,
            small,
            ExtractError::Limit {
                which: "moduleinfo",
            },
        ),
        (
            "(g) not UTF-8",
            archive_with_moduleinfo(&[0xff, 0xfe, b'{', b'}']),
            V,
            LIMITS,
            ExtractError::Version {
                expected: V.to_owned(),
                found: "unknown".to_owned(),
            },
        ),
    ];
    for (label, bytes, version, limits, want) in rows {
        install_capture();
        let _ = take_mine();
        let rig = Rig::new();
        let payload = if label.starts_with("(b)") {
            good_payload("real-shaped")
        } else {
            literal_payload(&BASE_LINES)
        };
        let art = rig.signed(&bytes, version, payload);
        let rec = RecordingExtractIo::forwarding(&FsExtractIo);
        let e = err_of(stage(rig.env(&rec, &limits), &art.entry, &art.artifact));
        assert_eq!(e, want, "{label}");
        assert!(
            removed_trees(&rec.calls())
                .iter()
                .any(|p| p.parent() == Some(rig.extract().as_path())),
            "{label}: remove_tree of the work dir"
        );
        assert!(rig.extract_names().is_empty(), "{label}");
        // (f) The log of (c) names the relative path only.
        if label.starts_with("(c)") {
            let records = take_mine();
            assert!(
                records
                    .iter()
                    .any(|(_, _, m)| m.contains("Contents/Resources/moduleinfo.json")),
                "(f) {records:?}"
            );
            for (_, _, message) in &records {
                for path in [rig.td.path(), rig.base.as_path()] {
                    assert!(!message.contains(&*path.to_string_lossy()), "(f) {message}");
                }
            }
        }
    }
    // (h) A leading BOM is stripped.
    {
        let rig = Rig::new();
        let art = rig.signed(
            &archive_with_moduleinfo(&bom),
            V,
            literal_payload(&BASE_LINES),
        );
        stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("(h) stages");
    }
}

fn archive_with_moduleinfo(info: &[u8]) -> Vec<u8> {
    let mut t = Tar::new();
    base(&mut t, info);
    gz(&t.finish())
}

#[test]
fn prune_removes_by_kind() {
    // A non-live directory tree, file and link (to a canary directory outside extract/), and one live root.
    let plant = |rig: &Rig, mem: &MemExtractIo| {
        let extract = rig.extract();
        mem.plant_dir(&rig.staging().join("canary"), 0o755);
        mem.plant_file(
            &rig.staging().join("canary").join("keep.txt"),
            b"keep",
            0o644,
        );
        mem.plant_dir(&extract.join("d1"), 0o755);
        mem.plant_file(&extract.join("d1").join("x"), b"x", 0o644);
        mem.plant_file(&extract.join("f1"), b"f", 0o644);
        mem.plant_link(&extract.join("l1"), "../canary");
    };
    let canary_intact = |rig: &Rig, mem: &MemExtractIo| {
        assert!(matches!(
            mem.node(&rig.staging().join("canary")),
            Some(MemNode::Dir { .. })
        ));
        assert!(matches!(
            mem.node(&rig.staging().join("canary").join("keep.txt")),
            Some(MemNode::File { .. })
        ));
    };
    install_capture();
    let _ = take_mine();
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        let art = rig.signed_good("exec-and-symlink");
        let live = stage(rig.env(&mem, &LIMITS), &art.entry, &art.artifact).expect("the live root");
        plant(&rig, &mem);
        assert_eq!(prune(rig.env(&mem, &LIMITS)), 3);
        let extract = rig.extract();
        for name in ["d1", "f1", "l1"] {
            assert_eq!(mem.node(&extract.join(name)), None, "{name}");
        }
        assert_eq!(mem.node(&extract.join("d1").join("x")), None);
        canary_intact(&rig, &mem);
        assert!(matches!(mem.node(live.root()), Some(MemNode::Dir { .. })));
        assert!(mem.node(&at(live.root(), SO)).is_some());
    }
    // A missing extract/: 0, nothing created.
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        assert_eq!(prune(rig.env(&mem, &LIMITS)), 0);
        assert_eq!(mem.node(&rig.extract()), None);
        assert_eq!(mem.mutating_calls(), 0);
    }
    // One injected remove_entry failure: logged, skipped, the other two removed.
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        plant(&rig, &mem);
        let faulty = FaultyExtractIo::new(&mem).with_failure(
            XKind::RemoveEntry,
            1,
            io::ErrorKind::PermissionDenied,
        );
        assert_eq!(prune(rig.env(&faulty, &LIMITS)), 2);
        let extract = rig.extract();
        assert_eq!(mem.node(&extract.join("d1")), None);
        let kept: Vec<&str> = ["f1", "l1"]
            .into_iter()
            .filter(|n| mem.node(&extract.join(n)).is_some())
            .collect();
        assert_eq!(kept.len(), 1, "{kept:?}");
        let records = take_mine();
        assert!(
            records.iter().any(|(t, level, m)| t == LOG_TARGET
                && *level == Level::Warn
                && m.contains(kept[0])),
            "{records:?}"
        );
        for (_, _, message) in &records {
            for path in [rig.td.path(), rig.base.as_path()] {
                assert!(!message.contains(&*path.to_string_lossy()), "{message}");
            }
        }
        canary_intact(&rig, &mem);
    }
    // SI-9: a linked extract/ is never listed.
    {
        let rig = Rig::new();
        let mem = MemExtractIo::new(rig.staging());
        mem.plant_dir(&rig.staging().join("canary"), 0o755);
        mem.plant_file(
            &rig.staging().join("canary").join("keep.txt"),
            b"keep",
            0o644,
        );
        mem.plant_link(&rig.extract(), "canary");
        assert_eq!(prune(rig.env(&mem, &LIMITS)), 0);
        canary_intact(&rig, &mem);
    }
}

#[test]
fn path_length_bounds() {
    let name255 = format!("presets/{}.vstpreset", "a".repeat(245));
    let name256 = format!("presets/{}.vstpreset", "a".repeat(246));
    assert_eq!(name255.len() - "presets/".len(), 255);
    // (a) A 255-byte segment stages.
    let lines = lines_with(&[format!("f 0644 {name255}").as_str()]);
    let bytes = base_with(|t| {
        t.long_file(name255.as_bytes(), b"long name");
    });
    {
        let rig = Rig::new();
        let art = rig.signed(&bytes, V, literal_payload(&lines));
        let tree = stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("(a)");
        assert_eq!(
            fs::read(at(tree.root(), &name255)).expect("(a) the file"),
            b"long name"
        );
    }
    // (b) A 256-byte segment.
    {
        let rig = Rig::new();
        let bytes = base_with(|t| {
            t.long_file(name256.as_bytes(), b"x");
        });
        let art = rig.signed(&bytes, V, NO_PAYLOAD);
        let rec = RecordingExtractIo::fail_closed();
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(e, ExtractError::Limit { which: "segment" });
        assert_eq!(rec.mutating_calls(), 0, "(d)");
        assert!(rig.extract_names().is_empty(), "(d)");
    }
    // (c) The joined bound at the exact length and one below it.
    {
        let rig = Rig::new();
        let art = rig.signed(&bytes, V, literal_payload(&lines));
        let hex = hex_lower(art.artifact.sha256());
        let mut computed = rig.staging().join("extract").join(format!("{hex}.xxxxxx"));
        for s in name255.split('/') {
            computed.push(s);
        }
        let computed = computed.as_os_str().as_encoded_bytes().len();
        let below = ExtractLimits {
            max_joined_bytes: computed - 1,
            ..LIMITS
        };
        let rec = RecordingExtractIo::fail_closed();
        let e = err_of(stage(rig.env(&rec, &below), &art.entry, &art.artifact));
        assert_eq!(e, ExtractError::Limit { which: "joined" });
        assert_eq!(rec.mutating_calls(), 0, "(d)");
        assert!(rig.extract_names().is_empty(), "(d)");
        let exact = ExtractLimits {
            max_joined_bytes: computed,
            ..LIMITS
        };
        let tree = stage(rig.env(&FsExtractIo, &exact), &art.entry, &art.artifact).expect("(c)");
        let name = tree.root().file_name().expect("a name");
        assert_eq!(name.as_encoded_bytes().len(), 64 + 1 + 6);
        assert!(name.to_string_lossy().starts_with(&format!("{hex}.")));
        assert_eq!(tree.root().parent(), Some(rig.extract().as_path()));
        let mut created = tree.root().to_path_buf();
        for s in name255.split('/') {
            created.push(s);
        }
        assert_eq!(created.as_os_str().as_encoded_bytes().len(), computed);
        assert!(created.is_file());
    }
}

#[test]
fn trailing_zero_padding_is_accepted() {
    let original = good("real-shaped");
    let listing = good_listing("real-shaped");
    let padded = |n: usize| {
        let mut b = original.clone();
        b.extend(std::iter::repeat_n(0u8, n));
        b
    };
    for n in [1, 10, 511, 100_000] {
        let plan = plan_archive(&padded(n)[..], &LIMITS, 64).unwrap_or_else(|e| panic!("{n}: {e}"));
        assert_eq!(plan.listing.text(), listing, "{n}");
        let rig = Rig::new();
        let art = rig.signed(&padded(n), V, good_payload("real-shaped"));
        assert_eq!(*art.artifact.sha256(), sha(&padded(n)));
        stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact)
            .unwrap_or_else(|e| panic!("{n}: {e}"));
    }
    let corrupt_code = |label: &str, bytes: &[u8]| match plan_archive(bytes, &LIMITS, 64) {
        Ok(_) => panic!("{label}: planned"),
        Err(e) => assert_eq!(e.code(), "E_LISTING_CORRUPT", "{label}: {e}"),
    };
    let mut odd = original.clone();
    odd.extend_from_slice(&[0, 0, 1]);
    corrupt_code("00 00 01", &odd[..]);
    let mut garbage = original.clone();
    garbage.extend_from_slice(b"garbage!");
    corrupt_code("garbage!", &garbage[..]);
    // Padding over an injected max_tar_bytes.
    let tar = gunzip(&original);
    let bound = ExtractLimits {
        max_tar_bytes: tar.len() as u64 + 50,
        ..LIMITS
    };
    plan_archive(&padded(10)[..], &bound, 64).expect("10 zeros stay under the bound");
    match plan_archive(&padded(100)[..], &bound, 64) {
        Err(ExtractError::Limit { which: "tar_bytes" }) => {}
        other => panic!("padding over the bound: {:?}", other.err()),
    }
    // The gzip layer (FR-010).
    let mut two = gz(&tar[..1000]);
    two.extend_from_slice(&gz(&tar[1000..]));
    let plan = plan_archive(&two[..], &LIMITS, 64).expect("(g1) two members");
    assert_eq!(plan.listing.text(), listing, "(g1)");
    let len = original.len();
    let flip = |i: usize| {
        let mut b = original.clone();
        b[i] ^= 0x01;
        b
    };
    corrupt_code("(g2) CRC", &flip(len - 8)[..]);
    corrupt_code("(g3) ISIZE", &flip(len - 1)[..]);
    corrupt_code("(g4) cut in the trailer", &original[..len - 4]);
    corrupt_code("(g5) cut in the deflate stream", &original[..len / 2]);
    let mut zero_first = original.clone();
    zero_first[0] = 0;
    corrupt_code("(g6) first byte 0", &zero_first[..]);
}

// ---- Unix (macOS and Linux) ------------------------------------------------------------------------------------------

#[cfg(unix)]
#[test]
fn prune_removes_real_link_unix() {
    use std::os::unix::fs::symlink;
    // A real link in extract/ to a canary directory: removed as a link.
    {
        let rig = Rig::new();
        let canary = rig.base.join("canary");
        fs::create_dir_all(&canary).expect("canary");
        fs::write(canary.join("keep.txt"), b"keep").expect("keep.txt");
        fs::create_dir_all(rig.extract()).expect("extract");
        let link = rig.extract().join("l1");
        symlink(&canary, &link).expect("the link");
        assert_eq!(prune(rig.env(&FsExtractIo, &LIMITS)), 1);
        assert!(fs::symlink_metadata(&link).is_err(), "the link is gone");
        assert_eq!(
            fs::read(canary.join("keep.txt")).expect("keep.txt"),
            b"keep"
        );
        let art = rig.signed_good("real-shaped");
        stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
    }
    // SI-9: extract/ itself a link: never listed, and the production create_work_dir refuses it.
    {
        let rig = Rig::new();
        let canary = rig.base.join("canary");
        fs::create_dir_all(&canary).expect("canary");
        fs::write(canary.join("keep.txt"), b"keep").expect("keep.txt");
        symlink(&canary, rig.extract()).expect("the extract link");
        let before = snapshot(&canary);
        assert_eq!(prune(rig.env(&FsExtractIo, &LIMITS)), 0);
        let art = rig.signed_good("real-shaped");
        let rec = RecordingExtractIo::forwarding(&FsExtractIo);
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert!(
            matches!(
                e,
                ExtractError::Io {
                    step: "create_work_dir",
                    kind: io::ErrorKind::InvalidInput,
                    ..
                }
            ),
            "{e}"
        );
        let calls = rec.calls();
        let made: Vec<usize> = calls
            .iter()
            .enumerate()
            .filter(|(_, op)| op.kind() == XKind::CreateWorkDir)
            .map(|(i, _)| i)
            .collect();
        assert_eq!(made.len(), 1);
        assert!(
            !calls[made[0] + 1..].iter().any(XOp::is_mutating),
            "{calls:?}"
        );
        assert_eq!(snapshot(&canary), before);
    }
}

#[cfg(unix)]
#[test]
fn posix_modes_and_links_unix() {
    use std::os::unix::fs::PermissionsExt as _;
    let rig = Rig::new();
    let art = rig.signed_good("exec-and-symlink");
    let tree = stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
    for line in tree.listing().lines() {
        let line = parse_line(line);
        let meta = fs::symlink_metadata(at(tree.root(), line.path)).expect("metadata");
        let mode = meta.permissions().mode() & 0o7777;
        match line.kind {
            'd' => assert_eq!(mode, 0o755, "{}", line.path),
            'f' if line.path == SO => assert_eq!(mode, 0o755, "{}", line.path),
            'f' => assert_eq!(mode, 0o644, "{}", line.path),
            _ => assert!(meta.file_type().is_symlink(), "{}", line.path),
        }
    }
    assert_eq!(
        fs::read_link(at(tree.root(), LIBALPHA)).expect("read_link"),
        Path::new("Alpha.so")
    );
    let rebuilt =
        CanonicalListing::from_entries(&FsExtractIo.read_tree(tree.root()).expect("read_tree"))
            .expect("rebuilt");
    assert_eq!(
        hex_lower(&rebuilt.digest()),
        "baad75026258c646e2887dd01d00dbd85f57ec43111430c2a57563da80659704"
    );
}

#[cfg(unix)]
#[test]
fn special_bits_and_mtime_not_applied_unix() {
    use std::os::unix::fs::PermissionsExt as _;
    let mut t = Tar::new();
    t.mtime = 978_307_200;
    t.dir("Alpha.vst3")
        .dir("Alpha.vst3/Contents")
        .dir("Alpha.vst3/Contents/Resources")
        .file(
            "Alpha.vst3/Contents/Resources/moduleinfo.json",
            0o644,
            &moduleinfo(V),
        )
        .dir("Alpha.vst3/Contents/x86_64-linux")
        .file(SO, 0o4755, b"alpha shared object")
        .record(b"presets/", b'5', 0o1755, b"", b"")
        .file("presets/Init.vstpreset", 0o644, b"init preset")
        .file("README.txt", 0o2644, b"read me")
        .file("CHANGELOG.txt", 0o644, b"changes");
    let bytes = gz(&t.finish());
    let lines: Vec<String> = BASE_LINES
        .iter()
        .map(|l| {
            if l.ends_with("Alpha.so") {
                format!("f 0755 {SO}")
            } else {
                (*l).to_owned()
            }
        })
        .collect();
    let rig = Rig::new();
    let art = rig.signed(&bytes, V, literal_payload(&lines));
    let tree = stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
    let mode = |rel: &str| {
        fs::symlink_metadata(at(tree.root(), rel))
            .expect("metadata")
            .permissions()
            .mode()
            & 0o7777
    };
    assert_eq!(mode(SO), 0o755);
    assert_eq!(mode("README.txt"), 0o644);
    assert_eq!(mode("presets"), 0o755);
    let modified = fs::symlink_metadata(at(tree.root(), "README.txt"))
        .and_then(|m| m.modified())
        .expect("modified");
    assert!(modified > std::time::SystemTime::UNIX_EPOCH + Duration::from_secs(978_393_600));
}

#[cfg(unix)]
#[test]
fn escape_cases_unix() {
    for name in [
        "symlink-escape-root",
        "symlink-escape-bundle",
        "symlink-chain",
        "symlink-self",
        "through-symlink",
    ] {
        let expected =
            String::from_utf8(read_fixture(&format!("listings/bad/{name}.expected-error")))
                .expect("UTF-8");
        let rig = Rig::new();
        let art = rig.signed(&bad(name), V, NO_PAYLOAD);
        let before = snapshot(rig.td.path());
        let rec = RecordingExtractIo::forwarding(&FsExtractIo);
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(e.code(), expected.trim(), "{name}");
        assert_eq!(rec.mutating_calls(), 0, "{name}");
        assert_eq!(snapshot(rig.td.path()), before, "{name}");
        assert!(rig.extract_names().is_empty(), "{name}");
    }
}

#[cfg(unix)]
#[test]
fn symlinked_staging_root_unix() {
    let linked = || {
        let td = TempDir::new().expect("a TempDir");
        let roots = crate::test_support::temp_roots_via_link(td.path());
        Rig::over(td, roots)
    };
    // Success: both sides went through canonicalize.
    {
        let rig = linked();
        let staging = rig.staging().to_path_buf();
        assert_ne!(
            staging,
            fs::canonicalize(&staging).expect("canonical"),
            "precondition"
        );
        let art = rig.signed_good("real-shaped");
        let rec = RecordingExtractIo::forwarding(&FsExtractIo);
        let tree = stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact).expect("stages");
        let seen = rec.canonicalized();
        assert!(
            seen.iter().any(|p| p == tree.root()),
            "the work dir, spelled through via"
        );
        for line in tree.listing().lines() {
            let line = parse_line(line);
            let path = at(tree.root(), line.path);
            let parent = path.parent().expect("a parent").to_path_buf();
            assert!(
                seen.contains(&path) || seen.contains(&parent),
                "{}",
                line.path
            );
        }
    }
    // The negative twin: one staged file mapped to a sibling inside the TempDir.
    {
        let rig = linked();
        let sibling = rig.base.join("sibling");
        let rec = RecordingExtractIo::forwarding(&FsExtractIo).with_canonicalize(Box::new(
            move |path: &Path, result: io::Result<PathBuf>| {
                if path.file_name().is_some_and(|n| n == "README.txt") {
                    Ok(sibling.clone())
                } else {
                    result
                }
            },
        ));
        let art = rig.signed_good("real-shaped");
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(
            e,
            ExtractError::Escape {
                path: "README.txt".to_owned()
            }
        );
        assert!(rig.extract_names().is_empty());
    }
    // FR-046 (1): a dangling in-bundle link stages and is left dangling.
    {
        let rig = linked();
        let missing = "Alpha.vst3/Contents/x86_64-linux/missing.so";
        let lines =
            lines_with(&["l 0777 Alpha.vst3/Contents/x86_64-linux/missing.so -> nothere.so"]);
        let bytes = base_with(|t| {
            t.link(missing, "nothere.so");
        });
        let art = rig.signed(&bytes, V, literal_payload(&lines));
        let tree =
            stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
        let link = at(tree.root(), missing);
        assert!(
            fs::symlink_metadata(&link)
                .expect("a link")
                .file_type()
                .is_symlink()
        );
        assert_eq!(
            fs::read_link(&link).expect("read_link"),
            Path::new("nothere.so")
        );
        assert_eq!(
            fs::metadata(&link).map(|_| ()).map_err(|e| e.kind()),
            Err(io::ErrorKind::NotFound)
        );
    }
    // FR-046 (2): the link resolves inside the work dir but outside its top-level entry.
    {
        let rig = linked();
        let rec = RecordingExtractIo::forwarding(&FsExtractIo).with_canonicalize(Box::new(
            |path: &Path, result: io::Result<PathBuf>| {
                if path.ends_with(Path::new(LIBALPHA)) {
                    let work = fs::canonicalize(path.ancestors().nth(4).expect("the work dir"))?;
                    Ok(work.join("presets").join("x"))
                } else {
                    result
                }
            },
        ));
        let art = rig.signed_good("exec-and-symlink");
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(
            e,
            ExtractError::Escape {
                path: LIBALPHA.to_owned()
            }
        );
        assert!(rig.extract_names().is_empty());
    }
}

// ---- macOS -----------------------------------------------------------------------------------------------------------

#[cfg(target_os = "macos")]
#[test]
fn staged_tree_has_no_xattrs_macos() {
    let rig = Rig::new();
    let art = rig.signed(&quarantined(), V, literal_payload(&BASE_LINES));
    let tree = stage(rig.env(&FsExtractIo, &LIMITS), &art.entry, &art.artifact).expect("stages");
    let out = std::process::Command::new("/usr/bin/xattr")
        .args(["-lr"])
        .arg(tree.root())
        .output()
        .expect("run xattr");
    assert!(out.status.success(), "{out:?}");
    assert!(
        out.stdout.is_empty(),
        "{}",
        String::from_utf8_lossy(&out.stdout)
    );
}

#[cfg(target_os = "macos")]
#[test]
fn raw_temp_dir_staging_root_macos() {
    let td = TempDir::new().expect("a TempDir");
    let roots = crate::test_support::temp_roots_raw(td.path());
    let rig = Rig::over(td, roots);
    let staging = rig.staging().to_path_buf();
    let canonical = fs::canonicalize(&staging).expect("canonical");
    assert_ne!(staging, canonical, "precondition");
    assert!(canonical.starts_with("/private/var/"), "{canonical:?}");
    let art = rig.signed_good("real-shaped");
    let rec = RecordingExtractIo::forwarding(&FsExtractIo);
    let tree = stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact).expect("stages");
    let seen = rec.canonicalized();
    assert!(
        seen.iter().any(|p| p == tree.root()),
        "the work dir, raw spelling"
    );
    for line in tree.listing().lines() {
        let line = parse_line(line);
        let path = at(tree.root(), line.path);
        let parent = path.parent().expect("a parent").to_path_buf();
        assert!(
            seen.contains(&path) || seen.contains(&parent),
            "{}",
            line.path
        );
    }
}

// ---- Windows ---------------------------------------------------------------------------------------------------------

#[cfg(windows)]
#[test]
fn symlink_entry_unsupported_windows() {
    {
        let rig = Rig::new();
        let art = rig.signed_good("exec-and-symlink");
        let rec = RecordingExtractIo::forwarding(&FsExtractIo);
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert_eq!(
            e,
            ExtractError::Unsupported {
                entry: LIBALPHA.to_owned()
            }
        );
        assert_eq!(rec.mutating_calls(), 0);
        assert!(
            fs::symlink_metadata(rig.extract()).is_err(),
            "extract/ absent"
        );
        let art = rig.signed_good("real-shaped");
        let rec = RecordingExtractIo::forwarding(&FsExtractIo);
        stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact).expect("real-shaped stages");
    }
    // mklink takes plain drive paths, not the verbatim form the canonical temp base has.
    let plain = |p: &Path| {
        let s = p.to_str().expect("UTF-8");
        s.strip_prefix(r"\\?\").unwrap_or(s).to_owned()
    };
    let junction = |link: &Path, target: &Path| {
        let made = std::process::Command::new("cmd")
            .args(["/C", "mklink", "/J"])
            .arg(plain(link))
            .arg(plain(target))
            .output()
            .expect("run cmd");
        assert!(made.status.success(), "mklink /J failed: {made:?}");
    };
    let with_canary = || {
        let rig = Rig::new();
        let canary = rig.base.join("canary");
        fs::create_dir_all(&canary).expect("canary");
        fs::write(canary.join("keep.txt"), b"keep").expect("keep.txt");
        (rig, canary)
    };
    // (j1) A junction child of extract/ is removed as itself (PD-7).
    {
        let (rig, canary) = with_canary();
        fs::create_dir_all(rig.extract()).expect("extract");
        let link = rig.extract().join(format!("{}.abcdef", "0".repeat(64)));
        junction(&link, &canary);
        assert_eq!(prune(rig.env(&FsExtractIo, &LIMITS)), 1);
        assert_eq!(
            fs::symlink_metadata(&link)
                .map(|_| ())
                .map_err(|e| e.kind()),
            Err(io::ErrorKind::NotFound)
        );
        assert_eq!(
            fs::read(canary.join("keep.txt")).expect("keep.txt"),
            b"keep"
        );
    }
    // (j2) A junction at extract/ itself is never listed, and create_work_dir refuses it (PD-6).
    {
        let (rig, canary) = with_canary();
        junction(&rig.extract(), &canary);
        let before = snapshot(&canary);
        assert_eq!(prune(rig.env(&FsExtractIo, &LIMITS)), 0);
        assert_eq!(snapshot(&canary), before);
        let art = rig.signed_good("real-shaped");
        let rec = RecordingExtractIo::forwarding(&FsExtractIo);
        let e = err_of(stage(rig.env(&rec, &LIMITS), &art.entry, &art.artifact));
        assert!(
            matches!(
                e,
                ExtractError::Io {
                    step: "create_work_dir",
                    kind: io::ErrorKind::InvalidInput,
                    ..
                }
            ),
            "{e}"
        );
        assert_eq!(snapshot(&canary), before);
    }
}
