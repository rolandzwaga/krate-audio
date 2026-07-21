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

// Gradus addresses its arp lanes in TWO different orders, and they disagree at
// indices 3/4/5:
//
//   lane-param order (this function, getArpLaneStepBaseParamId,
//   getArpLaneLengthParamId):
//       0 Vel  1 Gate  2 Pitch  3 Ratchet   4 Modifier   5 Condition  6 Chord  7 Inv  8 Delay
//   ring / UI order (subZoneToLaneIndex, ringDataBridge_.setLane,
//   RingRenderer::isBarTypeLane, kDepthParamIds):
//       0 Vel  1 Gate  2 Pitch  3 Modifier  4 Condition  5 Ratchet    6 Chord  7 Inv
//
// Anything fed a lane index by the ring renderer is in RING order and must go
// through getRingLaneStepBaseParamId(), never getArpLaneStepBaseParamId().
Krate::Plugins::IArpLane* Controller::getArpLane(int index) {
    Krate::Plugins::IArpLane* lanes[kArpLaneCount] = {
        velocityLane_, gateLane_, pitchLane_,
        ratchetLane_, modifierLane_, conditionLane_,
        chordLane_, inversionLane_, midiDelayLane_
    };
    if (index < 0 || index >= kArpLaneCount) return nullptr;
    return lanes[index];
}

uint32_t Controller::getRingLaneStepBaseParamId(int ringIndex) {
    // RING order -- note 3/4/5 vs the lane-param table in
    // getArpLaneStepBaseParamId(). The MIDI delay lane has no ring slot.
    static constexpr uint32_t kRingStepBaseIds[8] = {
        kArpVelocityLaneStep0Id,
        kArpGateLaneStep0Id,
        kArpPitchLaneStep0Id,
        kArpModifierLaneStep0Id,
        kArpConditionLaneStep0Id,
        kArpRatchetLaneStep0Id,
        kArpChordLaneStep0Id,
        kArpInversionLaneStep0Id
    };
    if (ringIndex < 0 || ringIndex >= 8) return 0;
    return kRingStepBaseIds[ringIndex];
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

// -----------------------------------------------------------------------------
// Spec 142: Sequencer Note lane (lane index 9 inside ArpeggiatorCore).
// -----------------------------------------------------------------------------
// The Sequencer Note lane has no `IArpLane` UI representation — it is edited
// via the PianoRollView (hosted inside a UIViewSwitchContainer) and its lane
// modulators (Speed/Swing/Jitter/SpeedCurveDepth) are bound directly via
// control-tag in editor.uidesc. These helper accessors expose the lane's
// per-step pitch base ID and the length param ID for code paths that want
// to walk all-lanes uniformly without bumping `kArpLaneCount` (which would
// add a tab to the ring/LaneTabBar — see FR-035: the ring view is unchanged).
//
// IDependent wiring for the lane's 64 step params + length + playhead lives
// inside `PianoRollView::attached()`. The lane-level modulator knobs use the
// standard control-tag binding path (no IDependent registration needed).
// -----------------------------------------------------------------------------

uint32_t Controller::getSequencerNoteLaneStepBaseParamId() noexcept {
    return kArpSequencerNoteLaneStep0Id;
}

uint32_t Controller::getSequencerNoteLaneLengthParamId() noexcept {
    return kArpSequencerNoteLaneLengthId;
}

} // namespace Gradus
