# Implementation Plan: Impact Exciter

**Branch**: `128-impact-exciter` | **Date**: 2026-03-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/128-impact-exciter/spec.md`

## Summary

Implement an `ImpactExciter` DSP component (Layer 2 processor) for the Innexus harmonic resynthesis instrument. The exciter generates short percussive excitation bursts (mallet/pluck attacks) from MIDI note-on events, with 7 sub-systems: asymmetric pulse, micro-bounce, noise texture (xorshift32 RNG), per-trigger variation, hardness SVF filter, velocity coupling, and strike position comb filter. The excitation signal feeds the existing `ModalResonatorBank` to create struck-object sounds from any analyzed timbre. Five new parameters (ExciterType, ImpactHardness, ImpactMass, ImpactBrightness, ImpactPosition) are added to the Innexus plugin. Retrigger safety includes exponential decay energy capping and mallet choke via multiplicative `decayScale` on the resonator bank.

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang, GCC)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (internal shared library)
**Storage**: N/A (in-memory DSP processing only)
**Testing**: Catch2 (dsp_tests, innexus_tests)
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang/Xcode), Linux (GCC)
**Project Type**: Monorepo -- shared DSP library + plugin
**Performance Goals**: < 0.1% CPU per voice at 44.1 kHz (Layer 2 budget), < 5% total plugin
**Constraints**: Zero allocations on audio thread, real-time safe, deterministic output for golden-reference tests
**Scale/Scope**: 1 new Layer 0 utility (XorShift32), 1 new Layer 2 processor (ImpactExciter), 5 new parameters, extensions to ModalResonatorBank and InnexusVoice

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] New parameters stored as atomics in Processor, registered in Controller
- [x] No cross-threading violations -- exciter runs on audio thread only
- [x] State save/load follows existing pattern

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] ImpactExciter allocates nothing on audio thread (DelayLine/SVF allocated in prepare())
- [x] XorShift32 is pure arithmetic -- no allocation, no stdlib
- [x] Energy capping and choke are per-sample arithmetic only
- [x] No locks, exceptions, or I/O in any processing path

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD analysis completed (see section below)
- [x] Scalar-first workflow: full scalar implementation in this spec

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] XorShift32 at Layer 0 (no dependencies)
- [x] ImpactExciter at Layer 2 (depends on Layer 0: XorShift32, Layer 1: SVF, DelayLine)
- [x] No upward dependencies

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

**Classes/Structs to be created**: XorShift32, ImpactExciter, ExciterType

| Planned Type | Search Result | Action |
|---|---|---|
| `XorShift32` | No existing class. `ResidualSynthesizer` uses `std::minstd_rand` (different algorithm). | Create New at Layer 0 |
| `ImpactExciter` | No existing class. Only mentioned in roadmap spec. | Create New at Layer 2 |
| `ExciterType` | No existing enum. | Create New in plugin_ids.h |

**Utility Functions to be created**: None -- all math is inline in ImpactExciter methods.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|---|---|---|---|
| `SVF` | `dsp/include/krate/dsp/primitives/svf.h` | 1 | Hardness-controlled lowpass filter (FR-015) |
| `DelayLine` | `dsp/include/krate/dsp/primitives/delay_line.h` | 1 | Strike position comb filter delay buffer (FR-022) |
| `ModalResonatorBank` | `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | 2 | Integration point -- exciter output feeds this (FR-031) |
| `ResidualSynthesizer` | `dsp/include/krate/dsp/processors/residual_synthesizer.h` | 2 | Parallel excitation source (FR-030) |
| `PhysicalModelMixer` | `plugins/innexus/src/dsp/physical_model_mixer.h` | Plugin | Blends residual and physical paths (existing) |
| `midiNoteToFrequency` | `dsp/include/krate/dsp/core/math_utils.h` (or similar) | 0 | Convert MIDI note to f0 for comb filter |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - No `xorshift32.h` exists
- [x] `dsp/include/krate/dsp/primitives/` - SVF and DelayLine confirmed available; `comb_filter.h` has FeedforwardComb (positive gain only, not suitable for spec's negative-gain comb)
- [x] `dsp/include/krate/dsp/processors/` - No `impact_exciter.h` exists
- [x] `plugins/innexus/src/plugin_ids.h` - IDs 805-809 are available

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (`XorShift32`, `ImpactExciter`, `ExciterType`) are unique names not found anywhere in the codebase. The `XorShift32` struct is placed in `Krate::DSP` namespace to avoid any collision with potential stdlib or third-party types.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|---|---|---|---|
| SVF | prepare | `void prepare(double sampleRate, float smoothingTimeSec = kDefaultSmoothingTimeSec)` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float cutoffHz) noexcept` | Yes |
| SVF | snapToTarget | `void snapToTarget() noexcept` | Yes |
| SVF | process | `float process(float input) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds)` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | read | `float read(int delaySamples) const noexcept` | Yes |
| ModalResonatorBank | processSample (original) | `float processSample(float excitation) noexcept` | Yes |
| ModalResonatorBank | processSample (choke) | `float processSample(float excitation, float decayScale) noexcept` | See contract `specs/128-impact-exciter/contracts/modal_resonator_bank_extension.h` |
| ResidualSynthesizer | process | `float process() noexcept` | Yes |
| PhysicalModelMixer | process | `static float process(float harmonic, float residual, float physical, float mix) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class (740 lines, full API)
- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/primitives/comb_filter.h` - FeedforwardComb/FeedbackComb (not suitable)
- [x] `dsp/include/krate/dsp/primitives/pink_noise_filter.h` - PinkNoiseFilter (different algorithm)
- [x] `dsp/include/krate/dsp/processors/modal_resonator_bank.h` - ModalResonatorBank (339 lines)
- [x] `dsp/include/krate/dsp/processors/residual_synthesizer.h` - ResidualSynthesizer
- [x] `plugins/innexus/src/processor/innexus_voice.h` - InnexusVoice struct
- [x] `plugins/innexus/src/processor/processor.h` - Innexus Processor class
- [x] `plugins/innexus/src/processor/processor_midi.cpp` - Note-on handling, initVoiceForNoteOn
- [x] `plugins/innexus/src/processor/processor.cpp` - Voice processing loop (line ~1585)
- [x] `plugins/innexus/src/processor/processor_state.cpp` - State save/load pattern
- [x] `plugins/innexus/src/controller/controller.cpp` - Parameter registration pattern
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter ID allocation
- [x] `plugins/innexus/src/dsp/physical_model_mixer.h` - PhysicalModelMixer

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|---|---|---|
| SVF | Default smoothing may cause lag on trigger | Call `snapToTarget()` after setCutoff() at trigger time |
| DelayLine | `read(int)` uses integer delay; `readLinear(float)` for fractional | Use `read(int)` for comb (spec uses `floor()`) |
| ModalResonatorBank | `processSample()` calls `smoothCoefficients()` internally | No need to call smooth externally |
| InnexusVoice | `velocityGain` is raw velocity [0,1], not a dB gain | Use directly as normalized velocity |
| Processor state | Params stored as `std::atomic<float>` with relaxed ordering | Follow existing pattern exactly |
| FeedforwardComb | Gain clamped to [0.0, 1.0] -- cannot do negative gain | Use DelayLine directly for spec's `1 - z^(-D)` comb |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|---|---|---|---|
| `XorShift32` | Deterministic PRNG needed by ImpactExciter, potentially Bow exciter (Phase 4), and other per-voice noise | `dsp/include/krate/dsp/core/xorshift32.h` | ImpactExciter, future BowExciter, any processor needing deterministic per-voice noise |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|---|---|
| Pulse shape computation (`pow(sin(...), gamma)`) | Specific to impact exciter physics model, not reusable |
| Energy capping accumulator | Specific to exciter energy management |
| One-pole pinking filter (`pink = white - b * prev`) | Trivial one-liner, spec-defined fixed coefficient |

**Decision**: Only `XorShift32` is extracted to Layer 0. All other functions are kept as ImpactExciter member functions.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|---|---|---|
| **Feedback loops** | YES | SVF has per-sample feedback; pinking filter has per-sample state |
| **Data parallelism width** | 1 (per voice) | Each voice processes independently, but within a voice, all operations are serial |
| **Branch density in inner loop** | LOW | Only `if (pulseActive_)` and `if (bounceActive_)` checks |
| **Dominant operations** | Transcendental (powf, sinf, expf) + arithmetic | Pulse shape uses pow/sin; SVF is pure arithmetic |
| **Current CPU budget vs expected usage** | 0.1% budget vs ~0.02% expected | Layer 2 budget is 0.5%, SC-012 target is 0.1%. Impact exciter is a short burst (0.5-15ms) so active processing is minimal |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The ImpactExciter processes one sample at a time with serial feedback dependencies (SVF state, pinking filter state). There is no data parallelism within a single voice's processing. Cross-voice SIMD parallelism is theoretically possible but the exciter is only active during brief attack transients (~15ms max), making the optimization overhead larger than the benefit. The expected CPU usage (~0.02%) is far below the 0.1% target.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|---|---|---|---|
| Early-out when pulse inactive | ~90% reduction (no processing between notes) | LOW | YES |
| Fast powf/sinf approximation | ~20% reduction in trigger cost | MED | DEFER (not needed for 0.1% target) |
| Skip comb filter when position = 0.0 | ~15% reduction for default position | LOW | YES |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (processors)

**Related features at same layer** (from roadmap):
- Phase 4: Bow Exciter (continuous excitation, different physics)
- Phase 3: Waveguide String Resonance (different resonator type)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|---|---|---|---|
| `XorShift32` | HIGH | Bow exciter, any per-voice noise source | Extract to Layer 0 (done) |
| Energy capping pattern | MEDIUM | Bow exciter may need similar energy management | Keep in ImpactExciter; extract if Bow needs it |
| Exciter type switch in voice | HIGH | Phase 4 adds Bow (type=2) using same switch | Design switch to be extensible |
| Mallet choke in voice | LOW | Bow exciter uses sustained contact, not impact choke | Keep in voice; may need different mechanism for Bow |

### Decision Log

| Decision | Rationale |
|---|---|
| XorShift32 at Layer 0 | Clear reuse by Bow exciter; pure utility with no dependencies |
| Energy capping in ImpactExciter | Specific to impulse energy; Bow will have different energy model |
| Choke envelope in InnexusVoice | Affects resonator (shared); voice owns playing behavior per spec |
| No base class for exciters | Only 2 types; simple if/switch is faster and clearer than vtable |

### Review Trigger

After implementing **Phase 4 (Bow Exciter)**, review:
- [ ] Does Bow need `XorShift32`? (Likely yes -- extract is already done)
- [ ] Does Bow need energy capping? (If so, extract common pattern)
- [ ] Does Bow need different choke mechanism? (Probably yes -- sustained contact vs impact)
- [ ] Should exciter types share an interface? (Only if 3+ types exist)

## Project Structure

### Documentation (this feature)

```text
specs/128-impact-exciter/
  plan.md              # This file
  spec.md              # Feature specification
  research.md          # Phase 0 research findings
  data-model.md        # Entity definitions and relationships
  quickstart.md        # Build and orientation guide
  contracts/           # API contracts
    impact_exciter_api.h
    xorshift32_api.h
    modal_resonator_bank_extension.h
    parameter_ids.h
  tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
  include/krate/dsp/
    core/
      xorshift32.h                 # NEW: Layer 0 deterministic PRNG
    processors/
      impact_exciter.h             # NEW: Layer 2 impact exciter
      modal_resonator_bank.h       # MODIFIED: add decayScale overload
  tests/unit/
    core/
      xorshift32_test.cpp          # NEW: XorShift32 unit tests
    processors/
      impact_exciter_test.cpp      # NEW: ImpactExciter unit tests
      test_modal_resonator_bank.cpp # MODIFIED: add decayScale tests

plugins/innexus/
  src/
    plugin_ids.h                   # MODIFIED: add ExciterType enum + param IDs
    processor/
      innexus_voice.h              # MODIFIED: add ImpactExciter member + choke state
      processor.h                  # MODIFIED: add atomic params
      processor.cpp                # MODIFIED: voice loop exciter switch
      processor_midi.cpp           # MODIFIED: trigger exciter on note-on, choke logic
      processor_state.cpp          # MODIFIED: save/load new params
    controller/
      controller.cpp               # MODIFIED: register new params
  tests/
    unit/processor/
      test_physical_model.cpp      # MODIFIED: add exciter integration tests
    unit/vst/
      innexus_vst_tests.cpp        # MODIFIED: add parameter tests
```

**Structure Decision**: This follows the existing monorepo layout exactly. New DSP code goes in the shared KrateDSP library (Layer 0/2). Plugin integration code goes in the Innexus plugin directory. Tests mirror the source structure.

## Complexity Tracking

No constitution violations detected. All design decisions follow existing patterns and layer rules.

### Key Design Decisions with Complexity Implications

| Decision | Complexity Impact | Rationale |
|---|---|---|
| `combDelaySamples_` stored as `int` (not `float`) | Low -- integer delay avoids fractional interpolation overhead | Spec uses `floor()` before passing to `DelayLine::read(int)`; no sub-sample accuracy required for comb position |
| `ModalResonatorBank` choke as a separate overload (not default parameter) | Low -- two distinct method signatures in header | Contract `modal_resonator_bank_extension.h` defines two separate overloads; no default argument to maintain binary compatibility with existing call sites |
| Energy threshold computed analytically in `prepare()` | Low -- one-time computation at setup | Avoids magic constant; tied to pulse formula at default params (vel=0.5, hardness=0.5, mass=0.3) so threshold tracks spec intent |
| Attack ramp resets from zero on every retrigger (including mid-burst) | Medium -- retrigger during active pulse causes brief amplitude dip from whatever the current level is to zero, then ramps up | FR-033 requires this for click-free onset; the ramp length (0.1-0.5ms) is short enough that the dip is below audible threshold |
| No base class / vtable for exciter types | Low -- plain if/switch in voice loop | Only 2 types in this spec; vtable overhead exceeds benefit at this count. Revisit at Phase 4 (Bow) if a third type is added |
| XorShift32 at Layer 0 (not a member lambda) | Low -- extracted to shareable header | Enables Bow exciter reuse; pure utility with no layer violations |
