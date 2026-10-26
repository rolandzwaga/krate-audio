#pragma once

// ==============================================================================
// Profundum - Macro Parameters (ID 100-103)   FR-041
// ==============================================================================
// The four personality macros, in ProfundumMacro order
// (dsp/include/krate/dsp/systems/profundum_macro_matrix.h:38). Plan section 2.4.
// Stored normalized [0, 1], neutral 0.5 (identity mapping).
//
// Stream: four floats = 16 bytes (plan section 3.4).
//
// The four macros are live (user ruling Q3, 2026-10-22): the processor pushes
// macroValues() through ProfundumEngine::setMacros on every process().
// ==============================================================================

#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdio>

namespace Profundum {

/// ProfundumMacro order (profundum_macro_matrix.h:38); explicit initialisers are load-bearing (P-8).
struct MacroParams {
    std::atomic<float> weight{Krate::DSP::ProfundumMacroMatrix::kNeutral};
    std::atomic<float> darkBright{Krate::DSP::ProfundumMacroMatrix::kNeutral};
    std::atomic<float> tightLoose{Krate::DSP::ProfundumMacroMatrix::kNeutral};
    std::atomic<float> cleanViolent{Krate::DSP::ProfundumMacroMatrix::kNeutral};
};

/// Number of macro parameters (== ProfundumMacroMatrix::kNumMacros, asserted below).
inline constexpr int kNumMacroParams = static_cast<int>(kMacroCleanViolentId - kMacroWeightId + 1);
static_assert(kNumMacroParams == static_cast<int>(Krate::DSP::ProfundumMacroMatrix::kNumMacros),
              "the macro band must hold exactly the four ProfundumMacro values "
              "(profundum_macro_matrix.h:68)");
static_assert(static_cast<int>(kMacroDarkBrightId - kMacroWeightId) ==
                  static_cast<int>(Krate::DSP::ProfundumMacro::DarkBright),
              "macro IDs follow ProfundumMacro order (profundum_macro_matrix.h:38)");
static_assert(Krate::DSP::ProfundumMacroValues{}.weight == Krate::DSP::ProfundumMacroMatrix::kNeutral,
              "pack default == engine default (P-8; profundum_macro_matrix.h:58-67)");

// ==============================================================================
// Field access by ProfundumMacro index (0 == Weight ... 3 == CleanViolent)
// ==============================================================================
// ONLY ever called with index in [0, kNumMacroParams): the other callers are fixed
// 0..3 loops, and handleMacroParamChange returns early for an ID outside
// [100, 103] before computing the index. The default arm is therefore unreachable;
// it asserts and then returns the last field so no path is undefined.

[[nodiscard]] inline std::atomic<float>& macroField(MacroParams& p, int index) noexcept {
    switch (index) {
        case 0: return p.weight;
        case 1: return p.darkBright;
        case 2: return p.tightLoose;
        case 3: return p.cleanViolent;
        default:
            assert(false && "macroField index out of range");
            return p.cleanViolent;
    }
}

[[nodiscard]] inline const std::atomic<float>& macroField(const MacroParams& p, int index) noexcept {
    switch (index) {
        case 0: return p.weight;
        case 1: return p.darkBright;
        case 2: return p.tightLoose;
        case 3: return p.cleanViolent;
        default:
            assert(false && "macroField index out of range");
            return p.cleanViolent;
    }
}

/// The engine-side snapshot (relaxed loads), handed to ProfundumEngine::setMacros (engine.h:371).
[[nodiscard]] inline Krate::DSP::ProfundumMacroValues macroValues(const MacroParams& p) noexcept {
    Krate::DSP::ProfundumMacroValues v;
    v.weight = p.weight.load(std::memory_order_relaxed);
    v.darkBright = p.darkBright.load(std::memory_order_relaxed);
    v.tightLoose = p.tightLoose.load(std::memory_order_relaxed);
    v.cleanViolent = p.cleanViolent.load(std::memory_order_relaxed);
    return v;
}

// ==============================================================================
// Pack functions (T009: handleMacroParamChange; T010: register / format;
// T011: save / load; T012: the controller mirror)
// ==============================================================================

/// Plan 2.4. An ID outside [100, 103] returns BEFORE the index is computed
/// (FR-042; IDs 104-199 are reserved for Phase 11). Stores clamp(float(v), 0, 1),
/// relaxed.
inline void handleMacroParamChange(MacroParams& params, Steinberg::Vst::ParamID id,
                                   Steinberg::Vst::ParamValue value) noexcept {
    if (id < kMacroWeightId || id > kMacroCleanViolentId) {
        return;
    }
    const int index = static_cast<int>(id - kMacroWeightId);
    macroField(params, index).store(std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                                    std::memory_order_relaxed);
}

/// FR-047 (plan 2.4): four plain parameters, unit "%", default 0.5 (neutral), the
/// ASCII titles of P-13, IDs 100 + i in ProfundumMacro order.
inline void registerMacroParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    const std::array<const TChar*, static_cast<std::size_t>(kNumMacroParams)> titles{
        STR16("Weight"), STR16("Dark/Bright"), STR16("Tight/Loose"), STR16("Clean/Violent")};

    for (int i = 0; i < kNumMacroParams; ++i) {
        parameters.addParameter(titles[static_cast<std::size_t>(i)], STR16("%"), 0,
                                static_cast<ParamValue>(Krate::DSP::ProfundumMacroMatrix::kNeutral),
                                ParameterInfo::kCanAutomate,
                                static_cast<Steinberg::int32>(kMacroWeightId) + i);
    }
}

inline Steinberg::tresult formatMacroParam(Steinberg::Vst::ParamID id,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    if (id >= kMacroWeightId && id <= kMacroCleanViolentId) {
        char8 text[32];
        snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

/// Plan 2.4 / 3.4: four floats in ProfundumMacro order (16 bytes).
inline void saveMacroParams(const MacroParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < kNumMacroParams; ++i) {
        streamer.writeFloat(macroField(params, i).load(std::memory_order_relaxed));
    }
}

/// EOF-safe; false at the first failed read, leaving every later field unchanged.
/// FR-045: a non-finite value keeps its atomic (db_utils.h:118); a finite one is
/// clamped to [0, 1].
inline bool loadMacroParams(MacroParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < kNumMacroParams; ++i) {
        float value = 0.0f;
        if (!streamer.readFloat(value)) {
            return false;
        }
        if (Krate::DSP::detail::isFinite(value)) {
            macroField(params, i).store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
        }
    }
    return true;
}

/// FR-045 / FR-046 (plan 2.4): EOF-safe; a non-finite value makes NO call, a
/// finite one is clamped to [0, 1] and set as the normalized value.
template <typename SetParamFunc>
inline void loadMacroParamsToController(Steinberg::IBStreamer& streamer,
                                        SetParamFunc setParam) {
    for (int i = 0; i < kNumMacroParams; ++i) {
        float value = 0.0f;
        if (!streamer.readFloat(value)) {
            return;
        }
        if (Krate::DSP::detail::isFinite(value)) {
            setParam(static_cast<Steinberg::Vst::ParamID>(kMacroWeightId) +
                         static_cast<Steinberg::Vst::ParamID>(i),
                     static_cast<double>(std::clamp(value, 0.0f, 1.0f)));
        }
    }
}

}  // namespace Profundum
