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

#include <cmath>

namespace Disrumpo {

AnimatedExpandController::AnimatedExpandController(
    VSTGUI::VST3Editor** editorPtr,
    Steinberg::Vst::Parameter* watchedParam,
    Steinberg::int32 containerTag,
    float expandedHeight,
    uint32_t animationDurationMs,
    Steinberg::int32 parentBandTag,
    int bandIndex,
    float expandedWidth,
    Steinberg::Vst::Parameter* bandCountParam)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , bandCountParam_(bandCountParam)
    , containerTag_(containerTag)
    , parentBandTag_(parentBandTag)
    , bandIndex_(bandIndex)
    , expandedHeight_(expandedHeight)
    , expandedWidth_(expandedWidth)
    , animationDurationMs_(animationDurationMs)
{
    if (watchedParam_) {
        watchedParam_->addRef();
        watchedParam_->addDependent(this);
        // Apply initial state immediately
        watchedParam_->deferUpdate();
    }
    if (bandCountParam_) {
        bandCountParam_->addRef();
        bandCountParam_->addDependent(this);
    }
}

AnimatedExpandController::~AnimatedExpandController() {
    deactivate();
    if (watchedParam_) {
        watchedParam_->release();
        watchedParam_ = nullptr;
    }
    if (bandCountParam_) {
        bandCountParam_->release();
        bandCountParam_ = nullptr;
    }
}

void AnimatedExpandController::deactivate() {
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (watchedParam_) {
            watchedParam_->removeDependent(this);
        }
        if (bandCountParam_) {
            bandCountParam_->removeDependent(this);
        }
    }
}

void AnimatedExpandController::setAnimationsEnabled(bool enabled) {
    animationsEnabled_ = enabled;
}

void AnimatedExpandController::update(
    Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) {
    if (!isActive_.load(std::memory_order_acquire)) {
        return;
    }

    VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
    if (message == IDependent::kChanged && watchedParam_ && editor) {
        float normalizedValue = static_cast<float>(watchedParam_->getNormalized());
        bool shouldExpand = (normalizedValue >= 0.5f);

        // D-170: a Band Count change shows or hides the panel at once, like the band
        // container itself: shown = expanded AND band visible
        if (bandCountParam_ != nullptr
            && Steinberg::FCast<Steinberg::Vst::Parameter>(changedUnknown) == bandCountParam_) {
            auto* container = findContainerByTag(containerTag_);
            if (!container) return;
            const bool shouldShow = shouldExpand && isBandVisible();
            if (shouldShow == container->isVisible()) return;
            if (shouldShow) {
                instantExpand(container);
            } else {
                instantCollapse(container);
            }
            return;
        }

        // FR-004: Guard against expanding a hidden band's detail panel.
        // If the parent band container is hidden (band count < band index),
        // skip animation entirely to avoid invisible layout changes.
        if (shouldExpand && !isBandVisible()) {
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

bool AnimatedExpandController::isBandVisible() {
    // From the parameter, not the band container: the container's own Band Count
    // observer may not have run yet for this change (D-170)
    if (bandCountParam_ != nullptr) {
        const int bandCount = static_cast<int>(std::round(bandCountParam_->getNormalized() * 3.0)) + 1;
        return bandIndex_ < bandCount;
    }
    return isParentBandVisible();
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

void AnimatedExpandController::positionContainerForBand(VSTGUI::CViewContainer* container) const {
    // Position expanded panel left-aligned to the triggering band, clamped to parent bounds.
    // Uses stored expandedWidth_ to avoid reading stale width from a mid-animation rect.
    constexpr float kBandSpacing = 240.0f;
    float targetX = bandIndex_ * kBandSpacing;

    if (auto* parent = container->getParentView()) {
        auto parentWidth = static_cast<float>(parent->getViewSize().getWidth());
        targetX = std::min(targetX, parentWidth - expandedWidth_);
    }
    targetX = std::max(0.0f, targetX);

    auto rect = container->getViewSize();
    auto height = rect.getHeight();
    rect.left = static_cast<VSTGUI::CCoord>(targetX);
    rect.right = rect.left + static_cast<VSTGUI::CCoord>(expandedWidth_);
    rect.setHeight(height);  // Preserve current height
    container->setViewSize(rect);
}

void AnimatedExpandController::animateExpand(VSTGUI::CViewContainer* container) {
    // Move to band position first, then animate height (original flow preserved)
    positionContainerForBand(container);
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
    positionContainerForBand(container);
    auto rect = container->getViewSize();
    rect.setHeight(static_cast<VSTGUI::CCoord>(expandedHeight_));
    container->setViewSize(rect);
    container->setVisible(true);
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
