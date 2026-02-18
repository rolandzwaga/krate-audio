#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/harmonizer_engine.h>

#include <algorithm>
#include <vector>

// =============================================================================
// Phase 2: Lifecycle Tests (FR-014, FR-015)
// =============================================================================

TEST_CASE("HarmonizerEngine isPrepared() returns false before prepare()",
          "[systems][harmonizer][lifecycle]") {
    Krate::DSP::HarmonizerEngine engine;
    REQUIRE(engine.isPrepared() == false);
}

TEST_CASE("HarmonizerEngine isPrepared() returns true after prepare()",
          "[systems][harmonizer][lifecycle]") {
    Krate::DSP::HarmonizerEngine engine;
    engine.prepare(44100.0, 512);
    REQUIRE(engine.isPrepared() == true);
}

TEST_CASE("HarmonizerEngine reset() preserves prepared state",
          "[systems][harmonizer][lifecycle]") {
    Krate::DSP::HarmonizerEngine engine;
    engine.prepare(44100.0, 512);
    REQUIRE(engine.isPrepared() == true);

    engine.reset();
    REQUIRE(engine.isPrepared() == true);
}

TEST_CASE("HarmonizerEngine process() before prepare() zero-fills outputs (FR-015)",
          "[systems][harmonizer][lifecycle]") {
    Krate::DSP::HarmonizerEngine engine;
    // Do NOT call prepare()

    constexpr std::size_t numSamples = 64;
    std::vector<float> input(numSamples, 1.0f);   // Non-zero input
    std::vector<float> outputL(numSamples, 999.0f); // Fill with garbage
    std::vector<float> outputR(numSamples, 999.0f); // Fill with garbage

    engine.process(input.data(), outputL.data(), outputR.data(), numSamples);

    // Verify both output channels are zero-filled
    bool allLeftZero = std::all_of(outputL.begin(), outputL.end(),
                                   [](float s) { return s == 0.0f; });
    bool allRightZero = std::all_of(outputR.begin(), outputR.end(),
                                    [](float s) { return s == 0.0f; });
    REQUIRE(allLeftZero);
    REQUIRE(allRightZero);
}
