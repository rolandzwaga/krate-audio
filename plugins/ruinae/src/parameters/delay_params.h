#pragma once
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
// Delay Parameter Struct
// =============================================================================

struct RuinaeDelayParams {
    // Common params (IDs 1600-1605)
    std::atomic<int> type{0};          // RuinaeDelayType (0-4)
    std::atomic<float> timeMs{500.0f}; // 1-5000 ms
    std::atomic<float> feedback{0.4f}; // 0-1.2
    std::atomic<float> mix{0.5f};      // 0-1
    std::atomic<bool> sync{true};      // default: synced
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};

    // Digital-specific (IDs 1606-1615)
    std::atomic<int> digitalEra{0};              // DigitalEra (0-2)
    std::atomic<float> digitalAge{0.0f};         // 0-1
    std::atomic<int> digitalLimiter{0};          // LimiterCharacter (0-2)
    std::atomic<float> digitalModDepth{0.0f};    // 0-1
    std::atomic<float> digitalModRateHz{1.0f};   // 0.1-10 Hz
    std::atomic<int> digitalModWaveform{0};      // Waveform (0-5)
    std::atomic<float> digitalWidth{100.0f};     // 0-200%
    std::atomic<float> digitalWavefoldAmt{0.0f}; // 0-100%
    std::atomic<int> digitalWavefoldModel{0};    // WavefolderModel (0-3)
    std::atomic<float> digitalWavefoldSym{0.0f}; // -1 to +1

    // Tape-specific (IDs 1626-1640)
    std::atomic<float> tapeInertiaMs{300.0f};    // 100-1000 ms
    std::atomic<float> tapeWear{0.0f};           // 0-1
    std::atomic<float> tapeSaturation{0.5f};     // 0-1
    std::atomic<float> tapeAge{0.0f};            // 0-1
    std::atomic<bool> tapeSpliceEnabled{false};
    std::atomic<float> tapeSpliceIntensity{0.0f}; // 0-1
    std::atomic<bool> tapeHead1Enabled{true};
    std::atomic<float> tapeHead1Level{0.0f};     // -96 to +6 dB
    std::atomic<float> tapeHead1Pan{0.0f};       // -100 to +100
    std::atomic<bool> tapeHead2Enabled{true};
    std::atomic<float> tapeHead2Level{0.0f};     // -96 to +6 dB
    std::atomic<float> tapeHead2Pan{0.0f};       // -100 to +100
    std::atomic<bool> tapeHead3Enabled{true};
    std::atomic<float> tapeHead3Level{0.0f};     // -96 to +6 dB
    std::atomic<float> tapeHead3Pan{0.0f};       // -100 to +100

    // Granular-specific (IDs 1646-1658)
    std::atomic<float> granularSizeMs{100.0f};   // 10-500 ms
    std::atomic<float> granularDensity{10.0f};   // 1-100 grains/s
    std::atomic<float> granularPitch{0.0f};      // -24 to +24 st
    std::atomic<float> granularPitchSpray{0.0f}; // 0-1
    std::atomic<int> granularPitchQuant{0};      // PitchQuantMode (0-4)
    std::atomic<float> granularPosSpray{0.0f};   // 0-1
    std::atomic<float> granularReverseProb{0.0f}; // 0-1
    std::atomic<float> granularPanSpray{0.0f};   // 0-1
    std::atomic<float> granularJitter{0.0f};     // 0-1
    std::atomic<float> granularTexture{0.0f};    // 0-1
    std::atomic<float> granularWidth{1.0f};      // 0-1
    std::atomic<int> granularEnvelope{0};        // GrainEnvelopeType (0-5)
    std::atomic<bool> granularFreeze{false};

    // Spectral-specific (IDs 1666-1673)
    std::atomic<int> spectralFFTSize{1};         // dropdown index (0-3), default 1 = 1024
    std::atomic<float> spectralSpreadMs{0.0f};   // 0-2000 ms
    std::atomic<int> spectralDirection{0};       // SpreadDirection (0-2)
    std::atomic<int> spectralCurve{0};           // SpreadCurve (0-1)
    std::atomic<float> spectralTilt{0.0f};       // -1 to +1
    std::atomic<float> spectralDiffusion{0.0f};  // 0-1
    std::atomic<float> spectralWidth{0.0f};      // 0-1
    std::atomic<bool> spectralFreeze{false};

    // PingPong-specific (IDs 1686-1690)
    std::atomic<int> pingPongRatio{0};           // LRRatio (0-6)
    std::atomic<float> pingPongCrossFeed{1.0f};  // 0-1
    std::atomic<float> pingPongWidth{100.0f};    // 0-200%
    std::atomic<float> pingPongModDepth{0.0f};   // 0-1
    std::atomic<float> pingPongModRateHz{1.0f};  // 0.1-10 Hz
};

// =============================================================================
// Parameter Change Handler (denormalization)
// =============================================================================

inline void handleDelayParamChange(
    RuinaeDelayParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        // --- Common ---
        case kDelayTypeId:
            params.type.store(
                std::clamp(static_cast<int>(value * (kDelayTypeCount - 1) + 0.5), 0, kDelayTypeCount - 1),
                std::memory_order_relaxed); break;
        case kDelayTimeId:
            params.timeMs.store(
                std::clamp(static_cast<float>(1.0 + value * 4999.0), 1.0f, 5000.0f),
                std::memory_order_relaxed); break;
        case kDelayFeedbackId:
            params.feedback.store(
                std::clamp(static_cast<float>(value * 1.2), 0.0f, 1.2f),
                std::memory_order_relaxed); break;
        case kDelayMixId:
            params.mix.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelaySyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        case kDelayNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed); break;

        // --- Digital ---
        case kDelayDigitalEraId:
            params.digitalEra.store(
                std::clamp(static_cast<int>(value * (kDigitalEraCount - 1) + 0.5), 0, kDigitalEraCount - 1),
                std::memory_order_relaxed); break;
        case kDelayDigitalAgeId:
            params.digitalAge.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayDigitalLimiterId:
            params.digitalLimiter.store(
                std::clamp(static_cast<int>(value * (kLimiterCharacterCount - 1) + 0.5), 0, kLimiterCharacterCount - 1),
                std::memory_order_relaxed); break;
        case kDelayDigitalModDepthId:
            params.digitalModDepth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayDigitalModRateId:
            // 0-1 -> 0.1-10 Hz
            params.digitalModRateHz.store(
                std::clamp(static_cast<float>(0.1 + value * 9.9), 0.1f, 10.0f),
                std::memory_order_relaxed); break;
        case kDelayDigitalModWaveformId:
            params.digitalModWaveform.store(
                std::clamp(static_cast<int>(value * (kWaveformCount - 1) + 0.5), 0, kWaveformCount - 1),
                std::memory_order_relaxed); break;
        case kDelayDigitalWidthId:
            // 0-1 -> 0-200%
            params.digitalWidth.store(
                std::clamp(static_cast<float>(value * 200.0), 0.0f, 200.0f),
                std::memory_order_relaxed); break;
        case kDelayDigitalWavefoldAmountId:
            // 0-1 -> 0-100%
            params.digitalWavefoldAmt.store(
                std::clamp(static_cast<float>(value * 100.0), 0.0f, 100.0f),
                std::memory_order_relaxed); break;
        case kDelayDigitalWavefoldModelId:
            params.digitalWavefoldModel.store(
                std::clamp(static_cast<int>(value * (kWavefolderModelCount - 1) + 0.5), 0, kWavefolderModelCount - 1),
                std::memory_order_relaxed); break;
        case kDelayDigitalWavefoldSymmetryId:
            // 0-1 -> -1 to +1
            params.digitalWavefoldSym.store(
                std::clamp(static_cast<float>(value * 2.0 - 1.0), -1.0f, 1.0f),
                std::memory_order_relaxed); break;

        // --- Tape ---
        case kDelayTapeMotorInertiaId:
            // 0-1 -> 100-1000 ms
            params.tapeInertiaMs.store(
                std::clamp(static_cast<float>(100.0 + value * 900.0), 100.0f, 1000.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeWearId:
            params.tapeWear.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeSaturationId:
            params.tapeSaturation.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeAgeId:
            params.tapeAge.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeSpliceEnabledId:
            params.tapeSpliceEnabled.store(value >= 0.5, std::memory_order_relaxed); break;
        case kDelayTapeSpliceIntensityId:
            params.tapeSpliceIntensity.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeHead1EnabledId:
            params.tapeHead1Enabled.store(value >= 0.5, std::memory_order_relaxed); break;
        case kDelayTapeHead1LevelId:
            // 0-1 -> -96 to +6 dB
            params.tapeHead1Level.store(
                std::clamp(static_cast<float>(-96.0 + value * 102.0), -96.0f, 6.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeHead1PanId:
            // 0-1 -> -100 to +100
            params.tapeHead1Pan.store(
                std::clamp(static_cast<float>(value * 200.0 - 100.0), -100.0f, 100.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeHead2EnabledId:
            params.tapeHead2Enabled.store(value >= 0.5, std::memory_order_relaxed); break;
        case kDelayTapeHead2LevelId:
            params.tapeHead2Level.store(
                std::clamp(static_cast<float>(-96.0 + value * 102.0), -96.0f, 6.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeHead2PanId:
            params.tapeHead2Pan.store(
                std::clamp(static_cast<float>(value * 200.0 - 100.0), -100.0f, 100.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeHead3EnabledId:
            params.tapeHead3Enabled.store(value >= 0.5, std::memory_order_relaxed); break;
        case kDelayTapeHead3LevelId:
            params.tapeHead3Level.store(
                std::clamp(static_cast<float>(-96.0 + value * 102.0), -96.0f, 6.0f),
                std::memory_order_relaxed); break;
        case kDelayTapeHead3PanId:
            params.tapeHead3Pan.store(
                std::clamp(static_cast<float>(value * 200.0 - 100.0), -100.0f, 100.0f),
                std::memory_order_relaxed); break;

        // --- Granular ---
        case kDelayGranularSizeId:
            // 0-1 -> 10-500 ms
            params.granularSizeMs.store(
                std::clamp(static_cast<float>(10.0 + value * 490.0), 10.0f, 500.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularDensityId:
            // 0-1 -> 1-100
            params.granularDensity.store(
                std::clamp(static_cast<float>(1.0 + value * 99.0), 1.0f, 100.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularPitchId:
            // 0-1 -> -24 to +24
            params.granularPitch.store(
                std::clamp(static_cast<float>(value * 48.0 - 24.0), -24.0f, 24.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularPitchSprayId:
            params.granularPitchSpray.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularPitchQuantId:
            params.granularPitchQuant.store(
                std::clamp(static_cast<int>(value * (kPitchQuantModeCount - 1) + 0.5), 0, kPitchQuantModeCount - 1),
                std::memory_order_relaxed); break;
        case kDelayGranularPositionSprayId:
            params.granularPosSpray.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularReverseProbId:
            params.granularReverseProb.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularPanSprayId:
            params.granularPanSpray.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularJitterId:
            params.granularJitter.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularTextureId:
            params.granularTexture.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularWidthId:
            params.granularWidth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayGranularEnvelopeId:
            params.granularEnvelope.store(
                std::clamp(static_cast<int>(value * (kGrainEnvelopeCount - 1) + 0.5), 0, kGrainEnvelopeCount - 1),
                std::memory_order_relaxed); break;
        case kDelayGranularFreezeId:
            params.granularFreeze.store(value >= 0.5, std::memory_order_relaxed); break;

        // --- Spectral ---
        case kDelaySpectralFFTSizeId:
            params.spectralFFTSize.store(
                std::clamp(static_cast<int>(value * (kFFTSizeCount - 1) + 0.5), 0, kFFTSizeCount - 1),
                std::memory_order_relaxed); break;
        case kDelaySpectralSpreadId:
            // 0-1 -> 0-2000 ms
            params.spectralSpreadMs.store(
                std::clamp(static_cast<float>(value * 2000.0), 0.0f, 2000.0f),
                std::memory_order_relaxed); break;
        case kDelaySpectralDirectionId:
            params.spectralDirection.store(
                std::clamp(static_cast<int>(value * (kSpreadDirectionCount - 1) + 0.5), 0, kSpreadDirectionCount - 1),
                std::memory_order_relaxed); break;
        case kDelaySpectralCurveId:
            params.spectralCurve.store(
                std::clamp(static_cast<int>(value * (kSpreadCurveCount - 1) + 0.5), 0, kSpreadCurveCount - 1),
                std::memory_order_relaxed); break;
        case kDelaySpectralTiltId:
            // 0-1 -> -1 to +1
            params.spectralTilt.store(
                std::clamp(static_cast<float>(value * 2.0 - 1.0), -1.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelaySpectralDiffusionId:
            params.spectralDiffusion.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelaySpectralWidthId:
            params.spectralWidth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelaySpectralFreezeId:
            params.spectralFreeze.store(value >= 0.5, std::memory_order_relaxed); break;

        // --- PingPong ---
        case kDelayPingPongRatioId:
            params.pingPongRatio.store(
                std::clamp(static_cast<int>(value * (kLRRatioCount - 1) + 0.5), 0, kLRRatioCount - 1),
                std::memory_order_relaxed); break;
        case kDelayPingPongCrossFeedId:
            params.pingPongCrossFeed.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayPingPongWidthId:
            // 0-1 -> 0-200%
            params.pingPongWidth.store(
                std::clamp(static_cast<float>(value * 200.0), 0.0f, 200.0f),
                std::memory_order_relaxed); break;
        case kDelayPingPongModDepthId:
            params.pingPongModDepth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelayPingPongModRateId:
            // 0-1 -> 0.1-10 Hz
            params.pingPongModRateHz.store(
                std::clamp(static_cast<float>(0.1 + value * 9.9), 0.1f, 10.0f),
                std::memory_order_relaxed); break;

        default: break;
    }
}

// =============================================================================
// Delay Parameter Registration
// =============================================================================

inline void registerDelayParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // --- Common ---
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Type"), kDelayTypeId,
        {STR16("Digital"), STR16("Tape"), STR16("Ping Pong"),
         STR16("Granular"), STR16("Spectral")}
    ));
    parameters.addParameter(STR16("Delay Time"), STR16("ms"), 0, 0.100,
        ParameterInfo::kCanAutomate, kDelayTimeId);
    parameters.addParameter(STR16("Delay Feedback"), STR16("%"), 0, 0.333,
        ParameterInfo::kCanAutomate, kDelayFeedbackId);
    parameters.addParameter(STR16("Delay Mix"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelayMixId);
    parameters.addParameter(STR16("Delay Sync"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kDelaySyncId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("Delay Note Value"), kDelayNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // --- Digital ---
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Era"), kDelayDigitalEraId,
        {STR16("Pristine"), STR16("80s Digital"), STR16("Lo-Fi")}));
    parameters.addParameter(STR16("Delay Age"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayDigitalAgeId);
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Limiter"), kDelayDigitalLimiterId,
        {STR16("Soft"), STR16("Medium"), STR16("Hard")}));
    parameters.addParameter(STR16("Delay Mod Depth"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayDigitalModDepthId);
    parameters.addParameter(STR16("Delay Mod Rate"), STR16("Hz"), 0, 0.091,
        ParameterInfo::kCanAutomate, kDelayDigitalModRateId);  // default ~1 Hz
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Mod Waveform"), kDelayDigitalModWaveformId,
        {STR16("Sine"), STR16("Triangle"), STR16("Sawtooth"),
         STR16("Square"), STR16("S&H"), STR16("Smooth Rnd")}));
    parameters.addParameter(STR16("Delay Width"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelayDigitalWidthId);  // default 100%
    parameters.addParameter(STR16("Delay Wavefold"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayDigitalWavefoldAmountId);
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Fold Model"), kDelayDigitalWavefoldModelId,
        {STR16("Simple"), STR16("Serge"), STR16("Buchla 259"), STR16("Lockhart")}));
    parameters.addParameter(STR16("Delay Fold Sym"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelayDigitalWavefoldSymmetryId);  // default 0 (center)

    // --- Tape ---
    parameters.addParameter(STR16("Delay Inertia"), STR16("ms"), 0, 0.222,
        ParameterInfo::kCanAutomate, kDelayTapeMotorInertiaId);  // default ~300ms
    parameters.addParameter(STR16("Delay Wear"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayTapeWearId);
    parameters.addParameter(STR16("Delay Saturation"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelayTapeSaturationId);
    parameters.addParameter(STR16("Delay Tape Age"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayTapeAgeId);
    parameters.addParameter(STR16("Delay Splice"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kDelayTapeSpliceEnabledId);
    parameters.addParameter(STR16("Delay Splice Int"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayTapeSpliceIntensityId);
    // Head 1
    parameters.addParameter(STR16("Delay Head 1"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kDelayTapeHead1EnabledId);
    parameters.addParameter(STR16("Delay Head 1 Lvl"), STR16("dB"), 0, 0.941,
        ParameterInfo::kCanAutomate, kDelayTapeHead1LevelId);  // default 0 dB
    parameters.addParameter(STR16("Delay Head 1 Pan"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelayTapeHead1PanId);  // default center
    // Head 2
    parameters.addParameter(STR16("Delay Head 2"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kDelayTapeHead2EnabledId);
    parameters.addParameter(STR16("Delay Head 2 Lvl"), STR16("dB"), 0, 0.941,
        ParameterInfo::kCanAutomate, kDelayTapeHead2LevelId);
    parameters.addParameter(STR16("Delay Head 2 Pan"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelayTapeHead2PanId);
    // Head 3
    parameters.addParameter(STR16("Delay Head 3"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kDelayTapeHead3EnabledId);
    parameters.addParameter(STR16("Delay Head 3 Lvl"), STR16("dB"), 0, 0.941,
        ParameterInfo::kCanAutomate, kDelayTapeHead3LevelId);
    parameters.addParameter(STR16("Delay Head 3 Pan"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelayTapeHead3PanId);

    // --- Granular ---
    parameters.addParameter(STR16("Delay Grain Size"), STR16("ms"), 0, 0.184,
        ParameterInfo::kCanAutomate, kDelayGranularSizeId);  // default ~100ms
    parameters.addParameter(STR16("Delay Density"), STR16("g/s"), 0, 0.091,
        ParameterInfo::kCanAutomate, kDelayGranularDensityId);  // default ~10
    parameters.addParameter(STR16("Delay Pitch"), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelayGranularPitchId);  // default 0 st
    parameters.addParameter(STR16("Delay Pitch Spray"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayGranularPitchSprayId);
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Pitch Quant"), kDelayGranularPitchQuantId,
        {STR16("Off"), STR16("Semitones"), STR16("Octaves"),
         STR16("Fifths"), STR16("Scale")}));
    parameters.addParameter(STR16("Delay Pos Spray"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayGranularPositionSprayId);
    parameters.addParameter(STR16("Delay Reverse"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayGranularReverseProbId);
    parameters.addParameter(STR16("Delay Pan Spray"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayGranularPanSprayId);
    parameters.addParameter(STR16("Delay Jitter"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayGranularJitterId);
    parameters.addParameter(STR16("Delay Texture"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayGranularTextureId);
    parameters.addParameter(STR16("Delay Gr Width"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kDelayGranularWidthId);
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Envelope"), kDelayGranularEnvelopeId,
        {STR16("Hann"), STR16("Trapezoid"), STR16("Sine"),
         STR16("Blackman"), STR16("Linear"), STR16("Exponential")}));
    parameters.addParameter(STR16("Delay Gr Freeze"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kDelayGranularFreezeId);

    // --- Spectral ---
    parameters.addParameter(createDropdownParameter(
        STR16("Delay FFT Size"), kDelaySpectralFFTSizeId,
        {STR16("512"), STR16("1024"), STR16("2048"), STR16("4096")}));
    parameters.addParameter(STR16("Delay Spread"), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelaySpectralSpreadId);
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Spread Dir"), kDelaySpectralDirectionId,
        {STR16("Low > High"), STR16("High > Low"), STR16("Center Out")}));
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Spread Curve"), kDelaySpectralCurveId,
        {STR16("Linear"), STR16("Logarithmic")}));
    parameters.addParameter(STR16("Delay Tilt"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelaySpectralTiltId);  // default 0 (center)
    parameters.addParameter(STR16("Delay Diffusion"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelaySpectralDiffusionId);
    parameters.addParameter(STR16("Delay Sp Width"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelaySpectralWidthId);
    parameters.addParameter(STR16("Delay Sp Freeze"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kDelaySpectralFreezeId);

    // --- PingPong ---
    parameters.addParameter(createDropdownParameter(
        STR16("Delay L/R Ratio"), kDelayPingPongRatioId,
        {STR16("1:1"), STR16("2:1"), STR16("3:2"), STR16("4:3"),
         STR16("1:2"), STR16("2:3"), STR16("3:4")}));
    parameters.addParameter(STR16("Delay Cross Feed"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kDelayPingPongCrossFeedId);
    parameters.addParameter(STR16("Delay PP Width"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kDelayPingPongWidthId);  // default 100%
    parameters.addParameter(STR16("Delay PP Mod Depth"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayPingPongModDepthId);
    parameters.addParameter(STR16("Delay PP Mod Rate"), STR16("Hz"), 0, 0.091,
        ParameterInfo::kCanAutomate, kDelayPingPongModRateId);  // default ~1 Hz
}

// =============================================================================
// Display Formatting
// =============================================================================

inline Steinberg::tresult formatDelayParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kDelayTimeId: {
            float ms = static_cast<float>(1.0 + value * 4999.0);
            if (ms >= 1000.0f) snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDelayFeedbackId:
            snprintf(text, sizeof(text), "%.0f%%", value * 120.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;

        // Percentage (0-100%) params
        case kDelayMixId:
        case kDelayDigitalAgeId:
        case kDelayDigitalModDepthId:
        case kDelayTapeWearId:
        case kDelayTapeSaturationId:
        case kDelayTapeAgeId:
        case kDelayTapeSpliceIntensityId:
        case kDelayGranularPitchSprayId:
        case kDelayGranularPositionSprayId:
        case kDelayGranularReverseProbId:
        case kDelayGranularPanSprayId:
        case kDelayGranularJitterId:
        case kDelayGranularTextureId:
        case kDelayGranularWidthId:
        case kDelaySpectralDiffusionId:
        case kDelaySpectralWidthId:
        case kDelayPingPongCrossFeedId:
        case kDelayPingPongModDepthId:
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;

        // Width params (0-200%)
        case kDelayDigitalWidthId:
        case kDelayPingPongWidthId:
            snprintf(text, sizeof(text), "%.0f%%", value * 200.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;

        // Wavefold amount (0-100%)
        case kDelayDigitalWavefoldAmountId:
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;

        // Mod rate (0.1-10 Hz)
        case kDelayDigitalModRateId:
        case kDelayPingPongModRateId: {
            float hz = static_cast<float>(0.1 + value * 9.9);
            snprintf(text, sizeof(text), "%.1f Hz", hz);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // Bipolar symmetry/tilt (-100% to +100%)
        case kDelayDigitalWavefoldSymmetryId:
        case kDelaySpectralTiltId:
            snprintf(text, sizeof(text), "%+.0f%%", (value * 2.0 - 1.0) * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;

        // Tape motor inertia (100-1000 ms)
        case kDelayTapeMotorInertiaId: {
            float ms = static_cast<float>(100.0 + value * 900.0);
            snprintf(text, sizeof(text), "%.0f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // Head level (-96 to +6 dB)
        case kDelayTapeHead1LevelId:
        case kDelayTapeHead2LevelId:
        case kDelayTapeHead3LevelId: {
            float dB = static_cast<float>(-96.0 + value * 102.0);
            snprintf(text, sizeof(text), "%.1f dB", dB);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // Head pan (-100 to +100)
        case kDelayTapeHead1PanId:
        case kDelayTapeHead2PanId:
        case kDelayTapeHead3PanId: {
            float pan = static_cast<float>(value * 200.0 - 100.0);
            if (pan < -0.5f) snprintf(text, sizeof(text), "L%.0f", -pan);
            else if (pan > 0.5f) snprintf(text, sizeof(text), "R%.0f", pan);
            else snprintf(text, sizeof(text), "C");
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // Grain size (10-500 ms)
        case kDelayGranularSizeId: {
            float ms = static_cast<float>(10.0 + value * 490.0);
            snprintf(text, sizeof(text), "%.0f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // Grain density (1-100)
        case kDelayGranularDensityId: {
            float d = static_cast<float>(1.0 + value * 99.0);
            snprintf(text, sizeof(text), "%.0f g/s", d);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // Pitch (-24 to +24 semitones)
        case kDelayGranularPitchId: {
            float st = static_cast<float>(value * 48.0 - 24.0);
            snprintf(text, sizeof(text), "%+.1f st", st);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // Spectral spread (0-2000 ms)
        case kDelaySpectralSpreadId: {
            float ms = static_cast<float>(value * 2000.0);
            snprintf(text, sizeof(text), "%.0f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        default: break;
    }
    return kResultFalse;
}

// =============================================================================
// State Save/Load — Base (v1-v8 compatible)
// =============================================================================

inline void saveDelayParamsBase(const RuinaeDelayParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.timeMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
}

inline bool loadDelayParams(RuinaeDelayParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (!streamer.readInt32(iv)) { return false; } params.type.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.timeMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.feedback.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.mix.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.sync.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.noteValue.store(iv, std::memory_order_relaxed);
    return true;
}

// =============================================================================
// State Save/Load — v9+ (type-specific parameters)
// =============================================================================

inline void saveDelayParams(const RuinaeDelayParams& params, Steinberg::IBStreamer& streamer) {
    saveDelayParamsBase(params, streamer);

    // Digital
    streamer.writeInt32(params.digitalEra.load(std::memory_order_relaxed));
    streamer.writeFloat(params.digitalAge.load(std::memory_order_relaxed));
    streamer.writeInt32(params.digitalLimiter.load(std::memory_order_relaxed));
    streamer.writeFloat(params.digitalModDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.digitalModRateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.digitalModWaveform.load(std::memory_order_relaxed));
    streamer.writeFloat(params.digitalWidth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.digitalWavefoldAmt.load(std::memory_order_relaxed));
    streamer.writeInt32(params.digitalWavefoldModel.load(std::memory_order_relaxed));
    streamer.writeFloat(params.digitalWavefoldSym.load(std::memory_order_relaxed));

    // Tape
    streamer.writeFloat(params.tapeInertiaMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tapeWear.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tapeSaturation.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tapeAge.load(std::memory_order_relaxed));
    streamer.writeInt32(params.tapeSpliceEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.tapeSpliceIntensity.load(std::memory_order_relaxed));
    streamer.writeInt32(params.tapeHead1Enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.tapeHead1Level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tapeHead1Pan.load(std::memory_order_relaxed));
    streamer.writeInt32(params.tapeHead2Enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.tapeHead2Level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tapeHead2Pan.load(std::memory_order_relaxed));
    streamer.writeInt32(params.tapeHead3Enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.tapeHead3Level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tapeHead3Pan.load(std::memory_order_relaxed));

    // Granular
    streamer.writeFloat(params.granularSizeMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularDensity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularPitch.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularPitchSpray.load(std::memory_order_relaxed));
    streamer.writeInt32(params.granularPitchQuant.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularPosSpray.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularReverseProb.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularPanSpray.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularJitter.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularTexture.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularWidth.load(std::memory_order_relaxed));
    streamer.writeInt32(params.granularEnvelope.load(std::memory_order_relaxed));
    streamer.writeInt32(params.granularFreeze.load(std::memory_order_relaxed) ? 1 : 0);

    // Spectral
    streamer.writeInt32(params.spectralFFTSize.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spectralSpreadMs.load(std::memory_order_relaxed));
    streamer.writeInt32(params.spectralDirection.load(std::memory_order_relaxed));
    streamer.writeInt32(params.spectralCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spectralTilt.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spectralDiffusion.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spectralWidth.load(std::memory_order_relaxed));
    streamer.writeInt32(params.spectralFreeze.load(std::memory_order_relaxed) ? 1 : 0);

    // PingPong
    streamer.writeInt32(params.pingPongRatio.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pingPongCrossFeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pingPongWidth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pingPongModDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pingPongModRateHz.load(std::memory_order_relaxed));
}

inline bool loadDelayParamsV9(RuinaeDelayParams& params, Steinberg::IBStreamer& streamer) {
    if (!loadDelayParams(params, streamer)) return false;

    Steinberg::int32 iv = 0; float fv = 0.0f;

    // Digital
    if (!streamer.readInt32(iv)) return false;
    params.digitalEra.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.digitalAge.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.digitalLimiter.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.digitalModDepth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.digitalModRateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.digitalModWaveform.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.digitalWidth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.digitalWavefoldAmt.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.digitalWavefoldModel.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.digitalWavefoldSym.store(fv, std::memory_order_relaxed);

    // Tape
    if (!streamer.readFloat(fv)) return false;
    params.tapeInertiaMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeWear.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeSaturation.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeAge.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.tapeSpliceEnabled.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeSpliceIntensity.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.tapeHead1Enabled.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeHead1Level.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeHead1Pan.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.tapeHead2Enabled.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeHead2Level.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeHead2Pan.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.tapeHead3Enabled.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeHead3Level.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.tapeHead3Pan.store(fv, std::memory_order_relaxed);

    // Granular
    if (!streamer.readFloat(fv)) return false;
    params.granularSizeMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.granularDensity.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.granularPitch.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.granularPitchSpray.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.granularPitchQuant.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.granularPosSpray.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.granularReverseProb.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.granularPanSpray.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.granularJitter.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.granularTexture.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.granularWidth.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.granularEnvelope.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.granularFreeze.store(iv != 0, std::memory_order_relaxed);

    // Spectral
    if (!streamer.readInt32(iv)) return false;
    params.spectralFFTSize.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.spectralSpreadMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.spectralDirection.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.spectralCurve.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.spectralTilt.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.spectralDiffusion.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.spectralWidth.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return false;
    params.spectralFreeze.store(iv != 0, std::memory_order_relaxed);

    // PingPong
    if (!streamer.readInt32(iv)) return false;
    params.pingPongRatio.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.pingPongCrossFeed.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.pingPongWidth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.pingPongModDepth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) return false;
    params.pingPongModRateHz.store(fv, std::memory_order_relaxed);

    return true;
}

// =============================================================================
// Controller State Restore
// =============================================================================

template<typename SetParamFunc>
inline void loadDelayParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (streamer.readInt32(iv)) setParam(kDelayTypeId, static_cast<double>(iv) / (kDelayTypeCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelayTimeId, static_cast<double>((fv - 1.0f) / 4999.0f));
    if (streamer.readFloat(fv)) setParam(kDelayFeedbackId, static_cast<double>(fv / 1.2f));
    if (streamer.readFloat(fv)) setParam(kDelayMixId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kDelaySyncId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kDelayNoteValueId, static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
}

template<typename SetParamFunc>
inline void loadDelayParamsToControllerV9(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    // Read base fields first
    loadDelayParamsToController(streamer, setParam);

    Steinberg::int32 iv = 0; float fv = 0.0f;

    // Digital
    if (streamer.readInt32(iv)) setParam(kDelayDigitalEraId, static_cast<double>(iv) / (kDigitalEraCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelayDigitalAgeId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kDelayDigitalLimiterId, static_cast<double>(iv) / (kLimiterCharacterCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelayDigitalModDepthId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayDigitalModRateId, static_cast<double>((fv - 0.1f) / 9.9f));
    if (streamer.readInt32(iv)) setParam(kDelayDigitalModWaveformId, static_cast<double>(iv) / (kWaveformCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelayDigitalWidthId, static_cast<double>(fv / 200.0f));
    if (streamer.readFloat(fv)) setParam(kDelayDigitalWavefoldAmountId, static_cast<double>(fv / 100.0f));
    if (streamer.readInt32(iv)) setParam(kDelayDigitalWavefoldModelId, static_cast<double>(iv) / (kWavefolderModelCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelayDigitalWavefoldSymmetryId, static_cast<double>((fv + 1.0f) / 2.0f));

    // Tape
    if (streamer.readFloat(fv)) setParam(kDelayTapeMotorInertiaId, static_cast<double>((fv - 100.0f) / 900.0f));
    if (streamer.readFloat(fv)) setParam(kDelayTapeWearId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayTapeSaturationId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayTapeAgeId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kDelayTapeSpliceEnabledId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readFloat(fv)) setParam(kDelayTapeSpliceIntensityId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kDelayTapeHead1EnabledId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readFloat(fv)) setParam(kDelayTapeHead1LevelId, static_cast<double>((fv + 96.0f) / 102.0f));
    if (streamer.readFloat(fv)) setParam(kDelayTapeHead1PanId, static_cast<double>((fv + 100.0f) / 200.0f));
    if (streamer.readInt32(iv)) setParam(kDelayTapeHead2EnabledId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readFloat(fv)) setParam(kDelayTapeHead2LevelId, static_cast<double>((fv + 96.0f) / 102.0f));
    if (streamer.readFloat(fv)) setParam(kDelayTapeHead2PanId, static_cast<double>((fv + 100.0f) / 200.0f));
    if (streamer.readInt32(iv)) setParam(kDelayTapeHead3EnabledId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readFloat(fv)) setParam(kDelayTapeHead3LevelId, static_cast<double>((fv + 96.0f) / 102.0f));
    if (streamer.readFloat(fv)) setParam(kDelayTapeHead3PanId, static_cast<double>((fv + 100.0f) / 200.0f));

    // Granular
    if (streamer.readFloat(fv)) setParam(kDelayGranularSizeId, static_cast<double>((fv - 10.0f) / 490.0f));
    if (streamer.readFloat(fv)) setParam(kDelayGranularDensityId, static_cast<double>((fv - 1.0f) / 99.0f));
    if (streamer.readFloat(fv)) setParam(kDelayGranularPitchId, static_cast<double>((fv + 24.0f) / 48.0f));
    if (streamer.readFloat(fv)) setParam(kDelayGranularPitchSprayId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kDelayGranularPitchQuantId, static_cast<double>(iv) / (kPitchQuantModeCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelayGranularPositionSprayId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayGranularReverseProbId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayGranularPanSprayId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayGranularJitterId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayGranularTextureId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayGranularWidthId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kDelayGranularEnvelopeId, static_cast<double>(iv) / (kGrainEnvelopeCount - 1));
    if (streamer.readInt32(iv)) setParam(kDelayGranularFreezeId, iv != 0 ? 1.0 : 0.0);

    // Spectral
    if (streamer.readInt32(iv)) setParam(kDelaySpectralFFTSizeId, static_cast<double>(iv) / (kFFTSizeCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelaySpectralSpreadId, static_cast<double>(fv / 2000.0f));
    if (streamer.readInt32(iv)) setParam(kDelaySpectralDirectionId, static_cast<double>(iv) / (kSpreadDirectionCount - 1));
    if (streamer.readInt32(iv)) setParam(kDelaySpectralCurveId, static_cast<double>(iv) / (kSpreadCurveCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelaySpectralTiltId, static_cast<double>((fv + 1.0f) / 2.0f));
    if (streamer.readFloat(fv)) setParam(kDelaySpectralDiffusionId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelaySpectralWidthId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kDelaySpectralFreezeId, iv != 0 ? 1.0 : 0.0);

    // PingPong
    if (streamer.readInt32(iv)) setParam(kDelayPingPongRatioId, static_cast<double>(iv) / (kLRRatioCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelayPingPongCrossFeedId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayPingPongWidthId, static_cast<double>(fv / 200.0f));
    if (streamer.readFloat(fv)) setParam(kDelayPingPongModDepthId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kDelayPingPongModRateId, static_cast<double>((fv - 0.1f) / 9.9f));
}

} // namespace Ruinae
