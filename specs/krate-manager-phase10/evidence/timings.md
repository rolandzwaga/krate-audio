# Krate Manager Phase 10: close-lane wall clocks

Written by `evidence/close-lane.js`, 2026-11-01T18:18:37.461Z. Total wall 203.6s; CLOSE LANE GREEN. Raw logs (removed by C3): `F:/tmp/gate/km10-close/20261101-181513`.
A1 (git scans, then cargo) and A2 (npm / node) run as two chains beside A3-A7 and A10 (L-WSL, on WSL's own target dir); G runs first (file scans) except G5, which reads A1g's log; B1 (L-BUNDLE) starts after A1 (the cargo build lock), B2 (L-REQ) after A1 and A2; C runs last.

| Row | Result | Wall (s) | Command |
|---|---|---|---|
| A0 | GREEN | 0.1 | rustc="rustc 1.99.1 (58cf8f9c3 2026-10-13)" node="v24.7.0" df(repo root)="F: 50G 45G 5.8G 89% /f" |
| A0b | GREEN | - | git status --porcelain: 91 lines (without specs/krate-manager-phase10/evidence/) file=F:/tmp/gate/km10-close/20261101-181513/A0b-git-status.txt |
| A1j | GREEN | 0.2 | G2 / SC-026 git diff -U0 4a8b3e2a -- apps/krate-manager -> specs/krate-manager-phase10/evidence/diff-u0.txt (6799 lines) plus 41 untracked file(s) read whole: no added allow( / expect( attribute, @ts-ignore or eslint-disable exit=0/exit=0 |
| A1k | GREEN | 0.7 | FR-122 git diff --name-only 4a8b3e2a -- plugins dsp cmake extern: empty; untracked under them: empty exit=0/exit=0 |
| A1a | GREEN | 2.0 | cargo fmt --all --check last="" exit=0 |
| A1b | GREEN | 3.0 | cargo clippy --workspace --all-targets --locked -- -D warnings warning-lines=0 exit=0 |
| A1c | GREEN | 1.8 | cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings warning-lines=0 exit=0 |
| A1d | GREEN | 1.2 | cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked -- -D warnings warning-lines=0 exit=0 |
| A1e | GREEN | 1.5 | cargo clippy -p krate-manager --example manual_elevation --features manual-elevation --locked -- -D warnings warning-lines=0 exit=0 |
| A1f | GREEN | 2.6 | L-GUARD step clippy-guard notice="::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0" copied to specs/krate-manager-phase10/evidence/clippy-guard.txt exit=0 |
| A1g | GREEN | 63.9 | cargo test --workspace --locked binaries=12 passed=383 binaries-with-failures=0 -> specs/krate-manager-phase10/evidence/cargo-test.txt exit=0 |
| A1h | GREEN | 8.3 | L-HFEAT cargo test -p krate-manager-helper --features ci-temp-roots --locked binaries=4 passed=46 binaries-with-failures=0 "test cli::tests::ci_roots_file_is_read ... ok" x1 (want 1) -> specs/krate-manager-phase10/evidence/cargo-test-ci-roots.txt exit=0 |
| A2a | GREEN | - | node_modules present: npm ci not run |
| A2b | GREEN | 4.9 | npm run typecheck last="> tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json" exit=0 |
| A2c | GREEN | 9.6 | npm run lint last="> eslint . --max-warnings 0" exit=0 |
| A2d | GREEN | 7.0 | npm test -- --reporter=default --reporter=json --outputFile.json=F:/tmp/gate/km10-close/20261101-181513/vitest.json last="JSON report written to F:/tmp/gate/km10-close/20261101-181513/vitest.json" exit=0 |
| A2e | GREEN | 0.7 | L-NAPP node --test --test-reporter=tap "scripts/*.test.js" fail=0 "bundle sidecar name per host tuple" ok x1, "fixture inno script keeps the plugin directives" ok x1, "write test job prints verbatim windows paths" ok x1 (want 1 each) warning-lines=0 tap=F:/tmp/gate/km10-close/20261101-181513/node-tap.txt exit=0 |
| A3a | GREEN | - | node --check on 9 files: all ok |
| A3b | GREEN | - | --self-test of scope-check, wsl-lane, bundle-lane: all GREEN |
| A3c | GREEN | 38.6 | L-NODE node --test plan=1..59 tests=773 pass=768 fail=0 nodeWarnings=0 tap=F:/tmp/gate/km10-close/20261101-181513/feed-tap.txt exit=0 |
| A3d | GREEN | 0.2 | L-TAP want "TAP plan=1..59 top-level=59" printed last="TAP plan=1..59 top-level=59" exit=0 |
| A3e | GREEN | 0.2 | L-VLINT stderr=none exit=0 |
| A4 | GREEN | 2.5 | L-YAML .github/workflows/manager-ci.yml exit=0 |
| A5 | GREEN | 0.2 | L-LINT lint-serial-test-runs last="lint-serial-test-runs: clean (64 files)" exit=0 |
| A6 | GREEN | 0.4 | L-INDEX gen-specs-index --check last="specs/INDEX.md is up to date." exit=0 |
| A7 | GREEN | 1.3 | L-SCOPE scope-check last="scope-check: 102 paths (82 under apps/krate-manager/, 20 outside), 7 roadmap hunks: OK (base 4a8b3e2a)" exit=0 |
| G1 | GREEN | - | rule 9: todo!( / unimplemented!( outside *_tests.rs and tests/ in 99 .rs files: none |
| G3 | GREEN | - | no thread-count flag in 186 files (the app, manager-ci.yml, specs/krate-manager-phase10/evidence/*.js; exempt apps/krate-manager/crates/core/tests/injection.rs): none |
| G4 | GREEN | - | M-1 fence (SC-032): apps/krate-manager/tests/fixtures/helper/inno-locked/ absent; LOCKED_EXIT / LOCKED_LINE words: none |
| G5 | GREEN | - | T019 step 6: "test classify::tests::provisional_rule ... ok" 1 time(s) in A1g's log (want 1) |
| G6 | GREEN | - | FR-123 needles of T002, one line each in specs/Krate-manager-roadmap.md: "CommandEnv" 1 hit(s); "lipo -create" 1 hit(s); "structural" 1 hit(s); "D19" 1 hit(s); "E_HELPER_JOB_LOCATION" 1 hit(s) |
| A10 | GREEN | 91.8 | L-WSL last="WSL LANE GREEN (409 passed, 83 s)" info="Linux-run Phase 9+10 tests seen ok: 13/13" -> specs/krate-manager-phase10/evidence/wsl-core.txt (early signal; closes no PENDING-CI row) exit=0 |
| B1 | GREEN | 116.4 | L-BUNDLE last="BUNDLE LANE GREEN" rows: L0 GREEN rustc --print host-tuple="x86_64-pc-windows-msvc" (TA-16 expects x86_64-pc-windows-msvc) \| L1 GREEN npm run bundle exit=0 |
| B2 | GREEN | 0.2 | L-REQ step required tests ran notices="::notice title=required tests ran (Windows)::345 cargo + 57 vitest + 3 node" want "::notice title=required tests ran (Windows)::345 cargo + 57 vitest + 3 node" exit=0 |
| C1 | GREEN | - | git status --porcelain (without specs/krate-manager-phase10/evidence/) equal to A0b (91 lines; changed: none) file=F:/tmp/gate/km10-close/20261101-181513/C1-git-status.txt |
| C2 | GREEN | 0.6 | L-SCOPE scope-check last="scope-check: 102 paths (82 under apps/krate-manager/, 20 outside), 7 roadmap hunks: OK (base 4a8b3e2a)" exit=0 |
| C3 | GREEN | 0.1 | node tools/clean-test-output.js --apply summary="1 entries, 0.00 GB removed" exit=0 |
