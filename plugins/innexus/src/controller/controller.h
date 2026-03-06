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
#include "update/update_checker.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/cvstguitimer.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Innexus {

// Forward declarations for custom views
class HarmonicDisplayView;
class ConfidenceIndicatorView;
class MemorySlotStatusView;
class EvolutionPositionView;
class ModulatorActivityView;

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate
{
public:
    Controller() = default;
    ~Controller() override = default;

    // --- IEditController ---
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state) override;

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

    /// @brief Import a snapshot from a JSON file into a memory slot (FR-025, FR-029).
    bool importSnapshotFromJson(const std::string& filePath, int slotIndex);

    /// @brief Called by SampleLoadSubController when user selects a sample file.
    void onSampleFileSelected(const std::string& filePath);

    /// @brief Update the sample filename display label.
    void setSampleFilenameDisplay(const std::string& filename,
                                 const std::string& fullPath);

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

private:
    /// Timer callback for display updates (FR-049)
    void onDisplayTimerFired();

    /// Update sample load panel visibility based on InputSource parameter
    void updateSampleLoadVisibility();

    // Update Checker
    std::unique_ptr<Krate::Plugins::UpdateChecker> updateChecker_;

    // Display data pipeline (FR-048)
    DisplayData cachedDisplayData_{};

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

    // Cached sample filename for display across editor open/close
    std::string loadedSampleFilename_;
    std::string loadedSampleFullPath_;

    // Active editor pointer for visibility controllers
    VSTGUI::VST3Editor* activeEditor_ = nullptr;

    // Track last processed frame counter to avoid redundant updates
    uint32_t lastProcessedFrameCounter_ = 0;
};

} // namespace Innexus
