# Implementation Plan: Body Resonance

**Branch**: `131-body-resonance` | **Date**: 2026-03-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/131-body-resonance/spec.md`

## Summary

Implement a hybrid modal bank + FDN body resonance processor as a Layer 2 DSP component for the Innexus physical modelling instrument. The processor adds instrument body coloring (guitar, violin, cello) as a post-resonator stage, controlled by three parameters: body size (interpolating between violin/guitar/cello modal presets), material (wood vs metal damping character), and dry/wet mix. The signal chain is: coupling filter -> first-order crossover -> modal bank (low) / FDN (high) -> radiation HPF -> mix.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP (Biquad, OnePoleSmoother), Steinberg VST3 SDK
**Storage**: N/A (constexpr preset data, no files)
**Testing**: Catch2 via `dsp_tests` and `innexus_tests` targets
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo -- shared DSP library + plugin
**Performance Goals**: < 0.5% single core per voice at 44.1 kHz (~265 FLOPS/sample)
**Constraints**: Real-time safe (zero allocations in audio), energy passive, header-only DSP
**Scale/Scope**: 1 new header file (~500-700 lines), 3 new parameters, ~5 modified files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check**:

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Parameters registered in controller, processed in processor, no cross-references |
| II. Real-Time Audio Thread Safety | PASS | All buffers pre-allocated in prepare(), no allocations/locks in process() |
| III. Modern C++ Standards | PASS | constexpr arrays, std::array, RAII, no raw new/delete |
| IV. SIMD & DSP Optimization | PASS | SIMD analysis below; scalar-first workflow |
| V. VSTGUI Development | N/A | No UI changes in this spec |
| VI. Cross-Platform Compatibility | PASS | Header-only, no platform-specific code |
| VII. Project Structure & Build | PASS | Layer 2 placement, angle bracket includes |
| VIII. Testing Discipline | PASS | Test-first development, unit + integration tests |
| IX. Layered DSP Architecture | PASS | Layer 2, depends only on Layer 0/1 |
| X. DSP Processing Constraints | PASS | Impulse-invariant design, linear interpolation for FDN delays |
| XI. Performance Budgets | PASS | Target ~265 FLOPS/sample, well under 0.5% per voice |
| XII. Debugging Discipline | PASS | N/A for planning |
| XIII. Test-First Development | PASS | Tests written before implementation |
| XIV. Living Architecture Documentation | PASS | Will update layer-2-processors.md |
| XV. Pre-Implementation Research (ODR) | PASS | BodyResonance/BodyMode not found in codebase |
| XVI. Honest Completion | PASS | Compliance table in spec.md |

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

**Classes/Structs to be created**: BodyResonance, BodyMode

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| BodyResonance | `grep -r "class BodyResonance\|struct BodyResonance" dsp/ plugins/` | No | Create New |
| BodyMode | `grep -r "class BodyMode\|struct BodyMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all math is inline in the header)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Biquad | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | Coupling filter (2x), modal bank (8x), radiation HPF (1x) |
| BiquadCoefficients | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | Set impulse-invariant coefficients via setCoefficients() |
| FilterType | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | Highpass for radiation HPF, Peak/HighShelf for coupling |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Smooth size, material, mix parameters |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no body_resonance.h exists)
- [x] `specs/_architecture_/layer-2-processors.md` - No BodyResonance documented
- [x] `plugins/innexus/src/plugin_ids.h` - IDs 850-852 not yet registered

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (BodyResonance, BodyMode) are unique -- no matches found in codebase. The names are descriptive and unlikely to conflict with future additions.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Biquad | setCoefficients | `void setCoefficients(const BiquadCoefficients& coeffs) noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| BiquadCoefficients | fields | `float b0, b1, b2, a1, a2` | Yes |
| BiquadCoefficients | isStable | `[[nodiscard]] bool isStable() const noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float timeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class, BiquadCoefficients, FilterType
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/processors/crossover_filter.h` - CrossoverLR4 (NOT reused, wrong order)
- [x] `dsp/include/krate/dsp/effects/fdn_reverb.h` - FDNReverb (reference for Hadamard/absorption patterns)
- [x] `dsp/include/krate/dsp/processors/modal_resonator_bank.h` - ModalResonatorBank (reference for smoothing)
- [x] `plugins/innexus/src/processor/innexus_voice.h` - InnexusVoice struct
- [x] `plugins/innexus/src/processor/processor.cpp` - Voice processing loop
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter ID registry

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Biquad | `configure()` uses FilterType enum, not raw string | `FilterType::Highpass` |
| BiquadCoefficients | `setCoefficients()` takes a struct, not individual floats | `setCoefficients(BiquadCoefficients{b0,b1,b2,a1,a2})` |
| OnePoleSmoother | `snapTo()` sets both current AND target | Use at init, not during processing |
| CrossoverLR4 | LR4 = 24 dB/oct, NOT the 6 dB/oct required by spec | Do NOT reuse; implement simple first-order inline |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

**Decision**: No Layer 0 extraction needed. The impulse-invariant design formula is specific to this component and could be extracted later if Phase 6 (Sympathetic Resonance) needs it. For now, it stays as a private helper in BodyResonance.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| computeImpulseInvariantCoeffs() | Specific to modal body synthesis, only 1 consumer |
| computeAbsorptionFilter() | FDN-specific, same formula as FDNReverb but simpler 4-line version |
| applyHadamard4() | Trivial 4-line implementation, different from 8-channel FDNReverb version |

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | FDN has feedback. Modal bank is parallel (no feedback) but biquads have internal state |
| **Data parallelism width** | 8 modes (modal) / 4 lines (FDN) | Modal bank: 8 independent biquads = 2 AVX lanes. FDN: 4 lines = 1 AVX lane |
| **Branch density in inner loop** | LOW | No conditionals in hot path except mix==0 early-out |
| **Dominant operations** | Arithmetic (multiply-accumulate) | Biquad TDF2 = 5 mul + 4 add per mode |
| **Current CPU budget vs expected usage** | 0.5% budget vs ~0.13% expected | Substantial headroom |

### SIMD Viability Verdict

**Verdict**: MARGINAL -- DEFER

**Reasoning**: The 8 parallel modal biquads could benefit from SIMD (process 4 modes per SSE register). However, at ~0.13% CPU per voice, the algorithm is already well within budget. The FDN's 4 lines have feedback dependencies that prevent straightforward SIMD. Defer SIMD to a follow-up spec if profiling shows CPU pressure.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when mix==0 | ~100% for bypass case | LOW | YES |
| Skip modal bank when size is snapped | ~30% if no interpolation needed | LOW | YES (check if smoothing complete) |
| Batch biquad processing (processBlock) | ~5-10% from loop overhead reduction | LOW | YES |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - Processors

**Related features at same layer** (from roadmap):
- Phase 6: Sympathetic Resonance (Layer 3, but uses resonant filter banks)
- Future body type presets (extension of this feature)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Impulse-invariant biquad design | MEDIUM | Phase 6 sympathetic resonance | Keep local; extract after 2nd use |
| Modal preset interpolation | MEDIUM | Future body presets | Keep local; could become a utility |
| 4-line Hadamard FDN | LOW | Unlikely (room reverb uses 8-channel) | Keep local |
| T60-to-absorption conversion | MEDIUM | Any future reverb/resonance | Keep local; extract if needed |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep impulse-invariant design local | Only 1 consumer; Phase 6 may use different topology |
| Do NOT reuse CrossoverLR4 | Wrong order (24 dB/oct vs 6 dB/oct needed) |
| Do NOT reuse FDNReverb | Different scale (8-channel room vs 4-line body), too heavyweight |
| Reference FDNReverb patterns | applyHadamard butterfly, Jot absorption formula are proven |

### Review Trigger

After implementing **Phase 6 (Sympathetic Resonance)**, review this section:
- [ ] Does sympathetic resonance need impulse-invariant biquad design? -> Extract to shared utility
- [ ] Does it need T60-to-absorption conversion? -> Extract
- [ ] Any duplicated preset interpolation code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/131-body-resonance/
  plan.md              # This file
  research.md          # Phase 0 output - algorithm research
  data-model.md        # Phase 1 output - entity definitions
  quickstart.md        # Phase 1 output - build/test guide
  contracts/           # Phase 1 output
    body_resonance_api.h  # Public API contract
  tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/include/krate/dsp/processors/
  body_resonance.h            # NEW: Main DSP processor (header-only, ~500-700 lines)

dsp/tests/unit/processors/
  body_resonance_tests.cpp    # NEW: Unit tests for BodyResonance

plugins/innexus/src/
  plugin_ids.h                # MODIFIED: Add kBodySizeId/kBodyMaterialId/kBodyMixId
  processor/
    innexus_voice.h           # MODIFIED: Add BodyResonance field
    processor.h               # MODIFIED: Add body param atomics
    processor.cpp             # MODIFIED: Process body resonance per voice
    processor_params.cpp      # MODIFIED: Handle body param changes
    processor_state.cpp       # MODIFIED: Save/load body state
  controller/
    controller.cpp            # MODIFIED: Register 3 body parameters

plugins/innexus/tests/unit/processor/
  body_resonance_integration_tests.cpp  # NEW: Integration tests

specs/_architecture_/
  layer-2-processors.md       # MODIFIED: Document BodyResonance
```

**Structure Decision**: Follows existing monorepo layout. DSP component is header-only in the shared KrateDSP library at Layer 2. Plugin integration modifies the Innexus processor and controller.

## Implementation Phases

### Phase 1: DSP Core -- BodyResonance Processor

**Goal**: Implement the complete BodyResonance DSP component with unit tests.

**Tasks**:
1. Write unit tests for BodyResonance (test-first per Constitution XIII):
   - Bypass test (mix=0 produces bit-identical output)
   - Energy passivity test (RMS out <= RMS in across parameter sweep)
   - Size interpolation test (verify modal frequencies at size=0, 0.5, 1.0)
   - Material damping test (HF decay ratio at wood vs metal)
   - FDN stability test (no runaway at any parameter combo)
   - Sample rate scaling test (44.1k, 48k, 96k, 192k)
   - RT60 cap test (FDN RT60 within spec limits)
   - Radiation HPF test (energy below 0.7x lowest mode is attenuated)
   - Parameter sweep test (no clicks/pops during smooth size/material changes)

2. Implement `body_resonance.h`:
   - BodyMode struct and constexpr preset arrays
   - prepare(), reset(), setParams(), process(), processBlock()
   - Internal: coupling filter, modal bank, first-order crossover, 4-line FDN, radiation HPF
   - Parameter smoothing via OnePoleSmoother and pole/zero domain interpolation

3. Add to CMake build (`dsp/tests/unit/CMakeLists.txt`)

4. Build and verify all tests pass with zero warnings

**Deliverables**: `body_resonance.h`, `body_resonance_tests.cpp`, passing unit tests

### Phase 2: Plugin Integration -- Innexus Voice Engine

**Goal**: Wire BodyResonance into the Innexus processor and register VST3 parameters.

**Tasks**:
1. Write integration tests (test-first):
   - Parameter registration test (IDs 850, 851, 852 exist)
   - State save/load test (body params persist)
   - Full voice chain test (exciter -> resonator -> body -> mixer)

2. Add parameter IDs to `plugin_ids.h` (kBodySizeId=850, kBodyMaterialId=851, kBodyMixId=852)

3. Add `BodyResonance bodyResonance` field to `InnexusVoice`

4. Modify `processor.h`: Add atomic params (`bodySize_`, `bodyMaterial_`, `bodyMix_`)

5. Modify `processor.cpp`:
   - In `prepare()`: call `bodyResonance.prepare(sampleRate_)` for each voice
   - In `reset()`: call `bodyResonance.reset()` for each voice
   - In per-sample loop: call `bodyResonance.setParams()` per block, then `bodyResonance.process(physicalSample)` after resonator output
   - Apply to all resonator paths (modal, waveguide, crossfade)

6. Modify `processor_params.cpp`: Handle kBodySizeId, kBodyMaterialId, kBodyMixId

7. Modify `processor_state.cpp`: Save/load body parameters

8. Modify `controller.cpp`: Register 3 RangeParameters

9. Build, test, run pluginval

**Deliverables**: Working Innexus plugin with body resonance, passing integration tests and pluginval

### Phase 3: Quality Assurance and Documentation

**Goal**: Final verification, clang-tidy, architecture docs.

**Tasks**:
1. Run clang-tidy on all modified files
2. Fix any warnings
3. Run full test suite (dsp_tests + innexus_tests)
4. Run pluginval at strictness level 5
5. Update `specs/_architecture_/layer-2-processors.md` with BodyResonance documentation
6. Fill compliance table in spec.md
7. Commit

**Deliverables**: Clean build, all tests pass, architecture docs updated, compliance table filled

## Complexity Tracking

No constitution violations. No complexity exceptions needed.
