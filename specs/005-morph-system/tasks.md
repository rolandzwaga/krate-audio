# Tasks: MorphEngine DSP System

**Input**: Design documents from `F:\projects\iterum\specs\005-morph-system\`
**Prerequisites**: plan.md, spec.md

**Related Reference Documents**:
- [specs/Disrumpo/roadmap.md](../Disrumpo/roadmap.md) - Condensed task summary T5.1-T5.17, Milestone M4 criteria
- [specs/Disrumpo/tasks-overview.md](../Disrumpo/tasks-overview.md) - Week 5-6 Morph System task breakdown

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**Task ID Format**: Following condensed T5.x style from roadmap.md (Milestone M4). This implementation spec expands the condensed T5.1-T5.17 into granular sub-tasks (T5.1a, T5.1b, etc.) for better implementation tracking while maintaining traceability to the roadmap

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase 7.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `plugins\disrumpo\tests\CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/morph_weight_computation_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Phase 1: Setup (Data Structures)

**Purpose**: Create core data structures for morph system

### 1.1 MorphNode Data Structure

- [ ] **T5.1** [P] Create MorphNode struct in `plugins\disrumpo\src\dsp\morph_node.h`
  - Fields: id (int), type (DistortionType), params (DistortionParams), posX (float), posY (float)
  - Default constructor with safe defaults
  - Per spec FR-002, dsp-details.md Section 7.1

### 1.2 DistortionFamily Enum

- [ ] **T5.1b** [P] Add DistortionFamily enum to `plugins\disrumpo\src\dsp\distortion_types.h`
  - Seven families: Saturation, Wavefold, Digital, Rectify, Dynamic, Hybrid, Experimental
  - Per spec FR-016

- [ ] **T5.1c** [P] Add getFamily() function to `plugins\disrumpo\src\dsp\distortion_types.h`
  - Maps DistortionType → DistortionFamily
  - Returns constexpr DistortionFamily
  - Per spec FR-016 family-type mapping table

### 1.3 MorphMode Enum Formalization

- [ ] **T5.1d** [P] Add MorphMode enum to `plugins\disrumpo\src\dsp\distortion_types.h`
  - Values: Linear1D, Planar2D, Radial2D
  - Per spec FR-003, FR-004, FR-005

### 1.4 BandState Extension

- [ ] **T5.1e** [P] Extend BandState struct in `plugins\disrumpo\src\dsp\band_state.h`
  - Add: std::array<MorphNode, 4> nodes
  - Add: int activeNodeCount (default 2)
  - Add: MorphMode morphMode (default Linear1D)
  - Add: float morphX (default 0.5f), morphY (default 0.5f)

### 1.5 Commit

- [ ] **T5.1f** Commit data structures: "Add MorphNode, DistortionFamily, MorphMode data structures"

---

## Phase 2: Foundational (MorphEngine Shell)

**Purpose**: Create MorphEngine class structure with weight computation

### 2.1 MorphEngine Class Declaration

- [ ] **T5.2** Create MorphEngine class in `plugins\disrumpo\src\dsp\morph_engine.h`
  - Constructor, prepare(), reset() methods
  - Public API: setMorphPosition(x, y), setMode(mode), setNodes(nodes), process(input)
  - Private members: weight array, smoothers, distortion adapters
  - Per plan.md "MorphEngine Class Declaration"

### 2.2 Write Failing Tests for Weight Computation

- [ ] **T5.3a** [P] [US1] Write unit tests for inverse distance weighting in `plugins\disrumpo\tests\unit\morph_weight_computation_test.cpp`
  - Test: cursor at node position → 100% weight
  - Test: cursor equidistant from 2 nodes → 50/50 weights
  - Test: 4 nodes at corners, cursor at center → 25% each
  - Test: weights sum to 1.0 (normalized)
  - Test: node below 0.001 threshold skipped, weights renormalized (FR-015)
  - Tests MUST FAIL initially (no implementation yet)
  - Per spec FR-001, FR-014, FR-015, SC-001, SC-005

### 2.3 Implement Weight Computation

- [ ] **T5.3** [US1] Implement calculateMorphWeights() in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - Inverse distance weighting with exponent p=2
  - Handle cursor-on-node special case (100% weight)
  - Skip weights below 0.001 threshold
  - Renormalize remaining weights to sum to 1.0
  - Per spec FR-001, FR-015, dsp-details.md Section 7.1

### 2.4 Verify Tests Pass

- [ ] **T5.3b** [US1] Verify morph_weight_computation_test.cpp tests pass
- [ ] **T5.3c** [US1] Benchmark weight computation < 100ns for 4 nodes (SC-001)

### 2.5 Cross-Platform Check

- [ ] **T5.3d** [US1] **Verify IEEE 754 compliance**: If tests use NaN checks, add to `-fno-fast-math` list in plugins\disrumpo\tests\CMakeLists.txt

### 2.6 Commit

- [ ] **T5.3e** [US1] Commit weight computation: "Implement inverse distance weight computation"

---

## Phase 3: User Story 1 - Basic Morph Between Two Distortion Types (Priority: P1) 🎯 MVP

**Goal**: Enable smooth A-B morphing between two distortion types within a single band

**Independent Test**: Process sine wave while automating morph position from 0 to 1, verify smooth output transition with no clicks

### 3.1 Write Failing Tests for 1D Linear Mode

- [ ] **T5.4a** [P] [US1] Write unit tests for 1D Linear mode in `plugins\disrumpo\tests\unit\morph_mode_test.cpp`
  - Test: position 0.0 → 100% node A
  - Test: position 1.0 → 100% node B
  - Test: position 0.5 → 50% A, 50% B
  - Test: 3 nodes (A, B, C) at 0.0, 0.5, 1.0 → position 0.25 → correct weights
  - Tests MUST FAIL initially
  - Per spec User Story 1 acceptance scenarios, FR-003

### 3.2 Implement 1D Linear Mode

- [ ] **T5.4** [US1] Implement 1D Linear morph mode in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - Single axis interpolation using morphX only
  - Map cursor position to node positions on [0, 1] line
  - Call calculateMorphWeights() with 1D distances
  - Per spec FR-003

### 3.3 Verify Tests Pass

- [ ] **T5.4b** [US1] Verify morph_mode_test.cpp (1D Linear) tests pass

### 3.4 Write Failing Tests for Smoothing

- [ ] **T5.11a** [P] [US1] Write unit tests for morph smoothing in `plugins\disrumpo\tests\unit\morph_transition_test.cpp`
  - Test: 0ms smoothing → instant transition
  - Test: 100ms smoothing → transition completes in 95-105ms (SC-006)
  - Test: rapid automation (20Hz square wave) → no clicks or artifacts (SC-007)
  - Tests MUST FAIL initially
  - Per spec FR-009, SC-006, SC-007

### 3.5 Implement Morph Smoothing

- [ ] **T5.11** [US1] Implement morph position smoothing in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - Use OnePoleSmoother for morphX and morphY
  - Configure smoothing time 0-500ms
  - Context-aware: smooth position for manual control, smooth weights for automated drivers
  - Per spec FR-009, plan.md "Smoothing"

### 3.6 Verify Tests Pass

- [ ] **T5.11b** [US1] Verify morph_transition_test.cpp (smoothing) tests pass

### 3.7 Basic Integration

- [ ] **T5.12** [US1] Add morph position parameters (morphX, morphY) per band in `plugins\disrumpo\src\plugin_ids.h`
  - Per spec FR-012

- [ ] **T5.13** [US1] Add morph mode parameter per band in `plugins\disrumpo\src\plugin_ids.h`
  - Per spec FR-013

- [ ] **T5.14** [US1] Integrate MorphEngine into BandProcessor in `plugins\disrumpo\src\dsp\band_processor.h`
  - BandProcessor owns MorphEngine instance
  - Pass audio through MorphEngine.process() (simple blend for now)
  - Per spec FR-010, plan.md "BandProcessor Integration"

### 3.8 Cross-Platform Check

- [ ] **T5.14a** [US1] **Verify IEEE 754 compliance**: Check all test files, add to `-fno-fast-math` list if needed

### 3.9 Commit

- [ ] **T5.14b** [US1] Commit User Story 1: "Implement basic A-B morphing with 1D Linear mode and smoothing"

**Checkpoint**: User Story 1 complete - basic A-B morphing functional

---

## Phase 4: User Story 2 - 2D Planar Morph with Four Nodes (Priority: P1)

**Goal**: Enable 2D XY pad morphing with up to 4 distortion nodes at corners

**Independent Test**: Set cursor to each corner and center, verify correct weight distribution

### 4.1 Write Failing Tests for 2D Planar Mode

- [ ] **T5.5a** [P] [US2] Write unit tests for 2D Planar mode in `plugins\disrumpo\tests\unit\morph_mode_test.cpp`
  - Test: cursor at (0,0) → node A 100%
  - Test: cursor at (0.5, 0.5) → all 4 nodes 25% each
  - Test: cursor at (0.25, 0.25) → node A highest weight, distance-based for B, C, D
  - Tests MUST FAIL initially
  - Per spec User Story 2 acceptance scenarios, FR-004

### 4.2 Implement 2D Planar Mode

- [ ] **T5.5** [US2] Implement 2D Planar morph mode in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - 2D inverse distance weighting using morphX and morphY
  - Calculate 2D Euclidean distances from cursor to each node
  - Call calculateMorphWeights() with 2D distances
  - Per spec FR-004

### 4.3 Verify Tests Pass

- [ ] **T5.5b** [US2] Verify morph_mode_test.cpp (2D Planar) tests pass

### 4.4 Cross-Platform Check

- [ ] **T5.5c** [US2] **Verify IEEE 754 compliance**: Check test files, add to `-fno-fast-math` list if needed

### 4.5 Commit

- [ ] **T5.5d** [US2] Commit User Story 2: "Implement 2D Planar morph mode"

**Checkpoint**: User Story 2 complete - 2D morphing functional

---

## Phase 5: User Story 3 - Same-Family Parameter Interpolation (Priority: P2)

**Goal**: Optimize same-family morphs using parameter interpolation (single processor)

**Independent Test**: Compare CPU usage of same-family morph vs cross-family morph, verify parameter values interpolated linearly

### 5.1 Write Failing Tests for Same-Family Interpolation

- [ ] **T5.7a** [P] [US3] Write unit tests for same-family interpolation in `plugins\disrumpo\tests\unit\morph_interpolation_test.cpp`
  - Test: two Saturation nodes (Soft Clip, Tube) → parameters interpolated linearly
  - Test: node A drive=2.0, node B drive=8.0, 50/50 weights → effective drive=5.0
  - Test: same-family morph uses single distortion instance (CPU measurement, SC-004)
  - Tests MUST FAIL initially
  - Per spec User Story 3, FR-006, FR-018, SC-004

### 5.2 Implement Same-Family Parameter Interpolation

- [ ] **T5.7** [US3] Implement same-family parameter interpolation in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - Check if all active nodes belong to same family (via getFamily())
  - If same family: interpolate DistortionParams weighted by node weights
  - Process through single DistortionAdapter instance with blended params
  - Per spec FR-006, FR-016, FR-018

### 5.3 Verify Tests Pass

- [ ] **T5.7b** [US3] Verify morph_interpolation_test.cpp (same-family) tests pass
- [ ] **T5.7c** [US3] Verify same-family CPU < 50% of cross-family CPU (SC-004)

### 5.4 Cross-Platform Check

- [ ] **T5.7d** [US3] **Verify IEEE 754 compliance**: Check test files, add to `-fno-fast-math` list if needed

### 5.5 Commit

- [ ] **T5.7e** [US3] Commit User Story 3: "Implement same-family parameter interpolation"

**Checkpoint**: User Story 3 complete - same-family optimization functional

---

## Phase 6: User Story 4 - Cross-Family Parallel Processing (Priority: P2)

**Goal**: Enable morphing between incompatible distortion families using parallel processing and equal-power crossfade

**Independent Test**: Morph between incompatible types (Saturation + Digital), measure output level consistency (no volume dips at 50% morph)

### 6.1 Write Failing Tests for Cross-Family Processing

- [ ] **T5.8a** [P] [US4] Write unit tests for cross-family processing in `plugins\disrumpo\tests\unit\morph_interpolation_test.cpp`
  - Test: nodes from different families → parallel processing
  - Test: 50% cross-family morph → output level matches single-type (equal-power, SC-002)
  - Test: transition zone 40-60% → both processors active
  - Tests MUST FAIL initially
  - Per spec User Story 4, FR-007, FR-008, SC-002

### 6.2 Implement Cross-Family Parallel Processing

- [ ] **T5.8** [US4] Implement cross-family parallel processing in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - Detect cross-family scenario (nodes have different families)
  - Process each node's distortion in parallel
  - Output: weighted sum of each node's output
  - Per spec FR-007

### 6.3 Implement Equal-Power Crossfade

- [ ] **T5.9** [US4] Implement equal-power crossfade for cross-family in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - Use KrateDSP equalPowerGains() from dsp/include/krate/dsp/core/crossfade_utils.h
  - Apply to output blend to prevent volume dips
  - Per spec FR-007, plan.md "Dependency API Contracts"

### 6.4 Implement Transition Zone Fade

- [ ] **T5.10** [US4] Implement transition zone fade-in (40-60%) in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - Processors activate smoothly between 40-60% weight
  - Use equal-power ramp for activation
  - Fade-in duration: 5-10ms to prevent clicks
  - Per spec FR-008, dsp-details.md Section 7.2

### 6.5 Verify Tests Pass

- [ ] **T5.10a** [US4] Verify morph_interpolation_test.cpp (cross-family) tests pass
- [ ] **T5.10b** [US4] Verify output level within 1dB at all blend positions (SC-002)

### 6.6 Write Failing Approval Tests

- [ ] **T5.17a** [P] [US4] Write approval test AP-003 in `plugins\disrumpo\tests\approval\morph_artifact_test.cpp`
  - Process sine sweep during morph automation
  - Save output to reference file
  - Verify no clicks/pops/discontinuities (visual/audio inspection)
  - Test MUST FAIL initially (or create baseline)
  - Per spec SC-003

### 6.7 Verify Approval Tests Pass

- [ ] **T5.17** [US4] Verify AP-003 approval test passes (artifact-free transitions, SC-003)

### 6.8 Cross-Platform Check

- [ ] **T5.10c** [US4] **Verify IEEE 754 compliance**: Check test files, add to `-fno-fast-math` list if needed

### 6.9 Commit

- [ ] **T5.10d** [US4] Commit User Story 4: "Implement cross-family parallel processing with equal-power crossfade"

**Checkpoint**: User Story 4 complete - cross-family morphing functional with no artifacts

---

## Phase 7: User Story 5 - Artifact-Free Fast Automation (Priority: P2)

**Goal**: Ensure morph system handles rapid automation without clicks, pops, or zipper noise

**Independent Test**: Automate morph position with square wave LFO, verify no audible artifacts in output

### 7.1 Write Failing Tests for Rapid Automation

- [ ] **T5.15a** [P] [US5] Write unit tests for rapid automation in `plugins\disrumpo\tests\unit\morph_transition_test.cpp`
  - Test: 0ms smoothing → instant transition within one audio block
  - Test: 500ms smoothing → transition takes ~500ms (475-525ms, SC-006)
  - Test: 20Hz LFO modulation → no clicks or zipper noise (SC-007)
  - Tests MUST FAIL initially (or verify existing smoothing covers this)
  - Per spec User Story 5, FR-009, SC-006, SC-007

### 7.2 Verify Smoothing Handles Automation

- [ ] **T5.15** [US5] Verify existing morph smoothing (T5.11) handles rapid automation cases
  - If tests fail: enhance smoothing implementation
  - If tests pass: smoothing already sufficient

### 7.3 Verify Tests Pass

- [ ] **T5.15b** [US5] Verify morph_transition_test.cpp (rapid automation) tests pass

### 7.4 Cross-Platform Check

- [ ] **T5.15c** [US5] **Verify IEEE 754 compliance**: Check test files, add to `-fno-fast-math` list if needed

### 7.5 Commit

- [ ] **T5.15d** [US5] Commit User Story 5: "Verify artifact-free rapid automation"

**Checkpoint**: User Story 5 complete - rapid automation safe

---

## Phase 8: User Story 6 - 2D Radial Mode (Priority: P3)

**Goal**: Add 2D Radial mode where angle selects nodes and distance controls blend intensity

**Independent Test**: Set various angle/distance combinations, verify correct node selection and blend intensity

### 8.1 Write Failing Tests for 2D Radial Mode

- [ ] **T5.6a** [P] [US6] Write unit tests for 2D Radial mode in `plugins\disrumpo\tests\unit\morph_mode_test.cpp`
  - Test: angle 0° distance 1.0 → node A 100%
  - Test: distance 0.0 (center) → all nodes equal weight regardless of angle
  - Test: angle 45° distance 1.0 → nodes A and B share weight equally
  - Tests MUST FAIL initially
  - Per spec User Story 6, FR-005

### 8.2 Implement 2D Radial Mode

- [ ] **T5.6** [US6] Implement 2D Radial morph mode in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - Convert morphX/morphY to polar coordinates (angle, distance)
  - Angle selects which nodes are active
  - Distance controls blend intensity
  - Per spec FR-005

### 8.3 Verify Tests Pass

- [ ] **T5.6b** [US6] Verify morph_mode_test.cpp (2D Radial) tests pass

### 8.4 Cross-Platform Check

- [ ] **T5.6c** [US6] **Verify IEEE 754 compliance**: Check test files, add to `-fno-fast-math` list if needed

### 8.5 Commit

- [ ] **T5.6d** [US6] Commit User Story 6: "Implement 2D Radial morph mode"

**Checkpoint**: User Story 6 complete - Radial mode functional

---

## Phase 9: User Story 7 - Chaos-Driven Morph Animation (Priority: P2)

**Goal**: Integrate Chaos morph driver for organic, evolving morph cursor movement

**Independent Test**: Enable Chaos driver, verify morph position changes over time in non-repeating pattern

**Note**: This user story requires modulation system integration (from 008-modulation-system spec). Implementation deferred until after modulation system is complete.

### 9.1 Placeholder for Future Integration

- [ ] **T5.16** [US7] **DEFERRED** Implement Chaos morph driver integration (depends on 008-modulation-system)
  - Will be implemented as part of 008-modulation-system spec
  - MorphEngine.setMorphPosition() will be called by ModulationEngine
  - Per spec User Story 7, FR-017

**Checkpoint**: User Story 7 deferred to 008-modulation-system

---

## Phase 10: Polish & CPU Optimization

**Purpose**: Global processor cap and performance optimization

### 10.1 Global Processor Cap Enforcement

- [ ] **T5.16a** [P] Write unit tests for global processor cap in `plugins\disrumpo\tests\unit\morph_processor_cap_test.cpp`
  - Test: 5 bands × 4 nodes cross-family = 20 processors → cap at 16
  - Test: exceed cap → dynamically reduce node count or raise weight threshold
  - Per spec FR-019, SC-009

- [ ] **T5.16b** Implement global processor cap in `plugins\disrumpo\src\dsp\morph_engine.cpp`
  - Track active distortion instances globally across all bands
  - When cap (16) would be exceeded: raise weight threshold or limit nodes to highest-weight subset
  - Per spec FR-019

- [ ] **T5.16c** Verify processor cap tests pass

### 10.2 Performance Profiling

- [ ] **T5.16d** [P] Profile MorphEngine CPU usage
  - Measure weight computation time (target < 100ns for 4 nodes, SC-001)
  - Measure same-family vs cross-family CPU (SC-004)
  - Identify hot paths for optimization

### 10.3 Commit

- [ ] **T5.16e** Commit polish: "Implement global processor cap and CPU optimization"

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [ ] **T5.18** Update `specs\_architecture_\` with new components added by this spec:
  - Add MorphEngine entry to layer-3-systems.md (purpose, public API, location, usage)
  - Add MorphNode, DistortionFamily, MorphMode to layer-0-core.md or appropriate layer
  - Include usage examples for MorphEngine
  - Document when to use same-family vs cross-family morphing
  - Verify no duplicate functionality introduced

### 11.2 Final Commit

- [ ] **T5.19** Commit architecture documentation updates: "Update architecture docs for MorphEngine"

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 12.1 Run Clang-Tidy Analysis

- [ ] **T5.20** Run clang-tidy on all modified/new source files:
  ```powershell
  # Windows (PowerShell from repo root)
  .\tools\run-clang-tidy.ps1 -Target disrumpo -BuildDir build\windows-ninja
  ```

### 12.2 Address Findings

- [ ] **T5.21** Fix all errors reported by clang-tidy (blocking issues)
- [ ] **T5.22** Review warnings and fix where appropriate (use judgment for DSP code)
- [ ] **T5.23** Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] **T5.24** Review ALL FR-xxx requirements (FR-001 through FR-019) from spec.md against implementation
- [ ] **T5.25** Review ALL SC-xxx success criteria (SC-001 through SC-009) and verify measurable targets achieved
- [ ] **T5.26** Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [ ] **T5.27** Update spec.md "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx
- [ ] **T5.28** Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] **T5.29** All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [ ] **T5.30** Commit all spec work to feature branch: "005-morph-system complete"
- [ ] **T5.31** Verify all tests pass

### 14.2 Completion Claim

- [ ] **T5.32** Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - can start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 completion - BLOCKS all user stories
- **User Stories (Phases 3-9)**: All depend on Phase 2 completion
  - User Story 1 (Phase 3): Can start after Phase 2 - P1 PRIORITY (MVP)
  - User Story 2 (Phase 4): Can start after Phase 2 - P1 PRIORITY
  - User Story 3 (Phase 5): Can start after Phase 2 - P2 PRIORITY
  - User Story 4 (Phase 6): Can start after Phase 2 - P2 PRIORITY
  - User Story 5 (Phase 7): Depends on Phase 3 (T5.11 smoothing) - P2 PRIORITY
  - User Story 6 (Phase 8): Can start after Phase 2 - P3 PRIORITY
  - User Story 7 (Phase 9): DEFERRED to 008-modulation-system spec - P2 PRIORITY
- **Phase 10 (Polish)**: Depends on all desired user stories being complete
- **Phases 11-14 (Documentation, Analysis, Verification)**: Depend on Phase 10

### User Story Completion Order (Recommended)

**Milestone M4 (Roadmap T5.1-T5.17) priorities:**

1. **User Story 1 (P1)**: Basic A-B morphing with 1D Linear mode → MVP READY
2. **User Story 2 (P1)**: 2D Planar morph → Core differentiator
3. **User Story 3 (P2)**: Same-family optimization → Performance improvement
4. **User Story 4 (P2)**: Cross-family parallel processing → Full morph capability
5. **User Story 5 (P2)**: Rapid automation → Production-ready
6. **User Story 6 (P3)**: Radial mode → Alternative interaction model
7. **User Story 7 (P2)**: Chaos driver → DEFERRED to 008-modulation-system

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core algorithm before integration
- Verify tests pass after implementation
- Cross-platform check (IEEE 754 functions)
- Commit LAST - commit completed work

### Parallel Opportunities

- All Setup tasks marked [P] can run in parallel (Phase 1)
- Once Phase 2 complete, User Stories 1-6 can start in parallel (if team capacity allows)
- All tests within a user story marked [P] can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch tests in parallel (once foundational phase complete):
Task T5.4a: "Write unit tests for 1D Linear mode"
Task T5.11a: "Write unit tests for morph smoothing"

# Launch implementation in sequence (tests must fail first):
Task T5.4: "Implement 1D Linear mode"
Task T5.11: "Implement morph smoothing"
```

---

## Implementation Strategy

### MVP First (User Stories 1-2 Only)

1. Complete Phase 1: Setup (data structures)
2. Complete Phase 2: Foundational (MorphEngine shell + weight computation)
3. Complete Phase 3: User Story 1 (basic A-B morphing)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Complete Phase 4: User Story 2 (2D Planar morph)
6. **STOP and VALIDATE**: Test User Story 2 independently
7. Deploy/demo if ready - Core morph system functional

### Incremental Delivery

1. Phases 1-2 → Foundation ready
2. Phase 3 (User Story 1) → Test independently → Basic morphing works (MVP!)
3. Phase 4 (User Story 2) → Test independently → 2D morphing works
4. Phase 5 (User Story 3) → Test independently → Same-family optimization
5. Phase 6 (User Story 4) → Test independently → Cross-family full capability
6. Phase 7 (User Story 5) → Test independently → Production-ready automation
7. Phase 8 (User Story 6) → Test independently → Radial mode alternative
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Phases 1-2 together
2. Once Phase 2 done:
   - Developer A: User Story 1 (Phase 3)
   - Developer B: User Story 2 (Phase 4)
   - Developer C: User Story 3 (Phase 5)
3. Stories complete and integrate independently

---

## Notes

- Task IDs follow condensed T5.x style from roadmap.md (Milestone M4: Tasks T5.1-T5.17)
- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability (US1-US7)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs\_architecture_\` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- User Story 7 (Chaos driver) deferred to 008-modulation-system spec per plan.md

---

## Summary

**Total User Stories**: 7 (6 implementable now, 1 deferred to 008-modulation-system)
**Total Tasks**: 80+ checkpoints (including sub-tasks)
**Parallel Opportunities**: Data structure creation (Phase 1), test writing within each story
**Independent Test Criteria**: Each user story has clear acceptance test in spec.md
**MVP Scope**: User Stories 1-2 (basic A-B morphing + 2D Planar)

**Task count per user story**:
- User Story 1 (Basic A-B morph): 12 tasks (T5.1-T5.14)
- User Story 2 (2D Planar): 5 tasks (T5.5)
- User Story 3 (Same-family): 5 tasks (T5.7)
- User Story 4 (Cross-family): 10 tasks (T5.8-T5.17)
- User Story 5 (Rapid automation): 5 tasks (T5.15)
- User Story 6 (Radial mode): 5 tasks (T5.6)
- User Story 7 (Chaos driver): DEFERRED (T5.16)
- Polish & CPU: 5 tasks (T5.16a-e)
- Documentation: 2 tasks (T5.18-19)
- Static Analysis: 4 tasks (T5.20-23)
- Completion Verification: 9 tasks (T5.24-32)

**Format validation**: ✅ ALL tasks follow checklist format (checkbox, ID, labels, file paths)
