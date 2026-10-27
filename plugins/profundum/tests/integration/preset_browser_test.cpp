// Profundum Phase 13 preset browser tests (specs/profundum-phase13-presets-release/tasks.md)
//
// Placeholder created by T007 (Deviation 1). Cases written by T012, T013, T024:
// Profundum_LoadPresetStateMessage (FR-031, FR-032), browser lifecycle (FR-030, FR-059),
// Profundum_PresetBrowserLoad (SC-013).
//
// T012 (plan 4.6 "Processor"): Profundum_LoadPresetStateMessage drives Processor::notify with a
// kMsgLoadPresetState HostMessage carrying a whole v3 component stream as binary "state". The loaded
// processor's getState must decode equal to a second fresh processor's setState(comp) -> getState, and every
// refused message must leave getState byte-equal.
//
// T013 (plan 4.6 "Controller", "View factory"; FR-030, FR-031, FR-032, FR-059):
//   Profundum_ControllerLoadPresetStream - Controller::loadPresetStream sends exactly one kMsgLoadPresetState, the
//     processor's getState then decodes equal to a fresh setState(comp), the host is told kParamValuesChanged and the
//     controller shows the processor's Mass; oversize / future-version / hostless loads change nothing.
//   Profundum_PresetBrowserSave - the state provider saves the processor's own getState bytes through the host's
//     IComponent (PresetHostComponentHandler); without an IComponent the save fails (non-vacuity).
//   Profundum_PresetBrowserViewLifecycle - "PresetBrowserButton" is an OutlineBrowserButton, the overlay gets
//     makeProfundumPresetTabLabels(), is added in didOpen (closed), opened by openPresetBrowser and dropped in
//     willClose; no Profundum tree walker descends into it (re-cut 2026-10-24, the Vorago pattern).

#include <catch2/catch_test_macros.hpp>

#include "profundum_test_fixture.h"
#include "preset_test_support.h"

#include "ui_tree_helpers.h"  // HeadlessEditor, collectViewsOfType (T013)

#include "controller/controller.h"
#include "parameters/morph_params.h"
#include "parameters/sound_param_row.h"
#include "plugin_ids.h"
#include "preset/preset_info.h"
#include "preset/preset_manager.h"
#include "preset/profundum_preset_config.h"
#include "ui/outline_button.h"
#include "ui/preset_browser_view.h"

#include <krate/dsp/systems/morph_state.h>

#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/vstpresetfile.h>
#include <vstgui/lib/crect.h>
#include <vstgui/lib/cview.h>
#include <vstgui/uidescription/uiattributes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid: the Profundum_PresetBrowserSave file name (T013)
#else
#include <unistd.h>   // getpid
#endif

using namespace ProfundumPluginTest;

namespace {

constexpr double kLpsFs = 48000.0;
constexpr Steinberg::int32 kLpsBlock = 512;
constexpr std::uint32_t kLpsSeed = 0x0BADF00Du;

[[nodiscard]] DecodedPresetState lpsDecode(const StateBytes& b) {
    return decodePresetState(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(b.data()), b.size()));
}

/// Writes @p name into the v3 name block of endpoint @p e (u8 length, bytes, zero padding; plugin_ids.h:55-57).
void lpsPatchName(StateBytes& b, std::size_t e, std::string_view name) {
    const std::size_t at = ::Profundum::kStateNamesOffset + e * ::Profundum::kEndpointNameBlockBytes;
    REQUIRE(name.size() <= ::Profundum::kEndpointNameMaxBytes);
    REQUIRE(at + ::Profundum::kEndpointNameBlockBytes <= b.size());
    b[at] = static_cast<std::byte>(name.size());
    for (std::size_t k = 0; k < ::Profundum::kEndpointNameMaxBytes; ++k) {
        b[at + 1u + k] = k < name.size() ? static_cast<std::byte>(static_cast<unsigned char>(name[k])) : std::byte{0};
    }
}

/// The T012 / T013 load stream: a fresh processor's v3 stream with two distinct valid endpoints, seed 0x0BADF00D
/// and the names "Load A" / "Load B".
[[nodiscard]] StateBytes lpsMakeComp() {
    auto fresh = std::make_unique<ProcessorFixture>();
    StateBytes comp =
        withEndpoints(writeV2Stream(*fresh->proc), randomValidMorphState(0x1312Au), randomValidMorphState(0x1312Bu));
    REQUIRE(comp.size() == ::Profundum::kStateV3Bytes);
    patchU32(comp, kV2SeedAt, kLpsSeed);
    lpsPatchName(comp, 0, "Load A");
    lpsPatchName(comp, 1, "Load B");
    return comp;
}

/// A kMsgLoadPresetState message; @p withState false leaves out the "state" attribute.
[[nodiscard]] Steinberg::IPtr<Steinberg::Vst::HostMessage> lpsLoadMessage(std::span<const std::byte> bytes,
                                                                          bool withState = true) {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID(::Profundum::kMsgLoadPresetState);
    if (withState) {
        Steinberg::Vst::IAttributeList* attrs = msg->getAttributes();
        REQUIRE(attrs != nullptr);
        REQUIRE(attrs->setBinary("state", bytes.data(), static_cast<Steinberg::uint32>(bytes.size())) ==
                Steinberg::kResultOk);
    }
    return msg;
}

[[nodiscard]] bool lpsRefused(Steinberg::tresult r) {
    return r == Steinberg::kResultFalse || r == Steinberg::kInvalidArgument;
}

// ---- T013 helpers ----

/// Every registered parameter's (ID, normalized value), in registration order.
[[nodiscard]] std::vector<std::pair<Steinberg::Vst::ParamID, double>> pbParamSnapshot(::Profundum::Controller& c) {
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> out;
    const Steinberg::int32 n = c.getParameterCount();
    for (Steinberg::int32 i = 0; i < n; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(c.getParameterInfo(i, info) == Steinberg::kResultOk);
        out.emplace_back(info.id, c.getParamNormalized(info.id));
    }
    return out;
}

[[nodiscard]] bool pbHasParamValuesChanged(const std::vector<Steinberg::int32>& flags) {
    return std::any_of(flags.begin(), flags.end(),
                       [](Steinberg::int32 f) { return (f & Steinberg::Vst::kParamValuesChanged) != 0; });
}

[[nodiscard]] long pbPid() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

/// A host's component handler: queryInterface(IComponent::iid) hands out the processor (with addRef), so the
/// controller's createComponentStateStream (FUnknownPtr<IComponent>(getComponentHandler())) reaches it. Declared
/// before the ConnectedPair, so it outlives the controller's reference to it.
class PresetHostComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    Steinberg::Vst::IComponent* component = nullptr;
    std::vector<Steinberg::int32> restartFlags;

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID /*id*/,
                                              Steinberg::Vst::ParamValue /*value*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override {
        restartFlags.push_back(flags);
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        if (component != nullptr && Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponent::iid)) {
            component->addRef();
            *obj = component;
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    // Owned by the test (never by the controller's IPtr): reference counting is a no-op.
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

/// Deletes the saved preset file on scope exit (through deletePreset; a plain remove if that refuses, e.g. the empty
/// file a failed save leaves behind).
struct PresetFileGuard {
    Krate::Plugins::PresetManager* manager = nullptr;
    std::filesystem::path path;

    PresetFileGuard(Krate::Plugins::PresetManager* m, std::filesystem::path p) : manager(m), path(std::move(p)) {}
    ~PresetFileGuard() {
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec)) {
            return;
        }
        try {
            Krate::Plugins::PresetInfo info;
            info.name = path.stem().string();
            info.path = path;
            info.isFactory = false;
            if (manager == nullptr || !manager->deletePreset(info)) {
                std::filesystem::remove(path, ec);
            }
        } catch (...) {
            std::filesystem::remove(path, ec);   // the error_code overload does not throw
        }
    }
    PresetFileGuard(const PresetFileGuard&) = delete;
    PresetFileGuard& operator=(const PresetFileGuard&) = delete;
    PresetFileGuard(PresetFileGuard&&) = delete;
    PresetFileGuard& operator=(PresetFileGuard&&) = delete;
};

/// The Comp (component state) chunk of the .vstpreset at @p path.
[[nodiscard]] StateBytes pbReadCompChunk(const std::filesystem::path& path) {
    Steinberg::IBStream* raw = Steinberg::Vst::FileStream::open(path.string().c_str(), "rb");
    REQUIRE(raw != nullptr);
    auto file = Steinberg::owned(raw);
    Steinberg::Vst::PresetFile preset(file);
    REQUIRE(preset.readChunkList());
    const Steinberg::Vst::PresetFile::Entry* entry = preset.getEntry(Steinberg::Vst::kComponentState);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->size > 0);
    REQUIRE(preset.seekToComponentState());
    StateBytes out(static_cast<std::size_t>(entry->size));
    Steinberg::int32 got = 0;
    REQUIRE(file->read(out.data(), static_cast<Steinberg::int32>(out.size()), &got) == Steinberg::kResultOk);
    REQUIRE(std::cmp_equal(got, out.size()));
    return out;
}

}  // namespace

TEST_CASE("Profundum_LoadPresetStateMessage", "[profundum][browser]") {
    const StateBytes comp = lpsMakeComp();

    // The reference: a second fresh processor, prepared the same way, loads comp through setState.
    auto ref = std::make_unique<ProcessorFixture>();
    REQUIRE(ref->prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
    REQUIRE(loadStateBytes(*ref->proc, comp) == Steinberg::kResultOk);
    const DecodedPresetState want = lpsDecode(writeV2Stream(*ref->proc));
    REQUIRE(want.ok);

    auto fx = std::make_unique<ProcessorFixture>();
    REQUIRE(fx->prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
    const StateBytes beforeLoad = writeV2Stream(*fx->proc);

    REQUIRE(fx->proc->notify(lpsLoadMessage(comp)) == Steinberg::kResultOk);
    const StateBytes loaded = writeV2Stream(*fx->proc);
    const DecodedPresetState got = lpsDecode(loaded);
    REQUIRE(got.ok);

    // Non-vacuity: the message changed the processor (the fresh state differs from comp).
    const bool changed = beforeLoad != loaded;
    CHECK(changed);

    std::string where;
    const bool same = decodedEqual(got, want, 0.0, where);
    INFO(where);
    CHECK(same);

    // The seed, both names and the runtime block, by name.
    CHECK(got.seed == kLpsSeed);
    CHECK(want.seed == kLpsSeed);
    CHECK(got.names[0] == "Load A");
    CHECK(got.names[1] == "Load B");
    CHECK(got.runtime.version == want.runtime.version);
    CHECK(got.runtime.target == want.runtime.target);
    CHECK(got.runtime.position == want.runtime.position);
    CHECK(got.runtime.anchorPosition == want.runtime.anchorPosition);
    CHECK(got.runtime.mode == want.runtime.mode);
    CHECK(got.runtime.rampMs == want.runtime.rampMs);
    CHECK(got.runtime.mainMask == want.runtime.mainMask);
    CHECK(got.runtime.anchorSelection == want.runtime.anchorSelection);

    // Refusals: each one returns kResultFalse / kInvalidArgument and leaves getState byte-equal.
    SECTION("no state attribute") {
        REQUIRE(lpsRefused(fx->proc->notify(lpsLoadMessage({}, false))));
        const bool unchanged = writeV2Stream(*fx->proc) == loaded;
        CHECK(unchanged);
    }
    SECTION("size 0") {
        REQUIRE(lpsRefused(fx->proc->notify(lpsLoadMessage({}))));
        const bool unchanged = writeV2Stream(*fx->proc) == loaded;
        CHECK(unchanged);
    }
    SECTION("size 4097") {
        // A different stream at the front, so an accepted oversize message would change the state.
        StateBytes big = beforeLoad;
        big.resize(4097u, std::byte{0});
        REQUIRE(lpsRefused(fx->proc->notify(lpsLoadMessage(big))));
        const bool unchanged = writeV2Stream(*fx->proc) == loaded;
        CHECK(unchanged);
    }
    SECTION("kMsgEndpointName still works afterwards") {
        auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
        msg->setMessageID(::Profundum::kMsgEndpointName);
        Steinberg::Vst::IAttributeList* attrs = msg->getAttributes();
        REQUIRE(attrs != nullptr);
        constexpr std::string_view kRenamed = "Renamed";
        REQUIRE(attrs->setInt("endpoint", 0) == Steinberg::kResultOk);
        REQUIRE(attrs->setBinary("name", kRenamed.data(), static_cast<Steinberg::uint32>(kRenamed.size())) ==
                Steinberg::kResultOk);
        REQUIRE(fx->proc->notify(msg) == Steinberg::kResultOk);
        const DecodedPresetState renamed = lpsDecode(writeV2Stream(*fx->proc));
        CHECK(renamed.names[0] == "Renamed");
        CHECK(renamed.names[1] == "Load B");
    }
}

// =============================================================================
// T013 - Controller::loadPresetStream (FR-031 steps 1-3, FR-032; plan 4.6 "Controller")
// =============================================================================

TEST_CASE("Profundum_ControllerLoadPresetStream", "[profundum][browser]") {
    const StateBytes comp = lpsMakeComp();
    const DecodedPresetState compDecoded = lpsDecode(comp);
    REQUIRE(compDecoded.ok);
    // Mass on endpoint A at a non-default value, Edit Target A: the Mass check below reads endpoint A.
    REQUIRE(::Profundum::leafBitsOf(compDecoded.a)[::Profundum::kMassLeaf] !=
            ::Profundum::kDefaultLeafBits[::Profundum::kMassLeaf]);
    REQUIRE(compDecoded.morph.editTarget == 0);

    SECTION("loads through the processor, then mirrors") {
        // The reference: a fresh processor, prepared the same way, loads comp through setState.
        auto ref = std::make_unique<ProcessorFixture>();
        REQUIRE(ref->prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
        REQUIRE(loadStateBytes(*ref->proc, comp) == Steinberg::kResultOk);
        const DecodedPresetState want = lpsDecode(writeV2Stream(*ref->proc));
        REQUIRE(want.ok);

        ConnectedPair pair;
        REQUIRE(pair.fx.prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
        ::Profundum::Controller& ctl = *pair.cf.controller;
        const double massBefore = ctl.getParamNormalized(::Profundum::kMassId);
        pair.clearRecords();
        pair.cf.handler.restartFlags.clear();

        REQUIRE(ctl.loadPresetStream(memoryStreamOf(comp)));
        CHECK(pair.toProcessor.count(::Profundum::kMsgLoadPresetState) == 1u);

        const DecodedPresetState got = lpsDecode(writeV2Stream(*pair.fx.proc));
        REQUIRE(got.ok);
        std::string where;
        const bool same = decodedEqual(got, want, 0.0, where);
        INFO(where);
        CHECK(same);
        CHECK(got.seed == kLpsSeed);
        CHECK(got.names[0] == "Load A");
        CHECK(got.names[1] == "Load B");
        CHECK(got.runtime.version == want.runtime.version);
        CHECK(got.runtime.target == want.runtime.target);
        CHECK(got.runtime.position == want.runtime.position);
        CHECK(got.runtime.anchorPosition == want.runtime.anchorPosition);
        CHECK(got.runtime.mode == want.runtime.mode);
        CHECK(got.runtime.rampMs == want.runtime.rampMs);
        CHECK(got.runtime.mainMask == want.runtime.mainMask);
        CHECK(got.runtime.anchorSelection == want.runtime.anchorSelection);

        CHECK(pbHasParamValuesChanged(pair.cf.handler.restartFlags));

        // The controller shows the processor's Mass (endpoint A, the Edit Target), and it moved.
        const std::uint32_t procMassWord =
            pair.fx.proc->endpointSlotForTest(Krate::DSP::BassMorphEngine::Endpoint::A, ::Profundum::kMassLeaf);
        CHECK(procMassWord == ::Profundum::leafBitsOf(compDecoded.a)[::Profundum::kMassLeaf]);
        const double procMass = ::Profundum::normalizedFromLeafWord(::Profundum::kMassLeaf, procMassWord);
        const double ctlMass = ctl.getParamNormalized(::Profundum::kMassId);
        CAPTURE(massBefore, ctlMass, procMass);
        CHECK(std::abs(ctlMass - procMass) <= 1e-12);
        CHECK(massBefore != ctlMass);
        CHECK(::Profundum::leafBitsOf(ctl.mirrorForTest(Krate::DSP::BassMorphEngine::Endpoint::A)) ==
              ::Profundum::leafBitsOf(compDecoded.a));
        CHECK(ctl.endpointName(Krate::DSP::BassMorphEngine::Endpoint::A).view() == "Load A");
        CHECK(ctl.endpointName(Krate::DSP::BassMorphEngine::Endpoint::B).view() == "Load B");
    }

    SECTION("a 4097-byte stream is refused") {
        ConnectedPair pair;
        REQUIRE(pair.fx.prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
        ::Profundum::Controller& ctl = *pair.cf.controller;
        pair.clearRecords();
        pair.cf.handler.restartFlags.clear();
        const StateBytes procBefore = writeV2Stream(*pair.fx.proc);
        const auto paramsBefore = pbParamSnapshot(ctl);

        StateBytes big = comp;
        big.resize(4097u, std::byte{0});
        CHECK_FALSE(ctl.loadPresetStream(memoryStreamOf(big)));
        CHECK(pair.toProcessor.records().empty());
        CHECK(pair.cf.handler.restartFlags.empty());
        const bool procUnchanged = writeV2Stream(*pair.fx.proc) == procBefore;
        CHECK(procUnchanged);
        const bool paramsUnchanged = pbParamSnapshot(ctl) == paramsBefore;
        CHECK(paramsUnchanged);
    }

    SECTION("a future version is refused") {
        ConnectedPair pair;
        REQUIRE(pair.fx.prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
        ::Profundum::Controller& ctl = *pair.cf.controller;
        pair.clearRecords();
        pair.cf.handler.restartFlags.clear();
        const StateBytes procBefore = writeV2Stream(*pair.fx.proc);
        const auto paramsBefore = pbParamSnapshot(ctl);

        StateBytes future = comp;
        patchI32(future, 0, ::Profundum::kCurrentStateVersion + 1);
        CHECK_FALSE(ctl.loadPresetStream(memoryStreamOf(future)));
        CHECK(pair.toProcessor.records().empty());
        CHECK(pair.cf.handler.restartFlags.empty());
        const bool procUnchanged = writeV2Stream(*pair.fx.proc) == procBefore;
        CHECK(procUnchanged);
        const bool paramsUnchanged = pbParamSnapshot(ctl) == paramsBefore;
        CHECK(paramsUnchanged);
    }

    SECTION("no host context: refused, parameters unchanged") {
        ControllerFixture bare;  // initialize(nullptr): allocateMessage() returns nullptr
        const auto paramsBefore = pbParamSnapshot(*bare.controller);
        CHECK_FALSE(bare.controller->loadPresetStream(memoryStreamOf(comp)));
        const bool paramsUnchanged = pbParamSnapshot(*bare.controller) == paramsBefore;
        CHECK(paramsUnchanged);
        CHECK(bare.handler.restartFlags.empty());
        CHECK(bare.controller->endpointName(Krate::DSP::BassMorphEngine::Endpoint::A).view() == "A");
    }
}

// =============================================================================
// T013 - the state provider (FR-031; plan 8 row "Profundum_PresetBrowserSave")
// =============================================================================

TEST_CASE("Profundum_PresetBrowserSave", "[profundum][browser]") {
    const StateBytes comp = lpsMakeComp();

    SECTION("saves the processor's own getState bytes") {
        PresetHostComponentHandler hostHandler;  // declared first: outlives the controller's reference
        ConnectedPair pair;
        hostHandler.component = pair.fx.proc.get();
        REQUIRE(pair.fx.prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
        ::Profundum::Controller& ctl = *pair.cf.controller;
        REQUIRE(ctl.setComponentHandler(&hostHandler) == Steinberg::kResultTrue);

        REQUIRE(ctl.loadPresetStream(memoryStreamOf(comp)));

        Krate::Plugins::PresetManager* manager = ctl.presetManagerForTest();
        REQUIRE(manager != nullptr);
        const std::string name = "Profundum Save Probe " + std::to_string(pbPid());
        const std::filesystem::path path = manager->getUserPresetDirectory() / "Sub" / (name + ".vstpreset");
        const PresetFileGuard guard(manager, path);
        const bool savedOk = manager->savePreset(name, "Sub");
        INFO(manager->getLastError());
        REQUIRE(savedOk);
        REQUIRE(std::filesystem::exists(path));

        const StateBytes saved = pbReadCompChunk(path);
        const DecodedPresetState savedDecoded = lpsDecode(saved);
        REQUIRE(savedDecoded.ok);
        CHECK(savedDecoded.version == 3);
        CHECK(savedDecoded.bytes == 610u);
        CHECK(saved.size() == ::Profundum::kStateV3Bytes);

        const DecodedPresetState procDecoded = lpsDecode(writeV2Stream(*pair.fx.proc));
        REQUIRE(procDecoded.ok);
        std::string where;
        const bool same = decodedEqual(savedDecoded, procDecoded, 0.0, where);
        INFO(where);
        CHECK(same);
        CHECK(savedDecoded.seed == kLpsSeed);
        CHECK(savedDecoded.names[0] == "Load A");
        CHECK(savedDecoded.names[1] == "Load B");
    }

    SECTION("non-vacuity: no IComponent behind the handler -> the save fails") {
        ConnectedPair pair;  // the fixture's RecordingComponentHandler answers no IComponent
        REQUIRE(pair.fx.prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
        ::Profundum::Controller& ctl = *pair.cf.controller;
        REQUIRE(ctl.loadPresetStream(memoryStreamOf(comp)));

        Krate::Plugins::PresetManager* manager = ctl.presetManagerForTest();
        REQUIRE(manager != nullptr);
        const std::string name = "Profundum Save Probe Vacuity " + std::to_string(pbPid());
        const std::filesystem::path path = manager->getUserPresetDirectory() / "Sub" / (name + ".vstpreset");
        const PresetFileGuard guard(manager, path);  // a failed save can leave an empty file behind
        CHECK_FALSE(manager->savePreset(name, "Sub"));
    }
}

// =============================================================================
// T013 - the view factory and the overlay lifecycle (FR-030, FR-059; plan 4.6 "View factory")
// =============================================================================

TEST_CASE("Profundum_PresetBrowserViewLifecycle", "[profundum][browser]") {
    Krate::TestSupport::ensureVstguiInitialized();
    ControllerFixture cf;
    ::Profundum::Controller& ctl = *cf.controller;

    // The tab labels the controller passes: "All" + the seven subcategories (Phase 14b: Kicks).
    const std::vector<std::string> wantLabels = ::Profundum::makeProfundumPresetTabLabels();
    CHECK(wantLabels.size() == 8u);
    CHECK(ctl.presetTabLabelsForTest() == wantLabels);

    SECTION("createCustomView(PresetBrowserButton)") {
        VSTGUI::UIAttributes attrs;
        attrs.setAttribute("origin", "452, 24");
        attrs.setAttribute("size", "96, 24");
        VSTGUI::CView* view = ctl.createCustomView("PresetBrowserButton", attrs, nullptr, nullptr);
        REQUIRE(view != nullptr);
        auto* button = dynamic_cast<Krate::Plugins::OutlineBrowserButton*>(view);
        CHECK(button != nullptr);
        if (button != nullptr) {
            CHECK(button->getTag() == -1);  // the panel sub-controller tags it (T014)
        }
        CHECK(view->getViewSize() == VSTGUI::CRect(452.0, 24.0, 548.0, 48.0));
        view->forget();
    }

    SECTION("added in didOpen, opened on request, dropped in willClose") {
        CHECK(ctl.presetBrowserViewForTest() == nullptr);
        ctl.openPresetBrowser();  // no editor: a no-op
        CHECK(ctl.presetBrowserViewForTest() == nullptr);
        for (int cycle = 0; cycle < 2; ++cycle) {
            CAPTURE(cycle);
            {
                HeadlessEditor ed(cf.controller.get());
                REQUIRE(ed.frame() != nullptr);
                const Krate::Plugins::PresetBrowserView* browser = ctl.presetBrowserViewForTest();
                REQUIRE(browser != nullptr);
                // A direct child of the frame. Not getParentView(): the headless frame is never attached to a
                // window, and VSTGUI sets a child's parent only when it is attached (cviewcontainer.cpp:526-530,
                // cview.cpp:445-451), so the parent stays nullptr here for every view, panel included.
                bool directChildOfFrame = false;
                for (std::uint32_t i = 0; i < ed.frame()->getNbViews(); ++i) {
                    directChildOfFrame = directChildOfFrame || ed.frame()->getView(i) == browser;
                }
                CHECK(directChildOfFrame);
                const auto overlays = collectViewsOfType<Krate::Plugins::PresetBrowserView>(ed.frame());
                REQUIRE(overlays.size() == 1u);
                CHECK_FALSE(browser->isOpen());  // closed until the header button opens it

                // Re-cut 2026-10-24: every Profundum tree walker stops at the overlay. Its own tagged buttons
                // (Save 1 .. Close 5, the dialog tags 10-31; preset_browser_view.h:35-50) must never reach a walk of
                // the frame. Parameter IDs 1-3 are bound on the panel, so the check is by identity, not by tag.
                std::vector<VSTGUI::CControl*> overlayTagged;
                collectBoundControls(overlays.front(), overlayTagged);  // the overlay as the root: its children
                std::erase_if(overlayTagged, [](const VSTGUI::CControl* c) {
                    const std::int32_t t = c->getTag();
                    return (t < 1 || t > 5) && (t < 10 || t > 31);
                });
                REQUIRE_FALSE(overlayTagged.empty());  // non-vacuity: the overlay does carry 1-5 / 10-31 controls
                std::vector<VSTGUI::CControl*> frameBound;
                collectBoundControls(ed.frame(), frameBound);
                REQUIRE_FALSE(frameBound.empty());
                const auto frameControls = collectViewsOfType<VSTGUI::CControl>(ed.frame());
                for (const VSTGUI::CControl* c : overlayTagged) {
                    CAPTURE(c->getTag());
                    CHECK(std::find(frameBound.begin(), frameBound.end(), c) == frameBound.end());
                    CHECK(std::find(frameControls.begin(), frameControls.end(), c) == frameControls.end());
                }
                ctl.openPresetBrowser();
                CHECK(browser->isOpen());
                ctl.openPresetBrowser();  // already open: unchanged
                CHECK(browser->isOpen());
            }
            CHECK(ctl.presetBrowserViewForTest() == nullptr);
            CHECK(ctl.viewPointersNullForTest());
        }
    }
}

// =============================================================================
// T024 - the factory library through the browser (FR-030 - FR-032, FR-035, FR-059; SC-013, SC-014). Every case
// reads the committed tree under PROFUNDUM_PRESETS_DIR and is red until T026 lands the 53-file tree.
// =============================================================================

#ifndef PROFUNDUM_PRESETS_DIR
#error "PROFUNDUM_PRESETS_DIR must be defined (plugins/profundum/tests/CMakeLists.txt)"
#endif

namespace {

[[nodiscard]] std::filesystem::path pbPresetsRoot() {
    return {PROFUNDUM_PRESETS_DIR};
}

/// Every committed *.vstpreset under the presets root, sorted.
[[nodiscard]] std::vector<std::filesystem::path> pbCommittedPresetFiles() {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    namespace fs = std::filesystem;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(pbPresetsRoot(), ec)) {
        std::error_code typeEc;
        if (e.is_regular_file(typeEc) && e.path().extension() == ".vstpreset") {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// Removes a directory tree on construction and on scope exit (the scan's empty user directory).
struct PbTempDir {
    std::filesystem::path path;
    explicit PbTempDir(std::filesystem::path p) : path(std::move(p)) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ~PbTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    PbTempDir(const PbTempDir&) = delete;
    PbTempDir& operator=(const PbTempDir&) = delete;
    PbTempDir(PbTempDir&&) = delete;
    PbTempDir& operator=(PbTempDir&&) = delete;
};

#ifdef _WIN32
/// An environment variable; getenv_s under MSVC avoids C4996.
[[nodiscard]] std::string pbReadEnv(const char* name) {
#ifdef _MSC_VER
    std::array<char, 1024> buf{};
    std::size_t len = 0;
    if (getenv_s(&len, buf.data(), buf.size(), name) != 0 || len == 0u) {
        return {};
    }
    return {buf.data()};
#else
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string{};
#endif
}
#endif

}  // namespace

// SC-013 (FR-030 - FR-032, FR-059): every factory preset loads through PresetManager::loadPreset on a connected pair;
// the processor then decodes equal to a fresh setState(Comp), the host hears kParamValuesChanged, and exactly one
// kMsgLoadPresetState crosses per load.
TEST_CASE("Profundum_PresetBrowserLoad", "[profundum][browser]") {
    const std::vector<std::filesystem::path> files = pbCommittedPresetFiles();
    REQUIRE_FALSE(files.empty());

    ConnectedPair pair;
    REQUIRE(pair.fx.prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
    ::Profundum::Controller& ctl = *pair.cf.controller;
    Krate::Plugins::PresetManager* manager = ctl.presetManagerForTest();
    REQUIRE(manager != nullptr);

    std::size_t mismatches = 0;
    bool anyChanged = false;
    for (const std::filesystem::path& path : files) {
        INFO("preset " << path.string());
        const StateBytes comp = pbReadCompChunk(path);

        // The reference: a fresh processor, prepared the same way, loads the Comp chunk through setState.
        auto ref = std::make_unique<ProcessorFixture>();
        REQUIRE(ref->prepare(kLpsFs, kLpsBlock) == Steinberg::kResultOk);
        REQUIRE(loadStateBytes(*ref->proc, comp) == Steinberg::kResultOk);
        const DecodedPresetState want = lpsDecode(writeV2Stream(*ref->proc));
        REQUIRE(want.ok);

        const StateBytes before = writeV2Stream(*pair.fx.proc);
        pair.clearRecords();
        pair.cf.handler.restartFlags.clear();

        Krate::Plugins::PresetInfo info;
        info.name = path.stem().string();
        info.category = path.parent_path().filename().string();
        info.subcategory = info.category;
        info.path = path;
        info.isFactory = true;
        const bool loaded = manager->loadPreset(info);
        INFO("PresetManager error: " << manager->getLastError());
        CHECK(loaded);
        CHECK(pair.toProcessor.count(::Profundum::kMsgLoadPresetState) == 1u);
        CHECK(pbHasParamValuesChanged(pair.cf.handler.restartFlags));

        const StateBytes after = writeV2Stream(*pair.fx.proc);
        anyChanged = anyChanged || before != after;
        const DecodedPresetState got = lpsDecode(after);
        CHECK(got.ok);
        std::string where;
        const bool same = decodedEqual(got, want, 0.0, where);
        INFO("first difference: " << where);
        CHECK(same);
        mismatches += same ? 0u : 1u;

        // The seed, both names and the runtime block, by name.
        CHECK(got.seed == want.seed);
        CHECK(got.names[0] == want.names[0]);
        CHECK(got.names[1] == want.names[1]);
        CHECK(got.runtime.version == want.runtime.version);
        CHECK(got.runtime.target == want.runtime.target);
        CHECK(got.runtime.position == want.runtime.position);
        CHECK(got.runtime.anchorPosition == want.runtime.anchorPosition);
        CHECK(got.runtime.mode == want.runtime.mode);
        CHECK(got.runtime.rampMs == want.runtime.rampMs);
        CHECK(got.runtime.mainMask == want.runtime.mainMask);
        CHECK(got.runtime.anchorSelection == want.runtime.anchorSelection);
    }
    CHECK(mismatches == 0u);
    CHECK(anyChanged);  // non-vacuity: the loads moved the processor
}

// SC-014 (FR-035): a PresetManager over the committed tree (empty user directory) scans N = 53 factory presets, each
// under its directory's category, none with an empty subcategory, every category populated.
TEST_CASE("Profundum_FactoryPresetScan", "[profundum][browser]") {
    const std::filesystem::path factory = pbPresetsRoot();
    const PbTempDir user(std::filesystem::temp_directory_path() / ("profundum-scan-user-" + std::to_string(pbPid())));
    const Krate::Plugins::PresetManagerConfig config = ::Profundum::makeProfundumPresetConfig();
    Krate::Plugins::PresetManager manager(config, nullptr, nullptr, user.path, factory);
    CHECK(manager.getFactoryPresetDirectory() == factory);

    const Krate::Plugins::PresetManager::PresetList list = manager.scanPresets();
    std::size_t n = 0;
    std::size_t emptySubcategory = 0;
    std::set<std::string> seen;
    for (const Krate::Plugins::PresetInfo& p : list) {
        INFO("preset " << p.path.string());
        CHECK(p.isFactory);
        if (!p.isFactory) {
            continue;
        }
        ++n;
        CHECK(p.subcategory == p.path.parent_path().filename().string());
        emptySubcategory += p.subcategory.empty() ? 1u : 0u;
        seen.insert(p.subcategory);
    }
    CHECK(n == 71u);
    CHECK(emptySubcategory == 0u);
    for (const std::string& category : config.subcategoryNames) {
        INFO("category " << category);
        CHECK(seen.contains(category));
        CHECK_FALSE(manager.getPresetsForSubcategory(category).empty());
    }
    CHECK(seen.size() == config.subcategoryNames.size());
}

#ifdef _WIN32
// SC-014 installed half (release gate, hidden): the default factory directory is %PROGRAMDATA%\Krate Audio\Profundum,
// and a scan with no override lists 71 factory presets under exactly the seven categories.
TEST_CASE("Profundum_InstalledPresetTree", "[profundum][browser][.install]") {
    const std::string programData = pbReadEnv("PROGRAMDATA");
    REQUIRE_FALSE(programData.empty());
    const Krate::Plugins::PresetManagerConfig config = ::Profundum::makeProfundumPresetConfig();
    Krate::Plugins::PresetManager manager(config, nullptr, nullptr);
    const std::filesystem::path want = std::filesystem::path(programData) / "Krate Audio" / "Profundum";
    CHECK(manager.getFactoryPresetDirectory() == want);

    const Krate::Plugins::PresetManager::PresetList list = manager.scanPresets();
    std::size_t n = 0;
    std::set<std::string> seen;
    for (const Krate::Plugins::PresetInfo& p : list) {
        if (!p.isFactory) {
            continue;  // user presets are not the installed tree
        }
        INFO("preset " << p.path.string());
        ++n;
        CHECK_FALSE(p.subcategory.empty());
        seen.insert(p.subcategory);
    }
    CHECK(n == 71u);
    const std::set<std::string> six(config.subcategoryNames.begin(), config.subcategoryNames.end());
    CHECK(seen == six);
}
#endif
