# Krate Manager Phase 7: close-lane wall clocks

Written by `evidence/close-lane.js`, 2026-10-31T22:47:37.931Z. Total wall 157.0s; CLOSE LANE GREEN. Raw logs (removed by C3): `F:/tmp/gate/krate-manager-phase7-close/20261031-224500`.
A1 (git scans, then cargo) and A2 (npm) run as two chains beside A3-A7, A9, A10 (A10 is L-WSL, on WSL's own cores); B starts after A1 and A2; C runs last.

| Row | Result | Wall (s) | Command |
|---|---|---|---|
| A0 | GREEN | 0.1 | rustc="rustc 1.99.1 (58cf8f9c3 2026-10-13)" node="v24.7.0" df(repo root)="F: 50G 44G 6.2G 88% /f" |
| A0b | GREEN | - | git status --porcelain: 36 lines (without specs/krate-manager-phase7/evidence/) file=F:/tmp/gate/krate-manager-phase7-close/20261031-224500/A0b-git-status.txt |
| A0c | GREEN | - | base binary size (TA-11) 7650304 bytes from specs/krate-manager-phase7/evidence/base-binary-size.txt |
| A1a | GREEN | 1.5 | step cargo fmt last="" exit=0 |
| A1b | GREEN | 1.6 | step clippy warning-lines=0 notices="" exit=0 |
| A1c | GREEN | 1.2 | step clippy dev-feed warning-lines=0 notices="::notice title=clippy dev-feed (Windows)::clean" exit=0 |
| A1d | GREEN | 1.7 | step clippy-guard notice="::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1 lint-named=5 other-errors=0" copied to specs/krate-manager-phase7/evidence/clippy-guard.txt exit=0 |
| A1e | GREEN | 26.8 | step cargo test binaries=11 passed=246 binaries-with-failures=0 -> specs/krate-manager-phase7/evidence/cargo-test.txt exit=0 |
| A1f | GREEN | 0.5 | step core dependency tree notices="::notice title=core dependency tree (Windows)::88 crates, none excluded" copied to specs/krate-manager-phase7/evidence/core-tree.txt exit=0 |
| A1h | GREEN | 0.2 | cargo tree -p krate-manager-core -e normal --locked --prefix none --format {p} -> specs/krate-manager-phase7/evidence/core-tree-phase7.txt lines=168 minisign-verify v0.3.0 and sha2 v0.11.1 present, no minisign / rpassword / scrypt exit=0 |
| A1i | GREEN | 18.6 | SC-024 core lib + injection + config_guard "finished in" (quiet re-run after the A lanes): Phase 6 core lib=2.42s injection=6.11s config_guard=0.85s sum=9.38s; Phase 7 core lib=10.24s injection=6.45s config_guard=0.53s sum=17.22s; delta=+7.84s (want < 20 s) (a red is a main-loop decision, plan §8; never automatic) |
| A1j | GREEN | 0.2 | SC-001 (Q-B) git diff -U0 2f3c93d7 -- apps/krate-manager -> specs/krate-manager-phase7/evidence/diff-u0.txt (5625 lines) plus 5 untracked file(s) read whole: no added allow( / expect( attribute, @ts-ignore or eslint-disable exit=0/exit=0 |
| A1k | GREEN | 0.1 | FR-063 git diff --name-only 2f3c93d7 -- apps/krate-manager/src: empty; untracked under apps/krate-manager/src: empty exit=0/exit=0 |
| A2a | GREEN | - | node_modules present: step npm ci not run (tasks.md T009 A2) |
| A2b | GREEN | 4.3 | step typecheck last="> tsc --noEmit -p tsconfig.json && tsc --noEmit -p tsconfig.node.json" exit=0 |
| A2c | GREEN | 5.2 | step lint last="> eslint . --max-warnings 0" exit=0 |
| A2d | GREEN | 3.4 | step vitest last="JSON report written to F:/tmp/gate/krate-manager-phase7-close/20261031-224500/vitest.json" exit=0 |
| A3a | GREEN | - | node --check on 3 files: all ok |
| A3b | GREEN | 23.8 | node --test plan=1..59 tests=752 pass=747 fail=0 skipped=5 nodeWarnings=0 tap=F:/tmp/gate/krate-manager-phase7-close/20261031-224500/A3b-node-test.tap exit=0 |
| A3c | GREEN | 0.1 | L-TAP want "TAP plan=1..59 top-level=59" printed last="TAP plan=1..59 top-level=59" exit=0 |
| A3d | GREEN | 0.1 | L-VLINT stderr=none exit=0 |
| A4 | GREEN | 2.1 | L-YAML .github/workflows/manager-ci.yml exit=0 |
| A5 | GREEN | 0.1 | L-LINT lint-serial-test-runs last="lint-serial-test-runs: clean (64 files)" exit=0 |
| A6 | GREEN | 0.2 | L-INDEX gen-specs-index --check last="specs/INDEX.md is up to date." exit=0 |
| A7a | GREEN | 0.1 | scope-check --self-test last="SELF-TEST GREEN (39 cases)" exit=0 |
| A7b | GREEN | 1.2 | L-SCOPE scope-check last="scope-check: 51 paths (30 under apps/krate-manager/, 21 outside), 0 roadmap hunks, 4 shape-test hunks: OK (base 2f3c93d7)" exit=0 |
| A9 | GREEN | 0.1 | TA-1 grep (todo!( / unimplemented!( outside tests) exit=1 printed nothing |
| A10 | GREEN | 46.3 | L-WSL last="WSL LANE GREEN (260 passed, 40 s)" info="Linux-run Phase 7 tests seen ok: 3/3" -> specs/krate-manager-phase7/evidence/wsl-core.txt (early signal; closes no PENDING-CI row) exit=0 |
| B1 | GREEN | 0.1 | step required tests ran notices="::notice title=required tests ran (Windows)::234 cargo + 29 vitest" want "::notice title=required tests ran (Windows)::234 cargo + 29 vitest" exit=0 |
| B2 | GREEN | 102.5 | step tauri build last="F:\projects\iterum\apps\krate-manager\target\release\bundle\nsis\Krate Manager_0.1.0_x64-setup.exe (2.51 MiB)" exit=0 |
| B3 | GREEN | 0.2 | step build warnings notices="::notice title=build warnings (Windows)::0 warning lines in the release build" want "::notice title=build warnings (Windows)::0 warning lines in the release build" exit=0 |
| B4 | GREEN | 0.1 | step dev-feed absent notices="::notice title=dev-feed absent (Windows)::marker in source=1, in target/release/krate-manager.exe=0" want "::notice title=dev-feed absent (Windows)::" exit=0 |
| B5 | GREEN | 0.1 | step roster absent notices="::notice title=roster absent (Windows)::9 names, 0 hits in 4 files" (want n >= 9 names, 0 hits; a notice means the positive control was found) -> specs/krate-manager-phase7/evidence/roster-absent.txt exit=0 |
| B6 | GREEN | 0.4 | step artifact version notices="::notice title=artifact version (Windows)::0.1.0 (nsis=present)" want "::notice title=artifact version (Windows)::0.1.0 (nsis=present)" exit=0 |
| B7 | GREEN | 0.1 | release binary 7850496 bytes, base 7650304, delta +200192 bytes (2.6 %) -> specs/krate-manager-phase7/evidence/binary-size.txt exit=0 |
| C1 | GREEN | - | git status --porcelain (without specs/krate-manager-phase7/evidence/) equal to A0b (36 lines) file=F:/tmp/gate/krate-manager-phase7-close/20261031-224500/C1-git-status.txt |
| C2 | GREEN | 0.5 | L-SCOPE scope-check last="scope-check: 51 paths (30 under apps/krate-manager/, 21 outside), 0 roadmap hunks, 4 shape-test hunks: OK (base 2f3c93d7)" exit=0 |
| C3 | GREEN | 0.1 | node tools/clean-test-output.js --apply summary="3 entries, 0.00 GB removed" exit=0 |
