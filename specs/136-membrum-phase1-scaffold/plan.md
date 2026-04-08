# Implementation Plan: Membrum Phase 1 -- Plugin Scaffold + Single Voice

**Branch**: `136-membrum-phase1-scaffold` | **Date**: 2026-04-08 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/136-membrum-phase1-scaffold/spec.md`

## Summary

Create a new VST3 instrument plugin "Membrum" (synthesized drum machine) in the existing monorepo. Phase 1 delivers the plugin scaffold (CMake, entry, processor, controller, AU config, CI) and a single drum voice that produces a membrane drum sound on MIDI note 36. The voice wires three existing KrateDSP components -- ImpactExciter, ModalResonatorBank (16 modes with Bessel membrane ratios), and ADSREnvelope -- and exposes 5 parameters: Material, Size, Decay, Strike Position, Level. No custom UI; host-generic editor only.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang/Xcode 13+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (internal), KratePluginsShared (internal)
**Storage**: Binary state (44 bytes: version int32 + 5x float64 parameters)
**Testing**: Catch2 (via test_helpers), pluginval (strictness 5), auval (macOS)
**Target Platform**: Windows 10/11, macOS 11+, Linux (GCC 10+)
**Project Type**: Plugin in existing monorepo (plugins/membrum/)
**Performance Goals**: < 0.5% CPU single voice (16 modes) at 44.1 kHz (SC-003)
**Constraints**: 0 allocations in audio thread, real-time safe, cross-platform
**Scale/Scope**: 5 parameters, 1 voice, 1 MIDI note, ~15 source files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller are separate classes with kDistributable flag
- [x] State flows Host -> Processor -> Controller (setComponentState)
- [x] Processor works without controller

**Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in process() -- all DSP pre-allocated in setupProcessing/setActive
- [x] No locks, exceptions, or I/O in audio path
- [x] std::atomic for parameter transfer from controller to processor

**Principle III (Modern C++ Standards):**
- [x] C++20, RAII, constexpr for membrane constants
- [x] No raw new/delete

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed (see section below)
- [x] Scalar-first workflow: Phase 1 is scalar only

**Principle V (VSTGUI Development):**
- [x] N/A -- no custom UI in Phase 1 (host-generic editor only)

**Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific UI code
- [x] CI builds on all 3 platforms
- [x] AU config for macOS

**Principle VII (Project Structure & Build System):**
- [x] CMake 3.20+ with smtg_add_vst3plugin
- [x] Follows monorepo structure (plugins/membrum/)
- [x] DSP via angle-bracket includes, plugin via relative includes

**Principle VIII (Testing Discipline):**
- [x] Tests written before implementation (test-first)
- [x] Unit tests for VST parameters, MIDI handling, voice behavior
- [x] CI runs membrum_tests on all platforms

**Principle IX (Layered DSP Architecture):**
- [x] Reuses Layer 1 (ADSREnvelope) and Layer 2 (ImpactExciter, ModalResonatorBank)
- [x] DrumVoice is plugin-local, not in shared DSP library

**Principle XII (Debugging Discipline):**
- [x] Framework commitment -- using VST3 SDK patterns as-is

**Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide)
- [x] Tests written BEFORE implementation code
- [x] Each task group ends with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase research complete -- no DrumVoice class exists
- [x] Membrum namespace prevents collisions

**Principle XVI (Honest Completion):**
- [x] Compliance table requires file paths, line numbers, test names, measured values

**Post-design re-check**: PASS -- no constitution violations detected.

## Codebase Research (Principle XIV - ODR Prevention)

### Mandatory Searches Performed

**Classes/Structs to be created**: DrumVoice, Membrum::Processor, Membrum::Controller

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| DrumVoice | `search_for_pattern "class DrumVoice"` | No | Create New in plugins/membrum/src/dsp/ |
| Membrum::Processor | `find_symbol Processor` in plugins/ | No (only Gradus::Processor etc.) | Create New |
| Membrum::Controller | `find_symbol Controller` in plugins/ | No (only Gradus::Controller etc.) | Create New |

**Utility Functions to be created**: None planned -- all parameter mapping logic is in DrumVoice methods.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ImpactExciter | dsp/include/krate/dsp/processors/impact_exciter.h | 2 | Excitation source in DrumVoice |
| ModalResonatorBank | dsp/include/krate/dsp/processors/modal_resonator_bank.h | 2 | Body resonator in DrumVoice (16 modes) |
| ADSREnvelope | dsp/include/krate/dsp/primitives/adsr_envelope.h | 1 | Amplitude envelope in DrumVoice |
| IResonator | dsp/include/krate/dsp/processors/iresonator.h | 2 | Interface (ModalResonatorBank implements it) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - No conflicts
- [x] `dsp/include/krate/dsp/primitives/` - ADSREnvelope reused
- [x] `dsp/include/krate/dsp/processors/` - ImpactExciter, ModalResonatorBank reused
- [x] `plugins/` - No existing "membrum" or "DrumVoice"

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All new types are in the `Membrum` namespace (Processor, Controller) or in plugin-local files (DrumVoice). No name collisions with existing codebase.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| ImpactExciter | prepare | `void prepare(double sampleRate, uint32_t voiceId) noexcept` | Y |
| ImpactExciter | trigger | `void trigger(float velocity, float hardness, float mass, float brightness, float position, float f0) noexcept` | Y |
| ImpactExciter | process | `[[nodiscard]] float process(float feedbackVelocity) noexcept` | Y |
| ImpactExciter | isActive | `bool isActive()` (inline, checks pulseActive_ or bounceActive_) | Y |
| ImpactExciter | reset | `void reset() noexcept` | Y |
| ModalResonatorBank | prepare | `void prepare(double sampleRate) noexcept override` | Y |
| ModalResonatorBank | setModes | `void setModes(const float* frequencies, const float* amplitudes, int numPartials, float decayTime, float brightness, float stretch, float scatter) noexcept` | Y |
| ModalResonatorBank | updateModes | `void updateModes(const float* frequencies, const float* amplitudes, int numPartials, float decayTime, float brightness, float stretch, float scatter) noexcept` | Y |
| ModalResonatorBank | processSample | `[[nodiscard]] float processSample(float excitation) noexcept` | Y |
| ModalResonatorBank | reset | `void reset() noexcept` | Y |
| ADSREnvelope | prepare | `void prepare(double sampleRate)` | Y |
| ADSREnvelope | gate | `void gate(bool on) noexcept` | Y |
| ADSREnvelope | setAttack | `void setAttack(float ms) noexcept` | Y |
| ADSREnvelope | setDecay | `void setDecay(float ms) noexcept` | Y |
| ADSREnvelope | setSustain | `void setSustain(float level) noexcept` | Y |
| ADSREnvelope | setRelease | `void setRelease(float ms) noexcept` | Y |
| ADSREnvelope | setVelocity | `void setVelocity(float velocity) noexcept` | Y |
| ADSREnvelope | setVelocityScaling | `void setVelocityScaling(bool enabled) noexcept` | Y |
| ADSREnvelope | process | `float process() noexcept` | Y |
| ADSREnvelope | isActive | `bool isActive()` | Y |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/impact_exciter.h` - ImpactExciter (trigger, process, prepare)
- [x] `dsp/include/krate/dsp/processors/modal_resonator_bank.h` - ModalResonatorBank (setModes, updateModes, processSample, computeModeCoefficients)
- [x] `dsp/include/krate/dsp/primitives/adsr_envelope.h` - ADSREnvelope (gate, set*, process)
- [x] `dsp/include/krate/dsp/processors/iresonator.h` - IResonator interface

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ImpactExciter | `prepare()` takes voiceId for RNG seeding | `exciter_.prepare(sampleRate, 0)` |
| ImpactExciter | `process()` takes feedbackVelocity param (for bowed mode) | Pass `0.0f` for struck mode |
| ImpactExciter | trigger position=0.0, f0=0.0 disables comb filter | Use these values for Phase 1 |
| ModalResonatorBank | `setModes()` clears filter states (for note-on), `updateModes()` does not (for param changes) | Use setModes on noteOn, updateModes on param change |
| ModalResonatorBank | `computeModeCoefficients` clamps decayTime to [0.01, 5.0] | Ensure mapped values are in this range |
| ModalResonatorBank | brightness=1.0 means b3=0 (no HF damping = metallic); brightness=0.0 means maximum HF damping (woody). Material maps directly to brightness: `brightness = material`. Do NOT invert — higher Material value → higher brightness → less HF damping → metallic sound. | Map Material directly to brightness (`brightness = material`) |
| ModalResonatorBank | kMaxModes=96, but we use 16 | Pass numPartials=16 |
| ADSREnvelope | Times are in milliseconds, not seconds | A=0ms, D=200ms, S=0.0, R=300ms |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

No Layer 0 extractions needed. All parameter mapping logic is Membrum-specific (membrane Bessel ratios, material-to-damping mapping). If Phase 2 introduces other body models with similar mapping patterns, extraction can be considered then.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| DrumVoice::computeModeAmplitudes() | Membrane-specific Bessel evaluation, only used by DrumVoice |
| DrumVoice::mapMaterialToDamping() | Membrum-specific parameter mapping |

**Decision**: All new code stays in plugins/membrum/. No DSP library changes needed.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Each Gordon-Smith mode has per-sample feedback (sin/cos states) |
| **Data parallelism width** | 16 modes | 16 independent resonators = good SIMD width |
| **Branch density in inner loop** | LOW | Per-sample processing is branchless (active modes already filtered at setup) |
| **Dominant operations** | arithmetic | multiply-accumulate in Gordon-Smith resonators |
| **Current CPU budget vs expected usage** | 0.5% budget, expected ~0.2% | Scalar should be well within budget |

### SIMD Viability Verdict

**Verdict**: BENEFICIAL -- DEFER to Phase 2+

**Reasoning**: The 16 independent Gordon-Smith resonators have excellent SIMD parallelism (4 modes per SSE lane, 8 per AVX). ModalResonatorBank already has a SIMD variant (ModalResonatorBankSIMD) in the DSP library. However, Phase 1 only needs a single voice with 16 modes -- scalar is well within the 0.5% CPU budget. SIMD becomes critical in Phase 3 when scaling to 8+ simultaneous voices.

### Implementation Workflow

**Phase 1 (this spec)**: Scalar path using existing ModalResonatorBank. Establish correctness tests and CPU baseline.
**Phase 2+ (future)**: Switch to ModalResonatorBankSIMD for multi-voice scaling. Same API, drop-in replacement.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Use ModalResonatorBankSIMD | ~2-4x for modal processing | LOW (drop-in API) | DEFER to Phase 3 |
| Skip silent modes (flushSilentModes) | Variable, depends on decay | LOW | YES -- already built into ModalResonatorBank |
| Early-out when envelope idle | Saves full process cost when silent | LOW | YES -- check isActive() before processing |

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-local (not in shared DSP library)

**Related features at same layer** (from Membrum roadmap):
- Phase 2: 5 exciter types + 5 body models (swap-in architecture)
- Phase 3: 32-pad voice allocation
- Phase 4: Bowed/sustained excitation

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| DrumVoice | HIGH | Phase 2-3 (one voice per pad) | Keep local; Phase 2 refactors to support swappable exciter/body |
| membrane_modes.h | MEDIUM | Phase 2 (membrane body model) | Keep local; becomes one of several body model configs |
| Parameter mapping helpers | MEDIUM | Phase 2 (per-pad params) | Keep as DrumVoice methods for now |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| DrumVoice uses direct composition, not IResonator interface | Phase 1 only has one body model. Phase 2 can add polymorphism when needed. |
| No shared base class for voices | First voice type -- patterns not established yet |
| Parameter mapping in DrumVoice, not separate helper | Only one consumer. Phase 2 may extract if patterns emerge. |

### Review Trigger

After implementing Phase 2 (multi-exciter/body), review:
- [ ] Does Phase 2 need a VoiceBase class? If so, extract from DrumVoice
- [ ] Are parameter mapping patterns shared across body models? If so, extract helpers
- [ ] Should membrane_modes.h move to a body_models/ directory?

## Project Structure

### Documentation (this feature)

```text
specs/136-membrum-phase1-scaffold/
  plan.md              # This file
  research.md          # Phase 0 research findings
  data-model.md        # Entity definitions, state format, parameter mapping
  quickstart.md        # Build/test instructions, file layout
  contracts/
    vst3-interface.md  # Parameter IDs, MIDI behavior, bus config, state format
```

### Source Code (repository root)

```text
plugins/membrum/
  CMakeLists.txt                    # Plugin build config (following Gradus pattern)
  version.json                      # Single source of truth for version info
  CHANGELOG.md                      # Release notes

  src/
    entry.cpp                       # Plugin factory (BEGIN_FACTORY_DEF pattern)
    plugin_ids.h                    # FUIDs, subcategories, parameter IDs
    version.h.in                    # CMake template for version.h
    processor/
      processor.h                   # Membrum::Processor declaration
      processor.cpp                 # MIDI handling, parameter changes, voice processing
    controller/
      controller.h                  # Membrum::Controller declaration
      controller.cpp                # Parameter registration, state sync
    dsp/
      drum_voice.h                  # DrumVoice class (exciter + modal bank + envelope)
      membrane_modes.h              # Bessel ratios, mode constants, J_m evaluation

  tests/
    CMakeLists.txt                  # Test target (membrum_tests)
    unit/
      test_main.cpp                 # Catch2 entry point
      vst/
        membrum_vst_tests.cpp       # Parameter IDs, state round-trip, bus config
      processor/
        membrum_processor_tests.cpp # MIDI handling, voice output, velocity response

  resources/
    au-info.plist                   # AU v2 config (aumu/Mbrm/KrAt)
    auv3/
      audiounitconfig.h             # AU v3 config (0022 channel config)
    win32resource.rc.in             # Windows resource template

  docs/
    index.html                      # Plugin documentation landing page
    manual-template.html            # Manual template
    assets/
      style.css                     # Documentation styles
```

**Changes to existing files:**
- `CMakeLists.txt` (root): Add `add_subdirectory(plugins/membrum)` after gradus
- `.github/workflows/ci.yml`: Add membrum detection, build, test, artifact, AU validation steps

**Structure Decision**: Follows established monorepo plugin pattern (Gradus reference). Plugin-local DSP in `src/dsp/` (not shared library) because DrumVoice encodes Membrum-specific parameter mapping.

## Complexity Tracking

No constitution violations to justify. All design decisions follow established patterns.
