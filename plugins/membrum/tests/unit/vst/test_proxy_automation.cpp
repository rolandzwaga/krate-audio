// ==============================================================================
// Selected-pad proxies are UI-only binding targets (MB-072, MB-163, MB-131,
// MB-099, MB-117; Membrum audit fix plan T1.20)
// ==============================================================================
// Every editor control is bound to a global proxy ID that stands for "this
// field of the selected pad". A gesture on one must reach the host on the
// selected pad's per-pad ID, so a recorded lane stays on its pad whatever is
// selected at playback; the proxies and Selected Pad are not automatable; the
// processor no longer applies a proxy ID (the per-pad route, with its
// reclassify and coupling-matrix refresh, is the only way a pad field changes);
// and a host write to the selected pad's per-pad ID refreshes the proxy the
// editor shows.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/body_model_type.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include "vst_param_changes.h"
#include "unit/controller/recording_component_handler.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr int32  kPaBlockSize  = 256;
constexpr double kPaSampleRate = 48000.0;

ParamID padId(int pad, int offset)
{
    return static_cast<ParamID>(Membrum::padParamId(pad, offset));
}

double selectNorm(int pad)
{
    return static_cast<double>(pad) / static_cast<double>(Membrum::kNumPads - 1);
}

// A controller wired to a recording host component handler.
struct RecordedController
{
    Membrum::TestSupport::RecordingComponentHandler handler;
    Membrum::Controller controller;

    RecordedController()
    {
        REQUIRE(controller.initialize(nullptr) == kResultOk);
        controller.setComponentHandler(&handler);
    }
    ~RecordedController()
    {
        controller.setComponentHandler(nullptr);
        controller.terminate();
    }
    RecordedController(const RecordedController&) = delete;
    RecordedController& operator=(const RecordedController&) = delete;

    // What VSTGUI's VST3Editor does for a control bound to `tag`
    // (vst3editor.cpp ParameterChangeListener): beginEdit, setParamNormalized,
    // performEdit with the controller's value, endEdit.
    void gesture(ParamID tag, ParamValue value)
    {
        controller.beginEdit(tag);
        if (controller.setParamNormalized(tag, value) == kResultTrue)
            controller.performEdit(tag, controller.getParamNormalized(tag));
        controller.endEdit(tag);
    }
};

std::unique_ptr<Membrum::Processor> makeProcessor()
{
    auto proc = std::make_unique<Membrum::Processor>();
    REQUIRE(proc->initialize(nullptr) == kResultOk);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kPaBlockSize;
    setup.sampleRate = kPaSampleRate;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    REQUIRE(proc->setActive(true) == kResultOk);
    return proc;
}

void shutdown(Membrum::Processor& proc)
{
    proc.setActive(false);
    proc.terminate();
}

void processBlock(Membrum::Processor& proc, IParameterChanges* changes)
{
    std::array<float, kPaBlockSize> outL{};
    std::array<float, kPaBlockSize> outR{};
    std::array<float*, 2> chans{outL.data(), outR.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = chans.data();

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kPaBlockSize;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputParameterChanges = changes;
    data.inputEvents = nullptr;
    REQUIRE(proc.process(data) == kResultOk);
}

// A host hands the recorded performEdits to the processor in the next block.
void replay(Membrum::Processor& proc,
            const std::vector<std::pair<ParamID, ParamValue>>& edits)
{
    Krate::Test::ParameterChanges changes;
    for (const auto& [id, value] : edits)
        changes.addChange(id, value);
    processBlock(proc, &changes);
}

std::vector<char> stateBytes(Membrum::Processor& proc)
{
    MemoryStream stream;
    REQUIRE(proc.getState(&stream) == kResultOk);
    const auto size = static_cast<std::size_t>(stream.getSize());
    const char* data = stream.getData();
    return std::vector<char>(data, data + size);
}

using Matrix = std::array<float, static_cast<std::size_t>(Membrum::kNumPads) * Membrum::kNumPads>;

Matrix matrixOf(const Membrum::Processor& proc)
{
    Matrix m{};
    const auto& gains = proc.couplingMatrixForTest().effectiveGainArray();
    for (int s = 0; s < Membrum::kNumPads; ++s)
        for (int d = 0; d < Membrum::kNumPads; ++d)
            m[static_cast<std::size_t>(s * Membrum::kNumPads + d)] =
                gains[s][d];
    return m;
}

// The tag editor.uidesc binds to the control-tag `name` (its
// <control-tag name="..." tag="NNN"/> entry), so the tests follow the editor.
ParamID uidescControlTag(const std::string& name)
{
    std::ifstream file(std::string(MEMBRUM_RESOURCES_DIR) + "/editor.uidesc");
    REQUIRE(file.good());
    std::stringstream ss;
    ss << file.rdbuf();
    const std::string xml = ss.str();
    const std::string needle = "<control-tag name=\"" + name + "\" tag=\"";
    const auto pos = xml.find(needle);
    INFO("control-tag " << name);
    REQUIRE(pos != std::string::npos);
    const auto start = pos + needle.size();
    const auto end = xml.find('"', start);
    REQUIRE(end != std::string::npos);
    return static_cast<ParamID>(std::stoul(xml.substr(start, end - start)));
}

struct MacroKnob
{
    const char* controlTag;
    int         offset;
    double      value;
};

// The five Simple-view macro knobs and the Advanced-view Coupling Amount knob,
// each with a distinct gesture value.
constexpr std::array<MacroKnob, 6> kMacroKnobs{{
    {.controlTag = "MacroTightness",  .offset = Membrum::kPadMacroTightness,  .value = 0.80},
    {.controlTag = "MacroBrightness", .offset = Membrum::kPadMacroBrightness, .value = 0.70},
    {.controlTag = "MacroBodySize",   .offset = Membrum::kPadMacroBodySize,   .value = 0.65},
    {.controlTag = "MacroPunch",      .offset = Membrum::kPadMacroPunch,      .value = 0.60},
    {.controlTag = "MacroComplexity", .offset = Membrum::kPadMacroComplexity, .value = 0.75},
    {.controlTag = "CouplingAmount",  .offset = Membrum::kPadCouplingAmount,  .value = 0.90},
}};

float macroField(const Membrum::PadConfig& cfg, int offset)
{
    switch (offset)
    {
    case Membrum::kPadMacroTightness:  return cfg.macroTightness;
    case Membrum::kPadMacroBrightness: return cfg.macroBrightness;
    case Membrum::kPadMacroBodySize:   return cfg.macroBodySize;
    case Membrum::kPadMacroPunch:      return cfg.macroPunch;
    case Membrum::kPadMacroComplexity: return cfg.macroComplexity;
    case Membrum::kPadCouplingAmount:  return cfg.couplingAmount;
    default:                           return -1.0f;
    }
}

} // namespace

TEST_CASE("Selected-pad proxy: a knob gesture is recorded on the selected pad's per-pad ID",
          "[pad_params][proxy][automation]")
{
    RecordedController rc;
    rc.controller.setParamNormalized(Membrum::kSelectedPadId, selectNorm(2));
    rc.handler.edits.clear();

    rc.gesture(Membrum::kDecayId, 0.9);

    const auto pad2Decay = padId(2, Membrum::kPadDecay);
    INFO("recorded edits: " << rc.handler.edits.size());
    REQUIRE(rc.handler.edits.size() == 1);
    CHECK(rc.handler.edits[0].first == pad2Decay);
    CHECK(rc.handler.edits[0].second == Approx(0.9).margin(1e-9));
    for (const auto& edit : rc.handler.edits)
        CHECK(edit.first != static_cast<ParamID>(Membrum::kDecayId));
    // The gesture brackets the same per-pad ID.
    CHECK(rc.handler.beginEditIds == std::vector<ParamID>{pad2Decay});
    CHECK(rc.handler.endEditIds == std::vector<ParamID>{pad2Decay});

    // The user then selects pad 1; the lane plays back with pad 1 selected.
    rc.controller.setParamNormalized(Membrum::kSelectedPadId, selectNorm(1));

    auto proc = makeProcessor();
    proc->setSelectedPadIndexForTest(1);
    const float pad1Before = proc->voicePoolForTest().padConfig(1).decay;
    const float pad2Before = proc->voicePoolForTest().padConfig(2).decay;
    REQUIRE(pad2Before != Approx(0.9f).margin(1e-4f));

    replay(*proc, rc.handler.edits);

    const auto& pool = proc->voicePoolForTest();
    INFO("pad 1 decay " << pad1Before << " -> " << pool.padConfig(1).decay
         << ", pad 2 decay " << pad2Before << " -> " << pool.padConfig(2).decay);
    CHECK(pool.padConfig(2).decay == Approx(0.9f).margin(1e-6f));
    CHECK(pool.padConfig(1).decay == pad1Before);

    shutdown(*proc);
}

TEST_CASE("Selected-pad proxy: proxies and Selected Pad are not automatable",
          "[pad_params][proxy][automation]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    int proxies = 0;
    int perPad = 0;
    bool selectedPadSeen = false;
    const int32 count = controller.getParameterCount();
    for (int32 i = 0; i < count; ++i)
    {
        ParameterInfo info{};
        REQUIRE(controller.getParameterInfo(i, info) == kResultOk);
        CAPTURE(info.id, info.flags);

        if (Membrum::padIndexFromParamId(static_cast<int>(info.id)) >= 0)
        {
            ++perPad;
            CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
            CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
            continue;
        }

        const bool isProxy = Membrum::Controller::selectedPadProxyOffset(info.id) >= 0;
        const bool isSelectedPad = info.id == static_cast<ParamID>(Membrum::kSelectedPadId);
        if (!isProxy && !isSelectedPad)
        {
            // Kit-wide globals (polyphony, coupling, master gain, UI mode)
            // stay automatable.
            CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
            continue;
        }

        if (isProxy)
            ++proxies;
        if (isSelectedPad)
            selectedPadSeen = true;
        CHECK((info.flags & ParameterInfo::kCanAutomate) == 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
        // The grid's Selected Pad performEdit must still reach the processor.
        CHECK((info.flags & ParameterInfo::kIsReadOnly) == 0);
    }

    CHECK(proxies == 66);
    CHECK(selectedPadSeen);
    CHECK(perPad == Membrum::kNumPads * Membrum::kPadActiveParamCountV12);

    // List parameters stay lists.
    for (const ParamID listId : {static_cast<ParamID>(Membrum::kExciterTypeId),
                                 static_cast<ParamID>(Membrum::kBodyModelId),
                                 static_cast<ParamID>(Membrum::kChokeGroupId),
                                 static_cast<ParamID>(Membrum::kOutputBusId)})
    {
        auto* param = controller.getParameterObject(listId);
        REQUIRE(param != nullptr);
        CAPTURE(listId);
        CHECK((param->getInfo().flags & ParameterInfo::kIsList) != 0);
    }

    controller.terminate();
}

TEST_CASE("Selected-pad proxy: the processor ignores a proxy ID",
          "[pad_params][proxy][automation]")
{
    auto proc = makeProcessor();
    proc->setSelectedPadIndexForTest(3);

    // A non-trivial coupling matrix, so a reclassify would show.
    {
        Krate::Test::ParameterChanges changes;
        changes.addChange(Membrum::kSnareBuzzId, 1.0);
        changes.addChange(Membrum::kTomResonanceId, 1.0);
        processBlock(*proc, &changes);
    }
    const auto stateBefore = stateBytes(*proc);
    const auto matrixBefore = matrixOf(*proc);
    const auto cfgBefore = proc->voicePoolForTest().padConfig(3);

    // Body Membrane (pad 3, the default kit's clap, is NoiseBody) and
    // Material 0.93 on their proxy IDs, then every other proxy ID as well.
    REQUIRE(cfgBefore.bodyModel != Membrum::BodyModelType::Membrane);
    Krate::Test::ParameterChanges changes;
    changes.addChange(Membrum::kBodyModelId, 0.0);
    changes.addChange(Membrum::kMaterialId, 0.93);
    for (ParamID id = 0; id < static_cast<ParamID>(Membrum::kPadBaseId); ++id)
    {
        if (id == static_cast<ParamID>(Membrum::kBodyModelId) ||
            id == static_cast<ParamID>(Membrum::kMaterialId))
            continue;
        if (Membrum::Controller::selectedPadProxyOffset(id) >= 0)
            changes.addChange(id, 0.93);
    }
    processBlock(*proc, &changes);

    const auto& cfg = proc->voicePoolForTest().padConfig(3);
    INFO("pad 3 material " << cfgBefore.material << " -> " << cfg.material
         << ", body " << static_cast<int>(cfgBefore.bodyModel) << " -> "
         << static_cast<int>(cfg.bodyModel));
    CHECK(cfg.material == cfgBefore.material);
    CHECK(cfg.bodyModel == cfgBefore.bodyModel);
    CHECK(matrixOf(*proc) == matrixBefore);
    CHECK(stateBytes(*proc) == stateBefore);

    shutdown(*proc);
}

TEST_CASE("Selected-pad proxy: a Body menu gesture reclassifies the pad",
          "[pad_params][proxy][automation]")
{
    ParamID proxy = 0;
    int offset = -1;
    ParamValue value = 0.0;
    SECTION("Body menu: a Membrane tom becomes NoiseBody (HatCymbal)")
    {
        proxy = Membrum::kBodyModelId;
        offset = Membrum::kPadBodyModel;
        value = 1.0;  // last of six entries: NoiseBody
    }
    SECTION("Pitch-envelope time: a Membrane tom becomes a kick")
    {
        proxy = Membrum::kToneShaperPitchEnvTimeId;
        offset = Membrum::kPadTSPitchEnvTime;
        value = 0.5;
    }

    // Pad 3 starts as a tom (Membrane body, Impulse exciter, no pitch
    // envelope) in both processors, with snare buzz and tom resonance up.
    const auto setUp = [](Membrum::Processor& proc) {
        proc.setSelectedPadIndexForTest(3);
        Krate::Test::ParameterChanges changes;
        changes.addChange(padId(3, Membrum::kPadBodyModel), 0.0);
        changes.addChange(padId(3, Membrum::kPadExciterType), 0.0);
        changes.addChange(padId(3, Membrum::kPadTSPitchEnvTime), 0.0);
        changes.addChange(padId(3, Membrum::kPadCouplingAmount), 1.0);
        changes.addChange(Membrum::kSnareBuzzId, 1.0);
        changes.addChange(Membrum::kTomResonanceId, 1.0);
        processBlock(proc, &changes);
    };

    auto proc = makeProcessor();
    auto reference = makeProcessor();
    setUp(*proc);
    setUp(*reference);
    const auto matrixBefore = matrixOf(*proc);
    REQUIRE(matrixOf(*reference) == matrixBefore);

    // The editor gesture, with pad 3 selected, recorded by the host.
    RecordedController rc;
    rc.controller.setParamNormalized(Membrum::kSelectedPadId, selectNorm(3));
    rc.handler.edits.clear();
    rc.gesture(proxy, value);
    REQUIRE_FALSE(rc.handler.edits.empty());
    replay(*proc, rc.handler.edits);

    // The reference receives the per-pad ID directly.
    {
        Krate::Test::ParameterChanges changes;
        changes.addChange(padId(3, offset), value);
        processBlock(*reference, &changes);
    }

    const auto got = matrixOf(*proc);
    const auto want = matrixOf(*reference);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < got.size(); ++i)
        if (got[i] != want[i])
            ++differing;
    INFO("coupling-matrix cells that differ from the per-pad reference: " << differing);
    CHECK(got == want);
    CHECK(want != matrixBefore);

    shutdown(*proc);
    shutdown(*reference);
}

TEST_CASE("Selected-pad proxy: macro and Coupling Amount knobs edit the selected pad",
          "[pad_params][proxy][macro]")
{
    RecordedController rc;
    rc.controller.setParamNormalized(Membrum::kSelectedPadId, selectNorm(5));
    rc.handler.edits.clear();

    for (const auto& knob : kMacroKnobs)
    {
        const ParamID tag = uidescControlTag(knob.controlTag);
        const std::size_t before = rc.handler.edits.size();
        rc.gesture(tag, knob.value);
        INFO(knob.controlTag << " bound to tag " << tag);
        REQUIRE(rc.handler.edits.size() == before + 1);
        CHECK(rc.handler.edits.back().first == padId(5, knob.offset));
        CHECK(rc.handler.edits.back().second == Approx(knob.value).margin(1e-9));
    }

    // The host replays the lane into a processor with pad 5 selected.
    auto proc = makeProcessor();
    proc->setSelectedPadIndexForTest(5);
    const auto pad0Before = proc->voicePoolForTest().padConfig(0);

    replay(*proc, rc.handler.edits);

    const auto& pool = proc->voicePoolForTest();
    for (const auto& knob : kMacroKnobs)
    {
        INFO(knob.controlTag);
        CHECK(macroField(pool.padConfig(5), knob.offset) ==
              Approx(knob.value).margin(1e-6));
        CHECK(macroField(pool.padConfig(0), knob.offset) ==
              macroField(pad0Before, knob.offset));
    }
    const auto& pad0 = pool.padConfig(0);
    CHECK(pad0.material == pad0Before.material);
    CHECK(pad0.decay == pad0Before.decay);

    shutdown(*proc);
}

TEST_CASE("Selected-pad proxy: macro knobs show the selected pad's values",
          "[pad_params][proxy][macro]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);
    const ParamID punchTag = uidescControlTag("MacroPunch");

    controller.setParamNormalized(padId(2, Membrum::kPadMacroPunch), 0.2);

    controller.setParamNormalized(Membrum::kSelectedPadId, selectNorm(2));
    CHECK(controller.getParamNormalized(punchTag) == Approx(0.2).margin(1e-9));

    controller.setParamNormalized(Membrum::kSelectedPadId, selectNorm(0));
    CHECK(controller.getParamNormalized(punchTag) == Approx(0.5).margin(1e-9));

    controller.terminate();
}

TEST_CASE("Selected-pad proxy: a host write to the selected pad's per-pad ID refreshes the proxy",
          "[pad_params][proxy][automation]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    controller.setParamNormalized(Membrum::kSelectedPadId, selectNorm(3));
    // The selection sync queues every refresh; start from an empty word.
    (void)controller.takePitchEnvDisplayRefreshForTest();

    controller.setParamNormalized(padId(3, Membrum::kPadMaterial), 0.9);
    CHECK(controller.getParamNormalized(Membrum::kMaterialId) == Approx(0.9).margin(1e-9));

    controller.setParamNormalized(padId(3, Membrum::kPadTSPitchEnvTime), 0.4);
    CHECK(controller.getParamNormalized(Membrum::kToneShaperPitchEnvTimeId) ==
          Approx(0.4).margin(1e-9));
    CHECK(controller.takePitchEnvDisplayRefreshForTest());

    // A write to another pad leaves the proxy alone.
    const double materialBefore = controller.getParamNormalized(Membrum::kMaterialId);
    controller.setParamNormalized(padId(4, Membrum::kPadMaterial), 0.2);
    CHECK(controller.getParamNormalized(Membrum::kMaterialId) == materialBefore);
    CHECK(controller.getParamNormalized(padId(4, Membrum::kPadMaterial)) ==
          Approx(0.2).margin(1e-9));

    controller.terminate();
}
