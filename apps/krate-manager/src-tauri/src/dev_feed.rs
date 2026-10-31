//! The dev-feed override (FR-106), compiled only with the `dev-feed` feature.

use krate_manager_core::context::parse_dev_feed_url;
use krate_manager_core::http::{self, HttpConfig, ProxyResolution};
use krate_manager_core::pac::PacHostHandle;
use std::path::Path;

pub const DEV_FEED_FILE: &str = "dev-feed-url.txt";

pub fn read(state_dir: &Path) -> Option<String> {
    std::fs::read_to_string(state_dir.join(DEV_FEED_FILE))
        .ok()
        .and_then(|t| parse_dev_feed_url(&t))
}

/// The dev-feed URL's HTTP configuration (FR-061, PD-12).
pub fn http_config(
    url: &str,
    resolution: ProxyResolution,
    pac_host: Option<PacHostHandle>,
) -> HttpConfig {
    http::config_for_dev_url(url, resolution, pac_host)
}
