//! contract tests (plan §4.2, §7.1): T005. Fixtures are read in place from the repo's
//! `tools/feed-contract/fixtures/` (Phase 1); nothing is written.

use super::*;
use serde_json::json;
use std::fs;
use std::path::{Path, PathBuf};

fn fixtures() -> PathBuf {
    // crates/core -> crates -> krate-manager -> apps -> the repo root
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../../tools/feed-contract/fixtures")
}

fn read_json(path: &Path) -> Value {
    let bytes = fs::read(path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()));
    serde_json::from_slice(&bytes).unwrap_or_else(|e| panic!("parse {}: {e}", path.display()))
}

fn unsupported(schema: &str) -> Option<(String, String)> {
    match compile_schema_text(schema) {
        Err(ContractError::Unsupported { pointer, keyword }) => Some((pointer, keyword)),
        Err(other) => panic!("{schema}: not Unsupported: {other}"),
        Ok(_) => None,
    }
}

/// The exact, sorted `(code, instance_path, keyword)` list of `instance` under `schema`.
fn triples(schema: &str, instance: &str) -> Vec<(String, String, String)> {
    let compiled = match compile_schema_text(schema) {
        Ok(c) => c,
        Err(e) => panic!("{schema}: {e}"),
    };
    let doc: Value = serde_json::from_str(instance).expect("instance is JSON");
    let mut out: Vec<_> = compiled
        .evaluate(&doc)
        .into_iter()
        .map(|f| {
            (
                f.code.to_owned(),
                f.instance_path,
                f.keyword.unwrap_or("").to_owned(),
            )
        })
        .collect();
    out.sort();
    out
}

fn expect(pairs: &[(&str, &str)]) -> Vec<(String, String, String)> {
    let mut out: Vec<_> = pairs
        .iter()
        .map(|(p, k)| ("E_SCHEMA".to_owned(), (*p).to_owned(), (*k).to_owned()))
        .collect();
    out.sort();
    out
}

fn rule_code(result: Result<impl Sized, ContractError>) -> Option<&'static str> {
    match result {
        Ok(_) => None,
        Err(ContractError::Rule { code, .. }) => Some(code.as_str()),
        Err(e @ ContractError::Unsupported { .. }) => panic!("unexpected {e}"),
    }
}

#[test]
fn embedded_schemas_compile() {
    for (name, text) in [("v2", V2_SCHEMA_TEXT), ("v1", V1_SCHEMA_TEXT)] {
        if let Err(e) = compile_schema_text(text) {
            panic!("the embedded {name} schema does not compile: {e}");
        }
    }
}

#[test]
fn unsupported_schema_constructs_are_refused() {
    let rows: [(&str, &str, &str); 7] = [
        (r#"{"minLength":1}"#, "", "minLength"),
        (r#"{"type":"array"}"#, "", "type"),
        (r##"{"$ref":"#/$defs/missing"}"##, "", "$ref"),
        (r#"{"$ref":"other.json"}"#, "", "$ref"),
        (r#"{"pattern":"("}"#, "", "pattern"),
        (r#"{"pattern":"\\w"}"#, "", "pattern"),
        (
            r#"{"properties":{"a":{"minLength":1}}}"#,
            "/properties/a",
            "minLength",
        ),
    ];
    for (schema, pointer, keyword) in rows {
        assert_eq!(
            unsupported(schema),
            Some((pointer.to_owned(), keyword.to_owned())),
            "{schema}"
        );
    }
    assert_eq!(unsupported(r#"{"type":"object","$comment":"x"}"#), None);
}

#[test]
fn keyword_semantics_cases() {
    // (1) if / then without else
    let s1 = r#"{"if":{"required":["a"]},"then":{"required":["p"]}}"#;
    assert_eq!(
        triples(s1, r#"{"a":1}"#),
        expect(&[("", "required"), ("", "if")])
    );
    assert_eq!(triples(s1, r#"{"a":1,"p":2}"#), expect(&[]));
    assert_eq!(triples(s1, "{}"), expect(&[]));
    // (2) if / then / else, the else branch failing
    let s2 = r#"{"if":{"required":["a"]},"then":{},"else":{"required":["p"]}}"#;
    assert_eq!(triples(s2, "{}"), expect(&[("", "required"), ("", "if")]));
    assert_eq!(triples(s2, r#"{"p":1}"#), expect(&[]));
    // (3) propertyNames: the key's finding at the object's path
    let s3 = r#"{"properties":{"o":{"propertyNames":{"pattern":"^a"}}}}"#;
    assert_eq!(
        triples(s3, r#"{"o":{"b":1,"ab":2}}"#),
        expect(&[("/o", "pattern"), ("/o", "propertyNames")])
    );
    assert_eq!(triples(s3, r#"{"o":{"ab":1}}"#), expect(&[]));
    // (4) enum, numbers mathematically
    let s4 = r#"{"enum":["x","y"]}"#;
    assert_eq!(triples(s4, r#""z""#), expect(&[("", "enum")]));
    assert_eq!(triples(s4, r#""x""#), expect(&[]));
    assert_eq!(triples(r#"{"enum":[1,"x"]}"#, "1.0"), expect(&[]));
    // (5) const
    let s5 = r#"{"const":2}"#;
    assert_eq!(triples(s5, "2.0"), expect(&[]));
    assert_eq!(triples(s5, "3"), expect(&[("", "const")]));
    // (6) integer
    let s6 = r#"{"type":"integer"}"#;
    assert_eq!(triples(s6, "2.0"), expect(&[]));
    assert_eq!(triples(s6, "2.5"), expect(&[("", "type")]));
}

#[test]
fn c1_cases_reproduce() {
    let feeds = fixtures().join("feeds");
    let cases = read_json(&feeds.join("cases.json"));
    let rows = cases.as_array().expect("cases.json is an array");
    let mut visited = 0usize;
    let mut failures = Vec::new();
    for row in rows {
        visited += 1;
        let file = row["file"].as_str().expect("row.file");
        let schema = match row["schema"].as_str() {
            Some("v2") => Schema::V2,
            Some("v1") => Schema::V1,
            other => panic!("{file}: schema {other:?}"),
        };
        let bytes = fs::read(feeds.join(file)).unwrap_or_else(|e| panic!("read {file}: {e}"));
        let expect = &row["expect"];
        if expect == &json!({ "error": "E_INPUT" }) {
            if serde_json::from_slice::<Value>(&bytes).is_ok() {
                failures.push(format!("{file}: E_INPUT row parses as JSON"));
            }
            continue;
        }
        let doc: Value = serde_json::from_slice(&bytes)
            .unwrap_or_else(|e| panic!("{file} is not JSON but expects {expect}: {e}"));
        let findings = validate(&doc, schema);
        let show = |list: &[Finding]| {
            list.iter()
                .map(|f| format!("{} {:?} {:?}", f.code, f.instance_path, f.keyword))
                .collect::<Vec<_>>()
        };
        match expect {
            Value::String(s) if s == "accept" || s == "accept-without-platforms" => {
                if !findings.is_empty() {
                    failures.push(format!("{file}: {s} but {:?}", show(&findings)));
                }
                if schema == Schema::V2 {
                    let v1 = validate(&doc, Schema::V1);
                    if !v1.is_empty() {
                        failures.push(format!("{file}: v2 {s} but v1 {:?}", show(&v1)));
                    }
                }
            }
            Value::Object(o) => {
                let wanted = o["error"].as_str().expect("expect.error is a string");
                let hit = findings.iter().any(|f| {
                    f.code == wanted
                        || f.keyword
                            .is_some_and(|k| format!("{} {k}", f.instance_path) == wanted)
                });
                if !hit {
                    failures.push(format!(
                        "{file}: {wanted:?} not among {:?}",
                        show(&findings)
                    ));
                }
            }
            other => panic!("{file}: unknown expect {other}"),
        }
    }
    println!("c1_cases_reproduce: {visited} rows");
    assert_eq!(visited, rows.len());
    assert!(visited > 0, "cases.json has no rows");
    assert!(
        failures.is_empty(),
        "{} rows differ:\n{}",
        failures.len(),
        failures.join("\n")
    );
}

#[test]
fn gen_feed_outputs_accepted() {
    let root = fixtures().join("gen-feed");
    let mut dirs: Vec<PathBuf> = fs::read_dir(&root)
        .unwrap_or_else(|e| panic!("read {}: {e}", root.display()))
        .map(|e| e.expect("dir entry").path())
        .filter(|p| p.is_dir())
        .collect();
    dirs.sort();
    let (mut v2, mut legacy) = (0usize, 0usize);
    let mut failures = Vec::new();
    for dir in &dirs {
        for (name, schema, count) in [
            ("expected.v2.json", Schema::V2, &mut v2),
            ("expected.legacy.json", Schema::V1, &mut legacy),
        ] {
            let path = dir.join(name);
            if !path.is_file() {
                continue;
            }
            *count += 1;
            let findings = validate(&read_json(&path), schema);
            if !findings.is_empty() {
                failures.push(format!("{}: {findings:?}", path.display()));
            }
        }
    }
    println!("gen_feed_outputs_accepted: {v2} expected.v2.json, {legacy} expected.legacy.json");
    assert!(v2 >= 1, "no expected.v2.json under {}", root.display());
    assert!(
        legacy >= 1,
        "no expected.legacy.json under {}",
        root.display()
    );
    assert!(failures.is_empty(), "{}", failures.join("\n"));
}

#[test]
fn ecma_pattern_translation() {
    let schema: Value = serde_json::from_str(V2_SCHEMA_TEXT).expect("v2 schema is JSON");
    let compile = |p: &str| {
        let rust = ecma_pattern_to_rust(p).unwrap_or_else(|e| panic!("{p}: {e}"));
        regex::Regex::new(&rust).unwrap_or_else(|e| panic!("{rust}: {e}"))
    };
    let url = compile(
        schema["$defs"]["url"]["pattern"]
            .as_str()
            .expect("url pattern"),
    );
    assert!(
        !url.is_match("https://a\u{FEFF}b"),
        "U+FEFF is ECMA whitespace"
    );
    assert!(
        url.is_match("https://a\u{0085}b"),
        "U+0085 is not ECMA whitespace"
    );
    let stem = compile(
        schema["$defs"]["reservedStem"]["pattern"]
            .as_str()
            .expect("reservedStem pattern"),
    );
    assert!(stem.is_match("CON.txt"));
    assert!(!stem.is_match("CON.\r"), "ECMA . excludes CR");
    assert!(!stem.is_match("CON.\u{2028}"), "ECMA . excludes U+2028");
    for refused in [r"\d", r"\w", r"\b"] {
        assert!(
            matches!(
                ecma_pattern_to_rust(refused),
                Err(ContractError::Unsupported { .. })
            ),
            "{refused}"
        );
    }
}

#[test]
fn trusted_comment_cases() {
    let doc = read_json(&fixtures().join("trusted-comments.json"));
    let valid = doc["valid"].as_array().expect("valid rows");
    assert!(!valid.is_empty());
    for row in valid {
        let s = row.as_str().expect("valid row is a string");
        let f: Vec<&str> = s.split(':').collect();
        let parsed = parse_trusted_comment(s).unwrap_or_else(|e| panic!("{s}: {e}"));
        assert_eq!(
            parsed,
            TrustedComment {
                name: f[2].to_owned(),
                version: f[3].to_owned(),
                platform: f[4].to_owned(),
                asset: f[5].to_owned(),
            },
            "{s}"
        );
    }
    let invalid = doc["invalid"].as_array().expect("invalid rows");
    assert!(!invalid.is_empty());
    for row in invalid {
        let s = row["comment"].as_str().expect("comment");
        let code = row["code"].as_str().expect("code");
        assert_eq!(rule_code(parse_trusted_comment(s)), Some(code), "{s:?}");
    }
}

#[test]
fn minisig_text_cases() {
    let kat_path = fixtures().join("kat/KatPlugin-1.2.3-Linux-x64.tar.gz.minisig");
    let kat_text = fs::read_to_string(&kat_path).expect("KAT minisig");
    let kat = parse_minisig_text(&kat_text).unwrap_or_else(|e| panic!("KAT: {e}"));
    // minisign prints the key id as the little-endian u64 in hex
    assert_eq!(
        format!("{:016X}", u64::from_le_bytes(kat.key_id)),
        "440FA75168B852F7"
    );
    if let Err(e) = parse_trusted_comment(&kat.trusted_comment) {
        panic!("KAT trusted comment: {e}");
    }

    let feeds = fixtures().join("feeds");
    let full = read_json(&feeds.join("valid/full-v2.json"));
    let mut seen = 0usize;
    for (name, entry) in full["plugins"].as_object().expect("plugins") {
        for part in [Some(entry), entry.get("prerelease")].into_iter().flatten() {
            let Some(platforms) = part.get("platforms").and_then(Value::as_object) else {
                continue;
            };
            for (key, platform) in platforms {
                let sig = platform["signature"].as_str().expect("signature");
                if let Err(e) = parse_minisig_text(sig) {
                    panic!("{name} {key}: {e}");
                }
                seen += 1;
            }
        }
    }
    assert!(seen > 0, "full-v2.json has no platform signature");

    let alpha_linux = |file: &str| -> String {
        let doc = read_json(&feeds.join("invalid").join(file));
        doc["plugins"]["Alpha"]["platforms"]["linux-x86_64"]["signature"]
            .as_str()
            .unwrap_or_else(|| panic!("{file}: no Alpha linux-x86_64 signature"))
            .to_owned()
    };
    for file in [
        "signature-legacy-Ed.json",
        "signature-blob-length.json",
        "signature-noncanonical-base64.json",
    ] {
        assert_eq!(
            rule_code(parse_minisig_text(&alpha_linux(file))),
            Some("E_SIGNATURE_TEXT"),
            "{file}"
        );
    }
    let tc = parse_minisig_text(&alpha_linux("signature-trusted-comment.json"))
        .unwrap_or_else(|e| panic!("signature-trusted-comment.json: {e}"));
    assert_eq!(
        rule_code(parse_trusted_comment(&tc.trusted_comment)),
        Some("E_TC_FIELDS")
    );
    for file in ["signature-cr.json", "signature-no-final-lf.json"] {
        if let Err(e) = parse_minisig_text(&alpha_linux(file)) {
            panic!("{file} must parse outside a feed: {e}");
        }
    }
}
