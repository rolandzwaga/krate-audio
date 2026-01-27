# Feature Specification: MidSideProcessor

**Feature Branch**: `014-midside-processor`
**Created**: 2025-12-24
**Status**: Draft
**Layer**: 2 (DSP Processor)
**Input**: User description: "Implement a MidSideProcessor Layer 2 DSP processor for stereo field manipulation. Should include: Mid/Side encoding (L/R to M/S matrix conversion), Mid/Side decoding (M/S to L/R matrix conversion), independent Mid and Side gain controls, width control (0-200% where 100% is unity, 0% is mono, 200% is full stereo enhancement), and optional solo modes for monitoring Mid or Side independently. Must handle mono input gracefully (Side channel is silent). Real-time safe with no allocations in process(), composable with other Layer 2 processors for the Stereo Field system in Layer 3."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Mid/Side Encoding and Decoding (Priority: P1)

A DSP developer needs to convert stereo audio from Left/Right format to Mid/Side format for processing, and then convert back to Left/Right for output. This is the foundational operation that all other features depend on.

**Why this priority**: Mid/Side encoding/decoding is the core mathematical operation. Without this, no other features can work. This enables all stereo field manipulation.

**Independent Test**: Process a known stereo signal through encode then decode, verify output matches input within floating-point tolerance.

**Acceptance Scenarios**:

1. **Given** a stereo input with L=1.0, R=1.0, **When** encoded to M/S, **Then** Mid=1.0 (sum) and Side=0.0 (difference)
2. **Given** a stereo input with L=1.0, R=-1.0, **When** encoded to M/S, **Then** Mid=0.0 and Side=1.0
3. **Given** M/S encoded signal, **When** decoded back to L/R, **Then** output equals original input (roundtrip)
4. **Given** L=0.5, R=0.3, **When** encoded then decoded, **Then** L=0.5, R=0.3 (perfect reconstruction)

---

### User Story 2 - Stereo Width Control (Priority: P2)

A mixing engineer wants to adjust the stereo width of audio from mono (0%) through normal (100%) to enhanced stereo (200%) without changing the overall level perception.

**Why this priority**: Width control is the most common use case for Mid/Side processing in mixing and mastering. It provides immediate value for stereo enhancement or narrowing.

**Independent Test**: Process stereo audio with width at 0%, 100%, and 200%, verify mono collapses to center, 100% is unity, and 200% doubles the stereo difference.

**Acceptance Scenarios**:

1. **Given** stereo input and width=0%, **When** processed, **Then** output is mono (L=R=Mid)
2. **Given** stereo input and width=100%, **When** processed, **Then** output equals input (unity/bypass)
3. **Given** stereo input and width=200%, **When** processed, **Then** Side component is doubled
4. **Given** width change from 0% to 200%, **When** processing, **Then** transition is click-free (smoothed)

---

### User Story 3 - Independent Mid and Side Gain (Priority: P3)

A mastering engineer needs to independently adjust the level of the Mid (center/mono) and Side (stereo difference) components to balance the stereo image or fix mix issues.

**Why this priority**: Independent gain control allows precise stereo image correction beyond simple width adjustment. Essential for mastering workflows.

**Independent Test**: Boost Mid by +6dB while cutting Side by -6dB, verify center content is louder and stereo content is quieter.

**Acceptance Scenarios**:

1. **Given** midGain=+6dB and sideGain=0dB, **When** processed, **Then** center content is 2x louder
2. **Given** midGain=0dB and sideGain=-96dB, **When** processed, **Then** output is effectively mono
3. **Given** midGain=0dB and sideGain=+6dB, **When** processed, **Then** stereo width increases
4. **Given** gain change on either channel, **When** processing, **Then** transition is click-free

---

### User Story 4 - Solo Modes for Monitoring (Priority: P4)

A mix/mastering engineer wants to solo either the Mid or Side component to identify problematic content or verify stereo placement.

**Why this priority**: Solo modes are essential diagnostic tools but not required for normal operation. They enable quality control without being part of the signal path.

**Independent Test**: Enable Mid solo, verify only center content is audible. Enable Side solo, verify only stereo difference is audible.

**Acceptance Scenarios**:

1. **Given** soloMid=true, **When** processed, **Then** output contains only Mid content (L=R=Mid)
2. **Given** soloSide=true, **When** processed, **Then** output contains only Side content (L=+Side, R=-Side)
3. **Given** both solo modes enabled, **When** processed, **Then** Mid solo takes precedence (safety behavior)
4. **Given** solo mode toggled during playback, **When** processing, **Then** transition is click-free

---

### User Story 5 - Mono Input Handling (Priority: P5)

A DSP developer needs the processor to gracefully handle mono input (L=R) without producing artifacts or requiring special configuration.

**Why this priority**: Mono compatibility is essential for robust plugin behavior but is an edge case. The processor should "just work" with mono content.

**Independent Test**: Feed mono audio (L=R), verify Side component is zero and processing still works correctly.

**Acceptance Scenarios**:

1. **Given** mono input (L=R), **When** encoded, **Then** Side=0.0 exactly
2. **Given** mono input with width=200%, **When** processed, **Then** output remains mono (no phantom stereo)
3. **Given** mono input with sideGain=+20dB, **When** processed, **Then** no noise amplification occurs

---

### User Story 6 - Real-Time Safe Processing (Priority: P6)

A plugin host requires that the processor never allocates memory, blocks, or causes audio dropouts during real-time audio processing.

**Why this priority**: Real-time safety is a constitution requirement (Principle II) but is verified at implementation, not user-facing.

**Independent Test**: Process audio for extended period, verify no allocations occur and CPU usage is consistent.

**Acceptance Scenarios**:

1. **Given** any parameter configuration, **When** process() called, **Then** no memory allocation occurs
2. **Given** varying block sizes (1 to 8192 samples), **When** processed, **Then** operation completes without issues
3. **Given** extreme parameter values, **When** processed, **Then** output remains bounded and valid

---

### Edge Cases

- What happens when input contains NaN or Infinity? (Processor should output zero or pass-through safely)
- What happens at width boundaries (exactly 0% or exactly 200%)? (Should work identically to nearby values)
- What happens with DC offset in input? (Should be preserved through encode/decode cycle)
- What happens with sample rate changes? (Parameter smoothers should recalculate coefficients)

## Requirements *(mandatory)*

### Functional Requirements

**Core Encoding/Decoding:**
- **FR-001**: System MUST encode stereo L/R to M/S using formula: Mid = (L + R) / 2, Side = (L - R) / 2
- **FR-002**: System MUST decode M/S to stereo L/R using formula: L = Mid + Side, R = Mid - Side
- **FR-003**: System MUST guarantee perfect reconstruction: decode(encode(L,R)) = (L, R) within floating-point tolerance

**Width Control:**
- **FR-004**: System MUST provide width parameter with range [0%, 200%] (normalized 0.0 to 2.0)
- **FR-005**: System MUST implement width as Side channel scaling: effectiveSide = Side * (width / 100%)
- **FR-006**: At width=0%, output MUST be mono (L = R = Mid)
- **FR-007**: At width=100%, output MUST equal input (unity/bypass behavior)
- **FR-008**: At width=200%, Side component MUST be doubled

**Gain Controls:**
- **FR-009**: System MUST provide independent midGain parameter in range [-96dB, +24dB]
- **FR-010**: System MUST provide independent sideGain parameter in range [-96dB, +24dB]
- **FR-011**: Gain at -96dB MUST effectively silence the channel (< -120dB output)
- **FR-012**: All gain changes MUST be smoothed to prevent clicks (parameter smoothing)

**Solo Modes:**
- **FR-013**: System MUST provide soloMid boolean mode
- **FR-014**: System MUST provide soloSide boolean mode
- **FR-015**: When soloMid=true, output MUST be Mid only: L = R = Mid
- **FR-016**: When soloSide=true, output MUST be Side only: L = +Side, R = -Side
- **FR-017**: When both solos enabled, soloMid MUST take precedence
- **FR-018**: Solo mode changes MUST be smoothed to prevent clicks

**Mono Handling:**
- **FR-019**: System MUST handle mono input (L=R) correctly, producing Side=0
- **FR-020**: Width/gain adjustments on mono input MUST NOT produce phantom stereo or noise

**Real-Time Safety:**
- **FR-021**: process() MUST NOT allocate memory
- **FR-022**: process() MUST be noexcept
- **FR-023**: System MUST support block sizes from 1 to 8192 samples
- **FR-024**: System MUST provide prepare(sampleRate, maxBlockSize) for initialization

**API:**
- **FR-025**: System MUST provide process(leftIn, rightIn, leftOut, rightOut, numSamples) method
- **FR-026**: System MUST provide processStereo(stereoBuffer, numSamples) for interleaved stereo
- **FR-027**: System MUST provide reset() to clear internal state
- **FR-028**: System MUST be header-only and depend only on Layer 0-1 components

### Key Entities

- **MidSideProcessor**: Main processor class handling all Mid/Side operations
- **StereoSample**: Conceptual pair of (left, right) or (mid, side) samples
- **ProcessingMode**: Current state - normal, soloMid, soloSide
- **Width**: Normalized stereo width value (0.0 = mono, 1.0 = unity, 2.0 = enhanced)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Roundtrip encode/decode accuracy within 1e-6 (single-precision tolerance)
- **SC-002**: Width=0% produces output where |L - R| < 1e-6 (effectively mono)
- **SC-003**: Width=100% produces output within 1e-6 of input (unity)
- **SC-004**: All parameter changes produce click-free transitions (no samples > 2x adjacent samples)
- **SC-005**: Processing 8192-sample blocks completes with O(n) complexity
- **SC-006**: CPU usage < 0.1% per instance at 44.1kHz stereo
- **SC-007**: Solo modes correctly isolate components (< -100dB crosstalk)
- **SC-008**: Mono input produces exactly zero Side component (not just nearly zero)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Input audio is stereo (2 channels) - mono will be treated as L=R
- Sample rate is known at prepare() time and stable during processing
- Block sizes are consistent within a processing session
- Parameter changes may occur at any time, including during process() calls
- Standard float precision (32-bit) is sufficient for audio processing

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| OnePoleSmoother | src/dsp/primitives/smoother.h | Reuse for parameter smoothing |
| dbToGain() | src/dsp/core/db_utils.h | Reuse for gain conversion |
| Biquad | src/dsp/primitives/biquad.h | Not needed - no filtering required |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "MidSide\|Mid.*Side\|M.*S.*encode\|stereo.*width" src/
grep -r "class.*Stereo\|struct.*Stereo" src/
```

**Search Results Summary**: To be completed during planning phase

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| ... | | |

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
