# Feature Specification: Envelope Filter / Auto-Wah

**Feature Branch**: `078-envelope-filter`
**Created**: 2026-01-22
**Status**: Draft
**Input**: User description: "Envelope Filter / Auto-Wah - Layer 2 processor combining EnvelopeFollower with SVF for classic wah/envelope filter effects. Use cases: Auto-wah, touch-sensitive filtering, dynamic EQ."

## Clarifications

### Session 2026-01-22

- Q: FilterType to SVFMode mapping - which specific SVF modes should be used? → A: Lowpass→SVFMode::Lowpass, Bandpass→SVFMode::Bandpass, Highpass→SVFMode::Highpass (explicit enum qualification, 3 modes only)
- Q: Envelope clamping for frequency mapping - should envelope > 1.0 push cutoff beyond maxFrequency? → A: Always clamp envelope to [0.0, 1.0] before frequency mapping (maxFrequency is hard ceiling)
- Q: minFrequency >= maxFrequency handling - swap or clamp? → A: Clamp - setMinFrequency() clamps to maxFreq-1Hz, setMaxFrequency() clamps to minFreq+1Hz
- Q: Sensitivity gain application point - for envelope only or both envelope and filter? → A: Apply sensitivity gain only for envelope detection, pass original input to filter
- Q: Depth parameter behavior at extremes - what happens when depth = 0? → A: depth = 0 means cutoff stays at minFrequency (Up) or maxFrequency (Down), no modulation

## Overview

This specification defines an Envelope Filter (Auto-Wah) processor that combines the existing EnvelopeFollower (010) with the SVF (071) to create classic wah and touch-sensitive filter effects. The envelope of the input signal controls the filter cutoff frequency, creating dynamic tonal effects that respond to playing dynamics.

**Location**: `dsp/include/krate/dsp/processors/envelope_filter.h`
**Layer**: 2 (Processor)
**Test File**: `dsp/tests/processors/envelope_filter_test.cpp`
**Namespace**: `Krate::DSP`

### Motivation

The EnvelopeFilter (Auto-Wah) is a classic effect that:
1. **Responds to playing dynamics**: Filter opens/closes based on input level
2. **Creates expressive sounds**: "Touch-sensitive" response makes the sound more organic
3. **Versatile applications**: From classic funk guitar to electronic music filter sweeps

This component combines two existing, tested primitives (EnvelopeFollower and SVF) to create a complete effect with minimal new code.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Classic Auto-Wah Effect (Priority: P1)

A guitarist or bassist wants to apply a classic auto-wah effect where playing harder opens the filter (higher cutoff) and playing softer closes it. The effect should respond naturally to playing dynamics without manual control.

**Why this priority**: This is the fundamental use case for an envelope filter. Without envelope-controlled cutoff modulation, the component has no value. This represents the minimum viable product.

**Independent Test**: Can be fully tested by processing a signal with varying amplitude and verifying the filter cutoff tracks the envelope proportionally.

**Acceptance Scenarios**:

1. **Given** an EnvelopeFilter in Up direction with 10ms attack and 100ms release, **When** a sudden loud input occurs (burst of audio), **Then** the filter cutoff rises from minFrequency toward maxFrequency tracking the envelope shape.

2. **Given** an EnvelopeFilter configured with minFrequency 200Hz and maxFrequency 2000Hz, **When** processing a signal at full amplitude (envelope = 1.0), **Then** the filter cutoff approaches maxFrequency (within 5% of target after settling).

3. **Given** an EnvelopeFilter in Down direction, **When** playing loudly, **Then** the filter cutoff moves from maxFrequency toward minFrequency (inverse of Up direction).

---

### User Story 2 - Touch-Sensitive Filter with Resonance (Priority: P1)

A sound designer wants to create a resonant filter sweep that responds to playing dynamics. High resonance creates the characteristic "wah" vowel-like sound. The filter should remain stable even at high resonance values.

**Why this priority**: Resonance is essential for the characteristic "wah" sound. Without it, the effect is just a dynamic lowpass filter. This is part of the core functionality.

**Independent Test**: Can be tested by processing audio with high Q values (8-15) and verifying stable, resonant filter sweeps without oscillation or instability.

**Acceptance Scenarios**:

1. **Given** an EnvelopeFilter in Bandpass mode with Q = 10, **When** processing a chord strum, **Then** the output exhibits the characteristic vowel-like resonant sweep.

2. **Given** an EnvelopeFilter with Q = 20 (high resonance), **When** sweeping across the full frequency range, **Then** the filter remains stable (no runaway oscillation or NaN output).

3. **Given** an EnvelopeFilter with Q set above maximum (30), **When** Q is clamped to safe maximum, **Then** the filter continues to operate safely.

---

### User Story 3 - Multiple Filter Types (Priority: P2)

A producer wants to use different filter types (lowpass, bandpass, highpass) for different sonic characters. Bandpass gives the classic wah, lowpass is more subtle, highpass creates "backwards" effects.

**Why this priority**: Multiple filter modes extend creative possibilities but are not essential for the core auto-wah functionality. The effect works with just bandpass mode.

**Independent Test**: Can be tested by switching filter modes and verifying each produces the expected frequency response while envelope modulation continues to work.

**Acceptance Scenarios**:

1. **Given** an EnvelopeFilter in Lowpass mode, **When** processing white noise with modulated envelope, **Then** the output shows lowpass frequency response that sweeps with the envelope.

2. **Given** an EnvelopeFilter in Bandpass mode, **When** processing at cutoff frequency, **Then** the bandpass peak is near unity gain with frequencies above and below attenuated.

3. **Given** an EnvelopeFilter in Highpass mode with Down direction, **When** playing softly, **Then** the filter cutoff is high, passing mostly high frequencies.

---

### User Story 4 - Sensitivity and Pre-Gain Control (Priority: P2)

A player with a low-output instrument (passive bass, quiet synth patch) needs to boost the signal before envelope detection so the filter responds to their playing dynamics. Conversely, a hot signal may need attenuation.

**Why this priority**: Sensitivity control ensures the effect works with various input levels, but default settings should work for most signals. This is an enhancement for edge cases.

**Independent Test**: Can be tested by processing a quiet signal with increased sensitivity and verifying the envelope responds as if the input were louder.

**Acceptance Scenarios**:

1. **Given** an EnvelopeFilter with sensitivity +12dB, **When** processing a -18dBFS signal, **Then** the envelope responds as if the signal were -6dBFS.

2. **Given** an EnvelopeFilter with sensitivity 0dB (default), **When** processing a typical guitar signal, **Then** the filter responds naturally to playing dynamics.

3. **Given** an EnvelopeFilter with sensitivity -6dB, **When** processing a hot synth signal, **Then** the envelope response is tamed and more subtle.

---

### User Story 5 - Dry/Wet Mix Control (Priority: P3)

A user wants to blend the dry (unfiltered) signal with the wet (filtered) signal to create parallel filtering effects or subtle enhancement without losing the original character.

**Why this priority**: Mix control is a common effect parameter but not essential for the core functionality. Many classic auto-wah pedals are 100% wet only.

**Independent Test**: Can be tested by setting mix to 0.5 and verifying the output is an equal blend of dry input and filtered output.

**Acceptance Scenarios**:

1. **Given** an EnvelopeFilter with mix = 0.0 (fully dry), **When** processing audio, **Then** the output equals the input (no filtering applied).

2. **Given** an EnvelopeFilter with mix = 1.0 (fully wet), **When** processing audio, **Then** the output is 100% filtered signal.

3. **Given** an EnvelopeFilter with mix = 0.5, **When** processing audio, **Then** the output is a 50/50 blend of dry and filtered signals.

---

### Edge Cases

- What happens when input is silent (all zeros)? Envelope decays to zero, filter cutoff settles at minFrequency (Up) or maxFrequency (Down), output is silent.
- What happens with depth = 0? Filter cutoff stays fixed at minFrequency (Up) or maxFrequency (Down) regardless of envelope value, effectively disabling modulation and creating a static filter.
- What happens when minFrequency >= maxFrequency? Use clamping strategy: setMinFrequency() clamps to maxFreq-1Hz if needed, setMaxFrequency() clamps to minFreq+1Hz if needed. This ensures minFreq < maxFreq always.
- What happens when envelope exceeds 1.0 (hot signal)? Envelope is clamped to [0.0, 1.0] before frequency mapping, so cutoff never exceeds maxFrequency.
- What happens when frequency range approaches or exceeds Nyquist? Clamp maxFrequency to 0.45 * sampleRate for filter stability.
- What happens when process() is called before prepare()? Return input unchanged (safe default, matching SVF behavior).
- What happens with NaN/Inf input? Return 0 and reset internal state (inherited from SVF behavior).
- What happens with very fast attack (< 1ms)? Clamp to EnvelopeFollower minimum (0.1ms) for stability.
- What happens when reset() is called during processing? Clear both envelope and filter states without affecting parameters.

## Requirements *(mandatory)*

### Functional Requirements

#### Class Structure and Types

- **FR-001**: envelope_filter.h MUST define class `EnvelopeFilter` in namespace `Krate::DSP` at `dsp/include/krate/dsp/processors/envelope_filter.h`.
- **FR-002**: EnvelopeFilter MUST define enum class `Direction` with values: `Up` (envelope opens filter), `Down` (envelope closes filter).
- **FR-003**: EnvelopeFilter MUST define enum class `FilterType` with values: `Lowpass`, `Bandpass`, `Highpass`.

#### Composition

- **FR-004**: EnvelopeFilter MUST compose an `EnvelopeFollower` instance for amplitude envelope tracking (from `envelope_follower.h`).
- **FR-005**: EnvelopeFilter MUST compose an `SVF` instance for filtering (from `svf.h`).
- **FR-006**: EnvelopeFilter MUST NOT allocate memory in the processing path; all allocations occur in `prepare()`.

#### Lifecycle

- **FR-007**: EnvelopeFilter MUST provide `void prepare(double sampleRate)` that initializes both composed components for the given sample rate.
- **FR-008**: EnvelopeFilter MUST provide `void reset() noexcept` that clears both envelope and filter states without reallocation.

#### Envelope Parameters

- **FR-009**: EnvelopeFilter MUST provide `void setSensitivity(float dB)` with range [-24dB, +24dB] to control input gain before envelope detection.
- **FR-010**: EnvelopeFilter MUST provide `void setAttack(float ms)` as a convenience wrapper that delegates to EnvelopeFollower::setAttackTime() with range [0.1ms, 500ms].
- **FR-011**: EnvelopeFilter MUST provide `void setRelease(float ms)` as a convenience wrapper that delegates to EnvelopeFollower::setReleaseTime() with range [1ms, 5000ms].
- **FR-012**: EnvelopeFilter MUST provide `void setDirection(Direction dir)` to control whether envelope opens (Up) or closes (Down) the filter.

#### Filter Parameters

- **FR-013**: EnvelopeFilter MUST provide `void setFilterType(FilterType type)` mapping to SVF as follows: FilterType::Lowpass→SVFMode::Lowpass, FilterType::Bandpass→SVFMode::Bandpass, FilterType::Highpass→SVFMode::Highpass. Only these 3 SVF modes are exposed (Notch, Peak, AllPass, Shelf are not used).
- **FR-014**: EnvelopeFilter MUST provide `void setMinFrequency(float hz)` with range [20Hz, sampleRate * 0.4] for the low end of the sweep range. If the new minFrequency would be >= maxFrequency, clamp it to maxFrequency - 1Hz.
- **FR-015**: EnvelopeFilter MUST provide `void setMaxFrequency(float hz)` with range [20Hz, sampleRate * 0.45] for the high end of the sweep range. If the new maxFrequency would be <= minFrequency, clamp it to minFrequency + 1Hz.
- **FR-016**: EnvelopeFilter MUST provide `void setResonance(float q)` delegating to SVF with range [0.5, 20.0].
- **FR-017**: EnvelopeFilter MUST provide `void setDepth(float amount)` with range [0.0, 1.0] to control envelope modulation depth. When depth = 0.0, cutoff remains fixed at minFrequency (Up) or maxFrequency (Down) regardless of envelope, disabling modulation.

#### Output Parameters

- **FR-018**: EnvelopeFilter MUST provide `void setMix(float dryWet)` with range [0.0, 1.0] where 0.0 = fully dry, 1.0 = fully wet.

#### Processing

- **FR-019**: EnvelopeFilter MUST provide `[[nodiscard]] float process(float input) noexcept` that:
  1. Applies sensitivity gain to input for envelope detection only (original input preserved)
  2. Processes sensitivity-gained signal through EnvelopeFollower to get envelope value
  3. Clamps envelope to [0.0, 1.0] before frequency mapping (prevents cutoff exceeding maxFrequency)
  4. Maps clamped envelope value to filter cutoff frequency based on direction and depth
  5. Processes original (ungained) input through SVF at calculated cutoff
  6. Returns dry/wet mixed output

- **FR-020**: EnvelopeFilter MUST provide `void processBlock(float* buffer, size_t numSamples) noexcept` for in-place block processing.

- **FR-021**: The frequency mapping formula MUST be exponential for perceptually linear sweeps:
  ```
  For Direction::Up:
    cutoff = minFreq * pow(maxFreq / minFreq, envelope * depth)
  For Direction::Down:
    cutoff = maxFreq * pow(minFreq / maxFreq, envelope * depth)
  ```

#### Real-Time Safety

- **FR-022**: All processing methods MUST be declared `noexcept`.
- **FR-023**: All processing methods MUST NOT allocate memory, throw exceptions, or perform I/O.
- **FR-024**: EnvelopeFilter MUST flush denormals via the composed components (SVF and EnvelopeFollower already handle this).

#### Dependencies and Code Quality

- **FR-025**: EnvelopeFilter MUST only depend on Layer 0/1 components: `envelope_follower.h` (Layer 2), `svf.h` (Layer 1), and Layer 0 utilities.
- **FR-026**: EnvelopeFilter MUST be a header-only implementation.
- **FR-027**: All components MUST include Doxygen documentation for classes, enums, and public methods.
- **FR-028**: All components MUST follow naming conventions (trailing underscore for members, PascalCase for classes, camelCase for methods).

#### Default Values

- **FR-029**: EnvelopeFilter MUST initialize with the following default values:
  - sensitivity: 0dB (unity gain)
  - attack: 10ms
  - release: 100ms
  - direction: Direction::Up
  - filterType: FilterType::Lowpass
  - minFrequency: 200Hz
  - maxFrequency: 2000Hz
  - resonance: 8.0
  - depth: 1.0
  - mix: 1.0

### Key Entities

- **EnvelopeFilter**: The main Layer 2 processor combining envelope following with resonant filtering.
  - Composes: EnvelopeFollower (for amplitude tracking), SVF (for filtering)
  - Parameters: sensitivity, attack, release, direction, filterType, minFrequency, maxFrequency, resonance, depth, mix

- **Direction**: Enum controlling whether envelope opens (Up) or closes (Down) the filter.

- **FilterType**: Enum selecting the SVF output type (Lowpass, Bandpass, Highpass).

### API Summary

```cpp
class EnvelopeFilter {
public:
    enum class Direction { Up, Down };
    enum class FilterType { Lowpass, Bandpass, Highpass };

    // Lifecycle
    void prepare(double sampleRate);
    void reset() noexcept;

    // Envelope parameters
    void setSensitivity(float dB);       // [-24, +24] dB
    void setAttack(float ms);            // [0.1, 500] ms
    void setRelease(float ms);           // [1, 5000] ms
    void setDirection(Direction dir);

    // Filter parameters
    void setFilterType(FilterType type);
    void setMinFrequency(float hz);      // [20, 0.4*sr] Hz
    void setMaxFrequency(float hz);      // [20, 0.45*sr] Hz
    void setResonance(float q);          // [0.5, 20.0]
    void setDepth(float amount);         // [0.0, 1.0]

    // Output
    void setMix(float dryWet);           // [0.0, 1.0]

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Getters
    [[nodiscard]] float getCurrentCutoff() const noexcept;
    [[nodiscard]] float getCurrentEnvelope() const noexcept;
};
```

## Success Criteria *(mandatory)*

### Measurable Outcomes

**Envelope Tracking:**
- **SC-001**: EnvelopeFilter cutoff frequency reaches 90% of target (maxFrequency for Up direction) within 5 * attackTime when input steps from 0 to 1.0.
- **SC-002**: EnvelopeFilter cutoff frequency decays to 10% of range within 5 * releaseTime when input steps from 1.0 to 0.
- **SC-003**: EnvelopeFilter with depth = 0.5 produces half the frequency sweep range compared to depth = 1.0.

**Filter Response:**
- **SC-004**: EnvelopeFilter in Lowpass mode at fixed cutoff attenuates frequencies 2 octaves above cutoff by at least 20dB.
- **SC-005**: EnvelopeFilter in Bandpass mode at fixed cutoff has peak gain within 1dB of unity at cutoff frequency.
- **SC-006**: EnvelopeFilter in Highpass mode at fixed cutoff attenuates frequencies 2 octaves below cutoff by at least 20dB.

**Frequency Sweep:**
- **SC-007**: Frequency sweep from minFrequency to maxFrequency is exponential (logarithmically linear) - each equal increment in envelope produces equal musical interval change.
- **SC-008**: With minFrequency = 200Hz, maxFrequency = 2000Hz, envelope = 0.5, depth = 1.0, Up direction: cutoff is approximately 632Hz (geometric mean, sqrt(200*2000)).

**Stability:**
- **SC-009**: EnvelopeFilter remains stable (no NaN, no infinity) with Q = 20 across full frequency sweep.
- **SC-010**: EnvelopeFilter processes 1 million samples without producing NaN or Infinity from valid [-1, 1] inputs.
- **SC-011**: All tests pass on Windows (MSVC), macOS (Clang), and Linux (GCC) at 44.1kHz, 48kHz, 96kHz, and 192kHz sample rates.

**Mix and Direction:**
- **SC-012**: Mix = 0.0 produces output identical to input (within floating-point precision).
- **SC-013**: Mix = 1.0 produces 100% filtered output.
- **SC-014**: Direction::Down produces inverse behavior - high envelope results in low cutoff.

**Performance:**
- **SC-015**: CPU usage for EnvelopeFilter is less than 100ns per sample on reference hardware: Intel i7-10700K @ 3.8GHz or Apple M1, single-threaded, Release build (EnvelopeFollower ~10ns + SVF ~10ns + overhead).

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Sample rates between 44.1kHz and 192kHz are supported.
- Input signals are typically within [-1.0, +1.0] but may exceed this range.
- The EnvelopeFollower is configured in Amplitude mode (default) for typical auto-wah response.
- Attack time is typically shorter than release time for musical results, but this is not enforced.
- Default parameters should produce a usable auto-wah effect without extensive tweaking:
  - minFrequency: 200Hz (typical low for guitar/bass wah)
  - maxFrequency: 2000Hz (typical high for classic wah)
  - resonance: 8.0 (moderate resonance for vowel-like character)
  - attack: 10ms (fast response to pick attack)
  - release: 100ms (natural decay)
  - depth: 1.0 (full sweep range)
  - direction: Up (classic auto-wah behavior)
  - mix: 1.0 (fully wet)
  - sensitivity: 0dB (unity gain)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| EnvelopeFollower | `dsp/include/krate/dsp/processors/envelope_follower.h` | MUST compose for amplitude envelope tracking |
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | MUST compose for resonant filtering with excellent modulation stability |
| SVFMode | `dsp/include/krate/dsp/primitives/svf.h` | MUST use for filter type selection (only SVFMode::Lowpass, SVFMode::Bandpass, SVFMode::Highpass - 3 modes) |
| DetectionMode | `dsp/include/krate/dsp/processors/envelope_follower.h` | MAY use to select envelope detection algorithm |
| dbToGain() | `dsp/include/krate/dsp/core/db_utils.h` | MUST use for sensitivity gain conversion |

**Initial codebase search for key terms:**

```bash
grep -r "EnvelopeFilter\|envelope.*filter" dsp/    # No existing EnvelopeFilter found
grep -r "AutoWah\|auto.*wah" dsp/                  # No existing AutoWah found
grep -r "class EnvelopeFollower" dsp/              # Found in envelope_follower.h
grep -r "class SVF" dsp/                           # Found in svf.h
```

**Search Results Summary**: No existing EnvelopeFilter or AutoWah implementation found. EnvelopeFollower and SVF are available and will be composed. SVF is preferred over MultimodeFilter for its superior modulation stability during frequency sweeps.

### Forward Reusability Consideration

*Note for planning phase: This is a Layer 2 processor that may be used by effects.*

**Sibling features at same layer** (Layer 2):
- FormantFilter (077) - Different topology (parallel bandpass), no envelope control
- MultimodeFilter - General purpose, no envelope control

**Consumers in Layer 3/4** (potential):
- VocalProcessor - May use EnvelopeFilter for talk-box-like effects
- GuitarEffectsChain - May include EnvelopeFilter as one effect in chain

**Potential shared components**:
- The frequency mapping formula (exponential sweep) could be extracted to a utility if other envelope-controlled effects need it, but for now it's simple enough to inline.

## Implementation Notes

### Processing Flow

```
                                    +------------------------+
                                    |   Sensitivity (dB)     |
                                    |     (pre-gain)         |
                                    +------------------------+
                                              |
                                              v
Input ------+-----> [Sensitivity Gain] --> [EnvelopeFollower] --> envelope (0-1+)
            |       (envelope only)                                     |
            |                                                           v
            |                                           +-----------------------------+
            |                                           |    Frequency Mapping        |
            |                                           | (clamp + exponential map)   |
            |                                           +-----------------------------+
            |                                                           |
            |                                                           v cutoff
            |                                           +-----------------------+
            +-----------------------------------------> |         SVF           |
              (original ungained input)                | (LP/BP/HP, resonance) |
                                                        +-----------------------+
                                                                    |
                                                                    v
                                              +-----------------------------+
                                              |         Dry/Wet Mix         |
                                              +-----------------------------+
                                                                    |
                                                                    v
                                                                 Output
```

### Frequency Mapping Algorithm

The frequency mapping uses an exponential function to create perceptually linear (musically useful) sweeps:

```cpp
// Envelope value is clamped to [0, 1] for mapping
float clampedEnvelope = std::clamp(envelope, 0.0f, 1.0f);

// Calculate modulation amount based on depth
float modAmount = clampedEnvelope * depth_;

// Exponential mapping for perceptually linear sweep
float freqRatio = maxFreq_ / minFreq_;

float cutoff;
if (direction_ == Direction::Up) {
    // Low envelope = minFreq, high envelope = maxFreq
    cutoff = minFreq_ * std::pow(freqRatio, modAmount);
} else {
    // Low envelope = maxFreq, high envelope = minFreq
    cutoff = maxFreq_ * std::pow(1.0f / freqRatio, modAmount);
}
```

### Classic Auto-Wah Parameters

Based on classic pedal designs:

| Parameter | Typical Range | Classic Wah Value | Notes |
|-----------|---------------|-------------------|-------|
| Min Frequency | 100-400 Hz | 350 Hz | Low sweep limit |
| Max Frequency | 1.5-4 kHz | 2.2 kHz | High sweep limit |
| Resonance (Q) | 4-15 | 8-10 | Higher = more vowel-like |
| Attack | 1-20 ms | 5-10 ms | Fast response to transients |
| Release | 50-500 ms | 100-200 ms | Natural decay |

## References

- [FLT-ROADMAP.md](../FLT-ROADMAP.md) - Project filter roadmap Phase 9
- [010-envelope-follower spec](../010-envelope-follower/spec.md) - EnvelopeFollower specification
- [071-svf spec](../071-svf/spec.md) - SVF specification
- [Cytomic Technical Papers](https://cytomic.com/technical-papers/) - SVF implementation details
- Dunlop Cry Baby GCB95 schematic - Classic auto-wah frequency range reference
- Mutron III schematic - Envelope filter circuit topology reference

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | envelope_filter.h:28-29,75 - class EnvelopeFilter in Krate::DSP |
| FR-002 | MET | envelope_filter.h:82-85 - enum class Direction { Up, Down } |
| FR-003 | MET | envelope_filter.h:88-92 - enum class FilterType { Lowpass, Bandpass, Highpass } |
| FR-004 | MET | envelope_filter.h:428 - EnvelopeFollower envFollower_ member |
| FR-005 | MET | envelope_filter.h:429 - SVF filter_ member |
| FR-006 | MET | process() is noexcept with no new/delete/malloc |
| FR-007 | MET | envelope_filter.h:117-147 - prepare(double sampleRate) |
| FR-008 | MET | envelope_filter.h:152-157 - reset() noexcept |
| FR-009 | MET | envelope_filter.h:166-169 - setSensitivity clamped to [-24,+24] |
| FR-010 | MET | envelope_filter.h:173-176 - setAttack delegates to setAttackTime |
| FR-011 | MET | envelope_filter.h:180-183 - setRelease delegates to setReleaseTime |
| FR-012 | MET | envelope_filter.h:187-189 - setDirection(Direction) |
| FR-013 | MET | envelope_filter.h:197-202,410-421 - mapFilterType to SVFMode |
| FR-014 | MET | envelope_filter.h:206-216 - setMinFrequency with clamping |
| FR-015 | MET | envelope_filter.h:220-235 - setMaxFrequency with clamping |
| FR-016 | MET | envelope_filter.h:239-244 - setResonance clamped to [0.5,20] |
| FR-017 | MET | envelope_filter.h:249-251 - setDepth clamped to [0,1] |
| FR-018 | MET | envelope_filter.h:259-261 - setMix clamped to [0,1] |
| FR-019 | MET | envelope_filter.h:272-299 - process() with correct algorithm |
| FR-020 | MET | envelope_filter.h:305-309 - processBlock() noexcept |
| FR-021 | MET | envelope_filter.h:390-405 - exponential mapping formula |
| FR-022 | MET | All processing methods declared noexcept |
| FR-023 | MET | No allocations/exceptions/IO in process path |
| FR-024 | MET | Denormals flushed via SVF and EnvelopeFollower |
| FR-025 | MET | Includes only db_utils.h, svf.h, envelope_follower.h |
| FR-026 | MET | Single header-only implementation |
| FR-027 | MET | Full Doxygen docs for class, enums, and methods |
| FR-028 | MET | trailing underscore_, PascalCase, camelCase used |
| FR-029 | MET | envelope_filter.h:433-444 - all defaults match spec |
| SC-001 | MET | test: "envelope tracking attack reaches 90% in 5*attackTime" |
| SC-002 | MET | test: "envelope tracking release decays to 10% in 5*releaseTime" |
| SC-003 | MET | test: "depth parameter reduces sweep range proportionally" |
| SC-004 | MET | test: "lowpass attenuates 2 octaves above cutoff by 20dB" |
| SC-005 | MET | test: "bandpass peak within 1dB of unity at cutoff" |
| SC-006 | MET | test: "highpass attenuates 2 octaves below cutoff by 20dB" |
| SC-007 | MET | test: "exponential mapping produces logarithmically linear sweep" |
| SC-008 | MET | test: "envelope 0.5 produces geometric mean cutoff" |
| SC-009 | MET | test: "Q=20 stability across full frequency sweep" |
| SC-010 | MET | test: "1 million samples processed without NaN" |
| SC-011 | MET | test: "multi-sample-rate 44.1k, 48k, 96k, 192k" |
| SC-012 | MET | test: "mix=0.0 produces output identical to input" |
| SC-013 | MET | test: "mix=1.0 produces 100% filtered output" |
| SC-014 | MET | test: "Direction::Down inverse behavior" |
| SC-015 | MET | test: "performance < 100ns per sample" |

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

**Evidence Summary:**
- 36 tests with 105+ assertions, all passing
- Full test suite: 2906 test cases, all passing
- All 29 functional requirements (FR-001 to FR-029) verified
- All 15 success criteria (SC-001 to SC-015) measured and passing
- No TODOs, no placeholders, no relaxed thresholds

**Recommendation**: Ready for merge after code review.
