// ==============================================================================
// Controller: View Wiring (verifyView + constructArpLanes)
// ==============================================================================
// verifyView() captures pointers to RingDisplay and DetailStrip as they are
// created from the uidesc. Lane construction happens in constructArpLanes(),
// called from didOpen() after ALL views exist.
// ==============================================================================

#include "controller.h"
#include "../plugin_ids.h"
#include "../parameters/arpeggiator_params.h"
#include "../parameters/dropdown_mappings.h"
#include "../ui/ring_display.h"
#include "../ui/detail_strip.h"
#include "../ui/pin_flag_strip.h"
#include "../ui/markov_matrix_editor.h"
#include "../ui/speed_curve_editor.h"
#include "ui/arp_lane_editor.h"

#include "ui/toggle_button.h"
#include "ui/arc_knob.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cfont.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"
#include "../ui/midi_delay_lane_editor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <random>

namespace {
// Lightweight IControlListener that forwards to a std::function
class LambdaListener : public VSTGUI::IControlListener {
public:
    using Callback = std::function<void(VSTGUI::CControl*, float)>;
    explicit LambdaListener(Callback cb) : cb_(std::move(cb)) {}
    void valueChanged(VSTGUI::CControl* control) override {
        if (cb_) cb_(control, control->getValueNormalized());
    }
private:
    Callback cb_;
};
} // anonymous namespace

namespace Gradus {

// ==============================================================================
// verifyView — Capture view pointers only
// ==============================================================================

VSTGUI::CView* Controller::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/) {

    // Capture RingDisplay pointer (created by createCustomView)
    if (auto* ringDisp = dynamic_cast<RingDisplay*>(view))
        ringDisplay_ = ringDisp;

    // Capture DetailStrip pointer (created by createCustomView)
    if (auto* strip = dynamic_cast<DetailStrip*>(view))
        detailStrip_ = strip;

    // Capture contextual labels by custom-view-name (no control-tag available)
    if (const auto* nameAttr = attributes.getAttributeValue("custom-view-name")) {
        const std::string& name = *nameAttr;
        if (name == "RatchetDecayLabel") {
            ratchetDecayLabel_ = view;
            view->setVisible(false);
        } else if (name == "RatchetSubSwingLabel") {
            ratchetSubSwingLabel_ = view;
            view->setVisible(false);
        } else if (name == "StrumTimeLabel") {
            strumTimeLabel_ = view;
            view->setVisible(false);
        } else if (name == "StrumDirectionLabel") {
            strumDirectionLabel_ = view;
            view->setVisible(false);
        } else if (name == "VelCurveLabel") {
            velCurveLabel_ = view;
            view->setVisible(false);
        } else if (name == "VelCurveTypeLabel") {
            velCurveTypeLabel_ = view;
            view->setVisible(false);
        } else if (name == "TransposeLabel") {
            transposeLabel_ = view;
            view->setVisible(false);
        } else if (name == "PinNoteLabel") {
            pinNoteLabel_ = view;
            view->setVisible(false);
        } else if (name == "RangeLoLabel") {
            rangeLowLabel_ = view;
            view->setVisible(false);
        } else if (name == "RangeHiLabel") {
            rangeHighLabel_ = view;
            view->setVisible(false);
        }
    }

    // Capture per-lane swing knobs by control-tag
    if (auto* ctrl = dynamic_cast<VSTGUI::CControl*>(view)) {
        int32_t tag = ctrl->getTag();
        // Map param order (Vel, Gate, Pitch, Mod, Ratchet, Cond, Chord, Inv)
        // to UI lane index (0=Vel, 1=Gate, 2=Pitch, 3=Mod, 4=Cond, 5=Ratchet, 6=Chord, 7=Inv)
        static constexpr int kParamToLane[8] = {0, 1, 2, 3, 5, 4, 6, 7};

        if (tag >= kArpVelocityLaneSwingId && tag <= kArpInversionLaneSwingId) {
            int laneIdx = static_cast<int>(tag - kArpVelocityLaneSwingId);
            int uiLane = kParamToLane[laneIdx];
            if (uiLane >= 0 && uiLane < 8) {
                laneSwingKnobs_[uiLane] = view;
                view->setVisible(uiLane == 0);
            }
        }
        // v1.5 Part 2: Per-lane length jitter knobs
        if (tag >= kArpVelocityLaneJitterId && tag <= kArpInversionLaneJitterId) {
            int laneIdx = static_cast<int>(tag - kArpVelocityLaneJitterId);
            int uiLane = kParamToLane[laneIdx];
            if (uiLane >= 0 && uiLane < 8) {
                laneJitterKnobs_[uiLane] = view;
                view->setVisible(uiLane == 0);
            }
        }
        // v1.5: Ratchet Decay knob — only visible when Ratchet lane (5) selected
        if (tag == kArpRatchetDecayId) {
            ratchetDecayKnob_ = view;
            view->setVisible(false); // hidden until Ratchet tab selected
        }
        // Ratchet sub-step shuffle — also Ratchet lane only
        if (tag == kArpRatchetSwingId) {
            ratchetSubSwingKnob_ = view;
            view->setVisible(false);
        }
        // v1.5: Strum controls — only visible when Chord (6) or Inversion (7) selected
        if (tag == kArpStrumTimeId) {
            strumTimeKnob_ = view;
            view->setVisible(false);
        }
        if (tag == kArpStrumDirectionId) {
            strumDirectionMenu_ = view;
            view->setVisible(false);
        }
        // v1.5 Part 2: Velocity Curve controls — only visible when Velocity (0) selected
        if (tag == kArpVelocityCurveAmountId) {
            velCurveKnob_ = view;
            view->setVisible(false);
        }
        if (tag == kArpVelocityCurveTypeId) {
            velCurveTypeMenu_ = view;
            view->setVisible(false);
        }
        // v1.5 Part 3: Pitch-lane contextual controls — only visible when Pitch (2) selected
        if (tag == kArpTransposeId) {
            transposeKnob_ = view;
            view->setVisible(false);
        }
        if (tag == kArpPinNoteId) {
            pinNoteKnob_ = view;
            view->setVisible(false);
        }
        if (tag == kArpRangeLowId) {
            rangeLowKnob_ = view;
            view->setVisible(false);
        }
        if (tag == kArpRangeHighId) {
            rangeHighKnob_ = view;
            view->setVisible(false);
        }
        if (tag == kArpRangeModeId) {
            rangeModeMenu_ = view;
            view->setVisible(false);
        }
        // (Speed curve depth knobs are created programmatically in constructArpLanes)
    }

    return view;
}

// ==============================================================================
// constructArpLanes — Called from didOpen() after all views exist
// ==============================================================================

void Controller::constructArpLanes()
{
    if (!ringDisplay_ || !detailStrip_) return;

    // Helper: wire speed param for any lane type
    auto wireSpeedParam = [this](Krate::Plugins::IArpLane* lane, uint32_t speedParamId) {
        lane->setSpeedParamId(speedParamId);
        lane->setSpeedParamCallback(
            [this](uint32_t paramId, float normalizedValue) {
                beginEdit(paramId);
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
                endEdit(paramId);
            });
        auto* param = getParameterObject(speedParamId);
        if (param) {
            double norm = param->getNormalized();
            if (norm > 0.01) {
                int idx = std::clamp(
                    static_cast<int>(std::round(norm * 9.0)), 0, 9);
                lane->setSpeedMultiplier(kLaneSpeedValues[idx]);
            }
        }
    };

    // Helper: wire standard callbacks for a bar-type lane
    auto wireLaneCallbacks = [this](Krate::Plugins::ArpLaneEditor* lane) {
        lane->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        lane->setBeginEditCallback(
            [this](uint32_t paramId) { beginEdit(paramId); });
        lane->setEndEditCallback(
            [this](uint32_t paramId) { endEdit(paramId); });
        lane->setLengthParamCallback(
            [this](uint32_t paramId, float normalizedValue) {
                beginEdit(paramId);
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
                endEdit(paramId);
            });
    };

    // Helper: sync bar lane steps from current parameters
    auto syncBarLane = [this](Krate::Plugins::ArpLaneEditor* lane,
                               uint32_t stepBaseId, uint32_t lengthId) {
        for (int i = 0; i < 32; ++i) {
            auto paramId = static_cast<Steinberg::Vst::ParamID>(stepBaseId + i);
            auto* paramObj = getParameterObject(paramId);
            if (paramObj) {
                lane->setStepLevel(i,
                    static_cast<float>(paramObj->getNormalized()));
            }
        }
        auto* lenParam = getParameterObject(lengthId);
        if (lenParam) {
            double val = lenParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
            lane->setNumSteps(steps);
        }
    };

    // ========================================================================
    // Construct velocity lane
    // ========================================================================
    auto* velLane = new Krate::Plugins::ArpLaneEditor(
        VSTGUI::CRect(0, 0, 500, 105), nullptr, -1);
    velLane->setLaneName("VEL");
    velLane->setLaneType(Krate::Plugins::ArpLaneType::kVelocity);
    velLane->setAccentColor(VSTGUI::CColor{208, 132, 92, 255});
    velLane->setDisplayRange(0.0f, 1.0f, "1.0", "0.0");
    velLane->setStepLevelBaseParamId(kArpVelocityLaneStep0Id);
    velLane->setLengthParamId(kArpVelocityLaneLengthId);
    velLane->setPlayheadParamId(kArpVelocityPlayheadId);
    wireLaneCallbacks(velLane);
    syncBarLane(velLane, kArpVelocityLaneStep0Id, kArpVelocityLaneLengthId);
    wireSpeedParam(velLane, kArpVelocityLaneSpeedId);
    velocityLane_ = velLane;
    detailStrip_->setLane(0, velLane);

    // ========================================================================
    // Construct gate lane
    // ========================================================================
    auto* gLane = new Krate::Plugins::ArpLaneEditor(
        VSTGUI::CRect(0, 0, 500, 105), nullptr, -1);
    gLane->setLaneName("GATE");
    gLane->setLaneType(Krate::Plugins::ArpLaneType::kGate);
    gLane->setAccentColor(VSTGUI::CColor{200, 164, 100, 255});
    gLane->setDisplayRange(0.0f, 2.0f, "200%", "0%");
    gLane->setStepLevelBaseParamId(kArpGateLaneStep0Id);
    gLane->setLengthParamId(kArpGateLaneLengthId);
    gLane->setPlayheadParamId(kArpGatePlayheadId);
    wireLaneCallbacks(gLane);
    syncBarLane(gLane, kArpGateLaneStep0Id, kArpGateLaneLengthId);
    wireSpeedParam(gLane, kArpGateLaneSpeedId);
    gateLane_ = gLane;
    detailStrip_->setLane(1, gLane);

    // ========================================================================
    // Construct pitch lane
    // ========================================================================
    auto* pLane = new Krate::Plugins::ArpLaneEditor(
        VSTGUI::CRect(0, 0, 500, 105), nullptr, -1);
    pLane->setLaneName("PITCH");
    pLane->setLaneType(Krate::Plugins::ArpLaneType::kPitch);
    pLane->setAccentColor(VSTGUI::CColor{108, 168, 160, 255});
    pLane->setDisplayRange(-24.0f, 24.0f, "+24", "-24");
    pLane->setStepLevelBaseParamId(kArpPitchLaneStep0Id);
    pLane->setLengthParamId(kArpPitchLaneLengthId);
    pLane->setPlayheadParamId(kArpPitchPlayheadId);
    wireLaneCallbacks(pLane);
    syncBarLane(pLane, kArpPitchLaneStep0Id, kArpPitchLaneLengthId);
    {
        auto* scaleParam = getParameterObject(kArpScaleTypeId);
        if (scaleParam) {
            double scaleNorm = scaleParam->getNormalized();
            int uiIndex = std::clamp(
                static_cast<int>(scaleNorm * (kArpScaleTypeCount - 1) + 0.5),
                0, kArpScaleTypeCount - 1);
            int enumValue = kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)];
            pLane->setScaleType(enumValue);
        }
    }
    wireSpeedParam(pLane, kArpPitchLaneSpeedId);
    pitchLane_ = pLane;
    detailStrip_->setLane(2, pLane);

    // ========================================================================
    // Construct chord lane
    // ========================================================================
    auto* chdLane = new Krate::Plugins::ArpChordLane(
        VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    chdLane->setLaneName("CHORD");
    chdLane->setAccentColor(VSTGUI::CColor{168, 136, 200, 255});
    chdLane->setStepBaseParamId(kArpChordLaneStep0Id);
    chdLane->setLengthParamId(kArpChordLaneLengthId);
    chdLane->setPlayheadParamId(kArpChordPlayheadId);
    chdLane->setParameterCallback(
        [this](uint32_t paramId, float normalizedValue) {
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
        });
    chdLane->setBeginEditCallback(
        [this](uint32_t paramId) { beginEdit(paramId); });
    chdLane->setEndEditCallback(
        [this](uint32_t paramId) { endEdit(paramId); });
    chdLane->setLengthParamCallback(
        [this](uint32_t paramId, float normalizedValue) {
            beginEdit(paramId);
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
            endEdit(paramId);
        });
    for (int i = 0; i < 32; ++i) {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            kArpChordLaneStep0Id + i);
        auto* paramObj = getParameterObject(paramId);
        if (paramObj) {
            float normalized = static_cast<float>(paramObj->getNormalized());
            auto chordIdx = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(normalized * 4.0f)), 0, 4));
            chdLane->setStepValue(i, chordIdx);
        }
    }
    {
        auto* lenParam = getParameterObject(kArpChordLaneLengthId);
        if (lenParam) {
            double val = lenParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
            chdLane->setNumSteps(steps);
        }
    }
    wireSpeedParam(chdLane, kArpChordLaneSpeedId);
    chordLane_ = chdLane;
    detailStrip_->setLane(6, chdLane);

    // ========================================================================
    // Construct inversion lane
    // ========================================================================
    auto* invLane = new Krate::Plugins::ArpInversionLane(
        VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    invLane->setLaneName("INV");
    invLane->setAccentColor(VSTGUI::CColor{136, 168, 200, 255});
    invLane->setStepBaseParamId(kArpInversionLaneStep0Id);
    invLane->setLengthParamId(kArpInversionLaneLengthId);
    invLane->setPlayheadParamId(kArpInversionPlayheadId);
    invLane->setParameterCallback(
        [this](uint32_t paramId, float normalizedValue) {
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
        });
    invLane->setBeginEditCallback(
        [this](uint32_t paramId) { beginEdit(paramId); });
    invLane->setEndEditCallback(
        [this](uint32_t paramId) { endEdit(paramId); });
    invLane->setLengthParamCallback(
        [this](uint32_t paramId, float normalizedValue) {
            beginEdit(paramId);
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
            endEdit(paramId);
        });
    for (int i = 0; i < 32; ++i) {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            kArpInversionLaneStep0Id + i);
        auto* paramObj = getParameterObject(paramId);
        if (paramObj) {
            float normalized = static_cast<float>(paramObj->getNormalized());
            auto invIdx = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(normalized * 3.0f)), 0, 3));
            invLane->setStepValue(i, invIdx);
        }
    }
    {
        auto* lenParam = getParameterObject(kArpInversionLaneLengthId);
        if (lenParam) {
            double val = lenParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
            invLane->setNumSteps(steps);
        }
    }
    wireSpeedParam(invLane, kArpInversionLaneSpeedId);
    inversionLane_ = invLane;
    detailStrip_->setLane(7, invLane);

    // ========================================================================
    // Construct MIDI delay lane (multi-knob grid editor)
    // ========================================================================
    auto* delayLane = new MidiDelayLaneEditor(
        VSTGUI::CRect(0, 0, 500, 400));
    delayLane->setLengthParamId(kArpMidiDelayLaneLengthId);
    delayLane->setParameterCallback(
        [this](uint32_t paramId, float normalizedValue) {
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
        });
    delayLane->setBeginEditCallback(
        [this](uint32_t paramId) { beginEdit(paramId); });
    delayLane->setEndEditCallback(
        [this](uint32_t paramId) { endEdit(paramId); });
    delayLane->setLengthParamCallback(
        [this](uint32_t paramId, float normalizedValue) {
            beginEdit(paramId);
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
            endEdit(paramId);
        });

    // Sync all 6 per-step parameter rows from current host state
    for (int step = 0; step < 32; ++step) {
        auto syncRow = [&](MidiDelayLaneEditor::KnobRow row, uint32_t baseId) {
            auto pid = static_cast<Steinberg::Vst::ParamID>(baseId + step);
            auto* param = getParameterObject(pid);
            if (param) {
                float val = static_cast<float>(param->getNormalized());
                delayLane->setStepValue(step, row, val);
            }
        };
        syncRow(MidiDelayLaneEditor::KnobRow::kActive,    kArpMidiDelayActiveStep0Id);
        syncRow(MidiDelayLaneEditor::KnobRow::kTimeMode,  kArpMidiDelayTimeModeStep0Id);
        syncRow(MidiDelayLaneEditor::KnobRow::kDelayTime,  kArpMidiDelayTimeStep0Id);
        syncRow(MidiDelayLaneEditor::KnobRow::kFeedback,   kArpMidiDelayFeedbackStep0Id);
        syncRow(MidiDelayLaneEditor::KnobRow::kVelDecay,   kArpMidiDelayVelDecayStep0Id);
        syncRow(MidiDelayLaneEditor::KnobRow::kPitchShift, kArpMidiDelayPitchShiftStep0Id);
        syncRow(MidiDelayLaneEditor::KnobRow::kGateScale,  kArpMidiDelayGateScaleStep0Id);
    }
    wireSpeedParam(delayLane, kArpMidiDelayLaneSpeedId);
    midiDelayLane_ = delayLane;
    detailStrip_->setLane(8, delayLane);

    // Sync lane length AFTER setLane so the view has its final size
    // (rebuildControls needs the actual dimensions to position knobs)
    {
        auto* lenParam = getParameterObject(kArpMidiDelayLaneLengthId);
        if (lenParam) {
            double val = lenParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
            delayLane->setNumSteps(steps);
        } else {
            delayLane->setNumSteps(16);  // trigger rebuildControls with final size
        }
    }

    // ========================================================================
    // Construct ratchet lane
    // ========================================================================
    auto* ratLane = new Krate::Plugins::ArpLaneEditor(
        VSTGUI::CRect(0, 0, 500, 105), nullptr, -1);
    ratLane->setLaneName("RATCH");
    ratLane->setLaneType(Krate::Plugins::ArpLaneType::kRatchet);
    ratLane->setAccentColor(VSTGUI::CColor{152, 128, 176, 255});
    ratLane->setDisplayRange(1.0f, 4.0f, "4", "1");
    ratLane->setStepLevelBaseParamId(kArpRatchetLaneStep0Id);
    ratLane->setLengthParamId(kArpRatchetLaneLengthId);
    ratLane->setPlayheadParamId(kArpRatchetPlayheadId);
    wireLaneCallbacks(ratLane);
    syncBarLane(ratLane, kArpRatchetLaneStep0Id, kArpRatchetLaneLengthId);
    wireSpeedParam(ratLane, kArpRatchetLaneSpeedId);
    ratchetLane_ = ratLane;
    detailStrip_->setLane(5, ratLane);

    // ========================================================================
    // Construct modifier lane
    // ========================================================================
    auto* modLane = new Krate::Plugins::ArpModifierLane(
        VSTGUI::CRect(0, 0, 500, 79), nullptr, -1);
    modLane->setLaneName("MOD");
    modLane->setAccentColor(VSTGUI::CColor{192, 112, 124, 255});
    modLane->setStepFlagBaseParamId(kArpModifierLaneStep0Id);
    modLane->setLengthParamId(kArpModifierLaneLengthId);
    modLane->setPlayheadParamId(kArpModifierPlayheadId);
    modLane->setParameterCallback(
        [this](uint32_t paramId, float normalizedValue) {
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
        });
    modLane->setBeginEditCallback(
        [this](uint32_t paramId) { beginEdit(paramId); });
    modLane->setEndEditCallback(
        [this](uint32_t paramId) { endEdit(paramId); });
    modLane->setLengthParamCallback(
        [this](uint32_t paramId, float normalizedValue) {
            beginEdit(paramId);
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
            endEdit(paramId);
        });
    for (int i = 0; i < 32; ++i) {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            kArpModifierLaneStep0Id + i);
        auto* paramObj = getParameterObject(paramId);
        if (paramObj) {
            float normalized = static_cast<float>(paramObj->getNormalized());
            auto flags = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(normalized * 255.0f)), 0, 255));
            modLane->setStepFlags(i, flags);
        }
    }
    auto* modLenParam = getParameterObject(kArpModifierLaneLengthId);
    if (modLenParam) {
        double val = modLenParam->getNormalized();
        int steps = std::clamp(
            static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
        modLane->setNumSteps(steps);
    }
    wireSpeedParam(modLane, kArpModifierLaneSpeedId);
    modifierLane_ = modLane;
    detailStrip_->setLane(3, modLane);

    // ========================================================================
    // Construct condition lane
    // ========================================================================
    auto* condLane = new Krate::Plugins::ArpConditionLane(
        VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    condLane->setLaneName("COND");
    condLane->setAccentColor(VSTGUI::CColor{124, 144, 176, 255});
    condLane->setStepConditionBaseParamId(kArpConditionLaneStep0Id);
    condLane->setLengthParamId(kArpConditionLaneLengthId);
    condLane->setPlayheadParamId(kArpConditionPlayheadId);
    condLane->setParameterCallback(
        [this](uint32_t paramId, float normalizedValue) {
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
        });
    condLane->setBeginEditCallback(
        [this](uint32_t paramId) { beginEdit(paramId); });
    condLane->setEndEditCallback(
        [this](uint32_t paramId) { endEdit(paramId); });
    condLane->setLengthParamCallback(
        [this](uint32_t paramId, float normalizedValue) {
            beginEdit(paramId);
            setParamNormalized(paramId, static_cast<double>(normalizedValue));
            performEdit(paramId, static_cast<double>(normalizedValue));
            endEdit(paramId);
        });
    for (int i = 0; i < 32; ++i) {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            kArpConditionLaneStep0Id + i);
        auto* paramObj = getParameterObject(paramId);
        if (paramObj) {
            float normalized = static_cast<float>(paramObj->getNormalized());
            auto condIndex = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(normalized * 17.0f)), 0, 17));
            condLane->setStepCondition(i, condIndex);
        }
    }
    auto* condLenParam = getParameterObject(kArpConditionLaneLengthId);
    if (condLenParam) {
        double val = condLenParam->getNormalized();
        int steps = std::clamp(
            static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
        condLane->setNumSteps(steps);
    }
    wireSpeedParam(condLane, kArpConditionLaneSpeedId);
    conditionLane_ = condLane;
    detailStrip_->setLane(4, condLane);

    // ========================================================================
    // v1.6: Construct Pin Flag Strip (Pitch lane contextual)
    // ========================================================================
    // 32-cell toggle row placed in DetailStrip's reserved pin-row slot,
    // immediately above the lane editors (which DetailStrip::setLane has
    // already positioned below kHeaderHeight + kPinRowHeight).
    //
    // Horizontal frame matches the pitch lane's *bar area*, not the full
    // lane width: the left edge is inset by ArpLaneEditor::kStepContentLeftMargin
    // (40px) so each pin cell lines up column-by-column with the pitch bar
    // underneath. Only visible when Pitch lane is active.
    {
        const auto stripSize = detailStrip_->getViewSize();
        const VSTGUI::CCoord stripWidth = stripSize.getWidth();
        const VSTGUI::CCoord pinTop    = DetailStrip::pinRowTop();
        const VSTGUI::CCoord pinBottom = pinTop + DetailStrip::pinRowHeight();
        const auto pinLeft = static_cast<VSTGUI::CCoord>(
            Krate::Plugins::ArpLaneEditor::kStepContentLeftMargin);
        VSTGUI::CRect pinRect(pinLeft, pinTop, stripWidth, pinBottom);
        auto* pinStrip = new PinFlagStrip(pinRect);
        pinStrip->setParameterCallback(
            [this](Steinberg::Vst::ParamID paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        pinStrip->setBeginEditCallback(
            [this](Steinberg::Vst::ParamID paramId) { beginEdit(paramId); });
        pinStrip->setEndEditCallback(
            [this](Steinberg::Vst::ParamID paramId) { endEdit(paramId); });

        // Initialize each cell from current parameter state
        for (int i = 0; i < 32; ++i) {
            const auto paramId = static_cast<Steinberg::Vst::ParamID>(
                kArpPinFlagStep0Id + i);
            auto* paramObj = getParameterObject(paramId);
            if (paramObj) {
                pinStrip->setStepValue(i,
                    static_cast<float>(paramObj->getNormalized()));
            }
        }

        // Sync visible cell count to the pitch lane's current active length
        // so cells align 1:1 with the pitch bars underneath. Kept in sync by
        // Controller::setParamNormalized (see controller_view_sync.cpp).
        if (auto* lenParam = getParameterObject(kArpPitchLaneLengthId)) {
            const double lenNorm = lenParam->getNormalized();
            const int steps = std::clamp(
                static_cast<int>(1.0 + std::round(lenNorm * 31.0)), 1, 32);
            pinStrip->setNumSteps(steps);
        }

        // Hidden until Pitch lane selected; visibility managed below.
        pinStrip->setVisible(false);
        detailStrip_->addView(pinStrip);
        pinFlagStrip_ = pinStrip;
    }

    // ========================================================================
    // Construct Markov Matrix Editor
    // ========================================================================
    // 7x7 probability matrix editor. Visible only when the current arp mode
    // is Markov (index 11). Added to the frame (not DetailStrip) so it can
    // overlay the RingDisplay at its top-left corner. Anchored at (12, 120)
    // in frame coordinates — just below the control bars, visually anchored
    // to the Pattern dropdown in the top-left.
    //
    // Two states: expanded (160x160 full 7x7 grid) and collapsed (32x32
    // trigger button). Starts expanded when first created; user can minimize
    // via the "-" button in the editor's top-right corner. While Markov mode
    // is active but the editor is collapsed, the trigger button remains in
    // the top-left corner of the ring display.
    if (activeEditor_) {
        if (auto* frame = activeEditor_->getFrame()) {
            VSTGUI::CRect anchor(12.0, 120.0, 12.0, 120.0);  // top-left point
            auto* editor = new MarkovMatrixEditor(anchor);

            editor->setParameterCallback(
                [this](Steinberg::Vst::ParamID paramId, float normalizedValue) {
                    setParamNormalized(paramId, static_cast<double>(normalizedValue));
                    performEdit(paramId, static_cast<double>(normalizedValue));
                });
            editor->setBeginEditCallback(
                [this](Steinberg::Vst::ParamID paramId) { beginEdit(paramId); });
            editor->setEndEditCallback(
                [this](Steinberg::Vst::ParamID paramId) { endEdit(paramId); });

            // Initialize cells from current parameter state
            for (int i = 0; i < 49; ++i) {
                const auto paramId = static_cast<Steinberg::Vst::ParamID>(
                    kArpMarkovCell00Id + i);
                auto* paramObj = getParameterObject(paramId);
                if (paramObj) {
                    editor->setCellValueFlat(i,
                        static_cast<float>(paramObj->getNormalized()));
                }
            }

            // Initialize preset dropdown from current parameter state
            if (auto* presetParam = getParameterObject(kArpMarkovPresetId)) {
                const int idx = std::clamp(
                    static_cast<int>(presetParam->getNormalized() * 5.0 + 0.5),
                    0, 5);
                editor->setPresetValue(idx);
            }

            // Reflect current arp mode: visible only when mode == Markov
            bool showMarkov = false;
            if (auto* modeParam = getParameterObject(kArpModeId)) {
                const int modeIdx = std::clamp(
                    static_cast<int>(modeParam->getNormalized() * 11.0 + 0.5),
                    0, 11);
                showMarkov = (modeIdx == 11);
            }
            editor->setVisible(showMarkov);

            frame->addView(editor);
            markovEditor_ = editor;
        }
    }

    // ========================================================================
    // Wire transform callbacks
    // ========================================================================
    auto wireBarLaneTransform = [this](
        Krate::Plugins::ArpLaneEditor* lane, uint32_t stepBaseParamId) {
        if (!lane) return;
        lane->setTransformCallback(
            [this, lane, stepBaseParamId](int transformType) {
                auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                auto newValues = lane->computeTransform(type);
                int32_t len = lane->getActiveLength();
                for (int32_t i = 0; i < len; ++i) {
                    uint32_t paramId = stepBaseParamId + static_cast<uint32_t>(i);
                    beginEdit(paramId);
                    performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                    setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                    endEdit(paramId);
                }
                for (int32_t i = 0; i < len; ++i) {
                    lane->setNormalizedStepValue(i, newValues[static_cast<size_t>(i)]);
                }
                lane->setDirty(true);
            });
    };

    wireBarLaneTransform(velLane, kArpVelocityLaneStep0Id);
    wireBarLaneTransform(gLane, kArpGateLaneStep0Id);
    wireBarLaneTransform(pLane, kArpPitchLaneStep0Id);
    wireBarLaneTransform(ratLane, kArpRatchetLaneStep0Id);

    auto wireNonBarTransform = [this](auto* lane, uint32_t stepBaseParamId) {
        if (!lane) return;
        lane->setTransformCallback(
            [this, lane, stepBaseParamId](int transformType) {
                auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                auto newValues = lane->computeTransform(type);
                int32_t len = lane->getActiveLength();
                for (int32_t i = 0; i < len; ++i) {
                    uint32_t paramId = stepBaseParamId + static_cast<uint32_t>(i);
                    beginEdit(paramId);
                    performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                    setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                    endEdit(paramId);
                }
                for (int32_t i = 0; i < len; ++i) {
                    lane->setNormalizedStepValue(i, newValues[static_cast<size_t>(i)]);
                }
                lane->setDirty(true);
            });
    };

    wireNonBarTransform(modLane, kArpModifierLaneStep0Id);
    wireNonBarTransform(condLane, kArpConditionLaneStep0Id);
    wireNonBarTransform(chdLane, kArpChordLaneStep0Id);
    wireNonBarTransform(invLane, kArpInversionLaneStep0Id);

    // --- MIDI Delay lane transforms (shift left/right, randomize) ---
    if (delayLane) {
        delayLane->setTransformCallback(
            [this, delayLane](int transformType) {
                auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                int32_t len = delayLane->getActiveLength();
                if (len <= 0) return;

                // Per-row param base IDs matching KnobRow order
                static constexpr uint32_t kRowBaseIds[] = {
                    kArpMidiDelayActiveStep0Id,
                    kArpMidiDelayTimeModeStep0Id,
                    kArpMidiDelayTimeStep0Id,
                    kArpMidiDelayFeedbackStep0Id,
                    kArpMidiDelayVelDecayStep0Id,
                    kArpMidiDelayPitchShiftStep0Id,
                    kArpMidiDelayGateScaleStep0Id,
                };
                static constexpr int kRows = 7;

                if (type == Krate::Plugins::TransformType::kShiftLeft ||
                    type == Krate::Plugins::TransformType::kShiftRight) {
                    // Rotate all rows together by 1 step
                    for (int row = 0; row < kRows; ++row) {
                        auto knobRow = static_cast<MidiDelayLaneEditor::KnobRow>(row);
                        std::array<float, 32> vals{};
                        for (int i = 0; i < len; ++i)
                            vals[i] = delayLane->getStepValue(i, knobRow);

                        std::array<float, 32> shifted{};
                        for (int i = 0; i < len; ++i) {
                            int src = (type == Krate::Plugins::TransformType::kShiftLeft)
                                ? (i + 1) % len : (i - 1 + len) % len;
                            shifted[i] = vals[src];
                        }

                        for (int i = 0; i < len; ++i) {
                            uint32_t paramId = kRowBaseIds[row] + static_cast<uint32_t>(i);
                            beginEdit(paramId);
                            performEdit(paramId, static_cast<double>(shifted[i]));
                            setParamNormalized(paramId, static_cast<double>(shifted[i]));
                            endEdit(paramId);
                            delayLane->setStepValue(i, knobRow, shifted[i]);
                        }
                    }
                } else if (type == Krate::Plugins::TransformType::kRandomize) {
                    // Randomize all per-step values with sensible ranges
                    std::mt19937 rng(static_cast<unsigned>(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
                    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
                    for (int i = 0; i < len; ++i) {
                        for (int row = 0; row < kRows; ++row) {
                            auto knobRow = static_cast<MidiDelayLaneEditor::KnobRow>(row);
                            float val = dist01(rng);
                            uint32_t paramId = kRowBaseIds[row] + static_cast<uint32_t>(i);
                            beginEdit(paramId);
                            performEdit(paramId, static_cast<double>(val));
                            setParamNormalized(paramId, static_cast<double>(val));
                            endEdit(paramId);
                            delayLane->setStepValue(i, knobRow, val);
                        }
                    }
                }
                delayLane->setDirty(true);
            });
    }

    wireCopyPasteCallbacks();

    // ========================================================================
    // Per-lane Speed Curve Editors (overlay on lane editor area)
    // ========================================================================
    {
        // Position the overlay to match the lane editor's bar area (where
        // step rectangles are drawn), excluding the header, phase-offset
        // strip, step labels, and playback indicator.
        auto stripSize = detailStrip_->getViewSize();
        constexpr float kLaneEditorTop = 70.0f;  // tab(24) + per-lane(32) + pin(14)
        constexpr float kBarAreaTopInset = 28.0f; // header(16) + phaseOffset(12)
        constexpr float kBarAreaBottomInset = 20.0f; // stepLabels(12) + playbackIndicator(8)
        constexpr float kBarAreaLeftInset = 40.0f; // kStepContentLeftMargin
        VSTGUI::CRect overlayRect(
            stripSize.left + kBarAreaLeftInset,
            stripSize.top + kLaneEditorTop + kBarAreaTopInset,
            stripSize.right,
            stripSize.bottom - kBarAreaBottomInset);

        for (int i = 0; i < 8; ++i) {
            auto* editor = new SpeedCurveEditor(overlayRect);
            editor->setVisible(false);
            // Committed callback: sends IMessage on mouse-up / preset change
            editor->setCurveCommittedCallback(
                [this, i](const SpeedCurveData& data) {
                    sendSpeedCurveTable(static_cast<size_t>(i), data);
                });
            speedCurveEditors_[i] = editor;
            // Add to the DetailStrip's parent (the frame) so it overlays
            if (auto* frame = activeEditor_->getFrame())
                frame->addView(editor);
        }

        // Apply pending speed curve data from setComponentState
        if (hasPendingSpeedCurves_) {
            for (int i = 0; i < 8; ++i) {
                if (speedCurveEditors_[i])
                    speedCurveEditors_[i]->setCurveData(pendingSpeedCurves_[static_cast<size_t>(i)]);
            }
        }

        // Create speed curve control bar in a container with background
        // that cleanly overlaps the pin row area.
        if (auto* frame = activeEditor_->getFrame()) {
            // Container starts narrow (just the toggle) and grows when enabled
            constexpr float kContainerY = 56.0f;  // pin row y within strip
            constexpr float kContainerH = 14.0f;
            constexpr float kCollapsedW = 74.0f;  // toggle + "Speed Curve" label + padding
            VSTGUI::CRect containerRect(
                stripSize.left, stripSize.top + kContainerY,
                stripSize.left + kCollapsedW, stripSize.top + kContainerY + kContainerH);
            auto* container = new VSTGUI::CViewContainer(containerRect);
            container->setBackgroundColor(VSTGUI::CColor(26, 26, 46, 255)); // matches bg
            speedCurveContainer_ = container;
            frame->addView(container);

            // All child coordinates are local to the container (0,0 = top-left)
            // Layout: [power toggle 14x14] [4px gap] [Depth label 36x14] [knob 18x18] [4px] [preset 70x14]

            // Toggle button
            VSTGUI::CRect toggleRect(2, 0, 16, 14);
            auto* toggle = new Krate::Plugins::ToggleButton(toggleRect, nullptr, -1);
            toggle->setOnColor(VSTGUI::CColor(208, 132, 92, 255));
            toggle->setOffColor(VSTGUI::CColor(80, 80, 96, 255));
            toggle->setIconStyle(Krate::Plugins::IconStyle::kPower);
            toggle->setValueNormalized(0.0);
            toggle->setTooltipText("Enable speed curve for this lane");
            speedCurveToggle_ = toggle;
            container->addView(toggle);

            // "Speed Curve" label (always visible)
            VSTGUI::CRect speedLabelRect(17, 0, 78, 14);
            auto* speedLabel = new VSTGUI::CTextLabel(speedLabelRect);
            speedLabel->setFont(VSTGUI::kNormalFontSmaller);
            speedLabel->setFontColor(VSTGUI::CColor(144, 144, 152, 255));
            speedLabel->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
            speedLabel->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
            speedLabel->setText("Speed Curve");
            speedLabel->setHoriAlign(VSTGUI::kLeftText);
            container->addView(speedLabel);

            // "Depth" label
            VSTGUI::CRect depthLabelRect(74, 0, 110, 14);
            auto* depthLabel = new VSTGUI::CTextLabel(depthLabelRect);
            depthLabel->setFont(VSTGUI::kNormalFontSmaller);
            depthLabel->setFontColor(VSTGUI::CColor(144, 144, 152, 255));
            depthLabel->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
            depthLabel->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
            depthLabel->setText("Depth");
            depthLabel->setHoriAlign(VSTGUI::kRightText);
            depthLabel->setVisible(false);
            speedCurveDepthLabel_ = depthLabel;
            container->addView(depthLabel);

            // 8 depth knobs (ArcKnob) stacked at same position, toggled per lane
            static constexpr VSTGUI::CColor kLaneColors[8] = {
                {208, 132, 92, 255},  // velocity
                {200, 164, 100, 255}, // gate
                {108, 168, 160, 255}, // pitch
                {192, 112, 124, 255}, // modifier
                {124, 144, 176, 255}, // condition
                {152, 128, 176, 255}, // ratchet
                {168, 140, 200, 255}, // chord
                {136, 168, 200, 255}, // inversion
            };
            // Param IDs in lane-param order (Vel, Gate, Pitch, Mod, Ratchet, Cond, Chord, Inv)
            // but UI lane order is (Vel, Gate, Pitch, Mod, Cond, Ratchet, Chord, Inv)
            static constexpr uint32_t kDepthParamIds[8] = {
                kArpVelocityLaneSpeedCurveDepthId,
                kArpGateLaneSpeedCurveDepthId,
                kArpPitchLaneSpeedCurveDepthId,
                kArpModifierLaneSpeedCurveDepthId,
                kArpConditionLaneSpeedCurveDepthId,
                kArpRatchetLaneSpeedCurveDepthId,
                kArpChordLaneSpeedCurveDepthId,
                kArpInversionLaneSpeedCurveDepthId,
            };
            // Depth knob sub-listener: sends updated depth via IMessage alongside
            // the current curve table, so the processor always has both values.
            speedCurveDepthListener_ = std::make_shared<LambdaListener>(
                [this]([[maybe_unused]] VSTGUI::CControl* ctrl,
                       [[maybe_unused]] float value) {
                    int lane = selectedLaneIndex_;
                    if (lane < 0 || lane >= 8) return;
                    auto laneIdx = static_cast<size_t>(lane);
                    if (speedCurveEditors_[laneIdx])
                        sendSpeedCurveTable(laneIdx,
                            speedCurveEditors_[laneIdx]->curveData());
                });

            VSTGUI::CRect knobRect(112, -2, 130, 16);
            for (int i = 0; i < 8; ++i) {
                // Primary listener: VST3Editor for host parameter wiring
                auto* knob = new Krate::Plugins::ArcKnob(knobRect, activeEditor_,
                    static_cast<int32_t>(kDepthParamIds[i]));
                knob->setArcColor(kLaneColors[i]);
                knob->setGuideColor(VSTGUI::CColor(64, 64, 80, 255));
                knob->setMin(0.0f);
                knob->setMax(1.0f);
                knob->setDefaultValue(0.5f);
                knob->setValueNormalized(static_cast<float>(
                    getParamNormalized(kDepthParamIds[i])));
                knob->setTooltipText("How much the speed curve affects step timing");
                knob->setVisible(false);
                // Sub-listener: sends depth via IMessage to processor
                knob->registerControlListener(speedCurveDepthListener_.get());
                speedCurveDepthKnobs_[i] = knob;
                container->addView(knob);
            }

            // Preset dropdown
            VSTGUI::CRect presetRect(134, 0, 204, 14);
            auto* presetMenu = new VSTGUI::COptionMenu(presetRect, nullptr, -1);
            presetMenu->setFont(VSTGUI::kNormalFontSmaller);
            presetMenu->setFontColor(VSTGUI::CColor(208, 208, 216, 255));
            presetMenu->setBackColor(VSTGUI::CColor(42, 42, 62, 255));
            presetMenu->setFrameColor(VSTGUI::CColor(80, 80, 96, 255));
            for (int p = 0; p < kSpeedCurvePresetCount; ++p) {
                presetMenu->addEntry(
                    speedCurvePresetName(static_cast<SpeedCurvePreset>(p)));
            }
            presetMenu->setVisible(false);
            speedCurvePresetMenu_ = presetMenu;
            container->addView(presetMenu);

            // Wire toggle listener
            constexpr float kExpandedW = 208.0f;  // toggle + "Speed Curve" + depth label + knob + preset + padding
            speedCurveToggleListener_ = std::make_shared<LambdaListener>(
                [this, kCollapsedW, kExpandedW]
                ([[maybe_unused]] VSTGUI::CControl* ctrl, float value) {
                    bool enabled = value > 0.5f;
                    int lane = selectedLaneIndex_;
                    if (lane < 0 || lane >= 8) return;
                    auto laneIdx = static_cast<size_t>(lane);
                    if (speedCurveEditors_[laneIdx]) {
                        auto data = speedCurveEditors_[laneIdx]->curveData();
                        data.enabled = enabled;
                        speedCurveEditors_[laneIdx]->setCurveData(data);
                        sendSpeedCurveTable(laneIdx, data);
                    }
                    showSpeedCurveForLane(lane);
                    // Show/hide depth knob + label + preset
                    if (speedCurveDepthLabel_) speedCurveDepthLabel_->setVisible(enabled);
                    if (speedCurvePresetMenu_) speedCurvePresetMenu_->setVisible(enabled);
                    for (int i = 0; i < 8; ++i) {
                        if (speedCurveDepthKnobs_[i])
                            speedCurveDepthKnobs_[i]->setVisible(i == lane && enabled);
                    }
                    // Resize container
                    if (speedCurveContainer_) {
                        auto r = speedCurveContainer_->getViewSize();
                        r.right = r.left + (enabled ? kExpandedW : kCollapsedW);
                        speedCurveContainer_->setViewSize(r);
                        speedCurveContainer_->setMouseableArea(r);
                        speedCurveContainer_->invalid();
                    }
                });
            toggle->registerControlListener(speedCurveToggleListener_.get());

            // Wire preset menu listener
            speedCurvePresetListener_ = std::make_shared<LambdaListener>(
                [this](VSTGUI::CControl* ctrl, [[maybe_unused]] float value) {
                    auto* menu = dynamic_cast<VSTGUI::COptionMenu*>(ctrl);
                    if (!menu) return;
                    int presetIdx = static_cast<int>(menu->getCurrentIndex());
                    int lane = selectedLaneIndex_;
                    if (lane < 0 || lane >= 8) return;
                    if (speedCurveEditors_[static_cast<size_t>(lane)])
                        speedCurveEditors_[static_cast<size_t>(lane)]->applyPreset(
                            static_cast<SpeedCurvePreset>(presetIdx));
                });
            presetMenu->registerControlListener(speedCurvePresetListener_.get());
        }
    }

    // ========================================================================
    // Wire ring data bridge
    // ========================================================================
    ringDataBridge_.clearLanes();
    ringDataBridge_.setLane(0, velocityLane_);
    ringDataBridge_.setLane(1, gateLane_);
    ringDataBridge_.setLane(2, pitchLane_);
    ringDataBridge_.setLane(3, modifierLane_);
    ringDataBridge_.setLane(4, conditionLane_);
    ringDataBridge_.setLane(5, ratchetLane_);
    ringDataBridge_.setLane(6, chordLane_);
    ringDataBridge_.setLane(7, inversionLane_);

    // Connect ring renderer to data bridge
    auto* renderer = ringDisplay_->getRenderer();
    if (renderer) {
        renderer->setDataSource(&ringDataBridge_);

        // Sync step counts to geometry
        auto& geo = renderer->geometry();
        for (int i = 0; i < 8; ++i) {
            auto* lane = getArpLane(i);
            if (lane)
                geo.setLaneStepCount(i, lane->getActiveLength());
        }

        // Sync Euclidean state
        auto* eucEnabledParam = getParameterObject(kArpEuclideanEnabledId);
        auto* eucHitsParam = getParameterObject(kArpEuclideanHitsId);
        auto* eucStepsParam = getParameterObject(kArpEuclideanStepsId);
        auto* eucRotParam = getParameterObject(kArpEuclideanRotationId);
        if (eucEnabledParam && eucHitsParam && eucStepsParam && eucRotParam) {
            ringDataBridge_.setEuclideanState(
                eucEnabledParam->getNormalized() > 0.5,
                static_cast<int>(std::round(eucHitsParam->getNormalized() * 32.0)),
                std::clamp(2 + static_cast<int>(std::round(eucStepsParam->getNormalized() * 30.0)), 2, 32),
                static_cast<int>(std::round(eucRotParam->getNormalized() * 31.0)));
        }

        // Wire ring edit callbacks
        renderer->setBeginEditCallback(
            [this](int laneIndex, int step) {
                uint32_t paramId = getArpLaneStepBaseParamId(laneIndex)
                    + static_cast<uint32_t>(step);
                beginEdit(paramId);
            });
        renderer->setEndEditCallback(
            [this](int laneIndex, int step) {
                uint32_t paramId = getArpLaneStepBaseParamId(laneIndex)
                    + static_cast<uint32_t>(step);
                endEdit(paramId);
            });
        renderer->setValueChangeCallback(
            [this](int laneIndex, int step, float value) {
                uint32_t paramId = getArpLaneStepBaseParamId(laneIndex)
                    + static_cast<uint32_t>(step);
                setParamNormalized(paramId, static_cast<double>(value));
                performEdit(paramId, static_cast<double>(value));
            });

        // Visibility logic for per-lane contextual controls. Extracted into a
        // lambda so we can call it once at startup (DetailStrip::selectLane()
        // early-returns when selectedLane_ already matches, so the initial
        // default lane's contextual controls would otherwise stay hidden).
        auto updateContextualVisibility = [this, renderer](int laneIndex) {
            renderer->setSelectedLane(laneIndex);
            renderer->invalid();
            // Toggle swing + jitter knob visibility (per-lane, stacked)
            for (int i = 0; i < 8; ++i) {
                if (laneSwingKnobs_[i])
                    laneSwingKnobs_[i]->setVisible(i == laneIndex);
                if (laneJitterKnobs_[i])
                    laneJitterKnobs_[i]->setVisible(i == laneIndex);
            }
            // Toggle Ratchet-specific controls (UI lane 5 = Ratchet)
            const bool showDecay = (laneIndex == 5);
            if (ratchetDecayKnob_)      ratchetDecayKnob_->setVisible(showDecay);
            if (ratchetDecayLabel_)     ratchetDecayLabel_->setVisible(showDecay);
            if (ratchetSubSwingKnob_)   ratchetSubSwingKnob_->setVisible(showDecay);
            if (ratchetSubSwingLabel_)  ratchetSubSwingLabel_->setVisible(showDecay);
            // Toggle Strum (UI lanes 6 = Chord, 7 = Inversion)
            const bool showStrum = (laneIndex == 6 || laneIndex == 7);
            if (strumTimeKnob_)       strumTimeKnob_->setVisible(showStrum);
            if (strumTimeLabel_)      strumTimeLabel_->setVisible(showStrum);
            if (strumDirectionMenu_)  strumDirectionMenu_->setVisible(showStrum);
            if (strumDirectionLabel_) strumDirectionLabel_->setVisible(showStrum);
            // v1.5 Part 2: Toggle Velocity Curve (UI lane 0 = Velocity)
            const bool showVelCurve = (laneIndex == 0);
            if (velCurveKnob_)      velCurveKnob_->setVisible(showVelCurve);
            if (velCurveLabel_)     velCurveLabel_->setVisible(showVelCurve);
            if (velCurveTypeMenu_)  velCurveTypeMenu_->setVisible(showVelCurve);
            if (velCurveTypeLabel_) velCurveTypeLabel_->setVisible(showVelCurve);
            // v1.5 Part 3: Toggle Pitch-lane contextual controls
            // (Transpose + Pin Note + Range Mapping, UI lane 2 = Pitch)
            const bool showPitchContext = (laneIndex == 2);
            if (transposeKnob_)  transposeKnob_->setVisible(showPitchContext);
            if (transposeLabel_) transposeLabel_->setVisible(showPitchContext);
            if (pinNoteKnob_)    pinNoteKnob_->setVisible(showPitchContext);
            if (pinNoteLabel_)   pinNoteLabel_->setVisible(showPitchContext);
            if (rangeLowKnob_)   rangeLowKnob_->setVisible(showPitchContext);
            if (rangeLowLabel_)  rangeLowLabel_->setVisible(showPitchContext);
            if (rangeHighKnob_)  rangeHighKnob_->setVisible(showPitchContext);
            if (rangeHighLabel_) rangeHighLabel_->setVisible(showPitchContext);
            if (rangeModeMenu_)  rangeModeMenu_->setVisible(showPitchContext);
            if (pinFlagStrip_)   pinFlagStrip_->setVisible(showPitchContext);
            // Show speed curve editor for selected lane (if enabled).
            // The MIDI Delay lane (index 8) has no speed curve — hide everything.
            selectedLaneIndex_ = laneIndex;
            const bool hasSpeedCurve = (laneIndex >= 0 && laneIndex < 8);
            if (hasSpeedCurve) {
                showSpeedCurveForLane(laneIndex);
            } else {
                // Hide all speed curve editors when on the delay lane
                for (int i = 0; i < 8; ++i) {
                    if (speedCurveEditors_[i])
                        speedCurveEditors_[i]->setVisible(false);
                }
            }
            // Sync toggle, depth knob, label, and preset menu for selected lane
            bool curveEnabled = false;
            if (hasSpeedCurve && speedCurveEditors_[laneIndex]) {
                curveEnabled = speedCurveEditors_[laneIndex]->curveData().enabled;
            }
            if (auto* toggle = dynamic_cast<VSTGUI::CControl*>(speedCurveToggle_))
                toggle->setValueNormalized(curveEnabled ? 1.0 : 0.0);
            if (speedCurveDepthLabel_)
                speedCurveDepthLabel_->setVisible(curveEnabled);
            if (speedCurvePresetMenu_)
                speedCurvePresetMenu_->setVisible(curveEnabled);
            for (int i = 0; i < 8; ++i) {
                if (speedCurveDepthKnobs_[i])
                    speedCurveDepthKnobs_[i]->setVisible(i == laneIndex && curveEnabled);
            }
            // Sync preset menu selection
            if (curveEnabled && speedCurvePresetMenu_ && hasSpeedCurve) {
                auto* menu = dynamic_cast<VSTGUI::COptionMenu*>(speedCurvePresetMenu_);
                if (menu && speedCurveEditors_[laneIndex]) {
                    int presetIdx = speedCurveEditors_[laneIndex]->curveData().presetIndex;
                    if (presetIdx >= 0 && presetIdx < kSpeedCurvePresetCount)
                        menu->setCurrent(presetIdx);
                }
            }
            // Hide entire speed curve container on MIDI Delay lane,
            // otherwise resize to match enabled/collapsed state
            if (speedCurveContainer_) {
                if (!hasSpeedCurve) {
                    speedCurveContainer_->setVisible(false);
                } else {
                    speedCurveContainer_->setVisible(true);
                    auto r = speedCurveContainer_->getViewSize();
                    r.right = r.left + (curveEnabled ? 208.0f : 74.0f);
                    speedCurveContainer_->setViewSize(r);
                    speedCurveContainer_->setMouseableArea(r);
                    speedCurveContainer_->invalid();
                }
            }
        };

        // Wire bidirectional selection: detail strip ↔ ring renderer
        detailStrip_->setLaneSelectedCallback(updateContextualVisibility);
        renderer->setLaneSelectedCallback(
            [this](int laneIndex) {
                if (detailStrip_)
                    detailStrip_->selectLane(laneIndex);
            });

        // Initialize contextual visibility for the default-selected lane.
        // DetailStrip::selectLane(selectedLane_) is a no-op because the
        // selected lane is already selectedLane_, so we invoke the logic
        // directly with the current selected lane index.
        updateContextualVisibility(detailStrip_->selectedLane());

        renderer->invalid();
    }
}

} // namespace Gradus
