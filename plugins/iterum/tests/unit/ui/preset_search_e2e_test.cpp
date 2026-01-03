// =============================================================================
// Preset Search End-to-End Tests
// =============================================================================
// Tests for the complete search flow in PresetBrowserView
//
// Note: Full VSTGUI widget tests require runtime environment.
// These tests verify the interface contracts and callback wiring.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "ui/preset_data_source.h"
#include "ui/search_debouncer.h"
#include "preset/preset_info.h"
#include <functional>

using namespace Iterum;

// =============================================================================
// Test Helpers - Simulates PresetBrowserView behavior
// =============================================================================

/// Mock timer that can be controlled by tests
class MockTimer {
public:
    using Callback = std::function<void()>;

    void setCallback(Callback cb) { callback_ = std::move(cb); }

    void schedule(uint64_t delayMs) {
        scheduledTime_ = currentTime_ + delayMs;
        isScheduled_ = true;
    }

    void cancel() {
        isScheduled_ = false;
    }

    void advanceTime(uint64_t ms) {
        currentTime_ += ms;
        if (isScheduled_ && currentTime_ >= scheduledTime_) {
            isScheduled_ = false;
            if (callback_) {
                callback_();
            }
        }
    }

    uint64_t currentTime() const { return currentTime_; }
    bool isScheduled() const { return isScheduled_; }

private:
    Callback callback_;
    uint64_t currentTime_ = 0;
    uint64_t scheduledTime_ = 0;
    bool isScheduled_ = false;
};

/// Simulates the search behavior of PresetBrowserView
/// This mirrors what the actual implementation should do
class MockPresetBrowserView {
public:
    MockPresetBrowserView() {
        setupPresets();
        setupTimer();
    }

    // Simulates text edit focus gained - starts polling
    void onSearchFieldFocused() {
        isPolling_ = true;
        lastPolledText_ = currentSearchText_;
    }

    // Simulates text edit focus lost - stops polling, applies final filter
    void onSearchFieldBlurred() {
        isPolling_ = false;
        timer_.cancel();

        // Apply any pending filter immediately on blur
        if (debouncer_.hasPendingFilter()) {
            auto query = debouncer_.consumePendingFilter();
            dataSource_.setSearchFilter(query);
        }
    }

    // Simulates text being typed (called by polling timer)
    void setSearchText(const std::string& text) {
        currentSearchText_ = text;
    }

    // Simulates polling tick (called periodically while focused)
    void pollSearchText() {
        if (!isPolling_) return;

        if (currentSearchText_ != lastPolledText_) {
            lastPolledText_ = currentSearchText_;

            bool applyNow = debouncer_.onTextChanged(currentSearchText_, timer_.currentTime());
            if (applyNow) {
                // Empty/whitespace - apply immediately
                dataSource_.setSearchFilter("");
            } else {
                // Schedule debounce timer
                timer_.schedule(SearchDebouncer::kDebounceMs);
            }
        }
    }

    // Timer callback
    void onDebounceTimerFired() {
        if (debouncer_.shouldApplyFilter(timer_.currentTime())) {
            auto query = debouncer_.consumePendingFilter();
            dataSource_.setSearchFilter(query);
        }
    }

    // Advance time and trigger any scheduled callbacks
    void advanceTime(uint64_t ms) {
        timer_.advanceTime(ms);
    }

    int getVisiblePresetCount() {
        return dataSource_.dbGetNumRows(nullptr);
    }

    bool hasScheduledDebounce() const {
        return timer_.isScheduled();
    }

private:
    PresetDataSource dataSource_;
    SearchDebouncer debouncer_;
    MockTimer timer_;
    std::string currentSearchText_;
    std::string lastPolledText_;
    bool isPolling_ = false;

    void setupPresets() {
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

        dataSource_.setPresets(presets);
    }

    void setupTimer() {
        timer_.setCallback([this]() {
            onDebounceTimerFired();
        });
    }
};

// =============================================================================
// End-to-End Tests
// =============================================================================

TEST_CASE("E2E: Search field typing triggers debounced filter", "[ui][preset-browser][search][e2e]") {
    MockPresetBrowserView view;

    SECTION("typing applies filter after debounce period") {
        view.onSearchFieldFocused();
        REQUIRE(view.getVisiblePresetCount() == 3);

        // Type "tape"
        view.setSearchText("tape");
        view.pollSearchText();

        // Filter not applied yet
        REQUIRE(view.getVisiblePresetCount() == 3);
        REQUIRE(view.hasScheduledDebounce());

        // Wait for debounce
        view.advanceTime(200);

        // Filter now applied
        REQUIRE(view.getVisiblePresetCount() == 1);  // Only "Warm Tape Echo"
    }

    SECTION("rapid typing resets debounce") {
        view.onSearchFieldFocused();

        // Type "t"
        view.setSearchText("t");
        view.pollSearchText();
        view.advanceTime(50);

        // Type "ta"
        view.setSearchText("ta");
        view.pollSearchText();
        view.advanceTime(50);

        // Type "tap"
        view.setSearchText("tap");
        view.pollSearchText();
        view.advanceTime(50);

        // Type "tape" - this resets the debounce timer
        view.setSearchText("tape");
        view.pollSearchText();

        // At 150ms total, but only 0ms since "tape" was typed
        // Filter should NOT be applied yet
        REQUIRE(view.getVisiblePresetCount() == 3);

        // Wait remaining debounce time (200ms from "tape")
        view.advanceTime(200);
        REQUIRE(view.getVisiblePresetCount() == 1);
    }

    SECTION("clearing search applies immediately") {
        view.onSearchFieldFocused();

        // Apply "tape" filter
        view.setSearchText("tape");
        view.pollSearchText();
        view.advanceTime(200);
        REQUIRE(view.getVisiblePresetCount() == 1);

        // Clear search
        view.setSearchText("");
        view.pollSearchText();

        // Applied immediately, no debounce
        REQUIRE(view.getVisiblePresetCount() == 3);
        REQUIRE_FALSE(view.hasScheduledDebounce());
    }
}

TEST_CASE("E2E: Focus events control polling", "[ui][preset-browser][search][e2e]") {
    MockPresetBrowserView view;

    SECTION("blur applies pending filter immediately") {
        view.onSearchFieldFocused();

        // Type "tape" but don't wait for debounce
        view.setSearchText("tape");
        view.pollSearchText();
        view.advanceTime(100);  // Only 100ms, not enough for debounce

        REQUIRE(view.getVisiblePresetCount() == 3);  // Not applied yet

        // User clicks away (blur)
        view.onSearchFieldBlurred();

        // Filter applied immediately on blur
        REQUIRE(view.getVisiblePresetCount() == 1);
    }

    SECTION("focus then blur with no typing does nothing") {
        view.onSearchFieldFocused();
        view.pollSearchText();
        view.onSearchFieldBlurred();

        REQUIRE(view.getVisiblePresetCount() == 3);
    }
}

TEST_CASE("E2E: Search combined with mode filter", "[ui][preset-browser][search][e2e]") {
    // This test uses the real PresetDataSource to verify combined filtering
    PresetDataSource dataSource;

    std::vector<PresetInfo> presets;
    PresetInfo p1; p1.name = "Tape Echo"; p1.mode = DelayMode::Tape;
    PresetInfo p2; p2.name = "Tape Delay"; p2.mode = DelayMode::Tape;
    PresetInfo p3; p3.name = "Digital Tape"; p3.mode = DelayMode::Digital;
    PresetInfo p4; p4.name = "Clean"; p4.mode = DelayMode::Digital;
    presets.push_back(p1);
    presets.push_back(p2);
    presets.push_back(p3);
    presets.push_back(p4);
    dataSource.setPresets(presets);

    SECTION("mode filter then search narrows results") {
        dataSource.setModeFilter(static_cast<int>(DelayMode::Tape));
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // "Tape Echo", "Tape Delay"

        dataSource.setSearchFilter("echo");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 1);  // Only "Tape Echo"
    }

    SECTION("search then mode filter narrows results") {
        dataSource.setSearchFilter("tape");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 3);  // "Tape Echo", "Tape Delay", "Digital Tape"

        dataSource.setModeFilter(static_cast<int>(DelayMode::Tape));
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // "Tape Echo", "Tape Delay"
    }

    SECTION("clear search restores mode-filtered results") {
        dataSource.setModeFilter(static_cast<int>(DelayMode::Digital));
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // "Digital Tape", "Clean"

        dataSource.setSearchFilter("xyz");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 0);

        dataSource.setSearchFilter("");
        REQUIRE(dataSource.dbGetNumRows(nullptr) == 2);  // Back to Digital mode results
    }
}
