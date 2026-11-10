// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"
#include "state_codec.h"

#include "dsp/shape_slot_descriptors.h"
#include "dsp/sweep_morph_link.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include <algorithm>  // for std::max, std::min
#include <array>
#include <cmath>      // for std::log10, std::pow
#include <cstddef>
#include <cstring>    // for memcpy
#include <random>     // for instance ID generation

namespace Disrumpo {

// ==============================================================================
// Processor parameter handling (shape-slot mapping + processParameterChanges)
// ==============================================================================

// ==============================================================================
// Shape Slot → DistortionParams Mapping
// ==============================================================================
// Maps normalized [0,1] shape slot values to DistortionParams fields through the
// per-type descriptor table in dsp/shape_slot_descriptors.h, which the
// controller's slot formatter reads as well (D-090).
// ==============================================================================

static_assert(MorphNode::kShapeSlotCount == kShapeSlotDescriptorCount);

void Processor::mapShapeSlotsToParams(DistortionType type, const float* slots,
                                      DistortionParams& p) noexcept {
    applyShapeSlots(type, slots, p);
}

void Processor::processParameterChanges(Steinberg::Vst::IParameterChanges* changes) {
    if (!changes) {
        return;
    }

    const Steinberg::int32 numParamsChanged = changes->getParameterCount();

    // Bands whose morph nodes changed in this batch: pushed once each after the
    // loop, not once per parameter (D-006)
    std::array<bool, kMaxBands> morphNodesDirty{};

    for (Steinberg::int32 i = 0; i < numParamsChanged; ++i) {
        Steinberg::Vst::IParamValueQueue* paramQueue = changes->getParameterData(i);
        if (!paramQueue) {
            continue;
        }

        const Steinberg::Vst::ParamID paramId = paramQueue->getParameterId();
        const Steinberg::int32 numPoints = paramQueue->getPointCount();

        // Get the last value (most recent)
        Steinberg::int32 sampleOffset = 0;
        Steinberg::Vst::ParamValue value = 0.0;

        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value)
            != Steinberg::kResultTrue) {
            continue;
        }

        // =======================================================================
        // Route parameter changes by ID
        // Constitution Principle V: Values are normalized 0.0 to 1.0
        // =======================================================================

        switch (paramId) {
            case kInputGainId:
                inputGain_.store(static_cast<float>(value), std::memory_order_relaxed);
                break;

            case kOutputGainId:
                outputGain_.store(static_cast<float>(value), std::memory_order_relaxed);
                break;

            case kGlobalMixId:
                globalMix_.store(static_cast<float>(value), std::memory_order_relaxed);
                break;

            case kBandCountId: {
                // Convert normalized [0,1] to band count [1,4]
                const int newBandCount = 1 + static_cast<int>(value * 3.0 + 0.5);
                const int clamped = std::clamp(newBandCount, kMinBands, 4);
                // process() switches the crossovers through the output dip (D-023)
                bandCount_.store(clamped, std::memory_order_relaxed);
                break;
            }

            case kOversampleMaxId: {
                // FR-005, FR-006: Map normalized [0,1] to {1, 2, 4, 4}
                // StringListParameter with 4 items: index = round(value * 3)
                // Index 0 = 1x, Index 1 = 2x, Index 2 = 4x, Index 3 = "8x"
                // The "8x" entry stays registered so list indices and saved values keep
                // their meaning, but it maps to 4x: 4x is the highest factor (D-009, D-034)
                static constexpr int kOversampleFactors[] = {1, 2, 4, 4};
                const int index = std::clamp(
                    static_cast<int>(value * 3.0 + 0.5), 0, 3);
                const int factor = kOversampleFactors[index];
                // FR-016: Apply to all band processors
                for (auto& bp : bandProcessors_) {
                    bp.setMaxOversampleFactor(factor);
                }
                break;
            }

            default:
                // =================================================================
                // Sweep Parameters (spec 007-sweep-system)
                // FR-002 to FR-005: Sweep frequency, width, intensity, falloff
                // =================================================================
                if (isSweepParamId(paramId)) {
                    const SweepParamType sweepType = static_cast<SweepParamType>(paramId & 0xFF); // NOLINT(modernize-use-auto) explicit type for readability
                    switch (sweepType) {
                        case SweepParamType::kSweepEnable:
                            // FR-011: Enable/disable sweep
                            sweepProcessor_.setEnabled(value >= 0.5);
                            break;

                        case SweepParamType::kSweepFrequency: {
                            // FR-002: Convert normalized [0,1] to Hz [20, 20000] logarithmically
                            // (log2 scale as per data-model.md; state_codec.h)
                            const float freqHz = sweepFreqNormToHz(static_cast<float>(value));
                            // Store base frequency for modulation (FR-029a)
                            baseSweepFrequency_.store(freqHz, std::memory_order_relaxed);
                            sweepProcessor_.setCenterFrequency(freqHz);
                            break;
                        }

                        case SweepParamType::kSweepWidth: {
                            // FR-003: Convert normalized [0,1] to octaves [0.5, 4.0]
                            baseSweepWidthNorm_.store(static_cast<float>(value), std::memory_order_relaxed);
                            const float widthOctaves = kSweepWidthOctRange.toPlain(static_cast<float>(value));
                            sweepProcessor_.setWidth(widthOctaves);
                            break;
                        }

                        case SweepParamType::kSweepIntensity: {
                            // FR-004: Convert normalized [0,1] to intensity [0, 2] (0-200%)
                            baseSweepIntensityNorm_.store(static_cast<float>(value), std::memory_order_relaxed);
                            const float intensity = kSweepIntensityRange.toPlain(static_cast<float>(value));
                            sweepProcessor_.setIntensity(intensity);
                            break;
                        }

                        case SweepParamType::kSweepMorphLink: {
                            // FR-014: Sweep-morph link mode (alias for bands whose X link is None)
                            sweepProcessor_.setMorphLinkMode(decodeMorphLinkMode(value));
                            break;
                        }

                        case SweepParamType::kSweepFalloff:
                            // FR-005: Falloff mode (0 = Sharp, 1 = Smooth)
                            sweepProcessor_.setFalloffMode(value >= 0.5f ? SweepFalloff::Smooth : SweepFalloff::Sharp);
                            break;

                        // ========================================================
                        // Sweep LFO Parameters (FR-024, FR-025)
                        // ========================================================
                        case SweepParamType::kSweepLFOEnable:
                            sweepLFO_.setEnabled(value >= 0.5);
                            break;

                        case SweepParamType::kSweepLFORate: {
                            // Convert normalized [0,1] to Hz [0.01, 20] logarithmically
                            sweepLFO_.setRate(lfoRateNormToHz(static_cast<float>(value)));
                            break;
                        }

                        case SweepParamType::kSweepLFOWaveform: {
                            // Convert normalized [0,1] to waveform index [0,5]
                            const int waveformIndex = normToListIndex(value, kLfoShapeCount);
                            sweepLFO_.setWaveform(static_cast<Krate::DSP::Waveform>(waveformIndex));
                            break;
                        }

                        case SweepParamType::kSweepLFODepth:
                            // Depth is already normalized [0,1]
                            sweepLFO_.setDepth(static_cast<float>(value));
                            break;

                        case SweepParamType::kSweepLFOSync:
                            sweepLFO_.setTempoSync(value >= 0.5);
                            break;

                        case SweepParamType::kSweepLFONoteValue: {
                            // Convert normalized [0,1] to note value index [0, kNoteValueCount - 1] = [0, 29]
                            // (the shared note-value dropdown, Krate::DSP::getNoteValueFromDropdown)
                            const int noteIndex = normToListIndex(value, kNoteValueCount);
                            const auto mapping = Krate::DSP::getNoteValueFromDropdown(noteIndex);
                            sweepLFO_.setNoteValue(mapping.note, mapping.modifier);
                            break;
                        }

                        // ========================================================
                        // Sweep Envelope Parameters (FR-026, FR-027)
                        // ========================================================
                        case SweepParamType::kSweepEnvEnable:
                            sweepEnvelope_.setEnabled(value >= 0.5);
                            break;

                        case SweepParamType::kSweepEnvAttack: {
                            // Convert normalized [0,1] to ms [1, 100]
                            const float attackMs = kMinSweepEnvAttackMs +
                                static_cast<float>(value) * (kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs);
                            sweepEnvelope_.setAttackTime(attackMs);
                            break;
                        }

                        case SweepParamType::kSweepEnvRelease: {
                            // Convert normalized [0,1] to ms [10, 500]
                            const float releaseMs = kMinSweepEnvReleaseMs +
                                static_cast<float>(value) * (kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs);
                            sweepEnvelope_.setReleaseTime(releaseMs);
                            break;
                        }

                        case SweepParamType::kSweepEnvSensitivity:
                            // Sensitivity is already normalized [0,1]
                            sweepEnvelope_.setSensitivity(static_cast<float>(value));
                            break;

                        // ========================================================
                        // Custom Curve Parameters (FR-039a, FR-039b, FR-039c)
                        // ========================================================
                        case SweepParamType::kSweepCustomCurvePointCount: {
                            // Rebuild curve when point count changes
                            int pointCount = static_cast<int>(2.0f + static_cast<float>(value) * 6.0f + 0.5f);
                            pointCount = std::clamp(pointCount, 2, 8);
                            // Curve will be rebuilt next time a point param changes
                            (void)pointCount;
                            break;
                        }

                        case SweepParamType::kSweepCustomCurveP0X:
                        case SweepParamType::kSweepCustomCurveP0Y:
                        case SweepParamType::kSweepCustomCurveP1X:
                        case SweepParamType::kSweepCustomCurveP1Y:
                        case SweepParamType::kSweepCustomCurveP2X:
                        case SweepParamType::kSweepCustomCurveP2Y:
                        case SweepParamType::kSweepCustomCurveP3X:
                        case SweepParamType::kSweepCustomCurveP3Y:
                        case SweepParamType::kSweepCustomCurveP4X:
                        case SweepParamType::kSweepCustomCurveP4Y:
                        case SweepParamType::kSweepCustomCurveP5X:
                        case SweepParamType::kSweepCustomCurveP5Y:
                        case SweepParamType::kSweepCustomCurveP6X:
                        case SweepParamType::kSweepCustomCurveP6Y:
                        case SweepParamType::kSweepCustomCurveP7X:
                        case SweepParamType::kSweepCustomCurveP7Y:
                            // Curve point changed - defer rebuild to process loop
                            // (handled below after all params processed)
                        default:
                            break;
                    }
                    break;  // Exit the default case after handling sweep params
                }
                // =================================================================
                // Modulation Parameters (spec 008-modulation-system)
                // =================================================================
                if (isModulationParamId(paramId)) {
                    if (isRoutingParamId(paramId)) {
                        // Routing parameters handled separately
                        const uint8_t routIdx = extractRoutingIndex(paramId);
                        const uint8_t routOff = extractRoutingOffset(paramId);
                        if (routIdx < Krate::DSP::kMaxModRoutings) {
                            auto routing = modulationEngine_.getRouting(routIdx);
                            switch (routOff) {
                                case 0:  // Source
                                    routing.source = static_cast<Krate::DSP::ModSource>(
                                        normToListIndex(value, kUIModSourceCount));
                                    routing.active = (routing.source != Krate::DSP::ModSource::None);
                                    break;
                                case 1:  // Destination
                                    routing.destParamId = static_cast<uint32_t>(
                                        value * static_cast<double>(ModDest::kTotalDestinations - 1) + 0.5);
                                    break;
                                case 2:  // Amount [-1, +1]
                                    routing.amount = static_cast<float>(value * 2.0 - 1.0);
                                    break;
                                case 3:  // Curve
                                    routing.curve = static_cast<Krate::DSP::ModCurve>(
                                        static_cast<int>(value * 3.0 + 0.5));
                                    break;
                                default:
                                    break;
                            }
                            modulationEngine_.setRouting(routIdx, routing);
                        }
                    } else {
                        const auto modType = static_cast<ModParamType>(paramId & 0xFF);
                        switch (modType) {
                            // LFO 1
                            case ModParamType::kLFO1Rate:
                                modulationEngine_.setLFO1Rate(lfoRateNormToHz(static_cast<float>(value)));
                                break;
                            case ModParamType::kLFO1Shape: {
                                int idx = normToListIndex(value, kLfoShapeCount);
                                modulationEngine_.setLFO1Waveform(static_cast<Krate::DSP::Waveform>(idx));
                                break;
                            }
                            case ModParamType::kLFO1Phase:
                                modulationEngine_.setLFO1PhaseOffset(kLfoPhaseDegRange.toPlain(static_cast<float>(value)));
                                break;
                            case ModParamType::kLFO1Sync:
                                modulationEngine_.setLFO1TempoSync(value >= 0.5);
                                break;
                            case ModParamType::kLFO1NoteValue: {
                                const int idx = normToListIndex(value, kNoteValueCount);
                                const auto mapping = Krate::DSP::getNoteValueFromDropdown(idx);
                                modulationEngine_.setLFO1NoteValue(mapping.note, mapping.modifier);
                                break;
                            }
                            case ModParamType::kLFO1Unipolar:
                                modulationEngine_.setLFO1Unipolar(value >= 0.5);
                                break;
                            case ModParamType::kLFO1Retrigger:
                                modulationEngine_.setLFO1Retrigger(value >= 0.5);
                                break;

                            // LFO 2
                            case ModParamType::kLFO2Rate:
                                modulationEngine_.setLFO2Rate(lfoRateNormToHz(static_cast<float>(value)));
                                break;
                            case ModParamType::kLFO2Shape: {
                                int idx = normToListIndex(value, kLfoShapeCount);
                                modulationEngine_.setLFO2Waveform(static_cast<Krate::DSP::Waveform>(idx));
                                break;
                            }
                            case ModParamType::kLFO2Phase:
                                modulationEngine_.setLFO2PhaseOffset(kLfoPhaseDegRange.toPlain(static_cast<float>(value)));
                                break;
                            case ModParamType::kLFO2Sync:
                                modulationEngine_.setLFO2TempoSync(value >= 0.5);
                                break;
                            case ModParamType::kLFO2NoteValue: {
                                const int idx = normToListIndex(value, kNoteValueCount);
                                const auto mapping = Krate::DSP::getNoteValueFromDropdown(idx);
                                modulationEngine_.setLFO2NoteValue(mapping.note, mapping.modifier);
                                break;
                            }
                            case ModParamType::kLFO2Unipolar:
                                modulationEngine_.setLFO2Unipolar(value >= 0.5);
                                break;
                            case ModParamType::kLFO2Retrigger:
                                modulationEngine_.setLFO2Retrigger(value >= 0.5);
                                break;

                            // Envelope Follower
                            case ModParamType::kEnvFollowerAttack: {
                                float ms = kEnvFollowerAttackMsRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setEnvFollowerAttack(ms);
                                break;
                            }
                            case ModParamType::kEnvFollowerRelease: {
                                float ms = kEnvFollowerReleaseMsRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setEnvFollowerRelease(ms);
                                break;
                            }
                            case ModParamType::kEnvFollowerSensitivity:
                                modulationEngine_.setEnvFollowerSensitivity(static_cast<float>(value));
                                break;
                            case ModParamType::kEnvFollowerSource: {
                                int idx = normToListIndex(value, kEnvFollowerSourceCount);
                                modulationEngine_.setEnvFollowerSource(
                                    static_cast<Krate::DSP::EnvFollowerSourceType>(idx));
                                break;
                            }

                            // Random
                            case ModParamType::kRandomRate: {
                                float hz = kRandomRateHzRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setRandomRate(hz);
                                break;
                            }
                            case ModParamType::kRandomSmoothness:
                                modulationEngine_.setRandomSmoothness(static_cast<float>(value));
                                break;
                            case ModParamType::kRandomSync:
                                modulationEngine_.setRandomTempoSync(value >= 0.5);
                                break;

                            // Chaos
                            case ModParamType::kChaosModel: {
                                int idx = normToListIndex(value, kChaosModelCount);
                                modulationEngine_.setChaosModel(
                                    static_cast<Krate::DSP::ChaosModel>(idx));
                                break;
                            }
                            case ModParamType::kChaosSpeed: {
                                float speed = kChaosSpeedRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setChaosSpeed(speed);
                                break;
                            }
                            case ModParamType::kChaosCoupling:
                                modulationEngine_.setChaosCoupling(static_cast<float>(value));
                                break;

                            // Sample & Hold
                            case ModParamType::kSampleHoldSource: {
                                int idx = normToListIndex(value, kSampleHoldSourceCount);
                                modulationEngine_.setSampleHoldSource(
                                    static_cast<Krate::DSP::SampleHoldInputType>(idx));
                                break;
                            }
                            case ModParamType::kSampleHoldRate: {
                                float hz = kSampleHoldRateHzRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setSampleHoldRate(hz);
                                break;
                            }
                            case ModParamType::kSampleHoldSlew: {
                                float ms = kSampleHoldSlewMsRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setSampleHoldSlew(ms);
                                break;
                            }

                            // Pitch Follower
                            case ModParamType::kPitchFollowerMinHz: {
                                float hz = kPitchFollowerMinHzRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setPitchFollowerMinHz(hz);
                                break;
                            }
                            case ModParamType::kPitchFollowerMaxHz: {
                                float hz = kPitchFollowerMaxHzRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setPitchFollowerMaxHz(hz);
                                break;
                            }
                            case ModParamType::kPitchFollowerConfidence:
                                modulationEngine_.setPitchFollowerConfidence(static_cast<float>(value));
                                break;
                            case ModParamType::kPitchFollowerTrackingSpeed: {
                                float ms = kPitchFollowerTrackingMsRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setPitchFollowerTrackingSpeed(ms);
                                break;
                            }

                            // Transient Detector
                            case ModParamType::kTransientSensitivity:
                                modulationEngine_.setTransientSensitivity(static_cast<float>(value));
                                break;
                            case ModParamType::kTransientAttack: {
                                float ms = kTransientAttackMsRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setTransientAttack(ms);
                                break;
                            }
                            case ModParamType::kTransientDecay: {
                                float ms = kTransientDecayMsRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setTransientDecay(ms);
                                break;
                            }

                            // Rungler
                            case ModParamType::kRunglerRate: {
                                float hz = kRunglerRateHzRange.toPlain(static_cast<float>(value));
                                modulationEngine_.setRunglerOsc1Freq(hz);
                                modulationEngine_.setRunglerOsc2Freq(hz * 1.5f);
                                break;
                            }
                            case ModParamType::kRunglerDepth:
                                modulationEngine_.setRunglerDepth(static_cast<float>(value));
                                break;
                            case ModParamType::kRunglerBits: {
                                const size_t bits = 4 + static_cast<size_t>(normToListIndex(value, kRunglerBitsCount));
                                modulationEngine_.setRunglerBits(bits);
                                break;
                            }
                            case ModParamType::kRunglerLoop:
                                modulationEngine_.setRunglerLoopMode(value >= 0.5);
                                break;

                            // Macros
                            case ModParamType::kMacro1Value:
                                modulationEngine_.setMacroValue(0, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro1Min:
                                modulationEngine_.setMacroMin(0, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro1Max:
                                modulationEngine_.setMacroMax(0, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro1Curve: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setMacroCurve(0, static_cast<Krate::DSP::ModCurve>(idx));
                                break;
                            }
                            case ModParamType::kMacro2Value:
                                modulationEngine_.setMacroValue(1, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro2Min:
                                modulationEngine_.setMacroMin(1, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro2Max:
                                modulationEngine_.setMacroMax(1, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro2Curve: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setMacroCurve(1, static_cast<Krate::DSP::ModCurve>(idx));
                                break;
                            }
                            case ModParamType::kMacro3Value:
                                modulationEngine_.setMacroValue(2, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro3Min:
                                modulationEngine_.setMacroMin(2, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro3Max:
                                modulationEngine_.setMacroMax(2, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro3Curve: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setMacroCurve(2, static_cast<Krate::DSP::ModCurve>(idx));
                                break;
                            }
                            case ModParamType::kMacro4Value:
                                modulationEngine_.setMacroValue(3, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro4Min:
                                modulationEngine_.setMacroMin(3, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro4Max:
                                modulationEngine_.setMacroMax(3, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro4Curve: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setMacroCurve(3, static_cast<Krate::DSP::ModCurve>(idx));
                                break;
                            }

                            default:
                                break;
                        }
                    }
                    break;  // Exit the default case after handling modulation params
                }
                // =============================================================
                // Node Parameters (per-band, per-node distortion params)
                // =============================================================
                if (isNodeParamId(paramId)) {
                    const uint8_t band = extractBandFromNodeParam(paramId);
                    const uint8_t node = extractNode(paramId);
                    const NodeParamType nodeType = extractNodeParamType(paramId);

                    if (band < kMaxBands && node < kMaxMorphNodes) {
                        auto& cache = bandMorphCache_[band];
                        auto& mn = cache.nodes[static_cast<size_t>(node)];

                        switch (nodeType) {
                            case NodeParamType::kNodeType: {
                                // StringListParameter: 26 types
                                int idx = static_cast<int>(value * 25.0 + 0.5);
                                auto newType = static_cast<DistortionType>(std::clamp(idx, 0, 25));
                                if (newType != mn.type) {
                                    auto& shadow = cache.shapeShadow[static_cast<size_t>(node)];
                                    // Save current slots for old type
                                    shadow.save(static_cast<int>(mn.type), mn.shapeSlots);
                                    mn.type = newType;
                                    // Restore slots for new type
                                    shadow.load(static_cast<int>(newType), mn.shapeSlots);
                                    // Re-map slots to params for the new type
                                    mapShapeSlotsToParams(mn.type, mn.shapeSlots, mn.params);
                                }
                                break;
                            }
                            case NodeParamType::kNodeDrive:
                                // RangeParameter [0, 10]
                                mn.commonParams.drive = static_cast<float>(value * 10.0);  // exact (D-160)
                                break;
                            case NodeParamType::kNodeMix:
                                // RangeParameter [0, 100]% -> [0, 1]
                                mn.commonParams.mix = static_cast<float>(value);
                                break;
                            case NodeParamType::kNodeTone:
                                // RangeParameter [200, 8000] Hz
                                mn.commonParams.toneHz = static_cast<float>(200.0 + value * 7800.0);  // exact (D-160)
                                break;
                            // Bias, Folds and Bit Depth are also shape slots of the types that
                            // use them (RingSaturation / saturators Bias, the wavefolders' Folds,
                            // Bitcrush Bits). One precedence, the slot wins (follow-ups 34 and 87,
                            // user ruling 2026-11-09): the node parameter is applied, then the
                            // type's slots again, whatever order the parameters arrive in. setState
                            // maps the slots last as well.
                            case NodeParamType::kNodeBias:
                                // RangeParameter [-1, +1]
                                mn.params.bias = static_cast<float>(value * 2.0 - 1.0);  // exact (D-160)
                                mapShapeSlotsToParams(mn.type, mn.shapeSlots, mn.params);
                                break;
                            case NodeParamType::kNodeFolds:
                                // RangeParameter [1, 12] (integer steps)
                                mn.params.folds = 1.0f + std::round(static_cast<float>(value) * 11.0f);
                                mapShapeSlotsToParams(mn.type, mn.shapeSlots, mn.params);
                                break;
                            case NodeParamType::kNodeBitDepth:
                                // RangeParameter [4, 24] (integer steps)
                                mn.params.bitDepth = 4.0f + std::round(static_cast<float>(value) * 20.0f);
                                mapShapeSlotsToParams(mn.type, mn.shapeSlots, mn.params);
                                break;
                            default: {
                                // Generic shape slots (kNodeShape0 through kNodeShape9)
                                const auto paramByte = static_cast<uint8_t>(nodeType);
                                if (paramByte >= static_cast<uint8_t>(NodeParamType::kNodeShape0) &&
                                    paramByte <= static_cast<uint8_t>(NodeParamType::kNodeShape9)) {
                                    int slotIndex = paramByte - static_cast<uint8_t>(NodeParamType::kNodeShape0);
                                    mn.shapeSlots[slotIndex] = static_cast<float>(value);
                                    // Keep shadow in sync for the current type
                                    cache.shapeShadow[static_cast<size_t>(node)]
                                        .typeSlots[static_cast<int>(mn.type)][slotIndex] =
                                        static_cast<float>(value);
                                    // Update DistortionParams from slots
                                    mapShapeSlotsToParams(mn.type, mn.shapeSlots, mn.params);
                                }
                                break;
                            }
                        }

                        // Pushed to the BandProcessor after the loop
                        morphNodesDirty[band] = true;
                    }
                    break;
                }
                // Check for band parameters
                if (isBandParamId(paramId)) {
                    const uint8_t band = extractBandIndex(paramId);
                    const BandParamType paramType = extractBandParamType(paramId);

                    if (band < kMaxBands) {
                        switch (paramType) {
                            case BandParamType::kBandGain: {
                                // Convert normalized [0,1] to dB [-24, +24]
                                // in double: the state stores dB, so a load through the parameters
                                // comes back bit-exact (D-160)
                                const auto gainDb = static_cast<float>(
                                    static_cast<double>(kMinBandGainDb) +
                                    value * static_cast<double>(kMaxBandGainDb - kMinBandGainDb));
                                bandStates_[band].gainDb = gainDb;
                                bandProcessors_[band].setGainDb(gainDb);
                                break;
                            }
                            case BandParamType::kBandPan: {
                                // Convert normalized [0,1] to pan [-1, +1]
                                const auto pan = static_cast<float>(value * 2.0 - 1.0);  // exact (D-160)
                                bandStates_[band].pan = pan;
                                bandProcessors_[band].setPan(pan);
                                break;
                            }
                            case BandParamType::kBandSolo:
                                bandStates_[band].solo = value >= 0.5;
                                break;
                            case BandParamType::kBandBypass:
                                bandStates_[band].bypass = value >= 0.5;
                                bandProcessors_[band].setBypassed(bandStates_[band].bypass);
                                break;
                            case BandParamType::kBandMute:
                                bandStates_[band].mute = value >= 0.5;
                                bandProcessors_[band].setMute(bandStates_[band].mute);
                                break;
                            case BandParamType::kBandMorphX: {
                                bandMorphCache_[band].morphX = static_cast<float>(value);
                                bandProcessors_[band].setMorphPosition(
                                    bandMorphCache_[band].morphX,
                                    bandMorphCache_[band].morphY);
                                break;
                            }
                            case BandParamType::kBandMorphY: {
                                bandMorphCache_[band].morphY = static_cast<float>(value);
                                bandProcessors_[band].setMorphPosition(
                                    bandMorphCache_[band].morphX,
                                    bandMorphCache_[band].morphY);
                                break;
                            }
                            case BandParamType::kBandActiveNodes: {
                                // StringListParameter: 4 entries ["1","2","3","4"]
                                int idx = normToListIndex(value, kMaxMorphNodes);
                                int count = std::clamp(idx + 1, kMinActiveNodes, kMaxMorphNodes);
                                bandMorphCache_[band].activeNodeCount = count;
                                morphNodesDirty[band] = true;
                                break;
                            }
                            case BandParamType::kBandMorphSmoothing: {
                                // RangeParameter [0, 500] ms
                                float timeMs = kMorphSmoothingMsRange.toPlainExact(value);  // stored in ms (D-160)
                                bandMorphCache_[band].morphSmoothingMs = timeMs;
                                bandProcessors_[band].setMorphSmoothingTime(timeMs);
                                break;
                            }
                            case BandParamType::kBandMorphMode: {
                                // StringListParameter: 3 entries
                                int idx = normToListIndex(value, kMorphModeCount);
                                const auto mode = static_cast<MorphMode>(std::clamp(idx, 0, 2));
                                bandMorphCache_[band].morphMode = mode;
                                bandProcessors_[band].setMorphMode(mode);
                                break;
                            }
                            case BandParamType::kBandMorphXLink:
                                // Applied in process() from the smoothed sweep centre (D-137)
                                bandStates_[band].xLink = decodeMorphLinkMode(value);
                                break;
                            case BandParamType::kBandMorphYLink:
                                bandStates_[band].yLink = decodeMorphLinkMode(value);
                                break;
                            case BandParamType::kBandExpanded:
                            case BandParamType::kBandSelectedNode:
                            case BandParamType::kBandDisplayedType:
                            default:
                                // UI-only params (expanded, selectedNode,
                                // displayedType): no processor action
                                break;
                        }
                    }
                }
                // Check for crossover frequency parameters
                else if (isCrossoverParamId(paramId)) {
                    const uint8_t index = extractCrossoverIndex(paramId);
                    if (index < kMaxBands - 1) {
                        // Convert normalized [0,1] to Hz [20, 20000] logarithmically
                        const float freqHz = crossoverNormToHzExact(value);  // the state stores Hz (D-160)
                        crossoverL_.setCrossoverFrequency(static_cast<int>(index), freqHz);
                        crossoverR_.setCrossoverFrequency(static_cast<int>(index), freqHz);
                    }
                }
                break;
        }
    }

    for (std::size_t band = 0; band < morphNodesDirty.size(); ++band) {
        if (morphNodesDirty[band]) {
            const auto& cache = bandMorphCache_[band];
            bandProcessors_[band].setMorphNodes(cache.nodes, cache.activeNodeCount);
        }
    }
}
} // namespace Disrumpo
