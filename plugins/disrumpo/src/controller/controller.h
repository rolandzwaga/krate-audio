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
#include "vstgui/plugin-bindings/vst3editor.h"

#include <array>
#include <memory>

namespace Disrumpo {

// Forward declarations
class SpectrumDisplay;

// ==============================================================================
// Controller Class
// ==============================================================================

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate {
public:
    Controller() = default;
    ~Controller() override;

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
    // ==========================================================================
    // Parameter Registration Helpers
    // ==========================================================================

    /// Register global parameters (Input Gain, Output Gain, Mix, Band Count, Oversample)
    void registerGlobalParams();

    /// Register sweep parameters (Enable, Frequency, Width, Intensity, MorphLink, Falloff)
    void registerSweepParams();

    /// Register modulation parameters (placeholder for Week 9)
    void registerModulationParams();

    /// Register per-band parameters for all 8 bands
    void registerBandParams();

    /// Register per-node parameters for all 8 bands x 4 nodes
    void registerNodeParams();

    // ==========================================================================
    // UI State
    // ==========================================================================

    /// Active editor instance (nullptr when editor is closed)
    VSTGUI::VST3Editor* activeEditor_ = nullptr;

    /// Pointer to the SpectrumDisplay instance (for listener registration)
    SpectrumDisplay* spectrumDisplay_ = nullptr;

    // ==========================================================================
    // Visibility Controllers (FR-021, FR-022, FR-025)
    // ==========================================================================
    // Band visibility controllers - show/hide band containers based on Band Count
    // Using IDependent mechanism for thread-safe parameter observation

    static constexpr int kMaxBands = 8;
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
};

} // namespace Disrumpo
