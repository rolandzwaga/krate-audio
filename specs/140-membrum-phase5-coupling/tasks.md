---
description: "Task list for Membrum Phase 5 -- Cross-Pad Coupling (Sympathetic Resonance)"
---

# Tasks: Membrum Phase 5 -- Cross-Pad Coupling

**Input**: Design documents from `/specs/140-membrum-phase5-coupling/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md, contracts/
**Branch**: `140-membrum-phase5-coupling`

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Build**: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests`
4. **Verify**: Run `build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling]"` and confirm pass
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required.

**Tool Discipline**: Use Write/Edit tools for all file creation and modification. Never use bash heredocs, `echo >`, or shell redirection to write source files.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Branch setup, CMakeLists.txt registration for new source files, and build verification.

- [X] T001 Verify feature branch `140-membrum-phase5-coupling` is checked out (STOP if on main -- create/checkout branch first)
- [X] T002 [P] Add new source files to `plugins/membrum/CMakeLists.txt`: `src/dsp/pad_category.h`, `src/dsp/coupling_matrix.h` (header-only -- verify they are included in the build target)
- [X] T003 [P] Add new test files to `plugins/membrum/tests/CMakeLists.txt`: `test_pad_category.cpp`, `test_coupling_matrix.cpp`, `test_coupling_integration.cpp`, `test_coupling_state.cpp`, `test_coupling_energy.cpp` and `dsp/tests/unit/processors/test_modal_bank_frequency.cpp` to the dsp_tests target
- [X] T004 Verify clean build succeeds: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests` (fix any pre-existing errors before proceeding)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core DSP extension and data structures that ALL user stories depend on. Must be complete before any user story work begins.

**CRITICAL**: No user story work can begin until this phase is complete.

### 2.1 ModalResonatorBank Frequency Accessor (DSP Layer)

- [X] T005 Write failing tests for `getModeFrequency()` and `getNumModes()` in `dsp/tests/unit/processors/test_modal_bank_frequency.cpp` -- tests must FAIL before proceeding (verify bank stores epsilon, formula: `f = asin(eps * 0.5f) * sr / pi`, returns 0 for out-of-range k, getNumModes returns configured count)
- [X] T006 Add `getModeFrequency(int k) const noexcept` and `getNumModes() const noexcept` to `dsp/include/krate/dsp/processors/modal_resonator_bank.h` per data-model.md section 7 (recover Hz from `epsilonTarget_[k]` using `std::asin`)
- [X] T007 Build and verify dsp_tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "*modal*bank*freq*" 2>&1 | tail -5`

### 2.2 PadConfig Extension

- [X] T008 Write failing test for `kPadCouplingAmount = 36` offset access in `plugins/membrum/tests/unit/dsp/test_pad_category.cpp` (include: field exists at correct offset, padOffsetFromParamId accepts 36, padParamId(n, 36) computes correctly, default value is 0.5f)
- [X] T009 Add `kPadCouplingAmount = 36` to `PadParamOffset` enum and `kPadActiveParamCountV5 = 37` in `plugins/membrum/src/dsp/pad_config.h`; add `float couplingAmount = 0.5f` field to `PadConfig` struct after `frictionPressure`; update `padOffsetFromParamId` to accept offset 36 (per research R10)
- [X] T010 Build and verify padConfig tests pass

### 2.3 PadCategory Classification

- [X] T011 Write failing tests for `classifyPad()` in `plugins/membrum/tests/unit/dsp/test_pad_category.cpp` (cover all 5 rules in priority order: Membrane+pitchEnv=Kick, Membrane+NoiseBurst=Snare, Membrane=Tom, NoiseBody=HatCymbal, else=Perc; test priority ordering: Kick rule fires before Snare rule when both conditions met)
- [X] T012 Create `plugins/membrum/src/dsp/pad_category.h` matching the contract at `specs/140-membrum-phase5-coupling/contracts/pad_category.h` (PadCategory enum with Kick/Snare/Tom/HatCymbal/Perc/kCount; inline `classifyPad()` free function)
- [X] T013 Build and verify pad_category tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[pad_category]" 2>&1 | tail -5`

### 2.4 CouplingMatrix Data Structure

- [X] T014 Write failing tests for `CouplingMatrix` in `plugins/membrum/tests/unit/dsp/test_coupling_matrix.cpp` (cover: Tier 1 recompute sets Kick->Snare gain = snareBuzz * 0.05f, Tom->Tom gain = tomResonance * 0.05f, all other pairs = 0.0f, self-pairs always = 0.0f; Tier 2 override replaces computed gain for that pair only; clearOverride reverts to computed; kMaxCoefficient clamp on setOverride; forEachOverride iterates correctly; clearAll zeros everything; getOverrideCount correct)
- [X] T015 Create `plugins/membrum/src/dsp/coupling_matrix.h` matching the contract at `specs/140-membrum-phase5-coupling/contracts/coupling_matrix.h` (CouplingMatrix class: recomputeFromTier1, setOverride/clearOverride/hasOverrideAt/getOverrideGain, getEffectiveGain/effectiveGainArray, getOverrideCount/forEachOverride/clearAll, private resolve/resolveAll)
- [X] T016 Build and verify coupling_matrix tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling_matrix]" 2>&1 | tail -5`

### 2.5 Foundational Commit

- [X] T017 Commit all foundational work: ModalResonatorBank extension, PadConfig extension, PadCategory, CouplingMatrix

**Checkpoint**: Foundation ready -- all data structures and DSP extension verified. User story implementation can now begin.

---

## Phase 3: User Story 1 -- Snare Buzz from Kick (Priority: P1)

**Goal**: When the kick is struck with Global Coupling > 0 and Snare Buzz > 0, the output contains audible sympathetic buzz at the snare's modal frequencies. Setting Snare Buzz to 0 removes the effect entirely.

**Independent Test**: Trigger MIDI 36 (kick) with Snare Buzz at 50% and Global Coupling at 100%. Measure energy in the 1-8 kHz band -- must be above the noise floor. Set Snare Buzz to 0 -- output must be identical to Phase 4 (no coupling contribution). Verify bypass SC-001 when Global Coupling = 0.

### 3.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T018 [P] [US1] Write failing integration test in `plugins/membrum/tests/unit/processor/test_coupling_integration.cpp`: signal chain wiring -- noteOn with couplingEngine_ pointer set causes noteOn on SympatheticResonance; process() with global coupling > 0 adds non-zero energy to output (verify at sample level); process() with global coupling = 0 adds zero energy (SC-001: identical to Phase 4 output within -120 dBFS); mono sum (L+R)/2 feeds delay-read then delay-write then engine (not raw L or R); SC-002 assertion: kick triggered with Snare Buzz at 50% and Global Coupling at 100% produces coupling contribution that is measurably above noise floor AND at least -40 dBFS below the kick's peak level (audible but not dominant)
- [X] T019 [P] [US1] Write failing integration test in `plugins/membrum/tests/unit/processor/test_coupling_energy.cpp`: energy limiter caps output below -20 dBFS (SC-007: trigger all 32 pads simultaneously at max velocity + max coupling); bypass early-out when globalCoupling_ == 0.0f adds < 0.01% measurable overhead (verify isBypassed path); velocity scaling: lower velocity produces proportionally less coupling excitation

### 3.2 Implementation for User Story 1

- [X] T020 [US1] Add Phase 5 parameter IDs to `plugins/membrum/src/plugin_ids.h`: `kGlobalCouplingId = 270`, `kSnareBuzzId = 271`, `kTomResonanceId = 272`, `kCouplingDelayId = 273`; add `static_assert(kSelectedPadId < kGlobalCouplingId)` (FR-062); update `kCurrentStateVersion = 5`
- [X] T021 [US1] Extend `plugins/membrum/src/processor/processor.h` with coupling members per data-model.md section 6: `couplingEngine_` (SympatheticResonance), `couplingDelay_` (DelayLine), `couplingMatrix_` (CouplingMatrix), `padCategories_` (std::array<PadCategory, kNumPads>), atomics `globalCoupling_`, `snareBuzz_`, `tomResonance_`, `couplingDelayMs_`, and `energyEnvelope_`
- [X] T022 [US1] Add `getPartialInfo() const noexcept -> Krate::DSP::SympatheticPartialInfo` to `plugins/membrum/src/dsp/drum_voice.h` (extract first 4 partial frequencies from bodyBank_.getSharedBank() using getModeFrequency(0..3) per coupling_integration.h contract)
- [X] T023 [US1] Extend `plugins/membrum/src/voice_pool/voice_pool.h` and `voice_pool.cpp` with coupling engine pointer and hooks per coupling_integration.h contract: `setCouplingEngine(SympatheticResonance*)` method; modified noteOn() calls `couplingEngine_->noteOn(voiceId, partials, velocity)` after applyPadConfigToSlot() -- pass the MIDI velocity (0-127 normalized to 0.0-1.0) so the engine scales coupling excitation proportionally (FR-041); modified noteOff() calls `couplingEngine_->noteOff(voiceId)` before releasing
- [X] T024 [US1] Implement coupling signal chain in `plugins/membrum/src/processor/processor.cpp`: in `setupProcessing()` call `couplingEngine_.prepare(sampleRate)`, `couplingDelay_.prepare(sampleRate, 0.002f)`, `voicePool_.setCouplingEngine(&couplingEngine_)`; in `process()` after voicePool_.processBlock() add the per-sample loop with the correct read-before-write order: compute `delaySamples = couplingDelayMs_ * sampleRate / 1000.0f` once before the loop, then per sample: mono sum (L+R)/2 -> `delayed = couplingDelay_.readLinear(delaySamples)` -> `couplingDelay_.write(mono)` -> `couplingEngine_.process(delayed)` -> `applyEnergyLimiter()` -> outL[s] += coupling, outR[s] += coupling (skip entire loop when `couplingEngine_.isBypassed()` per FR-072); implement `applyEnergyLimiter()` per coupling_integration.h contract (kThreshold = 0.1f, kAttackCoeff = 0.001f, kReleaseCoeff = 0.9999f)
- [X] T025 [US1] Add parameter handling in `processParameterChanges()` in `plugins/membrum/src/processor/processor.cpp`: cases for kGlobalCouplingId (store + call setAmount on engine), kSnareBuzzId (store + recompute matrix), kTomResonanceId (store + recompute matrix), kCouplingDelayId (denormalize 0.5-2.0ms range + store)
- [X] T026 [US1] Register Phase 5 global parameters in `plugins/membrum/src/controller/controller.cpp`: RangeParameter for kGlobalCouplingId (0.0-1.0, default 0.0), kSnareBuzzId (0.0-1.0, default 0.0), kTomResonanceId (0.0-1.0, default 0.0), kCouplingDelayId (0.5-2.0 ms, default 1.0)
- [X] T027 [US1] Build and verify all coupling tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling]" 2>&1 | tail -5`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T028 [US1] Check if `test_coupling_integration.cpp` or `test_coupling_energy.cpp` use `std::isnan`/`std::isfinite`/`std::isinf` -- if yes, add those files to the `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [X] T029 [US1] Commit completed User Story 1 work (signal chain, energy limiter, global coupling parameter handling, parameter registration)

**Checkpoint**: User Story 1 fully functional -- kick triggers audible snare buzz. Build clean, tests pass, committed.

---

## Phase 4: User Story 2 -- Tom Sympathetic Resonance (Priority: P1)

**Goal**: Striking a tom causes other toms to resonate sympathetically. The effect is frequency-selective via modal coincidence: a receiver tom whose fundamental lands on one of the driver's first 4 modal partials (mode-coincident) couples at least 12 dB more strongly than a receiver whose fundamental falls in a gap between the driver's modes (mode-gap) per SC-008. (Membrane modes are inharmonic -- Bessel ratios 1.0, 1.594, 2.136, 2.296 -- so musical intervals like octave/tritone do not predict coupling strength; modal coincidence does.)

**Independent Test**: Configure two toms, with the receiver's fundamental tuned onto one of the driver's first 4 modal partials (mode-coincident). Strike the driver. Measure spectral energy at the receiver's fundamental -- must be above noise floor. Repeat with the receiver's fundamental placed in a gap between the driver's modal partials (mode-gap) -- coupling must be at least 12 dB weaker (SC-008). Set Tom Resonance to 0 -- no coupling occurs.

### 4.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T030 [P] [US2] Write failing tests in `plugins/membrum/tests/unit/processor/test_coupling_integration.cpp` (extend existing file or add SECTION): Tom Resonance knob at 50% causes Tom->Tom pairs to have computedGain = tomResonance * 0.05f in the matrix; classifyPad correctly identifies Tom-category pads; frequency-selective coupling via modal coincidence: mode-coincident toms (receiver f0 lands on one of driver's first 4 modal partials) produce at least 12 dB more coupling energy than mode-gap toms (receiver f0 in a gap between driver's modes) -- verify SC-008 via spectral measurement; Tom Resonance at 0 produces zero Tom->Tom gain in matrix

### 4.2 Implementation for User Story 2

- [X] T031 [US2] Verify `recomputeFromTier1()` correctly handles Tom->Tom pairs (covered by CouplingMatrix implementation in T015 -- validate against Tom-specific PadConfig instances with bodyModel=Membrane, no pitch env, no noise exciter)
- [X] T032 [US2] Verify pad category derivation for Tom category works with actual PadConfig fixtures from default kit (membrane body, no pitch envelope, no noise burst exciter); ensure `padCategories_` array is updated when pad configuration changes (body model, exciter type, or pitch envelope settings change -- i.e., on per-pad config updates, NOT on kSnareBuzzId or kTomResonanceId knob changes, which only trigger recomputeFromTier1() and already read the cached padCategories_)
- [X] T033 [US2] Build and verify Tom-specific tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling]" 2>&1 | tail -5`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T034 [US2] Verify no new IEEE 754 function usage was introduced -- check spectral measurement helpers if used in Tom tests

### 4.4 Commit (MANDATORY)

- [X] T035 [US2] Commit User Story 2 work (Tom resonance verification tests + any fixes to pad category recomputation path)

**Checkpoint**: User Story 2 functional -- toms resonate sympathetically with frequency selectivity verified.

---

## Phase 5: User Story 3 -- Global Coupling Control (Priority: P1)

**Goal**: Global Coupling knob acts as master scaling factor for all coupling paths. At 0.0, output is identical to Phase 4 (no coupling, < 0.01% CPU overhead per SC-004). At 100%, maximum sympathetic effects.

**Independent Test**: Set Global Coupling to 0. Verify output matches Phase 4 exactly (SC-001: < -120 dBFS difference). Set to 50% -- coupling at half intensity. Measure CPU with coupling disabled vs enabled.

### 5.1 Tests for User Story 3 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T036 [P] [US3] Write failing tests in `plugins/membrum/tests/unit/processor/test_coupling_energy.cpp` (extend existing file): Global Coupling = 0 produces output identical to no-coupling baseline within floating-point tolerance (SC-001); isBypassed() returns true when globalCoupling_ = 0; verify early-out path skips the per-sample coupling loop entirely when isBypassed(); Global Coupling at 50% scales all coupling paths by 0.5x vs 100% (verify proportionally in matrix gain formula); setAmount() on couplingEngine_ is called with globalCoupling value when parameter changes

### 5.2 Implementation for User Story 3

- [ ] T037 [US3] Verify the bypass path in `processor.cpp` uses `couplingEngine_.isBypassed()` (which checks both smoother current value AND couplingGain_) before entering the per-sample coupling loop -- confirm no signal is added when Global Coupling is 0 (SC-001 compliance)
- [ ] T038 [US3] Verify global coupling parameter change triggers `couplingEngine_.setAmount(globalCoupling)` (SympatheticResonance::setAmount maps [0,1] to dB range internally -- pass the raw normalized value directly per plan.md gotchas table)
- [ ] T039 [US3] Build and verify all coupling tests still pass after global coupling verification changes

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T040 [US3] Verify IEEE 754 compliance for all coupling test files registered in tests/CMakeLists.txt

### 5.4 Commit (MANDATORY)

- [ ] T041 [US3] Commit User Story 3 work (global coupling bypass verification + any fixes)

**Checkpoint**: User Story 3 functional -- global coupling bypass verified, SC-001 and SC-004 met.

---

## Phase 6: User Story 4 -- Per-Pad Coupling Amount (Priority: P2)

**Goal**: Each pad has a per-pad Coupling Amount parameter (offset 36 in PadConfig) controlling how strongly it participates in coupling as both source and receiver. A pad with coupling amount = 0 is completely excluded from all coupling computation.

**Independent Test**: Set pad 1 (kick) coupling amount to 0. Trigger kick -- no coupling energy generated (US4 scenario 1). Set pad 3 (snare) coupling amount to 0. Trigger kick -- no snare buzz (US4 scenario 2). Save and reload state -- coupling amounts round-trip exactly (US4 scenario 4).

### 6.1 Tests for User Story 4 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T042 [P] [US4] Write failing tests in `plugins/membrum/tests/unit/processor/test_coupling_integration.cpp` (extend): pad with couplingAmount = 0.0f excluded from noteOn registration (no resonators added); pad with couplingAmount = 0.0f excluded as receiver (zero sympathetic energy at that pad's frequencies); per-pad coupling amount param ID formula: `kPadBaseId + N * kPadParamStride + 36` generates correct ID for each pad N; setPadConfigField with offset 36 updates couplingAmount in PadConfig; recompute matrix is triggered when per-pad coupling amount changes (FR-034)

### 6.2 Implementation for User Story 4

- [ ] T043 [US4] Add per-pad coupling amount parameter handling in `processParameterChanges()` in `plugins/membrum/src/processor/processor.cpp`: detect offset 36 via padOffsetFromParamId(), call setPadConfigField for couplingAmount, then trigger couplingMatrix_.recomputeFromTier1() to incorporate updated per-pad amounts into effectiveGain formula (per data-model.md section 8)
- [ ] T044 [US4] Register per-pad coupling amount parameters in `plugins/membrum/src/controller/controller.cpp`: for each pad N, register RangeParameter at `padParamId(N, kPadCouplingAmount)` (0.0-1.0, default 0.5) -- 32 parameters total
- [ ] T044b [US4] Verify per-pad preset serialization excludes `couplingAmount` (FR-022): open `plugins/membrum/src/preset/` per-pad preset save/load code and confirm `couplingAmount` (offset 36) is NOT written to or read from per-pad sound presets; write a test in `test_coupling_state.cpp` that saves a per-pad preset, reloads it, and asserts that `couplingAmount` is NOT restored (value should remain at the pad's pre-reload state, not overwritten by preset data)
- [ ] T045 [US4] Verify noteOn exclusion: in VoicePool noteOn hook, check `padConfig(padIndex).couplingAmount == 0.0f` before calling `couplingEngine_->noteOn()` -- if zero, skip registration entirely (FR-023 CPU optimization)
- [ ] T046 [US4] Build and verify all coupling tests pass including per-pad amount tests: `build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling]" 2>&1 | tail -5`

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T047 [US4] Check for new IEEE 754 function usage in per-pad coupling tests

### 6.4 Commit (MANDATORY)

- [ ] T048 [US4] Commit User Story 4 work (per-pad coupling amount parameters, controller registration, exclusion logic)

**Checkpoint**: User Story 4 functional -- per-pad coupling control verified, 32 parameters registered.

---

## Phase 7: User Story 5 -- State Version 5 and Migration (Priority: P2)

**Goal**: State version 5 saves and loads all Phase 5 coupling parameters. Loading a v4 state blob produces Phase 4 behavior (all coupling defaults = disabled). Migration chain v1->v2->v3->v4->v5 works.

**Independent Test**: Save state with non-default coupling parameters. Reload -- all values round-trip exactly (SC-005). Load a v4 state blob -- all coupling params at defaults, coupling disabled (SC-006, FR-051). Load v1/v2/v3 state blobs -- migration chain succeeds.

### 7.1 Tests for User Story 5 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T049 [P] [US5] Write failing tests in `plugins/membrum/tests/unit/processor/test_coupling_state.cpp`: state v5 round-trip -- save with globalCoupling=0.7, snareBuzz=0.4, tomResonance=0.3, couplingDelay=1.5ms, per-pad amounts all distinct values, 3 override pairs; reload and verify all values identical (SC-005); v4 migration -- load v4 blob, verify all Phase 5 params at defaults (globalCoupling=0.0, snareBuzz=0.0, tomResonance=0.0, couplingDelay=1.0ms, perPad=0.5, overrideCount=0) (SC-006, FR-051); kCurrentStateVersion == 5 (FR-050); override serialization format: uint16 count + (uint8 src, uint8 dst, float32 coeff) per entry (FR-053); per-pair coefficients clamped to [0.0, 0.05] on load (FR-031); state version mismatch on older blobs triggers migration without crash (FR-052)

### 7.2 Implementation for User Story 5

- [ ] T050 [US5] Implement state version 5 serialization in `plugins/membrum/src/processor/processor.cpp` `getState()`: after all v4 fields (including selectedPadIndex), append Phase 5 data per data-model.md section 5 binary layout: 4 x float64 (globalCoupling, snareBuzz, tomResonance, couplingDelay), 32 x float64 (perPadCouplingAmounts), uint16 overrideCount, then N x (uint8 src, uint8 dst, float32 coeff) override entries; write `kCurrentStateVersion = 5`
- [ ] T051 [US5] Implement state version 5 deserialization in `setState()`: read version field; if version == 4, read v4 data then set all Phase 5 params to defaults (globalCoupling=0.0, snareBuzz=0.0, tomResonance=0.0, couplingDelayMs=1.0f, all perPad=0.5f, overrideCount=0); if version == 5, read all fields including Phase 5 appended data; pass through existing v1->v2->v3->v4 migration chain before Phase 5 handling
- [ ] T052 [US5] After loading state, apply all Phase 5 params to engine: call `couplingEngine_.setAmount()`, `couplingDelay_.setDelayMs()` or equivalent, update padConfig couplingAmount fields, call `couplingMatrix_.recomputeFromTier1()` and load overrides via `couplingMatrix_.setOverride()` for each serialized pair
- [ ] T053 [US5] Build and verify state tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling_state]" 2>&1 | tail -5`

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T054 [US5] Verify state test files use `Approx().margin()` not exact equality for float64 round-trip comparisons (MSVC/Clang differ at 7th-8th decimal)

### 7.4 Commit (MANDATORY)

- [ ] T055 [US5] Commit User Story 5 work (state v5 serialization, v4 migration, override round-trip)

**Checkpoint**: User Story 5 functional -- state round-trip verified, v4 migration tested and passing.

---

## Phase 8: User Story 5 -- Coupling Matrix Data Model (Priority: P3)

> **Note**: User Story 5 spans both Phase 7 (state serialization of matrix overrides) and Phase 8 (matrix data model correctness). The [US5] tag is used throughout both phases.

**Goal**: The two-layer coupling matrix data model is fully operational: Tier 1 knobs compute gain for Kick->Snare and Tom->Tom pairs; Tier 2 per-pair overrides replace computed values for specific pairs; the resolved effectiveGain array is used at audio time.

**Independent Test**: Programmatically set a (kick, snare) pair override to 0.03. Trigger kick. Verify coupling energy in output scaled by 0.03. Set all matrix coefficients to 0.0 -- no coupling occurs regardless of Tier 1 knobs. Save/reload state -- all per-pair coefficients round-trip.

### 8.1 Tests (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T056 [P] [US5] Write failing tests in `plugins/membrum/tests/unit/dsp/test_coupling_matrix.cpp` (extend existing): Tier 2 override at (kick, snare) = 0.03 takes priority over Tier 1 computedGain; all-zeros matrix produces zero output regardless of Tier 1 knobs; per-pair override persists after recomputeFromTier1() call (override not wiped by Tier 1 recompute); clearOverride() reverts to computedGain at that pair; getOverrideCount() counts only pairs with active overrides; effectiveGainArray() returns pointer to flat array usable in batch iteration

### 8.2 Implementation

- [ ] T057 [US5] Verify CouplingMatrix from Phase 2 (T015) fully covers all matrix data model requirements (most requirements are already covered by the contract-based implementation -- this task is a gap analysis and any residual fixes)
- [ ] T058 [US5] Build and verify all coupling matrix tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling_matrix]" 2>&1 | tail -5`

### 8.3 Commit (MANDATORY)

- [ ] T059 [US5] Commit any residual coupling matrix fixes

**Checkpoint**: All user stories implemented and committed.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Pluginval verification, edge case hardening, and routing verification.

- [ ] T059b Write zero-allocation fuzz test for SC-009: in `plugins/membrum/tests/unit/processor/test_coupling_energy.cpp` (or a new `test_coupling_fuzz.cpp`), use `AllocationDetector` from the shared test helpers to monitor audio-thread allocations; send random MIDI noteOn/noteOff events across all 32 pads for a 10-second equivalent (sampleRate * 10 samples processed in blocks) with coupling enabled; assert zero allocations occur on the audio thread during the run (SC-009)
- [ ] T060 [P] Verify coupling output routes to main bus only (FR-073): check that auxiliary output buses in `processor.cpp` do NOT receive coupling signal; coupling is added only to the stereo main output pair
- [ ] T061 [P] Verify sample rate change handling (FR-006/edge case): in `setupProcessing()`, both `couplingEngine_.prepare()` and `couplingDelay_.prepare()` are called on every sample rate change, not only on first call; verify resonator coefficients recalculate correctly
- [ ] T062 [P] Verify choke group edge case (edge case from spec): when a voice is choked (fast-release), `noteOff()` is called on the coupling engine allowing resonators to ring out naturally -- verify noteOff is called in the voice steal path as well as the normal note-off path
- [ ] T063 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` and fix any errors (SC-010)
- [ ] T064 Run full membrum_tests suite and verify all tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5`

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Clang-tidy quality gate before final verification.

- [ ] T065 Generate compile_commands.json if not current: ensure `build/windows-ninja` is up to date with `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja`
- [ ] T066 Run clang-tidy on all modified and new files: `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja` -- redirect output to log file for inspection
- [ ] T067 Fix all clang-tidy errors (blocking issues) and review warnings; add `// NOLINT(...)` with reason for any intentionally suppressed warnings in DSP-critical inner loops
- [ ] T068 Commit clang-tidy fixes

---

## Phase 11: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation per Constitution Principle XIII.

- [ ] T069 Update `specs/_architecture_/` -- add Phase 5 components to the appropriate layer files: `PadCategory` + `classifyPad()` (plugin-local DSP); `CouplingMatrix` (plugin-local DSP); `ModalResonatorBank` accessor additions (Layer 2); note the coupling signal chain in the Membrum processor section
- [ ] T070 Commit architecture documentation updates

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honest verification of all requirements before claiming completion.

### 12.1 Requirements Verification

- [ ] T071 Open each FR-xxx implementation file and verify with line-level evidence (no "implemented" without file + line citation):
  - FR-001: SympatheticResonance integrated in processor.h/processor.cpp
  - FR-002: Mono sum (L+R)/2 feeds delay then engine; output additive to main out
  - FR-003: First 4 partials extracted from ModalResonatorBank per voice
  - FR-004: Only active voices participate (noteOn/noteOff lifecycle correct)
  - FR-005: Frequency selectivity inherent to resonator design (no code needed -- verify via SC-008 test result)
  - FR-006: DelayLine on mono sum before engine; 0.5-2ms range; per coupling_integration.h chain
  - FR-010: kSnareBuzzId = 271, 0.0-1.0, default 0.0
  - FR-011: kTomResonanceId = 272, 0.0-1.0, default 0.0
  - FR-012: kGlobalCouplingId = 270, early-out via isBypassed()
  - FR-013: Tier 1 knobs map to matrix coefficients via recomputeFromTier1()
  - FR-014: Effective gain formula: globalCoupling * effectiveGain[src][dst] * padCouplingAmount[src] * padCouplingAmount[dst]
  - FR-020: kPadCouplingAmount = 36, default 0.5, range 0.0-1.0
  - FR-021: Per-pad amount controls both source strength and receiver sensitivity
  - FR-022: Per-pad coupling amounts included in kit presets, excluded from per-pad presets -- verify preset save/load code
  - FR-023: couplingAmount == 0 skips noteOn registration
  - FR-030: Two-layer resolver with hasOverride flag, flat effectiveGain array
  - FR-031: kMaxCoefficient = 0.05f clamp on setOverride and load
  - FR-032: Matrix stored in state v5
  - FR-033: Priority-ordered rule chain in classifyPad()
  - FR-034: Tier 1 recompute on knob change; Tier 2 override preserves computedGain for non-overridden pairs
  - FR-040: Energy limiter with kThreshold = 0.1f (-20 dBFS), transparent (no audible artifacts)
  - FR-041: Velocity scaling via SympatheticResonance noteOn (velocity param)
  - FR-042: CPU cap via eviction logic in SympatheticResonance (64 resonator pool)
  - FR-043: kMaxSympatheticResonators = 64 (existing engine constant)
  - FR-050: kCurrentStateVersion = 5
  - FR-051: v4 load sets all Phase 5 params to defaults
  - FR-052: v1->v2->v3->v4->v5 migration chain succeeds
  - FR-053: State format: uint16 count + (uint8 src, uint8 dst, float32 coeff) per entry; v4 migration writes count=0
  - FR-060: IDs 270-273 registered correctly
  - FR-061: padParamId(N, 36) for per-pad coupling amounts
  - FR-062: static_assert(kSelectedPadId < kGlobalCouplingId)
  - FR-070: noteOn extracts 4 partials and registers with coupling engine
  - FR-071: noteOff called on coupling engine on voice release/choke/steal
  - FR-072: process() called once per sample after voices rendered, before master output
  - FR-073: Coupling output added to main bus only

- [ ] T072 Verify ALL SC-xxx success criteria with actual test output (copy test results, do not paraphrase):
  - SC-001: Global Coupling 0 output identical to Phase 4 (< -120 dBFS difference) -- cite test name and assertion
  - SC-002: Kick + Snare Buzz 50% + Global 100% produces measurable 1-8 kHz energy -- cite measurement
  - SC-003: CPU < 1.5% with 8 voices + 64 resonators -- cite benchmark or analysis
  - SC-004: Bypass CPU < 0.01% -- cite test or timing measurement
  - SC-005: State v5 round-trip zero loss -- cite test name
  - SC-006: v4 load produces Phase 4 behavior -- cite test name
  - SC-007: Energy limiter prevents > -20 dBFS -- cite test name and measurement
  - SC-008: Mode-coincident toms produce >= 12 dB more coupling than mode-gap toms -- cite test name and dB difference
  - SC-009: Zero allocations in 10-second fuzz test (use AllocationDetector from test helpers)
  - SC-010: Pluginval level 5 passes -- cite pluginval output

### 12.2 Fill Compliance Table in spec.md

- [ ] T073 Update `specs/140-membrum-phase5-coupling/spec.md` "Implementation Verification" section with status and evidence for every FR-xxx and SC-xxx row

### 12.3 Honest Self-Check

Answer these questions before claiming completion:
1. Did you change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did you remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If you were the user, would you feel cheated?

- [ ] T074 All self-check questions answered "no" (or gaps documented honestly)

---

## Phase 13: Final Completion

- [ ] T075 Final build and full test run: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests dsp_tests && build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5 && build/windows-x64-release/bin/Release/dsp_tests.exe "*modal*" 2>&1 | tail -5`
- [ ] T076 Verify all spec work is committed to `140-membrum-phase5-coupling` branch (not main)
- [ ] T077 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 -- BLOCKS all user stories
- **Phase 3 (US1 -- Snare Buzz)**: Depends on Phase 2 -- implements core signal chain
- **Phase 4 (US2 -- Tom Resonance)**: Depends on Phase 3 (reuses signal chain wiring)
- **Phase 5 (US3 -- Global Coupling)**: Depends on Phase 3 (verifies bypass path from US1)
- **Phase 6 (US4 -- Per-Pad Amount)**: Depends on Phase 2 (PadConfig extension) + Phase 3 (noteOn hook)
- **Phase 7 (US5 -- State v5)**: Depends on Phases 3-6 (all parameters must exist before state round-trip)
- **Phase 8 (Matrix Data Model)**: Depends on Phase 2 (CouplingMatrix from foundational)
- **Phase 9 (Polish)**: Depends on Phases 3-8
- **Phase 10 (Clang-Tidy)**: Depends on Phase 9
- **Phase 11 (Docs)**: Depends on Phase 10
- **Phase 12 (Verification)**: Depends on all prior phases
- **Phase 13 (Final)**: Depends on Phase 12

### User Story Dependencies

- **US1 (P1 -- Snare Buzz)**: Core signal chain -- all other stories build on this
- **US2 (P1 -- Tom Resonance)**: Independent of US1 in logic but reuses signal chain from US1
- **US3 (P1 -- Global Coupling)**: Independent verification of bypass path from US1
- **US4 (P2 -- Per-Pad Amount)**: Depends on PadConfig extension (Phase 2) and noteOn hook (US1)
- **US5 (P2 -- State v5)**: Depends on all parameters existing (US1-US4 complete)
- **US6 (P3 -- Matrix Model)**: Depends on CouplingMatrix (Phase 2 -- largely already implemented)

### Within Each User Story

- **Tests FIRST**: Must FAIL before implementation
- Models before services before integration
- Build after every implementation task
- Cross-platform check (IEEE 754) after each story
- Commit after each story

### Parallel Opportunities Within Phases

- **Phase 2**: T005-T007 (ModalResonatorBank) can run in parallel with T008-T010 (PadConfig)
- **Phase 2**: T011-T013 (PadCategory) and T014-T016 (CouplingMatrix) can run in parallel after PadConfig is done
- **Phase 3**: T018 and T019 (test writing) can run in parallel
- **Phase 6**: T042 (tests) can run in parallel with other story verification
- **Phase 9**: T060, T061, T062 (edge case checks) can run in parallel

---

## Parallel Execution Examples

### Phase 2 Parallel Batches

```
Batch 1 (after T001-T004):
  Agent A: T005-T007 (ModalResonatorBank accessor + dsp_tests)
  Agent B: T008-T010 (PadConfig extension + offset 36)

Batch 2 (after both Batch 1 tasks complete):
  Agent A: T011-T013 (PadCategory tests + implementation)
  Agent B: T014-T016 (CouplingMatrix tests + implementation)
```

### Phase 3 Parallel Test Writing

```
Batch (after Phase 2 complete):
  Agent A: T018 (signal chain integration test)
  Agent B: T019 (energy limiter test)
```

---

## Implementation Strategy

### Full Feature (All User Stories)

All 5 user stories (plus matrix data model) are required per project policy (no MVP subset). Implement in this order:

1. Phase 1: Setup
2. Phase 2: Foundational (ModalResonatorBank, PadConfig, PadCategory, CouplingMatrix)
3. Phase 3: US1 (Snare Buzz -- core signal chain)
4. Phase 4: US2 (Tom Resonance -- validate category-based matrix computation)
5. Phase 5: US3 (Global Coupling -- verify bypass)
6. Phase 6: US4 (Per-Pad Amount -- exclusion logic + 32 parameter registration)
7. Phase 7: US5 (State v5 -- round-trip and migration)
8. Phase 8: Matrix Data Model (gap analysis + any residual matrix fixes)
9. Phase 9-13: Polish, static analysis, docs, verification, final commit

---

## Notes

- [P] tasks = different files, no dependencies -- can run in parallel
- [Story] label maps task to specific user story for traceability
- **Build command**: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests`
- **Test run**: `build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling]" 2>&1 | tail -5`
- **DSP tests**: `build/windows-x64-release/bin/Release/dsp_tests.exe "*modal*" 2>&1 | tail -5`
- **Pluginval**: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"`
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance for all new test files
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest evidence
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
- Per project memory: Speckit tasks authorize commits -- commit per-phase without re-asking
- Per project memory: Always full feature, never MVP subset
