# Tasks: Sympathetic Resonance

**Input**: Design documents from `/specs/132-sympathetic-resonance/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

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

### Integration Tests (MANDATORY When Applicable)

The sympathetic resonance component wires into the Innexus processor's per-sample audio loop, parameter dispatch (`processParameterChanges()`), MIDI handlers (`handleNoteOn`/`handleNoteOff`), and state save/load. Integration tests covering all four wiring points are required in addition to DSP unit tests.

Key rules:
- **Behavioral correctness over existence checks**: Verify sympathetic output is audibly different from dry sum, not just that it compiles. "Output is non-zero" is not a valid integration test.
- **Test degraded host conditions**: Not just ideal `kPlaying | kTempoValid` -- also no transport, no tempo, `nullptr` process context.
- **Test per-block configuration safety**: Ensure `setAmount()`/`setDecay()` called every block in `applyParamsToEngine()` do not silently reset the resonator pool or smoother.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (DSP tests) or `plugins/innexus/tests/CMakeLists.txt` (plugin tests)
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/systems/sympathetic_resonance_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

---

## Phase 1: Setup (Branch & Build Baseline)

**Purpose**: Verify the correct branch is checked out and confirm the existing test suites are green before any modifications begin.

- [X] T001 Verify the `132-sympathetic-resonance` feature branch is checked out (`git branch` -- if on `main`, STOP and create/checkout the feature branch before proceeding)
- [X] T002 Build `dsp_tests` and verify all existing DSP tests pass before any changes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [X] T003 Build `innexus_tests` and verify all existing Innexus tests pass before any changes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

**Checkpoint**: Green baseline confirmed on the correct branch -- implementation can now begin.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Establish shared structural scaffolding that all user stories depend on. These additions have no logic yet -- they register build targets, allocate parameter IDs, and declare the public API so that story-specific phases can compile.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T004 Add sympathetic resonance parameter IDs to `plugins/innexus/src/plugin_ids.h`: `kSympatheticAmountId = 860`, `kSympatheticDecayId = 861`
- [X] T005 Add `sympathetic_resonance_test.cpp` as a source entry in `dsp/tests/CMakeLists.txt` (file does not yet exist -- register it so the build system is ready)
- [X] T006 Add `sympathetic_resonance_integration_test.cpp` as a source entry in `plugins/innexus/tests/CMakeLists.txt` for the `innexus_tests` target (file does not yet exist -- register it so the build system is ready)
- [X] T007 Add `sympathetic_resonance_simd.cpp` as a source entry in `dsp/CMakeLists.txt` alongside the existing SIMD source files (file does not yet exist -- register it so Phase 7 SIMD work has a build slot ready)

**Checkpoint**: Build scaffolding ready. Parameter IDs allocated. Test files registered. Implementation can now begin.

---

## Phase 3: User Story 1 - Cross-Voice Harmonic Reinforcement (Priority: P1) - MVP

**Goal**: Implement the scalar `SympatheticResonance` DSP class at Layer 3 with the full resonator pool, second-order driven resonator recurrence, pool management (add/merge/evict/reclaim), inharmonicity-adjusted partial frequencies, and per-resonator gain weighting. This delivers the core value proposition: chords produce interval-dependent harmonic reinforcement that is physically correct and musically usable.

**Independent Test**: Instantiate `SympatheticResonance` directly in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp`. Call `prepare(44100.0)`, set amount to 0.5, call `noteOn` for two voices an octave apart (e.g., 440 Hz + 880 Hz, 4 partials each), and drive the resonators with a sine burst. Verify the output contains spectral energy at the shared harmonic frequencies. No plugin, controller, or MIDI handler is required -- pure DSP class. Also verify that with Amount=0.0, `process()` returns exactly 0.0 for any input.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T008 [US1] Write failing tests in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` covering:
  - Lifecycle: `prepare(44100.0)` does not crash; `reset()` clears y1/y2 state; `getActiveResonatorCount()` returns 0 after reset
  - Bypass: Amount=0.0 returns exactly 0.0 for any input (FR-014)
  - noteOn basic: After `noteOn(0, partials)`, `getActiveResonatorCount()` equals `kSympatheticPartialCount` (= 4) (FR-008, FR-020)
  - noteOff orphan: After `noteOff(0)`, `getActiveResonatorCount()` still equals 4 (resonators ring out, not immediately reclaimed) (FR-009)
  - Resonator reclaim: After driving resonators to steady state then removing input (silence), resonators are eventually reclaimed below -96 dB threshold (FR-009)
  - Pool cap: Adding more than 64 resonators (noteOn for 17 voices x 4 partials) does not exceed `kMaxSympatheticResonators`; quietest is evicted (FR-010)
  - Merge: Two voices with fundamentals within 0.3 Hz are merged (refCount increases, resonator count stays at 4, not 8) (FR-011, FR-008)
  - No merge for near-unison: Two voices ~1 Hz apart (440 Hz and 441 Hz) produce 8 separate resonators (one set per voice), not 4 merged ones -- preserving beating potential (FR-008, US5)
  - True duplicate merge: Re-triggering the same voiceId always merges, not adds (FR-011)
  - Inharmonicity: `noteOn` with a non-zero B coefficient produces partial frequencies matching `f_n = n * f0 * sqrt(1 + B * n^2)` passed through `SympatheticPartialInfo.frequencies` (FR-018, FR-020) -- verify the resonator `freq` field matches the passed frequencies
  - Second-order recurrence coefficients: For a known (f, Q, sampleRate) triplet, verify `r`, `omega`, and `coeff` (= 2*r*cos(omega)) fields are computed correctly in the pool (FR-006)
  - Frequency-dependent Q: At f=1000 Hz with Q_user=400, verify Q_eff = 200 (= 400 * clamp(500/1000, 0.5, 1.0) = 400 * 0.5); at f=200 Hz, verify Q_eff = 400 (= 400 * clamp(500/200, 0.5, 1.0) = 400 * 1.0 clamped) (FR-013)
  - Per-resonator gain: Partial 1 has gain = `1.0`, partial 2 has gain = `1/sqrt(2)`, partial 4 has gain = `0.5` (FR-007 — the `amount` factor is applied per-sample in `process()` via `scaledInput = input * smoothedCouplingGain`, NOT stored in the per-resonator gain field)
  - Sample rate scaling: `prepare(96000.0)` produces different `r` and `omega` values than `prepare(44100.0)` for the same frequency (FR-022)
  - Harmonic hierarchy (SC-002, SC-014): Process a sustained 440+880 Hz octave pair vs 440+660 Hz fifth pair (same amount); octave pair produces more total sympathetic energy than fifth pair
  - Dissonant interval (SC-006): 440 Hz + 466 Hz (minor second) produces near-zero sympathetic output compared to the octave pair (very few shared harmonics)
  - Self-excitation inaudibility (FR-021, SC-001): Drive a single-voice scenario (one voice's partials feeding into its own resonators through the global sum) with Amount=0.5; verify the output contains only a subtle sustain extension with no audible ringing artefacts -- peak sympathetic amplitude must not exceed -40 dB relative to the dry voice output
  - Unidirectional coupling (FR-005): Process the sympathetic output; verify that feeding it back into the input does not cause runaway growth (the component itself enforces no feedback path -- test that the process() output stays bounded for 10000 samples)

### 3.2 Implementation for User Story 1

- [X] T009 [US1] Create `dsp/include/krate/dsp/systems/sympathetic_resonance.h` with:
  - `SympatheticPartialInfo` struct (`std::array<float, kSympatheticPartialCount> frequencies`)
  - Private `ResonatorState` struct with all fields from data-model.md: `freq`, `omega`, `r`, `coeff`, `rSquared`, `y1`, `y2`, `gain`, `envelope`, `voiceId`, `partialNumber`, `refCount`, `active`
  - SoA layout for the pool: six `std::array<float, kMaxSympatheticResonators>` arrays (`freqs_`, `coeffs_`, `rSquareds_`, `y1s_`, `y2s_`, `gains_`) plus `std::array<float, kMaxSympatheticResonators> envelopes_`, `std::array<int32_t, kMaxSympatheticResonators> voiceIds_`, `std::array<int, kMaxSympatheticResonators> refCounts_`, `std::array<bool, kMaxSympatheticResonators> actives_` (SoA now for SIMD-readiness per plan.md)
  - `SympatheticResonance` class with public API matching `contracts/sympathetic_resonance_api.h` exactly: `prepare()`, `reset()`, `setAmount()`, `setDecay()`, `noteOn()`, `noteOff()`, `process()`, `getActiveResonatorCount()`, `isBypassed()`
  - Private helpers: `computeResonatorCoeffs(float f, float Q_eff, float sampleRate)`, `computeFreqDependentQ(float Q_user, float f)`, `findMergeCandidate(float freq)`, `evictQuietest()`
  - `Biquad antiMudHpf_` member for output high-pass filter (reuse `dsp/include/krate/dsp/primitives/biquad.h`)
  - `OnePoleSmoother amountSmoother_` member for coupling amount (reuse `dsp/include/krate/dsp/primitives/smoother.h`)
  - `float userQ_`, `float envelopeReleaseCoeff_`, `float sampleRate_` members
  - All constants: `kSympatheticPartialCount = 4`, `kMaxSympatheticResonators = 64`, `kMergeThresholdHz = 0.3f`, `kReclaimThresholdLinear = 1.585e-5f`, `kAntiMudFreqRef = 100.0f`, `kQFreqRef = 500.0f`, `kMinQScale = 0.5f`
  - No Highway headers in this file (SIMD will be behind a separate API in Phase 7)

- [X] T010 [US1] Implement `prepare(double sampleRate)` in `dsp/include/krate/dsp/systems/sympathetic_resonance.h`:
  - Store `sampleRate_`
  - Compute `envelopeReleaseCoeff_ = exp(-1.0f / (0.010f * sampleRate_))` (10ms release tau)
  - Configure `antiMudHpf_` as a high-pass filter at `kAntiMudFreqRef` Hz (use `Biquad::configure(FilterType::HighPass, kAntiMudFreqRef, 0.707f, 0.0f, sampleRate_)`)
  - Configure `amountSmoother_` with 5ms smoothing time
  - Zero-initialize all pool arrays, set `activeCount_ = 0`

- [X] T011 [US1] Implement `noteOn(int32_t voiceId, const SympatheticPartialInfo& partials)` in `dsp/include/krate/dsp/systems/sympathetic_resonance.h`:
  - For same voiceId re-trigger: first call `noteOff(voiceId)` to orphan existing resonators, then add fresh resonators (FR-011 -- true duplicates merge by replacing)
  - For each of the `kSympatheticPartialCount` partial frequencies in `partials.frequencies`:
    - Skip if frequency <= 0 or >= sampleRate/2
    - Search pool for existing resonator with `|freq - f_new| < kMergeThresholdHz` (FR-008)
    - If merge candidate found: update frequency to weighted average `(f_existing * refCount + f_new) / (refCount + 1)`, recompute coefficients, increment `refCount`; do NOT add new slot
    - If no merge candidate: acquire a free slot (or evict quietest via `evictQuietest()` if pool is full); initialize all fields including `Q_eff = computeFreqDependentQ(userQ_, f_new)`, set `voiceId`, `partialNumber`, `gain = 1.0f / std::sqrt(static_cast<float>(partialNumber))` (amount is applied per-sample in `process()` via `scaledInput`, NOT baked into per-resonator gain — this ensures gain tracks the smoother in real-time)
  - Increment `activeCount_` only for genuinely new (non-merged) slots

- [X] T012 [US1] Implement `noteOff(int32_t voiceId)` in `dsp/include/krate/dsp/systems/sympathetic_resonance.h`:
  - For each active resonator with matching `voiceId`: set `voiceId = -1` (orphaned/ringing out), decrement `refCount`; if `refCount == 0`, leave active (will ring out); if `refCount > 0` (shared resonator), another voice still owns it -- do not orphan
  - Do NOT deactivate or reclaim immediately (FR-009)

- [X] T013 [US1] Implement `process(float input)` scalar loop in `dsp/include/krate/dsp/systems/sympathetic_resonance.h`:
  - Early-out: if `isBypassed()`, return 0.0f immediately (FR-014)
  - Advance `amountSmoother_` to get current smoothed amount (FR-023)
  - Scaled input: `scaledInput = input * smoothedAmount`
  - For each active resonator in pool (branchless inner loop using `actives_[]`):
    - Compute recurrence: `y = coeff * y1 - rSquared * y2 + scaledInput * gain`
    - Update `y2 = y1`, `y1 = y`
    - Update envelope follower: `envelope = max(abs(y), envelope * releaseCoeff)`
    - Accumulate to `sum += y`
    - If `envelope < kReclaimThresholdLinear`: set `active = false`, decrement `activeCount_`
  - Apply anti-mud HPF to `sum`: `output = antiMudHpf_.process(sum)`
  - Return `output`

- [X] T014 [US1] Verify all User Story 1 DSP tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "SympatheticResonance*" 2>&1 | tail -10`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T015 [US1] Verify IEEE 754 compliance: check `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage; if present, add the file to the `-fno-fast-math` compile flag list in `dsp/tests/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [X] T016 [US1] Commit completed User Story 1 work: `SympatheticResonance` scalar class + DSP unit tests + build registration (T004-T007, T008-T015)

**Checkpoint**: User Story 1 -- the core resonator pool and cross-voice harmonic reinforcement -- is fully functional, tested, and committed. Can play octave chords through the DSP class and verify interval-dependent reinforcement.

---

## Phase 4: User Story 2 - Sympathetic Amount Control (Priority: P1)

**Goal**: Deliver the `setAmount()` parameter pathway with zero-bypass at Amount=0.0 and smooth parameter transitions. The amount maps to coupling gain (-40 dB to -20 dB). The `OnePoleSmoother` prevents clicks at parameter changes. This story depends on the resonator pool from US1 but adds the full parameter control layer.

**Independent Test**: Instantiate `SympatheticResonance`, call `prepare(44100.0)`, trigger a voice with `noteOn`, then: (1) verify Amount=0.0 produces exactly 0.0 output with zero resonator activity; (2) sweep Amount from 0.0 to 1.0 over 1000 samples while feeding a sustained sine and verify no sample-level discontinuity (amplitude delta < 0.01 per sample); (3) verify Amount=1.0 produces significantly more output energy than Amount=0.1 for the same input.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T017 [US2] Write failing tests in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` (append to existing file) covering:
  - Zero bypass: Amount=0.0 produces exactly 0.0 for any input; `isBypassed()` returns true (FR-014, SC-009)
  - Non-zero activation: Amount=0.001 (just above zero) produces non-zero output for a sustained sine input with active resonators (FR-014)
  - Amount range: Amount=1.0 produces more total output energy than Amount=0.1 for the same 1000-sample sine burst with active resonators (spec: -40 dB at low, -20 dB at high)
  - Smooth transition: Sweep Amount from 0.0 to 1.0 over 500 samples; verify no single sample has output change > 0.01 (no clicks/pops) (FR-023, US2 acceptance scenario 4)
  - Smooth transition down: Sweep Amount from 1.0 to 0.0 over 500 samples; same smoothness criterion (FR-023)
  - snapTo: After `prepare()`, `setAmount(0.0)`, verify `isBypassed()` returns true immediately (smoother at 0)
  - `setAmount()` called every block: Calling `setAmount(0.5)` on every process block for 100 blocks does not reset resonator state or pool (per-block configuration safety)

### 4.2 Implementation for User Story 2

- [X] T018 [US2] Implement `setAmount(float amount)` in `dsp/include/krate/dsp/systems/sympathetic_resonance.h`:
  - Map normalized amount [0.0, 1.0] to coupling gain: `couplingGain_ = (amount == 0.0f) ? 0.0f : std::pow(10.0f, (-40.0f + 20.0f * amount) / 20.0f)` (maps 0.0 -> -40 dB = 0.01, 1.0 -> -20 dB = 0.1)
  - Set smoother target: `amountSmoother_.setTarget(couplingGain_)`
  - `isBypassed()` returns true when `amountSmoother_.getCurrentValue() == 0.0f && couplingGain_ == 0.0f`

- [X] T019 [US2] Verify all User Story 2 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "SympatheticResonance*" 2>&1 | tail -10`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T020 [US2] Verify IEEE 754 compliance for any new test code added in T017; add to `-fno-fast-math` list if needed

### 4.4 Commit (MANDATORY)

- [X] T021 [US2] Commit completed User Story 2 work: amount control implementation + tests

**Checkpoint**: User Story 2 -- sympathetic amount control -- is fully functional, tested, and committed. Amount=0.0 is a true zero-cost bypass.

---

## Phase 5: User Story 3 - Sympathetic Decay Control (Priority: P1)

**Goal**: Deliver the `setDecay()` parameter pathway controlling Q-factor from 100 (Decay=0.0) to 1000 (Decay=1.0). Q affects newly added resonators only (existing resonators keep their coefficients -- physically correct). Frequency-dependent Q (`Q_eff = Q_user * clamp(500/f, 0.5, 1.0)`) is already in the resonator initialization path from US1; this story adds the user-facing parameter mapping and verifies the full Q range behavior.

**Independent Test**: Call `setDecay(0.0)` then `noteOn` for a 440 Hz voice; verify `Q_eff` stored in the resonator pool is ~100. Call `reset()`, `setDecay(1.0)`, `noteOn` again; verify `Q_eff` is ~1000. Drive both pools with the same impulse and measure the -60 dB ring-out time -- high-Q version must ring significantly longer.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T022 [US3] Write failing tests in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` (append) covering:
  - Q range low: `setDecay(0.0)` followed by `noteOn` with 440 Hz partial; verify resonator `r` pole radius is consistent with Q=100 at 440 Hz: `r = exp(-pi * (440.0/100.0) / 44100.0)` (FR-006)
  - Q range high: `setDecay(1.0)` followed by `noteOn` with 440 Hz partial; verify `r` is consistent with Q=1000 at 440 Hz (FR-006)
  - Existing resonators unchanged: `noteOn` with Decay=0.5, then `setDecay(1.0)`; verify existing resonator `r` is unchanged (Q does not change mid-vibration) (research.md R-005)
  - Frequency-dependent Q at 440 Hz: `setDecay(x)` -> Q_user; at 440 Hz, `Q_eff = Q_user * clamp(500/440, 0.5, 1.0) = Q_user * 1.0` (below 500 Hz, full Q) (FR-013)
  - Frequency-dependent Q at 1000 Hz: at 1000 Hz, `Q_eff = Q_user * 0.5` (FR-013)
  - Frequency-dependent Q at 2000+ Hz: at 2000 Hz, `Q_eff = Q_user * clamp(500/2000, 0.5, 1.0) = Q_user * 0.5` (clamped at minimum) (FR-013)
  - Ring-out duration: decay=0.0 (Q=100) ring-out for 440 Hz is < 200ms to -60 dB; decay=1.0 (Q=1000) ring-out is > 1500ms to -60 dB (verify time constant difference is order-of-magnitude) (SC-013, US3 acceptance scenarios 1-2)
  - Smooth decay sweep: `setDecay()` called while resonators are active; new resonators added after decay change use the new Q; no crash or assertion failure (US3 acceptance scenario 3)

### 5.2 Implementation for User Story 3

- [X] T023 [US3] Implement `setDecay(float decay)` in `dsp/include/krate/dsp/systems/sympathetic_resonance.h`:
  - Map normalized decay [0.0, 1.0] to Q: `userQ_ = 100.0f * std::pow(10.0f, decay)` (maps 0.0 -> Q=100, 1.0 -> Q=1000 via logarithmic mapping)
  - Store as `userQ_` member; do NOT recompute coefficients for existing active resonators (only new noteOns use the updated Q)

- [X] T024 [US3] Verify all User Story 3 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "SympatheticResonance*" 2>&1 | tail -10`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T025 [US3] Verify IEEE 754 compliance for any new test code added in T022; add to `-fno-fast-math` list if needed

### 5.4 Commit (MANDATORY)

- [X] T026 [US3] Commit completed User Story 3 work: decay control implementation + tests

**Checkpoint**: User Story 3 -- sympathetic decay control -- is fully functional, tested, and committed. Short wash vs crystalline ring behavior verified.

---

## Phase 6: User Story 4 - Sympathetic Ring-Out After Voice Steal (Priority: P2)

**Goal**: Verify and solidify the ring-out behavior after voice release or steal. The `noteOff` path orphans resonators (voiceId=-1) but keeps them active, and the amplitude-based reclaim threshold (-96 dB) drives the actual deactivation. This story adds explicit integration tests that validate ring-out persists naturally and pool capacity is correctly managed under rapid voice stealing.

**Independent Test**: Call `noteOn(0, partials)`, let resonators build up for 100ms of samples, then call `noteOff(0)`. Verify that immediately after `noteOff`, `getActiveResonatorCount()` is still `kSympatheticPartialCount` (resonators are orphaned, not reclaimed). Continue processing silence for several seconds and verify resonators eventually reach 0 count as amplitude decays below -96 dB. Time the decay to confirm it matches the expected Q-determined ring-out rate.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T027 [US4] Write failing tests in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` (append) covering:
  - Ring-out persists: After `noteOff(0)`, resonators remain active (`getActiveResonatorCount() == 4`); processing silence for 1 block does not reclaim them (FR-009, US4 acceptance scenario 1)
  - Natural decay: After `noteOff(0)` and processing silence for `N` samples (N chosen to be below the ring-out time), resonators are still present; after `M >> N` samples, they are reclaimed (FR-009)
  - -96 dB threshold: Resonators are reclaimed when envelope drops below `kReclaimThresholdLinear = 1.585e-5f` (FR-009, US4 acceptance scenario 2)
  - Pool recovery: Pool capacity is fully recovered after resonators decay out (activeCount returns to 0), allowing new noteOns to use the full pool again (SC-010)
  - Voice steal sequence: noteOn(0), noteOn(1), ..., noteOn(17) (17 voices x 4 partials = 68 > 64 cap); verify `getActiveResonatorCount() == 64` at all times (FR-010, SC-010)
  - Quietest eviction: After filling the pool, verify that when a new noteOn triggers eviction, the evicted resonator is the one with the lowest envelope value (FR-010)
  - Rapid tremolo: 100 rapid noteOn/noteOff cycles (same voice, 1ms apart) do not crash, leak memory, or exceed pool cap (edge case from spec)

### 6.2 Implementation for User Story 4

- [ ] T028 [US4] Review and harden `noteOff()` implementation from T012 to ensure refCount is decremented correctly for merged resonators -- a shared resonator (refCount > 1) should only be orphaned when its last owner calls noteOff (FR-009)
- [ ] T029 [US4] Review and harden `process()` reclaim logic from T013 to ensure envelope-based reclaim correctly decrements `activeCount_` and marks the slot as inactive so subsequent noteOns can reuse it (FR-009, FR-010)
- [ ] T030 [US4] Verify all User Story 4 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "SympatheticResonance*" 2>&1 | tail -10`

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T031 [US4] Verify IEEE 754 compliance for any new test code; add to `-fno-fast-math` list if needed

### 6.4 Commit (MANDATORY)

- [ ] T032 [US4] Commit completed User Story 4 work: ring-out + pool management hardening + tests

**Checkpoint**: User Story 4 -- natural ring-out after voice steal -- is fully functional, tested, and committed. Pool management is solid under rapid voice stealing.

---

## Phase 7: User Story 5 - Near-Unison Beating (Priority: P2)

**Goal**: Verify that the 0.3 Hz merge threshold correctly preserves near-unison beating. Two voices ~1 Hz apart (440 Hz vs 441 Hz) must NOT be merged, producing two separate resonators that interfere naturally. Two voices within ~0.3 Hz (440.1 Hz vs 439.9 Hz) MUST be merged to avoid redundancy. This story validates the merge threshold boundary condition that distinguishes this implementation from a simple reverb.

**Independent Test**: Call `noteOn(0, partials_at_440Hz)` and `noteOn(1, partials_at_441Hz)`. Verify `getActiveResonatorCount() == 8` (separate resonators, not merged). Drive with a burst and measure amplitude modulation in the output at ~1 Hz. Then call `noteOn(2, partials_at_440_1Hz)` and `noteOn(3, partials_at_439_9Hz)`. Verify `getActiveResonatorCount() == 4` (merged) with weighted-average frequency = 440.0 Hz.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T033 [US5] Write failing tests in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` (append) covering:
  - No merge at 1 Hz separation: voices at 440 Hz and 441 Hz produce 8 active resonators (FR-008, US5 acceptance scenario 1, SC-008)
  - Beating present: Drive the 440/441 Hz pair with a sustained input; verify amplitude modulation at approximately 1 Hz in the output over 3+ seconds of samples (SC-008)
  - Merge at 0.2 Hz separation: voices at 440.1 Hz and 439.9 Hz produce 4 active resonators (merged) (FR-008, US5 acceptance scenario 2)
  - Merged frequency: After merging 440.1 and 439.9, the resonator's `freq` field equals 440.0 Hz (weighted average with equal refCounts) (FR-008)
  - Boundary at exactly 0.3 Hz: voices at 440.0 Hz and 440.3 Hz -- this is the exact threshold, verify the implementation is consistent (either both cases handled the same, or document which way the boundary goes)
  - No merge across partial numbers: partial 1 of voice A (440 Hz) vs partial 1 of voice B (441 Hz) -- these are correctly NOT merged; partial 1 of voice A (440 Hz) vs partial 2 of voice B (220 Hz) -- also not merged despite being different partials (frequency separation >> 0.3 Hz)

### 7.2 Implementation for User Story 5

- [ ] T034 [US5] Review `findMergeCandidate()` implementation from T009 to ensure the 0.3 Hz threshold is `|f_existing - f_new| < kMergeThresholdHz` (strict less-than, correct direction) and does NOT merge across partial numbers if that matters for the use case
- [ ] T035 [US5] Verify all User Story 5 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "SympatheticResonance*" 2>&1 | tail -10`

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T036 [US5] Verify IEEE 754 compliance for any new test code; add to `-fno-fast-math` list if needed

### 7.4 Commit (MANDATORY)

- [ ] T037 [US5] Commit completed User Story 5 work: merge threshold validation + beating tests

**Checkpoint**: User Story 5 -- near-unison beating -- is fully functional, tested, and committed. The 0.3 Hz threshold correctly preserves beating and prevents redundant merging.

---

## Phase 8: User Story 6 - Dense Chord Clarity (Priority: P2)

**Goal**: Validate the anti-mud filter system under dense chord conditions. The output high-pass filter (Biquad at ~100 Hz, 6 dB/oct) prevents sub-bass buildup, and frequency-dependent Q (already implemented in the resonator coefficients) ensures high-frequency partials decay faster. This story adds tests specifically targeting anti-mud effectiveness with 4+ simultaneous voices.

**Independent Test**: Trigger 4+ voices spanning the full pitch range, run the sympathetic resonance with Amount=0.8 for 2000 samples, then apply FFT analysis to the output. Verify that energy below 80 Hz is substantially attenuated compared to the 200-1000 Hz band. Also verify that resonators at 2000+ Hz have shorter ring-out times than resonators at 200 Hz (frequency-dependent Q).

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T038 [US6] Write failing tests in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` (append) covering:
  - Anti-mud HPF active: Trigger a 60 Hz resonator (low bass partial), drive it to steady state, verify the anti-mud HPF substantially attenuates the output at 60 Hz relative to a 500 Hz resonator driven with equal amplitude (FR-012, SC-012)
  - No buildup below 80 Hz: With 4 simultaneous voices (low bass chord), verify sympathetic output has minimal energy below 80 Hz after anti-mud filter (FR-012)
  - HPF frequency response: Verify the anti-mud HPF gain at 100 Hz (f_ref) is ~0.5 (-6 dB) and at 200 Hz is ~0.8, consistent with `gain(f) = 1 / (1 + (100/f)^2)` (FR-012)
  - Frequency-dependent Q comparison: noteOn a resonator at 200 Hz with Q_user=400 (Q_eff=400, full Q below 500 Hz) and another at 1000 Hz with same Q_user=400 (Q_eff=200, half Q above 500 Hz); verify the 1000 Hz resonator decays to -60 dB approximately twice as fast as the 200 Hz one (FR-013, SC-013)
  - Dense chord clarity: 4-voice chord (C2/E2/G2/C3, fundamentals ~65/82/98/130 Hz) -- sympathetic output after anti-mud has no energy below 80 Hz and clear output in the 100-500 Hz range (FR-012, US6 acceptance scenario 1, SC-012)
  - Biquad reset: After `reset()`, the anti-mud HPF state is cleared (no DC offset or residual state from previous notes) (FR-012)

### 8.2 Implementation for User Story 6

- [ ] T039 [US6] Verify the `antiMudHpf_` configuration in `prepare()` (from T010) uses the correct HPF formula: `Biquad::configure(FilterType::HighPass, kAntiMudFreqRef, 0.707f, 0.0f, sampleRate_)` which produces the 6 dB/oct roll-off matching `gain(f) = 1 / (1 + (f_ref/f)^2)` (FR-012)
- [ ] T040 [US6] Verify all User Story 6 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "SympatheticResonance*" 2>&1 | tail -10`

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T041 [US6] Verify IEEE 754 compliance for any new test code; add to `-fno-fast-math` list if needed

### 8.4 Commit (MANDATORY)

- [ ] T042 [US6] Commit completed User Story 6 work: anti-mud validation tests

**Checkpoint**: User Story 6 -- dense chord clarity -- is fully functional, tested, and committed. Anti-mud system verified to prevent low-frequency buildup.

---

## Phase 9: SIMD Optimization (Cross-Cutting DSP Performance)

**Goal**: Add the Highway SIMD kernel (`sympathetic_resonance_simd.cpp`) behind the same `SympatheticResonance` API, following the established `modal_resonator_bank_simd.cpp` pattern. The scalar implementation from Phases 3-8 becomes the correctness reference. All existing tests must pass with the SIMD path active; CPU cost should drop to ~56-80 ops/sample for 32 resonators.

**Independent Test**: The full DSP unit test suite (`dsp_tests.exe "SympatheticResonance*"`) must pass unchanged after SIMD is wired in. Additionally, run a performance benchmark comparing scalar vs SIMD paths to verify the expected ~4x throughput improvement (FR-017, SC-015).

### 9.0 SIMD Pre-Implementation Tests (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before SIMD implementation begins. Write these tests against the scalar path first; they will fail on SIMD correctness only after T045 wires in the SIMD kernel.

- [ ] T042a Write a SIMD correctness test in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` (append): drive the same resonator pool configuration through the scalar process() path and a direct call to `processSympatheticBankSIMD()` (once it exists); verify every output sample matches within `Approx().margin(1e-5f)` -- this test initially fails because the SIMD function does not exist yet, and must pass after T044-T045
- [ ] T042b Write a performance benchmark test tagged `[.perf]` in `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` (append): measure throughput (samples/second) for the scalar resonator loop vs the SIMD path for 32 active resonators at 44100 Hz; record both numbers and verify the SIMD path achieves at least 2x throughput over scalar (FR-017, SC-015); tag with `[.perf]` so it is excluded from normal CI runs but can be triggered explicitly with `dsp_tests.exe "[.perf]"`

### 9.1 SIMD Implementation

- [ ] T043 Create `dsp/include/krate/dsp/systems/sympathetic_resonance_simd.h` with the SIMD kernel public API: `void processSympatheticBankSIMD(float* HWY_RESTRICT y1s, float* HWY_RESTRICT y2s, const float* HWY_RESTRICT coeffs, const float* HWY_RESTRICT rSquareds, const float* HWY_RESTRICT gains, int count, float scaledInput, float* HWY_RESTRICT sums, float releaseCoeff, float* HWY_RESTRICT envelopes)` -- no Highway headers in this file (FR-017)
- [ ] T044 Create `dsp/include/krate/dsp/systems/sympathetic_resonance_simd.cpp` implementing the Highway SIMD kernel using the `#undef HWY_TARGET_INCLUDE` / `#include "hwy/foreach_target.h"` self-inclusion pattern, `HWY_NAMESPACE`, `HWY_EXPORT`, and `HWY_DYNAMIC_DISPATCH` -- process 4 resonators per SIMD lane (SSE/NEON) or 8 (AVX2) in parallel (FR-017)
- [ ] T045 Wire the SIMD kernel into `SympatheticResonance::process()` in `dsp/include/krate/dsp/systems/sympathetic_resonance.h` by replacing the scalar resonator loop with a call to `processSympatheticBankSIMD()` for the active resonator count; retain the scalar loop as fallback for counts < lane width (FR-017)

### 9.2 Verification

- [ ] T046 Verify all existing DSP unit tests still pass with SIMD active (including the T042a scalar-vs-SIMD correctness test and the T042b `[.perf]` benchmark): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "SympatheticResonance*" 2>&1 | tail -10` -- all non-perf tests must pass within floating-point tolerance; run `dsp_tests.exe "[.perf]"` separately and record scalar vs SIMD throughput numbers in a comment (SC-015, FR-017)
- [ ] T047 Verify the SIMD file was added to `dsp/CMakeLists.txt` (T007 from Phase 2) and the build succeeds with zero warnings

### 9.3 Commit (MANDATORY)

- [ ] T048 Commit completed SIMD optimization: SIMD kernel + wiring + verified test pass

**Checkpoint**: SIMD optimization complete. Same API, all tests pass, CPU cost reduced per FR-017 and SC-015.

---

## Phase 10: Plugin Integration

**Goal**: Wire `SympatheticResonance` into the Innexus processor: add the member, register VST3 parameters, handle parameter changes, save/load state, route MIDI noteOn/noteOff events, and insert the component into the process() signal chain post-voice-accumulation pre-master-gain. Add plugin-level integration tests to verify end-to-end correctness.

**Independent Test**: Run `innexus_tests.exe "SympatheticResonance*"` to verify: (1) parameters are registered with correct IDs and ranges; (2) Amount=0.0 produces zero sympathetic contribution to master output; (3) noteOn in MIDI handler populates resonator pool; (4) state save/load round-trips both parameters without data loss.

### 10.1 Tests for Plugin Integration (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T049 [P] Write failing integration tests in `plugins/innexus/tests/unit/processor/sympathetic_resonance_integration_test.cpp` covering:
  - Parameter registration: `kSympatheticAmountId` (860) is registered as a RangeParameter with range [0.0, 1.0], default 0.0, unit ""; `kSympatheticDecayId` (861) is registered with range [0.0, 1.0], default 0.5, displayed as "Sympathetic Decay" (FR-015)
  - Parameter dispatch to DSP: Setting `kSympatheticAmountId` via `processParameterChanges()` updates `sympatheticAmount_` atomic; the DSP component receives the updated value before the next audio block (FR-015)
  - Parameter dispatch decay: Same for `kSympatheticDecayId` / `sympatheticDecay_` atomic (FR-015)
  - Signal chain position: With Amount=0.5 and an active voice, the processor output contains sympathetic energy added post-voice-sum (verify by comparing output with Amount=0.0 vs Amount=0.5 for the same MIDI input) (FR-016)
  - Zero bypass at processor level: With Amount=0.0, the sympathetic component produces no output and does not alter processor output (FR-014, SC-009)
  - MIDI noteOn routing: A `kNoteOn` event in the processor MIDI handler calls `sympatheticResonance_.noteOn(voiceId, partials)` with the correct inharmonicity-adjusted partial frequencies computed from the voice's oscillator state (FR-020)
  - MIDI noteOff routing: A `kNoteOff` event calls `sympatheticResonance_.noteOff(voiceId)` and resonators continue to ring out (FR-009)
  - State save/load: `setState()` round-trip preserves both parameters; loading state from a stream with no sympathetic data (old preset) defaults to Amount=0.0, Decay=0.5 without crash (backward compatibility) (FR-015)
  - Per-block configuration safety: `applyParamsToEngine()` called on every process block with constant parameters does not reset resonator state (US2/US3 concern)

### 10.2 Plugin Implementation

- [ ] T050 Modify `plugins/innexus/src/processor/processor.h` to add:
  - `Krate::DSP::SympatheticResonance sympatheticResonance_` member
  - `std::atomic<float> sympatheticAmount_{0.0f}` member
  - `std::atomic<float> sympatheticDecay_{0.5f}` member
  - `#include <krate/dsp/systems/sympathetic_resonance.h>` at the top

- [ ] T051 Modify `plugins/innexus/src/processor/processor.cpp` to integrate sympathetic resonance into the process() loop:
  - In `setupProcessing()` / `setBusArrangements()`: call `sympatheticResonance_.prepare(processSetup.sampleRate)` after existing prepare calls
  - In the per-sample processing loop (after polyphony gain compensation, before master gain): compute `float monoSum = (sampleL + sampleR) * 0.5f; float sympatheticOut = sympatheticResonance_.process(monoSum); sampleL += sympatheticOut; sampleR += sympatheticOut;` (FR-016, quickstart.md signal chain diagram)
  - In `applyParamsToEngine()`: call `sympatheticResonance_.setAmount(sympatheticAmount_.load(std::memory_order_relaxed))` and `sympatheticResonance_.setDecay(sympatheticDecay_.load(std::memory_order_relaxed))` once per block

- [ ] T052 Modify `plugins/innexus/src/processor/processor_params.cpp` to handle `kSympatheticAmountId` and `kSympatheticDecayId` parameter changes:
  - In `processParameterChanges()`: add cases for both IDs using the same `std::memory_order_relaxed` atomic store pattern as existing parameters

- [ ] T053 Modify `plugins/innexus/src/processor/processor_state.cpp` to save/load sympathetic parameters:
  - In `getState()`: `streamer.writeFloat(sympatheticAmount_.load(...)); streamer.writeFloat(sympatheticDecay_.load(...))`
  - In `setState()`: read with defaults `float amount = 0.0f; streamer.readFloat(amount); float decay = 0.5f; streamer.readFloat(decay);` using the same backward-compat pattern as existing state code

- [ ] T054 Modify `plugins/innexus/src/processor/processor_midi.cpp` to route MIDI events to sympathetic resonance:
  - In `handleNoteOn(voiceId, ...)`: after assigning the voice, compute `SympatheticPartialInfo partials; for (int i = 0; i < kSympatheticPartialCount; ++i) { partials.frequencies[i] = computeInharmonicPartialFreq(voice.baseFreq, i+1, voice.inharmonicityB); }` then call `sympatheticResonance_.noteOn(voiceId, partials)`
  - **Inharmonicity formula**: `f_n = n * f0 * sqrt(1 + B * n^2)` per FR-018 and research R-007; the caller (processor_midi.cpp) computes this inline or via a voice system helper method -- it is NOT recomputed inside `SympatheticResonance` (already-adjusted frequencies are passed via `PartialInfo.frequencies`)
  - **Before implementing**: verify the actual inharmonicity field name on `InnexusVoice` in `plugins/innexus/src/processor/innexus_voice.h` (likely `inharmonicityAmount_` or `inharmonicityB_`); use the correct field name, not a placeholder
  - In `handleNoteOff(voiceId, ...)`: call `sympatheticResonance_.noteOff(voiceId)`

- [ ] T055 Modify `plugins/innexus/src/controller/controller.cpp` to register both parameters:
  - Add `addRangeParameter(kSympatheticAmountId, "Sympathetic Amount", "", 0.0, 1.0, 0.0)` in `Controller::initialize()`
  - Add `addRangeParameter(kSympatheticDecayId, "Sympathetic Decay", "", 0.0, 1.0, 0.5)` in `Controller::initialize()`
  - Follow the exact same RangeParameter registration pattern as adjacent parameters in controller.cpp

- [ ] T056 Verify all plugin integration tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe "SympatheticResonance*" 2>&1 | tail -10`

### 10.3 Cross-Platform Verification (MANDATORY)

- [ ] T057 Verify IEEE 754 compliance for `sympathetic_resonance_integration_test.cpp`; add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed

### 10.4 Commit (MANDATORY)

- [ ] T058 Commit completed plugin integration: processor member + signal chain + parameter dispatch + MIDI routing + state + controller registration + integration tests

**Checkpoint**: Plugin integration complete. SympatheticResonance is live in the Innexus audio chain. Both VST3 parameters are registered. MIDI routing is active.

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Run pluginval, verify the complete build is warning-free, confirm all tests pass across both test targets, and address any edge cases identified during implementation.

- [ ] T059 Build the full Innexus plugin (not just tests): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release` and verify zero compilation warnings in all new files (`sympathetic_resonance.h`, `sympathetic_resonance_simd.h`, `sympathetic_resonance_simd.cpp`, and all modified plugin files)
- [ ] T060 Run the full `dsp_tests` suite (all tests, not just new ones) and verify zero regressions: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [ ] T061 Run the full `innexus_tests` suite and verify zero regressions: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`
- [ ] T062 Run pluginval on the Innexus plugin: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` -- verify PASS with no errors
- [ ] T063 [P] Verify edge case: rapid noteOn/noteOff alternation (tremolo at 16th notes, 120 BPM) does not cause assertion failures or pool overflows; add a stress test to `sympathetic_resonance_test.cpp` if not already covered
- [ ] T064 [P] Verify edge case: all 8 voices playing the same unison note (same voiceId re-trigger handled as duplicate merge per FR-011); verify `getActiveResonatorCount() == kSympatheticPartialCount` not 8x that

---

## Phase 12: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task.

### 12.1 Architecture Documentation Update

- [ ] T065 Update `specs/_architecture_/layer-3-systems.md` with the new `SympatheticResonance` component entry:
  - Component name, purpose, public API summary (`prepare`, `setAmount`, `setDecay`, `noteOn`, `noteOff`, `process`)
  - File location: `dsp/include/krate/dsp/systems/sympathetic_resonance.h`
  - SIMD kernel location: `dsp/include/krate/dsp/systems/sympathetic_resonance_simd.cpp`
  - "When to use this": global post-voice-accumulation sympathetic string resonance effect
  - Key constants: `kSympatheticPartialCount`, `kMaxSympatheticResonators`, `kMergeThresholdHz`
  - Dependencies: Layer 1 `Biquad`, Layer 1 `OnePoleSmoother`, Google Highway (PRIVATE)

### 12.2 Final Commit

- [ ] T066 Commit architecture documentation updates
- [ ] T067 Verify all spec work is committed to the `132-sympathetic-resonance` feature branch (`git status` shows clean working tree)

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase 13: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 13.1 Run Clang-Tidy Analysis

- [ ] T068 Run clang-tidy on all modified/new source files:
  ```powershell
  # Windows (PowerShell) - requires Ninja build
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja
  ```

### 13.2 Address Findings

- [ ] T069 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T070 Review warnings and fix where appropriate; document suppressions with `// NOLINT(reason)` if any warnings are intentionally ignored (DSP-specific patterns)
- [ ] T071 Commit any clang-tidy fixes

**Checkpoint**: Static analysis clean -- ready for completion verification.

---

## Phase 14: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 14.1 Requirements Verification

- [ ] T072 Review ALL FR-001 through FR-023 requirements from `specs/132-sympathetic-resonance/spec.md` against the implementation -- for each FR, open the implementation file and record the file path and line number that satisfies it
- [ ] T073 Review ALL SC-001 through SC-015 success criteria -- for each SC, run the specific test or measurement and record the actual output vs spec target (no paraphrasing -- use real numbers)
- [ ] T074 Search for cheating patterns in all new code:
  - No `// placeholder` or `// TODO` comments in `sympathetic_resonance.h`, `sympathetic_resonance_simd.cpp`, or integration test file
  - No test thresholds relaxed from spec requirements (e.g., using -80 dB threshold when spec says -96 dB)
  - No features quietly removed from scope (all 23 FRs and 15 SCs addressed)

### 14.2 Fill Compliance Table in spec.md

- [ ] T075 Update `specs/132-sympathetic-resonance/spec.md` "Implementation Verification" section with compliance status and evidence for all 38 requirements (23 FR + 15 SC)
- [ ] T076 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 14.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T077 All self-check questions answered "no" (or gaps documented honestly in spec.md)

---

## Phase 15: Final Completion

### 15.1 Final Commit

- [ ] T078 Commit all remaining spec work (compliance table updates) to `132-sympathetic-resonance` feature branch
- [ ] T079 Run full test suite one final time to confirm clean state: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5` and `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 15.2 Completion Claim

- [ ] T080 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    -> Phase 2 (Foundational: IDs + build registration) -- BLOCKS all phases below
        -> Phase 3 (US1: Core resonator pool + scalar DSP class) -- BLOCKS US2/US3/US4/US5/US6
            -> Phase 4 (US2: Amount control) -- can parallelize with US3 after US1
            -> Phase 5 (US3: Decay control) -- can parallelize with US2 after US1
            -> Phase 6 (US4: Ring-out + pool management) -- depends on US1
            -> Phase 7 (US5: Near-unison beating) -- depends on US1
            -> Phase 8 (US6: Dense chord clarity) -- depends on US1
        -> Phase 9 (SIMD optimization) -- depends on US1 (full scalar class must exist)
        -> Phase 10 (Plugin integration) -- depends on US1 + US2 + US3 (needs full DSP class with parameter setters)
    -> Phase 11 (Polish) -- depends on Phase 9 + Phase 10
    -> Phase 12 (Docs) -- depends on Phase 11
    -> Phase 13 (Clang-tidy) -- depends on Phase 11
    -> Phase 14 (Compliance verification) -- depends on Phase 12 + Phase 13
    -> Phase 15 (Final completion) -- depends on Phase 14
```

### User Story Dependencies

- **US1 (P1)**: Foundational only -- no other story dependencies. Standalone DSP class test.
- **US2 (P1)**: Depends on US1 (amount parameter maps into the smoother introduced in US1). Can parallelize with US3.
- **US3 (P1)**: Depends on US1 (Q parameter maps into resonator coefficients introduced in US1). Can parallelize with US2.
- **US4 (P2)**: Depends on US1 (ring-out is already the core noteOff behavior; this phase hardens and tests it). After US1.
- **US5 (P2)**: Depends on US1 (merge threshold is part of noteOn introduced in US1). After US1.
- **US6 (P2)**: Depends on US1 (anti-mud filter is in the resonator pool; frequency-dependent Q is in coefficient init). After US1.
- **Plugin Integration (Phase 10)**: Depends on US1 + US2 + US3 -- needs `setAmount()`, `setDecay()`, `noteOn()`, `noteOff()`, and `process()` all implemented.

### Parallel Opportunities

Within Phase 3 (US1), after the header is created (T009):
- T010 (prepare), T011 (noteOn), T012 (noteOff), T013 (process) are sequential (each depends on the prior)

After Phase 3 (US1) completes:
- Phase 4 (US2) and Phase 5 (US3) can run in parallel (different parameter setters, no code conflict)
- Phase 6 (US4), Phase 7 (US5), Phase 8 (US6) can run in parallel (test-only verification phases)
- Phase 9 (SIMD) can run in parallel with Phase 10 (plugin integration) since they touch different files

Within Phase 10 (Plugin Integration), after T049 (tests written):
- T050 (`processor.h`) + T055 (`controller.cpp`) are parallelizable [P]
- T051 (`processor.cpp`) depends on T050
- T052 (`processor_params.cpp`) depends on T050
- T053 (`processor_state.cpp`) depends on T050
- T054 (`processor_midi.cpp`) depends on T050

---

## Parallel Execution Examples

### After Phase 3 (US1 Complete) -- Start US2 + US3 Together

```
Task A: Phase 4 (US2 Amount Control)
  T017 Write amount tests -> T018 Implement setAmount() -> T019 Verify -> T020 Cross-platform -> T021 Commit

Task B: Phase 5 (US3 Decay Control)
  T022 Write decay tests -> T023 Implement setDecay() -> T024 Verify -> T025 Cross-platform -> T026 Commit
```

### After Phase 3 (US1 Complete) -- P2 Stories in Parallel

```
Task A: Phase 6 (US4 Ring-Out)
Task B: Phase 7 (US5 Near-Unison)
Task C: Phase 8 (US6 Dense Chord)
```

### After US1+US2+US3 Complete -- SIMD + Plugin Integration in Parallel

```
Task A: Phase 9 (SIMD Kernel)
  T043 SIMD header -> T044 SIMD .cpp -> T045 Wire into process() -> T046 Verify -> T047 Build check -> T048 Commit

Task B: Phase 10 (Plugin Integration)
  T049 Integration tests -> T050+T055 [P] Header + Controller -> T051-T054 Plugin files -> T056 Verify -> T058 Commit
```

---

## Implementation Strategy

### MVP First (P1 User Stories Only -- Phases 1-5)

1. Complete Phase 1: Setup (branch + baseline)
2. Complete Phase 2: Foundational (IDs + build registration)
3. Complete Phase 3: US1 -- core resonator pool and harmonic reinforcement
4. **STOP and VALIDATE**: Run `dsp_tests.exe "SympatheticResonance*"` -- should see interval-dependent reinforcement
5. Complete Phase 4: US2 -- amount control + zero bypass
6. Complete Phase 5: US3 -- decay control + Q range
7. **STOP and VALIDATE**: All three P1 stories working. DSP class is complete and tested.

### Incremental Delivery

1. Phases 1-5 (P1 stories) -> Complete scalar DSP class with full parameter control
2. Phase 6 (US4) -> Ring-out hardening
3. Phase 7 (US5) -> Near-unison beating validated
4. Phase 8 (US6) -> Dense chord clarity validated
5. Phase 9 (SIMD) -> Performance optimization
6. Phase 10 (Plugin integration) -> Feature live in Innexus plugin
7. Phases 11-15 (Polish, docs, verification) -> Spec complete

### Suggested MVP Scope

Phase 3 (US1) alone is independently testable and delivers the core value (cross-voice harmonic reinforcement). Phases 3-5 together (all three P1 stories) constitute the full MVP: a working SympatheticResonance DSP class with Amount and Decay parameters, ready for plugin integration.

---

## Notes

- [P] tasks = different files, no dependencies on each other
- [US1]-[US6] labels map tasks to specific user stories for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- **MANDATORY**: Run pluginval after plugin integration (Phase 10)
- SoA layout for the resonator pool (introduced in Phase 3) is intentional -- it prepares for SIMD in Phase 9 without requiring API changes
- The scalar loop in `process()` and the SIMD kernel process the same SoA arrays -- Phase 9 is a drop-in replacement, not a refactor
- Highway SIMD kernel follows the exact same pattern as `modal_resonator_bank_simd.cpp` -- read that file before starting Phase 9
- The `amountSmoother_` uses `OnePoleSmoother::process()` (returns value) not `setTarget()` alone -- call `process()` once per sample inside the processing loop
- State save/load order in `processor_state.cpp` MUST match exactly between write and read to avoid data corruption on preset load
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
