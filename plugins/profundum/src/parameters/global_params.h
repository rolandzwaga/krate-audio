#pragma once

// ==============================================================================
// Profundum - Global Parameters (ID 0-99)   FR-040, FR-043, FR-045, FR-047
// ==============================================================================
// The six-function pack contract of plugins/vorago/src/parameters/global_params.h.
// Plan section 2.3.
//
//   0 Output Gain     linear [0, 1], -60 dB taper, 0 == -inf (plan 3.1)
//   1 Voice Mode      L(2) Mono / Poly                default Mono
//   2 Polyphony       L(8) "1".."8"                   default 4
//   3 Output Ceiling  dBTP [-12, 0]                   default -1
//
// Stream: float outputGain + int32 voiceMode + int32 polyphony + float
// outputCeilingDb = 16 bytes (plan section 3.4).
//
// Defaults are derived from the engine's constants (plan P-8, static_asserts
// below) so the per-block push changes nothing at the registered defaults.
// ==============================================================================

#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h (FR-047)

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Profundum {

using Engine = Krate::DSP::ProfundumEngine;

inline constexpr double kOutputGainFloorDb = -60.0;  // Q1
inline constexpr int kNumPolyphonyEntries = 8;       // "1".."8"
static_assert(kNumPolyphonyEntries == static_cast<int>(Engine::kMaxVoices),
              "FR-043: one Polyphony entry per engine voice (profundum_engine.h:99)");
static_assert(Engine::kMinCeilingDb == -12.0f && Engine::kMaxCeilingDb == 0.0f,
              "the ceiling mapping assumes [-12, 0] dBTP (profundum_engine.h:108-109)");

// Defaults DERIVED from the engine (P-8), so the per-block push is a no-op on a default engine.
inline constexpr float kDefaultOutputGain = 1.0f;
inline constexpr int kDefaultVoiceModeIndex = 0;                                       // Mono
inline constexpr int kDefaultPolyphony = static_cast<int>(Engine::kDefaultPolyphony);  // 4 (engine.h:100)
inline constexpr float kDefaultCeilingDb = Engine::kDefaultCeilingDb;                  // -1 (engine.h:110)

struct GlobalParams {
    std::atomic<float> outputGain{kDefaultOutputGain};      ///< linear [0, 1] (FR-028)
    std::atomic<int> voiceMode{kDefaultVoiceModeIndex};     ///< 0 Mono, 1 Poly
    std::atomic<int> polyphony{kDefaultPolyphony};          ///< [1, 8]
    std::atomic<float> outputCeilingDb{kDefaultCeilingDb};  ///< [-12, 0] dBTP
};

// ---- the ONE place each mapping is written (processor, controller and tests all call these) ----

/// Plan 3.1. v already finite and in [0, 1]. Exactly 0 (-inf dB) at v <= 0 and
/// exactly 1 (never 1 + ulp) at v >= 1; the -60 dB taper in between, computed in
/// double and rounded to float once.
[[nodiscard]] inline float outputGainFromNormalized(double v) noexcept {
    if (v <= 0.0) {
        return 0.0f;
    }
    if (v >= 1.0) {
        return 1.0f;
    }
    return static_cast<float>(std::pow(10.0, (kOutputGainFloorDb + 60.0 * v) / 20.0));
}

/// Plan 3.1 inverse (FR-045). g already finite and in [0, 1].
[[nodiscard]] inline double outputGainToNormalized(float g) noexcept {
    if (!(g > 0.0f)) {
        return 0.0;
    }
    return std::clamp(1.0 + 20.0 * std::log10(static_cast<double>(g)) / 60.0, 0.0, 1.0);
}

/// FR-043: the eight list entries "1".."8", round to nearest.
[[nodiscard]] inline int polyphonyFromNormalized(double v) noexcept {
    return std::clamp(static_cast<int>(v * 7.0 + 1.0 + 0.5), 1, kNumPolyphonyEntries);
}

[[nodiscard]] inline int clampPolyphony(Steinberg::int32 n) noexcept {
    return std::clamp(static_cast<int>(n), 1, kNumPolyphonyEntries);
}

/// [-12, 0] dBTP, linear in v (profundum_engine.h:108-109).
[[nodiscard]] inline float ceilingFromNormalized(double v) noexcept {
    return static_cast<float>(static_cast<double>(Engine::kMinCeilingDb) +
                              static_cast<double>(Engine::kMaxCeilingDb - Engine::kMinCeilingDb) * v);
}

/// -12 maps to exactly 0 and 0 to exactly 1: under the macOS SDK's -ffast-math, clang contracted the division into
/// fma(db, 1 / 12, 1), and -12 read back as 5.6e-17 (macOS arm64 CI, 2026-10-25).
[[nodiscard]] inline double ceilingToNormalized(float db) noexcept {
    if (!(db > Engine::kMinCeilingDb)) {
        return 0.0;
    }
    if (db >= Engine::kMaxCeilingDb) {
        return 1.0;
    }
    return (static_cast<double>(db) + 12.0) / 12.0;
}

// ==============================================================================
// Pack functions (T009: handleGlobalParamChange; T010: register / format;
// T011: save / load; T012: the controller mirror)
// ==============================================================================

/// Plan 2.3 table. value arrives already finite and clamped to [0, 1] (plan
/// 2.5.8). IDs 4-99 store nothing (FR-042). Every store is relaxed.
inline void handleGlobalParamChange(GlobalParams& params, Steinberg::Vst::ParamID id,
                                    Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kOutputGainId:
            params.outputGain.store(outputGainFromNormalized(value), std::memory_order_relaxed);
            break;
        case kVoiceModeId:
            params.voiceMode.store(value >= 0.5 ? 1 : 0, std::memory_order_relaxed);
            break;
        case kPolyphonyId:
            params.polyphony.store(polyphonyFromNormalized(value), std::memory_order_relaxed);
            break;
        case kOutputCeilingId:
            params.outputCeilingDb.store(ceilingFromNormalized(value), std::memory_order_relaxed);
            break;
        default:
            break;  // FR-042: unregistered global-band ID, ignored
    }
}

/// FR-047 (plan 2.3). The dropdown helper sets only the CURRENT value
/// (plugins/shared/src/ui/parameter_helpers.h:47-69), so each list's REGISTERED
/// default is pinned by hand.
inline void registerGlobalParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Output Gain"), STR16("dB"), 0, 1.0,
                            ParameterInfo::kCanAutomate, kOutputGainId);

    auto* voiceMode = Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Voice Mode"), kVoiceModeId, kDefaultVoiceModeIndex, {STR16("Mono"), STR16("Poly")});
    voiceMode->getInfo().defaultNormalizedValue = 0.0;
    parameters.addParameter(voiceMode);

    auto* polyphony = Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Polyphony"), kPolyphonyId, kDefaultPolyphony - 1,
        {STR16("1"), STR16("2"), STR16("3"), STR16("4"), STR16("5"), STR16("6"), STR16("7"),
         STR16("8")});
    polyphony->getInfo().defaultNormalizedValue = 3.0 / 7.0;
    parameters.addParameter(polyphony);

    parameters.addParameter(STR16("Output Ceiling"), STR16("dBTP"), 0, 11.0 / 12.0,
                            ParameterInfo::kCanAutomate, kOutputCeilingId);
}

/// Plan 2.3. The two StringListParameters format themselves (kResultFalse).
inline Steinberg::tresult formatGlobalParam(Steinberg::Vst::ParamID id,
                                            Steinberg::Vst::ParamValue value,
                                            Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    if (id == kOutputGainId) {
        if (value <= 0.0) {
            UString(string, 128).fromAscii("-inf dB");
            return kResultOk;
        }
        char8 text[32];
        snprintf(text, sizeof(text), "%.1f dB", kOutputGainFloorDb + 60.0 * value);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    if (id == kOutputCeilingId) {
        char8 text[32];
        snprintf(text, sizeof(text), "%.1f dBTP", static_cast<double>(ceilingFromNormalized(value)));
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

/// Plan 2.3 / 3.4: float outputGain, int32 voiceMode, int32 polyphony, float
/// outputCeilingDb (16 bytes, little-endian through the caller's IBStreamer).
inline void saveGlobalParams(const GlobalParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.outputGain.load(std::memory_order_relaxed));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.voiceMode.load(std::memory_order_relaxed)));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.polyphony.load(std::memory_order_relaxed)));
    streamer.writeFloat(params.outputCeilingDb.load(std::memory_order_relaxed));
}

/// EOF-safe; false at the first failed read, leaving every later field unchanged.
/// FR-045 sanitising (plan 2.3): a non-finite float keeps its atomic (fast-math
/// immune, db_utils.h:118); gain clamps to [0, 1]; voiceMode is stored only if it
/// is 0 or 1; polyphony clamps to [1, 8]; the ceiling clamps to [-12, 0].
inline bool loadGlobalParams(GlobalParams& params, Steinberg::IBStreamer& streamer) {
    float gain = 0.0f;
    if (!streamer.readFloat(gain)) {
        return false;
    }
    if (Krate::DSP::detail::isFinite(gain)) {
        params.outputGain.store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    Steinberg::int32 mode = 0;
    if (!streamer.readInt32(mode)) {
        return false;
    }
    if (mode == 0 || mode == 1) {
        params.voiceMode.store(static_cast<int>(mode), std::memory_order_relaxed);
    }

    Steinberg::int32 poly = 0;
    if (!streamer.readInt32(poly)) {
        return false;
    }
    params.polyphony.store(clampPolyphony(poly), std::memory_order_relaxed);

    float ceilingDb = 0.0f;
    if (!streamer.readFloat(ceilingDb)) {
        return false;
    }
    if (Krate::DSP::detail::isFinite(ceilingDb)) {
        params.outputCeilingDb.store(
            std::clamp(ceilingDb, Engine::kMinCeilingDb, Engine::kMaxCeilingDb),
            std::memory_order_relaxed);
    }
    return true;
}

/// FR-045 / FR-046 (plan 2.3): the loadGlobalParams rules applied BEFORE the
/// field is normalised, then setParam(id, normalized). EOF-safe; returns at the
/// first failed read. A non-finite float or an invalid voiceMode makes NO call, so
/// the parameter keeps its value (Parameter::setNormalized would pass a NaN).
template <typename SetParamFunc>
inline void loadGlobalParamsToController(Steinberg::IBStreamer& streamer,
                                         SetParamFunc setParam) {
    float gain = 0.0f;
    if (!streamer.readFloat(gain)) {
        return;
    }
    if (Krate::DSP::detail::isFinite(gain)) {
        setParam(kOutputGainId, outputGainToNormalized(std::clamp(gain, 0.0f, 1.0f)));
    }

    Steinberg::int32 mode = 0;
    if (!streamer.readInt32(mode)) {
        return;
    }
    if (mode == 0 || mode == 1) {
        setParam(kVoiceModeId, static_cast<double>(mode));
    }

    Steinberg::int32 poly = 0;
    if (!streamer.readInt32(poly)) {
        return;
    }
    setParam(kPolyphonyId, static_cast<double>(clampPolyphony(poly) - 1) / 7.0);

    float ceilingDb = 0.0f;
    if (!streamer.readFloat(ceilingDb)) {
        return;
    }
    if (Krate::DSP::detail::isFinite(ceilingDb)) {
        setParam(kOutputCeilingId,
                 ceilingToNormalized(
                     std::clamp(ceilingDb, Engine::kMinCeilingDb, Engine::kMaxCeilingDb)));
    }
}

}  // namespace Profundum
