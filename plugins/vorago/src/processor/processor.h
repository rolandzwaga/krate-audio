#pragma once

// ==============================================================================
// Vorago - Audio Processor (audio thread)   Plan section 2.5.1
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation. This header includes
// NO controller header.
//
// The class declaration is FINAL (T005). The bodies in processor.cpp start as
// stubs and are replaced, in sequence, by T011-T016.
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"  // Vst::Event (dispatchEvent signature)
#include "pluginterfaces/vst/ivstparameterchanges.h"  // IParamValueQueue (collectPedalPoints)
#include "parameters/bloom_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/ecology_params.h"
#include "parameters/ecosystem_params.h"
#include "parameters/envelope_params.h"
#include "parameters/events_params.h"
#include "parameters/ghost_params.h"
#include "parameters/global_params.h"
#include "parameters/life_params.h"
#include "parameters/macro_params.h"
#include "parameters/noise_params.h"
#include "parameters/param_routes.h"
#include "parameters/resonance_params.h"
#include "parameters/smear_params.h"
#include "parameters/space_params.h"
#include "parameters/sub_params.h"
#include "processor/sustain_latch.h"

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Vorago {

namespace detail {
/// FR-053 test-only seam (SC-011 positive control (b)). Declared here, DEFINED only
/// in tests/integration/continuity_test.cpp; the shipped plugin never names it, so
/// masterGainSnapProbe_ stays false there. ODR sweep 2026-09-25: 0 hits.
struct VoragoMasterGainSmootherBypassProbe;
}  // namespace detail

class Processor : public Steinberg::Vst::AudioEffect {
public:
    /// Plan section 3.2. 1024 x 8 B = 8 KiB; keeps sizeof(Processor) far below
    /// FR-064's 64 KiB.
    static constexpr std::size_t kMaxEventsPerBlock = 1024;

    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    // getTailSamples(): NOT overridden -> SDK default kNoTail (Clarification Q6).

    // ---- test-access seams: const, allocation-free, never called by process() ----
    [[nodiscard]] const Krate::DSP::VoragoEngine* engineForTest() const noexcept {  // FR-026a
        return engine_.get();
    }
    [[nodiscard]] const Krate::DSP::CavernVerb* cavernForTest() const noexcept {  // SC-013
        return cavern_.get();
    }
    [[nodiscard]] std::uint32_t setPolyphonyCallCountForTest() const noexcept {  // FR-024a.1
        return setPolyphonyCalls_;
    }
    [[nodiscard]] std::uint32_t lastSliceCountForTest() const noexcept {  // FR-026
        return lastSliceCount_;
    }
    [[nodiscard]] float masterGainValueForTest() const noexcept {  // P-2
        return masterGain_.getCurrentValue();
    }
    [[nodiscard]] const GlobalParams& globalParamsForTest() const noexcept {  // SC-009
        return globalParams_;
    }
    [[nodiscard]] const MacroParams& macroParamsForTest() const noexcept {  // SC-009
        return macroParams_;
    }
    /// SC-007: +1 per process() that reaches the push step (setMacros / apply /
    /// applyCavernTargets, once each).
    [[nodiscard]] std::uint64_t macroPushCountForTest() const noexcept {
        return macroPushCount_;
    }
    [[nodiscard]] const Krate::DSP::VoragoMacroMatrix& macrosForTest() const noexcept {  // SC-004/5
        return macros_;
    }
    [[nodiscard]] const SustainLatch& latchForTest() const noexcept {  // SC-012 (plan 4.7)
        return latch_;
    }

    /// Const references to every parameter pack (T036, SC-010), band order. A
    /// short-lived view returned by value; reference members are the point.
    // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
    struct PacksForTest {
        const GlobalParams& global;
        const MacroParams& macro;
        const CloudParams& cloud;
        const NoiseParams& noise;
        const ResonanceParams& resonance;
        const EcologyParams& ecology;
        const SubParams& sub;
        const SmearParams& smear;
        const EventsParams& events;
        const EcosystemParams& ecosystem;
        const BodyParams& body;
        const SpaceParams& space;
        const EnvelopeParams& envelope;
        const BloomParams& bloom;
        const GhostParams& ghost;
        const LifeParams& life;
    };
    // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
    [[nodiscard]] PacksForTest packsForTest() const noexcept {  // SC-010
        return PacksForTest{globalParams_,    macroParams_,     cloudParams_,  noiseParams_,
                            resonanceParams_, ecologyParams_,   subParams_,    smearParams_,
                            eventsParams_,    ecosystemParams_, bodyParams_,   spaceParams_,
                            envelopeParams_,  bloomParams_,     ghostParams_,  lifeParams_};
    }

    // FR-024 steps 5-6 (master gain, then the engine's output stage with the
    // limiter LAST), in place. renderSlice() calls it after the engine and the
    // cavern; it is public so SC-006's discrimination arm (ruling B-1,
    // 2026-09-24) can drive it with a signal of known level, which the six-voice
    // render cannot supply (its unity peak is ~0.24). Audio thread only.
    void renderGainAndOutputStage(float* outL, float* outR, std::size_t n) noexcept;

private:
    friend struct detail::VoragoMasterGainSmootherBypassProbe;  // FR-053

    struct EventSlot {           // plan section 3.2
        std::int32_t offset;     // clamped to [0, numSamples - 1]
        std::int32_t listIndex;  // index into data.inputEvents
    };

    /// Plan 4.6: who asked for a full re-push. Reprepared runs the pushes at
    /// once (setupProcessing, audio stopped); PresetLoad only invalidates and
    /// lets the push step of the same process() call do the work.
    enum class Scope : std::uint8_t { Reprepared, PresetLoad };

    /// Plan 4.7: one sustain-pedal automation point of the current block.
    /// offset is the RAW host offset (clamped by process() against numSamples).
    struct PedalPoint {
        std::int32_t offset;
        bool down;  // plain value >= 0.5
    };
    static constexpr std::size_t kMaxPedalPoints = 128;

    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void pushAllSurfaces(Scope scope) noexcept;
    void pushGlobalParams() noexcept;
    void pushEngParams() noexcept;
    void pushCavernParams() noexcept;
    void pushVoiceParams() noexcept;
    void pushMacroBases() noexcept;
    void markDirty(Steinberg::Vst::ParamID id) noexcept;
    void collectPedalPoints(Steinberg::Vst::IParamValueQueue* queue,
                            Steinberg::int32 count) noexcept;
    void applyPedalPoint(const PedalPoint& p) noexcept;
    void applyPendingPedalPointsImmediately() noexcept;
    void noteOffToEngine(std::uint8_t note) noexcept;
    [[nodiscard]] Krate::DSP::VoragoMacroValues buildMacroVector() const noexcept;
    [[nodiscard]] const std::atomic<float>* mbSourceFor(Steinberg::Vst::ParamID id) const noexcept;
    std::size_t buildEventOrder(Steinberg::Vst::IEventList* events, std::size_t total) noexcept;
    void dispatchEvent(const Steinberg::Vst::Event& e) noexcept;
    void renderSlice(float* outL, float* outR, std::size_t n) noexcept;

    std::unique_ptr<Krate::DSP::VoragoEngine> engine_;  // FR-022: NEVER by value / stack
    std::unique_ptr<Krate::DSP::CavernVerb> cavern_;    // FR-022
    // FR-022: by value. Written once per process() (FR-020/FR-021): MB bases,
    // then the macro vector, then apply + applyCavernTargets.
    Krate::DSP::VoragoMacroMatrix macros_{};

    GlobalParams globalParams_{};
    MacroParams macroParams_{};  // MAC route (FR-021)
    // Phase 12 packs (T036): stored by processParameterChanges; their MB fields
    // are pushed by pushMacroBases() (T037), the rest by later routes.
    CloudParams cloudParams_{};
    NoiseParams noiseParams_{};
    ResonanceParams resonanceParams_{};
    EcologyParams ecologyParams_{};
    SubParams subParams_{};
    SmearParams smearParams_{};
    EventsParams eventsParams_{};
    EcosystemParams ecosystemParams_{};
    BodyParams bodyParams_{};
    SpaceParams spaceParams_{};
    EnvelopeParams envelopeParams_{};
    BloomParams bloomParams_{};
    GhostParams ghostParams_{};
    LifeParams lifeParams_{};

    Krate::DSP::OnePoleSmoother masterGain_{1.0f};  // P-2: constructed at 1.0f on purpose
    bool snapGainPending_ = true;                   // FR-024a.2 first-block snap
    bool masterGainSnapProbe_ = false;  // FR-053: set only by the test probe; false when shipped
    bool prepared_ = false;
    std::size_t lastPushedPolyphony_ = 0;
    std::uint32_t setPolyphonyCalls_ = 0;  // written only on the setup/process thread
    std::uint32_t lastSliceCount_ = 0;
    std::array<EventSlot, kMaxEventsPerBlock> eventOrder_{};

    // MB route (plan 4.4): kMbRoutes[i] reads mbSources_[i] (built once by the
    // constructor, no per-block switch); lastPushedMb_[i] is the plain value last
    // handed to setTargetBase. mbValid_ false forces every base on the next push.
    std::array<const std::atomic<float>*, kMbRoutes.size()> mbSources_{};
    std::array<float, kMbRoutes.size()> lastPushedMb_{};
    bool mbValid_ = false;
    std::uint64_t macroPushCount_ = 0;  // SC-007 seam; only incremented, never branched on

    // VP route (plan 4.2, 4.4): markDirty() bumps voiceParamGeneration_ for every
    // stored VP change; pushVoiceParams() broadcasts applyVoiceParams() once when
    // it differs from lastAppliedVpGen_. The sentinel start forces the first push.
    static constexpr std::uint64_t kGenerationSentinel = ~std::uint64_t{0};
    std::uint64_t voiceParamGeneration_ = 0;
    std::uint64_t lastAppliedVpGen_ = kGenerationSentinel;

    // ENG route (plan 4.4, FR-023): one tracker per direct engine setter, holding
    // the PLAIN value last pushed. engValid_ false forces every ENG value on the
    // next push EXCEPT the seed, which is compared on its own (plan 4.6, D-P2):
    // setupProcessing() seeds before prepare and records lastSeedIndex_.
    int lastSeedIndex_ = 0;
    int lastEnvMode_ = 0;
    std::array<float, 4> lastStageMs_{};
    float lastReleaseMs_ = 0.0f;
    float lastGrowthS_ = 0.0f;
    std::array<float, 3> lastSubToneDb_{};
    float lastGhostReverse_ = 0.0f;
    bool lastGhostTriggers_ = false;
    bool engValid_ = false;

    // CV route (plan 4.4): the PLAIN value last handed to each of the eight float
    // CavernVerb setters (pushCavernParams() order: density, dimensionality,
    // breath, early size, early level, early absorption, early send, damper
    // rate) plus freeze. cvValid_ false forces every CV value on the next push.
    std::array<float, 8> lastCv_{};
    bool lastFreeze_ = false;
    bool cvValid_ = false;

    // FR-022 / plan 4.3 (D-P7): a full re-push requested from outside the audio
    // thread (setState, T042). Consumed by process() AFTER the not-ready path, so
    // a process() issued before setupProcessing() cannot swallow it.
    std::atomic<bool> forcePushPending_{false};
    // FR-030 / C-9 (Q8 ruled (a)): setState() raises it (release store) and makes
    // no DSP call; the next process() releases every latched note (consumed in T043).
    std::atomic<bool> latchReleasePending_{false};

    // FR-030 / C-9 (plan 4.7): audio-thread sustain latch plus EVERY sustain
    // point of the current block (processParameterChanges() fills them; the
    // slice loop, or an early return, consumes them). Surplus points past
    // kMaxPedalPoints coalesce into the last slot, so the final state survives.
    SustainLatch latch_{};
    std::array<PedalPoint, kMaxPedalPoints> pedalPoints_{};
    std::size_t numPedalPoints_ = 0;
};

// FR-064: unique_ptr ownership keeps the object small; tests still heap-allocate it.
static_assert(sizeof(Processor) < 64u * 1024u, "FR-064: the 808 KB engine must live on the heap");

}  // namespace Vorago
