// ==============================================================================
// Visibility Controller Multi-Control Test
// ==============================================================================
// Regression test for bug where VisibilityController only hid the FIRST
// control with a given tag, leaving other controls (like CParamDisplay
// value labels) visible when they should be hidden.
//
// BUG: When CParamDisplay was added with the same control-tag as CSlider,
// the findControlByTag function only found ONE control, so the slider
// would hide but the value display would remain visible.
//
// FIX: Change findControlByTag to findAllControlsByTag, returning a vector
// of ALL controls with the matching tag.
//
// This test verifies that ALL controls with a given tag are found and updated.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <functional>
#include <cstdint>

namespace {

// =============================================================================
// Mock types to simulate VSTGUI control hierarchy
// =============================================================================

// Simulates a VSTGUI CControl
class MockControl {
public:
    explicit MockControl(int32_t tag) : tag_(tag) {}

    int32_t getTag() const { return tag_; }

    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }

private:
    int32_t tag_;
    bool visible_ = true;
};

// Simulates a view container with multiple controls
class MockViewContainer {
public:
    void addControl(MockControl* control) {
        controls_.push_back(control);
    }

    const std::vector<MockControl*>& getControls() const {
        return controls_;
    }

private:
    std::vector<MockControl*> controls_;
};

// =============================================================================
// The functions under test - mirrors the pattern from controller.cpp
// =============================================================================

// ORIGINAL (BROKEN): Only returns first control with tag
MockControl* findControlByTag_Original(MockViewContainer* container, int32_t tag) {
    if (!container) return nullptr;
    for (auto* control : container->getControls()) {
        if (control->getTag() == tag) {
            return control;  // BUG: Returns first match only!
        }
    }
    return nullptr;
}

// FIXED: Returns ALL controls with tag
std::vector<MockControl*> findAllControlsByTag(MockViewContainer* container, int32_t tag) {
    std::vector<MockControl*> results;
    if (!container) return results;
    for (auto* control : container->getControls()) {
        if (control->getTag() == tag) {
            results.push_back(control);
        }
    }
    return results;
}

// Simulates the visibility update loop
void updateVisibility_Original(MockViewContainer* container, int32_t tag, bool visible) {
    // BROKEN: Only updates first control
    auto* control = findControlByTag_Original(container, tag);
    if (control) {
        control->setVisible(visible);
    }
}

void updateVisibility_Fixed(MockViewContainer* container, int32_t tag, bool visible) {
    // FIXED: Updates ALL controls with tag
    auto controls = findAllControlsByTag(container, tag);
    for (auto* control : controls) {
        control->setVisible(visible);
    }
}

} // namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("findAllControlsByTag finds all controls with matching tag",
          "[controller][visibility][regression]") {

    SECTION("Single control with tag returns one result") {
        MockViewContainer container;
        MockControl slider(100);
        container.addControl(&slider);

        auto results = findAllControlsByTag(&container, 100);

        REQUIRE(results.size() == 1);
        REQUIRE(results[0] == &slider);
    }

    SECTION("Two controls with same tag returns both") {
        MockViewContainer container;
        MockControl slider(100);       // CSlider with tag 100
        MockControl valueDisplay(100); // CParamDisplay with same tag 100
        container.addControl(&slider);
        container.addControl(&valueDisplay);

        auto results = findAllControlsByTag(&container, 100);

        REQUIRE(results.size() == 2);
        // Both controls should be in the results
        bool foundSlider = false, foundDisplay = false;
        for (auto* ctrl : results) {
            if (ctrl == &slider) foundSlider = true;
            if (ctrl == &valueDisplay) foundDisplay = true;
        }
        REQUIRE(foundSlider);
        REQUIRE(foundDisplay);
    }

    SECTION("Controls with different tags are not included") {
        MockViewContainer container;
        MockControl slider(100);
        MockControl label(9901);  // Label has different tag
        MockControl valueDisplay(100);
        container.addControl(&slider);
        container.addControl(&label);
        container.addControl(&valueDisplay);

        auto results = findAllControlsByTag(&container, 100);

        REQUIRE(results.size() == 2);
        // Label should not be in results
        for (auto* ctrl : results) {
            REQUIRE(ctrl->getTag() == 100);
        }
    }

    SECTION("Empty container returns empty vector") {
        MockViewContainer container;

        auto results = findAllControlsByTag(&container, 100);

        REQUIRE(results.empty());
    }

    SECTION("No matching tag returns empty vector") {
        MockViewContainer container;
        MockControl control(200);
        container.addControl(&control);

        auto results = findAllControlsByTag(&container, 100);

        REQUIRE(results.empty());
    }

    SECTION("Null container returns empty vector") {
        auto results = findAllControlsByTag(nullptr, 100);

        REQUIRE(results.empty());
    }
}

TEST_CASE("Original findControlByTag only finds first control (demonstrates bug)",
          "[controller][visibility][regression]") {

    SECTION("BUG: Only returns first control when two have same tag") {
        MockViewContainer container;
        MockControl slider(100);
        MockControl valueDisplay(100);
        container.addControl(&slider);
        container.addControl(&valueDisplay);

        // Original function only returns first match
        auto* result = findControlByTag_Original(&container, 100);

        REQUIRE(result == &slider);
        // valueDisplay is NOT returned - this is the bug!
    }
}

TEST_CASE("Visibility update affects all controls with tag",
          "[controller][visibility][regression]") {

    SECTION("BROKEN: Original update only hides first control") {
        MockViewContainer container;
        MockControl slider(100);
        MockControl valueDisplay(100);
        container.addControl(&slider);
        container.addControl(&valueDisplay);

        // Both start visible
        REQUIRE(slider.isVisible());
        REQUIRE(valueDisplay.isVisible());

        // Hide controls with tag 100
        updateVisibility_Original(&container, 100, false);

        // BUG: Only slider is hidden!
        REQUIRE_FALSE(slider.isVisible());
        REQUIRE(valueDisplay.isVisible());  // Bug: still visible!
    }

    SECTION("FIXED: New update hides all controls with same tag") {
        MockViewContainer container;
        MockControl slider(100);
        MockControl valueDisplay(100);
        container.addControl(&slider);
        container.addControl(&valueDisplay);

        // Both start visible
        REQUIRE(slider.isVisible());
        REQUIRE(valueDisplay.isVisible());

        // Hide controls with tag 100
        updateVisibility_Fixed(&container, 100, false);

        // Both should be hidden
        REQUIRE_FALSE(slider.isVisible());
        REQUIRE_FALSE(valueDisplay.isVisible());
    }

    SECTION("Show operation affects all controls") {
        MockViewContainer container;
        MockControl slider(100);
        MockControl valueDisplay(100);
        slider.setVisible(false);
        valueDisplay.setVisible(false);
        container.addControl(&slider);
        container.addControl(&valueDisplay);

        // Both start hidden
        REQUIRE_FALSE(slider.isVisible());
        REQUIRE_FALSE(valueDisplay.isVisible());

        // Show controls with tag 100
        updateVisibility_Fixed(&container, 100, true);

        // Both should be visible
        REQUIRE(slider.isVisible());
        REQUIRE(valueDisplay.isVisible());
    }
}

TEST_CASE("TimeMode visibility toggle scenario",
          "[controller][visibility][regression]") {
    // This test simulates the real scenario:
    // - Digital panel has DelayTime slider (tag = kDigitalDelayTimeId)
    // - Digital panel has DelayTime value display (tag = kDigitalDelayTimeId)
    // - Digital panel has DelayTime label (tag = 9901)
    // - When TimeMode changes to Synced, all three should hide
    // - When TimeMode changes to Free, all three should show

    constexpr int32_t kDelayTimeLabelTag = 9901;
    constexpr int32_t kDigitalDelayTimeId = 100;

    MockViewContainer container;
    MockControl delayTimeLabel(kDelayTimeLabelTag);
    MockControl delayTimeSlider(kDigitalDelayTimeId);
    MockControl delayTimeValueDisplay(kDigitalDelayTimeId);
    container.addControl(&delayTimeLabel);
    container.addControl(&delayTimeSlider);
    container.addControl(&delayTimeValueDisplay);

    SECTION("Switching to Synced mode hides all delay time controls") {
        // All start visible
        REQUIRE(delayTimeLabel.isVisible());
        REQUIRE(delayTimeSlider.isVisible());
        REQUIRE(delayTimeValueDisplay.isVisible());

        // Simulate TimeMode -> Synced: hide delay time controls
        // (In real code, this is triggered by parameter change callback)
        updateVisibility_Fixed(&container, kDelayTimeLabelTag, false);
        updateVisibility_Fixed(&container, kDigitalDelayTimeId, false);

        // All should be hidden
        REQUIRE_FALSE(delayTimeLabel.isVisible());
        REQUIRE_FALSE(delayTimeSlider.isVisible());
        REQUIRE_FALSE(delayTimeValueDisplay.isVisible());
    }

    SECTION("Switching to Free mode shows all delay time controls") {
        // Start hidden (synced mode)
        delayTimeLabel.setVisible(false);
        delayTimeSlider.setVisible(false);
        delayTimeValueDisplay.setVisible(false);

        // Simulate TimeMode -> Free: show delay time controls
        updateVisibility_Fixed(&container, kDelayTimeLabelTag, true);
        updateVisibility_Fixed(&container, kDigitalDelayTimeId, true);

        // All should be visible
        REQUIRE(delayTimeLabel.isVisible());
        REQUIRE(delayTimeSlider.isVisible());
        REQUIRE(delayTimeValueDisplay.isVisible());
    }
}
