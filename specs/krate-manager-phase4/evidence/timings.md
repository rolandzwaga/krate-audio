# Krate Manager Phase 4: close-lane wall clocks

Written by `evidence/close-lane.js`, 2026-10-30T21:21:24.338Z. Total wall 142.8s; CLOSE LANE GREEN. Raw logs (cleaned at close): `F:/tmp/gate/krate-manager-phase4-close/20261030-211901`.
A1 (cargo) and A2 (npm) run as two chains beside A3-A7 and A9; A8 starts after A1; B starts after A1 and A2.

| Row | Result | Wall (s) | Command |
|---|---|---|---|
| A0 | GREEN | 0.0 | rustc="rustc 1.99.1 (58cf8f9c3 2026-10-13)" node="v24.7.0" df(repo root)="F: 50G 33G 18G 65% /f" |
| A0b | GREEN | - | git status --porcelain: 95 lines (without 4 close-lane outputs) file=F:/tmp/gate/krate-manager-phase4-close/20261030-211901/A0b-git-status.txt |
| A1a | GREEN | 0.5 | step cargo fmt exit=0 last="" |
| A1b | GREEN | 1.7 | step clippy exit=0 warning-lines=0 notices="" |
| A1c | GREEN | 1.1 | step clippy dev-feed exit=0 warning-lines=0 notices="::notice title=clippy dev-feed (Windows)::clean" |
| A1d | GREEN | 1.6 | step clippy-guard exit=0 notice="::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0" copied to specs/krate-manager-phase4/evidence/clippy-guard.txt |
| A1e | GREEN | 2.4 | step cargo test exit=0 binaries=8 passed=49 binaries-with-failures=0 |
| A1f | GREEN | 0.5 | check_under_temp_symlink_escape exit=0 ok-line=found link made (the escape assertion ran) |
| A1g | GREEN | 0.3 | cargo tree -p krate-manager-core -e normal exit=0 crates=101 root=ok banned=none (tauri, dirs, directories, home) -> specs/krate-manager-phase4/evidence/core-tree.txt |
| A2a | GREEN | 8.9 | step npm ci exit=0 last="found 0 vulnerabilities" |
| A2b | GREEN | 2.1 | step typecheck exit=0 last="> tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json" |
| A2c | GREEN | 2.1 | step lint exit=0 last="> eslint . --max-warnings 0" |
| A2d | GREEN | 1.7 | step vitest exit=0 last="JSON report written to F:/tmp/gate/krate-manager-phase4-close/20261030-211901/vitest.json" |
| A3a | GREEN | 24.9 | node --test exit=0 plan=1..59 tests=745 pass=740 fail=0 skipped=5 nodeWarnings=0 |
| A3b | GREEN | 0.2 | L-TAP exit=0 want "TAP plan=1..59 top-level=59" printed last="TAP plan=1..59 top-level=59" |
| A3c | GREEN | 0.2 | L-VLINT exit=0 stderr=none |
| A4 | GREEN | 2.1 | L-YAML .github/workflows/manager-ci.yml exit=0 |
| A4 | GREEN | 1.8 | L-YAML .github/workflows/feed-tools.yml exit=0 |
| A5 | GREEN | 0.1 | L-LINT lint-serial-test-runs exit=0 last="lint-serial-test-runs: clean (64 files)" |
| A6 | GREEN | 0.3 | gen-specs-index --check exit=0 last="specs/INDEX.md is up to date." |
| A7a | GREEN | 0.1 | scope-check --self-test exit=0 last="SELF-TEST GREEN (9 cases)" |
| A7b | GREEN | 1.2 | L-SCOPE scope-check exit=0 last="scope-check: 18 paths, 3 roadmap hunks: OK" |
| A8 | GREEN | 0.5 | L-XCHECK x86_64-unknown-linux-gnu exit=0 warning-lines=0 |
| A8 | GREEN | 0.4 | L-XCHECK aarch64-apple-darwin exit=0 warning-lines=0 |
| A9 | GREEN | 0.1 | SC-021 grep exit=1 printed nothing |
| B1 | GREEN | 0.1 | step required tests ran exit=0 notices="::notice title=required tests ran (Windows)::49 cargo + 3 vitest" want "::notice title=required tests ran (Windows)::49 cargo + 3 vitest" |
| B2 | GREEN | 126.1 | step tauri build exit=0 last="F:\projects\iterum\apps\krate-manager\target\release\bundle\nsis\Krate Manager_0.1.0_x64-setup.exe (1.30 MiB)" |
| B3 | GREEN | 0.1 | step build warnings exit=0 notices="::notice title=build warnings (Windows)::0 warning lines in the release build" want "::notice title=build warnings (Windows)::0 warning lines in the release build" |
| B4 | GREEN | 0.1 | step dev-feed absent exit=0 notices="::notice title=dev-feed absent (Windows)::marker in source=1, in target/release/krate-manager.exe=0" want "::notice title=dev-feed absent (Windows)::" |
| B5 | GREEN | 0.6 | step artifact version exit=0 notices="::notice title=artifact version (Windows)::0.1.0 (nsis=present)" want "::notice title=artifact version (Windows)::0.1.0 (nsis=present)" |
| C1 | GREEN | - | git status --porcelain equal to A0b (95 lines) file=F:/tmp/gate/krate-manager-phase4-close/20261030-211901/C1-git-status.txt |
| C2 | GREEN | 0.4 | L-SCOPE scope-check exit=0 last="scope-check: 18 paths, 3 roadmap hunks: OK" |
