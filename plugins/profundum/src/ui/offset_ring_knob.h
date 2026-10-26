#pragma once

// ==============================================================================
// Profundum Phase 12 - Profundum::OffsetRingKnob (FR-061; plan OQ-3 / P-5 ruling)
// ==============================================================================
// Spec:  specs/profundum-phase12-ui/spec.md  (FR-061; SC-028)
// Plan:  specs/profundum-phase12-ui/plan.md  (sections 1.6 "ArcKnob", 4.6.5, 5.5)
// Tasks: specs/profundum-phase12-ui/tasks.md (T016 "P-5 re-cut")
//
// A Krate::Plugins::ArcKnob (plugins/shared/src/ui/arc_knob.h:49) whose macro
// ring is ONE-SIDED: it draws from the knob value v to v + d, where d is the
// signed normalised offset the controller computes for the bound target ID
// (macroRingRange, plan 4.6.5). The shared ArcKnob's ring is symmetric
// ([v - |r|, v + |r|], arc_knob.h:338-345), which the P-5 ruling rejects, so the
// base modulation range is kept at 0 and this class strokes its own arc.
//
// setOffset() never moves the knob value: the ring is display only, and every
// mouse / wheel / keyboard interaction and gesture comes from ArcKnob unchanged.
//
// Registration is a ViewCreatorAdapter with base view "ArcKnob" (the
// SyncableArcKnob shape, syncable_arc_knob.h:88-99), so the uidesc's control-tag
// and every ArcKnob attribute (arc-color, mod-color, ...) apply. It registers in
// every linked TU that includes this header (controller.cpp, plan 4.13).
// ==============================================================================

#include "ui/arc_knob.h"

#include "vstgui/lib/crect.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/uiviewfactory.h"

#include <cstdint>
#include <optional>

namespace Profundum {

class OffsetRingKnob : public Krate::Plugins::ArcKnob {
public:
    /// The drawn normalised span, low <= high, both in [0, 1].
    struct Span {
        float low = 0.0f;
        float high = 0.0f;
    };

    OffsetRingKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag)
        : ArcKnob(size, listener, tag) {}

    OffsetRingKnob(const OffsetRingKnob& other) : ArcKnob(other), offset_(other.offset_) {}

    /// The signed normalised delta d of the bound target (0 = no ring).
    void setOffset(float signedNormDelta) noexcept;
    [[nodiscard]] float getOffset() const noexcept { return offset_; }

    /// [min(v, v + d), max(v, v + d)] clamped to [0, 1]; nullopt when d == 0.
    [[nodiscard]] std::optional<Span> ringSpan() const noexcept;

    /// The base ArcKnob draw with its modulation range at 0, then the one-sided arc.
    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS(OffsetRingKnob, Krate::Plugins::ArcKnob)

private:
    void drawOffsetArc(VSTGUI::CDrawContext* context) const;

    float offset_ = 0.0f;
};

// ==============================================================================
// ViewCreator Registration
// ==============================================================================

struct OffsetRingKnobCreator : VSTGUI::ViewCreatorAdapter {
    OffsetRingKnobCreator() { VSTGUI::UIViewFactory::registerViewCreator(*this); }

    VSTGUI::IdStringPtr getViewName() const override { return "OffsetRingKnob"; }

    /// ArcKnobCreator applies arc-color / mod-color / angles, then CControl's.
    VSTGUI::IdStringPtr getBaseViewName() const override { return "ArcKnob"; }

    VSTGUI::UTF8StringPtr getDisplayName() const override { return "Offset Ring Knob"; }

    VSTGUI::CView* create(const VSTGUI::UIAttributes& /*attributes*/,
                          const VSTGUI::IUIDescription* /*description*/) const override {
        return new OffsetRingKnob(VSTGUI::CRect(0, 0, 40, 40), nullptr, -1);
    }
};

// Inline variable (C++17) - safe for inclusion from multiple translation units.
// It only registers in a TU that is actually LINKED (arc_knob.h's rule).
inline OffsetRingKnobCreator gOffsetRingKnobCreator;

}  // namespace Profundum
