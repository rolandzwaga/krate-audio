// ==============================================================================
// Editor screenshots for the site and the manual (hidden: [.][screenshots][site]; never a gate)
// ==============================================================================
// Loads the "Bowed_Cello" factory preset into the controller, hands it one analysis frame (a bowed-string harmonic
// series on C3, the DisplayData the processor publishes over DataExchange), builds the editor headlessly and renders
// offscreen at 2x: the site images (hero.png, og-image.png, card.png; tests/test_helpers/editor_screenshot.h), and
// for MANUAL.md the header, the analysis displays and every panel group, each found by its fieldset title. The images
// are documentation, not goldens: nothing asserts on pixels.
//
// The analysis frame is written here, not analysed from audio: it shows what the displays draw for a real cello-like
// input without loading a sample. Analysis Feedback is visible only with the sidechain input selected, so it is shot
// last, after switching Input Source.
//
// Regenerate: node tools/gen-screenshots.js innexus   (writes plugins/innexus/images/)
// By hand:    innexus_tests.exe "[site]"  with INNEXUS_SHOTS_DIR set (default <temp>/innexus-shots)

#include "controller/controller.h"
#include "controller/display_data.h"
#include "plugin_ids.h"
// The shared controls the uidesc names register their ViewCreators when their header is included; entry.cpp, which
// does that in the plugin, is not in innexus_tests.
#include "ui/action_button.h"
#include "ui/arc_knob.h"
#include "ui/bipolar_slider.h"
#include "ui/fieldset_container.h"
#include "ui/toggle_button.h"

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>

#include <base/source/updatehandler.h>
#include <pluginterfaces/vst/ivstdataexchange.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/plugin-bindings/vst3editor.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace Shots = ::Krate::TestSupport::Screenshots;

constexpr std::string_view kPresetPath = "Strings/Bowed_Cello.vstpreset";
const VSTGUI::CRect kHeaderRect{0, 0, 800, 50};   // the header container (editor.uidesc :195)

/// A panel group: the union of every fieldset titled @p title (Modulator is two panels, one shot).
struct GroupShot {
    std::string_view file;
    std::string_view title;
};

constexpr std::array kGroupShots{
    GroupShot{.file = "musical-control.png", .title = "Musical Control"},
    GroupShot{.file = "oscillator-residual.png", .title = "Oscillator / Residual"},
    GroupShot{.file = "memory.png", .title = "Memory"},
    GroupShot{.file = "stereo-detune.png", .title = "Stereo / Detune"},
    GroupShot{.file = "evolution.png", .title = "Evolution"},
    GroupShot{.file = "modulators.png", .title = "Modulator"},
    GroupShot{.file = "blend.png", .title = "Blend"},
    GroupShot{.file = "exciter.png", .title = "Exciter"},
    GroupShot{.file = "resonator-bank.png", .title = "Resonator Bank"},
    GroupShot{.file = "body-resonance.png", .title = "Body Resonance"},
    GroupShot{.file = "sympathetic-resonance.png", .title = "Sympathetic Resonance"},
    GroupShot{.file = "harmonic-physics.png", .title = "Harmonic Physics"},
    GroupShot{.file = "adsr.png", .title = "ADSR Envelope"},
    GroupShot{.file = "presets.png", .title = "Presets"},
};

/// One analysis frame of a bowed string on C3: partial n at roughly 1/n, odd partials a little stronger, with a
/// gentle comb from the bow position; three memory slots filled and the first modulator running.
[[nodiscard]] Innexus::DisplayData cello() {
    Innexus::DisplayData d{};
    d.activePartialCount = 48;
    for (int n = 1; n <= d.activePartialCount; ++n) {
        const float comb = 0.55f + 0.45f * std::abs(std::sin(static_cast<float>(n) * 0.9f));
        const float odd = (n % 2 == 1) ? 1.0f : 0.8f;
        d.partialAmplitudes[n - 1] = odd * comb / static_cast<float>(n);
        d.partialActive[n - 1] = 1;
    }
    d.f0 = 130.81f;
    d.f0Confidence = 0.93f;
    d.slotOccupied[0] = 1;
    d.slotOccupied[1] = 1;
    d.slotOccupied[2] = 1;
    d.evolutionPosition = 0.35f;
    d.manualMorphPosition = 0.35f;
    d.mod1Phase = 0.3f;
    d.mod1Active = true;
    d.numVoices = 1;
    d.voices[0] = Innexus::VoiceDisplayData{.f0 = 130.81f, .confidence = 0.93f, .amplitude = 0.8f};
    d.analysisMode = 2;
    d.frameCounter = 1;
    return d;
}

void deliver(Innexus::Controller& controller, Innexus::DisplayData& data) {
    Steinberg::Vst::DataExchangeBlock block{};
    block.data = &data;
    block.size = static_cast<Steinberg::uint32>(sizeof(data));
    block.blockID = 0;
    Steinberg::Vst::IDataExchangeReceiver* receiver = nullptr;
    controller.queryInterface(Steinberg::Vst::IDataExchangeReceiver::iid, reinterpret_cast<void**>(&receiver));
    REQUIRE(receiver != nullptr);
    receiver->onDataExchangeBlocksReceived(0, 1, &block, static_cast<Steinberg::TBool>(false));
    receiver->release();
    controller.testTickDisplayTimer();   // pushes the cached frame into the views
    Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
}

void shoot(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect, const std::filesystem::path& file) {
    INFO("shot " << file.filename().string());
    const std::size_t bytes = Shots::writeShot(root, rect, file);
    std::cout << file.filename().string() << ": " << rect.getWidth() << " x " << rect.getHeight() << " at ("
              << rect.left << ", " << rect.top << "), " << bytes << " bytes\n";
    CHECK(bytes > 0u);
}

[[nodiscard]] VSTGUI::CRect fieldsets(VSTGUI::CViewContainer* root, std::string_view title) {
    const VSTGUI::CRect rect = Shots::unionOfViews(root, [title](VSTGUI::CView* v) {
        auto* fieldset = dynamic_cast<Krate::Plugins::FieldsetContainer*>(v);
        return fieldset != nullptr && v->isVisible() && fieldset->getTitle() == title;
    });
    INFO("fieldset " << title);
    REQUIRE_FALSE(rect.isEmpty());
    return rect;
}

}  // namespace

TEST_CASE("Innexus_EditorScreenshots", "[.][screenshots][site]") {
    REQUIRE(Shots::initPlatformForShots());
    const std::filesystem::path dir = Shots::shotsDir("INNEXUS_SHOTS_DIR", "innexus-shots");

    Steinberg::Vst::HostApplication host;
    auto controller = Steinberg::owned(new Innexus::Controller());
    REQUIRE(controller->initialize(&host) == Steinberg::kResultOk);
    const std::filesystem::path preset = std::filesystem::path(INNEXUS_RESOURCES_DIR) / "presets" / kPresetPath;
    REQUIRE(Shots::loadPresetIntoController(preset, controller.get()));
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidesc = std::string(INNEXUS_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "Editor", uidesc.c_str());
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
        Innexus::DisplayData frame = cello();
        deliver(*controller, frame);

        CHECK(Shots::writeSiteShots(root, dir) == 2);
        // The card: the analysis spectrum from its low end, at the card's shape.
        CHECK(Shots::writeCard(root, dir, Shots::fromLeftAtCardShape(fieldsets(root, "Analysis"))) > 0u);
        shoot(root, kHeaderRect, dir / "header.png");
        shoot(root, fieldsets(root, "Analysis").unite(fieldsets(root, "Pitch Detection")), dir / "analysis.png");
        for (const GroupShot& shot : kGroupShots) {
            shoot(root, fieldsets(root, shot.title), dir / shot.file);
        }

        // Analysis Feedback appears with the sidechain input only.
        controller->setParamNormalized(Innexus::kInputSourceId, 1.0);
        deliver(*controller, frame);
        shoot(root, fieldsets(root, "Feedback"), dir / "analysis-feedback.png");

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    std::cout << kGroupShots.size() + 6 << " shots written to " << dir.string() << '\n';
}
