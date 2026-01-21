# Feature Specification: State Variable Filter (SVF)

**Feature Branch**: `080-svf`
**Created**: 2026-01-21
**Status**: Draft
**Input**: User description: "State Variable Filter (SVF) based on Cytomic TPT/Trapezoidal topology - Phase 2 Sprint 2 item 4 from FLT-ROADMAP.md"

## Overview

This specification defines a State Variable Filter (SVF) implementation using the Cytomic TPT (Topology-Preserving Transform) / Trapezoidal integration topology. The SVF is a versatile 2-pole filter that provides simultaneous lowpass, highpass, bandpass, and notch outputs from a single computation, with excellent modulation stability.

**Location**: `dsp/include/krate/dsp/primitives/svf.h`
**Layer**: 1 (Primitive)
**Test File**: `dsp/tests/primitives/svf_test.cpp`
**Namespace**: `Krate::DSP`

### Motivation

The current codebase has `Biquad` for general filtering, but it has limitations:
1. **Modulation instability**: Biquad coefficients can produce clicks/artifacts when rapidly modulated
2. **Single output**: Each biquad produces only one filter type at a time
3. **Interdependent parameters**: Changing cutoff affects resonance behavior

The TPT SVF solves these problems:
1. **Designed for modulation**: Trapezoidal integration provides excellent audio-rate modulation stability
2. **Simultaneous outputs**: LP/HP/BP/Notch available from single computation cycle
3. **Independent parameters**: Cutoff and Q are truly orthogonal controls
4. **Better low-frequency precision**: Superior numerical behavior at low frequencies

This component will be used by `envelope_filter.h` (auto-wah) in Phase 9.

### References

- [Cytomic Technical Papers](https://cytomic.com/technical-papers/)
- [SvfLinearTrapOptimised2.pdf](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf)
- [Vadim Zavalishin - The Art of VA Filter Design](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf)
- [GitHub VA SVF Implementation](https://github.com/JordanTHarris/VAStateVariableFilter)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - DSP Developer Uses SVF for Synth-Style Filtering (Priority: P1)

A DSP developer building a synthesizer or filter effect needs a resonant filter that can be smoothly modulated without clicks or artifacts. They configure an SVF with cutoff and resonance parameters and sweep the cutoff using an LFO or envelope without audible discontinuities.

**Why this priority**: Audio-rate modulation stability is the primary advantage of the TPT SVF over biquad filters. This is the most critical use case.

**Independent Test**: Can be fully tested by configuring the filter, modulating cutoff at audio rate, and verifying no clicks or discontinuities occur in the output.

**Acceptance Scenarios**:

1. **Given** an SVF in lowpass mode at 44.1 kHz, **When** cutoff is swept from 100 Hz to 10 kHz over 100 samples (audio-rate modulation), **Then** the output contains no clicks or discontinuities (peak-to-peak change between adjacent samples < 0.5).

2. **Given** an SVF with Q = 10 (high resonance), **When** cutoff is modulated sinusoidally at 20 Hz with depth 2 octaves, **Then** the filter remains stable (no runaway oscillation or NaN output).

3. **Given** an SVF in lowpass mode prepared at 44.1 kHz with 1000 Hz cutoff and Q = 0.7071, **When** processing a 100 Hz sine wave, **Then** the output amplitude is within 0.5 dB of input (low frequency passes).

4. **Given** an SVF in lowpass mode prepared at 44.1 kHz with 1000 Hz cutoff, **When** processing a 10000 Hz sine wave, **Then** the output is attenuated by at least 22 dB (12 dB/octave at 1 decade, ~24 dB for 2-pole).

---

### User Story 2 - DSP Developer Uses Multi-Output Processing (Priority: P1)

A DSP developer needs simultaneous lowpass, highpass, bandpass, and notch outputs from a single filter computation for a multi-band effect or crossover design. They use the processMulti() method to obtain all four outputs efficiently.

**Why this priority**: Simultaneous multi-output is a key differentiator of SVF over biquad and enables efficient multi-band processing.

**Independent Test**: Can be tested by processing a test signal and verifying all four outputs have correct frequency responses simultaneously.

**Acceptance Scenarios**:

1. **Given** an SVF prepared at 44.1 kHz with 1000 Hz cutoff, **When** calling processMulti() with a mixed-frequency signal, **Then** all four outputs (low, high, band, notch) are returned in a single call.

2. **Given** processMulti() output at 1000 Hz cutoff, **When** processing a 100 Hz sine wave, **Then** `low` output is near unity gain, `high` output is attenuated (~-24 dB), `band` output is attenuated, and `notch` output is near unity.

3. **Given** processMulti() output at 1000 Hz cutoff, **When** processing a 1000 Hz sine wave (at cutoff), **Then** `band` output is near unity gain (bandpass peaks at cutoff), and `notch` output is at minimum.

---

### User Story 3 - DSP Developer Configures Various Filter Modes (Priority: P2)

A DSP developer needs different filter responses (lowpass, highpass, bandpass, notch, allpass, peak, low shelf, high shelf) for various applications. They set the filter mode and use the standard process() method.

**Why this priority**: While multi-output is efficient, single-mode operation with mode selection is the standard filter API pattern and enables peak/shelf modes not available from processMulti().

**Independent Test**: Can be tested by configuring each mode and verifying the expected frequency response.

**Acceptance Scenarios**:

1. **Given** an SVF in highpass mode at 1000 Hz, **When** processing a 100 Hz sine wave, **Then** output is attenuated by at least 18 dB.

2. **Given** an SVF in bandpass mode at 1000 Hz with Q = 5, **When** processing a 1000 Hz sine wave, **Then** output is near unity gain (within 1 dB).

3. **Given** an SVF in notch mode at 1000 Hz, **When** processing a 1000 Hz sine wave, **Then** output is attenuated by at least 20 dB.

4. **Given** an SVF in allpass mode at 1000 Hz, **When** processing any frequency, **Then** output amplitude equals input amplitude within 0.1 dB (flat magnitude response).

5. **Given** an SVF in peak mode at 1000 Hz with +6 dB gain, **When** processing a 1000 Hz sine wave, **Then** output is boosted by approximately 6 dB (+/- 1 dB).

6. **Given** an SVF in low shelf mode at 1000 Hz with +6 dB gain, **When** processing a 100 Hz sine wave, **Then** output is boosted by approximately 6 dB (+/- 1 dB).

7. **Given** an SVF in high shelf mode at 1000 Hz with +6 dB gain, **When** processing a 10000 Hz sine wave, **Then** output is boosted by approximately 6 dB (+/- 1 dB).

---

### User Story 4 - DSP Developer Uses Block Processing (Priority: P2)

A DSP developer needs efficient processing of audio buffers rather than sample-by-sample for better cache performance. They use processBlock() to process entire buffers.

**Why this priority**: Block processing is important for performance but less critical than the filter functionality itself.

**Independent Test**: Can be tested by comparing block output with equivalent sample-by-sample processing.

**Acceptance Scenarios**:

1. **Given** an SVF configured for lowpass at 1000 Hz, **When** processBlock() is called on a buffer, **Then** the output is bit-identical to calling process() on each sample sequentially.

2. **Given** processBlock() with a 1024-sample buffer, **When** processing is complete, **Then** no memory allocation occurred during processing.

---

### Edge Cases

- What happens when sample rate is 0 or negative in prepare()? Must clamp silently to valid minimum (e.g., 1000 Hz).
- What happens when cutoff frequency is 0 or negative? Must clamp silently to valid minimum (e.g., 1 Hz).
- What happens when cutoff frequency exceeds Nyquist? Must clamp silently to below Nyquist (e.g., sampleRate * 0.495).
- What happens when Q is 0 or negative? Must clamp silently to valid minimum (e.g., 0.1).
- What happens when Q exceeds safe maximum? Must clamp silently to safe maximum (e.g., 30) to prevent instability.
- What happens when process() is called before prepare()? Must return input unchanged (safe default).
- What happens with denormal values in filter state? Must flush denormals after every process() call to prevent CPU spikes.
- What happens when NaN or Infinity input is processed? Must return 0.0f and reset internal state.
- What happens when gain is extremely large (e.g., +48 dB) in peak/shelf modes? Must clamp silently to safe maximum.

## Clarifications

### Session 2026-01-21

- Q: When setGain() is called on peak/shelf modes, should coefficient A be recalculated immediately (instant parameter change), deferred until next process() call (lazy evaluation), or require explicit prepare() re-call? → A: Recalculate A coefficient immediately on setGain() call
- Q: When setCutoff() or setResonance() is called, should g/k coefficients and derived coefficients (a1, a2, a3) be recalculated immediately or deferred until next process() call? → A: Recalculate coefficients immediately on setCutoff/setResonance call (no smoothing)
- Q: When processMulti() processes multiple channels, should the filter maintain independent state per channel or share coefficients with separate state? → A: Share coefficients, independent state per channel (efficient, consistent response)
- Q: When parameters like cutoff, Q, or gain are set to out-of-range values (e.g., negative cutoff, Q=0, gain > 48dB), how should the filter respond? → A: Clamp silently to safe range (real-time safe, predictable)
- Q: For state variable flushing (ic1eq_, ic2eq_), when should denormal flushing occur? → A: Flush state after every process() call (most robust, prevents CPU spikes)

## Requirements *(mandatory)*

### Functional Requirements

#### svf.h (Layer 1)

##### Class Structure and Types

- **FR-001**: svf.h MUST define an enum class `SVFMode` with values: Lowpass, Highpass, Bandpass, Notch, Allpass, Peak, LowShelf, HighShelf.
- **FR-002**: svf.h MUST define a struct `SVFOutputs` with members: `float low, high, band, notch` for simultaneous multi-output processing.
- **FR-003**: svf.h MUST define class `SVF` implementing a TPT/Trapezoidal integrated state variable filter.

##### Lifecycle and Configuration

- **FR-004**: SVF MUST provide `void prepare(double sampleRate)` to initialize the filter for a given sample rate. May be called multiple times with different sample rates (e.g., when host changes sample rate).
- **FR-005**: SVF MUST provide `void setMode(SVFMode mode)` to select the filter output mode for process().
- **FR-006**: SVF MUST provide `void setCutoff(float hz)` to set the cutoff/center frequency, clamping silently to valid range [1 Hz, sampleRate * 0.495] and recalculating coefficients g, a1, a2, a3 immediately upon call. The 0.495 Nyquist limit prevents tan(pi * fc / fs) from approaching infinity near Nyquist.
- **FR-007**: SVF MUST provide `void setResonance(float q)` to set the Q factor (resonance), clamping silently to valid range [0.1, 30.0] and recalculating coefficients k, a1, a2, a3 immediately upon call.
- **FR-008**: SVF MUST provide `void setGain(float dB)` to set the gain for peak and shelf modes (ignored for other modes), clamping silently to valid range [-24 dB, +24 dB] and recalculating coefficient A immediately upon call.
- **FR-009**: SVF MUST provide `void reset() noexcept` to clear all internal state.

##### Processing Methods

- **FR-010**: SVF MUST provide `[[nodiscard]] float process(float input) noexcept` that returns the output for the currently selected mode.
- **FR-011**: SVF MUST provide `void processBlock(float* buffer, size_t numSamples) noexcept` for in-place block processing.
- **FR-012**: SVF MUST provide `[[nodiscard]] SVFOutputs processMulti(float input) noexcept` returning simultaneous LP/HP/BP/Notch outputs from a single computation.

##### Implementation

- **FR-013**: SVF MUST implement the Cytomic TPT/Trapezoidal topology with coefficients: `g = tan(pi * cutoff / sampleRate)`, `k = 1/Q`.
- **FR-014**: SVF MUST compute derived coefficients: `a1 = 1 / (1 + g * (g + k))`, `a2 = g * a1`, `a3 = g * a2`.
- **FR-015**: SVF MUST maintain two integrator state variables (`ic1eq_`, `ic2eq_`) for trapezoidal integration.
- **FR-016**: SVF MUST compute per-sample outputs as:
    - `v3 = input - ic2eq`
    - `v1 = a1 * ic1eq + a2 * v3`
    - `v2 = ic2eq + a2 * ic1eq + a3 * v3`
    - `ic1eq = 2 * v1 - ic1eq`
    - `ic2eq = 2 * v2 - ic2eq`
    - `low = v2`, `band = v1`, `high = v3 - k * v1 - v2`

- **FR-017**: SVF MUST implement mode mixing using coefficients (m0, m1, m2) where output = m0*high + m1*band + m2*low:

    | Mode | m0 | m1 | m2 |
    |------|----|----|-----|
    | Lowpass | 0 | 0 | 1 |
    | Highpass | 1 | 0 | 0 |
    | Bandpass | 0 | 1 | 0 |
    | Notch | 1 | 0 | 1 |
    | Allpass | 1 | -k | 1 |
    | Peak | 1 | 0 | -1 |
    | LowShelf | 1 | k*(A-1) | A^2 |
    | HighShelf | A^2 | k*(A-1) | 1 |

    Where A = 10^(gainDb/40) for shelf/peak modes.

##### Real-Time Safety

- **FR-018**: All processing methods MUST be declared `noexcept`.
- **FR-019**: All processing methods MUST flush denormals using `detail::flushDenormal()` on state variables (ic1eq_, ic2eq_) after every process() call to prevent CPU performance degradation.
- **FR-020**: All processing methods MUST NOT allocate memory, throw exceptions, or perform I/O.
- **FR-021**: If process() or processMulti() is called before prepare(), it MUST return input unchanged (for process()) or zeros (for processMulti()).
- **FR-022**: If NaN or Infinity input is detected, methods MUST return 0.0f and reset internal state. Implementation MUST use `detail::isNaN()` and `detail::isInf()` from db_utils.h (not std::isnan/std::isinf) for compatibility with -ffast-math compiler flag.

##### Dependencies and Code Quality

- **FR-023**: svf.h MUST only depend on Layer 0 components (math_constants.h, db_utils.h) and standard library.
- **FR-024**: svf.h MUST be a header-only implementation.
- **FR-025**: All components MUST be in namespace `Krate::DSP`.
- **FR-026**: All components MUST include Doxygen documentation for classes, enums, and public methods.
- **FR-027**: All components MUST follow naming conventions (trailing underscore for members, PascalCase for classes).

### Key Entities

- **SVFMode**: Enum selecting which filter response type to output from process().
- **SVFOutputs**: Struct containing all four simultaneous filter outputs (low, high, band, notch).
- **SVF**: The main State Variable Filter class implementing TPT/Trapezoidal topology.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: SVF in lowpass mode at 1000 Hz cutoff attenuates a 10000 Hz sine wave by at least 22 dB (12 dB/oct slope, 2 poles).
- **SC-002**: SVF in lowpass mode at 1000 Hz cutoff passes a 100 Hz sine wave with less than 0.5 dB attenuation.
- **SC-003**: SVF in highpass mode at 100 Hz cutoff attenuates a 10 Hz signal by at least 22 dB.
- **SC-004**: SVF in highpass mode at 100 Hz cutoff passes a 1000 Hz sine wave with less than 0.5 dB attenuation.
- **SC-005**: SVF in bandpass mode at 1000 Hz with Q = 5 has peak gain within 1 dB of unity at cutoff frequency.
- **SC-006**: SVF in notch mode at 1000 Hz attenuates a 1000 Hz sine wave by at least 20 dB.
- **SC-007**: SVF in allpass mode has flat magnitude response (within 0.1 dB) across the audio spectrum (20 Hz to 20 kHz).
- **SC-008**: SVF in peak mode at 1000 Hz with +6 dB gain boosts 1000 Hz by 6 dB (+/- 1 dB).
- **SC-009**: SVF in low shelf mode at 1000 Hz with +6 dB gain boosts 100 Hz by 6 dB (+/- 1 dB).
- **SC-010**: SVF in high shelf mode at 1000 Hz with +6 dB gain boosts 10000 Hz by 6 dB (+/- 1 dB).
- **SC-011**: Audio-rate cutoff modulation (sweeping 100 Hz to 10 kHz in 100 samples) produces no clicks (max absolute sample-to-sample amplitude change < 0.5 when processing unit amplitude input signal).
- **SC-012**: processBlock() produces bit-identical output to equivalent process() calls.
- **SC-013**: SVF handles 1 million samples without producing unexpected NaN or Infinity outputs from valid [-1, 1] inputs.
- **SC-014**: Unit test coverage reaches 100% of all public methods including edge cases.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Target platforms support IEEE 754 floating-point arithmetic.
- C++20 is available for language features (constexpr).
- Typical audio sample rates are 44100 Hz to 192000 Hz.
- Users understand that Q values above ~20 can cause ringing and near-self-oscillation behavior.
- Shelf/peak gain values are typically in the range -24 dB to +24 dB.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `kPi`, `kTwoPi` | `core/math_constants.h` | MUST REUSE for coefficient calculations |
| `detail::flushDenormal()` | `core/db_utils.h` | MUST REUSE for denormal flushing |
| `detail::isNaN()`, `detail::isInf()` | `core/db_utils.h` | MUST REUSE for input validation |
| `dbToGain()` | `core/db_utils.h` | MAY REUSE for gain calculations (A = 10^(dB/40)) |
| `Biquad` | `primitives/biquad.h` | Reference pattern for filter API design |
| `OnePoleLP`, `OnePoleHP` | `primitives/one_pole.h` | Reference pattern for prepare/process API |
| `FilterType` enum | `primitives/biquad.h` | Note: SVF uses its own `SVFMode` to avoid confusion |
| `kMinFilterFrequency`, `kMaxFrequencyRatio` | `primitives/biquad.h` | MAY REUSE for parameter clamping |
| `FilterDesign::prewarpFrequency()` | `core/filter_design.h` | Reference - SVF uses tan() directly |

**Initial codebase search for key terms:**

```bash
grep -r "SVF\|StateVariable" dsp/
grep -r "class SVF" dsp/
grep -r "SVFMode\|SVFOutputs" dsp/
```

**Search Results Summary**: No existing SVF, StateVariable, SVFMode, or SVFOutputs implementations found. Safe to proceed with new implementation.

### Forward Reusability Consideration

**Sibling features at same layer** (Layer 1):
- `ladder_filter.h` (Phase 5) - May reference SVF pattern for TPT-style coefficient calculation
- `allpass_1pole.h` (Phase 4) - Similar prepare/process API pattern

**Features that will use this component**:
- `envelope_filter.h` (Phase 9) - Will compose SVF for auto-wah filter sweeps
- Future synth filters - SVF is preferred for LFO/envelope modulation due to stability

**Potential shared components**:
- The TPT coefficient calculation pattern (g = tan(pi * fc / fs)) could be extracted to filter_design.h if other TPT filters are added, but for now it's simple enough to inline.

## Out of Scope

- Oversampling (not needed for SVF - already stable at 1x)
- Multi-channel/stereo variants (users create separate instances per channel with shared coefficients but independent state variables)
- SIMD implementations (can be added later as optimization)
- Double-precision overloads (can be added later if needed)
- SmoothedSVF variant (coefficient smoothing - can be added later if needed)
- Band shelf mode (not in initial roadmap)

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-024 | | |
| FR-025 | | |
| FR-026 | | |
| FR-027 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |
| SC-010 | | |
| SC-011 | | |
| SC-012 | | |
| SC-013 | | |
| SC-014 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
