// ==============================================================================
// Vorago Phase 13 - the ecosystem-frame producer (integration)
// ==============================================================================
// Reference: specs/vorago-phase13-ui/spec.md  (C-2, FR-020 - FR-027, SC-001,
//            SC-006 - SC-011), plan.md sections 4.1 - 4.4 and 9.2, tasks.md T005.
//
// CASES OWNED BY THIS TU (tag [vorago][integration][ecosystem]):
//   Vorago_EcosystemFrame_DoesNotChangeAudio   SC-001  in-binary A/B, seam forced vs off
//   Vorago_EcosystemFrame_MatchesEngine        SC-006  every field == the live getter
//   Vorago_EcosystemFrame_LinksAreStrongest    SC-007 (a) product-level link set
//   Vorago_EcosystemFrame_Cadence              SC-008  ENABLED seam (plan D-2)
//   Vorago_EcosystemFrame_FocusVoice           SC-009  rules (a)/(b)/(c)
//   Vorago_EcosystemFrame_Determinism          SC-010  same-build, field by field
//   Vorago_EcosystemFrame_AllocationFree       SC-011  seam path + connected path
//   Vorago_EcosystemFrame_HandlerLifecycle     FR-021, FR-026, C-2 clauses 1, 2(iii), 7
//
// Every frame read goes through lastPublishedFrameForTest() BETWEEN process()
// calls. ProcessorFixture initializes with a null host context
// (vorago_test_fixture.h:186), so it can never open a DataExchange queue; the two
// connected cases build ConnectedFixture below (Innexus PipelineFixture order,
// plugins/innexus/tests/integration/test_data_exchange_pipeline.cpp:108-163):
// HostApplication does not implement IDataExchangeHandler, so the SDK's IMessage
// fallback is used, which is deterministic WITHOUT a message pump - openQueue
// notifies "DataExchangeQueueOpened" synchronously and an unpumped queue accepts
// exactly numBlocks = 4 sends (dataexchange.cpp:77-105, :137-172).
//
// PLATFORM CAVEAT (plan 4.4): Timer::create returns nullptr on Linux without an
// injected factory (base/source/timer.cpp:339-353), so the fallback queue never
// opens there. Queue-dependent asserts are WARN-skipped on SMTG_OS_LINUX only; on
// Windows / macOS a closed queue is a REQUIRE failure.
//
// NO bit-exact float golden: every float compare is a live-value compare of two
// reads of the same DSP state, or an A/B of two instances in this same binary.
// NO std::isnan: finiteness is Krate::DSP::detail::isFinite (bit pattern).
// NAMESPACE HAZARD (vorago_test_fixture.h banner): plugin types are spelled
// ::Vorago::..., and Krate::DSP::TestUtils is never opened here.
// ==============================================================================

#include "controller/controller.h"
#include "parameters/envelope_params.h"
#include "parameters/param_mapping.h"
#include "plugin_ids.h"
#include "processor/ecosystem_frame.h"
#include "processor/ecosystem_frame_builder.h"
#include "processor/processor.h"
#include "vorago_test_fixture.h"

#include <allocation_detector.h>
#include <vst_event_list.h>

#include "pluginterfaces/base/fplatform.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/ecosystem_engine.h>
#include <krate/dsp/systems/voice_allocator.h>
#include <krate/dsp/systems/vorago_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::EcosystemEngine;
using Krate::DSP::VoiceState;
using Krate::DSP::VoragoEngine;
using ::Vorago::EcosystemFrame;

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr Steinberg::int32 kMaxBlock = 2048;
constexpr float kVelocity = 0.8f;
constexpr std::uint32_t kVeco = 0x5645434Fu;  // 'VECO', spec C-2
constexpr float kSilenceLevel = 1.0e-4f;       // C-2 clause 4 (b), Seraphis's value
constexpr std::size_t kMaxVoices = VoragoEngine::kMaxVoices;

constexpr std::size_t samplesFor(double seconds) {
    return static_cast<std::size_t>(seconds * kSampleRate);
}

// kPolyphonyId: 0-1 normalized -> 1-6 (global_params.h:70-73), so n voices -> (n-1)/5.
constexpr double polyphonyNorm(int voices) {
    return static_cast<double>(voices - 1) / 5.0;
}

// kEnvelopeReleaseId plain 200 ms, through the pack's own mapping
// (envelope_params.h: envelopeTimeToNormalized, used by registration at :163-165).
double shortReleaseNorm() {
    return ::Vorago::detail::envelopeTimeToNormalized(200.0);
}

#if SMTG_OS_LINUX
constexpr bool kQueueMayBeUnavailable = true;
#else
constexpr bool kQueueMayBeUnavailable = false;
#endif

/// Queue-open precondition (plan 4.4). Returns true when the queue-dependent
/// asserts may run. Linux: WARN and skip; elsewhere a closed queue fails.
bool queueAvailable(bool opened, const char* step) {
    if (opened) {
        return true;
    }
    if constexpr (kQueueMayBeUnavailable) {
        WARN("DataExchange fallback queue did not open at " << step
                                                             << " (Linux: no injected timer "
                                                                "factory); queue-dependent "
                                                                "asserts skipped");
        return false;
    } else {
        INFO("DataExchange fallback queue did not open at " << step);
        REQUIRE(opened);
        return false;
    }
}

// -----------------------------------------------------------------------------
// Rig - a prepared ProcessorFixture plus a reusable event list and parameter
// container. Every block clears the capture (capacity kept), so any block size
// up to kMaxBlock can be issued repeatedly.
// -----------------------------------------------------------------------------
struct Rig {
    VoragoTest::ProcessorFixture fx;
    Krate::Test::EventList ev;
    VoragoTest::MultiParamChanges pc;
    std::size_t renderedSamples = 0;

    Rig() {
        fx.prepare(kSampleRate, kMaxBlock);
        fx.reserveCapture(static_cast<std::size_t>(kMaxBlock));
        pc.reserve(16);
    }

    [[nodiscard]] ::Vorago::Processor& proc() const { return *fx.proc; }

    [[nodiscard]] const VoragoEngine& engine() const {
        REQUIRE(fx.proc->engineForTest() != nullptr);
        return *fx.proc->engineForTest();
    }

    [[nodiscard]] const EcosystemFrame& frame() const {
        return fx.proc->lastPublishedFrameForTest();
    }

    void noteOn(Steinberg::int16 pitch, Steinberg::int32 offset = 0) {
        ev.addNoteOn(pitch, kVelocity, offset);
    }
    void noteOff(Steinberg::int16 pitch, Steinberg::int32 offset = 0) {
        ev.addNoteOff(pitch, offset);
    }
    void param(Steinberg::Vst::ParamID id, double normalized, Steinberg::int32 offset = 0) {
        pc.addQueue(id).addTestPoint(offset, normalized);
    }

    void block(std::size_t n = kBlock) {
        fx.capturedL.clear();  // keeps capacity
        fx.capturedR.clear();
        REQUIRE(fx.processBlock(n, &ev, &pc) == Steinberg::kResultOk);
        ev.clear();
        pc.clear();
        renderedSamples += n;
    }

    [[nodiscard]] std::uint64_t processCalls() const {
        return fx.proc->ecosystemFrameProcessCallCountForTest();
    }
    [[nodiscard]] std::uint64_t attempts() const {
        return fx.proc->ecosystemFramePublishAttemptCountForTest();
    }
    [[nodiscard]] char rule() const { return fx.proc->ecosystemFocusRuleForTest(); }

    /// The simulation-step count of the frame's focus voice (the trigger's input).
    [[nodiscard]] std::uint64_t focusStep() const {
        return engine()
            .getVoice(static_cast<std::size_t>(frame().focusVoice))
            .ecosystem()
            .getControlStepCount();
    }

    [[nodiscard]] std::array<VoiceState, kMaxVoices> states() const {
        std::array<VoiceState, kMaxVoices> s{};
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            s[v] = engine().getVoiceState(v);
        }
        return s;
    }

    [[nodiscard]] bool allIdle() const {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (engine().getVoiceState(v) != VoiceState::Idle) {
                return false;
            }
        }
        return true;
    }

    /// One note-on in its own block; returns the slot that went from Idle to
    /// non-Idle (exactly one must).
    std::size_t playNote(Steinberg::int16 pitch) {
        const auto before = states();
        noteOn(pitch);
        block();
        const auto after = states();
        std::size_t found = kMaxVoices;
        std::size_t count = 0;
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (before[v] == VoiceState::Idle && after[v] != VoiceState::Idle) {
                found = v;
                ++count;
            }
        }
        INFO("note " << pitch);
        REQUIRE(count == 1u);
        return found;
    }

    /// Render until every listed slot's level is above kSilenceLevel. A voice
    /// released (or force-idled) before its level ever rose above the tail
    /// threshold is already finished (vorago_voice.h: clearRunState seeds the
    /// quiescence counter at the retire value and only an audible chunk resets
    /// it), so it retires on the very next block and never runs a release tail
    /// or an orphan tail. The SC-009 rows need the real tail, so notes are held
    /// until they sound before anything is released or shrunk.
    void holdUntilAudible(std::initializer_list<std::size_t> slots) {
        const std::size_t start = renderedSamples;
        for (const std::size_t v : slots) {
            INFO("slot " << v);
            while (engine().getVoiceLevel(v) <= kSilenceLevel) {
                REQUIRE(renderedSamples - start <= static_cast<std::size_t>(kSampleRate * 20.0));
                block();
            }
        }
    }
};

// -----------------------------------------------------------------------------
// ProcessData for the six early-return shapes (processor.cpp:256-286).
// -----------------------------------------------------------------------------
enum class Shape : std::uint8_t { NullChannelBuffers, OneChannel, ZeroSamples, NullOutL };

Steinberg::tresult callShape(VoragoTest::ProcessorFixture& fx, Shape shape) {
    std::array<float*, 2> channels{fx.audioL(), fx.audioR()};

    Steinberg::Vst::AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.silenceFlags = 0;
    bus.channelBuffers32 = channels.data();

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kBlock);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;
    data.processContext = nullptr;

    switch (shape) {
        case Shape::NullChannelBuffers: bus.channelBuffers32 = nullptr; break;
        case Shape::OneChannel: bus.numChannels = 1; break;
        case Shape::ZeroSamples: data.numSamples = 0; break;
        case Shape::NullOutL: channels[0] = nullptr; break;
    }
    return fx.proc->process(data);
}

// -----------------------------------------------------------------------------
// Frame checks
// -----------------------------------------------------------------------------

/// SC-006: every field of `f` against the live getters of its focus voice.
bool frameMatchesEngine(const EcosystemFrame& f, const VoragoEngine& eng, std::string& why) {
    std::ostringstream os;
    const auto v = static_cast<std::size_t>(f.focusVoice);
    const EcosystemEngine& eco = eng.getVoice(v).ecosystem();
    const auto n = static_cast<std::size_t>(f.agentCount);

    if (n != eco.getAgentCount()) {
        os << "agentCount " << n << " != " << eco.getAgentCount();
        why = os.str();
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const bool ok =
            f.agentX[i] == ::Vorago::sanitizeFrameFloat(eco.getAgentPositionX(i))
            && f.agentY[i] == ::Vorago::sanitizeFrameFloat(eco.getAgentPositionY(i))
            && f.agentGlow[i]
                   == ::Vorago::ecosystemEnergyGlow(eco.getAgentEnergy(i), eco.getAgentCount(),
                                                    eco.getEnergyBudget())
            && f.agentKind[i] == static_cast<std::uint8_t>(eco.getAgentKind(i))
            && f.agentDormant[i] == (eco.isAgentDormant(i) ? 1 : 0);
        if (!ok) {
            os << "agent " << i << " differs from its getters";
            why = os.str();
            return false;
        }
    }
    for (std::size_t i = n; i < ::Vorago::kMaxFrameAgents; ++i) {
        if (f.agentX[i] != 0.0f || f.agentY[i] != 0.0f || f.agentGlow[i] != 0.0f
            || f.agentKind[i] != 0 || f.agentDormant[i] != 0) {
            os << "agent slot " << i << " >= agentCount is not zero";
            why = os.str();
            return false;
        }
    }
    for (auto l = static_cast<std::size_t>(f.linkCount); l < ::Vorago::kMaxFrameLinks; ++l) {
        if (f.linkA[l] != 0 || f.linkB[l] != 0 || f.linkStrength[l] != 0.0f) {
            os << "link slot " << l << " >= linkCount is not zero";
            why = os.str();
            return false;
        }
    }
    if (f.voiceLevel != ::Vorago::sanitizeFrameFloat(static_cast<double>(eng.getVoiceLevel(v)))) {
        why = "voiceLevel differs from getVoiceLevel(focus)";
        return false;
    }
    if (static_cast<std::size_t>(f.activeVoices) != eng.getActiveVoiceCount()) {
        os << "activeVoices " << static_cast<int>(f.activeVoices)
           << " != " << eng.getActiveVoiceCount();
        why = os.str();
        return false;
    }
    const float expectedScale =
        (n > 0) ? ::Vorago::sanitizeFrameFloat(eco.getEnergyBudget() / static_cast<double>(n))
                : 0.0f;
    if (f.linkFlowScale != expectedScale) {
        why = "linkFlowScale differs from getEnergyBudget() / agentCount";
        return false;
    }
    return true;
}

/// SC-007 (a): the carried links against the reference built from `eco`.
bool linksAreStrongest(const EcosystemFrame& f, const EcosystemEngine& eco, std::string& why) {
    std::ostringstream os;
    struct RefPair {
        std::size_t a;
        std::size_t b;
        double absFlow;
    };
    const std::size_t pairCount = eco.getPairInteractionCount();
    std::vector<RefPair> ref;
    ref.reserve(pairCount);
    for (std::size_t p = 0; p < pairCount; ++p) {
        const double flow = eco.getPairFlow(p);
        if (flow == 0.0) {
            continue;
        }
        ref.push_back(RefPair{.a = eco.getPairAgentA(p), .b = eco.getPairAgentB(p), .absFlow = std::fabs(flow)});
    }
    std::sort(ref.begin(), ref.end(),
              [](const RefPair& x, const RefPair& y) { return x.absFlow > y.absFlow; });
    const std::size_t k = std::min(::Vorago::kMaxFrameLinks, ref.size());

    if (static_cast<std::size_t>(f.linkCount) != k) {
        os << "linkCount " << static_cast<int>(f.linkCount) << " != reference " << k;
        why = os.str();
        return false;
    }

    std::vector<float> expected;
    expected.reserve(k);
    for (std::size_t l = 0; l < k; ++l) {
        expected.push_back(static_cast<float>(ref[l].absFlow));
    }
    std::vector<float> carried(std::begin(f.linkStrength), std::begin(f.linkStrength) + k);
    std::sort(expected.begin(), expected.end());
    std::sort(carried.begin(), carried.end());
    if (expected != carried) {
        why = "multiset of linkStrength != multiset of the strongest |flow|";
        return false;
    }

    const auto agentCount = static_cast<std::size_t>(f.agentCount);
    for (std::size_t l = 0; l < k; ++l) {
        const auto a = static_cast<std::size_t>(f.linkA[l]);
        const auto b = static_cast<std::size_t>(f.linkB[l]);
        if (a >= agentCount || b >= agentCount || a == b) {
            os << "link " << l << " (" << a << ", " << b << ") invalid for agentCount "
               << agentCount;
            why = os.str();
            return false;
        }
        bool recorded = false;
        for (std::size_t p = 0; p < pairCount && !recorded; ++p) {
            recorded = eco.getPairAgentA(p) == a && eco.getPairAgentB(p) == b
                       && static_cast<float>(std::fabs(eco.getPairFlow(p))) == f.linkStrength[l];
        }
        if (!recorded) {
            os << "link " << l << " (" << a << ", " << b << ") is not a recorded pair with that "
               << "strength";
            why = os.str();
            return false;
        }
    }
    return true;
}

bool sameFrame(const EcosystemFrame& x, const EcosystemFrame& y) {
    const auto eq = [](const auto& p, const auto& q) {
        return std::equal(std::begin(p), std::end(p), std::begin(q), std::end(q));
    };
    return x.sequence == y.sequence && x.activeVoices == y.activeVoices
           && x.focusVoice == y.focusVoice && x.agentCount == y.agentCount
           && x.linkCount == y.linkCount && x.voiceLevel == y.voiceLevel
           && eq(x.agentX, y.agentX) && eq(x.agentY, y.agentY) && eq(x.agentGlow, y.agentGlow)
           && eq(x.agentKind, y.agentKind) && eq(x.agentDormant, y.agentDormant)
           && eq(x.linkA, y.linkA) && eq(x.linkB, y.linkB)
           && eq(x.linkStrength, y.linkStrength) && x.linkFlowScale == y.linkFlowScale;
}

bool frameFloatsFinite(const EcosystemFrame& f) {
    const auto finite = [](const auto& arr) {
        return std::all_of(std::begin(arr), std::end(arr),
                           [](float x) { return Krate::DSP::detail::isFinite(x); });
    };
    return Krate::DSP::detail::isFinite(f.voiceLevel)
           && Krate::DSP::detail::isFinite(f.linkFlowScale) && finite(f.agentX)
           && finite(f.agentY) && finite(f.agentGlow) && finite(f.linkStrength);
}

/// SC-006 non-vacuity: two agents with share >= 2, differing energies and
/// differing glow. `glowOf(i)` returns the glow under test for agent i.
template <typename GlowOf>
bool hasShareSpread(const EcosystemEngine& eco, std::size_t agentCount, GlowOf glowOf) {
    const double budget = eco.getEnergyBudget();
    if (!(budget > 0.0) || agentCount < 2) {
        return false;
    }
    std::array<std::size_t, EcosystemEngine::kMaxAgents> high{};
    std::size_t numHigh = 0;
    for (std::size_t i = 0; i < agentCount; ++i) {
        const double share = eco.getAgentEnergy(i) * static_cast<double>(agentCount) / budget;
        if (share >= 2.0) {
            high[numHigh++] = i;
        }
    }
    for (std::size_t x = 0; x < numHigh; ++x) {
        for (std::size_t y = x + 1; y < numHigh; ++y) {
            const std::size_t i = high[x];
            const std::size_t j = high[y];
            if (eco.getAgentEnergy(i) != eco.getAgentEnergy(j) && glowOf(i) != glowOf(j)) {
                return true;
            }
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// RecordingPeer - a test-local IConnectionPoint. Copies the message ID and the
// "UserContextID" / "BlockSize" ints out inside notify() (the message is
// released after the call), then optionally forwards to a real peer. The
// attribute keys are the SDK's own (dataexchange.cpp:44-49).
// -----------------------------------------------------------------------------
class RecordingPeer final : public Steinberg::Vst::IConnectionPoint {
public:
    struct Record {
        std::string id;
        bool hasUserContextId = false;
        std::int64_t userContextId = 0;
        bool hasBlockSize = false;
        std::int64_t blockSize = 0;
    };

    RecordingPeer() { records.reserve(32); }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** obj) override {
        if (obj != nullptr) {
            *obj = nullptr;
        }
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* /*other*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API disconnect(
        Steinberg::Vst::IConnectionPoint* /*other*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override {
        Record r{};
        if (message != nullptr) {
            const char* id = message->getMessageID();
            r.id = (id != nullptr) ? id : "";
            if (Steinberg::Vst::IAttributeList* attr = message->getAttributes()) {
                Steinberg::int64 value = 0;
                if (attr->getInt("UserContextID", value) == Steinberg::kResultTrue) {
                    r.hasUserContextId = true;
                    r.userContextId = value;
                }
                value = 0;
                if (attr->getInt("BlockSize", value) == Steinberg::kResultTrue) {
                    r.hasBlockSize = true;
                    r.blockSize = value;
                }
            }
        }
        records.push_back(r);
        return (forward != nullptr) ? forward->notify(message) : Steinberg::kResultOk;
    }

    [[nodiscard]] std::size_t count(std::string_view id) const {
        return static_cast<std::size_t>(std::count_if(
            records.begin(), records.end(), [id](const Record& r) { return r.id == id; }));
    }
    [[nodiscard]] const Record* last(std::string_view id) const {
        const auto it = std::find_if(records.rbegin(), records.rend(),
                                     [id](const Record& r) { return r.id == id; });
        return it == records.rend() ? nullptr : &*it;
    }
    void clear() { records.clear(); }

    std::vector<Record> records;
    Steinberg::Vst::IConnectionPoint* forward = nullptr;
};

constexpr std::string_view kQueueOpened = "DataExchangeQueueOpened";
constexpr std::string_view kQueueClosed = "DataExchangeQueueClosed";

// -----------------------------------------------------------------------------
// ConnectedFixture (plan 4.4) - a processor initialized with a REAL host
// context, so connect() + setActive(true) opens the SDK fallback queue. The
// optional controller peer follows the Innexus PipelineFixture order:
// initialize both, connect both, then setupProcessing + setActive(true).
// Members are ordered so the host outlives everything that references it.
// -----------------------------------------------------------------------------
struct ConnectedFixture {
    Steinberg::Vst::HostApplication host;
    RecordingPeer recorder;
    Steinberg::IPtr<::Vorago::Controller> ctrl;
    std::unique_ptr<::Vorago::Processor> proc = std::make_unique<::Vorago::Processor>();
    std::vector<float> bufL, bufR;
    Krate::Test::EventList ev;
    Steinberg::Vst::IConnectionPoint* peer = nullptr;
    bool active = false;
    bool ctrlConnected = false;

    ConnectedFixture()
        : bufL(static_cast<std::size_t>(kMaxBlock), 0.0f),
          bufR(static_cast<std::size_t>(kMaxBlock), 0.0f) {
        REQUIRE(proc->initialize(&host) == Steinberg::kResultOk);
    }

    ~ConnectedFixture() {
        if (active) {
            proc->setActive(0);
        }
        if (peer != nullptr) {
            proc->disconnect(peer);
        }
        if (ctrl) {
            if (ctrlConnected) {
                ctrl->disconnect(procConnection());
            }
            ctrl->terminate();
        }
        proc->terminate();
    }

    ConnectedFixture(const ConnectedFixture&) = delete;
    ConnectedFixture& operator=(const ConnectedFixture&) = delete;

    [[nodiscard]] Steinberg::Vst::IConnectionPoint* procConnection() const {
        return static_cast<Steinberg::Vst::IConnectionPoint*>(
            static_cast<Steinberg::Vst::AudioEffect*>(proc.get()));
    }

    /// A real Vorago::Controller, initialized with the same host. The recorder
    /// tees every processor -> controller message into it.
    void attachController() {
        ctrl = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(ctrl->initialize(&host) == Steinberg::kResultOk);
        recorder.forward = static_cast<Steinberg::Vst::IConnectionPoint*>(
            static_cast<Steinberg::Vst::EditControllerEx1*>(ctrl.get()));
        REQUIRE(ctrl->connect(procConnection()) == Steinberg::kResultTrue);
        ctrlConnected = true;
    }

    void connect(Steinberg::Vst::IConnectionPoint* other) {
        REQUIRE(peer == nullptr);
        REQUIRE(proc->connect(other) == Steinberg::kResultTrue);
        peer = other;
    }

    void disconnect() {
        REQUIRE(peer != nullptr);
        REQUIRE(proc->disconnect(peer) == Steinberg::kResultTrue);
        peer = nullptr;
    }

    void setupProcessing() const {
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.maxSamplesPerBlock = kMaxBlock;
        setup.sampleRate = kSampleRate;
        REQUIRE(proc->setupProcessing(setup) == Steinberg::kResultOk);
    }

    void setActive(bool on) {
        REQUIRE(proc->setActive(on ? 1 : 0) == Steinberg::kResultOk);
        active = on;
    }

    /// One stereo process() call. No REQUIRE and no allocation: callable inside
    /// an AllocationScope. The caller fills / clears `ev` outside any scope.
    Steinberg::tresult processBlock(std::size_t n) noexcept {
        std::array<float*, 2> channels{bufL.data(), bufR.data()};

        Steinberg::Vst::AudioBusBuffers bus{};
        bus.numChannels = 2;
        bus.silenceFlags = 0;
        bus.channelBuffers32 = channels.data();

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(std::min(n, bufL.size()));
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &ev;
        data.outputEvents = nullptr;
        data.processContext = nullptr;
        return proc->process(data);
    }

    [[nodiscard]] std::uint64_t processCalls() const {
        return proc->ecosystemFrameProcessCallCountForTest();
    }
    [[nodiscard]] std::uint64_t attempts() const {
        return proc->ecosystemFramePublishAttemptCountForTest();
    }
    [[nodiscard]] std::uint64_t skipped() const {
        return proc->ecosystemFrameSkippedBlockCountForTest();
    }
};

}  // namespace

// =============================================================================
// SC-001 - the producer is read-only: forced on vs off, same audio.
// =============================================================================
TEST_CASE("Vorago_EcosystemFrame_DoesNotChangeAudio", "[vorago][integration][ecosystem]") {
    constexpr std::size_t kTotal = std::size_t{60} * 48000u;      // 60 s
    constexpr std::size_t kNoteOffAt = std::size_t{45} * 48000u;  // 45 s
    constexpr std::size_t kBlocks = kTotal / kBlock;  // 5625
    static_assert(kBlocks * kBlock == kTotal);

    VoragoTest::ProcessorFixture a;
    VoragoTest::ProcessorFixture b;
    a.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
    b.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
    a.proc->setEcosystemFrameForcedForTest(true);
    b.proc->setEcosystemFrameForcedForTest(false);
    // Seed index 0 is the shipped default (global_params.h:43) and is not touched.
    a.reserveCapture(kTotal);
    b.reserveCapture(kTotal);

    Krate::Test::EventList evA;
    Krate::Test::EventList evB;
    bool sawHabitat = false;

    for (std::size_t blk = 0; blk < kBlocks; ++blk) {
        const std::size_t start = blk * kBlock;
        if (start == 0) {
            evA.addNoteOn(48, kVelocity, 0);
            evB.addNoteOn(48, kVelocity, 0);
        }
        if (kNoteOffAt >= start && kNoteOffAt < start + kBlock) {
            const auto off = static_cast<Steinberg::int32>(kNoteOffAt - start);
            evA.addNoteOff(48, off);
            evB.addNoteOff(48, off);
        }
        REQUIRE(a.processBlock(kBlock, &evA) == Steinberg::kResultOk);
        REQUIRE(b.processBlock(kBlock, &evB) == Steinberg::kResultOk);
        evA.clear();
        evB.clear();

        const EcosystemFrame& f = a.proc->lastPublishedFrameForTest();
        if (f.agentCount > 0 && f.linkCount > 0) {
            sawHabitat = true;
        }
    }

    // Non-vacuity: A published on every call, and something was drawn.
    CHECK(a.proc->ecosystemFrameProcessCallCountForTest() == kBlocks);
    CHECK(a.proc->ecosystemFramePublishAttemptCountForTest() == kBlocks);
    CHECK(sawHabitat);
    // B really was off (gate closed: no seam, no handler).
    CHECK(b.proc->ecosystemFrameProcessCallCountForTest() == 0u);

    REQUIRE(a.capturedL.size() == kTotal);
    REQUIRE(b.capturedL.size() == kTotal);
    REQUIRE(a.capturedR.size() == kTotal);
    REQUIRE(b.capturedR.size() == kTotal);

    std::size_t mismatchL = 0;
    std::size_t mismatchR = 0;
    std::size_t firstMismatch = kTotal;
    for (std::size_t i = 0; i < kTotal; ++i) {
        const bool l = a.capturedL[i] == b.capturedL[i];
        const bool r = a.capturedR[i] == b.capturedR[i];
        mismatchL += l ? 0u : 1u;
        mismatchR += r ? 0u : 1u;
        if ((!l || !r) && firstMismatch == kTotal) {
            firstMismatch = i;
        }
    }
    INFO("first mismatching sample = " << firstMismatch);
    REQUIRE(mismatchL == 0u);
    REQUIRE(mismatchR == 0u);
    // The render was audible, so equality is not two silences.
    REQUIRE(VoragoTest::peakOf(a.capturedL) > 0.0f);
}

// =============================================================================
// SC-006 - the frame equals the live engine state of its focus voice.
// =============================================================================
TEST_CASE("Vorago_EcosystemFrame_MatchesEngine", "[vorago][integration][ecosystem]") {
    constexpr std::size_t kBlocks = samplesFor(20.0) / kBlock;  // 1875

    Rig rig;
    rig.proc().setEcosystemFrameForcedForTest(true);

    std::vector<float> heldLevels;
    heldLevels.reserve(kBlocks);
    std::size_t mismatches = 0;
    std::size_t firstBadBlock = kBlocks;
    std::string firstWhy;
    bool sawShareSpread = false;

    rig.noteOn(48);
    for (std::size_t blk = 0; blk < kBlocks; ++blk) {
        rig.block();
        const EcosystemFrame& f = rig.frame();
        const VoragoEngine& eng = rig.engine();

        std::string why;
        if (!frameMatchesEngine(f, eng, why)) {
            if (mismatches == 0) {
                firstBadBlock = blk;
                firstWhy = why;
            }
            ++mismatches;
        }

        const EcosystemEngine& eco = eng.getVoice(static_cast<std::size_t>(f.focusVoice)).ecosystem();
        const auto n = static_cast<std::size_t>(f.agentCount);
        if (!sawShareSpread
            && hasShareSpread(eco, n, [&f](std::size_t i) { return f.agentGlow[i]; })) {
            sawShareSpread = true;
        }
        heldLevels.push_back(f.voiceLevel);
    }

    INFO("first mismatching block " << firstBadBlock << ": " << firstWhy);
    REQUIRE(mismatches == 0u);

    // OQ-3 evidence: the held voiceLevel range.
    std::vector<float> sorted = heldLevels;
    std::sort(sorted.begin(), sorted.end());
    WARN("SC-006 held voiceLevel over " << sorted.size() << " blocks: min " << sorted.front()
                                        << ", median " << sorted[sorted.size() / 2] << ", max "
                                        << sorted.back());

    if (!sawShareSpread) {
        // Never dropped: the arm moves to a directly prepared engine.
        WARN("SC-006 non-vacuity: the 20 s render never had two agents with share >= 2 and "
             "differing glow; repeating the arm on a directly prepared EcosystemEngine "
             "(48 agents, setExchangeRate(3.0f))");
        auto eng = std::make_unique<EcosystemEngine>();
        eng->setSeed(0x5EED1234u);
        eng->prepare(kSampleRate, EcosystemEngine::PrepareConfig{.agentCount = 48});
        eng->setExchangeRate(3.0f);
        const std::size_t stepSamples =
            eng->getStepIntervalChunks() * EcosystemEngine::kControlChunkSamples;
        bool found = false;
        for (std::size_t step = 0; step < 4000 && !found; ++step) {
            eng->processChunk(stepSamples);
            const std::size_t n = eng->getAgentCount();
            const double budget = eng->getEnergyBudget();
            found = hasShareSpread(*eng, n, [&eng, n, budget](std::size_t i) {
                return ::Vorago::ecosystemEnergyGlow(eng->getAgentEnergy(i), n, budget);
            });
        }
        REQUIRE(found);
    }
}

// =============================================================================
// SC-007 (a) - the carried links are the strongest set of the focus voice.
// =============================================================================
TEST_CASE("Vorago_EcosystemFrame_LinksAreStrongest", "[vorago][integration][ecosystem]") {
    constexpr std::size_t kBlocks = samplesFor(20.0) / kBlock;

    Rig rig;
    rig.proc().setEcosystemFrameForcedForTest(true);

    std::size_t mismatches = 0;
    std::size_t firstBadBlock = kBlocks;
    std::string firstWhy;
    bool sawLinks = false;

    rig.noteOn(48);
    for (std::size_t blk = 0; blk < kBlocks; ++blk) {
        rig.block();
        const EcosystemFrame& f = rig.frame();
        const EcosystemEngine& eco =
            rig.engine().getVoice(static_cast<std::size_t>(f.focusVoice)).ecosystem();

        std::string why;
        if (!linksAreStrongest(f, eco, why)) {
            if (mismatches == 0) {
                firstBadBlock = blk;
                firstWhy = why;
            }
            ++mismatches;
        }
        sawLinks = sawLinks || f.linkCount > 0;
    }

    INFO("first mismatching block " << firstBadBlock << ": " << firstWhy);
    REQUIRE(mismatches == 0u);
    REQUIRE(sawLinks);
}

// =============================================================================
// SC-008 - cadence tracks the habitat, not the callback. ENABLED seam (plan
// D-2): the gate is open, the natural trigger stays in place.
// =============================================================================
TEST_CASE("Vorago_EcosystemFrame_Cadence", "[vorago][integration][ecosystem]") {
    SECTION("(1) process-call counter: +1 per slice-loop call, +0 per early return") {
        Rig rig;
        rig.proc().setEcosystemFrameEnabledForTest(true);

        // 0 events.
        std::uint64_t before = rig.processCalls();
        rig.block(kBlock);
        CHECK(rig.processCalls() == before + 1u);

        // 1 event.
        before = rig.processCalls();
        rig.noteOn(48);
        rig.block(kBlock);
        CHECK(rig.processCalls() == before + 1u);

        // 1024 events (kMaxEventsPerBlock, processor.h:59).
        before = rig.processCalls();
        for (Steinberg::int32 k = 0; k < 512; ++k) {
            rig.noteOn(72, k);
            rig.noteOff(72, k);
        }
        rig.block(kBlock);
        CHECK(rig.processCalls() == before + 1u);

        // A 2048-sample block.
        before = rig.processCalls();
        rig.block(2048);
        CHECK(rig.processCalls() == before + 1u);

        // The early-return shapes: +0 each.
        before = rig.processCalls();
        REQUIRE(rig.fx.processNoOutputs(nullptr) == Steinberg::kResultOk);
        CHECK(rig.processCalls() == before);
        for (const Shape s :
             {Shape::NullChannelBuffers, Shape::OneChannel, Shape::ZeroSamples, Shape::NullOutL}) {
            INFO("shape " << static_cast<int>(s));
            before = rig.processCalls();
            REQUIRE(callShape(rig.fx, s) == Steinberg::kResultOk);
            CHECK(rig.processCalls() == before);
        }

        // Unprepared fixture (the not-ready return).
        VoragoTest::ProcessorFixture unprepared;
        unprepared.proc->setEcosystemFrameEnabledForTest(true);
        unprepared.reserveCapture(kBlock);
        REQUIRE(unprepared.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(unprepared.proc->ecosystemFrameProcessCallCountForTest() == 0u);
        CHECK(unprepared.proc->ecosystemFramePublishAttemptCountForTest() == 0u);
    }

    SECTION("(2)-(6) publish attempts follow the step, focus and agentCount") {
        Rig rig;
        rig.proc().setEcosystemFrameEnabledForTest(true);

        // Held note, then 512 * k samples so the render is well past the onset.
        rig.noteOn(48);
        rig.block(kBlock);
        for (int k = 0; k < 8; ++k) {
            rig.block(kBlock);
        }

        // (2) 16 x 32-sample calls = one simulation step's worth -> exactly 1.
        {
            const std::uint64_t a0 = rig.attempts();
            const std::uint64_t s0 = rig.focusStep();
            for (int c = 0; c < 16; ++c) {
                rig.block(32);
            }
            INFO("focus step advanced by " << (rig.focusStep() - s0));
            REQUIRE(rig.focusStep() == s0 + 1u);  // precondition: exactly one step
            CHECK(rig.attempts() == a0 + 1u);
        }

        // (3) one 2048-sample call (several steps) -> exactly 1.
        {
            const std::uint64_t a0 = rig.attempts();
            const std::uint64_t s0 = rig.focusStep();
            rig.block(2048);
            REQUIRE(rig.focusStep() >= s0 + 2u);  // precondition: more than one step
            CHECK(rig.attempts() == a0 + 1u);
        }

        // (4) seven 32-sample calls inside one step -> +0; the crossing call -> +1.
        {
            // Land just past a boundary: 32-sample calls until the step advances.
            int guard = 0;
            std::uint64_t s = rig.focusStep();
            while (rig.focusStep() == s) {
                REQUIRE(++guard <= 17);
                rig.block(32);
            }
            s = rig.focusStep();
            const std::uint64_t a0 = rig.attempts();
            for (int c = 0; c < 7; ++c) {
                rig.block(32);
                REQUIRE(rig.focusStep() == s);  // precondition: no advance
                CHECK(rig.attempts() == a0);
            }
            guard = 0;
            while (rig.focusStep() == s) {
                REQUIRE(++guard <= 17);
                const std::uint64_t before = rig.attempts();
                rig.block(32);
                if (rig.focusStep() == s) {
                    CHECK(rig.attempts() == before);
                } else {
                    CHECK(rig.attempts() == before + 1u);  // the crossing call
                }
            }
        }

        // (5) notes on slots 0-3 at polyphony 4, then a mid-step shrink to "1".
        {
            rig.param(::Vorago::kPolyphonyId, polyphonyNorm(4));
            rig.block(kBlock);
            (void)rig.playNote(55);
            (void)rig.playNote(60);
            (void)rig.playNote(67);
            for (std::size_t v = 0; v < 4; ++v) {
                INFO("slot " << v);
                REQUIRE(rig.engine().getVoiceState(v) != VoiceState::Idle);
            }

            // Just past a boundary, so the next 32 samples cannot advance a step.
            int guard = 0;
            const std::uint64_t s = rig.focusStep();
            while (rig.focusStep() == s) {
                REQUIRE(++guard <= 17);
                rig.block(32);
            }
            const std::uint8_t focusBefore = rig.frame().focusVoice;
            const std::uint64_t a0 = rig.attempts();
            rig.param(::Vorago::kPolyphonyId, polyphonyNorm(1));
            rig.block(32);
            CHECK(rig.attempts() == a0 + 1u);
            CHECK(rig.frame().focusVoice != focusBefore);
        }

        // (6) gate closed (seam off, no handler) -> both counters +0.
        {
            rig.proc().setEcosystemFrameEnabledForTest(false);
            const std::uint64_t p0 = rig.processCalls();
            const std::uint64_t a0 = rig.attempts();
            for (int c = 0; c < 100; ++c) {
                rig.block(32);
            }
            CHECK(rig.processCalls() == p0);
            CHECK(rig.attempts() == a0);
        }
    }
}

// =============================================================================
// SC-009 - the focus-voice rule, all three clauses. Every section first sets a
// 200 ms release so retirement (release + the 10 s quiescence counter) fits the
// 60 s bound (risk R-4: a longer time-to-Idle is reported, never accommodated).
// =============================================================================
TEST_CASE("Vorago_EcosystemFrame_FocusVoice", "[vorago][integration][ecosystem]") {
    constexpr double kBoundSeconds = 60.0;

    Rig rig;
    rig.proc().setEcosystemFrameForcedForTest(true);
    rig.param(::Vorago::kEnvelopeReleaseId, shortReleaseNorm());
    rig.block(kBlock);

    const auto secondsSince = [&rig](std::size_t startSample) {
        return static_cast<double>(rig.renderedSamples - startSample) / kSampleRate;
    };

    SECTION("one note -> its slot, rule (a)") {
        const std::size_t slot = rig.playNote(48);
        CHECK(static_cast<std::size_t>(rig.frame().focusVoice) == slot);
        CHECK(rig.rule() == 'a');
    }

    SECTION("two notes -> the later slot, rule (a)") {
        const std::size_t first = rig.playNote(48);
        const std::size_t second = rig.playNote(55);
        REQUIRE(first != second);
        CHECK(static_cast<std::size_t>(rig.frame().focusVoice) == second);
        CHECK(rig.rule() == 'a');
    }

    SECTION("release the later of two -> the earlier slot, rule (a)") {
        const std::size_t first = rig.playNote(48);
        const std::size_t second = rig.playNote(55);
        REQUIRE(first != second);
        rig.holdUntilAudible({first, second});

        rig.noteOff(55);
        const std::size_t start = rig.renderedSamples;
        bool sawEarlier = false;
        while (!sawEarlier) {
            rig.block();
            REQUIRE(secondsSince(start) <= kBoundSeconds);
            REQUIRE(rig.rule() == 'a');
            const auto focus = static_cast<std::size_t>(rig.frame().focusVoice);
            if (rig.engine().getVoiceState(second) != VoiceState::Idle) {
                // Clause 4 (a): the released slot is still non-Idle and holds the
                // greatest serial, so it keeps the focus while it releases.
                CHECK(focus == second);
            } else {
                REQUIRE(rig.engine().getVoiceState(first) == VoiceState::Active);
                CHECK(focus == first);
                sawEarlier = true;
            }
        }
        WARN("SC-009 released later voice reached Idle after " << secondsSince(start) << " s");
    }

    SECTION("all released -> (a) while Releasing, then (c) with agentCount 0, never (b)") {
        const std::size_t first = rig.playNote(48);
        const std::size_t second = rig.playNote(55);
        rig.holdUntilAudible({first, second});
        rig.noteOff(48);
        rig.noteOff(55);

        const std::size_t start = rig.renderedSamples;
        bool reachedIdle = false;
        while (!reachedIdle) {
            rig.block();
            REQUIRE(secondsSince(start) <= kBoundSeconds);
            REQUIRE(rig.rule() != 'b');
            if (!rig.allIdle()) {
                CHECK(rig.rule() == 'a');
                CHECK(rig.engine().getVoiceState(static_cast<std::size_t>(rig.frame().focusVoice))
                      == VoiceState::Releasing);
            } else {
                reachedIdle = true;
            }
        }
        WARN("SC-009 time-to-Idle after release: " << secondsSince(start) << " s");
        CHECK(rig.rule() == 'c');
        CHECK(rig.frame().agentCount == 0);
        CHECK(rig.frame().focusVoice == 0);
    }

    SECTION("shrink 4 -> 1 with slots 0-3 held -> focus 0, rule (a)") {
        rig.param(::Vorago::kPolyphonyId, polyphonyNorm(4));
        rig.block();
        (void)rig.playNote(48);
        (void)rig.playNote(55);
        (void)rig.playNote(60);
        (void)rig.playNote(67);
        for (std::size_t v = 0; v < 4; ++v) {
            INFO("slot " << v);
            REQUIRE(rig.engine().getVoiceState(v) == VoiceState::Active);
        }

        rig.param(::Vorago::kPolyphonyId, polyphonyNorm(1));
        rig.block();
        for (std::size_t v = 1; v < 4; ++v) {
            INFO("slot " << v);
            CHECK(rig.engine().getVoiceState(v) == VoiceState::Idle);
        }
        CHECK(rig.frame().focusVoice == 0);
        CHECK(rig.rule() == 'a');
    }

    SECTION("orphan tail -> focus 3 by rule (b), then (c) with agentCount 0") {
        rig.param(::Vorago::kPolyphonyId, polyphonyNorm(4));
        rig.block();
        constexpr std::array<Steinberg::int16, 4> kNotes{48, 55, 60, 67};
        std::array<std::size_t, 4> slotOf{};
        for (std::size_t k = 0; k < kNotes.size(); ++k) {
            slotOf[k] = rig.playNote(kNotes[k]);
        }
        std::size_t heldIndex = kNotes.size();
        for (std::size_t k = 0; k < kNotes.size(); ++k) {
            if (slotOf[k] == 3u) {
                heldIndex = k;
            }
        }
        REQUIRE(heldIndex < kNotes.size());  // one of the four landed on slot 3
        rig.holdUntilAudible({0, 1, 2, 3});

        for (std::size_t k = 0; k < kNotes.size(); ++k) {
            if (k != heldIndex) {
                rig.noteOff(kNotes[k]);
            }
        }
        const std::size_t releaseStart = rig.renderedSamples;
        const auto slots012Idle = [&rig]() {
            return rig.engine().getVoiceState(0) == VoiceState::Idle
                   && rig.engine().getVoiceState(1) == VoiceState::Idle
                   && rig.engine().getVoiceState(2) == VoiceState::Idle;
        };
        do {
            rig.block();
            REQUIRE(secondsSince(releaseStart) <= kBoundSeconds);
        } while (!slots012Idle());
        WARN("SC-009 orphan row: slots 0-2 reached Idle after " << secondsSince(releaseStart)
                                                                << " s");
        REQUIRE(rig.engine().getVoiceState(3) == VoiceState::Active);
        REQUIRE(rig.frame().focusVoice == 3);
        REQUIRE(rig.rule() == 'a');

        // Shrink: slot 3 is force-idled but keeps rendering its tail (orphan).
        rig.param(::Vorago::kPolyphonyId, polyphonyNorm(1));
        const std::size_t shrinkStart = rig.renderedSamples;
        bool sawB = false;
        bool sawC = false;
        while (!sawC) {
            rig.block();
            REQUIRE(secondsSince(shrinkStart) <= kBoundSeconds);
            REQUIRE(rig.rule() != 'a');  // every slot is Idle to the allocator
            const float level3 = rig.engine().getVoiceLevel(3);
            if (rig.rule() == 'b') {
                CHECK(rig.engine().getVoiceState(3) == VoiceState::Idle);
                CHECK(rig.frame().focusVoice == 3);
                CHECK(level3 > kSilenceLevel);
                sawB = true;
            } else {
                CHECK(level3 <= kSilenceLevel);
                CHECK(rig.frame().agentCount == 0);
                sawC = true;
            }
        }
        WARN("SC-009 orphan tail fell below the silence level after "
             << secondsSince(shrinkStart) << " s");
        CHECK(sawB);
    }
}

// =============================================================================
// SC-010 - same seed + same script -> identical frames; seed 0 vs 1 differs.
// =============================================================================
TEST_CASE("Vorago_EcosystemFrame_Determinism", "[vorago][integration][ecosystem]") {
    constexpr std::size_t kBlocks = 2000;
    constexpr std::size_t kSeedBlocks = 200;

    Rig a;
    Rig b;
    Rig c;  // seed index 1
    a.proc().setEcosystemFrameForcedForTest(true);
    b.proc().setEcosystemFrameForcedForTest(true);
    c.proc().setEcosystemFrameForcedForTest(true);

    a.param(::Vorago::kSeedId, ::Vorago::indexToNormalized(0, ::Vorago::kNumSeeds));
    b.param(::Vorago::kSeedId, ::Vorago::indexToNormalized(0, ::Vorago::kNumSeeds));
    c.param(::Vorago::kSeedId, ::Vorago::indexToNormalized(1, ::Vorago::kNumSeeds));
    for (Rig* r : {&a, &b, &c}) {
        r->noteOn(48);
        r->noteOn(55);
    }

    std::size_t mismatches = 0;
    std::size_t firstBadBlock = kBlocks;
    bool seedDiffers = false;
    bool sawAgents = false;

    for (std::size_t blk = 0; blk < kBlocks; ++blk) {
        a.block();
        b.block();
        const EcosystemFrame& fa = a.frame();
        const EcosystemFrame& fb = b.frame();
        if (!sameFrame(fa, fb)) {
            if (mismatches == 0) {
                firstBadBlock = blk;
            }
            ++mismatches;
        }
        sawAgents = sawAgents || fa.agentCount > 0;

        if (blk < kSeedBlocks) {
            c.block();
            const EcosystemFrame& fc = c.frame();
            if (!std::equal(std::begin(fa.agentX), std::end(fa.agentX), std::begin(fc.agentX),
                            std::end(fc.agentX))) {
                seedDiffers = true;
            }
        }
    }

    INFO("first differing block " << firstBadBlock);
    REQUIRE(mismatches == 0u);
    REQUIRE(sawAgents);
    REQUIRE(seedDiffers);
}

// =============================================================================
// SC-011 - the producer allocates nothing and publishes only finite floats.
// =============================================================================
TEST_CASE("Vorago_EcosystemFrame_AllocationFree", "[vorago][integration][ecosystem]") {
    SECTION("seam path: 2000 random blocks, polyphony 2") {
        constexpr std::size_t kBlocks = 2000;
        constexpr int kMaxNotesPerBlock = 3;

        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, kMaxBlock);
        fx.reserveCapture(kBlock);  // cleared (capacity kept) after every block
        fx.proc->setEcosystemFrameForcedForTest(true);

        std::mt19937 rng{13011u};
        std::uniform_real_distribution<double> valueDist(0.0, 1.0);
        std::uniform_int_distribution<int> offsetDist(0, static_cast<int>(kBlock) - 1);
        std::uniform_int_distribution<int> noteCountDist(0, kMaxNotesPerBlock);
        std::uniform_int_distribution<int> pitchDist(36, 72);
        std::uniform_int_distribution<int> percentDist(0, 99);
        std::uniform_real_distribution<float> velocityDist(0.1f, 1.0f);

        // Host-side containers, capacity reserved up front and filled OUTSIDE the
        // scope (automation_rt_test.cpp pattern).
        VoragoTest::MultiParamChanges pc;
        pc.reserve(8);
        Krate::Test::EventList ev;

        std::size_t totalAllocs = 0;
        std::size_t firstAllocBlock = kBlocks;
        std::size_t firstNonFiniteBlock = kBlocks;
        std::size_t okBlocks = 0;
        std::size_t noteOns = 0;

        for (std::size_t blk = 0; blk < kBlocks; ++blk) {
            // ---- host side, outside the scope ----
            pc.clear();
            ev.clear();
            if (blk == 0) {
                pc.addQueue(::Vorago::kPolyphonyId).addTestPoint(0, polyphonyNorm(2));
            }
            if (percentDist(rng) < 25) {
                pc.addQueue(::Vorago::kSustainPedalId)
                    .addTestPoint(offsetDist(rng), (percentDist(rng) < 50) ? 0.0 : 1.0);
            }
            pc.addQueue(::Vorago::kCloudTiltId).addTestPoint(offsetDist(rng), valueDist(rng));
            pc.addQueue(::Vorago::kCloudSpectralGravityId)
                .addTestPoint(offsetDist(rng), valueDist(rng));
            pc.addQueue(::Vorago::kResonanceAnchorModeId)
                .addTestPoint(offsetDist(rng), valueDist(rng));

            const int n = noteCountDist(rng);
            std::array<int, kMaxNotesPerBlock> offs{};
            for (int k = 0; k < n; ++k) {
                offs[static_cast<std::size_t>(k)] = offsetDist(rng);
            }
            std::sort(offs.begin(), offs.begin() + n);
            for (int k = 0; k < n; ++k) {
                const auto pitch = static_cast<Steinberg::int16>(pitchDist(rng));
                const auto off = static_cast<Steinberg::int32>(offs[static_cast<std::size_t>(k)]);
                if (percentDist(rng) < 60) {
                    ev.addNoteOn(pitch, velocityDist(rng), off);
                    ++noteOns;
                } else {
                    ev.addNoteOff(pitch, off);
                }
            }
            fx.capturedL.clear();
            fx.capturedR.clear();

            // ---- audio side: the scope wraps process() only ----
            std::size_t blockAllocs = 0;
            {
                TestHelpers::AllocationScope scope;
                if (fx.processBlock(kBlock, &ev, &pc) == Steinberg::kResultOk) {
                    ++okBlocks;
                }
                blockAllocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
            }
            if (blockAllocs > 0 && firstAllocBlock == kBlocks) {
                firstAllocBlock = blk;
            }
            totalAllocs += blockAllocs;

            if (!frameFloatsFinite(fx.proc->lastPublishedFrameForTest())
                && firstNonFiniteBlock == kBlocks) {
                firstNonFiniteBlock = blk;
            }
        }

        INFO("allocations=" << totalAllocs << " first allocating block=" << firstAllocBlock
                            << " first non-finite frame block=" << firstNonFiniteBlock
                            << " noteOns=" << noteOns);
        REQUIRE(okBlocks == kBlocks);
        REQUIRE(totalAllocs == 0u);
        REQUIRE(firstNonFiniteBlock == kBlocks);
        // Non-vacuity: the producer ran on every call and the run played notes.
        REQUIRE(fx.proc->ecosystemFramePublishAttemptCountForTest() == kBlocks);
        REQUIRE(noteOns > 500u);
    }

    SECTION("connected path: real handler + real controller, no seam") {
        constexpr std::size_t kScopedBlocks = 65;  // note-on block + 64 held

        ConnectedFixture cf;
        cf.attachController();
        cf.connect(&cf.recorder);  // tees every message into the controller
        // The fallback openQueue allocates here, on the host thread, by design
        // (dataexchange.cpp:77-105) - OUTSIDE any scope.
        cf.setupProcessing();
        cf.setActive(true);

        const bool opened = cf.recorder.count(kQueueOpened) == 1u;
        const std::uint64_t a0 = cf.attempts();
        const std::uint64_t s0 = cf.skipped();

        std::size_t totalAllocs = 0;
        std::size_t firstAllocBlock = kScopedBlocks;
        std::size_t okBlocks = 0;
        cf.ev.addNoteOn(48, kVelocity, 0);
        for (std::size_t blk = 0; blk < kScopedBlocks; ++blk) {
            std::size_t blockAllocs = 0;
            {
                TestHelpers::AllocationScope scope;
                if (cf.processBlock(kBlock) == Steinberg::kResultOk) {
                    ++okBlocks;
                }
                blockAllocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
            }
            cf.ev.clear();  // outside the scope
            if (blockAllocs > 0 && firstAllocBlock == kScopedBlocks) {
                firstAllocBlock = blk;
            }
            totalAllocs += blockAllocs;
        }

        const std::uint64_t attemptsAdvanced = cf.attempts() - a0;
        const std::uint64_t skippedAdvanced = cf.skipped() - s0;
        INFO("allocations=" << totalAllocs << " first allocating block=" << firstAllocBlock
                            << " attempts+" << attemptsAdvanced << " skipped+"
                            << skippedAdvanced);
        REQUIRE(okBlocks == kScopedBlocks);
        REQUIRE(totalAllocs == 0u);
        REQUIRE(attemptsAdvanced >= 64u);
        if (queueAvailable(opened, "AllocationFree connected arm")) {
            // Both the send path (first 4 fills) and the queue-full skip path ran
            // inside a scope.
            REQUIRE(skippedAdvanced >= 60u);
        }
    }
}

// =============================================================================
// FR-021, FR-026 counters, C-2 clause 1, clause 2 trigger (iii), clause 7 - the
// DataExchange handler follows connect / setActive / disconnect. No seam.
// =============================================================================
TEST_CASE("Vorago_EcosystemFrame_HandlerLifecycle", "[vorago][integration][ecosystem]") {
    ConnectedFixture cf;
    cf.setupProcessing();
    cf.setActive(true);

    // (1) Prepared and active but not connected: gate closed.
    {
        const std::uint64_t p0 = cf.processCalls();
        for (int c = 0; c < 10; ++c) {
            REQUIRE(cf.processBlock(kBlock) == Steinberg::kResultOk);
        }
        CHECK(cf.processCalls() == p0);
        CHECK(cf.attempts() == 0u);
    }

    // (2) connect, then activate -> exactly one QueueOpened, VECO, 1072 bytes.
    cf.setActive(false);
    cf.connect(&cf.recorder);
    cf.setActive(true);
    {
        const bool opened = cf.recorder.count(kQueueOpened) >= 1u;
        if (queueAvailable(opened, "step (2)")) {
            REQUIRE(cf.recorder.count(kQueueOpened) == 1u);
            const RecordingPeer::Record* r = cf.recorder.last(kQueueOpened);
            REQUIRE(r != nullptr);
            REQUIRE(r->hasUserContextId);
            CHECK(std::cmp_equal(r->userContextId, kVeco));
            REQUIRE(r->hasBlockSize);
            // sizeof(EcosystemFrame) = 1072, rounded up to the 32-byte alignment the
            // processor requests (processor.cpp connect(): macOS's aligned_alloc
            // returns null for 1072 bytes at alignment 32).
            CHECK(std::cmp_greater_equal(r->blockSize, sizeof(EcosystemFrame)));
            CHECK(r->blockSize == 1088);
        }
    }

    // (3) Trigger (iii): no note (rule 'c', step 0, agentCount 0 - equal to the
    // initial frameLast* values), so only the resync can fire.
    {
        const std::uint64_t a0 = cf.attempts();
        REQUIRE(cf.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(cf.attempts() == a0 + 1u);
        REQUIRE(cf.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(cf.attempts() == a0 + 1u);
    }
    // Resync on reconnect: connect() must re-arm frameResyncPending_.
    cf.setActive(false);
    cf.disconnect();
    cf.connect(&cf.recorder);
    cf.setActive(true);
    const bool reopened = cf.recorder.count(kQueueOpened) >= 2u;
    {
        const std::uint64_t a0 = cf.attempts();
        REQUIRE(cf.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(cf.attempts() == a0 + 1u);
        REQUIRE(cf.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(cf.attempts() == a0 + 1u);
    }
    cf.recorder.clear();
    std::size_t fillsSinceOpen = 1;  // the reconnect resync fill above

    // (4) Held note, one step per 512-sample block: fills 1-4 since the open
    // send, fill 5 and fill 6 are skipped (no pump returns a block).
    {
        const bool queueDependent = queueAvailable(reopened, "step (4)");
        cf.ev.addNoteOn(48, kVelocity, 0);
        int guard = 0;
        while (fillsSinceOpen < 6) {
            REQUIRE(++guard <= 16);
            const std::uint64_t a0 = cf.attempts();
            const std::uint64_t s0 = cf.skipped();
            REQUIRE(cf.processBlock(kBlock) == Steinberg::kResultOk);
            cf.ev.clear();
            REQUIRE(cf.attempts() == a0 + 1u);  // one fill per block
            ++fillsSinceOpen;
            INFO("fill " << fillsSinceOpen << " since the queue opened");
            if (queueDependent) {
                CHECK(cf.skipped() == s0 + ((fillsSinceOpen <= 4) ? 0u : 1u));
            }
        }
    }

    // (5) deactivate -> exactly one QueueClosed with the VECO id.
    cf.setActive(false);
    if (queueAvailable(reopened, "step (5)")) {
        REQUIRE(cf.recorder.count(kQueueClosed) == 1u);
        const RecordingPeer::Record* r = cf.recorder.last(kQueueClosed);
        REQUIRE(r != nullptr);
        REQUIRE(r->hasUserContextId);
        CHECK(std::cmp_equal(r->userContextId, kVeco));
    }

    // (6) disconnect releases the handler: the gate is closed again.
    cf.disconnect();
    cf.setActive(true);
    {
        const std::uint64_t p0 = cf.processCalls();
        const std::uint64_t a0 = cf.attempts();
        const std::uint64_t s0 = cf.skipped();
        cf.ev.addNoteOn(48, kVelocity, 0);
        for (int c = 0; c < 10; ++c) {
            REQUIRE(cf.processBlock(kBlock) == Steinberg::kResultOk);
            cf.ev.clear();
        }
        CHECK(cf.processCalls() == p0);
        CHECK(cf.attempts() == a0);
        CHECK(cf.skipped() == s0);
    }
}
