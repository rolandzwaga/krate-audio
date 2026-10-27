// ==============================================================================
// Profundum Phase 14 - MorphIndicatorSlider implementation (FR-035; plan 4.9)
// ==============================================================================
// Tasks: specs/profundum-phase14-morph-modulation/tasks.md (T018)
// ==============================================================================

#include "ui/morph_indicator_slider.h"

#include "vstgui/lib/cdrawcontext.h"

#include <krate/dsp/core/db_utils.h>  // detail::isFinite (fast-math safe)

#include <algorithm>
#include <bit>
#include <cstdint>

namespace Profundum {

// The CSlider creator's own constructor shape (sliderviewcreator.cpp: no handle bitmap, so a 1 x 1 handle), then the
// size through CSliderBase::setViewSize so the handle range matches the view (the uidesc path does the same when it
// applies "size").
MorphIndicatorSlider::MorphIndicatorSlider(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag)
    : CSlider(size, listener, tag, 0, 0, nullptr, nullptr) {
    setViewSize(size, false);
}

void MorphIndicatorSlider::setIndicator(float resolved) noexcept {
    const float v = Krate::DSP::detail::isFinite(resolved) ? std::clamp(resolved, 0.0f, 1.0f) : 0.0f;
    const bool changed = !has_ || std::bit_cast<std::uint32_t>(v) != std::bit_cast<std::uint32_t>(indicator_);
    indicator_ = v;
    has_ = true;
    if (changed) {
        invalid();
    }
}

void MorphIndicatorSlider::clearIndicator() noexcept {
    if (has_) {
        has_ = false;
        invalid();
    }
}

float MorphIndicatorSlider::indicatorPosition() const noexcept {
    return has_ ? indicator_ : getValueNormalized();
}

void MorphIndicatorSlider::draw(VSTGUI::CDrawContext* context) {
    CSlider::draw(context);  // ends with setDirty(false)
    if (context == nullptr) {
        return;
    }
    const VSTGUI::CRect vs = getViewSize();
    const VSTGUI::CRect handle = calculateHandleRect(indicatorPosition());
    const double x = handle.left + (handle.getWidth() / 2.0);
    const VSTGUI::CRect marker(x - (kIndicatorWidth / 2.0), vs.top, x + (kIndicatorWidth / 2.0), vs.bottom);
    context->setDrawMode(VSTGUI::kAliasing);
    context->setFillColor(kIndicatorColor);
    context->drawRect(marker, VSTGUI::kDrawFilled);
}

}  // namespace Profundum
