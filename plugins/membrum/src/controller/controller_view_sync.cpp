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
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace Membrum {
using namespace Steinberg;
using namespace Steinberg::Vst;

// ==============================================================================
// Controller view sync: bus tooltip, refresh, section enable/visibility, env displays, meters
// ==============================================================================


// ------------------------------------------------------------------------------
// Phase 8 (T074 / US7 / FR-066): push a warning tooltip onto the Output Bus
// selector when the currently-selected aux bus is inactive. FR-066 requires
// the message to read "Host must activate Aux {N} bus". Clears the tooltip
// when the bus is active or when Main (bus 0) is selected. Tolerant of a
// missing view -- safe to call before or after the editor opens.
// ------------------------------------------------------------------------------
void Controller::updateOutputBusTooltip() noexcept
{
    if (outputBusSelView_ == nullptr)
        return;

    // Resolve the currently-selected bus index from the global proxy value.
    // The parameter is a 16-entry StringListParameter so its normalised value
    // maps to [0, kMaxOutputBuses - 1] via round-to-nearest.
    const auto norm = getParamNormalized(static_cast<ParamID>(kOutputBusId));
    const int busIndex = std::clamp(
        static_cast<int>(std::lround(norm * (kMaxOutputBuses - 1))),
        0, kMaxOutputBuses - 1);

    if (busIndex >= 1 && !isBusActive(busIndex))
    {
        char buf[64] = {};
        std::snprintf(buf, sizeof(buf),
                      "Host must activate Aux %d bus", busIndex);
        outputBusSelView_->setTooltipText(buf);
    }
    else
    {
        // Clear any stale warning: VSTGUI's CView::setTooltipText with an
        // empty string removes the tooltip.
        outputBusSelView_->setTooltipText("");
    }
}

// ------------------------------------------------------------------------------
// Reflect the MorphEnabled (power toggle) state onto the Material Morph
// section's controls: dim to 0.35 alpha and block mouse input when off,
// restore to 1.0 and re-enable when on. Tolerant of any null view pointer,
// so it is safe to call before verifyView caches them or after willClose
// zeros them.
// ------------------------------------------------------------------------------
// ------------------------------------------------------------------------------
// Audit finding 12: keep all VSTGUI view mutation on the UI thread. The SDK
// tags setParamNormalized()/setComponentState() as [UI-thread], so on a
// compliant host this applies immediately (zero added latency, identical to the
// old inline calls). A non-compliant host driving those callbacks from a worker
// thread instead queues the work for the 30 Hz poll timer, which drains it on
// the UI thread within ~33 ms. The helpers are all idempotent and null-safe.
// ------------------------------------------------------------------------------
void Controller::requestViewRefresh(std::uint32_t flags) noexcept
{
    if (std::this_thread::get_id() == uiThreadId_)
    {
        applyViewRefresh(flags);
        return;
    }
    pendingViewRefresh_.fetch_or(flags, std::memory_order_relaxed);
    // MB-157: with no editor open no poll timer drains the word and no view
    // is on screen, so apply now as well (a grid built by createCustomView
    // before the editor opens still follows every change). The queued bits
    // stay: a later editor's first tick re-applies them, which is idempotent.
    if (activeEditor_ == nullptr)
        applyViewRefresh(flags);
}

void Controller::applyViewRefresh(std::uint32_t flags) noexcept
{
    if (flags & kRefreshMorphControls)    updateMorphControlsEnabled();
    if (flags & kRefreshContextControls)  updateContextControlsEnabled();
    if (flags & kRefreshPadGrid)          updatePadGrid();
    if (flags & kRefreshFilterEnvDisplay) updateFilterEnvDisplay();
    if (flags & kRefreshPitchEnvDisplay)  updatePitchEnvelopeDisplay();
    if (flags & kRefreshMorphPad)         updateMorphPadPosition();
}

void Controller::drainPendingViewRefresh() noexcept
{
    const auto pending = pendingViewRefresh_.exchange(0, std::memory_order_relaxed);
    if (pending != 0)
        applyViewRefresh(pending);
}

// ------------------------------------------------------------------------------
// MB-101: the pad grid's per-pad meta, decoded from the controller's own
// per-pad parameters exactly as the processor decodes them:
//   exciterType, bodyModel   clamp(int(norm * kCount), 0, kCount - 1)
//                            (processor.cpp processParameterChanges)
//   chokeGroup               clamp(int(norm * 8 + 0.5), 0, 8)
//   outputBus                clamp(int(norm * 15 + 0.5), 0, 15)
//                            (VoicePool::setPadConfigField)
//   tsPitchEnvTime           the normalized value.
// The macros never switch the pitch envelope on or off (macro_offsets.h), so
// the category these fields give is the one the processor classifies.
// ------------------------------------------------------------------------------
void Controller::refreshGridMeta() noexcept
{
    const auto norm = [this](int pad, int offset) {
        return getParamNormalized(static_cast<ParamID>(padParamId(pad, offset)));
    };
    const int exciterCount = static_cast<int>(ExciterType::kCount);
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        auto& meta = gridMeta_[static_cast<std::size_t>(pad)];
        meta.exciterType = static_cast<ExciterType>(std::clamp(
            static_cast<int>(norm(pad, kPadExciterType)
                             * static_cast<double>(exciterCount)),
            0, exciterCount - 1));
        meta.bodyModel = decodeBodyModel(norm(pad, kPadBodyModel));
        meta.tsPitchEnvTime = static_cast<float>(norm(pad, kPadTSPitchEnvTime));
        meta.chokeGroup = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(norm(pad, kPadChokeGroup) * 8.0 + 0.5), 0, 8));
        meta.outputBus = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(norm(pad, kPadOutputBus) * 15.0 + 0.5), 0, 15));
    }
}

void Controller::updatePadGrid() noexcept
{
    if (padGridView_ == nullptr)
        return;
    refreshGridMeta();
    padGridView_->setSelectedPadIndex(selectedPadIndex_);
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        padGridView_->setPadEnabled(
            pad, getParamNormalized(static_cast<ParamID>(
                     padParamId(pad, kPadEnabled))) >= 0.5);
    }
    // The glyph and the CG / BUS text come from gridMeta_ at draw time.
    padGridView_->invalid();
}

void Controller::updateMorphPadPosition() noexcept
{
    if (xyMorphPad_ == nullptr || xyMorphPad_->isDragging())
        return;
    xyMorphPad_->setMorphPosition(
        static_cast<float>(getParamNormalized(static_cast<ParamID>(kMorphStartId))),
        static_cast<float>(getParamNormalized(static_cast<ParamID>(kMorphEndId))));
}

void Controller::updateMorphControlsEnabled() noexcept
{
    const bool enabled =
        getParamNormalized(static_cast<ParamID>(kMorphEnabledId)) >= 0.5;
    const float alpha = enabled ? 1.0f : 0.35f;

    // Guard against redundant setAlphaValue()/invalid() churn: verifyView()
    // invokes this helper for every Material Morph view as they are built, so
    // most calls end up asking for the alpha a previously-cached view already
    // has. Comparing to getAlphaValue() short-circuits those no-ops.
    auto apply = [enabled, alpha](VSTGUI::CView* v) {
        if (v == nullptr) return;
        if (v->getAlphaValue() == alpha
            && v->getMouseEnabled() == enabled)
            return;
        v->setAlphaValue(alpha);
        v->setMouseEnabled(enabled);
        v->invalid();
    };

    apply(xyMorphPad_);
    apply(morphDurationView_);
    apply(morphCurveView_);
    apply(morphDurLabel_);
}

// ------------------------------------------------------------------------------
// MB-085: context-only controls. The pitch envelope and Material Morph drive
// every body (drum_voice.h: retuneFundamental, refreshBodyForMaterialOnBank),
// so they are not in this table. Each control below acts in one context only
// and is dimmed (alpha 0.35, mouse off) everywhere else:
//   FM / FB / Burst / Press: their own exciter only (exciter_bank.h forwards
//     each setter only to its own alternative).
//   Air: read only by membrane_mapper.h. Tens: Membrane-only in drum_voice.h.
//   Shell: needs 2nd on. 2Sz / 2Mat: need 2nd on AND Shell > 0 (drum_voice.h
//     secondaryEnabled_ >= 0.5 && couplingStrength_ > 0).
//   Noise Cutf / Res / Dec / Color: need noise mix > 0.
//   Wire: modulates the noise layer from the modal energy, so it needs noise
//     mix > 0 and a body other than String (computeWireGainMod).
// ------------------------------------------------------------------------------
namespace {

constexpr std::array<ParamID, 14> kContextOnlyTags{
    kExciterFMRatioId,   kExciterFeedbackAmountId, kExciterNoiseBurstDurationId,
    kExciterFrictionPressureId, kAirLoadingId,     kTensionModAmtId,
    kCouplingStrengthId, kSecondarySizeId,         kSecondaryMaterialId,
    kNoiseLayerCutoffId, kNoiseLayerResonanceId,   kNoiseLayerDecayId,
    kNoiseLayerColorId,  kWireCouplingId};

bool isContextOnlyTag(ParamID tag) noexcept
{
    return std::find(kContextOnlyTags.begin(), kContextOnlyTags.end(), tag)
           != kContextOnlyTags.end();
}

template <typename Fn>
void forEachControl(VSTGUI::CViewContainer* container, const Fn& fn)
{
    container->forEachChild([&fn](VSTGUI::CView* child) {
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(child))
            fn(control);
        if (auto* sub = child->asViewContainer())
            forEachControl(sub, fn);
    });
}

} // namespace

bool Controller::isContextControlLiveForTest(ParamID tag) noexcept
{
    if (!isContextOnlyTag(tag))
        return true;

    // Decode the exciter and body the way the processor does:
    // clamp(int(norm * kCount), 0, kCount - 1).
    const int exciterCount = static_cast<int>(ExciterType::kCount);
    const auto exciter = static_cast<ExciterType>(std::clamp(
        static_cast<int>(getParamNormalized(static_cast<ParamID>(kExciterTypeId))
                         * static_cast<double>(exciterCount)),
        0, exciterCount - 1));
    const BodyModelType body =
        decodeBodyModel(getParamNormalized(static_cast<ParamID>(kBodyModelId)));
    const bool secondaryOn =
        getParamNormalized(static_cast<ParamID>(kSecondaryEnabledId)) >= 0.5;
    const bool shellOn =
        getParamNormalized(static_cast<ParamID>(kCouplingStrengthId)) > 0.0;
    const bool noiseOn =
        getParamNormalized(static_cast<ParamID>(kNoiseLayerMixId)) > 0.0;

    switch (tag)
    {
    case kExciterFMRatioId:            return exciter == ExciterType::FMImpulse;
    case kExciterFeedbackAmountId:     return exciter == ExciterType::Feedback;
    case kExciterNoiseBurstDurationId: return exciter == ExciterType::NoiseBurst;
    case kExciterFrictionPressureId:   return exciter == ExciterType::Friction;
    case kAirLoadingId:
    case kTensionModAmtId:             return body == BodyModelType::Membrane;
    case kCouplingStrengthId:          return secondaryOn;
    case kSecondarySizeId:
    case kSecondaryMaterialId:         return secondaryOn && shellOn;
    case kNoiseLayerCutoffId:
    case kNoiseLayerResonanceId:
    case kNoiseLayerDecayId:
    case kNoiseLayerColorId:           return noiseOn;
    case kWireCouplingId:              return noiseOn && body != BodyModelType::String;
    default:                           return true;
    }
}

void Controller::applyContextControlState(VSTGUI::CControl* control) noexcept
{
    if (control == nullptr || control->getTag() < 0)
        return;
    const auto tag = static_cast<ParamID>(control->getTag());
    if (!isContextOnlyTag(tag))
        return;
    const bool live = isContextControlLiveForTest(tag);
    const float alpha = live ? 1.0f : 0.35f;
    if (control->getAlphaValue() == alpha && control->getMouseEnabled() == live)
        return;
    control->setAlphaValue(alpha);
    control->setMouseEnabled(live);
    control->invalid();
}

void Controller::updateContextControlsEnabled() noexcept
{
    if (activeEditor_ == nullptr)
        return;
    auto* frame = activeEditor_->getFrame();
    if (frame == nullptr)
        return;
    forEachControl(frame, [this](VSTGUI::CControl* control) {
        applyContextControlState(control);
    });
}

void Controller::setContextParamDependency(bool on) noexcept
{
    if (on == contextParamDependency_)
        return;
    contextParamDependency_ = on;
    for (const ParamID tag : kContextOnlyTags)
    {
        auto* parameter = getParameterObject(tag);
        if (parameter == nullptr)
            continue;
        if (on)
            parameter->addDependent(this);
        else
            parameter->removeDependent(this);
    }
}

void PLUGIN_API Controller::update(FUnknown* changedUnknown, int32 message)
{
    EditControllerEx1::update(changedUnknown, message);
    if (message != IDependent::kChanged)
        return;
    // Always queued, never applied here: the controller can be notified before
    // VST3Editor's ParameterChangeListener, which then re-enables the mouse.
    const auto* parameter = FCast<Parameter>(changedUnknown);
    if (parameter != nullptr && isContextOnlyTag(parameter->getInfo().id))
        pendingViewRefresh_.fetch_or(kRefreshContextControls,
                                     std::memory_order_relaxed);
}

// ------------------------------------------------------------------------------
// Push the four Tone Shaper filter-envelope normalized values into the cached
// ADSRDisplay, converting attack/decay/release to their true DSP millisecond
// ranges (x500 for attack, x2000 for decay and release -- see
// processor.cpp:72-75). Sustain is a pure [0,1] level. Tolerant of a null
// display pointer, so this is safe to call before verifyView populates the
// pointer or after willClose zeros it.
// ------------------------------------------------------------------------------
void Controller::updateFilterEnvDisplay() noexcept
{
    if (filterEnvDisplay_ == nullptr && filterEnvOverlayDisplay_ == nullptr)
        return;

    const auto attackNorm  = getParamNormalized(
        static_cast<ParamID>(kToneShaperFilterEnvAttackId));
    const auto decayNorm   = getParamNormalized(
        static_cast<ParamID>(kToneShaperFilterEnvDecayId));
    const auto sustainNorm = getParamNormalized(
        static_cast<ParamID>(kToneShaperFilterEnvSustainId));
    const auto releaseNorm = getParamNormalized(
        static_cast<ParamID>(kToneShaperFilterEnvReleaseId));

    // Decode cubically (norm^3 * maxMs) to round-trip the ADSRDisplay's drag
    // encoding (adsr_display.h::normalizedToTimeMs). Linear decoding here
    // strands the display at a different ms than the user dragged to after a
    // pad-switch sync or any other refresh path. Sustain is a pure [0,1]
    // level and needs no scaling.
    const float aN = std::clamp(static_cast<float>(attackNorm),  0.0f, 1.0f);
    const float dN = std::clamp(static_cast<float>(decayNorm),   0.0f, 1.0f);
    const float rN = std::clamp(static_cast<float>(releaseNorm), 0.0f, 1.0f);
    const float attackMs  = aN * aN * aN * 500.0f;
    const float decayMs   = dN * dN * dN * 2000.0f;
    const float sustain   = static_cast<float>(sustainNorm);
    const float releaseMs = rN * rN * rN * 2000.0f;

    auto pushTo = [&](Krate::Plugins::ADSRDisplay* display) {
        if (display == nullptr) return;
        display->setAttackMs(attackMs);
        display->setDecayMs(decayMs);
        display->setSustainLevel(sustain);
        display->setReleaseMs(releaseMs);
    };
    pushTo(filterEnvDisplay_);
    pushTo(filterEnvOverlayDisplay_);
}

// ------------------------------------------------------------------------------
// Push the four Tone Shaper pitch-envelope normalised values into the cached
// PitchEnvelopeDisplay. The display owns its four parameter values internally
// (startN_/endN_/timeN_/curveN_) -- they are not derived from CControl's
// single-tag value_, so the standard EditController -> CControl propagation
// path leaves them stale. This pushes them by hand whenever the host,
// automation, a pad switch, or a UIViewSwitchContainer rebuild changes the
// underlying state. Tolerant of a null display pointer.
// ------------------------------------------------------------------------------
void Controller::updatePitchEnvelopeDisplay() noexcept
{
    if (pitchEnvelopeDisplay_ == nullptr)
        return;

    const auto startNorm       = getParamNormalized(
        static_cast<ParamID>(kToneShaperPitchEnvStartId));
    const auto endNorm         = getParamNormalized(
        static_cast<ParamID>(kToneShaperPitchEnvEndId));
    const auto timeNorm        = getParamNormalized(
        static_cast<ParamID>(kToneShaperPitchEnvTimeId));
    const auto curveNorm       = getParamNormalized(
        static_cast<ParamID>(kToneShaperPitchEnvCurveId));
    const auto kneeNorm        = getParamNormalized(
        static_cast<ParamID>(kPitchEnvKneeEnabledId));
    const auto midPitchNorm    = getParamNormalized(
        static_cast<ParamID>(kPitchEnvMidPitchId));
    const auto midFractionNorm = getParamNormalized(
        static_cast<ParamID>(kPitchEnvMidFractionId));
    const auto curve2Norm      = getParamNormalized(
        static_cast<ParamID>(kPitchEnvCurve2Id));

    pitchEnvelopeDisplay_->setStartNormalized       (static_cast<float>(startNorm));
    pitchEnvelopeDisplay_->setEndNormalized         (static_cast<float>(endNorm));
    pitchEnvelopeDisplay_->setTimeNormalized        (static_cast<float>(timeNorm));
    pitchEnvelopeDisplay_->setCurveNormalized       (static_cast<float>(curveNorm));
    pitchEnvelopeDisplay_->setKneeEnabled           (kneeNorm >= 0.5);
    pitchEnvelopeDisplay_->setMidPitchNormalized    (static_cast<float>(midPitchNorm));
    pitchEnvelopeDisplay_->setMidFractionNormalized (static_cast<float>(midFractionNorm));
    pitchEnvelopeDisplay_->setCurve2Normalized      (static_cast<float>(curve2Norm));
}

// ------------------------------------------------------------------------------
// updateMeterViews (T046): push MetersBlock values to cached views.
// Tolerant of missing views -- safe when editor is not open.
// ------------------------------------------------------------------------------
void Controller::updateMeterViews(const MetersBlock& meters) noexcept
{
    if (kitMetersView_ != nullptr)
    {
        kitMetersView_->setPeaks(meters.peakL, meters.peakR);
    }
    if (cpuLabel_ != nullptr)
    {
        // cpuPermille is 0..1000 (per-mille). Display as whole percent.
        const auto percent =
            static_cast<unsigned int>((meters.cpuPermille + 5) / 10);
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "CPU: %u%%", percent);
        cpuLabel_->setText(buf);
    }

    // T060/T062 (Phase 6 / US5): push MetersBlock.activeVoices into the
    // Kit Column readout. The label title prefix marker ("ActiveVoices") is
    // discovered in verifyView() so we never collide with the CPU label.
    if (activeVoicesLabel_ != nullptr)
    {
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "ActiveVoices: %u",
                      static_cast<unsigned int>(meters.activeVoices));
        activeVoicesLabel_->setText(buf);
    }

    // T056: surface preset load failures on the status label. A fresh failure
    // arms a ~3 second countdown (90 ticks at 30 Hz); when it elapses both
    // the label and the latched flags are cleared. Tolerant of a missing
    // label: the flags still clear on timeout so state does not accumulate.
    constexpr int kPresetStatusDurationTicks = 90; // ~3 s at 30 Hz
    if (kitPresetLoadFailed_ || padPresetLoadFailed_)
    {
        if (presetStatusClearTicks_ == 0)
        {
            const char* text = kitPresetLoadFailed_
                                   ? "Kit preset load failed"
                                   : "Pad preset load failed";
            if (presetStatusLabel_ != nullptr)
                presetStatusLabel_->setText(text);
            presetStatusClearTicks_ = kPresetStatusDurationTicks;
        }
    }
    if (presetStatusClearTicks_ > 0)
    {
        if (--presetStatusClearTicks_ == 0)
        {
            if (presetStatusLabel_ != nullptr)
                presetStatusLabel_->setText("");
            kitPresetLoadFailed_ = false;
            padPresetLoadFailed_ = false;
        }
    }
}
} // namespace Membrum
