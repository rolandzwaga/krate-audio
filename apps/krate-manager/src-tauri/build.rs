fn main() {
    tauri_build::try_build(
        tauri_build::Attributes::new().app_manifest(
            tauri_build::AppManifest::new().commands(&["app_version", "startup_error"]),
        ),
    )
    .expect("tauri-build failed");
}
