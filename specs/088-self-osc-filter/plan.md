# Implementation Plan: Self-Oscillating Filter

**Branch**: `088-self-osc-filter` | **Date**: 2026-01-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/088-self-osc-filter/spec.md`

## Summary

A Layer 2 DSP processor that wraps LadderFilter for melodic sine-wave generation from filter resonance. Provides MIDI note control (noteOn/noteOff), configurable attack/release envelope, glide/portamento, external input mixing for filter ping effects, and wave shaping via tanh saturation. The component enables using a self-oscillating filter as a playable melodic instrument.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: LadderFilter (Layer 1), DCBlocker2 (Layer 1), OnePoleSmoother/LinearRamp (Layer 1), FastMath::fastTanh (Layer 0), dbToGain (Layer 0)
**Storage**: N/A (no persistence)
**Testing**: Catch2 via dsp_tests target
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component (Layer 2 processor)
**Performance Goals**: < 0.5% CPU per instance at 44.1 kHz stereo per Layer 2 budget
**Constraints**: Zero allocations in process(), noexcept, real-time safe
**Scale/Scope**: Single-channel mono processor (stereo via two instances)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in process path
- [x] No locks/mutexes in audio thread
- [x] No exceptions in audio thread
- [x] All buffers pre-allocated in prepare()

**Required Check - Principle III (Modern C++ Standards):**
- [x] Using C++20 features where appropriate
- [x] RAII for all resource management
- [x] constexpr and const used aggressively
- [x] noexcept on all processing methods

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor - depends only on Layers 0 and 1
- [x] No circular dependencies
- [x] Independently testable

**Required Check - Principle X (DSP Processing Constraints):**
- [x] DC blocking after asymmetric saturation (DCBlocker2)
- [x] Oversampling handled by underlying LadderFilter (optional)
- [x] Feedback safety N/A (no external feedback loop)

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

**Classes/Structs to be created**: SelfOscillatingFilter

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SelfOscillatingFilter | `grep -r "class SelfOscillatingFilter" dsp/ plugins/` | No | Create New |
| SelfOscState (internal) | `grep -r "struct SelfOscState" dsp/ plugins/` | No | Create New (nested) |

**Utility Functions to be created**: midiNoteToFrequency

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| midiNoteToFrequency | `grep -r "midiNoteToFrequency\|midiToFreq\|midiToHz" dsp/ plugins/` | No | N/A | Create in Layer 0 (core/midi_utils.h) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| LadderFilter | dsp/include/krate/dsp/primitives/ladder_filter.h | 1 | Core filter for self-oscillation |
| DCBlocker2 | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | DC offset removal in output |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Level/mix parameter smoothing |
| LinearRamp | dsp/include/krate/dsp/primitives/smoother.h | 1 | Glide frequency interpolation |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | dB to linear for level control |
| FastMath::fastTanh | dsp/include/krate/dsp/core/fast_math.h | 0 | Wave shaping saturation |
| kPi, kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Frequency calculations |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing "SelfOscillatingFilter" or similar class found. The MIDI utility function will be placed in a new Layer 0 file to be reusable. All other components are existing primitives that will be composed, not duplicated.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| LadderFilter | prepare | `void prepare(double sampleRate, int maxBlockSize) noexcept` | Yes |
| LadderFilter | reset | `void reset() noexcept` | Yes |
| LadderFilter | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| LadderFilter | setResonance | `void setResonance(float amount) noexcept` | Yes |
| LadderFilter | setModel | `void setModel(LadderModel model) noexcept` | Yes |
| LadderFilter | setSlope | `void setSlope(int poles) noexcept` | Yes |
| LadderFilter | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| DCBlocker2 | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker2 | reset | `void reset() noexcept` | Yes |
| DCBlocker2 | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| LinearRamp | configure | `void configure(float rampTimeMs, float sampleRate) noexcept` | Yes |
| LinearRamp | setTarget | `void setTarget(float target) noexcept` | Yes |
| LinearRamp | process | `[[nodiscard]] float process() noexcept` | Yes |
| LinearRamp | snapTo | `void snapTo(float value) noexcept` | Yes |
| LinearRamp | reset | `void reset() noexcept` | Yes |
| dbToGain | (function) | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| FastMath::fastTanh | (function) | `[[nodiscard]] constexpr float fastTanh(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/ladder_filter.h` - LadderFilter class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker2 class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother, LinearRamp classes
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain function
- [x] `dsp/include/krate/dsp/core/fast_math.h` - FastMath::fastTanh function
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi constants

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| LadderFilter | Resonance max is 4.0, self-oscillation at ~3.9 | Use setResonance(3.95f) for reliable oscillation |
| LadderFilter | kMaxCutoffRatio = 0.45f (not Nyquist) | Freq capped at sampleRate * 0.45 |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| LinearRamp | Glide calculated from current to target | Call setTarget() after snapTo() for immediate update |
| DCBlocker2 | Default cutoff is 10Hz | Appropriate for self-oscillation DC removal |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| midiNoteToFrequency | Standard MIDI pitch conversion, will be needed by future melodic DSP | core/midi_utils.h | SelfOscillatingFilter, future melodic processors |
| velocityToGain | Standard MIDI velocity to amplitude curve | core/midi_utils.h | SelfOscillatingFilter, future MIDI-controlled processors |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| (none identified) | All utilities extracted to Layer 0 |

**Decision**: Create `core/midi_utils.h` with `midiNoteToFrequency()` and `velocityToGain()` functions for reuse by this and future melodic DSP components.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from ROADMAP.md or known plans):
- Future ring modulator (would benefit from MIDI-to-frequency)
- Future FM synthesis components
- Karplus-Strong (already exists but could use MIDI utils)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| midiNoteToFrequency | HIGH | Ring modulator, FM, any melodic DSP | Extract to Layer 0 now |
| velocityToGain | MEDIUM | Any MIDI-controlled processor | Extract to Layer 0 now |
| Envelope state machine | LOW | Specific to this component | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract MIDI utils to Layer 0 | Standard conversions with clear reuse potential |
| Keep envelope logic local | State machine specific to self-oscillating behavior |

## Project Structure

### Documentation (this feature)

```text
specs/088-self-osc-filter/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (N/A - no external API)
└── tasks.md             # Phase 2 output
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   └── midi_utils.h              # NEW: MIDI utilities (Layer 0)
│   └── processors/
│       └── self_oscillating_filter.h # NEW: Main processor (Layer 2)
└── tests/
    ├── core/
    │   └── midi_utils_tests.cpp      # NEW: MIDI utility tests
    └── processors/
        └── self_oscillating_filter_tests.cpp  # NEW: Processor tests
```

**Structure Decision**: Following existing monorepo structure. New Layer 0 utility file for MIDI conversions (reusable), new Layer 2 processor file. Tests mirror source structure.

## Complexity Tracking

No constitution violations requiring justification. Design follows all principles.

---

## Phase 0: Research Summary

### Research Tasks Identified

1. **LadderFilter self-oscillation behavior**: Verify exact resonance threshold and amplitude characteristics
2. **Envelope design for oscillator**: Attack/release state machine patterns
3. **Glide implementation**: Linear frequency vs linear pitch interpolation
4. **MIDI note to frequency conversion**: Standard 12-TET formula verification
5. **Velocity curves**: Standard MIDI velocity to amplitude mapping

### Research Findings

#### 1. LadderFilter Self-Oscillation

From `ladder_filter.h`:
- Self-oscillation threshold: resonance >= 3.9 (kMaxResonance = 4.0)
- Resonance range: 0.0 to 4.0
- Cutoff range: 20Hz to sampleRate * 0.45
- For reliable oscillation: use resonance = 3.95f or higher
- Model selection: LadderModel::Nonlinear for best oscillation character

#### 2. Envelope State Machine

Design for attack/sustain/release:
```
States: IDLE, ATTACK, SUSTAIN, RELEASE
IDLE -> ATTACK: on noteOn()
ATTACK -> SUSTAIN: when envelope reaches target level
SUSTAIN -> RELEASE: on noteOff()
RELEASE -> IDLE: when envelope reaches threshold (-60dB)
ATTACK -> ATTACK (retrigger): noteOn() during active note restarts from current level
```

Envelope coefficient: exponential curve using OnePoleSmoother for both attack and release phases.

#### 3. Glide Implementation

Per spec FR-010: "linear frequency ramp, perceived as constant-rate pitch change"
- Use LinearRamp for frequency (Hz) interpolation
- This creates a linear frequency sweep, which is perceived as a constant-rate pitch glide
- Alternative (log frequency) would give constant semitones/sec but spec explicitly requests linear frequency

#### 4. MIDI Note to Frequency

Standard 12-TET formula:
```cpp
constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = 440.0f) noexcept {
    return a4Frequency * std::pow(2.0f, (midiNote - 69) / 12.0f);
}
```
- A4 (note 69) = 440 Hz
- Each semitone = 2^(1/12) ratio

#### 5. Velocity to Gain Mapping

Standard MIDI velocity curve (linear for simplicity per spec):
```cpp
// FR-007: velocity 127 = full level, velocity 64 = approximately -6 dB
// Linear mapping: gain = velocity / 127
// -6 dB at 64: dbToGain(-6) = 0.501, 64/127 = 0.504 - close match
constexpr float velocityToGain(int velocity) noexcept {
    return static_cast<float>(velocity) / 127.0f;
}
```

---

## Phase 1: Design

### Data Model

**SelfOscillatingFilter** (Layer 2 Processor)

```cpp
namespace Krate::DSP {

class SelfOscillatingFilter {
public:
    // Constants
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMaxFrequency = 20000.0f;
    static constexpr float kMinAttackMs = 0.0f;
    static constexpr float kMaxAttackMs = 20.0f;
    static constexpr float kMinReleaseMs = 10.0f;
    static constexpr float kMaxReleaseMs = 2000.0f;
    static constexpr float kMinGlideMs = 0.0f;
    static constexpr float kMaxGlideMs = 5000.0f;
    static constexpr float kMinLevelDb = -60.0f;
    static constexpr float kMaxLevelDb = 6.0f;
    static constexpr float kSelfOscResonance = 3.95f;
    static constexpr float kReleaseThresholdDb = -60.0f;

    // Lifecycle
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    // Per-sample processing
    [[nodiscard]] float process(float externalInput) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // MIDI control
    void noteOn(int midiNote, int velocity) noexcept;
    void noteOff() noexcept;

    // Parameter setters (thread-safe, smoothed)
    void setFrequency(float hz) noexcept;
    void setResonance(float amount) noexcept;  // 0.0 to 1.0 normalized
    void setGlide(float ms) noexcept;
    void setAttack(float ms) noexcept;
    void setRelease(float ms) noexcept;
    void setExternalMix(float mix) noexcept;   // 0.0 = osc only, 1.0 = ext only
    void setWaveShape(float amount) noexcept;  // 0.0 = clean, 1.0 = saturated
    void setOscillationLevel(float dB) noexcept;

    // Getters
    [[nodiscard]] float getFrequency() const noexcept;
    [[nodiscard]] float getResonance() const noexcept;
    [[nodiscard]] float getGlide() const noexcept;
    [[nodiscard]] float getAttack() const noexcept;
    [[nodiscard]] float getRelease() const noexcept;
    [[nodiscard]] float getExternalMix() const noexcept;
    [[nodiscard]] float getWaveShape() const noexcept;
    [[nodiscard]] float getOscillationLevel() const noexcept;
    [[nodiscard]] bool isOscillating() const noexcept;

private:
    // Envelope states
    enum class EnvelopeState : uint8_t { Idle, Attack, Sustain, Release };

    // Components
    LadderFilter filter_;
    DCBlocker2 dcBlocker_;
    LinearRamp frequencyRamp_;      // For glide
    OnePoleSmoother levelSmoother_; // For output level
    OnePoleSmoother mixSmoother_;   // For external mix
    OnePoleSmoother attackEnvelope_;  // Attack phase smoother
    OnePoleSmoother releaseEnvelope_; // Release phase smoother

    // State
    EnvelopeState envelopeState_ = EnvelopeState::Idle;
    float currentEnvelopeLevel_ = 0.0f;
    float targetVelocityGain_ = 1.0f;

    // Parameters (user-facing)
    float frequency_ = 440.0f;
    float resonance_ = 1.0f;       // Normalized 0-1
    float glideMs_ = 0.0f;
    float attackMs_ = 0.0f;
    float releaseMs_ = 500.0f;
    float externalMix_ = 0.0f;
    float waveShapeAmount_ = 0.0f;
    float levelDb_ = 0.0f;

    // Runtime
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Internal methods
    float processEnvelope() noexcept;
    float applyWaveShaping(float input) noexcept;
    float mapResonanceToFilter(float normalized) const noexcept;
};

} // namespace Krate::DSP
```

### Signal Flow

```
                    +-------------+
   External Input ->| Mix Control |--+
                    +-------------+  |
                                     v
                    +-------------+  |  +-------------+
   (self-osc) ----->|   Ladder    |--+->|  DC Block   |
                    |   Filter    |     +-------------+
                    +-------------+            |
                           ^                   v
                           |            +-------------+
                    Cutoff (from glide) | Wave Shape  |
                    Resonance (fixed    +-------------+
                     at ~3.95 for osc)         |
                                               v
                                        +-------------+
                                        |  Envelope   |
                                        |    * Gain   |
                                        +-------------+
                                               |
                                               v
                                        +-------------+
                                        | Level Gain  |
                                        +-------------+
                                               |
                                               v
                                            Output
```

### Contracts

N/A - This is an internal DSP processor with no external API contract (OpenAPI/GraphQL). The public API is defined in the data model above.

---

## Review Trigger

After implementing the next melodic DSP feature, review:
- [ ] Does sibling feature need midiNoteToFrequency? -> Already in Layer 0
- [ ] Does sibling feature use same envelope pattern? -> Consider shared abstraction
- [ ] Any duplicated code? -> Extract to shared utilities
