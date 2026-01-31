// ==============================================================================
// Edit Controller Implementation
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation
// Constitution Principle V: VSTGUI Development
// ==============================================================================

#include "controller.h"
#include "controller/sub_controllers.h"
#include "plugin_ids.h"
#include "version.h"
#include "dsp/band_state.h"
#include "preset/disrumpo_preset_config.h"
#include "controller/views/spectrum_display.h"
#include "controller/views/morph_pad.h"
#include "controller/views/dynamic_node_selector.h"
#include "controller/views/node_editor_border.h"
#include "controller/views/sweep_indicator.h"
#include "controller/views/custom_curve_editor.h"
#include "controller/morph_link.h"
#include "controller/animated_expand_controller.h"
#include "controller/keyboard_shortcut_handler.h"
#include "dsp/sweep_morph_link.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "platform/accessibility_helper.h"
#include "midi/midi_cc_manager.h"

#include "base/source/fstreamer.h"
#include "base/source/fobject.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/iuidescription.h"

#include <cmath>
#include <cstring>
#include <atomic>
#include <functional>
#include <vector>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Disrumpo {

// ==============================================================================
// VisibilityController: Thread-safe control visibility manager
// ==============================================================================
// Uses IDependent mechanism to receive parameter change notifications on UI thread.
// This is the CORRECT pattern for updating VSTGUI controls based on parameter values.
//
// CRITICAL Threading Rules:
// - setParamNormalized() can be called from ANY thread (automation, state load, etc.)
// - VSTGUI controls MUST only be manipulated on the UI thread
// - Solution: Use Parameter::addDependent() + deferred updates via UpdateHandler
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
// When the mod panel visibility parameter changes, this controller:
// 1. Shows or hides the mod panel container (same as ContainerVisibilityController)
// 2. Resizes the editor window to accommodate or remove the mod panel height
// 3. Updates the editor size constraints dynamically
// ==============================================================================
class ModPanelToggleController : public Steinberg::FObject {
public:
    static constexpr VSTGUI::CCoord kModPanelHeight = 200.0;

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
            // Initialize lastState_ from current param value so the initial
            // deferUpdate sets visibility but does NOT trigger a resize
            // (the window is already the correct size from state restore)
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

        // Always set container visibility (handles initial setup via deferUpdate)
        auto* container = findContainerByTag(containerTag_);
        if (container) {
            container->setVisible(shouldShow);
            if (container->getFrame()) {
                container->invalid();
            }
        }

        // Only resize when state actually changes (not on initial deferUpdate)
        if (shouldShow != lastState_) {
            lastState_ = shouldShow;

            auto* frame = editor->getFrame();
            if (!frame) return;

            auto currentRect = frame->getViewSize();
            auto currentWidth = currentRect.getWidth();
            auto currentHeight = currentRect.getHeight();
            VSTGUI::CCoord newHeight = shouldShow
                ? currentHeight + kModPanelHeight
                : currentHeight - kModPanelHeight;

            // Update size constraints to allow the new height range
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
// Watches the band count parameter and calls setNumBands() on the
// SpectrumDisplay so crossover lines update in real-time.
// ==============================================================================
class BandCountDisplayController : public Steinberg::FObject {
public:
    BandCountDisplayController(
        SpectrumDisplay** displayPtr,
        Steinberg::Vst::Parameter* bandCountParam)
    : displayPtr_(displayPtr)
    , bandCountParam_(bandCountParam)
    {
        if (bandCountParam_) {
            bandCountParam_->addRef();
            bandCountParam_->addDependent(this);
        }
    }

    ~BandCountDisplayController() override {
        deactivate();
        if (bandCountParam_) {
            bandCountParam_->release();
            bandCountParam_ = nullptr;
        }
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (bandCountParam_) {
                bandCountParam_->removeDependent(this);
            }
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown* /*changedUnknown*/,
                           Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) return;

        if (message == IDependent::kChanged && bandCountParam_
            && displayPtr_ && *displayPtr_) {
            float normalized = static_cast<float>(bandCountParam_->getNormalized());
            int bandCount = static_cast<int>(std::round(normalized * 3.0f)) + 1;
            (*displayPtr_)->setNumBands(bandCount);
        }
    }

    OBJ_METHODS(BandCountDisplayController, FObject)

private:
    SpectrumDisplay** displayPtr_;
    Steinberg::Vst::Parameter* bandCountParam_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// MorphSweepLinkController: Update morph position based on sweep frequency
// ==============================================================================
// T159-T161: Listens to sweep frequency changes and updates morph X/Y positions
// based on the Band*MorphXLink and Band*MorphYLink parameter values.
// ==============================================================================
class MorphSweepLinkController : public Steinberg::FObject {
public:
    MorphSweepLinkController(
        Steinberg::Vst::EditControllerEx1* controller,
        Steinberg::Vst::Parameter* sweepFreqParam)
    : controller_(controller)
    , sweepFreqParam_(sweepFreqParam)
    {
        if (sweepFreqParam_) {
            sweepFreqParam_->addRef();
            sweepFreqParam_->addDependent(this);
        }
    }

    ~MorphSweepLinkController() override {
        deactivate();
        if (sweepFreqParam_) {
            sweepFreqParam_->release();
            sweepFreqParam_ = nullptr;
        }
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (sweepFreqParam_) {
                sweepFreqParam_->removeDependent(this);
            }
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        if (message == IDependent::kChanged && sweepFreqParam_ && controller_) {
            // Get sweep frequency as normalized position (log scale already handled by RangeParameter)
            float sweepNorm = static_cast<float>(sweepFreqParam_->getNormalized());

            // Update morph position for each band based on its link mode settings
            for (int band = 0; band < 8; ++band) {
                updateBandMorphFromSweep(static_cast<uint8_t>(band), sweepNorm);
            }
        }
    }

    OBJ_METHODS(MorphSweepLinkController, FObject)

private:
    void updateBandMorphFromSweep(uint8_t band, float sweepNorm) {
        // Get the link mode parameters for this band
        auto* morphXLinkParam = controller_->getParameterObject(
            makeBandParamId(band, BandParamType::kBandMorphXLink));
        auto* morphYLinkParam = controller_->getParameterObject(
            makeBandParamId(band, BandParamType::kBandMorphYLink));

        if (!morphXLinkParam || !morphYLinkParam) {
            return;
        }

        // Get current link modes (discrete values 0-6 for 7 modes)
        int xLinkIndex = static_cast<int>(std::round(morphXLinkParam->getNormalized() * 6.0));
        int yLinkIndex = static_cast<int>(std::round(morphYLinkParam->getNormalized() * 6.0));

        MorphLinkMode xLinkMode = static_cast<MorphLinkMode>(std::clamp(xLinkIndex, 0, 6));
        MorphLinkMode yLinkMode = static_cast<MorphLinkMode>(std::clamp(yLinkIndex, 0, 6));

        // Skip if both are None (no link)
        if (xLinkMode == MorphLinkMode::None && yLinkMode == MorphLinkMode::None) {
            return;
        }

        // Get current manual morph positions
        auto* morphXParam = controller_->getParameterObject(
            makeBandParamId(band, BandParamType::kBandMorphX));
        auto* morphYParam = controller_->getParameterObject(
            makeBandParamId(band, BandParamType::kBandMorphY));

        if (!morphXParam || !morphYParam) {
            return;
        }

        float currentX = static_cast<float>(morphXParam->getNormalized());
        float currentY = static_cast<float>(morphYParam->getNormalized());

        // Apply link modes to compute new positions
        float newX = applyMorphLinkMode(xLinkMode, sweepNorm, currentX);
        float newY = applyMorphLinkMode(yLinkMode, sweepNorm, currentY);

        // Update parameters if they changed (only for linked modes)
        if (xLinkMode != MorphLinkMode::None && std::abs(newX - currentX) > 0.001f) {
            controller_->setParamNormalized(
                makeBandParamId(band, BandParamType::kBandMorphX), newX);
            controller_->performEdit(
                makeBandParamId(band, BandParamType::kBandMorphX), newX);
        }

        if (yLinkMode != MorphLinkMode::None && std::abs(newY - currentY) > 0.001f) {
            controller_->setParamNormalized(
                makeBandParamId(band, BandParamType::kBandMorphY), newY);
            controller_->performEdit(
                makeBandParamId(band, BandParamType::kBandMorphY), newY);
        }
    }

    Steinberg::Vst::EditControllerEx1* controller_;
    Steinberg::Vst::Parameter* sweepFreqParam_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// NodeSelectionController: Bidirectional sync between DisplayedType and node types
// ==============================================================================
// US7 FR-024/FR-025: Maintains bidirectional sync between the DisplayedType proxy
// parameter (bound to the type dropdown) and the selected node's actual type.
//
// When user selects a different node (A/B/C/D):
//   → Copy that node's type to DisplayedType (for UIViewSwitchContainer)
// When user changes the type dropdown (DisplayedType):
//   → Copy DisplayedType to the selected node's actual type parameter
// ==============================================================================
class NodeSelectionController : public Steinberg::FObject {
public:
    NodeSelectionController(
        Steinberg::Vst::EditControllerEx1* controller,
        uint8_t band)
    : controller_(controller)
    , band_(band)
    , selectedNodeParam_(controller->getParameterObject(
          makeBandParamId(band, BandParamType::kBandSelectedNode)))
    {
        // Watch the SelectedNode parameter
        if (selectedNodeParam_) {
            selectedNodeParam_->addRef();
            selectedNodeParam_->addDependent(this);
        }

        // Watch all 4 node type parameters so we update when types change
        for (int n = 0; n < 4; ++n) {
            auto paramId = makeNodeParamId(band, static_cast<uint8_t>(n), NodeParamType::kNodeType);
            nodeTypeParams_[n] = controller_->getParameterObject(paramId);

            if (nodeTypeParams_[n]) {
                nodeTypeParams_[n]->addRef();
                nodeTypeParams_[n]->addDependent(this);
            }
        }

        // Watch DisplayedType for bidirectional sync (when user changes dropdown)
        displayedTypeParam_ = controller_->getParameterObject(
            makeBandParamId(band, BandParamType::kBandDisplayedType));
        if (displayedTypeParam_) {
            displayedTypeParam_->addRef();
            displayedTypeParam_->addDependent(this);
        }

        // Trigger initial sync
        if (selectedNodeParam_) {
            selectedNodeParam_->deferUpdate();
        }
    }

    ~NodeSelectionController() override {
        deactivate();
        if (selectedNodeParam_) {
            selectedNodeParam_->release();
            selectedNodeParam_ = nullptr;
        }
        for (auto*& param : nodeTypeParams_) {
            if (param) {
                param->release();
                param = nullptr;
            }
        }
        if (displayedTypeParam_) {
            displayedTypeParam_->release();
            displayedTypeParam_ = nullptr;
        }
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (selectedNodeParam_) {
                selectedNodeParam_->removeDependent(this);
            }
            for (auto* param : nodeTypeParams_) {
                if (param) {
                    param->removeDependent(this);
                }
            }
            if (displayedTypeParam_) {
                displayedTypeParam_->removeDependent(this);
            }
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }
        if (message != IDependent::kChanged || !controller_) {
            return;
        }
        if (isUpdating_) {
            return;  // Prevent re-entrancy during bidirectional sync
        }

        isUpdating_ = true;

        // Determine which parameter changed
        auto* changedParam = Steinberg::FCast<Steinberg::Vst::Parameter>(changedUnknown);

        if (changedParam == displayedTypeParam_) {
            // User changed the type dropdown → copy to selected node's type
            copyDisplayedTypeToSelectedNode();
        } else {
            // Selected node or node type changed → copy to DisplayedType
            copySelectedNodeToDisplayedType();
        }

        isUpdating_ = false;
    }

    OBJ_METHODS(NodeSelectionController, FObject)

private:
    void copySelectedNodeToDisplayedType() {
        if (!selectedNodeParam_ || !displayedTypeParam_) return;

        // Get selected node index (0-3)
        int selectedNode = static_cast<int>(
            selectedNodeParam_->getNormalized() * 3.0 + 0.5);
        selectedNode = std::clamp(selectedNode, 0, 3);

        // Get that node's type
        auto* nodeTypeParam = nodeTypeParams_[selectedNode];
        if (!nodeTypeParam) return;

        float nodeTypeNorm = static_cast<float>(nodeTypeParam->getNormalized());

        // Get current displayed type value
        float currentDisplayedType = static_cast<float>(displayedTypeParam_->getNormalized());

        // Only update if different to avoid unnecessary notifications
        if (std::abs(currentDisplayedType - nodeTypeNorm) < 0.001f) {
            return;  // Values are the same, no update needed
        }

        // Copy to DisplayedType parameter
        // Must use performEdit() to trigger VSTGUI's ParameterChangeListener
        auto displayedTypeId = makeBandParamId(band_, BandParamType::kBandDisplayedType);
        controller_->beginEdit(displayedTypeId);
        controller_->setParamNormalized(displayedTypeId, nodeTypeNorm);
        controller_->performEdit(displayedTypeId, nodeTypeNorm);
        controller_->endEdit(displayedTypeId);
    }

    void copyDisplayedTypeToSelectedNode() {
        if (!selectedNodeParam_ || !displayedTypeParam_) return;

        // Get selected node index (0-3)
        int selectedNode = static_cast<int>(
            selectedNodeParam_->getNormalized() * 3.0 + 0.5);
        selectedNode = std::clamp(selectedNode, 0, 3);

        // Get displayed type value
        float displayedTypeNorm = static_cast<float>(displayedTypeParam_->getNormalized());

        // Get selected node's type parameter
        auto* nodeTypeParam = nodeTypeParams_[selectedNode];
        if (!nodeTypeParam) return;

        // Only update if different to avoid unnecessary notifications
        float currentNodeType = static_cast<float>(nodeTypeParam->getNormalized());
        if (std::abs(currentNodeType - displayedTypeNorm) < 0.001f) {
            return;  // Values are the same, no update needed
        }

        // Copy DisplayedType to selected node's type
        auto nodeTypeId = makeNodeParamId(band_, static_cast<uint8_t>(selectedNode),
                                           NodeParamType::kNodeType);
        controller_->beginEdit(nodeTypeId);
        controller_->setParamNormalized(nodeTypeId, displayedTypeNorm);
        controller_->performEdit(nodeTypeId, displayedTypeNorm);
        controller_->endEdit(nodeTypeId);
    }

    Steinberg::Vst::EditControllerEx1* controller_;
    uint8_t band_;
    Steinberg::Vst::Parameter* selectedNodeParam_ = nullptr;
    Steinberg::Vst::Parameter* nodeTypeParams_[4] = {nullptr, nullptr, nullptr, nullptr};
    Steinberg::Vst::Parameter* displayedTypeParam_ = nullptr;
    std::atomic<bool> isActive_{true};
    bool isUpdating_ = false;  // Re-entrancy guard for bidirectional sync
};

// ==============================================================================
// SweepVisualizationController: Update sweep indicator from output parameter
// ==============================================================================
// FR-047, FR-049: Watches the modulated frequency output parameter and updates
// the SweepIndicator and SpectrumDisplay with current sweep state.
// ==============================================================================
class SweepVisualizationController : public Steinberg::FObject {
public:
    SweepVisualizationController(
        Steinberg::Vst::EditControllerEx1* controller,
        SweepIndicator** sweepIndicator,
        SpectrumDisplay** spectrumDisplay)
    : controller_(controller)
    , sweepIndicatorPtr_(sweepIndicator)
    , spectrumDisplayPtr_(spectrumDisplay)
    , modFreqParam_(controller->getParameterObject(kSweepModulatedFrequencyOutputId))
    {
        // Watch the modulated frequency output parameter
        if (modFreqParam_) {
            modFreqParam_->addRef();
            modFreqParam_->addDependent(this);
        }

        // Watch sweep enable parameter
        sweepEnableParam_ = controller_->getParameterObject(
            makeSweepParamId(SweepParamType::kSweepEnable));
        if (sweepEnableParam_) {
            sweepEnableParam_->addRef();
            sweepEnableParam_->addDependent(this);
        }

        // Watch width parameter
        sweepWidthParam_ = controller_->getParameterObject(
            makeSweepParamId(SweepParamType::kSweepWidth));
        if (sweepWidthParam_) {
            sweepWidthParam_->addRef();
            sweepWidthParam_->addDependent(this);
        }

        // Watch intensity parameter
        sweepIntensityParam_ = controller_->getParameterObject(
            makeSweepParamId(SweepParamType::kSweepIntensity));
        if (sweepIntensityParam_) {
            sweepIntensityParam_->addRef();
            sweepIntensityParam_->addDependent(this);
        }

        // Watch falloff parameter
        sweepFalloffParam_ = controller_->getParameterObject(
            makeSweepParamId(SweepParamType::kSweepFalloff));
        if (sweepFalloffParam_) {
            sweepFalloffParam_->addRef();
            sweepFalloffParam_->addDependent(this);
        }
    }

    ~SweepVisualizationController() override {
        deactivate();
        releaseParam(modFreqParam_);
        releaseParam(sweepEnableParam_);
        releaseParam(sweepWidthParam_);
        releaseParam(sweepIntensityParam_);
        releaseParam(sweepFalloffParam_);
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            removeDependent(modFreqParam_);
            removeDependent(sweepEnableParam_);
            removeDependent(sweepWidthParam_);
            removeDependent(sweepIntensityParam_);
            removeDependent(sweepFalloffParam_);
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }
        if (message != IDependent::kChanged || !controller_) {
            return;
        }

        SweepIndicator* indicator = sweepIndicatorPtr_ ? *sweepIndicatorPtr_ : nullptr;
        if (!indicator) {
            return;
        }

        // Update enable state
        if (sweepEnableParam_) {
            indicator->setEnabled(sweepEnableParam_->getNormalized() >= 0.5);
        }

        // Update center frequency from modulated output parameter
        if (modFreqParam_) {
            float normFreq = static_cast<float>(modFreqParam_->getNormalized());
            float freqHz = denormalizeSweepFrequency(normFreq);
            indicator->setCenterFrequency(freqHz);
        }

        // Update width
        if (sweepWidthParam_) {
            constexpr float kMinWidth = 0.5f;
            constexpr float kMaxWidth = 4.0f;
            float widthNorm = static_cast<float>(sweepWidthParam_->getNormalized());
            float widthOct = kMinWidth + widthNorm * (kMaxWidth - kMinWidth);
            indicator->setWidth(widthOct);
        }

        // Update intensity
        if (sweepIntensityParam_) {
            float intensityNorm = static_cast<float>(sweepIntensityParam_->getNormalized());
            indicator->setIntensity(intensityNorm * 2.0f);
        }

        // Update falloff mode
        if (sweepFalloffParam_) {
            indicator->setFalloffMode(
                sweepFalloffParam_->getNormalized() >= 0.5
                    ? SweepFalloff::Smooth
                    : SweepFalloff::Sharp);
        }

        // Update spectrum display band intensities (FR-050)
        updateSpectrumBandIntensities();
    }

    OBJ_METHODS(SweepVisualizationController, FObject)

private:
    void updateSpectrumBandIntensities() {
        SpectrumDisplay* display = spectrumDisplayPtr_ ? *spectrumDisplayPtr_ : nullptr;
        if (!display || !modFreqParam_ || !sweepWidthParam_ || !sweepIntensityParam_ || !sweepEnableParam_) {
            return;
        }

        bool enabled = sweepEnableParam_->getNormalized() >= 0.5;
        if (!enabled) {
            display->setSweepEnabled(false);
            return;
        }

        display->setSweepEnabled(true);

        // Get current sweep parameters
        float normFreq = static_cast<float>(modFreqParam_->getNormalized());
        float sweepCenterHz = denormalizeSweepFrequency(normFreq);

        constexpr float kMinWidth = 0.5f;
        constexpr float kMaxWidth = 4.0f;
        float widthNorm = static_cast<float>(sweepWidthParam_->getNormalized());
        float widthOctaves = kMinWidth + widthNorm * (kMaxWidth - kMinWidth);

        float intensityNorm = static_cast<float>(sweepIntensityParam_->getNormalized());
        float intensity = intensityNorm * 2.0f;

        bool smoothFalloff = (sweepFalloffParam_ != nullptr) && sweepFalloffParam_->getNormalized() >= 0.5;

        // Compute per-band intensities
        int numBands = display->getNumBands();
        std::array<float, 4> intensities{};

        for (int i = 0; i < numBands && i < 4; ++i) {
            // Get band center frequency from crossover positions
            float lowFreq = (i == 0) ? 20.0f : display->getCrossoverFrequency(i - 1);
            float highFreq = (i == numBands - 1) ? 20000.0f : display->getCrossoverFrequency(i);
            // Geometric mean for band center
            float bandCenterHz = std::sqrt(lowFreq * highFreq);

            if (smoothFalloff) {
                intensities[static_cast<size_t>(i)] = calculateGaussianIntensity(
                    bandCenterHz, sweepCenterHz, widthOctaves, intensity);
            } else {
                intensities[static_cast<size_t>(i)] = calculateLinearFalloff(
                    bandCenterHz, sweepCenterHz, widthOctaves, intensity);
            }
        }

        display->setSweepBandIntensities(intensities, numBands);
    }

    void removeDependent(Steinberg::Vst::Parameter* param) {
        if (param) {
            param->removeDependent(this);
        }
    }

    static void releaseParam(Steinberg::Vst::Parameter*& param) {
        if (param) {
            param->release();
            param = nullptr;
        }
    }

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
// Implements SpectrumDisplayListener to convert crossover frequency changes
// from the SpectrumDisplay UI into VST3 parameter edits that reach the Processor.
// Uses logarithmic normalization matching the Processor's interpretation:
//   normalized = (log10(freq) - log10(20)) / (log10(20000) - log10(20))
// ==============================================================================
class CrossoverDragBridge : public Steinberg::FObject, public SpectrumDisplayListener {
public:
    CrossoverDragBridge(Steinberg::Vst::EditControllerEx1* controller)
    : controller_(controller) {}

    void onCrossoverChanged(int dividerIndex, float frequencyHz) override {
        if (!controller_ || dividerIndex < 0 || dividerIndex >= kMaxBands - 1)
            return;

        auto paramId = makeCrossoverParamId(static_cast<uint8_t>(dividerIndex));

        // Convert Hz to normalized [0,1] using logarithmic mapping
        // Must match processor's interpretation:
        //   logFreq = log10(20) + normalized * (log10(20000) - log10(20))
        //   freqHz = 10^logFreq
        const float logMin = std::log10(kMinCrossoverHz);
        const float logMax = std::log10(kMaxCrossoverHz);
        float clampedFreq = std::clamp(frequencyHz, kMinCrossoverHz, kMaxCrossoverHz);
        float logFreq = std::log10(clampedFreq);
        double normalized = static_cast<double>(logFreq - logMin) / static_cast<double>(logMax - logMin);
        normalized = std::clamp(normalized, 0.0, 1.0);

        controller_->beginEdit(paramId);
        controller_->setParamNormalized(paramId, normalized);
        controller_->performEdit(paramId, normalized);
        controller_->endEdit(paramId);
    }

    void onBandSelected(int /*bandIndex*/) override {
        // No-op: band selection is handled elsewhere
    }

    void deactivate() {
        controller_ = nullptr;
    }

    OBJ_METHODS(CrossoverDragBridge, FObject)

private:
    Steinberg::Vst::EditControllerEx1* controller_ = nullptr;
};

// ==============================================================================
// Helper: Convert int to TChar string
// ==============================================================================
static void intToString128(int value, Steinberg::Vst::String128 dest) {
    char temp[32];
    snprintf(temp, sizeof(temp), "%d", value);
    for (int i = 0; temp[i] && i < 127; ++i) {
        dest[i] = static_cast<Steinberg::Vst::TChar>(temp[i]);
    }
    dest[std::min(static_cast<int>(strlen(temp)), 127)] = 0;
}

static void floatToString128(double value, int precision, Steinberg::Vst::String128 dest) {
    char temp[64];
    snprintf(temp, sizeof(temp), "%.*f", precision, value);
    for (int i = 0; temp[i] && i < 127; ++i) {
        dest[i] = static_cast<Steinberg::Vst::TChar>(temp[i]);
    }
    dest[std::min(static_cast<int>(strlen(temp)), 127)] = 0;
}

static void appendToString128(Steinberg::Vst::String128 dest, const Steinberg::Vst::TChar* suffix) {
    int len = 0;
    while (dest[len] && len < 127) ++len;
    int suffixLen = 0;
    while (suffix[suffixLen] && (len + suffixLen) < 127) {
        dest[len + suffixLen] = suffix[suffixLen];
        ++suffixLen;
    }
    dest[len + suffixLen] = 0;
}

// ==============================================================================
// PresetBrowserButton: Opens the preset browser modal (Spec 010)
// ==============================================================================
class PresetBrowserButton : public VSTGUI::CTextButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Controller* controller)
        : CTextButton(size, nullptr, -1, "Presets")
        , controller_(controller)
    {
        setFrameColor(VSTGUI::CColor(80, 80, 85));
        setTextColor(VSTGUI::CColor(255, 255, 255));
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (buttons.isLeftButton() && controller_) {
            controller_->openPresetBrowser();
            return VSTGUI::kMouseEventHandled;
        }
        return CTextButton::onMouseDown(where, buttons);
    }

private:
    Controller* controller_ = nullptr;
};

// ==============================================================================
// SavePresetButton: Opens the save preset dialog (Spec 010)
// ==============================================================================
class SavePresetButton : public VSTGUI::CTextButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Controller* controller)
        : CTextButton(size, nullptr, -1, "Save")
        , controller_(controller)
    {
        setFrameColor(VSTGUI::CColor(80, 80, 85));
        setTextColor(VSTGUI::CColor(255, 255, 255));
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (buttons.isLeftButton() && controller_) {
            controller_->openSavePresetDialog();
            return VSTGUI::kMouseEventHandled;
        }
        return CTextButton::onMouseDown(where, buttons);
    }

private:
    Controller* controller_ = nullptr;
};

// ==============================================================================
// Controller Implementation
// ==============================================================================

Controller::~Controller() {
    // Ensure visibility controllers are cleaned up
    for (auto& vc : bandVisibilityControllers_) {
        if (vc) {
            if (auto* cvc = dynamic_cast<ContainerVisibilityController*>(vc.get())) {
                cvc->deactivate();
            }
        }
    }
}

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    // Always call parent first
    Steinberg::tresult result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // Register all parameters (FR-004 through FR-006)
    registerGlobalParams();
    registerSweepParams();
    registerModulationParams();
    registerBandParams();
    registerNodeParams();

    // ==========================================================================
    // MIDI CC Manager (Spec 012)
    // ==========================================================================
    midiCCManager_ = std::make_unique<Krate::Plugins::MidiCCManager>();

    // ==========================================================================
    // Preset Manager (Spec 010)
    // ==========================================================================
    // Create PresetManager for preset browsing/scanning.
    // Note: We pass nullptr for processor since the controller doesn't have
    // direct access to it. We provide a state provider callback for saving.
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeDisrumpoPresetConfig(), nullptr, this);

    // Set state provider callback for preset saving
    presetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return this->createComponentStateStream();
    });

    // Set load provider callback for preset loading
    presetManager_->setLoadProvider([this](Steinberg::IBStream* state) -> bool {
        return this->loadComponentStateWithNotify(state);
    });

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    // Cleanup any resources allocated in initialize()
    return EditControllerEx1::terminate();
}

// ==============================================================================
// Parameter Registration Helpers
// ==============================================================================

void Controller::registerGlobalParams() {
    // FR-004: Register all global parameters

    // Input Gain: RangeParameter [-24, +24] dB, default 0
    auto* inputGainParam = new Steinberg::Vst::RangeParameter(
        STR16("Input Gain"),
        makeGlobalParamId(GlobalParamType::kGlobalInputGain),
        STR16("dB"),
        -24.0, 24.0, 0.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(inputGainParam);

    // Output Gain: RangeParameter [-24, +24] dB, default 0
    auto* outputGainParam = new Steinberg::Vst::RangeParameter(
        STR16("Output Gain"),
        makeGlobalParamId(GlobalParamType::kGlobalOutputGain),
        STR16("dB"),
        -24.0, 24.0, 0.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(outputGainParam);

    // Mix: RangeParameter [0, 100] %, default 100
    auto* mixParam = new Steinberg::Vst::RangeParameter(
        STR16("Mix"),
        makeGlobalParamId(GlobalParamType::kGlobalMix),
        STR16("%"),
        0.0, 100.0, 100.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(mixParam);

    // Band Count: StringListParameter ["1".."8"], default "4"
    auto* bandCountParam = new Steinberg::Vst::StringListParameter(
        STR16("Band Count"),
        makeGlobalParamId(GlobalParamType::kGlobalBandCount),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    for (int i = 1; i <= 4; ++i) {
        Steinberg::Vst::String128 str;
        intToString128(i, str);
        bandCountParam->appendString(str);
    }
    bandCountParam->setNormalized(3.0 / 3.0);  // Default to index 3 = "4"
    parameters.addParameter(bandCountParam);

    // Oversample Max: StringListParameter ["1x","2x","4x","8x"], default "4x"
    auto* oversampleParam = new Steinberg::Vst::StringListParameter(
        STR16("Oversample Max"),
        makeGlobalParamId(GlobalParamType::kGlobalOversample),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    oversampleParam->appendString(STR16("1x"));
    oversampleParam->appendString(STR16("2x"));
    oversampleParam->appendString(STR16("4x"));
    oversampleParam->appendString(STR16("8x"));
    oversampleParam->setNormalized(2.0 / 3.0);  // Default to index 2 = "4x"
    parameters.addParameter(oversampleParam);

    // T033: Modulation Panel Visible (Spec 012 FR-007, FR-009)
    auto* modPanelParam = new Steinberg::Vst::Parameter(
        STR16("Mod Panel Visible"),
        makeGlobalParamId(GlobalParamType::kGlobalModPanelVisible),
        nullptr,
        0.0,   // Default: hidden
        1,     // stepCount = 1 (boolean)
        Steinberg::Vst::ParameterInfo::kNoFlags  // Not automatable (UI-only)
    );
    parameters.addParameter(modPanelParam);

    // T055: MIDI Learn Active (Spec 012 FR-031)
    auto* midiLearnActiveParam = new Steinberg::Vst::Parameter(
        STR16("MIDI Learn Active"),
        makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive),
        nullptr,
        0.0,
        1,
        Steinberg::Vst::ParameterInfo::kNoFlags
    );
    parameters.addParameter(midiLearnActiveParam);

    // T055: MIDI Learn Target (Spec 012 FR-031)
    auto* midiLearnTargetParam = new Steinberg::Vst::Parameter(
        STR16("MIDI Learn Target"),
        makeGlobalParamId(GlobalParamType::kGlobalMidiLearnTarget),
        nullptr,
        0.0,
        0,
        Steinberg::Vst::ParameterInfo::kNoFlags
    );
    parameters.addParameter(midiLearnTargetParam);

    // Crossover frequency parameters (3 crossovers for 4 bands)
    const Steinberg::Vst::TChar* crossoverNames[] = {
        STR16("Crossover 1"), STR16("Crossover 2"), STR16("Crossover 3")
    };

    for (int i = 0; i < kMaxBands - 1; ++i) {
        const float logMin = std::log10(kMinCrossoverHz);
        const float logMax = std::log10(kMaxCrossoverHz);
        const float step = (logMax - logMin) / static_cast<float>(kMaxBands);
        const float logDefault = logMin + step * static_cast<float>(i + 1);
        const float defaultFreq = std::pow(10.0f, logDefault);

        auto* crossoverParam = new Steinberg::Vst::RangeParameter(
            crossoverNames[i],
            makeCrossoverParamId(static_cast<uint8_t>(i)),
            STR16("Hz"),
            static_cast<double>(kMinCrossoverHz),
            static_cast<double>(kMaxCrossoverHz),
            static_cast<double>(defaultFreq),
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(crossoverParam);
    }
}

void Controller::registerSweepParams() {
    // FR-004: Register sweep parameters (T4.10)

    // Sweep Enable: boolean toggle
    parameters.addParameter(
        STR16("Sweep Enable"),
        nullptr,
        1,  // stepCount = 1 for boolean
        0.0,  // default off
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepEnable)
    );

    // Sweep Frequency: RangeParameter [20, 20000] Hz, log scale
    auto* sweepFreqParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Frequency"),
        makeSweepParamId(SweepParamType::kSweepFrequency),
        STR16("Hz"),
        20.0, 20000.0, 1000.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(sweepFreqParam);

    // Sweep Width: RangeParameter [0.5, 4.0] octaves
    auto* sweepWidthParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Width"),
        makeSweepParamId(SweepParamType::kSweepWidth),
        STR16("oct"),
        0.5, 4.0, 1.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(sweepWidthParam);

    // Sweep Intensity: RangeParameter [0, 100] %
    auto* sweepIntensityParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Intensity"),
        makeSweepParamId(SweepParamType::kSweepIntensity),
        STR16("%"),
        0.0, 100.0, 50.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(sweepIntensityParam);

    // Sweep Morph Link: StringListParameter
    auto* morphLinkParam = new Steinberg::Vst::StringListParameter(
        STR16("Sweep Morph Link"),
        makeSweepParamId(SweepParamType::kSweepMorphLink),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    morphLinkParam->appendString(STR16("None"));
    morphLinkParam->appendString(STR16("Linear"));
    morphLinkParam->appendString(STR16("Inverse"));
    morphLinkParam->appendString(STR16("Ease In"));
    morphLinkParam->appendString(STR16("Ease Out"));
    morphLinkParam->appendString(STR16("Ease In-Out"));
    parameters.addParameter(morphLinkParam);

    // Sweep Falloff: StringListParameter
    auto* falloffParam = new Steinberg::Vst::StringListParameter(
        STR16("Sweep Falloff"),
        makeSweepParamId(SweepParamType::kSweepFalloff),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    falloffParam->appendString(STR16("Hard"));
    falloffParam->appendString(STR16("Soft"));
    parameters.addParameter(falloffParam);

    // =========================================================================
    // Sweep LFO Parameters (FR-024, FR-025)
    // =========================================================================

    // LFO Enable: boolean toggle
    parameters.addParameter(
        STR16("Sweep LFO Enable"),
        nullptr,
        1,  // stepCount = 1 for boolean
        0.0,  // default off
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepLFOEnable)
    );

    // LFO Rate: RangeParameter [0.01, 20] Hz
    auto* lfoRateParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep LFO Rate"),
        makeSweepParamId(SweepParamType::kSweepLFORate),
        STR16("Hz"),
        0.01, 20.0, 1.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(lfoRateParam);

    // LFO Waveform: StringListParameter
    auto* lfoWaveformParam = new Steinberg::Vst::StringListParameter(
        STR16("Sweep LFO Waveform"),
        makeSweepParamId(SweepParamType::kSweepLFOWaveform),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    lfoWaveformParam->appendString(STR16("Sine"));
    lfoWaveformParam->appendString(STR16("Triangle"));
    lfoWaveformParam->appendString(STR16("Sawtooth"));
    lfoWaveformParam->appendString(STR16("Square"));
    lfoWaveformParam->appendString(STR16("S&H"));
    lfoWaveformParam->appendString(STR16("Random"));
    parameters.addParameter(lfoWaveformParam);

    // LFO Depth: RangeParameter [0, 100] %
    auto* lfoDepthParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep LFO Depth"),
        makeSweepParamId(SweepParamType::kSweepLFODepth),
        STR16("%"),
        0.0, 100.0, 50.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(lfoDepthParam);

    // LFO Tempo Sync: boolean toggle
    parameters.addParameter(
        STR16("Sweep LFO Sync"),
        nullptr,
        1,  // stepCount = 1 for boolean
        0.0,  // default off (free mode)
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepLFOSync)
    );

    // LFO Note Value: StringListParameter for tempo-synced note values
    // Encoding: 5 base notes x 3 modifiers = 15 values
    auto* lfoNoteParam = new Steinberg::Vst::StringListParameter(
        STR16("Sweep LFO Note"),
        makeSweepParamId(SweepParamType::kSweepLFONoteValue),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    lfoNoteParam->appendString(STR16("1/1"));
    lfoNoteParam->appendString(STR16("1/1d"));
    lfoNoteParam->appendString(STR16("1/1t"));
    lfoNoteParam->appendString(STR16("1/2"));
    lfoNoteParam->appendString(STR16("1/2d"));
    lfoNoteParam->appendString(STR16("1/2t"));
    lfoNoteParam->appendString(STR16("1/4"));
    lfoNoteParam->appendString(STR16("1/4d"));
    lfoNoteParam->appendString(STR16("1/4t"));
    lfoNoteParam->appendString(STR16("1/8"));
    lfoNoteParam->appendString(STR16("1/8d"));
    lfoNoteParam->appendString(STR16("1/8t"));
    lfoNoteParam->appendString(STR16("1/16"));
    lfoNoteParam->appendString(STR16("1/16d"));
    lfoNoteParam->appendString(STR16("1/16t"));
    parameters.addParameter(lfoNoteParam);

    // =========================================================================
    // Sweep Envelope Follower Parameters (FR-026, FR-027)
    // =========================================================================

    // Envelope Enable: boolean toggle
    parameters.addParameter(
        STR16("Sweep Env Enable"),
        nullptr,
        1,  // stepCount = 1 for boolean
        0.0,  // default off
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepEnvEnable)
    );

    // Envelope Attack: RangeParameter [1, 100] ms
    auto* envAttackParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Env Attack"),
        makeSweepParamId(SweepParamType::kSweepEnvAttack),
        STR16("ms"),
        1.0, 100.0, 10.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(envAttackParam);

    // Envelope Release: RangeParameter [10, 500] ms
    auto* envReleaseParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Env Release"),
        makeSweepParamId(SweepParamType::kSweepEnvRelease),
        STR16("ms"),
        10.0, 500.0, 100.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(envReleaseParam);

    // Envelope Sensitivity: RangeParameter [0, 100] %
    auto* envSensParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Env Sensitivity"),
        makeSweepParamId(SweepParamType::kSweepEnvSensitivity),
        STR16("%"),
        0.0, 100.0, 50.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(envSensParam);

    // =========================================================================
    // Output Parameters (Processor -> Controller) — FR-047, FR-049
    // =========================================================================

    // Modulated Sweep Frequency: read-only output parameter
    parameters.addParameter(
        STR16("Sweep Mod Freq"),
        nullptr,
        0,  // continuous
        0.5,  // default: mid-range
        Steinberg::Vst::ParameterInfo::kIsReadOnly,
        kSweepModulatedFrequencyOutputId
    );

    // Detected MIDI CC: read-only output parameter for MIDI Learn (FR-029)
    parameters.addParameter(
        STR16("Sweep Detected CC"),
        nullptr,
        0,
        0.0,
        Steinberg::Vst::ParameterInfo::kIsReadOnly,
        kSweepDetectedCCOutputId
    );

    // =========================================================================
    // Custom Curve Parameters (FR-039a, FR-039b, FR-039c)
    // =========================================================================

    // Point Count: [2-8]
    auto* curvePointCountParam = new Steinberg::Vst::RangeParameter(
        STR16("Curve Point Count"),
        makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount),
        nullptr,
        2.0, 8.0, 2.0,
        6,  // 7 steps (2-8)
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(curvePointCountParam);

    // Register 8 pairs of X/Y point parameters
    static const Steinberg::Vst::TChar* pointNames[] = {
        STR16("Curve P0 X"), STR16("Curve P0 Y"),
        STR16("Curve P1 X"), STR16("Curve P1 Y"),
        STR16("Curve P2 X"), STR16("Curve P2 Y"),
        STR16("Curve P3 X"), STR16("Curve P3 Y"),
        STR16("Curve P4 X"), STR16("Curve P4 Y"),
        STR16("Curve P5 X"), STR16("Curve P5 Y"),
        STR16("Curve P6 X"), STR16("Curve P6 Y"),
        STR16("Curve P7 X"), STR16("Curve P7 Y")
    };

    for (int p = 0; p < 8; ++p) {
        auto idx = static_cast<size_t>(p);

        // Compute default X position
        float defaultX = 0.0f;
        if (p == 7) {
            defaultX = 1.0f;
        } else if (p > 0) {
            defaultX = static_cast<float>(p) / 7.0f;
        }

        // X coordinate
        auto xType = static_cast<SweepParamType>(
            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + p * 2);
        parameters.addParameter(
            pointNames[idx * 2],
            nullptr, 0, defaultX,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeSweepParamId(xType)
        );

        // Y coordinate
        auto yType = static_cast<SweepParamType>(
            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + p * 2);
        float defaultY = defaultX;  // Default to linear (y = x)
        parameters.addParameter(
            pointNames[idx * 2 + 1],
            nullptr, 0, defaultY,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeSweepParamId(yType)
        );
    }

    // =========================================================================
    // MIDI Parameters (FR-028, FR-029)
    // =========================================================================

    // MIDI Learn Active: boolean toggle
    parameters.addParameter(
        STR16("Sweep MIDI Learn"),
        nullptr,
        1,  // stepCount = 1 for boolean
        0.0,  // default off
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepMidiLearnActive)
    );

    // MIDI CC Number: [0-128], 128 = none
    auto* midiCCParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep MIDI CC"),
        makeSweepParamId(SweepParamType::kSweepMidiCCNumber),
        nullptr,
        0.0, 128.0, 128.0,
        128,  // 129 integer steps
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(midiCCParam);
}

void Controller::registerModulationParams() {
    // spec 008-modulation-system: Register all modulation parameters

    // =========================================================================
    // LFO 1 Parameters
    // =========================================================================

    auto* lfo1Rate = new Steinberg::Vst::RangeParameter(
        STR16("LFO 1 Rate"), makeModParamId(ModParamType::kLFO1Rate),
        STR16("Hz"), 0.01, 20.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(lfo1Rate);

    auto* lfo1Shape = new Steinberg::Vst::StringListParameter(
        STR16("LFO 1 Shape"), makeModParamId(ModParamType::kLFO1Shape));
    lfo1Shape->appendString(STR16("Sine"));
    lfo1Shape->appendString(STR16("Triangle"));
    lfo1Shape->appendString(STR16("Saw"));
    lfo1Shape->appendString(STR16("Square"));
    lfo1Shape->appendString(STR16("S&H"));
    lfo1Shape->appendString(STR16("Smooth Random"));
    parameters.addParameter(lfo1Shape);

    parameters.addParameter(STR16("LFO 1 Phase"), STR16("deg"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kLFO1Phase));

    parameters.addParameter(STR16("LFO 1 Sync"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kLFO1Sync));

    auto* lfo1Note = new Steinberg::Vst::StringListParameter(
        STR16("LFO 1 Note Value"), makeModParamId(ModParamType::kLFO1NoteValue));
    lfo1Note->appendString(STR16("1/1"));
    lfo1Note->appendString(STR16("1/1 D"));
    lfo1Note->appendString(STR16("1/1 T"));
    lfo1Note->appendString(STR16("1/2"));
    lfo1Note->appendString(STR16("1/2 D"));
    lfo1Note->appendString(STR16("1/2 T"));
    lfo1Note->appendString(STR16("1/4"));
    lfo1Note->appendString(STR16("1/4 D"));
    lfo1Note->appendString(STR16("1/4 T"));
    lfo1Note->appendString(STR16("1/8"));
    lfo1Note->appendString(STR16("1/8 D"));
    lfo1Note->appendString(STR16("1/8 T"));
    lfo1Note->appendString(STR16("1/16"));
    lfo1Note->appendString(STR16("1/16 D"));
    lfo1Note->appendString(STR16("1/16 T"));
    parameters.addParameter(lfo1Note);

    parameters.addParameter(STR16("LFO 1 Unipolar"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kLFO1Unipolar));

    parameters.addParameter(STR16("LFO 1 Retrigger"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kLFO1Retrigger));

    // =========================================================================
    // LFO 2 Parameters
    // =========================================================================

    auto* lfo2Rate = new Steinberg::Vst::RangeParameter(
        STR16("LFO 2 Rate"), makeModParamId(ModParamType::kLFO2Rate),
        STR16("Hz"), 0.01, 20.0, 0.5, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(lfo2Rate);

    auto* lfo2Shape = new Steinberg::Vst::StringListParameter(
        STR16("LFO 2 Shape"), makeModParamId(ModParamType::kLFO2Shape));
    lfo2Shape->appendString(STR16("Sine"));
    lfo2Shape->appendString(STR16("Triangle"));
    lfo2Shape->appendString(STR16("Saw"));
    lfo2Shape->appendString(STR16("Square"));
    lfo2Shape->appendString(STR16("S&H"));
    lfo2Shape->appendString(STR16("Smooth Random"));
    parameters.addParameter(lfo2Shape);

    parameters.addParameter(STR16("LFO 2 Phase"), STR16("deg"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kLFO2Phase));

    parameters.addParameter(STR16("LFO 2 Sync"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kLFO2Sync));

    auto* lfo2Note = new Steinberg::Vst::StringListParameter(
        STR16("LFO 2 Note Value"), makeModParamId(ModParamType::kLFO2NoteValue));
    lfo2Note->appendString(STR16("1/1"));
    lfo2Note->appendString(STR16("1/1 D"));
    lfo2Note->appendString(STR16("1/1 T"));
    lfo2Note->appendString(STR16("1/2"));
    lfo2Note->appendString(STR16("1/2 D"));
    lfo2Note->appendString(STR16("1/2 T"));
    lfo2Note->appendString(STR16("1/4"));
    lfo2Note->appendString(STR16("1/4 D"));
    lfo2Note->appendString(STR16("1/4 T"));
    lfo2Note->appendString(STR16("1/8"));
    lfo2Note->appendString(STR16("1/8 D"));
    lfo2Note->appendString(STR16("1/8 T"));
    lfo2Note->appendString(STR16("1/16"));
    lfo2Note->appendString(STR16("1/16 D"));
    lfo2Note->appendString(STR16("1/16 T"));
    parameters.addParameter(lfo2Note);

    parameters.addParameter(STR16("LFO 2 Unipolar"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kLFO2Unipolar));

    parameters.addParameter(STR16("LFO 2 Retrigger"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kLFO2Retrigger));

    // =========================================================================
    // Envelope Follower Parameters
    // =========================================================================

    parameters.addParameter(STR16("Env Attack"), STR16("ms"), 0, 0.091,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kEnvFollowerAttack));

    parameters.addParameter(STR16("Env Release"), STR16("ms"), 0, 0.184,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kEnvFollowerRelease));

    parameters.addParameter(STR16("Env Sensitivity"), STR16("%"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kEnvFollowerSensitivity));

    auto* envSource = new Steinberg::Vst::StringListParameter(
        STR16("Env Source"), makeModParamId(ModParamType::kEnvFollowerSource));
    envSource->appendString(STR16("Input L"));
    envSource->appendString(STR16("Input R"));
    envSource->appendString(STR16("Input Sum"));
    envSource->appendString(STR16("Mid"));
    envSource->appendString(STR16("Side"));
    parameters.addParameter(envSource);

    // =========================================================================
    // Random Source Parameters
    // =========================================================================

    parameters.addParameter(STR16("Random Rate"), STR16("Hz"), 0, 0.078,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kRandomRate));

    parameters.addParameter(STR16("Random Smoothness"), STR16("%"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kRandomSmoothness));

    parameters.addParameter(STR16("Random Sync"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kRandomSync));

    // =========================================================================
    // Chaos Source Parameters
    // =========================================================================

    auto* chaosModel = new Steinberg::Vst::StringListParameter(
        STR16("Chaos Model"), makeModParamId(ModParamType::kChaosModel));
    chaosModel->appendString(STR16("Lorenz"));
    chaosModel->appendString(STR16("Rossler"));
    chaosModel->appendString(STR16("Chua"));
    chaosModel->appendString(STR16("Henon"));
    parameters.addParameter(chaosModel);

    parameters.addParameter(STR16("Chaos Speed"), nullptr, 0, 0.048,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kChaosSpeed));

    parameters.addParameter(STR16("Chaos Coupling"), nullptr, 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kChaosCoupling));

    // =========================================================================
    // Sample & Hold Parameters
    // =========================================================================

    auto* shSource = new Steinberg::Vst::StringListParameter(
        STR16("S&H Source"), makeModParamId(ModParamType::kSampleHoldSource));
    shSource->appendString(STR16("Random"));
    shSource->appendString(STR16("LFO 1"));
    shSource->appendString(STR16("LFO 2"));
    shSource->appendString(STR16("External"));
    parameters.addParameter(shSource);

    parameters.addParameter(STR16("S&H Rate"), STR16("Hz"), 0, 0.078,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kSampleHoldRate));

    parameters.addParameter(STR16("S&H Slew"), STR16("ms"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kSampleHoldSlew));

    // =========================================================================
    // Pitch Follower Parameters
    // =========================================================================

    parameters.addParameter(STR16("Pitch Min Hz"), STR16("Hz"), 0, 0.125,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kPitchFollowerMinHz));

    parameters.addParameter(STR16("Pitch Max Hz"), STR16("Hz"), 0, 0.375,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kPitchFollowerMaxHz));

    parameters.addParameter(STR16("Pitch Confidence"), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kPitchFollowerConfidence));

    parameters.addParameter(STR16("Pitch Tracking"), STR16("ms"), 0, 0.138,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kPitchFollowerTrackingSpeed));

    // =========================================================================
    // Transient Detector Parameters
    // =========================================================================

    parameters.addParameter(STR16("Transient Sensitivity"), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kTransientSensitivity));

    parameters.addParameter(STR16("Transient Attack"), STR16("ms"), 0, 0.158,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kTransientAttack));

    parameters.addParameter(STR16("Transient Decay"), STR16("ms"), 0, 0.167,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeModParamId(ModParamType::kTransientDecay));

    // =========================================================================
    // Macro Parameters (4 macros x 4 params)
    // =========================================================================

    const Steinberg::Vst::TChar* macroNames[] = {
        STR16("Macro 1"), STR16("Macro 2"), STR16("Macro 3"), STR16("Macro 4")
    };
    const ModParamType macroValueTypes[] = {
        ModParamType::kMacro1Value, ModParamType::kMacro2Value,
        ModParamType::kMacro3Value, ModParamType::kMacro4Value
    };
    const ModParamType macroMinTypes[] = {
        ModParamType::kMacro1Min, ModParamType::kMacro2Min,
        ModParamType::kMacro3Min, ModParamType::kMacro4Min
    };
    const ModParamType macroMaxTypes[] = {
        ModParamType::kMacro1Max, ModParamType::kMacro2Max,
        ModParamType::kMacro3Max, ModParamType::kMacro4Max
    };
    const ModParamType macroCurveTypes[] = {
        ModParamType::kMacro1Curve, ModParamType::kMacro2Curve,
        ModParamType::kMacro3Curve, ModParamType::kMacro4Curve
    };

    for (int m = 0; m < 4; ++m) {
        parameters.addParameter(macroNames[m], nullptr, 0, 0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeModParamId(macroValueTypes[m]));

        // Build names dynamically is complex with STR16; use fixed format
        Steinberg::UString128 minStr("Macro ");
        minStr.append(Steinberg::UString128(std::to_string(m + 1).c_str()));
        minStr.append(Steinberg::UString128(" Min"));

        parameters.addParameter(minStr, nullptr, 0, 0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeModParamId(macroMinTypes[m]));

        Steinberg::UString128 maxStr("Macro ");
        maxStr.append(Steinberg::UString128(std::to_string(m + 1).c_str()));
        maxStr.append(Steinberg::UString128(" Max"));

        parameters.addParameter(maxStr, nullptr, 0, 1.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeModParamId(macroMaxTypes[m]));

        auto* macroCurve = new Steinberg::Vst::StringListParameter(
            STR16("Macro Curve"), makeModParamId(macroCurveTypes[m]));
        macroCurve->appendString(STR16("Linear"));
        macroCurve->appendString(STR16("Exponential"));
        macroCurve->appendString(STR16("S-Curve"));
        macroCurve->appendString(STR16("Stepped"));
        parameters.addParameter(macroCurve);
    }

    // =========================================================================
    // Routing Parameters (32 routings x 4 params)
    // =========================================================================

    for (uint8_t r = 0; r < 32; ++r) {
        // Source
        auto* routeSource = new Steinberg::Vst::StringListParameter(
            STR16("Route Source"), makeRoutingParamId(r, 0));
        routeSource->appendString(STR16("None"));
        routeSource->appendString(STR16("LFO 1"));
        routeSource->appendString(STR16("LFO 2"));
        routeSource->appendString(STR16("Env Follower"));
        routeSource->appendString(STR16("Random"));
        routeSource->appendString(STR16("Macro 1"));
        routeSource->appendString(STR16("Macro 2"));
        routeSource->appendString(STR16("Macro 3"));
        routeSource->appendString(STR16("Macro 4"));
        routeSource->appendString(STR16("Chaos"));
        routeSource->appendString(STR16("S&H"));
        routeSource->appendString(STR16("Pitch"));
        routeSource->appendString(STR16("Transient"));
        parameters.addParameter(routeSource);

        // Destination (named list of 54 modulatable parameters)
        auto* routeDest = new Steinberg::Vst::StringListParameter(
            STR16("Route Dest"), makeRoutingParamId(r, 1));
        // Global destinations (0-2)
        routeDest->appendString(STR16("Input Gain"));
        routeDest->appendString(STR16("Output Gain"));
        routeDest->appendString(STR16("Global Mix"));
        // Sweep destinations (3-5)
        routeDest->appendString(STR16("Sweep Freq"));
        routeDest->appendString(STR16("Sweep Width"));
        routeDest->appendString(STR16("Sweep Intensity"));
        // Per-band destinations (6-53): 8 bands x 6 params
        for (int b = 1; b <= 8; ++b) {
            routeDest->appendString(
                Steinberg::String().printf("Band %d Morph X", b));
            routeDest->appendString(
                Steinberg::String().printf("Band %d Morph Y", b));
            routeDest->appendString(
                Steinberg::String().printf("Band %d Drive", b));
            routeDest->appendString(
                Steinberg::String().printf("Band %d Mix", b));
            routeDest->appendString(
                Steinberg::String().printf("Band %d Gain", b));
            routeDest->appendString(
                Steinberg::String().printf("Band %d Pan", b));
        }
        parameters.addParameter(routeDest);

        // Amount [-1, +1] -> normalized [0, 1]
        auto* routeAmount = new Steinberg::Vst::RangeParameter(
            STR16("Route Amount"), makeRoutingParamId(r, 2),
            STR16("%"), -1.0, 1.0, 0.0, 0,
            Steinberg::Vst::ParameterInfo::kCanAutomate);
        parameters.addParameter(routeAmount);

        // Curve
        auto* routeCurve = new Steinberg::Vst::StringListParameter(
            STR16("Route Curve"), makeRoutingParamId(r, 3));
        routeCurve->appendString(STR16("Linear"));
        routeCurve->appendString(STR16("Exponential"));
        routeCurve->appendString(STR16("S-Curve"));
        routeCurve->appendString(STR16("Stepped"));
        parameters.addParameter(routeCurve);
    }
}

void Controller::registerBandParams() {
    // FR-005: Register per-band parameters for 4 bands

    const Steinberg::Vst::TChar* bandGainNames[] = {
        STR16("Band 1 Gain"), STR16("Band 2 Gain"), STR16("Band 3 Gain"), STR16("Band 4 Gain")
    };
    const Steinberg::Vst::TChar* bandPanNames[] = {
        STR16("Band 1 Pan"), STR16("Band 2 Pan"), STR16("Band 3 Pan"), STR16("Band 4 Pan")
    };
    const Steinberg::Vst::TChar* bandSoloNames[] = {
        STR16("Band 1 Solo"), STR16("Band 2 Solo"), STR16("Band 3 Solo"), STR16("Band 4 Solo")
    };
    const Steinberg::Vst::TChar* bandBypassNames[] = {
        STR16("Band 1 Bypass"), STR16("Band 2 Bypass"), STR16("Band 3 Bypass"), STR16("Band 4 Bypass")
    };
    const Steinberg::Vst::TChar* bandMuteNames[] = {
        STR16("Band 1 Mute"), STR16("Band 2 Mute"), STR16("Band 3 Mute"), STR16("Band 4 Mute")
    };
    const Steinberg::Vst::TChar* bandMorphXNames[] = {
        STR16("Band 1 Morph X"), STR16("Band 2 Morph X"), STR16("Band 3 Morph X"), STR16("Band 4 Morph X")
    };
    const Steinberg::Vst::TChar* bandMorphYNames[] = {
        STR16("Band 1 Morph Y"), STR16("Band 2 Morph Y"), STR16("Band 3 Morph Y"), STR16("Band 4 Morph Y")
    };
    const Steinberg::Vst::TChar* bandMorphModeNames[] = {
        STR16("Band 1 Morph Mode"), STR16("Band 2 Morph Mode"), STR16("Band 3 Morph Mode"), STR16("Band 4 Morph Mode")
    };
    const Steinberg::Vst::TChar* bandExpandedNames[] = {
        STR16("Band 1 Expanded"), STR16("Band 2 Expanded"), STR16("Band 3 Expanded"), STR16("Band 4 Expanded")
    };

    for (int b = 0; b < kMaxBands; ++b) {
        // Band Gain: RangeParameter [-24, +24] dB, default 0
        auto* gainParam = new Steinberg::Vst::RangeParameter(
            bandGainNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandGain),
            STR16("dB"),
            static_cast<double>(kMinBandGainDb),
            static_cast<double>(kMaxBandGainDb),
            0.0,
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(gainParam);

        // Band Pan: RangeParameter [-1, +1], default 0 (center)
        auto* panParam = new Steinberg::Vst::RangeParameter(
            bandPanNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandPan),
            STR16(""),
            -1.0, 1.0, 0.0,
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(panParam);

        // Band Solo: boolean toggle, stepCount=1
        parameters.addParameter(
            bandSoloNames[b],
            nullptr,
            1,  // stepCount = 1 for boolean (FR-030)
            0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSolo)
        );

        // Band Bypass: boolean toggle, stepCount=1
        parameters.addParameter(
            bandBypassNames[b],
            nullptr,
            1,
            0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandBypass)
        );

        // Band Mute: boolean toggle, stepCount=1
        parameters.addParameter(
            bandMuteNames[b],
            nullptr,
            1,
            0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMute)
        );

        // Band MorphX: RangeParameter [0, 1], default 0.5
        auto* morphXParam = new Steinberg::Vst::RangeParameter(
            bandMorphXNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphX),
            STR16(""),
            0.0, 1.0, 0.5,
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(morphXParam);

        // Band MorphY: RangeParameter [0, 1], default 0.5
        auto* morphYParam = new Steinberg::Vst::RangeParameter(
            bandMorphYNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphY),
            STR16(""),
            0.0, 1.0, 0.5,
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(morphYParam);

        // Band MorphMode: StringListParameter ["1D Linear","2D Planar","2D Radial"]
        auto* morphModeParam = new Steinberg::Vst::StringListParameter(
            bandMorphModeNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphMode),
            nullptr,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
        );
        morphModeParam->appendString(STR16("1D Linear"));
        morphModeParam->appendString(STR16("2D Planar"));
        morphModeParam->appendString(STR16("2D Radial"));
        parameters.addParameter(morphModeParam);

        // Band ActiveNodes: StringListParameter ["2","3","4"], default "4" (US6)
        static const Steinberg::Vst::TChar* activeNodesParamNames[] = {
            STR16("Band 1 Active Nodes"), STR16("Band 2 Active Nodes"),
            STR16("Band 3 Active Nodes"), STR16("Band 4 Active Nodes")
        };
        auto* activeNodesParam = new Steinberg::Vst::StringListParameter(
            activeNodesParamNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandActiveNodes),
            nullptr,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
        );
        activeNodesParam->appendString(STR16("2"));
        activeNodesParam->appendString(STR16("3"));
        activeNodesParam->appendString(STR16("4"));
        activeNodesParam->setNormalized(1.0);  // Default to "4" (index 2 = 1.0)
        parameters.addParameter(activeNodesParam);

        // Band Expanded: boolean toggle for expand/collapse state (UI only)
        // stepCount=1 for boolean, default 0 (collapsed)
        parameters.addParameter(
            bandExpandedNames[b],
            nullptr,
            1,
            0.0,
            Steinberg::Vst::ParameterInfo::kNoFlags,  // UI-only, not automatable
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandExpanded)
        );

        // Band MorphSmoothing: RangeParameter [0, 500] ms, default 0 (FR-031)
        static const Steinberg::Vst::TChar* morphSmoothingNames[] = {
            STR16("Band 1 Morph Smoothing"), STR16("Band 2 Morph Smoothing"),
            STR16("Band 3 Morph Smoothing"), STR16("Band 4 Morph Smoothing")
        };
        auto* morphSmoothingParam = new Steinberg::Vst::RangeParameter(
            morphSmoothingNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphSmoothing),
            STR16("ms"),
            0.0, 500.0, 0.0,
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(morphSmoothingParam);

        // Band MorphXLink: StringListParameter for morph X link mode (US8 FR-032)
        static const Steinberg::Vst::TChar* morphXLinkNames[] = {
            STR16("Band 1 Morph X Link"), STR16("Band 2 Morph X Link"),
            STR16("Band 3 Morph X Link"), STR16("Band 4 Morph X Link")
        };
        auto* morphXLinkParam = new Steinberg::Vst::StringListParameter(
            morphXLinkNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphXLink),
            nullptr,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
        );
        morphXLinkParam->appendString(STR16("None"));
        morphXLinkParam->appendString(STR16("Sweep Freq"));
        morphXLinkParam->appendString(STR16("Inverse Sweep"));
        morphXLinkParam->appendString(STR16("Ease In"));
        morphXLinkParam->appendString(STR16("Ease Out"));
        morphXLinkParam->appendString(STR16("Hold-Rise"));
        morphXLinkParam->appendString(STR16("Stepped"));
        parameters.addParameter(morphXLinkParam);

        // Band MorphYLink: StringListParameter for morph Y link mode (US8 FR-033)
        static const Steinberg::Vst::TChar* morphYLinkNames[] = {
            STR16("Band 1 Morph Y Link"), STR16("Band 2 Morph Y Link"),
            STR16("Band 3 Morph Y Link"), STR16("Band 4 Morph Y Link")
        };
        auto* morphYLinkParam = new Steinberg::Vst::StringListParameter(
            morphYLinkNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphYLink),
            nullptr,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
        );
        morphYLinkParam->appendString(STR16("None"));
        morphYLinkParam->appendString(STR16("Sweep Freq"));
        morphYLinkParam->appendString(STR16("Inverse Sweep"));
        morphYLinkParam->appendString(STR16("Ease In"));
        morphYLinkParam->appendString(STR16("Ease Out"));
        morphYLinkParam->appendString(STR16("Hold-Rise"));
        morphYLinkParam->appendString(STR16("Stepped"));
        parameters.addParameter(morphYLinkParam);

        // US7 FR-025: Selected Node parameter (which node's parameters to display)
        static const Steinberg::Vst::TChar* selectedNodeNames[] = {
            STR16("Band 1 Selected Node"), STR16("Band 2 Selected Node"),
            STR16("Band 3 Selected Node"), STR16("Band 4 Selected Node")
        };
        auto* selectedNodeParam = new Steinberg::Vst::StringListParameter(
            selectedNodeNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSelectedNode),
            nullptr,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
        );
        selectedNodeParam->appendString(STR16("Node A"));
        selectedNodeParam->appendString(STR16("Node B"));
        selectedNodeParam->appendString(STR16("Node C"));
        selectedNodeParam->appendString(STR16("Node D"));
        parameters.addParameter(selectedNodeParam);

        // US7 FR-024: Displayed Type (proxy for UIViewSwitchContainer, mirrors selected node's type)
        // This parameter is updated by NodeSelectionController when selected node changes
        static const Steinberg::Vst::TChar* displayedTypeNames[] = {
            STR16("Band 1 Displayed Type"), STR16("Band 2 Displayed Type"),
            STR16("Band 3 Displayed Type"), STR16("Band 4 Displayed Type")
        };
        auto* displayedTypeParam = new Steinberg::Vst::StringListParameter(
            displayedTypeNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandDisplayedType),
            nullptr,
            Steinberg::Vst::ParameterInfo::kIsList  // Not automatable - internal use only
        );
        // Same 26 distortion types as node type parameters
        displayedTypeParam->appendString(STR16("Soft Clip"));
        displayedTypeParam->appendString(STR16("Hard Clip"));
        displayedTypeParam->appendString(STR16("Tube"));
        displayedTypeParam->appendString(STR16("Tape"));
        displayedTypeParam->appendString(STR16("Fuzz"));
        displayedTypeParam->appendString(STR16("Asymmetric Fuzz"));
        displayedTypeParam->appendString(STR16("Sine Fold"));
        displayedTypeParam->appendString(STR16("Triangle Fold"));
        displayedTypeParam->appendString(STR16("Serge Fold"));
        displayedTypeParam->appendString(STR16("Full Rectify"));
        displayedTypeParam->appendString(STR16("Half Rectify"));
        displayedTypeParam->appendString(STR16("Bitcrush"));
        displayedTypeParam->appendString(STR16("Sample Reduce"));
        displayedTypeParam->appendString(STR16("Quantize"));
        displayedTypeParam->appendString(STR16("Temporal"));
        displayedTypeParam->appendString(STR16("Ring Saturation"));
        displayedTypeParam->appendString(STR16("Feedback"));
        displayedTypeParam->appendString(STR16("Aliasing"));
        displayedTypeParam->appendString(STR16("Bitwise Mangler"));
        displayedTypeParam->appendString(STR16("Chaos"));
        displayedTypeParam->appendString(STR16("Formant"));
        displayedTypeParam->appendString(STR16("Granular"));
        displayedTypeParam->appendString(STR16("Spectral"));
        displayedTypeParam->appendString(STR16("Fractal"));
        displayedTypeParam->appendString(STR16("Stochastic"));
        displayedTypeParam->appendString(STR16("Allpass Resonant"));
        parameters.addParameter(displayedTypeParam);

        // Band TabView: Main/Shape tab switching (UI only, not persisted)
        static const Steinberg::Vst::TChar* bandTabViewNames[] = {
            STR16("Band 1 Tab View"), STR16("Band 2 Tab View"),
            STR16("Band 3 Tab View"), STR16("Band 4 Tab View")
        };
        auto* tabViewParam = new Steinberg::Vst::StringListParameter(
            bandTabViewNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandTabView),
            nullptr,
            Steinberg::Vst::ParameterInfo::kNoFlags);
        tabViewParam->appendString(STR16("Main"));
        tabViewParam->appendString(STR16("Shape"));
        parameters.addParameter(tabViewParam);
    }
}

void Controller::registerNodeParams() {
    // FR-006: Register per-node parameters for 4 nodes x 8 bands (T4.13)

    // 26 distortion type names from dsp-details.md
    static const Steinberg::Vst::TChar* kDistortionTypeNames[] = {
        STR16("Soft Clip"),
        STR16("Hard Clip"),
        STR16("Tube"),
        STR16("Tape"),
        STR16("Fuzz"),
        STR16("Asymmetric Fuzz"),
        STR16("Sine Fold"),
        STR16("Triangle Fold"),
        STR16("Serge Fold"),
        STR16("Full Rectify"),
        STR16("Half Rectify"),
        STR16("Bitcrush"),
        STR16("Sample Reduce"),
        STR16("Quantize"),
        STR16("Temporal"),
        STR16("Ring Saturation"),
        STR16("Feedback"),
        STR16("Aliasing"),
        STR16("Bitwise Mangler"),
        STR16("Chaos"),
        STR16("Formant"),
        STR16("Granular"),
        STR16("Spectral"),
        STR16("Fractal"),
        STR16("Stochastic"),
        STR16("Allpass Resonant")
    };
    static constexpr int kNumDistortionTypes = 26;

    // Pre-defined parameter names for all band/node combinations
    static const Steinberg::Vst::TChar* nodeTypeNames[4][4] = {
        { STR16("B1 N1 Type"), STR16("B1 N2 Type"), STR16("B1 N3 Type"), STR16("B1 N4 Type") },
        { STR16("B2 N1 Type"), STR16("B2 N2 Type"), STR16("B2 N3 Type"), STR16("B2 N4 Type") },
        { STR16("B3 N1 Type"), STR16("B3 N2 Type"), STR16("B3 N3 Type"), STR16("B3 N4 Type") },
        { STR16("B4 N1 Type"), STR16("B4 N2 Type"), STR16("B4 N3 Type"), STR16("B4 N4 Type") }
    };
    static const Steinberg::Vst::TChar* nodeDriveNames[4][4] = {
        { STR16("B1 N1 Drive"), STR16("B1 N2 Drive"), STR16("B1 N3 Drive"), STR16("B1 N4 Drive") },
        { STR16("B2 N1 Drive"), STR16("B2 N2 Drive"), STR16("B2 N3 Drive"), STR16("B2 N4 Drive") },
        { STR16("B3 N1 Drive"), STR16("B3 N2 Drive"), STR16("B3 N3 Drive"), STR16("B3 N4 Drive") },
        { STR16("B4 N1 Drive"), STR16("B4 N2 Drive"), STR16("B4 N3 Drive"), STR16("B4 N4 Drive") }
    };
    static const Steinberg::Vst::TChar* nodeMixNames[4][4] = {
        { STR16("B1 N1 Mix"), STR16("B1 N2 Mix"), STR16("B1 N3 Mix"), STR16("B1 N4 Mix") },
        { STR16("B2 N1 Mix"), STR16("B2 N2 Mix"), STR16("B2 N3 Mix"), STR16("B2 N4 Mix") },
        { STR16("B3 N1 Mix"), STR16("B3 N2 Mix"), STR16("B3 N3 Mix"), STR16("B3 N4 Mix") },
        { STR16("B4 N1 Mix"), STR16("B4 N2 Mix"), STR16("B4 N3 Mix"), STR16("B4 N4 Mix") }
    };
    static const Steinberg::Vst::TChar* nodeToneNames[4][4] = {
        { STR16("B1 N1 Tone"), STR16("B1 N2 Tone"), STR16("B1 N3 Tone"), STR16("B1 N4 Tone") },
        { STR16("B2 N1 Tone"), STR16("B2 N2 Tone"), STR16("B2 N3 Tone"), STR16("B2 N4 Tone") },
        { STR16("B3 N1 Tone"), STR16("B3 N2 Tone"), STR16("B3 N3 Tone"), STR16("B3 N4 Tone") },
        { STR16("B4 N1 Tone"), STR16("B4 N2 Tone"), STR16("B4 N3 Tone"), STR16("B4 N4 Tone") }
    };
    static const Steinberg::Vst::TChar* nodeBiasNames[4][4] = {
        { STR16("B1 N1 Bias"), STR16("B1 N2 Bias"), STR16("B1 N3 Bias"), STR16("B1 N4 Bias") },
        { STR16("B2 N1 Bias"), STR16("B2 N2 Bias"), STR16("B2 N3 Bias"), STR16("B2 N4 Bias") },
        { STR16("B3 N1 Bias"), STR16("B3 N2 Bias"), STR16("B3 N3 Bias"), STR16("B3 N4 Bias") },
        { STR16("B4 N1 Bias"), STR16("B4 N2 Bias"), STR16("B4 N3 Bias"), STR16("B4 N4 Bias") }
    };
    static const Steinberg::Vst::TChar* nodeFoldsNames[4][4] = {
        { STR16("B1 N1 Folds"), STR16("B1 N2 Folds"), STR16("B1 N3 Folds"), STR16("B1 N4 Folds") },
        { STR16("B2 N1 Folds"), STR16("B2 N2 Folds"), STR16("B2 N3 Folds"), STR16("B2 N4 Folds") },
        { STR16("B3 N1 Folds"), STR16("B3 N2 Folds"), STR16("B3 N3 Folds"), STR16("B3 N4 Folds") },
        { STR16("B4 N1 Folds"), STR16("B4 N2 Folds"), STR16("B4 N3 Folds"), STR16("B4 N4 Folds") }
    };
    static const Steinberg::Vst::TChar* nodeBitDepthNames[4][4] = {
        { STR16("B1 N1 BitDepth"), STR16("B1 N2 BitDepth"), STR16("B1 N3 BitDepth"), STR16("B1 N4 BitDepth") },
        { STR16("B2 N1 BitDepth"), STR16("B2 N2 BitDepth"), STR16("B2 N3 BitDepth"), STR16("B2 N4 BitDepth") },
        { STR16("B3 N1 BitDepth"), STR16("B3 N2 BitDepth"), STR16("B3 N3 BitDepth"), STR16("B3 N4 BitDepth") },
        { STR16("B4 N1 BitDepth"), STR16("B4 N2 BitDepth"), STR16("B4 N3 BitDepth"), STR16("B4 N4 BitDepth") }
    };

    for (int b = 0; b < kMaxBands; ++b) {
        for (int n = 0; n < 4; ++n) {  // 4 nodes per band
            // Node Type: StringListParameter with 26 distortion types
            auto* typeParam = new Steinberg::Vst::StringListParameter(
                nodeTypeNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeType),
                nullptr,
                Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
            );
            for (auto & kDistortionTypeName : kDistortionTypeNames) {
                typeParam->appendString(kDistortionTypeName);
            }
            parameters.addParameter(typeParam);

            // Node Drive: RangeParameter [0, 10], default 1
            auto* driveParam = new Steinberg::Vst::RangeParameter(
                nodeDriveNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeDrive),
                STR16(""),
                0.0, 10.0, 1.0,
                0,
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(driveParam);

            // Node Mix: RangeParameter [0, 100] %, default 100
            auto* nodeMixParam = new Steinberg::Vst::RangeParameter(
                nodeMixNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeMix),
                STR16("%"),
                0.0, 100.0, 100.0,
                0,
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(nodeMixParam);

            // Node Tone: RangeParameter [200, 8000] Hz
            auto* toneParam = new Steinberg::Vst::RangeParameter(
                nodeToneNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeTone),
                STR16("Hz"),
                200.0, 8000.0, 4000.0,
                0,
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(toneParam);

            // Node Bias: RangeParameter [-1, +1], default 0
            auto* biasParam = new Steinberg::Vst::RangeParameter(
                nodeBiasNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeBias),
                STR16(""),
                -1.0, 1.0, 0.0,
                0,
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(biasParam);

            // Node Folds: RangeParameter [1, 12], integer steps
            auto* foldsParam = new Steinberg::Vst::RangeParameter(
                nodeFoldsNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeFolds),
                STR16(""),
                1.0, 12.0, 2.0,
                11,  // 12 integer steps (1-12)
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(foldsParam);

            // Node BitDepth: RangeParameter [4, 24], integer steps
            auto* bitDepthParam = new Steinberg::Vst::RangeParameter(
                nodeBitDepthNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeBitDepth),
                STR16("bit"),
                4.0, 24.0, 16.0,
                20,  // 21 integer steps (4-24)
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(bitDepthParam);
        }
    }
}

// ==============================================================================
// IEditController
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
    // FR-026: Sync from processor state
    // This method receives the processor's state and synchronizes the controller

    if (!state) {
        return Steinberg::kResultFalse;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version first (same format as Processor::setState)
    int32_t version = 0;
    if (!streamer.readInt32(version)) {
        return Steinberg::kResultFalse;
    }

    if (version < 1) {
        return Steinberg::kResultFalse;
    }

    // Read global parameters
    float inputGain = 0.5f;
    float outputGain = 0.5f;
    float globalMix = 1.0f;

    if (!streamer.readFloat(inputGain)) return Steinberg::kResultFalse;
    if (!streamer.readFloat(outputGain)) return Steinberg::kResultFalse;
    if (!streamer.readFloat(globalMix)) return Steinberg::kResultFalse;

    // Update controller's parameter values (for UI display)
    setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalInputGain), inputGain);
    setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalOutputGain), outputGain);
    setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalMix), globalMix);

    // Read band count if version >= 2
    if (version >= 2) {
        int32_t bandCount = 4;
        if (streamer.readInt32(bandCount)) {
            // Convert band count to normalized value (1-4 maps to 0.0-1.0)
            int clampedCount = std::clamp(bandCount, 1, 4);
            float normalizedBandCount = static_cast<float>(clampedCount - 1) / 3.0f;
            setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalBandCount), normalizedBandCount);
        }

        // Read band states
        // v7 and earlier wrote 8 bands; v8+ writes 4
        constexpr int kV7MaxBands = 8;
        const int streamBands = (version <= 7) ? kV7MaxBands : kMaxBands;
        for (int b = 0; b < streamBands; ++b) {
            float gain = 0.0f;
            float pan = 0.0f;
            Steinberg::int8 soloInt = 0;
            Steinberg::int8 bypassInt = 0;
            Steinberg::int8 muteInt = 0;

            streamer.readFloat(gain);
            streamer.readFloat(pan);
            streamer.readInt8(soloInt);
            streamer.readInt8(bypassInt);
            streamer.readInt8(muteInt);

            if (b < kMaxBands) {
                auto* gainParam = getParameterObject(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandGain));
                if (gainParam) {
                    setParamNormalized(gainParam->getInfo().id, gainParam->toNormalized(gain));
                }

                auto* panParam = getParameterObject(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandPan));
                if (panParam) {
                    setParamNormalized(panParam->getInfo().id, panParam->toNormalized(pan));
                }

                setParamNormalized(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSolo), soloInt != 0 ? 1.0f : 0.0f);
                setParamNormalized(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandBypass), bypassInt != 0 ? 1.0f : 0.0f);
                setParamNormalized(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMute), muteInt != 0 ? 1.0f : 0.0f);
            }
            // else: discard data from bands 4-7 (v7 migration)
        }

        // Read crossover frequencies
        // v7 and earlier wrote 7 crossovers; v8+ writes 3
        const int streamCrossovers = (version <= 7) ? 7 : (kMaxBands - 1);
        for (int i = 0; i < streamCrossovers; ++i) {
            float freq = 1000.0f;
            if (streamer.readFloat(freq)) {
                if (i < kMaxBands - 1) {
                    auto* param = getParameterObject(makeCrossoverParamId(static_cast<uint8_t>(i)));
                    if (param) {
                        setParamNormalized(param->getInfo().id, param->toNormalized(freq));
                    }
                }
            }
        }
    }

    // =========================================================================
    // Sweep System State (v4+) — SC-012
    // =========================================================================
    if (version >= 4) {
        // Sweep Core
        Steinberg::int8 sweepEnable = 0;
        float sweepFreqNorm = 0.566f;
        float sweepWidthNorm = 0.286f;
        float sweepIntensityNorm = 0.25f;
        Steinberg::int8 sweepFalloff = 1;
        Steinberg::int8 sweepMorphLink = 0;

        if (streamer.readInt8(sweepEnable))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepEnable),
                               sweepEnable != 0 ? 1.0 : 0.0);
        if (streamer.readFloat(sweepFreqNorm))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepFrequency),
                               sweepFreqNorm);
        if (streamer.readFloat(sweepWidthNorm))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepWidth),
                               sweepWidthNorm);
        if (streamer.readFloat(sweepIntensityNorm))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepIntensity),
                               sweepIntensityNorm);
        if (streamer.readInt8(sweepFalloff))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepFalloff),
                               sweepFalloff != 0 ? 1.0 : 0.0);
        if (streamer.readInt8(sweepMorphLink))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepMorphLink),
                               static_cast<double>(sweepMorphLink) / (kMorphLinkModeCount - 1));

        // LFO
        Steinberg::int8 lfoEnable = 0;
        float lfoRateNorm = 0.606f;
        Steinberg::int8 lfoWaveform = 0;
        float lfoDepth = 0.0f;
        Steinberg::int8 lfoSync = 0;
        Steinberg::int8 lfoNoteIndex = 0;

        if (streamer.readInt8(lfoEnable))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepLFOEnable),
                               lfoEnable != 0 ? 1.0 : 0.0);
        if (streamer.readFloat(lfoRateNorm))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepLFORate),
                               lfoRateNorm);
        if (streamer.readInt8(lfoWaveform))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepLFOWaveform),
                               static_cast<double>(lfoWaveform) / 5.0);
        if (streamer.readFloat(lfoDepth))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepLFODepth),
                               lfoDepth);
        if (streamer.readInt8(lfoSync))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepLFOSync),
                               lfoSync != 0 ? 1.0 : 0.0);
        if (streamer.readInt8(lfoNoteIndex))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepLFONoteValue),
                               static_cast<double>(lfoNoteIndex) / 14.0);

        // Envelope
        Steinberg::int8 envEnable = 0;
        float envAttackNorm = 0.091f;
        float envReleaseNorm = 0.184f;
        float envSensitivity = 0.5f;

        if (streamer.readInt8(envEnable))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepEnvEnable),
                               envEnable != 0 ? 1.0 : 0.0);
        if (streamer.readFloat(envAttackNorm))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepEnvAttack),
                               envAttackNorm);
        if (streamer.readFloat(envReleaseNorm))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepEnvRelease),
                               envReleaseNorm);
        if (streamer.readFloat(envSensitivity))
            setParamNormalized(makeSweepParamId(SweepParamType::kSweepEnvSensitivity),
                               envSensitivity);

        // Custom Curve - skip breakpoint data (controller doesn't need curve details,
        // processor handles it in setState)
        int32_t pointCount = 2;
        if (streamer.readInt32(pointCount)) {
            pointCount = std::clamp(pointCount, 2, 8);
            for (int32_t i = 0; i < pointCount; ++i) {
                float px = 0.0f;
                float py = 0.0f;
                streamer.readFloat(px);
                streamer.readFloat(py);
            }
        }
    }

    // =========================================================================
    // Modulation System State (v5+) — SC-010
    // =========================================================================
    if (version >= 5) {
        // --- Source Parameters ---

        // LFO 1 (7 values)
        float lfo1RateNorm = 0.5f;
        if (streamer.readFloat(lfo1RateNorm))
            setParamNormalized(makeModParamId(ModParamType::kLFO1Rate), lfo1RateNorm);
        Steinberg::int8 lfo1Shape = 0;
        if (streamer.readInt8(lfo1Shape))
            setParamNormalized(makeModParamId(ModParamType::kLFO1Shape),
                               static_cast<double>(lfo1Shape) / 5.0);
        float lfo1Phase = 0.0f;
        if (streamer.readFloat(lfo1Phase))
            setParamNormalized(makeModParamId(ModParamType::kLFO1Phase), lfo1Phase);
        Steinberg::int8 lfo1Sync = 0;
        if (streamer.readInt8(lfo1Sync))
            setParamNormalized(makeModParamId(ModParamType::kLFO1Sync),
                               lfo1Sync != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo1NoteIdx = 0;
        if (streamer.readInt8(lfo1NoteIdx))
            setParamNormalized(makeModParamId(ModParamType::kLFO1NoteValue),
                               static_cast<double>(lfo1NoteIdx) / 14.0);
        Steinberg::int8 lfo1Unipolar = 0;
        if (streamer.readInt8(lfo1Unipolar))
            setParamNormalized(makeModParamId(ModParamType::kLFO1Unipolar),
                               lfo1Unipolar != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo1Retrigger = 1;
        if (streamer.readInt8(lfo1Retrigger))
            setParamNormalized(makeModParamId(ModParamType::kLFO1Retrigger),
                               lfo1Retrigger != 0 ? 1.0 : 0.0);

        // LFO 2 (7 values)
        float lfo2RateNorm = 0.5f;
        if (streamer.readFloat(lfo2RateNorm))
            setParamNormalized(makeModParamId(ModParamType::kLFO2Rate), lfo2RateNorm);
        Steinberg::int8 lfo2Shape = 0;
        if (streamer.readInt8(lfo2Shape))
            setParamNormalized(makeModParamId(ModParamType::kLFO2Shape),
                               static_cast<double>(lfo2Shape) / 5.0);
        float lfo2Phase = 0.0f;
        if (streamer.readFloat(lfo2Phase))
            setParamNormalized(makeModParamId(ModParamType::kLFO2Phase), lfo2Phase);
        Steinberg::int8 lfo2Sync = 0;
        if (streamer.readInt8(lfo2Sync))
            setParamNormalized(makeModParamId(ModParamType::kLFO2Sync),
                               lfo2Sync != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo2NoteIdx = 0;
        if (streamer.readInt8(lfo2NoteIdx))
            setParamNormalized(makeModParamId(ModParamType::kLFO2NoteValue),
                               static_cast<double>(lfo2NoteIdx) / 14.0);
        Steinberg::int8 lfo2Unipolar = 0;
        if (streamer.readInt8(lfo2Unipolar))
            setParamNormalized(makeModParamId(ModParamType::kLFO2Unipolar),
                               lfo2Unipolar != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo2Retrigger = 1;
        if (streamer.readInt8(lfo2Retrigger))
            setParamNormalized(makeModParamId(ModParamType::kLFO2Retrigger),
                               lfo2Retrigger != 0 ? 1.0 : 0.0);

        // Envelope Follower (4 values)
        float envAttackNorm = 0.0f;
        if (streamer.readFloat(envAttackNorm))
            setParamNormalized(makeModParamId(ModParamType::kEnvFollowerAttack), envAttackNorm);
        float envReleaseNorm = 0.0f;
        if (streamer.readFloat(envReleaseNorm))
            setParamNormalized(makeModParamId(ModParamType::kEnvFollowerRelease), envReleaseNorm);
        float envSensitivity = 0.5f;
        if (streamer.readFloat(envSensitivity))
            setParamNormalized(makeModParamId(ModParamType::kEnvFollowerSensitivity), envSensitivity);
        Steinberg::int8 envSource = 0;
        if (streamer.readInt8(envSource))
            setParamNormalized(makeModParamId(ModParamType::kEnvFollowerSource),
                               static_cast<double>(envSource) / 4.0);

        // Random (3 values)
        float randomRateNorm = 0.0f;
        if (streamer.readFloat(randomRateNorm))
            setParamNormalized(makeModParamId(ModParamType::kRandomRate), randomRateNorm);
        float randomSmoothness = 0.0f;
        if (streamer.readFloat(randomSmoothness))
            setParamNormalized(makeModParamId(ModParamType::kRandomSmoothness), randomSmoothness);
        Steinberg::int8 randomSync = 0;
        if (streamer.readInt8(randomSync))
            setParamNormalized(makeModParamId(ModParamType::kRandomSync),
                               randomSync != 0 ? 1.0 : 0.0);

        // Chaos (3 values)
        Steinberg::int8 chaosModel = 0;
        if (streamer.readInt8(chaosModel))
            setParamNormalized(makeModParamId(ModParamType::kChaosModel),
                               static_cast<double>(chaosModel) / 3.0);
        float chaosSpeedNorm = 0.0f;
        if (streamer.readFloat(chaosSpeedNorm))
            setParamNormalized(makeModParamId(ModParamType::kChaosSpeed), chaosSpeedNorm);
        float chaosCoupling = 0.0f;
        if (streamer.readFloat(chaosCoupling))
            setParamNormalized(makeModParamId(ModParamType::kChaosCoupling), chaosCoupling);

        // Sample & Hold (3 values)
        Steinberg::int8 shSource = 0;
        if (streamer.readInt8(shSource))
            setParamNormalized(makeModParamId(ModParamType::kSampleHoldSource),
                               static_cast<double>(shSource) / 3.0);
        float shRateNorm = 0.0f;
        if (streamer.readFloat(shRateNorm))
            setParamNormalized(makeModParamId(ModParamType::kSampleHoldRate), shRateNorm);
        float shSlewNorm = 0.0f;
        if (streamer.readFloat(shSlewNorm))
            setParamNormalized(makeModParamId(ModParamType::kSampleHoldSlew), shSlewNorm);

        // Pitch Follower (4 values)
        float pitchMinNorm = 0.0f;
        if (streamer.readFloat(pitchMinNorm))
            setParamNormalized(makeModParamId(ModParamType::kPitchFollowerMinHz), pitchMinNorm);
        float pitchMaxNorm = 0.0f;
        if (streamer.readFloat(pitchMaxNorm))
            setParamNormalized(makeModParamId(ModParamType::kPitchFollowerMaxHz), pitchMaxNorm);
        float pitchConfidence = 0.5f;
        if (streamer.readFloat(pitchConfidence))
            setParamNormalized(makeModParamId(ModParamType::kPitchFollowerConfidence), pitchConfidence);
        float pitchTrackNorm = 0.0f;
        if (streamer.readFloat(pitchTrackNorm))
            setParamNormalized(makeModParamId(ModParamType::kPitchFollowerTrackingSpeed), pitchTrackNorm);

        // Transient (3 values)
        float transSensitivity = 0.5f;
        if (streamer.readFloat(transSensitivity))
            setParamNormalized(makeModParamId(ModParamType::kTransientSensitivity), transSensitivity);
        float transAttackNorm = 0.0f;
        if (streamer.readFloat(transAttackNorm))
            setParamNormalized(makeModParamId(ModParamType::kTransientAttack), transAttackNorm);
        float transDecayNorm = 0.0f;
        if (streamer.readFloat(transDecayNorm))
            setParamNormalized(makeModParamId(ModParamType::kTransientDecay), transDecayNorm);

        // Macros (4 x 4 = 16 values)
        constexpr ModParamType macroParams[4][4] = {
            {ModParamType::kMacro1Value, ModParamType::kMacro1Min, ModParamType::kMacro1Max, ModParamType::kMacro1Curve},
            {ModParamType::kMacro2Value, ModParamType::kMacro2Min, ModParamType::kMacro2Max, ModParamType::kMacro2Curve},
            {ModParamType::kMacro3Value, ModParamType::kMacro3Min, ModParamType::kMacro3Max, ModParamType::kMacro3Curve},
            {ModParamType::kMacro4Value, ModParamType::kMacro4Min, ModParamType::kMacro4Max, ModParamType::kMacro4Curve},
        };
        for (const auto& macro : macroParams) {
            float macroValue = 0.0f;
            if (streamer.readFloat(macroValue))
                setParamNormalized(makeModParamId(macro[0]), macroValue);
            float macroMin = 0.0f;
            if (streamer.readFloat(macroMin))
                setParamNormalized(makeModParamId(macro[1]), macroMin);
            float macroMax = 1.0f;
            if (streamer.readFloat(macroMax))
                setParamNormalized(makeModParamId(macro[2]), macroMax);
            Steinberg::int8 macroCurve = 0;
            if (streamer.readInt8(macroCurve))
                setParamNormalized(makeModParamId(macro[3]),
                                   static_cast<double>(macroCurve) / 3.0);
        }

        // --- Routing Parameters (32 x 4 values) ---
        for (uint8_t r = 0; r < 32; ++r) {
            Steinberg::int8 source = 0;
            if (streamer.readInt8(source))
                setParamNormalized(makeRoutingParamId(r, 0),
                                   static_cast<double>(source) / 12.0);
            int32_t dest = 0;
            if (streamer.readInt32(dest))
                setParamNormalized(makeRoutingParamId(r, 1),
                                   static_cast<double>(std::clamp(dest, 0, static_cast<int32_t>(ModDest::kTotalDestinations - 1)))
                                   / static_cast<double>(ModDest::kTotalDestinations - 1));
            float amount = 0.0f;
            if (streamer.readFloat(amount))
                setParamNormalized(makeRoutingParamId(r, 2),
                                   static_cast<double>(amount + 1.0f) / 2.0);
            Steinberg::int8 curve = 0;
            if (streamer.readInt8(curve))
                setParamNormalized(makeRoutingParamId(r, 3),
                                   static_cast<double>(curve) / 3.0);
        }
    }

    // =========================================================================
    // Morph Node State (v6+)
    // =========================================================================
    if (version >= 6) {
        // v7 and earlier wrote 8 bands of morph state; v8+ writes 4
        constexpr int kV7MorphBands = 8;
        const int streamMorphBands = (version <= 7) ? kV7MorphBands : kMaxBands;
        for (int b = 0; b < streamMorphBands; ++b) {
            const auto band = static_cast<uint8_t>(b);

            // Always read to advance stream position
            float morphX = 0.5f;
            float morphY = 0.5f;
            Steinberg::int8 morphMode = 0;
            auto activeNodes = static_cast<Steinberg::int8>(kDefaultActiveNodes);
            float morphSmoothing = 0.0f;

            bool readMorphX = streamer.readFloat(morphX);
            bool readMorphY = streamer.readFloat(morphY);
            bool readMorphMode = streamer.readInt8(morphMode);
            bool readActiveNodes = streamer.readInt8(activeNodes);
            bool readMorphSmoothing = streamer.readFloat(morphSmoothing);

            if (b < kMaxBands) {
                if (readMorphX)
                    setParamNormalized(
                        makeBandParamId(band, BandParamType::kBandMorphX),
                        static_cast<double>(morphX));
                if (readMorphY)
                    setParamNormalized(
                        makeBandParamId(band, BandParamType::kBandMorphY),
                        static_cast<double>(morphY));
                if (readMorphMode)
                    setParamNormalized(
                        makeBandParamId(band, BandParamType::kBandMorphMode),
                        static_cast<double>(morphMode) / 2.0);
                if (readActiveNodes) {
                    int count = std::clamp(static_cast<int>(activeNodes),
                                           kMinActiveNodes, kMaxMorphNodes);
                    setParamNormalized(
                        makeBandParamId(band, BandParamType::kBandActiveNodes),
                        static_cast<double>(count - 2) / 2.0);
                }
                if (readMorphSmoothing)
                    setParamNormalized(
                        makeBandParamId(band, BandParamType::kBandMorphSmoothing),
                        static_cast<double>(morphSmoothing) / 500.0);
            }

            // Per-node state (4 nodes x 7 values each) - always read to advance stream
            for (int n = 0; n < kMaxMorphNodes; ++n) {
                const auto node = static_cast<uint8_t>(n);

                Steinberg::int8 nodeType = 0;
                float drive = 1.0f;
                float mix = 1.0f;
                float tone = 4000.0f;
                float bias = 0.0f;
                float folds = 1.0f;
                float bitDepth = 16.0f;

                bool rType = streamer.readInt8(nodeType);
                bool rDrive = streamer.readFloat(drive);
                bool rMix = streamer.readFloat(mix);
                bool rTone = streamer.readFloat(tone);
                bool rBias = streamer.readFloat(bias);
                bool rFolds = streamer.readFloat(folds);
                bool rBitDepth = streamer.readFloat(bitDepth);

                if (b < kMaxBands) {
                    if (rType)
                        setParamNormalized(
                            makeNodeParamId(band, node, NodeParamType::kNodeType),
                            static_cast<double>(nodeType) / 25.0);
                    if (rDrive)
                        setParamNormalized(
                            makeNodeParamId(band, node, NodeParamType::kNodeDrive),
                            static_cast<double>(drive) / 10.0);
                    if (rMix)
                        setParamNormalized(
                            makeNodeParamId(band, node, NodeParamType::kNodeMix),
                            static_cast<double>(mix));
                    if (rTone)
                        setParamNormalized(
                            makeNodeParamId(band, node, NodeParamType::kNodeTone),
                            static_cast<double>(tone - 200.0f) / 7800.0);
                    if (rBias)
                        setParamNormalized(
                            makeNodeParamId(band, node, NodeParamType::kNodeBias),
                            static_cast<double>(bias + 1.0f) / 2.0);
                    if (rFolds)
                        setParamNormalized(
                            makeNodeParamId(band, node, NodeParamType::kNodeFolds),
                            static_cast<double>(folds - 1.0f) / 11.0);
                    if (rBitDepth)
                        setParamNormalized(
                            makeNodeParamId(band, node, NodeParamType::kNodeBitDepth),
                            static_cast<double>(bitDepth - 4.0f) / 20.0);
                }
            }
            // else for b >= kMaxBands: data read-and-discarded (v7 migration)
        }
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
    // Save controller-specific state (UI settings, etc.)
    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write controller state version (bumped to 2 for Spec 012)
    if (!streamer.writeInt32(2)) {
        return Steinberg::kResultFalse;
    }

    // Spec 012 T027: Serialize window size
    if (!streamer.writeDouble(lastWindowWidth_)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.writeDouble(lastWindowHeight_)) {
        return Steinberg::kResultFalse;
    }

    // Spec 012 T058: Serialize global MIDI CC mappings
    if (midiCCManager_) {
        auto midiData = midiCCManager_->serializeGlobalMappings();
        auto midiDataSize = static_cast<Steinberg::int32>(midiData.size());
        if (!streamer.writeInt32(midiDataSize)) {
            return Steinberg::kResultFalse;
        }
        if (midiDataSize > 0) {
            if (state->write(midiData.data(), midiDataSize, nullptr) != Steinberg::kResultOk) {
                return Steinberg::kResultFalse;
            }
        }
    } else {
        if (!streamer.writeInt32(0)) {
            return Steinberg::kResultFalse;
        }
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
    // Restore controller-specific state
    Steinberg::IBStreamer streamer(state, kLittleEndian);

    int32_t version = 0;
    if (!streamer.readInt32(version)) {
        return Steinberg::kResultOk;
    }

    // Spec 012 T028: Deserialize window size (version >= 2)
    // Note: height may include mod panel (200px extra) if it was visible.
    // The 5:3 ratio is enforced on the base area in editorAttached().
    if (version >= 2) {
        double width = 1000.0;
        double height = 600.0;
        if (streamer.readDouble(width) && streamer.readDouble(height)) {
            width = std::clamp(width, 834.0, 1400.0);
            height = std::clamp(height, 500.0, 1040.0);
            lastWindowWidth_ = width;
            lastWindowHeight_ = height;
        }

        // Spec 012 T059: Deserialize global MIDI CC mappings
        Steinberg::int32 midiDataSize = 0;
        if (streamer.readInt32(midiDataSize) && midiDataSize > 0) {
            std::vector<uint8_t> midiData(static_cast<size_t>(midiDataSize));
            if (state->read(midiData.data(), midiDataSize, nullptr) == Steinberg::kResultOk) {
                if (midiCCManager_) {
                    midiCCManager_->deserializeGlobalMappings(midiData.data(), midiData.size());
                }
            }
        }
    }

    return Steinberg::kResultOk;
}

// ==============================================================================
// IMidiMapping (FR-028, FR-029)
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::getMidiControllerAssignment(
    Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
    Steinberg::Vst::CtrlNumber midiControllerNumber,
    Steinberg::Vst::ParamID& id) {

    if (busIndex != 0) {
        return Steinberg::kResultFalse;
    }

    // T056: Query MidiCCManager for generalized CC mappings (Spec 012)
    if (midiCCManager_) {
        auto ccNum = static_cast<uint8_t>(midiControllerNumber);
        if (midiCCManager_->getMidiControllerAssignment(ccNum, id)) {
            return Steinberg::kResultTrue;
        }
    }

    // Legacy: Check sweep-only assigned CC
    if (assignedMidiCC_ < 128 && midiControllerNumber == assignedMidiCC_) {
        id = makeSweepParamId(SweepParamType::kSweepFrequency);
        return Steinberg::kResultTrue;
    }

    return Steinberg::kResultFalse;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
    // FR-011: Create VST3Editor with editor.uidesc
    if (std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0) {
        auto* editor = new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");

        // T025/FR-017: Window resize constraint with 5:3 aspect ratio
        // Min: 834x500 (exact 5:3), Max: 1400x840 (exact 5:3)
        // Constraints are updated dynamically by ModPanelToggleController when
        // the mod panel is shown (+200px to height bounds)
        {
            auto* mpParam = getParameterObject(
                makeGlobalParamId(GlobalParamType::kGlobalModPanelVisible));
            bool modVis = mpParam && (mpParam->getNormalized() >= 0.5);
            double extraH = modVis ? ModPanelToggleController::kModPanelHeight : 0.0;
            editor->setEditorSizeConstrains(
                VSTGUI::CPoint(834, 500 + extraH),
                VSTGUI::CPoint(1400, 840 + extraH));
        }

        return editor;
    }
    return nullptr;
}

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string) {

    // FR-027: Custom formatting for Drive, Mix, Gain, Type, Pan

    // Check for node parameters
    if (isNodeParamId(id)) {
        NodeParamType paramType = extractNodeParamType(id);

        // Drive: plain number, one decimal, no unit (e.g., "5.2")
        if (paramType == NodeParamType::kNodeDrive) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                floatToString128(plainValue, 1, string);
                return Steinberg::kResultTrue;
            }
        }

        // Node Mix: percentage with no decimal (e.g., "75%")
        if (paramType == NodeParamType::kNodeMix) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                int percent = static_cast<int>(std::round(plainValue));
                intToString128(percent, string);
                appendToString128(string, STR16("%"));
                return Steinberg::kResultTrue;
            }
        }
    }

    // Check for band parameters
    if (isBandParamId(id)) {
        BandParamType paramType = extractBandParamType(id);

        // Band Gain: dB with one decimal (e.g., "4.5 dB")
        if (paramType == BandParamType::kBandGain) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                floatToString128(plainValue, 1, string);
                appendToString128(string, STR16(" dB"));
                return Steinberg::kResultTrue;
            }
        }

        // Band Pan: percentage with L/R suffix (e.g., "30% L", "30% R", "Center")
        if (paramType == BandParamType::kBandPan) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);  // -1 to +1

                if (std::abs(plainValue) < 0.01) {
                    // Use manual copy for "Center"
                    const Steinberg::Vst::TChar* center = STR16("Center");
                    for (int i = 0; center[i] && i < 127; ++i) {
                        string[i] = center[i];
                        string[i + 1] = 0;
                    }
                } else if (plainValue < 0) {
                    int percent = static_cast<int>(std::round(std::abs(plainValue) * 100.0));
                    intToString128(percent, string);
                    appendToString128(string, STR16("% L"));
                } else {
                    int percent = static_cast<int>(std::round(plainValue * 100.0));
                    intToString128(percent, string);
                    appendToString128(string, STR16("% R"));
                }
                return Steinberg::kResultTrue;
            }
        }
    }

    // Check for global parameters
    if (isGlobalParamId(id)) {
        // Global Mix: percentage with no decimal (e.g., "75%")
        if (id == makeGlobalParamId(GlobalParamType::kGlobalMix)) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                int percent = static_cast<int>(std::round(plainValue));
                intToString128(percent, string);
                appendToString128(string, STR16("%"));
                return Steinberg::kResultTrue;
            }
        }

        // Input/Output Gain: dB with one decimal (e.g., "4.5 dB")
        if (id == makeGlobalParamId(GlobalParamType::kGlobalInputGain) ||
            id == makeGlobalParamId(GlobalParamType::kGlobalOutputGain)) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                floatToString128(plainValue, 1, string);
                appendToString128(string, STR16(" dB"));
                return Steinberg::kResultTrue;
            }
        }
    }

    // Fall back to default formatting
    return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string,
    Steinberg::Vst::ParamValue& valueNormalized) {
    // Fall back to default parsing
    return EditControllerEx1::getParamValueByString(id, string, valueNormalized);
}

// ==============================================================================
// VST3EditorDelegate
// ==============================================================================

VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* editor) {

    // FR-013: Create custom views
    if (std::strcmp(name, "SpectrumDisplay") == 0) {
        // Read size from UIAttributes
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;

        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");

        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2) {
                origin = VSTGUI::CPoint(x, y);
            }
        }

        if (sizeStr) {
            double w = 980.0;
            double h = 200.0;  // Default size
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2) {
                size = VSTGUI::CPoint(w, h);
            }
        } else {
            size = VSTGUI::CPoint(980.0, 200.0);  // Default size
        }

        VSTGUI::CRect rect(origin, size);
        auto* spectrumDisplay = new SpectrumDisplay(rect);

        // Initialize with current band count from parameter
        auto* bandCountParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalBandCount));
        if (bandCountParam) {
            float normalized = static_cast<float>(bandCountParam->getNormalized());
            int bandCount = static_cast<int>(std::round(normalized * 3.0f)) + 1;
            spectrumDisplay->setNumBands(bandCount);
        }

        // Initialize crossover frequencies from parameters
        for (int i = 0; i < kMaxBands - 1; ++i) {
            auto* crossoverParam = getParameterObject(makeCrossoverParamId(static_cast<uint8_t>(i)));
            if (crossoverParam) {
                float freq = static_cast<float>(crossoverParam->toPlain(crossoverParam->getNormalized()));
                spectrumDisplay->setCrossoverFrequency(i, freq);
            }
        }

        // Store reference for later access (e.g., in willClose)
        spectrumDisplay_ = spectrumDisplay;

        // Connect crossover drag bridge so UI drags propagate to processor
        auto* bridge = new CrossoverDragBridge(this);
        crossoverDragBridge_ = Steinberg::owned(bridge);
        spectrumDisplay->setListener(bridge);

        return spectrumDisplay;
    }

    if (std::strcmp(name, "MorphPad") == 0) {
        // FR-010: Create MorphPad custom control
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;

        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");

        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2) {
                origin = VSTGUI::CPoint(x, y);
            }
        }

        if (sizeStr) {
            double w = 250.0;  // Default full size
            double h = 200.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2) {
                size = VSTGUI::CPoint(w, h);
            }
        } else {
            size = VSTGUI::CPoint(250.0, 200.0);  // Default size
        }

        // FR-011: Wire to band-specific morph parameters
        // Read band index from "band" attribute (0-7, default 0)
        int bandIndex = 0;
        const std::string* bandStr = attributes.getAttributeValue("band");
        if (bandStr) {
            bandIndex = std::stoi(*bandStr);
            bandIndex = std::clamp(bandIndex, 0, kMaxBands - 1);
        }

        // Get ActiveNodes parameter ID for this band (US6: dynamic node count)
        Steinberg::Vst::ParamID activeNodesParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandActiveNodes);

        VSTGUI::CRect rect(origin, size);
        auto* morphPad = new MorphPad(rect, this, activeNodesParamId);

        // Set the control tag to the MorphX parameter ID for this band
        // MorphPad uses CControl::getValue()/setValue() for X position
        Steinberg::Vst::ParamID morphXParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandMorphX);
        morphPad->setTag(static_cast<int32_t>(morphXParamId));

        // Wire CControl listener so X position changes reach the host
        morphPad->setListener(editor);

        // Wire Y position parameter for direct host communication
        Steinberg::Vst::ParamID morphYParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandMorphY);
        morphPad->setMorphYParamId(morphYParamId);

        // Initialize morph position from current parameter values
        auto* morphXParam = getParameterObject(morphXParamId);
        auto* morphYParam = getParameterObject(makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandMorphY));

        if (morphXParam && morphYParam) {
            float morphX = static_cast<float>(morphXParam->getNormalized());
            float morphY = static_cast<float>(morphYParam->getNormalized());
            morphPad->setMorphPosition(morphX, morphY);
            morphPad->setValue(morphX);
        }

        // Initialize node types from the band's node type parameters
        for (int n = 0; n < 4; ++n) {
            auto* nodeTypeParam = getParameterObject(makeNodeParamId(
                static_cast<uint8_t>(bandIndex), static_cast<uint8_t>(n), NodeParamType::kNodeType));
            if (nodeTypeParam) {
                int typeIndex = static_cast<int>(std::round(nodeTypeParam->getNormalized() * 25.0));
                morphPad->setNodeType(n, static_cast<DistortionType>(typeIndex));
            }
        }

        // Store reference for cleanup in willClose()
        morphPads_[bandIndex] = morphPad;

        return morphPad;
    }

    if (std::strcmp(name, "DynamicNodeSelector") == 0) {
        // FR-XXX: Create DynamicNodeSelector custom control
        // A CSegmentButton that dynamically shows/hides segments based on ActiveNodes
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;

        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");

        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2) {
                origin = VSTGUI::CPoint(x, y);
            }
        }

        if (sizeStr) {
            double w = 140.0;  // Default width for A/B/C/D buttons
            double h = 22.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2) {
                size = VSTGUI::CPoint(w, h);
            }
        } else {
            size = VSTGUI::CPoint(140.0, 22.0);  // Default size
        }

        // Read band index from "band" attribute (0-7, default 0)
        int bandIndex = 0;
        const std::string* bandStr = attributes.getAttributeValue("band");
        if (bandStr) {
            bandIndex = std::stoi(*bandStr);
            bandIndex = std::clamp(bandIndex, 0, kMaxBands - 1);
        }

        // Get parameter IDs for this band
        Steinberg::Vst::ParamID activeNodesParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandActiveNodes);
        Steinberg::Vst::ParamID selectedNodeParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandSelectedNode);

        VSTGUI::CRect rect(origin, size);
        auto* nodeSelector = new DynamicNodeSelector(
            rect, this, activeNodesParamId, selectedNodeParamId);

        // Set the control tag to the SelectedNode parameter ID for this band
        // This enables VSTGUI's automatic parameter binding
        nodeSelector->setTag(static_cast<int32_t>(selectedNodeParamId));

        // Initialize selection from current parameter value
        auto* selectedNodeParam = getParameterObject(selectedNodeParamId);
        if (selectedNodeParam) {
            float normalized = static_cast<float>(selectedNodeParam->getNormalized());
            nodeSelector->setValueNormalized(normalized);
        }

        // Store reference for cleanup in willClose()
        dynamicNodeSelectors_[bandIndex] = nodeSelector;

        return nodeSelector;
    }

    if (std::strcmp(name, "NodeEditorBorder") == 0) {
        // Debug helper: Colored border showing which node (A/B/C/D) is selected
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;

        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");

        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2) {
                origin = VSTGUI::CPoint(x, y);
            }
        }

        if (sizeStr) {
            double w = 280.0;  // Default width
            double h = 230.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2) {
                size = VSTGUI::CPoint(w, h);
            }
        } else {
            size = VSTGUI::CPoint(280.0, 230.0);  // Default size
        }

        // Read band index from "band" attribute (0-7, default 0)
        int bandIndex = 0;
        const std::string* bandStr = attributes.getAttributeValue("band");
        if (bandStr) {
            bandIndex = std::stoi(*bandStr);
            bandIndex = std::clamp(bandIndex, 0, kMaxBands - 1);
        }

        // Get SelectedNode parameter ID for this band
        Steinberg::Vst::ParamID selectedNodeParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandSelectedNode);

        VSTGUI::CRect rect(origin, size);
        auto* border = new NodeEditorBorder(rect, this, selectedNodeParamId);

        // Store reference for cleanup (reuse morphPads array slot or add new array)
        // For now, the border will clean itself up via deactivate() in destructor

        return border;
    }

    // FR-039a: CustomCurveEditor for custom morph link mode
    if (std::strcmp(name, "CustomCurveEditor") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;

        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");

        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2) {
                origin = VSTGUI::CPoint(x, y);
            }
        }

        if (sizeStr) {
            double w = 200.0;
            double h = 150.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2) {
                size = VSTGUI::CPoint(w, h);
            }
        } else {
            size = VSTGUI::CPoint(200.0, 150.0);
        }

        VSTGUI::CRect rect(origin, size);
        auto* curveEditor = new CustomCurveEditor(rect, nullptr, 9200);

        // Initialize from current curve parameters
        std::array<std::pair<float, float>, 8> points{};
        auto* pointCountParam = getParameterObject(
            makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount));
        int pointCount = 2;
        if (pointCountParam) {
            pointCount = static_cast<int>(std::round(pointCountParam->toPlain(
                pointCountParam->getNormalized())));
            pointCount = std::clamp(pointCount, 2, 8);
        }

        for (int p = 0; p < pointCount; ++p) {
            auto xType = static_cast<SweepParamType>(
                static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + p * 2);
            auto yType = static_cast<SweepParamType>(
                static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + p * 2);

            auto* xParam = getParameterObject(makeSweepParamId(xType));
            auto* yParam = getParameterObject(makeSweepParamId(yType));

            float px = 0.0f;
            if (p == 0) {
                px = 0.0f;
            } else if (p == 7) {
                px = 1.0f;
            }
            float py = 0.0f;
            if (xParam) px = static_cast<float>(xParam->getNormalized());
            if (yParam) py = static_cast<float>(yParam->getNormalized());
            points[static_cast<size_t>(p)] = {px, py};
        }
        curveEditor->setBreakpoints(points, pointCount);

        // Wire up callbacks to update parameters
        curveEditor->setOnChange([this](int pointIndex, float x, float y) {
            auto xType = static_cast<SweepParamType>(
                static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + pointIndex * 2);
            auto yType = static_cast<SweepParamType>(
                static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + pointIndex * 2);

            auto xId = makeSweepParamId(xType);
            auto yId = makeSweepParamId(yType);

            beginEdit(xId);
            setParamNormalized(xId, x);
            performEdit(xId, x);
            endEdit(xId);

            beginEdit(yId);
            setParamNormalized(yId, y);
            performEdit(yId, y);
            endEdit(yId);
        });

        curveEditor->setOnAdd([this](float x, float y) {
            // Increment point count parameter
            auto pointCountId = makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount);
            auto* pcParam = getParameterObject(pointCountId);
            if (pcParam) {
                int count = static_cast<int>(std::round(pcParam->toPlain(pcParam->getNormalized())));
                if (count < 8) {
                    count++;
                    double norm = pcParam->toNormalized(static_cast<double>(count));
                    beginEdit(pointCountId);
                    setParamNormalized(pointCountId, norm);
                    performEdit(pointCountId, norm);
                    endEdit(pointCountId);

                    // Set the new point's X/Y
                    int newIdx = count - 1;  // Will need to be sorted
                    auto xType = static_cast<SweepParamType>(
                        static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + newIdx * 2);
                    auto yType = static_cast<SweepParamType>(
                        static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + newIdx * 2);

                    auto xId = makeSweepParamId(xType);
                    auto yId = makeSweepParamId(yType);

                    beginEdit(xId);
                    setParamNormalized(xId, x);
                    performEdit(xId, x);
                    endEdit(xId);

                    beginEdit(yId);
                    setParamNormalized(yId, y);
                    performEdit(yId, y);
                    endEdit(yId);
                }
            }
        });

        curveEditor->setOnRemove([this](int pointIndex) {
            auto pointCountId = makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount);
            auto* pcParam = getParameterObject(pointCountId);
            if (pcParam) {
                int count = static_cast<int>(std::round(pcParam->toPlain(pcParam->getNormalized())));
                if (count > 2 && pointIndex > 0 && pointIndex < count - 1) {
                    // Shift points down
                    for (int i = pointIndex; i < count - 1; ++i) {
                        auto srcXType = static_cast<SweepParamType>(
                            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + (i + 1) * 2);
                        auto srcYType = static_cast<SweepParamType>(
                            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + (i + 1) * 2);
                        auto dstXType = static_cast<SweepParamType>(
                            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + i * 2);
                        auto dstYType = static_cast<SweepParamType>(
                            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + i * 2);

                        auto* srcXParam = getParameterObject(makeSweepParamId(srcXType));
                        auto* srcYParam = getParameterObject(makeSweepParamId(srcYType));
                        auto dstXId = makeSweepParamId(dstXType);
                        auto dstYId = makeSweepParamId(dstYType);

                        if (srcXParam) {
                            double val = srcXParam->getNormalized();
                            beginEdit(dstXId);
                            setParamNormalized(dstXId, val);
                            performEdit(dstXId, val);
                            endEdit(dstXId);
                        }
                        if (srcYParam) {
                            double val = srcYParam->getNormalized();
                            beginEdit(dstYId);
                            setParamNormalized(dstYId, val);
                            performEdit(dstYId, val);
                            endEdit(dstYId);
                        }
                    }

                    // Decrement count
                    count--;
                    double norm = pcParam->toNormalized(static_cast<double>(count));
                    beginEdit(pointCountId);
                    setParamNormalized(pointCountId, norm);
                    performEdit(pointCountId, norm);
                    endEdit(pointCountId);
                }
            }
        });

        return curveEditor;
    }

    // FR-040 to FR-045: SweepIndicator for sweep visualization
    if (std::strcmp(name, "SweepIndicator") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;

        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");

        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2) {
                origin = VSTGUI::CPoint(x, y);
            }
        }

        if (sizeStr) {
            double w = 980.0;  // Default width (same as spectrum)
            double h = 200.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2) {
                size = VSTGUI::CPoint(w, h);
            }
        } else {
            size = VSTGUI::CPoint(980.0, 200.0);  // Default size
        }

        VSTGUI::CRect rect(origin, size);
        auto* sweepIndicator = new SweepIndicator(rect);

        // Initialize from current sweep parameter values
        auto* sweepEnableParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepEnable));
        if (sweepEnableParam) {
            sweepIndicator->setEnabled(sweepEnableParam->getNormalized() >= 0.5);
        }

        auto* sweepFreqParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepFrequency));
        auto* sweepWidthParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepWidth));
        auto* sweepIntensityParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepIntensity));
        auto* sweepFalloffParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepFalloff));

        if (sweepFreqParam && sweepWidthParam && sweepIntensityParam) {
            // Convert normalized to Hz (log scale)
            constexpr float kSweepLog2Min = 4.321928f;   // log2(20)
            constexpr float kSweepLog2Max = 14.287712f;  // log2(20000)
            constexpr float kSweepLog2Range = kSweepLog2Max - kSweepLog2Min;
            float freqNorm = static_cast<float>(sweepFreqParam->getNormalized());
            float log2Freq = kSweepLog2Min + freqNorm * kSweepLog2Range;
            float freqHz = std::pow(2.0f, log2Freq);

            // Convert normalized to octaves (linear 0.5 - 4.0)
            constexpr float kMinWidth = 0.5f;
            constexpr float kMaxWidth = 4.0f;
            float widthNorm = static_cast<float>(sweepWidthParam->getNormalized());
            float widthOct = kMinWidth + widthNorm * (kMaxWidth - kMinWidth);

            // Convert normalized to intensity (0 - 2)
            float intensityNorm = static_cast<float>(sweepIntensityParam->getNormalized());
            float intensity = intensityNorm * 2.0f;

            sweepIndicator->setPosition(freqHz, widthOct, intensity);
        }

        if (sweepFalloffParam) {
            sweepIndicator->setFalloffMode(
                sweepFalloffParam->getNormalized() >= 0.5
                    ? SweepFalloff::Smooth
                    : SweepFalloff::Sharp);
        }

        // Store reference for later access
        sweepIndicator_ = sweepIndicator;

        return sweepIndicator;
    }

    // Preset Browser Button (Spec 010)
    if (std::strcmp(name, "PresetBrowserButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(80, 25);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new PresetBrowserButton(rect, this);
    }

    // Save Preset Button (Spec 010)
    if (std::strcmp(name, "SavePresetButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(60, 25);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new SavePresetButton(rect, this);
    }

    return nullptr;
}

// ==============================================================================
// Sub-Controller Factory
// ==============================================================================
// Dispatches sub-controller creation based on name.
// Names encode the band index as a suffix digit: "BandShapeTab0" -> band 0
// The returned controller is owned by the framework (it will be deleted).
// ==============================================================================

VSTGUI::IController* Controller::createSubController(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* editor) {

    // Parse band-specific sub-controller names with band index as suffix digit:
    // "BandShapeTab0" through "BandShapeTab3"
    // "BandMainTab0" through "BandMainTab3"
    // "BandExpandedStrip0" through "BandExpandedStrip3"
    std::string_view sv(name);

    if (sv.size() > 1) {
        char lastChar = sv.back();
        if (lastChar >= '0' && lastChar <= '3') {
            int bandIndex = lastChar - '0';
            auto prefix = sv.substr(0, sv.size() - 1);

            if (prefix == "BandShapeTab" || prefix == "BandMainTab") {
                // editor (VST3Editor*) is the IController parent for delegation
                return new BandSubController(bandIndex, editor);
            }

            if (prefix == "BandExpandedStrip") {
                // Expanded strip needs createView() override for custom view band injection
                return new BandExpandedStripController(bandIndex, editor);
            }
        }
    }

    return nullptr;
}

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    // FR-023: Called when the editor is opened
    activeEditor_ = editor;

    // Create band visibility controllers (FR-025)
    // Show/hide band containers based on Band Count parameter
    auto* bandCountParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalBandCount));
    if (bandCountParam) {
        for (int b = 0; b < kMaxBands; ++b) {
            // Threshold for band visibility: band b is shown when bandCount >= b+1
            // Use midpoint thresholds to avoid float precision issues at exact boundaries
            // With 4 items (norm = 0, 0.333, 0.667, 1.0):
            // Band 0: -0.167 (always visible), Band 1: 0.167, Band 2: 0.5, Band 3: 0.833
            float threshold = (static_cast<float>(b) - 0.5f) / 3.0f;

            // UI-only visibility tags are 9000 + band index
            Steinberg::int32 containerTag = 9000 + b;

            bandVisibilityControllers_[b] = new ContainerVisibilityController(
                &activeEditor_,
                bandCountParam,
                containerTag,
                threshold,
                false  // Show when value >= threshold
            );
        }

        // Update SpectrumDisplay when band count changes
        bandCountDisplayController_ = new BandCountDisplayController(
            &spectrumDisplay_, bandCountParam);
    }

    // Create animated expand controllers (Spec 012 US1, replaces ContainerVisibilityController)
    // Show/hide BandStripExpanded based on Band*Expanded parameter with animation
    for (int b = 0; b < kMaxBands; ++b) {
        auto* expandedParam = getParameterObject(
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandExpanded));
        if (expandedParam) {
            // UI tag for expanded container: 9100 + band index
            Steinberg::int32 expandedContainerTag = 9100 + b;

            // FR-004: Pass parent band container tag (9000 + b) for visibility guard.
            // When band is hidden (band count < band index), skip expand animation.
            Steinberg::int32 parentBandTag = 9000 + b;

            expandedVisibilityControllers_[b] = new AnimatedExpandController(
                &activeEditor_,
                expandedParam,
                expandedContainerTag,
                280.0f,  // Expanded height matching uidesc container size (680x280)
                250,     // FR-005: 250ms animation (well within 300ms limit)
                parentBandTag  // FR-004: Parent band container tag for visibility guard
            );
        }
    }

    // T035: Create modulation panel visibility + resize controller (Spec 012 US3)
    auto* modPanelParam = getParameterObject(
        makeGlobalParamId(GlobalParamType::kGlobalModPanelVisible));
    if (modPanelParam) {
        modPanelVisController_ = new ModPanelToggleController(
            &activeEditor_,
            modPanelParam,
            9300    // UI tag for modulation panel container
        );
    }

    // T029/T030: Restore last window size (height may include mod panel)
    {
        bool modPanelOpen = modPanelParam && (modPanelParam->getNormalized() >= 0.5);
        double extraH = modPanelOpen ? ModPanelToggleController::kModPanelHeight : 0.0;
        double defaultH = 600.0 + extraH;

        if (lastWindowWidth_ != 1000.0 || lastWindowHeight_ != defaultH) {
            double constrainedWidth = std::clamp(lastWindowWidth_, 834.0, 1400.0);
            // Base height from 5:3 ratio, then add mod panel if visible
            double baseHeight = constrainedWidth * 3.0 / 5.0;
            double constrainedHeight = baseHeight + extraH;
            editor->requestResize(VSTGUI::CPoint(constrainedWidth, constrainedHeight));
        }
    }

    // T045/T047: Register keyboard shortcut handler and enable focus drawing
    {
        auto* frame = editor->getFrame();
        if (frame) {
            // Get current band count for keyboard handler
            int bandCount = 4;
            auto* bcParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalBandCount));
            if (bcParam) {
                bandCount = static_cast<int>(std::round(bcParam->getNormalized() * 3.0)) + 1;
            }

            keyboardHandler_ = std::make_unique<KeyboardShortcutHandler>(this, frame, bandCount);

            // T055a: Connect Escape key to MIDI Learn cancellation
            if (midiCCManager_) {
                keyboardHandler_->setEscapeCallback([this]() {
                    if (midiCCManager_ && midiCCManager_->isLearning()) {
                        midiCCManager_->cancelLearn();
                        setParamNormalized(
                            makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive), 0.0);
                    }
                });
            }

            frame->registerKeyboardHook(keyboardHandler_.get());

            // FR-010a: Enable focus drawing with 2px colored outline
            frame->setFocusDrawingEnabled(true);
            frame->setFocusColor(VSTGUI::CColor(0x3A, 0x96, 0xDD));  // Accent blue
            frame->setFocusWidth(2.0);
        }
    }

    // Spec 012 US7: Check OS accessibility preferences
    accessibilityPrefs_ = Krate::Plugins::queryAccessibilityPreferences();
    if (accessibilityPrefs_.reducedMotionPreferred) {
        // FR-028/FR-029: Disable animations when reduced motion is active
        for (auto& vc : expandedVisibilityControllers_) {
            if (auto* aec = dynamic_cast<AnimatedExpandController*>(vc.get())) {
                aec->setAnimationsEnabled(false);
            }
        }
    }

    // T070-T074: Apply high contrast colors when OS high contrast mode is active
    if (accessibilityPrefs_.highContrastEnabled) {
        auto& colors = accessibilityPrefs_.colors;

        // Convert uint32_t ARGB to CColor
        auto toCColor = [](uint32_t argb) {
            return VSTGUI::CColor(
                static_cast<uint8_t>((argb >> 16) & 0xFF),
                static_cast<uint8_t>((argb >> 8) & 0xFF),
                static_cast<uint8_t>(argb & 0xFF),
                static_cast<uint8_t>((argb >> 24) & 0xFF)
            );
        };

        auto borderColor = toCColor(colors.border);
        auto bgColor = toCColor(colors.background);
        auto accentColor = toCColor(colors.accent);

        // T070: Apply to CFrame background
        auto* frame = editor->getFrame();
        if (frame) {
            frame->setBackgroundColor(bgColor);
        }

        // T071: Apply to SpectrumDisplay
        if (spectrumDisplay_) {
            spectrumDisplay_->setHighContrastMode(true, borderColor, bgColor, accentColor);
        }

        // T072: Apply to MorphPads
        for (auto* mp : morphPads_) {
            if (mp) {
                mp->setHighContrastMode(true, borderColor, accentColor);
            }
        }

        // T073: Apply to SweepIndicator
        if (sweepIndicator_) {
            sweepIndicator_->setHighContrastMode(true, accentColor);
        }

        // T074: Apply to DynamicNodeSelectors, CustomCurveEditor, NodeEditorBorder
        for (auto* dns : dynamicNodeSelectors_) {
            if (dns) {
                dns->setHighContrastMode(true);
            }
        }
    }

    // T159-T161: Create morph-sweep link controller (US8)
    // Updates morph X/Y positions when sweep frequency changes (based on link mode)
    auto* sweepFreqParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepFrequency));
    if (sweepFreqParam) {
        morphSweepLinkController_ = new MorphSweepLinkController(this, sweepFreqParam);
    }

    // US7 FR-024/FR-025: Create node selection controllers
    // Updates DisplayedType proxy when SelectedNode changes
    for (int b = 0; b < kMaxBands; ++b) {
        nodeSelectionControllers_[b] = new NodeSelectionController(
            this, static_cast<uint8_t>(b));
    }

    // FR-047, FR-049: Create sweep visualization controller
    sweepVisualizationController_ = new SweepVisualizationController(
        this, &sweepIndicator_, &spectrumDisplay_);

    // FR-047: Create 30fps timer for smooth sweep indicator redraws
    sweepVisualizationTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
        [this](VSTGUI::CVSTGUITimer* /*timer*/) {
            if (sweepIndicator_ && sweepIndicator_->isEnabled()) {
                sweepIndicator_->setDirty();
            }
        },
        33  // ~30fps (33ms interval)
    );

    // FR-039a: Custom curve visibility controller
    // Show curve editor container when Morph Link mode is "Custom" (index 7)
    auto* morphLinkParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepMorphLink));
    if (morphLinkParam) {
        // Custom mode is the last value (index 7 of 8 modes, normalized ~= 1.0)
        // But currently MorphLink dropdown only has 6 modes (None to Ease In-Out)
        // so Custom = index 5 (highest registered). Threshold at ~0.93 to show only
        // when the last mode is selected.
        customCurveVisController_ = new ContainerVisibilityController(
            &activeEditor_,
            morphLinkParam,
            9200,  // UI tag for custom curve container
            0.93f,
            false  // Show when value >= threshold
        );
    }

    // =========================================================================
    // Preset Browser (Spec 010)
    // =========================================================================
    // Create preset browser and save dialog views as frame overlays.
    // Views are initially hidden and shown via openPresetBrowser()/openSavePresetDialog().
    if (presetManager_) {
        auto* frame = editor->getFrame();
        if (frame) {
            auto frameSize = frame->getViewSize();
            presetBrowserView_ = new Krate::Plugins::PresetBrowserView(
                frameSize, presetManager_.get(), getDisrumpoTabLabels());
            frame->addView(presetBrowserView_);

            savePresetDialogView_ = new Krate::Plugins::SavePresetDialogView(
                frameSize, presetManager_.get());
            frame->addView(savePresetDialogView_);
        }
    }
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // Save current window size before closing so getState() persists it
    if (editor) {
        auto* frame = editor->getFrame();
        if (frame) {
            auto rect = frame->getViewSize();
            lastWindowWidth_ = rect.getWidth();
            lastWindowHeight_ = rect.getHeight();
        }
    }

    // FR-024: Called when the editor is about to close
    // CRITICAL: Deactivate all visibility controllers BEFORE clearing them

    for (auto& vc : bandVisibilityControllers_) {
        if (vc) {
            if (auto* cvc = dynamic_cast<ContainerVisibilityController*>(vc.get())) {
                cvc->deactivate();
            }
            vc = nullptr;
        }
    }

    // Spec 012: Deactivate animated expand controllers
    for (auto& vc : expandedVisibilityControllers_) {
        if (vc) {
            if (auto* aec = dynamic_cast<AnimatedExpandController*>(vc.get())) {
                aec->deactivate();
            }
            vc = nullptr;
        }
    }

    // T161: Deactivate morph-sweep link controller
    if (morphSweepLinkController_) {
        if (auto* mslc = dynamic_cast<MorphSweepLinkController*>(morphSweepLinkController_.get())) {
            mslc->deactivate();
        }
        morphSweepLinkController_ = nullptr;
    }

    // US7: Deactivate node selection controllers
    for (auto& nsc : nodeSelectionControllers_) {
        if (nsc) {
            if (auto* controller = dynamic_cast<NodeSelectionController*>(nsc.get())) {
                controller->deactivate();
            }
            nsc = nullptr;
        }
    }

    // US6: Deactivate dynamic node selectors
    // Note: The views themselves are managed by VSTGUI, we just deactivate and clear refs
    for (auto& dns : dynamicNodeSelectors_) {
        if (dns) {
            dns->deactivate();
            dns = nullptr;  // Don't delete - VSTGUI owns the view
        }
    }

    // US6: Deactivate MorphPads
    // Note: The views themselves are managed by VSTGUI, we just deactivate and clear refs
    for (auto& mp : morphPads_) {
        if (mp) {
            mp->deactivate();
            mp = nullptr;  // Don't delete - VSTGUI owns the view
        }
    }

    // FR-047: Deactivate sweep visualization controller
    if (sweepVisualizationController_) {
        if (auto* svc = dynamic_cast<SweepVisualizationController*>(sweepVisualizationController_.get())) {
            svc->deactivate();
        }
        sweepVisualizationController_ = nullptr;
    }

    // FR-047: Stop visualization timer
    if (sweepVisualizationTimer_) {
        sweepVisualizationTimer_->stop();
        sweepVisualizationTimer_ = nullptr;
    }

    // FR-039a: Deactivate custom curve visibility controller
    if (customCurveVisController_) {
        if (auto* cvc = dynamic_cast<ContainerVisibilityController*>(customCurveVisController_.get())) {
            cvc->deactivate();
        }
        customCurveVisController_ = nullptr;
    }

    // Deactivate band count display controller
    if (bandCountDisplayController_) {
        if (auto* bcdc = dynamic_cast<BandCountDisplayController*>(bandCountDisplayController_.get())) {
            bcdc->deactivate();
        }
        bandCountDisplayController_ = nullptr;
    }

    // T046: Unregister keyboard shortcut handler
    if (keyboardHandler_) {
        auto* frame = editor->getFrame();
        if (frame) {
            frame->unregisterKeyboardHook(keyboardHandler_.get());
        }
        keyboardHandler_.reset();
    }

    // Spec 012: Deactivate modulation panel toggle controller
    if (modPanelVisController_) {
        if (auto* mtc = dynamic_cast<ModPanelToggleController*>(modPanelVisController_.get())) {
            mtc->deactivate();
        }
        modPanelVisController_ = nullptr;
    }

    // Spec 010: Clear preset browser view pointers (views are owned by frame)
    presetBrowserView_ = nullptr;
    savePresetDialogView_ = nullptr;

    // Deactivate crossover drag bridge before clearing SpectrumDisplay
    if (crossoverDragBridge_) {
        if (auto* bridge = dynamic_cast<CrossoverDragBridge*>(crossoverDragBridge_.get())) {
            bridge->deactivate();
        }
        crossoverDragBridge_ = nullptr;
    }

    sweepIndicator_ = nullptr;
    spectrumDisplay_ = nullptr;
    activeEditor_ = nullptr;

    (void)editor;  // Suppress unused parameter warning
}

// ==============================================================================
// MIDI Learn Context Menu (Spec 012 FR-031)
// ==============================================================================

bool Controller::findParameter(
    const VSTGUI::CPoint& pos,
    Steinberg::Vst::ParamID& paramID,
    VSTGUI::VST3Editor* editor) {

    if (!editor) return false;
    auto* frame = editor->getFrame();
    if (!frame) return false;

    // Hit test the point against all controls
    VSTGUI::CPoint localPos(pos);
    auto* hitView = frame->getViewAt(localPos, VSTGUI::GetViewOptions().deep());
    if (!hitView) return false;

    auto* control = dynamic_cast<VSTGUI::CControl*>(hitView);
    if (!control) return false;

    auto tag = control->getTag();
    if (tag < 0) return false;

    paramID = static_cast<Steinberg::Vst::ParamID>(tag);
    return true;
}

VSTGUI::COptionMenu* Controller::createContextMenu(
    const VSTGUI::CPoint& pos,
    VSTGUI::VST3Editor* editor) {

    Steinberg::Vst::ParamID paramId = 0;
    if (!findParameter(pos, paramId, editor)) {
        return nullptr;
    }

    if (!midiCCManager_) return nullptr;

    auto* menu = new VSTGUI::COptionMenu();

    // T052: Add "MIDI Learn" menu item
    {
        VSTGUI::CCommandMenuItem::Desc desc("MIDI Learn");
        auto* learnItem = new VSTGUI::CCommandMenuItem(std::move(desc));
        learnItem->setActions([this, paramId](VSTGUI::CCommandMenuItem*) {
            if (midiCCManager_) {
                midiCCManager_->startLearn(paramId);
                setParamNormalized(
                    makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive), 1.0);
            }
        });
        menu->addEntry(learnItem);
    }

    // T053: Add "Clear MIDI Learn" if parameter is already mapped
    uint8_t existingCC = 0;
    if (midiCCManager_->getCCForParam(paramId, existingCC)) {
        {
            VSTGUI::CCommandMenuItem::Desc clearDesc("Clear MIDI Learn");
            auto* clearItem = new VSTGUI::CCommandMenuItem(std::move(clearDesc));
            clearItem->setActions([this, paramId](VSTGUI::CCommandMenuItem*) {
                if (midiCCManager_) {
                    midiCCManager_->removeMappingsForParam(paramId);
                }
            });
            menu->addEntry(clearItem);
        }

        // T054: Add "Save Mapping with Preset" checkbox
        Krate::Plugins::MidiCCMapping mapping;
        if (midiCCManager_->getMapping(existingCC, mapping)) {
            VSTGUI::CCommandMenuItem::Desc presetDesc("Save Mapping with Preset");
            auto* presetItem = new VSTGUI::CCommandMenuItem(std::move(presetDesc));
            presetItem->setActions([this, existingCC, paramId, mapping](VSTGUI::CCommandMenuItem*) {
                if (midiCCManager_) {
                    if (!mapping.isPerPreset) {
                        midiCCManager_->removeGlobalMapping(existingCC);
                        midiCCManager_->addPresetMapping(existingCC, paramId, mapping.is14Bit);
                    } else {
                        midiCCManager_->removePresetMapping(existingCC);
                        midiCCManager_->addGlobalMapping(existingCC, paramId, mapping.is14Bit);
                    }
                }
            });
            menu->addEntry(presetItem);
        }
    }

    return menu;
}

// ==============================================================================
// Preset Browser (Spec 010)
// ==============================================================================

void Controller::openPresetBrowser() {
    if (presetBrowserView_ && !presetBrowserView_->isOpen()) {
        // Disrumpo doesn't have a single "mode" like Iterum's delay modes.
        // Open with empty subcategory to show "All" tab.
        presetBrowserView_->open("");
    }
}

void Controller::openSavePresetDialog() {
    if (savePresetDialogView_ && !savePresetDialogView_->isOpen()) {
        savePresetDialogView_->open("");
    }
}

void Controller::closePresetBrowser() {
    if (presetBrowserView_ && presetBrowserView_->isOpen()) {
        presetBrowserView_->close();
    }
}

// ==============================================================================
// State Serialization for Preset Saving
// ==============================================================================

Steinberg::MemoryStream* Controller::createComponentStateStream() {
    // Create a memory stream and serialize current parameter values
    // in the same format as Processor::getState()
    auto* stream = new Steinberg::MemoryStream();
    Steinberg::IBStreamer streamer(stream, kLittleEndian);

    // Helper to get normalized float from controller parameter
    auto getParamNorm = [this](Steinberg::Vst::ParamID id) -> float {
        if (auto* param = getParameterObject(id)) {
            return static_cast<float>(param->getNormalized());
        }
        return 0.0f;
    };

    // Helper to get denormalized float from controller parameter
    auto getFloat = [this](Steinberg::Vst::ParamID id, float defaultVal) -> float {
        if (auto* param = getParameterObject(id)) {
            return static_cast<float>(param->toPlain(param->getNormalized()));
        }
        return defaultVal;
    };

    // Helper to get int32 from controller parameter
    auto getInt32 = [this](Steinberg::Vst::ParamID id, Steinberg::int32 defaultVal) -> Steinberg::int32 {
        if (auto* param = getParameterObject(id)) {
            return static_cast<Steinberg::int32>(param->toPlain(param->getNormalized()));
        }
        return defaultVal;
    };

    // Helper to get int8 from list parameter (multiplied by step count)
    auto getInt8FromList = [this](Steinberg::Vst::ParamID id, int maxVal) -> Steinberg::int8 {
        if (auto* param = getParameterObject(id)) {
            return static_cast<Steinberg::int8>(
                std::round(param->getNormalized() * maxVal));
        }
        return 0;
    };

    // Helper to get bool parameter as int8
    auto getBoolInt8 = [this](Steinberg::Vst::ParamID id) -> Steinberg::int8 {
        if (auto* param = getParameterObject(id)) {
            return static_cast<Steinberg::int8>(param->getNormalized() >= 0.5 ? 1 : 0);
        }
        return 0;
    };

    // =========================================================================
    // Write version (v6)
    // =========================================================================
    streamer.writeInt32(kPresetVersion);

    // =========================================================================
    // Global parameters (v1)
    // =========================================================================
    streamer.writeFloat(getParamNorm(makeGlobalParamId(GlobalParamType::kGlobalInputGain)));
    streamer.writeFloat(getParamNorm(makeGlobalParamId(GlobalParamType::kGlobalOutputGain)));
    streamer.writeFloat(getParamNorm(makeGlobalParamId(GlobalParamType::kGlobalMix)));

    // =========================================================================
    // Band management (v2)
    // =========================================================================
    // Band count: normalized value (0-1) maps to (1-4)
    int32_t bandCount = static_cast<int32_t>(std::round(
        getParamNorm(makeGlobalParamId(GlobalParamType::kGlobalBandCount)) * 3.0f)) + 1;
    streamer.writeInt32(bandCount);

    // Per-band state (8 bands)
    for (int b = 0; b < kMaxBands; ++b) {
        auto band = static_cast<uint8_t>(b);
        streamer.writeFloat(getFloat(makeBandParamId(band, BandParamType::kBandGain), 0.0f));
        streamer.writeFloat(getFloat(makeBandParamId(band, BandParamType::kBandPan), 0.0f));
        streamer.writeInt8(getBoolInt8(makeBandParamId(band, BandParamType::kBandSolo)));
        streamer.writeInt8(getBoolInt8(makeBandParamId(band, BandParamType::kBandBypass)));
        streamer.writeInt8(getBoolInt8(makeBandParamId(band, BandParamType::kBandMute)));
    }

    // Crossover frequencies (7)
    for (int c = 0; c < kMaxBands - 1; ++c) {
        streamer.writeFloat(getFloat(makeCrossoverParamId(static_cast<uint8_t>(c)), 1000.0f));
    }

    // =========================================================================
    // Sweep system (v4)
    // =========================================================================
    // Sweep Core (6 values)
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepEnable)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepFrequency)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepWidth)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepIntensity)));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepFalloff)));
    streamer.writeInt8(getInt8FromList(makeSweepParamId(SweepParamType::kSweepMorphLink),
                                       kMorphLinkModeCount - 1));

    // LFO (6 values)
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepLFOEnable)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepLFORate)));
    streamer.writeInt8(getInt8FromList(makeSweepParamId(SweepParamType::kSweepLFOWaveform), 5));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepLFODepth)));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepLFOSync)));
    streamer.writeInt8(getInt8FromList(makeSweepParamId(SweepParamType::kSweepLFONoteValue), 14));

    // Envelope (4 values)
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepEnvEnable)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepEnvAttack)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepEnvRelease)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepEnvSensitivity)));

    // Custom Curve breakpoints (default 2 points: (0,0) and (1,1))
    streamer.writeInt32(2);
    streamer.writeFloat(0.0f);
    streamer.writeFloat(0.0f);
    streamer.writeFloat(1.0f);
    streamer.writeFloat(1.0f);

    // =========================================================================
    // Modulation system (v5)
    // =========================================================================

    // LFO 1 (7 values)
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO1Rate)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO1Shape), 5));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO1Phase)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO1Sync)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO1NoteValue), 14));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO1Unipolar)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO1Retrigger)));

    // LFO 2 (7 values)
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO2Rate)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO2Shape), 5));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO2Phase)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO2Sync)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO2NoteValue), 14));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO2Unipolar)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO2Retrigger)));

    // Envelope Follower (4 values)
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kEnvFollowerAttack)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kEnvFollowerRelease)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kEnvFollowerSensitivity)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kEnvFollowerSource), 4));

    // Random (3 values)
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kRandomRate)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kRandomSmoothness)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kRandomSync)));

    // Chaos (3 values)
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kChaosModel), 3));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kChaosSpeed)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kChaosCoupling)));

    // Sample & Hold (3 values)
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kSampleHoldSource), 3));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kSampleHoldRate)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kSampleHoldSlew)));

    // Pitch Follower (4 values)
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerMinHz)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerMaxHz)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerConfidence)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerTrackingSpeed)));

    // Transient (3 values)
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kTransientSensitivity)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kTransientAttack)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kTransientDecay)));

    // Macros (4 x 4 = 16 values)
    constexpr ModParamType macroParams[4][4] = {
        {ModParamType::kMacro1Value, ModParamType::kMacro1Min, ModParamType::kMacro1Max, ModParamType::kMacro1Curve},
        {ModParamType::kMacro2Value, ModParamType::kMacro2Min, ModParamType::kMacro2Max, ModParamType::kMacro2Curve},
        {ModParamType::kMacro3Value, ModParamType::kMacro3Min, ModParamType::kMacro3Max, ModParamType::kMacro3Curve},
        {ModParamType::kMacro4Value, ModParamType::kMacro4Min, ModParamType::kMacro4Max, ModParamType::kMacro4Curve},
    };
    for (const auto& macro : macroParams) {
        streamer.writeFloat(getParamNorm(makeModParamId(macro[0])));
        streamer.writeFloat(getParamNorm(makeModParamId(macro[1])));
        streamer.writeFloat(getParamNorm(makeModParamId(macro[2])));
        streamer.writeInt8(getInt8FromList(makeModParamId(macro[3]), 3));
    }

    // Routing (32 x 4 values)
    for (uint8_t r = 0; r < 32; ++r) {
        streamer.writeInt8(getInt8FromList(makeRoutingParamId(r, 0), 12));
        // Destination: int32 (0 to kTotalDestinations-1)
        auto destNorm = getParamNorm(makeRoutingParamId(r, 1));
        streamer.writeInt32(static_cast<int32_t>(
            std::round(destNorm * static_cast<float>(ModDest::kTotalDestinations - 1))));
        // Amount: float stored as [-1, 1], normalized as (amount + 1) / 2
        float amountNorm = getParamNorm(makeRoutingParamId(r, 2));
        streamer.writeFloat(amountNorm * 2.0f - 1.0f);
        streamer.writeInt8(getInt8FromList(makeRoutingParamId(r, 3), 3));
    }

    // =========================================================================
    // Morph node state (v6)
    // =========================================================================
    for (int b = 0; b < kMaxBands; ++b) {
        auto band = static_cast<uint8_t>(b);

        // Band morph position & config (2 floats + 1 int8 + 1 int8 + 1 float)
        streamer.writeFloat(getParamNorm(makeBandParamId(band, BandParamType::kBandMorphX)));
        streamer.writeFloat(getParamNorm(makeBandParamId(band, BandParamType::kBandMorphY)));
        streamer.writeInt8(getInt8FromList(makeBandParamId(band, BandParamType::kBandMorphMode), 2));

        // ActiveNodes: normalized (0-1) maps to (2-4)
        float activeNodesNorm = getParamNorm(makeBandParamId(band, BandParamType::kBandActiveNodes));
        int activeNodes = static_cast<int>(std::round(activeNodesNorm * 2.0f)) + 2;
        streamer.writeInt8(static_cast<Steinberg::int8>(activeNodes));

        // Morph smoothing: normalized (0-1) maps to (0-500ms)
        float smoothingNorm = getParamNorm(makeBandParamId(band, BandParamType::kBandMorphSmoothing));
        streamer.writeFloat(smoothingNorm * 500.0f);

        // Per-node state (4 nodes x 7 values)
        for (int n = 0; n < kMaxMorphNodes; ++n) {
            auto node = static_cast<uint8_t>(n);

            // Type: int8 (0-25)
            streamer.writeInt8(getInt8FromList(
                makeNodeParamId(band, node, NodeParamType::kNodeType), 25));

            // Drive: float (0-10), normalized = drive / 10
            float driveNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeDrive));
            streamer.writeFloat(driveNorm * 10.0f);

            // Mix: float (0-1)
            streamer.writeFloat(getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeMix)));

            // Tone: float (200-8000 Hz), normalized = (tone - 200) / 7800
            float toneNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeTone));
            streamer.writeFloat(toneNorm * 7800.0f + 200.0f);

            // Bias: float (-1 to 1), normalized = (bias + 1) / 2
            float biasNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeBias));
            streamer.writeFloat(biasNorm * 2.0f - 1.0f);

            // Folds: float (1-12), normalized = (folds - 1) / 11
            float foldsNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeFolds));
            streamer.writeFloat(foldsNorm * 11.0f + 1.0f);

            // BitDepth: float (4-24), normalized = (bitDepth - 4) / 20
            float bitDepthNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeBitDepth));
            streamer.writeFloat(bitDepthNorm * 20.0f + 4.0f);
        }
    }

    return stream;
}

// ==============================================================================
// Preset Loading Helpers
// ==============================================================================

void Controller::editParamWithNotify(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    // Clamp value to valid range
    value = std::max(0.0, std::min(1.0, value));

    // Full edit cycle to notify host of parameter change
    beginEdit(id);
    setParamNormalized(id, value);
    performEdit(id, value);
    endEdit(id);
}

bool Controller::loadComponentStateWithNotify(Steinberg::IBStream* state) {
    // ==========================================================================
    // Load component state with host notification
    // Parses the same binary format as setComponentState(), but calls
    // editParamWithNotify (performEdit) instead of just setParamNormalized,
    // so the host propagates changes to the processor.
    // ==========================================================================

    if (!state) {
        return false;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version
    int32_t version = 0;
    if (!streamer.readInt32(version)) return false;
    if (version < 1) return false;

    // Global parameters
    float inputGain = 0.5f;
    float outputGain = 0.5f;
    float globalMix = 1.0f;
    if (streamer.readFloat(inputGain))
        editParamWithNotify(makeGlobalParamId(GlobalParamType::kGlobalInputGain), inputGain);
    if (streamer.readFloat(outputGain))
        editParamWithNotify(makeGlobalParamId(GlobalParamType::kGlobalOutputGain), outputGain);
    if (streamer.readFloat(globalMix))
        editParamWithNotify(makeGlobalParamId(GlobalParamType::kGlobalMix), globalMix);

    // Band management (v2+)
    if (version >= 2) {
        int32_t bandCount = 4;
        if (streamer.readInt32(bandCount)) {
            int32_t clampedCount = std::clamp(bandCount, 1, 4);
            float normalizedBandCount = static_cast<float>(clampedCount - 1) / 3.0f;
            editParamWithNotify(makeGlobalParamId(GlobalParamType::kGlobalBandCount), normalizedBandCount);
        }

        // v7 and earlier wrote 8 bands; v8+ writes 4
        constexpr int kV7MaxBands = 8;
        const int streamBands = (version <= 7) ? kV7MaxBands : kMaxBands;
        for (int b = 0; b < streamBands; ++b) {
            float gain = 0.0f;
            float pan = 0.0f;
            Steinberg::int8 soloInt = 0;
            Steinberg::int8 bypassInt = 0;
            Steinberg::int8 muteInt = 0;

            streamer.readFloat(gain);
            streamer.readFloat(pan);
            streamer.readInt8(soloInt);
            streamer.readInt8(bypassInt);
            streamer.readInt8(muteInt);

            if (b < kMaxBands) {
                auto* gainParam = getParameterObject(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandGain));
                if (gainParam)
                    editParamWithNotify(gainParam->getInfo().id, gainParam->toNormalized(gain));

                auto* panParam = getParameterObject(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandPan));
                if (panParam)
                    editParamWithNotify(panParam->getInfo().id, panParam->toNormalized(pan));

                editParamWithNotify(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSolo), soloInt != 0 ? 1.0 : 0.0);
                editParamWithNotify(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandBypass), bypassInt != 0 ? 1.0 : 0.0);
                editParamWithNotify(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMute), muteInt != 0 ? 1.0 : 0.0);
            }
        }

        // v7 and earlier wrote 7 crossovers; v8+ writes 3
        const int streamCrossovers = (version <= 7) ? 7 : (kMaxBands - 1);
        for (int i = 0; i < streamCrossovers; ++i) {
            float freq = 1000.0f;
            if (streamer.readFloat(freq)) {
                if (i < kMaxBands - 1) {
                    auto* param = getParameterObject(makeCrossoverParamId(static_cast<uint8_t>(i)));
                    if (param)
                        editParamWithNotify(param->getInfo().id, param->toNormalized(freq));
                }
            }
        }
    }

    // Sweep system (v4+)
    if (version >= 4) {
        Steinberg::int8 sweepEnable = 0;
        float sweepFreqNorm = 0.566f;
        float sweepWidthNorm = 0.286f;
        float sweepIntensityNorm = 0.25f;
        Steinberg::int8 sweepFalloff = 1;
        Steinberg::int8 sweepMorphLink = 0;

        if (streamer.readInt8(sweepEnable))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepEnable), sweepEnable != 0 ? 1.0 : 0.0);
        if (streamer.readFloat(sweepFreqNorm))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepFrequency), sweepFreqNorm);
        if (streamer.readFloat(sweepWidthNorm))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepWidth), sweepWidthNorm);
        if (streamer.readFloat(sweepIntensityNorm))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepIntensity), sweepIntensityNorm);
        if (streamer.readInt8(sweepFalloff))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepFalloff), sweepFalloff != 0 ? 1.0 : 0.0);
        if (streamer.readInt8(sweepMorphLink))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepMorphLink),
                                static_cast<double>(sweepMorphLink) / (kMorphLinkModeCount - 1));

        // LFO
        Steinberg::int8 lfoEnable = 0;
        Steinberg::int8 lfoWaveform = 0;
        Steinberg::int8 lfoSync = 0;
        Steinberg::int8 lfoNoteIndex = 0;
        float lfoRateNorm = 0.606f;
        float lfoDepth = 0.0f;

        if (streamer.readInt8(lfoEnable))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepLFOEnable), lfoEnable != 0 ? 1.0 : 0.0);
        if (streamer.readFloat(lfoRateNorm))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepLFORate), lfoRateNorm);
        if (streamer.readInt8(lfoWaveform))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepLFOWaveform), static_cast<double>(lfoWaveform) / 5.0);
        if (streamer.readFloat(lfoDepth))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepLFODepth), lfoDepth);
        if (streamer.readInt8(lfoSync))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepLFOSync), lfoSync != 0 ? 1.0 : 0.0);
        if (streamer.readInt8(lfoNoteIndex))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepLFONoteValue), static_cast<double>(lfoNoteIndex) / 14.0);

        // Envelope
        Steinberg::int8 envEnable = 0;
        float envAttackNorm = 0.091f;
        float envReleaseNorm = 0.184f;
        float envSensitivity = 0.5f;

        if (streamer.readInt8(envEnable))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepEnvEnable), envEnable != 0 ? 1.0 : 0.0);
        if (streamer.readFloat(envAttackNorm))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepEnvAttack), envAttackNorm);
        if (streamer.readFloat(envReleaseNorm))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepEnvRelease), envReleaseNorm);
        if (streamer.readFloat(envSensitivity))
            editParamWithNotify(makeSweepParamId(SweepParamType::kSweepEnvSensitivity), envSensitivity);

        // Custom curve - skip
        int32_t pointCount = 2;
        if (streamer.readInt32(pointCount)) {
            pointCount = std::clamp(pointCount, 2, 8);
            for (int32_t i = 0; i < pointCount; ++i) {
                float px = 0.0f;
                float py = 0.0f;
                streamer.readFloat(px);
                streamer.readFloat(py);
            }
        }
    }

    // Modulation system (v5+)
    if (version >= 5) {
        // LFO 1
        float lfo1RateNorm = 0.5f;
        if (streamer.readFloat(lfo1RateNorm))
            editParamWithNotify(makeModParamId(ModParamType::kLFO1Rate), lfo1RateNorm);
        Steinberg::int8 lfo1Shape = 0;
        if (streamer.readInt8(lfo1Shape))
            editParamWithNotify(makeModParamId(ModParamType::kLFO1Shape), static_cast<double>(lfo1Shape) / 5.0);
        float lfo1Phase = 0.0f;
        if (streamer.readFloat(lfo1Phase))
            editParamWithNotify(makeModParamId(ModParamType::kLFO1Phase), lfo1Phase);
        Steinberg::int8 lfo1Sync = 0;
        if (streamer.readInt8(lfo1Sync))
            editParamWithNotify(makeModParamId(ModParamType::kLFO1Sync), lfo1Sync != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo1NoteIdx = 0;
        if (streamer.readInt8(lfo1NoteIdx))
            editParamWithNotify(makeModParamId(ModParamType::kLFO1NoteValue), static_cast<double>(lfo1NoteIdx) / 14.0);
        Steinberg::int8 lfo1Unipolar = 0;
        if (streamer.readInt8(lfo1Unipolar))
            editParamWithNotify(makeModParamId(ModParamType::kLFO1Unipolar), lfo1Unipolar != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo1Retrigger = 1;
        if (streamer.readInt8(lfo1Retrigger))
            editParamWithNotify(makeModParamId(ModParamType::kLFO1Retrigger), lfo1Retrigger != 0 ? 1.0 : 0.0);

        // LFO 2
        float lfo2RateNorm = 0.5f;
        if (streamer.readFloat(lfo2RateNorm))
            editParamWithNotify(makeModParamId(ModParamType::kLFO2Rate), lfo2RateNorm);
        Steinberg::int8 lfo2Shape = 0;
        if (streamer.readInt8(lfo2Shape))
            editParamWithNotify(makeModParamId(ModParamType::kLFO2Shape), static_cast<double>(lfo2Shape) / 5.0);
        float lfo2Phase = 0.0f;
        if (streamer.readFloat(lfo2Phase))
            editParamWithNotify(makeModParamId(ModParamType::kLFO2Phase), lfo2Phase);
        Steinberg::int8 lfo2Sync = 0;
        if (streamer.readInt8(lfo2Sync))
            editParamWithNotify(makeModParamId(ModParamType::kLFO2Sync), lfo2Sync != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo2NoteIdx = 0;
        if (streamer.readInt8(lfo2NoteIdx))
            editParamWithNotify(makeModParamId(ModParamType::kLFO2NoteValue), static_cast<double>(lfo2NoteIdx) / 14.0);
        Steinberg::int8 lfo2Unipolar = 0;
        if (streamer.readInt8(lfo2Unipolar))
            editParamWithNotify(makeModParamId(ModParamType::kLFO2Unipolar), lfo2Unipolar != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo2Retrigger = 1;
        if (streamer.readInt8(lfo2Retrigger))
            editParamWithNotify(makeModParamId(ModParamType::kLFO2Retrigger), lfo2Retrigger != 0 ? 1.0 : 0.0);

        // Envelope Follower
        float envAttackNorm = 0.0f;
        if (streamer.readFloat(envAttackNorm))
            editParamWithNotify(makeModParamId(ModParamType::kEnvFollowerAttack), envAttackNorm);
        float envReleaseNorm = 0.0f;
        if (streamer.readFloat(envReleaseNorm))
            editParamWithNotify(makeModParamId(ModParamType::kEnvFollowerRelease), envReleaseNorm);
        float envSensitivity = 0.5f;
        if (streamer.readFloat(envSensitivity))
            editParamWithNotify(makeModParamId(ModParamType::kEnvFollowerSensitivity), envSensitivity);
        Steinberg::int8 envSource = 0;
        if (streamer.readInt8(envSource))
            editParamWithNotify(makeModParamId(ModParamType::kEnvFollowerSource), static_cast<double>(envSource) / 4.0);

        // Random
        float randomRateNorm = 0.0f;
        if (streamer.readFloat(randomRateNorm))
            editParamWithNotify(makeModParamId(ModParamType::kRandomRate), randomRateNorm);
        float randomSmoothness = 0.0f;
        if (streamer.readFloat(randomSmoothness))
            editParamWithNotify(makeModParamId(ModParamType::kRandomSmoothness), randomSmoothness);
        Steinberg::int8 randomSync = 0;
        if (streamer.readInt8(randomSync))
            editParamWithNotify(makeModParamId(ModParamType::kRandomSync), randomSync != 0 ? 1.0 : 0.0);

        // Chaos
        Steinberg::int8 chaosModel = 0;
        if (streamer.readInt8(chaosModel))
            editParamWithNotify(makeModParamId(ModParamType::kChaosModel), static_cast<double>(chaosModel) / 3.0);
        float chaosSpeedNorm = 0.0f;
        if (streamer.readFloat(chaosSpeedNorm))
            editParamWithNotify(makeModParamId(ModParamType::kChaosSpeed), chaosSpeedNorm);
        float chaosCoupling = 0.0f;
        if (streamer.readFloat(chaosCoupling))
            editParamWithNotify(makeModParamId(ModParamType::kChaosCoupling), chaosCoupling);

        // Sample & Hold
        Steinberg::int8 shSource = 0;
        if (streamer.readInt8(shSource))
            editParamWithNotify(makeModParamId(ModParamType::kSampleHoldSource), static_cast<double>(shSource) / 3.0);
        float shRateNorm = 0.0f;
        if (streamer.readFloat(shRateNorm))
            editParamWithNotify(makeModParamId(ModParamType::kSampleHoldRate), shRateNorm);
        float shSlewNorm = 0.0f;
        if (streamer.readFloat(shSlewNorm))
            editParamWithNotify(makeModParamId(ModParamType::kSampleHoldSlew), shSlewNorm);

        // Pitch Follower
        float pitchMinNorm = 0.0f;
        if (streamer.readFloat(pitchMinNorm))
            editParamWithNotify(makeModParamId(ModParamType::kPitchFollowerMinHz), pitchMinNorm);
        float pitchMaxNorm = 0.0f;
        if (streamer.readFloat(pitchMaxNorm))
            editParamWithNotify(makeModParamId(ModParamType::kPitchFollowerMaxHz), pitchMaxNorm);
        float pitchConfidence = 0.5f;
        if (streamer.readFloat(pitchConfidence))
            editParamWithNotify(makeModParamId(ModParamType::kPitchFollowerConfidence), pitchConfidence);
        float pitchTrackNorm = 0.0f;
        if (streamer.readFloat(pitchTrackNorm))
            editParamWithNotify(makeModParamId(ModParamType::kPitchFollowerTrackingSpeed), pitchTrackNorm);

        // Transient
        float transSensitivity = 0.5f;
        if (streamer.readFloat(transSensitivity))
            editParamWithNotify(makeModParamId(ModParamType::kTransientSensitivity), transSensitivity);
        float transAttackNorm = 0.0f;
        if (streamer.readFloat(transAttackNorm))
            editParamWithNotify(makeModParamId(ModParamType::kTransientAttack), transAttackNorm);
        float transDecayNorm = 0.0f;
        if (streamer.readFloat(transDecayNorm))
            editParamWithNotify(makeModParamId(ModParamType::kTransientDecay), transDecayNorm);

        // Macros
        constexpr ModParamType macroParams[4][4] = {
            {ModParamType::kMacro1Value, ModParamType::kMacro1Min, ModParamType::kMacro1Max, ModParamType::kMacro1Curve},
            {ModParamType::kMacro2Value, ModParamType::kMacro2Min, ModParamType::kMacro2Max, ModParamType::kMacro2Curve},
            {ModParamType::kMacro3Value, ModParamType::kMacro3Min, ModParamType::kMacro3Max, ModParamType::kMacro3Curve},
            {ModParamType::kMacro4Value, ModParamType::kMacro4Min, ModParamType::kMacro4Max, ModParamType::kMacro4Curve},
        };
        for (const auto& macro : macroParams) {
            float macroValue = 0.0f;
            if (streamer.readFloat(macroValue))
                editParamWithNotify(makeModParamId(macro[0]), macroValue);
            float macroMin = 0.0f;
            if (streamer.readFloat(macroMin))
                editParamWithNotify(makeModParamId(macro[1]), macroMin);
            float macroMax = 1.0f;
            if (streamer.readFloat(macroMax))
                editParamWithNotify(makeModParamId(macro[2]), macroMax);
            Steinberg::int8 macroCurve = 0;
            if (streamer.readInt8(macroCurve))
                editParamWithNotify(makeModParamId(macro[3]), static_cast<double>(macroCurve) / 3.0);
        }

        // Routing (32 x 4 values)
        for (uint8_t r = 0; r < 32; ++r) {
            Steinberg::int8 source = 0;
            if (streamer.readInt8(source))
                editParamWithNotify(makeRoutingParamId(r, 0), static_cast<double>(source) / 12.0);
            int32_t dest = 0;
            if (streamer.readInt32(dest))
                editParamWithNotify(makeRoutingParamId(r, 1),
                                   static_cast<double>(std::clamp(dest, 0, static_cast<int32_t>(ModDest::kTotalDestinations - 1)))
                                   / static_cast<double>(ModDest::kTotalDestinations - 1));
            float amount = 0.0f;
            if (streamer.readFloat(amount))
                editParamWithNotify(makeRoutingParamId(r, 2), static_cast<double>(amount + 1.0f) / 2.0);
            Steinberg::int8 curve = 0;
            if (streamer.readInt8(curve))
                editParamWithNotify(makeRoutingParamId(r, 3), static_cast<double>(curve) / 3.0);
        }
    }

    // Morph node state (v6+)
    if (version >= 6) {
        // v7 and earlier wrote 8 bands of morph state; v8+ writes 4
        constexpr int kV7MorphBands = 8;
        const int streamMorphBands = (version <= 7) ? kV7MorphBands : kMaxBands;
        for (int b = 0; b < streamMorphBands; ++b) {
            const auto band = static_cast<uint8_t>(b);

            float morphX = 0.5f;
            float morphY = 0.5f;
            Steinberg::int8 morphMode = 0;
            auto activeNodes = static_cast<Steinberg::int8>(kDefaultActiveNodes);
            float morphSmoothing = 0.0f;

            bool readMorphX = streamer.readFloat(morphX);
            bool readMorphY = streamer.readFloat(morphY);
            bool readMorphMode = streamer.readInt8(morphMode);
            bool readActiveNodes = streamer.readInt8(activeNodes);
            bool readMorphSmoothing = streamer.readFloat(morphSmoothing);

            if (b < kMaxBands) {
                if (readMorphX)
                    editParamWithNotify(makeBandParamId(band, BandParamType::kBandMorphX), static_cast<double>(morphX));
                if (readMorphY)
                    editParamWithNotify(makeBandParamId(band, BandParamType::kBandMorphY), static_cast<double>(morphY));
                if (readMorphMode)
                    editParamWithNotify(makeBandParamId(band, BandParamType::kBandMorphMode), static_cast<double>(morphMode) / 2.0);
                if (readActiveNodes) {
                    int count = std::clamp(static_cast<int>(activeNodes), kMinActiveNodes, kMaxMorphNodes);
                    editParamWithNotify(makeBandParamId(band, BandParamType::kBandActiveNodes), static_cast<double>(count - 2) / 2.0);
                }
                if (readMorphSmoothing)
                    editParamWithNotify(makeBandParamId(band, BandParamType::kBandMorphSmoothing), static_cast<double>(morphSmoothing) / 500.0);
            }

            for (int n = 0; n < kMaxMorphNodes; ++n) {
                const auto node = static_cast<uint8_t>(n);

                Steinberg::int8 nodeType = 0;
                float drive = 1.0f, mix = 1.0f, tone = 4000.0f;
                float bias = 0.0f, folds = 1.0f, bitDepth = 16.0f;

                bool rType = streamer.readInt8(nodeType);
                bool rDrive = streamer.readFloat(drive);
                bool rMix = streamer.readFloat(mix);
                bool rTone = streamer.readFloat(tone);
                bool rBias = streamer.readFloat(bias);
                bool rFolds = streamer.readFloat(folds);
                bool rBitDepth = streamer.readFloat(bitDepth);

                if (b < kMaxBands) {
                    if (rType) editParamWithNotify(makeNodeParamId(band, node, NodeParamType::kNodeType), static_cast<double>(nodeType) / 25.0);
                    if (rDrive) editParamWithNotify(makeNodeParamId(band, node, NodeParamType::kNodeDrive), static_cast<double>(drive) / 10.0);
                    if (rMix) editParamWithNotify(makeNodeParamId(band, node, NodeParamType::kNodeMix), static_cast<double>(mix));
                    if (rTone) editParamWithNotify(makeNodeParamId(band, node, NodeParamType::kNodeTone), static_cast<double>(tone - 200.0f) / 7800.0);
                    if (rBias) editParamWithNotify(makeNodeParamId(band, node, NodeParamType::kNodeBias), static_cast<double>(bias + 1.0f) / 2.0);
                    if (rFolds) editParamWithNotify(makeNodeParamId(band, node, NodeParamType::kNodeFolds), static_cast<double>(folds - 1.0f) / 11.0);
                    if (rBitDepth) editParamWithNotify(makeNodeParamId(band, node, NodeParamType::kNodeBitDepth), static_cast<double>(bitDepth - 4.0f) / 20.0);
                }
            }
        }
    }

    return true;
}

} // namespace Disrumpo
