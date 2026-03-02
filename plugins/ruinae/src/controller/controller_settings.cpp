// ==============================================================================
// Controller: Settings Drawer & Tab Changed
// ==============================================================================
// Extracted from controller.cpp - handles toggleSettingsDrawer() and
// onTabChanged() which null cached view pointers on tab switch.
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "vstgui/lib/cvstguitimer.h"

namespace Ruinae {

void Controller::onTabChanged([[maybe_unused]] int newTab) {
    // UIViewSwitchContainer destroys views from the old template before
    // instantiating the new one. All cached pointers to views that live
    // inside tab templates become dangling. Null them here; verifyView()
    // will re-populate when the new template is created.

    // SOUND tab residents
    oscAPWKnob_ = nullptr;
    oscBPWKnob_ = nullptr;
    xyMorphPad_ = nullptr;
    polyGroup_ = nullptr;
    monoGroup_ = nullptr;
    ampEnvDisplay_ = nullptr;
    filterEnvDisplay_ = nullptr;
    modEnvDisplay_ = nullptr;

    // MOD tab residents
    modMatrixGrid_ = nullptr;
    ringIndicators_.fill(nullptr);
    lfo1RateGroup_ = nullptr;
    lfo2RateGroup_ = nullptr;
    lfo1NoteValueGroup_ = nullptr;
    lfo2NoteValueGroup_ = nullptr;
    chaosRateGroup_ = nullptr;
    chaosNoteValueGroup_ = nullptr;
    shRateGroup_ = nullptr;
    shNoteValueGroup_ = nullptr;
    randomRateGroup_ = nullptr;
    randomNoteValueGroup_ = nullptr;

    // FX tab residents
    harmonizerVoiceRows_.fill(nullptr);
    delayTimeGroup_ = nullptr;
    delayNoteValueGroup_ = nullptr;
    phaserRateGroup_ = nullptr;
    phaserNoteValueGroup_ = nullptr;

    // SEQ tab residents
    stepPatternEditor_ = nullptr;
    arpLaneContainer_ = nullptr;
    velocityLane_ = nullptr;
    gateLane_ = nullptr;
    pitchLane_ = nullptr;
    ratchetLane_ = nullptr;
    modifierLane_ = nullptr;
    conditionLane_ = nullptr;
    euclideanControlsGroup_ = nullptr;
    euclideanDotDisplay_ = nullptr;
    arpEuclideanGroup_ = nullptr;
    diceButton_ = nullptr;
    tranceGateRateGroup_ = nullptr;
    tranceGateNoteValueGroup_ = nullptr;
    arpRateGroup_ = nullptr;
    arpNoteValueGroup_ = nullptr;
    presetDropdown_ = nullptr;
}

void Controller::toggleSettingsDrawer() {
    settingsDrawerTargetOpen_ = !settingsDrawerTargetOpen_;

    // If timer already running (animation in progress), it will naturally
    // reverse direction because we changed the target. No need to restart.
    if (settingsAnimTimer_) return;

    settingsAnimTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
        [this](VSTGUI::CVSTGUITimer*) {
            constexpr float kAnimDuration = 0.16f;   // 160ms
            constexpr float kTimerInterval = 0.016f;  // ~60fps
            constexpr float kStep = kTimerInterval / kAnimDuration;

            if (settingsDrawerTargetOpen_) {
                settingsDrawerProgress_ = std::min(settingsDrawerProgress_ + kStep, 1.0f);
            } else {
                settingsDrawerProgress_ = std::max(settingsDrawerProgress_ - kStep, 0.0f);
            }

            // Ease-out curve: 1 - (1-t)^2
            float t = settingsDrawerProgress_;
            float eased = 1.0f - (1.0f - t) * (1.0f - t);

            // Map eased progress to x position
            constexpr float kClosedX = 1400.0f;
            constexpr float kOpenX = 1180.0f;
            float x = kClosedX + (kOpenX - kClosedX) * eased;

            if (settingsDrawer_) {
                VSTGUI::CRect r = settingsDrawer_->getViewSize();
                r.moveTo(VSTGUI::CPoint(x, 0));
                settingsDrawer_->setViewSize(r);
                settingsDrawer_->invalid();
            }

            // Check if animation is complete
            bool done = settingsDrawerTargetOpen_
                ? (settingsDrawerProgress_ >= 1.0f)
                : (settingsDrawerProgress_ <= 0.0f);

            if (done) {
                settingsDrawerOpen_ = settingsDrawerTargetOpen_;
                settingsAnimTimer_ = nullptr;

                // Show/hide overlay
                if (settingsOverlay_) {
                    settingsOverlay_->setVisible(settingsDrawerOpen_);
                }

                // Update gear button state
                if (gearButton_) {
                    gearButton_->setValue(settingsDrawerOpen_ ? 1.0f : 0.0f);
                    gearButton_->invalid();
                }
            }
        }, 16);  // ~60fps

    // Show overlay immediately when opening
    if (settingsDrawerTargetOpen_ && settingsOverlay_) {
        settingsOverlay_->setVisible(true);
    }
    // Hide overlay immediately when closing
    if (!settingsDrawerTargetOpen_ && settingsOverlay_) {
        settingsOverlay_->setVisible(false);
    }
}

} // namespace Ruinae
