# Krate Manager Phase 11: close-lane wall clocks

Written by `evidence/close-lane.js`, 2026-11-02T06:22:06.833Z. Total wall 91.3s; CLOSE LANE GREEN. Raw logs (removed by C3): `F:/tmp/gate/km11-close/20261102-062035`.
D9 (`specs/krate-manager-phase11/evidence/d9-windows.txt`): `D9 WINDOWS LOADS`
G runs first (git listings and file scans). A1 (the cargo lints, then cargo test beside H, L-HARNESS), A2 (npm / node), A3-A7 and A10 (L-WSL, WSL's own target dir) run as concurrent chains; B2 (L-REQ) after A1 and A2; C last.

| Row | Result | Wall (s) | Command |
|---|---|---|---|
| A0 | GREEN | 0.3 | rustc="rustc 1.99.1 (58cf8f9c3 2026-10-13)" node="v24.7.0" df(repo root)="F: 50G 48G 2.5G 96% /f" |
| D9 | GREEN | - | specs/krate-manager-phase11/evidence/d9-windows.txt: loads "D9 WINDOWS LOADS" (TA-22, T014; a LOAD FAILURE is the recorded Windows gap of SC-007, PD-33) |
| A0b | GREEN | - | git status --porcelain: 70 lines (without specs/krate-manager-phase11/evidence/) file=F:/tmp/gate/km11-close/20261102-062035/A0b-git-status.txt |
| G1 | GREEN | - | Rule 9: todo!( / unimplemented!( outside *_tests.rs and tests/ (the harness tree counts) in 23 changed .rs files: none |
| G2 | GREEN | - | Rule 2 / SC-012: added suppressions over the diff from 30878b36 and 43 untracked file(s): none unexpected; the harness build.rs #[expect( x1 (want 1), clippy::disallowed_methods + reason = "a build script reads cargo's CARGO_CFG_TARGET_OS" present |
| G3 | GREEN | - | Rule 6: no --test-threads / --no-file-parallelism in the change set (exempt the ban lists' homes apps/krate-manager/crates/core/tests/injection.rs, tools/feed-contract/tests/manager-ci-shape.test.js, tools/feed-contract/tests/release-manager-shape.test.js): none |
| G4 | GREEN | - | FR-083 / Rule 1: no *.key under apps/krate-manager/ (tracked, untracked or ignored) and no secret-key comment in its changed files: none |
| G5 | GREEN | - | FR-097 / TA-2 git check-attr -a: text: unset for tools/feed-contract/keys/krate-signing.pub and the three rsign fixture files: all four (output: tools/feed-contract/keys/krate-signing.pub: text: unset \| apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/package.bin: text: unset \| apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/package.bin.minisig: text: unset \| apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/rsign.pub: text: unset) |
| G6 | GREEN | - | TA-2: git diff 30878b36 -- .gitattributes adds exactly the two lines: yes |
| G7 | GREEN | - | TA-25: head -1 apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/package.bin.minisig = "untrusted comment: rsign-format fixture (Krate Manager Phase 11)" |
| G8 | GREEN | - | Rule 10: F:/tmp/km11-target gone |
| A1a | GREEN | 1.6 | L-RS cargo fmt --all --check last="" exit=0 |
| A1b | GREEN | 6.5 | L-RS cargo clippy --workspace --all-targets --locked -- -D warnings warning-lines=0 exit=0 |
| A1c | GREEN | 0.9 | L-APP cargo clippy -p krate-manager --all-targets --locked -- -D warnings warning-lines=0 exit=0 |
| A1d | GREEN | 8.2 | L-DEVFEED cargo clippy -p krate-manager --all-targets --features dev-feed --locked -- -D warnings warning-lines=0 exit=0 |
| A1e | GREEN | 4.7 | L-MANUAL cargo clippy -p krate-manager --example manual_elevation --features manual-elevation --locked -- -D warnings warning-lines=0 exit=0 |
| A1f | GREEN | 5.0 | L-E2E KRATE_E2E_FEED_URL=http://127.0.0.1:9/versions.json KRATE_E2E_MARKER_DIR=never-run KRATE_E2E_OLD_VERSION=0.0.0 cargo clippy -p krate-manager --all-targets --features updater-e2e --locked -- -D warnings warning-lines=0 exit=0 |
| A1g | GREEN | 1.2 | L-GUARD step clippy-guard notice="::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0" copied to specs/krate-manager-phase11/evidence/clippy-guard.txt exit=0 |
| A1h | GREEN | 53.6 | L-RS cargo test --workspace --locked binaries=12 passed=411 binaries-with-failures=0 -> specs/krate-manager-phase11/evidence/cargo-test.txt exit=0 |
| H | GREEN | 11.2 | L-HARNESS last="HARNESS LANE GREEN (Windows, 27 ok, 3521 ms)" exit=0 |
| A2a | GREEN | - | node_modules present: npm ci not run |
| A2b | GREEN | 7.0 | L-TS npm run typecheck last="> tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json" exit=0 |
| A2c | GREEN | 14.6 | L-TS npm run lint last="> eslint . --max-warnings 0" exit=0 |
| A2d | GREEN | 3.9 | L-TS npm test -- --reporter=default --reporter=json --outputFile.json=F:/tmp/gate/km11-close/20261102-062035/vitest.json vitest.json success=true passed=71 failed=0 total=71 exit=0 |
| A2e | GREEN | 0.4 | L-NAPP node --test --test-reporter=tap "scripts/*.test.js" fail=0 "bundle sidecar name per host tuple" ok x1, "fixture inno script keeps the plugin directives" ok x1, "write test job prints verbatim windows paths" ok x1, "bundle release mode argv" ok x1, "bundle release asset table" ok x1, "e2e update server serves only its directory" ok x1 (want 1 each) warning-lines=0 tap=F:/tmp/gate/km11-close/20261102-062035/node-tap.txt exit=0 |
| A3a | GREEN | - | node --check on 21 files: all ok |
| A3b | GREEN | - | --self-test of scope-check, wsl-lane, harness-lane, close-lane: all GREEN |
| A3c | GREEN | 25.2 | L-NODE node --test plan=1..64 tests=869 pass=864 fail=0 nodeWarnings=0 tap=F:/tmp/gate/km11-close/20261102-062035/feed-tap.txt exit=0 |
| A3d | GREEN | 0.1 | L-TAP want "TAP plan=1..64 top-level=64" printed last="TAP plan=1..64 top-level=64" exit=0 |
| A3e | GREEN | 0.1 | L-VLINT stderr=none exit=0 |
| A3f | GREEN | 0.1 | node apps/krate-manager/tests/fixtures/updater-refusals/make-rsign-fixture.js --self-test last="make-rsign-fixture self-test: ok" exit=0 |
| A4a | GREEN | 19.3 | L-YAML .github/workflows/release-manager.yml exit=0 |
| A4b | GREEN | 1.6 | L-YAML .github/workflows/manager-ci.yml exit=0 |
| A4c | GREEN | 1.5 | L-YAML .github/workflows/feed-tools.yml exit=0 |
| A5 | GREEN | 0.2 | L-LINT lint-serial-test-runs last="lint-serial-test-runs: clean (64 files)" exit=0 |
| A6 | GREEN | 0.5 | L-INDEX gen-specs-index --check last="specs/INDEX.md is up to date." exit=0 |
| A7 | GREEN | 0.8 | L-SCOPE scope-check last="scope-check: 81 paths (48 under apps/krate-manager/, 33 outside): OK (base 30878b36)" exit=0 |
| A10 | GREEN | 89.2 | L-WSL last="WSL LANE GREEN (437 passed, 81 s)" info="Linux-run Phase 9+10 tests seen ok: 13/13" -> specs/krate-manager-phase11/evidence/wsl-core.txt (early signal; closes no PENDING-CI row) exit=0 |
| B2 | GREEN | 0.1 | L-REQ step required tests ran notices="::notice title=required tests ran (Windows)::372 cargo + 70 vitest + 6 node" want "::notice title=required tests ran (Windows)::372 cargo + 70 vitest + 6 node" exit=0 |
| C1 | GREEN | - | git status --porcelain (without specs/krate-manager-phase11/evidence/) equal to A0b (70 lines; changed: none) |
| C2 | GREEN | 0.6 | L-SCOPE scope-check last="scope-check: 81 paths (48 under apps/krate-manager/, 33 outside): OK (base 30878b36)" exit=0 |
| C3 | GREEN | 0.0 | raw logs F:/tmp/gate/km11-close/20261102-062035 removed |
