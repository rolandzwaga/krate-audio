// ==============================================================================
// Distortion Adapter Implementation
// ==============================================================================
// Implementation of the unified distortion interface for Disrumpo plugin.
//
// Reference: specs/003-distortion-integration/spec.md
// ==============================================================================

#include "distortion_adapter.h"

#include <krate/dsp/core/fast_math.h>

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

    // Prepare tape-specific processing
    tapeHfFilter_.prepare(sampleRate);
    tapeHfFilter_.setCutoff(8000.0f);
    flutterPhase_ = 0.0f;

    // Prepare asymmetric fuzz body filter
    bodyFilter_.prepare(sampleRate);
    bodyFilter_.setCutoff(20000.0f);  // Start fully open

    // Prepare wavefold smooth filter
    wavefoldSmoothFilter_.prepare(sampleRate);
    wavefoldSmoothFilter_.setCutoff(20000.0f);  // Start fully open

    // Prepare triangle wavefolder for shape crossfade
    wavefoldTriangle_.setType(Krate::DSP::WavefoldType::Triangle);

    // Prepare wavefolder
    wavefolder_.prepare(sampleRate, blockSize);

    // Prepare digital-category processors
    rawBitCrusher_.prepare(sampleRate);
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

    // Reset tape-specific state
    tapeHfFilter_.reset();
    bodyFilter_.reset();
    wavefoldSmoothFilter_.reset();
    flutterPhase_ = 0.0f;

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
    // Temporal handles drive internally via setBaseDrive() — skip external scaling
    // to avoid double-drive that makes envelope params (sensitivity/attack/release) inaudible
    float wet;
    if (currentType_ == DistortionType::Temporal) {
        wet = input;
    } else {
        wet = input * commonParams_.drive;
    }

    // Process through current type
    wet = processRaw(wet);

    // Apply DC blocker if needed
    if (needsDCBlock_) {
        wet = dcBlocker_.process(wet);
    }

    // Apply tone filter to wet signal
    // Skip for digital types where harsh quantization/aliasing IS the effect
    // The tone filter (prepared at oversampled rate) would destroy the staircase
    if (currentType_ != DistortionType::Bitcrush &&
        currentType_ != DistortionType::SampleReduce &&
        currentType_ != DistortionType::Quantize &&
        currentType_ != DistortionType::Aliasing &&
        currentType_ != DistortionType::BitwiseMangler) {
        wet = applyTone(wet);
    }

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
        case DistortionType::SoftClip: {
            // D01: Parameterized soft clip with curve and knee control
            const float absX = std::abs(input);
            const float sgn = input >= 0.0f ? 1.0f : -1.0f;

            // Knee [0,1] → threshold [0.1, 0.9]
            // knee=0: low threshold, heavy saturation (more color)
            // knee=1: high threshold, only peaks saturate (transparent)
            const float threshold = 0.1f + typeParams_.knee * 0.8f;

            if (absX <= threshold) {
                return input;
            }

            // Curve [0,1] → saturation steepness [1, 6]
            // curve=0: gentle tanh curve (warm, subtle harmonics)
            // curve=1: steep tanh curve (aggressive, near hard-clip)
            const float curveDrive = 1.0f + typeParams_.curve * 5.0f;

            const float headroom = 1.0f - threshold;
            const float excess = (absX - threshold) / headroom;
            const float shaped = headroom * Krate::DSP::FastMath::fastTanh(excess * curveDrive);

            return sgn * (threshold + shaped);
        }

        case DistortionType::HardClip: {
            // D02: Parameterized hard clip with threshold and ceiling
            // Threshold [0,1] → clip onset [0.1, 1.0]
            // threshold=0: clips almost everything (extreme)
            // threshold=1: clips only above unity (subtle)
            const float clipThreshold = 0.1f + typeParams_.threshold * 0.9f;

            // Ceiling [0,1] → output ceiling [0.1, 1.0]
            // ceiling=0: very low output cap (crushed)
            // ceiling=1: full output level
            const float clipCeiling = 0.1f + typeParams_.ceiling * 0.9f;

            const float sgn = input >= 0.0f ? 1.0f : -1.0f;
            const float absX = std::abs(input);

            if (absX <= clipThreshold) {
                // Scale linearly within threshold to map to ceiling range
                return input * (clipCeiling / clipThreshold);
            }
            return sgn * clipCeiling;
        }

        case DistortionType::Tube: {
            // D03: Tube stage emulation via TubeStage processor
            // Supports bias, sag (saturation amount), and multi-stage cascading
            singleSampleBuffer_[0] = input;
            const int stages = std::clamp(typeParams_.satStage + 1, 1, 4);
            for (int s = 0; s < stages; ++s) {
                tube_.process(singleSampleBuffer_, 1);
            }
            return singleSampleBuffer_[0];
        }

        case DistortionType::Tape: {
            // D04: Tape saturator via TapeSaturator processor
            // Apply flutter modulation (subtle pitch/gain wobble)
            float modulated = input;
            if (typeParams_.flutter > 0.001f) {
                // Flutter rate ~4-8 Hz, depth controlled by flutter param
                constexpr float kFlutterRateHz = 5.5f;
                const float flutterDepth = typeParams_.flutter * 0.02f;
                const float lfo = std::sin(flutterPhase_ * 6.2831853f);
                modulated = input * (1.0f + lfo * flutterDepth);
                flutterPhase_ += kFlutterRateHz / static_cast<float>(sampleRate_);
                if (flutterPhase_ >= 1.0f) flutterPhase_ -= 1.0f;
            }

            singleSampleBuffer_[0] = modulated;
            tape_.process(singleSampleBuffer_, 1);
            float result = singleSampleBuffer_[0];

            // Apply HF rolloff filter
            result = tapeHfFilter_.process(result);

            return result;
        }

        case DistortionType::Fuzz: {
            // D05: Fuzz with transistor type selection
            singleSampleBuffer_[0] = input;
            fuzz_.setFuzzType(typeParams_.transistor == 0
                ? Krate::DSP::FuzzType::Germanium
                : Krate::DSP::FuzzType::Silicon);
            fuzz_.process(singleSampleBuffer_, 1);
            return singleSampleBuffer_[0];
        }

        case DistortionType::AsymmetricFuzz: {
            // D06: Asymmetric fuzz with transistor select and body
            // Apply asymmetry: bias the signal before fuzz for asymmetric clipping
            const float asymBias = typeParams_.asymmetry * 0.5f;
            float biased = input + asymBias;

            singleSampleBuffer_[0] = biased;
            fuzz_.setFuzzType(typeParams_.transistor == 0
                ? Krate::DSP::FuzzType::Germanium
                : Krate::DSP::FuzzType::Silicon);
            fuzz_.process(singleSampleBuffer_, 1);

            // Body: one-pole lowpass filter for tone darkening
            // body=0 → fully open (bright/harsh), body=1 → dark/thick
            float result = singleSampleBuffer_[0] - asymBias;
            result = bodyFilter_.process(result);
            return result;
        }

        // =====================================================================
        // Wavefold (D07-D09) - Phase 4
        // =====================================================================
        case DistortionType::SineFold: {
            // D07: Sine wavefolder (Serge model) with bias, shape, smooth
            // Bias: DC offset before folding for asymmetric harmonics
            float biased = input + typeParams_.bias;
            singleSampleBuffer_[0] = biased;
            wavefolder_.process(singleSampleBuffer_, 1);
            float result = singleSampleBuffer_[0];
            // Shape: crossfade between sine fold (0) and triangle fold (1)
            if (typeParams_.shape > 0.001f) {
                float triFolded = wavefoldTriangle_.process(biased);
                result = result * (1.0f - typeParams_.shape) + triFolded * typeParams_.shape;
            }
            // Smooth: lowpass filter to tame harsh harmonics
            result = wavefoldSmoothFilter_.process(result);
            return result;
        }

        case DistortionType::TriangleFold: {
            // D08: Triangle wavefolder (Simple model) with bias, angle, smooth
            float biased = input + typeParams_.bias;
            singleSampleBuffer_[0] = biased;
            wavefolder_.process(singleSampleBuffer_, 1);
            float result = singleSampleBuffer_[0];
            // Angle: crossfade between triangle fold (0) and sine fold (1)
            if (typeParams_.angle > 0.001f) {
                wavefoldTriangle_.setType(Krate::DSP::WavefoldType::Sine);
                float sinFolded = wavefoldTriangle_.process(biased);
                wavefoldTriangle_.setType(Krate::DSP::WavefoldType::Triangle);
                result = result * (1.0f - typeParams_.angle) + sinFolded * typeParams_.angle;
            }
            result = wavefoldSmoothFilter_.process(result);
            return result;
        }

        case DistortionType::SergeFold: {
            // D09: Serge-style wavefolder with selectable model, bias, shape, smooth
            // Model is set in routeParamsToProcessor — do NOT override here
            float biased = input + typeParams_.bias;
            singleSampleBuffer_[0] = biased;
            wavefolder_.process(singleSampleBuffer_, 1);
            float result = singleSampleBuffer_[0];
            // Shape: crossfade with triangle fold
            if (typeParams_.shape > 0.001f) {
                float triFolded = wavefoldTriangle_.process(biased);
                result = result * (1.0f - typeParams_.shape) + triFolded * typeParams_.shape;
            }
            result = wavefoldSmoothFilter_.process(result);
            return result;
        }

        // =====================================================================
        // Rectify (D10-D11) - Phase 4
        // =====================================================================
        case DistortionType::FullRectify: {
            // D10: Full-wave rectification (absolute value) with smooth
            float result = std::abs(input);
            if (typeParams_.smoothness > 0.001f) {
                result = wavefoldSmoothFilter_.process(result);
            }
            return result;
        }

        case DistortionType::HalfRectify: {
            // D11: Half-wave rectification with threshold and smooth
            // Threshold [0,1] → clip point [0, 1]
            const float thresh = typeParams_.threshold;
            float result = (input >= thresh) ? input : 0.0f;
            if (typeParams_.smoothness > 0.001f) {
                result = wavefoldSmoothFilter_.process(result);
            }
            return result;
        }

        // =====================================================================
        // Digital (D12-D14, D18-D19) - Phase 5
        // =====================================================================
        case DistortionType::Bitcrush: {
            // D12: Bit depth reduction via BitCrusher primitive (Layer 1)
            float bitDepth = typeParams_.bitDepth;

            // Jitter: randomize bit depth ±2 bits per sample
            if (bitcrushJitter_ > 0.001f) {
                jitterRng_ ^= jitterRng_ << 13;
                jitterRng_ ^= jitterRng_ >> 17;
                jitterRng_ ^= jitterRng_ << 5;
                float rnd = static_cast<float>(jitterRng_ >> 8) / 16777215.0f;
                float offset = (rnd * 2.0f - 1.0f) * bitcrushJitter_ * 2.0f;
                bitDepth = std::clamp(bitDepth + offset, 1.0f, 16.0f);
            }

            // Set per-sample bit depth (handles jitter) and dither
            rawBitCrusher_.setBitDepth(bitDepth);
            rawBitCrusher_.setDither(
                (bitcrushDitherMode_ != 0) ? typeParams_.dither : 0.0f);

            return rawBitCrusher_.process(input);
        }

        case DistortionType::SampleReduce:
            // D13: Sample rate reduction
            return srReducer_.process(input);

        case DistortionType::Quantize: {
            // D14: N-level uniform quantization (linear level count, distinct from Bitcrush)
            // Levels: [0,1] → [2, 64] linear (vs Bitcrush exponential 2^bits)
            // N levels are uniformly spaced in [-1, +1] with step = 2/(N-1)
            const float numLevels = 2.0f + typeParams_.quantLevels * 62.0f;
            const float stepSize = 2.0f / (numLevels - 1.0f);

            // Map input from [-1,1] to index space [0, N-1]
            float index = (input + 1.0f) / stepSize;

            // Offset shifts the quantization grid for asymmetric harmonics
            index += typeParams_.quantOffset;

            // Optional TPDF dither before rounding
            if (typeParams_.dither > 0.0f) {
                jitterRng_ ^= jitterRng_ << 13;
                jitterRng_ ^= jitterRng_ >> 17;
                jitterRng_ ^= jitterRng_ << 5;
                float r1 = static_cast<float>(jitterRng_ >> 8) / 16777215.0f - 0.5f;
                jitterRng_ ^= jitterRng_ << 13;
                jitterRng_ ^= jitterRng_ >> 17;
                jitterRng_ ^= jitterRng_ << 5;
                float r2 = static_cast<float>(jitterRng_ >> 8) / 16777215.0f - 0.5f;
                index += (r1 + r2) * typeParams_.dither;
            }

            // Round to nearest level, undo offset, clamp to valid range
            index = std::round(index) - typeParams_.quantOffset;
            index = std::clamp(index, 0.0f, numLevels - 1.0f);

            // Convert back to [-1, 1]
            float quantized = index * stepSize - 1.0f;
            quantized = std::clamp(quantized, -1.0f, 1.0f);

            // Optional smoothing lowpass
            if (typeParams_.smoothness > 0.001f) {
                quantized = wavefoldSmoothFilter_.process(quantized);
            }

            return quantized;
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
            // Curve and knee are used directly in processRaw()
            break;

        case DistortionType::HardClip:
            // Threshold and ceiling are used directly in processRaw()
            break;

        case DistortionType::Tube:
            tube_.setBias(p.bias);
            tube_.setSaturationAmount(p.sag);
            break;

        case DistortionType::Tape:
            tape_.setBias(p.bias);
            tape_.setSaturation(p.sag);
            tape_.setModel(p.tapeModel == 0
                ? Krate::DSP::TapeModel::Simple
                : Krate::DSP::TapeModel::Hysteresis);
            // Speed [0,1] → drive: higher speed = more headroom = less natural saturation
            // Map so speed=0 → high drive (hot), speed=1 → low drive (clean)
            tape_.setDrive(12.0f - p.speed * 10.0f);
            // HF rolloff [0,1] → cutoff [20000, 1000] Hz
            // hfRoll=0: no rolloff (full bandwidth), hfRoll=1: heavy rolloff
            tapeHfFilter_.setCutoff(20000.0f - p.hfRoll * 19000.0f);
            // Flutter is read directly in processRaw()
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
            // Body: map [0,1] to cutoff [20000, 400] Hz (exponential)
            // body=0 → 20kHz (fully open), body=1 → 400Hz (dark)
            bodyFilter_.setCutoff(20000.0f * std::pow(400.0f / 20000.0f, p.body));
            break;

        // =================================================================
        // Wavefold (D07-D09)
        // =================================================================
        case DistortionType::SineFold:
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Serge);
            wavefolder_.setFoldAmount(p.folds);
            wavefolder_.setSymmetry(p.symmetry);
            // Shape: crossfade target (triangle wavefolder)
            wavefoldTriangle_.setFoldAmount(p.folds);
            // Smooth: map [0,1] → cutoff [20000, 500] Hz (exponential)
            // smoothness=0 → fully open, smoothness=1 → dark/mellow
            wavefoldSmoothFilter_.setCutoff(20000.0f * std::pow(500.0f / 20000.0f, p.smoothness));
            break;

        case DistortionType::TriangleFold:
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Simple);
            wavefolder_.setFoldAmount(p.folds);
            wavefolder_.setSymmetry(p.symmetry);
            // Shape: crossfade target (sine wavefolder)
            wavefoldTriangle_.setFoldAmount(p.folds);
            wavefoldSmoothFilter_.setCutoff(20000.0f * std::pow(500.0f / 20000.0f, p.smoothness));
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
            wavefoldTriangle_.setFoldAmount(p.folds);
            wavefoldSmoothFilter_.setCutoff(20000.0f * std::pow(500.0f / 20000.0f, p.smoothness));
            break;
        }

        // =================================================================
        // Rectify (D10-D11)
        // =================================================================
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
            // Smooth filter cutoff
            wavefoldSmoothFilter_.setCutoff(20000.0f * std::pow(500.0f / 20000.0f, p.smoothness));
            // DC block toggle — override the always-on default
            needsDCBlock_ = p.dcBlock;
            break;

        // =================================================================
        // Digital (D12-D14, D18-D19)
        // =================================================================
        case DistortionType::Bitcrush:
            // BitCrusher primitive configured per-sample in processRaw for jitter
            rawBitCrusher_.setBitDepth(p.bitDepth);
            rawBitCrusher_.setDither(
                (p.bitcrushMode != 0) ? p.dither : 0.0f);
            bitcrushDitherMode_ = p.bitcrushMode;
            bitcrushJitter_ = p.jitter;
            break;

        case DistortionType::SampleReduce:
            srReducer_.setReductionFactor(p.sampleRateRatio);
            srReducer_.setJitter(p.jitter);
            srReducer_.setMode(static_cast<Krate::DSP::SampleRateReducer::Mode>(p.sampleMode));
            srReducer_.setSmoothness(p.smoothness);
            break;

        case DistortionType::Quantize:
            // Smooth filter cutoff: same exponential mapping as wavefold/rectify
            wavefoldSmoothFilter_.setCutoff(20000.0f * std::pow(500.0f / 20000.0f, p.smoothness));
            break;

        case DistortionType::Aliasing:
            aliasing_.setDownsampleFactor(p.sampleRateRatio);
            aliasing_.setFrequencyShift(p.freqShift);
            break;

        case DistortionType::BitwiseMangler: {
            int op = std::clamp(p.bitwiseOp, 0, 5);
            bitwiseMangler_.setOperation(static_cast<Krate::DSP::BitwiseOperation>(op));
            bitwiseMangler_.setIntensity(p.bitwiseIntensity);
            // Route Pattern and Bits to all relevant DSP setters:
            // - Pattern → XorPattern mask, BitShuffle seed
            // - Bits → BitRotate amount
            bitwiseMangler_.setPattern(static_cast<uint32_t>(p.bitwisePattern * 65535.0f));
            bitwiseMangler_.setRotateAmount(static_cast<int>(p.bitwiseBits * 32.0f - 16.0f));
            bitwiseMangler_.setSeed(static_cast<uint32_t>(p.bitwisePattern * 65535.0f));
            break;
        }

        // =================================================================
        // Dynamic (D15)
        // =================================================================
        case DistortionType::Temporal: {
            int mode = std::clamp(p.dynamicMode, 0, 3);
            temporal_.setMode(static_cast<Krate::DSP::TemporalMode>(mode));
            // Map plugin Drive [0, 10] → baseDrive [0.5, 3.0].
            // The waveshaper's tanh(drive * x) is perceptually flat above drive ~3
            // (tanh(3) ≈ 0.995), so the envelope modulation becomes inaudible when
            // baseDrive is too high. Restricting to [0.5, 3.0] keeps modulation in
            // the zone where it crosses the saturation knee — audibly meaningful.
            // Input is passed raw (no pre-gain) so the envelope tracks true levels.
            const float driveNorm = commonParams_.drive / 10.0f;  // [0, 1]
            temporal_.setBaseDrive(0.5f + driveNorm * 2.5f);
            temporal_.setDriveModulation(p.sensitivity);
            temporal_.setAttackTime(p.attackMs);
            temporal_.setReleaseTime(p.releaseMs);
            temporal_.setHysteresisDepth(p.dynamicDepth);

            // Curve (Slot 2) → waveshape type selection (9 types)
            int shapeIndex = static_cast<int>(p.dynamicCurve * 8.0f + 0.5f);
            shapeIndex = std::clamp(shapeIndex, 0, 8);
            temporal_.setWaveshapeType(
                static_cast<Krate::DSP::WaveshapeType>(shapeIndex));

            // Hold (Slot 7) → hysteresis decay time [1, 500] ms
            float decayMs = 1.0f + p.hold * 499.0f;
            temporal_.setHysteresisDecay(decayMs);
            break;
        }

        // =================================================================
        // Hybrid (D16-D17, D26)
        // =================================================================
        case DistortionType::RingSaturation: {
            ringSaturation_.setModulationDepth(p.modDepth);
            ringSaturation_.setStages(std::clamp(p.stages, 1, 4));

            // Curve → waveshape type (9 discrete types)
            int curveIdx = static_cast<int>(p.rsCurve * 8.0f + 0.5f);
            curveIdx = std::clamp(curveIdx, 0, 8);
            ringSaturation_.setSaturationCurve(
                static_cast<Krate::DSP::WaveshapeType>(curveIdx));

            // Carrier waveform (Sine, Triangle, Square, Saw)
            ringSaturation_.setCarrierWaveform(
                static_cast<Krate::DSP::RSCarrierWaveform>(
                    std::clamp(p.carrierType, 0, 3)));

            // Bias [-1, +1]
            ringSaturation_.setBias(p.bias);

            // Frequency mode
            int freqMode = std::clamp(p.rsFreqSelect, 0, 3);
            ringSaturation_.setFreqMode(
                static_cast<Krate::DSP::RSFreqMode>(freqMode));

            // Carrier frequency param — interpretation depends on mode
            if (freqMode == 0) {
                // Fixed: [0,1] → [20, 5000] Hz exponential
                float freqHz = 20.0f * std::pow(250.0f, p.rsCarrierFreq);
                ringSaturation_.setCarrierFrequency(freqHz);
            } else if (freqMode == 1) {
                // Harmonic: [0,1] → ratio 1-16
                float ratio = 1.0f + std::round(p.rsCarrierFreq * 15.0f);
                ringSaturation_.setHarmonicRatio(ratio);
            }
            // Track/Random: frequency is auto-determined, no param needed
            break;
        }

        case DistortionType::FeedbackDist:
            feedbackDist_.setFeedback(p.feedback);
            feedbackDist_.setDelayTime(p.delayMs);
            feedbackDist_.setLimiterThreshold(p.limiter ? (p.limThreshold * -24.0f) : 0.0f);
            feedbackDist_.setToneFrequency(20.0f + p.filterFreq * 19980.0f);
            feedbackDist_.setFilterType(p.filterType);
            feedbackDist_.setStages(p.stages);
            feedbackDist_.setDecay(p.fbDecay);
            break;

        case DistortionType::AllpassResonant: {
            int topo = std::clamp(p.allpassTopo, 0, 3);
            allpassSaturator_.setTopology(static_cast<Krate::DSP::NetworkTopology>(topo));
            allpassSaturator_.setFrequency(p.resonantFreq);
            allpassSaturator_.setFeedback(p.allpassFeedback);
            allpassSaturator_.setDecay(p.decayTimeS);

            // Drive: denormalize [0,1] → [0.1, 10.0]
            allpassSaturator_.setDrive(0.1f + p.allpassDrive * 9.9f);

            // Saturation curve: map menu index to WaveshapeType
            constexpr Krate::DSP::WaveshapeType kSatTypes[] = {
                Krate::DSP::WaveshapeType::Tanh,      // 0: Tanh
                Krate::DSP::WaveshapeType::HardClip,   // 1: Clip
                Krate::DSP::WaveshapeType::Diode,      // 2: Asym
                Krate::DSP::WaveshapeType::Tube         // 3: Hyst
            };
            int satIdx = std::clamp(p.allpassSatType, 0, 3);
            allpassSaturator_.setSaturationCurve(kSatTypes[satIdx]);

            // Damping
            allpassSaturator_.setDamping(p.allpassDamp);
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
            chaos_.setXDrive(p.chaosXDrive);
            chaos_.setYDrive(p.chaosYDrive);
            chaos_.setSmoothness(p.smoothness);
            break;
        }

        case DistortionType::Formant: {
            int vowel = std::clamp(p.vowelSelect, 0, 4);
            formant_.setVowel(static_cast<Krate::DSP::Vowel>(vowel));
            formant_.setFormantShift(p.formantShift);
            formant_.setAsymmetry(p.formantCurve * 2.0f - 1.0f);   // [0,1] → [-1,+1]
            // Reso: exponential map [0,1] → [0.25, 4.0] gain multiplier
            formant_.setResonanceGain(std::pow(4.0f, p.formantReso * 2.0f - 1.0f));
            // BW: exponential map [0,1] → [0.25, 4.0] bandwidth scale
            formant_.setBandwidthScale(std::pow(4.0f, p.formantBW * 2.0f - 1.0f));
            formant_.setActiveFormants(p.formantCount + 2);         // 0-3 → 2-5
            formant_.setGender(p.formantGender * 2.0f - 1.0f);     // [0,1] → [-1,+1]
            formant_.setVowelBlend(p.formantBlend * 4.0f);         // [0,1] → [0,4]
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
