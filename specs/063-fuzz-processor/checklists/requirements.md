# Requirements Checklist: FuzzProcessor

**Feature**: 063-fuzz-processor | **Date**: 2026-01-14

## Specification Quality (from spec phase)

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed
- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

---

## Functional Requirements

### Fuzz Type Enumeration

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-001 | FuzzType enum with Germanium and Silicon, uint8_t underlying | P1 | Unit test enum values |

### Lifecycle Methods

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-002 | prepare(sampleRate, maxBlockSize) configures processor | P1 | Unit test prepare() |
| FR-003 | reset() clears state without reallocation | P1 | Unit test reset() |
| FR-004 | Before prepare(), process() returns input unchanged | P1 | Unit test unprepared processing |
| FR-005 | Default constructor: type=Germanium, fuzz=0.5, volume=0dB, bias=0.7, tone=0.5 | P1 | Unit test defaults |

### Parameter Setters

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-006 | setFuzzType(type) selects transistor type | P1 | Unit test type switching |
| FR-006a | Type changes crossfade over 5ms | P1 | Audio analysis for clicks |
| FR-007 | setFuzz(amount) clamped to [0.0, 1.0] | P2 | Unit test clamping |
| FR-008 | setVolume(dB) clamped to [-24, +24] | P3 | Unit test clamping |
| FR-009 | setBias(bias) clamped to [0.0, 1.0] | P2 | Unit test clamping |
| FR-010 | setTone(tone) clamped to [0.0, 1.0] | P2 | Unit test clamping |

### Getter Methods

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-011 | getFuzzType() returns current type | P1 | Unit test getter |
| FR-012 | getFuzz() returns fuzz amount | P2 | Unit test getter |
| FR-013 | getVolume() returns volume in dB | P3 | Unit test getter |
| FR-014 | getBias() returns bias value | P2 | Unit test getter |
| FR-015 | getTone() returns tone value | P2 | Unit test getter |

### Germanium Type Implementation

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-016 | Germanium applies softer clipping with even+odd harmonics | P1 | FFT harmonic analysis |
| FR-017 | Germanium sag via envelope follower (1ms attack, 100ms release) | P1 | Transient response test |
| FR-018 | Germanium uses Asymmetric::tube() or similar | P1 | Code inspection |

### Silicon Type Implementation

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-019 | Silicon applies harder clipping with odd harmonics | P1 | FFT harmonic analysis |
| FR-020 | Silicon has tighter, more consistent clipping | P1 | Waveform comparison |
| FR-021 | Silicon uses Sigmoid::tanh() or similar | P1 | Code inspection |

### Bias Implementation

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-022 | Bias affects operating point, low bias creates gating | P2 | Gating measurement |
| FR-023 | Bias=0.0 creates maximum gating | P2 | Audio analysis |
| FR-024 | Bias=1.0 creates normal operation | P2 | Audio analysis |
| FR-025 | Bias implemented as DC offset before waveshaping | P2 | Code inspection |

### Tone Control Implementation

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-026 | Tone is 1-pole or 2-pole lowpass after waveshaping | P2 | Code inspection |
| FR-027 | Tone=0.0 sets cutoff to ~400Hz | P2 | Frequency response |
| FR-028 | Tone=1.0 sets cutoff to ~8000Hz | P2 | Frequency response |
| FR-029 | Tone filter uses Biquad from Layer 1 | P2 | Code inspection |

### Processing

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-030 | process(buffer, numSamples) in-place block processing | P1 | Unit test processing |
| FR-031 | process() does NOT allocate memory | P1 | Memory profiling |
| FR-032 | process() handles numSamples=0 gracefully | P1 | Unit test edge case |

### DC Blocking

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-033 | DC blocking after saturation | P1 | DC offset measurement |
| FR-034 | DC blocker cutoff ~10Hz | P1 | Code inspection |
| FR-035 | DC blocker uses DCBlocker from Layer 1 | P1 | Code inspection |

### Parameter Smoothing

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-036 | Fuzz changes smoothed (5ms target, <10ms max) | P2 | Click detection |
| FR-037 | Volume changes smoothed | P2 | Click detection |
| FR-038 | Bias changes smoothed | P2 | Click detection |
| FR-039 | Tone changes smoothed | P2 | Click detection |
| FR-040 | reset() snaps smoothers to target | P2 | Unit test reset |

### Component Composition

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-041 | Uses Waveshaper (Layer 1) for saturation | P1 | Code inspection |
| FR-042 | Uses DCBlocker (Layer 1) for DC removal | P1 | Code inspection |
| FR-043 | Uses Biquad (Layer 1) for tone filter | P2 | Code inspection |
| FR-044 | Uses OnePoleSmoother (Layer 1) for smoothing | P2 | Code inspection |

### Architecture & Quality

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-045 | Header-only implementation | P1 | File structure |
| FR-046 | Namespace Krate::DSP | P1 | Code inspection |
| FR-047 | Only depends on Layer 0 and Layer 1 | P1 | Include analysis |
| FR-048 | Doxygen documentation | P1 | Documentation review |
| FR-049 | Naming conventions followed | P1 | Code review |

### Octave Fuzz Option

| ID | Requirement | Priority | Test Strategy |
|----|-------------|----------|---------------|
| FR-050 | setOctaveUp(bool) enables/disables octave-up | P2 | Unit test |
| FR-051 | getOctaveUp() returns octave-up state | P2 | Unit test getter |
| FR-052 | Octave-up: input * |input| for self-modulation | P2 | Harmonic analysis |
| FR-053 | Octave-up applied before main fuzz stage | P2 | Code inspection |

---

## Success Criteria

| ID | Criterion | Test Strategy |
|----|-----------|---------------|
| SC-001 | Germanium and Silicon produce measurably different spectra | FFT comparison |
| SC-002 | Germanium produces measurable even harmonics (2nd, 4th) | FFT analysis |
| SC-003 | Silicon produces predominantly odd harmonics (3rd, 5th) | FFT analysis |
| SC-004 | Parameter changes complete within 10ms without clicks | Click detector |
| SC-005 | Processing < 0.5% CPU @ 44.1kHz/2.5GHz baseline | Benchmark |
| SC-006 | DC offset < -50dBFS after processing | DC measurement |
| SC-007 | Tests pass at 44.1k, 48k, 88.2k, 96k, 192kHz | Multi-rate tests |
| SC-008 | Fuzz=0.0 produces THD < 1% | THD measurement |
| SC-009 | Bias=0.2 attenuates -20dBFS by >6dB vs bias=1.0 | Level comparison |
| SC-010 | Tone 0->1 shows >12dB change at 4kHz | Frequency response |
| SC-011 | Octave-up produces measurable 2nd harmonic | FFT analysis |

---

## Implementation Status

*To be filled during implementation*

| Requirement | Status | Evidence | Notes |
|-------------|--------|----------|-------|
| FR-001 | PENDING | | |
| FR-002 | PENDING | | |
| FR-003 | PENDING | | |
| FR-004 | PENDING | | |
| FR-005 | PENDING | | |
| FR-006 | PENDING | | |
| FR-006a | PENDING | | |
| FR-007 | PENDING | | |
| FR-008 | PENDING | | |
| FR-009 | PENDING | | |
| FR-010 | PENDING | | |
| FR-011 | PENDING | | |
| FR-012 | PENDING | | |
| FR-013 | PENDING | | |
| FR-014 | PENDING | | |
| FR-015 | PENDING | | |
| FR-016 | PENDING | | |
| FR-017 | PENDING | | |
| FR-018 | PENDING | | |
| FR-019 | PENDING | | |
| FR-020 | PENDING | | |
| FR-021 | PENDING | | |
| FR-022 | PENDING | | |
| FR-023 | PENDING | | |
| FR-024 | PENDING | | |
| FR-025 | PENDING | | |
| FR-026 | PENDING | | |
| FR-027 | PENDING | | |
| FR-028 | PENDING | | |
| FR-029 | PENDING | | |
| FR-030 | PENDING | | |
| FR-031 | PENDING | | |
| FR-032 | PENDING | | |
| FR-033 | PENDING | | |
| FR-034 | PENDING | | |
| FR-035 | PENDING | | |
| FR-036 | PENDING | | |
| FR-037 | PENDING | | |
| FR-038 | PENDING | | |
| FR-039 | PENDING | | |
| FR-040 | PENDING | | |
| FR-041 | PENDING | | |
| FR-042 | PENDING | | |
| FR-043 | PENDING | | |
| FR-044 | PENDING | | |
| FR-045 | PENDING | | |
| FR-046 | PENDING | | |
| FR-047 | PENDING | | |
| FR-048 | PENDING | | |
| FR-049 | PENDING | | |
| FR-050 | PENDING | | |
| FR-051 | PENDING | | |
| FR-052 | PENDING | | |
| FR-053 | PENDING | | |
| SC-001 | PENDING | | |
| SC-002 | PENDING | | |
| SC-003 | PENDING | | |
| SC-004 | PENDING | | |
| SC-005 | PENDING | | |
| SC-006 | PENDING | | |
| SC-007 | PENDING | | |
| SC-008 | PENDING | | |
| SC-009 | PENDING | | |
| SC-010 | PENDING | | |
| SC-011 | PENDING | | |

**Status Key:**
- PENDING: Not started
- IN_PROGRESS: Implementation started
- MET: Requirement fully satisfied with test evidence
- NOT_MET: Requirement not satisfied
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval
