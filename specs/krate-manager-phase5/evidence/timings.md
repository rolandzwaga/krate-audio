# Krate Manager Phase 5: close-lane wall clocks

Written by `evidence/close-lane.js`, 2026-10-31T14:22:11.408Z. Total wall 414.6s; CLOSE LANE GREEN. Raw logs (cleaned at close): `F:/tmp/gate/krate-manager-phase5-close/20261031-141516`.
A1 (cargo) and A2 (npm) run as two chains beside A3-A7, A9-A12 (A11 is L-WSL, on WSL's own 4 cores); A8 starts after A1; B starts after A1 and A2.

| Row | Result | Wall (s) | Command |
|---|---|---|---|
| A0 | GREEN | 0.4 | rustc="rustc 1.99.1 (58cf8f9c3 2026-10-13)" node="v24.7.0" df(repo root)="F: 50G 38G 13G 76% /f" |
| A0b | GREEN | - | git status --porcelain: 43 lines (without specs/krate-manager-phase5/evidence/) file=F:/tmp/gate/krate-manager-phase5-close/20261031-141516/A0b-git-status.txt |
| A0c | GREEN | - | base binary size (TA-24) 4269056 bytes from specs/krate-manager-phase5/evidence/base-binary-size.txt |
| A1a | GREEN | 3.1 | step cargo fmt last="" exit=0 |
| A1b | GREEN | 8.2 | step clippy warning-lines=0 notices="" exit=0 |
| A1c | GREEN | 12.3 | step clippy dev-feed warning-lines=0 notices="::notice title=clippy dev-feed (Windows)::clean" exit=0 |
| A1d | GREEN | 2.4 | L-LIVE clippy --features live-network warning-lines=0 exit=0 |
| A1e | GREEN | 10.4 | step clippy-guard notice="::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0" copied to specs/krate-manager-phase5/evidence/clippy-guard.txt exit=0 |
| A1f | GREEN | 21.8 | step cargo test binaries=10 passed=137 binaries-with-failures=0 -> specs/krate-manager-phase5/evidence/cargo-test.txt exit=0 |
| A1g | GREEN | 14.7 | cargo test --list (default) tests=136 lines naming live_feed_parses=0 (want 0) -> specs/krate-manager-phase5/evidence/list-default.txt exit=0 |
| A1h | GREEN | 1.9 | cargo test --features live-network --test live_feed --list --ignored lines naming live_feed_parses=1 (want 1) -> specs/krate-manager-phase5/evidence/list-live.txt exit=0 |
| A1i | GREEN | 0.7 | cargo tree -p krate-manager-core -e normal -> specs/krate-manager-phase5/evidence/tree-core.txt lines=153 checks ok exit=0 |
| A1i | GREEN | 0.7 | cargo tree -p krate-manager-core -e normal --target x86_64-pc-windows-msvc -> specs/krate-manager-phase5/evidence/tree-core-windows.txt lines=153 checks ok exit=0 |
| A1i | GREEN | 0.9 | cargo tree -p krate-manager-core -e normal --target x86_64-unknown-linux-gnu -> specs/krate-manager-phase5/evidence/tree-core-linux.txt lines=166 checks ok exit=0 |
| A1i | GREEN | 6.6 | cargo tree -p krate-manager -e normal --target all -> specs/krate-manager-phase5/evidence/tree-app.txt lines=1223 checks ok exit=0 |
| A1i | GREEN | 0.6 | cargo tree -p krate-manager-core -e features -i rquickjs --target x86_64-unknown-linux-gnu -> specs/krate-manager-phase5/evidence/tree-rquickjs-features-linux.txt lines=6 checks ok exit=0 |
| A1i | GREEN | 0.7 | cargo tree -p krate-manager-core -e features -i rquickjs --target aarch64-apple-darwin -> specs/krate-manager-phase5/evidence/tree-rquickjs-features-macos.txt lines=6 checks ok exit=0 |
| A1i | GREEN | 0.6 | cargo tree -p krate-manager-core -e features -i webpki-roots -> specs/krate-manager-phase5/evidence/tree-webpki.txt lines=23 checks ok exit=0 |
| A1i | GREEN | 1.1 | cargo tree --workspace -d --target all -> specs/krate-manager-phase5/evidence/tree-dups.txt lines=1026 checks ok exit=0 |
| A1i | GREEN | 1.2 | cargo tree --workspace -i core-foundation --target all -> specs/krate-manager-phase5/evidence/tree-core-foundation.txt lines=23 checks ok exit=0 |
| A2a | GREEN | - | node_modules present: step npm ci not run (tasks.md T016 A2) |
| A2b | GREEN | 24.9 | step typecheck last="> tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json" exit=0 |
| A2c | GREEN | 18.6 | step lint last="> eslint . --max-warnings 0" exit=0 |
| A2d | GREEN | 5.6 | step vitest last="JSON report written to F:/tmp/gate/krate-manager-phase5-close/20261031-141516/vitest.json" exit=0 |
| A3a | GREEN | - | node --check on 5 files: all ok |
| A3b | GREEN | 92.3 | node --test plan=1..59 tests=752 pass=747 fail=0 skipped=5 nodeWarnings=0 tap=F:/tmp/gate/krate-manager-phase5-close/20261031-141516/A3b-node-test.tap exit=0 |
| A3c | GREEN | 0.2 | L-TAP want "TAP plan=1..59 top-level=59" printed last="TAP plan=1..59 top-level=59" exit=0 |
| A3d | GREEN | 0.2 | L-VLINT stderr=none exit=0 |
| A3e | GREEN | 0.5 | model-range-node (SC-037 Node parity) rows=6 findings=0 on 6/6 -> specs/krate-manager-phase5/evidence/model-range-node.txt exit=0 |
| A4 | GREEN | 9.7 | L-YAML .github/workflows/manager-ci.yml exit=0 |
| A4 | GREEN | 9.3 | L-YAML .github/workflows/feed-tools.yml exit=0 |
| A5 | GREEN | 0.5 | L-LINT lint-serial-test-runs last="lint-serial-test-runs: clean (64 files)" exit=0 |
| A6 | GREEN | 0.9 | L-INDEX gen-specs-index --check last="specs/INDEX.md is up to date." exit=0 |
| A7a | GREEN | 0.4 | scope-check --self-test last="SELF-TEST GREEN (21 cases)" exit=0 |
| A7b | GREEN | 9.5 | L-SCOPE scope-check last="scope-check: 17 paths, 3 roadmap hunks, specs/krate-manager/probes/results.md 1 0: OK (base a145271b)" exit=0 |
| A8 | GREEN | 3.1 | L-XCHECK x86_64-unknown-linux-gnu SKIPPED (TA-7 build-script skip, verbatim: "warning: rquickjs-sys@0.14.0: Compiler family detection failed due to error: ToolNotFound: failed to find tool "x86_64-linux-gnu-gcc": program not found (see https://docs.rs/cc/latest/cc/#compile-time-requirements for help)"; error: failed to run custom build command for `rquickjs-sys v0.14.0`; error: failed to run custom build command for `ring v0.17.14`) exit=101 |
| A8 | GREEN | 7.0 | L-XCHECK aarch64-apple-darwin SKIPPED (TA-7 build-script skip, verbatim: "warning: ring@0.17.14: Compiler family detection failed due to error: ToolNotFound: failed to find tool "cc": program not found (see https://docs.rs/cc/latest/cc/#compile-time-requirements for help)"; error: failed to run custom build command for `ring v0.17.14`; error: failed to run custom build command for `rquickjs-sys v0.14.0`) exit=101 |
| A9 | GREEN | 0.5 | SC-021 grep exit=1 printed nothing |
| A10 | GREEN | 0.5 | TA-4 grep (todo!( / unimplemented!( outside tests) exit=1 printed nothing |
| A11 | GREEN | 158.6 | L-WSL last="WSL LANE GREEN (150 passed, 148 s)" info="cfg(not(windows)) tests seen ok: 13/13" -> specs/krate-manager-phase5/evidence/wsl-core.txt (early signal; closes no PENDING-CI row) exit=0 |
| A12a | GREEN | 0.4 | SC-044 rustsec.org lines=1 (want 1) missing=none -> specs/krate-manager-phase5/evidence/rustsec-line.txt exit=0 |
| A12b | GREEN | 0.2 | git diff --numstat a145271b -- specs/krate-manager/probes/results.md = "1 0" (want "1 0") exit=0 |
| B1 | GREEN | 0.3 | step required tests ran notices="::notice title=required tests ran (Windows)::132 cargo + 3 vitest" want "::notice title=required tests ran (Windows)::132 cargo + 3 vitest" exit=0 |
| B2 | GREEN | 318.0 | step tauri build last="F:\projects\iterum\apps\krate-manager\target\release\bundle\nsis\Krate Manager_0.1.0_x64-setup.exe (1.32 MiB)" exit=0 |
| B3 | GREEN | 0.4 | step build warnings notices="::notice title=build warnings (Windows)::0 warning lines in the release build" want "::notice title=build warnings (Windows)::0 warning lines in the release build" exit=0 |
| B4 | GREEN | 0.3 | step dev-feed absent notices="::notice title=dev-feed absent (Windows)::marker in source=1, in target/release/krate-manager.exe=0" want "::notice title=dev-feed absent (Windows)::" exit=0 |
| B5 | GREEN | 0.3 | step roster absent notices="::notice title=roster absent (Windows)::9 names, 0 hits in 4 files" (want n >= 9 names, 0 hits; a notice means the positive control was found) -> specs/krate-manager-phase5/evidence/roster-absent.txt exit=0 |
| B6 | GREEN | 1.6 | step artifact version notices="::notice title=artifact version (Windows)::0.1.0 (nsis=present)" want "::notice title=artifact version (Windows)::0.1.0 (nsis=present)" exit=0 |
| B7 | GREEN | 0.2 | release binary 4314624 bytes, base 4269056, delta +45568 bytes (1.1 %) -> specs/krate-manager-phase5/evidence/binary-size.txt exit=0 |
| C1 | GREEN | - | git status --porcelain (without specs/krate-manager-phase5/evidence/) equal to A0b (43 lines) file=F:/tmp/gate/krate-manager-phase5-close/20261031-141516/C1-git-status.txt |
| C2 | GREEN | 1.1 | L-SCOPE scope-check last="scope-check: 32 paths, 3 roadmap hunks, specs/krate-manager/probes/results.md 1 0: OK (base a145271b)" exit=0 |
