#pragma once

// ==============================================================================
// Visibility Controllers: Thread-safe control visibility managers
// ==============================================================================
// Uses IDependent mechanism to receive parameter change notifications on UI thread.
// This is the CORRECT pattern for updating VSTGUI controls based on parameter values.
//
// CRITICAL Threading Rules:
// - setParamNormalized() can be called from ANY thread (automation, state load, etc.)
// - VSTGUI controls MUST only be manipulated on the UI thread
// - Solution: Use Parameter::addDependent() + deferred updates via UpdateHandler
//
// CRITICAL View Switching:
// - UIViewSwitchContainer DESTROYS and RECREATES controls when switching templates
// - DO NOT cache control pointers - they become invalid (dangling) after view switch
// - MUST look up control DYNAMICALLY on each update using control tag
// - Control tag remains constant, pointer changes on every view switch
// ==============================================================================

#include "base/source/fobject.h"
#include "public.sdk/source/vst/vstparameters.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <atomic>
#include <functional>
#include <initializer_list>
#include <vector>

// ==============================================================================
// VisibilityController: Show/hide individual controls by tag
// ==============================================================================
class VisibilityController : public Steinberg::FObject {
public:
    // editorPtr: Pointer to the controller's activeEditor_ member (NOT the editor itself!)
    // This allows us to always get the CURRENT editor, or nullptr if closed.
    VisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        std::initializer_list<Steinberg::int32> controlTags,
        float visibilityThreshold = 0.5f,
        bool showWhenBelow = true)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , controlTags_(controlTags)
    , visibilityThreshold_(visibilityThreshold)
    , showWhenBelow_(showWhenBelow)
    {
        if (watchedParam_) {
            watchedParam_->addRef();
            watchedParam_->addDependent(this);  // Register for parameter change notifications
            // Trigger initial update on UI thread
            watchedParam_->deferUpdate();
        }
    }

    ~VisibilityController() override {
        // Ensure we're deactivated (handles case of direct destruction without deactivate())
        deactivate();

        // Release our reference to the parameter
        if (watchedParam_) {
            watchedParam_->release();
            watchedParam_ = nullptr;
        }
    }

    // Deactivate this controller to safely handle editor close.
    // CRITICAL: This must be called BEFORE destruction to prevent use-after-free.
    // It removes us as a dependent BEFORE the object is destroyed, ensuring that
    // any queued deferred updates won't be delivered to a destroyed object.
    void deactivate() {
        // Use exchange to ensure we only do this once (idempotent)
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            // Was active, now deactivated - remove dependent to stop receiving updates
            // This must happen BEFORE destruction to prevent the race condition where
            // a deferred update fires during/after the destructor runs.
            if (watchedParam_) {
                watchedParam_->removeDependent(this);
            }
        }
    }

    // IDependent::update - called on UI thread via deferred update mechanism
    void PLUGIN_API update(Steinberg::FUnknown*  /*changedUnknown*/, Steinberg::int32 message) override {
        // CRITICAL: Check isActive FIRST before accessing ANY member.
        // This prevents use-after-free when deferred updates fire during/after destruction.
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        // Get current editor from controller's member - may be nullptr if editor closed
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && watchedParam_ && editor) {
            // Get current parameter value (normalized: 0.0 to 1.0)
            float normalizedValue = watchedParam_->getNormalized();

            // Determine visibility based on threshold and direction
            bool shouldBeVisible = showWhenBelow_ ?
                (normalizedValue < visibilityThreshold_) :
                (normalizedValue >= visibilityThreshold_);

            // Update visibility for all associated controls (label + slider + value display)
            for (Steinberg::int32 tag : controlTags_) {
                // CRITICAL: Look up ALL controls DYNAMICALLY on each update
                // UIViewSwitchContainer destroys/recreates controls on view switch,
                // so cached pointers become dangling references.
                // IMPORTANT: Multiple controls can have the same tag (e.g., slider + value display)
                auto controls = findAllControlsByTag(tag);

                for (auto* control : controls) {
                    // SAFE: This is called on UI thread via UpdateHandler::deferedUpdate()
                    control->setVisible(shouldBeVisible);

                    // Trigger redraw if needed
                    if (control->getFrame()) {
                        control->invalid();
                    }
                }
            }
        }
    }

    OBJ_METHODS(VisibilityController, FObject)

private:
    // Find ALL controls with given tag in current view hierarchy
    // Returns vector because multiple controls can share a tag (slider + value display)
    std::vector<VSTGUI::CControl*> findAllControlsByTag(Steinberg::int32 tag) {
        std::vector<VSTGUI::CControl*> results;
        // Get current editor - may be nullptr if closed
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (!editor) return results;
        auto* frame = editor->getFrame();
        if (!frame) return results;

        std::function<void(VSTGUI::CViewContainer*)> search;
        search = [tag, &results, &search](VSTGUI::CViewContainer* container) {
            if (!container) return;
            VSTGUI::ViewIterator it(container);
            while (*it) {
                if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                    if (control->getTag() == tag) {
                        results.push_back(control);
                    }
                }
                if (auto* childContainer = (*it)->asViewContainer()) {
                    search(childContainer);
                }
                ++it;
            }
        };
        search(frame);
        return results;
    }

    VSTGUI::VST3Editor** editorPtr_;  // Pointer to controller's activeEditor_ member
    Steinberg::Vst::Parameter* watchedParam_;
    std::vector<Steinberg::int32> controlTags_;
    float visibilityThreshold_;
    bool showWhenBelow_;
    std::atomic<bool> isActive_{true};  // Guards against use-after-free in deferred updates
};

// ==============================================================================
// ContainerVisibilityController: Hide/show entire CViewContainer by child tag
// ==============================================================================
// Finds a container by locating a child control with a known tag, then
// toggling the PARENT container's visibility based on a parameter value.
// Per VST-GUIDE Section 10: setVisible() on CViewContainer correctly hides
// the container and all its children in VSTGUI 4.x.
//
// Supports two modes:
// 1. Threshold mode (upperThreshold < 0): show when value >= threshold (or < if showWhenBelow)
// 2. Range mode (upperThreshold >= 0): show when lowerThreshold <= value < upperThreshold
// ==============================================================================
class ContainerVisibilityController : public Steinberg::FObject {
public:
    // Threshold mode: show when value >= threshold (or < if showWhenBelow)
    ContainerVisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        Steinberg::int32 childControlTag,  // Tag of a control INSIDE the container
        float visibilityThreshold = 0.5f,
        bool showWhenBelow = true)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , childControlTag_(childControlTag)
    , lowerThreshold_(visibilityThreshold)
    , upperThreshold_(-1.0f)  // Negative = threshold mode
    , showWhenBelow_(showWhenBelow)
    {
        if (watchedParam_) {
            watchedParam_->addRef();
            watchedParam_->addDependent(this);
            watchedParam_->deferUpdate();  // Trigger initial update
        }
    }

    // Range mode: show when lowerThreshold <= value < upperThreshold
    ContainerVisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        Steinberg::int32 childControlTag,
        float lowerThreshold,
        float upperThreshold)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , childControlTag_(childControlTag)
    , lowerThreshold_(lowerThreshold)
    , upperThreshold_(upperThreshold)
    , showWhenBelow_(false)  // Not used in range mode
    {
        if (watchedParam_) {
            watchedParam_->addRef();
            watchedParam_->addDependent(this);
            watchedParam_->deferUpdate();  // Trigger initial update
        }
    }

    ~ContainerVisibilityController() override {
        deactivate();
        if (watchedParam_) {
            watchedParam_->release();
            watchedParam_ = nullptr;
        }
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (watchedParam_) {
                watchedParam_->removeDependent(this);
            }
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown*  /*changedUnknown*/, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && watchedParam_ && editor) {
            float normalizedValue = watchedParam_->getNormalized();
            bool shouldBeVisible = false;

            if (upperThreshold_ >= 0.0f) {
                // Range mode: show when lowerThreshold <= value < upperThreshold
                shouldBeVisible = (normalizedValue >= lowerThreshold_ && normalizedValue < upperThreshold_);
            } else {
                // Threshold mode: show when value >= threshold (or < if showWhenBelow)
                shouldBeVisible = showWhenBelow_ ?
                    (normalizedValue < lowerThreshold_) :
                    (normalizedValue >= lowerThreshold_);
            }

            // Find the container by locating its child control (handles view switches)
            if (auto* container = findContainerByChildTag(childControlTag_)) {
                container->setVisible(shouldBeVisible);
                if (container->getFrame()) {
                    container->invalid();
                }
            }
        }
    }

    OBJ_METHODS(ContainerVisibilityController, FObject)

private:
    // Find a CViewContainer by locating a child control with the given tag
    // and returning its parent container
    VSTGUI::CViewContainer* findContainerByChildTag(Steinberg::int32 tag) {
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (!editor) return nullptr;
        auto* frame = editor->getFrame();
        if (!frame) return nullptr;

        VSTGUI::CViewContainer* result = nullptr;
        std::function<void(VSTGUI::CViewContainer*)> search;
        search = [tag, &result, &search](VSTGUI::CViewContainer* container) {
            if (!container || result) return;

            VSTGUI::ViewIterator it(container);
            while (*it && !result) {
                // Check if this view is a control with the target tag
                if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                    if (control->getTag() == tag) {
                        // Found the child - return its parent container
                        result = container;
                        return;
                    }
                }
                // Recursively search child containers
                if (auto* childContainer = (*it)->asViewContainer()) {
                    search(childContainer);
                }
                ++it;
            }
        };
        search(frame);
        return result;
    }

    VSTGUI::VST3Editor** editorPtr_;
    Steinberg::Vst::Parameter* watchedParam_;
    Steinberg::int32 childControlTag_;
    float lowerThreshold_;
    float upperThreshold_;  // Negative = threshold mode, >= 0 = range mode
    bool showWhenBelow_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// CompoundVisibilityController: Visibility based on TWO parameters (AND logic)
// ==============================================================================
// Shows controls when BOTH conditions are met:
// - param1 condition is true (based on threshold1 and showWhenBelow1)
// - param2 condition is true (based on threshold2 and showWhenBelow2)
//
// Use case: MultiTap Note Value visibility
// - Show when TimeMode is Synced (>= 0.5) AND Pattern is Mathematical (>= 14/19)
// ==============================================================================
class CompoundVisibilityController : public Steinberg::FObject {
public:
    CompoundVisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* param1,
        float threshold1,
        bool showWhenBelow1,
        Steinberg::Vst::Parameter* param2,
        float threshold2,
        bool showWhenBelow2,
        std::initializer_list<Steinberg::int32> controlTags)
    : editorPtr_(editorPtr)
    , param1_(param1)
    , param2_(param2)
    , controlTags_(controlTags)
    , threshold1_(threshold1)
    , threshold2_(threshold2)
    , showWhenBelow1_(showWhenBelow1)
    , showWhenBelow2_(showWhenBelow2)
    {
        if (param1_) {
            param1_->addRef();
            param1_->addDependent(this);
        }
        if (param2_) {
            param2_->addRef();
            param2_->addDependent(this);
        }
        // Trigger initial update
        if (param1_) param1_->deferUpdate();
    }

    ~CompoundVisibilityController() override {
        deactivate();
        if (param1_) {
            param1_->release();
            param1_ = nullptr;
        }
        if (param2_) {
            param2_->release();
            param2_ = nullptr;
        }
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (param1_) param1_->removeDependent(this);
            if (param2_) param2_->removeDependent(this);
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown*  /*changedUnknown*/, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && editor && param1_ && param2_) {
            float val1 = param1_->getNormalized();
            float val2 = param2_->getNormalized();

            // Check both conditions
            bool cond1 = showWhenBelow1_ ? (val1 < threshold1_) : (val1 >= threshold1_);
            bool cond2 = showWhenBelow2_ ? (val2 < threshold2_) : (val2 >= threshold2_);
            bool shouldBeVisible = cond1 && cond2;

            for (Steinberg::int32 tag : controlTags_) {
                auto controls = findAllControlsByTag(tag);
                for (auto* control : controls) {
                    control->setVisible(shouldBeVisible);
                    if (control->getFrame()) {
                        control->invalid();
                    }
                }
            }
        }
    }

    OBJ_METHODS(CompoundVisibilityController, FObject)

private:
    std::vector<VSTGUI::CControl*> findAllControlsByTag(Steinberg::int32 tag) {
        std::vector<VSTGUI::CControl*> results;
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (!editor) return results;
        auto* frame = editor->getFrame();
        if (!frame) return results;

        std::function<void(VSTGUI::CViewContainer*)> search;
        search = [tag, &results, &search](VSTGUI::CViewContainer* container) {
            if (!container) return;
            VSTGUI::ViewIterator it(container);
            while (*it) {
                if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                    if (control->getTag() == tag) {
                        results.push_back(control);
                    }
                }
                if (auto* childContainer = (*it)->asViewContainer()) {
                    search(childContainer);
                }
                ++it;
            }
        };
        search(frame);
        return results;
    }

    VSTGUI::VST3Editor** editorPtr_;
    Steinberg::Vst::Parameter* param1_;
    Steinberg::Vst::Parameter* param2_;
    std::vector<Steinberg::int32> controlTags_;
    float threshold1_;
    float threshold2_;
    bool showWhenBelow1_;
    bool showWhenBelow2_;
    std::atomic<bool> isActive_{true};
};

// Helper to safely deactivate a visibility controller (any of the three types)
inline void deactivateVisibilityController(Steinberg::IPtr<Steinberg::FObject>& controller) {
    if (controller) {
        if (auto* vc = dynamic_cast<VisibilityController*>(controller.get())) {
            vc->deactivate();
        } else if (auto* cvc = dynamic_cast<CompoundVisibilityController*>(controller.get())) {
            cvc->deactivate();
        } else if (auto* ccvc = dynamic_cast<ContainerVisibilityController*>(controller.get())) {
            ccvc->deactivate();
        }
    }
}
