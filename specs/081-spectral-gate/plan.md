# Implementation Plan: Spectral Gate

**Branch**: `081-spectral-gate` | **Date**: 2026-01-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/081-spectral-gate/spec.md`

## Summary

Per-bin noise gate that passes frequency components above a magnitude threshold while creating spectral holes below threshold. Uses STFT analysis with independent per-bin envelope tracking for attack/release, expansion ratio control, optional frequency range limiting, and spectral smearing for reduced musical noise artifacts. Layer 2 processor reusing existing FFT/STFT infrastructure from SpectralMorphFilter.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: FFT (fft.h), STFT (stft.h), SpectralBuffer (spectral_buffer.h), OnePoleSmoother (smoother.h), dbToGain (db_utils.h)
**Storage**: N/A
**Testing**: Catch2 (per project standard)
**Target Platform**: Windows, macOS, Linux (cross-platform VST3)
**Project Type**: KrateDSP shared library (Layer 2 processor)
**Performance Goals**: < 0.5% CPU at 44.1kHz with 1024 FFT size (SC-007)
**Constraints**: Real-time safe (noexcept process, allocations in prepare() only), COLA-compliant synthesis
**Scale/Scope**: Single processor class, ~400-600 lines of implementation

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in process() or processBlock()
- [x] No locks, mutexes, or blocking primitives in audio thread
- [x] All buffers pre-allocated in prepare()
- [x] noexcept on all processing methods

**Required Check - Principle III (Modern C++ Standards):**
- [x] C++20 features where applicable
- [x] RAII for resource management
- [x] Smart pointers where needed (none for this implementation - stack allocations)
- [x] constexpr for constants

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor depends only on Layer 0-1 components
- [x] Uses Layer 1: STFT, OverlapAdd, SpectralBuffer, FFT
- [x] Uses Layer 0: db_utils.h, window_functions.h, math_constants.h

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

**Classes/Structs to be created**: SpectralGate

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SpectralGate | `grep -r "class SpectralGate" dsp/ plugins/` | No | Create New |
| BinEnvelope | `grep -r "BinEnvelope\|PerBinEnvelope" dsp/ plugins/` | No | Not needed - use inline per-bin state vectors |

**Utility Functions to be created**: None planned (will use existing db_utils.h)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" dsp/` | Yes | db_utils.h | Reuse |
| gainToDb | `grep -r "gainToDb" dsp/` | Yes | db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FFT | dsp/include/krate/dsp/primitives/fft.h | 1 | Underlying FFT for STFT (via STFT class) |
| STFT | dsp/include/krate/dsp/primitives/stft.h | 1 | Streaming spectral analysis with windowing |
| OverlapAdd | dsp/include/krate/dsp/primitives/stft.h | 1 | COLA-compliant synthesis |
| SpectralBuffer | dsp/include/krate/dsp/primitives/spectral_buffer.h | 1 | Complex spectrum storage, magnitude/phase access |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (threshold, ratio) |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Threshold dB to linear conversion |
| kDenormalThreshold | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing |
| WindowType | dsp/include/krate/dsp/core/window_functions.h | 0 | Hann window for STFT |
| kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Coefficient calculations |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (db_utils.h exists, will reuse)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (STFT, SpectralBuffer exist, will reuse)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 (SpectralMorphFilter reference, no SpectralGate)
- [x] `specs/_architecture_/` - Component inventory (SpectralGate not listed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing SpectralGate class in codebase. All planned types are unique. Will reuse existing Layer 0-1 components without modification. Architecture pattern follows SpectralMorphFilter.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| STFT | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | Yes |
| STFT | reset | `void reset() noexcept` | Yes |
| STFT | pushSamples | `void pushSamples(const float* input, size_t numSamples) noexcept` | Yes |
| STFT | canAnalyze | `[[nodiscard]] bool canAnalyze() const noexcept` | Yes |
| STFT | analyze | `void analyze(SpectralBuffer& output) noexcept` | Yes |
| OverlapAdd | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | Yes |
| OverlapAdd | reset | `void reset() noexcept` | Yes |
| OverlapAdd | synthesize | `void synthesize(const SpectralBuffer& input) noexcept` | Yes |
| OverlapAdd | samplesAvailable | `[[nodiscard]] size_t samplesAvailable() const noexcept` | Yes |
| OverlapAdd | pullSamples | `void pullSamples(float* output, size_t numSamples) noexcept` | Yes |
| SpectralBuffer | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| SpectralBuffer | reset | `void reset() noexcept` | Yes |
| SpectralBuffer | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| SpectralBuffer | getMagnitude | `[[nodiscard]] float getMagnitude(size_t bin) const noexcept` | Yes |
| SpectralBuffer | setMagnitude | `void setMagnitude(size_t bin, float magnitude) noexcept` | Yes |
| SpectralBuffer | getPhase | `[[nodiscard]] float getPhase(size_t bin) const noexcept` | Yes |
| SpectralBuffer | setPhase | `void setPhase(size_t bin, float phase) noexcept` | Yes |
| SpectralBuffer | data | `[[nodiscard]] Complex* data() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| dbToGain | - | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| flushDenormal | - | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/stft.h` - STFT and OverlapAdd classes
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain, flushDenormal, kDenormalThreshold

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SpectralBuffer | getMagnitude computes sqrt(real^2+imag^2) | Use directly, no manual computation needed |
| OnePoleSmoother | Uses 99% settling time semantics | 5ms smoothTimeMs = ~5ms to reach 99% of target |
| STFT | analyze() consumes hopSize samples | Call only when canAnalyze() is true |
| OverlapAdd | Normalization is built-in | COLA normalization automatic, don't double-apply |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| hzToBin(float hz, float sampleRate, size_t fftSize) | Frequency-to-bin conversion, useful for all spectral processors | spectral_utils.h | SpectralGate, future SpectralTilt, ResonatorBank |
| binToHz(size_t bin, float sampleRate, size_t fftSize) | Bin-to-frequency conversion, useful for all spectral processors | spectral_utils.h | SpectralGate, future spectral processors |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateGateGain | Single consumer, requires internal envelope state |
| applySmearing | Single consumer, specific to gating algorithm |

**Decision**: Consider extracting hzToBin/binToHz to spectral_utils.h if this pattern appears in 2+ spectral processors. For now, implement as inline helper functions in SpectralGate.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from FLT-ROADMAP.md):
- SpectralTilt (Phase 12.3) - different algorithm (brightness tilt), unlikely to share
- ResonatorBank (Phase 13.1) - different domain (resonant filters), unlikely to share
- Other spectral processors in Phases 12-18 - may share per-bin envelope pattern

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Per-bin envelope state vectors | MEDIUM | SpectralCompressor, SpectralExpander | Keep local / Extract after 2nd use |
| hzToBin/binToHz utilities | HIGH | SpectralTilt, any frequency-selective spectral processor | Extract to Layer 0 if needed by sibling |
| Boxcar smearing algorithm | LOW | Specific to gating | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep per-bin envelope local | First feature using this pattern - wait for second consumer |
| Keep smearing local | Specific to gating application, unlikely to be reused |
| Extract hz/bin conversion if needed | Clear utility but wait for concrete second use |

### Review Trigger

After implementing **SpectralTilt**, review this section:
- [ ] Does SpectralTilt need hzToBin/binToHz? -> Extract to spectral_utils.h
- [ ] Does SpectralTilt use per-bin processing? -> Document shared pattern

## Project Structure

### Documentation (this feature)

```text
specs/081-spectral-gate/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (API contract)
└── tasks.md             # Phase 2 output (NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── spectral_gate.h       # Main implementation (header-only)
└── tests/
    └── processors/
        └── spectral_gate_tests.cpp  # Unit tests
```

**Structure Decision**: Layer 2 processor in standard location. Header-only implementation following SpectralMorphFilter pattern for consistency and template flexibility.

## Complexity Tracking

No Constitution violations identified. Design follows established patterns from SpectralMorphFilter.

---

## Phase 0: Research (Completed)

See [research.md](research.md) for full research findings.

### Research Summary

#### Per-Bin Envelope Algorithm

**Decision**: Use simple asymmetric one-pole smoother per bin, computed at frame rate (not sample rate)

**Rationale**:
- Attack/release times specified in ms can be converted to frame-rate coefficients
- Frame rate = sampleRate / hopSize (e.g., 44100/512 = 86.13 frames/sec at 50% overlap with 1024 FFT)
- Coefficient formula: `coeff = exp(-1.0 / (timeMs * 0.001 * frameRate))`
- Rising magnitude uses attack coefficient, falling uses release coefficient

**Alternatives considered**:
- Sample-rate envelope with interpolation: More accurate but excessive CPU for 513 bins
- Block-averaged envelope: Less responsive, not suitable for transient material

#### Gate Gain Calculation

**Decision**: Downward expansion below threshold with configurable ratio

**Formula**:
```
For bin magnitude M (linear) and threshold T (linear):
  if M >= T: gain = 1.0 (unity)
  if M < T:
    dB_below = 20 * log10(T / M)
    expanded_dB = dB_below * ratio
    gain = T / (M * 10^(expanded_dB/20))
```

Simplified for efficiency:
```
gain = pow(M / T, 1.0 - ratio)  when M < T
gain = 1.0                       when M >= T
```

Where ratio=1 means no expansion (bypass), ratio=100 means near-infinite expansion (hard gate)

#### Spectral Smearing Algorithm

**Decision**: Boxcar averaging of gate gains (not magnitudes)

**Rationale**: Averaging the computed gains (not the raw magnitudes) ensures that loud neighbors can "pull up" quiet bins without altering the spectral balance of passing signals.

**Implementation**:
```
smearWidth = 1 + floor(smearAmount * (maxSmearBins - 1))
where maxSmearBins = fftSize / 64  (e.g., 16 bins for 1024 FFT)

For each bin:
  smoothedGain[bin] = average(gain[bin - halfWidth] ... gain[bin + halfWidth])
```

#### Frequency Range Implementation

**Decision**: Use bin indices computed from Hz, round to nearest bin center

**Formula**:
```
lowBin = round(lowHz * fftSize / sampleRate)
highBin = round(highHz * fftSize / sampleRate)

For each bin:
  if bin < lowBin || bin > highBin:
    output gain = 1.0 (passthrough)
  else:
    output gain = computed gate gain
```

---

## Phase 1: Design

### Data Model

See [data-model.md](data-model.md) for entity definitions.

### API Contract

See [contracts/spectral_gate.h](contracts/spectral_gate.h) for C++ interface.

### Quickstart Guide

See [quickstart.md](quickstart.md) for usage examples.

---

## Files Generated

| File | Purpose | Status |
|------|---------|--------|
| plan.md | This implementation plan | Complete |
| research.md | Phase 0 research findings | Complete |
| data-model.md | Entity definitions | Complete |
| contracts/spectral_gate.h | API contract | Complete |
| quickstart.md | Usage examples | Complete |
