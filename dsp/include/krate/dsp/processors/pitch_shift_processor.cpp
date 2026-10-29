// pitch_shift_processor.cpp
// Out-of-line definitions for the largest PitchShift* methods, moved out of the
// header (D4) to shrink a 2000-line multi-class header. Layer-2, .cpp precedent:
// core/spectral_simd.cpp. These per-block processing loops are not inline-eligible,
// so moving them costs nothing at runtime.
#include <krate/dsp/processors/pitch_shift_processor.h>

namespace Krate::DSP {

void SimplePitchShifter::process(const float* input, float* output, std::size_t numSamples,
                 float pitchRatio) noexcept {
        // At unity pitch, just pass through
        // Check both target and current smoothed value (or uninitialized state)
        const bool targetIsUnity = std::abs(pitchRatio - 1.0f) < 0.0001f;
        const bool smoothedIsUnity = !smoothedRatioInitialized_ ||
                                     std::abs(smoothedRatio_ - 1.0f) < 0.0001f;
        if (targetIsUnity && smoothedIsUnity) {
            if (input != output) {
                std::copy(input, input + numSamples, output);
            }
            return;
        }

        // Delay-based pitch shifter using Doppler effect:
        //
        // Key physics: ω_out = ω_in × (1 - dDelay/dt)
        // For pitch ratio R: dDelay/dt = 1 - R
        //
        // R = 2.0: delay decreases by 1 sample/sample (pitch UP)
        // R = 0.5: delay increases by 0.5 samples/sample (pitch DOWN)
        //
        // Algorithm:
        // 1. Delay1 is the "active" delay, ramping in the appropriate direction
        // 2. When delay1 approaches its limit, reset delay2 to the START and crossfade
        // 3. After crossfade completes, delay2 becomes active (swap roles)
        // 4. Repeat
        //
        // Per-sample smoothing of pitchRatio prevents clicks during parameter changes

        const float bufferSizeF = static_cast<float>(bufferSize_);

        // Crossfade over ~25% of the delay range for smooth transitions
        const float crossfadeLength = maxDelay_ * 0.25f;
        const float crossfadeRate = 1.0f / crossfadeLength;

        // Threshold for triggering crossfade (when delay gets close to limit)
        const float triggerThreshold = crossfadeLength;

        for (std::size_t i = 0; i < numSamples; ++i) {
            // Per-sample smoothing of pitch ratio to prevent clicks
            // On first use, snap to target to avoid startup transients
            if (!smoothedRatioInitialized_) {
                smoothedRatio_ = pitchRatio;
                smoothedRatioInitialized_ = true;
            } else {
                smoothedRatio_ += ratioSmoothCoeff_ * (pitchRatio - smoothedRatio_);
            }
            const float delayChange = 1.0f - smoothedRatio_;  // Negative for pitch up

            // Write input to buffer
            buffer_[writePos_] = input[i];

            // Read from both delay taps
            float readPos1 = static_cast<float>(writePos_) - delay1_;
            float readPos2 = static_cast<float>(writePos_) - delay2_;

            // Wrap to valid buffer range
            if (readPos1 < 0.0f) readPos1 += bufferSizeF;
            if (readPos2 < 0.0f) readPos2 += bufferSizeF;

            float sample1 = readInterpolated(readPos1);
            float sample2 = readInterpolated(readPos2);

            // Half-sine crossfade for constant power
            float gain1 = std::cos(crossfadePhase_ * kPi * 0.5f);
            float gain2 = std::sin(crossfadePhase_ * kPi * 0.5f);

            output[i] = sample1 * gain1 + sample2 * gain2;

            // Update the active delay (always delay1 conceptually, but we swap)
            delay1_ += delayChange;
            delay2_ += delayChange;

            // Check if we need to start a crossfade
            if (!needsCrossfade_) {
                // For pitch UP (delayChange < 0): delay decreases toward minDelay_
                // For pitch DOWN (delayChange > 0): delay increases toward maxDelay_
                bool approachingLimit = (delayChange < 0.0f && delay1_ <= minDelay_ + triggerThreshold) ||
                                        (delayChange > 0.0f && delay1_ >= maxDelay_ - triggerThreshold);

                if (approachingLimit) {
                    // Reset delay2 to the START of the cycle
                    delay2_ = (smoothedRatio_ > 1.0f) ? maxDelay_ : minDelay_;
                    needsCrossfade_ = true;
                }
            }

            // Manage crossfade
            if (needsCrossfade_) {
                crossfadePhase_ += crossfadeRate;

                if (crossfadePhase_ >= 1.0f) {
                    // Crossfade complete - swap delays
                    crossfadePhase_ = 0.0f;
                    needsCrossfade_ = false;

                    // Swap delay1 and delay2 (delay2 becomes the new active)
                    std::swap(delay1_, delay2_);
                }
            }

            // Clamp delays to valid range (safety, shouldn't normally hit this)
            delay1_ = std::clamp(delay1_, minDelay_, maxDelay_);
            delay2_ = std::clamp(delay2_, minDelay_, maxDelay_);

            // Advance write position
            writePos_ = (writePos_ + 1) % bufferSize_;
        }
    }

void GranularPitchShifter::process(const float* input, float* output, std::size_t numSamples,
                 float pitchRatio) noexcept {
        // At unity pitch, pass through
        if (std::abs(pitchRatio - 1.0f) < 0.0001f) {
            if (input != output) {
                std::copy(input, input + numSamples, output);
            }
            return;
        }

        pitchRatio = std::clamp(pitchRatio, 0.25f, 4.0f);

        const float delayChange = 1.0f - pitchRatio;
        const float bufferSizeF = static_cast<float>(bufferSize_);

        // Longer crossfade (33% of delay range) for smoother transitions
        const float crossfadeLength = maxDelay_ * 0.33f;
        const float crossfadeRate = 1.0f / crossfadeLength;
        const float triggerThreshold = crossfadeLength;

        for (std::size_t i = 0; i < numSamples; ++i) {
            buffer_[writePos_] = input[i];

            // Read from both delay taps
            float readPos1 = static_cast<float>(writePos_) - delay1_;
            float readPos2 = static_cast<float>(writePos_) - delay2_;

            if (readPos1 < 0.0f) readPos1 += bufferSizeF;
            if (readPos2 < 0.0f) readPos2 += bufferSizeF;

            float sample1 = readInterpolated(readPos1);
            float sample2 = readInterpolated(readPos2);

            // Hann window crossfade (smoother than half-sine)
            // Map crossfadePhase [0,1] to Hann window index
            auto fadeIdx = static_cast<std::size_t>(crossfadePhase_ *
                                  static_cast<float>(crossfadeWindowSize_));
            if (fadeIdx >= crossfadeWindowSize_) fadeIdx = crossfadeWindowSize_ - 1;

            // Hann window goes 0 -> 1 over first half
            float gain2 = crossfadeWindow_[fadeIdx];
            float gain1 = 1.0f - gain2;

            output[i] = sample1 * gain1 + sample2 * gain2;

            // Update both delays
            delay1_ += delayChange;
            delay2_ += delayChange;

            // Check if we need to start a crossfade
            if (!needsCrossfade_) {
                bool approachingLimit = (delayChange < 0.0f && delay1_ <= minDelay_ + triggerThreshold) ||
                                        (delayChange > 0.0f && delay1_ >= maxDelay_ - triggerThreshold);

                if (approachingLimit) {
                    delay2_ = (pitchRatio > 1.0f) ? maxDelay_ : minDelay_;
                    needsCrossfade_ = true;
                }
            }

            // Manage crossfade
            if (needsCrossfade_) {
                crossfadePhase_ += crossfadeRate;

                if (crossfadePhase_ >= 1.0f) {
                    crossfadePhase_ = 0.0f;
                    needsCrossfade_ = false;
                    std::swap(delay1_, delay2_);
                }
            }

            delay1_ = std::clamp(delay1_, minDelay_, maxDelay_);
            delay2_ = std::clamp(delay2_, minDelay_, maxDelay_);

            writePos_ = (writePos_ + 1) % bufferSize_;
        }
    }

void PitchSyncGranularShifter::processWithSharedPitch(const float* input, float* output,
                                std::size_t numSamples, float pitchRatio,
                                float sharedPeriod,
                                float sharedConfidence) noexcept {
        // At unity pitch, pass through
        if (std::abs(pitchRatio - 1.0f) < 0.0001f) {
            if (input != output) {
                std::copy(input, input + numSamples, output);
            }
            return;
        }

        pitchRatio = std::clamp(pitchRatio, 0.25f, 4.0f);

        const float delayChange = 1.0f - pitchRatio;
        const float bufferSizeF = static_cast<float>(bufferSize_);

        for (std::size_t i = 0; i < numSamples; ++i) {
            // Write to buffer (still needed for granular read-back)
            buffer_[writePos_] = input[i];

            // Use shared pitch detection results instead of internal detector
            updateGrainSizeFromPeriod(sharedPeriod, sharedConfidence);

            processGranularSample(output, i, pitchRatio, delayChange, bufferSizeF);
        }
    }

void PitchSyncGranularShifter::processGranularSample(float* output, std::size_t i,
                               float pitchRatio, float delayChange,
                               float bufferSizeF) noexcept {
        // Crossfade parameters based on current grain size
        const float crossfadeLength = maxDelay_ * 0.4f;  // 40% crossfade
        // Above one octave up the taps move more than a sample per sample: shorten the
        // fade so the outgoing tap finishes it before it reaches the end of its range.
        const float crossfadeRate = std::max(1.0f, std::abs(delayChange)) / crossfadeLength;
        const float triggerThreshold = crossfadeLength;

        // A tap the grain shrank under (the detected period fell, and maxDelay_ with it) is
        // not pulled down to the new maximum: that jump of the read position clicks under the
        // tap's gain. It keeps moving at its rate, held only to the longest grain the buffer
        // serves, until it is back under maxDelay_. While the grain is steady no tap starts a
        // sample above maxDelay_, so nothing changes there.
        bool above1 = delay1_ > maxDelay_;
        bool above2 = delay2_ > maxDelay_;

        // Read from both delay taps
        float readPos1 = static_cast<float>(writePos_) - delay1_;
        float readPos2 = static_cast<float>(writePos_) - delay2_;

        if (readPos1 < 0.0f) readPos1 += bufferSizeF;
        if (readPos2 < 0.0f) readPos2 += bufferSizeF;

        float sample1 = readInterpolated(readPos1);
        float sample2 = readInterpolated(readPos2);

        // Hann window crossfade
        auto fadeIdx = static_cast<std::size_t>(crossfadePhase_ *
                              static_cast<float>(crossfadeWindowSize_));
        if (fadeIdx >= crossfadeWindowSize_) fadeIdx = crossfadeWindowSize_ - 1;

        float gain2 = crossfadeWindow_[fadeIdx];
        float gain1 = 1.0f - gain2;

        // Hold the level through the fade for how alike the taps are: the gains sum to
        // one in amplitude when they are in phase, and in power when they are unrelated
        // (a short note's head in one tap and its tail in the other).
        const float sumPower = gain1 * gain1 + gain2 * gain2
                             + 2.0f * spliceCorrelation_ * gain1 * gain2;
        output[i] = (sample1 * gain1 + sample2 * gain2) / std::sqrt(sumPower);

        // Update both delays
        delay1_ += delayChange;
        delay2_ += delayChange;

        // Check if we need to start a crossfade
        if (!needsCrossfade_) {
            bool approachingLimit = (delayChange < 0.0f && delay1_ <= minDelay_ + triggerThreshold) ||
                                    (delayChange > 0.0f && delay1_ >= maxDelay_ - triggerThreshold);

            if (approachingLimit) {
                const float target = (pitchRatio > 1.0f) ? maxDelay_ : minDelay_;
                beginSplice(target, crossfadeLength * std::min(1.0f, std::abs(delayChange)));
                above2 = false;  // a new incoming tap, placed under the grain
                needsCrossfade_ = true;
            }
        }

        // Manage crossfade
        if (needsCrossfade_) {
            crossfadePhase_ += crossfadeRate;

            if (crossfadePhase_ >= 1.0f) {
                crossfadePhase_ = 0.0f;
                needsCrossfade_ = false;
                std::swap(delay1_, delay2_);
                std::swap(above1, above2);
            }
        }

        const float longestGrain = static_cast<float>(maxGrainSamples_);
        delay1_ = std::clamp(delay1_, minDelay_, above1 ? longestGrain : maxDelay_);
        delay2_ = std::clamp(delay2_, minDelay_, above2 ? longestGrain : maxDelay_);

        writePos_ = (writePos_ + 1) % bufferSize_;
    }

void PitchSyncGranularShifter::beginSplice(float target, float travel) noexcept {
        // The incoming tap starts where the audio behind it best matches the audio
        // behind the outgoing tap (normalised cross-correlation), so a periodic input
        // stays in phase across the fade instead of cancelling itself. It must start at
        // least `travel` away so it is still clear of the end when the fade completes.
        // With nothing to match, it starts at the far end and the taps count as unrelated.
        delay2_ = target;
        spliceCorrelation_ = 0.0f;
        // An outgoing tap the grain shrank under (see processGranularSample) is above
        // maxDelay_; the incoming tap still starts under it.
        const float underGrain = (target < delay1_) ? std::max(0.0f, delay1_ - maxDelay_) : 0.0f;
        const auto minOffset = static_cast<std::size_t>(std::ceil(std::max(travel, underGrain)));
        const auto maxOffset = static_cast<std::size_t>(std::abs(target - delay1_));
        if (minOffset == 0 || minOffset > maxOffset) return;

        const std::size_t matchLength = std::clamp<std::size_t>(currentGrainSize_ / 2, 1, 256);
        const std::size_t n = bufferSize_;
        // Read position of the outgoing tap, and the step from it to a candidate
        // (older audio for a larger delay, newer for a smaller one).
        const auto base = (writePos_ + n - (static_cast<std::size_t>(delay1_) % n)) % n;
        const bool older = target > delay1_;
        auto at = [&](std::size_t offset, std::size_t back) {
            const std::size_t idx = older ? (base + 2 * n - offset - back) : (base + n + offset - back);
            return buffer_[idx % n];
        };

        double outgoingEnergy = 0.0;
        for (std::size_t j = 0; j < matchLength; ++j) {
            const float x = at(0, j);
            outgoingEnergy += static_cast<double>(x) * x;
        }
        if (outgoingEnergy <= 1e-12) return;

        std::size_t bestOffset = maxOffset;
        double bestScore = -2.0;
        for (std::size_t offset = minOffset; offset <= maxOffset; ++offset) {
            double dot = 0.0;
            double energy = 0.0;
            for (std::size_t j = 0; j < matchLength; ++j) {
                const float x = at(0, j);
                const float y = at(offset, j);
                dot += static_cast<double>(x) * y;
                energy += static_cast<double>(y) * y;
            }
            const double score = (energy > 1e-12) ? dot / std::sqrt(outgoingEnergy * energy) : -1.0;
            if (score > bestScore) {
                bestScore = score;
                bestOffset = offset;
            }
        }
        const float offset = static_cast<float>(bestOffset);
        delay2_ = older ? delay1_ + offset : delay1_ - offset;
        spliceCorrelation_ = static_cast<float>(std::clamp(bestScore, 0.0, 1.0));
    }

// prepare() allocates (vector::resize), so a std::bad_alloc could in principle
// escape a noexcept function and terminate. That is the intended contract
// throughout this library: prepare() is the non-realtime setup call, running out
// of memory while sizing the FFT buffers is not something a caller could
// meaningfully recover from, and every other prepare() in KrateDSP is noexcept
// for the same reason. Dropping noexcept here would break that uniformity for no
// gain.
// NOLINTNEXTLINE(bugprone-exception-escape)
void PhaseVocoderPitchShifter::prepare(double sampleRate, std::size_t /*maxBlockSize*/) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Prepare STFT analysis
        stft_.prepare(kFFTSize, kHopSize, WindowType::Hann);

        // Prepare overlap-add synthesis with synthesis windowing enabled
        // PV pitch shifting modifies the spectrum, so IFFT output needs windowing
        // to avoid boundary discontinuities. Hann² at 75% overlap is COLA-compliant.
        ola_.prepare(kFFTSize, kHopSize, WindowType::Hann, 9.0f, true);

        // Prepare spectral buffers
        analysisSpectrum_.prepare(kFFTSize);
        synthesisSpectrum_.prepare(kFFTSize);

        // Allocate phase tracking arrays
        const std::size_t numBins = kFFTSize / 2 + 1;
        prevPhase_.resize(numBins, 0.0f);
        synthPhase_.resize(numBins, 0.0f);
        magnitude_.resize(numBins, 0.0f);
        frequency_.resize(numBins, 0.0f);

        // Calculate expected phase advance per bin per hop
        // For bin k: expected_advance = 2π * k * hop_size / fft_size
        expectedPhaseInc_.resize(numBins);
        for (std::size_t k = 0; k < numBins; ++k) {
            expectedPhaseInc_[k] = kTwoPi * static_cast<float>(k) *
                                   static_cast<float>(kHopSize) /
                                   static_cast<float>(kFFTSize);
        }

        // Output buffer for resampled output
        outputBuffer_.resize(kFFTSize * 4, 0.0f);
        outputReadPos_ = 0;
        outputWritePos_ = 0;
        outputSamplesReady_ = 0;

        // Input buffer for accumulating samples
        inputBuffer_.resize(kFFTSize * 4, 0.0f);
        inputWritePos_ = 0;
        inputSamplesReady_ = 0;

        // Prepare formant preservation
        formantPreserver_.prepare(kFFTSize, sampleRate);
        originalEnvelope_.resize(numBins, 1.0f);
        shiftedEnvelope_.resize(numBins, 1.0f);
        shiftedMagnitude_.resize(numBins, 0.0f);

        // Prepare transient detector for phase reset
        transientDetector_.prepare(numBins);

        reset();
    }

void PhaseVocoderPitchShifter::process(const float* input, float* output, std::size_t numSamples,
                 float pitchRatio) noexcept {
        // At unity pitch, pass through (with latency compensation)
        if (std::abs(pitchRatio - 1.0f) < 0.0001f) {
            processUnityPitch(input, output, numSamples);
            return;
        }

        pitchRatio = std::clamp(pitchRatio, 0.25f, 4.0f);

        // Push input samples to STFT
        stft_.pushSamples(input, numSamples);

        // Process as many frames as possible
        while (stft_.canAnalyze()) {
            // Analyze frame
            stft_.analyze(analysisSpectrum_);

            // Phase vocoder pitch shift (FR-023: pass analysis/synthesis by reference)
            processFrame(analysisSpectrum_, synthesisSpectrum_, pitchRatio);

            // Synthesize frame
            ola_.synthesize(synthesisSpectrum_);
        }

        // Pull available output samples
        std::size_t samplesToOutput = std::min(numSamples, ola_.samplesAvailable());
        if (samplesToOutput > 0) {
            ola_.pullSamples(output, samplesToOutput);
        }

        // Zero any remaining output (during startup latency)
        for (std::size_t i = samplesToOutput; i < numSamples; ++i) {
            output[i] = 0.0f;
        }
    }

void PhaseVocoderPitchShifter::processFrame(const SpectralBuffer& analysis, SpectralBuffer& synthesis,
                      float pitchRatio) noexcept {
        const std::size_t numBins = kFFTSize / 2 + 1;

        // Step 1: Extract magnitude and compute instantaneous frequency
        for (std::size_t k = 0; k < numBins; ++k) {
            // Get magnitude and phase from the analysis parameter (not internal member)
            magnitude_[k] = analysis.getMagnitude(k);
            float phase = analysis.getPhase(k);

            // Compute phase difference from previous frame
            float phaseDiff = phase - prevPhase_[k];
            prevPhase_[k] = phase;

            // Subtract expected phase increment to get deviation
            float deviation = phaseDiff - expectedPhaseInc_[k];

            // Wrap deviation to [-pi, pi]
            deviation = wrapPhase(deviation);

            // Compute true frequency as deviation from bin center
            // true_freq = bin_freq + deviation / (2pi * hopSize / sampleRate)
            // But we store as phase per hop for synthesis
            frequency_[k] = expectedPhaseInc_[k] + deviation;
        }

        // Step 1b: Extract original spectral envelope if formant preservation enabled
        if (formantPreserve_) {
            formantPreserver_.extractEnvelope(magnitude_.data(), originalEnvelope_.data());
        }

        // Step 1b-reset: Transient detection and phase reset (FR-012)
        // Note: prevPhase_[k] already holds the current frame's analysis phase
        // (updated above at line: prevPhase_[k] = phase), which is correct for
        // phase reset per FR-012.
        if (phaseResetEnabled_) {
            const bool isTransient = transientDetector_.detect(magnitude_.data(), numBins);
            if (isTransient) {
                for (std::size_t k = 0; k < numBins; ++k) {
                    synthPhase_[k] = prevPhase_[k];
                }
            }
        }

        // Step 1c: Phase locking setup (peak detection + region assignment)
        if (phaseLockingEnabled_) {
            // Stage A: Peak detection in analysis-domain magnitude spectrum
            numPeaks_ = 0;
            // Clear only the bins we use (numBins, not kMaxBins)
            for (std::size_t k = 0; k < numBins; ++k) {
                isPeak_[k] = false;
            }

            for (std::size_t k = 1; k < numBins - 1 && numPeaks_ < kMaxPeaks; ++k) {
                if (magnitude_[k] > magnitude_[k - 1] && magnitude_[k] > magnitude_[k + 1]) {
                    isPeak_[k] = true;
                    peakIndices_[numPeaks_] = static_cast<uint16_t>(k);
                    ++numPeaks_;
                }
            }

            // Stage B: Region-of-influence assignment
            if (numPeaks_ > 0) {
                if (numPeaks_ == 1) {
                    // Single peak: all bins assigned to it
                    for (std::size_t k = 0; k < numBins; ++k) {
                        regionPeak_[k] = peakIndices_[0];
                    }
                } else {
                    // Forward scan: assign bins to peaks based on midpoint boundaries
                    std::size_t peakIdx = 0;
                    for (std::size_t k = 0; k < numBins; ++k) {
                        // Move to next peak if we've passed the midpoint
                        if (peakIdx + 1 < numPeaks_) {
                            uint16_t midpoint = static_cast<uint16_t>(
                                (peakIndices_[peakIdx] + peakIndices_[peakIdx + 1]) / 2);
                            if (k > midpoint) {
                                ++peakIdx;
                            }
                        }
                        regionPeak_[k] = peakIndices_[peakIdx];
                    }
                }
            }
        }

        // Toggle-to-basic re-initialization check
        if (wasLocked_ && !phaseLockingEnabled_) {
            for (std::size_t k = 0; k < numBins; ++k) {
                synthPhase_[k] = prevPhase_[k];
            }
        }
        wasLocked_ = phaseLockingEnabled_;

        // Step 2: Pitch shift by scaling frequencies and resampling spectrum
        synthesis.reset();

        if (phaseLockingEnabled_ && numPeaks_ > 0) {
            // Two-pass synthesis: peaks first, then non-peaks

            // Pass 1: Process PEAK bins only (accumulate synthPhase_ for peaks)
            for (std::size_t k = 0; k < numBins; ++k) {
                float srcBin = static_cast<float>(k) / pitchRatio;
                if (srcBin >= static_cast<float>(numBins - 1)) continue;

                auto srcBinRounded = static_cast<std::size_t>(srcBin + 0.5f);
                if (srcBinRounded >= numBins) srcBinRounded = numBins - 1;

                if (!isPeak_[srcBinRounded]) continue; // Skip non-peaks in Pass 1

                // Standard bin mapping and magnitude interpolation
                auto srcBin0 = static_cast<std::size_t>(srcBin);
                std::size_t srcBin1 = srcBin0 + 1;
                if (srcBin1 >= numBins) srcBin1 = numBins - 1;

                float frac = srcBin - static_cast<float>(srcBin0);
                float mag = magnitude_[srcBin0] * (1.0f - frac) + magnitude_[srcBin1] * frac;
                shiftedMagnitude_[k] = mag;

                // Peak bin: standard horizontal phase propagation
                float freq = frequency_[srcBin0] * pitchRatio;
                synthPhase_[k] += freq;
                synthPhase_[k] = wrapPhase(synthPhase_[k]);

                float real = mag * std::cos(synthPhase_[k]);
                float imag = mag * std::sin(synthPhase_[k]);
                synthesis.setCartesian(k, real, imag);
            }

            // Pass 2: Process NON-PEAK bins (use peak phases from Pass 1)
            for (std::size_t k = 0; k < numBins; ++k) {
                float srcBin = static_cast<float>(k) / pitchRatio;
                if (srcBin >= static_cast<float>(numBins - 1)) continue;

                auto srcBinRounded = static_cast<std::size_t>(srcBin + 0.5f);
                if (srcBinRounded >= numBins) srcBinRounded = numBins - 1;

                if (isPeak_[srcBinRounded]) continue; // Skip peaks in Pass 2

                // Standard bin mapping and magnitude interpolation
                auto srcBin0 = static_cast<std::size_t>(srcBin);
                std::size_t srcBin1 = srcBin0 + 1;
                if (srcBin1 >= numBins) srcBin1 = numBins - 1;

                float frac = srcBin - static_cast<float>(srcBin0);
                float mag = magnitude_[srcBin0] * (1.0f - frac) + magnitude_[srcBin1] * frac;
                shiftedMagnitude_[k] = mag;

                // Non-peak bin: identity phase locking via rotation angle
                uint16_t analysisPeak = regionPeak_[srcBinRounded];

                // Find the synthesis bin corresponding to the analysis peak
                auto synthPeakBin = static_cast<std::size_t>(
                    static_cast<float>(analysisPeak) * pitchRatio + 0.5f);
                if (synthPeakBin >= numBins) synthPeakBin = numBins - 1;

                // Rotation angle: peak's synthesis phase minus peak's analysis phase
                float analysisPhaseAtPeak = prevPhase_[analysisPeak];
                float rotationAngle = synthPhase_[synthPeakBin] - analysisPhaseAtPeak;

                // Apply rotation to this bin's analysis phase (interpolated)
                float analysisPhaseAtSrc = prevPhase_[srcBin0] * (1.0f - frac)
                                         + prevPhase_[srcBin1] * frac;
                float phaseForOutput = analysisPhaseAtSrc + rotationAngle;

                // Store in synthPhase_ for formant step compatibility
                synthPhase_[k] = phaseForOutput;

                float real = mag * std::cos(phaseForOutput);
                float imag = mag * std::sin(phaseForOutput);
                synthesis.setCartesian(k, real, imag);
            }
        } else {
            // Basic path: standard per-bin phase accumulation (pre-modification behavior)
            // Also used as fallback when phaseLockingEnabled_ && numPeaks_ == 0 (FR-011)
            for (std::size_t k = 0; k < numBins; ++k) {
                // Map source bin to destination bin
                float srcBin = static_cast<float>(k) / pitchRatio;

                // Skip if source bin is out of range
                if (srcBin >= static_cast<float>(numBins - 1)) continue;

                // Linear interpolation for magnitude
                auto srcBin0 = static_cast<std::size_t>(srcBin);
                std::size_t srcBin1 = srcBin0 + 1;
                if (srcBin1 >= numBins) srcBin1 = numBins - 1;

                float frac = srcBin - static_cast<float>(srcBin0);
                float mag = magnitude_[srcBin0] * (1.0f - frac) + magnitude_[srcBin1] * frac;

                // Store shifted magnitude for formant preservation
                shiftedMagnitude_[k] = mag;

                // Scale frequency by pitch ratio
                float freq = frequency_[srcBin0] * pitchRatio;

                // Accumulate synthesis phase
                synthPhase_[k] += freq;
                synthPhase_[k] = wrapPhase(synthPhase_[k]);

                // Set synthesis bin (Cartesian form)
                float real = mag * std::cos(synthPhase_[k]);
                float imag = mag * std::sin(synthPhase_[k]);
                synthesis.setCartesian(k, real, imag);
            }
        }

        // Step 3: Apply formant preservation if enabled
        if (formantPreserve_) {
            // Extract envelope of the shifted spectrum
            formantPreserver_.extractEnvelope(shiftedMagnitude_.data(), shiftedEnvelope_.data());

            // Apply formant preservation: adjust magnitudes to preserve original envelope
            for (std::size_t k = 0; k < numBins; ++k) {
                // Compute envelope ratio: originalEnv / shiftedEnv
                float shiftedEnv = std::max(shiftedEnvelope_[k], 1e-10f);
                float ratio = originalEnvelope_[k] / shiftedEnv;

                // Clamp ratio to avoid extreme amplification (especially at extreme shifts)
                ratio = std::min(ratio, 100.0f);
                ratio = std::max(ratio, 0.01f);

                // Apply ratio to shifted magnitude
                float adjustedMag = shiftedMagnitude_[k] * ratio;

                // Reconstruct Cartesian form with adjusted magnitude
                float real = adjustedMag * std::cos(synthPhase_[k]);
                float imag = adjustedMag * std::sin(synthPhase_[k]);
                synthesis.setCartesian(k, real, imag);
            }
        }
    }

}  // namespace Krate::DSP
