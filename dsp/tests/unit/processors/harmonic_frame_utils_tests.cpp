// ==============================================================================
// Harmonic Frame Utilities Tests (M4 Musical Control Layer)
// ==============================================================================
// Unit tests for lerpHarmonicFrame, lerpResidualFrame, computeHarmonicMask,
// and applyHarmonicMask utility functions.
//
// Feature: 118-musical-control-layer
// User Stories: US2 (Morph), US3 (Harmonic Filtering)
// Requirements: FR-011 to FR-015, FR-020 to FR-025
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/harmonic_frame_utils.h>

using Catch::Approx;

// =============================================================================
// Helper: Build a HarmonicFrame with known values
// =============================================================================
static Krate::DSP::HarmonicFrame makeFrame(
    int numPartials, float f0, float baseAmp,
    float globalAmp = 0.5f, float centroid = 1000.0f,
    float brightness = 0.5f, float noisiness = 0.1f,
    float confidence = 0.9f)
{
    Krate::DSP::HarmonicFrame frame{};
    frame.f0 = f0;
    frame.f0Confidence = confidence;
    frame.numPartials = numPartials;
    frame.globalAmplitude = globalAmp;
    frame.spectralCentroid = centroid;
    frame.brightness = brightness;
    frame.noisiness = noisiness;

    for (int i = 0; i < numPartials; ++i)
    {
        auto& p = frame.partials[static_cast<size_t>(i)];
        p.harmonicIndex = i + 1;
        p.amplitude = baseAmp / static_cast<float>(i + 1);
        p.relativeFrequency = static_cast<float>(i + 1) + 0.01f * static_cast<float>(i);
        p.frequency = f0 * p.relativeFrequency;
        p.phase = 0.1f * static_cast<float>(i);
        p.inharmonicDeviation = 0.01f * static_cast<float>(i);
        p.stability = 0.8f + 0.01f * static_cast<float>(i);
        p.age = 10 + i;
    }
    return frame;
}

static Krate::DSP::ResidualFrame makeResidualFrame(
    float totalEnergy, float bandBase, bool transient = false)
{
    Krate::DSP::ResidualFrame frame{};
    frame.totalEnergy = totalEnergy;
    frame.transientFlag = transient;
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
        frame.bandEnergies[b] = bandBase + 0.001f * static_cast<float>(b);
    return frame;
}

// =============================================================================
// Phase 4: US2 - lerpHarmonicFrame Tests (T039-T045)
// =============================================================================

// T039: lerpHarmonicFrame at t=0.0 returns frame equal to a
TEST_CASE("lerpHarmonicFrame at t=0.0 returns frame a",
          "[dsp][processors][morph][us2]")
{
    auto a = makeFrame(4, 440.0f, 0.8f, 0.6f, 1200.0f, 0.7f, 0.2f, 0.95f);
    auto b = makeFrame(4, 880.0f, 0.4f, 0.3f, 2400.0f, 0.3f, 0.4f, 0.8f);

    auto result = Krate::DSP::lerpHarmonicFrame(a, b, 0.0f);

    REQUIRE(result.numPartials == a.numPartials);
    REQUIRE(result.f0 == Approx(a.f0));
    REQUIRE(result.f0Confidence == Approx(a.f0Confidence));
    REQUIRE(result.globalAmplitude == Approx(a.globalAmplitude));
    REQUIRE(result.spectralCentroid == Approx(a.spectralCentroid));
    REQUIRE(result.brightness == Approx(a.brightness));
    REQUIRE(result.noisiness == Approx(a.noisiness));

    for (int i = 0; i < a.numPartials; ++i)
    {
        REQUIRE(result.partials[static_cast<size_t>(i)].amplitude ==
            Approx(a.partials[static_cast<size_t>(i)].amplitude).margin(1e-5f));
        REQUIRE(result.partials[static_cast<size_t>(i)].relativeFrequency ==
            Approx(a.partials[static_cast<size_t>(i)].relativeFrequency).margin(1e-5f));
    }
}

// T040: lerpHarmonicFrame at t=1.0 returns frame equal to b
TEST_CASE("lerpHarmonicFrame at t=1.0 returns frame b",
          "[dsp][processors][morph][us2]")
{
    auto a = makeFrame(4, 440.0f, 0.8f, 0.6f, 1200.0f, 0.7f, 0.2f, 0.95f);
    auto b = makeFrame(4, 880.0f, 0.4f, 0.3f, 2400.0f, 0.3f, 0.4f, 0.8f);

    auto result = Krate::DSP::lerpHarmonicFrame(a, b, 1.0f);

    REQUIRE(result.numPartials == b.numPartials);
    REQUIRE(result.f0 == Approx(b.f0));
    REQUIRE(result.f0Confidence == Approx(b.f0Confidence));
    REQUIRE(result.globalAmplitude == Approx(b.globalAmplitude));
    REQUIRE(result.spectralCentroid == Approx(b.spectralCentroid));
    REQUIRE(result.brightness == Approx(b.brightness));
    REQUIRE(result.noisiness == Approx(b.noisiness));

    for (int i = 0; i < b.numPartials; ++i)
    {
        REQUIRE(result.partials[static_cast<size_t>(i)].amplitude ==
            Approx(b.partials[static_cast<size_t>(i)].amplitude).margin(1e-5f));
        REQUIRE(result.partials[static_cast<size_t>(i)].relativeFrequency ==
            Approx(b.partials[static_cast<size_t>(i)].relativeFrequency).margin(1e-5f));
    }
}

// T041: lerpHarmonicFrame at t=0.5 with equal partial counts returns arithmetic mean
TEST_CASE("lerpHarmonicFrame at t=0.5 returns arithmetic mean of amplitudes",
          "[dsp][processors][morph][us2]")
{
    auto a = makeFrame(4, 440.0f, 0.8f);
    auto b = makeFrame(4, 880.0f, 0.4f);

    auto result = Krate::DSP::lerpHarmonicFrame(a, b, 0.5f);

    REQUIRE(result.numPartials == 4);
    for (int i = 0; i < 4; ++i)
    {
        float expectedAmp = (a.partials[static_cast<size_t>(i)].amplitude +
                            b.partials[static_cast<size_t>(i)].amplitude) / 2.0f;
        REQUIRE(result.partials[static_cast<size_t>(i)].amplitude ==
            Approx(expectedAmp).margin(1e-5f));
    }
}

// T042 (FR-015): Unequal partial counts - missing partials treated as zero amplitude
TEST_CASE("lerpHarmonicFrame with unequal partial counts (FR-015)",
          "[dsp][processors][morph][us2][fr015]")
{
    auto a = makeFrame(30, 440.0f, 0.6f);
    auto b = makeFrame(20, 880.0f, 0.4f);

    auto result = Krate::DSP::lerpHarmonicFrame(a, b, 0.5f);

    REQUIRE(result.numPartials == 30); // max of both

    // Partials 1-20: normal lerp
    for (int i = 0; i < 20; ++i)
    {
        float expectedAmp = (a.partials[static_cast<size_t>(i)].amplitude +
                            b.partials[static_cast<size_t>(i)].amplitude) / 2.0f;
        REQUIRE(result.partials[static_cast<size_t>(i)].amplitude ==
            Approx(expectedAmp).margin(1e-5f));
    }

    // Partials 21-30: only in A, lerp toward zero
    for (int i = 20; i < 30; ++i)
    {
        float expectedAmp = a.partials[static_cast<size_t>(i)].amplitude * 0.5f;
        REQUIRE(result.partials[static_cast<size_t>(i)].amplitude ==
            Approx(expectedAmp).margin(1e-5f));
    }
}

// T043 (FR-012): Missing partial relativeFrequency defaults to float(harmonicIndex)
TEST_CASE("lerpHarmonicFrame missing partial relativeFrequency defaults to harmonicIndex (FR-012)",
          "[dsp][processors][morph][us2][fr012]")
{
    auto a = makeFrame(10, 440.0f, 0.5f);
    auto b = makeFrame(5, 880.0f, 0.5f);

    auto result = Krate::DSP::lerpHarmonicFrame(a, b, 0.5f);

    // Partials 6-10 exist only in A; the missing B side defaults to
    // relativeFrequency = float(harmonicIndex)
    for (int i = 5; i < 10; ++i)
    {
        float aRelFreq = a.partials[static_cast<size_t>(i)].relativeFrequency;
        float defaultRelFreq = static_cast<float>(a.partials[static_cast<size_t>(i)].harmonicIndex);
        float expected = (aRelFreq + defaultRelFreq) / 2.0f;
        REQUIRE(result.partials[static_cast<size_t>(i)].relativeFrequency ==
            Approx(expected).margin(1e-5f));
    }
}

// T044: lerpHarmonicFrame does NOT interpolate phase - takes from dominant source
TEST_CASE("lerpHarmonicFrame does not interpolate phase",
          "[dsp][processors][morph][us2]")
{
    auto a = makeFrame(4, 440.0f, 0.5f);
    auto b = makeFrame(4, 880.0f, 0.5f);

    // Set distinguishable phases
    for (int i = 0; i < 4; ++i)
    {
        a.partials[static_cast<size_t>(i)].phase = 0.1f * static_cast<float>(i);
        b.partials[static_cast<size_t>(i)].phase = 1.0f + 0.2f * static_cast<float>(i);
    }

    // At t=0.3 (< 0.5), dominant source is A
    auto resultA = Krate::DSP::lerpHarmonicFrame(a, b, 0.3f);
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(resultA.partials[static_cast<size_t>(i)].phase ==
            Approx(a.partials[static_cast<size_t>(i)].phase));
    }

    // At t=0.7 (> 0.5), dominant source is B
    auto resultB = Krate::DSP::lerpHarmonicFrame(a, b, 0.7f);
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(resultB.partials[static_cast<size_t>(i)].phase ==
            Approx(b.partials[static_cast<size_t>(i)].phase));
    }
}

// T045: lerpHarmonicFrame interpolates frame metadata
TEST_CASE("lerpHarmonicFrame interpolates metadata at t=0.5",
          "[dsp][processors][morph][us2]")
{
    auto a = makeFrame(4, 440.0f, 0.5f, 0.6f, 1200.0f, 0.7f, 0.2f, 0.95f);
    auto b = makeFrame(4, 880.0f, 0.5f, 0.3f, 2400.0f, 0.3f, 0.4f, 0.8f);

    auto result = Krate::DSP::lerpHarmonicFrame(a, b, 0.5f);

    REQUIRE(result.f0 == Approx((a.f0 + b.f0) / 2.0f).margin(1e-5f));
    REQUIRE(result.globalAmplitude ==
        Approx((a.globalAmplitude + b.globalAmplitude) / 2.0f).margin(1e-5f));
    REQUIRE(result.spectralCentroid ==
        Approx((a.spectralCentroid + b.spectralCentroid) / 2.0f).margin(1e-5f));
    REQUIRE(result.brightness ==
        Approx((a.brightness + b.brightness) / 2.0f).margin(1e-5f));
    REQUIRE(result.noisiness ==
        Approx((a.noisiness + b.noisiness) / 2.0f).margin(1e-5f));
    REQUIRE(result.f0Confidence ==
        Approx((a.f0Confidence + b.f0Confidence) / 2.0f).margin(1e-5f));
}

// T045b: lerpHarmonicFrame interpolates frequency continuously (no hard switch)
TEST_CASE("lerpHarmonicFrame interpolates frequency continuously",
          "[dsp][processors][morph][us2]")
{
    auto a = makeFrame(4, 440.0f, 0.5f);
    auto b = makeFrame(4, 880.0f, 0.5f);

    // At t=0.3 and t=0.7, frequency should be a smooth lerp, NOT a hard switch
    auto r03 = Krate::DSP::lerpHarmonicFrame(a, b, 0.3f);
    auto r07 = Krate::DSP::lerpHarmonicFrame(a, b, 0.7f);

    for (int i = 0; i < 4; ++i)
    {
        auto idx = static_cast<size_t>(i);
        float freqA = a.partials[idx].frequency;
        float freqB = b.partials[idx].frequency;

        // t=0.3: should be 70% A + 30% B
        float expected03 = 0.7f * freqA + 0.3f * freqB;
        REQUIRE(r03.partials[idx].frequency == Approx(expected03).margin(1e-3f));

        // t=0.7: should be 30% A + 70% B
        float expected07 = 0.3f * freqA + 0.7f * freqB;
        REQUIRE(r07.partials[idx].frequency == Approx(expected07).margin(1e-3f));
    }
}

// T045c: lerpHarmonicFrame interpolates inharmonicDeviation and stability
TEST_CASE("lerpHarmonicFrame interpolates inharmonicDeviation and stability",
          "[dsp][processors][morph][us2]")
{
    auto a = makeFrame(4, 440.0f, 0.5f);
    auto b = makeFrame(4, 880.0f, 0.5f);

    auto result = Krate::DSP::lerpHarmonicFrame(a, b, 0.5f);

    for (int i = 0; i < 4; ++i)
    {
        auto idx = static_cast<size_t>(i);

        float expectedDev = (a.partials[idx].inharmonicDeviation +
                             b.partials[idx].inharmonicDeviation) / 2.0f;
        REQUIRE(result.partials[idx].inharmonicDeviation ==
            Approx(expectedDev).margin(1e-5f));

        float expectedStab = (a.partials[idx].stability +
                              b.partials[idx].stability) / 2.0f;
        REQUIRE(result.partials[idx].stability ==
            Approx(expectedStab).margin(1e-5f));
    }
}

// T045d: lerpHarmonicFrame frequency is continuous across t=0.5 boundary
TEST_CASE("lerpHarmonicFrame frequency has no discontinuity at t=0.5",
          "[dsp][processors][morph][us2]")
{
    auto a = makeFrame(4, 440.0f, 0.5f);
    auto b = makeFrame(4, 880.0f, 0.5f);

    // Sample at t=0.49 and t=0.51 — frequency should be nearly identical
    auto rLo = Krate::DSP::lerpHarmonicFrame(a, b, 0.49f);
    auto rHi = Krate::DSP::lerpHarmonicFrame(a, b, 0.51f);

    for (int i = 0; i < 4; ++i)
    {
        auto idx = static_cast<size_t>(i);
        float freqLo = rLo.partials[idx].frequency;
        float freqHi = rHi.partials[idx].frequency;

        // Should differ by < 2% (smooth linear step, no hard switch)
        // A hard switch would produce a >50% jump at this boundary.
        float diff = std::abs(freqHi - freqLo);
        float avg = (freqLo + freqHi) / 2.0f;
        INFO("Partial " << i << ": freqLo=" << freqLo << " freqHi=" << freqHi);
        REQUIRE(diff / avg < 0.02f);
    }
}

// =============================================================================
// Phase 4: US2 - lerpResidualFrame Tests (T046-T049)
// =============================================================================

// T046: lerpResidualFrame at t=0.0 returns frame equal to a
TEST_CASE("lerpResidualFrame at t=0.0 returns frame a",
          "[dsp][processors][morph][us2]")
{
    auto a = makeResidualFrame(0.1f, 0.01f, true);
    auto b = makeResidualFrame(0.5f, 0.05f, false);

    auto result = Krate::DSP::lerpResidualFrame(a, b, 0.0f);

    REQUIRE(result.totalEnergy == Approx(a.totalEnergy));
    REQUIRE(result.transientFlag == a.transientFlag);
    for (size_t i = 0; i < Krate::DSP::kResidualBands; ++i)
        REQUIRE(result.bandEnergies[i] == Approx(a.bandEnergies[i]).margin(1e-6f));
}

// T047: lerpResidualFrame at t=1.0 returns frame equal to b
TEST_CASE("lerpResidualFrame at t=1.0 returns frame b",
          "[dsp][processors][morph][us2]")
{
    auto a = makeResidualFrame(0.1f, 0.01f, true);
    auto b = makeResidualFrame(0.5f, 0.05f, false);

    auto result = Krate::DSP::lerpResidualFrame(a, b, 1.0f);

    REQUIRE(result.totalEnergy == Approx(b.totalEnergy));
    REQUIRE(result.transientFlag == b.transientFlag);
    for (size_t i = 0; i < Krate::DSP::kResidualBands; ++i)
        REQUIRE(result.bandEnergies[i] == Approx(b.bandEnergies[i]).margin(1e-6f));
}

// T048: lerpResidualFrame at t=0.5 returns arithmetic mean of band energies
TEST_CASE("lerpResidualFrame at t=0.5 returns mean band energies",
          "[dsp][processors][morph][us2]")
{
    auto a = makeResidualFrame(0.1f, 0.01f);
    auto b = makeResidualFrame(0.5f, 0.05f);

    auto result = Krate::DSP::lerpResidualFrame(a, b, 0.5f);

    REQUIRE(result.totalEnergy == Approx((a.totalEnergy + b.totalEnergy) / 2.0f).margin(1e-6f));
    for (size_t i = 0; i < Krate::DSP::kResidualBands; ++i)
    {
        float expected = (a.bandEnergies[i] + b.bandEnergies[i]) / 2.0f;
        REQUIRE(result.bandEnergies[i] == Approx(expected).margin(1e-6f));
    }
}

// T049: lerpResidualFrame transient flag selection
TEST_CASE("lerpResidualFrame transient flag selection at morph boundary",
          "[dsp][processors][morph][us2]")
{
    auto a = makeResidualFrame(0.1f, 0.01f, true);   // a has transient
    auto b = makeResidualFrame(0.5f, 0.05f, false);   // b has no transient

    // t=0.4: t > 0.5f is false -> use a.transientFlag
    REQUIRE(Krate::DSP::lerpResidualFrame(a, b, 0.4f).transientFlag == true);

    // t=0.5: t > 0.5f is false -> use a.transientFlag
    REQUIRE(Krate::DSP::lerpResidualFrame(a, b, 0.5f).transientFlag == true);

    // t=0.6: t > 0.5f is true -> use b.transientFlag
    REQUIRE(Krate::DSP::lerpResidualFrame(a, b, 0.6f).transientFlag == false);
}

// =============================================================================
// Phase 5: US3 - computeHarmonicMask Tests (T065-T069)
// =============================================================================

// Helper: Build a partial array with consecutive harmonicIndex 1..n
static void fillPartials(
    std::array<Krate::DSP::Partial, Krate::DSP::kMaxPartials>& partials,
    int count)
{
    for (int i = 0; i < count; ++i)
    {
        partials[static_cast<size_t>(i)] = {};
        partials[static_cast<size_t>(i)].harmonicIndex = i + 1;
        partials[static_cast<size_t>(i)].amplitude = 0.5f;
        partials[static_cast<size_t>(i)].relativeFrequency = static_cast<float>(i + 1);
    }
}

// T065 (FR-021): All-Pass mask sets all values to 1.0
TEST_CASE("computeHarmonicMask All-Pass sets all mask values to 1.0 (FR-021)",
          "[dsp][processors][filter][us3][fr021]")
{
    constexpr int n = 10;
    std::array<Krate::DSP::Partial, Krate::DSP::kMaxPartials> partials{};
    fillPartials(partials, n);
    std::array<float, Krate::DSP::kMaxPartials> mask{};

    Krate::DSP::computeHarmonicMask(0, partials, n, mask);

    for (int i = 0; i < n; ++i)
        REQUIRE(mask[static_cast<size_t>(i)] == Approx(1.0f));
}

// T066 (FR-022): Odd Only mask passes odd harmonicIndex, blocks even
TEST_CASE("computeHarmonicMask Odd Only passes odd, blocks even (FR-022)",
          "[dsp][processors][filter][us3][fr022]")
{
    constexpr int n = 10;
    std::array<Krate::DSP::Partial, Krate::DSP::kMaxPartials> partials{};
    fillPartials(partials, n);
    std::array<float, Krate::DSP::kMaxPartials> mask{};

    Krate::DSP::computeHarmonicMask(1, partials, n, mask);

    for (int i = 0; i < n; ++i)
    {
        int idx = partials[static_cast<size_t>(i)].harmonicIndex;
        if (idx % 2 == 1) // odd
            REQUIRE(mask[static_cast<size_t>(i)] == Approx(1.0f));
        else // even
            REQUIRE(mask[static_cast<size_t>(i)] == Approx(0.0f));
    }
}

// T067 (FR-023): Even Only mask passes even harmonicIndex, blocks odd
TEST_CASE("computeHarmonicMask Even Only passes even, blocks odd (FR-023)",
          "[dsp][processors][filter][us3][fr023]")
{
    constexpr int n = 10;
    std::array<Krate::DSP::Partial, Krate::DSP::kMaxPartials> partials{};
    fillPartials(partials, n);
    std::array<float, Krate::DSP::kMaxPartials> mask{};

    Krate::DSP::computeHarmonicMask(2, partials, n, mask);

    for (int i = 0; i < n; ++i)
    {
        int idx = partials[static_cast<size_t>(i)].harmonicIndex;
        if (idx % 2 == 0) // even
            REQUIRE(mask[static_cast<size_t>(i)] == Approx(1.0f));
        else // odd (including fundamental at index 1)
            REQUIRE(mask[static_cast<size_t>(i)] == Approx(0.0f));
    }
}

// T068 (FR-024): Low Harmonics mask follows clamp(8/n, 0, 1) floor
TEST_CASE("computeHarmonicMask Low Harmonics follows clamp(8/n, 0, 1) floor (FR-024)",
          "[dsp][processors][filter][us3][fr024]")
{
    // Test at specific harmonic indices: 1, 8, 16, 32
    constexpr int n = 32;
    std::array<Krate::DSP::Partial, Krate::DSP::kMaxPartials> partials{};
    fillPartials(partials, n);
    std::array<float, Krate::DSP::kMaxPartials> mask{};

    Krate::DSP::computeHarmonicMask(3, partials, n, mask);

    // harmonicIndex 1: clamp(8/1, 0, 1) = 1.0
    REQUIRE(mask[0] == Approx(1.0f));

    // harmonicIndex 8: clamp(8/8, 0, 1) = 1.0
    REQUIRE(mask[7] == Approx(1.0f));

    // harmonicIndex 16: clamp(8/16, 0, 1) = 0.5
    REQUIRE(mask[15] == Approx(0.5f));

    // harmonicIndex 32: clamp(8/32, 0, 1) = 0.25
    REQUIRE(mask[31] == Approx(0.25f));

    // Verify all values meet the floor: mask[i] >= clamp(8/n, 0, 1)
    for (int i = 0; i < n; ++i)
    {
        int idx = partials[static_cast<size_t>(i)].harmonicIndex;
        float floor = std::clamp(8.0f / static_cast<float>(idx), 0.0f, 1.0f);
        REQUIRE(mask[static_cast<size_t>(i)] >= floor - 1e-6f);
    }
}

// T069 (FR-025): High Harmonics attenuates index 1 by >= 12 dB relative to index 8+
TEST_CASE("computeHarmonicMask High Harmonics attenuates fundamental >= 12 dB (FR-025)",
          "[dsp][processors][filter][us3][fr025]")
{
    constexpr int n = 16;
    std::array<Krate::DSP::Partial, Krate::DSP::kMaxPartials> partials{};
    fillPartials(partials, n);
    std::array<float, Krate::DSP::kMaxPartials> mask{};

    Krate::DSP::computeHarmonicMask(4, partials, n, mask);

    // harmonicIndex 1 should be attenuated by >= 12 dB relative to index 8+
    // 12 dB = linear ratio of 0.25 => mask(1) / mask(8) <= 0.25
    float mask1 = mask[0]; // harmonicIndex 1
    float mask8 = mask[7]; // harmonicIndex 8

    // mask(8) should be 1.0 (clamp(8/8, 0, 1) = 1.0)
    REQUIRE(mask8 == Approx(1.0f));

    // mask(1) / mask(8) <= 0.25 (12 dB attenuation)
    REQUIRE(mask1 / mask8 <= 0.25f);

    // Also verify higher harmonics (index >= 8) are at full amplitude
    for (int i = 7; i < n; ++i)
        REQUIRE(mask[static_cast<size_t>(i)] == Approx(1.0f));
}

// =============================================================================
// Phase 5: US3 - applyHarmonicMask Tests (T070-T071)
// =============================================================================

// T070: applyHarmonicMask multiplies amplitude and preserves other fields
TEST_CASE("applyHarmonicMask multiplies amplitude, preserves other fields",
          "[dsp][processors][filter][us3]")
{
    auto frame = makeFrame(4, 440.0f, 1.0f);

    // Create a mask that halves all amplitudes
    std::array<float, Krate::DSP::kMaxPartials> mask{};
    mask.fill(1.0f);
    for (int i = 0; i < 4; ++i)
        mask[static_cast<size_t>(i)] = 0.5f;

    // Save original non-amplitude fields
    std::array<int, 4> origIndex{};
    std::array<float, 4> origRelFreq{};
    std::array<float, 4> origPhase{};
    std::array<float, 4> origInharm{};
    std::array<float, 4> origAmps{};
    for (int i = 0; i < 4; ++i)
    {
        origIndex[static_cast<size_t>(i)] = frame.partials[static_cast<size_t>(i)].harmonicIndex;
        origRelFreq[static_cast<size_t>(i)] = frame.partials[static_cast<size_t>(i)].relativeFrequency;
        origPhase[static_cast<size_t>(i)] = frame.partials[static_cast<size_t>(i)].phase;
        origInharm[static_cast<size_t>(i)] = frame.partials[static_cast<size_t>(i)].inharmonicDeviation;
        origAmps[static_cast<size_t>(i)] = frame.partials[static_cast<size_t>(i)].amplitude;
    }

    Krate::DSP::applyHarmonicMask(frame, mask);

    for (int i = 0; i < 4; ++i)
    {
        auto idx = static_cast<size_t>(i);
        // Amplitude should be halved
        REQUIRE(frame.partials[idx].amplitude ==
            Approx(origAmps[idx] * 0.5f).margin(1e-6f));
        // Other fields should be unchanged
        REQUIRE(frame.partials[idx].harmonicIndex == origIndex[idx]);
        REQUIRE(frame.partials[idx].relativeFrequency == Approx(origRelFreq[idx]));
        REQUIRE(frame.partials[idx].phase == Approx(origPhase[idx]));
        REQUIRE(frame.partials[idx].inharmonicDeviation == Approx(origInharm[idx]));
    }
}

// T071: applyHarmonicMask with All-Pass mask leaves amplitudes unchanged
TEST_CASE("applyHarmonicMask with All-Pass mask leaves amplitudes unchanged",
          "[dsp][processors][filter][us3]")
{
    auto frame = makeFrame(4, 440.0f, 1.0f);

    // All-pass mask: all 1.0
    std::array<float, Krate::DSP::kMaxPartials> mask{};
    mask.fill(1.0f);

    // Save original amplitudes
    std::array<float, 4> origAmps{};
    for (int i = 0; i < 4; ++i)
        origAmps[static_cast<size_t>(i)] = frame.partials[static_cast<size_t>(i)].amplitude;

    Krate::DSP::applyHarmonicMask(frame, mask);

    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(frame.partials[static_cast<size_t>(i)].amplitude ==
            Approx(origAmps[static_cast<size_t>(i)]).margin(1e-6f));
    }
}
