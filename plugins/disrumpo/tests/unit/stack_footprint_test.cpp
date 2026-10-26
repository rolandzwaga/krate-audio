// ==============================================================================
// Stack Footprint Guard
// ==============================================================================
// Windows gives a thread a 1 MB stack. A test that builds a ~936 KB MorphEngine
// as a local overflowed it, which Catch2 reported as "Unknown exception" only
// when the case ran alone. MorphEngine now lives on the heap in every test (as
// in BandProcessor); the types tests still build as locals must stay under half
// the stack.

#include <catch2/catch_test_macros.hpp>

#include "dsp/band_processor.h"
#include "dsp/distortion_adapter.h"
#include "dsp/morph_engine.h"

#include <cstddef>

namespace {
constexpr std::size_t kStackLocalCeiling = 512 * 1024;  // half of Windows' 1 MB thread stack
}

TEST_CASE("Types tests build as locals stay under half the Windows thread stack", "[disrumpo][stack]") {
    INFO("sizeof(DistortionAdapter) = " << sizeof(Disrumpo::DistortionAdapter));
    INFO("sizeof(BandProcessor) = " << sizeof(Disrumpo::BandProcessor));
    INFO("sizeof(MorphEngine) = " << sizeof(Disrumpo::MorphEngine) << " (heap only)");
    CHECK(sizeof(Disrumpo::DistortionAdapter) < kStackLocalCeiling);
    CHECK(sizeof(Disrumpo::BandProcessor) < kStackLocalCeiling);
}
