// ==============================================================================
// Distortion Adapter Implementation
// ==============================================================================
// Implementation of the unified distortion interface for Disrumpo plugin.
//
// Reference: specs/003-distortion-integration/spec.md
// ==============================================================================

#include "distortion_adapter.h"
#include "shape_slot_descriptors.h"

#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/wavefold_math.h>

#include <cmath>
#include <algorithm>
#include <numbers>

namespace Disrumpo {

namespace {

/// First antiderivative of WavefoldMath::triangleFold(x, t) (odd, period 4t, peak t): even,
/// zero at 0 (as WavefolderProcessor's ADAA1 integral).
double triangleFoldF1(double x, double t) noexcept {
    const double p = std::fmod(std::abs(x) + t, 4.0 * t);
    const double g = p < 2.0 * t ? 0.5 * p * p - t * p : 3.0 * t * (p - 2.0 * t) - 0.5 * (p * p - 4.0 * t * t);
    return g + 0.5 * t * t;
}

/// Second antiderivative (F2' = triangleFoldF1): odd, zero at 0. F1 is periodic in |x| + t with
/// period 4t and mean t^2 / 2; K integrates one period's profile from its start (2 t^3 per period).
double triangleFoldF2(double x, double t) noexcept {
    const double period = 4.0 * t;
    const double q = std::abs(x) + t;
    const double cycles = std::floor(q / period);
    const double p = q - cycles * period;
    double k = 0.0;
    if (p < 2.0 * t) {
        k = p * p * p / 6.0 - 0.5 * t * p * p + 0.5 * t * t * p;
    } else {
        const double r = p - 2.0 * t;
        k = t * t * t / 3.0 + 0.5 * t * r * r - r * r * r / 6.0 + 0.5 * t * t * r;
    }
    const double h = cycles * 2.0 * t * t * t + k - t * t * t / 6.0;  // K(t) = t^3 / 6 at x = 0
    return x < 0.0 ? -h : h;
}

/// The factor AllpassResonant's presets were voiced at (4x until round 3, user ruling 2026-11-11):
/// above it the adapter keeps the damping cutoff in Hz it had there (routeParamsToProcessor).
constexpr double kAllpassVoicedFactor = 4.0;

/// ADAA2 input step at or below which a difference quotient takes its limit (argument units,
/// as ClosedFormAdaa2::kEpsilon and WavefolderProcessor::kAdaaEpsilon)
constexpr double kTriangleAdaa2Epsilon = 1.0e-5;

/// Temporal's Curve slot [0, 1] -> WaveshapeType index 0..8 (7 = Diode, 8 = Tube).
int temporalShapeIndex(float dynamicCurve) noexcept {
    return waveshapeCurveIndex(dynamicCurve);
}

/// Makeup gain for the fold models whose output peak the fold amount caps below 1 (D-158).
/// A triangle fold has threshold 1/folds, so it peaks at 1/folds; the classic Buchla259 sums
/// triangle folds at T_i/folds weighted by G_i and divides by sum(G_i), peaking at
/// sum(G_i T_i) / sum(G_i) / folds = 1.4 / (3 folds). The sine models already reach 1.
float foldMakeup(Krate::DSP::WavefolderModel model, float foldAmount) noexcept {
    switch (model) {
        case Krate::DSP::WavefolderModel::Simple:
            return foldAmount;
        case Krate::DSP::WavefolderModel::Buchla259:
            return foldAmount * 3.0f / 1.4f;
        case Krate::DSP::WavefolderModel::Serge:
        case Krate::DSP::WavefolderModel::Lockhart:
            break;
    }
    return 1.0f;
}

/// The digital / experimental types whose output follows their driven input level, so the
/// input drive acted as a plain output gain on them (D-144).
bool normalisesDrive(DistortionType type) noexcept {
    return type == DistortionType::Aliasing || type == DistortionType::SampleReduce ||
           type == DistortionType::BitwiseMangler || type == DistortionType::Granular ||
           type == DistortionType::Chaos || type == DistortionType::Spectral;
}

/// Drive above which the scaling types' output level stops following the drive.
constexpr float kDriveGainKnee = 5.0f;

/// Fuzz, AsymmetricFuzz and the rectifiers scale with their driven input (the rectifiers
/// exactly, the fuzzes nearly: +7.5 / +9.4 dBFS peak at drive 7.5 / 10 from a -6 dBFS
/// sine), so above kDriveGainKnee the drive acted as an output gain on them (follow-up 50).
bool capsDriveGain(DistortionType type) noexcept {
    return type == DistortionType::Fuzz || type == DistortionType::AsymmetricFuzz ||
           type == DistortionType::FullRectify || type == DistortionType::HalfRectify;
}

/// SoftClip's curve, in double: x up to the threshold t, then t + h tanh(c (|x| - t) / h), odd.
double softClipCurve(double x, double t, double h, double c) noexcept {
    const double a = std::abs(x);
    if (a <= t) return x;
    const double y = t + h * std::tanh(c * (a - t) / h);
    return x >= 0.0 ? y : -y;
}

/// Antiderivative of softClipCurve, zero at 0 and even: x^2 / 2 up to t, then
/// t^2 / 2 + t (|x| - t) + (h^2 / c) ln cosh(c (|x| - t) / h), ln cosh(z) = z + ln(1 + e^-2z) - ln 2.
double softClipIntegral(double x, double t, double h, double c) noexcept {
    const double a = std::abs(x);
    if (a <= t) return 0.5 * x * x;
    const double z = c * (a - t) / h;
    const double logCosh = z + std::log1p(std::exp(-2.0 * z)) - std::numbers::ln2;
    return 0.5 * t * t + t * (a - t) + (h * h / c) * logCosh;
}

/// Fuzz / AsymmetricFuzz makeup slope: 0.95 dB per unit of drive, in nepers (0.95 / 20 * ln 10).
constexpr float kFuzzMakeupNepersPerDrive = 0.10939f;

/// Drive re-stage (user ruling 2026-11-10): +18 dB, exact as a power of two.
constexpr float kDriveReference = 8.0f;

/// The level-dependent types: their knee, clip threshold, fold point, rectifier threshold or
/// loop saturation sits at a fixed sample value, which a -18 dBFS input at drive 1 never
/// reached. They are driven kDriveReference harder into the shaper and brought back down by
/// the same factor after it, so drive 1 bites at -18 dBFS and a signal in their linear region
/// keeps its level. The other types are left as they were: FullRectify is the same at any
/// gain; SampleReduce, Aliasing, Spectral and Granular distort the same at any level;
/// Quantize, BitwiseMangler and Fractal already distort past 5 % at drive 1 and -18 dBFS
/// (more input makes Quantize and BitwiseMangler cleaner); Bitcrush coarsens its grid
/// instead (processRaw).
bool restagesDrive(DistortionType type) noexcept {
    switch (type) {
        case DistortionType::SoftClip:
        case DistortionType::HardClip:
        case DistortionType::Tube:
        case DistortionType::Tape:
        case DistortionType::Fuzz:
        case DistortionType::AsymmetricFuzz:
        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
        case DistortionType::HalfRectify:
        case DistortionType::Temporal:
        case DistortionType::RingSaturation:
        case DistortionType::FeedbackDist:
        case DistortionType::AllpassResonant:
        case DistortionType::Chaos:
        case DistortionType::Formant:
        case DistortionType::Stochastic:
            return true;
        default:
            return false;
    }
}

}  // namespace

// =============================================================================
// Lifecycle
// =============================================================================

void DistortionAdapter::prepare(double sampleRate, int maxBlockSize, int maxOversampleFactor) noexcept {
    maxBlockSize_ = maxBlockSize;
    hostRate_ = sampleRate;
    maxProcessingRate_ = sampleRate * static_cast<double>(std::max(1, maxOversampleFactor));

    // Size every rate-dependent member for the highest rate it can be clocked at: their
    // buffers (DelayLine, Oversampler) are std::vectors, so a later prepare() at a lower
    // or equal rate stays within capacity and allocates nothing (setProcessingRate).
    prepareRateDependent(maxProcessingRate_);

    // Initial cutoffs (OnePoleLP::prepare keeps the cutoff when the rate changes)
    toneFilter_.setCutoff(commonParams_.toneHz);
    tapeHfFilter_.setCutoff(8000.0f);
    flutterPhase_ = 0.0f;
    bodyFilter_.setCutoff(20000.0f);            // Start fully open
    wavefoldSmoothFilter_.setCutoff(20000.0f);  // Start fully open

    // Prepare triangle wavefolder for shape crossfade
    wavefoldTriangle_.setType(Krate::DSP::WavefoldType::Triangle);

    // SpectralDistortion allocates in prepare() (STFT / FFT setup), so it is prepared
    // once, at the host rate, and never re-rated (Spectral recommends 1x). Allocate for
    // the largest FFT size the plugin offers; the active size then changes without
    // allocating (routeParamsToProcessor runs on the audio thread).
    spectral_.prepare(sampleRate, kSpectralMaxFftSize);
    spectral_.setActiveFftSize(static_cast<std::size_t>(typeParams_.fftSize));

    // Latency delay lines (D-081): the largest latency is in clocked samples at any rate.
    // DelayLine sizes in seconds x rate; a rate of 1 makes the seconds samples, exactly.
    dryDelay_.prepare(1.0, static_cast<float>(kMaxLatencySamples));
    padDelay_.prepare(1.0, static_cast<float>(kMaxLatencySamples));
    updateBlockLatency();
    latent_ = false;
    refreshLatencyMode();

    // Stochastic: silent input stays silent. Subtract the curve's value at the jitter
    // offset (D-142); the residual DC is removed by dcBlocker_ (updateDCBlockerState).
    stochastic_.setOffsetCompensation(true);

    // Update DC blocker and block-based state for current type
    updateDCBlockerState();

    // Processing starts at the host rate
    setProcessingRate(sampleRate);

    // Mix starts on its value (setProcessingRate returns early when the rate is unchanged)
    mixSmoother_.setTarget(std::clamp(commonParams_.mix, 0.0f, 1.0f));
    mixSmoother_.snapToTarget();
    currentMix_ = mixSmoother_.getTarget();
}

void DistortionAdapter::setProcessingRate(double rate) noexcept {
    rate = std::min(rate, maxProcessingRate_);
    if (rate == sampleRate_) {
        return;
    }
    prepareRateDependent(rate);
    if (fadeFromRunning_) {
        // The outgoing type's filters are derived for the old rate: fade from the last output
        fadeFromRunning_ = false;
        typeFadeOrigin_ = lastOutput_;
    }
    // Re-apply what is derived from the rate: the tone cutoff (clamped to the new
    // Nyquist), the active type's routed parameters and the Granular latency in samples.
    setCommonParams(commonParams_);
    setParams(typeParams_);
}

void DistortionAdapter::prepareRateDependent(double rate) noexcept {
    sampleRate_ = rate;
    const auto blockSize = static_cast<size_t>(maxBlockSize_);

    // Mix smoothing (kMixSmoothingMs at the clocked rate)
    mixSmoother_.configure(kMixSmoothingMs, static_cast<float>(rate));

    // Tone filter and DC blocker (10Hz cutoff)
    toneFilter_.prepare(rate);
    dcBlocker_.prepare(rate, 10.0f);

    // Saturation-category processors
    for (auto& stage : tube_) {
        stage.prepare(rate, blockSize);
    }
    tape_.prepare(rate, blockSize);
    fuzz_.prepare(rate, blockSize);

    // Tape HF, asymmetric fuzz body and wavefold smooth filters
    tapeHfFilter_.prepare(rate);
    bodyFilter_.prepare(rate);
    wavefoldSmoothFilter_.prepare(rate);

    // Wavefolder
    wavefolder_.prepare(rate, blockSize);
    // TriangleFold ADAA2 path: WavefolderProcessor's 5 ms smoothing and 10 Hz DC blocker
    triFoldAmountSmoother_.configure(Krate::DSP::WavefolderProcessor::kDefaultSmoothingMs, static_cast<float>(rate));
    triFoldSymmetrySmoother_.configure(Krate::DSP::WavefolderProcessor::kDefaultSmoothingMs, static_cast<float>(rate));
    triFoldDcBlocker_.prepare(rate, Krate::DSP::WavefolderProcessor::kDCBlockerCutoffHz);
    resetTriangleFoldAdaa2();

    // Digital-category processors
    rawBitCrusher_.prepare(rate);
    srReducer_.prepare(rate);

    // Dynamic/temporal
    temporal_.prepare(rate, blockSize);

    // Hybrid-category processors
    ringSaturation_.prepare(rate);
    feedbackDist_.prepare(rate, blockSize);
    allpassSaturator_.prepare(rate, blockSize);

    // Digital continued
    aliasing_.prepare(rate, blockSize);
    bitwiseMangler_.prepare(rate);

    // Experimental-category processors (Spectral: see prepare())
    chaos_.prepare(rate, blockSize);
    formant_.prepare(rate, blockSize);
    granular_.prepare(rate, blockSize);
    fractal_.prepare(rate, blockSize);
    stochastic_.prepare(rate);
}

void DistortionAdapter::reset() noexcept {
    // Reset tone filter
    toneFilter_.reset();

    // Reset DC blocker
    dcBlocker_.reset();

    // Reset all processors
    for (auto& stage : tube_) {
        stage.reset();
    }
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
    resetTriangleFoldAdaa2();
    flutterPhase_ = 0.0f;
    halfRectAdaaPrev_ = 0.0;
    softClipAdaaPrev_ = 0.0;
    adaaDryPrev_ = 0.0f;

    // Latency delay lines (D-081)
    dryDelay_.reset();
    padDelay_.reset();

    // Mix: no glide to finish after a reset
    mixSmoother_.setTarget(std::clamp(commonParams_.mix, 0.0f, 1.0f));
    mixSmoother_.snapToTarget();
    currentMix_ = mixSmoother_.getTarget();

    // Type-change fade (D-141): every processor is clear, nothing to fade from
    resetPending_ = false;
    typeFadeRemaining_ = 0;
    lastOutput_ = 0.0f;
    hasOutput_ = false;
}

void DistortionAdapter::warmStartFrom(const DistortionAdapter& other) noexcept {
    // The next output of a blocker is x - (x[n-1] - R y[n-1]): at x = 0, minus the mean it removes
    const auto seed = [](Krate::DSP::DCBlocker& to, const Krate::DSP::DCBlocker& from) {
        Krate::DSP::DCBlocker probe = from;
        to.setState(-probe.process(0.0f), 0.0f);
    };
    seed(dcBlocker_, other.dcBlocker_);
    seed(triFoldDcBlocker_, other.triFoldDcBlocker_);
    wavefolder_.seedDcBlockerFrom(other.wavefolder_);
    fuzz_.seedDcBlockerFrom(other.fuzz_);
    allpassSaturator_.warmStartFrom(other.allpassSaturator_);
}

// =============================================================================
// Type Selection
// =============================================================================

void DistortionAdapter::setType(DistortionType type) noexcept {
    const bool changed = type != currentType_;
    const bool hadDCBlock = needsDCBlock_;
    const bool hadTone = usesToneFilter(currentType_);
    const DistortionType previousType = currentType_;
    const bool wasLatent = latent_;
    currentType_ = type;
    updateDCBlockerState();
    updateBlockLatency();
    if (!changed || !hasOutput_) {
        // processSameFamily re-asserts the type every sample: no switch. Before the first
        // processed sample (after prepare() / reset()) every processor is clear already.
        return;
    }

    // The outgoing type keeps running beside the new one for the fade when they share no
    // processor state and neither is latent; a change during a fade fades from the last output.
    // Its DC blocker and tone filter continue from the shared ones (round 3, see fadeFromRunning_)
    fadeFromRunning_ = typeFadeRemaining_ == 0 && !wasLatent && !latent_ &&
                       !sharesProcessorState(previousType, currentType_);
    if (fadeFromRunning_) {
        fadeFromType_ = previousType;
        fadeFromDCBlock_ = hadDCBlock;
        fadeAdaaDryPrev_ = adaaDryPrev_;
        fadeDcBlocker_ = dcBlocker_;
        fadeToneFilter_ = toneFilter_;
    }

    // The new type's processor resumes with what it held at its last use (D-032, D-080):
    // clear it at the next process(), after the caller's setParams has routed its targets
    resetPending_ = true;
    // The shared DC blocker and tone filter hold stale state if the old type bypassed them
    if (needsDCBlock_ && !hadDCBlock) {
        dcBlocker_.reset();
    }
    if (usesToneFilter(currentType_) && !hadTone) {
        toneFilter_.reset();
    }
    // Fade from the last output to the new type over 5 ms (D-141)
    typeFadeOrigin_ = lastOutput_;
    typeFadeLength_ = std::max(1, static_cast<int>(kTypeFadeSeconds * sampleRate_));
    typeFadeRemaining_ = typeFadeLength_;
}

bool DistortionAdapter::usesToneFilter(DistortionType type) noexcept {
    // The digital types skip the tone filter: their staircase / aliasing is the effect
    return type != DistortionType::Bitcrush && type != DistortionType::SampleReduce &&
           type != DistortionType::Quantize && type != DistortionType::Aliasing &&
           type != DistortionType::BitwiseMangler;
}

void DistortionAdapter::resetProcessorFor(DistortionType type) noexcept {
    switch (type) {
        case DistortionType::SoftClip:
            softClipAdaaPrev_ = 0.0;
            break;
        case DistortionType::HardClip:
            break;  // memoryless
        case DistortionType::Tube:
            for (auto& stage : tube_) {
                stage.reset();
            }
            break;
        case DistortionType::Tape:
            tape_.reset();
            tapeHfFilter_.reset();
            flutterPhase_ = 0.0f;
            break;
        case DistortionType::Fuzz:
            fuzz_.reset();
            break;
        case DistortionType::AsymmetricFuzz:
            fuzz_.reset();
            bodyFilter_.reset();
            break;
        case DistortionType::SineFold:
        case DistortionType::SergeFold:
            wavefolder_.reset();
            wavefoldSmoothFilter_.reset();
            break;
        case DistortionType::TriangleFold:
            wavefolder_.reset();
            wavefoldSmoothFilter_.reset();
            resetTriangleFoldAdaa2();
            break;
        case DistortionType::HalfRectify:
            halfRectAdaaPrev_ = 0.0;
            wavefoldSmoothFilter_.reset();
            break;
        case DistortionType::FullRectify:
        case DistortionType::Quantize:
            wavefoldSmoothFilter_.reset();
            break;
        case DistortionType::Bitcrush:
            rawBitCrusher_.reset();
            break;
        case DistortionType::SampleReduce:
            srReducer_.reset();
            break;
        case DistortionType::Aliasing:
            aliasing_.reset();
            break;
        case DistortionType::BitwiseMangler:
            bitwiseMangler_.reset();
            break;
        case DistortionType::Temporal:
            temporal_.reset();
            break;
        case DistortionType::RingSaturation:
            ringSaturation_.reset();
            break;
        case DistortionType::FeedbackDist:
            feedbackDist_.reset();
            break;
        case DistortionType::AllpassResonant:
            allpassSaturator_.reset();
            break;
        case DistortionType::Chaos:
            chaos_.reset();
            break;
        case DistortionType::Formant:
            formant_.reset();
            break;
        case DistortionType::Granular:
            granular_.reset();
            break;
        case DistortionType::Spectral:
            spectral_.reset();
            break;
        case DistortionType::Fractal:
            fractal_.reset();
            break;
        case DistortionType::Stochastic:
            stochastic_.reset();
            break;
        default:
            break;
    }
}

void DistortionAdapter::updateBlockLatency() noexcept {
    if (currentType_ != DistortionType::Spectral) {
        // setParams runs per sample on the morph path: nothing to do in the common case
        if (blockLatency_ != 0) {
            blockLatency_ = 0;
            refreshLatencyMode();
        }
        return;
    }
    // The active FFT size follows typeParams_ (a no-op when unchanged), so the latency is
    // the processor's own: FFT size - 1 (getProcessingLatency)
    spectral_.setActiveFftSize(static_cast<std::size_t>(typeParams_.fftSize));
    blockLatency_ = std::clamp(static_cast<int>(spectral_.latency()) - 1, 0, kMaxLatencySamples);
    refreshLatencyMode();
}

void DistortionAdapter::setLatencyAlignment(int samples) noexcept {
    alignLatency_ = std::clamp(samples, 0, kMaxLatencySamples);
    refreshLatencyMode();
}

bool DistortionAdapter::sharesProcessorState(DistortionType a, DistortionType b) noexcept {
    // Fuzz / AsymmetricFuzz share fuzz_; the folds, the rectifiers and Quantize share the wavefold
    // smoothing filter (the folds also wavefolder_); Bitcrush and Quantize share the dither RNG
    const auto group = [](DistortionType t) {
        switch (t) {
            case DistortionType::Fuzz:
            case DistortionType::AsymmetricFuzz:
                return 1;
            case DistortionType::SineFold:
            case DistortionType::TriangleFold:
            case DistortionType::SergeFold:
            case DistortionType::FullRectify:
            case DistortionType::HalfRectify:
                return 2;
            case DistortionType::Quantize:
                return 3;  // shares with group 2 (smoothing filter) and Bitcrush (RNG)
            case DistortionType::Bitcrush:
                return 4;
            default:
                return 0;
        }
    };
    const int ga = group(a);
    const int gb = group(b);
    if (ga == 0 || gb == 0) return false;
    if (ga == gb) return true;
    const auto quantizeWith = [](int g) { return g == 2 || g == 4; };
    return (ga == 3 && quantizeWith(gb)) || (gb == 3 && quantizeWith(ga));
}

void DistortionAdapter::refreshLatencyMode() noexcept {
    const bool latent = blockLatency_ != 0 || alignLatency_ != 0;
    if (latent && fadeFromRunning_) {
        // The outgoing type ran unpadded: the rest of the fade starts from the last output
        fadeFromRunning_ = false;
        typeFadeOrigin_ = lastOutput_;
    }
    if (latent && !latent_) {
        // The delay lines were not written while the adapter was not latent
        dryDelay_.reset();
        padDelay_.reset();
    }
    latent_ = latent;
}

// =============================================================================
// Parameter Control
// =============================================================================

void DistortionAdapter::setCommonParams(const DistortionCommonParams& params) noexcept {
    const bool driveChanged = params.drive != commonParams_.drive;
    commonParams_ = params;
    // Mix glides to the new value; before the first output it starts there (kMixSmoothingMs)
    mixSmoother_.setTarget(std::clamp(params.mix, 0.0f, 1.0f));
    if (!hasOutput_) {
        mixSmoother_.snapToTarget();
        currentMix_ = mixSmoother_.getTarget();
    }
    // Fuzz / AsymmetricFuzz level makeup above drive 1 (user ruling 2026-11-10): 0.95 dB per unit of
    // drive, fitted to the level the 1 / drive^2 clean blend loses at -18 dBFS (Init: 1.9 / 4.3 /
    // 9.0 dB at drive 3 / 6 / 10), so their output stays level from drive 1 to 10
    fuzzMakeup_ = params.drive > 1.0f ? std::exp(kFuzzMakeupNepersPerDrive * (params.drive - 1.0f)) : 1.0f;
    toneFilter_.setCutoff(std::clamp(params.toneHz, 200.0f, 8000.0f));
    // Temporal and Stochastic take the Drive inside routeParamsToProcessor() (they are not
    // pre-scaled), so a Drive change must re-route them: callers set the type parameters
    // before the common ones (MorphEngine::setNodes)
    if (driveChanged &&
        (currentType_ == DistortionType::Temporal || currentType_ == DistortionType::Stochastic)) {
        routeParamsToProcessor();
    }
}

void DistortionAdapter::setParams(const DistortionParams& params) noexcept {
    typeParams_ = params;
    routeParamsToProcessor();
    updateBlockLatency();
}

// =============================================================================
// Processing
// =============================================================================

float DistortionAdapter::process(float input) noexcept {
    if (resetPending_) {
        resetProcessorFor(currentType_);
        resetPending_ = false;
    }
    currentMix_ = mixSmoother_.process();
    float out = 0.0f;
    if (!latent_) {
        out = processWithDry(input, input);
    } else {
        // A latent type mixes (and passes through) its dry image delayed by its own latency,
        // and the owner's alignment pads the output up to its slowest member's latency (D-081)
        dryDelay_.write(input);
        padDelay_.write(processWithDry(input, dryDelay_.read(static_cast<size_t>(blockLatency_))));
        out = padDelay_.read(static_cast<size_t>(std::max(0, alignLatency_ - blockLatency_)));
    }
    if (typeFadeRemaining_ > 0) {
        // Linear crossfade from the outgoing type, running beside the new one (fadeFromRunning_),
        // or else from the last output before the type change; its last sample is the new
        // type's output exactly (branch, not g == 1 arithmetic)
        --typeFadeRemaining_;
        if (typeFadeRemaining_ > 0) {
            const float g =
                1.0f - static_cast<float>(typeFadeRemaining_) / static_cast<float>(typeFadeLength_);
            const float origin = fadeFromRunning_
                                     ? processWithDryFor(fadeFromType_, input, input, fadeAdaaDryPrev_,
                                                         fadeDcBlocker_, fadeToneFilter_, fadeFromDCBlock_)
                                     : typeFadeOrigin_;
            out = origin + g * (out - origin);
        }
    }
    lastOutput_ = out;
    hasOutput_ = true;
    return out;
}

float DistortionAdapter::processWithDryFor(DistortionType type, float input, float dry, float& dryPrev,
                                             Krate::DSP::DCBlocker& dcBlocker, Krate::DSP::OnePoleLP& toneFilter,
                                             bool dcBlock) noexcept {
    // ADAA1 delays the wet path by half a clocked sample (its linear segments read the mean of the
    // last two inputs) and the owner aligns the band to it (usesAdaa): the dry image, which the
    // drive gate and the mix pass on, takes the same mean so both arrive together
    const float prevDry = dryPrev;
    dryPrev = dry;
    if (usesAdaa(type) && adaaActive()) {
        dry = usesAdaa2(type) ? prevDry : 0.5f * (dry + prevDry);  // ADAA2: one clocked sample
    }

    // Drive Gate: drive exactly 0 is an exact passthrough of the dry image (the
    // input, delayed by a latent type's latency)
    if (commonParams_.drive <= 0.0f) {
        return dry;
    }

    // Below kDriveBlendFull the wet path runs at that drive and is blended in by
    // drive / kDriveBlendFull, so the transfer is continuous from the passthrough
    // at 0 into the driven curve (D-014: the old hard gate at 1e-4 stepped a
    // linear-region shaper from unity to -80 dB)
    const float wetWeight = std::min(commonParams_.drive / kDriveBlendFull, 1.0f);
    const float drive = std::max(commonParams_.drive, kDriveBlendFull);

    // Apply drive scaling
    // Temporal handles drive internally via setBaseDrive() — skip external scaling
    // to avoid double-drive that makes envelope params (sensitivity/attack/release) inaudible.
    // Stochastic handles drive internally so jitter/coeff noise operate on the normalized
    // signal BEFORE drive amplification — otherwise drive pushes the signal deep into the
    // waveshaper's saturation plateau where ±0.5 of jitter is compressed to nothing.
    float wet;
    if (type == DistortionType::Temporal ||
        type == DistortionType::Stochastic) {
        wet = input;
    } else {
        wet = input * drive;
    }

    // Process through current type, re-staged by kDriveReference (exact: a power of two)
    if (restagesDrive(type)) {
        wet = processRawFor(type, wet * kDriveReference) * (1.0f / kDriveReference);
    } else {
        wet = processRawFor(type, wet);
    }

    // Drive sets how hard a digital type bites, not its output level: divide by
    // max(1, drive) (D-144). The branch keeps drive <= 1 bit-identical.
    if (drive > 1.0f && normalisesDrive(type)) {
        wet /= drive;
    }
    // Fuzz, AsymmetricFuzz and the rectifiers: unchanged up to kDriveGainKnee, scaled by
    // kDriveGainKnee / drive above it (follow-up 50). The branch keeps drive <= 5 bit-identical.
    if (drive > kDriveGainKnee && capsDriveGain(type)) {
        wet *= kDriveGainKnee / drive;
    }

    // Apply DC blocker if needed
    if (dcBlock) {
        wet = dcBlocker.process(wet);
    }

    // Apply tone filter to wet signal
    // Skip for digital types where harsh quantization/aliasing IS the effect
    // The tone filter (prepared at oversampled rate) would destroy the staircase
    if (usesToneFilter(type)) {
        wet = toneFilter.process(wet);
    }

    // Mix dry/wet (the drive blend scales the wet share; exactly the old mix at
    // drive >= kDriveBlendFull, where wetWeight is 1 by the min above); the smoothed Mix of this
    // sample (process())
    const float mix = currentMix_;
    if (wetWeight >= 1.0f) {
        return dry * (1.0f - mix) + wet * mix;
    }
    const float wetMix = mix * wetWeight;
    return dry * (1.0f - wetMix) + wet * wetMix;
}

void DistortionAdapter::processBlock(const float* input, float* output, int numSamples) noexcept {
    // process() per sample while a reset or a type fade is pending, and always for a latent
    // type (its dry image and padding run through delay lines)
    int i = 0;
    while (i < numSamples && (resetPending_ || latent_ || typeFadeRemaining_ > 0 || !mixSmoother_.isComplete())) {
        output[i] = process(input[i]);
        ++i;
    }
    if (i >= numSamples) {
        return;
    }

    // From here every sample is processWithDry(x, x) with parameters no sample changes:
    // the same arithmetic per sample, its branches decided once for the span
    const float* in = input + i;
    float* out = output + i;
    const int n = numSamples - i;
    const bool adaaDry = usesAdaa(currentType_) && adaaActive();
    const bool adaa2Dry = adaaDry && usesAdaa2(currentType_);  // one clocked sample, not half
    if (commonParams_.drive <= 0.0f && !adaaDry) {
        std::copy_n(in, n, out);  // Drive Gate: exact passthrough
        adaaDryPrev_ = in[n - 1];
    } else if (commonParams_.drive <= 0.0f) {
        for (int k = 0; k < n; ++k) {  // Drive Gate: the dry image, ADAA-aligned (processWithDry)
            out[k] = adaa2Dry ? adaaDryPrev_ : 0.5f * (in[k] + adaaDryPrev_);
            adaaDryPrev_ = in[k];
        }
    } else {
        const float wetWeight = std::min(commonParams_.drive / kDriveBlendFull, 1.0f);
        const float drive = std::max(commonParams_.drive, kDriveBlendFull);
        const bool scaleInput = currentType_ != DistortionType::Temporal &&
                                currentType_ != DistortionType::Stochastic;
        const bool restage = restagesDrive(currentType_);
        const float preGain = (scaleInput ? drive : 1.0f) * (restage ? kDriveReference : 1.0f);
        const float makeup = restage ? 1.0f / kDriveReference : 1.0f;
        const bool normalise = drive > 1.0f && normalisesDrive(currentType_);
        const bool capGain = drive > kDriveGainKnee && capsDriveGain(currentType_);
        const float capScale = capGain ? kDriveGainKnee / drive : 1.0f;
        const bool dcBlock = needsDCBlock_;
        const bool tone = usesToneFilter(currentType_);
        const float mix = std::clamp(commonParams_.mix, 0.0f, 1.0f);
        const bool fullWet = wetWeight >= 1.0f;
        const float wetMix = mix * wetWeight;
        // Fractal, RingSaturation, Granular, HardClip and FullRectify run their span as a block
        // (FractalDistortion::process(float*, size_t), its Harmonic mode in level-major order,
        // round 4 CPU; RingSaturation::processBlock stage by stage, GranularDistortion::
        // process(float*, size_t) grain by grain and the two curves in a loop of their own, round 5
        // CPU; FormantDistortion::process(float*, size_t) stage by stage, round 6 CPU): the same
        // per-sample result. Their processRaw keeps no state shared with the steps after it
        const bool blockShaper = currentType_ == DistortionType::Fractal ||
                                 currentType_ == DistortionType::RingSaturation ||
                                 currentType_ == DistortionType::Granular ||
                                 currentType_ == DistortionType::Formant ||
                                 currentType_ == DistortionType::HardClip ||
                                 currentType_ == DistortionType::FullRectify;
        // The DC blocker and tone filter run on copies in locals for the span: their recursions keep
        // their state in registers instead of reloading it from the adapter after every output store
        // (round 6 CPU); the same process() arithmetic, written back after the span
        Krate::DSP::DCBlocker dcBlocker = dcBlocker_;
        Krate::DSP::OnePoleLP toneFilter = toneFilter_;
        std::array<float, kShaperChunk> shaped{};
        for (int start = 0; start < n; start += kShaperChunk) {
            const int len = std::min(kShaperChunk, n - start);
            if (blockShaper) {
                const auto count = static_cast<size_t>(len);
                for (int k = 0; k < len; ++k) shaped[static_cast<size_t>(k)] = in[start + k] * preGain;
                switch (currentType_) {
                    case DistortionType::Fractal: fractal_.process(shaped.data(), count); break;
                    case DistortionType::RingSaturation: ringSaturation_.processBlock(shaped.data(), count); break;
                    case DistortionType::Granular: granular_.process(shaped.data(), count); break;
                    case DistortionType::Formant: formant_.process(shaped.data(), count); break;
                    default: shapeCurveChunk(shaped.data(), count); break;
                }
            }
            for (int k = start; k < start + len; ++k) {
                const float raw = in[k];
                float x = raw;  // the dry image (processWithDry)
                if (adaa2Dry) {
                    x = adaaDryPrev_;
                } else if (adaaDry) {
                    x = 0.5f * (raw + adaaDryPrev_);
                }
                adaaDryPrev_ = raw;
                float wet = (blockShaper ? shaped[static_cast<size_t>(k - start)] : processRaw(raw * preGain)) * makeup;
                if (normalise) wet /= drive;
                if (capGain) wet *= capScale;
                if (dcBlock) wet = dcBlocker.process(wet);
                if (tone) wet = toneFilter.process(wet);
                out[k] = fullWet ? x * (1.0f - mix) + wet * mix : x * (1.0f - wetMix) + wet * wetMix;
            }
        }
        dcBlocker_ = dcBlocker;
        toneFilter_ = toneFilter;
    }
    lastOutput_ = output[numSamples - 1];
    hasOutput_ = true;
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

float DistortionAdapter::processRawFor(DistortionType type, float input) noexcept {
    switch (type) {
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

            if (adaaActive() && Krate::DSP::detail::isFinite(input)) {
                // ADAA1 at 4x (user ruling 2026-11-10: the re-staged drive aliased past T6.01's
                // -50 dB): the difference quotient of the closed-form antiderivative, the curve
                // at the midpoint for a step below the epsilon
                const double t = threshold;
                const double h = 1.0 - t;
                const double c = 1.0 + static_cast<double>(typeParams_.curve) * 5.0;
                const double x = input;
                const double prev = softClipAdaaPrev_;
                const double dx = x - prev;
                softClipAdaaPrev_ = x;
                if (std::abs(dx) > Krate::DSP::WavefolderProcessor::kAdaaEpsilon) {
                    return static_cast<float>((softClipIntegral(x, t, h, c) - softClipIntegral(prev, t, h, c)) / dx);
                }
                return static_cast<float>(softClipCurve(0.5 * (x + prev), t, h, c));
            }
            // Host rate, or NaN / Inf (passed on as before, with no history kept)
            softClipAdaaPrev_ = 0.0;

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
            // Supports bias, sag (saturation amount), and multi-stage cascading:
            // stage s runs through its own TubeStage (DC blocker and smoothers per stage)
            singleSampleBuffer_[0] = input;
            const auto stages = static_cast<std::size_t>(std::clamp(typeParams_.satStage + 1, 1, 4));
            for (std::size_t s = 0; s < stages; ++s) {
                tube_[s].process(singleSampleBuffer_, 1);
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
            return (singleSampleBuffer_[0] - fuzzCleanExcess(input)) * fuzzMakeup_;
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
            float result = (singleSampleBuffer_[0] - fuzzCleanExcess(biased) - asymBias) * fuzzMakeup_;
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
                float triFolded = wavefoldTriangle_.process(biased) * wavefoldTriangle_.getFoldAmount();
                result = result * (1.0f - typeParams_.shape) + triFolded * typeParams_.shape;
            }
            // Smooth: lowpass filter to tame harsh harmonics
            result = wavefoldSmoothFilter_.process(result);
            return result;
        }

        case DistortionType::TriangleFold: {
            // D08: Triangle wavefolder (Simple model) with bias, angle, smooth
            float biased = input + typeParams_.bias;
            // Oversampled: the fold through ADAA2, one clocked sample late (usesAdaa2)
            const bool adaa2 = adaaActive();
            float result = 0.0f;
            if (adaa2) {
                result = triangleFoldAdaa2(biased) *
                         foldMakeup(Krate::DSP::WavefolderModel::Simple, wavefolder_.getFoldAmount());
            } else {
                singleSampleBuffer_[0] = biased;
                wavefolder_.process(singleSampleBuffer_, 1);
                result = singleSampleBuffer_[0] * foldMakeup(wavefolder_.getModel(), wavefolder_.getFoldAmount());
            }
            // Angle: crossfade between triangle fold (0) and sine fold (1)
            if (typeParams_.angle > 0.001f) {
                wavefoldTriangle_.setType(Krate::DSP::WavefoldType::Sine);
                float sinFolded = wavefoldTriangle_.process(biased);
                wavefoldTriangle_.setType(Krate::DSP::WavefoldType::Triangle);
                if (adaa2) {
                    // ... the sine fold delayed the same clocked sample
                    const float delayed = triAngleDelay_;
                    triAngleDelay_ = sinFolded;
                    sinFolded = delayed;
                }
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
            float result = singleSampleBuffer_[0] * foldMakeup(wavefolder_.getModel(), wavefolder_.getFoldAmount());
            // Shape: crossfade with triangle fold
            if (typeParams_.shape > 0.001f) {
                float triFolded = wavefoldTriangle_.process(biased) * wavefoldTriangle_.getFoldAmount();
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
            float result = 0.0f;
            if (adaaActive() && Krate::DSP::detail::isFinite(input)) {
                // ADAA1 (ruling 2026-11-09): f(x) = x at and above the threshold t, else 0 (a
                // step of t); its antiderivative F(x) = (x^2 - t^2) / 2 above t, else 0, is
                // continuous. The rectified midpoint replaces an ill-conditioned quotient.
                const double t = thresh;
                const double x = input;
                const double prev = halfRectAdaaPrev_;
                const double dx = x - prev;
                const auto antiderivative = [t](double v) { return v >= t ? 0.5 * (v * v - t * t) : 0.0; };
                if (std::abs(dx) > Krate::DSP::WavefolderProcessor::kAdaaEpsilon) {
                    result = static_cast<float>((antiderivative(x) - antiderivative(prev)) / dx);
                } else {
                    const double mid = 0.5 * (x + prev);
                    result = mid >= t ? static_cast<float>(mid) : 0.0f;
                }
                halfRectAdaaPrev_ = x;
            } else {
                // Host rate, or NaN / Inf (passed on as before, with no history kept)
                halfRectAdaaPrev_ = 0.0;
                result = (input >= thresh) ? input : 0.0f;
            }
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
                bitDepth = std::clamp(bitDepth + offset, 4.0f, 16.0f);
            }

            // Set per-sample bit depth (handles jitter) and dither
            rawBitCrusher_.setBitDepth(bitDepth);
            rawBitCrusher_.setDither(
                (bitcrushDitherMode_ != 0) ? typeParams_.dither : 0.0f);

            // Drive re-stage (user ruling 2026-11-10): more input makes a fixed grid cleaner, so
            // the grid is kDriveReference coarser against the input instead (3 bits; 10 bits read
            // -41 dB THD+N at -18 dBFS, -23 dB now). The output keeps its full scale of 1.
            const float crushed = rawBitCrusher_.process(input * (1.0f / kDriveReference)) * kDriveReference;
            return std::clamp(crushed, -1.0f, 1.0f);
        }

        case DistortionType::SampleReduce:
            // D13: Sample rate reduction
            return srReducer_.process(input);

        case DistortionType::Quantize: {
            // D14: N-level uniform quantization (linear level count, distinct from Bitcrush)
            // Levels: [0,1] → N odd in [3, 63] linear (vs Bitcrush exponential 2^bits)
            // Mid-tread grid (D-010, D-143): the N levels k * step, k = -(N-1)/2 .. (N-1)/2,
            // step = 2/(N-1), are centred on 0, so 0 is a level and silence stays silent.
            const float numLevels = std::max(3.0f, 2.0f * std::round(typeParams_.quantLevels * 31.0f) + 1.0f);
            const float stepSize = 2.0f / (numLevels - 1.0f);
            const float halfSpan = (numLevels - 1.0f) * 0.5f;

            // Offset [0,1] moves every decision threshold down by up to half a step (the
            // levels stay put) for asymmetric harmonics; at most half a step keeps 0 on level 0.
            const float thresholdShift = 0.5f * typeParams_.quantOffset;
            float index = input / stepSize + thresholdShift;

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

            // Round half down (thresholds at k + 0.5 - shift), clamp to the outer levels
            index = std::ceil(index - 0.5f);
            index = std::clamp(index, -halfSpan, halfSpan);

            float quantized = index * stepSize;
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

void DistortionAdapter::shapeCurveChunk(float* x, size_t n) noexcept {
    // processRawFor's HardClip and FullRectify over n driven samples, the curve in a loop of its
    // own (no state: the samples are independent) and FullRectify's smooth filter after it
    if (currentType_ == DistortionType::HardClip) {
        const float clipThreshold = 0.1f + typeParams_.threshold * 0.9f;
        const float clipCeiling = 0.1f + typeParams_.ceiling * 0.9f;
        for (size_t k = 0; k < n; ++k) {
            const float input = x[k];
            const float sgn = input >= 0.0f ? 1.0f : -1.0f;
            x[k] = std::abs(input) <= clipThreshold ? input * (clipCeiling / clipThreshold) : sgn * clipCeiling;
        }
        return;
    }
    for (size_t k = 0; k < n; ++k) x[k] = std::abs(x[k]);
    if (typeParams_.smoothness > 0.001f) {
        for (size_t k = 0; k < n; ++k) x[k] = wavefoldSmoothFilter_.process(x[k]);
    }
}

float DistortionAdapter::fuzzCleanExcess(float fuzzInput) const noexcept {
    // FuzzProcessor blends its own input back in at (1 - fuzz amount) (its volume is never set
    // here: 0 dB). Above drive 1 that clean share is scaled by 1 / drive^2, so it falls as the drive
    // rises and more drive means a dirtier output, not a cleaner one (user ruling 2026-11-10;
    // 1 / drive held it level and the THD+N flat); this is the part removed.
    const float drive = commonParams_.drive;
    if (drive <= 1.0f) return 0.0f;
    return (1.0f - 1.0f / (drive * drive)) * (1.0f - fuzz_.getFuzz()) * fuzzInput;
}

void DistortionAdapter::resetTriangleFoldAdaa2() noexcept {
    triFoldAmountSmoother_.snapToTarget();
    triFoldSymmetrySmoother_.snapToTarget();
    triFoldDcBlocker_.reset();
    triAdaaX1_ = 0.0;
    triAdaaX2_ = 0.0;
    triAdaaF2x1_ = 0.0;
    triAdaaD1_ = 0.0;
    triAdaaT_ = -1.0;
    triAngleDelay_ = 0.0f;
}

float DistortionAdapter::triangleFoldAdaa2(float input) noexcept {
    // WavefolderProcessor's Simple model: symmetry as an offset scaled by 1 / fold amount, the
    // fold at threshold 1 / fold amount, its 10 Hz DC blocker (mix 1)
    const float foldAmount = triFoldAmountSmoother_.process();
    const float symmetry = triFoldSymmetrySmoother_.process();
    const float u = input + symmetry * (1.0f / foldAmount);
    const double t = std::max(static_cast<double>(Krate::DSP::WavefoldMath::kMinThreshold),
                              1.0 / static_cast<double>(foldAmount));
    double y = 0.0;
    if (!Krate::DSP::detail::isFinite(u)) {
        // NaN / Inf: the plain fold's answer, and no history to carry it forward
        triAdaaX1_ = 0.0;
        triAdaaX2_ = 0.0;
        triAdaaF2x1_ = 0.0;
        triAdaaD1_ = 0.0;
        triAdaaT_ = -1.0;
        y = Krate::DSP::WavefoldMath::triangleFold(u, static_cast<float>(t));
    } else {
        if (t != triAdaaT_) {
            // The threshold moved (fold amount smoothing): the cached F2(x[n-1]) and the last
            // difference quotient are re-derived at it, so every quotient reads one function
            triAdaaT_ = t;
            triAdaaF2x1_ = triangleFoldF2(triAdaaX1_, t);
            const double d12 = triAdaaX1_ - triAdaaX2_;
            triAdaaD1_ = std::abs(d12) > kTriangleAdaa2Epsilon
                             ? (triAdaaF2x1_ - triangleFoldF2(triAdaaX2_, t)) / d12
                             : triangleFoldF1(0.5 * (triAdaaX1_ + triAdaaX2_), t);
        }
        // Second-order ADAA (Bilbao, Esqueda, Parker, Valimaki 2017; core/closed_form_adaa2.h):
        // the band-limited fold of x[n-1]
        const double x = u;
        const double f2x = triangleFoldF2(x, t);
        const double dx1 = x - triAdaaX1_;
        const double d0 = std::abs(dx1) > kTriangleAdaa2Epsilon ? (f2x - triAdaaF2x1_) / dx1
                                                                : triangleFoldF1(0.5 * (x + triAdaaX1_), t);
        const double dx2 = x - triAdaaX2_;
        if (std::abs(dx2) > kTriangleAdaa2Epsilon) {
            y = 2.0 * (d0 - triAdaaD1_) / dx2;
        } else {
            // x[n] ~ x[n-2]: the limit around their mean (ill-conditioned quotient)
            const double xBar = 0.5 * (x + triAdaaX2_);
            const double delta = xBar - triAdaaX1_;
            if (std::abs(delta) > kTriangleAdaa2Epsilon) {
                y = (2.0 / delta) * (triangleFoldF1(xBar, t) + (triAdaaF2x1_ - triangleFoldF2(xBar, t)) / delta);
            } else {
                y = Krate::DSP::WavefoldMath::triangleFold(static_cast<float>(0.5 * (xBar + triAdaaX1_)),
                                                           static_cast<float>(t));
            }
        }
        triAdaaX2_ = triAdaaX1_;
        triAdaaX1_ = x;
        triAdaaD1_ = d0;
        triAdaaF2x1_ = f2x;
    }
    return triFoldDcBlocker_.process(static_cast<float>(y));
}

void DistortionAdapter::updateDCBlockerState() noexcept {
    // DC blocking required for asymmetric types that introduce DC offset.
    // Tube (asymmetric curve + bias), Stochastic (jitter/drift offset around the signal)
    // and Temporal on its Diode / Tube curves (indices 7 and 8) leave DC on the band
    // output (D-075, D-142). Quantize's Offset moves its thresholds asymmetrically (D-010).
    // The folds (bias / symmetry), Chaos and Granular left up to 0.16 DC on factory
    // presets (D-144).
    const int temporalCurve = temporalShapeIndex(typeParams_.dynamicCurve);
    needsDCBlock_ = (currentType_ == DistortionType::AsymmetricFuzz ||
                     currentType_ == DistortionType::SineFold ||
                     currentType_ == DistortionType::TriangleFold ||
                     currentType_ == DistortionType::SergeFold ||
                     currentType_ == DistortionType::Chaos ||
                     currentType_ == DistortionType::Granular ||
                     currentType_ == DistortionType::FullRectify ||
                     currentType_ == DistortionType::HalfRectify ||
                     currentType_ == DistortionType::FeedbackDist ||
                     currentType_ == DistortionType::Tube ||
                     currentType_ == DistortionType::Stochastic ||
                     currentType_ == DistortionType::Quantize ||
                     (currentType_ == DistortionType::Temporal &&
                      (temporalCurve == 7 || temporalCurve == 8)));
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
        case DistortionType::SoftClip: // NOLINT(bugprone-branch-clone): intentionally same as HardClip — both use params directly in processRaw()
            // Curve and knee are used directly in processRaw()
            break;

        case DistortionType::HardClip:
            // Threshold and ceiling are used directly in processRaw()
            break;

        case DistortionType::Tube:
            for (auto& stage : tube_) {
                stage.setBias(p.bias);
                stage.setSaturationAmount(p.sag);
            }
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
            // DistortionParams::bias is bipolar [-1, 1] for every type (node Bias, bias
            // modulation); FuzzProcessor's is unipolar [0, 1], 1 = centred (D-082).
            fuzz_.setBias((p.bias + 1.0f) * 0.5f);
            fuzz_.setFuzz(p.sustain);
            fuzz_.setOctaveUp(p.octave >= 0.5f);
            // The node Tone (adapter tone filter) is the only tone control: open the
            // processor's own tone biquad fully (8 kHz) instead of its 1.8 kHz default (D-076).
            fuzz_.setTone(1.0f);
            break;

        case DistortionType::AsymmetricFuzz:
            // Note: FuzzType is set per-sample in processRaw() (hardcoded Silicon).
            // Setting it here would pre-empt the crossfade that processRaw() triggers
            // on first use, changing audio behavior. Leave type to processRaw().
            fuzz_.setBias((p.bias + 1.0f) * 0.5f);  // bipolar -> unipolar, as Fuzz (D-082)
            fuzz_.setFuzz(p.sustain);
            fuzz_.setTone(1.0f);  // tone biquad fully open, as Fuzz (D-076)
            // Body: map [0,1] to cutoff [20000, 400] Hz (exponential)
            // body=0 → 20kHz (fully open), body=1 → 400Hz (dark)
            bodyFilter_.setCutoff(20000.0f * std::pow(400.0f / 20000.0f, p.body));
            break;

        // =================================================================
        // Wavefold (D07-D09)
        // =================================================================
        case DistortionType::SineFold:
            wavefolder_.setModel(Krate::DSP::WavefolderModel::Serge);
            wavefolder_.setAntiderivativeAntialiasing(false);
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
            // Oversampled, the fold runs through the adapter's ADAA2 (triangleFoldAdaa2, user ruling
            // 2026-11-11, round 3; ADAA1 here before); the processor folds at the host rate
            wavefolder_.setAntiderivativeAntialiasing(false);
            wavefolder_.setFoldAmount(p.folds);
            wavefolder_.setSymmetry(p.symmetry);
            triFoldAmountSmoother_.setTarget(wavefolder_.getFoldAmount());
            triFoldSymmetrySmoother_.setTarget(wavefolder_.getSymmetry());
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
            wavefolder_.setAntiderivativeAntialiasing(false);
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
            int op = std::clamp(p.bitwiseOp, 0, 3);
            bitwiseMangler_.setOperation(static_cast<Krate::DSP::BitwiseOperation>(op));
            bitwiseMangler_.setIntensity(p.bitwiseIntensity);
            bitwiseMangler_.setPattern(static_cast<uint32_t>(p.bitwisePattern * 16777215.0f));
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
            const int shapeIndex = temporalShapeIndex(p.dynamicCurve);
            temporal_.setWaveshapeType(
                static_cast<Krate::DSP::WaveshapeType>(shapeIndex));
            // The curve decides whether the output carries DC (Diode / Tube)
            updateDCBlockerState();

            // Hold (Slot 7) → hysteresis decay time [1, 500] ms
            float decayMs = temporalHoldMs(p.hold);
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
            const int curveIdx = waveshapeCurveIndex(p.rsCurve);
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
                float freqHz = ringSatCarrierHz(p.rsCarrierFreq);
                ringSaturation_.setCarrierFrequency(freqHz);
            } else if (freqMode == 1) {
                // Harmonic: [0,1] → ratio 1-16
                float ratio = ringSatHarmonicRatio(p.rsCarrierFreq);
                ringSaturation_.setHarmonicRatio(ratio);
            }
            // Track/Random: frequency is auto-determined, no param needed
            break;
        }

        case DistortionType::FeedbackDist: {
            feedbackDist_.setFeedback(p.feedback);
            feedbackDist_.setDelayTime(p.delayMs);
            // Curve → loop waveshape (9 discrete types), mapped like RingSaturation's curve (D-078)
            const int curveIdx = waveshapeCurveIndex(p.fbCurve);
            feedbackDist_.setSaturationCurve(static_cast<Krate::DSP::WaveshapeType>(curveIdx));
            // Lim off bypasses the limiter; Thr [0, 1] → [-24, 0] dB, higher = looser (D-078)
            feedbackDist_.setLimiterEnabled(p.limiter);
            feedbackDist_.setLimiterThreshold(-24.0f + p.limThreshold * 24.0f);
            feedbackDist_.setToneFrequency(20.0f + p.filterFreq * 19980.0f);
            feedbackDist_.setFilterType(p.filterType);
            feedbackDist_.setStages(p.stages);
            feedbackDist_.setDecay(p.fbDecay);
            break;
        }

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

            // Damping. AllpassSaturator maps it to a loop lowpass cutoff from 0.45 x its rate down
            // to 100 Hz, so the cutoff follows the clocked rate: at 8x Allpass Shimmer's loop
            // (damping 0.25: 15.9 kHz at 4x, 26.8 kHz at 8x) sustained itself, its -58 dBFS input
            // coming out at -12.6 dBFS (round 3). Above 4x the host rate, the rate the type was
            // voiced at, the damping is remapped to give the cutoff it gives there, in Hz. Karplus
            // Strong scales its own cutoff with it, rate-independent: unchanged.
            {
                float damping = p.allpassDamp;
                const double voicedRate = hostRate_ * kAllpassVoicedFactor;
                if (sampleRate_ > voicedRate && topo != static_cast<int>(Krate::DSP::NetworkTopology::KarplusStrong)) {
                    constexpr double kMinCutoff = 100.0;  // AllpassSaturator::setDamping's floor
                    const double maxNow = 0.45 * sampleRate_;
                    const double maxVoiced = 0.45 * voicedRate;
                    const double cutoff = maxVoiced * std::pow(kMinCutoff / maxVoiced, static_cast<double>(damping));
                    damping = static_cast<float>(std::log(cutoff / maxNow) / std::log(kMinCutoff / maxNow));
                }
                allpassSaturator_.setDamping(damping);
            }
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
            granular_.setPosition(p.grainPos);
            granular_.setCurve(p.grainCurve);
            granular_.setEnvelopeType(static_cast<Krate::DSP::GrainEnvelopeType>(
                std::clamp(p.grainEnvType, 0, 3)));
            // The adapter already applies commonParams_.drive to the input before
            // processRaw(); neutralise the processor's internal drive (default 5.0) so
            // the drive is not applied twice (D-079, as Fractal below).
            granular_.setDrive(1.0f);
            break;

        case DistortionType::Spectral: {
            // Slot 0: Mode (4 processing algorithms)
            int mode = std::clamp(p.spectralMode, 0, 3);
            spectral_.setMode(static_cast<Krate::DSP::SpectralDistortionMode>(mode));

            // Slot 1: FFT size — RT-safe switch inside the buffers prepare() allocated
            spectral_.setActiveFftSize(static_cast<std::size_t>(p.fftSize));

            // Slot 2: Curve — map continuous [0,1] to WaveshapeType enum (9 types: 0-8)
            const int curveIdx = waveshapeCurveIndex(p.spectralCurve);
            spectral_.setSaturationCurve(static_cast<Krate::DSP::WaveshapeType>(curveIdx));

            // Slot 3: Tilt — spectral tilt [0,1] (0=dark, 0.5=neutral, 1=bright)
            spectral_.setTilt(p.spectralTilt);

            // Slot 4: Threshold — spectral gate [0,1]
            spectral_.setThreshold(p.spectralThreshold);

            // Slot 5: Magnitude scale mode (4 modes: Linear, Log, Bark, ERB)
            int magMode = std::clamp(p.spectralMagMode, 0, 3);
            spectral_.setMagnitudeScaleMode(
                static_cast<Krate::DSP::MagnitudeScaleMode>(magMode));

            // Slot 6: Frequency — pivot/focus frequency [0,1]
            spectral_.setFrequency(p.spectralFreq);

            // Slot 7: Phase mode (4 modes: Preserve, Random, Gradient, Temporal)
            int phaseMode = std::clamp(p.spectralPhase, 0, 3);
            spectral_.setPhaseMode(
                static_cast<Krate::DSP::SpectralPhaseMode>(phaseMode));

            // Common parameter: BitDepth → magnitude quantization for Bitcrush mode
            spectral_.setMagnitudeBits(p.bitDepth);
            break;
        }

        case DistortionType::Fractal: {
            int mode = std::clamp(p.fractalMode, 0, 4);
            fractal_.setMode(static_cast<Krate::DSP::FractalMode>(mode));
            fractal_.setIterations(p.iterations);
            fractal_.setScaleFactor(p.scaleFactor);
            fractal_.setFrequencyDecay(p.frequencyDecay);
            fractal_.setFeedbackAmount(p.fractalFB);
            // Curve controls saturation waveshaper type across all modes
            auto curveType = static_cast<Krate::DSP::WaveshapeType>(
                waveshapeCurveIndex(p.fractalCurve));
            fractal_.setCurveType(curveType);
            // Blend mode controls how iteration levels are combined
            int blendIdx = std::clamp(p.fractalBlend, 0, 3);
            fractal_.setBlendMode(
                static_cast<Krate::DSP::FractalBlendMode>(blendIdx));
            // Depth controls band iteration scale (multiband mode)
            fractal_.setBandIterationScale(p.fractalDepth);
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
            // Drive is handled internally (not pre-scaled) so jitter/coeff noise
            // operate on the normalized signal before amplification
            stochastic_.setDrive(commonParams_.drive);
            stochastic_.setJitterAmount(p.jitterAmount);
            stochastic_.setJitterRate(p.jitterRate);
            stochastic_.setCoefficientNoise(p.coefficientNoise);
            stochastic_.setDrift(p.stochasticDrift);
            stochastic_.setCorrelationMode(p.stochasticCorr);
            stochastic_.setOutputSmooth(p.stochasticSmooth);
            stochastic_.setNoiseColor(p.stochasticNoiseColor);
            break;
        }

        default:
            break;
    }
}

} // namespace Disrumpo
