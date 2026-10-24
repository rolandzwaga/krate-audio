// ==============================================================================
// Profundum - SPREAD curve tests (Phase 12)
// ==============================================================================
// Profundum_SpreadCurveMatchesField — written by T024, T030 (specs/profundum-phase12-ui/tasks.md)
//
// SECTION("Model") (T024; FR-080, SC-014 model arm; plan §4.6.8): the controller's SPREAD model against an
// independent heap BassStereoField over the grid Spread {0, 0.5, 1} x Gravity {0, 0.5, 1} x crossover
// {60, 120, 250} Hz x curve {0, 0.5, 1} x image mode {Solid, Spread, Wide} x Field Motion Depth {0, 1}, every value
// written through setParamNormalized, normalised by its row's law. w[i] must equal ref.widthCurve(hz[i]) * m within
// 1e-6, m = 0 in Solid, else spread * (1 - 0.5 * depth).
// SECTION("BuiltView") (T030; SC-014 view arm; plan 6.2): the editor open, the one built SpreadCurveView over a
// subset of the Model grid (every Spread x image mode, Gravity {0, 1}, crossover {60, 250} Hz, curve 0.5, depth
// {0, 1}): sampledValuesForTest() equals the reference w within 1e-6, kneeMarksForTest() the Model knees, and every
// change invalidates the view. Invalidation probe: CView::invalid() calls setDirty(false) (cview.h:79), so the view
// is set dirty before a write and must be clean after it.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "profundum_test_fixture.h"
#include "ui_tree_helpers.h"  // HeadlessEditor, collectViewsOfType (T017)

#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"
#include "ui/spread_curve_view.h"

#include "pluginterfaces/vst/vsttypes.h"
#include "vstgui/lib/cview.h"

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

namespace P = ::Profundum;
using Field = Krate::DSP::BassStereoField;
using ProfundumPluginTest::ControllerFixture;
using Steinberg::Vst::ParamID;

constexpr std::size_t kPoints = 64;

/// The model's abscissa (plan §4.6.8 step 5): hz[i] = 20 * (10000 / 20)^(i / 63).
[[nodiscard]] float gridHz(std::size_t i) {
    return static_cast<float>(20.0 * std::pow(500.0, static_cast<double>(i) / 63.0));
}

/// Writes @p plain to @p id through setParamNormalized, normalised by the row's own law.
void writePlain(P::Controller& ctl, ParamID id, double plain) {
    const P::SoundParamRow* row = P::soundRowOf(id);
    REQUIRE(row != nullptr);
    const double norm = P::normalizedFromPlain(P::lawOf(row->leaf), plain);
    REQUIRE(ctl.setParamNormalized(id, norm) == Steinberg::kResultTrue);
}

struct Cell {
    float spread;
    float gravity;
    float crossoverHz;
    float curve;
    Field::ImageMode mode;
    float depth;
};

void writeCell(P::Controller& ctl, const Cell& c) {
    writePlain(ctl, P::kFieldImageModeId, static_cast<double>(static_cast<int>(c.mode)));
    writePlain(ctl, P::kFieldSpreadId, static_cast<double>(c.spread));
    writePlain(ctl, P::kFieldGravityId, static_cast<double>(c.gravity));
    writePlain(ctl, P::kFieldCrossoverId, static_cast<double>(c.crossoverHz));
    writePlain(ctl, P::kFieldCurveId, static_cast<double>(c.curve));
    writePlain(ctl, P::kFieldMotionDepthId, static_cast<double>(c.depth));
}

/// The expected magnitude: 0 in Solid, else spread * (1 - kappa * depth), kappa = 0.5 (bass_stereo_field.h:78).
[[nodiscard]] float expectedM(const Cell& c) {
    return c.mode == Field::ImageMode::Solid ? 0.0f : c.spread * (1.0f - 0.5f * c.depth);
}

/// The reference curve: an independent heap field, Controls by copy-then-assign, then reset().
[[nodiscard]] std::array<float, kPoints> referenceW(Field& ref, const Cell& c) {
    Field::Controls k{};
    k.mode = c.mode;
    k.spread = c.spread;
    k.gravity = c.gravity;
    k.crossoverHz = c.crossoverHz;
    k.curve = c.curve;
    k.motionDepth = c.depth;
    ref.setControls(k);
    ref.reset();
    const float m = expectedM(c);
    std::array<float, kPoints> w{};
    for (std::size_t i = 0; i < kPoints; ++i) {
        w[i] = ref.widthCurve(gridHz(i)) * m;
    }
    return w;
}

[[nodiscard]] bool relClose(double a, double b, double rel) {
    return std::fabs(a - b) <= rel * std::max(std::fabs(a), std::fabs(b));
}

/// The model's w after writing @p c (a copy: the next write replaces it).
[[nodiscard]] std::array<float, kPoints> modelW(P::Controller& ctl, const Cell& c) {
    writeCell(ctl, c);
    return ctl.spreadModelForTest().w;
}

}  // namespace

TEST_CASE("Profundum_SpreadCurveMatchesField", "[profundum][ui]") {
    SECTION("Model") {
        ControllerFixture cf;
        P::Controller& ctl = *cf.controller;
        auto ref = std::make_unique<Field>();

        constexpr std::array<float, 3> kUnit{0.0f, 0.5f, 1.0f};
        constexpr std::array<float, 3> kCrossovers{60.0f, 120.0f, 250.0f};
        constexpr std::array<Field::ImageMode, 3> kModes{Field::ImageMode::Solid, Field::ImageMode::Spread,
                                                         Field::ImageMode::Wide};
        constexpr std::array<float, 2> kDepths{0.0f, 1.0f};

        int cells = 0;
        for (const float spread : kUnit) {
            for (const float gravity : kUnit) {
                for (const float fc : kCrossovers) {
                    for (const float curve : kUnit) {
                        for (const float depth : kDepths) {
                            std::array<float, kPoints> spreadModeW{};
                            for (const Field::ImageMode mode : kModes) {
                                const Cell c{.spread = spread,
                                             .gravity = gravity,
                                             .crossoverHz = fc,
                                             .curve = curve,
                                             .mode = mode,
                                             .depth = depth};
                                writeCell(ctl, c);
                                const P::UI::SpreadCurveModel& model = ctl.spreadModelForTest();
                                const auto expected = referenceW(*ref, c);
                                const float m = expectedM(c);
                                INFO("spread " << spread << " gravity " << gravity << " fc " << fc << " curve "
                                               << curve << " mode " << static_cast<int>(mode) << " depth " << depth);

                                float maxErr = 0.0f;
                                float maxHzErr = 0.0f;
                                for (std::size_t i = 0; i < kPoints; ++i) {
                                    maxErr = std::max(maxErr, std::fabs(model.w[i] - expected[i]));
                                    maxHzErr = std::max(maxHzErr, std::fabs(model.hz[i] - gridHz(i)) / gridHz(i));
                                }
                                CHECK(maxErr <= 1e-6f);
                                CHECK(maxHzErr <= 1e-6f);

                                // Solid and Spread 0 draw 0 everywhere.
                                if (mode == Field::ImageMode::Solid || spread == 0.0f) {
                                    CHECK(std::all_of(model.w.begin(), model.w.end(),
                                                      [](float v) { return v == 0.0f; }));
                                }
                                // At 10 kHz c = 1: the value is m.
                                CHECK(std::fabs(model.w[kPoints - 1] - m) <= 1e-6f);

                                // The knees (bass_stereo_field.h:648-651) and the crossover.
                                const double lower =
                                    static_cast<double>(fc) *
                                    std::exp2(-static_cast<double>(Field::kLowerKneeMaxOctaves) *
                                              (1.0 - static_cast<double>(gravity)));
                                CHECK(relClose(model.crossoverHz, fc, 1e-3));
                                CHECK(relClose(model.lowerKneeHz, lower, 1e-3));
                                CHECK(relClose(model.upperKneeHz, 4.0 * static_cast<double>(fc), 1e-3));

                                // The flags follow 800.
                                CHECK(model.wide == (mode == Field::ImageMode::Wide));
                                CHECK(model.solid == (mode == Field::ImageMode::Solid));

                                // Spread and Wide give identical w.
                                if (mode == Field::ImageMode::Spread) {
                                    spreadModeW = model.w;
                                } else if (mode == Field::ImageMode::Wide) {
                                    CHECK(model.w == spreadModeW);
                                }
                                ++cells;
                            }
                        }
                    }
                }
            }
        }
        CHECK(cells == 486);

        // The three Spread values at 10 kHz are distinct and linearly spaced.
        {
            std::array<float, 3> top{};
            for (std::size_t s = 0; s < kUnit.size(); ++s) {
                top[s] = modelW(ctl, Cell{.spread = kUnit[s],
                                          .gravity = 0.5f,
                                          .crossoverHz = 120.0f,
                                          .curve = 0.5f,
                                          .mode = Field::ImageMode::Spread,
                                          .depth = 0.0f})[kPoints - 1];
            }
            CHECK(top[0] != top[1]);
            CHECK(top[1] != top[2]);
            CHECK(top[0] != top[2]);
            CHECK(std::fabs((top[1] - top[0]) - (top[2] - top[1])) <= 1e-6f);
        }

        // Depth 1 gives half the depth-0 value, at every point.
        {
            const auto w0 = modelW(ctl, Cell{.spread = 1.0f,
                                             .gravity = 0.5f,
                                             .crossoverHz = 120.0f,
                                             .curve = 0.5f,
                                             .mode = Field::ImageMode::Spread,
                                             .depth = 0.0f});
            const auto w1 = modelW(ctl, Cell{.spread = 1.0f,
                                             .gravity = 0.5f,
                                             .crossoverHz = 120.0f,
                                             .curve = 0.5f,
                                             .mode = Field::ImageMode::Spread,
                                             .depth = 1.0f});
            float maxErr = 0.0f;
            for (std::size_t i = 0; i < kPoints; ++i) {
                maxErr = std::max(maxErr, std::fabs(w1[i] - 0.5f * w0[i]));
            }
            CHECK(maxErr <= 1e-6f);
            CHECK(w0[kPoints - 1] > 0.0f);
        }

        // c(f) differs across Gravity and across crossover at some point below 4 f_c.
        {
            const auto g0 = modelW(ctl, Cell{.spread = 1.0f,
                                             .gravity = 0.0f,
                                             .crossoverHz = 120.0f,
                                             .curve = 0.5f,
                                             .mode = Field::ImageMode::Spread,
                                             .depth = 0.0f});
            const auto g1 = modelW(ctl, Cell{.spread = 1.0f,
                                             .gravity = 1.0f,
                                             .crossoverHz = 120.0f,
                                             .curve = 0.5f,
                                             .mode = Field::ImageMode::Spread,
                                             .depth = 0.0f});
            bool gravityMoves = false;
            for (std::size_t i = 0; i < kPoints; ++i) {
                gravityMoves = gravityMoves || (gridHz(i) < 4.0f * 120.0f && std::fabs(g0[i] - g1[i]) > 1e-3f);
            }
            CHECK(gravityMoves);

            const auto f60 = modelW(ctl, Cell{.spread = 1.0f,
                                              .gravity = 0.5f,
                                              .crossoverHz = 60.0f,
                                              .curve = 0.5f,
                                              .mode = Field::ImageMode::Spread,
                                              .depth = 0.0f});
            const auto f250 = modelW(ctl, Cell{.spread = 1.0f,
                                               .gravity = 0.5f,
                                               .crossoverHz = 250.0f,
                                               .curve = 0.5f,
                                               .mode = Field::ImageMode::Spread,
                                               .depth = 0.0f});
            bool crossoverMoves = false;
            for (std::size_t i = 0; i < kPoints; ++i) {
                crossoverMoves =
                    crossoverMoves || (gridHz(i) < 4.0f * 250.0f && std::fabs(f60[i] - f250[i]) > 1e-3f);
            }
            CHECK(crossoverMoves);
        }

        // The lower knee moves with Gravity, the upper does not.
        {
            writeCell(ctl, Cell{.spread = 1.0f,
                                .gravity = 0.0f,
                                .crossoverHz = 120.0f,
                                .curve = 0.5f,
                                .mode = Field::ImageMode::Spread,
                                .depth = 0.0f});
            const P::UI::SpreadCurveModel atG0 = ctl.spreadModelForTest();
            writeCell(ctl, Cell{.spread = 1.0f,
                                .gravity = 1.0f,
                                .crossoverHz = 120.0f,
                                .curve = 0.5f,
                                .mode = Field::ImageMode::Spread,
                                .depth = 0.0f});
            const P::UI::SpreadCurveModel atG1 = ctl.spreadModelForTest();
            CHECK(atG1.lowerKneeHz > atG0.lowerKneeHz * 1.01f);
            CHECK(atG1.upperKneeHz == atG0.upperKneeHz);
        }

        // A mirror replacement of the Edit Target re-displays 800 - 805 through the base setParamNormalized, which
        // bypasses the override; the model still follows (plan §4.6.4).
        {
            writeCell(ctl, Cell{.spread = 0.5f,
                                .gravity = 0.5f,
                                .crossoverHz = 120.0f,
                                .curve = 0.5f,
                                .mode = Field::ImageMode::Spread,
                                .depth = 0.0f});
            Krate::DSP::MorphState a{};
            a.field.mode = Field::ImageMode::Wide;
            a.field.spread = 1.0f;
            a.field.motionDepth = 0.0f;
            REQUIRE(ctl.notify(ProfundumPluginTest::makeEndpointMessage(Krate::DSP::BassMorphEngine::Endpoint::A,
                                                                        a)) == Steinberg::kResultOk);
            const P::UI::SpreadCurveModel& model = ctl.spreadModelForTest();
            CHECK(model.wide);
            CHECK_FALSE(model.solid);
            CHECK(std::fabs(model.w[kPoints - 1] - 1.0f) <= 1e-6f);
        }
    }

    SECTION("BuiltView") {  // SC-014 view arm (T030)
        ControllerFixture cf;
        P::Controller& ctl = *cf.controller;
        ProfundumPluginTest::HeadlessEditor ed(cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        const std::vector<P::UI::SpreadCurveView*> views =
            ProfundumPluginTest::collectViewsOfType<P::UI::SpreadCurveView>(ed.frame());
        REQUIRE(views.size() == 1u);
        P::UI::SpreadCurveView& view = *views[0];
        // The built view draws the controller's own model (no copy).
        REQUIRE(view.sampledValuesForTest().data() == ctl.spreadModelForTest().w.data());
        auto ref = std::make_unique<Field>();

        // The probe is meaningful only while setDirty(true) latches (CView::kDirtyCallAlwaysOnMainThread false).
        view.setDirty(true);
        REQUIRE(view.isDirty());

        // Every field control 800 - 805 invalidates on its own.
        for (const ParamID id : {static_cast<ParamID>(P::kFieldImageModeId), static_cast<ParamID>(P::kFieldSpreadId),
                                 static_cast<ParamID>(P::kFieldGravityId), static_cast<ParamID>(P::kFieldCrossoverId),
                                 static_cast<ParamID>(P::kFieldCurveId),
                                 static_cast<ParamID>(P::kFieldMotionDepthId)}) {
            INFO("ID " << id);
            view.setDirty(true);
            REQUIRE(view.isDirty());
            REQUIRE(ctl.setParamNormalized(id, 1.0) == Steinberg::kResultTrue);
            CHECK_FALSE(view.isDirty());
        }

        constexpr std::array<float, 3> kSpreads{0.0f, 0.5f, 1.0f};
        constexpr std::array<Field::ImageMode, 3> kModes{Field::ImageMode::Solid, Field::ImageMode::Spread,
                                                         Field::ImageMode::Wide};
        constexpr std::array<float, 2> kGravities{0.0f, 1.0f};
        constexpr std::array<float, 2> kCrossovers{60.0f, 250.0f};
        constexpr std::array<float, 2> kDepths{0.0f, 1.0f};

        int cells = 0;
        for (const float spread : kSpreads) {
            for (const Field::ImageMode mode : kModes) {
                for (const float gravity : kGravities) {
                    for (const float fc : kCrossovers) {
                        for (const float depth : kDepths) {
                            const Cell c{.spread = spread,
                                         .gravity = gravity,
                                         .crossoverHz = fc,
                                         .curve = 0.5f,
                                         .mode = mode,
                                         .depth = depth};
                            INFO("spread " << spread << " gravity " << gravity << " fc " << fc << " mode "
                                           << static_cast<int>(mode) << " depth " << depth);
                            view.setDirty(true);
                            REQUIRE(view.isDirty());
                            writeCell(ctl, c);
                            CHECK_FALSE(view.isDirty());  // invalidated by the change

                            const auto expected = referenceW(*ref, c);
                            const std::array<float, kPoints>& shown = view.sampledValuesForTest();
                            float maxErr = 0.0f;
                            for (std::size_t i = 0; i < kPoints; ++i) {
                                maxErr = std::max(maxErr, std::fabs(shown[i] - expected[i]));
                            }
                            CHECK(maxErr <= 1e-6f);

                            const std::array<float, 2> knees = view.kneeMarksForTest();
                            const double lower =
                                static_cast<double>(fc) * std::exp2(-static_cast<double>(Field::kLowerKneeMaxOctaves) *
                                                                    (1.0 - static_cast<double>(gravity)));
                            CHECK(relClose(knees[0], lower, 1e-3));
                            CHECK(relClose(knees[1], 4.0 * static_cast<double>(fc), 1e-3));
                            CHECK(view.wideLabelVisibleForTest() == (mode == Field::ImageMode::Wide));
                            ++cells;
                        }
                    }
                }
            }
        }
        CHECK(cells == 72);

        // Non-vacuity: the grid drew a non-zero curve (Spread 1, Spread mode, depth 0 is m = 1 at 10 kHz).
        writeCell(ctl, Cell{.spread = 1.0f,
                            .gravity = 0.5f,
                            .crossoverHz = 120.0f,
                            .curve = 0.5f,
                            .mode = Field::ImageMode::Spread,
                            .depth = 0.0f});
        CHECK(std::fabs(view.sampledValuesForTest()[kPoints - 1] - 1.0f) <= 1e-6f);
    }
}
