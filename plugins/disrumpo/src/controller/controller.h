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
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "preset/preset_manager.h"
#include "platform/accessibility_helper.h"
#include "midi/midi_cc_manager.h"
#include "controller/keyboard_shortcut_handler.h"

#include <array>
#include <functional>
#include <memory>

namespace Krate::Plugins {
class PresetBrowserView;
class SavePresetDialogView;
}

namespace Disrumpo {

// Forward declarations
class SpectrumDisplay;
class DynamicNodeSelector;
class MorphPad;
class SweepIndicator;

// ==============================================================================
// Controller Class
// ==============================================================================

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate,
                   public Steinberg::Vst::IMidiMapping {
public:
    Controller() = default;
    ~Controller() override;  // Defined in cpp to allow unique_ptr with forward declaration

    // ===========================================================================
    // IPluginBase
    // ===========================================================================

    /// Called when the controller is first loaded
    /// Registers all ~450 parameters for the Disrumpo plugin
    Steinberg::tresult PLUGIN_API initialize(FUnknown* context) override;

    /// Called when the controller is unloaded
    Steinberg::tresult PLUGIN_API terminate() override;

    // ===========================================================================
    // IEditController
    // ===========================================================================

    /// Receive processor state and synchronize controller
    /// FR-026: Sync from processor state
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state) override;

    /// Save controller-specific state (UI settings, etc.)
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    /// Restore controller-specific state
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;

    /// Create the plugin editor (UI)
    /// Returns VST3Editor with editor.uidesc
    Steinberg::IPlugView* PLUGIN_API createView(
        Steinberg::FIDString name) override;

    /// Convert normalized parameter value to string for display
    /// FR-027: Custom formatting for Drive, Mix, Gain, Type, Pan
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
    // VST3EditorDelegate (VSTGUI)
    // ===========================================================================

    /// Create custom views based on view name
    /// FR-013: Creates SpectrumDisplay, MorphPad (placeholder)
    VSTGUI::CView* createCustomView(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;

    /// Called when the editor is opened
    /// FR-023: Creates visibility controllers
    void didOpen(VSTGUI::VST3Editor* editor) override;

    /// Called when the editor is about to close
    /// FR-024: Properly deactivates and cleans up visibility controllers
    void willClose(VSTGUI::VST3Editor* editor) override;

    /// Create context menu for MIDI Learn (Spec 012 FR-031)
    VSTGUI::COptionMenu* createContextMenu(
        const VSTGUI::CPoint& pos,
        VSTGUI::VST3Editor* editor) override;

    /// Find parameter under mouse position (Spec 012 FR-031)
    bool findParameter(
        const VSTGUI::CPoint& pos,
        Steinberg::Vst::ParamID& paramID,
        VSTGUI::VST3Editor* editor) override;

    // ===========================================================================
    // Preset Browser (Spec 010)
    // ===========================================================================

    /// Open the preset browser modal
    void openPresetBrowser();

    /// Close the preset browser modal
    void closePresetBrowser();

    /// Open standalone save preset dialog (quick save from main UI)
    void openSavePresetDialog();

    /// Get the preset manager instance
    Krate::Plugins::PresetManager* getPresetManager() const { return presetManager_.get(); }

    /// Create a memory stream containing the current component state
    /// Used for preset saving - serializes controller's parameter values
    /// in the same format as Processor::getState()
    /// @return New MemoryStream (caller must release), or nullptr on failure
    Steinberg::MemoryStream* createComponentStateStream();

    // ===========================================================================
    // IMidiMapping (FR-028, FR-029)
    // ===========================================================================

    /// Map MIDI CC to parameter ID for MIDI CC control of sweep frequency
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::CtrlNumber midiControllerNumber,
        Steinberg::Vst::ParamID& id) override;

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
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(EditController)

    DELEGATE_REFCOUNT(EditController)

private:
    // ==========================================================================
    // Parameter Registration Helpers
    // ==========================================================================

    /// Register global parameters (Input Gain, Output Gain, Mix, Band Count, Oversample)
    void registerGlobalParams();

    /// Register sweep parameters (Enable, Frequency, Width, Intensity, MorphLink, Falloff)
    void registerSweepParams();

    /// Register modulation parameters (placeholder for Week 9)
    void registerModulationParams();

    /// Register per-band parameters for all 4 bands
    void registerBandParams();

    /// Register per-node parameters for all 4 bands x 4 nodes
    void registerNodeParams();

    // ==========================================================================
    // UI State
    // ==========================================================================

    /// Active editor instance (nullptr when editor is closed)
    VSTGUI::VST3Editor* activeEditor_ = nullptr;

    /// Pointer to the SpectrumDisplay instance (for listener registration)
    SpectrumDisplay* spectrumDisplay_ = nullptr;

    /// Pointer to the SweepIndicator instance (for sweep visualization)
    SweepIndicator* sweepIndicator_ = nullptr;

    // ==========================================================================
    // Visibility Controllers (FR-021, FR-022, FR-025)
    // ==========================================================================
    // Band visibility controllers - show/hide band containers based on Band Count
    // Using IDependent mechanism for thread-safe parameter observation

    static constexpr int kMaxBands = 4;
    std::array<Steinberg::IPtr<Steinberg::FObject>, kMaxBands> bandVisibilityControllers_;

    // ==========================================================================
    // Expanded State Visibility Controllers (T079, FR-015)
    // ==========================================================================
    // Expand/collapse visibility controllers - show/hide BandStripExpanded based on Band*Expanded
    std::array<Steinberg::IPtr<Steinberg::FObject>, kMaxBands> expandedVisibilityControllers_;

    // ==========================================================================
    // Morph-Sweep Link Controller (T159-T161, US8)
    // ==========================================================================
    // Updates morph position based on sweep frequency and link mode settings
    Steinberg::IPtr<Steinberg::FObject> morphSweepLinkController_;

    // ==========================================================================
    // Node Selection Controllers (US7 FR-024/FR-025)
    // ==========================================================================
    // Updates DisplayedType proxy when SelectedNode changes
    std::array<Steinberg::IPtr<Steinberg::FObject>, kMaxBands> nodeSelectionControllers_;

    // ==========================================================================
    // Dynamic Node Selectors (US6)
    // ==========================================================================
    // Custom CSegmentButton controls that show/hide A/B/C/D based on ActiveNodes
    std::array<DynamicNodeSelector*, kMaxBands> dynamicNodeSelectors_{};

    // ==========================================================================
    // MorphPads (US6)
    // ==========================================================================
    // MorphPad controls that show/hide nodes based on ActiveNodes
    std::array<MorphPad*, kMaxBands> morphPads_{};

    // ==========================================================================
    // Sweep Visualization (FR-047, FR-049)
    // ==========================================================================
    // Timer for consistent sweep indicator redraws (~30fps)
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> sweepVisualizationTimer_;

    // IDependent controller for sweep output parameter -> UI updates
    Steinberg::IPtr<Steinberg::FObject> sweepVisualizationController_;

    // ==========================================================================
    // Custom Curve Visibility (FR-039a)
    // ==========================================================================
    Steinberg::IPtr<Steinberg::FObject> customCurveVisController_;

    // ==========================================================================
    // Band Count Display Controller
    // ==========================================================================
    // Updates SpectrumDisplay band count when Bands dropdown changes
    Steinberg::IPtr<Steinberg::FObject> bandCountDisplayController_;

    // ==========================================================================
    // Window Size State (Spec 012 US2)
    // ==========================================================================
    // Persisted in controller state for session restore (FR-023)
    double lastWindowWidth_ = 1000.0;   // FR-020: Default 1000px
    double lastWindowHeight_ = 600.0;   // FR-020: Default 600px (5:3 ratio)

    // ==========================================================================
    // Keyboard Shortcut Handler (Spec 012 US4)
    // ==========================================================================
    std::unique_ptr<KeyboardShortcutHandler> keyboardHandler_;

    // ==========================================================================
    // MIDI CC Manager (Spec 012 US5)
    // ==========================================================================
    std::unique_ptr<Krate::Plugins::MidiCCManager> midiCCManager_;

    // ==========================================================================
    // Modulation Panel Visibility Controller (Spec 012 US3)
    // ==========================================================================
    Steinberg::IPtr<Steinberg::FObject> modPanelVisController_;

    // ==========================================================================
    // Accessibility Preferences Cache (Spec 012 US7)
    // ==========================================================================
    Krate::Plugins::AccessibilityPreferences accessibilityPrefs_;

    // ==========================================================================
    // MIDI CC Mapping (FR-028, FR-029) - Legacy
    // ==========================================================================
    // Stored MIDI CC number (0-127, or 128 for none)
    int assignedMidiCC_ = 128;

    // ==========================================================================
    // Preset Browser (Spec 010)
    // ==========================================================================

    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;  // Owned by frame
    Krate::Plugins::SavePresetDialogView* savePresetDialogView_ = nullptr;  // Owned by frame

    // ==========================================================================
    // Preset Loading Helpers
    // ==========================================================================

    /// Edit a parameter with full notification (beginEdit + setParamNormalized + performEdit + endEdit)
    /// Used when loading presets to notify the host of parameter changes
    void editParamWithNotify(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

    /// Load component state from stream with host notification
    /// Same parsing as setComponentState(), but calls performEdit to propagate changes to processor
    /// @param state Stream containing component state in Processor::getState() format
    /// @return true on success
    bool loadComponentStateWithNotify(Steinberg::IBStream* state);
};

} // namespace Disrumpo
