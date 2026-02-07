// ==============================================================================
// API Contract: PolySynthEngine
// ==============================================================================
// This is NOT the implementation file. This is the API contract document
// showing the exact public interface that will be implemented.
//
// Location: dsp/include/krate/dsp/systems/poly_synth_engine.h
// Layer: 3 (System)
// Namespace: Krate::DSP
//
// Dependencies:
//   - Layer 0: sigmoid.h (Sigmoid::tanh), db_utils.h (detail::isNaN/isInf)
//   - Layer 1: svf.h (SVF, SVFMode), polyblep_oscillator.h (OscWaveform),
//              envelope_utils.h (EnvCurve)
//   - Layer 2: mono_handler.h (MonoHandler, MonoMode, PortaMode),
//              note_processor.h (NoteProcessor, VelocityCurve)
//   - Layer 3: voice_allocator.h (VoiceAllocator, AllocationMode, StealMode,
//              VoiceEvent, VoiceState), synth_voice.h (SynthVoice)
// ==============================================================================

#pragma once

namespace Krate::DSP {

// FR-002: Voice mode enumeration
enum class VoiceMode : uint8_t {
    Poly = 0,   // Polyphonic via VoiceAllocator
    Mono = 1    // Monophonic via MonoHandler
};

// FR-001: Complete polyphonic synthesis engine
class PolySynthEngine {
public:
    // =========================================================================
    // Constants (FR-003, FR-004)
    // =========================================================================

    static constexpr size_t kMaxPolyphony = 16;
    static constexpr float kMinMasterGain = 0.0f;
    static constexpr float kMaxMasterGain = 2.0f;

    // =========================================================================
    // Lifecycle (FR-005, FR-006, FR-032, FR-033)
    // =========================================================================

    PolySynthEngine() noexcept;

    // NOT real-time safe. Initializes all sub-components.
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    // Real-time safe. Clears all state to silence.
    void reset() noexcept;

    // =========================================================================
    // Note Dispatch (FR-007 through FR-011)
    // =========================================================================

    void noteOn(uint8_t note, uint8_t velocity) noexcept;
    void noteOff(uint8_t note) noexcept;

    // =========================================================================
    // Polyphony Configuration (FR-012)
    // =========================================================================

    void setPolyphony(size_t count) noexcept;

    // =========================================================================
    // Voice Mode (FR-013)
    // =========================================================================

    void setMode(VoiceMode mode) noexcept;

    // =========================================================================
    // Mono Mode Config (FR-014)
    // =========================================================================

    void setMonoPriority(MonoMode mode) noexcept;
    void setLegato(bool enabled) noexcept;
    void setPortamentoTime(float ms) noexcept;
    void setPortamentoMode(PortaMode mode) noexcept;

    // =========================================================================
    // Voice Allocator Config (FR-015)
    // =========================================================================

    void setAllocationMode(AllocationMode mode) noexcept;
    void setStealMode(StealMode mode) noexcept;

    // =========================================================================
    // NoteProcessor Config (FR-016)
    // =========================================================================

    void setPitchBendRange(float semitones) noexcept;
    void setTuningReference(float a4Hz) noexcept;
    void setVelocityCurve(VelocityCurve curve) noexcept;

    // =========================================================================
    // Pitch Bend (FR-017)
    // =========================================================================

    void setPitchBend(float bipolar) noexcept;

    // =========================================================================
    // Voice Parameter Forwarding (FR-018)
    // All forward to all 16 pre-allocated voices.
    // =========================================================================

    // Oscillators
    void setOsc1Waveform(OscWaveform waveform) noexcept;
    void setOsc2Waveform(OscWaveform waveform) noexcept;
    void setOscMix(float mix) noexcept;
    void setOsc2Detune(float cents) noexcept;
    void setOsc2Octave(int octave) noexcept;

    // Per-voice filter
    void setFilterType(SVFMode type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float semitones) noexcept;
    void setFilterKeyTrack(float amount) noexcept;

    // Amplitude envelope
    void setAmpAttack(float ms) noexcept;
    void setAmpDecay(float ms) noexcept;
    void setAmpSustain(float level) noexcept;
    void setAmpRelease(float ms) noexcept;
    void setAmpAttackCurve(EnvCurve curve) noexcept;
    void setAmpDecayCurve(EnvCurve curve) noexcept;
    void setAmpReleaseCurve(EnvCurve curve) noexcept;

    // Filter envelope
    void setFilterAttack(float ms) noexcept;
    void setFilterDecay(float ms) noexcept;
    void setFilterSustain(float level) noexcept;
    void setFilterRelease(float ms) noexcept;
    void setFilterAttackCurve(EnvCurve curve) noexcept;
    void setFilterDecayCurve(EnvCurve curve) noexcept;
    void setFilterReleaseCurve(EnvCurve curve) noexcept;

    // Velocity routing
    void setVelocityToFilterEnv(float amount) noexcept;

    // =========================================================================
    // Global Filter (FR-019, FR-020, FR-021)
    // =========================================================================

    void setGlobalFilterEnabled(bool enabled) noexcept;
    void setGlobalFilterCutoff(float hz) noexcept;
    void setGlobalFilterResonance(float q) noexcept;
    void setGlobalFilterType(SVFMode mode) noexcept;

    // =========================================================================
    // Master Output (FR-022, FR-023, FR-024, FR-025)
    // =========================================================================

    void setMasterGain(float gain) noexcept;
    void setSoftLimitEnabled(bool enabled) noexcept;

    // =========================================================================
    // Processing (FR-026, FR-027, FR-028, FR-029)
    // =========================================================================

    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // State Queries (FR-030, FR-031)
    // =========================================================================

    [[nodiscard]] uint32_t getActiveVoiceCount() const noexcept;
    [[nodiscard]] VoiceMode getMode() const noexcept;
};

} // namespace Krate::DSP
