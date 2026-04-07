# Implementation Plan: Innexus Milestone 1 -- Core Playable Instrument

**Branch**: `115-innexus-m1-core-instrument` | **Date**: 2026-03-03 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/115-innexus-m1-core-instrument/spec.md`

**Note**: This plan covers 58 functional requirements across 9 implementation phases. Innexus is a new VST3 instrument plugin that analyzes harmonic structure of audio samples and resynthesizes them as a MIDI-playable instrument.

## Summary

Innexus M1 delivers the core analysis-driven synthesis loop: load a WAV/AIFF sample, run it through a pre-processing pipeline, YIN pitch detection, dual-window STFT, partial tracking, and harmonic model building, then play the extracted timbre from MIDI using a 48-oscillator Gordon-Smith MCF bank. The plugin is monophonic for this milestone with velocity and pitch bend support. Analysis runs on a background thread with immutable results published to the audio thread via atomic pointer swap.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang/Xcode 13+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (shared DSP library), dr_wav (MIT, single-header WAV/AIFF decoder)
**Storage**: Binary state via VST3 IBStream (sample file path + parameter values)
**Testing**: Catch2 (DSP unit tests, plugin integration tests, approval tests)
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang/Xcode), Linux (GCC) -- cross-platform
**Project Type**: Monorepo plugin (plugins/innexus/) + shared DSP library (dsp/)
**Performance Goals**: Oscillator bank <0.5% CPU for 48 partials; full plugin <5% CPU at 44.1kHz stereo; analysis <10s for 10s mono file
**Constraints**: Zero memory allocations on audio thread; zero aliased partials above Nyquist; MIDI note-on response within one buffer duration
**Scale/Scope**: 58 functional requirements, 10 success criteria, 9 implementation phases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller are separate classes with separate FUIDs (already scaffolded)
- [x] Processor will function without Controller (outputs silence when no sample loaded)
- [x] Communication via IMessage for sample file path delivery (controller to processor)
- [x] State flows: Host -> Processor -> Controller via setComponentState()

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] All audio-thread DSP components use pre-allocated buffers (prepare() pattern)
- [x] No allocations in process(): oscillator bank is fixed-size SoA arrays
- [x] Background thread analysis publishes immutable SampleAnalysis via std::atomic pointer swap
- [x] No mutex/lock on audio thread -- acquire/release semantics only

**Required Check - Principle III (Modern C++):**
- [x] C++20 with std::array, constexpr, noexcept, RAII throughout
- [x] No raw new/delete -- smart pointers for SampleAnalysis lifecycle

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD analysis completed below (oscillator bank is SIMD-viable; scalar-first workflow applies)
- [x] SoA layout for oscillator bank (cache efficiency)

**Required Check - Principle VI (Cross-Platform):**
- [x] No platform-specific code (no UI in M1; dr_wav is cross-platform; standard C++ threading)
- [x] No Win32/Cocoa APIs

**Required Check - Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) -- no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] YinPitchDetector -> Layer 2 (uses FFT from Layer 1)
- [x] PartialTracker -> Layer 2 (uses SpectralBuffer/spectral_utils from Layer 1)
- [x] HarmonicOscillatorBank -> Layer 2 (uses Layer 0 math/pitch utilities)
- [x] HarmonicModelBuilder -> Layer 3 (uses Layer 2 PartialTracker output)
- [x] No upward dependencies; all within layer rules

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XIII (Test-First Development):**
- [x] Each DSP component gets tests before implementation
- [x] Bug-first testing: reproduce in test, verify fails, fix, verify passes

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| YinPitchDetector | `grep -r "class YinPitchDetector" dsp/ plugins/` | No | Create New at dsp/include/krate/dsp/processors/ |
| PartialTracker | `grep -r "class PartialTracker" dsp/ plugins/` | No | Create New at dsp/include/krate/dsp/processors/ |
| HarmonicOscillatorBank | `grep -r "class HarmonicOscillatorBank" dsp/ plugins/` | No | Create New at dsp/include/krate/dsp/processors/ |
| HarmonicModelBuilder | `grep -r "class HarmonicModelBuilder" dsp/ plugins/` | No | Create New at dsp/include/krate/dsp/systems/ |
| HarmonicFrame | `grep -r "struct HarmonicFrame" dsp/ plugins/` | No | Create New (shared header) |
| Partial (in Innexus context) | `grep -r "struct Partial" dsp/ plugins/` | No (no matching struct) | Create New (within harmonic_types.h) |
| F0Estimate | `grep -r "struct F0Estimate" dsp/ plugins/` | No | Create New (within yin_pitch_detector.h) |
| SampleAnalysis | `grep -r "struct SampleAnalysis" dsp/ plugins/` | No | Create New (plugin-local) |
| SampleAnalyzer | `grep -r "class SampleAnalyzer" dsp/ plugins/` | No | Create New (plugin-local) |
| PreProcessingPipeline | `grep -r "class PreProcessingPipeline" dsp/ plugins/` | No | Create New (plugin-local) |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| parabolicInterpolation | `grep -r "parabolicInterpolation" dsp/ plugins/` | No | -- | Create in spectral_utils.h or yin header |
| generateBlackmanHarris | `grep -r "BlackmanHarris\|blackman_harris" dsp/ plugins/` | No | -- | Add to window_functions.h |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DCBlocker2 | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | Pre-processing DC removal (FR-005) |
| Biquad (FilterType::Highpass) | dsp/include/krate/dsp/primitives/biquad.h | 1 | 30 Hz HPF in pre-processing (FR-006) |
| FFT | dsp/include/krate/dsp/primitives/fft.h | 1 | YIN FFT-accelerated difference function (FR-011) |
| FFTAutocorrelation | dsp/include/krate/dsp/primitives/fft_autocorrelation.h | 1 | Reference for YIN difference function FFT pattern |
| STFT | dsp/include/krate/dsp/primitives/stft.h | 1 | Dual-window spectral analysis (FR-018) |
| SpectralBuffer | dsp/include/krate/dsp/primitives/spectral_buffer.h | 1 | STFT output storage (FR-021) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Amplitude smoothing (FR-034, FR-041) |
| Window::generate (Hann, Blackman) | dsp/include/krate/dsp/core/window_functions.h | 0 | STFT windowing; needs Blackman-Harris addition (FR-019) |
| binToFrequency / frequencyToBin | dsp/include/krate/dsp/primitives/spectral_utils.h | 1 | Peak detection frequency mapping |
| calculateSpectralCentroid | dsp/include/krate/dsp/primitives/spectral_utils.h | 1 | Spectral centroid computation (FR-032) |
| wrapPhase | dsp/include/krate/dsp/primitives/spectral_utils.h | 1 | Phase utilities in partial tracking |
| midiNoteToFrequency | dsp/include/krate/dsp/core/midi_utils.h | 0 | MIDI note to Hz conversion (FR-048) |
| velocityToGain | dsp/include/krate/dsp/core/midi_utils.h | 0 | Velocity scaling (FR-050) |
| semitonesToRatio | dsp/include/krate/dsp/core/pitch_utils.h | 0 | Pitch bend calculation (FR-051) |
| kPi, kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | MCF epsilon calculation |
| EnvelopeFollower | dsp/include/krate/dsp/processors/envelope_follower.h | 2 | Transient detection in pre-processing (FR-008) |
| PitchDetector (autocorrelation) | dsp/include/krate/dsp/primitives/pitch_detector.h | 1 | Reference/fallback; YIN replaces as primary |
| ParticleOscillator | dsp/include/krate/dsp/processors/particle_oscillator.h | 2 | Reference for MCF + SoA pattern |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (YinPitchDetector, PartialTracker, HarmonicOscillatorBank, HarmonicModelBuilder, HarmonicFrame, Partial, F0Estimate, SampleAnalysis, SampleAnalyzer) do not exist anywhere in the codebase. No naming conflicts detected. The Partial struct will be defined in a dedicated `harmonic_types.h` header to avoid any potential name collisions.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DCBlocker2 | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker2 | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker2 | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| Biquad | (needs FilterType::Highpass) | FilterType enum has `Highpass` | Yes |
| FFT | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| FFT | forward | `void forward(const float* input, Complex* output) noexcept` | Yes |
| FFT | inverse | `void inverse(const Complex* input, float* output) noexcept` | Yes |
| FFT | size | `[[nodiscard]] size_t size() const noexcept` | Yes |
| FFT | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| STFT | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window, float kaiserBeta) noexcept` | Yes |
| STFT | pushSamples | `void pushSamples(const float* input, size_t numSamples) noexcept` | Yes |
| STFT | canAnalyze | `[[nodiscard]] bool canAnalyze() const noexcept` | Yes |
| STFT | analyze | `void analyze(SpectralBuffer& output) noexcept` | Yes |
| SpectralBuffer | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| SpectralBuffer | getMagnitude | `[[nodiscard]] float getMagnitude(size_t bin) const noexcept` | Yes |
| SpectralBuffer | getPhase | `[[nodiscard]] float getPhase(size_t bin) const noexcept` | Yes |
| SpectralBuffer | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| midiNoteToFrequency | (free function) | `[[nodiscard]] constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = kA4FrequencyHz) noexcept` | Yes |
| binToFrequency | (free function) | `[[nodiscard]] inline constexpr float binToFrequency(size_t bin, size_t fftSize, float sampleRate) noexcept` | Yes |
| frequencyToBin | (free function) | `[[nodiscard]] inline constexpr float frequencyToBin(float frequency, size_t fftSize, float sampleRate) noexcept` | Yes |
| calculateSpectralCentroid | (free function) | `[[nodiscard]] inline float calculateSpectralCentroid(const float* magnitudes, size_t numBins, float sampleRate, size_t fftSize) noexcept` | Yes |
| semitonesToRatio | (free function) | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |
| wrapPhase | (free function) | Located in `spectral_utils.h` per this plan; spec.md lists it in `phase_utils.h`. **VERIFY at implementation**: confirm actual file before including. | Needs verification |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker2 class
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class, FilterType enum
- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT class, Complex struct
- [x] `dsp/include/krate/dsp/primitives/stft.h` - STFT class
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother, LinearRamp
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - bin/freq conversions, spectral centroid
- [x] `dsp/include/krate/dsp/primitives/fft_autocorrelation.h` - FFTAutocorrelation class
- [x] `dsp/include/krate/dsp/primitives/pitch_detector.h` - PitchDetector class (reference)
- [x] `dsp/include/krate/dsp/processors/particle_oscillator.h` - ParticleOscillator (MCF pattern)
- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower
- [x] `dsp/include/krate/dsp/core/window_functions.h` - Window functions, WindowType enum
- [x] `dsp/include/krate/dsp/core/midi_utils.h` - midiNoteToFrequency, velocityToGain
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio, frequencyToMidiNote
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| WindowType | No BlackmanHarris variant exists yet | Must add to WindowType enum and Window namespace |
| STFT | Uses WindowType enum, not string | `WindowType::Blackman` exists; need to add `WindowType::BlackmanHarris` |
| SpectralBuffer | data() invalidates polar cache | After calling data() (mutable), polar values must be recomputed |
| FFT | kMaxFFTSize is 8192 | Sufficient for 4096-sample analysis windows |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| midiNoteToFrequency | Takes int, not float | Cast MIDI note to int first |
| semitonesToRatio | In pitch_utils.h, NOT midi_utils.h | `#include <krate/dsp/core/pitch_utils.h>` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| parabolicInterpolation | Used by YIN (CMNDF minima), peak detection (spectral peaks), existing PitchDetector | spectral_utils.h or interpolation.h | YinPitchDetector, PartialTracker, PitchDetector |
| generateBlackmanHarris | Standard window function needed for sidelobe rejection | window_functions.h (extend WindowType enum) | STFT analysis for Innexus, potentially Iterum spectral modes |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| CMNDF computation | YIN-specific algorithm, not general-purpose |
| Harmonic sieve matching | Partial-tracking-specific, tied to F0 context |
| MCF epsilon calculation | Simple formula (2*sin(pi*f/sr)), inline in oscillator bank |

**Decision**: Extract `parabolicInterpolation` to spectral_utils.h (3+ consumers: YIN, PartialTracker, existing PitchDetector). Add `BlackmanHarris` window to window_functions.h. All other utilities stay local to their components.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES (MCF oscillators) | Each oscillator has s[n+1] = s[n] + eps*c[n], c[n+1] = c[n] - eps*s[n+1]. Serial per-oscillator, but independent across oscillators. |
| **Data parallelism width** | 48 oscillators | 48 independent MCF state pairs -- excellent SIMD width (12x SSE lanes or 6x AVX lanes) |
| **Branch density in inner loop** | LOW | Hot loop is: advance MCF, multiply amplitude, accumulate. No conditionals per sample in main path. |
| **Dominant operations** | Arithmetic (mul, add) | 2 muls + 2 adds per oscillator per sample for MCF; 1 mul for amplitude |
| **Current CPU budget vs expected usage** | <0.5% budget; ParticleOscillator at 0.38% for 64 oscillators | Already within budget with scalar code |

### SIMD Viability Verdict

**Verdict**: BENEFICIAL -- DEFER to Phase 2

**Reasoning**: The 48-oscillator bank has excellent SIMD characteristics: no inter-oscillator dependencies, branchless inner loop, SoA layout already planned, and 48 lanes of parallelism. However, the scalar MCF pattern (proven at 0.38% for 64 oscillators in ParticleOscillator) already meets the <0.5% budget for 48 oscillators. Per Constitution Principle IV, scalar-first is mandatory. SIMD optimization should be deferred to a follow-up spec after M1 is complete and profiled.

### Implementation Workflow

| Phase | What | When | Deliverables |
|-------|------|------|-------------|
| **1. Scalar** | Implement full algorithm with scalar code | This spec (M1) | Working oscillator bank + complete test suite + CPU baseline |
| **2. SIMD** | Add SIMD-optimized code path (SSE/NEON) | Follow-up spec after M1 | SIMD path + all tests pass + CPU improvement measured |

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| SoA layout (already planned) | ~10-15% cache improvement | LOW | YES (FR-036 requires it) |
| Anti-alias gain skip when all partials below 80% Nyquist | ~5-10% for low notes | LOW | YES |
| Block-rate model updates (not per-sample) | Reduces branch checks | LOW | YES |

## Higher-Layer Reusability Analysis

**This feature's layer**: Components span Layers 0-3 (DSP library) + plugin-local code

**Related features at same layer** (from roadmap):
- M2: Residual Analysis/Synthesis (needs PartialTracker output, HarmonicModelBuilder)
- M3: Live Sidechain (reuses entire analysis pipeline with different input)
- M4: Freeze/Morph (extends HarmonicFrame, HarmonicOscillatorBank)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| YinPitchDetector | HIGH | M3 live sidechain, any plugin needing pitch tracking | Place in KrateDSP Layer 2 (dsp/include/krate/dsp/processors/) |
| PartialTracker | HIGH | M2 residual analysis, M3 live sidechain, Iterum spectral effects | Place in KrateDSP Layer 2 |
| HarmonicOscillatorBank | HIGH | M4 freeze/morph, M5 harmonic memory, M6 creative extensions | Place in KrateDSP Layer 2 |
| HarmonicModelBuilder | HIGH | M2, M3, M4 | Place in KrateDSP Layer 3 |
| HarmonicFrame/Partial types | HIGH | All future milestones | Shared header in KrateDSP |
| SampleAnalyzer | MEDIUM | M3 could partially reuse for file loading | Keep plugin-local for now |
| PreProcessingPipeline | LOW | Configuration-specific wiring of existing components | Keep plugin-local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| All new DSP components in KrateDSP, not plugin-local | M2-M6 all reuse these components; extracting later risks ODR issues |
| SampleAnalyzer stays plugin-local | File I/O and threading are plugin-specific concerns |
| PreProcessingPipeline stays plugin-local | Just wiring of existing DCBlocker2 + Biquad + gate; not a reusable abstraction |

## Project Structure

### Documentation (this feature)

```text
specs/115-innexus-m1-core-instrument/
 plan.md              # This file
 research.md          # Phase 0 output
 data-model.md        # Phase 1 output
 quickstart.md        # Phase 1 output
 contracts/           # Phase 1 output
 tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
# New DSP library components (KrateDSP, shared across plugins)
dsp/include/krate/dsp/
  core/
    window_functions.h          # MODIFIED: Add BlackmanHarris window type
    spectral_utils.h            # MODIFIED: Add parabolicInterpolation()
  processors/
    harmonic_types.h            # NEW: HarmonicFrame, Partial, F0Estimate structs
    yin_pitch_detector.h        # NEW: YIN pitch detector (Layer 2)
    partial_tracker.h           # NEW: Partial detection and tracking (Layer 2)
    harmonic_oscillator_bank.h  # NEW: 48-oscillator MCF bank (Layer 2)
  systems/
    harmonic_model_builder.h    # NEW: Analysis-to-model pipeline (Layer 3)

dsp/tests/unit/
  core/
    window_functions_tests.cpp  # MODIFIED: BlackmanHarris tests
  processors/
    yin_pitch_detector_tests.cpp        # NEW
    partial_tracker_tests.cpp           # NEW
    harmonic_oscillator_bank_tests.cpp  # NEW
  systems/
    harmonic_model_builder_tests.cpp    # NEW

# Plugin-local code (Innexus-specific)
plugins/innexus/
  src/
    plugin_ids.h                # MODIFIED: Add M1 parameter IDs
    processor/
      processor.h               # MODIFIED: Add DSP members, MIDI handling
      processor.cpp             # MODIFIED: Full process() implementation
    controller/
      controller.h              # MODIFIED: Register M1 parameters
      controller.cpp            # MODIFIED: Parameter registration
    dsp/
      pre_processing_pipeline.h # NEW: DC block + HPF + gate + transient suppression
      sample_analyzer.h         # NEW: Background thread sample analysis
      sample_analysis.h         # NEW: SampleAnalysis data structure
    parameters/
      innexus_params.h          # NEW: Parameter registration helpers
  tests/
    unit/
      processor/
        innexus_processor_tests.cpp  # NEW: MIDI handling, state persistence
      vst/
        innexus_vst_tests.cpp        # NEW: pluginval-style integration

# External dependency (to be added)
extern/
  dr_libs/
    dr_wav.h                    # NEW: Single-header WAV/AIFF decoder (MIT)
```

**Structure Decision**: Follows the established monorepo pattern. New DSP components go into KrateDSP at their appropriate layers for reusability. Plugin-local code stays in `plugins/innexus/src/`. The `dsp/` directory for plugin-local DSP wiring follows the Disrumpo pattern (`plugins/disrumpo/src/dsp/`).

---

## Phase-by-Phase Implementation Design

### Phase 1: Plugin Scaffold Completion (FR-001 to FR-004)

**Status**: Partially complete. The plugin skeleton already exists at `plugins/innexus/` with entry.cpp, processor, controller, CMakeLists.txt, and build integration.

**Remaining work**:

1. **Verify build succeeds** on current scaffold
2. **Add M1 parameter IDs** to `plugin_ids.h`:
   - `kReleaseTimeId` (200) -- release time in ms (FR-049)
   - `kInharmonicityAmountId` (201) -- 0-100% (FR-042)
3. **Register parameters** in controller.cpp
4. **Wire parameter changes** in processor.cpp processParameterChanges()
5. **Run pluginval** at strictness level 5

**Files to modify**:
- `plugins/innexus/src/plugin_ids.h` -- add parameter IDs
- `plugins/innexus/src/controller/controller.cpp` -- register parameters
- `plugins/innexus/src/processor/processor.h` -- add parameter atomics
- `plugins/innexus/src/processor/processor.cpp` -- handle parameter changes

**Files to create**:
- `plugins/innexus/src/parameters/innexus_params.h` -- parameter registration helper

**Dependencies**: None (scaffold exists)

**Testing**:
- Build succeeds on all platforms (CI)
- pluginval passes at strictness level 5
- Parameters appear in generic host UI

---

### Phase 2: Pre-Processing Pipeline (FR-005 to FR-009)

**Goal**: Clean the analysis signal for reliable harmonic extraction.

**Components** (all reusing existing KrateDSP primitives):

1. **DC Offset Removal** (FR-005): Reuse `DCBlocker2` with default 10 Hz cutoff. 2nd-order Bessel with 13ms settling time satisfies the requirement.

2. **High-Pass Filter** (FR-006): Reuse `Biquad` with `FilterType::Highpass` at 30 Hz, Butterworth Q (0.707).

3. **Noise Gate** (FR-007): Simple RMS threshold gate. Compute windowed RMS; when below threshold, zero the output. Configurable threshold parameter.

4. **Transient Suppression** (FR-008): Use `EnvelopeFollower` with fast attack (0.5ms) and slow release (50ms). When the fast envelope exceeds slow envelope by a configurable ratio, apply gain reduction. This prevents sharp transients from corrupting harmonic analysis.

5. **Separate Analysis Path** (FR-009): PreProcessingPipeline class wraps all four components. Takes raw audio input, outputs cleaned analysis signal. Not applied to any audio output -- analysis only.

**Files to create**:
- `plugins/innexus/src/dsp/pre_processing_pipeline.h` -- pipeline class

**Design**:
```cpp
class PreProcessingPipeline {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Process a block of samples in-place (modifies analysisBuffer)
    void processBlock(float* analysisBuffer, size_t numSamples) noexcept;

    void setNoiseGateThreshold(float thresholdDb) noexcept;
    void setTransientSuppression(float amount) noexcept;

private:
    DCBlocker2 dcBlocker_;
    Biquad highPass_;               // 30 Hz HPF
    EnvelopeFollower fastEnvelope_; // Fast attack for transient detection
    EnvelopeFollower slowEnvelope_; // Slow attack for baseline level
    float noiseGateThresholdLinear_ = 0.001f; // ~-60 dB default
    float transientSuppression_ = 1.0f;       // 0=off, 1=full
};
```

**Testing**:
- DC offset removal: apply 0.1 DC offset, verify output converges to 0 within 13ms
- HPF: verify 20 Hz sine is attenuated, 100 Hz passes through
- Noise gate: verify sub-threshold signal is zeroed
- Transient suppression: verify impulse is attenuated while steady tone passes

---

### Phase 3: Fundamental Frequency (F0) Tracking (FR-010 to FR-017)

**Goal**: Implement YIN pitch detection with FFT acceleration.

**New Component**: `YinPitchDetector` at `dsp/include/krate/dsp/processors/yin_pitch_detector.h`

**Algorithm (de Cheveigne & Kawahara 2002)**:

1. **Difference function** d(tau): For each lag tau, compute sum of squared differences between x[i] and x[i+tau].

2. **FFT acceleration** (FR-011): Use Wiener-Khinchin theorem. The difference function can be expressed as:
   ```
   d(tau) = r(0) + r_shifted(0) - 2*r(tau)
   ```
   where r(tau) is the autocorrelation. Compute autocorrelation via FFT in O(N log N) using the existing FFTAutocorrelation pattern (zero-pad to 2N, forward FFT, power spectrum, inverse FFT).

3. **CMNDF** (FR-010): Cumulative Mean Normalized Difference Function:
   ```
   d'(tau) = 1                              if tau == 0
   d'(tau) = d(tau) / ((1/tau) * sum(d(j), j=1..tau))  otherwise
   ```

4. **Absolute threshold**: Find the first tau where d'(tau) < threshold (default 0.3). This selects the fundamental period over subharmonics.

5. **Parabolic interpolation** (FR-012): Fit parabola through the three points around the minimum for sub-sample precision.

6. **Output** (FR-013): F0Estimate with frequency (Hz), confidence (0-1), and voiced flag.

**Data Structures** (defined in `harmonic_types.h`):
```cpp
struct F0Estimate {
    float frequency = 0.0f;   // Hz (0 if unvoiced)
    float confidence = 0.0f;  // 0..1
    bool voiced = false;      // confidence > threshold
};
```

**Stability Features**:
- **Confidence gating** (FR-015): Reject estimates below configurable threshold (default 0.3)
- **Frequency hysteresis** (FR-016): ~2% band. If new estimate is within 2% of previous, keep previous to prevent jitter.
- **Hold-previous** (FR-017): When confidence drops, hold last known-good F0.

**Configurable Parameters** (FR-014):
- Window size: 2048-4096 samples
- Min/Max F0: default 40-2000 Hz
- Confidence threshold: default 0.3

**Files to create**:
- `dsp/include/krate/dsp/processors/harmonic_types.h` -- F0Estimate, Partial, HarmonicFrame structs
- `dsp/include/krate/dsp/processors/yin_pitch_detector.h` -- YinPitchDetector class
- `dsp/tests/unit/processors/yin_pitch_detector_tests.cpp` -- unit tests

**Files to modify**:
- `dsp/include/krate/dsp/primitives/spectral_utils.h` -- add parabolicInterpolation()
- `dsp/CMakeLists.txt` -- add test file

**Testing** (SC-003: <2% gross pitch error):
- Sine waves at known frequencies across 40-2000 Hz range
- Sawtooth waves (harmonic content)
- Frequency sweep (verify tracking)
- Noisy signal (verify confidence gating)
- Silent input (verify unvoiced detection)
- Sub-sample precision test (verify parabolic interpolation improves accuracy)

---

### Phase 4: Dual-Window STFT Analysis (FR-018 to FR-021)

**Goal**: Run two concurrent STFT passes with different window sizes.

**Requires**: Add Blackman-Harris window to KrateDSP.

> **Note (tasks.md alignment)**: The tasks breakdown extracts the Blackman-Harris window addition and `parabolicInterpolation()` utility as a separate prerequisite phase (tasks.md Phase 3: "Foundational DSP Extensions") before the STFT configuration phase (tasks.md Phase 6). This allows those utilities to be developed and tested independently, and also unblocks tasks.md Phase 5 (YIN, which needs `parabolicInterpolation`) in parallel. The window work described here as "Requires" maps to tasks.md Phase 3.

**Blackman-Harris Window** (FR-019):
```
w[n] = a0 - a1*cos(2*pi*n/N) + a2*cos(4*pi*n/N) - a3*cos(6*pi*n/N)
a0=0.35875, a1=0.48829, a2=0.14128, a3=0.01168
```
Sidelobe rejection: ~92 dB (vs ~58 dB for Blackman, ~43 dB for Hann). Critical for peak detection where weak partials near strong ones must be resolved.

**Dual-Window Configuration**:

| Window | FFT Size | Hop Size | Update Rate | Purpose |
|--------|----------|----------|-------------|---------|
| Long | 4096 | 2048 (50%) | ~21.5 Hz at 44.1kHz | Low-frequency resolution (partials 1-4) |
| Short | 1024 | 512 (50%) | ~86.1 Hz at 44.1kHz | Upper harmonic tracking (partials 5+) |

The long window runs at a slower update rate (FR-020) by virtue of its larger hop size. Both use Blackman-Harris windowing.

**Design**: This is plugin-local wiring of two STFT instances with SpectralBuffer outputs. The dual-window analysis is orchestrated by the SampleAnalyzer (Phase 8) or the processor (for future live mode).

**Files to modify**:
- `dsp/include/krate/dsp/core/window_functions.h` -- add BlackmanHarris to WindowType enum, add generateBlackmanHarris()
- `dsp/tests/unit/core/window_functions_tests.cpp` -- add BlackmanHarris tests

**Testing**:
- BlackmanHarris window coefficients match published values
- STFT with BlackmanHarris window produces expected spectral output
- Dual-window setup: both STFTs produce valid SpectralBuffer output
- Long window produces higher frequency resolution than short window (verify bin spacing)

---

### Phase 5: Partial Detection and Tracking (FR-022 to FR-028)

**Goal**: Detect spectral peaks, map to harmonics, track across frames.

**New Component**: `PartialTracker` at `dsp/include/krate/dsp/processors/partial_tracker.h`

**Sub-stages**:

1. **Peak Detection** (FR-022): Find local maxima in magnitude spectrum. For each peak, apply parabolic interpolation across 3 bins for sub-bin frequency precision.

2. **Harmonic Sieve** (FR-023): Map detected peaks to integer multiples of F0. Tolerance window scales with harmonic number: `tolerance_n = baseToleranceHz * sqrt(n)`. Peaks not matching any harmonic are discarded.

3. **Frame-to-Frame Matching** (FR-024): Match current frame's partials to previous frame's tracks using frequency proximity. Cost metric: `|freq_current - freq_predicted|` where prediction is based on previous frequency (simple linear extrapolation).

4. **Birth/Death** (FR-025): New peaks start with zero amplitude and fade in over ~2ms. Missing peaks are held for a grace period of 4 frames before fade-out over ~2ms.

5. **Active Set Management** (FR-026, FR-027): Hard cap of 48 partials. Rank by `energy * stability`. Hysteresis: once a partial enters the active set, it stays for at least 4 frames before it can be replaced (prevents rapid timbral instability).

6. **Per-Partial Data** (FR-028):
```cpp
struct Partial {
    int harmonicIndex = 0;      // 1-based harmonic number
    float frequency = 0.0f;     // Hz (measured, not idealized)
    float amplitude = 0.0f;     // Linear
    float phase = 0.0f;         // Radians
    float relativeFrequency = 0.0f; // frequency / F0
    float inharmonicDeviation = 0.0f; // relativeFrequency - harmonicIndex
    float stability = 0.0f;     // Tracking confidence (0..1)
    int age = 0;                // Frames since track birth
};
```

**Design**:
```cpp
class PartialTracker {
public:
    static constexpr size_t kMaxPartials = 48;
    static constexpr int kGracePeriodFrames = 4;

    void prepare(size_t fftSize, double sampleRate) noexcept;
    void reset() noexcept;

    // Process one spectral frame, given the current F0 estimate
    void processFrame(const SpectralBuffer& spectrum,
                      const F0Estimate& f0,
                      size_t fftSize,
                      float sampleRate) noexcept;

    // Get current tracked partials
    [[nodiscard]] const std::array<Partial, kMaxPartials>& getPartials() const noexcept;
    [[nodiscard]] int getActiveCount() const noexcept;

private:
    // Peak detection
    void detectPeaks(const SpectralBuffer& spectrum, size_t fftSize, float sampleRate);
    // Harmonic sieve
    void mapToHarmonics(float f0);
    // Frame-to-frame matching
    void matchTracks();
    // Birth/death management
    void updateLifecycles();
    // Active set management
    void enforcePartialCap();

    std::array<Partial, kMaxPartials> partials_{};
    std::array<Partial, kMaxPartials> previousPartials_{};
    int activeCount_ = 0;
    // ... internal buffers for peak detection
};
```

**Files to create**:
- `dsp/include/krate/dsp/processors/partial_tracker.h`
- `dsp/tests/unit/processors/partial_tracker_tests.cpp`

**Testing**:
- Single sine: detects one partial at correct frequency and amplitude
- Harmonic series: detects partials 1-N at correct frequencies
- Inharmonic signal (bell): detects non-integer frequency ratios
- Tracking stability: partials maintain identity across frames
- Grace period: disappearing partial is held for 4 frames before death
- Partial cap: never exceeds 48 active partials

---

### Phase 6: Harmonic Model Builder (FR-029 to FR-034)

**Goal**: Convert raw partial measurements into a stable, musically useful HarmonicFrame.

**New Component**: `HarmonicModelBuilder` at `dsp/include/krate/dsp/systems/harmonic_model_builder.h`

**HarmonicFrame Structure** (FR-029, defined in harmonic_types.h):
```cpp
struct HarmonicFrame {
    float f0 = 0.0f;                           // Fundamental frequency (Hz)
    float f0Confidence = 0.0f;                  // From YIN
    std::array<Partial, 48> partials{};         // Active partials
    int numPartials = 0;                        // Active count (0-48)
    float spectralCentroid = 0.0f;              // Amplitude-weighted mean freq
    float brightness = 0.0f;                    // Perceptual brightness
    float noisiness = 0.0f;                     // Residual-to-harmonic ratio
    float globalAmplitude = 0.0f;               // Smoothed RMS
};
```

**Key Responsibilities**:

1. **L2 Normalization** (FR-030): Separate spectral shape from loudness.
   ```
   normFactor = 1.0 / sqrt(sum(amp_i^2))
   normalizedAmp_i = amp_i * normFactor
   ```

2. **Dual-Timescale Blending** (FR-031): Fast layer captures articulation (~5ms smoothing), slow layer captures timbral identity (~100ms smoothing). Blended via responsiveness parameter:
   ```
   output = lerp(slowModel, fastFrame, responsiveness)
   ```

3. **Spectral Descriptors** (FR-032): Centroid = amplitude-weighted mean frequency. Brightness = spectral centroid normalized by F0.

4. **Median Filtering** (FR-033): Per-partial amplitude median filter (window of 5 frames) to reject impulsive outliers while preserving step edges.

5. **Global Amplitude** (FR-034): OnePoleSmoother tracking RMS of the source signal, independent of per-partial amplitudes.

**Files to create**:
- `dsp/include/krate/dsp/systems/harmonic_model_builder.h`
- `dsp/tests/unit/systems/harmonic_model_builder_tests.cpp`

**Testing**:
- L2 normalization: verify sum of squared normalized amplitudes = 1.0
- Dual timescale: verify fast layer responds within ~5ms, slow layer within ~100ms
- Median filter: verify impulse is rejected, step change is preserved
- Spectral centroid: verify against known harmonic distributions

---

### Phase 7: Harmonic Oscillator Bank (FR-035 to FR-042)

**Goal**: Synthesize audio from the harmonic model using MCF oscillators.

**New Component**: `HarmonicOscillatorBank` at `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`

**Design** (adapted from ParticleOscillator MCF pattern):

```cpp
class HarmonicOscillatorBank {
public:
    static constexpr size_t kMaxPartials = 48;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Load a harmonic frame (updates target amplitudes and frequencies)
    void loadFrame(const HarmonicFrame& frame, float targetPitch) noexcept;

    // Set the target pitch (MIDI-driven)
    void setTargetPitch(float frequencyHz) noexcept;

    // Set inharmonicity amount (0-1, FR-042)
    void setInharmonicityAmount(float amount) noexcept;

    // Process a single sample
    [[nodiscard]] float process() noexcept;

    // Process a block
    void processBlock(float* output, size_t numSamples) noexcept;

private:
    // SoA layout (FR-036) -- 32-byte aligned for cache efficiency
    alignas(32) std::array<float, kMaxPartials> sinState_{};
    alignas(32) std::array<float, kMaxPartials> cosState_{};
    alignas(32) std::array<float, kMaxPartials> epsilon_{};
    alignas(32) std::array<float, kMaxPartials> currentAmplitude_{};
    alignas(32) std::array<float, kMaxPartials> targetAmplitude_{};
    alignas(32) std::array<float, kMaxPartials> antiAliasGain_{};
    alignas(32) std::array<float, kMaxPartials> relativeFrequency_{};
    alignas(32) std::array<float, kMaxPartials> inharmonicDeviation_{};

    float targetPitch_ = 440.0f;
    float inharmonicityAmount_ = 1.0f;
    float sampleRate_ = 44100.0;
    float nyquist_ = 22050.0f;
    float inverseSampleRate_ = 1.0f / 44100.0f;
    int activePartials_ = 0;

    // Amplitude smoothing coefficient (one-pole)
    float ampSmoothCoeff_ = 0.0f;
};
```

**MCF Variant Note**: The oscillator uses the Gordon-Smith Modified Coupled Form (MCF), which is the same algorithm as the "magic circle oscillator" referenced in project memory. The specific per-sample recurrence is `sinNew = sin + eps*cos; cosNew = cos - eps*sinNew` (uses the updated `sinNew` in the second step). This variant has a determinant of 1 (amplitude-stable). It is NOT the standard "magic circle" variant which uses `cosNew = cos - eps*sin` (original `sin`). The former is used throughout this implementation.

**Frequency Calculation** (FR-037):
```
partialFreq_n = (harmonicIndex + deviation_n * inharmonicityAmount) * targetPitch
```
Where `deviation_n = relativeFrequency_n - harmonicIndex` captures natural inharmonicity.

**Anti-Aliasing** (FR-038):
```
fadeStart = 0.8 * nyquist
fadeEnd = nyquist
antiAliasGain = clamp(1.0 - (partialFreq - fadeStart) / (fadeEnd - fadeStart), 0, 1)
```
Recalculated on pitch change, not per sample.

**Phase Continuity** (FR-039): Only epsilon and amplitude are updated. Phase accumulators (sinState/cosState) continue running.

**Crossfade on Discontinuity** (FR-040): When F0 jumps > 1 semitone, snapshot current oscillator output, start fresh oscillators, crossfade old-to-new over 2-5ms (default 3ms).

**Amplitude Smoothing** (FR-041): One-pole lowpass per partial. Coefficient set for ~2ms smoothing at sample rate.

**Inharmonicity Control** (FR-042): 0% = perfect harmonic ratios (freq = n * f0), 100% = source's captured inharmonicity.

**Files to create**:
- `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`
- `dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp`

**Testing** (SC-002: <0.5% CPU for 48 partials):
- Single partial at known frequency: verify output matches expected sine
- 48 partials: verify all contribute to output
- Anti-aliasing: verify partials above Nyquist are silent (SC-006)
- Phase continuity: verify no clicks when frequency changes gradually
- Crossfade: verify smooth transition on large pitch jump
- Amplitude smoothing: verify no clicks on amplitude step change
- Inharmonicity: verify 0% produces perfect harmonics, 100% preserves source deviation
- CPU benchmark: measure and verify <0.5% (SC-002)

---

### Phase 8: Sample Mode Integration (FR-043 to FR-047)

**Goal**: Load audio files, run analysis on background thread, store results.

**External Dependency**: dr_wav (dr_libs)
- Single-header MIT library at `extern/dr_libs/dr_wav.h`
- Supports WAV and AIFF containers
- API: `drwav_open_file_and_read_pcm_frames_f32()` for one-call loading

**New Plugin-Local Components**:

1. **SampleAnalysis** (`plugins/innexus/src/dsp/sample_analysis.h`):
```cpp
struct SampleAnalysis {
    std::vector<HarmonicFrame> frames;   // Time-indexed sequence
    float sampleRate = 0.0f;             // Source sample rate
    float hopTimeSec = 0.0f;             // Time between frames
    size_t totalFrames = 0;
    std::string filePath;                // For state persistence

    [[nodiscard]] const HarmonicFrame& getFrame(size_t index) const noexcept;
    [[nodiscard]] size_t frameCount() const noexcept { return totalFrames; }
};
```

2. **SampleAnalyzer** (`plugins/innexus/src/dsp/sample_analyzer.h`):
```cpp
class SampleAnalyzer {
public:
    // Start analysis on background thread (FR-044)
    void startAnalysis(const std::string& filePath);

    // Check if analysis is complete
    [[nodiscard]] bool isComplete() const noexcept;

    // Get result (call only after isComplete() returns true)
    [[nodiscard]] std::unique_ptr<SampleAnalysis> takeResult();

    // Cancel ongoing analysis
    void cancel();

private:
    // Background thread runs the full pipeline (FR-045):
    // 1. Load WAV/AIFF with dr_wav (FR-043, stereo-to-mono downmix)
    // 2. Pre-processing pipeline
    // 3. Feed through YIN + dual STFT + PartialTracker + ModelBuilder
    // 4. Store resulting HarmonicFrame sequence
    void analyzeOnThread(std::vector<float> audioData, float sampleRate);

    std::thread analysisThread_;
    std::atomic<bool> complete_{false};
    std::atomic<bool> cancelled_{false};
    std::unique_ptr<SampleAnalysis> result_;
};
```

**File Loading** (FR-043):
- Use dr_wav's `drwav_open_file_and_read_pcm_frames_f32()` for WAV files
- Stereo files: downmix to mono by averaging L+R channels
- The `dr_wav.h` implementation define goes in exactly one .cpp file:
  ```cpp
  #define DR_WAV_IMPLEMENTATION
  #include "dr_wav.h"
  ```

**Background Thread** (FR-044): Analysis runs on `std::thread`, never blocking the audio thread. Completed SampleAnalysis is published to the audio thread via `std::atomic<SampleAnalysis*>` (FR-058).

**Same Code Path** (FR-045): The analyzer uses PreProcessingPipeline, YinPitchDetector, STFT, PartialTracker, and HarmonicModelBuilder -- identical code to what would be used for live analysis.

**Frame Indexing** (FR-046): HarmonicFrames indexed by time position. Frame interval = STFT hop size / sample rate.

**Playback Behavior** (FR-047): On MIDI note-on, start from frame 0, advance one frame per hop interval. When last frame is reached, hold it for the remainder of the note.

**Files to create**:
- `extern/dr_libs/dr_wav.h` -- downloaded from github.com/mackron/dr_libs
- `plugins/innexus/src/dsp/sample_analysis.h`
- `plugins/innexus/src/dsp/sample_analyzer.h`
- `plugins/innexus/src/dsp/sample_analyzer.cpp` -- contains DR_WAV_IMPLEMENTATION
- Test files for sample loading and analysis

**Testing** (SC-005: <10s for 10s mono at 44.1kHz):
- Load WAV file: verify sample data is correctly read
- Load AIFF file: verify AIFF support works
- Stereo downmix: verify stereo file produces mono output
- Background thread: verify analysis does not block main thread
- Analysis completion: verify SampleAnalysis contains expected number of frames
- Analysis timing: measure and verify <10s for 10s file (SC-005)
- Atomic pointer swap: verify audio thread reads correct data after publication

---

### Phase 9: MIDI Integration and Playback (FR-048 to FR-058)

**Goal**: Wire MIDI input to oscillator bank for a playable instrument.

**Processor Modifications** (all in `plugins/innexus/src/processor/`):

1. **MIDI Note-On** (FR-048, FR-054):
   - Convert MIDI note to frequency: `midiNoteToFrequency(note)`
   - Load HarmonicFrame from SampleAnalysis (start at frame 0)
   - Set oscillator bank target pitch
   - Apply velocity gain (FR-050)
   - Monophonic: last-note-priority for overlapping notes

2. **MIDI Note-Off** (FR-049, FR-057):
   - Enter release phase: exponential decay `A(t) = exp(-t/tau)`
   - Default release time: 100ms (user-adjustable via kReleaseTimeId parameter)
   - Applied to summed voice output, not per-partial
   - Minimum 20ms anti-click fade enforced (FR-057)

3. **Velocity** (FR-050):
   - Scale global amplitude, NOT individual partial amplitudes
   - Preserves timbre across velocity levels

4. **Pitch Bend** (FR-051):
   - Recalculate epsilon for all partials immediately
   - Recalculate anti-aliasing gains on new frequencies

5. **Confidence-Gated Freeze** (FR-052, FR-053):
   - When F0 confidence drops below threshold during analysis, hold last known-good frame
   - On confidence recovery: crossfade from frozen frame back to live tracking over 5-10ms

6. **No Sample = Silence** (FR-055): When no SampleAnalysis is loaded, output silence even if MIDI notes arrive.

7. **State Persistence** (FR-056): Save/restore sample file path + parameter values via IBStream. On restore, re-run analysis if the file exists.

8. **Atomic Pointer Swap** (FR-058): The processor reads `currentAnalysis_` with `memory_order_acquire` before each process block, and the background thread publishes via `memory_order_release`. See Phase 8 (Sample Mode Integration) for the full atomic pointer swap design and memory management pattern.

**Frame Advancement Logic** (FR-047):
```cpp
// Per audio block:
if (noteActive_ && analysis) {
    frameSampleCounter_ += numSamples;
    while (frameSampleCounter_ >= hopSizeInSamples && currentFrameIndex_ < analysis->totalFrames - 1) {
        frameSampleCounter_ -= hopSizeInSamples;
        currentFrameIndex_++;
        oscillatorBank_.loadFrame(analysis->frames[currentFrameIndex_], targetPitch_);
    }
    // If at last frame, just keep it loaded (hold behavior)
}
```

**Release Envelope** (FR-049):
```cpp
// Per sample during release:
float releaseGain = std::exp(-samplesSinceNoteOff * releaseDecayRate_);
// Where releaseDecayRate_ = 1.0 / (releaseTimeMs * 0.001 * sampleRate * 5.0)
// (5 time constants for ~99% decay)

// Anti-click minimum (FR-057):
float antiClickMinMs = 20.0f;
float effectiveRelease = std::max(releaseTimeMs, antiClickMinMs);
```

**Files to modify**:
- `plugins/innexus/src/processor/processor.h` -- add all DSP members
- `plugins/innexus/src/processor/processor.cpp` -- full process() with MIDI handling
- `plugins/innexus/src/plugin_ids.h` -- ensure all M1 parameters are defined
- `plugins/innexus/src/controller/controller.cpp` -- register all M1 parameters
- `plugins/innexus/CMakeLists.txt` -- add new source files

**Testing**:
- Note-on produces audio output (SC-007: within one buffer duration)
- Note-off produces smooth decay (SC-008: no clicks)
- Velocity scaling: higher velocity = louder, same timbre
- Pitch bend: all partials shift proportionally
- Monophonic: second note replaces first without click
- No sample = silence (FR-055)
- State save/load round-trip (SC-009)
- Zero audio-thread allocations (SC-010)
- 20ms minimum anti-click fade verified

---

## New Files Summary

### KrateDSP Library (dsp/)

| File | Layer | Type | Description |
|------|-------|------|-------------|
| `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | NEW | F0Estimate, Partial, HarmonicFrame data types |
| `dsp/include/krate/dsp/processors/yin_pitch_detector.h` | 2 | NEW | YIN pitch detection with FFT acceleration |
| `dsp/include/krate/dsp/processors/partial_tracker.h` | 2 | NEW | Spectral peak detection and harmonic tracking |
| `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | 2 | NEW | 48-oscillator MCF synthesis bank |
| `dsp/include/krate/dsp/systems/harmonic_model_builder.h` | 3 | NEW | Analysis-to-model pipeline with smoothing |
| `dsp/include/krate/dsp/core/window_functions.h` | 0 | MODIFIED | Add BlackmanHarris window |
| `dsp/include/krate/dsp/primitives/spectral_utils.h` | 1 | MODIFIED | Add parabolicInterpolation() |
| `dsp/tests/unit/processors/yin_pitch_detector_tests.cpp` | -- | NEW | YIN tests |
| `dsp/tests/unit/processors/partial_tracker_tests.cpp` | -- | NEW | Partial tracking tests |
| `dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp` | -- | NEW | Oscillator bank tests |
| `dsp/tests/unit/systems/harmonic_model_builder_tests.cpp` | -- | NEW | Model builder tests |

### Plugin (plugins/innexus/)

| File | Type | Description |
|------|------|-------------|
| `plugins/innexus/src/dsp/pre_processing_pipeline.h` | NEW | DC block + HPF + gate + transient suppression |
| `plugins/innexus/src/dsp/sample_analysis.h` | NEW | SampleAnalysis data structure |
| `plugins/innexus/src/dsp/sample_analyzer.h` | NEW | Background thread sample analysis |
| `plugins/innexus/src/dsp/sample_analyzer.cpp` | NEW | dr_wav implementation + analysis thread |
| `plugins/innexus/src/parameters/innexus_params.h` | NEW | Parameter registration helpers |
| `plugins/innexus/src/plugin_ids.h` | MODIFIED | Add M1 parameter IDs |
| `plugins/innexus/src/processor/processor.h` | MODIFIED | Add DSP members, MIDI handling |
| `plugins/innexus/src/processor/processor.cpp` | MODIFIED | Full process() implementation |
| `plugins/innexus/src/controller/controller.h` | MODIFIED | Register parameters |
| `plugins/innexus/src/controller/controller.cpp` | MODIFIED | Parameter registration |
| `plugins/innexus/CMakeLists.txt` | MODIFIED | Add new source files |

### External Dependencies

| File | Type | Description |
|------|------|-------------|
| `extern/dr_libs/dr_wav.h` | NEW | Single-header WAV/AIFF decoder (MIT license) |

---

## Sample File Delivery Mechanism

The user will need to specify a WAV/AIFF file path to the plugin. Since there is no GUI in M1, the mechanism is:

1. **State persistence**: The file path is stored as a string in the plugin state (IBStream). On session reload, the processor reads the path and re-runs analysis.

2. **IMessage**: The controller sends the file path to the processor via `IMessage`. For M1 without GUI, this would be triggered by a host-provided file browser or preset loading.

3. **For testing**: A dedicated test helper loads a sample directly into the SampleAnalyzer without going through the VST3 message system.

**Note**: Full file browser UI is deferred to M7 (Plugin UI milestone). For M1, the plugin can be tested by:
- Loading state from a preset file that contains a sample path
- Using the generic host parameter UI
- Direct unit testing of SampleAnalyzer

---

## Complexity Tracking

No constitution violations requiring justification. All design decisions are within the bounds of the constitution.

| Decision | Rationale |
|----------|-----------|
| Background std::thread for analysis | std::thread is simpler than a full thread pool; only one analysis runs at a time |
| Atomic pointer swap (not lock-free queue) | SampleAnalysis is published once (immutable after publication); no need for streaming queue |
| Monophonic only for M1 | Spec requirement (FR-054); polyphonic voice allocation deferred |
| No GUI for M1 | Spec assumption; full VSTGUI interface deferred to M7 |
