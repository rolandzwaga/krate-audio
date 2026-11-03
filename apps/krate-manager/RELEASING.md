# Releasing Krate Manager

One manual dispatch of `.github/workflows/release-manager.yml` ("Release Krate Manager") releases the version in
`apps/krate-manager/version.json` as tag `manager/vX.Y.Z`. Its `preflight` job refuses a release by error code before
any build.

## Version

Set a new version with one command from the repository root, never by hand:

```
node tools/bump-manager-version.js X.Y.Z
```

It writes `version.json` and every copy (`Cargo.toml`'s workspace version, `src-tauri/tauri.conf.json`, the manager
crates' entries in `Cargo.lock` and `tests/fixtures/*/Cargo.lock`) and the `CHANGELOG.md` heading: an unreleased
newest entry takes the new number, otherwise a new `## [X.Y.Z] - Unreleased` goes above it. It refuses, writing
nothing, when `tools/lint-manager-version.js` fails on the tree or the version is not newer, and checks the lint again
afterwards.

## Procedure

1. Record the RustSec check for the version in the table below (section "RustSec"). Without a matching row at most
   7 days old, `preflight` refuses dry and real dispatches alike with `E_PREFLIGHT_RUSTSEC`.
2. Dispatch `release-manager.yml` with `dry_run` on (the default) and wait for it to finish green.
3. Download the dry run's `manager-landing-dry-run` artifact (job `landing-dry`) and read its `index.html`: the Krate
   Manager card names the version and links its four installers.
4. Before each manager release, open one release-asset download URL of the release and confirm its redirect host is
   `release-assets.githubusercontent.com` (`RELEASE_ASSET_HOST`, `crates/core/src/self_update.rs:32`).
5. Date the version's `CHANGELOG.md` heading (`## [X.Y.Z] - YYYY-MM-DD`; a real run refuses `- Unreleased` with
   `E_PREFLIGHT_CHANGELOG_UNDATED`), then dispatch `release-manager.yml` with `dry_run` off: the real release.

## RustSec

Before every manager release, read the RustSec advisory database, https://rustsec.org/, for the three `rquickjs`
crates the manager locks:

- `rquickjs`: https://rustsec.org/packages/rquickjs.html
- `rquickjs-core`: https://rustsec.org/packages/rquickjs-core.html
- `rquickjs-sys`: https://rustsec.org/packages/rquickjs-sys.html

An advisory affecting a locked version is answered with a pin bump before the release.

Add one row per released version: `version`, `date` (the UTC date of the reading, `YYYY-MM-DD`), the versions of the
three crates in `apps/krate-manager/Cargo.lock`, and `result`: `no advisory`, or the advisory ids and the bump that
answered them (`RUSTSEC-YYYY-NNNN answered by bump to rquickjs A.B.C, rquickjs-core A.B.C, rquickjs-sys A.B.C`).
`preflight` refuses with `E_PREFLIGHT_RUSTSEC` unless the row for the version being released names the three
`Cargo.lock` versions and is dated at most 7 days before the dispatch.

| version | date | rquickjs | rquickjs-core | rquickjs-sys | result |
|---|---|---|---|---|---|
| 1.0.0 | 2026-11-03 | 0.14.0 | 0.14.0 | 0.14.0 | no advisory |
| 1.0.1 | 2026-11-03 | 0.14.0 | 0.14.0 | 0.14.0 | no advisory |
