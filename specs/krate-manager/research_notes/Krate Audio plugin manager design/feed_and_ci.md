# Release feed, artifact layout and GitHub Actions pipeline for a Krate Audio plugin manager (state as of 2026-10-29)

Scope note: nine free, unsigned VST3/AU plugins in one GitHub repo; a planned Tauri 2 desktop manager; no accounts. Facts carry a source link; recommendations are kept in the "Inferences" blocks. Repo facts cite the file in the monorepo they were read from.

## Baseline: what the pipeline and the in-plugin checker do today

### Takeaway
`release.yml` is a manual, one-plugin-per-run workflow that tags `<plugin>/vX.Y.Z`, uploads three assets (Inno `.exe`, unsigned `.pkg`, `.tar.gz`) and regenerates `versions.json` from local git tags; the plugins' parser ignores `schema_version` entirely and only requires `plugins.<Name>.version`, so additive v2 fields are safe by construction.

### Cited Findings
- Trigger is `workflow_dispatch` with a `plugin` choice input and a `prerelease` boolean; tag format is `{plugin}/v{version}` (e.g. `iterum/v0.8.0`) — `F:\projects\iterum\.github\workflows\release.yml` (header comment and `version` job).
- Assets uploaded per release via `softprops/action-gh-release@v2`: `<Name>-<ver>-Windows-x64.exe`, `<Name>-<ver>-macOS.pkg`, `<Name>-<ver>-Linux-x64.tar.gz` — `release.yml`, `release` job `files:` list.
- The Windows installer input is already a folder bundle: the CI artifact is renamed to `<Name>.vst3` (a directory) and the factory presets are copied beside it — `release.yml`, `windows-installer` job, "Prepare Installer Files".
- macOS staging places `<Name>.vst3` under `Library/Audio/Plug-Ins/VST3`, `<Name>.component` under `Library/Audio/Plug-Ins/Components`, optional `<Name> AUv3.app` under `Applications`, and presets under `Library/Application Support/Krate Audio/<Name>[/<preset_subdir>]`; `pkgbuild --root staging --identifier <id from version.json or com.krateaudio.<name>> --version $VERSION --install-location /` — `release.yml`, `macos-installer` job.
- The Linux archive already is the "plain bundle + presets" format: `staging/<Name>.vst3`, `staging/presets/`, `README.txt`, `CHANGELOG.txt`, then `tar -czvf` — `release.yml`, `linux-archive` job.
- Release notes are composed from the `## [VERSION]` section of `plugins/<plugin>/CHANGELOG.md` plus a path-filtered `git log` since the previous `<plugin>/v*` tag — `release.yml`, "Generate Release Notes".
- `versions.json` is generated inside the `deploy-pages` job with inline Node: for every `plugins/*/version.json`, take the latest `<name>/v*` git tag, emit `{ version, download_url: data.url, release_notes: "" }`, wrap in `{ schema_version: 1, plugins }`, write `_site/versions.json`; deploy with `peaceiris/actions-gh-pages@v4` and `keep_files: true` — `release.yml`, `deploy-pages` job.
- `download_url` in the live feed is the plugin homepage from `version.json` (`data.url`), not an asset URL; `release_notes` is always the empty string — same step.
- The plugin-side parser (`UpdateChecker::parseResponse`) requires `plugins` to be an object, `plugins[<pluginName>]` to be an object and `version` to be a string; it reads `download_url` and `release_notes` only if present and strings; it never reads `schema_version`; any parse exception yields an empty result (no update) — `F:\projects\iterum\plugins\shared\src\update\update_checker.cpp` lines 221-262.
- All nine plugins hard-code the endpoint `https://rolandzwaga.github.io/krate-audio/versions.json` — e.g. `F:\projects\iterum\plugins\disrumpo\src\update\disrumpo_update_config.h` line 12 (same in gradus, innexus, ...).

### Inferences
- Because the shipped parser ignores unknown keys and `schema_version`, a single `versions.json` can be bumped to `schema_version: 2` with new keys added beside `version` / `download_url` / `release_notes` without breaking any plugin in the field. The only breaking changes would be removing or retyping those three keys or nesting them.
- The Linux tarball is already the "manager-friendly" layout; the Windows and macOS pipelines already stage exactly the files a zip would need, so adding zip artifacts is a few extra steps in existing jobs, not a new build.
- The feed generator's "latest tag from local checkout" approach is only correct on the release runner because the job uses `fetch-depth: 0`; a regeneration job triggered outside a release must use the Releases list instead (see CI section).

### Gaps
- None for the baseline; everything above was read from the repo.

## Feed schema design: which public schemas to borrow from, and a concrete v2

### Takeaway
No public schema covers "many products, many platforms, plain-archive installs" in one file; the best fit is Tauri's `latest.json` vocabulary (`platforms.{os}-{arch}.{url,signature}`, `version`, `notes`, `pub_date`) extended with Sparkle's `length`/`minimumSystemVersion`/`releaseNotesLink`/`channel` and Scoop/winget's `sha256`, nested under the existing `plugins.<Name>` entries so the v1 parser keeps working.

### Cited Findings
- Tauri v2 static updater JSON: required keys are `version`, `platforms.[target].url` and `platforms.[target].signature`; optional `notes` and `pub_date`; platform key is `OS-ARCH` with OS in `linux|darwin|windows` and ARCH in `x86_64|aarch64|i686|armv7`; `version` must be valid SemVer with or without a leading `v`; `pub_date` must be RFC 3339; the `signature` value is the content of the generated `.sig` file ("A path or URL does not work"); Tauri validates the entire file before checking `version`, so every platform entry must be complete — [Tauri v2 Updater plugin](https://v2.tauri.app/plugin/updater/).
- Tauri dynamic server: respond `204 No Content` for "no update", or `200 OK` with JSON `{url, version, signature, notes?, pub_date?}`; endpoint URLs may contain `{{current_version}}`, `{{target}}` (`linux|windows|darwin`) and `{{arch}}` (`x86_64|i686|aarch64|armv7`); custom variables are not supported — [Tauri v2 Updater plugin](https://v2.tauri.app/plugin/updater/).
- Sparkle appcast enclosure attributes: `url`, `length` (bytes), `type`, `sparkle:edSignature` (EdDSA); item-level `sparkle:version`, `sparkle:shortVersionString`, `sparkle:minimumSystemVersion` (three-part, e.g. `12.0.0`), `sparkle:maximumSystemVersion`, `sparkle:hardwareRequirements` (`arm64`, Sparkle 2.9), `sparkle:channel` (items without it are on the default channel), `sparkle:phasedRolloutInterval`, `sparkle:criticalUpdate`, `sparkle:informationalUpdate`; release notes either `<sparkle:releaseNotesLink>` (URL, optionally signed) or embedded `<description>` CDATA with `sparkle:format="plain-text"` (2.4+) or `"markdown"` (2.9+, macOS 12+), plus optional `<sparkle:fullReleaseNotesLink>`; `sign_update <archive>` prints the `sparkle:edSignature` and `length` fragment; `generate_appcast` builds the feed and signatures — [Sparkle: Publishing an update](https://sparkle-project.org/documentation/publishing/).
- Scoop manifest: `version`, `url` (string or array; `#/` fragment renames the download), `hash` (SHA256 default; `sha512:`, `sha1:`, `md5:` prefixes), `architecture` wrapper for `32bit|64bit|arm64` overriding `url/hash/bin/installer/uninstaller/...`, `extract_dir`, `extract_to`, `pre_install`/`post_install`/`pre_uninstall`/`post_uninstall`, `installer`/`uninstaller` (`file`, `script`, `args`, `keep`), `persist`, `checkver`, `autoupdate`, `depends`, `notes` — [Scoop wiki: App Manifests](https://github.com/ScoopInstaller/Scoop/wiki/App-Manifests).
- winget installer manifest: `InstallerSha256` is required; `Architecture` enum is `x86|x64|arm|arm64|neutral`; `MinimumOSVersion` is "the minimum version of the Windows operating system supported by the package" (examples `10.0.18362.0`); current manifests target schema 1.10/1.12 and Microsoft says to use the latest schema — [Microsoft Learn: manifest](https://learn.microsoft.com/fi-fi/windows/package-manager/package/manifest); [winget-types Installer docs](https://docs.rs/winget-types/0.5.0/winget_types/installer/struct.Installer.html); [JReleaser winget reference](https://jreleaser.org/guide/latest/reference/packagers/winget.html).
- electron-updater `latest.yml`: `version`, `files: [{url, sha512 (base64), size}]`, `path`, `sha512`, `releaseDate`; platform-specific file names `latest.yml` / `latest-mac.yml` / `latest-linux.yml` — [electron-updater-yaml README](https://cdn.jsdelivr.net/npm/electron-updater-yaml@1.1.2/README.md); [shinyelectron auto-updates vignette](https://cran.nics.utk.edu/cran/web/packages/shinyelectron/vignettes/auto-updates.qmd).
- Velopack: feed is `releases.{channel}.json` listing every release, `assets.{channel}.json` for the latest build, a legacy `RELEASES` file only for Squirrel migrations; packages must live in the same folder as the feed — [Velopack distributing overview](https://docs.velopack.io/distributing/overview); [Velopack packaging overview](https://docs.velopack.io/packaging/overview).
- Modrinth: version files carry a `hashes` map; the hash lookup endpoint accepts `sha1` or `sha512`; the `.mrpack` format says a file entry MUST contain both the SHA1 and SHA512 hash — [Modrinth API: version from hash](https://docs.modrinth.com/api/operations/versionfromhash); [Modrinth modpack format](https://docs.modrinth.com/modpacks/format).
- Homebrew cask stanzas include `version`, `url`, `sha256`, `depends_on` (e.g. `depends_on macos: ">= :ventura"`); formulae.brew.sh publishes `/api/cask/<token>.json` — [Homebrew Cask Cookbook](https://docs.brew.sh/Cask-Cookbook); [formulae.brew.sh cask page](https://formulae.brew.sh/cask/brewy).
- Tauri's updater public key is a base64 minisign public key (secondary source; the official page does not name the algorithm) — [DeepWiki: clash-nyanpasu update system](https://deepwiki.com/libnyanpasu/clash-nyanpasu/8.2-update-system); Tauri's own page only says `tauri signer generate -w ~/.tauri/myapp.key`, `pubkey` goes into `tauri.conf.json`, `TAURI_SIGNING_PRIVATE_KEY` / `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` at build time, `.env` files do not work, and "signing cannot be disabled" — [Tauri v2 Updater plugin](https://v2.tauri.app/plugin/updater/).
- A 2026 CVE summary states the Tauri updater's minisign signatures cover only the raw binary bytes (summary only; details not read) — [Ubuntu CVE-2026-95625](https://ubuntu.com/security/CVE-2026-95625).

### Inferences
- Recommended v2 feed (additive, one file, keeps the three v1 keys at the same place and type):

```jsonc
{
  "schema_version": 2,
  "generated_at": "2026-10-29T12:00:00Z",           // RFC 3339
  "plugins": {
    "Iterum": {
      "version": "1.2.0",                            // v1 key: unchanged semantics
      "download_url": "https://krateaudio.com/iterum/", // v1 key: landing page, unchanged
      "release_notes": "",                           // v1 key: keep "" (plugins show it inline)
      "tag": "iterum/v1.2.0",
      "pub_date": "2026-10-20T09:12:00Z",
      "channel": "stable",                           // Sparkle/Velopack idea; prereleases go to "beta"
      "release_notes_url": "https://github.com/<owner>/<repo>/releases/tag/<url-encoded tag>",
      "notes_markdown": "## 1.2.0\n- ...",           // optional inline copy of the CHANGELOG section
      "platforms": {
        "windows-x86_64": {
          "url": "https://github.com/<owner>/<repo>/releases/download/<tag>/Iterum-1.2.0-Windows-x64.zip",
          "size": 12345678, "sha256": "<hex>", "signature": "<minisign .sig text>",
          "format": "zip", "min_os": "10.0.17763",
          "installer": { "url": ".../Iterum-1.2.0-Windows-x64.exe", "size": 0, "sha256": "...", "signature": "...", "format": "inno" }
        },
        "darwin-universal": {
          "url": ".../Iterum-1.2.0-macOS.zip", "size": 0, "sha256": "...", "signature": "...",
          "format": "zip", "min_os": "10.13",
          "installer": { "url": ".../Iterum-1.2.0-macOS.pkg", "format": "pkg", "sha256": "...", "signature": "..." }
        },
        "linux-x86_64": {
          "url": ".../Iterum-1.2.0-Linux-x64.tar.gz", "size": 0, "sha256": "...", "signature": "...", "format": "tar.gz"
        }
      },
      "install": {
        "vst3_bundle": "Iterum.vst3",
        "au_component": "Iterum.component",          // macOS only; omit when absent
        "presets_subdir": "Krate Audio/Iterum",      // from version.json preset_subdir logic
        "windows_uninstall_key": "<Inno AppId>_is1", // lets the manager detect/remove legacy .exe installs
        "macos_pkg_id": "com.krateaudio.iterum"      // lets the manager detect/forget legacy .pkg installs
      }
    }
  },
  "manager": { "version": "0.1.0", "pub_date": "...", "release_notes_url": "...", "latest_json_url": "https://.../manager/latest.json" }
}
```
- Key naming: use Tauri's `{os}-{arch}` keys (`windows-x86_64`, `linux-x86_64`, `darwin-aarch64`, `darwin-x86_64`) so the manager's self-update file and the plugin feed share one vocabulary. Add `darwin-universal` as a Krate-specific key (tauri-action itself emits a `darwin-universal` entry when `updaterJsonKeepUniversal` is set, see CI section) and have the manager resolve `darwin-universal` when the native key is missing.
- Borrow from Sparkle: `size` (its `length`), `min_os` (its `minimumSystemVersion`), `release_notes_url` (its `releaseNotesLink`), `channel`. Borrow from Scoop/winget: a `sha256` field per asset and a `SHA256SUMS` release asset. Borrow from Tauri: `signature` carries the `.sig` text inline (no extra HTTP round trip, same as Tauri). Borrow from electron-updater: `size` next to the hash so the client can pre-check Content-Length.
- The Tauri updater plugin cannot read a nested per-plugin object; its static-JSON format must have `version`/`platforms` at the top level and it validates the whole document. So the manager's own self-update needs a second, Tauri-shaped file (`manager/latest.json`), produced by tauri-action and copied to Pages by the feed job; `versions.json` only points to it. Do not try to make one file satisfy both parsers.
- Keep `release_notes` as `""` for the plugins (they render it inline today) and put markdown in `notes_markdown`; switching `release_notes` to long markdown would change what nine shipped editors display in their banners.
- Prereleases: today's `prerelease` input creates GitHub prereleases; put them under `channel: "beta"` entries (e.g. a sibling `"prerelease": {...}` object) rather than in `version`, since the v1 parser upgrades on any higher SemVer in `version`.

### Gaps
- No official Tauri page states the signature algorithm/key format; the minisign claim rests on a secondary source (and the CVE summary). Verify by generating a key with `tauri signer generate` and checking it against `minisign`/`rsign2` before deciding to share one key between plugin zips and the manager.
- Whether Tauri 2's updater accepts a `darwin-universal` platform key natively was not confirmed; tauri-action's `updaterJsonKeepUniversal` suggests the plugin resolves it, but the official updater page lists only the four architectures.

## Hosting: GitHub Pages for the feed, GitHub Releases for binaries, API limits, "latest" URLs, jsDelivr

### Takeaway
Keep the small JSON on GitHub Pages (soft 100 GB/month, 1 GB site, 10-minute edge cache) and every binary on GitHub Releases (no bandwidth or total-size limit, 2 GiB per file); never let the manager call the REST API (60 requests/hour per IP unauthenticated); `/releases/latest/...` is unusable in a monorepo because "latest" is one release per repository; jsDelivr serves git-tree files up to 20 MB, not release assets, so it is not a binary mirror.

### Cited Findings
- GitHub Pages: "soft bandwidth limit of 100 GB per month", published sites "may be no larger than 1 GB", source repositories recommended max 1 GB, deployments time out after 10 minutes, soft limit of 10 builds per hour applies to the default Pages build but not to custom GitHub Actions workflows; if exceeded GitHub may stop serving the site or email suggesting a CDN — [GitHub Pages limits](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits).
- GitHub Pages custom domains: apex, `www` and custom subdomains supported; subdomains use a `CNAME`, apex uses `A`/`ALIAS`/`ANAME`; GitHub recommends always using a `www` subdomain and verifying the domain first "to improve security and avoid takeover attacks"; a domain set on a user/org site applies by default to that account's project sites — [About custom domains and GitHub Pages](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/about-custom-domains-and-github-pages).
- Pages HTTPS: all Pages sites including custom domains support HTTPS and "Enforce HTTPS"; after the DNS check GitHub requests a Let's Encrypt certificate automatically; the full domain name must be under 64 characters; extra `A`/`AAAA`/`ALIAS`/`ANAME` records on `@` or stray `CNAME`s can block certificate issuance — [Securing your GitHub Pages site with HTTPS](https://docs.github.com/en/pages/getting-started-with-github-pages/securing-your-github-pages-site-with-https).
- Pages cache TTL: GitHub Pages responses carry `Cache-Control: max-age=600` (observed in a 2013 Stack Overflow question and repeated by a third-party header guide; no official GitHub doc found) — [Stack Overflow mirror: GitHub pages HTTP headers](https://dev.library.kiwix.org/content/stackoverflow_en_nopic_2021-08/questions/14798589/github-pages-http-headers); [jquery.app cache-control guide](https://www.jquery.app/tools/cache-control-header-builder/).
- GitHub Releases: "Each file included in a release must be under 2 GiB. There is no limit on the total size of a release, nor bandwidth usage"; up to 1000 assets per release — [About releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases); unit confusion GB vs GiB discussed in [community discussion #61733](https://github.com/orgs/community/discussions/61733).
- REST API primary limits: unauthenticated 60 requests/hour per originating IP; authenticated users 5,000/hour; `GITHUB_TOKEN` in Actions 1,000/hour per repository; secondary limits of 100 concurrent requests and 900 points/minute per REST endpoint; exceeding returns 403 or 429 with `x-ratelimit-remaining: 0` — [Rate limits for the REST API](https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api).
- "Latest release" definition: "The latest release is the most recent non-prerelease, non-draft release, sorted by the created_at attribute. The created_at attribute is the date of the commit used for the release"; `make_latest` on create/update accepts `true|false|legacy`, default `true`, "Drafts and prereleases cannot be set as latest", `legacy` orders by creation date and higher semantic version — [REST API: Releases](https://docs.github.com/en/rest/releases/releases?apiVersion=2022-11-28).
- Stable link forms documented by GitHub: `<repo>/releases/latest` and `<repo>/releases/latest/download/<asset-name>` — [Linking to releases](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases).
- `gh release download` without a tag downloads from the latest release and requires `--pattern` — [gh release download manual](https://cli.github.com/manual/gh_release_download).
- jsDelivr GitHub endpoint `/gh/user/repo@version/file` loads "any GitHub release, commit, or branch" (a `@tag` resolves to the git tree at that tag; `latest` falls back to the default branch when there are no tags); "Packages larger than 150 MB or single files larger than 20 MB (in the case of GitHub) are not supported by default"; static versions/commit hashes are cached effectively forever (one-year headers, permanent S3 copy), version aliases including `latest` 7 days, branches 12 hours; purge API requires an email request; "no bandwidth limits or premium features" — [jsDelivr README](https://github.com/jsdelivr/jsdelivr/blob/master/README.md). (An older README copy and a third-party directory quote a 50 MB figure — [8adre/jsdelivr fork README](https://github.com/8adre/jsdelivr), [publicapi.dev](https://publicapi.dev/js-delivr-api) — the current README says 20 MB per file / 150 MB per package.)

### Inferences
- Architecture: `versions.json` (and `manager/latest.json`, `SHA256SUMS` copies if wanted) on Pages; all archives/installers as Release assets; the feed carries full asset URLs so the manager never constructs URLs or touches `api.github.com`. Nine plugins × every DAW launch × 60 requests/hour per IP would be exhausted by a single studio behind NAT if the API were used.
- `/releases/latest/download/<asset>` must not appear in the feed: with `make_latest: true` as the default, "latest" is whichever plugin was released most recently, so a `Vorago` release would silently retarget any `.../latest/download/Iterum-...` link. Use per-tag `browser_download_url` values as returned by the Releases API/`gh release view --json assets` (they already contain the correct encoding for slashed tags).
- Propagation: with `max-age=600` the feed is at most ~10 minutes stale at the edge; the manager should treat the feed as eventually consistent (re-fetch on a manual "Check now" with `Cache-Control: no-cache`) rather than assume an instant flip after a release.
- Custom domain: moving the feed to `krateaudio.com` is supported (CNAME `www`/`downloads` subdomain recommended by GitHub), but nine shipped plugins hard-code the `github.io` URL; keep that URL serving (or confirm GitHub's redirect) before cutting over, and put the new domain into the plugins' `endpointUrl` only after the feed is live there.
- jsDelivr can mirror the JSON (20 MB cap is irrelevant for a feed) but its 12-hour branch cache and 7-day alias cache are slower than Pages' 10 minutes, and it cannot serve the release binaries at all; do not use it.
- Bandwidth estimate: a 100 GB/month Pages cap only matters if large files are on Pages; a 10 KB feed polled by 10,000 installs every launch is ~0.1 GB/day. Keep binaries off Pages.

### Gaps
- No official GitHub page documents the URL encoding of a slash in `/releases/download/<tag>/<asset>` (community code parses the tag as the second-to-last segment and notes `%2F` encoding — [balatro-mod-index commit](https://smallgit.dasguney.com/skyline/balatro-mod-index/commit/2777af675be04fa638ea3d5ee145b24148d66b62)). Confirm with one `gh release view iterum/v<x> --json assets` and store the returned URLs verbatim.
- No official GitHub statement that `github.io` URLs 301 to a configured custom domain was retrieved; test before switching domains, since the in-field plugins depend on it.
- GitHub's own `Cache-Control` policy for Pages is undocumented; the 600 s figure is observational.

## Artifact formats for a manager: plain archive vs driving the existing installers

### Takeaway
Produce, in addition to the current installers, one plain archive per OS containing the bundle(s) plus presets (Windows `.zip` of the `<Name>.vst3` folder, macOS `.zip` made with `ditto -c -k --keepParent`, Linux keep `.tar.gz`), because launcher-style tools (Modrinth/Prism) download plain files and verify hashes, user-level plug-in folders need no elevation on macOS/Linux, and a `.pkg` has no uninstaller.

### Cited Findings
- VST3 plug-in locations (Steinberg): Windows, in priority order, `%LOCALAPPDATA%/Programs/Common/VST3/` (user, "mainly for development"), `/Program Files/Common Files/VST3/`, `/Program Files (x86)/Common Files/VST3/`, `$APPFOLDER/VST3/`; macOS `~/Library/Audio/Plug-ins/VST3/` (user), `/Library/Audio/Plug-ins/VST3/`, `/Network/Library/Audio/Plug-ins/VST3/`, `$APPFOLDER/Contents/VST3/`; Linux `$HOME/.vst3/` (user), then `/usr/lib64/vst3/`, `/usr/lib/vst3/`, `/usr/local/lib64/vst3/`, `/usr/local/lib/vst3/`, `$APPFOLDER/vst3/`; hosts scan recursively — [VST3 Plug-in Locations](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Locations.html).
- Steinberg Help Center repeats `C:\Program Files\Common Files\VST3` and `/Library/Audio/Plug-ins/VST3` with the per-user macOS path "rarely used" — [Steinberg help: VST plug-in locations on macOS](https://helpcenter.steinberg.de/hc/en-us/articles/115000171310-VST-plug-in-locations-on-Mac-OS-X-and-macOS).
- `ditto -c -k --keepParent <bundle> <zip>` creates a PKZip archive "similarly to the Finder's Compress functionality"; without `--keepParent` the top-level entry is `Contents` instead of the bundle; generic zip tools can store framework symlinks as duplicate files which breaks bundle structure and code signatures; verify by `ditto -x -k` into a fresh dir — [Flavio Copes: create a zip with ditto](https://flaviocopes.com/courses/ship-macos-apps/create-a-zip-with-ditto/); Apple forum threads on notarytool rejecting non-ditto zips — [Apple Developer Forums 727123](https://developer.apple.com/forums/thread/727123), [718887](https://developer.apple.com/forums/thread/718887).
- Universal macOS binaries in CMake: `CMAKE_OSX_ARCHITECTURES` seeds each target's `OSX_ARCHITECTURES` (`-arch`), must be set before the first `project()`/`enable_language()` as a `CACHE` entry; if unset the host architecture is used with Xcode compilers — [CMAKE_OSX_ARCHITECTURES](https://cmake.org/cmake/help/latest/variable/CMAKE_OSX_ARCHITECTURES.html); [OSX_ARCHITECTURES target property](https://cmake.org/cmake/help/latest/prop_tgt/OSX_ARCHITECTURES.html).
- Modrinth-style launchers download individual files and verify SHA-1/SHA-512 from the API; `.mrpack` entries MUST carry both hashes; an example Rust client checks byte length against the reported size, then SHA-512, and rejects on mismatch — [Modrinth modpack format](https://docs.modrinth.com/modpacks/format); [uklient commit](https://git.uku3lig.net/uku/uklient/commit/4249195d0fffce653d46fc8fe24ffa7682f7db02).
- Tauri updater artifacts it can install: Windows NSIS `setup.exe` and `.msi` (each with a `.sig`; `createUpdaterArtifacts: "v1Compatible"` wraps them as `.nsis.zip`/`.msi.zip`), macOS `.app.tar.gz`, Linux AppImage only; Windows `installMode` `passive` (default), `basicUi`, `quiet` (quiet cannot elevate, so it works only for per-user installs); the app exits before installing on Windows — [Tauri v2 Updater plugin](https://v2.tauri.app/plugin/updater/).
- Inno Setup uninstaller (`unins???.exe`) switches: `/SILENT` and `/VERYSILENT` skip the startup confirmation (`/VERYSILENT` also hides the progress window and reboots without asking unless `/NORESTART`), `/SUPPRESSMSGBOXES` (only with `/SILENT` or `/VERYSILENT`), `/LOG` or `/LOG="file"`, `/NORESTART` — [Inno Setup help: Uninstaller Command Line Parameters](https://jrsoftware.org/ishelp/topic_uninstcmdline.htm). Deployment guides use `unins000.exe /VERYSILENT /NORESTART` (+ `/SUPPRESSMSGBOXES`) and note the registry `QuietUninstallString` Inno writes uses `/SILENT` rather than `/VERYSILENT` — [Advanced Installer: Inno silent switches](https://www.advancedinstaller.com/innosetup-silent-install-uninstall-paramaters.html); [tranquil.it forum](https://forum.tranquil.it/en/viewtopic.php?p=4724).
- macOS `.pkg` removal: `pkgutil --forget` removes only the receipt ("will not delete any files"); the workflow is `pkgutil --pkgs | grep ...`, `pkgutil --pkg-info <id>` for the install location, `pkgutil --files <id>` for the list, delete files then directories, then `sudo pkgutil --forget <id>`; Homebrew's cask code follows the same sequence — [Scripting OS X: Inspecting packages](https://scriptingosx.com/2025/07/inspecting-packages/); [Revenera: removing PKG files using pkgutil](https://community.revenera.com/s/article/Removing-PKG-Files-Using-pkgutil-on-macOS); [Homebrew Cask::Pkg](https://rubydoc.brew.sh/Cask/Pkg.html).
- macOS Sequoia removed the Control-click "Open" bypass for unsigned/unnotarized apps; the supported route is a failed launch followed by System Settings > Privacy & Security > "Open Anyway"; whether `spctl --master-disable` still works is disputed across sources — [Macworld](https://www.macworld.com/article/2457844/what-to-do-when-you-cant-open-an-app-you-just-installed-in-macos-sequoia.html); [troz.net: App permissions on macOS Sequoia](https://troz.net/post/2024/sequoia_app_permissions/); [iDownloadBlog](https://www.idownloadblog.com/apple-macos-sequoia-gatekeeper-change-install-unsigned-apps-mac/).

### Inferences
- New CI artifacts (names chosen to sort beside the existing ones):
  - `<Name>-<ver>-Windows-x64.zip`: `<Name>.vst3/` (the folder bundle already staged) + `presets/` + `CHANGELOG.txt`; built with PowerShell `Compress-Archive` or `7z a -tzip`; keep the Inno `.exe` for people who prefer it.
  - `<Name>-<ver>-macOS.zip`: `VST3/<Name>.vst3`, `Components/<Name>.component`, optional `<Name> AUv3.app`, `presets/`, `CHANGELOG.txt`; create with `ditto -c -k --keepParent staging <zip>` (or `--sequesterRsrc` added) and never `zip -r`, because VST3/AU bundles are directory trees whose symlinks and executable bits must survive; keep the `.pkg` for the double-click path.
  - Linux: the existing `.tar.gz` is already the right layout; add the same `format: "tar.gz"` entry to the feed.
  - Universal macOS: the manager should prefer one `darwin-universal` asset built with `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, which removes an arch-detection branch from the client and halves the feed; confirm the current CI macOS job already builds universal (not checked here).
- Install strategy for the manager: extract to the per-user folders (`~/Library/Audio/Plug-Ins/{VST3,Components}`, `$HOME/.vst3`) with no elevation on macOS/Linux; on Windows the per-user `%LOCALAPPDATA%\Programs\Common\VST3` is documented as "mainly for development", so default to `C:\Program Files\Common Files\VST3` (needs a UAC prompt; Tauri's `quiet` mode note says the same for its own installer) and offer the per-user folder as an option. Presets go where the current installers put them (`Application Support/Krate Audio/<Name>[/<subdir>]` on macOS; mirror the Inno script's path on Windows).
- Uninstall: the manager writes its own local manifest of extracted paths at install time (that is how Scoop/Homebrew work) and deletes exactly those. For pre-manager installs the feed's `install.windows_uninstall_key` lets it find `unins000.exe` and run `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`; `install.macos_pkg_id` lets it `pkgutil --files` and `--forget` (needs `sudo`, so show an auth prompt or tell the user).
- Driving the existing installers from the manager is viable on Windows (Inno is scriptable) but not on macOS: `installer -pkg` needs root and leaves no uninstaller. That asymmetry is the main argument for shipping plain archives on all three platforms.
- The unsigned manager itself faces Gatekeeper: on Sequoia the first launch requires the System Settings "Open Anyway" step; the download page and the README must describe it. Signing the manager (Apple Developer ID, USD 99/year) is the only way around this and is out of scope for "everything unsigned", but it is the single largest UX cost in the plan.

### Gaps
- Whether files downloaded by a Tauri app's own HTTP stack receive `com.apple.quarantine` (and thus whether extracted unsigned plug-in bundles get Gatekeeper-checked by the DAW) was not sourced; test on a Sequoia machine with `xattr -l`.
- Whether DAWs on Windows scan `%LOCALAPPDATA%\Programs\Common\VST3` by default was not sourced (the Steinberg page labels it "mainly for development"); the repo itself installs there for development (`C:\Users\scab\AppData\Local\Programs\Common\VST3\Membrum.vst3` in this session's working directories), so check Reaper/Ableton/Cubase behaviour before offering it as a user-facing option.
- AU registration after a copy to `~/Library/Audio/Plug-Ins/Components` (whether `auval`/`AudioComponentRegistrar` needs a kick) was not researched.

## Checksums and signatures in CI: SHA256SUMS, minisign/rsign2, Sigstore/attestations, client-side verification

### Takeaway
Use SHA-256 per asset plus one minisign (Ed25519) signature per asset, generated in CI and inlined in the feed; the manager verifies with the dependency-free `minisign-verify` Rust crate; add GitHub artifact attestations as a free, separate provenance layer that humans verify with `gh attestation verify` (offline possible with `--bundle` and `--custom-trusted-root`), but do not make the client depend on Sigstore.

### Cited Findings
- `minisign-verify` (Rust): "zero-dependency verification of Minisign signatures", supports standard and pre-hashed modes, streaming verification only for pre-hashed signatures; current version 0.2.5 — [docs.rs minisign-verify](https://docs.rs/minisign-verify); [lib.rs minisign](https://lib.rs/crates/minisign).
- `minisign` crate (Rust): full pure-Rust implementation that can also sign; points to `minisign-verify` for verification-only use — [docs.rs minisign](https://docs.rs/minisign).
- `rsign` crate: a Rust minisign-compatible CLI (`rsign2`) — [docs.rs rsign](https://docs.rs/crate/rsign/latest).
- JavaScript: no dedicated minisign npm verifier surfaced; `@noble/ed25519` (RFC 8032/ZIP215-compliant, Node/Deno/Bun/browsers) or Node's built-in `crypto` Ed25519 can verify the raw signature if the minisign file format (key id, base64 signature, trusted comment, prehashed `ED` variant) is parsed by hand — [@noble/ed25519 on npm](https://npmjs.com/package/@noble/ed25519); Go reference implementation [aead.dev/minisign](https://pkg.go.dev/aead.dev/minisign).
- GitHub artifact attestations: "cryptographically signed claims that establish your build's provenance"; GitHub uses Sigstore; for public repositories the Sigstore Public Good Instance is used and the bundle is written to a public immutable transparency log; private repositories use GitHub's Sigstore instance without a transparency log; "Artifact attestations by itself provides SLSA v1.0 Build Level 2"; verification is via the GitHub CLI — [Artifact attestations concept](https://docs.github.com/en/actions/concepts/security/artifact-attestations).
- `gh attestation verify` flags: `--bundle` "Path to bundle on disk, either a single bundle in a JSON file or a JSON lines file with multiple bundles"; `--custom-trusted-root` "Path to a trusted_root.jsonl file; likely for offline verification"; by default attestations are fetched through the GitHub API; `--digest-alg` `sha256` (default) or `sha512`; default predicate type `https://slsa.dev/provenance/v1`; `--deny-self-hosted-runners`, `--signer-workflow` available — [gh attestation verify manual](https://cli.github.com/manual/gh_attestation_verify).
- Sigstore in Rust: the `sigstore-verify` crate's example loads `TrustedRoot::production()`, reads a bundle and calls `verify(...)` with a policy; the `sigstore` crate has a `sigstore-trust-root` feature; the Rekor inclusion proof is embedded in the bundle so no live Rekor call is needed, but a current trusted root must be available locally and offline roots go stale as keys rotate — [docs.rs sigstore-verify](https://docs.rs/sigstore-verify); [docs.rs sigstore](https://docs.rs/sigstore/); [OneUptime: verify cosign signatures air-gapped](https://oneuptime.com/blog/post/2026-08-11-verify-cosign-air-gapped-sigstore-bundles/markdown).
- Sparkle's `sign_update` prints `sparkle:edSignature` and `length`; Sparkle requires signed updates — [Sparkle publishing](https://sparkle-project.org/documentation/publishing/). Tauri requires a signature per platform entry and "signing cannot be disabled" — [Tauri v2 Updater plugin](https://v2.tauri.app/plugin/updater/).
- Scoop verifies `hash` (SHA256 default) on every download; winget requires `InstallerSha256` — [Scoop App Manifests](https://github.com/ScoopInstaller/Scoop/wiki/App-Manifests); [Microsoft Learn manifest](https://learn.microsoft.com/fi-fi/windows/package-manager/package/manifest).

### Inferences
- CI steps per release job (after the archives exist): `sha256sum *.zip *.exe *.pkg *.tar.gz > SHA256SUMS` (`shasum -a 256` on macOS; in practice run it once on the ubuntu `release` job after downloading all artifacts so one file covers everything); sign each asset with `rsign2 sign -s <key> -x <asset>.minisig <asset>` (or the `minisign` apt package); upload `SHA256SUMS`, every `.minisig`, and sign `SHA256SUMS` too. Store the private key as a repository secret; the public key is compiled into the manager and published on the site.
- Feed generation then reads each `.minisig` into `platforms.<key>.signature` and each line of `SHA256SUMS` into `sha256`, so the client needs the feed only; the `.minisig` assets remain for manual checks.
- Client verification order (Modrinth-style): compare `Content-Length` to `size`, stream to a temp file computing SHA-256, compare, then `minisign-verify` the file against the embedded public key, then extract. Signature protects against a compromised Pages/Release (the hash in the feed would be replaced too); the hash alone catches corruption.
- If Tauri's signer keys are confirmed minisign-compatible, one keypair (`TAURI_SIGNING_PRIVATE_KEY`) can sign both the manager bundles and the plugin archives, and the manager's self-update and plugin verification use the same public key. If not, keep two keys; the cost is one more secret.
- Attestations: add `actions/attest-build-provenance` with `subject-path` on each uploaded asset in the `release` job; it is free for public repos and gives `gh attestation verify <file> --owner <owner>` to anyone. Do not embed Sigstore verification in the manager: it adds a TUF trusted-root refresh (network or stale-root problem) and a large dependency for a check minisign already provides.

### Gaps
- No source quantified `actions/attest-build-provenance` limits (number of subjects per call, bundle retention); check the action README before relying on it for ~6 assets × 9 plugins.
- Whether the `tauri signer sign` CLI can sign arbitrary files (so the plugin archives could be signed without `rsign2`) was not confirmed.

## CI pipeline changes: manager matrix build, idempotent feed regeneration, incremental Pages deploy

### Takeaway
Add a `release-manager.yml` (tauri-action matrix, tag `manager/vX.Y.Z`, `includeUpdaterJson` producing `latest.json` with native darwin keys from a universal build) and a separate `feed.yml` that rebuilds `versions.json` from the Releases list (`gh release list` / `gh release view --json`) plus a tiny per-release `release.json` asset, deploys with `keep_files: true`, and is called by both release workflows and runnable by hand.

### Cited Findings
- tauri-action: `releaseId` takes priority over `tagName`/`releaseName`; with `tagName` and no existing release, `releaseName` is required to create one; leaving all three out builds without uploading; `includeUpdaterJson` defaults to true when the updater is configured; universal macOS binaries are mapped to both `darwin-aarch64` and `darwin-x86_64` entries; `updaterJsonKeepUniversal` (default false) adds a separate `darwin-universal` entry; `args: --target universal-apple-darwin` on the macOS matrix leg; if a `releaseId` is passed without `tagName` the generated `latest.json` points at `releases/latest/download/<bundle>`; a `create-release` job can output a `release_id` consumed by the matrix so all platforms upload to one release — [tauri-apps/tauri-action](https://github.com/tauri-apps/tauri-action); [tauri-action docs mirror](https://docsearch.algolia.com/mcp/docs/repo/tauri-apps/tauri-action).
- Tauri updater build inputs: `TAURI_SIGNING_PRIVATE_KEY` (path or content) and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` at build time; `createUpdaterArtifacts: true` yields NSIS/MSI + `.sig`, `.app.tar.gz` + `.sig`, AppImage + `.sig` — [Tauri v2 Updater plugin](https://v2.tauri.app/plugin/updater/).
- `GITHUB_TOKEN` REST budget inside Actions is 1,000 requests/hour per repository — [Rate limits for the REST API](https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api).
- Pages deploy via Actions is not subject to the 10 builds/hour soft limit; deployments time out after 10 minutes — [GitHub Pages limits](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits).
- Scoop's `autoupdate` pattern derives hashes from a published `SHA256SUMS`-style file rather than re-downloading binaries (page lists the field; details on the Autoupdate wiki page) — [Scoop App Manifests](https://github.com/ScoopInstaller/Scoop/wiki/App-Manifests).
- Current repo: the feed is produced only inside `release.yml`'s `deploy-pages` job from local tags, and the root index is generated by an inline Python script — `F:\projects\iterum\.github\workflows\release.yml` (project rule: helper scripts must be Node, `F:\projects\iterum\CLAUDE.md` "Use Node.js for Helper Scripts").

### Inferences
- Per-release manifest: in `release.yml`'s `release` job, after computing hashes and signatures, write `release.json` (`{tag, name, version, pub_date, assets: {<platform-key>: {url, size, sha256, signature, format}}, install: {...}}`) and upload it as a release asset. It is a few KB and makes feed regeneration O(number of plugins) small downloads instead of re-hashing gigabytes.
- `feed.yml` (reusable `workflow_call` + `workflow_dispatch`): `gh release list --limit 300 --json tagName,isPrerelease,isDraft,publishedAt` → group by the `<plugin>/` prefix → pick the highest non-prerelease SemVer per plugin (and the highest prerelease for a `beta` channel) → `gh release download <tag> --pattern release.json` for each → merge into `versions.json` (v1 keys + v2 keys) and copy the manager's `latest.json` to `manager/latest.json` → `peaceiris/actions-gh-pages@v4` with `keep_files: true`. Idempotent: running it twice yields the same file; a plugin whose release is deleted disappears on the next run. Budget: ~20 API calls per run against 1,000/hour.
- Keep the tag as the source of truth for "what is released" but take it from Releases, not from `git tag -l`, so the feed job does not need `fetch-depth: 0` and can run from any trigger (including a manual re-run after a hotfix to the generator).
- Prefer `make_latest: false` (via `softprops/action-gh-release` input or `gh release edit`) on plugin releases, or accept that "latest" is meaningless in this repo; either way nothing may link to `/releases/latest`.
- `release-manager.yml`: `create-release` job (tag `manager/v<version from manager's tauri.conf.json>`, outputs `release_id`) → matrix `{windows-latest, macos-latest (args --target universal-apple-darwin, updaterJsonKeepUniversal: true), ubuntu-22.04}` with `tauri-apps/tauri-action@v0`, `releaseId`, `includeUpdaterJson: true`, `TAURI_SIGNING_PRIVATE_KEY*` secrets → `feed.yml`. Because `latest.json` from tauri-action with a `releaseId` points to `releases/latest/download/`, the feed job should rewrite those URLs to the per-tag `releases/download/manager/v<ver>/` form before publishing `manager/latest.json`, or pass `tagName` so the action emits per-tag URLs.
- Linux manager artifact: the Tauri updater only updates AppImage, so ship the AppImage as the self-updating format and the `.deb` as a convenience that does not self-update.
- Also add the plain-archive steps to the three existing installer jobs and attestations to the `release` job (previous sections).

### Gaps
- tauri-action's exact input defaults were read from mirrors/forks, not the current README; confirm `includeUpdaterJson`, `updaterJsonKeepUniversal`, `tagName`/`releaseId` semantics against the release pinned in the workflow.
- Whether `softprops/action-gh-release` exposes `make_latest` was not checked.

## Backward compatibility with the nine shipped parsers

### Takeaway
Additive fields in the same `versions.json` are safe (the parser ignores unknown keys and never reads `schema_version`); keep `version`, `download_url`, `release_notes` as strings at their current paths, put everything new beside them, and reserve a separate file only for the Tauri-shaped `manager/latest.json`, which no plugin reads.

### Cited Findings
- Parser requirements (strings only, unknown keys ignored, no `schema_version` check, exceptions yield "no update") — `F:\projects\iterum\plugins\shared\src\update\update_checker.cpp` lines 221-262.
- Tauri static JSON requires `version` and `platforms` at the top level and validates the whole document before use — [Tauri v2 Updater plugin](https://v2.tauri.app/plugin/updater/).
- Sparkle's compatibility practice: older `sparkle:version` placement on the enclosure "still works"; new elements are additive — [Sparkle publishing](https://sparkle-project.org/documentation/publishing/).

### Inferences
- Option A (recommended): one `versions.json`, `schema_version: 2`, additive keys. Risk: a future plugin build that starts checking `schema_version == 1` would break; forbid that in `update_checker.cpp` (accept any `schema_version >= 1`) in the next shared-code release.
- Option B: freeze `versions.json` as v1 forever and publish `manager.json` for the manager. Costs a second generator and lets the two drift; only worth it if a non-additive change (e.g. `release_notes` becoming an object) is ever wanted.
- Either way `manager/latest.json` is a third, Tauri-format file; the plugins never see it.
- Do not change `download_url` to an asset URL: the banners open it in a browser and a `.exe`/`.pkg` auto-download is a worse experience than the landing page; the per-platform URLs go under `platforms`.
- Add a test in `plugins/shared/tests` that feeds a v2 sample document to `parseResponse` and asserts the v1 fields are still read; it is the cheapest guard against a regression in the shared parser.

### Gaps
- None beyond the parser facts, which were read directly.

## Cost: Actions minutes for a public repo, macOS multipliers, artifact storage and retention

### Takeaway
Standard GitHub-hosted runners, including macOS, are free for public repositories, so the extra matrix and feed jobs cost nothing in minutes; only larger runners are billed in public repos; artifacts expire after at most 90 days in public repos, so release assets, not workflow artifacts, are the durable store.

### Cited Findings
- "The use of standard GitHub-hosted runners is free: In public repositories ..."; "Larger runners are billed for public repositories"; baseline per-minute prices (relevant only to private repos / larger runners): Linux 2-core x64 USD 0.006, Windows 2-core x64 USD 0.010, macOS 3-/4-core USD 0.062, Linux 2-core arm64 USD 0.005; GitHub Free includes USD 12/month of compute, 500 MB artifact storage shared with Packages, 10 GB cache per repository — [How use of GitHub Actions is measured](https://docs.github.com/en/billing/concepts/product-billing/github-actions).
- Minute multipliers for private-repo included minutes: Linux 1x, Windows 2x, macOS 10x; minutes rounded up per job — [About billing for GitHub Actions (usage limits reference)](https://docs.github.com/en/actions/reference/usage-limits-billing-and-administration).
- Artifacts and logs are retained 90 days by default; public repositories can set 1-90 days (private 1-400); from October 1, 2026 checks, workflow runs and statuses fall under the same retention setting — [Usage limits, billing and administration](https://docs.github.com/en/actions/reference/usage-limits-billing-and-administration); [GitHub Changelog, 2026-08-27](https://github.blog/changelog/2026-08-27-actions-retention-will-cover-checks-workflow-runs-and-statuses).
- Scheduled workflows in a public repository are disabled automatically after 60 days without repository activity — [Usage limits, billing and administration](https://docs.github.com/en/actions/reference/usage-limits-billing-and-administration).
- Releases: no bandwidth or total-size limit; 2 GiB per file — [About releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).

### Inferences
- The manager matrix (one macOS universal leg, one Windows, one Linux) and a feed job add zero billable minutes as long as the repo stays public and uses `*-latest` standard runners; do not pick larger runners for speed.
- Do not make the feed job rely on expired workflow artifacts: everything it needs must be a Release asset (`release.json`, `SHA256SUMS`, `.minisig`).
- If a scheduled "refresh feed" cron is added, note the 60-day inactivity auto-disable; triggering `feed.yml` from the two release workflows avoids that.

### Gaps
- Whether the 500 MB artifact-storage allowance applies to public repositories (the page lists it under the Free plan without a public-repo exemption) was not resolved; the current pipeline's artifacts are a few MB per release and expire, so it is unlikely to matter.
