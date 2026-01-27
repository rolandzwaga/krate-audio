# Implementation Plan: FuzzProcessor

**Branch**: `063-fuzz-processor` | **Date**: 2026-01-14 | **Spec**: [specs/063-fuzz-processor/spec.md](spec.md)
**Input**: Feature specification from `/specs/063-fuzz-processor/spec.md`

## Summary

Implement a Layer 2 FuzzProcessor providing Fuzz Face style distortion with two transistor types: Germanium (warm, saggy, even harmonics) and Silicon (bright, tight, odd harmonics). Features include bias control for "dying battery" gating effects, tone control via low-pass filter, octave-up mode via self-modulation, and click-free type switching via 5ms crossfade.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0/1 DSP components (Waveshaper, Biquad, DCBlocker, OnePoleSmoother, Sigmoid, Asymmetric)
**Storage**: N/A (stateless except filter state and envelope follower state)
**Testing**: Catch2 (unit tests in `dsp/tests/unit/processors/`)
**Target Platform**: Cross-platform (Windows, macOS, Linux)
**Project Type**: Audio DSP library (monorepo structure)
**Performance Goals**: < 0.5% CPU @ 44.1kHz/2.5GHz baseline (SC-005)
**Constraints**: Real-time safe (no allocations in process), 5ms type crossfade, 10ms parameter smoothing
**Scale/Scope**: Layer 2 processor for guitar/bass fuzz effects

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in process() path
- [x] No exceptions in audio path (noexcept)
- [x] No locks/mutexes in audio path
- [x] No I/O operations in audio path
- [x] Denormal handling via flushDenormal()

**Required Check - Principle III (Modern C++):**
- [x] C++20 features (constexpr, [[nodiscard]], etc.)
- [x] RAII for all resources
- [x] Value semantics where appropriate

**Required Check - Principle IX (Layer Architecture):**
- [x] Layer 2 depends only on Layer 0 and Layer 1
- [x] No circular dependencies
- [x] Note: EnvelopeFollower is Layer 2, so Germanium sag must use inline one-pole implementation

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

**Classes/Structs to be created**: FuzzProcessor, FuzzType

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FuzzProcessor | `grep -r "class FuzzProcessor" dsp/ plugins/` | No | Create New |
| FuzzType | `grep -r "enum.*FuzzType" dsp/ plugins/` | No | Create New |

**Related searches for potential conflicts:**

| Search Term | Search Command | Existing? | Location | Action |
|-------------|----------------|-----------|----------|--------|
| Fuzz | `grep -r "Fuzz" dsp/ plugins/` | No | - | Create New |
| Germanium | `grep -r "Germanium" dsp/ plugins/` | Yes (DiodeClipper) | processors/diode_clipper.h | Different context (DiodeType::Germanium), no conflict |
| Silicon | `grep -r "Silicon" dsp/ plugins/` | Yes (DiodeClipper) | processors/diode_clipper.h | Different context (DiodeType::Silicon), no conflict |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Saturation processing (Tube for Germanium, Tanh for Silicon) |
| WaveshapeType::Tube | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Germanium asymmetric saturation (even harmonics) |
| WaveshapeType::Tanh | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Silicon symmetric saturation (odd harmonics) |
| Sigmoid::tanh | dsp/include/krate/dsp/core/sigmoid.h | 0 | Silicon hard clipping |
| Asymmetric::tube | dsp/include/krate/dsp/core/sigmoid.h | 0 | Germanium soft clipping |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | Tone filter (low-pass) |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | DC offset removal after asymmetric saturation |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (fuzz, volume, bias, tone) |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Convert volume dB to linear gain |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Prevent denormal CPU spikes |
| equalPowerGains | dsp/include/krate/dsp/core/crossfade_utils.h | 0 | Type crossfade (equal-power) |
| crossfadeIncrement | dsp/include/krate/dsp/core/crossfade_utils.h | 0 | Calculate crossfade rate |

### Reference Implementations

| Component | Location | Layer | Reference For |
|-----------|----------|-------|---------------|
| TubeStage | dsp/include/krate/dsp/processors/tube_stage.h | 2 | Layer 2 processor pattern, smoothing, DC blocking |
| DiodeClipper | dsp/include/krate/dsp/processors/diode_clipper.h | 2 | Type switching, asymmetric saturation |
| TapeSaturator | dsp/include/krate/dsp/processors/tape_saturator.h | 2 | Model crossfade pattern |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no fuzz_processor.h)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: FuzzProcessor and FuzzType are unique names. Germanium/Silicon enum values exist in DiodeClipper as DiodeType::Germanium and DiodeType::Silicon, but FuzzType is a separate enum in FuzzProcessor's scope, preventing conflicts.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Yes |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Yes |
| Waveshaper | setAsymmetry | `void setAsymmetry(float bias) noexcept` | Yes |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Sigmoid::tanh | tanh | `[[nodiscard]] constexpr float tanh(float x) noexcept` | Yes |
| Asymmetric::tube | tube | `[[nodiscard]] inline float tube(float x) noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | snapToTarget | `void snapToTarget() noexcept` | Yes |
| dbToGain | dbToGain | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| flushDenormal | flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| equalPowerGains | equalPowerGains | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` | Yes |
| crossfadeIncrement | crossfadeIncrement | `[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid:: and Asymmetric:: namespaces
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class, FilterType enum
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain, flushDenormal
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - equalPowerGains, crossfadeIncrement

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| Biquad | Uses `configure(FilterType, freq, Q, gainDb, sr)` order | `biquad.configure(FilterType::Lowpass, freq, Q, 0.0f, sr)` |
| DCBlocker | Uses `prepare(sampleRate, cutoffHz)` with double sampleRate | `dcBlocker.prepare(sampleRate, 10.0f)` |
| crossfadeIncrement | Returns per-sample increment, not total samples | `position += increment` per sample |
| Waveshaper | setAsymmetry() creates DC offset - must DC-block after | Always follow with DCBlocker |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Germanium sag envelope follower | Simple one-pole, specific to FuzzProcessor, would be Layer 2 if extracted |
| Octave-up self-modulation | Single-line formula (input * abs(input)), not reusable |
| Bias gating calculation | Specific to fuzz behavior, not generalizable |

**Decision**: All helper functions remain as private methods or inline calculations. No Layer 0 extraction needed.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer**:
- TubeStage (059) - already implemented, shares parameter smoothing patterns
- DiodeClipper (060) - already implemented, shares type switching/crossfade patterns
- TapeSaturator (062) - already implemented, shares crossfade patterns

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FuzzProcessor | MEDIUM | Guitar effects, distortion pedal chains | Keep as standalone processor |
| FuzzType enum | LOW | Only FuzzProcessor | Keep local to class |

**Decision**: FuzzProcessor is a complete processor designed for composition by Layer 3/4 components. No shared base class with other saturation processors (TubeStage, DiodeClipper) as their internal architectures differ significantly.

## Project Structure

### Documentation (this feature)

```text
specs/063-fuzz-processor/
├── spec.md              # Feature specification (input)
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/
│   └── fuzz_processor_api.h  # API contract
├── checklists/
│   └── requirements.md  # FR/NFR checklist
└── tasks.md             # Phase 2 output (to be created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/processors/
│   └── fuzz_processor.h    # New: Layer 2 processor header
└── tests/unit/processors/
    └── fuzz_processor_test.cpp  # New: Unit tests
```

**Structure Decision**: Single header implementation (header-only) following the pattern of other Layer 2 processors (DiodeClipper, TubeStage, TapeSaturator).

## Phase 0: Research

### Key Design Decisions

#### 1. Germanium "Sag" Implementation

**Decision**: Envelope follower with 1ms attack, 100ms release modulating clipping threshold.

**Rationale**: Per spec clarification, Germanium sag is implemented as signal-dependent compression where loud signals dynamically lower the clipping threshold. The envelope follower tracks input level; higher levels = lower threshold = more compression.

**Implementation**: Simple inline one-pole envelope follower (cannot use EnvelopeFollower class as it's Layer 2):
```cpp
// Attack coefficient: exp(-1 / (1ms * sampleRate))
// Release coefficient: exp(-1 / (100ms * sampleRate))
float envelope = (abs(input) > sagEnvelope_)
    ? abs(input) + attackCoeff_ * (sagEnvelope_ - abs(input))
    : abs(input) + releaseCoeff_ * (sagEnvelope_ - abs(input));
```

#### 2. Octave-Up Implementation

**Decision**: Self-modulation via `input * abs(input)`.

**Rationale**: Per spec clarification, octave-up effect is achieved by multiplying the input signal by its rectified version before the main fuzz stage. This creates frequency doubling (octave up) through ring modulation.

**Implementation**:
```cpp
if (octaveUp_) {
    input = input * std::abs(input);
}
```

#### 3. Type Transition Crossfade

**Decision**: 5ms equal-power crossfade between type outputs.

**Rationale**: Per spec clarification, when switching between Germanium and Silicon, both types process in parallel during the transition and outputs are blended using equal-power gains. This prevents audible clicks while maintaining constant perceived loudness.

**Implementation**:
```cpp
if (crossfadeActive_) {
    float fadeOut, fadeIn;
    equalPowerGains(crossfadePosition_, fadeOut, fadeIn);
    output = previousTypeOutput * fadeOut + currentTypeOutput * fadeIn;
    crossfadePosition_ += crossfadeIncrement_;
    if (crossfadePosition_ >= 1.0f) crossfadeActive_ = false;
}
```

#### 4. Bias Gating Implementation

**Decision**: DC offset added before waveshaping affecting clipping symmetry.

**Rationale**: Per FR-022 to FR-025, bias affects the transistor operating point. Low bias (0.0) creates maximum gating where quiet signals are cut off. High bias (1.0) creates normal operation.

**Implementation**: Bias is inverted and scaled to create a threshold below which signals are attenuated:
```cpp
// bias=0 -> gateThreshold high (aggressive gating)
// bias=1 -> gateThreshold=0 (no gating)
float gateThreshold = (1.0f - bias_) * 0.2f;  // ~-14dB at bias=0
float gatingFactor = std::min(1.0f, std::abs(input) / gateThreshold);
output *= gatingFactor;
```

#### 5. Tone Filter Design

**Decision**: 2-pole Biquad low-pass filter with Butterworth Q.

**Rationale**: Per FR-026 to FR-029, tone control maps [0, 1] to cutoff frequency [400Hz, 8000Hz]. Using Biquad provides smooth filtering with 12dB/octave rolloff.

**Implementation**:
```cpp
float cutoff = 400.0f + tone_ * (8000.0f - 400.0f);  // Linear interpolation
toneFilter_.configure(FilterType::Lowpass, cutoff, kButterworthQ, 0.0f, sampleRate_);
```

### Signal Flow

```
Input
  │
  ├──[Octave-Up: input * |input|]── (if enabled)
  │
  ├──[Parameter Smoothing]
  │     fuzz, bias, tone, volume (5ms OnePoleSmoother)
  │
  ├──[Drive Stage: input * fuzz_gain]
  │
  ├──[Bias Addition: input + bias_offset]
  │
  ├──[Type-Specific Processing]──────────────────────┐
  │     │                                            │
  │  ┌──┴──────────────────────┐  ┌──────────────────┴──┐
  │  │  GERMANIUM              │  │  SILICON            │
  │  │  - Sag envelope follower │  │  - No sag           │
  │  │  - Dynamic threshold     │  │  - Fixed threshold  │
  │  │  - Asymmetric::tube()   │  │  - Sigmoid::tanh()  │
  │  │  - Even + odd harmonics │  │  - Odd harmonics    │
  │  └──────────┬───────────────┘  └──────────┬─────────┘
  │             │                             │
  │             └──────────┬──────────────────┘
  │                        │
  │  ┌─────────────────────┴─────────────────────┐
  │  │  TYPE CROSSFADE (5ms equal-power)         │
  │  │  (when type changes during processing)    │
  │  └─────────────────────┬─────────────────────┘
  │                        │
  ├──[Bias Gating: attenuate quiet signals]
  │
  ├──[DC Blocker: 10Hz highpass]
  │
  ├──[Tone Filter: 400-8000Hz lowpass]
  │
  └──[Volume: output * volume_gain]
        │
      Output
```

## Phase 1: Design

### Data Model

#### FuzzType Enumeration (FR-001)

```cpp
enum class FuzzType : uint8_t {
    Germanium = 0,  ///< Warm, saggy, even harmonics, soft clipping
    Silicon = 1     ///< Bright, tight, odd harmonics, hard clipping
};
```

#### FuzzProcessor Class

| Member | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| type_ | FuzzType | Germanium | - | Current transistor type |
| fuzz_ | float | 0.5 | [0.0, 1.0] | Fuzz/saturation amount |
| volumeDb_ | float | 0.0 | [-24, +24] | Output volume in dB |
| bias_ | float | 0.7 | [0.0, 1.0] | Transistor bias (gating control) |
| tone_ | float | 0.5 | [0.0, 1.0] | Tone control (filter cutoff) |
| octaveUp_ | bool | false | - | Octave-up effect enable |

#### Internal State

| Member | Type | Purpose |
|--------|------|---------|
| sampleRate_ | double | Current sample rate |
| prepared_ | bool | Whether prepare() was called |
| fuzzSmoother_ | OnePoleSmoother | Smooths fuzz parameter |
| volumeSmoother_ | OnePoleSmoother | Smooths volume gain |
| biasSmoother_ | OnePoleSmoother | Smooths bias parameter |
| toneCtrlSmoother_ | OnePoleSmoother | Smooths tone parameter |
| germaniumShaper_ | Waveshaper | Tube-type for Germanium |
| siliconShaper_ | Waveshaper | Tanh-type for Silicon |
| toneFilter_ | Biquad | Low-pass tone control |
| dcBlocker_ | DCBlocker | DC offset removal |
| sagEnvelope_ | float | Germanium sag envelope state |
| sagAttackCoeff_ | float | 1ms attack coefficient |
| sagReleaseCoeff_ | float | 100ms release coefficient |
| crossfadeActive_ | bool | Type crossfade in progress |
| crossfadePosition_ | float | Crossfade position [0, 1] |
| crossfadeIncrement_ | float | Per-sample crossfade increment |
| previousType_ | FuzzType | Type before crossfade started |

### API Contract

```cpp
// ==============================================================================
// Layer 2: DSP Processor - FuzzProcessor API Contract
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Transistor type for fuzz character
enum class FuzzType : uint8_t {
    Germanium = 0,  ///< Warm, saggy, even harmonics
    Silicon = 1     ///< Bright, tight, odd harmonics
};

/// @brief Fuzz Face style distortion processor
class FuzzProcessor {
public:
    // Constants
    static constexpr float kDefaultFuzz = 0.5f;
    static constexpr float kDefaultVolumeDb = 0.0f;
    static constexpr float kDefaultBias = 0.7f;
    static constexpr float kDefaultTone = 0.5f;
    static constexpr float kMinVolumeDb = -24.0f;
    static constexpr float kMaxVolumeDb = +24.0f;
    static constexpr float kSmoothingTimeMs = 5.0f;
    static constexpr float kCrossfadeTimeMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;
    static constexpr float kToneMinHz = 400.0f;
    static constexpr float kToneMaxHz = 8000.0f;
    static constexpr float kSagAttackMs = 1.0f;
    static constexpr float kSagReleaseMs = 100.0f;

    // Lifecycle (FR-002 to FR-005)
    FuzzProcessor() noexcept;
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Type Selection (FR-006, FR-006a, FR-011)
    void setFuzzType(FuzzType type) noexcept;
    [[nodiscard]] FuzzType getFuzzType() const noexcept;

    // Parameter Setters (FR-007 to FR-010)
    void setFuzz(float amount) noexcept;      // [0.0, 1.0]
    void setVolume(float dB) noexcept;        // [-24, +24] dB
    void setBias(float bias) noexcept;        // [0.0, 1.0]
    void setTone(float tone) noexcept;        // [0.0, 1.0]

    // Octave-Up (FR-050 to FR-053)
    void setOctaveUp(bool enabled) noexcept;
    [[nodiscard]] bool getOctaveUp() const noexcept;

    // Parameter Getters (FR-012 to FR-015)
    [[nodiscard]] float getFuzz() const noexcept;
    [[nodiscard]] float getVolume() const noexcept;
    [[nodiscard]] float getBias() const noexcept;
    [[nodiscard]] float getTone() const noexcept;

    // Processing (FR-030 to FR-032)
    void process(float* buffer, size_t numSamples) noexcept;
};

} // namespace DSP
} // namespace Krate
```

## Phase 2: Tasks (To Be Generated)

The `/speckit.tasks` command will generate:
1. Task breakdown following TDD workflow
2. Each task includes: failing test -> implementation -> warnings fix -> verify -> commit
3. Benchmark tasks for SC-005 (CPU < 0.5%) compliance
4. Harmonic analysis tests for SC-001 to SC-003

### Task Outline

1. **Core Structure** (FR-001 to FR-005)
   - FuzzType enum
   - FuzzProcessor class shell
   - Default constructor with safe defaults
   - prepare() and reset() methods

2. **Silicon Type** (FR-019 to FR-021)
   - Tanh-based symmetric saturation
   - Harder clipping, odd harmonics
   - Unit tests for harmonic content

3. **Germanium Type** (FR-016 to FR-018)
   - Asymmetric tube saturation
   - Sag envelope follower (1ms attack, 100ms release)
   - Unit tests for even harmonic content

4. **Bias Control** (FR-022 to FR-025)
   - Gating effect implementation
   - bias=0 aggressive gating
   - bias=1 normal operation
   - Unit tests for gating behavior (SC-009)

5. **Tone Control** (FR-026 to FR-029)
   - Biquad low-pass filter
   - 400Hz to 8000Hz range
   - Unit tests for frequency response (SC-010)

6. **Volume Control** (FR-008)
   - dB to linear conversion
   - Clamping to [-24, +24] dB

7. **Parameter Smoothing** (FR-036 to FR-040)
   - OnePoleSmoother for all parameters
   - 5ms smoothing time
   - reset() snaps to target

8. **Type Crossfade** (FR-006a)
   - 5ms equal-power crossfade
   - Tests for click-free transitions (SC-004)

9. **Octave-Up Mode** (FR-050 to FR-053)
   - Self-modulation implementation
   - Applied before main fuzz stage
   - Unit tests for 2nd harmonic (SC-011)

10. **DC Blocking** (FR-033 to FR-035)
    - 10Hz DCBlocker after saturation
    - Unit tests for DC removal (SC-006)

11. **Integration Tests**
    - Full signal path tests
    - CPU benchmark (SC-005)
    - Multi-sample-rate tests (SC-007)
    - THD measurement (SC-008)

## Complexity Tracking

### Constitution Exception: Oversampling (Principle X)

**Issue**: Constitution Principle X states "Oversampling (min 2x) for saturation/distortion/waveshaping" but this spec explicitly excludes internal oversampling (Out of Scope section).

**Justification**: FuzzProcessor is a Layer 2 processor designed as a building block for composition in oversampled contexts at Layer 3/4. The constitution principle applies to complete effects (Layer 4), not individual primitives. When FuzzProcessor is used in a complete guitar effects chain, the containing system wraps it with `Oversampler<2>` or higher as appropriate.

**Evidence**: This pattern is consistent with other Layer 2 processors (DiodeClipper, WavefolderProcessor, TubeStage, TapeSaturator) which also omit internal oversampling per DST-ROADMAP design principle: "Distortion primitives are 'pure' - no internal oversampling."

**Resolution**: Documented exception - no code change required. Layer 3/4 consumers are responsible for oversampling.

### Constitution Exception: Layer 2 EnvelopeFollower

**Issue**: The spec references using envelope follower for Germanium sag, but EnvelopeFollower is a Layer 2 component, and FuzzProcessor cannot depend on Layer 2.

**Justification**: The Germanium sag envelope follower is a simple one-pole implementation with fixed attack/release (1ms/100ms). This can be implemented inline using basic one-pole math identical to OnePoleSmoother's internals, without requiring the full EnvelopeFollower class.

**Resolution**: Implement sag envelope tracking as private member variables and inline calculations in processSample(), not as a separate EnvelopeFollower instance.
