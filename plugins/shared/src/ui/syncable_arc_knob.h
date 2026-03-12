#pragma once

// ==============================================================================
// SyncableArcKnob - ArcKnob with right-click sync toggle
// ==============================================================================
// Subclass of ArcKnob that adds a right-click toggle for tempo sync.
// When right-clicked, it toggles an associated bool parameter (sync on/off)
// and swaps its own tag between free-rate and note-value params.
//
// Registered as "SyncableArcKnob" via VSTGUI ViewCreator system.
// ==============================================================================

#include "arc_knob.h"

#include "public.sdk/source/vst/vstguieditor.h"

namespace Krate::Plugins {

class SyncableArcKnob : public ArcKnob {
public:
    SyncableArcKnob(const VSTGUI::CRect& size,
                     VSTGUI::IControlListener* listener, int32_t tag)
        : ArcKnob(size, listener, tag) {}

    SyncableArcKnob(const SyncableArcKnob& other)
        : ArcKnob(other)
        , syncParamId_(other.syncParamId_)
        , freeRateTag_(other.freeRateTag_)
        , noteValueTag_(other.noteValueTag_) {}

    /// Set the parameter ID of the sync toggle (bool param).
    void setSyncParamId(int32_t id) { syncParamId_ = id; }
    [[nodiscard]] int32_t getSyncParamId() const { return syncParamId_; }

    /// Set the two tags this knob alternates between.
    void setAlternateTags(int32_t freeTag, int32_t syncedTag) {
        freeRateTag_ = freeTag;
        noteValueTag_ = syncedTag;
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        // Right-click: toggle sync parameter and swap tag
        if (buttons.isRightButton() && syncParamId_ >= 0)
        {
            auto* controller = getEditController();
            if (controller)
            {
                auto paramId = static_cast<Steinberg::Vst::ParamID>(syncParamId_);
                auto currentVal = controller->getParamNormalized(paramId);
                bool wasSynced = currentVal > 0.5;
                auto newVal = wasSynced ? 0.0 : 1.0;

                controller->beginEdit(paramId);
                controller->performEdit(paramId, newVal);
                controller->setParamNormalized(paramId, newVal);
                controller->endEdit(paramId);

                // Swap our own tag to show the correct parameter
                bool nowSynced = !wasSynced;
                int32_t newTag = nowSynced ? noteValueTag_ : freeRateTag_;
                if (newTag >= 0 && newTag != getTag())
                {
                    setTag(newTag);
                    invalid();
                }
            }
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }

        return ArcKnob::onMouseDown(where, buttons);
    }

    CLASS_METHODS(SyncableArcKnob, ArcKnob)

private:
    int32_t syncParamId_ = -1;
    int32_t freeRateTag_ = -1;
    int32_t noteValueTag_ = -1;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct SyncableArcKnobCreator : VSTGUI::ViewCreatorAdapter {
    SyncableArcKnobCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "SyncableArcKnob";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return "ArcKnob";
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Syncable Arc Knob";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new SyncableArcKnob(VSTGUI::CRect(0, 0, 40, 40), nullptr, -1);
    }

    bool apply(VSTGUI::CView* /*view*/,
               const VSTGUI::UIAttributes& /*attributes*/,
               const VSTGUI::IUIDescription* /*description*/) const override {
        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& /*attributeNames*/) const override {
        return true;
    }

    AttrType getAttributeType(
        const std::string& /*attributeName*/) const override {
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* /*view*/,
                           const std::string& /*attributeName*/,
                           std::string& /*stringValue*/,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        return false;
    }
};

inline SyncableArcKnobCreator gSyncableArcKnobCreator;

} // namespace Krate::Plugins
