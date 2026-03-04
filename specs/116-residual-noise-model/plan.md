# Implementation Plan: Innexus Milestone 2 -- Residual/Noise Model (SMS Decomposition)

**Branch**: `116-residual-noise-model` | **Date**: 2026-03-04 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/116-residual-noise-model/spec.md`

## Summary

Implement the deterministic+stochastic (SMS) decomposition for the Innexus additive synthesis engine. This adds two new KrateDSP Layer 2 processors -- `ResidualAnalyzer` (extracts the noise component from analyzed samples via spectral subtraction) and `ResidualSynthesizer` (resynthesizes shaped noise in real time via FFT-domain spectral envelope multiplication and overlap-add reconstruction). Three new VST3 parameters (Harmonic/Residual Mix, Residual Brightness, Transient Emphasis) allow user control. The existing `SampleAnalysis` structure, `SampleAnalyzer` pipeline, Innexus `Processor`, `Controller`, and state persistence are extended to integrate the residual model alongside the existing harmonic model from Milestone 1.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: KrateDSP (in-repo), Steinberg VST3 SDK 3.7.x, VSTGUI 4.12+, pffft (FFT backend)
**Storage**: IBStream binary blob (VST3 state persistence), extended with format versioning
**Testing**: Catch2 (via `dsp_tests` and `innexus_tests` targets) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang), Linux (GCC) -- cross-platform
**Project Type**: Monorepo -- shared DSP library + plugin
**Performance Goals**: ResidualSynthesizer < 0.5% CPU single core @ 44.1kHz/128 buffer; total plugin < 5% CPU
**Constraints**: Zero allocations on audio thread; real-time safe; all buffers pre-allocated in `prepare()`
**Scale/Scope**: 2 new DSP components, 3 new VST3 parameters, extensions to 6 existing files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate; new parameters registered in Controller, atomics in Processor
- [x] State flows Host -> Processor -> Controller via `setComponentState()`
- [x] No cross-inclusion of processor/controller headers

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] ResidualSynthesizer pre-allocates all buffers in `prepare()` (FFT, OverlapAdd, SpectralBuffer, noise buffer)
- [x] No allocations, locks, exceptions, or I/O on audio thread
- [x] ResidualAnalyzer runs only on background analysis thread (not audio thread)

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed below (verdict: MARGINAL -- DEFER)
- [x] Scalar-first workflow: implement scalar, measure, then SIMD if needed

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] ResidualAnalyzer at Layer 2, depends only on Layer 0-1 (FFT, STFT, SpectralTransientDetector, Xorshift32)
- [x] ResidualSynthesizer at Layer 2, depends only on Layer 0-1 (FFT, OverlapAdd, SpectralBuffer, Xorshift32, OnePoleSmoother)
- [x] No circular dependencies

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created (verified: no existing ResidualAnalyzer, ResidualSynthesizer, ResidualFrame in codebase)

**Required Check - Principle XVI (Honest Completion):**
- [x] Compliance table will be filled with actual file paths, line numbers, test names, measured values
- [x] No thresholds will be relaxed from spec

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ResidualFrame, ResidualAnalyzer, ResidualSynthesizer

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ResidualFrame | `grep -r "struct ResidualFrame" dsp/ plugins/` | No | Create New in `dsp/include/krate/dsp/processors/residual_types.h` |
| ResidualAnalyzer | `grep -r "class ResidualAnalyzer" dsp/ plugins/` | No | Create New in `dsp/include/krate/dsp/processors/residual_analyzer.h` |
| ResidualSynthesizer | `grep -r "class ResidualSynthesizer" dsp/ plugins/` | No | Create New in `dsp/include/krate/dsp/processors/residual_synthesizer.h` |

**Utility Functions to be created**: None planned as standalone utilities. All residual-specific logic is encapsulated in the two new classes.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FFT | `dsp/include/krate/dsp/primitives/fft.h` | 1 | Forward FFT for residual spectral analysis; inverse FFT in OverlapAdd for resynthesis |
| STFT | `dsp/include/krate/dsp/primitives/stft.h` | 1 | Streaming STFT for residual spectral analysis during sample analysis |
| OverlapAdd | `dsp/include/krate/dsp/primitives/stft.h` | 1 | Overlap-add reconstruction in ResidualSynthesizer for real-time output |
| SpectralBuffer | `dsp/include/krate/dsp/primitives/spectral_buffer.h` | 1 | Temporary spectral storage during analysis and synthesis |
| SpectralTransientDetector | `dsp/include/krate/dsp/primitives/spectral_transient_detector.h` | 1 | Transient detection for residual frames (FR-007) |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Parameter smoothing for mix/brightness/transient emphasis (FR-025) |
| Xorshift32 | `dsp/include/krate/dsp/core/random.h` | 0 | Deterministic white noise generation (FR-013, FR-030) |
| Window functions | `dsp/include/krate/dsp/core/window_functions.h` | 0 | Synthesis windowing in OverlapAdd |
| HarmonicFrame | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Existing harmonic analysis output; residual frames are produced alongside these |
| Partial | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Tracked partial frequencies/amplitudes for harmonic subtraction |
| kMaxPartials | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Constant (48) for partial array sizing |
| HarmonicOscillatorBank | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | 2 | Used offline to generate harmonic signal for subtraction |
| SampleAnalysis | `plugins/innexus/src/dsp/sample_analysis.h` | plugin | Extend to include residual frames |
| SampleAnalyzer | `plugins/innexus/src/dsp/sample_analyzer.h` | plugin | Extend analysis pipeline to run residual analysis |
| dual_stft_config.h | `plugins/innexus/src/dsp/dual_stft_config.h` | plugin | Short window config (fftSize=1024, hopSize=512) for residual synthesizer alignment |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no existing residual classes)
- [x] `plugins/innexus/src/` - Plugin source (no residual code yet)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All three planned types (`ResidualFrame`, `ResidualAnalyzer`, `ResidualSynthesizer`) are unique names not found anywhere in the codebase. They will live in the `Krate::DSP` namespace (for the DSP components) and `Innexus` namespace (for plugin extensions). No naming conflicts detected.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FFT | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| FFT | forward | `void forward(const float* input, Complex* output) noexcept` | Yes |
| FFT | inverse | `void inverse(const Complex* input, float* output) noexcept` | Yes |
| FFT | numBins | `[[nodiscard]] size_t numBins() const noexcept` (returns N/2+1) | Yes |
| OverlapAdd | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window, float kaiserBeta, bool applySynthesisWindow)` | Yes |
| OverlapAdd | synthesize | `void synthesize(const SpectralBuffer& input) noexcept` | Yes |
| OverlapAdd | pullSamples | `void pullSamples(float* output, size_t numSamples) noexcept` | Yes |
| OverlapAdd | samplesAvailable | `[[nodiscard]] size_t samplesAvailable() const noexcept` | Yes |
| OverlapAdd | reset | `void reset() noexcept` | Yes |
| SpectralBuffer | prepare | `void prepare(size_t fftSize)` | Yes |
| SpectralBuffer | data (const) | `[[nodiscard]] const Complex* data() const noexcept` | Yes |
| SpectralBuffer | data (mutable) | `[[nodiscard]] Complex* data() noexcept` | Yes |
| SpectralBuffer | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| SpectralBuffer | getMagnitude | `[[nodiscard]] float getMagnitude(size_t bin) const noexcept` | Yes |
| SpectralBuffer | setCartesian | `void setCartesian(size_t bin, float real, float imag) noexcept` | Yes |
| SpectralTransientDetector | prepare | `void prepare(std::size_t numBins) noexcept` | Yes |
| SpectralTransientDetector | detect | `[[nodiscard]] bool detect(const float* magnitudes, std::size_t numBins) noexcept` | Yes |
| SpectralTransientDetector | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float timeMs, float sampleRate) noexcept` (inferred from ctor) | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float value) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| Xorshift32 | seed | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` (returns [-1.0, 1.0]) | Yes |
| Complex | real/imag | `float real = 0.0f; float imag = 0.0f;` | Yes |
| Complex | magnitude | `[[nodiscard]] float magnitude() const noexcept` | Yes |
| Complex | phase | `[[nodiscard]] float phase() const noexcept` | Yes |
| HarmonicFrame | partials | `std::array<Partial, kMaxPartials> partials{}` | Yes |
| HarmonicFrame | numPartials | `int numPartials = 0` | Yes |
| Partial | frequency | `float frequency = 0.0f` (Hz, actual measured) | Yes |
| Partial | amplitude | `float amplitude = 0.0f` (linear) | Yes |
| Partial | phase | `float phase = 0.0f` (radians) | Yes |
| STFT | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window)` | Yes |
| STFT | pushSamples | (takes float* and size_t) | Yes |
| STFT | canAnalyze | `[[nodiscard]] bool canAnalyze() const noexcept` | Yes |
| STFT | analyze | `void analyze(SpectralBuffer& output) noexcept` | Yes |
| kShortWindowConfig | fftSize | `1024` | Yes |
| kShortWindowConfig | hopSize | `512` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT class and Complex struct
- [x] `dsp/include/krate/dsp/primitives/stft.h` - STFT class and OverlapAdd class
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer class
- [x] `dsp/include/krate/dsp/primitives/spectral_transient_detector.h` - SpectralTransientDetector class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/processors/harmonic_types.h` - HarmonicFrame, Partial, F0Estimate, kMaxPartials
- [x] `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` - HarmonicOscillatorBank class
- [x] `plugins/innexus/src/dsp/dual_stft_config.h` - StftWindowConfig, kShortWindowConfig, kLongWindowConfig
- [x] `plugins/innexus/src/dsp/sample_analysis.h` - SampleAnalysis struct
- [x] `plugins/innexus/src/dsp/sample_analyzer.h` - SampleAnalyzer class
- [x] `plugins/innexus/src/processor/processor.h` - Innexus Processor class
- [x] `plugins/innexus/src/controller/controller.h` - Innexus Controller class
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter IDs and ranges
- [x] `plugins/innexus/src/parameters/innexus_params.h` - Parameter registration pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| FFT | Output format: `Complex[N/2+1]` where bin 0 = DC, bin N/2 = Nyquist | Use `numBins()` for iteration range |
| FFT | Inverse applies 1/N normalization internally | Do not apply additional normalization |
| OverlapAdd | `synthesize()` takes `const SpectralBuffer&`, not raw Complex* | Must populate a SpectralBuffer before calling |
| OverlapAdd | Samples must be pulled via `pullSamples()` after each `synthesize()` call | Check `samplesAvailable()` first |
| SpectralBuffer | `data()` returns `Complex*` (Cartesian form); polar access via `getMagnitude()`/`getPhase()` causes lazy conversion | Use Cartesian access for writing spectral data directly |
| Xorshift32 | `nextFloat()` returns [-1.0, 1.0] bipolar | This IS what we want for white noise |
| OnePoleSmoother | Constructor takes `(timeMs, sampleRate)` | Must call `configure()` or use parameterized constructor |
| State version | Currently version 1 in `Processor::getState()` | Must bump to version 2 for M2 with residual data |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Spectral envelope extraction (piecewise-linear from magnitude spectrum) | Tightly coupled to ResidualAnalyzer; only 1 consumer now; may extract if needed by M3 spectral coring |
| Spectral envelope interpolation (breakpoints to FFT bins) | Only used by ResidualSynthesizer; simple linear interpolation between log-spaced breakpoints |
| Spectral tilt application | Only used by ResidualSynthesizer brightness control; 2-line computation per bin |

**Decision**: No Layer 0 extractions needed. All new utility logic is specific to the residual model and has only 1 consumer. Extraction would be premature; revisit after M3 (spectral coring) if shared patterns emerge.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Residual synthesizer is feed-forward: noise -> FFT -> multiply envelope -> IFFT -> overlap-add. No sample feedback. |
| **Data parallelism width** | ~513 bins (FFT size 1024) | The spectral multiplication loop processes 513 complex bins independently per frame. |
| **Branch density in inner loop** | LOW | The hot path is a simple multiply-and-accumulate per bin with no conditionals. |
| **Dominant operations** | FFT/IFFT (handled by pffft with SIMD internally) + per-bin multiply | pffft already uses SIMD for FFT. The spectral multiply loop is the only custom code. |
| **Current CPU budget vs expected usage** | 0.5% budget; expected ~0.2-0.3% (one STFT pass) | Well within budget based on existing STFT benchmarks. |

### SIMD Viability Verdict

**Verdict**: MARGINAL -- DEFER

**Reasoning**: The dominant cost is the FFT/IFFT pair, which is already SIMD-optimized internally by pffft. The remaining per-bin spectral multiplication loop (513 bins per frame, ~86 frames/sec at 44.1kHz/512 hop) is trivially cheap. The algorithm is well within its 0.5% CPU budget without custom SIMD. If profiling reveals otherwise, the bin-multiplication loop is trivially vectorizable in a future pass.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip residual processing when mix is 0% | ~100% savings when residual disabled | LOW | YES |
| Pre-compute interpolated envelope once per frame | Avoids re-interpolation per sample | LOW | YES (included in design) |
| Reuse noise buffer across frames (rotate instead of regenerate) | ~10% savings on PRNG calls | LOW | DEFER (PRNG is cheap) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer** (from ROADMAP.md):
- M3 Phase 12: Live Sidechain Mode -- needs real-time residual estimation via spectral coring (different analysis, same ResidualFrame format, same ResidualSynthesizer)
- M4 Phases 13-14: Freeze/Morph -- morphs `residualBands` between snapshots (consumes ResidualFrame data)
- M5 Phases 15-16: Harmonic Memory -- stores `residualBands[16]` and `residualEnergy` in HarmonicSnapshot

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| ResidualFrame | HIGH | M3 spectral coring, M4 morph, M5 snapshot | Extract to `residual_types.h` in Layer 2 now |
| ResidualSynthesizer | HIGH | M3 live sidechain (same synthesis, different analysis source) | Keep in Layer 2 KrateDSP -- designed for reuse |
| ResidualAnalyzer | MEDIUM | M3 may use spectral coring instead, but format is shared | Keep in Layer 2 KrateDSP |
| Spectral envelope interpolation | MEDIUM | M3 spectral coring output needs same interpolation | Keep as method in ResidualSynthesizer; extract if M3 needs it |

### Detailed Analysis (for HIGH potential items)

**ResidualFrame** provides:
- Standard per-frame representation of the stochastic component
- 16-band spectral envelope + total energy + transient flag
- Serializable format for state persistence and snapshot storage

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| M3 Live Sidechain | YES | Spectral coring produces ResidualFrame with same fields |
| M4 Freeze/Morph | YES | Morphs `bandEnergies` between two ResidualFrame snapshots |
| M5 Harmonic Memory | YES | HarmonicSnapshot stores `residualBands[16]` + `residualEnergy` from ResidualFrame |

**Recommendation**: Place `ResidualFrame` in `dsp/include/krate/dsp/processors/residual_types.h` alongside `harmonic_types.h`, making it a first-class KrateDSP type immediately.

**ResidualSynthesizer** provides:
- FFT-domain spectral envelope shaping of white noise
- Frame-driven synthesis with overlap-add output
- Parameter controls (brightness tilt, energy scaling)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| M3 Live Sidechain | YES | Same synthesis engine, different frame source (real-time vs precomputed) |
| Iterum spectral effects | MAYBE | General noise shaping concept, but Iterum uses different architecture |

**Recommendation**: Keep in KrateDSP Layer 2 as a general-purpose component. API is frame-driven, not Innexus-specific.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| ResidualFrame in separate `residual_types.h` | High reuse across M3-M5; avoids bloating `harmonic_types.h` while keeping it discoverable at the same layer |
| ResidualSynthesizer as KrateDSP Layer 2 | Reused directly by M3; general-purpose FFT noise shaping |
| ResidualAnalyzer as KrateDSP Layer 2 | General-purpose SMS subtraction method; M3 uses different method but same output format |

### Review Trigger

After implementing **M3 Live Sidechain (Phase 12)**, review this section:
- [ ] Does M3 need the spectral envelope interpolation utility? -> Extract to shared location
- [ ] Does M3 reuse ResidualSynthesizer directly? -> Confirm API stability
- [ ] Any duplicated code between subtraction analyzer and spectral coring? -> Consider shared base

## Project Structure

### Documentation (this feature)

```text
specs/116-residual-noise-model/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- residual_analyzer.h    # ResidualAnalyzer API contract
|   +-- residual_synthesizer.h # ResidualSynthesizer API contract
|   +-- residual_types.h       # ResidualFrame struct contract
+-- tasks.md             # Phase 2 output (NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
dsp/include/krate/dsp/processors/
+-- residual_types.h            # NEW: ResidualFrame struct (Layer 2)
+-- residual_analyzer.h         # NEW: ResidualAnalyzer class (Layer 2)
+-- residual_synthesizer.h      # NEW: ResidualSynthesizer class (Layer 2)

dsp/tests/unit/processors/
+-- residual_analyzer_tests.cpp    # NEW: Unit tests for ResidualAnalyzer
+-- residual_synthesizer_tests.cpp # NEW: Unit tests for ResidualSynthesizer
+-- residual_types_tests.cpp       # NEW: Unit tests for ResidualFrame

plugins/innexus/src/
+-- plugin_ids.h                # EXTEND: Add kHarmonicLevelId, kResidualLevelId,
|                               #         kResidualBrightnessId, kTransientEmphasisId
+-- dsp/sample_analysis.h      # EXTEND: Add residualFrames vector
+-- dsp/sample_analyzer.cpp     # EXTEND: Add residual analysis after harmonic analysis
+-- processor/processor.h       # EXTEND: Add ResidualSynthesizer, smoothers, atomic params
+-- processor/processor.cpp     # EXTEND: Sum harmonic + residual output, state versioning
+-- controller/controller.cpp   # EXTEND: Register new parameters, setComponentState
+-- parameters/innexus_params.h # EXTEND: Add residual param handling

plugins/innexus/tests/unit/processor/
+-- residual_integration_tests.cpp # NEW: Integration tests for residual in processor
```

**Structure Decision**: This feature follows the established monorepo pattern. New DSP components go in `dsp/include/krate/dsp/processors/` (Layer 2). Plugin integration extends existing files in `plugins/innexus/src/`. Tests are split between DSP unit tests (`dsp/tests/unit/processors/`) and plugin integration tests (`plugins/innexus/tests/unit/processor/`).

## Complexity Tracking

No constitution violations requiring justification. All design decisions align with established patterns.
