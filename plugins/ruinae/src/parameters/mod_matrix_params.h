#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct ModMatrixSlot {
    std::atomic<int> source{0};   // ModSource enum (0-12)
    std::atomic<int> dest{0};     // RuinaeModDest index (0-6)
    std::atomic<float> amount{0.0f}; // -1 to +1
};

struct ModMatrixParams {
    std::array<ModMatrixSlot, 8> slots;
};

inline void handleModMatrixParamChange(
    ModMatrixParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    // Determine slot index and sub-parameter
    if (id < kModMatrixBaseId || id > kModMatrixSlot7AmountId) return;
    int offset = static_cast<int>(id - kModMatrixBaseId);
    int slotIdx = offset / 3;
    int subParam = offset % 3;
    if (slotIdx < 0 || slotIdx >= 8) return;

    auto& slot = params.slots[static_cast<size_t>(slotIdx)];
    switch (subParam) {
        case 0: // Source
            slot.source.store(
                std::clamp(static_cast<int>(value * (kModSourceCount - 1) + 0.5), 0, kModSourceCount - 1),
                std::memory_order_relaxed);
            break;
        case 1: // Dest
            slot.dest.store(
                std::clamp(static_cast<int>(value * (kModDestCount - 1) + 0.5), 0, kModDestCount - 1),
                std::memory_order_relaxed);
            break;
        case 2: // Amount (-1 to +1)
            slot.amount.store(
                std::clamp(static_cast<float>(value * 2.0 - 1.0), -1.0f, 1.0f),
                std::memory_order_relaxed);
            break;
    }
}

inline void registerModMatrixParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // IDs are sequential: base + slot*3 + {0=source, 1=dest, 2=amount}
    const Steinberg::Vst::ParamID slotSourceIds[] = {
        kModMatrixSlot0SourceId, kModMatrixSlot1SourceId, kModMatrixSlot2SourceId,
        kModMatrixSlot3SourceId, kModMatrixSlot4SourceId, kModMatrixSlot5SourceId,
        kModMatrixSlot6SourceId, kModMatrixSlot7SourceId,
    };
    const Steinberg::Vst::ParamID slotDestIds[] = {
        kModMatrixSlot0DestId, kModMatrixSlot1DestId, kModMatrixSlot2DestId,
        kModMatrixSlot3DestId, kModMatrixSlot4DestId, kModMatrixSlot5DestId,
        kModMatrixSlot6DestId, kModMatrixSlot7DestId,
    };
    const Steinberg::Vst::ParamID slotAmountIds[] = {
        kModMatrixSlot0AmountId, kModMatrixSlot1AmountId, kModMatrixSlot2AmountId,
        kModMatrixSlot3AmountId, kModMatrixSlot4AmountId, kModMatrixSlot5AmountId,
        kModMatrixSlot6AmountId, kModMatrixSlot7AmountId,
    };

    for (int i = 0; i < 8; ++i) {
        char srcName[64], dstName[64], amtName[64];
        snprintf(srcName, sizeof(srcName), "Mod %d Source", i + 1);
        snprintf(dstName, sizeof(dstName), "Mod %d Dest", i + 1);
        snprintf(amtName, sizeof(amtName), "Mod %d Amount", i + 1);

        Steinberg::Vst::String128 srcNameW, dstNameW, amtNameW;
        Steinberg::UString(srcNameW, 128).fromAscii(srcName);
        Steinberg::UString(dstNameW, 128).fromAscii(dstName);
        Steinberg::UString(amtNameW, 128).fromAscii(amtName);

        // Source dropdown
        auto* srcParam = new StringListParameter(
            srcNameW, slotSourceIds[i], nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        for (int s = 0; s < kModSourceCount; ++s) srcParam->appendString(kModSourceStrings[s]);
        parameters.addParameter(srcParam);

        // Dest dropdown
        auto* dstParam = new StringListParameter(
            dstNameW, slotDestIds[i], nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        for (int d = 0; d < kModDestCount; ++d) dstParam->appendString(kModDestStrings[d]);
        parameters.addParameter(dstParam);

        // Amount (bipolar)
        parameters.addParameter(amtNameW, STR16("%"), 0, 0.5,
            ParameterInfo::kCanAutomate, slotAmountIds[i]);
    }
}

inline Steinberg::tresult formatModMatrixParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    if (id < kModMatrixBaseId || id > kModMatrixSlot7AmountId) return kResultFalse;
    int offset = static_cast<int>(id - kModMatrixBaseId);
    int subParam = offset % 3;
    if (subParam == 2) { // Amount
        char8 text[32];
        snprintf(text, sizeof(text), "%+.0f%%", (value * 2.0 - 1.0) * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse; // Source/Dest handled by StringListParameter
}

inline void saveModMatrixParams(const ModMatrixParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < 8; ++i) {
        streamer.writeInt32(params.slots[static_cast<size_t>(i)].source.load(std::memory_order_relaxed));
        streamer.writeInt32(params.slots[static_cast<size_t>(i)].dest.load(std::memory_order_relaxed));
        streamer.writeFloat(params.slots[static_cast<size_t>(i)].amount.load(std::memory_order_relaxed));
    }
}

inline bool loadModMatrixParams(ModMatrixParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    for (int i = 0; i < 8; ++i) {
        if (!streamer.readInt32(iv)) return false;
        params.slots[static_cast<size_t>(i)].source.store(iv, std::memory_order_relaxed);
        if (!streamer.readInt32(iv)) return false;
        params.slots[static_cast<size_t>(i)].dest.store(iv, std::memory_order_relaxed);
        if (!streamer.readFloat(fv)) return false;
        params.slots[static_cast<size_t>(i)].amount.store(fv, std::memory_order_relaxed);
    }
    return true;
}

template<typename SetParamFunc>
inline void loadModMatrixParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    const Steinberg::Vst::ParamID srcIds[] = {
        kModMatrixSlot0SourceId, kModMatrixSlot1SourceId, kModMatrixSlot2SourceId,
        kModMatrixSlot3SourceId, kModMatrixSlot4SourceId, kModMatrixSlot5SourceId,
        kModMatrixSlot6SourceId, kModMatrixSlot7SourceId,
    };
    const Steinberg::Vst::ParamID dstIds[] = {
        kModMatrixSlot0DestId, kModMatrixSlot1DestId, kModMatrixSlot2DestId,
        kModMatrixSlot3DestId, kModMatrixSlot4DestId, kModMatrixSlot5DestId,
        kModMatrixSlot6DestId, kModMatrixSlot7DestId,
    };
    const Steinberg::Vst::ParamID amtIds[] = {
        kModMatrixSlot0AmountId, kModMatrixSlot1AmountId, kModMatrixSlot2AmountId,
        kModMatrixSlot3AmountId, kModMatrixSlot4AmountId, kModMatrixSlot5AmountId,
        kModMatrixSlot6AmountId, kModMatrixSlot7AmountId,
    };
    for (int i = 0; i < 8; ++i) {
        if (streamer.readInt32(iv))
            setParam(srcIds[i], static_cast<double>(iv) / (kModSourceCount - 1));
        if (streamer.readInt32(iv))
            setParam(dstIds[i], static_cast<double>(iv) / (kModDestCount - 1));
        if (streamer.readFloat(fv))
            setParam(amtIds[i], static_cast<double>((fv + 1.0f) / 2.0f));
    }
}

} // namespace Ruinae
