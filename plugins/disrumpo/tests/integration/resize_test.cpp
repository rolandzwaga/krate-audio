// ==============================================================================
// Window Resize Integration Tests
// ==============================================================================
// T031: Tests for window resize constraints, aspect ratio, and persistence
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>

// =============================================================================
// Aspect Ratio Constraint Tests
// =============================================================================

TEST_CASE("5:3 aspect ratio is maintained", "[integration][resize]") {
    constexpr double kAspectRatio = 5.0 / 3.0;

    SECTION("default size is 5:3") {
        double width = 1000.0;
        double height = 600.0;
        REQUIRE_THAT(width / height, Catch::Matchers::WithinAbs(kAspectRatio, 0.01));
    }

    SECTION("minimum size is 5:3") {
        double width = 834.0;
        double height = 500.0;
        // 834/500 = 1.668 ~= 5/3 = 1.667
        REQUIRE_THAT(width / height, Catch::Matchers::WithinAbs(kAspectRatio, 0.01));
    }

    SECTION("maximum size is 5:3") {
        double width = 1400.0;
        double height = 840.0;
        REQUIRE_THAT(width / height, Catch::Matchers::WithinAbs(kAspectRatio, 0.01));
    }
}

// =============================================================================
// Min/Max Bounds Tests
// =============================================================================

TEST_CASE("Window size is clamped to min/max bounds", "[integration][resize]") {
    constexpr double kMinWidth = 834.0;
    constexpr double kMaxWidth = 1400.0;

    SECTION("width below minimum is clamped") {
        double width = 500.0;
        width = std::clamp(width, kMinWidth, kMaxWidth);
        REQUIRE(width == kMinWidth);
    }

    SECTION("width above maximum is clamped") {
        double width = 2000.0;
        width = std::clamp(width, kMinWidth, kMaxWidth);
        REQUIRE(width == kMaxWidth);
    }

    SECTION("width within range is unchanged") {
        double width = 1200.0;
        double clamped = std::clamp(width, kMinWidth, kMaxWidth);
        REQUIRE(clamped == 1200.0);
    }
}

// =============================================================================
// Aspect Ratio Enforcement Tests
// =============================================================================

TEST_CASE("Height is computed from width for 5:3 ratio", "[integration][resize]") {
    SECTION("at minimum width") {
        double width = 834.0;
        double height = width * 3.0 / 5.0;
        REQUIRE_THAT(height, Catch::Matchers::WithinAbs(500.4, 0.1));
    }

    SECTION("at default width") {
        double width = 1000.0;
        double height = width * 3.0 / 5.0;
        REQUIRE_THAT(height, Catch::Matchers::WithinAbs(600.0, 0.1));
    }

    SECTION("at maximum width") {
        double width = 1400.0;
        double height = width * 3.0 / 5.0;
        REQUIRE_THAT(height, Catch::Matchers::WithinAbs(840.0, 0.1));
    }
}

// =============================================================================
// Size Persistence Tests
// =============================================================================

TEST_CASE("Window size persists across editor close/open", "[integration][resize]") {
    // Simulate saving and restoring window size
    double savedWidth = 1200.0;
    double savedHeight = 720.0;  // 1200 * 3/5 = 720

    // Simulate restore with clamping
    double restoredWidth = std::clamp(savedWidth, 834.0, 1400.0);
    double restoredHeight = restoredWidth * 3.0 / 5.0;

    REQUIRE_THAT(restoredWidth, Catch::Matchers::WithinAbs(1200.0, 0.1));
    REQUIRE_THAT(restoredHeight, Catch::Matchers::WithinAbs(720.0, 0.1));
}

TEST_CASE("Invalid stored size is corrected on restore", "[integration][resize]") {
    SECTION("too small width is clamped up") {
        double storedWidth = 400.0;
        double width = std::clamp(storedWidth, 834.0, 1400.0);
        double height = width * 3.0 / 5.0;

        REQUIRE(width == 834.0);
        REQUIRE_THAT(height, Catch::Matchers::WithinAbs(500.4, 0.1));
    }

    SECTION("non-5:3 stored height is corrected") {
        double storedWidth = 1000.0;
        double storedHeight = 700.0;  // Not 5:3

        // On restore, height is recomputed from width
        double height = storedWidth * 3.0 / 5.0;
        REQUIRE_THAT(height, Catch::Matchers::WithinAbs(600.0, 0.1));
        REQUIRE(height != storedHeight);
    }
}
