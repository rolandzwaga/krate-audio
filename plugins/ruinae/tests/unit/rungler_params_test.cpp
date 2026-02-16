// ==============================================================================
// Unit Test: Rungler Parameter Handling and State Persistence
// ==============================================================================
// Verifies that rungler parameters are correctly handled, formatted, and
// persisted through save/load cycles.
//
// Reference: specs/057-macros-rungler/spec.md FR-005, FR-007, FR-011
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"
#include "parameters/rungler_params.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

using Catch::Approx;

// =============================================================================
// T076: Rungler parameter changes update engine
// =============================================================================

TEST_CASE("Rungler parameter handle/format functions", "[rungler_params]") {
    SECTION("handleRunglerParamChange stores correct values for osc freqs") {
        Ruinae::RunglerParams params;

        // 0.5 normalized -> 0.1 * pow(1000, 0.5) = 0.1 * 31.623 = 3.162 Hz
        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerOsc1FreqId, 0.5);
        float expectedFreq = static_cast<float>(0.1 * std::pow(1000.0, 0.5));
        REQUIRE(params.osc1FreqHz.load() == Approx(expectedFreq).margin(0.01f));

        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerOsc2FreqId, 0.0);
        REQUIRE(params.osc2FreqHz.load() == Approx(0.1f).margin(0.001f));

        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerOsc2FreqId, 1.0);
        REQUIRE(params.osc2FreqHz.load() == Approx(100.0f).margin(0.1f));
    }

    SECTION("handleRunglerParamChange stores correct depth and filter") {
        Ruinae::RunglerParams params;

        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerDepthId, 0.5);
        REQUIRE(params.depth.load() == Approx(0.5f));

        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerFilterId, 0.75);
        REQUIRE(params.filter.load() == Approx(0.75f));
    }

    SECTION("handleRunglerParamChange stores correct bits") {
        Ruinae::RunglerParams params;

        // 0.0 -> 4 bits, 0.3333 -> 8 bits, 1.0 -> 16 bits
        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerBitsId, 0.0);
        REQUIRE(params.bits.load() == 4);

        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerBitsId, 1.0);
        REQUIRE(params.bits.load() == 16);

        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerBitsId, 0.3333);
        REQUIRE(params.bits.load() == 8);
    }

    SECTION("handleRunglerParamChange stores correct loop mode") {
        Ruinae::RunglerParams params;

        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerLoopModeId, 0.0);
        REQUIRE(params.loopMode.load() == false);

        Ruinae::handleRunglerParamChange(params, Ruinae::kRunglerLoopModeId, 1.0);
        REQUIRE(params.loopMode.load() == true);
    }

    SECTION("formatRunglerParam produces correct frequency string") {
        Steinberg::Vst::String128 str{};

        // Normalized 0.4337 ~= 2.0 Hz
        auto result = Ruinae::formatRunglerParam(Ruinae::kRunglerOsc1FreqId, 0.4337, str);
        REQUIRE(result == Steinberg::kResultOk);

        char ascii[128];
        Steinberg::UString(str, 128).toAscii(ascii, 128);
        // Should be approximately "2.00 Hz"
        std::string s(ascii);
        REQUIRE(s.find("Hz") != std::string::npos);
    }

    SECTION("formatRunglerParam produces correct depth percentage string") {
        Steinberg::Vst::String128 str{};

        auto result = Ruinae::formatRunglerParam(Ruinae::kRunglerDepthId, 0.5, str);
        REQUIRE(result == Steinberg::kResultOk);

        char ascii[128];
        Steinberg::UString(str, 128).toAscii(ascii, 128);
        REQUIRE(std::string(ascii) == "50%");
    }

    SECTION("formatRunglerParam produces correct bits string") {
        Steinberg::Vst::String128 str{};

        // 0.3333 normalized -> 8 bits
        auto result = Ruinae::formatRunglerParam(Ruinae::kRunglerBitsId, 0.3333, str);
        REQUIRE(result == Steinberg::kResultOk);

        char ascii[128];
        Steinberg::UString(str, 128).toAscii(ascii, 128);
        REQUIRE(std::string(ascii) == "8");
    }

    SECTION("formatRunglerParam returns kResultFalse for non-rungler IDs") {
        Steinberg::Vst::String128 str{};
        auto result = Ruinae::formatRunglerParam(Ruinae::kMasterGainId, 0.5, str);
        REQUIRE(result == Steinberg::kResultFalse);
    }
}

// =============================================================================
// T077: Rungler params save and load
// =============================================================================

TEST_CASE("Rungler params save and load round-trip", "[rungler_params][state_persistence]") {
    Ruinae::RunglerParams params;

    // Set non-default values
    params.osc1FreqHz.store(10.0f, std::memory_order_relaxed);
    params.osc2FreqHz.store(15.0f, std::memory_order_relaxed);
    params.depth.store(0.5f, std::memory_order_relaxed);
    params.filter.store(0.3f, std::memory_order_relaxed);
    params.bits.store(12, std::memory_order_relaxed);
    params.loopMode.store(true, std::memory_order_relaxed);

    // Save to stream
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        Ruinae::saveRunglerParams(params, streamer);
    }

    // Reset params to defaults
    params.osc1FreqHz.store(2.0f, std::memory_order_relaxed);
    params.osc2FreqHz.store(3.0f, std::memory_order_relaxed);
    params.depth.store(0.0f, std::memory_order_relaxed);
    params.filter.store(0.0f, std::memory_order_relaxed);
    params.bits.store(8, std::memory_order_relaxed);
    params.loopMode.store(false, std::memory_order_relaxed);

    // Load from stream
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        bool ok = Ruinae::loadRunglerParams(params, streamer);
        REQUIRE(ok);
    }

    // Verify restored values
    REQUIRE(params.osc1FreqHz.load() == Approx(10.0f));
    REQUIRE(params.osc2FreqHz.load() == Approx(15.0f));
    REQUIRE(params.depth.load() == Approx(0.5f));
    REQUIRE(params.filter.load() == Approx(0.3f));
    REQUIRE(params.bits.load() == 12);
    REQUIRE(params.loopMode.load() == true);
}

TEST_CASE("Rungler params controller load maps values correctly", "[rungler_params][state_persistence]") {
    Ruinae::RunglerParams params;
    params.osc1FreqHz.store(5.0f, std::memory_order_relaxed);
    params.osc2FreqHz.store(7.0f, std::memory_order_relaxed);
    params.depth.store(0.5f, std::memory_order_relaxed);
    params.filter.store(0.3f, std::memory_order_relaxed);
    params.bits.store(12, std::memory_order_relaxed);
    params.loopMode.store(true, std::memory_order_relaxed);

    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        Ruinae::saveRunglerParams(params, streamer);
    }

    // Track setParam calls
    struct ParamSet {
        Steinberg::Vst::ParamID id;
        double value;
    };
    std::vector<ParamSet> calls;

    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        Ruinae::loadRunglerParamsToController(streamer,
            [&](Steinberg::Vst::ParamID id, double value) {
                calls.push_back({id, value});
            });
    }

    REQUIRE(calls.size() == 6);

    // Verify osc1 freq inverse mapping: 5.0 Hz -> runglerFreqToNormalized(5.0)
    REQUIRE(calls[0].id == Ruinae::kRunglerOsc1FreqId);
    double expected1 = Ruinae::runglerFreqToNormalized(5.0f);
    REQUIRE(calls[0].value == Approx(expected1).margin(0.001));

    // Verify osc2 freq inverse mapping
    REQUIRE(calls[1].id == Ruinae::kRunglerOsc2FreqId);
    double expected2 = Ruinae::runglerFreqToNormalized(7.0f);
    REQUIRE(calls[1].value == Approx(expected2).margin(0.001));

    // Verify depth (linear, already normalized)
    REQUIRE(calls[2].id == Ruinae::kRunglerDepthId);
    REQUIRE(calls[2].value == Approx(0.5).margin(0.001));

    // Verify filter (linear, already normalized)
    REQUIRE(calls[3].id == Ruinae::kRunglerFilterId);
    REQUIRE(calls[3].value == Approx(0.3).margin(0.001));

    // Verify bits inverse mapping: 12 -> runglerBitsToNormalized(12)
    REQUIRE(calls[4].id == Ruinae::kRunglerBitsId);
    double expectedBits = Ruinae::runglerBitsToNormalized(12);
    REQUIRE(calls[4].value == Approx(expectedBits).margin(0.001));

    // Verify loop mode
    REQUIRE(calls[5].id == Ruinae::kRunglerLoopModeId);
    REQUIRE(calls[5].value == Approx(1.0).margin(0.001));
}

// =============================================================================
// T078: Rungler frequency mapping round-trip
// =============================================================================

TEST_CASE("Rungler frequency mapping round-trip", "[rungler_params]") {
    // Test that normalized -> Hz -> normalized round-trips correctly
    SECTION("0.0 maps to 0.1 Hz min") {
        float hz = Ruinae::runglerFreqFromNormalized(0.0);
        REQUIRE(hz == Approx(0.1f).margin(0.001f));
    }

    SECTION("1.0 maps to 100 Hz max") {
        float hz = Ruinae::runglerFreqFromNormalized(1.0);
        REQUIRE(hz == Approx(100.0f).margin(0.1f));
    }

    SECTION("round-trip at 2.0 Hz default") {
        double norm = Ruinae::runglerFreqToNormalized(2.0f);
        float hz = Ruinae::runglerFreqFromNormalized(norm);
        REQUIRE(hz == Approx(2.0f).margin(0.01f));
    }

    SECTION("round-trip at 50 Hz") {
        double norm = Ruinae::runglerFreqToNormalized(50.0f);
        float hz = Ruinae::runglerFreqFromNormalized(norm);
        REQUIRE(hz == Approx(50.0f).margin(0.1f));
    }
}

TEST_CASE("Rungler bits mapping round-trip", "[rungler_params]") {
    SECTION("0.0 maps to 4 bits") {
        int bits = Ruinae::runglerBitsFromNormalized(0.0);
        REQUIRE(bits == 4);
    }

    SECTION("1.0 maps to 16 bits") {
        int bits = Ruinae::runglerBitsFromNormalized(1.0);
        REQUIRE(bits == 16);
    }

    SECTION("round-trip at 8 bits") {
        double norm = Ruinae::runglerBitsToNormalized(8);
        int bits = Ruinae::runglerBitsFromNormalized(norm);
        REQUIRE(bits == 8);
    }

    SECTION("round-trip at 12 bits") {
        double norm = Ruinae::runglerBitsToNormalized(12);
        int bits = Ruinae::runglerBitsFromNormalized(norm);
        REQUIRE(bits == 12);
    }
}
