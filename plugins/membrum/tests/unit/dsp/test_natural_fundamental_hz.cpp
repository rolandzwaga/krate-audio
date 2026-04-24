// ==============================================================================
// Body-aware natural fundamental frequency
// ==============================================================================
// Regression test for the audit finding that DrumVoice hard-coded the membrane
// f0 formula (500 * 0.1^size) regardless of the active body model. The correct
// behaviour is that `naturalFundamentalHz_` -- which feeds ToneShaper's pitch-
// envelope baseline and ModeInject's harmonic series -- must match the body
// type's own Size->f0 curve.
//
// Expected per-body bases (size = 0.5, so sizeDecay = sqrt(0.1) ≈ 0.31623):
//   Membrane :  500 * sqrt(0.1) ≈ 158.11 Hz
//   Plate    :  800 * sqrt(0.1) ≈ 252.98 Hz
//   Shell    : 1500 * sqrt(0.1) ≈ 474.34 Hz
//   String   :  800 * sqrt(0.1) ≈ 252.98 Hz
//   Bell     :  800 * sqrt(0.1) ≈ 252.98 Hz
//   NoiseBody: 1500 * sqrt(0.1) ≈ 474.34 Hz
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/bodies/natural_fundamental.h"
#include "dsp/body_model_type.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"

using Catch::Approx;
using Membrum::BodyModelType;
using Membrum::ExciterType;
using Membrum::Bodies::naturalFundamentalHz;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr float  kSize       = 0.5f;     // sqrt(0.1) ≈ 0.31623
constexpr float  kVelocity   = 0.8f;

// Reference constants per naturalFundamental.h. If the mapper formulas change,
// update these and the comment above.
constexpr float kBaseMembrane  =  500.0f;
constexpr float kBasePlate     =  800.0f;
constexpr float kBaseShell     = 1500.0f;
constexpr float kBaseString    =  800.0f;
constexpr float kBaseBell      =  800.0f;
constexpr float kBaseNoiseBody = 1500.0f;

// Helper: trigger noteOn on a freshly prepared DrumVoice with a given body
// type and return the voice's natural fundamental.
[[nodiscard]] float f0AfterNoteOn(BodyModelType type)
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate, /*voiceId*/ 0u);
    voice.setSize(kSize);
    voice.setExciterType(ExciterType::Impulse);
    voice.setBodyModel(type);
    voice.noteOn(kVelocity);
    return voice.getNaturalFundamentalHz();
}

} // namespace

TEST_CASE("naturalFundamentalHz helper matches per-body Size->f0 formulas",
          "[natural_fundamental][physics]")
{
    const float sizeDecay = std::pow(0.1f, kSize);

    REQUIRE(naturalFundamentalHz(BodyModelType::Membrane,  kSize)
            == Approx(kBaseMembrane  * sizeDecay).epsilon(1e-5));
    REQUIRE(naturalFundamentalHz(BodyModelType::Plate,     kSize)
            == Approx(kBasePlate     * sizeDecay).epsilon(1e-5));
    REQUIRE(naturalFundamentalHz(BodyModelType::Shell,     kSize)
            == Approx(kBaseShell     * sizeDecay).epsilon(1e-5));
    REQUIRE(naturalFundamentalHz(BodyModelType::String,    kSize)
            == Approx(kBaseString    * sizeDecay).epsilon(1e-5));
    REQUIRE(naturalFundamentalHz(BodyModelType::Bell,      kSize)
            == Approx(kBaseBell      * sizeDecay).epsilon(1e-5));
    REQUIRE(naturalFundamentalHz(BodyModelType::NoiseBody, kSize)
            == Approx(kBaseNoiseBody * sizeDecay).epsilon(1e-5));
}

TEST_CASE("naturalFundamentalHz clamps size into [0, 1]",
          "[natural_fundamental][physics]")
{
    // size = 0  -> sizeDecay = 1.0 -> base Hz directly
    REQUIRE(naturalFundamentalHz(BodyModelType::Membrane, 0.0f)
            == Approx(kBaseMembrane).epsilon(1e-5));
    REQUIRE(naturalFundamentalHz(BodyModelType::Shell,    0.0f)
            == Approx(kBaseShell).epsilon(1e-5));

    // size = 1 -> sizeDecay = 0.1 -> base * 0.1
    REQUIRE(naturalFundamentalHz(BodyModelType::Membrane, 1.0f)
            == Approx(kBaseMembrane * 0.1f).epsilon(1e-5));
    REQUIRE(naturalFundamentalHz(BodyModelType::Shell,    1.0f)
            == Approx(kBaseShell    * 0.1f).epsilon(1e-5));

    // Out-of-range inputs clamp to the endpoints.
    REQUIRE(naturalFundamentalHz(BodyModelType::Plate, -0.5f)
            == Approx(kBasePlate).epsilon(1e-5));
    REQUIRE(naturalFundamentalHz(BodyModelType::Plate,  2.0f)
            == Approx(kBasePlate * 0.1f).epsilon(1e-5));
}

TEST_CASE("DrumVoice::naturalFundamentalHz is Membrane base after noteOn",
          "[drum_voice][natural_fundamental][physics]")
{
    // Membrane is the unchanged Phase 1 baseline; this test guards against
    // accidental regression of the legacy path.
    const float expected = kBaseMembrane * std::pow(0.1f, kSize);
    REQUIRE(f0AfterNoteOn(BodyModelType::Membrane)
            == Approx(expected).epsilon(1e-5));
}

TEST_CASE("DrumVoice::naturalFundamentalHz tracks the active body type",
          "[drum_voice][natural_fundamental][physics]")
{
    // Regression for the bug where every body type got the membrane formula.
    // After the fix, each body must expose its own f0 curve.
    const float decay = std::pow(0.1f, kSize);

    REQUIRE(f0AfterNoteOn(BodyModelType::Plate)
            == Approx(kBasePlate * decay).epsilon(1e-5));

    REQUIRE(f0AfterNoteOn(BodyModelType::Shell)
            == Approx(kBaseShell * decay).epsilon(1e-5));

    REQUIRE(f0AfterNoteOn(BodyModelType::String)
            == Approx(kBaseString * decay).epsilon(1e-5));

    REQUIRE(f0AfterNoteOn(BodyModelType::Bell)
            == Approx(kBaseBell * decay).epsilon(1e-5));

    REQUIRE(f0AfterNoteOn(BodyModelType::NoiseBody)
            == Approx(kBaseNoiseBody * decay).epsilon(1e-5));
}
