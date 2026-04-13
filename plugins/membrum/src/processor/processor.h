#pragma once

// ==============================================================================
// Membrum Processor -- Phase 5
// ==============================================================================
// Phase 5 adds cross-pad sympathetic resonance (coupling). The coupling engine
// (SympatheticResonance) processes after VoicePool output: mono sum -> delay ->
// engine -> energy limiter -> add to L/R. Global coupling parameters (270-273)
// control the coupling amount, snare buzz, tom resonance, and propagation delay.
// ==============================================================================

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include "dsp/drum_voice.h"
#include "dsp/pad_config.h"
#include "dsp/pad_category.h"
#include "dsp/coupling_matrix.h"
#include "dsp/pad_glow_publisher.h"
#include "dsp/matrix_activity_publisher.h"
#include "processor/macro_mapper.h"
#include "voice_pool/voice_pool.h"

#include <krate/dsp/systems/sympathetic_resonance.h>
#include <krate/dsp/primitives/delay_line.h>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

// Forward-declare DataExchangeHandler so processor.h stays cheap to include.
namespace Steinberg::Vst { class DataExchangeHandler; }

namespace Membrum {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    /// Load a kit preset blob (v4 format without selectedPadIndex, 9036 bytes).
    /// Does NOT modify selectedPadIndex.
    Steinberg::tresult loadKitPreset(Steinberg::IBStream* stream);
    Steinberg::tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                              Steinberg::Vst::BusDirection dir,
                                              Steinberg::int32 index,
                                              Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;

    // Test-only accessors (Phase 4)
    VoicePool& voicePoolForTest() noexcept { return voicePool_; }
    const VoicePool& voicePoolForTest() const noexcept { return voicePool_; }
    void setSelectedPadIndexForTest(int idx) noexcept { selectedPadIndex_ = idx; }
    int selectedPadIndexForTest() const noexcept { return selectedPadIndex_; }

    // Test-only accessors (Phase 5: coupling)
    Krate::DSP::SympatheticResonance& couplingEngineForTest() noexcept { return couplingEngine_; }
    const CouplingMatrix& couplingMatrixForTest() const noexcept { return couplingMatrix_; }
    float energyEnvelopeForTest() const noexcept { return energyEnvelope_; }

    // Test-only accessors (Phase 6: macros, publishers)
    MacroMapper& macroMapperForTest() noexcept { return macroMapper_; }
    PadGlowPublisher& padGlowPublisherForTest() noexcept { return padGlowPublisher_; }
    MatrixActivityPublisher& matrixActivityPublisherForTest() noexcept { return matrixActivityPublisher_; }
    bool editorOpenForTest() const noexcept { return editorOpen_.load(); }
    void setEditorOpenForTest(bool open) noexcept { editorOpen_.store(open); }

private:
    void processParameterChanges(Steinberg::Vst::IParameterChanges* paramChanges);
    void processEvents(Steinberg::Vst::IEventList* events);

    /// Phase 6 (US4): recompute the coupling matrix passing the current
    /// per-pad couplingAmount values so they are baked into effectiveGain
    /// (FR-014, FR-023). Called whenever a Tier 1 knob, pad category, or
    /// per-pad coupling amount changes.
    void recomputeCouplingMatrix() noexcept;

    // DSP
    VoicePool voicePool_;
    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 2048;

    // ---- Global-only parameters (not per-pad) ----
    std::atomic<int> maxPolyphony_{8};           // FR-111 -- [4, 16]
    std::atomic<int> voiceStealingPolicy_{0};    // FR-120 -- VoiceStealingPolicy int

    // ---- Phase 4: per-pad parameters are stored in VoicePool::padConfigs_ ----
    // No individual atomic<float> fields for material/size/decay etc. anymore.
    // The processor dispatches per-pad parameter changes directly to VoicePool.

    // ---- Phase 4: bus activation tracking ----
    std::array<bool, kMaxOutputBuses> busActive_ = {true};  // bus 0 always active

    // ---- Phase 4: selected pad index (for state only, not used in audio) ----
    int selectedPadIndex_ = 0;

    // ---- Phase 5: Cross-pad coupling (sympathetic resonance) ----
    Krate::DSP::SympatheticResonance  couplingEngine_;
    Krate::DSP::DelayLine             couplingDelay_;
    CouplingMatrix                    couplingMatrix_;

    // Global coupling parameters (atomics for thread-safe parameter updates)
    std::atomic<float> globalCoupling_{0.0f};
    std::atomic<float> snareBuzz_{0.0f};
    std::atomic<float> tomResonance_{0.0f};
    std::atomic<float> couplingDelayMs_{1.0f};

    // Cached pad categories (recomputed on pad config changes)
    std::array<PadCategory, kNumPads> padCategories_{};

    // Energy limiter state
    float energyEnvelope_ = 0.0f;

    // ---- Phase 6: Macros, publishers, meters DataExchange, editor state ----
    MacroMapper              macroMapper_;
    PadGlowPublisher         padGlowPublisher_;
    MatrixActivityPublisher  matrixActivityPublisher_;
    std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_;
    std::atomic<bool>        editorOpen_{false};

    /// Phase 6: build the RegisteredDefaultsTable consumed by MacroMapper.
    /// Mirrors the Controller's registered-default values for Phase 4/5 per-pad
    /// parameters referenced by the five macros.
    [[nodiscard]] RegisteredDefaultsTable buildRegisteredDefaultsTable() const noexcept;

    /// One-pole envelope follower energy limiter (FR-014, SC-007).
    /// Caps coupling output below -20 dBFS.
    float applyEnergyLimiter(float sample) noexcept
    {
        constexpr float kThreshold = 0.1f;     // -20 dBFS
        constexpr float kAttackCoeff = 0.001f;  // fast attack
        constexpr float kReleaseCoeff = 0.9999f; // slow release
        float absVal = std::abs(sample);
        float coeff = absVal > energyEnvelope_ ? kAttackCoeff : kReleaseCoeff;
        energyEnvelope_ += (1.0f - coeff) * (absVal - energyEnvelope_);
        if (energyEnvelope_ > kThreshold)
        {
            return sample * (kThreshold / energyEnvelope_);
        }
        return sample;
    }
};

} // namespace Membrum
