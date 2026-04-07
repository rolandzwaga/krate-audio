// ==============================================================================
// Controller: Arp Interaction (Skip Events, Copy/Paste)
// ==============================================================================

#include "controller.h"
#include "../plugin_ids.h"
#include "ui/arp_lane_editor.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"

namespace Gradus {

void Controller::handleArpSkipEvent(int lane, int step) {
    if (lane < 0 || lane >= kArpLaneCount) return;
    if (step < 0 || step >= 32) return;

    Krate::Plugins::IArpLane* lanes[kArpLaneCount] = {
        velocityLane_, gateLane_, pitchLane_,
        ratchetLane_, modifierLane_, conditionLane_,
        chordLane_, inversionLane_, midiDelayLane_
    };

    auto* targetLane = lanes[lane];
    if (!targetLane) return;

    targetLane->setSkippedStep(static_cast<int32_t>(step));

    auto* view = targetLane->getView();
    if (view) {
        view->invalid();
    }
}

Krate::Plugins::IArpLane* Controller::getArpLane(int index) {
    Krate::Plugins::IArpLane* lanes[kArpLaneCount] = {
        velocityLane_, gateLane_, pitchLane_,
        ratchetLane_, modifierLane_, conditionLane_,
        chordLane_, inversionLane_, midiDelayLane_
    };
    if (index < 0 || index >= kArpLaneCount) return nullptr;
    return lanes[index];
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
uint32_t Controller::getArpLaneStepBaseParamId(int index) {
    static constexpr uint32_t kStepBaseIds[kArpLaneCount] = {
        kArpVelocityLaneStep0Id,
        kArpGateLaneStep0Id,
        kArpPitchLaneStep0Id,
        kArpRatchetLaneStep0Id,
        kArpModifierLaneStep0Id,
        kArpConditionLaneStep0Id,
        kArpChordLaneStep0Id,
        kArpInversionLaneStep0Id,
        kArpMidiDelayFeedbackStep0Id  // MIDI Delay: use feedback as primary step value
    };
    if (index < 0 || index >= kArpLaneCount) return 0;
    return kStepBaseIds[index];
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
uint32_t Controller::getArpLaneLengthParamId(int index) {
    static constexpr uint32_t kLengthIds[kArpLaneCount] = {
        kArpVelocityLaneLengthId,
        kArpGateLaneLengthId,
        kArpPitchLaneLengthId,
        kArpRatchetLaneLengthId,
        kArpModifierLaneLengthId,
        kArpConditionLaneLengthId,
        kArpChordLaneLengthId,
        kArpInversionLaneLengthId,
        kArpMidiDelayLaneLengthId
    };
    if (index < 0 || index >= kArpLaneCount) return 0;
    return kLengthIds[index];
}

void Controller::onLaneCopy(int laneIndex) {
    auto* lane = getArpLane(laneIndex);
    if (!lane) return;

    int32_t len = lane->getActiveLength();
    for (int32_t i = 0; i < len; ++i) {
        clipboard_.values[static_cast<size_t>(i)] = lane->getNormalizedStepValue(i);
    }
    clipboard_.length = len;
    clipboard_.sourceType = static_cast<Krate::Plugins::ClipboardLaneType>(
        lane->getLaneTypeId());
    clipboard_.hasData = true;

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

    for (int32_t i = 0; i < clipboard_.length; ++i) {
        uint32_t paramId = stepBaseId + static_cast<uint32_t>(i);
        float value = clipboard_.values[static_cast<size_t>(i)];

        beginEdit(paramId);
        performEdit(paramId, static_cast<double>(value));
        setParamNormalized(paramId, static_cast<double>(value));
        endEdit(paramId);

        lane->setNormalizedStepValue(i, value);
    }

    if (clipboard_.length != lane->getActiveLength()) {
        float normalizedLength = static_cast<float>(clipboard_.length - 1) / 31.0f;
        beginEdit(lengthParamId);
        performEdit(lengthParamId, static_cast<double>(normalizedLength));
        setParamNormalized(lengthParamId, static_cast<double>(normalizedLength));
        endEdit(lengthParamId);

        lane->setLength(clipboard_.length);
    }

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

} // namespace Gradus
