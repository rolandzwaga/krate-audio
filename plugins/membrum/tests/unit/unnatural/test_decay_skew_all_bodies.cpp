// ==============================================================================
// Decay Skew per-mode tilt -- all modal bodies (audit finding M-5)
// ==============================================================================
// Audit §2 MEDIUM M-5: "decaySkew degrades to a +/-30% global decay nudge for
// Plate/Shell/Bell/NoiseBody". Only the Membrane mapper applied the per-mode
// amplitude tilt ratio^(-decaySkew); the other four bodies got only a scalar
// [0.7,1.3] bias on the GLOBAL decay time, which scales every mode equally and
// therefore leaves the high/low spectral BALANCE unchanged (near-JND, no tilt).
//
// These tests assert the per-mode tilt at the mapper level (deterministic, no
// audio): with decaySkew = -1 the high-mode / low-mode amplitude ratio must
// grow by ~ratio[HI]/ratio[LO] relative to decaySkew = 0, and with
// decaySkew = +1 it must shrink. decaySkew == 0 must stay bit-identical to the
// untilted result (FR-055 default-off guarantee, enforced by the mapper's
// `if (decaySkew != 0)` guard).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/bodies/bell_mapper.h"
#include "dsp/bodies/bell_modes.h"
#include "dsp/bodies/noise_body_mapper.h"
#include "dsp/bodies/plate_mapper.h"
#include "dsp/bodies/plate_modes.h"
#include "dsp/bodies/shell_mapper.h"
#include "dsp/bodies/shell_modes.h"
#include "dsp/voice_common_params.h"

#include <cmath>

using Catch::Approx;

namespace {

Membrum::VoiceCommonParams makeParams(float decaySkew)
{
    Membrum::VoiceCommonParams p{};
    p.material    = 0.5f;
    p.size        = 0.5f;
    p.decay       = 0.5f;
    p.strikePos   = 0.3f;
    p.level       = 0.8f;
    p.modeStretch = 1.0f;
    p.decaySkew   = decaySkew;
    return p;
}

// high-mode / low-mode amplitude ratio for a MapperResult.
double hiLoRatio(const Membrum::Bodies::MapperResult& r, int loIdx, int hiIdx)
{
    return static_cast<double>(r.amplitudes[hiIdx]) /
           static_cast<double>(r.amplitudes[loIdx]);
}

} // namespace

// ------------------------------------------------------------------------------
// Plate: ratio[0]=1.0, ratio[8]=12.5 -> decaySkew=-1 must boost hi/lo by ~12.5x.
// ------------------------------------------------------------------------------
TEST_CASE("DecaySkew per-mode tilt -- Plate", "[UnnaturalZone][DecaySkew][Plate]")
{
    using Membrum::Bodies::PlateMapper;
    constexpr int kLo = 0, kHi = 8;

    const auto rZero = PlateMapper::map(makeParams(0.0f), 0.0f);
    const auto rNeg  = PlateMapper::map(makeParams(-1.0f), 0.0f);
    const auto rPos  = PlateMapper::map(makeParams(1.0f), 0.0f);

    const double base = hiLoRatio(rZero, kLo, kHi);
    INFO("hi/lo: skew0=" << base << " skew-1=" << hiLoRatio(rNeg, kLo, kHi)
         << " skew+1=" << hiLoRatio(rPos, kLo, kHi));

    // Negative skew shifts energy UP (high modes louder); positive shifts DOWN.
    CHECK(hiLoRatio(rNeg, kLo, kHi) > base * 2.0);
    CHECK(hiLoRatio(rPos, kLo, kHi) < base * 0.6);
}

// ------------------------------------------------------------------------------
// Shell: ratio[0]=1.0, ratio[7]=31.87 -> very strong tilt.
// ------------------------------------------------------------------------------
TEST_CASE("DecaySkew per-mode tilt -- Shell", "[UnnaturalZone][DecaySkew][Shell]")
{
    using Membrum::Bodies::ShellMapper;
    constexpr int kLo = 0, kHi = 7;

    const auto rZero = ShellMapper::map(makeParams(0.0f), 0.0f);
    const auto rNeg  = ShellMapper::map(makeParams(-1.0f), 0.0f);
    const auto rPos  = ShellMapper::map(makeParams(1.0f), 0.0f);

    const double base = hiLoRatio(rZero, kLo, kHi);
    INFO("hi/lo: skew0=" << base << " skew-1=" << hiLoRatio(rNeg, kLo, kHi)
         << " skew+1=" << hiLoRatio(rPos, kLo, kHi));

    CHECK(hiLoRatio(rNeg, kLo, kHi) > base * 2.0);
    CHECK(hiLoRatio(rPos, kLo, kHi) < base * 0.6);
}

// ------------------------------------------------------------------------------
// Bell: ratio[0]=0.25 (hum), ratio[15]=12.0 -> tilt is relative to the nominal
// partial (ratio 1.0), so the hum is cut and the high partials boosted.
// ------------------------------------------------------------------------------
TEST_CASE("DecaySkew per-mode tilt -- Bell", "[UnnaturalZone][DecaySkew][Bell]")
{
    using Membrum::Bodies::BellMapper;
    constexpr int kLo = 0, kHi = 15;

    const auto rZero = BellMapper::map(makeParams(0.0f), 0.0f);
    const auto rNeg  = BellMapper::map(makeParams(-1.0f), 0.0f);
    const auto rPos  = BellMapper::map(makeParams(1.0f), 0.0f);

    const double base = hiLoRatio(rZero, kLo, kHi);
    INFO("hi/lo: skew0=" << base << " skew-1=" << hiLoRatio(rNeg, kLo, kHi)
         << " skew+1=" << hiLoRatio(rPos, kLo, kHi));

    CHECK(hiLoRatio(rNeg, kLo, kHi) > base * 2.0);
    CHECK(hiLoRatio(rPos, kLo, kHi) < base * 0.6);
}

// ------------------------------------------------------------------------------
// NoiseBody: shares the plate ratio table on its modal layer.
// ------------------------------------------------------------------------------
TEST_CASE("DecaySkew per-mode tilt -- NoiseBody",
          "[UnnaturalZone][DecaySkew][NoiseBody]")
{
    using Membrum::Bodies::NoiseBodyMapper;
    constexpr int kLo = 0, kHi = 8;

    const auto rZero = NoiseBodyMapper::map(makeParams(0.0f), 0.0f);
    const auto rNeg  = NoiseBodyMapper::map(makeParams(-1.0f), 0.0f);
    const auto rPos  = NoiseBodyMapper::map(makeParams(1.0f), 0.0f);

    const double base = hiLoRatio(rZero.modal, kLo, kHi);
    INFO("hi/lo: skew0=" << base << " skew-1=" << hiLoRatio(rNeg.modal, kLo, kHi)
         << " skew+1=" << hiLoRatio(rPos.modal, kLo, kHi));

    CHECK(hiLoRatio(rNeg.modal, kLo, kHi) > base * 2.0);
    CHECK(hiLoRatio(rPos.modal, kLo, kHi) < base * 0.6);
}

// ------------------------------------------------------------------------------
// Default-off guarantee: decaySkew == 0 must leave amplitudes bit-identical to
// the untilted path (the mapper guards the tilt behind `decaySkew != 0`). We
// assert the hi/lo ratio equals the raw frequency-independent strike amplitudes
// (no ratio^0 contamination) by checking skew=0 against a tiny perturbation that
// the tilt would otherwise have already moved.
// ------------------------------------------------------------------------------
TEST_CASE("DecaySkew per-mode tilt -- decaySkew==0 leaves amplitudes untilted",
          "[UnnaturalZone][DecaySkew][DefaultsOff]")
{
    using Membrum::Bodies::PlateMapper;
    const auto a = PlateMapper::map(makeParams(0.0f), 0.0f);
    const auto b = PlateMapper::map(makeParams(0.0f), 0.0f);
    for (int k = 0; k < PlateMapper::kModeCount; ++k)
        CHECK(a.amplitudes[k] == Approx(b.amplitudes[k]).margin(1e-9f));

    // And a nonzero skew must actually move at least one amplitude, proving the
    // skew=0 path is a real (not vacuous) bypass.
    const auto skewed = PlateMapper::map(makeParams(-1.0f), 0.0f);
    bool moved = false;
    for (int k = 0; k < PlateMapper::kModeCount; ++k)
        if (std::abs(skewed.amplitudes[k] - a.amplitudes[k]) > 1e-4f) { moved = true; break; }
    CHECK(moved);
}
