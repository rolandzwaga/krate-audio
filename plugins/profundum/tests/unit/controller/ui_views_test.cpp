// ==============================================================================
// Profundum Phase 12 - custom view unit tests (T016; Deviation 3)
// ==============================================================================
// Spec:  specs/profundum-phase12-ui/spec.md  (FR-061, FR-070, FR-073, FR-080, FR-091)
// Plan:  specs/profundum-phase12-ui/plan.md  (sections 4.8, 4.9, 4.6.8; P-5)
// Tasks: specs/profundum-phase12-ui/tasks.md (T016)
//
// Profundum_UiViewsUnit drives the three plugin-local views without an editor:
//   - HarmonicBarView: bar heights from a hand-built ProfundumSpectrumFrame, the
//     ruled 300 ms linear silence decay, display-only mouse handling and the
//     attached()/removed() timer lifetime;
//   - SpreadCurveView: mirrors the SpreadCurveModel it is given (no timer);
//   - OffsetRingKnob: the one-sided ring span of the P-5 ruling, value untouched.
//
// NEVER name a kPlatformType* constant here (lint-platform-type-literals.js).
// ==============================================================================

#include "test_helpers/editor_lifecycle_harness.h"

#include "processor/spectrum_frame.h"
#include "ui/harmonic_bar_view.h"
#include "ui/offset_ring_knob.h"
#include "ui/spread_curve_view.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <memory>

namespace {

using Catch::Approx;
using Profundum::ProfundumSpectrumFrame;
using Profundum::UI::HarmonicBarView;
using Profundum::UI::SpreadCurveModel;
using Profundum::UI::SpreadCurveView;

const VSTGUI::CRect kBarRect(0, 0, 400, 160);
constexpr float kTick = 0.033f;

/// The BarHeightsFromFrame frame: fs 48000, f0 41.2, 64 partials.
void fillReferenceFrame(ProfundumSpectrumFrame& f) {
    f = ProfundumSpectrumFrame{};
    f.sequence = 1;
    f.sampleRate = 48000.0f;
    f.f0Hz = 41.2f;
    f.numPartials = 64;
    f.partialPower[0] = 1.0f;
    f.partialPower[1] = 0.1f;
    f.partialPower[2] = 1e-6f;
    f.partialPower[3] = 1e-7f;
    f.anchorPower = 0.5f;
}

}  // namespace

TEST_CASE("Profundum_UiViewsUnit", "[profundum][ui]") {
    Krate::TestSupport::ensureVstguiInitialized();

    SECTION("BarHeightsFromFrame") {
        auto frame = std::make_unique<ProfundumSpectrumFrame>();
        fillReferenceFrame(*frame);
        auto view = VSTGUI::makeOwned<HarmonicBarView>(kBarRect, frame.get(), HarmonicBarView::Variant::Main);

        view->onTickForTest(kTick);

        const auto& h = view->barHeightsForTest();
        REQUIRE(h.size() == Profundum::kSpectrumMaxPartials);
        CHECK(h[0] == Approx(1.0f).margin(1e-6));
        CHECK(h[1] == Approx(50.0f / 60.0f).margin(1e-6));
        CHECK(h[2] == Approx(0.0f).margin(1e-6));
        CHECK(h[3] == Approx(0.0f).margin(1e-6));  // -70 dB clamps to the -60 dB floor
        const double expectedAnchor = ((10.0 * std::log10(0.5)) + 60.0) / 60.0;
        CHECK(view->anchorHeightForTest() == Approx(expectedAnchor).margin(1e-6));

        // Heights past numPartials are 0 (a frame of 3 partials with power beyond).
        frame->sequence = 2;
        frame->numPartials = 3;
        frame->partialPower[10] = 1.0f;
        view->onTickForTest(kTick);
        const auto& h2 = view->barHeightsForTest();
        for (std::size_t n = 3; n < h2.size(); ++n) {
            INFO("n index " << n);
            CHECK(h2[n] == 0.0f);
        }
        CHECK(h2[0] == Approx(1.0f).margin(1e-6));
    }

    SECTION("SilenceDecaysLinearly") {
        auto frame = std::make_unique<ProfundumSpectrumFrame>();
        fillReferenceFrame(*frame);
        auto view = VSTGUI::makeOwned<HarmonicBarView>(kBarRect, frame.get(), HarmonicBarView::Variant::Core);
        view->onTickForTest(kTick);
        REQUIRE(view->barHeightsForTest()[0] == Approx(1.0f).margin(1e-6));
        REQUIRE(HarmonicBarView::kSilenceDecaySeconds == Approx(0.300f));

        // The silent frame (plan 4.2: numPartials 0 on a silent frame).
        frame->sequence = 2;
        frame->numPartials = 0;
        frame->flags = Profundum::kFrameFlagSilent;
        for (auto& p : frame->partialPower) {
            p = 0.0f;
        }
        frame->anchorPower = 0.0f;

        auto previous = view->barHeightsForTest();
        float previousAnchor = view->anchorHeightForTest();
        float elapsed = 0.0f;
        bool crossed = false;
        for (int tick = 1; tick <= 20 && !crossed; ++tick) {
            view->onTickForTest(kTick);
            elapsed = static_cast<float>(tick) * kTick;
            const auto& now = view->barHeightsForTest();
            for (std::size_t n = 0; n < now.size(); ++n) {
                INFO("tick " << tick << " n index " << n);
                REQUIRE(now[n] <= previous[n]);
            }
            REQUIRE(view->anchorHeightForTest() <= previousAnchor);
            if (tick == 5) {
                CHECK(now[0] == Approx(1.0f - (0.165f / 0.300f)).margin(1e-6));
            }
            if (elapsed >= 0.300f) {
                crossed = true;
                for (std::size_t n = 0; n < now.size(); ++n) {
                    INFO("after crossing, n index " << n);
                    CHECK(now[n] == 0.0f);
                }
                CHECK(view->anchorHeightForTest() == 0.0f);
            }
            previous = now;
            previousAnchor = view->anchorHeightForTest();
        }
        REQUIRE(crossed);
    }

    SECTION("DisplayOnly") {
        auto frame = std::make_unique<ProfundumSpectrumFrame>();
        auto view = VSTGUI::makeOwned<HarmonicBarView>(kBarRect, frame.get(), HarmonicBarView::Variant::Main);
        VSTGUI::CView* asView = view.get();
        CHECK(dynamic_cast<VSTGUI::CControl*>(asView) == nullptr);

        const VSTGUI::CButtonState left(VSTGUI::kLButton);
        for (const VSTGUI::CPoint& p : {VSTGUI::CPoint(0, 0), VSTGUI::CPoint(200, 80), VSTGUI::CPoint(399, 159),
                                        VSTGUI::CPoint(-10, 500)}) {
            VSTGUI::CPoint where = p;
            CHECK(asView->onMouseDown(where, left) == VSTGUI::kMouseEventNotHandled);
            where = p;
            CHECK(asView->onMouseMoved(where, left) == VSTGUI::kMouseEventNotHandled);
            where = p;
            CHECK(asView->onMouseUp(where, left) == VSTGUI::kMouseEventNotHandled);
        }
    }

    SECTION("TimerOnlyWhileAttached") {
        auto parent = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, 960, 600));
        auto frame = std::make_unique<ProfundumSpectrumFrame>();
        auto* view = new HarmonicBarView(kBarRect, frame.get(), HarmonicBarView::Variant::Main);
        REQUIRE_FALSE(view->hasTimerForTest());
        for (int cycle = 0; cycle < 10; ++cycle) {
            INFO("cycle " << cycle);
            REQUIRE(view->attached(parent.get()));
            REQUIRE(view->hasTimerForTest());
            REQUIRE(view->removed(parent.get()));
            REQUIRE_FALSE(view->hasTimerForTest());
        }
        view->forget();
    }

    SECTION("SpreadViewMirrorsModel") {
        for (const bool wide : {false, true}) {
            INFO("wide " << wide);
            SpreadCurveModel model;
            for (std::size_t i = 0; i < model.w.size(); ++i) {
                model.w[i] = static_cast<float>(i) / 63.0f;
                model.hz[i] = 20.0f * std::pow(10000.0f / 20.0f, static_cast<float>(i) / 63.0f);
            }
            model.crossoverHz = 120.0f;
            model.lowerKneeHz = 85.0f;
            model.upperKneeHz = 480.0f;
            model.wide = wide;

            auto view = VSTGUI::makeOwned<SpreadCurveView>(VSTGUI::CRect(0, 0, 300, 120), &model);
            const auto& sampled = view->sampledValuesForTest();
            REQUIRE(sampled.size() == model.w.size());
            for (std::size_t i = 0; i < sampled.size(); ++i) {
                INFO("i " << i);
                CHECK(sampled[i] == model.w[i]);
            }
            const auto marks = view->kneeMarksForTest();
            CHECK(marks[0] == 85.0f);
            CHECK(marks[1] == 480.0f);
            CHECK(view->wideLabelVisibleForTest() == wide);
        }
    }

    SECTION("OffsetRingOneSided") {
        auto knob = VSTGUI::makeOwned<Profundum::OffsetRingKnob>(VSTGUI::CRect(0, 0, 40, 40), nullptr, 200);

        struct Arm {
            float v;
            float d;
            bool hasSpan;
            float lo;
            float hi;
        };
        const Arm arms[] = {
            {.v = 0.4f, .d = 0.2f, .hasSpan = true, .lo = 0.4f, .hi = 0.6f},
            {.v = 0.4f, .d = -0.3f, .hasSpan = true, .lo = 0.1f, .hi = 0.4f},
            {.v = 0.9f, .d = 0.3f, .hasSpan = true, .lo = 0.9f, .hi = 1.0f},
            {.v = 0.4f, .d = 0.0f, .hasSpan = false, .lo = 0.0f, .hi = 0.0f},
        };
        for (const Arm& a : arms) {
            INFO("v " << a.v << " d " << a.d);
            knob->setValueNormalized(a.v);
            const float before = knob->getValue();
            knob->setOffset(a.d);
            CHECK(knob->getOffset() == a.d);
            const auto span = knob->ringSpan();
            REQUIRE(span.has_value() == a.hasSpan);
            if (a.hasSpan && span.has_value()) {
                CHECK(span->low == Approx(a.lo).margin(1e-6));
                CHECK(span->high == Approx(a.hi).margin(1e-6));
            }
            CHECK(knob->getValue() == before);
            CHECK(knob->getModulationRange() == 0.0f);  // the base symmetric ring is never used
        }
    }
}
