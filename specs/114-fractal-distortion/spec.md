# Feature Specification: FractalDistortion

**Feature Branch**: `114-fractal-distortion`
**Created**: 2026-01-27
**Status**: Draft
**Input**: User description: "Recursive multi-scale distortion processor with self-similar harmonic structure - Phase 8 Digital Destruction from DST-ROADMAP"

## Clarifications

### Session 2026-01-27

- Q: Multiband mode crossover implementation - Use existing `Crossover4Way` from `crossover_filter.h` or implement new crossover? → A: Use existing `Crossover4Way` from `crossover_filter.h`
- Q: Frequency decay base frequency parameter - What is the baseFrequency value for highpass filtering at deeper levels? → A: 200Hz base frequency
- Q: Harmonic mode odd/even curve selection - How do users control which saturation curves are applied to odd vs even harmonics? → A: Two setters: `setOddHarmonicCurve(WaveshapeType)` and `setEvenHarmonicCurve(WaveshapeType)` with defaults Tanh (odd) and Tube (even)
- Q: Multiband mode iteration distribution - Acceptance scenario iteration counts don't match formula → A: Use existing formula; update acceptance scenario to Band[0]=1, Band[1]=2, Band[2]=3, Band[3]=6 iterations
- Q: Default waveshaper for base saturation - Which WaveshapeType is used for saturate() function in Residual, Multiband, and Feedback modes? → A: Tanh for smooth, symmetric, and CPU-efficient harmonic processing

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Fractal Saturation (Priority: P1)

A sound designer wants to add complex, evolving harmonic content to a sound by applying distortion that reveals new detail at every "zoom level." They load FractalDistortion in Residual mode, set 4 iterations, and adjust drive and scale factor to taste.

**Why this priority**: This is the core functionality - recursive distortion with amplitude scaling. Without this, the processor has no value. Residual mode is the simplest algorithm and serves as the foundation for all other modes.

**Independent Test**: Can be fully tested by processing a sine wave through Residual mode and measuring that each iteration adds progressively smaller harmonic content, verifiable via FFT analysis.

**Acceptance Scenarios**:

1. **Given** FractalDistortion in Residual mode with iterations=4, scale=0.5, drive=2.0, **When** a sine wave is processed, **Then** output contains harmonics at multiple amplitude levels corresponding to each iteration depth
2. **Given** FractalDistortion with iterations=1, **When** audio is processed, **Then** output is equivalent to a single saturate(input * drive) operation
3. **Given** FractalDistortion with scale=0.0, **When** audio is processed, **Then** only the first iteration contributes (deeper levels produce zero output)

---

### User Story 2 - Multiband Fractal Processing (Priority: P2)

A producer wants to add aggressive distortion to high frequencies while keeping bass frequencies clean. They use Multiband mode to split the signal into octave bands, applying more iterations to higher bands and fewer to lower bands.

**Why this priority**: Multiband mode provides frequency-aware fractal processing, enabling practical mixing applications like adding "grit" to highs without muddying lows.

**Independent Test**: Can be tested by processing a full-spectrum signal and measuring that high-frequency bands receive more iterations (more harmonic complexity) than low-frequency bands.

**Acceptance Scenarios**:

1. **Given** FractalDistortion in Multiband mode with baseIterations=6, bandIterationScale=0.5, crossoverFrequency=250Hz, **When** audio is processed, **Then** band[0] receives 1 iteration, band[1] receives 2 iterations, band[2] receives 3 iterations, and band[3] receives 6 iterations
2. **Given** FractalDistortion in Multiband mode, **When** bandIterationScale is set to 1.0, **Then** all bands receive equal iterations (defeats purpose but validates parameter)
3. **Given** FractalDistortion in Multiband mode, **When** bands are recombined, **Then** phase coherence is maintained (using Linkwitz-Riley crossovers via `Crossover4Way`)

---

### User Story 3 - Cascade Mode with Per-Level Waveshapers (Priority: P2)

A sound designer wants to create specific harmonic evolution - warm saturation at low intensity, brighter as it increases, harsh at extremes. They use Cascade mode to assign different waveshaper types to each iteration level.

**Why this priority**: Cascade mode offers precise tonal design by allowing different distortion characters at each recursion level, enabling warm-to-harsh evolution.

**Independent Test**: Can be tested by setting distinct waveshaper types per level and verifying each level applies its designated algorithm.

**Acceptance Scenarios**:

1. **Given** FractalDistortion in Cascade mode with level0=Tube, level1=Tanh, level2=HardClip, **When** audio is processed, **Then** each level applies its designated waveshaper
2. **Given** FractalDistortion in Cascade mode, **When** a level's waveshaper is changed, **Then** the change affects only that level's processing
3. **Given** FractalDistortion in Cascade mode with iterations=3, **When** setLevelWaveshaper(5, type) is called, **Then** the call is safely ignored (level 5 exceeds iterations)

---

### User Story 4 - Harmonic Mode with Odd/Even Separation (Priority: P3)

A sound designer wants to create complex intermodulation effects for bell-like or metallic tones. They use Harmonic mode to separate odd and even harmonics and apply different saturation curves to each.

**Why this priority**: Harmonic mode enables specialized tonal effects through Chebyshev polynomial extraction, useful for unique sound design but more specialized than core modes.

**Independent Test**: Can be tested by processing audio and verifying that odd and even harmonics receive different saturation treatments via spectral analysis.

**Acceptance Scenarios**:

1. **Given** FractalDistortion in Harmonic mode, **When** a sine wave is processed, **Then** odd harmonics (3rd, 5th, 7th) receive curveA treatment and even harmonics (2nd, 4th, 6th) receive curveB treatment
2. **Given** FractalDistortion in Harmonic mode with identical curves for odd and even, **When** audio is processed, **Then** output is equivalent to standard Residual mode

---

### User Story 5 - Feedback Mode for Chaotic Textures (Priority: P3)

An experimental musician wants self-oscillating, droning textures. They use Feedback mode to cross-feed between iteration levels, creating chaotic but controllable distortion effects.

**Why this priority**: Feedback mode enables experimental "glitch" and drone effects, appealing to experimental genres but more niche than production-focused modes.

**Independent Test**: Can be tested by enabling feedback and verifying that output exhibits cross-level energy transfer that creates evolving textures.

**Acceptance Scenarios**:

1. **Given** FractalDistortion in Feedback mode with feedbackAmount=0.3, **When** audio is processed, **Then** iteration levels exhibit sample-to-sample energy cross-feeding (level[N-1] from previous sample feeds into level[N])
2. **Given** FractalDistortion in Feedback mode with feedbackAmount=0.0, **When** audio is processed, **Then** output is equivalent to Residual mode (no cross-feeding)
3. **Given** FractalDistortion in Feedback mode with feedbackAmount=0.5 (maximum), **When** audio is processed continuously, **Then** output remains bounded (no runaway feedback)

---

### User Story 6 - Frequency Decay for Brightness Control (Priority: P3)

A mix engineer wants the deeper fractal iterations to emphasize high frequencies, adding "air" and presence without affecting the fundamental. They enable frequencyDecay to apply progressive highpass filtering at deeper levels.

**Why this priority**: Frequency decay is a modifier that works across all modes, providing tonal shaping for the fractal structure. Important for polish but not core functionality.

**Independent Test**: Can be tested by enabling frequencyDecay and verifying that deeper iterations are progressively highpass-filtered.

**Acceptance Scenarios**:

1. **Given** FractalDistortion with frequencyDecay=0.5, **When** processing with iterations=4, **Then** level 4 is highpass-filtered at a higher frequency than level 2
2. **Given** FractalDistortion with frequencyDecay=0.0, **When** audio is processed, **Then** no highpass filtering is applied to any level (full frequency content)

---

### Edge Cases

- What happens when iterations is set below minimum? System clamps to 1 (minimum valid iteration count per FR-011)
- What happens when drive=0? Output is zero (no signal passes through)
- What happens when mix=0? Dry signal passes through unchanged (bit-exact pass-through)
- What happens when NaN/Inf input is received? System resets internal state and returns 0.0
- What happens when sample rate changes? prepare() must be called to reconfigure internal filters
- What happens when iterations is changed during processing? Change applies immediately but may cause minor discontinuity (smoothing recommended at higher layer)

## Requirements *(mandatory)*

### Functional Requirements

**Core Lifecycle:**
- **FR-001**: System MUST provide prepare(sampleRate, maxBlockSize) for initialization
- **FR-002**: System MUST provide reset() to clear all internal state without reallocation
- **FR-003**: System MUST support sample rates from 44100Hz to 192000Hz

**Mode Selection:**
- **FR-004**: System MUST provide setMode(FractalMode) to select processing algorithm
- **FR-005**: System MUST support FractalMode::Residual (classic residual-based recursion)
- **FR-006**: System MUST support FractalMode::Multiband (octave-band splitting with scaled iterations)
- **FR-007**: System MUST support FractalMode::Harmonic (odd/even harmonic separation)
- **FR-008**: System MUST support FractalMode::Cascade (different waveshaper per level)
- **FR-009**: System MUST support FractalMode::Feedback (cross-level feedback with delay)

**Iteration Control:**
- **FR-010**: System MUST provide setIterations(int) to control recursion depth
- **FR-011**: Iterations MUST be clamped to range [1, 8] (values ≤0 clamp to 1, values >8 clamp to 8)
- **FR-012**: Each iteration level MUST apply progressively smaller amplitude contribution: level N scaled by scaleFactor^N (see FR-015)

**Scale Factor:**
- **FR-013**: System MUST provide setScaleFactor(float) for amplitude reduction per level
- **FR-014**: Scale factor MUST be clamped to range [0.3, 0.9]
- **FR-015**: Level N contribution MUST be scaled by scaleFactor^N

**Drive Control:**
- **FR-016**: System MUST provide setDrive(float) for base distortion intensity
- **FR-017**: Drive MUST be clamped to range [1.0, 20.0]
- **FR-018**: Drive changes MUST be smoothed to prevent clicks (10ms smoothing time; click-free defined as no single-sample amplitude delta exceeding 0.1 during transition)

**Mix Control:**
- **FR-019**: System MUST provide setMix(float) for dry/wet balance
- **FR-020**: Mix MUST be clamped to range [0.0, 1.0]
- **FR-021**: Mix=0.0 MUST return bit-exact dry signal (no processing overhead)
- **FR-022**: Mix changes MUST be smoothed to prevent clicks

**Frequency Decay (All Modes):**
- **FR-023**: System MUST provide setFrequencyDecay(float) for high-frequency emphasis at deeper levels
- **FR-024**: Frequency decay MUST be clamped to range [0.0, 1.0]
- **FR-025**: When frequencyDecay > 0, level N MUST be highpass-filtered at baseFrequency * (N+1) where baseFrequency = 200Hz, using 2nd-order Biquad highpass with Q=0.707 (Butterworth response)

**Base Saturation:**
- **FR-026**: Residual, Multiband, and Feedback modes MUST use Tanh waveshape for base saturation function

**Residual Mode Algorithm:**
- **FR-027**: Residual mode MUST compute: level[0] = tanh(input * drive)
- **FR-028**: Residual mode MUST compute: level[N] = tanh((input - sum(level[0..N-1])) * scaleFactor^N * drive)
- **FR-029**: Residual mode MUST sum all levels for final output

**Multiband Mode:**
- **FR-030**: Multiband mode MUST split input into 4 frequency bands using Linkwitz-Riley 4th-order (LR4) crossovers via `Crossover4Way` with frequencies: subLow=crossover/4, lowMid=crossover, midHigh=crossover*4 (pseudo-octave spacing optimized for bass/mid/high separation)
- **FR-030a**: Band indexing: Band[0]=sub (lowest), Band[1]=low, Band[2]=mid, Band[3]=high (highest)
- **FR-031**: System MUST provide setCrossoverFrequency(float) for base band split (default 250Hz, yielding bands at 62.5Hz, 250Hz, 1000Hz)
- **FR-032**: System MUST provide setBandIterationScale(float) to control iteration reduction at lower bands (default 0.5, clamped to [0.0, 1.0])
- **FR-033**: Higher bands MUST receive more iterations: bandIterations[i] = max(1, round(baseIterations * bandIterationScale^(numBands - 1 - i)))

**Harmonic Mode:**
- **FR-034**: Harmonic mode MUST separate odd harmonics using Chebyshev polynomial extraction
- **FR-035**: Harmonic mode MUST separate even harmonics using Chebyshev polynomial extraction
- **FR-036**: System MUST provide setOddHarmonicCurve(WaveshapeType) and setEvenHarmonicCurve(WaveshapeType) for curve selection
- **FR-037**: Harmonic mode odd/even curves MUST default to Tanh (odd) and Tube (even)
- **FR-038**: Harmonic mode MUST apply the selected Waveshaper curves to the Chebyshev-extracted odd and even harmonic components independently (ChebyshevShaper extracts harmonics, then Waveshaper applies saturation curve)

**Cascade Mode:**
- **FR-039**: System MUST provide setLevelWaveshaper(int level, WaveshapeType type) for per-level algorithm
- **FR-040**: Cascade mode MUST apply waveshaper[N] at iteration level N
- **FR-041**: Invalid level indices (outside [0, iterations-1]) MUST be safely ignored

**Feedback Mode:**
- **FR-042**: System MUST provide setFeedbackAmount(float) clamped to [0.0, 0.5]
- **FR-043**: Feedback mode MUST store previous iteration outputs in a per-level buffer (std::array<float, kMaxIterations>) for cross-level feedback (no time-based delay - feedback is from previous sample's iteration outputs)
- **FR-044**: Feedback mode MUST cross-feed previous sample's level[N-1] output into current sample's level[N] computation: level[N] = tanh((residual + feedbackBuffer[N-1] * feedbackAmount) * scaleFactor^N * drive)
- **FR-045**: Feedback mode MUST maintain bounded output by applying soft-limit tanh to final sum

**Processing:**
- **FR-046**: System MUST provide [[nodiscard]] float process(float input) noexcept for single-sample processing, returning the processed output sample
- **FR-047**: System MUST provide void process(float* buffer, size_t numSamples) noexcept for in-place block processing
- **FR-048**: All processing MUST be real-time safe (noexcept, no allocations, no blocking operations)
- **FR-049**: System MUST flush denormals after each saturate() call using flushDenormal() from core/db_utils.h to prevent CPU spikes

**DC Blocking:**
- **FR-050**: System MUST apply DC blocking after processing (asymmetric saturation generates DC)

### Key Entities

- **FractalMode**: Enumeration of processing algorithm modes (Residual, Multiband, Harmonic, Cascade, Feedback)
- **WaveshapeType**: Existing enumeration from waveshaper.h (Tanh, Atan, Cubic, Tube, etc.)
- **Level State**: Per-iteration state including waveshaper instance, highpass filter (for frequencyDecay), and output accumulator
- **Feedback Buffer**: std::array<float, kMaxIterations> storing previous sample's level outputs for cross-feeding

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Processing 8 iterations at 44.1kHz with 512-sample buffer completes within 0.5% CPU budget per channel (measure with platform profiler: Instruments on macOS, Very Sleepy on Windows, perf on Linux)
- **SC-002**: Residual mode with iterations=4 produces 4 distinct harmonic layers verifiable via FFT analysis (each iteration adds progressively smaller harmonic amplitude peaks)
- **SC-003**: Multiband mode maintains phase coherence: sum of all 4 Crossover4Way bands produces frequency response flat within ±0.5dB from 20Hz-20kHz (Linkwitz-Riley flat-sum property)
- **SC-004**: Mix=0.0 produces bit-exact dry signal output (memcmp equality with input)
- **SC-005**: Drive parameter changes produce click-free audio: no single-sample amplitude delta exceeds 0.1 during 10ms transition
- **SC-006**: Feedback mode with feedbackAmount=0.5 remains bounded: peak output amplitude never exceeds 4x peak input amplitude (12dB) over 1-second test signal
- **SC-007**: All modes handle NaN/Inf input gracefully (reset internal state and return 0.0)
- **SC-008**: Frequency decay at 1.0 results in level 8 being highpass-filtered at 1600Hz (200Hz × 8)
- **SC-009**: Cascade mode with 8 different waveshaper types produces distinct harmonic signatures per level (verifiable via spectral analysis showing different harmonic ratios)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Users have audio processing context with valid sample rate before calling prepare()
- Input audio is normalized to [-1.0, 1.0] range
- Aliasing is acceptable as this is "Digital Destruction" aesthetic (per DST-ROADMAP)
- No internal oversampling (handled at higher layer if needed)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | Direct reuse for saturation at each iteration level |
| WaveshapeType | dsp/include/krate/dsp/primitives/waveshaper.h | Reuse enum for Cascade mode waveshaper selection |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | Reuse for highpass filtering (frequencyDecay) |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | Direct reuse for post-processing DC removal |
| Crossover4Way | dsp/include/krate/dsp/processors/crossover_filter.h | Direct reuse for Multiband mode band splitting with Linkwitz-Riley crossovers |
| ChebyshevShaper | dsp/include/krate/dsp/primitives/chebyshev_shaper.h | Reuse for Harmonic mode odd/even extraction |
| Chebyshev::Tn | dsp/include/krate/dsp/core/chebyshev.h | Reuse for arbitrary harmonic polynomial evaluation |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | Reuse for parameter smoothing (drive, mix) |
| GranularDistortion | dsp/include/krate/dsp/processors/granular_distortion.h | Reference implementation for processor patterns |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "class.*Fractal" dsp/ plugins/
grep -r "Residual" dsp/ plugins/
grep -r "FractalMode" dsp/ plugins/
```

**Search Results Summary**: No existing FractalDistortion or FractalMode implementations found. All required primitives (Waveshaper, Biquad, DCBlocker, Crossover, Chebyshev, DelayLine) already exist.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- SpectralDistortion - may share multiband/frequency-domain concepts
- FormantDistortion - may benefit from harmonic separation utilities
- TemporalDistortion - different focus but similar processor structure

**Potential shared components** (preliminary, refined in plan.md):
- Multi-iteration saturation loop pattern could be extracted if other processors need recursive processing
- Harmonic separation (odd/even via Chebyshev) could become a shared utility if Harmonic mode proves useful
- Feedback crossfade pattern from Feedback mode could be reused in other feedback-based effects

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
| FR-028 | | |
| FR-029 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-034 | | |
| FR-035 | | |
| FR-036 | | |
| FR-037 | | |
| FR-038 | | |
| FR-039 | | |
| FR-040 | | |
| FR-041 | | |
| FR-042 | | |
| FR-043 | | |
| FR-044 | | |
| FR-045 | | |
| FR-046 | | |
| FR-047 | | |
| FR-048 | | |
| FR-049 | | |
| FR-050 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |

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
