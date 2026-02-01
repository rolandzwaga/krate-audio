// ==============================================================================
// Layer 3: System Component - Modulation Engine
// ==============================================================================
// Orchestrates all modulation sources and applies routing with curve shaping
// to destination parameters. Central DSP component for modulation.
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
/// - 12 modulation sources: 2 LFOs, EnvFollower, Random, 4 Macros,
///   Chaos, S&H, PitchFollower, Transient
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
/// float val = engine.getModulatedValue(kSweepFrequencyId, baseSweepFreq);
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
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        lfo1_.prepare(sampleRate);
        lfo2_.prepare(sampleRate);
        envFollower_.prepare(sampleRate, maxBlockSize);
        random_.prepare(sampleRate);
        chaos_.prepare(sampleRate);
        sampleHold_.prepare(sampleRate);
        sampleHold_.setLFOPointers(&lfo1_, &lfo2_);
        pitchFollower_.prepare(sampleRate);
        transient_.prepare(sampleRate);

        // Configure amount smoothers
        for (auto& smoother : amountSmoothers_) {
            smoother.configure(20.0f, static_cast<float>(sampleRate));
        }

        reset();
    }

    /// @brief Reset all sources and routing state.
    void reset() noexcept {
        lfo1_.reset();
        lfo2_.reset();
        envFollower_.reset();
        random_.reset();
        chaos_.reset();
        sampleHold_.reset();
        pitchFollower_.reset();
        transient_.reset();
        wasPlaying_ = false;

        modOffsets_.fill(0.0f);
        destActive_.fill(false);

        for (auto& smoother : amountSmoothers_) {
            smoother.reset();
        }

        // Reset routings
        for (auto& r : routings_) {
            r = ModRouting{};
        }

        // Reset macros
        for (auto& m : macros_) {
            m = MacroConfig{};
        }
    }

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
                 size_t numSamples) noexcept {
        // Update LFO tempo
        lfo1_.setTempo(static_cast<float>(ctx.tempoBPM));
        lfo2_.setTempo(static_cast<float>(ctx.tempoBPM));

        // Handle retrigger on transport start
        if (ctx.isPlaying && !wasPlaying_) {
            lfo1_.retrigger();
            lfo2_.retrigger();
        }
        wasPlaying_ = ctx.isPlaying;

        // =====================================================================
        // Determine which sources are active (skip expensive unused sources)
        // =====================================================================
        updateActiveSourceFlags();

        // =====================================================================
        // Per-sample sources: LFOs, EnvFollower, Transient
        // These need sample-accurate processing for waveform/detection accuracy.
        // LFOs and EnvFollower are always processed (cheap, commonly routed).
        // Transient detector is only processed when routed.
        // =====================================================================
        const size_t safeSamples = (numSamples <= monoBuffer_.size())
                                       ? numSamples : monoBuffer_.size();
        const bool needsMono = sourceActive_[static_cast<size_t>(ModSource::PitchFollower)]
                            || sourceActive_[static_cast<size_t>(ModSource::Transient)];

        for (size_t i = 0; i < safeSamples; ++i) {
            float sampleL = (inputL != nullptr) ? inputL[i] : 0.0f;
            float sampleR = (inputR != nullptr) ? inputR[i] : 0.0f;

            // Process LFOs (cheap wavetable lookup, always active)
            lfo1LastValue_ = lfo1_.process();
            lfo2LastValue_ = lfo2_.process();

            // Process envelope follower (cheap, always active for chaos coupling)
            processEnvFollowerSample(sampleL, sampleR);

            if (needsMono) {
                float monoInput = (sampleL + sampleR) * 0.5f;
                monoBuffer_[i] = monoInput;

                // Process transient detector only if routed
                if (sourceActive_[static_cast<size_t>(ModSource::Transient)]) {
                    transient_.process(monoInput);
                }
            }
        }

        // =====================================================================
        // Per-block sources: Pitch, Random, Chaos, S&H
        // Only process sources that have active routings.
        // =====================================================================
        if (sourceActive_[static_cast<size_t>(ModSource::PitchFollower)]) {
            pitchFollower_.processBlock(monoBuffer_.data(), safeSamples);
        }
        if (sourceActive_[static_cast<size_t>(ModSource::Random)]) {
            random_.processBlock(safeSamples);
        }
        if (sourceActive_[static_cast<size_t>(ModSource::Chaos)]) {
            chaos_.process();
        }
        if (sourceActive_[static_cast<size_t>(ModSource::SampleHold)]) {
            sampleHold_.processBlock(safeSamples);
        }

        // Update chaos coupling from audio envelope
        chaos_.setInputLevel(envFollower_.getCurrentValue());

        // Apply unipolar conversion for LFOs if enabled
        float lfo1Output = lfo1LastValue_;
        if (lfo1Unipolar_) {
            lfo1Output = (lfo1Output + 1.0f) * 0.5f;
        }
        lfo1CurrentOutput_ = lfo1Output;

        float lfo2Output = lfo2LastValue_;
        if (lfo2Unipolar_) {
            lfo2Output = (lfo2Output + 1.0f) * 0.5f;
        }
        lfo2CurrentOutput_ = lfo2Output;

        // Evaluate all routings
        evaluateRoutings(numSamples);
    }

    // =========================================================================
    // Modulation Value Retrieval (FR-060, FR-061, FR-062)
    // =========================================================================

    /// @brief Get the modulation offset for a destination parameter.
    /// @param destParamId Destination parameter ID
    /// @return Modulation offset clamped to [-1.0, +1.0]
    [[nodiscard]] float getModulationOffset(uint32_t destParamId) const noexcept {
        if (destParamId >= kMaxModDestinations) {
            return 0.0f;
        }
        return modOffsets_[destParamId];
    }

    /// @brief Get direct pointer to modulation offset array for UI visualization.
    /// Audio thread writes, UI thread reads at display rate (~30Hz).
    /// Aligned float reads are naturally atomic on x86/ARM.
    [[nodiscard]] const float* getModOffsetsArray() const noexcept {
        return modOffsets_.data();
    }

    /// @brief Get the modulated parameter value.
    /// @param destParamId Destination parameter ID
    /// @param baseNormalized Base parameter value (normalized 0-1)
    /// @return Modulated value clamped to [0.0, 1.0]
    [[nodiscard]] float getModulatedValue(uint32_t destParamId,
                                           float baseNormalized) const noexcept {
        float offset = getModulationOffset(destParamId);
        return std::clamp(baseNormalized + offset, 0.0f, 1.0f);
    }

    // =========================================================================
    // Routing Management (FR-003, FR-004)
    // =========================================================================

    /// @brief Set a routing slot.
    /// @param index Routing index (0 to kMaxModRoutings-1)
    /// @param routing Routing configuration
    void setRouting(size_t index, const ModRouting& routing) noexcept {
        if (index >= kMaxModRoutings) {
            return;
        }
        routings_[index] = routing;
        // Snap smoother to current amount for immediate response
        amountSmoothers_[index].snapTo(routing.amount);
    }

    /// @brief Clear a routing slot.
    /// @param index Routing index (0 to kMaxModRoutings-1)
    void clearRouting(size_t index) noexcept {
        if (index >= kMaxModRoutings) {
            return;
        }
        routings_[index] = ModRouting{};
        amountSmoothers_[index].snapTo(0.0f);
    }

    /// @brief Get a routing configuration.
    /// @param index Routing index
    /// @return Current routing at that index
    [[nodiscard]] const ModRouting& getRouting(size_t index) const noexcept {
        static const ModRouting kEmpty{};
        if (index >= kMaxModRoutings) {
            return kEmpty;
        }
        return routings_[index];
    }

    /// @brief Get number of active routings.
    [[nodiscard]] size_t getActiveRoutingCount() const noexcept {
        size_t count = 0;
        for (const auto& r : routings_) {
            if (r.active) {
                ++count;
            }
        }
        return count;
    }

    // =========================================================================
    // LFO 1 Parameters (FR-007 to FR-014a)
    // =========================================================================

    void setLFO1Rate(float hz) noexcept { lfo1_.setFrequency(hz); }
    void setLFO1Waveform(Waveform waveform) noexcept { lfo1_.setWaveform(waveform); }
    void setLFO1PhaseOffset(float degrees) noexcept { lfo1_.setPhaseOffset(degrees); }
    void setLFO1TempoSync(bool enabled) noexcept { lfo1_.setTempoSync(enabled); }
    void setLFO1NoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept {
        lfo1_.setNoteValue(value, modifier);
    }
    void setLFO1Unipolar(bool enabled) noexcept { lfo1Unipolar_ = enabled; }
    void setLFO1Retrigger(bool enabled) noexcept { lfo1_.setRetriggerEnabled(enabled); }

    // =========================================================================
    // LFO 2 Parameters (same API as LFO 1)
    // =========================================================================

    void setLFO2Rate(float hz) noexcept { lfo2_.setFrequency(hz); }
    void setLFO2Waveform(Waveform waveform) noexcept { lfo2_.setWaveform(waveform); }
    void setLFO2PhaseOffset(float degrees) noexcept { lfo2_.setPhaseOffset(degrees); }
    void setLFO2TempoSync(bool enabled) noexcept { lfo2_.setTempoSync(enabled); }
    void setLFO2NoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept {
        lfo2_.setNoteValue(value, modifier);
    }
    void setLFO2Unipolar(bool enabled) noexcept { lfo2Unipolar_ = enabled; }
    void setLFO2Retrigger(bool enabled) noexcept { lfo2_.setRetriggerEnabled(enabled); }

    // =========================================================================
    // Envelope Follower Parameters (FR-015 to FR-020a)
    // =========================================================================

    void setEnvFollowerAttack(float ms) noexcept { envFollower_.setAttackTime(ms); }
    void setEnvFollowerRelease(float ms) noexcept { envFollower_.setReleaseTime(ms); }
    void setEnvFollowerSensitivity(float normalized) noexcept {
        envFollowerSensitivity_ = std::clamp(normalized, 0.0f, 1.0f);
    }
    void setEnvFollowerSource(EnvFollowerSourceType source) noexcept {
        envFollowerSourceType_ = source;
    }

    // =========================================================================
    // Macro Parameters (FR-026 to FR-029a)
    // =========================================================================

    void setMacroValue(size_t index, float value) noexcept {
        if (index < kMaxMacros) {
            macros_[index].value = std::clamp(value, 0.0f, 1.0f);
        }
    }

    void setMacroMin(size_t index, float min) noexcept {
        if (index < kMaxMacros) {
            macros_[index].minOutput = std::clamp(min, 0.0f, 1.0f);
        }
    }

    void setMacroMax(size_t index, float max) noexcept {
        if (index < kMaxMacros) {
            macros_[index].maxOutput = std::clamp(max, 0.0f, 1.0f);
        }
    }

    void setMacroCurve(size_t index, ModCurve curve) noexcept {
        if (index < kMaxMacros) {
            macros_[index].curve = curve;
        }
    }

    // =========================================================================
    // Random Source Parameters (FR-021 to FR-025)
    // =========================================================================

    void setRandomRate(float hz) noexcept { random_.setRate(hz); }
    void setRandomSmoothness(float normalized) noexcept { random_.setSmoothness(normalized); }
    void setRandomTempoSync(bool enabled) noexcept { random_.setTempoSync(enabled); }
    void setRandomTempo(float bpm) noexcept { random_.setTempo(bpm); }

    // =========================================================================
    // Chaos Source Parameters (FR-030 to FR-035)
    // =========================================================================

    void setChaosModel(ChaosModel model) noexcept { chaos_.setModel(model); }
    void setChaosSpeed(float speed) noexcept { chaos_.setSpeed(speed); }
    void setChaosCoupling(float coupling) noexcept { chaos_.setCoupling(coupling); }

    // =========================================================================
    // Sample & Hold Parameters (FR-036 to FR-040)
    // =========================================================================

    void setSampleHoldSource(SampleHoldInputType type) noexcept { sampleHold_.setInputType(type); }
    void setSampleHoldRate(float hz) noexcept { sampleHold_.setRate(hz); }
    void setSampleHoldSlew(float ms) noexcept { sampleHold_.setSlewTime(ms); }
    void setSampleHoldExternalLevel(float level) noexcept { sampleHold_.setExternalLevel(level); }

    // =========================================================================
    // Pitch Follower Parameters (FR-041 to FR-047)
    // =========================================================================

    void setPitchFollowerMinHz(float hz) noexcept { pitchFollower_.setMinHz(hz); }
    void setPitchFollowerMaxHz(float hz) noexcept { pitchFollower_.setMaxHz(hz); }
    void setPitchFollowerConfidence(float threshold) noexcept { pitchFollower_.setConfidenceThreshold(threshold); }
    void setPitchFollowerTrackingSpeed(float ms) noexcept { pitchFollower_.setTrackingSpeed(ms); }

    // =========================================================================
    // Transient Detector Parameters (FR-048 to FR-054)
    // =========================================================================

    void setTransientSensitivity(float sensitivity) noexcept { transient_.setSensitivity(sensitivity); }
    void setTransientAttack(float ms) noexcept { transient_.setAttackTime(ms); }
    void setTransientDecay(float ms) noexcept { transient_.setDecayTime(ms); }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get current source output value for visualization.
    /// @param source Which source to query
    /// @return Current source value
    [[nodiscard]] float getSourceValue(ModSource source) const noexcept {
        return getRawSourceValue(source);
    }

    // =========================================================================
    // State Getters (for serialization)
    // =========================================================================

    // LFO 1
    [[nodiscard]] float getLFO1Rate() const noexcept { return lfo1_.freeRunningFrequency(); }
    [[nodiscard]] Waveform getLFO1Waveform() const noexcept { return lfo1_.waveform(); }
    [[nodiscard]] float getLFO1PhaseOffset() const noexcept { return lfo1_.phaseOffset(); }
    [[nodiscard]] bool getLFO1TempoSync() const noexcept { return lfo1_.tempoSyncEnabled(); }
    [[nodiscard]] NoteValue getLFO1NoteValue() const noexcept { return lfo1_.noteValue(); }
    [[nodiscard]] NoteModifier getLFO1NoteModifier() const noexcept { return lfo1_.noteModifier(); }
    [[nodiscard]] bool getLFO1Unipolar() const noexcept { return lfo1Unipolar_; }
    [[nodiscard]] bool getLFO1Retrigger() const noexcept { return lfo1_.retriggerEnabled(); }

    // LFO 2
    [[nodiscard]] float getLFO2Rate() const noexcept { return lfo2_.freeRunningFrequency(); }
    [[nodiscard]] Waveform getLFO2Waveform() const noexcept { return lfo2_.waveform(); }
    [[nodiscard]] float getLFO2PhaseOffset() const noexcept { return lfo2_.phaseOffset(); }
    [[nodiscard]] bool getLFO2TempoSync() const noexcept { return lfo2_.tempoSyncEnabled(); }
    [[nodiscard]] NoteValue getLFO2NoteValue() const noexcept { return lfo2_.noteValue(); }
    [[nodiscard]] NoteModifier getLFO2NoteModifier() const noexcept { return lfo2_.noteModifier(); }
    [[nodiscard]] bool getLFO2Unipolar() const noexcept { return lfo2Unipolar_; }
    [[nodiscard]] bool getLFO2Retrigger() const noexcept { return lfo2_.retriggerEnabled(); }

    // Envelope Follower
    [[nodiscard]] float getEnvFollowerAttack() const noexcept { return envFollower_.getAttackTime(); }
    [[nodiscard]] float getEnvFollowerRelease() const noexcept { return envFollower_.getReleaseTime(); }
    [[nodiscard]] float getEnvFollowerSensitivity() const noexcept { return envFollowerSensitivity_; }
    [[nodiscard]] EnvFollowerSourceType getEnvFollowerSource() const noexcept { return envFollowerSourceType_; }

    // Random
    [[nodiscard]] float getRandomRate() const noexcept { return random_.getRate(); }
    [[nodiscard]] float getRandomSmoothness() const noexcept { return random_.getSmoothness(); }
    [[nodiscard]] bool getRandomTempoSync() const noexcept { return random_.isTempoSynced(); }

    // Chaos
    [[nodiscard]] ChaosModel getChaosModel() const noexcept { return chaos_.getModel(); }
    [[nodiscard]] float getChaosSpeed() const noexcept { return chaos_.getSpeed(); }
    [[nodiscard]] float getChaosCoupling() const noexcept { return chaos_.getCoupling(); }

    // Sample & Hold
    [[nodiscard]] SampleHoldInputType getSampleHoldSource() const noexcept { return sampleHold_.getInputType(); }
    [[nodiscard]] float getSampleHoldRate() const noexcept { return sampleHold_.getRate(); }
    [[nodiscard]] float getSampleHoldSlew() const noexcept { return sampleHold_.getSlewTime(); }

    // Pitch Follower
    [[nodiscard]] float getPitchFollowerMinHz() const noexcept { return pitchFollower_.getMinHz(); }
    [[nodiscard]] float getPitchFollowerMaxHz() const noexcept { return pitchFollower_.getMaxHz(); }
    [[nodiscard]] float getPitchFollowerConfidence() const noexcept { return pitchFollower_.getConfidenceThreshold(); }
    [[nodiscard]] float getPitchFollowerTrackingSpeed() const noexcept { return pitchFollower_.getTrackingSpeed(); }

    // Transient
    [[nodiscard]] float getTransientSensitivity() const noexcept { return transient_.getSensitivity(); }
    [[nodiscard]] float getTransientAttack() const noexcept { return transient_.getAttackTime(); }
    [[nodiscard]] float getTransientDecay() const noexcept { return transient_.getDecayTime(); }

    // Macros
    [[nodiscard]] const MacroConfig& getMacro(size_t index) const noexcept {
        static const MacroConfig kEmpty{};
        if (index >= kMaxMacros) return kEmpty;
        return macros_[index];
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update flags indicating which sources have active routings.
    void updateActiveSourceFlags() noexcept {
        sourceActive_.fill(false);
        for (const auto& r : routings_) {
            if (r.active && r.source != ModSource::None) {
                sourceActive_[static_cast<size_t>(r.source)] = true;
            }
        }
    }

    /// @brief Get raw output from a modulation source.
    [[nodiscard]] float getRawSourceValue(ModSource source) const noexcept {
        switch (source) {
            case ModSource::None:
                return 0.0f;
            case ModSource::LFO1:
                return lfo1CurrentOutput_;
            case ModSource::LFO2:
                return lfo2CurrentOutput_;
            case ModSource::EnvFollower:
                return std::clamp(envFollower_.getCurrentValue() * envFollowerSensitivity_,
                                  0.0f, 1.0f);
            case ModSource::Random:
                return random_.getCurrentValue();
            case ModSource::Macro1:
                return getMacroOutput(0);
            case ModSource::Macro2:
                return getMacroOutput(1);
            case ModSource::Macro3:
                return getMacroOutput(2);
            case ModSource::Macro4:
                return getMacroOutput(3);
            case ModSource::Chaos:
                return chaos_.getCurrentValue();
            case ModSource::SampleHold:
                return sampleHold_.getCurrentValue();
            case ModSource::PitchFollower:
                return pitchFollower_.getCurrentValue();
            case ModSource::Transient:
                return transient_.getCurrentValue();
        }
        return 0.0f;
    }

    /// @brief Get processed macro output (with Min/Max mapping and curve).
    [[nodiscard]] float getMacroOutput(size_t index) const noexcept {
        if (index >= kMaxMacros) {
            return 0.0f;
        }
        const auto& macro = macros_[index];
        // FR-028: Min/Max mapping FIRST
        float mapped = macro.minOutput + macro.value * (macro.maxOutput - macro.minOutput);
        // FR-029: Curve applied AFTER mapping
        float output = applyModCurve(macro.curve, mapped);
        // FR-029a: clamp to [0, +1]
        return std::clamp(output, 0.0f, 1.0f);
    }

    /// @brief Process envelope follower with selected audio source.
    void processEnvFollowerSample(float inputL, float inputR) noexcept {
        float envInput = 0.0f;
        switch (envFollowerSourceType_) {
            case EnvFollowerSourceType::InputL:
                envInput = inputL;
                break;
            case EnvFollowerSourceType::InputR:
                envInput = inputR;
                break;
            case EnvFollowerSourceType::InputSum:
                envInput = inputL + inputR;
                break;
            case EnvFollowerSourceType::Mid:
                envInput = (inputL + inputR) * 0.5f;
                break;
            case EnvFollowerSourceType::Side:
                envInput = (inputL - inputR) * 0.5f;
                break;
        }
        static_cast<void>(envFollower_.processSample(envInput));
    }

    /// @brief Evaluate all routings and accumulate modulation offsets.
    void evaluateRoutings(size_t numSamples) noexcept {
        // Clear offsets
        modOffsets_.fill(0.0f);
        destActive_.fill(false);

        // Process each routing
        for (size_t i = 0; i < kMaxModRoutings; ++i) {
            const auto& routing = routings_[i];
            if (!routing.active || routing.source == ModSource::None) {
                continue;
            }

            // Get raw source value
            float sourceValue = getRawSourceValue(routing.source);

            // Clamp source to valid range (edge case handling)
            sourceValue = std::clamp(sourceValue, -1.0f, 1.0f);

            // Smooth the amount for zipper-free changes (single step per block
            // since amount changes arrive at block boundaries)
            amountSmoothers_[i].setTarget(routing.amount);
            float smoothedAmount = (numSamples > 0) ? amountSmoothers_[i].process()
                                                     : routing.amount;

            // Apply bipolar modulation: curve on abs(source), then multiply by amount
            float contribution = applyBipolarModulation(routing.curve, sourceValue, smoothedAmount);

            // Accumulate to destination
            if (routing.destParamId < kMaxModDestinations) {
                modOffsets_[routing.destParamId] += contribution;
                destActive_[routing.destParamId] = true;
            }
        }

        // Clamp each destination offset to [-1, +1] per FR-061
        for (size_t i = 0; i < kMaxModDestinations; ++i) {
            if (destActive_[i]) {
                modOffsets_[i] = std::clamp(modOffsets_[i], -1.0f, 1.0f);
            }
        }
    }

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

    // Cached LFO output values (last sample in block)
    float lfo1LastValue_ = 0.0f;
    float lfo2LastValue_ = 0.0f;
    float lfo1CurrentOutput_ = 0.0f;
    float lfo2CurrentOutput_ = 0.0f;

    // LFO unipolar flags
    bool lfo1Unipolar_ = false;
    bool lfo2Unipolar_ = false;

    // Envelope follower configuration
    EnvFollowerSourceType envFollowerSourceType_ = EnvFollowerSourceType::InputSum;
    float envFollowerSensitivity_ = 0.5f;

    // Transport state tracking for retrigger
    bool wasPlaying_ = false;

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
    std::array<float, kMaxModDestinations> modOffsets_ = {};
    std::array<bool, kMaxModDestinations> destActive_ = {};

    // =========================================================================
    // Block Processing Buffer
    // =========================================================================

    /// @brief Mono mix buffer for block-rate pitch follower processing.
    std::array<float, 4096> monoBuffer_ = {};

    /// @brief Tracks which sources have at least one active routing.
    std::array<bool, kModSourceCount> sourceActive_ = {};

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
};

}  // namespace DSP
}  // namespace Krate
