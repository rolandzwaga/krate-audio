// ==============================================================================
// Compile-only guard: the parameter-pack mapping header does not pull in the
// DSP engine (audit T4.3, R-112).
// ==============================================================================
// This TU includes ONLY parameters/dropdown_mappings.h (plus Catch2). The engine
// header uses #pragma once, so its inclusion is detected by completeness of
// RuinaeEngine, which only that header defines.
// ==============================================================================

#include "parameters/dropdown_mappings.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

namespace Krate::DSP { class RuinaeEngine; }

namespace {
template <typename T>
constexpr bool isCompleteType = requires { sizeof(T); };

static_assert(!isCompleteType<Krate::DSP::RuinaeEngine>,
              "dropdown_mappings.h must not include engine/ruinae_engine.h (R-112): "
              "RuinaeModDest lives in ruinae_types.h");
} // namespace

TEST_CASE("modDestFromIndex clamps its index to the 15 global destinations",
          "[params][dropdown]") {
    using Krate::DSP::RuinaeModDest;
    CHECK(Ruinae::modDestFromIndex(0) == RuinaeModDest::GlobalFilterCutoff);
    CHECK(Ruinae::modDestFromIndex(14) == RuinaeModDest::ArpSpice);
    CHECK(Ruinae::modDestFromIndex(-5) == RuinaeModDest::GlobalFilterCutoff);
    CHECK(Ruinae::modDestFromIndex(15) == RuinaeModDest::ArpSpice);
    CHECK(Ruinae::modDestFromIndex(1000) == RuinaeModDest::ArpSpice);
}
