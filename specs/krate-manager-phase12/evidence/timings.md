# Krate Manager Phase 12: close-lane wall clocks

Written by `evidence/close-lane.js`, 2026-11-02T10:40:06.808Z. Total wall 125.1s; CLOSE LANE GREEN. Raw logs (removed by C3): `F:/tmp/gate/km12-close/20261102-103801`.
G runs first (git listings and file scans). A1 (the cargo lints, then cargo test beside H, L-HARNESS), A2 (npm / node), A3-A8 and A10 (L-WSL, WSL's own target dir) run as concurrent chains; B2 (L-REQ) after A1 and A2; C last.

| Row | Result | Wall (s) | Command |
|---|---|---|---|
| A0 | GREEN | 0.4 | rustc="rustc 1.99.1 (58cf8f9c3 2026-10-13)" node="v24.7.0" df(repo root)="F: 50G 37G 14G 74% /f" |
| A0b | GREEN | - | git status --porcelain: 99 lines (without specs/krate-manager-phase12/evidence/) file=F:/tmp/gate/km12-close/20261102-103801/A0b-git-status.txt |
| G1 | GREEN | - | todo!( / unimplemented!( outside *_tests.rs and tests/ in 20 changed .rs files: none |
| G2 | GREEN | - | Rule 2: added #[allow( / #[expect( / @ts-ignore / @ts-nocheck / eslint-disable over the diff from 7c4b6837 and 50 untracked file(s): none |
| G3 | GREEN | - | Rule 6: no --test-threads / --no-file-parallelism / --test-concurrency=1 / --test-concurrency 1 in the change set (exempt the ban lists' homes apps/krate-manager/crates/core/tests/injection.rs, tools/feed-contract/tests/manager-ci-shape.test.js, tools/feed-contract/tests/release-manager-shape.test.js, tools/feed-contract/tests/hygiene.test.js): none |
| G4 | GREEN | - | T001 step 14: git check-attr eol tools/gen-root-index.js -> "tools/gen-root-index.js: eol: lf" (want "tools/gen-root-index.js: eol: lf") |
| G5 | GREEN | - | Rule 1: no file named *.key under apps/krate-manager/ (tracked, untracked or ignored): none |
| A1a | GREEN | 3.1 | L-RS cargo fmt --all --check last="" exit=0 |
| A1b | GREEN | 4.4 | L-RS cargo clippy --workspace --all-targets --locked -- -D warnings warning-lines=0 exit=0 |
| A1c | GREEN | 1.2 | L-APP cargo clippy -p krate-manager --all-targets --locked -- -D warnings warning-lines=0 exit=0 |
| A1d | GREEN | 1.1 | L-DEVFEED cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings warning-lines=0 exit=0 |
| A1e | GREEN | 1.3 | L-MANUAL cargo clippy -p krate-manager --example manual_elevation --features manual-elevation --locked -- -D warnings warning-lines=0 exit=0 |
| A1f | GREEN | 1.2 | L-E2E KRATE_E2E_FEED_URL=http://127.0.0.1:9/versions.json KRATE_E2E_MARKER_DIR=never-run KRATE_E2E_OLD_VERSION=0.0.0 cargo clippy -p krate-manager --all-targets --features updater-e2e --locked -- -D warnings warning-lines=0 exit=0 |
| A1g | GREEN | 2.1 | L-GUARD step clippy-guard notice="::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0" error-lines=0 (want other-errors=0, no ::error) copied to specs/krate-manager-phase12/evidence/clippy-guard.txt exit=0 |
| A1h | GREEN | 81.6 | L-RS cargo test --workspace --locked binaries=12 passed=445 binaries-with-failures=0 -> specs/krate-manager-phase12/evidence/cargo-test.txt exit=0 |
| H | GREEN | 18.4 | L-HARNESS last="HARNESS LANE GREEN (Windows, 27 ok, 2953 ms)" exit=0 |
| A2a | GREEN | - | node_modules present: npm ci not run |
| A2b | GREEN | 7.2 | L-TS npm run typecheck last="> tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json" exit=0 |
| A2c | GREEN | 13.9 | L-TS npm run lint last="> eslint . --max-warnings 0" exit=0 |
| A2d | GREEN | 9.1 | L-TS npm test -- --reporter=default --reporter=json --outputFile.json=F:/tmp/gate/km12-close/20261102-103801/vitest.json vitest.json success=true passed=87 failed=0 total=87 exit=0 |
| A2e | GREEN | 0.6 | L-NAPP node --test --test-reporter=tap "scripts/*.test.js" fail=0 "bundle sidecar name per host tuple" ok x1, "fixture inno script keeps the plugin directives" ok x1, "write test job prints verbatim windows paths" ok x1, "bundle release mode argv" ok x1, "bundle release asset table" ok x1, "e2e update server serves only its directory" ok x1 (want 1 each) warning-lines=0 tap=F:/tmp/gate/km12-close/20261102-103801/node-tap.txt exit=0 |
| A3a | GREEN | - | node --check on 15 files (the change set's .js and the four evidence scripts): all ok |
| A3b | GREEN | - | --self-test of scope-check, wsl-lane, close-lane, rustsec-check: all GREEN |
| A3c | GREEN | 44.6 | L-NODE node --test plan=1..69 tests=937 pass=932 fail=0 nodeWarnings=0 want plan = feed-tools.yml PLAN=1..69 tap=F:/tmp/gate/km12-close/20261102-103801/feed-tap.txt exit=0 |
| A3d | GREEN | 0.1 | L-TAP want "TAP plan=1..69 top-level=69" printed last="TAP plan=1..69 top-level=69" exit=0 |
| A3e | GREEN | 0.1 | L-VLINT stderr=none exit=0 |
| A4a | GREEN | 3.5 | L-YAML .github/workflows/release.yml exit=0 |
| A4b | GREEN | 2.6 | L-YAML .github/workflows/docs.yml exit=0 |
| A4c | GREEN | 2.0 | L-YAML .github/workflows/release-manager.yml exit=0 |
| A4d | GREEN | 1.9 | L-YAML .github/workflows/feed-tools.yml exit=0 |
| A5 | GREEN | 0.2 | L-LINT lint-serial-test-runs last="lint-serial-test-runs: clean (64 files)" exit=0 |
| A6 | GREEN | 0.4 | L-INDEX gen-specs-index --check last="specs/INDEX.md is up to date." exit=0 |
| A7 | GREEN | 2.4 | L-SCOPE scope-check last="scope-check: 104 paths (46 under apps/krate-manager/, 58 outside): OK (base 7c4b6837)" exit=0 |
| A8 | GREEN | 0.2 | FR-111 / TA-17 node tools/header-consumers.js --tag '[krate-manager]' last="CONSUMERS OUTSIDE [krate-manager]: none" (want "CONSUMERS OUTSIDE [krate-manager]: none") exit=0 |
| A10 | GREEN | 122.7 | L-WSL last="WSL LANE GREEN (471 passed, 112 s)" info="Linux-run Phase 9+10 tests seen ok: 13/13 \| Phase 12 required cargo tests seen ok: 34/34" -> specs/krate-manager-phase12/evidence/wsl-core.txt (early signal; closes no PENDING-CI row) exit=0 |
| B2 | GREEN | 0.3 | L-REQ step required tests ran notices="::notice title=required tests ran (Windows)::406 cargo + 86 vitest + 6 node" want "::notice title=required tests ran (Windows)::406 cargo + 86 vitest + 6 node" exit=0 |
| C1 | GREEN | - | git status --porcelain (without specs/krate-manager-phase12/evidence/) equal to A0b (99 lines; changed: none) |
| C3 | GREEN | 0.0 | raw logs F:/tmp/gate/km12-close/20261102-103801 removed |
