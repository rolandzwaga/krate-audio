// ==============================================================================
// PadCategory Tests -- Classification rules and PadConfig coupling offset
// ==============================================================================
// Phase 5 T008 + T011

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/pad_config.h"
#include "dsp/pad_category.h"

using namespace Membrum;
using Catch::Approx;

// ==============================================================================
// 2.2 PadConfig Extension Tests (T008)
// ==============================================================================

TEST_CASE("kPadCouplingAmount offset is 36", "[pad_category]")
{
    REQUIRE(kPadCouplingAmount == 36);
}

TEST_CASE("kPadActiveParamCountV5 is 37", "[pad_category]")
{
    REQUIRE(kPadActiveParamCountV5 == 37);
}

TEST_CASE("PadConfig couplingAmount default is 0.5f", "[pad_category]")
{
    PadConfig cfg;
    REQUIRE(cfg.couplingAmount == Approx(0.5f));
}

TEST_CASE("padParamId computes correctly for coupling offset", "[pad_category]")
{
    // Pad 0: base + 0 * stride + 36 = 1000 + 36 = 1036
    REQUIRE(padParamId(0, kPadCouplingAmount) == 1036);
    // Pad 1: base + 1 * stride + 36 = 1000 + 64 + 36 = 1100
    REQUIRE(padParamId(1, kPadCouplingAmount) == 1100);
    // Pad 31: base + 31 * stride + 36 = 1000 + 1984 + 36 = 3020
    REQUIRE(padParamId(31, kPadCouplingAmount) == 3020);
}

TEST_CASE("padOffsetFromParamId accepts offset 36", "[pad_category]")
{
    int paramId = padParamId(0, kPadCouplingAmount); // 1036
    REQUIRE(padOffsetFromParamId(paramId) == 36);
}

TEST_CASE("padIndexFromParamId works for coupling offset param", "[pad_category]")
{
    int paramId = padParamId(5, kPadCouplingAmount);
    REQUIRE(padIndexFromParamId(paramId) == 5);
}

// ==============================================================================
// 2.3 PadCategory Classification Tests (T011)
// ==============================================================================

TEST_CASE("classifyPad: Membrane + pitch envelope -> Kick", "[pad_category]")
{
    PadConfig cfg;
    cfg.bodyModel = BodyModelType::Membrane;
    cfg.tsPitchEnvTime = 0.1f; // pitch envelope active
    cfg.exciterType = ExciterType::Impulse;
    REQUIRE(classifyPad(cfg) == PadCategory::Kick);
}

TEST_CASE("classifyPad: Membrane + NoiseBurst exciter (no pitch env) -> Snare",
          "[pad_category]")
{
    PadConfig cfg;
    cfg.bodyModel = BodyModelType::Membrane;
    cfg.exciterType = ExciterType::NoiseBurst;
    cfg.tsPitchEnvTime = 0.0f; // no pitch envelope
    REQUIRE(classifyPad(cfg) == PadCategory::Snare);
}

TEST_CASE("classifyPad: Membrane only (no pitch env, no noise) -> Tom",
          "[pad_category]")
{
    PadConfig cfg;
    cfg.bodyModel = BodyModelType::Membrane;
    cfg.exciterType = ExciterType::Impulse;
    cfg.tsPitchEnvTime = 0.0f;
    REQUIRE(classifyPad(cfg) == PadCategory::Tom);
}

TEST_CASE("classifyPad: NoiseBody -> HatCymbal", "[pad_category]")
{
    PadConfig cfg;
    cfg.bodyModel = BodyModelType::NoiseBody;
    REQUIRE(classifyPad(cfg) == PadCategory::HatCymbal);
}

TEST_CASE("classifyPad: Other body models -> Perc", "[pad_category]")
{
    PadConfig cfg;

    SECTION("Plate -> Perc")
    {
        cfg.bodyModel = BodyModelType::Plate;
        REQUIRE(classifyPad(cfg) == PadCategory::Perc);
    }

    SECTION("Shell -> Perc")
    {
        cfg.bodyModel = BodyModelType::Shell;
        REQUIRE(classifyPad(cfg) == PadCategory::Perc);
    }

    SECTION("String -> Perc")
    {
        cfg.bodyModel = BodyModelType::String;
        REQUIRE(classifyPad(cfg) == PadCategory::Perc);
    }

    SECTION("Bell -> Perc")
    {
        cfg.bodyModel = BodyModelType::Bell;
        REQUIRE(classifyPad(cfg) == PadCategory::Perc);
    }
}

TEST_CASE("classifyPad: Priority - Kick fires before Snare when both conditions met",
          "[pad_category]")
{
    // Membrane + pitch envelope + NoiseBurst -> should be Kick (rule 1 first)
    PadConfig cfg;
    cfg.bodyModel = BodyModelType::Membrane;
    cfg.tsPitchEnvTime = 0.2f;              // pitch envelope active
    cfg.exciterType = ExciterType::NoiseBurst; // noise exciter too
    REQUIRE(classifyPad(cfg) == PadCategory::Kick);
}

TEST_CASE("PadCategory kCount is 5", "[pad_category]")
{
    REQUIRE(static_cast<int>(PadCategory::kCount) == 5);
}
