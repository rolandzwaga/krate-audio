# Tasks: Modal Resonator Bank for Physical Modelling

**Input**: Design documents from `/specs/127-modal-resonator-bank/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/
**Branch**: `127-modal-resonator-bank`
**Plugin**: Innexus

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

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in the appropriate CMakeLists.txt
   - Pattern for `dsp/tests/CMakeLists.txt`:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/test_modal_resonator_bank.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```
   - Same pattern for `plugins/innexus/tests/CMakeLists.txt` for `test_physical_model.cpp`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Register feature branch, add parameter IDs and CMake entries so the build system is ready for new code.

- [X] T001 Verify feature branch `127-modal-resonator-bank` is checked out (NEVER implement on main)
- [X] T002 Add 5 new parameter IDs to `plugins/innexus/src/plugin_ids.h`: `kPhysModelMixId=800`, `kResonanceDecayId=801`, `kResonanceBrightnessId=802`, `kResonanceStretchId=803`, `kResonanceScatterId=804`
- [X] T003 [P] Add `test_modal_resonator_bank.cpp` source entry to `dsp/tests/CMakeLists.txt` (file does not exist yet -- CMake entry added now so the build knows about it)
- [X] T004 [P] Add `test_physical_model.cpp` source entry to `plugins/innexus/tests/CMakeLists.txt`

**Checkpoint**: Branch verified, parameter IDs allocated, CMake entries ready. Build will fail to link (missing sources) until Phase 2.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented. Establishes the two new classes in stub form so all later phases can build.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T005 Create `dsp/include/krate/dsp/processors/modal_resonator_bank.h` matching the API contract in `specs/127-modal-resonator-bank/contracts/modal_resonator_bank.h` -- full class skeleton with SoA state arrays (`alignas(32) float sinState_[96]` etc.), all public method declarations, all private constants (`kTransientEmphasisGain=4.0f`, `kMaxB3=4.0e-5f`, `kSilenceThreshold=1e-12f`, `kNyquistGuard=0.49f`, `kAmplitudeThresholdLinear=0.0001f`, `kSmoothingTimeMs=2.0f`, `kEnvelopeAttackMs=5.0f`, `kSoftClipThreshold=0.707f`), and stub bodies that compile (no-op or return 0)
- [X] T006 Create `plugins/innexus/src/dsp/physical_model_mixer.h` matching the API contract in `specs/127-modal-resonator-bank/contracts/physical_model_mixer.h` -- stateless struct with inline `static float process(float, float, float, float) noexcept` body implementing `harmonic + (1-mix)*residual + mix*physical`
- [X] T007 Add `modalResonator` field of type `Krate::DSP::ModalResonatorBank` to `InnexusVoice` struct in `plugins/innexus/src/processor/innexus_voice.h`; forward-include the new header; update `prepare()` and `reset()` stubs to call through to `modalResonator.prepare()` and `modalResonator.reset()`
- [X] T008 Add 5 `std::atomic<float>` fields (physModelMix_, resonanceDecay_, resonanceBrightness_, resonanceStretch_, resonanceScatter_) to `plugins/innexus/src/processor/processor.h` with correct defaults (mix=0, decay=0.5, brightness=0.5, stretch=0, scatter=0)
- [X] T009 Build `dsp_tests` and `innexus_tests` targets to confirm the skeleton compiles with zero errors: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests innexus_tests`

**Checkpoint**: Both targets build. Stub implementations produce no audio changes yet. All existing Innexus tests continue to pass.

---

## Phase 3: User Story 1 - Physical Resonance from Analyzed Sound (Priority: P1) MVP

**Goal**: Implement the complete modal resonator bank DSP and wire it into the Innexus voice render loop. Physical Model Mix at 0% must be bit-exact with prior behavior. At 100%, the residual path is fully replaced by modal resonator output.

**User Stories covered**: US1 (core resonator), US4 (backwards compatibility -- mix=0 path is proven here)

**Independent Test**: Load any analyzed sample, sweep Physical Model Mix from 0 to 100%, verify audio changes from flat noise to ringing resonance. At mix=0 the output must be bit-exact to the pre-feature reference.

### 3.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T010 [P] [US1] Write unit tests for `ModalResonatorBank` core algorithm in `dsp/tests/unit/processors/test_modal_resonator_bank.cpp`:
  - `prepare()` sets `isPrepared()` true
  - `reset()` clears all sin/cos states to zero
  - `setModes()` followed by `processSample()` with a nonzero excitation produces nonzero output (basic signal flow)
  - Single-mode impulse response: configure one mode at 440 Hz, feed one impulse, verify subsequent samples decay exponentially (SC-004 precursor)
  - `processSample()` returns 0 when no modes are configured (after `reset()` only)
  - `getNumActiveModes()` returns 0 before `setModes()`, correct count after
  - Nyquist culling: mode at exactly `0.49 * sampleRate` Hz is culled (`getNumActiveModes()` excludes it)
  - Amplitude culling: mode with amplitude < `0.0001f` is culled
  - Mode count respects `numPartials` argument clamped to `[0, kMaxModes]`
  - SC-003 (denormal protection): after `setModes()` with very low damping followed by 30-second silence (30*44100 samples), `flushSilentModes()` has zeroed all states below `kSilenceThreshold` -- no NaN/Inf in output and no runtime slowdown from denormal traps
  - SC-004 (amplitude stability): configure one mode with R=0.99999 (very low damping); excite with a single impulse; measure the output amplitude envelope at 1s, 5s, 10s after the impulse; verify it tracks the expected exponential `R^(n*sampleRate)` within 0.5 dB
  - Coefficient smoothing: call `setModes()` to configure a mode at 440 Hz, then call `updateModes()` with the same mode at 880 Hz; after exactly 1 sample, `epsilon_[0]` must NOT equal `epsilonTarget_[0]` (smoothing is active, not a snap); after N samples where N = `ceil(-log(0.01) / (1.0 - smoothCoeff_))` (derived from 2ms time constant at 44100 Hz), `epsilon_[0]` must be within 1% of `epsilonTarget_[0]`

- [X] T011 [P] [US1] Write unit tests for `PhysicalModelMixer` in `plugins/innexus/tests/unit/processor/test_physical_model.cpp` (built by the `innexus_tests` binary, NOT `dsp_tests`):
  - At mix=0: `process(h, r, p, 0)` equals `h + r` (bit-exact backwards compat, SC-001 precursor)
  - At mix=1: `process(h, r, p, 1)` equals `h + p`
  - At mix=0.5: output is midpoint blend
  - Harmonic signal passes through unchanged at all mix values

- [X] T012 [US1] Write integration tests in `plugins/innexus/tests/unit/processor/test_physical_model.cpp`:
  - At `kPhysModelMixId` normalized=0: render 512 samples and compare output to reference render from before the feature (SC-001 -- store a reference block, verify exact equality)
  - At `kPhysModelMixId` normalized=1: render 512 samples with a loaded analysis, verify resonant output is nonzero and distinct from residual-only output
  - At mix=0.5: output is between the two extremes (not silence, not 100% physical)

### 3.2 Implementation for User Story 1

- [X] T013 [P] [US1] Implement `ModalResonatorBank::prepare(double sampleRate)` in `dsp/include/krate/dsp/processors/modal_resonator_bank.h`: store `sampleRate_`, compute `smoothCoeff_ = std::exp(-1.0f / (kSmoothingTimeMs * 0.001f * sampleRate_))`, compute `envelopeAttackCoeff_ = std::exp(-1.0f / (kEnvelopeAttackMs * 0.001f * sampleRate_))`, set `prepared_ = true`

- [X] T014 [P] [US1] Implement `ModalResonatorBank::reset()`: zero all `sinState_`, `cosState_`, zero `envelopeState_` and `previousEnvelope_`, snap all smoothed arrays to their targets (epsilon_, radius_, inputGain_ = their target arrays)

- [X] T015 [US1] Implement `ModalResonatorBank::computeModeCoefficients()` private helper in `dsp/include/krate/dsp/processors/modal_resonator_bank.h`:
  - Clamp parameters to valid ranges
  - Compute `b1 = 1.0f / decayTime`, `b3 = (1.0f - brightness) * kMaxB3`
  - For each partial k in `[0, numPartials)`:
    - Apply Stretch: `float B = stretch * stretch * 0.001f; float f_w = f_k * std::sqrt(1.0f + B * k * k)`
    - Apply Scatter: `constexpr float D = ...; float C = scatter * 0.02f; f_w *= (1.0f + C * std::sin(k * D))`
    - Nyquist cull: if `f_w >= kNyquistGuard * sampleRate_`, mark inactive and continue
    - Amplitude cull: if `amplitudes[k] < kAmplitudeThresholdLinear`, mark inactive and continue
    - Compute `decayRate_k = b1 + b3 * f_w * f_w`
    - Compute `R_k = std::exp(-decayRate_k / sampleRate_)`
    - Compute `epsilon_k = 2.0f * std::sin(std::numbers::pi_v<float> * f_w / sampleRate_)`
    - Compute `inputGain_k = amplitudes[k] * (1.0f - R_k)` (leaky-integrator compensation per FR-009)
    - Store into target arrays; set `active_[k] = true`
  - Modes from `numPartials` to `numModes_` set inactive
  - Update `numActiveModes_`
  - If `snapSmoothing`: snap epsilon_, radius_, inputGain_ to their target arrays immediately

- [X] T016 [US1] Implement `ModalResonatorBank::setModes()`: zero all filter states (sin/cos), call `computeModeCoefficients(..., snapSmoothing=true)` (note-on path clears state per FR-018)

- [X] T017 [US1] Implement `ModalResonatorBank::updateModes()`: do NOT zero filter states, call `computeModeCoefficients(..., snapSmoothing=false)` (frame-transition path per FR-019)

- [X] T018 [US1] Implement `ModalResonatorBank::applyTransientEmphasis(float sample)` private helper:
  - One-pole envelope follower: `envelopeState_ = envelopeAttackCoeff_ * envelopeState_ + (1.0f - envelopeAttackCoeff_) * std::abs(sample)`
  - Compute derivative: `float derivative = envelopeState_ - previousEnvelope_`
  - Continuous proportional boost: `float emphasis = 1.0f + kTransientEmphasisGain * std::max(0.0f, derivative)` (NOT binary on/off — subtle transients get gentle boost, strong transients get proportionally larger emphasis)
  - `previousEnvelope_ = envelopeState_`
  - Return `sample * emphasis`
  - Add comment: `// kTransientEmphasisGain may be promoted to a user-facing parameter in a future phase`

- [X] T019 [US1] Implement `ModalResonatorBank::processSample(float excitation)` -- scalar implementation:
  - Apply transient emphasis: `float ex = applyTransientEmphasis(excitation)`
  - One-pole smooth epsilon_, radius_, inputGain_ toward their targets: `val = val + (1.0f - smoothCoeff_) * (target - val)` (per FR-020, ~2ms time constant)
  - Inner loop over active modes: implement coupled-form per quickstart.md algorithm: `s_new = R*(s + eps*c) + gain*ex; c_new = R*(c - eps*s_new); output += s_new`
  - Apply soft-clip safety limiter via existing `Krate::DSP::softClip()`: scale by `kSoftClipThreshold`, clip, scale back (per FR-010, R-007)
  - Return clipped output

- [X] T020 [US1] Implement `ModalResonatorBank::processBlock(const float* input, float* output, int numSamples)`:
  - Loop calling `processSample()` per sample
  - Call `flushSilentModes()` once per block (per FR-027, R-013)

- [X] T021 [US1] Implement `ModalResonatorBank::flushSilentModes()`:
  - For each active mode, check `sinState_[k]*sinState_[k] + cosState_[k]*cosState_[k] < kSilenceThreshold`
  - If below threshold, zero both states and mark inactive, decrement `numActiveModes_`

- [X] T022 [US1] Handle new parameter changes in `plugins/innexus/src/processor/processor_params.cpp`:
  - For `kPhysModelMixId`, `kResonanceDecayId`, `kResonanceBrightnessId`, `kResonanceStretchId`, `kResonanceScatterId`: store denormalized values into the corresponding `std::atomic<float>` fields added in T008
  - Log mapping for `kResonanceDecayId`: `decaySeconds = 0.01f * std::pow(500.0f, normalizedValue)` (maps 0->0.01s, 1->5.0s with log curve)

- [X] T023 [US1] Wire modal resonator into voice render loop in `plugins/innexus/src/processor/processor.cpp`:
  - After `residualSynth.process()` produces `residualSample`, read atomics for all 5 new parameters
  - Call `voice.modalResonator.processSample(residualSample)` to get `physicalSample` (transient emphasis is internal)
  - Replace the simple harmonic+residual mix with `PhysicalModelMixer::process(harmonicMono, residualSample * resLevel, physicalSample, physModelMix)` per FR-023
  - Note: `residualSample * resLevel` is computed and passed in to `PhysicalModelMixer::process()` as its second argument. The `residualSample` fed to `modalResonator.processSample()` (first bullet) is the RAW unscaled residual -- `resLevel` scaling is NOT applied before the excitation path. Only the mixer's dry-residual argument receives the `resLevel` scale.
  - At `physModelMix=0` the formula reduces to `harmonic + residual * resLevel` -- bit-exact with pre-feature behavior (SC-001, FR-024)
  - Add `#include "dsp/physical_model_mixer.h"` at top of file

- [X] T024 [US1] Call `voice.modalResonator.setModes()` on note-on (FR-018) and `voice.modalResonator.updateModes()` on frame advance (FR-019) in processor.cpp, passing frequencies/amplitudes from the current `HarmonicFrame`, and the 4 material parameters

- [X] T025 [US1] Build both targets and run all tests:
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests innexus_tests
  build/windows-x64-release/bin/Release/dsp_tests.exe "ModalResonatorBank*" 2>&1 | tail -10
  build/windows-x64-release/bin/Release/innexus_tests.exe "PhysicalModel*" 2>&1 | tail -10
  ```
  Fix ALL failures before continuing. No failures are acceptable.

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T026 [US1] Check `test_modal_resonator_bank.cpp` and `test_physical_model.cpp` for use of `std::isnan`, `std::isfinite`, `std::isinf` -- if present, add both files to the `-fno-fast-math` list in their respective CMakeLists.txt files

### 3.4 Commit (MANDATORY)

- [X] T027 [US1] **Commit completed User Story 1 work** (core resonator DSP + voice wiring + backwards compat mix)

**Checkpoint**: Core resonator is functional. Mix=0 is bit-exact with prior behavior. Mix=100% produces resonant output from analyzed partials. Both test targets pass.

---

## Phase 4: User Story 2 - Material Character Sculpting (Priority: P1)

**Goal**: Verify that the Decay and Brightness parameters produce the correct frequency-dependent damping behavior as specified by the Chaigne-Lambourg model. The implementation was already written in Phase 3 (T015 computes Chaigne-Lambourg coefficients); this phase adds the specific tests that validate measurable damping behavior per SC-005 and SC-006.

**Independent Test**: Sweep Brightness from 0 to 1 and verify measurably different decay profiles. Sweep Decay and verify T60 scales proportionally.

### 4.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T028 [P] [US2] Add damping behavior tests to `dsp/tests/unit/processors/test_modal_resonator_bank.cpp`:
  - SC-005 (Brightness sweep): Configure one mode at 2000 Hz and one at 100 Hz. At Brightness=0, measure T60 of each; verify 2 kHz mode decays at least 3x faster than 100 Hz mode. At Brightness=1, verify both modes decay within 20% of the same rate. Use `kMaxB3 = 4.0e-5f` formula to compute expected R values and check against actual sinState decay
  - SC-006 (Decay scaling): Configure a single mode at 440 Hz. Measure T60 at `decayTime=0.5s` and `decayTime=1.0s`; verify doubling decay time approximately doubles T60 (within 10% tolerance)
  - Decay parameter boundary: `decayTime=0.01s` produces very short ring (mode decays within 100 samples at 44100 Hz); `decayTime=5.0s` produces long ring (mode still active after 100k samples)
  - Brightness=0 (wood): high-freq modes decay faster than fundamental -- verify R values for 5000 Hz mode are smaller than R for 100 Hz mode
  - Brightness=1 (metal): R values for 5000 Hz and 100 Hz modes are within 5% of each other

### 4.2 Implementation for User Story 2

No new implementation code is needed. The Chaigne-Lambourg damping (`decayRate_k = b1 + b3 * f_k'^2`) was already implemented in `computeModeCoefficients()` during Phase 3 (T015). This phase is purely about verifying the damping math is correct with targeted tests.

- [X] T029 [US2] If any test from T028 fails, fix the `computeModeCoefficients()` implementation in `dsp/include/krate/dsp/processors/modal_resonator_bank.h` until all SC-005 and SC-006 criteria pass. Common mistakes to check: wrong sign on damping formula, using frequency in radians instead of Hz, off-by-one on `brightness` inversion (`b3 = (1.0f - brightness) * kMaxB3` -- NOT `brightness * kMaxB3`)

- [X] T030 [US2] Build and verify tests pass:
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
  build/windows-x64-release/bin/Release/dsp_tests.exe "ModalResonatorBank*" 2>&1 | tail -10
  ```

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T031 [US2] Verify no new IEEE 754 function usage was introduced. If `std::exp`, `std::log` are called in test assertions (not implementation), no action needed -- those are safe. Only `std::isnan`/`std::isfinite`/`std::isinf` require `-fno-fast-math`.

### 4.4 Commit (MANDATORY)

- [X] T032 [US2] **Commit completed User Story 2 work** (damping behavior tests + any damping fixes)

**Checkpoint**: Decay and Brightness parameters produce measurably correct Chaigne-Lambourg damping. SC-005 and SC-006 pass.

---

## Phase 5: User Story 3 - Inharmonic Mode Warping (Priority: P2)

**Goal**: Verify that Stretch and Scatter parameters produce measurably inharmonic mode frequencies, and that warped frequencies feed into the damping model correctly.

**Independent Test**: Set Stretch and Scatter to extreme values, measure impulse response spectrum and verify mode frequencies are displaced from harmonic positions.

### 5.1 Tests for User Story 3 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T033 [P] [US3] Add inharmonic warping tests to `dsp/tests/unit/processors/test_modal_resonator_bank.cpp`:
  - Stretch=0, Scatter=0: configured at frequencies `[100, 200, 300, 400]` Hz, verify impulse response spectrum peaks within ±1 Hz of those frequencies (SC-007 for harmonic case)
  - Stretch=1 (maximum): mode k=5 at 500 Hz should warp to `500 * sqrt(1 + 0.001 * 25) = 500 * sqrt(1.025) ≈ 506.2 Hz` -- verify `epsilonTarget_[4]` encodes this frequency (compute expected epsilon and compare with tolerance)
  - Scatter=1 (maximum): mode k=1 at 200 Hz, `C=0.02`, `D=pi*(sqrt(5)-1)/2 ≈ 1.9416`, displacement = `200 * (1 + 0.02 * sin(1 * D)) ≈ 200 * (1 + 0.02 * 0.9511) ≈ 203.8 Hz` -- verify epsilon encodes this
  - Stretch and Scatter both non-zero: effects combine multiplicatively (FR-014), verify the warped frequency used in damping model is the fully-warped frequency, not the original
  - Warped frequencies above Nyquist (0.49 * sampleRate) are culled even when Stretch pushes them over the limit -- verify `getNumActiveModes()` decreases when Stretch is high enough to push the highest partial above Nyquist

- [ ] T034 [P] [US3] SC-007 spectral accuracy test (below fs/6 modes): configure a single mode at 440 Hz with Stretch=0, Scatter=0; feed one impulse; measure frequency of the peak in the resulting impulse response using DFT over ~4096 samples; verify peak frequency is within ±1 Hz of 440 Hz

### 5.2 Implementation for User Story 3

No new implementation code is needed. The Stretch and Scatter warping was already implemented in `computeModeCoefficients()` during Phase 3 (T015). This phase is purely about verifying the warping math is correct with targeted tests.

- [ ] T035 [US3] If any test from T033/T034 fails, fix the Stretch and Scatter computation in `computeModeCoefficients()` in `dsp/include/krate/dsp/processors/modal_resonator_bank.h`. Common mistakes: using `k` as 0-indexed when formula uses 1-indexed, wrong formula for stiff-string inharmonicity (`sqrt(1 + B*k^2)` not `1 + B*k`), wrong golden-ratio constant for scatter

- [ ] T036 [US3] Build and verify tests pass:
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
  build/windows-x64-release/bin/Release/dsp_tests.exe "ModalResonatorBank*" 2>&1 | tail -10
  ```

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T037 [US3] If the SC-007 DFT test in T034 uses `std::isnan` or `std::isinf` for NaN/Inf detection in the DFT buffer, add the test file to `-fno-fast-math` in `dsp/tests/CMakeLists.txt`

### 5.4 Commit (MANDATORY)

- [ ] T038 [US3] **Commit completed User Story 3 work** (inharmonic warping tests + any warping fixes)

**Checkpoint**: Stretch and Scatter produce measurably inharmonic modes. SC-007 spectral accuracy passes. All three user stories are independently tested and committed.

---

## Phase 6: User Story 4 - Backwards Compatibility (Priority: P1)

**Goal**: Formally verify that at Physical Model Mix = 0%, the output is bit-exact identical to pre-feature behavior, and that old presets load cleanly with the new parameters defaulting correctly.

**Note**: The bit-exact behavior at mix=0 was engineered in Phase 3 (T023) and tested in T012. This phase adds the explicit state save/load tests and controller parameter registration to complete the full backwards-compat story.

**Independent Test**: Load a preset without the new parameters and verify default values are used. Save and reload a preset with the new parameters and verify they round-trip correctly.

### 6.1 Tests for User Story 4 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T039 [P] [US4] Add backwards-compat tests to `plugins/innexus/tests/unit/processor/test_physical_model.cpp`:
  - State load from old stream (stream ends before new params): verify `kPhysModelMixId` defaults to 0.0, `kResonanceDecayId` defaults to 0.5s normalized, `kResonanceBrightnessId` defaults to 0.5, `kResonanceStretchId` defaults to 0.0, `kResonanceScatterId` defaults to 0.0
  - State round-trip: save state with all 5 new params at non-default values, reload, verify all 5 values are restored exactly
  - SC-001: Render 512 samples with `kPhysModelMixId=0` and verify output matches a pre-computed reference block (capture a reference from the unmodified Innexus output for the same voice configuration)

### 6.2 Implementation for User Story 4

- [ ] T040 [US4] Add state save/load for 5 new parameters in `plugins/innexus/src/processor/processor_state.cpp`:
  - On save (getState): after all existing `streamer.writeFloat()` calls, append: `streamer.writeFloat(physModelMix_)`, `streamer.writeFloat(resonanceDecay_)`, `streamer.writeFloat(resonanceBrightness_)`, `streamer.writeFloat(resonanceStretch_)`, `streamer.writeFloat(resonanceScatter_)`
  - On load (setState): after all existing reads, use optional reads with defaults: `if (streamer.readFloat(val)) physModelMix_ = val; else physModelMix_ = 0.0f;` (same pattern for other 4 params with their defaults)

- [ ] T041 [US4] Register 5 new parameters in `plugins/innexus/src/controller/controller.cpp` `initialize()` method:
  - `kPhysModelMixId`: name="Physical Model Mix", range=[0,1], default=0, linear, unitless
  - `kResonanceDecayId`: name="Decay", range=[0,1] normalized (log-mapped in processor), default=normalized(0.5s), unit=seconds
  - `kResonanceBrightnessId`: name="Brightness", range=[0,1], default=0.5, linear, unitless
  - `kResonanceStretchId`: name="Stretch", range=[0,1], default=0, linear, unitless
  - `kResonanceScatterId`: name="Scatter", range=[0,1], default=0, linear, unitless
  - Follow the exact registration pattern used for existing Innexus parameters

- [ ] T042 [US4] Build `innexus_tests` and verify all tests pass including the regression test from T039:
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target innexus_tests
  build/windows-x64-release/bin/Release/innexus_tests.exe "PhysicalModel*" 2>&1 | tail -10
  ```

- [ ] T043 [US4] Run ALL existing Innexus tests (not just the new ones) to verify SC-008 (no regressions):
  ```
  build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5
  ```
  Fix ALL failures. No pre-existing failures are acceptable.

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T044 [US4] Verify IEEE 754 compliance for `test_physical_model.cpp` -- if the SC-001 reference comparison uses floating-point equality, confirm it is bit-exact integer comparison (not floating-point comparison that would be affected by fast-math). If any `std::isnan`/`std::isinf` are used, add to `-fno-fast-math` list.

### 6.4 Commit (MANDATORY)

- [ ] T045 [US4] **Commit completed User Story 4 work** (state save/load, controller registration, backwards-compat tests)

**Checkpoint**: Old presets load cleanly with new parameter defaults. State round-trips correctly. SC-001 bit-exact at mix=0 is formally proven with a test.

---

## Phase 7: User Story 5 - Polyphonic Physical Modelling (Priority: P2)

**Goal**: Verify that 8-voice polyphony with Physical Model Mix active remains within CPU budget (SC-002), and that each voice has independently tuned resonance.

**Independent Test**: Play 8-note chord with mix=100%, monitor CPU usage and verify independent tuning per voice.

### 7.1 Tests for User Story 5 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T046 [P] [US5] Add polyphony and performance tests to `plugins/innexus/tests/unit/processor/test_physical_model.cpp`:
  - Voice independence: configure two `ModalResonatorBank` instances with different partial frequency sets, feed the same impulse to both, verify their outputs differ (independent tuning per FR-025)
  - Mode count respects `kPartialCountId`: configure bank with `numPartials=48`, verify `getNumActiveModes() <= 48`; repeat for 64, 80, 96 -- verify `getNumActiveModes()` never exceeds the requested count (FR-017)
  - Note-off ring: simulate a note-off by calling the processor's note-off handler (which resets the ADSR and residual synth but must NOT call `modalResonator.reset()`); immediately after note-off, verify that `modalResonator.processSample(0.0f)` still returns non-zero output -- confirming the resonator rings free of the voice envelope (FR-026). Do not test this by manually skipping a `reset()` call; exercise it through the actual processor note-off code path so the test will catch any future regression that inadvertently adds a `modalResonator.reset()` call to that path.

- [ ] T047 [P] [US5] Add performance benchmark test (tagged `[.perf]`) to `dsp/tests/unit/processors/test_modal_resonator_bank.cpp`:
  - SC-002b: Create 8 `ModalResonatorBank` instances, each configured with 96 active modes at 44.1 kHz; time a 512-sample block across all 8 instances; verify total block time < 5% of 512/44100 available time (approximately 58ms budget for 5%)
  - SC-002a: Time a 128-sample block across all 8 instances; verify worst-case time < 80% of 128/44100 = 2.32ms

### 7.2 Implementation for User Story 5

No new implementation code is needed. Each `InnexusVoice` already owns one `ModalResonatorBank` instance (added in T007). The processor already processes each voice independently. This phase verifies the architecture is correct.

- [ ] T048 [US5] If the performance tests from T047 reveal SC-002a or SC-002b violations, apply the following optimizations in order of impact (stop when both criteria pass):
  1. Verify mode culling is working correctly -- inactive modes must be skipped in the inner loop of `processSample()`, not just flagged
  2. Verify `flushSilentModes()` is zeroing decayed mode states so they are culled on subsequent blocks
  3. If still over budget, open `dsp/include/krate/dsp/processors/modal_resonator_bank.h` and move the smoothing step (`epsilon_ = epsilon_ + (1-c) * (target-epsilon_)`) outside the sample loop to a once-per-block update (acceptable since smoothing at block rate is sufficient for the coefficient updates)

- [ ] T049 [US5] Build and verify tests pass:
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests innexus_tests
  build/windows-x64-release/bin/Release/dsp_tests.exe "ModalResonatorBank*" 2>&1 | tail -10
  build/windows-x64-release/bin/Release/innexus_tests.exe "PhysicalModel*" 2>&1 | tail -10
  ```

- [ ] T050 [US5] Run performance tests explicitly (tagged `.perf`):
  ```
  build/windows-x64-release/bin/Release/dsp_tests.exe "[.perf]" 2>&1 | tail -20
  ```

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T051 [US5] The performance benchmark uses wall-clock timing -- no IEEE 754 concern. Confirm no isnan/isinf added. No CMakeLists.txt changes needed.

### 7.4 Commit (MANDATORY)

- [ ] T052 [US5] **Commit completed User Story 5 work** (polyphony tests + any performance fixes)

**Checkpoint**: 8-voice polyphony passes SC-002a and SC-002b. Voice independence confirmed. Mode count respects kPartialCountId.

---

## Phase 8: SIMD Acceleration (Performance Enhancement)

**Purpose**: Replace the scalar inner loop in `processSample()` with a Google Highway SIMD kernel. This is the "Phase 2 SIMD" step from plan.md. Scalar path and all tests remain unchanged; SIMD is an internal optimization that must produce identical output.

**Prerequisite**: All previous phases complete and passing. CPU targets SC-002a/SC-002b must already pass in scalar form (if they do not, fix scalar first before adding SIMD complexity).

**Test-first exception**: SIMD is a pure internal optimization that must produce numerically identical output to the scalar path. No new tests need to be written before implementing the SIMD kernel. The scalar tests written in Phases 3-5 serve as the oracle: if the SIMD kernel is correct, all existing `ModalResonatorBank*` tests will continue to pass unchanged. This is consistent with Constitution Principle XII because the observable behavior is already fully covered -- the SIMD path adds no new behavior to specify.

- [ ] T053 Create `dsp/include/krate/dsp/processors/modal_resonator_bank_simd.cpp` using the Highway self-inclusion pattern (`HWY_TARGET_INCLUDE`, `HWY_EXPORT`, `HWY_DYNAMIC_DISPATCH`) following the exact template of `dsp/src/harmonic_oscillator_bank_simd.cpp`; implement the coupled-form inner loop over SoA arrays using `hn::ScalableTag<float>` (FR-005)

- [ ] T054 Add `modal_resonator_bank_simd.cpp` to `dsp/CMakeLists.txt` under the SIMD source list (same pattern as `harmonic_oscillator_bank_simd.cpp`)

- [ ] T055 Update `ModalResonatorBank::processBlock()` to dispatch to the SIMD kernel via `HWY_DYNAMIC_DISPATCH` for the bulk of modes, with a scalar tail loop for remainder modes (same scalar-plus-tail pattern as `HarmonicOscillatorBank`)

- [ ] T056 Build `dsp_tests` and run ALL existing `ModalResonatorBank*` tests to confirm SIMD output is identical to scalar baseline (the tests written in Phases 3-5 serve as correctness regression for the SIMD path):
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
  build/windows-x64-release/bin/Release/dsp_tests.exe "ModalResonatorBank*" 2>&1 | tail -10
  ```

- [ ] T057 Run performance tests again to confirm SIMD provides measurable improvement over scalar:
  ```
  build/windows-x64-release/bin/Release/dsp_tests.exe "[.perf]" 2>&1 | tail -20
  ```

- [ ] T058 **Commit SIMD acceleration** (modal resonator bank SIMD kernel + CMakeLists update)

**Checkpoint**: SIMD kernel accelerates the inner loop. All existing tests pass unchanged. Performance is measurably better than scalar.

---

## Phase 9: Pluginval Verification

**Purpose**: Verify the complete updated Innexus plugin passes pluginval at strictness level 5 (SC-009).

- [ ] T059 Build the full Innexus plugin:
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release
  ```
  (Permission error copying to `C:/Program Files/Common Files/VST3/` is acceptable -- compilation success is what matters.)

- [ ] T060 Run pluginval:
  ```
  tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3" 2>&1 | tail -20
  ```
  Fix ALL failures. "PASSED" on all checks is required for SC-009.

- [ ] T061 **Commit pluginval verification result** (if any fixes were required during this phase)

**Checkpoint**: Innexus passes pluginval strictness level 5. SC-009 met.

---

## Phase 10: Polish and Cross-Cutting Concerns

**Purpose**: Code review and final build verification across all user stories. SC-003 and SC-004 are validated in Phase 3 tests; this phase reviews the implementation for correctness against FRs.

- [ ] T062 [P] Code review against all FRs: walk through `modal_resonator_bank.h` and verify: `kTransientEmphasisGain = 4.0f` has the required comment about future promotion (FR-022), `kMaxB3 = 4.0e-5f` is inside the class (FR-006), `(1-R)` normalization is used not `(1-R^2)/2` (FR-009), `kMaxModes = 96` matches (FR-001)

- [ ] T063 [P] Verify FR-013: confirm in `computeModeCoefficients()` that the Stretch/Scatter warping modifies only the local `f_w` variable used for resonator coefficients and does NOT touch the `HarmonicFrame::partials` data (which feeds the oscillator bank)

- [ ] T064 Build all test targets one final time and confirm zero failures:
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests innexus_tests
  build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5
  build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5
  ```

- [ ] T065 **Commit polish and cross-cutting verification**

**Checkpoint**: All FRs spot-checked. SC-003 and SC-004 were proven in Phase 3 tests. Zero test failures.

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task.

### 11.1 Architecture Documentation Update

- [ ] T066 Update `specs/_architecture_/layer-2-processors.md`: add entry for `ModalResonatorBank` including purpose ("Bank of up to 96 parallel damped coupled-form resonators for modal synthesis"), public API summary (`prepare`, `reset`, `setModes`, `updateModes`, `processBlock`, `processSample`, `flushSilentModes`), file location (`dsp/include/krate/dsp/processors/modal_resonator_bank.h`), "when to use" ("Use when you need physically-motivated resonance driven by analyzed harmonic content; designed for 96-mode SIMD operation with SoA layout"), and note on existing alternatives (`ModalResonator` = 32-mode biquad material presets; `ResonatorBank` = 16 bandpass filters with Q control -- different topologies)

- [ ] T067 [P] Update `specs/_architecture_/innexus-plugin.md` (if it exists) or the relevant Innexus architecture doc: add `PhysicalModelMixer` to plugin-local DSP section, add note about the physical modelling signal path (residual -> transient emphasis -> modal resonator -> PhysicalModelMixer blend)

### 11.2 Final Commit

- [ ] T068 **Commit architecture documentation updates**
- [ ] T069 Verify all spec work is committed to `127-modal-resonator-bank` branch: run `git status` and confirm working tree is clean

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

- [ ] T070 Generate `compile_commands.json` if not current (run from VS Developer PowerShell):
  ```powershell
  cd F:/projects/iterum
  cmake --preset windows-ninja
  ```

- [ ] T071 Run clang-tidy on new and modified files:
  ```powershell
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja
  ```

- [ ] T072 Fix all clang-tidy errors (blocking issues). Review warnings and fix where appropriate. Add `// NOLINT(<check>): <reason>` comments for intentional suppressions (e.g., performance-no-automatic-move on aligned arrays, magic-number on DSP constants that are already named constexpr).

- [ ] T073 **Commit clang-tidy fixes** if any changes were required

**Checkpoint**: Static analysis clean -- ready for completion verification.

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

- [ ] T074 **Review ALL FR-001 through FR-034 requirements** from `specs/127-modal-resonator-bank/spec.md` against actual implementation code -- open each relevant source file and confirm each FR is met; record file path and line number evidence

- [ ] T075 **Review ALL SC-001 through SC-009 success criteria** and verify measurable targets are achieved with actual test output:
  - SC-001: Cite the specific integration test name and its "PASSED" output
  - SC-002a/b: Cite the performance test timing output vs the 2.32ms and 5% thresholds
  - SC-003: Cite the Phase 3 test result (no NaN, no slowdown after 30s silence)
  - SC-004: Cite the Phase 3 amplitude stability measurement at 10 seconds (within 0.5 dB)
  - SC-005: Cite measured decay rates for 2 kHz vs fundamental at Brightness=0 and Brightness=1
  - SC-006: Cite measured T60 ratio at 0.5s vs 1.0s decay settings
  - SC-007: Cite frequency measurement result (peak within ±1 Hz of configured frequency)
  - SC-008: Run `innexus_tests.exe` with no filter and cite the "All tests passed" summary line
  - SC-009: Cite the pluginval output "PASSED" line

- [ ] T076 **Search for cheating patterns** in implementation:
  - No `// placeholder` or `// TODO` comments in new code
  - No test thresholds relaxed from spec requirements
  - No features quietly removed from scope
  - `kTransientEmphasisGain` has future-phase promotion comment
  - `kMaxB3` is inside the class (not in a shared header)
  - `(1-R)` normalization is used (not `(1-R^2)/2`)

### 13.2 Fill Compliance Table in spec.md

- [ ] T077 **Update `specs/127-modal-resonator-bank/spec.md` "Implementation Verification" section** with compliance status for each FR and SC -- include file paths, line numbers, test names, and actual measured values for every row

- [ ] T078 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Before marking complete, answer all five self-check questions from the template. If ANY answer is "yes", do not claim completion.

- [ ] T079 **All self-check questions answered "no"** (or gaps documented honestly with user notification)

**Checkpoint**: Honest assessment complete -- ready for final phase.

---

## Phase 14: Final Completion

- [ ] T080 **Verify all tests pass** one final time:
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests innexus_tests
  build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5
  build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5
  ```

- [ ] T081 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec 127 implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- can start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 -- BLOCKS all user story phases
- **Phase 3 (US1 - Core Resonator)**: Depends on Phase 2 -- this is the MVP gate
- **Phase 4 (US2 - Material Damping)**: Depends on Phase 3 (T015 must exist before T028 tests can pass)
- **Phase 5 (US3 - Inharmonic Warping)**: Depends on Phase 3 (T015 must exist) -- can run in parallel with Phase 4
- **Phase 6 (US4 - Backwards Compat)**: Depends on Phase 3 -- can run in parallel with Phases 4 and 5
- **Phase 7 (US5 - Polyphony/Perf)**: Depends on Phase 6 (controller registration must be complete)
- **Phase 8 (SIMD)**: Depends on Phase 7 (all scalar tests must pass before SIMD layer is added)
- **Phase 9 (Pluginval)**: Depends on Phase 8 (full plugin must be built)
- **Phase 10 (Polish)**: Depends on Phase 9
- **Phase 11 (Docs)**: Depends on Phase 10
- **Phase 12 (Clang-Tidy)**: Depends on Phase 11
- **Phase 13 (Verification)**: Depends on Phase 12
- **Phase 14 (Final)**: Depends on Phase 13

### User Story Dependencies

- **US1 (Core Resonator - P1)**: Foundation only -- MVP
- **US2 (Material Damping - P1)**: Depends on US1 (uses computeModeCoefficients from US1)
- **US3 (Inharmonic Warping - P2)**: Depends on US1 -- independent of US2
- **US4 (Backwards Compat - P1)**: Depends on US1 -- independent of US2/US3
- **US5 (Polyphony/Perf - P2)**: Depends on US4 (controller registration needed for full integration)

### Within Each User Story

1. **Tests FIRST**: Written and confirmed to fail before any implementation
2. **Implementation**: Written to make tests pass
3. **Cross-platform check**: IEEE 754 compliance for test files
4. **Commit**: LAST step in each phase

### Parallel Opportunities

- T003 and T004 (CMakeLists entries) can run in parallel
- T010, T011 (unit tests for ModalResonatorBank) can be written in parallel
- T013, T014 (prepare/reset implementations) can be written in parallel
- Phase 4 (Damping tests) and Phase 5 (Warping tests) can run in parallel after Phase 3
- Phase 5 (US3) and Phase 6 (US4) can run in parallel after Phase 3
- T062 and T063 (FR review tasks) can run in parallel
- T066 and T067 (architecture doc updates) can run in parallel

---

## Parallel Example: User Story 1

```bash
# Write tests in parallel (different test groups, same file):
Task T010: Unit tests for ModalResonatorBank algorithm (signal flow, culling, denormals)
Task T011: Unit tests for PhysicalModelMixer (mix formula correctness)

# Implement prepare/reset in parallel with each other:
Task T013: Implement prepare() -- computes smoothCoeff_, envelopeAttackCoeff_
Task T014: Implement reset() -- zeros state arrays

# After T015 (computeModeCoefficients) is done, wire independently:
Task T022: Parameter change handler in processor_params.cpp
Task T016-T021: Core algorithm methods in modal_resonator_bank.h
```

---

## Implementation Strategy

### MVP (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T004)
2. Complete Phase 2: Foundational (T005-T009) -- CRITICAL, blocks everything
3. Complete Phase 3: User Story 1 (T010-T027) -- core resonator DSP + voice wiring
4. **STOP and VALIDATE**: Sweep Physical Model Mix from 0% to 100% and verify the fundamental sonic transformation is working
5. Release as MVP if ready

### Incremental Delivery

1. Complete Setup + Foundational -> Foundation ready
2. Add US1 -> Test independently -> MVP (core resonance works)
3. Add US2 -> Verify material sculpting (decay/brightness damping behavior proven)
4. Add US3 -> Verify inharmonic warping (stretch/scatter)
5. Add US4 -> Verify backwards compatibility (state save/load, controller)
6. Add US5 -> Verify polyphony and CPU budget
7. Add SIMD -> Verify performance headroom
8. Pluginval -> Verify plugin-level correctness

### Notes

- Tests are REQUIRED for this spec (test-first methodology)
- [P] tasks can run in parallel (different files, no incomplete task dependencies)
- [USn] labels map tasks to user stories for traceability
- The scalar implementation (Phase 3) must be complete and tested before the SIMD layer (Phase 8) is added
- Do NOT add SIMD until SC-002a/SC-002b pass in scalar form -- SIMD should be the optimization layer, not the fix for a broken scalar implementation
- The `kTransientEmphasisGain = 4.0f` constant MUST have a code comment noting it may be promoted to a user-facing parameter in a future phase (FR-022)
- The `kMaxB3 = 4.0e-5f` constant is provisional and must be noted for retuning after listening tests (spec clarification)
- Denormalization: FTZ/DAZ is assumed enabled by the audio thread setup; `flushSilentModes()` provides the explicit per-block guard on top of hardware FTZ
- NEVER claim completion if ANY requirement is not met -- document gaps honestly instead
