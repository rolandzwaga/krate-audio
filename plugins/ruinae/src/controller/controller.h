#pragma once

// ==============================================================================
// Edit Controller
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation
// - This is the Controller component (IEditController)
// - MUST be completely separate from Processor
// - Runs on UI thread, NOT audio thread
//
// Constitution Principle V: VSTGUI Development
// - Use UIDescription for UI layout
// - Implement VST3EditorDelegate for custom views
// - UI thread MUST NEVER directly access audio data
// ==============================================================================

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "preset/preset_manager.h"

#include <atomic>
#include <memory>

namespace Krate::Plugins {
class PresetBrowserView;
class SavePresetDialogView;
class StepPatternEditor;
class XYMorphPad;
class ADSRDisplay;
}

namespace Ruinae {

// ==============================================================================
// Controller Class
// ==============================================================================

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate,
                   public VSTGUI::IControlListener {
public:
    Controller() = default;
    ~Controller() override;

    // ===========================================================================
    // IPluginBase
    // ===========================================================================

    /// Called when the controller is first loaded
    Steinberg::tresult PLUGIN_API initialize(FUnknown* context) override;

    /// Called when the controller is unloaded
    Steinberg::tresult PLUGIN_API terminate() override;

    // ===========================================================================
    // IEditController
    // ===========================================================================

    /// Receive processor state and synchronize controller
    /// Constitution Principle I: Controller syncs TO processor state
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state) override;

    /// Save controller-specific state (UI settings, etc.)
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    /// Restore controller-specific state
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;

    /// Create the plugin editor (UI)
    /// Constitution Principle V: Use VSTGUI UIDescription
    Steinberg::IPlugView* PLUGIN_API createView(
        Steinberg::FIDString name) override;

    /// Convert normalized parameter value to string for display
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;

    /// Convert string to normalized parameter value
    Steinberg::tresult PLUGIN_API getParamValueByString(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::TChar* string,
        Steinberg::Vst::ParamValue& valueNormalized) override;

    // ===========================================================================
    // IEditController (parameter sync)
    // ===========================================================================

    /// Push host parameter changes to custom views (StepPatternEditor)
    Steinberg::tresult PLUGIN_API setParamNormalized(
        Steinberg::Vst::ParamID tag,
        Steinberg::Vst::ParamValue value) override;

    /// Receive IMessage from processor (playback position, transport state)
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message) override;

    // ===========================================================================
    // VST3EditorDelegate (VSTGUI)
    // ===========================================================================

    /// Called when the editor is opened
    void didOpen(VSTGUI::VST3Editor* editor) override;

    /// Called when the editor is about to close
    void willClose(VSTGUI::VST3Editor* editor) override;

    /// Called for each view created by UIDescription - wire custom controls
    VSTGUI::CView* verifyView(VSTGUI::CView* view,
                               const VSTGUI::UIAttributes& attributes,
                               const VSTGUI::IUIDescription* description,
                               VSTGUI::VST3Editor* editor) override;

    // ===========================================================================
    // IControlListener (for action buttons)
    // ===========================================================================

    /// Handle action button clicks (presets, transforms, Euclidean regen)
    void valueChanged(VSTGUI::CControl* control) override;

    // ===========================================================================
    // Factory
    // ===========================================================================

    static FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    // ===========================================================================
    // Interface Support
    // ===========================================================================

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IEditController)
        DEF_INTERFACE(Steinberg::Vst::IEditController2)
    END_DEFINE_INTERFACES(EditController)

    DELEGATE_REFCOUNT(EditController)

private:
    /// Wire an ADSRDisplay instance based on its control-tag
    void wireAdsrDisplay(Krate::Plugins::ADSRDisplay* display);

    /// Sync an ADSRDisplay from current parameter state
    void syncAdsrDisplay(Krate::Plugins::ADSRDisplay* display,
                         uint32_t adsrBaseId, uint32_t curveBaseId,
                         uint32_t bezierEnabledId, uint32_t bezierBaseId);

    /// Push a single parameter change to an ADSRDisplay if it matches
    static void syncAdsrParamToDisplay(Steinberg::Vst::ParamID tag,
                                        Steinberg::Vst::ParamValue value,
                                        Krate::Plugins::ADSRDisplay* display,
                                        uint32_t adsrBaseId, uint32_t curveBaseId,
                                        uint32_t bezierEnabledId, uint32_t bezierBaseId);

    /// Wire envelope display playback state pointers to ADSRDisplay instances
    void wireEnvDisplayPlayback();
    // ==========================================================================
    // UI State
    // ==========================================================================

    VSTGUI::VST3Editor* activeEditor_ = nullptr;
    Krate::Plugins::StepPatternEditor* stepPatternEditor_ = nullptr;
    Krate::Plugins::XYMorphPad* xyMorphPad_ = nullptr;
    Krate::Plugins::ADSRDisplay* ampEnvDisplay_ = nullptr;
    Krate::Plugins::ADSRDisplay* filterEnvDisplay_ = nullptr;
    Krate::Plugins::ADSRDisplay* modEnvDisplay_ = nullptr;

    // Playback position shared from processor via IMessage pointer
    std::atomic<int>* tranceGatePlaybackStepPtr_ = nullptr;
    std::atomic<bool>* isTransportPlayingPtr_ = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> playbackPollTimer_;

    // Envelope display state shared from processor via IMessage pointer
    std::atomic<float>* ampEnvOutputPtr_ = nullptr;
    std::atomic<int>* ampEnvStagePtr_ = nullptr;
    std::atomic<float>* filterEnvOutputPtr_ = nullptr;
    std::atomic<int>* filterEnvStagePtr_ = nullptr;
    std::atomic<float>* modEnvOutputPtr_ = nullptr;
    std::atomic<int>* modEnvStagePtr_ = nullptr;
    std::atomic<bool>* envVoiceActivePtr_ = nullptr;

    // ==========================================================================
    // Preset Browser
    // ==========================================================================

    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
};

} // namespace Ruinae
