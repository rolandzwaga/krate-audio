// ==============================================================================
// SpectralCoringEstimator Unit Tests
// ==============================================================================
// Layer 2: Processors
// Spec: specs/117-live-sidechain-mode/spec.md
// Covers: FR-007 (spectral coring residual estimation), SC-007
//
// Phase 4 (User Story 1): Full test suite for SpectralCoringEstimator.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/spectral_coring_estimator.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include <cmath>
#include <array>

using Catch::Approx;

static constexpr size_t kTestFftSize = 1024;
static constexpr float kTestSampleRate = 44100.0f;
static constexpr size_t kTestNumBins = kTestFftSize / 2 + 1;

// =============================================================================
// T035: prepare() sets isPrepared
// =============================================================================

TEST_CASE("SpectralCoringEstimator: prepare sets isPrepared",
          "[spectral-coring]")
{
    Krate::DSP::SpectralCoringEstimator estimator;
    REQUIRE_FALSE(estimator.isPrepared());

    estimator.prepare(kTestFftSize, kTestSampleRate);
    REQUIRE(estimator.isPrepared());
    REQUIRE(estimator.fftSize() == kTestFftSize);
}

// =============================================================================
// T036: harmonic-only spectrum produces near-zero residual
// =============================================================================

TEST_CASE("SpectralCoringEstimator: harmonic-only spectrum produces near-zero residual",
          "[spectral-coring]")
{
    Krate::DSP::SpectralCoringEstimator estimator;
    estimator.prepare(kTestFftSize, kTestSampleRate);

    // Build a spectrum with energy only at harmonic bins
    Krate::DSP::SpectralBuffer spectrum;
    spectrum.prepare(kTestFftSize);

    // Create a harmonic frame with a 440 Hz fundamental
    Krate::DSP::HarmonicFrame frame;
    frame.f0 = 440.0f;
    frame.f0Confidence = 0.95f;
    frame.numPartials = 5;

    const float binSpacing = kTestSampleRate / static_cast<float>(kTestFftSize);

    // Place energy ONLY at harmonic frequencies
    for (int h = 0; h < 5; ++h)
    {
        float freq = 440.0f * static_cast<float>(h + 1);
        frame.partials[h].frequency = freq;
        frame.partials[h].amplitude = 0.5f;
        frame.partials[h].harmonicIndex = h + 1;

        // Put energy in the bin closest to this harmonic
        auto bin = static_cast<size_t>(freq / binSpacing + 0.5f);
        if (bin < kTestNumBins)
        {
            spectrum.setCartesian(bin, 0.5f, 0.0f);
        }
    }

    auto residual = estimator.estimateResidual(spectrum, frame);

    // All energy was at harmonic bins, so residual should be near zero
    REQUIRE(residual.totalEnergy < 0.01f);
}

// =============================================================================
// T037: noise spectrum produces residual above -60 dBFS (SC-007)
// =============================================================================

TEST_CASE("SpectralCoringEstimator: noise spectrum produces non-trivial residual (SC-007)",
          "[spectral-coring]")
{
    Krate::DSP::SpectralCoringEstimator estimator;
    estimator.prepare(kTestFftSize, kTestSampleRate);

    // Build a flat (noise-like) spectrum with uniform energy in all bins
    Krate::DSP::SpectralBuffer spectrum;
    spectrum.prepare(kTestFftSize);

    for (size_t b = 0; b < kTestNumBins; ++b)
    {
        spectrum.setCartesian(b, 0.1f, 0.0f);
    }

    // Create a harmonic frame with a 440 Hz fundamental and 5 harmonics
    Krate::DSP::HarmonicFrame frame;
    frame.f0 = 440.0f;
    frame.f0Confidence = 0.95f;
    frame.numPartials = 5;

    for (int h = 0; h < 5; ++h)
    {
        float freq = 440.0f * static_cast<float>(h + 1);
        frame.partials[h].frequency = freq;
        frame.partials[h].amplitude = 0.5f;
        frame.partials[h].harmonicIndex = h + 1;
    }

    auto residual = estimator.estimateResidual(spectrum, frame);

    // Most bins are NOT harmonic, so residual should have significant energy
    // SC-007: at least -60 dBFS
    // -60 dBFS => 10^(-60/20) = 0.001 linear
    REQUIRE(residual.totalEnergy > 0.001f);

    // Verify at least some band energies are non-zero
    bool anyNonZeroBand = false;
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        if (residual.bandEnergies[b] > 0.0f)
        {
            anyNonZeroBand = true;
            break;
        }
    }
    REQUIRE(anyNonZeroBand);
}

// =============================================================================
// T038: output is compatible with ResidualSynthesizer::loadFrame()
// =============================================================================

TEST_CASE("SpectralCoringEstimator: output ResidualFrame has valid fields",
          "[spectral-coring]")
{
    Krate::DSP::SpectralCoringEstimator estimator;
    estimator.prepare(kTestFftSize, kTestSampleRate);

    Krate::DSP::SpectralBuffer spectrum;
    spectrum.prepare(kTestFftSize);

    // Put some energy in a few bins
    for (size_t b = 10; b < 50; ++b)
    {
        spectrum.setCartesian(b, 0.05f, 0.02f);
    }

    Krate::DSP::HarmonicFrame frame;
    frame.f0 = 200.0f;
    frame.f0Confidence = 0.9f;
    frame.numPartials = 3;
    for (int h = 0; h < 3; ++h)
    {
        frame.partials[h].frequency = 200.0f * static_cast<float>(h + 1);
        frame.partials[h].amplitude = 0.3f;
        frame.partials[h].harmonicIndex = h + 1;
    }

    auto residual = estimator.estimateResidual(spectrum, frame);

    // Verify all band energies are non-negative
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        REQUIRE(residual.bandEnergies[b] >= 0.0f);
    }

    // Verify totalEnergy is non-negative
    REQUIRE(residual.totalEnergy >= 0.0f);

    // transientFlag should be false (no transient detection in coring)
    REQUIRE_FALSE(residual.transientFlag);
}

// =============================================================================
// T039: calling estimateResidual before prepare does not crash
// =============================================================================

TEST_CASE("SpectralCoringEstimator: estimateResidual before prepare returns empty frame",
          "[spectral-coring]")
{
    Krate::DSP::SpectralCoringEstimator estimator;
    REQUIRE_FALSE(estimator.isPrepared());

    Krate::DSP::SpectralBuffer spectrum;
    spectrum.prepare(kTestFftSize);

    Krate::DSP::HarmonicFrame frame;
    frame.f0 = 440.0f;
    frame.numPartials = 1;
    frame.partials[0].frequency = 440.0f;

    // Should not crash -- returns empty/zero frame
    auto residual = estimator.estimateResidual(spectrum, frame);
    REQUIRE(residual.totalEnergy == 0.0f);
}

// =============================================================================
// Additional: reset clears state
// =============================================================================

TEST_CASE("SpectralCoringEstimator: reset clears prepared state",
          "[spectral-coring]")
{
    Krate::DSP::SpectralCoringEstimator estimator;
    estimator.prepare(kTestFftSize, kTestSampleRate);
    REQUIRE(estimator.isPrepared());

    estimator.reset();
    REQUIRE_FALSE(estimator.isPrepared());
}
