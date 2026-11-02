# Krate Manager Phase 12: user-performed checks

Only the user fills a row; agents never perform or fill these checks (FR-070).

Each check has one table (spec FR-070; plan §4.10). Columns: `id`; `date`; `machine`; `OS name and build` (the
`sw_vers` / `winver` / `lsb_release -a` output); `manager version and source` (the `release-manager.yml` run URL or the
release tag); `steps as performed`; `observed result`; `evidence files` (paths under
`specs/krate-manager-phase12/evidence/`: screenshots or terminal transcripts); `result`. A row counts as recorded when
its `result` cell is not empty. A literal `|` inside a cell is written `\|`.

The macOS first-launch text (`macos.*` in `tools/feed-contract/first-launch-statements.json`) is written only after
both FR-073 rows are recorded (FR-076); `tools/feed-contract/tests/manager-first-launch.test.js` reads their `result`
cells.

## FR-073 Sequoia: macOS GUI pkg check on macOS 15.x

The pkg is `Krate-Manager-<v>-macOS.pkg` from the `manager-release-dry-run` artifact of a green `release-manager.yml`
`dry_run` at the readiness commit (record the run URL and commit), served as a plain file over HTTP(S) from a machine
you control and downloaded on the Mac by opening its URL (record it) in a browser (record its name and version).
Opening the artifact's zip, copying the pkg by `scp` / `curl` / AirDrop, or adding the attribute with `xattr -w` does
not count. Before the double-click record `xattr -l` and `xattr -p com.apple.quarantine` of the downloaded pkg;
double-click; record whether Installer.app refuses it and whether Privacy & Security offers "Open Anyway"; if offered,
complete the install; then record `xattr -l "/Applications/Krate Manager.app"` and whether the app launches.

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| FR-073-sequoia | | | | | | | | |

## FR-073 Tahoe: macOS GUI pkg check on macOS 26.x

The same steps as FR-073 Sequoia, on Tahoe 26.x.

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| FR-073-tahoe | | | | | | | | |

## FR-074 (a): Windows install through the manager

After FR-103 is recorded: install one plugin through the manager (the real Inno `.exe`); record the UAC prompt text
and the result.

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| FR-074-a | | | | | | | | |

## FR-074 (b): Windows locked update

Install the plugin's previous release by hand from its per-tag Inno `.exe` URL
(`https://github.com/<repo>/releases/download/<plugin>/v<previous>/<Name>-<previous>-Windows-x64.exe`, recorded here);
start a DAW with that plug-in loaded; Update it through the manager and record the "close your DAW and retry" text;
close the DAW, retry, and record the success.

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| FR-074-b | | | | | | | | |

## FR-074 (c): Windows Remove

Remove the plugin through the manager (the Inno uninstaller); record the result.

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| FR-074-c | | | | | | | | |

## FR-074 (d): macOS install through the manager

Install one plugin through the manager (the real `.pkg`); record the osascript prompt text, the result and the AU
rescan notice.

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| FR-074-d | | | | | | | | |

## FR-074 (e): macOS Remove

Remove the plugin through the manager (the `pkgutil` route); record the result.

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| FR-074-e | | | | | | | | |

## FR-074 (f): Linux install and Remove

Install and Remove one plugin through the manager; record that no prompt appears and the placed paths
(`~/.vst3/<P>.vst3`, `~/Documents/Krate Audio/<P>`).

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| FR-074-f | | | | | | | | |

## FR-075 / R-POST-2: macOS self-update of a pkg-installed manager (post-release)

After manager 0.1.0 is public: install 0.1.0 from its pkg (FR-073's route); when the next public manager release is
announced at `https://krateaudio.com/manager/latest.json`, let 0.1.0 update itself. Record the two versions and their
release URLs, the admin prompt, the version shown after relaunch, and `ls -l` of `/Applications/Krate Manager.app` and
of its `Contents/MacOS/krate-manager-helper` after the update, showing you as owner.

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| FR-075 | | | | | | | | |

## R-POST-3: Windows NSIS and Linux AppImage self-update (post-release)

On the same occasion as FR-075: a Windows NSIS install and a Linux AppImage install of 0.1.0 update to the same next
release; record the version shown after relaunch on each.

| id | date | machine | OS name and build | manager version and source | steps as performed | observed result | evidence files | result |
|---|---|---|---|---|---|---|---|---|
| R-POST-3 | | | | | | | | |
