//! The dev-feed override (FR-106), compiled only with the `dev-feed` feature.

use krate_manager_core::context::parse_dev_feed_url;
use std::path::Path;

pub const DEV_FEED_FILE: &str = "dev-feed-url.txt";

pub fn read(state_dir: &Path) -> Option<String> {
    std::fs::read_to_string(state_dir.join(DEV_FEED_FILE))
        .ok()
        .and_then(|t| parse_dev_feed_url(&t))
}
