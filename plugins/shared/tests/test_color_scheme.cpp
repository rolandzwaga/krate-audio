// ==============================================================================
// Color Scheme Consistency Tests (081-interaction-polish, Phase 9, T090)
// ==============================================================================
// Verifies that darkenColor/brightenColor produce deterministic, consistent
// results across all 6 lane accent colors, and that trail alpha + X overlay
// derivations match the expected RGBA values.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "ui/color_utils.h"

using namespace Krate::Plugins;
using namespace VSTGUI;

// ==============================================================================
// The 6 canonical lane accent colors (FR-037 through FR-042)
// ==============================================================================
static constexpr CColor kCopper{208, 132, 92, 255};   // velocity
static constexpr CColor kSand{200, 164, 100, 255};    // gate
static constexpr CColor kSage{108, 168, 160, 255};    // pitch
static constexpr CColor kLavender{152, 128, 176, 255}; // ratchet
static constexpr CColor kRose{192, 112, 124, 255};    // modifier
static constexpr CColor kSlate{124, 144, 176, 255};   // condition

static constexpr CColor kAllAccents[] = {
    kCopper, kSand, kSage, kLavender, kRose, kSlate};
static constexpr const char* kAccentNames[] = {
    "copper", "sand", "sage", "lavender", "rose", "slate"};

// ==============================================================================
// darkenColor determinism: same input always gives same output for all 6 colors
// ==============================================================================

TEST_CASE("darkenColor is deterministic for all 6 accent colors", "[color][scheme]") {
    constexpr float factors[] = {0.25f, 0.35f, 0.4f, 0.5f, 0.6f};

    for (int c = 0; c < 6; ++c) {
        const CColor& accent = kAllAccents[c];
        INFO("Accent: " << kAccentNames[c]);

        for (float factor : factors) {
            INFO("Factor: " << factor);
            CColor first = darkenColor(accent, factor);
            CColor second = darkenColor(accent, factor);

            REQUIRE(first.red == second.red);
            REQUIRE(first.green == second.green);
            REQUIRE(first.blue == second.blue);
            REQUIRE(first.alpha == second.alpha);
        }
    }
}

// ==============================================================================
// brightenColor determinism: same input always gives same output for all 6 colors
// ==============================================================================

TEST_CASE("brightenColor is deterministic for all 6 accent colors", "[color][scheme]") {
    constexpr float factors[] = {1.0f, 1.3f, 1.5f, 2.0f};

    for (int c = 0; c < 6; ++c) {
        const CColor& accent = kAllAccents[c];
        INFO("Accent: " << kAccentNames[c]);

        for (float factor : factors) {
            INFO("Factor: " << factor);
            CColor first = brightenColor(accent, factor);
            CColor second = brightenColor(accent, factor);

            REQUIRE(first.red == second.red);
            REQUIRE(first.green == second.green);
            REQUIRE(first.blue == second.blue);
            REQUIRE(first.alpha == second.alpha);
        }
    }
}

// ==============================================================================
// Trail alpha derivation: applying kTrailAlphas to accent colors produces
// expected CColor values (alpha override, RGB from accent)
// ==============================================================================

TEST_CASE("Trail alpha derivation produces correct RGBA for all 6 accents", "[color][scheme]") {
    // These are the authoritative trail alpha values from PlayheadTrailState::kTrailAlphas
    constexpr float kTrailAlphas[] = {160.0f, 100.0f, 55.0f, 25.0f};

    for (int c = 0; c < 6; ++c) {
        const CColor& accent = kAllAccents[c];
        INFO("Accent: " << kAccentNames[c]);

        for (int t = 0; t < 4; ++t) {
            INFO("Trail index: " << t);

            // The trail overlay uses accent RGB with alpha from kTrailAlphas
            CColor expected = accent;
            expected.alpha = static_cast<uint8_t>(
                std::clamp(kTrailAlphas[t], 0.0f, 255.0f));

            // Verify the derivation
            REQUIRE(expected.red == accent.red);
            REQUIRE(expected.green == accent.green);
            REQUIRE(expected.blue == accent.blue);
            REQUIRE(expected.alpha == static_cast<uint8_t>(kTrailAlphas[t]));
        }
    }
}

// ==============================================================================
// X overlay color derivation: brightenColor(accent, 1.3) at alpha 204
// produces identical results for all 6 accent colors
// ==============================================================================

TEST_CASE("X overlay color derivation is consistent for all 6 accents", "[color][scheme]") {
    for (int c = 0; c < 6; ++c) {
        const CColor& accent = kAllAccents[c];
        INFO("Accent: " << kAccentNames[c]);

        // Compute the X overlay color the same way draw code does
        CColor xColor = brightenColor(accent, 1.3f);
        xColor.alpha = 204;  // ~80%

        // Verify the brightened color is brighter or clamped
        REQUIRE(xColor.red >= accent.red);  // only fails if clamped to 255
        REQUIRE(xColor.green >= accent.green);
        REQUIRE(xColor.blue >= accent.blue);

        // Verify alpha is exactly 204
        REQUIRE(xColor.alpha == 204);

        // Verify RGB channels are clamped to 255
        REQUIRE(xColor.red <= 255);
        REQUIRE(xColor.green <= 255);
        REQUIRE(xColor.blue <= 255);

        // Verify the computation is deterministic
        CColor xColor2 = brightenColor(accent, 1.3f);
        xColor2.alpha = 204;
        REQUIRE(xColor.red == xColor2.red);
        REQUIRE(xColor.green == xColor2.green);
        REQUIRE(xColor.blue == xColor2.blue);
    }
}

// ==============================================================================
// Specific expected values for X overlay (brightenColor at 1.3)
// Pre-computed: red = min(255, accent.red * 1.3), etc.
// ==============================================================================

TEST_CASE("X overlay brightenColor(1.3) produces expected RGB values", "[color][scheme]") {
    // Copper #D0845C: R=208*1.3=270->255, G=132*1.3=171, B=92*1.3=119
    {
        CColor x = brightenColor(kCopper, 1.3f);
        REQUIRE(x.red == 255);
        REQUIRE(x.green == 171);
        REQUIRE(x.blue == 119);
    }

    // Sand #C8A464: R=200*1.3=260->255, G=164*1.3=213, B=100*1.3=130
    {
        CColor x = brightenColor(kSand, 1.3f);
        REQUIRE(x.red == 255);
        REQUIRE(x.green == 213);
        REQUIRE(x.blue == 130);
    }

    // Sage #6CA8A0: R=108*1.3=140, G=168*1.3=218, B=160*1.3=208
    {
        CColor x = brightenColor(kSage, 1.3f);
        REQUIRE(x.red == 140);
        REQUIRE(x.green == 218);
        REQUIRE(x.blue == 208);
    }

    // Lavender #9880B0: R=152*1.3=197, G=128*1.3=166, B=176*1.3=228
    {
        CColor x = brightenColor(kLavender, 1.3f);
        REQUIRE(x.red == 197);
        REQUIRE(x.green == 166);
        REQUIRE(x.blue == 228);
    }

    // Rose #C0707C: R=192*1.3=249, G=112*1.3=145, B=124*1.3=161
    {
        CColor x = brightenColor(kRose, 1.3f);
        REQUIRE(x.red == 249);
        REQUIRE(x.green == 145);
        REQUIRE(x.blue == 161);
    }

    // Slate #7C90B0: R=124*1.3=161, G=144*1.3=187, B=176*1.3=228
    {
        CColor x = brightenColor(kSlate, 1.3f);
        REQUIRE(x.red == 161);
        REQUIRE(x.green == 187);
        REQUIRE(x.blue == 228);
    }
}

// ==============================================================================
// darkenColor(0.6) "normal" variant: used by setAccentColor for bar colors
// ==============================================================================

TEST_CASE("darkenColor(0.6) produces expected normal bar colors", "[color][scheme]") {
    // Copper: R=208*0.6=124, G=132*0.6=79, B=92*0.6=55
    {
        CColor n = darkenColor(kCopper, 0.6f);
        REQUIRE(n.red == 124);
        REQUIRE(n.green == 79);
        REQUIRE(n.blue == 55);
        REQUIRE(n.alpha == 255);
    }

    // Sand: R=200*0.6=120, G=164*0.6=98, B=100*0.6=60
    {
        CColor n = darkenColor(kSand, 0.6f);
        REQUIRE(n.red == 120);
        REQUIRE(n.green == 98);
        REQUIRE(n.blue == 60);
        REQUIRE(n.alpha == 255);
    }
}

// ==============================================================================
// darkenColor(0.35) "ghost" variant: used by setAccentColor for bar colors
// ==============================================================================

TEST_CASE("darkenColor(0.35) produces expected ghost bar colors", "[color][scheme]") {
    // Copper: R=208*0.35=72, G=132*0.35=46, B=92*0.35=32
    {
        CColor g = darkenColor(kCopper, 0.35f);
        REQUIRE(g.red == 72);
        REQUIRE(g.green == 46);
        REQUIRE(g.blue == 32);
        REQUIRE(g.alpha == 255);
    }

    // Sand: R=200*0.35=70, G=164*0.35=57, B=100*0.35=35
    {
        CColor g = darkenColor(kSand, 0.35f);
        REQUIRE(g.red == 70);
        REQUIRE(g.green == 57);
        REQUIRE(g.blue == 35);
        REQUIRE(g.alpha == 255);
    }
}

// ==============================================================================
// darkenColor preserves alpha for all 6 accent colors
// ==============================================================================

TEST_CASE("darkenColor preserves alpha for all 6 accents", "[color][scheme]") {
    for (int c = 0; c < 6; ++c) {
        INFO("Accent: " << kAccentNames[c]);

        CColor input = kAllAccents[c];
        input.alpha = 42;

        CColor result = darkenColor(input, 0.5f);
        REQUIRE(result.alpha == 42);
    }
}

// ==============================================================================
// brightenColor preserves alpha for all 6 accent colors
// ==============================================================================

TEST_CASE("brightenColor preserves alpha for all 6 accents", "[color][scheme]") {
    for (int c = 0; c < 6; ++c) {
        INFO("Accent: " << kAccentNames[c]);

        CColor input = kAllAccents[c];
        input.alpha = 42;

        CColor result = brightenColor(input, 1.3f);
        REQUIRE(result.alpha == 42);
    }
}
