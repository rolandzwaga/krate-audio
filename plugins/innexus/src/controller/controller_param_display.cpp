// ==============================================================================
// Controller: Parameter Display Formatting
// ==============================================================================
// Overrides getParamStringByValue() to display human-readable parameter values
// with proper units (dB, ms, Hz, %) instead of raw normalized 0.0-1.0 values.
//
// Denormalization formulas here MUST match the processor's formulas in
// processor_params.cpp and processor.cpp exactly.
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/base/ustring.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Innexus {

using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;
using Steinberg::Vst::String128;

// =============================================================================
// Formatting Helpers
// =============================================================================

namespace {

/// Write an ASCII string into a VST3 String128 (UTF-16) buffer.
void writeString(String128 dst, const char* src)
{
    Steinberg::UString(dst, 128).fromAscii(src);
}

/// Format a percentage value. norm [0,1] → "0%" to "100%"
void formatPercent(ParamValue norm, String128 string)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f%%", norm * 100.0);
    writeString(string, text);
}

/// Format a percentage with a wider plain range. norm [0,1] → "0%" to "maxPct%"
void formatPercentScaled(ParamValue norm, float maxPct, String128 string)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f%%", norm * static_cast<double>(maxPct));
    writeString(string, text);
}

/// Format a bipolar percentage. norm [0,1] → "-100%" to "+100%"
void formatBipolarPercent(ParamValue norm, String128 string)
{
    double pct = (norm * 2.0 - 1.0) * 100.0;
    char text[32];
    if (pct > 0.5)
        std::snprintf(text, sizeof(text), "+%.0f%%", pct);
    else if (pct < -0.5)
        std::snprintf(text, sizeof(text), "%.0f%%", pct);
    else
        std::snprintf(text, sizeof(text), "0%%");
    writeString(string, text);
}

/// Format a time value in ms with log mapping. Auto-switches to seconds above 1000ms.
/// Formula: ms = minMs * (maxMs/minMs)^norm
void formatLogTimeMs(ParamValue norm, float minMs, float maxMs, String128 string)
{
    float ms = minMs * std::pow(maxMs / minMs, static_cast<float>(norm));
    ms = std::clamp(ms, minMs, maxMs);

    char text[32];
    if (ms >= 1000.0f)
        std::snprintf(text, sizeof(text), "%.1f s", ms / 1000.0f);
    else if (ms >= 100.0f)
        std::snprintf(text, sizeof(text), "%.0f ms", ms);
    else if (ms >= 10.0f)
        std::snprintf(text, sizeof(text), "%.1f ms", ms);
    else
        std::snprintf(text, sizeof(text), "%.1f ms", ms);
    writeString(string, text);
}

/// Format a time value in seconds with log mapping. Auto-switches to ms below 1s.
/// Formula: sec = minSec * (maxSec/minSec)^norm
void formatLogTimeSec(ParamValue norm, float minSec, float maxSec, String128 string)
{
    float sec = minSec * std::pow(maxSec / minSec, static_cast<float>(norm));
    sec = std::clamp(sec, minSec, maxSec);

    char text[32];
    float ms = sec * 1000.0f;
    if (ms >= 1000.0f)
        std::snprintf(text, sizeof(text), "%.2f s", sec);
    else if (ms >= 100.0f)
        std::snprintf(text, sizeof(text), "%.0f ms", ms);
    else
        std::snprintf(text, sizeof(text), "%.1f ms", ms);
    writeString(string, text);
}

/// Format a linear Hz value. norm [0,1] → [minHz, maxHz]
void formatLinearHz(ParamValue norm, float minHz, float maxHz, String128 string)
{
    float hz = minHz + static_cast<float>(norm) * (maxHz - minHz);
    char text[32];
    if (hz >= 10.0f)
        std::snprintf(text, sizeof(text), "%.1f Hz", hz);
    else
        std::snprintf(text, sizeof(text), "%.2f Hz", hz);
    writeString(string, text);
}

/// Format a bipolar curve value. norm [0,1] → plain [-1, +1]
void formatBipolarCurve(ParamValue norm, String128 string)
{
    double plain = -1.0 + norm * 2.0;
    char text[32];
    if (plain > 0.005)
        std::snprintf(text, sizeof(text), "+%.2f", plain);
    else if (plain < -0.005)
        std::snprintf(text, sizeof(text), "%.2f", plain);
    else
        std::snprintf(text, sizeof(text), "0.00");
    writeString(string, text);
}

/// Format a multiplier. norm [0,1] → plain [minX, maxX] → "1.50x"
void formatMultiplier(ParamValue norm, float minX, float maxX, String128 string)
{
    float x = minX + static_cast<float>(norm) * (maxX - minX);
    char text[32];
    std::snprintf(text, sizeof(text), "%.2fx", x);
    writeString(string, text);
}

/// Format an integer from stepped range. norm [0,1] → [min, max] integer
void formatInteger(ParamValue norm, int minVal, int maxVal, String128 string)
{
    int val = minVal + static_cast<int>(std::round(norm * static_cast<double>(maxVal - minVal)));
    char text[32];
    std::snprintf(text, sizeof(text), "%d", val);
    writeString(string, text);
}

/// Format dB from linear gain. norm [0,1] → gain [0, 2] → dB
void formatGainAsDb(ParamValue norm, String128 string)
{
    float gain = static_cast<float>(norm) * 2.0f;
    char text[32];
    if (gain < 0.0001f)
        std::snprintf(text, sizeof(text), "-inf dB");
    else
    {
        float dB = 20.0f * std::log10(gain);
        std::snprintf(text, sizeof(text), "%.1f dB", dB);
    }
    writeString(string, text);
}

} // anonymous namespace

// =============================================================================
// getParamStringByValue — Main Routing
// =============================================================================

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    ParamID id,
    ParamValue valueNormalized,
    String128 string)
{
    switch (id)
    {
    // =========================================================================
    // Global
    // =========================================================================
    case kMasterGainId:
        formatGainAsDb(valueNormalized, string);
        return Steinberg::kResultOk;

    // =========================================================================
    // Oscillator Bank (200-202)
    // =========================================================================
    case kReleaseTimeId:
        // Log mapping: 20ms * 250^norm
        formatLogTimeMs(valueNormalized, 20.0f, 5000.0f, string);
        return Steinberg::kResultOk;

    case kInharmonicityAmountId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    // kPartialCountId: StringListParameter, handled by SDK

    // =========================================================================
    // Residual Model (400-403)
    // =========================================================================
    case kHarmonicLevelId:
    case kResidualLevelId:
        // Plain 0-2, display as 0-200%
        formatPercentScaled(valueNormalized, 200.0f, string);
        return Steinberg::kResultOk;

    case kResidualBrightnessId:
        // Plain -1 to +1, display as -100% to +100%
        formatBipolarPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    case kTransientEmphasisId:
        // Plain 0-2, display as 0-200%
        formatPercentScaled(valueNormalized, 200.0f, string);
        return Steinberg::kResultOk;

    // =========================================================================
    // Musical Control (300-306)
    // =========================================================================
    case kMorphPositionId:
    case kResponsivenessId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    // kFreezeId, kHarmonicFilterTypeId, kMemorySlotId, etc.: handled by SDK

    // =========================================================================
    // Creative Extensions (600-649)
    // =========================================================================
    case kStereoSpreadId:
    case kEvolutionDepthId:
    case kMod1DepthId:
    case kMod2DepthId:
    case kDetuneSpreadId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    case kEvolutionSpeedId:
        // Linear: 0.01 + norm * 9.99 Hz
        formatLinearHz(valueNormalized, 0.01f, 10.0f, string);
        return Steinberg::kResultOk;

    case kMod1RateId:
    case kMod2RateId:
        // Linear: 0.01 + norm * 19.99 Hz
        formatLinearHz(valueNormalized, 0.01f, 20.0f, string);
        return Steinberg::kResultOk;

    case kMod1RangeStartId:
    case kMod1RangeEndId:
    case kMod2RangeStartId:
    case kMod2RangeEndId:
        // Integer 1-96
        formatInteger(valueNormalized, 1, 96, string);
        return Steinberg::kResultOk;

    case kBlendSlotWeight1Id:
    case kBlendSlotWeight2Id:
    case kBlendSlotWeight3Id:
    case kBlendSlotWeight4Id:
    case kBlendSlotWeight5Id:
    case kBlendSlotWeight6Id:
    case kBlendSlotWeight7Id:
    case kBlendSlotWeight8Id:
    case kBlendLiveWeightId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    // =========================================================================
    // Harmonic Physics (700-703)
    // =========================================================================
    case kWarmthId:
    case kCouplingId:
    case kStabilityId:
    case kEntropyId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    // =========================================================================
    // Analysis Feedback Loop (710-711)
    // =========================================================================
    case kAnalysisFeedbackId:
    case kAnalysisFeedbackDecayId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    // =========================================================================
    // ADSR Envelope (720-728)
    // =========================================================================
    case kAdsrAttackId:
    case kAdsrDecayId:
    case kAdsrReleaseId:
        // Log mapping: 1ms * 5000^norm
        formatLogTimeMs(valueNormalized, 1.0f, 5000.0f, string);
        return Steinberg::kResultOk;

    case kAdsrSustainId:
    case kAdsrAmountId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    case kAdsrTimeScaleId:
        // Linear: 0.25 + norm * 3.75
        formatMultiplier(valueNormalized, 0.25f, 4.0f, string);
        return Steinberg::kResultOk;

    case kAdsrAttackCurveId:
    case kAdsrDecayCurveId:
    case kAdsrReleaseCurveId:
        formatBipolarCurve(valueNormalized, string);
        return Steinberg::kResultOk;

    // =========================================================================
    // Physical Modelling (800-861)
    // =========================================================================
    case kPhysModelMixId:
    case kResonanceBrightnessId:
    case kResonanceStretchId:
    case kResonanceScatterId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    case kResonanceDecayId:
        // Log mapping: 0.01s * 500^norm
        formatLogTimeSec(valueNormalized, 0.01f, 5.0f, string);
        return Steinberg::kResultOk;

    // Impact Exciter
    case kImpactHardnessId:
    case kImpactMassId:
    case kImpactPositionId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    case kImpactBrightnessId:
        // Plain -1 to +1
        formatBipolarPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    // Waveguide String
    case kWaveguideStiffnessId:
    case kWaveguidePickPositionId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    // Bow Exciter
    case kBowPressureId:
    case kBowSpeedId:
    case kBowPositionId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    // Body Resonance
    case kBodySizeId:
    {
        // 0.0 = Small (violin), 0.5 = Medium (guitar), 1.0 = Large (cello)
        float v = static_cast<float>(valueNormalized);
        char text[32];
        if (v < 0.25f)
            std::snprintf(text, sizeof(text), "Small");
        else if (v < 0.75f)
            std::snprintf(text, sizeof(text), "Medium");
        else
            std::snprintf(text, sizeof(text), "Large");
        writeString(string, text);
        return Steinberg::kResultOk;
    }
    case kBodyMaterialId:
    {
        // 0.0 = Wood (low Q), 1.0 = Metal (high Q)
        float v = static_cast<float>(valueNormalized);
        char text[32];
        if (v < 0.2f)
            std::snprintf(text, sizeof(text), "Wood");
        else if (v < 0.8f)
            std::snprintf(text, sizeof(text), "%.0f%% Metal", v * 100.0f);
        else
            std::snprintf(text, sizeof(text), "Metal");
        writeString(string, text);
        return Steinberg::kResultOk;
    }
    case kBodyMixId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    // Sympathetic Resonance
    case kSympatheticAmountId:
    case kSympatheticDecayId:
        formatPercent(valueNormalized, string);
        return Steinberg::kResultOk;

    default:
        // Fall back to SDK default formatting (handles StringList, Bool, etc.)
        return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
    }
}

} // namespace Innexus
