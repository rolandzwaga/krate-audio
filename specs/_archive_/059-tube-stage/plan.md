# Implementation Plan: TubeStage Processor

**Branch**: `059-tube-stage` | **Date**: 2026-01-13 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/059-tube-stage/spec.md`

## Summary

TubeStage is a Layer 2 processor modeling a single triode tube gain stage with configurable input drive, output gain, bias (asymmetry), and saturation amount. It composes Layer 1 primitives (Waveshaper with WaveshapeType::Tube, DCBlocker, OnePoleSmoother) into a cohesive gain stage module with parameter smoothing and full bypass capability at saturation=0.0.

Signal chain: Input -> [Input Gain (smoothed)] -> [Waveshaper (Tube + asymmetry)] -> [DC Blocker] -> [Output Gain (smoothed)] -> Blend with Dry (saturation amount smoothed)

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 (db_utils.h), Layer 1 (waveshaper.h, dc_blocker.h, smoother.h)
**Storage**: N/A (stateful DSP processor with internal buffers)
**Testing**: Catch2 (unit tests, spectral analysis, artifact detection)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component in KrateDSP monorepo
**Performance Goals**: < 0.5% CPU per instance (Layer 2 budget), 512-sample buffer < 100us at 44.1kHz
**Constraints**: Real-time safe (no allocations in process()), header-only implementation
**Scale/Scope**: Single-channel processor; users instantiate per channel for stereo

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in process() - all state pre-allocated in prepare()
- [x] All processing methods marked noexcept
- [x] No exceptions, locks, or I/O in audio path

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 2 processor depends only on Layer 0 and Layer 1
- [x] Uses existing Layer 1 primitives (Waveshaper, DCBlocker, OnePoleSmoother)

**Required Check - Principle X (DSP Constraints):**
- [x] DC blocking after asymmetric saturation (per constitution)
- [x] No internal oversampling (per spec - handled externally)

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

**Classes/Structs to be created**: TubeStage

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TubeStage | `grep -r "class TubeStage" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None - all utilities exist in Layer 0/1

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" dsp/` | Yes | core/db_utils.h | Reuse |
| gainToDb | `grep -r "gainToDb" dsp/` | Yes | core/db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Core saturation with WaveshapeType::Tube, setAsymmetry() for bias |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | DC removal after asymmetric saturation |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing for input gain, output gain, saturation amount |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Convert dB parameters to linear gain |
| kDefaultSmoothingTimeMs | dsp/include/krate/dsp/primitives/smoother.h | 1 | Standard 5ms smoothing time constant |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (reference pattern)
- [x] ARCHITECTURE.md - Component inventory (no TubeStage exists)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: TubeStage is a new class not present in codebase. All dependencies are established Layer 0/1 components. No naming conflicts identified.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Yes |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Yes |
| Waveshaper | setAsymmetry | `void setAsymmetry(float bias) noexcept` | Yes |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Waveshaper | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | getTarget | `[[nodiscard]] float getTarget() const noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapToTarget | `void snapToTarget() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| dbToGain | - | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain function

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Waveshaper | setAsymmetry maps 1:1 from bias parameter (clamped to [-1,1]) | `waveshaper_.setAsymmetry(bias_)` |
| Waveshaper | setDrive uses linear gain, not dB | `waveshaper_.setDrive(linearInputGain)` |
| DCBlocker | Must call prepare() before process() | Call in TubeStage::prepare() |
| OnePoleSmoother | configure() takes smoothTimeMs and sampleRate | Use kDefaultSmoothingTimeMs (5.0f) |
| OnePoleSmoother | snapToTarget() snaps current to target (no ramp) | Call in reset() per FR-025 |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | No new utilities needed | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| - | All required utilities exist in Layer 0/1 |

**Decision**: No Layer 0 extraction needed. TubeStage purely composes existing primitives.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer** (from DST-ROADMAP):
- DiodeClipper: Similar gain staging structure
- FuzzProcessor: Similar bias control and DC blocking
- TapeSaturator: Similar gain staging and smoothing patterns
- WavefolderProcessor: Similar DC blocking after nonlinear processing

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Signal chain pattern (input gain -> waveshaper -> DC block -> output gain -> mix) | HIGH | DiodeClipper, FuzzProcessor, TapeSaturator | Keep local - extract after 2nd processor uses same pattern |
| Parameter smoothing setup (3 smoothers, 5ms, snapToTarget in reset) | MEDIUM | All Layer 2 processors | Keep local - consistent with SaturationProcessor pattern |
| Gain clamping range [-24, +24] dB | MEDIUM | All gain-based processors | Already established in SaturationProcessor |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First processor at this pattern - wait for concrete reuse evidence |
| Keep signal chain local | Follow SaturationProcessor pattern; extract common base later if 3+ processors use same structure |

### Review Trigger

After implementing **DiodeClipper** or **FuzzProcessor**, review this section:
- [ ] Does sibling need same signal chain? -> Extract to shared base
- [ ] Does sibling use same 3-smoother pattern? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/059-tube-stage/
├── spec.md              # Feature specification
├── plan.md              # This file
├── checklists/          # Requirements checklist
└── (no additional artifacts needed - straightforward implementation)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── tube_stage.h      # NEW: TubeStage header-only implementation
└── tests/
    └── unit/
        └── processors/
            └── tube_stage_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Standard KrateDSP processor layout - header-only in processors/, tests in unit/processors/

## Implementation Design

### Class Interface

```cpp
namespace Krate {
namespace DSP {

class TubeStage {
public:
    // Constants
    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = +24.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    // Lifecycle (FR-001, FR-002, FR-003)
    TubeStage() noexcept;
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Parameter Setters (FR-004 to FR-011)
    void setInputGain(float dB) noexcept;
    void setOutputGain(float dB) noexcept;
    void setBias(float bias) noexcept;
    void setSaturationAmount(float amount) noexcept;

    // Getters (FR-012 to FR-015)
    [[nodiscard]] float getInputGain() const noexcept;
    [[nodiscard]] float getOutputGain() const noexcept;
    [[nodiscard]] float getBias() const noexcept;
    [[nodiscard]] float getSaturationAmount() const noexcept;

    // Processing (FR-016 to FR-020)
    void process(float* buffer, size_t numSamples) noexcept;

private:
    // Parameters (stored in user units)
    float inputGainDb_ = 0.0f;
    float outputGainDb_ = 0.0f;
    float bias_ = 0.0f;
    float saturationAmount_ = 1.0f;

    // Parameter smoothers (FR-021 to FR-025)
    OnePoleSmoother inputGainSmoother_;
    OnePoleSmoother outputGainSmoother_;
    OnePoleSmoother saturationSmoother_;

    // DSP components
    Waveshaper waveshaper_;
    DCBlocker dcBlocker_;

    // Sample rate for calculations
    double sampleRate_ = 44100.0;
};

} // namespace DSP
} // namespace Krate
```

### Signal Flow Implementation

```cpp
void TubeStage::process(float* buffer, size_t numSamples) noexcept {
    // FR-019: Handle n=0 gracefully
    if (numSamples == 0) return;

    // Process sample-by-sample for parameter smoothing
    for (size_t i = 0; i < numSamples; ++i) {
        // Advance smoothers
        const float inputGain = inputGainSmoother_.process();
        const float outputGain = outputGainSmoother_.process();
        const float satAmount = saturationSmoother_.process();

        // FR-020: Full bypass when saturation amount is 0.0
        if (satAmount < 0.0001f) {
            // Skip waveshaper AND DC blocker - output equals input
            continue;
        }

        // Store dry sample for blend
        const float dry = buffer[i];

        // Apply input gain
        float wet = dry * inputGain;

        // Apply waveshaper (Tube type with bias already set)
        wet = waveshaper_.process(wet);

        // Apply DC blocking
        wet = dcBlocker_.process(wet);

        // Apply output gain to wet signal only (per clarification)
        wet *= outputGain;

        // Blend dry/wet based on saturation amount
        buffer[i] = dry * (1.0f - satAmount) + wet * satAmount;
    }
}
```

### Key Implementation Notes

1. **Bias Mapping (FR-024)**: Bias maps 1:1 to Waveshaper asymmetry. Set in setBias():
   ```cpp
   void setBias(float bias) noexcept {
       bias_ = std::clamp(bias, -1.0f, 1.0f);
       waveshaper_.setAsymmetry(bias_);  // 1:1 mapping
   }
   ```

2. **Waveshaper Configuration**: Configure once in prepare():
   ```cpp
   waveshaper_.setType(WaveshapeType::Tube);
   waveshaper_.setDrive(1.0f);  // Drive handled by input gain
   ```

3. **Reset Behavior (FR-025)**: Smoothers snap to current targets:
   ```cpp
   void reset() noexcept {
       inputGainSmoother_.snapToTarget();
       outputGainSmoother_.snapToTarget();
       saturationSmoother_.snapToTarget();
       dcBlocker_.reset();
   }
   ```

4. **Gain Smoothing**: Smoothers operate in linear domain:
   ```cpp
   void setInputGain(float dB) noexcept {
       inputGainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
       inputGainSmoother_.setTarget(dbToGain(inputGainDb_));
   }
   ```

## Test Strategy

### Test File Structure

```cpp
// tube_stage_test.cpp

// Phase 1: Foundational Tests
TEST_CASE("TubeStage default construction", "[tube_stage][foundational]")
TEST_CASE("TubeStage prepare and reset", "[tube_stage][foundational]")

// Phase 2: Parameter Tests (FR-004 to FR-015)
TEST_CASE("US2: Input gain drives saturation harder", "[tube_stage][US2]")
TEST_CASE("US2: Input gain clamping", "[tube_stage][US2]")
TEST_CASE("US3: Bias affects even harmonic content", "[tube_stage][US3]")
TEST_CASE("US4: Saturation amount 0 bypasses entirely", "[tube_stage][US4][SC-003]")
TEST_CASE("US5: Output gain scales wet signal", "[tube_stage][US5]")

// Phase 3: Processing Tests (FR-016 to FR-020)
TEST_CASE("US1: Produces even harmonics", "[tube_stage][US1][SC-001]")
TEST_CASE("US1: THD > 5% at +24dB drive", "[tube_stage][US1][SC-002]")
TEST_CASE("US6: No zipper noise on parameter change", "[tube_stage][US6][SC-008]")

// Phase 4: DC Blocking Tests (FR-026, FR-027)
TEST_CASE("US6: DC offset < 0.001 after 500ms", "[tube_stage][SC-004]")

// Phase 5: Real-Time Safety (FR-018)
TEST_CASE("US1: noexcept verification", "[tube_stage][realtime]")
TEST_CASE("US7: Process 1M samples without NaN/Inf", "[tube_stage][SC-005]")
TEST_CASE("US7: 512 samples < 100us", "[tube_stage][SC-006]")
```

### Success Criteria Mapping

| SC | Test Method | Expected Result |
|----|-------------|-----------------|
| SC-001 | FFT analysis of 1kHz sine with +12dB input gain | 2nd harmonic > -30dB relative to fundamental |
| SC-002 | THD measurement at 0.5 amplitude sine, +24dB drive | THD > 5% |
| SC-003 | Compare output to input at saturation=0.0 | Relative error < 1e-6 |
| SC-004 | Process DC input, measure decay | DC < 1% within 500ms |
| SC-005 | Process 1M samples of valid audio | No NaN/Inf in output |
| SC-006 | Time 512-sample process() call | < 100 microseconds |
| SC-007 | Code coverage analysis | 100% public method coverage |
| SC-008 | Check output discontinuities during parameter change | No discontinuities > 0.01 |

### Spectral Analysis Tests

Use existing test utilities from `spectral_analysis.h` and `signal_metrics.h`:

```cpp
// Even harmonic verification (SC-001)
TEST_CASE("TubeStage produces even harmonics", "[tube_stage][spectral]") {
    using namespace Krate::DSP::TestUtils;

    TubeStage stage;
    stage.prepare(44100.0, 8192);
    stage.setInputGain(12.0f);
    stage.setSaturationAmount(1.0f);

    std::vector<float> buffer(8192);
    generateSine(buffer.data(), 8192, 1000.0f, 44100.0f, 1.0f);
    stage.process(buffer.data(), 8192);

    // Measure 2nd harmonic using SignalMetrics
    float thd = SignalMetrics::calculateTHD(buffer.data(), 8192, 1000.0f, 44100.0f, 10);
    // Also verify 2nd harmonic specifically using DFT bin analysis
    float secondHarmonic = measureHarmonicMagnitude(buffer.data(), 8192, 372);  // 2kHz bin
    float fundamental = measureHarmonicMagnitude(buffer.data(), 8192, 186);     // 1kHz bin

    REQUIRE(20.0f * std::log10(secondHarmonic / fundamental) > -30.0f);
}
```

## Complexity Tracking

> No constitution violations identified.

| Consideration | Resolution |
|---------------|------------|
| No oversampling | Per spec - aliasing mitigation deferred to external handling |
| No ADAA | Per spec - use existing Waveshaper without internal ADAA |
| Header-only | Standard for KrateDSP processors |

## Task Breakdown Preview

Implementation will follow this sequence:

1. **T001**: Create test file skeleton with foundational tests
2. **T002**: Implement class skeleton with default constructor and prepare/reset
3. **T003**: Add parameter setters/getters with clamping
4. **T004**: Implement process() with signal chain
5. **T005**: Add parameter smoothing
6. **T006**: Add bypass logic for saturation=0.0
7. **T007**: Add spectral analysis tests (SC-001, SC-002)
8. **T008**: Add DC blocking tests (SC-004)
9. **T009**: Add real-time safety tests (SC-005, SC-006, SC-008)
10. **T010**: Final verification and documentation

Estimated effort: ~4 hours total
