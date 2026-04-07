# Tasks: Analysis Feedback Loop (Innexus Spec B)

**Input**: Design documents from `specs/123-analysis-feedback-loop/`
**Prerequisites**: plan.md (required), spec.md (required for user stories)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1–US5)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Foundational — Parameter IDs and Processor Members)

**Purpose**: Add the two new parameter IDs and Processor member declarations. Everything else depends on these.

**Why foundational**: `plugin_ids.h` must exist before any test or implementation file can reference `kAnalysisFeedbackId` or `kAnalysisFeedbackDecayId`. The atomic members must exist before the process loop can use them.

- [X] T001 Add `kAnalysisFeedbackId = 710` and `kAnalysisFeedbackDecayId = 711` to the `ParameterIds` enum in `plugins/innexus/src/plugin_ids.h` (after `kEntropyId = 703`, per plan.md §1)
- [X] T002 Add `std::atomic<float> feedbackAmount_{0.0f}` and `std::atomic<float> feedbackDecay_{0.2f}` atomics to `Processor` in `plugins/innexus/src/processor/processor.h` (FR-007, FR-008)
- [X] T003 Add `std::array<float, 8192> feedbackBuffer_{}` and `bool previousFreezeForFeedback_ = false` to `Processor` in `plugins/innexus/src/processor/processor.h` (FR-005, FR-006, FR-016)
- [X] T004 Add read-only test accessors `getFeedbackAmount()` and `getFeedbackDecay()` to `Processor` in `plugins/innexus/src/processor/processor.h`
- [X] T005 Add `case kAnalysisFeedbackId` and `case kAnalysisFeedbackDecayId` handling (clamped store to atomics) in `processParameterChanges()` in `plugins/innexus/src/processor/processor.cpp` (FR-007, FR-008)
- [X] T006 [P] Register `RangeParameter` for `kAnalysisFeedbackId` (range 0.0–1.0, default 0.0) and `kAnalysisFeedbackDecayId` (range 0.0–1.0, default 0.2) in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp` (FR-007, FR-008)
- [X] T007 Build Release to verify no compilation errors or warnings before writing any tests: `cmake --build build/windows-x64-release --config Release --target innexus`

**Checkpoint**: Parameter IDs and processor members exist — test files can now be created.

---

## Phase 2: State Persistence (User Story: State Version Bump — US-State)

**Purpose**: Bump state version from 7 to 8, persist both new parameters, handle backwards compatibility, and add VST state round-trip tests.

**Independent Test**: `plugins/innexus/tests/unit/vst/test_state_v8.cpp` — verify getState/setState round-trip for both new parameters and version < 8 defaults.

### 2.1 Write Failing Tests First

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T008 Create `plugins/innexus/tests/unit/vst/test_state_v8.cpp` with test cases for: (a) getState writes version 8, (b) setState round-trips FeedbackAmount and FeedbackDecay, (c) setState with version 7 blob defaults FeedbackAmount to 0.0 and FeedbackDecay to 0.2 (FR-017, FR-020)
- [X] T009 Build and confirm T008 tests FAIL (state version still 7, parameters not yet persisted)

### 2.2 Implementation

- [X] T010 Update `getState()` in `plugins/innexus/src/processor/processor.cpp` to write version 8 and append FeedbackAmount and FeedbackDecay floats after Spec A parameters (FR-017, FR-020)
- [X] T011 Update `setState()` in `plugins/innexus/src/processor/processor.cpp` to default both params when `version < 8`, then read them when `version >= 8` with clamping (FR-017, FR-020)
- [X] T012 Update `setComponentState()` in `plugins/innexus/src/controller/controller.cpp` to mirror the same version 8 logic so the controller receives correct values from saved state, including value clamping to [0.0, 1.0] when reading both `kAnalysisFeedbackId` and `kAnalysisFeedbackDecayId` (FR-017, FR-020)

### 2.3 Verify and Cross-Platform Check

- [X] T013 Build Release and run innexus_tests to confirm T008 tests now pass: `cmake --build build/windows-x64-release --config Release --target innexus_tests`
- [X] T014 Verify IEEE 754 compliance: check `test_state_v8.cpp` for any `std::isnan`/`std::isfinite` usage and add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if present

### 2.4 Commit

- [X] T015 **Commit completed state v8 work** (T001–T014) to branch `123-analysis-feedback-loop`

**Checkpoint**: State persistence fully functional and tested. Presets from version < 8 behave identically to before (FeedbackAmount=0.0, FeedbackDecay=0.2).

---

## Phase 3: User Story 1 — Basic Feedback Loop (Priority: P1)

**Goal**: Feed the synth's previous block output back into the analysis pipeline input via per-sample soft-limited mixing, with an early-out when feedback amount is zero. Establishes the core signal flow for self-evolving timbres.

**Independent Test**: With feedback=0.0 output is bit-identical to baseline (SC-001). With feedback=1.0 and silent sidechain input, output converges/decays rather than diverging (SC-002). Output never exceeds the hard clamp ceiling (SC-004).

### 3.1 Write Failing Tests First

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T016 Create `plugins/innexus/tests/integration/test_analysis_feedback.cpp` with test fixture that instantiates the Processor in sidechain mode
- [X] T017 [US1] Write SC-001 test: FeedbackAmount=0.0 produces output bit-identical to no-feedback baseline in `plugins/innexus/tests/integration/test_analysis_feedback.cpp` (FR-019)
- [X] T018 [US1] Write SC-002 test: FeedbackAmount=1.0 with silent sidechain input, measure output RMS at 1-second intervals over 10 seconds and verify no interval's RMS exceeds the t=0 RMS by more than 3dB in `plugins/innexus/tests/integration/test_analysis_feedback.cpp`
- [X] T019 [US1] Write SC-004 test: Output samples never exceed `HarmonicOscillatorBank::kOutputClamp` (2.0f) for any combination of FeedbackAmount/FeedbackDecay in `plugins/innexus/tests/integration/test_analysis_feedback.cpp`
- [X] T020 Build and confirm T017–T019 tests FAIL

### 3.2 Implementation

- [X] T021 [US1] Add feedback buffer clear in `setActive()` in `plugins/innexus/src/processor/processor.cpp`: `feedbackBuffer_.fill(0.0f)` (FR-005, FR-018)
- [X] T022 [US1] Add feedback mixing between the sidechain stereo-to-mono downmix and `pushSamples()` call in `plugins/innexus/src/processor/processor.cpp`: read `feedbackAmount_`, early-out when 0.0f, copy raw bus pointer to `sidechainBuffer_` if needed, per-sample loop applying `fbSample = tanh(feedbackBuffer_[s] * fbAmount * 2.0f) * 0.5f` and `sidechainBuffer_[s] = sidechainBuffer_[s] * (1.0f - fbAmount) + fbSample` (FR-001, FR-003, FR-009, FR-014)
- [X] T023 [US1] Add feedback capture after the per-sample output loop in `plugins/innexus/src/processor/processor.cpp`: in sidechain mode only, capture `(out[0][s] + out[1][s]) * 0.5f` (or mono if single channel) into `feedbackBuffer_` (FR-002, FR-006, FR-014)
- [X] T024 [US1] Add per-block exponential decay after capture in `plugins/innexus/src/processor/processor.cpp`: `decayCoeff = exp(-decayAmount * blockSize / sampleRate)`, multiply all `feedbackBuffer_` samples by `decayCoeff` (FR-013)

### 3.3 Verify

- [X] T025 [US1] Build Release and run innexus_tests to confirm SC-001, SC-002, SC-004 tests now pass
- [X] T026 [US1] Verify IEEE 754 compliance: check `test_analysis_feedback.cpp` for any `std::isnan`/`std::isfinite` usage and add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if present

### 3.4 Commit

- [X] T027 [US1] **Commit completed User Story 1 work** (feedback path core) to branch `123-analysis-feedback-loop`

**Checkpoint**: Core feedback signal flow works. Bit-identical at amount=0. Converges at amount=1. Hard clamp respected.

---

## Phase 4: User Story 2 — Feedback Decay for Natural Sustain (Priority: P1)

**Goal**: Ensure the feedback buffer decays to silence within the measurable SC-003 time bounds (decay=1.0 within 10s, decay=0.5 within 60s at 44.1kHz / 512-sample blocks), providing musical natural sustain behavior.

**Independent Test**: SC-003 with specific parameter values — after sidechain stops (silence), output RMS falls below -60dBFS within the specified time bounds.

### 4.1 Write Decay Verification Tests

> **Constitution Principle XII**: Tests MUST be written before verification begins. Note: because the decay formula was already implemented in T024, these tests may pass immediately if T024's formula is correct, or fail if it is wrong. Either outcome is informative. Write the tests before running T031.

- [X] T028 [US2] Write SC-003 test (decay=1.0 case): feed sidechain audio for 1 second with feedback=1.0, then set sidechain to silence and run for 10 seconds at 44.1kHz / 512-sample blocks, verify output RMS falls below -60dBFS in `plugins/innexus/tests/integration/test_analysis_feedback.cpp`
- [X] T029 [US2] Write SC-003 test (decay=0.5 case): same setup, run for 60 seconds, verify output RMS below -60dBFS in `plugins/innexus/tests/integration/test_analysis_feedback.cpp`
- [X] T030 Build and run T028–T029 tests; if they FAIL, proceed to T031; if they PASS, confirm T024's formula is correct and document (the formula from FR-013 was already implemented correctly)

### 4.2 Verify Decay Implementation is Correct

The per-block decay formula was implemented in T024. These tests verify the formula constants are correct.

- [X] T031 [US2] Build Release and run SC-003 tests; if they fail, verify the decay coefficient formula in `plugins/innexus/src/processor/processor.cpp` matches `exp(-decayAmount * blockSize / sampleRate)` exactly as specified in FR-013 and the clarification for SC-003

### 4.3 Verify and Cross-Platform Check

- [X] T032 [US2] Confirm all SC-003 tests pass with correct time bounds
- [X] T033 [US2] Verify no new IEEE 754 function calls were introduced without `-fno-fast-math` guard

### 4.4 Commit

- [X] T034 [US2] **Commit completed User Story 2 decay verification work** to branch `123-analysis-feedback-loop`

**Checkpoint**: Decay times are correct and within spec bounds. Feedback energy dissipates predictably.

---

## Phase 5: User Story 3 — Safety and Stability Guarantees (Priority: P1)

**Goal**: Verify the 5-layer safety stack prevents divergence under all parameter combinations. The soft limiter (tanh), energy budget normalization (existing), hard output clamp (existing), confidence gate (existing), and feedback decay (Spec B) work together as documented in plan.md §Architecture.

**Independent Test**: SC-002 (convergence), SC-004 (ceiling), SC-008 (negligible CPU overhead) are all stability indicators — already covered by earlier tests. This phase adds the explicit overhead measurement.

### 5.1 Write Failing Test First

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T035 [US3] Write SC-008 test: (a) code-structure verification — review the feedback mixing loop body in `plugins/innexus/src/processor/processor.cpp` and document in a test comment that it contains only arithmetic operations (tanh, multiply, add) with no allocations, system calls, or virtual dispatch; (b) coarse timing sanity check — run 1000 process() blocks with feedback=1.0 and 1000 blocks with feedback=0.0, verify the average block time difference is less than 1% of baseline using wall-clock measurement in `plugins/innexus/tests/integration/test_analysis_feedback.cpp`
- [X] T036 Build and confirm T035 test FAIL or produces a baseline measurement

### 5.2 Implementation

No new code for the safety stack — the 5 layers are already in place from earlier phases and existing code (FR-010, FR-011, FR-012 require no changes per spec.md assumptions and plan.md §Safety Stack). This phase confirms they cooperate correctly.

- [X] T037 [US3] Build Release and run all integration tests (SC-001 through SC-004, SC-008) to confirm the full safety stack passes together

### 5.3 Verify

- [X] T038 [US3] Run full innexus_tests suite and confirm all tests pass with no regressions: `cmake --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 5.4 Commit

- [X] T039 [US3] **Commit completed User Story 3 safety verification work** to branch `123-analysis-feedback-loop`

**Checkpoint**: All P1 user stories (US1, US2, US3) are complete, tested, and stable.

---

## Phase 6: User Story 4 — Freeze Interaction (Priority: P2)

**Goal**: When freeze is engaged, bypass the feedback path. When freeze is disengaged, clear the feedback buffer so stale audio does not contaminate the re-engaged analysis pipeline.

**Independent Test**: SC-005 (freeze bypasses feedback in same process call) and SC-006 (freeze disengage clears buffer to all zeros).

### 6.1 Write Failing Tests First

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T040 [US4] Write SC-005 test: engage freeze while feedback is active, verify feedback buffer contents are not mixed into analysis input on the next process call in `plugins/innexus/tests/integration/test_analysis_feedback.cpp` (FR-015)
- [X] T041 [US4] Write SC-006 test: disengage freeze, verify `feedbackBuffer_` contains all zeros immediately after the process call that disengages freeze in `plugins/innexus/tests/integration/test_analysis_feedback.cpp` (FR-016)
- [X] T042 Build and confirm T040–T041 tests FAIL

### 6.2 Implementation

- [X] T043 [US4] Add freeze bypass gate to the feedback mixing block in `plugins/innexus/src/processor/processor.cpp`: read `freeze_` atomic alongside `feedbackAmount_` and skip the feedback mixing loop when `manualFrozen == true` (FR-015; the mixing block from T022 already reads this flag per plan.md §3.A, confirm it is wired correctly)
- [X] T044 [US4] Add `feedbackBuffer_.fill(0.0f)` to the freeze-disengage transition block in `plugins/innexus/src/processor/processor.cpp` (at the existing `!currentFreezeState && previousFreezeState_` branch, per plan.md §3.C) (FR-016)

### 6.3 Verify

- [X] T045 [US4] Build Release and run SC-005 and SC-006 tests, confirm they pass
- [X] T046 [US4] Run full innexus_tests to confirm no freeze regression in existing tests

### 6.4 Commit

- [X] T047 [US4] **Commit completed User Story 4 freeze interaction work** to branch `123-analysis-feedback-loop`

**Checkpoint**: Freeze engages/disengages cleanly with feedback. No frozen frame corruption.

---

## Phase 7: User Story 5 — Sample Mode Bypass (Priority: P2)

**Goal**: In sample mode (not sidechain mode), FeedbackAmount has no effect — output is identical regardless of its value.

**Independent Test**: SC-007 — switching to sample mode with any FeedbackAmount value produces identical output to FeedbackAmount=0.0 (FR-014).

### 7.1 Write Failing Test First

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T048 [US5] Write SC-007 test: set input source to sample mode, compare output buffers with FeedbackAmount=0.0 vs FeedbackAmount=1.0, verify they are bit-identical in `plugins/innexus/tests/integration/test_analysis_feedback.cpp` (FR-014)
- [X] T049 Build and confirm T048 test FAILS. If it passes before T050's implementation step is confirmed complete, document why (the `InputSource::Sidechain` gate in T022/T023 already enforces sample mode bypass) and mark the test as a regression guard going forward — a test that passes before implementation because the gate was already in place is still valid; it proves the gate functions correctly. Do not skip or remove the test.

### 7.2 Implementation

The sample mode bypass is gated by the `currentSource == 1` (sidechain) check already present in the mixing block (T022) and capture block (T023). No new code should be needed.

- [X] T050 [US5] Confirm the feedback mixing block and feedback capture block both check `InputSource::Sidechain` before executing; if any path is missing the guard, add it to `plugins/innexus/src/processor/processor.cpp` (FR-014)

### 7.3 Verify

- [X] T051 [US5] Build Release and run SC-007 test, confirm it passes
- [X] T052 [US5] Run full innexus_tests to confirm no regressions

### 7.4 Commit

- [X] T053 [US5] **Commit completed User Story 5 sample mode bypass work** to branch `123-analysis-feedback-loop`

**Checkpoint**: All 5 user stories implemented, tested, and committed.

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 8.1 Run Clang-Tidy Analysis

- [X] T054 Regenerate compile_commands.json if needed (run from VS Developer PowerShell): `cmake --preset windows-ninja`
- [X] T055 Run clang-tidy on all modified and new source files: `./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja` (Windows) or `./tools/run-clang-tidy.sh --target innexus` (Linux/macOS)

### 8.2 Address Findings

- [X] T056 Fix all clang-tidy errors (blocking issues) in `plugins/innexus/src/processor/processor.h`, `plugins/innexus/src/processor/processor.cpp`, and `plugins/innexus/src/controller/controller.cpp`
- [X] T057 Review warnings and fix where appropriate; add `// NOLINT(<check>): <reason>` for any intentional suppressions (DSP-specific numeric code may require suppressions for magic number checks)

**Checkpoint**: Static analysis clean — ready for completion verification.

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task.

### 9.1 Architecture Documentation Update

- [X] T058 Update `specs/_architecture_/` to record the analysis feedback loop addition: document the feedback buffer pattern (`std::array<float, 8192>` pre-allocated in `setActive()`), the soft limiter formula, the 5-layer safety stack, and the freeze interaction contract; add or update the Innexus processor entry in the relevant architecture section

### 9.2 Final Commit

- [X] T059 **Commit architecture documentation updates** to branch `123-analysis-feedback-loop`
- [X] T060 Verify all spec work is committed: `git status` should show a clean working tree on branch `123-analysis-feedback-loop`

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

- [X] T061 **Review ALL FR-001 through FR-020** from `specs/123-analysis-feedback-loop/spec.md` against the actual implementation — open each relevant file, find the code, cite file and line number
- [X] T062 **Run all SC-xxx success criteria tests** and verify measurable targets are achieved: SC-001 (bit-identical at 0.0), SC-002 (convergence), SC-003 (decay time bounds), SC-004 (output ceiling), SC-005 (freeze bypass), SC-006 (buffer clear), SC-007 (sample mode), SC-008 (CPU overhead)
- [X] T063 **Search for cheating patterns**: grep new code for `// placeholder`, `// TODO`, `// FIXME`; verify no test thresholds were relaxed from spec values; verify no features were removed from scope

### 10.2 Run Full Test Suite

- [X] T064 Run full innexus_tests suite: `cmake --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`
- [X] T065 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`

### 10.3 Fill Compliance Table in spec.md

- [X] T066 **Update `specs/123-analysis-feedback-loop/spec.md` "Implementation Verification" section** — fill every FR-xxx and SC-xxx row with status (MET/NOT MET/PARTIAL) and evidence (file path, line number, test name, measured value); mark overall status COMPLETE / NOT COMPLETE / PARTIAL

### 10.4 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T067 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

### 10.5 Final Commit

- [X] T068 **Commit spec.md compliance table update** to branch `123-analysis-feedback-loop`
- [X] T069 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (State)**: Depends on Phase 1 (needs parameter IDs and atomics)
- **Phase 3 (US1 — Feedback Path)**: Depends on Phase 1 (needs atomics and buffer); Phase 2 can proceed in parallel
- **Phase 4 (US2 — Decay)**: Depends on Phase 3 (decay code added in T024; this phase tests it)
- **Phase 5 (US3 — Safety)**: Depends on Phases 3 and 4 (verifies the combined safety stack)
- **Phase 6 (US4 — Freeze)**: Depends on Phase 3 (freeze bypass is part of the mixing block)
- **Phase 7 (US5 — Sample Mode)**: Depends on Phase 3 (sample mode gate is part of the mixing block)
- **Phases 6 and 7**: Can proceed in parallel after Phase 3
- **Phase 8 (Clang-Tidy)**: Depends on all implementation phases complete
- **Phase 9 (Docs)**: Depends on Phase 8
- **Phase 10 (Completion)**: Depends on all phases

### User Story Dependencies

- **US1 (P1)**: Can start after Phase 1 — core feedback path, blocks US2–US5
- **US2 (P1)**: Depends on US1 — verifies decay formula installed in T024
- **US3 (P1)**: Depends on US1 and US2 — verifies combined safety stack
- **US4 (P2)**: Depends on US1 — freeze bypass lives in the mixing block
- **US5 (P2)**: Depends on US1 — sample mode gate lives in the mixing block
- **US4 and US5**: Independent of each other, can proceed in parallel

### Parallel Opportunities

- T006 (controller parameter registration) is independent of T005 (processor parameter changes) — can be written in parallel
- T028–T029 (SC-003 decay tests) can be written while T022–T024 implementation is being verified
- T040–T041 (SC-005/SC-006 freeze tests) and T048 (SC-007 sample mode test) can be written in parallel after Phase 3 completes

---

## Implementation Strategy

### MVP: User Story 1 Only

1. Complete Phase 1 (parameter IDs, members) — 30 min
2. Complete Phase 2 (state v8) — 1 hour
3. Complete Phase 3 (core feedback path) — 1–2 hours
4. **STOP and VALIDATE**: SC-001 (no regression) and SC-002 (convergence) confirm the core loop works

### Full Delivery (All Stories)

1. Phase 1 + 2 → Foundation + state persistence
2. Phase 3 → Core feedback path (US1, MVP)
3. Phase 4 → Decay verification (US2)
4. Phase 5 → Safety stack confirmation (US3)
5. Phase 6 + 7 in parallel → Freeze interaction (US4) + Sample mode (US5)
6. Phases 8, 9, 10 → Quality gates, docs, honest completion

---

## Notes

- [P] tasks = different files, no dependencies on each other
- [USn] label maps task to specific user story for traceability
- All file paths are relative to the monorepo root `F:\projects\iterum\`
- Spec A prerequisite (122-harmonic-physics) is already merged — `HarmonicPhysics`, `kOutputClamp`, and energy budget normalization exist
- Parameter IDs 700–703 are used by Spec A; 710–711 are reserved for this spec
- `sidechainBuffer_` at `processor.h:504` is the sizing reference for `feedbackBuffer_` (both `std::array<float, 8192>`)
- The `std::tanh` soft limiter is a stdlib call — no custom DSP primitive needed
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance for any test files using `std::isnan`/`std::isfinite`
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- **NEVER claim completion if ANY requirement is not met** — document gaps honestly instead
