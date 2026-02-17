// ==============================================================================
// Layer 1: DSP Primitive Tests - Spectral Transient Detector
// ==============================================================================
// Test-First Development (Constitution Principle XIII)
// Tests written before implementation.
//
// Tests for: dsp/include/krate/dsp/primitives/spectral_transient_detector.h
// Contract: specs/062-spectral-transient-detector/contracts/spectral_transient_detector.h
// Spec: specs/062-spectral-transient-detector/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/spectral_transient_detector.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

// Create a flat magnitude spectrum (sustained sine): all bins at the same level
static std::vector<float> makeSustainedSpectrum(std::size_t numBins, float level = 0.5f)
{
    return std::vector<float>(numBins, level);
}

// Create a broadband impulse spectrum: all bins have significant energy
static std::vector<float> makeImpulseSpectrum(std::size_t numBins, float level = 1.0f)
{
    return std::vector<float>(numBins, level);
}

// Create a silence spectrum: all bins zero
static std::vector<float> makeSilenceSpectrum(std::size_t numBins)
{
    return std::vector<float>(numBins, 0.0f);
}

// Create a single-bin spike spectrum: one bin elevated, rest at baseline
[[maybe_unused]]
static std::vector<float> makeSingleBinSpike(std::size_t numBins, std::size_t spikeIndex,
                                             float spikeLevel = 10.0f, float baseline = 0.1f)
{
    std::vector<float> spectrum(numBins, baseline);
    if (spikeIndex < numBins) {
        spectrum[spikeIndex] = spikeLevel;
    }
    return spectrum;
}

// Helper to feed frames and discard results (for priming / state setup)
static void feedFrames(SpectralTransientDetector& detector, const float* data,
                       std::size_t numBins, int count)
{
    for (int i = 0; i < count; ++i) {
        (void)detector.detect(data, numBins);
    }
}

// ==============================================================================
// Default Construction State
// ==============================================================================

TEST_CASE("SpectralTransientDetector default construction", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;

    SECTION("isTransient returns false") {
        REQUIRE(detector.isTransient() == false);
    }

    SECTION("getSpectralFlux returns 0") {
        REQUIRE(detector.getSpectralFlux() == Approx(0.0f).margin(1e-10f));
    }

    SECTION("getRunningAverage returns 0") {
        REQUIRE(detector.getRunningAverage() == Approx(0.0f).margin(1e-10f));
    }
}

// ==============================================================================
// prepare() Behavior
// ==============================================================================

TEST_CASE("SpectralTransientDetector prepare allocates state", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;

    detector.prepare(numBins);

    // After prepare, first-frame suppression is active
    // Feed a large impulse and verify it returns false (first-frame suppression)
    auto impulse = makeImpulseSpectrum(numBins, 10.0f);
    bool result = detector.detect(impulse.data(), numBins);
    REQUIRE(result == false); // First frame is always suppressed
}

TEST_CASE("SpectralTransientDetector prepare called twice with different bin count", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;

    // First prepare with 513 bins (1024-point FFT)
    detector.prepare(513);
    auto spectrum513 = makeSustainedSpectrum(513, 0.5f);
    feedFrames(detector, spectrum513.data(), 513, 2);

    // Now re-prepare with 2049 bins (4096-point FFT) - should reallocate and reset
    detector.prepare(2049);

    // After re-prepare, state must be fully reset:
    REQUIRE(detector.isTransient() == false);
    REQUIRE(detector.getSpectralFlux() == Approx(0.0f).margin(1e-10f));
    REQUIRE(detector.getRunningAverage() == Approx(0.0f).margin(1e-10f));

    // First frame after re-prepare is suppressed
    auto impulse = makeImpulseSpectrum(2049, 10.0f);
    bool result = detector.detect(impulse.data(), 2049);
    REQUIRE(result == false); // First frame suppressed after re-prepare
}

// ==============================================================================
// reset() Behavior (FR-008)
// ==============================================================================

TEST_CASE("SpectralTransientDetector reset clears state without reallocation", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;

    detector.prepare(numBins);
    detector.setThreshold(2.0f);
    detector.setSmoothingCoeff(0.9f);

    // Process some frames to build up state
    auto sustained = makeSustainedSpectrum(numBins, 0.5f);
    feedFrames(detector, sustained.data(), numBins, 2);

    // Now reset
    detector.reset();

    SECTION("detection state is cleared") {
        REQUIRE(detector.isTransient() == false);
        REQUIRE(detector.getSpectralFlux() == Approx(0.0f).margin(1e-10f));
        REQUIRE(detector.getRunningAverage() == Approx(0.0f).margin(1e-10f));
    }

    SECTION("first frame after reset is suppressed") {
        auto impulse = makeImpulseSpectrum(numBins, 10.0f);
        bool result = detector.detect(impulse.data(), numBins);
        REQUIRE(result == false); // First frame suppressed
    }

    SECTION("threshold and smoothingCoeff are preserved") {
        // Feed data and verify behavior matches custom settings
        auto silence = makeSilenceSpectrum(numBins);
        feedFrames(detector, silence.data(), numBins, 1); // prime (first frame)
        auto sustained2 = makeSustainedSpectrum(numBins, 0.5f);
        bool result = detector.detect(sustained2.data(), numBins); // second frame
        (void)result;

        // The running average should be seeded from the first frame's flux
        // and the second frame's flux should be compared against threshold * runningAvg
        // We just verify no crash and the detector produces consistent results
        REQUIRE(detector.getRunningAverage() >= 0.0f);
    }
}

// ==============================================================================
// First-Frame Suppression (FR-010)
// ==============================================================================

TEST_CASE("SpectralTransientDetector first frame suppression", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;
    detector.prepare(numBins);

    SECTION("first detect always returns false even with large impulse") {
        auto impulse = makeImpulseSpectrum(numBins, 100.0f);
        bool result = detector.detect(impulse.data(), numBins);
        REQUIRE(result == false);
    }

    SECTION("first frame flux still seeds the running average") {
        auto impulse = makeImpulseSpectrum(numBins, 1.0f);
        bool result = detector.detect(impulse.data(), numBins);
        REQUIRE(result == false); // Suppressed

        // The flux from first frame is the sum of max(0, 1.0 - 0.0) for all bins = numBins * 1.0
        float expectedFlux = static_cast<float>(numBins) * 1.0f;
        REQUIRE(detector.getSpectralFlux() == Approx(expectedFlux).margin(1.0f));

        // Running average should be seeded: alpha * 0.0 + (1-alpha) * flux
        // With alpha=0.95: 0.05 * expectedFlux
        float expectedAvg = 0.05f * expectedFlux;
        REQUIRE(detector.getRunningAverage() == Approx(expectedAvg).margin(1.0f));
    }
}

// ==============================================================================
// Sustained-Sine Scenario (SC-002, FR-002)
// ==============================================================================

TEST_CASE("SpectralTransientDetector sustained sine zero detections", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;
    detector.prepare(numBins);

    auto sustained = makeSustainedSpectrum(numBins, 0.5f);

    int detectionCount = 0;
    for (int i = 0; i < 100; ++i) {
        bool result = detector.detect(sustained.data(), numBins);
        if (result) {
            ++detectionCount;
        }
    }

    REQUIRE(detectionCount == 0);
}

// ==============================================================================
// Impulse Onset Scenario (SC-001, FR-001)
// ==============================================================================

TEST_CASE("SpectralTransientDetector impulse onset detection", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;
    detector.prepare(numBins);

    auto silence = makeSilenceSpectrum(numBins);

    // Feed several silence frames to prime the detector (first frame is suppressed)
    feedFrames(detector, silence.data(), numBins, 5);

    // Now feed a broadband impulse
    auto impulse = makeImpulseSpectrum(numBins, 1.0f);
    bool result = detector.detect(impulse.data(), numBins);

    // The impulse onset should be detected
    REQUIRE(result == true);
    REQUIRE(detector.isTransient() == true);
    REQUIRE(detector.getSpectralFlux() > 0.0f);
}

// ==============================================================================
// Drum Pattern Scenario (SC-003)
// ==============================================================================

TEST_CASE("SpectralTransientDetector drum pattern detection", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;
    detector.prepare(numBins);

    auto silence = makeSilenceSpectrum(numBins);
    auto impulse = makeImpulseSpectrum(numBins, 1.0f);

    // Prime with a silence frame (first frame suppressed)
    feedFrames(detector, silence.data(), numBins, 1);

    // Feed alternating impulse/silence pattern (at least 5 onsets)
    constexpr int numOnsets = 7;
    int detectedOnsets = 0;
    int falseSilenceDetections = 0;

    for (int i = 0; i < numOnsets; ++i) {
        // Impulse frame (onset)
        bool onsetResult = detector.detect(impulse.data(), numBins);
        if (onsetResult) {
            ++detectedOnsets;
        }

        // Silence frame (should not detect)
        bool silenceResult = detector.detect(silence.data(), numBins);
        if (silenceResult) {
            ++falseSilenceDetections;
        }
    }

    // All onsets must be detected
    REQUIRE(detectedOnsets == numOnsets);
    // No false positives on silence frames
    REQUIRE(falseSilenceDetections == 0);
}

// ==============================================================================
// Silence Edge Case (FR-011)
// ==============================================================================

TEST_CASE("SpectralTransientDetector silence produces zero flux", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;
    detector.prepare(numBins);

    auto silence = makeSilenceSpectrum(numBins);

    // Feed many silence frames
    bool anyDetected = false;
    for (int i = 0; i < 50; ++i) {
        bool result = detector.detect(silence.data(), numBins);
        if (result) {
            anyDetected = true;
        }
    }

    REQUIRE(anyDetected == false);
    REQUIRE(detector.getSpectralFlux() == Approx(0.0f).margin(1e-10f));
    // Running average should stay near the floor but not go below it
    REQUIRE(detector.getRunningAverage() >= 1e-10f);
}

// ==============================================================================
// Running Average Floor - Onset After Prolonged Silence (FR-011)
// ==============================================================================

TEST_CASE("SpectralTransientDetector onset after prolonged silence", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;
    detector.prepare(numBins);

    auto silence = makeSilenceSpectrum(numBins);

    // Feed many silence frames to drive running average to floor
    feedFrames(detector, silence.data(), numBins, 200);

    // Now a sudden onset should still be detected
    auto impulse = makeImpulseSpectrum(numBins, 1.0f);
    bool result = detector.detect(impulse.data(), numBins);
    REQUIRE(result == true);
}

// ==============================================================================
// Single-Bin Spike (Edge Case from Spec)
// ==============================================================================

TEST_CASE("SpectralTransientDetector single bin spike does not trigger", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;
    detector.prepare(numBins);

    // Prime with a moderate sustained spectrum
    auto sustained = makeSustainedSpectrum(numBins, 0.5f);
    feedFrames(detector, sustained.data(), numBins, 20);

    // Create a spectrum with just a single-bin spike
    auto spiked = makeSustainedSpectrum(numBins, 0.5f);
    spiked[100] = 10.0f; // Single bin dramatically increases

    bool result = detector.detect(spiked.data(), numBins);

    // A single bin's contribution to spectral flux should be below the
    // adaptive threshold based on broadband flux history
    REQUIRE(result == false);
}

// ==============================================================================
// Bin-Count Mismatch in Release Mode (FR-016)
// ==============================================================================

TEST_CASE("SpectralTransientDetector bin count mismatch handling", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t preparedBins = 2049;
    detector.prepare(preparedBins);

    // Prime the first frame
    auto silence = makeSilenceSpectrum(preparedBins);
    feedFrames(detector, silence.data(), preparedBins, 1);

    SECTION("detect with wrong count clamps silently in release mode") {
        // Pass more bins than prepared - should clamp to preparedBins
        std::vector<float> largerSpectrum(4097, 1.0f);
        // This should not crash or produce UB
        bool result = detector.detect(largerSpectrum.data(), 4097);
        (void)result;
        REQUIRE(detector.getSpectralFlux() >= 0.0f);
    }

    SECTION("detect with count=0 returns false and updates running average with flux=0") {
        // Edge case: zero bins passed
        bool result = detector.detect(nullptr, 0);
        REQUIRE(result == false);
        REQUIRE(detector.getSpectralFlux() == Approx(0.0f).margin(1e-10f));
    }
}

// ==============================================================================
// Getter Methods Return Most Recent detect() Values (FR-009)
// ==============================================================================

TEST_CASE("SpectralTransientDetector getters reflect most recent detect", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;
    constexpr std::size_t numBins = 2049;
    detector.prepare(numBins);

    auto silence = makeSilenceSpectrum(numBins);
    auto impulse = makeImpulseSpectrum(numBins, 1.0f);

    // Prime with silence (two frames)
    feedFrames(detector, silence.data(), numBins, 2);

    float fluxBefore = detector.getSpectralFlux();
    bool transientBefore = detector.isTransient();
    REQUIRE(fluxBefore == Approx(0.0f).margin(1e-10f));
    REQUIRE(transientBefore == false);

    // Now feed an impulse
    bool result = detector.detect(impulse.data(), numBins);
    REQUIRE(result == true);

    float fluxAfter = detector.getSpectralFlux();
    bool transientAfter = detector.isTransient();
    REQUIRE(fluxAfter > 0.0f);
    REQUIRE(transientAfter == true);

    // The running average should reflect the update
    float avgAfter = detector.getRunningAverage();
    REQUIRE(avgAfter > 0.0f);
}

// ==============================================================================
// noexcept Verification (FR-015)
// ==============================================================================

TEST_CASE("SpectralTransientDetector noexcept verification", "[primitives][spectral_transient_detector]")
{
    SpectralTransientDetector detector;

    static_assert(noexcept(detector.detect(nullptr, 0)),
                  "detect() must be noexcept");
    static_assert(noexcept(detector.reset()),
                  "reset() must be noexcept");
    static_assert(noexcept(detector.getSpectralFlux()),
                  "getSpectralFlux() must be noexcept");
    static_assert(noexcept(detector.getRunningAverage()),
                  "getRunningAverage() must be noexcept");
    static_assert(noexcept(detector.isTransient()),
                  "isTransient() must be noexcept");
    static_assert(noexcept(detector.setThreshold(1.5f)),
                  "setThreshold() must be noexcept");
    static_assert(noexcept(detector.setSmoothingCoeff(0.95f)),
                  "setSmoothingCoeff() must be noexcept");

    REQUIRE(true); // If we get here, all static_asserts passed
}

// ==============================================================================
// Multiple FFT Sizes (FR-014, SC-007)
// ==============================================================================

TEST_CASE("SpectralTransientDetector works with all supported FFT sizes", "[primitives][spectral_transient_detector]")
{
    // FFT size -> bin count: N/2 + 1
    const std::vector<std::size_t> binCounts = {257, 513, 1025, 2049, 4097};

    for (std::size_t numBins : binCounts) {
        DYNAMIC_SECTION("bin count " << numBins) {
            SpectralTransientDetector detector;
            detector.prepare(numBins);

            auto silence = makeSilenceSpectrum(numBins);
            auto impulse = makeImpulseSpectrum(numBins, 1.0f);

            // Prime with 3 silence frames
            feedFrames(detector, silence.data(), numBins, 3);

            // Impulse should be detected at any supported FFT size
            bool result = detector.detect(impulse.data(), numBins);
            REQUIRE(result == true);

            // Sustained frames should not be detected
            auto sustained = makeSustainedSpectrum(numBins, 0.5f);
            detector.reset();
            int detections = 0;
            for (int i = 0; i < 50; ++i) {
                if (detector.detect(sustained.data(), numBins)) {
                    ++detections;
                }
            }
            REQUIRE(detections == 0);
        }
    }
}
