// ==============================================================================
// test_default_kit.cpp -- Phase 4 GM Default Kit Template Tests (T020)
// ==============================================================================
// FR-030: All 32 pads initialized with GM-inspired defaults on first load
// FR-031: 6 template archetypes (Kick, Snare, Tom, Hat, Cymbal, Perc)
// FR-032: Choke group 1 for hats (MIDI 42, 44, 46)
// FR-033: Tom pads have progressively increasing Size values
// SC-009: Default kit produces recognizable drum sounds
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/pad_config.h"
#include "dsp/default_kit.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"

#include <array>
#include <cmath>
#include <cstring>

using namespace Membrum;
using Catch::Approx;

// Helper: pad index from MIDI note
static constexpr int pad(int midiNote) { return midiNote - 36; }

// Helper: check float is finite (no NaN, no Inf) using bit manipulation
// (safe under -ffast-math)
static bool isFiniteBits(float v)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    // IEEE 754: exponent bits 23-30 all set = Inf or NaN
    return (bits & 0x7F800000u) != 0x7F800000u;
}

TEST_CASE("DefaultKit::apply initializes all 32 pads", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    SECTION("All 32 pads have finite core parameters")
    {
        for (int i = 0; i < kNumPads; ++i) {
            INFO("Pad index: " << i);
            REQUIRE(isFiniteBits(pads[i].material));
            REQUIRE(isFiniteBits(pads[i].size));
            REQUIRE(isFiniteBits(pads[i].decay));
            REQUIRE(isFiniteBits(pads[i].strikePosition));
            REQUIRE(isFiniteBits(pads[i].level));
        }
    }

    SECTION("All 32 pads have valid enum ranges")
    {
        for (int i = 0; i < kNumPads; ++i) {
            INFO("Pad index: " << i);
            REQUIRE(static_cast<int>(pads[i].exciterType) >= 0);
            REQUIRE(static_cast<int>(pads[i].exciterType) < static_cast<int>(ExciterType::kCount));
            REQUIRE(static_cast<int>(pads[i].bodyModel) >= 0);
            REQUIRE(static_cast<int>(pads[i].bodyModel) < static_cast<int>(BodyModelType::kCount));
        }
    }

    SECTION("All 32 pads have outputBus = 0 (main)")
    {
        for (int i = 0; i < kNumPads; ++i) {
            INFO("Pad index: " << i);
            REQUIRE(pads[i].outputBus == 0);
        }
    }
}

// ---------------------------------------------------------------------------
// FR-031: Kick template (Pad 1 = MIDI 36)
// ---------------------------------------------------------------------------
TEST_CASE("DefaultKit Kick template - pad 0 (MIDI 36)", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    const auto& kick = pads[pad(36)];
    REQUIRE(kick.exciterType == ExciterType::Impulse);
    REQUIRE(kick.bodyModel == BodyModelType::Membrane);
    REQUIRE(kick.material == Approx(0.3f).margin(0.01f));
    REQUIRE(kick.size == Approx(0.8f).margin(0.01f));
    REQUIRE(kick.decay == Approx(0.3f).margin(0.01f));
    REQUIRE(kick.strikePosition == Approx(0.3f).margin(0.01f));
    REQUIRE(kick.level == Approx(0.8f).margin(0.01f));

    // Pitch envelope: 160->50Hz, 20ms (normalized values)
    // Hz = 20 * pow(100, normalized), so normalized = log(Hz/20)/log(100)
    // 160 Hz -> log(8)/log(100) = 0.4515
    // 50 Hz  -> log(2.5)/log(100) = 0.1990
    // 20 ms  -> 20/500 = 0.04
    REQUIRE(kick.tsPitchEnvStart > 0.0f);  // pitch env is enabled
    REQUIRE(kick.tsPitchEnvEnd > 0.0f);
    REQUIRE(kick.tsPitchEnvTime > 0.0f);

    REQUIRE(kick.chokeGroup == 0);
}

// ---------------------------------------------------------------------------
// FR-031: Snare template (Pad 3 = MIDI 38, Pad 5 = MIDI 40)
// ---------------------------------------------------------------------------
TEST_CASE("DefaultKit Snare template - pad 2 (MIDI 38)", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    const auto& snare = pads[pad(38)];
    REQUIRE(snare.exciterType == ExciterType::NoiseBurst);
    REQUIRE(snare.bodyModel == BodyModelType::Membrane);
    REQUIRE(snare.material == Approx(0.5f).margin(0.01f));
    REQUIRE(snare.size == Approx(0.5f).margin(0.01f));
    REQUIRE(snare.decay == Approx(0.4f).margin(0.01f));
    REQUIRE(snare.level == Approx(0.8f).margin(0.01f));

    // NoiseBurstDuration = 8ms -> normalized = (8-2)/13 = 0.461538
    REQUIRE(snare.noiseBurstDuration == Approx(0.461538f).margin(0.01f));

    REQUIRE(snare.chokeGroup == 0);
}

TEST_CASE("DefaultKit Electric Snare - pad 4 (MIDI 40)", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    const auto& snare2 = pads[pad(40)];
    REQUIRE(snare2.exciterType == ExciterType::NoiseBurst);
    REQUIRE(snare2.bodyModel == BodyModelType::Membrane);
    REQUIRE(snare2.chokeGroup == 0);
}

// ---------------------------------------------------------------------------
// FR-031: Hat template (Pads 7, 9, 11 = MIDI 42, 44, 46)
// FR-032: Hats in choke group 1
// ---------------------------------------------------------------------------
TEST_CASE("DefaultKit Hat template - closed hat pad 6 (MIDI 42)", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    const auto& hat = pads[pad(42)];
    REQUIRE(hat.exciterType == ExciterType::NoiseBurst);
    REQUIRE(hat.bodyModel == BodyModelType::NoiseBody);
    REQUIRE(hat.material == Approx(0.9f).margin(0.01f));
    REQUIRE(hat.size == Approx(0.15f).margin(0.01f));
    REQUIRE(hat.decay == Approx(0.1f).margin(0.01f));
    REQUIRE(hat.level == Approx(0.8f).margin(0.01f));

    // NoiseBurstDuration = 3ms -> normalized = (3-2)/13 = 0.076923
    REQUIRE(hat.noiseBurstDuration == Approx(0.076923f).margin(0.01f));
}

TEST_CASE("DefaultKit Hat choke group 1 - MIDI 42, 44, 46", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    REQUIRE(pads[pad(42)].chokeGroup == 1);  // Closed hi-hat
    REQUIRE(pads[pad(44)].chokeGroup == 1);  // Pedal hi-hat
    REQUIRE(pads[pad(46)].chokeGroup == 1);  // Open hi-hat
}

TEST_CASE("DefaultKit non-hat pads have choke group 0", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    for (int note = 36; note <= 67; ++note) {
        if (note == 42 || note == 44 || note == 46)
            continue;  // skip hats
        INFO("MIDI note: " << note);
        REQUIRE(pads[pad(note)].chokeGroup == 0);
    }
}

// ---------------------------------------------------------------------------
// FR-031: Cymbal template (MIDI 49, 51, 52, 53, 55, 57, 59)
// ---------------------------------------------------------------------------
TEST_CASE("DefaultKit Cymbal template - crash pad 13 (MIDI 49)", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    const auto& cymbal = pads[pad(49)];
    REQUIRE(cymbal.exciterType == ExciterType::NoiseBurst);
    REQUIRE(cymbal.bodyModel == BodyModelType::NoiseBody);
    REQUIRE(cymbal.material == Approx(0.95f).margin(0.01f));
    REQUIRE(cymbal.size == Approx(0.3f).margin(0.01f));
    REQUIRE(cymbal.decay == Approx(0.8f).margin(0.01f));
    REQUIRE(cymbal.level == Approx(0.8f).margin(0.01f));

    // NoiseBurstDuration = 10ms -> normalized = (10-2)/13 = 0.615385
    REQUIRE(cymbal.noiseBurstDuration == Approx(0.615385f).margin(0.01f));

    REQUIRE(cymbal.chokeGroup == 0);
}

// ---------------------------------------------------------------------------
// FR-031: Perc template (MIDI 37, 39, 54, 56, 58, 60-67)
// ---------------------------------------------------------------------------
TEST_CASE("DefaultKit Perc template - side stick pad 1 (MIDI 37)", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    const auto& perc = pads[pad(37)];
    REQUIRE(perc.exciterType == ExciterType::Mallet);
    REQUIRE(perc.bodyModel == BodyModelType::Plate);
    REQUIRE(perc.material == Approx(0.7f).margin(0.01f));
    REQUIRE(perc.size == Approx(0.3f).margin(0.01f));
    REQUIRE(perc.decay == Approx(0.3f).margin(0.01f));
    REQUIRE(perc.level == Approx(0.8f).margin(0.01f));
    REQUIRE(perc.chokeGroup == 0);
}

// ---------------------------------------------------------------------------
// FR-033: Tom pads have progressively increasing Size values
// High Tom (50) Size=0.4, Hi-Mid Tom (48) Size=0.45, Low-Mid Tom (47) Size=0.5,
// Low Tom (45) Size=0.6, High Floor Tom (43) Size=0.7, Low Floor Tom (41) Size=0.8
// ---------------------------------------------------------------------------
TEST_CASE("DefaultKit Tom size progression (FR-033)", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    // All toms should use Mallet + Membrane
    const int tomNotes[] = {41, 43, 45, 47, 48, 50};
    for (int note : tomNotes) {
        INFO("MIDI note: " << note);
        REQUIRE(pads[pad(note)].exciterType == ExciterType::Mallet);
        REQUIRE(pads[pad(note)].bodyModel == BodyModelType::Membrane);
        REQUIRE(pads[pad(note)].decay == Approx(0.5f).margin(0.01f));
        REQUIRE(pads[pad(note)].material == Approx(0.4f).margin(0.01f));
    }

    // Specific size values per FR-033
    REQUIRE(pads[pad(50)].size == Approx(0.4f).margin(0.01f));   // High Tom
    REQUIRE(pads[pad(48)].size == Approx(0.45f).margin(0.01f));  // Hi-Mid Tom
    REQUIRE(pads[pad(47)].size == Approx(0.5f).margin(0.01f));   // Low-Mid Tom
    REQUIRE(pads[pad(45)].size == Approx(0.6f).margin(0.01f));   // Low Tom
    REQUIRE(pads[pad(43)].size == Approx(0.7f).margin(0.01f));   // High Floor Tom
    REQUIRE(pads[pad(41)].size == Approx(0.8f).margin(0.01f));   // Low Floor Tom

    // Verify progressive ordering: higher MIDI note = smaller Size
    REQUIRE(pads[pad(50)].size < pads[pad(48)].size);
    REQUIRE(pads[pad(48)].size < pads[pad(47)].size);
    REQUIRE(pads[pad(47)].size < pads[pad(45)].size);
    REQUIRE(pads[pad(45)].size < pads[pad(43)].size);
    REQUIRE(pads[pad(43)].size < pads[pad(41)].size);
}

// ---------------------------------------------------------------------------
// FR-031: Tom template base parameters
// ---------------------------------------------------------------------------
TEST_CASE("DefaultKit Tom template base parameters", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    // All toms should have strikePosition=0.3, level=0.8
    const int tomNotes[] = {41, 43, 45, 47, 48, 50};
    for (int note : tomNotes) {
        INFO("MIDI note: " << note);
        REQUIRE(pads[pad(note)].strikePosition == Approx(0.3f).margin(0.01f));
        REQUIRE(pads[pad(note)].level == Approx(0.8f).margin(0.01f));
    }
}

// ---------------------------------------------------------------------------
// FR-030: All tone shaper params are finite across all 32 pads
// ---------------------------------------------------------------------------
TEST_CASE("DefaultKit all parameters finite across all 32 pads", "[default_kit]")
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    for (int i = 0; i < kNumPads; ++i) {
        INFO("Pad index: " << i);
        REQUIRE(isFiniteBits(pads[i].tsFilterCutoff));
        REQUIRE(isFiniteBits(pads[i].tsFilterResonance));
        REQUIRE(isFiniteBits(pads[i].tsFilterEnvAmount));
        REQUIRE(isFiniteBits(pads[i].tsDriveAmount));
        REQUIRE(isFiniteBits(pads[i].tsFoldAmount));
        REQUIRE(isFiniteBits(pads[i].tsPitchEnvStart));
        REQUIRE(isFiniteBits(pads[i].tsPitchEnvEnd));
        REQUIRE(isFiniteBits(pads[i].tsPitchEnvTime));
        REQUIRE(isFiniteBits(pads[i].modeStretch));
        REQUIRE(isFiniteBits(pads[i].decaySkew));
        REQUIRE(isFiniteBits(pads[i].modeInjectAmount));
        REQUIRE(isFiniteBits(pads[i].nonlinearCoupling));
        REQUIRE(isFiniteBits(pads[i].fmRatio));
        REQUIRE(isFiniteBits(pads[i].feedbackAmount));
        REQUIRE(isFiniteBits(pads[i].noiseBurstDuration));
        REQUIRE(isFiniteBits(pads[i].frictionPressure));
    }
}
