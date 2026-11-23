#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/state_load_helpers.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include "ui/mod_matrix_types.h"
#include <krate/dsp/systems/voice_mod_types.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Ruinae {

// ==============================================================================
// ModMatrixSlot - Per-slot atomic storage for processor-side
// ==============================================================================
// Base params (1300-1323): Source, Dest, Amount
// Detail params (1324-1355): Curve, Smooth, Scale, Bypass (spec 049)

struct ModMatrixSlot {
    std::atomic<int> source{0};       // ModSource enum (0..kNumGlobalSources, 0 = None)
    std::atomic<int> dest{0};         // RuinaeModDest index (0..kNumGlobalDestinations-1)
    std::atomic<float> amount{0.0f};  // -1 to +1
    std::atomic<int> curve{0};        // Krate::DSP::ModCurve: 0=Linear, 1=Exponential, 2=S-Curve, 3=Stepped
    std::atomic<float> smoothMs{0.0f};// 0-100ms
    std::atomic<int> scale{2};        // 0=x0.25, 1=x0.5, 2=x1, 3=x2, 4=x4
    std::atomic<int> bypass{0};       // 0 or 1
};

struct ModMatrixParams {
    std::array<ModMatrixSlot, 8> slots;
};

// ==============================================================================
// Curve/Scale constants for parameter registration
// ==============================================================================

inline constexpr int kModCurveCount = 4;
inline constexpr int kModScaleCount = 5;

inline const Steinberg::Vst::TChar* const kModCurveStrings[] = {
    STR16("Linear"),
    STR16("Exponential"),
    STR16("S-Curve"),
    STR16("Stepped"),
};

inline const Steinberg::Vst::TChar* const kModScaleStrings[] = {
    STR16("x0.25"),
    STR16("x0.5"),
    STR16("x1"),
    STR16("x2"),
    STR16("x4"),
};

/// Multiplier of each Scale entry. It multiplies a route's contribution after the
/// amount (global and voice routes), so x2 / x4 extend the depth (R-053).
inline constexpr std::array<float, kModScaleCount> kModScaleMultipliers = {
    0.25f, 0.5f, 1.0f, 2.0f, 4.0f};

[[nodiscard]] inline float modScaleMultiplier(int scaleIdx) {
    return kModScaleMultipliers[static_cast<size_t>(std::clamp(scaleIdx, 0, kModScaleCount - 1))];
}

/// Full scale of a voice route to Filter Resonance, in Q. The All-Voice Resonance
/// global route uses the same 10 Q (R-044); before state v9 the voice route used 1 Q.
inline constexpr float kVoiceRouteResonanceQ = 10.0f;

/// Before state v9 a route's amount was multiplied by its Scale and the product
/// clamped to +/-1, so x2 / x4 saturated. Returns the amount that gives an older
/// route the depth it was saved with now that the Scale multiplies after the amount.
[[nodiscard]] inline float preV9RouteAmount(float amount, int scaleIdx) {
    const float mul = modScaleMultiplier(scaleIdx);
    if (std::abs(amount * mul) <= 1.0f) return amount;
    return std::clamp(amount * mul, -1.0f, 1.0f) / mul;
}

/// preV9RouteAmount for a voice route; a route to Filter Resonance (destination 1)
/// also keeps its old 1 Q full scale.
[[nodiscard]] inline float preV9VoiceRouteAmount(float amount, int destination, int scaleIdx) {
    const float a = preV9RouteAmount(amount, scaleIdx);
    return destination == static_cast<int>(Krate::DSP::VoiceModDest::FilterResonance)
        ? a / kVoiceRouteResonanceQ : a;
}

/// Applies preV9RouteAmount to every slot (a state older than v9 was just loaded).
inline void migratePreV9ModMatrixAmounts(ModMatrixParams& params) {
    for (auto& slot : params.slots) {
        slot.amount.store(preV9RouteAmount(slot.amount.load(std::memory_order_relaxed),
                                           slot.scale.load(std::memory_order_relaxed)),
                          std::memory_order_relaxed);
    }
}

// ==============================================================================
// handleModMatrixParamChange - Process base + detail param changes
// ==============================================================================

inline void handleModMatrixParamChange(
    ModMatrixParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    // --- Base parameters (1300-1323): Source, Dest, Amount ---
    if (id >= kModMatrixBaseId && id <= kModMatrixSlot7AmountId) {
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
            default:
                break;
        }
        return;
    }

    // --- Detail parameters (1324-1355): Curve, Smooth, Scale, Bypass ---
    if (id >= kModMatrixDetailBaseId && id <= kModMatrixSlot7BypassId) {
        int offset = static_cast<int>(id - kModMatrixDetailBaseId);
        int slotIdx = offset / 4;
        int subParam = offset % 4;
        if (slotIdx < 0 || slotIdx >= 8) return;

        auto& slot = params.slots[static_cast<size_t>(slotIdx)];
        switch (subParam) {
            case 0: // Curve (0-3)
                slot.curve.store(
                    std::clamp(static_cast<int>(value * (kModCurveCount - 1) + 0.5), 0, kModCurveCount - 1),
                    std::memory_order_relaxed);
                break;
            case 1: // Smooth (0-100ms)
                slot.smoothMs.store(
                    std::clamp(static_cast<float>(value * 100.0), 0.0f, 100.0f),
                    std::memory_order_relaxed);
                break;
            case 2: // Scale (0-4)
                slot.scale.store(
                    std::clamp(static_cast<int>(value * (kModScaleCount - 1) + 0.5), 0, kModScaleCount - 1),
                    std::memory_order_relaxed);
                break;
            case 3: // Bypass (0 or 1)
                slot.bypass.store(
                    value >= 0.5 ? 1 : 0,
                    std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }
}

// ==============================================================================
// registerModMatrixParams - Register all 56 parameters (base + detail)
// ==============================================================================

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

    // Detail param ID arrays (1324-1355): 4 per slot
    const Steinberg::Vst::ParamID slotCurveIds[] = {
        kModMatrixSlot0CurveId, kModMatrixSlot1CurveId, kModMatrixSlot2CurveId,
        kModMatrixSlot3CurveId, kModMatrixSlot4CurveId, kModMatrixSlot5CurveId,
        kModMatrixSlot6CurveId, kModMatrixSlot7CurveId,
    };
    const Steinberg::Vst::ParamID slotSmoothIds[] = {
        kModMatrixSlot0SmoothId, kModMatrixSlot1SmoothId, kModMatrixSlot2SmoothId,
        kModMatrixSlot3SmoothId, kModMatrixSlot4SmoothId, kModMatrixSlot5SmoothId,
        kModMatrixSlot6SmoothId, kModMatrixSlot7SmoothId,
    };
    const Steinberg::Vst::ParamID slotScaleIds[] = {
        kModMatrixSlot0ScaleId, kModMatrixSlot1ScaleId, kModMatrixSlot2ScaleId,
        kModMatrixSlot3ScaleId, kModMatrixSlot4ScaleId, kModMatrixSlot5ScaleId,
        kModMatrixSlot6ScaleId, kModMatrixSlot7ScaleId,
    };
    const Steinberg::Vst::ParamID slotBypassIds[] = {
        kModMatrixSlot0BypassId, kModMatrixSlot1BypassId, kModMatrixSlot2BypassId,
        kModMatrixSlot3BypassId, kModMatrixSlot4BypassId, kModMatrixSlot5BypassId,
        kModMatrixSlot6BypassId, kModMatrixSlot7BypassId,
    };

    for (int i = 0; i < 8; ++i) {
        // --- Base parameters (Source, Dest, Amount) ---
        char srcName[64];
        char dstName[64];
        char amtName[64];
        snprintf(srcName, sizeof(srcName), "Mod %d Source", i + 1);
        snprintf(dstName, sizeof(dstName), "Mod %d Dest", i + 1);
        snprintf(amtName, sizeof(amtName), "Mod %d Amount", i + 1);

        Steinberg::Vst::String128 srcNameW;
        Steinberg::Vst::String128 dstNameW;
        Steinberg::Vst::String128 amtNameW;
        Steinberg::UString(srcNameW, 128).fromAscii(srcName);
        Steinberg::UString(dstNameW, 128).fromAscii(dstName);
        Steinberg::UString(amtNameW, 128).fromAscii(amtName);

        // Source dropdown (T010) — names from central registry
        auto* srcParam = new StringListParameter(
            srcNameW, slotSourceIds[i], nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        appendSourceStrings(srcParam);
        parameters.addParameter(srcParam);

        // Dest dropdown (T011) — names from central registry
        auto* dstParam = new StringListParameter(
            dstNameW, slotDestIds[i], nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        appendDestStrings(dstParam);
        parameters.addParameter(dstParam);

        // Amount - bipolar (T012)
        parameters.addParameter(amtNameW, STR16("%"), 0, 0.5,
            ParameterInfo::kCanAutomate, slotAmountIds[i]);

        // --- Detail parameters (Curve, Smooth, Scale, Bypass) ---
        char curveName[64];
        char smoothName[64];
        char scaleName[64];
        char bypassName[64];
        snprintf(curveName, sizeof(curveName), "Mod %d Curve", i + 1);
        snprintf(smoothName, sizeof(smoothName), "Mod %d Smooth", i + 1);
        snprintf(scaleName, sizeof(scaleName), "Mod %d Scale", i + 1);
        snprintf(bypassName, sizeof(bypassName), "Mod %d Bypass", i + 1);

        Steinberg::Vst::String128 curveNameW;
        Steinberg::Vst::String128 smoothNameW;
        Steinberg::Vst::String128 scaleNameW;
        Steinberg::Vst::String128 bypassNameW;
        Steinberg::UString(curveNameW, 128).fromAscii(curveName);
        Steinberg::UString(smoothNameW, 128).fromAscii(smoothName);
        Steinberg::UString(scaleNameW, 128).fromAscii(scaleName);
        Steinberg::UString(bypassNameW, 128).fromAscii(bypassName);

        // Curve dropdown - 4 items (T013)
        auto* curveParam = new StringListParameter(
            curveNameW, slotCurveIds[i], nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        for (const auto* name : kModCurveStrings) curveParam->appendString(name);
        parameters.addParameter(curveParam);

        // Smooth - RangeParameter 0-100ms (T014)
        parameters.addParameter(smoothNameW, STR16("ms"), 0, 0.0,
            ParameterInfo::kCanAutomate, slotSmoothIds[i]);

        // Scale dropdown - 5 items, default=x1 (index 2) (T015)
        auto* scaleParam = new StringListParameter(
            scaleNameW, slotScaleIds[i], nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
        for (const auto* name : kModScaleStrings) scaleParam->appendString(name);
        // Start at, and register as the default, x1 (index 2)
        Krate::Plugins::applyDefaultIndex(scaleParam, 2);
        parameters.addParameter(scaleParam);

        // Bypass - boolean toggle (T016)
        parameters.addParameter(bypassNameW, nullptr, 1, 0.0,
            ParameterInfo::kCanAutomate, slotBypassIds[i]);
    }
}

// ==============================================================================
// formatModMatrixParam - Display formatting for base + detail params
// ==============================================================================

inline Steinberg::tresult formatModMatrixParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    // Base Amount params
    if (id >= kModMatrixBaseId && id <= kModMatrixSlot7AmountId) {
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

    // Detail params
    if (id >= kModMatrixDetailBaseId && id <= kModMatrixSlot7BypassId) {
        int offset = static_cast<int>(id - kModMatrixDetailBaseId);
        int subParam = offset % 4;
        if (subParam == 1) { // Smooth (ms)
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        if (subParam == 3) { // Bypass
            UString(string, 128).fromAscii(value >= 0.5 ? "On" : "Off");
            return kResultOk;
        }
        return kResultFalse; // Curve/Scale handled by StringListParameter
    }

    return kResultFalse;
}

// ==============================================================================
// State Save/Load - Base + Detail parameters (T018, T019)
// ==============================================================================

inline void saveModMatrixParams(const ModMatrixParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < 8; ++i) {
        const auto& slot = params.slots[static_cast<size_t>(i)];
        // Base
        streamer.writeInt32(slot.source.load(std::memory_order_relaxed));
        streamer.writeInt32(slot.dest.load(std::memory_order_relaxed));
        streamer.writeFloat(slot.amount.load(std::memory_order_relaxed));
        // Detail
        streamer.writeInt32(slot.curve.load(std::memory_order_relaxed));
        streamer.writeFloat(slot.smoothMs.load(std::memory_order_relaxed));
        streamer.writeInt32(slot.scale.load(std::memory_order_relaxed));
        streamer.writeInt32(slot.bypass.load(std::memory_order_relaxed));
    }
}

// Saved-field ranges, read by both parsers (R-211)
namespace ModMatrixLoad {
inline constexpr LoadedInt kSource{.lo = 0, .hi = kModSourceCount - 1};
inline constexpr LoadedInt kDest{.lo = 0, .hi = kModDestCount - 1};
inline constexpr LoadedFloat kAmount{.lo = -1.0f, .hi = 1.0f, .def = 0.0f};
inline constexpr LoadedInt kCurve{.lo = 0, .hi = kModCurveCount - 1};
inline constexpr LoadedFloat kSmoothMs{.lo = 0.0f, .hi = 100.0f, .def = 0.0f};
inline constexpr LoadedInt kScale{.lo = 0, .hi = kModScaleCount - 1};
} // namespace ModMatrixLoad

inline bool loadModMatrixParams(ModMatrixParams& params, Steinberg::IBStreamer& streamer) {
    // Every index is clamped to the range handleModMatrixParamChange produces: a
    // source past kModSourceCount wrote past ModulationEngine::sourceActive_ (R-003).
    Steinberg::int32 iv = 0; float fv = 0.0f;
    for (int i = 0; i < 8; ++i) {
        auto& slot = params.slots[static_cast<size_t>(i)];
        // Base
        if (!streamer.readInt32(iv)) return false;
        slot.source.store(ModMatrixLoad::kSource(iv), std::memory_order_relaxed);
        if (!streamer.readInt32(iv)) return false;
        slot.dest.store(ModMatrixLoad::kDest(iv), std::memory_order_relaxed);
        if (!streamer.readFloat(fv)) return false;
        slot.amount.store(ModMatrixLoad::kAmount(fv), std::memory_order_relaxed);
        // Detail
        if (!streamer.readInt32(iv)) return false;
        slot.curve.store(ModMatrixLoad::kCurve(iv), std::memory_order_relaxed);
        if (!streamer.readFloat(fv)) return false;
        slot.smoothMs.store(ModMatrixLoad::kSmoothMs(fv), std::memory_order_relaxed);
        if (!streamer.readInt32(iv)) return false;
        slot.scale.store(ModMatrixLoad::kScale(iv), std::memory_order_relaxed);
        if (!streamer.readInt32(iv)) return false;
        slot.bypass.store(iv != 0 ? 1 : 0, std::memory_order_relaxed);
    }
    return true;
}

// ==============================================================================
// Voice routes (16 slots after the Mono Mode pack, not parameters)
// ==============================================================================

// Saved-field ranges of a voice route, read by both parsers (R-211). Indices
// are the voice tab's, not the global matrix's (R-003, R-109).
namespace VoiceRouteLoad {
inline constexpr LoadedInt kSource{.lo = 0, .hi = static_cast<int>(Krate::DSP::VoiceModSource::NumSources) - 1};
inline constexpr LoadedInt kDestination{.lo = 0, .hi = Krate::Plugins::kNumVoiceDestinations - 1};
inline constexpr LoadedFloat kAmount{.lo = -1.0f, .hi = 1.0f, .def = 0.0f};
inline constexpr LoadedInt kCurve{.lo = 0, .hi = kModCurveCount - 1};
inline constexpr LoadedFloat kSmoothMs{.lo = 0.0f, .hi = 100.0f, .def = 0.0f};
inline constexpr LoadedInt kScale{.lo = 0, .hi = kModScaleCount - 1};
} // namespace VoiceRouteLoad

/// One saved voice route, sanitised (and, before state v9, migrated).
struct LoadedVoiceRoute {
    int source = 0;
    int destination = 0;
    float amount = 0.0f;
    int curve = 0;
    float smoothMs = 0.0f;
    int scale = 2;
    bool bypass = false;
    bool active = false;
};

/// Reads one voice route (int8 source, int8 destination, float amount, int8
/// curve, float smooth, int8 scale, int8 bypass, int8 active). Returns false
/// when the stream ends inside it; `out` is then unspecified.
inline bool readVoiceRoute(Steinberg::IBStreamer& streamer, Steinberg::int32 version,
                           LoadedVoiceRoute& out) {
    // The int8 indices are read as unsigned bytes, as they are written.
    const auto index = [](Steinberg::int8 v) { return static_cast<Steinberg::int32>(static_cast<uint8_t>(v)); };
    Steinberg::int8 i8 = 0;
    float f = 0.0f;
    if (!streamer.readInt8(i8)) return false;
    out.source = VoiceRouteLoad::kSource(index(i8));
    if (!streamer.readInt8(i8)) return false;
    out.destination = VoiceRouteLoad::kDestination(index(i8));
    if (!streamer.readFloat(f)) return false;
    out.amount = VoiceRouteLoad::kAmount(f);
    if (!streamer.readInt8(i8)) return false;
    out.curve = VoiceRouteLoad::kCurve(index(i8));
    if (!streamer.readFloat(f)) return false;
    out.smoothMs = VoiceRouteLoad::kSmoothMs(f);
    if (!streamer.readInt8(i8)) return false;
    out.scale = VoiceRouteLoad::kScale(index(i8));
    if (!streamer.readInt8(i8)) return false;
    out.bypass = i8 != 0;
    if (!streamer.readInt8(i8)) return false;
    out.active = i8 != 0;
    // Before v9: Scale saturated the amount and Filter Resonance spanned 1 Q
    if (version < 9) out.amount = preV9VoiceRouteAmount(out.amount, out.destination, out.scale);
    return true;
}

template<typename SetParamFunc>
inline void loadModMatrixParamsToController(
    Steinberg::IBStreamer& streamer, const SetParamFunc& setParam, Steinberg::int32 version) {
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
    const Steinberg::Vst::ParamID curveIds[] = {
        kModMatrixSlot0CurveId, kModMatrixSlot1CurveId, kModMatrixSlot2CurveId,
        kModMatrixSlot3CurveId, kModMatrixSlot4CurveId, kModMatrixSlot5CurveId,
        kModMatrixSlot6CurveId, kModMatrixSlot7CurveId,
    };
    const Steinberg::Vst::ParamID smoothIds[] = {
        kModMatrixSlot0SmoothId, kModMatrixSlot1SmoothId, kModMatrixSlot2SmoothId,
        kModMatrixSlot3SmoothId, kModMatrixSlot4SmoothId, kModMatrixSlot5SmoothId,
        kModMatrixSlot6SmoothId, kModMatrixSlot7SmoothId,
    };
    const Steinberg::Vst::ParamID scaleIds[] = {
        kModMatrixSlot0ScaleId, kModMatrixSlot1ScaleId, kModMatrixSlot2ScaleId,
        kModMatrixSlot3ScaleId, kModMatrixSlot4ScaleId, kModMatrixSlot5ScaleId,
        kModMatrixSlot6ScaleId, kModMatrixSlot7ScaleId,
    };
    const Steinberg::Vst::ParamID bypassIds[] = {
        kModMatrixSlot0BypassId, kModMatrixSlot1BypassId, kModMatrixSlot2BypassId,
        kModMatrixSlot3BypassId, kModMatrixSlot4BypassId, kModMatrixSlot5BypassId,
        kModMatrixSlot6BypassId, kModMatrixSlot7BypassId,
    };

    for (int i = 0; i < 8; ++i) {
        // Base
        if (streamer.readInt32(iv))
            setParam(srcIds[i], static_cast<double>(ModMatrixLoad::kSource(iv)) / (kModSourceCount - 1));
        if (streamer.readInt32(iv))
            setParam(dstIds[i], static_cast<double>(ModMatrixLoad::kDest(iv)) / (kModDestCount - 1));
        // The amount is set after the scale is known: states older than v9 migrate it.
        float amount = 0.0f;
        const bool hasAmount = streamer.readFloat(amount);
        amount = ModMatrixLoad::kAmount(amount);
        // Detail
        if (streamer.readInt32(iv))
            setParam(curveIds[i], static_cast<double>(ModMatrixLoad::kCurve(iv)) / (kModCurveCount - 1));
        if (streamer.readFloat(fv))
            setParam(smoothIds[i], static_cast<double>(ModMatrixLoad::kSmoothMs(fv) / 100.0f));
        if (streamer.readInt32(iv)) {
            const int scale = ModMatrixLoad::kScale(iv);
            setParam(scaleIds[i], static_cast<double>(scale) / (kModScaleCount - 1));
            if (version < 9) amount = preV9RouteAmount(amount, scale);
        }
        if (hasAmount)
            setParam(amtIds[i], static_cast<double>((amount + 1.0f) / 2.0f));
        if (streamer.readInt32(iv))
            setParam(bypassIds[i], iv != 0 ? 1.0 : 0.0);
    }
}

} // namespace Ruinae
