// =============================================================================
// PinFlagStrip — 32-cell toggle row for Step Pinning (Pitch lane)
// =============================================================================
// Spec 133 (Gradus v1.6): inline pin-grid editor for the Pitch lane.
// Renders 32 small toggle cells immediately above the pitch bars. Click a
// cell to toggle the corresponding kArpPinFlagStepNId parameter.
// Visibility is managed externally (only shown when Pitch lane is active).
//
// All testable logic lives in pin_flag_strip_logic.h (humble object pattern).
// This file only wires drawing, mouse handling, and callback dispatch to the
// underlying state.
// =============================================================================

#pragma once

#include "plugin_ids.h"
#include "pin_flag_strip_logic.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <functional>

namespace Gradus {

class PinFlagStrip : public VSTGUI::CControl {
public:
    static constexpr int kNumSteps = PinFlagStripLogic::kNumSteps;

    explicit PinFlagStrip(const VSTGUI::CRect& size)
        : CControl(size, nullptr, -1, nullptr)
    {
    }

    ~PinFlagStrip() override = default;

    // -------------------------------------------------------------------------
    // State accessors (thin wrappers around PinFlagStripLogic)
    // -------------------------------------------------------------------------

    float getStepValue(int step) const
    {
        return PinFlagStripLogic::getStepValue(state_, step);
    }

    // Number of currently visible cells (clamped to [1, kNumSteps]).
    // Must track the pitch lane's active length so cells align with the
    // pitch bars underneath. Host-driven; called from the Controller.
    void setNumSteps(int n)
    {
        const int clamped = (n < 1) ? 1 : (n > kNumSteps ? kNumSteps : n);
        if (clamped != numSteps_) {
            numSteps_ = clamped;
            setDirty(true);
        }
    }

    int getNumSteps() const { return numSteps_; }

    // Host-driven update (preset load, automation). Does NOT fire callbacks.
    // Uses setDirty(true) rather than invalid() because this may be called
    // from a non-UI thread via Controller::setParamNormalized.
    void setStepValue(int step, float value)
    {
        if (PinFlagStripLogic::setStepValue(state_, step, value)) {
            setDirty(true);
        }
    }

    // User-driven toggle (from onMouseDown, UI thread only).
    // Fires beginEdit/paramCallback/endEdit in that order.
    void toggleStep(int step)
    {
        if (step < 0 || step >= kNumSteps) return;
        const float newValue = PinFlagStripLogic::toggleStep(state_, step);

        const auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Gradus::kArpPinFlagStep0Id + step);

        if (beginEditCallback_) beginEditCallback_(paramId);
        if (paramCallback_)     paramCallback_(paramId, newValue);
        if (endEditCallback_)   endEditCallback_(paramId);

        invalid();
    }

    // -------------------------------------------------------------------------
    // Parameter callbacks (mirrors ArpLaneEditor / TapPatternEditor pattern)
    // -------------------------------------------------------------------------

    using ParameterCallback =
        std::function<void(Steinberg::Vst::ParamID paramId, float normalizedValue)>;
    using EditGateCallback =
        std::function<void(Steinberg::Vst::ParamID paramId)>;

    void setParameterCallback(ParameterCallback cb) { paramCallback_ = std::move(cb); }
    void setBeginEditCallback(EditGateCallback cb)  { beginEditCallback_ = std::move(cb); }
    void setEndEditCallback  (EditGateCallback cb)  { endEditCallback_ = std::move(cb); }

    // -------------------------------------------------------------------------
    // CControl overrides
    // -------------------------------------------------------------------------

    void draw(VSTGUI::CDrawContext* context) override
    {
        const VSTGUI::CRect bounds = getViewSize();
        const float w = static_cast<float>(bounds.getWidth());
        const float h = static_cast<float>(bounds.getHeight());
        if (w <= 0.0f || h <= 0.0f) return;

        // Background
        context->setFillColor({0x16, 0x16, 0x1C, 0xC0});
        context->drawRect(bounds, VSTGUI::kDrawFilled);

        const int visibleSteps = numSteps_;
        const float cellWidth = w / static_cast<float>(visibleSteps);
        const float padding = 1.5f;

        const VSTGUI::CColor accentFilled   {0xE8, 0xC8, 0x4C, 0xFF}; // amber/gold
        const VSTGUI::CColor accentOutline  {0x6A, 0x5C, 0x2E, 0xFF};
        const VSTGUI::CColor unfilledStroke {0x44, 0x44, 0x4A, 0xFF};

        for (int i = 0; i < visibleSteps; ++i) {
            VSTGUI::CRect cell(
                bounds.left + static_cast<VSTGUI::CCoord>(i) * cellWidth + padding,
                bounds.top + padding,
                bounds.left + static_cast<VSTGUI::CCoord>(i + 1) * cellWidth - padding,
                bounds.bottom - padding);

            if (PinFlagStripLogic::isPinned(state_[static_cast<size_t>(i)])) {
                context->setFillColor(accentFilled);
                context->drawRect(cell, VSTGUI::kDrawFilled);
                context->setFrameColor(accentOutline);
                context->setLineWidth(1.0);
                context->drawRect(cell, VSTGUI::kDrawStroked);
            } else {
                context->setFrameColor(unfilledStroke);
                context->setLineWidth(1.0);
                context->drawRect(cell, VSTGUI::kDrawStroked);
            }
        }

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (!buttons.isLeftButton()) return VSTGUI::kMouseEventNotHandled;

        const VSTGUI::CRect bounds = getViewSize();
        const float localX = static_cast<float>(where.x - bounds.left);
        const int idx = PinFlagStripLogic::cellIndexForX(
            localX, static_cast<float>(bounds.getWidth()), numSteps_);
        if (idx < 0) return VSTGUI::kMouseEventNotHandled;

        toggleStep(idx);
        return VSTGUI::kMouseEventHandled;
    }

    CLASS_METHODS(PinFlagStrip, CControl)

private:
    PinFlagStripLogic::State state_{};
    int numSteps_ = kNumSteps;  // tracks pitch lane active length

    ParameterCallback paramCallback_;
    EditGateCallback  beginEditCallback_;
    EditGateCallback  endEditCallback_;
};

} // namespace Gradus
