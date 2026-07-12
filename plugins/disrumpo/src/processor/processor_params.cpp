// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "dsp/sweep_morph_link.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include <algorithm>  // for std::max, std::min
#include <cmath>      // for std::log10, std::pow
#include <cstring>    // for memcpy
#include <random>     // for instance ID generation

namespace Disrumpo {

// ==============================================================================
// Processor parameter handling (shape-slot mapping + processParameterChanges)
// ==============================================================================

// ==============================================================================
// Shape Slot → DistortionParams Mapping
// ==============================================================================
// Maps normalized [0,1] shape slot values to denormalized DistortionParams
// fields based on the active distortion type. Each type's UI controls are
// assigned sequential slots (see plan mapping table).
// ==============================================================================

static void mapShapeSlotsToParams(DistortionType type, const float* slots,
                                   DistortionParams& p) {
    switch (type) {
        case DistortionType::SoftClip:
            // Slot0=Curve, Slot1=Knee
            p.curve = slots[0];
            p.knee = slots[1];
            break;

        case DistortionType::HardClip:
            // Slot0=Threshold, Slot1=Ceiling
            p.threshold = slots[0];
            p.ceiling = slots[1];
            break;

        case DistortionType::Tube:
            // Slot0=Bias, Slot1=Sag, Slot2=Stage
            p.bias = slots[0] * 2.0f - 1.0f;        // [0,1] → [-1,1]
            p.sag = slots[1];
            p.satStage = static_cast<int>(slots[2] * 3.0f + 0.5f);
            break;

        case DistortionType::Tape:
            // Slot0=Bias, Slot1=Sag, Slot2=Speed, Slot3=Model, Slot4=HFRoll, Slot5=Flutter
            p.bias = slots[0] * 2.0f - 1.0f;
            p.sag = slots[1];
            p.speed = slots[2];
            p.tapeModel = static_cast<int>(slots[3] * 1.0f + 0.5f); // 0-1 (Simple/Hysteresis)
            p.hfRoll = slots[4];
            p.flutter = slots[5];
            break;

        case DistortionType::Fuzz:
            // Slot0=Bias, Slot1=Transistor, Slot2=Octave, Slot3=Sustain
            p.bias = slots[0];  // Fuzz bias is unipolar [0,1]
            p.transistor = static_cast<int>(slots[1] * 1.0f + 0.5f); // 0-1
            p.octave = slots[2];
            p.sustain = slots[3];
            break;

        case DistortionType::AsymmetricFuzz:
            // Slot0=Bias, Slot1=Asym, Slot2=Trans, Slot3=Sustain, Slot4=Body
            p.bias = slots[0];  // Fuzz bias is unipolar [0,1]
            p.asymmetry = slots[1];
            p.transistor = static_cast<int>(slots[2] * 1.0f + 0.5f);
            p.sustain = slots[3];
            p.body = slots[4];
            break;

        case DistortionType::SineFold:
            // Slot0=Folds, Slot1=Symmetry, Slot2=Shape, Slot3=Bias, Slot4=Smooth
            p.folds = 1.0f + slots[0] * 11.0f;       // [0,1] → [1,12]
            p.symmetry = slots[1] * 2.0f - 1.0f;     // [0,1] → [-1,1]
            p.shape = slots[2];
            p.bias = slots[3] * 2.0f - 1.0f;
            p.smoothness = slots[4];
            break;

        case DistortionType::TriangleFold:
            // Slot0=Folds, Slot1=Symmetry, Slot2=Angle, Slot3=Bias, Slot4=Smooth
            p.folds = 1.0f + slots[0] * 11.0f;
            p.symmetry = slots[1] * 2.0f - 1.0f;
            p.angle = slots[2];
            p.bias = slots[3] * 2.0f - 1.0f;
            p.smoothness = slots[4];
            break;

        case DistortionType::SergeFold:
            // Slot0=Folds, Slot1=Symm, Slot2=Model, Slot3=Bias, Slot4=Shape, Slot5=Smooth
            p.folds = 1.0f + slots[0] * 11.0f;
            p.symmetry = slots[1] * 2.0f - 1.0f;
            p.foldModel = static_cast<int>(slots[2] * 3.0f + 0.5f); // 0-3 models
            p.bias = slots[3] * 2.0f - 1.0f;
            p.shape = slots[4];
            p.smoothness = slots[5];
            break;

        case DistortionType::FullRectify:
            // Slot0=Smooth, Slot1=DCBlock
            p.smoothness = slots[0];
            p.dcBlock = slots[1] >= 0.5f;
            break;

        case DistortionType::HalfRectify:
            // Slot0=Threshold, Slot1=Smooth, Slot2=DCBlock
            p.threshold = slots[0];
            p.smoothness = slots[1];
            p.dcBlock = slots[2] >= 0.5f;
            break;

        case DistortionType::Bitcrush:
            // Slot0=Bits, Slot1=Dither, Slot2=Mode, Slot3=Jitter
            p.bitDepth = 1.0f + slots[0] * 15.0f;     // [0,1] → [1,16]
            p.dither = slots[1];
            p.bitcrushMode = static_cast<int>(slots[2] * 1.0f + 0.5f);
            p.jitter = slots[3];
            break;

        case DistortionType::SampleReduce:
            // Slot0=Rate, Slot1=Jitter, Slot2=Mode, Slot3=Smooth
            p.sampleRateRatio = 1.0f + slots[0] * 31.0f; // [0,1] → [1,32]
            p.jitter = slots[1];
            p.sampleMode = static_cast<int>(slots[2] * 1.0f + 0.5f);
            p.smoothness = slots[3];
            break;

        case DistortionType::Quantize:
            // Slot0=Levels, Slot1=Dither, Slot2=Smooth, Slot3=Offset
            p.quantLevels = slots[0];
            p.dither = slots[1];
            p.smoothness = slots[2];
            p.quantOffset = slots[3];
            break;

        case DistortionType::Temporal:
            // Slot0=Mode, Slot1=Sens, Slot2=Curve, Slot3=Atk, Slot4=Rel, Slot5=Depth,
            // Slot6=(unused), Slot7=Hold
            p.dynamicMode = static_cast<int>(slots[0] * 3.0f + 0.5f); // 0-3 modes
            p.sensitivity = slots[1];
            p.dynamicCurve = slots[2];
            p.attackMs = 1.0f + slots[3] * 499.0f;    // [0,1] → [1,500]
            p.releaseMs = 10.0f + slots[4] * 4990.0f; // [0,1] → [10,5000]
            p.dynamicDepth = slots[5];
            p.hold = slots[7];
            break;

        case DistortionType::RingSaturation:
            // Slot0=Mod, Slot1=Stages, Slot2=Curve, Slot3=Carrier, Slot4=Bias,
            // Slot5=Freq mode, Slot6=Hz/Ratio
            p.modDepth = slots[0];
            p.stages = 1 + static_cast<int>(slots[1] * 3.0f + 0.5f); // [0,1] → 1-4
            p.rsCurve = slots[2];
            p.carrierType = static_cast<int>(slots[3] * 3.0f + 0.5f);
            p.bias = slots[4] * 2.0f - 1.0f;
            p.rsFreqSelect = static_cast<int>(slots[5] * 3.0f + 0.5f);
            p.rsCarrierFreq = slots[6];
            break;

        case DistortionType::FeedbackDist:
            // Slot0=FB, Slot1=Delay, Slot2=Curve, Slot3=Filter, Slot4=Freq,
            // Slot5=Stage, Slot6=Lim, Slot7=Thr, Slot8=Decay
            p.feedback = slots[0] * 1.5f;              // [0,1] → [0,1.5]
            p.delayMs = 1.0f + slots[1] * 99.0f;      // [0,1] → [1,100]
            p.fbCurve = slots[2];
            p.filterType = static_cast<int>(slots[3] * 3.0f + 0.5f);
            p.filterFreq = slots[4];
            p.stages = 1 + static_cast<int>(slots[5] * 3.0f + 0.5f);
            p.limiter = slots[6] >= 0.5f;
            p.limThreshold = slots[7];
            p.fbDecay = slots[8];                      // [0,1] direct
            break;

        case DistortionType::Aliasing:
            // Slot0=Down, Slot1=Shift
            p.sampleRateRatio = 2.0f + slots[0] * 30.0f; // [0,1] → [2,32]
            p.freqShift = (slots[1] * 2.0f - 1.0f) * 5000.0f; // [0,1] → [-5000,5000]
            break;

        case DistortionType::BitwiseMangler:
            // Slot0=Op, Slot1=Intensity, Slot2=Pattern
            p.bitwiseOp = static_cast<int>(slots[0] * 3.0f + 0.5f); // 0-3 operations
            p.bitwiseIntensity = slots[1];
            p.bitwisePattern = slots[2];
            break;

        case DistortionType::Chaos:
            // Slot0=Attr, Slot1=Spd, Slot2=Amt, Slot3=Coup, Slot4=XDr, Slot5=YDr, Slot6=Smth
            p.chaosAttractor = static_cast<int>(slots[0] * 3.0f + 0.5f); // 0-3
            p.attractorSpeed = 0.01f + slots[1] * 99.99f; // [0,1] → [0.01,100]
            p.chaosAmount = slots[2];
            p.chaosCoupling = slots[3];
            p.chaosXDrive = slots[4];
            p.chaosYDrive = slots[5];
            p.smoothness = slots[6];
            break;

        case DistortionType::Formant:
            // Slot0=Vowel, Slot1=Shift, Slot2=Curve, Slot3=Reso, Slot4=BW,
            // Slot5=Fmts, Slot6=Gendr, Slot7=Blend
            p.vowelSelect = static_cast<int>(slots[0] * 4.0f + 0.5f); // 0-4 vowels
            p.formantShift = (slots[1] * 2.0f - 1.0f) * 24.0f; // [0,1] → [-24,24]
            p.formantCurve = slots[2];
            p.formantReso = slots[3];
            p.formantBW = slots[4];
            p.formantCount = static_cast<int>(slots[5] * 3.0f + 0.5f);
            p.formantGender = slots[6];
            p.formantBlend = slots[7];
            break;

        case DistortionType::Granular:
            // Slot0=Size, Slot1=Dens, Slot2=PVar, Slot3=DVar, Slot4=Pos,
            // Slot5=Curve, Slot6=Env
            p.grainSizeMs = 5.0f + slots[0] * 95.0f;  // [0,1] → [5,100]
            p.grainDensity = slots[1];
            p.grainPVar = slots[2];
            p.grainDVar = slots[3];
            p.grainPos = slots[4];
            p.grainCurve = slots[5];
            p.grainEnvType = static_cast<int>(slots[6] * 3.0f + 0.5f);
            break;

        case DistortionType::Spectral:
            // Slot0=Mode, Slot1=FFT, Slot2=Curve, Slot3=Tilt, Slot4=Thr,
            // Slot5=Mag, Slot6=Freq, Slot7=Phase
            p.spectralMode = static_cast<int>(slots[0] * 3.0f + 0.5f); // 0-3 modes
            p.fftSize = 512 * (1 << static_cast<int>(slots[1] * 3.0f + 0.5f)); // 512-4096
            p.spectralCurve = slots[2];
            p.spectralTilt = slots[3];
            p.spectralThreshold = slots[4];
            p.spectralMagMode = static_cast<int>(slots[5] * 3.0f + 0.5f);
            p.spectralFreq = slots[6];
            p.spectralPhase = static_cast<int>(slots[7] * 3.0f + 0.5f);
            break;

        case DistortionType::Fractal:
            // Slot0=Mode, Slot1=Iter, Slot2=Scale, Slot3=Curve, Slot4=FDecay,
            // Slot5=FB, Slot6=Blend, Slot7=Depth
            p.fractalMode = static_cast<int>(slots[0] * 4.0f + 0.5f); // 0-4 modes
            p.iterations = 1 + static_cast<int>(slots[1] * 7.0f + 0.5f); // [0,1] → 1-8
            p.scaleFactor = 0.3f + slots[2] * 0.6f;    // [0,1] → [0.3,0.9]
            p.fractalCurve = slots[3];
            p.frequencyDecay = slots[4];
            p.fractalFB = slots[5] * 0.5f;              // [0,1] → [0,0.5]
            p.fractalBlend = static_cast<int>(slots[6] * 3.0f + 0.5f);
            p.fractalDepth = slots[7];
            break;

        case DistortionType::Stochastic:
            // Slot0=Curve, Slot1=Jit, Slot2=Rate, Slot3=Coef, Slot4=Drift,
            // Slot5=Corr, Slot6=Smth, Slot7=Dist (noise color)
            p.stochasticCurve = static_cast<int>(slots[0] * 5.0f + 0.5f);
            p.jitterAmount = slots[1];
            p.jitterRate = 0.1f + slots[2] * 99.9f;   // [0,1] → [0.1,100]
            p.coefficientNoise = slots[3];
            p.stochasticDrift = slots[4];
            p.stochasticCorr = static_cast<int>(slots[5] * 3.0f + 0.5f);
            p.stochasticSmooth = slots[6];
            p.stochasticNoiseColor = static_cast<int>(slots[7] * 4.0f + 0.5f);
            break;

        case DistortionType::AllpassResonant:
            // Slot0=Topo, Slot1=Freq, Slot2=FB, Slot3=Decay, Slot4=Drive,
            // Slot5=Curve, Slot7=Damp (Slot6 unused)
            p.allpassTopo = static_cast<int>(slots[0] * 3.0f + 0.5f); // 0-3 topologies
            p.resonantFreq = 20.0f + slots[1] * 1980.0f; // [0,1] → [20,2000]
            p.allpassFeedback = slots[2] * 0.99f;       // [0,1] → [0,0.99]
            p.decayTimeS = 0.01f + slots[3] * 9.99f;    // [0,1] → [0.01,10]
            p.allpassDrive = slots[4];                   // [0,1] → drive amount
            p.allpassSatType = static_cast<int>(slots[5] * 3.0f + 0.5f); // 0-3 curve types
            p.allpassDamp = slots[7];                    // [0,1] → damping amount
            break;

        default:
            break;
    }
}

void Processor::processParameterChanges(Steinberg::Vst::IParameterChanges* changes) {
    if (!changes) {
        return;
    }

    const Steinberg::int32 numParamsChanged = changes->getParameterCount();

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
                bandCount_.store(clamped, std::memory_order_relaxed);
                crossoverL_.setBandCount(clamped);
                crossoverR_.setBandCount(clamped);
                break;
            }

            case kOversampleMaxId: {
                // FR-005, FR-006: Map normalized [0,1] to {1, 2, 4, 8}
                // StringListParameter with 4 items: index = round(value * 3)
                // Index 0 = 1x, Index 1 = 2x, Index 2 = 4x, Index 3 = 8x
                static constexpr int kOversampleFactors[] = {1, 2, 4, 8};
                const int index = std::clamp(
                    static_cast<int>(value * 3.0 + 0.5), 0, 3);
                const int factor = kOversampleFactors[index];
                maxOversampleFactor_.store(factor, std::memory_order_relaxed);
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
                            // Using log2 scale as per data-model.md
                            constexpr float kSweepLog2Min = 4.321928f;   // log2(20)
                            constexpr float kSweepLog2Max = 14.287712f;  // log2(20000)
                            constexpr float kSweepLog2Range = kSweepLog2Max - kSweepLog2Min;
                            const float log2Freq = kSweepLog2Min + static_cast<float>(value) * kSweepLog2Range;
                            const float freqHz = std::pow(2.0f, log2Freq);
                            // Store base frequency for modulation (FR-029a)
                            baseSweepFrequency_.store(freqHz, std::memory_order_relaxed);
                            sweepProcessor_.setCenterFrequency(freqHz);
                            break;
                        }

                        case SweepParamType::kSweepWidth: {
                            // FR-003: Convert normalized [0,1] to octaves [0.5, 4.0]
                            constexpr float kMinWidth = 0.5f;
                            constexpr float kMaxWidth = 4.0f;
                            baseSweepWidthNorm_.store(static_cast<float>(value), std::memory_order_relaxed);
                            const float widthOctaves = kMinWidth + static_cast<float>(value) * (kMaxWidth - kMinWidth);
                            sweepProcessor_.setWidth(widthOctaves);
                            break;
                        }

                        case SweepParamType::kSweepIntensity: {
                            // FR-004: Convert normalized [0,1] to intensity [0, 2] (0-200%)
                            baseSweepIntensityNorm_.store(static_cast<float>(value), std::memory_order_relaxed);
                            const float intensity = static_cast<float>(value) * 2.0f;
                            sweepProcessor_.setIntensity(intensity);
                            break;
                        }

                        case SweepParamType::kSweepMorphLink: {
                            // FR-014: Sweep-morph link mode
                            const int modeIndex = static_cast<int>(value * static_cast<float>(kMorphLinkModeCount - 1) + 0.5f);
                            sweepProcessor_.setMorphLinkMode(static_cast<MorphLinkMode>(modeIndex));
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
                            constexpr float kMinRateLog = -4.6052f;  // ln(0.01)
                            constexpr float kMaxRateLog = 2.9957f;   // ln(20)
                            const float logRate = kMinRateLog + static_cast<float>(value) * (kMaxRateLog - kMinRateLog);
                            const float rateHz = std::exp(logRate);
                            sweepLFO_.setRate(rateHz);
                            break;
                        }

                        case SweepParamType::kSweepLFOWaveform: {
                            // Convert normalized [0,1] to waveform index [0,5]
                            const int waveformIndex = static_cast<int>(value * 5.0f + 0.5f);
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
                            // Convert normalized [0,1] to note value index [0,15]
                            // Standard note values: Whole, Half, Quarter, Eighth, Sixteenth (x3 for normal, dotted, triplet)
                            const int noteIndex = static_cast<int>(value * (kNoteValueCount - 1) + 0.5f);
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
                            break;

                        // ========================================================
                        // MIDI Parameters (FR-028, FR-029)
                        // ========================================================
                        case SweepParamType::kSweepMidiLearnActive:
                            midiLearnActive_ = (value >= 0.5);
                            break;

                        case SweepParamType::kSweepMidiCCNumber: {
                            assignedMidiCC_ = static_cast<int>(value * 128.0 + 0.5);
                            assignedMidiCC_ = std::clamp(assignedMidiCC_, 0, 128);
                            break;
                        }

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
                                        static_cast<int>(value * (kUIModSourceCount - 1) + 0.5));
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
                            case ModParamType::kLFO1Rate: {
                                constexpr float kMinLog = -4.6052f;
                                constexpr float kMaxLog = 2.9957f;
                                float rateHz = std::exp(kMinLog + static_cast<float>(value) * (kMaxLog - kMinLog));
                                modulationEngine_.setLFO1Rate(rateHz);
                                break;
                            }
                            case ModParamType::kLFO1Shape: {
                                int idx = static_cast<int>(value * 5.0f + 0.5f);
                                modulationEngine_.setLFO1Waveform(static_cast<Krate::DSP::Waveform>(idx));
                                break;
                            }
                            case ModParamType::kLFO1Phase:
                                modulationEngine_.setLFO1PhaseOffset(static_cast<float>(value) * 360.0f);
                                break;
                            case ModParamType::kLFO1Sync:
                                modulationEngine_.setLFO1TempoSync(value >= 0.5);
                                break;
                            case ModParamType::kLFO1NoteValue: {
                                const int idx = static_cast<int>(value * (kNoteValueCount - 1) + 0.5f);
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
                            case ModParamType::kLFO2Rate: {
                                constexpr float kMinLog = -4.6052f;
                                constexpr float kMaxLog = 2.9957f;
                                float rateHz = std::exp(kMinLog + static_cast<float>(value) * (kMaxLog - kMinLog));
                                modulationEngine_.setLFO2Rate(rateHz);
                                break;
                            }
                            case ModParamType::kLFO2Shape: {
                                int idx = static_cast<int>(value * 5.0f + 0.5f);
                                modulationEngine_.setLFO2Waveform(static_cast<Krate::DSP::Waveform>(idx));
                                break;
                            }
                            case ModParamType::kLFO2Phase:
                                modulationEngine_.setLFO2PhaseOffset(static_cast<float>(value) * 360.0f);
                                break;
                            case ModParamType::kLFO2Sync:
                                modulationEngine_.setLFO2TempoSync(value >= 0.5);
                                break;
                            case ModParamType::kLFO2NoteValue: {
                                const int idx = static_cast<int>(value * (kNoteValueCount - 1) + 0.5f);
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
                                float ms = 1.0f + static_cast<float>(value) * 99.0f;
                                modulationEngine_.setEnvFollowerAttack(ms);
                                break;
                            }
                            case ModParamType::kEnvFollowerRelease: {
                                float ms = 10.0f + static_cast<float>(value) * 490.0f;
                                modulationEngine_.setEnvFollowerRelease(ms);
                                break;
                            }
                            case ModParamType::kEnvFollowerSensitivity:
                                modulationEngine_.setEnvFollowerSensitivity(static_cast<float>(value));
                                break;
                            case ModParamType::kEnvFollowerSource: {
                                int idx = static_cast<int>(value * 4.0f + 0.5f);
                                modulationEngine_.setEnvFollowerSource(
                                    static_cast<Krate::DSP::EnvFollowerSourceType>(idx));
                                break;
                            }

                            // Random
                            case ModParamType::kRandomRate: {
                                float hz = 0.1f + static_cast<float>(value) * 49.9f;
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
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setChaosModel(
                                    static_cast<Krate::DSP::ChaosModel>(idx));
                                break;
                            }
                            case ModParamType::kChaosSpeed: {
                                float speed = 0.05f + static_cast<float>(value) * 19.95f;
                                modulationEngine_.setChaosSpeed(speed);
                                break;
                            }
                            case ModParamType::kChaosCoupling:
                                modulationEngine_.setChaosCoupling(static_cast<float>(value));
                                break;

                            // Sample & Hold
                            case ModParamType::kSampleHoldSource: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setSampleHoldSource(
                                    static_cast<Krate::DSP::SampleHoldInputType>(idx));
                                break;
                            }
                            case ModParamType::kSampleHoldRate: {
                                float hz = 0.1f + static_cast<float>(value) * 49.9f;
                                modulationEngine_.setSampleHoldRate(hz);
                                break;
                            }
                            case ModParamType::kSampleHoldSlew: {
                                float ms = static_cast<float>(value) * 500.0f;
                                modulationEngine_.setSampleHoldSlew(ms);
                                break;
                            }

                            // Pitch Follower
                            case ModParamType::kPitchFollowerMinHz: {
                                float hz = 20.0f + static_cast<float>(value) * 480.0f;
                                modulationEngine_.setPitchFollowerMinHz(hz);
                                break;
                            }
                            case ModParamType::kPitchFollowerMaxHz: {
                                float hz = 200.0f + static_cast<float>(value) * 4800.0f;
                                modulationEngine_.setPitchFollowerMaxHz(hz);
                                break;
                            }
                            case ModParamType::kPitchFollowerConfidence:
                                modulationEngine_.setPitchFollowerConfidence(static_cast<float>(value));
                                break;
                            case ModParamType::kPitchFollowerTrackingSpeed: {
                                float ms = 10.0f + static_cast<float>(value) * 290.0f;
                                modulationEngine_.setPitchFollowerTrackingSpeed(ms);
                                break;
                            }

                            // Transient Detector
                            case ModParamType::kTransientSensitivity:
                                modulationEngine_.setTransientSensitivity(static_cast<float>(value));
                                break;
                            case ModParamType::kTransientAttack: {
                                float ms = 0.5f + static_cast<float>(value) * 9.5f;
                                modulationEngine_.setTransientAttack(ms);
                                break;
                            }
                            case ModParamType::kTransientDecay: {
                                float ms = 20.0f + static_cast<float>(value) * 180.0f;
                                modulationEngine_.setTransientDecay(ms);
                                break;
                            }

                            // Rungler
                            case ModParamType::kRunglerRate: {
                                float hz = 0.1f + static_cast<float>(value) * 49.9f;
                                modulationEngine_.setRunglerOsc1Freq(hz);
                                modulationEngine_.setRunglerOsc2Freq(hz * 1.5f);
                                break;
                            }
                            case ModParamType::kRunglerDepth:
                                modulationEngine_.setRunglerDepth(static_cast<float>(value));
                                break;
                            case ModParamType::kRunglerBits: {
                                size_t bits = 4 + static_cast<size_t>(value * 12.0 + 0.5);
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
                                mn.commonParams.drive = static_cast<float>(value) * 10.0f;
                                break;
                            case NodeParamType::kNodeMix:
                                // RangeParameter [0, 100]% -> [0, 1]
                                mn.commonParams.mix = static_cast<float>(value);
                                break;
                            case NodeParamType::kNodeTone:
                                // RangeParameter [200, 8000] Hz
                                mn.commonParams.toneHz = 200.0f + static_cast<float>(value) * 7800.0f;
                                break;
                            case NodeParamType::kNodeBias:
                                // RangeParameter [-1, +1]
                                mn.params.bias = static_cast<float>(value) * 2.0f - 1.0f;
                                break;
                            case NodeParamType::kNodeFolds:
                                // RangeParameter [1, 12] (integer steps)
                                mn.params.folds = 1.0f + std::round(static_cast<float>(value) * 11.0f);
                                break;
                            case NodeParamType::kNodeBitDepth:
                                // RangeParameter [4, 24] (integer steps)
                                mn.params.bitDepth = 4.0f + std::round(static_cast<float>(value) * 20.0f);
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

                        // Push updated nodes to BandProcessor
                        bandProcessors_[band].setMorphNodes(cache.nodes, cache.activeNodeCount);
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
                                const float gainDb = kMinBandGainDb + static_cast<float>(value) * (kMaxBandGainDb - kMinBandGainDb);
                                bandStates_[band].gainDb = gainDb;
                                bandProcessors_[band].setGainDb(gainDb);
                                break;
                            }
                            case BandParamType::kBandPan: {
                                // Convert normalized [0,1] to pan [-1, +1]
                                const float pan = static_cast<float>(value) * 2.0f - 1.0f;
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
                                int idx = static_cast<int>(value * 3.0 + 0.5);
                                int count = std::clamp(idx + 1, kMinActiveNodes, kMaxMorphNodes);
                                bandMorphCache_[band].activeNodeCount = count;
                                bandProcessors_[band].setMorphNodes(
                                    bandMorphCache_[band].nodes, count);
                                break;
                            }
                            case BandParamType::kBandMorphSmoothing: {
                                // RangeParameter [0, 500] ms
                                float timeMs = static_cast<float>(value) * 500.0f;
                                bandProcessors_[band].setMorphSmoothingTime(timeMs);
                                break;
                            }
                            case BandParamType::kBandMorphMode: {
                                // StringListParameter: 3 entries
                                int idx = static_cast<int>(value * 2.0 + 0.5);
                                bandProcessors_[band].setMorphMode(
                                    static_cast<MorphMode>(std::clamp(idx, 0, 2)));
                                break;
                            }
                            case BandParamType::kBandMorphXLink:
                            case BandParamType::kBandMorphYLink:
                            case BandParamType::kBandExpanded:
                            case BandParamType::kBandSelectedNode:
                            case BandParamType::kBandDisplayedType:
                            default:
                                // UI-only params (sweep-morph link, expanded,
                                // selectedNode, displayedType): no processor action
                                break;
                        }
                    }
                }
                // Check for crossover frequency parameters
                else if (isCrossoverParamId(paramId)) {
                    const uint8_t index = extractCrossoverIndex(paramId);
                    if (index < kMaxBands - 1) {
                        // Convert normalized [0,1] to Hz [20, 20000] logarithmically
                        const float logMin = std::log10(kMinCrossoverHz);
                        const float logMax = std::log10(kMaxCrossoverHz);
                        const float logFreq = logMin + static_cast<float>(value) * (logMax - logMin);
                        const float freqHz = std::pow(10.0f, logFreq);
                        crossoverL_.setCrossoverFrequency(static_cast<int>(index), freqHz);
                        crossoverR_.setCrossoverFrequency(static_cast<int>(index), freqHz);
                    }
                }
                break;
        }
    }
}
} // namespace Disrumpo
