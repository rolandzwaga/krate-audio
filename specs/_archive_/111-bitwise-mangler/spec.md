# Feature Specification: BitwiseMangler

**Feature Branch**: `111-bitwise-mangler`
**Created**: 2026-01-27
**Status**: Ready for Planning
**Input**: User description: "BitwiseMangler - bit manipulation distortion primitive with XOR, rotation, shuffle, and overflow wrap operations for wild tonal shifts. Based on DST-ROADMAP.md section 8.1."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - XOR Pattern Distortion (Priority: P1)

A sound designer wants to create harmonically complex distortion with a characteristic metallic edge. They load the BitwiseMangler, select XorPattern mode, and set a 32-bit pattern value. When they process a sine wave, the XOR operation creates complex harmonic structures that shift based on the input amplitude, producing wild tonal shifts unlike any traditional distortion.

**Why this priority**: XorPattern is the most versatile and commonly desired bit manipulation effect. It creates the "wild tonal shifts" mentioned in the roadmap and demonstrates the core novelty of bitwise distortion.

**Independent Test**: Can be fully tested by processing a sine wave through XorPattern mode and verifying that output contains harmonics not present in input, with harmonic content varying based on pattern value.

**Acceptance Scenarios**:

1. **Given** BitwiseMangler in XorPattern mode with pattern 0xAAAAAAAA, **When** a 440Hz sine wave is processed, **Then** the output spectrum contains harmonics at frequencies not present in pure sine input.
2. **Given** BitwiseMangler in XorPattern mode with intensity 0.5, **When** audio is processed, **Then** the output is a blend of 50% original and 50% mangled signal.
3. **Given** different pattern values (0x55555555 vs 0xFFFFFFFF), **When** the same input is processed, **Then** the spectral content differs audibly between patterns.

---

### User Story 2 - XOR with Previous Sample (Priority: P1)

A musician wants to create input-reactive distortion that changes character based on the signal content. They select XorPrevious mode, which XORs each sample with the previous sample. Fast-changing signals (transients, high frequencies) produce more dramatic effects, while slow signals (low frequencies, sustained notes) produce subtler changes.

**Why this priority**: XorPrevious creates signal-dependent distortion that responds to the input character - this is a unique property that makes the effect musically responsive rather than static.

**Independent Test**: Can be fully tested by comparing the effect on high-frequency vs low-frequency content and verifying that transients produce more dramatic spectral changes.

**Acceptance Scenarios**:

1. **Given** BitwiseMangler in XorPrevious mode with intensity 1.0, **When** a high-frequency (8kHz) sine is processed, **Then** the output has more dramatic spectral change than when processing a low-frequency (100Hz) sine.
2. **Given** XorPrevious mode, **When** a drum transient is processed, **Then** the attack portion shows more distortion than the decay.
3. **Given** XorPrevious mode after reset(), **When** the first sample is processed, **Then** it XORs with the default previous sample value (0 in integer representation).

---

### User Story 3 - Bit Rotation (Priority: P2)

A producer wants pitch-shifting-like artifacts without actual pitch shifting. They select BitRotate mode and set the rotation amount. Rotating bits left or right creates pseudo-pitch effects where the frequency content shifts in unusual, non-harmonic ways - higher rotations create more extreme shifts.

**Why this priority**: BitRotate creates unique pseudo-pitch effects that cannot be achieved with traditional DSP, expanding the creative palette significantly.

**Independent Test**: Can be fully tested by rotating bits by different amounts and verifying that output frequency content shifts in a non-linear manner.

**Acceptance Scenarios**:

1. **Given** BitwiseMangler in BitRotate mode with rotateAmount +4, **When** a 1kHz sine is processed, **Then** the output's primary frequency content shifts (not necessarily to 1kHz).
2. **Given** BitRotate with rotateAmount +8 vs -8, **When** the same input is processed, **Then** the two outputs sound different (left vs right rotation produces different results).
3. **Given** rotateAmount 0, **When** audio is processed, **Then** the output equals the input (no rotation = passthrough).

---

### User Story 4 - Bit Shuffle (Priority: P2)

A sound designer wants to create glitchy, unpredictable textures. They select BitShuffle mode which reorders bits within each sample using a deterministic pattern based on the seed parameter. The result is chaotic yet repeatable - the same seed produces the same shuffle pattern.

**Why this priority**: BitShuffle creates the most dramatic destruction of the original signal, useful for extreme sound design and glitch aesthetics.

**Independent Test**: Can be fully tested by processing with different seeds and verifying that different seeds produce different but deterministic outputs.

**Acceptance Scenarios**:

1. **Given** BitwiseMangler in BitShuffle mode with seed 12345, **When** a sine wave is processed twice with reset() between, **Then** both outputs are identical (deterministic).
2. **Given** BitShuffle with seed 12345 vs seed 67890, **When** the same input is processed, **Then** the outputs differ audibly.
3. **Given** BitShuffle mode, **When** processing occurs, **Then** the output waveform is dramatically different from input (destructive transformation).

---

### User Story 5 - Bit Average (Priority: P3)

A musician wants subtle bit-level smoothing or thickening. They select BitAverage mode which performs AND operations with adjacent samples in the buffer. This creates a unique type of "bit-level averaging" that smooths the signal by preserving only common set bits between adjacent samples.

**Why this priority**: BitAverage is more subtle than other modes, providing a range from gentle texture to aggressive mangling based on the adjacent sample relationship.

**Independent Test**: Can be fully tested by processing a signal and verifying that output values are influenced by adjacent sample values.

**Acceptance Scenarios**:

1. **Given** BitwiseMangler in BitAverage mode (AND operation), **When** two adjacent samples have different bit patterns, **Then** the output tends toward values with fewer set bits.
2. **Given** BitAverage mode with intensity 0.5, **When** audio is processed, **Then** the output blends the averaged result with the original.
3. **Given** BitAverage mode on a varying signal, **When** adjacent samples differ significantly, **Then** the output shows more smoothing than when adjacent samples are similar.

---

### User Story 6 - Overflow Wrap (Priority: P3)

A producer wants to create hard digital clipping with wrap-around instead of saturation. They apply upstream gain or processing to drive the signal hot, then select OverflowWrap mode where values that exceed the valid integer range wrap around instead of clipping. This creates harsh, unpredictable distortion characteristic of integer overflow.

**Why this priority**: OverflowWrap provides a unique alternative to traditional clipping that creates distinctive digital artifacts.

**Independent Test**: Can be fully tested by applying upstream gain to drive input hot, then verifying that output wraps around rather than clipping at boundaries.

**Acceptance Scenarios**:

1. **Given** BitwiseMangler in OverflowWrap mode, **When** upstream processing produces a value of 1.5 (exceeds +1.0 float normalization), **Then** the output wraps to a negative value (simulating integer overflow behavior).
2. **Given** OverflowWrap mode with moderate input, **When** signals stay within [-1, 1], **Then** the output equals the input (no wrapping needed).
3. **Given** OverflowWrap mode with hot input from upstream gain, **When** compared to traditional hard clipping, **Then** the output sounds characteristically different (wrap vs clip).

---

### Edge Cases

- What happens when pattern is set to 0x00000000 in XorPattern mode?
  - XOR with all zeros equals the original value, effectively bypassing the effect.
- What happens when pattern is set to 0xFFFFFFFF in XorPattern mode?
  - XOR with all ones inverts all bits, creating maximum bit-flipping effect.
- What happens with DC input (constant value)?
  - In XorPattern mode: creates constant altered DC; in XorPrevious mode: creates near-zero output (sample XOR same sample = 0); in other modes: creates constant altered output.
- What happens with extreme values (+/- 1.0 at float boundaries)?
  - Float-to-int conversion handles boundary values correctly; output remains valid after inverse conversion.
- What happens with NaN/Inf input?
  - Processor detects invalid float values and returns 0.0 to prevent corruption.
- What happens with denormal input values?
  - Denormals are flushed to zero before processing to prevent CPU spikes.
- What happens when rotateAmount exceeds bit width (e.g., +32 or more)?
  - Rotation amount is taken modulo the effective bit width (rotation by 32 equals rotation by 0).
- What happens when sample rate changes?
  - prepare() must be called to reconfigure; no sample-rate-dependent behavior exists (stateless operations).
- What happens when intensity is 0.0?
  - Output equals input (full bypass of the bit manipulation).
- What happens when intensity is 1.0?
  - Output is fully mangled (no blend with original).

## Clarifications

### Session 2026-01-27

- Q: What scaling formula should be used for float-to-integer conversion? → A: Multiply by 8388608 (power of 2), accepting slight asymmetry - standard audio conversion approach
- Q: Should BitAverage operation be fixed to AND or parameterized with AND/OR options? → A: Fixed to AND operation, noted as future parameter candidate
- Q: How should BitShuffle generate the shuffle permutation from the seed? → A: Pre-compute permutation table on setSeed() call (24-element array mapping input bit position to output position)
- Q: Should OverflowWrap apply internal gain to drive signals into overflow? → A: OverflowWrap shall not apply internal gain. It shall only wrap values that exceed the numeric range due to upstream processing.
- Q: How should the BitShuffle permutation table be stored to maintain real-time safety? → A: BitShuffle shall store its permutation as a fixed-size array of 24 indices allocated as part of the object state.

## Requirements *(mandatory)*

### Functional Requirements

**Lifecycle:**
- **FR-001**: System MUST provide `prepare(double sampleRate)` to initialize for processing
- **FR-002**: System MUST provide `reset()` to clear all internal state (previous sample, RNG state)
- **FR-003**: System MUST support sample rates from 44100Hz to 192000Hz

**Operation Selection:**
- **FR-004**: System MUST provide `setOperation(BitwiseOperation op)` to select the bit manipulation algorithm
- **FR-005**: System MUST support all six BitwiseOperation modes: XorPattern, XorPrevious, BitRotate, BitShuffle, BitAverage, OverflowWrap
- **FR-006**: Operation changes MUST take effect on the next sample (no smoothing required for discrete selection)

**Intensity Control:**
- **FR-007**: System MUST provide `setIntensity(float intensity)` to control blend with original signal
- **FR-008**: Intensity MUST be clamped to range [0.0, 1.0] where 0.0 = bypass, 1.0 = full effect
- **FR-009**: Intensity blending formula MUST be: `output = original * (1 - intensity) + mangled * intensity`

**XorPattern Parameters:**
- **FR-010**: System MUST provide `setPattern(uint32_t pattern)` to set the XOR pattern for XorPattern mode
- **FR-011**: Pattern value MUST accept any 32-bit unsigned integer value (full range)
- **FR-012**: Default pattern MUST be 0xAAAAAAAA (alternating bits) for interesting initial results

**BitRotate Parameters:**
- **FR-013**: System MUST provide `setRotateAmount(int bits)` to set rotation direction and amount for BitRotate mode
- **FR-014**: Rotate amount MUST be clamped to range [-16, +16] where positive = left rotation, negative = right rotation
- **FR-015**: Rotation MUST operate on the significant bits of the integer representation (24-bit effective for float precision)

**Seed Control:**
- **FR-016**: System MUST provide `setSeed(uint32_t seed)` to set the random seed for BitShuffle and any random operations
- **FR-017**: Same seed MUST produce identical results for BitShuffle mode (deterministic behavior)
- **FR-018**: Default seed MUST be non-zero to ensure valid RNG state
- **FR-018a**: BitShuffle MUST pre-compute a 24-element permutation table when setSeed() is called, mapping each input bit position to an output bit position
- **FR-018b**: Permutation table MUST be stored as a fixed-size array (std::array<uint8_t, 24>) allocated as part of object state to maintain real-time safety

**Processing:**
- **FR-019**: System MUST provide `float process(float x) noexcept` for single sample processing
- **FR-020**: System MUST provide `void processBlock(float* buffer, size_t n) noexcept` for block processing
- **FR-021**: Processing MUST be real-time safe (no allocations, locks, exceptions, or I/O)
- **FR-022**: System MUST handle NaN/Inf inputs by returning 0.0
- **FR-023**: System MUST flush denormals to zero before processing
- **FR-024**: Output values MUST remain in valid float range (no NaN/Inf output)

**Float-to-Integer Conversion:**
- **FR-025**: System MUST convert float samples to integer representation for bit operations using consistent scaling
- **FR-026**: Integer representation MUST use 24-bit signed integer range (-8388608 to +8388607) to match float mantissa precision
- **FR-026a**: Float-to-int conversion MUST use multiplication by 8388608 (2^23) for performance and standard audio conversion compatibility
- **FR-027**: Conversion MUST be reversible: `intToFloat(floatToInt(x)) == x` for valid float inputs (within float precision)

**XorPrevious State:**
- **FR-028**: XorPrevious mode MUST maintain state of the previous sample across process calls
- **FR-029**: reset() MUST clear the previous sample state to zero (integer representation)

**BitAverage Behavior:**
- **FR-030**: BitAverage mode MUST operate on the current sample and the adjacent sample within the block
- **FR-031**: For single-sample processing, BitAverage MUST use the previous sample (same as XorPrevious but with AND)
- **FR-032**: BitAverage MUST perform bitwise AND for the averaging operation (fixed for v1; OR operation considered as future parameter candidate)

**OverflowWrap Behavior:**
- **FR-033**: OverflowWrap mode MUST wrap values that exceed integer range instead of clamping
- **FR-034**: Wrap behavior MUST simulate two's complement integer overflow
- **FR-034a**: OverflowWrap mode MUST NOT apply internal gain; wrapping occurs only when upstream processing produces values exceeding the normalized float range

### Key Entities

- **BitwiseMangler**: Main primitive class implementing bit manipulation distortion
- **BitwiseOperation**: Enum defining the six operation modes (XorPattern, XorPrevious, BitRotate, BitShuffle, BitAverage, OverflowWrap)
- **Xorshift32**: Existing PRNG in `core/random.h` used for BitShuffle shuffle pattern generation

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: XorPattern mode produces output with total harmonic distortion (THD) greater than 10% when processing a pure sine wave with pattern 0xAAAAAAAA at intensity 1.0
- **SC-002**: XorPrevious mode produces higher THD for 8kHz input than 100Hz input (frequency-dependent response)
- **SC-003**: BitRotate with amount +8 produces different spectrum than amount -8 on the same input (asymmetric rotation behavior)
- **SC-004**: BitShuffle with same seed produces bit-exact identical output across multiple runs after reset()
- **SC-005**: All parameter changes take effect within one sample (no latency)
- **SC-006**: Processor uses less than 0.1% CPU per instance at 44100Hz sample rate (Layer 1 primitive budget)
- **SC-007**: Processing latency is zero samples
- **SC-008**: Float roundtrip (float -> int -> operation -> float) maintains signal integrity within 24-bit precision (-144dB noise floor relative to full scale)
- **SC-009**: Intensity at 0.0 produces bit-exact passthrough of input signal
- **SC-010**: No DC offset greater than 0.001 is introduced by any operation mode when processing zero-mean input

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Users understand that bitwise operations create non-traditional, often harsh distortion
- Users will compose with other processors (filters, limiters) to tame extreme results
- The processor operates in mono; stereo processing requires two instances with potentially different seeds
- Float-to-integer conversion assumes input is normalized to [-1.0, +1.0] range
- Output may exceed [-1.0, +1.0] in OverflowWrap mode; downstream limiting may be needed
- BitShuffle creates deterministic but not cryptographically meaningful permutations

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Xorshift32 | dsp/include/krate/dsp/core/random.h | PRNG for BitShuffle permutation generation - REUSE directly |
| BitCrusher | dsp/include/krate/dsp/primitives/bit_crusher.h | Reference for float-to-int conversion patterns - REFERENCE implementation |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "class BitwiseMangler" dsp/ plugins/
grep -r "BitwiseOperation" dsp/ plugins/
grep -r "XorPattern\|BitRotate\|BitShuffle" dsp/ plugins/
```

**Search Results Summary**: No existing BitwiseMangler or BitwiseOperation found. Xorshift32 exists and should be reused. BitCrusher provides reference patterns for float-to-integer conversion but uses a different approach (quantization levels rather than direct bit manipulation).

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer.*

**Sibling features at same layer** (from DST-ROADMAP.md Priority 8 - Digital Destruction):
- 112-aliasing-effect - Intentional aliasing processor (Layer 2)
- 113-granular-distortion - Per-grain variable distortion (Layer 2)
- 114-fractal-distortion - Recursive multi-scale distortion (Layer 2)

**Potential shared components** (preliminary, refined in plan.md):
- The float-to-int-to-float conversion utilities could be extracted to Layer 0 core utilities if they prove useful for other digital destruction effects
- The BitwiseOperation enum pattern could be referenced by other processors that need discrete mode selection

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
| FR-018a | | |
| FR-018b | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-024 | | |
| FR-025 | | |
| FR-026 | | |
| FR-026a | | |
| FR-027 | | |
| FR-028 | | |
| FR-029 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-034 | | |
| FR-034a | | |
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
