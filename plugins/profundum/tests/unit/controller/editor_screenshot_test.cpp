// ==============================================================================
// Editor screenshots for the site and the manual (hidden: [.][screenshots][site]; never a gate)
// ==============================================================================
// Builds the editor headlessly (HeadlessEditor), loads a factory preset into the controller, and renders the whole
// editor or one uidesc-labelled section offscreen at 2x into PNG files for MANUAL.md, plus the site images
// (hero.png, og-image.png, card.png; tests/test_helpers/editor_screenshot.h). The images are documentation, not
// goldens: nothing asserts on pixels.
//
// Regenerate: node tools/gen-screenshots.js profundum   (writes plugins/profundum/images/)
// By hand:    profundum_tests.exe "[site]"  with PROFUNDUM_SHOTS_DIR set (default <temp>/profundum-shots)
//
// The live harmonic displays get one hand-written spectrum frame (an A1 bass note: 24 harmonics and the sub anchor),
// delivered as the processor's DataExchange frame would be, so they show what they draw while a note plays.

#include "controller/controller.h"
#include "processor/spectrum_frame.h"
#include "profundum_preset_host.h"
#include "ui/harmonic_bar_view.h"
#include "ui/panel_sub_controller.h"
#include "ui_tree_helpers.h"

#include <editor_screenshot.h>

#include <base/source/updatehandler.h>
#include <pluginterfaces/vst/ivstdataexchange.h>
#include <public.sdk/source/common/memorystream.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/controls/csegmentbutton.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace P = ::Profundum;
namespace Pd = ::Profundum::PresetDefs;
namespace Shots = ::Krate::TestSupport::Screenshots;

/// One manual image: the preset loaded, the page (0 Main, 1 Advanced) and tab (Advanced: 0 SECTIONS, 1 MODULATION,
/// 2 SETTINGS) shown, and the uidesc-label of the view cropped ("" = the whole editor).
struct Shot {
    std::string_view file;
    std::string_view preset;
    int page;
    int tab;
    std::string_view label;
};

constexpr std::string_view kShowcase = "Stiff Wire";
constexpr std::string_view kAnimated = "Tidal Fold";   // Morph Modulation on Cycle

constexpr std::array kShots{
    Shot{.file = "editor-main.png", .preset = kShowcase, .page = 0, .tab = 0, .label = ""},
    Shot{.file = "editor-advanced.png", .preset = kShowcase, .page = 1, .tab = 0, .label = ""},
    Shot{.file = "header.png", .preset = kShowcase, .page = 0, .tab = 0, .label = "header"},
    Shot{.file = "main-mass.png", .preset = kShowcase, .page = 0, .tab = 0, .label = "mass-group"},
    Shot{.file = "main-anchor.png", .preset = kShowcase, .page = 0, .tab = 0, .label = "anchor-group"},
    Shot{.file = "main-bass-morph.png", .preset = kShowcase, .page = 0, .tab = 0, .label = "morph-band"},
    Shot{.file = "adv-core-spectrum.png", .preset = kShowcase, .page = 1, .tab = 0, .label = "section-core-spectrum"},
    Shot{.file = "adv-excitation-fold.png", .preset = kShowcase, .page = 1, .tab = 0,
         .label = "section-excitation-fold"},
    Shot{.file = "adv-shape-character.png", .preset = kShowcase, .page = 1, .tab = 0,
         .label = "section-shape-character"},
    Shot{.file = "adv-mass.png", .preset = kShowcase, .page = 1, .tab = 0, .label = "section-mass"},
    Shot{.file = "adv-sub-anchor.png", .preset = kShowcase, .page = 1, .tab = 0, .label = "section-sub-anchor"},
    Shot{.file = "adv-stereo-output.png", .preset = kShowcase, .page = 1, .tab = 0, .label = "section-stereo-output"},
    Shot{.file = "adv-pitch-envelopes.png", .preset = kShowcase, .page = 1, .tab = 0,
         .label = "section-pitch-envelopes"},
    Shot{.file = "adv-morph-behaviour.png", .preset = kShowcase, .page = 1, .tab = 0,
         .label = "section-morph-behaviour"},
    Shot{.file = "adv-modulation.png", .preset = kAnimated, .page = 1, .tab = 1, .label = "tab-modulation"},
    Shot{.file = "adv-settings.png", .preset = kShowcase, .page = 1, .tab = 2, .label = "tab-settings"},
};

/// One spectrum frame of a driven A1 (55 Hz) note: harmonic n at about 0.6 / n^1.1, odd harmonics a little stronger,
/// over a centred anchor sine of amplitude 0.5 (power 0.25).
[[nodiscard]] P::ProfundumSpectrumFrame bassFrame() {
    P::ProfundumSpectrumFrame f{};
    f.sequence = 1;
    f.sampleRate = 48000.0f;
    f.f0Hz = 55.0f;
    f.anchorPower = 0.25f;
    f.windowSamples = 4096;
    f.numPartials = 24;
    for (unsigned n = 1; n <= f.numPartials; ++n) {
        const float amp = (n % 2 == 1 ? 0.6f : 0.45f) / std::pow(static_cast<float>(n), 1.1f);
        f.partialPower[n - 1] = amp * amp;
    }
    f.morphPosition = 0.0f;
    return f;
}

void deliver(P::Controller& ctl, P::ProfundumSpectrumFrame& frame) {
    const auto context = static_cast<Steinberg::Vst::DataExchangeUserContextID>(P::kProfundumFrameUserContextId);
    Steinberg::TBool background = 1;
    ctl.queueOpened(context, static_cast<Steinberg::uint32>(sizeof(frame)), background);
    Steinberg::Vst::DataExchangeBlock block{};
    block.data = &frame;
    block.size = static_cast<Steinberg::uint32>(sizeof(frame));
    block.blockID = 0;
    ctl.onDataExchangeBlocksReceived(context, 1, &block, 0);
}

/// Runs one timer tick on every harmonic display, which is when a display takes a new frame (no run loop here).
void tickDisplays(VSTGUI::CViewContainer* frame) {
    VSTGUI::CRect unused;
    static_cast<void>(Shots::findView(
        frame,
        [](VSTGUI::CView* v) {
            if (auto* bars = dynamic_cast<P::UI::HarmonicBarView*>(v)) {
                bars->onTickForTest(0.0f);
            }
            return false;
        },
        {0, 0}, unused));
}

void select(VSTGUI::CViewContainer* frame, std::int32_t stripTag, int segment) {
    auto* strip = Shots::findFirst<VSTGUI::CSegmentButton>(
        frame, [stripTag](VSTGUI::CSegmentButton* s) { return s->getTag() == stripTag; });
    REQUIRE(strip != nullptr);
    strip->setSelectedSegment(static_cast<std::uint32_t>(segment));
    strip->valueChanged();
}

void loadPreset(P::Controller& ctl, std::string_view name) {
    for (const Pd::ProfundumPresetDef& def : Pd::allPresets()) {
        if (def.name != name) {
            continue;
        }
        std::vector<std::uint8_t> comp;
        std::string why;
        INFO(why);
        REQUIRE(ProfundumPluginTest::buildPresetComponentState(def, comp, why));
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        Steinberg::int32 written = 0;
        stream->write(comp.data(), static_cast<Steinberg::int32>(comp.size()), &written);
        REQUIRE(static_cast<std::size_t>(written) == comp.size());
        stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        REQUIRE(ctl.setComponentState(stream) == Steinberg::kResultOk);
        return;
    }
    FAIL("no factory preset " << name);
}

/// One editor per preset: load it into a fresh controller, build the editor, render every shot that uses it (and,
/// for the showcase preset, the site images of the Main page).
void renderShotsFor(std::string_view preset, const std::filesystem::path& dir) {
    auto controller = Steinberg::owned(new P::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    loadPreset(*controller, preset);
    {
        ProfundumPluginTest::HeadlessEditor ed(controller.get());
        VSTGUI::CFrame* frame = ed.frame();
        REQUIRE(frame != nullptr);
        // The template root: the frame's first child (the second is the preset browser overlay).
        VSTGUI::CViewContainer* root = frame->getView(0) != nullptr ? frame->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root != nullptr);
        // A headless frame has no window, so CFrame::open never attaches the tree; segment buttons lay out their
        // segments only once attached (CSegmentButton::updateSegmentSizes), and other views draw blank too.
        if (!root->isAttached()) {
            root->attached(frame);
        }
        // VST3Editor fills a bound menu's entries and a segment button's segments in a deferred parameter update,
        // which a host's idle timer flushes; no timer runs here.
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        P::ProfundumSpectrumFrame spectrum = bassFrame();
        deliver(*controller, spectrum);

        if (preset == kShowcase) {
            select(frame, P::UI::kPageStripTag, 0);
            tickDisplays(frame);
            CHECK(Shots::writeSiteShots(root, dir) == 3);
        }
        for (const Shot& shot : kShots) {
            if (shot.preset != preset) {
                continue;
            }
            INFO("shot " << shot.file);
            select(frame, P::UI::kPageStripTag, shot.page);
            if (shot.page == 1) {
                select(frame, P::UI::kTabStripTag, shot.tab);
            }
            tickDisplays(frame);
            VSTGUI::CRect rect = root->getViewSize();
            if (!shot.label.empty()) {
                REQUIRE(Shots::findLabelled(frame, shot.label, rect));
            }
            const std::size_t bytes = Shots::writeShot(root, rect, dir / shot.file);
            std::cout << shot.file << ": " << rect.getWidth() << " x " << rect.getHeight() << " at (" << rect.left
                      << ", " << rect.top << "), " << bytes << " bytes\n";
            CHECK(bytes > 0u);
        }
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

}  // namespace

TEST_CASE("Profundum_EditorScreenshots", "[.][screenshots][site]") {
    REQUIRE(Shots::initPlatformForShots());
    const std::filesystem::path dir = Shots::shotsDir("PROFUNDUM_SHOTS_DIR", "profundum-shots");
    for (const std::string_view preset : {kShowcase, kAnimated}) {
        renderShotsFor(preset, dir);
    }
    std::cout << kShots.size() + 3 << " shots written to " << dir.string() << '\n';
}
