// ==============================================================================
// AnimatedExpandController Contract
// ==============================================================================
// Extends the ContainerVisibilityController pattern to add smooth animation
// when expanding/collapsing band detail panels.
//
// FR-005: Transition <= 300ms
// FR-006: Mid-animation state change handled smoothly
// FR-028/FR-029: Disabled when reduced motion is active
// ==============================================================================

#pragma once

#include "base/source/fobject.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <atomic>

namespace Disrumpo {

class AnimatedExpandController : public Steinberg::FObject {
public:
    /// @param editorPtr Pointer to the active editor (double-pointer for lifecycle safety)
    /// @param watchedParam The boolean expand/collapse parameter to observe
    /// @param containerTag The UI tag of the container to animate
    /// @param expandedHeight The full height of the container when expanded
    /// @param animationDurationMs Duration of animation in milliseconds (0 = instant)
    AnimatedExpandController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        Steinberg::int32 containerTag,
        float expandedHeight,
        uint32_t animationDurationMs = 250);

    ~AnimatedExpandController() override;

    /// Stop observing parameter changes (call before destruction)
    void deactivate();

    /// Set whether animations are enabled (false = instant transitions)
    void setAnimationsEnabled(bool enabled);

    /// IDependent update callback (called on UI thread via UpdateHandler)
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

    OBJ_METHODS(AnimatedExpandController, FObject)

private:
    VSTGUI::CViewContainer* findContainerByTag(Steinberg::int32 tag);
    void animateExpand(VSTGUI::CViewContainer* container);
    void animateCollapse(VSTGUI::CViewContainer* container);
    void instantExpand(VSTGUI::CViewContainer* container);
    void instantCollapse(VSTGUI::CViewContainer* container);

    VSTGUI::VST3Editor** editorPtr_;
    Steinberg::Vst::Parameter* watchedParam_;
    Steinberg::int32 containerTag_;
    float expandedHeight_;
    uint32_t animationDurationMs_;
    bool animationsEnabled_ = true;
    std::atomic<bool> isActive_{true};
};

} // namespace Disrumpo
