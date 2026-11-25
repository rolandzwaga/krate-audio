// T2.29 (MB-095): the processor reports an infinite tail. Voices ring for up
// to about 35 s and the coupling resonators after them, so no finite tail can
// be right; idle detection is the per-block silence flag.

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"

#include <pluginterfaces/vst/ivstaudioprocessor.h>

TEST_CASE("Processor reports an infinite tail and relies on silence flags",
          "[processor][host-contract]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultOk);

    CHECK(processor.getTailSamples() == Steinberg::Vst::kInfiniteTail);

    processor.terminate();
}
