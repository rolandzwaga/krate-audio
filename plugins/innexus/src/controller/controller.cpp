// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "parameters/innexus_params.h"
#include "plugin_ids.h"
#include "update/innexus_update_config.h"

#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <vector>

namespace Innexus {

// ==============================================================================
// Initialize
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::initialize(Steinberg::FUnknown* context)
{
    auto result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultOk)
        return result;

    // --- Register parameters ---
    parameters.addParameter(STR16("Bypass"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsBypass,
        kBypassId);

    parameters.addParameter(STR16("Master Gain"), STR16("dB"), 0, 0.8,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMasterGainId);

    // M1 parameters
    registerInnexusParams(parameters);

    // M2 Residual parameters (FR-021, FR-024)
    auto* harmonicLevelParam = new Steinberg::Vst::RangeParameter(
        STR16("Harmonic Level"), kHarmonicLevelId,
        STR16(""),
        0.0,    // min plain
        2.0,    // max plain
        1.0,    // default plain
        0,      // stepCount (continuous)
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(harmonicLevelParam);

    auto* residualLevelParam = new Steinberg::Vst::RangeParameter(
        STR16("Residual Level"), kResidualLevelId,
        STR16(""),
        0.0,    // min plain
        2.0,    // max plain
        1.0,    // default plain
        0,      // stepCount (continuous)
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(residualLevelParam);

    // M2 Residual shaping parameters (FR-022, FR-023)
    auto* brightnessParam = new Steinberg::Vst::RangeParameter(
        STR16("Residual Brightness"), kResidualBrightnessId,
        STR16("%"),
        -1.0,   // min plain
        1.0,    // max plain
        0.0,    // default plain (neutral)
        0,      // stepCount (continuous)
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(brightnessParam);

    auto* transientParam = new Steinberg::Vst::RangeParameter(
        STR16("Transient Emphasis"), kTransientEmphasisId,
        STR16("%"),
        0.0,    // min plain
        2.0,    // max plain
        0.0,    // default plain (no boost)
        0,      // stepCount (continuous)
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(transientParam);

    // M3 Sidechain parameters (FR-002, FR-004)
    auto* inputSourceParam = new Steinberg::Vst::StringListParameter(
        STR16("Input Source"), kInputSourceId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    inputSourceParam->appendString(STR16("Sample"));
    inputSourceParam->appendString(STR16("Sidechain"));
    parameters.addParameter(inputSourceParam);

    auto* latencyModeParam = new Steinberg::Vst::StringListParameter(
        STR16("Latency Mode"), kLatencyModeId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    latencyModeParam->appendString(STR16("Low Latency"));
    latencyModeParam->appendString(STR16("High Precision"));
    parameters.addParameter(latencyModeParam);

    // M4 Musical Control parameters (FR-001, FR-010, FR-019, FR-029)
    parameters.addParameter(STR16("Freeze"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kFreezeId);

    auto* morphParam = new Steinberg::Vst::RangeParameter(
        STR16("Morph Position"), kMorphPositionId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(morphParam);

    auto* filterParam = new Steinberg::Vst::StringListParameter(
        STR16("Harmonic Filter"), kHarmonicFilterTypeId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    filterParam->appendString(STR16("All-Pass"));
    filterParam->appendString(STR16("Odd Only"));
    filterParam->appendString(STR16("Even Only"));
    filterParam->appendString(STR16("Low Harmonics"));
    filterParam->appendString(STR16("High Harmonics"));
    parameters.addParameter(filterParam);

    auto* respParam = new Steinberg::Vst::RangeParameter(
        STR16("Responsiveness"), kResponsivenessId,
        STR16("%"), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(respParam);

    // Update checker
    updateChecker_ = std::make_unique<Krate::Plugins::UpdateChecker>(makeInnexusUpdateConfig());

    return Steinberg::kResultOk;
}

// ==============================================================================
// Terminate
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::terminate()
{
    updateChecker_.reset();
    return EditControllerEx1::terminate();
}

// ==============================================================================
// Set Component State (FR-056: Restore controller parameters from processor state)
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::setComponentState(
    Steinberg::IBStream* state)
{
    if (!state)
        return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version (must match Processor::getState format)
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))
        return Steinberg::kResultFalse;

    if (version >= 1)
    {
        float floatVal = 0.0f;

        // Read releaseTimeMs and convert to normalized for controller
        if (streamer.readFloat(floatVal))
        {
            double normalized = releaseTimeToNormalized(
                std::clamp(floatVal, 20.0f, 5000.0f));
            setParamNormalized(kReleaseTimeId, normalized);
        }

        // Read inharmonicityAmount (0-1 maps directly to normalized)
        if (streamer.readFloat(floatVal))
        {
            setParamNormalized(kInharmonicityAmountId,
                static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
        }

        // Read masterGain (0-1 maps directly to normalized)
        if (streamer.readFloat(floatVal))
        {
            setParamNormalized(kMasterGainId,
                static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
        }

        // Read bypass
        if (streamer.readFloat(floatVal))
        {
            setParamNormalized(kBypassId,
                floatVal > 0.5f ? 1.0 : 0.0);
        }

        // Skip sample file path (controller does not need it)
        Steinberg::int32 pathLen = 0;
        if (streamer.readInt32(pathLen) && pathLen > 0 && pathLen < 4096)
        {
            // Skip the path bytes
            std::vector<char> pathBuf(static_cast<size_t>(pathLen));
            Steinberg::int32 bytesRead = 0;
            state->read(pathBuf.data(), pathLen, &bytesRead);
        }

        // M2: Read residual parameters if version >= 2 (FR-027)
        if (version >= 2)
        {
            // Harmonic Level (plain 0.0-2.0, normalized = plain / 2.0)
            if (streamer.readFloat(floatVal))
            {
                double normalized = static_cast<double>(
                    std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
                setParamNormalized(kHarmonicLevelId, normalized);
            }

            // Residual Level (plain 0.0-2.0, normalized = plain / 2.0)
            if (streamer.readFloat(floatVal))
            {
                double normalized = static_cast<double>(
                    std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
                setParamNormalized(kResidualLevelId, normalized);
            }

            // Residual Brightness (plain -1.0 to +1.0, normalized = (plain + 1.0) / 2.0)
            if (streamer.readFloat(floatVal))
            {
                double normalized = static_cast<double>(
                    std::clamp(floatVal, -1.0f, 1.0f) + 1.0f) / 2.0;
                setParamNormalized(kResidualBrightnessId, normalized);
            }

            // Transient Emphasis (plain 0.0-2.0, normalized = plain / 2.0)
            if (streamer.readFloat(floatVal))
            {
                double normalized = static_cast<double>(
                    std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
                setParamNormalized(kTransientEmphasisId, normalized);
            }
        }

        // Controller needs to skip residual frames data to reach M3 parameters
        // The processor state contains residual frame data between M2 and M3 sections
        if (version >= 2)
        {
            // Skip residual frames block (mirrors processor getState format)
            Steinberg::int32 residualFrameCount = 0;
            Steinberg::int32 analysisFFTSizeInt = 0;
            Steinberg::int32 analysisHopSizeInt = 0;

            if (streamer.readInt32(residualFrameCount) &&
                streamer.readInt32(analysisFFTSizeInt) &&
                streamer.readInt32(analysisHopSizeInt) &&
                residualFrameCount > 0)
            {
                // Skip each frame: 16 floats (bandEnergies) + 1 float (totalEnergy) + 1 int8 (transientFlag)
                for (Steinberg::int32 f = 0; f < residualFrameCount; ++f)
                {
                    for (size_t b = 0; b < 16; ++b) // kResidualBands = 16
                    {
                        float skipFloat = 0.0f;
                        streamer.readFloat(skipFloat);
                    }
                    float skipFloat = 0.0f;
                    streamer.readFloat(skipFloat); // totalEnergy
                    Steinberg::int8 skipByte = 0;
                    streamer.readInt8(skipByte); // transientFlag
                }
            }
        }

        // M3: Read sidechain parameters if version >= 3
        if (version >= 3)
        {
            Steinberg::int32 inputSourceInt = 0;
            Steinberg::int32 latencyModeInt = 0;
            if (streamer.readInt32(inputSourceInt))
            {
                setParamNormalized(kInputSourceId,
                    inputSourceInt > 0 ? 1.0 : 0.0);
            }
            if (streamer.readInt32(latencyModeInt))
            {
                setParamNormalized(kLatencyModeId,
                    latencyModeInt > 0 ? 1.0 : 0.0);
            }
        }

        // M4: Read musical control parameters if version >= 4
        if (version >= 4)
        {
            Steinberg::int8 freezeState = 0;
            if (streamer.readInt8(freezeState))
            {
                setParamNormalized(kFreezeId,
                    freezeState ? 1.0 : 0.0);
            }

            float morphPos = 0.0f;
            if (streamer.readFloat(morphPos))
            {
                setParamNormalized(kMorphPositionId,
                    static_cast<double>(std::clamp(morphPos, 0.0f, 1.0f)));
            }

            Steinberg::int32 filterType = 0;
            if (streamer.readInt32(filterType))
            {
                setParamNormalized(kHarmonicFilterTypeId,
                    static_cast<double>(std::clamp(
                        static_cast<float>(filterType) / 4.0f, 0.0f, 1.0f)));
            }

            float resp = 0.5f;
            if (streamer.readFloat(resp))
            {
                setParamNormalized(kResponsivenessId,
                    static_cast<double>(std::clamp(resp, 0.0f, 1.0f)));
            }
        }
        else
        {
            // Default M4 values for older states
            setParamNormalized(kFreezeId, 0.0);
            setParamNormalized(kMorphPositionId, 0.0);
            setParamNormalized(kHarmonicFilterTypeId, 0.0);  // All-Pass
            setParamNormalized(kResponsivenessId, 0.5);
        }
    }

    return Steinberg::kResultOk;
}

} // namespace Innexus
