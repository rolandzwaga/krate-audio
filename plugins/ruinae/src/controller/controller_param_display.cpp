// ==============================================================================
// Controller: Parameter Display Formatting
// ==============================================================================
// Extracted from controller.cpp - handles getParamStringByValue() and
// getParamValueByString() which route to per-section format functions.
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"

// Parameter pack headers (for format functions)
#include "parameters/global_params.h"
#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"
#include "parameters/mixer_params.h"
#include "parameters/filter_params.h"
#include "parameters/distortion_params.h"
#include "parameters/trance_gate_params.h"
#include "parameters/amp_env_params.h"
#include "parameters/filter_env_params.h"
#include "parameters/mod_env_params.h"
#include "parameters/lfo1_params.h"
#include "parameters/lfo2_params.h"
#include "parameters/chaos_mod_params.h"
#include "parameters/mod_matrix_params.h"
#include "parameters/global_filter_params.h"
#include "parameters/delay_params.h"
#include "parameters/reverb_params.h"
#include "parameters/phaser_params.h"
#include "parameters/flanger_params.h"
#include "parameters/harmonizer_params.h"
#include "parameters/mono_mode_params.h"
#include "parameters/macro_params.h"
#include "parameters/rungler_params.h"
#include "parameters/settings_params.h"
#include "parameters/env_follower_params.h"
#include "parameters/sample_hold_params.h"
#include "parameters/random_params.h"
#include "parameters/pitch_follower_params.h"
#include "parameters/transient_params.h"
#include "parameters/arpeggiator_params.h"

namespace Ruinae {

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string) {

    // Route to appropriate parameter pack formatter by ID range
    Steinberg::tresult result = Steinberg::kResultFalse;

    if (id <= kGlobalEndId) {
        result = formatGlobalParam(id, valueNormalized, string);
    } else if (id >= kOscABaseId && id <= kOscAEndId) {
        result = formatOscAParam(id, valueNormalized, string);
    } else if (id >= kOscBBaseId && id <= kOscBEndId) {
        result = formatOscBParam(id, valueNormalized, string);
    } else if (id >= kMixerBaseId && id <= kMixerEndId) {
        result = formatMixerParam(id, valueNormalized, string);
    } else if (id >= kFilterBaseId && id <= kFilterEndId) {
        result = formatFilterParam(id, valueNormalized, string);
    } else if (id >= kDistortionBaseId && id <= kDistortionEndId) {
        result = formatDistortionParam(id, valueNormalized, string);
    } else if (id >= kTranceGateBaseId && id <= kTranceGateEndId) {
        result = formatTranceGateParam(id, valueNormalized, string);
    } else if (id >= kAmpEnvBaseId && id <= kAmpEnvEndId) {
        result = formatAmpEnvParam(id, valueNormalized, string);
    } else if (id >= kFilterEnvBaseId && id <= kFilterEnvEndId) {
        result = formatFilterEnvParam(id, valueNormalized, string);
    } else if (id >= kModEnvBaseId && id <= kModEnvEndId) {
        result = formatModEnvParam(id, valueNormalized, string);
    } else if (id >= kLFO1BaseId && id <= kLFO1EndId) {
        result = formatLFO1Param(id, valueNormalized, string);
    } else if (id >= kLFO2BaseId && id <= kLFO2EndId) {
        result = formatLFO2Param(id, valueNormalized, string);
    } else if (id >= kChaosModBaseId && id <= kChaosModEndId) {
        result = formatChaosModParam(id, valueNormalized, string);
    } else if (id >= kModMatrixBaseId && id <= kModMatrixEndId) {
        result = formatModMatrixParam(id, valueNormalized, string);
    } else if (id >= kGlobalFilterBaseId && id <= kGlobalFilterEndId) {
        result = formatGlobalFilterParam(id, valueNormalized, string);
    } else if (id >= kDelayBaseId && id <= kDelayEndId) {
        result = formatDelayParam(id, valueNormalized, string);
    } else if (id >= kReverbBaseId && id <= kReverbEndId) {
        result = formatReverbParam(id, valueNormalized, string);
    } else if (id >= kPhaserBaseId && id <= kPhaserEndId) {
        result = formatPhaserParam(id, valueNormalized, string);
    } else if (id >= kFlangerRateId && id <= kFlangerEndId) {
        result = formatFlangerParam(id, valueNormalized, string);
    } else if (id >= kHarmonizerBaseId && id <= kHarmonizerEndId) {
        result = formatHarmonizerParam(id, valueNormalized, string);
    } else if (id >= kMonoBaseId && id <= kMonoEndId) {
        result = formatMonoModeParam(id, valueNormalized, string);
    } else if (id >= kMacroBaseId && id <= kMacroEndId) {
        result = formatMacroParam(id, valueNormalized, string);
    } else if (id >= kRunglerBaseId && id <= kRunglerEndId) {
        result = formatRunglerParam(id, valueNormalized, string);
    } else if (id >= kSettingsBaseId && id <= kSettingsEndId) {
        result = formatSettingsParam(id, valueNormalized, string);
    } else if (id >= kEnvFollowerBaseId && id <= kEnvFollowerEndId) {
        result = formatEnvFollowerParam(id, valueNormalized, string);
    } else if (id >= kSampleHoldBaseId && id <= kSampleHoldEndId) {
        result = formatSampleHoldParam(id, valueNormalized, string);
    } else if (id >= kRandomBaseId && id <= kRandomEndId) {
        result = formatRandomParam(id, valueNormalized, string);
    } else if (id >= kPitchFollowerBaseId && id <= kPitchFollowerEndId) {
        result = formatPitchFollowerParam(id, valueNormalized, string);
    } else if (id >= kTransientBaseId && id <= kTransientEndId) {
        result = formatTransientParam(id, valueNormalized, string);
    } else if (id >= kArpBaseId && id <= kArpEndId) {
        result = formatArpParam(id, valueNormalized, string);
    }

    // Fall back to default implementation for unhandled parameters
    // (StringListParameter handles its own formatting)
    if (result != Steinberg::kResultOk) {
        return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
    }
    return result;
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string,
    Steinberg::Vst::ParamValue& valueNormalized) {
    // Use default implementation for now
    return EditControllerEx1::getParamValueByString(id, string, valueNormalized);
}

} // namespace Ruinae
