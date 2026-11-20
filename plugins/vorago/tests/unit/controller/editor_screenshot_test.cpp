// ==============================================================================
// Editor screenshots for the site and the manual (hidden: [.][screenshots][site]; never a gate)
// ==============================================================================
// Loads the "Haunted Colony" factory preset, runs the processor for eight seconds with a chord held so the colony
// grows, hands the last EcosystemFrame to the controller (the DataExchange path a host drives), then builds the
// editor headlessly and renders offscreen at 2x: the site images (hero.png, og-image.png, card.png;
// tests/test_helpers/editor_screenshot.h), and the header, the ecosystem view, both macro banks and each of the
// seven section pages for MANUAL.md. The images are documentation, not goldens: nothing asserts on pixels.
//
// Regenerate: node tools/gen-screenshots.js vorago   (writes plugins/vorago/images/)
// By hand:    vorago_tests.exe "[site]"  with VORAGO_SHOTS_DIR set (default <temp>/vorago-shots)

#include "controller/controller.h"
#include "processor/ecosystem_frame.h"
#include "ui/ecosystem_view.h"
#include "ui/panel_sub_controller.h"
#include "vorago_preset_host.h"

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>
#include <vst_event_list.h>

#include <base/source/updatehandler.h>
#include <pluginterfaces/vst/ivstdataexchange.h>
#include <public.sdk/source/common/memorystream.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/controls/csegmentbutton.h>
#include <vstgui/plugin-bindings/vst3editor.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace Shots = ::Krate::TestSupport::Screenshots;

constexpr std::string_view kPreset = "Haunted Colony";

/// One labelled region (uidesc-label) of the editor, shown on page @p page (-1: any page).
struct Shot {
    std::string_view file;
    std::string_view label;
    int page;
};

constexpr std::array kShots{
    Shot{.file = "header.png", .label = "header", .page = 0},
    Shot{.file = "ecosystem.png", .label = "ecosystem", .page = 0},
    Shot{.file = "macros-left.png", .label = "macros-left", .page = 0},
    Shot{.file = "macros-right.png", .label = "macros-right", .page = 0},
    Shot{.file = "page-cloud.png", .label = "page-0", .page = 0},
    Shot{.file = "page-noise.png", .label = "page-1", .page = 1},
    Shot{.file = "page-resonance.png", .label = "page-2", .page = 2},
    Shot{.file = "page-body.png", .label = "page-3", .page = 3},
    Shot{.file = "page-sub-smear.png", .label = "page-4", .page = 4},
    Shot{.file = "page-space.png", .label = "page-5", .page = 5},
    Shot{.file = "page-life.png", .label = "page-6", .page = 6},
};

[[nodiscard]] std::vector<std::uint8_t> presetState(std::string_view name) {
    for (const Vorago::PresetDefs::VoragoPresetDef& def : Vorago::PresetDefs::allPresets()) {
        if (def.name != name) {
            continue;
        }
        std::vector<std::uint8_t> comp;
        std::string why;
        INFO(why);
        REQUIRE(VoragoTest::buildPresetComponentState(def, comp, why));
        return comp;
    }
    FAIL("no factory preset " << name);
    return {};
}

/// The colony after eight seconds of a held chord, as the processor publishes it.
[[nodiscard]] Vorago::EcosystemFrame liveFrame(std::span<const std::uint8_t> comp) {
    VoragoTest::PresetHost host;
    REQUIRE(host.prepare(48000.0, 512) == Steinberg::kResultOk);
    REQUIRE(host.loadState(comp) == Steinberg::kResultOk);
    host.processor().setEcosystemFrameForcedForTest(true);
    Krate::Test::EventList chord;
    chord.addNoteOn(36, 0.8f);
    chord.addNoteOn(43, 0.7f);
    chord.addNoteOn(48, 0.7f);
    REQUIRE(host.process(512, &chord, nullptr) == Steinberg::kResultOk);
    Krate::Test::EventList none;
    for (int block = 0; block < 48000 * 8 / 512; ++block) {
        REQUIRE(host.process(512, &none, nullptr) == Steinberg::kResultOk);
    }
    return host.processor().lastPublishedFrameForTest();
}

void deliver(Vorago::Controller& controller, Vorago::EcosystemFrame& frame) {
    using Steinberg::Vst::DataExchangeBlock;
    const auto context = static_cast<Steinberg::Vst::DataExchangeUserContextID>(Vorago::kEcosystemFrameUserContextId);
    Steinberg::TBool background = 1;
    controller.queueOpened(context, static_cast<Steinberg::uint32>(sizeof(frame)), background);
    DataExchangeBlock block{};
    block.data = &frame;
    block.size = static_cast<Steinberg::uint32>(sizeof(frame));
    block.blockID = 0;
    controller.onDataExchangeBlocksReceived(context, 1, &block, 0);
}

void loadState(Vorago::Controller& controller, std::vector<std::uint8_t> comp) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    Steinberg::int32 written = 0;
    stream->write(comp.data(), static_cast<Steinberg::int32>(comp.size()), &written);
    REQUIRE(static_cast<std::size_t>(written) == comp.size());
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(controller.setComponentState(stream) == Steinberg::kResultOk);
}

void selectPage(VSTGUI::CViewContainer* frame, int page) {
    auto* strip = Shots::findFirst<VSTGUI::CSegmentButton>(
        frame, [](VSTGUI::CSegmentButton* s) { return s->getTag() == Vorago::UI::kPageStripTag; });
    REQUIRE(strip != nullptr);
    strip->setSelectedSegment(static_cast<std::uint32_t>(page));
    strip->valueChanged();
    Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
}

}  // namespace

TEST_CASE("Vorago_EditorScreenshots", "[.][screenshots][site]") {
    REQUIRE(Shots::initPlatformForShots());
    const std::filesystem::path dir = Shots::shotsDir("VORAGO_SHOTS_DIR", "vorago-shots");
    const std::vector<std::uint8_t> comp = presetState(kPreset);
    Vorago::EcosystemFrame frame = liveFrame(comp);

    auto controller = Steinberg::owned(new Vorago::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    loadState(*controller, comp);
    deliver(*controller, frame);
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidesc = std::string(VORAGO_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "editor", uidesc.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue);
        VSTGUI::CFrame* cframe = editor->getFrame();
        REQUIRE(cframe != nullptr);
        VSTGUI::CViewContainer* root = cframe->getView(0) != nullptr ? cframe->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root != nullptr);
        // A headless frame has no window, so CFrame::open never attaches the tree; views draw blank until attached.
        if (!root->isAttached()) {
            root->attached(cframe);
        }
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        // The links between agents fade in through the view's timer, which no run loop drives here.
        auto* ecosystem = Shots::findFirst<Vorago::UI::EcosystemView>(root);
        REQUIRE(ecosystem != nullptr);
        for (int tick = 0; tick < 30; ++tick) {
            (void)ecosystem->onTimerForTest(1.0f / 30.0f);
        }

        selectPage(root, 0);
        CHECK(Shots::writeSiteShots(root, dir) == 2);
        // The card: the logo, the left macro bank and the edge of the habitat (the habitat alone is near-black at
        // card size: the colony is drawn dim by design).
        CHECK(Shots::writeCard(root, dir, VSTGUI::CRect(0, 0, 750, 395)) > 0u);
        for (const Shot& shot : kShots) {
            INFO("shot " << shot.file);
            selectPage(root, shot.page);
            VSTGUI::CRect rect;
            REQUIRE(Shots::findLabelled(root, shot.label, rect));
            const std::size_t bytes = Shots::writeShot(root, rect, dir / shot.file);
            std::cout << shot.file << ": " << rect.getWidth() << " x " << rect.getHeight() << " at (" << rect.left
                      << ", " << rect.top << "), " << bytes << " bytes\n";
            CHECK(bytes > 0u);
        }

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    std::cout << kShots.size() + 3 << " shots written to " << dir.string() << '\n';
}
