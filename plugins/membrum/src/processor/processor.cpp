// ==============================================================================
// Membrum Processor -- Phase 4
// ==============================================================================
// Per-pad parameter dispatch: parameter IDs in the per-pad range
// [kPadBaseId, kPadBaseId + kNumPads * kPadParamStride) are dispatched to
// VoicePool::setPadConfigField / setPadConfigSelector. Global proxy IDs
// (100-252) are no-ops in the processor (controller-only proxies).
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"
#include "dsp/default_kit.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "dsp/pad_category.h"
#include "dsp/tone_shaper.h"
#include "voice_pool/choke_group_table.h"
#include "voice_pool/voice_stealing_policy.h"
#include "processor/meters_block.h"
#include "state/state_codec.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/utility/dataexchange.h"

// FTZ/DAZ for denormal prevention on x86 (SC-007)
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <xmmintrin.h> // _MM_SET_FLUSH_ZERO_MODE
#include <pmmintrin.h> // _MM_SET_DENORMALS_ZERO_MODE
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace Membrum {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Helper: denormalize a pad config's normalized values and apply to a voice.
// This is called after loading state to push values into the DSP voice.
void applyPadConfigToVoice(DrumVoice& v, const PadConfig& cfg) noexcept
{
    v.setExciterType(cfg.exciterType);
    v.setBodyModel(cfg.bodyModel);
    v.setMaterial(cfg.material);
    v.setSize(cfg.size);
    v.setDecay(cfg.decay);
    v.setStrikePosition(cfg.strikePosition);
    v.setLevel(cfg.level);

    // Tone Shaper
    {
        const int filterTypeIdx = std::clamp(static_cast<int>(cfg.tsFilterType * 3.0f), 0, 2);
        v.toneShaper().setFilterType(static_cast<ToneShaperFilterType>(filterTypeIdx));
    }
    v.toneShaper().setFilterCutoff(
        20.0f * std::pow(1000.0f, std::clamp(cfg.tsFilterCutoff, 0.0f, 1.0f)));
    v.toneShaper().setFilterResonance(cfg.tsFilterResonance);
    v.toneShaper().setFilterEnvAmount(cfg.tsFilterEnvAmount * 2.0f - 1.0f);
    v.toneShaper().setFilterEnvAttackMs(cfg.tsFilterEnvAttack * 500.0f);
    v.toneShaper().setFilterEnvDecayMs(cfg.tsFilterEnvDecay * 2000.0f);
    v.toneShaper().setFilterEnvSustain(cfg.tsFilterEnvSustain);
    v.toneShaper().setFilterEnvReleaseMs(cfg.tsFilterEnvRelease * 2000.0f);
    v.toneShaper().setDriveAmount(cfg.tsDriveAmount);
    v.toneShaper().setFoldAmount(cfg.tsFoldAmount);
    {
        const float startHz = 20.0f * std::pow(100.0f, std::clamp(cfg.tsPitchEnvStart, 0.0f, 1.0f));
        v.toneShaper().setPitchEnvStartHz(startHz);
    }
    {
        const float endHz = 20.0f * std::pow(100.0f, std::clamp(cfg.tsPitchEnvEnd, 0.0f, 1.0f));
        v.toneShaper().setPitchEnvEndHz(endHz);
    }
    v.toneShaper().setPitchEnvTimeMs(cfg.tsPitchEnvTime * 500.0f);
    {
        const int curveIdx = std::clamp(static_cast<int>(cfg.tsPitchEnvCurve * 2.0f), 0, 1);
        v.toneShaper().setPitchEnvCurve(static_cast<ToneShaperCurve>(curveIdx));
    }

    // Unnatural Zone
    v.unnaturalZone().setModeStretch(
        0.5f + std::clamp(cfg.modeStretch, 0.0f, 1.0f) * 1.5f);
    v.unnaturalZone().setDecaySkew(
        std::clamp(cfg.decaySkew, 0.0f, 1.0f) * 2.0f - 1.0f);
    v.unnaturalZone().modeInject.setAmount(
        std::clamp(cfg.modeInjectAmount, 0.0f, 1.0f));
    v.unnaturalZone().nonlinearCoupling.setAmount(
        std::clamp(cfg.nonlinearCoupling, 0.0f, 1.0f));

    // Material Morph
    v.unnaturalZone().materialMorph.setEnabled(cfg.morphEnabled >= 0.5f);
    v.unnaturalZone().materialMorph.setStart(
        std::clamp(cfg.morphStart, 0.0f, 1.0f));
    v.unnaturalZone().materialMorph.setEnd(
        std::clamp(cfg.morphEnd, 0.0f, 1.0f));
    v.unnaturalZone().materialMorph.setDurationMs(
        10.0f + std::clamp(cfg.morphDuration, 0.0f, 1.0f) * 1990.0f);
    v.unnaturalZone().materialMorph.setCurve(cfg.morphCurve >= 0.5f);
}

// Helper: apply a normalized parameter change to a voice for a given pad offset.
// Used by processParameterChanges to update currently-sounding voices when a
// per-pad param changes. This is the Phase 4 replacement for the Phase 2/3
// "broadcast to all voices" approach.
void applyPadParamToMatchingVoices(VoicePool& pool, int padIndex, int offset,
                                   float fValue,
                                   [[maybe_unused]] const VoiceMeta* meta,
                                   int maxPolyphony) noexcept
{
    // In Phase 4, parameter changes take effect on the next noteOn (pad config
    // is read at noteOn time). Currently-sounding voices are NOT modified.
    // This matches the spec: "does NOT touch currently sounding voices".
    (void)pool;
    (void)padIndex;
    (void)offset;
    (void)fValue;
    (void)meta;
    (void)maxPolyphony;
}

} // namespace

Processor::Processor()
{
    setControllerClass(kControllerUID);
}

Processor::~Processor() = default;

// Phase 6 (T025): mirror the Controller's registered-default values for each
// per-pad parameter the MacroMapper targets. Kept in sync with kPadParamSpecs.
RegisteredDefaultsTable Processor::buildRegisteredDefaultsTable() noexcept
{
    RegisteredDefaultsTable t{};
    t.byOffset[kPadMaterial]           = 0.5f;
    t.byOffset[kPadSize]               = 0.5f;
    t.byOffset[kPadDecay]              = 0.3f;
    t.byOffset[kPadStrikePosition]     = 0.3f;
    t.byOffset[kPadLevel]              = 0.8f;
    t.byOffset[kPadTSFilterCutoff]     = 1.0f;
    t.byOffset[kPadTSPitchEnvStart]    = 0.0f;
    t.byOffset[kPadTSPitchEnvEnd]      = 0.0f;
    t.byOffset[kPadTSPitchEnvTime]     = 0.0f;
    t.byOffset[kPadModeStretch]        = 0.333333f;
    t.byOffset[kPadDecaySkew]          = 0.5f;
    t.byOffset[kPadModeInjectAmount]   = 0.0f;
    t.byOffset[kPadNonlinearCoupling]  = 0.0f;
    t.byOffset[kPadFMRatio]            = 0.5f;
    t.byOffset[kPadFeedbackAmount]     = 0.0f;
    t.byOffset[kPadNoiseBurstDuration] = 0.5f;
    t.byOffset[kPadFrictionPressure]   = 0.0f;
    t.byOffset[kPadCouplingAmount]     = 0.5f;
    return t;
}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    auto result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addEventInput(STR16("Event In"));

    // FR-040: 1 main + 15 auxiliary stereo output buses
    addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo);
    for (int i = 1; i < kMaxOutputBuses; ++i)
    {
        char narrowName[32];
        std::snprintf(narrowName, sizeof(narrowName), "Aux %d", i);
        Steinberg::Vst::String128 wideName;
        for (int c = 0; c < 31 && narrowName[c] != '\0'; ++c)
            wideName[c] = static_cast<Steinberg::char16>(narrowName[c]);
        wideName[std::strlen(narrowName)] = 0;
        addAudioOutput(wideName, SpeakerArr::kStereo, BusTypes::kAux, 0);
    }

    // FR-030: Initialize all 32 pads with GM-inspired default templates.
    // This is overwritten by setState() if a saved state is loaded.
    DefaultKit::apply(voicePool_.padConfigsArray());

    // Phase 5: Initialize pad categories from default kit configuration.
    for (int i = 0; i < kNumPads; ++i)
        padCategories_[static_cast<size_t>(i)] =
            classifyPad(voicePool_.padConfig(i));

    // Phase 6 (T025): cache registered defaults for the MacroMapper. This
    // must happen before any audio-thread apply() call.
    macroMapper_.prepare(buildRegisteredDefaultsTable());

    return kResultOk;
}

// ==============================================================================
// processParameterChanges -- Phase 4 per-pad dispatch
// ==============================================================================

// Phase 6 (US4 / FR-014, FR-023): rebuild effectiveGain using the current
// Tier 1 knobs, pad categories, and per-pad coupling amounts.
void Processor::recomputeCouplingMatrix() noexcept
{
    std::array<float, kNumPads> amounts{};
    for (int pad = 0; pad < kNumPads; ++pad)
        amounts[static_cast<size_t>(pad)] =
            voicePool_.padConfig(pad).couplingAmount;

    couplingMatrix_.recomputeFromTier1(
        snareBuzz_.load(std::memory_order_relaxed),
        tomResonance_.load(std::memory_order_relaxed),
        padCategories_.data(),
        amounts.data());
}

void Processor::processParameterChanges(IParameterChanges* paramChanges)
{
    if (!paramChanges)
        return;

    int32 numParams = paramChanges->getParameterCount();
    for (int32 i = 0; i < numParams; ++i)
    {
        auto* queue = paramChanges->getParameterData(i);
        if (!queue)
            continue;

        ParamID paramId = queue->getParameterId();
        int32 numPoints = queue->getPointCount();
        if (numPoints <= 0)
            continue;

        // Use the last point value (most recent)
        ParamValue value = 0.0;
        int32 sampleOffset = 0;
        if (queue->getPoint(numPoints - 1, sampleOffset, value) != kResultOk)
            continue;

        const float fValue = static_cast<float>(value);

        // ------------------------------------------------------------------
        // 1. Check if paramId is in per-pad range [kPadBaseId, kPadBaseId + 32*64)
        // ------------------------------------------------------------------
        const int padIdx = padIndexFromParamId(static_cast<int>(paramId));
        const int padOff = padOffsetFromParamId(static_cast<int>(paramId));
        if (padIdx >= 0 && padOff >= 0)
        {
            // Dispatch to per-pad config in VoicePool
            if (padOff == kPadExciterType)
            {
                const int idx = std::clamp(
                    static_cast<int>(value * static_cast<double>(ExciterType::kCount)),
                    0, static_cast<int>(ExciterType::kCount) - 1);
                voicePool_.setPadConfigSelector(padIdx, padOff, idx);
                // Phase 5: exciter type change can affect pad category
                padCategories_[static_cast<size_t>(padIdx)] =
                    classifyPad(voicePool_.padConfig(padIdx));
                recomputeCouplingMatrix();
            }
            else if (padOff == kPadBodyModel)
            {
                const int idx = std::clamp(
                    static_cast<int>(value * static_cast<double>(BodyModelType::kCount)),
                    0, static_cast<int>(BodyModelType::kCount) - 1);
                voicePool_.setPadConfigSelector(padIdx, padOff, idx);
                // Phase 5: body model change can affect pad category
                padCategories_[static_cast<size_t>(padIdx)] =
                    classifyPad(voicePool_.padConfig(padIdx));
                recomputeCouplingMatrix();
            }
            else if (padOff == kPadTSPitchEnvTime)
            {
                voicePool_.setPadConfigField(padIdx, padOff, fValue);
                // Phase 5: pitch env time change can affect pad category (Kick vs Tom)
                padCategories_[static_cast<size_t>(padIdx)] =
                    classifyPad(voicePool_.padConfig(padIdx));
                recomputeCouplingMatrix();
            }
            else if (padOff == kPadCouplingAmount)
            {
                // Phase 6 (US4 / FR-014, FR-023, FR-034): per-pad coupling
                // amount feeds the effectiveGain formula. Bake amounts into
                // the matrix so a pad with amount = 0 is fully excluded as
                // both source and receiver.
                voicePool_.setPadConfigField(padIdx, padOff, fValue);
                recomputeCouplingMatrix();
            }
            else if (padOff >= kPadMacroTightness && padOff <= kPadMacroComplexity)
            {
                // Phase 6 (US1 / T025, FR-022, FR-023): a macro parameter
                // changed. Store the new macro value, then let the MacroMapper
                // recompute the derived underlying parameters for this pad.
                voicePool_.setPadConfigField(padIdx, padOff, fValue);
                macroMapper_.apply(padIdx, voicePool_.padConfigMut(padIdx));
                if (padOff == kPadMacroComplexity)
                {
                    // Complexity drives couplingAmount; matrix must refresh.
                    recomputeCouplingMatrix();
                }
            }
            else
            {
                voicePool_.setPadConfigField(padIdx, padOff, fValue);
            }
            continue;
        }

        // ------------------------------------------------------------------
        // 2. Global-only params
        // ------------------------------------------------------------------
        switch (paramId)
        {
        case kMaxPolyphonyId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const int n = 4 + static_cast<int>(std::lround(clamped * 12.0f));
            const int nClamped = std::clamp(n, 4, 16);
            maxPolyphony_.store(nClamped);
            voicePool_.setMaxPolyphony(nClamped);
            break;
        }
        case kVoiceStealingId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const int idx = std::clamp(static_cast<int>(clamped * 3.0f), 0, 2);
            voiceStealingPolicy_.store(idx);
            voicePool_.setVoiceStealingPolicy(
                static_cast<VoiceStealingPolicy>(idx));
            break;
        }

        // ------------------------------------------------------------------
        // 3. Global proxy IDs (100-252) -- map to the selected pad.
        //    In a DAW, the controller forwards these as per-pad params,
        //    but the host also sends them directly to the processor.
        //    We handle them by routing to the selected pad's config.
        // ------------------------------------------------------------------
        case kMaterialId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadMaterial, fValue);
            break;
        case kSizeId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadSize, fValue);
            break;
        case kDecayId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadDecay, fValue);
            break;
        case kStrikePositionId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadStrikePosition, fValue);
            break;
        case kLevelId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadLevel, fValue);
            break;
        case kExciterTypeId:
        {
            const int idx = std::clamp(
                static_cast<int>(value * static_cast<double>(ExciterType::kCount)),
                0, static_cast<int>(ExciterType::kCount) - 1);
            voicePool_.setPadConfigSelector(selectedPadIndex_, kPadExciterType, idx);
            break;
        }
        case kBodyModelId:
        {
            const int idx = std::clamp(
                static_cast<int>(value * static_cast<double>(BodyModelType::kCount)),
                0, static_cast<int>(BodyModelType::kCount) - 1);
            voicePool_.setPadConfigSelector(selectedPadIndex_, kPadBodyModel, idx);
            break;
        }
        case kExciterFMRatioId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadFMRatio, fValue);
            break;
        case kExciterFeedbackAmountId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadFeedbackAmount, fValue);
            break;
        case kExciterNoiseBurstDurationId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadNoiseBurstDuration, fValue);
            break;
        case kExciterFrictionPressureId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadFrictionPressure, fValue);
            break;
        case kToneShaperFilterTypeId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSFilterType, fValue);
            break;
        case kToneShaperFilterCutoffId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSFilterCutoff, fValue);
            break;
        case kToneShaperFilterResonanceId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSFilterResonance, fValue);
            break;
        case kToneShaperFilterEnvAmountId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSFilterEnvAmount, fValue);
            break;
        case kToneShaperDriveAmountId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSDriveAmount, fValue);
            break;
        case kToneShaperFoldAmountId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSFoldAmount, fValue);
            break;
        case kToneShaperPitchEnvStartId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSPitchEnvStart, fValue);
            break;
        case kToneShaperPitchEnvEndId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSPitchEnvEnd, fValue);
            break;
        case kToneShaperPitchEnvTimeId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSPitchEnvTime, fValue);
            break;
        case kToneShaperPitchEnvCurveId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSPitchEnvCurve, fValue);
            break;
        case kToneShaperFilterEnvAttackId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSFilterEnvAttack, fValue);
            break;
        case kToneShaperFilterEnvDecayId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSFilterEnvDecay, fValue);
            break;
        case kToneShaperFilterEnvSustainId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSFilterEnvSustain, fValue);
            break;
        case kToneShaperFilterEnvReleaseId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadTSFilterEnvRelease, fValue);
            break;
        case kUnnaturalModeStretchId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadModeStretch, fValue);
            break;
        case kUnnaturalDecaySkewId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadDecaySkew, fValue);
            break;
        case kUnnaturalModeInjectAmountId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadModeInjectAmount, fValue);
            break;
        case kUnnaturalNonlinearCouplingId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadNonlinearCoupling, fValue);
            break;
        case kMorphEnabledId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadMorphEnabled, fValue);
            break;
        case kMorphStartId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadMorphStart, fValue);
            break;
        case kMorphEndId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadMorphEnd, fValue);
            break;
        case kMorphDurationMsId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadMorphDuration, fValue);
            break;
        case kMorphCurveId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadMorphCurve, fValue);
            break;
        case kChokeGroupId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const int n = static_cast<int>(std::lround(clamped * 8.0f));
            const int nClamped = std::clamp(n, 0, 8);
            voicePool_.setPadChokeGroup(selectedPadIndex_, static_cast<std::uint8_t>(nClamped));
            break;
        }
        // Phase 8 (US7 / FR-065): Output Bus selector proxy -- forward to the
        // currently selected pad's kPadOutputBus field.
        case kOutputBusId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            voicePool_.setPadConfigField(selectedPadIndex_, kPadOutputBus, clamped);
            break;
        }
        case kSelectedPadId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            selectedPadIndex_ = std::clamp(
                static_cast<int>(std::lround(clamped * 31.0f)), 0, 31);
            break;
        }

        // ---- Phase 5: Coupling parameters ----
        case kGlobalCouplingId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            globalCoupling_.store(clamped, std::memory_order_relaxed);
            couplingEngine_.setAmount(clamped);
            break;
        }
        case kSnareBuzzId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            snareBuzz_.store(clamped, std::memory_order_relaxed);
            recomputeCouplingMatrix();
            break;
        }
        case kTomResonanceId:
        {
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            tomResonance_.store(clamped, std::memory_order_relaxed);
            recomputeCouplingMatrix();
            break;
        }
        case kCouplingDelayId:
        {
            // Denormalize from [0, 1] to [0.5, 2.0] ms
            const float clamped = std::clamp(fValue, 0.0f, 1.0f);
            const float delayMs = 0.5f + clamped * 1.5f;
            couplingDelayMs_.store(delayMs, std::memory_order_relaxed);
            break;
        }

        // ---- Phase 7: parallel noise layer + click transient proxies ----
        case kNoiseLayerMixId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadNoiseLayerMix, fValue);
            break;
        case kNoiseLayerCutoffId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadNoiseLayerCutoff, fValue);
            break;
        case kNoiseLayerResonanceId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadNoiseLayerResonance, fValue);
            break;
        case kNoiseLayerDecayId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadNoiseLayerDecay, fValue);
            break;
        case kNoiseLayerColorId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadNoiseLayerColor, fValue);
            break;
        case kClickLayerMixId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadClickLayerMix, fValue);
            break;
        case kClickLayerContactMsId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadClickLayerContactMs, fValue);
            break;
        case kClickLayerBrightnessId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadClickLayerBrightness, fValue);
            break;

        // ---- Phase 8A: per-mode damping law proxies ----
        case kBodyDampingB1Id:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadBodyDampingB1, fValue);
            break;
        case kBodyDampingB3Id:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadBodyDampingB3, fValue);
            break;

        // ---- Phase 8C: air-loading + per-mode scatter proxies ----
        case kAirLoadingId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadAirLoading, fValue);
            break;
        case kModeScatterId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadModeScatter, fValue);
            break;

        // ---- Phase 8D: head <-> shell coupling proxies ----
        case kCouplingStrengthId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadCouplingStrength, fValue);
            break;
        case kSecondaryEnabledId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadSecondaryEnabled, fValue);
            break;
        case kSecondarySizeId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadSecondarySize, fValue);
            break;
        case kSecondaryMaterialId:
            voicePool_.setPadConfigField(selectedPadIndex_, kPadSecondaryMaterial, fValue);
            break;

        default:
            break;
        }
    }
}

// Consume a pending audition request (posted via IMessage "AuditionPad") and
// fire a voice note-on for it from the audio thread.
void Processor::consumePendingAudition()
{
    const std::uint32_t word = pendingAudition_.exchange(0u, std::memory_order_acq_rel);
    if ((word & 0x4000u) == 0u)
        return;
    const int midi  = static_cast<int>(word & 0x7Fu);
    const int velByte = static_cast<int>((word >> 7) & 0x7Fu);
    if (midi < 36 || midi > 67)
        return;
    if (velByte == 0)
    {
        voicePool_.noteOff(static_cast<std::uint8_t>(midi));
        return;
    }
    const float velNorm = static_cast<float>(velByte) / 127.0f;
    voicePool_.noteOn(static_cast<std::uint8_t>(midi), velNorm);
}

// Process MIDI events: note-on/note-off on any MIDI note in [36, 67]
void Processor::processEvents(IEventList* events)
{
    if (!events)
        return;

    int32 numEvents = events->getEventCount();
    for (int32 i = 0; i < numEvents; ++i)
    {
        Event event{};
        if (events->getEvent(i, event) != kResultOk)
            continue;

        switch (event.type)
        {
        case Event::kNoteOnEvent:
        {
            const int16 pitch = event.noteOn.pitch;
            if (pitch < 36 || pitch > 67)
                break;
            if (event.noteOn.velocity <= 0.0f)
            {
                voicePool_.noteOff(static_cast<std::uint8_t>(pitch));
                break;
            }
            voicePool_.noteOn(static_cast<std::uint8_t>(pitch),
                              event.noteOn.velocity);
            break;
        }
        case Event::kNoteOffEvent:
        {
            const int16 pitch = event.noteOff.pitch;
            if (pitch < 36 || pitch > 67)
                break;
            voicePool_.noteOff(static_cast<std::uint8_t>(pitch));
            break;
        }
        default:
            break;
        }
    }
}

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    if (data.numSamples == 0)
        return kResultOk;

    // T045: start CPU-usage timer (steady_clock::now is lock-free and
    // allocation-free on all supported platforms).
    const auto cpuT0 = std::chrono::steady_clock::now();

    processParameterChanges(data.inputParameterChanges);
    processEvents(data.inputEvents);
    consumePendingAudition();

    if (data.numOutputs > 0 && data.outputs != nullptr)
    {
        float* outL = data.outputs[0].channelBuffers32[0];
        float* outR = data.outputs[0].channelBuffers32[1];

        // FR-044 / FR-048: Extract auxiliary bus buffer pointers
        const int numOutputBuses = std::min(static_cast<int>(data.numOutputs),
                                            kMaxOutputBuses);

        if (numOutputBuses <= 1)
        {
            // No aux buses available -- fall back to Phase 3 behavior
            voicePool_.processBlock(outL, outR, data.numSamples);
        }
        else
        {
            // Build aux buffer pointer arrays from data.outputs[1..N]
            std::array<float*, kMaxOutputBuses> auxLPtrs{};
            std::array<float*, kMaxOutputBuses> auxRPtrs{};

            for (int b = 1; b < numOutputBuses; ++b)
            {
                if (data.outputs[b].channelBuffers32 != nullptr)
                {
                    auxLPtrs[static_cast<std::size_t>(b)] =
                        data.outputs[b].channelBuffers32[0];
                    auxRPtrs[static_cast<std::size_t>(b)] =
                        data.outputs[b].channelBuffers32[1];

                    // Zero aux buffers (host may not guarantee zeroed)
                    if (auxLPtrs[static_cast<std::size_t>(b)] != nullptr)
                        std::memset(auxLPtrs[static_cast<std::size_t>(b)], 0,
                                    static_cast<std::size_t>(data.numSamples) * sizeof(float));
                    if (auxRPtrs[static_cast<std::size_t>(b)] != nullptr)
                        std::memset(auxRPtrs[static_cast<std::size_t>(b)], 0,
                                    static_cast<std::size_t>(data.numSamples) * sizeof(float));
                }
            }

            voicePool_.processBlock(outL, outR,
                                    auxLPtrs.data(), auxRPtrs.data(),
                                    busActive_.data(), numOutputBuses,
                                    data.numSamples);

            // Set silence flags for aux buses
            for (int b = 1; b < numOutputBuses; ++b)
            {
                bool auxSilent = true;
                if (auxLPtrs[static_cast<std::size_t>(b)] != nullptr)
                {
                    for (int32 s = 0; s < data.numSamples; ++s)
                    {
                        if (auxLPtrs[static_cast<std::size_t>(b)][s] != 0.0f ||
                            auxRPtrs[static_cast<std::size_t>(b)][s] != 0.0f)
                        {
                            auxSilent = false;
                            break;
                        }
                    }
                }
                data.outputs[b].silenceFlags = auxSilent ? 3ULL : 0ULL;
            }
        }

        // ============================================================
        // Phase 5: Coupling signal chain (FR-072)
        // mono sum -> delay read -> delay write -> engine -> energy limiter -> add to L/R
        // Skip entire loop when engine is bypassed (global coupling = 0).
        // ============================================================
        if (!couplingEngine_.isBypassed())
        {
            const float delayMs = couplingDelayMs_.load(std::memory_order_relaxed);
            const float delaySamples = delayMs * static_cast<float>(sampleRate_) * 0.001f;

            for (int32 s = 0; s < data.numSamples; ++s)
            {
                const float mono = (outL[s] + outR[s]) * 0.5f;
                const float delayed = couplingDelay_.readLinear(delaySamples);
                couplingDelay_.write(mono);
                float coupling = couplingEngine_.process(delayed);
                coupling = applyEnergyLimiter(coupling);
                outL[s] += coupling;
                outR[s] += coupling;
            }
        }

        data.outputs[0].silenceFlags =
            (voicePool_.isAnyVoiceActive() || !couplingEngine_.isBypassed())
                ? 0ULL : 3ULL;
    }

    // ============================================================
    // Phase 6 (T043): publish per-pad glow amplitudes.
    // Walk both main and shadow voice slots, take the max currentLevel per
    // pad, and publish. Pads with no active voice are published as 0.0 so
    // the UI can decay its own smoothing.
    // ============================================================
    {
        std::array<float, kNumPads> padAmp{};
        padAmp.fill(0.0f);
        for (int slot = 0; slot < kMaxVoices; ++slot)
        {
            const auto& m = voicePool_.voiceMeta(slot);
            if (m.state != VoiceSlotState::Free)
            {
                const int padIdx = static_cast<int>(m.originatingNote)
                                 - static_cast<int>(kFirstDrumNote);
                if (padIdx >= 0 && padIdx < kNumPads)
                {
                    const float lvl = m.currentLevel * m.fastReleaseGain;
                    padAmp[static_cast<std::size_t>(padIdx)] =
                        std::max(padAmp[static_cast<std::size_t>(padIdx)], lvl);
                }
            }
            const auto& rm = voicePool_.releasingMeta(slot);
            if (rm.state != VoiceSlotState::Free)
            {
                const int padIdx = static_cast<int>(rm.originatingNote)
                                 - static_cast<int>(kFirstDrumNote);
                if (padIdx >= 0 && padIdx < kNumPads)
                {
                    const float lvl = rm.currentLevel * rm.fastReleaseGain;
                    padAmp[static_cast<std::size_t>(padIdx)] =
                        std::max(padAmp[static_cast<std::size_t>(padIdx)], lvl);
                }
            }
        }
        for (int pad = 0; pad < kNumPads; ++pad)
            padGlowPublisher_.publish(pad, padAmp[static_cast<std::size_t>(pad)]);
    }

    // ============================================================
    // Phase 6 (T044): publish per-source matrix activity mask.
    // A (src, dst) pair is "active" this block when effectiveGain is
    // non-zero AND src pad is currently sounding (its glow > 0).
    // ============================================================
    {
        std::array<std::uint8_t, kNumPads> srcActive{};
        for (int slot = 0; slot < kMaxVoices; ++slot)
        {
            const auto& m = voicePool_.voiceMeta(slot);
            if (m.state == VoiceSlotState::Active)
            {
                const int padIdx = static_cast<int>(m.originatingNote)
                                 - static_cast<int>(kFirstDrumNote);
                if (padIdx >= 0 && padIdx < kNumPads)
                    srcActive[static_cast<std::size_t>(padIdx)] = 1;
            }
        }
        for (int src = 0; src < kNumPads; ++src)
        {
            std::uint32_t dstMask = 0u;
            if (srcActive[static_cast<std::size_t>(src)] != 0)
            {
                for (int dst = 0; dst < kNumPads; ++dst)
                {
                    if (src == dst)
                        continue;
                    if (couplingMatrix_.getEffectiveGain(src, dst) > 0.0f)
                        dstMask |= (1u << dst);
                }
            }
            matrixActivityPublisher_.publishSourceActivity(src, dstMask);
        }
    }

    // ============================================================
    // Phase 6 (T045): publish MetersBlock via DataExchangeHandler.
    // Peak L/R over this block, active voice count, CPU permille placeholder.
    // ============================================================
    if (dataExchangeHandler_ && data.numOutputs > 0 && data.outputs != nullptr)
    {
        float peakL = 0.0f;
        float peakR = 0.0f;
        const float* outL = data.outputs[0].channelBuffers32[0];
        const float* outR = data.outputs[0].channelBuffers32[1];
        for (int32 s = 0; s < data.numSamples; ++s)
        {
            const float aL = std::abs(outL[s]);
            const float aR = std::abs(outR[s]);
            peakL = std::max(peakL, aL);
            peakR = std::max(peakR, aR);
        }

        // T045: compute CPU usage (per-mille of the block budget) using a
        // simple EWMA smoother so brief spikes do not dominate the meter.
        // blockBudget = numSamples / sampleRate (in seconds).
        std::uint16_t cpuPermille = 0;
        if (sampleRate_ > 0.0 && data.numSamples > 0)
        {
            const auto cpuT1 = std::chrono::steady_clock::now();
            const double elapsedSec =
                std::chrono::duration<double>(cpuT1 - cpuT0).count();
            const double blockBudgetSec =
                static_cast<double>(data.numSamples) / sampleRate_;
            const double ratio =
                blockBudgetSec > 0.0 ? (elapsedSec / blockBudgetSec) : 0.0;
            const float instantaneousPermille =
                static_cast<float>(std::clamp(ratio * 1000.0, 0.0, 65535.0));
            // EWMA: alpha = 0.1 smooths ~10 blocks; cheap and audio-safe.
            constexpr float kEwmaAlpha = 0.1f;
            cpuPermilleEwma_ =
                cpuPermilleEwma_ + kEwmaAlpha
                * (instantaneousPermille - cpuPermilleEwma_);
            cpuPermille = static_cast<std::uint16_t>(
                std::clamp(cpuPermilleEwma_ + 0.5f, 0.0f, 65535.0f));
        }

        auto block = dataExchangeHandler_->getCurrentOrNewBlock();
        if (block.blockID != Steinberg::Vst::InvalidDataExchangeBlockID
            && block.data != nullptr
            && block.size >= sizeof(MetersBlock))
        {
            MetersBlock mb;
            mb.peakL        = peakL;
            mb.peakR        = peakR;
            mb.activeVoices = static_cast<std::uint16_t>(
                                  voicePool_.getActiveVoiceCount());
            mb.cpuPermille  = cpuPermille;
            std::memcpy(block.data, &mb, sizeof(MetersBlock));
            dataExchangeHandler_->sendCurrentBlock();
        }
    }

    return kResultOk;
}

// ==============================================================================
// State v4 -- data-model.md
// ==============================================================================
// Binary layout (v4):
//   [int32] version = 4
//   [int32] maxPolyphony [4, 16]
//   [int32] voiceStealingPolicy [0, 2]
//   For each of 32 pads:
//     [int32]  exciterType
//     [int32]  bodyModel
//     [34 x float64] sound params (offsets 2-35 as normalized float64)
//     [uint8]  chokeGroup
//     [uint8]  outputBus
//   [int32] selectedPadIndex [0, 31]
// ==============================================================================

tresult PLUGIN_API Processor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    // Build a KitSnapshot from the current processor state and delegate to
    // the shared codec. Session-scoped fields (uiMode) are NOT emitted from
    // the processor path -- the controller resets them to defaults before
    // setComponentState is consumed. hasSession=false.
    State::KitSnapshot kit;
    kit.maxPolyphony        = maxPolyphony_.load();
    kit.voiceStealingPolicy = voiceStealingPolicy_.load();
    kit.selectedPadIndex    = selectedPadIndex_;
    kit.globalCoupling      =
        static_cast<double>(globalCoupling_.load(std::memory_order_relaxed));
    kit.snareBuzz           =
        static_cast<double>(snareBuzz_.load(std::memory_order_relaxed));
    kit.tomResonance        =
        static_cast<double>(tomResonance_.load(std::memory_order_relaxed));
    kit.couplingDelayMs     =
        static_cast<double>(couplingDelayMs_.load(std::memory_order_relaxed));

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        kit.pads[static_cast<std::size_t>(pad)] =
            State::toPadSnapshot(voicePool_.padConfig(pad));
    }

    // Overrides.
    kit.overrides.clear();
    kit.overrides.reserve(couplingMatrix_.getOverrideCount());
    couplingMatrix_.forEachOverride(
        [&kit](int src, int dst, float coeff) {
            kit.overrides.push_back(State::TierTwoOverride{
                .src   = static_cast<std::uint8_t>(src),
                .dst   = static_cast<std::uint8_t>(dst),
                .coeff = coeff,
            });
        });

    kit.hasSession = false;
    return State::writeKitBlob(state, kit);
}

tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    State::KitSnapshot kit;
    if (State::readKitBlob(state, kit) != kResultOk)
        return kResultFalse;

    // Polyphony / stealing.
    maxPolyphony_.store(kit.maxPolyphony);
    voiceStealingPolicy_.store(kit.voiceStealingPolicy);
    voicePool_.setMaxPolyphony(kit.maxPolyphony);
    voicePool_.setVoiceStealingPolicy(
        static_cast<VoiceStealingPolicy>(kit.voiceStealingPolicy));

    // Per-pad configs.
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        State::applyPadSnapshot(
            kit.pads[static_cast<std::size_t>(pad)],
            voicePool_.padConfigMut(pad));
    }

    // Sync choke group table from padConfigs.
    for (int pad = 0; pad < kNumPads; ++pad)
        voicePool_.setPadChokeGroup(pad, voicePool_.padConfig(pad).chokeGroup);

    selectedPadIndex_ = kit.selectedPadIndex;

    // Phase 5 globals.
    const float gcF = static_cast<float>(kit.globalCoupling);
    const float sbF = static_cast<float>(kit.snareBuzz);
    const float trF = static_cast<float>(kit.tomResonance);
    const float cdF = static_cast<float>(kit.couplingDelayMs);
    globalCoupling_.store(gcF, std::memory_order_relaxed);
    snareBuzz_.store(sbF, std::memory_order_relaxed);
    tomResonance_.store(trF, std::memory_order_relaxed);
    couplingDelayMs_.store(cdF, std::memory_order_relaxed);
    couplingEngine_.setAmount(gcF);

    // Refresh categories and recompute Tier 1 matrix.
    for (int i = 0; i < kNumPads; ++i)
        padCategories_[static_cast<size_t>(i)] =
            classifyPad(voicePool_.padConfig(i));
    recomputeCouplingMatrix();

    // Apply overrides.
    for (const auto& ov : kit.overrides)
    {
        couplingMatrix_.setOverride(
            static_cast<int>(ov.src),
            static_cast<int>(ov.dst),
            ov.coeff);
    }

    // Reapply macros so derived parameters reflect loaded macro values
    // (FR-082). We gate on "any non-neutral macro" because reapplyAll
    // rewrites underlying targets from base+delta -- for neutral (0.5)
    // macros the delta is zero so the call would overwrite custom
    // underlying values with the registered defaults. Bit-exact round-trip
    // of custom underlying values must be preserved (FR-084, SC-005).
    auto& pads = voicePool_.padConfigsArray();
    const bool anyNonNeutral = std::ranges::any_of(pads,
        [](const auto& p) noexcept {
            return p.macroTightness  != 0.5f || p.macroBrightness != 0.5f ||
                   p.macroBodySize   != 0.5f || p.macroPunch      != 0.5f ||
                   p.macroComplexity != 0.5f;
        });
    if (anyNonNeutral)
        macroMapper_.reapplyAll(pads);

    return kResultOk;
}

// ==============================================================================
// activateBus -- FR-045: track bus activation for multi-bus output
// ==============================================================================

tresult PLUGIN_API Processor::activateBus(MediaType type, BusDirection dir,
                                           int32 index, TBool state)
{
    auto result = AudioEffect::activateBus(type, dir, index, state);
    if (result == kResultTrue && type == kAudio && dir == kOutput)
    {
        if (index >= 0 && std::cmp_less(index, busActive_.size()))
        {
            busActive_[static_cast<std::size_t>(index)] = (state != 0);
            // FR-045: main bus (index 0) is always active
            busActive_[0] = true;
        }
    }
    return result;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif

    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;
    voicePool_.prepare(sampleRate_, maxBlockSize_);
    voicePool_.setMaxPolyphony(maxPolyphony_.load());
    voicePool_.setVoiceStealingPolicy(
        static_cast<VoiceStealingPolicy>(voiceStealingPolicy_.load()));

    // Phase 5: prepare coupling engine and delay
    couplingEngine_.prepare(sampleRate_);
    couplingDelay_.prepare(sampleRate_, 0.002f);  // 2 ms max delay
    voicePool_.setCouplingEngine(&couplingEngine_);
    energyEnvelope_ = 0.0f;

    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state)
    {
        voicePool_.prepare(sampleRate_, maxBlockSize_);
        voicePool_.setMaxPolyphony(maxPolyphony_.load());
        voicePool_.setVoiceStealingPolicy(
            static_cast<VoiceStealingPolicy>(voiceStealingPolicy_.load()));

        // Phase 6 (T045): open the DataExchange queue for MetersBlock.
        if (dataExchangeHandler_)
        {
            Steinberg::Vst::ProcessSetup setup{};
            setup.sampleRate          = sampleRate_;
            setup.maxSamplesPerBlock  = maxBlockSize_;
            setup.processMode         = Steinberg::Vst::kRealtime;
            setup.symbolicSampleSize  = Steinberg::Vst::kSample32;
            dataExchangeHandler_->onActivate(setup);
        }
    }
    else
    {
        // Phase 6 (T045): close the DataExchange queue cleanly.
        if (dataExchangeHandler_)
            dataExchangeHandler_->onDeactivate();

        // Reset publishers so stale values do not persist after bypass.
        padGlowPublisher_.reset();
        matrixActivityPublisher_.reset();
    }
    return kResultOk;
}

// ==============================================================================
// Phase 6 (T045): connect/disconnect -- DataExchange lifecycle.
// ==============================================================================
tresult PLUGIN_API Processor::connect(IConnectionPoint* other)
{
    const auto result = AudioEffect::connect(other);
    if (result == kResultTrue)
    {
        auto configCallback = [](Steinberg::Vst::DataExchangeHandler::Config& config,
                                 const Steinberg::Vst::ProcessSetup& /*setup*/) {
            config.blockSize     = static_cast<Steinberg::uint32>(sizeof(MetersBlock));
            config.numBlocks     = 4;
            config.alignment     = 32;
            config.userContextID = kMetersDataExchangeUserContextId;
            return true;
        };
        dataExchangeHandler_ =
            std::make_unique<Steinberg::Vst::DataExchangeHandler>(this, configCallback);
        dataExchangeHandler_->onConnect(other, getHostContext());
    }
    return result;
}

tresult PLUGIN_API Processor::disconnect(IConnectionPoint* other)
{
    if (dataExchangeHandler_)
    {
        dataExchangeHandler_->onDisconnect(other);
        dataExchangeHandler_.reset();
    }
    return AudioEffect::disconnect(other);
}

// ==============================================================================
// T068 (Spec 141, retry): IMessage handler -- bridge the CouplingMatrix edits
// and snapshot requests between the UI controller and this authoritative
// processor-side instance. See controller.cpp for the wire format description.
// notify() is called on the UI/component thread, NOT the audio thread, so it
// is safe to mutate couplingMatrix_ directly -- the same pattern is used by
// setState() when loading per-pair overrides from a preset blob.
// ==============================================================================
tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message)
{
    if (message == nullptr)
        return AudioEffect::notify(message);

    const auto* id = message->getMessageID();
    if (id == nullptr)
        return AudioEffect::notify(message);

    if (std::strcmp(id, "AuditionPad") == 0)
    {
        auto* attrs = message->getAttributes();
        if (attrs == nullptr) return kResultOk;
        Steinberg::int64 midi = 0, velocity = 100;
        attrs->getInt("midi", midi);
        attrs->getInt("velocity", velocity);
        const std::uint32_t word =
            0x4000u
            | (static_cast<std::uint32_t>(midi & 0x7F))
            | ((static_cast<std::uint32_t>(velocity & 0x7F)) << 7);
        pendingAudition_.store(word, std::memory_order_release);
        return kResultOk;
    }

    if (std::strcmp(id, "CouplingMatrixEdit") == 0)
    {
        auto* attrs = message->getAttributes();
        if (attrs == nullptr) return kResultOk;

        Steinberg::int64 op = -1;
        Steinberg::int64 src = 0;
        Steinberg::int64 dst = 0;
        Steinberg::int64 valBits = 0;
        attrs->getInt("op",  op);
        attrs->getInt("src", src);
        attrs->getInt("dst", dst);
        attrs->getInt("val", valBits);
        float value = 0.0f;
        std::memcpy(&value, &valBits, sizeof(float));

        const int s = static_cast<int>(src);
        const int d = static_cast<int>(dst);

        switch (op)
        {
        case 0: // setOverride
            if (s >= 0 && s < kNumPads && d >= 0 && d < kNumPads)
            {
                couplingMatrix_.setOverride(s, d,
                    std::clamp(value, 0.0f, CouplingMatrix::kMaxCoefficient));
            }
            break;
        case 1: // clearOverride
            if (s >= 0 && s < kNumPads && d >= 0 && d < kNumPads)
                couplingMatrix_.clearOverride(s, d);
            break;
        case 2: // setSolo
            if (s >= 0 && s < kNumPads && d >= 0 && d < kNumPads)
                couplingMatrix_.setSoloPath(s, d);
            break;
        case 3: // clearSolo
            couplingMatrix_.clearSolo();
            break;
        default:
            break;
        }
        return kResultOk;
    }

    if (std::strcmp(id, "CouplingMatrixSnapshotRequest") == 0)
    {
        // Serialise the current override map and send it back to the
        // controller so its uiCouplingMatrix_ mirror can re-sync.
        auto* reply = allocateMessage();
        if (reply == nullptr) return kResultOk;
        Steinberg::IPtr<Steinberg::Vst::IMessage> owned(reply, /*addRef*/ false);
        owned->setMessageID("CouplingMatrixSnapshot");
        auto* attrs = owned->getAttributes();
        if (attrs == nullptr) return kResultOk;

        // Record layout: uint8 src, uint8 dst, float32 coeff (6 bytes).
        constexpr int kRecordSize = 1 + 1 + 4;
        std::vector<unsigned char> blob;
        blob.reserve(static_cast<std::size_t>(
            couplingMatrix_.getOverrideCount()) * kRecordSize);
        // NOLINTNEXTLINE(bugprone-exception-escape): blob.reserve() above
        // sizes the vector to the exact final capacity; push_back/insert
        // within reserved capacity cannot throw, so the lambda is effectively
        // noexcept even though std::vector's member functions are not.
        couplingMatrix_.forEachOverride(
            [&blob](int src, int dst, float coeff) {
                blob.push_back(static_cast<unsigned char>(src));
                blob.push_back(static_cast<unsigned char>(dst));
                unsigned char coeffBytes[4];
                std::memcpy(coeffBytes, &coeff, sizeof(float));
                blob.insert(blob.end(), coeffBytes, coeffBytes + 4);
            });

        attrs->setBinary("overrides",
            blob.empty() ? static_cast<const void*>("") : blob.data(),
            static_cast<Steinberg::uint32>(blob.size()));
        sendMessage(owned);
        return kResultOk;
    }

    return AudioEffect::notify(message);
}

} // namespace Membrum
