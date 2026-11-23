// ==============================================================================
// Arp skip highlights (R-069, R-002)
// ==============================================================================
// A skipped arp step (Euclidean rest, failed condition, modifier rest) is shown
// on every lane of the editor. The processor used to send one "ArpSkipEvent"
// IMessage per lane from process(): a synchronous host call on the audio thread
// that ran the controller's VSTGUI code there too. It also sent the velocity
// lane's step to six lanes and nothing to the chord and inversion lanes.
//
// The skip now travels as eight hidden output parameters (kArpSkipLane0Id ...
// kArpSkipLane7Id), polled by the controller's 30 fps timer like the
// playheads. Each carries the step its own lane was on when the arp skipped.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace {

// -----------------------------------------------------------------------------
// Output parameter capture (records every addPoint the processor makes)
// -----------------------------------------------------------------------------

class SkipOutputQueue final : public Steinberg::Vst::IParamValueQueue {
public:
    explicit SkipOutputQueue(Steinberg::Vst::ParamID id) : id_(id) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
    Steinberg::int32 PLUGIN_API getPointCount() override {
        return static_cast<Steinberg::int32>(values_.size());
    }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset,
                                           Steinberg::Vst::ParamValue& value) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(values_.size()))
            return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = values_[static_cast<size_t>(index)];
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32, Steinberg::Vst::ParamValue value,
                                           Steinberg::int32& index) override {
        index = static_cast<Steinberg::int32>(values_.size());
        values_.push_back(value);
        return Steinberg::kResultTrue;
    }

    [[nodiscard]] std::optional<double> last() const {
        if (values_.empty()) return std::nullopt;
        return values_.back();
    }

private:
    Steinberg::Vst::ParamID id_;
    std::vector<double> values_;
};

class SkipOutputChanges final : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size())) return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,
                                                                  Steinberg::int32& index) override {
        for (size_t i = 0; i < queues_.size(); ++i) {
            if (queues_[i].getParameterId() == id) {
                index = static_cast<Steinberg::int32>(i);
                return &queues_[i];
            }
        }
        index = static_cast<Steinberg::int32>(queues_.size());
        queues_.emplace_back(id);
        return &queues_.back();
    }

    [[nodiscard]] std::optional<double> last(Steinberg::Vst::ParamID id) {
        for (auto& q : queues_)
            if (q.getParameterId() == id) return q.last();
        return std::nullopt;
    }

    void clear() { queues_.clear(); }

private:
    std::vector<SkipOutputQueue> queues_;
};

// -----------------------------------------------------------------------------
// Connection peer that records whether a message arrived inside process()
// -----------------------------------------------------------------------------

class SkipRecordingPeer final : public Steinberg::Vst::IConnectionPoint {
public:
    const bool* inProcess = nullptr;
    int notifiesInsideProcess = 0;
    int notifiesOutsideProcess = 0;

    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint*) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override {
        if (inProcess != nullptr && *inProcess)
            ++notifiesInsideProcess;
        else
            ++notifiesOutsideProcess;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

// Lane order of the skip and playhead parameters (the editor's lane order).
constexpr std::array<Steinberg::Vst::ParamID, 8> kSkipIds{
    Ruinae::kArpSkipLane0Id, Ruinae::kArpSkipLane1Id, Ruinae::kArpSkipLane2Id,
    Ruinae::kArpSkipLane3Id, Ruinae::kArpSkipLane4Id, Ruinae::kArpSkipLane5Id,
    Ruinae::kArpSkipLane6Id, Ruinae::kArpSkipLane7Id};
constexpr std::array<Steinberg::Vst::ParamID, 8> kPlayheadIds{
    Ruinae::kArpVelocityPlayheadId, Ruinae::kArpGatePlayheadId,
    Ruinae::kArpPitchPlayheadId, Ruinae::kArpRatchetPlayheadId,
    Ruinae::kArpModifierPlayheadId, Ruinae::kArpConditionPlayheadId,
    Ruinae::kArpChordPlayheadId, Ruinae::kArpInversionPlayheadId};

/// Skip encoding (step + 1 + 33 * (seq & 3)) / 132; 0 = none.
std::optional<int> decodeSkipStep(double value) {
    const long code = std::lround(value * 132.0);
    if (code <= 0) return std::nullopt;
    return static_cast<int>((code - 1) % 33);
}

/// Playhead encoding step / 32; 1.0 = no playback.
int decodePlayhead(double value) {
    const long step = std::lround(value * 32.0);
    return step >= 32 ? -1 : static_cast<int>(step);
}

double laneLength(int length) { return static_cast<double>(length - 1) / 31.0; }

// -----------------------------------------------------------------------------
// Processor rig: host context (so allocateMessage() works), connected peer,
// editor reported open, arp in MIDI mode with a 3-in-8 Euclidean pattern.
// -----------------------------------------------------------------------------

struct SkipFixture {
    static constexpr size_t kBlockSize = 512;
    static constexpr double kSampleRate = 44100.0;

    Steinberg::Vst::HostApplication host;
    Ruinae::Processor processor;
    SkipRecordingPeer peer;
    bool inProcess = false;

    Krate::Test::EventList events;
    Krate::Test::ParameterChanges noParams;
    SkipOutputChanges output;
    std::vector<float> outL = std::vector<float>(kBlockSize, 0.0f);
    std::vector<float> outR = std::vector<float>(kBlockSize, 0.0f);
    float* channels[2]{};
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    Steinberg::Vst::ProcessContext context{};

    SkipFixture() {
        channels[0] = outL.data();
        channels[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channels;

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &noParams;
        data.outputParameterChanges = &output;
        data.inputEvents = &events;

        context.state = Steinberg::Vst::ProcessContext::kPlaying
                      | Steinberg::Vst::ProcessContext::kTempoValid
                      | Steinberg::Vst::ProcessContext::kTimeSigValid;
        context.tempo = 120.0;
        context.timeSigNumerator = 4;
        context.timeSigDenominator = 4;
        context.sampleRate = kSampleRate;
        data.processContext = &context;

        peer.inProcess = &inProcess;
        REQUIRE(processor.initialize(&host) == Steinberg::kResultOk);
        processor.connect(&peer);

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);

        // The controller reports an open editor from didOpen().
        auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
        msg->setMessageID("EditorState");
        msg->getAttributes()->setInt("open", 1);
        processor.notify(msg);
    }

    ~SkipFixture() {
        processor.setActive(false);
        processor.disconnect(&peer);
        processor.terminate();
    }

    void process(Steinberg::Vst::IParameterChanges* params = nullptr) {
        output.clear();
        data.inputParameterChanges = params != nullptr ? params : &noParams;
        inProcess = true;
        processor.process(data);
        inProcess = false;
        data.inputParameterChanges = &noParams;
        events.clear();
        context.projectTimeSamples += static_cast<Steinberg::int64>(kBlockSize);
        context.projectTimeMusic += static_cast<double>(kBlockSize) / kSampleRate * (120.0 / 60.0);
    }

    /// Arp on (MIDI), Euclidean 3 hits in 8 steps, lanes of different lengths.
    void configureArp() {
        Krate::Test::ParameterChanges params;
        params.addChange(Ruinae::kArpOperatingModeId, 1.0 / 3.0);
        params.addChange(Ruinae::kArpEuclideanEnabledId, 1.0);
        params.addChange(Ruinae::kArpEuclideanHitsId, 3.0 / 32.0);
        params.addChange(Ruinae::kArpEuclideanStepsId, (8.0 - 2.0) / 30.0);
        params.addChange(Ruinae::kArpVelocityLaneLengthId, laneLength(4));
        params.addChange(Ruinae::kArpGateLaneLengthId, laneLength(3));
        // Ratchet and modifier differ in length so their two skip IDs cannot be swapped unseen
        params.addChange(Ruinae::kArpRatchetLaneLengthId, laneLength(7));
        params.addChange(Ruinae::kArpModifierLaneLengthId, laneLength(6));
        params.addChange(Ruinae::kArpChordLaneLengthId, laneLength(2));
        params.addChange(Ruinae::kArpInversionLaneLengthId, laneLength(5));
        process(&params);
    }
};

} // namespace

TEST_CASE("process() never calls sendMessage for skip events", "[vst][arp][rt-safety]") {
    SkipFixture f;
    f.configureArp();
    f.events.addNoteOn(60, 0.8f);

    int skipBlocks = 0;
    // 1/8 steps at 120 BPM are 11025 samples: 300 blocks cover ~14 steps,
    // about 8 of them Euclidean rests.
    for (int block = 0; block < 300; ++block) {
        f.process();
        if (f.output.last(Ruinae::kArpSkipLane0Id).has_value()) ++skipBlocks;
    }

    INFO("notify() calls made from inside process(): " << f.peer.notifiesInsideProcess);
    INFO("blocks that published a skip: " << skipBlocks);
    // Non-vacuous: the pattern skipped, and the skip reached the controller path.
    REQUIRE(skipBlocks > 0);
    REQUIRE(f.peer.notifiesInsideProcess == 0);
}

TEST_CASE("Skip highlight uses each lane's own step and covers all eight lanes", "[vst][arp]") {
    SkipFixture f;
    f.configureArp();
    f.events.addNoteOn(60, 0.8f);

    // The playheads publish each lane's step after the block's step has
    // advanced it, so the previous block's playhead is the step the lane was on
    // when this block's step fired (one step per block at this rate).
    std::array<int, 8> prevPlayhead{-1, -1, -1, -1, -1, -1, -1, -1};
    int skipsChecked = 0;
    int skipsWhereGateDiffersFromVelocity = 0;
    int skipsWhereRatchetDiffersFromModifier = 0;

    for (int block = 0; block < 300; ++block) {
        f.process();

        std::array<std::optional<double>, 8> skip{};
        for (size_t lane = 0; lane < 8; ++lane) skip[lane] = f.output.last(kSkipIds[lane]);

        if (skip[0].has_value() && prevPlayhead[0] >= 0) {
            ++skipsChecked;
            for (size_t lane = 0; lane < 8; ++lane) {
                INFO("block " << block << ", lane " << lane);
                REQUIRE(skip[lane].has_value());
                const auto step = decodeSkipStep(*skip[lane]);
                REQUIRE(step.has_value());
                CHECK(*step == prevPlayhead[lane]);
            }
            const auto velStep = decodeSkipStep(*skip[0]);
            const auto gateStep = decodeSkipStep(*skip[1]);
            if (velStep && gateStep && *velStep != *gateStep)
                ++skipsWhereGateDiffersFromVelocity;
            const auto ratchetStep = decodeSkipStep(*skip[3]);
            const auto modifierStep = decodeSkipStep(*skip[4]);
            if (ratchetStep && modifierStep && *ratchetStep != *modifierStep)
                ++skipsWhereRatchetDiffersFromModifier;
        }

        for (size_t lane = 0; lane < 8; ++lane) {
            if (const auto v = f.output.last(kPlayheadIds[lane])) prevPlayhead[lane] = decodePlayhead(*v);
        }
    }

    INFO("skips checked: " << skipsChecked
         << ", gate step != velocity step: " << skipsWhereGateDiffersFromVelocity
         << ", ratchet step != modifier step: " << skipsWhereRatchetDiffersFromModifier);
    REQUIRE(skipsChecked > 0);
    // Velocity length 4 and gate length 3 (ratchet 7, modifier 6) put the lanes
    // of each pair on different steps for some skips; at least one must show it,
    // or the case proves nothing.
    REQUIRE(skipsWhereGateDiffersFromVelocity > 0);
    REQUIRE(skipsWhereRatchetDiffersFromModifier > 0);
}
