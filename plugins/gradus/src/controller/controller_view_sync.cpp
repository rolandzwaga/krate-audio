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
#include "../ui/pin_flag_strip.h"
#include "../ui/markov_matrix_editor.h"

#include "krate/dsp/processors/markov_matrices.h"
#include "krate/dsp/primitives/held_note_buffer.h"

#include "ui/arp_lane_editor.h"
#include "../ui/midi_delay_lane_editor.h"
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

    // --- v1.6: Pin flag strip (Pitch lane contextual) ---
    if (pinFlagStrip_) {
        if (tag >= kArpPinFlagStep0Id && tag <= kArpPinFlagStep31Id) {
            int stepIndex = static_cast<int>(tag - kArpPinFlagStep0Id);
            pinFlagStrip_->setStepValue(stepIndex, static_cast<float>(value));
        } else if (tag == kArpPitchLaneLengthId) {
            // Keep visible cell count aligned with the pitch lane's length.
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            pinFlagStrip_->setNumSteps(steps);
        }
    }

    // --- Arp mode changed → show/hide Markov matrix editor ---
    if (tag == kArpModeId && markovEditor_) {
        // 12 entries → Markov is index 11 → normalized 11/11 = 1.0
        const int modeIdx = std::clamp(static_cast<int>(value * 11.0 + 0.5), 0, 11);
        const bool showMarkov = (modeIdx == 11);
        markovEditor_->setVisible(showMarkov);
    }

    // --- Cell param changed → mirror to editor widget ---
    if (markovEditor_ && tag >= kArpMarkovCell00Id && tag <= kArpMarkovCell66Id) {
        const int flat = static_cast<int>(tag - kArpMarkovCell00Id);
        markovEditor_->setCellValueFlat(flat, static_cast<float>(value));
    }

    // --- Markov preset → batch-load matrix cells ---
    //
    // When the user picks a preset from the dropdown, rewrite the 49 cell
    // params with the hardcoded values. Skipped during state recall (the
    // saved cell values should win over the saved preset selection).
    //
    // If the preset is Custom (5) we leave the cells alone — Custom means
    // "whatever the current cells contain."
    if (tag == kArpMarkovPresetId) {
        const int presetIdx = std::clamp(
            static_cast<int>(value * 5.0 + 0.5), 0, 5);

        // Mirror into the editor's dropdown widget (any source: user click,
        // automation, cell-edit auto-flip-to-Custom, state recall).
        if (markovEditor_) {
            markovEditor_->setPresetValue(presetIdx);
        }

        // Batch-load preset cells when appropriate. Skipped during state
        // recall (saved cells should win) and skipped when switching TO
        // Custom (which should leave current cells alone).
        if (!suppressMarkovPresetLoad_ && presetIdx >= 0 && presetIdx <= 4) {
            const auto& matrix = Krate::DSP::getMarkovPresetMatrix(
                static_cast<Krate::DSP::MarkovPreset>(presetIdx));
            // Suppress the cell-edit echo during the batch write, otherwise
            // the first cell write would flip the preset back to Custom.
            suppressMarkovCellEcho_ = true;
            for (size_t i = 0; i < Krate::DSP::kMarkovMatrixSize; ++i) {
                const auto cellId =
                    static_cast<ParamID>(kArpMarkovCell00Id + static_cast<int>(i));
                beginEdit(cellId);
                setParamNormalized(cellId, static_cast<double>(matrix[i]));
                performEdit(cellId, static_cast<double>(matrix[i]));
                endEdit(cellId);
            }
            suppressMarkovCellEcho_ = false;
        }
    }

    // --- Cell edit → flip preset to Custom ---
    //
    // Any cell edit that doesn't come from a preset load should flip the
    // dropdown to "Custom" so the user knows the matrix no longer matches
    // any hardcoded preset.
    if (tag >= kArpMarkovCell00Id && tag <= kArpMarkovCell66Id &&
        !suppressMarkovCellEcho_ && !suppressMarkovPresetLoad_) {
        // Only flip if not already Custom (to avoid unnecessary churn).
        const double presetNorm = getParamNormalized(kArpMarkovPresetId);
        const int currentPreset = std::clamp(
            static_cast<int>(presetNorm * 5.0 + 0.5), 0, 5);
        if (currentPreset != 5) {
            constexpr double kCustomNormalized = 1.0;  // index 5 of 6 (stepCount=5)
            // Use the same suppress guard so the preset change doesn't trigger
            // a matrix reload that would overwrite the cell we just edited.
            suppressMarkovPresetLoad_ = true;
            beginEdit(kArpMarkovPresetId);
            setParamNormalized(kArpMarkovPresetId, kCustomNormalized);
            performEdit(kArpMarkovPresetId, kCustomNormalized);
            endEdit(kArpMarkovPresetId);
            suppressMarkovPresetLoad_ = false;
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
            if (view && view->isVisible()) {
                view->invalidRect(view->getViewSize());
            }
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
    if (tag == kArpMidiDelayPlayheadId) setPlayhead(midiDelayLane_, 8, value);

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
    syncLaneLength(kArpMidiDelayLaneLengthId, 8);

    // --- MIDI Delay lane length → update editor step count ---
    if (tag == kArpMidiDelayLaneLengthId && midiDelayLane_) {
        auto* p = getParameterObject(kArpMidiDelayLaneLengthId);
        if (p) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(p->getNormalized() * 31.0)), 1, 32);
            midiDelayLane_->setLength(steps);
        }
    }

    // --- MIDI Delay lane step sync ---
    if (auto* delayEditor = dynamic_cast<MidiDelayLaneEditor*>(
            static_cast<Krate::Plugins::IArpLane*>(midiDelayLane_))) {
        auto syncDelayRow = [&](uint32_t baseId, MidiDelayLaneEditor::KnobRow row) {
            if (tag >= baseId && tag < baseId + 32) {
                int stepIndex = static_cast<int>(tag - baseId);
                delayEditor->setStepValue(stepIndex, row, static_cast<float>(value));
            }
        };
        syncDelayRow(kArpMidiDelayActiveStep0Id,     MidiDelayLaneEditor::KnobRow::kActive);
        syncDelayRow(kArpMidiDelayTimeModeStep0Id,  MidiDelayLaneEditor::KnobRow::kTimeMode);
        syncDelayRow(kArpMidiDelayTimeStep0Id,       MidiDelayLaneEditor::KnobRow::kDelayTime);
        syncDelayRow(kArpMidiDelayFeedbackStep0Id,   MidiDelayLaneEditor::KnobRow::kFeedback);
        syncDelayRow(kArpMidiDelayVelDecayStep0Id,   MidiDelayLaneEditor::KnobRow::kVelDecay);
        syncDelayRow(kArpMidiDelayPitchShiftStep0Id, MidiDelayLaneEditor::KnobRow::kPitchShift);
        syncDelayRow(kArpMidiDelayGateScaleStep0Id,  MidiDelayLaneEditor::KnobRow::kGateScale);
    }

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
