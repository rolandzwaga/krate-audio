// ==============================================================================
// API Contract: RuinaeVoice
// ==============================================================================
// Layer 3: System Component
// Location: dsp/include/krate/dsp/systems/ruinae_voice.h
//
// Complete per-voice processing unit for the Ruinae synthesizer.
// Composes: 2x SelectableOscillator + Mixer + Filter + Distortion +
//           TranceGate + VCA + 3x ADSR + LFO + VoiceModRouter
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// Forward declarations
enum class OscType : uint8_t;
enum class MixMode : uint8_t;
enum class RuinaeFilterType : uint8_t;
enum class RuinaeDistortionType : uint8_t;
struct TranceGateParams;
struct VoiceModRoute;

class ADSREnvelope;
class LFO;

class RuinaeVoice {
public:
    RuinaeVoice() noexcept = default;
    ~RuinaeVoice() noexcept = default;

    // Non-copyable, movable
    RuinaeVoice(const RuinaeVoice&) = delete;
    RuinaeVoice& operator=(const RuinaeVoice&) = delete;
    RuinaeVoice(RuinaeVoice&&) noexcept = default;
    RuinaeVoice& operator=(RuinaeVoice&&) noexcept = default;

    // Lifecycle (FR-031, FR-032)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Note control (FR-028, FR-029, FR-030)
    void noteOn(float frequency, float velocity) noexcept;
    void noteOff() noexcept;
    void setFrequency(float hz) noexcept;
    [[nodiscard]] bool isActive() const noexcept;

    // Processing (FR-033, FR-034)
    void processBlock(float* output, size_t numSamples) noexcept;

    // Oscillator A (FR-001 through FR-005)
    void setOscAType(OscType type) noexcept;
    void setOscBType(OscType type) noexcept;

    // Mixer (FR-006, FR-007, FR-008)
    void setMixMode(MixMode mode) noexcept;
    void setMixPosition(float mix) noexcept;

    // Filter (FR-010, FR-011, FR-012)
    void setFilterType(RuinaeFilterType type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float semitones) noexcept;
    void setFilterKeyTrack(float amount) noexcept;

    // Distortion (FR-013, FR-014, FR-015)
    void setDistortionType(RuinaeDistortionType type) noexcept;
    void setDistortionDrive(float drive) noexcept;
    void setDistortionCharacter(float character) noexcept;

    // Trance Gate (FR-016, FR-017, FR-018, FR-019)
    void setTranceGateEnabled(bool enabled) noexcept;
    void setTranceGateParams(const TranceGateParams& params) noexcept;
    void setTranceGateTempo(double bpm) noexcept;
    [[nodiscard]] float getGateValue() const noexcept;

    // Modulation routing (FR-024)
    void setModRoute(int index, VoiceModRoute route) noexcept;

    // Envelope/LFO access (FR-022, FR-023)
    ADSREnvelope& getAmpEnvelope() noexcept;
    ADSREnvelope& getFilterEnvelope() noexcept;
    ADSREnvelope& getModEnvelope() noexcept;
    LFO& getVoiceLFO() noexcept;
};

} // namespace Krate::DSP
