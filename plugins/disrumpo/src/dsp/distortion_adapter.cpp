// ==============================================================================
// Distortion Adapter Implementation
// ==============================================================================
// Implementation of the unified distortion interface for Disrumpo plugin.
//
// Reference: specs/003-distortion-integration/spec.md
// ==============================================================================

#include "distortion_adapter.h"

#include <cmath>
#include <algorithm>

namespace Disrumpo {

// =============================================================================
// Lifecycle
// =============================================================================

void DistortionAdapter::prepare(double sampleRate, int maxBlockSize) noexcept {
    sampleRate_ = sampleRate;
    const auto blockSize = static_cast<size_t>(maxBlockSize);

    // Prepare tone filter
    toneFilter_.prepare(sampleRate);
    toneFilter_.setCutoff(commonParams_.toneHz);

    // Prepare DC blocker (10Hz cutoff)
    dcBlocker_.prepare(sampleRate, 10.0f);

    // Prepare all saturation-category processors
    saturation_.prepare(sampleRate, blockSize);
    tube_.prepare(sampleRate, blockSize);
    tape_.prepare(sampleRate, blockSize);
    fuzz_.prepare(sampleRate, blockSize);

    // Prepare direct waveshapers for sample-by-sample processing
    tubeShaper_.setType(Krate::DSP::WaveshapeType::Tube);
    tubeShaper_.setDrive(1.0f);
    tapeShaper_.setType(Krate::DSP::WaveshapeType::Tanh);
    tapeShaper_.setDrive(1.5f);

    // Prepare wavefolder
    wavefolder_.prepare(sampleRate, blockSize);

    // Prepare digital-category processors
    bitcrusher_.prepare(sampleRate, blockSize);
    srReducer_.prepare(sampleRate);

    // Prepare dynamic/temporal
    temporal_.prepare(sampleRate, blockSize);

    // Prepare hybrid-category processors
    ringSaturation_.prepare(sampleRate);
    feedbackDist_.prepare(sampleRate, blockSize);
    allpassSaturator_.prepare(sampleRate, blockSize);

    // Prepare digital continued
    aliasing_.prepare(sampleRate, blockSize);
    bitwiseMangler_.prepare(sampleRate);

    // Prepare experimental-category processors
    chaos_.prepare(sampleRate, blockSize);
    formant_.prepare(sampleRate, blockSize);
    granular_.prepare(sampleRate, blockSize);
    spectral_.prepare(sampleRate, static_cast<size_t>(maxBlockSize));
    fractal_.prepare(sampleRate, blockSize);
    stochastic_.prepare(sampleRate);

    // Reset ring buffer state
    inputRingBuffer_.fill(0.0f);
    outputRingBuffer_.fill(0.0f);
    ringWritePos_ = 0;
    ringReadPos_ = 0;

    // Update DC blocker and block-based state for current type
    updateDCBlockerState();
}

void DistortionAdapter::reset() noexcept {
    // Reset tone filter
    toneFilter_.reset();

    // Reset DC blocker
    dcBlocker_.reset();

    // Reset all processors
    saturation_.reset();
    tube_.reset();
    tape_.reset();
    fuzz_.reset();
    wavefolder_.reset();
    bitcrusher_.reset();
    srReducer_.reset();
    temporal_.reset();
    ringSaturation_.reset();
    feedbackDist_.reset();
    allpassSaturator_.reset();
    aliasing_.reset();
    bitwiseMangler_.reset();
    chaos_.reset();
    formant_.reset();
    granular_.reset();
    spectral_.reset();
    fractal_.reset();
    stochastic_.reset();

    // Reset ring buffer state
    inputRingBuffer_.fill(0.0f);
    outputRingBuffer_.fill(0.0f);
    ringWritePos_ = 0;
    ringReadPos_ = 0;
}

// =============================================================================
// Type Selection
// =============================================================================

void DistortionAdapter::setType(DistortionType type) noexcept {
    currentType_ = type;
    updateDCBlockerState();

    // Update block-based state
    switch (type) {
        case DistortionType::Spectral:
            isBlockBased_ = true;
            blockLatency_ = typeParams_.fftSize;
            break;
        case DistortionType::Granular:
            isBlockBased_ = true;
            // Grain size in samples = grainSizeMs * sampleRate / 1000
            blockLatency_ = static_cast<int>(typeParams_.grainSizeMs * sampleRate_ / 1000.0);
            break;
        default:
            isBlockBased_ = false;
            blockLatency_ = 0;
            break;
    }
}

// =============================================================================
// Parameter Control
// =============================================================================

void DistortionAdapter::setCommonParams(const DistortionCommonParams& params) noexcept {
    commonParams_ = params;
    toneFilter_.setCutoff(std::clamp(params.toneHz, 200.0f, 8000.0f));
}

void DistortionAdapter::setParams(const DistortionParams& params) noexcept {
    typeParams_ = params;
    routeParamsToProcessor();

    // Update block-based latency if applicable
    if (currentType_ == DistortionType::Spectral) {
        blockLatency_ = params.fftSize;
    } else if (currentType_ == DistortionType::Granular) {
        blockLatency_ = static_cast<int>(params.grainSizeMs * sampleRate_ / 1000.0);
    }
}

// =============================================================================
// Processing
// =============================================================================

float DistortionAdapter::process(float input) noexcept {
    // Drive Gate: bypass entire distortion path if drive is essentially 0
    if (commonParams_.drive < 0.0001f) {
        // Passthrough: return input blended with mix (though mix of dry with dry = dry)
        // For true passthrough: output = input
        return input;
    }

    // Store dry signal for mix blend
    const float dry = input;

    // Apply drive scaling
    float wet = input * commonParams_.drive;

    // Process through current type
    wet = processRaw(wet);

    // Apply DC blocker if needed
    if (needsDCBlock_) {
        wet = dcBlocker_.process(wet);
    }

    // Apply tone filter to wet signal
    wet = applyTone(wet);

    // Mix dry/wet
    const float mix = std::clamp(commonParams_.mix, 0.0f, 1.0f);
    return dry * (1.0f - mix) + wet * mix;
}

void DistortionAdapter::processBlock(const float* input, float* output, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i) {
        output[i] = process(input[i]);
    }
}

// =============================================================================
// Latency Query
// =============================================================================

int DistortionAdapter::getProcessingLatency() const noexcept {
    return blockLatency_;
}

// =============================================================================
// Internal Processing
// =============================================================================

float DistortionAdapter::processRaw(float input) noexcept {
    switch (currentType_) {
        // =====================================================================
        // Saturation (D01-D06) - Phase 3
        // =====================================================================
        case DistortionType::SoftClip:
            // D01: Tanh-based soft saturation via SaturationProcessor
            saturation_.setType(Krate::DSP::SaturationType::Tape);
            return saturation_.processSample(input);

        case DistortionType::HardClip:
            // D02: Digital hard clipping via SaturationProcessor
            saturation_.setType(Krate::DSP::SaturationType::Digital);
            return saturation_.processSample(input);

        case DistortionType::Tube:
            // D03: Tube stage emulation - use direct waveshaper for single sample
            tubeShaper_.setAsymmetry(typeParams_.bias);
            return tubeShaper_.process(input);

        case DistortionType::Tape:
            // D04: Tape saturator - use direct waveshaper for single sample
            return tapeShaper_.process(input);

        case DistortionType::Fuzz: {
            // D05: Germanium fuzz - process single sample through block processor
            singleSampleBuffer_[0] = input;
            fuzz_.setFuzzType(Krate::DSP::FuzzType::Germanium);
            fuzz_.process(singleSampleBuffer_, 1);
            return singleSampleBuffer_[0];
        }

        case DistortionType::AsymmetricFuzz: {
            // D06: Silicon fuzz with bias control
            singleSampleBuffer_[0] = input;
            fuzz_.setFuzzType(Krate::DSP::FuzzType::Silicon);
            fuzz_.process(singleSampleBuffer_, 1);
            return singleSampleBuffer_[0];
        }

        // =====================================================================
        // Wavefold (D07-D09) - Phase 4
        // =====================================================================
        case DistortionType::SineFold: {
            // D07: Sine wavefolder (Serge model)
            singleSampleBuffer_[0] = input;
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Serge);
            wavefolder_.process(singleSampleBuffer_, 1);
            return singleSampleBuffer_[0];
        }

        case DistortionType::TriangleFold: {
            // D08: Triangle wavefolder (Simple model)
            singleSampleBuffer_[0] = input;
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Simple);
            wavefolder_.process(singleSampleBuffer_, 1);
            return singleSampleBuffer_[0];
        }

        case DistortionType::SergeFold: {
            // D09: Serge-style wavefolder (Lockhart model)
            singleSampleBuffer_[0] = input;
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Lockhart);
            wavefolder_.process(singleSampleBuffer_, 1);
            return singleSampleBuffer_[0];
        }

        // =====================================================================
        // Rectify (D10-D11) - Phase 4
        // =====================================================================
        case DistortionType::FullRectify:
            // D10: Full-wave rectification (absolute value)
            return std::abs(input);

        case DistortionType::HalfRectify:
            // D11: Half-wave rectification (positive only)
            return std::max(0.0f, input);

        // =====================================================================
        // Digital (D12-D14, D18-D19) - Phase 5
        // =====================================================================
        case DistortionType::Bitcrush: {
            // D12: Bit depth reduction
            singleSampleBuffer_[0] = input;
            bitcrusher_.process(singleSampleBuffer_, 1);
            return singleSampleBuffer_[0];
        }

        case DistortionType::SampleReduce:
            // D13: Sample rate reduction
            return srReducer_.process(input);

        case DistortionType::Quantize: {
            // D14: Quantization distortion (same as bitcrush with different params)
            singleSampleBuffer_[0] = input;
            bitcrusher_.process(singleSampleBuffer_, 1);
            return singleSampleBuffer_[0];
        }

        case DistortionType::Aliasing:
            // D18: Intentional aliasing
            return aliasing_.process(input);

        case DistortionType::BitwiseMangler:
            // D19: Bit rotation and XOR
            return bitwiseMangler_.process(input);

        // =====================================================================
        // Dynamic (D15) - Phase 6
        // =====================================================================
        case DistortionType::Temporal:
            // D15: Time-varying distortion
            return temporal_.processSample(input);

        // =====================================================================
        // Hybrid (D16-D17, D26) - Phase 6
        // =====================================================================
        case DistortionType::RingSaturation:
            // D16: Ring modulation + saturation
            return ringSaturation_.process(input);

        case DistortionType::FeedbackDist:
            // D17: Feedback-based distortion
            return feedbackDist_.process(input);

        case DistortionType::AllpassResonant:
            // D26: Resonant allpass saturation
            return allpassSaturator_.process(input);

        // =====================================================================
        // Experimental (D20-D25) - Phase 6
        // =====================================================================
        case DistortionType::Chaos:
            // D20: Chaotic attractor waveshaping
            return chaos_.process(input);

        case DistortionType::Formant:
            // D21: Formant filtering + distortion
            return formant_.process(input);

        case DistortionType::Granular:
            // D22: Granular distortion
            return granular_.process(input);

        case DistortionType::Spectral:
            // D23: FFT-domain distortion
            return spectral_.process(input);

        case DistortionType::Fractal:
            // D24: Fractal/iterative distortion
            return fractal_.process(input);

        case DistortionType::Stochastic:
            // D25: Noise-modulated distortion
            return stochastic_.process(input);

        default:
            return input;
    }
}

float DistortionAdapter::applyTone(float wet) noexcept {
    return toneFilter_.process(wet);
}

void DistortionAdapter::updateDCBlockerState() noexcept {
    // DC blocking required for asymmetric types that introduce DC offset
    needsDCBlock_ = (currentType_ == DistortionType::AsymmetricFuzz ||
                     currentType_ == DistortionType::FullRectify ||
                     currentType_ == DistortionType::HalfRectify ||
                     currentType_ == DistortionType::FeedbackDist);
}

void DistortionAdapter::routeParamsToProcessor() noexcept {
    // Route type-specific parameters to the appropriate KrateDSP processor.
    // Per-type routing ensures each type's shape controls map to the correct
    // DSP setters without cross-type interference.

    const auto& p = typeParams_;

    switch (currentType_) {
        // =================================================================
        // Saturation (D01-D06)
        // =================================================================
        case DistortionType::SoftClip:
            saturation_.setInputGain(commonParams_.drive * 6.0f);
            saturation_.setMix(1.0f);
            saturation_.setType(Krate::DSP::SaturationType::Tape);
            break;

        case DistortionType::HardClip:
            saturation_.setInputGain(commonParams_.drive * 6.0f);
            saturation_.setMix(1.0f);
            saturation_.setType(Krate::DSP::SaturationType::Digital);
            break;

        case DistortionType::Tube:
            tubeShaper_.setAsymmetry(p.bias);
            tube_.setBias(p.bias);
            tube_.setSaturationAmount(p.sag);
            break;

        case DistortionType::Tape:
            tapeShaper_.setDrive(1.0f + p.sag * 2.0f);
            tape_.setBias(p.bias);
            tape_.setSaturation(p.sag);
            tape_.setModel(p.tapeModel == 0
                ? Krate::DSP::TapeModel::Simple
                : Krate::DSP::TapeModel::Hysteresis);
            break;

        case DistortionType::Fuzz:
            // Note: FuzzType is set per-sample in processRaw() (hardcoded Germanium).
            // Setting it here would pre-empt the crossfade that processRaw() triggers
            // on first use, changing audio behavior. Leave type to processRaw().
            fuzz_.setBias(p.bias);
            fuzz_.setFuzz(p.sustain);
            fuzz_.setOctaveUp(p.octave >= 0.5f);
            break;

        case DistortionType::AsymmetricFuzz:
            // Note: FuzzType is set per-sample in processRaw() (hardcoded Silicon).
            // Setting it here would pre-empt the crossfade that processRaw() triggers
            // on first use, changing audio behavior. Leave type to processRaw().
            fuzz_.setBias(p.bias);
            fuzz_.setFuzz(p.sustain);
            break;

        // =================================================================
        // Wavefold (D07-D09)
        // =================================================================
        case DistortionType::SineFold:
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Serge);
            wavefolder_.setFoldAmount(p.folds);
            wavefolder_.setSymmetry(p.symmetry);
            break;

        case DistortionType::TriangleFold:
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Simple);
            wavefolder_.setFoldAmount(p.folds);
            wavefolder_.setSymmetry(p.symmetry);
            break;

        case DistortionType::SergeFold: {
            // Map foldModel to WavefolderModel (0=Serge, 1=Simple, 2=Buchla259, 3=Lockhart)
            static constexpr Krate::DSP::WavefolderModel models[] = {
                Krate::DSP::WavefolderModel::Serge,
                Krate::DSP::WavefolderModel::Simple,
                Krate::DSP::WavefolderModel::Buchla259,
                Krate::DSP::WavefolderModel::Lockhart
            };
            int mi = std::clamp(p.foldModel, 0, 3);
            wavefolder_.setModel(models[mi]);
            wavefolder_.setFoldAmount(p.folds);
            wavefolder_.setSymmetry(p.symmetry);
            break;
        }

        // =================================================================
        // Rectify (D10-D11) — minimal DSP routing
        // =================================================================
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
            break;

        // =================================================================
        // Digital (D12-D14, D18-D19)
        // =================================================================
        case DistortionType::Bitcrush:
            bitcrusher_.setBitDepth(p.bitDepth);
            bitcrusher_.setDitherAmount(p.dither);
            bitcrusher_.setProcessingOrder(p.bitcrushMode == 0
                ? Krate::DSP::ProcessingOrder::BitCrushFirst
                : Krate::DSP::ProcessingOrder::SampleReduceFirst);
            break;

        case DistortionType::SampleReduce:
            srReducer_.setReductionFactor(p.sampleRateRatio);
            break;

        case DistortionType::Quantize:
            bitcrusher_.setBitDepth(p.quantLevels * 12.0f + 4.0f);
            bitcrusher_.setDitherAmount(p.dither);
            break;

        case DistortionType::Aliasing:
            aliasing_.setDownsampleFactor(p.sampleRateRatio);
            aliasing_.setFrequencyShift(p.freqShift);
            break;

        case DistortionType::BitwiseMangler: {
            int op = std::clamp(p.bitwiseOp, 0, 5);
            bitwiseMangler_.setOperation(static_cast<Krate::DSP::BitwiseOperation>(op));
            bitwiseMangler_.setIntensity(p.bitwiseIntensity);
            // Support both legacy fields (rotateAmount/xorPattern) and shape slot fields
            if (p.rotateAmount != 0) {
                bitwiseMangler_.setOperation(Krate::DSP::BitwiseOperation::BitRotate);
                bitwiseMangler_.setRotateAmount(p.rotateAmount);
            } else if (p.xorPattern != 0xAAAA) {
                bitwiseMangler_.setOperation(Krate::DSP::BitwiseOperation::XorPattern);
                bitwiseMangler_.setPattern(p.xorPattern);
            } else {
                bitwiseMangler_.setPattern(static_cast<uint32_t>(p.bitwisePattern * 65535.0f));
                bitwiseMangler_.setRotateAmount(static_cast<int>(p.bitwiseBits * 32.0f - 16.0f));
            }
            break;
        }

        // =================================================================
        // Dynamic (D15)
        // =================================================================
        case DistortionType::Temporal: {
            int mode = std::clamp(p.dynamicMode, 0, 3);
            temporal_.setMode(static_cast<Krate::DSP::TemporalMode>(mode));
            temporal_.setDriveModulation(p.sensitivity);
            temporal_.setAttackTime(p.attackMs);
            temporal_.setReleaseTime(p.releaseMs);
            temporal_.setHysteresisDepth(p.dynamicDepth);
            break;
        }

        // =================================================================
        // Hybrid (D16-D17, D26)
        // =================================================================
        case DistortionType::RingSaturation:
            ringSaturation_.setModulationDepth(p.modDepth);
            ringSaturation_.setStages(std::clamp(p.stages, 1, 4));
            break;

        case DistortionType::FeedbackDist:
            feedbackDist_.setFeedback(p.feedback);
            feedbackDist_.setDelayTime(p.delayMs);
            feedbackDist_.setLimiterThreshold(p.limiter ? (p.limThreshold * -24.0f) : 0.0f);
            feedbackDist_.setToneFrequency(20.0f + p.filterFreq * 19980.0f);
            break;

        case DistortionType::AllpassResonant: {
            int topo = std::clamp(p.allpassTopo, 0, 3);
            allpassSaturator_.setTopology(static_cast<Krate::DSP::NetworkTopology>(topo));
            allpassSaturator_.setFrequency(p.resonantFreq);
            allpassSaturator_.setFeedback(p.allpassFeedback);
            allpassSaturator_.setDecay(p.decayTimeS);
            break;
        }

        // =================================================================
        // Experimental (D20-D25)
        // =================================================================
        case DistortionType::Chaos: {
            int model = std::clamp(p.chaosAttractor, 0, 3);
            chaos_.setModel(static_cast<Krate::DSP::ChaosModel>(model));
            chaos_.setChaosAmount(p.chaosAmount);
            chaos_.setAttractorSpeed(p.attractorSpeed);
            chaos_.setInputCoupling(p.chaosCoupling);
            break;
        }

        case DistortionType::Formant: {
            int vowel = std::clamp(p.vowelSelect, 0, 4);
            formant_.setVowel(static_cast<Krate::DSP::Vowel>(vowel));
            formant_.setFormantShift(p.formantShift);
            formant_.setVowelBlend(p.formantBlend * 4.0f); // [0,1] → [0,4]
            break;
        }

        case DistortionType::Granular:
            granular_.setGrainSize(p.grainSizeMs);
            granular_.setGrainDensity(1.0f + p.grainDensity * 7.0f); // [0,1] → [1,8]
            granular_.setPositionJitter(p.grainPVar * 50.0f);        // [0,1] → [0,50]
            granular_.setDriveVariation(p.grainDVar);
            break;

        case DistortionType::Spectral: {
            int mode = std::clamp(p.spectralMode, 0, 3);
            spectral_.setMode(static_cast<Krate::DSP::SpectralDistortionMode>(mode));
            spectral_.setMagnitudeBits(static_cast<float>(p.magnitudeBits));
            break;
        }

        case DistortionType::Fractal: {
            int mode = std::clamp(p.fractalMode, 0, 4);
            fractal_.setMode(static_cast<Krate::DSP::FractalMode>(mode));
            fractal_.setIterations(p.iterations);
            fractal_.setScaleFactor(p.scaleFactor);
            fractal_.setFrequencyDecay(p.frequencyDecay);
            fractal_.setFeedbackAmount(p.fractalFB);
            // Set internal drive to 1.0 since the adapter already applies
            // commonParams_.drive to the input before processRaw().
            // Without this, the default internal drive of 2.0 doubles
            // the effective drive (e.g., user sets 3.0 but gets 6.0x).
            fractal_.setDrive(1.0f);
            break;
        }

        case DistortionType::Stochastic: {
            int curve = std::clamp(p.stochasticCurve, 0, 5);
            stochastic_.setBaseType(static_cast<Krate::DSP::WaveshapeType>(curve));
            stochastic_.setJitterAmount(p.jitterAmount);
            stochastic_.setJitterRate(p.jitterRate);
            stochastic_.setCoefficientNoise(p.coefficientNoise);
            break;
        }

        default:
            break;
    }
}

} // namespace Disrumpo
