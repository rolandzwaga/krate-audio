# Tasks: Intelligent Per-Band Oversampling

**Input**: Design documents from `/specs/009-intelligent-oversampling/`
**Prerequisites**: plan.md, spec.md (5 user stories, 20 FRs, 12 SCs), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**Roadmap Reference**: Phase 3, Week 11 (Tasks T11.1-T11.10)

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

### Build Commands

```bash
# Set CMake alias (Windows - Python cmake wrapper in PATH does NOT work)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Build Disrumpo plugin tests
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo_tests

# Run DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run Disrumpo tests
build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe

# Run all tests via CTest
ctest --test-dir build/windows-x64-release -C Release --output-on-failure

# Pluginval (after plugin code changes)
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"
```

**Note**: Plugin build may fail on post-build copy step (permission error) - this is fine, compilation succeeded.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create utility files and verify existing infrastructure

No new project setup needed - all infrastructure exists. This phase verifies dependencies are in place.

- [X] T11.001 Verify `getRecommendedOversample()` exists in `plugins/disrumpo/src/dsp/distortion_types.h` with all 26 types covered
- [X] T11.001b Verify `getRecommendedOversample()` returns the correct factor for each FR-014 category: 1 for Bitcrush/SampleReduce/Quantize/Aliasing/BitwiseMangler/Spectral, 2 for SoftClip/Tube/Tape/Temporal/FeedbackDist/Chaos/Formant/Granular/Fractal/Stochastic, 4 for HardClip/Fuzz/AsymmetricFuzz/SineFold/TriangleFold/SergeFold/FullRectify/HalfRectify/RingSaturation/AllpassResonant
- [X] T11.002 Verify `equalPowerGains()` and `crossfadeIncrement()` exist in `dsp/include/krate/dsp/core/crossfade_utils.h`
- [X] T11.003 Verify `Oversampler<2,2>` and `Oversampler<4,2>` exist in `dsp/include/krate/dsp/primitives/oversampler.h`
- [X] T11.004 Verify `kOversampleMaxId` parameter ID exists in `plugins/disrumpo/src/plugin_ids.h`
- [X] T11.005 Verify `MorphEngine::getWeights()` exists in `plugins/disrumpo/src/dsp/morph_engine.h`

**Checkpoint**: All dependencies confirmed - ready for utility implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core oversampling utilities that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Create Oversampling Utilities (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T11.006 [P] Create test file `plugins/disrumpo/tests/oversampling_utils_tests.cpp` with failing unit tests for `roundUpToPowerOf2Factor()` (FR-004)
- [X] T11.007 [P] Add failing unit tests for `getSingleTypeOversampleFactor()` with limit clamping (FR-007, FR-008)
- [X] T11.008 [P] Add failing unit tests for `calculateMorphOversampleFactor()` with all 26 types individually (FR-001, FR-002, SC-008)
- [X] T11.009 [P] Add failing unit tests for morph-weighted computation with 20+ weight combinations (FR-003, FR-004, SC-009). Include test cases: all nodes same type, equidistant weights, single dominant node (0.9/0.1 split), gradual transitions (0.7/0.3, 0.6/0.4), 3-node and 4-node morphs with varied distributions, boundary cases (1.0/0.0/0.0/0.0), and rounding thresholds (weights that produce 1.5, 2.5, 3.0 averages)

### 2.2 Implement Oversampling Utilities

- [X] T11.010 [P] Create `plugins/disrumpo/src/dsp/oversampling_utils.h` with `roundUpToPowerOf2Factor()` implementation
- [X] T11.011 [P] Implement `getSingleTypeOversampleFactor()` in `oversampling_utils.h` (wraps `getRecommendedOversample()` with limit clamping)
- [X] T11.012 [P] Implement `calculateMorphOversampleFactor()` in `oversampling_utils.h` with weighted average and rounding logic
- [X] T11.013 Verify all oversampling utility tests pass
- [X] T11.014 Build with zero compiler warnings

### 2.3 Cross-Platform Verification (MANDATORY)

- [X] T11.015 **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `plugins/disrumpo/tests/CMakeLists.txt`

### 2.4 Commit (MANDATORY)

- [X] T11.016 **Commit oversampling utilities and unit tests**

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Automatic Quality Without Thinking (Priority: P1) 🎯 MVP

**Goal**: Per-type oversampling profiles automatically apply the right amount of oversampling to each band based on which distortion algorithm is active (FR-001, FR-002, FR-014)

**Independent Test**: Load each of the 26 distortion types individually and verify that the correct oversampling factor is applied per band, delivering alias-free output for types that need it while preserving intentional artifacts for types that want them.

**Scope**: Covers all single-type (non-morph) oversampling factor selection. Does NOT include morph-weighted calculation (that's US2) or crossfade transitions (that's US4).

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T11.017 [P] [US1] Create test file `plugins/disrumpo/tests/oversampling_single_type_tests.cpp` with failing tests for all 26 types individually
- [X] T11.018 [P] [US1] Add failing tests for global limit clamping (FR-007, FR-008) for each type
- [X] T11.019 [P] [US1] Add failing tests for 1x bypass path (FR-020) - verify oversampler is skipped entirely for 1x types
- [X] T11.020 [P] [US1] Add failing alias suppression tests (SC-006): Process 5kHz sine wave at maximum drive through representative 2x/4x types, perform FFT analysis of output, verify aliasing artifacts are suppressed (>6dB for 4x, >3dB for 2x with IIR economy mode)

### 3.2 BandProcessor Modifications for Single-Type Selection

- [X] T11.021 [US1] Add `targetOversampleFactor_`, `crossfadeOldFactor_`, `crossfadeProgress_`, `crossfadeIncrement_`, `crossfadeActive_` members to `plugins/disrumpo/src/dsp/band_processor.h` (prepare state for future crossfade, but not used yet)
- [X] T11.021b [US1] Verify `prepare()` in `band_processor.h` pre-allocates both `oversampler2x_` and `oversampler4x_` regardless of current factor (per FR-009 - already exists, verify only)
- [X] T11.022 [P] [US1] Add `crossfadeOldLeft_` and `crossfadeOldRight_` buffers (`std::array<float, kMaxBlockSize>`) to BandProcessor (pre-allocate for future crossfade)
- [X] T11.022b [US1] Verify crossfade buffer sizes support worst-case dual-path output: `kMaxBlockSize` is sufficient because oversamplers internally manage upsampled buffers and only output base-rate samples to the provided callback buffers
- [X] T11.023 [US1] Implement `recalculateOversampleFactor()` method in `band_processor.h` that uses `getSingleTypeOversampleFactor()` for non-morph mode (FR-002, FR-017)
- [X] T11.024 [US1] Modify `setDistortionType()` in `band_processor.h` to call `recalculateOversampleFactor()` instead of directly setting factor
- [X] T11.025 [US1] Implement `processWithFactor()` method to route processing to 1x (direct), 2x, 4x, or 8x paths (refactor existing routing logic)
- [X] T11.026 [US1] Modify `processBlock()` to use `processWithFactor()` with `currentOversampleFactor_` (no crossfade yet - instant switching)
- [X] T11.027 [US1] Verify all single-type oversampling tests pass
- [X] T11.028 [US1] Build with zero compiler warnings

### 3.3 Bypass Optimization

- [X] T11.029 [US1] Modify `processBlock()` to skip oversampling entirely when band is bypassed (FR-012) - bit-transparent pass-through
- [X] T11.030 [P] [US1] Add failing bit-transparency test (SC-011): verify bypassed band output is bit-identical to input
- [X] T11.031 [US1] Verify bypass bit-transparency test passes

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T11.032 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `plugins/disrumpo/tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [X] T11.033 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed (single-type oversampling working, no morph yet)

---

## Phase 4: User Story 2 - Morph-Aware Oversampling (Priority: P2)

**Goal**: When morphing between types with different oversampling requirements, the system dynamically adjusts the oversampling factor based on a weighted average of the active nodes' recommendations (FR-003, FR-004)

**Independent Test**: Set up a 2-node morph between types with different oversampling requirements, automate the morph position, and verify the correct factor is selected at each position.

**Scope**: Morph-weighted factor computation. Does NOT include smooth transitions (that's US4).

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T11.034 [P] [US2] Create test file `plugins/disrumpo/tests/oversampling_morph_tests.cpp` with failing tests for 2-node morphs (SoftClip 2x + HardClip 4x)
- [X] T11.035 [P] [US2] Add failing tests for weighted average rounding (FR-004): verify 1.5->2, 2.5->4, 3.0->4
- [X] T11.036 [P] [US2] Add failing tests for 4-node morph with various weight combinations (SC-009)
- [X] T11.037 [P] [US2] Add failing tests for edge cases: all nodes same type, all nodes 1x, equidistant weights
- [X] T11.037b [P] [US2] Add failing test for morph transition threshold: Verify that morphing between SoftClip (2x) and HardClip (4x) transitions from factor 2 to factor 4 at the correct weight threshold

### 4.2 BandProcessor Morph Integration

- [X] T11.038 [US2] Modify `recalculateOversampleFactor()` in `band_processor.h` to detect morph mode and call `calculateMorphOversampleFactor()` (FR-003)
- [X] T11.039 [US2] Modify `setMorphPosition()` in `band_processor.h` to trigger `recalculateOversampleFactor()` after position change (FR-017)
- [X] T11.040 [US2] Modify `setMorphNodes()` in `band_processor.h` to trigger `recalculateOversampleFactor()` after node type changes (FR-017)
- [X] T11.041 [US2] Verify all morph-weighted oversampling tests pass
- [X] T11.042 [US2] Build with zero compiler warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T11.043 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `plugins/disrumpo/tests/CMakeLists.txt`

### 4.4 Commit (MANDATORY)

- [X] T11.044 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed (morph-weighted factor selection working, but still instant switching)

---

## Phase 5: User Story 3 - Global Limit for CPU Management (Priority: P3)

**Goal**: User-configurable Global Oversampling Limit parameter that caps all bands to a maximum factor (1x, 2x, 4x, or 8x) regardless of their computed recommendation (FR-005, FR-006, FR-007, FR-008, FR-015, FR-016)

**Independent Test**: Set the global limit parameter and verify that no band exceeds the limit regardless of its recommended factor.

**Scope**: Global limit parameter wiring. Does NOT include smooth transitions when limit changes (that's US4).

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T11.045 [P] [US3] Create test file `plugins/disrumpo/tests/oversampling_limit_tests.cpp` with failing tests for global limit clamping
- [ ] T11.046 [P] [US3] Add failing tests for limit 1x: all bands forced to 1x regardless of type
- [ ] T11.047 [P] [US3] Add failing tests for limit 2x: Fuzz (4x recommended) clamped to 2x
- [ ] T11.048 [P] [US3] Add failing tests for limit 4x (default): no clamping for types requiring ≤4x
- [ ] T11.049 [P] [US3] Add failing tests for limit changes during processing: verify all bands respect new limit
- [ ] T11.049b [P] [US3] Add failing test for parameter automation: simulate host sending rapid `kOversampleMaxId` changes (e.g., 4x→2x→4x within 1 second) and verify smooth crossfade transitions occur (FR-015)

### 5.2 Processor Parameter Wiring

- [ ] T11.050 [US3] Add `maxOversampleFactor_` atomic member to `plugins/disrumpo/src/processor/processor.h` (default 4)
- [ ] T11.050b [US3] Verify Controller registers `kOversampleMaxId` with default normalized value mapping to 4x (FR-006, verify only)
- [ ] T11.051 [US3] Implement `kOversampleMaxId` handling in `processParameterChanges()` in `processor.cpp`: map normalized [0,1] to {1,2,4,8}
- [ ] T11.052 [US3] Call `setMaxOversampleFactor()` on all 8 band processors when limit changes in `processor.cpp`
- [ ] T11.053 [US3] Modify `setMaxOversampleFactor()` in `band_processor.h` to trigger `recalculateOversampleFactor()` after clamping (FR-016)
- [ ] T11.054 [US3] Verify all global limit tests pass
- [ ] T11.055 [US3] Build with zero compiler warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T11.056 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `plugins/disrumpo/tests/CMakeLists.txt`

### 5.4 Commit (MANDATORY)

- [ ] T11.057 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed (global limit parameter functional, but still instant switching)

---

## Phase 6: User Story 4 - Smooth Factor Transitions (Priority: P4)

**Goal**: When the oversampling factor changes (due to type change, morph movement, or limit change), the system crossfades between the old and new oversampling paths over 8ms using equal-power gains to prevent audible artifacts (FR-010, FR-011, FR-016, FR-017, SC-005)

**Independent Test**: Rapidly switch between distortion types that require different factors and verify the output remains click-free through spectral and transient analysis.

**Scope**: Crossfade transition implementation. This is where the crossfade state members added in US1 actually get used.

**Hysteresis definition**: The system only triggers a crossfade when the newly computed factor differs from the currently active factor, preventing unnecessary transitions during continuous morphing within a single factor region (per FR-017).

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T11.058 [P] [US4] Create test file `plugins/disrumpo/tests/oversampling_crossfade_tests.cpp` with failing tests for 8ms crossfade duration (SC-005)
- [ ] T11.059 [P] [US4] Add failing tests for click-free transitions: Process sustained audio through 2x→4x, 4x→1x, 1x→2x transitions. Verify no sudden amplitude discontinuities >-60dBFS between consecutive samples at transition boundaries. Optionally use spectral analysis to detect transient clicks in high-frequency range
- [ ] T11.060 [P] [US4] Add failing tests for equal-power crossfade curve (FR-011): verify `oldGain² + newGain² = 1` throughout transition
- [ ] T11.061 [P] [US4] Add failing tests for abort-and-restart behavior (FR-010): rapid factor changes mid-transition
- [ ] T11.062 [P] [US4] Add failing tests for hysteresis (FR-017): continuous morph within same factor region does NOT trigger transitions

### 6.2 BandProcessor Crossfade Implementation

- [ ] T11.063 [US4] Implement `requestOversampleFactor()` method in `band_processor.h` with hysteresis: only trigger crossfade if target differs from current (FR-017)
- [ ] T11.064 [US4] Implement `startCrossfade()` private method in `band_processor.h`: initialize crossfade state with 8ms duration via `crossfadeIncrement(8.0f, sampleRate_)`
- [ ] T11.065 [US4] Implement `processBlockWithCrossfade()` private method in `band_processor.h`: dual-path processing with equal-power blending (FR-010, FR-011)
- [ ] T11.066 [US4] Modify `recalculateOversampleFactor()` in `band_processor.h` to call `requestOversampleFactor()` instead of directly setting factor
- [ ] T11.067 [US4] Modify `processBlock()` in `band_processor.h` to route to `processBlockWithCrossfade()` when `crossfadeActive_` is true
- [ ] T11.068 [US4] Implement abort-and-restart logic in `startCrossfade()`: if crossfade already active, make current "new" factor the new "old" factor and restart from progress 0.0
- [ ] T11.069 [US4] Verify all crossfade transition tests pass
- [ ] T11.070 [US4] Build with zero compiler warnings

### 6.3 Integration Tests

- [ ] T11.071 [P] [US4] Create test file `plugins/disrumpo/tests/oversampling_integration_tests.cpp` with multi-band scenarios
- [ ] T11.072 [P] [US4] Add integration test: 8 bands with different types and morph states, verify independent factor selection
- [ ] T11.073 [P] [US4] Add integration test: rapid type automation across multiple bands, verify no artifact accumulation
- [ ] T11.072b [P] [US4] Add integration test: Verify FR-017 triggers -- change each of the 4 conditions (type, morph position, morph nodes, global limit) and confirm `recalculateOversampleFactor()` is called and correct factor is computed. Use state inspection to verify factor selection after each trigger
- [ ] T11.074 [US4] Verify all integration tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T11.075 [US4] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `plugins/disrumpo/tests/CMakeLists.txt`

### 6.5 Commit (MANDATORY)

- [ ] T11.076 [US4] **Commit completed User Story 4 work**

**Checkpoint**: User Stories 1, 2, 3, AND 4 should all work independently and be committed (smooth crossfade transitions working)

---

## Phase 7: User Story 5 - Performance Optimization (Priority: P5)

**Goal**: The intelligent oversampling system meets defined CPU targets and adds minimal overhead compared to static oversampling (SC-001, SC-002, SC-003, SC-004, SC-007, SC-010)

**Independent Test**: Run performance benchmarks at various band counts and oversampling configurations, comparing against CPU budget targets.

**Scope**: Performance validation and optimization. This is the final polish story.

### 7.1 Performance Benchmark Tests

- [ ] T11.077 [P] [US5] Create test file `plugins/disrumpo/tests/oversampling_performance_tests.cpp` with CPU usage benchmarks
- [ ] T11.078 [P] [US5] Add benchmark for 4 bands @ 4x: target <15% CPU (SC-001)
- [ ] T11.079 [P] [US5] Add benchmark for 1 band @ 1x: target <2% CPU (SC-002)
- [ ] T11.080 [P] [US5] Add benchmark for 8 bands mixed: target <40% CPU (SC-003)
- [ ] T11.081 [P] [US5] Add benchmark for factor selection overhead: target <1% additional CPU (SC-007)
- [ ] T11.081b [P] [US5] Verify constant-time factor selection (FR-013): Profile `calculateMorphOversampleFactor()` with varying `activeNodeCount` (2, 3, 4 nodes) and confirm execution time is constant (max 4 nodes means O(4) = O(1))
- [ ] T11.082 [P] [US5] Add benchmark for bypassed band CPU: target near-zero (SC-010)

### 7.2 Latency Verification

- [ ] T11.083 [US5] Add test for latency reporting (SC-012): verify `BandProcessor::getLatency()` returns 0 samples (IIR mode, FR-018, FR-019)
- [ ] T11.083b [US5] Verify `BandProcessor::prepare()` calls `oversampler.prepare()` with `OversamplingMode::ZeroLatency` (FR-018, verify only)
- [ ] T11.084 [US5] Add test for latency stability: verify latency does not change when factors change dynamically

### 7.3 Performance Analysis & Optimization

- [ ] T11.085 [US5] Run all performance benchmarks and record baseline results
- [ ] T11.086 [US5] Profile hot paths if any targets are not met: identify bottlenecks
- [ ] T11.087 [US5] Optimize if needed: consider SIMD for crossfade blending, cache locality for factor selection
- [ ] T11.088 [US5] Re-run benchmarks and verify all SC-001 through SC-003, SC-007, SC-010 targets are met. If ANY target is not met, optimization is MANDATORY before claiming User Story 5 complete -- return to T11.087 to implement optimizations until all targets pass
- [ ] T11.089 [US5] Verify end-to-end latency does not exceed 10ms at highest quality (SC-004)

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T11.090 [US5] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `plugins/disrumpo/tests/CMakeLists.txt`

### 7.5 Commit (MANDATORY)

- [ ] T11.091 [US5] **Commit completed User Story 5 work**

**Checkpoint**: All user stories should now be independently functional, tested, committed, and meeting performance targets

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Final validation and cleanup

### 8.1 Pluginval Validation

- [ ] T11.092 Run pluginval on Disrumpo.vst3 and verify all tests pass at strictness level 5
- [ ] T11.093 Fix any pluginval failures related to oversampling changes

### 8.2 Comprehensive Testing

- [ ] T11.094 [P] Run all DSP tests and verify 100% pass rate
- [ ] T11.095 [P] Run all Disrumpo plugin tests and verify 100% pass rate
- [ ] T11.096 [P] Run CTest across all test suites and verify no failures
- [ ] T11.097 Manual smoke test: Load Disrumpo in DAW, test type switching, morph automation, limit parameter automation

### 8.3 Documentation

- [ ] T11.098 Update `quickstart.md` with any implementation discoveries or gotchas
- [ ] T11.099 Update `plan.md` with actual implementation details vs. initial plan (note any deviations)

---

## Phase N-1.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### N-1.0.1 Run Clang-Tidy Analysis

- [ ] T11.100 **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell from VS Developer PowerShell)
  cmake --preset windows-ninja  # One-time: generate compile_commands.json
  ./tools/run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja

  # Linux/macOS
  cmake --preset linux-release  # One-time: generate compile_commands.json
  ./tools/run-clang-tidy.sh --target disrumpo
  ```

### N-1.0.2 Address Findings

- [ ] T11.101 **Fix all errors** reported by clang-tidy (blocking issues)
- [ ] T11.102 **Review warnings** and fix where appropriate (use judgment for DSP code)
- [ ] T11.103 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for architecture documentation

---

## Phase N-2: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### N-2.1 Architecture Documentation Update

- [ ] T11.104 **Update `specs/_architecture_/`** with new components added by this spec:
  - Add `oversampling_utils.h` entry to plugin-level components (NOT shared DSP)
  - Document `calculateMorphOversampleFactor()`, `roundUpToPowerOf2Factor()`, `getSingleTypeOversampleFactor()` API
  - Document BandProcessor extensions: `requestOversampleFactor()`, `recalculateOversampleFactor()`, crossfade state
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples for per-type and morph-weighted oversampling
  - Note: This is Disrumpo-specific and NOT reusable by other plugins

### N-2.2 Final Commit

- [ ] T11.105 **Commit architecture documentation updates**
- [ ] T11.106 **Verify all spec work is committed to feature branch**

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase N-1: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### N-1.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T11.107 **Review ALL FR-001 through FR-020 requirements** from spec.md against implementation
- [ ] T11.108 **Review ALL SC-001 through SC-012 success criteria** and verify measurable targets are achieved
- [ ] T11.109 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements (48dB aliasing suppression, 8ms crossfade, CPU targets)
  - [ ] No features quietly removed from scope (all 26 types covered, morph weighting works, limit parameter functional, crossfade smooth)

### N-1.2 Fill Compliance Table in spec.md

- [ ] T11.110 **Update spec.md "Implementation Verification" section** with compliance status for each FR and SC
- [ ] T11.111 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### N-1.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T11.112 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase N: Final Completion

**Purpose**: Final commit and completion claim

### N.1 Final Commit

- [ ] T11.113 **Commit all spec work** to feature branch `009-intelligent-oversampling`
- [ ] T11.114 **Verify all tests pass** (DSP, plugin, integration, performance)

### N.2 Completion Claim

- [ ] T11.115 **Claim completion ONLY if all 20 FRs and 12 SCs are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P2 → P3 → P4 → P5)
- **Polish (Phase 8)**: Depends on all desired user stories being complete
- **Static Analysis (Phase N-1.0)**: Depends on all implementation complete
- **Architecture Docs (Phase N-2)**: Depends on static analysis being clean
- **Completion Verification (Phase N-1)**: Depends on architecture docs being updated
- **Final Completion (Phase N)**: Depends on honest verification passing

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Depends on US1 (needs single-type selection infrastructure) but extends it independently
- **User Story 3 (P3)**: Depends on US1 (needs `recalculateOversampleFactor()`) but can run in parallel with US2
- **User Story 4 (P4)**: Depends on US1, US2, US3 (needs all factor selection logic to add transitions)
- **User Story 5 (P5)**: Depends on US1-US4 (validates final performance)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation makes tests pass
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Setup (Phase 1)**: All verification tasks T11.001-T11.005 can run in parallel
- **Foundational (Phase 2)**: All test creation tasks T11.006-T11.009 can run in parallel; all implementation tasks T11.010-T11.012 can run in parallel
- **User Story 1**: Test creation tasks T11.017-T11.020 can run in parallel; implementation tasks T11.021-T11.022 can run in parallel
- **User Story 2**: Test creation tasks T11.034-T11.037 can run in parallel
- **User Story 3**: Test creation tasks T11.045-T11.049 can run in parallel
- **User Story 4**: Test creation tasks T11.058-T11.062 can run in parallel; integration test tasks T11.071-T11.072 can run in parallel
- **User Story 5**: Benchmark creation tasks T11.077-T11.082 can run in parallel
- **After Foundational phase completes**: User Stories 1, 2, 3 can start in parallel (US4 and US5 must wait for earlier stories)
- **Polish (Phase 8)**: Tasks T11.094-T11.096 can run in parallel

---

## Parallel Example: Foundational Phase

```bash
# Launch all test files in parallel:
Task T11.006: Create oversampling_utils_tests.cpp
Task T11.007: Add limit clamping tests
Task T11.008: Add per-type tests
Task T11.009: Add morph-weighted tests

# Then launch all implementations in parallel:
Task T11.010: Implement roundUpToPowerOf2Factor()
Task T11.011: Implement getSingleTypeOversampleFactor()
Task T11.012: Implement calculateMorphOversampleFactor()
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (verify dependencies)
2. Complete Phase 2: Foundational (oversampling utilities)
3. Complete Phase 3: User Story 1 (per-type automatic selection)
4. **STOP and VALIDATE**: Test User Story 1 independently (single-type oversampling works, no morph yet)
5. Demo if ready (shows automatic quality optimization for 26 types)

### Incremental Delivery

1. Complete Setup + Foundational → Utility functions ready
2. Add User Story 1 → Test independently → Demo (MVP: per-type automatic oversampling)
3. Add User Story 2 → Test independently → Demo (morph-aware oversampling)
4. Add User Story 3 → Test independently → Demo (global limit parameter)
5. Add User Story 4 → Test independently → Demo (smooth transitions, no clicks)
6. Add User Story 5 → Test independently → Demo (performance targets met)
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (per-type selection)
   - Developer B: User Story 2 (morph-aware selection) - starts after US1 basics exist
   - Developer C: User Story 3 (global limit parameter)
3. Once US1-US3 complete:
   - Developer A: User Story 4 (crossfade transitions)
4. Once US1-US4 complete:
   - Developer A or B: User Story 5 (performance validation)
5. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Run clang-tidy before claiming completion (Phase N-1.0)
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Total tasks: 115 (T11.001 - T11.115)
- Task IDs match roadmap: Week 11 tasks (T11.1-T11.10 expanded to detailed implementation steps)
- All 20 FRs and 12 SCs mapped to specific tasks
- All 5 user stories independently testable
- MVP = User Story 1 only (per-type automatic oversampling)
- Full feature = All 5 user stories (includes morph-aware, global limit, smooth transitions, performance validation)
