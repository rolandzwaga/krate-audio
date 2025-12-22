# Feature Specification: Delay Line DSP Primitive

**Feature Branch**: `002-delay-line`
**Created**: 2025-12-22
**Status**: Draft
**Input**: User description: "Layer 1 DSP Primitive - A real-time safe circular buffer delay line with fractional sample interpolation. Core building block for all delay effects."

## Overview

The Delay Line is a Layer 1 DSP Primitive that provides a real-time safe circular buffer with fractional sample interpolation. It serves as the foundational building block for all delay-based effects in the plugin, including:
- Simple delays
- Chorus/flanger (via modulated delay times)
- Feedback networks
- Reverb diffusion stages

As a Layer 1 component, it may only depend on Layer 0 utilities and must be independently testable without VST infrastructure.

## User Scenarios & Testing *(mandatory)*

**Note**: For this DSP primitive, "users" are developers building higher-layer DSP components.

### User Story 1 - Basic Fixed Delay (Priority: P1)

A DSP developer needs to implement a simple delay effect with a fixed delay time. They instantiate a delay line, configure the maximum delay time, and read samples at a constant offset from the write position.

**Why this priority**: Fixed delay is the most fundamental use case and validates core read/write functionality.

**Independent Test**: Can be fully tested by writing a known signal and reading it back at a fixed offset, verifying the output matches the original signal delayed by the expected number of samples.

**Acceptance Scenarios**:

1. **Given** a delay line configured for 1 second max delay at 44100 Hz, **When** I write 44100 samples and read at 22050 samples delay, **Then** the read output matches the written input delayed by exactly 22050 samples.
2. **Given** a delay line with 0 samples delay, **When** I write and read simultaneously, **Then** the output equals the current input (no delay).
3. **Given** a delay line at maximum delay time, **When** I read at max delay, **Then** the output is the oldest sample in the buffer.

---

### User Story 2 - Fractional Delay with Linear Interpolation (Priority: P2)

A DSP developer needs sub-sample accurate delay for pitch shifting or smooth delay time modulation. They configure the delay line for linear interpolation and read at fractional sample positions.

**Why this priority**: Fractional delays enable smooth modulation and precise timing, essential for chorus/flanger effects.

**Independent Test**: Can be tested by reading at fractional positions and verifying the output matches linearly interpolated values between adjacent samples.

**Acceptance Scenarios**:

1. **Given** a delay line containing samples [0.0, 1.0] at positions [0, 1], **When** I read at position 0.5 with linear interpolation, **Then** the output is 0.5 (midpoint).
2. **Given** a delay line containing samples [0.0, 1.0, 0.0], **When** I read at position 1.25 with linear interpolation, **Then** the output is 0.75 (interpolated between 1.0 and 0.0).
3. **Given** fractional delays, **When** the delay time changes smoothly, **Then** the output has no discontinuities (zipper noise).

---

### User Story 3 - Allpass Interpolation for Feedback Loops (Priority: P3)

A DSP developer building a feedback delay network needs interpolation that doesn't cause amplitude distortion when placed in a feedback loop. They configure the delay line for allpass interpolation.

**Why this priority**: Allpass interpolation is critical for feedback networks but is a specialized use case after basic and linear delay are working.

**Independent Test**: Can be tested by processing a sinusoid through an allpass-interpolated delay and verifying the output amplitude matches the input (unity gain at all frequencies).

**Acceptance Scenarios**:

1. **Given** a delay line with allpass interpolation in a feedback loop, **When** processing at fractional delay times, **Then** the frequency response is flat (no amplitude distortion).
2. **Given** allpass interpolation, **When** reading at integer sample positions, **Then** the output matches the exact sample value (no interpolation artifacts).
3. **Given** allpass interpolation, **When** the delay time is static, **Then** phase response is frequency-dependent but amplitude is preserved.

---

### User Story 4 - Modulated Delay Time (Priority: P2)

A DSP developer creating a chorus effect needs to smoothly vary the delay time using an LFO. The delay line must handle rapid delay time changes without audible artifacts.

**Why this priority**: Modulation is core to chorus/flanger/vibrato effects, making this essential for Layer 2 processors.

**Independent Test**: Can be tested by sweeping delay time while processing a signal and verifying no clicks, pops, or discontinuities in the output.

**Acceptance Scenarios**:

1. **Given** a delay line with linear interpolation, **When** delay time is modulated by an LFO, **Then** the output is smooth with no audible artifacts.
2. **Given** delay time changing from 10ms to 20ms over 100 samples, **When** processing, **Then** the transition produces no discontinuities.
3. **Given** rapid delay time modulation (audio-rate), **When** using linear interpolation, **Then** the output maintains signal integrity.

---

### User Story 5 - Real-Time Safety (Priority: P1)

A DSP developer integrating the delay line into the audio processor needs guaranteed real-time safety. The delay line must never allocate memory or block during the process callback.

**Why this priority**: Real-time safety is a constitution-level requirement; violations cause audio glitches.

**Independent Test**: Can be verified by code inspection and memory profiler analysis during process calls.

**Acceptance Scenarios**:

1. **Given** a configured delay line, **When** calling read/write during audio processing, **Then** no memory allocations occur.
2. **Given** `prepare()` is called before `setActive(true)`, **When** processing begins, **Then** all buffers are pre-allocated.
3. **Given** any valid input, **When** calling process methods, **Then** no exceptions are thrown (noexcept guarantee).

---

### Edge Cases

- What happens when reading beyond the buffer length? Return the oldest available sample (clamp to max delay).
- How does the system handle delay time of 0 samples? Return current input sample directly.
- What happens when delay time equals exactly the buffer length? Return the sample about to be overwritten.
- How does the system handle negative delay times? Clamp to 0 (no lookahead).
- What happens when sample rate changes mid-session? `prepare()` must be called again to resize buffers.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Delay line MUST provide a circular buffer implementation for storing audio samples.
- **FR-002**: Delay line MUST support configurable maximum delay time (up to 10 seconds at 192kHz = 1,920,000 samples).
- **FR-003**: Delay line MUST provide integer sample delay (no interpolation mode).
- **FR-004**: Delay line MUST provide linear interpolation for fractional sample delays.
- **FR-005**: Delay line MUST provide allpass interpolation for fractional sample delays.
- **FR-006**: Delay line MUST pre-allocate all memory in `prepare()` before audio processing begins.
- **FR-007**: Delay line MUST NOT allocate memory, throw exceptions, or perform blocking operations in read/write methods.
- **FR-008**: Delay line MUST provide a `reset()` method to clear the buffer to silence.
- **FR-009**: Delay line MUST clamp delay time requests to valid range [0, maxDelay].
- **FR-010**: Delay line MUST maintain sample-accurate delay regardless of block size.
- **FR-011**: Delay line MUST support mono operation (single channel).
- **FR-012**: All public methods MUST be marked `noexcept`.

### Non-Functional Requirements

- **NFR-001**: Read/write operations MUST complete in O(1) time.
- **NFR-002**: Memory footprint MUST be `maxDelaySamples * sizeof(float)` plus minimal overhead.
- **NFR-003**: Delay line MUST be usable as a constexpr type where possible (C++20).

### Key Entities

- **DelayLine**: The primary class providing circular buffer delay functionality.
  - Attributes: maxDelaySamples, writeIndex, sampleRate
  - Operations: prepare(), reset(), write(), read(), readLinear(), readAllpass()

- **InterpolationType**: Conceptual modes (implemented as separate methods for clarity and performance):
  - None (integer): `read(size_t)` - fastest, no interpolation
  - Linear: `readLinear(float)` - smooth modulation
  - Allpass: `readAllpass(float)` - unity gain for feedback loops

  *Design Note*: Separate methods chosen over enum parameter to eliminate runtime branching and clarify intent at call site.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Delay accuracy within 1 sample for integer delays, verified by unit tests.
- **SC-002**: Linear interpolation error less than 0.01% for sinusoidal signals at all frequencies.
- **SC-003**: Allpass interpolation maintains unity gain (within 0.001 dB) at all frequencies.
- **SC-004**: Process methods execute in under 100 nanoseconds per sample (measured on x64 CPU at 3.0GHz+, Release build).
- **SC-005**: Memory usage equals exactly `maxDelaySamples * sizeof(float) + constant overhead`.
- **SC-006**: Zero memory allocations detected during process callback (verified by profiler).
- **SC-007**: All unit tests pass on Windows (MSVC), macOS (Clang), and Linux (GCC).

## Assumptions

- Sample rate is known and stable during audio processing (changes require `prepare()` call).
- Maximum delay time of 10 seconds at 192kHz is sufficient for all plugin features.
- Single-channel (mono) operation is sufficient; stereo will use two DelayLine instances.
- Allpass interpolation coefficient can be computed per-sample without excessive CPU cost.
- Linear interpolation is acceptable for modulated delays (per CLAUDE.md guidance on avoiding allpass for modulated delays).

## Dependencies

- **Layer 0**: No external dependencies; uses only standard library and Layer 0 utilities.
- **Constitution**: Must comply with Principle II (Real-Time Safety) and Principle IX (Layered Architecture).

## Out of Scope

- Multi-channel delay (handled by instantiating multiple DelayLine objects)
- Tempo-sync (handled by Layer 3 systems)
- Feedback path (handled by Layer 3 feedback network)
- Parameter smoothing for delay time (handled by Layer 1 Smoother primitive)
- Higher-order interpolation (cubic, sinc) - may be added in future iteration
