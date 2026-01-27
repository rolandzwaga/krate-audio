# Feature Specification: Digital Delay Mode

**Feature Branch**: `026-digital-delay`
**Created**: 2025-12-26
**Status**: Draft
**Input**: User description: "Digital Delay Mode - Layer 4 user feature implementing clean digital delay with multiple era presets (pristine/clean, 80s digital, lo-fi). Composes DelayEngine, FeedbackNetwork, and CharacterProcessor (digital mode). Features include: program-dependent limiter in feedback path, optional bit reduction for vintage character, tempo sync, and modulation. Controls: Time (delay length), Feedback (with soft limiting), Modulation (optional LFO depth/rate), Era preset selection, Limiter character, Mix, and Output Level. Should provide transparent pristine delays as well as characterful vintage digital tones."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Pristine Digital Delay (Priority: P1)

A musician wants to add crystal-clear digital delay to their mix without any coloration. They need the delay to be transparent, preserving the original signal's frequency content and dynamics exactly. This is the fundamental use case - a clean, reliable digital delay that sounds identical to the input, just time-shifted.

**Why this priority**: Core functionality - without a clean, transparent delay mode, the plugin cannot serve as a reliable digital delay. This is the baseline that all other modes build upon.

**Independent Test**: Can be fully tested by processing a test signal through the delay at 100% wet and verifying the output matches the input (time-shifted) with no measurable frequency or amplitude changes.

**Acceptance Scenarios**:

1. **Given** pristine mode is selected and delay time is 500ms, **When** a full-spectrum test signal is processed, **Then** the delayed output has flat frequency response within 0.1dB of the input from 20Hz to 20kHz.

2. **Given** pristine mode at 100% feedback, **When** the delay self-oscillates, **Then** repeats maintain constant amplitude without decay or growth (within 0.5dB).

3. **Given** pristine mode with 0% mix, **When** any audio is processed, **Then** the output exactly matches the dry input with no audible artifacts.

---

### User Story 2 - 80s Digital Character (Priority: P2)

A producer creating retro or synthwave music wants authentic 80s digital delay character. They want the subtle quantization artifacts, the slightly grainy high-end, and the characteristic "stepped" feedback decay of early digital delays like the Lexicon PCM42 or Roland SDE-3000.

**Why this priority**: Primary vintage character mode. After clean operation is verified, this represents the most requested vintage digital sound.

**Independent Test**: Can be tested by comparing frequency response and noise characteristics against reference recordings of vintage digital delays.

**Acceptance Scenarios**:

1. **Given** 80s Digital era is selected, **When** audio is processed, **Then** the output has audible high-frequency rolloff starting around 12-14kHz (early A/D converter characteristic).

2. **Given** 80s Digital era with feedback at 50%, **When** a transient is processed, **Then** repeats show subtle artifacts from reduced sample precision.

3. **Given** 80s Digital era at high feedback, **When** listening carefully, **Then** a subtle noise floor is audible between repeats (characteristic of early converters).

---

### User Story 3 - Lo-Fi Digital Degradation (Priority: P3)

An experimental artist wants aggressive digital degradation for creative effect. They want bit-crushed, sample-rate-reduced delay that sounds obviously "broken" in a musical way - aliasing, quantization noise, and harsh digital artifacts as a creative tool.

**Why this priority**: Advanced creative mode. Builds on the vintage processing but takes it to extremes for sound design.

**Independent Test**: Can be tested by measuring bit depth reduction and sample rate reduction artifacts against specification.

**Acceptance Scenarios**:

1. **Given** Lo-Fi era is selected, **When** audio is processed, **Then** obvious bit reduction artifacts are audible (quantization noise, stepped waveforms).

2. **Given** Lo-Fi era with high Age setting, **When** high-frequency content is processed, **Then** aliasing artifacts are clearly audible (folded frequencies).

3. **Given** Lo-Fi era at any setting, **When** comparing to pristine mode, **Then** the difference is immediately obvious to any listener.

---

### User Story 4 - Tempo-Synced Delay (Priority: P2)

A musician working on a song wants the delay time to lock to their DAW's tempo. When the tempo changes, the delay time should update smoothly to maintain musical timing (quarter notes, dotted eighths, etc.).

**Why this priority**: Essential for professional use. Tempo sync is expected in any modern delay plugin.

**Independent Test**: Can be tested by changing host tempo and verifying delay time updates correctly for each note value.

**Acceptance Scenarios**:

1. **Given** tempo sync is enabled with quarter note selected at 120 BPM, **When** the host tempo is read, **Then** delay time is exactly 500ms.

2. **Given** tempo sync with dotted eighth at 120 BPM, **When** delay time is calculated, **Then** delay time is exactly 375ms.

3. **Given** tempo sync enabled, **When** host tempo changes from 120 to 140 BPM, **Then** delay time transitions smoothly without clicks or pops.

---

### User Story 5 - Program-Dependent Limiting (Priority: P3)

A sound designer wants to push the feedback into self-oscillation territory but needs the system to remain stable. The feedback limiter should respond to the audio content - allowing transients through while controlling sustained buildups, preventing harsh digital clipping.

**Why this priority**: Safety and creative feature. Prevents runaway feedback while allowing creative abuse of high feedback settings.

**Independent Test**: Can be tested by setting feedback above 100% and verifying the system remains stable with various input signals.

**Acceptance Scenarios**:

1. **Given** feedback is set to 120%, **When** a loud signal is processed, **Then** the output remains within safe limits (no digital clipping).

2. **Given** high feedback with the limiter engaged, **When** transients enter the feedback loop, **Then** they pass through with minimal squashing on the first few repeats.

3. **Given** sustained feedback buildup, **When** level approaches clipping, **Then** the limiter smoothly reduces gain without audible pumping.

---

### User Story 6 - Modulated Digital Delay (Priority: P3)

A guitarist wants flexible modulation on their delays for added depth. Unlike analog emulations limited to triangle LFOs, digital delays can offer multiple waveform shapes for different creative effects - from smooth sine waves for subtle chorus, to sample & hold for rhythmic glitchy textures.

**Why this priority**: Enhancement feature. Adds dimension and creative possibilities beyond what analog delays can offer.

**Independent Test**: Can be tested by measuring pitch deviation on sustained notes with each waveform type and verifying characteristic behavior.

**Acceptance Scenarios**:

1. **Given** modulation depth is 0%, **When** a sustained note is processed with any waveform, **Then** the delayed output has zero pitch variation.

2. **Given** Sine waveform at 50% depth and 0.5Hz rate, **When** a sustained note is processed, **Then** smooth, natural pitch wobble is audible.

3. **Given** Triangle waveform at 50% depth, **When** a sustained note is processed, **Then** linear pitch sweeps are audible (classic chorus character).

4. **Given** Saw waveform at 50% depth, **When** a sustained note is processed, **Then** rising or falling pitch ramps create Doppler-like effect.

5. **Given** Square waveform at 50% depth, **When** a sustained note is processed, **Then** alternating pitch steps create rhythmic modulation effect.

6. **Given** Sample & Hold waveform, **When** a sustained note is processed, **Then** random stepped pitch changes occur at the LFO rate.

7. **Given** Random (smoothed) waveform, **When** a sustained note is processed, **Then** continuously varying random pitch movement is audible.

---

### Edge Cases

- What happens when delay time is set to minimum (1ms)? Should produce clean comb filtering effect.
- What happens when delay time is at maximum (10 seconds)? Should work without buffer issues or memory problems.
- How does system handle sudden tempo changes in sync mode? Should crossfade or smooth transition.
- What happens when feedback exceeds 100%? Limiter should engage without harsh clipping.
- What happens when switching eras during playback? Should transition smoothly without pops.
- How does bit reduction interact with very quiet signals? Should maintain minimum bit depth to prevent total silence.

## Requirements *(mandatory)*

### Functional Requirements

#### Core Delay Engine

- **FR-001**: System MUST provide delay times from 1ms to 10,000ms (10 seconds)
- **FR-002**: System MUST support both free-running (milliseconds) and tempo-synced time modes
- **FR-003**: Tempo sync MUST support note values: 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1/1, with dotted and triplet modifiers
- **FR-004**: System MUST provide smooth delay time transitions without clicks or pops

#### Era Presets

- **FR-005**: System MUST provide three era presets: Pristine, 80s Digital, Lo-Fi
- **FR-006**: Pristine mode MUST provide flat frequency response (within 0.1dB from 20Hz-20kHz)
- **FR-007**: Pristine mode MUST introduce no measurable noise or distortion
- **FR-008**: 80s Digital mode MUST apply subtle high-frequency rolloff (12-14kHz corner)
- **FR-009**: 80s Digital mode MUST add subtle sample rate reduction effect (effective 32kHz)
- **FR-010**: 80s Digital mode MUST add low-level noise characteristic of early converters (-80dB floor)
- **FR-011**: Lo-Fi mode MUST apply aggressive bit depth reduction (down to 8-bit at max Age)
- **FR-012**: Lo-Fi mode MUST apply sample rate reduction creating audible aliasing (down to 8kHz at max Age)
- **FR-013**: Lo-Fi mode MUST produce obviously degraded audio character

#### Feedback System

- **FR-014**: System MUST provide feedback control from 0% to 120%
- **FR-015**: Feedback path MUST include a program-dependent limiter
- **FR-016**: Limiter MUST prevent digital clipping at all feedback settings
- **FR-017**: Limiter MUST allow transients through with minimal coloration on initial repeats
- **FR-018**: Limiter MUST apply increasing gain reduction as feedback builds up
- **FR-019**: Limiter character MUST be adjustable (soft/medium/hard knee)

#### Modulation

- **FR-020**: System MUST provide optional LFO modulation of delay time
- **FR-021**: Modulation depth MUST be adjustable from 0% to 100%
- **FR-022**: Modulation rate MUST be adjustable from 0.1Hz to 10Hz
- **FR-023**: Modulation waveform MUST be selectable from: Sine, Triangle, Saw, Square, Sample & Hold, Random (smoothed)
- **FR-024**: At 0% depth, there MUST be zero pitch variation regardless of waveform
- **FR-025**: Sine waveform MUST produce smooth, natural pitch variation
- **FR-026**: Triangle waveform MUST produce linear pitch sweeps (classic chorus character)
- **FR-027**: Saw waveform MUST produce rising or falling pitch ramps (Doppler-like effect)
- **FR-028**: Square waveform MUST produce alternating pitch steps (rhythmic modulation)
- **FR-029**: Sample & Hold MUST produce random stepped pitch changes at the LFO rate
- **FR-030**: Random (smoothed) MUST produce continuously varying random pitch movement

#### Mix and Output

- **FR-031**: System MUST provide dry/wet mix control from 0% to 100%
- **FR-032**: System MUST provide output level control in dB (-inf to +12dB)
- **FR-033**: All parameter changes MUST be smoothed to prevent zipper noise (20ms smoothing)
- **FR-034**: Mix at 0% MUST pass dry signal unaffected

#### Processing Modes

- **FR-035**: System MUST support stereo processing (independent L/R)
- **FR-036**: System MUST support mono processing
- **FR-037**: Stereo processing MUST maintain channel separation

#### Real-Time Safety

- **FR-038**: Processing MUST NOT allocate memory during audio callback
- **FR-039**: All processing functions MUST be noexcept
- **FR-040**: Processing MUST complete within real-time constraints

#### Age/Degradation Control

- **FR-041**: Age parameter (0-100%) MUST control degradation intensity for 80s Digital and Lo-Fi eras
- **FR-042**: In Pristine mode, Age parameter MUST have no effect
- **FR-043**: Age at 0% in 80s Digital MUST provide minimal vintage character
- **FR-044**: Age at 100% in Lo-Fi MUST provide maximum degradation

### Key Entities

- **DigitalDelay**: Main processor class composing Layer 3 components
- **DigitalEra**: Enumeration of era presets (Pristine, EightiesDigital, LoFi)
- **LimiterCharacter**: Enumeration of limiter knee types (Soft, Medium, Hard)
- **TimeMode**: Reuse existing enum from DelayEngine (Free, Synced)
- **Waveform**: Reuse existing enum from LFO (Sine, Triangle, Sawtooth, Square, SampleHold, SmoothRandom)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Pristine mode achieves frequency response flat within 0.1dB from 20Hz to 20kHz
- **SC-002**: Pristine mode introduces less than -120dB noise floor relative to full-scale signal
- **SC-003**: 80s Digital mode produces audible vintage character distinguishable from Pristine in blind A/B test
- **SC-004**: Lo-Fi mode produces obviously degraded audio that listeners identify as "lo-fi"
- **SC-005**: All era transitions complete without audible clicks or pops
- **SC-006**: Feedback at 120% with limiter engaged produces stable, non-clipping output
- **SC-007**: Processing completes within real-time constraints at 44.1kHz stereo
- **SC-008**: Tempo sync accuracy within 1 sample of calculated note value
- **SC-009**: Parameter changes produce no audible zipper noise

### Perceptual Criteria (Manual Testing)

- **SC-010**: Pristine mode sounds indistinguishable from a simple sample-delayed signal
- **SC-011**: 80s Digital mode evokes the character of vintage digital delays
- **SC-012**: Lo-Fi mode sounds deliberately "broken" in a musically useful way

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Sample rate is provided by host and ranges from 44.1kHz to 192kHz
- Maximum block size is 8192 samples
- Host provides tempo and transport information via BlockContext
- Users understand the difference between era presets and can select appropriately
- Bit reduction and sample rate reduction use existing Layer 1 primitives

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| DelayEngine | src/dsp/systems/delay_engine.h | Core delay - will compose |
| FeedbackNetwork | src/dsp/systems/feedback_network.h | Feedback path - will compose |
| CharacterProcessor | src/dsp/systems/character_processor.h | Has Digital mode - will compose |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Parameter smoothing |
| BitCrusher | src/dsp/primitives/bit_crusher.h | Bit reduction for Lo-Fi |
| SampleRateReducer | src/dsp/primitives/sample_rate_reducer.h | SR reduction for vintage |
| DynamicsProcessor | src/dsp/processors/dynamics_processor.h | May use for limiter |
| LFO | src/dsp/primitives/lfo.h | Modulation source |
| BlockContext | src/dsp/core/block_context.h | Tempo sync support |
| NoteValue | src/dsp/core/note_value.h | Note value calculations |
| dbToGain | src/dsp/core/db_utils.h | Level conversions |

**Initial codebase search for key terms:**

```bash
grep -r "BitCrusher" src/
grep -r "SampleRateReducer" src/
grep -r "CharacterMode::Digital" src/
```

**Search Results Summary**: BitCrusher and SampleRateReducer exist in Layer 1 primitives. CharacterProcessor already has a Digital mode that can be leveraged.

### Forward Reusability Consideration

**Sibling features at same layer:**
- PingPong Delay (will share core delay/feedback structure)
- Multi-Tap Delay (will share era presets and limiting)
- Shimmer Delay (will share feedback network patterns)

**Potential shared components:**
- The program-dependent limiter could be extracted as a reusable component if sufficiently general
- Era-based processing pipeline pattern may inform other delay modes
- Bit/sample rate reduction integration pattern

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| SC-001 | | |
| SC-002 | | |

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
