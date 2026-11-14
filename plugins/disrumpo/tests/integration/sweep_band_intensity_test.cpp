// ==============================================================================
// Sweep Band Intensity Integration Tests
// ==============================================================================
// Tests for Phase 9 (US7): Per-Band Intensity Integration
// Verifies that SweepProcessor intensities are correctly applied to BandProcessor
// distortion via setSweepIntensity().
//
// References:
// - specs/007-sweep-system/spec.md FR-001, FR-007, SC-001 to SC-005
// - specs/007-sweep-system/tasks.md T063-T070
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/sweep_processor.h"
#include "dsp/band_processor.h"
#include "dsp/band_state.h"

#include "test_helpers/signal_metrics.h"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using Catch::Approx;

namespace {

// Test constants
constexpr double kTestSampleRate = 44100.0;
constexpr int kTestBlockSize = 512;

// Band center frequencies (approximate Bark scale for 8 bands)
constexpr std::array<float, 8> kBandCenterFreqs = {
    50.0f, 150.0f, 350.0f, 750.0f, 1500.0f, 3000.0f, 6000.0f, 12000.0f
};

} // namespace

TEST_CASE("SweepProcessor calculates per-band intensities", "[sweep][integration]") {
    Disrumpo::SweepProcessor sweep;
    sweep.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("Gaussian intensity distribution - center band receives full intensity") {
        // FR-001: Sweep at 1500 Hz (band 4 center)
        sweep.setEnabled(true);
        sweep.setCenterFrequency(1500.0f);
        sweep.setWidth(2.0f);  // 2 octaves
        sweep.setIntensity(1.0f);  // 100%
        sweep.setFalloffMode(Disrumpo::SweepFalloff::Smooth);

        // Let the frequency smoother converge
        // Default smoothing is 20ms, at 44100 Hz that's ~882 samples
        for (int i = 0; i < 2000; ++i) {
            sweep.process();
        }

        // Calculate intensity for band 4 (at sweep center)
        float centerIntensity = sweep.calculateBandIntensity(1500.0f);

        // SC-001: At center, intensity = intensityParam (100%)
        REQUIRE(centerIntensity == Approx(1.0f).margin(0.01f));
    }

    SECTION("Gaussian intensity distribution - falloff with distance") {
        sweep.setEnabled(true);
        sweep.setCenterFrequency(1000.0f);
        sweep.setWidth(2.0f);  // 2 octaves, sigma = 1 octave
        sweep.setIntensity(1.0f);
        sweep.setFalloffMode(Disrumpo::SweepFalloff::Smooth);

        // Let the frequency smoother converge
        for (int i = 0; i < 2000; ++i) {
            sweep.process();
        }

        // Calculate intensities at various distances
        float centerIntensity = sweep.calculateBandIntensity(1000.0f);
        float oneOctaveAway = sweep.calculateBandIntensity(2000.0f);  // 1 sigma
        float twoOctavesAway = sweep.calculateBandIntensity(4000.0f);  // 2 sigma

        // SC-001: Center = 100%
        REQUIRE(centerIntensity == Approx(1.0f).margin(0.01f));

        // SC-002: 1 sigma away = 60.6% (Gaussian exp(-0.5))
        REQUIRE(oneOctaveAway == Approx(0.606f).margin(0.02f));

        // SC-003: 2 sigma away = 13.5% (Gaussian exp(-2))
        REQUIRE(twoOctavesAway == Approx(0.135f).margin(0.02f));
    }

    SECTION("Sharp (linear) falloff - edge is exactly zero") {
        sweep.setEnabled(true);
        sweep.setCenterFrequency(1000.0f);
        sweep.setWidth(2.0f);  // +/- 1 octave
        sweep.setIntensity(1.0f);
        sweep.setFalloffMode(Disrumpo::SweepFalloff::Sharp);

        // Let the frequency smoother converge
        for (int i = 0; i < 2000; ++i) {
            sweep.process();
        }

        // Calculate at center and edge
        float centerIntensity = sweep.calculateBandIntensity(1000.0f);
        float edgeIntensity = sweep.calculateBandIntensity(2000.0f);  // Exactly 1 octave away (edge)
        float beyondEdge = sweep.calculateBandIntensity(4000.0f);  // Beyond edge

        // SC-004: Center = 100%
        REQUIRE(centerIntensity == Approx(1.0f).margin(0.01f));

        // SC-005: Edge = exactly 0%
        REQUIRE(edgeIntensity == Approx(0.0f).margin(0.01f));

        // Beyond edge also 0
        REQUIRE(beyondEdge == Approx(0.0f).margin(0.01f));
    }

    SECTION("calculateAllBandIntensities batch calculation") {
        sweep.setEnabled(true);
        sweep.setCenterFrequency(1500.0f);
        sweep.setWidth(2.0f);
        sweep.setIntensity(0.8f);  // 80%
        sweep.setFalloffMode(Disrumpo::SweepFalloff::Smooth);

        // Let the frequency smoother converge
        for (int i = 0; i < 2000; ++i) {
            sweep.process();
        }

        std::array<float, 8> intensities{};
        sweep.calculateAllBandIntensities(kBandCenterFreqs.data(), 8, intensities.data());

        // Verify intensity decreases with distance from center (1500 Hz is band 4)
        // Band 4 (1500 Hz) should have highest intensity
        float maxIntensity = 0.0f;
        int maxBand = -1;
        for (int i = 0; i < 8; ++i) {
            if (intensities[i] > maxIntensity) {
                maxIntensity = intensities[i];
                maxBand = i;
            }
        }

        REQUIRE(maxBand == 4);  // Band 4 is closest to 1500 Hz
        REQUIRE(maxIntensity <= 0.8f);  // Should not exceed intensity param

        // Verify monotonic decrease away from center
        for (int i = 0; i < 3; ++i) {
            REQUIRE(intensities[i] < intensities[i + 1]);  // Increasing toward center
        }
        for (int i = 4; i < 7; ++i) {
            REQUIRE(intensities[i] > intensities[i + 1]);  // Decreasing away from center
        }
    }

    SECTION("Disabled sweep returns zero intensity") {
        sweep.setEnabled(false);
        sweep.setCenterFrequency(1500.0f);
        sweep.setWidth(2.0f);
        sweep.setIntensity(1.0f);

        float intensity = sweep.calculateBandIntensity(1500.0f);
        REQUIRE(intensity == 0.0f);

        std::array<float, 8> intensities{};
        sweep.calculateAllBandIntensities(kBandCenterFreqs.data(), 8, intensities.data());
        for (int i = 0; i < 8; ++i) {
            REQUIRE(intensities[i] == 0.0f);
        }
    }
}

TEST_CASE("BandProcessor receives sweep intensity", "[sweep][integration]") {
    Disrumpo::BandProcessor band;
    band.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("setSweepIntensity affects processing") {
        // Default sweep intensity should be 1.0 (no reduction)
        band.setSweepIntensity(1.0f);

        float testL = 0.5f;
        float testR = 0.5f;
        band.process(testL, testR);

        // With sweep=1.0, gain=0dB, pan=center, unmuted, output should be close to input
        // (some variation due to equal-power pan law)
        REQUIRE(std::abs(testL) > 0.1f);  // Not zeroed
        REQUIRE(std::abs(testR) > 0.1f);
    }

    SECTION("Zero sweep intensity leaves the band level unchanged") {
        band.setSweepIntensity(0.0f);

        // Let smoother settle
        for (int i = 0; i < 1000; ++i) {
            float dummy = 0.0f;
            band.process(dummy, dummy);
        }

        float testL = 0.5f;
        float testR = 0.5f;
        band.process(testL, testR);

        // The sweep scales drive, never the band level (D-019): a clean band passes unchanged
        REQUIRE(testL == Approx(0.5f).margin(1e-6f));
        REQUIRE(testR == Approx(0.5f).margin(1e-6f));
    }

    SECTION("Sweep intensity scales drive, not output level") {
        // Saturating legacy band: SoftClip, drive 3 (the sweep scales it at block rate)
        Disrumpo::DistortionCommonParams cp;
        cp.drive = 3.0f;
        cp.mix = 1.0f;
        cp.toneHz = 8000.0f;
        band.setDistortionCommonParams(cp);

        // 1378.125 Hz = bin 256 of an 8192-point FFT at 44.1 kHz. At -30 dBFS: the drive re-stage
        // (user ruling 2026-11-10) runs SoftClip 18.06 dB hotter, and at the -12 dBFS this case
        // was written for drive 3 already saturated it (THD 33.9 % at 0 %, 35.0 % at 200 %); 18.06
        // dB lower its shaper sees the drive it saw before
        constexpr float kHz = 1378.125f;
        constexpr float kAmp = 0.25f * 0.125f;
        constexpr size_t kN = 8192;
        auto measure = [&band](float intensity, float& thd, float& rmsDb) {
            band.setSweepIntensity(intensity);
            std::vector<float> l(kN);
            std::vector<float> r(kN);
            for (int pass = 0; pass < 2; ++pass) {  // pass 0 lets the sweep smoother settle
                for (size_t i = 0; i < kN; ++i) {
                    l[i] = kAmp * std::sin(2.0f * std::numbers::pi_v<float> * kHz * static_cast<float>(i) /
                                           static_cast<float>(kTestSampleRate));
                    r[i] = l[i];
                }
                for (size_t off = 0; off < kN; off += static_cast<size_t>(kTestBlockSize)) {
                    band.processBlock(l.data() + off, r.data() + off, static_cast<size_t>(kTestBlockSize));
                }
            }
            thd = Krate::DSP::TestUtils::SignalMetrics::calculateTHD(l.data(), kN, kHz,
                                                                     static_cast<float>(kTestSampleRate));
            double s = 0.0;
            for (float v : l) s += static_cast<double>(v) * v;
            rmsDb = static_cast<float>(10.0 * std::log10(s / static_cast<double>(kN)));
        };

        float thd0 = 0.0f;
        float db0 = 0.0f;
        float thd2 = 0.0f;
        float db2 = 0.0f;
        measure(0.0f, thd0, db0);
        measure(2.0f, thd2, db2);
        INFO("THD at 0 %: " << thd0 << " %, at 200 %: " << thd2 << " %; level " << db0 << " / " << db2 << " dB");
        REQUIRE(thd2 > thd0 * 1.1f);               // drive doubled: more harmonics
        REQUIRE(std::abs(db2 - db0) < 3.0f);       // the level does not follow the drive (+6 dB)
    }
}

TEST_CASE("Sweep-BandProcessor integration workflow", "[sweep][integration]") {
    Disrumpo::SweepProcessor sweep;
    sweep.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("Full sweep-to-band intensity workflow") {
        // Configure sweep
        sweep.setEnabled(true);
        sweep.setCenterFrequency(1000.0f);
        sweep.setWidth(2.0f);
        sweep.setIntensity(1.0f);
        sweep.setFalloffMode(Disrumpo::SweepFalloff::Smooth);

        // Let the frequency smoother converge
        for (int i = 0; i < 2000; ++i) {
            sweep.process();
        }

        // Calculate intensities for all bands
        std::array<float, 8> intensities{};
        sweep.calculateAllBandIntensities(kBandCenterFreqs.data(), 8, intensities.data());

        // Find the band with highest intensity (should be closest to sweep center)
        int maxBand = 0;
        float maxIntensity = intensities[0];
        for (int i = 1; i < 8; ++i) {
            if (intensities[i] > maxIntensity) {
                maxIntensity = intensities[i];
                maxBand = i;
            }
        }

        // Band 3 (750 Hz) or band 4 (1500 Hz) should be closest to 1000 Hz sweep center
        // 1000 Hz is between bands 3 and 4
        REQUIRE((maxBand == 3 || maxBand == 4));

        // Verify intensity decreases away from sweep center
        // Bands far from center should have lower intensity
        REQUIRE(intensities[0] < intensities[3]);  // Far low vs center
        REQUIRE(intensities[7] < intensities[4]);  // Far high vs center
    }

    SECTION("Sweep disabled bypasses intensity modulation") {
        sweep.setEnabled(false);

        // Calculate intensities (should all be 0)
        std::array<float, 8> intensities{};
        sweep.calculateAllBandIntensities(kBandCenterFreqs.data(), 8, intensities.data());

        // When sweep is disabled every band gets intensity 0: its drive as set (D-019)
        for (int i = 0; i < 8; ++i) {
            REQUIRE(intensities[i] == 0.0f);
        }
    }
}

TEST_CASE("Sweep intensity respects parameter bounds", "[sweep][integration]") {
    Disrumpo::SweepProcessor sweep;
    sweep.prepare(kTestSampleRate, kTestBlockSize);
    sweep.setEnabled(true);
    sweep.setFalloffMode(Disrumpo::SweepFalloff::Smooth);

    SECTION("Intensity 200% scales output proportionally") {
        sweep.setCenterFrequency(1000.0f);
        sweep.setWidth(2.0f);
        sweep.setIntensity(2.0f);  // 200%

        // Let the frequency smoother converge
        for (int i = 0; i < 2000; ++i) {
            sweep.process();
        }

        float intensity = sweep.calculateBandIntensity(1000.0f);

        // SC-001: At center, intensity = intensityParam (200%)
        REQUIRE(intensity == Approx(2.0f).margin(0.01f));
    }

    SECTION("Intensity 0% gives zero output") {
        sweep.setCenterFrequency(1000.0f);
        sweep.setWidth(2.0f);
        sweep.setIntensity(0.0f);

        // Let the frequency smoother converge
        for (int i = 0; i < 2000; ++i) {
            sweep.process();
        }

        float intensity = sweep.calculateBandIntensity(1000.0f);
        REQUIRE(intensity == 0.0f);
    }

    SECTION("Width affects falloff rate") {
        sweep.setCenterFrequency(1000.0f);
        sweep.setIntensity(1.0f);

        // Let the frequency smoother converge
        for (int i = 0; i < 2000; ++i) {
            sweep.process();
        }

        // Narrow width (0.5 octaves) - steep falloff
        sweep.setWidth(0.5f);
        float narrowOneOctave = sweep.calculateBandIntensity(2000.0f);  // 1 octave away (2 sigma)

        // Wide width (4.0 octaves) - gentle falloff
        sweep.setWidth(4.0f);
        float wideOneOctave = sweep.calculateBandIntensity(2000.0f);  // 1 octave away (0.5 sigma)

        // Narrow width should have much lower intensity at same distance
        REQUIRE(narrowOneOctave < wideOneOctave);
    }
}
