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
const CAPABILITY_KEYS: [&str; 4] = ["identifier", "description", "windows", "permissions"];
/// FR-004's `[dependencies]` key set of `src-tauri/Cargo.toml` (PD-23).
const SRC_TAURI_DEPENDENCIES: [&str; 5] =
    ["krate-manager-core", "log", "serde", "serde_json", "tauri"];
/// The strings `src-tauri/src/lib.rs` must contain (FR-017, FR-042, FR-044, FR-101).
const LIB_RS_STRINGS: [&str; 7] = [
    ".plugin(navigation::guard())",
    "assemble_context(",
    ".manage(",
    "SharedState",
    "startup_notice",
    "startup_error",
    "LOCKED_EXIT_DELAY",
];
/// The workspace members of FR-002, in order.
const MEMBERS: [&str; 3] = ["crates/core", "crates/helper", "src-tauri"];
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
    ConfigFiles {
        conf: read_json(&dir.join("tauri.conf.json")),
        platform_confs,
        capabilities,
        build_rs: file_text(&files, "build.rs"),
        cargo_toml: file_text(&files, "Cargo.toml"),
        workspace_cargo_toml: read_text(&app_root().join("Cargo.toml")),
        lib_rs: file_text(&files, "src/lib.rs"),
        src_tauri_texts,
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

    // D15: the platform files set only bundle.targets.
    for (name, _) in &f.platform_confs {
        if !PLATFORM_TARGETS.iter().any(|(known, _)| known == name) {
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
                        json!(["allow-app-version", "allow-startup-error"]),
                    ),
                ];
                for (key, want) in expected {
                    if object.get(key) != Some(&want) {
                        m.push(format!(
                            "capabilities/main.json `{key}` is {}, expected {want} (FR-041)",
                            show(object.get(key))
                        ));
                    }
                }
            }
        }
    }

    // FR-040: build.rs registers exactly the two commands (whitespace-insensitive).
    let squeezed: String = f
        .build_rs
        .chars()
        .filter(|ch| !ch.is_whitespace())
        .collect();
    if !squeezed
        .replace(",]", "]")
        .contains(r#"commands(&["app_version","startup_error"])"#)
    {
        m.push(
            "src-tauri/build.rs does not register exactly app_version and startup_error (FR-040)"
                .to_owned(),
        );
    }

    check_src_tauri_manifest(f, &mut m);

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
    if features != ["dev-feed = []"] {
        m.push(format!("src-tauri/Cargo.toml [features] is {features:?}, expected exactly [\"dev-feed = []\"] (FR-106)"));
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
    for (index, line) in toml.lines().enumerate() {
        if line.contains("tauri-plugin") {
            m.push(format!(
                "src-tauri/Cargo.toml:{}: names a tauri-plugin crate (FR-004)",
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

/// FR-045's lint-independent scan of `src/**` and `index.html`.
fn check_frontend(files: &[(String, String)]) -> Vec<String> {
    let mut m = Vec::new();
    for (name, text) in files {
        for (index, line) in text.lines().enumerate() {
            let at = format!("{name}:{}", index + 1);
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
            if line.contains("test-support") && section != "[dev-dependencies]" && !core_features {
                m.push(format!("{member}/Cargo.toml {section} `{line}` names test-support outside [dev-dependencies] (FR-022)"));
            }
        }
        if member == "crates/core" {
            let features = section_lines(toml, "[features]");
            if features != ["test-support = []"] {
                m.push(format!(
                    "crates/core/Cargo.toml [features] is {features:?}, expected exactly [\"test-support = []\"] (FR-022)"
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
    });
    if package.get("scripts") != Some(&scripts) {
        m.push(format!(
            "package.json scripts is {}, expected FR-050's six (FR-050)",
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

fn member_manifest<'a>(f: &'a mut AppMetaFiles, member: &str) -> &'a mut String {
    let (_, toml) = f
        .member_manifests
        .iter_mut()
        .find(|(name, _)| name == member)
        .expect("member manifest");
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
            "tauri-plugin-opener in [dependencies]",
            &["[dependencies] keys", "names a tauri-plugin crate"],
            |f| {
                f.cargo_toml = insert_after(
                    &f.cargo_toml,
                    "[dependencies]",
                    "tauri-plugin-opener = \"2\"",
                );
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
    ];
    assert_plants(&base, check_config, plants);

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
