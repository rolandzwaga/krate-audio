#pragma once

// ==============================================================================
// Gradus Controller — UI Thread Component
// ==============================================================================

#include "ui/arp_lane.h"
#include "ui/ring_data_bridge.h"
#include "preset/preset_manager.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <array>
#include <memory>

namespace Steinberg {
class MemoryStream;
}

namespace Krate::Plugins {
class PresetBrowserView;
class SavePresetDialogView;
} // namespace Krate::Plugins

namespace Gradus {
class RingDisplay;
class DetailStrip;
} // namespace Gradus

namespace Gradus {

static constexpr int kArpLaneCount = 8;

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
    Steinberg::tresult PLUGIN_API setParamNormalized(
        Steinberg::Vst::ParamID tag,
        Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;

    // --- IMessage handler ---
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message) override;

    // --- VST3EditorDelegate ---
    Steinberg::IPlugView* PLUGIN_API createView(
        Steinberg::FIDString name) override;
    VSTGUI::CView* createCustomView(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;
    VSTGUI::CView* verifyView(
        VSTGUI::CView* view,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;
    void didOpen(VSTGUI::VST3Editor* editor) override;
    void willClose(VSTGUI::VST3Editor* editor) override;

    // Arp lane management
    void constructArpLanes();
    void handleArpSkipEvent(int lane, int step);
    Krate::Plugins::IArpLane* getArpLane(int index);
    uint32_t getArpLaneStepBaseParamId(int index);
    uint32_t getArpLaneLengthParamId(int index);
    void onLaneCopy(int laneIndex);
    void onLanePaste(int targetLaneIndex);
    void wireCopyPasteCallbacks();

    // Preset management
    void openPresetBrowser();
    void closePresetBrowser();
    void openSavePresetDialog();
    Krate::Plugins::PresetManager* getPresetManager() { return presetManager_.get(); }
    Steinberg::MemoryStream* createComponentStateStream();
    bool loadComponentStateWithNotify(Steinberg::IBStream* state);
    void editParamWithNotify(Steinberg::Vst::ParamID id,
                             Steinberg::Vst::ParamValue value);

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

private:
    // Preset button factory (called from createCustomView)
    VSTGUI::CView* createPresetButton(const VSTGUI::CRect& rect, bool isBrowse);

    // Arp lane pointers (VSTGUI-owned, nulled in willClose)
    Krate::Plugins::IArpLane* velocityLane_ = nullptr;
    Krate::Plugins::IArpLane* gateLane_ = nullptr;
    Krate::Plugins::IArpLane* pitchLane_ = nullptr;
    Krate::Plugins::IArpLane* ratchetLane_ = nullptr;
    Krate::Plugins::IArpLane* modifierLane_ = nullptr;
    Krate::Plugins::IArpLane* conditionLane_ = nullptr;
    Krate::Plugins::IArpLane* chordLane_ = nullptr;
    Krate::Plugins::IArpLane* inversionLane_ = nullptr;

    // Circular ring display (VSTGUI-owned, nulled in willClose)
    RingDisplay* ringDisplay_ = nullptr;

    // Detail strip with lane tabs (VSTGUI-owned, nulled in willClose)
    DetailStrip* detailStrip_ = nullptr;

    // Ring data bridge (owned by controller, reads from IArpLane pointers)
    RingDataBridge ringDataBridge_;

    // v1.5: Per-lane swing knobs (captured in verifyView, toggled in selectLane)
    std::array<VSTGUI::CView*, 8> laneSwingKnobs_{};

    // Clipboard for copy/paste
    Krate::Plugins::LaneClipboard clipboard_{};

    // Preset manager
    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;
    Krate::Plugins::SavePresetDialogView* savePresetDialogView_ = nullptr;

    // Active editor
    VSTGUI::VST3Editor* activeEditor_ = nullptr;
};

} // namespace Gradus
