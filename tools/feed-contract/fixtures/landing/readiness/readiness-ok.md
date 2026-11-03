# Fixture readiness ledger

A fixture for tools/feed-contract/tests/manager-readiness-counts.test.js, laid out as
specs/krate-manager-phase12/tasks.md TA-16 fixes it. Every PENDING-CI row is closed by a green run except
phase11:SC-020, which is deferred to the first real dispatch (D17).

## Counts

```
phase1 file=2 status=2
phase2 file=1 status=1
phase3 file=0 status=0
phase4 file=1 status=1
phase5 file=1 status=2
phase6 file=1 status=1
phase7 file=1 status=1
phase8 file=1 status=1
phase9 file=1 status=1
phase10 file=1 status=1
phase11 file=3 status=3
total file=13 status=14
```

## PENDING-CI rows

| id | job and assertion | closing workflow | run URL | commit | verdict |
|---|---|---|---|---|---|
| phase1:FR-001 | feed-tools.yml `node-test (ubuntu-latest)` proves the POSIX branch. | feed-tools.yml | https://github.com/o/r/actions/runs/101 | 0123abc | green |
| phase1:FR-003 | `node-test (macos-latest)` \| `node-test (ubuntu-latest)` | feed-tools.yml | https://github.com/o/r/actions/runs/101 | 0123abc | green |
| phase2:SC-001 | Needs a `dry_run: true` dispatch of `release.yml`. | release.yml | https://github.com/o/r/actions/runs/102 | 0123456789abcdef0123456789abcdef01234567 | green |
| phase4:FR-010 | `check (ubuntu-24.04)` | manager-ci.yml | https://github.com/o/r/actions/runs/104 | 89abcdef | green |
| phase5:FR-010 | `check (ubuntu-24.04)` | manager-ci.yml | https://github.com/o/r/actions/runs/104 | 89abcdef | green |
| phase6:FR-010 | `check (ubuntu-24.04)` | manager-ci.yml | https://github.com/o/r/actions/runs/104 | 89abcdef | green |
| phase7:FR-010 | `check (ubuntu-24.04)` | manager-ci.yml | https://github.com/o/r/actions/runs/104 | 89abcdef | green |
| phase8:FR-010 | `check (ubuntu-24.04)` | manager-ci.yml | https://github.com/o/r/actions/runs/104 | 89abcdef | green |
| phase9:FR-010 | `check (ubuntu-24.04)` | manager-ci.yml | https://github.com/o/r/actions/runs/104 | 89abcdef | green |
| phase10:FR-010 | `check (ubuntu-24.04)` | manager-ci.yml | https://github.com/o/r/actions/runs/104 | 89abcdef | green |
| phase11:FR-004 | Needs `build (macos-latest)`. | release-manager.yml | https://github.com/o/r/actions/runs/111 | fedcba9 | green |
| phase11:SC-014 | Needs `dry-publish` (its dry half; the `publish` half is a post-release row). | release-manager.yml | https://github.com/o/r/actions/runs/111 | fedcba9 | green |
| phase11:SC-020 | Phase 12 exit criterion; needs the user's first real dispatch. | release-manager.yml |  |  | post-release |

## Gates

| check | run URL | commit | verdict |
|---|---|---|---|

## Backfill and live feed

| check | run URL | commit | verdict |
|---|---|---|---|

## RustSec

| version | date | rquickjs | rquickjs-core | rquickjs-sys | result |
|---|---|---|---|---|---|

## Asset host

| release-asset URL | redirect host | date | verdict |
|---|---|---|---|

## Post-release rows

| id | job and assertion | closing workflow | run URL | commit | verdict |
|---|---|---|---|---|---|
| phase11:SC-014:publish | `publish` | the first real dispatch of release-manager.yml |  |  | post-release |
| phase11:SC-020 | Phase 12 exit criterion | the first real dispatch of release-manager.yml |  |  | post-release |
| phase12:R-POST-1 | the live landing card links 0.1.0's four installers | the first real dispatch of release-manager.yml |  |  | post-release |
| phase12:R-POST-2 | FR-075, the macOS self-update of a pkg-installed 0.1.0 | the release after 0.1.0 |  |  | post-release |
| phase12:R-POST-3 | a pkg-installed 0.1.0 sees the next release | the release after 0.1.0 |  |  | post-release |
| phase12:R-POST-4 | FR-073, the macOS GUI pkg check on Sequoia and Tahoe | the first real dispatch of release-manager.yml |  |  | post-release |
| phase12:R-POST-5 | FR-074 (d) and (e), the macOS plug-in install and Remove | the first real dispatch of release-manager.yml |  |  | post-release |
