#pragma once

// ==============================================================================
// Vorago Phase 12 - Sub parameter pack (ID 600-699)   T020, FR-011/012/013
// ==============================================================================
// The pack contract of global_params.h (plan section 3.4), five continuous fields:
//   600 Sub Level Offset       dB  [-24, 24]  default   0  (plan D-P3; the engine
//                                                          setter has no clamp)
//   601 Sub Tracking           %   [0, 1]     default   1
//   610 Sub Div2 Level         dB  [-60, 6]   default -18
//   611 Sub Div4 Level         dB  [-60, 6]   default -24
//   612 Sub Fifth-Below Level  dB  [-60, 6]   default -30
// All linear taper. Atomics hold PLAIN units.
//
// Stream: 5 floats in ascending ID order = 20 bytes (plan section 4.9).
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>

namespace Vorago {

struct SubParams {
    std::atomic<float> levelOffsetDb{0.0f};       ///< 600, dB [-24, 24]
    std::atomic<float> tracking{1.0f};            ///< 601, [0, 1]
    std::atomic<float> div2LevelDb{-18.0f};       ///< 610, dB [-60, 6]
    std::atomic<float> div4LevelDb{-24.0f};       ///< 611, dB [-60, 6]
    std::atomic<float> fifthBelowLevelDb{-30.0f}; ///< 612, dB [-60, 6]
};

namespace detail {

struct SubRange {
    Steinberg::Vst::ParamID id;
    double mn;
    double mx;
};

inline constexpr int kNumSubParams = 5;

/// Ascending ID order == stream order.
inline constexpr std::array<SubRange, kNumSubParams> kSubRanges{{
    {kSubLevelOffsetId, -24.0, 24.0},
    {kSubTrackingId, 0.0, 1.0},
    {kSubDiv2LevelId, -60.0, 6.0},
    {kSubDiv4LevelId, -60.0, 6.0},
    {kSubFifthBelowLevelId, -60.0, 6.0},
}};

/// Index into kSubRanges for `id`, or -1 for any other ID.
[[nodiscard]] constexpr int subIndexOf(Steinberg::Vst::ParamID id) noexcept {
    for (int i = 0; i < kNumSubParams; ++i) {
        if (kSubRanges[static_cast<std::size_t>(i)].id == id)
            return i;
    }
    return -1;
}

[[nodiscard]] inline std::atomic<float>& subField(SubParams& p, int i) noexcept {
    switch (i) {
        case 0: return p.levelOffsetDb;
        case 1: return p.tracking;
        case 2: return p.div2LevelDb;
        case 3: return p.div4LevelDb;
        default: return p.fifthBelowLevelDb;
    }
}

[[nodiscard]] inline const std::atomic<float>& subField(const SubParams& p, int i) noexcept {
    switch (i) {
        case 0: return p.levelOffsetDb;
        case 1: return p.tracking;
        case 2: return p.div2LevelDb;
        case 3: return p.div4LevelDb;
        default: return p.fifthBelowLevelDb;
    }
}

}  // namespace detail

// ==============================================================================
// Parameter Change Handler (caller has rejected non-finite and clamped to [0, 1])
// ==============================================================================

inline void handleSubParamChange(SubParams& params, Steinberg::Vst::ParamID id,
                                 Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kSubLevelOffsetId:
        case kSubTrackingId:
        case kSubDiv2LevelId:
        case kSubDiv4LevelId:
        case kSubFifthBelowLevelId: {
            const int i = detail::subIndexOf(id);
            const auto& r = detail::kSubRanges[static_cast<std::size_t>(i)];
            detail::subField(params, i).store(
                static_cast<float>(linearFromNormalized(value, r.mn, r.mx)),
                std::memory_order_relaxed);
            break;
        }
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerSubParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // n0 = linear inverse of the default: 0.5, 1.0, 42/66, 36/66, 30/66.
    parameters.addParameter(STR16("Sub Level Offset"), STR16("dB"), 0,
                            linearToNormalized(0.0, -24.0, 24.0), ParameterInfo::kCanAutomate,
                            kSubLevelOffsetId);
    parameters.addParameter(STR16("Sub Tracking"), STR16("%"), 0,
                            linearToNormalized(1.0, 0.0, 1.0), ParameterInfo::kCanAutomate,
                            kSubTrackingId);
    parameters.addParameter(STR16("Sub Div2 Level"), STR16("dB"), 0,
                            linearToNormalized(-18.0, -60.0, 6.0), ParameterInfo::kCanAutomate,
                            kSubDiv2LevelId);
    parameters.addParameter(STR16("Sub Div4 Level"), STR16("dB"), 0,
                            linearToNormalized(-24.0, -60.0, 6.0), ParameterInfo::kCanAutomate,
                            kSubDiv4LevelId);
    parameters.addParameter(STR16("Sub Fifth-Below Level"), STR16("dB"), 0,
                            linearToNormalized(-30.0, -60.0, 6.0), ParameterInfo::kCanAutomate,
                            kSubFifthBelowLevelId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatSubParam(Steinberg::Vst::ParamID id,
                                         Steinberg::Vst::ParamValue value,
                                         Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    const int i = detail::subIndexOf(id);
    if (i < 0) { return kResultFalse; }
    const auto& r = detail::kSubRanges[static_cast<std::size_t>(i)];
    const double plain = linearFromNormalized(value, r.mn, r.mx);

    char8 text[32];
    if (id == kSubTrackingId) {
        snprintf(text, sizeof(text), "%.0f%%", plain * 100.0);
    } else {
        snprintf(text, sizeof(text), "%.1f dB", plain);
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 20 bytes (five floats, ascending ID)
// ==============================================================================

inline void saveSubParams(const SubParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < detail::kNumSubParams; ++i) {
        streamer.writeFloat(detail::subField(params, i).load(std::memory_order_relaxed));
    }
}

/// EOF-safe: stops at the first failed read and returns false, leaving that field and
/// every later one at its CURRENT value. A non-finite float is skipped (field
/// unchanged); finite values are clamped to the plain range.
inline bool loadSubParams(SubParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < detail::kNumSubParams; ++i) {
        float fv = 0.0f;
        if (!streamer.readFloat(fv)) { return false; }
        if (Krate::DSP::detail::isFinite(fv)) {  // fast-math-immune, db_utils.h:118
            const auto& r = detail::kSubRanges[static_cast<std::size_t>(i)];
            detail::subField(params, i).store(
                std::clamp(fv, static_cast<float>(r.mn), static_cast<float>(r.mx)),
                std::memory_order_relaxed);
        }
    }
    return true;
}

// ==============================================================================
// Controller State Sync (inverts the linear mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadSubParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    for (int i = 0; i < detail::kNumSubParams; ++i) {
        float fv = 0.0f;
        if (!streamer.readFloat(fv)) { return; }
        if (Krate::DSP::detail::isFinite(fv)) {  // same rule as loadSubParams
            const auto& r = detail::kSubRanges[static_cast<std::size_t>(i)];
            setParam(r.id, linearToNormalized(static_cast<double>(fv), r.mn, r.mx));
        }
    }
}

}  // namespace Vorago
