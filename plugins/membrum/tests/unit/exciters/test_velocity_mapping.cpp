// ==============================================================================
// Velocity → spectral-centroid mapping for all 6 exciters (Phase 3 -- T036)
// ==============================================================================
// FR-016 / SC-004: For each of the 6 exciter types, process at velocity 0.23
// and velocity 1.0 and assert spectral centroid ratio ≥ 2.0.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/exciter_bank.h"
#include "dsp/exciter_type.h"
#include "exciter_test_helpers.h"

#include <array>

using membrum_exciter_tests::spectralCentroidDFT;

namespace {

struct CentroidResult
{
    Membrum::ExciterType type;
    float                centLow;
    float                centHigh;
    float                ratio;
};

CentroidResult measureCentroidRatio(Membrum::ExciterType type, double sr, int numSamples)
{
    Membrum::ExciterBank bank;
    bank.prepare(sr, 0);
    bank.setExciterType(type);

    std::array<float, 441> bufLow{};
    std::array<float, 441> bufHigh{};
    // 441 samples = 10 ms at 44.1 kHz
    REQUIRE(numSamples <= static_cast<int>(bufLow.size()));

    bank.trigger(0.23f);
    for (int i = 0; i < numSamples; ++i)
        bufLow[i] = bank.process(0.0f);

    // Need to force a clean re-trigger for high velocity. ExciterBank's swap
    // pattern doesn't auto-reset between triggers, so use a separate bank.
    Membrum::ExciterBank bank2;
    bank2.prepare(sr, 0);
    bank2.setExciterType(type);
    bank2.trigger(1.0f);
    for (int i = 0; i < numSamples; ++i)
        bufHigh[i] = bank2.process(0.0f);

    const float cLow  = spectralCentroidDFT(bufLow.data(),  numSamples, sr);
    const float cHigh = spectralCentroidDFT(bufHigh.data(), numSamples, sr);
    return {type, cLow, cHigh, cLow > 0.0f ? cHigh / cLow : 0.0f};
}

} // namespace

TEST_CASE("Velocity mapping: all 6 exciters have centroid ratio >= 2.0 (SC-004)",
          "[membrum][exciter][velocity][sc004]")
{
    constexpr double kSR = 44100.0;
    constexpr int kSamples = 441;

    const Membrum::ExciterType types[] = {
        Membrum::ExciterType::Impulse,
        Membrum::ExciterType::Mallet,
        Membrum::ExciterType::NoiseBurst,
        Membrum::ExciterType::Friction,
        Membrum::ExciterType::FMImpulse,
        Membrum::ExciterType::Feedback,
    };

    for (auto t : types)
    {
        const auto r = measureCentroidRatio(t, kSR, kSamples);
        INFO("exciter index=" << static_cast<int>(r.type)
             << " cLow=" << r.centLow
             << " cHigh=" << r.centHigh
             << " ratio=" << r.ratio);
        REQUIRE(r.centLow  > 0.0f);
        REQUIRE(r.centHigh > 0.0f);
        CHECK(r.ratio >= 2.0f);
    }
}
