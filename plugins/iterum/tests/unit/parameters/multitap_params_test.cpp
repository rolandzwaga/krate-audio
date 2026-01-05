// ==============================================================================
// MultiTap Delay Parameters Unit Tests
// ==============================================================================
// Tests normalization accuracy and formula correctness for MultiTap delay parameters.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// ==============================================================================
// Normalization Formulas (extracted from multitap_params.h)
// ==============================================================================

// Timing Pattern: 0-19 discrete
int denormTimingPattern(double normalized) {
    return static_cast<int>(normalized * 19.0 + 0.5);
}

double normTimingPattern(int pattern) {
    return static_cast<double>(pattern) / 19.0;
}

// Spatial Pattern: 0-6 discrete
int denormSpatialPattern(double normalized) {
    return static_cast<int>(normalized * 6.0 + 0.5);
}

double normSpatialPattern(int pattern) {
    return static_cast<double>(pattern) / 6.0;
}

// Tap Count: 2-16 (offset range)
int denormTapCount(double normalized) {
    return static_cast<int>(2.0 + normalized * 14.0 + 0.5);
}

double normTapCount(int count) {
    return static_cast<double>(count - 2) / 14.0;
}

// Base Time: 1-5000ms
float denormBaseTime(double normalized) {
    return static_cast<float>(1.0 + normalized * 4999.0);
}

double normBaseTime(float ms) {
    return static_cast<double>((ms - 1.0f) / 4999.0f);
}

// Tempo: 20-300 BPM
float denormTempo(double normalized) {
    return static_cast<float>(20.0 + normalized * 280.0);
}

double normTempo(float bpm) {
    return static_cast<double>((bpm - 20.0f) / 280.0f);
}

// Feedback: 0-1.1 (110%)
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.1);
}

double normFeedback(float feedback) {
    return static_cast<double>(feedback / 1.1f);
}

// Feedback LP/HP Cutoff: 20-20000Hz (logarithmic)
float denormFreqCutoff(double normalized) {
    return static_cast<float>(20.0 * std::pow(1000.0, normalized));
}

double normFreqCutoff(float hz) {
    return std::log(hz / 20.0f) / std::log(1000.0f);
}

// Morph Time: 50-2000ms
float denormMorphTime(double normalized) {
    return static_cast<float>(50.0 + normalized * 1950.0);
}

double normMorphTime(float ms) {
    return static_cast<double>((ms - 50.0f) / 1950.0f);
}

// Dry/Wet: 0-100%
float denormDryWet(double normalized) {
    return static_cast<float>(normalized * 100.0);
}

double normDryWet(float percent) {
    return static_cast<double>(percent / 100.0f);
}

} // anonymous namespace

// ==============================================================================
// Discrete Pattern Tests
// ==============================================================================

TEST_CASE("MultiTap Timing Pattern normalization", "[params][multitap]") {
    SECTION("round-trip all timing patterns (0-19)") {
        for (int pattern = 0; pattern <= 19; ++pattern) {
            double normalized = normTimingPattern(pattern);
            int result = denormTimingPattern(normalized);
            REQUIRE(result == pattern);
        }
    }

    SECTION("boundary values") {
        REQUIRE(denormTimingPattern(0.0) == 0);   // Whole
        REQUIRE(denormTimingPattern(1.0) == 19);  // Custom
    }
}

TEST_CASE("MultiTap Spatial Pattern normalization", "[params][multitap]") {
    SECTION("round-trip all spatial patterns (0-6)") {
        for (int pattern = 0; pattern <= 6; ++pattern) {
            double normalized = normSpatialPattern(pattern);
            int result = denormSpatialPattern(normalized);
            REQUIRE(result == pattern);
        }
    }
}

TEST_CASE("MultiTap Tap Count normalization", "[params][multitap]") {
    SECTION("normalized 0.0 -> 2 taps (minimum)") {
        REQUIRE(denormTapCount(0.0) == 2);
    }

    SECTION("normalized 1.0 -> 16 taps (maximum)") {
        REQUIRE(denormTapCount(1.0) == 16);
    }

    SECTION("round-trip all tap counts (2-16)") {
        for (int count = 2; count <= 16; ++count) {
            double normalized = normTapCount(count);
            int result = denormTapCount(normalized);
            REQUIRE(result == count);
        }
    }

    SECTION("default 4 taps") {
        // normalized = (4-2)/14 = 0.143
        REQUIRE(denormTapCount(0.143) == 4);
    }
}

// ==============================================================================
// Time/Tempo Tests
// ==============================================================================

TEST_CASE("MultiTap Base Time normalization", "[params][multitap]") {
    SECTION("normalized 0.0 -> 1ms (minimum)") {
        REQUIRE(denormBaseTime(0.0) == Approx(1.0f));
    }

    SECTION("normalized 1.0 -> 5000ms (maximum)") {
        REQUIRE(denormBaseTime(1.0) == Approx(5000.0f));
    }

    SECTION("round-trip: 500ms (default)") {
        float original = 500.0f;
        double normalized = normBaseTime(original);
        float result = denormBaseTime(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("MultiTap Tempo normalization", "[params][multitap]") {
    SECTION("normalized 0.0 -> 20 BPM (minimum)") {
        REQUIRE(denormTempo(0.0) == Approx(20.0f));
    }

    SECTION("normalized 1.0 -> 300 BPM (maximum)") {
        REQUIRE(denormTempo(1.0) == Approx(300.0f));
    }

    SECTION("round-trip: 120 BPM (default)") {
        float original = 120.0f;
        double normalized = normTempo(original);
        float result = denormTempo(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

// ==============================================================================
// Logarithmic Frequency Tests
// ==============================================================================

TEST_CASE("MultiTap Frequency Cutoff normalization (logarithmic)", "[params][multitap]") {
    SECTION("normalized 0.0 -> 20Hz (minimum)") {
        REQUIRE(denormFreqCutoff(0.0) == Approx(20.0f));
    }

    SECTION("normalized 0.5 -> ~632Hz (geometric midpoint)") {
        // Geometric mean of 20 and 20000 = sqrt(20*20000) = 632.5
        REQUIRE(denormFreqCutoff(0.5) == Approx(632.5f).margin(1.0f));
    }

    SECTION("normalized 1.0 -> 20000Hz (maximum)") {
        REQUIRE(denormFreqCutoff(1.0) == Approx(20000.0f));
    }

    SECTION("round-trip: 1000Hz") {
        float original = 1000.0f;
        double normalized = normFreqCutoff(original);
        float result = denormFreqCutoff(normalized);
        REQUIRE(result == Approx(original).margin(1.0f));
    }

    SECTION("round-trip: 20000Hz (LP default)") {
        float original = 20000.0f;
        double normalized = normFreqCutoff(original);
        float result = denormFreqCutoff(normalized);
        REQUIRE(result == Approx(original).margin(1.0f));
    }
}

// ==============================================================================
// Feedback Tests
// ==============================================================================

TEST_CASE("MultiTap Feedback normalization", "[params][multitap]") {
    SECTION("normalized 0.0 -> 0.0") {
        REQUIRE(denormFeedback(0.0) == Approx(0.0f));
    }

    SECTION("normalized 1.0 -> 1.1 (110%)") {
        REQUIRE(denormFeedback(1.0) == Approx(1.1f));
    }

    SECTION("round-trip: 0.5 (50% default)") {
        float original = 0.5f;
        double normalized = normFeedback(original);
        float result = denormFeedback(normalized);
        REQUIRE(result == Approx(original).margin(0.001f));
    }
}

// ==============================================================================
// Morph Time Tests
// ==============================================================================

TEST_CASE("MultiTap Morph Time normalization", "[params][multitap]") {
    SECTION("normalized 0.0 -> 50ms (minimum)") {
        REQUIRE(denormMorphTime(0.0) == Approx(50.0f));
    }

    SECTION("normalized 1.0 -> 2000ms (maximum)") {
        REQUIRE(denormMorphTime(1.0) == Approx(2000.0f));
    }

    SECTION("round-trip: 500ms (default)") {
        float original = 500.0f;
        double normalized = normMorphTime(original);
        float result = denormMorphTime(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

// ==============================================================================
// Dry/Wet Tests
// ==============================================================================

TEST_CASE("MultiTap Dry/Wet normalization", "[params][multitap]") {
    SECTION("normalized 0.0 -> 0%") {
        REQUIRE(denormDryWet(0.0) == Approx(0.0f));
    }

    SECTION("normalized 0.5 -> 50% (default)") {
        REQUIRE(denormDryWet(0.5) == Approx(50.0f));
    }

    SECTION("normalized 1.0 -> 100%") {
        REQUIRE(denormDryWet(1.0) == Approx(100.0f));
    }

    SECTION("round-trip: 50%") {
        float original = 50.0f;
        double normalized = normDryWet(original);
        float result = denormDryWet(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

// ==============================================================================
// Custom Pattern Persistence Tests (Phase 4 - User Story 2)
// ==============================================================================

#include "parameters/multitap_params.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include <map>

TEST_CASE("MultiTap Custom Pattern save/load round-trip", "[params][multitap][persistence]") {
    using namespace Iterum;
    using namespace Steinberg;

    SECTION("custom time ratios are saved and loaded correctly") {
        MultiTapParams original;
        for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
            float customTime = static_cast<float>(i) / 20.0f + 0.1f;
            original.customTimeRatios[i].store(customTime, std::memory_order_relaxed);
        }

        auto memStream = Steinberg::owned(new MemoryStream());
        IBStreamer saveStreamer(memStream, kLittleEndian);
        saveMultiTapParams(original, saveStreamer);

        memStream->seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer loadStreamer(memStream, kLittleEndian);
        MultiTapParams loaded;
        loadMultiTapParams(loaded, loadStreamer);

        for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
            float expected = static_cast<float>(i) / 20.0f + 0.1f;
            REQUIRE(loaded.customTimeRatios[i].load(std::memory_order_relaxed)
                    == Approx(expected).margin(0.0001f));
        }
    }

    SECTION("custom levels are saved and loaded correctly") {
        MultiTapParams original;
        for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
            float customLevel = 1.0f - (static_cast<float>(i) / 20.0f);
            original.customLevels[i].store(customLevel, std::memory_order_relaxed);
        }

        auto memStream = Steinberg::owned(new MemoryStream());
        IBStreamer saveStreamer(memStream, kLittleEndian);
        saveMultiTapParams(original, saveStreamer);

        memStream->seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer loadStreamer(memStream, kLittleEndian);
        MultiTapParams loaded;
        loadMultiTapParams(loaded, loadStreamer);

        for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
            float expected = 1.0f - (static_cast<float>(i) / 20.0f);
            REQUIRE(loaded.customLevels[i].load(std::memory_order_relaxed)
                    == Approx(expected).margin(0.0001f));
        }
    }

    SECTION("full round-trip with all parameters including custom pattern") {
        MultiTapParams original;
        original.noteValue.store(5, std::memory_order_relaxed);
        original.noteModifier.store(1, std::memory_order_relaxed);
        original.timingPattern.store(19, std::memory_order_relaxed);
        original.spatialPattern.store(4, std::memory_order_relaxed);
        original.tapCount.store(8, std::memory_order_relaxed);
        original.feedback.store(0.75f, std::memory_order_relaxed);
        original.feedbackLPCutoff.store(5000.0f, std::memory_order_relaxed);
        original.feedbackHPCutoff.store(200.0f, std::memory_order_relaxed);
        original.morphTime.store(1000.0f, std::memory_order_relaxed);
        original.dryWet.store(0.7f, std::memory_order_relaxed);

        const float fibTimes[] = {0.05f, 0.08f, 0.13f, 0.21f, 0.34f, 0.55f, 0.75f, 0.95f,
                                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        const float fibLevels[] = {1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f,
                                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
            original.customTimeRatios[i].store(fibTimes[i], std::memory_order_relaxed);
            original.customLevels[i].store(fibLevels[i], std::memory_order_relaxed);
        }

        auto memStream = Steinberg::owned(new MemoryStream());
        IBStreamer saveStreamer(memStream, kLittleEndian);
        saveMultiTapParams(original, saveStreamer);

        memStream->seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer loadStreamer(memStream, kLittleEndian);
        MultiTapParams loaded;
        loadMultiTapParams(loaded, loadStreamer);

        REQUIRE(loaded.noteValue.load(std::memory_order_relaxed) == 5);
        REQUIRE(loaded.noteModifier.load(std::memory_order_relaxed) == 1);
        REQUIRE(loaded.timingPattern.load(std::memory_order_relaxed) == 19);
        REQUIRE(loaded.spatialPattern.load(std::memory_order_relaxed) == 4);
        REQUIRE(loaded.tapCount.load(std::memory_order_relaxed) == 8);
        REQUIRE(loaded.feedback.load(std::memory_order_relaxed) == Approx(0.75f).margin(0.0001f));
        REQUIRE(loaded.feedbackLPCutoff.load(std::memory_order_relaxed) == Approx(5000.0f).margin(0.1f));
        REQUIRE(loaded.feedbackHPCutoff.load(std::memory_order_relaxed) == Approx(200.0f).margin(0.1f));
        REQUIRE(loaded.morphTime.load(std::memory_order_relaxed) == Approx(1000.0f).margin(0.1f));
        REQUIRE(loaded.dryWet.load(std::memory_order_relaxed) == Approx(0.7f).margin(0.0001f));

        for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
            REQUIRE(loaded.customTimeRatios[i].load(std::memory_order_relaxed)
                    == Approx(fibTimes[i]).margin(0.0001f));
            REQUIRE(loaded.customLevels[i].load(std::memory_order_relaxed)
                    == Approx(fibLevels[i]).margin(0.0001f));
        }
    }
}

TEST_CASE("MultiTap Custom Pattern controller sync", "[params][multitap][persistence]") {
    using namespace Iterum;
    using namespace Steinberg;

    SECTION("loadMultiTapParamsToController syncs custom pattern values") {
        MultiTapParams params;
        params.timingPattern.store(19, std::memory_order_relaxed);
        params.tapCount.store(6, std::memory_order_relaxed);

        for (size_t i = 0; i < 6; ++i) {
            params.customTimeRatios[i].store(static_cast<float>(i + 1) / 10.0f, std::memory_order_relaxed);
            params.customLevels[i].store(1.0f - static_cast<float>(i) / 10.0f, std::memory_order_relaxed);
        }

        auto memStream = Steinberg::owned(new MemoryStream());
        IBStreamer saveStreamer(memStream, kLittleEndian);
        saveMultiTapParams(params, saveStreamer);

        std::map<Vst::ParamID, double> capturedParams;
        auto mockSetParam = [&capturedParams](Vst::ParamID id, double value) {
            capturedParams[id] = value;
        };

        memStream->seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer loadStreamer(memStream, kLittleEndian);
        loadMultiTapParamsToController(loadStreamer, mockSetParam);

        for (size_t i = 0; i < 6; ++i) {
            float expectedTime = static_cast<float>(i + 1) / 10.0f;
            auto it = capturedParams.find(kMultiTapCustomTime0Id + static_cast<Vst::ParamID>(i));
            REQUIRE(it != capturedParams.end());
            REQUIRE(it->second == Approx(expectedTime).margin(0.0001));
        }

        for (size_t i = 0; i < 6; ++i) {
            float expectedLevel = 1.0f - static_cast<float>(i) / 10.0f;
            auto it = capturedParams.find(kMultiTapCustomLevel0Id + static_cast<Vst::ParamID>(i));
            REQUIRE(it != capturedParams.end());
            REQUIRE(it->second == Approx(expectedLevel).margin(0.0001));
        }
    }
}
