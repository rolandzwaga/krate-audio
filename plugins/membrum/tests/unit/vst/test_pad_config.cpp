// Phase 4 T004: PadConfig struct and helper function tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/pad_config.h"

using namespace Membrum;

// ==============================================================================
// Constants
// ==============================================================================

TEST_CASE("PadConfig constants are correct", "[pad_config]")
{
    CHECK(kNumPads == 32);
    CHECK(kPadBaseId == 1000);
    CHECK(kPadParamStride == 64);
    CHECK(kMaxOutputBuses == 16);
    CHECK(kFirstDrumNote == 36);
    CHECK(kLastDrumNote == 67);
}

// ==============================================================================
// PadParamOffset enum values
// ==============================================================================

TEST_CASE("PadParamOffset enum has correct values", "[pad_config]")
{
    CHECK(kPadExciterType == 0);
    CHECK(kPadBodyModel == 1);
    CHECK(kPadMaterial == 2);
    CHECK(kPadSize == 3);
    CHECK(kPadDecay == 4);
    CHECK(kPadStrikePosition == 5);
    CHECK(kPadLevel == 6);
    CHECK(kPadTSFilterType == 7);
    CHECK(kPadTSFilterCutoff == 8);
    CHECK(kPadTSFilterResonance == 9);
    CHECK(kPadTSFilterEnvAmount == 10);
    CHECK(kPadTSDriveAmount == 11);
    CHECK(kPadTSFoldAmount == 12);
    CHECK(kPadTSPitchEnvStart == 13);
    CHECK(kPadTSPitchEnvEnd == 14);
    CHECK(kPadTSPitchEnvTime == 15);
    CHECK(kPadTSPitchEnvCurve == 16);
    CHECK(kPadTSFilterEnvAttack == 17);
    CHECK(kPadTSFilterEnvDecay == 18);
    CHECK(kPadTSFilterEnvSustain == 19);
    CHECK(kPadTSFilterEnvRelease == 20);
    CHECK(kPadModeStretch == 21);
    CHECK(kPadDecaySkew == 22);
    CHECK(kPadModeInjectAmount == 23);
    CHECK(kPadNonlinearCoupling == 24);
    CHECK(kPadMorphEnabled == 25);
    CHECK(kPadMorphStart == 26);
    CHECK(kPadMorphEnd == 27);
    CHECK(kPadMorphDuration == 28);
    CHECK(kPadMorphCurve == 29);
    CHECK(kPadChokeGroup == 30);
    CHECK(kPadOutputBus == 31);
    CHECK(kPadFMRatio == 32);
    CHECK(kPadFeedbackAmount == 33);
    CHECK(kPadNoiseBurstDuration == 34);
    CHECK(kPadFrictionPressure == 35);
    CHECK(kPadActiveParamCount == 36);
}

// ==============================================================================
// PadConfig default values
// ==============================================================================

TEST_CASE("PadConfig default-constructed has correct defaults", "[pad_config]")
{
    PadConfig pad{};

    // Selectors
    CHECK(pad.exciterType == ExciterType::Impulse);
    CHECK(pad.bodyModel == BodyModelType::Membrane);

    // Core sound params
    CHECK(pad.material == Catch::Approx(0.5f).margin(1e-6f));
    CHECK(pad.size == Catch::Approx(0.5f).margin(1e-6f));
    CHECK(pad.decay == Catch::Approx(0.3f).margin(1e-6f));
    CHECK(pad.strikePosition == Catch::Approx(0.3f).margin(1e-6f));
    CHECK(pad.level == Catch::Approx(0.8f).margin(1e-6f));

    // Tone Shaper
    CHECK(pad.tsFilterType == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsFilterCutoff == Catch::Approx(1.0f).margin(1e-6f));
    CHECK(pad.tsFilterResonance == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsFilterEnvAmount == Catch::Approx(0.5f).margin(1e-6f));
    CHECK(pad.tsDriveAmount == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsFoldAmount == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsPitchEnvStart == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsPitchEnvEnd == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsPitchEnvTime == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsPitchEnvCurve == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsFilterEnvAttack == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsFilterEnvDecay == Catch::Approx(0.1f).margin(1e-6f));
    CHECK(pad.tsFilterEnvSustain == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.tsFilterEnvRelease == Catch::Approx(0.1f).margin(1e-6f));

    // Unnatural Zone
    CHECK(pad.modeStretch == Catch::Approx(0.333333f).margin(1e-5f));
    CHECK(pad.decaySkew == Catch::Approx(0.5f).margin(1e-6f));
    CHECK(pad.modeInjectAmount == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.nonlinearCoupling == Catch::Approx(0.0f).margin(1e-6f));

    // Material Morph
    CHECK(pad.morphEnabled == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.morphStart == Catch::Approx(1.0f).margin(1e-6f));
    CHECK(pad.morphEnd == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.morphDuration == Catch::Approx(0.095477f).margin(1e-5f));
    CHECK(pad.morphCurve == Catch::Approx(0.0f).margin(1e-6f));

    // Kit-level per-pad settings
    CHECK(pad.chokeGroup == 0);
    CHECK(pad.outputBus == 0);

    // Exciter secondary params
    CHECK(pad.fmRatio == Catch::Approx(0.5f).margin(1e-6f));
    CHECK(pad.feedbackAmount == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(pad.noiseBurstDuration == Catch::Approx(0.5f).margin(1e-6f));
    CHECK(pad.frictionPressure == Catch::Approx(0.0f).margin(1e-6f));
}

// ==============================================================================
// padParamId() computation
// ==============================================================================

TEST_CASE("padParamId computes correct IDs", "[pad_config]")
{
    // Pad 0, offset 0 => 1000
    CHECK(padParamId(0, 0) == 1000);

    // Pad 0, offset 35 (last active) => 1035
    CHECK(padParamId(0, 35) == 1035);

    // Pad 1, offset 0 => 1064
    CHECK(padParamId(1, 0) == 1064);

    // Pad 31, offset 0 => 1000 + 31*64 = 2984
    CHECK(padParamId(31, 0) == 2984);

    // Pad 31, offset 35 => 2984 + 35 = 3019
    CHECK(padParamId(31, 35) == 3019);

    // Stride check: consecutive pads differ by 64
    CHECK(padParamId(1, 0) - padParamId(0, 0) == kPadParamStride);
    CHECK(padParamId(5, 0) - padParamId(4, 0) == kPadParamStride);
}

// ==============================================================================
// padIndexFromParamId() round-trips
// ==============================================================================

TEST_CASE("padIndexFromParamId extracts correct pad index", "[pad_config]")
{
    // Pad 0
    CHECK(padIndexFromParamId(padParamId(0, 0)) == 0);
    CHECK(padIndexFromParamId(padParamId(0, 35)) == 0);

    // Pad 15 (middle)
    CHECK(padIndexFromParamId(padParamId(15, 10)) == 15);

    // Pad 31 (last)
    CHECK(padIndexFromParamId(padParamId(31, 0)) == 31);
    CHECK(padIndexFromParamId(padParamId(31, 35)) == 31);
}

TEST_CASE("padIndexFromParamId rejects out-of-range IDs", "[pad_config]")
{
    // Below kPadBaseId
    CHECK(padIndexFromParamId(0) == -1);
    CHECK(padIndexFromParamId(999) == -1);

    // Above pad 31's range: 1000 + 32*64 = 3048
    CHECK(padIndexFromParamId(3048) == -1);
    CHECK(padIndexFromParamId(5000) == -1);

    // Negative (cast to int)
    CHECK(padIndexFromParamId(-1) == -1);
}

// ==============================================================================
// padOffsetFromParamId() round-trips
// ==============================================================================

TEST_CASE("padOffsetFromParamId extracts correct offset", "[pad_config]")
{
    // First active offset
    CHECK(padOffsetFromParamId(padParamId(0, 0)) == 0);

    // Last active offset (35)
    CHECK(padOffsetFromParamId(padParamId(0, 35)) == 35);

    // Middle offset for pad 15
    CHECK(padOffsetFromParamId(padParamId(15, 20)) == 20);

    // Last pad, last offset
    CHECK(padOffsetFromParamId(padParamId(31, 35)) == 35);
}

TEST_CASE("padOffsetFromParamId rejects reserved offsets", "[pad_config]")
{
    // Offset 36 is valid (Phase 5 kPadCouplingAmount)
    int couplingId = kPadBaseId + 36;  // pad 0, offset 36
    CHECK(padOffsetFromParamId(couplingId) == 36);

    // Offsets 37-41 are now valid Phase 6 macro fields.
    for (int off = 37; off <= 41; ++off) {
        const int macroId = kPadBaseId + off;
        CHECK(padOffsetFromParamId(macroId) == off);
    }

    // Offsets 42-49 are valid Phase 7 noise/click-layer fields.
    for (int off = 42; off <= 49; ++off) {
        const int layerId = kPadBaseId + off;
        CHECK(padOffsetFromParamId(layerId) == off);
    }

    // Offsets 50-51 are valid Phase 8A per-mode damping fields.
    for (int off = 50; off <= 51; ++off) {
        const int dampId = kPadBaseId + off;
        CHECK(padOffsetFromParamId(dampId) == off);
    }

    // Offsets 52-53 are valid Phase 8C fields (airLoading, modeScatter).
    for (int off = 52; off <= 53; ++off) {
        const int acId = kPadBaseId + off;
        CHECK(padOffsetFromParamId(acId) == off);
    }

    // Offset 54 is the first reserved offset within pad 0's stride (Phase 8C).
    int reservedId = kPadBaseId + 54;
    CHECK(padOffsetFromParamId(reservedId) == -1);

    // Offset 63 (last in stride)
    int lastReservedId = kPadBaseId + 63;
    CHECK(padOffsetFromParamId(lastReservedId) == -1);

    // Same for pad 5 offset 54+
    int pad5Reserved = padParamId(5, 0) + 54;
    CHECK(padOffsetFromParamId(pad5Reserved) == -1);
}

TEST_CASE("padOffsetFromParamId rejects out-of-range IDs", "[pad_config]")
{
    CHECK(padOffsetFromParamId(0) == -1);
    CHECK(padOffsetFromParamId(999) == -1);
    CHECK(padOffsetFromParamId(3048) == -1);
}

// ==============================================================================
// Full round-trip: padParamId -> padIndexFromParamId + padOffsetFromParamId
// ==============================================================================

TEST_CASE("padParamId round-trips through index and offset extractors", "[pad_config]")
{
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        for (int offset = 0; offset < kPadActiveParamCount; ++offset)
        {
            int id = padParamId(pad, offset);
            CAPTURE(pad, offset, id);
            CHECK(padIndexFromParamId(id) == pad);
            CHECK(padOffsetFromParamId(id) == offset);
        }
    }
}

// ==============================================================================
// constexpr verification
// ==============================================================================

TEST_CASE("padParamId helpers are constexpr", "[pad_config]")
{
    // These should compile as constexpr
    constexpr int id = padParamId(0, 0);
    constexpr int idx = padIndexFromParamId(id);
    constexpr int off = padOffsetFromParamId(id);
    CHECK(id == 1000);
    CHECK(idx == 0);
    CHECK(off == 0);
}
