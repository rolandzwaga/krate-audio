#pragma once

// ==============================================================================
// Visibility Controllers: Thread-safe IDependent-based parameter observers
// ==============================================================================
// Constitution Principle V: VSTGUI Development
// These controllers use the IDependent mechanism to receive parameter change
// notifications on the UI thread and update VSTGUI control visibility.
//
// CRITICAL Threading Rules:
// - setParamNormalized() can be called from ANY thread
// - VSTGUI controls MUST only be manipulated on the UI thread
// - Solution: Use Parameter::addDependent() + deferred updates via UpdateHandler
// ==============================================================================

#include "base/source/fobject.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ccontrol.h"

#include "controller/views/spectrum_display.h"

#include <atomic>
#include <functional>
#include <initializer_list>
#include <vector>

namespace Disrumpo {

// ==============================================================================
// VisibilityController: Show/hide individual controls by tag
// ==============================================================================

class VisibilityController : public Steinberg::FObject {
public:
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
            watchedParam_->addDependent(this);
            watchedParam_->deferUpdate();
        }
    }

    ~VisibilityController() override {
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

    void PLUGIN_API update(Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && watchedParam_ && editor) {
            float normalizedValue = watchedParam_->getNormalized();
            bool shouldBeVisible = showWhenBelow_ ?
                (normalizedValue < visibilityThreshold_) :
                (normalizedValue >= visibilityThreshold_);

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

    OBJ_METHODS(VisibilityController, FObject)

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
    Steinberg::Vst::Parameter* watchedParam_;
    std::vector<Steinberg::int32> controlTags_;
    float visibilityThreshold_;
    bool showWhenBelow_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// ContainerVisibilityController: Hide/show entire CViewContainer by child tag
// ==============================================================================

class ContainerVisibilityController : public Steinberg::FObject {
public:
    ContainerVisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        Steinberg::int32 containerTag,
        float threshold,
        bool showWhenBelow = true)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , containerTag_(containerTag)
    , threshold_(threshold)
    , showWhenBelow_(showWhenBelow)
    {
        if (watchedParam_) {
            watchedParam_->addRef();
            watchedParam_->addDependent(this);
            watchedParam_->deferUpdate();
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

    void PLUGIN_API update(Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && watchedParam_ && editor) {
            float normalizedValue = watchedParam_->getNormalized();
            bool shouldBeVisible = showWhenBelow_ ?
                (normalizedValue < threshold_) :
                (normalizedValue >= threshold_);

            auto* container = findContainerByTag(containerTag_);
            if (container) {
                container->setVisible(shouldBeVisible);
                if (container->getFrame()) {
                    container->invalid();
                }
            }
        }
    }

    OBJ_METHODS(ContainerVisibilityController, FObject)

private:
    VSTGUI::CViewContainer* findContainerByTag(Steinberg::int32 tag) {
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

    VSTGUI::VST3Editor** editorPtr_;
    Steinberg::Vst::Parameter* watchedParam_;
    Steinberg::int32 containerTag_;
    float threshold_;
    bool showWhenBelow_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// ModPanelToggleController: Shows/hides mod panel with editor resize
// ==============================================================================

class ModPanelToggleController : public Steinberg::FObject {
public:
    static constexpr VSTGUI::CCoord kModPanelHeight = 230.0;
    static constexpr VSTGUI::CCoord kBaseHeight = 600.0;

    ModPanelToggleController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        Steinberg::int32 containerTag)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , containerTag_(containerTag)
    {
        if (watchedParam_) {
            watchedParam_->addRef();
            watchedParam_->addDependent(this);
            lastState_ = (watchedParam_->getNormalized() >= 0.5);
            watchedParam_->deferUpdate();
        }
    }

    ~ModPanelToggleController() override {
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

    void PLUGIN_API update(Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) return;

        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message != IDependent::kChanged || !watchedParam_ || !editor) return;

        bool shouldShow = (watchedParam_->getNormalized() >= 0.5f);

        auto* container = findContainerByTag(containerTag_);
        if (container) {
            container->setVisible(shouldShow);
            if (container->getFrame()) {
                container->invalid();
            }
        }

        if (shouldShow != lastState_) {
            lastState_ = shouldShow;

            auto* frame = editor->getFrame();
            if (!frame) return;

            auto currentRect = frame->getViewSize();
            auto currentWidth = currentRect.getWidth();
            VSTGUI::CCoord newHeight = shouldShow
                ? kBaseHeight + kModPanelHeight
                : kBaseHeight;

            VSTGUI::CCoord minH = shouldShow ? (500.0 + kModPanelHeight) : 500.0;
            VSTGUI::CCoord maxH = shouldShow ? (840.0 + kModPanelHeight) : 840.0;
            editor->setEditorSizeConstrains(
                VSTGUI::CPoint(834, minH),
                VSTGUI::CPoint(1400, maxH));

            editor->requestResize(VSTGUI::CPoint(currentWidth, newHeight));
        }
    }

    OBJ_METHODS(ModPanelToggleController, FObject)

private:
    VSTGUI::CViewContainer* findContainerByTag(Steinberg::int32 tag) {
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

    VSTGUI::VST3Editor** editorPtr_;
    Steinberg::Vst::Parameter* watchedParam_;
    Steinberg::int32 containerTag_;
    bool lastState_ = false;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// BandCountDisplayController: Update SpectrumDisplay when band count changes
// ==============================================================================

class BandCountDisplayController : public Steinberg::FObject {
public:
    BandCountDisplayController(
        SpectrumDisplay** displayPtr,
        Steinberg::Vst::Parameter* bandCountParam);

    ~BandCountDisplayController() override;
    void deactivate();
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

    OBJ_METHODS(BandCountDisplayController, FObject)

private:
    SpectrumDisplay** displayPtr_;
    Steinberg::Vst::Parameter* bandCountParam_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// SpectrumModeController: Update SpectrumDisplay view mode when param changes
// ==============================================================================

class SpectrumModeController : public Steinberg::FObject {
public:
    SpectrumModeController(
        SpectrumDisplay** displayPtr,
        Steinberg::Vst::Parameter* modeParam);

    ~SpectrumModeController() override;
    void deactivate();
    void applyMode();
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

    OBJ_METHODS(SpectrumModeController, FObject)

private:
    SpectrumDisplay** displayPtr_;
    Steinberg::Vst::Parameter* modeParam_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// MorphSweepLinkController: Update morph position based on sweep frequency
// ==============================================================================

class MorphSweepLinkController : public Steinberg::FObject {
public:
    MorphSweepLinkController(
        Steinberg::Vst::EditControllerEx1* controller,
        Steinberg::Vst::Parameter* sweepFreqParam);

    ~MorphSweepLinkController() override;
    void deactivate();
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

    OBJ_METHODS(MorphSweepLinkController, FObject)

private:
    void updateBandMorphFromSweep(uint8_t band, float sweepNorm);

    Steinberg::Vst::EditControllerEx1* controller_;
    Steinberg::Vst::Parameter* sweepFreqParam_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// NodeSelectionController: Bidirectional sync between DisplayedType and nodes
// ==============================================================================

class NodeSelectionController : public Steinberg::FObject {
public:
    NodeSelectionController(
        Steinberg::Vst::EditControllerEx1* controller,
        uint8_t band,
        struct ShapeShadowStorage* shadowStorage = nullptr);

    ~NodeSelectionController() override;
    void deactivate();
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

    OBJ_METHODS(NodeSelectionController, FObject)

private:
    int getSelectedNode() const;
    bool isProxyParam(Steinberg::Vst::Parameter* param) const;
    void editParam(Steinberg::Vst::ParamID id, double normValue);
    void copySelectedNodeToDisplayedType();
    void copySelectedNodeToProxies();
    void copyProxyToSelectedNode(Steinberg::Vst::Parameter* changedProxy);
    void copyDisplayedTypeToSelectedNode();

    Steinberg::Vst::EditControllerEx1* controller_;
    uint8_t band_;
    Steinberg::Vst::Parameter* selectedNodeParam_ = nullptr;
    Steinberg::Vst::Parameter* nodeTypeParams_[4] = {nullptr, nullptr, nullptr, nullptr};
    Steinberg::Vst::Parameter* displayedTypeParam_ = nullptr;
    static constexpr int kNumProxyParams = 14;  // Drive, Mix, Tone, Bias, Shape0-9
    Steinberg::Vst::Parameter* proxyParams_[kNumProxyParams] = {};
    std::atomic<bool> isActive_{true};
    bool isUpdating_ = false;
    struct ShapeShadowStorage* shapeShadowPtr_ = nullptr;
};

// ==============================================================================
// SweepVisualizationController: Update sweep indicator from output parameter
// ==============================================================================

class SweepIndicator;

class SweepVisualizationController : public Steinberg::FObject {
public:
    SweepVisualizationController(
        Steinberg::Vst::EditControllerEx1* controller,
        SweepIndicator** sweepIndicator,
        SpectrumDisplay** spectrumDisplay);

    ~SweepVisualizationController() override;
    void deactivate();
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

    OBJ_METHODS(SweepVisualizationController, FObject)

private:
    void updateSpectrumBandIntensities();
    void removeDependent(Steinberg::Vst::Parameter* param);
    static void releaseParam(Steinberg::Vst::Parameter*& param);

    Steinberg::Vst::EditControllerEx1* controller_;
    SweepIndicator** sweepIndicatorPtr_;
    SpectrumDisplay** spectrumDisplayPtr_;
    Steinberg::Vst::Parameter* modFreqParam_ = nullptr;
    Steinberg::Vst::Parameter* sweepEnableParam_ = nullptr;
    Steinberg::Vst::Parameter* sweepWidthParam_ = nullptr;
    Steinberg::Vst::Parameter* sweepIntensityParam_ = nullptr;
    Steinberg::Vst::Parameter* sweepFalloffParam_ = nullptr;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// CrossoverDragBridge: Propagates SpectrumDisplay crossover drags to parameters
// ==============================================================================

class CrossoverDragBridge : public Steinberg::FObject, public SpectrumDisplayListener {
public:
    CrossoverDragBridge(Steinberg::Vst::EditControllerEx1* controller);

    void onCrossoverChanged(int dividerIndex, float frequencyHz) override;
    void onBandSelected(int bandIndex) override;
    void deactivate();

    OBJ_METHODS(CrossoverDragBridge, FObject)

private:
    Steinberg::Vst::EditControllerEx1* controller_ = nullptr;
};

} // namespace Disrumpo
