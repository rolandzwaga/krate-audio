// ==============================================================================
// Processor Parameter Changes
// ==============================================================================

#include "processor.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>

namespace Innexus {

// ==============================================================================
// Process Parameter Changes
// ==============================================================================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- accesses atomic members via includes clang-tidy can't resolve
void Processor::processParameterChanges(
    Steinberg::Vst::IParameterChanges* changes)
{
    if (!changes)
        return;

    auto numParams = changes->getParameterCount();
    for (Steinberg::int32 i = 0; i < numParams; ++i)
    {
        auto* paramQueue = changes->getParameterData(i);
        if (!paramQueue)
            continue;

        Steinberg::Vst::ParamValue value;
        Steinberg::int32 sampleOffset;
        auto numPoints = paramQueue->getPointCount();
        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) ==
            Steinberg::kResultTrue)
        {
            switch (paramQueue->getParameterId())
            {
            case kBypassId:
                bypass_.store(static_cast<float>(value));
                break;
            case kMasterGainId:
                masterGain_.store(static_cast<float>(value));
                break;
            case kReleaseTimeId:
            {
                // Exponential mapping: 20ms to 5000ms
                constexpr float kMinMs = 20.0f;
                constexpr float kMaxMs = 5000.0f;
                constexpr float kRatio = kMaxMs / kMinMs;
                auto timeMs = kMinMs * std::pow(kRatio, static_cast<float>(value));
                releaseTimeMs_.store(std::clamp(timeMs, kMinMs, kMaxMs));
                break;
            }
            case kInharmonicityAmountId:
                inharmonicityAmount_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kHarmonicLevelId:
                harmonicLevel_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kResidualLevelId:
                residualLevel_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kResidualBrightnessId:
                residualBrightness_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kTransientEmphasisId:
                transientEmphasis_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kInputSourceId:
                inputSource_.store(
                    static_cast<float>(value) > 0.5f ? 1.0f : 0.0f);
                break;
            case kLatencyModeId:
            {
                auto newMode = static_cast<float>(value) > 0.5f
                    ? LatencyMode::HighPrecision
                    : LatencyMode::LowLatency;
                latencyMode_.store(
                    static_cast<float>(value) > 0.5f ? 1.0f : 0.0f);
                liveAnalysis_.setLatencyMode(newMode);
                break;
            }
            // M4 Musical Control parameters
            case kFreezeId:
                freeze_.store(static_cast<float>(value) > 0.5f ? 1.0f : 0.0f);
                break;
            case kMorphPositionId:
                morphPosition_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kHarmonicFilterTypeId:
                harmonicFilterType_.store(static_cast<float>(value));
                break;
            case kResponsivenessId:
                responsiveness_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            // M5 Harmonic Memory parameters
            case kMemorySlotId:
                memorySlot_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kMemoryCaptureId:
                memoryCapture_.store(static_cast<float>(value));
                break;
            case kMemoryRecallId:
                memoryRecall_.store(static_cast<float>(value));
                break;

            // M6 Creative Extensions parameters
            case kTimbralBlendId:
                timbralBlend_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kStereoSpreadId:
                stereoSpread_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kEvolutionEnableId:
                evolutionEnable_.store(
                    static_cast<float>(value) > 0.5f ? 1.0f : 0.0f);
                break;
            case kEvolutionSpeedId:
                evolutionSpeed_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kEvolutionDepthId:
                evolutionDepth_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kEvolutionModeId:
                evolutionMode_.store(static_cast<float>(value));
                break;
            case kMod1EnableId:
                mod1Enable_.store(
                    static_cast<float>(value) > 0.5f ? 1.0f : 0.0f);
                break;
            case kMod1WaveformId:
                mod1Waveform_.store(static_cast<float>(value));
                break;
            case kMod1RateId:
                mod1Rate_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kMod1DepthId:
                mod1Depth_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kMod1RangeStartId:
                mod1RangeStart_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kMod1RangeEndId:
                mod1RangeEnd_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kMod1TargetId:
                mod1Target_.store(static_cast<float>(value));
                break;
            case kMod2EnableId:
                mod2Enable_.store(
                    static_cast<float>(value) > 0.5f ? 1.0f : 0.0f);
                break;
            case kMod2WaveformId:
                mod2Waveform_.store(static_cast<float>(value));
                break;
            case kMod2RateId:
                mod2Rate_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kMod2DepthId:
                mod2Depth_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kMod2RangeStartId:
                mod2RangeStart_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kMod2RangeEndId:
                mod2RangeEnd_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kMod2TargetId:
                mod2Target_.store(static_cast<float>(value));
                break;
            case kDetuneSpreadId:
                detuneSpread_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kBlendEnableId:
                blendEnable_.store(
                    static_cast<float>(value) > 0.5f ? 1.0f : 0.0f);
                break;
            case kBlendSlotWeight1Id:
            case kBlendSlotWeight2Id:
            case kBlendSlotWeight3Id:
            case kBlendSlotWeight4Id:
            case kBlendSlotWeight5Id:
            case kBlendSlotWeight6Id:
            case kBlendSlotWeight7Id:
            case kBlendSlotWeight8Id:
            {
                auto idx = paramQueue->getParameterId() - kBlendSlotWeight1Id;
                blendSlotWeights_[idx].store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            }
            case kBlendLiveWeightId:
                blendLiveWeight_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;

            // Harmonic Physics (Spec A)
            case kWarmthId:
                warmth_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kCouplingId:
                coupling_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kStabilityId:
                stability_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kEntropyId:
                entropy_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;

            // Analysis Feedback Loop (Spec B)
            case kAnalysisFeedbackId:
                feedbackAmount_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;
            case kAnalysisFeedbackDecayId:
                feedbackDecay_.store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f));
                break;

            default:
                break;
            }
        }
    }
}

} // namespace Innexus
