# Implementation Plan: Innexus M6 -- Creative Extensions

**Branch**: `120-creative-extensions` | **Date**: 2026-03-05 | **Spec**: `specs/120-creative-extensions/spec.md`
**Input**: Feature specification from `specs/120-creative-extensions/spec.md`

## Summary

Innexus Milestone 6 adds five creative extension features on top of the M1-M5 infrastructure: (1) harmonic cross-synthesis with timbral blend control, (2) stereo partial spread in the oscillator bank, (3) an autonomous evolution engine for timbral drift across memory snapshots, (4) two independent harmonic modulators with LFO-driven per-partial animation plus detune spread, and (5) multi-source blending from weighted memory slots. This requires extending the `HarmonicOscillatorBank` (Layer 2) with stereo output and detune, adding three new plugin-local DSP classes (`EvolutionEngine`, `HarmonicModulator`, `HarmonicBlender`), implementing cross-synthesis blend logic in the processor pipeline, adding 31 new parameters with full state persistence, and maintaining backward compatibility with M5 state files.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang/Xcode 13+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x+, VSTGUI 4.12+, KrateDSP (internal Layer 0-2)
**Storage**: VST3 IBStream binary state (version 6, backward-compatible with version 5)
**Testing**: Catch2 (via `dsp_tests` and `innexus_tests` targets)
**Target Platform**: Windows 10/11, macOS 11+, Linux (cross-platform)
**Project Type**: Monorepo VST3 plugin (Innexus instrument)
**Performance Goals**: < 1.0% additional CPU for all M6 features combined at 44.1 kHz (SC-008); oscillator bank + creative extensions < 2.0% total CPU
**Constraints**: Zero allocations on audio thread; all buffers pre-allocated in `prepare()`/`setupProcessing()`; no locks, no exceptions, no I/O on audio thread
**Scale/Scope**: 31 new parameters, 4 new DSP classes, 1 extended DSP class, ~2000 lines of new code estimated

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | New parameters follow existing pattern: atomics in processor, registered in controller, state sync via `setComponentState()` |
| II. Real-Time Audio Thread Safety | PASS | All new components use fixed-size arrays, no heap allocation on audio thread. LFO uses phase accumulator (no wavetable allocation). |
| III. Modern C++ Standards | PASS | C++20 features, `constexpr`, `noexcept`, `[[nodiscard]]`, RAII throughout |
| IV. SIMD & DSP Optimization | PASS | SoA layout preserved in oscillator bank extension; SIMD viability analyzed below |
| V. VSTGUI Development | N/A | No UI work in this milestone (parameters only, no custom views) |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code; all math is standard C++ |
| VII. Project Structure & Build System | PASS | DSP extensions in `dsp/include/krate/dsp/processors/`, plugin-local in `plugins/innexus/src/dsp/` |
| VIII. Testing Discipline | PASS | Test-first workflow; unit tests for each new component |
| IX. Layered Architecture | PASS | Stereo/detune in Layer 2 (oscillator bank). Evolution/modulator/blender in plugin-local DSP. |
| X. DSP Processing Constraints | PASS | No saturation, no oversampling needed. Parameter smoothing via OnePoleSmoother. |
| XI. Performance Budgets | PASS | Target < 1% additional CPU. Per-partial operations are O(48) per frame update, not per sample. |
| XII. Debugging Discipline | PASS | N/A for planning phase |
| XIII. Test-First Development | PASS | All tasks start with failing tests |
| XIV. Living Architecture Documentation | PASS | Architecture docs updated as final task |
| XV. Pre-Implementation Research (ODR Prevention) | PASS | See ODR section below |
| XVI. Honest Completion | PASS | N/A for planning phase |

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

**Classes/Structs to be created**: EvolutionEngine, HarmonicModulator, HarmonicBlender, ModulatorLfo (lightweight LFO for modulators)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| EvolutionEngine | `grep -r "class EvolutionEngine" dsp/ plugins/` | No | Create New in `plugins/innexus/src/dsp/` |
| HarmonicModulator | `grep -r "class HarmonicModulator" dsp/ plugins/` | No | Create New in `plugins/innexus/src/dsp/` |
| HarmonicBlender | `grep -r "class HarmonicBlender" dsp/ plugins/` | No | Create New in `plugins/innexus/src/dsp/` |
| ModulatorLfo | `grep -r "class ModulatorLfo" dsp/ plugins/` | No | Create New in `plugins/innexus/src/dsp/` |
| StereoSample | `grep -r "struct StereoSample" dsp/ plugins/` | No | Not needed; use `float& left, float& right` |
| PanMap | `grep -r "struct PanMap" dsp/ plugins/` | No | Not needed; use `std::array<float, kMaxPartials>` |
| DetuneMap | `grep -r "struct DetuneMap" dsp/ plugins/` | No | Not needed; use `std::array<float, kMaxPartials>` |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| computePureHarmonicReference | `grep -r "computePureHarmonic" dsp/ plugins/` | No | processor or inline | Create New |
| computeConstantPowerPan | `grep -r "constantPowerPan\|constantPower" dsp/ plugins/` | No | oscillator bank | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `HarmonicOscillatorBank` | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | 2 | EXTEND with `processStereo()`, stereo spread, detune spread |
| `lerpHarmonicFrame()` | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | 2 | REUSE for evolution interpolation, cross-synthesis blend |
| `lerpResidualFrame()` | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | 2 | REUSE for evolution and multi-source residual blending |
| `applyHarmonicMask()` | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | 2 | REUSE in pipeline after morph/evolution, before oscillator |
| `computeHarmonicMask()` | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | 2 | REUSE for harmonic filter |
| `HarmonicSnapshot` | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | 2 | REUSE as evolution waypoints and blend inputs |
| `MemorySlot` | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | 2 | REUSE for 8 memory slots as evolution/blend sources |
| `captureSnapshot()` | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | 2 | REUSE for snapshot creation |
| `recallSnapshotToFrame()` | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | 2 | REUSE to reconstruct frames from snapshots |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | REUSE for all 31 parameter smoothers |
| `HarmonicFrame` / `Partial` | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | REUSE as core data types |
| `ResidualFrame` | `dsp/include/krate/dsp/processors/residual_types.h` | 2 | REUSE for residual blending |
| `Xorshift32` | `dsp/include/krate/dsp/core/random.h` | 0 | REUSE for Random Walk and S&H waveform |
| `kPi`, `kTwoPi`, `kHalfPi` | `dsp/include/krate/dsp/core/math_constants.h` | 0 | REUSE for LFO and pan calculations |
| `LFO` (Layer 1) | `dsp/include/krate/dsp/primitives/lfo.h` | 1 | NOT REUSED -- too heavyweight (wavetable-based, uses `std::vector`). Harmonic modulators need a lightweight phase-only LFO (5 waveforms from formulas, no allocation). |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (LFO exists but not reused)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (harmonic_oscillator_bank.h to extend)
- [x] `plugins/innexus/src/dsp/` - Plugin-local DSP (no conflicts)
- [x] `specs/_architecture_/` - Component inventory checked

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new types (EvolutionEngine, HarmonicModulator, HarmonicBlender, ModulatorLfo) have unique names not found anywhere in the codebase. The HarmonicOscillatorBank extension adds new methods without renaming the class. All new classes are in the `Innexus` namespace (plugin-local) or extend `Krate::DSP` (Layer 2), with no overlap.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| HarmonicOscillatorBank | process() | `[[nodiscard]] float process() noexcept` | Yes |
| HarmonicOscillatorBank | loadFrame() | `void loadFrame(const HarmonicFrame& frame, float targetPitch) noexcept` | Yes |
| HarmonicOscillatorBank | prepare() | `void prepare(double sampleRate) noexcept` | Yes |
| HarmonicOscillatorBank | reset() | `void reset() noexcept` | Yes |
| HarmonicOscillatorBank | setInharmonicityAmount() | `void setInharmonicityAmount(float amount) noexcept` | Yes |
| HarmonicOscillatorBank | setTargetPitch() | `void setTargetPitch(float frequencyHz) noexcept` | Yes |
| lerpHarmonicFrame | (free function) | `inline HarmonicFrame lerpHarmonicFrame(const HarmonicFrame& a, const HarmonicFrame& b, float t) noexcept` | Yes |
| lerpResidualFrame | (free function) | `inline ResidualFrame lerpResidualFrame(const ResidualFrame& a, const ResidualFrame& b, float t) noexcept` | Yes |
| applyHarmonicMask | (free function) | `inline void applyHarmonicMask(HarmonicFrame& frame, const std::array<float, kMaxPartials>& mask) noexcept` | Yes |
| captureSnapshot | (free function) | `inline HarmonicSnapshot captureSnapshot(const HarmonicFrame& frame, const ResidualFrame& residual) noexcept` | Yes |
| recallSnapshotToFrame | (free function) | `inline void recallSnapshotToFrame(const HarmonicSnapshot& snap, HarmonicFrame& frame, ResidualFrame& residual) noexcept` | Yes |
| OnePoleSmoother | configure() | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget() | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process() | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | getCurrentValue() | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | snapTo() | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | advanceSamples() | `void advanceSamples(size_t numSamples) noexcept` | Yes |
| Xorshift32 | nextFloat() | `[[nodiscard]] constexpr float nextFloat() noexcept` (range: [-1.0, 1.0]) | Yes |
| Xorshift32 | nextUnipolar() | `[[nodiscard]] constexpr float nextUnipolar() noexcept` (range: [0.0, 1.0]) | Yes |
| HarmonicSnapshot | numPartials | `int numPartials = 0` | Yes |
| HarmonicSnapshot | relativeFreqs | `std::array<float, kMaxPartials> relativeFreqs{}` | Yes |
| HarmonicSnapshot | normalizedAmps | `std::array<float, kMaxPartials> normalizedAmps{}` | Yes |
| HarmonicSnapshot | inharmonicDeviation | `std::array<float, kMaxPartials> inharmonicDeviation{}` | Yes |
| HarmonicSnapshot | residualBands | `std::array<float, kResidualBands> residualBands{}` | Yes |
| HarmonicSnapshot | residualEnergy | `float residualEnergy = 0.0f` | Yes |
| MemorySlot | occupied | `bool occupied = false` | Yes |
| MemorySlot | snapshot | `HarmonicSnapshot snapshot{}` | Yes |
| Partial | harmonicIndex | `int harmonicIndex = 0` (1-based) | Yes |
| Partial | amplitude | `float amplitude = 0.0f` | Yes |
| Partial | relativeFrequency | `float relativeFrequency = 0.0f` | Yes |
| Partial | inharmonicDeviation | `float inharmonicDeviation = 0.0f` | Yes |
| HarmonicFrame | numPartials | `int numPartials = 0` | Yes |
| HarmonicFrame | partials | `std::array<Partial, kMaxPartials> partials{}` | Yes |
| kMaxPartials | constant | `inline constexpr size_t kMaxPartials = 48` | Yes |
| kResidualBands | constant | `inline constexpr size_t kResidualBands = 16` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` - Full class
- [x] `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` - lerp, filter functions
- [x] `dsp/include/krate/dsp/processors/harmonic_types.h` - Partial, HarmonicFrame, kMaxPartials
- [x] `dsp/include/krate/dsp/processors/harmonic_snapshot.h` - HarmonicSnapshot, MemorySlot, captureSnapshot, recallSnapshotToFrame
- [x] `dsp/include/krate/dsp/processors/residual_types.h` - ResidualFrame, kResidualBands
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi, kHalfPi
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class (decided not to reuse)
- [x] `plugins/innexus/src/processor/processor.h` - Full processor state
- [x] `plugins/innexus/src/processor/processor.cpp` - Full process() implementation
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter IDs
- [x] `plugins/innexus/src/controller/controller.cpp` - Parameter registration
- [x] `plugins/innexus/src/parameters/innexus_params.h` - Parameter helpers

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| OnePoleSmoother | `advanceSamples()` for block-rate processing | `smoother.advanceSamples(blockSize)` then `getCurrentValue()` |
| Xorshift32 | `nextFloat()` returns [-1.0, 1.0] bipolar | For unipolar [0.0, 1.0] use `nextUnipolar()` |
| HarmonicSnapshot | `normalizedAmps` are L2-normalized, not raw | Must match normalization when computing pure reference |
| Partial.harmonicIndex | 1-based (not 0-based) | First harmonic is index 1 |
| Processor state version | Currently version 5 | Must increment to version 6 for M6 |
| processParameterChanges | Uses last point: `paramQueue->getPoint(numPoints - 1, ...)` | Follow same pattern for new params |
| oscillatorBank_.process() | Called in multiple places (crossfade capture) | After stereo extension, all call sites must switch to `processStereo()` |
| Process loop output | Currently `out[0][s] = sample; out[1][s] = sample;` (mono to stereo copy) | Must change to `out[0][s] = leftSample; out[1][s] = rightSample;` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| constantPowerPan | Audio pan law, general-purpose | Could go to `core/stereo_utils.h` | HarmonicOscillatorBank, future stereo components |
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| computePureHarmonicReference | Specific to cross-synthesis, single consumer (processor) |
| ModulatorLfo waveform formulas | Specific to harmonic modulators, lightweight inline |
| Evolution waypoint management | Specific to evolution engine |

**Decision**: The constant-power pan formula is simple enough (two trig calls) to keep inline in the oscillator bank for now. If a second consumer emerges, extract to `core/stereo_utils.h`. All other utilities stay local to their consuming class.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES (MCF oscillator) | Each partial has s[n] = s[n-1] + eps*c[n-1]; serial dependency per partial. However, partials are independent of each other. |
| **Data parallelism width** | 48 partials | 48 independent oscillators in SoA layout. Excellent width for SIMD. |
| **Branch density in inner loop** | LOW | Per-sample loop is nearly branchless (renorm check every 16 samples). |
| **Dominant operations** | Arithmetic (mul, add) | MCF: 2 muls + 2 adds per partial. Pan: 2 muls per partial. |
| **Current CPU budget vs expected usage** | <2.0% total target | Existing mono oscillator bank is well under budget. Stereo adds ~2x work (two sums instead of one). |

### SIMD Viability Verdict

**Verdict**: MARGINAL -- DEFER

**Reasoning**: The oscillator bank's SoA layout (48 partials in aligned arrays) is structurally ideal for SIMD. The per-partial MCF step (2 muls + 2 adds) and pan coefficient multiplication could process 4-8 partials simultaneously with SSE/AVX. However, the current scalar implementation is already well under the CPU budget, and the stereo extension only adds one multiply and two accumulates per partial per sample. SIMD optimization should be deferred to a follow-up spec if profiling shows the combined creative extensions layer exceeds the 2% CPU target.

### Implementation Workflow

**Phase 1 (Scalar)**: Implement all M6 features with scalar code during `/speckit.implement`. Measure CPU baseline.

**Phase 2 (SIMD)**: Deferred. Only pursued if SC-008 fails with scalar implementation.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip stereo pan when spread=0 | ~50% for mono case | LOW | YES |
| Skip detune when detuneSpread=0 | ~10% for non-detuned case | LOW | YES |
| Skip modulator when depth=0 | ~100% for unmodulated partials | LOW | YES |
| Skip evolution when disabled | ~100% for static case | LOW | YES |
| Per-frame (not per-sample) pan/detune recalc | Already specified in FR-010/FR-031 | N/A | YES (spec-mandated) |
| Early-out blend when single source weight=1.0 | ~90% for single-source case | LOW | YES |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (oscillator bank stereo/detune) + Plugin-local DSP

### Sibling Features Analysis

**Related features at same layer** (known or anticipated):
- Future additive synth plugins in the monorepo could reuse stereo spread and detune
- Future Innexus milestones may add more modulation types

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `HarmonicOscillatorBank::processStereo()` | HIGH | Any additive synth plugin | Keep in Layer 2 (spec-mandated FR-046) |
| `HarmonicOscillatorBank` detune spread | HIGH | Any additive synth plugin | Keep in Layer 2 (spec-mandated FR-046) |
| ModulatorLfo | MEDIUM | Other plugins needing lightweight LFO | Keep plugin-local; promote if 2nd consumer appears |
| EvolutionEngine | LOW | Innexus-specific | Keep plugin-local |
| HarmonicBlender | LOW | Innexus-specific | Keep plugin-local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Stereo/detune in Layer 2 | General-purpose additive synthesis features (FR-046). Any future additive plugin benefits. |
| Evolution/modulator/blender plugin-local | Innexus-specific workflow. No current sibling consumers. |
| Lightweight ModulatorLfo instead of reusing Layer 1 LFO | Layer 1 LFO uses `std::vector` wavetables (allocated in prepare). Modulators need a zero-allocation, formula-based LFO with only 5 waveforms. The Layer 1 LFO is designed for general-purpose delay/synth modulation with tempo sync, symmetry, quantization -- features not needed here. |

## Project Structure

### Documentation (this feature)

```text
specs/120-creative-extensions/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
    +-- harmonic_oscillator_bank_stereo.h
    +-- evolution_engine.h
    +-- harmonic_modulator.h
    +-- harmonic_blender.h
+-- tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
# Layer 2 extension (KrateDSP shared library)
dsp/include/krate/dsp/processors/
+-- harmonic_oscillator_bank.h  # MODIFIED: add processStereo(), stereo spread, detune spread

# Plugin-local DSP (Innexus only)
plugins/innexus/src/dsp/
+-- evolution_engine.h           # NEW: autonomous timbral drift (FR-014 to FR-023)
+-- harmonic_modulator.h         # NEW: LFO-driven per-partial animation (FR-024 to FR-033)
+-- harmonic_blender.h           # NEW: multi-source weighted blending (FR-034 to FR-042)
+-- modulator_lfo.h              # NEW: lightweight formula-based LFO for modulators

# Plugin processor/controller
plugins/innexus/src/
+-- plugin_ids.h                 # MODIFIED: add M6 parameter IDs (600-649)
+-- processor/processor.h        # MODIFIED: add M6 atomics, DSP members, state
+-- processor/processor.cpp      # MODIFIED: process(), processParameterChanges(), state save/load
+-- controller/controller.cpp    # MODIFIED: register M6 parameters

# Tests
dsp/tests/unit/processors/
+-- test_harmonic_oscillator_bank_stereo.cpp  # NEW: stereo spread + detune tests (T005, T006)

plugins/innexus/tests/unit/processor/
+-- test_cross_synthesis.cpp                  # NEW: timbral blend tests (T016, T017)
+-- test_stereo_spread_integration.cpp        # NEW: stereo spread integration tests (T012)
+-- test_evolution_engine.cpp                 # NEW: evolution engine tests (T023, T024)
+-- test_harmonic_modulator.cpp               # NEW: modulator + LFO waveform tests (T030-T033)
+-- test_harmonic_blender.cpp                 # NEW: multi-source blend tests (T039-T041)
+-- test_m6_pipeline_integration.cpp          # NEW: full pipeline integration (T055)

plugins/innexus/tests/unit/vst/
+-- test_state_v6.cpp                         # NEW: state round-trip + v5 backward compat (T047)
```

**Structure Decision**: Follows existing monorepo layout. Layer 2 extensions go in KrateDSP shared library. Plugin-specific DSP goes in `plugins/innexus/src/dsp/`. Tests mirror the source structure.

---

## Architecture & Design

### Signal Flow Diagram

```
                           M6 Processing Pipeline (FR-049)
                           ===============================

  [Source Selection Stage]
         |
         v
  +------------------+     +-------------------+
  | Multi-Source      |  OR | Cross-Synthesis   |  OR  [Single Source]
  | Blend (FR-034)   |     | Timbral Blend     |      (recalled/live)
  | (weighted N-slot) |     | (FR-001)          |
  +------------------+     +-------------------+
         |                         |                       |
         +------------+------------+-----------------------+
                      |
                      v
              +------------------+
              | Evolution Engine |  (FR-014, if enabled; skipped if blend active)
              | (waypoint interp)|
              +------------------+
                      |
                      v
              +------------------+
              | Harmonic Filter  |  (existing M4, FR-019-025)
              | (per-partial     |
              |  amplitude mask) |
              +------------------+
                      |
                      v
              +------------------+
              | Harmonic         |  (FR-024, per-partial amp/freq/pan LFO)
              | Modulators 1 & 2|
              +------------------+
                      |
                      v
              +------------------------------+
              | HarmonicOscillatorBank       |
              | - loadFrame() with modified  |
              |   amplitudes/frequencies     |
              | - processStereo() with       |
              |   stereo spread (FR-006)     |
              |   + detune spread (FR-030)   |
              +------------------------------+
                      |
                      v
              [left, right] stereo output
                      |
                      v  (residual is mono center, mixed into both channels)
              +------------------+
              | Mix with         |
              | residual synth   |
              | (existing M2)    |
              +------------------+
                      |
                      v
              [final left, right] -> velocity -> release envelope -> master gain -> output
```

### Component Diagram

```
+------------------------------------------------------------------+
|                    Krate::DSP (Layer 2)                           |
|                                                                  |
|  harmonic_oscillator_bank.h  [MODIFIED]                          |
|  +------------------------------------------------------------+ |
|  | class HarmonicOscillatorBank                                | |
|  |   + processStereo(float& left, float& right) noexcept  NEW | |
|  |   + setStereoSpread(float spread) noexcept              NEW | |
|  |   + setDetuneSpread(float spread) noexcept              NEW | |
|  |   - panLeft_[48], panRight_[48]                         NEW | |
|  |   - detuneMultiplier_[48]                               NEW | |
|  |   - stereoSpread_, detuneSpread_                        NEW | |
|  |   - recalculatePanPositions()                           NEW | |
|  |   - recalculateDetuneOffsets()                          NEW | |
|  +------------------------------------------------------------+ |
+------------------------------------------------------------------+

+------------------------------------------------------------------+
|                    Innexus Plugin-Local DSP                       |
|                                                                  |
|  modulator_lfo.h  [NEW]                                          |
|  +------------------------------------------------------------+ |
|  | class ModulatorLfo                                          | |
|  |   + prepare(double sampleRate) noexcept                     | |
|  |   + process() noexcept -> float (bipolar or unipolar)       | |
|  |   + setRate(float hz) noexcept                              | |
|  |   + setWaveform(int waveform) noexcept                      | |
|  |   + reset() noexcept                                        | |
|  |   - phase_, phaseIncrement_                                 | |
|  |   - Xorshift32 rng_ (for S&H)                              | |
|  +------------------------------------------------------------+ |
|                                                                  |
|  harmonic_modulator.h  [NEW]                                     |
|  +------------------------------------------------------------+ |
|  | class HarmonicModulator                                     | |
|  |   + prepare(double sampleRate) noexcept                     | |
|  |   + setEnabled(bool) noexcept                               | |
|  |   + setWaveform(ModulatorWaveform) noexcept                 | |
|  |   + setRate(float hz) noexcept                              | |
|  |   + setDepth(float) noexcept                                | |
|  |   + setTargetRange(int start, int end) noexcept             | |
|  |   + setTarget(ModulatorTarget) noexcept                     | |
|  |   + advance() noexcept            // per-sample LFO step    | |
|  |   + applyAmplitudeModulation(     // per-frame application  | |
|  |       HarmonicFrame&) noexcept                              | |
|  |   + getFrequencyMultipliers() noexcept // per-frame query   | |
|  |   + getPanOffsets() noexcept           // per-frame query   | |
|  |   + computeWaveform() noexcept                              | |
|  |   - ModulatorLfo lfo_                                       | |
|  |   // Note: rate/depth smoothing done in processor, not here | |
|  +------------------------------------------------------------+ |
|                                                                  |
|  evolution_engine.h  [NEW]                                       |
|  +------------------------------------------------------------+ |
|  | class EvolutionEngine                                       | |
|  |   + prepare(double sampleRate) noexcept                     | |
|  |   + reset() noexcept                                        | |
|  |   + updateWaypoints(slots) noexcept                         | |
|  |   + setMode(EvolutionMode) noexcept                         | |
|  |   + setSpeed(float hz) noexcept                             | |
|  |   + setDepth(float) noexcept                                | |
|  |   + setManualOffset(float offset) noexcept                  | |
|  |   + advance() noexcept           // per-sample phase update | |
|  |   + getInterpolatedFrame(slots,                             | |
|  |       frame, residual) -> bool noexcept // per-frame query  | |
|  |   + getPosition() -> float noexcept                         | |
|  |   + getNumWaypoints() -> int noexcept                       | |
|  |   - float phase_, inverseSampleRate_                        | |
|  |   - int direction_ (for PingPong)                           | |
|  |   - Xorshift32 rng_ (for Random Walk)                      | |
|  |   - int numWaypoints_, waypointIndices_[8]                  | |
|  +------------------------------------------------------------+ |
|                                                                  |
|  harmonic_blender.h  [NEW]                                       |
|  +------------------------------------------------------------+ |
|  | class HarmonicBlender                                       | |
|  |   + setSlotWeight(int slotIndex, float weight) noexcept NEW | |
|  |   + setLiveWeight(float weight) noexcept               NEW | |
|  |   + blend(slots, liveFrame, liveResidual,              NEW | |
|  |           hasLiveSource, frame, residual) noexcept          | |
|  |   + getEffectiveSlotWeight(int) noexcept               NEW | |
|  |   + getEffectiveLiveWeight() noexcept                  NEW | |
|  |   - slotWeights_[8]                                         | |
|  |   - liveWeight_                                             | |
|  +------------------------------------------------------------+ |
+------------------------------------------------------------------+
```

---

## Detailed Component Designs

### 1. HarmonicOscillatorBank Stereo Extension (Layer 2)

**Location**: `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`

**New public methods**:
```cpp
/// @brief Generate a single stereo output sample pair.
/// Uses constant-power panning based on stereo spread.
/// @param[out] left Left channel output
/// @param[out] right Right channel output
/// @note Real-time safe
void processStereo(float& left, float& right) noexcept;

/// @brief Set stereo spread amount (FR-006).
/// @param spread 0.0 = mono center, 1.0 = maximum L/R alternation
void setStereoSpread(float spread) noexcept;

/// @brief Set detune spread amount (FR-030).
/// @param spread 0.0 = no detuning, 1.0 = maximum detune (kDetuneMaxCents per harmonic)
void setDetuneSpread(float spread) noexcept;
```

**New private members**:
```cpp
// Stereo spread (FR-006 to FR-013)
alignas(32) std::array<float, kMaxPartials> panLeft_{};    ///< Per-partial left gain
alignas(32) std::array<float, kMaxPartials> panRight_{};   ///< Per-partial right gain
float stereoSpread_ = 0.0f;                                ///< Current spread amount

// Detune spread (FR-030 to FR-032)
alignas(32) std::array<float, kMaxPartials> detuneMultiplier_{};  ///< Per-partial pre-computed freq multiplier
float detuneSpread_ = 0.0f;                                      ///< Current detune amount

static constexpr float kDetuneMaxCents = 15.0f;  ///< Maximum detune per harmonic number at spread=1.0
```

**New private methods**:
```cpp
/// @brief Recalculate per-partial pan positions from stereo spread.
/// Called when stereoSpread_ changes.
/// Pan law: constant-power
///   panAngle = pi/4 + panPosition * pi/4
///   left = cos(panAngle), right = sin(panAngle)
/// Where panPosition for partial i:
///   - Fundamental (harmonicIndex==1): panPosition = spread * 0.25 * direction
///   - Others: panPosition = spread * direction
///   - direction = -1 for odd harmonicIndex, +1 for even harmonicIndex
void recalculatePanPositions() noexcept;

/// @brief Recalculate per-partial detune multipliers from detune spread.
/// Called when detuneSpread_ changes.
/// detuneOffset_cents = detuneSpread * harmonicIndex * kDetuneMaxCents * direction
/// detuneMultiplier_[i] = pow(2.0, detuneOffset_cents / 1200.0)  // pre-computed multiplier
/// where direction = +1 for odd harmonicIndex, -1 for even harmonicIndex
void recalculateDetuneOffsets() noexcept;
```

**Constant-power pan law formulas** (FR-008):
```
// Sign convention: panPosition < 0 = pan left, panPosition > 0 = pan right.
// direction_n = -1 for odd harmonics → negative panPosition → left channel dominates.
// direction_n = +1 for even harmonics → positive panPosition → right channel dominates.
// At panPosition = 0 (center): angle = pi/4, cos(pi/4) = sin(pi/4) = 0.7071 (equal power).
// At panPosition = -1 (full left): angle = 0, cos(0) = 1.0, sin(0) = 0.0.
// At panPosition = +1 (full right): angle = pi/2, cos(pi/2) = 0.0, sin(pi/2) = 1.0.

panPosition_n = spread * direction_n * scaleFactor_n
  where direction_n = (harmonicIndex_n % 2 == 1) ? -1.0 : +1.0   // odd=left (-), even=right (+)
        scaleFactor_n = (harmonicIndex_n == 1) ? 0.25 : 1.0       // fundamental stays near center

panAngle = kPi/4.0 + panPosition_n * kPi/4.0
  (kPi/4 = 45 degrees = center; -kPi/4 offset = full left; +kPi/4 offset = full right)

panLeft_n  = cos(panAngle)
panRight_n = sin(panAngle)
```

At spread=0: all panPosition=0, panAngle=pi/4, left=cos(pi/4)=0.7071, right=sin(pi/4)=0.7071 (equal power, mono center).
At spread=1: odd partials get panAngle=0 (left=1,right=0), even get panAngle=pi/2 (left=0,right=1).

**Detune formula** (FR-030, FR-032):
```
// Compute offset in cents, then pre-compute multiplier (stored in detuneMultiplier_[n]):
detuneOffsetCents = detuneSpread * harmonicIndex_n * kDetuneMaxCents * direction_n
  where direction_n = (harmonicIndex_n % 2 == 1) ? +1.0 : -1.0  // opposite directions for width
detuneMultiplier_[n] = pow(2.0, detuneOffsetCents / 1200.0)     // pre-computed once per frame

Final frequency (combined with inharmonicity):
  freq_n = (harmonicIndex_n + inharmonicDeviation_n * inharmonicityAmount) * targetPitch
           * detuneMultiplier_[n]
```

**`processStereo()` implementation outline**:
```cpp
void processStereo(float& left, float& right) noexcept {
    if (!prepared_ || !frameLoaded_) {
        left = right = 0.0f;
        return;
    }

    float sumL = 0.0f, sumR = 0.0f;
    const int n = activePartials_;

    ++renormCounter_;
    const bool doRenorm = (renormCounter_ >= 16);
    if (doRenorm) renormCounter_ = 0;

    for (int i = 0; i < n; ++i) {
        // Amplitude smoothing (same as mono)
        float target = targetAmplitude_[i] * antiAliasGain_[i];
        currentAmplitude_[i] += ampSmoothCoeff_ * (target - currentAmplitude_[i]);

        // MCF oscillator step (same as mono)
        float s = sinState_[i];
        float c = cosState_[i];
        float eps = epsilon_[i];
        float amp = s * currentAmplitude_[i];

        // Stereo panning
        sumL += amp * panLeft_[i];
        sumR += amp * panRight_[i];

        // Advance phasor
        float sNew = s + eps * c;
        float cNew = c - eps * sNew;

        if (doRenorm) { /* same renorm logic */ }

        sinState_[i] = sNew;
        cosState_[i] = cNew;
    }

    // Fade-out residual partials (same as mono but stereo)
    // ... (similar loop for partials beyond activePartials_)

    // Crossfade (adapted for stereo)
    // NOTE: crossfadeOldLevel_ is captured as a mono scalar (lastOutputSample_ before frame change).
    // During the crossfade window (~10ms), the captured old signal is blended in mono-center into
    // both L and R channels equally. This means the spatial position transitions from mono-center
    // (the old frame's final captured sample) to the new frame's stereo pan positions over the
    // crossfade duration. This is acceptable: the crossfade is only ~441 samples at 44.1kHz and
    // the perceptual effect is indistinguishable from a true stereo crossfade at those timescales.
    if (crossfadeRemaining_ > 0) {
        float fadeProgress = float(crossfadeRemaining_) / float(crossfadeLengthSamples_);
        float oldMono = crossfadeOldLevel_ * fadeProgress;
        sumL = oldMono + sumL * (1.0f - fadeProgress);
        sumR = oldMono + sumR * (1.0f - fadeProgress);
        --crossfadeRemaining_;
    }

    left = std::clamp(sumL, -kOutputClamp, kOutputClamp);
    right = std::clamp(sumR, -kOutputClamp, kOutputClamp);
    lastOutputSample_ = (left + right) * 0.5f;  // for crossfade capture
}
```

**Impact on existing `process()`**: The existing `float process()` is PRESERVED (FR-050 backward compatibility). It continues to work identically. `processStereo()` is a new parallel method. The processor calls `processStereo()` when stereo output is needed (which is always in Innexus M6).

**`recalculateFrequencies()` modification**: Must incorporate detune offsets when `detuneSpread_ > 0`:
```cpp
void recalculateFrequencies() noexcept {
    constexpr float kMaxEpsilon = 1.99f;
    for (int i = 0; i < activePartials_; ++i) {
        float freq = computePartialFrequency(i);  // includes inharmonicity
        if (detuneSpread_ > 0.0f) {
            freq *= detuneMultiplier_[i];  // pre-computed multiplier from recalculateDetuneOffsets()
        }
        float eps = 2.0f * std::sin(kPi * freq * inverseSampleRate_);
        epsilon_[i] = std::clamp(eps, -kMaxEpsilon, kMaxEpsilon);
    }
}
```

**Thread Safety**: Single-threaded (same as existing). All methods called from audio thread only.

**Memory**: No new allocations. All arrays are fixed-size `std::array<float, kMaxPartials>` with `alignas(32)`.

---

### 2. ModulatorLfo (Plugin-Local)

**Location**: `plugins/innexus/src/dsp/modulator_lfo.h`
**Namespace**: `Innexus`

A lightweight, zero-allocation LFO using direct formulas (no wavetable). Designed for the harmonic modulator's per-partial animation.

```cpp
class ModulatorLfo {
public:
    enum Waveform : int { Sine = 0, Triangle, Square, Saw, RandomSH };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;   // Reset phase to 0.0

    /// @brief Generate one LFO sample.
    /// @return Value in [-1.0, 1.0] (bipolar).
    [[nodiscard]] float process() noexcept;

    /// @brief Get unipolar value [0.0, 1.0] from the bipolar output.
    [[nodiscard]] float processUnipolar() noexcept;

    void setRate(float hz) noexcept;
    void setWaveform(int waveform) noexcept;

private:
    float phase_ = 0.0f;           // [0.0, 1.0)
    float phaseIncrement_ = 0.0f;  // rate / sampleRate
    double sampleRate_ = 44100.0;
    int waveform_ = 0;             // Sine
    Krate::DSP::Xorshift32 rng_{42};
    float currentSHValue_ = 0.0f;  // Held S&H value
    bool previousHalf_ = false;    // For S&H trigger detection
};
```

**LFO Waveform Formulas** (all produce bipolar [-1, 1] from phase in [0, 1)):

| Waveform | Formula |
|----------|---------|
| Sine | `sin(2 * pi * phase)` |
| Triangle | `4.0 * fabs(phase - 0.5) - 1.0` (phase=0 → 1, phase=0.5 → -1, phase=1 → 1) |
| Square | `phase < 0.5 ? 1.0 : -1.0` |
| Saw | `2.0 * phase - 1.0` |
| Random S&H | Hold `rng_.nextFloat()` until phase wraps past 0.0 (cycle boundary), then new value |

**Unipolar conversion**: `(bipolar + 1.0) * 0.5` for amplitude modulation target.

---

### 3. HarmonicModulator (Plugin-Local)

**Location**: `plugins/innexus/src/dsp/harmonic_modulator.h`
**Namespace**: `Innexus`

```cpp
class HarmonicModulator {
public:
    enum Target : int { Amplitude = 0, Frequency, Pan };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /// @brief Apply modulation to a HarmonicFrame in-place.
    /// Also writes pan offsets to the provided array (for Pan target).
    /// @param frame Frame to modulate (amplitudes/frequencies modified in-place)
    /// @param panOffsets Output array for pan offsets (only written if target==Pan)
    void process(Krate::DSP::HarmonicFrame& frame,
                 std::array<float, Krate::DSP::kMaxPartials>& panOffsets) noexcept;

    void setEnabled(bool enabled) noexcept;
    void setWaveform(int waveform) noexcept;   // 0-4 maps to ModulatorLfo::Waveform
    void setRate(float hz) noexcept;            // 0.01-20 Hz
    void setDepth(float depth) noexcept;        // 0.0-1.0
    void setTargetRange(int start, int end) noexcept;  // 1-48
    void setTarget(int target) noexcept;        // 0=Amplitude, 1=Frequency, 2=Pan

    [[nodiscard]] bool isEnabled() const noexcept;

private:
    ModulatorLfo lfo_;
    Krate::DSP::OnePoleSmoother rateSmoother_;
    Krate::DSP::OnePoleSmoother depthSmoother_;

    bool enabled_ = false;
    int targetType_ = 0;    // Amplitude
    int rangeStart_ = 1;    // 1-based partial index
    int rangeEnd_ = 48;
    float depth_ = 0.0f;

    static constexpr float kModMaxCents = 50.0f;  // FR-026
};
```

**Modulation formulas** (FR-025, FR-026, FR-027):

**Amplitude** (multiplicative, unipolar LFO):
```
effectiveAmp_n = modelAmp_n * (1.0 - depth + depth * lfoUnipolar)
  where lfoUnipolar = (lfo.process() + 1.0) * 0.5   // [0.0, 1.0]
```

**Frequency** (additive in cents, bipolar LFO):
```
effectiveFreq_n = modelFreq_n * pow(2.0, depth * lfoBipolar * kModMaxCents / 1200.0)
  where lfoBipolar = lfo.process()  // [-1.0, 1.0]
```

**Pan** (offset, bipolar LFO):
```
panOffset_n = depth * lfoBipolar * 0.5   // +/-0.5 range
  // Applied later: effectivePan_n = basePan_n + panOffset_n, clamped to [-1, 1]
```

**Overlapping modulators** (FR-028):
- Amplitude: effects multiply (both modulators apply their factor sequentially)
- Frequency/Pan: effects add (both modulators contribute their offset)

---

### 4. EvolutionEngine (Plugin-Local)

**Location**: `plugins/innexus/src/dsp/evolution_engine.h`
**Namespace**: `Innexus`

```cpp
class EvolutionEngine {
public:
    enum Mode : int { Cycle = 0, PingPong, RandomWalk };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /// @brief Compute the evolved frame from populated memory slots.
    /// Writes blended harmonic and residual frames.
    /// @param slots Array of 8 memory slots
    /// @param manualMorphOffset Manual morph offset: (morphPosition - 0.5)
    /// @param outFrame Output harmonic frame
    /// @param outResidual Output residual frame
    /// @return true if evolution produced a valid frame, false if disabled or no waypoints
    [[nodiscard]] bool process(
        const std::array<Krate::DSP::MemorySlot, 8>& slots,
        float manualMorphOffset,
        Krate::DSP::HarmonicFrame& outFrame,
        Krate::DSP::ResidualFrame& outResidual) noexcept;

    /// @brief Advance the evolution phase by one audio block.
    /// Call once per process() block, not per sample.
    /// @param blockSize Number of samples in the current block
    void advancePhase(size_t blockSize) noexcept;

    void setEnabled(bool enabled) noexcept;
    void setSpeed(float hz) noexcept;        // 0.01-10.0 Hz
    void setDepth(float depth) noexcept;     // 0.0-1.0
    void setMode(int mode) noexcept;         // 0=Cycle, 1=PingPong, 2=RandomWalk

    [[nodiscard]] bool isEnabled() const noexcept;

private:
    /// @brief Build list of populated slot indices.
    /// @return Number of populated waypoints found
    int buildWaypointList(const std::array<Krate::DSP::MemorySlot, 8>& slots) noexcept;

    /// @brief Interpolate between two snapshots, handling partial count mismatch.
    void interpolateSnapshots(
        const Krate::DSP::HarmonicSnapshot& a,
        const Krate::DSP::HarmonicSnapshot& b,
        float t,
        Krate::DSP::HarmonicFrame& outFrame,
        Krate::DSP::ResidualFrame& outResidual) noexcept;

    bool enabled_ = false;
    int mode_ = 0;           // Cycle
    float phase_ = 0.0f;     // [0.0, 1.0) across entire waypoint sequence
    int direction_ = 1;      // +1 or -1 for PingPong
    double sampleRate_ = 44100.0;

    Krate::DSP::OnePoleSmoother speedSmoother_;
    Krate::DSP::OnePoleSmoother depthSmoother_;

    float speed_ = 0.1f;     // Hz
    float depth_ = 0.5f;

    Krate::DSP::Xorshift32 rng_{12345};

    // Cached waypoint indices (populated slots only)
    std::array<int, 8> waypointIndices_{};
    int numWaypoints_ = 0;
};
```

**Evolution phase advancement**:
```
phaseIncrement = speed / sampleRate  (per sample)
// Advanced by blockSize samples per process() block via advancePhase()

// Cycle mode: phase wraps at 1.0 -> 0.0
// PingPong mode: phase bounces at 0.0 and 1.0 (direction flips)
// Random Walk: phase += rng_.nextFloat() * stepSize * blockSize/sampleRate
//   where stepSize = speed * depth, clamped to [0, 1]
```

**Waypoint interpolation**:
```
// Map phase [0, 1] to waypoint pair:
// With N waypoints, phase maps to segment:
//   segmentFloat = phase * N  (Cycle) or phase * (N-1) (PingPong/RandomWalk)
//   segmentIndex = floor(segmentFloat)
//   t = segmentFloat - segmentIndex
//   interpolate between waypoints[segmentIndex] and waypoints[segmentIndex+1]

// FR-021: Final position = clamp(evolutionPosition + manualMorphOffset, 0, 1)
//   where manualMorphOffset = morphPosition - 0.5
```

**Component-matching for unequal partial counts** (FR-019): Uses `lerpHarmonicFrame()` and `lerpResidualFrame()` after converting snapshots to frames via `recallSnapshotToFrame()`. These functions already handle partial count mismatches (missing partials interpolate with zero amplitude).

---

### 5. HarmonicBlender (Plugin-Local)

**Location**: `plugins/innexus/src/dsp/harmonic_blender.h`
**Namespace**: `Innexus`

```cpp
class HarmonicBlender {
public:
    /// @brief Compute blended frame from weighted memory slots + optional live source.
    /// @param slots Array of 8 memory slots
    /// @param weights Array of 8 per-slot weights (0.0-1.0 each)
    /// @param liveWeight Weight for live analysis source (0.0-1.0)
    /// @param liveFrame Current live analysis frame (may be nullptr if liveWeight==0)
    /// @param liveResidual Current live residual frame (may be nullptr)
    /// @param outFrame Output blended harmonic frame
    /// @param outResidual Output blended residual frame
    /// @return true if blending produced a valid frame, false if all weights zero
    [[nodiscard]] bool process(
        const std::array<Krate::DSP::MemorySlot, 8>& slots,
        const std::array<float, 8>& weights,
        float liveWeight,
        const Krate::DSP::HarmonicFrame* liveFrame,
        const Krate::DSP::ResidualFrame* liveResidual,
        Krate::DSP::HarmonicFrame& outFrame,
        Krate::DSP::ResidualFrame& outResidual) noexcept;

    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;

private:
    bool enabled_ = false;
};
```

**Blending algorithm** (FR-037):
```
1. Compute total weight = sum of all active weights (occupied slots + live)
2. If totalWeight == 0: return false (silence)
3. Normalize: effectiveWeight_i = weight_i / totalWeight
4. For each partial index 0..maxPartials:
     blendedAmp_n = sum(effectiveWeight_i * source_i.normalizedAmp_n)
     blendedRelFreq_n = sum(effectiveWeight_i * source_i.relativeFreq_n)
       (missing partials contribute 0 amplitude, harmonicIndex for relFreq)
5. For each residual band 0..15:
     blendedBand_k = sum(effectiveWeight_i * source_i.residualBand_k)
6. blendedResidualEnergy = sum(effectiveWeight_i * source_i.residualEnergy)
```

---

### 6. Cross-Synthesis Timbral Blend (Processor-Level)

**Location**: Inline in `plugins/innexus/src/processor/processor.cpp` (not a separate class)

The cross-synthesis blend operates on the harmonic frame before it enters the oscillator bank. It interpolates between a **pure harmonic reference** (compile-time constant) and the source model.

**Pure harmonic reference computation** (FR-004):
```cpp
// Computed once in setupProcessing() or prepare():
// For partial n (1-based):
//   pureRelativeFreq_n = n (integer harmonic)
//   pureAmp_n = 1.0 / n (natural harmonic rolloff)
//   pureDeviation_n = 0.0 (no inharmonicity)
//
// L2-normalize the amplitude array:
//   sumSquares = sum(1/n^2) for n=1..kMaxPartials
//   invNorm = 1.0 / sqrt(sumSquares)
//   pureNormalizedAmp_n = (1.0/n) * invNorm
//
// Pre-computed constant:
// sum(1/n^2, n=1..48) = 1.6251... (Basel problem partial sum)

// At prepare-time, stored in pureHarmonicReference_:
for (int n = 1; n <= kMaxPartials; ++n) {
    pureReference_.relativeFreqs[n-1] = float(n);
    pureReference_.normalizedAmps[n-1] = (1.0f / float(n));
    pureReference_.inharmonicDeviation[n-1] = 0.0f;
}
pureReference_.numPartials = kMaxPartials;
// Then L2-normalize:
float sumSq = 0.0f;
for (int i = 0; i < kMaxPartials; ++i)
    sumSq += pureReference_.normalizedAmps[i] * pureReference_.normalizedAmps[i];
float invNorm = 1.0f / std::sqrt(sumSq);
for (int i = 0; i < kMaxPartials; ++i)
    pureReference_.normalizedAmps[i] *= invNorm;
```

**Timbral blend application** (FR-002):
```cpp
// Applied per frame, before oscillator bank loadFrame():
// For each partial n:
//   effectiveRelFreq_n = lerp(pureRelFreq_n, sourceRelFreq_n, timbralBlend)
//   effectiveAmp_n = lerp(pureNormAmp_n, sourceNormAmp_n, timbralBlend)
//   effectiveDeviation_n = sourceDeviation_n * timbralBlend * inharmonicityAmount

// Implementation: reconstruct pureFrame from pureReference_, then
// use lerpHarmonicFrame(pureFrame, sourceFrame, timbralBlend)
```

---

## Integration Points

### processor.h Additions

**New atomic parameters** (31 total):
```cpp
// M6 Cross-Synthesis (600)
std::atomic<float> timbralBlend_{1.0f};        // 0.0-1.0, default 1.0

// M6 Stereo Spread (601)
std::atomic<float> stereoSpread_{0.0f};        // 0.0-1.0, default 0.0

// M6 Evolution Engine (602-605)
std::atomic<float> evolutionEnable_{0.0f};     // 0/1
std::atomic<float> evolutionSpeed_{0.1f};      // Hz (smoothed via engine)
std::atomic<float> evolutionDepth_{0.5f};      // 0.0-1.0
std::atomic<float> evolutionMode_{0.0f};       // 0-2

// M6 Modulator 1 (610-616)
std::atomic<float> mod1Enable_{0.0f};
std::atomic<float> mod1Waveform_{0.0f};
std::atomic<float> mod1Rate_{1.0f};            // Hz
std::atomic<float> mod1Depth_{0.0f};
std::atomic<float> mod1RangeStart_{0.0f};      // normalized (1-48)
std::atomic<float> mod1RangeEnd_{1.0f};        // normalized (1-48)
std::atomic<float> mod1Target_{0.0f};          // 0-2

// M6 Modulator 2 (620-626)
std::atomic<float> mod2Enable_{0.0f};
std::atomic<float> mod2Waveform_{0.0f};
std::atomic<float> mod2Rate_{1.0f};
std::atomic<float> mod2Depth_{0.0f};
std::atomic<float> mod2RangeStart_{0.0f};
std::atomic<float> mod2RangeEnd_{1.0f};
std::atomic<float> mod2Target_{0.0f};

// M6 Detune Spread (630)
std::atomic<float> detuneSpread_{0.0f};        // 0.0-1.0

// M6 Multi-Source Blend (640-649)
std::atomic<float> blendEnable_{0.0f};
std::array<std::atomic<float>, 8> blendWeights_{}; // each 0.0-1.0, default 0.0
std::atomic<float> blendLiveWeight_{0.0f};
```

**New DSP members**:
```cpp
// M6 Cross-Synthesis
Krate::DSP::OnePoleSmoother timbralBlendSmoother_;
Krate::DSP::HarmonicSnapshot pureHarmonicReference_;  // Pre-computed in setupProcessing

// M6 Evolution Engine
EvolutionEngine evolutionEngine_;

// M6 Harmonic Modulators
HarmonicModulator harmonicMod1_;
HarmonicModulator harmonicMod2_;
std::array<float, Krate::DSP::kMaxPartials> modPanOffsets_{};  // Accumulated pan offsets from modulators

// M6 Stereo Spread
Krate::DSP::OnePoleSmoother stereoSpreadSmoother_;

// M6 Detune Spread
Krate::DSP::OnePoleSmoother detuneSpreadSmoother_;

// M6 Multi-Source Blend
HarmonicBlender harmonicBlender_;
std::array<Krate::DSP::OnePoleSmoother, 8> blendWeightSmoothers_;
Krate::DSP::OnePoleSmoother blendLiveWeightSmoother_;
```

### process() Integration

The process() method's per-block frame processing section (currently lines ~643-711 in processor.cpp) must be restructured. The new pipeline order (FR-049):

```
1. [Source Selection]
   if (blendEnabled):
     harmonicBlender_.process(slots, weights, liveWeight, liveFrame, liveResidual, frame, residual)
   else if (manualFreezeActive_ && morphPosition > 0):
     existing morph interpolation between frozen and live
   else:
     existing single-source pass-through

2. [Cross-Synthesis Timbral Blend] (FR-001, FR-002)
   if (timbralBlend < 1.0 - epsilon):
     blend frame with pureHarmonicReference

3. [Evolution Engine] (FR-014, FR-022)
   if (evolutionEnabled && !blendEnabled):
     evolutionEngine_.process(slots, manualOffset, frame, residual)
     // This REPLACES the frame from step 1

4. [Harmonic Filter] (existing M4)
   if (filterType != AllPass):
     applyHarmonicMask(frame, filterMask_)

5. [Harmonic Modulators] (FR-024)
   if (mod1 or mod2 enabled):
     harmonicMod1_.process(frame, modPanOffsets_)
     harmonicMod2_.process(frame, modPanOffsets_)

6. [Load frame into oscillator bank]
   oscillatorBank_.loadFrame(frame, targetPitch)
   oscillatorBank_.setStereoSpread(smoothedSpread)
   oscillatorBank_.setDetuneSpread(smoothedDetune)
```

And in the per-sample loop:
```cpp
// Replace:
//   float harmonicSample = oscillatorBank_.process();
//   out[0][s] = sample; out[1][s] = sample;
// With:
float leftSample = 0.0f, rightSample = 0.0f;
oscillatorBank_.processStereo(leftSample, rightSample);
float residualSample = hasResidual ? residualSynth_.process() : 0.0f;

float harmLevel = harmonicLevelSmoother_.process();
float resLevel = residualLevelSmoother_.process();

// Residual is mono center (FR-012)
float leftMix = leftSample * harmLevel + residualSample * resLevel;
float rightMix = rightSample * harmLevel + residualSample * resLevel;

// ... crossfade, velocity, release, gain ...

out[0][s] = leftMix;
out[1][s] = rightMix;
```

### State Save/Load (FR-044)

**State version**: Increment from 5 to 6.

**Backward compatibility**: When loading version 5 state, all M6 parameters initialize to defaults (timbralBlend=1.0, stereoSpread=0.0, evolutionEnable=0, etc.).

**State format additions** (appended after M5 data):
```
// M6 block (only present in version >= 6)
float timbralBlend
float stereoSpread
int8  evolutionEnable
float evolutionSpeed
float evolutionDepth
int32 evolutionMode
int8  mod1Enable
int32 mod1Waveform
float mod1Rate
float mod1Depth
int32 mod1RangeStart   // plain value 1-48
int32 mod1RangeEnd
int32 mod1Target
int8  mod2Enable
int32 mod2Waveform
float mod2Rate
float mod2Depth
int32 mod2RangeStart
int32 mod2RangeEnd
int32 mod2Target
float detuneSpread
int8  blendEnable
float blendWeight[8]   // 8 floats
float blendLiveWeight
```

### processParameterChanges() Integration

Add `case` entries for all 31 new parameter IDs following the existing pattern. Each stores a clamped float into the corresponding atomic.

For parameters with denormalization (evolution speed, modulator rate/range):
- Evolution Speed: RangeParameter handles plain/normalized conversion (0.01-10.0 Hz)
- Mod Rate: RangeParameter handles plain/normalized conversion (0.01-20.0 Hz)
- Mod Range Start/End: RangeParameter with stepCount=47 gives integer 1-48

### controller.cpp Integration

Register all 31 parameters using `RangeParameter` and `StringListParameter` as specified in the spec's parameter ID table. Follow the existing registration pattern in `Controller::initialize()`.

---

## Testing Strategy

### Unit Tests

| Test File | Tests | Coverage |
|-----------|-------|----------|
| `harmonic_oscillator_bank_tests.cpp` (modified) | processStereo mono equivalence (SC-010), stereo spread at 0/50/100%, detune spread, fundamental stays centered (FR-009), pan smoothing | FR-006 to FR-013, FR-030 to FR-032, SC-002, SC-005, SC-010 |
| `creative_extensions_tests.cpp` (new) | Timbral blend 0/0.5/1.0, pure harmonic reference L2 norm, source switching crossfade, evolution engine modes, waypoint interpolation, modulator amplitude/freq/pan, overlapping modulators, blender weights, blender normalization | FR-001 to FR-005, FR-014 to FR-023, FR-024 to FR-029, FR-034 to FR-042, SC-001, SC-003, SC-004, SC-006 |
| `stereo_spread_tests.cpp` (new) | Integration: stereo spread + harmonic filter interaction (US2-SC6), stereo spread + detune, stereo + residual mono center | FR-006+filter, FR-007+residual, SC-002 |
| `creative_extensions_vst_tests.cpp` (new) | Parameter registration, state save/load round-trip (SC-009), backward compat with v5 state, parameter change handling | FR-043, FR-044, SC-009 |

### Performance Tests

| Test | Target | Method |
|------|--------|--------|
| SC-008: CPU budget | < 1.0% additional, < 2.0% total | Process 10 seconds at 44.1 kHz with all features active. Measure wall-clock time vs buffer duration. |
| SC-007: Click-free transitions | No discontinuities > -80 dBFS | Sweep all parameters at max automation rate over 1 second. Check for sample-level jumps > threshold. |

### Key Acceptance Tests

| Scenario | SC | Method |
|----------|-----|--------|
| Timbral Blend correlation | SC-001 | At blend=1.0, compute correlation between output spectrum and source model > 0.95 |
| Stereo decorrelation | SC-002 | At spread=1.0, measure 1 - cross-correlation between L and R > 0.8 |
| Evolution centroid variation | SC-003 | 10s sustained note, evolving between dark/bright snapshots, std(centroid) > 100 Hz |
| Modulator depth accuracy | SC-004 | Measure amplitude modulation depth within +/-5% of configured depth |
| Detune bandwidth | SC-005 | At detune=1.0, measure increased spectral bandwidth vs detune=0.0 |
| Blend centroid accuracy | SC-006 | 2 sources at 50/50, output centroid within +/-10% of mean |
| Mono equivalence | SC-010 | At spread=0, verify left == right (bit-identical) |
| Single-source blend | SC-011 | Single source weight=1.0 matches direct recall within float tolerance |

---

## Risk Analysis

### High Risk

1. **HarmonicOscillatorBank stereo extension** (Layer 2 public API change)
   - **Risk**: Breaking existing tests or callers
   - **Mitigation**: New `processStereo()` is additive; existing `process()` unchanged. All existing tests continue to pass. New tests cover stereo-specific behavior.
   - **Verification**: Run `dsp_tests` after oscillator bank changes, before any other work.

2. **Process loop restructuring** (processor.cpp ~900 lines)
   - **Risk**: Introducing bugs in the existing pipeline when adding new stages
   - **Mitigation**: Incremental integration -- add one feature at a time, test after each. Keep existing behavior when all new features are at defaults.
   - **Verification**: All existing `innexus_tests` must pass after each integration step.

### Medium Risk

3. **Parameter count explosion** (31 new params)
   - **Risk**: State save/load regression, parameter change handler bugs
   - **Mitigation**: State version bump to 6; backward compat tested explicitly. Systematic parameter registration and handling following existing patterns.

4. **Evolution + Morph interaction** (FR-021, FR-022)
   - **Risk**: Ambiguous behavior when both evolution and manual morph are active
   - **Mitigation**: Spec is clear: evolution position + manual offset, clamped. Multi-source blend takes priority over evolution (FR-052).

### Low Risk

5. **ModulatorLfo lightweight implementation**
   - **Risk**: Numerical inaccuracy in formula-based waveforms
   - **Mitigation**: Standard trig functions; tested against known values.

6. **Cross-synthesis pure reference** (FR-004)
   - **Risk**: Normalization mismatch between pure reference and source snapshots
   - **Mitigation**: Both use L2 normalization. Pure reference computed at prepare-time, verified in unit test.

---

## Complexity Tracking

No constitution violations detected. All design decisions conform to the established architecture.

| Decision | Rationale |
|----------|-----------|
| Lightweight ModulatorLfo instead of reusing Layer 1 LFO | Layer 1 LFO uses `std::vector` wavetables allocated in `prepare()`. Modulators need zero-allocation formula-based LFO. No constitution violation -- this is a different component serving a different purpose. |
| Cross-synthesis blend in processor (not separate class) | Simple lerp between two frames; does not warrant a standalone class. Implemented as inline logic in the process pipeline. |
