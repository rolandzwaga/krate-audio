# Implementation Plan: Sympathetic Resonance

**Branch**: `132-sympathetic-resonance` | **Date**: 2026-03-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/132-sympathetic-resonance/spec.md`

## Summary

Implement a global shared resonance field for the Innexus synthesizer that models sympathetic string vibrations. The system manages a pool of up to 64 second-order driven resonators tuned to the union of all active voices' low-order partials (4 per voice). Fed by the global voice sum (post polyphony gain compensation, pre master gain), the resonators produce cross-voice harmonic reinforcement that is strongest for consonant intervals. An anti-mud filter (output HPF + frequency-dependent Q) prevents low-frequency buildup on dense chords. Two user parameters control coupling amount (-40 to -20 dB) and resonator Q (100-1000). Google Highway provides SIMD acceleration for the resonator loop (Phase 2, after scalar Phase 1).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP (Layer 0-2), Google Highway (SIMD, PRIVATE link), Steinberg VST3 SDK
**Storage**: N/A
**Testing**: Catch2 (via dsp_tests and innexus_tests targets)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: VST3 plugin monorepo (Innexus instrument plugin + KrateDSP shared library)
**Performance Goals**: ~56-80 ops/sample with 4-wide SIMD for 32 resonators; zero CPU when bypassed (Amount=0.0)
**Constraints**: Real-time audio thread (no allocation, no locks, no exceptions); zero allocation in process(); total plugin < 5% CPU @ 44.1 kHz
**Scale/Scope**: 1 new Layer 3 system class + 1 SIMD kernel file + plugin integration across 6 files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] SympatheticResonance is a DSP component, not a VST3 component -- no separation concerns
- [x] Parameters registered in Controller, atomics in Processor, DSP in KrateDSP library

**Required Check - Principle II (Real-Time Safety):**
- [x] Fixed-size `std::array<ResonatorState, 64>` pool -- zero dynamic allocation
- [x] All pool management (add/merge/evict/reclaim) operates on the fixed array
- [x] No locks, exceptions, or I/O in any audio-thread code path
- [x] OnePoleSmoother for parameter smoothing (no allocation)

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability evaluated: BENEFICIAL (see SIMD Optimization Analysis below)
- [x] Scalar-first workflow: Phase 1 scalar + tests, Phase 2 SIMD behind same API

**Required Check - Principle IX (Layered Architecture):**
- [x] SympatheticResonance at Layer 3 (systems/) -- depends on Layer 0 (math) and Layer 1 (Biquad, Smoother)
- [x] No Layer 4 dependencies; no circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] All 23 FR-xxx and 15 SC-xxx requirements will be individually verified

**Post-Design Re-Check**: All checks PASS. No constitution violations.

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Performed | Existing? | Action |
|--------------|-----------------|-----------|--------|
| `SympatheticResonance` | Searched via spec pre-check | No | Create New at `dsp/include/krate/dsp/systems/sympathetic_resonance.h` |
| `SympatheticPartialInfo` | Searched codebase | No | Create New (nested in same header) |
| `ResonatorState` | Name exists? No in Krate::DSP namespace | No | Create New (private struct inside SympatheticResonance) |

**Utility Functions to be created**:

| Planned Function | Search Performed | Existing? | Location | Action |
|------------------|-----------------|-----------|----------|--------|
| `processSympatheticBankSampleSIMD` | No match in codebase | No | `sympathetic_resonance_simd.h/cpp` | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `Biquad` | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | Output high-pass anti-mud filter |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Coupling amount parameter smoothing |
| `BiquadDesign::highPass1` | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | HPF coefficient computation for anti-mud filter |
| `ModalResonatorBankSIMD` pattern | `dsp/include/krate/dsp/processors/modal_resonator_bank_simd.cpp` | 2 | Reference for Highway SIMD kernel pattern |
| `PhysicalModelMixer` pattern | `plugins/innexus/src/dsp/physical_model_mixer.h` | Plugin-local | Reference for signal chain integration pattern |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (Biquad, Smoother confirmed)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (ModalResonator, BodyResonance confirmed as reference only)
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (no conflicting names)
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter IDs 860, 861 confirmed unused

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: `SympatheticResonance` class does not exist anywhere in the codebase. `SympatheticPartialInfo` and `ResonatorState` are unique names. No namespace conflicts in `Krate::DSP`. Parameter IDs 860 and 861 are unused.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `Biquad` | constructor | `Biquad() noexcept = default` | Yes |
| `Biquad` | `configure` | `void configure(FilterType type, float frequency, float Q, float gain, float sampleRate) noexcept` | Yes |
| `Biquad` | `process` | `float process(float input) noexcept` | Yes |
| `Biquad` | `reset` | `void reset() noexcept` | Yes |
| `OnePoleSmoother` | constructor | `OnePoleSmoother() noexcept` (default-constructs to 0) | Yes |
| `OnePoleSmoother` | `configure` | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| `OnePoleSmoother` | `setTarget` | `void setTarget(float newTarget) noexcept` | Yes |
| `OnePoleSmoother` | `process` | `float process() noexcept` (returns next smoothed value) | Yes |
| `OnePoleSmoother` | `getCurrentValue` | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| `OnePoleSmoother` | `snapTo` | `void snapTo(float value) noexcept` | Yes |
| `OnePoleSmoother` | `isComplete` | `[[nodiscard]] bool isComplete() const noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class (TDF2, FilterType enum, configure method)
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class (configure, process, snapTo)
- [x] `dsp/include/krate/dsp/processors/modal_resonator_bank_simd.h` - SIMD kernel public API pattern
- [x] `dsp/include/krate/dsp/processors/modal_resonator_bank_simd.cpp` - Highway self-inclusion pattern
- [x] `plugins/innexus/src/processor/processor.h` - Processor class members, voice array
- [x] `plugins/innexus/src/processor/processor.cpp` - process() signal chain (lines 1866-1905)
- [x] `plugins/innexus/src/processor/processor_params.cpp` - Parameter handling pattern
- [x] `plugins/innexus/src/processor/processor_state.cpp` - State save/load pattern
- [x] `plugins/innexus/src/processor/processor_midi.cpp` - handleNoteOn/Off pattern
- [x] `plugins/innexus/src/processor/innexus_voice.h` - InnexusVoice struct (midiNote, pitchBendSemitones)
- [x] `plugins/innexus/src/controller/controller.cpp` - Parameter registration pattern (RangeParameter)
- [x] `plugins/innexus/src/plugin_ids.h` - ParameterIds enum (IDs 860, 861 available)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `OnePoleSmoother` | Default constructor initializes to 0.0 at 44100 Hz | Must call `configure(timeMs, sampleRate)` in `prepare()` |
| `OnePoleSmoother` | `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| `Biquad` | `configure()` takes `FilterType` enum, not string | `biquad.configure(FilterType::HighPass, freq, Q, 0.0f, sampleRate)` |
| `Processor::process()` | Voice sum uses `sampleL += vL; sampleR += vR` at line 1866 | Insert sympathetic processing after polyphony gain comp at line 1877 |
| State save/load | Uses `streamer.writeFloat()` / reads with `streamer.readFloat()` | Must add reads in same order as writes, with backward compat defaults |
| Parameter handling | Atomics use `std::memory_order_relaxed` for all param loads/stores | Consistent with all existing parameter patterns in Innexus |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| --- | --- | --- | --- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `computeResonatorCoeffs(f, Q, sampleRate)` | Specific to driven resonator recurrence; only used by SympatheticResonance |
| `computeFreqDependentQ(Q_user, f)` | Anti-mud Q scaling; one consumer |

**Decision**: No Layer 0 extractions needed. All new utility functions are specific to the sympathetic resonance use case.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES (per-resonator) | Each resonator has y[n-1], y[n-2] time-domain feedback. But resonators are independent of each other -- no cross-resonator feedback. |
| **Data parallelism width** | 32-64 resonators | Excellent parallelism. With 4-wide SIMD: 8-16 iterations. With 8-wide AVX2: 4-8 iterations. |
| **Branch density in inner loop** | LOW | Per-sample loop is branchless: multiply-add recurrence for each resonator, then sum. Active/inactive check is outside inner loop. |
| **Dominant operations** | Arithmetic (mul, add) | 2 multiplies + 2 adds per resonator per sample. Plus 1 abs + 1 max for envelope follower. |
| **Current CPU budget vs expected usage** | <5% total plugin budget; sympathetic ~0.5-1% scalar, ~0.15-0.3% SIMD | Clear benefit from SIMD. |

### SIMD Viability Verdict

**Verdict**: BENEFICIAL

**Reasoning**: The resonator pool provides 32-64 independent data streams with a branchless inner loop dominated by multiply-add operations. This is an ideal SIMD workload. The existing `ModalResonatorBankSIMD` provides a proven Highway pattern for exactly this type of cross-mode vectorization. With 4-wide SIMD, the effective cost drops from ~330-490 ops/sample to ~85-125 ops/sample.

### Implementation Workflow

| Phase | What | When | Deliverables |
|-------|------|------|-------------|
| **1. Scalar** | Full SympatheticResonance with scalar resonator loop | `/speckit.implement` Phase 1-3 | Working implementation + complete test suite + CPU baseline |
| **2. SIMD** | `sympathetic_resonance_simd.cpp` Highway kernel behind same API | `/speckit.implement` Phase 4 | SIMD kernel + all tests still pass + CPU improvement measured |

- Phase 2 MUST NOT change the public API -- same `process()` signature
- Phase 2 MUST keep scalar as fallback (Highway handles this via dynamic dispatch)
- Phase 2 MUST re-run full test suite confirming tolerance-matched output

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when Amount=0.0 | 100% savings when bypassed | LOW | YES (FR-014) |
| SoA layout for resonator arrays | Better cache utilization for SIMD | LOW | YES (prepare SoA in Phase 1 for Phase 2 SIMD) |
| Skip envelope follower when pool is below 50% capacity | ~10% savings in low-polyphony scenarios | LOW | DEFER (marginal) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 - Systems

**Related features at same layer** (from physical modelling roadmap):
- Future "Ultra" quality mode (128 resonators, 8-10 partials)
- Optional nonlinear saturation (soft-clip on resonator state)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Driven resonator pool (add/merge/evict/reclaim) | MEDIUM | Ultra mode (same class, just increase constants) | Keep local -- Ultra mode changes only compile-time constants |
| Frequency-dependent Q model | MEDIUM | Other physical modelling components needing soundboard absorption | Keep local -- wait for 2nd consumer |
| SIMD resonator kernel | MEDIUM | Any future driven-resonator use case | Keep local -- generic extraction premature |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep driven resonator pool logic local | Only one consumer (SympatheticResonance); Ultra mode just changes constants |
| Keep frequency-dependent Q model local | No other current consumer; extract if body resonance or future component needs it |
| Use SoA layout from Phase 1 | Prepares for Phase 2 SIMD without API changes; cache-friendly even in scalar |

### Review Trigger

After implementing **Ultra mode** or **nonlinear saturation**, review this section:
- [ ] Does Ultra mode need any structural changes? -> Likely just constant changes
- [ ] Does nonlinear saturation need its own pool management? -> Probably extends existing pool
- [ ] Any duplicated resonator code? -> Consider shared driven-resonator primitive

## Project Structure

### Documentation (this feature)

```text
specs/132-sympathetic-resonance/
+-- plan.md              # This file
+-- research.md          # Phase 0 research findings
+-- data-model.md        # Entity definitions and relationships
+-- quickstart.md        # Implementation quickstart guide
+-- contracts/           # API contracts
|   +-- sympathetic_resonance_api.h
+-- spec.md              # Feature specification
+-- checklists/          # Pre-existing checklists
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- systems/
|       +-- sympathetic_resonance.h          # NEW: Layer 3 system (header-only scalar)
|       +-- sympathetic_resonance_simd.h     # NEW: SIMD kernel public API
|       +-- sympathetic_resonance_simd.cpp   # NEW: Highway SIMD kernel impl
+-- tests/unit/
    +-- systems/
        +-- sympathetic_resonance_test.cpp   # NEW: DSP unit tests

plugins/innexus/
+-- src/
|   +-- plugin_ids.h                         # MODIFY: Add kSympatheticAmountId, kSympatheticDecayId
|   +-- processor/
|   |   +-- processor.h                      # MODIFY: Add member fields
|   |   +-- processor.cpp                    # MODIFY: Signal chain integration
|   |   +-- processor_params.cpp             # MODIFY: Parameter handling
|   |   +-- processor_state.cpp              # MODIFY: State save/load
|   |   +-- processor_midi.cpp               # MODIFY: noteOn/noteOff callbacks
|   +-- controller/
|       +-- controller.cpp                   # MODIFY: Parameter registration
+-- tests/unit/processor/
    +-- sympathetic_resonance_integration_test.cpp  # NEW: Plugin integration tests
```

**Structure Decision**: Follows established monorepo pattern. New DSP component at Layer 3 (systems/), SIMD kernel as separate .cpp with Highway self-inclusion. Plugin integration follows existing Body Resonance (Spec 131) pattern for parameters, state, MIDI, and signal chain.

## Complexity Tracking

No constitution violations to justify. All design decisions align with the 18 constitutional principles.
