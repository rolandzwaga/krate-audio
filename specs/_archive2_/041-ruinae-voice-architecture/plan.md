# Implementation Plan: Ruinae Voice Architecture

**Branch**: `041-ruinae-voice-architecture` | **Date**: 2026-02-08 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/041-ruinae-voice-architecture/spec.md`

## Summary

Build the complete per-voice signal chain for the Ruinae chaos/spectral hybrid synthesizer. This composes existing DSP components (10 oscillator types, 4 filter types, 6 distortion types, TranceGate, ADSR envelopes, LFO) into three new Layer 3 system components: SelectableOscillator (variant-based oscillator wrapper), VoiceModRouter (per-voice modulation routing), and RuinaeVoice (the complete voice unit). The architecture uses `std::variant` with visitor dispatch following the established DistortionRack pattern.

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang 15+, GCC 12+)
**Primary Dependencies**: KrateDSP library (Layers 0-2), VST3 SDK (not used in this spec)
**Storage**: N/A (all state in pre-allocated memory)
**Testing**: Catch2 v3 via CTest
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo DSP library
**Performance Goals**: SC-001: <1% CPU for basic voice; SC-002: <3% CPU for SpectralMorph voice; SC-003: <8% CPU for 8 concurrent basic voices
**Constraints**: Zero allocation in processBlock(); all buffers pre-allocated in prepare(); real-time safe (no locks, exceptions, I/O)
**Scale/Scope**: 4 new header files, 3 test files, ~2000-3000 lines of code

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation)**: N/A -- this is DSP-only, no plugin code.
**Principle II (Real-Time Safety)**: PASS -- all processBlock methods will be noexcept, no allocation, no locks.
**Principle III (Modern C++)**: PASS -- C++20, std::variant, RAII, smart pointers not needed (value semantics).
**Principle IV (SIMD & DSP Optimization)**: PASS -- see SIMD Analysis section below.
**Principle VI (Cross-Platform)**: PASS -- pure C++ DSP, no platform-specific code.
**Principle VII (Project Structure)**: PASS -- follows monorepo layout, Layer 3 in systems/.
**Principle VIII (Testing)**: PASS -- test-first, Catch2, CI on all platforms.
**Principle IX (Layered Architecture)**: PASS -- Layer 3 depends on Layers 0-2 only.
**Principle X (DSP Constraints)**: PASS -- DC blocking after distortion, feedback limiting not applicable.
**Principle XII (Test-First)**: PASS -- tests written before implementation.
**Principle XIV (ODR Prevention)**: PASS -- see Codebase Research below.
**Principle XV (Pre-Implementation Research)**: PASS -- see Codebase Research below.
**Principle XVI (Honest Completion)**: PASS -- compliance table will be filled with evidence.

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

### Mandatory Searches Performed

**Classes/Structs to be created**: SelectableOscillator, RuinaeVoice, VoiceModRouter, VoiceModRoute

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SelectableOscillator | `grep -r "class SelectableOscillator" dsp/` | No | Create New |
| RuinaeVoice | `grep -r "class RuinaeVoice" dsp/` | No | Create New |
| VoiceModRouter | `grep -r "class VoiceModRouter" dsp/` | No | Create New |
| VoiceModRoute | `grep -r "struct VoiceModRoute" dsp/` | No | Create New |
| OscType | `grep -r "enum class OscType" dsp/` | No | Create New |
| MixMode | `grep -r "enum class MixMode" dsp/` | No | Create New |
| RuinaeFilterType | `grep -r "enum class RuinaeFilterType" dsp/` | No | Create New |
| RuinaeDistortionType | `grep -r "enum class RuinaeDistortionType" dsp/` | No | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| PolyBlepOscillator | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | 1 | OscType::PolyBLEP in SelectableOscillator variant |
| WavetableOscillator | `dsp/include/krate/dsp/primitives/wavetable_oscillator.h` | 1 | OscType::Wavetable in variant |
| NoiseOscillator | `dsp/include/krate/dsp/primitives/noise_oscillator.h` | 1 | OscType::Noise in variant |
| ChaosOscillator | `dsp/include/krate/dsp/processors/chaos_oscillator.h` | 2 | OscType::Chaos in variant |
| ParticleOscillator | `dsp/include/krate/dsp/processors/particle_oscillator.h` | 2 | OscType::Particle in variant |
| FormantOscillator | `dsp/include/krate/dsp/processors/formant_oscillator.h` | 2 | OscType::Formant in variant |
| SpectralFreezeOscillator | `dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h` | 2 | OscType::SpectralFreeze in variant |
| PhaseDistortionOscillator | `dsp/include/krate/dsp/processors/phase_distortion_oscillator.h` | 2 | OscType::PhaseDistortion in variant |
| SyncOscillator | `dsp/include/krate/dsp/processors/sync_oscillator.h` | 2 | OscType::Sync in variant |
| AdditiveOscillator | `dsp/include/krate/dsp/processors/additive_oscillator.h` | 2 | OscType::Additive in variant |
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | 1 | Filter variant (LP/HP/BP/Notch) |
| LadderFilter | `dsp/include/krate/dsp/primitives/ladder_filter.h` | 1 | Filter variant (24dB/oct) |
| FormantFilter | `dsp/include/krate/dsp/processors/formant_filter.h` | 2 | Filter variant (vowel) |
| FeedbackComb | `dsp/include/krate/dsp/primitives/comb_filter.h` | 1 | Filter variant (metallic) |
| ChaosWaveshaper | `dsp/include/krate/dsp/primitives/chaos_waveshaper.h` | 1 | Distortion variant |
| SpectralDistortion | `dsp/include/krate/dsp/processors/spectral_distortion.h` | 2 | Distortion variant |
| GranularDistortion | `dsp/include/krate/dsp/processors/granular_distortion.h` | 2 | Distortion variant |
| Wavefolder | `dsp/include/krate/dsp/primitives/wavefolder.h` | 1 | Distortion variant |
| TapeSaturator | `dsp/include/krate/dsp/processors/tape_saturator.h` | 2 | Distortion variant |
| SpectralMorphFilter | `dsp/include/krate/dsp/processors/spectral_morph_filter.h` | 2 | SpectralMorph mixer mode |
| TranceGate | `dsp/include/krate/dsp/processors/trance_gate.h` | 2 | Per-voice rhythmic gate |
| ADSREnvelope | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | 1 | 3 envelopes per voice |
| LFO | `dsp/include/krate/dsp/primitives/lfo.h` | 1 | Per-voice modulation |
| DCBlocker | `dsp/include/krate/dsp/primitives/dc_blocker.h` | 1 | Post-distortion DC removal |
| semitonesToRatio | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | Filter cutoff modulation |
| frequencyToMidiNote | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | Key tracking calculation |
| isNaN/isInf/flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | NaN/Inf safety guards |
| DistortionRack (pattern reference) | `dsp/include/krate/dsp/systems/distortion_rack.h` | 3 | Reference for variant visitor pattern |
| SynthVoice (pattern reference) | `dsp/include/krate/dsp/systems/synth_voice.h` | 3 | Reference for voice lifecycle pattern |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems
- [x] `dsp/tests/unit/systems/` - Existing system tests

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (SelectableOscillator, RuinaeVoice, VoiceModRouter, OscType, MixMode, RuinaeFilterType, RuinaeDistortionType) are new and unique. No existing classes or enums with these names exist in the codebase. The enums use "Ruinae" prefix where needed to avoid collision with existing enums (e.g., SVFMode exists, so RuinaeFilterType is distinct).

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| ADSREnvelope | prepare | `void prepare(float sampleRate) noexcept` | Yes |
| ADSREnvelope | gate | `void gate(bool on) noexcept` | Yes |
| ADSREnvelope | process | `float process() noexcept` (returns 0-1) | Yes |
| ADSREnvelope | isActive | `bool isActive() const noexcept` | Yes |
| ADSREnvelope | reset | `void reset() noexcept` | Yes |
| LFO | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| LFO | process | `float process() noexcept` (returns LFO value) | Yes |
| LFO | reset | `void reset() noexcept` | Yes |
| TranceGate | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| TranceGate | processBlock (mono) | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| TranceGate | getGateValue | `float getGateValue() const noexcept` | Yes |
| TranceGate | setTempo | `void setTempo(double bpm) noexcept` | Yes |
| TranceGate | reset | `void reset() noexcept` | Yes |
| SpectralMorphFilter | prepare | `void prepare(double sampleRate, std::size_t fftSize = kDefaultFFTSize) noexcept` | Yes |
| SpectralMorphFilter | processBlock (dual) | `void processBlock(const float* inputA, const float* inputB, float* output, std::size_t numSamples) noexcept` | Yes |
| SpectralMorphFilter | setMorphAmount | `void setMorphAmount(float amount) noexcept` | Yes |
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | process | `float process(float input) noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = ...) noexcept` | Yes |
| DCBlocker | process | `float process(float input) noexcept` | Yes |
| semitonesToRatio | | `float semitonesToRatio(float semitones) noexcept` | Yes |
| frequencyToMidiNote | | `float frequencyToMidiNote(float hz) noexcept` | Yes |
| isNaN | | `constexpr bool isNaN(float x) noexcept` (in detail namespace) | Yes |
| flushDenormal | | `float flushDenormal(float x) noexcept` (in detail namespace) | Yes |
| PolyBlepOscillator | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| PolyBlepOscillator | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| PolyBlepOscillator | processBlock | `void processBlock(float* output, size_t numSamples) noexcept` | Yes |
| ChaosOscillator | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| ChaosOscillator | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| ChaosOscillator | processBlock | `void processBlock(float* output, size_t numSamples, const float* extInput = nullptr) noexcept` | Yes |
| ParticleOscillator | setFrequency | `void setFrequency(float centerHz) noexcept` | Yes |
| ParticleOscillator | processBlock | `void processBlock(float* output, size_t numSamples) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/adsr_envelope.h` - ADSREnvelope class
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class
- [x] `dsp/include/krate/dsp/processors/trance_gate.h` - TranceGate class
- [x] `dsp/include/krate/dsp/processors/spectral_morph_filter.h` - SpectralMorphFilter class
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` - PolyBlepOscillator
- [x] `dsp/include/krate/dsp/processors/chaos_oscillator.h` - ChaosOscillator
- [x] `dsp/include/krate/dsp/processors/particle_oscillator.h` - ParticleOscillator
- [x] `dsp/include/krate/dsp/primitives/noise_oscillator.h` - NoiseOscillator
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio, frequencyToMidiNote
- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN, isInf, flushDenormal
- [x] `dsp/include/krate/dsp/systems/distortion_rack.h` - variant pattern reference
- [x] `dsp/include/krate/dsp/systems/synth_voice.h` - voice lifecycle reference

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ADSREnvelope | prepare takes `float` not `double` | `ampEnv_.prepare(static_cast<float>(sampleRate))` |
| LFO | Non-copyable (wavetable vector) | Must use move semantics or store as direct member |
| SpectralMorphFilter | Non-copyable, movable; prepare allocates | Store as member, call prepare once |
| SpectralMorphFilter | processBlock takes `const float*` inputs | Separate osc buffers from output |
| ChaosOscillator | processBlock has optional `extInput` param | Pass `nullptr` for normal use |
| ChaosOscillator | Non-copyable (contains DCBlocker state) | Store as variant alternative, not copy |
| NoiseOscillator | No setFrequency method | Skip frequency setter for Noise type |
| TranceGate | reset() behavior depends on perVoice flag | Call reset() on noteOn for per-voice mode |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

**Decision**: No new Layer 0 utilities needed. All required utilities (semitonesToRatio, frequencyToMidiNote, isNaN, flushDenormal) already exist.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Modulation offset computation | Specific to VoiceModRouter, 1 consumer |
| Filter cutoff modulation formula | Voice-specific formula with multiple inputs |

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Filter and distortion stages have per-sample feedback |
| **Data parallelism width** | 2 oscillators, but narrow | Only 2 parallel streams within a voice |
| **Branch density in inner loop** | LOW | Variant dispatch is once per block, not per sample |
| **Dominant operations** | arithmetic + transcendental | Filter coefficient computation, oscillator processing |
| **Current CPU budget vs expected** | <1% budget vs ~0.5% expected (basic) | Good headroom for basic mode |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL -- DEFER

**Reasoning**: The voice processing has per-sample feedback loops in the filter and distortion stages, which prevent SIMD parallelization within a single voice. The 2 oscillators could theoretically be processed in parallel (2 SIMD lanes for OSC A + OSC B), but the 50% lane utilization makes this marginal. Cross-voice SIMD (processing 4 voices simultaneously) would require SoA refactoring which is premature. The basic voice is well within CPU budget. Defer SIMD to Phase 9 optimization if needed.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip inactive sections (bypass distortion/gate when disabled) | ~30-50% for basic patches | LOW | YES |
| Block processing for all components | ~10-20% vs per-sample | LOW | YES |
| Lazy oscillator initialization | ~60% memory reduction | MEDIUM | YES (per spec) |
| Early-out when voice inactive | ~100% for idle voices | LOW | YES |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 (Systems)

**Related features at same layer** (from roadmap):
- RuinaeEngine (Phase 6): Will compose 16 RuinaeVoice instances
- VoiceAllocator (existing): Will dispatch noteOn/noteOff to RuinaeVoice pool

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SelectableOscillator | HIGH | RuinaeEngine, future synth voices | Keep in own header for standalone reuse |
| VoiceModRouter | MEDIUM | Future voice types | Keep in own header |
| ruinae_types.h | HIGH | RuinaeEngine, plugin parameters | Keep in own header |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| SelectableOscillator in separate header | Standalone reuse by future synth voices and RuinaeEngine |
| VoiceModRouter in separate header | Could be reused by any polyphonic voice with per-voice modulation |
| RuinaeVoice in separate header | Main deliverable, composed by RuinaeEngine in Phase 6 |
| ruinae_types.h for all enums | Shared between voice, engine, and plugin parameter layers |

## Project Structure

### Documentation (this feature)

```text
specs/041-ruinae-voice-architecture/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── selectable_oscillator.h
│   ├── ruinae_voice.h
│   └── voice_mod_router.h
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code

```text
dsp/include/krate/dsp/systems/
├── ruinae_types.h              # NEW: Enumerations and type aliases
├── selectable_oscillator.h     # NEW: Variant-based oscillator wrapper
├── voice_mod_router.h          # NEW: Per-voice modulation routing
└── ruinae_voice.h              # NEW: Complete voice unit

dsp/tests/unit/systems/
├── selectable_oscillator_test.cpp  # NEW: SelectableOscillator tests
├── voice_mod_router_test.cpp       # NEW: VoiceModRouter tests
└── ruinae_voice_test.cpp           # NEW: RuinaeVoice integration tests
```

**Structure Decision**: All new files go in the existing `dsp/include/krate/dsp/systems/` directory (Layer 3). Tests go in `dsp/tests/unit/systems/`. No new directories needed. CMakeLists.txt in `dsp/tests/` must be updated to include new test files.

## Implementation Phases

### Phase 1: ruinae_types.h (Enumerations)

**Files**: `dsp/include/krate/dsp/systems/ruinae_types.h`

**Contents**:
- `OscType` enum (10 types + NumTypes)
- `MixMode` enum (CrossfadeMix, SpectralMorph)
- `PhaseMode` enum (Reset, Continuous)
- `RuinaeFilterType` enum (SVF_LP, SVF_HP, SVF_BP, SVF_Notch, Ladder, Formant, Comb)
- `RuinaeDistortionType` enum (Clean, ChaosWaveshaper, SpectralDistortion, GranularDistortion, Wavefolder, TapeSaturator)
- `VoiceModSource` enum (Env1, Env2, Env3, VoiceLFO, GateOutput, Velocity, KeyTrack)
- `VoiceModDest` enum (FilterCutoff, FilterResonance, MorphPosition, DistortionDrive, TranceGateDepth, OscAPitch, OscBPitch)
- `VoiceModRoute` struct (source, destination, amount)

**Dependencies**: None (pure enums and POD struct).

**Test**: Compile-only test verifying enum values and sizes.

### Phase 2: SelectableOscillator

**Files**:
- `dsp/include/krate/dsp/systems/selectable_oscillator.h`
- `dsp/tests/unit/systems/selectable_oscillator_test.cpp`

**Architecture**:
- `OscillatorVariant` = `std::variant<std::monostate, PolyBlepOscillator, WavetableOscillator, PhaseDistortionOscillator, SyncOscillator, AdditiveOscillator, ChaosOscillator, ParticleOscillator, FormantOscillator, SpectralFreezeOscillator, NoiseOscillator>`
- Visitor structs for prepare, reset, setFrequency, processBlock
- Lazy initialization: default type constructed and prepared on `prepare()`; other types constructed and prepared on `setType()` switch

**Key design details**:
1. `prepare()` -> emplaces default type (PolyBLEP), calls prepare visitor
2. `setType(newType)` -> if different type: emplace new type, call prepare visitor, set frequency
3. `setFrequency()` -> dispatches via visitor (no-op for NoiseOscillator)
4. `processBlock()` -> dispatches via visitor

**Tests** (write FIRST):
- Default construction produces PolyBLEP type
- All 10 types produce non-zero output after prepare (SC-005)
- Type switching preserves frequency setting
- Type switching same type is no-op
- processBlock before prepare produces silence
- setType with PhaseMode::Reset resets phase
- Zero heap allocations during type switch for non-FFT types (SC-004)
- NaN/Inf frequency is silently ignored

### Phase 3: VoiceModRouter

**Files**:
- `dsp/include/krate/dsp/systems/voice_mod_router.h`
- `dsp/tests/unit/systems/voice_mod_router_test.cpp`

**Architecture**:
- Fixed `std::array<VoiceModRoute, 16>` for routes
- `std::array<float, NumDestinations>` for computed offsets
- `computeOffsets()` iterates active routes, reads source value, multiplies by amount, accumulates to destination offset
- Source value mapping: Env1/2/3 in [0,1], LFO in [-1,+1], Gate in [0,1], Velocity in [0,1], KeyTrack = (midiNote - 60) / 60 in [-1,+1]

**Tests** (write FIRST):
- Empty router produces zero offsets
- Single route Env2 -> FilterCutoff with amount +48 -> offset = env2Value * 48
- Two routes to same destination are summed (FR-027)
- Amount clamped to [-1.0, +1.0]
- Velocity source is constant per note
- 16 routes all functional
- Clear route zeroes its contribution

### Phase 4: RuinaeVoice (Basic: OSC A + Filter + VCA)

**Files**:
- `dsp/include/krate/dsp/systems/ruinae_voice.h`
- `dsp/tests/unit/systems/ruinae_voice_test.cpp`

**First pass**: Implement core signal chain without OSC B, mixer, distortion, trance gate, or modulation. This validates the basic voice lifecycle.

**Signal flow**: OSC A -> Filter -> VCA (Amp Envelope) -> Output

**Tests** (write FIRST, User Story 1):
- noteOn produces non-zero output at correct pitch (AS-1.1)
- noteOff -> isActive false after envelope completes (AS-1.2)
- Retrigger: envelopes restart from current level (AS-1.3)
- processBlock before prepare produces silence (edge case)
- reset() -> isActive false
- SC-007: silence within 100ms of envelope idle

### Phase 5: RuinaeVoice (Dual OSC + Mixer)

**Extends Phase 4 with**: OSC B, CrossfadeMix, SpectralMorph modes.

**Tests** (write FIRST, User Stories 2, 3, 7):
- CrossfadeMix at 0.0 = OSC A only (AS-2.1)
- CrossfadeMix at 1.0 = OSC B only (AS-2.2)
- CrossfadeMix at 0.5 = blended (AS-2.3)
- Oscillator type switch during playback: no clicks (AS-2.4)
- All 10 oscillator types produce non-zero output (SC-005)
- SpectralMorph at 0.0 matches OSC A spectrally (AS-7.1)
- SpectralMorph at 1.0 matches OSC B spectrally (AS-7.2)
- SpectralMorph no allocation during processBlock (AS-7.4)

### Phase 6: RuinaeVoice (Filter + Distortion sections)

**Extends Phase 5 with**: Selectable filter, selectable distortion, DC blocker.

**Tests** (write FIRST, User Stories 4, 5):
- SVF lowpass attenuates above cutoff (AS-4.1)
- Ladder at max resonance self-oscillates (AS-4.2)
- Key tracking doubles cutoff for octave (AS-4.3)
- Filter type switch: no clicks (AS-4.4)
- Clean distortion passthrough (AS-5.1, bit-identical)
- ChaosWaveshaper adds harmonics (AS-5.2)
- Distortion type switch: no allocation, no clicks (AS-5.3)
- SC-006: filter cutoff modulation within 1 semitone accuracy

### Phase 7: RuinaeVoice (TranceGate + Modulation)

**Extends Phase 6 with**: TranceGate integration, VoiceModRouter integration.

**Tests** (write FIRST, User Stories 6, 8):
- TranceGate enabled: rhythmic amplitude variation (AS-8.1)
- TranceGate depth 0 = bypass (AS-8.2)
- TranceGate does not affect voice lifetime (AS-8.3, FR-018)
- getGateValue returns [0,1] (AS-8.4)
- Env2 modulating cutoff during attack (AS-6.1)
- LFO modulating morph position (AS-6.2)
- Velocity modulating cutoff (AS-6.3)
- Multiple routes summed in semitone space (AS-6.4)
- SC-008: modulation updates within one block

### Phase 8: Performance and Safety Verification

**Tests**:
- SC-001: Basic voice <1% CPU at 44.1kHz
- SC-002: SpectralMorph voice <3% CPU
- SC-003: 8 basic voices <8% CPU
- SC-004: Zero heap allocation during type switch (operator new override test)
- SC-009: Memory footprint per voice <64KB
- SC-010: No NaN/Inf in output after 10s of chaos oscillator processing
- FR-036: NaN/Inf safety for all output stages

### Phase 9: Code Quality

- Fix all compiler warnings (MSVC, Clang, GCC)
- Run clang-tidy
- Update `specs/_architecture_/` documentation

## Test Strategy

### Test Hierarchy

1. **Unit tests**: SelectableOscillator (isolated), VoiceModRouter (isolated)
2. **Integration tests**: RuinaeVoice (all sections composed)
3. **Performance tests**: CPU benchmarks, allocation checks, NaN safety

### Test Patterns

Following existing patterns from `dsp/tests/unit/systems/synth_voice_test.cpp`:

```cpp
TEST_CASE("RuinaeVoice: basic playback", "[ruinae_voice]") {
    Krate::DSP::RuinaeVoice voice;
    voice.prepare(44100.0, 512);
    voice.noteOn(440.0f, 0.8f);

    std::array<float, 512> buffer{};
    voice.processBlock(buffer.data(), 512);

    float rms = computeRMS(buffer.data(), 512);
    REQUIRE(rms > 0.001f);  // Non-silent output
}
```

### Allocation Detection Pattern

```cpp
// Override global operator new for allocation tracking
static int allocationCount = 0;
void* operator new(std::size_t size) {
    ++allocationCount;
    return std::malloc(size);
}

TEST_CASE("SelectableOscillator: type switch is allocation-free for PolyBLEP") {
    SelectableOscillator osc;
    osc.prepare(44100.0, 512);

    allocationCount = 0;
    osc.setType(OscType::Chaos);  // ChaosOscillator has no vectors
    float buf[512];
    osc.processBlock(buf, 512);
    REQUIRE(allocationCount == 0);
}
```

## Integration Points

### With Existing Code
- **All oscillator types**: Used via SelectableOscillator's variant. No modifications to existing oscillator code.
- **All filter types**: Used via FilterVariant. No modifications.
- **All distortion types**: Used via DistortionVariant. No modifications.
- **TranceGate**: Used directly as member. No modifications.
- **ADSREnvelope**: Used directly (3 instances). No modifications.
- **LFO**: Used directly as member. No modifications.
- **SpectralMorphFilter**: Used for SpectralMorph mixer mode. No modifications.

### With Future Code (Phase 6: RuinaeEngine)
- RuinaeVoice API matches the SynthVoice pattern (noteOn/noteOff/isActive/processBlock)
- RuinaeEngine will compose `std::array<RuinaeVoice, 16>` identically to PolySynthEngine
- All parameter setters are designed for engine-level forwarding

### CMake Integration
- Add test files to `dsp/tests/CMakeLists.txt` (same pattern as existing system tests)
- No new library targets needed (header-only Layer 3 components)

## Complexity Tracking

No constitution violations. No complexity tracking entries needed.
