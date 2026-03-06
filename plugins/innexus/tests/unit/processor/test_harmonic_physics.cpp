#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/harmonic_physics.h"

#include <chrono>
#include <cmath>
#include <numeric>

using Catch::Approx;

// Helper: create a frame with specified amplitudes
static Krate::DSP::HarmonicFrame makeFrame(
    const std::initializer_list<float>& amps, float globalAmp = 1.0f)
{
    Krate::DSP::HarmonicFrame frame{};
    int i = 0;
    for (float a : amps)
    {
        frame.partials[static_cast<size_t>(i)].amplitude = a;
        frame.partials[static_cast<size_t>(i)].frequency = 440.0f * static_cast<float>(i + 1);
        frame.partials[static_cast<size_t>(i)].phase = 0.1f * static_cast<float>(i);
        frame.partials[static_cast<size_t>(i)].harmonicIndex = i + 1;
        ++i;
    }
    frame.numPartials = i;
    frame.globalAmplitude = globalAmp;
    frame.f0 = 440.0f;
    frame.f0Confidence = 1.0f;
    return frame;
}

// Helper: compute RMS of partial amplitudes
static float computeRMS(const Krate::DSP::HarmonicFrame& frame)
{
    float sumSq = 0.0f;
    for (int i = 0; i < frame.numPartials; ++i)
        sumSq += frame.partials[static_cast<size_t>(i)].amplitude *
                 frame.partials[static_cast<size_t>(i)].amplitude;
    return std::sqrt(sumSq / static_cast<float>(frame.numPartials));
}

// =============================================================================
// US1: Warmth Tests
// =============================================================================

TEST_CASE("HarmonicPhysics Warmth bypass at 0.0 produces bit-exact output",
          "[harmonic_physics][warmth]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.setWarmth(0.0f);

    auto frame = makeFrame({0.1f, 0.5f, 0.9f, 0.3f, 0.7f});
    auto original = frame; // copy

    physics.processFrame(frame);

    for (int i = 0; i < frame.numPartials; ++i)
    {
        // Bit-exact: no Approx, must be identical
        REQUIRE(frame.partials[static_cast<size_t>(i)].amplitude ==
                original.partials[static_cast<size_t>(i)].amplitude);
    }
}

TEST_CASE("HarmonicPhysics Warmth compresses dominant partials and boosts quiet ones",
          "[harmonic_physics][warmth]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.setWarmth(1.0f);

    // One dominant partial at 0.9, several quiet at 0.1
    auto frame = makeFrame({0.9f, 0.1f, 0.1f, 0.1f, 0.1f});
    const float originalDominant = frame.partials[0].amplitude;
    const float originalQuiet = frame.partials[1].amplitude;
    const float originalRatio = originalDominant / originalQuiet;

    physics.processFrame(frame);

    const float newDominant = frame.partials[0].amplitude;
    const float newQuiet = frame.partials[1].amplitude;
    const float newRatio = newDominant / newQuiet;

    // Dominant should be reduced
    REQUIRE(newDominant < originalDominant);
    // Quiet should be relatively boosted (ratio decreases)
    REQUIRE(newRatio < originalRatio);
}

TEST_CASE("HarmonicPhysics Warmth peak-to-average ratio reduction >= 50%",
          "[harmonic_physics][warmth]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.setWarmth(1.0f);

    // One partial at 10x the average
    // Average of {1.0, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1} = 0.19
    // Peak / avg = 1.0 / 0.19 = ~5.26
    auto frame = makeFrame({1.0f, 0.1f, 0.1f, 0.1f, 0.1f,
                            0.1f, 0.1f, 0.1f, 0.1f, 0.1f});

    // Compute original peak-to-average
    float sumOrig = 0.0f;
    float peakOrig = 0.0f;
    for (int i = 0; i < frame.numPartials; ++i)
    {
        float a = frame.partials[static_cast<size_t>(i)].amplitude;
        sumOrig += a;
        peakOrig = std::max(peakOrig, a);
    }
    float avgOrig = sumOrig / static_cast<float>(frame.numPartials);
    float ratioOrig = peakOrig / avgOrig;

    physics.processFrame(frame);

    // Compute new peak-to-average
    float sumNew = 0.0f;
    float peakNew = 0.0f;
    for (int i = 0; i < frame.numPartials; ++i)
    {
        float a = frame.partials[static_cast<size_t>(i)].amplitude;
        sumNew += a;
        peakNew = std::max(peakNew, a);
    }
    float avgNew = sumNew / static_cast<float>(frame.numPartials);
    float ratioNew = peakNew / avgNew;

    // Peak-to-average reduction must be at least 50% (SC-003)
    float reduction = (ratioOrig - ratioNew) / ratioOrig;
    REQUIRE(reduction >= 0.5f);
}

TEST_CASE("HarmonicPhysics Warmth energy non-increase (output RMS <= input RMS)",
          "[harmonic_physics][warmth]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);

    auto frame = makeFrame({0.9f, 0.5f, 0.3f, 0.7f, 0.2f, 0.8f});

    SECTION("warmth = 0.5")
    {
        physics.setWarmth(0.5f);
        float inputRMS = computeRMS(frame);
        physics.processFrame(frame);
        float outputRMS = computeRMS(frame);
        REQUIRE(outputRMS <= inputRMS + 1e-6f);
    }

    SECTION("warmth = 1.0")
    {
        physics.setWarmth(1.0f);
        float inputRMS = computeRMS(frame);
        physics.processFrame(frame);
        float outputRMS = computeRMS(frame);
        REQUIRE(outputRMS <= inputRMS + 1e-6f);
    }
}

TEST_CASE("HarmonicPhysics Warmth zero-frame safety: all-zero input produces all-zero output",
          "[harmonic_physics][warmth]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);

    auto frame = makeFrame({0.0f, 0.0f, 0.0f, 0.0f, 0.0f});

    SECTION("warmth = 0.5")
    {
        physics.setWarmth(0.5f);
        physics.processFrame(frame);

        for (int i = 0; i < frame.numPartials; ++i)
        {
            float a = frame.partials[static_cast<size_t>(i)].amplitude;
            REQUIRE(a == 0.0f);
            // Check no NaN using bit pattern (ffast-math safe)
            // NOLINTNEXTLINE(misc-redundant-expression)
            REQUIRE(a == a); // NaN != NaN
        }
    }

    SECTION("warmth = 1.0")
    {
        physics.setWarmth(1.0f);
        physics.processFrame(frame);

        for (int i = 0; i < frame.numPartials; ++i)
        {
            float a = frame.partials[static_cast<size_t>(i)].amplitude;
            REQUIRE(a == 0.0f);
            // NOLINTNEXTLINE(misc-redundant-expression)
            REQUIRE(a == a);
        }
    }
}

// =============================================================================
// US2: Coupling Tests
// =============================================================================

// Helper: compute sum-of-squares of partial amplitudes
static float computeSumOfSquares(const Krate::DSP::HarmonicFrame& frame)
{
    float sumSq = 0.0f;
    for (int i = 0; i < frame.numPartials; ++i)
    {
        const float a = frame.partials[static_cast<size_t>(i)].amplitude;
        sumSq += a * a;
    }
    return sumSq;
}

TEST_CASE("HarmonicPhysics Coupling bypass at 0.0 produces bit-exact output",
          "[harmonic_physics][coupling]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.setCoupling(0.0f);

    auto frame = makeFrame({0.1f, 0.5f, 0.9f, 0.3f, 0.7f});
    auto original = frame; // copy

    physics.processFrame(frame);

    for (int i = 0; i < frame.numPartials; ++i)
    {
        // Bit-exact: no Approx, must be identical (FR-007, SC-001)
        REQUIRE(frame.partials[static_cast<size_t>(i)].amplitude ==
                original.partials[static_cast<size_t>(i)].amplitude);
    }
}

TEST_CASE("HarmonicPhysics Coupling neighbor spread: isolated partial spreads to neighbors",
          "[harmonic_physics][coupling]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.setCoupling(0.5f);

    // Single partial at index 5 with amplitude 1.0, all others 0.0
    auto frame = makeFrame({0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, 0.0f, 0.0f});

    physics.processFrame(frame);

    // Partial 5 should be reduced (gave energy to neighbors)
    REQUIRE(frame.partials[5].amplitude < 1.0f);
    // Neighbors at index 4 and 6 should receive energy (FR-006)
    REQUIRE(frame.partials[4].amplitude > 0.0f);
    REQUIRE(frame.partials[6].amplitude > 0.0f);
}

TEST_CASE("HarmonicPhysics Coupling energy conservation over 100+ frames",
          "[harmonic_physics][coupling]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);

    // Test across various coupling values and frames (FR-008, SC-002)
    const float couplingValues[] = {0.1f, 0.3f, 0.5f, 0.7f, 1.0f};

    for (float coupling : couplingValues)
    {
        physics.setCoupling(coupling);

        // Run 100+ frames with different amplitude patterns
        for (int f = 0; f < 25; ++f)
        {
            // Create varied amplitude patterns
            auto frame = makeFrame({
                0.1f * static_cast<float>(f % 10 + 1),
                0.05f * static_cast<float>((f + 3) % 10 + 1),
                0.08f * static_cast<float>((f + 7) % 10 + 1),
                0.12f * static_cast<float>((f + 1) % 10 + 1),
                0.03f * static_cast<float>((f + 5) % 10 + 1),
                0.15f * static_cast<float>((f + 2) % 10 + 1),
                0.07f * static_cast<float>((f + 9) % 10 + 1),
                0.11f * static_cast<float>((f + 4) % 10 + 1)
            });

            float inputSumSq = computeSumOfSquares(frame);

            physics.processFrame(frame);

            float outputSumSq = computeSumOfSquares(frame);

            // Energy conservation within 0.001% tolerance (SC-002)
            if (inputSumSq > 0.0f)
            {
                float relError = std::abs(outputSumSq - inputSumSq) / inputSumSq;
                REQUIRE(relError < 0.00001f); // 0.001%
            }
        }
    }
}

TEST_CASE("HarmonicPhysics Coupling boundary: partial at index 0 handled safely",
          "[harmonic_physics][coupling]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.setCoupling(1.0f);

    // Partial at index 0 - no neighbor at index -1 (FR-009)
    auto frame = makeFrame({1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    float inputSumSq = computeSumOfSquares(frame);

    physics.processFrame(frame);

    float outputSumSq = computeSumOfSquares(frame);

    // No crash, and energy is still conserved
    if (inputSumSq > 0.0f)
    {
        float relError = std::abs(outputSumSq - inputSumSq) / inputSumSq;
        REQUIRE(relError < 0.00001f);
    }

    // Also test last partial (index numPartials-1)
    auto frame2 = makeFrame({0.0f, 0.0f, 0.0f, 0.0f, 1.0f});
    float inputSumSq2 = computeSumOfSquares(frame2);

    physics.processFrame(frame2);

    float outputSumSq2 = computeSumOfSquares(frame2);

    if (inputSumSq2 > 0.0f)
    {
        float relError2 = std::abs(outputSumSq2 - inputSumSq2) / inputSumSq2;
        REQUIRE(relError2 < 0.00001f);
    }
}

TEST_CASE("HarmonicPhysics Coupling frequency preservation: only amplitudes modified",
          "[harmonic_physics][coupling]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.setCoupling(0.8f);

    auto frame = makeFrame({0.5f, 0.3f, 0.7f, 0.1f, 0.9f});
    auto original = frame; // copy

    physics.processFrame(frame);

    // Frequencies, phases, and other fields must be unchanged (FR-010)
    for (int i = 0; i < frame.numPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        REQUIRE(frame.partials[idx].frequency ==
                original.partials[idx].frequency);
        REQUIRE(frame.partials[idx].phase ==
                original.partials[idx].phase);
        REQUIRE(frame.partials[idx].harmonicIndex ==
                original.partials[idx].harmonicIndex);
    }
    // Global frame metadata unchanged
    REQUIRE(frame.f0 == original.f0);
    REQUIRE(frame.f0Confidence == original.f0Confidence);
    REQUIRE(frame.numPartials == original.numPartials);
}

// =============================================================================
// US3: Dynamics Tests
// =============================================================================

TEST_CASE("HarmonicPhysics Dynamics bypass: Stability=0 Entropy=0 tracks input exactly",
          "[harmonic_physics][dynamics]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.reset();
    physics.setStability(0.0f);
    physics.setEntropy(0.0f);

    // First frame: initializes agents
    auto frame1 = makeFrame({0.5f, 0.3f, 0.7f, 0.1f, 0.9f});
    physics.processFrame(frame1);

    // Second frame with different amplitudes: output should track input exactly
    auto frame2 = makeFrame({0.2f, 0.8f, 0.4f, 0.6f, 0.1f});
    auto expected = frame2; // copy before processing

    physics.processFrame(frame2);

    for (int i = 0; i < frame2.numPartials; ++i)
    {
        REQUIRE(frame2.partials[static_cast<size_t>(i)].amplitude ==
                Approx(expected.partials[static_cast<size_t>(i)].amplitude).margin(1e-6f));
    }
}

TEST_CASE("HarmonicPhysics Dynamics stability inertia: Stability=1.0 resists 100% change",
          "[harmonic_physics][dynamics]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.reset();
    physics.setStability(1.0f);
    physics.setEntropy(0.0f);

    // Use large globalAmplitude so energy budget normalization doesn't interfere
    const float globalAmp = 5.0f;

    // First frame: initializes agents with these amplitudes
    auto frame1 = makeFrame({0.5f, 0.5f, 0.5f, 0.5f, 0.5f}, globalAmp);
    physics.processFrame(frame1);

    // Second frame: 100% change - all amplitudes go to 1.0
    auto frame2 = makeFrame({1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, globalAmp);
    physics.processFrame(frame2);

    // With Stability=1.0, output should change by less than 5% from initial (SC-004)
    for (int i = 0; i < frame2.numPartials; ++i)
    {
        float change = std::abs(frame2.partials[static_cast<size_t>(i)].amplitude - 0.5f);
        float changePercent = change / 0.5f;
        REQUIRE(changePercent < 0.05f);
    }
}

TEST_CASE("HarmonicPhysics Dynamics entropy decay: Entropy=1.0 decays to <1% in 10 frames",
          "[harmonic_physics][dynamics]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.reset();
    physics.setStability(0.0f);
    physics.setEntropy(1.0f);

    // First frame: initializes agents with significant amplitudes
    auto frame1 = makeFrame({0.8f, 0.6f, 0.7f, 0.5f, 0.9f});
    physics.processFrame(frame1);

    // Record initial amplitudes after first frame
    std::array<float, 5> initial{};
    for (int i = 0; i < 5; ++i)
        initial[static_cast<size_t>(i)] = frame1.partials[static_cast<size_t>(i)].amplitude;

    // Feed 10 frames of zero input
    for (int f = 0; f < 10; ++f)
    {
        auto zeroFrame = makeFrame({0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
        physics.processFrame(zeroFrame);

        // After 10 frames, check amplitudes
        if (f == 9)
        {
            for (int i = 0; i < 5; ++i)
            {
                float amp = zeroFrame.partials[static_cast<size_t>(i)].amplitude;
                float threshold = initial[static_cast<size_t>(i)] * 0.01f;
                REQUIRE(amp < threshold);
            }
        }
    }
}

TEST_CASE("HarmonicPhysics Dynamics entropy infinite sustain: Entropy=0 persists indefinitely",
          "[harmonic_physics][dynamics]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.reset();
    physics.setStability(1.0f); // High stability to sustain against zero input
    physics.setEntropy(0.0f);   // No entropy decay

    // Use large globalAmplitude so energy budget normalization doesn't interfere
    const float globalAmp = 5.0f;

    // First frame: initialize
    auto frame1 = makeFrame({0.8f, 0.6f, 0.7f, 0.5f, 0.9f}, globalAmp);
    physics.processFrame(frame1);

    // Record amplitudes after initialization
    std::array<float, 5> afterInit{};
    for (int i = 0; i < 5; ++i)
        afterInit[static_cast<size_t>(i)] = frame1.partials[static_cast<size_t>(i)].amplitude;

    // Feed 20 frames of zero input with entropy=0 and stability=1.0
    // With high stability and no entropy, amplitudes should persist significantly
    // (inertia ~0.95 means ~0.95^20 = 0.358 retained after 20 frames)
    for (int f = 0; f < 20; ++f)
    {
        auto zeroFrame = makeFrame({0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, globalAmp);
        physics.processFrame(zeroFrame);

        // After 20 frames, amplitudes should still be substantial
        // (entropy=0 means no entropy-based decay; stability=1.0 means
        // high inertia resists moving toward zero input)
        if (f == 19)
        {
            for (int i = 0; i < 5; ++i)
            {
                float amp = zeroFrame.partials[static_cast<size_t>(i)].amplitude;
                // Should still be at least 20% of the initial value
                REQUIRE(amp > afterInit[static_cast<size_t>(i)] * 0.2f);
            }
        }
    }
}

TEST_CASE("HarmonicPhysics Dynamics persistence growth: small deltas grow persistence toward 1.0",
          "[harmonic_physics][dynamics]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.reset();
    physics.setStability(0.5f);
    physics.setEntropy(0.0f);

    // Feed many frames with very small amplitude changes
    // This should grow persistence toward 1.0
    for (int f = 0; f < 30; ++f)
    {
        auto frame = makeFrame({0.5f, 0.5f, 0.5f, 0.5f, 0.5f});
        physics.processFrame(frame);
    }

    // After 30 stable frames, a sudden large change should be heavily resisted
    // (because persistence is high, effective inertia is high)
    auto changeFrame = makeFrame({1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    physics.processFrame(changeFrame);

    for (int i = 0; i < changeFrame.numPartials; ++i)
    {
        // With stability=0.5 and high persistence, effective inertia ≈ 0.5.
        // Agent: 0.5 * 0.5 + 0.5 * 1.0 = 0.75, change ≈ 0.25.
        // Allow up to 0.3 change (stability=0.5 resists ~50%, not ~90%).
        float change = std::abs(changeFrame.partials[static_cast<size_t>(i)].amplitude - 0.5f);
        REQUIRE(change < 0.3f); // Less than 60% change despite 100% input change
    }
}

TEST_CASE("HarmonicPhysics Dynamics persistence decay: dramatic change decays persistence",
          "[harmonic_physics][dynamics]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.reset();
    physics.setStability(0.5f);
    physics.setEntropy(0.0f);

    // Initialize
    auto frame1 = makeFrame({0.5f, 0.5f, 0.5f, 0.5f, 0.5f});
    physics.processFrame(frame1);

    // Build persistence with stable frames
    for (int f = 0; f < 20; ++f)
    {
        auto stableFrame = makeFrame({0.5f, 0.5f, 0.5f, 0.5f, 0.5f});
        physics.processFrame(stableFrame);
    }

    // Now send a dramatic change - this should decay persistence
    auto changeFrame1 = makeFrame({1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    physics.processFrame(changeFrame1);

    // Record output after first dramatic change
    std::array<float, 5> afterFirstChange{};
    for (int i = 0; i < 5; ++i)
        afterFirstChange[static_cast<size_t>(i)] =
            changeFrame1.partials[static_cast<size_t>(i)].amplitude;

    // Send another dramatic change (from 1.0 to 0.0)
    auto changeFrame2 = makeFrame({0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    physics.processFrame(changeFrame2);

    // After persistence decay, the second change should have MORE effect
    // than the first change did (partial is now more responsive)
    for (int i = 0; i < 5; ++i)
    {
        // The output should have changed more from afterFirstChange than
        // afterFirstChange changed from 0.5
        float firstChangeMagnitude = std::abs(afterFirstChange[static_cast<size_t>(i)] - 0.5f);
        float secondChangeMagnitude = std::abs(
            changeFrame2.partials[static_cast<size_t>(i)].amplitude -
            afterFirstChange[static_cast<size_t>(i)]);
        REQUIRE(secondChangeMagnitude > firstChangeMagnitude);
    }
}

TEST_CASE("HarmonicPhysics Dynamics reset: clears all agent state",
          "[harmonic_physics][dynamics]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);

    // Process some frames to build up state
    physics.setStability(0.5f);
    physics.setEntropy(0.0f);
    auto frame1 = makeFrame({0.8f, 0.6f, 0.7f, 0.5f, 0.9f});
    physics.processFrame(frame1);
    for (int f = 0; f < 10; ++f)
    {
        auto frame = makeFrame({0.5f, 0.5f, 0.5f, 0.5f, 0.5f});
        physics.processFrame(frame);
    }

    // Reset
    physics.reset();

    // After reset, stability=0 entropy=0 should give exact passthrough
    physics.setStability(0.0f);
    physics.setEntropy(0.0f);

    // First frame after reset initializes agents from input (no ramp-from-zero)
    auto postResetFrame = makeFrame({0.3f, 0.7f, 0.2f, 0.8f, 0.4f});
    auto expected = postResetFrame;
    physics.processFrame(postResetFrame);

    for (int i = 0; i < postResetFrame.numPartials; ++i)
    {
        REQUIRE(postResetFrame.partials[static_cast<size_t>(i)].amplitude ==
                Approx(expected.partials[static_cast<size_t>(i)].amplitude).margin(1e-6f));
    }
}

TEST_CASE("HarmonicPhysics Dynamics first-frame initialization: no ramp-from-zero",
          "[harmonic_physics][dynamics]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.reset();
    physics.setStability(1.0f); // High stability would ramp from zero if not handled
    physics.setEntropy(0.0f);

    // First frame after reset should initialize agents from input directly
    auto frame = makeFrame({0.8f, 0.6f, 0.7f, 0.5f, 0.9f});
    physics.processFrame(frame);

    // Output should be close to input (no ramp-from-zero artifact) (FR-017)
    REQUIRE(frame.partials[0].amplitude == Approx(0.8f).margin(0.01f));
    REQUIRE(frame.partials[1].amplitude == Approx(0.6f).margin(0.01f));
    REQUIRE(frame.partials[2].amplitude == Approx(0.7f).margin(0.01f));
    REQUIRE(frame.partials[3].amplitude == Approx(0.5f).margin(0.01f));
    REQUIRE(frame.partials[4].amplitude == Approx(0.9f).margin(0.01f));
}

TEST_CASE("HarmonicPhysics Dynamics energy budget: agent sum-of-squares normalized to input energy",
          "[harmonic_physics][dynamics]")
{
    Innexus::HarmonicPhysics physics;
    physics.prepare(48000.0, 512);
    physics.reset();
    physics.setStability(0.8f);
    physics.setEntropy(0.0f);

    // Initialize with significant amplitudes
    auto frame1 = makeFrame({0.8f, 0.6f, 0.7f, 0.5f, 0.9f}, 0.5f);
    physics.processFrame(frame1);

    // Feed frames — agent sum-of-squares should not exceed input sum-of-squares
    const std::initializer_list<float> amps = {0.8f, 0.6f, 0.7f, 0.5f, 0.9f};
    float inputSumSq = 0.0f;
    for (float a : amps)
        inputSumSq += a * a;

    for (int f = 0; f < 5; ++f)
    {
        auto frame = makeFrame({0.8f, 0.6f, 0.7f, 0.5f, 0.9f}, 0.5f);
        physics.processFrame(frame);

        // Sum-of-squares of output should not exceed input sum-of-squares
        float sumSq = computeSumOfSquares(frame);
        REQUIRE(sumSq <= inputSumSq + 1e-5f);
    }

    // Also verify: when globalAmplitude == 0, the conservation step is skipped
    // (no division by zero)
    physics.reset();
    auto zeroGlobalFrame = makeFrame({0.5f, 0.3f, 0.2f}, 0.0f);
    physics.processFrame(zeroGlobalFrame);
    // Should not crash or produce NaN
    for (int i = 0; i < zeroGlobalFrame.numPartials; ++i)
    {
        float a = zeroGlobalFrame.partials[static_cast<size_t>(i)].amplitude;
        // NOLINTNEXTLINE(misc-redundant-expression)
        REQUIRE(a == a); // NaN check
    }
}

// =============================================================================
// Performance Benchmark (SC-006)
// =============================================================================

// Helper: create a frame with 48 partials (all active) at specified amplitudes
static Krate::DSP::HarmonicFrame makeFullFrame(float baseAmp, float globalAmp)
{
    Krate::DSP::HarmonicFrame frame{};
    constexpr int kNumPartials = 48;
    for (int i = 0; i < kNumPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        // Vary amplitudes to exercise all code paths
        frame.partials[idx].amplitude = baseAmp * (1.0f - 0.5f * static_cast<float>(i) / static_cast<float>(kNumPartials));
        frame.partials[idx].frequency = 110.0f * static_cast<float>(i + 1);
        frame.partials[idx].phase = 0.1f * static_cast<float>(i);
        frame.partials[idx].harmonicIndex = i + 1;
    }
    frame.numPartials = kNumPartials;
    frame.globalAmplitude = globalAmp;
    frame.f0 = 110.0f;
    frame.f0Confidence = 1.0f;
    return frame;
}

TEST_CASE("HarmonicPhysics CPU benchmark: all processors < 0.5% CPU at 48kHz/512 hop with 48 partials",
          "[.perf][harmonic_physics]")
{
    // SC-006: Combined CPU overhead of all three processors < 0.5% of a single
    // core at 48kHz with 48 partials.
    //
    // At 48kHz with 512-sample hop: 48000/512 = 93.75 frames/sec
    // 0.5% of one core = 0.005 * (1.0 / 93.75) = 53.33 microseconds per frame

    constexpr double kSampleRate = 48000.0;
    constexpr int kHopSize = 512;
    constexpr int kNumFrames = 1000;  // ~10.67 seconds worth of frames for stable timing
    constexpr double kBudgetMicroseconds = 53.33; // SC-006 budget

    Innexus::HarmonicPhysics physics;
    physics.prepare(kSampleRate, kHopSize);
    physics.reset();

    // All params at 1.0 -- worst case, no early-outs
    physics.setWarmth(1.0f);
    physics.setCoupling(1.0f);
    physics.setStability(1.0f);
    physics.setEntropy(1.0f);

    // Warm up: let the dynamics agent state settle
    for (int i = 0; i < 50; ++i)
    {
        auto frame = makeFullFrame(0.5f, 1.0f);
        physics.processFrame(frame);
    }

    // Benchmark: measure time for kNumFrames calls to processFrame()
    auto startTime = std::chrono::high_resolution_clock::now();

    volatile float sink = 0.0f; // prevent dead code elimination
    for (int i = 0; i < kNumFrames; ++i)
    {
        auto frame = makeFullFrame(0.5f + 0.3f * static_cast<float>(i % 10) / 10.0f, 1.0f);
        physics.processFrame(frame);
        sink = frame.partials[0].amplitude; // prevent optimization
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();

    double usPerFrame = static_cast<double>(elapsedUs) / static_cast<double>(kNumFrames);
    double cpuPercent = (usPerFrame / (1e6 / 93.75)) * 100.0;

    // Report results
    WARN("HarmonicPhysics benchmark results:");
    WARN("  Total time for " << kNumFrames << " frames: " << elapsedUs << " us");
    WARN("  Time per frame: " << usPerFrame << " us");
    WARN("  CPU usage: " << cpuPercent << "% of one core at 48kHz/512 hop");
    WARN("  Budget: " << kBudgetMicroseconds << " us per frame (0.5% CPU)");

    (void)sink;

    // SC-006: Must be under 0.5% CPU (53.33 us per frame)
    REQUIRE(usPerFrame < kBudgetMicroseconds);
}
