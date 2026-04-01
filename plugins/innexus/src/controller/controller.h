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

#include "display_data.h"
#include "preset/preset_manager.h"
#include "update/update_checker.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vstnoteexpressiontypes.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstphysicalui.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/cvstguitimer.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Steinberg {
class MemoryStream;
}

namespace Krate::Plugins {
class ADSRDisplay;
class PresetBrowserView;
class SavePresetDialogView;
class UpdateBannerView;
}

namespace Innexus {

// Forward declarations for custom views
class ADSRExpandedOverlayView;
class HarmonicDisplayView;
class ConfidenceIndicatorView;
class MemorySlotStatusView;
class EvolutionPositionView;
class ModulatorActivityView;

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate,
                   public Steinberg::Vst::IDataExchangeReceiver,
                   public Steinberg::Vst::INoteExpressionController,
                   public Steinberg::Vst::INoteExpressionPhysicalUIMapping
{
public:
    Controller() = default;
    ~Controller() override = default;

    // --- IEditController ---
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setParamNormalized(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue value) override;

    // --- Parameter Display Formatting ---
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;

    // --- VST3EditorDelegate ---
    Steinberg::IPlugView* PLUGIN_API createView(
        Steinberg::FIDString name) override;

    VSTGUI::CView* createCustomView(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;

    VSTGUI::IController* createSubController(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;

    void didOpen(VSTGUI::VST3Editor* editor) override;
    void willClose(VSTGUI::VST3Editor* editor) override;

    // --- IMessage handler ---
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message) override;

    // --- IDataExchangeReceiver ---
    void PLUGIN_API queueOpened(
        Steinberg::Vst::DataExchangeUserContextID userContextID,
        Steinberg::uint32 blockSize,
        Steinberg::TBool& dispatchOnBackgroundThread) override;
    void PLUGIN_API queueClosed(
        Steinberg::Vst::DataExchangeUserContextID userContextID) override;
    void PLUGIN_API onDataExchangeBlocksReceived(
        Steinberg::Vst::DataExchangeUserContextID userContextID,
        Steinberg::uint32 numBlocks,
        Steinberg::Vst::DataExchangeBlock* blocks,
        Steinberg::TBool onBackgroundThread) override;

    // --- INoteExpressionController ---
    Steinberg::int32 PLUGIN_API getNoteExpressionCount(
        Steinberg::int32 busIndex, Steinberg::int16 channel) override;
    Steinberg::tresult PLUGIN_API getNoteExpressionInfo(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::int32 noteExpressionIndex,
        Steinberg::Vst::NoteExpressionTypeInfo& info) override;
    Steinberg::tresult PLUGIN_API getNoteExpressionStringByValue(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        Steinberg::Vst::NoteExpressionValue valueNormalized,
        Steinberg::Vst::String128 string) override;
    Steinberg::tresult PLUGIN_API getNoteExpressionValueByString(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        const Steinberg::Vst::TChar* string,
        Steinberg::Vst::NoteExpressionValue& valueNormalized) override;

    // --- INoteExpressionPhysicalUIMapping ---
    Steinberg::tresult PLUGIN_API getPhysicalUIMapping(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::PhysicalUIMapList& list) override;

    // --- Interface support ---
    OBJ_METHODS(Controller, EditControllerEx1)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
        DEF_INTERFACE(Steinberg::Vst::INoteExpressionController)
        DEF_INTERFACE(Steinberg::Vst::INoteExpressionPhysicalUIMapping)
    END_DEFINE_INTERFACES(EditControllerEx1)
    DELEGATE_REFCOUNT(EditControllerEx1)

    /// @brief Import a snapshot from a JSON file into a memory slot (FR-025, FR-029).
    bool importSnapshotFromJson(const std::string& filePath, int slotIndex);

    /// @brief Called by SampleLoadSubController when user selects a sample file.
    void onSampleFileSelected(const std::string& filePath);

    /// @brief Update the sample filename display label.
    void setSampleFilenameDisplay(const std::string& filename,
                                 const std::string& fullPath);

    /// Open the preset browser overlay
    void openPresetBrowser();
    /// Close the preset browser overlay
    void closePresetBrowser();
    /// Open the save preset dialog overlay
    void openSavePresetDialog();

    /// Open/close the expanded ADSR envelope overlay
    void openAdsrExpandedOverlay();
    void closeAdsrExpandedOverlay();

    /// Get the preset manager instance (for custom view buttons)
    Krate::Plugins::PresetManager* getPresetManager() { return presetManager_.get(); }

    /// Create a MemoryStream containing the current component state
    Steinberg::MemoryStream* createComponentStateStream();

    /// Load a component state stream, applying all parameters via performEdit
    bool loadComponentStateWithNotify(Steinberg::IBStream* state);

    /// Apply a single parameter with begin/perform/end edit notifications
    void editParamWithNotify(Steinberg::Vst::ParamID id,
                             Steinberg::Vst::ParamValue value);

    /// Get cached display data (test accessor)
    const DisplayData& getCachedDisplayData() const { return cachedDisplayData_; }

    /// Simulate one display timer tick (TEST ONLY).
    void testTickDisplayTimer() { onDisplayTimerFired(); }

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

private:
    /// Timer callback for display updates (FR-049)
    void onDisplayTimerFired();

    /// Factory for preset browser / save buttons (called from createCustomView)
    VSTGUI::CView* createPresetButton(const VSTGUI::CRect& rect, bool isBrowse);

    /// Factory for ADSR expand button (called from createCustomView)
    VSTGUI::CView* createAdsrExpandButton(const VSTGUI::CRect& rect);

    /// Update sample load panel visibility based on InputSource parameter
    void updateSampleLoadVisibility();

    /// Update Impact exciter knob container visibility based on ExciterType
    void updateImpactKnobVisibility();

    /// Update Bow exciter knob container visibility based on ExciterType
    void updateBowVisibility();

    /// Update resonator knob containers visibility based on ResonanceType
    void updateResonatorVisibility();

    /// Show/hide the feedback section based on input source (sidechain only)
    void updateFeedbackVisibility();

    /// Push current ADSR parameter values to the ADSRDisplay view(s)
    void updateAdsrDisplayFromParams();

    /// Helper: wire an ADSRDisplay instance with parameter callbacks
    void wireAdsrDisplay(Krate::Plugins::ADSRDisplay* display);

    /// Helper: forward a single ADSR parameter change to a display
    static void forwardAdsrParamToDisplay(Krate::Plugins::ADSRDisplay* display,
                                          Steinberg::Vst::ParamID id,
                                          float norm);

    // Update Checker
    std::unique_ptr<Krate::Plugins::UpdateChecker> updateChecker_;

    // Display data pipeline (FR-048)
    DisplayData cachedDisplayData_{};
    Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};

    // Display timer (FR-049)
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> displayTimer_;

    // Modulator sub-controller instance counter (FR-046)
    int modInstanceCounter_ = 0;

    // Custom view pointers (set in createCustomView, nulled in willClose)
    // VSTGUI owns the view lifetime; these are raw observation pointers.
    HarmonicDisplayView* harmonicDisplayView_ = nullptr;
    ConfidenceIndicatorView* confidenceIndicatorView_ = nullptr;
    MemorySlotStatusView* memorySlotStatusView_ = nullptr;
    EvolutionPositionView* evolutionPositionView_ = nullptr;
    ModulatorActivityView* modActivityView0_ = nullptr;
    ModulatorActivityView* modActivityView1_ = nullptr;

    // Sample load UI pointers (VSTGUI-owned, nulled in willClose)
    VSTGUI::CTextLabel* sampleFilenameLabel_ = nullptr;
    VSTGUI::CView* sampleLoadContainer_ = nullptr;
    VSTGUI::CView* sampleDropOverlay_ = nullptr;

    // Cached sample filename for display across editor open/close
    std::string loadedSampleFilename_;
    std::string loadedSampleFullPath_;

    // Preset manager
    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;

    // Preset browser overlay view (VSTGUI-owned, raw pointer)
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;
    // Save preset dialog overlay view (VSTGUI-owned, raw pointer)
    Krate::Plugins::SavePresetDialogView* savePresetDialogView_ = nullptr;

    // Update banner view (VSTGUI-owned, nulled in willClose)
    Krate::Plugins::UpdateBannerView* updateBannerView_ = nullptr;

    // ADSR display view (VSTGUI-owned, nulled in willClose)
    Krate::Plugins::ADSRDisplay* adsrDisplayView_ = nullptr;

    // ADSR expanded overlay (VSTGUI-owned, nulled in willClose)
    ADSRExpandedOverlayView* adsrExpandedOverlay_ = nullptr;

    // ADSR playback state pointers from processor (Spec 124: T049)
    // Received via IMessage from Processor::process() on first block.
    std::atomic<float>* adsrOutputPtr_ = nullptr;
    std::atomic<int>* adsrStagePtr_ = nullptr;
    std::atomic<bool>* adsrActivePtr_ = nullptr;

    // Impact exciter knob container (VSTGUI-owned, nulled in willClose)
    VSTGUI::CViewContainer* impactKnobContainer_ = nullptr;

    // Bow exciter knob container (VSTGUI-owned, nulled in willClose)
    VSTGUI::CViewContainer* bowKnobContainer_ = nullptr;

    // Feedback section container (VSTGUI-owned, nulled in willClose)
    // Disabled when not in sidechain mode (feedback requires live input)
    VSTGUI::CViewContainer* feedbackContainer_ = nullptr;

    // Latency mode container (VSTGUI-owned, nulled in willClose)
    // Only relevant in sidechain mode (configures live analysis pipeline)
    VSTGUI::CViewContainer* latencyModeContainer_ = nullptr;

    // Resonator knob containers (VSTGUI-owned, nulled in willClose)
    VSTGUI::CViewContainer* modalKnobContainer_ = nullptr;
    VSTGUI::CViewContainer* waveguideKnobContainer_ = nullptr;

    // Active editor pointer for visibility controllers
    VSTGUI::VST3Editor* activeEditor_ = nullptr;

    // Track last processed frame counter to avoid redundant updates
    uint32_t lastProcessedFrameCounter_ = 0;

    // Stale data detection: count consecutive timer ticks with no new frame
    int staleTickCount_ = 0;
    static constexpr int kStaleTickThreshold = 3; // ~90ms at 30ms timer

    // =========================================================================
    // SharedDisplayBridge (Tier 3 fallback)
    // =========================================================================
    struct SharedDisplay {
        DisplayData buffer{};
        std::atomic<uint32_t> frameCounter{0};
    };

    uint64_t instanceId_{0};
    SharedDisplay* sharedDisplay_{nullptr};
    bool dataExchangeActive_{false};
    int fallbackTickCounter_{0};

    static constexpr Steinberg::int32 kInstanceIdMarker = 0x4B524154;

    // NoteExpression type container (Phase 4: MPE support)
    Steinberg::Vst::NoteExpressionTypeContainer noteExpressionTypes_;
};

} // namespace Innexus
