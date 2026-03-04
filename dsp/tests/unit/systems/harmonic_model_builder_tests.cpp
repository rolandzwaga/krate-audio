// ==============================================================================
// Harmonic Model Builder Tests
// ==============================================================================
// Spec: specs/115-innexus-m1-core-instrument/spec.md
// Phase 8: FR-029 to FR-034
// Tests: L2 normalization, dual-timescale blending, spectral centroid,
//        brightness, median filter, global amplitude, zero partials
// ==============================================================================

#include <krate/dsp/systems/harmonic_model_builder.h>
#include <krate/dsp/processors/harmonic_types.h>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

using namespace Krate::DSP;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helper: create a Partial with given harmonic index, frequency, and amplitude
// ---------------------------------------------------------------------------
static Partial makePartial(int harmonicIndex, float frequency, float amplitude,
                           float relativeFreq = 0.0f, float deviation = 0.0f) {
    Partial p{};
    p.harmonicIndex = harmonicIndex;
    p.frequency = frequency;
    p.amplitude = amplitude;
    p.phase = 0.0f;
    p.relativeFrequency = relativeFreq;
    p.inharmonicDeviation = deviation;
    p.stability = 1.0f;
    p.age = 10;
    return p;
}

// ===========================================================================
// FR-030: L2 Normalization
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: L2 normalization produces unit norm",
          "[systems][harmonic_model_builder][FR-030]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);

    // Create partials with known amplitudes: [0.3, 0.4, 0.5]
    // L2 norm = sqrt(0.09 + 0.16 + 0.25) = sqrt(0.5) = ~0.7071
    // After normalization: each amp_i / norm => sum(amp_i^2) = 1.0
    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.3f, 1.0f);
    partials[1] = makePartial(2, 880.0f, 0.4f, 2.0f);
    partials[2] = makePartial(3, 1320.0f, 0.5f, 3.0f);

    F0Estimate f0{};
    f0.frequency = 440.0f;
    f0.confidence = 0.9f;
    f0.voiced = true;

    // Feed several frames to fill median filter buffer
    HarmonicFrame frame;
    for (int i = 0; i < 10; ++i) {
        frame = builder.build(partials, 3, f0, 0.5f);
    }

    // Verify sum of squared normalized amplitudes is approximately 1.0
    float sumSqr = 0.0f;
    for (int i = 0; i < frame.numPartials; ++i) {
        sumSqr += frame.partials[i].amplitude * frame.partials[i].amplitude;
    }
    REQUIRE(sumSqr == Approx(1.0f).margin(0.01f));
}

TEST_CASE("HarmonicModelBuilder: L2 normalization preserves relative ratios",
          "[systems][harmonic_model_builder][FR-030]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);

    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.6f, 1.0f);
    partials[1] = makePartial(2, 880.0f, 0.3f, 2.0f);

    F0Estimate f0{440.0f, 0.9f, true};

    HarmonicFrame frame;
    for (int i = 0; i < 10; ++i) {
        frame = builder.build(partials, 2, f0, 0.5f);
    }

    // Ratio of amplitudes should be preserved: 0.6/0.3 = 2.0
    REQUIRE(frame.numPartials == 2);
    float ratio = frame.partials[0].amplitude / frame.partials[1].amplitude;
    REQUIRE(ratio == Approx(2.0f).margin(0.05f));
}

// ===========================================================================
// FR-031: Dual-Timescale Blending -- Fast Layer (~5ms response)
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: fast layer responds quickly with 2 partials",
          "[systems][harmonic_model_builder][FR-031]") {
    constexpr double sampleRate = 44100.0;
    constexpr int hopSize = 256;

    HarmonicModelBuilder builder;
    builder.prepare(sampleRate);
    builder.setHopSize(hopSize);
    builder.setResponsiveness(1.0f); // Full fast layer

    // Feed 2 partials at steady state with known ratio
    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.8f, 1.0f);
    partials[1] = makePartial(2, 880.0f, 0.2f, 2.0f);
    F0Estimate f0{440.0f, 0.9f, true};

    for (int i = 0; i < 50; ++i) {
        (void)builder.build(partials, 2, f0, 0.5f);
    }

    // Abrupt step: swap amplitudes
    partials[0].amplitude = 0.2f;
    partials[1].amplitude = 0.8f;

    // The median filter (window=5) delays the step by ~3 frames before the
    // median switches. After that, the fast smoother (~5ms) converges quickly.
    // With 5 frames of new data, the median fully switches and the fast smoother
    // has had ~29ms (5 * 5.8ms) to respond.
    HarmonicFrame frame;
    for (int i = 0; i < 5; ++i) {
        frame = builder.build(partials, 2, f0, 0.5f);
    }

    float ratio = frame.partials[0].amplitude / frame.partials[1].amplitude;
    // New ratio is 0.25, old ratio was 4.0. After median switches (~3 frames)
    // and the fast smoother runs for ~2 more hops (~11.6ms), the ratio should
    // have moved significantly toward the new value. Should be < 1.0.
    REQUIRE(ratio < 1.0f);
}

// ===========================================================================
// FR-031: Dual-Timescale Blending -- Slow Layer (~100ms response)
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: slow layer takes ~100ms to fully respond",
          "[systems][harmonic_model_builder][FR-031]") {
    constexpr double sampleRate = 44100.0;
    constexpr int hopSize = 256;

    HarmonicModelBuilder builder;
    builder.prepare(sampleRate);
    builder.setHopSize(hopSize);
    builder.setResponsiveness(0.0f); // Full slow layer

    // Feed 2 partials at steady state
    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.8f, 1.0f);
    partials[1] = makePartial(2, 880.0f, 0.2f, 2.0f);
    F0Estimate f0{440.0f, 0.9f, true};

    for (int i = 0; i < 100; ++i) {
        (void)builder.build(partials, 2, f0, 0.5f);
    }

    // Step change: swap amplitudes
    partials[0].amplitude = 0.2f;
    partials[1].amplitude = 0.8f;

    // After one frame, slow layer should barely have moved
    HarmonicFrame frame = builder.build(partials, 2, f0, 0.5f);

    // With 100ms slow smoother and hopSize=256 (~5.8ms), after 1 frame
    // the slow layer should have moved only ~5.6% of the way
    // The ratio should still be close to the old ratio (4:1)
    float ratio = frame.partials[0].amplitude / frame.partials[1].amplitude;
    REQUIRE(ratio > 2.0f);

    // After ~100ms worth of frames (100ms / 5.8ms per hop = ~17 frames),
    // the slow layer should be mostly converged
    for (int i = 0; i < 20; ++i) {
        frame = builder.build(partials, 2, f0, 0.5f);
    }

    ratio = frame.partials[0].amplitude / frame.partials[1].amplitude;
    // Should now be close to 1:4, i.e. ratio < 0.5
    REQUIRE(ratio < 0.5f);
}

// ===========================================================================
// FR-032: Spectral Centroid
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: spectral centroid matches amplitude-weighted mean",
          "[systems][harmonic_model_builder][FR-032]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);
    builder.setHopSize(256);

    // Two partials: 440Hz at amp 0.8, 880Hz at amp 0.2
    // Expected centroid = (440*0.8 + 880*0.2) / (0.8 + 0.2) = (352 + 176) / 1.0 = 528 Hz
    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.8f, 1.0f);
    partials[1] = makePartial(2, 880.0f, 0.2f, 2.0f);
    F0Estimate f0{440.0f, 0.9f, true};

    HarmonicFrame frame;
    for (int i = 0; i < 10; ++i) {
        frame = builder.build(partials, 2, f0, 0.5f);
    }

    REQUIRE(frame.spectralCentroid == Approx(528.0f).margin(5.0f));
}

// ===========================================================================
// FR-032: Brightness = spectralCentroid / F0
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: brightness equals centroid / f0",
          "[systems][harmonic_model_builder][FR-032]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);
    builder.setHopSize(256);

    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.8f, 1.0f);
    partials[1] = makePartial(2, 880.0f, 0.2f, 2.0f);
    F0Estimate f0{440.0f, 0.9f, true};

    HarmonicFrame frame;
    for (int i = 0; i < 10; ++i) {
        frame = builder.build(partials, 2, f0, 0.5f);
    }

    float expectedBrightness = frame.spectralCentroid / f0.frequency;
    REQUIRE(frame.brightness == Approx(expectedBrightness).margin(0.01f));
}

// ===========================================================================
// FR-033: Median Filter Rejects Impulse
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: median filter rejects single impulse",
          "[systems][harmonic_model_builder][FR-033]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);
    builder.setHopSize(256);
    builder.setResponsiveness(1.0f); // Full fast layer to isolate median filter

    // Use 2 partials to see the ratio effect
    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.8f, 1.0f);
    partials[1] = makePartial(2, 880.0f, 0.2f, 2.0f);
    F0Estimate f0{440.0f, 0.9f, true};

    // Feed 5 frames of steady state (fills median buffer)
    for (int i = 0; i < 5; ++i) {
        (void)builder.build(partials, 2, f0, 0.5f);
    }

    // Spike on partial 0 only
    partials[0].amplitude = 10.0f;
    (void)builder.build(partials, 2, f0, 0.5f);

    // Restore to normal
    partials[0].amplitude = 0.8f;
    HarmonicFrame afterSpike = builder.build(partials, 2, f0, 0.5f);

    // The ratio should stay close to the original 4:1
    // Median filter window of 5 means the single spike is rejected
    float ratio = afterSpike.partials[0].amplitude / afterSpike.partials[1].amplitude;
    REQUIRE(ratio == Approx(4.0f).margin(1.5f));
}

// ===========================================================================
// FR-033: Median Filter Preserves Step Edge
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: median filter preserves step edge",
          "[systems][harmonic_model_builder][FR-033]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);
    builder.setHopSize(256);
    builder.setResponsiveness(1.0f); // Full fast layer

    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.8f, 1.0f);
    partials[1] = makePartial(2, 880.0f, 0.2f, 2.0f);
    F0Estimate f0{440.0f, 0.9f, true};

    // Feed steady state
    for (int i = 0; i < 5; ++i) {
        (void)builder.build(partials, 2, f0, 0.5f);
    }

    // Step change: swap amplitudes permanently
    partials[0].amplitude = 0.2f;
    partials[1].amplitude = 0.8f;

    // Feed enough frames for median to fully adopt new values
    // With window of 5, after 3 new frames the median switches
    HarmonicFrame frame;
    for (int i = 0; i < 5; ++i) {
        frame = builder.build(partials, 2, f0, 0.5f);
    }

    // After 5 frames of the new value, median should be the new value
    float ratio = frame.partials[0].amplitude / frame.partials[1].amplitude;
    // New ratio is 0.2/0.8 = 0.25, should be < 0.5
    REQUIRE(ratio < 0.5f);
}

// ===========================================================================
// FR-034: Global Amplitude Tracks via One-Pole Smoother
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: global amplitude tracks input RMS",
          "[systems][harmonic_model_builder][FR-034]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);
    builder.setHopSize(256);

    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.5f, 1.0f);
    F0Estimate f0{440.0f, 0.9f, true};

    // Feed constant RMS of 0.5
    HarmonicFrame frame;
    for (int i = 0; i < 50; ++i) {
        frame = builder.build(partials, 1, f0, 0.5f);
    }

    // After convergence, globalAmplitude should be near 0.5
    REQUIRE(frame.globalAmplitude == Approx(0.5f).margin(0.05f));

    // Step to RMS 0.8
    for (int i = 0; i < 50; ++i) {
        frame = builder.build(partials, 1, f0, 0.8f);
    }

    REQUIRE(frame.globalAmplitude == Approx(0.8f).margin(0.05f));
}

// ===========================================================================
// FR-029: Noisiness Estimate
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: noisiness estimate",
          "[systems][harmonic_model_builder][FR-029]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);
    builder.setHopSize(256);

    // If input RMS = 0.5, inputEnergy = 0.25
    // If partial energy (sum of amp^2) = 0.20, inputEnergy = 0.25
    // noisiness = 1.0 - 0.20 / 0.25 = 0.2
    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.4f, 1.0f);  // energy = 0.16
    partials[1] = makePartial(2, 880.0f, 0.2f, 2.0f);  // energy = 0.04
    // total partial energy = 0.20
    F0Estimate f0{440.0f, 0.9f, true};
    float inputRms = 0.5f; // inputEnergy = 0.25

    HarmonicFrame frame;
    for (int i = 0; i < 10; ++i) {
        frame = builder.build(partials, 2, f0, inputRms);
    }

    // noisiness = 1 - partialEnergy / inputEnergy = 1 - 0.20/0.25 = 0.2
    REQUIRE(frame.noisiness == Approx(0.2f).margin(0.05f));
}

// ===========================================================================
// Edge: Zero Partials
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: zero partials produces valid frame",
          "[systems][harmonic_model_builder][edge]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);
    builder.setHopSize(256);

    std::array<Partial, kMaxPartials> partials{};
    F0Estimate f0{0.0f, 0.0f, false};

    HarmonicFrame frame = builder.build(partials, 0, f0, 0.0f);

    REQUIRE(frame.numPartials == 0);
    REQUIRE(frame.globalAmplitude >= 0.0f);
    REQUIRE(frame.noisiness >= 0.0f);
    REQUIRE(frame.noisiness <= 1.0f);
}

// ===========================================================================
// FR-029: Full HarmonicFrame fields populated
// ===========================================================================
TEST_CASE("HarmonicModelBuilder: output frame has all required fields",
          "[systems][harmonic_model_builder][FR-029]") {
    HarmonicModelBuilder builder;
    builder.prepare(44100.0);
    builder.setHopSize(256);

    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(1, 440.0f, 0.6f, 1.0f);
    partials[1] = makePartial(2, 880.0f, 0.3f, 2.0f);
    partials[2] = makePartial(3, 1320.0f, 0.1f, 3.0f);
    F0Estimate f0{440.0f, 0.85f, true};

    HarmonicFrame frame;
    for (int i = 0; i < 10; ++i) {
        frame = builder.build(partials, 3, f0, 0.5f);
    }

    REQUIRE(frame.f0 == 440.0f);
    REQUIRE(frame.f0Confidence == 0.85f);
    REQUIRE(frame.numPartials == 3);
    REQUIRE(frame.spectralCentroid > 0.0f);
    REQUIRE(frame.brightness > 0.0f);
    REQUIRE(frame.noisiness >= 0.0f);
    REQUIRE(frame.noisiness <= 1.0f);
    REQUIRE(frame.globalAmplitude > 0.0f);
}
