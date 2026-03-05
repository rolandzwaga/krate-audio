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
#include "update/update_checker.h"

#include <memory>
#include <string>

namespace Innexus {

class Controller : public Steinberg::Vst::EditControllerEx1
{
public:
    Controller() = default;
    ~Controller() override = default;

    // --- IEditController ---
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state) override;

    /// @brief Import a snapshot from a JSON file into a memory slot (FR-025, FR-029).
    ///
    /// Reads the file, parses JSON via jsonToSnapshot(), and dispatches the
    /// snapshot to the processor via IMessage. Runs entirely off the audio thread.
    /// The file dialog UI is deferred to Milestone 7; for M5 this is called
    /// from tests or programmatically.
    ///
    /// @param filePath Path to the JSON file
    /// @param slotIndex Target slot index (0-7)
    /// @return true on success, false on any failure
    bool importSnapshotFromJson(const std::string& filePath, int slotIndex);

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

private:
    // Update Checker (ready for when UI is added)
    std::unique_ptr<Krate::Plugins::UpdateChecker> updateChecker_;
};

} // namespace Innexus
