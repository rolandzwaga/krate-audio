// ==============================================================================
// KeyboardShortcutHandler Implementation
// ==============================================================================
// FR-010 to FR-016: Keyboard shortcuts for band navigation and parameter control
// ==============================================================================

#include "controller/keyboard_shortcut_handler.h"
#include "plugin_ids.h"

#include "vstgui/lib/controls/ccontrol.h"

namespace Disrumpo {

KeyboardShortcutHandler::KeyboardShortcutHandler(
    Steinberg::Vst::EditControllerEx1* controller,
    VSTGUI::CFrame* frame,
    int activeBandCount)
    : controller_(controller)
    , frame_(frame)
    , activeBandCount_(activeBandCount)
{
}

void KeyboardShortcutHandler::onKeyboardEvent(
    VSTGUI::KeyboardEvent& event, [[maybe_unused]] VSTGUI::CFrame* frame) {
    // FR-016: Only active when editor has keyboard focus from the host
    if (event.type != VSTGUI::EventType::KeyDown) {
        return;
    }

    bool handled = false;

    switch (event.virt) {
        case VSTGUI::VirtualKey::Tab:
            handled = handleTab(event);
            break;
        case VSTGUI::VirtualKey::Space:
            handled = handleSpace(event);
            break;
        case VSTGUI::VirtualKey::Up:
        case VSTGUI::VirtualKey::Down:
        case VSTGUI::VirtualKey::Left:
        case VSTGUI::VirtualKey::Right:
            handled = handleArrowKey(event);
            break;
        case VSTGUI::VirtualKey::Escape:
            handled = handleEscape(event);
            break;
        default:
            break;
    }

    if (handled) {
        event.consumed = true;
    }
}

void KeyboardShortcutHandler::setActiveBandCount(int count) {
    activeBandCount_ = count;
}

int KeyboardShortcutHandler::getFocusedBandIndex() const {
    return focusedBandIndex_;
}

void KeyboardShortcutHandler::setEscapeCallback(EscapeCallback callback) {
    escapeCallback_ = std::move(callback);
}

bool KeyboardShortcutHandler::handleTab(VSTGUI::KeyboardEvent& event) {
    bool reverse = event.modifiers.has(VSTGUI::ModifierKey::Shift);
    cycleBandFocus(reverse);
    return true;
}

bool KeyboardShortcutHandler::handleSpace(VSTGUI::KeyboardEvent& /*event*/) {
    if (focusedBandIndex_ >= 0 && focusedBandIndex_ < activeBandCount_) {
        toggleBandBypass(focusedBandIndex_);
        return true;
    }
    return false;
}

bool KeyboardShortcutHandler::handleArrowKey(VSTGUI::KeyboardEvent& event) {
    // Determine step fraction
    bool coarse = event.modifiers.has(VSTGUI::ModifierKey::Shift);
    float stepFraction = coarse ? 0.1f : 0.01f;

    // Direction: Up/Right = positive, Down/Left = negative
    if (event.virt == VSTGUI::VirtualKey::Down || event.virt == VSTGUI::VirtualKey::Left) {
        stepFraction = -stepFraction;
    }

    adjustFocusedParameter(stepFraction);
    return true;
}

bool KeyboardShortcutHandler::handleEscape(VSTGUI::KeyboardEvent& /*event*/) {
    if (escapeCallback_) {
        escapeCallback_();
        return true;
    }
    return false;
}

void KeyboardShortcutHandler::cycleBandFocus(bool reverse) {
    if (activeBandCount_ <= 0) return;

    // FR-010: Tab cycles through active bands (1 through N, wrapping)
    if (reverse) {
        // FR-011: Shift+Tab cycles in reverse
        focusedBandIndex_--;
        if (focusedBandIndex_ < 0) {
            focusedBandIndex_ = activeBandCount_ - 1;
        }
    } else {
        focusedBandIndex_++;
        if (focusedBandIndex_ >= activeBandCount_) {
            focusedBandIndex_ = 0;
        }
    }

    // Use CFrame focus drawing to show the focused band
    if (frame_) {
        frame_->advanceNextFocusView(frame_->getFocusView(), reverse);
    }
}

void KeyboardShortcutHandler::toggleBandBypass(int bandIndex) {
    if (!controller_ || bandIndex < 0 || bandIndex >= kMaxBands) return;

    // FR-012: Space toggles bypass on focused band
    auto paramId = makeBandParamId(static_cast<uint8_t>(bandIndex), BandParamType::kBandBypass);
    auto* param = controller_->getParameterObject(paramId);
    if (!param) return;

    double currentValue = param->getNormalized();
    double newValue = (currentValue >= 0.5) ? 0.0 : 1.0;

    controller_->beginEdit(paramId);
    controller_->setParamNormalized(paramId, newValue);
    controller_->performEdit(paramId, newValue);
    controller_->endEdit(paramId);
}

void KeyboardShortcutHandler::adjustFocusedParameter(float stepFraction) {
    if (!controller_ || !frame_) return;

    // Get the currently focused view
    auto* focusView = frame_->getFocusView();
    if (!focusView) return;

    auto* control = dynamic_cast<VSTGUI::CControl*>(focusView);
    if (!control) return;

    auto tag = control->getTag();
    if (tag < 0) return;

    auto paramId = static_cast<Steinberg::Vst::ParamID>(tag);
    auto* param = controller_->getParameterObject(paramId);
    if (!param) return;

    // FR-013/FR-014: Fine adjustment (1/100th range)
    // FR-015: Coarse adjustment (1/10th range)
    double currentValue = param->getNormalized();

    // For discrete parameters, use 1 step regardless of fraction
    auto info = param->getInfo();
    if (info.stepCount > 0) {
        double step = 1.0 / static_cast<double>(info.stepCount);
        double newValue = currentValue + (stepFraction > 0 ? step : -step);
        newValue = std::clamp(newValue, 0.0, 1.0);

        controller_->beginEdit(paramId);
        controller_->setParamNormalized(paramId, newValue);
        controller_->performEdit(paramId, newValue);
        controller_->endEdit(paramId);
    } else {
        double newValue = currentValue + static_cast<double>(stepFraction);
        newValue = std::clamp(newValue, 0.0, 1.0);

        controller_->beginEdit(paramId);
        controller_->setParamNormalized(paramId, newValue);
        controller_->performEdit(paramId, newValue);
        controller_->endEdit(paramId);
    }
}

} // namespace Disrumpo
