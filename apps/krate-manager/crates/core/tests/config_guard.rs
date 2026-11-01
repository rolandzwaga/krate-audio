//! Static guard over the `src-tauri` configuration, the frontend sources and the app metadata
//! (spec FR-045, SC-015; plan §4.10, PD-23). It lives in core so its test binary links no
//! `tauri` (D12a). Every test reads files only: nothing is created, nothing is written.

use krate_manager_core::navigation::DEV_URL;
use krate_manager_core::platform::APP_IDENTIFIER;
use serde_json::{Value, json};
use std::fs;
use std::path::{Path, PathBuf};

/// The D13 content security policy, verbatim.
const D13_CSP: &str = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; \
    font-src 'self'; connect-src ipc: http://ipc.localhost; object-src 'none'; base-uri 'none'; \
    form-action 'none'; frame-src 'none'; frame-ancestors 'none'";
/// Tauri's IPC origin: the only http(s) source the CSP may name (D13).
const IPC_ORIGIN: &str = "http://ipc.localhost";
const ICONS: [&str; 5] = [
    "icons/32x32.png",
    "icons/128x128.png",
    "icons/128x128@2x.png",
    "icons/icon.icns",
    "icons/icon.ico",
];
/// The three platform files and the one bundle target each sets (D15).
const PLATFORM_TARGETS: [(&str, &str); 3] = [
    ("tauri.linux.conf.json", "deb"),
    ("tauri.macos.conf.json", "app"),
    ("tauri.windows.conf.json", "nsis"),
];
/// Phase 10 FR-092: the one more `tauri.*.conf.json`, the helper sidecar overlay `bundle.js` passes to `tauri build`.
const HELPER_CONF: &str = "tauri.helper.conf.json";
/// Phase 10 FR-105: the manual elevation example, compiled and linted, never run.
const EXAMPLE_LINES: [&str; 3] = [
    "name = \"manual_elevation\"",
    "path = \"examples/manual_elevation.rs\"",
    "required-features = [\"manual-elevation\"]",
];
/// Phase 10 SC-015: core's one `[[bin]]`, the never-shipped echo target of `windows_quoting.rs`.
const CORE_BIN_LINES: [&str; 3] = [
    "name = \"argv-echo\"",
    "path = \"tests/bin/argv_echo.rs\"",
    "test = false",
];
/// Phase 10 PD-8: core's dev-dependency on the helper, for the loopback (never a normal dependency, D1).
const CORE_HELPER_DEV_DEPENDENCY: &str =
    "krate-manager-helper = { workspace = true, features = [\"test-support\"] }";
const CAPABILITY_KEYS: [&str; 4] = ["identifier", "description", "windows", "permissions"];
/// FR-004's `[dependencies]` key set of `src-tauri/Cargo.toml` (PD-23), narrowed by Phase 6 Q8 to admit the opener
/// crate (SI-2).
const SRC_TAURI_DEPENDENCIES: [&str; 6] = [
    "krate-manager-core",
    "log",
    "serde",
    "serde_json",
    "tauri",
    "tauri-plugin-opener",
];
/// The one `src-tauri/Cargo.toml` line, inside `[dependencies]`, that may name a tauri-plugin crate (SI-2).
const OPENER_DEPENDENCY_LINE: &str = "tauri-plugin-opener = { workspace = true }";
/// The capability's exact description (FR-061 as amended by SI-21).
const CAPABILITY_DESCRIPTION: &str = "The main window may call only the app's own commands.";
/// The strings `src-tauri/src/lib.rs` must contain (FR-017, FR-042, FR-044, FR-061, FR-101).
const LIB_RS_STRINGS: [&str; 15] = [
    ".plugin(navigation::guard())",
    "assemble_context(",
    ".manage(",
    "SharedState",
    "startup_notice",
    "startup_error",
    "LOCKED_EXIT_DELAY",
    "HttpConfig::production()",
    "resolve_proxy(",
    ".with_proxy(",
    ".with_pac(",
    ".with_pac_host(",
    "os_proxy::pac_host()",
    "proxy_env::read()",
    "SigningKey::release()",
];
/// (token, the one `src-tauri/` file it may occur in, the rule) (plan §4.11.2, PD-12, SI-3, SI-26).
const CONFINED: [(&str, &str, &str); 11] = [
    ("config_for_dev_url", "src/dev_feed.rs", "FR-061, PD-12"),
    ("LoopbackOnly", "src/dev_feed.rs", "FR-061, PD-12"),
    ("HttpConfig::loopback", "src/dev_feed.rs", "FR-061, PD-12"),
    (".with_proxy(", "src/lib.rs", "FR-063"),
    (".with_pac(", "src/lib.rs", "FR-063"),
    (".with_pac_host(", "src/lib.rs", "FR-063, SI-26"),
    ("resolve_proxy(", "src/lib.rs", "FR-063"),
    ("OsPacHost", "src/os_proxy.rs", "FR-063, SI-26"),
    ("HTTPS_PROXY", "src/proxy_env.rs", "FR-063, SI-3"),
    ("ALL_PROXY", "src/proxy_env.rs", "FR-063, SI-3"),
    ("NO_PROXY", "src/proxy_env.rs", "FR-063, SI-3"),
];
/// The proxy variable names no `crates/core/src` file may hold (FR-063, SI-3; case-sensitive).
const PROXY_ENV_NAMES: [&str; 3] = ["HTTPS_PROXY", "ALL_PROXY", "NO_PROXY"];
/// `proxy_env.rs`'s six `ProxyVars` field-to-variable pairs (plan §4.10, §4.11.2).
const PROXY_ENV_PAIRS: [(&str, &str); 6] = [
    ("https_upper", "HTTPS_PROXY"),
    ("https_lower", "https_proxy"),
    ("all_upper", "ALL_PROXY"),
    ("all_lower", "all_proxy"),
    ("no_upper", "NO_PROXY"),
    ("no_lower", "no_proxy"),
];
/// `src-tauri/Cargo.toml`'s target sections and the one line each holds (FR-073, TA-15).
const SRC_TAURI_TARGETS: [(&str, &str); 2] = [
    (
        "[target.'cfg(windows)'.dependencies]",
        "windows-sys = { workspace = true }",
    ),
    (
        "[target.'cfg(target_os = \"macos\")'.dependencies]",
        "system-configuration = { workspace = true }",
    ),
];
/// The workspace members of FR-002, in order.
const MEMBERS: [&str; 3] = ["crates/core", "crates/helper", "src-tauri"];
/// The two library members that must not depend on the opener crate (FR-065).
const LIBRARY_MEMBERS: [&str; 2] = ["crates/core", "crates/helper"];
/// The opener crate's identifier and the one file it may occur in (FR-065, SI-15).
const OPENER_CRATE: &str = "tauri_plugin_opener";
const OPENER_FILE: &str = "src-tauri/src/commands.rs";
/// What no Rust source may hold: the opener plugin is never registered, so no source builds it
/// or names its extension trait (FR-065, SI-15, PD-20).
const OPENER_REGISTRATION: [&str; 3] = [
    "tauri_plugin_opener::init",
    "tauri_plugin_opener::Builder",
    "OpenerExt",
];
/// The npm counterpart, which the webview never imports (FR-065).
const OPENER_NPM: &str = "@tauri-apps/plugin-opener";
/// FR-062: the Keep a Changelog header the plugin changelogs use (their lines 1-6), with the
/// product name changed.
const CHANGELOG_HEADER: [&str; 6] = [
    "# Changelog",
    "",
    "All notable changes to Krate Manager will be documented in this file.",
    "",
    "The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),",
    "and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).",
];
/// FR-051: the compiler options that must all be `true`.
const STRICT_OPTIONS: [&str; 6] = [
    "strict",
    "noUnusedLocals",
    "noUnusedParameters",
    "noFallthroughCasesInSwitch",
    "noUncheckedIndexedAccess",
    "exactOptionalPropertyTypes",
];
/// FR-052: the texts `eslint.config.mjs` must contain.
const ESLINT_TEXTS: [&str; 3] = [
    "reportUnusedDisableDirectives: \"error\"",
    "tseslint.configs.strictTypeChecked",
    "projectService: true",
];
/// Frontend strings that fail the source scan wherever they appear (FR-045, FR-052).
const FRONTEND_BANNED: [&str; 6] = [
    "@ts-ignore",
    "@ts-nocheck",
    "innerHTML",
    "outerHTML",
    "insertAdjacentHTML",
    "document.write",
];
/// What no `src/**/*.css` line may hold, ASCII case-insensitive: a font, image or URL (FR-077).
const STYLESHEET_BANNED: [&str; 4] = ["url(", "@font-face", "@import", "image-set("];
/// The squeezed `generate_handler!` list `src-tauri/src/lib.rs` must hold, in order (FR-061, SC-019).
const HANDLER_LIST: &str = "generate_handler![commands::app_version,commands::startup_error,commands::plugin_status,commands::check_now,commands::open_release_page,commands::download_plugin,commands::install_plugin,commands::remove_plugin]";
/// Phase 9 FR-096: the commands whose parameters may be only `State`, `String` and `Channel<InstallProgress>`.
const INSTALL_COMMANDS: [&str; 2] = ["install_plugin", "remove_plugin"];
/// The crates no member's normal dependency graph may reach: the signing crate and the two it
/// pulls in for key generation (FR-081).
const SIGNING_CRATES: [&str; 3] = ["minisign", "rpassword", "scrypt"];

/// Everything `check_config` reads from `src-tauri/` and the workspace root.
#[derive(Clone)]
struct ConfigFiles {
    conf: Value,
    /// `tauri.<x>.conf.json` beside `tauri.conf.json`: (file name, parsed).
    platform_confs: Vec<(String, Value)>,
    /// Every file under `capabilities/`: (path below `capabilities/`, parsed; `Null` if not JSON).
    capabilities: Vec<(String, Value)>,
    build_rs: String,
    cargo_toml: String,
    workspace_cargo_toml: String,
    lib_rs: String,
    /// The non-test `.rs` files of `src-tauri/src/` other than `lib.rs` (which is `lib_rs`).
    src_tauri_texts: Vec<(String, String)>,
    /// Every file under `src-tauri/`, as bytes (the icons are binary): ('/'-separated path, bytes).
    src_tauri_files: Vec<(String, Vec<u8>)>,
    /// The `.rs` files below `crates/core/src/`: (`src/`-prefixed '/'-separated path, text).
    core_src_texts: Vec<(String, String)>,
    tests_dir_exists: bool,
    info_plist_exists: bool,
}

/// Everything `check_app_meta` reads from the app root (PD-23).
#[derive(Clone)]
struct AppMetaFiles {
    workspace_cargo_toml: String,
    /// (member directory, its `Cargo.toml`) for the three FR-002 members.
    member_manifests: Vec<(String, String)>,
    package_json: Value,
    tsconfig: Value,
    eslint_config: String,
    changelog: String,
    /// `version.json`'s `version`.
    version: String,
}

/// Everything `check_signing_crates` reads (FR-081).
#[derive(Clone)]
struct LockFiles {
    /// (member directory, its `Cargo.toml`) for the three FR-002 members.
    member_manifests: Vec<(String, String)>,
    /// The committed workspace `Cargo.lock`.
    lock: String,
}

/// Everything `check_opener` reads (FR-065 as amended by SI-15).
#[derive(Clone)]
struct OpenerFiles {
    /// Every `.rs` file below `src-tauri/src/`, `crates/core/src/` and `crates/helper/src/`:
    /// (app-relative '/'-separated path, text).
    rust_sources: Vec<(String, String)>,
    /// Every file under `src-tauri/capabilities/`: (path below `capabilities/`, parsed; `Null` if not JSON).
    capabilities: Vec<(String, Value)>,
    /// (member directory, its `Cargo.toml`) for `crates/core` and `crates/helper`.
    library_manifests: Vec<(String, String)>,
    package_json: Value,
    conf: Value,
    /// `src/**`: ('/'-separated path, text).
    frontend: Vec<(String, String)>,
}

// ---------------------------------------------------------------------------------------------
// Loading (read only)

fn app_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("..").join("..")
}

fn src_tauri_dir() -> PathBuf {
    app_root().join("src-tauri")
}

fn read_text(path: &Path) -> String {
    fs::read_to_string(path).unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()))
}

fn read_json(path: &Path) -> Value {
    serde_json::from_str(&read_text(path))
        .unwrap_or_else(|e| panic!("{} is not JSON: {e}", path.display()))
}

/// Appends every file below `dir` as (`prefix`-relative '/'-separated path, bytes), sorted.
fn walk(dir: &Path, prefix: &str, out: &mut Vec<(String, Vec<u8>)>) {
    let entries = fs::read_dir(dir)
        .unwrap_or_else(|e| panic!("cannot read directory {}: {e}", dir.display()));
    let mut entries: Vec<_> = entries
        .map(|entry| entry.unwrap_or_else(|e| panic!("cannot list {}: {e}", dir.display())))
        .collect();
    entries.sort_by_key(|entry| entry.file_name());
    for entry in entries {
        let name = entry.file_name().to_string_lossy().into_owned();
        let rel = if prefix.is_empty() {
            name
        } else {
            format!("{prefix}/{name}")
        };
        let path = entry.path();
        if path.is_dir() {
            walk(&path, &rel, out);
        } else {
            let bytes =
                fs::read(&path).unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()));
            out.push((rel, bytes));
        }
    }
}

fn file_text(files: &[(String, Vec<u8>)], rel: &str) -> String {
    let (_, bytes) = files
        .iter()
        .find(|(name, _)| name == rel)
        .unwrap_or_else(|| panic!("src-tauri/{rel} does not exist"));
    String::from_utf8(bytes.clone()).unwrap_or_else(|e| panic!("src-tauri/{rel} is not UTF-8: {e}"))
}

fn load_config() -> ConfigFiles {
    let dir = src_tauri_dir();
    let mut files = Vec::new();
    walk(&dir, "", &mut files);
    let platform_confs = files
        .iter()
        .filter(|(rel, _)| {
            !rel.contains('/')
                && rel.starts_with("tauri.")
                && rel.ends_with(".conf.json")
                && rel != "tauri.conf.json"
        })
        .map(|(rel, _)| (rel.clone(), read_json(&dir.join(rel))))
        .collect();
    let capabilities = files
        .iter()
        .filter_map(|(rel, bytes)| {
            let name = rel.strip_prefix("capabilities/")?;
            Some((
                name.to_owned(),
                serde_json::from_slice(bytes).unwrap_or(Value::Null),
            ))
        })
        .collect();
    let src_tauri_texts = files
        .iter()
        .filter(|(rel, _)| {
            rel.starts_with("src/")
                && rel.ends_with(".rs")
                && !rel.ends_with("_tests.rs")
                && rel != "src/lib.rs"
        })
        .map(|(rel, _)| (rel.clone(), file_text(&files, rel)))
        .collect();
    let info_plist_exists = files.iter().any(|(rel, _)| {
        rel.rsplit('/')
            .next()
            .is_some_and(|name| name.eq_ignore_ascii_case("Info.plist"))
    });
    let mut core_files = Vec::new();
    walk(
        &Path::new(env!("CARGO_MANIFEST_DIR")).join("src"),
        "src",
        &mut core_files,
    );
    let core_src_texts = core_files
        .into_iter()
        .filter(|(rel, _)| rel.ends_with(".rs"))
        .map(|(rel, bytes)| {
            let text = String::from_utf8(bytes)
                .unwrap_or_else(|e| panic!("crates/core/{rel} is not UTF-8: {e}"));
            (rel, text)
        })
        .collect();
    ConfigFiles {
        conf: read_json(&dir.join("tauri.conf.json")),
        platform_confs,
        capabilities,
        build_rs: file_text(&files, "build.rs"),
        cargo_toml: file_text(&files, "Cargo.toml"),
        workspace_cargo_toml: read_text(&app_root().join("Cargo.toml")),
        lib_rs: file_text(&files, "src/lib.rs"),
        src_tauri_texts,
        core_src_texts,
        tests_dir_exists: dir.join("tests").exists(),
        info_plist_exists,
        src_tauri_files: files,
    }
}

/// `src/**` and `index.html` of the app root, as ('/'-separated path, text).
fn load_frontend() -> Vec<(String, String)> {
    let root = app_root();
    let mut files = Vec::new();
    walk(&root.join("src"), "src", &mut files);
    files.push((
        "index.html".to_owned(),
        read_text(&root.join("index.html")).into_bytes(),
    ));
    files
        .into_iter()
        .map(|(rel, bytes)| {
            let text =
                String::from_utf8(bytes).unwrap_or_else(|e| panic!("{rel} is not UTF-8: {e}"));
            (rel, text)
        })
        .collect()
}

fn load_app_meta() -> AppMetaFiles {
    let root = app_root();
    let version_json = read_json(&root.join("version.json"));
    let version = version_json
        .get("version")
        .and_then(Value::as_str)
        .expect("version.json has no string `version`")
        .to_owned();
    AppMetaFiles {
        workspace_cargo_toml: read_text(&root.join("Cargo.toml")),
        member_manifests: MEMBERS
            .iter()
            .map(|member| {
                (
                    (*member).to_owned(),
                    read_text(&root.join(member).join("Cargo.toml")),
                )
            })
            .collect(),
        package_json: read_json(&root.join("package.json")),
        tsconfig: read_json(&root.join("tsconfig.json")),
        eslint_config: read_text(&root.join("eslint.config.mjs")),
        changelog: read_text(&root.join("CHANGELOG.md")),
        version,
    }
}

fn load_lock_files() -> LockFiles {
    let root = app_root();
    LockFiles {
        member_manifests: MEMBERS
            .iter()
            .map(|member| {
                (
                    (*member).to_owned(),
                    read_text(&root.join(member).join("Cargo.toml")),
                )
            })
            .collect(),
        lock: read_text(&root.join("Cargo.lock")),
    }
}

fn load_opener() -> OpenerFiles {
    let root = app_root();
    let mut files = Vec::new();
    for dir in ["src-tauri/src", "crates/core/src", "crates/helper/src"] {
        walk(&root.join(dir), dir, &mut files);
    }
    let rust_sources = files
        .into_iter()
        .filter(|(rel, _)| rel.ends_with(".rs"))
        .map(|(rel, bytes)| {
            let text =
                String::from_utf8(bytes).unwrap_or_else(|e| panic!("{rel} is not UTF-8: {e}"));
            (rel, text)
        })
        .collect();
    let config = load_config();
    OpenerFiles {
        rust_sources,
        capabilities: config.capabilities,
        library_manifests: LIBRARY_MEMBERS
            .iter()
            .map(|member| {
                (
                    (*member).to_owned(),
                    read_text(&root.join(member).join("Cargo.toml")),
                )
            })
            .collect(),
        package_json: read_json(&root.join("package.json")),
        conf: config.conf,
        frontend: load_frontend()
            .into_iter()
            .filter(|(rel, _)| rel.starts_with("src/"))
            .collect(),
    }
}

// ---------------------------------------------------------------------------------------------
// Text helpers (`str::lines()` throughout, so CRLF checkouts read like LF ones)

/// The trimmed, non-blank, non-comment lines of every `header` section of a `Cargo.toml` text.
fn section_lines<'a>(toml: &'a str, header: &str) -> Vec<&'a str> {
    let mut out = Vec::new();
    let mut inside = false;
    for line in toml.lines() {
        let line = line.trim();
        if line.starts_with('[') {
            inside = line == header;
        } else if inside && !line.is_empty() && !line.starts_with('#') {
            out.push(line);
        }
    }
    out
}

/// The key of a `key = value` line, up to its first `.` (`tauri.workspace = true` is `tauri`).
fn key_of(line: &str) -> &str {
    let before_eq = line.split('=').next().unwrap_or_default().trim();
    before_eq.split('.').next().unwrap_or_default().trim()
}

/// The quoted strings of the array value of `key` in `lines` (the value may span lines).
fn string_array(lines: &[&str], key: &str) -> Option<Vec<String>> {
    let start = lines
        .iter()
        .position(|line| line.contains('=') && key_of(line) == key)?;
    let mut text = String::new();
    for line in &lines[start..] {
        text.push_str(line);
        if line.contains(']') {
            break;
        }
    }
    let (_, value) = text.split_once('=')?;
    Some(
        value
            .split('"')
            .skip(1)
            .step_by(2)
            .map(str::to_owned)
            .collect(),
    )
}

/// The first quoted string of the value of `key` in `lines`.
fn string_value(lines: &[&str], key: &str) -> Option<String> {
    let line = lines
        .iter()
        .find(|line| line.contains('=') && key_of(line) == key)?;
    let (_, value) = line.split_once('=')?;
    value.split('"').nth(1).map(str::to_owned)
}

fn find_bytes(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack
        .windows(needle.len())
        .position(|window| window == needle)
}

/// Phase 9 FR-096: `install_plugin` and `remove_plugin` in `commands.rs` take only `State`, `String` and
/// `Channel<InstallProgress>` parameters, so no path (`Path`, `PathBuf`, `OsString`) reaches the core from the webview.
fn check_install_command_params(f: &ConfigFiles) -> Vec<String> {
    let Some((_, text)) = f
        .src_tauri_texts
        .iter()
        .find(|(rel, _)| rel == "src/commands.rs")
    else {
        return vec!["src-tauri/src/commands.rs is missing (FR-096)".to_owned()];
    };
    let squeezed: String = text.chars().filter(|ch| !ch.is_whitespace()).collect();
    let mut m = Vec::new();
    for name in INSTALL_COMMANDS {
        let head = format!("fn{name}(");
        let Some(at) = squeezed.find(&head) else {
            m.push(format!(
                "src-tauri/src/commands.rs has no `fn {name}(` (FR-096)"
            ));
            continue;
        };
        for param in top_level_params(&squeezed[at + head.len()..]) {
            let ty = param.split_once(':').map_or(param, |(_, ty)| ty);
            let allowed = ty.starts_with("tauri::State<")
                || ty.starts_with("State<")
                || ty == "String"
                || ty == "tauri::ipc::Channel<InstallProgress>"
                || ty == "Channel<InstallProgress>";
            if !allowed {
                m.push(format!(
                    "src-tauri/src/commands.rs `{name}` takes `{param}`, expected only State, String and Channel<InstallProgress> parameters (FR-096)"
                ));
            }
        }
    }
    m
}

/// The top-level comma-separated items of a whitespace-free parameter list, up to its closing `)`.
fn top_level_params(rest: &str) -> Vec<&str> {
    let mut out = Vec::new();
    let mut depth = 0usize;
    let mut start = 0;
    for (i, ch) in rest.char_indices() {
        match ch {
            '<' | '(' | '[' => depth += 1,
            '>' | ']' => depth = depth.saturating_sub(1),
            ')' if depth == 0 => {
                if start < i {
                    out.push(&rest[start..i]);
                }
                return out;
            }
            ')' => depth -= 1,
            ',' if depth == 0 => {
                out.push(&rest[start..i]);
                start = i + 1;
            }
            _ => {}
        }
    }
    out
}

fn show(value: Option<&Value>) -> String {
    value.map_or_else(|| "absent".to_owned(), Value::to_string)
}

// ---------------------------------------------------------------------------------------------
// The checks

fn check_config(f: &ConfigFiles) -> Vec<String> {
    check_config_against(f, APP_IDENTIFIER)
}

/// `check_config` with the core's identifier passed in, so a test can plant an unequal one.
fn check_config_against(f: &ConfigFiles, app_identifier: &str) -> Vec<String> {
    let mut m = Vec::new();
    let conf = &f.conf;

    // FR-005 keys and values.
    let expected_values = [
        ("/productName", json!("Krate Manager")),
        ("/identifier", json!("com.krateaudio.krate-manager")),
        ("/build/devUrl", json!("http://localhost:1420")),
        ("/build/beforeDevCommand", json!("npm run dev")),
        ("/build/beforeBuildCommand", json!("npm run build")),
        ("/build/frontendDist", json!("../dist")),
        ("/app/withGlobalTauri", json!(false)),
        ("/app/security/csp", json!(D13_CSP)),
        ("/bundle/active", json!(true)),
        ("/bundle/icon", json!(ICONS)),
        ("/bundle/macOS/signingIdentity", json!("-")),
        ("/bundle/macOS/minimumSystemVersion", json!("15.0")),
        ("/bundle/windows/nsis/installMode", json!("currentUser")),
    ];
    for (pointer, want) in expected_values {
        let got = conf.pointer(pointer);
        if got != Some(&want) {
            m.push(format!(
                "tauri.conf.json {pointer} is {}, expected {want} (FR-005)",
                show(got)
            ));
        }
    }
    let workspace_version = string_value(
        &section_lines(&f.workspace_cargo_toml, "[workspace.package]"),
        "version",
    );
    let conf_version = conf.pointer("/version").and_then(Value::as_str);
    if conf_version.is_none() || conf_version != workspace_version.as_deref() {
        m.push(format!(
            "tauri.conf.json /version is {}, expected the workspace version {workspace_version:?} (FR-005)",
            show(conf.pointer("/version"))
        ));
    }
    if conf.pointer("/build/devUrl").and_then(Value::as_str) != Some(DEV_URL) {
        m.push(format!(
            "tauri.conf.json /build/devUrl differs from navigation::DEV_URL {DEV_URL} (FR-045)"
        ));
    }
    if conf.pointer("/identifier").and_then(Value::as_str) != Some(app_identifier) {
        m.push(format!(
            "tauri.conf.json /identifier is {}, platform::APP_IDENTIFIER is {app_identifier} (D7)",
            show(conf.pointer("/identifier"))
        ));
    }
    let one_main_window = conf
        .pointer("/app/windows")
        .and_then(Value::as_array)
        .is_some_and(|windows| {
            windows.len() == 1 && windows[0].get("label") == Some(&json!("main"))
        });
    if !one_main_window {
        m.push(
            "tauri.conf.json /app/windows is not exactly one window labelled main (FR-005, PD-21)"
                .to_owned(),
        );
    }
    let must_be_absent = [
        "/app/security/devCsp",
        "/app/security/capabilities",
        "/bundle/macOS/infoPlist",
        "/bundle/createUpdaterArtifacts",
        "/bundle/externalBin",
        "/bundle/targets",
        "/plugins",
    ];
    for pointer in must_be_absent {
        if conf.pointer(pointer).is_some() {
            m.push(format!(
                "tauri.conf.json has {pointer}, which must be absent (FR-005, D15)"
            ));
        }
    }
    check_csp(
        conf.pointer("/app/security/csp")
            .and_then(Value::as_str)
            .unwrap_or_default(),
        &mut m,
    );

    // D15: the platform files set only bundle.targets; Phase 10 FR-092: the helper overlay sets only the sidecar.
    let helper_conf = json!({ "bundle": { "externalBin": ["binaries/krate-manager-helper"] } });
    match f
        .platform_confs
        .iter()
        .find(|(file, _)| file == HELPER_CONF)
    {
        None => m.push(format!("src-tauri/{HELPER_CONF} is missing (FR-092)")),
        Some((_, got)) if *got != helper_conf => {
            m.push(format!(
                "src-tauri/{HELPER_CONF} is {got}, expected {helper_conf} (FR-092)"
            ));
        }
        Some(_) => {}
    }
    for (name, _) in &f.platform_confs {
        if name != HELPER_CONF && !PLATFORM_TARGETS.iter().any(|(known, _)| known == name) {
            m.push(format!(
                "src-tauri/{name} is not one of the three platform files (D15)"
            ));
        }
    }
    for (name, target) in PLATFORM_TARGETS {
        let want = json!({ "bundle": { "targets": [target] } });
        match f.platform_confs.iter().find(|(file, _)| file == name) {
            None => m.push(format!("src-tauri/{name} is missing (D15)")),
            Some((_, got)) if *got != want => {
                m.push(format!("src-tauri/{name} is {got}, expected {want} (D15)"));
            }
            Some(_) => {}
        }
    }

    // FR-041: capabilities/ holds exactly main.json.
    let capability_names: Vec<&str> = f
        .capabilities
        .iter()
        .map(|(name, _)| name.as_str())
        .collect();
    if capability_names != ["main.json"] {
        m.push(format!("src-tauri/capabilities/ holds {capability_names:?}, expected exactly [\"main.json\"] (FR-041)"));
    }
    if let Some((_, capability)) = f.capabilities.iter().find(|(name, _)| name == "main.json") {
        match capability.as_object() {
            None => m.push("capabilities/main.json is not a JSON object (FR-041)".to_owned()),
            Some(object) => {
                for key in object.keys() {
                    if !CAPABILITY_KEYS.contains(&key.as_str()) {
                        m.push(format!("capabilities/main.json has the key `{key}` (FR-041: no remote, platforms or webviews)"));
                    }
                }
                let expected = [
                    ("identifier", json!("main")),
                    ("windows", json!(["main"])),
                    (
                        "permissions",
                        json!([
                            "allow-app-version",
                            "allow-startup-error",
                            "allow-plugin-status",
                            "allow-check-now",
                            "allow-open-release-page",
                            "allow-download-plugin",
                            "allow-install-plugin",
                            "allow-remove-plugin"
                        ]),
                    ),
                ];
                for (key, want) in expected {
                    if object.get(key) != Some(&want) {
                        m.push(format!(
                            "capabilities/main.json `{key}` is {}, expected {want} (FR-041, FR-061)",
                            show(object.get(key))
                        ));
                    }
                }
                let description = json!(CAPABILITY_DESCRIPTION);
                if object.get("description") != Some(&description) {
                    m.push(format!(
                        "capabilities/main.json `description` is {}, expected {description} (FR-061, SI-21)",
                        show(object.get("description"))
                    ));
                }
            }
        }
    }

    // FR-040, Phase 6 FR-060, Phase 7 FR-061, Phase 9 FR-096: build.rs registers exactly the eight commands
    // (whitespace-insensitive).
    let squeezed: String = f
        .build_rs
        .chars()
        .filter(|ch| !ch.is_whitespace())
        .collect();
    if !squeezed.replace(",]", "]").contains(
        r#"commands(&["app_version","startup_error","plugin_status","check_now","open_release_page","download_plugin","install_plugin","remove_plugin"])"#,
    ) {
        m.push(
            "src-tauri/build.rs does not register exactly app_version, startup_error, plugin_status, check_now, open_release_page, download_plugin, install_plugin and remove_plugin (FR-040, FR-060, FR-061, FR-096)"
                .to_owned(),
        );
    }

    // FR-061, SC-019, Phase 9 FR-096: lib.rs hands exactly the eight commands to the invoke handler
    // (whitespace-insensitive).
    let squeezed_lib: String = f.lib_rs.chars().filter(|ch| !ch.is_whitespace()).collect();
    if !squeezed_lib.replace(",]", "]").contains(HANDLER_LIST) {
        m.push(
            "src-tauri/src/lib.rs generate_handler! does not list exactly app_version, startup_error, plugin_status, check_now, open_release_page, download_plugin, install_plugin and remove_plugin (FR-061, SC-019, FR-096)"
                .to_owned(),
        );
    }
    m.extend(check_install_command_params(f));

    check_src_tauri_manifest(f, &mut m);
    m.extend(check_http_injection(f));

    // FR-017, FR-042, FR-044, FR-101, FR-105: the lib.rs wiring.
    for needle in LIB_RS_STRINGS {
        if !f.lib_rs.contains(needle) {
            m.push(format!(
                "src-tauri/src/lib.rs does not contain `{needle}` (FR-045)"
            ));
        }
    }
    match (f.lib_rs.find("assemble_context("), f.lib_rs.find("log_sink::init(")) {
        (Some(assemble), Some(init)) if init > assemble => {}
        _ => m.push("src-tauri/src/lib.rs does not call `log_sink::init(` after `assemble_context(` (FR-105)".to_owned()),
    }
    for needle in ["set_logger", "eprintln!"] {
        let count = f.lib_rs.matches(needle).count()
            + f.src_tauri_texts
                .iter()
                .map(|(_, text)| text.matches(needle).count())
                .sum::<usize>();
        if count != 1 {
            m.push(format!(
                "src-tauri/src holds {count} `{needle}`, expected exactly one (FR-045)"
            ));
        }
    }

    // FR-004, FR-005, FR-106, FR-107: what must not exist under src-tauri/.
    if f.tests_dir_exists {
        m.push("src-tauri/tests/ exists; src-tauri has no tests (FR-004, D12a)".to_owned());
    }
    if f.info_plist_exists {
        m.push("an Info.plist exists under src-tauri/ (FR-005)".to_owned());
    }
    for (rel, bytes) in &f.src_tauri_files {
        if let Some(offset) = find_bytes(bytes, b"LSFileQuarantineEnabled") {
            let generated = if rel.starts_with("gen/") {
                " (a tauri-build output: STOP AND SURFACE to the user, tasks.md T011)"
            } else {
                ""
            };
            m.push(format!(
                "src-tauri/{rel} contains LSFileQuarantineEnabled at byte offset {offset}{generated} (FR-045)"
            ));
        }
        let text = String::from_utf8_lossy(bytes);
        for (index, line) in text.lines().enumerate() {
            let names_dev_feed = line
                .find("features")
                .is_some_and(|at| line[at..].contains("dev-feed"));
            if names_dev_feed {
                m.push(format!(
                    "src-tauri/{rel}:{}: a `features` key names dev-feed (FR-106)",
                    index + 1
                ));
            }
        }
    }
    m
}

/// D13: no http(s) source but the IPC origin, no `'unsafe-inline'` / `'unsafe-eval'`, no `*`.
fn check_csp(csp: &str, m: &mut Vec<String>) {
    let tokens = csp
        .split(|ch: char| ch == ';' || ch.is_whitespace())
        .filter(|token| !token.is_empty());
    for token in tokens {
        let lower = token.to_ascii_lowercase();
        let http_source = lower.starts_with("http:") || lower.starts_with("https:");
        if http_source && lower != IPC_ORIGIN {
            m.push(format!(
                "CSP source `{token}` is not allowed: the only http(s) source is {IPC_ORIGIN} (D13)"
            ));
        }
        if lower == "'unsafe-inline'" || lower == "'unsafe-eval'" {
            m.push(format!("CSP has {token} (D13)"));
        }
        if token.contains('*') {
            m.push(format!("CSP has the wildcard source `{token}` (D13)"));
        }
    }
}

/// FR-004, FR-106, PD-23: the `src-tauri/Cargo.toml` and root `[workspace.dependencies]` facts.
fn check_src_tauri_manifest(f: &ConfigFiles, m: &mut Vec<String>) {
    let toml = &f.cargo_toml;
    for (header, needed) in [
        ("[lib]", "test = false"),
        ("[lib]", "doctest = false"),
        ("[[bin]]", "test = false"),
    ] {
        if !section_lines(toml, header).contains(&needed) {
            m.push(format!(
                "src-tauri/Cargo.toml {header} lacks `{needed}` (FR-004, D12a)"
            ));
        }
    }
    let features = section_lines(toml, "[features]");
    if features.iter().any(|line| key_of(line) == "default") {
        m.push(
            "src-tauri/Cargo.toml [features] has a `default` feature (FR-004, FR-106)".to_owned(),
        );
    }
    if features != ["dev-feed = []", "manual-elevation = []"] {
        m.push(format!("src-tauri/Cargo.toml [features] is {features:?}, expected exactly [\"dev-feed = []\", \"manual-elevation = []\"] (FR-106, Phase 10 FR-105)"));
    }
    let example = section_lines(toml, "[[example]]");
    if example != EXAMPLE_LINES {
        m.push(format!(
            "src-tauri/Cargo.toml [[example]] is {example:?}, expected exactly {EXAMPLE_LINES:?} (Phase 10 FR-105)"
        ));
    }
    if !f
        .lib_rs
        .lines()
        .any(|line| line.trim() == "pub mod os_elevation;")
    {
        m.push(
            "src-tauri/src/lib.rs lacks `pub mod os_elevation;` (Phase 10 FR-090, PD-23)"
                .to_owned(),
        );
    }
    let dependencies = section_lines(toml, "[dependencies]");
    let mut keys: Vec<&str> = dependencies.iter().map(|line| key_of(line)).collect();
    keys.sort_unstable();
    if keys != SRC_TAURI_DEPENDENCIES {
        m.push(format!(
            "src-tauri/Cargo.toml [dependencies] keys are {keys:?}, expected {SRC_TAURI_DEPENDENCIES:?} (FR-004)"
        ));
    }
    let tauri_lines: Vec<&str> = dependencies
        .iter()
        .copied()
        .filter(|line| key_of(line) == "tauri")
        .collect();
    if tauri_lines != ["tauri = { workspace = true }"] {
        m.push(format!(
            "src-tauri/Cargo.toml [dependencies] tauri is {tauri_lines:?}, expected `tauri = {{ workspace = true }}` (FR-004)"
        ));
    }
    // SI-2: exactly one line may name a tauri-plugin crate, the opener entry inside `[dependencies]`.
    let mut header = "";
    for (index, line) in toml.lines().enumerate() {
        let trimmed = line.trim();
        if trimmed.starts_with('[') {
            header = trimmed;
        }
        let accepted = header == "[dependencies]" && trimmed == OPENER_DEPENDENCY_LINE;
        if line.contains("tauri-plugin") && !accepted {
            m.push(format!(
                "src-tauri/Cargo.toml:{}: names a tauri-plugin crate (FR-004, SI-2)",
                index + 1
            ));
        }
    }
    let workspace_dependencies = section_lines(&f.workspace_cargo_toml, "[workspace.dependencies]");
    let featureless = workspace_dependencies
        .iter()
        .find(|line| key_of(line) == "tauri")
        .is_some_and(|line| line.contains("features = []"));
    if !featureless {
        m.push(
            "Cargo.toml [workspace.dependencies] `tauri` entry lacks `features = []` (FR-004)"
                .to_owned(),
        );
    }
}

/// Whether `text` holds `token` as a whole token (comments included): an end of `token` that is an
/// identifier character may not touch another one, so `WINHTTP_ACCESS_TYPE_NO_PROXY` is not `NO_PROXY`.
fn holds_token(text: &str, token: &str) -> bool {
    let ident = |ch: char| ch.is_alphanumeric() || ch == '_';
    let starts_ident = token.chars().next().is_some_and(ident);
    let ends_ident = token.chars().next_back().is_some_and(ident);
    text.match_indices(token).any(|(at, _)| {
        let before = text[..at].chars().next_back();
        let after = text[at + token.len()..].chars().next();
        !(starts_ident && before.is_some_and(ident)) && !(ends_ident && after.is_some_and(ident))
    })
}

/// FR-061, FR-063, PD-12, SI-3, SI-26: the HTTP, proxy and PAC injection facts (plan §4.11.2).
fn check_http_injection(f: &ConfigFiles) -> Vec<String> {
    let mut m = Vec::new();
    let mut sources: Vec<(&str, &str)> = vec![("src/lib.rs", f.lib_rs.as_str())];
    sources.extend(
        f.src_tauri_texts
            .iter()
            .map(|(rel, text)| (rel.as_str(), text.as_str())),
    );

    // FR-061, Phase 7 FR-041: `assemble_context(roots, feed_url, http, signing_key)`.
    for count in call_argument_counts(&f.lib_rs, "assemble_context(") {
        if count != 4 {
            m.push(format!(
                "src-tauri/src/lib.rs calls `assemble_context(` with {count} top-level arguments, expected four (FR-061, Phase 7 FR-041)"
            ));
        }
    }

    // FR-061, FR-063, PD-12, SI-3, SI-26: each token in one src-tauri file only.
    for (token, allowed, rule) in CONFINED {
        for (rel, text) in &sources {
            if *rel != allowed && holds_token(text, token) {
                m.push(format!(
                    "src-tauri/{rel} holds `{token}`, which may occur only in src-tauri/{allowed} ({rule})"
                ));
            }
        }
    }
    for name in PROXY_ENV_NAMES {
        for (rel, text) in &f.core_src_texts {
            if holds_token(text, name) {
                m.push(format!(
                    "crates/core/{rel} holds `{name}`: proxy variables are named only in src-tauri/src/proxy_env.rs (FR-063, SI-3)"
                ));
            }
        }
    }

    // SI-3: the one untested copy step cannot swap a pair.
    match sources.iter().find(|(rel, _)| *rel == "src/proxy_env.rs") {
        None => m.push("src-tauri/src/proxy_env.rs is missing (FR-063, SI-3)".to_owned()),
        Some((_, text)) => {
            let squeezed: String = text.chars().filter(|ch| !ch.is_whitespace()).collect();
            for (field, name) in PROXY_ENV_PAIRS {
                if !squeezed.contains(&format!("{field}:got(\"{name}\")")) {
                    m.push(format!(
                        "src-tauri/src/proxy_env.rs lacks `{field}: got(\"{name}\")` (FR-063, SI-3)"
                    ));
                }
            }
        }
    }

    // FR-068, FR-092, FR-095: core logs the one `warn!` per PAC failure and per unreadable offset, so the
    // real PAC host (everything from `pub struct OsPacHost;` on) logs none; the context-leak line is not a failure.
    if let Some((_, text)) = sources.iter().find(|(rel, _)| *rel == "src/os_proxy.rs") {
        match text.find("pub struct OsPacHost;") {
            None => m.push(
                "src-tauri/src/os_proxy.rs lacks `pub struct OsPacHost;` (FR-092)".to_owned(),
            ),
            Some(at) => {
                let extra: Vec<usize> = text[at..]
                    .match_indices("warn!(")
                    .filter(|(i, _)| {
                        let rest = &text[at + i..];
                        !rest[..rest.find(';').unwrap_or(rest.len())].contains("CONTEXT_VALUE")
                    })
                    .map(|(i, _)| text[..at + i].lines().count())
                    .collect();
                if !extra.is_empty() {
                    m.push(format!(
                        "src-tauri/src/os_proxy.rs PAC host logs its own `warn!(` at lines {extra:?}; core logs the one warn (FR-068, FR-092, FR-095)"
                    ));
                }
            }
        }
    }

    // FR-073, TA-15: exactly the two OS target sections, one line each.
    let headers: Vec<&str> = f
        .cargo_toml
        .lines()
        .map(str::trim)
        .filter(|line| line.starts_with("[target."))
        .collect();
    let expected: Vec<&str> = SRC_TAURI_TARGETS
        .iter()
        .map(|(header, _)| *header)
        .collect();
    if headers != expected {
        m.push(format!(
            "src-tauri/Cargo.toml target sections are {headers:?}, expected {expected:?} (FR-073)"
        ));
    }
    for (header, line) in SRC_TAURI_TARGETS {
        let lines = section_lines(&f.cargo_toml, header);
        if lines != [line] {
            m.push(format!(
                "src-tauri/Cargo.toml {header} is {lines:?}, expected exactly [`{line}`] (FR-073, TA-15)"
            ));
        }
    }
    m
}

/// For each `needle` (a name ending in `(`) in `text`, the number of non-empty top-level
/// arguments between its parentheses (nested `()`, `[]`, `{}` are one argument).
fn call_argument_counts(text: &str, needle: &str) -> Vec<usize> {
    text.match_indices(needle)
        .map(|(at, _)| {
            let mut depth = 1_usize;
            let mut count = 0;
            let mut current = String::new();
            for ch in text[at + needle.len()..].chars() {
                match ch {
                    '(' | '[' | '{' => depth += 1,
                    ')' | ']' | '}' => depth -= 1,
                    _ => {}
                }
                if depth == 0 || (depth == 1 && ch == ',') {
                    if !current.trim().is_empty() {
                        count += 1;
                    }
                    current.clear();
                    if depth == 0 {
                        break;
                    }
                } else {
                    current.push(ch);
                }
            }
            count
        })
        .collect()
}

/// FR-045's lint-independent scan of `src/**` and `index.html`.
fn check_frontend(files: &[(String, String)]) -> Vec<String> {
    let mut m = Vec::new();
    for (name, text) in files {
        let stylesheet = name.starts_with("src/") && name.ends_with(".css");
        for (index, line) in text.lines().enumerate() {
            let at = format!("{name}:{}", index + 1);
            if stylesheet {
                let lower = line.to_ascii_lowercase();
                if STYLESHEET_BANNED
                    .iter()
                    .any(|banned| lower.contains(banned))
                {
                    m.push(format!(
                        "{at}: a font, image or URL in a stylesheet (FR-077)"
                    ));
                }
            }
            for banned in FRONTEND_BANNED {
                if line.contains(banned) {
                    m.push(format!("{at}: `{banned}` (FR-045, FR-052)"));
                }
            }
            if line.contains("http://") || line.contains("https://") {
                m.push(format!(
                    "{at}: a URL (FR-045: no http:// or https:// in the frontend)"
                ));
            }
            if let Some(start) = line.find("eslint-disable") {
                let reason = line[start..]
                    .split_once(" -- ")
                    .map(|(_, reason)| reason.trim().trim_end_matches("*/").trim());
                if reason.is_none_or(str::is_empty) {
                    m.push(format!(
                        "{at}: an eslint-disable without ` -- <reason>` (FR-045)"
                    ));
                }
            }
        }
    }
    m
}

/// FR-065 as amended by SI-15: the opener crate is called from `commands.rs` only, never
/// registered, never a library dependency and never granted to the webview.
fn check_opener(f: &OpenerFiles) -> Vec<String> {
    let mut m = Vec::new();
    for (rel, text) in &f.rust_sources {
        if rel != OPENER_FILE && holds_token(text, OPENER_CRATE) {
            m.push(format!(
                "{rel} holds `{OPENER_CRATE}`, which may occur only in {OPENER_FILE} (FR-065, SI-15)"
            ));
        }
        let joined = join_paths(text);
        for token in OPENER_REGISTRATION {
            if holds_token(&joined, token) {
                m.push(format!(
                    "{rel} holds `{token}`: the opener plugin is never registered (FR-065, SI-15, PD-20)"
                ));
            }
        }
    }
    for (name, capability) in &f.capabilities {
        let permissions = capability
            .get("permissions")
            .and_then(Value::as_array)
            .map_or(&[][..], Vec::as_slice);
        for permission in permissions {
            // A permission is a string or an object with an `identifier`.
            let identifier = permission
                .as_str()
                .or_else(|| permission.get("identifier").and_then(Value::as_str));
            if let Some(identifier) = identifier.filter(|id| id.starts_with("opener:")) {
                m.push(format!(
                    "capabilities/{name} grants `{identifier}` (FR-065: no opener permission)"
                ));
            }
        }
    }
    for (member, toml) in &f.library_manifests {
        for (index, line) in toml.lines().enumerate() {
            let line = line.trim();
            if !line.starts_with('#') && line.contains("tauri-plugin-opener") {
                m.push(format!(
                    "{member}/Cargo.toml:{}: names tauri-plugin-opener; only src-tauri depends on it (FR-065)",
                    index + 1
                ));
            }
        }
    }
    for map in ["dependencies", "devDependencies"] {
        if f.package_json
            .get(map)
            .and_then(|deps| deps.get(OPENER_NPM))
            .is_some()
        {
            m.push(format!("package.json {map} has `{OPENER_NPM}` (FR-065)"));
        }
    }
    if f.conf.pointer("/plugins/opener").is_some() {
        m.push("tauri.conf.json has /plugins/opener (FR-065)".to_owned());
    }
    let imports = [
        format!("from \"{OPENER_NPM}\""),
        format!("from '{OPENER_NPM}'"),
        format!("import(\"{OPENER_NPM}\")"),
        format!("import('{OPENER_NPM}')"),
    ];
    for (name, text) in &f.frontend {
        if name.ends_with(".test.ts") {
            continue;
        }
        for (index, line) in text.lines().enumerate() {
            if imports.iter().any(|import| line.contains(import.as_str())) {
                m.push(format!(
                    "{name}:{}: imports `{OPENER_NPM}`; the webview never calls the opener (FR-065)",
                    index + 1
                ));
            }
        }
    }
    m
}

/// `text` with the whitespace around every `::` removed, so the token sequence `a :: b` reads `a::b`.
fn join_paths(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut rest = text;
    while let Some(at) = rest.find("::") {
        out.push_str(rest[..at].trim_end());
        out.push_str("::");
        rest = rest[at + 2..].trim_start();
    }
    out.push_str(rest);
    out
}

/// PD-23: the workspace manifests, `package.json`, `tsconfig.json`, ESLint and CHANGELOG facts.
fn check_app_meta(f: &AppMetaFiles) -> Vec<String> {
    let mut m = Vec::new();

    // FR-002.
    let members = string_array(
        &section_lines(&f.workspace_cargo_toml, "[workspace]"),
        "members",
    );
    let expected_members: Vec<String> = MEMBERS.iter().map(|member| (*member).to_owned()).collect();
    if members.as_ref() != Some(&expected_members) {
        m.push(format!(
            "Cargo.toml [workspace] members is {members:?}, expected {MEMBERS:?} (FR-002)"
        ));
    }
    let clippy = section_lines(&f.workspace_cargo_toml, "[workspace.lints.clippy]");
    for needed in [
        "allow_attributes = \"deny\"",
        "allow_attributes_without_reason = \"deny\"",
    ] {
        if !clippy.contains(&needed) {
            m.push(format!(
                "Cargo.toml [workspace.lints.clippy] lacks `{needed}` (FR-002)"
            ));
        }
    }

    // FR-002 and FR-022 per member.
    for (member, toml) in &f.member_manifests {
        if !section_lines(toml, "[lints]").contains(&"workspace = true") {
            m.push(format!(
                "{member}/Cargo.toml has no [lints] section with `workspace = true` (FR-002)"
            ));
        }
        let mut section = "";
        for line in toml.lines() {
            let line = line.trim();
            if line.starts_with('[') {
                section = line;
                continue;
            }
            let core_features = member == "crates/core" && section == "[features]";
            let helper_features = member == "crates/helper" && section == "[features]";
            if line.contains("test-support")
                && section != "[dev-dependencies]"
                && !core_features
                && !helper_features
            {
                m.push(format!("{member}/Cargo.toml {section} `{line}` names test-support outside [dev-dependencies] (FR-022)"));
            }
        }
        if member == "crates/core" {
            let features = section_lines(toml, "[features]");
            if features != ["test-support = []", "live-network = [\"test-support\"]"] {
                m.push(format!(
                    "crates/core/Cargo.toml [features] is {features:?}, expected exactly [\"test-support = []\", \"live-network = [\\\"test-support\\\"]\"] (FR-022, FR-063)"
                ));
            }
            let bin = section_lines(toml, "[[bin]]");
            if bin != CORE_BIN_LINES {
                m.push(format!(
                    "crates/core/Cargo.toml [[bin]] is {bin:?}, expected exactly {CORE_BIN_LINES:?} (Phase 10 SC-015)"
                ));
            }
            if !section_lines(toml, "[dev-dependencies]").contains(&CORE_HELPER_DEV_DEPENDENCY) {
                m.push(format!(
                    "crates/core/Cargo.toml [dev-dependencies] lacks `{CORE_HELPER_DEV_DEPENDENCY}` (Phase 10 PD-8)"
                ));
            }
            if section_lines(toml, "[dependencies]")
                .iter()
                .any(|line| key_of(line) == "krate-manager-helper")
            {
                m.push(
                    "crates/core/Cargo.toml [dependencies] names krate-manager-helper: the core never depends on the helper (Phase 10 D1, PD-8)"
                        .to_owned(),
                );
            }
        }
        if member == "crates/helper" {
            let features = section_lines(toml, "[features]");
            if features != ["ci-temp-roots = []", "test-support = []"] {
                m.push(format!(
                    "crates/helper/Cargo.toml [features] is {features:?}, expected exactly [\"ci-temp-roots = []\", \"test-support = []\"] (Phase 10 FR-052, PD-8)"
                ));
            }
        }
    }

    // FR-050.
    let package = &f.package_json;
    if package.get("dependencies") != Some(&json!({ "@tauri-apps/api": "2.12.4" })) {
        m.push(format!(
            "package.json dependencies is {}, expected only @tauri-apps/api 2.12.4 (FR-050)",
            show(package.get("dependencies"))
        ));
    }
    match package.get("devDependencies").and_then(Value::as_object) {
        None => m.push("package.json has no devDependencies object (FR-050)".to_owned()),
        Some(dev) => {
            for (name, version) in dev {
                if !version.as_str().is_some_and(is_exact_version) {
                    m.push(format!("package.json devDependencies `{name}` is {version}, not an exact X.Y.Z (FR-050)"));
                }
            }
        }
    }
    if package.get("version").is_some() {
        m.push("package.json has a `version` key (FR-050, D18)".to_owned());
    }
    for map in ["dependencies", "devDependencies"] {
        if let Some(object) = package.get(map).and_then(Value::as_object) {
            for name in object
                .keys()
                .filter(|name| name.starts_with("@tauri-apps/plugin-"))
            {
                m.push(format!(
                    "package.json {map} has the Tauri plugin `{name}` (FR-050)"
                ));
            }
        }
    }
    let scripts = json!({
        "dev": "vite",
        "build": "vite build",
        "typecheck": "tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json",
        "lint": "eslint . --max-warnings 0",
        "test": "vitest run",
        "tauri": "tauri",
        "bundle": "node scripts/bundle.js",
    });
    if package.get("scripts") != Some(&scripts) {
        m.push(format!(
            "package.json scripts is {}, expected FR-050's six plus Phase 10's bundle (FR-050, FR-092)",
            show(package.get("scripts"))
        ));
    }
    if package.to_string().contains("dev-feed") {
        m.push("package.json names dev-feed (FR-045, FR-107)".to_owned());
    }

    // FR-051.
    let options = f.tsconfig.get("compilerOptions");
    for option in STRICT_OPTIONS {
        if options.and_then(|o| o.get(option)) != Some(&Value::Bool(true)) {
            m.push(format!(
                "tsconfig.json compilerOptions.{option} is not true (FR-051)"
            ));
        }
    }
    if options.and_then(|o| o.get("types")).is_some() {
        m.push(
            "tsconfig.json compilerOptions has `types` (FR-051: no Node types for browser code)"
                .to_owned(),
        );
    }
    if f.tsconfig.get("include") != Some(&json!(["src"])) {
        m.push(format!(
            "tsconfig.json include is {}, expected [\"src\"] (FR-051)",
            show(f.tsconfig.get("include"))
        ));
    }

    // FR-052.
    for needed in ESLINT_TEXTS {
        if !f.eslint_config.contains(needed) {
            m.push(format!("eslint.config.mjs lacks `{needed}` (FR-052)"));
        }
    }

    // FR-062.
    let head: Vec<&str> = f.changelog.lines().take(CHANGELOG_HEADER.len()).collect();
    if head != CHANGELOG_HEADER {
        m.push(format!(
            "CHANGELOG.md lines 1-6 are {head:?}, not the Keep a Changelog header (FR-062)"
        ));
    }
    let heading = format!("## [{}] - ", f.version);
    if !f.changelog.lines().any(|line| line.starts_with(&heading)) {
        m.push(format!(
            "CHANGELOG.md has no `{heading}` entry for version.json's version (FR-062)"
        ));
    }
    m
}

fn check_signing_crates(f: &LockFiles) -> Vec<String> {
    signing_crate_findings(&f.member_manifests, &f.lock)
}

/// One `[[package]]` block of a `Cargo.lock`.
struct LockPackage {
    name: String,
    version: String,
    /// `None` for a workspace member.
    source: Option<String>,
    /// `"<name>"` or `"<name> <version>"`, optionally followed by `" (<source>)"`.
    dependencies: Vec<String>,
}

/// The quoted strings of `text`.
fn quoted(text: &str) -> impl Iterator<Item = &str> {
    text.split('"').skip(1).step_by(2)
}

/// The `[[package]]` blocks of a `Cargo.lock` text (a `dependencies` array may span lines).
fn parse_lock(lock: &str) -> Vec<LockPackage> {
    let mut packages = Vec::new();
    let mut current: Option<LockPackage> = None;
    let mut in_dependencies = false;
    for line in lock.lines() {
        let line = line.trim();
        if in_dependencies {
            if let Some(package) = current.as_mut() {
                package.dependencies.extend(quoted(line).map(str::to_owned));
            }
            in_dependencies = !line.contains(']');
            continue;
        }
        if line.starts_with('[') {
            packages.extend(current.take());
            if line == "[[package]]" {
                current = Some(LockPackage {
                    name: String::new(),
                    version: String::new(),
                    source: None,
                    dependencies: Vec::new(),
                });
            }
            continue;
        }
        let (Some(package), Some((key, value))) = (current.as_mut(), line.split_once('=')) else {
            continue;
        };
        let first = quoted(value).next().unwrap_or_default().to_owned();
        match key.trim() {
            "name" => package.name = first,
            "version" => package.version = first,
            "source" => package.source = Some(first),
            "dependencies" => {
                package
                    .dependencies
                    .extend(quoted(value).map(str::to_owned));
                in_dependencies = !value.contains(']');
            }
            _ => {}
        }
    }
    packages.extend(current);
    packages
}

/// The package name of a lock dependency string.
fn dependency_name(dependency: &str) -> &str {
    dependency.split(' ').next().unwrap_or_default()
}

/// The package a lock dependency string names: by name when unique, else by name and version
/// (and source when the string gives one).
fn resolve(packages: &[LockPackage], dependency: &str) -> Option<usize> {
    let (spec, source) = match dependency.split_once(" (") {
        Some((spec, rest)) => (spec, Some(rest.trim_end_matches(')'))),
        None => (dependency, None),
    };
    let (name, version) = match spec.split_once(' ') {
        Some((name, version)) => (name, Some(version)),
        None => (spec, None),
    };
    let named: Vec<usize> = packages
        .iter()
        .enumerate()
        .filter(|(_, package)| package.name == name)
        .map(|(index, _)| index)
        .collect();
    if let [only] = named[..] {
        return Some(only);
    }
    named.into_iter().find(|&index| {
        let package = &packages[index];
        version.is_none_or(|v| package.version == v)
            && source.is_none_or(|s| package.source.as_deref() == Some(s))
    })
}

/// The dependency keys of a manifest's normal graph: `[dependencies]`, every
/// `[target.'…'.dependencies]` and `[build-dependencies]`, never `[dev-dependencies]` (FR-081).
/// A `[dependencies.<name>]` table header is the key `<name>`.
fn normal_roots(toml: &str) -> Vec<String> {
    let mut roots = Vec::new();
    let mut inside = false;
    for line in toml.lines() {
        let line = line.trim();
        if line.starts_with('[') {
            let header = line.trim_start_matches('[').trim_end_matches(']').trim();
            // `target.<spec>.<table>`: the spec may be quoted and hold dots.
            let table = match header.strip_prefix("target.") {
                None => header,
                Some(rest) => match rest.chars().next() {
                    Some(quote @ ('\'' | '"')) => rest[1..]
                        .split_once(quote)
                        .map_or("", |(_, after)| after.trim_start_matches('.')),
                    _ => rest.split_once('.').map_or("", |(_, after)| after),
                },
            };
            inside = table == "dependencies" || table == "build-dependencies";
            if let Some(name) = table
                .strip_prefix("dependencies.")
                .or_else(|| table.strip_prefix("build-dependencies."))
            {
                roots.push(name.trim_matches('"').to_owned());
            }
        } else if inside && !line.starts_with('#') && line.contains('=') {
            roots.push(key_of(line).trim_matches('"').to_owned());
        }
    }
    roots
}

/// The `name` → path of `at` from the walk's start, as `a -> b -> c`.
fn walk_path(packages: &[LockPackage], parent: &[Option<usize>], at: usize) -> String {
    let mut names = vec![packages[at].name.as_str()];
    let mut cursor = at;
    while let Some(up) = parent[cursor] {
        names.push(packages[up].name.as_str());
        cursor = up;
    }
    names.reverse();
    names.join(" -> ")
}

/// FR-081: the walk of every member's normal graph through the committed `Cargo.lock`. A
/// source-less package is a workspace member, expanded through its manifest's normal roots (its
/// lock entry also lists its dev-dependencies); a registry package through its lock entry.
fn signing_crate_findings(manifests: &[(String, String)], lock: &str) -> Vec<String> {
    let packages = parse_lock(lock);
    let members: Vec<(String, &str, Vec<String>)> = manifests
        .iter()
        .map(|(dir, toml)| {
            let name = string_value(&section_lines(toml, "[package]"), "name").unwrap_or_default();
            (name, dir.as_str(), normal_roots(toml))
        })
        .collect();
    let mut findings: Vec<String> = Vec::new();
    let mut report = |finding: String| {
        if !findings.contains(&finding) {
            findings.push(finding);
        }
    };
    for (name, dir, _) in &members {
        let Some(start) = packages
            .iter()
            .position(|package| package.source.is_none() && package.name == *name)
        else {
            report(format!(
                "{dir}: package `{name}` has no source-less Cargo.lock entry (FR-081)"
            ));
            continue;
        };
        let mut parent: Vec<Option<usize>> = vec![None; packages.len()];
        let mut seen = vec![false; packages.len()];
        seen[start] = true;
        let mut queue = std::collections::VecDeque::from([start]);
        while let Some(at) = queue.pop_front() {
            let package = &packages[at];
            if SIGNING_CRATES.contains(&package.name.as_str()) {
                report(format!(
                    "{dir} reaches `{}` through {} (FR-081)",
                    package.name,
                    walk_path(&packages, &parent, at)
                ));
            }
            let edges: Vec<&str> = if package.source.is_some() {
                package.dependencies.iter().map(String::as_str).collect()
            } else {
                let Some((_, member_dir, roots)) = members
                    .iter()
                    .find(|(member, _, _)| *member == package.name)
                else {
                    report(format!(
                        "{dir} reaches the path package `{}`, which is no workspace member (FR-081)",
                        package.name
                    ));
                    continue;
                };
                for root in roots {
                    if !package
                        .dependencies
                        .iter()
                        .any(|dependency| dependency_name(dependency) == root)
                    {
                        report(format!(
                            "{member_dir}/Cargo.toml dependency `{root}` is not in `{}`'s Cargo.lock dependencies (FR-081)",
                            package.name
                        ));
                    }
                }
                package
                    .dependencies
                    .iter()
                    .filter(|dependency| {
                        roots.iter().any(|root| root == dependency_name(dependency))
                    })
                    .map(String::as_str)
                    .collect()
            };
            for dependency in edges {
                match resolve(&packages, dependency) {
                    None => report(format!(
                        "Cargo.lock: `{}`'s dependency `{dependency}` has no package (FR-081)",
                        package.name
                    )),
                    Some(next) if !seen[next] => {
                        seen[next] = true;
                        parent[next] = Some(at);
                        queue.push_back(next);
                    }
                    Some(_) => {}
                }
            }
        }
    }
    findings
}

fn is_exact_version(text: &str) -> bool {
    let parts: Vec<&str> = text.split('.').collect();
    parts.len() == 3
        && parts
            .iter()
            .all(|part| !part.is_empty() && part.bytes().all(|b| b.is_ascii_digit()))
}

// ---------------------------------------------------------------------------------------------
// Planting helpers (in-memory copies only; every result is LF-joined, which `lines()` reads alike)

/// (name, the substrings one message each must hold, the mutation).
type Plant<T> = (&'static str, &'static [&'static str], fn(&mut T));

/// Applies each plant to a clone of `base`: every needle must be in a message of the planted
/// copy and in none of the base's, so the mutation itself is what the check reports.
fn assert_plants<T: Clone>(base: &T, check: fn(&T) -> Vec<String>, plants: &[Plant<T>]) {
    let base_messages = check(base);
    let mut missed = Vec::new();
    for (name, needles, plant) in plants {
        let mut copy = base.clone();
        plant(&mut copy);
        let messages = check(&copy);
        for needle in *needles {
            if base_messages.iter().any(|message| message.contains(needle)) {
                missed.push(format!(
                    "{name}: the unplanted files already report `{needle}`: {base_messages:#?}"
                ));
            } else if !messages.iter().any(|message| message.contains(needle)) {
                missed.push(format!(
                    "{name}: no message holds `{needle}`; got {messages:#?}"
                ));
            }
        }
    }
    assert!(missed.is_empty(), "{}", missed.join("\n"));
}

/// `text` with `new_line` inserted after every line whose trimmed text is `anchor`.
fn insert_after(text: &str, anchor: &str, new_line: &str) -> String {
    let mut out = Vec::new();
    for line in text.lines() {
        out.push(line);
        if line.trim() == anchor {
            out.push(new_line);
        }
    }
    out.join("\n")
}

/// `text` without the lines `drop` selects (on the trimmed line).
fn remove_lines(text: &str, drop: impl Fn(&str) -> bool) -> String {
    text.lines()
        .filter(|line| !drop(line.trim()))
        .collect::<Vec<_>>()
        .join("\n")
}

/// `text` without the `header` section (its header line and every line up to the next header).
fn remove_section(text: &str, header: &str) -> String {
    let mut out = Vec::new();
    let mut inside = false;
    for line in text.lines() {
        if line.trim().starts_with('[') {
            inside = line.trim() == header;
        }
        if !inside {
            out.push(line);
        }
    }
    out.join("\n")
}

/// `text` with each `key` line of the `header` section replaced by `new_line`.
fn replace_key(text: &str, header: &str, key: &str, new_line: &str) -> String {
    let mut out = Vec::new();
    let mut inside = false;
    for line in text.lines() {
        if line.trim().starts_with('[') {
            inside = line.trim() == header;
            out.push(line);
        } else if inside && line.contains('=') && key_of(line.trim()) == key {
            out.push(new_line);
        } else {
            out.push(line);
        }
    }
    out.join("\n")
}

fn set_csp(f: &mut ConfigFiles, csp: String) {
    f.conf["app"]["security"]["csp"] = Value::String(csp);
}

fn main_capability(f: &mut ConfigFiles) -> &mut Value {
    let (_, capability) = f
        .capabilities
        .iter_mut()
        .find(|(name, _)| name == "main.json")
        .expect("capabilities/main.json");
    capability
}

/// The text of `src-tauri/{rel}` among the non-`lib.rs` sources.
fn src_text<'a>(f: &'a mut ConfigFiles, rel: &str) -> &'a mut String {
    let (_, text) = f
        .src_tauri_texts
        .iter_mut()
        .find(|(name, _)| name == rel)
        .unwrap_or_else(|| panic!("src-tauri/{rel}"));
    text
}

fn member_manifest<'a>(f: &'a mut AppMetaFiles, member: &str) -> &'a mut String {
    let (_, toml) = f
        .member_manifests
        .iter_mut()
        .find(|(name, _)| name == member)
        .expect("member manifest");
    toml
}

/// The text of the Rust source at app-relative `rel`.
fn rust_source<'a>(f: &'a mut OpenerFiles, rel: &str) -> &'a mut String {
    let (_, text) = f
        .rust_sources
        .iter_mut()
        .find(|(name, _)| name == rel)
        .unwrap_or_else(|| panic!("{rel}"));
    text
}

fn library_manifest<'a>(f: &'a mut OpenerFiles, member: &str) -> &'a mut String {
    let (_, toml) = f
        .library_manifests
        .iter_mut()
        .find(|(name, _)| name == member)
        .expect("library manifest");
    toml
}

// ---------------------------------------------------------------------------------------------
// Tests

#[test]
fn real_src_tauri_config_passes_the_guard() {
    let config = check_config(&load_config());
    let frontend = check_frontend(&load_frontend());
    assert!(
        config.is_empty() && frontend.is_empty(),
        "config: {config:#?}\nfrontend: {frontend:#?}"
    );
}

#[test]
fn planted_config_changes_fail_the_guard() {
    let base = load_config();
    let plants: &[Plant<ConfigFiles>] = &[
        (
            "CSP names https://cdn.example",
            &["`https://cdn.example`"],
            |f| {
                set_csp(
                    f,
                    D13_CSP.replacen(
                        "script-src 'self'",
                        "script-src 'self' https://cdn.example",
                        1,
                    ),
                );
            },
        ),
        ("CSP connect-src *", &["wildcard source `*`"], |f| {
            set_csp(
                f,
                D13_CSP.replacen("connect-src ipc: http://ipc.localhost", "connect-src *", 1),
            );
        }),
        (
            "CSP script-src 'unsafe-inline'",
            &["CSP has 'unsafe-inline'"],
            |f| {
                set_csp(
                    f,
                    D13_CSP.replacen("script-src 'self'", "script-src 'self' 'unsafe-inline'", 1),
                );
            },
        ),
        (
            "second capability permission core:default",
            &["`permissions`"],
            |f| {
                main_capability(f)["permissions"] =
                    json!(["allow-app-version", "allow-startup-error", "core:default"]);
            },
        ),
        ("capability remote block", &["the key `remote`"], |f| {
            main_capability(f)["remote"] = json!({ "urls": ["https://cdn.example"] });
        }),
        (
            "installMode perMachine",
            &["/bundle/windows/nsis/installMode"],
            |f| {
                f.conf["bundle"]["windows"]["nsis"]["installMode"] = json!("perMachine");
            },
        ),
        (
            "signingIdentity absent",
            &["/bundle/macOS/signingIdentity"],
            |f| {
                if let Some(mac) = f.conf["bundle"]["macOS"].as_object_mut() {
                    mac.remove("signingIdentity");
                }
            },
        ),
        (
            "minimumSystemVersion 10.13",
            &["/bundle/macOS/minimumSystemVersion"],
            |f| {
                f.conf["bundle"]["macOS"]["minimumSystemVersion"] = json!("10.13");
            },
        ),
        (
            "a LSFileQuarantineEnabled string",
            &["contains LSFileQuarantineEnabled"],
            |f| {
                let plist = b"<dict><key>LSFileQuarantineEnabled</key><true/></dict>".to_vec();
                f.src_tauri_files.push(("planted.plist".to_owned(), plist));
            },
        ),
        (
            "devUrl http://localhost:1421",
            &["navigation::DEV_URL"],
            |f| {
                f.conf["build"]["devUrl"] = json!("http://localhost:1421");
            },
        ),
        (
            "Cargo.toml without test = false",
            &["[lib] lacks `test = false`"],
            |f| {
                f.cargo_toml = remove_lines(&f.cargo_toml, |line| line == "test = false");
            },
        ),
        (
            "Cargo.toml with default = [\"dev-feed\"]",
            &["a `default` feature"],
            |f| {
                f.cargo_toml =
                    insert_after(&f.cargo_toml, "[features]", "default = [\"dev-feed\"]");
            },
        ),
        (
            "Cargo.toml without the manual-elevation feature",
            &["src-tauri/Cargo.toml [features] is"],
            |f| {
                f.cargo_toml = remove_lines(&f.cargo_toml, |line| line == "manual-elevation = []");
            },
        ),
        (
            "[[example]] without required-features",
            &["src-tauri/Cargo.toml [[example]] is"],
            |f| {
                f.cargo_toml =
                    remove_lines(&f.cargo_toml, |line| line.starts_with("required-features"));
            },
        ),
        (
            "lib.rs with a private os_elevation",
            &["lacks `pub mod os_elevation;`"],
            |f| {
                f.lib_rs = f
                    .lib_rs
                    .replace("pub mod os_elevation;", "mod os_elevation;");
            },
        ),
        (
            "tauri.helper.conf.json with a second sidecar",
            &["src-tauri/tauri.helper.conf.json is {"],
            |f| {
                for (name, value) in &mut f.platform_confs {
                    if name == HELPER_CONF {
                        *value = json!({ "bundle": { "externalBin": [
                            "binaries/krate-manager-helper",
                            "binaries/other"
                        ] } });
                    }
                }
            },
        ),
        (
            "tauri.helper.conf.json removed",
            &["src-tauri/tauri.helper.conf.json is missing"],
            |f| {
                f.platform_confs.retain(|(name, _)| name != HELPER_CONF);
            },
        ),
        (
            "capability without allow-startup-error",
            &["`permissions`"],
            |f| {
                main_capability(f)["permissions"] = json!(["allow-app-version"]);
            },
        ),
        (
            "lib.rs without the navigation guard",
            &["`.plugin(navigation::guard())`"],
            |f| {
                f.lib_rs = f.lib_rs.replace(".plugin(navigation::guard())", "");
            },
        ),
        ("lib.rs without SharedState", &["`SharedState`"], |f| {
            f.lib_rs = f.lib_rs.replace("SharedState", "Shared");
        }),
        (
            "lib.rs without startup_notice",
            &["`startup_notice`"],
            |f| {
                f.lib_rs = f.lib_rs.replace("startup_notice", "notice");
            },
        ),
        (
            "lib.rs without LOCKED_EXIT_DELAY",
            &["`LOCKED_EXIT_DELAY`"],
            |f| {
                f.lib_rs = f.lib_rs.replace("LOCKED_EXIT_DELAY", "EXIT_DELAY");
            },
        ),
        ("lib.rs with a second eprintln!", &["2 `eprintln!`"], |f| {
            f.lib_rs
                .push_str("\nfn planted() { eprintln!(\"planted\"); }\n");
        }),
        (
            "tauri-plugin-fs in [dependencies]",
            &["[dependencies] keys", "names a tauri-plugin crate"],
            |f| {
                f.cargo_toml =
                    insert_after(&f.cargo_toml, "[dependencies]", "tauri-plugin-fs = \"2\"");
            },
        ),
        (
            "seventh capability permission allow-planted",
            &["`permissions`"],
            |f| {
                if let Some(list) = main_capability(f)["permissions"].as_array_mut() {
                    list.push(json!("allow-planted"));
                }
            },
        ),
        (
            "capability permission allow-fs-read",
            &["`permissions`"],
            |f| {
                if let Some(list) = main_capability(f)["permissions"].as_array_mut() {
                    list.push(json!("allow-fs-read"));
                }
            },
        ),
        (
            "capability permission opener:allow-open-url",
            &["`permissions`"],
            |f| {
                if let Some(list) = main_capability(f)["permissions"].as_array_mut() {
                    list.push(json!("opener:allow-open-url"));
                }
            },
        ),
        (
            "capability description read-only commands",
            &["`description`"],
            |f| {
                main_capability(f)["description"] =
                    json!("The main window may call only the app's own read-only commands.");
            },
        ),
        (
            "tauri-plugin-opener in [build-dependencies]",
            &["names a tauri-plugin crate"],
            |f| {
                f.cargo_toml = insert_after(
                    &f.cargo_toml,
                    "[build-dependencies]",
                    "tauri-plugin-opener = \"2\"",
                );
            },
        ),
        (
            "tauri with features devtools",
            &["`tauri = { workspace = true }`"],
            |f| {
                f.cargo_toml = replace_key(
                    &f.cargo_toml,
                    "[dependencies]",
                    "tauri",
                    "tauri = { workspace = true, features = [\"devtools\"] }",
                );
            },
        ),
        (
            "workspace tauri with features tray-icon",
            &["lacks `features = []`"],
            |f| {
                f.workspace_cargo_toml = replace_key(
                    &f.workspace_cargo_toml,
                    "[workspace.dependencies]",
                    "tauri",
                    "tauri = { version = \"2.12.5\", features = [\"tray-icon\"] }",
                );
            },
        ),
        (
            "lib.rs without HttpConfig::production()",
            &["does not contain `HttpConfig::production()`"],
            |f| {
                f.lib_rs = f
                    .lib_rs
                    .replace("HttpConfig::production()", "HttpConfig::default()");
            },
        ),
        (
            "LoopbackOnly in commands.rs",
            &["src-tauri/src/commands.rs holds `LoopbackOnly`"],
            |f| {
                src_text(f, "src/commands.rs")
                    .push_str("\nconst PLANTED: UrlPolicy = UrlPolicy::LoopbackOnly;\n");
            },
        ),
        (
            "lib.rs without .with_proxy(",
            &["does not contain `.with_proxy(`"],
            |f| {
                f.lib_rs = f.lib_rs.replace(".with_proxy(", ".proxy(");
            },
        ),
        (
            "lib.rs without .with_pac(",
            &["does not contain `.with_pac(`"],
            |f| {
                f.lib_rs = f.lib_rs.replace(".with_pac(", ".pac(");
            },
        ),
        (
            ".with_pac_host( in commands.rs",
            &["src-tauri/src/commands.rs holds `.with_pac_host(`"],
            |f| {
                src_text(f, "src/commands.rs").push_str(
                    "\nfn planted(c: HttpConfig) -> HttpConfig { c.with_pac_host(None) }\n",
                );
            },
        ),
        (
            "OsPacHost in lib.rs",
            &["src-tauri/src/lib.rs holds `OsPacHost`"],
            |f| {
                f.lib_rs
                    .push_str("\nfn planted() { let _ = os_proxy::OsPacHost; }\n");
            },
        ),
        (
            "std::env::var(\"HTTPS_PROXY\") in os_proxy.rs",
            &["src-tauri/src/os_proxy.rs holds `HTTPS_PROXY`"],
            |f| {
                src_text(f, "src/os_proxy.rs")
                    .push_str("\nfn planted() { let _ = std::env::var(\"HTTPS_PROXY\"); }\n");
            },
        ),
        (
            "NO_PROXY in a core comment",
            &["crates/core/src/planted.rs holds `NO_PROXY`"],
            |f| {
                f.core_src_texts
                    .push(("src/planted.rs".to_owned(), "// NO_PROXY wins\n".to_owned()));
            },
        ),
        (
            "a third target section in src-tauri/Cargo.toml",
            &["src-tauri/Cargo.toml target sections are"],
            |f| {
                f.cargo_toml.push_str(
                    "\n[target.'cfg(target_os = \"linux\")'.dependencies]\nlibc = \"0.2\"\n",
                );
            },
        ),
        (
            "a warn! in the OS PAC host",
            &["src-tauri/src/os_proxy.rs PAC host logs its own `warn!(`"],
            |f| {
                src_text(f, "src/os_proxy.rs")
                    .push_str("\nfn planted() { log::warn!(\"E_PAC_FETCH: planted\"); }\n");
            },
        ),
        (
            "proxy_env.rs with a swapped pair",
            &["lacks `https_upper: got(\"HTTPS_PROXY\")`"],
            |f| {
                let text = src_text(f, "src/proxy_env.rs");
                *text = text.replace(
                    "https_upper: got(\"HTTPS_PROXY\")",
                    "https_upper: got(\"https_proxy\")",
                );
            },
        ),
        (
            "assemble_context( with three arguments",
            &["calls `assemble_context(` with 3 top-level arguments"],
            |f| {
                f.lib_rs = f.lib_rs.replace(
                    "assemble_context(roots, &feed_url, http, SigningKey::release())",
                    "assemble_context(roots, &feed_url, http)",
                );
            },
        ),
        (
            "commands::download_plugin removed from the handler list",
            &["generate_handler! does not list exactly"],
            |f| {
                f.lib_rs = f
                    .lib_rs
                    .replace(
                        "commands::open_release_page,",
                        "commands::open_release_page",
                    )
                    .replace("commands::download_plugin", "");
            },
        ),
        (
            "commands::install_plugin removed from the handler list",
            &["generate_handler! does not list exactly"],
            |f| {
                f.lib_rs = f.lib_rs.replace("commands::install_plugin,", "");
            },
        ),
        (
            "path: PathBuf added to remove_plugin",
            &["`remove_plugin` takes `path:PathBuf`"],
            |f| {
                let text = src_text(f, "src/commands.rs");
                *text = text.replacen("fn remove_plugin(", "fn remove_plugin(path: PathBuf, ", 1);
            },
        ),
    ];
    assert_plants(&base, check_config, plants);

    // A longer identifier holding a proxy name is another token (FR-063 names tokens).
    let mut quiet = base.clone();
    quiet
        .lib_rs
        .push_str("\nfn planted() { let _ = WINHTTP_ACCESS_TYPE_NO_PROXY; }\n");
    quiet.core_src_texts.push((
        "src/planted.rs".to_owned(),
        "const MY_HTTPS_PROXY_X: u8 = 0;\n".to_owned(),
    ));
    let messages = check_config(&quiet);
    assert!(
        !messages.iter().any(|message| message.contains("_PROXY`")),
        "{messages:#?}"
    );

    // An APP_IDENTIFIER unequal to the config's identifier (D7).
    let needle = "platform::APP_IDENTIFIER";
    assert!(
        !check_config(&base)
            .iter()
            .any(|message| message.contains(needle))
    );
    let messages = check_config_against(&base, "com.example.other");
    assert!(
        messages.iter().any(|message| message.contains(needle)),
        "{messages:#?}"
    );
}

#[test]
fn frontend_source_scan_reports_planted_strings() {
    let clean = vec![(
        "src/clean.ts".to_owned(),
        "const a = 1;\n// eslint-disable-next-line no-console -- a stated reason\n".to_owned(),
    )];
    assert_eq!(check_frontend(&clean), Vec::<String>::new());
    let planted = [
        ("el.innerHTML = s;", "`innerHTML`"),
        ("// @ts-ignore", "`@ts-ignore`"),
        ("// eslint-disable-next-line", "an eslint-disable without"),
        ("import x from \"https://cdn.example/x.js\";", "a URL"),
    ];
    for (line, needle) in planted {
        let files = vec![(
            "src/planted.ts".to_owned(),
            format!("const a = 1;\n{line}\n"),
        )];
        let messages = check_frontend(&files);
        assert!(
            messages.iter().any(|message| message.starts_with("src/planted.ts:2:") && message.contains(needle)),
            "`{line}` not reported as `{needle}`: {messages:#?}"
        );
    }

    // FR-077: no font, image or URL in a stylesheet (the URL plant is not http-prefixed).
    let clean_css = vec![(
        "src/clean.css".to_owned(),
        "body {\n  font-family: system-ui, sans-serif;\n}\n".to_owned(),
    )];
    assert_eq!(check_frontend(&clean_css), Vec::<String>::new());
    for line in [
        "src: url(x.woff2)",
        "@font-face {",
        "@import \"x.css\";",
        "background: image-set(\"a.png\" 1x)",
        "BACKGROUND: URL(a.png)",
    ] {
        let files = vec![("src/planted.css".to_owned(), format!("body {{}}\n{line}\n"))];
        let messages = check_frontend(&files);
        assert!(
            messages.iter().any(|message| message
                == "src/planted.css:2: a font, image or URL in a stylesheet (FR-077)"),
            "`{line}` not reported as a stylesheet font, image or URL: {messages:#?}"
        );
    }
}

#[test]
fn real_app_metadata_passes_the_guard() {
    let messages = check_app_meta(&load_app_meta());
    assert!(messages.is_empty(), "{messages:#?}");
}

#[test]
fn planted_app_metadata_changes_fail_the_guard() {
    let base = load_app_meta();
    let plants: &[Plant<AppMetaFiles>] = &[
        (
            "core without [lints]",
            &["crates/core/Cargo.toml has no [lints]"],
            |f| {
                let toml = member_manifest(f, "crates/core");
                *toml = remove_section(toml, "[lints]");
            },
        ),
        (
            "test-support moved into src-tauri [dependencies]",
            &["outside [dev-dependencies]"],
            |f| {
                let toml = member_manifest(f, "src-tauri");
                *toml = replace_key(
                    toml,
                    "[dev-dependencies]",
                    "krate-manager-core",
                    "krate-manager-core = { workspace = true }",
                );
                *toml = replace_key(
                    toml,
                    "[dependencies]",
                    "krate-manager-core",
                    "krate-manager-core = { workspace = true, features = [\"test-support\"] }",
                );
            },
        ),
        (
            "default = [\"test-support\"] in core [features]",
            &["crates/core/Cargo.toml [features]"],
            |f| {
                let toml = member_manifest(f, "crates/core");
                *toml = insert_after(toml, "[features]", "default = [\"test-support\"]");
            },
        ),
        (
            "default = [\"live-network\"] in core [features]",
            &["crates/core/Cargo.toml [features]"],
            |f| {
                let toml = member_manifest(f, "crates/core");
                *toml = insert_after(toml, "[features]", "default = [\"live-network\"]");
            },
        ),
        (
            "default = [\"ci-temp-roots\"] in helper [features]",
            &["crates/helper/Cargo.toml [features] is"],
            |f| {
                let toml = member_manifest(f, "crates/helper");
                *toml = insert_after(toml, "[features]", "default = [\"ci-temp-roots\"]");
            },
        ),
        (
            "core [[bin]] without test = false",
            &["crates/core/Cargo.toml [[bin]] is"],
            |f| {
                let toml = member_manifest(f, "crates/core");
                *toml = remove_lines(toml, |line| line == "test = false");
            },
        ),
        (
            "the helper moved into core [dependencies]",
            &[
                "crates/core/Cargo.toml [dev-dependencies] lacks",
                "[dependencies] names krate-manager-helper",
            ],
            |f| {
                let toml = member_manifest(f, "crates/core");
                *toml = remove_lines(toml, |line| line == CORE_HELPER_DEV_DEPENDENCY);
                *toml = insert_after(toml, "[dependencies]", CORE_HELPER_DEV_DEPENDENCY);
            },
        ),
        (
            "package.json bundle script changed",
            &["plus Phase 10's bundle"],
            |f| {
                f.package_json["scripts"]["bundle"] = json!("node scripts/other.js");
            },
        ),
        (
            "allow_attributes deny dropped",
            &["lacks `allow_attributes = \"deny\"`"],
            |f| {
                f.workspace_cargo_toml = remove_lines(&f.workspace_cargo_toml, |line| {
                    line == "allow_attributes = \"deny\""
                });
            },
        ),
        (
            "allow_attributes_without_reason deny dropped",
            &["lacks `allow_attributes_without_reason = \"deny\"`"],
            |f| {
                f.workspace_cargo_toml = remove_lines(&f.workspace_cargo_toml, |line| {
                    line == "allow_attributes_without_reason = \"deny\""
                });
            },
        ),
        ("a fourth workspace member", &["[workspace] members"], |f| {
            f.workspace_cargo_toml = replace_key(
                &f.workspace_cargo_toml,
                "[workspace]",
                "members",
                "members = [\"crates/core\", \"crates/helper\", \"src-tauri\", \"crates/extra\"]",
            );
        }),
        (
            "@tauri-apps/plugin-opener in dependencies",
            &["`@tauri-apps/plugin-opener`"],
            |f| {
                f.package_json["dependencies"]["@tauri-apps/plugin-opener"] = json!("2.5.0");
            },
        ),
        ("vite ^8.3.5", &["devDependencies `vite`"], |f| {
            f.package_json["devDependencies"]["vite"] = json!("^8.3.5");
        }),
        (
            "noUncheckedIndexedAccess deleted",
            &["compilerOptions.noUncheckedIndexedAccess"],
            |f| {
                if let Some(options) = f.tsconfig["compilerOptions"].as_object_mut() {
                    options.remove("noUncheckedIndexedAccess");
                }
            },
        ),
        (
            "exactOptionalPropertyTypes deleted",
            &["compilerOptions.exactOptionalPropertyTypes"],
            |f| {
                if let Some(options) = f.tsconfig["compilerOptions"].as_object_mut() {
                    options.remove("exactOptionalPropertyTypes");
                }
            },
        ),
        (
            "types [\"node\"] added",
            &["compilerOptions has `types`"],
            |f| {
                f.tsconfig["compilerOptions"]["types"] = json!(["node"]);
            },
        ),
        (
            "reportUnusedDisableDirectives removed",
            &["lacks `reportUnusedDisableDirectives"],
            |f| {
                f.eslint_config = f.eslint_config.replace("reportUnusedDisableDirectives", "");
            },
        ),
        (
            "CHANGELOG heading 0.1.1",
            &["entry for version.json's version"],
            |f| {
                let heading = format!("## [{}] - ", f.version);
                f.changelog = f.changelog.replace(&heading, "## [0.1.1] - ");
            },
        ),
        (
            "CHANGELOG without the Keep a Changelog line",
            &["not the Keep a Changelog header"],
            |f| {
                f.changelog =
                    remove_lines(&f.changelog, |line| line.contains("[Keep a Changelog]"));
            },
        ),
    ];
    assert_plants(&base, check_app_meta, plants);
}

#[test]
fn core_has_no_signing_crate() {
    let messages = check_signing_crates(&load_lock_files());
    assert!(messages.is_empty(), "{messages:#?}");
}

#[test]
fn signing_crate_scan_reports_plants() {
    let base = load_lock_files();
    let plants: &[Plant<LockFiles>] = &[
        (
            "minisign in core [dependencies]",
            &["crates/core reaches `minisign` through krate-manager-core -> minisign"],
            |f| {
                let (_, toml) = f
                    .member_manifests
                    .iter_mut()
                    .find(|(name, _)| name == "crates/core")
                    .expect("crates/core manifest");
                *toml = insert_after(toml, "[dependencies]", "minisign = { workspace = true }");
            },
        ),
        (
            "rpassword in log's lock dependencies",
            &["crates/core reaches `rpassword` through krate-manager-core -> log -> rpassword"],
            |f| {
                f.lock = insert_after(&f.lock, "name = \"log\"", "dependencies = [\"rpassword\"]");
            },
        ),
    ];
    assert_plants(&base, check_signing_crates, plants);

    // A dev-dependency is never a root, so core's `[dev-dependencies]` may name minisign (FR-081, SI-21).
    let mut quiet = base.clone();
    let (_, toml) = quiet
        .member_manifests
        .iter_mut()
        .find(|(name, _)| name == "crates/core")
        .expect("crates/core manifest");
    *toml = remove_section(toml, "[dev-dependencies]");
    toml.push_str("\n[dev-dependencies]\nminisign = { workspace = true }\n");
    assert_eq!(
        section_lines(toml, "[dev-dependencies]"),
        ["minisign = { workspace = true }"]
    );
    let messages = check_signing_crates(&quiet);
    assert!(messages.is_empty(), "{messages:#?}");
}

#[test]
fn opener_is_confined_and_not_granted_to_the_webview() {
    let messages = check_opener(&load_opener());
    assert!(messages.is_empty(), "{messages:#?}");
}

#[test]
fn opener_guard_reports_plants() {
    let base = load_opener();
    let plants: &[Plant<OpenerFiles>] = &[
        (
            "capability permission opener:allow-open-url",
            &["capabilities/main.json grants `opener:allow-open-url`"],
            |f| {
                let (_, capability) = f
                    .capabilities
                    .iter_mut()
                    .find(|(name, _)| name == "main.json")
                    .expect("capabilities/main.json");
                if let Some(list) = capability["permissions"].as_array_mut() {
                    list.push(json!("opener:allow-open-url"));
                }
            },
        ),
        (
            "tauri_plugin_opener in status.rs",
            &["crates/core/src/status.rs holds `tauri_plugin_opener`"],
            |f| {
                rust_source(f, "crates/core/src/status.rs")
                    .push_str("\nfn planted() { let _ = tauri_plugin_opener::open_url; }\n");
            },
        ),
        (
            "tauri_plugin_opener in lib.rs",
            &["src-tauri/src/lib.rs holds `tauri_plugin_opener`"],
            |f| {
                rust_source(f, "src-tauri/src/lib.rs")
                    .push_str("\nfn planted() { let _ = tauri_plugin_opener::open_url; }\n");
            },
        ),
        (
            ".plugin(tauri_plugin_opener::init()) in commands.rs",
            &["src-tauri/src/commands.rs holds `tauri_plugin_opener::init`"],
            |f| {
                rust_source(f, "src-tauri/src/commands.rs").push_str(
                    "\nfn planted(b: tauri::Builder<tauri::Wry>) -> tauri::Builder<tauri::Wry> { b.plugin(tauri_plugin_opener::init()) }\n",
                );
            },
        ),
        (
            "tauri_plugin_opener :: Builder (spaced) in commands.rs",
            &["src-tauri/src/commands.rs holds `tauri_plugin_opener::Builder`"],
            |f| {
                rust_source(f, "src-tauri/src/commands.rs").push_str(
                    "\nfn planted() { let _ = tauri_plugin_opener ::\n    Builder::default(); }\n",
                );
            },
        ),
        (
            "use tauri_plugin_opener::OpenerExt; in commands.rs",
            &["src-tauri/src/commands.rs holds `OpenerExt`"],
            |f| {
                rust_source(f, "src-tauri/src/commands.rs")
                    .push_str("\nuse tauri_plugin_opener::OpenerExt;\n");
            },
        ),
        (
            "tauri-plugin-opener in crates/core/Cargo.toml",
            &["crates/core/Cargo.toml:"],
            |f| {
                let toml = library_manifest(f, "crates/core");
                *toml = insert_after(toml, "[dependencies]", "tauri-plugin-opener = \"2\"");
            },
        ),
        (
            "tauri-plugin-opener in crates/helper/Cargo.toml",
            &["crates/helper/Cargo.toml:"],
            |f| {
                library_manifest(f, "crates/helper")
                    .push_str("\n[dependencies.tauri-plugin-opener]\nworkspace = true\n");
            },
        ),
        (
            "@tauri-apps/plugin-opener in devDependencies",
            &["package.json devDependencies has `@tauri-apps/plugin-opener`"],
            |f| {
                f.package_json["devDependencies"]["@tauri-apps/plugin-opener"] = json!("2.7.2");
            },
        ),
        (
            "plugins.opener in tauri.conf.json",
            &["/plugins/opener"],
            |f| {
                f.conf["plugins"] = json!({ "opener": { "requireLiteralLeadingDot": false } });
            },
        ),
        (
            "an opener import in src/planted.ts",
            &["src/planted.ts:2: imports `@tauri-apps/plugin-opener`"],
            |f| {
                f.frontend.push((
                    "src/planted.ts".to_owned(),
                    "const a = 1;\nimport { openUrl } from \"@tauri-apps/plugin-opener\";\n"
                        .to_owned(),
                ));
            },
        ),
        (
            "a dynamic opener import in src/planted.ts",
            &["src/planted.ts:1: imports `@tauri-apps/plugin-opener`"],
            |f| {
                f.frontend.push((
                    "src/planted.ts".to_owned(),
                    "const m = await import('@tauri-apps/plugin-opener');\n".to_owned(),
                ));
            },
        ),
    ];
    assert_plants(&base, check_opener, plants);

    // A test file may hold the import line (status.test.ts plants it in memory, FR-065).
    let mut quiet = base.clone();
    quiet.frontend.push((
        "src/planted.test.ts".to_owned(),
        "import { openUrl } from \"@tauri-apps/plugin-opener\";\n".to_owned(),
    ));
    let messages = check_opener(&quiet);
    assert!(
        !messages
            .iter()
            .any(|message| message.contains("src/planted.test.ts")),
        "{messages:#?}"
    );
}
