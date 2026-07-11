// Self-test for tests/test_helpers/uidesc_reachability.h using a synthetic uidesc
// (no plugin coupling): verifies the tag-map / reference extraction and the
// unreachable-param diff, including the allowlist.
#include <catch2/catch_test_macros.hpp>

#include "uidesc_reachability.h"

namespace {

// Minimal uidesc: 4 control-tag defs; a template binds Gain and Mix (via
// control-tag="..."), a second template binds Cutoff. "Hidden" (tag 300) is
// defined but referenced by no control.
const char* kUidesc = R"(<?xml version="1.0"?>
<vstgui-ui-description>
  <control-tags>
    <control-tag name="Gain" tag="100"/>
    <control-tag name="Mix" tag="101"/>
    <control-tag name="Cutoff" tag="200"/>
    <control-tag name="Hidden" tag="300"/>
  </control-tags>
  <template name="main">
    <view class="CKnob" control-tag="Gain"/>
    <view class="CSlider" control-tag="Mix"/>
  </template>
  <template name="filter">
    <view class="CKnob" control-tag="Cutoff"/>
  </template>
</vstgui-ui-description>
)";

}  // namespace

TEST_CASE("uidesc reachability: tag map extraction", "[test_helpers][reachability]") {
    auto map = Krate::Test::extractControlTagMap(kUidesc);
    REQUIRE(map.size() == 4);
    CHECK(map["Gain"] == 100);
    CHECK(map["Mix"] == 101);
    CHECK(map["Cutoff"] == 200);
    CHECK(map["Hidden"] == 300);
}

TEST_CASE("uidesc reachability: referenced names across templates", "[test_helpers][reachability]") {
    auto names = Krate::Test::extractReferencedTagNames(kUidesc);
    CHECK(names.count("Gain") == 1);
    CHECK(names.count("Mix") == 1);
    CHECK(names.count("Cutoff") == 1);  // on the inactive 'filter' template — still counted
    CHECK(names.count("Hidden") == 0);  // defined but never bound
}

TEST_CASE("uidesc reachability: unreachable diff + allowlist", "[test_helpers][reachability]") {
    const std::vector<int> registered = {100, 101, 200, 300};

    SECTION("Hidden (300) is flagged when not allowlisted") {
        auto missing = Krate::Test::unreachableParams(kUidesc, registered);
        REQUIRE(missing.size() == 1);
        CHECK(missing[0] == 300);
    }

    SECTION("allowlisting 300 clears it") {
        auto missing = Krate::Test::unreachableParams(kUidesc, registered, {300});
        CHECK(missing.empty());
    }

    SECTION("a registered id with no control-tag at all is flagged") {
        auto missing = Krate::Test::unreachableParams(kUidesc, {100, 999});
        REQUIRE(missing.size() == 1);
        CHECK(missing[0] == 999);
    }
}
