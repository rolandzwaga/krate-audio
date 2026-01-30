// =============================================================================
// SearchDebouncer Unit Tests
// =============================================================================
// Tests for search debounce logic (pure functions, no VSTGUI dependencies)
//
// The debouncer delays filter application by 200ms to avoid excessive updates
// while the user is typing. This improves performance and UX.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "ui/search_debouncer.h"

using namespace Krate::Plugins;

// =============================================================================
// Core Debounce Logic Tests
// =============================================================================

TEST_CASE("SearchDebouncer basic behavior", "[ui][preset-browser][search]") {
    SearchDebouncer debouncer;

    SECTION("initial state has no pending query") {
        REQUIRE(debouncer.getPendingQuery().empty());
        REQUIRE_FALSE(debouncer.hasPendingFilter());
    }

    SECTION("onTextChanged sets pending query but doesn't trigger immediately") {
        bool shouldFilter = debouncer.onTextChanged("test", 0);

        REQUIRE_FALSE(shouldFilter);  // Never triggers immediately
        REQUIRE(debouncer.getPendingQuery() == "test");
        REQUIRE(debouncer.hasPendingFilter());
    }

    SECTION("shouldApplyFilter returns false before debounce period") {
        debouncer.onTextChanged("test", 0);

        // Check at various times before 200ms
        REQUIRE_FALSE(debouncer.shouldApplyFilter(0));
        REQUIRE_FALSE(debouncer.shouldApplyFilter(50));
        REQUIRE_FALSE(debouncer.shouldApplyFilter(100));
        REQUIRE_FALSE(debouncer.shouldApplyFilter(199));
    }

    SECTION("shouldApplyFilter returns true after 200ms debounce period") {
        debouncer.onTextChanged("test", 0);

        REQUIRE(debouncer.shouldApplyFilter(200));
        REQUIRE(debouncer.shouldApplyFilter(250));
        REQUIRE(debouncer.shouldApplyFilter(1000));
    }

    SECTION("consuming filter clears pending state") {
        debouncer.onTextChanged("test", 0);
        REQUIRE(debouncer.hasPendingFilter());

        // After debounce period, consume the filter
        REQUIRE(debouncer.shouldApplyFilter(200));
        std::string query = debouncer.consumePendingFilter();

        REQUIRE(query == "test");
        REQUIRE_FALSE(debouncer.hasPendingFilter());
        REQUIRE(debouncer.getPendingQuery().empty());
    }
}

TEST_CASE("SearchDebouncer timer reset behavior", "[ui][preset-browser][search]") {
    SearchDebouncer debouncer;

    SECTION("new text change resets debounce timer") {
        // First change at t=0
        debouncer.onTextChanged("te", 0);

        // Second change at t=100 (before debounce)
        debouncer.onTextChanged("tes", 100);

        // At t=200, only 100ms since last change - shouldn't fire
        REQUIRE_FALSE(debouncer.shouldApplyFilter(200));

        // At t=300, 200ms since last change - should fire
        REQUIRE(debouncer.shouldApplyFilter(300));
        REQUIRE(debouncer.getPendingQuery() == "tes");
    }

    SECTION("rapid typing keeps resetting timer") {
        // Simulate rapid typing: t, te, tes, test
        debouncer.onTextChanged("t", 0);
        debouncer.onTextChanged("te", 50);
        debouncer.onTextChanged("tes", 100);
        debouncer.onTextChanged("test", 150);

        // At t=300, only 150ms since last change
        REQUIRE_FALSE(debouncer.shouldApplyFilter(300));

        // At t=350, 200ms since last change - should fire
        REQUIRE(debouncer.shouldApplyFilter(350));
        REQUIRE(debouncer.getPendingQuery() == "test");
    }

    SECTION("same text doesn't reset timer") {
        debouncer.onTextChanged("test", 0);

        // Same text at t=100 shouldn't reset
        debouncer.onTextChanged("test", 100);

        // At t=200, should still fire (200ms since FIRST change)
        REQUIRE(debouncer.shouldApplyFilter(200));
    }
}

TEST_CASE("SearchDebouncer empty string handling", "[ui][preset-browser][search]") {
    SearchDebouncer debouncer;

    SECTION("empty string clears immediately without debounce") {
        // First set a non-empty query
        debouncer.onTextChanged("test", 0);

        // Then clear it
        bool shouldFilter = debouncer.onTextChanged("", 50);

        // Empty string should trigger immediate filter (no debounce)
        REQUIRE(shouldFilter);
        REQUIRE(debouncer.getPendingQuery().empty());
        REQUIRE_FALSE(debouncer.hasPendingFilter());
    }

    SECTION("typing after clear restarts debounce") {
        // Clear
        debouncer.onTextChanged("", 0);

        // Start typing again
        debouncer.onTextChanged("new", 100);

        // Should need debounce period
        REQUIRE_FALSE(debouncer.shouldApplyFilter(200));
        REQUIRE(debouncer.shouldApplyFilter(300));
    }
}

TEST_CASE("SearchDebouncer reset", "[ui][preset-browser][search]") {
    SearchDebouncer debouncer;

    SECTION("reset clears all state") {
        debouncer.onTextChanged("test", 0);
        REQUIRE(debouncer.hasPendingFilter());

        debouncer.reset();

        REQUIRE_FALSE(debouncer.hasPendingFilter());
        REQUIRE(debouncer.getPendingQuery().empty());
        REQUIRE_FALSE(debouncer.shouldApplyFilter(1000));
    }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("SearchDebouncer edge cases", "[ui][preset-browser][search]") {
    SearchDebouncer debouncer;

    SECTION("whitespace-only treated as empty (clears immediately)") {
        debouncer.onTextChanged("test", 0);

        bool shouldFilter = debouncer.onTextChanged("   ", 50);

        // Whitespace-only should be treated as clear
        REQUIRE(shouldFilter);
    }

    SECTION("leading/trailing whitespace is preserved in query") {
        debouncer.onTextChanged("  test  ", 0);

        // Non-empty with whitespace should debounce normally
        REQUIRE_FALSE(debouncer.shouldApplyFilter(100));
        REQUIRE(debouncer.shouldApplyFilter(200));

        // Query preserves whitespace (trimming is caller's responsibility)
        REQUIRE(debouncer.getPendingQuery() == "  test  ");
    }

    SECTION("time overflow handling") {
        // Start near max uint64_t
        uint64_t nearMax = UINT64_MAX - 100;
        debouncer.onTextChanged("test", nearMax);

        // Even if time wraps, we should handle gracefully
        // (In practice, time won't wrap in a single session)
        REQUIRE_FALSE(debouncer.shouldApplyFilter(nearMax + 50));
    }
}
