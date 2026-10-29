# Cross-platform desktop framework comparison for the Krate Audio plugin manager (Tauri 2 vs. alternatives, as of 2026-10-29)

Scope: a small Windows/macOS/Linux app that lists installed Krate plugins, checks GitHub Releases for updates, downloads/verifies/unzips/copies bundles, uninstalls, and updates itself. Team: C++ experts, Node.js tooling, **no code-signing certificates ever**. Everything below was checked against sources fetched on 2026-10-29; older material is marked.

Conventions: "API" = the GitHub REST API `repos/...` endpoint fetched today (license field, star count, last push). Star counts are included only as a rough activity signal.

## Key question 1: Candidate-by-candidate state in 2026 (release, maturity, size, memory, startup, Linux backend, min OS, WebView2)

### Takeaway
Every webview framework (Tauri, Wails, Dioxus-desktop) shares the same three engines and therefore the same Linux WebKitGTK/NVIDIA/Wayland pain; Electron sidesteps it by shipping Chromium at ~150-290 MB per install. Among non-webview options, Qt 6.12 LTS and Avalonia 12 are the mature choices; Flutter desktop, iced, egui, Slint and Dioxus Native are usable but either young, renderer-in-flux, or widget-poor for a forms-style app. Wails v3.0.0 shipped three days before this research and cannot yet be called proven.

### Cited Findings

**Tauri 2.x**
- Latest core release is `tauri-v2.12.5` (published 2026-10-21), alongside `tauri-cli-v2.12.4`, `tauri-bundler-v2.10.4` and `tauri-runtime-wry-v2.12.3` — [GitHub releases API](https://api.github.com/repos/tauri-apps/tauri/releases?per_page=8). Core repo: 112,296 stars, last push 2026-10-28 — [GitHub API](https://api.github.com/repos/tauri-apps/tauri).
- The updater plugin is at `updater-v2.14.1` (2026-10-28), with a `updater-v3.0.0-alpha.3` pre-release (2026-10-14) in flight; shell plugin at `shell-v2.6.0` (2026-10-28) — [plugins-workspace releases API](https://api.github.com/repos/tauri-apps/plugins-workspace/releases?per_page=40).
- Engines: WebView2 (Chromium) on Windows, "supported on Windows 7 and newer"; WKWebView on macOS, "the WebKit that ships with the OS", which "updates only with OS updates" and "unsupported macOS versions do not receive WebKit updates"; `webkit2gtk` on Linux where "the version depends on the distribution" — [Tauri webview-versions reference](https://v2.tauri.app/reference/webview-versions/).
- Prerequisites: Linux needs `libwebkit2gtk-4.1` plus `libayatana-appindicator3` / `libappindicator-gtk3`, `librsvg`, `libxdo`; macOS "Catalina (10.15) and later"; Windows "7 and later" with WebView2 "preinstalled on Windows 10 (version 1803 onward)" — [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/).
- WebView2 install modes and added installer size: `downloadBootstrapper` (default, 0 MB, needs internet), `embedBootstrapper` (~1.8 MB, needs internet), `offlineInstaller` (~127 MB, no internet), `fixedRuntime` (~180 MB, no internet), `skip` (0 MB, "not recommended") — [Tauri Windows installer guide](https://v2.tauri.app/distribute/windows-installer/).
- Linux graphics problems are documented first-party: blank/white window, flicker, crash on resize, `AcceleratedSurfaceDMABuf was unable to construct a complete framebuffer`, `Gdk-Message: Error 71 (Protocol error) dispatching to Wayland display`, and silent WebGL slowdowns; "most often on NVIDIA GPUs". Workarounds in order: `nvidia_drm.modeset=1` (drivers older than 545), `__NV_DISABLE_EXPLICIT_SYNC=1`, `WEBKIT_DISABLE_DMABUF_RENDERER=1` (disables the fast path), `WEBKIT_DISABLE_COMPOSITING_MODE=1` (last resort); the docs warn against shipping the env var unconditionally — [Tauri Linux graphics debug page](https://v2.tauri.app/develop/debug/linux-graphics/); upstream bug [WebKit 261874 "GTK 3 rendering broken with 2.42 on NVIDIA"](https://bugs.webkit.org/show_bug.cgi?id=261874); the originating Tauri issue is tauri-apps/tauri#9394 (cited on that page).
- A Tauri-based commercial app (Yaak) publishes the same workaround page for its users, i.e. the problem reaches end users of shipped apps — [Yaak Linux graphics issues](https://yaak.app/docs/getting-started/linux-graphics-issues).
- Six-month production report (Tabularis DB client, 64 releases, published 2026-07-29): installer sizes Tauri vs Electron (Beekeeper Studio) — Linux .deb 17 vs 207 MB, macOS .dmg 18 vs 290 MB, Windows installer 12 vs 201 MB, Linux AppImage 94 vs 282 MB (the AppImage "is mostly bundled WebKitGTK"); pain: Wayland protocol errors, blank AppImage window, a WebKit process abort on Solus still open, three engines disagreeing on CSS/input ("macOS converted straight quotes ... to curly quotes"), glibc mismatch (built on Ubuntu 24.04/glibc 2.39, failed on 22.04/glibc 2.35, fixed by building in a 22.04 container); verdict "would choose Tauri again ... budget for the seams" — [HackerNoon: Six months with Tauri](https://hackernoon.com/six-months-with-tauri-the-benefits-and-the-bill).
- DoltHub (2025-11-13, older) rebuilt Dolt Workbench in Tauri: "a minimal Hello World Electron app can take up to 150 megabytes"; Tauri's JS APIs "covered most needs, and the migration used only minimal Rust code"; blockers were no `.appx/.msix` target and macOS universal-binary signing problems — [DoltHub blog](http://www.dolthub.com/blog/2025-11-13-electron-vs-tauri/).
- Tauri's own benchmark page describes the method (hyperfine, 3 warm-ups + 10 runs to `DOMContentLoaded`; `time -v` max RSS, Linux only; GitHub Actions runners) but the chart data did not render in a text fetch, so no first-party numbers could be read — [tauri-apps benchmark_results](https://tauri-apps.github.io/benchmark_results/).
- Secondary, unverified blog benchmarks (methodology not auditable, treat as order-of-magnitude only): idle RSS ~42 MB Tauri vs ~168 MB Electron — [tech-insider](https://tech-insider.org/tauri-vs-electron-2026/); six-window app 172 MB vs 409 MB — [noqta](https://noqta.tn/en/blog/tauri-2-desktop-apps-rust-web-technologies-2026).
- Linux distro baseline: Tauri recommends building on the oldest base you support that ships WebKitGTK 4.1 packages, naming Ubuntu 22.04 and Debian 12 — [Tauri Debian guide (de mirror of the same page)](https://v2.tauri.app/de/distribute/debian/). Ubuntu package pools show `libwebkit2gtk-4.0` only for 20.04 — [Ubuntu webkit2gtk pool](https://mirrors.mit.edu/ubuntu/pool/universe/w/webkit2gtk). Tauri 2 moved the Linux port from webkit2gtk-4.0 to 4.1 in 2.0.0-alpha.3 (2023, older) — [Tauri 2.0.0-alpha.3 blog](https://v2.tauri.app/ja/blog/tauri-2-0-0-alpha-3/).

**Electron**
- Latest stable 45.1.0 (2026-10-27), Chromium 156.0.8078.49, Node.js 24.22.1; supported majors 45, 44 (44.10.0), 43 (43.8.4) — [releases.electronjs.org](https://releases.electronjs.org/).
- "The latest three stable major versions are supported"; "cadence between major version releases is 8 weeks" — [Electron timelines](https://www.electronjs.org/docs/latest/tutorial/electron-timelines).
- Platform support: macOS "Ventura and up" (Intel + Apple Silicon); Windows "10 and up" (x64, arm64); Linux x64/arm64 on Ubuntu/Fedora/Debian versions still supported by Chromium and the distro, prebuilt on Ubuntu; no glibc number stated — [Electron README](https://github.com/electron/electron/blob/main/README.md).
- Bundles its own Chromium, so there is no WebView2/WebKitGTK runtime dependency (inference from the README: "aligns its platform support with Chromium").

**Qt 6 (C++ / QML)**
- Qt 6.12 LTS: macOS target "macOS 14.4 or higher (including macOS 26)", Xcode 16 — [Qt for macOS 6.12](https://doc.qt.io/qt-6.12/macos.html); Windows "10 version 1809 or later and Windows 11", MSVC 2022 / MinGW-w64 15.1, ARM64 supported, "Qt 6.12 will be the last version to support Windows 10" — [Qt for Windows 6.12](https://doc.qt.io/qt-6.12/windows.html).
- Qt 6.12 LTS was announced 2026-09-30 with five-year maintenance (secondary source) — [alternativeto news](https://alternativeto.net/news/2026/9/qt-6-12-lts-brings-cra-compliance-qml-hot-reload-qt-canvas-painter-and-harmonyos-support/).
- Cadence: "Minor releases of Qt happen twice a year", "two or three patch releases" each, "every fourth minor Qt version gets additional LTS releases" since 6.8 — [Qt Releases](https://doc.qt.io/qt-6/qt-releases.html).

**wxWidgets**
- 3.3.0 (June 2025) opened the 3.3 development branch — [wxWidgets 3.3.0 released](https://www.wxwidgets.org/news/2025/06/wxwidgets-3.3.0-released/); 3.2.10 (stable) and 3.3.2 shipped March 2026 with "roughly 1000 changes from 61 contributors" — [wxWidgets 3.2.10 and 3.3.2 released](https://wxwidgets.org/news/2026/03/wxwidgets-3.2.10-and-3.3.2-released).
- No first-party installer or updater exists in the wxWidgets project (no source found; see Gaps).

**Flutter desktop**
- Supported desktop matrix: Windows 10/11 (x64, Arm64); macOS 12 Monterey through 27 (x64, Arm64) with a warning that Intel macOS support is being phased out; Debian 10-13; Ubuntu 20.04-24.04 LTS — [Flutter supported platforms](https://docs.flutter.dev/reference/supported-platforms). Repo: BSD-3-Clause, 180,539 stars, pushed 2026-10-29 — [GitHub API](https://api.github.com/repos/flutter/flutter).
- Flutter 3.47 (Aug 2026) reportedly made Impeller the default renderer on Windows, Linux and macOS (secondary source) — [startdebugging.net](https://startdebugging.net/2026/08/flutter-3-47-impeller-default-renderer-on-desktop/); Flutter's 2026 roadmap says Canonical is driving multi-window/desktop work — [Flutter & Dart 2026 roadmap](https://flutter.dev/blog/flutter-darts-2026-roadmap).

**Avalonia (.NET)**
- MIT, 31,726 stars, pushed 2026-10-29 — [GitHub API](https://api.github.com/repos/AvaloniaUI/Avalonia). Wikipedia lists stable 12.1.4 (2026-10-09); a Korean blog dates the 12.0 release to 2026-04-07 (secondary) — [Wikipedia](https://en.wikipedia.org/wiki/Avalonia_(software_framework)), [wikidocs blog](https://wikidocs.net/blog/@lukewire/9758/).
- Supported platforms: Windows 11 24H2 Tier 1, Windows 10 22H2 Tier 2; macOS 26 Tier 1, 14/15 Tier 2, 13 and earlier Tier 3; Linux Tier 1 Ubuntu 25.x / Fedora 43 / Debian 13, Tier 2 back to Ubuntu 16.04 / Debian 9; renders via Skia over OpenGL on X11, a native Wayland backend "opt-in ... starting with Avalonia 12.1.0"; Skia built against glibc 2.17; needs `libx11-6 libice6 libsm6 libfontconfig1` — [Avalonia supported platforms](https://docs.avaloniaui.net/docs/overview/supported-platforms).

**Rust-native GUI (iced, egui, Slint)**
- iced: MIT, 31,866 stars, pushed 2026-10-21 — [GitHub API](https://api.github.com/repos/iced-rs/iced); 0.14 released 2025-12-07 (reactive rendering, time-travel debugging, headless testing, IME) — [Phoronix](https://www.phoronix.com/news/Iced-0.14-Rust-GUI-LIbrary), [iced 0.14.0 release](https://github.com/iced-rs/iced/releases/tag/0.14.0). Still pre-1.0.
- egui: GitHub API reports Apache-2.0 (the project is commonly dual MIT/Apache; API shows one), 31,849 stars, pushed 2026-10-26 — [GitHub API](https://api.github.com/repos/emilk/egui); 0.36.0 is the latest minor (secondary listing) — [newreleases.io egui 0.36.0](https://newreleases.io/project/github/emilk/egui/release/0.36.0). Still pre-1.0.
- Slint: v1.18.1 (2026-09-21, "a patch release that fixes several bugs and crashes"), v1.18.0 (2026-09-16) — [GitHub releases API](https://api.github.com/repos/slint-ui/slint/releases?per_page=3). Has C++, Rust and Node.js APIs (upgrade notes cover all three on the same page).

**Dioxus desktop**
- GitHub API reports Apache-2.0 (project is commonly dual MIT/Apache), 39,529 stars, pushed 2026-10-29 — [GitHub API](https://api.github.com/repos/DioxusLabs/dioxus). Dioxus 0.7 shipped 2025-09-08; Dioxus Native/Blitz "still considered a work in progress" and "doesn't always produce the best outputs" — [Dioxus 0.7 release blog](https://dioxuslabs.com/blog/release-070). The default desktop renderer remains a webview; the blog does not say so explicitly (see Gaps), but Blitz is presented as the alternative to it.
- A single secondary report says the Dioxus team joined Cognition on 2026-09-10 and will keep investing in Dioxus Native/Blitz — [unite.ai](https://www.unite.ai/?p=465293) (unverified).

**Wails (Go)**
- v3.0.0 published 2026-10-26 (after 34 betas; nightlies `v3.1.0-nightly.*` already running) — [releases API](https://api.github.com/repos/wailsapp/wails/releases?per_page=6). MIT, 36,811 stars — [GitHub API](https://api.github.com/repos/wailsapp/wails).
- v3.0.0 notes: Linux uses "GTK4 and WebKitGTK 6.0 by default", GTK3 kept behind `-tags gtk3` until v3.1; Go 1.26+; Windows amd64/arm64, macOS Intel/Apple Silicon, Linux amd64/arm64; iOS/Android "experimental"; advice to pin an exact version in production — [v3.0.0 release notes](https://api.github.com/repos/wailsapp/wails/releases/tags/v3.0.0).

**Plain Node.js / CLI baseline**
- No external sources needed; see Inferences.

### Inferences
- For a forms-style app with a list, a few buttons and progress bars, the webview frameworks' size advantage is real on Windows/macOS (12-18 MB vs 200-290 MB installers in the Tabularis data) but mostly evaporates on Linux AppImage (94 vs 282 MB) because WebKitGTK must be bundled.
- Tauri's Windows floor is effectively Windows 10 1803+ in practice: the docs list Windows 7, but WebView2 is only preinstalled from 1803 and Electron/Qt/Flutter all stop at Windows 10 anyway. The relevant Krate user base (DAW users) is on Windows 10/11 and macOS 11+, so no candidate is excluded on OS floor; Qt 6.12's macOS 14.4 floor and Electron's Ventura (13) floor are the tightest, Tauri's 10.15 the loosest.
- WebKitGTK 6.0 (GTK4) in Wails v3 and WebKitGTK 4.1 (GTK3) in Tauri 2 are different ABI series; Tauri's documented NVIDIA/Wayland pain is in the WebKitGTK DMABUF/compositing path and is not GTK-version specific (the WebKit bug 261874 is a WebKitGTK regression), so expect it on both.
- The Node.js/CLI baseline (an `npx`/single-binary CLI that lists, downloads, verifies and copies bundles) has no GUI runtime dependency at all, but it cannot satisfy the stated requirement of "shows which plugins are installed" for non-terminal users and cannot prompt for elevation gracefully; it is the right fallback for CI and power users, not the product.

### Gaps
- No first-party Tauri vs Electron memory/startup figures could be read (the benchmark page is chart-only); only blog numbers exist and their methodology is not auditable.
- No Wails v3 production report exists yet (released 3 days ago).
- Dioxus docs fetched do not state explicitly that desktop uses wry/WebKitGTK; inferred from Blitz being positioned as the non-webview alternative.
- Avalonia self-contained binary size and memory, Flutter desktop binary size/memory, Qt deployment size for this app class: no 2026 measurements found.

## Key question 2: First-party updater, installer bundling, and elevated sub-processes — and which REQUIRE platform code signing

### Takeaway
Tauri is the only candidate whose first-party updater is documented to work with its own key pair (minisign-style, "cannot be disabled") and no Apple/Microsoft certificate; Electron's built-in `autoUpdater` and electron-builder both state the macOS app "must be signed", and Velopack states macOS signing/notarization is "required". Elevation is not a framework feature anywhere; every stack shells out to UAC / `osascript` / `pkexec`, and the cleanest design avoids elevation entirely by installing to per-user plugin folders.

### Cited Findings

**Tauri**
- Updater: "Updates must be signed, and ... cannot be disabled"; keys via `tauri signer generate`; `pubkey` in `tauri.conf.json`; `TAURI_SIGNING_PRIVATE_KEY` (+ optional password) at build time; artifacts with `createUpdaterArtifacts: true` are Linux `.AppImage` + `.sig`, macOS `.app.tar.gz` + `.sig`, Windows NSIS `-setup.exe` and MSI each + `.sig`; static `latest.json` with `platforms.{linux,darwin,windows}-{x86_64,aarch64,...}.{url,signature}`; Windows `installMode` `passive` (default) / `basicUi` / `quiet`, where `quiet` "can't request admin rights itself, so it only works for per-user installs or when the app already runs elevated"; the app exits when the Windows installer runs — [Tauri updater plugin](https://v2.tauri.app/plugin/updater/). The page nowhere requires Apple or Microsoft signing; `.deb`/`.rpm` are not updater targets (AppImage only on Linux).
- Security advisory (GHSA-j38x-g3m3-95fr / CVE-2026-95625, published 2026-09-23, CVSS 5.9 Medium, CWE-354): minisign signatures "cover only the binary's raw bytes", the manifest "is fetched over TLS but is never signed", so an attacker controlling the manifest could force a downgrade to an older validly signed release; fixed in `updater-v2.12.0` — [OSV entry](https://osv.dev/vulnerability/GHSA-j38x-g3m3-95fr). Tauri's own 2.12.0 notes (2026-09-20) describe moving `allowDowngrades` from the `check` call into plugin config (default `false`) so webview code can no longer relax the version check — [updater 2.12.0 release notes](https://v2.tauri.app/release/updater/v2.12.0/); 2.13.0 (2026-09-26) raised MSRV to Rust 1.90 and fixed a Linux TLS cert-path bug — [updater 2.13.0 release notes](https://v2.tauri.app/release/updater/v2.13.0/). The OpenCVE page additionally claims an opt-in `requireSignedVersion` option; that text is AI-generated and CNA-sourced and could not be confirmed in Tauri's release notes — [OpenCVE CVE-2026-95625](https://app.opencve.io/cve/CVE-2026-95625).
- Windows signing: "not required to execute your application on Windows" if users accept the SmartScreen warning; exception: Windows 11 **Smart App Control** blocks unknown unsigned apps and users can only turn it off entirely; since 2024 EV certificates no longer grant instant SmartScreen reputation — [Tauri Windows signing](https://v2.tauri.app/distribute/sign/windows/).
- macOS signing: `"signingIdentity": "-"` gives an ad-hoc signature, "useful on Apple Silicon, where code-signing is required for apps from the Internet", but "does not stop macOS from making users approve the installation in Privacy & Security settings"; notarization needs a paid Developer ID — [Tauri macOS signing](https://v2.tauri.app/distribute/sign/macos/). The distribute overview still says "Signing is required on most platforms" — [Tauri distribute overview](https://v2.tauri.app/distribute/) — which the per-platform pages qualify as above.
- Bundles: Windows NSIS `-setup.exe` (cross-compilable, "less tested") and MSI via WiX v3 ("can only be created on Windows"); NSIS install modes `currentUser` (default, `%LOCALAPPDATA%`, no admin), `perMachine` (admin), `both` — [Windows installer guide](https://v2.tauri.app/distribute/windows-installer/). macOS `.app`/`.dmg`; Linux `.deb`, `.rpm`, AppImage as `tauri build` targets; Snapcraft, AUR, Flatpak as guides — [distribute overview](https://v2.tauri.app/distribute/). Flatpak is a manual `flatpak-builder` manifest that repackages the `.deb` on `org.gnome.Platform` 46/47 (page inconsistent), needs generated `cargo-sources.json`/`node-sources.json` for Flathub, and does not discuss the updater — [Tauri Flatpak guide](https://v2.tauri.app/distribute/flatpak/).
- Elevation: a bundler fix in tauri-bundler 2.5.1 "fixes NSIS per-machine installer not requesting elevation when run by non-admin users" (installer-level only) — [tauri-bundler 2.5.1](https://v2.tauri.app/release/tauri-bundler/v2.5.1/). No first-party Tauri plugin for running an elevated child process was found. The `privesc` Rust crate (MIT, 1.1.2/1.2.0 released 2025-12-25, single owner) does UAC on Windows, `osascript` auth dialog or `sudo` on macOS, `pkexec`/`sudo` on Linux — [docs.rs privesc](https://docs.rs/crate/privesc/1.1.2).

**Electron**
- Built-in `autoUpdater`: "Your application must be signed for automatic updates on macOS" (Squirrel.Mac); Windows auto-selects MSIX updater or Squirrel.Windows; Linux "no built-in auto-updater support", use the distro package manager — [Electron autoUpdater API](https://www.electronjs.org/docs/latest/api/auto-updater).
- electron-builder/electron-updater: "a macOS application must be signed for auto-updating to work"; Linux AppImage, DEB, Pacman and RPM are auto-updatable; Windows auto-update only with the NSIS target ("Squirrel.Windows is not supported"); Authenticode verification exposed as `NsisUpdater.verifyUpdateFileAuthenticodeSignature`; update manifest `latest*.yml` is Ed25519-signed as of v27 (publishing fails without a key unless `updateManifest: false`); GitHub Releases provider; the page does not say whether unsigned Windows builds update — [electron-builder auto-update](https://www.electron.build/docs/features/auto-update).
- Linux targets: defaults are AppImage and Snap when building on Linux; deb/rpm/pacman/apk/freebsd/p5p via FPM; flatpak available — [electron-builder Linux](https://www.electron.build/docs/linux), [electron-builder CLI](https://www.electron.build/docs/cli/).
- Elevation: `sudo-prompt` (UAC / pkexec-kdesudo / macOS native prompt) is marked **deprecated** on npm — [npm sudo-prompt](https://npmjs.com/package/sudo-prompt); `electron-sudo` uses `elevate` + UAC on Windows, `pkexec`/`gksudo` on Linux, a bundled applet on macOS — [npm electron-sudo](https://npmjs.com/package/electron-sudo). Native Access 2/3 (Electron) "calls a UAC prompt" that Wine does not support — [NI community thread](https://community.native-instruments.com/discussion/51744/native-instruments-native-access-available-on-linux).

**Qt**
- Qt Installer Framework is "a robust toolset for creating custom online and offline installers" (installer + maintenance tool, not an in-app updater) — [QtIFW docs](https://doc.qt.io/qtinstallerframework). No first-party in-app self-updater exists in Qt (no source found; see Gaps).

**Avalonia / .NET**
- Velopack (the de-facto cross-platform updater for Avalonia/WPF/Uno, "supports building packages for Windows, OSX, and Linux") — [Velopack docs](https://docs.velopack.io/). Its signing page: Windows "applications without code signatures are likely to be flagged as viruses" (optional); macOS "codesigning and Notarization is required before your application can be run by users"; Velopack's own `Update`/`Setup` binaries are signed during the build; Linux not addressed — [Velopack signing](https://docs.velopack.io/packaging/signing).

**Flutter**
- `auto_updater` is Sparkle (macOS) / WinSparkle (Windows) based; setup docs cover only macOS and Windows — [pub.dev auto_updater](https://pub.dev/packages/auto_updater). `desktop_updater` 3.2.0 describes Windows and Linux native pieces as still gated, "candidate-only preview" — [pub.dev desktop_updater 3.2.0](https://pub.dev/documentation/desktop_updater/3.2.0/).

**Wails v3**
- First-party: NSIS and MSIX (Windows), DMG and .app (macOS), AppImage/deb/rpm/Arch (Linux); an in-app updater "with pluggable providers (including GitHub Releases), cryptographic verification, an atomic swap with rollback, and a default UI" — [Wails v3.0.0 release notes](https://api.github.com/repos/wailsapp/wails/releases/tags/v3.0.0). Whether that updater needs Apple signing is not stated (Gap).

**Dioxus, iced, egui, Slint, wxWidgets**
- Dioxus: `dx` does "Packaging and bundling for Web/Desktop/Mobile"; no app updater described (only `dx self-update` for the CLI) — [Dioxus 0.7 blog](https://dioxuslabs.com/blog/release-070). No first-party updater or installer found for iced, egui, Slint or wxWidgets (Gap: absence confirmed only by not finding one).

**Platform facts that apply to all candidates (unsigned distribution)**
- macOS "App is damaged" / "not verified" arises from quarantine + lack of notarization, not corruption; ad-hoc signatures do not satisfy Gatekeeper but are needed on Apple Silicon — [Apple dev forum thread](https://developer.apple.com/forums/thread/758537), [macos-tahoe.com Gatekeeper guide](https://macos-tahoe.com/blog/app-is-damaged-cant-be-opened-macos-tahoe-gatekeeper-quarantine-fix-2026/), [OpenJDK jpackage quarantine discussion](https://mail.openjdk.org/pipermail/core-libs-dev/2022-May/089954.html).

### Inferences
- With no certificates, the self-update path that is documented to work on all three OSes is Tauri's (own-key signatures, `.app.tar.gz` replaced in place, NSIS passive install on Windows, AppImage swap on Linux). Electron and Velopack rule themselves out on macOS by their own docs; Wails v3 is undocumented on this point.
- The Tauri updater's macOS path replaces the bundle from a tarball the app itself downloaded; files written by a non-browser app are not normally quarantined, which is why unsigned/ad-hoc Tauri apps are reported to self-update. This is an inference, not a documented guarantee — the first install still hits Gatekeeper's right-click-Open / Privacy & Security dance for every framework.
- Enable `allowDowngrades: false` (default since 2.12.0) and keep the updater plugin ≥ 2.12.0 to close GHSA-j38x-g3m3-95fr; since GitHub Releases is the manifest host, the attack needs control of GitHub or the user's TLS path.
- Elevation should be designed out, not solved: install plugins to per-user folders (`%LOCALAPPDATA%\Programs\Common\VST3` on Windows — the location this repo already uses for Membrum per the session's working directories — `~/Library/Audio/Plug-Ins/{VST3,Components}` on macOS, `~/.vst3` on Linux) and offer the per-machine locations as an opt-in that spawns one elevated helper (UAC via ShellExecute `runas`, `osascript ... with administrator privileges`, `pkexec`). Every framework needs the same three shell-outs; Tauri's `shell` plugin plus a small Rust helper or `privesc` is no worse than Electron's deprecated `sudo-prompt`.
- Windows 11 Smart App Control will block the unsigned manager (and the unsigned plugin installers) regardless of framework; it is off by default on upgraded machines but on for some new OEM installs. Document it.

### Gaps
- Whether the Tauri updater's macOS replace-in-place step works on an unsigned (non-ad-hoc) bundle under macOS 15/26 is not stated in Tauri docs; only indirectly supported by the ad-hoc guidance. Needs a 10-minute test on a Mac.
- No source confirms whether Wails v3's new updater requires platform signing on macOS.
- Sparkle 2 behaviour for unsigned Flutter apps not researched.
- No first-party elevation API found for any framework; the gap is structural, not a research miss.

## Key question 3: CI story on free GitHub Actions runners

### Takeaway
Tauri has a first-party action that builds all three OSes and both macOS architectures and writes `latest.json` into the GitHub Release; Electron's electron-builder is equally routine; everyone else is "run the toolchain's build on three runners and upload". The Linux gotchas (glibc floor of the runner, WebKitGTK package names, MSI only on Windows) are the same for every webview stack.

### Cited Findings
- `tauri-apps/tauri-action` builds "a native binary for macOS, Linux, and Windows" and can upload to a GitHub Release; example matrix `macos-latest --target aarch64-apple-darwin`, `macos-latest --target x86_64-apple-darwin`, `ubuntu-22.04`, `windows-latest`; Ubuntu deps `libwebkit2gtk-4.1-dev libappindicator3-dev librsvg2-dev patchelf xdg-utils`; options `uploadUpdaterJson` (default true, "assume you're using the GitHub Release as your updater endpoint"), `updaterJsonPreferNsis`, `uploadUpdaterSignatures` (default true) — [tauri-action README](https://github.com/tauri-apps/tauri-action).
- MSI "can only be created on Windows"; NSIS can cross-compile from Linux/macOS but is "less tested"; "Signing cross compiled Windows installers requires an external signing tool" — [Tauri Windows installer guide](https://v2.tauri.app/distribute/windows-installer/).
- glibc floor: a Tauri app built on Ubuntu 24.04 (glibc 2.39) failed on 22.04 (2.35); fix was building in a 22.04 container — [HackerNoon Tabularis report](https://hackernoon.com/six-months-with-tauri-the-benefits-and-the-bill). One project pinned `webkit2gtk 2.44.0-2` on 24.04 because a newer webkit2gtk broke AppImages (commit in a Rust/Tauri project) — [xmr-btc-swap commit](https://git.hackliberty.org/Git-Mirrors/xmr-btc-swap/commit/a056ae3a47be1f922ec77daa9b060a485725d7bd).
- Electron prebuilt binaries "are built on Ubuntu" and follow Chromium's support — [Electron README](https://github.com/electron/electron/blob/main/README.md); electron-builder publishes to GitHub Releases and generates `latest*.yml` (Ed25519-signed since v27) — [electron-builder auto-update](https://www.electron.build/docs/features/auto-update).
- DoltHub's Tauri port hit "problems creating and codesigning macOS universal binaries" (2025, older) — [DoltHub blog](http://www.dolthub.com/blog/2025-11-13-electron-vs-tauri/).
- Flutter builds per-OS with `flutter build windows|macos|linux`; no cross-compilation — [Flutter desktop docs](https://docs.flutter.dev/platform-integration/desktop).
- Wails v3 requires Go 1.26+ and builds each platform natively; GTK4/WebKitGTK 6.0 default on Linux — [Wails v3.0.0 notes](https://api.github.com/repos/wailsapp/wails/releases/tags/v3.0.0).
- Qt 6.12 needs Xcode 16 / MSVC 2022 toolchains — [Qt for macOS 6.12](https://doc.qt.io/qt-6.12/macos.html), [Qt for Windows 6.12](https://doc.qt.io/qt-6.12/windows.html).

### Inferences
- The Krate repo already runs a three-OS GitHub Actions release pipeline for CMake plugins; a Tauri job is one extra matrix entry per OS plus the Ubuntu apt line and two secrets (`TAURI_SIGNING_PRIVATE_KEY`, password). Build two macOS artifacts (aarch64 + x86_64) rather than a universal binary to avoid the universal-binary issue DoltHub hit.
- Pin the Linux runner to `ubuntu-22.04` for the lowest glibc floor still available on free runners; when GitHub retires it, build in a 22.04 container as the Tabularis team did.
- Qt in CI is heavier for an OSS project: either download prebuilt Qt via a community action (adds minutes and a cache) or build from source; none of the free LTS patch releases are available to open-source users immediately (see Q5).
- Avalonia: `dotnet publish -r <rid> --self-contained` on each runner is trivial, but Velopack's macOS story requires signing.

### Gaps
- No measured CI minutes per OS for a Tauri hello-world vs Electron vs Qt were found; Tauri's Rust compile is slower than an Electron pack but the numbers are app-specific.

## Key question 4: Learning curve and maintenance cost for a C++ team (how much Rust does a Tauri app of this scope need?)

### Takeaway
For this feature list, nearly everything can be done from TypeScript through Tauri's official plugins (http, fs, shell, process, os, upload, updater, dialog, store, log); Rust is needed only for glue (a few `#[tauri::command]` functions, e.g. reading a bundle's version or a registry key), and production reports say the Rust side stays "minimal". Electron is TypeScript-only but you own Chromium's 200+ MB and an 8-week major cadence; Qt is C++ the team already knows but buys the licensing/LTS friction and no updater.

### Cited Findings
- Official Tauri plugins: HTTP client ("using the client written in Rust"), File System, Shell ("runs system commands and spawns child processes"), Process, OS Information, Upload, Updater, Dialog, Opener, Store, Logging, Single Instance, Deep Linking — [Tauri plugin list](https://v2.tauri.app/plugin/). Each has a JS API surface (the list page does not spell out "no Rust needed"; each plugin page documents the JS import).
- "Tauri's JavaScript APIs covered most needs, and the migration used only minimal Rust code" — [DoltHub blog (2025)](http://www.dolthub.com/blog/2025-11-13-electron-vs-tauri/).
- Tabularis author (6 months, 64 releases): IPC "isn't secure by default", the capability model "needs careful ... management", every result page crosses the Rust-webview bridge — [HackerNoon](https://hackernoon.com/six-months-with-tauri-the-benefits-and-the-bill).
- Updater plugin now requires Rust 1.90+ (MSRV bump in 2.13.0) — [updater 2.13.0 notes](https://v2.tauri.app/release/updater/v2.13.0/); Tauri docs recommend `rustup` with `stable-msvc` on Windows — [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/).
- Electron ships a new major every 8 weeks and supports three — [Electron timelines](https://www.electronjs.org/docs/latest/tutorial/electron-timelines).
- Slint offers C++, Rust and Node.js APIs (its release notes give upgrade steps for all three) — [Slint releases](https://api.github.com/repos/slint-ui/slint/releases?per_page=3).
- Qt's LGPL page expects dynamic linking and a relinking mechanism (see Q5) — [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).
- ActivityWatch moved from Python+Qt to Tauri; "perhaps the largest [reason] is easy cross-platform packaging/builds" (HN comment by a maintainer) — [HN thread mirror](https://hn.svelte.dev/item/45512962).

### Inferences
- Concrete Rust surface for Krate Manager (estimate from the plugin list): (a) download → `@tauri-apps/plugin-http` or `plugin-upload` from TS; (b) SHA-256 → Web Crypto in the webview or a 10-line Rust command; (c) unzip → `plugin-fs` cannot unzip, so one Rust command using the `zip` crate (~30 lines) or shell out to `tar`/`Expand-Archive`; (d) copy/delete into plugin folders → `plugin-fs` with scoped capabilities; (e) run an installer / elevated helper → `plugin-shell` with an allow-listed sidecar; (f) read installed bundle version → Rust command parsing `moduleinfo.json` / `Info.plist` / the VST3 `.dll` resource (the team already produces these files); (g) registry read → `winreg` crate in one `#[cfg(windows)]` command. Order of magnitude: 200-400 lines of Rust total, the rest TypeScript. A C++ engineer will be productive in that subset of Rust within days; the capability/permissions JSON is the real learning item.
- Electron removes Rust but adds: 200-290 MB per install, Chromium CVE churn every 8 weeks, a deprecated elevation library, and a macOS updater that is documented as needing a signature — the last one alone fails the "no certificates" constraint.
- Qt/C++ is the smallest *language* leap, but the team would write the updater, download manager and installer plumbing themselves (QtIFW is an installer, not an in-app updater), and must honour LGPL relinking plus the LTS-access rules.
- Avalonia (C#) and Wails (Go) each add a whole new runtime/toolchain to the team; Avalonia's practical updater (Velopack) needs macOS signing; Wails' updater is 3 days old.
- iced/egui/Slint: Rust everywhere (not just glue), fewer ready widgets, and no first-party updater; Slint's C++ API is interesting but still no updater/installer story.

### Gaps
- No source quantifies "lines of Rust" for a Tauri app of this scope; the estimate above is derived from the plugin surfaces, not measured.

## Key question 5: Licensing conflicts with a free, open-source distribution

### Takeaway
Nothing here blocks a free open-source app, but Qt is the only candidate with active obligations (LGPLv3 relinking/source delivery, GPL-only modules) and a commercial-first LTS policy; Slint is fine under GPLv3 or its royalty-free licence with attribution; the rest are MIT/Apache/BSD.

### Cited Findings
- Tauri: repo root carries `LICENSE-APACHE-2.0`, `LICENSE-MIT` and `LICENSE.spdx` (dual) — [GitHub contents API](https://api.github.com/repos/tauri-apps/tauri/contents/); GitHub's license detector reports Apache-2.0 — [GitHub API](https://api.github.com/repos/tauri-apps/tauri).
- Electron: MIT; logo use follows the OpenJS Foundation trademark policy — [Electron README](https://github.com/electron/electron/blob/main/README.md).
- Qt: "most modules are under LGPL v3 and GPL v3"; you must deliver "the complete corresponding source code of the library ... even if the library has not been modified", allow users to "change and re-link the library", give a prominent LGPL notice; "some Qt parts are only provided under GPL"; with static linking "the application itself may no longer be 'work that uses the library'" — [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations). LTS: "immediate access to LTS releases is limited to commercial customers"; "the initial patch releases of such an LTS version are also available to open-source users"; Qt 6.8 LTS currently "commercial access only" (latest 6.8.8, support to 2029-10-08) — [Qt Releases](https://doc.qt.io/qt-6/qt-releases.html); background on the 2020 change — [Phoronix](https://www.phoronix.com/news/Qt-Going-More-Commercial).
- wxWidgets: wxWindows Library Licence 3.1 is "essentially the L-GPL ... with an exception" that lets you "distribute under your own terms, binary object code versions"; OSI-approved — [wxWidgets licence](https://www.wxwidgets.org/about/licence/).
- Flutter: BSD-3-Clause — [GitHub API](https://api.github.com/repos/flutter/flutter).
- Avalonia: MIT — [GitHub API](https://api.github.com/repos/AvaloniaUI/Avalonia).
- iced: MIT — [GitHub API](https://api.github.com/repos/iced-rs/iced). egui: Apache-2.0 per detector (dual MIT/Apache in the project) — [GitHub API](https://api.github.com/repos/emilk/egui). Dioxus: Apache-2.0 per detector (dual MIT/Apache in the project) — [GitHub API](https://api.github.com/repos/DioxusLabs/dioxus). Wails: MIT — [GitHub API](https://api.github.com/repos/wailsapp/wails).
- Slint: `GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0` — [Espressif component licence listing](https://components.espressif.com/components/slint/slint/versions/1.17.0/license?language=en); royalty-free licence requires disclosure (AboutSlint widget/badge) and excludes embedded — [Slint royalty-free licence PDF](https://slint.dev/agreements/slint-royalty-free-license.pdf), [Slint pricing FAQ](https://slint.dev/pricing).
- Velopack is a separate dependency with its own licence; its docs do not state it on the pages fetched (Gap).

### Inferences
- An MIT/GPL Krate Manager can use any of these. The only *operational* licence cost is Qt's: dynamic linking, shipping/offering Qt source, and being locked out of LTS patch releases unless you follow the KDE-agreement timing. For a team that will never buy anything, Qt's "free" is the most expensive free here.
- Slint's GPLv3 is compatible if the manager itself is GPLv3; if Krate wants MIT, use the royalty-free licence and keep the attribution.

### Gaps
- Velopack licence text not fetched. egui/Dioxus dual licensing not confirmed from LICENSE files (only inferred from project convention vs the detector's single value).

## Key question 6: Production apps in the audio/music space on each framework (fitness evidence)

### Takeaway
The audio industry's installer/launcher apps that could be identified are Electron (Native Access 3, Splice); Qt dominates full applications (Audacity 4, LMMS, Qtractor, MuseScore lineage); no shipping audio *plugin manager* built on Tauri, Wails, Avalonia, Flutter, iced/egui/Slint or Dioxus was found — only Tauri music players.

### Cited Findings
- Native Instruments Native Access 3.x "is an Electron app and doesn't run under Wine out of the box"; the legacy Qt-based v1 is the workaround users mention — [NI community: Native Access on Linux](https://community.native-instruments.com/discussion/51744/native-instruments-native-access-available-on-linux), [NI community: Native Access breaking Kontakt thread](https://community.native-instruments.com/discussion/49695/native-access-is-breaking-kontakt-for-many-legitimate-customers/p1). A third-party scanner lists Native Access 3.24.0 on Electron 40.6.0 (verified May 2026) — [Desktop Insights: Native Access](https://desktopinsights.com/apps/native-access). NI published "Dev Talks: Why We Transformed Native Access" (2022) but the page body could not be fetched — [NI community post](https://community.native-instruments.com/discussion/comment/45552).
- Splice desktop is Electron (v38.4.0, updated May 2026, third-party detection) — [Desktop Insights: Splice](https://desktopinsights.com/apps/splice); Electron's own site has listed Splice as a showcase app — [archived electronjs.org](https://webcf.waybackmachine.org/web/20201117235551/http://electronjs.org).
- Audacity 4's UI "now runs in Qt" reusing the MuseScore Studio 4 framework, with the wxWidgets-era engine wrapped via `au3wrap` — [Linux Compatible: Audacity 4.0 beta 4](https://www.linuxcompatible.org/story/audacity-40-beta-4-ships-with-qt6-ui-windows-asio-and-legacy-imports/); Wikipedia says 4.0 was released September 2026 (conflicts with the beta article's timing) — [Wikipedia: Audacity](https://en.wikipedia.org/wiki/Audacity_(audio_editor)). Audacity originally chose wxWidgets "as there were doubts about the license terms of Qt at that time" — same Wikipedia page.
- LMMS and Qtractor are Qt — [Wikipedia: Comparison of free software for audio](https://en.wikipedia.org/wiki/Comparison_of_free_software_for_audio).
- ProtonMail chose Electron for its desktop client because "the overwhelming majority [of competitors] is using either Electron or CEF" (not audio, but the same "safe choice" reasoning Native Access reflects) — [ProtonMail inbox-desktop issue #2](https://github.com/ProtonMail/inbox-desktop/issues/2).
- Tauri in music: Audion (local-library player with "plugins", ~413 stars, Svelte + Tauri) — [GitHub 3zsDev/Audion](https://github.com/3zsDev/Audion); Musicat, Zeri, one-listen, Apollo are Tauri music players — [awesome-tauri listing](https://awesome.ecosyste.ms/lists/tauri-apps%2Fawesome-tauri?topic=nuxt), [kazupot7/one-listen](https://github.com/kazupot7/one-listen), [maciejkrol18/apollo](https://github.com/maciejkrol18/apollo).
- Tauri launcher/installer-class apps outside audio: Tabularis (DB client), Yaak (API client) publish Linux-graphics support pages — [HackerNoon Tabularis](https://hackernoon.com/six-months-with-tauri-the-benefits-and-the-bill), [Yaak docs](https://yaak.app/docs/getting-started/linux-graphics-issues).

### Inferences
- The audio industry's choice of Electron for installers is explained by the frameworks' maturity in 2017-2022 and by those vendors having signing certificates and notarization — the one thing Krate will not have. Their precedent therefore says little about the unsigned case.
- Qt's dominance is in full DAWs/editors by companies with commercial Qt licences or GPL applications (Audacity, LMMS); again not the unsigned, MIT, free-tooling situation here.
- Absence of a Tauri audio-plugin-manager precedent is a mild risk signal, offset by Tauri launcher-style apps in adjacent categories and by the problem being mostly HTTP + filesystem.

### Gaps
- Arturia Software Center, iZotope Product Portal, Waves Central, Steinberg Download Assistant, Spitfire Audio app, Plugin Boutique Manager: no reliable source identified their frameworks; do not quote framework claims for them.
- The NI "Dev Talks" rationale (reported in a search snippet as Electron-over-Tauri) could not be read; treat as unverified.

## Ranked recommendation for this use case (unsigned, free, OSS, C++ team, GitHub Releases)

### Takeaway
1) **Tauri 2** — the only stack whose first-party updater and bundler are documented to work end-to-end with no Apple/Microsoft certificate on all three OSes, with 12-18 MB installers and a Rust surface that stays at glue level; budget for Linux WebKitGTK seams and the capability model. 2) **Qt 6 (C++ Widgets)** — if the team refuses a webview; costs LGPL hygiene, no updater, commercial-first LTS. 3) **Electron** — fails the no-signing constraint on macOS self-update; otherwise easiest. 4) **Wails v3** — promising (first-party updater, GitHub provider, native packaging) but 3 days old and Go. 5) **Avalonia** — mature UI, but Velopack's macOS path needs signing. 6) **Flutter desktop** — renderer just changed to Impeller on desktop, updater ecosystem immature on Linux. 7) **Slint / iced / egui / Dioxus** — fine toolkits, no installer/updater story, Dioxus desktop inherits the webview pain anyway. 8) **wxWidgets** — no updater/installer, no size win over Qt. Baseline **Node.js CLI** should exist regardless (CI, power users, the fallback when a GUI is broken), ideally sharing the same manifest schema as the GUI.

### Cited Findings
- Tauri updater: own key pair, "cannot be disabled", NSIS/MSI/`.app.tar.gz`/AppImage artifacts, `latest.json` on GitHub Releases, tauri-action generates it — [Tauri updater plugin](https://v2.tauri.app/plugin/updater/), [tauri-action](https://github.com/tauri-apps/tauri-action). Windows signing "not required to execute" (SmartScreen / Smart App Control caveats) — [Tauri Windows signing](https://v2.tauri.app/distribute/sign/windows/). macOS ad-hoc `"-"` identity for Apple Silicon — [Tauri macOS signing](https://v2.tauri.app/distribute/sign/macos/).
- Electron: "Your application must be signed for automatic updates on macOS" — [Electron autoUpdater](https://www.electronjs.org/docs/latest/api/auto-updater); electron-builder: macOS "must be signed in order for auto updating to work" — [electron-builder auto-update](https://www.electron.build/docs/features/auto-update).
- Velopack: macOS "codesigning and Notarization is required" — [Velopack signing](https://docs.velopack.io/packaging/signing).
- Wails v3.0.0 released 2026-10-26 with in-app updater and GitHub provider — [Wails v3.0.0 notes](https://api.github.com/repos/wailsapp/wails/releases/tags/v3.0.0).
- Qt LTS "immediate access ... limited to commercial customers"; LGPL relinking/source obligations — [Qt Releases](https://doc.qt.io/qt-6/qt-releases.html), [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).
- Flutter 3.47 Impeller-by-default on desktop (secondary) — [startdebugging.net](https://startdebugging.net/2026/08/flutter-3-47-impeller-default-renderer-on-desktop/); `desktop_updater` Linux/Windows "candidate-only preview" — [pub.dev](https://pub.dev/documentation/desktop_updater/3.2.0/).
- Linux pain and sizes for the Tauri route — [Tauri Linux graphics](https://v2.tauri.app/develop/debug/linux-graphics/), [HackerNoon Tabularis](https://hackernoon.com/six-months-with-tauri-the-benefits-and-the-bill).
- GHSA-j38x-g3m3-95fr fixed in updater 2.12.0 — [OSV](https://osv.dev/vulnerability/GHSA-j38x-g3m3-95fr).

### Inferences (design guard-rails if Tauri is chosen)
- Pin `tauri-plugin-updater` ≥ 2.14.1 (2.12.0 is the security floor), keep `allowDowngrades` at its default `false`, and host `latest.json` only on the GitHub Release the action writes.
- Ship `webviewInstallMode: downloadBootstrapper` by default (0 MB) and offer a separate `offlineInstaller` build (~127 MB) only if users ask; both are first-party switches.
- Default NSIS `installMode: currentUser` (no UAC) and install plugins to per-user folders; make per-machine an explicit opt-in that runs one elevated helper. This matches the repo's existing `%LOCALAPPDATA%\Programs\Common\VST3` usage and removes elevation from the normal path on every OS.
- Build on `ubuntu-22.04` (glibc 2.35, WebKitGTK 4.1 present); ship `.deb` + `.rpm` + AppImage, but only AppImage self-updates — tell deb/rpm users to use the package, or make the manager re-download the package and hand it to `pkexec apt/dnf`. Gate the `WEBKIT_DISABLE_DMABUF_RENDERER` workaround behind a detected-NVIDIA check or a `--safe-gl` flag rather than unconditionally.
- Generate the per-plugin manifest (version, SHA-256, URLs per OS/arch) in the existing release pipeline; the manager and a `node tools/krate-cli.js` baseline consume the same file.
- Document for all users: unsigned software triggers SmartScreen (Windows), Gatekeeper "not verified"/"damaged" (macOS; right-click Open or Privacy & Security), and Smart App Control (Windows 11) blocks it entirely. No framework changes this.

### Gaps
- Unverified assumption to test first: the Tauri updater's in-place `.app` replacement on an ad-hoc-signed bundle under macOS 15/26 (30-minute experiment on a Mac). If it fails, the fallback is "download DMG and open it" for macOS self-update, which still beats Electron's documented hard requirement.
- Wails v3 deserves a re-check in 3-6 months once production reports exist; if its updater is proven unsigned-friendly, it is the closest alternative to Tauri with no Rust.
