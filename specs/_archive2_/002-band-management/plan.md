# Implementation Plan: Band Management

**Branch**: `002-band-management` | **Date**: 2026-01-28 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/002-band-management/spec.md`

## Summary

Implement the band management system for Disrumpo multiband morphing distortion plugin. This includes a `CrossoverNetwork` wrapper class for 1-8 band splitting using cascaded `CrossoverLR4` instances, `BandState` structure for per-band parameters (gain, pan, solo, bypass, mute), and phase-coherent band summation. The system processes L/R channels independently with sample-by-sample cascaded processing.

## Technical Context

**Language/Version**: C++20 (per Constitution Principle III)
**Primary Dependencies**:
- `Krate::DSP::CrossoverLR4` (dsp/include/krate/dsp/processors/crossover_filter.h)
- `Krate::DSP::OnePoleSmoother` (dsp/include/krate/dsp/primitives/smoother.h)
- Steinberg VST3 SDK (public.sdk/source/vst/)
**Storage**: N/A (state serialization via IBStream)
**Testing**: Catch2 (per specs/_architecture_/testing.md)
**Target Platform**: Windows, macOS, Linux (cross-platform per Constitution Principle VI)
**Project Type**: VST3 plugin (plugins/disrumpo/)
**Performance Goals**: Crossover network < 5% of per-config CPU budget, 512-sample block < 50 microseconds at 44.1kHz
**Constraints**: Real-time safety (no allocations in audio thread), Phase-coherent summation within +/-0.1 dB
**Scale/Scope**: 1-8 configurable bands, 8 BandState instances (fixed-size for RT safety)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Validation

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] CrossoverNetwork and BandState are DSP components (processor-side)
- [x] Controller only registers parameters, does not process audio
- [x] State flows Host -> Processor -> Controller via setComponentState()

**Required Check - Principle II (Real-Time Safety):**
- [x] All buffers pre-allocated in setupProcessing()
- [x] Fixed-size arrays (8 BandState, 7 CrossoverLR4 max)
- [x] No allocations in process()
- [x] std::atomic for parameter exchange

**Required Check - Principle IX (Layer Architecture):**
- [x] CrossoverNetwork uses Layer 2 CrossoverLR4 (valid)
- [x] BandState is data structure, no layer dependency
- [x] OnePoleSmoother is Layer 1 (valid dependency)

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

**Classes/Structs to be created**: CrossoverNetwork, BandState, BandProcessor (data holder)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| CrossoverNetwork | `grep -r "class CrossoverNetwork" dsp/ plugins/` | No | Create New (plugin-specific) |
| BandState | `grep -r "struct BandState" dsp/ plugins/` | No | Create New (matches dsp-details.md) |
| BandProcessor | `grep -r "class BandProcessor" dsp/ plugins/` | No | Create New (simple wrapper) |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| makeBandParamId | `grep -r "makeBandParamId" plugins/` | Yes | specs/Disrumpo/dsp-details.md | Implement from spec |
| logarithmicMidpoint | `grep -r "logarithmicMidpoint" dsp/ plugins/` | No | N/A | Create New |
| calculateEqualPowerPan | `grep -r "calculateEqualPowerPan" dsp/` | No | N/A | Create inline |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| CrossoverLR4 | dsp/include/krate/dsp/processors/crossover_filter.h | 2 | Internal crossover implementation |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (gain, pan, mute transitions) |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | dB to linear conversion for band gain |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal prevention in summation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (CrossoverLR4 exists)
- [x] `specs/_architecture_/` - Component inventory
- [x] `plugins/disrumpo/src/` - Existing plugin skeleton (no conflicting types)
- [x] `specs/Disrumpo/dsp-details.md` - BandState structure definition reference

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. BandState matches the specification in dsp-details.md exactly. CrossoverNetwork is plugin-specific and distinct from the existing Crossover3Way/Crossover4Way classes in crossover_filter.h.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| CrossoverLR4 | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| CrossoverLR4 | reset | `void reset() noexcept` | Yes |
| CrossoverLR4 | setCrossoverFrequency | `void setCrossoverFrequency(float hz) noexcept` | Yes |
| CrossoverLR4 | process | `[[nodiscard]] CrossoverLR4Outputs process(float input) noexcept` | Yes |
| CrossoverLR4Outputs | low | `float low = 0.0f` | Yes |
| CrossoverLR4Outputs | high | `float high = 0.0f` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/crossover_filter.h` - CrossoverLR4 class (lines 1-390)
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class (lines 1-273)
- [x] `dsp/include/krate/dsp/core/db_utils.h` - db conversion utilities

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CrossoverLR4 | Returns struct, not tuple | `auto outputs = crossover.process(input); float low = outputs.low;` |
| CrossoverLR4 | Must call prepare() before process() | Always check `isPrepared()` or call `prepare()` in setupProcessing() |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| OnePoleSmoother | Uses `isComplete()` not `isDone()` | `if (smoother.isComplete()) { ... }` |

## Layer 0 Candidate Analysis

*No new Layer 0 utilities needed for this feature.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| logarithmicMidpoint | One-liner, only used in band redistribution logic |
| calculateEqualPowerPan | Simple formula, inline in BandProcessor |

**Decision**: No new Layer 0 utilities. Equal-power pan is a well-known formula (spec FR-022), keep inline.

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-specific DSP (plugins/disrumpo/src/dsp/)

**Related features at same layer** (from roadmap.md):
- 003-distortion-integration (Week 3): Distortion processing per band
- 004-vstgui-infrastructure (Week 4): UI for band controls
- 005-morph-engine (Week 5): Morph interpolation per band

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| CrossoverNetwork | MEDIUM | Could be promoted to KrateDSP if other plugins need N-band crossover | Keep in plugin, evaluate after 003 |
| BandState | LOW | Disrumpo-specific structure | Keep in plugin |
| BandProcessor | MEDIUM | 003 will add distortion to this | Keep in plugin, extend in 003 |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep CrossoverNetwork in plugin | First implementation; promote to KrateDSP only if reuse proven |
| Fixed 8 BandState array | Real-time safety; no dynamic allocation |
| Sample-by-sample cascaded processing | Avoids intermediate buffer allocation per spec clarification |

## Project Structure

### Documentation (this feature)

```text
specs/002-band-management/
|-- plan.md              # This file
|-- research.md          # Phase 0 output (minimal - most questions pre-answered)
|-- data-model.md        # Phase 1 output
|-- quickstart.md        # Phase 1 output
|-- contracts/           # Phase 1 output (API contracts)
```

### Source Code (repository root)

```text
plugins/disrumpo/
|-- src/
|   |-- dsp/
|   |   +-- crossover_network.h       # New: CrossoverNetwork wrapper
|   |   +-- crossover_network.cpp     # New: Implementation (if needed)
|   |   +-- band_state.h              # New: BandState structure
|   |   +-- band_processor.h          # New: Per-band gain/pan/mute
|   |-- processor/
|   |   |-- processor.h               # Modified: Add band state, crossover network
|   |   |-- processor.cpp             # Modified: Band processing in process()
|   |-- controller/
|   |   |-- controller.cpp            # Modified: Register per-band parameters
|   |-- plugin_ids.h                  # Modified: Add per-band parameter IDs
|-- tests/
    +-- unit/
        +-- crossover_network_test.cpp  # New: CrossoverNetwork unit tests
        +-- band_processing_test.cpp    # New: Band gain/pan/mute tests

dsp/
|-- tests/
    +-- unit/processors/
        # CrossoverLR4 tests already exist (crossover_filter_test.cpp)
        # No new shared DSP components needed
```

**Structure Decision**: All band management code lives in `plugins/disrumpo/src/dsp/` following the monorepo pattern. No changes to shared KrateDSP library.

## Architecture Overview

### Class Design

#### CrossoverNetwork (plugins/disrumpo/src/dsp/crossover_network.h)

```cpp
class CrossoverNetwork {
public:
    static constexpr int kMaxBands = 8;
    static constexpr int kMinBands = 1;
    static constexpr int kDefaultBands = 4;

    void prepare(double sampleRate, int numBands) noexcept;
    void reset() noexcept;
    void setBandCount(int numBands) noexcept;
    void setCrossoverFrequency(int index, float hz) noexcept;

    // Sample-by-sample processing for single channel
    void process(float input, std::array<float, kMaxBands>& bands) noexcept;

private:
    double sampleRate_ = 44100.0;
    int numBands_ = kDefaultBands;
    std::array<Krate::DSP::CrossoverLR4, kMaxBands - 1> crossovers_;  // N-1 for N bands
    std::array<float, kMaxBands - 1> crossoverFrequencies_;
};
```

#### BandState (plugins/disrumpo/src/dsp/band_state.h)

```cpp
struct BandState {
    // Frequency bounds
    float lowFreqHz = 20.0f;
    float highFreqHz = 20000.0f;

    // Output controls
    float gainDb = 0.0f;            // [-24, +24] dB
    float pan = 0.0f;               // [-1, +1] (left to right)

    // Control flags
    bool solo = false;
    bool bypass = false;
    bool mute = false;

    // Morph fields (for future integration, not processed in this spec)
    float morphX = 0.5f;
    float morphY = 0.5f;
    int morphMode = 0;
    std::array<MorphNode, 4> nodes;  // Forward declaration
    int activeNodeCount = 2;
};
```

#### BandProcessor (plugins/disrumpo/src/dsp/band_processor.h)

```cpp
class BandProcessor {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Parameter setters (smoothed)
    void setGainDb(float db) noexcept;
    void setPan(float pan) noexcept;
    void setMute(bool muted) noexcept;

    // Process single sample stereo pair
    void process(float& left, float& right) noexcept;

private:
    Krate::DSP::OnePoleSmoother gainSmoother_;
    Krate::DSP::OnePoleSmoother panSmoother_;
    Krate::DSP::OnePoleSmoother muteSmoother_;  // For click-free mute

    float targetGainLinear_ = 1.0f;
    float targetPan_ = 0.0f;
    float targetMute_ = 0.0f;  // 0.0 = unmuted, 1.0 = muted
};
```

### Signal Flow

```
Input L/R
    |
    v
+-------------------+
| Input Gain        | (existing in skeleton)
+-------------------+
    |
    v
+-------------------------------------------+
|         CrossoverNetwork (L)               |
|  crossover[0] -> band[0], remainder        |
|  crossover[1] -> band[1], remainder        |
|  ...                                       |
|  remainder -> band[N-1]                    |
+-------------------------------------------+
    |
    v (parallel for R channel)
+-------------------------------------------+
|         CrossoverNetwork (R)               |
+-------------------------------------------+
    |
    v
+-------------------------------------------+
|  Per-Band Processing (BandProcessor x8)   |
|  - Gain (smoothed dB->linear)             |
|  - Pan (equal-power law)                  |
|  - Solo/Mute logic                        |
+-------------------------------------------+
    |
    v
+-------------------------------------------+
|         Band Summation                     |
|  Sum all active bands -> Stereo Out        |
|  (respects solo/mute states)              |
+-------------------------------------------+
    |
    v
Output L/R (to future processing stages)
```

### Solo/Mute Logic (FR-025a)

```cpp
bool shouldBandContribute(int bandIndex, bool anySoloActive) {
    const auto& state = bandStates_[bandIndex];

    if (state.mute) {
        return false;  // Mute always suppresses
    }

    if (anySoloActive) {
        return state.solo;  // Only soloed bands contribute
    }

    return true;  // No solo active, all unmuted bands contribute
}
```

## Implementation Phases

### Phase 1: CrossoverNetwork Core (T2.1-T2.4)

**Tasks:**
1. Create `crossover_network.h` with class declaration
2. Implement `prepare()`, `reset()`, `process()` methods
3. Write unit tests for 1-8 band configurations
4. Test flat response summation (+/-0.1 dB)

**Files:**
- `plugins/disrumpo/src/dsp/crossover_network.h` (new)
- `plugins/disrumpo/tests/unit/crossover_network_test.cpp` (new)

**Verification:**
- SC-001: Flat frequency response within +/-0.1 dB
- SC-007: Flat response at 44.1kHz, 48kHz, 96kHz, 192kHz

### Phase 2: BandState and BandProcessor (T2.5-T2.7)

**Tasks:**
1. Create `band_state.h` with structure definition
2. Create `band_processor.h` with gain/pan/mute processing
3. Implement equal-power pan law (FR-022)
4. Implement solo/mute logic with smoothing
5. Write unit tests for gain, pan, solo/mute behavior

**Files:**
- `plugins/disrumpo/src/dsp/band_state.h` (new)
- `plugins/disrumpo/src/dsp/band_processor.h` (new)
- `plugins/disrumpo/tests/unit/band_processing_test.cpp` (new)

**Verification:**
- FR-019-FR-027: All per-band processing requirements
- SC-005: Click-free solo/mute transitions

### Phase 3: Processor Integration (T2.6-T2.8)

**Tasks:**
1. Add band state array and crossover networks to Processor
2. Integrate into process() method
3. Add parameter handling for per-band parameters
4. Update state serialization (FR-037-FR-039)
5. Test full signal path

**Files:**
- `plugins/disrumpo/src/processor/processor.h` (modified)
- `plugins/disrumpo/src/processor/processor.cpp` (modified)
- `plugins/disrumpo/src/plugin_ids.h` (modified)

**Verification:**
- SC-002: Band count changes without glitches
- SC-003: < 50us per 512-sample block
- SC-004: State save/restore

### Phase 4: Controller Integration and Validation (T2.8-T2.9)

**Tasks:**
1. Register per-band parameters in Controller
2. Run pluginval
3. Final integration testing
4. Performance benchmarking

**Files:**
- `plugins/disrumpo/src/controller/controller.cpp` (modified)

**Verification:**
- SC-006: pluginval strictness level 1
- FR-031-FR-033: Flat response verification tests

## Testing Strategy

### Unit Tests (Catch2)

| Test File | Coverage |
|-----------|----------|
| crossover_network_test.cpp | FR-001 to FR-014 (crossover behavior) |
| band_processing_test.cpp | FR-015 to FR-027 (per-band processing) |

### Integration Tests

| Test | Description |
|------|-------------|
| IT-001 | Full signal path through crossover -> bands -> summation |
| IT-004 | Band count change during playback |

### Approval Tests

| Test | Verification |
|------|--------------|
| Flat response | Pink noise FFT analysis at multiple sample rates (FR-033) |

### Performance Tests

| Metric | Target | Method |
|--------|--------|--------|
| Block processing time | < 50us @ 512 samples, 44.1kHz | std::chrono benchmark |
| CPU usage | < 5% of per-config budget | Profile with representative load |

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Phase cancellation in summation | Low | High | LR4 crossovers guarantee phase-coherent sum; unit tests verify |
| Clicks during band count change | Medium | Medium | Parameter smoothing on new crossover frequencies; fade transitions |
| Memory allocation in audio thread | Low | Critical | Fixed-size arrays; no std::vector resize in process() |
| Parameter ID collision | Low | High | Systematic encoding per dsp-details.md; unit test ID uniqueness |

## Dependencies

### External Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| CrossoverLR4 | Current | 2-way LR4 crossover implementation |
| OnePoleSmoother | Current | Parameter smoothing |
| Catch2 | 3.x | Unit testing framework |

### Internal Dependencies

| This Spec | Depends On | Status |
|-----------|------------|--------|
| 002-band-management | 001-plugin-skeleton | Complete |

| Depends On This | Spec | Notes |
|-----------------|------|-------|
| 003-distortion-integration | Week 3 | Adds distortion to BandProcessor |
| 004-vstgui-infrastructure | Week 4 | UI for band controls |

## Complexity Tracking

> No Constitution violations requiring justification.

## Deliverables Summary

After completion:
1. `CrossoverNetwork` class with 1-8 band support
2. `BandState` structure matching dsp-details.md
3. `BandProcessor` with gain/pan/solo/mute
4. Updated Processor with full band processing chain
5. Per-band parameter registration in Controller
6. Comprehensive unit tests
7. pluginval validation passing
