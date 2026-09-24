#pragma once

// ==============================================================================
// Vorago - Global Parameters (ID 0-99)   FR-040, FR-043, FR-044, FR-048
// ==============================================================================
// The six-function pack contract of plugins/seraphis/src/parameters/global_params.h,
// reduced to two fields (master gain, polyphony). Plan section 2.3.
//
// Stream: float masterGain + int32 polyphony = 8 bytes (plan section 3.4).
// ==============================================================================

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
    // kPolyphonyId is a StringListParameter and formats itself.
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

}  // namespace Vorago
