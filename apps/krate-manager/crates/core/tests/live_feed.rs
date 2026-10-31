//! The opt-in live-network feed test (spec FR-072, SC-019; plan §4.11.4, PD-14). Compiled only
//! with `--features live-network`, ignored even then, and never run by any workflow, script or
//! gate. The one manual run: `cargo test -p krate-manager-core --features live-network --test
//! live_feed -- --ignored`.
#![cfg(feature = "live-network")]

use krate_manager_core::context::assemble_context;
use krate_manager_core::feed::{self, FetchMode, SnapshotSource};
use krate_manager_core::http::HttpConfig;
use krate_manager_core::test_support::{fresh_temp_roots, kat_signing_key};
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

/// The production feed URL, read from the `const FEED_URL: &str = "…";` line of
/// `src-tauri/src/lib.rs` so this file names no host.
fn feed_url() -> String {
    let lib = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("src-tauri")
        .join("src")
        .join("lib.rs");
    let text = std::fs::read_to_string(&lib)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", lib.display()));
    let lines: Vec<&str> = text
        .lines()
        .filter(|l| l.trim_start().starts_with("const FEED_URL: &str = \""))
        .collect();
    let [line] = lines[..] else {
        panic!(
            "{} holds {} FEED_URL lines, not one",
            lib.display(),
            lines.len()
        );
    };
    line.trim()
        .strip_prefix("const FEED_URL: &str = \"")
        .and_then(|rest| rest.strip_suffix("\";"))
        .unwrap_or_else(|| panic!("unexpected FEED_URL line: {line}"))
        .to_owned()
}

#[test]
#[ignore = "live network; opt-in only, never in a gate"]
fn live_feed_parses() {
    let url = feed_url();
    let (_td, roots) = fresh_temp_roots();
    let state = assemble_context(Ok(roots), &url, HttpConfig::production(), kat_signing_key());
    let ctx = state
        .context
        .as_ref()
        .unwrap_or_else(|e| panic!("no context: {e}"));
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("the clock is after 1970")
        .as_secs();
    let check = feed::check(ctx, FetchMode::Normal, now);
    let snapshot = check
        .snapshot
        .unwrap_or_else(|| panic!("{url}: no snapshot; error {:?}", check.error));
    assert_eq!(snapshot.source, SnapshotSource::Fresh, "{url}");
    assert!(!snapshot.feed.plugins.is_empty(), "{url}: no plugins");
}
