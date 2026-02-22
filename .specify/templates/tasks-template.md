---

description: "Task list template for feature implementation"
---

# Tasks: [FEATURE NAME]

**Input**: Design documents from `/specs/[###-feature-name]/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Integration Tests (MANDATORY When Applicable)

If the feature **wires a sub-component into the processor** (MIDI routing, audio chain, parameter application via `applyParamsToEngine()`, stateful per-block processing, or host context dependency), integration tests are **required** — not optional. See `INTEGRATION-TESTING.md` in the testing-guide skill for the full checklist and anti-patterns.

Key rules:
- **Behavioral correctness over existence checks**: Verify the output is *correct*, not just *present*. "Audio exists" is not a valid integration test.
- **Test degraded host conditions**: Not just ideal `kPlaying | kTempoValid` — also no transport, no tempo, `nullptr` process context.
- **Test per-block configuration safety**: Ensure setters called every block in `applyParamsToEngine()` don't silently reset stateful components.

### Example Todo List Structure

```
[ ] Write failing tests for [feature]
[ ] Implement [feature] to make tests pass
[ ] Verify all tests pass
[ ] Cross-platform check: verify -fno-fast-math for IEEE 754 functions
[ ] Commit completed work
```

**DO NOT** skip the commit step. These appear as checkboxes because they MUST be tracked.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/path/to/your_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- **Single project**: `src/`, `tests/` at repository root
- **Web app**: `backend/src/`, `frontend/src/`
- **Mobile**: `api/src/`, `ios/src/` or `android/src/`
- Paths shown below assume single project - adjust based on plan.md structure

<!-- 
  ============================================================================
  IMPORTANT: The tasks below are SAMPLE TASKS for illustration purposes only.
  
  The /speckit.tasks command MUST replace these with actual tasks based on:
  - User stories from spec.md (with their priorities P1, P2, P3...)
  - Feature requirements from plan.md
  - Entities from data-model.md
  - Endpoints from contracts/
  
  Tasks MUST be organized by user story so each story can be:
  - Implemented independently
  - Tested independently
  - Delivered as an MVP increment
  
  DO NOT keep these sample tasks in the generated tasks.md file.
  ============================================================================
-->

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [ ] T001 Create project structure per implementation plan
- [ ] T002 Initialize [language] project with [framework] dependencies
- [ ] T003 [P] Configure linting and formatting tools

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

Examples of foundational tasks (adjust based on your project):

- [ ] T004 Setup database schema and migrations framework
- [ ] T005 [P] Implement authentication/authorization framework
- [ ] T006 [P] Setup API routing and middleware structure
- [ ] T007 Create base models/entities that all stories depend on
- [ ] T008 Configure error handling and logging infrastructure
- [ ] T009 Setup environment configuration management

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - [Title] (Priority: P1) 🎯 MVP

**Goal**: [Brief description of what this story delivers]

**Independent Test**: [How to verify this story works on its own]

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T011 [P] [US1] Unit tests for [Entity1] in tests/unit/test_[entity1].cpp
- [ ] T012 [P] [US1] Unit tests for [Entity2] in tests/unit/test_[entity2].cpp
- [ ] T013 [P] [US1] Integration test for [user journey] in tests/integration/test_[name].cpp

### 3.3 Implementation for User Story 1

- [ ] T014 [P] [US1] Create [Entity1] in src/[layer]/[entity1].h
- [ ] T015 [P] [US1] Create [Entity2] in src/[layer]/[entity2].h
- [ ] T016 [US1] Implement [Service] in src/[layer]/[service].h (depends on T014, T015)
- [ ] T017 [US1] Verify all tests pass
- [ ] T018 [US1] Add validation and error handling (if needed)

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T019 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [ ] T020 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - [Title] (Priority: P2)

**Goal**: [Brief description of what this story delivers]

**Independent Test**: [How to verify this story works on its own]

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T022 [P] [US2] Unit tests for [Entity] in tests/unit/test_[entity].cpp
- [ ] T023 [P] [US2] Integration test for [user journey] in tests/integration/test_[name].cpp

### 4.3 Implementation for User Story 2

- [ ] T024 [P] [US2] Create [Entity] in src/[layer]/[entity].h
- [ ] T025 [US2] Implement [Service] in src/[layer]/[service].h
- [ ] T026 [US2] Verify all tests pass
- [ ] T027 [US2] Integrate with User Story 1 components (if needed)

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T028 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 4.5 Commit (MANDATORY)

- [ ] T029 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - [Title] (Priority: P3)

**Goal**: [Brief description of what this story delivers]

**Independent Test**: [How to verify this story works on its own]

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T031 [P] [US3] Unit tests for [Entity] in tests/unit/test_[entity].cpp
- [ ] T032 [P] [US3] Integration test for [user journey] in tests/integration/test_[name].cpp

### 5.3 Implementation for User Story 3

- [ ] T033 [P] [US3] Create [Entity] in src/[layer]/[entity].h
- [ ] T034 [US3] Implement [Service] in src/[layer]/[service].h
- [ ] T035 [US3] Verify all tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T036 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 5.5 Commit (MANDATORY)

- [ ] T037 [US3] **Commit completed User Story 3 work**

**Checkpoint**: All user stories should now be independently functional and committed

---

[Add more user story phases as needed, following the same pattern]

---

## Phase N-1: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] TXXX [P] Documentation updates in docs/
- [ ] TXXX Code cleanup and refactoring
- [ ] TXXX Performance optimization across all stories
- [ ] TXXX [P] Additional unit tests (if requested) in tests/unit/
- [ ] TXXX Security hardening
- [ ] TXXX Run quickstart.md validation

---

## Phase N-2: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### N-2.1 Architecture Documentation Update

- [ ] TXXX **Update `specs/_architecture_/`** with new components added by this spec:
  - Add new component entries to appropriate layer section file (layer-0-core.md, layer-1-primitives.md, etc.)
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples if applicable
  - Verify no duplicate functionality was introduced

### N-2.2 Final Commit

- [ ] TXXX **Commit architecture documentation updates**
- [ ] TXXX Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase N-1.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### N-1.0.1 Run Clang-Tidy Analysis

- [ ] TXXX **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target all

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target all
  ```

### N-1.0.2 Address Findings

- [ ] TXXX **Fix all errors** reported by clang-tidy (blocking issues)
- [ ] TXXX **Review warnings** and fix where appropriate (use judgment for DSP code)
- [ ] TXXX **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase N-1: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### N-1.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] TXXX **Review ALL FR-xxx requirements** from spec.md against implementation
- [ ] TXXX **Review ALL SC-xxx success criteria** and verify measurable targets are achieved
- [ ] TXXX **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### N-1.2 Fill Compliance Table in spec.md

- [ ] TXXX **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] TXXX **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### N-1.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] TXXX **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase N: Final Completion

**Purpose**: Final commit and completion claim

### N.1 Final Commit

- [ ] TXXX **Commit all spec work** to feature branch
- [ ] TXXX **Verify all tests pass**

### N.2 Completion Claim

- [ ] TXXX **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3+)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P2 → P3)
- **Polish (Final Phase)**: Depends on all desired user stories being complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Can start after Foundational (Phase 2) - May integrate with US1 but should be independently testable
- **User Story 3 (P3)**: Can start after Foundational (Phase 2) - May integrate with US1/US2 but should be independently testable

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Models/entities before services
- Services before endpoints
- Core implementation before integration
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks marked [P] can run in parallel
- All Foundational tasks marked [P] can run in parallel (within Phase 2)
- Once Foundational phase completes, all user stories can start in parallel (if team capacity allows)
- All tests for a user story marked [P] can run in parallel
- Models within a story marked [P] can run in parallel
- Different user stories can be worked on in parallel by different team members

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together (if tests requested):
Task: "Contract test for [endpoint] in tests/contract/test_[name].py"
Task: "Integration test for [user journey] in tests/integration/test_[name].py"

# Launch all models for User Story 1 together:
Task: "Create [Entity1] model in src/models/[entity1].py"
Task: "Create [Entity2] model in src/models/[entity2].py"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Deploy/Demo (MVP!)
3. Add User Story 2 → Test independently → Deploy/Demo
4. Add User Story 3 → Test independently → Deploy/Demo
5. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1
   - Developer B: User Story 2
   - Developer C: User Story 3
3. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
