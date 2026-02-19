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
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cviewcontainer.h"
#include "preset/preset_manager.h"

#include <array>
#include <atomic>
#include <memory>

namespace Krate::Plugins {
class PresetBrowserView;
class SavePresetDialogView;
class StepPatternEditor;
class XYMorphPad;
class ADSRDisplay;
class ModMatrixGrid;
class ModRingIndicator;
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

    /// Wire ModMatrixGrid instance with callbacks for parameter editing
    void wireModMatrixGrid(Krate::Plugins::ModMatrixGrid* grid);

    /// Sync ModMatrixGrid route data from current parameter state
    void syncModMatrixGrid();

    /// Push all global route slot parameters from grid state to VST params
    void pushAllGlobalRouteParams();

    /// Wire a ModRingIndicator instance found in verifyView()
    void wireModRingIndicator(Krate::Plugins::ModRingIndicator* indicator);

    /// Rebuild ArcInfo lists for all ModRingIndicator instances from current params
    void rebuildRingIndicators();

    /// Select a modulation route for cross-component communication (FR-027)
    void selectModulationRoute(int sourceIndex, int destIndex);

    /// Toggle FX detail panel visibility (expand one, collapse others)
    void toggleFxDetail(int panelIndex);

    /// Toggle envelope panel expand/collapse (resize + visibility)
    void toggleEnvExpand(int panelIndex);

    /// Toggle settings drawer open/closed with animation
    void toggleSettingsDrawer();

    // ==========================================================================
    // UI State
    // ==========================================================================

    VSTGUI::VST3Editor* activeEditor_ = nullptr;
    Krate::Plugins::StepPatternEditor* stepPatternEditor_ = nullptr;
    VSTGUI::COptionMenu* presetDropdown_ = nullptr;
    Krate::Plugins::XYMorphPad* xyMorphPad_ = nullptr;
    Krate::Plugins::ModMatrixGrid* modMatrixGrid_ = nullptr;
    bool suppressModMatrixSync_ = false;  // Reentrancy guard for grid→param→sync loop

    /// ModRingIndicator pointers indexed by voice destination index (up to 7)
    static constexpr int kMaxRingIndicators = 7;
    std::array<Krate::Plugins::ModRingIndicator*, 7> ringIndicators_{};

    Krate::Plugins::ADSRDisplay* ampEnvDisplay_ = nullptr;
    Krate::Plugins::ADSRDisplay* filterEnvDisplay_ = nullptr;
    Krate::Plugins::ADSRDisplay* modEnvDisplay_ = nullptr;

    /// Euclidean controls container (regen, hits, rotate) - hidden when Euclidean mode is off
    VSTGUI::CView* euclideanControlsGroup_ = nullptr;

    /// LFO Rate groups - hidden when tempo sync is active
    VSTGUI::CView* lfo1RateGroup_ = nullptr;
    VSTGUI::CView* lfo2RateGroup_ = nullptr;
    /// LFO Note Value groups - visible when tempo sync is active
    VSTGUI::CView* lfo1NoteValueGroup_ = nullptr;
    VSTGUI::CView* lfo2NoteValueGroup_ = nullptr;

    /// Chaos Rate/NoteValue groups - toggled by sync state
    VSTGUI::CView* chaosRateGroup_ = nullptr;
    VSTGUI::CView* chaosNoteValueGroup_ = nullptr;

    /// S&H Rate/NoteValue groups - toggled by sync state
    VSTGUI::CView* shRateGroup_ = nullptr;
    VSTGUI::CView* shNoteValueGroup_ = nullptr;

    /// Random Rate/NoteValue groups - toggled by sync state
    VSTGUI::CView* randomRateGroup_ = nullptr;
    VSTGUI::CView* randomNoteValueGroup_ = nullptr;

    /// Delay Time/NoteValue groups - toggled by sync state
    VSTGUI::CView* delayTimeGroup_ = nullptr;
    VSTGUI::CView* delayNoteValueGroup_ = nullptr;

    /// Phaser Rate/NoteValue groups - toggled by sync state
    VSTGUI::CView* phaserRateGroup_ = nullptr;
    VSTGUI::CView* phaserNoteValueGroup_ = nullptr;

    /// Trance Gate Rate/NoteValue groups - toggled by sync state
    VSTGUI::CView* tranceGateRateGroup_ = nullptr;
    VSTGUI::CView* tranceGateNoteValueGroup_ = nullptr;

    /// Poly/Mono visibility groups - toggled by voice mode
    VSTGUI::CView* polyGroup_ = nullptr;
    VSTGUI::CView* monoGroup_ = nullptr;

    // FX detail panel expand/collapse state
    VSTGUI::CViewContainer* fxDetailDelay_ = nullptr;
    VSTGUI::CViewContainer* fxDetailReverb_ = nullptr;
    VSTGUI::CViewContainer* fxDetailPhaser_ = nullptr;
    VSTGUI::CViewContainer* fxDetailHarmonizer_ = nullptr;
    VSTGUI::CControl* fxExpandDelayChevron_ = nullptr;
    VSTGUI::CControl* fxExpandReverbChevron_ = nullptr;
    VSTGUI::CControl* fxExpandPhaserChevron_ = nullptr;
    VSTGUI::CControl* fxExpandHarmonizerChevron_ = nullptr;
    int expandedFxPanel_ = -1;  // -1=none, 0=delay, 1=reverb, 2=phaser, 3=harmonizer

    // Harmonizer voice row containers (for dimming based on NumVoices)
    std::array<VSTGUI::CViewContainer*, 4> harmonizerVoiceRows_{};

    // Envelope expand/collapse state
    VSTGUI::CViewContainer* envGroupAmp_ = nullptr;
    VSTGUI::CViewContainer* envGroupFilter_ = nullptr;
    VSTGUI::CViewContainer* envGroupMod_ = nullptr;
    int expandedEnvPanel_ = -1;  // -1=none, 0=amp, 1=filter, 2=mod

    // Settings drawer state
    VSTGUI::CViewContainer* settingsDrawer_ = nullptr;
    VSTGUI::CView* settingsOverlay_ = nullptr;
    VSTGUI::CControl* gearButton_ = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> settingsAnimTimer_;
    bool settingsDrawerOpen_ = false;
    float settingsDrawerProgress_ = 0.0f;  // 0.0 = closed, 1.0 = open
    bool settingsDrawerTargetOpen_ = false;

    // Playback position shared from processor via IMessage pointer
    std::atomic<int>* tranceGatePlaybackStepPtr_ = nullptr;
    std::atomic<bool>* isTransportPlayingPtr_ = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> playbackPollTimer_;

    // Morph pad modulated position shared from processor via IMessage pointer
    std::atomic<float>* modulatedMorphXPtr_ = nullptr;
    std::atomic<float>* modulatedMorphYPtr_ = nullptr;

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
