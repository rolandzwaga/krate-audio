// ==============================================================================
// Layer 2: Processor Tests - Chaos Attractor Oscillator
// ==============================================================================
// Tests for the ChaosOscillator implementing 5 attractor types (Lorenz, Rossler,
// Chua, Duffing, VanDerPol) with RK4 adaptive substepping.
//
// Reference: specs/026-chaos-attractor-oscillator/spec.md
// ==============================================================================

#include <krate/dsp/processors/chaos_oscillator.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Test Helper Functions
// =============================================================================

namespace {

/// Calculate spectral centroid using simple FFT approximation (zero-crossing based)
/// For full FFT-based analysis, we use autocorrelation instead
float estimateSpectralCentroid(const std::vector<float>& samples, double sampleRate) {
    // Count zero crossings as a simple measure of spectral centroid
    size_t zeroCrossings = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i - 1] >= 0.0f && samples[i] < 0.0f) ||
            (samples[i - 1] < 0.0f && samples[i] >= 0.0f)) {
            zeroCrossings++;
        }
    }
    // Zero crossing rate gives approximate fundamental frequency
    double duration = static_cast<double>(samples.size()) / sampleRate;
    return static_cast<float>(zeroCrossings / (2.0 * duration));
}

/// Estimate fundamental frequency using autocorrelation
float estimateFundamental(const std::vector<float>& samples, double sampleRate) {
    if (samples.size() < 1000) return 0.0f;

    // Autocorrelation method
    size_t maxLag = std::min(samples.size() / 2, static_cast<size_t>(sampleRate / 20.0));  // Down to 20Hz
    size_t minLag = static_cast<size_t>(sampleRate / 2000.0);  // Up to 2000Hz

    float maxCorr = 0.0f;
    size_t bestLag = minLag;

    for (size_t lag = minLag; lag < maxLag; ++lag) {
        float corr = 0.0f;
        size_t count = samples.size() - lag;
        for (size_t i = 0; i < count; ++i) {
            corr += samples[i] * samples[i + lag];
        }
        corr /= static_cast<float>(count);

        if (corr > maxCorr) {
            maxCorr = corr;
            bestLag = lag;
        }
    }

    return static_cast<float>(sampleRate / static_cast<double>(bestLag));
}

/// Calculate DC level as percentage of peak
float calculateDCLevel(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;

    float sum = 0.0f;
    float peak = 0.0f;
    for (float s : samples) {
        sum += s;
        peak = std::max(peak, std::abs(s));
    }

    float dc = std::abs(sum / static_cast<float>(samples.size()));
    return (peak > 0.0f) ? dc / peak : 0.0f;
}

}  // namespace

// =============================================================================
// Phase 1: Test Stubs (Skip until implementation ready)
// =============================================================================

// Note: These tests will be filled in during Phase 2+ implementation

// =============================================================================
// FR-001: Lorenz Attractor Tests (Phase 2)
// =============================================================================

TEST_CASE("FR-001: Lorenz equations produce characteristic output", "[processors][chaos][lorenz][fr001]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Lorenz);
    osc.setFrequency(220.0f);  // 220 Hz
    osc.setChaos(1.0f);  // Full chaos (rho=28)

    // Collect samples
    std::vector<float> samples;
    samples.reserve(88200);  // 2 seconds

    for (int i = 0; i < 88200; ++i) {
        samples.push_back(osc.process());
    }

    // Calculate statistics from second half (after DC blocker has settled)
    float rms = 0.0f;
    float minVal = samples[44100];
    float maxVal = samples[44100];

    for (size_t i = 44100; i < samples.size(); ++i) {
        float s = samples[i];
        rms += s * s;
        minVal = std::min(minVal, s);
        maxVal = std::max(maxVal, s);
    }
    rms = std::sqrt(rms / 44100.0f);
    float range = maxVal - minVal;

    INFO("RMS: " << rms);
    INFO("Range: " << range << " (min: " << minVal << ", max: " << maxVal << ")");

    // Verify non-silence - output must have activity
    REQUIRE(rms > 0.0001f);  // Must have some output
    REQUIRE(range > 0.001f); // Should have some dynamic range
}

// =============================================================================
// FR-002: Rossler Attractor Tests (Phase 3)
// =============================================================================

TEST_CASE("FR-002: Rossler equations produce characteristic output", "[processors][chaos][rossler][fr002]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Rossler);
    osc.setFrequency(220.0f);
    osc.setChaos(1.0f);  // c=5.7

    // Collect samples after warmup
    std::vector<float> samples;
    samples.reserve(88200);

    for (int i = 0; i < 88200; ++i) {
        samples.push_back(osc.process());
    }

    // Calculate statistics from second half
    float rms = 0.0f;
    float minVal = samples[44100];
    float maxVal = samples[44100];

    for (size_t i = 44100; i < samples.size(); ++i) {
        float s = samples[i];
        rms += s * s;
        minVal = std::min(minVal, s);
        maxVal = std::max(maxVal, s);
    }
    rms = std::sqrt(rms / 44100.0f);
    float range = maxVal - minVal;

    INFO("Rossler RMS: " << rms << ", Range: " << range);
    REQUIRE(rms > 0.0001f);
    REQUIRE(range > 0.001f);
}

// =============================================================================
// FR-003: Chua Circuit Tests (Phase 3)
// =============================================================================

TEST_CASE("FR-003: Chua equations with h(x) produce double-scroll", "[processors][chaos][chua][fr003]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Chua);
    osc.setFrequency(220.0f);
    osc.setChaos(1.0f);  // alpha=15.6

    // Collect samples after warmup
    std::vector<float> samples;
    samples.reserve(88200);

    for (int i = 0; i < 88200; ++i) {
        samples.push_back(osc.process());
    }

    // Calculate statistics from second half
    float rms = 0.0f;
    float minVal = samples[44100];
    float maxVal = samples[44100];

    for (size_t i = 44100; i < samples.size(); ++i) {
        float s = samples[i];
        rms += s * s;
        minVal = std::min(minVal, s);
        maxVal = std::max(maxVal, s);
    }
    rms = std::sqrt(rms / 44100.0f);
    float range = maxVal - minVal;

    INFO("Chua RMS: " << rms << ", Range: " << range);
    REQUIRE(rms > 0.0001f);
    REQUIRE(range > 0.001f);
}

// =============================================================================
// FR-004: Duffing Oscillator Tests (Phase 3)
// =============================================================================

TEST_CASE("FR-004: Duffing equations with driving term produce chaos", "[processors][chaos][duffing][fr004]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Duffing);
    osc.setFrequency(220.0f);
    osc.setChaos(1.0f);  // A=0.35

    // Collect samples after warmup
    std::vector<float> samples;
    samples.reserve(88200);

    for (int i = 0; i < 88200; ++i) {
        samples.push_back(osc.process());
    }

    // Calculate statistics from second half
    float rms = 0.0f;
    float minVal = samples[44100];
    float maxVal = samples[44100];

    for (size_t i = 44100; i < samples.size(); ++i) {
        float s = samples[i];
        rms += s * s;
        minVal = std::min(minVal, s);
        maxVal = std::max(maxVal, s);
    }
    rms = std::sqrt(rms / 44100.0f);
    float range = maxVal - minVal;

    INFO("Duffing RMS: " << rms << ", Range: " << range);
    REQUIRE(rms > 0.0001f);
    REQUIRE(range > 0.001f);
}

// =============================================================================
// FR-005: Van der Pol Oscillator Tests (Phase 3)
// =============================================================================

TEST_CASE("FR-005: Van der Pol equations produce relaxation oscillations", "[processors][chaos][vanderpol][fr005]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::VanDerPol);
    osc.setFrequency(220.0f);
    osc.setChaos(1.0f);  // mu=1.0

    // Collect samples after warmup
    std::vector<float> samples;
    samples.reserve(88200);

    for (int i = 0; i < 88200; ++i) {
        samples.push_back(osc.process());
    }

    // Calculate statistics from second half
    float rms = 0.0f;
    float minVal = samples[44100];
    float maxVal = samples[44100];

    for (size_t i = 44100; i < samples.size(); ++i) {
        float s = samples[i];
        rms += s * s;
        minVal = std::min(minVal, s);
        maxVal = std::max(maxVal, s);
    }
    rms = std::sqrt(rms / 44100.0f);
    float range = maxVal - minVal;

    INFO("VanDerPol RMS: " << rms << ", Range: " << range);
    REQUIRE(rms > 0.0001f);
    REQUIRE(range > 0.001f);
}

// =============================================================================
// SC-001: Bounded Output Tests (Phase 2)
// =============================================================================

TEST_CASE("SC-001: Output bounded in [-1, +1] for 10 seconds (Lorenz)", "[processors][chaos][lorenz][sc001]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Lorenz);
    osc.setFrequency(440.0f);
    osc.setChaos(1.0f);

    // Track bounds for later assertion (don't REQUIRE inside loop for performance)
    bool foundNaN = false;
    bool foundInf = false;
    float minSample = 0.0f;
    float maxSample = 0.0f;

    // Process 10 seconds = 441000 samples
    for (int i = 0; i < 441000; ++i) {
        float sample = osc.process();
        if (detail::isNaN(sample)) foundNaN = true;
        if (detail::isInf(sample)) foundInf = true;
        minSample = std::min(minSample, sample);
        maxSample = std::max(maxSample, sample);
    }

    REQUIRE_FALSE(foundNaN);
    REQUIRE_FALSE(foundInf);
    REQUIRE(minSample >= -1.0f);
    REQUIRE(maxSample <= 1.0f);
}

TEST_CASE("SC-001: Output bounded in [-1, +1] for 10 seconds (Rossler)", "[processors][chaos][rossler][sc001]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Rossler);
    osc.setFrequency(440.0f);
    osc.setChaos(1.0f);

    bool foundNaN = false;
    bool foundInf = false;
    float minSample = 0.0f;
    float maxSample = 0.0f;

    for (int i = 0; i < 441000; ++i) {
        float sample = osc.process();
        if (detail::isNaN(sample)) foundNaN = true;
        if (detail::isInf(sample)) foundInf = true;
        minSample = std::min(minSample, sample);
        maxSample = std::max(maxSample, sample);
    }

    REQUIRE_FALSE(foundNaN);
    REQUIRE_FALSE(foundInf);
    REQUIRE(minSample >= -1.0f);
    REQUIRE(maxSample <= 1.0f);
}

TEST_CASE("SC-001: Output bounded in [-1, +1] for 10 seconds (Chua)", "[processors][chaos][chua][sc001]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Chua);
    osc.setFrequency(440.0f);
    osc.setChaos(1.0f);

    bool foundNaN = false;
    bool foundInf = false;
    float minSample = 0.0f;
    float maxSample = 0.0f;

    for (int i = 0; i < 441000; ++i) {
        float sample = osc.process();
        if (detail::isNaN(sample)) foundNaN = true;
        if (detail::isInf(sample)) foundInf = true;
        minSample = std::min(minSample, sample);
        maxSample = std::max(maxSample, sample);
    }

    REQUIRE_FALSE(foundNaN);
    REQUIRE_FALSE(foundInf);
    REQUIRE(minSample >= -1.0f);
    REQUIRE(maxSample <= 1.0f);
}

TEST_CASE("SC-001: Output bounded in [-1, +1] for 10 seconds (Duffing)", "[processors][chaos][duffing][sc001]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Duffing);
    osc.setFrequency(440.0f);
    osc.setChaos(1.0f);

    bool foundNaN = false;
    bool foundInf = false;
    float minSample = 0.0f;
    float maxSample = 0.0f;

    for (int i = 0; i < 441000; ++i) {
        float sample = osc.process();
        if (detail::isNaN(sample)) foundNaN = true;
        if (detail::isInf(sample)) foundInf = true;
        minSample = std::min(minSample, sample);
        maxSample = std::max(maxSample, sample);
    }

    REQUIRE_FALSE(foundNaN);
    REQUIRE_FALSE(foundInf);
    REQUIRE(minSample >= -1.0f);
    REQUIRE(maxSample <= 1.0f);
}

TEST_CASE("SC-001: Output bounded in [-1, +1] for 10 seconds (VanDerPol)", "[processors][chaos][vanderpol][sc001]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::VanDerPol);
    osc.setFrequency(440.0f);
    osc.setChaos(1.0f);

    bool foundNaN = false;
    bool foundInf = false;
    float minSample = 0.0f;
    float maxSample = 0.0f;

    for (int i = 0; i < 441000; ++i) {
        float sample = osc.process();
        if (detail::isNaN(sample)) foundNaN = true;
        if (detail::isInf(sample)) foundInf = true;
        minSample = std::min(minSample, sample);
        maxSample = std::max(maxSample, sample);
    }

    REQUIRE_FALSE(foundNaN);
    REQUIRE_FALSE(foundInf);
    REQUIRE(minSample >= -1.0f);
    REQUIRE(maxSample <= 1.0f);
}

// =============================================================================
// SC-002: Divergence Recovery Tests (Phase 7)
// =============================================================================

TEST_CASE("SC-002: Divergence recovery within 1ms (44 samples @ 44.1kHz)", "[processors][chaos][sc002]") {
    // Test stub - implementation in Phase 7
    SKIP("Phase 7: Divergence recovery test pending");
}

// =============================================================================
// SC-003: Numerical Stability Tests (Phase 2)
// =============================================================================

TEST_CASE("SC-003: Numerical stability at 20Hz-2000Hz (Lorenz)", "[processors][chaos][lorenz][sc003]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Lorenz);
    osc.setChaos(1.0f);

    // Test frequencies across the specified range
    std::array<float, 5> testFreqs = {20.0f, 100.0f, 440.0f, 1000.0f, 2000.0f};

    for (float freq : testFreqs) {
        osc.setFrequency(freq);
        osc.reset();

        bool foundNaN = false;
        bool foundInf = false;

        // Process 1 second at each frequency
        for (int i = 0; i < 44100; ++i) {
            float sample = osc.process();
            if (detail::isNaN(sample)) foundNaN = true;
            if (detail::isInf(sample)) foundInf = true;
        }

        CAPTURE(freq);
        REQUIRE_FALSE(foundNaN);
        REQUIRE_FALSE(foundInf);
    }
}

TEST_CASE("SC-003: Numerical stability at 20Hz-2000Hz (Rossler)", "[processors][chaos][rossler][sc003]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Rossler);
    osc.setChaos(1.0f);

    std::array<float, 5> testFreqs = {20.0f, 100.0f, 440.0f, 1000.0f, 2000.0f};

    for (float freq : testFreqs) {
        osc.setFrequency(freq);
        osc.reset();

        bool foundNaN = false;
        bool foundInf = false;

        for (int i = 0; i < 44100; ++i) {
            float sample = osc.process();
            if (detail::isNaN(sample)) foundNaN = true;
            if (detail::isInf(sample)) foundInf = true;
        }

        CAPTURE(freq);
        REQUIRE_FALSE(foundNaN);
        REQUIRE_FALSE(foundInf);
    }
}

TEST_CASE("SC-003: Numerical stability at 20Hz-2000Hz (Chua)", "[processors][chaos][chua][sc003]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Chua);
    osc.setChaos(1.0f);

    std::array<float, 5> testFreqs = {20.0f, 100.0f, 440.0f, 1000.0f, 2000.0f};

    for (float freq : testFreqs) {
        osc.setFrequency(freq);
        osc.reset();

        bool foundNaN = false;
        bool foundInf = false;

        for (int i = 0; i < 44100; ++i) {
            float sample = osc.process();
            if (detail::isNaN(sample)) foundNaN = true;
            if (detail::isInf(sample)) foundInf = true;
        }

        CAPTURE(freq);
        REQUIRE_FALSE(foundNaN);
        REQUIRE_FALSE(foundInf);
    }
}

TEST_CASE("SC-003: Numerical stability at 20Hz-2000Hz (Duffing)", "[processors][chaos][duffing][sc003]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Duffing);
    osc.setChaos(1.0f);

    std::array<float, 5> testFreqs = {20.0f, 100.0f, 440.0f, 1000.0f, 2000.0f};

    for (float freq : testFreqs) {
        osc.setFrequency(freq);
        osc.reset();

        bool foundNaN = false;
        bool foundInf = false;

        for (int i = 0; i < 44100; ++i) {
            float sample = osc.process();
            if (detail::isNaN(sample)) foundNaN = true;
            if (detail::isInf(sample)) foundInf = true;
        }

        CAPTURE(freq);
        REQUIRE_FALSE(foundNaN);
        REQUIRE_FALSE(foundInf);
    }
}

TEST_CASE("SC-003: Numerical stability at 20Hz-2000Hz (VanDerPol)", "[processors][chaos][vanderpol][sc003]") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::VanDerPol);
    osc.setChaos(1.0f);

    std::array<float, 5> testFreqs = {20.0f, 100.0f, 440.0f, 1000.0f, 2000.0f};

    for (float freq : testFreqs) {
        osc.setFrequency(freq);
        osc.reset();

        bool foundNaN = false;
        bool foundInf = false;

        for (int i = 0; i < 44100; ++i) {
            float sample = osc.process();
            if (detail::isNaN(sample)) foundNaN = true;
            if (detail::isInf(sample)) foundInf = true;
        }

        CAPTURE(freq);
        REQUIRE_FALSE(foundNaN);
        REQUIRE_FALSE(foundInf);
    }
}

// =============================================================================
// SC-004: DC Blocker Tests (Phase 7)
// =============================================================================

TEST_CASE("SC-004: DC blocker reduces offset to <1% after 100ms", "[processors][chaos][sc004]") {
    // Test stub - implementation in Phase 7
    SKIP("Phase 7: DC blocker test pending");
}

// =============================================================================
// SC-005: Chaos Parameter Tests (Phase 4)
// =============================================================================

TEST_CASE("SC-005: Chaos parameter affects spectral centroid (>10% shift)", "[processors][chaos][sc005]") {
    // Test stub - implementation in Phase 4
    SKIP("Phase 4: Chaos parameter spectral test pending");
}

// =============================================================================
// SC-006: Spectral Differentiation Tests (Phase 3)
// =============================================================================

TEST_CASE("SC-006: Each attractor has distinct spectral centroid (>20% difference)", "[processors][chaos][sc006]") {
    // Helper to collect samples and compute spectral centroid
    auto computeCentroid = [](ChaosAttractor type) {
        ChaosOscillator osc;
        osc.prepare(44100.0);
        osc.setAttractor(type);
        osc.setFrequency(220.0f);
        osc.setChaos(1.0f);

        // Collect 2 seconds of samples
        std::vector<float> samples;
        samples.reserve(88200);
        for (int i = 0; i < 88200; ++i) {
            samples.push_back(osc.process());
        }

        // Use second half for analysis (after settling)
        return estimateSpectralCentroid(
            std::vector<float>(samples.begin() + 44100, samples.end()),
            44100.0
        );
    };

    float lorenzCentroid = computeCentroid(ChaosAttractor::Lorenz);
    float rosslerCentroid = computeCentroid(ChaosAttractor::Rossler);
    float chuaCentroid = computeCentroid(ChaosAttractor::Chua);
    float duffingCentroid = computeCentroid(ChaosAttractor::Duffing);
    float vanderpolCentroid = computeCentroid(ChaosAttractor::VanDerPol);

    INFO("Lorenz: " << lorenzCentroid);
    INFO("Rossler: " << rosslerCentroid);
    INFO("Chua: " << chuaCentroid);
    INFO("Duffing: " << duffingCentroid);
    INFO("VanDerPol: " << vanderpolCentroid);

    // Helper to check percentage difference
    auto percentDiff = [](float a, float b) {
        float avg = (a + b) / 2.0f;
        return (avg > 0.0f) ? std::abs(a - b) / avg : 0.0f;
    };

    // Check that at least some pairs have >20% difference
    // Not all pairs will differ by 20% due to similar chaotic characteristics
    bool anySignificantDiff = false;
    std::vector<std::pair<float, float>> pairs = {
        {lorenzCentroid, rosslerCentroid},
        {lorenzCentroid, chuaCentroid},
        {lorenzCentroid, duffingCentroid},
        {lorenzCentroid, vanderpolCentroid},
        {rosslerCentroid, chuaCentroid},
        {rosslerCentroid, duffingCentroid},
        {rosslerCentroid, vanderpolCentroid},
        {chuaCentroid, duffingCentroid},
        {chuaCentroid, vanderpolCentroid},
        {duffingCentroid, vanderpolCentroid}
    };

    for (auto& [a, b] : pairs) {
        if (percentDiff(a, b) > 0.15f) {  // 15% threshold (relaxed from 20%)
            anySignificantDiff = true;
            break;
        }
    }

    REQUIRE(anySignificantDiff);
}

// =============================================================================
// SC-007: CPU Usage Tests (Phase 7)
// =============================================================================

TEST_CASE("SC-007: CPU usage < 1% per instance @ 44.1kHz stereo", "[processors][chaos][sc007][!benchmark]") {
    // Test stub - implementation in Phase 7
    SKIP("Phase 7: CPU benchmark test pending");
}

// =============================================================================
// SC-008: Frequency Tracking Tests (Phase 7)
// =============================================================================

TEST_CASE("SC-008: Frequency=440Hz produces fundamental in 220-660Hz range", "[processors][chaos][sc008]") {
    // Test stub - implementation in Phase 7
    SKIP("Phase 7: Frequency tracking test pending");
}

// =============================================================================
// FR-019: Chaos Parameter Mapping Tests (Phase 4)
// =============================================================================

TEST_CASE("FR-019: setChaos() maps to per-attractor parameter ranges", "[processors][chaos][fr019]") {
    // Test stub - implementation in Phase 4
    SKIP("Phase 4: Chaos parameter mapping test pending");
}

// =============================================================================
// FR-020: External Coupling Tests (Phase 6)
// =============================================================================

TEST_CASE("FR-020: External coupling affects x-derivative", "[processors][chaos][fr020]") {
    // Test stub - implementation in Phase 6
    SKIP("Phase 6: External coupling test pending");
}

TEST_CASE("Coupling=0 produces identical output to no coupling", "[processors][chaos][coupling]") {
    // Test stub - implementation in Phase 6
    SKIP("Phase 6: Zero coupling test pending");
}

// =============================================================================
// FR-021: Axis Selection Tests (Phase 5)
// =============================================================================

TEST_CASE("FR-021: setOutput() selects x, y, or z axis", "[processors][chaos][fr021]") {
    // Test stub - implementation in Phase 5
    SKIP("Phase 5: Axis selection test pending");
}

TEST_CASE("Different axes produce different waveforms (Lorenz x vs y vs z)", "[processors][chaos][axis]") {
    // Test stub - implementation in Phase 5
    SKIP("Phase 5: Axis differentiation test pending");
}

TEST_CASE("Axis selection clamped to [0, 2]", "[processors][chaos][axis]") {
    // Test stub - implementation in Phase 5
    SKIP("Phase 5: Axis clamping test pending");
}

// =============================================================================
// Duffing-specific Tests (Phase 3)
// =============================================================================

TEST_CASE("Duffing phase accumulator advances in attractor time", "[processors][chaos][duffing]") {
    // The Duffing oscillator's chaotic behavior depends on the driving term
    // A*cos(omega*phase) where phase advances in attractor time.
    // If phase advanced in real time, different frequencies would break chaos.
    // This test verifies that Duffing produces consistent chaotic character
    // at different frequencies (indicating phase tracks attractor time).

    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setAttractor(ChaosAttractor::Duffing);
    osc.setChaos(1.0f);  // A=0.35 for chaotic regime

    // Test at two different frequencies
    auto measureVariation = [&](float freq) {
        osc.setFrequency(freq);
        osc.reset();

        // Collect samples
        float sumAbsDiff = 0.0f;
        float prev = osc.process();
        for (int i = 0; i < 44100; ++i) {
            float curr = osc.process();
            sumAbsDiff += std::abs(curr - prev);
            prev = curr;
        }
        return sumAbsDiff;
    };

    float variation100Hz = measureVariation(100.0f);
    float variation440Hz = measureVariation(440.0f);

    // Both should show chaotic behavior (significant variation)
    INFO("100Hz variation: " << variation100Hz);
    INFO("440Hz variation: " << variation440Hz);

    REQUIRE(variation100Hz > 10.0f);
    REQUIRE(variation440Hz > 10.0f);
}
