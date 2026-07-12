// ==============================================================================
// Membrum Controller -- Phase 4 per-pad parameter registration + proxy logic
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "state/state_codec.h"
#include "controller_state_codec.h"
#include "ui/pad_grid_view.h"
#include "ui/kit_meters_view.h"
#include "ui/polyphony_slider.h"
#include "ui/pitch_envelope_display.h"  // shared PitchEnvelopeDisplay (Krate::Plugins)
#include "ui/xy_morph_pad.h"             // shared XYMorphPad (Krate::Plugins)
#include "ui/adsr_display.h"             // shared ADSRDisplay   (Krate::Plugins)
#include "ui/adsr_expanded_overlay.h"    // Membrum::UI::ADSRExpandedOverlayView
#include "ui/membrum_buttons.h"          // Membrum::UI::IconExpandActionButton + shared OutlineActionButton
#include "preset/membrum_preset_config.h"

#include "preset/preset_manager.h"
#include "preset/preset_info.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "base/source/fstring.h"

#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiattributes.h"

#include "../ui/membrum_buttons.h"
#include "../ui/preset_inline_browser_view.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cframe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace Membrum {
using namespace Steinberg;
using namespace Steinberg::Vst;

// ==============================================================================
// Controller param display formatting (getParamStringByValue + format helpers)
// ==============================================================================


// ==============================================================================
// getParamStringByValue -- human-readable ArcKnob value popups (Acoustic view)
// ==============================================================================
// Formats the normalised [0, 1] parameter value for the ten ArcKnobs on the
// "Acoustic" template: five macro knobs (bipolar %), Material (Wood <-> Metal),
// Size (Hz, log-mapped), Decay (%), Strike Position (Center <-> Edge), Level
// (dB). Per-pad macro / physics params route through here for any pad slot
// because we dispatch on the offset within the 64-param stride. Anything else
// falls back to the SDK default formatter.
//
// The numeric mappings MUST stay in sync with:
//   drum_voice.h::updateModalParameters (Size -> 500 * 0.1^norm Hz)
//   macro_mapper.cpp                    (macros are centred at 0.5)
// ==============================================================================
namespace {

inline void writeString128(Steinberg::Vst::String128 dst, const char* src)
{
    Steinberg::UString(dst, 128).fromAscii(src);
}

void formatPercent(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f%%", norm * 100.0);
    writeString128(out, text);
}

void formatBipolarPercent(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    const double pct = (norm * 2.0 - 1.0) * 100.0;
    char text[32];
    if (pct > 0.5)
        std::snprintf(text, sizeof(text), "+%.0f%%", pct);
    else if (pct < -0.5)
        std::snprintf(text, sizeof(text), "%.0f%%", pct);
    else
        std::snprintf(text, sizeof(text), "0%%");
    writeString128(out, text);
}

// min..max linear multiplier ("0.50x" .. "2.00x" style).
void formatMultiplier(Steinberg::Vst::ParamValue norm,
                      float minX, float maxX,
                      Steinberg::Vst::String128 out)
{
    const float x = minX + static_cast<float>(norm) * (maxX - minX);
    char text[32];
    std::snprintf(text, sizeof(text), "%.2fx", x);
    writeString128(out, text);
}

// Linear ms mapping; auto-switches to seconds past 1000 ms.
void formatLinearMs(Steinberg::Vst::ParamValue norm,
                    float minMs, float maxMs,
                    Steinberg::Vst::String128 out)
{
    const float ms = minMs + static_cast<float>(norm) * (maxMs - minMs);
    char text[32];
    if (ms >= 1000.0f)
        std::snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
    else if (ms >= 100.0f)
        std::snprintf(text, sizeof(text), "%.0f ms", ms);
    else if (ms >= 10.0f)
        std::snprintf(text, sizeof(text), "%.1f ms", ms);
    else
        std::snprintf(text, sizeof(text), "%.2f ms", ms);
    writeString128(out, text);
}

// Log ms mapping: ms = minMs * (maxMs/minMs)^norm.
void formatLogMs(Steinberg::Vst::ParamValue norm,
                 float minMs, float maxMs,
                 Steinberg::Vst::String128 out)
{
    const float ms = minMs * std::pow(maxMs / minMs, static_cast<float>(norm));
    char text[32];
    if (ms >= 1000.0f)
        std::snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
    else if (ms >= 100.0f)
        std::snprintf(text, sizeof(text), "%.0f ms", ms);
    else if (ms >= 10.0f)
        std::snprintf(text, sizeof(text), "%.1f ms", ms);
    else
        std::snprintf(text, sizeof(text), "%.2f ms", ms);
    writeString128(out, text);
}

// Log Hz mapping: hz = minHz * (maxHz/minHz)^norm.
void formatLogHz(Steinberg::Vst::ParamValue norm,
                 float minHz, float maxHz,
                 Steinberg::Vst::String128 out)
{
    const float hz = minHz * std::pow(maxHz / minHz, static_cast<float>(norm));
    char text[32];
    if (hz >= 1000.0f)
        std::snprintf(text, sizeof(text), "%.2f kHz", hz / 1000.0f);
    else if (hz >= 100.0f)
        std::snprintf(text, sizeof(text), "%.0f Hz", hz);
    else
        std::snprintf(text, sizeof(text), "%.1f Hz", hz);
    writeString128(out, text);
}

// Linear Q factor ("Q 0.30" .. "Q 5.00").
void formatQ(Steinberg::Vst::ParamValue norm,
             float minQ, float maxQ,
             Steinberg::Vst::String128 out)
{
    const float q = minQ + static_cast<float>(norm) * (maxQ - minQ);
    char text[32];
    std::snprintf(text, sizeof(text), "Q %.2f", q);
    writeString128(out, text);
}

// Noise color discretisation (matches noise_layer.h::denormColor thresholds).
void formatNoiseColor(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    const float v = static_cast<float>(norm);
    const char* name;
    if (v < 0.25f)      name = "Brown";
    else if (v < 0.55f) name = "Pink";
    else if (v < 0.80f) name = "White";
    else                name = "Violet";
    writeString128(out, name);
}

void formatOnOff(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    writeString128(out, norm >= 0.5 ? "On" : "Off");
}

void formatMaterial(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    // 0.0 = pure Wood, 1.0 = pure Metal. Show the dominant material on either
    // side of the 50/50 midpoint so the user sees "70% Wood" at low values
    // instead of "30% Metal".
    const float v = static_cast<float>(norm);
    char text[32];
    if (v < 0.02f)
        std::snprintf(text, sizeof(text), "Wood");
    else if (v > 0.98f)
        std::snprintf(text, sizeof(text), "Metal");
    else if (v < 0.48f)
        std::snprintf(text, sizeof(text), "%.0f%% Wood", (1.0f - v) * 100.0f);
    else if (v > 0.52f)
        std::snprintf(text, sizeof(text), "%.0f%% Metal", v * 100.0f);
    else
        std::snprintf(text, sizeof(text), "Wood / Metal");
    writeString128(out, text);
}

void formatStrikePosition(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    const float v = static_cast<float>(norm);
    char text[32];
    if (v < 0.02f)
        std::snprintf(text, sizeof(text), "Center");
    else if (v > 0.98f)
        std::snprintf(text, sizeof(text), "Edge");
    else
        std::snprintf(text, sizeof(text), "%.0f%% Edge", v * 100.0f);
    writeString128(out, text);
}

void formatSizeHz(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    // Size reads in the knob's direction (0% smallest -> 100% largest) with the
    // actual body fundamental appended as a hint. Hz formula matches
    // drum_voice.h: naturalFundamentalHz = 500 * 0.1^size.
    const float hz = 500.0f * std::pow(0.1f, static_cast<float>(norm));
    const int pct = static_cast<int>(norm * 100.0 + 0.5);
    char text[32];
    if (hz >= 100.0f)
        std::snprintf(text, sizeof(text), "%d%% (%.0f Hz)", pct, hz);
    else
        std::snprintf(text, sizeof(text), "%d%% (%.1f Hz)", pct, hz);
    writeString128(out, text);
}

void formatLevelDb(Steinberg::Vst::ParamValue norm, Steinberg::Vst::String128 out)
{
    const float gain = static_cast<float>(norm);
    char text[32];
    if (gain < 0.0005f)
        std::snprintf(text, sizeof(text), "-inf dB");
    else
    {
        const float dB = 20.0f * std::log10(gain);
        std::snprintf(text, sizeof(text), "%+.1f dB", dB);
    }
    writeString128(out, text);
}

// Linear dB mapping: dB = minDb + norm * (maxDb - minDb). Used by the master
// gain knob ([-24..+12] dB at norm [0..1]).
void formatLinearDb(Steinberg::Vst::ParamValue norm,
                    float minDb, float maxDb,
                    Steinberg::Vst::String128 out)
{
    const float dB = minDb + static_cast<float>(norm) * (maxDb - minDb);
    char text[32];
    std::snprintf(text, sizeof(text), "%+.1f dB", dB);
    writeString128(out, text);
}

// Returns true (and fills `out`) if the pad-relative offset corresponds to a
// Simple- or Advanced-view ArcKnob parameter; false otherwise so the caller
// can fall through to the SDK default.
bool formatByPadOffset(int offset,
                       Steinberg::Vst::ParamValue norm,
                       Steinberg::Vst::String128 out)
{
    switch (offset)
    {
    // --- Simple view primaries ------------------------------------------
    case kPadMaterial:         formatMaterial(norm, out);        return true;
    case kPadSize:             formatSizeHz(norm, out);          return true;
    case kPadDecay:            formatPercent(norm, out);         return true;
    case kPadStrikePosition:   formatStrikePosition(norm, out);  return true;
    case kPadLevel:            formatLevelDb(norm, out);         return true;
    case kPadMacroTightness:
    case kPadMacroBrightness:
    case kPadMacroBodySize:
    case kPadMacroPunch:
    case kPadMacroComplexity:
        formatBipolarPercent(norm, out);
        return true;

    // --- Tone Shaper (Advanced) -----------------------------------------
    case kPadTSFilterCutoff:    formatLogHz(norm, 20.0f, 20000.0f, out); return true;
    case kPadTSFilterEnvAmount: formatBipolarPercent(norm, out);         return true;
    case kPadTSFilterEnvAttack: formatLinearMs(norm, 0.0f,  500.0f, out); return true;
    case kPadTSFilterEnvDecay:
    case kPadTSFilterEnvRelease: formatLinearMs(norm, 0.0f, 2000.0f, out); return true;

    // --- Unnatural Zone / Morph -----------------------------------------
    case kPadModeStretch:   formatMultiplier(norm, 0.5f, 2.0f, out);  return true;
    case kPadDecaySkew:     formatBipolarPercent(norm, out);          return true;
    case kPadMorphDuration: formatLinearMs(norm, 10.0f, 2000.0f, out); return true;

    // --- Exciter secondary params ---------------------------------------
    case kPadFMRatio:            formatMultiplier(norm, 1.0f, 4.0f, out); return true;
    case kPadNoiseBurstDuration: formatLinearMs(norm, 2.0f, 15.0f, out);  return true;

    // --- Phase 7 parallel layers ----------------------------------------
    case kPadNoiseLayerCutoff:     formatLogHz(norm, 40.0f, 18000.0f, out);  return true;
    case kPadNoiseLayerResonance:  formatQ(norm, 0.3f, 5.0f, out);           return true;
    case kPadNoiseLayerDecay:      formatLogMs(norm, 20.0f, 2000.0f, out);   return true;
    case kPadNoiseLayerColor:      formatNoiseColor(norm, out);              return true;
    case kPadClickLayerContactMs:  formatLinearMs(norm, 2.0f, 5.0f, out);    return true;
    case kPadClickLayerBrightness: formatLogHz(norm, 200.0f, 12000.0f, out); return true;

    // --- Phase 8D shell coupling ----------------------------------------
    case kPadSecondaryEnabled:  formatOnOff(norm, out);    return true;
    case kPadSecondaryMaterial: formatMaterial(norm, out); return true;

    // --- Phase 8F per-pad enable toggle ---------------------------------
    case kPadEnabled:           formatOnOff(norm, out);    return true;

    // --- All params that map straight to a percentage readout -----------
    // Grouped via fall-through to eliminate identical-branch noise. Covers
    // resonance, drive, fold, sustain, mode-inject, nonlinear coupling,
    // feedback, friction, per-pad coupling, click/noise mix, damping,
    // air-loading, scatter, shell coupling strength + size, tension mod.
    case kPadTSFilterResonance:
    case kPadTSDriveAmount:
    case kPadTSFoldAmount:
    case kPadTSFilterEnvSustain:
    case kPadModeInjectAmount:
    case kPadNonlinearCoupling:
    case kPadFeedbackAmount:
    case kPadFrictionPressure:
    case kPadCouplingAmount:
    case kPadNoiseLayerMix:
    case kPadClickLayerMix:
    case kPadBodyDampingB1:
    case kPadBodyDampingB3:
    case kPadAirLoading:
    case kPadModeScatter:
    case kPadCouplingStrength:
    case kPadSecondarySize:
    case kPadTensionModAmt:        formatPercent(norm, out); return true;

    default:
        return false;
    }
}

} // anonymous namespace

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID tag,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string)
{
    // Global proxies share formatters with their corresponding per-pad offsets.
    switch (tag)
    {
    // Phase 1 primaries
    case kMaterialId:        formatMaterial(valueNormalized, string);       return kResultOk;
    case kSizeId:            formatSizeHz(valueNormalized, string);         return kResultOk;
    case kDecayId:           formatPercent(valueNormalized, string);        return kResultOk;
    case kStrikePositionId:  formatStrikePosition(valueNormalized, string); return kResultOk;
    case kLevelId:           formatLevelDb(valueNormalized, string);        return kResultOk;

    // Exciter secondary params
    case kExciterFMRatioId:            formatMultiplier(valueNormalized, 1.0f, 4.0f, string);  return kResultOk;
    case kExciterNoiseBurstDurationId: formatLinearMs(valueNormalized, 2.0f, 15.0f, string);   return kResultOk;

    // Tone Shaper
    case kToneShaperFilterCutoffId:    formatLogHz(valueNormalized, 20.0f, 20000.0f, string);  return kResultOk;
    case kToneShaperFilterEnvAmountId: formatBipolarPercent(valueNormalized, string);          return kResultOk;
    case kToneShaperFilterEnvAttackId: formatLinearMs(valueNormalized, 0.0f, 500.0f,  string); return kResultOk;
    case kToneShaperFilterEnvDecayId:
    case kToneShaperFilterEnvReleaseId: formatLinearMs(valueNormalized, 0.0f, 2000.0f, string); return kResultOk;

    // Unnatural Zone / Material Morph
    case kUnnaturalModeStretchId: formatMultiplier(valueNormalized, 0.5f, 2.0f, string);    return kResultOk;
    case kUnnaturalDecaySkewId:   formatBipolarPercent(valueNormalized, string);            return kResultOk;
    case kMorphDurationMsId:      formatLinearMs(valueNormalized, 10.0f, 2000.0f, string);  return kResultOk;

    // Phase 7 parallel layers
    case kNoiseLayerCutoffId:     formatLogHz(valueNormalized, 40.0f, 18000.0f, string);   return kResultOk;
    case kNoiseLayerResonanceId:  formatQ(valueNormalized, 0.3f, 5.0f, string);            return kResultOk;
    case kNoiseLayerDecayId:      formatLogMs(valueNormalized, 20.0f, 2000.0f, string);    return kResultOk;
    case kNoiseLayerColorId:      formatNoiseColor(valueNormalized, string);               return kResultOk;
    case kClickLayerContactMsId:  formatLinearMs(valueNormalized, 2.0f, 5.0f, string);     return kResultOk;
    case kClickLayerBrightnessId: formatLogHz(valueNormalized, 200.0f, 12000.0f, string);  return kResultOk;

    // Phase 8 physics detail (on/off + material strings).
    case kSecondaryEnabledId:
    case kPadEnabledId:        formatOnOff(valueNormalized, string);    return kResultOk;
    case kSecondaryMaterialId: formatMaterial(valueNormalized, string); return kResultOk;

    // Phase 5 coupling delay (right-column Coupling section).
    case kCouplingDelayId: formatLinearMs(valueNormalized, 0.5f, 2.0f, string); return kResultOk;

    // Phase 9 master output gain (right-column Master section).
    case kMasterGainId: formatLinearDb(valueNormalized, -24.0f, 12.0f, string); return kResultOk;

    // All globals that read as a plain percentage. Grouped via fall-through
    // to eliminate identical-branch warnings and keep the dispatch table
    // tight (clang-tidy: bugprone-branch-clone).
    case kExciterFeedbackAmountId:
    case kExciterFrictionPressureId:
    case kToneShaperFilterResonanceId:
    case kToneShaperDriveAmountId:
    case kToneShaperFoldAmountId:
    case kToneShaperFilterEnvSustainId:
    case kUnnaturalModeInjectAmountId:
    case kUnnaturalNonlinearCouplingId:
    case kNoiseLayerMixId:
    case kClickLayerMixId:
    case kBodyDampingB1Id:
    case kBodyDampingB3Id:
    case kAirLoadingId:
    case kModeScatterId:
    case kCouplingStrengthId:
    case kSecondarySizeId:
    case kTensionModAmtId:
    case kGlobalCouplingId:
    case kSnareBuzzId:
    case kTomResonanceId:     formatPercent(valueNormalized, string); return kResultOk;

    default:
        break;
    }

    // Per-pad parameter space (tag >= 1000). Strip the pad index and dispatch
    // on the offset. Selected-pad proxies are the pad-0 slot.
    if (tag >= static_cast<ParamID>(kPadBaseId))
    {
        const int offset =
            (static_cast<int>(tag) - kPadBaseId) % kPadParamStride;
        if (formatByPadOffset(offset, valueNormalized, string))
            return kResultOk;
    }

    return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
}
} // namespace Membrum
