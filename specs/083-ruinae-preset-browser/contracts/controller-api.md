# Controller API Contract: Ruinae Preset Browser

**Date**: 2026-02-27

## Controller Header Additions (controller.h)

### New Public Methods

```cpp
// Preset browser management
void openPresetBrowser();
void closePresetBrowser();
void openSavePresetDialog();

// PresetManager accessor (for custom view buttons)
Krate::Plugins::PresetManager* getPresetManager() { return presetManager_.get(); }

// Custom view creation (VST3EditorDelegate override)
VSTGUI::CView* createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* description,
    VSTGUI::VST3Editor* editor) override;
```

### New Private Methods

```cpp
// State serialization for preset saving
Steinberg::MemoryStream* createComponentStateStream();

// Preset loading with host notification
bool loadComponentStateWithNotify(Steinberg::IBStream* state);

// Helper: edit parameter with full host notification
void editParamWithNotify(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);
```

### New Private Fields

```cpp
// Preset browser overlay views (owned by frame, raw pointers)
Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;
Krate::Plugins::SavePresetDialogView* savePresetDialogView_ = nullptr;
```

## Controller Implementation (controller.cpp)

### Anonymous Namespace Classes

```cpp
namespace {

class PresetBrowserButton : public VSTGUI::CTextButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Ruinae::Controller* controller);
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
private:
    Ruinae::Controller* controller_ = nullptr;
};

class SavePresetButton : public VSTGUI::CTextButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Ruinae::Controller* controller);
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
private:
    Ruinae::Controller* controller_ = nullptr;
};

} // anonymous namespace
```

### Method Contracts

#### editParamWithNotify

```cpp
void Controller::editParamWithNotify(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    // PRE: value should be normalized [0.0, 1.0]
    // POST: Host is notified of parameter change, processor receives update
    // SEQUENCE: beginEdit -> setParamNormalized -> performEdit -> endEdit
    value = std::max(0.0, std::min(1.0, value));
    beginEdit(id);
    setParamNormalized(id, value);
    performEdit(id, value);
    endEdit(id);
}
```

#### createComponentStateStream

```cpp
Steinberg::MemoryStream* Controller::createComponentStateStream() {
    // PRE: Controller has a valid component handler from the host
    // POST: Returns a MemoryStream containing the processor's serialized state
    //       in the same format as Processor::getState()
    // NOTE: Delegates to host via getComponentState() -- does NOT re-serialize
    //       parameters from controller. Caller owns the returned stream.
    // RETURNS: MemoryStream* on success, nullptr on failure

    // Step 1: Query the component handler for the IComponent interface.
    //         The component handler is available via getComponentHandler().
    //         FUnknownPtr performs the QueryInterface cast safely.
    Steinberg::FUnknownPtr<Steinberg::IComponent> component(getComponentHandler());
    if (!component)
        return nullptr;

    // Step 2: Create a MemoryStream to receive the serialized state.
    //         The caller takes ownership of this object.
    auto* stream = new Steinberg::MemoryStream();

    // Step 3: Ask the processor (via IComponent) to serialize its state into stream.
    if (component->getState(stream) != Steinberg::kResultOk) {
        stream->release();
        return nullptr;
    }

    // Step 4: Seek back to the start so the reader begins from byte 0.
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return stream;
}
```

#### loadComponentStateWithNotify

```cpp
bool Controller::loadComponentStateWithNotify(Steinberg::IBStream* state) {
    // PRE: state contains a valid processor state stream (Processor::getState format)
    // POST: All parameters updated via editParamWithNotify, host notified of changes
    // NOTE: Mirrors setComponentState() exactly, but uses editParamWithNotify
    //       instead of setParamNormalized. Reuses loadXxxParamsToController helpers.
    // RETURNS: true on success, false on invalid/truncated stream

    IBStreamer streamer(state, kLittleEndian);

    // Step 1: Read and validate version. Reject anything other than version 1.
    int32 version = 0;
    if (!streamer.readInt32(version) || version != 1)
        return false;

    // Step 2-19: Deserialize each parameter pack using the same loadXxxParamsToController
    //            helpers as setComponentState(), but supply editParamWithNotify as the
    //            SetParamFunc callback instead of setParamNormalized.
    auto setParam = [this](Steinberg::Vst::ParamID id, double value) {
        editParamWithNotify(id, value);
    };
    if (!loadGlobalParamsToController(streamer, setParam))    return false;
    if (!loadOscAParamsToController(streamer, setParam))      return false;
    if (!loadOscBParamsToController(streamer, setParam))      return false;
    if (!loadMixerParamsToController(streamer, setParam))     return false;
    if (!loadFilterParamsToController(streamer, setParam))    return false;
    if (!loadDistortionParamsToController(streamer, setParam))return false;
    if (!loadTranceGateParamsToController(streamer, setParam))return false;
    if (!loadAmpEnvParamsToController(streamer, setParam))    return false;
    if (!loadFilterEnvParamsToController(streamer, setParam)) return false;
    if (!loadModEnvParamsToController(streamer, setParam))    return false;
    if (!loadLFO1ParamsToController(streamer, setParam))      return false;
    if (!loadLFO2ParamsToController(streamer, setParam))      return false;
    if (!loadChaosModParamsToController(streamer, setParam))  return false;
    if (!loadModMatrixParamsToController(streamer, setParam)) return false;
    if (!loadGlobalFilterParamsToController(streamer, setParam)) return false;
    if (!loadDelayParamsToController(streamer, setParam))     return false;
    if (!loadReverbParamsToController(streamer, setParam))    return false;
    if (!loadMonoModeParamsToController(streamer, setParam))  return false;

    // Step 20 (GOTCHA): Voice routes are processor-internal -- read and DISCARD all 16 slots.
    //   Per route: int8 source, int8 dest, float amount, int8 curve, float smoothMs,
    //              int8 scale, int8 bypass, int8 active.
    for (int i = 0; i < 16; ++i) {
        int8 dummy8; float dummyF;
        if (!streamer.readInt8(dummy8) || !streamer.readInt8(dummy8) ||
            !streamer.readFloat(dummyF) || !streamer.readInt8(dummy8) ||
            !streamer.readFloat(dummyF) || !streamer.readInt8(dummy8) ||
            !streamer.readInt8(dummy8) || !streamer.readInt8(dummy8))
            return false;
    }

    // Steps 21-22 (GOTCHA): FX enable flags are stored as int8, NOT as normalized doubles.
    //   Read as int8, convert to 0.0 or 1.0, then pass to editParamWithNotify.
    int8 flag = 0;
    if (!streamer.readInt8(flag)) return false;
    editParamWithNotify(kDelayEnabledId, flag ? 1.0 : 0.0);
    if (!streamer.readInt8(flag)) return false;
    editParamWithNotify(kReverbEnabledId, flag ? 1.0 : 0.0);

    // Step 23: Phaser params.
    if (!loadPhaserParamsToController(streamer, setParam)) return false;

    // Step 24 (GOTCHA): Another int8 FX enable flag (phaser).
    if (!streamer.readInt8(flag)) return false;
    editParamWithNotify(kPhaserEnabledId, flag ? 1.0 : 0.0);

    // Steps 25-35: Remaining parameter packs.
    if (!loadLFO1ExtendedParamsToController(streamer, setParam))  return false;
    if (!loadLFO2ExtendedParamsToController(streamer, setParam))  return false;
    if (!loadMacroParamsToController(streamer, setParam))         return false;
    if (!loadRunglerParamsToController(streamer, setParam))       return false;
    if (!loadSettingsParamsToController(streamer, setParam))      return false;
    if (!loadEnvFollowerParamsToController(streamer, setParam))   return false;
    if (!loadSampleHoldParamsToController(streamer, setParam))    return false;
    if (!loadRandomParamsToController(streamer, setParam))        return false;
    if (!loadPitchFollowerParamsToController(streamer, setParam)) return false;
    if (!loadTransientParamsToController(streamer, setParam))     return false;
    if (!loadHarmonizerParamsToController(streamer, setParam))    return false;

    // Step 36 (GOTCHA): Harmonizer enable flag stored as int8.
    if (!streamer.readInt8(flag)) return false;
    editParamWithNotify(kHarmonizerEnabledId, flag ? 1.0 : 0.0);

    // Step 37: Arp params -- includes all lane step data (velocity, gate, pitch,
    //          ratchet, modifier, condition lane steps and lengths).
    if (!loadArpParamsToController(streamer, setParam)) return false;

    return true;
}
```

#### openPresetBrowser / closePresetBrowser / openSavePresetDialog

```cpp
void Controller::openPresetBrowser() {
    // PRE: presetBrowserView_ is valid (set in didOpen)
    // POST: Preset browser overlay is visible
    // GUARD: Only opens if view exists and is not already open
    if (presetBrowserView_ && !presetBrowserView_->isOpen()) {
        presetBrowserView_->open("");
    }
}

void Controller::closePresetBrowser() {
    // PRE: presetBrowserView_ is valid
    // POST: Preset browser overlay is hidden
    if (presetBrowserView_ && presetBrowserView_->isOpen()) {
        presetBrowserView_->close();
    }
}

void Controller::openSavePresetDialog() {
    // PRE: savePresetDialogView_ is valid (set in didOpen)
    // POST: Save dialog overlay is visible
    if (savePresetDialogView_ && !savePresetDialogView_->isOpen()) {
        savePresetDialogView_->open("");
    }
}
```

### Initialization Wiring (in initialize())

```cpp
// After existing presetManager_ creation:
presetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
    return this->createComponentStateStream();
});

presetManager_->setLoadProvider([this](Steinberg::IBStream* state) -> bool {
    return this->loadComponentStateWithNotify(state);
});
```

### didOpen() Additions

```cpp
// In didOpen(), after existing timer setup:
if (presetManager_) {
    auto* frame = editor->getFrame();
    if (frame) {
        auto frameSize = frame->getViewSize();
        presetBrowserView_ = new Krate::Plugins::PresetBrowserView(
            frameSize, presetManager_.get(), getRuinaeTabLabels());
        frame->addView(presetBrowserView_);

        savePresetDialogView_ = new Krate::Plugins::SavePresetDialogView(
            frameSize, presetManager_.get());
        frame->addView(savePresetDialogView_);
    }
}
```

### willClose() Additions

```cpp
// In willClose(), BEFORE activeEditor_ = nullptr:
presetBrowserView_ = nullptr;
savePresetDialogView_ = nullptr;
```

## Editor UI Description (editor.uidesc)

### Top Bar Button Definitions

```xml
<!-- In the top bar CViewContainer (origin="0, 0" size="1400, 40") -->

<!-- Presets button -->
<view custom-view-name="PresetBrowserButton" origin="460, 8" size="80, 25"/>

<!-- Save button -->
<view custom-view-name="SavePresetButton" origin="550, 8" size="60, 25"/>
```

### createCustomView Handler

```cpp
VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/) {

    if (std::strcmp(name, "PresetBrowserButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(80, 25);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new PresetBrowserButton(rect, this);
    }

    if (std::strcmp(name, "SavePresetButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(60, 25);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new SavePresetButton(rect, this);
    }

    return nullptr;
}
```
