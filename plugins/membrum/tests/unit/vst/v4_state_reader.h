#pragma once

// Helper for reading v4 state blobs in test readback.
// Used by old v2/v3 roundtrip tests that need to verify values
// after migration to v4 format.

#include "public.sdk/source/common/memorystream.h"
#include "dsp/pad_config.h"
#include <cstdint>

namespace Membrum {
namespace TestHelpers {

struct V4StateHeader
{
    Steinberg::int32 version = 0;
    Steinberg::int32 maxPolyphony = 0;
    Steinberg::int32 stealPolicy = 0;
};

struct V4PadData
{
    Steinberg::int32 exciterType = 0;
    Steinberg::int32 bodyModel = 0;
    double soundParams[34] = {};  // offsets 2-35 including choke/bus as float64
    std::uint8_t chokeGroup = 0;
    std::uint8_t outputBus = 0;
};

inline bool readV4Header(Steinberg::MemoryStream* s, V4StateHeader& hdr)
{
    s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    if (s->read(&hdr.version, sizeof(hdr.version), nullptr) != Steinberg::kResultOk) return false;
    if (s->read(&hdr.maxPolyphony, sizeof(hdr.maxPolyphony), nullptr) != Steinberg::kResultOk) return false;
    if (s->read(&hdr.stealPolicy, sizeof(hdr.stealPolicy), nullptr) != Steinberg::kResultOk) return false;
    return true;
}

inline bool readV4Pad(Steinberg::MemoryStream* s, V4PadData& pad)
{
    if (s->read(&pad.exciterType, sizeof(pad.exciterType), nullptr) != Steinberg::kResultOk) return false;
    if (s->read(&pad.bodyModel, sizeof(pad.bodyModel), nullptr) != Steinberg::kResultOk) return false;
    for (int i = 0; i < 34; ++i)
        if (s->read(&pad.soundParams[i], sizeof(double), nullptr) != Steinberg::kResultOk) return false;
    if (s->read(&pad.chokeGroup, sizeof(pad.chokeGroup), nullptr) != Steinberg::kResultOk) return false;
    if (s->read(&pad.outputBus, sizeof(pad.outputBus), nullptr) != Steinberg::kResultOk) return false;
    return true;
}

} // namespace TestHelpers
} // namespace Membrum
