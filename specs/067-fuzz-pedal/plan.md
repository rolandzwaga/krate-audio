# Implementation Plan: Fuzz Pedal System

**Branch**: `067-fuzz-pedal` | **Date**: 2026-01-15 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/067-fuzz-pedal/spec.md`

## Summary

A Layer 3 system that composes the existing Layer 2 FuzzProcessor with input buffering (selectable high-pass cutoff), tone control, and a noise gate with configurable type and timing. The system provides complete fuzz pedal functionality with volume control and parameter smoothing for click-free operation.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: FuzzProcessor (Layer 2), Biquad (Layer 1), EnvelopeFollower (Layer 2), OnePoleSmoother (Layer 1)
**Storage**: N/A (real-time DSP, no persistent storage)
**Testing**: Catch2 (dsp_tests executable)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform
**Project Type**: DSP library extension (Layer 3 system)
**Performance Goals**: < 1% CPU per instance at 44.1kHz stereo
**Constraints**: Real-time safe (no allocations in process), noexcept processing
**Scale/Scope**: Single mono processor instance per channel

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in process() method
- [x] No locks, mutexes, or blocking primitives
- [x] All parameter changes use smoothers (5ms default)
- [x] All methods marked noexcept

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Layer 3 system - may depend on Layers 0, 1, 2
- [x] Will be placed in `dsp/include/krate/dsp/systems/fuzz_pedal.h`
- [x] Uses existing Layer 2 FuzzProcessor (not Layer 4)
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FuzzPedal | `grep -r "class FuzzPedal" dsp/ plugins/` | No (only in DST-ROADMAP.md) | Create New |
| GateType | `grep -r "enum.*GateType" dsp/ plugins/` | No | Create New |
| GateTiming | `grep -r "enum.*GateTiming" dsp/ plugins/` | No | Create New |
| BufferCutoff | `grep -r "enum.*BufferCutoff" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | - | - | - | All utilities exist |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FuzzProcessor | `dsp/include/krate/dsp/processors/fuzz_processor.h` | 2 | Core fuzz engine (composed) |
| FuzzType | `dsp/include/krate/dsp/processors/fuzz_processor.h` | 2 | Transistor type enum (re-exported) |
| EnvelopeFollower | `dsp/include/krate/dsp/processors/envelope_follower.h` | 2 | Gate envelope detection |
| Biquad | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | Input buffer high-pass filter |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Volume parameter smoothing |
| dbToGain | `dsp/include/krate/dsp/core/db_utils.h` | 0 | dB to linear conversion |
| crossfadeIncrement | `dsp/include/krate/dsp/core/crossfade_utils.h` | 0 | Gate type crossfade timing |
| equalPowerGains | `dsp/include/krate/dsp/core/crossfade_utils.h` | 0 | Gate type crossfade blending |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (FuzzProcessor exists, will compose)
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (no FuzzPedal)
- [x] `specs/_architecture_/` - Component inventory (FuzzPedal planned in DST-ROADMAP)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new types (FuzzPedal, GateType, GateTiming, BufferCutoff) are unique and not found in the codebase. The system follows the DST-ROADMAP design pattern exactly.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins. Prevents compile-time API mismatch errors.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FuzzProcessor | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| FuzzProcessor | reset | `void reset() noexcept` | Yes |
| FuzzProcessor | process | `void process(float* buffer, size_t numSamples) noexcept` | Yes |
| FuzzProcessor | setFuzzType | `void setFuzzType(FuzzType type) noexcept` | Yes |
| FuzzProcessor | setFuzz | `void setFuzz(float amount) noexcept` | Yes |
| FuzzProcessor | setVolume | `void setVolume(float dB) noexcept` | Yes |
| FuzzProcessor | setBias | `void setBias(float bias) noexcept` | Yes |
| FuzzProcessor | setTone | `void setTone(float tone) noexcept` | Yes |
| FuzzProcessor | getFuzzType | `[[nodiscard]] FuzzType getFuzzType() const noexcept` | Yes |
| FuzzProcessor | getFuzz | `[[nodiscard]] float getFuzz() const noexcept` | Yes |
| FuzzProcessor | getVolume | `[[nodiscard]] float getVolume() const noexcept` | Yes |
| FuzzProcessor | getBias | `[[nodiscard]] float getBias() const noexcept` | Yes |
| FuzzProcessor | getTone | `[[nodiscard]] float getTone() const noexcept` | Yes |
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | reset | `void reset() noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| dbToGain | function | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| crossfadeIncrement | function | `[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept` | Yes |
| equalPowerGains | function | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/fuzz_processor.h` - FuzzProcessor class
- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain function
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - crossfade utilities

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| FuzzProcessor | setVolume uses dB not linear | `fuzz_.setVolume(0.0f)` for unity |
| FuzzProcessor | FuzzProcessor has internal volume control | FuzzPedal volume is ADDITIONAL output volume |
| EnvelopeFollower | processSample returns envelope value | Use return value for gate gain calculation |
| Biquad | Q for Butterworth is kButterworthQ | Use `kButterworthQ` (0.7071) for high-pass |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Gate gain calculation | Specific to noise gate implementation, uses local envelope state |
| Buffer cutoff frequency mapping | Simple enum-to-Hz mapping, only needed here |

**Decision**: No new Layer 0 utilities needed. All required utilities already exist.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from DST-ROADMAP.md):
- DistortionRack: Chainable distortion system
- TapeMachine: Already implemented (v0.0.66)
- AmpChannel: Already implemented (v0.0.65)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| GateType enum | MEDIUM | DistortionRack (if gate added) | Keep local until 2nd use |
| GateTiming enum | MEDIUM | DistortionRack (if gate added) | Keep local until 2nd use |
| BufferCutoff enum | LOW | Input buffer is fuzz-specific concept | Keep local |
| Noise gate logic | MEDIUM | Could be extracted as NoiseGate processor | Keep local until 2nd use |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep GateType/GateTiming in fuzz_pedal.h | First consumer - extract if DistortionRack needs similar |
| No shared NoiseGate processor | Pattern not yet established - revisit after 2nd system needs it |
| BufferCutoff stays local | Input buffer with selectable cutoff is fuzz-specific |

### Review Trigger

After implementing **DistortionRack** (next distortion system), review this section:
- [ ] Does DistortionRack need similar gate types? -> Extract to common location
- [ ] Does DistortionRack use input buffer pattern? -> Document shared pattern
- [ ] Any duplicated noise gate code? -> Consider NoiseGate processor

## Project Structure

### Documentation (this feature)

```text
specs/067-fuzz-pedal/
├── plan.md              # This file
├── research.md          # Phase 0 output (not needed - all research complete)
├── data-model.md        # Phase 1 output (below)
├── quickstart.md        # Phase 1 output (below)
├── contracts/           # Phase 1 output (N/A - internal DSP, no API contracts)
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── systems/
│       └── fuzz_pedal.h           # NEW: FuzzPedal class + enums
└── tests/
    └── unit/
        └── systems/
            └── fuzz_pedal_test.cpp  # NEW: Test suite
```

**Structure Decision**: Single header file in Layer 3 systems following established patterns (AmpChannel, TapeMachine). Header-only implementation as per existing Layer 3 systems.

## Complexity Tracking

> No Constitution Check violations - all gates passed.

---

# Phase 0: Research

*All research completed during Technical Context analysis above.*

## Key Findings

### FuzzProcessor API Analysis
- FuzzProcessor has its own internal volume control (-24 to +24 dB)
- FuzzPedal spec calls for ADDITIONAL volume control at system level
- **Decision**: Set FuzzProcessor volume to 0dB (unity), use FuzzPedal volume for output control
- FuzzProcessor handles: fuzz amount, bias, tone, type crossfade (5ms)
- FuzzPedal adds: input buffer, noise gate, additional volume

### Noise Gate Design
- Spec requires three gate types: SoftKnee, HardGate, LinearRamp
- Gate uses EnvelopeFollower for level detection
- Gate timing presets: Fast (0.5ms/20ms), Normal (1ms/50ms), Slow (2ms/100ms)
- Gate type crossfade: 5ms (matching fuzz type behavior)

### Input Buffer Design
- High-pass filter using Biquad with selectable cutoff
- BufferCutoff enum: Hz5 (5Hz), Hz10 (10Hz), Hz20 (20Hz)
- Default: Hz10 (standard DC blocking)
- Butterworth Q (0.7071) for flat response

### Signal Flow (FR-025)
```
Input -> [Input Buffer if enabled] -> [FuzzProcessor] -> [Noise Gate if enabled] -> [Volume] -> Output
```

### SC-009 Tone Control Measurement Methodology

To verify SC-009 (tone control provides at least 12dB of high-frequency adjustment range, 400Hz to 8kHz):

1. **Test Signal**: White noise or broadband test signal at -12dBFS
2. **Measurement Points**: 400Hz, 1kHz, 2kHz, 4kHz, 8kHz (5 frequency bands)
3. **Procedure**:
   - Process signal with tone=0.0 (dark), measure RMS at each frequency band
   - Process signal with tone=1.0 (bright), measure RMS at each frequency band
   - Calculate delta in dB for each frequency band: `delta = 20*log10(bright_rms / dark_rms)`
4. **Pass Criteria**:
   - At least one frequency band between 400Hz-8kHz shows >= 12dB difference
   - Higher frequencies should show more attenuation at tone=0.0 than lower frequencies
5. **Alternative Method**: FFT-based comparison of power spectral density at tone=0.0 vs tone=1.0, measuring integrated power in the 400Hz-8kHz band

---

# Phase 1: Design

## Data Model

### Enumerations

```cpp
/// @brief Noise gate behavior type
enum class GateType : uint8_t {
    SoftKnee = 0,   ///< Gradual attenuation curve (default, most musical)
    HardGate = 1,   ///< Binary on/off behavior
    LinearRamp = 2  ///< Linear gain reduction based on distance below threshold
};

/// @brief Noise gate timing presets
enum class GateTiming : uint8_t {
    Fast = 0,    ///< 0.5ms attack, 20ms release - staccato playing
    Normal = 1,  ///< 1ms attack, 50ms release - balanced (default)
    Slow = 2     ///< 2ms attack, 100ms release - sustain preservation
};

/// @brief Input buffer high-pass filter cutoff frequency
enum class BufferCutoff : uint8_t {
    Hz5 = 0,   ///< 5Hz - ultra-conservative, preserves sub-bass
    Hz10 = 1,  ///< 10Hz - standard DC blocking (default)
    Hz20 = 2   ///< 20Hz - tighter bass, removes more low-end rumble
};
```

### FuzzPedal Class Structure

```cpp
class FuzzPedal {
public:
    // Constants
    static constexpr float kDefaultVolumeDb = 0.0f;
    static constexpr float kMinVolumeDb = -24.0f;
    static constexpr float kMaxVolumeDb = +24.0f;
    static constexpr float kDefaultGateThresholdDb = -60.0f;
    static constexpr float kMinGateThresholdDb = -80.0f;
    static constexpr float kMaxGateThresholdDb = 0.0f;
    static constexpr float kSmoothingTimeMs = 5.0f;
    static constexpr float kCrossfadeTimeMs = 5.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // FuzzProcessor forwarding
    void setFuzzType(FuzzType type) noexcept;
    void setFuzz(float amount) noexcept;      // [0, 1]
    void setTone(float tone) noexcept;        // [0, 1]
    void setBias(float bias) noexcept;        // [0, 1]

    // Volume control
    void setVolume(float dB) noexcept;        // [-24, +24]

    // Input buffer
    void setInputBuffer(bool enabled) noexcept;
    void setBufferCutoff(BufferCutoff cutoff) noexcept;

    // Noise gate
    void setGateEnabled(bool enabled) noexcept;
    void setGateThreshold(float dB) noexcept; // [-80, 0]
    void setGateType(GateType type) noexcept;
    void setGateTiming(GateTiming timing) noexcept;

    // Getters
    [[nodiscard]] FuzzType getFuzzType() const noexcept;
    [[nodiscard]] float getFuzz() const noexcept;
    [[nodiscard]] float getTone() const noexcept;
    [[nodiscard]] float getBias() const noexcept;
    [[nodiscard]] float getVolume() const noexcept;
    [[nodiscard]] bool getInputBuffer() const noexcept;
    [[nodiscard]] BufferCutoff getBufferCutoff() const noexcept;
    [[nodiscard]] bool getGateEnabled() const noexcept;
    [[nodiscard]] float getGateThreshold() const noexcept;
    [[nodiscard]] GateType getGateType() const noexcept;
    [[nodiscard]] GateTiming getGateTiming() const noexcept;

    // Processing
    void process(float* buffer, size_t numSamples) noexcept;

private:
    // Composed processors
    FuzzProcessor fuzz_;
    Biquad inputBufferFilter_;
    EnvelopeFollower gateEnvelope_;

    // Volume smoother
    OnePoleSmoother volumeSmoother_;

    // Gate type crossfade state
    bool gateTypeCrossfadeActive_;
    float gateTypeCrossfadePosition_;
    float gateTypeCrossfadeIncrement_;
    GateType previousGateType_;

    // Parameters
    float volumeDb_;
    float gateThresholdDb_;
    bool inputBufferEnabled_;
    BufferCutoff bufferCutoff_;
    bool gateEnabled_;
    GateType gateType_;
    GateTiming gateTiming_;

    // Sample rate
    double sampleRate_;
    bool prepared_;

    // Private helpers
    void updateBufferFilter() noexcept;
    void updateGateTiming() noexcept;
    [[nodiscard]] float calculateGateGain(float envelope, GateType type) const noexcept;
    [[nodiscard]] float cutoffToHz(BufferCutoff cutoff) const noexcept;
};
```

### Gate Gain Calculation Logic

```cpp
// SoftKnee: Gradual attenuation using smooth curve
// gain = tanh(3 * (env - threshold) / threshold) * 0.5 + 0.5 when env < threshold
// gain = 1.0 when env >= threshold

// HardGate: Binary on/off
// gain = 0.0 when env < threshold
// gain = 1.0 when env >= threshold

// LinearRamp: Linear interpolation
// gain = env / threshold when env < threshold
// gain = 1.0 when env >= threshold
```

## Quickstart Guide

### Basic Usage

```cpp
#include <krate/dsp/systems/fuzz_pedal.h>

using namespace Krate::DSP;

// Create and configure
FuzzPedal pedal;
pedal.prepare(44100.0, 512);

// Set fuzz parameters (forwarded to FuzzProcessor)
pedal.setFuzzType(FuzzType::Germanium);
pedal.setFuzz(0.7f);       // 70% saturation
pedal.setTone(0.5f);       // Neutral tone
pedal.setBias(0.8f);       // Near-normal operation

// Set output volume
pedal.setVolume(0.0f);     // Unity gain

// Process audio
pedal.process(buffer, numSamples);
```

### With Input Buffer

```cpp
// Enable input buffer with 10Hz DC blocking
pedal.setInputBuffer(true);
pedal.setBufferCutoff(BufferCutoff::Hz10);
```

### With Noise Gate

```cpp
// Enable noise gate with soft knee at -60dB
pedal.setGateEnabled(true);
pedal.setGateThreshold(-60.0f);
pedal.setGateType(GateType::SoftKnee);
pedal.setGateTiming(GateTiming::Normal);
```

### Full Pedalboard Setup

```cpp
FuzzPedal pedal;
pedal.prepare(sampleRate, blockSize);

// Classic germanium fuzz tone
pedal.setFuzzType(FuzzType::Germanium);
pedal.setFuzz(0.7f);
pedal.setBias(0.7f);       // Slight gating character
pedal.setTone(0.4f);       // Slightly dark

// Clean up with noise gate
pedal.setGateEnabled(true);
pedal.setGateThreshold(-55.0f);
pedal.setGateType(GateType::SoftKnee);
pedal.setGateTiming(GateTiming::Normal);

// Boost output
pedal.setVolume(+6.0f);

// Process in audio callback
void processAudio(float* buffer, size_t n) {
    pedal.process(buffer, n);
}
```

---

# Phase 2: Implementation Tasks

See `tasks.md` (created by `/speckit.tasks` command).

## Task Overview (Preview)

### Task Group 1: Core Structure and Lifecycle
- FR-001, FR-002, FR-003: prepare(), reset(), real-time safety

### Task Group 2: FuzzProcessor Composition
- FR-004 to FR-008: Compose FuzzProcessor, forward parameters

### Task Group 3: Volume Control
- FR-009 to FR-011: Volume with smoothing and range validation

### Task Group 4: Input Buffer
- FR-012 to FR-015: High-pass filter with selectable cutoff

### Task Group 5: Noise Gate
- FR-016 to FR-021h: Gate with types and timing presets

### Task Group 6: Processing
- FR-022 to FR-025: Signal flow implementation

### Task Group 7: Getters
- FR-026 to FR-029b: All parameter getters

### Task Group 8: Success Criteria Validation
- SC-001 to SC-011: Harmonic content, smoothing, stability tests

---

## Artifacts Generated

| Artifact | Status |
|----------|--------|
| `specs/067-fuzz-pedal/plan.md` | Complete |
| `specs/067-fuzz-pedal/research.md` | N/A (research in plan.md) |
| `specs/067-fuzz-pedal/data-model.md` | Embedded in plan.md |
| `specs/067-fuzz-pedal/quickstart.md` | Embedded in plan.md |
| `specs/067-fuzz-pedal/contracts/` | N/A (internal DSP library) |
| Agent context update | Pending |

---

## Next Steps

1. Run `/speckit.tasks` to generate detailed task breakdown
2. Create test file `dsp/tests/unit/systems/fuzz_pedal_test.cpp`
3. Implement `dsp/include/krate/dsp/systems/fuzz_pedal.h`
4. Update `specs/_architecture_/layer-3-systems.md`
