// ==============================================================================
// LiveAnalysisPipeline Implementation
// ==============================================================================
// Phase 4: Full implementation of the real-time analysis pipeline.
//
// Orchestrates: PreProcessingPipeline -> YinPitchDetector -> STFT ->
// PartialTracker -> [MultiPitchDetector + MultiSourceSieve] ->
// HarmonicModelBuilder -> SpectralCoringEstimator
// ==============================================================================

#include "live_analysis_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Innexus {

// ==============================================================================
// Prepare
// ==============================================================================
void LiveAnalysisPipeline::prepare(double sampleRate, LatencyMode mode)
{
    sampleRate_ = static_cast<float>(sampleRate);
    latencyMode_ = mode;

    // Pre-processing pipeline
    preProcessing_.prepare(sampleRate);

    // Determine YIN window size based on mode
    if (mode == LatencyMode::HighPrecision)
    {
        yinWindowSize_ = kLongWindowConfig.fftSize / 2; // 2048 for bass detection
    }
    else
    {
        yinWindowSize_ = kShortWindowConfig.fftSize; // 1024 for low latency
    }

    // Reconstruct YIN with the correct window size
    yin_ = Krate::DSP::YinPitchDetector(yinWindowSize_);
    yin_.prepare(sampleRate);

    // Short STFT (always active)
    shortStft_.prepare(
        kShortWindowConfig.fftSize,
        kShortWindowConfig.hopSize,
        kShortWindowConfig.windowType);
    shortSpectrum_.prepare(kShortWindowConfig.fftSize);

    // Long STFT (only in HighPrecision mode)
    longStftActive_ = (mode == LatencyMode::HighPrecision);
    if (longStftActive_)
    {
        longStft_.prepare(
            kLongWindowConfig.fftSize,
            kLongWindowConfig.hopSize,
            kLongWindowConfig.windowType);
        longSpectrum_.prepare(kLongWindowConfig.fftSize);
    }

    // Partial tracker
    tracker_.prepare(kShortWindowConfig.fftSize, sampleRate);
    {
        float cg = Krate::DSP::Window::coherentGain(kShortWindowConfig.windowType);
        float ampScale = 2.0f / (static_cast<float>(kShortWindowConfig.fftSize) * cg);
        tracker_.setAmplitudeScale(ampScale);
    }

    // Harmonic model builder
    modelBuilder_.prepare(sampleRate);
    modelBuilder_.setHopSize(static_cast<int>(kShortWindowConfig.hopSize));

    // Spectral coring estimator
    coringEstimator_.prepare(kShortWindowConfig.fftSize, sampleRate_);

    // Multi-pitch detector
    multiPitchDetector_.prepare(kShortWindowConfig.fftSize, sampleRate);

    // Multi-source sieve
    multiSourceSieve_.prepare(sampleRate);

    // YIN circular buffer
    yinBuffer_.resize(yinWindowSize_, 0.0f);
    yinContiguousBuffer_.resize(yinWindowSize_, 0.0f);
    yinWriteIndex_ = 0;
    yinBufferFilled_ = false;

    // Reset output state
    latestFrame_ = {};
    latestPolyFrame_ = {};
    latestResidualFrame_ = {};
    previousModeFrame_ = {};
    newFrameAvailable_ = false;
    lastFrameWasPolyphonic_ = false;
    modeSwitchCrossfadeRemaining_ = 0;

    prepared_ = true;
}

// ==============================================================================
// Reset
// ==============================================================================
void LiveAnalysisPipeline::reset()
{
    if (!prepared_)
        return;

    preProcessing_.reset();
    yin_.reset();
    shortStft_.reset();
    if (longStftActive_)
        longStft_.reset();
    tracker_.reset();
    modelBuilder_.reset();

    std::fill(yinBuffer_.begin(), yinBuffer_.end(), 0.0f);
    yinWriteIndex_ = 0;
    yinBufferFilled_ = false;

    latestFrame_ = {};
    latestPolyFrame_ = {};
    latestResidualFrame_ = {};
    previousModeFrame_ = {};
    newFrameAvailable_ = false;
    lastFrameWasPolyphonic_ = false;
    modeSwitchCrossfadeRemaining_ = 0;
}

// ==============================================================================
// Set Latency Mode
// ==============================================================================
void LiveAnalysisPipeline::setLatencyMode(LatencyMode mode)
{
    if (mode == latencyMode_)
        return;

    latencyMode_ = mode;

    if (mode == LatencyMode::HighPrecision && !longStftActive_)
    {
        // Activate long STFT
        longStft_.prepare(
            kLongWindowConfig.fftSize,
            kLongWindowConfig.hopSize,
            kLongWindowConfig.windowType);
        longSpectrum_.prepare(kLongWindowConfig.fftSize);
        longStftActive_ = true;

        // Expand YIN window
        yinWindowSize_ = kLongWindowConfig.fftSize / 2;
        yin_ = Krate::DSP::YinPitchDetector(yinWindowSize_);
        yin_.prepare(static_cast<double>(sampleRate_));
        yinBuffer_.resize(yinWindowSize_, 0.0f);
        yinContiguousBuffer_.resize(yinWindowSize_, 0.0f);
        yinWriteIndex_ = 0;
        yinBufferFilled_ = false;
    }
    else if (mode == LatencyMode::LowLatency && longStftActive_)
    {
        // Deactivate long STFT (just stop using it, no reset needed)
        longStftActive_ = false;

        // Shrink YIN window back
        yinWindowSize_ = kShortWindowConfig.fftSize;
        yin_ = Krate::DSP::YinPitchDetector(yinWindowSize_);
        yin_.prepare(static_cast<double>(sampleRate_));
        yinBuffer_.resize(yinWindowSize_, 0.0f);
        yinContiguousBuffer_.resize(yinWindowSize_, 0.0f);
        yinWriteIndex_ = 0;
        yinBufferFilled_ = false;
    }
}

// ==============================================================================
// Push Samples
// ==============================================================================
void LiveAnalysisPipeline::pushSamples(const float* data, size_t count)
{
    if (!prepared_ || data == nullptr || count == 0)
        return;

    // Process in chunks that fit our pre-processing buffer
    size_t offset = 0;
    while (offset < count)
    {
        const size_t chunkSize = std::min(count - offset, kMaxPreProcBlockSize);

        // Copy to pre-processing buffer for in-place processing
        std::memcpy(preProcBuffer_.data(), data + offset, chunkSize * sizeof(float));

        // Apply pre-processing (DC removal, HPF, noise gate, transient suppression)
        preProcessing_.processBlock(preProcBuffer_.data(), chunkSize);

        // Feed processed samples to STFTs
        shortStft_.pushSamples(preProcBuffer_.data(), chunkSize);
        if (longStftActive_)
        {
            longStft_.pushSamples(preProcBuffer_.data(), chunkSize);
        }

        // Accumulate in YIN buffer
        for (size_t i = 0; i < chunkSize; ++i)
        {
            yinBuffer_[yinWriteIndex_] = preProcBuffer_[i];
            yinWriteIndex_ = (yinWriteIndex_ + 1) % yinWindowSize_;
            if (yinWriteIndex_ == 0)
                yinBufferFilled_ = true;
        }

        offset += chunkSize;
    }

    // If the noise gate fired, skip STFT analysis entirely and emit a
    // zero-confidence frame.  This prevents the overlapping STFT window
    // from producing spurious pitch detections during silence.
    if (preProcessing_.isGated())
    {
        // Drain any pending STFT hops so the internal ring buffer stays
        // in sync, but discard the spectra.
        while (shortStft_.canAnalyze())
            shortStft_.analyze(shortSpectrum_);
        if (longStftActive_)
        {
            while (longStft_.canAnalyze())
                longStft_.analyze(longSpectrum_);
        }

        latestFrame_ = {};
        latestPolyFrame_ = {};
        latestResidualFrame_ = {};
        newFrameAvailable_ = true;
        lastFrameWasPolyphonic_ = false;
        return;
    }

    // Check if short STFT has enough data for analysis
    while (shortStft_.canAnalyze())
    {
        shortStft_.analyze(shortSpectrum_);

        // Also analyze long STFT if it's ready
        if (longStftActive_ && longStft_.canAnalyze())
        {
            longStft_.analyze(longSpectrum_);
        }

        runAnalysis();
    }
}

// ==============================================================================
// Run Analysis
// ==============================================================================
void LiveAnalysisPipeline::runAnalysis()
{
    // Run YIN pitch detection on the accumulated buffer
    Krate::DSP::F0Estimate f0;
    if (yinBufferFilled_)
    {
        // YIN needs a contiguous window. We have a circular buffer.
        // Unwrap into pre-allocated contiguous buffer (FR-008: no audio-thread allocation).
        for (size_t i = 0; i < yinWindowSize_; ++i)
        {
            yinContiguousBuffer_[i] = yinBuffer_[(yinWriteIndex_ + i) % yinWindowSize_];
        }
        f0 = yin_.detect(yinContiguousBuffer_.data(), yinWindowSize_);
    }
    else
    {
        f0 = {};
    }

    // Compute input RMS from the short spectrum (approximation)
    float inputRms = 0.0f;
    {
        const size_t numBins = shortSpectrum_.numBins();
        float sumSqr = 0.0f;
        for (size_t b = 0; b < numBins; ++b)
        {
            float mag = shortSpectrum_.getMagnitude(b);
            sumSqr += mag * mag;
        }
        if (numBins > 0)
        {
            inputRms = std::sqrt(sumSqr / static_cast<float>(numBins));
        }
    }

    // Decide mono vs poly based on analysis mode and YIN confidence
    bool usePolyPath = false;
    switch (analysisMode_)
    {
    case Krate::DSP::AnalysisMode::Mono:
        usePolyPath = false;
        break;
    case Krate::DSP::AnalysisMode::Poly:
        usePolyPath = true;
        break;
    case Krate::DSP::AnalysisMode::Auto:
        // Use poly when YIN confidence is low (suggests polyphonic content)
        usePolyPath = (f0.confidence <= kPolyConfidenceThreshold);
        break;
    }

    // Detect mode switch and initiate crossfade
    bool modeChanged = (usePolyPath != lastFrameWasPolyphonic_);
    if (modeChanged)
    {
        // Snapshot the current frame before switching modes
        previousModeFrame_ = latestFrame_;
        modeSwitchCrossfadeRemaining_ = kModeSwitchCrossfadeFrames;
    }

    if (usePolyPath)
    {
        runPolyphonicAnalysis(f0, inputRms);
        lastFrameWasPolyphonic_ = true;
    }
    else
    {
        runMonophonicAnalysis(f0, inputRms);
        lastFrameWasPolyphonic_ = false;
    }

    // Apply crossfade between old mode's frame and new mode's frame
    if (modeSwitchCrossfadeRemaining_ > 0)
    {
        float fadeNew = 1.0f - static_cast<float>(modeSwitchCrossfadeRemaining_) /
                                   static_cast<float>(kModeSwitchCrossfadeFrames);
        float fadeOld = 1.0f - fadeNew;

        // Crossfade the partial amplitudes of the primary output frame
        int numNew = latestFrame_.numPartials;
        int numOld = previousModeFrame_.numPartials;
        int maxPartials = std::max(numNew, numOld);
        maxPartials = std::min(maxPartials, static_cast<int>(Krate::DSP::kMaxPartials));

        for (int i = 0; i < maxPartials; ++i)
        {
            float newAmp = (i < numNew) ? latestFrame_.partials[i].amplitude : 0.0f;
            float oldAmp = (i < numOld) ? previousModeFrame_.partials[i].amplitude : 0.0f;
            latestFrame_.partials[i].amplitude = oldAmp * fadeOld + newAmp * fadeNew;
        }
        latestFrame_.numPartials = maxPartials;

        // Also crossfade global amplitude
        latestFrame_.globalAmplitude =
            previousModeFrame_.globalAmplitude * fadeOld +
            latestFrame_.globalAmplitude * fadeNew;

        --modeSwitchCrossfadeRemaining_;
    }

    newFrameAvailable_ = true;
}

// ==============================================================================
// Monophonic Analysis (original behavior)
// ==============================================================================
void LiveAnalysisPipeline::runMonophonicAnalysis(
    const Krate::DSP::F0Estimate& f0, float inputRms)
{
    // Run partial tracker
    tracker_.processFrame(
        shortSpectrum_,
        f0,
        kShortWindowConfig.fftSize,
        sampleRate_);

    // Build harmonic model
    latestFrame_ = modelBuilder_.build(
        tracker_.getPartials(),
        tracker_.getActiveCount(),
        f0,
        inputRms);

    // Spectral coring residual estimation
    if (residualEnabled_)
    {
        latestResidualFrame_ = coringEstimator_.estimateResidual(
            shortSpectrum_, latestFrame_);
    }
    else
    {
        latestResidualFrame_ = {};
    }

    // Clear polyphonic frame in mono mode
    latestPolyFrame_ = {};
    latestPolyFrame_.numSources = 1;
    latestPolyFrame_.sources[0] = latestFrame_;
    latestPolyFrame_.f0s.numDetected = 1;
    latestPolyFrame_.f0s.estimates[0] = f0;
    latestPolyFrame_.globalAmplitude = latestFrame_.globalAmplitude;
}

// ==============================================================================
// Polyphonic Analysis
// ==============================================================================
void LiveAnalysisPipeline::runPolyphonicAnalysis(
    const Krate::DSP::F0Estimate& yinF0, float inputRms)
{
    // Step 1: Run partial tracker with YIN F0 (still needed for peak detection)
    // Use a neutral F0 so the sieve doesn't constrain peak assignment
    Krate::DSP::F0Estimate neutralF0{};
    neutralF0.frequency = yinF0.frequency;
    neutralF0.confidence = yinF0.confidence;
    neutralF0.voiced = false; // Don't apply monophonic sieve

    tracker_.processFrame(
        shortSpectrum_,
        neutralF0,
        kShortWindowConfig.fftSize,
        sampleRate_);

    // Step 2: Multi-pitch detection from the tracker's detected peaks
    // We need the raw peak data. Since PartialTracker now tracks peaks,
    // we extract frequencies and amplitudes from the tracked partials.
    const auto& trackedPartials = tracker_.getPartials();
    int numTracked = tracker_.getActiveCount();

    // Collect frequencies and amplitudes for multi-pitch detection
    std::array<float, Krate::DSP::kMaxPartials> peakFreqs{};
    std::array<float, Krate::DSP::kMaxPartials> peakAmps{};
    for (int i = 0; i < numTracked; ++i) {
        peakFreqs[static_cast<size_t>(i)] = trackedPartials[static_cast<size_t>(i)].frequency;
        peakAmps[static_cast<size_t>(i)] = trackedPartials[static_cast<size_t>(i)].amplitude;
    }

    auto multiF0 = multiPitchDetector_.detect(
        peakFreqs.data(), peakAmps.data(), numTracked);

    // Step 3: If no F0s detected, fall back to mono path
    if (multiF0.numDetected == 0) {
        runMonophonicAnalysis(yinF0, inputRms);
        return;
    }

    // Step 4: Multi-source sieve - assign partials to sources
    auto partials = trackedPartials; // Copy so we can modify sourceId
    multiSourceSieve_.assignSources(partials, numTracked, multiF0);

    // Step 5: Build polyphonic frame
    latestPolyFrame_ = multiSourceSieve_.buildPolyphonicFrame(
        partials, numTracked, multiF0, inputRms);

    // Step 6: Run HarmonicModelBuilder on each source independently
    for (int s = 0; s < multiF0.numDetected; ++s) {
        auto& srcFrame = latestPolyFrame_.sources[static_cast<size_t>(s)];
        // Build smoothed model for this source
        // Note: We use the same model builder for simplicity;
        // in a future upgrade, each source could have its own builder
        // for independent smoothing timescales.
        Krate::DSP::F0Estimate srcF0;
        srcF0.frequency = multiF0.estimates[static_cast<size_t>(s)].frequency;
        srcF0.confidence = multiF0.estimates[static_cast<size_t>(s)].confidence;
        srcF0.voiced = true;

        // For the first/primary source, use the main model builder
        if (s == 0) {
            srcFrame = modelBuilder_.build(
                srcFrame.partials, srcFrame.numPartials, srcF0, inputRms);
        }
        // Secondary sources get basic frame data without full model building
        // (to keep CPU budget manageable)
    }

    // The primary (strongest) source becomes the latestFrame_ for
    // backward compatibility with mono consumers
    if (multiF0.numDetected > 0) {
        latestFrame_ = latestPolyFrame_.sources[0];
    } else {
        latestFrame_ = {};
    }

    // Spectral coring residual estimation (based on primary source)
    if (residualEnabled_)
    {
        latestResidualFrame_ = coringEstimator_.estimateResidual(
            shortSpectrum_, latestFrame_);
    }
    else
    {
        latestResidualFrame_ = {};
    }
}

} // namespace Innexus
