// ==============================================================================
// Phase 6: parameter ID + PadConfig v6 layout tests
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-070, FR-071, FR-072)
// Tasks: T005, T008
// ==============================================================================

#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "voice_pool/voice_pool.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using namespace Membrum;

// ------------------------------------------------------------------------------
// T005: Phase 6 parameter IDs and static constraints
// ------------------------------------------------------------------------------
TEST_CASE("Phase 6 global parameter IDs are allocated correctly", "[phase6_params]")
{
    SECTION("kUiModeId == 280")
    {
        STATIC_REQUIRE(static_cast<int>(kUiModeId) == 280);
    }

    SECTION("kOutputBusId == 282")
    {
        STATIC_REQUIRE(static_cast<int>(kOutputBusId) == 282);
    }

    SECTION("kPhase6GlobalCount == 2")
    {
        STATIC_REQUIRE(kPhase6GlobalCount == 2);
    }

    SECTION("kCurrentStateVersion bumped to 6")
    {
        STATIC_REQUIRE(kCurrentStateVersion == 6);
    }
}

TEST_CASE("Phase 6 per-pad macro offsets are allocated correctly", "[phase6_params]")
{
    SECTION("kPadMacroTightness == 37")
    {
        STATIC_REQUIRE(static_cast<int>(kPadMacroTightness) == 37);
    }

    SECTION("kPadMacroBrightness == 38")
    {
        STATIC_REQUIRE(static_cast<int>(kPadMacroBrightness) == 38);
    }

    SECTION("kPadMacroBodySize == 39")
    {
        STATIC_REQUIRE(static_cast<int>(kPadMacroBodySize) == 39);
    }

    SECTION("kPadMacroPunch == 40")
    {
        STATIC_REQUIRE(static_cast<int>(kPadMacroPunch) == 40);
    }

    SECTION("kPadMacroComplexity == 41")
    {
        STATIC_REQUIRE(static_cast<int>(kPadMacroComplexity) == 41);
    }

    SECTION("kPadActiveParamCountV6 == 42")
    {
        STATIC_REQUIRE(kPadActiveParamCountV6 == 42);
    }
}

TEST_CASE("padParamId computes pad+offset combinations", "[phase6_params]")
{
    SECTION("Pad 0 offset 37 -> 1037")
    {
        REQUIRE(padParamId(0, kPadMacroTightness) == 1037);
    }

    SECTION("Pad 0 offset 41 -> 1041")
    {
        REQUIRE(padParamId(0, kPadMacroComplexity) == 1041);
    }

    SECTION("Pad 31 offset 37 -> 3021 (1000 + 31*64 + 37)")
    {
        REQUIRE(padParamId(31, kPadMacroTightness) == 1000 + 31 * 64 + 37);
    }

    SECTION("Pad 31 offset 41 -> last macro ID")
    {
        REQUIRE(padParamId(31, kPadMacroComplexity) == 1000 + 31 * 64 + 41);
    }
}

TEST_CASE("padOffsetFromParamId accepts macro offsets 37-41", "[phase6_params]")
{
    SECTION("Pad 0 macro IDs round-trip")
    {
        for (int off = 37; off <= 41; ++off) {
            const int id = padParamId(0, off);
            REQUIRE(padOffsetFromParamId(id) == off);
        }
    }

    SECTION("Pad 17 macro IDs round-trip")
    {
        for (int off = 37; off <= 41; ++off) {
            const int id = padParamId(17, off);
            REQUIRE(padOffsetFromParamId(id) == off);
        }
    }

    SECTION("Pad 31 macro IDs round-trip")
    {
        for (int off = 37; off <= 41; ++off) {
            const int id = padParamId(31, off);
            REQUIRE(padOffsetFromParamId(id) == off);
        }
    }

    SECTION("Offset 42 is reserved -- still rejected")
    {
        REQUIRE(padOffsetFromParamId(padParamId(0, 42)) == -1);
    }
}

// ------------------------------------------------------------------------------
// T008: PadConfig v6 macro fields default to 0.5 and round-trip via setPadConfigField
// ------------------------------------------------------------------------------
TEST_CASE("PadConfig v6 macro fields default to 0.5", "[phase6_params][padconfig]")
{
    PadConfig cfg{};
    REQUIRE(cfg.macroTightness  == 0.5f);
    REQUIRE(cfg.macroBrightness == 0.5f);
    REQUIRE(cfg.macroBodySize   == 0.5f);
    REQUIRE(cfg.macroPunch      == 0.5f);
    REQUIRE(cfg.macroComplexity == 0.5f);
}

TEST_CASE("VoicePool::setPadConfigField round-trips macro offsets", "[phase6_params][padconfig]")
{
    VoicePool pool;
    pool.prepare(48000.0, 512);

    SECTION("Tightness on pad 0")
    {
        pool.setPadConfigField(0, kPadMacroTightness, 0.25f);
        REQUIRE(pool.padConfig(0).macroTightness == 0.25f);
    }

    SECTION("Brightness on pad 7")
    {
        pool.setPadConfigField(7, kPadMacroBrightness, 0.10f);
        REQUIRE(pool.padConfig(7).macroBrightness == 0.10f);
    }

    SECTION("Body Size on pad 15")
    {
        pool.setPadConfigField(15, kPadMacroBodySize, 0.75f);
        REQUIRE(pool.padConfig(15).macroBodySize == 0.75f);
    }

    SECTION("Punch on pad 23")
    {
        pool.setPadConfigField(23, kPadMacroPunch, 0.90f);
        REQUIRE(pool.padConfig(23).macroPunch == 0.90f);
    }

    SECTION("Complexity on pad 31")
    {
        pool.setPadConfigField(31, kPadMacroComplexity, 1.0f);
        REQUIRE(pool.padConfig(31).macroComplexity == 1.0f);
    }

    SECTION("All five macros default to 0.5 on a fresh pool")
    {
        for (int p = 0; p < kNumPads; ++p) {
            REQUIRE(pool.padConfig(p).macroTightness  == 0.5f);
            REQUIRE(pool.padConfig(p).macroBrightness == 0.5f);
            REQUIRE(pool.padConfig(p).macroBodySize   == 0.5f);
            REQUIRE(pool.padConfig(p).macroPunch      == 0.5f);
            REQUIRE(pool.padConfig(p).macroComplexity == 0.5f);
        }
    }
}
