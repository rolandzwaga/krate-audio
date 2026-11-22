// ==============================================================================
// Controller: View Wiring (verifyView)
// ==============================================================================
// Extracted from controller.cpp - handles verifyView() which wires all custom
// views created by UIDescription on editor open.
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "ui/step_pattern_editor.h"
#include "ui/arp_lane_editor.h"
#include "ui/arp_lane_container.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"
#include "ui/xy_morph_pad.h"
#include "ui/adsr_display.h"
#include "ui/mod_matrix_grid.h"
#include "ui/mod_ring_indicator.h"
#include "ui/mod_heatmap.h"
#include "ui/euclidean_dot_display.h"
#include "ui/lfo_waveform_display.h"
#include "ui/chaos_mod_display.h"
#include "ui/rungler_display.h"
#include "ui/sample_hold_display.h"
#include "ui/random_mod_display.h"
#include "ui/category_tab_bar.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cviewcontainer.h"
#include "controller/view_sync_rules.h"

// Parameter pack headers (for denormalization helpers)
#include "parameters/arpeggiator_params.h"
#include "parameters/distortion_params.h"

#include <cmath>
#include <string>

namespace Ruinae {

VSTGUI::CView* Controller::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/) {

    // Register as sub-listener for action buttons (transforms, Euclidean regen)
    auto* control = dynamic_cast<VSTGUI::CControl*>(view);
    if (control) {
        auto tag = control->getTag();
        if (tag >= static_cast<int32_t>(kActionTransformInvertTag) &&
            tag <= static_cast<int32_t>(kActionEuclideanRegenTag)) {
            control->registerControlListener(this);
        }
    }

    // Populate the pattern preset dropdown (identified by custom-id, no control-tag)
    if (const auto* customId = attributes.getAttributeValue("custom-id")) {
        if (*customId == "preset-dropdown") {
            if (auto* menu = dynamic_cast<VSTGUI::COptionMenu*>(view)) {
                menu->addEntry("All On");
                menu->addEntry("All Off");
                menu->addEntry("Alternate");
                menu->addEntry("Ramp Up");
                menu->addEntry("Ramp Down");
                menu->addEntry("Random");
                menu->registerControlListener(this);
                cacheView(presetDropdown_, menu);
            }
        }
    }

    // Wire StepPatternEditor callbacks
    auto* spe = dynamic_cast<Krate::Plugins::StepPatternEditor*>(view);
    if (spe) {
        cacheView(stepPatternEditor_, spe);
        spe->setStepLevelBaseParamId(kTranceGateStepLevel0Id);

        spe->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        spe->setBeginEditCallback(
            [this](uint32_t paramId) { beginEdit(paramId); });
        spe->setEndEditCallback(
            [this](uint32_t paramId) { endEdit(paramId); });

        // Sync current parameter values
        for (int i = 0; i < 32; ++i) {
            auto paramId = static_cast<Steinberg::Vst::ParamID>(
                kTranceGateStepLevel0Id + i);
            auto* paramObj = getParameterObject(paramId);
            if (paramObj) {
                spe->setStepLevel(i,
                    static_cast<float>(paramObj->getNormalized()));
            }
        }

        auto* numStepsParam = getParameterObject(kTranceGateNumStepsId);
        if (numStepsParam) {
            double val = numStepsParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(2.0 + std::round(val * 30.0)), 2, 32);
            spe->setNumSteps(steps);
        }

        auto* euclEnabledParam = getParameterObject(kTranceGateEuclideanEnabledId);
        if (euclEnabledParam) {
            spe->setEuclideanEnabled(euclEnabledParam->getNormalized() >= 0.5);
        }
        auto* euclHitsParam = getParameterObject(kTranceGateEuclideanHitsId);
        if (euclHitsParam) {
            int hits = std::clamp(
                static_cast<int>(std::round(euclHitsParam->getNormalized() * 32.0)), 0, 32);
            spe->setEuclideanHits(hits);
        }
        auto* euclRotParam = getParameterObject(kTranceGateEuclideanRotationId);
        if (euclRotParam) {
            int rot = std::clamp(
                static_cast<int>(std::round(euclRotParam->getNormalized() * 31.0)), 0, 31);
            spe->setEuclideanRotation(rot);
        }
        auto* phaseParam = getParameterObject(kTranceGatePhaseOffsetId);
        if (phaseParam) {
            spe->setPhaseOffset(
                static_cast<float>(phaseParam->getNormalized()));
        }
    }

    // Wire ArpLaneContainer and construct arp lanes (079-layout-framework)
    auto* arpContainer = dynamic_cast<Krate::Plugins::ArpLaneContainer*>(view);
    if (arpContainer) {
        cacheView(arpLaneContainer_, arpContainer);

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
        cacheView(velocityLane_, new Krate::Plugins::ArpLaneEditor(
            VSTGUI::CRect(0, 0, 500, 105), nullptr, -1));
        velocityLane_->setLaneName("VEL");
        velocityLane_->setLaneType(Krate::Plugins::ArpLaneType::kVelocity);
        velocityLane_->setAccentColor(VSTGUI::CColor{208, 132, 92, 255});
        velocityLane_->setDisplayRange(0.0f, 1.0f, "1.0", "0.0");
        velocityLane_->setStepLevelBaseParamId(kArpVelocityLaneStep0Id);
        velocityLane_->setLengthParamId(kArpVelocityLaneLengthId);
        velocityLane_->setPlayheadParamId(kArpVelocityPlayheadId);
        wireLaneCallbacks(velocityLane_);
        syncBarLane(velocityLane_, kArpVelocityLaneStep0Id, kArpVelocityLaneLengthId);
        arpLaneContainer_->addLane(velocityLane_);

        // Construct gate lane
        cacheView(gateLane_, new Krate::Plugins::ArpLaneEditor(
            VSTGUI::CRect(0, 0, 500, 105), nullptr, -1));
        gateLane_->setLaneName("GATE");
        gateLane_->setLaneType(Krate::Plugins::ArpLaneType::kGate);
        gateLane_->setAccentColor(VSTGUI::CColor{200, 164, 100, 255});
        gateLane_->setDisplayRange(0.0f, 2.0f, "200%", "0%");
        gateLane_->setStepLevelBaseParamId(kArpGateLaneStep0Id);
        gateLane_->setLengthParamId(kArpGateLaneLengthId);
        gateLane_->setPlayheadParamId(kArpGatePlayheadId);
        wireLaneCallbacks(gateLane_);
        syncBarLane(gateLane_, kArpGateLaneStep0Id, kArpGateLaneLengthId);
        arpLaneContainer_->addLane(gateLane_);

        // Construct pitch lane
        cacheView(pitchLane_, new Krate::Plugins::ArpLaneEditor(
            VSTGUI::CRect(0, 0, 500, 105), nullptr, -1));
        pitchLane_->setLaneName("PITCH");
        pitchLane_->setLaneType(Krate::Plugins::ArpLaneType::kPitch);
        pitchLane_->setAccentColor(VSTGUI::CColor{108, 168, 160, 255});
        pitchLane_->setDisplayRange(-24.0f, 24.0f, "+24", "-24");
        pitchLane_->setStepLevelBaseParamId(kArpPitchLaneStep0Id);
        pitchLane_->setLengthParamId(kArpPitchLaneLengthId);
        pitchLane_->setPlayheadParamId(kArpPitchPlayheadId);
        wireLaneCallbacks(pitchLane_);
        syncBarLane(pitchLane_, kArpPitchLaneStep0Id, kArpPitchLaneLengthId);

        // Sync scale type for popup suffix (084-arp-scale-mode FR-018)
        {
            auto* scaleParam = getParameterObject(kArpScaleTypeId);
            if (scaleParam) {
                double scaleNorm = scaleParam->getNormalized();
                int uiIndex = std::clamp(
                    static_cast<int>(scaleNorm * (kArpScaleTypeCount - 1) + 0.5),
                    0, kArpScaleTypeCount - 1);
                int enumValue = kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)];
                pitchLane_->setScaleType(enumValue);
            }
        }
        arpLaneContainer_->addLane(pitchLane_);

        // Construct chord lane (arp-chord-lane) — after pitch, before ratchet
        cacheView(chordLane_, new Krate::Plugins::ArpChordLane(
            VSTGUI::CRect(0, 0, 500, 63), nullptr, -1));
        chordLane_->setLaneName("CHORD");
        chordLane_->setAccentColor(VSTGUI::CColor{168, 136, 200, 255});
        chordLane_->setStepBaseParamId(kArpChordLaneStep0Id);
        chordLane_->setLengthParamId(kArpChordLaneLengthId);
        chordLane_->setPlayheadParamId(kArpChordPlayheadId);

        chordLane_->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        chordLane_->setBeginEditCallback(
            [this](uint32_t paramId) { beginEdit(paramId); });
        chordLane_->setEndEditCallback(
            [this](uint32_t paramId) { endEdit(paramId); });
        chordLane_->setLengthParamCallback(
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
                chordLane_->setStepValue(i, chordIdx);
            }
        }
        {
            auto* lenParam = getParameterObject(kArpChordLaneLengthId);
            if (lenParam) {
                double val = lenParam->getNormalized();
                int steps = std::clamp(
                    static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
                chordLane_->setNumSteps(steps);
            }
        }
        arpLaneContainer_->addLane(chordLane_);

        // Construct inversion lane (arp-chord-lane) — after chord, before ratchet
        cacheView(inversionLane_, new Krate::Plugins::ArpInversionLane(
            VSTGUI::CRect(0, 0, 500, 63), nullptr, -1));
        inversionLane_->setLaneName("INV");
        inversionLane_->setAccentColor(VSTGUI::CColor{136, 168, 200, 255});
        inversionLane_->setStepBaseParamId(kArpInversionLaneStep0Id);
        inversionLane_->setLengthParamId(kArpInversionLaneLengthId);
        inversionLane_->setPlayheadParamId(kArpInversionPlayheadId);

        inversionLane_->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        inversionLane_->setBeginEditCallback(
            [this](uint32_t paramId) { beginEdit(paramId); });
        inversionLane_->setEndEditCallback(
            [this](uint32_t paramId) { endEdit(paramId); });
        inversionLane_->setLengthParamCallback(
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
                inversionLane_->setStepValue(i, invIdx);
            }
        }
        {
            auto* lenParam = getParameterObject(kArpInversionLaneLengthId);
            if (lenParam) {
                double val = lenParam->getNormalized();
                int steps = std::clamp(
                    static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
                inversionLane_->setNumSteps(steps);
            }
        }
        arpLaneContainer_->addLane(inversionLane_);

        // Sync disabled state from voice mode (chord/inversion require poly)
        {
            auto* vmParam = getParameterObject(kVoiceModeId);
            bool isMono = (vmParam != nullptr) && vmParam->getNormalized() >= 0.5;
            chordLane_->setDisabled(isMono, "Chord lane requires Poly voice mode");
            inversionLane_->setDisabled(isMono, "Inversion lane requires Poly voice mode");
        }

        // Construct ratchet lane
        cacheView(ratchetLane_, new Krate::Plugins::ArpLaneEditor(
            VSTGUI::CRect(0, 0, 500, 105), nullptr, -1));
        ratchetLane_->setLaneName("RATCH");
        ratchetLane_->setLaneType(Krate::Plugins::ArpLaneType::kRatchet);
        ratchetLane_->setAccentColor(VSTGUI::CColor{152, 128, 176, 255});
        ratchetLane_->setDisplayRange(1.0f, 4.0f, "4", "1");
        ratchetLane_->setStepLevelBaseParamId(kArpRatchetLaneStep0Id);
        ratchetLane_->setLengthParamId(kArpRatchetLaneLengthId);
        ratchetLane_->setPlayheadParamId(kArpRatchetPlayheadId);
        wireLaneCallbacks(ratchetLane_);
        syncBarLane(ratchetLane_, kArpRatchetLaneStep0Id, kArpRatchetLaneLengthId);
        arpLaneContainer_->addLane(ratchetLane_);

        // Construct modifier lane
        cacheView(modifierLane_, new Krate::Plugins::ArpModifierLane(
            VSTGUI::CRect(0, 0, 500, 79), nullptr, -1));
        modifierLane_->setLaneName("MOD");
        modifierLane_->setAccentColor(VSTGUI::CColor{192, 112, 124, 255});
        modifierLane_->setStepFlagBaseParamId(kArpModifierLaneStep0Id);
        modifierLane_->setLengthParamId(kArpModifierLaneLengthId);
        modifierLane_->setPlayheadParamId(kArpModifierPlayheadId);

        modifierLane_->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        modifierLane_->setBeginEditCallback(
            [this](uint32_t paramId) { beginEdit(paramId); });
        modifierLane_->setEndEditCallback(
            [this](uint32_t paramId) { endEdit(paramId); });
        modifierLane_->setLengthParamCallback(
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
                modifierLane_->setStepFlags(i, flags);
            }
        }
        auto* modLenParam = getParameterObject(kArpModifierLaneLengthId);
        if (modLenParam) {
            double val = modLenParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
            modifierLane_->setNumSteps(steps);
        }
        arpLaneContainer_->addLane(modifierLane_);

        // Construct condition lane
        cacheView(conditionLane_, new Krate::Plugins::ArpConditionLane(
            VSTGUI::CRect(0, 0, 500, 63), nullptr, -1));
        conditionLane_->setLaneName("COND");
        conditionLane_->setAccentColor(VSTGUI::CColor{124, 144, 176, 255});
        conditionLane_->setStepConditionBaseParamId(kArpConditionLaneStep0Id);
        conditionLane_->setLengthParamId(kArpConditionLaneLengthId);
        conditionLane_->setPlayheadParamId(kArpConditionPlayheadId);

        conditionLane_->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        conditionLane_->setBeginEditCallback(
            [this](uint32_t paramId) { beginEdit(paramId); });
        conditionLane_->setEndEditCallback(
            [this](uint32_t paramId) { endEdit(paramId); });
        conditionLane_->setLengthParamCallback(
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
                conditionLane_->setStepCondition(
                    i, conditionIndexFromNormalized(paramObj->getNormalized()));
            }
        }
        auto* condLenParam = getParameterObject(kArpConditionLaneLengthId);
        if (condLenParam) {
            double val = condLenParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
            conditionLane_->setNumSteps(steps);
        }
        arpLaneContainer_->addLane(conditionLane_);

        // Wire transform callbacks for all 8 lanes (081-interaction-polish, T049)
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

        wireBarLaneTransform(velocityLane_, kArpVelocityLaneStep0Id);
        wireBarLaneTransform(gateLane_, kArpGateLaneStep0Id);
        wireBarLaneTransform(pitchLane_, kArpPitchLaneStep0Id);
        wireBarLaneTransform(ratchetLane_, kArpRatchetLaneStep0Id);

        if (modifierLane_) {
            modifierLane_->setTransformCallback(
                [this, lane = modifierLane_](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = lane->computeTransform(type);
                    int32_t len = lane->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpModifierLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        lane->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    lane->setDirty(true);
                });
        }

        if (conditionLane_) {
            conditionLane_->setTransformCallback(
                [this, lane = conditionLane_](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = lane->computeTransform(type);
                    int32_t len = lane->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpConditionLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        lane->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    lane->setDirty(true);
                });
        }

        if (chordLane_) {
            chordLane_->setTransformCallback(
                [this, lane = chordLane_](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = lane->computeTransform(type);
                    int32_t len = lane->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpChordLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        lane->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    lane->setDirty(true);
                });
        }

        if (inversionLane_) {
            inversionLane_->setTransformCallback(
                [this, lane = inversionLane_](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = lane->computeTransform(type);
                    int32_t len = lane->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpInversionLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        lane->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    lane->setDirty(true);
                });
        }

        wireCopyPasteCallbacks();
    }

    // Wire XYMorphPad callbacks
    auto* xyPad = dynamic_cast<Krate::Plugins::XYMorphPad*>(view);
    if (xyPad) {
        cacheView(xyMorphPad_, xyPad);
        xyPad->setController(this);
        xyPad->setSecondaryParamId(kMixerTiltId);

        auto* posParam = getParameterObject(kMixerPositionId);
        auto* tiltParam = getParameterObject(kMixerTiltId);
        float initX = posParam
            ? static_cast<float>(posParam->getNormalized()) : 0.5f;
        float initY = tiltParam
            ? static_cast<float>(tiltParam->getNormalized()) : 0.5f;
        xyPad->setMorphPosition(initX, initY);
    }

    // Wire ADSRDisplay callbacks
    auto* adsrDisplay = dynamic_cast<Krate::Plugins::ADSRDisplay*>(view);
    if (adsrDisplay) {
        wireAdsrDisplay(adsrDisplay);
    }

    // Wire ModMatrixGrid callbacks (T047, T048, T049)
    auto* modGrid = dynamic_cast<Krate::Plugins::ModMatrixGrid*>(view);
    if (modGrid) {
        wireModMatrixGrid(modGrid);
    }

    // Wire ModRingIndicator overlays (T069)
    auto* ringIndicator = dynamic_cast<Krate::Plugins::ModRingIndicator*>(view);
    if (ringIndicator) {
        wireModRingIndicator(ringIndicator);
    }

    // Wire ModHeatmap cell click callback (T155)
    auto* heatmap = dynamic_cast<Krate::Plugins::ModHeatmap*>(view);
    if (heatmap) {
        heatmap->setCellClickCallback(
            [this](int sourceIndex, int destIndex) {
                selectModulationRoute(sourceIndex, destIndex);
            });
        if (modMatrixGrid_) {
            modMatrixGrid_->setHeatmap(heatmap);
        }
    }

    // Wire CategoryTabBar selection callback (T075)
    auto* tabBar = dynamic_cast<Krate::Plugins::CategoryTabBar*>(view);
    if (tabBar) {
        tabBar->setSelectionCallback([this](int tab) {
            if (modMatrixGrid_) {
                modMatrixGrid_->setActiveTab(tab);
            }
        });
    }

    // PW knob visual disable (068-osc-type-params FR-016)
    {
        const auto* viewName = attributes.getAttributeValue("custom-view-name");
        if (viewName) {
            if (*viewName == "OscAPWKnob") {
                cacheView(oscAPWKnob_, view);
                auto* wfParam = getParameterObject(kOscAWaveformId);
                if (wfParam) applyOscPwDimming(view, wfParam->getNormalized());
            } else if (*viewName == "OscBPWKnob") {
                cacheView(oscBPWKnob_, view);
                auto* wfParam = getParameterObject(kOscBWaveformId);
                if (wfParam) applyOscPwDimming(view, wfParam->getNormalized());
            } else if (*viewName == "SpectralCurveDropdown") {
                cacheView(spectralCurveDropdown_, view);
                auto* modeParam = getParameterObject(kDistortionSpectralModeId);
                int mode = 0;
                if (modeParam) {
                    mode = std::clamp(
                        static_cast<int>(modeParam->getNormalized() * (kSpectralModeCount - 1) + 0.5),
                        0, kSpectralModeCount - 1);
                }
                bool isBitcrush = (mode == 3);
                view->setAlphaValue(isBitcrush ? 0.35f : 1.0f);
                view->setMouseEnabled(!isBitcrush);
            }

            // LFO Waveform Displays
            auto* lfoDisplay = dynamic_cast<Krate::Plugins::LfoWaveformDisplay*>(view);
            if (lfoDisplay) {
                if (*viewName == "LFO1WaveformDisplay") {
                    cacheView(lfo1WaveformDisplay_, lfoDisplay);
                    syncLfoWaveformDisplay(lfoDisplay, 0);
                } else if (*viewName == "LFO2WaveformDisplay") {
                    cacheView(lfo2WaveformDisplay_, lfoDisplay);
                    syncLfoWaveformDisplay(lfoDisplay, 1);
                }
            }

            // Chaos Mod Display
            auto* chaosDisplay = dynamic_cast<Krate::Plugins::ChaosModDisplay*>(view);
            if (chaosDisplay && viewName && *viewName == "ChaosModDisplay") {
                cacheView(chaosModDisplay_, chaosDisplay);
                syncChaosModDisplay();
            }

            // Rungler Display
            auto* runglerDisp = dynamic_cast<Krate::Plugins::RunglerDisplay*>(view);
            if (runglerDisp && viewName && *viewName == "RunglerDisplay") {
                cacheView(runglerDisplay_, runglerDisp);
                syncRunglerDisplay();
            }

            // Sample & Hold Display
            auto* shDisp = dynamic_cast<Krate::Plugins::SampleHoldDisplay*>(view);
            if (shDisp && viewName && *viewName == "SampleHoldDisplay") {
                cacheView(sampleHoldDisplay_, shDisp);
                syncSampleHoldDisplay();
            }

            // Random Mod Display
            auto* rndDisp = dynamic_cast<Krate::Plugins::RandomModDisplay*>(view);
            if (rndDisp && viewName && *viewName == "RandomModDisplay") {
                cacheView(randomModDisplay_, rndDisp);
                syncRandomModDisplay();
            }
        }
    }

    // Wire sidechain indicator labels
    {
        auto* textLabel = dynamic_cast<VSTGUI::CTextLabel*>(view);
        if (textLabel) {
            const auto* scName = attributes.getAttributeValue("custom-view-name");
            if (scName) {
                if (*scName == "SidechainIndicatorEnvFollower") {
                    cacheView(sidechainIndicatorEnvFollower_, textLabel);
                    updateSidechainIndicator(textLabel);
                } else if (*scName == "SidechainIndicatorPitchFollower") {
                    cacheView(sidechainIndicatorPitchFollower_, textLabel);
                    updateSidechainIndicator(textLabel);
                } else if (*scName == "SidechainIndicatorTransient") {
                    cacheView(sidechainIndicatorTransient_, textLabel);
                    updateSidechainIndicator(textLabel);
                }
            }
        }
    }

    // Wire named containers by custom-view-name
    auto* container = dynamic_cast<VSTGUI::CViewContainer*>(view);
    if (container) {
        const auto* name = attributes.getAttributeValue("custom-view-name");
        if (name) {
            bool matchedVoiceRow = false;
            for (int vi = 0; vi < 4; ++vi) {
                // Match "HarmonizerVoice1" through "HarmonizerVoice4"
                std::string voiceName = "HarmonizerVoice" + std::to_string(vi + 1);
                if (*name == voiceName) {
                    cacheView(harmonizerVoiceRows_[vi], container);
                    matchedVoiceRow = true;
                    break;
                }
            }
            if (!matchedVoiceRow) {
            if (*name == "LFO1RateGroup") {
                cacheView(lfo1RateGroup_, container);
                auto* syncParam = getParameterObject(kLFO1SyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "LFO2RateGroup") {
                cacheView(lfo2RateGroup_, container);
                auto* syncParam = getParameterObject(kLFO2SyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "LFO1NoteValueGroup") {
                cacheView(lfo1NoteValueGroup_, container);
                auto* syncParam = getParameterObject(kLFO1SyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "LFO2NoteValueGroup") {
                cacheView(lfo2NoteValueGroup_, container);
                auto* syncParam = getParameterObject(kLFO2SyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "ChaosRateGroup") {
                cacheView(chaosRateGroup_, container);
                auto* syncParam = getParameterObject(kChaosModSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "ChaosNoteValueGroup") {
                cacheView(chaosNoteValueGroup_, container);
                auto* syncParam = getParameterObject(kChaosModSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "SHRateGroup") {
                cacheView(shRateGroup_, container);
                auto* syncParam = getParameterObject(kSampleHoldSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "SHNoteValueGroup") {
                cacheView(shNoteValueGroup_, container);
                auto* syncParam = getParameterObject(kSampleHoldSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "RandomRateGroup") {
                cacheView(randomRateGroup_, container);
                auto* syncParam = getParameterObject(kRandomSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "RandomNoteValueGroup") {
                cacheView(randomNoteValueGroup_, container);
                auto* syncParam = getParameterObject(kRandomSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "DelayTimeGroup") {
                cacheView(delayTimeGroup_, container);
                auto* syncParam = getParameterObject(kDelaySyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "DelayNoteValueGroup") {
                cacheView(delayNoteValueGroup_, container);
                auto* syncParam = getParameterObject(kDelaySyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "PhaserRateGroup") {
                cacheView(phaserRateGroup_, container);
                auto* syncParam = getParameterObject(kPhaserSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "PhaserNoteValueGroup") {
                cacheView(phaserNoteValueGroup_, container);
                auto* syncParam = getParameterObject(kPhaserSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "NoModulationGroup") {
                cacheView(noModulationGroup_, container);
                auto* modParam = getParameterObject(kModulationTypeId);
                int modType = modParam ? static_cast<int>(modParam->getNormalized() * 3.0 + 0.5) : 0;
                container->setVisible(modType == 0);
            } else if (*name == "PhaserControlsGroup") {
                cacheView(phaserControlsGroup_, container);
                auto* modParam = getParameterObject(kModulationTypeId);
                int modType = modParam ? static_cast<int>(modParam->getNormalized() * 3.0 + 0.5) : 0;
                container->setVisible(modType == 1);
            } else if (*name == "FlangerControlsGroup") {
                cacheView(flangerControlsGroup_, container);
                auto* modParam = getParameterObject(kModulationTypeId);
                int modType = modParam ? static_cast<int>(modParam->getNormalized() * 3.0 + 0.5) : 0;
                container->setVisible(modType == 2);
            } else if (*name == "ChorusControlsGroup") {
                cacheView(chorusControlsGroup_, container);
                auto* modParam = getParameterObject(kModulationTypeId);
                int modType = modParam ? static_cast<int>(modParam->getNormalized() * 3.0 + 0.5) : 0;
                container->setVisible(modType == 3);
            } else if (*name == "ChorusRateGroup") {
                cacheView(chorusRateGroup_, container);
                auto* syncParam = getParameterObject(kChorusSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "ChorusNoteValueGroup") {
                cacheView(chorusNoteValueGroup_, container);
                auto* syncParam = getParameterObject(kChorusSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "FlangerRateGroup") {
                cacheView(flangerRateGroup_, container);
                auto* syncParam = getParameterObject(kFlangerSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "FlangerNoteValueGroup") {
                cacheView(flangerNoteValueGroup_, container);
                auto* syncParam = getParameterObject(kFlangerSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "TranceGateRateGroup") {
                cacheView(tranceGateRateGroup_, container);
                auto* syncParam = getParameterObject(kTranceGateTempoSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "TranceGateNoteValueGroup") {
                cacheView(tranceGateNoteValueGroup_, container);
                auto* syncParam = getParameterObject(kTranceGateTempoSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "ArpRateGroup") {
                cacheView(arpRateGroup_, container);
                auto* syncParam = getParameterObject(kArpTempoSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "ArpNoteValueGroup") {
                cacheView(arpNoteValueGroup_, container);
                auto* syncParam = getParameterObject(kArpTempoSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "EuclideanControlsGroup") {
                cacheView(euclideanControlsGroup_, container);
                auto* param = getParameterObject(kTranceGateEuclideanEnabledId);
                bool enabled = (param != nullptr) && param->getNormalized() >= 0.5;
                container->setVisible(enabled);
            } else if (*name == "PolyGroup") {
                cacheView(polyGroup_, container);
                auto* voiceModeParam = getParameterObject(kVoiceModeId);
                bool isMono = (voiceModeParam != nullptr) && voiceModeParam->getNormalized() >= 0.5;
                container->setVisible(!isMono);
            } else if (*name == "MonoGroup") {
                cacheView(monoGroup_, container);
                auto* voiceModeParam = getParameterObject(kVoiceModeId);
                bool isMono = (voiceModeParam != nullptr) && voiceModeParam->getNormalized() >= 0.5;
                container->setVisible(isMono);
            } else if (*name == "SettingsDrawer") {
                cacheView(settingsDrawer_, container);
            } else if (*name == "ArpEuclideanGroup") {
                cacheView(arpEuclideanGroup_, container);
                auto* param = getParameterObject(kArpEuclideanEnabledId);
                bool enabled = (param != nullptr) && param->getNormalized() >= 0.5;
                container->setVisible(enabled);
            } else if (*name == "ArpRootNoteGroup") {
                cacheView(arpRootNoteGroup_, container);
                auto* scaleParam = getParameterObject(kArpScaleTypeId);
                bool isChromaticInit = (scaleParam == nullptr) || scaleParam->getNormalized() < 0.01;
                container->setAlphaValue(isChromaticInit ? 0.35f : 1.0f);
                container->setMouseEnabled(!isChromaticInit);
            } else if (*name == "ArpQuantizeInputGroup") {
                cacheView(arpQuantizeInputGroup_, container);
                auto* scaleParam = getParameterObject(kArpScaleTypeId);
                bool isChromaticInit = (scaleParam == nullptr) || scaleParam->getNormalized() < 0.01;
                container->setAlphaValue(isChromaticInit ? 0.35f : 1.0f);
                container->setMouseEnabled(!isChromaticInit);
            } else if (*name == "SpectralBitsGroup") {
                cacheView(spectralBitsGroup_, container);
                auto* modeParam = getParameterObject(kDistortionSpectralModeId);
                int mode = 0;
                if (modeParam) {
                    mode = std::clamp(
                        static_cast<int>(modeParam->getNormalized() * (kSpectralModeCount - 1) + 0.5),
                        0, kSpectralModeCount - 1);
                }
                bool isBitcrush = (mode == 3);
                container->setAlphaValue(isBitcrush ? 1.0f : 0.35f);
                container->setMouseEnabled(isBitcrush);
            }
            }  // if (!matchedVoiceRow)
        }
    }

    // Settings drawer: capture gear button and register as listener
    auto* ctrl = dynamic_cast<VSTGUI::CControl*>(view);
    if (ctrl) {
        auto tag = ctrl->getTag();
        if (tag == static_cast<int32_t>(kActionSettingsToggleTag)) {
            cacheView(gearButton_, ctrl);
            ctrl->registerControlListener(this);
        }
        if (tag == static_cast<int32_t>(kActionSettingsOverlayTag)) {
            cacheView(settingsOverlay_, view);
            view->setVisible(false);
            ctrl->registerControlListener(this);
        }
        if (tag == static_cast<int32_t>(kArpDiceTriggerId)) {
            cacheView(diceButton_, ctrl);
            ctrl->registerControlListener(this);
        }
    }

    // Wire EuclideanDotDisplay
    auto* eucDotDisplay = dynamic_cast<Krate::Plugins::EuclideanDotDisplay*>(view);
    if (eucDotDisplay) {
        cacheView(euclideanDotDisplay_, eucDotDisplay);

        auto* hitsParam = getParameterObject(kArpEuclideanHitsId);
        if (hitsParam) {
            int hits = std::clamp(
                static_cast<int>(std::round(hitsParam->getNormalized() * 32.0)),
                0, 32);
            eucDotDisplay->setHits(hits);
        }
        auto* stepsParam = getParameterObject(kArpEuclideanStepsId);
        if (stepsParam) {
            int steps = std::clamp(
                static_cast<int>(2.0 + std::round(stepsParam->getNormalized() * 30.0)),
                2, 32);
            eucDotDisplay->setSteps(steps);
        }
        auto* rotParam = getParameterObject(kArpEuclideanRotationId);
        if (rotParam) {
            int rot = std::clamp(
                static_cast<int>(std::round(rotParam->getNormalized() * 31.0)),
                0, 31);
            eucDotDisplay->setRotation(rot);
        }
    }

    return view;
}

} // namespace Ruinae
