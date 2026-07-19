#pragma once

// ==============================================================================
// Modulation Effect Parameters (shared by Chorus, ID 1920-1929, and Flanger,
// ID 1910-1919)
// ==============================================================================
// Chorus and flanger expose the same parameter surface apart from three things:
// the maximum LFO rate, the default stereo spread, and an extra `voices`
// parameter that only chorus has. One implementation driven by a small config
// covers both; chorus_params.h and flanger_params.h bind the config and keep the
// effect-specific names their call sites already use.
//
// The `voices` asymmetry is load-bearing for preset compatibility: chorus writes
// it between stereoSpread and waveform, and flanger writes nothing there. Both
// the ID offsets and the serialization order below follow from `hasVoices`, and
// a byte-golden test pins the result.

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/note_value_ui.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// Configuration
// =============================================================================

struct ModEffectConfig {
    Steinberg::Vst::ParamID base;   ///< ParamID of the effect's Rate parameter
    float rateMaxHz;                ///< Top of the 0.05..N Hz rate range
    float defaultStereoSpread;      ///< Degrees, used for the registered default
    bool hasVoices;                 ///< Chorus only: extra voice-count parameter

    // Parameter offsets from `base`. Everything after stereoSpread shifts by one
    // when the voices parameter is present.
    [[nodiscard]] constexpr Steinberg::Vst::ParamID rateId() const { return base + 0; }
    [[nodiscard]] constexpr Steinberg::Vst::ParamID depthId() const { return base + 1; }
    [[nodiscard]] constexpr Steinberg::Vst::ParamID feedbackId() const { return base + 2; }
    [[nodiscard]] constexpr Steinberg::Vst::ParamID mixId() const { return base + 3; }
    [[nodiscard]] constexpr Steinberg::Vst::ParamID stereoSpreadId() const { return base + 4; }
    [[nodiscard]] constexpr Steinberg::Vst::ParamID voicesId() const { return base + 5; }
    [[nodiscard]] constexpr Steinberg::Vst::ParamID waveformId() const {
        return base + (hasVoices ? 6 : 5);
    }
    [[nodiscard]] constexpr Steinberg::Vst::ParamID syncId() const {
        return base + (hasVoices ? 7 : 6);
    }
    [[nodiscard]] constexpr Steinberg::Vst::ParamID noteValueId() const {
        return base + (hasVoices ? 8 : 7);
    }

    /// Normalized-to-Hz span. Rate maps 0..1 onto 0.05..rateMaxHz.
    [[nodiscard]] constexpr float rateSpan() const { return rateMaxHz - 0.05f; }
};

inline constexpr ModEffectConfig kFlangerConfig{
    .base = kFlangerRateId, .rateMaxHz = 5.0f,
    .defaultStereoSpread = 90.0f, .hasVoices = false};
inline constexpr ModEffectConfig kChorusConfig{
    .base = kChorusRateId, .rateMaxHz = 10.0f,
    .defaultStereoSpread = 180.0f, .hasVoices = true};

// The offsets above must agree with the hand-written IDs in plugin_ids.h.
static_assert(kFlangerConfig.waveformId() == kFlangerWaveformId);
static_assert(kFlangerConfig.noteValueId() == kFlangerNoteValueId);
static_assert(kChorusConfig.voicesId() == kChorusVoicesId);
static_assert(kChorusConfig.waveformId() == kChorusWaveformId);
static_assert(kChorusConfig.noteValueId() == kChorusNoteValueId);

// =============================================================================
// Parameter Struct
// =============================================================================

/// Both effects share this struct. It is templated on the config because the
/// two differ in their default stereo spread, and that default is serialized:
/// a shared hard-coded value would change the bytes a fresh preset writes.
///
/// `voices` is meaningful for chorus only; flanger leaves it at its default and
/// never serializes it.
template <const ModEffectConfig& Cfg>
struct ModEffectParams {
    std::atomic<float> rateHz{0.5f};        // 0.05 .. rateMaxHz
    std::atomic<float> depth{0.5f};         // 0-1
    std::atomic<float> feedback{0.0f};      // -1 to +1
    std::atomic<float> mix{0.5f};           // 0-1 (true crossfade)
    std::atomic<float> stereoSpread{Cfg.defaultStereoSpread};  // 0-360 degrees
    std::atomic<int> voices{2};             // 1-4 (chorus only)
    std::atomic<int> waveform{1};           // 0=Sine, 1=Triangle
    std::atomic<bool> sync{false};          // tempo sync
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};
};

// =============================================================================
// Parameter Change Handler (denormalization)
// =============================================================================

template <typename Params>
inline void handleModEffectParamChange(
    const ModEffectConfig& cfg, Params& params,
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    if (id == cfg.rateId()) {
        params.rateHz.store(
            std::clamp(static_cast<float>(0.05 + value * cfg.rateSpan()),
                       0.05f, cfg.rateMaxHz),
            std::memory_order_relaxed);
    } else if (id == cfg.depthId()) {
        params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                           std::memory_order_relaxed);
    } else if (id == cfg.feedbackId()) {
        params.feedback.store(
            std::clamp(static_cast<float>(value * 2.0 - 1.0), -1.0f, 1.0f),
            std::memory_order_relaxed);
    } else if (id == cfg.mixId()) {
        params.mix.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                         std::memory_order_relaxed);
    } else if (id == cfg.stereoSpreadId()) {
        params.stereoSpread.store(
            std::clamp(static_cast<float>(value * 360.0), 0.0f, 360.0f),
            std::memory_order_relaxed);
    } else if (cfg.hasVoices && id == cfg.voicesId()) {
        params.voices.store(std::clamp(static_cast<int>(value * 3.0 + 0.5) + 1, 1, 4),
                            std::memory_order_relaxed);
    } else if (id == cfg.waveformId()) {
        params.waveform.store(std::clamp(static_cast<int>(value * 1.0 + 0.5), 0, 1),
                              std::memory_order_relaxed);
    } else if (id == cfg.syncId()) {
        params.sync.store(value >= 0.5, std::memory_order_relaxed);
    } else if (id == cfg.noteValueId()) {
        params.noteValue.store(
            std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                       0, Parameters::kNoteValueDropdownCount - 1),
            std::memory_order_relaxed);
    }
}

// =============================================================================
// Parameter Registration
// =============================================================================

/// Joins the effect prefix and a parameter name into a host-visible title.
/// Registration is not real-time and the temporary lives for the duration of the
/// addParameter() call it is passed to.
struct ModEffectTitle {
    Steinberg::Vst::String128 text{};

    ModEffectTitle(const Steinberg::Vst::TChar* prefix,
                   const Steinberg::Vst::TChar* name) noexcept {
        Steinberg::UString(text, 128).assign(prefix).append(name);
    }

    operator const Steinberg::Vst::TChar*() const noexcept { return text; }
};

inline void registerModEffectParams(Steinberg::Vst::ParameterContainer& parameters,
                                    const ModEffectConfig& cfg,
                                    const Steinberg::Vst::TChar* prefix) {
    using namespace Steinberg::Vst;

    // Default rate of 0.5 Hz expressed in the effect's own normalized range.
    const double defaultRate = static_cast<double>((0.5f - 0.05f) / cfg.rateSpan());

    parameters.addParameter(ModEffectTitle(prefix, STR16("Rate")), STR16("Hz"), 0,
        defaultRate, ParameterInfo::kCanAutomate, cfg.rateId());
    parameters.addParameter(ModEffectTitle(prefix, STR16("Depth")), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, cfg.depthId());
    parameters.addParameter(ModEffectTitle(prefix, STR16("Feedback")), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, cfg.feedbackId());  // 0.5 norm = 0.0 feedback
    parameters.addParameter(ModEffectTitle(prefix, STR16("Mix")), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, cfg.mixId());
    parameters.addParameter(ModEffectTitle(prefix, STR16("Spread")), STR16("\xC2\xB0"), 0,
        static_cast<double>(cfg.defaultStereoSpread / 360.0f),
        ParameterInfo::kCanAutomate, cfg.stereoSpreadId());
    if (cfg.hasVoices) {
        parameters.addParameter(createDropdownParameter(
            ModEffectTitle(prefix, STR16("Voices")), cfg.voicesId(),
            {STR16("1"), STR16("2"), STR16("3"), STR16("4")}));
    }
    parameters.addParameter(createDropdownParameter(
        ModEffectTitle(prefix, STR16("Waveform")), cfg.waveformId(),
        {STR16("Sine"), STR16("Triangle")}));
    parameters.addParameter(ModEffectTitle(prefix, STR16("Sync")), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, cfg.syncId());
    parameters.addParameter(createNoteValueDropdown(
        ModEffectTitle(prefix, STR16("Note Value")), cfg.noteValueId(),
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));
}

// =============================================================================
// Display Formatting
// =============================================================================

inline Steinberg::tresult formatModEffectParam(
    const ModEffectConfig& cfg, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value, Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];

    if (id == cfg.rateId()) {
        const float hz = static_cast<float>(0.05 + value * cfg.rateSpan());
        snprintf(text, sizeof(text), "%.2f Hz", hz);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    if (id == cfg.depthId() || id == cfg.mixId()) {
        snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    if (id == cfg.feedbackId()) {
        snprintf(text, sizeof(text), "%+.0f%%", (value * 2.0 - 1.0) * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    if (id == cfg.stereoSpreadId()) {
        const float deg = static_cast<float>(value * 360.0);
        snprintf(text, sizeof(text), "%.0f\xC2\xB0", deg);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

// =============================================================================
// State Save/Load
// =============================================================================

template <typename Params>
inline void saveModEffectParams(const ModEffectConfig& cfg,
                                const Params& params,
                                Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stereoSpread.load(std::memory_order_relaxed));
    if (cfg.hasVoices) {
        streamer.writeInt32(params.voices.load(std::memory_order_relaxed));
    }
    streamer.writeInt32(params.waveform.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
}

template <typename Params>
inline bool loadModEffectParams(const ModEffectConfig& cfg,
                                Params& params,
                                Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.feedback.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.mix.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.stereoSpread.store(fv, std::memory_order_relaxed);
    if (cfg.hasVoices) {
        if (!streamer.readInt32(iv)) { return false; } params.voices.store(iv, std::memory_order_relaxed);
    }
    if (!streamer.readInt32(iv)) { return false; } params.waveform.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.sync.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.noteValue.store(iv, std::memory_order_relaxed);
    return true;
}

// =============================================================================
// Controller State Restore
// =============================================================================

template<typename SetParamFunc>
inline void loadModEffectParamsToController(
    const ModEffectConfig& cfg, Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv))
        setParam(cfg.rateId(), static_cast<double>((fv - 0.05f) / cfg.rateSpan()));
    if (streamer.readFloat(fv)) setParam(cfg.depthId(), static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(cfg.feedbackId(), static_cast<double>((fv + 1.0f) / 2.0f));
    if (streamer.readFloat(fv)) setParam(cfg.mixId(), static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(cfg.stereoSpreadId(), static_cast<double>(fv / 360.0f));
    if (cfg.hasVoices) {
        if (streamer.readInt32(iv)) setParam(cfg.voicesId(), static_cast<double>(iv - 1) / 3.0);
    }
    if (streamer.readInt32(iv)) setParam(cfg.waveformId(), static_cast<double>(iv) / 1.0);
    if (streamer.readInt32(iv)) setParam(cfg.syncId(), iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv))
        setParam(cfg.noteValueId(),
                 static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
}

} // namespace Ruinae
