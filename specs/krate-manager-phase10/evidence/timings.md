# Krate Manager Phase 10: close-lane wall clocks

Written by `evidence/close-lane.js`, 2026-11-01T19:57:21.863Z. Total wall 225.9s; CLOSE LANE GREEN. Raw logs (removed by C3): `F:/tmp/gate/km10-close/20261101-195336`.
A1 (git scans, then cargo) and A2 (npm / node) run as two chains beside A3-A7 and A10 (L-WSL, on WSL's own target dir); G runs first (file scans) except G5, which reads A1g's log; B1 (L-BUNDLE) starts after A1 (the cargo build lock), B2 (L-REQ) after A1 and A2; C runs last.

| Row | Result | Wall (s) | Command |
|---|---|---|---|
| A0 | GREEN | 0.1 | rustc="rustc 1.99.1 (58cf8f9c3 2026-10-13)" node="v24.7.0" df(repo root)="F: 50G 45G 5.8G 89% /f" |
| A0b | GREEN | - | git status --porcelain: 17 lines (without specs/krate-manager-phase10/evidence/) file=F:/tmp/gate/km10-close/20261101-195336/A0b-git-status.txt |
| A1j | GREEN | 0.2 | G2 / SC-026 git diff -U0 4a8b3e2a -- apps/krate-manager -> specs/krate-manager-phase10/evidence/diff-u0.txt (19133 lines) plus 5 untracked file(s) read whole: no added allow( / expect( attribute, @ts-ignore or eslint-disable exit=0/exit=0 |
| A1k | GREEN | 0.7 | FR-122 git diff --name-only 4a8b3e2a -- plugins dsp cmake extern: empty; untracked under them: empty exit=0/exit=0 |
| A1a | GREEN | 1.9 | cargo fmt --all --check last="" exit=0 |
| A1b | GREEN | 2.7 | cargo clippy --workspace --all-targets --locked -- -D warnings warning-lines=0 exit=0 |
| A1c | GREEN | 1.8 | cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings warning-lines=0 exit=0 |
| A1d | GREEN | 2.6 | cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked -- -D warnings warning-lines=0 exit=0 |
| A1e | GREEN | 1.7 | cargo clippy -p krate-manager --example manual_elevation --features manual-elevation --locked -- -D warnings warning-lines=0 exit=0 |
| A1f | GREEN | 3.1 | L-GUARD step clippy-guard notice="::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0" copied to specs/krate-manager-phase10/evidence/clippy-guard.txt exit=0 |
| A1g | GREEN | 57.4 | cargo test --workspace --locked binaries=12 passed=386 binaries-with-failures=0 -> specs/krate-manager-phase10/evidence/cargo-test.txt exit=0 |
| A1h | GREEN | 19.6 | L-HFEAT cargo test -p krate-manager-helper --features ci-temp-roots --locked binaries=4 passed=47 binaries-with-failures=0 "test cli::tests::ci_roots_file_is_read ... ok" x1 (want 1) -> specs/krate-manager-phase10/evidence/cargo-test-ci-roots.txt exit=0 |
| A2a | GREEN | - | node_modules present: npm ci not run |
| A2b | GREEN | 4.7 | npm run typecheck last="> tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json" exit=0 |
| A2c | GREEN | 9.6 | npm run lint last="> eslint . --max-warnings 0" exit=0 |
| A2d | GREEN | 6.1 | npm test -- --reporter=default --reporter=json --outputFile.json=F:/tmp/gate/km10-close/20261101-195336/vitest.json last="JSON report written to F:/tmp/gate/km10-close/20261101-195336/vitest.json" exit=0 |
| A2e | GREEN | 1.4 | L-NAPP node --test --test-reporter=tap "scripts/*.test.js" fail=0 "bundle sidecar name per host tuple" ok x1, "fixture inno script keeps the plugin directives" ok x1, "write test job prints verbatim windows paths" ok x1 (want 1 each) warning-lines=0 tap=F:/tmp/gate/km10-close/20261101-195336/node-tap.txt exit=0 |
| A3a | GREEN | - | node --check on 9 files: all ok |
| A3b | GREEN | - | --self-test of scope-check, wsl-lane, bundle-lane: all GREEN |
| A3c | GREEN | 37.0 | L-NODE node --test plan=1..59 tests=774 pass=769 fail=0 nodeWarnings=0 tap=F:/tmp/gate/km10-close/20261101-195336/feed-tap.txt exit=0 |
| A3d | GREEN | 0.2 | L-TAP want "TAP plan=1..59 top-level=59" printed last="TAP plan=1..59 top-level=59" exit=0 |
| A3e | GREEN | 0.2 | L-VLINT stderr=none exit=0 |
| A4 | GREEN | 2.5 | L-YAML .github/workflows/manager-ci.yml exit=0 |
| A5 | GREEN | 0.2 | L-LINT lint-serial-test-runs last="lint-serial-test-runs: clean (64 files)" exit=0 |
| A6 | GREEN | 0.4 | L-INDEX current except the uncommitted specs/krate-manager-phase11/ row (3 changed line(s)); specs/INDEX.md kept as committed exit=1 |
| A7 | GREEN | 1.3 | L-SCOPE scope-check last="scope-check: 110 paths (87 under apps/krate-manager/, 23 outside), 7 roadmap hunks: OK (base 4a8b3e2a)" exit=0 |
| G1 | GREEN | - | rule 9: todo!( / unimplemented!( outside *_tests.rs and tests/ in 99 .rs files: none |
| G3 | GREEN | - | no thread-count flag in 190 files (the app, manager-ci.yml, specs/krate-manager-phase10/evidence/*.js; exempt apps/krate-manager/crates/core/tests/injection.rs): none |
| G4 | GREEN | - | M-1 written in (FR-061, FR-062): apps/krate-manager/tests/fixtures/helper/inno-locked/ missing=none; LOCKED_EXIT / LOCKED_LINE homes: apps/krate-manager/crates/helper/src/classify.rs, apps/krate-manager/crates/helper/src/classify_tests.rs (want apps/krate-manager/crates/helper/src/classify.rs, apps/krate-manager/crates/helper/src/classify_tests.rs) |
| G5 | GREEN | - | T020 SC-016 replay tests in A1g's log (want 1 each): classify::tests::recorded_locked_case x1, ops::tests::recorded_locked_case_through_runner x1, install::tests::recorded_locked_case_maps_to_locked x1, install::tests::unrecognised_log_falls_back x1 |
| G6 | GREEN | - | FR-123 needles of T002, one line each in specs/Krate-manager-roadmap.md: "CommandEnv" 1 hit(s); "lipo -create" 1 hit(s); "structural" 1 hit(s); "D19" 1 hit(s); "E_HELPER_JOB_LOCATION" 1 hit(s) |
| A10 | GREEN | 109.4 | L-WSL last="WSL LANE GREEN (412 passed, 101 s)" info="Linux-run Phase 9+10 tests seen ok: 13/13" -> specs/krate-manager-phase10/evidence/wsl-core.txt (early signal; closes no PENDING-CI row) exit=0 |
| B1 | GREEN | 132.2 | L-BUNDLE last="BUNDLE LANE GREEN" rows: L0 GREEN rustc --print host-tuple="x86_64-pc-windows-msvc" (TA-16 expects x86_64-pc-windows-msvc) \| L1 GREEN npm run bundle exit=0 |
| B2 | GREEN | 0.3 | L-REQ step required tests ran notices="::notice title=required tests ran (Windows)::348 cargo + 57 vitest + 3 node" want "::notice title=required tests ran (Windows)::348 cargo + 57 vitest + 3 node" exit=0 |
| C1 | GREEN | - | git status --porcelain (without specs/krate-manager-phase10/evidence/) equal to A0b (17 lines; changed: none) file=F:/tmp/gate/km10-close/20261101-195336/C1-git-status.txt |
| C2 | GREEN | 0.6 | L-SCOPE scope-check last="scope-check: 110 paths (87 under apps/krate-manager/, 23 outside), 7 roadmap hunks: OK (base 4a8b3e2a)" exit=0 |
| C3 | GREEN | 0.1 | node tools/clean-test-output.js --apply summary="14 entries, 0.00 GB removed" exit=0 |
