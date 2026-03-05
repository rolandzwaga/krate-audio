// ==============================================================================
// Harmonic Snapshot DSP Utility Tests (M5)
// ==============================================================================
// Tests for captureSnapshot() and recallSnapshotToFrame() conversion utilities.
//
// Feature: 119-harmonic-memory
// Requirements: FR-001, FR-002, FR-003, FR-004, SC-001
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/harmonic_snapshot.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include <cmath>

using Catch::Approx;

// =============================================================================
// Test Helpers
// =============================================================================

/// Build a synthetic HarmonicFrame with known values for testing.
static Krate::DSP::HarmonicFrame makeSyntheticFrame(
    int numPartials = 4,
    float f0 = 440.0f,
    float baseAmplitude = 0.5f,
    float inharmonicity = 0.0f)
{
    Krate::DSP::HarmonicFrame frame{};
    frame.f0 = f0;
    frame.f0Confidence = 0.9f;
    frame.numPartials = numPartials;
    frame.globalAmplitude = 0.8f;
    frame.spectralCentroid = 1500.0f;
    frame.brightness = 0.6f;
    frame.noisiness = 0.1f;

    for (int i = 0; i < numPartials; ++i)
    {
        auto& p = frame.partials[static_cast<size_t>(i)];
        p.harmonicIndex = i + 1;
        float relFreq = static_cast<float>(i + 1) + inharmonicity * static_cast<float>(i);
        p.relativeFrequency = relFreq;
        p.frequency = relFreq * f0;
        p.amplitude = baseAmplitude / static_cast<float>(i + 1);
        p.phase = static_cast<float>(i) * 0.5f;
        p.inharmonicDeviation = inharmonicity * static_cast<float>(i);
        p.stability = 0.8f;
        p.age = 5;
    }

    return frame;
}

static Krate::DSP::ResidualFrame makeSyntheticResidual(
    float totalEnergy = 0.05f,
    float bandBase = 0.003f)
{
    Krate::DSP::ResidualFrame residual{};
    residual.totalEnergy = totalEnergy;
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
        residual.bandEnergies[b] = bandBase + static_cast<float>(b) * 0.001f;
    residual.transientFlag = false;
    return residual;
}

// =============================================================================
// T018: captureSnapshot extracts relativeFreqs matching source
// =============================================================================

TEST_CASE("M5 captureSnapshot: relativeFreqs match source frame within 1e-6",
          "[dsp][m5][snapshot][capture]")
{
    auto frame = makeSyntheticFrame(4, 440.0f, 0.5f, 0.02f);
    auto residual = makeSyntheticResidual();

    auto snap = Krate::DSP::captureSnapshot(frame, residual);

    REQUIRE(snap.numPartials == 4);
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(snap.relativeFreqs[static_cast<size_t>(i)]
                == Approx(frame.partials[static_cast<size_t>(i)].relativeFrequency)
                       .margin(1e-6f));
    }
}

// =============================================================================
// T019: captureSnapshot L2-normalizes amplitudes
// =============================================================================

TEST_CASE("M5 captureSnapshot: amplitudes are L2-normalized (sum of squares == 1.0)",
          "[dsp][m5][snapshot][capture]")
{
    auto frame = makeSyntheticFrame(4, 440.0f, 0.5f);
    auto residual = makeSyntheticResidual();

    auto snap = Krate::DSP::captureSnapshot(frame, residual);

    float sumSquares = 0.0f;
    for (int i = 0; i < snap.numPartials; ++i)
        sumSquares += snap.normalizedAmps[static_cast<size_t>(i)]
                    * snap.normalizedAmps[static_cast<size_t>(i)];

    REQUIRE(sumSquares == Approx(1.0f).margin(1e-6f));
}

// =============================================================================
// T020: captureSnapshot stores both relativeFreqs and inharmonicDeviation
// =============================================================================

TEST_CASE("M5 captureSnapshot: stores inharmonicDeviation matching relativeFreqs - round(relativeFreqs)",
          "[dsp][m5][snapshot][capture]")
{
    // Use a frame with known inharmonicity
    auto frame = makeSyntheticFrame(4, 440.0f, 0.5f, 0.03f);
    auto residual = makeSyntheticResidual();

    auto snap = Krate::DSP::captureSnapshot(frame, residual);

    for (int i = 0; i < snap.numPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        // inharmonicDeviation should match source partial's inharmonicDeviation
        REQUIRE(snap.inharmonicDeviation[idx]
                == Approx(frame.partials[idx].inharmonicDeviation).margin(1e-6f));
        // And it should equal relativeFreqs - round(relativeFreqs)
        float expected = snap.relativeFreqs[idx] - std::round(snap.relativeFreqs[idx]);
        REQUIRE(snap.inharmonicDeviation[idx] == Approx(expected).margin(1e-6f));
    }
}

// =============================================================================
// T021: captureSnapshot stores phases from source
// =============================================================================

TEST_CASE("M5 captureSnapshot: stores phases from source partials",
          "[dsp][m5][snapshot][capture]")
{
    auto frame = makeSyntheticFrame(4, 440.0f, 0.5f);
    auto residual = makeSyntheticResidual();

    auto snap = Krate::DSP::captureSnapshot(frame, residual);

    for (int i = 0; i < snap.numPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        REQUIRE(snap.phases[idx]
                == Approx(frame.partials[idx].phase).margin(1e-6f));
    }
}

// =============================================================================
// T022: captureSnapshot extracts residual data
// =============================================================================

TEST_CASE("M5 captureSnapshot: extracts residual bandEnergies and totalEnergy",
          "[dsp][m5][snapshot][capture]")
{
    auto frame = makeSyntheticFrame();
    auto residual = makeSyntheticResidual(0.07f, 0.005f);

    auto snap = Krate::DSP::captureSnapshot(frame, residual);

    REQUIRE(snap.residualEnergy == Approx(residual.totalEnergy).margin(1e-6f));
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        REQUIRE(snap.residualBands[b]
                == Approx(residual.bandEnergies[b]).margin(1e-6f));
    }
}

// =============================================================================
// T023: captureSnapshot extracts metadata
// =============================================================================

TEST_CASE("M5 captureSnapshot: extracts metadata (f0, globalAmplitude, spectralCentroid, brightness)",
          "[dsp][m5][snapshot][capture]")
{
    auto frame = makeSyntheticFrame(4, 440.0f, 0.5f);
    auto residual = makeSyntheticResidual();

    auto snap = Krate::DSP::captureSnapshot(frame, residual);

    REQUIRE(snap.f0Reference == Approx(frame.f0).margin(1e-6f));
    REQUIRE(snap.globalAmplitude == Approx(frame.globalAmplitude).margin(1e-6f));
    REQUIRE(snap.spectralCentroid == Approx(frame.spectralCentroid).margin(1e-6f));
    REQUIRE(snap.brightness == Approx(frame.brightness).margin(1e-6f));
}

// =============================================================================
// T024: captureSnapshot with empty frame (numPartials == 0)
// =============================================================================

TEST_CASE("M5 captureSnapshot: empty frame stores numPartials=0 and zero amplitudes",
          "[dsp][m5][snapshot][capture]")
{
    Krate::DSP::HarmonicFrame frame{};
    frame.numPartials = 0;
    frame.f0 = 0.0f;

    Krate::DSP::ResidualFrame residual{};

    auto snap = Krate::DSP::captureSnapshot(frame, residual);

    REQUIRE(snap.numPartials == 0);
    REQUIRE(snap.residualEnergy == Approx(0.0f).margin(1e-6f));

    // All amplitude arrays should be zero (no div-by-zero)
    for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
    {
        REQUIRE(snap.normalizedAmps[i] == Approx(0.0f).margin(1e-6f));
    }
}

// =============================================================================
// T025: recallSnapshotToFrame reconstructs partial data
// =============================================================================

TEST_CASE("M5 recallSnapshotToFrame: reconstructs HarmonicFrame with correct partial data",
          "[dsp][m5][snapshot][recall]")
{
    // Create a snapshot manually
    Krate::DSP::HarmonicSnapshot snap{};
    snap.f0Reference = 440.0f;
    snap.numPartials = 3;
    snap.relativeFreqs[0] = 1.0f;
    snap.relativeFreqs[1] = 2.01f;
    snap.relativeFreqs[2] = 3.03f;
    snap.normalizedAmps[0] = 0.7f;
    snap.normalizedAmps[1] = 0.5f;
    snap.normalizedAmps[2] = 0.3f;
    snap.phases[0] = 0.0f;
    snap.phases[1] = 1.0f;
    snap.phases[2] = 2.0f;
    snap.inharmonicDeviation[0] = 0.0f;
    snap.inharmonicDeviation[1] = 0.01f;
    snap.inharmonicDeviation[2] = 0.03f;

    Krate::DSP::HarmonicFrame frame{};
    Krate::DSP::ResidualFrame residual{};

    Krate::DSP::recallSnapshotToFrame(snap, frame, residual);

    REQUIRE(frame.numPartials == 3);
    for (int i = 0; i < 3; ++i)
    {
        auto idx = static_cast<size_t>(i);
        REQUIRE(frame.partials[idx].relativeFrequency
                == Approx(snap.relativeFreqs[idx]).margin(1e-6f));
        REQUIRE(frame.partials[idx].amplitude
                == Approx(snap.normalizedAmps[idx]).margin(1e-6f));
        REQUIRE(frame.partials[idx].phase
                == Approx(snap.phases[idx]).margin(1e-6f));
    }
}

// =============================================================================
// T026: recallSnapshotToFrame derives harmonicIndex correctly
// =============================================================================

TEST_CASE("M5 recallSnapshotToFrame: derives harmonicIndex from relativeFreqs - inharmonicDeviation, clamped >= 1",
          "[dsp][m5][snapshot][recall]")
{
    Krate::DSP::HarmonicSnapshot snap{};
    snap.f0Reference = 440.0f;
    snap.numPartials = 3;
    // Partial 0: relFreq=1.0, dev=0.0 -> harmonicIndex = round(1.0-0.0) = 1
    snap.relativeFreqs[0] = 1.0f;
    snap.inharmonicDeviation[0] = 0.0f;
    // Partial 1: relFreq=2.05, dev=0.05 -> harmonicIndex = round(2.0) = 2
    snap.relativeFreqs[1] = 2.05f;
    snap.inharmonicDeviation[1] = 0.05f;
    // Partial 2: relFreq=0.3, dev=-0.7 -> harmonicIndex = round(1.0) = 1 (clamped >= 1)
    snap.relativeFreqs[2] = 0.3f;
    snap.inharmonicDeviation[2] = -0.7f;
    snap.normalizedAmps[0] = 0.5f;
    snap.normalizedAmps[1] = 0.3f;
    snap.normalizedAmps[2] = 0.2f;

    Krate::DSP::HarmonicFrame frame{};
    Krate::DSP::ResidualFrame residual{};
    Krate::DSP::recallSnapshotToFrame(snap, frame, residual);

    REQUIRE(frame.partials[0].harmonicIndex == 1);
    REQUIRE(frame.partials[1].harmonicIndex == 2);
    REQUIRE(frame.partials[2].harmonicIndex >= 1); // clamped
}

// =============================================================================
// T027: recallSnapshotToFrame sets confidence, stability, age
// =============================================================================

TEST_CASE("M5 recallSnapshotToFrame: sets f0Confidence=1, stability=1, age=1 on recalled partials",
          "[dsp][m5][snapshot][recall]")
{
    Krate::DSP::HarmonicSnapshot snap{};
    snap.f0Reference = 440.0f;
    snap.numPartials = 3;
    for (int i = 0; i < 3; ++i)
    {
        auto idx = static_cast<size_t>(i);
        snap.relativeFreqs[idx] = static_cast<float>(i + 1);
        snap.normalizedAmps[idx] = 0.5f;
        snap.inharmonicDeviation[idx] = 0.0f;
    }

    Krate::DSP::HarmonicFrame frame{};
    Krate::DSP::ResidualFrame residual{};
    Krate::DSP::recallSnapshotToFrame(snap, frame, residual);

    REQUIRE(frame.f0Confidence == Approx(1.0f));
    for (int i = 0; i < 3; ++i)
    {
        auto idx = static_cast<size_t>(i);
        REQUIRE(frame.partials[idx].stability == Approx(1.0f));
        REQUIRE(frame.partials[idx].age == 1);
    }
}

// =============================================================================
// T028: Capture -> Recall round-trip preserves data within 1e-6
// =============================================================================

TEST_CASE("M5 capture-recall round-trip: relativeFreqs and L2-normalized amps preserved within 1e-6",
          "[dsp][m5][snapshot][roundtrip]")
{
    auto frame = makeSyntheticFrame(4, 440.0f, 0.5f, 0.01f);
    auto residual = makeSyntheticResidual(0.05f, 0.003f);

    // Capture
    auto snap = Krate::DSP::captureSnapshot(frame, residual);

    // Recall
    Krate::DSP::HarmonicFrame recalledFrame{};
    Krate::DSP::ResidualFrame recalledResidual{};
    Krate::DSP::recallSnapshotToFrame(snap, recalledFrame, recalledResidual);

    // Verify relativeFreqs match original
    REQUIRE(recalledFrame.numPartials == frame.numPartials);
    for (int i = 0; i < frame.numPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        REQUIRE(recalledFrame.partials[idx].relativeFrequency
                == Approx(frame.partials[idx].relativeFrequency).margin(1e-6f));
    }

    // Verify recalled amplitudes are L2-normalized versions of originals
    // Compute expected L2 norm
    float sumSq = 0.0f;
    for (int i = 0; i < frame.numPartials; ++i)
        sumSq += frame.partials[static_cast<size_t>(i)].amplitude
               * frame.partials[static_cast<size_t>(i)].amplitude;
    float invNorm = 1.0f / std::sqrt(sumSq);

    for (int i = 0; i < frame.numPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        float expectedNormAmp = frame.partials[idx].amplitude * invNorm;
        REQUIRE(recalledFrame.partials[idx].amplitude
                == Approx(expectedNormAmp).margin(1e-6f));
    }

    // Verify residual round-trip
    REQUIRE(recalledResidual.totalEnergy == Approx(residual.totalEnergy).margin(1e-6f));
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        REQUIRE(recalledResidual.bandEnergies[b]
                == Approx(residual.bandEnergies[b]).margin(1e-6f));
    }

    // Verify metadata round-trip
    REQUIRE(recalledFrame.f0 == Approx(frame.f0).margin(1e-6f));
    REQUIRE(recalledFrame.globalAmplitude == Approx(frame.globalAmplitude).margin(1e-6f));
    REQUIRE(recalledFrame.spectralCentroid == Approx(frame.spectralCentroid).margin(1e-6f));
    REQUIRE(recalledFrame.brightness == Approx(frame.brightness).margin(1e-6f));
}
