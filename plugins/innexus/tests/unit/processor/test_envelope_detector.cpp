// ==============================================================================
// EnvelopeDetector Unit Tests (Spec 124: T012)
// ==============================================================================
// Tests for the ADSR envelope detection algorithm that extracts Attack, Decay,
// Sustain, and Release parameters from an amplitude contour.
//
// Test-first: these tests MUST fail initially (stub returns defaults).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/envelope_detector.h"

#include <krate/dsp/processors/harmonic_types.h>

#include <cmath>
#include <vector>

using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

/// Build a vector of HarmonicFrames with a given amplitude contour.
/// Each entry in `amplitudes` becomes the globalAmplitude of a frame.
static std::vector<Krate::DSP::HarmonicFrame> makeContour(
    const std::vector<float>& amplitudes,
    float f0 = 440.0f)
{
    std::vector<Krate::DSP::HarmonicFrame> frames;
    frames.reserve(amplitudes.size());
    for (float amp : amplitudes)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = f0;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 1;
        frame.globalAmplitude = amp;
        frame.partials[0].harmonicIndex = 1;
        frame.partials[0].frequency = f0;
        frame.partials[0].amplitude = amp;
        frame.partials[0].relativeFrequency = 1.0f;
        frame.partials[0].stability = 1.0f;
        frame.partials[0].age = 10;
        frames.push_back(frame);
    }
    return frames;
}

/// Hop time for a 44.1kHz sample rate with 512-sample hop.
static constexpr float kHopTimeSec = 512.0f / 44100.0f; // ~11.6ms

// =============================================================================
// Test: Synthetic percussive contour (step-up then decay)
// =============================================================================
TEST_CASE("EnvelopeDetector: percussive contour yields short Attack and low Sustain",
          "[adsr][envelope-detector]")
{
    // Fast attack (2 frames), decay to ~0.3 sustain, then steady for many frames
    // Frame 0: 0.1 (onset)
    // Frame 1: 0.8 (rising)
    // Frame 2: 1.0 (peak)
    // Frames 3-7: decay from 1.0 to ~0.3
    // Frames 8-30: steady at 0.3 (sustain)
    // Frames 31-35: release to 0.05
    std::vector<float> amps;
    amps.push_back(0.1f);
    amps.push_back(0.8f);
    amps.push_back(1.0f);

    // Decay region: exponential decay from 1.0 toward 0.3
    for (int i = 0; i < 5; ++i)
    {
        float t = static_cast<float>(i + 1) / 6.0f;
        amps.push_back(1.0f - t * 0.7f); // 0.88, 0.77, 0.65, 0.53, 0.42
    }

    // Sustain region: steady at ~0.3
    for (int i = 0; i < 23; ++i)
        amps.push_back(0.3f);

    // Release region: fade out
    for (int i = 0; i < 5; ++i)
    {
        float t = static_cast<float>(i + 1) / 5.0f;
        amps.push_back(0.3f * (1.0f - t)); // 0.24, 0.18, 0.12, 0.06, 0.0
    }

    auto frames = makeContour(amps);

    auto result = Innexus::EnvelopeDetector::detect(frames, kHopTimeSec);

    // Attack should be < 50ms (peak is at frame 2 = ~23ms)
    REQUIRE(result.attackMs < 50.0f);
    REQUIRE(result.attackMs >= 1.0f);

    // Sustain should be < 0.5 (steady region is at 0.3)
    REQUIRE(result.sustainLevel < 0.5f);
    REQUIRE(result.sustainLevel >= 0.0f);
}

// =============================================================================
// Test: Synthetic pad/drone contour (slow rise, flat sustain)
// =============================================================================
TEST_CASE("EnvelopeDetector: pad contour yields long Attack and high Sustain",
          "[adsr][envelope-detector]")
{
    // Slow attack: 10 frames rising linearly from 0 to 0.9
    // Long sustain: 40 frames at 0.9
    // Short release: 5 frames decay
    std::vector<float> amps;

    // Slow attack (10 frames = ~116ms)
    for (int i = 0; i <= 10; ++i)
        amps.push_back(static_cast<float>(i) / 10.0f * 0.9f);

    // Peak at frame 10 = 0.9
    // Sustain region: flat at 0.9
    for (int i = 0; i < 40; ++i)
        amps.push_back(0.9f);

    // Release
    for (int i = 0; i < 5; ++i)
    {
        float t = static_cast<float>(i + 1) / 5.0f;
        amps.push_back(0.9f * (1.0f - t));
    }

    auto frames = makeContour(amps);
    auto result = Innexus::EnvelopeDetector::detect(frames, kHopTimeSec);

    // Attack > 50ms (peak is at frame 10 = ~116ms)
    REQUIRE(result.attackMs > 50.0f);

    // Sustain > 0.7 (sustain region is at ~0.9/0.9 = 1.0)
    REQUIRE(result.sustainLevel > 0.7f);
}

// =============================================================================
// Test: Constant-amplitude contour yields defaults
// =============================================================================
TEST_CASE("EnvelopeDetector: constant amplitude yields near-default values",
          "[adsr][envelope-detector]")
{
    // 30 frames all at 0.5 amplitude
    std::vector<float> amps(30, 0.5f);
    auto frames = makeContour(amps);
    auto result = Innexus::EnvelopeDetector::detect(frames, kHopTimeSec);

    // Attack should be very short (peak is at frame 0 or nearby)
    REQUIRE(result.attackMs <= 15.0f);

    // Sustain should be very high (everything is steady)
    REQUIRE(result.sustainLevel > 0.9f);
}

// =============================================================================
// Test: Very short contour (< 50ms) produces valid clamped values
// =============================================================================
TEST_CASE("EnvelopeDetector: very short contour yields clamped values",
          "[adsr][envelope-detector]")
{
    // Only 3 frames: up, peak, down
    std::vector<float> amps = {0.2f, 1.0f, 0.3f};
    auto frames = makeContour(amps);
    auto result = Innexus::EnvelopeDetector::detect(frames, kHopTimeSec);

    // All times must be >= 1ms
    REQUIRE(result.attackMs >= 1.0f);
    REQUIRE(result.decayMs >= 1.0f);
    REQUIRE(result.releaseMs >= 1.0f);

    // Sustain in valid range
    REQUIRE(result.sustainLevel >= 0.0f);
    REQUIRE(result.sustainLevel <= 1.0f);

    // All times must be <= 5000ms
    REQUIRE(result.attackMs <= 5000.0f);
    REQUIRE(result.decayMs <= 5000.0f);
    REQUIRE(result.releaseMs <= 5000.0f);
}

// =============================================================================
// Test: Empty frame list produces sensible defaults without crash
// =============================================================================
TEST_CASE("EnvelopeDetector: empty frames returns defaults without crash",
          "[adsr][envelope-detector]")
{
    std::vector<Krate::DSP::HarmonicFrame> empty;
    auto result = Innexus::EnvelopeDetector::detect(empty, kHopTimeSec);

    // Should return the struct defaults
    REQUIRE(result.attackMs == Approx(10.0f));
    REQUIRE(result.decayMs == Approx(100.0f));
    REQUIRE(result.sustainLevel == Approx(1.0f));
    REQUIRE(result.releaseMs == Approx(100.0f));
}

// =============================================================================
// Test: Rolling least-squares steady-state detection identifies correct start frame
// =============================================================================
TEST_CASE("EnvelopeDetector: rolling least-squares detects steady-state region",
          "[adsr][envelope-detector]")
{
    // Create a contour with a clear peak, decay, then steady state.
    // The steady-state region starts around frame 15 (|slope| < 0.0005, var < 0.002).
    std::vector<float> amps;

    // Quick attack (2 frames)
    amps.push_back(0.3f);
    amps.push_back(1.0f); // peak at frame 1

    // Decay from 1.0 to 0.4 over 13 frames
    for (int i = 0; i < 13; ++i)
    {
        float t = static_cast<float>(i + 1) / 14.0f;
        amps.push_back(1.0f - t * 0.6f);
    }

    // Steady-state at 0.4 for 30 frames (well above window size)
    for (int i = 0; i < 30; ++i)
        amps.push_back(0.4f);

    // Release (5 frames)
    for (int i = 0; i < 5; ++i)
    {
        float t = static_cast<float>(i + 1) / 5.0f;
        amps.push_back(0.4f * (1.0f - t));
    }

    auto frames = makeContour(amps);
    auto result = Innexus::EnvelopeDetector::detect(frames, kHopTimeSec);

    // Decay should be approximately 13 frames * hop time
    float expectedDecayMs = 13.0f * kHopTimeSec * 1000.0f;
    // Allow generous tolerance since the window needs to fill up before detecting
    REQUIRE(result.decayMs > expectedDecayMs * 0.3f);
    REQUIRE(result.decayMs < expectedDecayMs * 3.0f);

    // Sustain should be close to 0.4 (0.4 / 1.0 peak)
    REQUIRE(result.sustainLevel == Approx(0.4f).margin(0.15f));

    // Release should be approximately 5 frames * hop time
    float expectedReleaseMs = 5.0f * kHopTimeSec * 1000.0f;
    REQUIRE(result.releaseMs > expectedReleaseMs * 0.3f);
    REQUIRE(result.releaseMs < expectedReleaseMs * 3.0f);
}

// =============================================================================
// Test: Sidechain mode flag suppresses detection (FR-022)
// NOTE: FR-022 is tested at the SampleAnalyzer level (it's the caller that
// decides whether to call detect). This test verifies EnvelopeDetector itself
// works with valid input -- the sidechain suppression test is in the
// integration test file where the full pipeline is tested.
// =============================================================================
TEST_CASE("EnvelopeDetector: valid contour produces non-default ADSR",
          "[adsr][envelope-detector]")
{
    // A percussive contour should produce values different from defaults
    std::vector<float> amps;
    amps.push_back(0.1f);
    amps.push_back(1.0f); // peak at frame 1

    // Decay
    for (int i = 0; i < 8; ++i)
    {
        float t = static_cast<float>(i + 1) / 9.0f;
        amps.push_back(1.0f - t * 0.7f);
    }

    // Sustain
    for (int i = 0; i < 25; ++i)
        amps.push_back(0.3f);

    // Release
    for (int i = 0; i < 5; ++i)
        amps.push_back(0.3f * (1.0f - static_cast<float>(i + 1) / 5.0f));

    auto frames = makeContour(amps);
    auto result = Innexus::EnvelopeDetector::detect(frames, kHopTimeSec);

    // With a real implementation, at least one value should differ from default
    bool isDefault = (result.attackMs == Approx(10.0f).margin(0.1f) &&
                      result.decayMs == Approx(100.0f).margin(0.1f) &&
                      result.sustainLevel == Approx(1.0f).margin(0.01f) &&
                      result.releaseMs == Approx(100.0f).margin(0.1f));

    // This test MUST FAIL with the stub (which returns all defaults)
    REQUIRE_FALSE(isDefault);
}
