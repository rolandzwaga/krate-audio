// ==============================================================================
// Controller: View Sync (setParamNormalized, syncAllViews)
// ==============================================================================
// Extracted from controller.cpp - handles incremental per-param view updates
// (setParamNormalized) and batch sync after preset loads (syncAllViews).
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "ui/step_pattern_editor.h"
#include "ui/arp_lane_editor.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"
#include "ui/xy_morph_pad.h"
#include "ui/adsr_display.h"
#include "ui/mod_matrix_grid.h"
#include "ui/mod_ring_indicator.h"
#include "ui/euclidean_dot_display.h"
#include "ui/lfo_waveform_display.h"
#include "ui/chaos_mod_display.h"
#include "ui/rungler_display.h"
#include "ui/sample_hold_display.h"
#include "ui/random_mod_display.h"
#include "adsr_expanded_overlay.h"
#include <krate/dsp/core/note_value.h>

// Parameter pack headers (for denormalization helpers)
#include "parameters/lfo1_params.h"
#include "parameters/lfo2_params.h"
#include "parameters/chaos_mod_params.h"
#include "parameters/rungler_params.h"
#include "parameters/sample_hold_params.h"
#include "parameters/random_params.h"
#include "parameters/arpeggiator_params.h"
#include "parameters/distortion_params.h"
#include "parameters/amp_env_params.h"
#include "parameters/filter_env_params.h"
#include "parameters/mod_env_params.h"
#include "parameters/harmonizer_params.h"

#include <cmath>

namespace Ruinae {

// Maps destination index to the actual VST parameter ID of that knob.
// Tab-dependent: voice tab and global tab have different mappings.
// Sizes derived from central registry in mod_matrix_types.h.
// Used by ModRingIndicator base value sync (T069-T072).
static constexpr std::array<Steinberg::Vst::ParamID,
    Krate::Plugins::kNumVoiceDestinations> kVoiceDestParamIds = {{
    kFilterCutoffId,          // 0: Filter Cutoff
    kFilterResonanceId,       // 1: Filter Resonance
    kMixerPositionId,         // 2: Morph Position
    kDistortionDriveId,       // 3: Distortion Drive
    kTranceGateDepthId,       // 4: TranceGate Depth
    kOscATuneId,              // 5: OSC A Pitch
    kOscBTuneId,              // 6: OSC B Pitch
    kMixerTiltId,             // 7: Spectral Tilt
}};

// ==============================================================================
// setParamNormalized: Incremental per-param view updates
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setParamNormalized(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) {

    // Let the base class handle its bookkeeping first
    auto result = EditControllerEx1::setParamNormalized(tag, value);

    // CRITICAL: Pointer-nulling for view lifecycle must happen even during bulk
    // loads. The base class setParamNormalized triggers IDependent notifications
    // which cause UIViewSwitchContainer to destroy old tab views. If we skip
    // onTabChanged(), the cached pointers become dangling and syncAllViews()
    // will dereference freed memory (use-after-free -> ACCESS_VIOLATION).
    if (tag == kMainTabTag) {
        int newTab = static_cast<int>(std::round(value * 3.0));
        onTabChanged(newTab);
    }
    if (tag == kOscATypeId && value > 0.01) {
        oscAPWKnob_ = nullptr;
    }
    if (tag == kOscBTypeId && value > 0.01) {
        oscBPWKnob_ = nullptr;
    }
    if (tag == kDistortionTypeId) {
        spectralCurveDropdown_ = nullptr;
        spectralBitsGroup_ = nullptr;
    }

    // During bulk parameter loads (preset switching), skip per-param view updates.
    // syncAllViews() will do a single batch sync afterwards.
    if (bulkParamLoad_)
        return result;

    // Push trance gate parameter changes to StepPatternEditor
    if (stepPatternEditor_) {
        if (tag >= kTranceGateStepLevel0Id && tag <= kTranceGateStepLevel31Id) {
            int stepIndex = static_cast<int>(tag - kTranceGateStepLevel0Id);
            stepPatternEditor_->setStepLevel(stepIndex, static_cast<float>(value));
        } else if (tag == kTranceGateNumStepsId) {
            int steps = std::clamp(
                static_cast<int>(2.0 + std::round(value * 30.0)), 2, 32);
            stepPatternEditor_->setNumSteps(steps);
        } else if (tag == kTranceGateEuclideanEnabledId) {
            stepPatternEditor_->setEuclideanEnabled(value >= 0.5);
            if (euclideanControlsGroup_)
                euclideanControlsGroup_->setVisible(value >= 0.5);
        } else if (tag == kTranceGateEuclideanHitsId) {
            int hits = std::clamp(
                static_cast<int>(std::round(value * 32.0)), 0, 32);
            stepPatternEditor_->setEuclideanHits(hits);
        } else if (tag == kTranceGateEuclideanRotationId) {
            int rot = std::clamp(
                static_cast<int>(std::round(value * 31.0)), 0, 31);
            stepPatternEditor_->setEuclideanRotation(rot);
        } else if (tag == kTranceGatePhaseOffsetId) {
            stepPatternEditor_->setPhaseOffset(static_cast<float>(value));
        }
    }

    // Push velocity lane parameter changes to ArpLaneEditor (079-layout-framework)
    if (velocityLane_) {
        if (tag >= kArpVelocityLaneStep0Id && tag <= kArpVelocityLaneStep31Id) {
            int stepIndex = static_cast<int>(tag - kArpVelocityLaneStep0Id);
            velocityLane_->setStepLevel(stepIndex, static_cast<float>(value));
            velocityLane_->setDirty(true);
        } else if (tag == kArpVelocityLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            velocityLane_->setNumSteps(steps);
            velocityLane_->setDirty(true);
        }
    }

    // Push gate lane parameter changes to ArpLaneEditor (079-layout-framework, US2)
    if (gateLane_) {
        if (tag >= kArpGateLaneStep0Id && tag <= kArpGateLaneStep31Id) {
            int stepIndex = static_cast<int>(tag - kArpGateLaneStep0Id);
            gateLane_->setStepLevel(stepIndex, static_cast<float>(value));
            gateLane_->setDirty(true);
        } else if (tag == kArpGateLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            gateLane_->setNumSteps(steps);
            gateLane_->setDirty(true);
        }
    }

    // Push pitch lane parameter changes (080-specialized-lane-types)
    if (pitchLane_) {
        if (tag >= kArpPitchLaneStep0Id && tag < kArpPitchLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpPitchLaneStep0Id);
            pitchLane_->setStepLevel(stepIndex, static_cast<float>(value));
            pitchLane_->setDirty(true);
        } else if (tag == kArpPitchLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            pitchLane_->setNumSteps(steps);
            pitchLane_->setDirty(true);
        }
    }

    // Push ratchet lane parameter changes (080-specialized-lane-types)
    if (ratchetLane_) {
        if (tag >= kArpRatchetLaneStep0Id && tag < kArpRatchetLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpRatchetLaneStep0Id);
            ratchetLane_->setStepLevel(stepIndex, static_cast<float>(value));
            ratchetLane_->setDirty(true);
        } else if (tag == kArpRatchetLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            ratchetLane_->setNumSteps(steps);
            ratchetLane_->setDirty(true);
        }
    }

    // Push modifier lane parameter changes (080-specialized-lane-types)
    if (modifierLane_) {
        if (tag >= kArpModifierLaneStep0Id && tag < kArpModifierLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpModifierLaneStep0Id);
            auto flags = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 255.0)), 0, 255));
            modifierLane_->setStepFlags(stepIndex, flags);
            modifierLane_->setDirty(true);
        } else if (tag == kArpModifierLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            modifierLane_->setNumSteps(steps);
            modifierLane_->setDirty(true);
        }
    }

    // Push condition lane parameter changes (080-specialized-lane-types)
    if (conditionLane_) {
        if (tag >= kArpConditionLaneStep0Id && tag < kArpConditionLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpConditionLaneStep0Id);
            auto condIndex = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 17.0)), 0, 17));
            conditionLane_->setStepCondition(stepIndex, condIndex);
            conditionLane_->setDirty(true);
        } else if (tag == kArpConditionLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            conditionLane_->setNumSteps(steps);
            conditionLane_->setDirty(true);
        }
    }

    // Push chord lane parameter changes (arp-chord-lane)
    if (chordLane_) {
        if (tag >= kArpChordLaneStep0Id && tag < kArpChordLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpChordLaneStep0Id);
            auto chordIdx = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 4.0)), 0, 4));
            chordLane_->setStepValue(stepIndex, chordIdx);
            chordLane_->setDirty(true);
        } else if (tag == kArpChordLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            chordLane_->setNumSteps(steps);
            chordLane_->setDirty(true);
        }
    }

    // Push inversion lane parameter changes (arp-chord-lane)
    if (inversionLane_) {
        if (tag >= kArpInversionLaneStep0Id && tag < kArpInversionLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpInversionLaneStep0Id);
            auto invIdx = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 3.0)), 0, 3));
            inversionLane_->setStepValue(stepIndex, invIdx);
            inversionLane_->setDirty(true);
        } else if (tag == kArpInversionLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            inversionLane_->setNumSteps(steps);
            inversionLane_->setDirty(true);
        }
    }

    // 081-interaction-polish US5: Push arp Euclidean parameter changes to
    // EuclideanDotDisplay and linear overlays on bar lanes
    if (tag == kArpEuclideanHitsId || tag == kArpEuclideanStepsId ||
        tag == kArpEuclideanRotationId || tag == kArpEuclideanEnabledId) {
        auto readInt = [this](Steinberg::Vst::ParamID pid, double scale,
                              double offset, int lo, int hi) -> int {
            auto* p = getParameterObject(pid);
            if (!p) return lo;
            return std::clamp(
                static_cast<int>(offset + std::round(p->getNormalized() * scale)),
                lo, hi);
        };
        int hits = readInt(kArpEuclideanHitsId, 32.0, 0.0, 0, 32);
        int steps = readInt(kArpEuclideanStepsId, 30.0, 2.0, 2, 32);
        int rot = readInt(kArpEuclideanRotationId, 31.0, 0.0, 0, 31);
        auto* enabledParam = getParameterObject(kArpEuclideanEnabledId);
        bool enabled = (enabledParam != nullptr) && enabledParam->getNormalized() >= 0.5;

        if (euclideanDotDisplay_) {
            euclideanDotDisplay_->setSteps(steps);
            euclideanDotDisplay_->setHits(hits);
            euclideanDotDisplay_->setRotation(rot);
            euclideanDotDisplay_->invalid();
        }

        Krate::Plugins::IArpLane* lanes[] = {
            velocityLane_, gateLane_, pitchLane_, ratchetLane_,
            modifierLane_, conditionLane_, chordLane_, inversionLane_};
        for (auto* lane : lanes) {
            if (lane) lane->setEuclideanOverlay(hits, steps, rot, enabled);
        }

        if (tag == kArpEuclideanEnabledId && arpEuclideanGroup_) {
            arpEuclideanGroup_->setVisible(enabled);
        }
    }

    // Toggle LFO Rate/NoteValue visibility based on sync state
    if (tag == kLFO1SyncId) {
        if (lfo1RateGroup_) lfo1RateGroup_->setVisible(value < 0.5);
        if (lfo1NoteValueGroup_) lfo1NoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kLFO2SyncId) {
        if (lfo2RateGroup_) lfo2RateGroup_->setVisible(value < 0.5);
        if (lfo2NoteValueGroup_) lfo2NoteValueGroup_->setVisible(value >= 0.5);
    }

    // Update LFO waveform displays when relevant parameters change
    // Helper: compute LFO frequency from sync state, rate, and note value params
    auto computeLfoFreq = [this](Steinberg::Vst::ParamID syncId,
                                  Steinberg::Vst::ParamID rateId,
                                  Steinberg::Vst::ParamID noteValueId) -> float {
        auto* syncParam = getParameterObject(syncId);
        bool synced = syncParam && syncParam->getNormalized() >= 0.5;
        if (synced) {
            auto* nvParam = getParameterObject(noteValueId);
            int nvIdx = nvParam ? static_cast<int>(
                nvParam->getNormalized() * (Krate::DSP::kNoteValueDropdownCount - 1) + 0.5) : 10;
            auto mapping = Krate::DSP::getNoteValueFromDropdown(nvIdx);
            float beats = Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
            float bps = Krate::Plugins::LfoWaveformDisplay::kDefaultBpm / 60.0f;
            return bps / beats;
        }
        auto* rateParam = getParameterObject(rateId);
        return rateParam ? lfoRateFromNormalized(rateParam->getNormalized()) : 1.0f;
    };

    if (lfo1WaveformDisplay_) {
        if (tag == kLFO1ShapeId)
            lfo1WaveformDisplay_->setShape(static_cast<int>(value * 5.0 + 0.5));
        else if (tag == kLFO1SymmetryId)
            lfo1WaveformDisplay_->setSymmetry(static_cast<float>(value));
        else if (tag == kLFO1QuantizeId)
            lfo1WaveformDisplay_->setQuantizeSteps(lfoQuantizeFromNormalized(value));
        else if (tag == kLFO1UnipolarId)
            lfo1WaveformDisplay_->setUnipolar(value >= 0.5);
        else if (tag == kLFO1PhaseOffsetId)
            lfo1WaveformDisplay_->setPhaseOffset(static_cast<float>(value));
        else if (tag == kLFO1DepthId)
            lfo1WaveformDisplay_->setDepth(static_cast<float>(value));
        else if (tag == kLFO1FadeInId)
            lfo1WaveformDisplay_->setFadeInMs(lfoFadeInFromNormalized(value));
        if (tag == kLFO1RateId || tag == kLFO1SyncId || tag == kLFO1NoteValueId)
            lfo1WaveformDisplay_->setFrequencyHz(
                computeLfoFreq(kLFO1SyncId, kLFO1RateId, kLFO1NoteValueId));
    }
    if (lfo2WaveformDisplay_) {
        if (tag == kLFO2ShapeId)
            lfo2WaveformDisplay_->setShape(static_cast<int>(value * 5.0 + 0.5));
        else if (tag == kLFO2SymmetryId)
            lfo2WaveformDisplay_->setSymmetry(static_cast<float>(value));
        else if (tag == kLFO2QuantizeId)
            lfo2WaveformDisplay_->setQuantizeSteps(lfoQuantizeFromNormalized(value));
        else if (tag == kLFO2UnipolarId)
            lfo2WaveformDisplay_->setUnipolar(value >= 0.5);
        else if (tag == kLFO2PhaseOffsetId)
            lfo2WaveformDisplay_->setPhaseOffset(static_cast<float>(value));
        else if (tag == kLFO2DepthId)
            lfo2WaveformDisplay_->setDepth(static_cast<float>(value));
        else if (tag == kLFO2FadeInId)
            lfo2WaveformDisplay_->setFadeInMs(lfoFadeInFromNormalized(value));
        if (tag == kLFO2RateId || tag == kLFO2SyncId || tag == kLFO2NoteValueId)
            lfo2WaveformDisplay_->setFrequencyHz(
                computeLfoFreq(kLFO2SyncId, kLFO2RateId, kLFO2NoteValueId));
    }
    if (tag == kChaosModSyncId) {
        if (chaosRateGroup_) chaosRateGroup_->setVisible(value < 0.5);
        if (chaosNoteValueGroup_) chaosNoteValueGroup_->setVisible(value >= 0.5);
    }
    // Update chaos mod display when type, rate, depth, sync, or note value changes
    if (chaosModDisplay_) {
        if (tag == kChaosModTypeId)
            chaosModDisplay_->setModel(static_cast<int>(value * (kChaosTypeCount - 1) + 0.5));
        if (tag == kChaosModRateId || tag == kChaosModSyncId || tag == kChaosModNoteValueId)
            chaosModDisplay_->setSpeed(
                computeLfoFreq(kChaosModSyncId, kChaosModRateId, kChaosModNoteValueId));
        if (tag == kChaosModDepthId)
            chaosModDisplay_->setDepth(static_cast<float>(value));
    }
    // Update rungler display when any rungler parameter changes
    if (runglerDisplay_) {
        if (tag == kRunglerOsc1FreqId)
            runglerDisplay_->setOsc1Freq(runglerFreqFromNormalized(value));
        if (tag == kRunglerOsc2FreqId)
            runglerDisplay_->setOsc2Freq(runglerFreqFromNormalized(value));
        if (tag == kRunglerDepthId)
            runglerDisplay_->setDepth(static_cast<float>(value));
        if (tag == kRunglerFilterId)
            runglerDisplay_->setFilterAmount(static_cast<float>(value));
        if (tag == kRunglerBitsId)
            runglerDisplay_->setBits(runglerBitsFromNormalized(value));
        if (tag == kRunglerLoopModeId)
            runglerDisplay_->setLoopMode(value >= 0.5);
    }
    // Update S&H display when rate, slew, sync, or note value changes
    if (sampleHoldDisplay_) {
        if (tag == kSampleHoldRateId || tag == kSampleHoldSyncId || tag == kSampleHoldNoteValueId)
            sampleHoldDisplay_->setRate(
                computeLfoFreq(kSampleHoldSyncId, kSampleHoldRateId, kSampleHoldNoteValueId));
        if (tag == kSampleHoldSlewId)
            sampleHoldDisplay_->setSlew(sampleHoldSlewFromNormalized(value));
    }
    // Update random display when rate, smoothness, sync, or note value changes
    if (randomModDisplay_) {
        if (tag == kRandomRateId || tag == kRandomSyncId || tag == kRandomNoteValueId)
            randomModDisplay_->setRate(
                computeLfoFreq(kRandomSyncId, kRandomRateId, kRandomNoteValueId));
        if (tag == kRandomSmoothnessId)
            randomModDisplay_->setSmoothness(static_cast<float>(value));
    }
    if (tag == kSampleHoldSyncId) {
        if (shRateGroup_) shRateGroup_->setVisible(value < 0.5);
        if (shNoteValueGroup_) shNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kRandomSyncId) {
        if (randomRateGroup_) randomRateGroup_->setVisible(value < 0.5);
        if (randomNoteValueGroup_) randomNoteValueGroup_->setVisible(value >= 0.5);
    }
    // Update sidechain indicator on all 3 audio-dependent mod source views
    if (tag == kSidechainActiveId) {
        sidechainActive_ = value >= 0.5;
        updateSidechainIndicator(sidechainIndicatorEnvFollower_);
        updateSidechainIndicator(sidechainIndicatorPitchFollower_);
        updateSidechainIndicator(sidechainIndicatorTransient_);
    }
    if (tag == kDelaySyncId) {
        if (delayTimeGroup_) delayTimeGroup_->setVisible(value < 0.5);
        if (delayNoteValueGroup_) delayNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kPhaserSyncId) {
        if (phaserRateGroup_) phaserRateGroup_->setVisible(value < 0.5);
        if (phaserNoteValueGroup_) phaserNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kFlangerSyncId) {
        if (flangerRateGroup_) flangerRateGroup_->setVisible(value < 0.5);
        if (flangerNoteValueGroup_) flangerNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kChorusSyncId) {
        if (chorusRateGroup_) chorusRateGroup_->setVisible(value < 0.5);
        if (chorusNoteValueGroup_) chorusNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kModulationTypeId) {
        // 4-step: 0=None, 1=Phaser, 2=Flanger, 3=Chorus. Normalized: 0.0, 0.333, 0.667, 1.0
        int modType = static_cast<int>(value * 3.0 + 0.5);
        if (noModulationGroup_) noModulationGroup_->setVisible(modType == 0);
        if (phaserControlsGroup_) phaserControlsGroup_->setVisible(modType == 1);
        if (flangerControlsGroup_) flangerControlsGroup_->setVisible(modType == 2);
        if (chorusControlsGroup_) chorusControlsGroup_->setVisible(modType == 3);
    }
    if (tag == kTranceGateTempoSyncId) {
        if (tranceGateRateGroup_) tranceGateRateGroup_->setVisible(value < 0.5);
        if (tranceGateNoteValueGroup_) tranceGateNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kArpTempoSyncId) {
        if (arpRateGroup_) arpRateGroup_->setVisible(value < 0.5);
        if (arpNoteValueGroup_) arpNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kVoiceModeId) {
        if (polyGroup_) polyGroup_->setVisible(value < 0.5);
        if (monoGroup_) monoGroup_->setVisible(value >= 0.5);
        bool isMono = value >= 0.5;
        if (chordLane_)
            chordLane_->setDisabled(isMono, "Chord lane requires Poly voice mode");
        if (inversionLane_)
            inversionLane_->setDisabled(isMono, "Inversion lane requires Poly voice mode");
    }

    // Harmonizer voice row dimming based on NumVoices
    if (tag == kHarmonizerNumVoicesId) {
        int numVoices = static_cast<int>(
            value * (kHarmonizerNumVoicesCount - 1) + 0.5) + 1;
        for (int i = 0; i < 4; ++i) {
            if (harmonizerVoiceRows_[static_cast<size_t>(i)]) {
                harmonizerVoiceRows_[static_cast<size_t>(i)]->setAlphaValue(
                    i < numVoices ? 1.0f : 0.3f);
            }
        }
    }

    // Arp Scale Mode dimming (084-arp-scale-mode FR-011)
    if (tag == kArpScaleTypeId) {
        bool isChromatic = (value < 0.01);
        float alpha = isChromatic ? 0.35f : 1.0f;
        if (arpRootNoteGroup_) {
            arpRootNoteGroup_->setAlphaValue(alpha);
            arpRootNoteGroup_->setMouseEnabled(!isChromatic);
        }
        if (arpQuantizeInputGroup_) {
            arpQuantizeInputGroup_->setAlphaValue(alpha);
            arpQuantizeInputGroup_->setMouseEnabled(!isChromatic);
        }
        if (pitchLane_) {
            int uiIndex = std::clamp(
                static_cast<int>(value * (kArpScaleTypeCount - 1) + 0.5),
                0, kArpScaleTypeCount - 1);
            int enumValue = kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)];
            pitchLane_->setScaleType(enumValue);
        }
    }

    // PW knob visual disable (068-osc-type-params FR-016)
    if (tag == kOscAWaveformId && oscAPWKnob_) {
        int wf = static_cast<int>(value * 4.0 + 0.5);
        oscAPWKnob_->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
    }
    if (tag == kOscBWaveformId && oscBPWKnob_) {
        int wf = static_cast<int>(value * 4.0 + 0.5);
        oscBPWKnob_->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
    }
    // Spectral distortion control dimming
    if (tag == kDistortionSpectralModeId) {
        int mode = std::clamp(
            static_cast<int>(value * (kSpectralModeCount - 1) + 0.5),
            0, kSpectralModeCount - 1);
        bool isBitcrush = (mode == 3);
        if (spectralCurveDropdown_) {
            spectralCurveDropdown_->setAlphaValue(isBitcrush ? 0.35f : 1.0f);
            spectralCurveDropdown_->setMouseEnabled(!isBitcrush);
        }
        if (spectralBitsGroup_) {
            spectralBitsGroup_->setAlphaValue(isBitcrush ? 1.0f : 0.35f);
            spectralBitsGroup_->setMouseEnabled(isBitcrush);
        }
    }
    // Push mixer parameter changes to XYMorphPad
    if (xyMorphPad_ && !modulatedMorphXPtr_) {
        if (tag == kMixerPositionId) {
            xyMorphPad_->setMorphPosition(
                static_cast<float>(value), xyMorphPad_->getMorphY());
        } else if (tag == kMixerTiltId) {
            xyMorphPad_->setMorphPosition(
                xyMorphPad_->getMorphX(), static_cast<float>(value));
        }
    }

    // Push envelope parameter changes to ADSRDisplay instances
    syncAdsrParamToDisplay(tag, value, ampEnvDisplay_,
        kAmpEnvAttackId, kAmpEnvAttackCurveId,
        kAmpEnvBezierEnabledId, kAmpEnvBezierAttackCp1XId);
    syncAdsrParamToDisplay(tag, value, filterEnvDisplay_,
        kFilterEnvAttackId, kFilterEnvAttackCurveId,
        kFilterEnvBezierEnabledId, kFilterEnvBezierAttackCp1XId);
    syncAdsrParamToDisplay(tag, value, modEnvDisplay_,
        kModEnvAttackId, kModEnvAttackCurveId,
        kModEnvBezierEnabledId, kModEnvBezierAttackCp1XId);

    // Forward to expanded ADSR display if open and matching current envelope
    if (adsrExpandedOverlay_ && adsrExpandedOverlay_->isOpen()) {
        auto* expandedDisplay = adsrExpandedOverlay_->getDisplay();
        if (expandedDisplay) {
            if (expandedEnvType_ == EnvelopeType::kAmp) {
                syncAdsrParamToDisplay(tag, value, expandedDisplay,
                    kAmpEnvAttackId, kAmpEnvAttackCurveId,
                    kAmpEnvBezierEnabledId, kAmpEnvBezierAttackCp1XId);
            } else if (expandedEnvType_ == EnvelopeType::kFilter) {
                syncAdsrParamToDisplay(tag, value, expandedDisplay,
                    kFilterEnvAttackId, kFilterEnvAttackCurveId,
                    kFilterEnvBezierEnabledId, kFilterEnvBezierAttackCp1XId);
            } else if (expandedEnvType_ == EnvelopeType::kMod) {
                syncAdsrParamToDisplay(tag, value, expandedDisplay,
                    kModEnvAttackId, kModEnvAttackCurveId,
                    kModEnvBezierEnabledId, kModEnvBezierAttackCp1XId);
            }
        }
    }

    // Push mod matrix parameter changes to ModMatrixGrid and ModRingIndicators
    if (tag >= kModMatrixBaseId && tag <= kModMatrixDetailEndId) {
        if (modMatrixGrid_ && !suppressModMatrixSync_) {
            syncModMatrixGrid();
        }
        rebuildRingIndicators();
    }

    // Sync destination knob value to ModRingIndicator base value
    for (int i = 0; i < kMaxRingIndicators; ++i) {
        if (ringIndicators_[static_cast<size_t>(i)] &&
            kVoiceDestParamIds[static_cast<size_t>(i)] == tag) {
            ringIndicators_[static_cast<size_t>(i)]->setBaseValue(
                static_cast<float>(value));
            break;
        }
    }

    return result;
}

// ==============================================================================
// syncAllViews: Batch sync after preset load
// ==============================================================================
// Called once after setComponentState() or loadComponentStateWithNotify() to
// sync all custom views from current parameter state. Replaces thousands of
// per-param invalidRect() calls with a single full-frame repaint.

void Controller::syncAllViews() {
    // Helper: read a normalized param value, return 0.0 if not found
    auto paramNorm = [this](Steinberg::Vst::ParamID id) -> double {
        auto* p = getParameterObject(id);
        return p ? p->getNormalized() : 0.0;
    };
    // Helper: read normalized param → clamped int
    auto paramInt = [&](Steinberg::Vst::ParamID id, double scale,
                        double offset, int lo, int hi) -> int {
        return std::clamp(
            static_cast<int>(offset + std::round(paramNorm(id) * scale)),
            lo, hi);
    };

    // ---- StepPatternEditor (trance gate) ----
    if (stepPatternEditor_) {
        for (int i = 0; i < 32; ++i) {
            auto stepVal = static_cast<float>(
                paramNorm(kTranceGateStepLevel0Id + static_cast<uint32_t>(i)));
            stepPatternEditor_->setStepLevel(i, stepVal);
        }
        int numSteps = paramInt(kTranceGateNumStepsId, 30.0, 2.0, 2, 32);
        stepPatternEditor_->setNumSteps(numSteps);
        bool eucEnabled = paramNorm(kTranceGateEuclideanEnabledId) >= 0.5;
        stepPatternEditor_->setEuclideanEnabled(eucEnabled);
        int eucHits = paramInt(kTranceGateEuclideanHitsId, 32.0, 0.0, 0, 32);
        stepPatternEditor_->setEuclideanHits(eucHits);
        int eucRot = paramInt(kTranceGateEuclideanRotationId, 31.0, 0.0, 0, 31);
        stepPatternEditor_->setEuclideanRotation(eucRot);
        stepPatternEditor_->setPhaseOffset(
            static_cast<float>(paramNorm(kTranceGatePhaseOffsetId)));

        if (euclideanControlsGroup_)
            euclideanControlsGroup_->setVisible(eucEnabled);
    }

    // ---- Arp lane editors (velocity, gate, pitch, ratchet, modifier, condition) ----
    auto syncBarLane = [&](Krate::Plugins::ArpLaneEditor* lane,
                           Steinberg::Vst::ParamID stepBase,
                           Steinberg::Vst::ParamID lengthId) {
        if (!lane) return;
        for (int i = 0; i < 32; ++i) {
            lane->setStepLevel(i, static_cast<float>(
                paramNorm(stepBase + static_cast<uint32_t>(i))));
        }
        int steps = paramInt(lengthId, 31.0, 1.0, 1, 32);
        lane->setNumSteps(steps);
        lane->setDirty(true);
    };
    syncBarLane(velocityLane_, kArpVelocityLaneStep0Id, kArpVelocityLaneLengthId);
    syncBarLane(gateLane_, kArpGateLaneStep0Id, kArpGateLaneLengthId);
    syncBarLane(pitchLane_, kArpPitchLaneStep0Id, kArpPitchLaneLengthId);
    syncBarLane(ratchetLane_, kArpRatchetLaneStep0Id, kArpRatchetLaneLengthId);

    // Modifier lane (flags, not float levels)
    if (modifierLane_) {
        for (int i = 0; i < 32; ++i) {
            auto flags = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(
                    paramNorm(kArpModifierLaneStep0Id + static_cast<uint32_t>(i)) * 255.0)),
                0, 255));
            modifierLane_->setStepFlags(i, flags);
        }
        int modSteps = paramInt(kArpModifierLaneLengthId, 31.0, 1.0, 1, 32);
        modifierLane_->setNumSteps(modSteps);
        modifierLane_->setDirty(true);
    }

    // Condition lane (condition indices, not float levels)
    if (conditionLane_) {
        for (int i = 0; i < 32; ++i) {
            auto condIndex = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(
                    paramNorm(kArpConditionLaneStep0Id + static_cast<uint32_t>(i)) * 17.0)),
                0, 17));
            conditionLane_->setStepCondition(i, condIndex);
        }
        int condSteps = paramInt(kArpConditionLaneLengthId, 31.0, 1.0, 1, 32);
        conditionLane_->setNumSteps(condSteps);
        conditionLane_->setDirty(true);
    }

    // Chord lane (chord indices)
    if (chordLane_) {
        for (int i = 0; i < 32; ++i) {
            auto chordIdx = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(
                    paramNorm(kArpChordLaneStep0Id + static_cast<uint32_t>(i)) * 4.0)),
                0, 4));
            chordLane_->setStepValue(i, chordIdx);
        }
        int chordSteps = paramInt(kArpChordLaneLengthId, 31.0, 1.0, 1, 32);
        chordLane_->setNumSteps(chordSteps);
        chordLane_->setDirty(true);
    }

    // Inversion lane (inversion indices)
    if (inversionLane_) {
        for (int i = 0; i < 32; ++i) {
            auto invIdx = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(
                    paramNorm(kArpInversionLaneStep0Id + static_cast<uint32_t>(i)) * 3.0)),
                0, 3));
            inversionLane_->setStepValue(i, invIdx);
        }
        int invSteps = paramInt(kArpInversionLaneLengthId, 31.0, 1.0, 1, 32);
        inversionLane_->setNumSteps(invSteps);
        inversionLane_->setDirty(true);
    }

    // ---- Arp Euclidean overlay + dot display ----
    {
        int arpEucHits = paramInt(kArpEuclideanHitsId, 32.0, 0.0, 0, 32);
        int arpEucSteps = paramInt(kArpEuclideanStepsId, 30.0, 2.0, 2, 32);
        int arpEucRot = paramInt(kArpEuclideanRotationId, 31.0, 0.0, 0, 31);
        bool arpEucEnabled = paramNorm(kArpEuclideanEnabledId) >= 0.5;

        if (euclideanDotDisplay_) {
            euclideanDotDisplay_->setSteps(arpEucSteps);
            euclideanDotDisplay_->setHits(arpEucHits);
            euclideanDotDisplay_->setRotation(arpEucRot);
        }

        Krate::Plugins::IArpLane* lanes[] = {
            velocityLane_, gateLane_, pitchLane_, ratchetLane_,
            modifierLane_, conditionLane_, chordLane_, inversionLane_};
        for (auto* lane : lanes) {
            if (lane) lane->setEuclideanOverlay(arpEucHits, arpEucSteps, arpEucRot, arpEucEnabled);
        }

        if (arpEuclideanGroup_)
            arpEuclideanGroup_->setVisible(arpEucEnabled);
    }

    // ---- Visibility toggles (sync groups) ----
    auto syncVisGroup = [&](Steinberg::Vst::ParamID syncId,
                            VSTGUI::CView* freeGroup,
                            VSTGUI::CView* syncGroup) {
        double v = paramNorm(syncId);
        if (freeGroup) freeGroup->setVisible(v < 0.5);
        if (syncGroup) syncGroup->setVisible(v >= 0.5);
    };
    syncVisGroup(kLFO1SyncId, lfo1RateGroup_, lfo1NoteValueGroup_);
    syncVisGroup(kLFO2SyncId, lfo2RateGroup_, lfo2NoteValueGroup_);

    // ---- LFO Waveform Displays ----
    auto syncLfoWaveform = [&](Krate::Plugins::LfoWaveformDisplay* display,
                               Steinberg::Vst::ParamID shapeId,
                               Steinberg::Vst::ParamID symmetryId,
                               Steinberg::Vst::ParamID quantizeId,
                               Steinberg::Vst::ParamID unipolarId,
                               Steinberg::Vst::ParamID phaseId,
                               Steinberg::Vst::ParamID rateId,
                               Steinberg::Vst::ParamID syncId,
                               Steinberg::Vst::ParamID noteValueId,
                               Steinberg::Vst::ParamID depthId,
                               Steinberg::Vst::ParamID fadeInId) {
        if (!display) return;
        display->setShape(static_cast<int>(paramNorm(shapeId) * 5.0 + 0.5));
        display->setSymmetry(static_cast<float>(paramNorm(symmetryId)));
        display->setQuantizeSteps(lfoQuantizeFromNormalized(paramNorm(quantizeId)));
        display->setUnipolar(paramNorm(unipolarId) >= 0.5);
        display->setPhaseOffset(static_cast<float>(paramNorm(phaseId)));
        display->setDepth(static_cast<float>(paramNorm(depthId)));
        display->setFadeInMs(lfoFadeInFromNormalized(paramNorm(fadeInId)));

        bool synced = paramNorm(syncId) >= 0.5;
        if (synced) {
            int nvIdx = static_cast<int>(
                paramNorm(noteValueId) * (Krate::DSP::kNoteValueDropdownCount - 1) + 0.5);
            auto mapping = Krate::DSP::getNoteValueFromDropdown(nvIdx);
            float beats = Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
            float bps = Krate::Plugins::LfoWaveformDisplay::kDefaultBpm / 60.0f;
            display->setFrequencyHz(bps / beats);
        } else {
            display->setFrequencyHz(lfoRateFromNormalized(paramNorm(rateId)));
        }
    };
    syncLfoWaveform(lfo1WaveformDisplay_,
                    kLFO1ShapeId, kLFO1SymmetryId, kLFO1QuantizeId,
                    kLFO1UnipolarId, kLFO1PhaseOffsetId,
                    kLFO1RateId, kLFO1SyncId, kLFO1NoteValueId,
                    kLFO1DepthId, kLFO1FadeInId);
    syncLfoWaveform(lfo2WaveformDisplay_,
                    kLFO2ShapeId, kLFO2SymmetryId, kLFO2QuantizeId,
                    kLFO2UnipolarId, kLFO2PhaseOffsetId,
                    kLFO2RateId, kLFO2SyncId, kLFO2NoteValueId,
                    kLFO2DepthId, kLFO2FadeInId);
    // Sync chaos mod display
    if (chaosModDisplay_) {
        chaosModDisplay_->setModel(static_cast<int>(
            paramNorm(kChaosModTypeId) * (kChaosTypeCount - 1) + 0.5));
        // Compute effective speed from sync state
        bool chaosSynced = paramNorm(kChaosModSyncId) >= 0.5;
        if (chaosSynced) {
            int nvIdx = static_cast<int>(
                paramNorm(kChaosModNoteValueId) * (Krate::DSP::kNoteValueDropdownCount - 1) + 0.5);
            auto mapping = Krate::DSP::getNoteValueFromDropdown(nvIdx);
            float beats = Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
            float bps = 120.0f / 60.0f;
            chaosModDisplay_->setSpeed(bps / beats);
        } else {
            chaosModDisplay_->setSpeed(lfoRateFromNormalized(paramNorm(kChaosModRateId)));
        }
        chaosModDisplay_->setDepth(static_cast<float>(paramNorm(kChaosModDepthId)));
    }
    // Sync rungler display
    if (runglerDisplay_) {
        runglerDisplay_->setOsc1Freq(runglerFreqFromNormalized(paramNorm(kRunglerOsc1FreqId)));
        runglerDisplay_->setOsc2Freq(runglerFreqFromNormalized(paramNorm(kRunglerOsc2FreqId)));
        runglerDisplay_->setDepth(static_cast<float>(paramNorm(kRunglerDepthId)));
        runglerDisplay_->setFilterAmount(static_cast<float>(paramNorm(kRunglerFilterId)));
        runglerDisplay_->setBits(runglerBitsFromNormalized(paramNorm(kRunglerBitsId)));
        runglerDisplay_->setLoopMode(paramNorm(kRunglerLoopModeId) >= 0.5);
    }
    // Sync S&H display
    if (sampleHoldDisplay_) {
        bool shSynced = paramNorm(kSampleHoldSyncId) >= 0.5;
        if (shSynced) {
            int nvIdx = static_cast<int>(
                paramNorm(kSampleHoldNoteValueId) * (Krate::DSP::kNoteValueDropdownCount - 1) + 0.5);
            auto mapping = Krate::DSP::getNoteValueFromDropdown(nvIdx);
            float beats = Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
            float bps = 120.0f / 60.0f;
            sampleHoldDisplay_->setRate(bps / beats);
        } else {
            sampleHoldDisplay_->setRate(lfoRateFromNormalized(paramNorm(kSampleHoldRateId)));
        }
        sampleHoldDisplay_->setSlew(sampleHoldSlewFromNormalized(paramNorm(kSampleHoldSlewId)));
    }
    // Sync random display
    if (randomModDisplay_) {
        bool rndSynced = paramNorm(kRandomSyncId) >= 0.5;
        if (rndSynced) {
            int nvIdx = static_cast<int>(
                paramNorm(kRandomNoteValueId) * (Krate::DSP::kNoteValueDropdownCount - 1) + 0.5);
            auto mapping = Krate::DSP::getNoteValueFromDropdown(nvIdx);
            float beats = Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
            float bps = 120.0f / 60.0f;
            randomModDisplay_->setRate(bps / beats);
        } else {
            randomModDisplay_->setRate(lfoRateFromNormalized(paramNorm(kRandomRateId)));
        }
        randomModDisplay_->setSmoothness(static_cast<float>(paramNorm(kRandomSmoothnessId)));
    }
    // Sync sidechain indicators
    sidechainActive_ = paramNorm(kSidechainActiveId) >= 0.5;
    updateSidechainIndicator(sidechainIndicatorEnvFollower_);
    updateSidechainIndicator(sidechainIndicatorPitchFollower_);
    updateSidechainIndicator(sidechainIndicatorTransient_);
    syncVisGroup(kChaosModSyncId, chaosRateGroup_, chaosNoteValueGroup_);
    syncVisGroup(kSampleHoldSyncId, shRateGroup_, shNoteValueGroup_);
    syncVisGroup(kRandomSyncId, randomRateGroup_, randomNoteValueGroup_);
    syncVisGroup(kDelaySyncId, delayTimeGroup_, delayNoteValueGroup_);
    syncVisGroup(kPhaserSyncId, phaserRateGroup_, phaserNoteValueGroup_);
    syncVisGroup(kFlangerSyncId, flangerRateGroup_, flangerNoteValueGroup_);
    syncVisGroup(kChorusSyncId, chorusRateGroup_, chorusNoteValueGroup_);

    // Sync modulation type visibility (None / Phaser / Flanger / Chorus)
    {
        auto* modParam = getParameterObject(kModulationTypeId);
        int modType = modParam ? static_cast<int>(modParam->getNormalized() * 3.0 + 0.5) : 0;
        if (noModulationGroup_) noModulationGroup_->setVisible(modType == 0);
        if (phaserControlsGroup_) phaserControlsGroup_->setVisible(modType == 1);
        if (flangerControlsGroup_) flangerControlsGroup_->setVisible(modType == 2);
        if (chorusControlsGroup_) chorusControlsGroup_->setVisible(modType == 3);
    }
    syncVisGroup(kTranceGateTempoSyncId, tranceGateRateGroup_, tranceGateNoteValueGroup_);
    syncVisGroup(kArpTempoSyncId, arpRateGroup_, arpNoteValueGroup_);

    // Poly/Mono
    {
        double vm = paramNorm(kVoiceModeId);
        if (polyGroup_) polyGroup_->setVisible(vm < 0.5);
        if (monoGroup_) monoGroup_->setVisible(vm >= 0.5);
        bool isMono = vm >= 0.5;
        if (chordLane_)
            chordLane_->setDisabled(isMono, "Chord lane requires Poly voice mode");
        if (inversionLane_)
            inversionLane_->setDisabled(isMono, "Inversion lane requires Poly voice mode");
    }

    // ---- Harmonizer voice row dimming ----
    {
        double hv = paramNorm(kHarmonizerNumVoicesId);
        int numVoices = static_cast<int>(hv * (kHarmonizerNumVoicesCount - 1) + 0.5) + 1;
        for (int i = 0; i < 4; ++i) {
            if (harmonizerVoiceRows_[static_cast<size_t>(i)]) {
                harmonizerVoiceRows_[static_cast<size_t>(i)]->setAlphaValue(
                    i < numVoices ? 1.0f : 0.3f);
            }
        }
    }

    // ---- Arp Scale Mode dimming ----
    {
        double scaleNorm = paramNorm(kArpScaleTypeId);
        bool isChromatic = (scaleNorm < 0.01);
        float alpha = isChromatic ? 0.35f : 1.0f;
        if (arpRootNoteGroup_) {
            arpRootNoteGroup_->setAlphaValue(alpha);
            arpRootNoteGroup_->setMouseEnabled(!isChromatic);
        }
        if (arpQuantizeInputGroup_) {
            arpQuantizeInputGroup_->setAlphaValue(alpha);
            arpQuantizeInputGroup_->setMouseEnabled(!isChromatic);
        }
        if (pitchLane_) {
            int uiIndex = std::clamp(
                static_cast<int>(scaleNorm * (kArpScaleTypeCount - 1) + 0.5),
                0, kArpScaleTypeCount - 1);
            int enumValue = kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)];
            pitchLane_->setScaleType(enumValue);
        }
    }

    // ---- PW knob dimming ----
    if (oscAPWKnob_) {
        int wf = static_cast<int>(paramNorm(kOscAWaveformId) * 4.0 + 0.5);
        oscAPWKnob_->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
    }
    if (oscBPWKnob_) {
        int wf = static_cast<int>(paramNorm(kOscBWaveformId) * 4.0 + 0.5);
        oscBPWKnob_->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
    }

    // ---- Spectral distortion control dimming ----
    {
        double modeNorm = paramNorm(kDistortionSpectralModeId);
        int mode = std::clamp(
            static_cast<int>(modeNorm * (kSpectralModeCount - 1) + 0.5),
            0, kSpectralModeCount - 1);
        bool isBitcrush = (mode == 3);
        if (spectralCurveDropdown_) {
            spectralCurveDropdown_->setAlphaValue(isBitcrush ? 0.35f : 1.0f);
            spectralCurveDropdown_->setMouseEnabled(!isBitcrush);
        }
        if (spectralBitsGroup_) {
            spectralBitsGroup_->setAlphaValue(isBitcrush ? 1.0f : 0.35f);
            spectralBitsGroup_->setMouseEnabled(isBitcrush);
        }
    }

    // ---- XYMorphPad ----
    if (xyMorphPad_ && !modulatedMorphXPtr_) {
        xyMorphPad_->setMorphPosition(
            static_cast<float>(paramNorm(kMixerPositionId)),
            static_cast<float>(paramNorm(kMixerTiltId)));
    }

    // ---- ADSR Displays ----
    syncAdsrDisplay(ampEnvDisplay_,
        kAmpEnvAttackId, kAmpEnvAttackCurveId,
        kAmpEnvBezierEnabledId, kAmpEnvBezierAttackCp1XId);
    syncAdsrDisplay(filterEnvDisplay_,
        kFilterEnvAttackId, kFilterEnvAttackCurveId,
        kFilterEnvBezierEnabledId, kFilterEnvBezierAttackCp1XId);
    syncAdsrDisplay(modEnvDisplay_,
        kModEnvAttackId, kModEnvAttackCurveId,
        kModEnvBezierEnabledId, kModEnvBezierAttackCp1XId);

    // ---- Mod Matrix Grid + Ring Indicators ----
    if (modMatrixGrid_ && !suppressModMatrixSync_) {
        syncModMatrixGrid();
    }
    rebuildRingIndicators();

    // Sync destination knob base values to ring indicators
    for (int i = 0; i < kMaxRingIndicators; ++i) {
        if (ringIndicators_[static_cast<size_t>(i)]) {
            auto* param = getParameterObject(kVoiceDestParamIds[static_cast<size_t>(i)]);
            if (param) {
                ringIndicators_[static_cast<size_t>(i)]->setBaseValue(
                    static_cast<float>(param->getNormalized()));
            }
        }
    }

    // ---- Expanded ADSR overlay ----
    if (adsrExpandedOverlay_ && adsrExpandedOverlay_->isOpen()) {
        auto* expandedDisplay = adsrExpandedOverlay_->getDisplay();
        if (expandedDisplay) {
            if (expandedEnvType_ == EnvelopeType::kAmp) {
                syncAdsrDisplay(expandedDisplay,
                    kAmpEnvAttackId, kAmpEnvAttackCurveId,
                    kAmpEnvBezierEnabledId, kAmpEnvBezierAttackCp1XId);
            } else if (expandedEnvType_ == EnvelopeType::kFilter) {
                syncAdsrDisplay(expandedDisplay,
                    kFilterEnvAttackId, kFilterEnvAttackCurveId,
                    kFilterEnvBezierEnabledId, kFilterEnvBezierAttackCp1XId);
            } else if (expandedEnvType_ == EnvelopeType::kMod) {
                syncAdsrDisplay(expandedDisplay,
                    kModEnvAttackId, kModEnvAttackCurveId,
                    kModEnvBezierEnabledId, kModEnvBezierAttackCp1XId);
            }
        }
    }

    // ---- Single full-frame repaint instead of thousands of invalidRects ----
    if (activeEditor_ && activeEditor_->getFrame()) {
        activeEditor_->getFrame()->invalid();
    }
}

} // namespace Ruinae
