// ==============================================================================
// AnimatedExpandController Implementation
// ==============================================================================
// Extends the ContainerVisibilityController pattern to add smooth animation
// when expanding/collapsing band detail panels.
//
// FR-005: Transition <= 300ms
// FR-006: Mid-animation state change handled smoothly
// ==============================================================================

#include "controller/animated_expand_controller.h"
#include "vstgui/lib/animation/animations.h"
#include "vstgui/lib/animation/animator.h"
#include "vstgui/lib/animation/timingfunctions.h"
#include "vstgui/lib/controls/ccontrol.h"

namespace Disrumpo {

AnimatedExpandController::AnimatedExpandController(
    VSTGUI::VST3Editor** editorPtr,
    Steinberg::Vst::Parameter* watchedParam,
    Steinberg::int32 containerTag,
    float expandedHeight,
    uint32_t animationDurationMs,
    Steinberg::int32 parentBandTag)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , containerTag_(containerTag)
    , parentBandTag_(parentBandTag)
    , expandedHeight_(expandedHeight)
    , animationDurationMs_(animationDurationMs)
{
    if (watchedParam_) {
        watchedParam_->addRef();
        watchedParam_->addDependent(this);
        // Apply initial state immediately
        watchedParam_->deferUpdate();
    }
}

AnimatedExpandController::~AnimatedExpandController() {
    deactivate();
    if (watchedParam_) {
        watchedParam_->release();
        watchedParam_ = nullptr;
    }
}

void AnimatedExpandController::deactivate() {
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (watchedParam_) {
            watchedParam_->removeDependent(this);
        }
    }
}

void AnimatedExpandController::setAnimationsEnabled(bool enabled) {
    animationsEnabled_ = enabled;
}

void AnimatedExpandController::update(
    Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) {
    if (!isActive_.load(std::memory_order_acquire)) {
        return;
    }

    VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
    if (message == IDependent::kChanged && watchedParam_ && editor) {
        float normalizedValue = static_cast<float>(watchedParam_->getNormalized());
        bool shouldExpand = (normalizedValue >= 0.5f);

        // FR-004: Guard against expanding a hidden band's detail panel.
        // If the parent band container is hidden (band count < band index),
        // skip animation entirely to avoid invisible layout changes.
        if (shouldExpand && !isParentBandVisible()) {
            return;
        }

        auto* container = findContainerByTag(containerTag_);
        if (!container) return;

        if (shouldExpand) {
            if (animationsEnabled_ && animationDurationMs_ > 0) {
                animateExpand(container);
            } else {
                instantExpand(container);
            }
        } else {
            if (animationsEnabled_ && animationDurationMs_ > 0) {
                animateCollapse(container);
            } else {
                instantCollapse(container);
            }
        }
    }
}

VSTGUI::CViewContainer* AnimatedExpandController::findContainerByTag(Steinberg::int32 tag) {
    VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
    if (!editor) return nullptr;
    auto* frame = editor->getFrame();
    if (!frame) return nullptr;

    std::function<VSTGUI::CViewContainer*(VSTGUI::CViewContainer*)> search;
    search = [tag, &search](VSTGUI::CViewContainer* container) -> VSTGUI::CViewContainer* {
        if (!container) return nullptr;
        VSTGUI::ViewIterator it(container);
        while (*it) {
            if (auto* ctrl = dynamic_cast<VSTGUI::CControl*>(*it)) {
                if (ctrl->getTag() == tag) {
                    return container;
                }
            }
            if (auto* childContainer = (*it)->asViewContainer()) {
                if (auto* found = search(childContainer)) {
                    return found;
                }
            }
            ++it;
        }
        return nullptr;
    };
    return search(frame);
}

bool AnimatedExpandController::isParentBandVisible() {
    // If no parent band tag is set, assume the parent is always visible
    if (parentBandTag_ < 0) {
        return true;
    }

    auto* parentContainer = findContainerByTag(parentBandTag_);
    if (!parentContainer) {
        return false;
    }

    return parentContainer->isVisible();
}

void AnimatedExpandController::animateExpand(VSTGUI::CViewContainer* container) {
    // Make visible first so animation is seen
    container->setVisible(true);

    VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
    if (!editor) return;
    auto* frame = editor->getFrame();
    if (!frame) return;

    auto* animator = frame->getAnimator();
    if (!animator) {
        instantExpand(container);
        return;
    }

    // Get current rect and compute target
    auto currentRect = container->getViewSize();
    auto targetRect = currentRect;
    targetRect.setHeight(static_cast<VSTGUI::CCoord>(expandedHeight_));

    // FR-006: Adding animation with same view+name cancels existing
    // This is built-in VSTGUI behavior - handles mid-animation reversal
    animator->addAnimation(
        container,
        "expandCollapse",
        new VSTGUI::Animation::ViewSizeAnimation(targetRect, true),
        new VSTGUI::Animation::CubicBezierTimingFunction(
            VSTGUI::Animation::CubicBezierTimingFunction::easyInOut(animationDurationMs_))
    );
}

void AnimatedExpandController::animateCollapse(VSTGUI::CViewContainer* container) {
    VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
    if (!editor) return;
    auto* frame = editor->getFrame();
    if (!frame) return;

    auto* animator = frame->getAnimator();
    if (!animator) {
        instantCollapse(container);
        return;
    }

    auto currentRect = container->getViewSize();
    auto targetRect = currentRect;
    targetRect.setHeight(0);

    // On completion, hide the container
    auto doneFunc = [container](VSTGUI::CView*, const VSTGUI::IdStringPtr, VSTGUI::Animation::IAnimationTarget*) {
        container->setVisible(false);
    };

    // FR-006: Same name cancels existing animation (mid-animation reversal)
    animator->addAnimation(
        container,
        "expandCollapse",
        new VSTGUI::Animation::ViewSizeAnimation(targetRect, true),
        new VSTGUI::Animation::CubicBezierTimingFunction(
            VSTGUI::Animation::CubicBezierTimingFunction::easyInOut(animationDurationMs_)),
        doneFunc
    );
}

void AnimatedExpandController::instantExpand(VSTGUI::CViewContainer* container) const {
    container->setVisible(true);
    auto rect = container->getViewSize();
    rect.setHeight(static_cast<VSTGUI::CCoord>(expandedHeight_));
    container->setViewSize(rect);
    if (container->getFrame()) {
        container->invalid();
    }
}

void AnimatedExpandController::instantCollapse(VSTGUI::CViewContainer* container) {
    container->setVisible(false);
    if (container->getFrame()) {
        container->invalid();
    }
}

} // namespace Disrumpo
