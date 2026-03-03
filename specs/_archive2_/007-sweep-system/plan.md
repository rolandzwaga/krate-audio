# Implementation Plan: Sweep System

**Branch**: `007-sweep-system` | **Date**: 2026-01-29 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/007-sweep-system/spec.md`

---

## Summary

The Sweep System provides frequency-based distortion focus for the Disrumpo plugin. A SweepProcessor DSP class calculates per-band intensity multipliers using Gaussian (Smooth) or linear (Sharp) falloff distributions. The sweep position can be automated via host automation, internal LFO, envelope follower, or MIDI CC. Sweep-morph linking enables automatic morph position control based on sweep frequency using 8 curve modes (including Custom breakpoint curves). The SweepIndicator visualizes sweep position on the SpectrumDisplay with audio-synchronized updates via lock-free SPSC buffer.

**Week 8 Tasks**: T8.1-T8.18 per roadmap.md

---

## Technical Context

**Language/Version**: C++20 (MSVC 19.x / Clang 15+)
**Primary Dependencies**: Steinberg VST3 SDK 3.7+, VSTGUI 4.11+, KrateDSP (internal library)
**Storage**: VST3 preset state serialization (IBStream)
**Testing**: Catch2 3.x (testing-guide skill auto-loads)
**Target Platform**: Windows 10+, macOS 10.13+, Linux (x86_64)
**Project Type**: VST3 plugin with monorepo structure (dsp/ + plugins/Disrumpo/)
**Performance Goals**: <0.1% CPU overhead per active band for sweep calculation (SC-013)
**Constraints**: Real-time safe (no allocations/locks in audio thread), 10-50ms parameter smoothing
**Scale/Scope**: 56 functional requirements, 18 success criteria, ~8-10 new source files

---

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] SweepProcessor will be noexcept with no allocations in process()
- [x] Lock-free SPSC buffer for audio-UI communication
- [x] OnePoleSmoother used for parameter smoothing (10-50ms)
- [x] No dynamic allocations after prepare()

**Required Check - Principle III (Modern C++):**
- [x] C++20 features: constexpr, [[nodiscard]], std::array
- [x] RAII for resource management
- [x] Value semantics for DSP state

**Required Check - Principle IX (Layered Architecture):**
- [x] SweepProcessor at Layer 3 (systems) - composes Layer 1/2 primitives
- [x] SweepMorphLink curves at Layer 0 (core) - pure math functions
- [x] SweepPositionBuffer at Layer 1 (primitives) - lock-free queue

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

---

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SweepProcessor | `grep -r "class SweepProcessor" dsp/ plugins/` | No (only in specs) | Create New |
| SweepPositionData | `grep -r "struct SweepPositionData" dsp/ plugins/` | No (spec only) | Create New |
| SweepPositionBuffer | `grep -r "class SweepPositionBuffer" dsp/ plugins/` | No (spec only) | Create New |
| SweepMorphLinkCurve | `grep -r "SweepMorphLink" dsp/ plugins/` | Enum exists in plugin_ids.h | Create curve functions |
| CustomCurve | `grep -r "class CustomCurve" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| calculateGaussianIntensity | `grep -r "Gaussian" dsp/` | No | - | Create New |
| calculateLinearFalloff | `grep -r "LinearFalloff" dsp/` | No | - | Create New |
| normalizedSweepFreq | `grep -r "normalizedSweep" dsp/` | No | - | Create New |
| applyMorphLinkCurve | `grep -r "MorphLinkCurve" dsp/` | No | - | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Sweep frequency smoothing (10-50ms) |
| EnvelopeFollower | dsp/include/krate/dsp/processors/envelope_follower.h | 2 | Input-driven sweep modulation |
| LFO | dsp/include/krate/dsp/primitives/lfo.h | 1 | Internal sweep frequency modulation |
| MorphEngine | plugins/Disrumpo/src/dsp/morph_engine.h | Plugin | Receive position updates from sweep link |
| RingBuffer | extern/vst3sdk/public.sdk/source/vst/utility/ringbuffer.h | SDK | Reference for SPSC pattern |
| MorphLinkMode | plugins/Disrumpo/src/plugin_ids.h | Plugin | Enum already defined (7 modes) |
| SweepParamType | plugins/Disrumpo/src/plugin_ids.h | Plugin | Parameter IDs already defined |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (smoother.h, lfo.h)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (envelope_follower.h)
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (no sweep conflicts)
- [x] `plugins/Disrumpo/src/dsp/` - Plugin DSP (morph_engine.h)
- [x] `plugins/Disrumpo/src/plugin_ids.h` - Parameter IDs (sweep IDs already defined)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. Existing components (OnePoleSmoother, LFO, EnvelopeFollower) will be composed, not duplicated. MorphLinkMode enum exists but sweep-specific curve functions do not.

---

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setMode | `void setMode(DetectionMode mode) noexcept` | Yes |
| LFO | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| LFO | process | `[[nodiscard]] float process() noexcept` | Yes |
| LFO | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| LFO | setWaveform | `void setWaveform(Waveform waveform) noexcept` | Yes |
| LFO | setTempoSync | `void setTempoSync(bool enabled) noexcept` | Yes |
| LFO | setTempo | `void setTempo(float bpm) noexcept` | Yes |
| LFO | setNoteValue | `void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept` | Yes |
| MorphEngine | setMorphPosition | `void setMorphPosition(float x, float y) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class
- [x] `plugins/Disrumpo/src/dsp/morph_engine.h` - MorphEngine class
- [x] `plugins/Disrumpo/src/plugin_ids.h` - Parameter IDs and MorphLinkMode enum
- [x] `extern/vst3sdk/public.sdk/source/vst/utility/ringbuffer.h` - SPSC RingBuffer reference

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| EnvelopeFollower | Requires `prepare()` before processing | Call `prepare(sampleRate, maxBlockSize)` first |
| LFO | Waveform enum is `Krate::DSP::Waveform` | `lfo.setWaveform(Krate::DSP::Waveform::Sine)` |
| LFO | Tempo sync requires both `setTempoSync(true)` and `setTempo(bpm)` | Set both for sync mode |
| MorphLinkMode | Enum in plugin_ids.h has 7 modes (COUNT=7) | Custom mode not in enum - add separately |

---

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| normalizedSweepFreq | Pure math, log2 normalization reusable | dsp/core/frequency_utils.h | SweepProcessor, SweepIndicator, future spectral features |
| applyMorphLinkCurve | Pure math curves, stateless | dsp/core/curve_utils.h | SweepProcessor, potential modulation matrix |
| linearInterpolate | Simple lerp for custom curves | dsp/core/math_utils.h | Already exists as inline, verify |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateGaussianIntensity | Sweep-specific formula, tied to sigma/width semantics |
| calculateLinearFalloff | Sweep-specific sharp mode, tightly coupled |

**Decision**: Extract normalizedSweepFreq and applyMorphLinkCurve to Layer 0 if they have 2+ consumers during implementation. Start as member functions, refactor if reuse emerges.

---

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 - DSP Systems / Layer 4 - UI Features

**Related features at same layer**:
- 008-modulation-system (Week 9-10): Similar parameter smoothing, audio-UI sync patterns
- Future spectral analyzer features: May reuse frequency normalization, lock-free buffer

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SweepPositionBuffer | HIGH | Modulation system, any audio-UI sync | Keep local, extract after 2nd use |
| Morph link curve functions | MEDIUM | Potential modulation matrix curves | Keep local initially |
| Gaussian intensity calc | LOW | Sweep-specific | Keep in SweepProcessor |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep SweepPositionBuffer in plugin code | First audio-UI sync feature - patterns not established |
| Custom curve breakpoints as separate class | Enables serialization and potential preset sharing |
| Use existing Steinberg RingBuffer pattern | Well-tested SPSC implementation in SDK |

---

## Project Structure

### Documentation (this feature)

```text
specs/007-sweep-system/
├── plan.md              # This file
├── research.md          # Phase 0 output - research findings
├── data-model.md        # Phase 1 output - entity definitions
├── quickstart.md        # Phase 1 output - implementation guide
├── contracts/           # Phase 1 output - API contracts
│   ├── sweep_processor.h
│   ├── sweep_position_buffer.h
│   └── sweep_morph_link.h
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   └── sweep_curves.h           # Layer 0: Morph link curve functions
│   └── primitives/
│       └── sweep_position_buffer.h  # Layer 1: Lock-free SPSC buffer
└── tests/
    ├── core/
    │   └── sweep_curves_tests.cpp
    └── primitives/
        └── sweep_position_buffer_tests.cpp

plugins/Disrumpo/
├── src/
│   ├── dsp/
│   │   ├── sweep_processor.h        # Layer 3: Main sweep DSP
│   │   ├── sweep_processor.cpp
│   │   ├── sweep_lfo.h              # LFO wrapper for sweep modulation
│   │   ├── sweep_envelope.h         # Envelope follower wrapper
│   │   └── custom_curve.h           # Breakpoint curve for Custom mode
│   └── controller/
│       ├── sweep_indicator.h        # VSTGUI overlay control
│       └── sweep_indicator.cpp
├── tests/
│   ├── dsp/
│   │   ├── sweep_processor_tests.cpp
│   │   ├── sweep_morph_link_tests.cpp
│   │   └── sweep_automation_tests.cpp
│   └── controller/
│       └── sweep_indicator_tests.cpp (if applicable)
└── resources/
    └── editor.uidesc                 # Sweep panel UI additions
```

**Structure Decision**: Plugin-specific DSP in `plugins/Disrumpo/src/dsp/`, shared primitives in `dsp/`. UI controls in `plugins/Disrumpo/src/controller/`.

---

## Complexity Tracking

No violations requiring justification. Architecture follows existing patterns.

---

## Phase 0: Research Summary

### Key Clarifications Resolved (from clarify workflow)

| Question | Resolution | Rationale |
|----------|------------|-----------|
| Intensity scaling | Multiplicative | Preserves shape invariance, predictable behavior |
| LFO + envelope combination | Additive | Sum modulation amounts, clamp to range |
| Custom curve editor location | Dedicated section | Appears when Custom mode selected |
| Sweep frequency smoothing | OnePoleSmoother 10-50ms | Prevents zipper noise |
| Sharp falloff at edge | Exactly 0.0 | Clean mathematical model |

### Curve Formulas (from dsp-details.md Section 8)

```cpp
// Input: x = normalized sweep frequency [0, 1]
// Output: y = morph position [0, 1]

float applyMorphLinkCurve(MorphLinkMode mode, float x) {
    switch (mode) {
        case MorphLinkMode::None:         return 0.5f;  // Center (manual)
        case MorphLinkMode::SweepFreq:    return x;     // Linear
        case MorphLinkMode::InverseSweep: return 1.0f - x;
        case MorphLinkMode::EaseIn:       return x * x;
        case MorphLinkMode::EaseOut:      return 1.0f - (1.0f - x) * (1.0f - x);
        case MorphLinkMode::HoldRise:     return (x < 0.6f) ? 0.0f : (x - 0.6f) / 0.4f;
        case MorphLinkMode::Stepped:      return std::floor(x * 4.0f) / 3.0f;
        default:                          return x;
    }
}
```

### Gaussian Intensity Formula

```cpp
// Per spec FR-008, FR-009, FR-010
float calculateGaussianIntensity(float bandFreqHz, float sweepCenterHz,
                                  float widthOctaves, float intensityParam) {
    // Distance in octave space (FR-009)
    float distanceOctaves = std::abs(std::log2(bandFreqHz) - std::log2(sweepCenterHz));

    // Sigma = width / 2 (FR-006)
    float sigma = widthOctaves / 2.0f;

    // Gaussian falloff (FR-008)
    float falloff = std::exp(-0.5f * (distanceOctaves / sigma) * (distanceOctaves / sigma));

    // Scale by intensity (FR-010) - multiplicative scaling
    return intensityParam * falloff;
}
```

### Sharp (Linear) Falloff Formula

```cpp
// Per spec FR-006a
float calculateLinearFalloff(float bandFreqHz, float sweepCenterHz,
                              float widthOctaves, float intensityParam) {
    float distanceOctaves = std::abs(std::log2(bandFreqHz) - std::log2(sweepCenterHz));
    float halfWidth = widthOctaves / 2.0f;

    // Linear falloff, exactly 0.0 at edge
    float falloff = std::max(0.0f, 1.0f - distanceOctaves / halfWidth);

    return intensityParam * falloff;
}
```

### Audio-UI Synchronization Pattern

From custom-controls.md Section 1.3.4:

```cpp
struct SweepPositionData {
    float centerFreqHz;      // Current sweep center frequency
    float widthOctaves;      // Sweep width in octaves
    float intensity;         // 0.0-1.0, how much the sweep affects bands
    uint64_t samplePosition; // Sample count for timing sync
    bool enabled;            // Sweep on/off state
};

// Ring buffer: 8 entries (~100ms at typical block sizes)
static constexpr int kSweepBufferSize = 8;
```

---

## Phase 1: Design Artifacts

### Data Model Summary

See [data-model.md](data-model.md) for complete entity definitions.

**Key Entities:**
1. **SweepProcessor** - Main DSP class calculating per-band intensity
2. **SweepPositionData** - Struct for audio-UI communication
3. **SweepPositionBuffer** - Lock-free SPSC queue
4. **CustomCurve** - Breakpoint curve for Custom morph link mode

### API Contracts

See [contracts/](contracts/) directory for header specifications.

### Implementation Quickstart

See [quickstart.md](quickstart.md) for step-by-step implementation guide.

---

## Constitution Re-Check (Post-Design)

- [x] Principle II (Real-Time Safety): SweepProcessor is noexcept, no allocations in process
- [x] Principle III (Modern C++): Using C++20 features, RAII, value semantics
- [x] Principle IX (Layers): SweepProcessor at Layer 3, curves at Layer 0, buffer at Layer 1
- [x] Principle XII (Test-First): Test files defined in project structure
- [x] Principle XIV (ODR): No conflicts found, using existing components correctly

**Gate Status: PASSED**

---

## Implementation Notes

### Parameter ID Reference (from plugin_ids.h)

```cpp
enum class SweepParamType : uint8_t {
    kSweepEnable       = 0x00,  // 0x0E00
    kSweepFrequency    = 0x01,  // 0x0E01
    kSweepWidth        = 0x02,  // 0x0E02
    kSweepIntensity    = 0x03,  // 0x0E03
    kSweepMorphLink    = 0x04,  // 0x0E04
    kSweepFalloff      = 0x05,  // 0x0E05
};
```

**Note**: Custom morph link mode needs to be added to MorphLinkMode enum (currently has 7 modes, need 8 with Custom).

### UI Integration Points

1. **Sweep Panel** - New UI section with controls for all sweep parameters
2. **SpectrumDisplay** - Add SweepIndicator overlay rendering
3. **Custom Curve Editor** - Dedicated expandable section below the Morph Link dropdown in the Sweep Panel:
   - Appears only when sweep-morph link mode is set to "Custom" (FR-039a, FR-039b)
   - Location: Below the Morph Link dropdown, above the LFO/Envelope controls
   - Size: ~200px width × 150px height for the curve editing area
   - Interaction: Click to add breakpoint, drag to move, right-click to delete (2-8 points)
   - Shows linear interpolation between breakpoints with visual handles
4. **MIDI Learn** - Button for CC mapping to sweep frequency

### Test Categories

1. **Unit Tests**: Gaussian/Linear intensity calculations, curve functions
2. **Integration Tests**: SweepProcessor with MorphEngine linking
3. **UI Tests**: SweepIndicator rendering, audio-visual sync
4. **Automation Tests**: LFO, envelope follower, MIDI CC response
