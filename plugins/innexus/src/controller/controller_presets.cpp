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
#include "plugin_ids.h"
#include "parameters/innexus_params.h"
#include "preset/innexus_preset_config.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"

#include "base/source/fstreamer.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstring>

namespace {

// ==============================================================================
// OutlineButton: Minimal outline-style button matching dark theme
// ==============================================================================
class OutlineButton : public VSTGUI::CView {
public:
    OutlineButton(const VSTGUI::CRect& size, std::string title,
                  const VSTGUI::CColor& frameColor = VSTGUI::CColor(64, 64, 72))
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

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read and validate version
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version) || version != 1)
        return false;

    // Lambda that calls editParamWithNotify instead of setParamNormalized
    auto setParam = [this](Steinberg::Vst::ParamID id, double value) {
        editParamWithNotify(id, value);
    };

    float floatVal = 0.0f;

    // --- M1 parameters ---
    if (streamer.readFloat(floatVal))
        setParam(kReleaseTimeId, releaseTimeToNormalized(
            std::clamp(floatVal, 20.0f, 5000.0f)));
    if (streamer.readFloat(floatVal))
        setParam(kInharmonicityAmountId,
            static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
    if (streamer.readFloat(floatVal))
        setParam(kMasterGainId,
            static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
    if (streamer.readFloat(floatVal))
        setParam(kBypassId, floatVal > 0.5f ? 1.0 : 0.0);

    // Skip file path (preset loading does not change sample)
    Steinberg::int32 pathLen = 0;
    if (streamer.readInt32(pathLen) && pathLen > 0 && pathLen < 4096) {
        // Skip path bytes
        for (Steinberg::int32 i = 0; i < pathLen; ++i) {
            Steinberg::int8 dummy = 0;
            streamer.readInt8(dummy);
        }
    }

    // --- M2 parameters ---
    if (streamer.readFloat(floatVal))
        setParam(kHarmonicLevelId,
            static_cast<double>(std::clamp(floatVal, 0.0f, 2.0f)) / 2.0);
    if (streamer.readFloat(floatVal))
        setParam(kResidualLevelId,
            static_cast<double>(std::clamp(floatVal, 0.0f, 2.0f)) / 2.0);
    if (streamer.readFloat(floatVal))
        setParam(kResidualBrightnessId,
            static_cast<double>(std::clamp(floatVal, -1.0f, 1.0f) + 1.0f) / 2.0);
    if (streamer.readFloat(floatVal))
        setParam(kTransientEmphasisId,
            static_cast<double>(std::clamp(floatVal, 0.0f, 2.0f)) / 2.0);

    // Skip residual frames
    {
        Steinberg::int32 residualFrameCount = 0;
        Steinberg::int32 fftSize = 0;
        Steinberg::int32 hopSize = 0;
        if (streamer.readInt32(residualFrameCount) &&
            streamer.readInt32(fftSize) &&
            streamer.readInt32(hopSize) &&
            residualFrameCount > 0) {
            for (Steinberg::int32 f = 0; f < residualFrameCount; ++f) {
                for (int b = 0; b < 16; ++b)
                    streamer.readFloat(floatVal);
                streamer.readFloat(floatVal); // totalEnergy
                Steinberg::int8 dummy = 0;
                streamer.readInt8(dummy); // transientFlag
            }
        }
    }

    // --- M3 parameters ---
    {
        Steinberg::int32 intVal = 0;
        if (streamer.readInt32(intVal))
            setParam(kInputSourceId, intVal > 0 ? 1.0 : 0.0);
        if (streamer.readInt32(intVal))
            setParam(kLatencyModeId, intVal > 0 ? 1.0 : 0.0);
    }

    // --- M4 parameters ---
    {
        Steinberg::int8 freezeState = 0;
        if (streamer.readInt8(freezeState))
            setParam(kFreezeId, freezeState ? 1.0 : 0.0);
        if (streamer.readFloat(floatVal))
            setParam(kMorphPositionId,
                static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
        Steinberg::int32 filterType = 0;
        if (streamer.readInt32(filterType))
            setParam(kHarmonicFilterTypeId,
                static_cast<double>(std::clamp(
                    static_cast<float>(filterType) / 4.0f, 0.0f, 1.0f)));
        if (streamer.readFloat(floatVal))
            setParam(kResponsivenessId,
                static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
    }

    // --- M5 parameters ---
    {
        Steinberg::int32 selectedSlot = 0;
        if (streamer.readInt32(selectedSlot)) {
            selectedSlot = std::clamp(selectedSlot,
                static_cast<Steinberg::int32>(0),
                static_cast<Steinberg::int32>(7));
            setParam(kMemorySlotId,
                std::clamp(static_cast<double>(selectedSlot) / 7.0, 0.0, 1.0));
        }

        // Skip 8 memory slots
        for (int s = 0; s < 8; ++s) {
            Steinberg::int8 occupiedByte = 0;
            if (!streamer.readInt8(occupiedByte))
                break;
            if (occupiedByte != 0) {
                float skipFloat = 0.0f;
                Steinberg::int32 skipInt = 0;
                streamer.readFloat(skipFloat);  // f0Reference
                streamer.readInt32(skipInt);     // numPartials
                for (int i = 0; i < 96 * 4; ++i)
                    streamer.readFloat(skipFloat);
                for (int i = 0; i < 16; ++i)
                    streamer.readFloat(skipFloat);
                streamer.readFloat(skipFloat);   // residualEnergy
                streamer.readFloat(skipFloat);   // globalAmplitude
                streamer.readFloat(skipFloat);   // spectralCentroid
                streamer.readFloat(skipFloat);   // brightness
            }
        }

        setParam(kMemoryCaptureId, 0.0);
        setParam(kMemoryRecallId, 0.0);
    }

    // --- M6 parameters ---
    {
        float m6Val = 0.0f;
        if (streamer.readFloat(m6Val))
            setParam(kTimbralBlendId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kStereoSpreadId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kEvolutionEnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParam(kEvolutionSpeedId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kEvolutionDepthId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kEvolutionModeId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod1EnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParam(kMod1WaveformId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod1RateId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod1DepthId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod1RangeStartId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod1RangeEndId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod1TargetId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod2EnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParam(kMod2WaveformId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod2RateId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod2DepthId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod2RangeStartId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod2RangeEndId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kMod2TargetId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kDetuneSpreadId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kBlendEnableId, m6Val > 0.5f ? 1.0 : 0.0);
        if (streamer.readFloat(m6Val))
            setParam(kBlendSlotWeight1Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kBlendSlotWeight2Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kBlendSlotWeight3Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kBlendSlotWeight4Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kBlendSlotWeight5Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kBlendSlotWeight6Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kBlendSlotWeight7Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kBlendSlotWeight8Id, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
        if (streamer.readFloat(m6Val))
            setParam(kBlendLiveWeightId, static_cast<double>(std::clamp(m6Val, 0.0f, 1.0f)));
    }

    // --- Harmonic Physics parameters ---
    {
        float physVal = 0.0f;
        if (streamer.readFloat(physVal))
            setParam(kWarmthId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
        if (streamer.readFloat(physVal))
            setParam(kCouplingId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
        if (streamer.readFloat(physVal))
            setParam(kStabilityId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
        if (streamer.readFloat(physVal))
            setParam(kEntropyId, static_cast<double>(std::clamp(physVal, 0.0f, 1.0f)));
    }

    // --- Analysis Feedback Loop parameters ---
    {
        float fbVal = 0.0f;
        if (streamer.readFloat(fbVal))
            setParam(kAnalysisFeedbackId, static_cast<double>(std::clamp(fbVal, 0.0f, 1.0f)));
        if (streamer.readFloat(fbVal))
            setParam(kAnalysisFeedbackDecayId, static_cast<double>(std::clamp(fbVal, 0.0f, 1.0f)));
    }

    // --- ADSR Envelope Detection parameters ---
    // Skip global ADSR (9 floats) + per-slot ADSR (8 x 9 = 72 floats) = 81 floats
    {
        float skipFloat = 0.0f;
        for (int i = 0; i < 81; ++i)
            streamer.readFloat(skipFloat);
    }

    // --- Partial Count parameter ---
    if (streamer.readFloat(floatVal))
        setParam(kPartialCountId,
            static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));

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

} // namespace Innexus
