// =============================================================================
// One preset-load path (fix plan T1.41, R-209)
// =============================================================================
// A preset-browser load used to replay the controller's parsers with performEdit
// only; Processor::setState never ran. Today setState stores only parameters, so
// the audio matched, but any future non-parameter field, reset or version gate
// would have applied on host loads alone. The browser load now hands the whole
// component state to the processor ("LoadPresetState", binary "bytes"), whose
// notify runs setState on it; the controller mirrors the same bytes with
// performEdit for the host's automation, undo and dirty flag.
//
// Every processor is heap-allocated: it holds every mode's DSP, and a stack
// instance risks a stack overflow (Catch2 reports it as "Unknown exception").
// =============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"
#include "processor/processor.h"

#include "preset/preset_info.h"
#include "preset/preset_manager.h"

#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int32 kBlock = 64;

using Bytes = std::vector<char>;

/// A processor brought up as a host does: initialize, setupProcessing, setActive(true).
class HostedProcessor {
public:
    explicit HostedProcessor(FUnknown* host) : proc_(Steinberg::owned(new Iterum::Processor())) {
        REQUIRE(proc_->initialize(host) == kResultOk);
        ProcessSetup setup{.processMode = kRealtime,
                           .symbolicSampleSize = kSample32,
                           .maxSamplesPerBlock = kBlock,
                           .sampleRate = kSampleRate};
        REQUIRE(proc_->setupProcessing(setup) == kResultOk);
        REQUIRE(proc_->setActive(static_cast<TBool>(true)) == kResultOk);
        REQUIRE(data_.prepare(*proc_, kBlock, kSample32));
        data_.numSamples = kBlock;
        context_.sampleRate = kSampleRate;
        context_.tempo = 120.0;
        context_.state = ProcessContext::kTempoValid;
        data_.processContext = &context_;
        data_.inputParameterChanges = &input_;
        data_.outputParameterChanges = &output_;
    }

    ~HostedProcessor() {
        data_.unprepare();
        proc_->setActive(static_cast<TBool>(false));
        proc_->terminate();
    }

    HostedProcessor(const HostedProcessor&) = delete;
    HostedProcessor& operator=(const HostedProcessor&) = delete;
    HostedProcessor(HostedProcessor&&) = delete;
    HostedProcessor& operator=(HostedProcessor&&) = delete;

    void queue(ParamID id, ParamValue value) {
        int32 index = 0;
        if (auto* q = input_.addParameterData(id, index)) {
            int32 point = 0;
            q->addPoint(0, value, point);
        }
    }

    /// One block of silence carrying the queued changes.
    void block() {
        for (int32 bus = 0; bus < data_.numInputs; ++bus) {
            for (int32 ch = 0; ch < data_.inputs[bus].numChannels; ++ch) {
                std::memset(data_.inputs[bus].channelBuffers32[ch], 0, sizeof(float) * kBlock);
            }
        }
        output_.clearQueue();
        REQUIRE(proc_->process(data_) == kResultOk);
        input_.clearQueue();
    }

    void setState(const Bytes& bytes) {
        auto stream = Steinberg::owned(new MemoryStream());
        stream->write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), nullptr);
        stream->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(proc_->setState(stream) == kResultOk);
    }

    [[nodiscard]] Bytes state() {
        auto stream = Steinberg::owned(new MemoryStream());
        REQUIRE(proc_->getState(stream) == kResultOk);
        const auto size = static_cast<size_t>(stream->getSize());
        const char* first = stream->getData();
        return Bytes(first, first + size);
    }

    Iterum::Processor& processor() { return *proc_; }

private:
    IPtr<Iterum::Processor> proc_;
    HostProcessData data_;
    ProcessContext context_{};
    ParameterChanges input_{64};
    ParameterChanges output_{64};
};

/// The host's component handler: records every performEdit, so the test can hand them
/// to the processor as a host would.
class PerformRecorder final : public IComponentHandler {
public:
    std::vector<std::pair<ParamID, ParamValue>> edits;

    tresult PLUGIN_API beginEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        edits.emplace_back(id, value);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 /*flags*/) override { return kResultOk; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }  // owned by the test
    uint32 PLUGIN_API release() override { return 1; }
};

/// Sits between the controller and the processor as the host's message relay: records every
/// message the controller sends (id and its "bytes" attribute), then delivers it.
class ConnectionSpy final : public IConnectionPoint {
public:
    struct Received {
        std::string id;
        Bytes bytes;
    };

    explicit ConnectionSpy(IConnectionPoint* target) : target_(target) {}

    std::vector<Received> received;

    tresult PLUGIN_API connect(IConnectionPoint* /*other*/) override { return kResultOk; }
    tresult PLUGIN_API disconnect(IConnectionPoint* /*other*/) override { return kResultOk; }
    tresult PLUGIN_API notify(IMessage* message) override {
        if (message == nullptr) return kInvalidArgument;
        Received r;
        r.id = message->getMessageID() != nullptr ? message->getMessageID() : "";
        if (auto* attrs = message->getAttributes()) {
            const void* data = nullptr;
            uint32 size = 0;
            if (attrs->getBinary("bytes", data, size) == kResultOk && data != nullptr) {
                const auto* first = static_cast<const char*>(data);
                r.bytes.assign(first, first + size);
            }
        }
        received.push_back(std::move(r));
        return target_->notify(message);
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IConnectionPoint::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IConnectionPoint*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }  // owned by the test
    uint32 PLUGIN_API release() override { return 1; }

    [[nodiscard]] size_t count(const std::string& id) const {
        size_t n = 0;
        for (const auto& r : received) n += r.id == id ? 1u : 0u;
        return n;
    }

private:
    IConnectionPoint* target_;
};

/// A preset written by a processor: every writable parameter the controller registers set to a
/// value derived from its id and @p seed, so two seeds differ in (nearly) every field of the stream.
Bytes makePreset(HostApplication& host, unsigned seed) {
    auto ctrl = Steinberg::owned(new Iterum::Controller());
    REQUIRE(ctrl->initialize(&host) == kResultOk);
    auto proc = std::make_unique<HostedProcessor>(nullptr);
    for (int32 i = 0; i < ctrl->getParameterCount(); ++i) {
        ParameterInfo info{};
        REQUIRE(ctrl->getParameterInfo(i, info) == kResultOk);
        if ((info.flags & ParameterInfo::kIsReadOnly) != 0) continue;
        const unsigned h = (info.id * 2654435761u) ^ (seed * 40503u);
        if (info.stepCount > 0) {
            const auto steps = static_cast<unsigned>(info.stepCount);
            proc->queue(info.id, static_cast<double>(h % (steps + 1)) / static_cast<double>(steps));
        } else {
            proc->queue(info.id, 0.05 + 0.9 * static_cast<double>(h % 1000u) / 1000.0);
        }
    }
    proc->block();
    ctrl->terminate();
    return proc->state();
}

/// A connected, active Controller + Processor pair as a host wires them, the controller's
/// messages passing through a ConnectionSpy.
struct BrowserPair {
    HostApplication host;
    PerformRecorder handler;
    std::unique_ptr<HostedProcessor> proc = std::make_unique<HostedProcessor>(&host);
    IPtr<Iterum::Controller> ctrl = Steinberg::owned(new Iterum::Controller());
    ConnectionSpy spy{static_cast<IConnectionPoint*>(static_cast<AudioEffect*>(&proc->processor()))};
    std::filesystem::path dir;
    int presetCount = 0;

    BrowserPair() {
        std::random_device rd;
        dir = std::filesystem::temp_directory_path() / ("iterum_browser_load_" + std::to_string(rd()));
        std::filesystem::create_directories(dir);
        REQUIRE(ctrl->initialize(&host) == kResultOk);
        REQUIRE(procConn()->connect(ctrlConn()) == kResultOk);
        REQUIRE(ctrlConn()->connect(&spy) == kResultOk);
        ctrl->setComponentHandler(&handler);
    }
    ~BrowserPair() {
        ctrl->setComponentHandler(nullptr);
        ctrlConn()->disconnect(&spy);
        procConn()->disconnect(ctrlConn());
        ctrl->terminate();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    BrowserPair(const BrowserPair&) = delete;
    BrowserPair& operator=(const BrowserPair&) = delete;
    BrowserPair(BrowserPair&&) = delete;
    BrowserPair& operator=(BrowserPair&&) = delete;

    IConnectionPoint* procConn() { return static_cast<IConnectionPoint*>(static_cast<AudioEffect*>(&proc->processor())); }
    IConnectionPoint* ctrlConn() { return static_cast<IConnectionPoint*>(static_cast<EditControllerEx1*>(ctrl.get())); }

    /// Writes @p component as a .vstpreset and loads it through the preset browser's manager.
    bool browserLoad(const Bytes& component) {
        const auto path = dir / ("preset" + std::to_string(++presetCount) + ".vstpreset");
        {
            auto* file = FileStream::open(path.string().c_str(), "wb");
            REQUIRE(file != nullptr);
            auto comp = Steinberg::owned(new MemoryStream());
            comp->write(const_cast<char*>(component.data()), static_cast<int32>(component.size()), nullptr);
            comp->seek(0, IBStream::kIBSeekSet, nullptr);
            const bool saved = PresetFile::savePreset(file, Iterum::kProcessorUID, comp);
            file->release();
            REQUIRE(saved);
        }
        Krate::Plugins::PresetInfo info;
        info.name = "preset" + std::to_string(presetCount);
        info.path = path;
        REQUIRE(ctrl->getPresetManager() != nullptr);
        return ctrl->getPresetManager()->loadPreset(info);
    }

    /// What a host does with the controller's performEdits: hands them to the processor
    /// in the next process() call.
    void forwardEditsAndProcess() {
        for (const auto& [id, value] : handler.edits) proc->queue(id, value);
        handler.edits.clear();
        proc->block();
    }
};

/// Every field of the state is a 4-byte int32 or float. A word whose int32 value lies within +-2^23 in both
/// streams is an integer field (a float there would be zero or subnormal) and must match exactly; any other
/// word is a float and may differ by @p tol (relative above 1). Returns "" when the streams agree, otherwise
/// the count and the first 16 differing words.
std::string fieldDifferences(const Bytes& a, const Bytes& b, double tol) {
    if (a.size() != b.size()) return "sizes differ: " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
    constexpr std::int32_t kIntRange = 1 << 23;
    std::string out;
    int count = 0;
    for (size_t off = 0; off + 4 <= a.size(); off += 4) {
        if (std::memcmp(a.data() + off, b.data() + off, 4) == 0) continue;
        std::int32_t ia = 0;
        std::int32_t ib = 0;
        float x = 0.0f;
        float y = 0.0f;
        std::memcpy(&ia, a.data() + off, 4);
        std::memcpy(&ib, b.data() + off, 4);
        std::memcpy(&x, a.data() + off, 4);
        std::memcpy(&y, b.data() + off, 4);
        const bool integers = ia > -kIntRange && ia < kIntRange && ib > -kIntRange && ib < kIntRange;
        const bool closeFloats = !integers && Krate::DSP::detail::isFinite(x) && Krate::DSP::detail::isFinite(y) &&
                                 std::fabs(static_cast<double>(x) - static_cast<double>(y)) <=
                                     tol * std::max(1.0, std::fabs(static_cast<double>(x)));
        if (closeFloats) continue;
        if (++count <= 16) {
            out += " @" + std::to_string(off) + (integers ? " int " + std::to_string(ia) + " vs " + std::to_string(ib)
                                                           : " float " + std::to_string(x) + " vs " + std::to_string(y));
        }
    }
    return count == 0 ? std::string() : std::to_string(count) + " differing fields:" + out;
}

} // namespace

TEST_CASE("Browser load sends the preset bytes to the processor", "[iterum][presets][state]") {
    HostApplication host;
    const Bytes presetA = makePreset(host, 1u);
    const Bytes presetB = makePreset(host, 2u);
    REQUIRE(presetA.size() == presetB.size());
    REQUIRE(presetA != presetB);

    BrowserPair pair;
    for (const Bytes* preset : {&presetA, &presetB}) {
        INFO((preset == &presetA ? "after preset A" : "after preset B"));
        pair.spy.received.clear();
        REQUIRE(pair.browserLoad(*preset));

        // One message carrying exactly the preset's component state
        REQUIRE(pair.spy.count("LoadPresetState") == 1u);
        for (const auto& r : pair.spy.received) {
            if (r.id == "LoadPresetState") CHECK(r.bytes == *preset);
        }

        HostedProcessor reference(nullptr);
        reference.setState(*preset);
        reference.block();
        const Bytes expected = reference.state();

        // The message ran setState: the processor holds the host-load state, byte for byte
        const Bytes loaded = pair.proc->state();
        CHECK(fieldDifferences(loaded, expected, 0.0) == std::string());
        CHECK(loaded == expected);

        // The controller's edits go to the processor as a host delivers them. They carry the same values,
        // normalized by the controller and denormalized again by the processor in separately compiled code,
        // so a float may move by an ulp or two (root CLAUDE.md); an integer field or a value off by more is
        // a different value.
        pair.forwardEditsAndProcess();
        CHECK(fieldDifferences(pair.proc->state(), expected, 1e-6) == std::string());
    }
}
