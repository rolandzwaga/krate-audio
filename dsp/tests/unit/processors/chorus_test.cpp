// ==============================================================================
// Chorus DSP Processor Tests
// ==============================================================================
// Constitution Principle XIII: Test-First Development
// Tests for the Chorus class (Layer 2 processor)
// ==============================================================================

#include <krate/dsp/processors/chorus.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helpers
// =============================================================================
static void fillStereo(float* left, float* right, size_t n, float value) {
    for (size_t i = 0; i < n; ++i) {
        left[i] = value;
        right[i] = value;
    }
}

static float rmsLevel(const float* buf, size_t n) {
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

static bool hasNaNOrInf(const float* buf, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (std::isnan(buf[i]) || std::isinf(buf[i])) return true;
    }
    return false;
}

// =============================================================================
// Phase 2: Lifecycle Tests
// =============================================================================

TEST_CASE("Chorus lifecycle: prepare sets isPrepared", "[chorus][lifecycle]") {
    Chorus chorus;
    REQUIRE_FALSE(chorus.isPrepared());

    chorus.prepare(44100.0);
    REQUIRE(chorus.isPrepared());
}

TEST_CASE("Chorus lifecycle: reset clears state without crash", "[chorus][lifecycle]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(1.0f);
    chorus.setFeedback(0.9f);
    chorus.setDepth(1.0f);

    constexpr size_t N = 512;
    std::vector<float> left(N, 1.0f), right(N, 1.0f);
    chorus.processStereo(left.data(), right.data(), N);

    chorus.reset();

    // After reset, processing a silent buffer should produce near-silence
    fillStereo(left.data(), right.data(), N, 0.0f);
    chorus.processStereo(left.data(), right.data(), N);

    float rms = rmsLevel(left.data(), N);
    CHECK(rms < 0.01f);
}

TEST_CASE("Chorus lifecycle: processStereo before prepare is silent", "[chorus][lifecycle]") {
    Chorus chorus;
    constexpr size_t N = 256;
    std::vector<float> left(N, 1.0f), right(N, 1.0f);

    chorus.processStereo(left.data(), right.data(), N);

    // Should be unchanged (not prepared, returns immediately)
    for (size_t i = 0; i < N; ++i) {
        CHECK(left[i] == 1.0f);
        CHECK(right[i] == 1.0f);
    }
}

TEST_CASE("Chorus lifecycle: null buffer handling", "[chorus][lifecycle]") {
    Chorus chorus;
    chorus.prepare(44100.0);

    // Should not crash
    chorus.processStereo(nullptr, nullptr, 0);
    chorus.processStereo(nullptr, nullptr, 256);

    float dummy = 0.0f;
    chorus.processStereo(&dummy, nullptr, 1);
    chorus.processStereo(nullptr, &dummy, 1);
}

// =============================================================================
// Phase 3: Parameter Tests
// =============================================================================

TEST_CASE("Chorus parameter: rate clamped to range", "[chorus][parameter]") {
    Chorus chorus;

    chorus.setRate(0.01f);
    CHECK(chorus.getRate() == Chorus::kMinRate);

    chorus.setRate(20.0f);
    CHECK(chorus.getRate() == Chorus::kMaxRate);

    chorus.setRate(2.0f);
    CHECK(chorus.getRate() == Approx(2.0f));
}

TEST_CASE("Chorus parameter: depth clamped to range", "[chorus][parameter]") {
    Chorus chorus;

    chorus.setDepth(-0.5f);
    CHECK(chorus.getDepth() == Chorus::kMinDepth);

    chorus.setDepth(2.0f);
    CHECK(chorus.getDepth() == Chorus::kMaxDepth);

    chorus.setDepth(0.75f);
    CHECK(chorus.getDepth() == Approx(0.75f));
}

TEST_CASE("Chorus parameter: mix clamped to range", "[chorus][parameter]") {
    Chorus chorus;

    chorus.setMix(-1.0f);
    CHECK(chorus.getMix() == Chorus::kMinMix);

    chorus.setMix(5.0f);
    CHECK(chorus.getMix() == Chorus::kMaxMix);

    chorus.setMix(0.3f);
    CHECK(chorus.getMix() == Approx(0.3f));
}

TEST_CASE("Chorus parameter: feedback clamped to range", "[chorus][parameter]") {
    Chorus chorus;

    chorus.setFeedback(-2.0f);
    CHECK(chorus.getFeedback() == Chorus::kMinFeedback);

    chorus.setFeedback(2.0f);
    CHECK(chorus.getFeedback() == Chorus::kMaxFeedback);

    chorus.setFeedback(-0.5f);
    CHECK(chorus.getFeedback() == Approx(-0.5f));
}

TEST_CASE("Chorus parameter: stereo spread wraps to [0,360)", "[chorus][parameter]") {
    Chorus chorus;

    chorus.setStereoSpread(180.0f);
    CHECK(chorus.getStereoSpread() == Approx(180.0f));

    chorus.setStereoSpread(400.0f);
    CHECK(chorus.getStereoSpread() == Approx(40.0f));

    chorus.setStereoSpread(-90.0f);
    CHECK(chorus.getStereoSpread() == Approx(270.0f));

    chorus.setStereoSpread(0.0f);
    CHECK(chorus.getStereoSpread() == Approx(0.0f));
}

TEST_CASE("Chorus parameter: voices clamped to [1,4]", "[chorus][parameter]") {
    Chorus chorus;

    chorus.setVoices(0);
    CHECK(chorus.getVoices() == Chorus::kMinVoices);

    chorus.setVoices(10);
    CHECK(chorus.getVoices() == Chorus::kMaxVoices);

    chorus.setVoices(3);
    CHECK(chorus.getVoices() == 3);
}

TEST_CASE("Chorus parameter: waveform getter matches setter", "[chorus][parameter]") {
    Chorus chorus;

    chorus.setWaveform(Waveform::Sine);
    CHECK(chorus.getWaveform() == Waveform::Sine);

    chorus.setWaveform(Waveform::Triangle);
    CHECK(chorus.getWaveform() == Waveform::Triangle);
}

// =============================================================================
// Phase 4: Core Processing Tests
// =============================================================================

TEST_CASE("Chorus Mix=0.0 passthrough", "[chorus][mix]") {
    Chorus chorus;
    chorus.setMix(0.0f);
    chorus.setDepth(0.5f);
    chorus.prepare(44100.0);  // prepare after setMix so smoother snaps to 0.0

    constexpr size_t N = 1024;
    std::vector<float> left(N), right(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    auto origLeft = left;
    chorus.processStereo(left.data(), right.data(), N);

    // With mix=0, output should equal dry input
    for (size_t i = 0; i < N; ++i) {
        CHECK(left[i] == Approx(origLeft[i]).margin(1e-5f));
    }
}

TEST_CASE("Chorus Mix=1.0 wet only", "[chorus][mix]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepth(0.5f);

    constexpr size_t N = 1024;
    std::vector<float> left(N), right(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    auto origLeft = left;
    chorus.processStereo(left.data(), right.data(), N);

    // With mix=1.0, output should differ from dry (delayed signal)
    bool anyDifferent = false;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i] - origLeft[i]) > 1e-5f) {
            anyDifferent = true;
            break;
        }
    }
    CHECK(anyDifferent);
}

TEST_CASE("Chorus Mix=0.5 true crossfade", "[chorus][mix]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(0.5f);
    chorus.setDepth(0.5f);
    chorus.setFeedback(0.0f);
    chorus.setVoices(1);

    constexpr size_t N = 2048;
    std::vector<float> left(N, 0.5f), right(N, 0.5f);

    chorus.processStereo(left.data(), right.data(), N);

    // At 50% mix, output should be between dry and wet levels
    // Check that it's not all 0.5 (fully dry) and not all 0 (fully wet of silence)
    bool hasMixed = false;
    for (size_t i = 256; i < N; ++i) {  // skip initial transient
        if (std::abs(left[i] - 0.5f) > 1e-6f) {
            hasMixed = true;
            break;
        }
    }
    CHECK(hasMixed);
}

TEST_CASE("Chorus basic processing: output differs from dry", "[chorus][processing]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(0.5f);
    chorus.setDepth(0.5f);
    chorus.setRate(1.0f);

    constexpr size_t N = 2048;
    std::vector<float> left(N), right(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    auto origLeft = left;
    chorus.processStereo(left.data(), right.data(), N);

    bool differs = false;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i] - origLeft[i]) > 1e-5f) {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

TEST_CASE("Chorus depth=0 produces static delay (comb filter)", "[chorus][processing]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepth(0.0f);
    chorus.setFeedback(0.0f);
    chorus.setVoices(1);

    constexpr size_t N = 4096;
    std::vector<float> left(N, 0.0f), right(N, 0.0f);
    // Impulse
    left[0] = 1.0f;
    right[0] = 1.0f;

    chorus.processStereo(left.data(), right.data(), N);

    // With depth=0, delay is fixed at center (15ms at 44100 = ~661 samples)
    // There should be exactly one non-zero output sample (the impulse delayed)
    int nonZeroCount = 0;
    int impulsePos = -1;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i]) > 1e-6f) {
            nonZeroCount++;
            impulsePos = static_cast<int>(i);
        }
    }

    // Should see the delayed impulse
    CHECK(nonZeroCount >= 1);
    // Center delay = (5+25)/2 = 15ms = 661.5 samples
    CHECK(impulsePos >= 650);
    CHECK(impulsePos <= 680);
}

TEST_CASE("Chorus depth=1.0 produces maximum modulation", "[chorus][processing]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepth(1.0f);
    chorus.setRate(2.0f);
    chorus.setVoices(1);

    constexpr size_t N = 4096;
    std::vector<float> left(N), right(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    auto origLeft = left;
    chorus.processStereo(left.data(), right.data(), N);

    // With depth=1.0, output should deviate significantly from input
    double totalDev = 0.0;
    for (size_t i = 256; i < N; ++i) {
        double diff = static_cast<double>(left[i]) - static_cast<double>(origLeft[i]);
        totalDev += diff * diff;
    }
    CHECK(totalDev > 0.1);
}

TEST_CASE("Chorus rate affects modulation speed", "[chorus][processing]") {
    // Use a sine signal so that delay modulation creates audible pitch variation
    auto computeSpectralSpread = [](float rateHz) {
        Chorus chorus;
        chorus.prepare(44100.0);
        chorus.setMix(1.0f);
        chorus.setDepth(1.0f);
        chorus.setRate(rateHz);
        chorus.setFeedback(0.0f);
        chorus.setVoices(1);

        constexpr size_t N = 44100;  // 1 second
        std::vector<float> left(N), right(N);
        for (size_t i = 0; i < N; ++i) {
            float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
            left[i] = val;
            right[i] = val;
        }
        chorus.processStereo(left.data(), right.data(), N);

        // Measure how different the output is from the original (total deviation)
        double totalDev = 0.0;
        for (size_t i = 0; i < N; ++i) {
            float orig = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
            double diff = static_cast<double>(left[i]) - static_cast<double>(orig);
            totalDev += diff * diff;
        }
        return totalDev;
    };

    double slowDev = computeSpectralSpread(0.5f);
    double fastDev = computeSpectralSpread(5.0f);

    // Both should produce noticeable deviation from dry
    CHECK(slowDev > 0.1);
    CHECK(fastDev > 0.1);

    // They should produce different amounts of deviation (different modulation speeds)
    CHECK(std::abs(slowDev - fastDev) > 0.01);
}

// =============================================================================
// Phase 5: Multi-Voice & Stereo Tests
// =============================================================================

TEST_CASE("Chorus voices=1 vs voices=4 produce different output", "[chorus][voices]") {
    auto processWithVoices = [](int voices) {
        Chorus chorus;
        chorus.prepare(44100.0);
        chorus.setMix(1.0f);
        chorus.setDepth(0.7f);
        chorus.setRate(1.0f);
        chorus.setVoices(voices);

        constexpr size_t N = 2048;
        std::vector<float> left(N), right(N);
        for (size_t i = 0; i < N; ++i) {
            float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
            left[i] = val;
            right[i] = val;
        }
        chorus.processStereo(left.data(), right.data(), N);
        return left;
    };

    auto out1 = processWithVoices(1);
    auto out4 = processWithVoices(4);

    bool differs = false;
    for (size_t i = 0; i < out1.size(); ++i) {
        if (std::abs(out1[i] - out4[i]) > 1e-5f) {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

TEST_CASE("Chorus stereoSpread=0 produces mono-compatible output", "[chorus][stereo]") {
    Chorus chorus;
    chorus.setMix(1.0f);
    chorus.setDepth(0.5f);
    chorus.setRate(1.0f);
    chorus.setStereoSpread(0.0f);
    chorus.setVoices(1);
    chorus.prepare(44100.0);

    constexpr size_t N = 2048;
    std::vector<float> left(N), right(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }
    chorus.processStereo(left.data(), right.data(), N);

    // With spread=0 and identical input, L and R should be identical
    for (size_t i = 0; i < N; ++i) {
        CHECK(left[i] == Approx(right[i]).margin(1e-6f));
    }
}

TEST_CASE("Chorus stereoSpread=180 produces stereo difference", "[chorus][stereo]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepth(0.5f);
    chorus.setRate(1.0f);
    chorus.setStereoSpread(180.0f);
    chorus.setVoices(1);

    constexpr size_t N = 2048;
    std::vector<float> left(N), right(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }
    chorus.processStereo(left.data(), right.data(), N);

    // With spread=180, L and R should differ
    bool differs = false;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i] - right[i]) > 1e-5f) {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

// =============================================================================
// Phase 6: Feedback & Stability Tests
// =============================================================================

TEST_CASE("Chorus feedback=0 no resonance", "[chorus][feedback]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepth(0.0f);
    chorus.setFeedback(0.0f);
    chorus.setVoices(1);

    constexpr size_t N = 4096;
    std::vector<float> left(N, 0.0f), right(N, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    chorus.processStereo(left.data(), right.data(), N);

    // With feedback=0 and depth=0, the impulse should appear once
    int nonZeroCount = 0;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i]) > 1e-6f) nonZeroCount++;
    }
    // Should have very few non-zero samples (delayed impulse spread by cubic interp ~4 taps)
    CHECK(nonZeroCount <= 8);
}

TEST_CASE("Chorus feedback positive adds resonance", "[chorus][feedback]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepth(0.0f);
    chorus.setFeedback(0.8f);
    chorus.setVoices(1);

    constexpr size_t N = 8192;
    std::vector<float> left(N, 0.0f), right(N, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    chorus.processStereo(left.data(), right.data(), N);

    // With feedback=0.8 and depth=0, should see repeating echoes
    int nonZeroCount = 0;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i]) > 1e-4f) nonZeroCount++;
    }
    // Should have multiple echoes (more non-zero samples than without feedback)
    CHECK(nonZeroCount > 10);
}

TEST_CASE("Chorus feedback negative inverts comb", "[chorus][feedback]") {
    auto processWithFeedback = [](float fb) {
        Chorus chorus;
        chorus.prepare(44100.0);
        chorus.setMix(1.0f);
        chorus.setDepth(0.0f);
        chorus.setFeedback(fb);
        chorus.setVoices(1);

        constexpr size_t N = 4096;
        std::vector<float> left(N, 0.0f), right(N, 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        chorus.processStereo(left.data(), right.data(), N);
        return left;
    };

    auto outPos = processWithFeedback(0.5f);
    auto outNeg = processWithFeedback(-0.5f);

    // Positive and negative feedback should produce different results
    bool differs = false;
    for (size_t i = 0; i < outPos.size(); ++i) {
        if (std::abs(outPos[i] - outNeg[i]) > 1e-5f) {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

TEST_CASE("Chorus stability: 10s constant input no NaN/Inf", "[chorus][stability]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(0.5f);
    chorus.setDepth(1.0f);
    chorus.setRate(2.0f);
    chorus.setFeedback(0.9f);
    chorus.setVoices(4);

    // Process 10 seconds in blocks
    constexpr size_t blockSize = 512;
    constexpr size_t totalSamples = 44100 * 10;
    std::vector<float> left(blockSize, 0.5f), right(blockSize, 0.5f);

    for (size_t processed = 0; processed < totalSamples; processed += blockSize) {
        fillStereo(left.data(), right.data(), blockSize, 0.5f);
        chorus.processStereo(left.data(), right.data(), blockSize);

        REQUIRE_FALSE(hasNaNOrInf(left.data(), blockSize));
        REQUIRE_FALSE(hasNaNOrInf(right.data(), blockSize));
    }
}

TEST_CASE("Chorus stability: extreme feedback bounded output", "[chorus][stability]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepth(1.0f);
    chorus.setRate(5.0f);
    chorus.setFeedback(1.0f);  // Max feedback
    chorus.setVoices(4);

    constexpr size_t N = 44100;  // 1 second
    std::vector<float> left(N, 1.0f), right(N, 1.0f);

    chorus.processStereo(left.data(), right.data(), N);

    // Output should remain bounded (tanh clips feedback)
    float maxAbs = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        maxAbs = std::max(maxAbs, std::abs(left[i]));
        maxAbs = std::max(maxAbs, std::abs(right[i]));
    }
    CHECK(maxAbs < 10.0f);
    CHECK_FALSE(hasNaNOrInf(left.data(), N));
    CHECK_FALSE(hasNaNOrInf(right.data(), N));
}

TEST_CASE("Chorus NaN input handling", "[chorus][stability]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(0.5f);
    chorus.setDepth(0.5f);

    constexpr size_t N = 512;
    std::vector<float> left(N, 0.0f), right(N, 0.0f);

    // Inject NaN at various positions
    left[10] = std::numeric_limits<float>::quiet_NaN();
    right[20] = std::numeric_limits<float>::infinity();
    left[100] = -std::numeric_limits<float>::infinity();

    chorus.processStereo(left.data(), right.data(), N);

    // Output should have no NaN/Inf
    CHECK_FALSE(hasNaNOrInf(left.data(), N));
    CHECK_FALSE(hasNaNOrInf(right.data(), N));
}

// =============================================================================
// Phase 7: Tempo Sync & Waveform Tests
// =============================================================================

TEST_CASE("Chorus tempo sync: quarter note at 120 BPM", "[chorus][tempo]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepth(1.0f);
    chorus.setVoices(1);

    // Enable tempo sync
    chorus.setTempoSync(true);
    chorus.setTempo(120.0);
    chorus.setNoteValue(NoteValue::Quarter);

    // Use a sine signal so modulated delay creates audible variation
    constexpr size_t N = 44100;  // 1 second
    std::vector<float> left(N), right(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    auto origLeft = left;
    chorus.processStereo(left.data(), right.data(), N);

    // Output should differ from input (tempo-synced modulation is active)
    double totalDev = 0.0;
    for (size_t i = 1024; i < N; ++i) {
        double diff = static_cast<double>(left[i]) - static_cast<double>(origLeft[i]);
        totalDev += diff * diff;
    }
    CHECK(totalDev > 0.1);
}

TEST_CASE("Chorus waveform: sine vs triangle produce different output", "[chorus][waveform]") {
    auto processWithWaveform = [](Waveform wf) {
        Chorus chorus;
        chorus.prepare(44100.0);
        chorus.setMix(1.0f);
        chorus.setDepth(0.5f);
        chorus.setRate(2.0f);
        chorus.setVoices(1);
        chorus.setWaveform(wf);

        constexpr size_t N = 2048;
        std::vector<float> left(N, 0.5f), right(N, 0.5f);
        chorus.processStereo(left.data(), right.data(), N);
        return left;
    };

    auto outSine = processWithWaveform(Waveform::Sine);
    auto outTri = processWithWaveform(Waveform::Triangle);

    bool differs = false;
    for (size_t i = 0; i < outSine.size(); ++i) {
        if (std::abs(outSine[i] - outTri[i]) > 1e-5f) {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

// =============================================================================
// Phase 8: Performance Test
// =============================================================================

TEST_CASE("Chorus performance: <1.0% CPU at 44.1kHz stereo, 4 voices", "[chorus][.perf]") {
    Chorus chorus;
    chorus.prepare(44100.0);
    chorus.setMix(0.5f);
    chorus.setDepth(0.7f);
    chorus.setRate(1.0f);
    chorus.setFeedback(0.3f);
    chorus.setVoices(4);
    chorus.setStereoSpread(180.0f);

    constexpr size_t blockSize = 512;
    constexpr size_t totalBlocks = 1000;
    std::vector<float> left(blockSize), right(blockSize);

    // Fill with test signal
    for (size_t i = 0; i < blockSize; ++i) {
        left[i] = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        right[i] = left[i];
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t b = 0; b < totalBlocks; ++b) {
        chorus.processStereo(left.data(), right.data(), blockSize);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    // Total audio time = totalBlocks * blockSize / sampleRate * 1000 ms
    double audioTimeMs = static_cast<double>(totalBlocks * blockSize) / 44100.0 * 1000.0;
    double cpuPercent = (elapsedMs / audioTimeMs) * 100.0;

    INFO("CPU usage: " << cpuPercent << "% (" << elapsedMs << "ms for " << audioTimeMs << "ms of audio)");
    CHECK(cpuPercent < 1.0);
}
