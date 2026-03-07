// ==============================================================================
// SpectralDecayEnvelope Unit Tests
// ==============================================================================
// Tests for the per-partial spectral decay envelope that produces a natural
// fade-out when the sidechain confidence gate freezes a harmonic frame.
// Higher partials decay faster than lower ones, mimicking acoustic behavior.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/spectral_decay_envelope.h"

#include <krate/dsp/processors/harmonic_types.h>

#include <algorithm>
#include <cmath>
#include <numeric>

using Catch::Approx;

static constexpr double kTestSR = 44100.0;
static constexpr size_t kTestBlock = 512;

/// Create a test frame with numPartials partials at uniform amplitude.
static Krate::DSP::HarmonicFrame makeTestFrame(
    float f0 = 440.0f, int numPartials = 16, float amplitude = 0.5f)
{
    Krate::DSP::HarmonicFrame frame{};
    frame.f0 = f0;
    frame.f0Confidence = 0.9f;
    frame.numPartials = numPartials;
    frame.globalAmplitude = amplitude;
    frame.spectralCentroid = 1200.0f;
    frame.brightness = 0.5f;
    frame.noisiness = 0.1f;

    for (int p = 0; p < numPartials; ++p)
    {
        auto& partial = frame.partials[static_cast<size_t>(p)];
        partial.harmonicIndex = p + 1;
        partial.frequency = f0 * static_cast<float>(p + 1);
        partial.amplitude = amplitude;
        partial.phase = 0.0f;
        partial.relativeFrequency = static_cast<float>(p + 1);
        partial.stability = 1.0f;
        partial.age = 100;
    }

    return frame;
}

// =============================================================================
// Basic State Tests
// =============================================================================

TEST_CASE("SpectralDecayEnvelope: inactive by default", "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    CHECK_FALSE(env.isActive());
    CHECK_FALSE(env.isFullyDecayed());
}

TEST_CASE("SpectralDecayEnvelope: inactive envelope does not modify frame",
          "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame();
    auto original = frame;

    env.processBlock(frame);

    // All partial amplitudes should be unchanged
    for (int p = 0; p < frame.numPartials; ++p)
    {
        CHECK(frame.partials[static_cast<size_t>(p)].amplitude ==
              Approx(original.partials[static_cast<size_t>(p)].amplitude));
    }
    CHECK(frame.globalAmplitude == Approx(original.globalAmplitude));
}

TEST_CASE("SpectralDecayEnvelope: activate starts decay", "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame();
    env.activate(frame);

    CHECK(env.isActive());
    CHECK_FALSE(env.isFullyDecayed());
}

// =============================================================================
// Decay Behavior Tests
// =============================================================================

TEST_CASE("SpectralDecayEnvelope: partial amplitudes decrease each block",
          "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame();
    env.activate(frame);

    float prevAmplitude = frame.partials[0].amplitude;

    // Process several blocks and verify amplitudes decrease
    for (int block = 0; block < 10; ++block)
    {
        env.processBlock(frame);
        float currentAmplitude = frame.partials[0].amplitude;
        CHECK(currentAmplitude < prevAmplitude);
        prevAmplitude = currentAmplitude;
    }
}

TEST_CASE("SpectralDecayEnvelope: higher partials decay faster than lower",
          "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame(440.0f, 32, 0.5f);
    env.activate(frame);

    // Process enough blocks to see clear differentiation (~100ms)
    constexpr int numBlocks = 9; // ~105ms at 512/44100
    for (int b = 0; b < numBlocks; ++b)
        env.processBlock(frame);

    // Compare remaining amplitude ratios:
    // Partial 0 (fundamental) should retain more amplitude than partial 15 (mid)
    // Partial 15 (mid) should retain more than partial 31 (high)
    float ampFundamental = frame.partials[0].amplitude;
    float ampMid = frame.partials[15].amplitude;
    float ampHigh = frame.partials[31].amplitude;

    INFO("After ~105ms: fundamental=" << ampFundamental
         << " mid=" << ampMid << " high=" << ampHigh);

    CHECK(ampFundamental > ampMid);
    CHECK(ampMid > ampHigh);
}

TEST_CASE("SpectralDecayEnvelope: fundamental has longest decay",
          "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame(440.0f, 16, 0.5f);
    env.activate(frame);

    // Process ~300ms of blocks
    constexpr int numBlocks = 26; // ~302ms
    for (int b = 0; b < numBlocks; ++b)
        env.processBlock(frame);

    // Fundamental should still be measurably above the highest partial
    float fundamentalDb = 20.0f * std::log10(
        std::max(frame.partials[0].amplitude, 1e-10f) / 0.5f);
    INFO("Fundamental attenuation at ~300ms: " << fundamentalDb << " dB");

    // Highest partial should be significantly more decayed than fundamental
    float highestDb = 20.0f * std::log10(
        std::max(frame.partials[15].amplitude, 1e-10f) / 0.5f);
    INFO("Partial 15 attenuation at ~300ms: " << highestDb << " dB");
    CHECK(frame.partials[0].amplitude > frame.partials[15].amplitude);
}

TEST_CASE("SpectralDecayEnvelope: all partials reach near-zero (fully decayed)",
          "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame(440.0f, 16, 0.5f);
    env.activate(frame);

    // Process enough blocks for full decay.
    // Fundamental tau=0.6s, initial amp=0.5, threshold=1e-4.
    // Need ~440 blocks at 512/44100.
    constexpr int maxBlocks = 600; // ~7 seconds, generous margin
    bool decayed = false;
    int decayBlock = -1;

    for (int b = 0; b < maxBlocks; ++b)
    {
        env.processBlock(frame);
        if (env.isFullyDecayed())
        {
            decayed = true;
            decayBlock = b;
            break;
        }
    }

    INFO("Fully decayed at block " << decayBlock
         << " (" << (decayBlock * kTestBlock / 44.1f) << " ms)");
    CHECK(decayed);

    // Verify all partials are near zero
    for (int p = 0; p < frame.numPartials; ++p)
    {
        CHECK(frame.partials[static_cast<size_t>(p)].amplitude < 1e-4f);
    }
    CHECK(frame.globalAmplitude < 1e-4f);
}

TEST_CASE("SpectralDecayEnvelope: globalAmplitude tracks decayed partials",
          "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame(440.0f, 8, 0.5f);
    env.activate(frame);

    float prevGlobal = frame.globalAmplitude;

    for (int b = 0; b < 10; ++b)
    {
        env.processBlock(frame);
        CHECK(frame.globalAmplitude < prevGlobal);
        CHECK(frame.globalAmplitude > 0.0f);
        prevGlobal = frame.globalAmplitude;
    }
}

TEST_CASE("SpectralDecayEnvelope: spectral centroid decreases during decay",
          "[spectral-decay]")
{
    // Because higher partials decay faster, the spectral centroid should
    // shift downward over time (timbre becomes darker).
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame(440.0f, 16, 0.5f);
    env.activate(frame);

    // Process one block to establish the true computed centroid
    env.processBlock(frame);
    float earlyCentroid = frame.spectralCentroid;

    // Process ~200ms more
    for (int b = 0; b < 17; ++b)
        env.processBlock(frame);

    INFO("Centroid: early=" << earlyCentroid
         << " after decay=" << frame.spectralCentroid);
    CHECK(frame.spectralCentroid < earlyCentroid);
}

// =============================================================================
// State Transition Tests
// =============================================================================

TEST_CASE("SpectralDecayEnvelope: deactivate stops decay and resets",
          "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame();
    env.activate(frame);

    // Decay a bit
    for (int b = 0; b < 5; ++b)
        env.processBlock(frame);

    env.deactivate();
    CHECK_FALSE(env.isActive());
    CHECK_FALSE(env.isFullyDecayed());

    // After deactivation, processBlock should not modify the frame
    auto freshFrame = makeTestFrame();
    auto original = freshFrame;
    env.processBlock(freshFrame);

    CHECK(freshFrame.partials[0].amplitude ==
          Approx(original.partials[0].amplitude));
}

TEST_CASE("SpectralDecayEnvelope: reactivation resets decay from new frame",
          "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame1 = makeTestFrame(440.0f, 16, 0.5f);
    env.activate(frame1);

    // Decay significantly
    for (int b = 0; b < 30; ++b)
        env.processBlock(frame1);

    float decayedAmp = frame1.partials[0].amplitude;
    INFO("Amplitude after 30 blocks: " << decayedAmp);
    CHECK(decayedAmp < 0.4f); // should have decayed noticeably

    // Reactivate with a fresh frame (simulates new freeze after recovery)
    auto frame2 = makeTestFrame(440.0f, 16, 0.8f);
    env.activate(frame2);

    CHECK(env.isActive());
    CHECK_FALSE(env.isFullyDecayed());

    // First block should decay from the NEW amplitude, not the old
    env.processBlock(frame2);
    CHECK(frame2.partials[0].amplitude > decayedAmp);
    CHECK(frame2.partials[0].amplitude < 0.8f); // but still decaying
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("SpectralDecayEnvelope: handles zero-partial frame", "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    Krate::DSP::HarmonicFrame emptyFrame{};
    env.activate(emptyFrame);

    // Should be immediately fully decayed (no partials to decay)
    env.processBlock(emptyFrame);
    CHECK(env.isFullyDecayed());
}

TEST_CASE("SpectralDecayEnvelope: handles single-partial frame",
          "[spectral-decay]")
{
    Innexus::SpectralDecayEnvelope env;
    env.prepare(kTestSR, kTestBlock);

    auto frame = makeTestFrame(440.0f, 1, 0.5f);
    env.activate(frame);

    // Should decay the single partial at the fundamental rate
    float prevAmp = frame.partials[0].amplitude;
    for (int b = 0; b < 5; ++b)
    {
        env.processBlock(frame);
        CHECK(frame.partials[0].amplitude < prevAmp);
        prevAmp = frame.partials[0].amplitude;
    }
}

TEST_CASE("SpectralDecayEnvelope: different sample rates produce similar decay times",
          "[spectral-decay]")
{
    // The decay should be time-based, not sample-based.
    // At 44100 and 96000 Hz, ~300ms should give similar amplitude.
    Innexus::SpectralDecayEnvelope env44;
    Innexus::SpectralDecayEnvelope env96;
    env44.prepare(44100.0, 512);
    env96.prepare(96000.0, 512);

    auto frame44 = makeTestFrame(440.0f, 8, 0.5f);
    auto frame96 = makeTestFrame(440.0f, 8, 0.5f);
    env44.activate(frame44);
    env96.activate(frame96);

    // Process ~100ms worth of blocks for each sample rate (short enough
    // that both still have measurable amplitude)
    int blocks44 = static_cast<int>(0.1 * 44100.0 / 512.0);
    int blocks96 = static_cast<int>(0.1 * 96000.0 / 512.0);

    for (int b = 0; b < blocks44; ++b)
        env44.processBlock(frame44);
    for (int b = 0; b < blocks96; ++b)
        env96.processBlock(frame96);

    // Fundamental amplitude should be similar (within 30% relative)
    float amp44 = frame44.partials[0].amplitude;
    float amp96 = frame96.partials[0].amplitude;
    INFO("At ~100ms: 44.1kHz amp=" << amp44 << ", 96kHz amp=" << amp96);
    CHECK(amp44 == Approx(amp96).margin(std::max(amp44, amp96) * 0.3f));
}
