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
#include <krate/dsp/processors/multi_pitch_detector.h>
#include <krate/dsp/processors/subharmonic_validator.h>
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

    // DFT amplitude normalization: convert raw magnitudes to sinusoidal amplitudes.
    // Factor = 2 / (N * windowCoherentGain).  Without this, partial amplitudes
    // are ~N*CG/2 times too large, causing the additive oscillator bank to clip.
    {
        float cg = Krate::DSP::Window::coherentGain(kShortWindowConfig.windowType);
        float ampScale = 2.0f / (static_cast<float>(kShortWindowConfig.fftSize) * cg);
        tracker.setAmplitudeScale(ampScale);
    }

    // Harmonic model builder (FR-029 to FR-034)
    Krate::DSP::HarmonicModelBuilder modelBuilder;
    modelBuilder.prepare(static_cast<double>(sampleRate));
    modelBuilder.setHopSize(static_cast<int>(kShortWindowConfig.hopSize));

    // Subharmonic validator: corrects YIN octave errors using spectral
    // evidence (Hermes 1988 subharmonic summation).  Applied before the
    // tracker so the harmonic sieve gets a correct F0 from the start.
    Krate::DSP::SubharmonicValidator subharmonicValidator;
    subharmonicValidator.prepare(kShortWindowConfig.fftSize,
                                  static_cast<double>(sampleRate));

    // Multi-pitch detection: finds the strongest F0 from spectral peaks
    // when YIN's estimate may be unreliable (polyphonic content, low confidence).
    Krate::DSP::MultiPitchDetector multiPitchDetector;
    multiPitchDetector.prepare(kShortWindowConfig.fftSize,
                               static_cast<double>(sampleRate));

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

            // --- F0 correction pipeline ---
            // YIN is prone to severe subharmonic errors on polyphonic content
            // (e.g. detecting 56 Hz for a guitar chord at 415 Hz).  We fix
            // this in two stages BEFORE the partial tracker runs, so the
            // tracker's harmonic sieve gets the correct F0 from the start.
            //
            // Stage 1: Subharmonic summation (Hermes 1988) — fixes octave errors
            if (f0.voiced) {
                f0 = subharmonicValidator.validate(f0, shortSpectrum);
            }

            // Stage 2: Multi-pitch detection on raw spectral peaks.
            // Extract peaks directly from the STFT (independently of the
            // tracker) and find the strongest F0.  This catches non-octave
            // errors that the subharmonic validator misses.
            {
                const size_t numBins = shortSpectrum.numBins();
                const float binHz = sampleRate / static_cast<float>(
                    kShortWindowConfig.fftSize);

                // Simple peak detection on the magnitude spectrum
                std::array<float, Krate::DSP::kMaxPartials> rawPeakFreqs{};
                std::array<float, Krate::DSP::kMaxPartials> rawPeakAmps{};
                int numRawPeaks = 0;

                for (size_t b = 2; b + 2 < numBins &&
                     numRawPeaks < static_cast<int>(Krate::DSP::kMaxPartials); ++b)
                {
                    float mag = shortSpectrum.getMagnitude(b);
                    float magL = shortSpectrum.getMagnitude(b - 1);
                    float magR = shortSpectrum.getMagnitude(b + 1);

                    if (mag > magL && mag > magR && mag > 1e-8f)
                    {
                        // Parabolic interpolation for sub-bin precision
                        float denom = magL - 2.0f * mag + magR;
                        float interpBin = static_cast<float>(b);
                        float peakAmp = mag;
                        if (denom != 0.0f)
                        {
                            float delta = 0.5f * (magL - magR) / denom;
                            interpBin += delta;
                            peakAmp = mag - 0.25f * (magL - magR) * delta;
                        }

                        auto idx = static_cast<size_t>(numRawPeaks);
                        rawPeakFreqs[idx] = interpBin * binHz;
                        rawPeakAmps[idx] = peakAmp;
                        numRawPeaks++;
                    }
                }

                if (numRawPeaks > 0)
                {
                    auto multiF0 = multiPitchDetector.detect(
                        rawPeakFreqs.data(), rawPeakAmps.data(), numRawPeaks);

                    // The multi-pitch detector's salience function is more
                    // robust than YIN for polyphonic content.  If it found
                    // an F0, use it — even if YIN disagrees.
                    if (multiF0.numDetected >= 1)
                    {
                        f0.frequency = multiF0.estimates[0].frequency;
                        f0.confidence = std::max(
                            f0.confidence, multiF0.estimates[0].confidence);
                        f0.voiced = true;
                    }
                }
            }

            // Partial tracking with the corrected F0 (FR-022 to FR-028)
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

    // --- Post-analysis F0 stabilization ---
    // Per-frame F0 estimation can vary wildly, especially on polyphonic
    // content.  Compute the median F0 across voiced frames and check if
    // it differs significantly from individual frames.  If the F0 variance
    // is high (polyphonic or unstable content), lock all frames to the
    // median.  If stable (monophonic), leave frames as-is.
    if (!analysis->frames.empty())
    {
        // Collect all voiced F0 values
        std::vector<float> voicedF0s;
        voicedF0s.reserve(analysis->frames.size());
        for (const auto& fr : analysis->frames)
        {
            if (fr.f0 > 0.0f && fr.f0Confidence > 0.3f)
                voicedF0s.push_back(fr.f0);
        }

        if (voicedF0s.size() >= 3)
        {
            // Compute median F0
            std::sort(voicedF0s.begin(), voicedF0s.end());
            float medianF0 = voicedF0s[voicedF0s.size() / 2];

            // Check F0 stability: ratio of IQR to median.
            // Stable monophonic signals have <5% variance.
            // Polyphonic/unstable signals have much higher variance.
            float q1 = voicedF0s[voicedF0s.size() / 4];
            float q3 = voicedF0s[voicedF0s.size() * 3 / 4];
            float iqrRatio = (q3 - q1) / std::max(medianF0, 1.0f);

            constexpr float kInstabilityThreshold = 0.1f; // 10% IQR/median

            if (iqrRatio > kInstabilityThreshold)
            {
                // High F0 variance: lock all frames to the median.
                // This stabilizes polyphonic content where per-frame F0
                // jumps between different chord tones or subharmonics.
                for (auto& fr : analysis->frames)
                {
                    fr.f0 = medianF0;
                    fr.f0Confidence = 1.0f;

                    for (int i = 0; i < fr.numPartials; ++i)
                    {
                        auto& p = fr.partials[static_cast<size_t>(i)];
                        float ratio = p.frequency / medianF0;
                        p.harmonicIndex = std::max(1,
                            static_cast<int>(std::round(ratio)));
                        p.relativeFrequency = ratio;
                        p.inharmonicDeviation =
                            ratio - static_cast<float>(p.harmonicIndex);
                    }
                }
            }
        }
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
