fn main() {
    tauri_build::try_build(tauri_build::Attributes::new().app_manifest(
        tauri_build::AppManifest::new().commands(&[
            "app_version",
            "startup_error",
            "plugin_status",
            "check_now",
            "open_release_page",
            "download_plugin",
        ]),
    ))
    .expect("tauri-build failed");
}
