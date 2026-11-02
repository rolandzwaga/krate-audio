fn main() {
    tauri_build::try_build(tauri_build::Attributes::new().app_manifest(
        tauri_build::AppManifest::new().commands(&[
            "app_version",
            "startup_error",
            "plugin_status",
            "check_now",
            "open_release_page",
            "download_plugin",
            "install_plugin",
            "remove_plugin",
            "manager_update_check",
            "manager_update_install",
        ]),
    ))
    .expect("tauri-build failed");
}
