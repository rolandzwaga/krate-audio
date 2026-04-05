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
#include "ui/arp_lane_editor.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"

#include <algorithm>
#include <cmath>

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

    wireCopyPasteCallbacks();

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
