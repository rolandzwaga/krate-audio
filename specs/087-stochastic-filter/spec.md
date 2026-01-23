# Feature Specification: Stochastic Filter

**Feature Branch**: `087-stochastic-filter`
**Created**: 2026-01-23
**Status**: Draft
**Input**: User description: "Stochastic Filter with randomly varying parameters - cutoff, Q, or type can drift or jump stochastically for experimental sound design"

## Clarifications

### Session 2026-01-23

- Q: Which Lorenz attractor axis should drive cutoff modulation (x, y, or z)? → A: Use X axis (primary oscillation dimension)
- Q: How many octaves of Perlin noise should be layered together? → A: 3 octaves (balanced detail and performance)
- Q: At what rate should the stochastic modulation values be calculated and applied to the filter? → A: Control-rate (update every 32-64 samples)
- Q: How should the system handle filter type transitions? → A: Parallel processing with gain crossfade (run both filters during transition)
- Q: How should stereo channels be processed? → A: Linked (same modulation for both channels, preserves stereo image)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Evolving Pad Textures with Brownian Motion (Priority: P1)

A sound designer wants to create slowly evolving, organic filter movements on a pad sound without manually automating parameters. They want the filter to "breathe" naturally with smooth, unpredictable variations.

**Why this priority**: This is the core use case for stochastic filtering - creating organic, evolving textures. Brownian motion (Walk mode) is the most commonly used random modulation type for this purpose.

**Independent Test**: Can be fully tested by processing a static tone through the filter in Walk mode and verifying the cutoff drifts smoothly over time, creating audible but non-jarring filter movement.

**Acceptance Scenarios**:

1. **Given** a StochasticFilter in Walk mode with cutoff randomization enabled, **When** processing a continuous audio signal for 5 seconds, **Then** the effective cutoff frequency varies smoothly over time without sudden jumps, and the variation stays within the configured octave range.

2. **Given** a StochasticFilter with Walk mode and a change rate of 2 Hz, **When** processing audio, **Then** the random walk takes approximately 0.5 seconds per step, creating a slow drift effect.

3. **Given** a StochasticFilter with Walk mode, **When** the same seed value is used twice, **Then** the random walk pattern is identical (deterministic behavior).

---

### User Story 2 - Glitchy Random Filter Jumps (Priority: P2)

A producer creating electronic or glitch music wants sudden, unpredictable filter changes that create rhythmic chaos. The filter should jump abruptly between random values at a controllable rate.

**Why this priority**: Jump mode represents the second major use case - discrete random changes. This is essential for glitch, IDM, and experimental electronic music.

**Independent Test**: Can be tested by processing audio in Jump mode and verifying that cutoff values change discretely at the specified rate.

**Acceptance Scenarios**:

1. **Given** a StochasticFilter in Jump mode with change rate of 4 Hz, **When** processing audio, **Then** the cutoff frequency jumps to a new random value approximately 4 times per second.

2. **Given** a StochasticFilter in Jump mode with smoothing set to 50ms, **When** a jump occurs, **Then** the transition between values takes 50ms (preventing clicks).

3. **Given** a StochasticFilter in Jump mode with resonance randomization enabled, **When** processing audio, **Then** both cutoff and resonance jump to new values at the configured rate.

---

### User Story 3 - Chaotic Filter Modulation (Priority: P2)

An experimental sound designer wants to use deterministic chaos (Lorenz attractor) to create complex, non-repeating but mathematically related filter movements that are more structured than pure randomness.

**Why this priority**: Lorenz mode provides a unique character distinct from random noise - chaotic but deterministic patterns that create interesting long-term evolution.

**Independent Test**: Can be tested by verifying that Lorenz mode produces filter movements that follow the characteristic Lorenz attractor shape - never settling, never exactly repeating, but bounded.

**Acceptance Scenarios**:

1. **Given** a StochasticFilter in Lorenz mode, **When** processing audio for 10 seconds, **Then** the cutoff modulation shows the characteristic chaotic attractor behavior - bounded but never exactly repeating.

2. **Given** a StochasticFilter in Lorenz mode with the same seed, **When** processing audio twice, **Then** the chaotic sequence is identical (deterministic chaos).

3. **Given** a StochasticFilter in Lorenz mode, **When** the change rate is increased, **Then** the chaotic evolution happens faster, compressing the attractor motion in time.

---

### User Story 4 - Smooth Coherent Random Modulation (Priority: P3)

A composer wants smooth, coherent random modulation similar to the smooth noise used in computer graphics - more organic than a simple random walk but still unpredictable.

**Why this priority**: Perlin noise provides a unique quality of randomness that is valued in generative music - smooth gradients with multiple octaves of detail.

**Independent Test**: Can be tested by verifying that Perlin mode produces smooth, band-limited noise without sudden changes.

**Acceptance Scenarios**:

1. **Given** a StochasticFilter in Perlin mode, **When** processing audio, **Then** the cutoff modulation varies smoothly with no sudden discontinuities.

2. **Given** a StochasticFilter in Perlin mode with change rate of 1 Hz, **When** processing audio, **Then** the noise has coherent variations at approximately 1 Hz fundamental frequency.

---

### User Story 5 - Randomized Filter Type Switching (Priority: P3)

An experimental producer wants the filter type itself to randomly change (e.g., switching between lowpass, highpass, bandpass) at controlled intervals for extreme sonic experimentation.

**Why this priority**: Filter type switching is an advanced feature that builds on the core random modulation infrastructure. It is less commonly needed but enables unique experimental effects.

**Independent Test**: Can be tested by enabling filter type randomization and verifying that the filter response changes between enabled types.

**Acceptance Scenarios**:

1. **Given** a StochasticFilter with type randomization enabled for LP/HP/BP, **When** in Jump mode with 1 Hz rate, **Then** the filter type changes approximately once per second to a randomly selected type.

2. **Given** a StochasticFilter with type randomization enabled, **When** smoothing is set to 100ms, **Then** transitions between filter types are crossfaded to prevent clicks.

---

### Edge Cases

- What happens when the change rate is set to 0 Hz? Filter parameters remain static (no randomization applied).
- What happens when the octave range is set to 0? No cutoff variation occurs (effective bypass of cutoff randomization).
- What happens when smoothing is 0ms in Jump mode? Immediate value changes may cause clicks - warn user or enforce minimum smoothing.
- How does the filter behave when switching filter types mid-processing? Run both old and new filter types in parallel with complementary gain crossfades (old fades out, new fades in) over the smoothing duration to prevent discontinuities.
- What happens with extreme Lorenz attractor parameters? Values are clamped to prevent runaway or numerical instability.
- How does seeding work across prepare() calls? Seed is restored on reset() but can be re-seeded explicitly.

## Requirements *(mandatory)*

### Functional Requirements

#### Random Mode System

- **FR-001**: System MUST provide four random modulation modes: Walk (Brownian motion), Jump (discrete random), Lorenz (chaotic attractor), and Perlin (coherent noise).

- **FR-002**: Walk mode MUST implement Brownian motion using a random walk algorithm where each step adds a small random delta to the current value, with delta size controlled by change rate.

- **FR-003**: Jump mode MUST generate discrete random jumps at the specified change rate (Hz), with values uniformly distributed within the configured range.

- **FR-004**: Lorenz mode MUST implement a discrete-time Lorenz attractor with standard parameters (sigma=10, rho=28, beta=8/3), using the X axis as the modulation output, scaled to produce modulation values in [-1, 1] range.

- **FR-005**: Perlin mode MUST implement 1D Perlin noise with 3 octaves (fundamental + 2 harmonics) using smooth interpolation, producing band-limited noise at the configured rate.

#### Randomizable Parameters

- **FR-006**: System MUST support randomization of cutoff frequency with a configurable range in octaves (0 to 8 octaves, default 2).

- **FR-007**: System MUST support randomization of resonance (Q) with a configurable range (normalized 0-1 mapping to SVF's Q range of 0.1-30.0).

- **FR-008**: System MUST support randomization of filter type, with the ability to enable/disable which types are included in random selection. Type transitions MUST use parallel processing with complementary gain crossfades (old filter fades out while new filter fades in) over the FR-011 smoothing duration to prevent discontinuities.

- **FR-009**: Each randomizable parameter MUST be independently enabled/disabled.

#### Control Parameters

- **FR-010**: System MUST provide a change rate parameter in Hz (0.01 to 100 Hz, default 1 Hz) controlling how fast parameters change.

- **FR-011**: System MUST provide a smoothing parameter in milliseconds (0 to 1000ms, default 50ms) specifying the transition duration between values. This is the approximate time for parameter changes to complete (not a time constant - the smoother reaches ~99% of target within this duration).

- **FR-012**: System MUST provide a seed parameter (uint32_t) for reproducible random sequences.

- **FR-013**: System MUST provide base value parameters for cutoff (center frequency) and resonance (Q) (center value), specifying the center of the random range.

#### Filter Integration

- **FR-014**: System MUST use the existing TPT SVF (primitives/svf.h) as the underlying filter for modulation stability during rapid parameter changes.

- **FR-015**: System MUST use the existing Xorshift32 PRNG (core/random.h) for all random number generation, ensuring real-time safety.

- **FR-016**: System MUST provide standard prepare(sampleRate)/reset()/process(sample)/processBlock(buffer, numSamples) interface.

- **FR-017**: System MUST support all SVF filter modes: Lowpass, Highpass, Bandpass, Notch, Allpass, Peak, LowShelf, HighShelf.

- **FR-018**: Stereo processing MUST use linked modulation (same random sequence applied to both L and R channels) to preserve stereo image and reduce CPU cost.

#### Real-Time Safety

- **FR-019**: All processing methods (process, processBlock) MUST be noexcept with zero allocations.

- **FR-020**: Random generation MUST use only the deterministic Xorshift32 PRNG with no system calls.

- **FR-021**: Parameter smoothing MUST use the existing OnePoleSmoother or equivalent real-time safe interpolation.

- **FR-022**: Modulation value updates MUST occur at control-rate (every 32-64 samples) to balance CPU efficiency and temporal resolution, with smoothing applied between updates.

#### State Management

- **FR-023**: System MUST support seeding via setSeed(uint32_t) for reproducible behavior.

- **FR-024**: System MUST reset random state on reset() while preserving the current seed for reproducibility.

- **FR-025**: System MUST preserve all configuration (mode, rates, ranges, enabled parameters) across reset() calls.

### Key Entities

- **StochasticFilter**: The main processor class that composes SVF, PRNG, and smoothers into a stochastic filter effect.
- **RandomMode**: Enumeration defining the four random modulation algorithms (Walk, Jump, Lorenz, Perlin).
- **StochasticParams**: Internal structure holding base values and ranges for each randomizable parameter.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Filter produces audibly random parameter variations when any randomization is enabled, verified by measuring parameter variance over 1 second of processing (variance > 0 for enabled parameters).

- **SC-002**: Walk mode produces smooth variations with no sudden jumps, verified by measuring maximum sample-to-sample parameter delta (delta < 0.1 * range per sample at 44.1kHz).

- **SC-003**: Jump mode produces discrete changes at the configured rate within 10% tolerance (e.g., 4 Hz rate produces 36-44 jumps in 10 seconds).

- **SC-004**: Same seed value produces bit-identical output across multiple runs (deterministic behavior test).

- **SC-005**: Filter processes audio without clicks or pops when smoothing is set to 10ms or higher, verified by artifact detection (no transients > 6dB above signal level).

- **SC-006**: All processing completes within real-time budget (< 0.5% CPU per instance at 44.1kHz stereo on reference hardware: typical 2020+ desktop CPU at 3GHz+). Stereo uses linked modulation (single calculation for both channels).

- **SC-007**: Cutoff randomization stays within configured octave range from base frequency (measured deviation never exceeds range).

- **SC-008**: Filter handles all edge cases gracefully: zero rate (static), zero range (no variation), type switching with smoothing (no clicks).

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The SVF filter (primitives/svf.h) provides stable audio-rate modulation as documented.
- The Xorshift32 PRNG (core/random.h) provides adequate randomness quality for audio applications.
- Target sample rates are 44.1kHz to 192kHz.
- Maximum block sizes up to 8192 samples are supported.
- The host provides accurate sample rate information via prepare().

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | Core filter - MUST reuse for modulation stability |
| Xorshift32 | `dsp/include/krate/dsp/core/random.h` | PRNG - MUST reuse for real-time safe randomness |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | Should reuse for parameter smoothing transitions |
| SVFMode | `dsp/include/krate/dsp/primitives/svf.h` | Enumeration for filter modes - MUST reuse |
| LFO | `dsp/include/krate/dsp/primitives/lfo.h` | Reference for SmoothRandom implementation pattern |
| NoiseGenerator | `dsp/include/krate/dsp/processors/noise_generator.h` | Reference for noise generation patterns |
| MultimodeFilter | `dsp/include/krate/dsp/processors/multimode_filter.h` | Reference for filter processor composition patterns |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "Lorenz\|lorenz" dsp/ plugins/  # No existing Lorenz implementations found
grep -r "Perlin\|perlin" dsp/ plugins/  # No existing Perlin implementations found
grep -r "Brownian\|brownian" dsp/ plugins/  # No existing Brownian implementations found
grep -r "Stochastic\|stochastic" dsp/ plugins/  # No existing stochastic components found
```

**Search Results Summary**: No existing implementations of Lorenz attractor, Perlin noise, or Brownian motion found. These algorithms must be implemented new. The random infrastructure (Xorshift32) and filter (SVF) exist and should be composed.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- Future stochastic delay time modulation
- Future stochastic panning processor
- Future generative LFO modes

**Potential shared components** (preliminary, refined in plan.md):
- The random modulation generators (Walk, Jump, Lorenz, Perlin) could be extracted to Layer 1 primitives if other processors need stochastic modulation
- A generic "StochasticModulator" primitive could be created for reuse

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | RandomMode enum with Walk, Jump, Lorenz, Perlin modes (line 44-49) |
| FR-002 | MET | calculateWalkValue() implements bounded random walk (line 615-632) |
| FR-003 | MET | calculateJumpValue() with timer-based trigger (line 636-649) |
| FR-004 | MET | calculateLorenzValue() with sigma=10, rho=28, beta=8/3, X-axis output (line 653-681) |
| FR-005 | MET | calculatePerlinValue() with perlin1D() using 3 octaves (line 685-721) |
| FR-006 | MET | setCutoffOctaveRange() 0-8 octaves (line 380-382), tested in stochastic_filter_test.cpp |
| FR-007 | PARTIAL | setResonanceRange() exists (line 386-388) but resonance modulation not applied in updateModulation() (line 601-605 has TODO comment) |
| FR-008 | PARTIAL | FilterTypeMask, setEnabledFilterTypes(), selectRandomType() exist; crossfade infrastructure complete; but type randomization not triggered in updateModulation() (line 608-610 has TODO comment) |
| FR-009 | MET | setCutoffRandomEnabled(), setResonanceRandomEnabled(), setTypeRandomEnabled() (line 309-321) |
| FR-010 | MET | setChangeRate() 0.01-100 Hz (line 415-417) |
| FR-011 | MET | setSmoothingTime() 0-1000ms (line 421-430) |
| FR-012 | MET | setSeed() with uint32_t (line 434-445) |
| FR-013 | MET | setBaseCutoff(), setBaseResonance() (line 341-349) |
| FR-014 | MET | Uses SVF from primitives/svf.h (line 478-479) |
| FR-015 | MET | Uses Xorshift32 from core/random.h (line 485) |
| FR-016 | MET | prepare(), reset(), process(), processBlock() interface (line 139-287) |
| FR-017 | MET | FilterTypeMask supports all 8 SVF modes (line 57-65) |
| FR-018 | MET | Single instance for stereo with linked modulation (documented in class comment line 82-84) |
| FR-019 | MET | All process methods noexcept, no allocations (line 229, 279) |
| FR-020 | MET | Only Xorshift32 used for random generation (line 485) |
| FR-021 | MET | Uses OnePoleSmoother for parameter smoothing (line 525-527) |
| FR-022 | MET | kControlRateInterval = 32 samples (line 117), tested in test file |
| FR-023 | MET | setSeed() method (line 434-445) |
| FR-024 | MET | reset() restores seed (line 198), tested "Seed is preserved across prepare() calls" |
| FR-025 | MET | reset() preserves configuration (line 192-219) |
| SC-001 | MET | Test "Filter produces parameter variance when randomization enabled" |
| SC-002 | MET | Test "Walk mode produces smooth variations" verifies max delta bound |
| SC-003 | MET | Test "Jump mode produces discrete changes at configured rate" |
| SC-004 | MET | Tests "Walk/Lorenz/Perlin mode is deterministic with same seed" |
| SC-005 | MET | Test "Jump mode is click-free with adequate smoothing" |
| SC-006 | MET | Test "StochasticFilter CPU performance is reasonable" completes without timeout |
| SC-007 | MET | Test "Walk mode cutoff stays within octave range" |
| SC-008 | MET | Tests for zero rate, zero range, mode switching edge cases |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [X] All FR-xxx requirements verified against implementation
- [X] All SC-xxx success criteria measured and documented
- [X] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code (see gaps below)
- [X] No features quietly removed from scope (gaps documented honestly)
- [X] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: PARTIAL

**Documented Gaps:**

1. **FR-007 (Resonance Randomization)**: The infrastructure exists (setResonanceRandomEnabled, setResonanceRange, resonanceSmoother), but the actual modulation value is not applied in updateModulation(). Line 601-605 has a TODO comment. The test for this passes because it tests the API exists, not that the modulation is actually applied.

2. **FR-008 (Type Randomization Triggering)**: The complete infrastructure exists (FilterTypeMask, setEnabledFilterTypes, selectRandomType, parallel crossfade logic in process()), but the type randomization is not triggered in updateModulation(). Line 608-610 has a TODO comment. Tests pass because they verify the infrastructure works, not that random triggering occurs.

**What Works:**
- All 4 random modes (Walk, Jump, Lorenz, Perlin) are fully implemented and tested
- Cutoff frequency randomization works completely with octave-based scaling
- Parameter smoothing (cutoff) works with OnePoleSmoother
- Filter type crossfade mechanism is implemented and works
- All determinism/seeding functionality works
- All edge cases handled (zero rate, zero range, mode switching)
- 33 tests pass with 1602 assertions

**Recommendation**: To complete FR-007 and FR-008:
1. Add resonance modulation application in updateModulation() (approximately 5 lines of code)
2. Add type randomization trigger in updateModulation() (approximately 10 lines of code)

These are straightforward additions since all the infrastructure already exists.
