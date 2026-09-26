#pragma once

// ==============================================================================
// Vorago - Global Parameters (ID 0-99)   FR-040, FR-043, FR-044, FR-048
// ==============================================================================
// The six-function pack contract of plugins/seraphis/src/parameters/global_params.h,
// reduced to two fields (master gain, polyphony). Plan section 2.3.
//
// Stream: float masterGain + int32 polyphony = 8 bytes (plan section 3.4).
//
// Phase 12 extension (T030, plan sections 3.2 / 4.9):
//   2 Seed               L(16) "Seed 1".."Seed 16"   default 0     ENG
//   3 Output Saturation  %  [0, 1]  0.12  lin                      MB
//   4 Sustain Pedal      [0, 1], >= 0.5 = down, kIsHidden, NOT persisted
//   5 Channel Pressure   %  [0, 1]  0, kIsHidden, NOT persisted
// The v1 8-byte block above is untouched; the v2 extension block is
// int32 seedIndex + float outputSaturation = 8 bytes (saveGlobalParamsV2Ext).
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h (FR-048)

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/vorago_engine.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace Vorago {

struct GlobalParams {
    std::atomic<float> masterGain{1.0f};  ///< linear [0, 2]; normalized 0.5 == unity
    std::atomic<int> polyphony{4};        ///< [1, 6]; == VoragoEngine::kDefaultPolyphony
    std::atomic<int> seedIndex{0};               ///< index 0-15 into kVoragoSeedValues
    std::atomic<float> outputSaturation{0.12f};  ///< [0, 1]
    std::atomic<float> sustainPedal{0.0f};       ///< [0, 1]; >= 0.5 = down; never persisted
    std::atomic<float> channelPressure{0.0f};    ///< [0, 1]; never persisted
};

/// The ONE conversion into the engine's polyphony domain (Seraphis global_params.h
/// clampPolyphony rationale): keeps the stored value and engine_->getPolyphony()
/// (clamped, vorago_engine.h:507-508) in the same domain, so the processor's change
/// detector converges after one push even on a corrupt stream.
[[nodiscard]] inline std::size_t clampPolyphony(int raw) noexcept {
    return std::clamp(static_cast<std::size_t>(std::max(raw, 1)), std::size_t{1},
                      Krate::DSP::VoragoEngine::kMaxVoices);  // == 6
}

// ==============================================================================
// Parameter Change Handler (FR-043 / FR-044)
// ==============================================================================

inline void handleGlobalParamChange(GlobalParams& params, Steinberg::Vst::ParamID id,
                                    Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kMasterGainId:
            // 0-1 normalized -> 0-2 linear gain
            params.masterGain.store(std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f),
                                    std::memory_order_relaxed);
            break;
        case kPolyphonyId:
            // 0-1 normalized -> 1-6 (index 0-5 rounded, +1)
            params.polyphony.store(std::clamp(static_cast<int>(value * 5.0 + 1.0 + 0.5), 1, 6),
                                   std::memory_order_relaxed);
            break;
        case kSeedId:
            params.seedIndex.store(indexFromNormalized(value, kNumSeeds),
                                   std::memory_order_relaxed);
            break;
        case kOutputSaturationId:
            params.outputSaturation.store(
                static_cast<float>(linearFromNormalized(value, 0.0, 1.0)),
                std::memory_order_relaxed);
            break;
        case kSustainPedalId:
            params.sustainPedal.store(static_cast<float>(linearFromNormalized(value, 0.0, 1.0)),
                                      std::memory_order_relaxed);
            break;
        case kChannelPressureId:
            params.channelPressure.store(
                static_cast<float>(linearFromNormalized(value, 0.0, 1.0)),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration (FR-048 - THE REGISTERED TYPES ARE FROZEN)
// ==============================================================================

inline void registerGlobalParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // Master Gain (0-200 %, default 100 % = normalized 0.5) -> plain Vst::Parameter
    parameters.addParameter(STR16("Master Gain"), STR16("dB"), 0, 0.5,
                            ParameterInfo::kCanAutomate, kMasterGainId);

    // Polyphony (1-6, default 4 => index 3) -> Vst::StringListParameter
    auto* poly = Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Polyphony"), kPolyphonyId, /*defaultIndex=*/3,
        {STR16("1"), STR16("2"), STR16("3"), STR16("4"), STR16("5"), STR16("6")});
    // P-1: the helper sets only the CURRENT value (parameter_helpers.h:64-66); the
    // REGISTERED default must be pinned here or a host "reset to default" yields one voice.
    poly->getInfo().defaultNormalizedValue = 3.0 / 5.0;
    parameters.addParameter(poly);

    // Seed (16 curated seeds, default index 0 == kEngineSeed) -> StringListParameter
    auto* seed = Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Seed"), kSeedId, /*defaultIndex=*/0,
        {STR16("Seed 1"), STR16("Seed 2"), STR16("Seed 3"), STR16("Seed 4"),
         STR16("Seed 5"), STR16("Seed 6"), STR16("Seed 7"), STR16("Seed 8"),
         STR16("Seed 9"), STR16("Seed 10"), STR16("Seed 11"), STR16("Seed 12"),
         STR16("Seed 13"), STR16("Seed 14"), STR16("Seed 15"), STR16("Seed 16")});
    seed->getInfo().defaultNormalizedValue = 0.0;  // P-1: pin the REGISTERED default
    parameters.addParameter(seed);

    parameters.addParameter(STR16("Output Saturation"), STR16("%"), 0, 0.12,
                            ParameterInfo::kCanAutomate, kOutputSaturationId);

    // Performance controllers (CC64 / channel aftertouch via IMidiMapping): hidden
    // from the host's generic UI and never persisted in state (FR-045).
    parameters.addParameter(STR16("Sustain Pedal"), STR16(""), 0, 0.0,
                            ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden,
                            kSustainPedalId);
    parameters.addParameter(STR16("Channel Pressure"), STR16("%"), 0, 0.0,
                            ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden,
                            kChannelPressureId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatGlobalParam(Steinberg::Vst::ParamID id,
                                            Steinberg::Vst::ParamValue value,
                                            Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    if (id == kMasterGainId) {
        const float gain = static_cast<float>(value * 2.0);
        const float dB = (gain > 0.0001f) ? 20.0f * std::log10(gain) : -80.0f;
        char8 text[32];
        snprintf(text, sizeof(text), "%.1f dB", static_cast<double>(dB));
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    if (id == kOutputSaturationId || id == kChannelPressureId) {
        char8 text[32];
        snprintf(text, sizeof(text), "%.0f%%", linearFromNormalized(value, 0.0, 1.0) * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    if (id == kSustainPedalId) {
        UString(string, 128).fromAscii(value >= 0.5 ? "Down" : "Up");
        return kResultOk;
    }
    // kPolyphonyId and kSeedId are StringListParameters and format themselves.
    return kResultFalse;
}

// ==============================================================================
// State Persistence - 8 bytes (float + int32)   FR-045 / FR-046
// ==============================================================================

inline void saveGlobalParams(const GlobalParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.masterGain.load(std::memory_order_relaxed));
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.polyphony.load(std::memory_order_relaxed)));
}

/// EOF-safe: a failed read returns false and leaves every later field at its CURRENT
/// value. Non-finite gain is rejected (field unchanged); in-range values are clamped
/// so a corrupt stream can neither poison the gain multiply nor make the polyphony
/// change detector fire every block.
inline bool loadGlobalParams(GlobalParams& params, Steinberg::IBStreamer& streamer) {
    float g = 1.0f;
    Steinberg::int32 n = 0;

    if (!streamer.readFloat(g)) { return false; }
    if (Krate::DSP::detail::isFinite(g)) {  // fast-math-immune, db_utils.h:118
        params.masterGain.store(std::clamp(g, 0.0f, 2.0f), std::memory_order_relaxed);
    }

    if (!streamer.readInt32(n)) { return false; }
    params.polyphony.store(static_cast<int>(clampPolyphony(n)), std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// State Persistence - v2 extension, 8 bytes (int32 seedIndex + float
// outputSaturation)   FR-045. Sustain pedal and channel pressure are NEVER written.
// ==============================================================================

inline void saveGlobalParamsV2Ext(const GlobalParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.seedIndex.load(std::memory_order_relaxed)));
    streamer.writeFloat(params.outputSaturation.load(std::memory_order_relaxed));
}

/// EOF-safe: stops at the first failed read (returns false; later fields keep their
/// CURRENT values). The seed index is clamped to [0, 15]; a non-finite saturation
/// leaves its field unchanged, a finite one is clamped to [0, 1].
inline bool loadGlobalParamsV2Ext(GlobalParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 i = 0;
    float f = 0.0f;

    if (!streamer.readInt32(i)) { return false; }
    params.seedIndex.store(std::clamp(static_cast<int>(i), 0, kNumSeeds - 1),
                           std::memory_order_relaxed);

    if (!streamer.readFloat(f)) { return false; }
    if (Krate::DSP::detail::isFinite(f)) {
        params.outputSaturation.store(std::clamp(f, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    return true;
}

// ==============================================================================
// Controller State Sync (FR-047 - inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadGlobalParamsToController(Steinberg::IBStreamer& streamer,
                                         SetParamFunc setParam) {
    float g = 0.0f;
    Steinberg::int32 n = 0;

    if (streamer.readFloat(g)) {
        // Same finite/clamp rule as loadGlobalParams, so controller and processor agree.
        if (Krate::DSP::detail::isFinite(g)) {
            setParam(kMasterGainId, static_cast<double>(std::clamp(g, 0.0f, 2.0f)) / 2.0);
        }
    } else {
        return;
    }
    if (streamer.readInt32(n)) {
        setParam(kPolyphonyId, (static_cast<double>(clampPolyphony(n)) - 1.0) / 5.0);
    }
}

/// Mirrors loadGlobalParamsV2Ext (same read order and rules).
template <typename SetParamFunc>
inline void loadGlobalParamsV2ExtToController(Steinberg::IBStreamer& streamer,
                                              SetParamFunc setParam) {
    Steinberg::int32 i = 0;
    float f = 0.0f;

    if (!streamer.readInt32(i)) { return; }
    setParam(kSeedId, indexToNormalized(static_cast<int>(i), kNumSeeds));

    if (!streamer.readFloat(f)) { return; }
    if (Krate::DSP::detail::isFinite(f)) {
        setParam(kOutputSaturationId, linearToNormalized(static_cast<double>(f), 0.0, 1.0));
    }
}

}  // namespace Vorago
