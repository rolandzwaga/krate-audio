// ==============================================================================
// COptionMenu Item Population Tests
// ==============================================================================
// Tests for the menu-items parsing and normalization roundtrip used by
// BandSubController::verifyView() to populate COptionMenu controls in
// TypeParams templates.
//
// Root cause: Shape slot parameters are RangeParameter(0,1, stepCount=0).
// COptionMenu controls can't auto-populate from these. The sub-controller
// reads a custom "menu-items" attribute and calls addEntry() manually.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/sub_controllers.h"

#include <cmath>
#include <string>
#include <vector>

using Catch::Approx;

// ==============================================================================
// parseMenuItems() — comma-separated string parsing
// ==============================================================================

TEST_CASE("parseMenuItems splits comma-separated string", "[menu-items][parse]") {
    SECTION("two items") {
        auto items = Disrumpo::parseMenuItems("Germanium,Silicon");
        REQUIRE(items.size() == 2);
        CHECK(items[0] == "Germanium");
        CHECK(items[1] == "Silicon");
    }

    SECTION("four items") {
        auto items = Disrumpo::parseMenuItems("Lorenz,Rossler,Chua,Henon");
        REQUIRE(items.size() == 4);
        CHECK(items[0] == "Lorenz");
        CHECK(items[1] == "Rossler");
        CHECK(items[2] == "Chua");
        CHECK(items[3] == "Henon");
    }

    SECTION("five items") {
        auto items = Disrumpo::parseMenuItems("A,E,I,O,U");
        REQUIRE(items.size() == 5);
        CHECK(items[0] == "A");
        CHECK(items[4] == "U");
    }

    SECTION("six items") {
        auto items = Disrumpo::parseMenuItems("XorPat,XorPrev,Rotate,Shuffle,BitAnd,Overflow");
        REQUIRE(items.size() == 6);
        CHECK(items[0] == "XorPat");
        CHECK(items[5] == "Overflow");
    }

    SECTION("single item") {
        auto items = Disrumpo::parseMenuItems("Only");
        REQUIRE(items.size() == 1);
        CHECK(items[0] == "Only");
    }

    SECTION("empty string returns empty vector") {
        auto items = Disrumpo::parseMenuItems("");
        CHECK(items.empty());
    }

    SECTION("items with spaces are preserved") {
        auto items = Disrumpo::parseMenuItems("Per Bin,Mag Only,Selective,Bitcrush");
        REQUIRE(items.size() == 4);
        CHECK(items[0] == "Per Bin");
        CHECK(items[1] == "Mag Only");
    }
}

// ==============================================================================
// Normalization roundtrip: index → normalized → processor mapping
// ==============================================================================
// Processor formula: static_cast<int>(normalized * maxIndex + 0.5f)
// COptionMenu normalization: index / (numItems - 1) = normalized
// Roundtrip must be exact for all indices.

static int processorDenormalize(float normalized, int maxIndex) {
    return static_cast<int>(normalized * static_cast<float>(maxIndex) + 0.5f);
}

TEST_CASE("Normalization roundtrip is exact for all item counts", "[menu-items][normalization]") {
    // Item counts found in the codebase: 2, 4, 5, 6
    auto testRoundtrip = [](int numItems) {
        int maxIndex = numItems - 1;
        for (int i = 0; i <= maxIndex; ++i) {
            // COptionMenu normalization: index / max
            float normalized = (maxIndex == 0) ? 0.0f
                : static_cast<float>(i) / static_cast<float>(maxIndex);
            // Processor denormalization
            int recovered = processorDenormalize(normalized, maxIndex);
            INFO("numItems=" << numItems << " index=" << i
                 << " normalized=" << normalized << " recovered=" << recovered);
            CHECK(recovered == i);
        }
    };

    SECTION("2 items (e.g., Germanium/Silicon)") { testRoundtrip(2); }
    SECTION("4 items (e.g., Lorenz/Rossler/Chua/Henon)") { testRoundtrip(4); }
    SECTION("5 items (e.g., A/E/I/O/U)") { testRoundtrip(5); }
    SECTION("6 items (e.g., BitwiseOperation)") { testRoundtrip(6); }
}

// ==============================================================================
// Reverse mapping: processor normalized value → COptionMenu index
// ==============================================================================
// CControl::setValueNormalized(val) → normalizedToPlain(val, 0, max) = val * max
// COptionMenu::setValue(float) → rounds to int index

TEST_CASE("Reverse mapping from parameter to menu index", "[menu-items][normalization]") {
    auto testReverse = [](int numItems) {
        int maxIndex = numItems - 1;
        for (int i = 0; i <= maxIndex; ++i) {
            // Processor stores normalized value
            float normalized = (maxIndex == 0) ? 0.0f
                : static_cast<float>(i) / static_cast<float>(maxIndex);
            // CControl::setValueNormalized denormalizes: val * (max - min) + min
            float plain = normalized * static_cast<float>(maxIndex);
            // COptionMenu::setValue rounds to int
            int menuIndex = static_cast<int>(std::round(plain));
            INFO("numItems=" << numItems << " expected=" << i
                 << " plain=" << plain << " menuIndex=" << menuIndex);
            CHECK(menuIndex == i);
        }
    };

    SECTION("2 items") { testReverse(2); }
    SECTION("4 items") { testReverse(4); }
    SECTION("5 items") { testReverse(5); }
    SECTION("6 items") { testReverse(6); }
}
