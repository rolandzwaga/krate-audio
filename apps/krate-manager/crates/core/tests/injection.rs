//! Source-scan guard (spec FR-021, SC-008; plan §4.10, PD-5, PD-22, PD-24): no production root
//! or URL in core, platform literals only from the allow-list, no filesystem token in the pure
//! modules, exactly the seven D6 `disallowed_methods` sites, and core's FR-007 dependency set.
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
const PLATFORM_LITERALS: [&str; 27] = [
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
];
/// The plan §4.1 D6 table, per file (app-root-relative); every other scanned file holds 0.
const D6_SITES: [(&str, usize); 5] = [
    ("src-tauri/src/lib.rs", 1),
    ("src-tauri/src/os_roots.rs", 2),
    ("crates/core/src/test_support.rs", 2),
    ("crates/core/src/roots_tests.rs", 1),
    ("crates/core/src/test_support_tests.rs", 1),
];
const D6_TOTAL: usize = 7;
const DISALLOWED_METHODS: &str = "clippy::disallowed_methods";
/// Lint groups that would silence `disallowed_methods` with no `disallowed_methods` token (PD-22).
const GROUP_SUPPRESSIONS: [&str; 3] = ["clippy::style", "clippy::all", "warnings"];
const FIXTURE_LIB: &str = "tests/fixtures/clippy-guard/src/lib.rs";
/// FR-007: the `[dependencies]` keys of `crates/core/Cargo.toml`.
const FR007_DEPENDENCIES: [&str; 6] =
    ["log", "serde", "serde_json", "tempfile", "thiserror", "url"];

const RULE_INLINE_TESTS: &str = "inline-test-module";
const RULE_SUBSTRING: &str = "forbidden-substring";
const RULE_PLATFORM_LITERAL: &str = "platform-literal-not-allow-listed";
const RULE_PLATFORM_RAW: &str = "platform-raw-literal";
const RULE_FS_TOKEN: &str = "filesystem-token";
const RULE_SANDBOXED: &str = "sandboxed-outside-roots";

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
            out.push(&code[start..i]);
        }
        if !c.is_whitespace() {
            out.push(&code[i..i + c.len_utf8()]);
        }
    }
    if let Some(start) = word {
        out.push(&code[start..]);
    }
    out
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
    let mut files = Vec::new();
    let crates = root.join("crates");
    let mut crate_names: Vec<String> = fs::read_dir(&crates)
        .unwrap_or_else(|e| panic!("cannot list {}: {e}", crates.display()))
        .map(|entry| entry.unwrap_or_else(|e| panic!("cannot list crates: {e}")))
        .filter(|entry| entry.path().is_dir())
        .map(|entry| entry.file_name().to_string_lossy().into_owned())
        .collect();
    crate_names.sort();
    for name in &crate_names {
        for sub in ["src", "tests"] {
            let dir = crates.join(name).join(sub);
            if dir.is_dir() {
                files.extend(rs_files(&dir, &format!("crates/{name}/{sub}/")));
            }
        }
    }
    files.extend(rs_files(
        &root.join("src-tauri").join("src"),
        "src-tauri/src/",
    ));
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
    let manifest = read_text(&app_root().join("crates").join("core").join("Cargo.toml"));
    let keys = section_keys(&manifest, "[dependencies]");
    let expected: BTreeSet<String> = FR007_DEPENDENCIES.into_iter().map(String::from).collect();
    assert_eq!(keys, expected, "crates/core/Cargo.toml [dependencies]");
}

#[test]
fn src_tauri_names_feed_host_once_and_reads_env_only_in_os_roots() {
    let files: Vec<(String, String)> = rs_files(&app_root().join("src-tauri").join("src"), "")
        .into_iter()
        .filter(|(rel, _)| !rel.ends_with("_tests.rs"))
        .collect();
    for must in ["lib.rs", "os_roots.rs"] {
        assert!(
            files.iter().any(|(r, _)| r == must),
            "src-tauri/src/{must} not found"
        );
    }
    let feed: Vec<(&str, usize)> = files
        .iter()
        .map(|(rel, text)| (rel.as_str(), text.matches("krateaudio.com").count()))
        .filter(|(_, n)| *n > 0)
        .collect();
    assert_eq!(feed, [("lib.rs", 1)], "krateaudio.com in src-tauri/src");
    let env: Vec<&str> = files
        .iter()
        .filter(|(_, text)| text.contains("var_os"))
        .map(|(rel, _)| rel.as_str())
        .collect();
    assert_eq!(env, ["os_roots.rs"], "var_os in src-tauri/src");
}
