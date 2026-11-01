# Krate Manager Phase 8: close-lane wall clocks

Written by `evidence/close-lane.js`, 2026-11-01T09:08:45.049Z. Total wall 80.8s; CLOSE LANE GREEN. Raw logs (removed by C3): `F:/tmp/gate/krate-manager-phase8-close/20261101-090724`.
A1 (git scans, then cargo) and A2 (npm) run as two chains beside A3-A7, A9, A10 (A10 is L-WSL, on WSL's own cores); B starts after A1 and A2; C runs last. No `tauri build` row (TA-11).

| Row | Result | Wall (s) | Command |
|---|---|---|---|
| A0 | GREEN | 0.1 | rustc="rustc 1.99.1 (58cf8f9c3 2026-10-13)" node="v24.7.0" df(repo root)="F: 50G 46G 4.8G 91% /f" |
| A0b | GREEN | - | git status --porcelain: 21 lines (without specs/krate-manager-phase8/evidence/) file=F:/tmp/gate/krate-manager-phase8-close/20261101-090724/A0b-git-status.txt |
| A1a | GREEN | 2.1 | step cargo fmt last="" exit=0 |
| A1b | GREEN | 1.7 | step clippy warning-lines=0 notices="" exit=0 |
| A1c | GREEN | 5.1 | step clippy dev-feed warning-lines=0 notices="::notice title=clippy dev-feed (Windows)::clean" exit=0 |
| A1d | GREEN | 5.0 | step clippy-guard notice="::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0" copied to specs/krate-manager-phase8/evidence/clippy-guard.txt exit=0 |
| A1e | GREEN | 35.0 | step cargo test binaries=11 passed=276 binaries-with-failures=0 -> specs/krate-manager-phase8/evidence/cargo-test.txt exit=0 |
| A1f | GREEN | 0.6 | step core dependency tree notices="::notice title=core dependency tree (Windows)::88 crates, none excluded" copied to specs/krate-manager-phase8/evidence/core-tree.txt exit=0 |
| A1h | GREEN | 0.3 | cargo tree -p krate-manager-core -e normal --locked --prefix none --format {p} -> specs/krate-manager-phase8/evidence/core-tree-phase8.txt lines=169 flate2 v1.1.10 present, no tar / tauri / tokio / reqwest / hyper / dirs / home exit=0 |
| A1l | GREEN | 5.0 | cargo tree --workspace --target all -e features -i zlib-rs --locked: "nothing to print"; cargo tree --workspace --target all -i tar --locked: "error: package ID specification `tar` did not match any packages" exit=0/exit=101 |
| A1j | GREEN | 0.1 | SC-001 git diff -U0 2499fb13 -- apps/krate-manager -> specs/krate-manager-phase8/evidence/diff-u0.txt (2312 lines) plus 2 untracked file(s) read whole: no added allow( / expect( attribute, @ts-ignore or eslint-disable exit=0/exit=0 |
| A1k | GREEN | 0.1 | FR-090 git diff --name-only 2499fb13 -- apps/krate-manager/src apps/krate-manager/src-tauri apps/krate-manager/index.html: empty; untracked under them: empty exit=0/exit=0 |
| A1m | GREEN | 0.1 | rule 8 / TA-1 git diff --numstat 2499fb13: apps/krate-manager/Cargo.lock: empty (want empty); apps/krate-manager/tests/fixtures/clippy-guard/Cargo.lock: 1 0 apps/krate-manager/tests/fixtures/clippy-guard/Cargo.lock (want "1 0") exit=0/exit=0 |
| A2a | GREEN | - | node_modules present: step npm ci not run (tasks.md T006 A2) |
| A2b | GREEN | 4.5 | step typecheck last="> tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json" exit=0 |
| A2c | GREEN | 4.0 | step lint last="> eslint . --max-warnings 0" exit=0 |
| A2d | GREEN | 2.9 | step vitest last="JSON report written to F:/tmp/gate/krate-manager-phase8-close/20261101-090724/vitest.json" exit=0 |
| A3a | GREEN | - | node --check on 3 files: all ok |
| A3b | GREEN | 24.3 | node --test plan=1..59 tests=752 pass=747 fail=0 skipped=5 nodeWarnings=0 tap=F:/tmp/gate/krate-manager-phase8-close/20261101-090724/A3b-node-test.tap exit=0 |
| A3c | GREEN | 0.1 | L-TAP want "TAP plan=1..59 top-level=59" printed last="TAP plan=1..59 top-level=59" exit=0 |
| A3d | GREEN | 0.1 | L-VLINT stderr=none exit=0 |
| A4 | GREEN | 4.5 | L-YAML .github/workflows/manager-ci.yml exit=0 |
| A5 | GREEN | 0.1 | L-LINT lint-serial-test-runs last="lint-serial-test-runs: clean (64 files)" exit=0 |
| A6 | GREEN | 0.2 | L-INDEX gen-specs-index --check last="specs/INDEX.md is up to date." exit=0 |
| A7a | GREEN | 0.1 | scope-check --self-test last="SELF-TEST GREEN (41 cases)" exit=0 |
| A7b | GREEN | 1.3 | L-SCOPE scope-check last="scope-check: 25 paths (14 under apps/krate-manager/, 11 outside), 1 roadmap hunks, 4 shape-test hunks: OK (base 2499fb13)" exit=0 |
| A9 | GREEN | 0.1 | TA-1 / rule 9 grep (todo!( / unimplemented!( outside tests) exit=1 printed nothing |
| A10 | GREEN | 79.5 | L-WSL last="WSL LANE GREEN (294 passed, 70 s)" info="Linux-run Phase 8 tests seen ok: 5/5" -> specs/krate-manager-phase8/evidence/wsl-core.txt (early signal; closes no PENDING-CI row) exit=0 |
| B1 | GREEN | 0.2 | step required tests ran notices="::notice title=required tests ran (Windows)::259 cargo + 29 vitest" want "::notice title=required tests ran (Windows)::259 cargo + 29 vitest" exit=0 |
| C1 | GREEN | - | git status --porcelain (without specs/krate-manager-phase8/evidence/) equal to A0b (21 lines) file=F:/tmp/gate/krate-manager-phase8-close/20261101-090724/C1-git-status.txt |
| C2 | GREEN | 0.5 | L-SCOPE scope-check last="scope-check: 30 paths (14 under apps/krate-manager/, 16 outside), 1 roadmap hunks, 4 shape-test hunks: OK (base 2499fb13)" exit=0 |
| C3 | GREEN | 0.1 | node tools/clean-test-output.js --apply summary="2 entries, 0.00 GB removed" exit=0 |
