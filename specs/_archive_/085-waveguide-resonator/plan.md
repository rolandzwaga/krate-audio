# Implementation Plan: Waveguide Resonator

**Branch**: `085-waveguide-resonator` | **Date**: 2026-01-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/085-waveguide-resonator/spec.md`

## Summary

Implement a Layer 2 DSP processor (`WaveguideResonator`) that creates flute/pipe-like resonances using bidirectional digital waveguide synthesis. The implementation uses Kelly-Lochbaum scattering principles for physically accurate end reflections, with configurable loss (frequency-dependent damping) and dispersion (inharmonicity). The processor composes existing Layer 1 primitives: two `DelayLine` instances for bidirectional wave propagation, `OnePoleLP` for loss filtering, `Allpass1Pole` for dispersion, `DCBlocker` for stability, and `OnePoleSmoother` for parameter automation.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: DelayLine, Allpass1Pole, OnePoleLP, DCBlocker, OnePoleSmoother (all Layer 1)
**Storage**: N/A (stateless audio processing with pre-allocated buffers)
**Testing**: Catch2 (via CTest) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows, macOS, Linux (VST3 cross-platform plugin)
**Project Type**: DSP library component (monorepo)
**Performance Goals**: < 0.5% CPU at 192kHz sample rate (SC-001)
**Constraints**: Real-time safe (no allocations in process), noexcept, denormal-free
**Scale/Scope**: Single processor class with ~500 lines of code

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check (PASSED)

**Principle II (Real-Time Safety):**
- [x] All processing methods will be noexcept
- [x] No memory allocation in process() or reset()
- [x] Denormal flushing via `detail::flushDenormal()`
- [x] DC blocking to prevent accumulation

**Principle IX (Layer Architecture):**
- [x] Layer 2 processor depends only on Layers 0-1
- [x] No circular dependencies introduced

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created (WaveguideResonator is new)

### Post-Design Check (PASSED)

- [x] Design uses only Layer 0-1 dependencies
- [x] All parameters have valid ranges documented
- [x] Processing algorithm preserves real-time safety
- [x] Signal flow matches Kelly-Lochbaum scattering model

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Complete - No ODR conflicts found.*

### Mandatory Searches Performed

**Classes/Structs to be created**: WaveguideResonator

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| WaveguideResonator | `grep -r "class WaveguideResonator" dsp/ plugins/` | No | Create New |
| WaveguideResonator | `grep -r "struct WaveguideResonator" dsp/ plugins/` | No | Create New |
| Waveguide | `grep -r "class Waveguide[^R]" dsp/ plugins/` | No | N/A |

**Utility Functions to be created**: None (all utilities exist in Layer 0)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | `primitives/delay_line.h` | 1 | Two instances for bidirectional waves |
| OnePoleLP | `primitives/one_pole.h` | 1 | Two instances for loss filtering |
| Allpass1Pole | `primitives/allpass_1pole.h` | 1 | Two instances for dispersion |
| DCBlocker | `primitives/dc_blocker.h` | 1 | One instance for output DC blocking |
| OnePoleSmoother | `primitives/smoother.h` | 1 | Three instances for parameter smoothing |
| detail::flushDenormal | `core/db_utils.h` | 0 | Denormal prevention in feedback |
| detail::isNaN | `core/db_utils.h` | 0 | Input validation |
| detail::isInf | `core/db_utils.h` | 0 | Input validation |
| kPi, kTwoPi | `core/math_constants.h` | 0 | Coefficient calculations |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 existing processors
- [x] `specs/_architecture_/layer-2-processors.md` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: WaveguideResonator is a completely new class. No existing waveguide implementations found in the codebase. The related KarplusStrong processor uses similar components but has a different architecture (single delay loop vs. bidirectional).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Complete - All APIs verified from source headers.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | read | `[[nodiscard]] float read(size_t delaySamples) const noexcept` | Yes |
| DelayLine | readAllpass | `[[nodiscard]] float readAllpass(float delaySamples) noexcept` | Yes |
| OnePoleLP | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| OnePoleLP | reset | `void reset() noexcept` | Yes |
| OnePoleLP | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| OnePoleLP | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Allpass1Pole | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| Allpass1Pole | reset | `void reset() noexcept` | Yes |
| Allpass1Pole | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| Allpass1Pole | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| detail::flushDenormal | N/A | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | N/A | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | N/A | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/primitives/one_pole.h` - OnePoleLP class
- [x] `dsp/include/krate/dsp/primitives/allpass_1pole.h` - Allpass1Pole class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - Utility functions
- [x] `dsp/include/krate/dsp/core/math_constants.h` - Constants

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| DelayLine | `readAllpass()` is non-const (updates state) | Call after read operations |
| OnePoleLP | Returns input unchanged if not prepared | Always call `prepare()` first |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None identified | N/A | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| updateDelayLength() | Class-specific, accesses multiple members |
| updateLossFilters() | Class-specific, accesses multiple members |
| updateDispersionFilters() | Class-specific, accesses multiple members |

**Decision**: No new Layer 0 utilities needed. All required utilities already exist in db_utils.h and math_constants.h.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - Processors

**Related features at same layer** (from FLT-ROADMAP.md):
- Phase 13.1: ResonatorBank (already implemented) - different architecture (bandpass filters)
- Phase 13.2: KarplusStrong (already implemented) - single delay loop for strings
- Phase 13.4: ModalResonator (planned) - modal synthesis approach

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Bidirectional waveguide topology | MEDIUM | Bowed string extension, flute models | Keep local |
| Excitation point distribution | LOW | Specific to waveguide topology | Keep local |

### Detailed Analysis

**WaveguideResonator** provides:
- Bidirectional wave propagation
- Configurable end reflections (Kelly-Lochbaum)
- Frequency-dependent loss and dispersion

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| KarplusStrong | NO | Already uses single delay loop, different physics |
| ModalResonator | NO | Modal synthesis uses different approach (filters) |
| Future BowedString | MAYBE | Could extend waveguide for bow excitation |

**Recommendation**: Keep in this feature's file. Bidirectional waveguide is specific to pipe/tube modeling. If a BowedString feature is added later, consider extracting a shared base.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base with KarplusStrong | Different architectures (bidirectional vs. single loop) |
| Keep excitation point logic local | Specific to waveguide topology, no other consumers |

### Review Trigger

After implementing **Phase 13.4 (ModalResonator)**, review this section:
- [ ] Does ModalResonator need bidirectional topology? -> Consider shared utilities
- [ ] Any duplicated parameter smoothing patterns? -> Already using shared OnePoleSmoother

## Project Structure

### Documentation (this feature)

```text
specs/085-waveguide-resonator/
├── plan.md              # This file
├── research.md          # Phase 0: Kelly-Lochbaum research, waveguide theory
├── data-model.md        # Phase 1: WaveguideResonator class design
├── quickstart.md        # Phase 1: Usage examples
├── contracts/           # Phase 1: API contract
│   └── waveguide_resonator_api.h
└── tasks.md             # Phase 2: Implementation tasks (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── waveguide_resonator.h   # New: WaveguideResonator class
└── tests/
    └── unit/
        └── processors/
            └── waveguide_resonator_test.cpp  # New: Unit tests
```

**Structure Decision**: Standard DSP processor structure. Header-only implementation in processors directory, matching existing patterns (KarplusStrong, ResonatorBank).

## Complexity Tracking

> No constitution violations. Standard Layer 2 processor implementation.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| None | N/A | N/A |

---

## Generated Artifacts

| Artifact | Path | Status |
|----------|------|--------|
| Research | [research.md](research.md) | Complete |
| Data Model | [data-model.md](data-model.md) | Complete |
| Quickstart | [quickstart.md](quickstart.md) | Complete |
| API Contract | [contracts/waveguide_resonator_api.h](contracts/waveguide_resonator_api.h) | Complete |

## Next Steps

This plan is ready for Phase 2 (task generation via `/speckit.tasks`). The implementation agent should:

1. Create the test file first (`waveguide_resonator_test.cpp`)
2. Implement the header (`waveguide_resonator.h`)
3. Verify all FR and SC requirements
4. Update `specs/_architecture_/layer-2-processors.md`

---

## Pitch Accuracy Fix Plan (SC-002) - Added 2026-01-23

### Problem Summary

The current implementation produces pitch errors:
- 440Hz: +1.73 cents (sharp)
- 220Hz: +3.19 cents (sharp)
- 880Hz: -1.01 cents (flat)

SC-002 requires pitch accuracy within 1 cent.

### Root Cause

The current implementation uses a **fixed compensation value** (0.475 samples), but the actual
loop delay varies with frequency due to:

1. **Loss filter phase delay**: varies with frequency and cutoff
2. **Allpass interpolator delay**: varies with fractional delay amount

### Solution: Frequency-Dependent Phase Delay Compensation

Calculate the OnePoleLP filter's phase delay at the target fundamental frequency and
subtract this from the delay line length.

**Mathematical Basis**:

For a first-order lowpass filter with cutoff frequency `fc`, the phase delay at
frequency `f` is:

```
phaseDelay_samples = arctan(f / fc) / (2 * pi * f) * sampleRate
```

**Implementation Algorithm**:

```cpp
void updateDelayLengthFromSmoothed(float smoothedFreq) noexcept {
    // Step 1: Calculate target delay per direction
    float totalDelay = sampleRate_ / smoothedFreq;
    float delayPerDirection = totalDelay * 0.5f;

    // Step 2: Calculate loss filter phase delay at fundamental
    // The loss filter cutoff is calculated from the loss parameter
    float lossCutoff = calculateLossCutoff();  // same as in updateLossFilterFromSmoothed

    // Phase delay of first-order lowpass: arctan(f/fc) / (2*pi*f)
    // In samples: phaseDelay_seconds * sampleRate
    float omega = 2.0f * kPi * smoothedFreq;  // radians/sec
    float filterPhaseRadians = std::atan(smoothedFreq / lossCutoff);
    float filterPhaseDelaySamples = filterPhaseRadians / omega * sampleRate_;

    // Step 3: Account for TWO loss filters in the round-trip (one per reflection path)
    // Total round-trip phase delay = 2 * filterPhaseDelaySamples
    // Per-direction contribution = filterPhaseDelaySamples (half the round-trip)
    float compensation = filterPhaseDelaySamples;

    // Step 4: Set delay line length
    delaySamples_ = delayPerDirection - compensation;

    // Enforce minimum delay
    delaySamples_ = std::max(delaySamples_, static_cast<float>(kMinDelaySamples));
}
```

### Implementation Steps

1. **Verify current test failures**: Build and run to confirm current pitch errors
2. **Add calculateLossCutoff helper**: Extract cutoff calculation into reusable method
3. **Implement phase delay compensation**: Replace fixed compensation with calculated value
4. **Run tests at multiple frequencies**: Verify 440Hz, 220Hz, 880Hz all pass
5. **Test edge cases**: Verify behavior at frequency extremes (20Hz, 10kHz)

### Validation Criteria

| Frequency | Target | Tolerance | Test Section |
|-----------|--------|-----------|--------------|
| 220Hz | ±1 cent | ±0.127Hz | T012 |
| 440Hz | ±1 cent | ±0.254Hz | T012 |
| 880Hz | ±1 cent | ±0.509Hz | T012 |

### Risk Assessment

**Low Risk**: This is a targeted fix to the delay compensation formula. The change:
- Affects only `updateDelayLengthFromSmoothed()`
- Uses existing mathematical principles from research
- Does not change the signal flow or architecture
