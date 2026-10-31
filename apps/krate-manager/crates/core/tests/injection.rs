//! Source-scan guard (spec FR-021, SC-008; plan §4.10, PD-5, PD-22, PD-24): no production root
//! or URL in core, platform literals only from the allow-list, no filesystem token in the pure
//! modules, exactly the seven D6 `disallowed_methods` sites, and core's FR-007 dependency set.
//! Phase 5 (plan §4.11.1): the network, PAC and OS crates confined to their files (FR-073 to
//! FR-075, FR-081), the feed-model seams (FR-026, FR-036), the test-body and FR-086 rules
//! (FR-087, FR-088), the `src-tauri` environment reads (FR-063) and the reader crates (SC-038).
//! Phase 6 (plan §4.12): the Markdown, plist and host-I/O symbols confined (FR-002, FR-082), the
//! test sources (FR-083), the write and listing tokens (FR-084), the two TA-1 test programs
//! (FR-086) and the real-location scan (FR-088).
//! Every test reads files only: nothing is created, nothing is written.

use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::ops::Range;
use std::path::{Path, PathBuf};

/// Raw-text substrings no scanned file outside `platform/` may hold, comments included (FR-021).
const FORBIDDEN_OUTSIDE_PLATFORM: [&str; 7] = [
    "\"C:\\\\",
    "\"/Library",
    "\"/usr/",
    "\"/home",
    "\"/Users",
    "krateaudio.com",
    "api.github.com",
];
/// Identifiers `platform/` and `roots.rs` may not hold in code: they touch no filesystem (FR-021).
const FS_TOKENS: [&str; 14] = [
    "fs",
    "File",
    "OpenOptions",
    "canonicalize",
    "metadata",
    "symlink_metadata",
    "exists",
    "try_exists",
    "is_dir",
    "is_file",
    "read_dir",
    "read_link",
    "create_dir",
    "create_dir_all",
];
/// Every string literal a non-test `platform/` file may hold outside an attribute (PD-5): the
/// six env names of §4.2, the path components, the seven absolute D8 literals, `APP_IDENTIFIER`.
const PLATFORM_LITERALS: [&str; 37] = [
    "LOCALAPPDATA",
    "CommonProgramW6432",
    "ProgramData",
    "USERPROFILE",
    "HOME",
    "XDG_DATA_HOME",
    "Programs",
    "Common",
    "VST3",
    "Krate Audio",
    "Documents",
    "Library",
    "Audio",
    "Plug-Ins",
    "Components",
    "Application Support",
    ".vst3",
    ".local",
    "share",
    "/Library/Audio/Plug-Ins/VST3",
    "/Library/Audio/Plug-Ins/Components",
    "/Library/Application Support/Krate Audio",
    "/usr/lib64/vst3",
    "/usr/lib/vst3",
    "/usr/local/lib64/vst3",
    "/usr/local/lib/vst3",
    "com.krateaudio.krate-manager",
    // Phase 6 FR-033 (PD-4, SI-8: `UNINSTALL_KEY` as its source text; SI-19: the absolute getconf).
    "SOFTWARE\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall",
    "DisplayVersion",
    "InstallLocation",
    "/usr/sbin/pkgutil",
    "--pkg-info",
    "--files",
    "/usr/bin/sw_vers",
    "-productVersion",
    "/usr/bin/getconf",
    "GNU_LIBC_VERSION",
];
/// The plan §4.1 D6 table, per file (app-root-relative); every other scanned file holds 0.
const D6_SITES: [(&str, usize); 6] = [
    ("src-tauri/src/lib.rs", 1),
    ("src-tauri/src/os_roots.rs", 2),
    ("src-tauri/src/proxy_env.rs", 1),
    ("crates/core/src/test_support.rs", 2),
    ("crates/core/src/roots_tests.rs", 1),
    ("crates/core/src/test_support_tests.rs", 1),
];
const D6_TOTAL: usize = 8;
const DISALLOWED_METHODS: &str = "clippy::disallowed_methods";
/// Lint groups that would silence `disallowed_methods` with no `disallowed_methods` token (PD-22).
const GROUP_SUPPRESSIONS: [&str; 3] = ["clippy::style", "clippy::all", "warnings"];
const FIXTURE_LIB: &str = "tests/fixtures/clippy-guard/src/lib.rs";
/// FR-007: the `[dependencies]` keys of `crates/core/Cargo.toml`.
const FR007_DEPENDENCIES: [&str; 11] = [
    "base64",
    "log",
    "pulldown-cmark",
    "regex",
    "rustls",
    "serde",
    "serde_json",
    "tempfile",
    "thiserror",
    "url",
    "ureq",
];

// App-root-relative paths the Phase 5 rules name.
const HTTP_RS: &str = "crates/core/src/http.rs";
const HTTP_TESTS_RS: &str = "crates/core/src/http_tests.rs";
const FIXTURE_RS: &str = "crates/core/src/fixture_server.rs";
const FIXTURE_TESTS_RS: &str = "crates/core/src/fixture_server_tests.rs";
const PAC_RS: &str = "crates/core/src/pac.rs";
const PAC_TESTS_RS: &str = "crates/core/src/pac_tests.rs";
const FEED_RS: &str = "crates/core/src/feed.rs";
const FEED_TESTS_RS: &str = "crates/core/src/feed_tests.rs";
const VERSION_RS: &str = "crates/core/src/version.rs";
const SYSPROXY_RS: &str = "crates/core/src/sysproxy.rs";
const SYSPROXY_TESTS_RS: &str = "crates/core/src/sysproxy_tests.rs";
const SYSPROXY_CF_RS: &str = "crates/core/src/sysproxy_cf.rs";
const SYSPROXY_CF_TESTS_RS: &str = "crates/core/src/sysproxy_cf_tests.rs";
const CONTEXT_TESTS_RS: &str = "crates/core/src/context_tests.rs";
const TEST_SUPPORT_RS: &str = "crates/core/src/test_support.rs";
const LIVE_FEED_RS: &str = "crates/core/tests/live_feed.rs";
const CONFIG_GUARD_RS: &str = "crates/core/tests/config_guard.rs";
const INJECTION_RS: &str = "crates/core/tests/injection.rs";
const OS_PROXY_RS: &str = "src-tauri/src/os_proxy.rs";
const OS_FACTS_RS: &str = "src-tauri/src/os_facts.rs";
const TAURI_LIB_RS: &str = "src-tauri/src/lib.rs";

// App-root-relative paths the Phase 6 rules name.
const CORE_SRC: &str = "crates/core/src/";
const PLATFORM_DIR: &str = "crates/core/src/platform/";
const DETECT_RS: &str = "crates/core/src/detect.rs";
const DETECT_TESTS_RS: &str = "crates/core/src/detect_tests.rs";
const NOTES_RS: &str = "crates/core/src/notes.rs";
const NOTES_TESTS_RS: &str = "crates/core/src/notes_tests.rs";
const STATUS_RS: &str = "crates/core/src/status.rs";
const STATUS_TESTS_RS: &str = "crates/core/src/status_tests.rs";
const WINDOWS_RS: &str = "crates/core/src/platform/windows.rs";
const MACOS_RS: &str = "crates/core/src/platform/macos.rs";
const LINUX_RS: &str = "crates/core/src/platform/linux.rs";
const COMMANDS_RS: &str = "src-tauri/src/commands.rs";

/// FR-082: the host FFI identifiers, only in `os_facts.rs`.
const HOST_FFI: [&str; 4] = [
    "RegOpenKeyExW",
    "RegGetValueW",
    "RegCloseKey",
    "RtlGetVersion",
];
/// FR-082, FR-083 (SI-8, SI-19): each FR-033 host literal, as its value (an escaped `\\` read as
/// one backslash, so the `UNINSTALL_KEY` source text and a raw spelling both match), and the one
/// file whose literals may hold it.
const HOST_LITERALS: [(&str, &str); 5] = [
    ("/usr/sbin/pkgutil", MACOS_RS),
    ("/usr/bin/sw_vers", MACOS_RS),
    ("/usr/bin/getconf", LINUX_RS),
    ("GNU_LIBC_VERSION", LINUX_RS),
    (
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        WINDOWS_RS,
    ),
];
/// FR-002: the vendor folder, spelled only under `platform/` (elsewhere `platform::VENDOR_DIR`).
const VENDOR_LITERAL: &str = "Krate Audio";
/// FR-084: identifiers and paths (token sequences) detection, notes, status and the non-test
/// `platform/` files never hold in code; `read_dir` has its own rule.
const FR084_TOKENS: [&str; 19] = [
    "create_dir",
    "create_dir_all",
    "OpenOptions",
    "rename",
    "remove_file",
    "remove_dir",
    "remove_dir_all",
    "set_permissions",
    "hard_link",
    "symlink",
    "Command",
    "TcpStream",
    "UdpSocket",
    "thread :: spawn",
    "thread :: scope",
    "rayon",
    "fs :: write",
    "fs :: copy",
    "File :: create",
];
/// FR-088 (TA-9): exactly these files (app-root-relative), each of which must be readable.
const FR088_FILES: [&str; 6] = [
    DETECT_TESTS_RS,
    STATUS_TESTS_RS,
    NOTES_TESTS_RS,
    TEST_SUPPORT_RS,
    "src/status.test.ts",
    "src/main.test.ts",
];
/// FR-088: the eleven patterns none of `FR088_FILES` may match.
const FR088_PATTERNS: [&str; 11] = [
    "--test-threads",
    r"127\.0\.0\.1:[0-9]+",
    r"localhost:[0-9]+",
    "OsHostIo",
    "HKEY_",
    "/Library",
    "/usr/",
    "%[A-Z_]+%",
    "AppData",
    "ProgramData",
    "Program Files",
];

/// FR-073 to FR-075, FR-081, R-15, R-16: a token sequence and the only workspace `.rs` files whose
/// code may hold it; an empty list bans it from every file.
const CONFINED_TOKENS: [(&str, &[&str]); 18] = [
    ("ureq", &[HTTP_RS, HTTP_TESTS_RS]),
    ("rustls", &[HTTP_RS, HTTP_TESTS_RS]),
    (
        "TcpStream",
        &[HTTP_RS, FIXTURE_RS, HTTP_TESTS_RS, FIXTURE_TESTS_RS],
    ),
    (
        "TcpListener",
        &[HTTP_RS, FIXTURE_RS, HTTP_TESTS_RS, FIXTURE_TESTS_RS],
    ),
    ("rquickjs", &[PAC_RS, PAC_TESTS_RS]),
    ("UdpSocket", &[HTTP_RS]),
    ("to_socket_addrs", &[HTTP_RS]),
    ("GetTimeZoneInformation", &[]),
    ("WinHttpDetectAutoProxyConfigUrl", &[]),
    ("WinHttpGetProxyForUrl", &[]),
    ("WinHttpGetProxyForUrlEx", &[OS_PROXY_RS]),
    ("WinHttpCreateProxyResolver", &[OS_PROXY_RS]),
    ("WinHttpGetProxyResult", &[OS_PROXY_RS]),
    ("WinHttpFreeProxyResult", &[OS_PROXY_RS]),
    ("WinHttpSetStatusCallback", &[OS_PROXY_RS]),
    ("WinHttpSetOption", &[OS_PROXY_RS]),
    ("windows_sys", &[OS_PROXY_RS, OS_FACTS_RS]),
    ("system_configuration", &[OS_PROXY_RS]),
];
/// FR-075, PD-31: the only files whose code names `core_foundation`.
const CORE_FOUNDATION_FILES: [&str; 2] = [SYSPROXY_CF_RS, SYSPROXY_CF_TESTS_RS];
/// FR-081, FR-073, FR-075: a dependency key and the only manifests that may declare it.
const CONFINED_TOML_KEYS: [(&str, &[&str]); 9] = [
    ("ureq", &["Cargo.toml", "crates/core/Cargo.toml"]),
    ("rustls", &["Cargo.toml", "crates/core/Cargo.toml"]),
    ("rquickjs", &["Cargo.toml", "crates/core/Cargo.toml"]),
    ("core-foundation", &["Cargo.toml", "crates/core/Cargo.toml"]),
    ("windows-sys", &["Cargo.toml", "src-tauri/Cargo.toml"]),
    (
        "system-configuration",
        &["Cargo.toml", "src-tauri/Cargo.toml"],
    ),
    ("pulldown-cmark", &["Cargo.toml", "crates/core/Cargo.toml"]),
    ("plist", &["Cargo.toml", "crates/core/Cargo.toml"]),
    (
        "tauri-plugin-opener",
        &["Cargo.toml", "src-tauri/Cargo.toml"],
    ),
];
/// SC-026: token sequences `http.rs` never holds (one agent, one trust store).
const SECOND_AGENT_CALLS: [&str; 6] = [
    "new_with_defaults",
    "Agent :: new",
    "ureq :: get",
    "ureq :: post",
    "ureq :: agent",
    "ureq :: run",
];
/// FR-026, PD-21, PD-22: the feed-model newtypes with no public constructor.
const NEWTYPES: [&str; 4] = ["PlainName", "AssetUrl", "PageUrl", "AssetName"];
/// Traits whose impl on a newtype would be a public constructor.
const CONSTRUCTOR_TRAITS: [&str; 5] = ["From", "TryFrom", "FromStr", "Default", "Deserialize"];

/// FR-087, SI-10, SI-26: the test bodies (file, `fn` name) that may build the production config.
const PRODUCTION_BODIES: [(&str, &str); 9] = [
    (HTTP_TESTS_RS, "https_only_refuses_before_connecting"),
    (HTTP_TESTS_RS, "config_for_dev_url_cases"),
    (HTTP_TESTS_RS, "url_policy_cases"),
    (HTTP_TESTS_RS, "next_hop_cases"),
    (HTTP_TESTS_RS, "agent_trusts_the_os_store"),
    (HTTP_TESTS_RS, "proxy_scheme_cases"),
    (HTTP_TESTS_RS, "production_values"),
    (HTTP_TESTS_RS, "pac_policy_cases"),
    (PAC_TESTS_RS, "wpad_candidate_cases"),
];
/// FR-087: the test bodies that may carry a static proxy.
const PROXY_BODIES: [(&str, &str); 5] = [
    (HTTP_TESTS_RS, "proxy_path_connect"),
    (HTTP_TESTS_RS, "proxy_path_http"),
    (HTTP_TESTS_RS, "no_proxy_bypass"),
    (HTTP_TESTS_RS, "loopback_proxy_only"),
    (FEED_TESTS_RS, "check_through_proxy_writes_cache"),
];
/// FR-087, SI-26, R-15: the PAC-path and OS-path test bodies.
const PAC_BODIES: [(&str, &str); 12] = [
    (HTTP_TESTS_RS, "pac_path_proxy"),
    (HTTP_TESTS_RS, "pac_path_direct"),
    (HTTP_TESTS_RS, "pac_list_walk"),
    (HTTP_TESTS_RS, "pac_per_hop"),
    (HTTP_TESTS_RS, "pac_fetched_once_never_via_proxy"),
    (HTTP_TESTS_RS, "pac_policy_cases"),
    (HTTP_TESTS_RS, "wpad_discovery_walk"),
    (HTTP_TESTS_RS, "pac_fallback_to_static_proxy"),
    (HTTP_TESTS_RS, "pac_body_cap"),
    (HTTP_TESTS_RS, "os_pac_walk"),
    (HTTP_TESTS_RS, "os_pac_failure_cases"),
    (FEED_TESTS_RS, "check_survives_pac_failure"),
];
/// A token sequence, the bodies (file, `fn` name) it may sit in, and whether all of
/// `tests/live_feed.rs` may hold it.
type BodyRule = (&'static str, &'static [(&'static str, &'static str)], bool);
const BODY_RULES: [BodyRule; 5] = [
    ("HttpConfig :: production (", &PRODUCTION_BODIES, true),
    ("UrlPolicy :: HttpsOnly", &PRODUCTION_BODIES, true),
    (". with_proxy (", &PROXY_BODIES, false),
    (". with_pac (", &PAC_BODIES, false),
    (". with_pac_host (", &PAC_BODIES, false),
];
/// FR-087, TA-12: the real OS reader, resolver, local address, discovery and zone: in no test.
const TEST_BANNED: [&str; 6] = [
    ". system_proxy (",
    "OsPacHost",
    "std_resolve (",
    "std_my_ip (",
    "pac_host (",
    "system_utc_offset (",
];
/// The only programs a `crates/core` test may start anywhere.
const TEST_PROGRAMS: [&str; 2] = ["sleep", "printf"];
/// TA-1 (FR-086): a program exactly one test body may start besides `TEST_PROGRAMS`, as (file,
/// enclosing `fn`, bare program name resolved through `PATH`).
const TEST_PROGRAM_BODIES: [(&str, &str, &str); 2] = [
    (DETECT_TESTS_RS, "probe_metadata_error_unix", "mkfifo"),
    (DETECT_TESTS_RS, "listing_skips_junction_windows", "cmd"),
];

/// FR-063, SC-039 rule 1: environment reads, only in `os_roots.rs` and `proxy_env.rs`.
const ENV_READS: [&str; 6] = [
    "env :: var",
    "env :: var_os",
    "env :: vars",
    "env :: vars_os",
    "var_os",
    "vars_os",
];
const ENV_READ_FILES: [&str; 2] = ["os_roots.rs", "proxy_env.rs"];
/// SC-039 rule 2: the Linux reader's variables, named in `src-tauri/src` only by `proxy_env.rs`.
const DESKTOP_VARS: [&str; 3] = ["XDG_CURRENT_DESKTOP", "XDG_CONFIG_HOME", "HOME"];
/// SC-039 rule 3 (SI-3, SI-36): every literal `proxy_env.rs` may hold outside an attribute.
const PROXY_ENV_LITERALS: [&str; 9] = [
    "HTTPS_PROXY",
    "https_proxy",
    "ALL_PROXY",
    "all_proxy",
    "NO_PROXY",
    "no_proxy",
    "XDG_CURRENT_DESKTOP",
    "XDG_CONFIG_HOME",
    "HOME",
];

const WORKSPACE_DEPS: &str = "[workspace.dependencies]";
const NOT_WINDOWS_DEPS: &str = "[target.'cfg(not(windows))'.dependencies]";
const WINDOWS_DEPS: &str = "[target.'cfg(windows)'.dependencies]";
const MACOS_DEPS: &str = "[target.'cfg(target_os = \"macos\")'.dependencies]";

/// SC-038: one reader crate's root pin and its only member declaration.
struct ReaderPin {
    name: &'static str,
    version: &'static str,
    /// `Some(list)`: exactly these features (an empty list: no `features` key); `None`: unchecked.
    features: Option<&'static [&'static str]>,
    home: (&'static str, &'static str),
    /// The home table holds this crate and nothing else.
    alone: bool,
}
const READER_PINS: [ReaderPin; 4] = [
    ReaderPin {
        name: "ureq",
        version: "3.5.0",
        features: Some(&["platform-verifier", "socks-proxy"]),
        home: ("crates/core/Cargo.toml", "[dependencies]"),
        alone: false,
    },
    ReaderPin {
        name: "windows-sys",
        version: "0.61.2",
        features: Some(&[
            "Win32_Foundation",
            "Win32_System_Registry",
            "Win32_System_SystemInformation",
            "Wdk_System_SystemServices",
            "Win32_Networking_WinHttp",
        ]),
        home: ("src-tauri/Cargo.toml", WINDOWS_DEPS),
        alone: true,
    },
    ReaderPin {
        name: "system-configuration",
        version: "0.8.0",
        features: None,
        home: ("src-tauri/Cargo.toml", MACOS_DEPS),
        alone: true,
    },
    ReaderPin {
        name: "rquickjs",
        version: "0.14.0",
        features: Some(&[]),
        home: ("crates/core/Cargo.toml", NOT_WINDOWS_DEPS),
        alone: true,
    },
];

const RULE_INLINE_TESTS: &str = "inline-test-module";
const RULE_SUBSTRING: &str = "forbidden-substring";
const RULE_PLATFORM_LITERAL: &str = "platform-literal-not-allow-listed";
const RULE_PLATFORM_RAW: &str = "platform-raw-literal";
const RULE_FS_TOKEN: &str = "filesystem-token";
const RULE_SANDBOXED: &str = "sandboxed-outside-roots";
const RULE_CONFINED: &str = "token-outside-its-files";
const RULE_BANNED: &str = "banned-token";
const RULE_PUB_SIGNATURE: &str = "pub-signature-names-crate";
const RULE_ONE_AGENT: &str = "one-agent";
const RULE_DROP_CONNECT: &str = "fixture-connect-outside-drop";
const RULE_TOML_KEY: &str = "dependency-outside-its-manifests";
const RULE_SEAM: &str = "feed-model-seam";
const RULE_NATIVE: &str = "native-call-site";
const RULE_TEST_BODY: &str = "outside-allowed-test-bodies";
const RULE_TEST_BANNED: &str = "banned-in-test-sources";
const RULE_FAKE_HOST: &str = "pac-host-handle-outside-fake";
const RULE_COMMAND: &str = "test-program";
const RULE_GSETTINGS: &str = "gsettings-in-test-source";
const RULE_SET_LOGGER: &str = "set-logger-outside-test-support";
const RULE_STATIC_LOCK: &str = "static-lock";
const RULE_LISTENER: &str = "listener-bind-outside-fixture";
const RULE_FIXED_PORT: &str = "fixed-port";
const RULE_ENV_READ: &str = "env-read";
const RULE_DESKTOP_VAR: &str = "desktop-variable-literal";
const RULE_PROXY_ENV_LITERAL: &str = "proxy-env-literal";
const RULE_PLIST_SITE: &str = "plist-outside-the-macos-au-reader";
const RULE_HOST_LITERAL: &str = "host-literal-outside-its-platform-file";
const RULE_VENDOR_LITERAL: &str = "vendor-literal-outside-platform";
const RULE_TEST_HOST_IO: &str = "host-io-in-test-source";
const RULE_WRITE_TOKEN: &str = "write-or-listing-token";
const RULE_READ_DIR: &str = "read-dir-site";

#[derive(Clone, Debug, PartialEq, Eq)]
struct Finding {
    file: String,
    rule: &'static str,
    detail: String,
}

/// A string literal: its source text between the delimiters, whether it is raw, whether it sits
/// inside an attribute, and the byte offset of its first character (prefix or quote).
#[derive(Clone, Debug, PartialEq, Eq)]
struct Literal {
    start: usize,
    text: String,
    raw: bool,
    in_attribute: bool,
}

/// The code text with every comment and literal blanked (same byte length, newlines kept) and
/// the string literals found, each marked `in_attribute` (PD-5).
fn strip_comments_and_literals(text: &str) -> (String, Vec<Literal>) {
    let bytes = text.as_bytes();
    let mut code: Vec<u8> = Vec::with_capacity(bytes.len());
    let mut literals = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        let b = bytes[i];
        let next = bytes.get(i + 1).copied();
        // A prefix letter starts a literal only at the start of a token, never inside `bar"`.
        let token_start = i == 0 || !is_ident_byte(bytes[i - 1]);
        let end = if b == b'/' && next == Some(b'/') {
            Some(line_end(bytes, i))
        } else if b == b'/' && next == Some(b'*') {
            Some(block_comment_end(bytes, i))
        } else if b == b'"' || (token_start && matches!(b, b'b' | b'c' | b'r')) {
            string_literal(bytes, i).map(|(end, content, raw)| {
                literals.push(Literal {
                    start: i,
                    text: text[content].to_owned(),
                    raw,
                    in_attribute: false,
                });
                end
            })
        } else {
            None
        };
        let end = end.or_else(|| {
            (b == b'\'' || (token_start && b == b'b'))
                .then(|| char_literal(bytes, i))
                .flatten()
        });
        match end {
            Some(end) => {
                code.extend(
                    bytes[i..end]
                        .iter()
                        .map(|&c| if c == b'\n' { c } else { b' ' }),
                );
                i = end;
            }
            None => {
                code.push(b);
                i += 1;
            }
        }
    }
    let code = String::from_utf8(code).expect("blanking replaces whole characters");
    let spans = attribute_spans(&code);
    for literal in &mut literals {
        literal.in_attribute = spans.iter().any(|s| s.contains(&literal.start));
    }
    (code, literals)
}

fn is_ident_byte(b: u8) -> bool {
    b == b'_' || b.is_ascii_alphanumeric() || !b.is_ascii()
}

fn skip_ws(bytes: &[u8], mut i: usize) -> usize {
    while bytes.get(i).is_some_and(|b| b.is_ascii_whitespace()) {
        i += 1;
    }
    i
}

/// The end (exclusive) of the `//` comment at `i`: the next `\n`, which stays code.
fn line_end(bytes: &[u8], i: usize) -> usize {
    bytes[i..]
        .iter()
        .position(|&b| b == b'\n')
        .map_or(bytes.len(), |p| i + p)
}

/// The end (exclusive) of the `/* */` comment at `i`, nested comments counted.
fn block_comment_end(bytes: &[u8], i: usize) -> usize {
    let mut depth = 0usize;
    let mut m = i;
    while m < bytes.len() {
        if bytes[m..].starts_with(b"/*") {
            depth += 1;
            m += 2;
        } else if bytes[m..].starts_with(b"*/") {
            depth -= 1;
            m += 2;
            if depth == 0 {
                return m;
            }
        } else {
            m += 1;
        }
    }
    bytes.len()
}

/// A string literal at `i` (`"…"`, `b"…"`, `c"…"`, `r"…"`, `r#"…"#`, `br…`, `cr…`): its end
/// (exclusive), the byte range between its delimiters, and whether it is raw.
fn string_literal(bytes: &[u8], i: usize) -> Option<(usize, Range<usize>, bool)> {
    let mut j = i;
    if matches!(bytes[j], b'b' | b'c') {
        j += 1;
    }
    if bytes.get(j) == Some(&b'r') {
        let mut k = j + 1;
        while bytes.get(k) == Some(&b'#') {
            k += 1;
        }
        if bytes.get(k) != Some(&b'"') {
            return None; // an identifier such as `rate`, or a raw identifier `r#type`
        }
        let hashes = k - j - 1;
        let start = k + 1;
        let mut m = start;
        while m < bytes.len() {
            let close = m + 1 + hashes;
            if bytes[m] == b'"'
                && close <= bytes.len()
                && bytes[m + 1..close].iter().all(|&h| h == b'#')
            {
                return Some((close, start..m, true));
            }
            m += 1;
        }
        return Some((bytes.len(), start..bytes.len(), true));
    }
    if bytes.get(j) != Some(&b'"') {
        return None;
    }
    let start = j + 1;
    let mut m = start;
    while m < bytes.len() {
        match bytes[m] {
            b'\\' => m += 2,
            b'"' => return Some((m + 1, start..m, false)),
            _ => m += 1,
        }
    }
    Some((bytes.len(), start..bytes.len(), false))
}

/// A char literal at `i` (`'x'`, `'\''`, `'\u{…}'`, `b'x'`): its end (exclusive). `None` for a
/// lifetime or a label (`'a`, `'static`).
fn char_literal(bytes: &[u8], i: usize) -> Option<usize> {
    let quote = if bytes[i] == b'b' { i + 1 } else { i };
    if bytes.get(quote) != Some(&b'\'') {
        return None;
    }
    let lead = *bytes.get(quote + 1)?;
    if lead == b'\\' {
        let close = bytes.get(quote + 3..)?.iter().position(|&b| b == b'\'')?;
        return Some(quote + 3 + close + 1);
    }
    let len = match lead {
        0x00..=0x7f => 1,
        0xc0..=0xdf => 2,
        0xe0..=0xef => 3,
        _ => 4,
    };
    (bytes.get(quote + 1 + len) == Some(&b'\'')).then_some(quote + 2 + len)
}

/// The byte ranges of every attribute in the code text: `#`, an optional `!`, then `[` up to
/// the matching `]`. Literals and comments are already blanked, so every bracket counted is code.
fn attribute_spans(code: &str) -> Vec<Range<usize>> {
    let bytes = code.as_bytes();
    let mut spans = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'#' {
            let mut j = skip_ws(bytes, i + 1);
            if bytes.get(j) == Some(&b'!') {
                j = skip_ws(bytes, j + 1);
            }
            if bytes.get(j) == Some(&b'[') {
                let end = matching_close(bytes, j, b'[', b']');
                spans.push(i..end);
                i = end;
                continue;
            }
        }
        i += 1;
    }
    spans
}

/// The end (exclusive) of the bracketed group opening at `open`.
fn matching_close(bytes: &[u8], open: usize, opening: u8, closing: u8) -> usize {
    let mut depth = 0usize;
    for (m, &b) in bytes.iter().enumerate().skip(open) {
        if b == opening {
            depth += 1;
        } else if b == closing {
            depth -= 1;
            if depth == 0 {
                return m + 1;
            }
        }
    }
    bytes.len()
}

/// Identifiers and single punctuation characters of the code text, whitespace dropped.
fn tokens(code: &str) -> Vec<&str> {
    spanned_tokens(code).into_iter().map(|(_, t)| t).collect()
}

/// The tokens of `tokens` with the byte offset each starts at.
fn spanned_tokens(code: &str) -> Vec<(usize, &str)> {
    let mut out = Vec::new();
    let mut word: Option<usize> = None;
    for (i, c) in code.char_indices() {
        if c.is_ascii_alphanumeric() || c == '_' {
            if word.is_none() {
                word = Some(i);
            }
            continue;
        }
        if let Some(start) = word.take() {
            out.push((start, &code[start..i]));
        }
        if !c.is_whitespace() {
            out.push((i, &code[i..i + c.len_utf8()]));
        }
    }
    if let Some(start) = word {
        out.push((start, &code[start..]));
    }
    out
}

/// One source file after blanking: its app-root-relative path, code text and literals.
struct Lexed {
    rel: String,
    code: String,
    literals: Vec<Literal>,
}

fn lex(files: &[(String, String)]) -> Vec<Lexed> {
    files
        .iter()
        .map(|(rel, text)| {
            let (code, literals) = strip_comments_and_literals(text);
            Lexed {
                rel: rel.clone(),
                code,
                literals,
            }
        })
        .collect()
}

/// Every index where the token sequence `needle` starts in `toks`.
fn seq_at(toks: &[&str], needle: &[&str]) -> Vec<usize> {
    if needle.is_empty() || toks.len() < needle.len() {
        return Vec::new();
    }
    toks.windows(needle.len())
        .enumerate()
        .filter(|(_, w)| *w == needle)
        .map(|(i, _)| i)
        .collect()
}

/// Each file's path and code tokens, tokenized once for every rule of a scan.
fn tokenized(lexed: &[Lexed]) -> Vec<(&str, Vec<&str>)> {
    lexed
        .iter()
        .map(|l| (l.rel.as_str(), tokens(&l.code)))
        .collect()
}

/// The one token-confinement scan (FR-075, FR-081, FR-086): every file outside `allowed` whose
/// code holds the token sequence `needle` (written space-separated, `TcpStream :: connect`).
fn confinement(
    files: &[(&str, Vec<&str>)],
    needle: &str,
    allowed: &[&str],
    rule: &'static str,
) -> Vec<Finding> {
    let want = tokens(needle);
    files
        .iter()
        .filter(|(rel, _)| !allowed.contains(rel))
        .filter_map(|(rel, toks)| {
            let n = seq_at(toks, &want).len();
            (n > 0).then(|| Finding {
                file: (*rel).to_owned(),
                rule,
                detail: format!("`{needle}` {n} time(s)"),
            })
        })
        .collect()
}

/// The index of the token closing the group `toks[open]` opens (`(`, `[` or `{`); `toks.len()`
/// when it is unbalanced.
fn close_of(toks: &[&str], open: usize) -> usize {
    let (o, c) = match toks[open] {
        "(" => ("(", ")"),
        "[" => ("[", "]"),
        _ => ("{", "}"),
    };
    let mut depth = 0usize;
    for (k, &t) in toks.iter().enumerate().skip(open) {
        if t == o {
            depth += 1;
        } else if t == c {
            depth -= 1;
            if depth == 0 {
                return k;
            }
        }
    }
    toks.len()
}

/// The index of the `>` closing the generics `toks[open]` (`<`) opens; a `->` arrow is not one.
fn angle_close(toks: &[&str], open: usize) -> usize {
    let mut depth = 0usize;
    for k in open..toks.len() {
        match toks[k] {
            "<" => depth += 1,
            ">" if k == 0 || toks[k - 1] != "-" => {
                depth -= 1;
                if depth == 0 {
                    return k;
                }
            }
            _ => {}
        }
    }
    toks.len()
}

fn is_ident(t: &str) -> bool {
    t.starts_with(|c: char| c.is_ascii_alphabetic() || c == '_')
}

/// One `fn` item: its name, the index of `fn`, its parameter and return-type token ranges, and
/// its body (`{` to `}` inclusive) when it has one.
struct FnItem<'a> {
    name: &'a str,
    start: usize,
    params: Range<usize>,
    ret: Range<usize>,
    body: Option<Range<usize>>,
}

fn fn_items<'a>(toks: &[&'a str]) -> Vec<FnItem<'a>> {
    let mut out = Vec::new();
    for i in 0..toks.len() {
        if toks[i] != "fn" || !toks.get(i + 1).is_some_and(|t| is_ident(t)) {
            continue; // `fn(u8) -> u8` is a pointer type, not an item
        }
        let mut p = i + 2;
        if toks.get(p) == Some(&"<") {
            p = angle_close(toks, p) + 1;
        }
        if toks.get(p) != Some(&"(") {
            continue;
        }
        let pc = close_of(toks, p);
        let mut ret_start = pc + 1;
        if toks.get(ret_start) == Some(&"-") && toks.get(ret_start + 1) == Some(&">") {
            ret_start += 2;
        }
        let mut depth = 0i32;
        let mut ret_end = toks.len();
        let mut body = None;
        for (m, &t) in toks.iter().enumerate().skip(ret_start) {
            match t {
                "(" | "[" => depth += 1,
                ")" | "]" => depth -= 1,
                "where" if depth == 0 => ret_end = ret_end.min(m),
                "{" if depth == 0 => {
                    ret_end = ret_end.min(m);
                    body = Some(m..close_of(toks, m) + 1);
                    break;
                }
                ";" if depth == 0 => {
                    ret_end = ret_end.min(m);
                    break;
                }
                _ => {}
            }
        }
        let ret_start = ret_start.min(ret_end);
        out.push(FnItem {
            name: toks[i + 1],
            start: i,
            params: p + 1..pc.max(p + 1),
            ret: ret_start..ret_end,
            body,
        });
    }
    out
}

/// The name of the outermost `fn` whose body holds token `idx`.
fn enclosing_fn<'a>(fns: &[FnItem<'a>], idx: usize) -> Option<&'a str> {
    fns.iter()
        .filter(|f| f.body.as_ref().is_some_and(|b| b.contains(&idx)))
        .min_by_key(|f| f.start)
        .map(|f| f.name)
}

/// One `impl` block: the trait's last path segment (`None` for an inherent impl), the trait's
/// token range, the self type's last path segment, and the body (`{` to `}` inclusive).
struct ImplBlock<'a> {
    trait_name: Option<&'a str>,
    trait_toks: Option<Range<usize>>,
    self_name: &'a str,
    body: Range<usize>,
}

/// The last identifier at generic depth 0 in `toks[range]` (a path's last segment), lifetimes,
/// `dyn` and `mut` skipped, stopping at `where`.
fn last_segment<'a>(toks: &[&'a str], range: Range<usize>) -> Option<&'a str> {
    let mut depth = 0usize;
    let mut last = None;
    for k in range {
        let t = toks[k];
        match t {
            "<" => depth += 1,
            ">" if toks[k - 1] != "-" => depth = depth.saturating_sub(1),
            "where" if depth == 0 => break,
            _ if depth == 0
                && is_ident(t)
                && !["dyn", "mut"].contains(&t)
                && toks[k - 1] != "'" =>
            {
                last = Some(t);
            }
            _ => {}
        }
    }
    last
}

fn impl_blocks<'a>(toks: &[&'a str]) -> Vec<ImplBlock<'a>> {
    let mut out = Vec::new();
    for i in 0..toks.len() {
        // An item `impl`, not an `impl Trait` type (`-> impl Fn()`, `x: impl Read`).
        let item = i == 0 || ["}", ";", "]", "{", "unsafe"].contains(&toks[i - 1]);
        if toks[i] != "impl" || !item {
            continue;
        }
        let mut j = i + 1;
        if toks.get(j) == Some(&"<") {
            j = angle_close(toks, j) + 1;
        }
        let Some(open) = (j..toks.len()).find(|&k| toks[k] == "{" || toks[k] == ";") else {
            continue;
        };
        if toks[open] == ";" {
            continue;
        }
        let for_at = (j..open).find(|&k| toks[k] == "for" && toks.get(k + 1) != Some(&"<"));
        let (trait_toks, self_toks) = match for_at {
            Some(f) => (Some(j..f), f + 1..open),
            None => (None, j..open),
        };
        let Some(self_name) = last_segment(toks, self_toks) else {
            continue;
        };
        out.push(ImplBlock {
            trait_name: trait_toks.clone().and_then(|r| last_segment(toks, r)),
            trait_toks,
            self_name,
            body: open..close_of(toks, open) + 1,
        });
    }
    out
}

/// The token range of every `use` declaration (`use` to its `;`).
fn use_ranges(toks: &[&str]) -> Vec<Range<usize>> {
    toks.iter()
        .enumerate()
        .filter(|(_, t)| **t == "use")
        .map(|(i, _)| {
            let end = (i..toks.len())
                .find(|&k| toks[k] == ";")
                .unwrap_or(toks.len());
            i..end
        })
        .collect()
}

/// The token range of every `pub` item's signature (`pub(crate)` / `pub(super)` items skipped):
/// a `fn`, `use`, `type`, `const`, `struct` or `mod` up to its first `{` or `;`; an `enum` or
/// `trait` with its body (its variants and methods are public); a `pub` field up to its `,`.
fn pub_signatures(toks: &[&str]) -> Vec<Range<usize>> {
    let mut out = Vec::new();
    for i in 0..toks.len() {
        if toks[i] != "pub" || toks.get(i + 1) == Some(&"(") {
            continue;
        }
        let field = toks.get(i + 1).is_some_and(|t| is_ident(t))
            && toks.get(i + 2) == Some(&":")
            && toks.get(i + 3) != Some(&":");
        let whole_body = toks[i + 1..]
            .iter()
            .take_while(|t| ["unsafe", "auto", "enum", "trait"].contains(t))
            .any(|t| *t == "enum" || *t == "trait");
        let mut depth = 0i32;
        let mut end = toks.len();
        for (m, &t) in toks.iter().enumerate().skip(i + 1) {
            match t {
                "(" | "[" => depth += 1,
                "<" if field => depth += 1,
                ">" if field && toks[m - 1] != "-" => depth -= 1,
                ")" | "]" | "}" if depth == 0 => {
                    end = m;
                    break;
                }
                ")" | "]" => depth -= 1,
                "," if field && depth == 0 => {
                    end = m;
                    break;
                }
                "{" if depth == 0 => {
                    end = if whole_body { close_of(toks, m) + 1 } else { m };
                    break;
                }
                ";" if depth == 0 => {
                    end = m;
                    break;
                }
                _ => {}
            }
        }
        out.push(i..end);
    }
    out
}

/// The outer attributes (`#[…]`) directly before token `decl`, as their inner token ranges.
fn outer_attributes(toks: &[&str], decl: usize) -> Vec<Range<usize>> {
    let mut out = Vec::new();
    let mut end = decl;
    while end > 1 && toks[end - 1] == "]" {
        let mut depth = 0usize;
        let mut k = end - 1;
        loop {
            match toks[k] {
                "]" => depth += 1,
                "[" => {
                    depth -= 1;
                    if depth == 0 {
                        break;
                    }
                }
                _ => {}
            }
            if k == 0 {
                return out;
            }
            k -= 1;
        }
        if k == 0 || toks[k - 1] != "#" {
            break;
        }
        out.push(k + 1..end - 1);
        end = k - 1;
    }
    out
}

/// Every `struct` / `enum` item: its name and the token range after the name (its fields).
fn item_bodies<'a>(toks: &[&'a str]) -> Vec<(&'a str, Range<usize>)> {
    let mut out = Vec::new();
    for i in 0..toks.len() {
        if !["struct", "enum"].contains(&toks[i]) || !toks.get(i + 1).is_some_and(|t| is_ident(t)) {
            continue;
        }
        let Some(open) = (i + 2..toks.len()).find(|&k| ["{", "(", ";"].contains(&toks[k])) else {
            continue;
        };
        let end = if toks[open] == ";" {
            open
        } else {
            close_of(toks, open) + 1
        };
        out.push((toks[i + 1], i + 2..end));
    }
    out
}

/// Whether the `fn` at token `at` is plain `pub` (not `pub(crate)` / `pub(super)` or private).
fn is_plain_pub(toks: &[&str], at: usize) -> bool {
    let mut k = at;
    while k > 0 && ["const", "async", "unsafe", "extern"].contains(&toks[k - 1]) {
        k -= 1;
    }
    k > 0 && toks[k - 1] == "pub"
}

/// Whether an integer token is zero: `0`, `0_0`, `0u16` (a `0x…` / `0o…` / `0b…` prefix is not
/// a type suffix, so `0x1F90` is not zero).
fn is_zero_integer(t: &str) -> bool {
    let run = t
        .find(|c: char| !(c.is_ascii_digit() || c == '_'))
        .unwrap_or(t.len());
    let (digits, suffix) = t.split_at(run);
    digits.chars().all(|c| c == '0' || c == '_')
        && (suffix.is_empty() || suffix.starts_with(['u', 'i']))
}

/// The literal starting between byte offsets `from` and `to` of a file's code text, if any.
fn literal_between(literals: &[Literal], from: usize, to: usize) -> Option<&Literal> {
    literals.iter().find(|l| l.start > from && l.start < to)
}

/// The FR-021 rules for one file under `crates/core/src` (`rel` is `/`-separated, relative to
/// `src`). `test_support.rs` and `*_tests.rs` are not scanned.
fn scan_core_file(rel: &str, text: &str) -> Vec<Finding> {
    let mut out = Vec::new();
    if !is_scanned(rel) {
        return out;
    }
    let (code, literals) = strip_comments_and_literals(text);
    let toks = tokens(&code);
    let in_platform = rel.starts_with("platform/");
    let mut report = |rule: &'static str, detail: String| {
        out.push(Finding {
            file: rel.to_owned(),
            rule,
            detail,
        });
    };
    if toks.windows(3).any(|w| w == ["mod", "tests", "{"]) {
        report(
            RULE_INLINE_TESTS,
            "`mod tests {`; use <stem>_tests.rs".to_owned(),
        );
    }
    if in_platform {
        for literal in literals.iter().filter(|l| !l.in_attribute) {
            if literal.raw {
                report(RULE_PLATFORM_RAW, format!("r\"{}\"", literal.text));
            } else if !PLATFORM_LITERALS.contains(&literal.text.as_str()) {
                report(RULE_PLATFORM_LITERAL, format!("\"{}\"", literal.text));
            }
        }
    } else {
        for needle in FORBIDDEN_OUTSIDE_PLATFORM {
            if text.contains(needle) {
                report(RULE_SUBSTRING, needle.to_owned());
            }
        }
    }
    if in_platform || rel == "roots.rs" {
        for token in FS_TOKENS {
            if toks.contains(&token) {
                report(RULE_FS_TOKEN, token.to_owned());
            }
        }
    }
    if rel != "roots.rs" && toks.contains(&"sandboxed") {
        report(RULE_SANDBOXED, "sandboxed".to_owned());
    }
    out
}

/// The number of `clippy::disallowed_methods` entries in `allow(…)` / `expect(…)` lint-level
/// attributes (also inside `cfg_attr(…)`), and every lint-group suppression found (PD-22).
fn lint_suppressions(text: &str) -> (usize, Vec<String>) {
    let (code, _) = strip_comments_and_literals(text);
    let mut count = 0;
    let mut groups = Vec::new();
    for span in attribute_spans(&code) {
        for lint in suppressed_lints(&code[span]) {
            if lint == DISALLOWED_METHODS {
                count += 1;
            } else if GROUP_SUPPRESSIONS.contains(&lint.as_str()) {
                groups.push(lint);
            }
        }
    }
    (count, groups)
}

/// The lint names of every `allow(…)` / `expect(…)` in one attribute's code text, whitespace
/// removed (`clippy :: all` is `clippy::all`); the `reason = …` entry is skipped.
fn suppressed_lints(attribute: &str) -> Vec<String> {
    let bytes = attribute.as_bytes();
    let mut lints = Vec::new();
    for keyword in ["allow", "expect"] {
        for (at, _) in attribute.match_indices(keyword) {
            let after = at + keyword.len();
            let open = skip_ws(bytes, after);
            let whole_word = (at == 0 || !is_ident_byte(bytes[at - 1]))
                && !bytes.get(after).copied().is_some_and(is_ident_byte);
            if !whole_word || bytes.get(open) != Some(&b'(') {
                continue;
            }
            let close = matching_close(bytes, open, b'(', b')');
            let inner = &attribute[open + 1..close.saturating_sub(1).max(open + 1)];
            for item in split_top_level(inner) {
                let lint: String = item.chars().filter(|c| !c.is_whitespace()).collect();
                if !lint.is_empty() && !lint.starts_with("reason=") {
                    lints.push(lint);
                }
            }
        }
    }
    lints
}

/// `text` split at the commas outside any parentheses.
fn split_top_level(text: &str) -> Vec<&str> {
    let mut items = Vec::new();
    let mut depth = 0usize;
    let mut start = 0;
    for (i, c) in text.char_indices() {
        match c {
            '(' => depth += 1,
            ')' => depth = depth.saturating_sub(1),
            ',' if depth == 0 => {
                items.push(&text[start..i]);
                start = i + 1;
            }
            _ => {}
        }
    }
    items.push(&text[start..]);
    items
}

fn is_scanned(rel: &str) -> bool {
    rel != "test_support.rs" && !rel.ends_with("_tests.rs")
}

/// Per-file D6 count against the plan §4.1 table, the total, and every group suppression.
fn d6_problems(files: &[(String, String)]) -> Vec<String> {
    let expected: BTreeMap<&str, usize> = D6_SITES.into_iter().collect();
    let mut problems = Vec::new();
    let mut total = 0;
    for (rel, text) in files {
        let (count, groups) = lint_suppressions(text);
        total += count;
        let want = expected.get(rel.as_str()).copied().unwrap_or(0);
        if count != want {
            problems.push(format!(
                "{rel}: {count} {DISALLOWED_METHODS} suppressions, expected {want}"
            ));
        }
        for group in groups {
            problems.push(format!("{rel}: lint-level attribute names {group}"));
        }
    }
    for (rel, _) in D6_SITES {
        if !files.iter().any(|(r, _)| r == rel) {
            problems.push(format!("{rel}: D6 site file not found"));
        }
    }
    if total != D6_TOTAL {
        problems.push(format!(
            "{total} {DISALLOWED_METHODS} suppressions in all, expected {D6_TOTAL}"
        ));
    }
    problems
}

fn show(findings: &[Finding]) -> String {
    findings
        .iter()
        .map(|f| format!("{}: {}: {}", f.file, f.rule, f.detail))
        .collect::<Vec<_>>()
        .join("\n")
}

fn app_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("..").join("..")
}

fn read_text(path: &Path) -> String {
    fs::read_to_string(path).unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()))
}

/// Every `*.rs` file under `dir`, recursively, as (`prefix` + `/`-separated relative path, text),
/// sorted by path.
fn rs_files(dir: &Path, prefix: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    collect_rs(dir, prefix, &mut out);
    out.sort();
    out
}

fn collect_rs(dir: &Path, prefix: &str, out: &mut Vec<(String, String)>) {
    let entries =
        fs::read_dir(dir).unwrap_or_else(|e| panic!("cannot list {}: {e}", dir.display()));
    for entry in entries {
        let entry = entry.unwrap_or_else(|e| panic!("cannot list {}: {e}", dir.display()));
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().into_owned();
        if path.is_dir() {
            collect_rs(&path, &format!("{prefix}{name}/"), out);
        } else if name.ends_with(".rs") {
            out.push((format!("{prefix}{name}"), read_text(&path)));
        }
    }
}

fn file_text<'a>(files: &'a [(String, String)], rel: &str) -> &'a str {
    files
        .iter()
        .find(|(r, _)| r == rel)
        .map(|(_, t)| t.as_str())
        .unwrap_or_else(|| panic!("{rel} not found"))
}

/// The keys of one TOML section (`key = …` or dotted `key.workspace = true`).
fn section_keys(toml: &str, header: &str) -> BTreeSet<String> {
    let mut inside = false;
    let mut keys = BTreeSet::new();
    for line in toml.lines() {
        let t = line.trim();
        if t.starts_with('[') {
            inside = t == header;
            continue;
        }
        if !inside || t.is_empty() || t.starts_with('#') {
            continue;
        }
        let key = t.split('=').next().unwrap_or(t).trim();
        let key = key
            .split('.')
            .next()
            .unwrap_or(key)
            .trim()
            .trim_matches('"');
        keys.insert(key.to_owned());
    }
    keys
}

/// Every `.rs` file of the workspace: `crates/*/src`, `crates/*/tests` and `src-tauri/src`, as
/// (app-root-relative path, text).
fn workspace_rs_files() -> Vec<(String, String)> {
    let root = app_root();
    let mut files = Vec::new();
    let crates = root.join("crates");
    for name in crate_names(&crates) {
        for sub in ["src", "tests"] {
            let dir = crates.join(&name).join(sub);
            if dir.is_dir() {
                files.extend(rs_files(&dir, &format!("crates/{name}/{sub}/")));
            }
        }
    }
    files.extend(rs_files(
        &root.join("src-tauri").join("src"),
        "src-tauri/src/",
    ));
    files
}

fn crate_names(crates: &Path) -> Vec<String> {
    let mut names: Vec<String> = fs::read_dir(crates)
        .unwrap_or_else(|e| panic!("cannot list {}: {e}", crates.display()))
        .map(|entry| entry.unwrap_or_else(|e| panic!("cannot list crates: {e}")))
        .filter(|entry| entry.path().is_dir())
        .map(|entry| entry.file_name().to_string_lossy().into_owned())
        .collect();
    names.sort();
    names
}

/// Every manifest of the app: the root, each crate, `src-tauri` and the clippy-guard fixture.
fn manifests() -> Vec<(String, String)> {
    let root = app_root();
    let mut rels = vec!["Cargo.toml".to_owned()];
    rels.extend(
        crate_names(&root.join("crates"))
            .into_iter()
            .map(|name| format!("crates/{name}/Cargo.toml")),
    );
    rels.push("src-tauri/Cargo.toml".to_owned());
    rels.push("tests/fixtures/clippy-guard/Cargo.toml".to_owned());
    rels.into_iter()
        .map(|rel| {
            let text = read_text(&root.join(&rel));
            (rel, text)
        })
        .collect()
}

/// A test source (FR-086): `crates/*/src/**/*_tests.rs` or `crates/*/tests/*.rs`.
fn is_test_source(rel: &str) -> bool {
    let parts: Vec<&str> = rel.split('/').collect();
    match parts.as_slice() {
        ["crates", _, "src", .., last] => last.ends_with("_tests.rs"),
        ["crates", _, "tests", file] => file.ends_with(".rs"),
        _ => false,
    }
}

/// The line with any `#` comment outside a `"…"` string cut off.
fn toml_uncomment(line: &str) -> &str {
    let mut in_string = false;
    for (i, c) in line.char_indices() {
        match c {
            '"' => in_string = !in_string,
            '#' if !in_string => return &line[..i],
            _ => {}
        }
    }
    line
}

type TomlSection = (String, Vec<(String, String)>);

/// Every `[header]` of a manifest with its `key = value` entries in file order, comments
/// dropped; entries before the first header sit under `""`. A dotted key keeps its first part.
fn toml_sections(toml: &str) -> Vec<TomlSection> {
    let mut out: Vec<TomlSection> = vec![(String::new(), Vec::new())];
    for line in toml.lines() {
        let t = toml_uncomment(line).trim();
        if t.is_empty() {
            continue;
        }
        if t.starts_with('[') {
            out.push((t.to_owned(), Vec::new()));
            continue;
        }
        if let Some((k, v)) = t.split_once('=') {
            let key = k
                .trim()
                .split('.')
                .next()
                .unwrap_or("")
                .trim()
                .trim_matches('"');
            if let Some(last) = out.last_mut() {
                last.1.push((key.to_owned(), v.trim().to_owned()));
            }
        }
    }
    out
}

fn toml_entry<'a>(sections: &'a [TomlSection], header: &str, key: &str) -> Option<&'a str> {
    sections
        .iter()
        .filter(|(h, _)| h == header)
        .flat_map(|(_, entries)| entries)
        .find(|(k, _)| k == key)
        .map(|(_, v)| v.as_str())
}

fn toml_section_keys(sections: &[TomlSection], header: &str) -> Vec<String> {
    sections
        .iter()
        .filter(|(h, _)| h == header)
        .flat_map(|(_, entries)| entries.iter().map(|(k, _)| k.clone()))
        .collect()
}

/// `text` split at the commas outside `[…]`, `{…}` and `"…"`.
fn split_toml_list(text: &str) -> Vec<&str> {
    let mut items = Vec::new();
    let mut depth = 0usize;
    let mut in_string = false;
    let mut start = 0;
    for (i, c) in text.char_indices() {
        match c {
            '"' => in_string = !in_string,
            '[' | '{' if !in_string => depth += 1,
            ']' | '}' if !in_string => depth = depth.saturating_sub(1),
            ',' if !in_string && depth == 0 => {
                items.push(&text[start..i]);
                start = i + 1;
            }
            _ => {}
        }
    }
    items.push(&text[start..]);
    items.into_iter().filter(|s| !s.trim().is_empty()).collect()
}

/// A dependency value's fields: an inline table's keys, or `version` for a bare version string.
fn dep_fields(value: &str) -> BTreeMap<String, String> {
    let v = value.trim();
    match v.strip_prefix('{').and_then(|r| r.strip_suffix('}')) {
        Some(inner) => split_toml_list(inner)
            .into_iter()
            .filter_map(|item| item.split_once('='))
            .map(|(k, v)| (k.trim().trim_matches('"').to_owned(), v.trim().to_owned()))
            .collect(),
        None => BTreeMap::from([("version".to_owned(), v.to_owned())]),
    }
}

fn toml_str(value: &str) -> Option<&str> {
    value.trim().strip_prefix('"')?.strip_suffix('"')
}

fn toml_str_array(value: &str) -> Option<Vec<String>> {
    let inner = value.trim().strip_prefix('[')?.strip_suffix(']')?;
    split_toml_list(inner)
        .into_iter()
        .map(|item| toml_str(item).map(str::to_owned))
        .collect()
}

/// Every dependency name a manifest declares: each entry key, the crate of a
/// `[….dependencies.<name>]` header, and every `package = "<name>"` rename.
fn toml_dependency_names(toml: &str) -> BTreeSet<String> {
    let mut names = BTreeSet::new();
    for (header, entries) in toml_sections(toml) {
        let inner = header.trim_start_matches('[').trim_end_matches(']');
        if let Some((head, last)) = inner.rsplit_once('.')
            && head.ends_with("dependencies")
        {
            names.insert(last.trim_matches('"').to_owned());
        }
        for (key, value) in entries {
            if let Some(package) = dep_fields(&value).get("package").and_then(|p| toml_str(p)) {
                names.insert(package.to_owned());
            }
            names.insert(key);
        }
    }
    names
}

/// Whitespace removed, so `{ workspace = true }` and `{workspace=true}` compare equal.
fn squash(value: &str) -> String {
    value.chars().filter(|c| !c.is_whitespace()).collect()
}

/// FR-075: `core_foundation` only in `sysproxy_cf.rs` and its tests, over every workspace file.
fn core_foundation_findings(files: &[(String, String)]) -> Vec<Finding> {
    confinement(
        &tokenized(&lex(files)),
        "core_foundation",
        &CORE_FOUNDATION_FILES,
        RULE_CONFINED,
    )
}

/// FR-073 to FR-075, FR-081, SC-026, R-15, R-16 over the workspace `.rs` files and manifests.
fn ureq_findings(files: &[(String, String)], manifests: &[(String, String)]) -> Vec<Finding> {
    let lexed = lex(files);
    let toks_of = tokenized(&lexed);
    let mut out: Vec<Finding> = CONFINED_TOKENS
        .iter()
        .flat_map(|(needle, allowed)| {
            let rule = if allowed.is_empty() {
                RULE_BANNED
            } else {
                RULE_CONFINED
            };
            confinement(&toks_of, needle, allowed, rule)
        })
        .collect();
    let find = |rel: &str| lexed.iter().find(|l| l.rel == rel);
    for (rel, crates) in [
        (HTTP_RS, &["ureq", "rustls"][..]),
        (PAC_RS, &["rquickjs"][..]),
    ] {
        let Some(l) = find(rel) else {
            continue;
        };
        let toks = tokens(&l.code);
        for sig in pub_signatures(&toks) {
            let sig = &toks[sig];
            if crates.iter().any(|c| sig.contains(c)) {
                out.push(Finding {
                    file: rel.to_owned(),
                    rule: RULE_PUB_SIGNATURE,
                    detail: format!("`{}`", sig.join(" ")),
                });
            }
        }
    }
    let mut report = |file: &str, rule: &'static str, detail: String| {
        out.push(Finding {
            file: file.to_owned(),
            rule,
            detail,
        });
    };
    match find(HTTP_RS) {
        None => report(HTTP_RS, RULE_ONE_AGENT, "not found".to_owned()),
        Some(l) => {
            let toks = tokens(&l.code);
            let fns = fn_items(&toks);
            let builders: Vec<Option<&str>> = seq_at(&toks, &["config_builder"])
                .into_iter()
                .map(|p| enclosing_fn(&fns, p))
                .collect();
            if builders != [Some("agent_config")] {
                report(
                    HTTP_RS,
                    RULE_ONE_AGENT,
                    format!("`config_builder` in {builders:?}, expected once, in fn agent_config"),
                );
            }
            let agents: Vec<bool> = seq_at(&toks, &["new_with_config"])
                .into_iter()
                .map(|p| toks.get(p + 1..p + 4) == Some(&["(", "agent_config", "("][..]))
                .collect();
            if agents != [true] {
                report(
                    HTTP_RS,
                    RULE_ONE_AGENT,
                    format!(
                        "`new_with_config` calls followed by `(agent_config(`: {agents:?}, expected [true]"
                    ),
                );
            }
            for call in SECOND_AGENT_CALLS {
                let n = seq_at(&toks, &tokens(call)).len();
                if n > 0 {
                    report(HTTP_RS, RULE_ONE_AGENT, format!("`{call}` {n} time(s)"));
                }
            }
        }
    }
    match find(FIXTURE_RS) {
        None => report(FIXTURE_RS, RULE_DROP_CONNECT, "not found".to_owned()),
        Some(l) => {
            let toks = tokens(&l.code);
            let drops: Vec<Range<usize>> = impl_blocks(&toks)
                .into_iter()
                .filter(|b| b.trait_name == Some("Drop"))
                .map(|b| b.body)
                .collect();
            let connects: Vec<bool> = seq_at(&toks, &tokens("TcpStream :: connect"))
                .into_iter()
                .map(|p| drops.iter().any(|d| d.contains(&p)))
                .collect();
            if connects != [true] {
                report(
                    FIXTURE_RS,
                    RULE_DROP_CONNECT,
                    format!(
                        "`TcpStream::connect` sites inside `impl Drop`: {connects:?}, expected [true]"
                    ),
                );
            }
        }
    }
    for (key, allowed) in CONFINED_TOML_KEYS {
        for (rel, text) in manifests {
            if !allowed.contains(&rel.as_str()) && toml_dependency_names(text).contains(key) {
                report(rel, RULE_TOML_KEY, key.to_owned());
            }
        }
    }
    out
}

/// FR-026, FR-036, PD-21, PD-22 over `feed.rs` and `version.rs`.
fn feed_model_findings(feed: &str, version: &str) -> Vec<Finding> {
    let mut out = Vec::new();
    let (code, _) = strip_comments_and_literals(feed);
    let toks = tokens(&code);
    let impls = impl_blocks(&toks);
    let fns = fn_items(&toks);
    let mut seam = |detail: String| {
        out.push(Finding {
            file: "feed.rs".to_owned(),
            rule: RULE_SEAM,
            detail,
        });
    };
    for ty in NEWTYPES {
        let decls = seq_at(&toks, &tokens(&format!("pub struct {ty} ( String ) ;")));
        if decls.len() != 1 {
            seam(format!(
                "`pub struct {ty}(String);` declared {} time(s)",
                decls.len()
            ));
        }
        for &at in &decls {
            for attr in outer_attributes(&toks, at) {
                let attr = &toks[attr];
                if attr.contains(&"derive")
                    && (attr.contains(&"Deserialize") || attr.contains(&"Default"))
                {
                    seam(format!("{ty}: `#[{}]`", attr.join(" ")));
                }
            }
        }
        for block in impls.iter().filter(|b| b.self_name == ty) {
            if let Some(tr) = block.trait_name
                && CONSTRUCTOR_TRAITS.contains(&tr)
            {
                seam(format!("impl {tr} for {ty}"));
            }
            for f in fns.iter().filter(|f| block.body.contains(&f.start)) {
                let ret = &toks[f.ret.clone()];
                if (ret.contains(&"Self") || ret.contains(&ty)) && is_plain_pub(&toks, f.start) {
                    seam(format!(
                        "{ty}: `pub fn {}` returns `{}`",
                        f.name,
                        ret.join(" ")
                    ));
                }
            }
        }
    }
    for block in &impls {
        let Some(tr) = block.trait_toks.clone() else {
            continue;
        };
        for url in ["PageUrl", "AssetUrl"] {
            if !seq_at(&toks[tr.clone()], &["From", "<", url, ">"]).is_empty() {
                seam(format!("impl From<{url}> for {}", block.self_name));
            }
        }
    }
    for f in &fns {
        let params = &toks[f.params.clone()];
        let ret = &toks[f.ret.clone()];
        let owner = impls
            .iter()
            .find(|b| b.body.contains(&f.start))
            .map(|b| b.self_name);
        for (from, to) in [("PageUrl", "AssetUrl"), ("AssetUrl", "PageUrl")] {
            let takes = params.contains(&from) || (owner == Some(from) && params.contains(&"self"));
            if takes && ret.contains(&to) {
                seam(format!("fn {} takes {from} and returns {to}", f.name));
            }
        }
    }
    for (name, fields) in item_bodies(&toks) {
        if name != "PlatformAsset" && toks[fields].contains(&"AssetUrl") {
            seam(format!("`AssetUrl` in a field of {name}"));
        }
    }

    let (code, _) = strip_comments_and_literals(version);
    let toks = tokens(&code);
    let uses = use_ranges(&toks);
    let fns = fn_items(&toks);
    // FR-036 names call sites: the `use` that imports `NativePlatform` is not one.
    for needle in ["Arch :: native (", "NativePlatform"] {
        let sites: Vec<&str> = seq_at(&toks, &tokens(needle))
            .into_iter()
            .filter(|p| !uses.iter().any(|u| u.contains(p)))
            .map(|p| enclosing_fn(&fns, p).unwrap_or("<item level>"))
            .collect();
        if sites != ["resolve_native"] {
            out.push(Finding {
                file: "version.rs".to_owned(),
                rule: RULE_NATIVE,
                detail: format!("`{needle}` in {sites:?}, expected once, in fn resolve_native"),
            });
        }
    }
    out
}

/// FR-087, FR-088, TA-12 and the FR-086 extension over the test sources (plus `fixture_server.rs`
/// for FR-086, and every crate file for `set_logger`).
fn test_source_findings(files: &[(String, String)]) -> Vec<Finding> {
    let lexed: Vec<Lexed> = lex(files)
        .into_iter()
        .filter(|l| l.rel.starts_with("crates/"))
        .collect();
    let spans_of: Vec<Vec<(usize, &str)>> = lexed.iter().map(|l| spanned_tokens(&l.code)).collect();
    let toks_of: Vec<(&str, Vec<&str>)> = lexed
        .iter()
        .zip(&spans_of)
        .map(|(l, spans)| (l.rel.as_str(), spans.iter().map(|(_, t)| *t).collect()))
        .collect();
    let mut out = Vec::new();
    for ((l, spans), (_, toks)) in lexed.iter().zip(&spans_of).zip(&toks_of) {
        if !is_test_source(&l.rel) {
            continue;
        }
        let fns = fn_items(toks);
        let mut report = |rule: &'static str, detail: String| {
            out.push(Finding {
                file: l.rel.clone(),
                rule,
                detail,
            });
        };
        for (needle, bodies, live_feed) in BODY_RULES {
            for p in seq_at(toks, &tokens(needle)) {
                let body = enclosing_fn(&fns, p);
                let allowed = (live_feed && l.rel == LIVE_FEED_RS)
                    || body.is_some_and(|b| bodies.contains(&(l.rel.as_str(), b)));
                if !allowed {
                    report(
                        RULE_TEST_BODY,
                        format!("`{needle}` in {}", body.unwrap_or("<item level>")),
                    );
                }
            }
        }
        for needle in TEST_BANNED {
            let n = seq_at(toks, &tokens(needle)).len();
            if n > 0 {
                report(RULE_TEST_BANNED, format!("`{needle}` {n} time(s)"));
            }
        }
        let fakes: Vec<Range<usize>> = impl_blocks(toks)
            .into_iter()
            .filter(|b| b.self_name == "FakePacHost")
            .map(|b| b.body)
            .collect();
        for p in seq_at(toks, &tokens("PacHostHandle :: new (")) {
            if l.rel != PAC_TESTS_RS || !fakes.iter().any(|f| f.contains(&p)) {
                report(
                    RULE_FAKE_HOST,
                    format!(
                        "`PacHostHandle::new(` in {}",
                        enclosing_fn(&fns, p).unwrap_or("<item level>")
                    ),
                );
            }
        }
        if l.rel.starts_with("crates/core/") {
            for p in seq_at(toks, &tokens("Command :: new (")) {
                let open = p + 4;
                let next = spans.get(open + 1).map_or(l.code.len(), |(o, _)| *o);
                let program = literal_between(&l.literals, spans[open].0, next);
                // TA-1: a program of `TEST_PROGRAM_BODIES` only inside its one named body.
                let body = enclosing_fn(&fns, p);
                let ok = program.is_some_and(|lit| {
                    let name = lit.text.as_str();
                    TEST_PROGRAMS.contains(&name)
                        || body.is_some_and(|b| {
                            TEST_PROGRAM_BODIES.contains(&(l.rel.as_str(), b, name))
                        })
                });
                if !ok {
                    report(
                        RULE_COMMAND,
                        format!(
                            "`Command::new({:?})`",
                            program.map_or("<not a literal>", |lit| lit.text.as_str())
                        ),
                    );
                }
            }
        }
        let exempt = [CONFIG_GUARD_RS, INJECTION_RS].contains(&l.rel.as_str());
        let in_literal = !exempt && l.literals.iter().any(|lit| lit.text.contains("gsettings"));
        if in_literal || toks.contains(&"gsettings") {
            report(RULE_GSETTINGS, "gsettings".to_owned());
        }
    }

    // FR-088: the one `log::set_logger` of the crates is the capture store's.
    for (rel, toks) in &toks_of {
        let n = seq_at(toks, &["set_logger"]).len();
        let want = usize::from(*rel == TEST_SUPPORT_RS);
        if n != want {
            out.push(Finding {
                file: (*rel).to_owned(),
                rule: RULE_SET_LOGGER,
                detail: format!("`set_logger` {n} time(s), expected {want}"),
            });
        }
    }
    if !lexed.iter().any(|l| l.rel == TEST_SUPPORT_RS) {
        out.push(Finding {
            file: TEST_SUPPORT_RS.to_owned(),
            rule: RULE_SET_LOGGER,
            detail: "not found".to_owned(),
        });
    }

    // FR-086: no process-wide lock, no listener outside the fixture, no fixed port.
    let in_fr086 = |rel: &str| is_test_source(rel) || rel == FIXTURE_RS;
    let fr086: Vec<(&str, Vec<&str>)> = toks_of
        .iter()
        .filter(|(rel, _)| in_fr086(rel))
        .cloned()
        .collect();
    out.extend(confinement(
        &fr086,
        "TcpListener :: bind",
        &[FIXTURE_RS],
        RULE_LISTENER,
    ));
    for ((l, spans), (_, toks)) in lexed.iter().zip(&spans_of).zip(&toks_of) {
        if !in_fr086(&l.rel) {
            continue;
        }
        let mut report = |rule: &'static str, detail: String| {
            out.push(Finding {
                file: l.rel.clone(),
                rule,
                detail,
            });
        };
        for (i, &t) in toks.iter().enumerate() {
            // `'static` is a lifetime, not a `static` item.
            if t != "static" || (i > 0 && toks[i - 1] == "'") {
                continue;
            }
            let mut depth = 0i32;
            let mut end = toks.len();
            for (m, &u) in toks.iter().enumerate().skip(i) {
                match u {
                    "(" | "[" | "{" => depth += 1,
                    ")" | "]" | "}" => depth -= 1,
                    ";" if depth == 0 => {
                        end = m;
                        break;
                    }
                    _ => {}
                }
            }
            let item = &toks[i..end];
            if item.contains(&"Mutex") || item.contains(&"RwLock") {
                report(RULE_STATIC_LOCK, format!("`{}`", item.join(" ")));
            }
        }
        for p in seq_at(toks, &["bind", "("]) {
            let open = p + 1;
            let next = spans.get(open + 1).map_or(l.code.len(), |(o, _)| *o);
            if let Some(lit) = literal_between(&l.literals, spans[open].0, next) {
                if !lit.text.ends_with(":0") {
                    report(RULE_FIXED_PORT, format!("`bind({:?})`", lit.text));
                }
                continue;
            }
            if toks.get(open + 1) != Some(&"(") {
                continue;
            }
            let inner = open + 1;
            let comma = spans.get(inner + 1).map_or(l.code.len(), |(o, _)| *o);
            let port = toks.get(inner + 2).copied().unwrap_or("");
            let tuple = toks.get(inner + 1) == Some(&",")
                && literal_between(&l.literals, spans[inner].0, comma).is_some();
            let is_number = port.starts_with(|c: char| c.is_ascii_digit());
            if tuple && is_number && !is_zero_integer(port) {
                report(RULE_FIXED_PORT, format!("`bind((…, {port}))`"));
            }
        }
    }
    out
}

/// FR-063, FR-074, SC-039 over the `src-tauri/src` files (paths relative to `src`), rules 1-4 of
/// plan §4.11.1, one `<file>: <rule>: <detail>` line per finding.
fn src_tauri_env_findings(files: &[(String, String)]) -> Vec<String> {
    let lexed = lex(files);
    let toks_of = tokenized(&lexed);
    let mut out: Vec<String> = ENV_READS
        .iter()
        .flat_map(|needle| confinement(&toks_of, needle, &ENV_READ_FILES, RULE_ENV_READ))
        .map(|f| format!("{}: {}: {}", f.file, f.rule, f.detail))
        .collect();
    for l in &lexed {
        for lit in &l.literals {
            let text = lit.text.as_str();
            if l.rel != "proxy_env.rs" && DESKTOP_VARS.contains(&text) {
                out.push(format!("{}: {RULE_DESKTOP_VAR}: \"{text}\"", l.rel));
            }
            if l.rel == "proxy_env.rs" && !lit.in_attribute && !PROXY_ENV_LITERALS.contains(&text) {
                out.push(format!("{}: {RULE_PROXY_ENV_LITERAL}: \"{text}\"", l.rel));
            }
        }
    }
    let feed: Vec<(&str, usize)> = files
        .iter()
        .map(|(rel, text)| (rel.as_str(), text.matches("krateaudio.com").count()))
        .filter(|(_, n)| *n > 0)
        .collect();
    if feed != [("lib.rs", 1)] {
        out.push(format!(
            "krateaudio.com in src-tauri/src: {feed:?}, expected [(\"lib.rs\", 1)]"
        ));
    }
    out
}

/// SC-038 over every manifest: the four reader crates' root pins, their one member declaration
/// each, and `rquickjs` outside `FR007_DEPENDENCIES`.
fn reader_dependency_findings(manifests: &[(String, String)]) -> Vec<String> {
    let mut out = Vec::new();
    let parsed: Vec<(&str, Vec<TomlSection>)> = manifests
        .iter()
        .map(|(rel, text)| (rel.as_str(), toml_sections(text)))
        .collect();
    let sections_of = |rel: &str| {
        parsed
            .iter()
            .find(|(r, _)| *r == rel)
            .map_or(&[][..], |(_, s)| s.as_slice())
    };
    for pin in &READER_PINS {
        match toml_entry(sections_of("Cargo.toml"), WORKSPACE_DEPS, pin.name) {
            None => out.push(format!("{}: not in the root {WORKSPACE_DEPS}", pin.name)),
            Some(value) => {
                let fields = dep_fields(value);
                let version = fields.get("version").and_then(|v| toml_str(v));
                if version != Some(pin.version) {
                    out.push(format!(
                        "{}: root version {version:?}, expected {}",
                        pin.name, pin.version
                    ));
                }
                if let Some(want) = pin.features {
                    out.extend(features_finding(pin.name, &fields, want));
                }
                if fields.get("default-features").map(String::as_str) == Some("false") {
                    out.push(format!(
                        "{}: default-features = false in the root",
                        pin.name
                    ));
                }
            }
        }
        for (rel, sections) in &parsed {
            for (header, entries) in sections {
                for (_, value) in entries.iter().filter(|(k, _)| k == pin.name) {
                    if *rel == "Cargo.toml" && header == WORKSPACE_DEPS {
                        continue;
                    }
                    if (*rel, header.as_str()) != pin.home {
                        out.push(format!("{}: declared in {rel} {header}", pin.name));
                        continue;
                    }
                    let fields = dep_fields(value);
                    for key in ["features", "default-features"] {
                        if fields.contains_key(key) {
                            out.push(format!(
                                "{}: member entry in {rel} {header} carries {key}",
                                pin.name
                            ));
                        }
                    }
                    if fields.get("workspace").map(String::as_str) != Some("true") {
                        out.push(format!(
                            "{}: member entry in {rel} {header} is not `workspace = true`",
                            pin.name
                        ));
                    }
                }
            }
        }
        let (rel, header) = pin.home;
        let keys = toml_section_keys(sections_of(rel), header);
        if !keys.iter().any(|k| k == pin.name) {
            out.push(format!("{}: missing from {rel} {header}", pin.name));
        } else if pin.alone && keys != [pin.name] {
            out.push(format!(
                "{rel} {header}: crates {keys:?}, expected [\"{}\"]",
                pin.name
            ));
        }
    }
    if FR007_DEPENDENCIES.contains(&"rquickjs") {
        out.push("rquickjs: in FR007_DEPENDENCIES".to_owned());
    }
    out
}

/// The root entry's `features` against `want` (an empty `want`: no `features` key at all).
fn features_finding(
    name: &str,
    fields: &BTreeMap<String, String>,
    want: &[&str],
) -> Option<String> {
    let got = fields.get("features").map(|f| toml_str_array(f));
    if want.is_empty() {
        return got.map(|_| format!("{name}: the root entry has a features key"));
    }
    let mut expected: Vec<String> = want.iter().map(|s| (*s).to_owned()).collect();
    expected.sort();
    let got = got.flatten().map(|mut g| {
        g.sort();
        g
    });
    (got.as_ref() != Some(&expected))
        .then(|| format!("{name}: root features {got:?}, expected {expected:?}"))
}

/// `files` with `text` appended to `rel`, which must be among them.
fn plant(files: &[(String, String)], rel: &str, text: &str) -> Vec<(String, String)> {
    let mut out = files.to_vec();
    let entry = out
        .iter_mut()
        .find(|(r, _)| r == rel)
        .unwrap_or_else(|| panic!("plant target {rel} not found"));
    entry.1 = format!("{}\n{text}", entry.1);
    out
}

/// `text` (line endings made `\n`) with its one `from` replaced by `to`.
fn replace_one(text: &str, from: &str, to: &str) -> String {
    let text = text.replace("\r\n", "\n");
    assert_eq!(text.matches(from).count(), 1, "plant anchor {from:?}");
    text.replacen(from, to, 1)
}

fn replace_in(
    files: &[(String, String)],
    rel: &str,
    from: &str,
    to: &str,
) -> Vec<(String, String)> {
    let mut out = files.to_vec();
    let entry = out
        .iter_mut()
        .find(|(r, _)| r == rel)
        .unwrap_or_else(|| panic!("plant target {rel} not found"));
    entry.1 = replace_one(&entry.1, from, to);
    out
}

/// The items of `planted` that `base` does not hold, counted (a repeated item is new).
fn added<T: Clone + PartialEq>(base: &[T], planted: &[T]) -> Vec<T> {
    let mut rest = base.to_vec();
    let mut out = Vec::new();
    for item in planted {
        match rest.iter().position(|b| b == item) {
            Some(at) => {
                rest.remove(at);
            }
            None => out.push(item.clone()),
        }
    }
    out
}

/// The plant added a finding of `rule` in `file`.
fn assert_reported(base: &[Finding], planted: &[Finding], file: &str, rule: &str, what: &str) {
    let new = added(base, planted);
    assert!(
        new.iter().any(|f| f.file == file && f.rule == rule),
        "{file}: planted {what:?} not reported as {rule}; new findings:\n{}",
        show(&new)
    );
}

/// A literal's value as far as the Phase 6 scans need it: a raw literal's text, or an escaped
/// literal's text with each `\\` read as one backslash.
fn literal_value(lit: &Literal) -> String {
    if lit.raw {
        lit.text.clone()
    } else {
        lit.text.replace("\\\\", "\\")
    }
}

/// The outer attributes of the `fn` whose keyword is token `at`, its visibility (`pub`,
/// `pub(crate)`) and qualifiers skipped.
fn fn_attributes(toks: &[&str], at: usize) -> Vec<Range<usize>> {
    let mut k = at;
    while k > 0 && ["const", "async", "unsafe", "extern"].contains(&toks[k - 1]) {
        k -= 1;
    }
    if k >= 4 && toks[k - 1] == ")" && toks[k - 3] == "(" && toks[k - 4] == "pub" {
        k -= 4;
    } else if k > 0 && toks[k - 1] == "pub" {
        k -= 1;
    }
    outer_attributes(toks, k)
}

/// Whether the attribute `attr` (inner token range, from `outer_attributes`) is a `cfg` naming
/// `target_os = "macos"` with no `not`. Its literals are blanked in the code text, so the
/// `"macos"` is looked up among the file's literals by byte offset.
fn is_macos_cfg(
    toks: &[&str],
    spans: &[(usize, &str)],
    literals: &[Literal],
    attr: &Range<usize>,
) -> bool {
    let inner = &toks[attr.clone()];
    let (Some(open), Some(close)) = (attr.start.checked_sub(1), spans.get(attr.end)) else {
        return false;
    };
    let from = spans[open].0;
    let names_macos = literals
        .iter()
        .any(|lit| lit.start > from && lit.start < close.0 && lit.text == "macos");
    inner.first() == Some(&"cfg")
        && !seq_at(inner, &["target_os", "="]).is_empty()
        && !inner.contains(&"not")
        && names_macos
}

/// FR-082 (crates): `pulldown_cmark` only in `notes.rs` and its tests; `plist ::` only in
/// `detect_tests.rs` and, in `detect.rs`, only inside a `fn read_au_plist` gated on macOS.
fn crate_confinement_findings(files: &[(String, String)]) -> Vec<Finding> {
    let lexed = lex(files);
    let toks_of = tokenized(&lexed);
    let mut out = confinement(
        &toks_of,
        "pulldown_cmark",
        &[NOTES_RS, NOTES_TESTS_RS],
        RULE_CONFINED,
    );
    let plist = tokens("plist ::");
    for (l, (_, toks)) in lexed.iter().zip(&toks_of) {
        if l.rel == DETECT_TESTS_RS {
            continue;
        }
        let sites = seq_at(toks, &plist);
        let outside: Vec<&str> = if l.rel == DETECT_RS {
            let spans = spanned_tokens(&l.code);
            let fns = fn_items(toks);
            sites
                .into_iter()
                .filter_map(|p| {
                    let owner = fns
                        .iter()
                        .filter(|f| f.body.as_ref().is_some_and(|b| b.contains(&p)))
                        .min_by_key(|f| f.start);
                    let ok = owner.is_some_and(|f| {
                        f.name == "read_au_plist"
                            && fn_attributes(toks, f.start)
                                .iter()
                                .any(|a| is_macos_cfg(toks, &spans, &l.literals, a))
                    });
                    (!ok).then(|| owner.map_or("<item level>", |f| f.name))
                })
                .collect()
        } else {
            vec!["<any site>"; sites.len()]
        };
        if !outside.is_empty() {
            out.push(Finding {
                file: l.rel.clone(),
                rule: RULE_PLIST_SITE,
                detail: format!(
                    "`plist ::` {} time(s) outside a macOS-gated fn read_au_plist: {outside:?}",
                    outside.len()
                ),
            });
        }
    }
    out
}

/// FR-082 (host I/O) and FR-002 over the workspace `.rs` files: the FFI names only in
/// `os_facts.rs`; `OsHostIo` only in `os_facts.rs` and `commands.rs`; each host literal only in
/// its `platform/<os>.rs` (`injection.rs` exempt); `Krate Audio` in no core `src` literal
/// outside `platform/`.
fn host_io_findings(files: &[(String, String)]) -> Vec<Finding> {
    let lexed = lex(files);
    let toks_of = tokenized(&lexed);
    let mut out: Vec<Finding> = HOST_FFI
        .iter()
        .flat_map(|needle| confinement(&toks_of, needle, &[OS_FACTS_RS], RULE_CONFINED))
        .collect();
    out.extend(confinement(
        &toks_of,
        "OsHostIo",
        &[OS_FACTS_RS, COMMANDS_RS],
        RULE_CONFINED,
    ));
    for l in lexed.iter().filter(|l| l.rel != INJECTION_RS) {
        let core_outside_platform = l.rel.starts_with(CORE_SRC) && !l.rel.starts_with(PLATFORM_DIR);
        for lit in &l.literals {
            let value = literal_value(lit);
            for (needle, home) in HOST_LITERALS {
                if l.rel != home && value.contains(needle) {
                    out.push(Finding {
                        file: l.rel.clone(),
                        rule: RULE_HOST_LITERAL,
                        detail: format!("{needle:?} (only in {home})"),
                    });
                }
            }
            if core_outside_platform && value.contains(VENDOR_LITERAL) {
                out.push(Finding {
                    file: l.rel.clone(),
                    rule: RULE_VENDOR_LITERAL,
                    detail: format!("{VENDOR_LITERAL:?}; use platform::VENDOR_DIR"),
                });
            }
        }
    }
    out
}

/// FR-083 (TA-2): the `is_test_source` files plus `test_support.rs`, minus `injection.rs`, name
/// `OsHostIo` nowhere in code and hold no host literal.
fn test_host_io_findings(files: &[(String, String)]) -> Vec<Finding> {
    let scanned: Vec<(String, String)> = files
        .iter()
        .filter(|(rel, _)| (is_test_source(rel) || rel == TEST_SUPPORT_RS) && rel != INJECTION_RS)
        .cloned()
        .collect();
    let mut out = Vec::new();
    for l in lex(&scanned) {
        let n = seq_at(&tokens(&l.code), &["OsHostIo"]).len();
        if n > 0 {
            out.push(Finding {
                file: l.rel.clone(),
                rule: RULE_TEST_HOST_IO,
                detail: format!("`OsHostIo` {n} time(s)"),
            });
        }
        for lit in &l.literals {
            let value = literal_value(lit);
            for (needle, _) in HOST_LITERALS.iter().filter(|(n, _)| value.contains(n)) {
                out.push(Finding {
                    file: l.rel.clone(),
                    rule: RULE_TEST_HOST_IO,
                    detail: format!("{needle:?}; compare with the platform constant"),
                });
            }
        }
    }
    out
}

/// The FR-084 files: `detect.rs`, `notes.rs`, `status.rs` and the non-test `platform/*.rs`.
fn is_fr084_file(rel: &str) -> bool {
    [DETECT_RS, NOTES_RS, STATUS_RS].contains(&rel)
        || rel
            .strip_prefix(PLATFORM_DIR)
            .is_some_and(|f| !f.contains('/') && f.ends_with(".rs") && !f.ends_with("_tests.rs"))
}

/// FR-084 over the FR-084 files of `files`: none of `FR084_TOKENS` in code; `read_dir` exactly
/// once in `detect.rs`, inside `fn list_root`, and nowhere else.
fn write_token_findings(files: &[(String, String)]) -> Vec<Finding> {
    let scanned: Vec<(String, String)> = files
        .iter()
        .filter(|(rel, _)| is_fr084_file(rel))
        .cloned()
        .collect();
    let mut out = Vec::new();
    for l in lex(&scanned) {
        let toks = tokens(&l.code);
        let mut report = |rule: &'static str, detail: String| {
            out.push(Finding {
                file: l.rel.clone(),
                rule,
                detail,
            });
        };
        for needle in FR084_TOKENS {
            let n = seq_at(&toks, &tokens(needle)).len();
            if n > 0 {
                report(RULE_WRITE_TOKEN, format!("`{needle}` {n} time(s)"));
            }
        }
        let fns = fn_items(&toks);
        let sites: Vec<&str> = seq_at(&toks, &["read_dir"])
            .into_iter()
            .map(|p| enclosing_fn(&fns, p).unwrap_or("<item level>"))
            .collect();
        if l.rel == DETECT_RS {
            if sites != ["list_root"] {
                report(
                    RULE_READ_DIR,
                    format!(
                        "`read_dir` {} time(s) in detect.rs, expected once, in fn list_root: {sites:?}",
                        sites.len()
                    ),
                );
            }
        } else if !sites.is_empty() {
            report(
                RULE_READ_DIR,
                format!("`read_dir` {} time(s), expected 0: {sites:?}", sites.len()),
            );
        }
    }
    if !scanned.iter().any(|(rel, _)| rel == DETECT_RS) {
        out.push(Finding {
            file: DETECT_RS.to_owned(),
            rule: RULE_READ_DIR,
            detail: "not found".to_owned(),
        });
    }
    out
}

/// The FR-088 files, each as (app-root-relative path, its text or `None` when it cannot be read).
fn fr088_texts() -> Vec<(String, Option<String>)> {
    let root = app_root();
    FR088_FILES
        .iter()
        .map(|rel| ((*rel).to_owned(), fs::read_to_string(root.join(rel)).ok()))
        .collect()
}

/// FR-088 (TA-9): `<file>: missing` for a file that cannot be read, and `<file>:<line>:
/// `<pattern>`` for every line matching one of `FR088_PATTERNS`.
fn real_location_findings(files: &[(String, Option<String>)]) -> Vec<String> {
    let patterns: Vec<(&str, regex::Regex)> = FR088_PATTERNS
        .iter()
        .map(|p| {
            let re = regex::Regex::new(p).unwrap_or_else(|e| panic!("FR-088 pattern {p:?}: {e}"));
            (*p, re)
        })
        .collect();
    let mut out = Vec::new();
    for (rel, text) in files {
        let Some(text) = text else {
            out.push(format!("{rel}: missing"));
            continue;
        };
        for (n, line) in text.lines().enumerate() {
            for (p, re) in &patterns {
                if re.is_match(line) {
                    out.push(format!("{rel}:{}: `{p}`", n + 1));
                }
            }
        }
    }
    out
}

#[test]
fn core_sources_hold_no_production_literal() {
    let src = app_root().join("crates").join("core").join("src");
    let files = rs_files(&src, "");
    for must in [
        "lib.rs",
        "roots.rs",
        "context.rs",
        "platform/mod.rs",
        "platform/windows.rs",
        "platform/macos.rs",
        "platform/linux.rs",
    ] {
        assert!(
            files.iter().any(|(r, _)| r == must && is_scanned(r)),
            "{must} is not among the scanned core files"
        );
    }
    let findings: Vec<Finding> = files
        .iter()
        .flat_map(|(rel, text)| scan_core_file(rel, text))
        .collect();
    assert!(findings.is_empty(), "FR-021 findings:\n{}", show(&findings));

    // PD-5: the real `platform/mod.rs` passes with its six `cfg(target_os = …)` attributes and
    // its `#[path = "mod_tests.rs"]`, because attribute literals are exempt, not because the
    // lexer missed them.
    let (_, literals) = strip_comments_and_literals(file_text(&files, "platform/mod.rs"));
    let in_attributes: Vec<&str> = literals
        .iter()
        .filter(|l| l.in_attribute)
        .map(|l| l.text.as_str())
        .collect();
    assert_eq!(
        in_attributes,
        [
            "windows",
            "windows",
            "macos",
            "macos",
            "linux",
            "linux",
            "mod_tests.rs"
        ],
        "attribute literals of platform/mod.rs"
    );
}

#[test]
fn injection_scanner_reports_planted_violations() {
    // SC-008's strings, the PD-24 `sandboxed` case and a raw platform literal (PD-5).
    let plants: [(&str, &str, &str); 8] = [
        (
            "context.rs",
            "const P: &str = \"C:\\\\Program Files\";\n",
            RULE_SUBSTRING,
        ),
        (
            "settings.rs",
            "const FEED: &str = \"https://krateaudio.com/versions.json\";\n",
            RULE_SUBSTRING,
        ),
        (
            "platform/windows.rs",
            "fn f(p: &Path) -> bool { std::fs::metadata(p).is_ok() }\n",
            RULE_FS_TOKEN,
        ),
        (
            "platform/macos.rs",
            "fn f(p: &Path) -> bool { p.canonicalize().is_ok() }\n",
            RULE_FS_TOKEN,
        ),
        (
            "platform/linux.rs",
            "fn f() -> PathBuf { PathBuf::from(\"/opt/vst3\") }\n",
            RULE_PLATFORM_LITERAL,
        ),
        (
            "navigation.rs",
            "#[cfg(test)]\nmod tests {\n    #[test]\n    fn t() {}\n}\n",
            RULE_INLINE_TESTS,
        ),
        (
            "context.rs",
            "fn f(r: Roots, b: PathBuf) -> Roots { r.sandboxed(b) }\n",
            RULE_SANDBOXED,
        ),
        (
            "platform/windows.rs",
            "const C: &str = r\"Programs\";\n",
            RULE_PLATFORM_RAW,
        ),
    ];
    for (rel, text, rule) in plants {
        let findings = scan_core_file(rel, text);
        assert!(
            findings.iter().any(|f| f.rule == rule && f.file == rel),
            "{rel}: planted {text:?} not reported as {rule}; got:\n{}",
            show(&findings)
        );
    }

    // A non-allow-listed literal in a `platform/` file is reported; its attribute literals are not.
    let platform = "#[cfg(target_os = \"windows\")]\n\
        fn etc() -> PathBuf {\n    PathBuf::from(\"/etc\")\n}\n\
        #[cfg(test)]\n#[path = \"windows_tests.rs\"]\nmod tests;\n";
    let findings = scan_core_file("platform/windows.rs", platform);
    assert_eq!(
        findings,
        [Finding {
            file: "platform/windows.rs".to_owned(),
            rule: RULE_PLATFORM_LITERAL,
            detail: "\"/etc\"".to_owned(),
        }],
        "planted platform text"
    );

    // Comments and literals are not code: none of these is a finding.
    let quiet = "// mod tests { r.sandboxed(b) }\n\
        /* outer /* nested mod tests { */ still sandboxed */\n\
        const S: &str = \"mod tests { sandboxed }\";\n";
    let findings = scan_core_file("context.rs", quiet);
    assert!(findings.is_empty(), "quiet text:\n{}", show(&findings));

    // Exempt files are not scanned.
    for rel in [
        "test_support.rs",
        "roots_tests.rs",
        "platform/windows_tests.rs",
    ] {
        assert!(scan_core_file(rel, "mod tests { const P: &str = \"/home/u\"; }").is_empty());
    }

    // The lexer: nested block comments, byte, raw and escaped strings, char literals versus
    // lifetimes, attribute spans.
    let tricky = r##"/* outer /* inner "no" */ still */ fn f<'a>(s: &'a str) -> char {
    let _ = b"by"; let _ = r#"ra"w"#; let _ = "es\"c";
    if s.is_empty() { '"' } else { '\'' }
}
#[doc = "at"] const K: u8 = b'x';
"##;
    let (code, literals) = strip_comments_and_literals(tricky);
    assert_eq!(code.len(), tricky.len(), "blanking keeps the byte length");
    assert!(code.contains("fn f<'a>(s: &'a str) -> char {"), "{code}");
    for gone in ["inner", "still", "by", "ra", "es", "at", "'x'", "'\"'"] {
        assert!(
            !code.contains(gone),
            "{gone:?} survived in the code text:\n{code}"
        );
    }
    let seen: Vec<(&str, bool, bool)> = literals
        .iter()
        .map(|l| (l.text.as_str(), l.raw, l.in_attribute))
        .collect();
    assert_eq!(
        seen,
        [
            ("by", false, false),
            ("ra\"w", true, false),
            ("es\\\"c", false, false),
            ("at", false, true),
        ]
    );
    assert!(
        literals
            .iter()
            .all(|l| tricky[l.start..].starts_with(['b', 'r', '"']))
    );
}

#[test]
fn disallowed_methods_expectations_are_the_seven_d6_sites() {
    let root = app_root();
    let mut files = workspace_rs_files();
    let fixture = root
        .join("tests")
        .join("fixtures")
        .join("clippy-guard")
        .join("src");
    files.extend(rs_files(&fixture, "tests/fixtures/clippy-guard/src/"));
    for must in [
        FIXTURE_LIB,
        "crates/core/tests/injection.rs",
        "crates/helper/src/main.rs",
    ] {
        assert!(
            files.iter().any(|(r, _)| r == must),
            "{must} was not scanned"
        );
    }

    let problems = d6_problems(&files);
    assert!(problems.is_empty(), "D6 count:\n{}", problems.join("\n"));

    // An eighth site, a group `expect`, and a group `allow` inside `cfg_attr(…)`, each planted in
    // the in-memory `context.rs`, are reported.
    let context = "crates/core/src/context.rs";
    for (plant, needle) in [
        (
            "#[expect(clippy::disallowed_methods, reason = \"x\")]\nfn planted() {}\n",
            "crates/core/src/context.rs: 1 clippy::disallowed_methods suppressions, expected 0",
        ),
        (
            "#[expect(clippy::style, reason = \"x\")]\nfn planted() {}\n",
            "crates/core/src/context.rs: lint-level attribute names clippy::style",
        ),
        (
            "#[cfg_attr(test, allow(warnings, reason = \"x\"))]\nfn planted() {}\n",
            "crates/core/src/context.rs: lint-level attribute names warnings",
        ),
        (
            "#![expect(clippy :: all)]\n",
            "crates/core/src/context.rs: lint-level attribute names clippy::all",
        ),
    ] {
        let mut planted = files.clone();
        let entry = planted
            .iter_mut()
            .find(|(r, _)| r == context)
            .unwrap_or_else(|| panic!("{context} not found"));
        entry.1 = format!("{plant}{}", entry.1);
        let problems = d6_problems(&planted);
        assert!(
            problems.iter().any(|p| p == needle),
            "planted {plant:?} not reported; got:\n{}",
            problems.join("\n")
        );
    }

    // A lint name only inside a literal or a comment is not a suppression.
    let quiet = "// #[expect(clippy::disallowed_methods)]\n\
        const S: &str = \"#[expect(clippy::all)]\";\n\
        #[doc = \"allow(warnings)\"]\nfn f() {}\n";
    assert_eq!(lint_suppressions(quiet), (0, Vec::new()));
}

#[test]
fn core_dependencies_are_the_fr007_set() {
    let root = app_root();
    let manifest = read_text(&root.join("crates").join("core").join("Cargo.toml"));
    let keys = section_keys(&manifest, "[dependencies]");
    let expected: BTreeSet<String> = FR007_DEPENDENCIES.into_iter().map(String::from).collect();
    assert_eq!(keys, expected, "crates/core/Cargo.toml [dependencies]");
    assert_eq!(
        section_keys(&manifest, "[dev-dependencies]"),
        BTreeSet::from(["flate2".to_owned()]),
        "crates/core/Cargo.toml [dev-dependencies]"
    );

    // R-6, R-8, R-15, PD-25: exactly two target sections, one crate each, `workspace = true`
    // only (no `features` / `default-features` key), and the root `rquickjs` pin a bare version.
    let targets: BTreeMap<String, Vec<(String, String)>> = toml_sections(&manifest)
        .into_iter()
        .filter(|(header, _)| header.starts_with("[target."))
        .map(|(header, entries)| {
            let entries = entries.into_iter().map(|(k, v)| (k, squash(&v))).collect();
            (header, entries)
        })
        .collect();
    let want = |name: &str| (name.to_owned(), "{workspace=true}".to_owned());
    assert_eq!(
        targets,
        BTreeMap::from([
            (NOT_WINDOWS_DEPS.to_owned(), vec![want("rquickjs")]),
            (
                MACOS_DEPS.to_owned(),
                vec![want("core-foundation"), want("plist")]
            ),
        ]),
        "crates/core/Cargo.toml target sections"
    );
    let workspace = toml_sections(&read_text(&root.join("Cargo.toml")));
    assert_eq!(
        toml_entry(&workspace, WORKSPACE_DEPS, "rquickjs"),
        Some("\"0.14.0\""),
        "root rquickjs pin"
    );
    // PD-15: the Phase 6 parsers pinned without their default features (no HTML writer, no serde).
    for (name, version) in [("pulldown-cmark", "0.13.6"), ("plist", "1.10.3")] {
        assert_eq!(
            toml_entry(&workspace, WORKSPACE_DEPS, name).map(squash),
            Some(format!("{{version=\"{version}\",default-features=false}}")),
            "root {name} pin"
        );
    }
    assert_eq!(
        toml_entry(
            &toml_sections(&manifest),
            "[dependencies]",
            "pulldown-cmark"
        )
        .map(squash),
        Some("{workspace=true}".to_owned()),
        "crates/core/Cargo.toml [dependencies] pulldown-cmark"
    );

    // FR-075, PD-31: `core_foundation` only in `sysproxy_cf.rs` and its tests.
    let files = workspace_rs_files();
    let base = core_foundation_findings(&files);
    let plant_text = "use core_foundation::string::CFString;\n";
    let planted = core_foundation_findings(&plant(&files, SYSPROXY_RS, plant_text));
    assert_reported(&base, &planted, SYSPROXY_RS, RULE_CONFINED, plant_text);
    assert!(base.is_empty(), "FR-075 findings:\n{}", show(&base));
}

#[test]
fn src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots() {
    let files: Vec<(String, String)> = rs_files(&app_root().join("src-tauri").join("src"), "")
        .into_iter()
        .filter(|(rel, _)| !rel.ends_with("_tests.rs"))
        .collect();
    for must in ["lib.rs", "os_roots.rs", "proxy_env.rs", "os_proxy.rs"] {
        assert!(
            files.iter().any(|(r, _)| r == must),
            "src-tauri/src/{must} not found"
        );
    }
    let base = src_tauri_env_findings(&files);
    let os_proxy_plant = "fn planted() { let _ = std::env::var(\"XDG_CURRENT_DESKTOP\"); }\n";
    for (rel, text, prefix) in [
        // SC-039's self-test: rules 1 and 2.
        (
            "os_proxy.rs",
            os_proxy_plant,
            "os_proxy.rs: env-read: `env :: var`",
        ),
        (
            "os_proxy.rs",
            os_proxy_plant,
            "os_proxy.rs: desktop-variable-literal: \"XDG_CURRENT_DESKTOP\"",
        ),
        (
            "lib.rs",
            "fn planted() { let _ = std::env::vars(); }\n",
            "lib.rs: env-read: `env :: vars`",
        ),
        (
            "proxy_env.rs",
            "const LANG: &str = \"LANG\";\n",
            "proxy_env.rs: proxy-env-literal: \"LANG\"",
        ),
    ] {
        let new = added(&base, &src_tauri_env_findings(&plant(&files, rel, text)));
        assert!(
            new.iter().any(|line| line.starts_with(prefix)),
            "{rel}: planted {text:?} not reported as {prefix:?}; new:\n{}",
            new.join("\n")
        );
    }
    // Attribute literals in `proxy_env.rs` (its `cfg(target_os = …)`, `reason = …`) are exempt.
    let quiet = "#[cfg(target_os = \"linux\")]\nconst Q: &str = \"HOME\";\n";
    let new = added(
        &base,
        &src_tauri_env_findings(&plant(&files, "proxy_env.rs", quiet)),
    );
    assert!(
        new.is_empty(),
        "quiet proxy_env.rs text:\n{}",
        new.join("\n")
    );
    assert!(base.is_empty(), "SC-039 findings:\n{}", base.join("\n"));
}

#[test]
fn ureq_is_confined_to_http_rs() {
    let files = workspace_rs_files();
    for must in [
        HTTP_RS,
        FIXTURE_RS,
        PAC_RS,
        OS_PROXY_RS,
        TAURI_LIB_RS,
        INJECTION_RS,
    ] {
        assert!(
            files.iter().any(|(r, _)| r == must),
            "{must} was not scanned"
        );
    }
    let manifests = manifests();
    let base = ureq_findings(&files, &manifests);
    let plants: [(&str, &str, &str); 16] = [
        (FEED_RS, "use ureq as u;\n", RULE_CONFINED),
        (HTTP_RS, "pub use ureq as u;\n", RULE_PUB_SIGNATURE),
        (
            HTTP_RS,
            "pub fn planted() -> rustls::Error { todo!() }\n",
            RULE_PUB_SIGNATURE,
        ),
        (
            VERSION_RS,
            "fn planted() { let _ = std::net::TcpStream::connect(\"127.0.0.1:1\"); }\n",
            RULE_CONFINED,
        ),
        (
            HTTP_RS,
            "fn planted() { let _ = ureq::Agent::config_builder(); }\n",
            RULE_ONE_AGENT,
        ),
        (
            HTTP_RS,
            "fn planted(other: ureq::config::Config) { let _ = ureq::Agent::new_with_config(other); }\n",
            RULE_ONE_AGENT,
        ),
        (
            FIXTURE_RS,
            "fn planted() { let _ = std::net::TcpStream::connect(\"127.0.0.1:1\"); }\n",
            RULE_DROP_CONNECT,
        ),
        (HTTP_RS, "use rquickjs::Runtime;\n", RULE_CONFINED),
        (
            PAC_RS,
            "pub fn planted() -> rquickjs::Runtime { todo!() }\n",
            RULE_PUB_SIGNATURE,
        ),
        (
            PAC_RS,
            "fn planted() { let _ = std::net::UdpSocket::bind(\"127.0.0.1:0\"); }\n",
            RULE_CONFINED,
        ),
        (
            PAC_RS,
            "fn planted() { let _ = (\"x\", 0).to_socket_addrs(); }\n",
            RULE_CONFINED,
        ),
        (
            OS_PROXY_RS,
            "fn planted(mut t: u32) { unsafe { GetTimeZoneInformation(&mut t) }; }\n",
            RULE_BANNED,
        ),
        (
            OS_PROXY_RS,
            "fn planted(s: u32, u: u32, o: u32, i: u32) { unsafe { WinHttpGetProxyForUrl(s, u, o, i) }; }\n",
            RULE_BANNED,
        ),
        (
            TAURI_LIB_RS,
            "fn planted(r: u32, u: u32, o: u32) { unsafe { WinHttpGetProxyForUrlEx(r, u, o, 0) }; }\n",
            RULE_CONFINED,
        ),
        (
            TAURI_LIB_RS,
            "use windows_sys::Win32::Foundation::GlobalFree;\n",
            RULE_CONFINED,
        ),
        (
            TAURI_LIB_RS,
            "use system_configuration::dynamic_store::SCDynamicStoreBuilder;\n",
            RULE_CONFINED,
        ),
    ];
    for (rel, text, rule) in plants {
        let planted = ureq_findings(&plant(&files, rel, text), &manifests);
        assert_reported(&base, &planted, rel, rule, text);
    }
    let planted_manifests = replace_in(
        &manifests,
        "src-tauri/Cargo.toml",
        "[dependencies]\n",
        "[dependencies]\nureq = { workspace = true }\n",
    );
    let planted = ureq_findings(&files, &planted_manifests);
    assert_reported(
        &base,
        &planted,
        "src-tauri/Cargo.toml",
        RULE_TOML_KEY,
        "ureq key",
    );

    // The lexer keeps `WinHttpGetProxyForUrlEx` apart from the banned `WinHttpGetProxyForUrl`.
    let quiet = "fn quiet(r: u32) { unsafe { WinHttpGetProxyForUrlEx(r, 0, 0, 0) }; }\n";
    let new = added(
        &base,
        &ureq_findings(&plant(&files, OS_PROXY_RS, quiet), &manifests),
    );
    assert!(new.is_empty(), "quiet os_proxy.rs text:\n{}", show(&new));
    assert!(base.is_empty(), "FR-081 findings:\n{}", show(&base));
}

#[test]
fn feed_model_seams_hold() {
    let src = app_root().join("crates").join("core").join("src");
    let feed = read_text(&src.join("feed.rs"));
    let version = read_text(&src.join("version.rs"));
    let base = feed_model_findings(&feed, &version);
    let resolve = ["fn resolve<", "fn resolve("]
        .into_iter()
        .find_map(|anchor| version.find(anchor))
        .expect("version.rs declares fn resolve");
    let body = resolve + version[resolve..].find('{').expect("fn resolve has a body") + 1;
    let second_native = format!(
        "{}\n    let _ = Arch::native();{}",
        &version[..body],
        &version[body..]
    );
    let plants: [(String, String, &str, &str); 7] = [
        (
            format!(
                "{feed}\nimpl AssetName {{\n    pub fn new(s: String) -> Self {{\n        AssetName(s)\n    }}\n}}\n"
            ),
            version.clone(),
            "feed.rs",
            "a `pub fn new` on AssetName",
        ),
        (
            feed.clone(),
            second_native,
            "version.rs",
            "a second `Arch::native()` in fn resolve",
        ),
        (
            replace_one(
                &feed,
                "pub download_url: Option<PageUrl>,",
                "pub download_url: Option<AssetUrl>,",
            ),
            version.clone(),
            "feed.rs",
            "`pub download_url: Option<AssetUrl>` in PluginEntry",
        ),
        (
            replace_one(
                &feed,
                "pub struct PlainName(String);",
                "#[derive(Default)]\npub struct PlainName(String);",
            ),
            version.clone(),
            "feed.rs",
            "`#[derive(Default)]` on PlainName",
        ),
        (
            format!(
                "{feed}\nimpl From<String> for PageUrl {{\n    fn from(s: String) -> Self {{\n        PageUrl(s)\n    }}\n}}\n"
            ),
            version.clone(),
            "feed.rs",
            "`impl From<String> for PageUrl`",
        ),
        (
            format!(
                "{feed}\nimpl PageUrl {{\n    pub(crate) fn asset(&self) -> AssetUrl {{\n        AssetUrl(self.0.clone())\n    }}\n}}\n"
            ),
            version.clone(),
            "feed.rs",
            "a PageUrl to AssetUrl conversion",
        ),
        (
            replace_one(
                &feed,
                "pub struct AssetUrl(String);",
                "pub struct AssetUrl(pub String);",
            ),
            version.clone(),
            "feed.rs",
            "a public field on AssetUrl",
        ),
    ];
    for (planted_feed, planted_version, file, what) in &plants {
        let planted = feed_model_findings(planted_feed, planted_version);
        let rule = if *file == "version.rs" {
            RULE_NATIVE
        } else {
            RULE_SEAM
        };
        assert_reported(&base, &planted, file, rule, what);
    }
    assert!(
        base.is_empty(),
        "FR-026 / FR-036 findings:\n{}",
        show(&base)
    );
}

#[test]
fn production_config_only_in_allowed_tests() {
    let files = workspace_rs_files();
    for must in [
        HTTP_TESTS_RS,
        FEED_TESTS_RS,
        PAC_TESTS_RS,
        SYSPROXY_TESTS_RS,
        SYSPROXY_CF_TESTS_RS,
        FIXTURE_RS,
        FIXTURE_TESTS_RS,
        CONTEXT_TESTS_RS,
        TEST_SUPPORT_RS,
        DETECT_TESTS_RS,
        STATUS_TESTS_RS,
    ] {
        assert!(
            files.iter().any(|(r, _)| r == must),
            "{must} was not scanned"
        );
    }
    let base = test_source_findings(&files);
    let program_plant = "fn planted() { let _ = std::process::Command::new(\"gsettings\"); }\n";
    let logger_plant = "fn planted() { let _ = log::set_logger(&CAPTURE); }\n";
    let plants: [(&str, &str, &str); 26] = [
        (
            HTTP_TESTS_RS,
            "#[test]\nfn planted() {\n    let _ = HttpConfig::production();\n}\n",
            RULE_TEST_BODY,
        ),
        (
            FEED_TESTS_RS,
            "fn planted() { let _ = UrlPolicy::HttpsOnly; }\n",
            RULE_TEST_BODY,
        ),
        (
            HTTP_TESTS_RS,
            "fn planted(c: HttpConfig) { let _ = c.with_proxy(None); }\n",
            RULE_TEST_BODY,
        ),
        (
            HTTP_TESTS_RS,
            "fn planted(c: HttpConfig) { let _ = c.with_pac(None); }\n",
            RULE_TEST_BODY,
        ),
        (
            FEED_TESTS_RS,
            "fn planted(c: HttpConfig) { let _ = c.with_pac_host(None); }\n",
            RULE_TEST_BODY,
        ),
        (
            SYSPROXY_TESTS_RS,
            "fn planted(p: NativePlatform, i: ProxyInputs) { let _ = p.system_proxy(&i); }\n",
            RULE_TEST_BANNED,
        ),
        (
            HTTP_TESTS_RS,
            "fn planted() { let _ = http::std_resolve(\"example.invalid\", Duration::from_secs(1)); }\n",
            RULE_TEST_BANNED,
        ),
        (
            HTTP_TESTS_RS,
            "fn planted() { let _ = http::std_my_ip(); }\n",
            RULE_TEST_BANNED,
        ),
        (
            PAC_TESTS_RS,
            "fn planted() { let _ = os_proxy::pac_host(); }\n",
            RULE_TEST_BANNED,
        ),
        (
            PAC_TESTS_RS,
            "fn planted() { let _ = os_proxy::OsPacHost; }\n",
            RULE_TEST_BANNED,
        ),
        (
            SYSPROXY_CF_TESTS_RS,
            "fn planted() { let _ = sysproxy_cf::system_utc_offset(); }\n",
            RULE_TEST_BANNED,
        ),
        (
            HTTP_TESTS_RS,
            "fn planted() { let _ = PacHostHandle::new(Arc::new(InertPacHost)); }\n",
            RULE_FAKE_HOST,
        ),
        (SYSPROXY_TESTS_RS, program_plant, RULE_COMMAND),
        (SYSPROXY_TESTS_RS, program_plant, RULE_GSETTINGS),
        (
            SYSPROXY_TESTS_RS,
            "fn planted() { let _ = std::process::Command::new(PROGRAM); }\n",
            RULE_COMMAND,
        ),
        // TA-1: `mkfifo` and `cmd` only in their one named body each.
        (
            DETECT_TESTS_RS,
            "fn planted() { let _ = std::process::Command::new(\"mkfifo\"); }\n",
            RULE_COMMAND,
        ),
        (
            STATUS_TESTS_RS,
            "fn planted() { let _ = std::process::Command::new(\"cmd\"); }\n",
            RULE_COMMAND,
        ),
        (
            DETECT_TESTS_RS,
            "fn probe_metadata_error_unix() { let _ = std::process::Command::new(\"cmd\"); }\n",
            RULE_COMMAND,
        ),
        (TEST_SUPPORT_RS, logger_plant, RULE_SET_LOGGER),
        (CONTEXT_TESTS_RS, logger_plant, RULE_SET_LOGGER),
        // FR-086.
        (
            FEED_TESTS_RS,
            "static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());\n",
            RULE_STATIC_LOCK,
        ),
        (
            HTTP_TESTS_RS,
            "static GATE: RwLock<()> = RwLock::new(());\n",
            RULE_STATIC_LOCK,
        ),
        (
            FIXTURE_RS,
            "static ROUTES: LazyLock<Mutex<Vec<u8>>> = LazyLock::new(|| { let v = Vec::new(); Mutex::new(v) });\n",
            RULE_STATIC_LOCK,
        ),
        (
            HTTP_TESTS_RS,
            "fn planted() { let _ = TcpListener::bind(\"127.0.0.1:0\"); }\n",
            RULE_LISTENER,
        ),
        (
            FIXTURE_TESTS_RS,
            "fn planted() { let _ = std::net::UdpSocket::bind(\"127.0.0.1:8080\"); }\n",
            RULE_FIXED_PORT,
        ),
        (
            FIXTURE_TESTS_RS,
            "fn planted() { let _ = std::net::UdpSocket::bind((\"127.0.0.1\", 8080)); }\n",
            RULE_FIXED_PORT,
        ),
    ];
    for (rel, text, rule) in plants {
        let planted = test_source_findings(&plant(&files, rel, text));
        assert_reported(&base, &planted, rel, rule, text);
    }

    // Allowed bodies, the fake's handle, the two programs, port 0 and a `'static` lifetime are
    // not findings.
    for (rel, text) in [
        (
            HTTP_TESTS_RS,
            "#[test]\nfn production_values() {\n    let _ = HttpConfig::production();\n    let _ = UrlPolicy::HttpsOnly;\n}\n",
        ),
        (
            FEED_TESTS_RS,
            "#[test]\nfn check_survives_pac_failure() {\n    let _ = HttpConfig::loopback(D, D).with_pac(None).with_pac_host(None);\n}\n",
        ),
        (
            PAC_TESTS_RS,
            "impl FakePacHost {\n    fn quiet(self) { let _ = PacHostHandle::new(Arc::new(self)); }\n}\n",
        ),
        (
            SYSPROXY_TESTS_RS,
            "fn quiet() { let _ = Command::new(\"sleep\"); let _ = Command::new(\"printf\"); }\n",
        ),
        (
            DETECT_TESTS_RS,
            "#[cfg(unix)]\n#[test]\nfn probe_metadata_error_unix() {\n    let _ = std::process::Command::new(\"mkfifo\");\n}\n\
             #[cfg(windows)]\n#[test]\nfn listing_skips_junction_windows() {\n    let _ = std::process::Command::new(\"cmd\");\n}\n",
        ),
        (
            FIXTURE_TESTS_RS,
            "fn quiet() { let _ = UdpSocket::bind(\"127.0.0.1:0\"); let _ = UdpSocket::bind((\"::1\", 0)); }\n",
        ),
        (
            FEED_TESTS_RS,
            "fn quiet() -> &'static Mutex<()> { todo!() }\nstatic NAME: &str = \"x\";\n",
        ),
    ] {
        let new = added(&base, &test_source_findings(&plant(&files, rel, text)));
        assert!(new.is_empty(), "{rel}: quiet {text:?}:\n{}", show(&new));
    }
    assert!(
        base.is_empty(),
        "FR-086 / FR-087 findings:\n{}",
        show(&base)
    );
}

#[test]
fn reader_dependencies_are_the_ruled_set() {
    let manifests = manifests();
    let base = reader_dependency_findings(&manifests);
    let plants: [(&str, &str, &str, &str); 5] = [
        (
            "Cargo.toml",
            "\"Win32_Networking_WinHttp\"]",
            "\"Win32_Networking_WinHttp\", \"Win32_System_Time\"]",
            "windows-sys: root features",
        ),
        (
            "src-tauri/Cargo.toml",
            "windows-sys = { workspace = true }",
            "windows-sys = { workspace = true }\nwinreg = \"0.55.0\"",
            "src-tauri/Cargo.toml [target.'cfg(windows)'.dependencies]: crates",
        ),
        (
            "crates/core/Cargo.toml",
            "rquickjs = { workspace = true }",
            "rquickjs = { workspace = true, features = [\"rust-alloc\"] }",
            "rquickjs: member entry in crates/core/Cargo.toml [target.'cfg(not(windows))'.dependencies] carries features",
        ),
        (
            "crates/core/Cargo.toml",
            "[dependencies]\n",
            "[dependencies]\nrquickjs = { workspace = true }\n",
            "rquickjs: declared in crates/core/Cargo.toml [dependencies]",
        ),
        (
            "crates/core/Cargo.toml",
            "[lints]",
            "[target.'cfg(windows)'.dependencies]\nrquickjs = { workspace = true }\n\n[lints]",
            "rquickjs: declared in crates/core/Cargo.toml [target.'cfg(windows)'.dependencies]",
        ),
    ];
    for (rel, from, to, prefix) in plants {
        let planted = reader_dependency_findings(&replace_in(&manifests, rel, from, to));
        let new = added(&base, &planted);
        assert!(
            new.iter().any(|line| line.starts_with(prefix)),
            "{rel}: plant {to:?} not reported as {prefix:?}; new:\n{}",
            new.join("\n")
        );
    }
    assert!(base.is_empty(), "SC-038 findings:\n{}", base.join("\n"));
}

#[test]
fn notes_and_plist_crates_are_confined() {
    let files = workspace_rs_files();
    for must in [
        NOTES_RS,
        NOTES_TESTS_RS,
        DETECT_RS,
        DETECT_TESTS_RS,
        STATUS_RS,
    ] {
        assert!(
            files.iter().any(|(r, _)| r == must),
            "{must} was not scanned"
        );
    }
    let base = crate_confinement_findings(&files);
    assert!(base.is_empty(), "FR-082 crate findings:\n{}", show(&base));
}

#[test]
fn host_io_symbols_are_confined() {
    let files = workspace_rs_files();
    for must in [
        OS_FACTS_RS,
        COMMANDS_RS,
        TAURI_LIB_RS,
        WINDOWS_RS,
        MACOS_RS,
        LINUX_RS,
        DETECT_RS,
    ] {
        assert!(
            files.iter().any(|(r, _)| r == must),
            "{must} was not scanned"
        );
    }
    // Not vacuous: each host literal is spelled once in its own platform file, so the value
    // reading of `literal_value` matches the source the scan sees.
    let lexed = lex(&files);
    for (needle, home) in HOST_LITERALS {
        let held = lexed
            .iter()
            .filter(|l| l.rel == home)
            .flat_map(|l| &l.literals)
            .filter(|lit| literal_value(lit) == needle)
            .count();
        assert_eq!(held, 1, "{home}: literals equal to {needle:?}");
    }
    let base = host_io_findings(&files);
    assert!(
        base.is_empty(),
        "FR-082 / FR-002 host findings:\n{}",
        show(&base)
    );
}

#[test]
fn confinement_scanner_reports_plants() {
    let files = workspace_rs_files();
    let findings = |files: &[(String, String)]| {
        let mut out = crate_confinement_findings(files);
        out.extend(host_io_findings(files));
        out
    };
    let base = findings(&files);
    let plants: [(&str, &str, &str); 15] = [
        (STATUS_RS, "use pulldown_cmark::Parser;\n", RULE_CONFINED),
        (
            DETECT_RS,
            "fn f() { let _ = plist::Value::Boolean(true); }\n",
            RULE_PLIST_SITE,
        ),
        (
            STATUS_RS,
            "fn f() { let _ = plist::Value::Boolean(true); }\n",
            RULE_PLIST_SITE,
        ),
        (
            DETECT_RS,
            "#[cfg(not(target_os = \"macos\"))]\nfn read_au_plist() { let _ = plist::Value::Boolean(true); }\n",
            RULE_PLIST_SITE,
        ),
        (
            DETECT_RS,
            "#[cfg(target_os = \"macos\")]\nfn read_other() { let _ = plist::Value::Boolean(true); }\n",
            RULE_PLIST_SITE,
        ),
        (
            COMMANDS_RS,
            "fn planted() { let _ = unsafe { RtlGetVersion(std::ptr::null_mut()) }; }\n",
            RULE_CONFINED,
        ),
        (
            OS_PROXY_RS,
            "fn planted() { let _ = unsafe { RegOpenKeyExW(0, 0, 0, 0, 0) }; }\n",
            RULE_CONFINED,
        ),
        (
            DETECT_RS,
            "fn planted() { let _ = RegGetValueW; }\n",
            RULE_CONFINED,
        ),
        (
            TAURI_LIB_RS,
            "fn planted() { let _ = RegCloseKey; }\n",
            RULE_CONFINED,
        ),
        (
            LINUX_RS,
            "const P: &str = \"/usr/sbin/pkgutil\";\n",
            RULE_HOST_LITERAL,
        ),
        (
            OS_FACTS_RS,
            "const G: &str = \"GNU_LIBC_VERSION\";\n",
            RULE_HOST_LITERAL,
        ),
        (
            MACOS_RS,
            "const K: &str = \"SOFTWARE\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\";\n",
            RULE_HOST_LITERAL,
        ),
        (
            COMMANDS_RS,
            "const K: &str = r\"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\";\n",
            RULE_HOST_LITERAL,
        ),
        (
            TAURI_LIB_RS,
            "fn planted() { let _ = os_facts::OsHostIo; }\n",
            RULE_CONFINED,
        ),
        (
            DETECT_RS,
            "const V: &str = \"Krate Audio\";\n",
            RULE_VENDOR_LITERAL,
        ),
    ];
    for (rel, text, rule) in plants {
        let planted = findings(&plant(&files, rel, text));
        assert_reported(&base, &planted, rel, rule, text);
    }

    // The macOS-gated AU reader, a `pub(crate)` one, comments and `platform::VENDOR_DIR` are not
    // findings.
    for (rel, text) in [
        (
            DETECT_RS,
            "#[cfg(target_os = \"macos\")]\nfn read_au_plist(b: &[u8]) { let _ = plist::Value::from_reader(b); }\n",
        ),
        (
            DETECT_RS,
            "#[cfg(target_os = \"macos\")]\n#[must_use]\npub(crate) fn read_au_plist() { let _ = plist::Value::Boolean(true); }\n",
        ),
        (
            STATUS_RS,
            "// pulldown_cmark::Parser, plist::Value, OsHostIo, RtlGetVersion, \"Krate Audio\"\n",
        ),
        (
            DETECT_RS,
            "fn quiet() -> &'static str { crate::platform::VENDOR_DIR }\n",
        ),
    ] {
        let new = added(&base, &findings(&plant(&files, rel, text)));
        assert!(new.is_empty(), "{rel}: quiet {text:?}:\n{}", show(&new));
    }
}

#[test]
fn test_sources_never_name_os_host_io() {
    let files = workspace_rs_files();
    for must in [
        TEST_SUPPORT_RS,
        DETECT_TESTS_RS,
        STATUS_TESTS_RS,
        NOTES_TESTS_RS,
        CONFIG_GUARD_RS,
    ] {
        assert!(
            files.iter().any(|(r, _)| r == must),
            "{must} was not scanned"
        );
    }
    // TA-2: the helper holding the fakes is scanned by name; `is_test_source` does not match it.
    assert!(!is_test_source(TEST_SUPPORT_RS));
    let base = test_host_io_findings(&files);
    assert!(base.is_empty(), "FR-083 findings:\n{}", show(&base));
}

#[test]
fn test_source_scanner_reports_planted_os_host_io() {
    let files = workspace_rs_files();
    let base = test_host_io_findings(&files);
    for (rel, text) in [
        (
            DETECT_TESTS_RS,
            "#[test]\nfn planted() {\n    let _ = OsHostIo;\n}\n",
        ),
        (TEST_SUPPORT_RS, "const P: &str = \"/usr/sbin/pkgutil\";\n"),
        (
            CONFIG_GUARD_RS,
            "fn planted() -> [&'static str; 2] { [\"/usr/bin/getconf\", \"GNU_LIBC_VERSION\"] }\n",
        ),
    ] {
        let planted = test_host_io_findings(&plant(&files, rel, text));
        assert_reported(&base, &planted, rel, RULE_TEST_HOST_IO, text);
    }
    // The re-exported constants, `FakeHostIo`, a comment and `injection.rs` itself are quiet.
    for (rel, text) in [
        (
            DETECT_TESTS_RS,
            "fn quiet(io: &FakeHostIo) -> [&'static str; 2] { let _ = io; [platform::PKGUTIL, platform::GETCONF] }\n",
        ),
        (
            STATUS_TESTS_RS,
            "// never OsHostIo, never /usr/sbin/pkgutil\n",
        ),
        (
            INJECTION_RS,
            "fn quiet() { let _ = OsHostIo; let _ = \"/usr/sbin/pkgutil\"; }\n",
        ),
    ] {
        let new = added(&base, &test_host_io_findings(&plant(&files, rel, text)));
        assert!(new.is_empty(), "{rel}: quiet {text:?}:\n{}", show(&new));
    }
}

#[test]
fn detect_status_notes_hold_no_write_or_listing_token() {
    let files = workspace_rs_files();
    let scanned: BTreeSet<&str> = files
        .iter()
        .map(|(r, _)| r.as_str())
        .filter(|r| is_fr084_file(r))
        .collect();
    let expected = BTreeSet::from([
        DETECT_RS,
        NOTES_RS,
        STATUS_RS,
        "crates/core/src/platform/mod.rs",
        WINDOWS_RS,
        MACOS_RS,
        LINUX_RS,
    ]);
    assert_eq!(scanned, expected, "FR-084 files");
    let base = write_token_findings(&files);
    assert!(base.is_empty(), "FR-084 findings:\n{}", show(&base));
}

#[test]
fn token_scanner_reports_planted_read_dir() {
    let files = workspace_rs_files();
    let base = write_token_findings(&files);
    let read_dir = "fn planted() { let _ = std::fs::read_dir(\"x\"); }\n";
    for (rel, text, rule) in [
        (STATUS_RS, read_dir, RULE_READ_DIR),
        (DETECT_RS, read_dir, RULE_READ_DIR),
        (
            STATUS_RS,
            "fn planted() { let _ = std::thread::spawn(|| ()); }\n",
            RULE_WRITE_TOKEN,
        ),
        (
            STATUS_RS,
            "#[derive(Serialize)]\npub struct Planted {\n    #[serde(rename = \"x\")]\n    pub y: u8,\n}\n",
            RULE_WRITE_TOKEN,
        ),
        (
            NOTES_RS,
            "fn planted() { let _ = std::fs::write(\"x\", b\"\"); }\n",
            RULE_WRITE_TOKEN,
        ),
        (
            LINUX_RS,
            "fn planted() { let _ = std::process::Command::new(GETCONF); }\n",
            RULE_WRITE_TOKEN,
        ),
    ] {
        let planted = write_token_findings(&plant(&files, rel, text));
        assert_reported(&base, &planted, rel, rule, text);
    }
    // The real `detect.rs`'s one `read_dir`, in `fn list_root`, is not reported; neither are
    // `rename_all`, `symlink_metadata` nor a comment.
    assert!(
        !base
            .iter()
            .any(|f| f.file == DETECT_RS && f.rule == RULE_READ_DIR),
        "detect.rs read_dir:\n{}",
        show(&base)
    );
    let quiet = "#[serde(rename_all = \"snake_case\")]\npub enum Q { A }\n\
        fn quiet(p: &Path) { let _ = p.symlink_metadata(); } // std::fs::write, Command\n";
    let new = added(
        &base,
        &write_token_findings(&plant(&files, STATUS_RS, quiet)),
    );
    assert!(new.is_empty(), "quiet status.rs text:\n{}", show(&new));
}

#[test]
fn test_sources_hold_no_real_location() {
    let texts = fr088_texts();
    assert_eq!(
        texts
            .iter()
            .map(|(rel, _)| rel.as_str())
            .collect::<Vec<_>>(),
        FR088_FILES,
        "FR-088 files"
    );
    let base = real_location_findings(&texts);
    assert!(base.is_empty(), "FR-088 findings:\n{}", base.join("\n"));
}

#[test]
fn real_location_scanner_reports_plants() {
    let texts = fr088_texts();
    let base = real_location_findings(&texts);
    let plants: [(&str, &str, &str); 11] = [
        (
            DETECT_TESTS_RS,
            "// cargo test -- --test-threads=1",
            "--test-threads",
        ),
        (
            STATUS_TESTS_RS,
            "let a = \"127.0.0.1:8080\";",
            r"127\.0\.0\.1:[0-9]+",
        ),
        (
            NOTES_TESTS_RS,
            "let u = \"http://localhost:3000/\";",
            r"localhost:[0-9]+",
        ),
        (TEST_SUPPORT_RS, "let _ = OsHostIo;", "OsHostIo"),
        (
            "src/status.test.ts",
            "const k = \"HKEY_LOCAL_MACHINE\";",
            "HKEY_",
        ),
        (
            "src/main.test.ts",
            "const p = \"/Library/Audio/Plug-Ins/VST3\";",
            "/Library",
        ),
        (DETECT_TESTS_RS, "let p = \"/usr/lib/vst3\";", "/usr/"),
        (STATUS_TESTS_RS, "let p = \"%LOCALAPPDATA%\";", "%[A-Z_]+%"),
        (NOTES_TESTS_RS, "let p = \"AppData\";", "AppData"),
        (TEST_SUPPORT_RS, "let p = \"ProgramData\";", "ProgramData"),
        (
            "src/status.test.ts",
            "const p = \"Program Files\";",
            "Program Files",
        ),
    ];
    let with = |rel: &str, text: Option<&str>| -> Vec<(String, Option<String>)> {
        let mut out = texts.clone();
        let entry = out
            .iter_mut()
            .find(|(r, _)| r == rel)
            .unwrap_or_else(|| panic!("plant target {rel} not found"));
        entry.1 = text.map(|t| format!("{}\n{t}\n", entry.1.as_deref().unwrap_or("")));
        out
    };
    for (rel, text, pattern) in plants {
        let new = added(&base, &real_location_findings(&with(rel, Some(text))));
        let (head, tail) = (format!("{rel}:"), format!(": `{pattern}`"));
        assert!(
            new.iter()
                .any(|f| f.starts_with(&head) && f.ends_with(&tail)),
            "{rel}: planted {text:?} not reported for {pattern:?}; new:\n{}",
            new.join("\n")
        );
    }
    // TA-9: a file the scan cannot read is a finding, not a pass.
    let new = added(&base, &real_location_findings(&with(NOTES_TESTS_RS, None)));
    assert!(
        new.contains(&format!("{NOTES_TESTS_RS}: missing")),
        "unreadable {NOTES_TESTS_RS} not reported; new:\n{}",
        new.join("\n")
    );
    // The `/fixture/` paths of the goldens (T005) and an injected root are not real locations.
    let quiet = "let p = PathBuf::from(\"/fixture/vst3-user/Alpha.vst3\");\nlet r = base.join(\"vst3-global\");";
    let new = added(
        &base,
        &real_location_findings(&with(STATUS_TESTS_RS, Some(quiet))),
    );
    assert!(
        new.is_empty(),
        "quiet status_tests.rs text:\n{}",
        new.join("\n")
    );
}
