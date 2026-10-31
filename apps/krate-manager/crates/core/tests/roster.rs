//! Roster guard (spec FR-082, SC-015; plan §4.11.3, PD-20): no plugin name and no `api.github.com`
//! in the manager's sources, comments included. The names are read from the repo's
//! `plugins/*/version.json` files, never written here. Every test reads files only.

use std::fs;
use std::path::{Path, PathBuf};

/// The host no source may name (FR-082). It lives here, outside every scanned path.
const HOST: &str = "api.github.com";
/// Fewer names than this means the scan would be vacuous (FR-082).
const MIN_NAMES: usize = 9;

fn app_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("..").join("..")
}

fn repo_root() -> PathBuf {
    app_root().join("..").join("..")
}

/// The `name` of every `plugins/*/version.json`, sorted; at least `MIN_NAMES`.
fn plugin_names() -> Vec<String> {
    let plugins = repo_root().join("plugins");
    let entries =
        fs::read_dir(&plugins).unwrap_or_else(|e| panic!("cannot list {}: {e}", plugins.display()));
    let mut names = Vec::new();
    for entry in entries {
        let entry = entry.unwrap_or_else(|e| panic!("cannot list {}: {e}", plugins.display()));
        let file = entry.path().join("version.json");
        if !file.is_file() {
            continue;
        }
        let text = fs::read_to_string(&file)
            .unwrap_or_else(|e| panic!("cannot read {}: {e}", file.display()));
        let json: serde_json::Value = serde_json::from_str(&text)
            .unwrap_or_else(|e| panic!("{} is not JSON: {e}", file.display()));
        let name = json
            .get("name")
            .and_then(serde_json::Value::as_str)
            .unwrap_or_else(|| panic!("{} has no string `name`", file.display()));
        names.push(name.to_owned());
    }
    names.sort();
    assert!(
        names.len() >= MIN_NAMES,
        "only {} plugin names read from {}: {names:?}",
        names.len(),
        plugins.display()
    );
    names
}

/// The plugin names followed by `HOST`.
fn needles(names: &[String]) -> Vec<&str> {
    names
        .iter()
        .map(String::as_str)
        .chain(std::iter::once(HOST))
        .collect()
}

/// Every `"<rel>:<line>: <needle>"` hit of `needles` in raw `text`, case-sensitively, in line
/// order (FR-082).
fn scan(rel: &str, text: &str, needles: &[&str]) -> Vec<String> {
    let mut hits = Vec::new();
    for (index, line) in text.lines().enumerate() {
        for needle in needles {
            if line.contains(needle) {
                hits.push(format!("{rel}:{}: {needle}", index + 1));
            }
        }
    }
    hits
}

/// `*_tests.rs`, `test_support.rs` and `*.test.ts` are not scanned (FR-082).
fn is_excluded(name: &str) -> bool {
    name.ends_with("_tests.rs") || name == "test_support.rs" || name.ends_with(".test.ts")
}

/// Every non-excluded file under `dir`, recursively, as (`prefix` + `/`-separated relative path,
/// absolute path).
fn collect(dir: &Path, prefix: &str, out: &mut Vec<(String, PathBuf)>) {
    let entries =
        fs::read_dir(dir).unwrap_or_else(|e| panic!("cannot list {}: {e}", dir.display()));
    for entry in entries {
        let entry = entry.unwrap_or_else(|e| panic!("cannot list {}: {e}", dir.display()));
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().into_owned();
        if path.is_dir() {
            collect(&path, &format!("{prefix}{name}/"), out);
        } else if !is_excluded(&name) {
            out.push((format!("{prefix}{name}"), path));
        }
    }
}

/// The FR-082 scan set, sorted by relative path.
fn scanned_files() -> Vec<(String, PathBuf)> {
    let root = app_root();
    let mut files = Vec::new();
    for rel in [
        "crates/core/src",
        "crates/helper/src",
        "src-tauri/src",
        "src-tauri/capabilities",
        "src",
    ] {
        let dir = rel.split('/').fold(root.clone(), |p, c| p.join(c));
        collect(&dir, &format!("{rel}/"), &mut files);
    }
    let tauri = root.join("src-tauri");
    let entries =
        fs::read_dir(&tauri).unwrap_or_else(|e| panic!("cannot list {}: {e}", tauri.display()));
    for entry in entries {
        let entry = entry.unwrap_or_else(|e| panic!("cannot list {}: {e}", tauri.display()));
        let name = entry.file_name().to_string_lossy().into_owned();
        if entry.path().is_file() && name.starts_with("tauri") && name.ends_with(".conf.json") {
            files.push((format!("src-tauri/{name}"), entry.path()));
        }
    }
    files.push(("index.html".to_owned(), root.join("index.html")));
    files.sort();
    files
}

#[test]
fn roster_and_hosts_absent_from_sources() {
    let names = plugin_names();
    let needles = needles(&names);
    let files = scanned_files();
    for must in [
        "crates/core/src/lib.rs",
        "crates/helper/src/main.rs",
        "src-tauri/src/lib.rs",
        "src-tauri/capabilities/main.json",
        "src-tauri/tauri.conf.json",
        "src/main.ts",
        "index.html",
    ] {
        assert!(
            files.iter().any(|(r, _)| r == must),
            "{must} was not scanned"
        );
    }
    for skipped in ["crates/core/src/test_support.rs", "src/main.test.ts"] {
        assert!(
            !files.iter().any(|(r, _)| r == skipped),
            "{skipped} is excluded but was scanned"
        );
    }
    let hits: Vec<String> = files
        .iter()
        .flat_map(|(rel, path)| {
            let bytes =
                fs::read(path).unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()));
            scan(rel, &String::from_utf8_lossy(&bytes), &needles)
        })
        .collect();
    assert!(
        hits.is_empty(),
        "{} plugin names read; roster or host hits in {} scanned files:\n{}",
        names.len(),
        files.len(),
        hits.join("\n")
    );
    println!(
        "{} plugin names read, {} files scanned, 0 hits",
        names.len(),
        files.len()
    );
}

#[test]
fn roster_scanner_reports_planted_names() {
    let names = plugin_names();
    let planted = &names[0];
    let rel = "crates/core/src/planted.rs";
    let text = format!(
        "fn kept() {{}}\n// {planted} is planted in a comment\nconst U: &str = \"https://{HOST}/repos\";\nconst L: &str = \"{}\";\n",
        planted.to_lowercase()
    );
    let hits = scan(rel, &text, &needles(&names));
    assert_eq!(
        hits,
        [format!("{rel}:2: {planted}"), format!("{rel}:3: {HOST}")],
        "the planted name and host, with their lines; the lower-cased name is not a hit"
    );
}
