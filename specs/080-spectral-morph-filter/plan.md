# Implementation Plan: Spectral Morph Filter

**Branch**: `080-spectral-morph-filter` | **Date**: 2026-01-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/080-spectral-morph-filter/spec.md`

## Summary

A Layer 2 processor that morphs between two audio signals by interpolating their magnitude spectra while preserving phase from a selectable source. Features include dual-input morphing, snapshot capture mode (N-frame averaging), spectral shift via bin rotation (nearest-neighbor rounding), spectral tilt with 1 kHz pivot, and complex vector interpolation for phase blending.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: STFT, OverlapAdd, SpectralBuffer, FFT (Layer 1); Window::generate, kPi, kTwoPi (Layer 0); OnePoleSmoother (Layer 1)
**Storage**: N/A (in-memory spectral processing)
**Testing**: Catch2 via dsp_tests target (Constitution Principle XIII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component (dsp/include/krate/dsp/processors/)
**Performance Goals**: < 50ms for two 1-second mono buffers (equivalent to stereo) at 44.1kHz with FFT size 2048 (< 2.5% CPU) per SC-001
**Constraints**: Real-time safe processing (no allocations in audio thread), COLA reconstruction error < -60 dB
**Scale/Scope**: Single processor class with ~500-700 LOC

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in process methods (pre-allocate in prepare())
- [x] No locks/mutexes in audio path
- [x] No exceptions in audio path
- [x] All buffers pre-allocated

**Required Check - Principle III (Modern C++ Standards):**
- [x] C++20 target
- [x] RAII for resource management
- [x] Smart pointers not needed (composition, not ownership)
- [x] constexpr and const used appropriately

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor depends only on Layer 0-1
- [x] No circular dependencies
- [x] Independently testable

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [ ] All FR-xxx and SC-xxx will be verified at completion
- [ ] No test thresholds will be relaxed
- [ ] No features quietly removed

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: SpectralMorphFilter, PhaseSource (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SpectralMorphFilter | `grep -r "class SpectralMorph" dsp/ plugins/` | No | Create New |
| PhaseSource | `grep -r "enum.*PhaseSource\|PhaseSource" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None required (reuse existing)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| semitones to ratio | `grep -r "semitone.*ratio\|pow.*12" dsp/` | Check | Possibly in pitch_shifter.h | Reuse if exists, else inline |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| STFT | dsp/include/krate/dsp/primitives/stft.h | 1 | STFT analysis for both inputs |
| OverlapAdd | dsp/include/krate/dsp/primitives/stft.h | 1 | COLA-compliant synthesis |
| SpectralBuffer | dsp/include/krate/dsp/primitives/spectral_buffer.h | 1 | Magnitude/phase storage for all spectra |
| FFT | dsp/include/krate/dsp/primitives/fft.h | 1 | Core FFT/IFFT operations |
| Complex | dsp/include/krate/dsp/primitives/fft.h | 1 | Complex number operations |
| Window::generate | dsp/include/krate/dsp/core/window_functions.h | 0 | Hann window for COLA |
| WindowType | dsp/include/krate/dsp/core/window_functions.h | 0 | Window type enum |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (morph, tilt) |
| kPi, kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Phase calculations |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (target location)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing SpectralMorphFilter or PhaseSource types found. All new types are unique within the Krate::DSP namespace. Heavy reuse of existing spectral infrastructure (STFT, SpectralBuffer) reduces new code surface.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| STFT | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | Yes |
| STFT | pushSamples | `void pushSamples(const float* input, size_t numSamples) noexcept` | Yes |
| STFT | canAnalyze | `[[nodiscard]] bool canAnalyze() const noexcept` | Yes |
| STFT | analyze | `void analyze(SpectralBuffer& output) noexcept` | Yes |
| STFT | fftSize | `[[nodiscard]] size_t fftSize() const noexcept` | Yes |
| STFT | latency | `[[nodiscard]] size_t latency() const noexcept` | Yes |
| STFT | reset | `void reset() noexcept` | Yes |
| OverlapAdd | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | Yes |
| OverlapAdd | synthesize | `void synthesize(const SpectralBuffer& input) noexcept` | Yes |
| OverlapAdd | samplesAvailable | `[[nodiscard]] size_t samplesAvailable() const noexcept` | Yes |
| OverlapAdd | pullSamples | `void pullSamples(float* output, size_t numSamples) noexcept` | Yes |
| OverlapAdd | reset | `void reset() noexcept` | Yes |
| SpectralBuffer | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| SpectralBuffer | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| SpectralBuffer | getMagnitude | `[[nodiscard]] float getMagnitude(size_t bin) const noexcept` | Yes |
| SpectralBuffer | getPhase | `[[nodiscard]] float getPhase(size_t bin) const noexcept` | Yes |
| SpectralBuffer | setMagnitude | `void setMagnitude(size_t bin, float magnitude) noexcept` | Yes |
| SpectralBuffer | setPhase | `void setPhase(size_t bin, float phase) noexcept` | Yes |
| SpectralBuffer | setCartesian | `void setCartesian(size_t bin, float real, float imag) noexcept` | Yes |
| SpectralBuffer | getReal | `[[nodiscard]] float getReal(size_t bin) const noexcept` | Yes |
| SpectralBuffer | getImag | `[[nodiscard]] float getImag(size_t bin) const noexcept` | Yes |
| SpectralBuffer | reset | `void reset() noexcept` | Yes |
| Complex | real | `float real = 0.0f` | Yes |
| Complex | imag | `float imag = 0.0f` | Yes |
| Complex | magnitude | `[[nodiscard]] float magnitude() const noexcept` | Yes |
| Complex | phase | `[[nodiscard]] float phase() const noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/stft.h` - STFT and OverlapAdd classes
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer class
- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT class and Complex struct
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/window_functions.h` - Window namespace and WindowType
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi constants

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| STFT | Uses circular buffer internally | No need to manage ring buffer |
| SpectralBuffer | prepare() takes fftSize, allocates fftSize/2+1 bins | `buffer.prepare(fftSize)` not `buffer.prepare(numBins)` |
| SpectralBuffer | setCartesian sets real+imag directly | Use for complex interpolation |
| OverlapAdd | Returns samples at frame rate, not sample rate | Pull after each synthesize() call |
| OnePoleSmoother | Uses ITERUM_NOINLINE on setTarget | NaN handling works under /fp:fast |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| semitonesToRatio | Audio conversion, reusable | core/pitch_utils.h or db_utils.h | PitchShifter, SpectralMorphFilter, future pitch effects |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateBinShift | Specific to spectral shift, uses class state |
| applySpectralTilt | Uses class state (pivot frequency, sample rate) |
| blendPhase | Implementation detail of phase interpolation |

**Decision**: Extract `semitonesToRatio` to Layer 0 if not already present; keep spectral-specific helpers as private members.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from ROADMAP.md or known plans):
- SpectralGate (Phase 12.2): Per-bin noise gate using magnitude thresholds
- SpectralTilt (Phase 12.3): Tilt filter for spectral balance

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Spectral tilt implementation | HIGH | SpectralTilt (Phase 12.3) | Keep local; extract after 2nd use |
| Bin rotation logic | MEDIUM | Other spectral pitch effects | Keep local |
| Snapshot averaging | MEDIUM | SpectralGate, SpectralFreeze variants | Keep local |

### Detailed Analysis (for HIGH potential items)

**Spectral Tilt** provides:
- Per-bin gain adjustment based on frequency ratio to pivot
- 1 kHz pivot point (industry standard)
- dB/octave slope calculation

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| SpectralTilt (Phase 12.3) | YES | Exact same algorithm, may become standalone |
| SpectralGate | NO | Uses threshold, not slope |

**Recommendation**: Keep spectral tilt as private member in SpectralMorphFilter. When SpectralTilt (Phase 12.3) is implemented, extract shared tilt calculation to a common location if algorithms match.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First spectral morphing feature at this layer |
| Keep tilt local | Only one consumer so far; extract on 2nd use |
| Reuse STFT/OverlapAdd heavily | Proven infrastructure from SpectralDelay |

### Review Trigger

After implementing **SpectralTilt (Phase 12.3)**, review this section:
- [ ] Does SpectralTilt need same tilt algorithm? -> Extract to common location
- [ ] Does SpectralGate need snapshot capability? -> Consider shared helper
- [ ] Any duplicated spectral frame processing patterns? -> Document shared pattern

## Project Structure

### Documentation (this feature)

```text
specs/080-spectral-morph-filter/
├── plan.md              # This file
├── research.md          # Phase 0 output - all clarifications resolved
├── data-model.md        # Phase 1 output - class structure
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - API contracts
└── tasks.md             # Phase 2 output (NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── spectral_morph_filter.h   # NEW: Main processor (Layer 2)
└── tests/
    └── processors/
        └── spectral_morph_filter_test.cpp  # NEW: Unit tests

# No plugin integration in this spec (DSP-only component)
```

**Structure Decision**: Single header-only implementation in Layer 2 processors, following existing patterns (e.g., crossover_filter.h, formant_filter.h). Test file in corresponding test directory.

## Complexity Tracking

> No Constitution violations identified. All design decisions comply with principles.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none) | - | - |

---

## Phase 0: Research (Complete)

All clarifications from spec have been resolved:

| Question | Resolution | Rationale |
|----------|------------|-----------|
| Bins beyond Nyquist | Zero bins | Prevents aliasing artifacts |
| Phase blend mode | Complex vector interpolation | Avoids phase wrapping discontinuities |
| Snapshot capture | Average NEXT N frames after captureSnapshot() call (default 4) | Smoother, more musical spectral fingerprint |
| Spectral tilt pivot | 1 kHz | Industry standard for tilt filters |
| Spectral shift bins | Nearest-neighbor rounding | Efficient, preserves spectral clarity |

### Additional Research Findings

**STFT Processing Pattern** (from SpectralDelay reference):
1. Push samples into STFT analyzers
2. Process spectral frames when canAnalyze() returns true
3. Synthesize with OverlapAdd
4. Pull output samples

**Complex Delay for Phase Preservation** (from SpectralDelay):
- Store real+imaginary separately to avoid phase wrapping during interpolation
- Linear interpolation of complex values is safe

**Parameter Smoothing**: 50ms time constant recommended for spectral processing (from SpectralDelay).

---

## Phase 1: Design

### Class Architecture

```cpp
namespace Krate::DSP {

/// Phase source selection for spectral morphing
enum class PhaseSource : uint8_t {
    A,      ///< Use phase from source A exclusively
    B,      ///< Use phase from source B exclusively
    Blend   ///< Interpolate via complex vector lerp
};

/// Spectral Morph Filter - Layer 2 Processor
/// Morphs between two audio signals by interpolating magnitude spectra
class SpectralMorphFilter {
public:
    // Constants
    static constexpr size_t kMinFFTSize = 256;
    static constexpr size_t kMaxFFTSize = 4096;
    static constexpr size_t kDefaultFFTSize = 2048;
    static constexpr float kMinMorphAmount = 0.0f;
    static constexpr float kMaxMorphAmount = 1.0f;
    static constexpr float kMinSpectralShift = -24.0f;  // semitones
    static constexpr float kMaxSpectralShift = +24.0f;
    static constexpr float kMinSpectralTilt = -12.0f;   // dB/octave
    static constexpr float kMaxSpectralTilt = +12.0f;
    static constexpr float kTiltPivotHz = 1000.0f;
    static constexpr size_t kDefaultSnapshotFrames = 4;

    // Lifecycle
    void prepare(double sampleRate, size_t fftSize = kDefaultFFTSize) noexcept;
    void reset() noexcept;

    // Dual-input processing (FR-002, FR-016)
    void processBlock(const float* inputA, const float* inputB,
                      float* output, size_t numSamples) noexcept;

    // Single-input with snapshot (FR-003, FR-017)
    float process(float input) noexcept;

    // Snapshot capture (FR-006)
    void captureSnapshot() noexcept;
    void setSnapshotFrameCount(size_t frames) noexcept;

    // Parameters
    void setMorphAmount(float amount) noexcept;          // FR-004
    void setPhaseSource(PhaseSource source) noexcept;    // FR-005
    void setSpectralShift(float semitones) noexcept;     // FR-007
    void setSpectralTilt(float dBPerOctave) noexcept;    // FR-008

    // Query
    [[nodiscard]] size_t getLatencySamples() const noexcept;  // FR-020
    [[nodiscard]] size_t getFftSize() const noexcept;
    [[nodiscard]] float getMorphAmount() const noexcept;
    [[nodiscard]] PhaseSource getPhaseSource() const noexcept;
    [[nodiscard]] float getSpectralShift() const noexcept;
    [[nodiscard]] float getSpectralTilt() const noexcept;
    [[nodiscard]] bool hasSnapshot() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // Internal processing
    void processSpectralFrame() noexcept;
    void accumulateSnapshotFrame(const SpectralBuffer& spectrum) noexcept;
    void finalizeSnapshot() noexcept;
    void applyMagnitudeInterpolation(const SpectralBuffer& specA,
                                     const SpectralBuffer& specB,
                                     SpectralBuffer& output,
                                     float morphAmount) noexcept;
    void applyPhaseSelection(const SpectralBuffer& specA,
                             const SpectralBuffer& specB,
                             SpectralBuffer& output,
                             float morphAmount,
                             PhaseSource source) noexcept;
    void applySpectralShift(SpectralBuffer& spectrum, float semitones) noexcept;
    void applySpectralTilt(SpectralBuffer& spectrum, float dBPerOctave) noexcept;

    // State
    double sampleRate_ = 44100.0;
    size_t fftSize_ = kDefaultFFTSize;
    size_t hopSize_ = kDefaultFFTSize / 2;
    bool prepared_ = false;

    // STFT analysis (two inputs)
    STFT stftA_;
    STFT stftB_;

    // Overlap-add synthesis
    OverlapAdd overlapAdd_;

    // Spectral buffers
    SpectralBuffer spectrumA_;
    SpectralBuffer spectrumB_;
    SpectralBuffer outputSpectrum_;

    // Snapshot state
    SpectralBuffer snapshotSpectrum_;
    SpectralBuffer snapshotAccumulator_;  // For averaging
    size_t snapshotFrameCount_ = kDefaultSnapshotFrames;
    size_t snapshotFramesAccumulated_ = 0;
    bool hasSnapshot_ = false;
    bool captureRequested_ = false;

    // Parameters
    float morphAmount_ = 0.0f;
    float spectralShift_ = 0.0f;
    float spectralTilt_ = 0.0f;
    PhaseSource phaseSource_ = PhaseSource::A;

    // Parameter smoothing
    OnePoleSmoother morphSmoother_;
    OnePoleSmoother tiltSmoother_;

    // Temp buffers for shift operation
    std::vector<float> shiftedMagnitudes_;
    std::vector<float> shiftedPhases_;
};

} // namespace Krate::DSP
```

### Internal Data Structures

**Dual-Input Mode:**
- Two STFT analyzers (stftA_, stftB_) process inputs in parallel
- SpectralBuffer for each input (spectrumA_, spectrumB_)
- Output spectrum combines interpolated magnitudes with selected phase

**Snapshot Mode:**
- snapshotAccumulator_ accumulates magnitudes across N frames
- snapshotFramesAccumulated_ tracks progress
- snapshotSpectrum_ stores the final averaged snapshot
- captureRequested_ flag triggers accumulation start

### Algorithm Details

**Magnitude Interpolation (FR-004):**
```
output_mag[bin] = specA_mag[bin] * (1 - morph) + specB_mag[bin] * morph
```

**Phase Selection (FR-005):**
- PhaseSource::A: `output_phase[bin] = specA_phase[bin]`
- PhaseSource::B: `output_phase[bin] = specB_phase[bin]`
- PhaseSource::Blend: Complex vector interpolation:
  ```
  blended_real = realA * (1 - morph) + realB * morph
  blended_imag = imagA * (1 - morph) + imagB * morph
  output_phase = atan2(blended_imag, blended_real)
  ```

**Spectral Shift (FR-007):**
- Convert semitones to frequency ratio: `ratio = pow(2.0, semitones / 12.0)`
- For each output bin k, find source bin: `src_bin = round(k / ratio)`
- If src_bin out of range (> numBins-1), zero the output bin
- Nearest-neighbor: `src_bin = static_cast<int>(k / ratio + 0.5f)`

**Spectral Tilt (FR-008):**
- Pivot at 1 kHz
- For each bin, calculate frequency: `freq = bin * (sampleRate / fftSize)`
- Calculate gain: `gain_dB = tilt * log2(freq / 1000.0)`
- Apply: `magnitude *= pow(10.0, gain_dB / 20.0)`

**Snapshot Averaging (FR-006):**
- captureSnapshot() sets captureRequested_ flag and resets accumulator
- For NEXT N frames: `accumulator[bin] += spectrum_mag[bin]`
- After N frames accumulated, finalize: `snapshot_mag[bin] = accumulator[bin] / N`
- Store phase from last frame of the accumulation period

### COLA Compliance (FR-012)

- Use Hann window at 50% overlap (hopSize = fftSize / 2)
- Analysis windowing in STFT
- OverlapAdd handles COLA normalization automatically
- Verified by existing Window::verifyCOLA() infrastructure

### Real-Time Safety (Principle II)

All allocations in prepare():
- STFT buffers
- SpectralBuffer vectors
- Temp shift buffers
- OnePoleSmoother state

Process methods:
- No allocations
- noexcept
- No locks
- No exceptions

### Parameter Smoothing (FR-018)

- morphSmoother_: 50ms time constant for morph amount
- tiltSmoother_: 50ms time constant for spectral tilt
- Spectral shift: No smoothing (discrete bin operation, would cause artifacts)

---

## Test Strategy

### Unit Tests (spectral_morph_filter_test.cpp)

**Category 1: Basic Functionality**
- T001: prepare() with valid FFT sizes (256, 512, 1024, 2048, 4096)
- T002: reset() clears all state
- T003: Latency equals FFT size

**Category 2: Morph Amount (SC-002, SC-003, SC-004)**
- T010: morph=0.0 outputs source A spectrum exactly
- T011: morph=1.0 outputs source B spectrum exactly
- T012: morph=0.5 produces arithmetic mean of magnitudes

**Category 3: Phase Source**
- T020: PhaseSource::A preserves A's phase
- T021: PhaseSource::B preserves B's phase
- T022: PhaseSource::Blend uses complex interpolation

**Category 4: Snapshot Mode**
- T030: captureSnapshot() captures current spectrum
- T031: Snapshot averages N frames
- T032: Single-input process() morphs with snapshot
- T033: No snapshot = passthrough

**Category 5: Spectral Shift (SC-005)**
- T040: +12 semitones doubles harmonic frequencies
- T041: -12 semitones halves harmonic frequencies
- T042: shift=0 no change
- T043: Bins beyond Nyquist are zeroed

**Category 6: Spectral Tilt (SC-006)**
- T050: +6 dB/octave boosts highs, cuts lows
- T051: -6 dB/octave cuts highs, boosts lows
- T052: tilt=0 no change
- T053: 1 kHz pivot has 0 dB gain

**Category 7: COLA Reconstruction (SC-007)**
- T060: Passthrough (morph=0, phaseA) < -60 dB error
- T061: Round-trip reconstruction accuracy

**Category 8: Edge Cases**
- T070: nullptr input handling
- T071: NaN/Inf input handling (FR-015)
- T072: process() before prepare()
- T073: Rapid parameter changes (FR-018)

**Category 9: Performance (SC-001)**
- T080: 1 second stereo @ 44.1kHz < 50ms

**Category 10: Consistency (SC-009)**
- T090: Single-sample and block processing produce consistent results

### Approval Tests

- Golden reference for known sine wave morph
- Spectral shift verification with harmonic series

---

## Success Criteria Mapping

| Criterion | Test | Method |
|-----------|------|--------|
| SC-001 | T080 | Benchmark timing |
| SC-002 | T010 | RMS spectrum comparison < 0.1 dB |
| SC-003 | T011 | RMS spectrum comparison < 0.1 dB |
| SC-004 | T012 | Bin-by-bin verification within 1% |
| SC-005 | T040 | FFT peak detection, 5% tolerance |
| SC-006 | T050-T053 | Frequency response measurement |
| SC-007 | T060 | RMS error calculation |
| SC-008 | T073 | No clicks in output (zero crossings) |
| SC-009 | T090 | Sample comparison |
| SC-010 | T003 | getLatencySamples() == fftSize |

---

## Implementation Order

1. **Phase 1**: Core class structure, prepare(), reset()
2. **Phase 2**: Dual-input processBlock() with magnitude interpolation only
3. **Phase 3**: Phase source selection (A, B, Blend)
4. **Phase 4**: Spectral shift (bin rotation)
5. **Phase 5**: Spectral tilt (1 kHz pivot)
6. **Phase 6**: Snapshot capture mode
7. **Phase 7**: Single-input process()
8. **Phase 8**: Parameter smoothing
9. **Phase 9**: Edge case handling, NaN/Inf protection
10. **Phase 10**: Performance optimization and validation

---

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/spectral_morph_filter.h` | Main processor implementation |
| `dsp/tests/processors/spectral_morph_filter_test.cpp` | Unit tests |
| `specs/080-spectral-morph-filter/research.md` | Research notes (this planning output) |
| `specs/080-spectral-morph-filter/data-model.md` | Class structure details |
| `specs/080-spectral-morph-filter/quickstart.md` | Usage examples |
| `specs/080-spectral-morph-filter/contracts/api.h` | Public API contract |
