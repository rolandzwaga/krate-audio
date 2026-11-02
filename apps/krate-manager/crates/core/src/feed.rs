//! The feed client: parse, the typed model, `check` and the cache (D1, D3, D7, D8, FR-020 to FR-029, FR-050 to
//! FR-059; plan §4.3).

use crate::context::AppContext;
use crate::contract::{self, Finding, Schema};
use crate::http::{self, HttpError};
use crate::version::{CoreVersion, OsVersion};
use log::{info, warn};
use regex::Regex;
use serde::Serialize;
use serde_json::{Map, Value};
use std::collections::BTreeMap;
use std::io::{self, Write};
use std::path::Path;
use std::sync::{Arc, OnceLock};
use tempfile::NamedTempFile;

pub const FEED_CACHE_FILE: &str = "feed-cache.json";
pub const CACHE_VERSION: u64 = 1;
pub const MAX_FEED_BYTES: u64 = 4 * 1024 * 1024;
/// A release can take up to this long to reach the client through the Pages edge (measured 2026-10-30, D7).
pub const EDGE_MAX_AGE_SECS: u64 = 600;
pub(crate) const LOG_TARGET: &str = "krate_manager_core::feed";

/// Log codes (plan §3), never findings.
pub(crate) const MODEL_RANGE: &str = "E_MODEL_RANGE";
pub(crate) const MODEL_ASSET_NAME: &str = "E_MODEL_ASSET_NAME";
pub(crate) const MODEL_V1_ENTRY: &str = "E_MODEL_V1_ENTRY";

/// The cache's temp-file prefix (PD-6), distinct from the settings file's.
const TEMP_PREFIX: &str = ".feed-cache.json.";
/// The D8 keys, in file order.
const CACHE_KEYS: [&str; 4] = ["cache_version", "fetched_at_unix", "source_url", "body"];
/// PD-8: the largest integer an `f64` holds exactly.
const MAX_EXACT_F64_INT: f64 = 9_007_199_254_740_992.0;

#[derive(Clone, Debug, PartialEq)]
pub struct Feed {
    pub schema_version: u8,
    pub plugins: BTreeMap<String, PluginEntry>,
    pub manager: Option<ManagerInfo>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct PluginEntry {
    pub name: String,
    pub version_text: String,
    pub version: Option<CoreVersion>,
    /// PD-22, SI-19: a page link, never fetched for install.
    pub download_url: Option<PageUrl>,
    pub release: Option<StableRelease>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct StableRelease {
    pub tag: String,
    pub pub_date: String,
    pub release_notes_url: String,
    pub notes_markdown: Option<String>,
    pub platforms: BTreeMap<PlatformKey, PlatformAsset>,
    pub install: Option<InstallFacts>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct PlatformAsset {
    pub url: AssetUrl,
    pub asset_name: AssetName,
    pub size: u64,
    pub sha256: [u8; 32],
    pub signature: String,
    pub format: AssetFormat,
    pub min_os: Option<OsVersion>,
    pub payload: Option<Payload>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Payload {
    pub files: u64,
    pub sha256: [u8; 32],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AssetFormat {
    InnoExe,
    Pkg,
    TarGz,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum PlatformKey {
    WindowsX86_64,
    DarwinAarch64,
    DarwinX86_64,
    DarwinUniversal,
    LinuxX86_64,
}

impl PlatformKey {
    pub fn as_str(self) -> &'static str {
        match self {
            PlatformKey::WindowsX86_64 => "windows-x86_64",
            PlatformKey::DarwinAarch64 => "darwin-aarch64",
            PlatformKey::DarwinX86_64 => "darwin-x86_64",
            PlatformKey::DarwinUniversal => "darwin-universal",
            PlatformKey::LinuxX86_64 => "linux-x86_64",
        }
    }

    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "windows-x86_64" => Some(PlatformKey::WindowsX86_64),
            "darwin-aarch64" => Some(PlatformKey::DarwinAarch64),
            "darwin-x86_64" => Some(PlatformKey::DarwinX86_64),
            "darwin-universal" => Some(PlatformKey::DarwinUniversal),
            "linux-x86_64" => Some(PlatformKey::LinuxX86_64),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct InstallFacts {
    pub vst3_bundle: PlainName,
    pub au_component: Option<PlainName>,
    pub preset_subdir: Option<PlainName>,
    pub windows_uninstall_key: Option<PlainName>,
    pub macos_pkg_id: Option<PlainName>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ManagerInfo {
    pub latest_json_url: String,
}

/// A checked install name; no public constructor (FR-026).
///
/// ```compile_fail
/// let _ = krate_manager_core::feed::PlainName(String::new());
/// ```
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PlainName(String);

impl PlainName {
    pub fn as_str(&self) -> &str {
        &self.0
    }

    /// Re-runs the contract's install-value rule (reader rule 8, FR-026).
    pub(crate) fn checked(field: &str, v: &Value) -> Option<Self> {
        contract::check_install_value(field, v).ok()?;
        v.as_str().map(|s| PlainName(s.to_owned()))
    }
}

/// A checked asset URL; no public constructor, only `PlatformAsset.url` holds one.
///
/// ```compile_fail
/// let _ = krate_manager_core::feed::AssetUrl(String::new());
/// ```
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AssetUrl(String);

impl AssetUrl {
    pub fn as_str(&self) -> &str {
        &self.0
    }

    pub(crate) fn checked(v: &str) -> Option<Self> {
        is_c1_url(v).then(|| AssetUrl(v.to_owned()))
    }
}

/// A checked release-page URL; no public constructor, only `PluginEntry.download_url` holds one (PD-22).
///
/// ```compile_fail
/// let _ = krate_manager_core::feed::PageUrl(String::new());
/// ```
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PageUrl(String);

impl PageUrl {
    pub fn as_str(&self) -> &str {
        &self.0
    }

    pub(crate) fn checked(v: &str) -> Option<Self> {
        is_c1_url(v).then(|| PageUrl(v.to_owned()))
    }
}

/// A checked asset file name; no public constructor (PD-21).
///
/// ```compile_fail
/// let _ = krate_manager_core::feed::AssetName(String::new());
/// ```
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AssetName(String);

impl AssetName {
    pub fn as_str(&self) -> &str {
        &self.0
    }

    /// The URL's last path segment as the URL parser holds it (not percent-decoded, FR-025), under PD-21's rule.
    pub(crate) fn from_url(url: &AssetUrl) -> Option<Self> {
        let parsed = url::Url::parse(url.as_str()).ok()?;
        let name = parsed.path_segments()?.next_back()?;
        let safe = asset_name_re().is_match(name)
            && name != "."
            && name != ".."
            && !reserved_stem_re().is_match(name);
        safe.then(|| AssetName(name.to_owned()))
    }
}

/// The C1 `url` rule (`versions.v2.schema.json` `$defs.url`): `^https://[^\s]+$` with ECMA `\s`, and no
/// `/releases/latest/`. One rule for both URL types (plan §4.3.1).
fn is_c1_url(v: &str) -> bool {
    v.strip_prefix("https://")
        .is_some_and(|rest| !rest.is_empty() && !rest.chars().any(is_ecma_whitespace))
        && !v.contains("/releases/latest/")
}

/// ECMA-262 `WhiteSpace` and `LineTerminator`: what `\s` matches under the `u` flag (FR-002).
fn is_ecma_whitespace(c: char) -> bool {
    matches!(
        c,
        '\u{9}'..='\u{D}'
            | '\u{20}'
            | '\u{A0}'
            | '\u{1680}'
            | '\u{2000}'..='\u{200A}'
            | '\u{2028}'
            | '\u{2029}'
            | '\u{202F}'
            | '\u{205F}'
            | '\u{3000}'
            | '\u{FEFF}'
    )
}

fn cached_re(cell: &'static OnceLock<Regex>, pattern: &str) -> &'static Regex {
    cell.get_or_init(|| {
        Regex::new(pattern).unwrap_or_else(|e| panic!("feed pattern {pattern}: {e}"))
    })
}

/// C1 `pluginName` (`versions.v2.schema.json` `$defs.pluginName`).
fn plugin_name_re() -> &'static Regex {
    static RE: OnceLock<Regex> = OnceLock::new();
    cached_re(&RE, "^[A-Z][A-Za-z0-9]{0,63}$")
}

/// C1 `reservedStem`; only applied to text the name patterns above and below already admitted, which holds no line
/// terminator, so `regex`'s `.` and ECMA's agree on it.
fn reserved_stem_re() -> &'static Regex {
    static RE: OnceLock<Regex> = OnceLock::new();
    cached_re(
        &RE,
        r"^([Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][0-9]|[Ll][Pp][Tt][0-9])(\..*)?$",
    )
}

/// PD-21 (the C3 asset field's pattern, `contract.js` `ASSET_NAME_RE`).
fn asset_name_re() -> &'static Regex {
    static RE: OnceLock<Regex> = OnceLock::new();
    cached_re(&RE, "^[A-Za-z0-9][A-Za-z0-9._-]*$")
}

fn is_plugin_name(key: &str) -> bool {
    plugin_name_re().is_match(key) && !reserved_stem_re().is_match(key)
}

/// FR-020: UTF-8 → one JSON value → `schema_version` dispatch → the contract → the sibling filter → the model.
pub fn parse(bytes: &[u8]) -> Result<Feed, FeedError> {
    let text = std::str::from_utf8(bytes).map_err(|e| FeedError::Input {
        reason: format!("the body is not UTF-8: {e}"),
    })?;
    let doc: Value = serde_json::from_str(text).map_err(|e| FeedError::Input {
        reason: format!("the body is not one JSON value: {e}"),
    })?;
    let schema = classify_schema_version(&doc)?;
    let (kept, dropped) = drop_sibling_findings(contract::validate(&doc, schema));
    for f in &dropped {
        match f.keyword {
            Some(keyword) => warn!(target: LOG_TARGET, "{} {} {keyword}", f.code, f.instance_path),
            None => warn!(target: LOG_TARGET, "{} {}", f.code, f.instance_path),
        }
    }
    if !kept.is_empty() {
        return Err(FeedError::Invalid { findings: kept });
    }
    Ok(build(&doc, schema))
}

/// PD-15: absent → V1; an integer (FR-002: `as_u64`, `as_i64`, or an `f64` with no fraction) `1` → V1, `2` → V2,
/// `> 2` → `E_FEED_FUTURE_SCHEMA`; anything else → `E_FEED_SCHEMA_VERSION`.
pub(crate) fn classify_schema_version(doc: &Value) -> Result<Schema, FeedError> {
    let Some(v) = doc.get("schema_version") else {
        return Ok(Schema::V1);
    };
    let found = if let Some(n) = v.as_u64() {
        n
    } else if v.as_i64().is_some() {
        // a negative integer
        return Err(FeedError::SchemaVersion);
    } else {
        match v.as_f64() {
            // `as u64` saturates, so a huge float is `u64::MAX` (PD-15)
            Some(f) if f.fract() == 0.0 && f >= 1.0 => f as u64,
            _ => return Err(FeedError::SchemaVersion),
        }
    };
    match found {
        1 => Ok(Schema::V1),
        2 => Ok(Schema::V2),
        0 => Err(FeedError::SchemaVersion),
        found => Err(FeedError::FutureSchema { found }),
    }
}

/// FR-024, PD-9: `(kept, dropped)`. A finding is dropped when its path is a sibling path, or when it is an install
/// duplicate either of whose sides is.
pub(crate) fn drop_sibling_findings(findings: Vec<Finding>) -> (Vec<Finding>, Vec<Finding>) {
    findings.into_iter().partition(|f| {
        let duplicate_side = f
            .duplicate
            .as_ref()
            .is_some_and(|d| is_sibling_path(&d.first) || is_sibling_path(&d.second));
        !(is_sibling_path(&f.instance_path) || duplicate_side)
    })
}

/// `/plugins/<seg>/prerelease`, exactly or followed by `/`: only the third segment decides, so a plugin named
/// `prerelease` does not.
fn is_sibling_path(path: &str) -> bool {
    let parts: Vec<&str> = path.split('/').collect();
    parts.len() >= 4 && parts[0].is_empty() && parts[1] == "plugins" && parts[3] == "prerelease"
}

/// PD-8: `as_u64()`, else a number with a zero fractional part in `0..=2^53`.
pub(crate) fn json_u64(v: &Value) -> Option<u64> {
    if let Some(n) = v.as_u64() {
        return Some(n);
    }
    let f = v.as_f64()?;
    (f.fract() == 0.0 && (0.0..=MAX_EXACT_F64_INT).contains(&f)).then_some(f as u64)
}

/// One `warn!` `"{code} {path}"` for an item the model leaves out (FR-029, R-7).
fn refuse<T>(code: &str, path: &str) -> Option<T> {
    warn!(target: LOG_TARGET, "{code} {path}");
    None
}

/// The model of a document `contract::validate` accepted under `schema`.
fn build(doc: &Value, schema: Schema) -> Feed {
    let empty = Map::new();
    let entries = doc
        .get("plugins")
        .and_then(Value::as_object)
        .unwrap_or(&empty);
    let mut plugins = BTreeMap::new();
    for (key, entry) in entries {
        let built = match schema {
            Schema::V1 => v1_entry(key, entry),
            Schema::V2 => v2_entry(key, entry),
        };
        if let Some(built) = built {
            plugins.insert(key.clone(), built);
        }
    }
    let manager = match schema {
        Schema::V1 => None,
        Schema::V2 => doc
            .get("manager")
            .and_then(|m| m.get("latest_json_url"))
            .and_then(Value::as_str)
            .map(|url| ManagerInfo {
                latest_json_url: url.to_owned(),
            }),
    };
    Feed {
        schema_version: match schema {
            Schema::V1 => 1,
            Schema::V2 => 2,
        },
        plugins,
        manager,
    }
}

fn str_of<'a>(obj: &'a Value, key: &str) -> Option<&'a str> {
    obj.get(key).and_then(Value::as_str)
}

/// PD-22, FR-029(c): a key outside C1 `pluginName`, or a present `download_url` outside the C1 `url` rule, drops the
/// entry with one warn.
fn v1_entry(key: &str, entry: &Value) -> Option<PluginEntry> {
    if !is_plugin_name(key) {
        return refuse(MODEL_V1_ENTRY, &contract::pointer(&["plugins", key]));
    }
    let download_url = match entry.get("download_url") {
        None => None,
        Some(v) => match v.as_str().and_then(PageUrl::checked) {
            Some(url) => Some(url),
            None => {
                return refuse(
                    MODEL_V1_ENTRY,
                    &contract::pointer(&["plugins", key, "download_url"]),
                );
            }
        },
    };
    let Some(version_text) = str_of(entry, "version") else {
        return refuse(
            MODEL_V1_ENTRY,
            &contract::pointer(&["plugins", key, "version"]),
        );
    };
    Some(PluginEntry {
        name: key.to_owned(),
        version_text: version_text.to_owned(),
        version: CoreVersion::parse(version_text),
        download_url,
        release: None,
    })
}

/// FR-021: the stable entry only (the sibling is never read, FR-024); the five defined platform keys only (reader
/// rule 2); a value the model cannot hold drops its asset (R-7).
fn v2_entry(key: &str, entry: &Value) -> Option<PluginEntry> {
    let at = |field: &str| contract::pointer(&["plugins", key, field]);
    // Each of these is guaranteed by C1; a refusal is a model/contract disagreement, so the entry is left out.
    let Some(version_text) = str_of(entry, "version") else {
        return refuse(MODEL_RANGE, &at("version"));
    };
    let Some(download_url) = str_of(entry, "download_url").and_then(PageUrl::checked) else {
        return refuse(MODEL_RANGE, &at("download_url"));
    };
    let (Some(tag), Some(pub_date), Some(release_notes_url)) = (
        str_of(entry, "tag"),
        str_of(entry, "pub_date"),
        str_of(entry, "release_notes_url"),
    ) else {
        return refuse(MODEL_RANGE, &contract::pointer(&["plugins", key]));
    };
    let mut platforms = BTreeMap::new();
    if let Some(listed) = entry.get("platforms").and_then(Value::as_object) {
        for (name, p) in listed {
            let Some(platform) = PlatformKey::parse(name) else {
                continue;
            };
            if let Some(asset) = platform_asset(key, name, p) {
                platforms.insert(platform, asset);
            }
        }
    }
    let install = match entry.get("install") {
        None => None,
        Some(i) => match install_facts(key, i) {
            Ok(facts) => Some(facts),
            Err(path) => {
                // Without its install names no asset of this entry can be placed: offer none.
                platforms.clear();
                warn!(target: LOG_TARGET, "{MODEL_RANGE} {path}");
                None
            }
        },
    };
    Some(PluginEntry {
        name: key.to_owned(),
        version_text: version_text.to_owned(),
        version: CoreVersion::parse(version_text),
        download_url: Some(download_url),
        release: Some(StableRelease {
            tag: tag.to_owned(),
            pub_date: pub_date.to_owned(),
            release_notes_url: release_notes_url.to_owned(),
            notes_markdown: str_of(entry, "notes_markdown").map(str::to_owned),
            platforms,
            install,
        }),
    })
}

/// One platform asset, or `None` with one warn naming the first value the model cannot hold (PD-8, PD-21, R-7).
fn platform_asset(plugin: &str, key: &str, p: &Value) -> Option<PlatformAsset> {
    let at = |field: &[&str]| {
        let mut segments = vec!["plugins", plugin, "platforms", key];
        segments.extend_from_slice(field);
        contract::pointer(&segments)
    };
    let Some(url) = str_of(p, "url").and_then(AssetUrl::checked) else {
        return refuse(MODEL_RANGE, &at(&["url"]));
    };
    let Some(asset_name) = AssetName::from_url(&url) else {
        return refuse(MODEL_ASSET_NAME, &at(&["url"]));
    };
    let Some(size) = p.get("size").and_then(json_u64) else {
        return refuse(MODEL_RANGE, &at(&["size"]));
    };
    let Some(sha256) = str_of(p, "sha256").and_then(decode_hex32) else {
        return refuse(MODEL_RANGE, &at(&["sha256"]));
    };
    let Some(signature) = str_of(p, "signature") else {
        return refuse(MODEL_RANGE, &at(&["signature"]));
    };
    let format = match str_of(p, "format") {
        Some("inno-exe") => AssetFormat::InnoExe,
        Some("pkg") => AssetFormat::Pkg,
        Some("tar.gz") => AssetFormat::TarGz,
        _ => return refuse(MODEL_RANGE, &at(&["format"])),
    };
    let min_os = match p.get("min_os") {
        None => None,
        Some(v) => match v.as_str().and_then(OsVersion::parse) {
            Some(min_os) => Some(min_os),
            None => return refuse(MODEL_RANGE, &at(&["min_os"])),
        },
    };
    let payload = match p.get("payload") {
        None => None,
        Some(pl) => {
            let Some(files) = pl.get("files").and_then(json_u64) else {
                return refuse(MODEL_RANGE, &at(&["payload", "files"]));
            };
            let Some(sha256) = str_of(pl, "sha256").and_then(decode_hex32) else {
                return refuse(MODEL_RANGE, &at(&["payload", "sha256"]));
            };
            Some(Payload { files, sha256 })
        }
    };
    Some(PlatformAsset {
        url,
        asset_name,
        size,
        sha256,
        signature: signature.to_owned(),
        format,
        min_os,
        payload,
    })
}

/// The install names, each re-checked (FR-026); `Err` names the path of the first refused field.
fn install_facts(plugin: &str, install: &Value) -> Result<InstallFacts, String> {
    let at = |field: &str| contract::pointer(&["plugins", plugin, "install", field]);
    let optional = |field: &str| match install.get(field) {
        None => Ok(None),
        Some(v) => PlainName::checked(field, v)
            .map(Some)
            .ok_or_else(|| at(field)),
    };
    let vst3_bundle = install
        .get("vst3_bundle")
        .and_then(|v| PlainName::checked("vst3_bundle", v))
        .ok_or_else(|| at("vst3_bundle"))?;
    Ok(InstallFacts {
        vst3_bundle,
        au_component: optional("au_component")?,
        preset_subdir: optional("preset_subdir")?,
        windows_uninstall_key: optional("windows_uninstall_key")?,
        macos_pkg_id: optional("macos_pkg_id")?,
    })
}

/// Sixty-four lower-case hex digits (C1 guarantees `[0-9a-f]{64}`), decoded by hand.
fn decode_hex32(s: &str) -> Option<[u8; 32]> {
    fn nibble(b: u8) -> Option<u8> {
        match b {
            b'0'..=b'9' => Some(b - b'0'),
            b'a'..=b'f' => Some(b - b'a' + 10),
            _ => None,
        }
    }
    let (pairs, rest) = s.as_bytes().as_chunks::<2>();
    if pairs.len() != 32 || !rest.is_empty() {
        return None;
    }
    let mut out = [0u8; 32];
    for (slot, [hi, lo]) in out.iter_mut().zip(pairs) {
        *slot = (nibble(*hi)? << 4) | nibble(*lo)?;
    }
    Some(out)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FetchMode {
    Normal,
    /// Sends `Cache-Control: no-cache` and `Pragma: no-cache`. A release can take up to `EDGE_MAX_AGE_SECS` to reach
    /// the client through the Pages edge (measured 2026-10-30, D7).
    CheckNow,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SnapshotSource {
    Fresh,
    Cached,
}

#[derive(Clone, Debug)]
pub struct FeedSnapshot {
    pub feed: Feed,
    pub fetched_at_unix: u64,
    pub source: SnapshotSource,
}

#[derive(Clone, Debug)]
pub struct FeedCheck {
    pub snapshot: Option<FeedSnapshot>,
    pub error: Option<FeedError>,
    pub cache_error: Option<FeedError>,
}

/// The FR-052 cache write steps.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CacheStep {
    CreateDir,
    CreateTemp,
    Write,
    SyncTemp,
    Persist,
    #[cfg(unix)]
    SyncDir,
}

impl CacheStep {
    pub fn as_str(self) -> &'static str {
        match self {
            CacheStep::CreateDir => "create-dir",
            CacheStep::CreateTemp => "create-temp",
            CacheStep::Write => "write",
            CacheStep::SyncTemp => "sync-temp",
            CacheStep::Persist => "persist",
            #[cfg(unix)]
            CacheStep::SyncDir => "sync-dir",
        }
    }
}

/// Why a feed check has no fresh feed (plan §3). Every `Display` begins with its code; `Http` is transparent.
#[derive(Clone, Debug, thiserror::Error)]
pub enum FeedError {
    #[error("E_INPUT: {reason}")]
    Input { reason: String },
    #[error("E_FEED_INVALID: {} finding(s); first: {}", .findings.len(), first_finding(.findings))]
    Invalid { findings: Vec<Finding> },
    #[error("E_FEED_FUTURE_SCHEMA: schema_version {found}: a newer Krate Manager is needed")]
    FutureSchema { found: u64 },
    #[error("E_FEED_SCHEMA_VERSION: schema_version is not a supported value")]
    SchemaVersion,
    #[error("E_FEED_CACHE_IO: the feed cache write failed at {}: {source}", .step.as_str())]
    CacheIo {
        step: CacheStep,
        source: Arc<io::Error>,
    },
    #[error(transparent)]
    Http(#[from] HttpError),
}

impl FeedError {
    pub fn code(&self) -> &'static str {
        match self {
            FeedError::Input { .. } => "E_INPUT",
            FeedError::Invalid { .. } => "E_FEED_INVALID",
            FeedError::FutureSchema { .. } => "E_FEED_FUTURE_SCHEMA",
            FeedError::SchemaVersion => "E_FEED_SCHEMA_VERSION",
            FeedError::CacheIo { .. } => "E_FEED_CACHE_IO",
            FeedError::Http(e) => e.code(),
        }
    }
}

/// The first finding of an `E_FEED_INVALID`, for its `Display`.
fn first_finding(findings: &[Finding]) -> String {
    match findings.first() {
        Some(f) => format!("{} at {:?}: {}", f.code, f.instance_path, f.message),
        None => "none".to_owned(),
    }
}

/// Every filesystem step of the cache (FR-050, FR-052, PD-6), each a fault point. Steps that hold the temp file take
/// it by value and hand it back.
pub(crate) trait CacheIo {
    /// `NotFound` → `Ok(None)`.
    fn read(&mut self, path: &Path) -> io::Result<Option<Vec<u8>>>;
    fn create_dir(&mut self, dir: &Path) -> io::Result<()>;
    /// In `dir`, prefix `.feed-cache.json.`.
    fn create_temp(&mut self, dir: &Path) -> io::Result<NamedTempFile>;
    fn write(&mut self, tmp: NamedTempFile, bytes: &[u8]) -> io::Result<NamedTempFile>;
    fn sync_temp(&mut self, tmp: NamedTempFile) -> io::Result<NamedTempFile>;
    /// A refused rename drops `PersistError.file`, which removes the temp file (FR-057).
    fn persist(&mut self, tmp: NamedTempFile, target: &Path) -> io::Result<()>;
    /// Persist does not sync the directory (tempfile docs), so Unix does it here.
    #[cfg(unix)]
    fn sync_dir(&mut self, dir: &Path) -> io::Result<()>;
}

pub(crate) struct StdCacheIo;

impl CacheIo for StdCacheIo {
    fn read(&mut self, path: &Path) -> io::Result<Option<Vec<u8>>> {
        match std::fs::read(path) {
            Ok(bytes) => Ok(Some(bytes)),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e),
        }
    }

    fn create_dir(&mut self, dir: &Path) -> io::Result<()> {
        std::fs::create_dir_all(dir)
    }

    fn create_temp(&mut self, dir: &Path) -> io::Result<NamedTempFile> {
        tempfile::Builder::new()
            .prefix(TEMP_PREFIX)
            .tempfile_in(dir)
    }

    fn write(&mut self, mut tmp: NamedTempFile, bytes: &[u8]) -> io::Result<NamedTempFile> {
        tmp.write_all(bytes)?;
        Ok(tmp)
    }

    fn sync_temp(&mut self, tmp: NamedTempFile) -> io::Result<NamedTempFile> {
        tmp.as_file().sync_all()?;
        Ok(tmp)
    }

    fn persist(&mut self, tmp: NamedTempFile, target: &Path) -> io::Result<()> {
        tmp.persist(target).map(|_| ()).map_err(|e| e.error)
    }

    #[cfg(unix)]
    fn sync_dir(&mut self, dir: &Path) -> io::Result<()> {
        std::fs::File::open(dir)?.sync_all()
    }
}

pub fn check(ctx: &AppContext, mode: FetchMode, now_unix: u64) -> FeedCheck {
    check_with(&mut StdCacheIo, ctx, mode, now_unix)
}

/// FR-050 to FR-059: one request chain, `parse`, then the cache written (fresh) or read (any failure); no lock, no
/// retry, no comparison with the cached copy; exactly one summary line.
pub(crate) fn check_with(
    io: &mut dyn CacheIo,
    ctx: &AppContext,
    mode: FetchMode,
    now_unix: u64,
) -> FeedCheck {
    let headers: &[(&str, &str)] = match mode {
        FetchMode::Normal => &[],
        FetchMode::CheckNow => &[("Cache-Control", "no-cache"), ("Pragma", "no-cache")],
    };
    let fetched = http::get(&ctx.http, &ctx.feed_url, headers, MAX_FEED_BYTES)
        .map_err(FeedError::from)
        .and_then(|bytes| parse(&bytes).map(|feed| (bytes, feed)));
    match fetched {
        Ok((bytes, feed)) => {
            let cache_error = write_cache(io, ctx, now_unix, &bytes).err();
            if let Some(e) = &cache_error {
                warn!(target: LOG_TARGET, "{e}");
            }
            info!(
                target: LOG_TARGET,
                "feed ok: {} plugins, schema {}",
                feed.plugins.len(),
                feed.schema_version
            );
            FeedCheck {
                snapshot: Some(FeedSnapshot {
                    feed,
                    fetched_at_unix: now_unix,
                    source: SnapshotSource::Fresh,
                }),
                error: None,
                cache_error,
            }
        }
        Err(error) => {
            let cached = read_cache(io, ctx);
            match &cached {
                Some(s) => warn!(
                    target: LOG_TARGET,
                    "{error}; using cached copy from {}", s.fetched_at_unix
                ),
                None => warn!(target: LOG_TARGET, "{error}; no cached copy"),
            }
            FeedCheck {
                snapshot: cached,
                error: Some(error),
                cache_error: None,
            }
        }
    }
}

/// The D8 file: field order is declaration order (FR-052).
#[derive(Serialize)]
struct CacheFileV1<'a> {
    cache_version: u64,
    fetched_at_unix: u64,
    source_url: &'a str,
    body: &'a str,
}

fn at_step(step: CacheStep) -> impl FnOnce(io::Error) -> FeedError {
    move |source| FeedError::CacheIo {
        step,
        source: Arc::new(source),
    }
}

/// FR-052: create dir → create temp → write → sync temp → persist → (Unix) sync dir. The body is the fetched bytes as
/// text verbatim (`parse` accepted them as UTF-8).
fn write_cache(
    io: &mut dyn CacheIo,
    ctx: &AppContext,
    now_unix: u64,
    body: &[u8],
) -> Result<(), FeedError> {
    let body = String::from_utf8_lossy(body);
    let file = CacheFileV1 {
        cache_version: CACHE_VERSION,
        fetched_at_unix: now_unix,
        source_url: &ctx.feed_url,
        body: &body,
    };
    let mut bytes = serde_json::to_vec_pretty(&file).map_err(|e| FeedError::CacheIo {
        step: CacheStep::Write,
        source: Arc::new(io::Error::other(e)),
    })?;
    bytes.push(b'\n');
    let dir = ctx.roots.state_dir();
    let target = dir.join(FEED_CACHE_FILE);
    io.create_dir(dir).map_err(at_step(CacheStep::CreateDir))?;
    let tmp = io
        .create_temp(dir)
        .map_err(at_step(CacheStep::CreateTemp))?;
    let tmp = io.write(tmp, &bytes).map_err(at_step(CacheStep::Write))?;
    let tmp = io.sync_temp(tmp).map_err(at_step(CacheStep::SyncTemp))?;
    io.persist(tmp, &target)
        .map_err(at_step(CacheStep::Persist))?;
    #[cfg(unix)]
    io.sync_dir(dir).map_err(at_step(CacheStep::SyncDir))?;
    Ok(())
}

/// FR-053: a missing file is no cache; an unreadable or unusable one is no cache with one warn. Never writes.
fn read_cache(io: &mut dyn CacheIo, ctx: &AppContext) -> Option<FeedSnapshot> {
    let path = ctx.roots.state_dir().join(FEED_CACHE_FILE);
    let bytes = match io.read(&path) {
        Ok(Some(bytes)) => bytes,
        Ok(None) => return None,
        Err(e) => {
            warn!(target: LOG_TARGET, "feed cache ignored: it cannot be read: {e}");
            return None;
        }
    };
    match decode_cache(&bytes, &ctx.feed_url) {
        Ok(snapshot) => Some(snapshot),
        Err(why) => {
            warn!(target: LOG_TARGET, "feed cache ignored: {why}");
            None
        }
    }
}

/// The D8 shape: exactly the four keys, `cache_version` 1, this `source_url`, a `body` that `parse` accepts.
fn decode_cache(bytes: &[u8], feed_url: &str) -> Result<FeedSnapshot, String> {
    let doc: Value = serde_json::from_slice(bytes).map_err(|e| format!("it is not JSON: {e}"))?;
    let obj = doc.as_object().ok_or("it is not a JSON object")?;
    if obj.len() != CACHE_KEYS.len() || !CACHE_KEYS.iter().all(|k| obj.contains_key(*k)) {
        return Err(format!("its keys are not exactly {CACHE_KEYS:?}"));
    }
    if obj["cache_version"].as_u64() != Some(CACHE_VERSION) {
        return Err(format!("cache_version is not {CACHE_VERSION}"));
    }
    let fetched_at_unix = obj["fetched_at_unix"]
        .as_u64()
        .ok_or("fetched_at_unix is not an unsigned integer")?;
    if obj["source_url"].as_str() != Some(feed_url) {
        return Err("it was fetched from another feed URL".to_owned());
    }
    let body = obj["body"].as_str().ok_or("body is not a string")?;
    let feed = parse(body.as_bytes()).map_err(|e| format!("its body is refused: {e}"))?;
    Ok(FeedSnapshot {
        feed,
        fetched_at_unix,
        source: SnapshotSource::Cached,
    })
}

#[cfg(test)]
#[path = "feed_tests.rs"]
mod tests;
