// =============================================================================
// Search Integration Tests
// =============================================================================
// Tests for SearchDebouncer + PresetDataSource working together
// Simulates realistic typing patterns and verifies filter application timing
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "ui/search_debouncer.h"
#include "ui/preset_data_source.h"
#include "preset/preset_info.h"

using namespace Iterum;

// =============================================================================
// Test Helpers
// =============================================================================

static std::vector<PresetInfo> createTestPresets() {
    std::vector<PresetInfo> presets;

    PresetInfo p1;
    p1.name = "Warm Tape Echo";
    p1.mode = DelayMode::Tape;
    presets.push_back(p1);

    PresetInfo p2;
    p2.name = "Digital Clean";
    p2.mode = DelayMode::Digital;
    presets.push_back(p2);

    PresetInfo p3;
    p3.name = "Granular Shimmer";
    p3.mode = DelayMode::Granular;
    presets.push_back(p3);

    PresetInfo p4;
    p4.name = "Tape Warmth";
    p4.mode = DelayMode::Tape;
    presets.push_back(p4);

    return presets;
}

/// Simulates the controller logic that coordinates debouncer and data source
class SearchController {
public:
    SearchController() {
        dataSource_.setPresets(createTestPresets());
    }

    void onTextChanged(const std::string& text, uint64_t timeMs) {
        bool applyImmediately = debouncer_.onTextChanged(text, timeMs);
        if (applyImmediately) {
            // When clearing (empty or whitespace-only), apply empty filter
            dataSource_.setSearchFilter("");
        }
    }

    void tick(uint64_t timeMs) {
        if (debouncer_.shouldApplyFilter(timeMs)) {
            auto query = debouncer_.consumePendingFilter();
            dataSource_.setSearchFilter(query);
        }
    }

    int getVisibleCount() {
        return dataSource_.dbGetNumRows(nullptr);
    }

    bool hasPendingFilter() const {
        return debouncer_.hasPendingFilter();
    }

    void reset() {
        debouncer_.reset();
        dataSource_.setSearchFilter("");
    }

private:
    SearchDebouncer debouncer_;
    PresetDataSource dataSource_;
};

// =============================================================================
// Integration Tests
// =============================================================================

TEST_CASE("Search integration: debounce + data source", "[ui][preset-browser][search][integration]") {
    SearchController controller;

    SECTION("filter not applied until debounce period elapses") {
        controller.onTextChanged("tape", 0);

        // Before debounce: all presets visible
        REQUIRE(controller.getVisibleCount() == 4);
        REQUIRE(controller.hasPendingFilter());

        // Tick at 100ms - still before debounce
        controller.tick(100);
        REQUIRE(controller.getVisibleCount() == 4);

        // Tick at 200ms - debounce elapsed, filter should apply
        controller.tick(200);
        REQUIRE(controller.getVisibleCount() == 2);  // "Warm Tape Echo" and "Tape Warmth"
        REQUIRE_FALSE(controller.hasPendingFilter());
    }

    SECTION("rapid typing resets debounce, filter applied once at end") {
        // Simulate typing "tape" character by character
        controller.onTextChanged("t", 0);
        controller.onTextChanged("ta", 50);
        controller.onTextChanged("tap", 100);
        controller.onTextChanged("tape", 150);

        // Tick at 200ms - only 50ms since last change, shouldn't apply
        controller.tick(200);
        REQUIRE(controller.getVisibleCount() == 4);

        // Tick at 350ms - 200ms since last change, should apply
        controller.tick(350);
        REQUIRE(controller.getVisibleCount() == 2);
    }

    SECTION("clearing search applies immediately without debounce") {
        // First apply a filter
        controller.onTextChanged("tape", 0);
        controller.tick(200);
        REQUIRE(controller.getVisibleCount() == 2);

        // Clear search - should apply immediately
        controller.onTextChanged("", 250);
        REQUIRE(controller.getVisibleCount() == 4);  // All presets visible immediately
        REQUIRE_FALSE(controller.hasPendingFilter());
    }

    SECTION("whitespace-only clears search immediately") {
        controller.onTextChanged("tape", 0);
        controller.tick(200);
        REQUIRE(controller.getVisibleCount() == 2);

        controller.onTextChanged("   ", 250);
        REQUIRE(controller.getVisibleCount() == 4);  // Cleared immediately
    }

    SECTION("typing after clear restarts debounce") {
        // Clear first
        controller.onTextChanged("", 0);

        // Start typing
        controller.onTextChanged("digital", 100);
        REQUIRE(controller.getVisibleCount() == 4);  // Still showing all

        controller.tick(200);  // Only 100ms since "digital"
        REQUIRE(controller.getVisibleCount() == 4);

        controller.tick(300);  // 200ms since "digital"
        REQUIRE(controller.getVisibleCount() == 1);  // "Digital Clean"
    }
}

TEST_CASE("Search integration: realistic typing scenarios", "[ui][preset-browser][search][integration]") {
    SearchController controller;

    SECTION("user types, pauses, sees results, continues typing") {
        // User types "ta"
        controller.onTextChanged("t", 0);
        controller.onTextChanged("ta", 50);

        // User pauses - filter applies
        controller.tick(250);
        // "ta" matches: "Warm Tape Echo", "Digital Clean" (digiTAl), "Tape Warmth"
        REQUIRE(controller.getVisibleCount() == 3);

        // User continues typing "pe"
        controller.onTextChanged("tap", 300);
        controller.onTextChanged("tape", 350);

        // Still showing "ta" results until new debounce (3 results)
        controller.tick(400);
        REQUIRE(controller.getVisibleCount() == 3);

        // After full debounce, "tape" filter applies (2 results)
        controller.tick(550);
        REQUIRE(controller.getVisibleCount() == 2);
    }

    SECTION("user types query, deletes, types new query") {
        // Type "tape"
        controller.onTextChanged("tape", 0);
        controller.tick(200);
        REQUIRE(controller.getVisibleCount() == 2);

        // Delete all and type "digital"
        controller.onTextChanged("", 300);  // Clear - immediate
        REQUIRE(controller.getVisibleCount() == 4);

        controller.onTextChanged("digital", 350);
        controller.tick(450);  // 100ms, not enough
        REQUIRE(controller.getVisibleCount() == 4);

        controller.tick(550);  // 200ms
        REQUIRE(controller.getVisibleCount() == 1);
    }

    SECTION("user types fast then waits") {
        // Very fast typing
        controller.onTextChanged("g", 0);
        controller.onTextChanged("gr", 20);
        controller.onTextChanged("gra", 40);
        controller.onTextChanged("gran", 60);
        controller.onTextChanged("granu", 80);
        controller.onTextChanged("granul", 100);
        controller.onTextChanged("granula", 120);
        controller.onTextChanged("granular", 140);

        // All presets still showing
        REQUIRE(controller.getVisibleCount() == 4);

        // Wait for debounce
        controller.tick(340);
        REQUIRE(controller.getVisibleCount() == 1);  // "Granular Shimmer"
    }
}

TEST_CASE("Search integration: edge cases", "[ui][preset-browser][search][integration]") {
    SearchController controller;

    SECTION("multiple rapid ticks don't cause issues") {
        controller.onTextChanged("tape", 0);

        // Rapid ticks before debounce
        for (int i = 0; i < 100; i++) {
            controller.tick(static_cast<uint64_t>(i));
        }
        REQUIRE(controller.getVisibleCount() == 4);

        // Finally after debounce
        controller.tick(200);
        REQUIRE(controller.getVisibleCount() == 2);

        // More ticks don't change anything
        controller.tick(201);
        controller.tick(300);
        REQUIRE(controller.getVisibleCount() == 2);
    }

    SECTION("same text doesn't reset debounce") {
        controller.onTextChanged("tape", 0);

        // Type "tape" again at 150ms
        controller.onTextChanged("tape", 150);

        // Debounce should fire at 200ms (from first change), not 350ms
        controller.tick(200);
        REQUIRE(controller.getVisibleCount() == 2);
    }

    SECTION("search with no results then clear") {
        controller.onTextChanged("xyz", 0);
        controller.tick(200);
        REQUIRE(controller.getVisibleCount() == 0);

        controller.onTextChanged("", 300);
        REQUIRE(controller.getVisibleCount() == 4);
    }
}
