// ==============================================================================
// Layer 3: System Component - Modulation Engine
// ==============================================================================
// Orchestrates all modulation sources and applies routing with curve shaping
// to destination parameters. Central DSP component for Disrumpo modulation.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 3 (depends only on Layer 0-2)
// - Principle XI: Performance Budget (<1% CPU for 32 routings)
//
// Reference: specs/008-modulation-system/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/modulation_curves.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/chaos_mod_source.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/pitch_follower_source.h>
#include <krate/dsp/processors/random_source.h>
#include <krate/dsp/processors/sample_hold_source.h>
#include <krate/dsp/processors/transient_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace Krate {
namespace DSP {

/// @brief Maximum number of modulatable destination parameters.
inline constexpr size_t kMaxModDestinations = 128;

/// @brief Layer 3 System Component - Modulation Engine
///
/// Owns all 12 modulation sources and processes up to 32 routings per block.
/// Each routing specifies source, destination, bipolar amount, and curve shape.
///
/// @par Features
/// - 12 modulation sources: 2 LFOs, EnvFollower, Random, 4 Macros, Chaos, S&H, PitchFollower, Transient
/// - Up to 32 simultaneous routings (FR-004)
/// - 4 curve shapes per routing: Linear, Exponential, S-Curve, Stepped (FR-058)
/// - Bipolar amount [-1, +1] with correct curve application order (FR-059)
/// - Multi-source summation with clamping (FR-060, FR-061, FR-062)
/// - Real-time safe: noexcept, no allocations in process (FR-005)
///
/// @par Usage
/// @code
/// ModulationEngine engine;
/// engine.prepare(44100.0, 512);
///
/// // Configure sources
/// engine.setLFO1Rate(2.0f);
/// engine.setLFO1Waveform(Waveform::Sine);
///
/// // Set up routing
/// ModRouting routing;
/// routing.source = ModSource::LFO1;
/// routing.destParamId = kSweepFrequencyId;
/// routing.amount = 0.5f;
/// routing.curve = ModCurve::Linear;
/// routing.active = true;
/// engine.setRouting(0, routing);
///
/// // In process callback
/// engine.process(blockCtx, inputL, inputR, numSamples);
/// float modulatedSweepFreq = engine.getModulatedValue(kSweepFrequencyId, baseSweepFreq);
/// @endcode
class ModulationEngine {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    ModulationEngine() noexcept = default;

    // Non-copyable (contains LFOs which are non-copyable)
    ModulationEngine(const ModulationEngine&) = delete;
    ModulationEngine& operator=(const ModulationEngine&) = delete;
    ModulationEngine(ModulationEngine&&) noexcept = default;
    ModulationEngine& operator=(ModulationEngine&&) noexcept = default;

    /// @brief Prepare all sources for processing.
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all sources and routing state.
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-001, FR-005)
    // =========================================================================

    /// @brief Process one audio block through the modulation engine.
    ///
    /// Updates all sources, evaluates routings, and computes modulation offsets.
    ///
    /// @param ctx Block context with tempo/transport info
    /// @param inputL Left channel audio input (for envelope follower, pitch, transient)
    /// @param inputR Right channel audio input
    /// @param numSamples Number of samples in this block
    void process(const BlockContext& ctx,
                 const float* inputL, const float* inputR,
                 size_t numSamples) noexcept;

    // =========================================================================
    // Modulation Value Retrieval (FR-060, FR-061, FR-062)
    // =========================================================================

    /// @brief Get the modulation offset for a destination parameter.
    /// @param destParamId Destination parameter ID
    /// @return Modulation offset clamped to [-1.0, +1.0]
    [[nodiscard]] float getModulationOffset(uint32_t destParamId) const noexcept;

    /// @brief Get the modulated parameter value.
    /// @param destParamId Destination parameter ID
    /// @param baseNormalized Base parameter value (normalized 0-1)
    /// @return Modulated value clamped to [0.0, 1.0]
    [[nodiscard]] float getModulatedValue(uint32_t destParamId,
                                           float baseNormalized) const noexcept;

    // =========================================================================
    // Routing Management (FR-003, FR-004)
    // =========================================================================

    /// @brief Set a routing slot.
    /// @param index Routing index (0 to kMaxModRoutings-1)
    /// @param routing Routing configuration
    void setRouting(size_t index, const ModRouting& routing) noexcept;

    /// @brief Clear a routing slot.
    /// @param index Routing index (0 to kMaxModRoutings-1)
    void clearRouting(size_t index) noexcept;

    /// @brief Get a routing configuration.
    /// @param index Routing index
    /// @return Current routing at that index
    [[nodiscard]] const ModRouting& getRouting(size_t index) const noexcept;

    /// @brief Get number of active routings.
    [[nodiscard]] size_t getActiveRoutingCount() const noexcept;

    // =========================================================================
    // LFO 1 Parameters (FR-007 to FR-014a)
    // =========================================================================

    void setLFO1Rate(float hz) noexcept;
    void setLFO1Waveform(Waveform waveform) noexcept;
    void setLFO1PhaseOffset(float degrees) noexcept;
    void setLFO1TempoSync(bool enabled) noexcept;
    void setLFO1NoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setLFO1Unipolar(bool enabled) noexcept;
    void setLFO1Retrigger(bool enabled) noexcept;

    // =========================================================================
    // LFO 2 Parameters (same API as LFO 1)
    // =========================================================================

    void setLFO2Rate(float hz) noexcept;
    void setLFO2Waveform(Waveform waveform) noexcept;
    void setLFO2PhaseOffset(float degrees) noexcept;
    void setLFO2TempoSync(bool enabled) noexcept;
    void setLFO2NoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setLFO2Unipolar(bool enabled) noexcept;
    void setLFO2Retrigger(bool enabled) noexcept;

    // =========================================================================
    // Envelope Follower Parameters (FR-015 to FR-020a)
    // =========================================================================

    void setEnvFollowerAttack(float ms) noexcept;
    void setEnvFollowerRelease(float ms) noexcept;
    void setEnvFollowerSensitivity(float normalized) noexcept;
    void setEnvFollowerSource(EnvFollowerSourceType source) noexcept;

    // =========================================================================
    // Random Source Parameters (FR-021 to FR-025)
    // =========================================================================

    void setRandomRate(float hz) noexcept;
    void setRandomSmoothness(float normalized) noexcept;
    void setRandomTempoSync(bool enabled) noexcept;

    // =========================================================================
    // Macro Parameters (FR-026 to FR-029a)
    // =========================================================================

    void setMacroValue(size_t index, float value) noexcept;
    void setMacroMin(size_t index, float min) noexcept;
    void setMacroMax(size_t index, float max) noexcept;
    void setMacroCurve(size_t index, ModCurve curve) noexcept;

    // =========================================================================
    // Chaos Source Parameters (FR-030 to FR-035)
    // =========================================================================

    void setChaosModel(ChaosModel model) noexcept;
    void setChaosSpeed(float speed) noexcept;
    void setChaosCoupling(float coupling) noexcept;

    // =========================================================================
    // Sample & Hold Parameters (FR-036 to FR-040)
    // =========================================================================

    void setSampleHoldSource(SampleHoldInputType source) noexcept;
    void setSampleHoldRate(float hz) noexcept;
    void setSampleHoldSlew(float ms) noexcept;

    // =========================================================================
    // Pitch Follower Parameters (FR-041 to FR-047)
    // =========================================================================

    void setPitchFollowerMinHz(float hz) noexcept;
    void setPitchFollowerMaxHz(float hz) noexcept;
    void setPitchFollowerConfidence(float threshold) noexcept;
    void setPitchFollowerTrackingSpeed(float ms) noexcept;

    // =========================================================================
    // Transient Detector Parameters (FR-048 to FR-054)
    // =========================================================================

    void setTransientSensitivity(float sensitivity) noexcept;
    void setTransientAttack(float ms) noexcept;
    void setTransientDecay(float ms) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get current source output value for visualization.
    /// @param source Which source to query
    /// @return Current source value
    [[nodiscard]] float getSourceValue(ModSource source) const noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Get raw output from a modulation source.
    [[nodiscard]] float getRawSourceValue(ModSource source) const noexcept;

    /// @brief Get processed macro output (with Min/Max mapping and curve).
    [[nodiscard]] float getMacroOutput(size_t index) const noexcept;

    /// @brief Process audio-dependent sources for one sample.
    void processAudioSample(float inputL, float inputR) noexcept;

    /// @brief Evaluate all routings and accumulate modulation offsets.
    void evaluateRoutings() noexcept;

    // =========================================================================
    // Sources
    // =========================================================================

    LFO lfo1_;
    LFO lfo2_;
    EnvelopeFollower envFollower_;
    RandomSource random_;
    ChaosModSource chaos_;
    SampleHoldSource sampleHold_;
    PitchFollowerSource pitchFollower_;
    TransientDetector transient_;

    // LFO unipolar flags
    bool lfo1Unipolar_ = false;
    bool lfo2Unipolar_ = false;

    // Envelope follower source type
    EnvFollowerSourceType envFollowerSourceType_ = EnvFollowerSourceType::InputSum;

    // Envelope follower sensitivity (scales output)
    float envFollowerSensitivity_ = 0.5f;

    // =========================================================================
    // Macros
    // =========================================================================

    std::array<MacroConfig, kMaxMacros> macros_ = {};

    // =========================================================================
    // Routing
    // =========================================================================

    std::array<ModRouting, kMaxModRoutings> routings_ = {};
    std::array<OnePoleSmoother, kMaxModRoutings> amountSmoothers_;

    // Per-destination modulation offset accumulation
    // Using a flat array indexed by param ID offset for real-time safety
    // (unordered_map allocates; use array or fixed-size map instead)
    std::array<float, kMaxModDestinations> modOffsets_ = {};
    std::array<bool, kMaxModDestinations> destActive_ = {};

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
};

}  // namespace DSP
}  // namespace Krate
