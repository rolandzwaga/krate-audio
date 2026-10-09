#pragma once

// ==============================================================================
// Vorago - Macro Parameters (ID 100-199)   FR-042, FR-043
// ==============================================================================
// Twelve plain Vst::Parameters in VoragoMacro order. INERT in Phase 11: no code
// reads MacroParams into VoragoMacroMatrix; Phase 12 wires it. Plan section 2.4.
//
// Stream: twelve floats = 48 bytes (plan section 3.4).
// ==============================================================================

#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdio>

namespace Vorago {

/// FR-042. Explicit initializers are LOAD-BEARING: value-initialisation would put
/// gravity at 0.0f while the controller registers 0.5 and VoragoMacroValues::gravity
/// is 0.5f (vorago_macro_matrix.h:198-211). Caught by SC-010.2.
struct MacroParams {
    std::atomic<float> darkness{0.0f};
    std::atomic<float> age{0.0f};
    std::atomic<float> density{0.0f};
    std::atomic<float> movement{0.0f};
    std::atomic<float> gravity{0.5f};  ///< bipolar around 0.5
    std::atomic<float> entropy{0.0f};
    std::atomic<float> pressure{0.0f};
    std::atomic<float> weight{0.0f};
    std::atomic<float> fog{0.0f};
    std::atomic<float> life{0.0f};
    std::atomic<float> depth{0.0f};
    std::atomic<float> mass{0.0f};
};

static_assert(kMacroMassId - kMacroDarknessId + 1 == Krate::DSP::VoragoMacroMatrix::kNumMacros,
              "the macro band must hold exactly the twelve VoragoMacro values "
              "(vorago_macro_matrix.h:251)");

/// Number of macro parameters (== VoragoMacroMatrix::kNumMacros, asserted above).
inline constexpr int kNumMacroParams = static_cast<int>(kMacroMassId - kMacroDarknessId + 1);

// ==============================================================================
// Field access by VoragoMacro index (0 == Darkness ... 11 == Mass)
// ==============================================================================
// ONLY ever called with index in [0, kNumMacroParams): the other callers are fixed
// 0..11 loops, and handleMacroParamChange returns early for id > kMacroMassId
// before computing the index. The default arm is therefore unreachable; it asserts
// and then returns the last field so no path is undefined.

[[nodiscard]] inline std::atomic<float>& macroField(MacroParams& p, int index) noexcept {
    switch (index) {
        case 0: return p.darkness;
        case 1: return p.age;
        case 2: return p.density;
        case 3: return p.movement;
        case 4: return p.gravity;
        case 5: return p.entropy;
        case 6: return p.pressure;
        case 7: return p.weight;
        case 8: return p.fog;
        case 9: return p.life;
        case 10: return p.depth;
        case 11: return p.mass;
        default:
            assert(false && "macroField index out of range");
            return p.mass;
    }
}

[[nodiscard]] inline const std::atomic<float>& macroField(const MacroParams& p,
                                                          int index) noexcept {
    switch (index) {
        case 0: return p.darkness;
        case 1: return p.age;
        case 2: return p.density;
        case 3: return p.movement;
        case 4: return p.gravity;
        case 5: return p.entropy;
        case 6: return p.pressure;
        case 7: return p.weight;
        case 8: return p.fog;
        case 9: return p.life;
        case 10: return p.depth;
        case 11: return p.mass;
        default:
            assert(false && "macroField index out of range");
            return p.mass;
    }
}

// ==============================================================================
// Parameter Change Handler (FR-043 / FR-044)
// ==============================================================================

inline void handleMacroParamChange(MacroParams& params, Steinberg::Vst::ParamID id,
                                   Steinberg::Vst::ParamValue value) noexcept {
    // FR-043: an unregistered in-band ID (112-199) changes nothing. Checked BEFORE
    // the index is computed.
    if (id < kMacroDarknessId || id > kMacroMassId) { return; }
    const int index = static_cast<int>(id - kMacroDarknessId);
    macroField(params, index).store(std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                                    std::memory_order_relaxed);
}

// ==============================================================================
// Parameter Registration (FR-048 - twelve plain Vst::Parameters, FROZEN)
// ==============================================================================

inline void registerMacroParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    struct MacroDef {
        const TChar* title;
        ParamValue defaultValue;
    };
    // VoragoMacro order; defaults are VoragoMacroValues (gravity 0.5, the rest 0).
    const std::array<MacroDef, static_cast<std::size_t>(kNumMacroParams)> defs{{
        {STR16("Darkness"), 0.0},
        {STR16("Age"), 0.0},
        {STR16("Density"), 0.0},
        {STR16("Movement"), 0.0},
        {STR16("Gravity"), 0.5},
        {STR16("Entropy"), 0.0},
        {STR16("Pressure"), 0.0},
        {STR16("Weight"), 0.0},
        {STR16("Fog"), 0.0},
        {STR16("Life"), 0.0},
        {STR16("Depth"), 0.0},
        {STR16("Mass"), 0.0},
    }};

    for (int i = 0; i < kNumMacroParams; ++i) {
        const auto& d = defs[static_cast<std::size_t>(i)];
        parameters.addParameter(d.title, STR16("%"), 0, d.defaultValue,
                                ParameterInfo::kCanAutomate,
                                static_cast<Steinberg::int32>(kMacroDarknessId) + i);
    }
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatMacroParam(Steinberg::Vst::ParamID id,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    if (id >= kMacroDarknessId && id <= kMacroMassId) {
        char8 text[32];
        snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

// ==============================================================================
// State Persistence - 48 bytes (twelve floats)   FR-045 / FR-046
// ==============================================================================

inline void saveMacroParams(const MacroParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < kNumMacroParams; ++i) {
        streamer.writeFloat(macroField(params, i).load(std::memory_order_relaxed));
    }
}

/// EOF-safe: stops at the first failed read and returns false, leaving that field
/// and every later one at its CURRENT value. A non-finite float is skipped (field
/// unchanged); finite values are clamped to [0, 1].
inline bool loadMacroParams(MacroParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < kNumMacroParams; ++i) {
        float fv = 0.0f;
        if (!streamer.readFloat(fv)) { return false; }
        if (Krate::DSP::detail::isFinite(fv)) {  // fast-math-immune, db_utils.h:118
            macroField(params, i).store(std::clamp(fv, 0.0f, 1.0f), std::memory_order_relaxed);
        }
    }
    return true;
}

// ==============================================================================
// Controller State Sync (FR-047 - macros are stored normalized: identity mapping)
// ==============================================================================

template <typename SetParamFunc>
inline void loadMacroParamsToController(Steinberg::IBStreamer& streamer,
                                        SetParamFunc setParam) {
    for (int i = 0; i < kNumMacroParams; ++i) {
        float fv = 0.0f;
        if (!streamer.readFloat(fv)) { return; }
        if (Krate::DSP::detail::isFinite(fv)) {
            setParam(static_cast<Steinberg::Vst::ParamID>(kMacroDarknessId) +
                         static_cast<Steinberg::Vst::ParamID>(i),
                     static_cast<double>(std::clamp(fv, 0.0f, 1.0f)));
        }
    }
}

}  // namespace Vorago
