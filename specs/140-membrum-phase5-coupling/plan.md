# Implementation Plan: Membrum Phase 5 -- Cross-Pad Coupling

**Branch**: `140-membrum-phase5-coupling` | **Date**: 2026-04-12 | **Spec**: `specs/140-membrum-phase5-coupling/spec.md`
**Input**: Feature specification from `/specs/140-membrum-phase5-coupling/spec.md`

## Summary

Phase 5 adds cross-pad sympathetic resonance to the Membrum drum synth. Striking one drum causes others to resonate based on their harmonic relationship -- the kick makes snare wires buzz, toms resonate sympathetically. This is implemented by integrating the existing `SympatheticResonanceSIMD` engine from KrateDSP with a two-layer coupling matrix (Tier 1 knobs + Tier 2 per-pair overrides), per-pad coupling parameters at PadConfig offset 36, pad category classification, and state version 5.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (SympatheticResonance, DelayLine, ModalResonatorBank)
**Storage**: Binary state stream (VST3 IStream), state version 5
**Testing**: Catch2 (membrum_tests target, dsp_tests target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo plugin (plugins/membrum/)
**Performance Goals**: Coupling engine < 1.5% CPU at 44.1 kHz with 8 voices + 64 resonators; bypass < 0.01% CPU
**Constraints**: Zero audio-thread allocations; real-time safe; per-sample coupling processing
**Scale/Scope**: 4 global params, 32 per-pad coupling amounts, 32x32 coupling matrix, ~11 source files modified, ~6 new test files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Coupling engine is processor-only. Controller registers params, no cross-inclusion. |
| II. Real-Time Audio Thread Safety | PASS | SympatheticResonance is noexcept, fixed-size arrays. DelayLine pre-allocated. CouplingMatrix resolved at param-change time (not per-sample). No allocations on audio thread. |
| III. Modern C++ Standards | PASS | RAII, constexpr, no raw new/delete. |
| IV. SIMD & DSP Optimization | PASS | SympatheticResonance already uses Highway SIMD. See SIMD analysis below. |
| V. VSTGUI Development | N/A | No UI changes in Phase 5 (deferred to Phase 6). |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code. All std:: and KrateDSP abstractions. |
| VII. Project Structure & Build System | PASS | New files follow monorepo layout. CMakeLists.txt updates for new sources. |
| VIII. Testing Discipline | PASS | Test-first development. All new code tested. |
| IX. Layered DSP Architecture | PASS | ModalResonatorBank accessor is Layer 2 (read-only). CouplingMatrix is plugin-local. |
| X. DSP Processing Constraints | PASS | No saturation/distortion (no oversampling needed). Energy limiter for safety. |
| XI. Performance Budgets | PASS | 1.5% budget within 5% total plugin budget. |
| XII. Debugging Discipline | PASS | Framework-based approach. |
| XIII. Test-First Development | PASS | Tests written before implementation. |
| XIV. ODR Prevention | PASS | All planned types verified unique (see Codebase Research). |
| XV. Pre-Implementation Research | PASS | Research complete. |
| XVI. Honest Completion | PASS | Evidence-based compliance table. |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: PadCategory (enum), CouplingMatrix

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| PadCategory | `grep -r "class PadCategory\|enum.*PadCategory" dsp/ plugins/` | No | Create New |
| CouplingMatrix | `grep -r "class CouplingMatrix" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: classifyPad, applyEnergyLimiter

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| classifyPad | `grep -r "classifyPad" dsp/ plugins/` | No | - | Create New |
| getModeFrequency | `grep -r "getModeFrequency" dsp/ plugins/` | No | - | Create New (add to ModalResonatorBank) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SympatheticResonance | dsp/include/krate/dsp/systems/sympathetic_resonance.h | 3 | Core coupling engine -- noteOn/noteOff/process lifecycle |
| SympatheticPartialInfo | dsp/include/krate/dsp/systems/sympathetic_resonance.h | 3 | Struct for passing partial frequencies to engine |
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | 1 | Propagation delay (0.5-2 ms) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Already used inside SympatheticResonance |
| ModalResonatorBank | dsp/include/krate/dsp/processors/modal_resonator_bank.h | 2 | Partial frequency source (add accessor) |
| VoicePool | plugins/membrum/src/voice_pool/voice_pool.h | plugin | Integration point for coupling hooks |
| PadConfig | plugins/membrum/src/dsp/pad_config.h | plugin | Extend with couplingAmount field |
| DefaultKit | plugins/membrum/src/dsp/default_kit.h | plugin | Category derivation reference |
| BodyBank | plugins/membrum/src/dsp/body_bank.h | plugin | getSharedBank() for partial extraction |
| DrumVoice | plugins/membrum/src/dsp/drum_voice.h | plugin | Add getPartialInfo() method |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (ModalResonatorBank extension)
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (SympatheticResonance reuse)
- [x] `plugins/membrum/src/dsp/` - Plugin-local DSP (new files: pad_category.h, coupling_matrix.h)
- [x] `plugins/membrum/src/processor/` - Processor integration
- [x] `plugins/membrum/src/voice_pool/` - VoicePool integration

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (PadCategory, CouplingMatrix) are unique and exist only in the Membrum namespace. The ModalResonatorBank extension adds accessor methods to an existing class (no new types). No name collisions found.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SympatheticResonance | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SympatheticResonance | reset | `void reset() noexcept` | Yes |
| SympatheticResonance | setAmount | `void setAmount(float amount) noexcept` | Yes |
| SympatheticResonance | noteOn | `void noteOn(int32_t voiceId, const SympatheticPartialInfo& partials) noexcept` | Yes |
| SympatheticResonance | noteOff | `void noteOff(int32_t voiceId) noexcept` | Yes |
| SympatheticResonance | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SympatheticResonance | isBypassed | `[[nodiscard]] bool isBypassed() const noexcept` | Yes |
| SympatheticResonance | getActiveResonatorCount | `[[nodiscard]] int getActiveResonatorCount() const noexcept` | Yes |
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | readLinear | `float readLinear(float delaySamples) const noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| BodyBank | getSharedBank | `[[nodiscard]] Krate::DSP::ModalResonatorBank& getSharedBank() noexcept` | Yes |
| PadConfig | padParamId | `[[nodiscard]] constexpr int padParamId(int padIndex, int offset) noexcept` | Yes |
| PadConfig | padIndexFromParamId | `[[nodiscard]] constexpr int padIndexFromParamId(int paramId) noexcept` | Yes |
| PadConfig | padOffsetFromParamId | `[[nodiscard]] constexpr int padOffsetFromParamId(int paramId) noexcept` | Yes |
| VoicePool | setPadConfigField | `void setPadConfigField(int padIndex, int offset, float normalizedValue) noexcept` | Yes |
| VoicePool | padConfig | `[[nodiscard]] const PadConfig& padConfig(int padIndex) const noexcept` | Yes |
| VoicePool | padConfigMut | `[[nodiscard]] PadConfig& padConfigMut(int padIndex) noexcept` | Yes |
| VoicePool | mainVoiceRef | `[[nodiscard]] DrumVoice& mainVoiceRef(int slot) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/systems/sympathetic_resonance.h` - SympatheticResonance class
- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/processors/modal_resonator_bank.h` - ModalResonatorBank class
- [x] `plugins/membrum/src/dsp/pad_config.h` - PadConfig, PadParamOffset
- [x] `plugins/membrum/src/dsp/drum_voice.h` - DrumVoice class
- [x] `plugins/membrum/src/dsp/body_bank.h` - BodyBank class
- [x] `plugins/membrum/src/voice_pool/voice_pool.h` - VoicePool class
- [x] `plugins/membrum/src/processor/processor.h` - Processor class
- [x] `plugins/membrum/src/plugin_ids.h` - Parameter IDs
- [x] `plugins/membrum/src/dsp/default_kit.h` - DefaultKit templates

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SympatheticResonance | `setAmount()` maps [0,1] to [-46,-26] dB internally | Pass the raw coupling gain, not dB |
| SympatheticResonance | `isBypassed()` checks both smoother current AND couplingGain_ | Safe for early-out |
| ModalResonatorBank | Frequencies stored as epsilon = 2*sin(pi*f/sr), not Hz | Recover via asin(eps/2)*sr/pi |
| PadConfig | `padOffsetFromParamId()` currently rejects offset >= 36 | Must update to accept offset 36 |
| VoicePool | mainVoiceRef(slot) returns DrumVoice, not a pointer | Use reference directly |
| DelayLine | `prepare()` takes maxDelaySeconds not samples | 0.002f for 2ms max |
| State stream | v4 uses float64 for all sound params | Phase 5 data also uses float64 for consistency |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| classifyPad | Plugin-specific logic (depends on Membrum's PadConfig/ExciterType/BodyModelType) |
| applyEnergyLimiter | Simple one-pole follower, 3 lines, single consumer |

**Decision**: No Layer 0 extraction needed. All new utilities are plugin-specific.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Resonators have per-sample feedback. Already handled by SympatheticResonance SIMD. |
| **Data parallelism width** | 64 resonators | Processed in SIMD batches by existing Highway kernel. |
| **Branch density in inner loop** | LOW | SIMD kernel is branchless. Energy limiter is single branch. |
| **Dominant operations** | arithmetic (resonator update) | Already SIMD-optimized. |
| **Current CPU budget vs expected usage** | 1.5% vs ~0.5-1.0% | Comfortable headroom. |

### SIMD Viability Verdict

**Verdict**: ALREADY IMPLEMENTED

**Reasoning**: The SympatheticResonance engine already uses Highway SIMD for the resonator bank processing. No new SIMD work is needed for Phase 5 -- we reuse the existing SIMD-accelerated engine. The new code (CouplingMatrix resolver, pad classification, energy limiter, mono sum, delay line) is all scalar and runs at parameter-change rate or is trivially cheap per-sample.

### Implementation Workflow

Phase 2 (SIMD) is not applicable -- the SIMD path already exists in the dependency.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when globalCoupling == 0 | ~100% savings when disabled | LOW | YES (already in SympatheticResonance::isBypassed) |
| Skip pads with couplingAmount == 0 | ~proportional savings | LOW | YES (skip noteOn registration) |
| Cache pad categories | Avoid recompute per block | LOW | YES (recompute on param change only) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-local (Membrum processor/DSP)

**Related features at same layer**:
- Phase 6: Macro controls (drives Global Coupling via "Complexity" macro)
- Phase 6: Acoustic/Extended UI modes (controls Tier 1 vs Tier 2 visibility)
- Future: Spatialization (per-pair delay offsets)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| PadCategory enum + classifyPad() | HIGH | Phase 6 macros, any category-aware feature | Keep local, extract if Phase 6 needs it |
| CouplingMatrix | MEDIUM | Could generalize to other cross-pad effects | Keep local for now |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep PadCategory in plugin-local dsp/ | Only one consumer now; Phase 6 is the review trigger |
| Keep CouplingMatrix in plugin-local dsp/ | Specific to coupling; not clear other cross-pad effects will use same resolver pattern |

### Review Trigger

After implementing **Phase 6 (macros + UI)**:
- [ ] Does Phase 6 need PadCategory? -> Extract to shared location
- [ ] Does Phase 6 need CouplingMatrix access? -> Consider visibility API

## Project Structure

### Documentation (this feature)

```text
specs/140-membrum-phase5-coupling/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── coupling_matrix.h
│   ├── pad_category.h
│   └── coupling_integration.h
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/processors/
│   └── modal_resonator_bank.h           # MODIFIED: add getModeFrequency(), getNumModes()
└── tests/unit/processors/
    └── test_modal_bank_frequency.cpp    # NEW: frequency accessor tests

plugins/membrum/
├── src/
│   ├── dsp/
│   │   ├── pad_config.h                 # MODIFIED: add kPadCouplingAmount, couplingAmount field
│   │   ├── pad_category.h              # NEW: PadCategory enum + classifyPad()
│   │   └── coupling_matrix.h           # NEW: CouplingMatrix class
│   ├── plugin_ids.h                     # MODIFIED: add IDs 270-273, version 5
│   ├── processor/
│   │   ├── processor.h                  # MODIFIED: add coupling members
│   │   └── processor.cpp               # MODIFIED: signal chain, state v5, param handling
│   ├── voice_pool/
│   │   ├── voice_pool.h                 # MODIFIED: add coupling engine pointer + hooks
│   │   └── voice_pool.cpp              # MODIFIED: implement coupling hooks
│   ├── controller/
│   │   └── controller.cpp              # MODIFIED: register Phase 5 parameters
│   └── dsp/
│       └── drum_voice.h                 # MODIFIED: add getPartialInfo()
└── tests/unit/
    ├── dsp/
    │   ├── test_pad_category.cpp        # NEW
    │   └── test_coupling_matrix.cpp     # NEW
    └── processor/
        ├── test_coupling_integration.cpp # NEW
        ├── test_coupling_state.cpp       # NEW
        └── test_coupling_energy.cpp      # NEW
```

**Structure Decision**: Standard monorepo plugin layout. New files in plugin-local `dsp/` for coupling-specific data structures. Tests mirror source structure.

## Complexity Tracking

No constitution violations. All design decisions follow established patterns.
