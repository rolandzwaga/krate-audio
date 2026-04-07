// ==============================================================================
// Controller: Preset Browser & Preset Loading
// ==============================================================================

#include "controller.h"
#include "../plugin_ids.h"
#include "../parameters/arpeggiator_params.h"
#include "../ui/speed_curve_editor.h"
#include "../preset/gradus_preset_config.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstring>

namespace {

// ==============================================================================
// OutlineButton: Minimal outline-style button
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

    // Load arp params with edit notifications
    auto setParam = [this](Steinberg::Vst::ParamID id, double value) {
        editParamWithNotify(id, value);
    };
    loadArpParamsToController(streamer, setParam);

    // Speed curve point data follows in the stream (must consume to keep
    // stream position in sync, even though presets may not have this data).
    for (int lane = 0; lane < 8; ++lane) {
        Steinberg::int32 enabledInt = 0;
        if (!streamer.readInt32(enabledInt)) break;  // pre-curve preset

        Steinberg::int32 presetIdx = 0;
        if (!streamer.readInt32(presetIdx)) break;

        Steinberg::int32 numPoints = 0;
        if (!streamer.readInt32(numPoints)) break;
        numPoints = std::clamp(numPoints, Steinberg::int32{0}, Steinberg::int32{64});

        SpeedCurveData curve;
        curve.enabled = (enabledInt != 0);
        curve.presetIndex = presetIdx;
        curve.points.clear();
        curve.points.reserve(static_cast<size_t>(numPoints));

        bool ok = true;
        for (Steinberg::int32 p = 0; p < numPoints; ++p) {
            SpeedCurvePoint pt;
            if (!streamer.readFloat(pt.x) || !streamer.readFloat(pt.y) ||
                !streamer.readFloat(pt.cpLeftX) || !streamer.readFloat(pt.cpLeftY) ||
                !streamer.readFloat(pt.cpRightX) || !streamer.readFloat(pt.cpRightY)) {
                ok = false;
                break;
            }
            curve.points.push_back(pt);
        }
        if (!ok) break;

        auto laneIdx = static_cast<size_t>(lane);

        // Update speed curve editor if it exists
        if (speedCurveEditors_[laneIdx])
            speedCurveEditors_[laneIdx]->setCurveData(curve);

        // Send baked table to processor
        sendSpeedCurveTable(laneIdx, curve);
    }

    // MIDI Delay Lane parameters (after speed curve point data)
    [&]() {
        Steinberg::int32 iv = 0;
        float fv = 0.0f;

        if (!streamer.readInt32(iv)) return;
        editParamWithNotify(kArpMidiDelayLaneLengthId,
            static_cast<double>(std::clamp(static_cast<int>(iv), 1, 32) - 1) / 31.0);

        for (int i = 0; i < 32; ++i) {
            if (!streamer.readInt32(iv)) return;
            editParamWithNotify(
                static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayTimeModeStep0Id + i),
                iv ? 1.0 : 0.0);
        }
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readFloat(fv)) return;
            editParamWithNotify(
                static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayTimeStep0Id + i),
                static_cast<double>(std::clamp(fv, 0.0f, 1.0f)));
        }
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readInt32(iv)) return;
            editParamWithNotify(
                static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayFeedbackStep0Id + i),
                static_cast<double>(std::clamp(static_cast<int>(iv), 0, 16)) / 16.0);
        }
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readFloat(fv)) return;
            editParamWithNotify(
                static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayVelDecayStep0Id + i),
                static_cast<double>(std::clamp(fv, 0.0f, 1.0f)));
        }
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readInt32(iv)) return;
            editParamWithNotify(
                static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayPitchShiftStep0Id + i),
                static_cast<double>(std::clamp(static_cast<int>(iv), -24, 24) + 24) / 48.0);
        }
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readFloat(fv)) return;
            editParamWithNotify(
                static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayGateScaleStep0Id + i),
                static_cast<double>(std::clamp(fv, 0.1f, 2.0f) - 0.1f) / 1.9);
        }
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readInt32(iv)) return;
            editParamWithNotify(
                static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayActiveStep0Id + i),
                iv ? 1.0 : 0.0);
        }
        if (!streamer.readFloat(fv)) return;
        {
            float speed = std::clamp(fv, 0.25f, 4.0f);
            int bestIdx = 3;
            float bestDist = 99.0f;
            for (int i = 0; i < kLaneSpeedCount; ++i) {
                float dist = std::abs(kLaneSpeedValues[i] - speed);
                if (dist < bestDist) { bestDist = dist; bestIdx = i; }
            }
            editParamWithNotify(kArpMidiDelayLaneSpeedId,
                static_cast<double>(bestIdx) / static_cast<double>(kLaneSpeedCount - 1));
        }
        if (streamer.readFloat(fv))
            editParamWithNotify(kArpMidiDelayLaneSwingId,
                static_cast<double>(std::clamp(fv, 0.0f, 75.0f)) / 75.0);
        if (streamer.readInt32(iv))
            editParamWithNotify(kArpMidiDelayLaneJitterId,
                static_cast<double>(std::clamp(static_cast<int>(iv), 0, 4)) / 4.0);
        if (streamer.readFloat(fv))
            editParamWithNotify(kArpMidiDelayLaneSpeedCurveDepthId,
                static_cast<double>(std::clamp(fv, 0.0f, 1.0f)));
    }();

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
