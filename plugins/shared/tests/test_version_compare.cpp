#include <catch2/catch_test_macros.hpp>
#include "update/version_compare.h"

using Krate::Plugins::SemVer;

TEST_CASE("SemVer::parse - valid versions", "[update][version]") {
    SECTION("simple versions") {
        auto v = SemVer::parse("1.2.3");
        REQUIRE(v.has_value());
        const SemVer s = v.value_or(SemVer{});
        CHECK(s.major == 1);
        CHECK(s.minor == 2);
        CHECK(s.patch == 3);
    }

    SECTION("zero version") {
        auto v = SemVer::parse("0.0.0");
        REQUIRE(v.has_value());
        const SemVer s = v.value_or(SemVer{});
        CHECK(s.major == 0);
        CHECK(s.minor == 0);
        CHECK(s.patch == 0);
    }

    SECTION("large numbers") {
        auto v = SemVer::parse("10.200.3000");
        REQUIRE(v.has_value());
        const SemVer s = v.value_or(SemVer{});
        CHECK(s.major == 10);
        CHECK(s.minor == 200);
        CHECK(s.patch == 3000);
    }

    SECTION("trailing whitespace is tolerated") {
        auto v = SemVer::parse("1.0.0 ");
        REQUIRE(v.has_value());
        const SemVer s = v.value_or(SemVer{});
        CHECK(s.major == 1);
    }
}

TEST_CASE("SemVer::parse - invalid versions", "[update][version]") {
    CHECK_FALSE(SemVer::parse("").has_value());
    CHECK_FALSE(SemVer::parse("1").has_value());
    CHECK_FALSE(SemVer::parse("1.2").has_value());
    CHECK_FALSE(SemVer::parse("1.2.").has_value());
    CHECK_FALSE(SemVer::parse(".2.3").has_value());
    CHECK_FALSE(SemVer::parse("abc").has_value());
    CHECK_FALSE(SemVer::parse("1.2.3-beta").has_value());
    CHECK_FALSE(SemVer::parse("1.2.3.4").has_value());
    CHECK_FALSE(SemVer::parse("v1.2.3").has_value());
}

TEST_CASE("SemVer comparison", "[update][version]") {
    SECTION("equal versions") {
        CHECK(SemVer{1, 2, 3} == SemVer{1, 2, 3});
        CHECK_FALSE(SemVer{1, 2, 3} != SemVer{1, 2, 3});
    }

    SECTION("major version difference") {
        CHECK(SemVer{2, 0, 0} > SemVer{1, 99, 99});
        CHECK(SemVer{1, 0, 0} < SemVer{2, 0, 0});
    }

    SECTION("minor version difference") {
        CHECK(SemVer{1, 3, 0} > SemVer{1, 2, 99});
        CHECK(SemVer{1, 2, 0} < SemVer{1, 3, 0});
    }

    SECTION("patch version difference") {
        CHECK(SemVer{1, 2, 4} > SemVer{1, 2, 3});
        CHECK(SemVer{1, 2, 3} < SemVer{1, 2, 4});
    }

    SECTION("edge cases") {
        CHECK(SemVer{0, 0, 1} > SemVer{0, 0, 0});
        CHECK(SemVer{1, 0, 0} > SemVer{0, 99, 99});
    }
}

TEST_CASE("SemVer::toString", "[update][version]") {
    CHECK(SemVer{1, 2, 3}.toString() == "1.2.3");
    CHECK(SemVer{0, 0, 0}.toString() == "0.0.0");
    CHECK(SemVer{10, 20, 30}.toString() == "10.20.30");
}
