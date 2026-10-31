# Krate Manager Phase 6: close-lane wall clocks

Written by `evidence/close-lane.js`, 2026-10-31T18:07:45.067Z. Total wall 117.5s; CLOSE LANE GREEN. Raw logs (removed by C3): `F:/tmp/gate/krate-manager-phase6-close/20261031-180547`.
A1 (cargo) and A2 (npm) run as two chains beside A3-A7, A9-A11 (A10 is L-WSL, on WSL's own cores); B starts after A1 and A2; C runs last.

| Row | Result | Wall (s) | Command |
|---|---|---|---|
| A0 | GREEN | 0.1 | rustc="rustc 1.99.1 (58cf8f9c3 2026-10-13)" node="v24.7.0" df(repo root)="F: 50G 41G 9.4G 82% /f" |
| A0b | GREEN | - | git status --porcelain: 78 lines (without specs/krate-manager-phase6/evidence/) file=F:/tmp/gate/krate-manager-phase6-close/20261031-180547/A0b-git-status.txt |
| A0c | GREEN | - | base binary size (TA-13) 4314624 bytes from specs/krate-manager-phase6/evidence/base-binary-size.txt |
| A1a | GREEN | 0.9 | step cargo fmt last="" exit=0 |
| A1b | GREEN | 1.6 | step clippy warning-lines=0 notices="" exit=0 |
| A1c | GREEN | 1.1 | step clippy dev-feed warning-lines=0 notices="::notice title=clippy dev-feed (Windows)::clean" exit=0 |
| A1d | GREEN | 1.5 | step clippy-guard notice="::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0" copied to specs/krate-manager-phase6/evidence/clippy-guard.txt exit=0 |
| A1e | GREEN | 10.8 | step cargo test binaries=10 passed=178 binaries-with-failures=0 -> specs/krate-manager-phase6/evidence/cargo-test.txt exit=0 |
| A1f | GREEN | 0.5 | step core dependency tree notices="::notice title=core dependency tree (Windows)::79 crates, none excluded" copied to specs/krate-manager-phase6/evidence/core-tree.txt exit=0 |
| A1g | GREEN | 0.4 | cargo tree -p krate-manager --locked -e normal --prefix none -> specs/krate-manager-phase6/evidence/tree-app.txt lines=582 exactly one line "tauri-plugin-opener v2.7.2" exit=0 |
| A1h | GREEN | 0.2 | cargo tree -p krate-manager-core --locked -e normal --prefix none -> specs/krate-manager-phase6/evidence/tree-core.txt lines=157 no line naming tauri-plugin-opener exit=0 |
| A2a | GREEN | - | node_modules present: step npm ci not run (tasks.md T010 A2) |
| A2b | GREEN | 4.1 | step typecheck last="> tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json" exit=0 |
| A2c | GREEN | 4.3 | step lint last="> eslint . --max-warnings 0" exit=0 |
| A2d | GREEN | 3.3 | step vitest last="JSON report written to F:/tmp/gate/krate-manager-phase6-close/20261031-180547/vitest.json" exit=0 |
| A2e | GREEN | 1.0 | npm ls @tauri-apps/plugin-opener exit=1 no such package ("(empty)") -> specs/krate-manager-phase6/evidence/npm-ls-opener.txt |
| A3a | GREEN | - | node --check on 3 files: all ok |
| A3b | GREEN | 20.6 | node --test plan=1..59 tests=752 pass=747 fail=0 skipped=5 nodeWarnings=0 tap=F:/tmp/gate/krate-manager-phase6-close/20261031-180547/A3b-node-test.tap exit=0 |
| A3c | GREEN | 0.1 | L-TAP want "TAP plan=1..59 top-level=59" printed last="TAP plan=1..59 top-level=59" exit=0 |
| A3d | GREEN | 0.1 | L-VLINT stderr=none exit=0 |
| A4 | GREEN | 2.0 | L-YAML .github/workflows/manager-ci.yml exit=0 |
| A5 | GREEN | 0.1 | L-LINT lint-serial-test-runs last="lint-serial-test-runs: clean (64 files)" exit=0 |
| A6 | GREEN | 0.2 | L-INDEX gen-specs-index --check last="specs/INDEX.md is up to date." exit=0 |
| A7a | GREEN | 0.1 | scope-check --self-test last="SELF-TEST GREEN (22 cases)" exit=0 |
| A7b | GREEN | 0.9 | L-SCOPE scope-check last="scope-check: 95 paths (70 under apps/krate-manager/, 25 outside), 2 roadmap hunks: OK (base 3efe70de)" exit=0 |
| A9 | GREEN | 0.1 | TA-4 grep (todo!( / unimplemented!( outside tests) exit=1 printed nothing |
| A10 | GREEN | 30.3 | L-WSL last="WSL LANE GREEN (192 passed, 24 s)" info="Linux-run Phase 6 tests seen ok: 6/6" -> specs/krate-manager-phase6/evidence/wsl-core.txt (early signal; closes no PENDING-CI row) exit=0 |
| A11 | GREEN | 0.0 | SC-028 ok  got=1 want=1 "in the plugin's install dir under the compiled-in Windows plug-in root" \| ok  got=1 want=1 "is never uninstalled by the helper" -> specs/krate-manager-phase6/evidence/sc028.txt |
| B1 | GREEN | 0.2 | step required tests ran notices="::notice title=required tests ran (Windows)::172 cargo + 29 vitest" want "::notice title=required tests ran (Windows)::172 cargo + 29 vitest" exit=0 |
| B2 | GREEN | 98.4 | step tauri build last="F:\projects\iterum\apps\krate-manager\target\release\bundle\nsis\Krate Manager_0.1.0_x64-setup.exe (2.44 MiB)" exit=0 |
| B3 | GREEN | 0.2 | step build warnings notices="::notice title=build warnings (Windows)::0 warning lines in the release build" want "::notice title=build warnings (Windows)::0 warning lines in the release build" exit=0 |
| B4 | GREEN | 0.1 | step dev-feed absent notices="::notice title=dev-feed absent (Windows)::marker in source=1, in target/release/krate-manager.exe=0" want "::notice title=dev-feed absent (Windows)::" exit=0 |
| B5 | GREEN | 0.1 | step roster absent notices="::notice title=roster absent (Windows)::9 names, 0 hits in 4 files" (want n >= 9 names, 0 hits; a notice means the positive control was found) -> specs/krate-manager-phase6/evidence/roster-absent.txt exit=0 |
| B6 | GREEN | 0.4 | step artifact version notices="::notice title=artifact version (Windows)::0.1.0 (nsis=present)" want "::notice title=artifact version (Windows)::0.1.0 (nsis=present)" exit=0 |
| B7 | GREEN | 0.1 | release binary 7650304 bytes, base 4314624, delta +3335680 bytes (77.3 %) -> specs/krate-manager-phase6/evidence/binary-size.txt exit=0 |
| C1 | GREEN | - | git status --porcelain (without specs/krate-manager-phase6/evidence/) equal to A0b (78 lines) file=F:/tmp/gate/krate-manager-phase6-close/20261031-180547/C1-git-status.txt |
| C2 | GREEN | 0.4 | L-SCOPE scope-check last="scope-check: 95 paths (70 under apps/krate-manager/, 25 outside), 2 roadmap hunks: OK (base 3efe70de)" exit=0 |
| C3 | GREEN | 0.1 | node tools/clean-test-output.js --apply summary="3 entries, 0.00 GB removed" exit=0 |
