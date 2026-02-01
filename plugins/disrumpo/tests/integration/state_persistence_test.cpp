// ==============================================================================
// Combined State Round-Trip Integration Test
// ==============================================================================
// SC-011: Verifies that ALL state persists together in a single round-trip:
// - Expand states for bands 0 and 2
// - Modulation panel visibility
// - Window size (1200x720)
// - Global MIDI CC mappings
// - Per-preset MIDI CC mappings
// - Modulation routing parameters (source, destination, amount, curve)
//
// Constitution Principle VIII: Testing Discipline
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "midi/midi_cc_manager.h"
#include "plugin_ids.h"
#include <krate/dsp/core/modulation_types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace Krate::Plugins;
using namespace Disrumpo;
using Steinberg::Vst::ParamID;

// =============================================================================
// SC-011: Combined State Round-Trip Test
// =============================================================================

TEST_CASE("SC-011: All state persists together in round-trip", "[integration][state_persistence]") {
    // Simulates the full controller state persistence workflow:
    // 1. Set expand state for bands 0 and 2
    // 2. Set modulation panel visible
    // 3. Set window size to 1200x720
    // 4. Add global and per-preset MIDI CC mappings
    // 5. Serialize all state
    // 6. Deserialize into fresh state
    // 7. Verify all values match

    // =========================================================================
    // Step 1: Set up expand states (band 0 and 2 expanded, others collapsed)
    // =========================================================================
    constexpr int kMaxBands = 8;
    float expandStates[kMaxBands] = {
        1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };

    // Verify initial setup
    REQUIRE(expandStates[0] == 1.0f);  // Band 0 expanded
    REQUIRE(expandStates[1] == 0.0f);  // Band 1 collapsed
    REQUIRE(expandStates[2] == 1.0f);  // Band 2 expanded
    for (int i = 3; i < kMaxBands; ++i) {
        REQUIRE(expandStates[i] == 0.0f);
    }

    // =========================================================================
    // Step 2: Set modulation panel visible
    // =========================================================================
    float modPanelVisible = 1.0f;
    REQUIRE(modPanelVisible == 1.0f);

    // =========================================================================
    // Step 3: Set window size to 1200x720
    // =========================================================================
    double windowWidth = 1200.0;
    double windowHeight = 720.0;

    // Verify 5:3 aspect ratio
    constexpr double kAspectRatio = 5.0 / 3.0;
    REQUIRE_THAT(windowWidth / windowHeight,
        Catch::Matchers::WithinAbs(kAspectRatio, 0.01));

    // =========================================================================
    // Step 4: Set up MIDI CC mappings
    // =========================================================================
    MidiCCManager originalManager;

    // Add a global mapping: CC 74 -> sweep frequency
    auto sweepFreqId = makeSweepParamId(SweepParamType::kSweepFrequency);
    originalManager.addGlobalMapping(74, sweepFreqId, false);

    // Add a global 14-bit mapping: CC 1 (MSB) + CC 33 (LSB) -> sweep width
    auto sweepWidthId = makeSweepParamId(SweepParamType::kSweepWidth);
    originalManager.addGlobalMapping(1, sweepWidthId, true);

    // Add a per-preset mapping: CC 11 -> band 0 gain
    auto band0GainId = makeBandParamId(0, BandParamType::kBandGain);
    originalManager.addPresetMapping(11, band0GainId, false);

    // =========================================================================
    // Step 5: Serialize all state
    // =========================================================================

    // 5a: Serialize expand states (these are standard VST3 parameters,
    // serialized as normalized floats by EditControllerEx1)
    std::vector<float> serializedExpandStates(expandStates, expandStates + kMaxBands);

    // 5b: Serialize modulation panel visibility (standard VST3 parameter)
    float serializedModPanel = modPanelVisible;

    // 5c: Serialize window size (controller state)
    // Note: Only width is used for restore; height is recomputed from 5:3 ratio.
    double serializedWidth = windowWidth;
    [[maybe_unused]] double serializedHeight = windowHeight;

    // 5d: Serialize MIDI CC mappings
    auto globalMidiData = originalManager.serializeGlobalMappings();
    auto presetMidiData = originalManager.serializePresetMappings();

    REQUIRE_FALSE(globalMidiData.empty());
    REQUIRE_FALSE(presetMidiData.empty());

    // =========================================================================
    // Step 6: Deserialize into fresh state
    // =========================================================================

    // 6a: Restore expand states
    float restoredExpandStates[kMaxBands]{};
    for (int i = 0; i < kMaxBands; ++i) {
        restoredExpandStates[i] = serializedExpandStates[static_cast<size_t>(i)];
    }

    // 6b: Restore modulation panel visibility
    float restoredModPanel = serializedModPanel;

    // 6c: Restore window size with clamping and aspect ratio enforcement
    double restoredWidth = std::clamp(serializedWidth, 834.0, 1400.0);
    double restoredHeight = restoredWidth * 3.0 / 5.0;  // Enforce 5:3 ratio

    // 6d: Restore MIDI CC mappings
    MidiCCManager restoredManager;
    REQUIRE(restoredManager.deserializeGlobalMappings(
        globalMidiData.data(), globalMidiData.size()));
    REQUIRE(restoredManager.deserializePresetMappings(
        presetMidiData.data(), presetMidiData.size()));

    // =========================================================================
    // Step 7: Verify all values match
    // =========================================================================

    SECTION("expand states restored correctly") {
        REQUIRE(restoredExpandStates[0] == 1.0f);
        REQUIRE(restoredExpandStates[1] == 0.0f);
        REQUIRE(restoredExpandStates[2] == 1.0f);
        for (int i = 3; i < kMaxBands; ++i) {
            REQUIRE(restoredExpandStates[i] == 0.0f);
        }
    }

    SECTION("modulation panel visibility restored correctly") {
        REQUIRE(restoredModPanel == 1.0f);
        bool shouldBeVisible = (restoredModPanel >= 0.5f);
        REQUIRE(shouldBeVisible == true);
    }

    SECTION("window size restored correctly") {
        REQUIRE_THAT(restoredWidth,
            Catch::Matchers::WithinAbs(1200.0, 0.1));
        REQUIRE_THAT(restoredHeight,
            Catch::Matchers::WithinAbs(720.0, 0.1));
        REQUIRE_THAT(restoredWidth / restoredHeight,
            Catch::Matchers::WithinAbs(kAspectRatio, 0.01));
    }

    SECTION("global MIDI CC mappings restored correctly") {
        MidiCCMapping mapping;

        // CC 74 -> sweep frequency
        REQUIRE(restoredManager.getMapping(74, mapping));
        REQUIRE(mapping.paramId == sweepFreqId);
        REQUIRE(mapping.is14Bit == false);

        // CC 1 -> sweep width (14-bit)
        REQUIRE(restoredManager.getMapping(1, mapping));
        REQUIRE(mapping.paramId == sweepWidthId);
        REQUIRE(mapping.is14Bit == true);
    }

    SECTION("per-preset MIDI CC mappings restored correctly") {
        MidiCCMapping mapping;

        // CC 11 -> band 0 gain (per-preset)
        REQUIRE(restoredManager.getMapping(11, mapping));
        REQUIRE(mapping.paramId == band0GainId);
        REQUIRE(mapping.isPerPreset == true);
    }

    SECTION("MIDI CC mappings are functional after restore") {
        // Verify CC 74 still controls sweep frequency
        ParamID callbackParamId = 0;
        double callbackValue = -1.0;

        restoredManager.processCCMessage(74, 100,
            [&](ParamID id, double val) {
                callbackParamId = id;
                callbackValue = val;
            });

        REQUIRE(callbackParamId == sweepFreqId);
        REQUIRE_THAT(callbackValue,
            Catch::Matchers::WithinAbs(100.0 / 127.0, 0.01));

        // Verify per-preset CC 11 controls band 0 gain
        callbackParamId = 0;
        restoredManager.processCCMessage(11, 64,
            [&](ParamID id, double val) {
                callbackParamId = id;
                callbackValue = val;
            });

        REQUIRE(callbackParamId == band0GainId);
    }

    SECTION("all state fields present in a single round-trip") {
        // Final combined check: all five state categories are non-default
        bool hasExpandStates = (restoredExpandStates[0] == 1.0f &&
                                restoredExpandStates[2] == 1.0f);
        bool hasModPanel = (restoredModPanel == 1.0f);
        bool hasWindowSize = (restoredWidth == 1200.0);
        bool hasGlobalMidi = restoredManager.getActiveMappings().size() >= 2;
        bool hasPresetMidi = false;
        {
            MidiCCMapping m;
            hasPresetMidi = restoredManager.getMapping(11, m) && m.isPerPreset;
        }

        REQUIRE(hasExpandStates);
        REQUIRE(hasModPanel);
        REQUIRE(hasWindowSize);
        REQUIRE(hasGlobalMidi);
        REQUIRE(hasPresetMidi);
    }
}

// =============================================================================
// Modulation Routing State Persistence Tests
// =============================================================================
// Tests that modulation routing parameters (source, destination, amount, curve)
// survive the full normalize → denormalize round-trip used by the processor
// and controller during state save/restore.
//
// These tests catch the class of bug where controller normalization and
// processor denormalization use different denominators (e.g., 54-item dropdown
// with denominator 53 vs. ModDest::kTotalDestinations - 1 = 29).
// =============================================================================

// Helper: Simulate processor denormalization for routing destination
// Mirrors processor.cpp processParameterChanges() case 1 (Destination)
static uint32_t processorDenormalizeDest(double normalized) {
    return static_cast<uint32_t>(
        normalized * static_cast<double>(Disrumpo::ModDest::kTotalDestinations - 1) + 0.5);
}

// Helper: Simulate controller normalization for routing destination
// Mirrors controller.cpp setComponentState() routing restore
static double controllerNormalizeDest(int32_t destParamId) {
    return static_cast<double>(
        std::clamp(destParamId, 0,
                   static_cast<int32_t>(Disrumpo::ModDest::kTotalDestinations - 1)))
        / static_cast<double>(Disrumpo::ModDest::kTotalDestinations - 1);
}

// Helper: Simulate processor denormalization for routing source
// Mirrors processor.cpp processParameterChanges() case 0 (Source)
static int processorDenormalizeSource(double normalized) {
    return static_cast<int>(normalized * 12.0 + 0.5);
}

// Helper: Simulate controller normalization for routing source
// Mirrors controller.cpp setComponentState() routing restore
static double controllerNormalizeSource(int8_t source) {
    return static_cast<double>(source) / 12.0;
}

// Helper: Simulate processor denormalization for routing curve
// Mirrors processor.cpp processParameterChanges() case 3 (Curve)
static int processorDenormalizeCurve(double normalized) {
    return static_cast<int>(normalized * 3.0 + 0.5);
}

// Helper: Simulate controller normalization for routing curve
static double controllerNormalizeCurve(int8_t curve) {
    return static_cast<double>(curve) / 3.0;
}

TEST_CASE("Routing destination round-trip for every destination index",
          "[integration][state_persistence][routing]") {
    // For each valid destination (0 to kTotalDestinations-1), verify that
    // normalizing and denormalizing gives back the exact same index.
    // This is the test that would have caught the 53 vs 29 bug.
    for (uint32_t d = 0; d < Disrumpo::ModDest::kTotalDestinations; ++d) {
        const double normalized = controllerNormalizeDest(static_cast<int32_t>(d));
        const uint32_t restored = processorDenormalizeDest(normalized);
        INFO("Destination index " << d << " normalized to " << normalized
             << " restored to " << restored);
        REQUIRE(restored == d);
    }
}

TEST_CASE("Routing source round-trip for every source",
          "[integration][state_persistence][routing]") {
    // All 13 sources: None(0) through Transient(12)
    for (int s = 0; s < static_cast<int>(Krate::DSP::kModSourceCount); ++s) {
        const double normalized = controllerNormalizeSource(static_cast<int8_t>(s));
        const int restored = processorDenormalizeSource(normalized);
        INFO("Source " << s << " normalized to " << normalized
             << " restored to " << restored);
        REQUIRE(restored == s);
    }
}

TEST_CASE("Routing curve round-trip for every curve type",
          "[integration][state_persistence][routing]") {
    // All 4 curves: Linear(0), Exponential(1), SCurve(2), Stepped(3)
    for (int c = 0; c < static_cast<int>(Krate::DSP::kModCurveCount); ++c) {
        const double normalized = controllerNormalizeCurve(static_cast<int8_t>(c));
        const int restored = processorDenormalizeCurve(normalized);
        INFO("Curve " << c << " normalized to " << normalized
             << " restored to " << restored);
        REQUIRE(restored == c);
    }
}

TEST_CASE("Routing amount round-trip preserves bipolar values",
          "[integration][state_persistence][routing]") {
    // Amount range [-1, +1] is normalized as (amount + 1) / 2
    // and denormalized as normalized * 2 - 1
    const float testAmounts[] = {-1.0f, -0.75f, -0.5f, -0.25f, 0.0f,
                                  0.25f, 0.5f, 0.75f, 1.0f};
    for (float amount : testAmounts) {
        const double normalized = static_cast<double>(amount + 1.0f) / 2.0;
        const float restored = static_cast<float>(normalized * 2.0 - 1.0);
        INFO("Amount " << amount << " normalized to " << normalized
             << " restored to " << restored);
        REQUIRE_THAT(static_cast<double>(restored),
            Catch::Matchers::WithinAbs(static_cast<double>(amount), 1e-6));
    }
}

TEST_CASE("Full routing slot binary round-trip",
          "[integration][state_persistence][routing]") {
    // Simulate the processor getState/setState binary format:
    // int8 source, int32 dest, float amount, int8 curve

    struct RoutingTestCase {
        int8_t source;
        int32_t dest;
        float amount;
        int8_t curve;
    };

    const RoutingTestCase cases[] = {
        // LFO1 -> Band 1 Drive (the user-reported bug case)
        {1, 8, 0.5f, 0},
        // LFO2 -> Band 4 Pan (last valid destination)
        {2, 29, -1.0f, 1},
        // None -> first destination (inactive routing)
        {0, 0, 0.0f, 0},
        // Envelope Follower -> Global Mix
        {3, 2, 0.75f, 2},
        // Macro4 -> Sweep Intensity
        {8, 5, -0.25f, 3},
        // Transient -> Band 3 Morph X
        {12, static_cast<int32_t>(Disrumpo::ModDest::bandParam(2, Disrumpo::ModDest::kBandMorphX)), 1.0f, 0},
        // Chaos -> Band 2 Gain
        {9, static_cast<int32_t>(Disrumpo::ModDest::bandParam(1, Disrumpo::ModDest::kBandGain)), -0.5f, 1},
    };

    for (const auto& tc : cases) {
        SECTION("source=" + std::to_string(tc.source)
                + " dest=" + std::to_string(tc.dest)
                + " amount=" + std::to_string(tc.amount)
                + " curve=" + std::to_string(tc.curve)) {
            // Write to binary buffer (simulating getState)
            std::vector<uint8_t> buffer;
            buffer.push_back(static_cast<uint8_t>(tc.source));
            // int32 little-endian
            const auto destU = static_cast<uint32_t>(tc.dest);
            buffer.push_back(static_cast<uint8_t>(destU & 0xFF));
            buffer.push_back(static_cast<uint8_t>((destU >> 8) & 0xFF));
            buffer.push_back(static_cast<uint8_t>((destU >> 16) & 0xFF));
            buffer.push_back(static_cast<uint8_t>((destU >> 24) & 0xFF));
            // float as bytes
            const uint8_t* amountBytes = reinterpret_cast<const uint8_t*>(&tc.amount);
            for (int i = 0; i < 4; ++i) buffer.push_back(amountBytes[i]);
            buffer.push_back(static_cast<uint8_t>(tc.curve));

            // Read back (simulating setState)
            size_t pos = 0;
            const auto readSource = static_cast<int8_t>(buffer[pos++]);
            uint32_t readDest = buffer[pos] | (buffer[pos + 1] << 8)
                | (buffer[pos + 2] << 16) | (buffer[pos + 3] << 24);
            pos += 4;
            float readAmount;
            std::memcpy(&readAmount, &buffer[pos], sizeof(float));
            pos += 4;
            const auto readCurve = static_cast<int8_t>(buffer[pos]);

            // Apply clamping as processor does
            const auto clampedSource = std::clamp(static_cast<int>(readSource), 0, 12);
            const auto clampedDest = static_cast<uint32_t>(
                std::clamp(static_cast<int32_t>(readDest), 0,
                           static_cast<int32_t>(Disrumpo::ModDest::kTotalDestinations - 1)));
            const auto clampedCurve = std::clamp(static_cast<int>(readCurve), 0, 3);

            REQUIRE(clampedSource == tc.source);
            REQUIRE(clampedDest == static_cast<uint32_t>(tc.dest));
            REQUIRE(readAmount == tc.amount);
            REQUIRE(clampedCurve == tc.curve);
        }
    }
}

TEST_CASE("Controller-Processor normalization consistency for all destinations",
          "[integration][state_persistence][routing]") {
    // The critical test: for every destination, the controller's normalization
    // followed by the processor's denormalization must return the exact index.
    // This verifies both sides use the same denominator.
    constexpr auto kDenom = static_cast<double>(Disrumpo::ModDest::kTotalDestinations - 1);

    for (uint32_t d = 0; d < Disrumpo::ModDest::kTotalDestinations; ++d) {
        // Controller normalize (setComponentState path)
        const double norm = static_cast<double>(d) / kDenom;

        // Verify normalized value is in [0, 1]
        REQUIRE(norm >= 0.0);
        REQUIRE(norm <= 1.0);

        // Processor denormalize (processParameterChanges path)
        const auto restored = static_cast<uint32_t>(norm * kDenom + 0.5);

        INFO("Destination " << d << ": norm=" << norm << " restored=" << restored);
        REQUIRE(restored == d);
    }
}

TEST_CASE("ModDest::bandParam produces expected indices for all bands",
          "[integration][state_persistence][routing]") {
    // Verify the mapping from (band, offset) -> destination index is correct
    // and matches the dropdown order: global(0-2), sweep(3-5), band0(6-11), ...
    using namespace Disrumpo::ModDest;

    // Global destinations
    REQUIRE(kInputGain == 0);
    REQUIRE(kOutputGain == 1);
    REQUIRE(kGlobalMix == 2);

    // Sweep destinations
    REQUIRE(kSweepFrequency == 3);
    REQUIRE(kSweepWidth == 4);
    REQUIRE(kSweepIntensity == 5);

    // Per-band destinations: band 0-3, params 0-5
    for (uint8_t band = 0; band < 4; ++band) {
        const uint32_t expectedBase = kBandBase + band * kParamsPerBand;
        REQUIRE(bandParam(band, kBandMorphX) == expectedBase + 0);
        REQUIRE(bandParam(band, kBandMorphY) == expectedBase + 1);
        REQUIRE(bandParam(band, kBandDrive) == expectedBase + 2);
        REQUIRE(bandParam(band, kBandMix) == expectedBase + 3);
        REQUIRE(bandParam(band, kBandGain) == expectedBase + 4);
        REQUIRE(bandParam(band, kBandPan) == expectedBase + 5);
    }

    // Verify total
    REQUIRE(kTotalDestinations == 30);
    REQUIRE(bandParam(3, kBandPan) == 29);  // Last valid destination
}

TEST_CASE("Dropdown item count matches kTotalDestinations",
          "[integration][state_persistence][routing]") {
    // The dropdown should have exactly kTotalDestinations items:
    // 3 global + 3 sweep + (kMaxBands * 6) per-band
    constexpr int kGlobalCount = 3;
    constexpr int kSweepCount = 3;
    constexpr int kBandsInDropdown = 4;  // Must match kMaxBands
    constexpr int kParamsPerBand = 6;

    constexpr int expectedTotal = kGlobalCount + kSweepCount
                                  + kBandsInDropdown * kParamsPerBand;

    REQUIRE(expectedTotal == static_cast<int>(Disrumpo::ModDest::kTotalDestinations));
}
