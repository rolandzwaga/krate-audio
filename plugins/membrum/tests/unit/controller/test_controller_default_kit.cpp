// ==============================================================================
// T2.7 (MB-090): a fresh controller shows, saves and resets to the kit the
// fresh processor plays (DefaultKit), not 32 generic pads.
// ==============================================================================

#include "controller/controller.h"
#include "processor/processor.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "state/state_codec.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

std::vector<std::uint8_t> streamBytes(MemoryStream& ms)
{
    int64 size = 0;
    ms.seek(0, IBStream::kIBSeekEnd, &size);
    ms.seek(0, IBStream::kIBSeekSet, nullptr);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    if (size > 0)
    {
        int32 got = 0;
        ms.read(out.data(), static_cast<int32>(size), &got);
    }
    return out;
}

} // namespace

TEST_CASE("Controller: a fresh instance shows and defaults to the kit the processor plays",
          "[controller][default_kit]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    SECTION("the controller's kit snapshot is byte-identical to the processor's state")
    {
        MemoryStream procState;
        REQUIRE(processor.getState(&procState) == kResultOk);
        const auto procBytes = streamBytes(procState);

        // The controller's own save path (kit preset), written without the
        // session-scoped uiMode the processor never writes.
        IBStream* saved = controller.kitPresetStateProvider();
        REQUIRE(saved != nullptr);
        Membrum::State::KitSnapshot kit;
        const auto readResult = Membrum::State::readKitBlob(saved, kit);
        saved->release();
        REQUIRE(readResult == kResultOk);
        kit.hasSession = false;
        MemoryStream ctrlState;
        REQUIRE(Membrum::State::writeKitBlob(&ctrlState, kit) == kResultOk);
        const auto ctrlBytes = streamBytes(ctrlState);

        REQUIRE(ctrlBytes.size() == procBytes.size());
        std::size_t differing = 0;
        std::size_t firstDiff = procBytes.size();
        for (std::size_t i = 0; i < procBytes.size(); ++i)
        {
            if (ctrlBytes[i] != procBytes[i])
            {
                ++differing;
                if (firstDiff == procBytes.size())
                    firstDiff = i;
            }
        }
        INFO("differing bytes " << differing << ", first at " << firstDiff);
        CHECK(differing == 0U);
    }

    SECTION("every per-pad parameter's value and default are its default-kit value")
    {
        // The default-kit value of each parameter: what the processor's state
        // maps to through the host load path.
        MemoryStream procState;
        REQUIRE(processor.getState(&procState) == kResultOk);
        procState.seek(0, IBStream::kIBSeekSet, nullptr);
        Membrum::Controller loaded;
        REQUIRE(loaded.initialize(nullptr) == kResultOk);
        REQUIRE(loaded.setComponentState(&procState) == kResultOk);

        int perPad = 0;
        int mismatches = 0;
        const int32 count = controller.getParameterCount();
        for (int32 i = 0; i < count; ++i)
        {
            ParameterInfo info{};
            REQUIRE(controller.getParameterInfo(i, info) == kResultOk);
            const int pad = Membrum::padIndexFromParamId(static_cast<int>(info.id));
            if (pad < 0)
                continue;
            ++perPad;
            const double value = controller.getParamNormalized(info.id);
            const double kitValue = loaded.getParamNormalized(info.id);
            if (info.defaultNormalizedValue != value || value != kitValue)
            {
                ++mismatches;
                if (mismatches <= 10)
                {
                    UNSCOPED_INFO("pad " << pad << " id " << info.id << " default "
                                  << info.defaultNormalizedValue << " value " << value
                                  << " kit " << kitValue);
                }
            }
        }
        CHECK(perPad == Membrum::kNumPads * Membrum::kPadActiveParamCountV12);
        CHECK(mismatches == 0);
        REQUIRE(loaded.terminate() == kResultOk);
    }

    REQUIRE(controller.terminate() == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
