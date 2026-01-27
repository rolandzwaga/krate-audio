# Implementation Plan: Tape Machine System

**Branch**: `066-tape-machine` | **Date**: 2026-01-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/066-tape-machine/spec.md`

## Summary

Implement a Layer 3 TapeMachine system that composes existing DSP components (TapeSaturator, NoiseGenerator, LFO, Biquad, OnePoleSmoother) to provide comprehensive tape machine emulation with:
- Multiple machine models (Studer/Ampex) with preset defaults
- Tape formulations (Type456/Type900/TypeGP9) affecting saturation characteristics
- Tape speeds (7.5/15/30 ips) affecting frequency response
- Full manual control over head bump, HF rolloff, wow/flutter, and hiss
- Real-time safe processing with parameter smoothing

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Krate::DSP library (TapeSaturator, NoiseGenerator, LFO, Biquad, OnePoleSmoother)
**Storage**: N/A (stateless processing component)
**Testing**: Catch2 v3 (existing test infrastructure in `dsp/tests/`)
**Target Platform**: Windows 10+, macOS 11+, Linux (cross-platform VST3 plugin)
**Project Type**: Monorepo - shared DSP library
**Performance Goals**: < 1% CPU single core @ 192kHz stereo (SC-001)
**Constraints**: Real-time safe (no allocations in process), < 5ms smoothing for parameter changes (SC-006)
**Scale/Scope**: Single Layer 3 system composing 5+ existing components

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in process()
- [x] noexcept processing methods
- [x] Pre-allocated buffers in prepare()
- [x] Parameter smoothing for click-free operation

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 3 system - composes only Layers 0-2
- [x] TapeSaturator (Layer 2) - REUSE
- [x] NoiseGenerator (Layer 2) - REUSE
- [x] LFO (Layer 1) - REUSE
- [x] Biquad (Layer 1) - REUSE
- [x] OnePoleSmoother (Layer 1) - REUSE

**Required Check - Principle X (DSP Constraints):**
- [x] DC blocking handled by TapeSaturator internally
- [x] Oversampling handled by TapeSaturator internally
- [x] Linear/Triangle waveform for modulated delay (FR-030)

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

**Classes/Structs to be created**: TapeMachine, MachineModel, TapeSpeed, TapeType

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TapeMachine | `grep -r "class TapeMachine" dsp/ plugins/` | No | Create New |
| MachineModel | `grep -r "MachineModel" dsp/ plugins/` | No | Create New |
| TapeSpeed | `grep -r "TapeSpeed" dsp/ plugins/` | No | Create New |
| TapeType | `grep -r "TapeType" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None - all utility functions exist in Layer 0

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" dsp/` | Yes | core/db_utils.h | Reuse |
| gainToDb | `grep -r "gainToDb" dsp/` | Yes | core/db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| TapeSaturator | dsp/include/krate/dsp/processors/tape_saturator.h | 2 | Core saturation engine |
| NoiseGenerator | dsp/include/krate/dsp/processors/noise_generator.h | 2 | TapeHiss noise type |
| LFO | dsp/include/krate/dsp/primitives/lfo.h | 1 | Wow and flutter modulation (2 instances) |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | Head bump (Peak) and HF rolloff (Lowpass) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | dB to linear conversion |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (TapeMachine, MachineModel, TapeSpeed, TapeType) are unique and not found in the codebase. The search confirmed no existing implementations exist.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| TapeSaturator | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| TapeSaturator | reset | `void reset() noexcept` | Yes |
| TapeSaturator | process | `void process(float* buffer, size_t numSamples) noexcept` | Yes |
| TapeSaturator | setDrive | `void setDrive(float dB) noexcept` | Yes |
| TapeSaturator | setSaturation | `void setSaturation(float amount) noexcept` | Yes |
| TapeSaturator | setBias | `void setBias(float bias) noexcept` | Yes |
| TapeSaturator | setModel | `void setModel(TapeModel model) noexcept` | Yes |
| TapeSaturator | setSolver | `void setSolver(HysteresisSolver solver) noexcept` | Yes |
| NoiseGenerator | prepare | `void prepare(float sampleRate, size_t maxBlockSize) noexcept` | Yes |
| NoiseGenerator | reset | `void reset() noexcept` | Yes |
| NoiseGenerator | setNoiseEnabled | `void setNoiseEnabled(NoiseType type, bool enabled) noexcept` | Yes |
| NoiseGenerator | setNoiseLevel | `void setNoiseLevel(NoiseType type, float dB) noexcept` | Yes |
| NoiseGenerator | processMix | `void processMix(const float* input, float* output, size_t numSamples) noexcept` | Yes |
| LFO | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| LFO | reset | `void reset() noexcept` | Yes |
| LFO | setWaveform | `void setWaveform(Waveform waveform) noexcept` | Yes |
| LFO | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| LFO | process | `[[nodiscard]] float process() noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/tape_saturator.h` - TapeSaturator class
- [x] `dsp/include/krate/dsp/processors/noise_generator.h` - NoiseGenerator class
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain, gainToDb

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| NoiseGenerator | Uses `float sampleRate` not `double` in prepare() | `noise_.prepare(static_cast<float>(sampleRate), maxBlockSize)` |
| LFO | Uses `double sampleRate` in prepare() | `lfoWow_.prepare(sampleRate)` |
| Biquad | configure() takes `float sampleRate` | `headBump_.configure(..., static_cast<float>(sampleRate))` |
| TapeSaturator | setMix() is 0-1 range not percentage | `saturator_.setMix(1.0f)` for 100% wet |
| OnePoleSmoother | snapTo() sets BOTH current and target | Use `snapTo()` in prepare(), `setTarget()` for runtime changes |
| Waveform | Triangle enum value is `Waveform::Triangle` | `lfo.setWaveform(Waveform::Triangle)` |
| FilterType | Peak for head bump, Lowpass for HF rolloff | `FilterType::Peak`, `FilterType::Lowpass` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None | All needed utilities already exist in db_utils.h | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| applyMachineModelDefaults() | Internal configuration helper, only used by TapeMachine |
| applyTapeSpeedDefaults() | Internal configuration helper, only used by TapeMachine |
| applyTapeTypeToSaturator() | Internal configuration helper, only used by TapeMachine |

**Decision**: No Layer 0 extractions needed - all utility functions already exist.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from roadmap/known plans):
- BBD Mode (bucket-brigade delay with clock noise, similar modulation)
- Vinyl Mode (similar analog character, different frequency profile)
- Reel-to-Reel Mode (tape machine variant with playback head modeling)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| TapeSpeed enum | MEDIUM | Reel-to-Reel Mode | Keep local for now |
| TapeType enum | MEDIUM | Reel-to-Reel Mode | Keep local for now |
| MachineModel enum | LOW | Specific to tape machines | Keep local |
| Head bump + HF rolloff pattern | HIGH | Vinyl Mode, general analog modes | Document pattern, don't extract yet |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep enums local to TapeMachine | First consumer - extract after 2nd use per YAGNI |
| Document filter pattern | Head bump + HF rolloff may apply to Vinyl Mode |

### Review Trigger

After implementing **Vinyl Mode**, review this section:
- [ ] Does Vinyl Mode need TapeSpeed/TapeType or similar? -> Consider extraction
- [ ] Does Vinyl Mode use head bump + rolloff pattern? -> Consider shared base

## Project Structure

### Documentation (this feature)

```text
specs/066-tape-machine/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── tape_machine_api.h  # Public API contract
└── tasks.md             # Phase 2 output (separate command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── systems/
│       └── tape_machine.h    # TapeMachine class (header-only)
└── tests/
    └── systems/
        └── tape_machine_tests.cpp  # Unit tests
```

**Structure Decision**: Header-only implementation in `dsp/include/krate/dsp/systems/tape_machine.h` following the pattern of other Layer 3 systems (AmpChannel, CharacterProcessor). Tests in `dsp/tests/systems/tape_machine_tests.cpp`.

## Complexity Tracking

No Constitution violations requiring justification.
