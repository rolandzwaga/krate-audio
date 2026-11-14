// ==============================================================================
// Band Visibility Tests
// ==============================================================================
// D-123 (fix plan T5.03): the band strips (containers holding the controls
// tagged 9000 + b) show and hide through the controller's real wiring: Band
// Count, the 4-entry StringListParameter registerAllParameters builds, and the
// ContainerVisibilityControllers didOpen attaches to it. Band b is visible
// iff Band Count > b, for every count 1-4.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "../test_support/headless_editor.h"

#include "controller/controller.h"
#include "dsp/band_state.h"
#include "plugin_ids.h"

#include "vstgui/lib/cframe.h"

#include <array>
#include <cstdio>

using namespace Disrumpo;

using Disrumpo::TestSupport::findContainerHoldingTag;
using Disrumpo::TestSupport::HeadlessEditor;

TEST_CASE("Band strip b is visible iff Band Count is greater than b", "[disrumpo][visibility][ui][D-123]") {
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    const auto bandCountId = makeGlobalParamId(GlobalParamType::kGlobalBandCount);
    Steinberg::Vst::ParameterInfo info{};
    REQUIRE(c.getParameterInfoByTag(bandCountId, info) == Steinberg::kResultTrue);
    REQUIRE(info.stepCount == kMaxBands - 1);  // entries "1" .. "4"
    c.setParamNormalized(bandCountId, 1.0);
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        std::array<VSTGUI::CViewContainer*, kMaxBands> strips{};
        for (int b = 0; b < kMaxBands; ++b) {
            strips[static_cast<size_t>(b)] = findContainerHoldingTag(ed.frame(), 9000 + b);
            REQUIRE(strips[static_cast<size_t>(b)] != nullptr);
        }

        int wrong = 0;
        for (int count = 1; count <= kMaxBands; ++count) {
            // The list entry a host or the segment button writes for this count
            c.setParamNormalized(bandCountId, static_cast<double>(count - 1) / static_cast<double>(info.stepCount));
            std::printf("D-123 Band Count %d: strips visible", count);
            for (int b = 0; b < kMaxBands; ++b) {
                const bool visible = strips[static_cast<size_t>(b)]->isVisible();
                std::printf(" %d", visible ? 1 : 0);
                if (visible != (count > b)) ++wrong;
            }
            std::printf("\n");
        }
        CHECK(wrong == 0);
    }
    c.terminate();
}
