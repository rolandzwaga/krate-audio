# Tauri 2 Info.plist / quarantine, updater install mechanics, and GitHub macOS runners

Researched 2026-10-29 from primary sources (GitHub source at pinned commits, docs.github.com, github.blog changelog,
actions/runner-images, Apple developer docs). Pinned commits:

- `tauri-apps/tauri` branch `dev` = `548a3692f59405aeb239c5b63fa589824eee51e8` (committed 2026-10-28T12:15Z)
- `tauri-apps/plugins-workspace` branch `v2` = `74a44c1d5694324b7fb9ba5ee9271a3096da9387` (committed 2026-10-28T10:54Z);
  `plugins/updater/Cargo.toml` at that commit says `version = "2.14.1"`
- `actions/runner-images` branch `main` = `239841a2fb2b4ca715e3514dc343fdbd57c7190c` (2026-10-29T12:36Z)

## Short answers

**Q1. Does the Tauri 2 bundler write `LSFileQuarantineEnabled`?** No. `create_info_plist` in
`crates/tauri-bundler/src/bundle/macos/app.rs` builds the plist in code (no template file) and writes only:
`CFBundleDevelopmentRegion`, `CFBundleDisplayName`, `CFBundleExecutable`, `CFBundleIdentifier`,
`CFBundleInfoDictionaryVersion`, `CFBundleName`, `CFBundlePackageType`, `CFBundleShortVersionString`,
`CFBundleVersion`, `CSResourcesFileMapped`, `LSApplicationCategoryType` (if a category is set),
`LSMinimumSystemVersion` (if set), the file-association keys (`CFBundleDocumentTypes` / `UTExportedTypeDeclarations`
via `tauri_utils::config::file_associations_plist`, if any), `CFBundleIconFile`, `CFBundleIconName` (Assets.car),
`CFBundleURLTypes` (deep links), `LSRequiresCarbon`, `NSHighResolutionCapable`, `NSHumanReadableCopyright` (if set),
`NSAppTransportSecurity` (if `exceptionDomain` is set), and then every key of the developer's merged plist
(`src-tauri/Info.plist` and/or `bundle.macOS.infoPlist`), which overrides same-named generated keys. The string
`LSFileQuarantineEnabled` does not occur anywhere in `tauri-apps/tauri` or `tauri-apps/plugins-workspace` (GitHub code
search; the only org hit is `tauri-apps/cef-rs`, an unrelated CEF build helper). Consequence: a Tauri app is not a
quarantine-opting-in downloader; files it writes via reqwest + `std::fs` carry **no** `com.apple.quarantine` xattr
unless the developer adds `<key>LSFileQuarantineEnabled</key><true/>` to `src-tauri/Info.plist` (then every file the app
creates is quarantined by default). Adding or overriding keys is supported and documented: drop an `Info.plist` next to
`tauri.conf.json` and/or point `bundle.macOS.infoPlist` at a file; the CLI merges both (later wins) and the bundler
inserts the result last. `bundle.macOS.files` is a different mechanism: it copies extra files into
`<name>.app/Contents/...`, it does not touch Info.plist.

**Q2. Does the updater plugin add or remove quarantine, and how does it install the `.app.tar.gz`?** It neither adds
nor removes any xattr; no `xattr`, `quarantine`, `set_unpack_xattrs` or `spctl` call exists in `updater.rs`. The
downloaded bytes stay in memory (`install(bytes)`), are gunzipped and un-tarred with the `tar` crate (xattr restore is
off by default and never enabled, and the bundler's `tar::Builder` writes no xattrs), the archive's first path
component (`<App>.app/`) is stripped, and the files land in a `tempfile` dir prefixed `tauri_updated_app` created on the
**same volume** as the installed bundle (`std::env::temp_dir()` if it is on that volume, else the bundle's parent
dir). That temp dir is chmod 0755 and becomes the new bundle: `swap_bundle` calls `renamex_np(staged, target,
RENAME_SWAP)` (atomic APFS swap, so the old bundle ends up in the temp dir and is deleted on drop); on `ENOTSUP`-class
errors it falls back to `replace_bundle` = `rename(target, backup)` then `rename(staged, target)` with restore on
failure; on `PermissionDenied` it reruns the same swap/move as a `do shell script ... with administrator privileges`
AppleScript (via `osakit`, JXA calling `renamex_np`, `mv -f` fallback). Afterwards it runs `touch <app>` to refresh
LaunchServices. The install path comes from `extract_path_from_executable`: the directory two levels above
`Contents/MacOS`. Because the new bundle is written by the running app and never passes through a quarantine-enabled
downloader, the swapped-in `.app` carries no quarantine xattr either (consistent with Q1), so Gatekeeper does not
re-assess it on next launch.

**Q3. GitHub-hosted macOS runners, October 2026.** Labels in the docs/README today: arm64 standard `macos-latest`,
`macos-14` (deprecated, retires 2 Nov 2026, in brownouts now), `macos-15`, `macos-26`, `xcode-27` (preview); Intel
standard `macos-15-intel`, `macos-26-intel`; large (Intel, 12 vCPU) `macos-latest-large`, `macos-14-large`,
`macos-15-large`, `macos-26-large`; xlarge (arm64 M2, 5 vCPU + GPU) `macos-latest-xlarge`, `macos-14-xlarge`,
`macos-15-xlarge`, `macos-26-xlarge`, `xcode-27-xlarge`. `macos-13*` was retired 4 Dec 2025 and is gone from the tables.
macOS 26 Tahoe runners exist and are GA since 26 Feb 2026; the current `macos-26` arm64 image is macOS 26.6.2 (25G83),
image 20261013, default Xcode 26.6, CLT present. `macos-latest` = `macos-26` (arm64) since the migration completed on
22 Jul 2026; `macos-latest-large` = macOS 26 Intel. Intel macOS runners end after the macOS 15 image retires in fall
2027. Headless capability: the docs state "The Linux and macOS virtual machines both run using passwordless `sudo`", so
`sudo installer -pkg ... -target /` works (runner-images' own image-build scripts do exactly that, plus
`sudo xattr -rd com.apple.quarantine` and `spctl --assess --raw`, all non-interactively). `xattr`, `spctl` and
`codesign` are base-OS / CLT binaries present on every image; ad-hoc `codesign -s -` needs no identity or keychain,
so nothing in it requires a GUI session. I found no official GitHub statement about `spctl`/`codesign` specifically;
see Gaps.

## Evidence

### Q1 — tauri-bundler `create_info_plist`

Permalink (dev @ 548a369): https://github.com/tauri-apps/tauri/blob/548a3692f59405aeb239c5b63fa589824eee51e8/crates/tauri-bundler/src/bundle/macos/app.rs#L211-L367

Line map at that commit (verified with `grep -n` on the raw file):

| Lines | Key(s) written |
|---|---|
| 218 | `let mut plist = plist::Dictionary::new();` (built in code, no template) |
| 219 | `CFBundleDevelopmentRegion` = "English" |
| 220 | `CFBundleDisplayName` = product name |
| 221-224 | `CFBundleExecutable` = main binary name |
| 225-228 | `CFBundleIdentifier` |
| 229 | `CFBundleInfoDictionaryVersion` = "6.0" |
| 230-238 | `CFBundleName` (= `macOS.bundleName` or product name) |
| 239 | `CFBundlePackageType` = "APPL" |
| 240-243 | `CFBundleShortVersionString` |
| 244-252 | `CFBundleVersion` (= `macOS.bundleVersion` or version) |
| 253 | `CSResourcesFileMapped` = true |
| 254-259 | `LSApplicationCategoryType` (if `category`) |
| 260-262 | `LSMinimumSystemVersion` (if set) |
| 264-273 | file-association keys merged from `tauri_utils::config::file_associations_plist` |
| 276-285 | `CFBundleIconFile` |
| 288-295 | `CFBundleIconName` (from Assets.car) |
| 297-332 | `CFBundleURLTypes` (`CFBundleURLSchemes`, `CFBundleURLName`, `CFBundleTypeRole`) for deep links |
| 334 | `LSRequiresCarbon` = true |
| 335 | `NSHighResolutionCapable` = true |
| 336-338 | `NSHumanReadableCopyright` (if set) |
| 340-350 | `NSAppTransportSecurity` / `NSExceptionDomains` (if `exceptionDomain`) |
| 352-362 | **user plist merge**: `settings.macos().info_plist` (`PlistKind::Path` or `::Plist`) iterated and `plist.insert(key, value)` — overrides any generated key |
| 364 | `plist::Value::Dictionary(plist).to_file_xml(bundle_dir.join("Info.plist"))` |

`LSFileQuarantineEnabled` is absent. There is no Info.plist template file in the bundler (GitHub code search
`Info.plist path:crates/tauri-bundler` returns only `app.rs`, `ios.rs`, `icon.rs`, `settings.rs` and CHANGELOG lines;
the CHANGELOG records the move from `PlistBuddy` to the `plist` crate, PR #4412, 2022-06-21).

Code-search results (GitHub `gh search code`, 2026-10-29):
- `LSFileQuarantineEnabled` in `tauri-apps/tauri`: 0 hits. In `tauri-apps/plugins-workspace`: 0 hits.
  Org-wide: 1 hit, `tauri-apps/cef-rs:cef/src/build_util/mac.rs` (CEF helper, not the bundler).
- `quarantine` in `tauri-apps/tauri`: 1 hit, a doc comment in `crates/tauri-utils/src/config.rs` L3508 about
  App Translocation of downloaded bundles. In `plugins-workspace`: 0 hits.

Where the user plist comes from (tauri-cli, dev @ 548a369):
- `crates/tauri-cli/src/interface/rust.rs` L1626-1643: on macOS the CLI collects `tauri_dir.join("Info.plist")` if it
  exists and then `config.macos.info_plist`, and passes `PlistKind::Plist(merge_plist(src_plists)?)` to the bundler:
  https://github.com/tauri-apps/tauri/blob/548a3692f59405aeb239c5b63fa589824eee51e8/crates/tauri-cli/src/interface/rust.rs#L1626-L1643
- `crates/tauri-cli/src/helpers/plist.rs` L25-42 `merge_plist`: a flat dictionary insert in order, so the later
  source (the `infoPlist` config path) wins over `src-tauri/Info.plist`, and both win over the generated keys:
  https://github.com/tauri-apps/tauri/blob/548a3692f59405aeb239c5b63fa589824eee51e8/crates/tauri-cli/src/helpers/plist.rs#L25-L42
- `crates/tauri-utils/src/config.rs` L665-669 (`MacConfig::info_plist`): "Path to a Info.plist file to merge with the
  default Info.plist. Note that Tauri also looks for a `Info.plist` file in the same directory as the Tauri
  configuration file." L621-623 (`MacConfig::files`): "The files to include in the application relative to the
  Contents directory."
  https://github.com/tauri-apps/tauri/blob/548a3692f59405aeb239c5b63fa589824eee51e8/crates/tauri-utils/src/config.rs#L621-L669
- `crates/tauri-bundler/src/bundle/settings.rs` L369-370, L384-390 (`MacOsSettings::info_plist: Option<PlistKind>`,
  `enum PlistKind { Path, Plist }`).

Docs (v2.tauri.app):
- https://v2.tauri.app/distribute/macos-application-bundle/ — "create an `Info.plist` file in the `src-tauri` folder
  and include the key-pairs you desire"; warns "Be careful when overwriting default values such as application
  version"; `bundle > macOS > files` "maps the destination path to its source relative to the `tauri.conf.json`
  file" and "The files are added to the `<product-name>.app/Contents` folder" (examples
  `embedded.provisionprofile`, `SharedSupport/docs.md`).
- https://v2.tauri.app/reference/config/#infoplist — `infoPlist: string | null`, same text as config.rs;
  `files: object` default `{}`; `minimumSystemVersion` defaults to `10.13`, `null` removes `LSMinimumSystemVersion`.

Apple semantics of the key (https://developer.apple.com/documentation/bundleresources/information-property-list/lsfilequarantineenabled):
"A Boolean value indicating whether the files this app creates are quarantined by default." Boolean, macOS 10.0+.
(The JSON endpoint carries no further prose; the related `LSFileQuarantineExcludedPathPatterns` key is not listed
there.)

### Q2 — updater plugin macOS install path

Permalink (v2 @ 74a44c1): https://github.com/tauri-apps/plugins-workspace/blob/74a44c1d5694324b7fb9ba5ee9271a3096da9387/plugins/updater/src/updater.rs#L1478-L1633

Key lines at that commit:
- L921 `pub fn install(&self, bytes: impl AsRef<[u8]>)` → L1486 `#[cfg(target_os = "macos")] fn install_inner(&self, bytes: &[u8])`.
  Doc comment L1480-1485: "Expected structure: `[AppName]_[version]_x64.app.tar.gz` ... `[AppName].app` ... `Contents`".
- L1498-1506: chooses `tmp_root` = `std::env::temp_dir()` or `extract_path.parent()`, whichever has the same `dev()` as
  the installed bundle, else `Error::TempDirNotOnSameMountPoint`.
- L1508-1514: `tempfile::Builder::new().prefix("tauri_current_app").tempdir_in(&tmp_root)` and
  `.prefix("tauri_updated_app").tempdir_in(&tmp_root)`.
- L1516-1536: `GzDecoder` + `tar::Archive`; per entry `entry.path()?.iter().skip(1).collect()` (strips the `.app`
  root), `create_dir_all(parent)`, `entry.unpack(&extraction_path)`; on error the extract dir is removed.
  No `set_unpack_xattrs`/`set_preserve_permissions` call (grep: 0 hits). tar crate docs: `set_unpack_xattrs` "is
  disabled by default" (https://docs.rs/tar/latest/tar/struct.Archive.html).
- L1541-1544: `set_permissions(tmp_extract_dir, 0o755)` because tempfile creates it 0700.
- L1553-1559: `swap_bundle(&self.extract_path, tmp_extract_dir.path())`, falling back to
  `replace_bundle(target, staged, &backup)` on any error other than `PermissionDenied`.
- L1638-1651 `swap_bundle`: `libc::renamex_np(from, to, libc::RENAME_SWAP)`.
- L1658-1666 `replace_bundle`: `rename(target, backup)?; rename(staged, target)` with rollback.
- L1573-1623 privileged path: `"do shell script {} with administrator privileges"` executed with `osakit` on the
  main thread; L1684-1693 `privileged_install_command` runs `osascript -l JavaScript` (JXA `$.renamex_np(..., 2)`),
  then `rm -rf staged`, or `mv -f target backup && mv -f staged target` fallback.
- L1626-1628: `Command::new("touch").arg(&self.extract_path).status()`.
- L1712-1745 `keep_backup_if_not_restored`: on failure the previous app is kept as `<name> (previous version).app`.
- L1793-1820 `extract_path_from_executable`: on macOS, if the exe dir contains `Contents/MacOS`, returns
  `parent().parent()` (= the `.app`).

Bundler side of the archive (dev @ 548a369), `crates/tauri-bundler/src/bundle/updater_bundle.rs` L50-72 and
L244-249: `bundle_update_macos` tars the `.app` with `tar::Builder` (`follow_symlinks(false)`,
`append_dir_all(<App>.app, src_dir)`) into `<App>.app.tar.gz`. `tar::Builder` has no xattr writer, so the archive
cannot carry `com.apple.quarantine` even if the build machine's bundle had it.

Words `xattr`, `quarantine`, `spctl`, `codesign` in `updater.rs`: 0 hits (local grep of the pinned file).

### Q3 — GitHub-hosted macOS runners (state on 2026-10-29)

docs.github.com, "Supported runners and hardware resources"
(https://docs.github.com/en/actions/reference/runners/github-hosted-runners):
- Standard macOS table (public and private repos identical): Intel, 4 CPU / 14 GB RAM / 14 GB: `macos-15-intel`,
  `macos-26-intel`. arm64 (M1), 3 CPU / 7 GB / 14 GB: `macos-latest`, `macos-14`, `macos-15`, `macos-26`, `xcode-27`.
  The `macos-latest` link targets the `macos-26-arm64` README. No `macos-13` row.
- "The `-latest` runner images are the latest stable images that GitHub provides".
- Administrative privileges: "The Linux and macOS virtual machines both run using passwordless `sudo`." / "Windows
  virtual machines are configured to run as administrators with User Account Control (UAC) disabled."

docs.github.com, "Larger runners" (https://docs.github.com/en/actions/reference/runners/larger-runners):
- Large, Intel, 12 CPU / 30 GB: `macos-latest-large`, `macos-14-large`, `macos-15-large`, `macos-26-large`
  (Intel runners get a static UDID).
- XLarge, arm64 (M2), 5 CPU (+8 GPU) / 14 GB: `macos-latest-xlarge`, `macos-14-xlarge`, `macos-15-xlarge`,
  `macos-26-xlarge`, `xcode-27-xlarge`. No static IPs for macOS larger runners.

actions/runner-images README (main @ 239841a, https://github.com/actions/runner-images#available-images):
- macOS 26 x64: `macos-latest-large`, `macos-26-intel`, `macos-26-large`
- macOS 26 arm64: `macos-latest`, `macos-26`, `macos-26-xlarge`
- macOS 15 x64: `macos-15-large`, `macos-15-intel`; macOS 15 arm64: `macos-15`, `macos-15-xlarge`
- macOS 14 x64 (deprecated): `macos-14-large`; macOS 14 arm64 (deprecated): `macos-14`, `macos-14-xlarge`
- "The `-xlarge` and `-large` suffixes are unique to macOS images and are only available for GitHub Actions."
  `-latest` migrations "happen over 1-2 months".
- macOS 26 arm64 image README (https://github.com/actions/runner-images/blob/main/images/macos/macos-26-arm64-Readme.md):
  "OS Version: macOS 26.6.2 (25G83)", "Kernel Version: Darwin 25.6.0", "Image Version: 20261013.0424.1",
  Xcode 26.0.1–26.6 installed, default Xcode 26.6, "Xcode Command Line Tools 27.0.0.0.1788430756".

Announcements:
- macOS 26 public beta: actions/runner-images#13008 (target 2025-09-11, labels `macos-26`, `macos-26-xlarge`);
  Intel beta #13637 (`macos-26-large`); GA #13739 and github.blog changelog 2026-02-26
  (https://github.blog/changelog/2026-02-26-macos-26-is-now-generally-available-for-github-hosted-runners/):
  labels `macos-26`, `macos-26-intel`, `macos-26-large`, `macos-26-xlarge`; "Previously in public preview".
- `macos-latest` → macOS 26: actions/runner-images#14167 (opened 2026-05-27; rollout from 2026-06-15, planned
  completion 2026-07-15; comment 2026-07-22: "the migration to macos-latest is complete, and the label now uses
  macOS 26 images"; issue closed same day). Also github.blog changelog 2026-05-14 "GitHub Actions: Upcoming image
  migrations" (https://github.blog/changelog/2026-05-14-github-actions-upcoming-image-migrations/). Default Xcode moved
  16.4 → 26.4.1 and Node 22 → 24 with that migration.
- macOS 14 retirement: actions/runner-images#13518 (opened 2026-01-11) and github.blog changelog 2026-10-01
  (https://github.blog/changelog/2026-10-01-github-actions-macos-14-runner-image-retirement): deprecation from
  2026-07-06, retired 2026-11-02, brownouts Oct 5/12/16/19/23/26/29/30 (14:00–24:00 UTC); affected `macos-14`,
  `macos-14-large`, `macos-14-xlarge`; replacements `macos-latest` (`macos-26`), `macos-15`, `macos-latest-xlarge`
  (`macos-26-xlarge`), `macos-15-xlarge`.
- macOS 13 retirement: github.blog changelog 2025-09-19
  (https://github.blog/changelog/2025-09-19-github-actions-macos-13-runner-image-is-closing-down/): retired by
  2025-12-04, brownouts Nov 4/11/18/25 2025; affected `macos-13`, `macos-13-large`, `macos-13-xlarge`; "Apple has
  discontinued support for the x86_64 (Intel) architecture" and GitHub stops x86_64 macOS after the macOS 15 image
  retires in fall 2027; Intel fallback labels `macos-15-intel`, `macos-15-large`.

Headless command evidence (runner-images' own macOS image-build scripts, run non-interactively by Packer, main @ 239841a):
- `images/macos/scripts/build/install-powershell.sh` L26 `sudo xattr -rd com.apple.quarantine $pkg_path`, L28
  `sudo installer -pkg $pkg_path -target /`:
  https://github.com/actions/runner-images/blob/239841a2fb2b4ca715e3514dc343fdbd57c7190c/images/macos/scripts/build/install-powershell.sh#L26-L28
- `images/macos/scripts/build/install-aws-tools.sh` L11 `sudo installer -pkg "$awscliv2_pkg_path" -target /`.
- `images/macos/scripts/helpers/Xcode.Installer.psm1` L86 `Invoke-ValidateCommand "spctl --assess --raw $XcodeRootPath"`:
  https://github.com/actions/runner-images/blob/239841a2fb2b4ca715e3514dc343fdbd57c7190c/images/macos/scripts/helpers/Xcode.Installer.psm1#L86
- `codesign` in runner-images: no macOS hits (only Windows Authenticode helpers). Ad-hoc signing (`-s -`) uses no
  certificate or keychain by definition (`man codesign`: identity `-` = ad-hoc), so it has no GUI/keychain-unlock
  dependency; `codesign` ships with the Command Line Tools listed in the image README.

## Gaps

1. No official GitHub statement that `spctl --assess` or `codesign -s -` work in a job step; the evidence is
   runner-images' own image-build scripts (same VM image, headless) plus the base-OS nature of the tools. A 30-second
   probe job on `macos-26` (`spctl --assess -vv`, `codesign -s - --force <bin>`, `xattr -p com.apple.quarantine`)
   would close this; I did not find a public workflow via GitHub code search (queries with bare `-` tokens are
   rejected by the search API).
2. Apple's current `LSFileQuarantineEnabled` page carries only the one-sentence abstract; the behaviour "the xattr is
   applied to files an app writes only if it opts in via this key (or is a CoreTypes-listed downloader)" is Apple
   folklore from the legacy Information Property List Key Reference, not re-verified on macOS 26. Whether Tahoe applies
   quarantine to files written by *any* non-opted-in app (e.g. via a sandbox or Launch Services change) was not tested;
   verify empirically on a macOS 26 runner with a reqwest download from a plain binary.
3. The `tar` crate `Builder` writing no xattrs is from reading the API surface (no xattr setter on `Builder`), not from
   a source audit of the crate; the practical result (updater writes no quarantine) also follows from Q1 regardless.
4. Runner tables were read on 2026-10-29; `macos-14*` disappears on 2026-11-02 and the README table lags label
   changes by days (issue #14167 comments noted the README still showed macos-15 a week after the switch).
5. `gh search code` is an index, not a tree grep; the 0-hit results for `LSFileQuarantineEnabled` are corroborated by
   a local grep of the five downloaded files (`app.rs`, `settings.rs`, `config.rs`, `rust.rs`, `updater.rs`) but not
   of the whole tauri tree.
