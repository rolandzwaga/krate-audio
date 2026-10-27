#pragma once

// ==============================================================================
// Profundum Phase 14 - Profundum::MorphIndicatorSlider (FR-035; plan 4.9)
// ==============================================================================
// Spec:  specs/profundum-phase14-morph-modulation/spec.md  (FR-035; SC-024 (e)(f))
// Plan:  specs/profundum-phase14-morph-modulation/plan.md  (section 4.9)
// Tasks: specs/profundum-phase14-morph-modulation/tasks.md (T018)
//
// The Main-page Morph Position slider. A VSTGUI::CSlider that also draws a 2 px
// marker at the RESOLVED morph position the processor publishes in the spectrum
// frame (ProfundumSpectrumFrame::morphPosition), so Morph Mod movement is visible
// while the slider keeps showing (and editing) the base position.
//
// The marker is display only (the OffsetRingKnob precedent, offset_ring_knob.h):
// setIndicator / clearIndicator never call setValue, valueChanged, beginEdit or
// performEdit, and every mouse / wheel / keyboard interaction is CSlider's own.
// Before the first frame of an opened queue the marker sits on the slider value
// (clearIndicator). UI thread only.
//
// Registration is a ViewCreatorAdapter with base view "CSlider", so the uidesc's
// control-tag and every CSlider attribute still apply. It registers in every
// linked TU that includes this header (controller.cpp).
// ==============================================================================

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/controls/cslider.h"
#include "vstgui/lib/crect.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/uiviewfactory.h"

#include <cstdint>

namespace Profundum {

class MorphIndicatorSlider : public VSTGUI::CSlider {
public:
    /// The marker colour (plan 4.9: fixed, no uidesc attribute).
    static constexpr VSTGUI::CColor kIndicatorColor{255, 255, 255, 220};
    /// The marker width in pixels.
    static constexpr double kIndicatorWidth = 2.0;

    MorphIndicatorSlider(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag);
    MorphIndicatorSlider(const MorphIndicatorSlider& other)
        : CSlider(other), indicator_(other.indicator_), has_(other.has_) {}

    /// Stores @p resolved clamped to [0, 1] (a non-finite value reads 0) and marks the view dirty only when the stored
    /// value changes bitwise. Never touches the control value.
    void setIndicator(float resolved) noexcept;
    /// Drops the stored position: the marker falls back to the slider's own value (C-18).
    void clearIndicator() noexcept;
    /// The drawn position: the stored value, or getValueNormalized() while none is stored.
    [[nodiscard]] float indicatorPosition() const noexcept;
    [[nodiscard]] bool hasIndicator() const noexcept { return has_; }

    /// CSlider::draw, then the 2 px full-height marker at the centre of calculateHandleRect(indicatorPosition()).
    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS(MorphIndicatorSlider, VSTGUI::CSlider)

private:
    float indicator_ = 0.0f;
    bool has_ = false;
};

// ==============================================================================
// ViewCreator Registration
// ==============================================================================

struct MorphIndicatorSliderCreator : VSTGUI::ViewCreatorAdapter {
    MorphIndicatorSliderCreator() { VSTGUI::UIViewFactory::registerViewCreator(*this); }

    VSTGUI::IdStringPtr getViewName() const override { return "MorphIndicatorSlider"; }

    /// The CSlider creator applies every slider attribute, then CControl's.
    VSTGUI::IdStringPtr getBaseViewName() const override { return "CSlider"; }

    VSTGUI::UTF8StringPtr getDisplayName() const override { return "Morph Indicator Slider"; }

    VSTGUI::CView* create(const VSTGUI::UIAttributes& /*attributes*/,
                          const VSTGUI::IUIDescription* /*description*/) const override {
        return new MorphIndicatorSlider(VSTGUI::CRect(0, 0, 0, 0), nullptr, -1);
    }
};

// Inline variable (C++17) - safe for inclusion from multiple translation units.
// It only registers in a TU that is actually LINKED (offset_ring_knob.h's rule).
inline MorphIndicatorSliderCreator gMorphIndicatorSliderCreator;

}  // namespace Profundum
