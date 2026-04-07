# Implementation Plan: Innexus ADSR Envelope Detection

**Branch**: `124-adsr-envelope-detection` | **Date**: 2026-03-08 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/124-adsr-envelope-detection/spec.md`

## Summary

Add automatic ADSR envelope detection to Innexus's sample analysis pipeline. The system extracts amplitude contour parameters from analyzed samples, provides 9 user-editable ADSR parameters (Attack, Decay, Sustain, Release, Amount, Time Scale, and 3 curve amounts), applies a global amplitude envelope to the synthesizer output, and integrates with the memory slot/morph/evolution systems. Uses the existing `ADSREnvelope` DSP class and `ADSRDisplay` UI component.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x+, VSTGUI 4.12+, KrateDSP (Layer 1: `ADSREnvelope`)
**Storage**: VST3 binary state stream (IBStreamer), version 9
**Testing**: Catch2 (innexus_tests target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: VST3 plugin monorepo
**Performance Goals**: <0.1% CPU overhead per voice at 44.1kHz (SC-005), <10% analysis overhead (SC-001)
**Constraints**: Zero allocations on audio thread, bit-exact bypass at Amount=0.0

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Processor handles ADSR on audio thread; Controller registers params and creates UI views |
| II. Real-Time Audio Thread Safety | PASS | ADSREnvelope is pre-allocated, no allocations in process(). EnvelopeDetector runs on analysis thread only. |
| III. Modern C++ Standards | PASS | Uses smart pointers, constexpr, atomics. No raw new/delete. |
| IV. SIMD & DSP Optimization | PASS | See SIMD section below -- not beneficial for this feature. |
| V. VSTGUI Development | PASS | Uses existing ADSRDisplay with UIDescription XML. |
| VI. Cross-Platform | PASS | No platform-specific code. ADSREnvelope and ADSRDisplay are cross-platform. |
| VII. Project Structure | PASS | New files follow existing patterns (plugin-local DSP, unit/integration tests). |
| VIII. Testing Discipline | PASS | Test-first approach; unit tests for detector, integration tests for pipeline. |
| IX. Layered DSP Architecture | PASS | Reuses Layer 1 ADSREnvelope. New EnvelopeDetector is plugin-local (not in DSP library). |
| X. DSP Processing Constraints | N/A | No saturation, waveshaping, or delay processing introduced; constraints do not apply. |
| XI. Performance Budgets | PASS | ADSR envelope adds <0.1% CPU (SC-005); total plugin remains well under 5% single core. EnvelopeDetector runs analysis-time only. |
| XII. Debugging Discipline | PASS | Framework components (ADSRDisplay, ADSREnvelope) are proven; no new framework usage patterns. |
| XIII. Test-First Development | PASS | Each phase writes tests before implementation. |
| XIV. Living Architecture Documentation | PASS | Architecture docs updated as final task (T072-T073) covering EnvelopeDetector and MemorySlot ADSR extension. |
| XV. Pre-Implementation Research (ODR Prevention) | PASS | See Codebase Research section. |
| XVI. Honest Completion | PASS | Compliance table in spec.md with concrete evidence (T074-T080). |

- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `EnvelopeDetector`, `DetectedADSR`

| Planned Type | Search | Existing? | Action |
|--------------|--------|-----------|--------|
| EnvelopeDetector | Searched via Serena and Grep | No | Create New in `plugins/innexus/src/dsp/` |
| DetectedADSR | Searched via Serena and Grep | No | Create New in `plugins/innexus/src/dsp/` |

**Utility Functions**: No new utility functions needed. Using existing `std::exp`, `std::log`, `std::sqrt`.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ADSREnvelope | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | 1 | Audio-thread envelope generation |
| ADSRDisplay | `plugins/shared/src/ui/adsr_display.h` | Shared | Envelope visualization with drag + playback dot |
| MemorySlot | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | 2 | Extended with 9 ADSR fields |
| HarmonicFrame | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Source of `globalAmplitude` for detection |
| SampleAnalysis | `plugins/innexus/src/dsp/sample_analysis.h` | Plugin | Extended with `DetectedADSR` field |
| SampleAnalyzer | `plugins/innexus/src/dsp/sample_analyzer.h/cpp` | Plugin | Hook point for envelope detection |
| EvolutionEngine | `plugins/innexus/src/dsp/evolution_engine.h` | Plugin | Extended for ADSR interpolation |
| OnePoleSmoother | `dsp/include/krate/dsp/core/smoothers.h` | 0 | Smooth Amount transitions |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (found ADSREnvelope to reuse)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 (found MemorySlot, HarmonicFrame to extend)
- [x] `plugins/innexus/src/dsp/` - Plugin-local DSP (no conflicts)
- [x] `plugins/shared/src/ui/` - Shared UI (found ADSRDisplay to reuse)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Both new types (`EnvelopeDetector`, `DetectedADSR`) are unique names not found anywhere in the codebase. They are scoped to the `Innexus` namespace and placed in plugin-local files.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| ADSREnvelope | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| ADSREnvelope | reset | `void reset() noexcept` | Yes |
| ADSREnvelope | gate | `void gate(bool gateOn) noexcept` | Yes |
| ADSREnvelope | setAttack | `void setAttack(float timeMs) noexcept` | Yes |
| ADSREnvelope | setDecay | `void setDecay(float timeMs) noexcept` | Yes |
| ADSREnvelope | setSustain | `void setSustain(float level) noexcept` | Yes |
| ADSREnvelope | setRelease | `void setRelease(float timeMs) noexcept` | Yes |
| ADSREnvelope | setAttackCurve[1] | `void setAttackCurve(float amount) noexcept` (float overload, -1 to +1) | Yes |
| ADSREnvelope | setDecayCurve[1] | `void setDecayCurve(float amount) noexcept` (float overload, -1 to +1) | Yes |
| ADSREnvelope | setReleaseCurve[1] | `void setReleaseCurve(float amount) noexcept` (float overload, -1 to +1) | Yes |
| ADSREnvelope | setRetriggerMode | `void setRetriggerMode(RetriggerMode mode) noexcept` | Yes |
| ADSREnvelope | process | `float process() noexcept` | Yes |
| ADSREnvelope | getOutput | `float getOutput() const noexcept` | Yes |
| ADSREnvelope | getStage | `Stage getStage() const noexcept` | Yes |
| ADSREnvelope | isActive | `bool isActive() const noexcept` | Yes |
| ADSRDisplay | setAdsrBaseParamId | `void setAdsrBaseParamId(int32_t baseId)` | Yes |
| ADSRDisplay | setCurveBaseParamId | `void setCurveBaseParamId(int32_t baseId)` | Yes |
| ADSRDisplay | setPlaybackStatePointers | `void setPlaybackStatePointers(const std::atomic<float>*, const std::atomic<int>*, const std::atomic<bool>*)` | Yes |
| ADSRDisplay | setParameterCallback | `void setParameterCallback(ParameterCallback cb)` | Yes |
| ADSRDisplay | setBeginEditCallback | `void setBeginEditCallback(EditCallback cb)` | Yes |
| ADSRDisplay | setEndEditCallback | `void setEndEditCallback(EditCallback cb)` | Yes |
| HarmonicFrame | globalAmplitude | `float globalAmplitude = 0.0f` | Yes |
| SampleAnalysis | frames | `std::vector<Krate::DSP::HarmonicFrame> frames` | Yes |
| SampleAnalysis | hopTimeSec | `float hopTimeSec = 0.0f` | Yes |
| MemorySlot | snapshot | `HarmonicSnapshot snapshot{}` | Yes |
| MemorySlot | occupied | `bool occupied = false` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/adsr_envelope.h` - ADSREnvelope class (lines 53-561)
- [x] `plugins/shared/src/ui/adsr_display.h` - ADSRDisplay class (lines 56-1833)
- [x] `dsp/include/krate/dsp/processors/harmonic_types.h` - HarmonicFrame struct
- [x] `dsp/include/krate/dsp/processors/harmonic_snapshot.h` - HarmonicSnapshot, MemorySlot
- [x] `plugins/innexus/src/dsp/sample_analysis.h` - SampleAnalysis struct
- [x] `plugins/innexus/src/dsp/sample_analyzer.h` and `.cpp` - SampleAnalyzer class
- [x] `plugins/innexus/src/dsp/evolution_engine.h` - EvolutionEngine class
- [x] `plugins/innexus/src/dsp/harmonic_blender.h` - HarmonicBlender class
- [x] `plugins/innexus/src/processor/processor.h` - Processor class
- [x] `plugins/innexus/src/processor/processor_state.cpp` - State version 8
- [x] `plugins/innexus/src/controller/controller.h` - Controller class
- [x] `plugins/innexus/src/plugin_ids.h` - ParameterIds enum (highest: 711)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ADSREnvelope | Has TWO overloads for curve setters: `setAttackCurve(CurveType)` and `setAttackCurve(float)` | Use the `float` overload for -1 to +1 curve amounts |
| ADSREnvelope | `process()` returns the envelope value (0-1), NOT the gain multiplier | Use return value directly as amplitude multiplier |
| ADSRDisplay | `setAdsrBaseParamId(id)` expects 4 consecutive IDs: A=id, D=id+1, S=id+2, R=id+3 | Parameter IDs 720-723 must be consecutive |
| ADSRDisplay | `setCurveBaseParamId(id)` expects 3 consecutive IDs: AC=id, DC=id+1, RC=id+2 | Parameter IDs 726-728 must be consecutive |
| MemorySlot | Adding fields changes struct size; all consumers see the same definition (header-only) | No ABI concern within same build |
| State version | Current is `8`, written as `streamer.writeInt32(8)` at line 27 | Change to `9` |
| kReleaseTimeId | ID 200 is oscillator release fade, NOT ADSR release | New ADSR release is kAdsrReleaseId = 723 |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| geometricMeanInterp | Reusable interpolation primitive | Defer | Only Innexus currently |
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| EnvelopeDetector::detect() | Analysis-specific, only one consumer |
| Rolling least-squares | Internal to EnvelopeDetector, not general-purpose |

**Decision**: No Layer 0 extractions needed. The geometric mean interpolation is a one-liner (`std::exp((1-t)*std::log(a) + t*std::log(b))`) and only used by Innexus. If future plugins need it, extract then.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| Feedback loops | YES | ADSR envelope has per-sample feedback (output feeds next sample's calculation) |
| Data parallelism width | 1 | Monophonic -- single ADSR instance |
| Branch density in inner loop | MEDIUM | Stage switch (attack/decay/sustain/release) per sample |
| Dominant operations | Arithmetic (mul, add) | Simple exponential envelope math |
| Current CPU budget vs expected usage | <0.1% vs ~0.01% | Trivial CPU cost for single envelope |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The ADSR envelope is monophonic (single instance), has per-sample feedback dependencies, and already consumes negligible CPU (<0.01%). SIMD cannot parallelize the feedback-dependent envelope calculation, and there is no data parallelism to exploit. The envelope detection algorithm runs once per sample load on the analysis thread, making SIMD irrelevant there too.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip ADSR when Amount==0.0 | ~100% bypass for inactive envelope | LOW | YES |
| Use `process()` return directly | Avoid extra getOutput() call | LOW | YES |

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-local (not in shared DSP library)

**Related features at same layer** (from known plans):
- Per-partial envelope detection could reuse amplitude contour analysis
- Other Krate plugins may want envelope detection for sample analysis

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| EnvelopeDetector | MEDIUM | Future per-partial envelope, other plugins | Keep local; extract to `dsp/core/` after 2nd consumer |
| MemorySlot ADSR fields | LOW | Specific to Innexus memory concept | Keep in MemorySlot |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep EnvelopeDetector in plugin-local | Only one consumer; constitution says wait for 2+ |
| No shared base for ADSR interpolation | Simple inline code, not worth abstracting |

## Project Structure

### Documentation (this feature)

```text
specs/124-adsr-envelope-detection/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── envelope-detector.h    # EnvelopeDetector API contract
│   ├── parameter-ids.h        # New parameter ID definitions
│   ├── memory-slot-extension.h # MemorySlot struct extension
│   └── state-v9.md            # State serialization v9 format
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
plugins/innexus/
├── src/
│   ├── plugin_ids.h                    # MODIFY: Add 9 parameter IDs (720-728)
│   ├── processor/
│   │   ├── processor.h                 # MODIFY: Add ADSR atomics, ADSREnvelope, smoother
│   │   ├── processor.cpp               # MODIFY: process(), handleNoteOn/Off, ADSR logic
│   │   └── processor_state.cpp         # MODIFY: v9 serialization
│   ├── controller/
│   │   └── controller.cpp              # MODIFY: Register params, createCustomView
│   ├── dsp/
│   │   ├── envelope_detector.h         # NEW: EnvelopeDetector + DetectedADSR
│   │   ├── sample_analysis.h           # MODIFY: Add DetectedADSR field
│   │   ├── sample_analyzer.cpp         # MODIFY: Call EnvelopeDetector
│   │   └── evolution_engine.h          # MODIFY: ADSR interpolation in getInterpolatedFrame()
│   └── resources/
│       └── editor.uidesc               # MODIFY: Add ADSRDisplay + knobs
├── tests/
│   ├── unit/processor/
│   │   ├── test_envelope_detector.cpp  # NEW: Unit tests for detection algorithm
│   │   └── test_memory_slot_adsr.cpp   # NEW: Unit tests for slot capture/recall/morph (US3)
│   └── integration/
│       └── test_adsr_envelope.cpp      # NEW: Integration tests for ADSR pipeline

dsp/include/krate/dsp/processors/
└── harmonic_snapshot.h                 # MODIFY: Extend MemorySlot with 9 ADSR fields
```

**Structure Decision**: Follows existing Innexus plugin patterns. New DSP code goes in `plugins/innexus/src/dsp/`. New tests in `tests/unit/processor/` (unit) and `tests/integration/` (integration). No new shared components.

## Complexity Tracking

No constitution violations. All principles are satisfied.
