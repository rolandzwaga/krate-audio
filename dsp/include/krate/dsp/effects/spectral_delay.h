// ==============================================================================
// Layer 4: User Feature - SpectralDelay
// ==============================================================================
// Applies delay to individual frequency bands using STFT analysis/resynthesis.
// Creates ethereal, frequency-dependent echo effects where different frequency
// bands can have different delay times.
//
// Composes:
// - STFT, OverlapAdd (Layer 1): Spectral analysis/resynthesis
// - SpectralBuffer (Layer 1): Spectrum storage
// - DelayLine (Layer 1): Per-bin delay lines
// - OnePoleSmoother (Layer 1): Parameter smoothing
//
// Feature: 033-spectral-delay
// Layer: 4 (User Feature)
// Reference: specs/033-spectral-delay/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/systems/delay_engine.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>


namespace Krate::DSP {

// =============================================================================
// SpreadDirection - Delay time distribution modes
// =============================================================================

/// @brief Spread direction modes for delay time distribution across frequency bins
enum class SpreadDirection : std::uint8_t {
    LowToHigh,  ///< Higher bins get longer delays (rising effect)
    HighToLow,  ///< Lower bins get longer delays (falling effect)
    CenterOut   ///< Edge bins get longer delays, center is base delay
};

/// @brief Spread curve modes for delay time distribution scaling
/// Both lay the spread over octaves (20 Hz to 20 kHz); see calculateBinDelayMs
enum class SpreadCurve : std::uint8_t {
    Linear,      ///< Equal share of the spread per octave
    Logarithmic  ///< Low octaves get most of the spread (ln(1 + 9p) / ln(10))
};

// =============================================================================
// SpectralDelay - Layer 4 User Feature
// =============================================================================

/// @brief Spectral delay effect using per-bin delay lines
///
/// Applies independent delay times to each frequency bin, creating unique
/// frequency-dependent echo effects. Features include:
/// - Configurable FFT size (512-4096)
/// - Per-bin delay with spread control
/// - Spectral freeze mode
/// - Frequency-dependent feedback with tilt
/// - Spectral diffusion/blur
///
/// @note Latency is FFT size + hop samples (getLatencySamples()) at any buffer size
class SpectralDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr std::size_t kMinFFTSize = 512;
    static constexpr std::size_t kMaxFFTSize = 4096;
    static constexpr std::size_t kDefaultFFTSize = 1024;

    static constexpr float kMinDelayMs = 0.0f;
    static constexpr float kMaxDelayMs = 2000.0f;  ///< Default maximum (prepare()'s maxDelayMs)
    static constexpr float kDefaultDelayMs = 250.0f;

    static constexpr float kMinSpreadMs = 0.0f;
    static constexpr float kMaxSpreadMs = 2000.0f;

    static constexpr float kMinFeedback = 0.0f;
    static constexpr float kMaxFeedback = 1.2f;  // Allow slight overdrive
    /// Soft-limit scale of the fed-back frame's RMS (time-domain units), the RMS
    /// of a full-scale sine: the loop settles near 0.66x this at kMaxFeedback.
    static constexpr float kFeedbackLimitRms = 0.70710678f;

    static constexpr float kMinTilt = -1.0f;
    static constexpr float kMaxTilt = 1.0f;

    static constexpr float kMinDiffusion = 0.0f;
    static constexpr float kMaxDiffusion = 1.0f;

    static constexpr float kMinDryWet = 0.0f;
    static constexpr float kMaxDryWet = 1.0f;
    static constexpr float kDefaultDryWet = 0.5f;

    // Phase 3.2: Stereo width/decorrelation
    static constexpr float kMinStereoWidth = 0.0f;
    static constexpr float kMaxStereoWidth = 1.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    SpectralDelay() noexcept = default;
    ~SpectralDelay() = default;

    // Non-copyable, movable
    SpectralDelay(const SpectralDelay&) = delete;
    SpectralDelay& operator=(const SpectralDelay&) = delete;
    SpectralDelay(SpectralDelay&&) noexcept = default;
    SpectralDelay& operator=(SpectralDelay&&) noexcept = default;

    /// @brief Prepare for processing at given sample rate
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxDelayMs Longest per-bin delay the lines hold; setBaseDelayMs() and the synced
    ///        time stop there (default kMaxDelayMs)
    void prepare(double sampleRate, std::size_t maxBlockSize,
                 float maxDelayMs = kMaxDelayMs) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        maxDelayMs_ = maxDelayMs;

        // Storage is allocated ONCE for every supported FFT size, so a size
        // requested while running (setFFTSize) is switched to inside process()
        // without allocating: the STFT / overlap-add hold the largest size plus
        // a transform for each smaller one, the spectra hold the largest bin
        // count, and each per-bin delay line holds the frames of the smallest
        // FFT size that has that bin (the highest frame rate it can run at).
        //
        // sqrt-Hann at analysis AND synthesis (R-195). Fractional per-bin reads
        // and recirculated feedback leave a frame whose IFFT no longer tapers to
        // zero; with Hann analysis only, every hop boundary of the output then
        // stepped. The synthesis window tapers each modified frame, and an
        // unmodified one still reconstructs exactly (sqrt-Hann^2 = Hann, COLA
        // at 50 % overlap; OverlapAdd's normalisation is then 1).
        stftL_.prepare(kMaxFFTSize, kMaxFFTSize / 2, WindowType::SqrtHann);
        stftR_.prepare(kMaxFFTSize, kMaxFFTSize / 2, WindowType::SqrtHann);
        stftL_.prepareActiveSizes(kMinFFTSize);
        stftR_.prepareActiveSizes(kMinFFTSize);
        overlapAddL_.prepare(kMaxFFTSize, kMaxFFTSize / 2, WindowType::SqrtHann,
                             kDefaultKaiserBeta, /*applySynthesisWindow=*/true);
        overlapAddR_.prepare(kMaxFFTSize, kMaxFFTSize / 2, WindowType::SqrtHann,
                             kDefaultKaiserBeta, /*applySynthesisWindow=*/true);
        overlapAddL_.prepareActiveSizes(kMinFFTSize);
        overlapAddR_.prepareActiveSizes(kMinFFTSize);

        for (SpectralBuffer* s : {&inputSpectrumL_, &inputSpectrumR_, &outputSpectrumL_,
                                  &outputSpectrumR_, &frozenSpectrumL_, &frozenSpectrumR_}) {
            s->prepare(kMaxFFTSize);
        }

        // Prepare complex delay lines (real + imaginary parts) at capacity
        constexpr std::size_t kMaxBins = kMaxFFTSize / 2 + 1;
        binRealDelaysL_.clear();
        binRealDelaysR_.clear();
        binImagDelaysL_.clear();
        binImagDelaysR_.clear();
        binRealDelaysL_.reserve(kMaxBins);
        binRealDelaysR_.reserve(kMaxBins);
        binImagDelaysL_.reserve(kMaxBins);
        binImagDelaysR_.reserve(kMaxBins);

        for (std::size_t i = 0; i < kMaxBins; ++i) {
            std::size_t smallestSize = kMinFFTSize;
            while (smallestSize / 2 + 1 <= i) smallestSize <<= 1;
            const std::size_t smallestHop = smallestSize / 2;
            const double capacityFrameRate = sampleRate / static_cast<double>(smallestHop);
            for (auto* lines : {&binRealDelaysL_, &binRealDelaysR_, &binImagDelaysL_, &binImagDelaysR_}) {
                lines->emplace_back();
                lines->back().prepare(capacityFrameRate, maxDelayMs_ / 1000.0f);
            }
        }

        // Allocate temp buffers
        tempBufferL_.resize(maxBlockSize, 0.0f);
        tempBufferR_.resize(maxBlockSize, 0.0f);
        dryBufferL_.resize(maxBlockSize, 0.0f);
        dryBufferR_.resize(maxBlockSize, 0.0f);
        // Wet FIFO: the fftSize latency plus one hop plus one block at most
        wetFifoL_.assign(2 * kMaxFFTSize + maxBlockSize, 0.0f);
        wetFifoR_.assign(2 * kMaxFFTSize + maxBlockSize, 0.0f);
        blurredMag_.assign(kMaxBins, 0.0f);
        binDelayFrames_.assign(kMaxBins, 0.0f);
        peaks_.assign(kMaxBins, 0);
        regionPeak_.assign(kMaxBins, 0);
        for (DelayedSpectrum* s : {&delayedL_, &delayedR_}) {
            s->magNew.assign(kMaxBins, 0.0f);
            s->advance.assign(kMaxBins, 0.0f);
            s->mag.assign(kMaxBins, 0.0f);
            s->phase.assign(kMaxBins, 0.0f);
        }
        stereoPhaseL_.assign(kMaxBins, 0.0f);
        stereoPhaseR_.assign(kMaxBins, 0.0f);

        // Activate the requested size inside that storage
        applyFFTSize(requestedFFTSize_);
        resetWetFifo();
        wetSwitchGain_ = 1.0f;
        wetSwitchStep_ = 1.0f / std::max(1.0f, kFFTSwitchFadeMs * 0.001f
                                                   * static_cast<float>(sampleRate));

        // Initialize smoothers to current values
        baseDelaySmoother_.setTarget(baseDelayMs_);
        spreadSmoother_.setTarget(spreadMs_);
        feedbackSmoother_.setTarget(feedback_);
        tiltSmoother_.setTarget(feedbackTilt_);
        diffusionSmoother_.setTarget(diffusion_);
        dryWetSmoother_.setTarget(dryWetMix_);
        stereoWidthSmoother_.setTarget(stereoWidth_);

        // Snap smoothers to initial values
        snapParameters();

        // Seed the RNG from rngSeed_ (a constant unless seedRng() set one), never from
        // the object's address: the same settings must render the same audio on
        // every instance and every run (offline bounce, freeze, A/B).
        rng_.seed(rngSeed_);

        // Phase buffers for stereo decorrelation (random walk approach) provide
        // smooth phase modulation without frame-boundary clicks. Initialize the
        // active bins with random values so each instance starts with unique
        // phase offsets.
        for (std::size_t i = 0; i < activeNumBins(); ++i) {
            // Start with random phase offsets in ±π/2. nextUnipolar() is [0, 1]; nextFloat() is
            // [-1, 1], which made this [-3π/2, π/2] and the walk below drift both channels together.
            stereoPhaseL_[i] = (rng_.nextUnipolar() - 0.5f) * kPi;
            stereoPhaseR_[i] = (rng_.nextUnipolar() - 0.5f) * kPi;
        }

        prepared_ = true;
    }

    /// @brief Reset all internal state (delay lines, STFT buffers)
    /// @note Applies a pending setFFTSize() request at once: everything it would
    ///       clear is cleared here anyway, so no fade is needed.
    void reset() noexcept {
        startIncrementalReset();
        std::size_t unlimited = SIZE_MAX;
        (void)continueIncrementalReset(unlimited);
    }

    /// @brief Start a reset() whose per-bin delay-line clear is left to
    ///        continueIncrementalReset(), so it fits a real-time callback (the
    ///        lines hold up to 5 s of frames per bin). Do not process until
    ///        that has returned true.
    /// @note A pending setFFTSize() is still applied here, and re-preparing the
    ///       lines it activates clears them in this call.
    void startIncrementalReset() noexcept {
        if (prepared_ && requestedFFTSize_ != fftSize_) {
            applyFFTSize(requestedFFTSize_);
        }
        wetSwitchGain_ = 1.0f;

        // Reset STFT
        stftL_.reset();
        stftR_.reset();

        // Reset overlap-add
        overlapAddL_.reset();
        overlapAddR_.reset();

        // Reset spectral buffers
        inputSpectrumL_.reset();
        inputSpectrumR_.reset();
        outputSpectrumL_.reset();
        outputSpectrumR_.reset();
        frozenSpectrumL_.reset();
        frozenSpectrumR_.reset();

        // Reset the active bins' complex delay lines (real + imaginary parts);
        // continueIncrementalReset() clears them. The lines above the active
        // bin count are not read, and applyFFTSize() clears each one it activates.
        const std::size_t numBins = std::min(activeNumBins(), binRealDelaysL_.size());
        for (std::size_t i = 0; i < numBins; ++i) {
            binRealDelaysL_[i].startIncrementalReset();
            binRealDelaysR_[i].startIncrementalReset();
            binImagDelaysL_[i].startIncrementalReset();
            binImagDelaysR_[i].startIncrementalReset();
        }
        resetLineBin_ = 0;

        // Reset freeze state
        wasFrozen_ = false;
        freezeCrossfade_ = 0.0f;

        // Re-seed, then re-draw the stereo phase state (random walk values), so a
        // reset restores the starting phases prepare() or seedRng() drew instead of
        // drawing new ones from the running stream.
        rng_.seed(rngSeed_);
        for (std::size_t i = 0; i < std::min(activeNumBins(), stereoPhaseL_.size()); ++i) {
            stereoPhaseL_[i] = (rng_.nextUnipolar() - 0.5f) * kPi;
            stereoPhaseR_[i] = (rng_.nextUnipolar() - 0.5f) * kPi;
        }

        // Clear temp buffers
        std::fill(tempBufferL_.begin(), tempBufferL_.end(), 0.0f);
        std::fill(tempBufferR_.begin(), tempBufferR_.end(), 0.0f);
        std::fill(dryBufferL_.begin(), dryBufferL_.end(), 0.0f);
        std::fill(dryBufferR_.begin(), dryBufferR_.end(), 0.0f);
        resetWetFifo();
    }

    /// @brief Clear up to @p budget more delay-line samples (deducted from it).
    /// @return true once the started reset is complete.
    bool continueIncrementalReset(std::size_t& budget) noexcept {
        const std::size_t numBins = std::min(activeNumBins(), binRealDelaysL_.size());
        for (; resetLineBin_ < numBins; ++resetLineBin_) {
            const std::size_t i = resetLineBin_;
            if (!binRealDelaysL_[i].continueIncrementalReset(budget) ||
                !binRealDelaysR_[i].continueIncrementalReset(budget) ||
                !binImagDelaysL_[i].continueIncrementalReset(budget) ||
                !binImagDelaysR_[i].continueIncrementalReset(budget)) {
                return false;
            }
        }
        return true;
    }

    /// @brief Seed the internal RNG (replaces the constant default seed)
    /// @param seed The seed value to use; prepare() and reset() re-seed from it too
    /// @note After seeding, call reset() to reinitialize phase buffers.
    void seedRng(uint32_t seed) noexcept {
        rngSeed_ = seed;
        rng_.seed(seed);
        // Reinitialize stereo phase buffers with deterministic values
        for (std::size_t i = 0; i < std::min(activeNumBins(), stereoPhaseL_.size()); ++i) {
            stereoPhaseL_[i] = (rng_.nextUnipolar() - 0.5f) * kPi;
            stereoPhaseR_[i] = (rng_.nextUnipolar() - 0.5f) * kPi;
        }
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio block
    /// @param left Left channel buffer (in-place)
    /// @param right Right channel buffer (in-place)
    /// @param numSamples Number of samples to process
    /// @param ctx Block processing context
    void process(float* left, float* right, std::size_t numSamples,
                 const BlockContext& ctx) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0) {
            return;
        }

        // A requested FFT size is switched to once the wet output has faded out
        // (see applyWetSwitchFade). The switch clears every frame and delay line,
        // so the new size's output starts from silence and rises with its first
        // windowed frame.
        if (requestedFFTSize_ != fftSize_ && wetSwitchGain_ <= 0.0f) {
            reset();
        }

        // Update base delay from tempo if in synced mode (spec 041, FR-003)
        if (timeMode_ == TimeMode::Synced) {
            // Get tempo with fallback to 120 BPM if unavailable (FR-007)
            double tempo = ctx.tempoBPM;
            if (tempo <= 0.0) {
                tempo = 120.0;  // Fallback default
            }

            // Calculate base delay from note value and tempo
            float syncedMs = dropdownToDelayMs(noteValueIndex_, tempo);

            // Clamp to max delay buffer (FR-006)
            syncedMs = std::clamp(syncedMs, kMinDelayMs, maxDelayMs_);

            // Update smoother target for smooth transitions - unless a snap is
            // pending: snapParameters() cannot know the host tempo, so a snap
            // in Synced mode lands here, on the first block that carries it.
            if (syncSnapPending_) {
                baseDelaySmoother_.snapTo(syncedMs);
            } else {
                baseDelaySmoother_.setTarget(syncedMs);
            }
        }
        // In Free mode (FR-004), base delay is set via setBaseDelayMs() - no change needed
        // (and the snap, if any, already happened in snapParameters())
        syncSnapPending_ = false;

        // Store dry signal for mixing
        std::copy(left, left + numSamples, dryBufferL_.begin());
        std::copy(right, right + numSamples, dryBufferR_.begin());

        // Push samples into STFT analyzers
        stftL_.pushSamples(left, numSamples);
        stftR_.pushSamples(right, numSamples);

        // Process spectral frames, DRAINING THE ACCUMULATOR AFTER EVERY FRAME.
        //
        // OverlapAdd::synthesize accumulates the new frame at the read position
        // (primitives/stft.h:375-401), so a frame lands one hop after the last
        // one only if that one's whole hop was pulled first. Synthesizing onto
        // an undrained accumulator (a) stacks the frame early and (b) lets
        // samplesReady_ claim more samples than outputBuffer_ (2 * fftSize)
        // physically holds. Every synthesized hop is therefore moved whole into
        // the wet FIFO, which is the contract every other spectral component in
        // this library observes: processors/spectral_gate.h:319-330,
        // processors/spectral_distortion.h:293-305,
        // processors/spectral_morph_filter.h:290-307 and
        // effects/aether_reverb.h:4062-4093.
        while (stftL_.canAnalyze() && stftR_.canAnalyze()) {
            // Analyze
            stftL_.analyze(inputSpectrumL_);
            stftR_.analyze(inputSpectrumR_);

            // Process the spectral frame
            processSpectralFrame(inputSpectrumL_, inputSpectrumR_,
                                 outputSpectrumL_, outputSpectrumR_);

            // Synthesize
            overlapAddL_.synthesize(outputSpectrumL_);
            overlapAddR_.synthesize(outputSpectrumR_);

            drainHopIntoWetFifo();
        }

        // The wet stream leaves the FIFO exactly fftSize samples after its
        // input, whatever lengths the caller cuts its buffers into: the FIFO
        // starts with fftSize samples of silence and the frame holding a
        // sample is always complete fftSize - 1 samples after it (R-150).
        // Pulled straight from the accumulator, the wet stream started at the
        // start of the call in which the first frame completed, so its delay
        // was fftSize minus up to one buffer and depended on the buffer size,
        // and a call that found too little left a gap of silence.
        popWetFifo(numSamples);

        if (requestedFFTSize_ != fftSize_ || wetSwitchGain_ < 1.0f) {
            applyWetSwitchFade(numSamples, numSamples);
        }

        // Apply dry/wet mix (smoothed per sample)
        for (std::size_t i = 0; i < numSamples; ++i) {
            const float wetMix = dryWetSmoother_.process();
            const float dryMix = 1.0f - wetMix;
            left[i] = dryBufferL_[i] * dryMix + tempBufferL_[i] * wetMix;
            right[i] = dryBufferR_[i] * dryMix + tempBufferR_[i] * wetMix;
        }
    }

    // =========================================================================
    // FFT Configuration
    // =========================================================================

    /// @brief Request an FFT size (512, 1024, 2048 or 4096; others round up to a
    ///        power of two within that range)
    /// @note Real-time safe. Before prepare() the size takes effect at once. On a
    ///       prepared delay, process() fades the wet output out over
    ///       kFFTSwitchFadeMs, then switches to the new size inside the storage
    ///       prepare() allocated and clears the spectral state (the echoes
    ///       restart); reset() switches at once. Until then getFFTSize() and
    ///       getLatencySamples() report the size in use.
    void setFFTSize(std::size_t fftSize) noexcept {
        const std::size_t clamped = std::clamp(fftSize, kMinFFTSize, kMaxFFTSize);
        // Round to nearest power of 2
        std::size_t pow2 = 1;
        while (pow2 < clamped) pow2 <<= 1;
        requestedFFTSize_ = pow2;
        if (!prepared_) {
            fftSize_ = pow2;
            hopSize_ = pow2 / 2;
        }
    }

    /// @brief Get the FFT size in use
    [[nodiscard]] std::size_t getFFTSize() const noexcept { return fftSize_; }

    // =========================================================================
    // Delay Controls
    // =========================================================================

    /// @brief Set base delay time in milliseconds
    void setBaseDelayMs(float ms) noexcept {
        baseDelayMs_ = std::clamp(ms, kMinDelayMs, maxDelayMs_);
        baseDelaySmoother_.setTarget(baseDelayMs_);
    }
    [[nodiscard]] float getBaseDelayMs() const noexcept { return baseDelayMs_; }

    /// @brief Set spread amount in milliseconds
    void setSpreadMs(float ms) noexcept {
        spreadMs_ = std::clamp(ms, kMinSpreadMs, kMaxSpreadMs);
        spreadSmoother_.setTarget(spreadMs_);
    }
    [[nodiscard]] float getSpreadMs() const noexcept { return spreadMs_; }

    /// @brief Set spread direction mode
    void setSpreadDirection(SpreadDirection dir) noexcept {
        spreadDirection_ = dir;
    }
    [[nodiscard]] SpreadDirection getSpreadDirection() const noexcept {
        return spreadDirection_;
    }

    /// @brief Set spread curve mode (Linear or Logarithmic)
    /// Linear: equal share per octave; Logarithmic: the low octaves get most of it
    void setSpreadCurve(SpreadCurve curve) noexcept {
        spreadCurve_ = curve;
    }
    [[nodiscard]] SpreadCurve getSpreadCurve() const noexcept {
        return spreadCurve_;
    }

    // =========================================================================
    // Feedback Controls
    // =========================================================================

    /// @brief Set global feedback amount (0.0 to 1.2)
    void setFeedback(float amount) noexcept {
        feedback_ = std::clamp(amount, kMinFeedback, kMaxFeedback);
        feedbackSmoother_.setTarget(feedback_);
    }
    [[nodiscard]] float getFeedback() const noexcept { return feedback_; }

    /// @brief Set feedback tilt (-1.0 to +1.0)
    /// Negative = more low-frequency feedback, Positive = more high-frequency
    void setFeedbackTilt(float tilt) noexcept {
        feedbackTilt_ = std::clamp(tilt, kMinTilt, kMaxTilt);
        tiltSmoother_.setTarget(feedbackTilt_);
    }
    [[nodiscard]] float getFeedbackTilt() const noexcept { return feedbackTilt_; }

    // =========================================================================
    // Freeze
    // =========================================================================

    /// @brief Enable/disable spectral freeze
    void setFreezeEnabled(bool enabled) noexcept {
        freezeEnabled_ = enabled;
    }
    [[nodiscard]] bool isFreezeEnabled() const noexcept { return freezeEnabled_; }

    // =========================================================================
    // Diffusion
    // =========================================================================

    /// @brief Set diffusion amount (0.0 to 1.0)
    void setDiffusion(float amount) noexcept {
        diffusion_ = std::clamp(amount, kMinDiffusion, kMaxDiffusion);
        diffusionSmoother_.setTarget(diffusion_);
    }
    [[nodiscard]] float getDiffusion() const noexcept { return diffusion_; }

    // =========================================================================
    // Output
    // =========================================================================

    /// @brief Set dry/wet mix (0.0 to 1.0)
    void setDryWetMix(float mix) noexcept {
        dryWetMix_ = std::clamp(mix, kMinDryWet, kMaxDryWet);
        dryWetSmoother_.setTarget(dryWetMix_);
    }
    [[nodiscard]] float getDryWetMix() const noexcept { return dryWetMix_; }

    // =========================================================================
    // Stereo Width (Phase 3.2)
    // =========================================================================

    /// @brief Set stereo width/decorrelation amount (0.0 to 1.0)
    /// 0.0 = mono-like processing, 1.0 = full stereo decorrelation
    void setStereoWidth(float amount) noexcept {
        stereoWidth_ = std::clamp(amount, kMinStereoWidth, kMaxStereoWidth);
        stereoWidthSmoother_.setTarget(stereoWidth_);
    }
    [[nodiscard]] float getStereoWidth() const noexcept { return stereoWidth_; }

    // =========================================================================
    // Tempo Sync (spec 041)
    // =========================================================================

    /// @brief Set time mode: 0 = Free (ms), 1 = Synced (note value + tempo)
    /// @param mode 0 for Free, 1 for Synced
    void setTimeMode(int mode) noexcept {
        timeMode_ = (mode == 1) ? TimeMode::Synced : TimeMode::Free;
    }
    [[nodiscard]] TimeMode getTimeMode() const noexcept { return timeMode_; }

    /// @brief Set note value index for tempo sync (0-9)
    /// Maps to: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1
    /// @param index Dropdown index (0-9), default 4 = 1/8 note
    void setNoteValue(int index) noexcept {
        noteValueIndex_ = std::clamp(index, 0, 9);
    }
    [[nodiscard]] int getNoteValue() const noexcept { return noteValueIndex_; }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get latency in samples: the wet FIFO's fftSize plus the one
    ///        frame (hop) the per-bin delay loop costs, so at base delay 0 the
    ///        wet output is the input delayed by exactly this many samples,
    ///        whatever lengths the caller's buffers have (R-150)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return fftSize_ + hopSize_;
    }

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief Base delay the spectral frames are currently using (the smoothed
    ///        value, in ms), as opposed to getBaseDelayMs(), the free-mode setting
    [[nodiscard]] float getSmoothedBaseDelayMs() const noexcept {
        return baseDelaySmoother_.getCurrentValue();
    }

    /// @brief Snap all smoothers to target for instant parameter changes
    /// @note In Synced mode the base delay's target is derived from the host
    ///       tempo, which only process() receives, so the base delay snaps to the
    ///       synced time at the start of the next process() call instead.
    void snapParameters() noexcept {
        syncSnapPending_ = true;
        baseDelaySmoother_.snapToTarget();
        spreadSmoother_.snapToTarget();
        feedbackSmoother_.snapToTarget();
        tiltSmoother_.snapToTarget();
        diffusionSmoother_.snapToTarget();
        dryWetSmoother_.snapToTarget();
        stereoWidthSmoother_.snapToTarget();
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// Bins of the FFT size in use
    [[nodiscard]] std::size_t activeNumBins() const noexcept { return fftSize_ / 2 + 1; }

    /// @brief Make @p fftSize the size in use, inside the storage prepare()
    ///        allocated for every supported size
    /// @note Real-time safe (no allocation): every transform is reserved and
    ///       every per-bin delay line was sized for its highest frame rate, so
    ///       re-preparing it at this size's frame rate only shrinks or regrows
    ///       it within its capacity. Clears the frames, spectra and the active
    ///       delay lines; the caller clears the rest (reset()).
    void applyFFTSize(std::size_t fftSize) noexcept {
        fftSize_ = fftSize;
        hopSize_ = fftSize / 2;  // 50% overlap

        // Every size in [kMinFFTSize, kMaxFFTSize] is reserved, so none of
        // these can fail for a size setFFTSize() produced.
        (void)stftL_.setActiveSize(fftSize_, hopSize_);
        (void)stftR_.setActiveSize(fftSize_, hopSize_);
        (void)overlapAddL_.setActiveSize(fftSize_, hopSize_);
        (void)overlapAddR_.setActiveSize(fftSize_, hopSize_);
        for (SpectralBuffer* s : {&inputSpectrumL_, &inputSpectrumR_, &outputSpectrumL_,
                                  &outputSpectrumR_, &frozenSpectrumL_, &frozenSpectrumR_}) {
            s->setActiveSize(fftSize_);
        }

        // Per-bin delay lines work at spectral frame rate (sampleRate / hopSize)
        const double frameRate = sampleRate_ / static_cast<double>(hopSize_);
        const float maxDelaySeconds = maxDelayMs_ / 1000.0f;
        const std::size_t numBins = std::min(activeNumBins(), binRealDelaysL_.size());
        for (std::size_t i = 0; i < numBins; ++i) {
            binRealDelaysL_[i].prepare(frameRate, maxDelaySeconds);
            binRealDelaysR_[i].prepare(frameRate, maxDelaySeconds);
            binImagDelaysL_[i].prepare(frameRate, maxDelaySeconds);
            binImagDelaysR_[i].prepare(frameRate, maxDelaySeconds);
        }

        // Configure parameter smoothers (50ms smoothing time for spectral processing)
        // Increased from 10ms to reduce artifacts during parameter changes.
        // Each smoother is configured at the rate it is ADVANCED at, so 50 ms is
        // 50 ms of real time: the spectral-parameter smoothers step once per frame
        // in processSpectralFrame() (frame rate = sampleRate / hop), dry/wet once
        // per sample in process(). Configured per sample but stepped per frame,
        // they took 'hop' times longer (~26 s at the 1024-point default).
        // configure() keeps each smoother's current value.
        const float smoothTimeMs = 50.0f;
        const auto frameRateF = static_cast<float>(frameRate);
        baseDelaySmoother_.configure(smoothTimeMs, frameRateF);
        spreadSmoother_.configure(smoothTimeMs, frameRateF);
        feedbackSmoother_.configure(smoothTimeMs, frameRateF);
        tiltSmoother_.configure(smoothTimeMs, frameRateF);
        diffusionSmoother_.configure(smoothTimeMs, frameRateF);
        stereoWidthSmoother_.configure(smoothTimeMs, frameRateF);
        dryWetSmoother_.configure(smoothTimeMs, static_cast<float>(sampleRate_));

        // Freeze crossfade increment (per frame, not per sample)
        freezeCrossfadeIncrement_ = static_cast<float>(hopSize_) /
                                    (kFreezeCrossfadeTimeMs * 0.001f *
                                     static_cast<float>(sampleRate_));
    }

    /// @brief Empty the wet FIFO and fill it with fftSize samples of silence:
    ///        the fixed pipeline part of getLatencySamples().
    void resetWetFifo() noexcept {
        std::fill(wetFifoL_.begin(), wetFifoL_.end(), 0.0f);
        std::fill(wetFifoR_.begin(), wetFifoR_.end(), 0.0f);
        wetFifoRead_ = 0;
        wetFifoCount_ = std::min(fftSize_, wetFifoL_.size());
    }

    /// @brief Move the hop the last synthesize() completed into the wet FIFO.
    void drainHopIntoWetFifo() noexcept {
        const std::size_t capacity = wetFifoL_.size();
        const std::size_t ready = std::min(overlapAddL_.samplesAvailable(),
                                           overlapAddR_.samplesAvailable());
        // Occupancy stays below fftSize + hop + one block (see process()),
        // inside the capacity prepare() allocated; the bound keeps an
        // impossible overflow inert.
        const std::size_t n = std::min(ready, capacity - wetFifoCount_);
        std::size_t write = wetFifoRead_ + wetFifoCount_;
        if (write >= capacity) write -= capacity;
        const std::size_t first = std::min(n, capacity - write);
        overlapAddL_.pullSamples(wetFifoL_.data() + write, first);
        overlapAddR_.pullSamples(wetFifoR_.data() + write, first);
        if (n > first) {
            overlapAddL_.pullSamples(wetFifoL_.data(), n - first);
            overlapAddR_.pullSamples(wetFifoR_.data(), n - first);
        }
        wetFifoCount_ += n;
    }

    /// @brief Take the next @p numSamples of the wet stream into tempBuffer.
    void popWetFifo(std::size_t numSamples) noexcept {
        const std::size_t capacity = wetFifoL_.size();
        // The FIFO always holds a block by construction; silence pads the
        // impossible shortfall rather than reading stale samples.
        const std::size_t n = std::min(numSamples, wetFifoCount_);
        const std::size_t first = std::min(n, capacity - wetFifoRead_);
        std::copy_n(wetFifoL_.data() + wetFifoRead_, first, tempBufferL_.data());
        std::copy_n(wetFifoR_.data() + wetFifoRead_, first, tempBufferR_.data());
        std::copy_n(wetFifoL_.data(), n - first, tempBufferL_.data() + first);
        std::copy_n(wetFifoR_.data(), n - first, tempBufferR_.data() + first);
        std::fill(tempBufferL_.data() + n, tempBufferL_.data() + numSamples, 0.0f);
        std::fill(tempBufferR_.data() + n, tempBufferR_.data() + numSamples, 0.0f);
        wetFifoRead_ += n;
        if (wetFifoRead_ >= capacity) wetFifoRead_ -= capacity;
        wetFifoCount_ -= n;
    }

    /// @brief Ramp the wet output (tempBuffer) toward silence while an FFT size
    ///        request is pending, and back up if the request is withdrawn
    /// @param produced Wet samples in tempBuffer this block
    /// @param numSamples Block length (the gain advances once per sample)
    void applyWetSwitchFade(std::size_t produced, std::size_t numSamples) noexcept {
        const bool fadingOut = requestedFFTSize_ != fftSize_;
        for (std::size_t i = 0; i < numSamples; ++i) {
            wetSwitchGain_ = fadingOut ? std::max(0.0f, wetSwitchGain_ - wetSwitchStep_)
                                       : std::min(1.0f, wetSwitchGain_ + wetSwitchStep_);
            if (i < produced) {
                tempBufferL_[i] *= wetSwitchGain_;
                tempBufferR_[i] *= wetSwitchGain_;
            }
        }
    }

    /// Spread runs over octave position from kSpreadLowHz (0) to kSpreadHighHz (1)
    static constexpr float kSpreadLowHz = 20.0f;
    static constexpr float kSpreadHighHz = 20000.0f;
    /// Logarithmic curve: ln(1 + k p) / ln(1 + k), front-loaded (p = 0.5 -> 0.74)
    static constexpr float kSpreadLogShape = 9.0f;

    /// @brief Calculate delay time for a specific bin based on spread settings
    ///
    /// The spread is laid out over octaves, not over the bin index. Linear in the
    /// bin index (linear in Hz) gave everything below ~2 kHz the first ~9 % of the
    /// spread at 44.1 kHz, so Low > High sounded like a plain echo and High > Low
    /// / Center Out like a plain longer one (measured 2026-11-23). On the octave
    /// scale each octave gets an equal share (Linear), or the low octaves most of
    /// it (Logarithmic), independent of sample rate and FFT size.
    [[nodiscard]] float calculateBinDelayMs(std::size_t bin,
                                            std::size_t numBins,
                                            float baseDelay,
                                            float spread) const noexcept {
        if (numBins <= 1) return baseDelay;

        const float binHz = static_cast<float>(bin) * static_cast<float>(sampleRate_)
                          / static_cast<float>(fftSize_);
        // Clamped below the log, so DC and sub-20 Hz bins sit exactly at 0
        float position = 0.0f;
        if (binHz > kSpreadLowHz) {
            position = std::clamp(std::log(binHz / kSpreadLowHz)
                                      / std::log(kSpreadHighHz / kSpreadLowHz),
                                  0.0f, 1.0f);
        }

        if (spreadCurve_ == SpreadCurve::Logarithmic) {
            position = std::clamp(std::log1p(kSpreadLogShape * position)
                                      / std::log1p(kSpreadLogShape),
                                  0.0f, 1.0f);
        }

        float delayOffset = 0.0f;

        switch (spreadDirection_) {
            case SpreadDirection::LowToHigh:
                delayOffset = position * spread;
                break;
            case SpreadDirection::HighToLow:
                delayOffset = (1.0f - position) * spread;
                break;
            case SpreadDirection::CenterOut:
                delayOffset = std::abs(position - 0.5f) * 2.0f * spread;
                break;
        }

        return baseDelay + delayOffset;
    }

    /// Per-channel scratch for readDelayedSpectrum (sized in prepare)
    struct DelayedSpectrum {
        std::vector<float> magNew;   ///< newer frame's magnitude (peak picking)
        std::vector<float> advance;  ///< phase advance per hop, locked to the region's peak
        std::vector<float> mag;      ///< delayed magnitude
        std::vector<float> phase;    ///< delayed phase
    };

    /// Bins quieter than this are never peaks (silence keeps per-bin advances)
    static constexpr float kPeakFloor = 1e-9f;

    /// @brief Read one channel's spectrum at each bin's fractional frame delay
    ///        (binDelayFrames_) into out.mag / out.phase
    ///
    /// Most delay times fall between two STFT frames. Blending the frames' complex
    /// values linearly cancels: at 50 % overlap a bin's phase turns by up to pi per
    /// hop, so the blend lost ~5 dB at the 250 ms default. Instead the magnitude is
    /// blended, and the phase steps back from the newer frame by the fraction of
    /// the phase advance between the two frames, as a phase vocoder does. The
    /// advance is measured at each spectral peak and shared by every bin in the
    /// peak's region (phase locking): 50 % overlap only resolves a component within
    /// one bin of a bin's centre, and a window's main lobe spans three or more, so per-bin
    /// estimates came out +1.3 dB on a steady tone. Locked, the bins around a
    /// component turn together, which is a time shift of that component. A
    /// whole-frame delay reads the newer frame unchanged.
    void readDelayedSpectrum(const std::vector<DelayLine>& realLines,
                             const std::vector<DelayLine>& imagLines,
                             DelayedSpectrum& out, std::size_t numBins) noexcept {
        // Pass 1: newer-frame magnitude and phase, blended magnitude, and each
        // bin's own true phase advance per hop (expected plus wrapped deviation)
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const float delayFrames = binDelayFrames_[bin];
            const auto newer = static_cast<std::size_t>(delayFrames);
            const float frac = delayFrames - static_cast<float>(newer);

            const float reNew = realLines[bin].read(newer);
            const float imNew = imagLines[bin].read(newer);
            const float reOld = realLines[bin].read(newer + 1);
            const float imOld = imagLines[bin].read(newer + 1);
            const float magNew = std::sqrt(reNew * reNew + imNew * imNew);
            const float magOld = std::sqrt(reOld * reOld + imOld * imOld);
            const float phaseNew = std::atan2(imNew, reNew);
            const float phaseOld = std::atan2(imOld, reOld);

            const float expected = kTwoPi * static_cast<float>(bin) * static_cast<float>(hopSize_)
                                 / static_cast<float>(fftSize_);
            float deviation = phaseNew - phaseOld - expected;
            deviation -= kTwoPi * std::round(deviation / kTwoPi);

            out.magNew[bin] = magNew;
            // Magnitude and phase come from the nearer frame, so the phase is never
            // projected more than half a hop (pass 3 steps it by its signed
            // distance). Blending the two magnitudes instead dimmed the echo of a
            // note onset, which the older frame holds less of, by up to 3.5 dB.
            const bool newerIsNearer = frac <= 0.5f;
            out.phase[bin] = newerIsNearer ? phaseNew : phaseOld;
            out.mag[bin] = newerIsNearer ? magNew : magOld;
            out.advance[bin] = expected + deviation;
        }

        // Pass 2: lock every bin to the advance of its region's peak. A region
        // runs from one peak to the lowest bin between it and the next.
        std::size_t peakCount = 0;
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const float m = out.magNew[bin];
            const bool aboveLeft = (bin == 0) || m > out.magNew[bin - 1];
            const bool notBelowRight = (bin + 1 == numBins) || m >= out.magNew[bin + 1];
            if (aboveLeft && notBelowRight && m > kPeakFloor) {
                peaks_[peakCount++] = bin;
            }
        }
        for (std::size_t bin = 0; bin < numBins; ++bin) regionPeak_[bin] = bin;
        if (peakCount > 0) {
            for (std::size_t bin = 0; bin < peaks_[0]; ++bin) {
                out.advance[bin] = out.advance[peaks_[0]];
                regionPeak_[bin] = peaks_[0];
            }
            for (std::size_t p = 0; p + 1 < peakCount; ++p) {
                const std::size_t left = peaks_[p];
                const std::size_t right = peaks_[p + 1];
                std::size_t lowest = left;
                for (std::size_t bin = left + 1; bin < right; ++bin) {
                    if (out.magNew[bin] < out.magNew[lowest]) lowest = bin;
                }
                for (std::size_t bin = left + 1; bin < right; ++bin) {
                    const std::size_t owner = bin <= lowest ? left : right;
                    out.advance[bin] = out.advance[owner];
                    regionPeak_[bin] = owner;
                }
            }
            for (std::size_t bin = peaks_[peakCount - 1] + 1; bin < numBins; ++bin) {
                out.advance[bin] = out.advance[peaks_[peakCount - 1]];
                regionPeak_[bin] = peaks_[peakCount - 1];
            }
        }

        // Pass 3: step each bin's phase from its nearer frame by the locked advance:
        // back by frac from the newer frame, forward by (1 - frac) from the older
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const float delayFrames = binDelayFrames_[bin];
            const float frac = delayFrames - std::floor(delayFrames);
            const float stepFromNearer = (frac <= 0.5f) ? -frac : (1.0f - frac);
            out.phase[bin] += stepFromNearer * out.advance[bin];
        }

        // Pass 4: identity phase locking. A component spans several bins (the
        // window's main lobe); with Spread they read different frames, so their
        // phases no longer line up and the component partly cancelled itself in
        // the resynthesis (-1.4 to -5.6 dB depending on the exact spread, measured
        // 2026-11-23). Every bin of a region takes its phase relative to the peak
        // from the PEAK's nearer frame, so the component leaves whole at the peak's
        // timing; each bin keeps its own delayed magnitude. With no spread every
        // bin reads the peak's frame already, and this changes nothing.
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const std::size_t peak = regionPeak_[bin];
            if (peak == bin) continue;
            const float peakDelay = binDelayFrames_[peak];
            const auto peakNewer = static_cast<std::size_t>(peakDelay);
            const std::size_t peakFrame =
                peakNewer + ((peakDelay - static_cast<float>(peakNewer)) <= 0.5f ? 0u : 1u);
            const float phaseBin = std::atan2(imagLines[bin].read(peakFrame),
                                              realLines[bin].read(peakFrame));
            const float phasePeak = std::atan2(imagLines[peak].read(peakFrame),
                                               realLines[peak].read(peakFrame));
            out.phase[bin] = out.phase[peak] + (phaseBin - phasePeak);
        }
    }

    /// @brief Calculate tilted feedback gain for a specific bin
    [[nodiscard]] static float calculateTiltedFeedback(std::size_t bin,
                                                std::size_t numBins,
                                                float globalFeedback,
                                                float tilt) noexcept {
        if (numBins <= 1) return globalFeedback;

        const float normalizedBin = static_cast<float>(bin) /
                                    static_cast<float>(numBins - 1);
        // tilt: -1.0 = full low bias, 0.0 = uniform, +1.0 = full high bias
        const float tiltFactor = 1.0f + tilt * (normalizedBin - 0.5f) * 2.0f;
        return std::clamp(globalFeedback * tiltFactor, 0.0f, kMaxFeedback);
    }

    /// @brief Apply diffusion blur to magnitude spectrum
    void applyDiffusion(const SpectralBuffer& input,
                        float diffusionAmount) noexcept {
        const std::size_t numBins = input.numBins();
        if (numBins < 3 || diffusionAmount < 0.001f) return;

        // 3-tap blur kernel
        const float side = diffusionAmount * 0.25f;
        const float center = 1.0f - diffusionAmount * 0.5f;

        // Store original magnitudes
        for (std::size_t i = 0; i < numBins; ++i) {
            blurredMag_[i] = input.getMagnitude(i);
        }

        // Apply blur (skip edges)
        for (std::size_t i = 1; i < numBins - 1; ++i) {
            blurredMag_[i] = input.getMagnitude(i - 1) * side +
                             input.getMagnitude(i) * center +
                             input.getMagnitude(i + 1) * side;
        }
    }

    /// @brief Process one spectral frame
    void processSpectralFrame(SpectralBuffer& inputL, SpectralBuffer& inputR,
                              SpectralBuffer& outputL, SpectralBuffer& outputR) noexcept {
        const std::size_t numBins = inputL.numBins();
        if (numBins == 0) return;

        // Get smoothed parameters
        const float baseDelay = baseDelaySmoother_.process();
        const float spread = spreadSmoother_.process();
        const float feedback = feedbackSmoother_.process();
        const float tilt = tiltSmoother_.process();
        const float diffusion = diffusionSmoother_.process();
        const float stereoWidth = stereoWidthSmoother_.process();

        // =====================================================================
        // Frame-Continuous Phase Update (Random Walk) for Stereo Decorrelation
        // =====================================================================
        // Uses a random walk approach where phase drifts smoothly with small
        // random increments. This creates stereo decorrelation without
        // discontinuities at frame boundaries.
        for (std::size_t i = 0; i < numBins && i < stereoPhaseL_.size(); ++i) {
            // Random walk for stereo decorrelation
            stereoPhaseL_[i] += (rng_.nextUnipolar() - 0.5f) * kPhaseWalkRate;
            stereoPhaseR_[i] += (rng_.nextUnipolar() - 0.5f) * kPhaseWalkRate;

            // Soft clamp to prevent unbounded growth while maintaining smoothness
            constexpr float kMaxPhase = kPi;
            constexpr float kDecay = 0.995f;

            if (std::abs(stereoPhaseL_[i]) > kMaxPhase) {
                stereoPhaseL_[i] *= kDecay;
            }
            if (std::abs(stereoPhaseR_[i]) > kMaxPhase) {
                stereoPhaseR_[i] *= kDecay;
            }
        }

        // Handle freeze transition
        const bool freezing = freezeEnabled_;
        if (freezing && !wasFrozen_) {
            // Just entered freeze: capture current spectrum
            for (std::size_t i = 0; i < numBins; ++i) {
                frozenSpectrumL_.setMagnitude(i, inputL.getMagnitude(i));
                frozenSpectrumL_.setPhase(i, inputL.getPhase(i));
                frozenSpectrumR_.setMagnitude(i, inputR.getMagnitude(i));
                frozenSpectrumR_.setPhase(i, inputR.getPhase(i));
            }
            freezeCrossfade_ = 0.0f;
            freezePhaseDrift_ = 0.0f;  // Reset phase drift when entering freeze
        }
        wasFrozen_ = freezing;

        // Update freeze crossfade
        if (freezing && freezeCrossfade_ < 1.0f) {
            freezeCrossfade_ = std::min(1.0f, freezeCrossfade_ + freezeCrossfadeIncrement_);
        } else if (!freezing && freezeCrossfade_ > 0.0f) {
            freezeCrossfade_ = std::max(0.0f, freezeCrossfade_ - freezeCrossfadeIncrement_);
        }

        // Phase 2.2: Accumulate phase drift when fully frozen
        // This prevents static resonance by slowly evolving the frozen spectrum
        if (freezing && freezeCrossfade_ >= 0.99f) {
            freezePhaseDrift_ += kPhaseDriftRate;
            // Wrap to prevent float precision issues over long freezes
            if (freezePhaseDrift_ > kTwoPi) {
                freezePhaseDrift_ -= kTwoPi;
            }
        }

        // Per-bin delay time in frames
        const float frameRate = static_cast<float>(sampleRate_) /
                                static_cast<float>(hopSize_);
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const float binDelayMs = calculateBinDelayMs(bin, numBins, baseDelay, spread);
            // ONE FRAME of the requested delay is spent by the loop itself. The
            // read below happens BEFORE this frame's write, and DelayLine::read(0)
            // returns the most recent write (primitives/delay_line.h:287-300), so
            // a value written at frame n is read back at frame n + 1 + d: the
            // realized delay is d + 1 frames, not d. Uncompensated, every echo
            // lands one hop late - 10.7 ms at the 1024-point default and 42.7 ms
            // at kMaxFFTSize, which for a TEMPO-SYNCED delay is an audible,
            // FFT-size-dependent offset from the beat the user selected.
            // Clamped at zero, so the shortest realizable delay stays the one
            // frame the loop structurally costs.
            binDelayFrames_[bin] =
                std::max(0.0f, (binDelayMs / 1000.0f) * frameRate - 1.0f);
        }

        // Read the delayed spectra (before this frame's write, see above)
        readDelayedSpectrum(binRealDelaysL_, binImagDelaysL_, delayedL_, numBins);
        readDelayedSpectrum(binRealDelaysR_, binImagDelaysR_, delayedR_, numBins);

        // Feedback soft limiter, in the signal's own (time-domain) scale (R-057).
        // tanh() on raw FFT magnitudes (FFT::forward is unnormalised: a sine of
        // amplitude A peaks near A * fftSize / 4) clamped every strong bin to ~1
        // and put echo 2 ~33 dB below echo 1 at 90 % feedback. A per-bin limiter
        // cannot bound feedback above unity either: every weak bin keeps growing
        // until it saturates on its own, flattening the echo into broadband noise.
        // So the whole frame gets one gain: its RMS (Parseval, sqrt-Hann window,
        // mean(w^2) = 1/2, so sum|X|^2 over the half spectrum ~= fftSize^2 *
        // rms^2 / 4) is soft-limited with C * tanh(rms / C). Near-linear at
        // normal levels, smooth (always applied, so no switch when feedback
        // drops), spectrum preserved.
        float feedbackPowL = 0.0f;
        float feedbackPowR = 0.0f;
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const float binFeedback = calculateTiltedFeedback(bin, numBins,
                                                              feedback, tilt);
            const float fbL = delayedL_.mag[bin] * binFeedback;
            const float fbR = delayedR_.mag[bin] * binFeedback;
            feedbackPowL += fbL * fbL;
            feedbackPowR += fbR * fbR;
        }
        const float feedbackRms = std::sqrt(std::max(feedbackPowL, feedbackPowR) * 4.0f) /
                                  static_cast<float>(fftSize_);
        const float feedbackLimitGain =
            feedbackRms > 1e-9f
                ? kFeedbackLimitRms * std::tanh(feedbackRms / kFeedbackLimitRms) / feedbackRms
                : 1.0f;

        // Process each bin
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            // Calculate tilted feedback for this bin
            const float binFeedback = calculateTiltedFeedback(bin, numBins,
                                                              feedback, tilt);

            // Get input magnitude and phase
            float inputMagL = inputL.getMagnitude(bin);
            float inputMagR = inputR.getMagnitude(bin);
            const float inputPhaseL = inputL.getPhase(bin);
            const float inputPhaseR = inputR.getPhase(bin);

            // Convert input to complex (real + imaginary) for delay line storage
            // This avoids phase wrapping issues during linear interpolation
            const float inputRealL = inputMagL * std::cos(inputPhaseL);
            const float inputImagL = inputMagL * std::sin(inputPhaseL);
            const float inputRealR = inputMagR * std::cos(inputPhaseR);
            const float inputImagR = inputMagR * std::sin(inputPhaseR);

            const float delayedMagL = delayedL_.mag[bin];
            const float delayedMagR = delayedR_.mag[bin];
            const float delayedPhaseL = delayedL_.phase[bin];
            const float delayedPhaseR = delayedR_.phase[bin];

            // Apply feedback with the frame-wide soft limit computed above
            const float feedbackMagL = delayedMagL * binFeedback * feedbackLimitGain;
            const float feedbackMagR = delayedMagR * binFeedback * feedbackLimitGain;

            // Convert feedback to complex using delayed phase
            const float feedbackRealL = feedbackMagL * std::cos(delayedPhaseL);
            const float feedbackImagL = feedbackMagL * std::sin(delayedPhaseL);
            const float feedbackRealR = feedbackMagR * std::cos(delayedPhaseR);
            const float feedbackImagR = feedbackMagR * std::sin(delayedPhaseR);

            // Only write to delay lines when not frozen
            // This ensures freeze truly ignores new input
            if (!freezing) {
                // Write complex values (input + feedback) to delay lines
                binRealDelaysL_[bin].write(inputRealL + feedbackRealL);
                binImagDelaysL_[bin].write(inputImagL + feedbackImagL);
                binRealDelaysR_[bin].write(inputRealR + feedbackRealR);
                binImagDelaysR_[bin].write(inputImagR + feedbackImagR);
            }

            // Output is the delayed magnitude and phase
            float outMagL = delayedMagL;
            float outMagR = delayedMagR;
            float outPhaseL = delayedPhaseL;
            float outPhaseR = delayedPhaseR;

            // Apply freeze crossfade if active
            if (freezeCrossfade_ > 0.0f) {
                const float frozenMagL = frozenSpectrumL_.getMagnitude(bin);
                const float frozenMagR = frozenSpectrumR_.getMagnitude(bin);
                float frozenPhaseL = frozenSpectrumL_.getPhase(bin);
                float frozenPhaseR = frozenSpectrumR_.getPhase(bin);

                // Phase 2.2: Apply phase drift per-bin with slight frequency variation
                // Higher bins drift slightly faster for more natural evolution
                if (freezePhaseDrift_ > 0.0f) {
                    const float binFactor = 1.0f + 0.5f * static_cast<float>(bin) /
                                                   static_cast<float>(numBins);
                    frozenPhaseL += freezePhaseDrift_ * binFactor;
                    frozenPhaseR += freezePhaseDrift_ * binFactor;
                }

                outMagL = outMagL * (1.0f - freezeCrossfade_) +
                          frozenMagL * freezeCrossfade_;
                outMagR = outMagR * (1.0f - freezeCrossfade_) +
                          frozenMagR * freezeCrossfade_;

                // When fully frozen (crossfade >= 0.99), use frozen phase (with drift)
                // to ensure new input has no effect on output
                if (freezeCrossfade_ >= 0.99f) {
                    outPhaseL = frozenPhaseL;
                    outPhaseR = frozenPhaseR;
                }
            }

            // Phase 3.2: Apply stereo decorrelation using frame-continuous phase
            // Uses smoothly interpolating phase offsets instead of per-frame random values
            // to avoid clicks at frame boundaries
            if (stereoWidth > 0.001f) {
                // Scale the smoothed per-bin phase offsets by stereo width
                const float stereoScaleL = stereoPhaseL_[bin] * stereoWidth;
                const float stereoScaleR = stereoPhaseR_[bin] * stereoWidth;
                outPhaseL += stereoScaleL;
                outPhaseR -= stereoScaleR;  // Opposite direction for maximum decorrelation
            }

            // Set output spectrum
            outputL.setMagnitude(bin, outMagL);
            outputL.setPhase(bin, outPhaseL);
            outputR.setMagnitude(bin, outMagR);
            outputR.setPhase(bin, outPhaseR);
        }

        // Apply diffusion magnitude blur if enabled
        // This spreads energy across neighboring frequency bins
        if (diffusion > 0.001f) {
            applyDiffusion(outputL, diffusion);
            for (std::size_t i = 0; i < numBins; ++i) {
                outputL.setMagnitude(i, blurredMag_[i]);
            }
            applyDiffusion(outputR, diffusion);
            for (std::size_t i = 0; i < numBins; ++i) {
                outputR.setMagnitude(i, blurredMag_[i]);
            }
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Random number generator for phase randomization. prepare() and reset() seed it
    // from rngSeed_, so a render does not depend on the object's address.
    static constexpr uint32_t kDefaultRngSeed = 0x5DE1A7u;
    uint32_t rngSeed_ = kDefaultRngSeed;
    Xorshift32 rng_{kDefaultRngSeed};

    // FFT Configuration
    std::size_t fftSize_ = kDefaultFFTSize;           ///< size in use
    std::size_t hopSize_ = kDefaultFFTSize / 2;       // 50% overlap
    std::size_t requestedFFTSize_ = kDefaultFFTSize;  ///< last setFFTSize(), applied by process()

    // Wet fade-out before a live FFT size switch (applyWetSwitchFade)
    static constexpr float kFFTSwitchFadeMs = 10.0f;
    float wetSwitchGain_ = 1.0f;
    float wetSwitchStep_ = 1.0f;

    // STFT Analysis (stereo)
    STFT stftL_;
    STFT stftR_;

    // Overlap-Add Synthesis (stereo)
    OverlapAdd overlapAddL_;
    OverlapAdd overlapAddR_;

    // Spectral Buffers
    SpectralBuffer inputSpectrumL_;
    SpectralBuffer inputSpectrumR_;
    SpectralBuffer outputSpectrumL_;
    SpectralBuffer outputSpectrumR_;
    SpectralBuffer frozenSpectrumL_;
    SpectralBuffer frozenSpectrumR_;

    // Per-Bin Delay Lines for Complex Values (stereo)
    // We delay real and imaginary parts instead of magnitude and phase
    // to avoid phase wrapping issues during linear interpolation.
    // When phase wraps from +π to -π, linear interpolation produces incorrect
    // values (e.g., interp(3.1, -3.1, 0.5) = 0.0 instead of ~±π).
    // Complex interpolation handles this correctly.
    std::vector<DelayLine> binRealDelaysL_;
    std::vector<DelayLine> binRealDelaysR_;
    std::vector<DelayLine> binImagDelaysL_;
    std::vector<DelayLine> binImagDelaysR_;
    std::size_t resetLineBin_ = 0;  ///< first bin continueIncrementalReset() has not finished

    // Parameters
    float maxDelayMs_ = kMaxDelayMs;  ///< prepare()'s maxDelayMs
    float baseDelayMs_ = kDefaultDelayMs;
    float spreadMs_ = 0.0f;
    SpreadDirection spreadDirection_ = SpreadDirection::LowToHigh;
    SpreadCurve spreadCurve_ = SpreadCurve::Linear;  // Phase 3.1: Default to linear
    float feedback_ = 0.0f;
    float feedbackTilt_ = 0.0f;
    float diffusion_ = 0.0f;
    float dryWetMix_ = kDefaultDryWet;
    float stereoWidth_ = 0.0f;  // Phase 3.2: Stereo decorrelation amount
    bool freezeEnabled_ = false;

    // Tempo Sync State (spec 041)
    TimeMode timeMode_ = TimeMode::Free;  // Default to free (ms) mode
    int noteValueIndex_ = 4;              // Default to 1/8 note (index 4)
    bool syncSnapPending_ = false;        // snapParameters() ran; next process() snaps the synced delay

    // Parameter Smoothers
    OnePoleSmoother baseDelaySmoother_;
    OnePoleSmoother spreadSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother tiltSmoother_;
    OnePoleSmoother diffusionSmoother_;
    OnePoleSmoother dryWetSmoother_;

    // Freeze State
    bool wasFrozen_ = false;
    float freezeCrossfade_ = 0.0f;
    static constexpr float kFreezeCrossfadeTimeMs = 75.0f;  // 50-100ms per spec
    float freezeCrossfadeIncrement_ = 0.0f;

    // Phase 2.2: Phase drift during freeze to prevent static resonance
    // Accumulates slowly over time when frozen to add subtle variation
    float freezePhaseDrift_ = 0.0f;
    static constexpr float kPhaseDriftRate = 0.01f;  // Radians per frame (slow drift)

    // =========================================================================
    // Stereo Decorrelation Phase System (Random Walk)
    // =========================================================================
    // Uses a random walk approach where phase drifts smoothly with small random
    // increments. This creates stereo decorrelation without discontinuities.
    //
    // Per-bin phase offsets for stereo width decorrelation (random walk values)
    std::vector<float> stereoPhaseL_;
    std::vector<float> stereoPhaseR_;

    // Random walk rate: small random increment per frame (±0.04 radians max)
    static constexpr float kPhaseWalkRate = 0.04f;

    // Stereo width parameter smoother
    OnePoleSmoother stereoWidthSmoother_;

    // Internal Buffers
    std::vector<float> tempBufferL_;
    std::vector<float> tempBufferR_;
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;
    std::vector<float> blurredMag_;

    // Wet stream FIFO (ring): synthesized hops in, one block out per call,
    // fftSize samples behind the input
    std::vector<float> wetFifoL_;
    std::vector<float> wetFifoR_;
    std::size_t wetFifoRead_ = 0;
    std::size_t wetFifoCount_ = 0;

    // Fractional-frame read (readDelayedSpectrum)
    std::vector<float> binDelayFrames_;
    std::vector<std::size_t> peaks_;
    std::vector<std::size_t> regionPeak_;  ///< each bin's region peak (readDelayedSpectrum)
    DelayedSpectrum delayedL_;
    DelayedSpectrum delayedR_;
};

} // namespace Krate::DSP

