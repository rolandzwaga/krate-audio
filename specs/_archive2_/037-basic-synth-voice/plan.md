# Implementation Plan: Basic Synth Voice

**Branch**: `037-basic-synth-voice` | **Date**: 2026-02-07 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/037-basic-synth-voice/spec.md`

## Summary

Implement a complete single-voice subtractive synthesis unit (SynthVoice) as a Layer 3 system component. The voice composes 2 PolyBlepOscillators with mix/detune/octave, 1 SVF filter with per-sample envelope modulation and key tracking, 2 ADSREnvelopes (amplitude and filter), and velocity mapping. Signal flow: oscillators -> crossfade mix -> filter -> amplitude envelope -> output. Additionally, a `frequencyToMidiNote()` utility function will be added to Layer 0 `pitch_utils.h` to support key tracking.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: PolyBlepOscillator (Layer 1), ADSREnvelope (Layer 1), SVF (Layer 1), semitonesToRatio (Layer 0), detail::isNaN/isInf (Layer 0)
**Storage**: N/A (no persistent state)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component (header-only, monorepo)
**Performance Goals**: SC-001: < 1% single CPU core at 44.1 kHz with all components active
**Constraints**: Real-time safe processing (no allocation, no locks, no exceptions in process()), zero compile warnings
**Scale/Scope**: Single header file (~400 lines), single test file (~1500 lines), one Layer 0 utility addition

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation)**: N/A -- this is a DSP-only component with no VST3 interaction.

**Principle II (Real-Time Audio Thread Safety)**:
- [x] No allocations in process() or processBlock()
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions thrown in audio path
- [x] Pre-allocation happens in prepare() only

**Principle III (Modern C++ Standards)**:
- [x] C++20 target
- [x] Value semantics (no raw new/delete)
- [x] constexpr where applicable
- [x] [[nodiscard]] on getters and process()

**Principle IV (SIMD & DSP Optimization)**:
- [x] SIMD viability analysis completed (see section below)
- [x] Scalar-first workflow followed

**Principle IX (Layered DSP Architecture)**:
- [x] Layer 3 system depends only on Layer 0 and Layer 1 -- no Layer 2+ dependencies
- [x] No circular dependencies

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

**Classes/Structs to be created**: SynthVoice

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SynthVoice | `grep -r "class SynthVoice" dsp/ plugins/` | No | Create New |
| SubtractiveVoice | `grep -r "class SubtractiveVoice" dsp/ plugins/` | No | N/A (alternate name check) |

**Utility Functions to be created**: frequencyToMidiNote

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| frequencyToMidiNote | `grep -r "frequencyToMidiNote" dsp/ plugins/` | No | pitch_utils.h | Create New |
| frequencyToNoteClass | `grep -r "frequencyToNoteClass" dsp/ plugins/` | Yes | pitch_utils.h | Reference (different function, returns note class 0-11) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| PolyBlepOscillator | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | 1 | 2 instances for osc1 and osc2 |
| OscWaveform | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | 1 | Waveform selection enum, exposed in SynthVoice API |
| ADSREnvelope | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | 1 | 2 instances for amp and filter envelopes |
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | 1 | 1 instance for the voice filter |
| SVFMode | `dsp/include/krate/dsp/primitives/svf.h` | 1 | Filter mode selection, exposed in SynthVoice API |
| EnvCurve | `dsp/include/krate/dsp/primitives/envelope_utils.h` | 1 | Envelope curve shapes, exposed in SynthVoice API |
| RetriggerMode | `dsp/include/krate/dsp/primitives/envelope_utils.h` | 1 | Used internally (Hard mode for attack-from-current-level) |
| semitonesToRatio | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | Detune and key-tracking frequency computation |
| detail::isNaN | `dsp/include/krate/dsp/core/db_utils.h` | 0 | NaN/Inf input guards (FR-032) |
| detail::isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | NaN/Inf input guards (FR-032) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (checked for name collision)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The only new class `SynthVoice` does not exist in the codebase. The new utility function `frequencyToMidiNote` does not exist. The existing `frequencyToNoteClass` function has a different name and purpose. No ODR violations possible.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PolyBlepOscillator | prepare | `inline void prepare(double sampleRate) noexcept` | Yes |
| PolyBlepOscillator | reset | `inline void reset() noexcept` | Yes |
| PolyBlepOscillator | setFrequency | `inline void setFrequency(float hz) noexcept` | Yes |
| PolyBlepOscillator | setWaveform | `inline void setWaveform(OscWaveform waveform) noexcept` | Yes |
| PolyBlepOscillator | process | `[[nodiscard]] inline float process() noexcept` | Yes |
| ADSREnvelope | prepare | `void prepare(float sampleRate) noexcept` | Yes |
| ADSREnvelope | reset | `void reset() noexcept` | Yes |
| ADSREnvelope | gate | `void gate(bool on) noexcept` | Yes |
| ADSREnvelope | process | `[[nodiscard]] float process() noexcept` | Yes |
| ADSREnvelope | isActive | `[[nodiscard]] bool isActive() const noexcept` | Yes |
| ADSREnvelope | getOutput | `[[nodiscard]] float getOutput() const noexcept` | Yes |
| ADSREnvelope | setAttack | `ITERUM_NOINLINE void setAttack(float ms) noexcept` | Yes |
| ADSREnvelope | setDecay | `ITERUM_NOINLINE void setDecay(float ms) noexcept` | Yes |
| ADSREnvelope | setSustain | `ITERUM_NOINLINE void setSustain(float level) noexcept` | Yes |
| ADSREnvelope | setRelease | `ITERUM_NOINLINE void setRelease(float ms) noexcept` | Yes |
| ADSREnvelope | setAttackCurve | `void setAttackCurve(EnvCurve curve) noexcept` | Yes |
| ADSREnvelope | setDecayCurve | `void setDecayCurve(EnvCurve curve) noexcept` | Yes |
| ADSREnvelope | setReleaseCurve | `void setReleaseCurve(EnvCurve curve) noexcept` | Yes |
| ADSREnvelope | setVelocityScaling | `void setVelocityScaling(bool enabled) noexcept` | Yes |
| ADSREnvelope | setVelocity | `ITERUM_NOINLINE void setVelocity(float velocity) noexcept` | Yes |
| ADSREnvelope | setRetriggerMode | `void setRetriggerMode(RetriggerMode mode) noexcept` | Yes |
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| semitonesToRatio | - | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` - PolyBlepOscillator class
- [x] `dsp/include/krate/dsp/primitives/adsr_envelope.h` - ADSREnvelope class
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/primitives/envelope_utils.h` - EnvCurve, RetriggerMode
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio, frequencyToNoteClass
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf
- [x] `dsp/include/krate/dsp/core/midi_utils.h` - midiNoteToFrequency (reference)
- [x] `dsp/include/krate/dsp/systems/fm_voice.h` - FMVoice (Layer 3 pattern reference)
- [x] `dsp/include/krate/dsp/systems/voice_allocator.h` - VoiceAllocator (upstream consumer)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ADSREnvelope | `prepare()` takes `float sampleRate` not `double` | `ampEnv_.prepare(static_cast<float>(sampleRate))` |
| ADSREnvelope | `gate(true)` in Hard mode attacks from current `output_` (does NOT reset to 0) | Just call `gate(true)` for retrigger -- current level is preserved |
| ADSREnvelope | Default retrigger mode is `RetriggerMode::Hard` (matches spec need) | No need to call `setRetriggerMode()` |
| ADSREnvelope | Velocity scaling must be explicitly enabled | Call `setVelocityScaling(true)` in `prepare()` for amp envelope |
| SVF | `prepare()` takes `double sampleRate` (different from ADSREnvelope's float) | `filter_.prepare(sampleRate)` |
| SVF | `process(float input)` takes the input signal as parameter | `filter_.process(mixedSample)` not `filter_.process()` |
| SVF | `setCutoff()` internally clamps to [1 Hz, sr*0.495], but we should pre-clamp to [20 Hz, sr*0.495] per spec | Clamp before calling `setCutoff()` |
| PolyBlepOscillator | `prepare()` resets frequency to 440 Hz and waveform to Sine | Set waveform and frequency AFTER prepare(), or in noteOn() |
| PolyBlepOscillator | Default waveform is Sine, but spec default is Sawtooth | Set to Sawtooth in prepare() |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `frequencyToMidiNote(float hz)` | Continuous MIDI note from frequency; needed for key tracking by any voice type | `pitch_utils.h` | SynthVoice, future WavetableVoice, FMVoice (if key tracking added) |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Filter cutoff computation | Voice-specific formula combining multiple parameters; not reusable |
| Effective envelope amount with velocity scaling | Voice-specific formula |

**Decision**: Extract `frequencyToMidiNote()` to `pitch_utils.h`. Keep filter cutoff computation and velocity-scaled envelope amount as inline code within SynthVoice::process().

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | SVF filter has internal state feedback (2 integrators). Cannot parallelize across samples. |
| **Data parallelism width** | 2 | Only 2 oscillators -- 50% lane waste on 4-wide SSE. |
| **Branch density in inner loop** | LOW | No per-sample branching in the main process path. |
| **Dominant operations** | Transcendental (sin, tan, pow) + arithmetic | Oscillator sine/polyblep, SVF tan(), cutoff pow(2, x/12) |
| **Current CPU budget vs expected usage** | 1% budget vs ~0.2-0.4% expected | Well under budget based on component profiles |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The SVF filter has per-sample feedback dependencies that prevent sample-level parallelization. The oscillators are only 2 wide (50% SSE lane waste, 75% AVX waste). The expected CPU usage (~0.3%) is well under the 1% budget, so optimization is unnecessary. The dominant transcendental operations (sin, tan, pow) are already single-instruction on modern CPUs.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Fast exp2 approximation for cutoff modulation | ~10-15% of modulation path | LOW | DEFER (not needed to meet SC-001) |
| Skip filter modulation when envAmount == 0 | ~30% for static filter case | LOW | YES |
| Skip osc2 when mix == 0.0 | ~40% for single-osc case | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 3 - Systems

**Related features at same layer** (from spec and roadmap):
- Polyphonic Synth Engine (roadmap 3.2): Will manage a pool of SynthVoice instances
- FMVoice: Existing Layer 3 voice with similar lifecycle (prepare/reset/process)
- Future WavetableVoice: Would follow the same voice pattern

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `frequencyToMidiNote()` | HIGH | FMVoice key tracking, WavetableVoice, PolySynth | Extract to pitch_utils.h now |
| Voice lifecycle pattern (noteOn/noteOff/isActive) | MEDIUM | PolySynth voice concept/interface | Keep local, extract interface when PolySynth is implemented |
| Filter cutoff modulation formula | LOW | Each voice type has different modulation sources | Keep as member code |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract `frequencyToMidiNote()` to Layer 0 | 3+ expected consumers (SynthVoice, FMVoice, WavetableVoice), pure stateless function |
| No shared voice base class/interface | Only 2 voice types exist (FMVoice, SynthVoice). Wait for PolySynth to define the interface. |
| Use SVF directly (not MultimodeFilter) | Spec explicitly requires SVF for per-voice use. MultimodeFilter adds unnecessary overhead (smoothers, oversampler allocation). |

### Review Trigger

After implementing **Polyphonic Synth Engine (roadmap 3.2)**, review this section:
- [ ] Does PolySynth need a shared voice interface? -> Extract IVoice concept
- [ ] Does PolySynth use `frequencyToMidiNote()`? -> Already extracted
- [ ] Any duplicated voice lifecycle code? -> Consider shared base

## Project Structure

### Documentation (this feature)

```text
specs/037-basic-synth-voice/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- synth_voice_api.h
+-- spec.md              # Feature specification
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- core/
|   |   +-- pitch_utils.h           # MODIFY: add frequencyToMidiNote()
|   +-- systems/
|       +-- synth_voice.h           # CREATE: SynthVoice class (header-only)
+-- tests/
|   +-- unit/systems/
|       +-- synth_voice_test.cpp    # CREATE: full test suite
+-- CMakeLists.txt                  # MODIFY: add synth_voice.h to headers list
+-- lint_all_headers.cpp            # MODIFY: add include

dsp/tests/
+-- CMakeLists.txt                  # MODIFY: add synth_voice_test.cpp
```

**Structure Decision**: Standard DSP library header-only component in the Layer 3 systems directory, following the established FMVoice pattern.

## Implementation Design

### File 1: `dsp/include/krate/dsp/core/pitch_utils.h` (MODIFY)

Add after `frequencyToCentsDeviation()`:

```cpp
/// Convert frequency in Hz to continuous MIDI note number.
/// Uses 12-TET: midiNote = 12 * log2(hz / 440) + 69
/// @param hz Frequency in Hz (must be > 0)
/// @return Continuous MIDI note (69.0 = A4, 60.0 = C4). Returns 0.0 if hz <= 0.
[[nodiscard]] inline float frequencyToMidiNote(float hz) noexcept {
    if (hz <= 0.0f) return 0.0f;
    return 12.0f * std::log2(hz / 440.0f) + 69.0f;
}
```

### File 2: `dsp/include/krate/dsp/systems/synth_voice.h` (CREATE)

#### Class Layout

```cpp
class SynthVoice {
public:
    // --- Lifecycle ---
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // --- Note Control ---
    void noteOn(float frequency, float velocity) noexcept;
    void noteOff() noexcept;
    [[nodiscard]] bool isActive() const noexcept;

    // --- Oscillator Parameters ---
    void setOsc1Waveform(OscWaveform waveform) noexcept;
    void setOsc2Waveform(OscWaveform waveform) noexcept;
    void setOscMix(float mix) noexcept;
    void setOsc2Detune(float cents) noexcept;
    void setOsc2Octave(int octave) noexcept;

    // --- Filter Parameters ---
    void setFilterType(SVFMode type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float semitones) noexcept;
    void setFilterKeyTrack(float amount) noexcept;

    // --- Envelope Parameters ---
    void setAmpAttack(float ms) noexcept;
    void setAmpDecay(float ms) noexcept;
    void setAmpSustain(float level) noexcept;
    void setAmpRelease(float ms) noexcept;
    void setFilterAttack(float ms) noexcept;
    void setFilterDecay(float ms) noexcept;
    void setFilterSustain(float level) noexcept;
    void setFilterRelease(float ms) noexcept;

    // --- Curve Shapes ---
    void setAmpAttackCurve(EnvCurve curve) noexcept;
    void setAmpDecayCurve(EnvCurve curve) noexcept;
    void setAmpReleaseCurve(EnvCurve curve) noexcept;
    void setFilterAttackCurve(EnvCurve curve) noexcept;
    void setFilterDecayCurve(EnvCurve curve) noexcept;
    void setFilterReleaseCurve(EnvCurve curve) noexcept;

    // --- Velocity ---
    void setVelocityToFilterEnv(float amount) noexcept;

    // --- Processing ---
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

private:
    // Sub-components
    PolyBlepOscillator osc1_;
    PolyBlepOscillator osc2_;
    SVF filter_;
    ADSREnvelope ampEnv_;
    ADSREnvelope filterEnv_;

    // Parameters
    float oscMix_ = 0.5f;
    float osc2DetuneCents_ = 0.0f;
    int osc2Octave_ = 0;
    float filterCutoffHz_ = 1000.0f;
    float filterEnvAmount_ = 0.0f;
    float filterKeyTrack_ = 0.0f;
    float velToFilterEnv_ = 0.0f;

    // Voice state
    float noteFrequency_ = 0.0f;
    float velocity_ = 0.0f;
    double sampleRate_ = 0.0;
    bool prepared_ = false;
};
```

#### prepare() Implementation

```cpp
void prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    osc1_.prepare(sampleRate);
    osc2_.prepare(sampleRate);
    osc1_.setWaveform(OscWaveform::Sawtooth);  // Default per FR-009
    osc2_.setWaveform(OscWaveform::Sawtooth);  // Default per FR-009

    filter_.prepare(sampleRate);
    filter_.setMode(SVFMode::Lowpass);          // Default per FR-014
    filter_.setCutoff(1000.0f);                 // Default per FR-015
    filter_.setResonance(SVF::kButterworthQ);   // Default per FR-016

    ampEnv_.prepare(static_cast<float>(sampleRate));
    ampEnv_.setAttack(10.0f);                   // Default per FR-023
    ampEnv_.setDecay(50.0f);
    ampEnv_.setSustain(1.0f);
    ampEnv_.setRelease(100.0f);
    ampEnv_.setVelocityScaling(true);           // FR-026

    filterEnv_.prepare(static_cast<float>(sampleRate));
    filterEnv_.setAttack(10.0f);                // Default per FR-023
    filterEnv_.setDecay(200.0f);
    filterEnv_.setSustain(0.0f);
    filterEnv_.setRelease(100.0f);

    // Reset state
    noteFrequency_ = 0.0f;
    velocity_ = 0.0f;
    prepared_ = true;
}
```

#### noteOn() Implementation

```cpp
void noteOn(float frequency, float velocity) noexcept {
    // FR-032: Silently ignore NaN/Inf
    if (detail::isNaN(frequency) || detail::isInf(frequency)) return;
    if (detail::isNaN(velocity) || detail::isInf(velocity)) return;

    // Clamp inputs
    noteFrequency_ = (frequency < 0.0f) ? 0.0f : frequency;
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);

    // Update oscillator frequencies (FR-007: preserve phase, update frequency)
    osc1_.setFrequency(noteFrequency_);
    float osc2Freq = noteFrequency_
                     * semitonesToRatio(static_cast<float>(osc2Octave_ * 12))
                     * semitonesToRatio(osc2DetuneCents_ / 100.0f);
    osc2_.setFrequency(osc2Freq);

    // Update velocity on amp envelope (FR-026)
    ampEnv_.setVelocity(velocity_);

    // Gate both envelopes (FR-004)
    // Hard mode: enterAttack() preserves current output_ (attacks from current level)
    ampEnv_.gate(true);
    filterEnv_.gate(true);
}
```

#### process() Implementation (per-sample, FR-028)

```cpp
[[nodiscard]] float process() noexcept {
    // FR-003: Return 0.0 if not prepared
    if (!prepared_) return 0.0f;

    // FR-006: Return 0.0 if not active
    if (!ampEnv_.isActive()) return 0.0f;

    // Step 1: Generate oscillator samples (FR-008)
    const float osc1Sample = osc1_.process();
    const float osc2Sample = osc2_.process();

    // Step 2: Mix oscillators (FR-010)
    const float mixed = (1.0f - oscMix_) * osc1Sample + oscMix_ * osc2Sample;

    // Step 3: Process filter envelope
    const float filterEnvLevel = filterEnv_.process();

    // Step 4: Compute effective cutoff (FR-018)
    const float effectiveEnvAmount = filterEnvAmount_
        * (1.0f - velToFilterEnv_ + velToFilterEnv_ * velocity_);
    const float keyTrackSemitones = (noteFrequency_ > 0.0f)
        ? filterKeyTrack_ * (frequencyToMidiNote(noteFrequency_) - 60.0f)
        : 0.0f;
    const float totalSemitones = effectiveEnvAmount * filterEnvLevel + keyTrackSemitones;
    float effectiveCutoff = filterCutoffHz_ * semitonesToRatio(totalSemitones);

    // Clamp to safe range (FR-018)
    const float maxCutoff = static_cast<float>(sampleRate_) * 0.495f;
    effectiveCutoff = std::clamp(effectiveCutoff, 20.0f, maxCutoff);

    // Step 5: Update and process filter (FR-019: per-sample update)
    filter_.setCutoff(effectiveCutoff);
    const float filtered = filter_.process(mixed);

    // Step 6: Apply amplitude envelope (FR-025)
    const float ampLevel = ampEnv_.process();
    return filtered * ampLevel;
}
```

#### processBlock() Implementation

```cpp
void processBlock(float* output, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = process();
    }
}
```

### File 3: `dsp/tests/unit/systems/synth_voice_test.cpp` (CREATE)

#### Test Organization by Functional Requirement

```
[synth-voice][lifecycle]     -- FR-001, FR-002, FR-003
[synth-voice][note-control]  -- FR-004, FR-005, FR-006, FR-007
[synth-voice][oscillator]    -- FR-008, FR-009, FR-010, FR-011, FR-012
[synth-voice][filter]        -- FR-013, FR-014, FR-015, FR-016
[synth-voice][filter-env]    -- FR-017, FR-018, FR-019
[synth-voice][key-tracking]  -- FR-020, FR-021
[synth-voice][envelope]      -- FR-022, FR-023, FR-024, FR-025
[synth-voice][velocity]      -- FR-026, FR-027
[synth-voice][signal-flow]   -- FR-028, FR-029, FR-030
[synth-voice][safety]        -- FR-031, FR-032
[synth-voice][performance]   -- SC-001
[synth-voice][acceptance]    -- SC-002 through SC-010
```

#### Key Test Cases

**FR-001 (prepare)**: Verify all sub-components are initialized after prepare().
**FR-002 (reset)**: Verify isActive()==false and process()==0.0 after reset().
**FR-003 (pre-prepare)**: Verify process()==0.0 and processBlock fills zeros before prepare().
**FR-004 (noteOn)**: Verify non-zero output within first 512 samples after noteOn.
**FR-005 (noteOff)**: Verify voice transitions to release and eventually becomes inactive.
**FR-006 (isActive)**: Verify false before noteOn, true after noteOn, false after release completes.
**FR-007 (retrigger)**: Verify no clicks on retrigger (amplitude continuity check).
**FR-008/009 (waveforms)**: Verify each waveform produces non-zero, distinct output.
**FR-010 (mix)**: Verify mix=0 silences osc2, mix=1 silences osc1, mix=0.5 blends both.
**FR-011 (detune)**: Verify +10 cents produces beating at expected rate.
**FR-012 (octave)**: Verify osc2 at 440Hz with +1 octave produces 880Hz.
**FR-013/014 (filter type)**: Verify LP/HP/BP/Notch modes produce distinct frequency responses.
**FR-015 (cutoff)**: Verify cutoff affects output frequency content.
**FR-016 (resonance)**: Verify high Q produces resonant peak.
**FR-017 (env amount)**: Verify envelope modulates cutoff.
**FR-018 (cutoff formula)**: Verify 500Hz * 2^(48/12) = 8000Hz at envelope peak.
**FR-019 (per-sample update)**: Verify smooth filter sweeps (no stepping artifacts).
**FR-020/021 (key tracking)**: Verify cutoff doubles for note one octave above C4 at 100% tracking.
**FR-022/023/024 (envelope params)**: Verify ADSR parameters work, curve shapes applied.
**FR-025 (amp env)**: Verify voice becomes inactive when amp env reaches idle.
**FR-026 (velocity->amplitude)**: Verify velocity 0.5 produces ~50% amplitude of velocity 1.0.
**FR-027 (velocity->filter)**: Verify velocity 0.25 with velToFilterEnv=1.0 gives 25% filter depth.
**FR-028 (signal flow)**: Verify correct signal chain order (osc->mix->filter->amp).
**FR-029 (RT safety)**: Verify no allocations (structural test -- inspect code, no runtime allocation checks needed beyond compile-time constraints).
**FR-030 (processBlock)**: Verify bit-identical to process() loop.
**FR-031 (setter safety)**: Verify setters work before prepare, while playing, while idle.
**FR-032 (NaN/Inf)**: Verify all setters ignore NaN and Inf inputs.
**SC-001 (CPU)**: Benchmark 1 second of audio, verify < 1% CPU.
**SC-002 (immediate output)**: Verify non-zero audio in first 512-sample block.
**SC-003 (silence after release)**: Verify exactly 0.0 output after release completes.
**SC-004 (bit-identical)**: Compare processBlock vs process() loop output.
**SC-005 (sample rates)**: Test at 44100, 48000, 88200, 96000, 176400, 192000 Hz.
**SC-006 (cutoff clamping)**: Extreme parameters: max env amount + max key tracking + highest note.
**SC-007 (mix extremes)**: Verify exact 0.0 contribution at mix boundaries.
**SC-008 (output range)**: Verify output in [-1, 1] under normal conditions.
**SC-009 (retrigger click-free)**: Measure discontinuity amplitude on retrigger.
**SC-010 (test coverage)**: All 32 FRs have tests (verified by test count/tags).

### CMake Integration

#### dsp/CMakeLists.txt -- Add to KRATE_DSP_SYSTEMS_HEADERS

```cmake
include/krate/dsp/systems/voice_allocator.h
include/krate/dsp/systems/synth_voice.h     # ADD THIS LINE
```

#### dsp/tests/CMakeLists.txt -- Add to dsp_tests sources

```cmake
# Layer 3: Systems
unit/systems/voice_allocator_test.cpp
unit/systems/synth_voice_test.cpp           # ADD THIS LINE
```

Also add to the `-fno-fast-math` file list for Clang/GCC:
```cmake
unit/systems/synth_voice_test.cpp
```

#### dsp/lint_all_headers.cpp -- Add include

```cpp
#include <krate/dsp/systems/synth_voice.h>
```

## Complexity Tracking

No constitution violations. All design decisions align with principles.

## Post-Design Constitution Re-Check

**Principle II (Real-Time Safety)**: PASS -- process() uses only inline arithmetic, no allocations, no locks, no exceptions.
**Principle III (Modern C++)**: PASS -- value semantics, [[nodiscard]], noexcept throughout.
**Principle IV (SIMD)**: PASS -- SIMD analysis complete, verdict NOT BENEFICIAL documented.
**Principle IX (Layer Architecture)**: PASS -- Layer 3 depends only on Layer 0 (pitch_utils, db_utils, math_constants) and Layer 1 (polyblep_oscillator, adsr_envelope, svf, envelope_utils).
**Principle XII (Test-First)**: PASS -- test plan covers all 32 FRs and 10 SCs.
**Principle XIV (ODR Prevention)**: PASS -- SynthVoice is unique, frequencyToMidiNote is unique.
**Principle XVI (Honest Completion)**: N/A -- applies at implementation completion.
