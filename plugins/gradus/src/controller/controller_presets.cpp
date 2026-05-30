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

#include <cstring>

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

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read and discard state version (single version, no migration needed)
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))
        return false;
    (void)version;

    // Single path for all state loading (arp params + speed curves + delay)
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
