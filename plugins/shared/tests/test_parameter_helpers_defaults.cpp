// ==============================================================================
// Dropdown helpers: the default index is the registered default
// ==============================================================================
// createDropdownParameterWithDefault and createNoteValueDropdown set the starting
// value to the requested index but left ParameterInfo::defaultNormalizedValue at
// 0, so a host's "reset to default" jumped every such dropdown to its first option
// (Iterum's Granular Time Mode reset to Free while a fresh instance opened Synced).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/parameter_helpers.h"

#include "public.sdk/source/vst/vstparameters.h"

#include <memory>

using namespace Krate::Plugins;
using Catch::Approx;

namespace {

const Steinberg::Vst::TChar* const kOptions[] = {STR16("A"), STR16("B"), STR16("C"), STR16("D")};

void requireStartsAndResetsTo(Steinberg::Vst::StringListParameter* raw, double expected) {
    std::unique_ptr<Steinberg::Vst::StringListParameter> param(raw);
    REQUIRE(param->getNormalized() == Approx(expected));
    REQUIRE(param->getInfo().defaultNormalizedValue == Approx(expected));
}

} // namespace

TEST_CASE("Dropdown helpers register the requested default index", "[parameter_helpers][defaults]") {
    SECTION("initializer-list overload") {
        requireStartsAndResetsTo(
            createDropdownParameterWithDefault(STR16("Mode"), 1, 2, {STR16("A"), STR16("B"), STR16("C"), STR16("D")}),
            2.0 / 3.0);
    }

    SECTION("pointer + count overload") {
        requireStartsAndResetsTo(createDropdownParameterWithDefault(STR16("Mode"), 1, 3, kOptions, 4), 1.0);
    }

    SECTION("note value dropdown") {
        requireStartsAndResetsTo(createNoteValueDropdown(STR16("Note"), 1, kOptions, 4, 1), 1.0 / 3.0);
    }

    SECTION("an out-of-range index leaves the first option as start and default") {
        requireStartsAndResetsTo(createDropdownParameterWithDefault(STR16("Mode"), 1, 9, kOptions, 4), 0.0);
    }
}
