// ==============================================================================
// Controller: Preset Browser & Preset Loading
// ==============================================================================

#include "controller.h"
#include "controller_state_helpers.h"
#include "../plugin_ids.h"
#include "../parameters/arpeggiator_params.h"
#include "../ui/speed_curve_editor.h"
#include "../preset/gradus_preset_config.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/outline_button.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/common/memorystream.h"

#include <array>
#include <cstring>
#include <vector>

namespace {

// Shared dark-theme outline button base (CView + virtual onClick()).
using Krate::Plugins::OutlineButton;

class PresetBrowserButton : public OutlineButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Gradus::Controller* controller)
        : OutlineButton(size, "Presets", VSTGUI::CColor(64, 64, 72))
        , controller_(controller) {}
protected:
    void onClick() override {
        if (controller_) controller_->openPresetBrowser();
    }
private:
    Gradus::Controller* controller_ = nullptr;
};

class SavePresetButton : public OutlineButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Gradus::Controller* controller)
        : OutlineButton(size, "Save", VSTGUI::CColor(64, 64, 72))
        , controller_(controller) {}
protected:
    void onClick() override {
        if (controller_) controller_->openSavePresetDialog();
    }
private:
    Gradus::Controller* controller_ = nullptr;
};

} // anonymous namespace

namespace Gradus {

// ==============================================================================
// Preset Button Factory
// ==============================================================================

VSTGUI::CView* Controller::createPresetButton(const VSTGUI::CRect& rect, bool isBrowse) {
    if (isBrowse)
        return new PresetBrowserButton(rect, this);
    return new SavePresetButton(rect, this);
}

// ==============================================================================
// Preset Browser Methods
// ==============================================================================

void Controller::openPresetBrowser() {
    if (presetBrowserView_ && !presetBrowserView_->isOpen()) {
        presetBrowserView_->open();
    }
}

void Controller::closePresetBrowser() {
    if (presetBrowserView_ && presetBrowserView_->isOpen()) {
        presetBrowserView_->close();
    }
}

void Controller::openSavePresetDialog() {
    if (savePresetDialogView_ && !savePresetDialogView_->isOpen()) {
        savePresetDialogView_->open("Classic");
    }
}

// ==============================================================================
// Preset Loading Helpers
// ==============================================================================

Steinberg::MemoryStream* Controller::createComponentStateStream() {
    Steinberg::FUnknownPtr<Steinberg::Vst::IComponent> component(getComponentHandler());
    if (!component)
        return nullptr;

    auto* stream = new Steinberg::MemoryStream();
    if (component->getState(stream) != Steinberg::kResultOk) {
        stream->release();
        return nullptr;
    }

    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return stream;
}

bool Controller::loadComponentStateWithNotify(Steinberg::IBStream* state) {
    if (!state)
        return false;

    // Read the whole stream first (an IBStream may not seek): the processor
    // gets these bytes, and loadFullState below reads the same buffer.
    std::vector<char> bytes;
    {
        constexpr Steinberg::int32 kChunkSize = 4096;
        std::array<char, kChunkSize> chunk{};
        Steinberg::int32 bytesRead = 0;
        while (state->read(chunk.data(), kChunkSize, &bytesRead) == Steinberg::kResultTrue &&
               bytesRead > 0) {
            bytes.insert(bytes.end(), chunk.data(), chunk.data() + bytesRead);
        }
    }
    if (bytes.empty())
        return false;

    Steinberg::MemoryStream memStream(bytes.data(), static_cast<Steinberg::TSize>(bytes.size()));
    Steinberg::IBStreamer streamer(&memStream, kLittleEndian);

    // Accept the same versions the processor does (isLoadableStateVersion:
    // v1 and v2 legacy, v3 current); the newer blocks are EOF-tolerant, so an
    // older stream simply leaves those fields at their defaults. Anything else
    // must be refused here too -- otherwise the processor keeps defaults while
    // the controller parses the payload into its parameter cache and the two
    // components disagree.
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))
        return false;
    if (!isLoadableStateVersion(version))
        return false;

    // One preset-load path (R-206): the processor parses and applies these
    // bytes in its setState, exactly as on a host load (speed curves and
    // depths included).
    if (auto msg = Steinberg::owned(allocateMessage())) {
        msg->setMessageID("LoadPresetState");
        if (auto* attrs = msg->getAttributes()) {
            attrs->setBinary("bytes", bytes.data(), static_cast<Steinberg::uint32>(bytes.size()));
            sendMessage(msg);
        }
    }

    // The edits tell the host (automation, undo, dirty flag) and the UI the new
    // values; they carry the values the processor already holds.
    auto setParam = [this](Steinberg::Vst::ParamID id, double value) {
        editParamWithNotify(id, value);
    };
    loadFullState(streamer, setParam);

    // Audition params are session-only — not loaded from presets

    return true;
}

void Controller::editParamWithNotify(Steinberg::Vst::ParamID id,
                                     Steinberg::Vst::ParamValue value) {
    value = std::max(0.0, std::min(1.0, value));
    beginEdit(id);
    setParamNormalized(id, value);
    performEdit(id, value);
    endEdit(id);
}

} // namespace Gradus
