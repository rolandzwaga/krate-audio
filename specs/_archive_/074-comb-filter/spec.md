# Feature Specification: Comb Filters (FeedforwardComb, FeedbackComb, SchroederAllpass)

**Feature Branch**: `074-comb-filter`
**Created**: 2026-01-21
**Status**: Draft
**Input**: User description: "Comb filter primitives (FeedforwardComb, FeedbackComb, SchroederAllpass) for Layer 1"

## Clarifications

### Session 2026-01-21

- Q: Damping coefficient range & interpretation for FeedbackComb one-pole lowpass filter? → A: Range [0.0, 1.0] where 0.0 = no damping (bright/pass-through), 1.0 = maximum damping (dark/heavy lowpass)
- Q: Where should denormal flushing be applied in filters with feedback? → A: Only feedback path state variables (damping lowpass state for FeedbackComb, feedback state for SchroederAllpass), not the DelayLine buffer (which has its own flushing responsibility)
- Q: Block processing bit-identical requirements? → A: processBlock() must be bit-identical for testing/verification purposes, but optimized SIMD implementations that differ at LSB are acceptable if faster

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Feedforward Comb Filter for Flanger/Chorus Effects (Priority: P1)

A DSP developer building a flanger or chorus effect needs a feedforward comb filter (FIR) that creates notches in the frequency spectrum. The filter combines the input signal with a delayed copy, and when the delay time is modulated by an LFO, it produces the characteristic "jet plane" flanging sound or lush chorus textures.

**Why this priority**: Feedforward comb is the simplest and most commonly used comb filter type. It's the foundation for flanger, chorus, and doubling effects. It's also the safest (no feedback = no instability risk).

**Independent Test**: Can be fully tested by processing a white noise signal through the filter and verifying the expected notch pattern in the frequency response. Delivers immediate value for building modulation effects.

**Acceptance Scenarios**:

1. **Given** a FeedforwardComb with delay D samples and gain g=0.5, **When** processing an impulse, **Then** the output shows impulse at n=0 with amplitude 1.0 and echo at n=D with amplitude 0.5
2. **Given** a FeedforwardComb with gain g=1.0, **When** analyzing the frequency response, **Then** notches appear at frequencies f = k/(2D) where k is odd (1, 3, 5, ...)
3. **Given** a FeedforwardComb with delay modulated by LFO, **When** processing audio, **Then** the notch frequencies sweep smoothly without artifacts

---

### User Story 2 - Feedback Comb Filter for Karplus-Strong and Reverb (Priority: P2)

A DSP developer building a Karplus-Strong plucked string synthesizer or reverb effect needs a feedback comb filter (IIR) that creates resonant peaks in the frequency spectrum. The filter feeds its output back into the delay line, creating sustained or decaying resonances at the comb frequencies.

**Why this priority**: Feedback comb is essential for physical modeling synthesis (plucked strings) and classic reverb algorithms (comb bank reverbs). It requires careful gain limiting to prevent instability.

**Independent Test**: Can be fully tested by exciting the filter with an impulse and verifying exponential decay at the expected rate based on feedback gain. Delivers value for physical modeling and reverb.

**Acceptance Scenarios**:

1. **Given** a FeedbackComb with delay D samples and feedback g=0.5, **When** processing an impulse, **Then** the output shows decaying echoes at n=D, 2D, 3D, ... with amplitudes 0.5, 0.25, 0.125, ...
2. **Given** a FeedbackComb with feedback g=0.99 and damping enabled, **When** analyzing the frequency response, **Then** peaks appear at frequencies f = k/D where k is integer, with reduced high-frequency content due to damping
3. **Given** a FeedbackComb with feedback g approaching 1.0, **When** processing continuously, **Then** the filter remains stable without runaway oscillation due to internal gain limiting

---

### User Story 3 - Schroeder Allpass for Reverb Diffusion (Priority: P3)

A DSP developer building a reverb diffusion network needs a Schroeder allpass filter that provides flat magnitude response while dispersing the phase of the input signal. This spreads transients in time, creating the characteristic "smeared" quality of reverberant sound without altering the tonal balance.

**Why this priority**: Schroeder allpass is a specialized component for reverb algorithms. While essential for high-quality reverb, it has a narrower use case than the feedforward and feedback comb filters.

**Independent Test**: Can be fully tested by verifying unity magnitude response across all frequencies and demonstrating impulse spreading behavior. Delivers value for reverb algorithms.

**Acceptance Scenarios**:

1. **Given** a SchroederAllpass with any delay and gain setting, **When** analyzing the frequency response, **Then** the magnitude is unity (1.0) at all frequencies within 0.01 dB tolerance
2. **Given** a SchroederAllpass with delay D and gain g=0.7, **When** processing an impulse, **Then** the output shows a decaying impulse train spread over time
3. **Given** multiple SchroederAllpass filters in series with different delay times, **When** processing an impulse, **Then** the result is a dense, smeared response suitable for reverb

---

### User Story 4 - Variable Delay for Real-Time Modulation (Priority: P4)

A DSP developer needs all three comb filter types to support variable (modulated) delay times for real-time LFO control without clicks or discontinuities, enabling smooth parameter automation and modulation.

**Why this priority**: Variable delay is an enhancement that enables modulation effects. The basic fixed-delay functionality is more fundamental.

**Independent Test**: Can be fully tested by sweeping delay time during processing and verifying no clicks, pops, or discontinuities in the output waveform.

**Acceptance Scenarios**:

1. **Given** any comb filter type processing continuous audio, **When** delay time is smoothly swept between two values, **Then** the output contains no audible clicks or discontinuities
2. **Given** any comb filter type, **When** delay time is changed abruptly, **Then** the output transitions smoothly using the delay line's interpolation

---

### Edge Cases

- What happens when delay is set to 0 samples? Clamp to minimum 1 sample to ensure proper comb behavior.
- What happens when delay exceeds maximum? Clamp to maximum configured delay time.
- What happens when feedback gain exceeds 1.0? Clamp internally to 0.9999f for stability in FeedbackComb.
- What happens when feedforward gain exceeds 1.0? Allow values up to 1.0 (no amplification beyond unity) for FeedforwardComb.
- What happens with NaN or infinity input? Reset state and return 0.0f, consistent with other DSP primitives.
- What happens with denormal values in state? Flush feedback path state variables to zero per-sample (damping lowpass state for FeedbackComb, feedback state for SchroederAllpass) to prevent CPU spikes. The DelayLine buffer manages its own denormal flushing separately.
- What happens when filter is used without calling prepare()? Return input unchanged (bypass behavior).
- What happens with very short delays (1-10 samples)? Should work correctly for high-frequency resonances.
- What happens with very long delays (>1 second)? Should work correctly within configured maximum.

## Requirements *(mandatory)*

### Functional Requirements

**FeedforwardComb (FIR Comb Filter):**
- **FR-001**: FeedforwardComb MUST implement the difference equation: `y[n] = x[n] + g * x[n-D]` where g is the feedforward gain and D is the delay in samples
- **FR-002**: FeedforwardComb MUST create notches at frequencies `f = (2k-1) / (2D*T)` where k=1,2,3,... and T is the sample period
- **FR-003**: FeedforwardComb MUST support feedforward gain in range [0.0, 1.0], clamped at boundaries
- **FR-004**: FeedforwardComb MUST use the existing DelayLine class for the delay buffer

**FeedbackComb (IIR Comb Filter):**
- **FR-005**: FeedbackComb MUST implement the difference equation: `y[n] = x[n] + g * y[n-D]` where g is the feedback gain and D is the delay in samples
- **FR-006**: FeedbackComb MUST create peaks (resonances) at frequencies `f = k / (D*T)` where k=0,1,2,... and T is the sample period
- **FR-007**: FeedbackComb MUST support feedback gain in range [-0.9999f, 0.9999f], clamped at boundaries to ensure stability
- **FR-008**: FeedbackComb MUST provide optional damping via a configurable lowpass filter in the feedback path
- **FR-009**: FeedbackComb MUST use the existing DelayLine class for the delay buffer
- **FR-010**: FeedbackComb damping MUST be implemented as a one-pole lowpass filter with configurable coefficient `damping_` in range [0.0, 1.0] where 0.0 = no damping (bright/pass-through) and 1.0 = maximum damping (dark/heavy lowpass filtering)

**SchroederAllpass:**
- **FR-011**: SchroederAllpass MUST implement the difference equation: `y[n] = -g*x[n] + x[n-D] + g*y[n-D]` where g is the allpass coefficient and D is the delay in samples
- **FR-012**: SchroederAllpass MUST maintain unity magnitude response (1.0) at all frequencies within 0.01 dB tolerance
- **FR-013**: SchroederAllpass MUST support coefficient g in range [-0.9999f, 0.9999f], clamped at boundaries
- **FR-014**: SchroederAllpass MUST use the existing DelayLine class for the delay buffer

**Common Requirements (All Three Types):**
- **FR-015**: All filters MUST provide a `prepare(double sampleRate, float maxDelaySeconds)` method to initialize for a given sample rate and maximum delay
- **FR-016**: All filters MUST provide a `reset()` method to clear internal state to zero
- **FR-017**: All filters MUST provide a `process(float input)` method for sample-by-sample processing with current delay setting
- **FR-018**: All filters MUST provide a `processBlock(float* buffer, size_t numSamples)` method for efficient block processing
- **FR-019**: All filters MUST support delay specification in both samples (`setDelaySamples(float)`) and milliseconds (`setDelayMs(float)`)
- **FR-020**: All filters MUST support variable (fractional) delay times for modulation using DelayLine's linear interpolation
- **FR-021**: All filters MUST handle NaN/infinity input by resetting state and returning 0.0f
- **FR-022**: FeedbackComb and SchroederAllpass MUST flush denormal values per-sample in feedback path state variables only (damping lowpass state for FeedbackComb, feedback state for SchroederAllpass), not the DelayLine buffer which manages its own denormal flushing, to prevent CPU spikes

**Real-Time Safety (Constitution Principle II):**
- **FR-023**: All processing methods MUST be marked `noexcept`
- **FR-024**: All processing methods MUST NOT allocate memory
- **FR-025**: All processing methods MUST NOT perform I/O or locking operations

**Layer Architecture (Constitution Principle IX):**
- **FR-026**: All filters MUST be located in Layer 1 (primitives) at `dsp/include/krate/dsp/primitives/comb_filter.h`
- **FR-027**: All filters MUST only depend on Layer 0 components and Layer 1 DelayLine primitive

### Key Entities

- **FeedforwardComb**: FIR comb filter creating spectral notches
  - Properties: feedforward gain (g), delay in samples (D), internal DelayLine
  - Equation: `y[n] = x[n] + g * x[n-D]`
  - Use cases: Flanger, chorus, phaser (with modulation)

- **FeedbackComb**: IIR comb filter creating spectral peaks/resonances
  - Properties: feedback gain (g), delay in samples (D), optional damping coefficient, internal DelayLine
  - Equation: `y[n] = x[n] + g * y[n-D]` (or with damping: `y[n] = x[n] + g * LP(y[n-D])`)
  - Use cases: Karplus-Strong synthesis, reverb comb banks

- **SchroederAllpass**: Allpass filter with flat magnitude response
  - Properties: allpass coefficient (g), delay in samples (D), internal DelayLine
  - Equation: `y[n] = -g*x[n] + x[n-D] + g*y[n-D]`
  - Use cases: Reverb diffusion networks, impulse spreading

- **Damping Filter**: One-pole lowpass for FeedbackComb damping
  - Properties: damping coefficient `damping_` in range [0.0, 1.0], state variable
  - Equation: `LP(x) = (1-damping_)*x + damping_*LP_prev`
  - Behavior: 0.0 = no damping (bright/pass-through), 1.0 = maximum damping (dark/heavy lowpass)
  - Use case: High-frequency attenuation in feedback path for natural decay

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: FeedforwardComb notch depth is at least -40 dB when g=1.0 at the theoretical notch frequencies
- **SC-002**: FeedbackComb peak height is at least +20 dB when g=0.99 at the theoretical peak frequencies
- **SC-003**: SchroederAllpass magnitude response deviation from unity is less than 0.01 dB across the full frequency range (20 Hz - 20 kHz)
- **SC-004**: All filters process audio with less than 50 nanoseconds per sample on standard desktop hardware (Release build, -O3, cache-warm measurement)
- **SC-005**: Memory footprint is determined by the configured maximum delay time (DelayLine buffer) plus less than 64 bytes overhead per filter instance
- **SC-006**: Block processing produces bit-identical output compared to sample-by-sample processing for testing/verification purposes; optimized SIMD implementations that differ at the LSB are acceptable if they provide measurable performance improvement
- **SC-007**: All filters correctly handle edge cases (NaN, infinity, denormals) without crashing or producing invalid output
- **SC-008**: Variable delay modulation produces no audible clicks when delay time changes smoothly at rates up to 10 Hz

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Sample rates in range 8000 Hz to 192000 Hz are supported
- Single-precision (float) is sufficient for audio quality requirements
- DelayLine class provides linear interpolation suitable for modulated delays
- Maximum delay time will be configured at prepare() time and not changed during processing. Re-calling prepare() with different sample rate or max delay is allowed and will clear state (typical use case: host sample rate change)
- Damping coefficient for FeedbackComb uses simple one-pole lowpass in range [0.0, 1.0] where 0.0 = no damping (bright), 1.0 = maximum damping (dark); this is a direct coefficient, not a cutoff frequency
- Feedforward gain is limited to [0, 1] to prevent output exceeding input level significantly
- Feedback gain is limited to [-0.9999f, 0.9999f] to guarantee stability

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component                     | Location                                        | Relevance                                                                      |
| ----------------------------- | ----------------------------------------------- | ------------------------------------------------------------------------------ |
| DelayLine                     | `dsp/include/krate/dsp/primitives/delay_line.h` | Should reuse - provides delay buffer and fractional interpolation              |
| DCBlocker                     | `dsp/include/krate/dsp/primitives/dc_blocker.h` | Reference - may use similar one-pole structure for damping                     |
| AllpassStage (diffusion)      | `dsp/include/krate/dsp/processors/diffusion_network.h` | Reference - Schroeder allpass implementation (Layer 2, different formulation)  |
| detail::flushDenormal()       | `dsp/include/krate/dsp/core/db_utils.h`         | Should reuse - denormal flushing                                               |
| detail::isNaN(), detail::isInf() | `dsp/include/krate/dsp/core/db_utils.h`      | Should reuse - NaN/infinity detection                                          |
| kPi, kTwoPi                   | `dsp/include/krate/dsp/core/math_constants.h`   | Should reuse - math constants                                                  |

**Initial codebase search for key terms:**

```bash
grep -r "FeedforwardComb" dsp/ plugins/
grep -r "FeedbackComb" dsp/ plugins/
grep -r "class.*Comb" dsp/ plugins/
grep -r "comb_filter" dsp/ plugins/
```

**Search Results Summary**:
- No existing `FeedforwardComb`, `FeedbackComb`, or `comb_filter.h` found - these are new components
- Found `AllpassStage` in `diffusion_network.h` - this is a Layer 2 Schroeder allpass using single-delay-line formulation; the new `SchroederAllpass` will be a Layer 1 primitive with the standard two-state formulation
- The existing `AllpassStage` uses `readAllpass()` interpolation; the new primitives will use `readLinear()` for modulation support

**Conclusion**: No ODR risk. The new comb filter classes serve distinct purposes:
- `FeedforwardComb` and `FeedbackComb` are entirely new
- `SchroederAllpass` in Layer 1 differs from `AllpassStage` in Layer 2: different formulation, different interpolation, reusable primitive vs. composed processor

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- Allpass1Pole (spec 073) - first-order allpass for phasers, different from Schroeder allpass
- Future phaser effects will use Allpass1Pole, not SchroederAllpass
- Future reverb implementations (Layer 4) will use FeedbackComb and SchroederAllpass

**Potential shared components** (preliminary, refined in plan.md):
- FeedbackComb can be used in Karplus-Strong synthesis for plucked strings
- SchroederAllpass can replace or complement the existing AllpassStage in diffusion networks
- The damping filter pattern (one-pole LP in feedback) may be extracted as a reusable utility

**References:**
- Stanford CCRMA Schroeder Allpass: https://ccrma.stanford.edu/~jos/pasp/Schroeder_Allpass_Sections.html
- Valhalla DSP - Reverb Diffusion: https://valhalladsp.com/2011/01/21/reverbs-diffusion-allpass-delays-and-metallic-artifacts/

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `comb_filter.h:176` implements `y[n] = x[n] + g * x[n-D]`; test: "Impulse response shows correct delay and gain" |
| FR-002 | MET | Test: "Notch frequencies at (2k-1)/(2DT)" verifies spectral notches at correct frequencies |
| FR-003 | MET | `comb_filter.h:119` clamps gain to [0.0, 1.0]; test: "Gain clamping" |
| FR-004 | MET | `comb_filter.h:196` uses `DelayLine delay_` member |
| FR-005 | MET | `comb_filter.h:353` implements with damped feedback; test: "Impulse response with decaying echoes" |
| FR-006 | MET | Test: "Peak frequencies at k/(DT)" verifies resonant peaks at correct frequencies |
| FR-007 | MET | `comb_filter.h:272` clamps to [-0.9999f, 0.9999f]; test: "Feedback clamping" |
| FR-008 | MET | `comb_filter.h:344-350` one-pole LP in feedback path; test: "Damping filter in feedback path" |
| FR-009 | MET | `comb_filter.h:379` uses `DelayLine delay_` member |
| FR-010 | MET | `comb_filter.h:285,346` damping [0,1] where 0=bright, 1=dark; test: "Damping coefficient range" |
| FR-011 | MET | `comb_filter.h:519` implements `-g*x[n] + x[n-D] + g*y[n-D]`; test: "Schroeder difference equation" |
| FR-012 | MET | Test: "Unity magnitude response within 0.01dB" across 20Hz-20kHz |
| FR-013 | MET | `comb_filter.h:452` clamps to [-0.9999f, 0.9999f]; test: "Coefficient clamping" |
| FR-014 | MET | `comb_filter.h:547` uses `DelayLine delay_` member |
| FR-015 | MET | All three classes have `prepare(double, float)` at lines 103, 255, 436 |
| FR-016 | MET | All three classes have `reset()` at lines 111, 263, 444 |
| FR-017 | MET | All three classes have `process(float)` at lines 160, 326, 493 |
| FR-018 | MET | All three classes have `processBlock()` at lines 185, 368, 536 |
| FR-019 | MET | All three have `setDelaySamples()` and `setDelayMs()` methods |
| FR-020 | MET | All use `delay_.readLinear()` for fractional delay interpolation |
| FR-021 | MET | NaN/Inf check at lines 162-165, 328-331, 495-498; tests: "NaN input handling" |
| FR-022 | MET | `flushDenormal()` at lines 350, 356, 525; tests: "Denormal flushing" |
| FR-023 | MET | All `process()` and `processBlock()` methods marked `noexcept` |
| FR-024 | MET | No `new`, `malloc`, or container operations in process methods |
| FR-025 | MET | No file I/O, mutexes, or blocking calls in process methods |
| FR-026 | MET | File located at `dsp/include/krate/dsp/primitives/comb_filter.h` |
| FR-027 | MET | Only includes `<krate/dsp/core/db_utils.h>` (L0) and `delay_line.h` (L1) |
| SC-001 | MET | Test: "Notch depth >= 40dB" with g=1.0 measures >60dB attenuation |
| SC-002 | MET | Test: "Peak height >= 20dB" with g=0.99 measures >25dB gain |
| SC-003 | MET | Test: "Unity magnitude within 0.01dB" verifies <0.01dB deviation |
| SC-004 | MET | Test: "Performance < 50ns/sample" - all filters pass benchmark |
| SC-005 | MET | Test: "Memory overhead < 64 bytes" - sizeof() < 64 for all (excl. DelayLine) |
| SC-006 | MET | Test: "Block vs sample-by-sample bit-identical" passes for all filters |
| SC-007 | MET | Tests: NaN, Inf, denormal handling verified with edge case tests |
| SC-008 | MET | Test: "Variable delay modulation smooth" verifies no clicks at 10Hz rate |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [x] All FR-xxx requirements verified against implementation
- [x] All SC-xxx success criteria measured and documented
- [x] No test thresholds relaxed from spec requirements
- [x] No placeholder values or TODO comments in new code
- [x] No features quietly removed from scope
- [x] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

**Summary**: All 27 functional requirements (FR-001 through FR-027) and all 8 success criteria (SC-001 through SC-008) are fully implemented and verified with 65 test cases (216,118 assertions). The implementation follows the spec exactly:

- FeedforwardComb, FeedbackComb, and SchroederAllpass all implement their specified difference equations
- All parameter ranges and clamping behaviors match spec
- All edge cases (NaN, Inf, denormals, unprepared state) handled correctly
- Performance meets <50ns/sample target on all filters
- Memory overhead <64 bytes per instance (excluding DelayLine buffer)
- Block processing is bit-identical to sample-by-sample
- Architecture documentation updated in layer-1-primitives.md

**Files Created/Modified**:
- `dsp/include/krate/dsp/primitives/comb_filter.h` (555 lines)
- `dsp/tests/unit/primitives/comb_filter_test.cpp` (65 test cases)
- `dsp/tests/CMakeLists.txt` (test registration)
- `specs/_architecture_/layer-1-primitives.md` (documentation)
