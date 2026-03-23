# Implementation Plan: Waveguide String Resonance

**Branch**: `129-waveguide-string-resonance` | **Date**: 2026-03-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/129-waveguide-string-resonance/spec.md`

## Summary

Add a digital waveguide string resonator (`WaveguideString`) as an alternative to `ModalResonatorBank` in Innexus. The waveguide implements a delay-line feedback loop with Extended Karplus-Strong foundations: weighted one-zero loss filter, Thiran allpass fractional delay tuning, 4-section biquad dispersion allpass cascade for stiff string inharmonicity, pick-position comb filter on excitation, and in-loop DC blocker. Both resonator types conform to a shared `IResonator` interface with dual energy followers, enabling click-free equal-power crossfade switching. The two-segment delay architecture with `ScatteringJunction` interface prepares for Phase 4 bow model without redesign. 8-voice polyphony, velocity waves internally, parameters frozen at note onset.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP (internal), Steinberg VST3 SDK 3.7.x, VSTGUI 4.12+
**Storage**: N/A (real-time audio processing, no persistent storage beyond plugin state)
**Testing**: Catch2 (dsp_tests, innexus_tests) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform mandatory
**Project Type**: VST3 plugin monorepo
**Performance Goals**: WaveguideString ~10-12 ops/sample per voice; total plugin < 5% single core @ 44.1 kHz stereo; 8-voice polyphony
**Constraints**: Zero allocations on audio thread; all buffers pre-allocated in prepare(); real-time safe (noexcept, no locks, no I/O)
**Scale/Scope**: 1 new DSP class (Layer 2), 1 new interface (Layer 2), 3 new parameters, modifications to voice engine and resonator integration

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] WaveguideString is a pure DSP class (no VST dependency)
- [x] Parameter handling stays in Processor; UI registration in Controller
- [x] State save/load follows existing pattern in processor_state.cpp

**Principle II (Real-Time Audio Thread Safety):**
- [x] All buffers allocated in prepare(), not in process()
- [x] No allocations, locks, exceptions, or I/O in process loop
- [x] WaveguideString uses fixed-size arrays for dispersion filters (4 biquads)
- [x] DelayLine pre-allocates for minimum 20 Hz frequency

**Principle III (Modern C++):**
- [x] Uses RAII, noexcept, constexpr, value semantics
- [x] No raw new/delete

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed (see section below)
- [x] Scalar-first workflow followed

**Principle VIII (Testing Discipline):**
- [x] DSP algorithms testable without VST infrastructure
- [x] Pitch accuracy verified via autocorrelation in automated Catch2 tests
- [x] Passivity verified via loop gain sweep test

**Principle IX (Layered Architecture):**
- [x] WaveguideString at Layer 2 (processors) -- depends on Layer 0 (XorShift32, math) and Layer 1 (DelayLine, Biquad, DCBlocker, OnePoleSmoother)
- [x] IResonator at Layer 2 (processors) -- no dependencies beyond stdlib

**Principle X (DSP Processing Constraints):**
- [x] Allpass interpolation for fixed delay in feedback loop (Thiran)
- [x] DC blocking inside feedback loop
- [x] Soft clipper for safety limiting during parameter sweeps

**Principle XIV (ODR Prevention):**
- [x] All new types searched and confirmed unique (see below)

**Principle XV (Pre-Implementation Research):**
- [x] Checked specs/_architecture_/ for existing components
- [x] Verified no name collisions

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

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| IResonator | `grep -r "class IResonator" dsp/ plugins/` | No (only in archived spec contracts) | Create New |
| WaveguideString | `grep -r "class WaveguideString" dsp/ plugins/` | No | Create New |
| ScatteringJunction | `grep -r "ScatteringJunction" dsp/ plugins/` | No | Create New (nested struct) |
| PluckJunction | `grep -r "PluckJunction" dsp/ plugins/` | No | Create New (nested struct) |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| softClipWaveguide | `grep -r "softClip" dsp/ plugins/` | Yes (ModalResonatorBank::softClip) | modal_resonator_bank.h | Create local version (different threshold/formula per FR-012) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | 1 | Two instances for nut/bridge delay segments |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | In-loop DC blocker at 3.5 Hz |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | 4 instances as allpass for dispersion cascade |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (freq, decay, brightness) |
| XorShift32 | dsp/include/krate/dsp/core/xorshift32.h | 0 | Per-voice noise burst generation |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal prevention in loop |
| kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Filter coefficient computation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (checked for WaveguideResonator, KarplusStrong, ModalResonatorBank)
- [x] `specs/_architecture_/` - Component inventory
- [x] `plugins/innexus/src/` - Plugin-local DSP and voice engine

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (IResonator, WaveguideString, ScatteringJunction, PluckJunction) are confirmed unique in the codebase. The existing WaveguideResonator and KarplusStrong classes remain untouched -- WaveguideString is a new, separate class with a different namespace path. IResonator exists only in archived spec contracts (not production code).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | read | `[[nodiscard]] float read(size_t delaySamples) const noexcept` | Yes |
| DelayLine | readAllpass | `[[nodiscard]] float readAllpass(float delaySamples) noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| DelayLine | peekNext | `[[nodiscard]] float peekNext(size_t offset) const noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz) noexcept` (inferred from usage) | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | (state zeroing) | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| XorShift32 | seed | `void seed(uint32_t voiceId) noexcept` | Yes |
| XorShift32 | next | `[[nodiscard]] uint32_t next() noexcept` | Yes |
| ModalResonatorBank | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| ModalResonatorBank | processSample | `[[nodiscard]] float processSample(float excitation) noexcept` | Yes |
| ModalResonatorBank | processSample | `[[nodiscard]] float processSample(float excitation, float decayScale) noexcept` | Yes |
| ModalResonatorBank | reset | `void reset() noexcept` | Yes |
| ModalResonatorBank | setModes | `void setModes(const float*, const float*, int, float, float, float, float) noexcept` | Yes |
| ModalResonatorBank | updateModes | `void updateModes(const float*, const float*, int, float, float, float, float) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad, BiquadCascade classes
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/xorshift32.h` - XorShift32 struct
- [x] `dsp/include/krate/dsp/processors/modal_resonator_bank.h` - ModalResonatorBank class
- [x] `dsp/include/krate/dsp/processors/waveguide_resonator.h` - WaveguideResonator class (reference)
- [x] `dsp/include/krate/dsp/processors/karplus_strong.h` - KarplusStrong class (reference)
- [x] `dsp/include/krate/dsp/processors/impact_exciter.h` - ImpactExciter class
- [x] `plugins/innexus/src/processor/innexus_voice.h` - InnexusVoice struct
- [x] `plugins/innexus/src/processor/processor.h` - Processor class
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter IDs
- [x] `plugins/innexus/src/dsp/physical_model_mixer.h` - PhysicalModelMixer struct

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| DelayLine | `prepare()` takes `(double sampleRate, float maxDelaySeconds)` not `(sampleRate, maxDelaySamples)` | `delay_.prepare(sampleRate_, 1.0f / 20.0f)` for 20 Hz minimum |
| OnePoleSmoother | `configure()` takes `(float smoothTimeMs, float sampleRate)` -- ms first, Hz second | `smoother_.configure(20.0f, 44100.0f)` |
| DCBlocker | Constructor takes `(sampleRate, cutoffHz)` | `dcBlocker_.prepare(sampleRate, 3.5f)` |
| ModalResonatorBank | `processSample()` returns raw output, not soft-clipped | Soft clipping is applied externally in `processBlock()` |
| Biquad | `configure()` takes `(FilterType, freq, q, gainDb, sampleRate)` | For allpass: `configure(FilterType::Allpass, freq, q, 0.0f, sr)` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| computeDispersionCoefficients | Complex, WaveguideString-specific, takes B + f0 + sampleRate |
| computeLossCoefficients | Simple formula, only used by WaveguideString |
| computeExcitationGain | Energy normalisation, WaveguideString-specific |
| applySoftClip | One-liner inline, different from ModalResonatorBank's version |

**Decision**: No Layer 0 extractions needed. All new utilities are WaveguideString-specific with a single consumer. The energy follower logic (two EMAs at output tap) is simple enough to inline in both resonator types rather than extract a shared class.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Single-sample feedback loop is the core of the waveguide -- each output sample feeds back as input to the next iteration |
| **Data parallelism width** | 8 voices (inter-voice) | Each voice is independent, but voices process one sample at a time through individual feedback loops |
| **Branch density in inner loop** | LOW | Only the soft clipper has a branch (`|x| < threshold`); rest is arithmetic |
| **Dominant operations** | Arithmetic (multiply, add) + memory (delay line read/write) | ~10-12 arithmetic ops per sample per voice |
| **Current CPU budget vs expected usage** | < 5% budget vs ~0.5% expected per voice (8 voices = ~4%) | Well within budget, no optimization pressure |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL (per-voice), MARGINAL -- DEFER (cross-voice Phase 6)

**Reasoning**: The waveguide's per-sample feedback loop creates a hard serial dependency that cannot be parallelized with SIMD within a single voice. Cross-voice SIMD (processing 4-8 voices in parallel) is theoretically possible but requires SoA layout across voices, which conflicts with the current per-voice struct design in InnexusVoice. The algorithm is already very cheap (~10-12 ops/sample vs modal's ~288 ops/sample unvectorised), so optimization pressure is minimal. Phase 6 (Sympathetic Resonance) explicitly mentions cross-string SIMD as viable, which is the appropriate time to consider this.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip dispersion when stiffness = 0 | ~30% fewer ops for clean strings | LOW | YES |
| Fast tanhf approximation for soft clipper | Negligible (rarely triggered) | LOW | DEFER |
| Bypass loss filter smoothing when stable | ~5% reduction | LOW | YES |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - Processors

**Related features at same layer** (from roadmap):
- Phase 4: Bow Model -- BowJunction replaces PluckJunction, couples via getFeedbackVelocity()
- Phase 5: Body Resonance -- wraps a resonator (different topology, not a peer)
- Phase 6: Sympathetic Resonance -- multiple WaveguideString instances in parallel

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| IResonator interface | HIGH | ModalResonatorBank, WaveguideString, future Body Resonance | Extract now (shared by design) |
| ScatteringJunction interface | HIGH | PluckJunction (Phase 3), BowJunction (Phase 4) | Extract now (forward compatibility) |
| Energy follower (dual EMA) | MEDIUM | All IResonator implementations | Keep inline for now (2 consumers, trivial code) |
| Crossfade logic | MEDIUM | Phase 5 Body Resonance switching | Keep in voice engine for now |

### Detailed Analysis (for HIGH potential items)

**IResonator** provides:
- Common process(excitation) -> float interface
- Dual energy followers (control + perceptual)
- silence() for voice steal cleanup
- getFeedbackVelocity() for Phase 4 bow coupling

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Phase 4 Bow Model | YES | BowJunction calls getFeedbackVelocity() on WaveguideString |
| Phase 5 Body Resonance | MAYBE | Body wraps a resonator rather than being one |
| Phase 6 Sympathetic | YES | Multiple WaveguideString instances through IResonator |

**Recommendation**: Extract IResonator now -- it has 2 immediate consumers (ModalResonatorBank, WaveguideString) and is architecturally fundamental.

**ScatteringJunction** provides:
- Two-wave-in, two-wave-out interface
- Characteristic impedance parameter

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Phase 4 Bow Model | YES | BowJunction is the primary Phase 4 deliverable |

**Recommendation**: Define the interface now (it's in the spec), but keep it simple. Phase 4 will flesh out the nonlinear friction model.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| IResonator as separate header | 2 immediate consumers, architecturally fundamental |
| Energy followers inlined, not extracted | Trivial code (2 lines), only 2 consumers |
| ScatteringJunction as nested types | Only used within WaveguideString; Phase 4 may extract |
| No cross-voice SIMD | Feedback loop prevents per-voice SIMD; defer to Phase 6 |

### Review Trigger

After implementing **Phase 4 (Bow Model)**, review this section:
- [ ] Does BowJunction need ScatteringJunction extracted to its own header?
- [ ] Does bow coupling via getFeedbackVelocity() work as designed?
- [ ] Any duplicated energy follower code?

## Project Structure

### Documentation (this feature)

```text
specs/129-waveguide-string-resonance/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── iresonator.h     # IResonator interface contract
│   └── waveguide_string.h  # WaveguideString class contract
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/include/krate/dsp/processors/
├── iresonator.h                # NEW: Shared resonator interface (Layer 2)
├── waveguide_string.h          # NEW: Waveguide string processor (Layer 2)
├── modal_resonator_bank.h      # MODIFIED: Add IResonator interface + energy followers
├── waveguide_resonator.h       # UNCHANGED (reference only)
├── karplus_strong.h            # UNCHANGED (reference only)
└── impact_exciter.h            # UNCHANGED

dsp/tests/unit/processors/
└── waveguide_string_test.cpp   # NEW: Pitch accuracy, passivity, energy, dispersion tests

plugins/innexus/src/
├── plugin_ids.h                # MODIFIED: Add kResonanceTypeId, kWaveguideStiffnessId, kWaveguidePickPositionId
├── processor/
│   ├── innexus_voice.h         # MODIFIED: Add WaveguideString member, crossfade state
│   ├── processor.cpp           # MODIFIED: Resonance type routing, crossfade in render loop
│   ├── processor_midi.cpp      # MODIFIED: WaveguideString noteOn on note events
│   ├── processor_params.cpp    # MODIFIED: Handle new parameter IDs
│   └── processor_state.cpp     # MODIFIED: Save/load new parameters
├── controller/
│   └── controller.cpp          # MODIFIED: Register new parameters
├── parameters/
│   └── innexus_params.h        # MODIFIED: Parameter registration helpers
└── dsp/
    └── physical_model_mixer.h  # MODIFIED: Extend for resonance type crossfade

plugins/innexus/tests/
├── unit/processor/
│   └── waveguide_integration_test.cpp  # NEW: Plugin-level waveguide tests
└── unit/vst/
    └── waveguide_param_test.cpp        # NEW: Parameter registration tests
```

**Structure Decision**: Follows existing monorepo layout. WaveguideString is a Layer 2 processor in the shared DSP library. Plugin integration touches the Innexus plugin's processor, controller, and parameter subsystems.

## Complexity Tracking

No constitution violations. All design decisions comply with the 18 principles.

### Key Design Decisions

| Decision | Rationale | Alternatives Considered |
|----------|-----------|------------------------|
| **SIMD: NOT BENEFICIAL per voice, DEFER to Phase 6** | The waveguide inner loop has a hard per-sample feedback dependency that prevents intra-voice SIMD. Cross-voice SIMD (4-8 voices in parallel) is possible in Phase 6 but requires SoA layout restructuring across InnexusVoice. The algorithm is cheap (~10-12 ops/sample vs modal bank's ~288), so optimization pressure is minimal now. | Cross-voice SIMD considered but requires AoS-to-SoA restructuring of InnexusVoice; deferred to Phase 6 Sympathetic Resonance when multiple WaveguideString instances per voice make SoA natural. |
| **Two-segment delay split (nut/bridge) over single segment** | Divides the delay line at the interaction point beta*N (nut side) and (1-beta)*N (bridge side), enabling a ScatteringJunction interface at the interaction point. Architecturally required for Phase 4 bow model where the junction transitions from transparent (PluckJunction) to nonlinear friction (BowJunction). A single-segment design would require a costly retrofit in Phase 4. | Single delay segment considered (simpler Phase 3 code) but rejected due to Phase 4 retrofit cost and architectural unsoundness. |
