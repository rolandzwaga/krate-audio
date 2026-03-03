# Implementation Plan: Identity Phase Locking for PhaseVocoderPitchShifter

**Branch**: `061-phase-locking` | **Date**: 2026-02-17 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/061-phase-locking/spec.md`

## Summary

Add identity phase locking (Laroche & Dolson, 1999) to the existing `PhaseVocoderPitchShifter` class in Layer 2 of the KrateDSP shared library. This modifies `processFrame()` to insert a 3-stage algorithm -- peak detection, region-of-influence assignment, and phase-locked propagation -- that preserves vertical phase coherence within spectral lobes, dramatically reducing the "phasiness" artifact inherent in standard phase vocoders. The implementation is a modification to an existing header-only class (not a new file), adds `setPhaseLocking()`/`getPhaseLocking()` API methods, and uses pre-allocated `std::array` storage with `uint16_t` bin indices for cache efficiency. Phase locking is enabled by default and can be toggled at runtime without clicks.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Existing `PhaseVocoderPitchShifter` class (Layer 2), `wrapPhase()` from `spectral_utils.h` (Layer 1), `SpectralBuffer` (Layer 1), `STFT`/`OverlapAdd` (Layer 1), `FormantPreserver` (Layer 2), `math_constants.h` (Layer 0)
**Storage**: N/A (no persistent state beyond member variables)
**Testing**: Catch2 (via `dsp_tests` target) *(Constitution Principle VIII: Testing Discipline)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform, no platform-specific code
**Project Type**: Shared library component (KrateDSP) -- modification to existing header-only class
**Performance Goals**: < 0.5% CPU per Layer 2 processor; phase-locked path should not be significantly slower than basic path (reduced per-bin arithmetic for non-peak bins offsets the peak detection + region assignment overhead)
**Constraints**: Zero heap allocations in process path, all new methods noexcept, real-time safe
**Scale/Scope**: Modification to single header file (~100 lines of new/modified code in `processFrame()`), new test file (~500-800 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-design check: PASSED**

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | DSP library component, no plugin code modified |
| II. Real-Time Audio Thread Safety | PASS | All new code noexcept, zero allocations in process path, pre-allocated `std::array` storage |
| III. Modern C++ Standards | PASS | `std::array`, `uint16_t`, `constexpr` constants, no raw new/delete |
| IV. SIMD & DSP Optimization | PASS | Scalar-first implementation per constitution; SIMD deferred to Phase 5 (see SIMD section) |
| V. VSTGUI Development | N/A | No UI component |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code, standard C++20 only |
| VII. Project Structure & Build System | PASS | Layer 2 location correct, test file added to CMakeLists.txt |
| VIII. Testing Discipline | PASS | Comprehensive test suite planned in dedicated `phase_locking_test.cpp`, test-first development |
| IX. Layered DSP Architecture | PASS | Layer 2 modification, depends only on Layer 0-1 components already included |
| X. DSP Processing Constraints | N/A | No new oversampling/saturation; phase vocoder pipeline unchanged structurally |
| XI. Performance Budgets | PASS | Well under Layer 2 budget (< 0.5% CPU); phase locking adds ~5-10% overhead to processFrame which is already well within budget |
| XII. Debugging Discipline | N/A | New feature addition, not debugging |
| XIII. Test-First Development | PASS | Tests written before implementation per workflow |
| XIV. Living Architecture Documentation | PASS | `layer-2-processors.md` update planned |
| XV. Pre-Implementation Research (ODR) | PASS | All searches performed, no conflicts (see below) |
| XVI. Honest Completion | PASS | Compliance table will be filled with real evidence |
| XVII. Framework Knowledge | N/A | No VSTGUI/VST3 framework interaction |
| XVIII. Spec Numbering | PASS | 061 is next available number |

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

**Classes/Structs to be created**: None (all additions are member variables and methods on the existing `PhaseVocoderPitchShifter` class)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| (no new types) | -- | -- | -- |

**Utility Functions to be created**: None (all methods are member functions on existing class)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `setPhaseLocking` | `grep -r "setPhaseLocking" dsp/ plugins/` | No | -- | Add as member method |
| `getPhaseLocking` | `grep -r "getPhaseLocking" dsp/ plugins/` | No | -- | Add as member method |

**Note**: The spec's "Existing Codebase Components" section confirmed no existing phase locking implementations. Search was performed across all code files in `dsp/` and `plugins/`. All matches were in specification/research documents only (`specs/harmonizer-roadmap.md`, `specs/_archive_/016-pitch-shifter/data-model.md`). No ODR risk for new member variables or methods since they are all added to the existing `PhaseVocoderPitchShifter` class.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PhaseVocoderPitchShifter` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L915-1190) | 2 | **Modify**: Add phase locking to `processFrame()`, new member state, new API methods |
| `wrapPhase()` | `dsp/include/krate/dsp/primitives/spectral_utils.h` (L172-176) | 1 | Phase wrapping for peak bin phase accumulation (already used by existing code) |
| `SpectralBuffer` | `dsp/include/krate/dsp/primitives/spectral_buffer.h` | 1 | `getMagnitude()`, `getPhase()`, `setCartesian()` -- already used by existing processFrame() |
| `magnitude_[]` | Existing member (`std::vector<float>`, allocated in `prepare()`) | -- | Peak detection operates on this analysis-domain array |
| `prevPhase_[]` | Existing member (`std::vector<float>`, allocated in `prepare()`) | -- | Toggle-to-basic re-initialization source (`synthPhase_[k] = prevPhase_[k]`) |
| `synthPhase_[]` | Existing member (`std::vector<float>`, allocated in `prepare()`) | -- | Phase accumulation for peak bins (existing), phase re-init for toggle (new) |
| `frequency_[]` | Existing member (`std::vector<float>`, allocated in `prepare()`) | -- | Instantaneous frequency for peak bin phase propagation (existing usage) |
| `FormantPreserver` | `dsp/include/krate/dsp/processors/formant_preserver.h` | 2 | **No modification**: Formant correction operates on phase-locked magnitudes (Step 3 of existing processFrame remains unchanged) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/processors/pitch_shift_processor.h` - Target file, no existing phase locking code
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - `wrapPhase()` reused as-is
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer API unchanged
- [x] `specs/_architecture_/layer-2-processors.md` - No conflicting component documentation

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new types are created. All additions are member variables and methods on the existing `PhaseVocoderPitchShifter` class. The new member variable names (`isPeak_`, `peakIndices_`, `numPeaks_`, `regionPeak_`, `phaseLockingEnabled_`, `wasLocked_`) were searched across the codebase and do not exist anywhere. No ODR risk.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `SpectralBuffer` | `getMagnitude` | `[[nodiscard]] float getMagnitude(std::size_t binIndex) const noexcept` | Yes -- read at L83-87 |
| `SpectralBuffer` | `getPhase` | `[[nodiscard]] float getPhase(std::size_t binIndex) const noexcept` | Yes -- read at L90-94 |
| `SpectralBuffer` | `setCartesian` | `void setCartesian(std::size_t binIndex, float real, float imag) noexcept` | Yes -- read at L131-137 |
| `spectral_utils.h` | `wrapPhase` | `inline float wrapPhase(float phase) noexcept` | Yes -- read at L172-176 |
| `math_constants.h` | `kPi` | (used in existing code, float constant) | Yes -- referenced in processFrame |
| `math_constants.h` | `kTwoPi` | (used in existing code, float constant) | Yes -- referenced in prepare() |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/pitch_shift_processor.h` - Full PhaseVocoderPitchShifter class read (L915-1190)
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - wrapPhase function verified
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer API verified
- [x] `dsp/include/krate/dsp/processors/formant_preserver.h` - FormantPreserver API verified (no changes needed)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `pitch_shift_processor.h` | Does NOT include `<array>` currently | Must add `#include <array>` for `std::array<bool, N>` and `std::array<uint16_t, N>` |
| `magnitude_[]` | Is a `std::vector<float>` not `std::array` | Access via `magnitude_[k]`, size is `numBins` after `prepare()` |
| `processFrame()` | Uses `srcBin` as `float` (fractional bin index) | `srcBin0 = static_cast<std::size_t>(srcBin)` for integer index |
| `synthesisSpectrum_` | Reset at start of Step 2 in processFrame | Must call `setCartesian()` for every bin that should have non-zero output |
| `wrapPhase()` | Uses while-loop (not modular arithmetic) | Works correctly but is O(n) for extreme phase values; fine for accumulated phases |
| `formantPreserve_` step | Recomputes `cos`/`sin` of `synthPhase_[k]` for ALL bins | When phase locking is enabled, non-peak bins MUST write their locked phase to `synthPhase_[k]` — do NOT store only in a local variable. The formant step uses `synthPhase_[k]` for all bins; stale values for non-peak bins will produce incorrect formant output. See RQ-2 for the rationale. The formant step code itself is unchanged. |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Peak detection loop | Inline in processFrame, operates on member `magnitude_[]`, only consumer is this class |
| Region assignment loop | Inline in processFrame, operates on member arrays, only consumer is this class |
| `setPhaseLocking()`/`getPhaseLocking()` | Simple setter/getter on member bool |

**Decision**: All phase locking logic stays inline in `processFrame()` of the `PhaseVocoderPitchShifter` class. The spec explicitly notes (Potential shared components): "keeping it inline in `processFrame()` is simpler and avoids premature abstraction." If Phase 2B (Spectral Transient Detection) or other future features need peak detection, extraction can happen then.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV requires evaluating SIMD viability.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO (within phase locking) | Peak detection, region assignment, and phase propagation are all forward-pass with no feedback. However, `synthPhase_[k]` accumulation for peak bins IS a frame-to-frame dependency (horizontal coherence). |
| **Data parallelism width** | ~2049 bins | Peak detection and region assignment process 2049 bins per frame; good width for SIMD |
| **Branch density in inner loop** | MEDIUM | Peak detection has one branch per bin (is peak?). Phase propagation has peak/non-peak branch per bin. |
| **Dominant operations** | Comparison (peak det), integer arithmetic (region assign), `cos`/`sin` (phase propagation) | cos/sin dominate CPU in the propagation step; these are present in both basic and locked paths |
| **Current CPU budget vs expected usage** | < 0.5% budget vs ~0.1-0.2% expected | processFrame is already well within budget; phase locking adds minimal overhead |

### SIMD Viability Verdict

**Verdict**: MARGINAL -- DEFER to Phase 5

**Reasoning**: The phase locking algorithm has good data parallelism width (2049 bins) and the peak detection loop is a simple 3-point comparison that vectorizes well with `_mm_cmpgt_ps` and `_mm_movemask_ps`. However: (1) the peak/non-peak branch in phase propagation makes SIMD awkward, (2) the `cos`/`sin` calls dominate CPU in both locked and basic paths and are already targeted by Phase 5A's vectorized math header, (3) the algorithm is already well under the CPU budget. The harmonizer roadmap Phase 5 SIMD Priority Matrix (Tier 4) explicitly lists peak detection as "Low Impact (don't hand-vectorize)" and identifies `cos`/`sin` vectorization (Tier 2) as the high-impact optimization. Scalar-first is the correct approach per Constitution Principle IV.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Shared rotation angle per region (already in algorithm) | Reduces per-bin arithmetic for non-peak bins | LOW (part of algorithm) | YES (inherent to identity phase locking) |
| `uint16_t` bin indices instead of `size_t` | ~4x smaller cache footprint for peak/region arrays | LOW | YES (already specified in spec) |
| Skip `wrapPhase` for non-peak bins | Non-peak bins use rotation angle, no accumulation to wrap | LOW | YES (inherent to algorithm) |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - Processors

**Related features at same layer** (from harmonizer roadmap):
- Phase 2B: Spectral Transient Detection & Phase Reset -- modifies the same `processFrame()` method
- FormantPreserver -- already at Layer 2, no modification needed
- Other spectral processors (SpectralGate, SpectralTilt, SpectralDistortion) -- different classes, no shared code needed

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Peak detection logic | MEDIUM | Phase 2B (transient detection may need peak info), Phase 5 (SIMD peak detection) | Keep inline for now; extract if Phase 2B needs peak data |
| Region-of-influence assignment | LOW | Only this algorithm uses region assignment | Keep inline |
| `isPeak_[]` / `peakIndices_[]` arrays | MEDIUM | Phase 2B may need peak information for transient-aware phase reset decisions | Keep as member variables; Phase 2B can access them directly since it modifies the same class |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep all phase locking logic inline in processFrame() | Only one consumer; spec recommends avoiding premature abstraction |
| Store locked phase in `synthPhase_[k]` for ALL bins (including non-peak bins) | Formant step (Step 3) reads `synthPhase_[k]` for every bin; non-peak bins must write `phaseForOutput` to `synthPhase_[k]` so Step 3 has the correct phase. This is safe: non-peak bins re-derive their locked phase each frame from the rotation angle, so overwriting `synthPhase_[k]` does not corrupt frame-to-frame accumulation. See RQ-2. |
| Use `uint16_t` for peakIndices_ and regionPeak_ | Spec clarification: 4x cache savings, bin indices never exceed 4096 |

### Review Trigger

After implementing **Phase 2B (Spectral Transient Detection & Phase Reset)**, review this section:
- [ ] Does Phase 2B need access to `isPeak_[]` or `peakIndices_[]`? If so, they're already member variables.
- [ ] Does Phase 2B's phase reset interact with phase locking? (Yes, per roadmap: "at transients, both peak and non-peak bins reset to analysis phase, temporarily overriding the phase locking logic")
- [ ] Any duplicated peak detection code? If Phase 2B needs its own peak analysis, consider extracting shared utility.

## Project Structure

### Documentation (this feature)

```text
specs/061-phase-locking/
├── plan.md              # This file
├── research.md          # Phase 0: Algorithm research and decisions
├── data-model.md        # Phase 1: Entity definitions (member variables, algorithm stages)
├── quickstart.md        # Phase 1: Usage guide and build instructions
├── contracts/
│   └── phase_locking_api.h  # Phase 1: Public API additions
└── tasks.md             # Phase 2: Implementation tasks (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── pitch_shift_processor.h     # MODIFIED: Add phase locking to PhaseVocoderPitchShifter
└── tests/
    └── unit/
        └── processors/
            └── phase_locking_test.cpp  # NEW: Dedicated phase locking test suite
```

### Files Modified

```text
dsp/include/krate/dsp/processors/pitch_shift_processor.h  # MODIFIED: Phase locking in PhaseVocoderPitchShifter
dsp/tests/CMakeLists.txt                                   # MODIFIED: Add phase_locking_test.cpp
specs/_architecture_/layer-2-processors.md                 # MODIFIED: Document phase locking capability
```

**Structure Decision**: This follows the established monorepo pattern for Layer 2 modifications. The existing `pitch_shift_processor.h` is modified in-place (header-only). A new dedicated test file `phase_locking_test.cpp` is created in the processors test directory, separate from the existing `pitch_shift_processor_test.cpp` which tests the basic functionality.

## Detailed Implementation Design

### Include Addition

The file `pitch_shift_processor.h` currently does not include `<array>`. This must be added alongside the existing standard library includes:

```cpp
#include <array>     // NEW: for std::array<bool, N>, std::array<uint16_t, N>
```

### Pre-Existing Code Comment Bug to Fix (I2)

The existing comment at `pitch_shift_processor.h:919` reads `// 25% overlap (4x)`, but with `kFFTSize = 4096` and `kHopSize = 1024`, the actual overlap is 75% (hop = 25% of window → overlap = 75%). The spec Assumptions section correctly states 75% overlap. This comment must be corrected to `// 75% overlap (4x)` during the include/constant changes step (T003/T006).

### New Constants

Add inside the `PhaseVocoderPitchShifter` class, adjacent to existing `kFFTSize` and `kHopSize`:

```cpp
static constexpr std::size_t kMaxBins = 4097;    // 8192/2+1 (max supported FFT)
static constexpr std::size_t kMaxPeaks = 512;    // Max detectable peaks per frame
```

### New Member Variables

Add in the private section of `PhaseVocoderPitchShifter`, after the existing formant preservation members:

```cpp
// Phase locking state (pre-allocated, zero runtime allocation)
std::array<bool, kMaxBins> isPeak_{};               // Peak flag per analysis bin
std::array<uint16_t, kMaxPeaks> peakIndices_{};     // Analysis-domain peak bin indices
std::size_t numPeaks_ = 0;                          // Number of detected peaks this frame
std::array<uint16_t, kMaxBins> regionPeak_{};       // Region-peak assignment per analysis bin
bool phaseLockingEnabled_ = true;                   // Phase locking toggle (default: enabled)
bool wasLocked_ = false;                            // Previous frame's locking state (for toggle-to-basic re-init)
```

**Memory footprint**: ~13.3 KB total per instance:
- `isPeak_`: 4097 bytes
- `peakIndices_`: 1024 bytes
- `regionPeak_`: 8194 bytes
- Other: ~10 bytes (numPeaks_, booleans)

### New Public API Methods

Add in the public section, near existing `setFormantPreserve()`/`getFormantPreserve()`:

```cpp
/// Enable or disable identity phase locking.
/// When disabled, behavior is identical to the pre-modification basic phase vocoder.
/// Phase locking is enabled by default.
void setPhaseLocking(bool enabled) noexcept {
    phaseLockingEnabled_ = enabled;
}

/// Returns the current phase locking state.
[[nodiscard]] bool getPhaseLocking() const noexcept {
    return phaseLockingEnabled_;
}
```

### Reset Method Modification

Add to the `reset()` method, after the existing `std::fill` calls:

```cpp
// Phase locking state
isPeak_.fill(false);
peakIndices_.fill(0);
numPeaks_ = 0;
regionPeak_.fill(0);
wasLocked_ = false;
```

### processFrame() Modification

The existing `processFrame()` has 3 steps:
1. Extract magnitude and compute instantaneous frequency (analysis)
2. Pitch shift by scaling frequencies and resampling spectrum (synthesis)
3. Apply formant preservation (if enabled)

The modification inserts phase locking logic between Step 1 and Step 2, and modifies Step 2. Step 3 (formant preservation) is adjusted to use the correct phase for each bin.

#### Insertion Point: After Step 1, Before Step 2

After the existing Step 1 loop and the formant envelope extraction, insert:

**Stage A: Peak Detection (analysis domain)**
```
// Phase locking: Detect peaks in analysis magnitude spectrum
if (phaseLockingEnabled_) {
    numPeaks_ = 0;
    isPeak_.fill(false);   // Note: only need to clear [0..numBins-1]

    for (std::size_t k = 1; k < numBins - 1 && numPeaks_ < kMaxPeaks; ++k) {
        if (magnitude_[k] > magnitude_[k - 1] && magnitude_[k] > magnitude_[k + 1]) {
            isPeak_[k] = true;
            peakIndices_[numPeaks_] = static_cast<uint16_t>(k);
            ++numPeaks_;
        }
    }
}
```

**Stage B: Region-of-Influence Assignment (analysis domain)**
```
// Assign each analysis bin to its nearest detected peak
if (phaseLockingEnabled_ && numPeaks_ > 0) {
    // Handle case of single peak: all bins assigned to it
    if (numPeaks_ == 1) {
        regionPeak_.fill(peakIndices_[0]);  // Note: only need [0..numBins-1]
    } else {
        // Forward scan: assign bins to peaks based on midpoint boundaries
        std::size_t peakIdx = 0;
        for (std::size_t k = 0; k < numBins; ++k) {
            // Move to next peak if we've passed the midpoint
            if (peakIdx + 1 < numPeaks_) {
                uint16_t midpoint = (peakIndices_[peakIdx] + peakIndices_[peakIdx + 1]) / 2;
                if (k > midpoint) {
                    ++peakIdx;
                }
            }
            regionPeak_[k] = peakIndices_[peakIdx];
        }
    }
}
```

**Toggle-to-basic re-initialization check:**
```
// Handle toggle from locked to basic: re-initialize non-peak synthPhase from analysis phase
if (wasLocked_ && !phaseLockingEnabled_) {
    for (std::size_t k = 0; k < numBins; ++k) {
        synthPhase_[k] = prevPhase_[k];
    }
}
wasLocked_ = phaseLockingEnabled_;
```

#### Modification to Step 2: Pitch Shift Loop (Two-Pass Approach)

The existing Step 2 loop iterates over synthesis bins `k`, computes `srcBin`, interpolates magnitude, accumulates `synthPhase_[k]`, and computes Cartesian output. When phase locking is enabled, this is replaced with a **two-pass approach** to resolve an ordering dependency: non-peak bins need the peak's `synthPhase_[synthPeakBin]` to compute their rotation angle, but the peak's synthesis bin may appear later in the iteration order (e.g., during pitch-up shifts).

> **Canonical pseudocode**: The authoritative algorithm flow is in `data-model.md` (Algorithm Flow section). The code blocks below are the detailed implementation rendering of the same algorithm.

**Two-pass structure when phase locking is enabled:**

```
// Pass 1: Process PEAK bins only (accumulate synthPhase_ for peaks)
for (std::size_t k = 0; k < numBins; ++k) {
    float srcBin = static_cast<float>(k) / pitchRatio;
    if (srcBin >= static_cast<float>(numBins - 1)) continue;

    std::size_t srcBinRounded = static_cast<std::size_t>(srcBin + 0.5f);
    if (srcBinRounded >= numBins) srcBinRounded = numBins - 1;

    if (!isPeak_[srcBinRounded]) continue;  // Skip non-peaks in Pass 1

    // Standard bin mapping and magnitude interpolation
    std::size_t srcBin0 = static_cast<std::size_t>(srcBin);
    std::size_t srcBin1 = srcBin0 + 1;
    if (srcBin1 >= numBins) srcBin1 = numBins - 1;
    float frac = srcBin - static_cast<float>(srcBin0);
    float mag = magnitude_[srcBin0] * (1.0f - frac) + magnitude_[srcBin1] * frac;
    shiftedMagnitude_[k] = mag;

    // Peak bin: standard horizontal phase propagation
    float freq = frequency_[srcBin0] * pitchRatio;
    synthPhase_[k] += freq;
    synthPhase_[k] = wrapPhase(synthPhase_[k]);

    float real = mag * std::cos(synthPhase_[k]);
    float imag = mag * std::sin(synthPhase_[k]);
    synthesisSpectrum_.setCartesian(k, real, imag);
}

// Pass 2: Process NON-PEAK bins (use peak phases from Pass 1)
for (std::size_t k = 0; k < numBins; ++k) {
    float srcBin = static_cast<float>(k) / pitchRatio;
    if (srcBin >= static_cast<float>(numBins - 1)) continue;

    // srcBin + 0.5f is standard round-half-up for positive values.
    // This is the synthesis→analysis mapping rounding, separate from the
    // equidistant-to-lower-peak rule used only in region boundary placement.
    std::size_t srcBinRounded = static_cast<std::size_t>(srcBin + 0.5f);
    if (srcBinRounded >= numBins) srcBinRounded = numBins - 1;

    if (isPeak_[srcBinRounded]) continue;  // Skip peaks in Pass 2

    // Standard bin mapping and magnitude interpolation
    std::size_t srcBin0 = static_cast<std::size_t>(srcBin);
    std::size_t srcBin1 = srcBin0 + 1;
    if (srcBin1 >= numBins) srcBin1 = numBins - 1;
    float frac = srcBin - static_cast<float>(srcBin0);
    float mag = magnitude_[srcBin0] * (1.0f - frac) + magnitude_[srcBin1] * frac;
    shiftedMagnitude_[k] = mag;

    // Non-peak bin: identity phase locking via rotation angle
    uint16_t analysisPeak = regionPeak_[srcBinRounded];

    // Find the synthesis bin corresponding to the analysis peak
    std::size_t synthPeakBin = static_cast<std::size_t>(
        static_cast<float>(analysisPeak) * pitchRatio + 0.5f);
    if (synthPeakBin >= numBins) synthPeakBin = numBins - 1;

    // Rotation angle: peak's synthesis phase minus peak's analysis phase
    float analysisPhaseAtPeak = prevPhase_[analysisPeak];
    float rotationAngle = synthPhase_[synthPeakBin] - analysisPhaseAtPeak;

    // Apply rotation to this bin's analysis phase (interpolated)
    float analysisPhaseAtSrc = prevPhase_[srcBin0] * (1.0f - frac)
                             + prevPhase_[srcBin1] * frac;
    float phaseForOutput = analysisPhaseAtSrc + rotationAngle;

    // Store in synthPhase_ for formant step compatibility
    synthPhase_[k] = phaseForOutput;

    float real = mag * std::cos(phaseForOutput);
    float imag = mag * std::sin(phaseForOutput);
    synthesisSpectrum_.setCartesian(k, real, imag);
}
```

**Basic path (when disabled or no peaks) remains as existing single-pass loop:**

```
// Basic path: standard per-bin phase accumulation (pre-modification behavior)
for (std::size_t k = 0; k < numBins; ++k) {
    float srcBin = static_cast<float>(k) / pitchRatio;
    if (srcBin >= static_cast<float>(numBins - 1)) continue;

    std::size_t srcBin0 = static_cast<std::size_t>(srcBin);
    std::size_t srcBin1 = srcBin0 + 1;
    if (srcBin1 >= numBins) srcBin1 = numBins - 1;
    float frac = srcBin - static_cast<float>(srcBin0);
    float mag = magnitude_[srcBin0] * (1.0f - frac) + magnitude_[srcBin1] * frac;
    shiftedMagnitude_[k] = mag;

    float freq = frequency_[srcBin0] * pitchRatio;
    synthPhase_[k] += freq;
    synthPhase_[k] = wrapPhase(synthPhase_[k]);

    float real = mag * std::cos(synthPhase_[k]);
    float imag = mag * std::sin(synthPhase_[k]);
    synthesisSpectrum_.setCartesian(k, real, imag);
}
```

**Why two passes**: During pitch-up shifts (pitchRatio > 1), a synthesis bin `k` maps to analysis bin `k/pitchRatio` (lower). The synthesis bin for an analysis peak is at `analysisPeak * pitchRatio` (higher). This means non-peak bins at lower synthesis indices may reference peak phases at higher synthesis indices that have not been computed yet in a single forward pass. The two-pass approach ensures all peak phases are available before any non-peak bin references them. See research.md RQ-5 for detailed analysis.

#### Modification to Step 3: Formant Preservation

The formant preservation step (Step 3) recomputes `cos(synthPhase_[k])`/`sin(synthPhase_[k])` for all bins. For this to work correctly with phase locking, `synthPhase_[k]` must hold the correct output phase for every bin — including non-peak bins.

**Decision (from RQ-2)**: Non-peak bins MUST write their locked phase to `synthPhase_[k]` in Pass 2: `synthPhase_[k] = phaseForOutput`. This is safe because non-peak bins do not use `synthPhase_[k]` for accumulation across frames — the next frame re-derives their locked phase from the rotation angle. So overwriting is harmless and ensures the formant step always has the correct phase without any modification to Step 3.

The formant preservation Step 3 code remains UNCHANGED -- it already uses `synthPhase_[k]` for all bins.

#### Edge Cases in processFrame()

1. **Zero peaks detected** (silence/noise): When `numPeaks_ == 0` and `phaseLockingEnabled_`, falls through to basic path (`phaseLockingEnabled_ && numPeaks_ > 0` is false). This satisfies FR-011.

2. **Peak count exceeds kMaxPeaks**: The peak detection loop condition includes `numPeaks_ < kMaxPeaks`, stopping collection at the limit. This satisfies FR-012.

3. **Unity pitch ratio**: The existing `process()` method short-circuits to `processUnityPitch()` when `pitchRatio ~ 1.0`, bypassing `processFrame()` entirely. Phase locking is not invoked. This is correct per spec edge case.

4. **Boundary bins (DC, Nyquist)**: Bins 0 and numBins-1 are excluded from peak detection (loop starts at 1, ends at numBins-2). They are assigned to the nearest peak via region-of-influence assignment.

### Test Strategy

A dedicated test file `dsp/tests/unit/processors/phase_locking_test.cpp` will be created with the following test categories:

**Category 1: Peak Detection (SC-003, FR-002)**
- Single sinusoid at 440 Hz: expect exactly 1 peak near bin 40-41
- Sawtooth at 100 Hz: expect ~220 peaks (harmonics below Nyquist), +/- 5%. Use a steady-state buffer (sufficient STFT frames) to ensure Hann windowing leakage does not cause adjacent harmonics to fail the 3-point local-maximum test; document the actual expected count if windowing reduces it.
- Silence (all zeros): expect 0 peaks
- Maximum peaks: verify kMaxPeaks limit is respected
- **Equal-magnitude plateau (G3)**: construct two adjacent bins with identical magnitude surrounded by lower neighbors; verify neither bin is flagged as a peak (strict inequality `>`, not `>=`).

**Category 2: Region-of-Influence Assignment (SC-004, FR-003)**
- Verify 100% bin coverage: assert directly that `regionPeak_[k]` holds a valid peak-index for every `k` in [0, numBins-1] (use a test accessor or `verifyRegionCoverage()` helper -- do NOT rely solely on output energy gaps, which cannot detect zero-magnitude bins). If no direct accessor is added, assert that the sum of per-peak assigned bin counts equals numBins exactly.
- Verify midpoint boundary placement between adjacent peaks
- Single peak: all bins assigned to that peak
- Equidistant bin: assigned to lower-frequency peak

**Category 3: Spectral Quality (SC-001, SC-002, FR-001)**
- Single sinusoid pitch-shifted +3 semitones: measure energy concentration in 3-bin window (target: >= 90% with locking vs < 70% without)
- Multi-harmonic (sawtooth) pitch-shifted: verify >= 95% of harmonics preserved as local maxima
- Compare spectral focus between locked and basic paths

**Category 4: Backward Compatibility (SC-005, FR-006, FR-007)**
- Phase locking disabled: output must be sample-accurate identical to pre-modification code. All comparisons MUST use `Approx().margin(1e-6f)` (not exact equality) to pass on all platforms; see SC-005 clarification in spec.md.
- API: verify `setPhaseLocking()`/`getPhaseLocking()` work correctly
- Default state: phase locking enabled by default

**Category 5: Toggle Behavior (SC-006, FR-008)**
- Toggle locked -> basic during continuous processing: verify no clicks (amplitude discontinuity check)
- Toggle basic -> locked during continuous processing: verify clean transition
- Multiple rapid toggles: verify stability

**Category 6: Extended Processing (SC-008)**
- Process 10 seconds of audio at 44.1 kHz with phase locking enabled for each pitch shift: -12, -7, -3, +3, +7, +12 semitones
- Verify no NaN, no inf, no crashes

**Category 7: Formant Preservation Compatibility (FR-015)**
- Enable both phase locking and formant preservation: verify no artifacts
- Compare output with formant preservation on/off while phase locking is on

**Category 8: Real-Time Safety (SC-007, FR-009, FR-016)**
- Code inspection: verify zero heap allocations in processFrame
- Verify all new methods are noexcept

### CMakeLists.txt Changes

Add to `dsp/tests/CMakeLists.txt`:

1. In the `add_executable(dsp_tests ...)` source list, under `# Layer 2: Processors`, add:
   ```
   unit/processors/phase_locking_test.cpp
   ```

2. In the `set_source_files_properties(...)` block for `-fno-fast-math`, add:
   ```
   unit/processors/phase_locking_test.cpp
   ```

### Build Verification

```bash
# Configure
"C:/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release

# Build DSP tests
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run phase locking tests only
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Phase Locking*"

# Run ALL tests to verify no regressions
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

## Complexity Tracking

No constitution violations. No complexity tracking entries needed.

## Post-Design Constitution Re-Check

*Re-checked after Phase 1 design completion.*

| Check | Result |
|-------|--------|
| All new methods noexcept? | Yes -- `setPhaseLocking()`, `getPhaseLocking()` are both noexcept |
| Zero heap allocations in process path? | Yes -- all new arrays are `std::array` (stack/member), no `std::vector::push_back` or `new` |
| Layer 2 deps only (layers 0-1)? | Yes -- uses existing Layer 0 (`math_constants.h`) and Layer 1 (`spectral_utils.h`) already included |
| No ODR conflicts? | Yes -- all additions are member variables/methods on existing class |
| Test-first planned? | Yes -- `phase_locking_test.cpp` is in the plan, tests written before implementation |
| Architecture docs updated? | Yes -- `layer-2-processors.md` update is in the plan |
| Cross-platform? | Yes -- standard C++20 only, no platform-specific code |
| Formant preservation compatible? | Yes -- formant step uses `synthPhase_[k]` which is correctly set for all bins |
| Backward compatible when disabled? | Yes -- when `phaseLockingEnabled_ == false`, code path is identical to pre-modification |
| `<array>` include added? | Yes -- noted as required include addition |

**Post-design gate: PASSED**
