// ==============================================================================
// Innexus - Sample Analyzer Implementation
// ==============================================================================
// Implements background-thread audio file loading and harmonic analysis.
//
// DR_WAV_IMPLEMENTATION is defined here (and ONLY here) to include the
// dr_wav implementation. This is the single .cpp file requirement.
//
// The analysis pipeline mirrors what would be used for live analysis (FR-045):
//   1. Load WAV/AIFF via dr_wav (FR-043)
//   2. Stereo-to-mono downmix if needed
//   3. PreProcessingPipeline (DC block, HPF, transient suppression, noise gate)
//   4. YIN pitch detection (per short-window hop)
//   5. Dual-window STFT (short + long)
//   6. PartialTracker (peak detection + harmonic sieve + tracking)
//   7. HarmonicModelBuilder (smoothing, L2 norm, centroid, noisiness)
//
// Reference: spec.md FR-043 to FR-047, FR-058
// ==============================================================================

// Suppress warnings from third-party dr_wav header
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4244) // conversion from 'drwav_uint64' to 'drwav_uint32'
#endif

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#include "sample_analyzer.h"
#include "envelope_detector.h"
#include "dual_stft_config.h"
#include "pre_processing_pipeline.h"

#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/partial_tracker.h>
#include <krate/dsp/processors/residual_analyzer.h>
#include <krate/dsp/processors/yin_pitch_detector.h>
#include <krate/dsp/systems/harmonic_model_builder.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace Innexus {

// ==============================================================================
// Destructor
// ==============================================================================
SampleAnalyzer::~SampleAnalyzer()
{
    cancel();
    joinThread();
}

// ==============================================================================
// Start Analysis
// ==============================================================================
void SampleAnalyzer::startAnalysis(const std::string& filePath)
{
    // Cancel any ongoing analysis
    cancel();
    joinThread();

    // Reset state
    complete_.store(false);
    cancelled_.store(false);
    result_.reset();

    // Load audio file using dr_wav (FR-043)
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drwav_uint64 totalFrameCount = 0;

    float* rawData = drwav_open_file_and_read_pcm_frames_f32(
        filePath.c_str(), &channels, &sampleRate, &totalFrameCount, nullptr);

    if (!rawData || totalFrameCount == 0 || channels == 0) {
        if (rawData) {
            drwav_free(rawData, nullptr);
        }
        complete_.store(true);
        return;
    }

    // Convert to mono if stereo (FR-043: stereo-to-mono downmix)
    std::vector<float> monoData;
    if (channels == 1) {
        monoData.assign(rawData, rawData + totalFrameCount);
    } else {
        // Average all channels to mono
        monoData.resize(static_cast<size_t>(totalFrameCount));
        const float invChannels = 1.0f / static_cast<float>(channels);
        for (size_t i = 0; i < static_cast<size_t>(totalFrameCount); ++i) {
            float sum = 0.0f;
            for (unsigned int ch = 0; ch < channels; ++ch) {
                sum += rawData[i * channels + ch];
            }
            monoData[i] = sum * invChannels;
        }
    }

    drwav_free(rawData, nullptr);

    auto sr = static_cast<float>(sampleRate);

    // Launch background analysis thread (FR-044)
    analysisThread_ = std::thread(
        &SampleAnalyzer::analyzeOnThread, this,
        std::move(monoData), sr, filePath);
}

// ==============================================================================
// Is Complete
// ==============================================================================
bool SampleAnalyzer::isComplete() const noexcept
{
    return complete_.load(std::memory_order_acquire);
}

// ==============================================================================
// Take Result
// ==============================================================================
// NOLINTNEXTLINE(readability-make-member-function-const) -- moves result_, modifying state
std::unique_ptr<SampleAnalysis> SampleAnalyzer::takeResult()
{
    if (!isComplete()) {
        return nullptr;
    }
    return std::move(result_);
}

// ==============================================================================
// Cancel
// ==============================================================================
void SampleAnalyzer::cancel()
{
    cancelled_.store(true, std::memory_order_release);
}

// ==============================================================================
// Join Thread
// ==============================================================================
void SampleAnalyzer::joinThread()
{
    if (analysisThread_.joinable()) {
        analysisThread_.join();
    }
}

// ==============================================================================
// Background Analysis Thread (FR-045)
// ==============================================================================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- accesses cancelled_ and result_ members
void SampleAnalyzer::analyzeOnThread(
    std::vector<float> audioData,
    float sampleRate,
    std::string filePath)
{
    const size_t totalSamples = audioData.size();
    if (totalSamples == 0) {
        complete_.store(true, std::memory_order_release);
        return;
    }

    // --- Initialize pipeline components (FR-045: same code path) ---

    // Pre-processing (FR-005 to FR-009)
    PreProcessingPipeline preProcessing;
    preProcessing.prepare(static_cast<double>(sampleRate));

    // YIN pitch detector (FR-010 to FR-017)
    // Window size should be large enough for lowest pitch (40 Hz)
    // At 44.1kHz, 40 Hz = 1102.5 samples period, need 2x = 2205
    // Use 2048 as reasonable window size
    constexpr size_t kYinWindowSize = 2048;
    Krate::DSP::YinPitchDetector yin(kYinWindowSize);
    yin.prepare(static_cast<double>(sampleRate));

    // Dual STFT (FR-018 to FR-021)
    Krate::DSP::STFT shortStft;
    shortStft.prepare(
        kShortWindowConfig.fftSize,
        kShortWindowConfig.hopSize,
        kShortWindowConfig.windowType);

    Krate::DSP::STFT longStft;
    longStft.prepare(
        kLongWindowConfig.fftSize,
        kLongWindowConfig.hopSize,
        kLongWindowConfig.windowType);

    // Spectral buffers for STFT output
    Krate::DSP::SpectralBuffer shortSpectrum;
    shortSpectrum.prepare(kShortWindowConfig.fftSize);

    Krate::DSP::SpectralBuffer longSpectrum;
    longSpectrum.prepare(kLongWindowConfig.fftSize);

    // Partial tracker (FR-022 to FR-028)
    Krate::DSP::PartialTracker tracker;
    tracker.prepare(kShortWindowConfig.fftSize, static_cast<double>(sampleRate));

    // Harmonic model builder (FR-029 to FR-034)
    Krate::DSP::HarmonicModelBuilder modelBuilder;
    modelBuilder.prepare(static_cast<double>(sampleRate));
    modelBuilder.setHopSize(static_cast<int>(kShortWindowConfig.hopSize));

    // Residual analyzer (FR-009: runs on background thread, FR-010: never audio thread)
    Krate::DSP::ResidualAnalyzer residualAnalyzer;
    residualAnalyzer.prepare(
        kShortWindowConfig.fftSize, kShortWindowConfig.hopSize, sampleRate);

    // --- Prepare output ---
    auto analysis = std::make_unique<SampleAnalysis>();
    analysis->sampleRate = sampleRate;
    analysis->hopTimeSec = static_cast<float>(kShortWindowConfig.hopSize) / sampleRate;
    analysis->filePath = std::move(filePath);
    analysis->analysisFFTSize = kShortWindowConfig.fftSize;
    analysis->analysisHopSize = kShortWindowConfig.hopSize;

    // Reserve expected frame count
    const size_t expectedFrames = totalSamples / kShortWindowConfig.hopSize;
    analysis->frames.reserve(expectedFrames + 1);
    analysis->residualFrames.reserve(expectedFrames + 1);

    // --- Process audio in hop-sized chunks ---
    const size_t shortHop = kShortWindowConfig.hopSize;
    const size_t longHopRatio = kLongWindowConfig.hopSize / shortHop; // 2048/512 = 4
    size_t shortHopCounter = 0;

    // Pre-processing buffer (process in small blocks)
    constexpr size_t kProcessBlockSize = 512;
    std::vector<float> processBuffer(kProcessBlockSize);

    // Feed audio through pipeline in blocks
    size_t sampleIndex = 0;

    while (sampleIndex < totalSamples) {
        if (cancelled_.load(std::memory_order_acquire)) {
            complete_.store(true, std::memory_order_release);
            return;
        }

        // Process one block of samples
        const size_t remaining = totalSamples - sampleIndex;
        const size_t blockSize = std::min(remaining, kProcessBlockSize);

        // Copy to processing buffer and apply pre-processing (FR-009: separate path)
        std::memcpy(processBuffer.data(), &audioData[sampleIndex],
                     blockSize * sizeof(float));
        preProcessing.processBlock(processBuffer.data(), blockSize);

        // Feed processed samples to both STFTs
        shortStft.pushSamples(processBuffer.data(), blockSize);
        longStft.pushSamples(processBuffer.data(), blockSize);

        // Check if short STFT has enough data for analysis
        while (shortStft.canAnalyze()) {
            // Run short STFT analysis
            shortStft.analyze(shortSpectrum);

            // Run long STFT analysis if it's time (every longHopRatio short hops)
            if (longStft.canAnalyze() && (shortHopCounter % longHopRatio == 0)) {
                longStft.analyze(longSpectrum);
            }

            // Compute RMS of the current hop region for model builder
            // Use the original audio data (before pre-processing) for RMS
            size_t hopStart = sampleIndex;
            if (hopStart >= shortHop) {
                hopStart = hopStart - shortHop + blockSize;
            }
            // Use the pre-processed data for RMS computation
            float rmsSum = 0.0f;
            const size_t rmsStart = (sampleIndex + blockSize >= shortHop)
                ? sampleIndex + blockSize - shortHop
                : 0;
            const size_t rmsEnd = std::min(rmsStart + shortHop, totalSamples);
            for (size_t i = rmsStart; i < rmsEnd; ++i) {
                rmsSum += audioData[i] * audioData[i];
            }
            const float inputRms = std::sqrt(
                rmsSum / static_cast<float>(std::max(rmsEnd - rmsStart, size_t(1))));

            // YIN pitch detection on the current window
            // Need to provide windowSize samples centered around the current hop
            const size_t currentPos = sampleIndex + blockSize;
            size_t yinStart = 0;
            if (currentPos >= kYinWindowSize) {
                yinStart = currentPos - kYinWindowSize;
            }
            const size_t yinLength = std::min(kYinWindowSize,
                                               totalSamples - yinStart);

            Krate::DSP::F0Estimate f0;
            if (yinLength >= kYinWindowSize) {
                f0 = yin.detect(&audioData[yinStart], yinLength);
            }

            // Partial tracking on short window spectrum (FR-022 to FR-028)
            tracker.processFrame(shortSpectrum, f0,
                                  kShortWindowConfig.fftSize, sampleRate);

            // Build harmonic frame (FR-029 to FR-034)
            Krate::DSP::HarmonicFrame frame = modelBuilder.build(
                tracker.getPartials(),
                tracker.getActiveCount(),
                f0,
                inputRms);

            analysis->frames.push_back(frame);

            // Residual analysis: extract stochastic component (FR-009)
            // Get the original audio segment for this frame
            const size_t frameStart = (sampleIndex + blockSize >= kShortWindowConfig.fftSize)
                ? sampleIndex + blockSize - kShortWindowConfig.fftSize
                : 0;
            const size_t frameSamples = std::min(kShortWindowConfig.fftSize,
                                                  totalSamples - frameStart);
            if (frameSamples >= kShortWindowConfig.fftSize)
            {
                auto residualFrame = residualAnalyzer.analyzeFrame(
                    &audioData[frameStart], frameSamples, frame);
                analysis->residualFrames.push_back(residualFrame);
            }
            else
            {
                // Not enough samples for a full frame -- push a silent residual frame
                analysis->residualFrames.push_back(Krate::DSP::ResidualFrame{});
            }

            shortHopCounter++;
        }

        sampleIndex += blockSize;
    }

    // Finalize
    analysis->totalFrames = analysis->frames.size();

    // Spec 124 FR-001, FR-002: Detect ADSR envelope from amplitude contour.
    // This runs on the analysis thread (not audio thread), so allocation is safe.
    // Note: sidechain mode (FR-022) is handled by the caller -- sidechain input
    // goes through the live analysis pipeline, not through SampleAnalyzer.
    if (!analysis->frames.empty() && analysis->hopTimeSec > 0.0f)
    {
        analysis->detectedADSR = EnvelopeDetector::detect(
            analysis->frames, analysis->hopTimeSec);
    }

    // Publish result
    result_ = std::move(analysis);
    complete_.store(true, std::memory_order_release);
}

} // namespace Innexus
