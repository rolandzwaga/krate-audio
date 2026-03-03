# Implementation Plan: Modulation System

**Branch**: `008-modulation-system` | **Date**: 2026-01-29 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/008-modulation-system/spec.md`

---

## Summary

The Modulation System provides a complete parameter modulation engine for the Disrumpo plugin (Week 9-10 per roadmap, Tasks T9.1-T9.27, Milestone M6). A ModulationEngine class at Layer 3 (systems) orchestrates 12 modulation sources (2 LFOs, Envelope Follower, Random, 4 Macros, Chaos, Sample & Hold, Pitch Follower, Transient Detector) and routes them to destination parameters via up to 32 simultaneous routings. Each routing specifies source, destination, bipolar amount (-1 to +1), and curve shape (Linear, Exponential, S-Curve, Stepped). The engine reuses existing KrateDSP primitives (LFO, EnvelopeFollower, PitchDetector, OnePoleSmoother, ChaosWaveshaper attractor logic) and the existing ModulationMatrix routing infrastructure. New components include ChaosModSource, SampleHoldSource, PitchFollowerSource, TransientDetector, RandomSource, and modulation curve application functions.

**Tasks**: T9.1-T9.27 per roadmap.md | **Milestone**: M6

---

## Technical Context

**Language/Version**: C++20 (MSVC 19.x / Clang 15+)
**Primary Dependencies**: Steinberg VST3 SDK 3.7+, VSTGUI 4.11+, KrateDSP (internal library)
**Storage**: VST3 preset state serialization (IBStream)
**Testing**: Catch2 3.x (testing-guide skill auto-loads)
**Target Platform**: Windows 10+, macOS 10.13+, Linux (x86_64)
**Project Type**: VST3 plugin with monorepo structure (dsp/ + plugins/Disrumpo/)
**Performance Goals**: <1% CPU overhead for 32 active routings with all 12 sources at 44.1kHz/512 samples (SC-011)
**Constraints**: Real-time safe (no allocations/locks in audio thread), 20ms parameter smoothing for routing amounts, modulation curves applied per-sample
**Scale/Scope**: 93 functional requirements, 18 success criteria, 14 user stories, ~12-15 new source files

---

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] ModulationEngine lives in DSP layer (Processor side)
- [x] UI panel communicates via IParameterChanges only
- [x] Processor functions correctly without controller

**Required Check - Principle II (Real-Time Safety):**
- [x] ModulationEngine.process() is noexcept with no allocations
- [x] All modulation sources use pre-allocated state
- [x] No locks or blocking in audio path
- [x] Chaos attractor uses only arithmetic (no branches that stall)
- [x] OnePoleSmoother used for routing depth smoothing (20ms)
- [x] Lock-free SPSC buffer pattern for modulation visualization data

**Required Check - Principle III (Modern C++):**
- [x] C++20 features: constexpr, [[nodiscard]], std::array, enum class
- [x] RAII for resource management
- [x] Value semantics for DSP state
- [x] Smart pointers where ownership transfer needed

**Required Check - Principle IX (Layered Architecture):**
- [x] ModulationEngine at Layer 3 (systems) - composes Layer 1/2 primitives
- [x] Modulation curve functions at Layer 0 (core) - pure math
- [x] New sources (ChaosModSource, TransientDetector, etc.) at Layer 2 (processors)
- [x] Reuses Layer 1 LFO, OnePoleSmoother, PitchDetector
- [x] Reuses Layer 2 EnvelopeFollower
- [x] Layer dependencies: 3 depends on 0-2 only

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] All 93 FRs and 18 SCs will be tracked with evidence
- [x] No test thresholds will be relaxed from spec

---

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ModulationEngine | `grep -r "class ModulationEngine" dsp/ plugins/` | No | Create New at Layer 3 |
| ChaosModSource | `grep -r "class ChaosModSource" dsp/ plugins/` | No | Create New at Layer 2 |
| SampleHoldSource | `grep -r "class SampleHoldSource" dsp/ plugins/` | No | Create New at Layer 2 |
| PitchFollowerSource | `grep -r "class PitchFollowerSource" dsp/ plugins/` | No | Create New at Layer 2 |
| TransientDetector | `grep -r "class TransientDetector" dsp/ plugins/` | No | Create New at Layer 2 |
| RandomSource | `grep -r "class RandomSource" dsp/ plugins/` | No | Create New at Layer 2 |
| ModSource (enum) | `grep -r "enum class ModSource" dsp/ plugins/` | No | Create New at Layer 0 |
| ModCurve (enum) | `grep -r "enum class ModCurve" dsp/ plugins/` | No | Create New at Layer 0 |
| ModRouting (struct) | `grep -r "struct ModRouting" dsp/ plugins/` | No | Create New at Layer 0 |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| applyModCurve | `grep -r "applyModCurve" dsp/` | No | - | Create New at Layer 0 |
| hzToModValue | `grep -r "hzToModValue" dsp/` | No | - | Create New (PitchFollower) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| LFO | dsp/include/krate/dsp/primitives/lfo.h | 1 | LFO 1 and LFO 2 sources (6 waveforms, tempo sync, phase offset) |
| EnvelopeFollower | dsp/include/krate/dsp/processors/envelope_follower.h | 2 | Envelope Follower source (attack/release, detection modes) |
| PitchDetector | dsp/include/krate/dsp/primitives/pitch_detector.h | 1 | Pitch Follower source (autocorrelation-based pitch detection) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Routing depth smoothing, S&H slew, Random smoothness, Pitch tracking |
| ChaosWaveshaper | dsp/include/krate/dsp/primitives/chaos_waveshaper.h | 1 | Reference for chaos attractor logic (Lorenz/Rossler/Chua/Henon); will extract attractor update logic |
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | Random source noise generation |
| BlockContext | dsp/include/krate/dsp/core/block_context.h | 0 | Tempo information for LFO sync |
| NoteValue/NoteModifier | dsp/include/krate/dsp/core/note_value.h | 0 | LFO tempo sync note values |
| ModulationMatrix | dsp/include/krate/dsp/systems/modulation_matrix.h | 3 | Existing routing infrastructure: ModulationSource interface, route management, smoothing |
| ModulationSource (interface) | dsp/include/krate/dsp/systems/modulation_matrix.h | 3 | Abstract base for all sources: getCurrentValue(), getSourceRange() |
| SweepPositionBuffer | dsp/include/krate/dsp/primitives/sweep_position_buffer.h | 1 | Pattern reference for lock-free audio-UI communication |
| Sigmoid::tanhVariable | dsp/include/krate/dsp/core/sigmoid.h | 0 | Chaos source soft-limit normalization |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing in all source processing |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection for robustness |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (random.h, block_context.h, note_value.h, sigmoid.h, db_utils.h)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (lfo.h, smoother.h, pitch_detector.h, chaos_waveshaper.h, sweep_position_buffer.h)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (envelope_follower.h)
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (modulation_matrix.h -- existing, will extend/compose)
- [x] `plugins/` - Plugin source (no Disrumpo directory yet; no conflicts)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new types are unique and not found in the codebase. The existing `ModulationMatrix` at Layer 3 provides routing infrastructure (source/destination registration, route management, depth smoothing, multi-source summation) that will be composed by ModulationEngine rather than duplicated. The existing `ModulationSource` abstract interface will be implemented by all new source classes. Key distinction: the existing matrix handles routing mechanics; the new ModulationEngine adds modulation curves, ModSource enum dispatch, source ownership, and Disrumpo-specific parameter integration.

---

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| LFO | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| LFO | process | `[[nodiscard]] float process() noexcept` | Yes |
| LFO | processBlock | `void processBlock(float* output, size_t numSamples) noexcept` | Yes |
| LFO | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| LFO | setWaveform | `void setWaveform(Waveform waveform) noexcept` | Yes |
| LFO | setPhaseOffset | `void setPhaseOffset(float degrees) noexcept` | Yes |
| LFO | setTempoSync | `void setTempoSync(bool enabled) noexcept` | Yes |
| LFO | setTempo | `void setTempo(float bpm) noexcept` | Yes |
| LFO | setNoteValue | `void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept` | Yes |
| LFO | setRetriggerEnabled | `void setRetriggerEnabled(bool enabled) noexcept` | Yes |
| LFO | retrigger | `void retrigger() noexcept` | Yes |
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setMode | `void setMode(DetectionMode mode) noexcept` | Yes |
| EnvelopeFollower | setSidechainEnabled | `void setSidechainEnabled(bool enabled) noexcept` | Yes |
| EnvelopeFollower | setSidechainCutoff | `void setSidechainCutoff(float hz) noexcept` | Yes |
| PitchDetector | prepare | `void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept` | Yes |
| PitchDetector | push | `void push(float sample) noexcept` | Yes |
| PitchDetector | pushBlock | `void pushBlock(const float* samples, std::size_t numSamples) noexcept` | Yes |
| PitchDetector | getDetectedFrequency | `[[nodiscard]] float getDetectedFrequency() const noexcept` | Yes |
| PitchDetector | getConfidence | `[[nodiscard]] float getConfidence() const noexcept` | Yes |
| PitchDetector | isPitchValid | `[[nodiscard]] bool isPitchValid() const noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` (returns [-1, 1]) | Yes |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` (returns [0, 1]) | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes |
| BlockContext | isPlaying | `bool isPlaying = false` | Yes |
| BlockContext | sampleRate | `double sampleRate = 44100.0` | Yes |
| BlockContext | blockSize | `size_t blockSize = 512` | Yes |
| ModulationSource | getCurrentValue | `[[nodiscard]] virtual float getCurrentValue() const noexcept = 0` | Yes |
| ModulationSource | getSourceRange | `[[nodiscard]] virtual std::pair<float, float> getSourceRange() const noexcept = 0` | Yes |
| ModulationMatrix | prepare | `void prepare(double sampleRate, size_t maxBlockSize, size_t maxRoutes = kMaxModulationRoutes) noexcept` | Yes |
| ModulationMatrix | registerSource | `bool registerSource(uint8_t id, ModulationSource* source) noexcept` | Yes |
| ModulationMatrix | registerDestination | `bool registerDestination(uint8_t id, float minValue, float maxValue, const char* label = nullptr) noexcept` | Yes |
| ModulationMatrix | createRoute | `int createRoute(uint8_t sourceId, uint8_t destinationId, float depth = 1.0f, ModulationMode mode = ModulationMode::Bipolar) noexcept` | Yes |
| ModulationMatrix | process | `void process(size_t numSamples) noexcept` | Yes |
| ModulationMatrix | getModulatedValue | `[[nodiscard]] float getModulatedValue(uint8_t destinationId, float baseValue) const noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class
- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/primitives/pitch_detector.h` - PitchDetector class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother, LinearRamp, SlewLimiter
- [x] `dsp/include/krate/dsp/primitives/chaos_waveshaper.h` - ChaosWaveshaper (attractor reference)
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 PRNG
- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct
- [x] `dsp/include/krate/dsp/core/note_value.h` - NoteValue, NoteModifier enums, getBeatsForNote()
- [x] `dsp/include/krate/dsp/systems/modulation_matrix.h` - ModulationMatrix, ModulationSource interface
- [x] `dsp/include/krate/dsp/primitives/sweep_position_buffer.h` - Lock-free SPSC buffer pattern
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid::tanhVariable for chaos normalization
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::flushDenormal

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| LFO | Non-copyable (deleted copy ctor) | Use move or pointer |
| LFO | Waveform enum is `Krate::DSP::Waveform`, not `LFOWaveform` | `lfo.setWaveform(Krate::DSP::Waveform::Sine)` |
| LFO | Tempo sync requires both setTempoSync(true) AND setTempo(bpm) | Always call both |
| EnvelopeFollower | prepare() takes double, not float for sampleRate | `env.prepare(44100.0, 512)` |
| EnvelopeFollower | getCurrentValue() does NOT advance state | Use processSample() in audio loop |
| PitchDetector | Uses push()/pushBlock() to feed samples, not process() | Feed samples, then query getDetectedFrequency() |
| PitchDetector | getConfidence() returns [0,1], kConfidenceThreshold is 0.3 | Use isPitchValid() or compare to custom threshold |
| OnePoleSmoother | Uses snapTo() not snap() | `smoother.snapTo(value)` |
| OnePoleSmoother | setTarget() has ITERUM_NOINLINE for NaN safety | Fine for use, do not inline manually |
| ChaosWaveshaper | Non-copyable (contains Oversampler) | Reference only for attractor math; create separate ChaosModSource |
| ModulationMatrix | Depth range is [0, 1] not [-1, +1] | Bipolar amount handled externally; depth is always positive |
| ModulationMatrix | ModulationMode::Bipolar vs Unipolar affects source mapping | Bipolar: source [-1,+1] direct; Unipolar: source [-1,+1] -> [0,1] |
| ModulationMatrix | Max 16 sources and 16 destinations by default | kMaxModulationSources=16, kMaxModulationDestinations=16 |
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| Xorshift32 | nextFloat() returns [-1, 1] bipolar | Use nextUnipolar() for [0, 1] |

---

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| applyModCurve(ModCurve, float) | Pure math, 4 curve formulas, stateless | dsp/core/modulation_curves.h | ModulationEngine, potential future modulation features |
| ModSource enum | Identifies modulation sources, plugin-independent concept | dsp/core/modulation_types.h | ModulationEngine, UI, preset system |
| ModCurve enum | Identifies curve shapes, pure enum | dsp/core/modulation_types.h | ModulationEngine, routing matrix, UI |
| ModRouting struct | Routing descriptor, value type | dsp/core/modulation_types.h | ModulationEngine, preset serialization |
| hzToModValue(freq, minHz, maxHz) | Logarithmic frequency mapping, reusable | dsp/core/frequency_utils.h (if exists) or within PitchFollowerSource | PitchFollowerSource, potential spectral features |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| ChaosModSource attractor updates | Class owns state; complex multi-model dispatch |
| TransientDetector envelope processing | Class-specific state management |
| SampleHoldSource sampling logic | Tied to source selection state |

**Decision**: Extract ModSource/ModCurve/ModRouting and applyModCurve() to Layer 0 `dsp/core/modulation_types.h` and `dsp/core/modulation_curves.h` since they are pure data/math with no dependencies. Keep source-specific processing as member functions.

---

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 - DSP Systems (ModulationEngine) / Layer 2 - Processors (new sources)

**Related features at same layer**:
- 009-intelligent-oversampling (Week 11): May need modulation-aware oversampling factor
- 010-preset-system (Week 12): Must serialize all modulation state
- 011-spectrum-metering (Week 13): May display modulation activity indicators

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| ModulationEngine | HIGH | Any plugin needing parameter modulation | Keep in KrateDSP systems/ for potential multi-plugin reuse |
| ChaosModSource | MEDIUM | Chaos modulation in other contexts | Keep in KrateDSP processors/ |
| TransientDetector | MEDIUM | Dynamics processing, gate effects | Keep in KrateDSP processors/ |
| PitchFollowerSource | MEDIUM | Pitch-tracking effects | Keep in KrateDSP processors/ |
| SampleHoldSource | MEDIUM | Classic synth modulation patterns | Keep in KrateDSP processors/ |
| RandomSource | MEDIUM | Noise modulation for any plugin | Keep in KrateDSP processors/ |
| ModulationSource interface | HIGH | Already exists in modulation_matrix.h | Reuse as-is |
| Modulation curves | HIGH | Any feature needing response shaping | Extract to Layer 0 core/ |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| New sources go in dsp/processors/ not plugin/ | Reusable across plugins; not Disrumpo-specific |
| ModulationEngine goes in dsp/systems/ | Composes sources + routing; may serve multiple plugins |
| Extend existing ModulationMatrix | Avoid duplicating routing infrastructure; compose with new curve logic |
| Modulation types/curves at Layer 0 | Pure data types and math; no dependencies; maximum reusability |

---

## Project Structure

### Documentation (this feature)

```text
specs/008-modulation-system/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 output - research findings
├── data-model.md        # Phase 1 output - entity definitions
├── quickstart.md        # Phase 1 output - implementation guide
├── contracts/           # Phase 1 output - API contracts
│   ├── modulation_engine.h
│   ├── chaos_mod_source.h
│   ├── sample_hold_source.h
│   ├── pitch_follower_source.h
│   ├── transient_detector.h
│   ├── random_source.h
│   └── modulation_curves.h
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   ├── modulation_types.h          # Layer 0: ModSource, ModCurve, ModRouting
│   │   └── modulation_curves.h         # Layer 0: applyModCurve() pure functions
│   ├── processors/
│   │   ├── chaos_mod_source.h          # Layer 2: Chaos attractor modulation source
│   │   ├── chaos_mod_source.cpp        # Implementation (if needed for size)
│   │   ├── sample_hold_source.h        # Layer 2: Sample & Hold source
│   │   ├── pitch_follower_source.h     # Layer 2: Pitch follower source
│   │   ├── transient_detector.h        # Layer 2: Transient detector source
│   │   └── random_source.h            # Layer 2: Random modulation source
│   └── systems/
│       ├── modulation_matrix.h         # Layer 3: EXISTING - routing infrastructure
│       └── modulation_engine.h         # Layer 3: NEW - orchestrates sources + routing + curves
└── tests/
    ├── unit/
    │   ├── core/
    │   │   ├── modulation_types_test.cpp
    │   │   └── modulation_curves_test.cpp
    │   ├── processors/
    │   │   ├── chaos_mod_source_test.cpp
    │   │   ├── sample_hold_source_test.cpp
    │   │   ├── pitch_follower_source_test.cpp
    │   │   ├── transient_detector_test.cpp
    │   │   └── random_source_test.cpp
    │   └── systems/
    │       └── modulation_engine_test.cpp
    └── integration/
        └── modulation_integration_test.cpp

plugins/Disrumpo/
├── src/
│   ├── plugin_ids.h                    # Add modulation parameter IDs (200-445)
│   ├── processor/
│   │   └── processor.cpp               # Integrate ModulationEngine into audio callback
│   └── controller/
│       └── controller.cpp              # Register modulation parameters
└── resources/
    └── editor.uidesc                   # Modulation UI panels (Sources, Routing Matrix, Macros)
```

**Structure Decision**: All new DSP components go in the KrateDSP library (`dsp/`) since they are plugin-agnostic. Disrumpo-specific integration (parameter IDs, processor hookup, UI panels) goes in `plugins/Disrumpo/`. The ModulationEngine at Layer 3 composes existing ModulationMatrix (Layer 3) with new sources (Layer 2) and curve functions (Layer 0).

---

## Complexity Tracking

No violations requiring justification. Architecture follows established layered patterns.

---

## Phase 0: Research Summary

See [research.md](research.md) for detailed findings.

### Key Decisions

| Decision | Rationale | Alternatives Considered |
|----------|-----------|------------------------|
| Compose with existing ModulationMatrix | Reuse routing/smoothing infrastructure; avoid ODR | Build new routing from scratch (rejected: duplicates ~400 LOC) |
| ChaosModSource extracts attractor math from ChaosWaveshaper | ChaosWaveshaper couples attractor to waveshaping; mod source needs raw attractor output only | Reuse ChaosWaveshaper directly (rejected: it applies waveshaping to audio, not modulation) |
| Layer 0 for types/curves, Layer 2 for sources | Types are pure data; sources depend on Layer 1 primitives (Smoother, LFO, PitchDetector) | All at Layer 2 (rejected: types have no deps, violates minimality); All at Layer 3 (rejected: prevents lower-layer reuse) |
| ModulationEngine wraps ModulationMatrix | Engine owns sources + adds curves; Matrix handles routing mechanics | Subclass ModulationMatrix (rejected: Matrix is not designed for inheritance, concrete class) |
| All sources implement existing ModulationSource interface | Interface already exists and is used by ModulationMatrix; no new interface needed | Create new interface (rejected: ODR risk, unnecessary duplication) |
| Soft-limit chaos with tanh(x/scale) | Smooth saturation; prevents discontinuities from coupling perturbations | Hard clamp (rejected: audible artifacts); Assume bounds (rejected: coupling can exceed) |

### Clarifications Already Resolved

| Question | Resolution |
|----------|------------|
| Macro curve application order | Min/Max mapping FIRST, then curve. Formula: `output = applyCurve(Min + knobValue * (Max - Min))` |
| Sample & Hold LFO source | User-selectable via dropdown: Random, LFO 1, LFO 2, External |
| Transient retrigger during attack | Restart attack from current level toward 1.0 |
| Stepped curve levels | 4 discrete levels: 0, 0.333, 0.667, 1.0 (formula: `floor(x * 4) / 3`) |
| Chaos normalization | Soft limit: `tanh(x / scale)` per model |

---

## Phase 1: Design Artifacts

### Data Model Summary

See [data-model.md](data-model.md) for complete entity definitions.

**Key Entities:**
1. **ModSource** - Enum (13 values) identifying modulation source type
2. **ModCurve** - Enum (4 values) identifying response curve shape
3. **ModRouting** - Struct describing source-to-destination connection
4. **ModulationEngine** - Layer 3 system orchestrating all sources and routing
5. **ChaosModSource** - Layer 2 chaotic attractor modulation source
6. **SampleHoldSource** - Layer 2 sample & hold modulation source
7. **PitchFollowerSource** - Layer 2 pitch-to-modulation converter
8. **TransientDetector** - Layer 2 transient-triggered envelope generator
9. **RandomSource** - Layer 2 random value generator with smoothing

### API Contracts

See [contracts/](contracts/) directory for header specifications.

### Implementation Quickstart

See [quickstart.md](quickstart.md) for step-by-step implementation guide.

---

## Constitution Re-Check (Post-Design)

- [x] Principle I (VST3 Architecture): ModulationEngine is DSP-only; UI communicates via parameters
- [x] Principle II (Real-Time Safety): All sources noexcept, no allocations in process; attractor uses arithmetic only
- [x] Principle III (Modern C++): C++20 features, RAII, value semantics, enum class
- [x] Principle IX (Layers): Types at L0, sources at L2, engine at L3; all dependencies downward only
- [x] Principle X (DSP Constraints): Per-sample modulation; smoothing on depth changes
- [x] Principle XI (Performance): <1% CPU for 32 routings per spec SC-011
- [x] Principle XII (Test-First): Test files defined in project structure; all FR/SC tracked
- [x] Principle XIV (ODR): No conflicts found; all new types verified unique; existing ModulationMatrix/Source reused
- [x] Principle XVI (Honest Completion): All 93 FRs and 18 SCs mapped to implementation tasks

**Gate Status: PASSED**

---

## Implementation Notes

### Parameter ID Reference (from spec.md)

```
LFO 1:      200-206 (Rate, Shape, Phase, Sync, NoteValue, Unipolar, Retrigger)
LFO 2:      220-226 (same layout)
EnvFollower: 240-243 (Attack, Release, Sensitivity, Source)
Random:     260-262 (Rate, Smoothness, Sync)
Chaos:      280-282 (Model, Speed, Coupling)
S&H:        285-287 (Source, Rate, Slew)
PitchFollow: 290-293 (MinHz, MaxHz, Confidence, TrackingSpeed)
Transient:  295-297 (Sensitivity, Attack, Decay)
Routing:    300-427 (32 routings x 4 params: Source, Dest, Amount, Curve)
Macros:     430-445 (4 macros x 4 params: Value, Min, Max, Curve)
```

### Existing ModulationMatrix Limitations and Extensions Needed

The existing `ModulationMatrix` (020-modulation-matrix) provides:
- Source/destination registration (16 sources, 16 destinations)
- Route management (up to 32 routes)
- Depth smoothing (20ms OnePoleSmoother)
- Multi-source summation with clamping
- Bipolar/Unipolar mode selection

**What ModulationEngine adds on top:**
- ModSource enum dispatch (maps enum to concrete source objects)
- ModCurve application (Linear, Exponential, S-Curve, Stepped)
- Bipolar amount handling (amount carries sign; curve applied to abs value)
- Source ownership and lifecycle management
- Block-level source processing before routing
- Macro parameter processing (Min/Max mapping + curve)
- Integration with Disrumpo's parameter system

### UI Integration Points

1. **Sources Panel** - Level 3 (Expert) disclosure section with controls for all 12 sources
2. **Routing Matrix Panel** - Add/edit/remove routing rows with source/dest/amount/curve
3. **Macros Panel** - 4 macro knobs with Min/Max/Curve per macro
4. **Modulation Visualization** - Lock-free SPSC buffer pattern from SweepPositionBuffer for source activity display

### Test Categories

1. **Unit Tests (Layer 0)**: Modulation types validation, curve formulas at key positions
2. **Unit Tests (Layer 2)**: Each source individually (ChaosModSource, TransientDetector, etc.)
3. **Unit Tests (Layer 3)**: ModulationEngine routing, multi-source summation, clamping
4. **Integration Tests**: End-to-end LFO-to-parameter modulation verification
