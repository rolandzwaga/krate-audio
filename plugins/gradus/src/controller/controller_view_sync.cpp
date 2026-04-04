// ==============================================================================
// Controller: View Sync (setParamNormalized override)
// ==============================================================================
// Routes host parameter changes to lane views for UI sync.
// Without this, lane step/length changes from automation, undo, or preset
// loading wouldn't be reflected in the lane editors.
// ==============================================================================

#include "controller.h"
#include "../plugin_ids.h"
#include "../parameters/arpeggiator_params.h"
#include "../parameters/dropdown_mappings.h"
#include "../ui/ring_display.h"

#include "ui/arp_lane_editor.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Gradus {

tresult PLUGIN_API Controller::setParamNormalized(
    ParamID tag, ParamValue value)
{
    tresult result = EditControllerEx1::setParamNormalized(tag, value);

    // --- Velocity lane ---
    if (auto* lane = dynamic_cast<Krate::Plugins::ArpLaneEditor*>(
            static_cast<Krate::Plugins::IArpLane*>(velocityLane_))) {
        if (tag >= kArpVelocityLaneStep0Id && tag <= kArpVelocityLaneStep31Id) {
            int stepIndex = static_cast<int>(tag - kArpVelocityLaneStep0Id);
            lane->setStepLevel(stepIndex, static_cast<float>(value));
            lane->setDirty(true);
        } else if (tag == kArpVelocityLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            lane->setNumSteps(steps);
            lane->setDirty(true);
        }
    }

    // --- Gate lane ---
    if (auto* lane = dynamic_cast<Krate::Plugins::ArpLaneEditor*>(
            static_cast<Krate::Plugins::IArpLane*>(gateLane_))) {
        if (tag >= kArpGateLaneStep0Id && tag <= kArpGateLaneStep31Id) {
            int stepIndex = static_cast<int>(tag - kArpGateLaneStep0Id);
            lane->setStepLevel(stepIndex, static_cast<float>(value));
            lane->setDirty(true);
        } else if (tag == kArpGateLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            lane->setNumSteps(steps);
            lane->setDirty(true);
        }
    }

    // --- Pitch lane ---
    if (auto* lane = dynamic_cast<Krate::Plugins::ArpLaneEditor*>(
            static_cast<Krate::Plugins::IArpLane*>(pitchLane_))) {
        if (tag >= kArpPitchLaneStep0Id && tag < kArpPitchLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpPitchLaneStep0Id);
            lane->setStepLevel(stepIndex, static_cast<float>(value));
            lane->setDirty(true);
        } else if (tag == kArpPitchLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            lane->setNumSteps(steps);
            lane->setDirty(true);
        }
    }

    // --- Ratchet lane ---
    if (auto* lane = dynamic_cast<Krate::Plugins::ArpLaneEditor*>(
            static_cast<Krate::Plugins::IArpLane*>(ratchetLane_))) {
        if (tag >= kArpRatchetLaneStep0Id && tag < kArpRatchetLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpRatchetLaneStep0Id);
            lane->setStepLevel(stepIndex, static_cast<float>(value));
            lane->setDirty(true);
        } else if (tag == kArpRatchetLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            lane->setNumSteps(steps);
            lane->setDirty(true);
        }
    }

    // --- Modifier lane ---
    if (auto* lane = dynamic_cast<Krate::Plugins::ArpModifierLane*>(
            static_cast<Krate::Plugins::IArpLane*>(modifierLane_))) {
        if (tag >= kArpModifierLaneStep0Id && tag < kArpModifierLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpModifierLaneStep0Id);
            auto flags = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 255.0)), 0, 255));
            lane->setStepFlags(stepIndex, flags);
            lane->setDirty(true);
        } else if (tag == kArpModifierLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            lane->setNumSteps(steps);
            lane->setDirty(true);
        }
    }

    // --- Condition lane ---
    if (auto* lane = dynamic_cast<Krate::Plugins::ArpConditionLane*>(
            static_cast<Krate::Plugins::IArpLane*>(conditionLane_))) {
        if (tag >= kArpConditionLaneStep0Id && tag < kArpConditionLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpConditionLaneStep0Id);
            auto condIndex = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 17.0)), 0, 17));
            lane->setStepCondition(stepIndex, condIndex);
            lane->setDirty(true);
        } else if (tag == kArpConditionLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            lane->setNumSteps(steps);
            lane->setDirty(true);
        }
    }

    // --- Chord lane ---
    if (auto* lane = dynamic_cast<Krate::Plugins::ArpChordLane*>(
            static_cast<Krate::Plugins::IArpLane*>(chordLane_))) {
        if (tag >= kArpChordLaneStep0Id && tag < kArpChordLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpChordLaneStep0Id);
            auto chordIdx = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 4.0)), 0, 4));
            lane->setStepValue(stepIndex, chordIdx);
            lane->setDirty(true);
        } else if (tag == kArpChordLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            lane->setNumSteps(steps);
            lane->setDirty(true);
        }
    }

    // --- Inversion lane ---
    if (auto* lane = dynamic_cast<Krate::Plugins::ArpInversionLane*>(
            static_cast<Krate::Plugins::IArpLane*>(inversionLane_))) {
        if (tag >= kArpInversionLaneStep0Id && tag < kArpInversionLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpInversionLaneStep0Id);
            auto invIdx = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 3.0)), 0, 3));
            lane->setStepValue(stepIndex, invIdx); // NOLINT(readability-suspicious-call-argument)
            lane->setDirty(true);
        } else if (tag == kArpInversionLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            lane->setNumSteps(steps);
            lane->setDirty(true);
        }
    }

    // --- Playhead parameters → lane views + ring display ---
    auto setPlayhead = [this](Krate::Plugins::IArpLane* lane, int laneIndex,
                              double val) {
        int step = static_cast<int32_t>(val * 32.0);
        if (lane) {
            lane->setPlayheadStep(step);
            auto* view = lane->getView();
            if (view) view->invalid();
        }
        ringDataBridge_.setPlayheadStep(laneIndex, step);
    };

    if (tag == kArpVelocityPlayheadId)  setPlayhead(velocityLane_, 0, value);
    if (tag == kArpGatePlayheadId)      setPlayhead(gateLane_, 1, value);
    if (tag == kArpPitchPlayheadId)     setPlayhead(pitchLane_, 2, value);
    if (tag == kArpModifierPlayheadId)  setPlayhead(modifierLane_, 3, value);
    if (tag == kArpConditionPlayheadId) setPlayhead(conditionLane_, 4, value);
    if (tag == kArpRatchetPlayheadId)   setPlayhead(ratchetLane_, 5, value);
    if (tag == kArpChordPlayheadId)     setPlayhead(chordLane_, 6, value);
    if (tag == kArpInversionPlayheadId) setPlayhead(inversionLane_, 7, value);

    // --- Scale type → pitch lane (for popup suffix display) ---
    if (tag == kArpScaleTypeId) {
        if (auto* lane = dynamic_cast<Krate::Plugins::ArpLaneEditor*>(
                static_cast<Krate::Plugins::IArpLane*>(pitchLane_))) {
            int uiIndex = std::clamp(
                static_cast<int>(value * (kArpScaleTypeCount - 1) + 0.5),
                0, kArpScaleTypeCount - 1);
            int enumValue = kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)];
            lane->setScaleType(enumValue);
        }
    }

    // --- Per-lane speed multipliers → lane header display ---
    if (tag >= kArpVelocityLaneSpeedId && tag <= kArpInversionLaneSpeedId) {
        int idx = std::clamp(
            static_cast<int>(std::round(value * (kLaneSpeedCount - 1))), 0, kLaneSpeedCount - 1);
        float speed = kLaneSpeedValues[idx];

        Krate::Plugins::IArpLane* targetLane = nullptr;
        switch (tag) {
            case kArpVelocityLaneSpeedId:  targetLane = velocityLane_; break;
            case kArpGateLaneSpeedId:      targetLane = gateLane_; break;
            case kArpPitchLaneSpeedId:     targetLane = pitchLane_; break;
            case kArpModifierLaneSpeedId:  targetLane = modifierLane_; break;
            case kArpRatchetLaneSpeedId:   targetLane = ratchetLane_; break;
            case kArpConditionLaneSpeedId: targetLane = conditionLane_; break;
            case kArpChordLaneSpeedId:     targetLane = chordLane_; break;
            case kArpInversionLaneSpeedId: targetLane = inversionLane_; break;
            default: break;
        }
        if (targetLane) {
            targetLane->setSpeedMultiplier(speed);
            auto* view = targetLane->getView();
            if (view) view->invalid();
        }
    }

    // --- Euclidean parameters → ring data bridge ---
    if (tag == kArpEuclideanEnabledId || tag == kArpEuclideanHitsId ||
        tag == kArpEuclideanStepsId || tag == kArpEuclideanRotationId) {
        auto getParamInt = [this](ParamID id, double scale, int offset = 0) -> int {
            auto* p = getParameterObject(id);
            return p ? offset + static_cast<int>(std::round(p->getNormalized() * scale))
                     : 0;
        };
        ringDataBridge_.setEuclideanState(
            getParamInt(kArpEuclideanEnabledId, 1.0) > 0,
            getParamInt(kArpEuclideanHitsId, 32.0),
            std::clamp(getParamInt(kArpEuclideanStepsId, 30.0, 2), 2, 32),
            getParamInt(kArpEuclideanRotationId, 31.0));
    }

    // --- Lane length changes → update ring geometry step counts ---
    auto syncLaneLength = [this, tag](ParamID lengthId, int laneIndex) {
        if (tag == lengthId) {
            auto* p = getParameterObject(lengthId);
            if (p && ringDisplay_) {
                int steps = std::clamp(
                    static_cast<int>(1.0 + std::round(p->getNormalized() * 31.0)), 1, 32);
                ringDisplay_->getRenderer()->geometry().setLaneStepCount(
                    laneIndex, steps);
            }
        }
    };
    syncLaneLength(kArpVelocityLaneLengthId, 0);
    syncLaneLength(kArpGateLaneLengthId, 1);
    syncLaneLength(kArpPitchLaneLengthId, 2);
    syncLaneLength(kArpModifierLaneLengthId, 3);
    syncLaneLength(kArpConditionLaneLengthId, 4);
    syncLaneLength(kArpRatchetLaneLengthId, 5);
    syncLaneLength(kArpChordLaneLengthId, 6);
    syncLaneLength(kArpInversionLaneLengthId, 7);

    // --- Invalidate ring display for any arp parameter change ---
    if (ringDisplay_) {
        bool isArpParam = (tag >= 3001 && tag <= 3400);
        if (isArpParam) {
            auto* renderer = ringDisplay_->getRenderer();
            if (renderer) renderer->invalid();
        }
    }

    return result;
}

} // namespace Gradus
