# Implementation Plan: Spectral Transient Detector

**Branch**: `062-spectral-transient-detector` | **Date**: 2026-02-17 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/062-spectral-transient-detector/spec.md`
**Roadmap**: [harmonizer-roadmap.md, Phase 2B](../harmonizer-roadmap.md)

## Summary

Implement a spectral flux-based transient detector (`SpectralTransientDetector`) as a Layer 1 primitive in the KrateDSP shared library, and integrate it with `PhaseVocoderPitchShifter` for transient-aware phase reset. The detector computes half-wave rectified spectral flux per frame (Duxbury et al. 2002, Dixon 2006), compares it against an adaptive threshold derived from an exponential moving average, and flags transient onsets. When integrated, detected transients trigger synthesis phase reset to analysis phases, preserving transient sharpness in pitch-shifted audio. This is Phase 2B of the harmonizer development roadmap.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Standard library only (`<vector>`, `<cstddef>`, `<cmath>`, `<algorithm>`, `<cassert>`). Integration target: `PhaseVocoderPitchShifter` (Layer 2).
**Storage**: N/A (in-memory frame-rate processing)
**Testing**: Catch2 (via `dsp_tests` target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library (KrateDSP monorepo)
**Performance Goals**: < 0.01% CPU per frame at 44.1kHz/4096-FFT (SC-005). Single linear pass over magnitude array, no transcendental math.
**Constraints**: Real-time safe `detect()` path (no allocations, exceptions, locks, I/O). All memory allocated in `prepare()`.
**Scale/Scope**: ~80 lines of implementation code + ~300 lines of tests. One new header, two new test files, one modified header, two modified CMakeLists.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASS)**:

**Required Check - Principle II (Real-Time Safety):**
- [x] `detect()` performs no memory allocation
- [x] `detect()` is `noexcept`
- [x] No locks, exceptions, or I/O in detection path
- [x] All buffers allocated in `prepare()`

**Required Check - Principle IX (Layered Architecture):**
- [x] SpectralTransientDetector is Layer 1 (depends only on stdlib, no Layer 1+ dependencies)
- [x] Integration into PhaseVocoderPitchShifter is Layer 2 depending on Layer 1 (valid)
- [x] No circular dependencies introduced

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle IV (SIMD):**
- [x] SIMD viability analysis completed below
- [x] Scalar-first workflow planned

**Post-Design Re-Check (PASS)**:
- [x] All design decisions comply with constitution
- [x] No platform-specific code introduced (Principle VI)
- [x] Header-only implementation follows existing pattern
- [x] Phase reset toggle independent of phase locking (no coupling violation)

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `SpectralTransientDetector`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `SpectralTransientDetector` | `grep -r "SpectralTransient" dsp/ plugins/` | No | Create New |
| `SpectralTransientDetector` | `grep -r "spectral_transient" dsp/ plugins/` | No | Create New |
| `TransientDetector` (conflict check) | `grep -r "class TransientDetector" dsp/ plugins/` | Yes -- `dsp/include/krate/dsp/processors/transient_detector.h:34` | No conflict (different name, different layer, different algorithm) |

**Utility Functions to be created**: None. All logic is encapsulated in the class.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PhaseVocoderPitchShifter` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | 2 | Integration target: add `SpectralTransientDetector` member, phase reset logic in `processFrame()` |
| `magnitude_[]` array | `pitch_shift_processor.h` (PhaseVocoderPitchShifter member) | 2 | Direct input to `detect()` -- already computed per frame, zero redundant computation |
| `synthPhase_[]` array | `pitch_shift_processor.h` (PhaseVocoderPitchShifter member) | 2 | Reset target: `synthPhase_[k] = prevPhase_[k]` on transient detection |
| `prevPhase_[]` array | `pitch_shift_processor.h` (PhaseVocoderPitchShifter member) | 2 | Source for phase reset (analysis phases from current frame) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no spectral transient detection)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no spectral transient detection)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 (existing `TransientDetector` is time-domain, different name)
- [x] `specs/_architecture_/` - Component inventory (no spectral transient detection listed)
- [x] `specs/harmonizer-roadmap.md` - Confirmed as planned Phase 2B component

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The class name `SpectralTransientDetector` is unique in the codebase (confirmed by grep). The existing `TransientDetector` is a completely different class (time-domain envelope derivative analysis, Layer 2, inherits `ModulationSource`) with no name collision. No utility functions are being added to shared headers.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `PhaseVocoderPitchShifter` | `magnitude_` | `std::vector<float> magnitude_` (private member) | Yes |
| `PhaseVocoderPitchShifter` | `synthPhase_` | `std::vector<float> synthPhase_` (private member) | Yes |
| `PhaseVocoderPitchShifter` | `prevPhase_` | `std::vector<float> prevPhase_` (private member) | Yes |
| `PhaseVocoderPitchShifter` | `kFFTSize` | `static constexpr std::size_t kFFTSize = 4096` | Yes |
| `PhaseVocoderPitchShifter` | `processFrame()` | `void processFrame(float pitchRatio) noexcept` (private) | Yes |
| `PhaseVocoderPitchShifter` | `setPhaseLocking()` | `void setPhaseLocking(bool enabled) noexcept` | Yes |
| `PhaseVocoderPitchShifter` | `phaseLockingEnabled_` | `bool phaseLockingEnabled_ = true` (private member) | Yes |
| `SpectralBuffer` | `getMagnitude()` | `[[nodiscard]] float getMagnitude(size_t bin) const noexcept` | Yes |
| `PitchShiftProcessor` | `pImpl_->phaseVocoderShifter` | `PhaseVocoderPitchShifter phaseVocoderShifter` (in Impl) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/pitch_shift_processor.h` - PhaseVocoderPitchShifter class (1566 lines)
- [x] `dsp/include/krate/dsp/processors/transient_detector.h` - Existing time-domain TransientDetector
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer API
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - wrapPhase and spectral utilities
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi constants

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `PhaseVocoderPitchShifter` | `prevPhase_[k]` stores the analysis phase AFTER update (line 1134: `prevPhase_[k] = phase`), so at the point of phase reset it already holds the current frame's analysis phase | `synthPhase_[k] = prevPhase_[k]` is correct for phase reset |
| `PhaseVocoderPitchShifter` | Phase locking toggle-to-basic re-init is at line 1196-1201 and checks `wasLocked_ && !phaseLockingEnabled_` | Phase reset insertion must be BEFORE this check |
| `PhaseVocoderPitchShifter` | `formantPreserve_` check at line 1149 is labeled "Step 1b" | Phase reset should be "Step 1b-reset" to avoid renumbering |
| `SpectralBuffer::getMagnitude()` | Triggers lazy polar conversion on first call after Cartesian set | The `magnitude_[k]` array in `PhaseVocoderPitchShifter` bypasses this (computed directly from `analysisSpectrum_.getMagnitude(k)`) |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Half-wave rectified flux computation | Self-contained in `detect()`, ~10 lines, single consumer |
| EMA update | Two-line formula, only used in `detect()` |

**Decision**: No Layer 0 extraction needed. The entire algorithm is self-contained within the class. The spectral flux computation could theoretically be extracted, but it has only one consumer and no benefit from sharing at this time. If a second spectral analysis feature needs it (per spec's "Forward Reusability Consideration"), it can be extracted then.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV requires evaluating SIMD viability.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Each bin is independent. Running average has frame-to-frame dependency but operates on the scalar flux sum, not per-bin. |
| **Data parallelism width** | 2049 bins (4096 FFT) | Excellent width for SIMD -- processes all bins in a single linear pass |
| **Branch density in inner loop** | LOW | One `max(0, diff)` per bin (branchless with `_mm_max_ps`) |
| **Dominant operations** | Arithmetic (subtract, max, add) | No transcendental math. Pure floating-point add/sub/max. |
| **Current CPU budget vs expected usage** | Budget: < 0.1% (Layer 1). Expected: << 0.01% | Single linear pass over 2049 floats with 3 ops each. Approximately 6000 FLOPs per frame. At 44.1kHz / 1024 hop = ~43 frames/sec = ~258K FLOPs/sec. Negligible. |

### SIMD Viability Verdict

**Verdict**: MARGINAL -- DEFER

**Reasoning**: The algorithm has excellent data parallelism (2049 independent bins) and the inner loop is branchless, making it theoretically SIMD-friendly. However, the absolute CPU cost is negligible (< 0.01% by design -- SC-005). The scalar implementation will easily meet the performance target. SIMD optimization would add complexity for no measurable user-facing benefit. This can be revisited as part of Phase 5 (SIMD optimization pass) in the harmonizer roadmap if profiling reveals it as a bottleneck, which is extremely unlikely.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out on zero numBins | Negligible | LOW | YES (defensive coding) |
| Prefetch hint for magnitude array | Negligible (fits in L1 cache) | LOW | NO |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 - Primitives

**Related features at same layer** (from harmonizer roadmap):
- PitchTracker (Phase 3): Also Layer 1, wraps PitchDetector with smoothing/hysteresis. No shared code.
- SpectralBuffer (existing): Layer 1 spectral storage. Input source for this detector.

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `SpectralTransientDetector` | MEDIUM | Spectral freeze (onset preservation), spectral gating (onset-aligned), granular processing (onset triggers) | Keep as standalone class, document in architecture |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Self-contained class (no extracted utilities) | Single consumer pattern. Extraction deferred until 2nd use per YAGNI. |
| `std::vector<float>` for prevMagnitudes (not `std::array`) | Bin count varies with FFT size (257 to 4097). Static array would waste 16KB for small FFTs. |
| Default phase reset OFF | Backward compatibility. Existing users of PhaseVocoderPitchShifter see no behavior change. |

### Review Trigger

After implementing **Phase 3: PitchTracker**, review this section:
- [ ] Does PitchTracker need spectral flux or similar onset analysis? (Unlikely -- operates on time-domain)
- [ ] Any duplicated buffer management patterns? Consider shared pattern documentation.

After implementing **Phase 4: HarmonizerEngine**, review this section:
- [ ] Does the engine need onset detection for voice onset alignment?
- [ ] Any shared spectral analysis utilities to extract?

## Project Structure

### Documentation (this feature)

```text
specs/062-spectral-transient-detector/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0: Algorithm research and design decisions
├── data-model.md        # Phase 1: Entity model and state transitions
├── quickstart.md        # Phase 1: Build/test/usage guide
├── contracts/           # Phase 1: API contracts
│   ├── spectral_transient_detector.h   # Detector API contract
│   └── phase_reset_integration.md      # Integration contract
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── primitives/
│   │   └── spectral_transient_detector.h   # NEW: Layer 1 primitive
│   └── processors/
│       └── pitch_shift_processor.h         # MODIFIED: Phase reset integration
├── CMakeLists.txt                          # MODIFIED: Add header to list
└── tests/
    ├── CMakeLists.txt                      # MODIFIED: Add test files
    └── unit/
        ├── primitives/
        │   └── spectral_transient_detector_test.cpp  # NEW: Standalone tests
        └── processors/
            └── phase_reset_test.cpp                  # NEW: Integration tests
```

**Structure Decision**: This follows the established KrateDSP monorepo pattern. The detector is a header-only Layer 1 primitive. Integration into the Phase Vocoder happens in the existing Layer 2 header. Tests are split into standalone detector tests (primitives/) and integration tests (processors/) following the pattern established by phase_locking_test.cpp.

## Complexity Tracking

No constitution violations. All design decisions comply with existing principles.
