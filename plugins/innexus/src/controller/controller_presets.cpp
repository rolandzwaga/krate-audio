// ==============================================================================
// Controller: Preset Browser & Preset Loading
// ==============================================================================
// Handles preset browser open/close methods, createComponentStateStream(),
// loadComponentStateWithNotify(), editParamWithNotify(), and the button classes
// used by createCustomView().
//
// Pattern: plugins/ruinae/src/controller/controller_presets.cpp
// ==============================================================================

#include "controller.h"
#include "preset/innexus_preset_config.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/outline_button.h"

#include "base/source/fstreamer.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstring>
#include <vector>

namespace {

// Shared dark-theme outline button base (CView + virtual onClick()).
using Krate::Plugins::OutlineButton;

class PresetBrowserButton : public OutlineButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Innexus::Controller* controller)
        : OutlineButton(size, "Presets", VSTGUI::CColor(64, 64, 72))
        , controller_(controller) {}
protected:
    void onClick() override {
        if (controller_) controller_->openPresetBrowser();
    }
private:
    Innexus::Controller* controller_ = nullptr;
};

class SavePresetButton : public OutlineButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Innexus::Controller* controller)
        : OutlineButton(size, "Save", VSTGUI::CColor(64, 64, 72))
        , controller_(controller) {}
protected:
    void onClick() override {
        if (controller_) controller_->openSavePresetDialog();
    }
private:
    Innexus::Controller* controller_ = nullptr;
};

} // anonymous namespace

namespace Innexus {

// ==============================================================================
// Preset Button Factory (called from createCustomView in controller.cpp)
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
        savePresetDialogView_->open("Voice");
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
    // gets these bytes, and readComponentState below reads the same buffer.
    std::vector<char> bytes;
    {
        constexpr Steinberg::int32 kChunkSize = 4096;
        char chunk[kChunkSize];
        Steinberg::int32 bytesRead = 0;
        while (state->read(chunk, kChunkSize, &bytesRead) == Steinberg::kResultTrue &&
               bytesRead > 0) {
            bytes.insert(bytes.end(), chunk, chunk + bytesRead);
        }
    }
    if (bytes.empty())
        return false;

    // Version check exactly as setComponentState / Processor::setState do.
    {
        Steinberg::MemoryStream versionStream(bytes.data(),
                                              static_cast<Steinberg::TSize>(bytes.size()));
        Steinberg::IBStreamer streamer(&versionStream, kLittleEndian);
        Steinberg::int32 version = 0;
        if (!streamer.readInt32(version) || (version != 1 && version != 2))
            return false;
    }

    // One preset-load path (R-200): the processor runs its own setState on
    // these bytes, as on a host load (parameters, sample, residual frames,
    // memory slots, per-slot ADSR, evolution waypoints).
    if (auto msg = Steinberg::owned(allocateMessage())) {
        msg->setMessageID("LoadPresetState");
        if (auto* attrs = msg->getAttributes()) {
            attrs->setBinary("bytes", bytes.data(), static_cast<Steinberg::uint32>(bytes.size()));
            sendMessage(msg);
        }
    }

    // Mirror the same bytes through the one controller parser; performEdit
    // tells the host (automation, undo, dirty flag). The values equal what
    // the processor already holds.
    Steinberg::MemoryStream memStream(bytes.data(), static_cast<Steinberg::TSize>(bytes.size()));
    return readComponentState(&memStream,
               [this](Steinberg::Vst::ParamID id, double value) {
                   editParamWithNotify(id, value);
               }) == Steinberg::kResultOk;
}

void Controller::editParamWithNotify(Steinberg::Vst::ParamID id,
                                     Steinberg::Vst::ParamValue value) {
    value = std::max(0.0, std::min(1.0, value));
    beginEdit(id);
    setParamNormalized(id, value);
    performEdit(id, value);
    endEdit(id);
}

} // namespace Innexus
