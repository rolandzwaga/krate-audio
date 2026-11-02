//! PD-12: on Windows, embed the Common-Controls v6 application manifest tauri-build embeds for the app, so the
//! harness binary loads the same comctl32 the app does.

/// Byte copy of tauri-build 2.7.4's `src/windows-app-manifest.xml` (its `lib.rs:360` default `app_manifest`).
const MANIFEST: &str = r#"<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <dependency>
    <dependentAssembly>
      <assemblyIdentity
        type="win32"
        name="Microsoft.Windows.Common-Controls"
        version="6.0.0.0"
        processorArchitecture="*"
        publicKeyToken="6595b64144ccf1df"
        language="*"
      />
    </dependentAssembly>
  </dependency>
</assembly>
"#;

fn main() {
    #[expect(
        clippy::disallowed_methods,
        reason = "a build script reads cargo's CARGO_CFG_TARGET_OS"
    )]
    let target_os = std::env::var("CARGO_CFG_TARGET_OS");
    if target_os.as_deref() == Ok("windows") {
        tauri_winres::WindowsResource::new()
            .set_manifest(MANIFEST)
            .compile()
            .expect("embed the Windows application manifest");
    }
}
