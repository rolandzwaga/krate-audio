# Implementation Plan: Bow Model Exciter

**Branch**: `130-bow-model-exciter` | **Date**: 2026-03-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/130-bow-model-exciter/spec.md`

## Summary

Implement a `BowExciter` Layer 2 DSP processor that models stick-slip friction for continuous physical modelling excitation. The component uses the STK power-law friction model with Schelleng/Guettler-aware dynamics, energy-aware gain control, and integrates with both WaveguideString and ModalResonatorBank resonators. Additionally, unify all exciter interfaces to `process(float feedbackVelocity)`, add 8 bowed-mode bandpass velocity taps to ModalResonatorBank, relocate WaveguideString's DC blocker, and expose 4 new VST parameters (pressure, speed, position, oversampling).

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: KrateDSP (Layer 0/1 primitives: OnePoleLP, LFO), VST3 SDK 3.7.x+
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 (dsp_tests, innexus_tests) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang), Linux (GCC) -- cross-platform
**Project Type**: Monorepo -- shared DSP library + plugin
**Performance Goals**: BowExciter per-sample: ~4 muls + 1 fabs + 1 clamp for friction core (SC-013). Total plugin < 5% single core @ 44.1 kHz stereo
**Constraints**: Zero allocations in audio thread; all processing noexcept; 1-sample feedback coupling
**Scale/Scope**: 1 new class (BowExciter), 3 modified classes, ~4 modified plugin files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] BowExciter: all methods noexcept, no allocations in process(), no locks
- [x] LFO and OnePoleLP are pre-allocated in prepare(), no per-sample allocations
- [x] Energy tracking uses simple EMA (no std::pow or transcendentals in hot path)

**Required Check - Principle IX (Layered Architecture):**
- [x] BowExciter at Layer 2 depends only on Layer 0/1 (OnePoleLP, LFO)
- [x] No circular dependencies introduced
- [x] ModalResonatorBank extension stays at Layer 2 (internal biquad filters)

**Required Check - Principle X (DSP Processing):**
- [x] Oversampling (2x) provided for friction nonlinearity (FR-022)
- [x] DC blocking via existing dcBlocker_ in WaveguideString (FR-021)
- [x] Feedback > 100% not applicable (friction model is self-limiting)

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle VI (Cross-Platform):**
- [x] No platform-specific APIs used
- [x] All DSP is pure C++20
- [x] No VSTGUI changes in this spec

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: BowExciter, BowedModeBPF (internal struct)

| Planned Type | Search Result | Existing? | Action |
|--------------|---------------|-----------|--------|
| BowExciter | `grep -r "class BowExciter" dsp/ plugins/` -- 0 results | No | Create New |
| BowedModeBPF | `grep -r "BowedModeBPF" dsp/ plugins/` -- 0 results | No | Create New (internal to ModalResonatorBank) |

**Utility Functions to be created**: None (all utilities reused from existing components)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OnePoleLP | `dsp/include/krate/dsp/primitives/one_pole.h` | 1 | Bow hair width LPF at 8 kHz (FR-009) |
| LFO | `dsp/include/krate/dsp/primitives/lfo.h` | 1 | Rosin character slow drift at 0.7 Hz (FR-008) |
| DCBlocker | `dsp/include/krate/dsp/primitives/dc_blocker.h` | 1 | Existing in WaveguideString, relocate only (FR-021) |
| IResonator | `dsp/include/krate/dsp/processors/iresonator.h` | 2 | getFeedbackVelocity() interface (already exists) |
| ImpactExciter | `dsp/include/krate/dsp/processors/impact_exciter.h` | 2 | Refactor process() signature (FR-016) |
| ResidualSynthesizer | `dsp/include/krate/dsp/processors/residual_synthesizer.h` | 2 | Refactor process() signature (FR-015) |
| ModalResonatorBank | `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | 2 | Extend with bowed-mode taps (FR-020) |
| WaveguideString | `dsp/include/krate/dsp/processors/waveguide_string.h` | 2 | Relocate DC blocker (FR-021) |
| ADSREnvelope | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | 1 | Drives bow acceleration (existing in InnexusVoice) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (impact_exciter, modal_resonator_bank, waveguide_string, iresonator, residual_synthesizer)
- [x] `plugins/innexus/src/` - InnexusVoice, plugin_ids, processor

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: BowExciter is a completely new class with no existing counterpart. BowedModeBPF is an internal struct scoped within ModalResonatorBank. No name collisions found in codebase search.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| OnePoleLP | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| OnePoleLP | setCutoff | `void setCutoff(float cutoffHz) noexcept` | Yes |
| OnePoleLP | process | `float process(float input) noexcept` | Yes |
| OnePoleLP | reset | `void reset() noexcept` | Yes |
| LFO | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| LFO | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| LFO | setWaveform | `void setWaveform(LFOWaveform waveform) noexcept` | Yes |
| LFO | process | `float process() noexcept` | Yes |
| LFO | retrigger | `void retrigger() noexcept` | Yes |
| LFO | reset | `void reset() noexcept` | Yes |
| IResonator | getFeedbackVelocity | `virtual float getFeedbackVelocity() const noexcept { return 0.0f; }` | Yes |
| IResonator | getControlEnergy | `virtual float getControlEnergy() const noexcept = 0` | Yes |
| ImpactExciter | process | `float process() noexcept` (BEFORE refactor) | Yes |
| ImpactExciter | processBlock | `void processBlock(float* output, int numSamples) noexcept` | Yes |
| ResidualSynthesizer | process | `[[nodiscard]] float process() noexcept` (BEFORE refactor) | Yes |
| WaveguideString | getFeedbackVelocity | `float getFeedbackVelocity() const noexcept override` | Yes |
| ModalResonatorBank | getFeedbackVelocity | `float getFeedbackVelocity() const noexcept override` | Yes |
| ModalResonatorBank | processSample | `float processSample(float input) noexcept` | Yes |
| ModalResonatorBank | kMaxModes | `static constexpr int kMaxModes = 96` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz) noexcept` | Yes |
| DCBlocker | process | `float process(float input) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/one_pole.h` - OnePoleLP, OnePoleHP
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/processors/iresonator.h` - IResonator interface
- [x] `dsp/include/krate/dsp/processors/impact_exciter.h` - ImpactExciter class
- [x] `dsp/include/krate/dsp/processors/residual_synthesizer.h` - ResidualSynthesizer class
- [x] `dsp/include/krate/dsp/processors/waveguide_string.h` - WaveguideString class
- [x] `dsp/include/krate/dsp/processors/modal_resonator_bank.h` - ModalResonatorBank class
- [x] `plugins/innexus/src/processor/innexus_voice.h` - InnexusVoice struct
- [x] `plugins/innexus/src/plugin_ids.h` - ExciterType enum, parameter IDs

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ImpactExciter | `process()` has NO parameters currently | Must refactor to `process(float)` before BowExciter integration |
| ResidualSynthesizer | `process()` has NO parameters currently | Must refactor to `process(float)` |
| LFO | 208 bytes per instance (wavetable-based) | Acceptable for per-voice use at 0.7 Hz |
| OnePoleLP | `prepare()` takes `double sampleRate` not `float` | Use `double` for sampleRate argument |
| DCBlocker | `prepare()` takes both sampleRate AND cutoffHz | `dcBlocker_.prepare(sampleRate, 20.0f)` |
| ModalResonatorBank | `processSample` has 2 overloads (1 with decay scale) | Use the overload matching current calling convention |
| ExciterType::Bow | Already exists at index 2 in plugin_ids.h | No enum change needed |
| kExciterTypeId | Already exists at 805 | No ID conflict |
| WaveguideString::dcBlocker_ | Is `DCBlocker` type (not `OnePoleHP`) | Uses `DCBlocker::process()` not `OnePoleHP::process()` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | No new Layer 0 utilities needed | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| bowTable (friction computation) | Domain-specific to BowExciter, unlikely to be reused |
| positionImpedance | Specific to bow-string coupling geometry |
| energyGain | Could be shared but wait for 2nd consumer |

**Decision**: No Layer 0 extractions. All new computations are domain-specific to the bow model. The energy-aware gain control pattern is noted as a potential future extraction if body resonance or sympathetic resonance needs it.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Bow velocity integrates from acceleration; friction depends on resonator feedback velocity (1-sample loop) |
| **Data parallelism width** | 1 (per voice) | Each voice has independent bow state but processing is inherently serial within a voice |
| **Branch density in inner loop** | LOW | Core is branchless: clamp + multiply chain. Energy check is a single conditional |
| **Dominant operations** | Arithmetic (4 muls, 1 fabs, 1 clamp) | No transcendentals in hot path (pow replaced with x*x*x*x) |
| **Current CPU budget vs expected usage** | <0.5% per layer-2 target vs ~0.1% expected | Well under budget |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The BowExciter's per-sample computation is inherently serial due to the 1-sample feedback loop between bow velocity and resonator velocity. The friction computation is only ~10 arithmetic ops per sample with no parallelizable dimension within a single voice. Multi-voice SIMD could theoretically process 4 voices simultaneously, but the feedback coupling with different resonators makes this impractical. The algorithm is already well under CPU budget at ~0.1% per voice.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Replace `1/(x*x*x*x)` with reciprocal approx | ~5-10% on friction core | LOW | DEFER (already fast enough) |
| 256-entry bow table lookup | ~20% on friction core | LOW | DEFER (spec notes as optional) |
| Skip processing when bowVelocity == 0 | 100% for inactive bows | LOW | YES (early-out) |
| Gordon-Smith phasor instead of LFO for rosin | ~save 200 bytes per voice | LOW | DEFER (memory not critical) |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from roadmap):
- Phase 5: Body Resonance (Layer 2 processor for sympathetic body coupling)
- Phase 6: Sympathetic Resonance (Layer 3 system for cross-string sympathetic vibration)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Unified exciter interface `process(float)` | HIGH | All future exciter types | Keep as convention (no base class) |
| Energy-aware gain control pattern | MEDIUM | Body resonance, sympathetic resonance | Keep local, extract after 2nd use |
| Bowed-mode bandpass tap pattern | MEDIUM | Sympathetic resonance (partial excitation) | Keep in ModalResonatorBank, extend if needed |
| Friction jitter pattern (LFO + noise) | LOW | Possibly blown-tube/reed models | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No IExciter virtual base class | Virtual calls in audio thread violate Principle IV; 3 exciter types is manageable with switch |
| Keep energy control in BowExciter | Only one consumer; extract to utility if body resonance needs similar pattern |
| Keep BowedModeBPF internal to ModalResonatorBank | Tightly coupled to mode frequencies and state |

### Review Trigger

After implementing **Phase 5 (Body Resonance)**, review this section:
- [ ] Does body resonance need energy-aware gain control? If yes, extract to shared utility
- [ ] Does body resonance use bandpass velocity taps? If yes, generalize BowedModeBPF
- [ ] Any duplicated code between BowExciter and body resonance? Consider shared pattern

## Project Structure

### Documentation (this feature)

```text
specs/130-bow-model-exciter/
+-- plan.md              # This file
+-- spec.md              # Feature specification
+-- research.md          # Phase 0 research findings
+-- data-model.md        # Phase 1 entity design
+-- quickstart.md        # Phase 1 implementation guide
+-- contracts/           # Phase 1 API contracts
|   +-- bow_exciter_api.h
|   +-- unified_exciter_interface.md
|   +-- modal_bowed_modes.md
+-- checklists/          # Pre-existing
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- processors/
|       +-- bow_exciter.h              # NEW: BowExciter class (Layer 2)
|       +-- impact_exciter.h           # MODIFIED: process(float) signature
|       +-- residual_synthesizer.h     # MODIFIED: process(float) signature
|       +-- modal_resonator_bank.h     # MODIFIED: bowed-mode taps
|       +-- waveguide_string.h         # MODIFIED: DC blocker relocation
+-- tests/unit/
    +-- processors/
        +-- bow_exciter_test.cpp       # NEW: BowExciter unit tests
        +-- impact_exciter_test.cpp    # MODIFIED: update process() calls
        +-- residual_synthesizer_tests.cpp  # MODIFIED: update process() calls

plugins/innexus/
+-- src/
|   +-- plugin_ids.h                   # MODIFIED: add kBowPressureId, etc.
|   +-- processor/
|       +-- innexus_voice.h            # MODIFIED: add bowExciter field
|       +-- processor.cpp              # MODIFIED: bow exciter dispatch
|       +-- processor_midi.cpp         # MODIFIED: bow trigger on note-on
|       +-- processor_params.cpp       # MODIFIED: bow parameter handling
|       +-- processor_state.cpp        # MODIFIED: save/load bow params
|   +-- controller/
|       +-- controller.cpp             # MODIFIED: register bow parameters
+-- tests/
    +-- unit/processor/                # MODIFIED: add bow-related tests
```

**Structure Decision**: Follows existing monorepo layout. New BowExciter at Layer 2 (processors/) alongside existing ImpactExciter. No new directories needed.

## Complexity Tracking

No constitution violations. All design decisions comply with the constitution:
- Layer 2 for BowExciter (correct: depends only on Layer 0/1)
- No virtual base class for exciters (avoids Principle IV virtual call prohibition)
- DC blocker reuse (Principle XIV: no duplication)
- Scalar-first, SIMD not beneficial (Principle IV: SIMD analysis completed)
