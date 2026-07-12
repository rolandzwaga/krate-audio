// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "dsp/harmonic_snapshot_json.h"
#include "parameters/innexus_params.h"
#include "parameters/note_value_ui.h"
#include "plugin_ids.h"
#include "preset/innexus_preset_config.h"
#include "update/innexus_update_config.h"
#include "version.h"

#include "controller/views/harmonic_display_view.h"
#include "controller/views/confidence_indicator_view.h"
#include "controller/views/memory_slot_status_view.h"
#include "controller/views/evolution_position_view.h"
#include "controller/views/modulator_activity_view.h"
#include "controller/modulator_sub_controller.h"
#include "controller/adsr_expanded_overlay.h"
#include "controller/sample_drop_target.h"
#include "ui/adsr_display.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/update_banner_view.h"
#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

namespace Innexus {

// ==============================================================================
// Controller state / preset snapshot (setComponentState, importSnapshotFromJson)
// ==============================================================================


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

    if (version != 1 && version != 2)
        return Steinberg::kResultFalse;

    float floatVal = 0.0f;

    // --- M1 parameters ---
    if (streamer.readFloat(floatVal))
    {
        double normalized = releaseTimeToNormalized(
            std::clamp(floatVal, 20.0f, 5000.0f));
        setParamNormalized(kReleaseTimeId, normalized);
    }

    if (streamer.readFloat(floatVal))
    {
        setParamNormalized(kInharmonicityAmountId,
            static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
    }

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

        // Read sample file path for filename display
        Steinberg::int32 pathLen = 0;
        if (streamer.readInt32(pathLen) && pathLen > 0 && pathLen < 4096)
        {
            std::vector<char> pathBuf(static_cast<size_t>(pathLen));
            Steinberg::int32 bytesRead = 0;
            state->read(pathBuf.data(), pathLen, &bytesRead);
            if (bytesRead == pathLen)
            {
                std::string fullPath(pathBuf.data(),
                                     static_cast<size_t>(pathLen));
                auto filename = std::filesystem::path(fullPath)
                                    .filename().string();
                loadedSampleFilename_ = filename;
                loadedSampleFullPath_ = fullPath;
            }
        }

    // --- M2 parameters (FR-027) ---
    if (streamer.readFloat(floatVal))
    {
        double normalized = static_cast<double>(
            std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
        setParamNormalized(kHarmonicLevelId, normalized);
    }

    if (streamer.readFloat(floatVal))
    {
        double normalized = static_cast<double>(
            std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
        setParamNormalized(kResidualLevelId, normalized);
    }

    if (streamer.readFloat(floatVal))
    {
        double normalized = static_cast<double>(
            std::clamp(floatVal, -1.0f, 1.0f) + 1.0f) / 2.0;
        setParamNormalized(kResidualBrightnessId, normalized);
    }

    if (streamer.readFloat(floatVal))
    {
        double normalized = static_cast<double>(
            std::clamp(floatVal, 0.0f, 2.0f)) / 2.0;
        setParamNormalized(kTransientEmphasisId, normalized);
    }

    // Skip residual frames block (mirrors processor getState format)
    {
        Steinberg::int32 residualFrameCount = 0;
        Steinberg::int32 analysisFFTSizeInt = 0;
        Steinberg::int32 analysisHopSizeInt = 0;

        if (streamer.readInt32(residualFrameCount) &&
            streamer.readInt32(analysisFFTSizeInt) &&
            streamer.readInt32(analysisHopSizeInt) &&
            residualFrameCount > 0)
        {
            for (Steinberg::int32 f = 0; f < residualFrameCount; ++f)
            {
                for (size_t b = 0; b < 16; ++b)
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

    // --- M3 parameters (sidechain) ---
    {
        Steinberg::int32 inputSourceInt = 0;
        Steinberg::int32 latencyModeInt = 0;
        if (streamer.readInt32(inputSourceInt))
            setParamNormalized(kInputSourceId, inputSourceInt > 0 ? 1.0 : 0.0);
        if (streamer.readInt32(latencyModeInt))
            setParamNormalized(kLatencyModeId, latencyModeInt > 0 ? 1.0 : 0.0);
    }

    // --- M4 parameters (musical control) ---
    {
        Steinberg::int8 freezeState = 0;
        if (streamer.readInt8(freezeState))
            setParamNormalized(kFreezeId, freezeState ? 1.0 : 0.0);

        float morphPos = 0.0f;
        if (streamer.readFloat(morphPos))
            setParamNormalized(kMorphPositionId,
                static_cast<double>(std::clamp(morphPos, 0.0f, 1.0f)));

        Steinberg::int32 filterType = 0;
        if (streamer.readInt32(filterType))
            setParamNormalized(kHarmonicFilterTypeId,
                static_cast<double>(std::clamp(
                    static_cast<float>(filterType) / 4.0f, 0.0f, 1.0f)));

        float resp = 0.5f;
        if (streamer.readFloat(resp))
            setParamNormalized(kResponsivenessId,
                static_cast<double>(std::clamp(resp, 0.0f, 1.0f)));
    }

    // --- M5 parameters (harmonic memory) ---
    {
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
        for (int s = 0; s < 8; ++s)
        {
            Steinberg::int8 occupiedByte = 0;
            if (!streamer.readInt8(occupiedByte))
                break;

            if (occupiedByte != 0)
            {
                float skipFloat = 0.0f;
                Steinberg::int32 skipInt = 0;

                streamer.readFloat(skipFloat);  // f0Reference
                streamer.readInt32(skipInt);     // numPartials

                // 96*4 floats for per-partial arrays
                for (int i = 0; i < 96 * 4; ++i)
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

    // --- M6 parameters (creative extensions) ---
    {
        float m6Val = 0.0f;
        // v1 had timbralBlend here — read and discard to advance stream position
        if (version == 1)
            streamer.readFloat(m6Val);
        if (streamer.readFloat(m6Val))
            setParamNormalized(kStereoSpreadId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kEvolutionEnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParamNormalized(kEvolutionSpeedId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kEvolutionDepthId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kEvolutionModeId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1EnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1WaveformId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1RateId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1DepthId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1RangeStartId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1RangeEndId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod1TargetId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2EnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2WaveformId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2RateId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2DepthId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2RangeStartId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2RangeEndId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kMod2TargetId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kDetuneSpreadId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendEnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight1Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight2Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight3Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight4Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight5Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight6Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight7Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendSlotWeight8Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParamNormalized(kBlendLiveWeightId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
    }

    // --- Harmonic Physics parameters ---
    {
        float physVal = 0.0f;
        if (streamer.readFloat(physVal))
            setParamNormalized(kWarmthId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
        if (streamer.readFloat(physVal))
            setParamNormalized(kCouplingId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
        if (streamer.readFloat(physVal))
            setParamNormalized(kStabilityId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
        if (streamer.readFloat(physVal))
            setParamNormalized(kEntropyId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
    }

    // --- Analysis Feedback Loop parameters ---
    {
        float fbVal = 0.0f;
        if (streamer.readFloat(fbVal))
            setParamNormalized(kAnalysisFeedbackId, static_cast<double>(std::clamp(fbVal, 0.0f, 1.0f)));
        if (streamer.readFloat(fbVal))
            setParamNormalized(kAnalysisFeedbackDecayId, static_cast<double>(std::clamp(fbVal, 0.0f, 1.0f)));
    }

    // --- ADSR Envelope Detection parameters ---
    {
        constexpr double kMin = 1.0;
        constexpr double kMax = 5000.0;
        const double kLogRatio = std::log(kMax / kMin);
        auto logNorm = [&](double plainMs) -> double {
            double clamped = std::clamp(plainMs, kMin, kMax);
            return std::log(clamped / kMin) / kLogRatio;
        };

        float adsrVal = 0.0f;
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrAttackId, logNorm(static_cast<double>(adsrVal)));
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrDecayId, logNorm(static_cast<double>(adsrVal)));
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrSustainId, static_cast<double>(std::clamp(adsrVal, 0.0f, 1.0f)));
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrReleaseId, logNorm(static_cast<double>(adsrVal)));
        if (streamer.readFloat(adsrVal))
            setParamNormalized(kAdsrAmountId, static_cast<double>(std::clamp(adsrVal, 0.0f, 1.0f)));
        if (streamer.readFloat(adsrVal))
        {
            double norm = static_cast<double>(std::clamp(adsrVal, 0.25f, 4.0f) - 0.25f) / (4.0 - 0.25);
            setParamNormalized(kAdsrTimeScaleId, norm);
        }
        if (streamer.readFloat(adsrVal))
        {
            double norm = static_cast<double>(std::clamp(adsrVal, -1.0f, 1.0f) + 1.0f) / 2.0;
            setParamNormalized(kAdsrAttackCurveId, norm);
        }
        if (streamer.readFloat(adsrVal))
        {
            double norm = static_cast<double>(std::clamp(adsrVal, -1.0f, 1.0f) + 1.0f) / 2.0;
            setParamNormalized(kAdsrDecayCurveId, norm);
        }
        if (streamer.readFloat(adsrVal))
        {
            double norm = static_cast<double>(std::clamp(adsrVal, -1.0f, 1.0f) + 1.0f) / 2.0;
            setParamNormalized(kAdsrReleaseCurveId, norm);
        }

        // Skip per-slot ADSR data (controller does not store slot data)
        for (int s = 0; s < 8; ++s)
        {
            float skipFloat = 0.0f;
            for (int f = 0; f < 9; ++f)
                streamer.readFloat(skipFloat);
        }
    }

    // --- Partial Count parameter ---
    {
        float pcVal = 0.0f;
        if (streamer.readFloat(pcVal))
            setParamNormalized(kPartialCountId,
                static_cast<double>(std::clamp(pcVal, 0.0f, 1.0f)));
    }

    // --- Modulator Tempo Sync parameters (skip 4 floats) ---
    {
        float skipVal = 0.0f;
        streamer.readFloat(skipVal); // mod1RateSync
        streamer.readFloat(skipVal); // mod1NoteValue
        streamer.readFloat(skipVal); // mod2RateSync
        streamer.readFloat(skipVal); // mod2NoteValue
    }

    // --- Voice Mode parameter ---
    {
        float vmVal = 0.0f;
        if (streamer.readFloat(vmVal))
            setParamNormalized(kVoiceModeId,
                static_cast<double>(std::clamp(vmVal, 0.0f, 1.0f)));
    }

    // --- Physical Modelling parameters (Spec 127, graceful fallback for old states) ---
    {
        float pmVal = 0.0f;
        if (streamer.readFloat(pmVal))
            setParamNormalized(kPhysModelMixId,
                static_cast<double>(std::clamp(pmVal, 0.0f, 1.0f)));
        if (streamer.readFloat(pmVal))
        {
            // resonanceDecay_ is stored as plain seconds (0.01-5.0)
            // Reverse log mapping: norm = log(plain/0.01) / log(500)
            float plain = std::clamp(pmVal, 0.01f, 5.0f);
            float norm = std::log(plain / 0.01f) / std::log(500.0f);
            setParamNormalized(kResonanceDecayId,
                static_cast<double>(std::clamp(norm, 0.0f, 1.0f)));
        }
        if (streamer.readFloat(pmVal))
            setParamNormalized(kResonanceBrightnessId,
                static_cast<double>(std::clamp(pmVal, 0.0f, 1.0f)));
        if (streamer.readFloat(pmVal))
            setParamNormalized(kResonanceStretchId,
                static_cast<double>(std::clamp(pmVal, 0.0f, 1.0f)));
        if (streamer.readFloat(pmVal))
            setParamNormalized(kResonanceScatterId,
                static_cast<double>(std::clamp(pmVal, 0.0f, 1.0f)));
    }

    // --- Impact Exciter parameters (Spec 128, graceful fallback for old states) ---
    {
        float ieVal = 0.0f;
        if (streamer.readFloat(ieVal))
            setParamNormalized(kExciterTypeId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
        if (streamer.readFloat(ieVal))
            setParamNormalized(kImpactHardnessId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
        if (streamer.readFloat(ieVal))
            setParamNormalized(kImpactMassId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
        if (streamer.readFloat(ieVal))
            setParamNormalized(kImpactBrightnessId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
        if (streamer.readFloat(ieVal))
            setParamNormalized(kImpactPositionId,
                static_cast<double>(std::clamp(ieVal, 0.0f, 1.0f)));
    }

    // --- Waveguide String Resonance parameters (Spec 129, graceful fallback for old states) ---
    {
        float wgVal = 0.0f;
        if (streamer.readFloat(wgVal))
            setParamNormalized(kResonanceTypeId,
                static_cast<double>(std::clamp(wgVal, 0.0f, 1.0f)));
        if (streamer.readFloat(wgVal))
            setParamNormalized(kWaveguideStiffnessId,
                static_cast<double>(std::clamp(wgVal, 0.0f, 1.0f)));
        if (streamer.readFloat(wgVal))
            setParamNormalized(kWaveguidePickPositionId,
                static_cast<double>(std::clamp(wgVal, 0.0f, 1.0f)));
    }

    // --- Bow Exciter parameters (Spec 130, graceful fallback for old states) ---
    {
        float bowVal = 0.0f;
        if (streamer.readFloat(bowVal))
            setParamNormalized(kBowPressureId,
                static_cast<double>(std::clamp(bowVal, 0.0f, 1.0f)));
        if (streamer.readFloat(bowVal))
            setParamNormalized(kBowSpeedId,
                static_cast<double>(std::clamp(bowVal, 0.0f, 1.0f)));
        if (streamer.readFloat(bowVal))
            setParamNormalized(kBowPositionId,
                static_cast<double>(std::clamp(bowVal, 0.0f, 1.0f)));
        if (streamer.readFloat(bowVal))
            setParamNormalized(kBowOversamplingId,
                static_cast<double>(std::clamp(bowVal, 0.0f, 1.0f)));
    }

    // --- Body Resonance parameters (Spec 131, graceful fallback for old states) ---
    {
        float bodyVal = 0.0f;
        if (streamer.readFloat(bodyVal))
            setParamNormalized(kBodySizeId,
                static_cast<double>(std::clamp(bodyVal, 0.0f, 1.0f)));
        if (streamer.readFloat(bodyVal))
            setParamNormalized(kBodyMaterialId,
                static_cast<double>(std::clamp(bodyVal, 0.0f, 1.0f)));
        if (streamer.readFloat(bodyVal))
            setParamNormalized(kBodyMixId,
                static_cast<double>(std::clamp(bodyVal, 0.0f, 1.0f)));
    }

    // SharedDisplayBridge: try to read instance ID from state trailer
    {
        Steinberg::int32 marker = 0;
        Steinberg::int64 storedId = 0;
        if (streamer.readInt32(marker) && marker == kInstanceIdMarker
            && streamer.readInt64(storedId))
        {
            instanceId_ = static_cast<uint64_t>(storedId);
            sharedDisplay_ = static_cast<SharedDisplay*>(
                Krate::Plugins::SharedDisplayBridge::instance().lookupInstance(instanceId_));
            KRATE_BRIDGE_LOG("Innexus::Controller::setComponentState() — id=0x%llx, bridge=%s",
                static_cast<unsigned long long>(instanceId_),
                sharedDisplay_ ? "found" : "NOT found");
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
