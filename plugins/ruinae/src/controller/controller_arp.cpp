// ==============================================================================
// Controller: Arp Interaction (Skip Events, Copy/Paste)
// ==============================================================================
// Extracted from controller.cpp - handles handleArpSkipEvent(), getArpLane(),
// getArpLaneStepBaseParamId(), getArpLaneLengthParamId(), onLaneCopy(),
// onLanePaste(), wireCopyPasteCallbacks().
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "ui/arp_lane_editor.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"

namespace Ruinae {

void Controller::handleArpSkipEvent(int lane, int step) {
    // 081-interaction-polish (FR-007, FR-008): route skip event to the lane
    if (lane < 0 || lane >= kArpLaneCount) return;
    if (step < 0 || step >= 32) return;

    // Map lane index to IArpLane pointer
    Krate::Plugins::IArpLane* lanes[kArpLaneCount] = {
        velocityLane_, gateLane_, pitchLane_,
        ratchetLane_, modifierLane_, conditionLane_,
        chordLane_, inversionLane_
    };

    auto* targetLane = lanes[lane];
    if (!targetLane) return;

    targetLane->setSkippedStep(static_cast<int32_t>(step));

    // Schedule repaint
    auto* view = targetLane->getView();
    if (view) {
        view->invalid();
    }
}

Krate::Plugins::IArpLane* Controller::getArpLane(int index) {
    Krate::Plugins::IArpLane* lanes[kArpLaneCount] = {
        velocityLane_, gateLane_, pitchLane_,
        ratchetLane_, modifierLane_, conditionLane_,
        chordLane_, inversionLane_
    };
    if (index < 0 || index >= kArpLaneCount) return nullptr;
    return lanes[index];
}

uint32_t Controller::getArpLaneStepBaseParamId(int index) {
    static constexpr uint32_t kStepBaseIds[8] = {
        kArpVelocityLaneStep0Id,
        kArpGateLaneStep0Id,
        kArpPitchLaneStep0Id,
        kArpRatchetLaneStep0Id,
        kArpModifierLaneStep0Id,
        kArpConditionLaneStep0Id,
        kArpChordLaneStep0Id,
        kArpInversionLaneStep0Id
    };
    if (index < 0 || index >= kArpLaneCount) return 0;
    return kStepBaseIds[index];
}

uint32_t Controller::getArpLaneLengthParamId(int index) {
    static constexpr uint32_t kLengthIds[8] = {
        kArpVelocityLaneLengthId,
        kArpGateLaneLengthId,
        kArpPitchLaneLengthId,
        kArpRatchetLaneLengthId,
        kArpModifierLaneLengthId,
        kArpConditionLaneLengthId,
        kArpChordLaneLengthId,
        kArpInversionLaneLengthId
    };
    if (index < 0 || index >= kArpLaneCount) return 0;
    return kLengthIds[index];
}

void Controller::onLaneCopy(int laneIndex) {
    auto* lane = getArpLane(laneIndex);
    if (!lane) return;

    // Read all step values as normalized floats
    int32_t len = lane->getActiveLength();
    for (int32_t i = 0; i < len; ++i) {
        clipboard_.values[static_cast<size_t>(i)] = lane->getNormalizedStepValue(i);
    }
    clipboard_.length = len;
    clipboard_.sourceType = static_cast<Krate::Plugins::ClipboardLaneType>(
        lane->getLaneTypeId());
    clipboard_.hasData = true;

    // Enable paste on all 6 lanes
    for (int i = 0; i < kArpLaneCount; ++i) {
        auto* l = getArpLane(i);
        if (l) l->setPasteEnabled(true);
    }
}

void Controller::onLanePaste(int targetLaneIndex) {
    if (!clipboard_.hasData) return;

    auto* lane = getArpLane(targetLaneIndex);
    if (!lane) return;

    uint32_t stepBaseId = getArpLaneStepBaseParamId(targetLaneIndex);
    uint32_t lengthParamId = getArpLaneLengthParamId(targetLaneIndex);
    if (stepBaseId == 0 || lengthParamId == 0) return;

    // Paste each step value with proper VST3 edit protocol for undo support
    for (int32_t i = 0; i < clipboard_.length; ++i) {
        uint32_t paramId = stepBaseId + static_cast<uint32_t>(i);
        float value = clipboard_.values[static_cast<size_t>(i)];

        beginEdit(paramId);
        performEdit(paramId, static_cast<double>(value));
        setParamNormalized(paramId, static_cast<double>(value));
        endEdit(paramId);

        // Update lane visual state
        lane->setNormalizedStepValue(i, value);
    }

    // Update target lane length to match clipboard length
    if (clipboard_.length != lane->getActiveLength()) {
        float normalizedLength = static_cast<float>(clipboard_.length - 1) / 31.0f;
        beginEdit(lengthParamId);
        performEdit(lengthParamId, static_cast<double>(normalizedLength));
        setParamNormalized(lengthParamId, static_cast<double>(normalizedLength));
        endEdit(lengthParamId);

        lane->setLength(clipboard_.length);
    }

    // Schedule repaint
    auto* view = lane->getView();
    if (view) {
        view->invalid();
    }
}

void Controller::wireCopyPasteCallbacks() {
    for (int i = 0; i < kArpLaneCount; ++i) {
        auto* lane = getArpLane(i);
        if (!lane) continue;

        int laneIdx = i;
        lane->setCopyPasteCallbacks(
            [this, laneIdx]() { onLaneCopy(laneIdx); },
            [this, laneIdx]() { onLanePaste(laneIdx); }
        );
        lane->setPasteEnabled(clipboard_.hasData);
    }
}

} // namespace Ruinae
