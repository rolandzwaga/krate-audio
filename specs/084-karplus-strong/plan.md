# Implementation Plan: Karplus-Strong String Synthesizer

**Branch**: `084-karplus-strong` | **Date**: 2026-01-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/084-karplus-strong/spec.md`

## Summary

Implement a Karplus-Strong plucked string synthesizer as a Layer 2 DSP processor. The algorithm uses a delay line with filtered feedback to simulate vibrating string behavior. Key technical choices from research:
- Allpass interpolation for fractional delay (best pitch accuracy within 1 cent)
- Two-pole lowpass (12dB/oct) for brightness control of excitation
- One-pole lowpass for damping in feedback loop
- Pick position via delay line tap during excitation fill
- Allpass filter for inharmonicity/stretch parameter

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: DelayLine, OnePoleLP, Allpass1Pole, DCBlocker2, NoiseGenerator, Biquad (for TwoPoleLP), Xorshift32
**Storage**: N/A (real-time DSP, no persistent storage)
**Testing**: Catch2 (via dsp_tests target)
**Target Platform**: Windows (MSVC 2019+), macOS (Clang/Xcode 13+), Linux (GCC 10+)
**Project Type**: DSP library component (Layer 2 processor)
**Performance Goals**: < 0.5% CPU per voice at 44.1kHz (SC-004)
**Constraints**: Real-time safe (noexcept, no allocations in process), stable for 10+ minutes continuous operation
**Scale/Scope**: Single DSP processor class with supporting utilities

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in process(), pluck(), bow(), excite() methods
- [x] All buffers pre-allocated in prepare()
- [x] noexcept on all audio processing methods
- [x] Denormal flushing in feedback loop

**Required Check - Principle III (Modern C++):**
- [x] RAII for resource management (DelayLine handles buffer)
- [x] constexpr where applicable
- [x] Value semantics, no raw new/delete

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 2 processor depends only on Layer 0/1
- [x] Uses existing primitives: DelayLine, OnePoleLP, Allpass1Pole, DCBlocker2
- [x] New TwoPoleLP will be Layer 1 primitive if created

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: KarplusStrong, TwoPoleLP (if needed)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| KarplusStrong | `grep -r "class KarplusStrong" dsp/ plugins/` | No | Create New |
| TwoPoleLP | `grep -r "class TwoPoleLP\|TwoPoleLowpass" dsp/ plugins/` | No | Create New (Layer 1) |

**Utility Functions to be created**: rt60ToFeedback (decay to feedback conversion)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| rt60ToFeedback | `grep -r "rt60ToFeedback\|decayToFeedback" dsp/` | No | - | Create in KarplusStrong |
| rt60ToQ | `grep -r "rt60ToQ" dsp/` | Yes | resonator_bank.h | Reuse pattern, not function |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | 1 | Main delay line with allpass interpolation |
| OnePoleLP | dsp/include/krate/dsp/primitives/one_pole.h | 1 | Damping filter in feedback loop |
| Allpass1Pole | dsp/include/krate/dsp/primitives/allpass_1pole.h | 1 | Dispersion for inharmonicity |
| DCBlocker2 | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | DC blocking in feedback path |
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | Real-time safe noise generation |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | Lowpass filter for brightness (12dB/oct) |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing in feedback |
| isNaN/isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Input validation |
| kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Coefficient calculations |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: KarplusStrong is a unique class name not found in codebase. TwoPoleLP (if needed) is also unique. All planned utilities are either class-specific or use existing Layer 0 functions.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | readAllpass | `[[nodiscard]] float readAllpass(float delaySamples) noexcept` | Yes |
| DelayLine | readLinear | `[[nodiscard]] float readLinear(float delaySamples) const noexcept` | Yes |
| DelayLine | maxDelaySamples | `[[nodiscard]] size_t maxDelaySamples() const noexcept` | Yes |
| OnePoleLP | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| OnePoleLP | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| OnePoleLP | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| OnePoleLP | reset | `void reset() noexcept` | Yes |
| Allpass1Pole | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| Allpass1Pole | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| Allpass1Pole | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Allpass1Pole | reset | `void reset() noexcept` | Yes |
| DCBlocker2 | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker2 | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker2 | reset | `void reset() noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| detail::flushDenormal | - | `inline float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | - | `inline bool isNaN(float x) noexcept` | Yes |
| detail::isInf | - | `inline bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/primitives/one_pole.h` - OnePoleLP class
- [x] `dsp/include/krate/dsp/primitives/allpass_1pole.h` - Allpass1Pole class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker2 class
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| DelayLine | readAllpass modifies state | Call once per sample, order matters |
| DelayLine | write before read | `delay_.write(sample)` then `delay_.readAllpass(delaySamples)` |
| OnePoleLP | Returns input if not prepared | Must call `prepare()` before first use |
| Allpass1Pole | NaN input resets filter | Handle NaN at KarplusStrong level |
| Biquad | FilterType::Lowpass needs Q | Use Q=0.7071 for Butterworth response |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

**No Layer 0 extraction needed.** The decay-to-feedback formula is Karplus-Strong specific and unlikely to be reused elsewhere. Similar calculation exists in ResonatorBank (rt60ToQ) but the formula is different for feedback coefficient vs filter Q.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateFeedback() | KS-specific formula: `feedback = 10^(-3 * delaySamples / (decayTime * sampleRate))` |
| calculateDampingCutoff() | KS-specific: cutoff relative to fundamental |

**Decision**: No Layer 0 extraction. All utility functions are KarplusStrong-specific.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from FLT-ROADMAP.md Phase 13):
- WaveguideResonator (Phase 13.3) - bidirectional waveguide for pipe/flute
- ModalResonator (Phase 13.4) - modal synthesis for bells/percussion
- ResonatorBank (Phase 13.1) - tuned resonator bank (already implemented)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| TwoPoleLP (if created) | HIGH | WaveguideResonator, other physical models | Create as Layer 1 primitive |
| Excitation patterns | MEDIUM | WaveguideResonator | Keep local, extract after 2nd use |
| Decay-to-feedback formula | LOW | KS-specific | Keep local |
| Pick position pattern | MEDIUM | WaveguideResonator | Keep local, review after Phase 13.3 |

### Detailed Analysis (for HIGH potential items)

**TwoPoleLP** provides:
- 12dB/octave lowpass filtering
- Smoother response than single-pole
- Standard biquad implementation with Butterworth Q

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| WaveguideResonator | YES | Brightness control for excitation |
| ModalResonator | MAYBE | May prefer SVF for modulation stability |
| FormantFilter | NO | Uses parallel bandpass, not lowpass |

**Recommendation**: Create TwoPoleLP as Layer 1 primitive now. It is a simple wrapper around Biquad configured as 12dB/oct lowpass, useful for multiple physical modeling features.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Create TwoPoleLP as Layer 1 | Needed for brightness control, reusable by sibling features |
| Keep excitation patterns local | Only one consumer so far, patterns may diverge |
| Use Biquad for TwoPoleLP | Existing infrastructure, well-tested |

### Review Trigger

After implementing **WaveguideResonator (Phase 13.3)**, review this section:
- [ ] Does WaveguideResonator need TwoPoleLP? -> Already extracted
- [ ] Does WaveguideResonator need excitation patterns? -> Extract if similar
- [ ] Does WaveguideResonator need pick position? -> Extract if identical

## Project Structure

### Documentation (this feature)

```text
specs/084-karplus-strong/
├── plan.md              # This file
├── spec.md              # Feature specification (exists)
└── checklists/          # Requirement checklists (exists)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── primitives/
│   │   └── two_pole_lp.h        # NEW: TwoPoleLP (Layer 1)
│   └── processors/
│       └── karplus_strong.h     # NEW: KarplusStrong (Layer 2)
└── tests/
    ├── primitives/
    │   └── two_pole_lp_tests.cpp  # NEW: TwoPoleLP tests
    └── processors/
        └── karplus_strong_tests.cpp  # NEW: KarplusStrong tests
```

**Structure Decision**: Standard DSP library layout with Layer 1 primitive (TwoPoleLP) and Layer 2 processor (KarplusStrong).

## Architecture Overview

### Signal Flow

```
                    +------------------+
                    | Excitation       |
                    | (noise/custom)   |
Pluck/Bow/Excite -> | [TwoPoleLP]      | -> (fills delay line)
                    | [Pick Position]  |
                    +------------------+
                            |
                            v
+----------------------------------------------------------------+
|                     FEEDBACK LOOP                               |
|                                                                 |
|  DelayLine -----> OnePoleLP -----> Allpass1Pole -----> DCBlocker2
|  (allpass        (damping)        (stretch/         (DC removal)
|   interp)                          dispersion)              |
|      ^                                                      |
|      |                                                      |
|      +---------- * feedback <-------------------------------+
|                                                                 |
+----------------------------------------------------------------+
                            |
                            v
                        Output
```

### Component Responsibilities

| Component | Responsibility |
|-----------|----------------|
| DelayLine | Main delay buffer, allpass interpolation for fractional delay |
| TwoPoleLP | 12dB/oct lowpass filter for excitation brightness |
| OnePoleLP | 6dB/oct lowpass in feedback loop for damping |
| Allpass1Pole | Phase dispersion for inharmonicity (stretch) |
| DCBlocker2 | Prevent DC accumulation in feedback loop |
| Xorshift32 | Real-time safe noise generation for pluck/bow |

## Implementation Phases

### Phase 1: TwoPoleLP Primitive (Layer 1)

**Objective**: Create reusable 12dB/oct lowpass filter

**Components**:
1. `TwoPoleLP` class in `dsp/include/krate/dsp/primitives/two_pole_lp.h`
2. Unit tests in `dsp/tests/primitives/two_pole_lp_tests.cpp`

**API Design**:
```cpp
class TwoPoleLP {
public:
    void prepare(double sampleRate) noexcept;
    void setCutoff(float hz) noexcept;
    [[nodiscard]] float getCutoff() const noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;

private:
    Biquad filter_;
    float cutoffHz_ = 1000.0f;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};
```

**Test Cases**:
- TC-TLP-001: Frequency response is -6dB at cutoff
- TC-TLP-002: Frequency response is -12dB one octave above cutoff
- TC-TLP-003: Passband is flat (within 0.5dB) below cutoff/2
- TC-TLP-004: NaN/Inf input handling (returns 0, resets state)
- TC-TLP-005: Returns input unchanged if not prepared

### Phase 2: KarplusStrong Core (Layer 2)

**Objective**: Implement basic plucked string with frequency and decay control

**Requirements Covered**: FR-001 to FR-004, FR-017 to FR-019, FR-022 to FR-029

**Components**:
1. `KarplusStrong` class shell with prepare/reset/process
2. Delay line setup with allpass interpolation
3. Feedback loop with decay control
4. DC blocking in feedback path

**API Design (Core)**:
```cpp
class KarplusStrong {
public:
    // Lifecycle
    void prepare(double sampleRate, float minFrequency = 20.0f) noexcept;
    void reset() noexcept;

    // Frequency and decay
    void setFrequency(float hz) noexcept;
    void setDecay(float seconds) noexcept;

    // Processing
    [[nodiscard]] float process(float input = 0.0f) noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
    void processBlock(const float* input, float* output, size_t numSamples) noexcept;
};
```

**Test Cases**:
- TC-KS-001: Output frequency matches setFrequency within 1 cent (SC-001)
- TC-KS-002: Decay time matches setDecay within 10% (SC-003)
- TC-KS-003: No DC offset accumulation after 10 minutes (SC-005)
- TC-KS-004: NaN input causes reset and returns 0 (FR-030)
- TC-KS-005: Frequency clamping to valid range (FR-031)

### Phase 3: Excitation Methods

**Objective**: Implement pluck(), bow(), excite() methods

**Requirements Covered**: FR-005 to FR-010, FR-033

**Components**:
1. `pluck(velocity)` - noise burst excitation
2. `bow(pressure)` - continuous excitation
3. `excite(signal, length)` - custom excitation buffer

**API Design (Excitation)**:
```cpp
class KarplusStrong {
    // ... core methods ...

    // Excitation
    void pluck(float velocity = 1.0f) noexcept;
    void bow(float pressure) noexcept;
    void excite(const float* signal, size_t length) noexcept;
};
```

**Test Cases**:
- TC-KS-006: pluck() produces pitched output at set frequency
- TC-KS-007: pluck velocity scales amplitude (FR-006)
- TC-KS-008: bow() produces sustained output (SC-009)
- TC-KS-009: bow(0.0f) causes natural decay (US3-AC3)
- TC-KS-010: excite() with sine burst produces purer tone (US4-AC1)
- TC-KS-011: Re-pluck during active adds with normalization (FR-033)

### Phase 4: Tone Shaping Parameters

**Objective**: Implement damping, brightness, pick position

**Requirements Covered**: FR-011 to FR-016

**Components**:
1. `setDamping(amount)` - high-frequency loss control
2. `setBrightness(amount)` - excitation spectrum control
3. `setPickPosition(position)` - comb filtering simulation

**Damping Multiplier Formula (FR-012 clarification)**:
The damping parameter (0.0-1.0) maps to OnePoleLP cutoff frequency relative to fundamental:
- `damping = 0.0` → `cutoff = fundamental × 20.0` (bright, minimal HF loss, ~20× fundamental)
- `damping = 0.5` → `cutoff = fundamental × 4.0` (moderate HF loss)
- `damping = 1.0` → `cutoff = fundamental × 1.0` (dark, maximum HF loss, cutoff at fundamental)
- Formula: `cutoff = fundamental × (1.0 + 19.0 × (1.0 - damping))`
This ensures consistent damping behavior across the entire pitch range.

**Pick Position Tap Implementation (FR-016 clarification)**:
During pluck(), the delay line is filled with noise that is read from a tapped position:
- When fillling sample at index `i`, read existing buffer content from `(i + tapOffset) % delayLength`
- `tapOffset = pickPosition × delayLength`
- This creates natural comb filtering: picking at position P attenuates harmonics at multiples of 1/P
- The tapped signal REPLACES the direct excitation (not mixed/added), creating authentic physical modeling
- pickPosition=0.5 (center) attenuates even harmonics; pickPosition=0.1 (near bridge) preserves more harmonics

**API Design (Tone Shaping)**:
```cpp
class KarplusStrong {
    // ... core and excitation methods ...

    // Tone shaping
    void setDamping(float amount) noexcept;       // 0.0-1.0
    void setBrightness(float amount) noexcept;    // 0.0-1.0
    void setPickPosition(float position) noexcept; // 0.0-1.0
};
```

**Test Cases**:
- TC-KS-012: Higher damping = less high-frequency content (US1-AC2)
- TC-KS-013: Higher damping = faster decay
- TC-KS-014: brightness=1.0 has more HF than brightness=0.2 (US2-AC1, US2-AC2)
- TC-KS-015: pickPosition=0.5 attenuates even harmonics (US2-AC3)
- TC-KS-016: pickPosition=0.1 has more harmonics (US2-AC4)

### Phase 5: Inharmonicity (Stretch)

**Objective**: Implement stretch parameter for piano-like inharmonicity

**Requirements Covered**: FR-020, FR-021, SC-010

**Components**:
1. `setStretch(amount)` - allpass dispersion control
2. Allpass filter coefficient calculation from stretch amount

**API Design (Inharmonicity)**:
```cpp
class KarplusStrong {
    // ... previous methods ...

    // Inharmonicity
    void setStretch(float amount) noexcept;  // 0.0-1.0
};
```

**Test Cases**:
- TC-KS-017: stretch=0.0 produces harmonic overtones (US5-AC1)
- TC-KS-018: stretch>0.3 produces audible inharmonicity (SC-010)
- TC-KS-019: High stretch produces bell-like timbre (US5-AC3)

### Phase 6: Edge Cases and Stability

**Objective**: Handle all edge cases, ensure long-term stability

**Requirements Covered**: FR-030 to FR-032, SC-005

**Components**:
1. Parameter clamping validation
2. Extreme frequency handling
3. Long-term stability testing

**Test Cases**:
- TC-KS-020: Frequency below 20Hz clamped (edge case)
- TC-KS-021: Frequency above Nyquist/2 clamped (FR-031)
- TC-KS-022: Very short decay (<10ms) produces brief transient (edge case)
- TC-KS-023: Very long decay (>30s) feedback clamped (edge case)
- TC-KS-024: 10-minute stability test (SC-005)
- TC-KS-025: CPU usage < 0.5% per voice (SC-004)

## Test Strategy

### Test Categories

| Category | Location | Purpose |
|----------|----------|---------|
| Unit Tests | `dsp/tests/primitives/two_pole_lp_tests.cpp` | TwoPoleLP filter behavior |
| Unit Tests | `dsp/tests/processors/karplus_strong_tests.cpp` | KarplusStrong behavior |
| Pitch Tests | karplus_strong_tests.cpp | SC-001: 1 cent accuracy |
| Decay Tests | karplus_strong_tests.cpp | SC-003: 10% accuracy |
| Stability Tests | karplus_strong_tests.cpp | SC-005: 10 min operation |
| Performance Tests | karplus_strong_tests.cpp | SC-004: <0.5% CPU |

### Test Utilities Required

- FFT-based pitch detection (existing in test infrastructure)
- RT60 decay measurement
- CPU usage measurement (chrono-based)

### Approval Testing

For audio quality verification, manual listening tests against reference sounds:
- Nylon guitar pluck
- Steel string guitar
- Harpsichord
- Harp

## Risk Analysis

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Pitch accuracy not meeting 1 cent | Low | High | Use allpass interpolation, verify with FFT pitch detection |
| DC accumulation causing offset | Medium | Medium | DCBlocker2 in feedback path, long-term stability test |
| Denormal slowdown | Low | Medium | flushDenormal on feedback state |
| Re-pluck normalization causing clipping | Medium | Low | Sum buffer, normalize if max > 1.0 |
| Allpass state corruption causing instability | Low | High | Reset allpass state on extreme inputs |

## Dependencies Graph

```
Layer 0 (Core)
├── math_constants.h (kTwoPi, kPi)
├── db_utils.h (flushDenormal, isNaN, isInf)
└── random.h (Xorshift32)

Layer 1 (Primitives)
├── delay_line.h (DelayLine)
├── one_pole.h (OnePoleLP)
├── allpass_1pole.h (Allpass1Pole)
├── dc_blocker.h (DCBlocker2)
├── biquad.h (Biquad, FilterType)
└── two_pole_lp.h (TwoPoleLP) [NEW]

Layer 2 (Processors)
└── karplus_strong.h (KarplusStrong) [NEW]
    └── depends on: DelayLine, OnePoleLP, TwoPoleLP, Allpass1Pole, DCBlocker2, Xorshift32
```

## Complexity Tracking

> **No Constitution Check violations requiring justification.**

All design decisions follow Constitution principles:
- Layer 2 depends only on Layer 0/1
- Real-time safe design (noexcept, pre-allocated buffers)
- Test-first development approach
- ODR prevention via codebase search
