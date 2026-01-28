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
    // Route type-specific parameters to the appropriate KrateDSP processor
    // Will be fully implemented as each type category is integrated

    const auto category = getCategory(currentType_);

    switch (category) {
        case DistortionCategory::Saturation:
            // Route saturation params
            saturation_.setInputGain(commonParams_.drive * 6.0f); // Convert drive to dB-ish
            saturation_.setMix(1.0f); // We handle mix externally
            tube_.setBias(typeParams_.bias);
            tube_.setSaturationAmount(typeParams_.sag);
            fuzz_.setBias(typeParams_.bias);
            break;

        case DistortionCategory::Wavefold:
            // Route wavefold params
            wavefolder_.setFoldAmount(typeParams_.folds);
            wavefolder_.setSymmetry(typeParams_.symmetry);
            break;

        case DistortionCategory::Digital:
            // Route digital params
            bitcrusher_.setBitDepth(typeParams_.bitDepth);
            srReducer_.setReductionFactor(typeParams_.sampleRateRatio);
            // BitwiseMangler uses BitRotate mode with rotateAmount, or XorPattern mode
            if (typeParams_.rotateAmount != 0) {
                bitwiseMangler_.setOperation(Krate::DSP::BitwiseOperation::BitRotate);
                bitwiseMangler_.setRotateAmount(typeParams_.rotateAmount);
            } else {
                bitwiseMangler_.setOperation(Krate::DSP::BitwiseOperation::XorPattern);
                bitwiseMangler_.setPattern(typeParams_.xorPattern);
            }
            break;

        case DistortionCategory::Dynamic:
            // Route dynamic params
            temporal_.setDriveModulation(typeParams_.sensitivity);
            temporal_.setAttackTime(typeParams_.attackMs);
            temporal_.setReleaseTime(typeParams_.releaseMs);
            break;

        case DistortionCategory::Hybrid:
            // Route hybrid params
            // RingSaturation uses setDrive and setModulationDepth
            ringSaturation_.setModulationDepth(typeParams_.modDepth);
            feedbackDist_.setFeedback(typeParams_.feedback);
            feedbackDist_.setDelayTime(typeParams_.delayMs);
            // AllpassSaturator uses setFrequency, setFeedback, setDecay
            allpassSaturator_.setFrequency(typeParams_.resonantFreq);
            allpassSaturator_.setFeedback(typeParams_.allpassFeedback);
            allpassSaturator_.setDecay(typeParams_.decayTimeS);
            break;

        case DistortionCategory::Experimental:
            // Route experimental params
            chaos_.setChaosAmount(typeParams_.chaosAmount);
            chaos_.setAttractorSpeed(typeParams_.attractorSpeed);
            formant_.setFormantShift(typeParams_.formantShift);
            granular_.setGrainSize(typeParams_.grainSizeMs);
            spectral_.setMagnitudeBits(static_cast<float>(typeParams_.magnitudeBits));
            fractal_.setIterations(typeParams_.iterations);
            fractal_.setScaleFactor(typeParams_.scaleFactor);
            fractal_.setFrequencyDecay(typeParams_.frequencyDecay);
            stochastic_.setJitterAmount(typeParams_.jitterAmount);
            stochastic_.setJitterRate(typeParams_.jitterRate);
            stochastic_.setCoefficientNoise(typeParams_.coefficientNoise);
            break;

        default:
            break;
    }
}

} // namespace Disrumpo
