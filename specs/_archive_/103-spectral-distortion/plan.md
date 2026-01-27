# Implementation Plan: Spectral Distortion Processor

**Branch**: `103-spectral-distortion` | **Date**: 2026-01-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/103-spectral-distortion/spec.md`

## Summary

Implement a Layer 2 DSP Processor that applies distortion algorithms to individual frequency bins in the spectral domain. The processor composes existing STFT, OverlapAdd, SpectralBuffer, and Waveshaper primitives to create four distinct spectral distortion modes: PerBinSaturate, MagnitudeOnly, BinSelective, and SpectralBitcrush. Key differentiators include per-frequency-bin waveshaping (creating "impossible" distortion effects unavailable in time-domain processing), optional phase modification vs. exact phase preservation, frequency-band-selective drive control, and spectral magnitude quantization.

## Technical Context

**Language/Version**: C++20 (per Constitution Principle III)
**Primary Dependencies**:
- Layer 1: STFT, OverlapAdd, SpectralBuffer (stft.h, spectral_buffer.h)
- Layer 1: Waveshaper, WaveshapeType (waveshaper.h)
- Layer 1: spectral_utils.h (bin-frequency conversion)
- Layer 0: math_constants.h, db_utils.h
**Storage**: N/A (real-time processor, no persistence)
**Testing**: Catch2 (per Constitution Principle VIII)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: DSP library component (header-only in dsp/include/krate/dsp/processors/)
**Performance Goals**: < 0.5% CPU per instance (Layer 2 budget per Constitution Principle XI)
**Constraints**: Real-time safe processing (noexcept, no allocations in processBlock)
**Scale/Scope**: Single-channel processor; stereo via dual instances

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II - Real-Time Safety:**
- [x] processBlock() will be noexcept with no heap allocations
- [x] All buffers allocated in prepare(), not during processing
- [x] No locks, mutexes, or blocking primitives in audio path
- [x] Denormal flushing applied to spectral data (FR-027)

**Principle III - Modern C++:**
- [x] C++20 targeting with constexpr and noexcept
- [x] RAII for resource management
- [x] No raw new/delete

**Principle IX - Layered Architecture:**
- [x] Layer 2 processor depending only on Layers 0-1
- [x] No circular dependencies

**Principle X - DSP Constraints:**
- [x] No internal oversampling (spectral processing doesn't require it - distortion applied to magnitudes, not waveforms)
- [x] DC blocking considered but handled via DC/Nyquist bin exclusion (FR-018)
- [x] COLA-compliant reconstruction via existing OverlapAdd

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: SpectralDistortion, SpectralDistortionMode, GapBehavior, BandConfig

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SpectralDistortion | `grep -r "class SpectralDistortion" dsp/ plugins/` | No | Create New |
| SpectralDistortionMode | `grep -r "SpectralDistortionMode" dsp/ plugins/` | No | Create New |
| GapBehavior | `grep -r "GapBehavior" dsp/ plugins/` | No | Create New |
| BandConfig | `grep -r "struct BandConfig" dsp/ plugins/` | No | Create New (internal only) |

**Utility Functions to be created**: None new - reusing existing spectral_utils.h

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| frequencyToBin | `grep -r "frequencyToBin" dsp/` | Yes | spectral_utils.h | Reuse |
| binToFrequency | `grep -r "binToFrequency" dsp/` | Yes | spectral_utils.h | Reuse |
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | spectral_utils.h (detail::) | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| STFT | dsp/include/krate/dsp/primitives/stft.h | 1 | Forward FFT analysis with windowing |
| OverlapAdd | dsp/include/krate/dsp/primitives/stft.h | 1 | Inverse FFT synthesis with COLA |
| SpectralBuffer | dsp/include/krate/dsp/primitives/spectral_buffer.h | 1 | Spectrum storage with getMagnitude/setMagnitude/getPhase/setPhase |
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Per-bin saturation curves (process() method) |
| WaveshapeType | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Enumeration of 9 saturation curves |
| frequencyToBin | dsp/include/krate/dsp/primitives/spectral_utils.h | 1 | Convert Hz to fractional bin index |
| frequencyToBinNearest | dsp/include/krate/dsp/primitives/spectral_utils.h | 1 | Convert Hz to nearest integer bin |
| binToFrequency | dsp/include/krate/dsp/primitives/spectral_utils.h | 1 | Convert bin index to Hz |
| detail::flushDenormal | dsp/include/krate/dsp/primitives/spectral_utils.h | 1 | Denormal flushing for magnitudes |
| detail::isNaN | dsp/include/krate/dsp/core/math_constants.h | 0 | NaN detection (bit-level, fast-math safe) |
| detail::isInf | dsp/include/krate/dsp/core/math_constants.h | 0 | Infinity detection (bit-level) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (checked spectral_gate.h for patterns)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. The name "SpectralDistortion" is distinctive and does not conflict with existing "SpectralGate", "SpectralTilt", or "SpectralMorphFilter" processors.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| STFT | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | Y |
| STFT | reset | `void reset() noexcept` | Y |
| STFT | pushSamples | `void pushSamples(const float* input, size_t numSamples) noexcept` | Y |
| STFT | canAnalyze | `[[nodiscard]] bool canAnalyze() const noexcept` | Y |
| STFT | analyze | `void analyze(SpectralBuffer& output) noexcept` | Y |
| STFT | latency | `[[nodiscard]] size_t latency() const noexcept` | Y |
| OverlapAdd | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | Y |
| OverlapAdd | reset | `void reset() noexcept` | Y |
| OverlapAdd | synthesize | `void synthesize(const SpectralBuffer& input) noexcept` | Y |
| OverlapAdd | samplesAvailable | `[[nodiscard]] size_t samplesAvailable() const noexcept` | Y |
| OverlapAdd | pullSamples | `void pullSamples(float* output, size_t numSamples) noexcept` | Y |
| SpectralBuffer | prepare | `void prepare(size_t fftSize) noexcept` | Y |
| SpectralBuffer | reset | `void reset() noexcept` | Y |
| SpectralBuffer | getMagnitude | `[[nodiscard]] float getMagnitude(size_t bin) const noexcept` | Y |
| SpectralBuffer | getPhase | `[[nodiscard]] float getPhase(size_t bin) const noexcept` | Y |
| SpectralBuffer | setMagnitude | `void setMagnitude(size_t bin, float magnitude) noexcept` | Y |
| SpectralBuffer | setPhase | `void setPhase(size_t bin, float phase) noexcept` | Y |
| SpectralBuffer | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Y |
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Y |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Y |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Y |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/stft.h` - STFT and OverlapAdd classes
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer class
- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class, WaveshapeType enum
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - Bin/frequency conversion utilities
- [x] `dsp/include/krate/dsp/primitives/fft.h` - Complex struct (used by SpectralBuffer)
- [x] `dsp/include/krate/dsp/processors/spectral_gate.h` - Reference implementation pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SpectralBuffer | Uses getMagnitude/setMagnitude, NOT mag() | `spectrum.getMagnitude(bin)` |
| STFT | latency() returns fftSize, not hopSize | `stft_.latency()` equals fftSize |
| Waveshaper | drive=0 returns 0, not bypass | Check drive==0 to bypass waveshaper |
| SpectralBuffer | numBins is fftSize/2+1, includes DC and Nyquist | Iterate `for (bin = 0; bin < numBins; ++bin)` |
| OverlapAdd | Must match STFT's fftSize and hopSize exactly | Use same values in both prepare() calls |

## Layer 0 Candidate Analysis

*No new Layer 0 utilities needed. All required utilities exist in spectral_utils.h.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| applyBinDistortion | Specific to spectral distortion algorithm, 1 consumer |
| quantizeMagnitude | SpectralBitcrush-specific, 1 consumer |
| getBandDrive | BinSelective-specific, 1 consumer |

**Decision**: No Layer 0 extraction needed. All new functions are feature-specific.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from DST-ROADMAP.md):
- FormantDistortion (Phase 5.3) - spectral processing with waveshaping on formant regions
- SpectralFreeze - spectral domain freeze effect
- SpectralMorphFilter - already exists, spectral filtering

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Band assignment logic | MEDIUM | FormantDistortion | Keep local (1 consumer so far) |
| DC/Nyquist exclusion pattern | HIGH | All spectral processors | Already exists in SpectralGate pattern |
| Magnitude quantization | LOW | None identified | Keep local |
| Phase-preserving processing pattern | HIGH | Common in spectral processors | Pattern exists, not extracted |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | SpectralGate shows composition pattern is preferred |
| Keep band config local | Only one consumer; extract if FormantDistortion needs it |
| Follow SpectralGate pattern | Proven pattern for STFT+OverlapAdd composition |

### Review Trigger

After implementing **FormantDistortion**, review this section:
- [ ] Does FormantDistortion need band assignment logic? -> Extract to shared utility
- [ ] Does FormantDistortion use same STFT composition pattern? -> Document pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/103-spectral-distortion/
+-- spec.md              # Feature specification (input)
+-- plan.md              # This file
+-- research.md          # Phase 0 output (N/A - no clarifications needed)
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
    +-- spectral_distortion.h  # API contract
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
    +-- processors/
        +-- spectral_distortion.h    # Main implementation (header-only)
+-- tests/
    +-- unit/
        +-- processors/
            +-- spectral_distortion_test.cpp  # Unit tests
```

**Structure Decision**: Header-only implementation in `processors/` following SpectralGate pattern. Tests in `dsp/tests/unit/processors/`.

## Architecture Decisions

### AD-001: Composition over Inheritance

**Decision**: Compose STFT + OverlapAdd + SpectralBuffer + Waveshaper rather than inheriting from a base class.

**Rationale**: SpectralGate demonstrates this pattern successfully. Composition provides flexibility and avoids tight coupling.

### AD-002: Phase Handling Strategy

**Decision**: Implement two distinct phase behaviors:
- MagnitudeOnly/SpectralBitcrush: Extract phase BEFORE processing, restore EXACTLY after
- PerBinSaturate/BinSelective: Allow phase to evolve naturally through spectral processing

**Rationale**: Spec explicitly differentiates these modes (FR-005 vs FR-006). MagnitudeOnly is for "surgical" control while PerBinSaturate allows "natural spectral interaction."

**Implementation**:
```cpp
// MagnitudeOnly: Store and restore phase
float phase = inputSpectrum_.getPhase(bin);
float magnitude = inputSpectrum_.getMagnitude(bin);
float newMagnitude = processWithWaveshaper(magnitude, drive);
outputSpectrum_.setMagnitude(bin, newMagnitude);
outputSpectrum_.setPhase(bin, phase);  // Exact restoration

// PerBinSaturate: Process real/imag directly (phase may drift)
// Actually, we still process magnitude but allow reconstruction to affect phase
```

### AD-003: Drive=0 Bypass Optimization

**Decision**: When drive=0, bypass waveshaper computation entirely (FR-019).

**Rationale**: Division by zero prevention and performance optimization. Pass bins through unmodified.

### AD-004: DC/Nyquist Handling

**Decision**: Exclude DC (bin 0) and Nyquist (bin fftSize/2) by default, with opt-in via setProcessDCNyquist(true).

**Rationale**:
- DC bin with asymmetric curves introduces DC offset
- Nyquist bin is real-only (no phase component)
- Opt-in preserves flexibility for users who want full-spectrum processing

### AD-005: Band Gap Handling

**Decision**: Two modes via GapBehavior enum:
- Passthrough (default): Unassigned bins pass through unmodified
- UseGlobalDrive: Unassigned bins use global drive parameter

**Rationale**: Spec explicitly defines this (FR-016). Default is safe (no unexpected modification).

### AD-006: Overlap Conflict Resolution

**Decision**: When bands overlap, use highest drive value among overlapping bands.

**Rationale**: Spec requirement FR-023. Maximizes effect in contested regions (more intuitive than averaging).

## Component Design

### SpectralDistortionMode Enumeration

```cpp
enum class SpectralDistortionMode : uint8_t {
    PerBinSaturate = 0,   // Natural phase evolution
    MagnitudeOnly = 1,    // Exact phase preservation
    BinSelective = 2,     // Per-band drive control
    SpectralBitcrush = 3  // Magnitude quantization
};
```

### GapBehavior Enumeration

```cpp
enum class GapBehavior : uint8_t {
    Passthrough = 0,     // Unassigned bins pass through
    UseGlobalDrive = 1   // Unassigned bins use global drive
};
```

### BandConfig Structure (Internal)

```cpp
struct BandConfig {
    float lowHz = 0.0f;
    float highHz = 0.0f;
    float drive = 1.0f;
    size_t lowBin = 0;
    size_t highBin = 0;
};
```

### SpectralDistortion Class Interface

```cpp
class SpectralDistortion {
public:
    // Constants
    static constexpr size_t kMinFFTSize = 256;
    static constexpr size_t kMaxFFTSize = 8192;
    static constexpr size_t kDefaultFFTSize = 2048;
    static constexpr float kMinDrive = 0.0f;
    static constexpr float kMaxDrive = 10.0f;
    static constexpr float kMinBits = 1.0f;
    static constexpr float kMaxBits = 16.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t fftSize = 2048) noexcept;
    void reset() noexcept;

    // Processing
    float process(float input) noexcept;  // Single-sample (for convenience)
    void processBlock(const float* input, float* output, size_t numSamples) noexcept;

    // Mode Selection
    void setMode(SpectralDistortionMode mode) noexcept;
    [[nodiscard]] SpectralDistortionMode getMode() const noexcept;

    // Global Parameters
    void setDrive(float drive) noexcept;
    [[nodiscard]] float getDrive() const noexcept;
    void setSaturationCurve(WaveshapeType curve) noexcept;
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept;
    void setProcessDCNyquist(bool enabled) noexcept;
    [[nodiscard]] bool getProcessDCNyquist() const noexcept;

    // Bin-Selective Parameters
    void setLowBand(float freqHz, float drive) noexcept;
    void setMidBand(float lowHz, float highHz, float drive) noexcept;
    void setHighBand(float freqHz, float drive) noexcept;
    void setGapBehavior(GapBehavior mode) noexcept;

    // SpectralBitcrush Parameters
    void setMagnitudeBits(float bits) noexcept;
    [[nodiscard]] float getMagnitudeBits() const noexcept;

    // Query
    [[nodiscard]] size_t latency() const noexcept;
    [[nodiscard]] size_t getFftSize() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // STFT components
    STFT stft_;
    OverlapAdd overlapAdd_;
    SpectralBuffer inputSpectrum_;
    SpectralBuffer outputSpectrum_;

    // Processing
    Waveshaper waveshaper_;

    // Mode and parameters
    SpectralDistortionMode mode_ = SpectralDistortionMode::PerBinSaturate;
    float drive_ = 1.0f;
    float magnitudeBits_ = 16.0f;
    bool processDCNyquist_ = false;
    GapBehavior gapBehavior_ = GapBehavior::Passthrough;

    // Band configuration
    BandConfig lowBand_;
    BandConfig midBand_;
    BandConfig highBand_;

    // Cached values
    double sampleRate_ = 44100.0;
    size_t fftSize_ = kDefaultFFTSize;
    size_t hopSize_ = kDefaultFFTSize / 2;
    size_t numBins_ = kDefaultFFTSize / 2 + 1;
    bool prepared_ = false;

    // Phase storage for MagnitudeOnly mode
    std::vector<float> storedPhases_;

    // Internal methods
    void processSpectralFrame() noexcept;
    void applyPerBinSaturate() noexcept;
    void applyMagnitudeOnly() noexcept;
    void applyBinSelective() noexcept;
    void applySpectralBitcrush() noexcept;
    [[nodiscard]] float getDriveForBin(size_t bin) const noexcept;
    void updateBandBins() noexcept;
};
```

## Implementation Approach

### Phase 1: Foundation (Tests First)

1. Create test file with basic tests for prepare/reset/latency
2. Implement SpectralDistortion shell with prepare(), reset(), latency()
3. Verify compilation and basic tests pass

### Phase 2: PerBinSaturate Mode

1. Write tests for sine wave distortion, harmonics generation
2. Implement processSpectralFrame() and applyPerBinSaturate()
3. Test that drive=0 bypasses, drive>1 adds harmonics

### Phase 3: MagnitudeOnly Mode

1. Write tests for exact phase preservation (< 0.001 radians error)
2. Implement applyMagnitudeOnly() with phase storage
3. Verify phase preservation test passes

### Phase 4: BinSelective Mode

1. Write tests for frequency-selective distortion
2. Implement band assignment, overlap resolution, gap behavior
3. Test that different bands receive different drive

### Phase 5: SpectralBitcrush Mode

1. Write tests for magnitude quantization at various bit depths
2. Implement applySpectralBitcrush() with quantization formula
3. Test 1-bit (binary), 4-bit (visible steps), 16-bit (transparent)

### Phase 6: Edge Cases and Success Criteria

1. Silence in/out test (noise floor < -120dB)
2. Unity gain test (drive=1, tanh curve, < -0.1dB level change)
3. Round-trip reconstruction test (< -60dB error)
4. CPU performance test (< 0.5% at 44.1kHz)

## Test Strategy

### Unit Tests (dsp/tests/unit/processors/spectral_distortion_test.cpp)

**Foundation Tests:**
- prepare() with valid/invalid FFT sizes
- reset() clears state
- latency() returns FFT size
- isPrepared() state tracking

**PerBinSaturate Mode Tests:**
- Sine wave produces harmonics with drive > 1
- Silence in produces silence out
- Drive=0 bypasses processing
- Different curves produce different harmonic content

**MagnitudeOnly Mode Tests:**
- Phase error < 0.001 radians across all bins
- Magnitude changes while phase preserved
- Works with complex multi-partial signals

**BinSelective Mode Tests:**
- Different bands receive different drive amounts
- Gap behavior: Passthrough vs UseGlobalDrive
- Overlap resolution uses highest drive
- Frequency boundaries map to correct bins

**SpectralBitcrush Mode Tests:**
- 1-bit: All non-zero bins same magnitude
- 4-bit: Visible quantization steps (16 levels)
- 16-bit: Perceptually transparent
- Phase preserved exactly

**Success Criteria Tests:**
- SC-001: Phase preservation < 0.001 radians
- SC-002: Unity gain within -0.1dB
- SC-003: Latency = FFT size
- SC-004: CPU < 0.5% (profile test)
- SC-005: Round-trip < -60dB error
- SC-006: Silence noise floor < -120dB
- SC-007: Four modes produce distinct output

### Test Helpers (from SpectralGate pattern)

```cpp
void generateSine(float* buffer, size_t size, float freq, float sampleRate);
float calculateRMS(const float* buffer, size_t size);
float linearToDb(float linear);
void generateWhiteNoise(float* buffer, size_t size, uint32_t seed);
```

## File Locations

| Artifact | Path |
|----------|------|
| Implementation | `dsp/include/krate/dsp/processors/spectral_distortion.h` |
| Tests | `dsp/tests/unit/processors/spectral_distortion_test.cpp` |
| Architecture Update | `specs/_architecture_/layer-2-processors.md` |

## Complexity Tracking

> No Constitution violations requiring justification.

| Aspect | Complexity | Justification |
|--------|------------|---------------|
| Four modes | Medium | Each mode has distinct behavior per spec |
| Band assignment | Medium | Required for BinSelective mode |
| Phase preservation | Low | Store/restore pattern from SpectralGate |

## Risk Assessment

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| Phase preservation accuracy | Low | Use exact same bin for store/restore |
| Drive=0 division by zero | None | Explicit bypass when drive=0 |
| Band overlap edge cases | Low | Use highest drive (deterministic) |
| CPU performance | Low | Follow SpectralGate optimizations |

## Dependencies

**Must exist before implementation:**
- [x] STFT (stft.h) - exists
- [x] OverlapAdd (stft.h) - exists
- [x] SpectralBuffer (spectral_buffer.h) - exists
- [x] Waveshaper (waveshaper.h) - exists
- [x] spectral_utils.h - exists

**No blocking dependencies.**
