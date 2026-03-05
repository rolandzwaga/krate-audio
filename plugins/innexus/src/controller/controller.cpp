// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "dsp/harmonic_snapshot_json.h"
#include "parameters/innexus_params.h"
#include "plugin_ids.h"
#include "update/innexus_update_config.h"

#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <fstream>
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

    // M5 Harmonic Memory parameters (FR-005, FR-006, FR-011)
    auto* memorySlotParam = new Steinberg::Vst::StringListParameter(
        STR16("Memory Slot"), kMemorySlotId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    memorySlotParam->appendString(STR16("Slot 1"));
    memorySlotParam->appendString(STR16("Slot 2"));
    memorySlotParam->appendString(STR16("Slot 3"));
    memorySlotParam->appendString(STR16("Slot 4"));
    memorySlotParam->appendString(STR16("Slot 5"));
    memorySlotParam->appendString(STR16("Slot 6"));
    memorySlotParam->appendString(STR16("Slot 7"));
    memorySlotParam->appendString(STR16("Slot 8"));
    parameters.addParameter(memorySlotParam);

    parameters.addParameter(STR16("Memory Capture"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMemoryCaptureId);

    parameters.addParameter(STR16("Memory Recall"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMemoryRecallId);

    // ==================================================================
    // M6 Creative Extensions parameters (FR-043)
    // ==================================================================

    // Cross-Synthesis: Timbral Blend (FR-001)
    auto* timbralBlendParam = new Steinberg::Vst::RangeParameter(
        STR16("Timbral Blend"), kTimbralBlendId,
        STR16("%"), 0.0, 1.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(timbralBlendParam);

    // Stereo Spread (FR-006)
    auto* stereoSpreadParam = new Steinberg::Vst::RangeParameter(
        STR16("Stereo Spread"), kStereoSpreadId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(stereoSpreadParam);

    // Evolution Engine (FR-014 to FR-017)
    parameters.addParameter(STR16("Evolution Enable"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kEvolutionEnableId);

    auto* evoSpeedParam = new Steinberg::Vst::RangeParameter(
        STR16("Evolution Speed"), kEvolutionSpeedId,
        STR16("Hz"), 0.01, 10.0, 0.1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(evoSpeedParam);

    auto* evoDepthParam = new Steinberg::Vst::RangeParameter(
        STR16("Evolution Depth"), kEvolutionDepthId,
        STR16("%"), 0.0, 1.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(evoDepthParam);

    auto* evoModeParam = new Steinberg::Vst::StringListParameter(
        STR16("Evolution Mode"), kEvolutionModeId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    evoModeParam->appendString(STR16("Cycle"));
    evoModeParam->appendString(STR16("PingPong"));
    evoModeParam->appendString(STR16("Random Walk"));
    parameters.addParameter(evoModeParam);

    // Modulator 1 (FR-024)
    parameters.addParameter(STR16("Mod 1 Enable"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMod1EnableId);

    auto* mod1WaveParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 1 Waveform"), kMod1WaveformId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod1WaveParam->appendString(STR16("Sine"));
    mod1WaveParam->appendString(STR16("Triangle"));
    mod1WaveParam->appendString(STR16("Square"));
    mod1WaveParam->appendString(STR16("Saw"));
    mod1WaveParam->appendString(STR16("Random S&H"));
    parameters.addParameter(mod1WaveParam);

    auto* mod1RateParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 1 Rate"), kMod1RateId,
        STR16("Hz"), 0.01, 20.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1RateParam);

    auto* mod1DepthParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 1 Depth"), kMod1DepthId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1DepthParam);

    auto* mod1RangeStartParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 1 Range Start"), kMod1RangeStartId,
        STR16(""), 1.0, 48.0, 1.0, 47,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1RangeStartParam);

    auto* mod1RangeEndParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 1 Range End"), kMod1RangeEndId,
        STR16(""), 1.0, 48.0, 48.0, 47,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod1RangeEndParam);

    auto* mod1TargetParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 1 Target"), kMod1TargetId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod1TargetParam->appendString(STR16("Amplitude"));
    mod1TargetParam->appendString(STR16("Frequency"));
    mod1TargetParam->appendString(STR16("Pan"));
    parameters.addParameter(mod1TargetParam);

    // Modulator 2 (FR-024)
    parameters.addParameter(STR16("Mod 2 Enable"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kMod2EnableId);

    auto* mod2WaveParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 2 Waveform"), kMod2WaveformId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod2WaveParam->appendString(STR16("Sine"));
    mod2WaveParam->appendString(STR16("Triangle"));
    mod2WaveParam->appendString(STR16("Square"));
    mod2WaveParam->appendString(STR16("Saw"));
    mod2WaveParam->appendString(STR16("Random S&H"));
    parameters.addParameter(mod2WaveParam);

    auto* mod2RateParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 2 Rate"), kMod2RateId,
        STR16("Hz"), 0.01, 20.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2RateParam);

    auto* mod2DepthParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 2 Depth"), kMod2DepthId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2DepthParam);

    auto* mod2RangeStartParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 2 Range Start"), kMod2RangeStartId,
        STR16(""), 1.0, 48.0, 1.0, 47,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2RangeStartParam);

    auto* mod2RangeEndParam = new Steinberg::Vst::RangeParameter(
        STR16("Mod 2 Range End"), kMod2RangeEndId,
        STR16(""), 1.0, 48.0, 48.0, 47,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(mod2RangeEndParam);

    auto* mod2TargetParam = new Steinberg::Vst::StringListParameter(
        STR16("Mod 2 Target"), kMod2TargetId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    mod2TargetParam->appendString(STR16("Amplitude"));
    mod2TargetParam->appendString(STR16("Frequency"));
    mod2TargetParam->appendString(STR16("Pan"));
    parameters.addParameter(mod2TargetParam);

    // Detune Spread (FR-030)
    auto* detuneSpreadParam = new Steinberg::Vst::RangeParameter(
        STR16("Detune Spread"), kDetuneSpreadId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(detuneSpreadParam);

    // Multi-Source Blend (FR-034 to FR-036)
    parameters.addParameter(STR16("Blend Enable"), nullptr, 1, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kBlendEnableId);

    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 1 Weight"), kBlendSlotWeight1Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 2 Weight"), kBlendSlotWeight2Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 3 Weight"), kBlendSlotWeight3Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 4 Weight"), kBlendSlotWeight4Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 5 Weight"), kBlendSlotWeight5Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 6 Weight"), kBlendSlotWeight6Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 7 Weight"), kBlendSlotWeight7Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Blend Slot 8 Weight"), kBlendSlotWeight8Id,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));

    auto* blendLiveWeightParam = new Steinberg::Vst::RangeParameter(
        STR16("Blend Live Weight"), kBlendLiveWeightId,
        STR16("%"), 0.0, 1.0, 0.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(blendLiveWeightParam);

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

        // M5: Read harmonic memory parameters if version >= 5
        if (version >= 5)
        {
            // Read selected slot index and set parameter
            Steinberg::int32 selectedSlot = 0;
            if (streamer.readInt32(selectedSlot))
            {
                selectedSlot = std::clamp(selectedSlot,
                    static_cast<Steinberg::int32>(0),
                    static_cast<Steinberg::int32>(7));
                setParamNormalized(kMemorySlotId,
                    std::clamp(static_cast<double>(selectedSlot) / 7.0, 0.0, 1.0));
            }

            // Skip all 8 memory slots' binary snapshot data
            // (controller does not store snapshot binary)
            for (int s = 0; s < 8; ++s)
            {
                Steinberg::int8 occupiedByte = 0;
                if (!streamer.readInt8(occupiedByte))
                    break;

                if (occupiedByte != 0)
                {
                    // Skip snapshot data: f0Reference (float) + numPartials (int32)
                    // + 48*4 floats (relativeFreqs, normalizedAmps, phases, inharmonicDeviation)
                    // + 16 floats (residualBands)
                    // + 4 floats (residualEnergy, globalAmplitude, spectralCentroid, brightness)
                    // Total: 1 float + 1 int32 + (48*4 + 16 + 4) floats = 1 + 1 + 212 = 213 reads
                    float skipFloat = 0.0f;
                    Steinberg::int32 skipInt = 0;

                    streamer.readFloat(skipFloat);  // f0Reference
                    streamer.readInt32(skipInt);     // numPartials

                    // 48*4 = 192 floats for per-partial arrays
                    for (int i = 0; i < 48 * 4; ++i)
                        streamer.readFloat(skipFloat);

                    // 16 floats for residualBands
                    for (int i = 0; i < 16; ++i)
                        streamer.readFloat(skipFloat);

                    // 4 scalar floats
                    streamer.readFloat(skipFloat);   // residualEnergy
                    streamer.readFloat(skipFloat);   // globalAmplitude
                    streamer.readFloat(skipFloat);   // spectralCentroid
                    streamer.readFloat(skipFloat);   // brightness
                }
            }

            // Momentary triggers always reset to 0 (FR-023, FR-030)
            setParamNormalized(kMemoryCaptureId, 0.0);
            setParamNormalized(kMemoryRecallId, 0.0);
        }
        else
        {
            // Default M5 values for older states
            setParamNormalized(kMemorySlotId, 0.0);
            setParamNormalized(kMemoryCaptureId, 0.0);
            setParamNormalized(kMemoryRecallId, 0.0);
        }
    }

    return Steinberg::kResultOk;
}

// ==============================================================================
// Import Snapshot from JSON (FR-025, FR-029)
// ==============================================================================
bool Controller::importSnapshotFromJson(const std::string& filePath,
                                        int slotIndex)
{
    // Read file
    std::ifstream f(filePath);
    if (!f.is_open()) return false;
    const std::string json((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());

    // Parse
    Krate::DSP::HarmonicSnapshot snap{};
    if (!Innexus::jsonToSnapshot(json, snap)) return false;

    // Dispatch to processor via IMessage (FR-029)
    auto* msg = allocateMessage();
    if (!msg) return false;
    msg->setMessageID("HarmonicSnapshotImport");
    auto* attrs = msg->getAttributes();
    attrs->setInt("slotIndex", static_cast<Steinberg::int64>(slotIndex));
    attrs->setBinary("snapshotData", &snap, sizeof(snap));
    sendMessage(msg);
    msg->release();
    return true;
}

} // namespace Innexus
