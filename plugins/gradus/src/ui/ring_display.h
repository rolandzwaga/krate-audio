#pragma once

// ==============================================================================
// RingDisplay — Container Wrapper for RingRenderer
// ==============================================================================
// CViewContainer that holds the RingRenderer. Provides a named custom view
// for the uidesc and manages the renderer lifecycle.
// ==============================================================================

#include "ring_renderer.h"

#include "vstgui/lib/cviewcontainer.h"

namespace Gradus {

class RingDisplay : public VSTGUI::CViewContainer {
public:
    explicit RingDisplay(const VSTGUI::CRect& size)
        : CViewContainer(size)
    {
        setBackgroundColor({0x1A, 0x1A, 0x1E, 0xFF});

        // Create the ring renderer filling the entire container
        renderer_ = new RingRenderer(VSTGUI::CRect(0, 0, size.getWidth(),
                                                    size.getHeight()));
        addView(renderer_);
    }

    RingRenderer* getRenderer() const { return renderer_; }

    /// Update geometry to match current container size.
    void updateGeometry()
    {
        if (!renderer_) return;

        auto size = getViewSize();
        float w = static_cast<float>(size.getWidth());
        float h = static_cast<float>(size.getHeight());
        float minDim = std::min(w, h);

        // Center the ring in the available space
        auto& geo = renderer_->geometry();
        geo.setCenter(w * 0.5f, h * 0.5f);

        // Scale radii proportionally if container is not 660x660
        float scale = minDim / 660.0f;
        if (std::abs(scale - 1.0f) > 0.01f) {
            // Re-apply default layout scaled
            geo.setCenterRadius(75.0f * scale);
            geo.setRingConfig(0, {245.0f * scale, 320.0f * scale,
                                  282.0f * scale, -1.0f});
            geo.setRingConfig(1, {185.0f * scale, 240.0f * scale,
                                  -1.0f, -1.0f});
            geo.setRingConfig(2, {135.0f * scale, 180.0f * scale,
                                  157.0f * scale, -1.0f});
            geo.setRingConfig(3, {80.0f * scale, 130.0f * scale,
                                  113.0f * scale, 97.0f * scale});
        }

        renderer_->invalid();
    }

private:
    RingRenderer* renderer_ = nullptr;  // Owned by CViewContainer

    CLASS_METHODS(RingDisplay, CViewContainer)
};

} // namespace Gradus
