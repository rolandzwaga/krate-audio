# Implementation Plan: Live Sidechain Mode

**Branch**: `117-live-sidechain-mode` | **Date**: 2026-03-04 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/117-live-sidechain-mode/spec.md`

## Summary

Add real-time continuous analysis from a VST3 sidechain audio input to the Innexus harmonic analysis/synthesis plugin. When sidechain mode is active, the existing analysis pipeline (PreProcessingPipeline, YIN F0 tracker, STFT, PartialTracker, HarmonicModelBuilder) runs directly on the audio thread, processing live audio into HarmonicFrame data that drives the existing oscillator bank and residual synthesizer. A new SpectralCoringEstimator provides lightweight residual estimation without the latency of full subtraction. Two latency modes (low-latency: short window only, <=25ms; high-precision: dual window, ~50-100ms) allow users to trade latency for analysis quality. Input source switching between sample and sidechain modes uses a 20ms crossfade.

## Technical Context

**Language/Version**: C++20, MSVC 2022 / Clang / GCC
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (shared library), pffft (FFT backend)
**Storage**: N/A (VST3 state persistence via IBStream)
**Testing**: Catch2 (dsp_tests, innexus_tests targets) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang), Linux (GCC) -- cross-platform VST3 plugin
**Project Type**: Monorepo with shared DSP library + plugin
**Performance Goals**: Analysis pipeline <5% single-core CPU @ 44.1kHz; combined analysis + synthesis <8% CPU
**Constraints**: Zero allocations on audio thread; analysis-to-synthesis latency <=25ms (low-latency mode); all buffers pre-allocated
**Scale/Scope**: 2 new parameters, 1 new DSP class (SpectralCoringEstimator, Layer 2), 1 new plugin-local pipeline class (LiveAnalysisPipeline), processor bus and routing extensions; 6 new test files, 127 tasks across 12 phases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate; no cross-includes
- [x] New parameters registered in Controller, handled via atomics in Processor
- [x] State version bumped for new parameters; backward compatible

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] All analysis pipeline buffers pre-allocated in prepare()/setupProcessing() before setActive(true)
- [x] No memory allocation, locks, exceptions, or I/O on audio thread
- [x] Sidechain downmix buffer pre-allocated as `std::array<float, 8192>` member of Processor (constructed with the object — zero audio-thread allocation)
- [x] YIN circular buffer pre-allocated in LiveAnalysisPipeline::prepare()

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed (see section below)
- [x] Scalar-first workflow planned

**Required Check - Principle IX (Layered Architecture):**
- [x] SpectralCoringEstimator at Layer 2 (processors) -- depends only on Layer 0/1
- [x] LiveAnalysisPipeline is plugin-local (not in shared DSP library)
- [x] No circular dependencies introduced

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

**Classes/Structs to be created**:

| Planned Type | Search Pattern | Existing? | Action |
|--------------|----------------|-----------|--------|
| SpectralCoringEstimator | `SpectralCoring` | No | Create New in `dsp/include/krate/dsp/processors/` |
| LiveAnalysisPipeline | `LiveAnalysis` | No | Create New in `plugins/innexus/src/dsp/` |
| InputSource (enum) | `InputSource` | No | Create New in `plugins/innexus/src/plugin_ids.h` |
| LatencyMode (enum) | `LatencyMode` | No | Create New in `plugins/innexus/src/plugin_ids.h` |

**Utility Functions to be created**: None -- all utility functions already exist in the codebase.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| PreProcessingPipeline | plugins/innexus/src/dsp/pre_processing_pipeline.h | Plugin-local | Reuse directly in LiveAnalysisPipeline for sidechain audio cleaning |
| YinPitchDetector | dsp/include/krate/dsp/processors/yin_pitch_detector.h | 2 | Reuse for live F0 tracking |
| STFT | dsp/include/krate/dsp/primitives/stft.h | 1 | Reuse for spectral analysis (1 or 2 instances per latency mode) |
| SpectralBuffer | dsp/include/krate/dsp/primitives/spectral_buffer.h | 1 | Reuse for STFT output storage |
| PartialTracker | dsp/include/krate/dsp/processors/partial_tracker.h | 2 | Reuse for frame-to-frame partial tracking |
| HarmonicModelBuilder | dsp/include/krate/dsp/systems/harmonic_model_builder.h | 3 | Reuse for smoothed harmonic model |
| ResidualSynthesizer | dsp/include/krate/dsp/processors/residual_synthesizer.h | 2 | Reuse unchanged for noise resynthesis |
| HarmonicOscillatorBank | dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h | 2 | Reuse unchanged for harmonic synthesis |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Already in use for parameter smoothing |
| StftWindowConfig | plugins/innexus/src/dsp/dual_stft_config.h | Plugin-local | Extend with low-latency config constants |
| ResidualFrame | dsp/include/krate/dsp/processors/residual_types.h | 2 | Reuse as output format from SpectralCoringEstimator |
| HarmonicFrame | dsp/include/krate/dsp/processors/harmonic_types.h | 2 | Reuse as pipeline output |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 DSP processors (where SpectralCoringEstimator goes)
- [x] `plugins/innexus/src/dsp/` - Plugin-local DSP (where LiveAnalysisPipeline goes)
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter IDs (500-599 range is free)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new types have unique names not found anywhere in the codebase. The SpectralCoringEstimator is in the `Krate::DSP` namespace (shared library), and the LiveAnalysisPipeline is in the `Innexus` namespace (plugin-local). No name collisions possible.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| STFT | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType windowType)` | Yes |
| STFT | pushSamples | `void pushSamples(const float* samples, size_t count)` | Yes |
| STFT | canAnalyze | `[[nodiscard]] bool canAnalyze() const noexcept` | Yes |
| STFT | analyze | `void analyze(SpectralBuffer& output)` | Yes |
| STFT | fftSize | `[[nodiscard]] size_t fftSize() const noexcept` | Yes |
| STFT | hopSize | `[[nodiscard]] size_t hopSize() const noexcept` | Yes |
| YinPitchDetector | prepare | `void prepare(double sampleRate)` | Yes |
| YinPitchDetector | detect | `F0Estimate detect(const float* audio, size_t numSamples)` | Yes |
| YinPitchDetector | reset | `void reset()` | Yes |
| PartialTracker | prepare | `void prepare(size_t fftSize, double sampleRate)` | Yes |
| PartialTracker | processFrame | `void processFrame(const SpectralBuffer& spectrum, const F0Estimate& f0, size_t fftSize, float sampleRate)` | Yes |
| PartialTracker | getPartials | `[[nodiscard]] const std::array<Partial, kMaxPartials>& getPartials() const noexcept` | Yes |
| PartialTracker | getActiveCount | `[[nodiscard]] int getActiveCount() const noexcept` | Yes |
| HarmonicModelBuilder | prepare | `void prepare(double sampleRate)` | Yes |
| HarmonicModelBuilder | setHopSize | `void setHopSize(int hopSize)` | Yes |
| HarmonicModelBuilder | build | `HarmonicFrame build(const std::array<Partial, kMaxPartials>& partials, int activeCount, const F0Estimate& f0, float inputRms)` | Yes |
| PreProcessingPipeline | prepare | `void prepare(double sampleRate)` | Yes |
| PreProcessingPipeline | processBlock | `void processBlock(float* data, size_t numSamples)` | Yes |
| SpectralBuffer | getMagnitude | `[[nodiscard]] float getMagnitude(size_t bin) const` | Yes |
| SpectralBuffer | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| ResidualSynthesizer | loadFrame | `void loadFrame(const ResidualFrame& frame, float brightness, float transientEmphasis)` | Yes |
| OnePoleSmoother | configure | `void configure(float timeMs, float sampleRate)` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value)` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/stft.h` - STFT class
- [x] `dsp/include/krate/dsp/processors/yin_pitch_detector.h` - YinPitchDetector class
- [x] `dsp/include/krate/dsp/processors/partial_tracker.h` - PartialTracker class
- [x] `dsp/include/krate/dsp/systems/harmonic_model_builder.h` - HarmonicModelBuilder class
- [x] `dsp/include/krate/dsp/processors/residual_analyzer.h` - ResidualAnalyzer (reference for spectral coring)
- [x] `dsp/include/krate/dsp/processors/residual_synthesizer.h` - ResidualSynthesizer class
- [x] `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` - HarmonicOscillatorBank class
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer class
- [x] `dsp/include/krate/dsp/processors/harmonic_types.h` - HarmonicFrame struct
- [x] `dsp/include/krate/dsp/processors/residual_types.h` - ResidualFrame struct
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `plugins/innexus/src/dsp/pre_processing_pipeline.h` - PreProcessingPipeline class
- [x] `plugins/innexus/src/dsp/dual_stft_config.h` - StftWindowConfig struct
- [x] `plugins/innexus/src/dsp/sample_analyzer.h` - SampleAnalyzer class
- [x] `plugins/innexus/src/dsp/sample_analysis.h` - SampleAnalysis struct
- [x] `plugins/innexus/src/processor/processor.h` - Processor class
- [x] `plugins/innexus/src/plugin_ids.h` - ParameterIds enum

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| YinPitchDetector | Constructor takes windowSize, but prepare() takes sampleRate (double not float) | `YinPitchDetector yin(windowSize); yin.prepare(sampleRate_as_double);` |
| PartialTracker | processFrame takes fftSize and sampleRate as separate args (float, not double) | `tracker.processFrame(spectrum, f0, fftSize, static_cast<float>(sampleRate))` |
| HarmonicModelBuilder | setHopSize takes int (not size_t) | `modelBuilder.setHopSize(static_cast<int>(hopSize))` |
| PreProcessingPipeline | processBlock modifies data IN-PLACE | Copy sidechain data to buffer first, then process |
| STFT | pushSamples takes const float* and size_t count | Correct pointer arithmetic for mono data |
| Processor state version | Current version is 2 (M2), must write 3 for M3 | `streamer.writeInt32(3)` |
| OnePoleSmoother | Uses `snapTo()` not `snap()` or `setValue()` | `smoother.snapTo(value)` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

No new Layer 0 utilities needed. All required math/conversion functions already exist.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| stereoToMonoDownmix (in Processor) | One-liner loop, context-specific to sidechain handling |
| detectSourceSwitch (in Processor) | Reads processor-local state, 2 consumers max |

**Decision**: No new Layer 0 extractions. All new logic is either in SpectralCoringEstimator (Layer 2) or LiveAnalysisPipeline (plugin-local).

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Analysis pipeline is feed-forward; no output-to-input feedback |
| **Data parallelism width** | LOW | Single analysis pipeline instance; parallelism is within components (FFT, partial tracker) |
| **Branch density in inner loop** | MEDIUM | Harmonic bin identification has per-bin conditionals in spectral coring |
| **Dominant operations** | FFT + arithmetic | FFT already SIMD-optimized via pffft; remaining is bin-level arithmetic |
| **Current CPU budget vs expected usage** | 5% budget vs ~3-4% expected | Existing pipeline components have been profiled; headroom exists |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The computationally expensive parts (FFT via pffft, oscillator bank via Gordon-Smith phasor) are already SIMD-optimized or use highly efficient scalar algorithms. The new SpectralCoringEstimator's inner loop (iterating over ~513 bins with harmonic proximity checks) is branch-heavy and has narrow parallelism. The overall pipeline is expected to be well within the 5% CPU budget without SIMD optimization of the new code.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip long STFT in low-latency mode | ~30% CPU reduction vs high-precision | LOW | YES (inherent to design) |
| Early-out when sidechain silent | ~95% CPU reduction for idle sidechain | LOW | YES |
| Skip spectral coring when residual level = 0 | ~10% CPU reduction | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Plugin-local (LiveAnalysisPipeline) + Layer 2 (SpectralCoringEstimator)

**Related features at same layer** (from spec Forward Reusability Consideration):
- Phase 21 (Multi-Source Blending): Multiple live analysis pipelines simultaneously
- Phase 13 (Freeze/Morph): Captures snapshots from live or sample harmonic models

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SpectralCoringEstimator | HIGH | Multi-Source Blending (Phase 21) | Keep in shared DSP library (already placed at Layer 2) |
| LiveAnalysisPipeline | HIGH | Multi-Source Blending (Phase 21) | Keep instantiable; designed for multiple instances |
| InputSource enum pattern | LOW | Only this plugin | Keep local |

### Detailed Analysis (for HIGH potential items)

**SpectralCoringEstimator** provides:
- Lightweight residual estimation from STFT + HarmonicFrame
- Zero additional latency (no resynthesis needed)
- Same ResidualFrame output as full ResidualAnalyzer

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Phase 21 (Multi-Source Blending) | YES | Each live source needs its own lightweight residual estimator |
| Phase 13 (Freeze/Morph) | NO | Freeze captures existing frames, does not re-analyze |

**Recommendation**: Keep SpectralCoringEstimator in shared DSP library at Layer 2. Already placed there by design.

**LiveAnalysisPipeline** provides:
- Real-time audio-to-HarmonicFrame conversion
- Configurable latency mode
- Incremental sample feeding

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Phase 21 (Multi-Source Blending) | YES | One instance per live source |
| Phase 13 (Freeze/Morph) | MAYBE | Could use to re-analyze frozen region |

**Recommendation**: Keep in plugin-local code but design as instantiable (no singletons, no static state).

### Decision Log

| Decision | Rationale |
|----------|-----------|
| SpectralCoringEstimator in shared DSP library | Phase 21 will need it; placing at Layer 2 now avoids future refactoring |
| LiveAnalysisPipeline plugin-local | Depends on plugin-local PreProcessingPipeline and StftWindowConfig |
| No shared base class for analysis pipelines | Only two consumers (SampleAnalyzer, LiveAnalysisPipeline) with different threading models |

### Review Trigger

After implementing **Phase 21 (Multi-Source Blending)**, review this section:
- [ ] Does Phase 21 need multiple LiveAnalysisPipeline instances? Confirm instantiability works
- [ ] Does Phase 21 use SpectralCoringEstimator? Confirm API is sufficient
- [ ] Any duplicated code between live and sample analysis? Consider shared pipeline config

## Project Structure

### Documentation (this feature)

```text
specs/117-live-sidechain-mode/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- api-contracts.md
+-- spec.md              # Feature specification
+-- checklists/          # Task checklists (Phase 2)
```

### Source Code (repository root)

```text
dsp/include/krate/dsp/processors/
+-- spectral_coring_estimator.h   # NEW: Layer 2 spectral coring residual estimator

dsp/tests/unit/processors/
+-- spectral_coring_estimator_tests.cpp  # NEW: Unit tests

plugins/innexus/src/
+-- plugin_ids.h                   # MODIFIED: Add InputSource, LatencyMode, param IDs 500-501
+-- dsp/
|   +-- dual_stft_config.h        # MODIFIED: Add low-latency window config
|   +-- live_analysis_pipeline.h   # NEW: Live analysis pipeline declaration
|   +-- live_analysis_pipeline.cpp # NEW: Live analysis pipeline implementation
+-- processor/
|   +-- processor.h                # MODIFIED: Add sidechain fields, LiveAnalysisPipeline member
|   +-- processor.cpp              # MODIFIED: Sidechain bus, process() flow, state v3
+-- controller/
|   +-- controller.cpp             # MODIFIED: Register new parameters, state v3
+-- parameters/
    +-- innexus_params.h           # MODIFIED: Add sidechain param handling

plugins/innexus/tests/unit/processor/
+-- live_analysis_pipeline_tests.cpp    # NEW: Pipeline unit tests
+-- sidechain_integration_tests.cpp     # NEW: Integration tests
```

**Structure Decision**: Follows existing monorepo pattern. New DSP class in shared library (Layer 2), new pipeline class in plugin-local DSP directory. No new directories created.

## Complexity Tracking

No constitution violations. All design decisions align with existing principles.
