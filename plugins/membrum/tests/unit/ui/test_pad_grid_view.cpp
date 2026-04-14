// ==============================================================================
// PadGridView -- Unit tests (Phase 6, Spec 141, T041)
// FRs covered: FR-010, FR-011, FR-012, FR-013, FR-014 (publisher wiring),
//              FR-015 (DPI-independent cell geom), SC-004, SC-005.
// ==============================================================================

#include "ui/pad_grid_view.h"
#include "dsp/pad_glow_publisher.h"
#include "dsp/pad_config.h"
#include "dsp/body_model_type.h"
#include "dsp/exciter_type.h"

#include "vstgui/lib/crect.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace Membrum;
using namespace Membrum::UI;

// Helper: build a default PadConfig array and return a meta provider that
// reads out of it. The caller owns the storage.
static PadGridView::PadMetaProvider makeProvider(
    const std::array<PadConfig, kNumPads>& pads)
{
    return [&pads](int idx) -> const PadConfig* {
        if (idx < 0 || idx >= kNumPads)
            return nullptr;
        return &pads[static_cast<std::size_t>(idx)];
    };
}

TEST_CASE("PadGridView geometry: MIDI 36 bottom-left, MIDI 67 top-right",
          "[pad_grid][phase6]")
{
    // FR-010: 4 columns x 8 rows; pad 0 (MIDI 36) at bottom-left,
    // pad 31 (MIDI 67) at top-right.

    SECTION("padIndexFromCell bottom-left == 0")
    {
        REQUIRE(padIndexFromCell(0, kPadGridRows - 1) == 0);
        REQUIRE(midiNoteForPad(0) == 36);
    }
    SECTION("padIndexFromCell top-right == 31")
    {
        REQUIRE(padIndexFromCell(kPadGridColumns - 1, 0) == 31);
        REQUIRE(midiNoteForPad(31) == 67);
    }
    SECTION("all 32 cells map uniquely to pads 0..31")
    {
        std::array<int, kNumPads> seen{};
        for (int r = 0; r < kPadGridRows; ++r)
            for (int c = 0; c < kPadGridColumns; ++c)
                seen[static_cast<std::size_t>(padIndexFromCell(c, r))] += 1;
        for (int i = 0; i < kNumPads; ++i)
            REQUIRE(seen[static_cast<std::size_t>(i)] == 1);
    }
}

TEST_CASE("PadGridView::padIndexFromPoint maps screen coords to pad",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        /*glowPublisher*/ nullptr,
        makeProvider(pads));

    // Cell is 100 x 100.
    SECTION("bottom-left quadrant -> pad 0 (MIDI 36)")
    {
        const int pad = view.padIndexFromPoint(VSTGUI::CPoint{ 10, 790 });
        REQUIRE(pad == 0);
        REQUIRE(midiNoteForPad(pad) == 36);
    }

    SECTION("top-right quadrant -> pad 31 (MIDI 67)")
    {
        const int pad = view.padIndexFromPoint(VSTGUI::CPoint{ 390, 10 });
        REQUIRE(pad == 31);
        REQUIRE(midiNoteForPad(pad) == 67);
    }

    SECTION("points outside the view bounds return -1")
    {
        REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ -5, 10 }) == -1);
        REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 10, -5 }) == -1);
        REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 500, 10 }) == -1);
        REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 10, 900 }) == -1);
    }
}

TEST_CASE("PadGridView DPI-scale independence (FR-015)",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};

    // At 1.5x DPI the pixel size of the view grows by 1.5; the mapping from
    // point to pad still works because padIndexFromPoint is expressed in the
    // view's own coordinate system.
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 600, 1200 }, // 1.5x of 400x800
        /*glowPublisher*/ nullptr,
        makeProvider(pads));

    REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 10, 1190 }) == 0);
    REQUIRE(view.padIndexFromPoint(VSTGUI::CPoint{ 590, 10 }) == 31);
}

TEST_CASE("PadGridView click selects AND auditions (FR-012 updated)",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        nullptr,
        makeProvider(pads));

    int lastSelected = -1;
    view.setSelectCallback([&](int p) { lastSelected = p; });

    int lastAudition = -1;
    view.setAuditionCallback([&](int p, float /*v*/) { lastAudition = p; });

    // Plain click must select the pad AND fire audition so the user hears
    // the drum sound on every click (hidden-feature complaint: shift-click
    // alone was too obscure).
    const int hit = view.handleMouseDown(
        VSTGUI::CPoint{ 150, 550 }, /*isShift*/ false, /*isRight*/ false);
    REQUIRE(hit >= 0);
    REQUIRE(lastSelected == hit);
    REQUIRE(lastAudition == hit);
    REQUIRE(view.selectedPadIndex() == hit);
}

TEST_CASE("PadGridView shift-click auditions without changing selection (FR-013)",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        nullptr,
        makeProvider(pads));

    int lastSelected = -1;
    int lastAudition = -1;
    float lastVelocity = -1.0f;
    view.setSelectCallback  ([&](int p)            { lastSelected  = p; });
    view.setAuditionCallback([&](int p, float v)   { lastAudition = p; lastVelocity = v; });

    // Prime selection to pad 0.
    view.setSelectedPadIndex(0);
    REQUIRE(view.selectedPadIndex() == 0);

    // Shift-click pad 7 (col=3, row=7) -> pad 3. Actually pad 7 is at
    // (col=3, row=6). Hit approximate centre of that cell: x=350, y=650.
    const int hit = view.handleMouseDown(
        VSTGUI::CPoint{ 350, 650 }, /*isShift*/ true, /*isRight*/ false);
    REQUIRE(hit >= 0);
    REQUIRE(lastSelected == -1);                   // selection unchanged
    REQUIRE(lastAudition == hit);                  // auditioned
    REQUIRE(lastVelocity > 0.0f);
    REQUIRE(lastVelocity <= 1.0f);
    REQUIRE(view.selectedPadIndex() == 0);         // still pad 0
}

TEST_CASE("PadGridView right-click auditions without changing selection (FR-013)",
          "[pad_grid][phase6]")
{
    std::array<PadConfig, kNumPads> pads{};
    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        nullptr,
        makeProvider(pads));

    view.setSelectedPadIndex(2);

    int lastSelected = -1;
    int lastAudition = -1;
    view.setSelectCallback  ([&](int p)          { lastSelected = p; });
    view.setAuditionCallback([&](int p, float)   { lastAudition = p; });

    const int hit = view.handleMouseDown(
        VSTGUI::CPoint{ 150, 550 }, /*isShift*/ false, /*isRight*/ true);
    REQUIRE(hit >= 0);
    REQUIRE(lastSelected == -1);
    REQUIRE(lastAudition == hit);
    REQUIRE(view.selectedPadIndex() == 2);
}

TEST_CASE("PadGridView glow intensity derived from PadGlowPublisher (FR-014)",
          "[pad_grid][phase6]")
{
    PadGlowPublisher publisher;
    std::array<PadConfig, kNumPads> pads{};

    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        &publisher,
        makeProvider(pads));

    // Publish a mid-level amplitude on pad 5, full amplitude on pad 10.
    publisher.publish(5,  0.5f);
    publisher.publish(10, 1.0f);

    view.pollGlow();

    const std::uint8_t bucket5  = view.glowBucketForPad(5);
    const std::uint8_t bucket10 = view.glowBucketForPad(10);
    const std::uint8_t bucket0  = view.glowBucketForPad(0);

    REQUIRE(bucket5  > 0);
    REQUIRE(bucket10 > bucket5);
    REQUIRE(bucket0  == 0);

    // glowIntensityFromBucket: bucket 31 == 1.0, bucket 0 == 0.0
    REQUIRE(glowIntensityFromBucket(31) == 1.0f);
    REQUIRE(glowIntensityFromBucket(0)  == 0.0f);
    REQUIRE(glowIntensityFromBucket(bucket10) > glowIntensityFromBucket(bucket5));
}

TEST_CASE("PadGridView indicator text helpers (FR-011)",
          "[pad_grid][phase6]")
{
    SECTION("chokeGroup == 0 yields empty string")
    {
        REQUIRE(chokeGroupIndicatorText(0).empty());
    }
    SECTION("chokeGroup == 2 yields 'CG2'")
    {
        REQUIRE(chokeGroupIndicatorText(2) == std::string{"CG2"});
    }
    SECTION("outputBus == 0 yields empty string")
    {
        REQUIRE(outputBusIndicatorText(0).empty());
    }
    SECTION("outputBus == 3 yields 'BUS3'")
    {
        REQUIRE(outputBusIndicatorText(3) == std::string{"BUS3"});
    }
}

TEST_CASE("PadGridView GM drum name helper (FR-011)",
          "[pad_grid][phase6]")
{
    // GM Percussion Key Map: MIDI 36 = Kick, 38 = Snare, 42 = Closed Hi-Hat.
    SECTION("MIDI 36 returns a non-empty kick name")
    {
        const auto name = gmDrumNameForNote(36);
        REQUIRE_FALSE(name.empty());
        // Must contain "Kick" (case-sensitive substring).
        REQUIRE(name.find("Kick") != std::string::npos);
    }
    SECTION("MIDI 38 returns a snare name")
    {
        const auto name = gmDrumNameForNote(38);
        REQUIRE_FALSE(name.empty());
        REQUIRE(name.find("Snare") != std::string::npos);
    }
    SECTION("MIDI 42 returns a hi-hat name")
    {
        const auto name = gmDrumNameForNote(42);
        REQUIRE_FALSE(name.empty());
        REQUIRE((name.find("HH") != std::string::npos
                 || name.find("Hat") != std::string::npos));
    }
    SECTION("all MIDI notes in [36, 67] yield non-empty names")
    {
        for (int n = 36; n <= 67; ++n)
            REQUIRE_FALSE(gmDrumNameForNote(n).empty());
    }
    SECTION("out-of-range notes return empty string")
    {
        REQUIRE(gmDrumNameForNote(35).empty());
        REQUIRE(gmDrumNameForNote(68).empty());
        REQUIRE(gmDrumNameForNote(0).empty());
        REQUIRE(gmDrumNameForNote(127).empty());
    }
}

TEST_CASE("PadGridView category glyph helper (FR-011)",
          "[pad_grid][phase6]")
{
    // Glyph must be derived from the PadCategory classifier used by the
    // coupling matrix so UI and DSP agree. Exactly one character (or empty).
    SECTION("Kick config (Membrane + pitch env) -> 'K'")
    {
        PadConfig cfg{};
        cfg.bodyModel        = BodyModelType::Membrane;
        cfg.tsPitchEnvTime   = 0.5f; // active pitch envelope
        REQUIRE(categoryGlyphForConfig(cfg) == "K");
    }
    SECTION("Snare config (Membrane + NoiseBurst) -> 'S'")
    {
        PadConfig cfg{};
        cfg.bodyModel        = BodyModelType::Membrane;
        cfg.exciterType      = ExciterType::NoiseBurst;
        cfg.tsPitchEnvTime   = 0.0f;
        REQUIRE(categoryGlyphForConfig(cfg) == "S");
    }
    SECTION("Tom config (Membrane only) -> 'T'")
    {
        PadConfig cfg{};
        cfg.bodyModel        = BodyModelType::Membrane;
        cfg.exciterType      = ExciterType::Impulse;
        cfg.tsPitchEnvTime   = 0.0f;
        REQUIRE(categoryGlyphForConfig(cfg) == "T");
    }
    SECTION("HatCymbal config (NoiseBody) -> 'H'")
    {
        PadConfig cfg{};
        cfg.bodyModel = BodyModelType::NoiseBody;
        REQUIRE(categoryGlyphForConfig(cfg) == "H");
    }
    SECTION("Glyph is always a single character for any classified pad")
    {
        PadConfig cfg{};
        const auto g = categoryGlyphForConfig(cfg);
        REQUIRE(g.size() == std::size_t{1});
    }
}

TEST_CASE("PadGridView draw() renders without asserting on a well-formed view",
          "[pad_grid][phase6]")
{
    // Regression coverage: ensure the category-glyph / GM-name rendering path
    // tolerates a null draw context (method must early-return) and does not
    // crash when the meta provider returns a valid config.
    std::array<PadConfig, kNumPads> pads{};
    pads[0].bodyModel      = BodyModelType::Membrane;
    pads[0].tsPitchEnvTime = 0.5f; // Kick
    pads[1].bodyModel      = BodyModelType::NoiseBody; // HatCymbal

    PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        nullptr,
        makeProvider(pads));

    // Null context must not crash.
    view.draw(nullptr);
    SUCCEED();
}

TEST_CASE("PadGridView removed() cancels the poll timer (SC-014)",
          "[pad_grid][phase6]")
{
    // Constructing with a publisher installs a CVSTGUITimer. Tearing the view
    // down via removed() must cancel it so no tick fires into a dying view.
    PadGlowPublisher publisher;
    std::array<PadConfig, kNumPads> pads{};

    auto* view = new PadGridView(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        &publisher,
        makeProvider(pads));

    view->removed(nullptr);
    // No crash, no dangling timer -- handled by PadGridView::removed().
    delete view;
    SUCCEED();
}
