# Feature Specification: Karplus-Strong String Synthesizer

**Feature Branch**: `084-karplus-strong`
**Created**: 2026-01-22
**Status**: Draft
**Input**: User description: "Karplus-Strong String - Classic plucked string synthesis using filtered delay line feedback. Phase 13.2 of filter roadmap (Physical Modeling Resonators)."

## Clarifications

### Session 2026-01-22

- Q: Fractional delay interpolation method? → A: Allpass interpolation (standard KS, best pitch accuracy)
- Q: Brightness filter implementation? → A: Two-pole lowpass filter (12dB/oct slope, smoother response)
- Q: Pick position (comb filtering) implementation? → A: Delay line tap during excitation (physically accurate, read from delayed position)
- Q: Damping parameter mapping to filter cutoff? → A: Cutoff relative to fundamental frequency (cutoff = fundamental × multiplier, consistent feel across pitch range)
- Q: Re-pluck behavior during active excitation? → A: Add with normalization (additive behavior preserved, scaled down if sum would exceed ±1.0 to prevent clipping)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Plucked String Sound (Priority: P1)

A sound designer wants to create realistic plucked string sounds (guitar, harp, harpsichord) by exciting a Karplus-Strong string model with a noise burst and controlling the decay and tone through damping parameters.

**Why this priority**: This is the core functionality of Karplus-Strong synthesis - without pluck excitation and damping control, the component has no value. This delivers the classic KS sound immediately.

**Independent Test**: Can be fully tested by calling pluck(), processing samples, and verifying pitch accuracy and exponential decay envelope. Delivers value as a standalone plucked string synthesizer.

**Acceptance Scenarios**:

1. **Given** a prepared KarplusStrong instance at 44100Hz, **When** setFrequency(440.0f) and pluck(1.0f) are called, **Then** the output produces a pitched sound at 440Hz (within 1 cent accuracy) that decays naturally.
2. **Given** a plucked string with setDamping(0.5f), **When** compared to setDamping(0.1f), **Then** the high-damping string sounds darker (less high-frequency content) and decays faster.
3. **Given** a plucked string, **When** processing continues for the decay time, **Then** the amplitude decays exponentially toward zero without DC offset accumulation.

---

### User Story 2 - Tone Shaping with Brightness and Pick Position (Priority: P2)

A musician wants to shape the timbre of the plucked string by adjusting the excitation brightness (controlling the initial noise burst spectrum) and simulating different pick positions along the string to affect the harmonic content.

**Why this priority**: These parameters are essential for creating varied string sounds (bright metallic vs warm nylon, bridge vs neck pickup). Builds on P1 by adding expressive control without changing core architecture.

**Independent Test**: Can be tested by comparing spectral analysis of different brightness and pick position settings. Delivers value by enabling diverse string timbres.

**Acceptance Scenarios**:

1. **Given** setBrightness(1.0f), **When** pluck() is called, **Then** the excitation contains full-spectrum noise resulting in bright, metallic tone.
2. **Given** setBrightness(0.2f), **When** pluck() is called, **Then** the excitation is low-pass filtered resulting in a warmer, softer tone.
3. **Given** setPickPosition(0.5f) (middle of string), **When** pluck() is called, **Then** the fundamental and odd harmonics are emphasized (even harmonics attenuated).
4. **Given** setPickPosition(0.1f) (near bridge), **When** pluck() is called, **Then** more harmonics are present creating a brighter, thinner sound.

---

### User Story 3 - Continuous Excitation (Bowing) (Priority: P3)

A synthesist wants to create sustained string sounds similar to bowed instruments by continuously exciting the string model with controlled pressure, enabling infinite sustain effects.

**Why this priority**: Bowing extends the use case beyond plucked strings to sustained sounds (violin, cello-like textures). Adds a new excitation mode without modifying the core resonator.

**Independent Test**: Can be tested by calling bow() with pressure and verifying sustained oscillation that continues indefinitely while bow() is active.

**Acceptance Scenarios**:

1. **Given** bow(0.5f) is called continuously, **When** processing continues, **Then** the string produces sustained oscillation that does not decay.
2. **Given** bow() with varying pressure values, **When** pressure increases, **Then** the output amplitude increases proportionally.
3. **Given** an actively bowed string, **When** bow(0.0f) is called, **Then** the string begins to decay naturally as if released.

---

### User Story 4 - Custom Excitation Signal (Priority: P4)

A sound designer wants to inject custom excitation signals (samples, wavetables, or processed audio) into the string model to create unique hybrid sounds that blend synthesis with sampled content.

**Why this priority**: Extends the synthesizer to creative applications beyond traditional plucked sounds. Lower priority because basic pluck/bow covers most use cases.

**Independent Test**: Can be tested by providing a custom excitation buffer and verifying the string resonates at the set frequency with the excitation's timbral character.

**Acceptance Scenarios**:

1. **Given** a custom 100-sample excitation buffer containing a sine wave burst, **When** excite() is called, **Then** the string resonates with a purer, more tonal quality than noise excitation.
2. **Given** external audio fed into process(input), **When** input signal is present, **Then** the string resonates sympathetically at its tuned frequency.

---

### User Story 5 - Inharmonicity Control (Stretch Tuning) (Priority: P5)

A synthesist wants to add piano-like inharmonicity to the string model by introducing dispersion that causes higher harmonics to be slightly sharp, creating a more realistic acoustic piano or bell-like character.

**Why this priority**: Inharmonicity is an advanced feature for realistic piano modeling. Most string sounds work fine without it. Lower priority as it adds complexity.

**Independent Test**: Can be tested by measuring the frequency of upper partials relative to the fundamental with stretch enabled vs disabled.

**Acceptance Scenarios**:

1. **Given** setStretch(0.0f), **When** spectral analysis is performed, **Then** harmonics are perfectly integer multiples of the fundamental.
2. **Given** setStretch(0.5f), **When** spectral analysis is performed, **Then** upper harmonics are progressively sharper than integer multiples (piano-like).
3. **Given** high stretch values, **When** pluck() is called, **Then** the timbre becomes more bell-like or metallic.

---

### Edge Cases

- What happens when frequency is set below 20Hz or above Nyquist? Frequency is clamped to valid range [minFrequency, Nyquist/2].
- What happens when pluck() is called during active excitation? New pluck is added to existing excitation buffer content; if sum exceeds ±1.0, the buffer is normalized to prevent clipping while preserving additive behavior.
- What happens when decay is set to very short times (<10ms)? String produces brief transient, minimum decay time enforced.
- What happens when decay is set to very long times (>30s)? Feedback coefficient clamped to prevent instability (<0.9999).
- How is DC offset prevented in the feedback loop? DC blocker in feedback path prevents accumulation.
- What happens at extremely low frequencies where delay line approaches maximum? Graceful degradation with maximum delay line size.

## Requirements *(mandatory)*

### Functional Requirements

**Core Algorithm**

- **FR-001**: System MUST implement the Karplus-Strong algorithm using a delay line with filtered feedback
- **FR-002**: System MUST calculate delay length from frequency as `delaySamples = sampleRate / frequency`
- **FR-003**: System MUST use allpass fractional delay interpolation for accurate tuning across all frequencies
- **FR-004**: System MUST support frequency range from 20Hz to half the Nyquist frequency (minFrequency parameter in prepare())

**Excitation Methods**

- **FR-005**: System MUST implement pluck(velocity) that fills the delay line with filtered noise burst
- **FR-006**: Pluck velocity MUST scale the amplitude of the excitation signal (0.0-1.0 range)
- **FR-007**: System MUST implement bow(pressure) for continuous excitation mode
- **FR-008**: Bow pressure MUST control the amplitude of continuous noise injection (0.0-1.0 range)
- **FR-009**: System MUST implement excite(signal, length) to inject custom excitation buffers
- **FR-010**: System MUST accept external excitation through the process(input) parameter

**Damping and Tone Control**

- **FR-011**: System MUST implement setDamping(amount) to control high-frequency loss per cycle (0.0-1.0 range)
- **FR-012**: Damping MUST be implemented as a one-pole lowpass filter in the feedback loop with cutoff frequency calculated relative to the fundamental frequency (cutoff = fundamental × multiplier based on damping parameter)
- **FR-013**: System MUST implement setBrightness(amount) to control excitation spectrum (0.0-1.0 range)
- **FR-014**: Brightness MUST be implemented as a two-pole lowpass filter (12dB/oct) applied to the noise burst before injection into the delay line
- **FR-015**: System MUST implement setPickPosition(position) to simulate pluck location (0.0-1.0 range)
- **FR-016**: Pick position MUST be implemented using delay line tap reading during excitation fill, reading from position × delayLength offset to create physically accurate comb filtering that attenuates harmonics at position-related frequencies

**Decay Control**

- **FR-017**: System MUST implement setDecay(seconds) to control the overall decay time (RT60)
- **FR-018**: Decay time MUST be converted to feedback coefficient using: `feedback = 10^(-3 * delaySamples / (decayTime * sampleRate))`
- **FR-019**: Feedback coefficient MUST be clamped to ensure stability (maximum 0.9999)

**Inharmonicity**

- **FR-020**: System MUST implement setStretch(amount) to add inharmonicity (0.0-1.0 range)
- **FR-021**: Stretch MUST be implemented using an allpass filter in the feedback loop to create dispersion

**Lifecycle**

- **FR-022**: System MUST implement prepare(sampleRate, maxFrequency) to initialize the delay line
- **FR-023**: Maximum delay line size MUST be calculated from maxFrequency (default 20Hz minimum)
- **FR-024**: System MUST implement reset() to clear all state without reallocation
- **FR-025**: Process method MUST return input unchanged if prepare() has not been called

**Real-Time Safety**

- **FR-026**: All process methods MUST be noexcept
- **FR-027**: No memory allocation MUST occur in process(), pluck(), bow(), or excite()
- **FR-028**: System MUST flush denormals in the feedback loop to prevent CPU spikes
- **FR-029**: System MUST include DC blocking in the feedback path to prevent DC accumulation

**Edge Case Handling**

- **FR-030**: NaN or Inf input MUST cause reset and return 0.0f
- **FR-031**: Frequency values outside valid range MUST be clamped to [minFrequency, sampleRate/2 * 0.99]
- **FR-032**: All parameters MUST be clamped to their valid ranges
- **FR-033**: When pluck() is called during active excitation, the new excitation MUST be added to existing buffer content; if the resulting sum exceeds ±1.0, the buffer MUST be normalized to prevent clipping

### Key Entities

- **KarplusStrong**: Main class implementing the string synthesizer
- **DelayLine**: Reusable primitive providing the circular buffer with allpass fractional interpolation
- **OnePoleLP**: Reusable primitive providing the damping lowpass filter
- **TwoPoleLP**: Reusable or new primitive providing the 12dB/oct brightness lowpass filter for excitation
- **Allpass1Pole**: Reusable primitive for inharmonicity/dispersion (stretch parameter)
- **DCBlocker2**: Reusable primitive for DC offset prevention in feedback loop
- **NoiseGenerator**: Reusable processor for excitation signal generation

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Pitch accuracy within 1 cent across the entire frequency range (20Hz to 10kHz)
- **SC-002**: Users can produce recognizable plucked string sounds (guitar, harp, harpsichord) within 30 seconds of first use
- **SC-003**: Decay time accuracy within 10% of specified value (measured as RT60)
- **SC-004**: Processing uses less than 0.5% CPU per voice at 44.1kHz sample rate on reference hardware
- **SC-005**: Output remains stable (no runaway feedback, DC offset, or denormal slowdown) after 10 minutes of continuous operation
- **SC-006**: Frequency changes produce audible pitch changes within 1ms of parameter update
- **SC-007**: Component integrates with existing modulation systems (can be driven by LFO, envelope, MIDI)
- **SC-008**: All parameters respond smoothly without audible clicks or discontinuities
- **SC-009**: Bowing mode sustains indefinitely without amplitude drift or instability
- **SC-010**: Stretch parameter produces audible inharmonicity at values above 0.3

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Sample rates supported: 44100Hz to 192000Hz
- Maximum delay line size calculated for 20Hz at maximum sample rate (192000/20 = 9600 samples)
- Users have basic understanding of plucked string instrument characteristics
- Integration will be as a Layer 2 processor in the DSP architecture
- White noise is the default excitation type (other noise types available via NoiseGenerator)
- DelayLine primitive will be used with allpass interpolation mode for fractional delay
- Two-pole lowpass filter (12dB/oct) will be used for brightness control (may require new TwoPoleLP primitive if not already available)
- Damping filter cutoff will be calculated relative to fundamental frequency to maintain consistent behavior across pitch range

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `DelayLine` | `primitives/delay_line.h` | **REUSE** - Provides circular buffer with allpass interpolation for fractional delay (confirmed use of allpass mode) |
| `OnePoleLP` | `primitives/one_pole.h` | **REUSE** - Provides 6dB/oct lowpass for damping filter in feedback loop |
| `Allpass1Pole` | `primitives/allpass_1pole.h` | **REUSE** - Provides phase dispersion for inharmonicity (stretch parameter) |
| `DCBlocker2` | `primitives/dc_blocker.h` | **REUSE** - Provides 2nd-order DC blocking for feedback loop stability |
| `NoiseGenerator` | `processors/noise_generator.h` | **REUSE** - Provides white/pink noise for excitation (13 noise types available) |
| `TwoPoleLP` | `primitives/` (if exists) | **REUSE or CREATE** - Need 12dB/oct lowpass for brightness control |
| `FeedbackComb` | `primitives/comb_filter.h` | **REFERENCE** - Similar architecture but KS needs custom feedback topology |
| `Xorshift32` | `core/random.h` | **REUSE** - Real-time safe PRNG for inline noise generation if simpler approach needed |

**Initial codebase search for key terms:**

```bash
grep -r "KarplusStrong\|karplus\|pluck\|string synthesis" dsp/ plugins/
```

**Search Results Summary**: No existing KarplusStrong implementation found. `FeedbackComb` in `comb_filter.h` uses similar delay-with-feedback architecture but lacks the specific excitation methods and inharmonicity features required.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- `WaveguideResonator` (Phase 13.3) - bidirectional waveguide for pipe/flute sounds
- `ModalResonator` (Phase 13.4) - modal synthesis for bells/percussion
- `ResonatorBank` (Phase 13.1) - tuned resonator bank (already specified)

**Potential shared components** (preliminary, refined in plan.md):
- Excitation generation patterns (pluck, bow) could be extracted to shared utility
- Decay-to-feedback coefficient conversion formula could be shared
- Pick position delay line tap pattern could be shared with waveguide models
- Allpass dispersion pattern is directly reusable for waveguide dispersion
- Two-pole lowpass filter for brightness control could be reused in other physical models
- Normalization logic for additive excitation could be shared

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is not met without explicit user approval.*

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
| FR-028 | | |
| FR-029 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
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
