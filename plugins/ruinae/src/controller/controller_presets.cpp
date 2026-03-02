// ==============================================================================
// Controller: Preset Browser & Preset Loading
// ==============================================================================
// Extracted from controller.cpp - handles preset browser open/close methods,
// createCustomView(), createComponentStateStream(),
// loadComponentStateWithNotify(), editParamWithNotify(), and the button classes
// used by createCustomView().
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "preset/ruinae_preset_config.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "vstgui/uidescription/uiattributes.h"

// Parameter pack headers (for loadXxxParamsToController)
#include "parameters/global_params.h"
#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"
#include "parameters/mixer_params.h"
#include "parameters/filter_params.h"
#include "parameters/distortion_params.h"
#include "parameters/trance_gate_params.h"
#include "parameters/amp_env_params.h"
#include "parameters/filter_env_params.h"
#include "parameters/mod_env_params.h"
#include "parameters/lfo1_params.h"
#include "parameters/lfo2_params.h"
#include "parameters/chaos_mod_params.h"
#include "parameters/mod_matrix_params.h"
#include "parameters/global_filter_params.h"
#include "parameters/delay_params.h"
#include "parameters/fx_enable_params.h"
#include "parameters/reverb_params.h"
#include "parameters/phaser_params.h"
#include "parameters/harmonizer_params.h"
#include "parameters/mono_mode_params.h"
#include "parameters/macro_params.h"
#include "parameters/rungler_params.h"
#include "parameters/settings_params.h"
#include "parameters/env_follower_params.h"
#include "parameters/sample_hold_params.h"
#include "parameters/random_params.h"
#include "parameters/pitch_follower_params.h"
#include "parameters/transient_params.h"
#include "parameters/arpeggiator_params.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ioptionmenulistener.h"

#include <cstring>
#include <functional>

namespace {

// COptionMenu subclass that escapes "&" -> "&&" only for the Win32 popup menu,
// which interprets "&" as an accelerator prefix. The label and macOS/Linux popups
// use the raw string directly, so we store single "&" and only escape around popup.
class FixedOptionMenu : public VSTGUI::COptionMenu,
                        public VSTGUI::OptionMenuListenerAdapter {
public:
    using COptionMenu::COptionMenu;

    bool attached(VSTGUI::CView* parent) override {
        if (COptionMenu::attached(parent)) {
            registerOptionMenuListener(this);
            return true;
        }
        return false;
    }

    bool removed(VSTGUI::CView* parent) override {
        unregisterOptionMenuListener(this);
        return COptionMenu::removed(parent);
    }

#ifdef _WIN32
    void onOptionMenuPrePopup(VSTGUI::COptionMenu* menu) override {
        // Escape "&" -> "&&" so Win32 HMENU shows literal ampersand
        for (auto& item : *menu->getItems()) {
            auto title = item->getTitle().getString();
            std::string::size_type pos = 0;
            bool modified = false;
            while ((pos = title.find('&', pos)) != std::string::npos) {
                title.insert(pos, 1, '&');
                pos += 2;
                modified = true;
            }
            if (modified)
                item->setTitle(VSTGUI::UTF8String(title));
        }
    }

    void onOptionMenuPostPopup(VSTGUI::COptionMenu* menu) override {
        // Restore "&&" -> "&" after popup closes
        for (auto& item : *menu->getItems()) {
            auto title = item->getTitle().getString();
            std::string::size_type pos = 0;
            bool modified = false;
            while ((pos = title.find("&&", pos)) != std::string::npos) {
                title.erase(pos, 1);
                pos += 1;
                modified = true;
            }
            if (modified)
                item->setTitle(VSTGUI::UTF8String(title));
        }
    }
#endif
};

// ==============================================================================
// OutlineButton: Minimal outline-style button matching Ruinae dark theme
// ==============================================================================
class OutlineButton : public VSTGUI::CView {
public:
    OutlineButton(const VSTGUI::CRect& size, std::string title,
                  VSTGUI::CColor frameColor = VSTGUI::CColor(64, 64, 72))
        : CView(size)
        , title_(std::move(title))
        , frameColor_(frameColor)
    {}

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
        auto r = getViewSize();
        r.inset(0.5, 0.5);

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (path) {
            constexpr double kRadius = 3.0;
            path->addRoundRect(r, kRadius);

            if (hovered_) {
                context->setFillColor(VSTGUI::CColor(255, 255, 255, 20));
                context->drawGraphicsPath(path,
                    VSTGUI::CDrawContext::kPathFilled);
            }

            context->setFrameColor(frameColor_);
            context->setLineWidth(1.0);
            context->drawGraphicsPath(path,
                VSTGUI::CDrawContext::kPathStroked);
        }

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>(
            *VSTGUI::kNormalFontSmaller);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(192, 192, 192));
        context->drawString(
            VSTGUI::UTF8String(title_), getViewSize(),
            VSTGUI::kCenterText);

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseEntered(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override {
        hovered_ = true;
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorHand);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override {
        hovered_ = false;
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorDefault);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& buttons) override {
        if (buttons.isLeftButton()) {
            onClick();
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

protected:
    virtual void onClick() = 0;

private:
    std::string title_;
    VSTGUI::CColor frameColor_;
    bool hovered_ = false;
};

class PresetBrowserButton : public OutlineButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Ruinae::Controller* controller)
        : OutlineButton(size, "Presets", VSTGUI::CColor(64, 64, 72))
        , controller_(controller) {}
protected:
    void onClick() override {
        if (controller_) controller_->openPresetBrowser();
    }
private:
    Ruinae::Controller* controller_ = nullptr;
};

class SavePresetButton : public OutlineButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Ruinae::Controller* controller)
        : OutlineButton(size, "Save", VSTGUI::CColor(64, 64, 72))
        , controller_(controller) {}
protected:
    void onClick() override {
        if (controller_) controller_->openSavePresetDialog();
    }
private:
    Ruinae::Controller* controller_ = nullptr;
};

class ArpPresetBrowserButton : public OutlineButton {
public:
    ArpPresetBrowserButton(const VSTGUI::CRect& size, Ruinae::Controller* controller)
        : OutlineButton(size, "Presets", VSTGUI::CColor(200, 120, 80))
        , controller_(controller) {}
protected:
    void onClick() override {
        if (controller_) controller_->openArpPresetBrowser();
    }
private:
    Ruinae::Controller* controller_ = nullptr;
};

class ArpSavePresetButton : public OutlineButton {
public:
    ArpSavePresetButton(const VSTGUI::CRect& size, Ruinae::Controller* controller)
        : OutlineButton(size, "Save", VSTGUI::CColor(200, 120, 80))
        , controller_(controller) {}
protected:
    void onClick() override {
        if (controller_) controller_->openArpSavePresetDialog();
    }
private:
    Ruinae::Controller* controller_ = nullptr;
};

} // anonymous namespace

namespace Ruinae {

// ==============================================================================
// Preset Browser Methods (Spec 083)
// ==============================================================================

void Controller::openPresetBrowser() {
    if (presetBrowserView_ && !presetBrowserView_->isOpen()) {
        // Close ARP browser if open (mutual exclusion)
        closeArpPresetBrowser();
        presetBrowserView_->open("");
    }
}

void Controller::closePresetBrowser() {
    if (presetBrowserView_ && presetBrowserView_->isOpen()) {
        presetBrowserView_->close();
    }
}

void Controller::openArpPresetBrowser() {
    if (arpPresetBrowserView_ && !arpPresetBrowserView_->isOpen()) {
        // Close synth browser if open (mutual exclusion)
        closePresetBrowser();
        arpPresetBrowserView_->open("");
    }
}

void Controller::closeArpPresetBrowser() {
    if (arpPresetBrowserView_ && arpPresetBrowserView_->isOpen()) {
        arpPresetBrowserView_->close();
    }
}

void Controller::openArpSavePresetDialog() {
    if (arpPresetBrowserView_ && !arpPresetBrowserView_->isOpen()) {
        closePresetBrowser();
        arpPresetBrowserView_->openWithSaveDialog("Arp Classic");
    }
}

void Controller::openSavePresetDialog() {
    if (savePresetDialogView_ && !savePresetDialogView_->isOpen()) {
        savePresetDialogView_->open("");
    }
}

// ==============================================================================
// Custom View Factory (preset buttons + FixedOptionMenu)
// ==============================================================================

VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/) {

    // Preset Browser Button (Spec 083)
    if (std::strcmp(name, "PresetBrowserButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(80, 25);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new PresetBrowserButton(rect, this);
    }

    // ARP Preset Browser Button
    if (std::strcmp(name, "ArpPresetBrowserButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(80, 18);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new ArpPresetBrowserButton(rect, this);
    }

    // ARP Save Preset Button
    if (std::strcmp(name, "ArpSavePresetButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(50, 18);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new ArpSavePresetButton(rect, this);
    }

    // Save Preset Button (Spec 083)
    if (std::strcmp(name, "SavePresetButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(60, 25);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new SavePresetButton(rect, this);
    }

    // Mod source dropdown: COptionMenu subclass that unescapes && in closed label
    if (std::strcmp(name, "ModSourceDropdown") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(160, 22);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new FixedOptionMenu(rect, nullptr, -1);
    }

    return nullptr;
}

// ==============================================================================
// Preset Loading Helpers
// ==============================================================================

Steinberg::MemoryStream* Controller::createComponentStateStream() {
    // Delegate to host via IComponent::getState() -- does NOT re-serialize
    // parameters from controller. Caller owns the returned stream.
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

bool Controller::loadComponentStateWithNotify(Steinberg::IBStream* state, bool arpOnly) {
    if (!state)
        return false;

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Item 1: Read and validate version
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version) || version != 1)
        return false;

    // Lambda that calls editParamWithNotify instead of setParamNormalized
    auto setParam = [this](Steinberg::Vst::ParamID id, double value) {
        editParamWithNotify(id, value);
    };

    // When loading an arp-only preset, synth parameters are read but not applied.
    // The no-op lambda discards values while the stream position still advances.
    using SetParamFunc = std::function<void(Steinberg::Vst::ParamID, double)>;
    SetParamFunc synthSetter = arpOnly
        ? SetParamFunc([](Steinberg::Vst::ParamID, double) {})
        : SetParamFunc(setParam);

    // Items 2-19: Parameter packs in deterministic order (matching Processor::getState)
    loadGlobalParamsToController(streamer, synthSetter);
    loadOscAParamsToController(streamer, synthSetter);
    loadOscBParamsToController(streamer, synthSetter);
    loadMixerParamsToController(streamer, synthSetter);
    loadFilterParamsToController(streamer, synthSetter);
    loadDistortionParamsToController(streamer, synthSetter);
    loadTranceGateParamsToController(streamer, synthSetter);
    loadAmpEnvParamsToController(streamer, synthSetter);
    loadFilterEnvParamsToController(streamer, synthSetter);
    loadModEnvParamsToController(streamer, synthSetter);
    loadLFO1ParamsToController(streamer, synthSetter);
    loadLFO2ParamsToController(streamer, synthSetter);
    loadChaosModParamsToController(streamer, synthSetter);
    loadModMatrixParamsToController(streamer, synthSetter);
    loadGlobalFilterParamsToController(streamer, synthSetter);
    loadDelayParamsToController(streamer, synthSetter);
    loadReverbParamsToController(streamer, synthSetter);
    loadMonoModeParamsToController(streamer, synthSetter);

    // Item 20: Voice routes (16 slots, processor-internal) -- read and DISCARD
    for (int i = 0; i < 16; ++i) {
        Steinberg::int8 dummy8 = 0;
        float dummyF = 0;
        if (!streamer.readInt8(dummy8) || !streamer.readInt8(dummy8) ||
            !streamer.readFloat(dummyF) || !streamer.readInt8(dummy8) ||
            !streamer.readFloat(dummyF) || !streamer.readInt8(dummy8) ||
            !streamer.readInt8(dummy8) || !streamer.readInt8(dummy8))
            return false;
    }

    // Items 21-22: FX enable flags (int8 -> 0.0/1.0)
    Steinberg::int8 flag = 0;
    if (!streamer.readInt8(flag)) return false;
    if (!arpOnly) editParamWithNotify(kDelayEnabledId, flag ? 1.0 : 0.0);
    if (!streamer.readInt8(flag)) return false;
    if (!arpOnly) editParamWithNotify(kReverbEnabledId, flag ? 1.0 : 0.0);

    // Item 23: Phaser params
    loadPhaserParamsToController(streamer, synthSetter);

    // Item 24: Phaser enable flag (int8 -> 0.0/1.0)
    if (!streamer.readInt8(flag)) return false;
    if (!arpOnly) editParamWithNotify(kPhaserEnabledId, flag ? 1.0 : 0.0);

    // Items 25-35: Remaining parameter packs
    loadLFO1ExtendedParamsToController(streamer, synthSetter);
    loadLFO2ExtendedParamsToController(streamer, synthSetter);
    loadMacroParamsToController(streamer, synthSetter);
    loadRunglerParamsToController(streamer, synthSetter);
    loadSettingsParamsToController(streamer, synthSetter);
    loadEnvFollowerParamsToController(streamer, synthSetter);
    loadSampleHoldParamsToController(streamer, synthSetter);
    loadRandomParamsToController(streamer, synthSetter);
    loadPitchFollowerParamsToController(streamer, synthSetter);
    loadTransientParamsToController(streamer, synthSetter);
    loadHarmonizerParamsToController(streamer, synthSetter);

    // Item 36: Harmonizer enable flag (int8 -> 0.0/1.0)
    if (!streamer.readInt8(flag)) return false;
    if (!arpOnly) editParamWithNotify(kHarmonizerEnabledId, flag ? 1.0 : 0.0);

    // Item 37: Arp params (includes all lane step data) - ALWAYS applied
    loadArpParamsToController(streamer, setParam);

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

} // namespace Ruinae
