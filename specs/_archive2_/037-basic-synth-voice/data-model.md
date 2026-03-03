# Data Model: Basic Synth Voice (037)

## Entity: SynthVoice

**Layer**: 3 (Systems)
**Location**: `dsp/include/krate/dsp/systems/synth_voice.h`
**Namespace**: `Krate::DSP`
**Dependencies**: Layer 0 (pitch_utils, db_utils, math_constants), Layer 1 (polyblep_oscillator, adsr_envelope, svf, envelope_utils)

### Class Definition

```cpp
class SynthVoice {
public:
    // Lifecycle
    void prepare(double sampleRate) noexcept;     // FR-001
    void reset() noexcept;                         // FR-002

    // Note control
    void noteOn(float frequency, float velocity) noexcept;   // FR-004
    void noteOff() noexcept;                                   // FR-005
    [[nodiscard]] bool isActive() const noexcept;              // FR-006

    // Oscillator parameters
    void setOsc1Waveform(OscWaveform waveform) noexcept;      // FR-009
    void setOsc2Waveform(OscWaveform waveform) noexcept;      // FR-009
    void setOscMix(float mix) noexcept;                        // FR-010
    void setOsc2Detune(float cents) noexcept;                  // FR-011
    void setOsc2Octave(int octave) noexcept;                   // FR-012

    // Filter parameters
    void setFilterType(SVFMode type) noexcept;                 // FR-014
    void setFilterCutoff(float hz) noexcept;                   // FR-015
    void setFilterResonance(float q) noexcept;                 // FR-016
    void setFilterEnvAmount(float semitones) noexcept;         // FR-017
    void setFilterKeyTrack(float amount) noexcept;             // FR-020

    // Amplitude envelope parameters
    void setAmpAttack(float ms) noexcept;                      // FR-023
    void setAmpDecay(float ms) noexcept;                       // FR-023
    void setAmpSustain(float level) noexcept;                  // FR-023
    void setAmpRelease(float ms) noexcept;                     // FR-023

    // Filter envelope parameters
    void setFilterAttack(float ms) noexcept;                   // FR-023
    void setFilterDecay(float ms) noexcept;                    // FR-023
    void setFilterSustain(float level) noexcept;               // FR-023
    void setFilterRelease(float ms) noexcept;                  // FR-023

    // Envelope curve shapes
    void setAmpAttackCurve(EnvCurve curve) noexcept;           // FR-024
    void setAmpDecayCurve(EnvCurve curve) noexcept;            // FR-024
    void setAmpReleaseCurve(EnvCurve curve) noexcept;          // FR-024
    void setFilterAttackCurve(EnvCurve curve) noexcept;        // FR-024
    void setFilterDecayCurve(EnvCurve curve) noexcept;         // FR-024
    void setFilterReleaseCurve(EnvCurve curve) noexcept;       // FR-024

    // Velocity mapping
    void setVelocityToFilterEnv(float amount) noexcept;        // FR-027

    // Processing
    [[nodiscard]] float process() noexcept;                    // FR-028, FR-030
    void processBlock(float* output, size_t numSamples) noexcept;  // FR-030

private:
    // Sub-components
    PolyBlepOscillator osc1_;       // Oscillator 1
    PolyBlepOscillator osc2_;       // Oscillator 2
    SVF filter_;                     // Multimode filter
    ADSREnvelope ampEnv_;           // Amplitude envelope
    ADSREnvelope filterEnv_;        // Filter envelope

    // Oscillator parameters
    float oscMix_ = 0.5f;           // 0.0 = osc1 only, 1.0 = osc2 only
    float osc2DetuneCents_ = 0.0f;  // -100 to +100 cents
    int osc2Octave_ = 0;            // -2 to +2 octaves

    // Filter parameters
    float filterCutoffHz_ = 1000.0f;       // Base cutoff frequency
    float filterEnvAmount_ = 0.0f;         // Semitones, -96 to +96
    float filterKeyTrack_ = 0.0f;          // 0.0 to 1.0

    // Velocity state
    float velocity_ = 0.0f;                // Current note velocity (0-1)
    float velToFilterEnv_ = 0.0f;          // Velocity-to-filter-env amount (0-1)

    // Voice state
    float noteFrequency_ = 0.0f;           // Current note frequency in Hz
    double sampleRate_ = 0.0;              // Current sample rate
    bool prepared_ = false;                 // Has prepare() been called?
};
```

### Member Variable Details

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `osc1_` | PolyBlepOscillator | - | - | First oscillator |
| `osc2_` | PolyBlepOscillator | - | - | Second oscillator with detune/octave |
| `filter_` | SVF | - | - | TPT state variable filter |
| `ampEnv_` | ADSREnvelope | A=10ms, D=50ms, S=1.0, R=100ms | - | Amplitude envelope |
| `filterEnv_` | ADSREnvelope | A=10ms, D=200ms, S=0.0, R=100ms | - | Filter modulation envelope |
| `oscMix_` | float | 0.5 | [0.0, 1.0] | Oscillator crossfade |
| `osc2DetuneCents_` | float | 0.0 | [-100.0, +100.0] | Osc2 detune in cents |
| `osc2Octave_` | int | 0 | [-2, +2] | Osc2 octave offset |
| `filterCutoffHz_` | float | 1000.0 | [20.0, 20000.0] | Base filter cutoff |
| `filterEnvAmount_` | float | 0.0 | [-96.0, +96.0] | Filter env mod depth (semitones) |
| `filterKeyTrack_` | float | 0.0 | [0.0, 1.0] | Key tracking amount |
| `velocity_` | float | 0.0 | [0.0, 1.0] | Current note velocity |
| `velToFilterEnv_` | float | 0.0 | [0.0, 1.0] | Velocity-to-filter-env scaling |
| `noteFrequency_` | float | 0.0 | [0, Nyquist] | Current note frequency |
| `sampleRate_` | double | 0.0 | [44100, 192000] | Sample rate |
| `prepared_` | bool | false | - | Preparation state |

### State Transitions

```
                  noteOn(freq, vel)
    IDLE --------------------------> ACTIVE
     ^                                  |
     |                                  | noteOff()
     |                                  v
     |                              RELEASING
     |                                  |
     +----------------------------------+
        ampEnv reaches Idle
```

- IDLE: `isActive() == false`, `process()` returns 0.0
- ACTIVE: Both envelopes running (gate on), producing audio
- RELEASING: Both envelopes in release (gate off), producing fade-out audio
- Transition from RELEASING back to ACTIVE: On retrigger (noteOn while active/releasing)
- Transition from ACTIVE to ACTIVE: On retrigger (noteOn while active), preserves phase

### Signal Flow (FR-028)

```
noteFrequency --+--> [Osc1] --+
                |              |  oscMix crossfade
                +--> [Osc2] --+----> [Filter] ----> [x AmpEnv] ----> OUTPUT
                      ^                  ^
                      |                  |
               detune + octave     effectiveCutoff
                                         |
                                   baseCutoff * 2^((envAmount * envLevel + keyTrackSemitones) / 12)
```

### Filter Cutoff Formula (FR-018)

```
keyTrackSemitones = keyTrackAmount * (frequencyToMidiNote(noteFrequency) - 60.0)

effectiveEnvAmount = envAmount * (1.0 - velToFilterEnv + velToFilterEnv * velocity)

effectiveCutoff = baseCutoff * 2^((effectiveEnvAmount * filterEnvLevel + keyTrackSemitones) / 12.0)

effectiveCutoff = clamp(effectiveCutoff, 20.0, sampleRate * 0.495)
```

### Velocity Mapping

**Amplitude** (FR-026):
- ADSREnvelope has built-in velocity scaling via `setVelocityScaling(true)` and `setVelocity(velocity)`.
- Peak level = velocity (at velocity 0.5, peak amplitude is 0.5).

**Filter Envelope Depth** (FR-027):
- `effectiveEnvAmount = envAmount * (1.0 - velToFilterEnv + velToFilterEnv * velocity)`
- At velToFilterEnv=0.0: effectiveEnvAmount = envAmount (velocity has no effect)
- At velToFilterEnv=1.0: effectiveEnvAmount = envAmount * velocity

## Utility Function: frequencyToMidiNote

**Layer**: 0 (Core)
**Location**: `dsp/include/krate/dsp/core/pitch_utils.h`
**Namespace**: `Krate::DSP`

```cpp
/// Convert frequency in Hz to continuous MIDI note number.
/// Uses 12-TET formula: midiNote = 12 * log2(hz / 440) + 69
///
/// @param hz Frequency in Hz (must be > 0)
/// @return Continuous MIDI note number (e.g., 69.0 for A4, 60.0 for C4)
///         Returns 0.0 for invalid frequency (<= 0)
/// @note Real-time safe: noexcept, no allocations
[[nodiscard]] inline float frequencyToMidiNote(float hz) noexcept {
    if (hz <= 0.0f) return 0.0f;
    return 12.0f * std::log2(hz / 440.0f) + 69.0f;
}
```

### Relationship to Existing Functions

| Function | Returns | Use Case |
|----------|---------|----------|
| `frequencyToNoteClass(hz)` | int 0-11 | Note-selective filtering |
| `frequencyToCentsDeviation(hz)` | float -50..+50 | Tuning deviation |
| `frequencyToMidiNote(hz)` | float (continuous) | Key tracking in synth voices |
