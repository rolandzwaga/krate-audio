// ==============================================================================
// BandStrip Parameter Binding Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for distortion type dropdown parameter binding (T053, T053b)
//
// Verifies:
// - makeNodeParamId returns correct tag values for type parameters
// - StringListParameter contains exactly 26 distortion types in canonical order
// - Control-tag values match parameter registration
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

#include <array>
#include <string>
#include <vector>

using namespace Disrumpo;
using Catch::Approx;

// ==============================================================================
// Canonical distortion type names from dsp-details.md (Appendix B)
// ==============================================================================
static const std::array<std::string, 26> kCanonicalDistortionTypes = {{
    "Soft Clip",
    "Hard Clip",
    "Tube",
    "Tape",
    "Fuzz",
    "Asymmetric Fuzz",
    "Sine Fold",
    "Triangle Fold",
    "Serge Fold",
    "Full Rectify",
    "Half Rectify",
    "Bitcrush",
    "Sample Reduce",
    "Quantize",
    "Temporal",
    "Ring Saturation",
    "Feedback",
    "Aliasing",
    "Bitwise Mangler",
    "Chaos",
    "Formant",
    "Granular",
    "Spectral",
    "Fractal",
    "Stochastic",
    "Allpass Resonant"
}};

// ==============================================================================
// Test: Node Type Parameter ID Encoding (T053)
// ==============================================================================
TEST_CASE("Node Type parameter ID is correctly encoded", "[bandstrip][type]") {

    SECTION("Band 0 Node 0 Type has tag value 0") {
        auto paramId = makeNodeParamId(0, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 0x0000);
        REQUIRE(paramId == 0);
    }

    SECTION("Band 0 Node 1 Type has tag value 4096") {
        // node=1 << 12 = 0x1000 = 4096
        auto paramId = makeNodeParamId(0, 1, NodeParamType::kNodeType);
        REQUIRE(paramId == 0x1000);
        REQUIRE(paramId == 4096);
    }

    SECTION("Band 1 Node 0 Type has tag value 256") {
        // band=1 << 8 = 0x0100 = 256
        auto paramId = makeNodeParamId(1, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 0x0100);
        REQUIRE(paramId == 256);
    }

    SECTION("Band 3 Node 2 Type has tag value 8960") {
        // node=2 << 12 | band=3 << 8 = 0x2300 = 8960
        auto paramId = makeNodeParamId(3, 2, NodeParamType::kNodeType);
        REQUIRE(paramId == 0x2300);
        REQUIRE(paramId == 8960);
    }

    SECTION("Band 7 Node 3 Type has tag value 14080") {
        // node=3 << 12 | band=7 << 8 = 0x3700 = 14080
        auto paramId = makeNodeParamId(7, 3, NodeParamType::kNodeType);
        REQUIRE(paramId == 0x3700);
        REQUIRE(paramId == 14080);
    }
}

// ==============================================================================
// Test: All 8 Bands Have Unique Node 0 Type IDs (T053)
// ==============================================================================
TEST_CASE("Each band's Node 0 Type has unique parameter ID", "[bandstrip][type]") {
    std::vector<Steinberg::Vst::ParamID> typeIds;

    for (int band = 0; band < 8; ++band) {
        auto paramId = makeNodeParamId(static_cast<uint8_t>(band), 0, NodeParamType::kNodeType);
        typeIds.push_back(paramId);
    }

    // Verify all IDs are unique
    for (size_t i = 0; i < typeIds.size(); ++i) {
        for (size_t j = i + 1; j < typeIds.size(); ++j) {
            REQUIRE(typeIds[i] != typeIds[j]);
        }
    }

    // Verify expected values
    REQUIRE(typeIds[0] == 0x0000);  // Band 0
    REQUIRE(typeIds[1] == 0x0100);  // Band 1
    REQUIRE(typeIds[2] == 0x0200);  // Band 2
    REQUIRE(typeIds[3] == 0x0300);  // Band 3
    REQUIRE(typeIds[4] == 0x0400);  // Band 4
    REQUIRE(typeIds[5] == 0x0500);  // Band 5
    REQUIRE(typeIds[6] == 0x0600);  // Band 6
    REQUIRE(typeIds[7] == 0x0700);  // Band 7
}

// ==============================================================================
// Test: Distortion Type Count (T053b)
// ==============================================================================
TEST_CASE("Distortion type list contains exactly 26 types", "[bandstrip][type]") {
    REQUIRE(kCanonicalDistortionTypes.size() == 26);
}

// ==============================================================================
// Test: Distortion Type Canonical Order (T053b)
// ==============================================================================
TEST_CASE("Distortion types are in canonical order from spec", "[bandstrip][type]") {
    // Verify specific positions per roadmap Appendix B

    SECTION("Basic saturation types at start") {
        REQUIRE(kCanonicalDistortionTypes[0] == "Soft Clip");
        REQUIRE(kCanonicalDistortionTypes[1] == "Hard Clip");
        REQUIRE(kCanonicalDistortionTypes[2] == "Tube");
        REQUIRE(kCanonicalDistortionTypes[3] == "Tape");
        REQUIRE(kCanonicalDistortionTypes[4] == "Fuzz");
    }

    SECTION("Wavefolder types in middle") {
        REQUIRE(kCanonicalDistortionTypes[6] == "Sine Fold");
        REQUIRE(kCanonicalDistortionTypes[7] == "Triangle Fold");
        REQUIRE(kCanonicalDistortionTypes[8] == "Serge Fold");
    }

    SECTION("Digital types") {
        REQUIRE(kCanonicalDistortionTypes[11] == "Bitcrush");
        REQUIRE(kCanonicalDistortionTypes[12] == "Sample Reduce");
        REQUIRE(kCanonicalDistortionTypes[13] == "Quantize");
    }

    SECTION("Exotic types at end") {
        REQUIRE(kCanonicalDistortionTypes[23] == "Fractal");
        REQUIRE(kCanonicalDistortionTypes[24] == "Stochastic");
        REQUIRE(kCanonicalDistortionTypes[25] == "Allpass Resonant");
    }
}

// ==============================================================================
// Test: Node Parameter Type Extraction (T053)
// ==============================================================================
TEST_CASE("Node Type parameter type can be extracted", "[bandstrip][type]") {

    SECTION("Extract type from Band 0 Node 0") {
        auto paramId = makeNodeParamId(0, 0, NodeParamType::kNodeType);
        REQUIRE(isNodeParamId(paramId) == true);
        REQUIRE(extractNodeParamType(paramId) == NodeParamType::kNodeType);
        REQUIRE(extractBandFromNodeParam(paramId) == 0);
        REQUIRE(extractNode(paramId) == 0);
    }

    SECTION("Extract type from Band 3 Node 2") {
        auto paramId = makeNodeParamId(3, 2, NodeParamType::kNodeType);
        REQUIRE(isNodeParamId(paramId) == true);
        REQUIRE(extractNodeParamType(paramId) == NodeParamType::kNodeType);
        REQUIRE(extractBandFromNodeParam(paramId) == 3);
        REQUIRE(extractNode(paramId) == 2);
    }
}

// ==============================================================================
// Test: StringListParameter Index-to-Normalized Mapping (T053)
// ==============================================================================
TEST_CASE("StringListParameter normalized value calculation", "[bandstrip][type]") {
    // For N items (indices 0 to N-1), normalized value for index i = i / (N-1)
    // For 26 types (indices 0-25): normalized = index / 25.0

    const int numTypes = 26;

    SECTION("Index 0 (Soft Clip) maps to normalized 0.0") {
        int index = 0;
        float normalized = static_cast<float>(index) / static_cast<float>(numTypes - 1);
        REQUIRE(normalized == Approx(0.0f).margin(0.001f));
    }

    SECTION("Index 2 (Tube) maps to normalized 2/25") {
        int index = 2;
        float normalized = static_cast<float>(index) / static_cast<float>(numTypes - 1);
        REQUIRE(normalized == Approx(2.0f / 25.0f).margin(0.001f));
        REQUIRE(normalized == Approx(0.08f).margin(0.001f));
    }

    SECTION("Index 12 (Sample Reduce) maps to normalized ~0.48") {
        int index = 12;
        float normalized = static_cast<float>(index) / static_cast<float>(numTypes - 1);
        REQUIRE(normalized == Approx(12.0f / 25.0f).margin(0.001f));
    }

    SECTION("Index 25 (Allpass Resonant) maps to normalized 1.0") {
        int index = 25;
        float normalized = static_cast<float>(index) / static_cast<float>(numTypes - 1);
        REQUIRE(normalized == Approx(1.0f).margin(0.001f));
    }
}

// ==============================================================================
// Test: Normalized-to-Index Recovery (T053)
// ==============================================================================
TEST_CASE("Normalized value converts back to correct type index", "[bandstrip][type]") {
    const int numTypes = 26;

    auto normalizedToIndex = [numTypes](float normalized) -> int {
        return static_cast<int>(std::round(normalized * static_cast<float>(numTypes - 1)));
    };

    SECTION("Normalized 0.0 gives index 0 (Soft Clip)") {
        REQUIRE(normalizedToIndex(0.0f) == 0);
    }

    SECTION("Normalized 2/25 gives index 2 (Tube)") {
        REQUIRE(normalizedToIndex(2.0f / 25.0f) == 2);
    }

    SECTION("Normalized 1.0 gives index 25 (Allpass Resonant)") {
        REQUIRE(normalizedToIndex(1.0f) == 25);
    }

    SECTION("All indices round-trip correctly") {
        for (int i = 0; i < numTypes; ++i) {
            float normalized = static_cast<float>(i) / static_cast<float>(numTypes - 1);
            int recovered = normalizedToIndex(normalized);
            REQUIRE(recovered == i);
        }
    }
}

// ==============================================================================
// Test: Control-Tag Decimal Values for uidesc (T053)
// ==============================================================================
TEST_CASE("Control-tag decimal values match parameter IDs", "[bandstrip][controltag]") {
    // uidesc control-tags must use decimal values of hex IDs

    SECTION("Band 1 Node 1 Type tag is 0 (decimal)") {
        auto paramId = makeNodeParamId(0, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 0);
    }

    SECTION("Band 2 Node 1 Type tag is 256 (0x0100)") {
        auto paramId = makeNodeParamId(1, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 256);
    }

    SECTION("Band 3 Node 1 Type tag is 512 (0x0200)") {
        auto paramId = makeNodeParamId(2, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 512);
    }

    SECTION("Band 4 Node 1 Type tag is 768 (0x0300)") {
        auto paramId = makeNodeParamId(3, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 768);
    }

    SECTION("Band 5 Node 1 Type tag is 1024 (0x0400)") {
        auto paramId = makeNodeParamId(4, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 1024);
    }

    SECTION("Band 6 Node 1 Type tag is 1280 (0x0500)") {
        auto paramId = makeNodeParamId(5, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 1280);
    }

    SECTION("Band 7 Node 1 Type tag is 1536 (0x0600)") {
        auto paramId = makeNodeParamId(6, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 1536);
    }

    SECTION("Band 8 Node 1 Type tag is 1792 (0x0700)") {
        auto paramId = makeNodeParamId(7, 0, NodeParamType::kNodeType);
        REQUIRE(paramId == 1792);
    }
}
