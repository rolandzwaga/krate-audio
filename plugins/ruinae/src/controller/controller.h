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
#include "parameters/arpeggiator_params.h"
#include "preset/preset_manager.h"
#include "ui/arp_lane.h"

#include <array>
#include <atomic>
#include <memory>

namespace Krate::Plugins {
// ClipboardLaneType and LaneClipboard are now defined in arp_lane.h
class PresetBrowserView;
class SavePresetDialogView;
class StepPatternEditor;
class ArpLaneEditor;
class ArpLaneContainer;
class ArpModifierLane;
class ArpConditionLane;
class XYMorphPad;
class ADSRDisplay;
class ModMatrixGrid;
class ModRingIndicator;
class EuclideanDotDisplay;
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

    /// Create custom views (preset browser/save buttons)
    VSTGUI::CView* createCustomView(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;

    // ===========================================================================
    // Preset Browser (Spec 083)
    // ===========================================================================

    /// Open the preset browser overlay
    void openPresetBrowser();

    /// Close the preset browser overlay
    void closePresetBrowser();

    /// Open the save preset dialog overlay
    void openSavePresetDialog();

    /// Get the preset manager instance (for custom view buttons)
    Krate::Plugins::PresetManager* getPresetManager() { return presetManager_.get(); }

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

    /// Null out cached view pointers for the old tab before UIViewSwitchContainer
    /// destroys them. Called from setParamNormalized() when kMainTabTag changes.
    void onTabChanged(int newTab);

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

    /// Euclidean circular dot display (081-interaction-polish US5)
    Krate::Plugins::EuclideanDotDisplay* euclideanDotDisplay_ = nullptr;

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

    /// Arp Rate/NoteValue groups - toggled by sync state (FR-016)
    VSTGUI::CViewContainer* arpRateGroup_ = nullptr;
    VSTGUI::CViewContainer* arpNoteValueGroup_ = nullptr;

    /// Arp lane container and lane editors (079-layout-framework + 080-specialized-lane-types)
    Krate::Plugins::ArpLaneContainer* arpLaneContainer_ = nullptr;
    Krate::Plugins::ArpLaneEditor* velocityLane_ = nullptr;
    Krate::Plugins::ArpLaneEditor* gateLane_ = nullptr;
    Krate::Plugins::ArpLaneEditor* pitchLane_ = nullptr;
    Krate::Plugins::ArpLaneEditor* ratchetLane_ = nullptr;
    Krate::Plugins::ArpModifierLane* modifierLane_ = nullptr;
    Krate::Plugins::ArpConditionLane* conditionLane_ = nullptr;

    /// Poly/Mono visibility groups - toggled by voice mode
    VSTGUI::CView* polyGroup_ = nullptr;
    VSTGUI::CView* monoGroup_ = nullptr;

    // Harmonizer voice row containers (for dimming based on NumVoices)
    std::array<VSTGUI::CViewContainer*, 4> harmonizerVoiceRows_{};

    // PW knob visual disable (068-osc-type-params FR-016)
    // These live inside UIViewSwitchContainer templates and are only valid
    // when the PolyBLEP template is the active view for that oscillator.
    VSTGUI::CView* oscAPWKnob_ = nullptr;
    VSTGUI::CView* oscBPWKnob_ = nullptr;

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
    // Arp Interaction Polish (Phase 11c)
    // ==========================================================================

    /// Handle an arp skip event received via IMessage from the processor
    void handleArpSkipEvent(int lane, int step);

    /// Copy lane values to clipboard (Phase 11c, T059)
    void onLaneCopy(int laneIndex);

    /// Paste clipboard values to target lane (Phase 11c, T060)
    void onLanePaste(int targetLaneIndex);

    /// Wire copy/paste callbacks on all 6 lanes (Phase 11c, T061)
    void wireCopyPasteCallbacks();

    /// Get the IArpLane pointer for a given lane index (0-5)
    Krate::Plugins::IArpLane* getArpLane(int index);

    /// Get the step base parameter ID for a given lane index (0-5)
    static uint32_t getArpLaneStepBaseParamId(int index);

    /// Get the length parameter ID for a given lane index (0-5)
    static uint32_t getArpLaneLengthParamId(int index);

    Krate::Plugins::LaneClipboard clipboard_;

    /// Trail polling timer (~30fps), drives playhead trail rendering in all lanes
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> trailTimer_;

    /// Per-lane trail state (6 lanes: vel, gate, pitch, ratchet, modifier, condition)
    static constexpr int kArpLaneCount = 6;
    std::array<Krate::Plugins::PlayheadTrailState, 6> laneTrailStates_{};
    std::array<int32_t, 6> lastPolledSteps_{-1, -1, -1, -1, -1, -1};
    bool wasTransportPlaying_ = false;  ///< Track transport state for stop→clear

    // ==========================================================================
    // Bottom Bar Controls (081-interaction-polish, Phase 8 / US6)
    // ==========================================================================

    /// Euclidean controls sub-container (knobs + dot display, hidden when disabled)
    VSTGUI::CView* arpEuclideanGroup_ = nullptr;

    /// Dice ActionButton control pointer (for registerControlListener)
    VSTGUI::CControl* diceButton_ = nullptr;

    // ==========================================================================
    // Preset Browser (Spec 083)
    // ==========================================================================

    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;

    /// Preset browser overlay view (owned by frame, raw pointer)
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;

    /// Save preset dialog overlay view (owned by frame, raw pointer)
    Krate::Plugins::SavePresetDialogView* savePresetDialogView_ = nullptr;

protected:
    // ==========================================================================
    // Preset Loading Helpers (Spec 083)
    // ==========================================================================
    // Protected (not private) to enable test subclass access via `using` pattern.

    /// Create a memory stream containing the current component state
    /// Delegates to host via getComponentState() -- does NOT re-serialize
    /// @return New MemoryStream (caller must release), or nullptr on failure
    Steinberg::MemoryStream* createComponentStateStream();

    /// Load component state from stream with host notification
    /// Mirrors setComponentState() deserialization, but calls editParamWithNotify
    /// @param state Stream containing component state in Processor::getState() format
    /// @return true on success
    bool loadComponentStateWithNotify(Steinberg::IBStream* state);

    /// Edit a parameter with full host notification
    /// Sequence: beginEdit -> setParamNormalized -> performEdit -> endEdit
    void editParamWithNotify(Steinberg::Vst::ParamID id,
                             Steinberg::Vst::ParamValue value);
};

} // namespace Ruinae
