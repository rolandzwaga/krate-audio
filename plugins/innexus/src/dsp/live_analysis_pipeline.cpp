// ==============================================================================
// LiveAnalysisPipeline Implementation
// ==============================================================================
// Phase 4: Full implementation of the real-time analysis pipeline.
//
// Orchestrates: PreProcessingPipeline -> YinPitchDetector -> STFT ->
// PartialTracker -> HarmonicModelBuilder -> SpectralCoringEstimator
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

    // Harmonic model builder
    modelBuilder_.prepare(sampleRate);
    modelBuilder_.setHopSize(static_cast<int>(kShortWindowConfig.hopSize));

    // Spectral coring estimator
    coringEstimator_.prepare(kShortWindowConfig.fftSize, sampleRate_);

    // YIN circular buffer
    yinBuffer_.resize(yinWindowSize_, 0.0f);
    yinContiguousBuffer_.resize(yinWindowSize_, 0.0f);
    yinWriteIndex_ = 0;
    yinBufferFilled_ = false;

    // Reset output state
    latestFrame_ = {};
    latestResidualFrame_ = {};
    newFrameAvailable_ = false;

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
    latestResidualFrame_ = {};
    newFrameAvailable_ = false;
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
        // The most recent yinWindowSize_ samples are:
        //   from yinWriteIndex_ to end, then from 0 to yinWriteIndex_
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

    newFrameAvailable_ = true;
}

} // namespace Innexus
