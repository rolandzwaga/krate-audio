// ==============================================================================
// Editor screenshots for the site and the manual (hidden: [.][screenshots][site]; never a gate)
// ==============================================================================
// Loads the "Angelic Drift" factory preset, runs the processor for three seconds with a chord held, hands the last
// CloudFrame to the controller (the DataExchange path a host drives), then builds the editor headlessly and renders
// offscreen at 2x: the site images (hero.png, og-image.png, card.png; tests/test_helpers/editor_screenshot.h), and
// for MANUAL.md the header, the cloud view, the editor in Edit mode and the open drawer on each of its seven tabs.
// The images are documentation, not goldens: nothing asserts on pixels.
//
// Seraphis's uidesc carries no uidesc-label, so the regions are found by view (the cloud view, the drawer) or, for
// the header strip, by its rectangle in the 1000 x 700 template.
//
// Regenerate: node tools/gen-screenshots.js seraphis   (writes plugins/seraphis/images/)
// By hand:    seraphis_tests.exe "[site]"  with SERAPHIS_SHOTS_DIR set (default <temp>/seraphis-shots)

#include "controller/controller.h"
#include "preset_test_support.h"
#include "processor/cloud_frame.h"
#include "seraphis_test_fixture.h"
#include "ui/cloud_view.h"
#include "ui/drawer_container.h"
// The shared controls the uidesc names register their ViewCreators when their header is included; entry.cpp, which
// does that in the plugin, is not in seraphis_tests.
#include <ui/icon_segment_button.h>
#include <ui/toggle_button.h>

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>

#include <base/source/updatehandler.h>
#include <pluginterfaces/vst/ivstdataexchange.h>
#include <public.sdk/source/common/memorystream.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/controls/ccontrol.h>
#include <vstgui/plugin-bindings/vst3editor.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace Shots = ::Krate::TestSupport::Screenshots;

constexpr std::string_view kPresetPath = "Choirs/Angelic Drift.vstpreset";
const VSTGUI::CRect kHeaderRect{0, 0, 1000, 32};   // the header's loose root children (editor.uidesc :182-233)

/// The drawer tabs in IconSegmentButton order (session tag 9100) and their image names.
constexpr std::array<std::string_view, 7> kDrawerShots{
    "drawer-cloud.png", "drawer-morph.png", "drawer-body.png", "drawer-atmos.png",
    "drawer-aether.png", "drawer-fx.png", "drawer-life-env.png",
};

[[nodiscard]] Steinberg::IPtr<Steinberg::MemoryStream> streamOf(std::vector<std::uint8_t> bytes) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    Steinberg::int32 written = 0;
    stream->write(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &written);
    REQUIRE(static_cast<std::size_t>(written) == bytes.size());
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return stream;
}

/// The cloud after three seconds of a held chord, as the processor publishes it.
[[nodiscard]] Seraphis::CloudFrame liveFrame(const std::vector<std::uint8_t>& comp) {
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(48000.0, 512) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setState(streamOf(comp)) == Steinberg::kResultOk);
    fx.proc->setCloudFrameGateForTest(true);
    for (const Steinberg::int16 pitch : {48, 55, 60, 64}) {
        fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, 0.8f, 0);
    }
    for (int block = 0; block < 48000 * 3 / 512; ++block) {
        REQUIRE(fx.processBlock(512) == Steinberg::kResultOk);
    }
    return fx.proc->lastPublishedFrameForTest();
}

void deliver(Seraphis::Controller& controller, Seraphis::CloudFrame& frame) {
    const auto context = static_cast<Steinberg::Vst::DataExchangeUserContextID>(Seraphis::kCloudFrameUserContextId);
    Steinberg::TBool background = 1;
    controller.queueOpened(context, static_cast<Steinberg::uint32>(sizeof(frame)), background);
    Steinberg::Vst::DataExchangeBlock block{};
    block.data = &frame;
    block.size = static_cast<Steinberg::uint32>(sizeof(frame));
    block.blockID = 0;
    controller.onDataExchangeBlocksReceived(context, 1, &block, 0);
}

void shoot(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect, const std::filesystem::path& file) {
    INFO("shot " << file.filename().string());
    const std::size_t bytes = Shots::writeShot(root, rect, file);
    std::cout << file.filename().string() << ": " << rect.getWidth() << " x " << rect.getHeight() << " at ("
              << rect.left << ", " << rect.top << "), " << bytes << " bytes\n";
    CHECK(bytes > 0u);
}

/// The rectangle of @p target in root coordinates.
[[nodiscard]] VSTGUI::CRect rectOf(VSTGUI::CViewContainer* root, const VSTGUI::CView* target) {
    VSTGUI::CRect rect;
    REQUIRE(Shots::findView(root, [target](VSTGUI::CView* v) { return v == target; }, {0, 0}, rect));
    return rect;
}

}  // namespace

TEST_CASE("Seraphis_EditorScreenshots", "[.][screenshots][site]") {
    REQUIRE(Shots::initPlatformForShots());
    const std::filesystem::path dir = Shots::shotsDir("SERAPHIS_SHOTS_DIR", "seraphis-shots");
    const SeraphisTest::PresetFile preset = SeraphisTest::parseVstPreset(SeraphisTest::factoryPresetRoot() / kPresetPath);
    REQUIRE_FALSE(preset.comp.empty());
    Seraphis::CloudFrame frame = liveFrame(preset.comp);

    auto controller = Steinberg::owned(new Seraphis::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(controller->setComponentState(streamOf(preset.comp)) == Steinberg::kResultOk);
    deliver(*controller, frame);
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidesc = std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc";
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

        auto* cloud = Shots::findFirst<Seraphis::UI::CloudView>(root);
        REQUIRE(cloud != nullptr);
        Seraphis::UI::DrawerContainer* drawer = controller->drawer();
        REQUIRE(drawer != nullptr);
        drawer->setOpen(false);

        CHECK(Shots::writeSiteShots(root, dir) == 2);
        // The card: the logo, the DREAM ring and the upper cloud, where the partials gather (the whole cloud at card
        // size is a black field with a few dots).
        CHECK(Shots::writeCard(root, dir, VSTGUI::CRect(0, 0, 700, 368)) > 0u);
        shoot(root, kHeaderRect, dir / "header.png");
        shoot(root, rectOf(root, cloud), dir / "cloud-view.png");

        controller->setCloudViewEditMode(true);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        shoot(root, root->getViewSize(), dir / "edit-mode.png");
        controller->setCloudViewEditMode(false);

        // The tab bar itself is driven (as a click does), so the highlighted tab matches the page shown.
        auto* tabs = Shots::findFirst<VSTGUI::CControl>(root, [](VSTGUI::CControl* c) { return c->getTag() == 9100; });
        REQUIRE(tabs != nullptr);
        for (std::size_t tab = 0; tab < kDrawerShots.size(); ++tab) {
            tabs->setValueNormalized(static_cast<float>(tab) / static_cast<float>(kDrawerShots.size() - 1));
            tabs->valueChanged();
            tabs->invalid();
            Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
            REQUIRE(drawer->isOpen());
            shoot(root, rectOf(root, drawer), dir / kDrawerShots[tab]);
        }

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    std::cout << kDrawerShots.size() + 6 << " shots written to " << dir.string() << '\n';
}
