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
        applyViewRefresh(flags);
    else
        pendingViewRefresh_.fetch_or(flags, std::memory_order_relaxed);
}

void Controller::applyViewRefresh(std::uint32_t flags) noexcept
{
    if (flags & kRefreshMorphControls)    updateMorphControlsEnabled();
    if (flags & kRefreshPitchEnvControls) updatePitchEnvControlsEnabled();
    if (flags & kRefreshMorphToggleVis)   updateMorphEnabledToggleVisibility();
    if (flags & kRefreshFilterEnvDisplay) updateFilterEnvDisplay();
    if (flags & kRefreshPitchEnvDisplay)  updatePitchEnvelopeDisplay();
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
// Reflect the BodyModel selection (via the global proxy that tracks the
// selected pad) onto the cached pitch-envelope views. The pitch envelope only
// retargets the modal bank's f0 for Membrane bodies -- on Plate/Shell/String/
// Bell/NoiseBody it advances but its output is ignored (see drum_voice.h:486).
// Dim + block mouse input for the entire section when the selected body is
// not Membrane so the UI cannot suggest controls that have no audible effect.
// ------------------------------------------------------------------------------
bool Controller::isMembraneBodySelectedForTest() noexcept
{
    // Mirror the processor's normalized-to-discrete clamp (processor.cpp:288):
    // bodyIdx = clamp(static_cast<int>(norm * kCount), 0, kCount - 1). Membrane
    // is 0, which corresponds to norm < 1/6.
    const auto bodyNorm =
        getParamNormalized(static_cast<ParamID>(kBodyModelId));
    const int bodyIdx = std::clamp(
        static_cast<int>(bodyNorm * static_cast<double>(BodyModelType::kCount)),
        0,
        static_cast<int>(BodyModelType::kCount) - 1);
    return bodyIdx == static_cast<int>(BodyModelType::Membrane);
}

void Controller::updatePitchEnvControlsEnabled() noexcept
{
    const bool enabled = isMembraneBodySelectedForTest();
    const float alpha  = enabled ? 1.0f : 0.35f;

    auto apply = [enabled, alpha](VSTGUI::CView* v) {
        if (v == nullptr) return;
        if (v->getAlphaValue() == alpha
            && v->getMouseEnabled() == enabled)
            return;
        v->setAlphaValue(alpha);
        v->setMouseEnabled(enabled);
        v->invalid();
    };

    apply(pitchEnvelopeDisplay_);
    apply(pitchEnvKneeView_);
    apply(pitchEnvKneeLabel_);
}

// ------------------------------------------------------------------------------
// Hide the Material Morph power toggle when the selected pad's body is not
// Membrane. Material Morph's body-mapper refresh only supports Membrane bodies
// (drum_voice.h:1238) -- on every other body the morph counter ticks but the
// mode bank is never re-mapped, so the section is inert. Hiding the toggle
// removes the only entry point for enabling that inert path.
//
// We deliberately preserve the underlying kMorphEnabledId parameter value
// across body switches: a pad set to Membrane with morph ON, then flipped to
// Plate, will re-show the toggle (still ON) when flipped back. That keeps
// preset state stable and avoids surprising the user with a parameter write
// triggered purely by a body change.
// ------------------------------------------------------------------------------
void Controller::updateMorphEnabledToggleVisibility() noexcept
{
    if (morphEnabledToggleView_ == nullptr)
        return;
    const bool visible = isMembraneBodySelectedForTest();
    // Always call setVisible() rather than short-circuiting on the current
    // isVisible() state: UIViewSwitchContainer's template build can leave the
    // toggle in a transient non-default state at the moment verifyView fires,
    // and a short-circuit there strands the toggle hidden until the next
    // explicit param edit. setVisible() is cheap and idempotent.
    morphEnabledToggleView_->setVisible(visible);
    morphEnabledToggleView_->invalid();
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
