// ==============================================================================
// Controller: View Wiring (verifyView)
// ==============================================================================
// Handles verifyView() which wires all custom views created by UIDescription
// on editor open. Constructs all 8 arp lanes programmatically when the
// ArpLaneContainer view is found.
// ==============================================================================

#include "controller.h"
#include "../plugin_ids.h"
#include "../parameters/arpeggiator_params.h"
#include "../parameters/dropdown_mappings.h"
#include "ui/arp_lane_editor.h"
#include "ui/arp_lane_container.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"

#include <algorithm>
#include <cmath>

namespace Gradus {

VSTGUI::CView* Controller::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& /*attributes*/,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/) {

    // Wire ArpLaneContainer and construct arp lanes
    auto* arpContainer = dynamic_cast<Krate::Plugins::ArpLaneContainer*>(view);
    if (arpContainer) {
        arpLaneContainer_ = arpContainer;

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
            // Sync initial value from host parameter
            auto* param = getParameterObject(speedParamId);
            if (param) {
                double norm = param->getNormalized();
                // Only override if the host has a non-default value stored.
                // Default normalized = kLaneSpeedDefault / (kLaneSpeedCount - 1) = 3/9
                // If norm is 0.0, the parameter hasn't been initialized yet — keep 1.0x default.
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

        // Construct velocity lane
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
        arpLaneContainer_->addLane(velLane);
        velocityLane_ = velLane;

        // Construct gate lane
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
        arpLaneContainer_->addLane(gLane);
        gateLane_ = gLane;

        // Construct pitch lane
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

        // Sync scale type for popup suffix
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
        arpLaneContainer_->addLane(pLane);
        pitchLane_ = pLane;

        // Construct chord lane
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

        // Sync chord lane from host parameters
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
        arpLaneContainer_->addLane(chdLane);
        chordLane_ = chdLane;

        // Construct inversion lane
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

        // Sync inversion lane from host parameters
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
        arpLaneContainer_->addLane(invLane);
        inversionLane_ = invLane;

        // Construct ratchet lane
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
        arpLaneContainer_->addLane(ratLane);
        ratchetLane_ = ratLane;

        // Construct modifier lane
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
        arpLaneContainer_->addLane(modLane);
        modifierLane_ = modLane;

        // Construct condition lane
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
        arpLaneContainer_->addLane(condLane);
        conditionLane_ = condLane;

        // Wire transform callbacks for bar-type lanes
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

        if (modLane) {
            modLane->setTransformCallback(
                [this, modLane](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = modLane->computeTransform(type);
                    int32_t len = modLane->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpModifierLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        modLane->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    modLane->setDirty(true);
                });
        }

        if (condLane) {
            condLane->setTransformCallback(
                [this, condLane](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = condLane->computeTransform(type);
                    int32_t len = condLane->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpConditionLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        condLane->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    condLane->setDirty(true);
                });
        }

        if (chdLane) {
            chdLane->setTransformCallback(
                [this, chdLane](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = chdLane->computeTransform(type);
                    int32_t len = chdLane->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpChordLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        chdLane->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    chdLane->setDirty(true);
                });
        }

        if (invLane) {
            invLane->setTransformCallback(
                [this, invLane](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = invLane->computeTransform(type);
                    int32_t len = invLane->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpInversionLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        invLane->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    invLane->setDirty(true);
                });
        }

        wireCopyPasteCallbacks();

        // Hide collapse buttons — all lanes fit on screen in Gradus
        for (int i = 0; i < kArpLaneCount; ++i) {
            auto* lane = getArpLane(i);
            if (lane) lane->setCollapseVisible(false);
        }
    }

    return view;
}

} // namespace Gradus
