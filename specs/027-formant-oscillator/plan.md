# Implementation Plan: FOF Formant Oscillator

**Branch**: `027-formant-oscillator` | **Date**: 2026-02-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/027-formant-oscillator/spec.md`

## Summary

FOF (Fonction d'Onde Formantique) oscillator generating vowel-like sounds directly through summed damped sinusoidal grains. Implements 5 parallel formant generators (F1-F5), each with fixed-size grain pools (8 grains per formant), 3ms attack rise, 20ms grain duration, and 0.4 master gain. Supports vowel presets, continuous morphing, and per-formant frequency/bandwidth/amplitude control.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 (`math_constants.h`, `phase_utils.h`, `filter_tables.h`), Layer 1 (none required)
**Storage**: N/A (stateful DSP, in-memory only)
**Testing**: Catch2 (`dsp/tests/unit/processors/`)
**Target Platform**: Windows, macOS, Linux (cross-platform VST3)
**Project Type**: Monorepo DSP library
**Performance Goals**: < 0.5% CPU per instance @ 44.1kHz mono (SC-005, Layer 2 budget)
**Constraints**: Zero allocations in audio thread, no exceptions, noexcept processing
**Scale/Scope**: Single DSP processor at Layer 2

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in process() methods
- [x] All processing methods marked noexcept
- [x] No exceptions, locks, or I/O in audio path
- [x] Grain pools are fixed-size (8 grains x 5 formants = 40 grains max)

**Required Check - Principle IX (Layer Architecture):**
- [x] Component is Layer 2 (processors/)
- [x] Only depends on Layer 0 (core/)
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

### Mandatory Searches Performed

**Classes/Structs to be created**:
- `FormantOscillator` - main oscillator class
- `VowelPreset` - enum for vowel selection (alias to existing Vowel)
- `FOFGrain` - internal grain state struct
- `FormantData5` - extended formant data with F4/F5

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FormantOscillator | `grep -r "class FormantOscillator" dsp/ plugins/` | No (only in OSC-ROADMAP) | Create New |
| VowelPreset | `grep -r "enum.*VowelPreset" dsp/ plugins/` | No | Create as alias to existing Vowel enum |
| FOFGrain | `grep -r "struct FOFGrain" dsp/ plugins/` | No | Create New (internal to FormantOscillator) |
| FormantData5 | `grep -r "struct FormantData5" dsp/ plugins/` | No | Create New (extends existing FormantData pattern) |

**Note on VowelPreset vs Vowel**: The existing `Vowel` enum in `filter_tables.h` matches the spec's `VowelPreset` exactly (A, E, I, O, U). The spec mentions using `VowelPreset` for consistency with FormantFilter. We can either:
1. Use `using VowelPreset = Vowel;` as a type alias
2. Use `Vowel` directly (matching FormantFilter)

Decision: Use existing `Vowel` enum directly for API consistency with FormantFilter.

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| calculateDecayConstant | `grep -r "calculateDecayConstant\|decayConstant" dsp/` | No | N/A | Create inline in class |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Vowel enum | `dsp/include/krate/dsp/core/filter_tables.h` | 0 | Direct reuse for vowel preset type |
| FormantData struct | `dsp/include/krate/dsp/core/filter_tables.h` | 0 | Reference for data structure pattern |
| kVowelFormants table | `dsp/include/krate/dsp/core/filter_tables.h` | 0 | Reference for F1-F3 data (extend for F4-F5) |
| PhaseAccumulator | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Fundamental phase tracking |
| kPi, kTwoPi | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Envelope and sinusoid calculations |
| detail::flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Output sanitization |
| detail::isNaN | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input validation |

**Note on GrainPool**: The existing `GrainPool` in `primitives/grain_pool.h` is designed for granular delay (reads from delay buffer). FOF grains are fundamentally different - they generate damped sinusoids directly without reading from a buffer. The existing `Grain` struct has fields for `readPosition`, `playbackRate`, etc. that don't apply to FOF. A new internal `FOFGrain` struct is more appropriate.

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (FormantFilter exists, different approach)
- [x] `specs/_architecture_/` - Component inventory
- [x] `specs/OSC-ROADMAP.md` - Oscillator roadmap (Phase 13)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The planned `FormantOscillator` class is a unique name not found in the codebase. The internal `FOFGrain` struct is private to the class. The existing `Vowel` enum will be reused rather than duplicated. The `FormantData5` struct extends the existing pattern without conflicting.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PhaseAccumulator | advance | `[[nodiscard]] bool advance() noexcept` | Yes |
| PhaseAccumulator | reset | `void reset() noexcept` | Yes |
| PhaseAccumulator | setFrequency | `void setFrequency(float frequency, float sampleRate) noexcept` | Yes |
| PhaseAccumulator | phase | `double phase = 0.0;` (member) | Yes |
| Vowel | enum values | `enum class Vowel : uint8_t { A = 0, E = 1, I = 2, O = 3, U = 4 }` | Yes |
| kNumVowels | constant | `inline constexpr size_t kNumVowels = 5;` | Yes |
| kVowelFormants | table | `inline constexpr std::array<FormantData, kNumVowels> kVowelFormants` | Yes |
| FormantData | members | `float f1, f2, f3, bw1, bw2, bw3` | Yes |
| kPi | constant | `inline constexpr float kPi` | Yes |
| kTwoPi | constant | `inline constexpr float kTwoPi` | Yes |
| detail::flushDenormal | function | `inline constexpr float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | function | `constexpr bool isNaN(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/phase_utils.h` - PhaseAccumulator struct
- [x] `dsp/include/krate/dsp/core/filter_tables.h` - Vowel enum, FormantData, kVowelFormants
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PhaseAccumulator | Uses double precision for phase | `static_cast<float>(acc.phase)` for sinusoid |
| kVowelFormants | Only has F1-F3, need F4-F5 | Create extended table `kVowelFormants5` |
| FormantData | Only 3 formants | Create `FormantData5` with 5 formants |
| Vowel enum | Cast to size_t for indexing | `static_cast<size_t>(vowel)` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| Extended formant data (F4-F5) | Could be used by FormantFilter if extended | filter_tables.h | FormantOscillator, potentially FormantFilter |

**Decision**: Create `FormantData5` and `kVowelFormants5` in the FormantOscillator header initially. If FormantFilter needs F4-F5 in the future, extract to filter_tables.h at that time. This follows the "wait for concrete evidence" principle.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| computeFOFEnvelope | Tightly coupled to grain processing, not general-purpose |
| computeDecayConstant | Simple formula, inlined |
| triggerGrain | Internal grain management |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (processors/)

**Related features at same layer** (from OSC-ROADMAP.md):
- Phase 14: Particle/Swarm Oscillator - Many lightweight sines, different architecture
- Phase 15: Rungler/Shift Register - Digital chaos, no shared components
- Phase 16: Spectral Freeze Oscillator - FFT-based, no shared components

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FormantData5 struct | MEDIUM | Potentially enhanced FormantFilter | Keep local, extract if FormantFilter needs 5 formants |
| FOFGrain struct | LOW | Specific to FOF technique | Keep local (internal) |
| kVowelFormants5 table | MEDIUM | Potentially enhanced FormantFilter | Keep local, extract if needed |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared grain base class | FOFGrain fundamentally different from existing Grain (generates vs reads) |
| Use existing Vowel enum | API consistency with FormantFilter |
| Keep extended formant data local | Only one consumer currently; extract when second emerges |

### Review Trigger

After implementing **enhanced FormantFilter (5 formants)**, review this section:
- [ ] Does FormantFilter need F4/F5? -> Extract FormantData5 to filter_tables.h
- [ ] Does FormantFilter need same vowel morphing? -> Extract shared interpolation

## Project Structure

### Documentation (this feature)

```text
specs/027-formant-oscillator/
├── spec.md              # Feature specification (complete)
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (N/A for DSP - no external API)
└── tasks.md             # Phase 2 output
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── formant_oscillator.h    # Main implementation (FR-001 to FR-019)
└── tests/
    └── unit/
        └── processors/
            └── formant_oscillator_test.cpp  # Unit tests (SC-001 to SC-008)
```

**Structure Decision**: Single header-only implementation at Layer 2 processors, following the established pattern of other oscillators (AdditiveOscillator, ChaosOscillator, PhaseDistortionOscillator). Test file co-located with other processor tests.

## Complexity Tracking

> No Constitution Check violations requiring justification.

N/A - All constitution principles are satisfied by the design.

---

## Phase 0: Research

### Research Tasks

1. **FOF Grain Envelope Implementation**
   - Half-cycle raised cosine attack over 3ms
   - Exponential decay controlled by bandwidth
   - Efficient computation without expensive functions

2. **Grain Pool Management Patterns**
   - Fixed 8-grain pool per formant
   - Oldest-grain recycling strategy
   - Sample counter for grain age tracking

3. **Efficient Damped Sinusoid Computation**
   - Phase accumulation for formant frequency
   - Per-sample envelope multiplication
   - Avoiding expensive exp() calls via incremental decay

4. **Testing Strategies for Formant Synthesis**
   - Spectral peak detection for formant verification
   - Morphing continuity tests
   - Output boundedness verification

### Research Findings

See [research.md](research.md) for detailed findings.

**Key Decisions:**

| Decision | Rationale | Alternatives Rejected |
|----------|-----------|----------------------|
| Fixed 8-grain pool per formant | Matches spec requirement, supports ~12.5Hz minimum fundamental | Dynamic allocation (real-time unsafe) |
| Incremental exponential decay | `y[n] = y[n-1] * decayFactor` is O(1) per sample | `exp(-k*t)` per sample (expensive) |
| Pre-computed attack table | Faster than per-sample cosine; 133 samples @ 44.1kHz for 3ms | Per-sample raised cosine (more expensive) |
| Summed sinusoids with envelope | Direct FOF implementation | IFFT approach (overkill for 5 formants) |

---

## Phase 1: Design

### Data Model

See [data-model.md](data-model.md) for detailed entity definitions.

**Key Entities:**

```cpp
// Extended formant data with F4 and F5 (FR-005)
struct FormantData5 {
    float frequencies[5];   // F1, F2, F3, F4, F5 in Hz
    float bandwidths[5];    // BW1-BW5 in Hz
};

// Per-grain state
struct FOFGrain {
    float phase = 0.0f;           // Formant sinusoid phase [0, 1)
    float phaseIncrement = 0.0f;  // Phase advance per sample
    float envelope = 0.0f;        // Current envelope value
    float decayFactor = 0.0f;     // Exponential decay multiplier
    float amplitude = 1.0f;       // Grain amplitude
    size_t age = 0;               // Samples since trigger (for recycling)
    size_t attackSamples = 0;     // Total attack samples (3ms)
    size_t attackCounter = 0;     // Current attack position
    size_t durationSamples = 0;   // Total grain duration (20ms)
    size_t sampleCounter = 0;     // Current position in grain
    bool active = false;          // Is grain currently sounding
};

// Per-formant generator
struct FormantGenerator {
    std::array<FOFGrain, 8> grains;  // Fixed-size grain pool
    float frequency = 600.0f;         // Current formant frequency
    float bandwidth = 60.0f;          // Current bandwidth
    float amplitude = 1.0f;           // Current amplitude
};

// Main oscillator class
class FormantOscillator {
    static constexpr size_t kNumFormants = 5;
    static constexpr size_t kGrainsPerFormant = 8;
    static constexpr float kAttackMs = 3.0f;
    static constexpr float kGrainDurationMs = 20.0f;
    static constexpr float kMasterGain = 0.4f;

    std::array<FormantGenerator, kNumFormants> formants_;
    PhaseAccumulator fundamentalPhase_;
    // ... (see data-model.md for complete definition)
};
```

### Vowel Preset Table (FR-005, extended)

```cpp
inline constexpr std::array<FormantData5, 5> kVowelFormants5 = {{
    // A: /a/ as in "father"
    {{600.0f, 1040.0f, 2250.0f, 2450.0f, 2750.0f},
     {60.0f, 70.0f, 110.0f, 120.0f, 130.0f}},
    // E: /e/ as in "bed"
    {{400.0f, 1620.0f, 2400.0f, 2800.0f, 3100.0f},
     {40.0f, 80.0f, 100.0f, 120.0f, 120.0f}},
    // I: /i/ as in "see"
    {{250.0f, 1750.0f, 2600.0f, 3050.0f, 3340.0f},
     {60.0f, 90.0f, 100.0f, 120.0f, 120.0f}},
    // O: /o/ as in "go"
    {{400.0f, 750.0f, 2400.0f, 2600.0f, 2900.0f},
     {40.0f, 80.0f, 100.0f, 120.0f, 120.0f}},
    // U: /u/ as in "boot"
    {{350.0f, 600.0f, 2400.0f, 2675.0f, 2950.0f},
     {40.0f, 80.0f, 100.0f, 120.0f, 120.0f}},
}};

// Default amplitude scaling (FR-006)
inline constexpr std::array<float, 5> kDefaultFormantAmplitudes = {
    1.0f,  // F1: full
    0.8f,  // F2: slightly reduced
    0.5f,  // F3: moderate
    0.3f,  // F4: quieter
    0.2f   // F5: quietest
};
```

### API Contracts

N/A - This is a DSP primitive with no external API contracts. The public interface is defined in the spec (FR-015 to FR-019).

### Quickstart

See [quickstart.md](quickstart.md) for usage examples.

```cpp
// Basic usage
FormantOscillator osc;
osc.prepare(44100.0);
osc.setFundamental(110.0f);  // A2, male voice range
osc.setVowel(Vowel::A);

// Process single sample
float sample = osc.process();

// Process block
float buffer[512];
osc.processBlock(buffer, 512);

// Vowel morphing
osc.morphVowels(Vowel::A, Vowel::O, 0.5f);  // 50% blend

// Position-based morphing (0=A, 1=E, 2=I, 3=O, 4=U)
osc.setMorphPosition(1.5f);  // Between E and I

// Per-formant control
osc.setFormantFrequency(0, 800.0f);   // Custom F1
osc.setFormantBandwidth(1, 200.0f);   // Wider F2
osc.setFormantAmplitude(2, 0.0f);     // Disable F3
```

---

## Implementation Phases

### Phase N-1.0: Quality Gate (Pre-Implementation)

**Tasks:**
- [ ] Run clang-tidy on related files (filter_tables.h, phase_utils.h)
- [ ] Verify all existing tests pass
- [ ] Review spec one more time for completeness
- [ ] Create git branch `027-formant-oscillator`

### Phase N.0: Test Infrastructure

**Tasks:**
1. Create test file `dsp/tests/unit/processors/formant_oscillator_test.cpp`
2. Add test file to CMakeLists.txt
3. Write test helper functions (spectral analysis, formant peak detection)
4. Write test stubs for all FR-xxx and SC-xxx requirements
5. Verify test file compiles (tests should fail initially)

### Phase N.1: Core Data Structures

**Tasks (TDD cycle):**

1. **FormantData5 and Vowel Tables (FR-005, FR-006)**
   - Write test: vowel A data matches spec values
   - Implement `FormantData5` struct and `kVowelFormants5` table
   - Write test: all 5 vowels have correct F1-F5 values
   - Implement default amplitude array

2. **FOFGrain Structure**
   - Write test: grain initialization sets expected values
   - Implement `FOFGrain` struct
   - Write test: grain active state management

3. **FormantGenerator Structure**
   - Write test: generator initialization
   - Implement `FormantGenerator` with 8-grain array

### Phase N.2: FOF Envelope Generation (FR-001, FR-004)

**Tasks (TDD cycle):**

1. **Attack Envelope (3ms raised cosine)**
   - Write test: attack rises from 0 to 1 over 3ms
   - Implement raised cosine attack: `0.5 * (1 - cos(pi * t / riseTime))`
   - Write test: verify half-cycle shape

2. **Decay Envelope (exponential from bandwidth)**
   - Write test: decay constant calculation from bandwidth
   - Implement `decayConstant = pi * bandwidthHz`
   - Write test: decay factor per sample
   - Implement incremental decay: `decayFactor = exp(-decayConstant / sampleRate)`

3. **Complete Envelope**
   - Write test: envelope shape over 20ms grain duration
   - Implement combined attack + decay envelope
   - Write test: envelope correctly terminates

### Phase N.3: Grain Triggering and Management (FR-002, FR-003)

**Tasks (TDD cycle):**

1. **Fundamental Phase Tracking (FR-002)**
   - Write test: phase accumulator advances correctly
   - Implement fundamental phase using `PhaseAccumulator`
   - Write test: phase wrap detection for grain trigger

2. **Grain Triggering**
   - Write test: grain triggers at fundamental zero-crossing
   - Implement grain trigger on phase wrap
   - Write test: all 5 formants trigger simultaneously

3. **Grain Pool Management**
   - Write test: oldest grain recycling when pool exhausted
   - Implement grain age tracking and recycling
   - Write test: 8 grains per formant limit enforced

### Phase N.4: FOF Grain Processing (FR-001)

**Tasks (TDD cycle):**

1. **Damped Sinusoid Generation**
   - Write test: sinusoid at formant frequency
   - Implement phase accumulation for formant frequency
   - Write test: envelope applied to sinusoid

2. **Single Grain Processing**
   - Write test: grain outputs bounded values
   - Implement grain process: `output = amplitude * envelope * sin(2*pi*phase)`
   - Write test: grain completes after 20ms

3. **Multi-Grain Summing**
   - Write test: multiple overlapping grains sum correctly
   - Implement grain pool summing per formant
   - Write test: formant output is sum of active grains

### Phase N.5: Vowel Selection and Morphing (FR-005 to FR-008)

**Tasks (TDD cycle):**

1. **Discrete Vowel Selection (FR-005, FR-017)**
   - Write test: setVowel(A) produces A formant frequencies
   - Implement `setVowel()` method
   - Write test: each vowel produces distinct spectrum

2. **Two-Vowel Morphing (FR-007)**
   - Write test: morphVowels(A, O, 0.5) produces midpoint frequencies
   - Implement linear interpolation of frequencies and bandwidths
   - Write test: mix=0 is pure "from", mix=1 is pure "to"

3. **Position-Based Morphing (FR-008)**
   - Write test: position 0.0 = A, 1.0 = E, etc.
   - Implement `setMorphPosition()` method
   - Write test: fractional positions interpolate correctly

### Phase N.6: Per-Formant Control (FR-009 to FR-011)

**Tasks (TDD cycle):**

1. **Frequency Control (FR-009)**
   - Write test: setFormantFrequency(0, 800) places F1 at 800Hz
   - Implement per-formant frequency setter
   - Write test: frequency clamping to valid range

2. **Bandwidth Control (FR-010)**
   - Write test: setFormantBandwidth changes spectral width
   - Implement per-formant bandwidth setter
   - Write test: bandwidth clamping (10-500Hz)

3. **Amplitude Control (FR-011)**
   - Write test: setFormantAmplitude(2, 0) disables F3
   - Implement per-formant amplitude setter
   - Write test: amplitude clamping (0-1)

### Phase N.7: Pitch and Output (FR-012 to FR-014)

**Tasks (TDD cycle):**

1. **Fundamental Frequency Control (FR-012, FR-013)**
   - Write test: setFundamental(110) produces 110Hz pitch
   - Implement fundamental frequency setter
   - Write test: formants remain fixed when fundamental changes

2. **Output Normalization (FR-014)**
   - Write test: output bounded with master gain 0.4
   - Implement master gain application
   - Write test: 10 seconds continuous output stays in bounds

### Phase N.8: Interface Completion (FR-015 to FR-019)

**Tasks (TDD cycle):**

1. **Lifecycle Methods (FR-015, FR-016)**
   - Write test: prepare() configures sample rate
   - Implement `prepare(double sampleRate)` method
   - Write test: reset() clears grain states
   - Implement `reset()` method

2. **Processing Methods (FR-018, FR-019)**
   - Write test: process() returns valid sample
   - Implement single-sample `process()` method
   - Write test: processBlock() fills buffer correctly
   - Implement `processBlock()` method

3. **Documentation**
   - Add Doxygen comments to all public methods
   - Update architecture docs

### Phase N+1.0: Quality Gate (Post-Implementation)

**Tasks:**
- [ ] All tests pass
- [ ] Run clang-tidy, fix all warnings
- [ ] Run pluginval (if plugin integration)
- [ ] Update `specs/_architecture_/layer-2-processors.md`
- [ ] Fill compliance table in spec.md

---

## Success Criteria Verification Plan

| SC | Verification Method | Test Name |
|----|---------------------|-----------|
| SC-001 | FFT analysis of vowel A @ 110Hz, check F1/F2/F3 peaks | `VowelASpectralPeaks` |
| SC-002 | Morph position 0.5 (A to E), verify F1 ~500Hz | `MorphMidpointF1` |
| SC-003 | setFormantFrequency(0, 800), verify spectral peak | `PerFormantFrequencyAccuracy` |
| SC-004 | 10 second processing at various settings, check bounds | `OutputBoundedness10Seconds` |
| SC-005 | Benchmark CPU usage at 44.1kHz | `CPUBudget` |
| SC-006 | FFT compare vowel I (F2~1750Hz) vs vowel U (F2~600Hz) | `VowelSpectralDistinction` |
| SC-007 | FFT analysis at 110Hz, 220Hz, 440Hz, verify harmonics | `FundamentalHarmonicAccuracy` |
| SC-008 | Set BW=100Hz, measure -6dB width in spectrum | `BandwidthMeasurement` |

---

## Files to Create/Modify

### New Files

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/formant_oscillator.h` | Main implementation |
| `dsp/tests/unit/processors/formant_oscillator_test.cpp` | Unit tests |
| `specs/027-formant-oscillator/research.md` | Research findings |
| `specs/027-formant-oscillator/data-model.md` | Data model |
| `specs/027-formant-oscillator/quickstart.md` | Usage guide |

### Modified Files

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add formant_oscillator_test.cpp |
| `specs/_architecture_/layer-2-processors.md` | Add FormantOscillator entry |
| `specs/OSC-ROADMAP.md` | Update Phase 13 status |

---

## Algorithm Details

### FOF Envelope Computation

The envelope consists of two phases:

**Attack (0 to attackSamples):**
```cpp
// Half-cycle raised cosine (3ms at 44.1kHz = 133 samples)
float t_normalized = static_cast<float>(attackCounter) / static_cast<float>(attackSamples);
float envelope = 0.5f * (1.0f - std::cos(kPi * t_normalized));
```

**Decay (attackSamples to durationSamples):**
```cpp
// Exponential decay: decayFactor = exp(-decayConstant / sampleRate)
// where decayConstant = pi * bandwidthHz
// Applied incrementally: envelope *= decayFactor each sample
float decayConstant = kPi * bandwidthHz;
float decayFactor = std::exp(-decayConstant / static_cast<float>(sampleRate));
// In process loop:
envelope *= decayFactor;
```

### Grain Triggering Logic

```cpp
// In process():
if (fundamentalPhase_.advance()) {  // Returns true on phase wrap (0-crossing)
    triggerGrains();  // Trigger new grain in each formant
}
```

### Oldest-Grain Recycling

```cpp
Grain* findOldestGrain(std::array<FOFGrain, 8>& pool) {
    FOFGrain* oldest = nullptr;
    size_t maxAge = 0;
    for (auto& grain : pool) {
        if (grain.active && grain.age > maxAge) {
            maxAge = grain.age;
            oldest = &grain;
        }
    }
    if (oldest == nullptr) {
        // All inactive, use first slot
        return &pool[0];
    }
    return oldest;
}
```

### Grain Processing

```cpp
float processGrain(FOFGrain& grain) {
    if (!grain.active) return 0.0f;

    // Compute envelope
    float env = 0.0f;
    if (grain.sampleCounter < grain.attackSamples) {
        // Attack phase
        float t = static_cast<float>(grain.sampleCounter) /
                  static_cast<float>(grain.attackSamples);
        env = 0.5f * (1.0f - std::cos(kPi * t));
    } else {
        // Decay phase
        env = grain.envelope;
        grain.envelope *= grain.decayFactor;
    }

    // Generate damped sinusoid
    float sinValue = std::sin(kTwoPi * grain.phase);
    float output = grain.amplitude * env * sinValue;

    // Advance state
    grain.phase += grain.phaseIncrement;
    if (grain.phase >= 1.0f) grain.phase -= 1.0f;
    grain.sampleCounter++;
    grain.age++;

    // Check completion
    if (grain.sampleCounter >= grain.durationSamples) {
        grain.active = false;
    }

    return output;
}
```
