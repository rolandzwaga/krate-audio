# Implementation Plan: Envelope Filter / Auto-Wah

**Branch**: `078-envelope-filter` | **Date**: 2026-01-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/078-envelope-filter/spec.md`

## Summary

Implement an EnvelopeFilter (Auto-Wah) Layer 2 processor that composes EnvelopeFollower with SVF for classic wah/envelope filter effects. The envelope of the input signal controls the SVF cutoff frequency using exponential mapping for perceptually linear sweeps. Key features: configurable attack/release, sensitivity gain for envelope detection, Up/Down direction modes, three filter types (Lowpass, Bandpass, Highpass), frequency range with depth control, and dry/wet mix.

## Technical Context

**Language/Version**: C++20 (per constitution III)
**Primary Dependencies**:
- EnvelopeFollower (Layer 2) - amplitude envelope tracking
- SVF (Layer 1) - resonant filtering with excellent modulation stability
- db_utils (Layer 0) - dbToGain for sensitivity conversion

**Storage**: N/A (stateless except for internal component state)
**Testing**: Catch2 via `dsp_tests` target (per CLAUDE.md)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library header
**Performance Goals**: < 100ns per sample (EnvelopeFollower ~10ns + SVF ~10ns + overhead)
**Constraints**: Real-time safe (noexcept, no allocations in process), header-only
**Scale/Scope**: Single class ~250-300 lines

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in process() - all state preallocated
- [x] No locks/mutexes - lock-free design
- [x] No exceptions - all methods noexcept
- [x] No I/O - pure computation

**Required Check - Principle III (Modern C++ Standards):**
- [x] C++20 target
- [x] RAII for all resource management (value types, no dynamic allocation)
- [x] constexpr and const used appropriately
- [x] Value semantics for composed components

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor can use Layer 0 (db_utils) and Layer 1 (SVF)
- [x] EnvelopeFollower is Layer 2 - EXCEPTION: Layer 2 can compose peer Layer 2 components when appropriate (common pattern: DuckingProcessor uses EnvelopeFollower, BitcrusherProcessor uses EnvelopeFollower)
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle X (DSP Processing Constraints):**
- [x] Denormal flushing via composed components (SVF and EnvelopeFollower handle this)
- [x] No DC blocking needed (SVF and EnvelopeFollower handle edge cases)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: EnvelopeFilter, Direction enum, FilterType enum

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| EnvelopeFilter | `grep -r "class EnvelopeFilter" dsp/ plugins/` | No | Create New |
| Direction (within EnvelopeFilter) | `grep -r "enum.*Direction" dsp/ plugins/` | No conflicts (scoped) | Create New |
| FilterType (within EnvelopeFilter) | `grep -r "enum.*FilterType" dsp/ plugins/` | Yes (biquad.h) | Scope within class to avoid conflict |

**Note**: `FilterType` exists in `biquad.h` but EnvelopeFilter::FilterType will be scoped within the class, avoiding ODR issues.

**Utility Functions to be created**: None (all needed utilities exist)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" dsp/` | Yes | db_utils.h | Reuse |
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | db_utils.h | Reuse (via composed components) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| EnvelopeFollower | dsp/include/krate/dsp/processors/envelope_follower.h | 2 | Composed for amplitude envelope tracking |
| SVF | dsp/include/krate/dsp/primitives/svf.h | 1 | Composed for resonant filtering |
| SVFMode | dsp/include/krate/dsp/primitives/svf.h | 1 | Map FilterType to SVFMode::Lowpass/Bandpass/Highpass |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Sensitivity dB to linear gain conversion |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory (README.md, layer-2-processors.md)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: EnvelopeFilter is a new unique type. Direction and FilterType enums are scoped within the class. All dependencies are existing components with stable APIs.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | reset | `void reset() noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setMode | `void setMode(DetectionMode mode) noexcept` | Yes |
| EnvelopeFollower | kMinAttackMs | `static constexpr float kMinAttackMs = 0.1f` | Yes |
| EnvelopeFollower | kMaxAttackMs | `static constexpr float kMaxAttackMs = 500.0f` | Yes |
| EnvelopeFollower | kMinReleaseMs | `static constexpr float kMinReleaseMs = 1.0f` | Yes |
| EnvelopeFollower | kMaxReleaseMs | `static constexpr float kMaxReleaseMs = 5000.0f` | Yes |
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | kMinQ | `static constexpr float kMinQ = 0.1f` | Yes |
| SVF | kMaxQ | `static constexpr float kMaxQ = 30.0f` | Yes |
| SVF | kMinCutoff | `static constexpr float kMinCutoff = 1.0f` | Yes |
| SVFMode | Lowpass | `SVFMode::Lowpass` | Yes |
| SVFMode | Bandpass | `SVFMode::Bandpass` | Yes |
| SVFMode | Highpass | `SVFMode::Highpass` | Yes |
| dbToGain | function | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain function

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| EnvelopeFollower | prepare takes maxBlockSize param (unused but required) | `envFollower_.prepare(sampleRate, 512)` |
| EnvelopeFollower | processSample not process (single sample) | `envFollower_.processSample(input)` |
| SVF | setResonance takes Q value, not bandwidth | `filter_.setResonance(q)` where Q in [0.5, 20] |
| SVF | Must call prepare before processing | `filter_.prepare(sampleRate)` in prepare() |
| SVFMode | Enum is scoped, requires explicit qualification | `SVFMode::Lowpass` not `Lowpass` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

**No extraction needed**: The exponential frequency mapping formula is 2-3 lines and specific to envelope-controlled filtering. If a second envelope-controlled effect needs it, consider extraction then.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateCutoff() | Single consumer, uses multiple member variables |
| Exponential freq mapping | 3 lines inline, specific to this processor |

**Decision**: No Layer 0 extraction. All utilities are simple enough to inline.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from layer-2-processors.md):
- FormantFilter (077) - Parallel bandpass filters for vowel sounds
- DynamicsProcessor - Compressor/limiter
- TiltEQ - Single-knob tonal shaping

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| EnvelopeFilter class | LOW | None identified | Keep local |
| Exponential freq mapping | LOW | Only if another envelope-controlled filter | Keep local, extract if 2nd consumer appears |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | EnvelopeFilter is unique composition of EnvelopeFollower+SVF |
| Keep freq mapping local | Simple formula, single consumer |

## Project Structure

### Documentation (this feature)

```text
specs/078-envelope-filter/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output
    └── envelope_filter_api.md
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── envelope_filter.h    # NEW: Header-only implementation
└── tests/
    └── processors/
        └── envelope_filter_test.cpp  # NEW: Catch2 tests
```

## Complexity Tracking

No constitution violations. Layer 2 composing Layer 2 (EnvelopeFollower) is an established pattern in this codebase (see BitcrusherProcessor, DuckingProcessor).

---

## Phase 0: Research (Complete)

### Research Tasks Completed

1. **EnvelopeFollower API**: Verified prepare/processSample/setAttackTime/setReleaseTime signatures
2. **SVF API**: Verified prepare/process/setMode/setCutoff/setResonance signatures
3. **SVFMode mapping**: Confirmed only Lowpass/Bandpass/Highpass needed (per spec clarification)
4. **dbToGain**: Verified constexpr noexcept signature
5. **Layer 2 composition pattern**: Confirmed valid (BitcrusherProcessor composes EnvelopeFollower)

### Clarifications Applied (from spec session)

1. **FilterType to SVFMode**: 3 modes only (Lowpass, Bandpass, Highpass) with explicit enum qualification
2. **Envelope clamping**: Always clamp to [0, 1] before frequency mapping - maxFrequency is hard ceiling
3. **minFreq >= maxFreq handling**: Clamp setters to maintain min < max always
4. **Sensitivity application**: Only affects envelope detection, original signal goes to filter
5. **depth = 0 behavior**: Cutoff fixed at starting position (minFreq for Up, maxFreq for Down)

---

## Phase 1: Design

### Data Model

See [data-model.md](data-model.md) for complete entity definitions.

Key entities:
- **EnvelopeFilter**: Main class composing EnvelopeFollower + SVF
- **Direction**: Enum { Up, Down } - controls envelope-to-cutoff mapping direction
- **FilterType**: Enum { Lowpass, Bandpass, Highpass } - maps to SVFMode

### API Contract

See [contracts/envelope_filter_api.md](contracts/envelope_filter_api.md) for complete API.

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

    // Getters for monitoring
    [[nodiscard]] float getCurrentCutoff() const noexcept;
    [[nodiscard]] float getCurrentEnvelope() const noexcept;
};
```

### Processing Algorithm

```
1. Apply sensitivity gain: gainedInput = input * dbToGain(sensitivity_)
2. Track envelope: envelope = envFollower_.processSample(gainedInput)
3. Clamp envelope: clampedEnv = std::clamp(envelope, 0.0f, 1.0f)
4. Calculate modulation: modAmount = clampedEnv * depth_
5. Map to frequency (exponential):
   - Up:   cutoff = minFreq_ * pow(maxFreq_/minFreq_, modAmount)
   - Down: cutoff = maxFreq_ * pow(minFreq_/maxFreq_, modAmount)
6. Update SVF cutoff: filter_.setCutoff(cutoff)
7. Filter original input: filtered = filter_.process(input)
8. Mix: output = input * (1 - mix_) + filtered * mix_
9. Return output
```

### Quickstart

See [quickstart.md](quickstart.md) for usage examples.

---

## Implementation Tasks (for /speckit.tasks)

### Task Group 1: Core Implementation (P1)

1. Create envelope_filter.h with class structure, enums, constants
2. Implement prepare() and reset()
3. Implement envelope parameter setters (sensitivity, attack, release, direction)
4. Implement filter parameter setters (filterType, minFreq, maxFreq, resonance, depth)
5. Implement mix setter
6. Implement process() with frequency mapping algorithm
7. Implement processBlock()
8. Implement getters (getCurrentCutoff, getCurrentEnvelope)

### Task Group 2: Tests (P1)

1. Test envelope tracking (SC-001, SC-002)
2. Test filter response (SC-004, SC-005, SC-006)
3. Test frequency sweep exponential mapping (SC-007, SC-008)
4. Test depth parameter (SC-003)
5. Test direction modes (SC-014)
6. Test mix parameter (SC-012, SC-013)
7. Test stability (SC-009, SC-010)
8. Test multi-sample-rate (SC-011)
9. Test edge cases (silent input, depth=0, freq clamping)
10. Test performance (SC-015)

### Task Group 3: Documentation (P2)

1. Add Doxygen comments to all public methods
2. Update specs/_architecture_/layer-2-processors.md
3. Fill compliance table in spec.md
