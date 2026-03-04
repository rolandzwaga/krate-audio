// ==============================================================================
// Layer 2: DSP Processor - Partial Tracker
// ==============================================================================
// Spectral peak detection, harmonic sieve, frame-to-frame partial matching,
// and birth/death management for harmonic analysis.
//
// Spec: specs/115-innexus-m1-core-instrument/spec.md
// Covers: FR-022 (peak detection with parabolic interpolation),
//         FR-023 (harmonic sieve), FR-024 (frame-to-frame matching),
//         FR-025 (birth/death with grace period),
//         FR-026 (48-partial cap), FR-027 (hysteresis on active set),
//         FR-028 (per-partial data)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, allocations only in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends on Layer 0 core, Layer 1 primitives)
// - Principle XV: ODR Prevention (header-only, inline)
// ==============================================================================

#pragma once

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/spectral_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace Krate::DSP {

/// @brief Spectral partial tracker with harmonic sieve, frame-to-frame
///        matching, birth/death management, and 48-partial cap.
///
/// Processes SpectralBuffer frames from STFT analysis alongside F0 estimates
/// from the YIN pitch detector. Outputs tracked Partial data suitable for
/// the HarmonicModelBuilder.
///
/// Usage:
/// @code
///   PartialTracker tracker;
///   tracker.prepare(4096, 44100.0);
///   // Per analysis frame:
///   tracker.processFrame(spectrum, f0Estimate, 4096, 44100.0f);
///   const auto& partials = tracker.getPartials();
///   int count = tracker.getActiveCount();
/// @endcode
class PartialTracker {
public:
    /// Maximum number of tracked partials (FR-026)
    static constexpr size_t kMaxPartials = 48;

    /// Number of frames a disappearing partial is held before death (FR-025)
    static constexpr int kGracePeriodFrames = 4;

    PartialTracker() noexcept = default;

    /// @brief Prepare the tracker for a given FFT size and sample rate.
    /// @param fftSize FFT size used by the STFT analysis
    /// @param sampleRate Audio sample rate in Hz
    /// @note NOT real-time safe (allocates memory)
    void prepare(size_t fftSize, double sampleRate) noexcept {
        fftSize_ = fftSize;
        sampleRate_ = static_cast<float>(sampleRate);
        numBins_ = fftSize / 2 + 1;

        // Pre-allocate peak detection buffers
        peakBins_.resize(numBins_);
        peakFreqs_.resize(numBins_);
        peakAmps_.resize(numBins_);
        peakPhases_.resize(numBins_);
        peakHarmonicIndex_.resize(numBins_);
        peakMatched_.resize(numBins_);
        peakMatchedSlot_.resize(numBins_);

        reset();
    }

    /// @brief Reset all tracking state.
    /// @note Real-time safe
    void reset() noexcept {
        for (auto& p : partials_) {
            p = Partial{};
        }
        for (auto& p : previousPartials_) {
            p = Partial{};
        }
        activeCount_ = 0;
        previousActiveCount_ = 0;
        numPeaks_ = 0;
        for (auto& g : gracePeriodCountdown_) {
            g = 0;
        }
        for (auto& m : matched_) {
            m = false;
        }
    }

    /// @brief Process one spectral frame.
    /// @param spectrum The STFT magnitude/phase spectrum
    /// @param f0 Current F0 estimate from pitch detector
    /// @param fftSize FFT size
    /// @param sampleRate Sample rate in Hz
    /// @note Real-time safe (no allocations)
    void processFrame(const SpectralBuffer& spectrum,
                      const F0Estimate& f0,
                      size_t fftSize,
                      float sampleRate) noexcept {
        // Save previous state for frame-to-frame matching
        previousPartials_ = partials_;
        previousActiveCount_ = activeCount_;

        // Step 1: Peak detection (FR-022)
        detectPeaks(spectrum, fftSize, sampleRate);

        // Step 2: Harmonic sieve (FR-023) -- only if voiced
        if (f0.voiced && f0.frequency > 0.0f) {
            mapToHarmonics(f0.frequency);
        } else {
            // Unvoiced: no harmonic indices assigned
            for (int i = 0; i < numPeaks_; ++i) {
                peakHarmonicIndex_[static_cast<size_t>(i)] = 0;
            }
        }

        // Step 3: Frame-to-frame matching (FR-024)
        matchTracks();

        // Step 4: Birth/death management (FR-025)
        updateLifecycles(f0);

        // Step 5: Active set management with cap (FR-026) and hysteresis (FR-027)
        enforcePartialCap();
    }

    /// @brief Get the current tracked partials array.
    /// @return Reference to the fixed-size partial array
    [[nodiscard]] const std::array<Partial, kMaxPartials>& getPartials() const noexcept {
        return partials_;
    }

    /// @brief Get the number of currently active (tracked) partials.
    /// @return Active partial count [0, kMaxPartials]
    [[nodiscard]] int getActiveCount() const noexcept {
        return activeCount_;
    }

private:
    // =========================================================================
    // Peak Detection (FR-022)
    // =========================================================================

    /// Find local maxima in the magnitude spectrum and apply parabolic
    /// interpolation for sub-bin frequency precision.
    void detectPeaks(const SpectralBuffer& spectrum,
                     size_t fftSize,
                     float sampleRate) noexcept {
        numPeaks_ = 0;
        const size_t numBins = fftSize / 2 + 1;
        const float binSpacing = sampleRate / static_cast<float>(fftSize);

        // Minimum magnitude threshold to reject noise floor
        constexpr float kMinMagnitude = 1e-6f;

        // Scan for local maxima (bins 1..numBins-2 to have neighbors)
        for (size_t b = 2; b < numBins - 2; ++b) {
            const float mag = spectrum.getMagnitude(b);
            if (mag < kMinMagnitude) continue;

            const float magLeft = spectrum.getMagnitude(b - 1);
            const float magRight = spectrum.getMagnitude(b + 1);

            // Local maximum: higher than both neighbors
            if (mag > magLeft && mag > magRight) {
                // Parabolic interpolation for sub-bin precision
                const float interpolatedBin = parabolicInterpolation(
                    magLeft, mag, magRight, static_cast<float>(b));

                const float freq = interpolatedBin * binSpacing;
                const float phase = spectrum.getPhase(b);

                // Compute interpolated amplitude
                // Use the parabolic peak amplitude estimate
                const float denom = magLeft - 2.0f * mag + magRight;
                float peakAmp = mag;
                if (denom != 0.0f) {
                    const float delta =
                        0.5f * (magLeft - magRight) / denom;
                    peakAmp = mag - 0.25f * (magLeft - magRight) * delta;
                }

                const auto idx = static_cast<size_t>(numPeaks_);
                peakBins_[idx] = interpolatedBin;
                peakFreqs_[idx] = freq;
                peakAmps_[idx] = peakAmp;
                peakPhases_[idx] = phase;
                peakHarmonicIndex_[idx] = 0; // Will be assigned in sieve
                peakMatched_[idx] = false;
                ++numPeaks_;

                // Safety: don't overflow buffers
                if (numPeaks_ >= static_cast<int>(numBins_) - 1) break;
            }
        }
    }

    // =========================================================================
    // Harmonic Sieve (FR-023)
    // =========================================================================

    /// Map detected peaks to integer multiples of F0 with tolerance that
    /// scales as sqrt(n) per harmonic index.
    void mapToHarmonics(float f0) noexcept {
        // Base tolerance factor: fraction of F0
        constexpr float kBaseToleranceFactor = 0.06f;

        // Maximum harmonic index to search
        const float nyquist = sampleRate_ * 0.5f;
        const int maxHarmonic =
            static_cast<int>(nyquist / f0);

        // For each peak, find the best matching harmonic
        for (int p = 0; p < numPeaks_; ++p) {
            const auto pidx = static_cast<size_t>(p);
            const float peakFreq = peakFreqs_[pidx];
            float bestError = std::numeric_limits<float>::max();
            int bestHarmonic = 0;

            for (int n = 1; n <= maxHarmonic &&
                            n <= static_cast<int>(kMaxPartials); ++n) {
                const float harmonicFreq = static_cast<float>(n) * f0;
                const float tolerance =
                    kBaseToleranceFactor * std::sqrt(static_cast<float>(n)) * f0;
                const float error = std::abs(peakFreq - harmonicFreq);

                if (error < tolerance && error < bestError) {
                    bestError = error;
                    bestHarmonic = n;
                }
            }

            peakHarmonicIndex_[pidx] = bestHarmonic;
        }

        // Resolve conflicts: if two peaks map to the same harmonic,
        // keep the one with lower error
        for (int i = 0; i < numPeaks_; ++i) {
            if (peakHarmonicIndex_[static_cast<size_t>(i)] == 0) continue;
            for (int j = i + 1; j < numPeaks_; ++j) {
                if (peakHarmonicIndex_[static_cast<size_t>(j)] ==
                    peakHarmonicIndex_[static_cast<size_t>(i)]) {
                    // Keep the peak closer to the expected harmonic frequency
                    const float harmonicFreq =
                        static_cast<float>(
                            peakHarmonicIndex_[static_cast<size_t>(i)]) *
                        f0;
                    const float errI =
                        std::abs(peakFreqs_[static_cast<size_t>(i)] -
                                 harmonicFreq);
                    const float errJ =
                        std::abs(peakFreqs_[static_cast<size_t>(j)] -
                                 harmonicFreq);
                    if (errI <= errJ) {
                        peakHarmonicIndex_[static_cast<size_t>(j)] = 0;
                    } else {
                        peakHarmonicIndex_[static_cast<size_t>(i)] = 0;
                        break; // i is now unassigned, move on
                    }
                }
            }
        }
    }

    // =========================================================================
    // Frame-to-Frame Matching (FR-024)
    // =========================================================================

    /// Match current peaks to previous partials by frequency proximity.
    void matchTracks() noexcept {
        // Reset match flags
        for (int i = 0; i < numPeaks_; ++i) {
            peakMatched_[static_cast<size_t>(i)] = false;
        }
        for (size_t i = 0; i < kMaxPartials; ++i) {
            matched_[i] = false;
        }

        // Match each previous partial to the nearest current peak
        for (int prev = 0; prev < previousActiveCount_; ++prev) {
            const auto& prevPartial = previousPartials_[static_cast<size_t>(prev)];
            if (prevPartial.frequency <= 0.0f) continue;

            float bestDist = std::numeric_limits<float>::max();
            int bestPeak = -1;

            for (int p = 0; p < numPeaks_; ++p) {
                if (peakMatched_[static_cast<size_t>(p)]) continue;

                const float dist =
                    std::abs(peakFreqs_[static_cast<size_t>(p)] -
                             prevPartial.frequency);

                // Maximum matching distance: 5% of previous frequency
                const float maxDist = prevPartial.frequency * 0.05f;

                if (dist < bestDist && dist < maxDist) {
                    bestDist = dist;
                    bestPeak = p;
                }
            }

            if (bestPeak >= 0) {
                peakMatched_[static_cast<size_t>(bestPeak)] = true;
                matched_[static_cast<size_t>(prev)] = true;

                // Mark the peak with the previous partial's slot index
                // We'll use this in updateLifecycles
                peakMatchedSlot_[static_cast<size_t>(bestPeak)] = prev;
            }
        }
    }

    // =========================================================================
    // Birth/Death Management (FR-025)
    // =========================================================================

    /// Update partial lifecycles: continue matched tracks, start new births,
    /// manage grace period for deaths.
    void updateLifecycles(const F0Estimate& f0) noexcept {
        // Build new partial array
        std::array<Partial, kMaxPartials> newPartials{};
        std::array<int, kMaxPartials> newGrace{};
        int newCount = 0;

        // 1. Continue matched partials (update from peaks)
        for (int p = 0; p < numPeaks_ && newCount < static_cast<int>(kMaxPartials); ++p) {
            const auto pidx = static_cast<size_t>(p);
            if (!peakMatched_[pidx]) continue;

            const int prevSlot = peakMatchedSlot_[pidx];
            const auto& prev = previousPartials_[static_cast<size_t>(prevSlot)];
            auto& np = newPartials[static_cast<size_t>(newCount)];

            np.frequency = peakFreqs_[pidx];
            np.amplitude = peakAmps_[pidx];
            np.phase = peakPhases_[pidx];
            np.harmonicIndex = peakHarmonicIndex_[pidx] != 0
                                   ? peakHarmonicIndex_[pidx]
                                   : prev.harmonicIndex;
            np.age = prev.age + 1;

            // Update stability: increase if matched continuously
            np.stability = std::min(1.0f, prev.stability + 0.1f);

            // Compute relative frequency and inharmonic deviation (FR-028)
            if (f0.voiced && f0.frequency > 0.0f) {
                np.relativeFrequency = np.frequency / f0.frequency;
                np.inharmonicDeviation =
                    np.relativeFrequency -
                    static_cast<float>(np.harmonicIndex);
            } else {
                np.relativeFrequency =
                    np.frequency > 0.0f ? np.frequency : 0.0f;
                np.inharmonicDeviation = 0.0f;
            }

            newGrace[static_cast<size_t>(newCount)] = 0; // Reset grace
            ++newCount;
        }

        // 2. Handle unmatched previous partials (grace period / death)
        for (int prev = 0; prev < previousActiveCount_ &&
                           newCount < static_cast<int>(kMaxPartials);
             ++prev) {
            if (matched_[static_cast<size_t>(prev)]) continue;

            const auto& prevPartial = previousPartials_[static_cast<size_t>(prev)];
            if (prevPartial.frequency <= 0.0f) continue;

            const int prevGrace = gracePeriodCountdown_[static_cast<size_t>(prev)];

            if (prevGrace < kGracePeriodFrames) {
                // Still in grace period: hold the partial with decaying amplitude
                auto& np = newPartials[static_cast<size_t>(newCount)];
                np = prevPartial;
                np.amplitude *= 0.7f; // Fade during grace period
                np.stability = std::max(0.0f, prevPartial.stability - 0.15f);
                np.age = prevPartial.age + 1;

                newGrace[static_cast<size_t>(newCount)] = prevGrace + 1;
                ++newCount;
            }
            // else: grace period expired, partial dies
        }

        // 3. Birth new (unmatched) peaks
        for (int p = 0; p < numPeaks_ && newCount < static_cast<int>(kMaxPartials); ++p) {
            const auto pidx = static_cast<size_t>(p);
            if (peakMatched_[pidx]) continue;

            // Filter: only birth peaks with valid harmonic index (if voiced)
            // or any peak (if unvoiced)
            if (f0.voiced && peakHarmonicIndex_[pidx] == 0) continue;

            auto& np = newPartials[static_cast<size_t>(newCount)];
            np.frequency = peakFreqs_[pidx];
            np.amplitude = peakAmps_[pidx] * 0.1f; // Fade in from low amplitude
            np.phase = peakPhases_[pidx];
            np.harmonicIndex = peakHarmonicIndex_[pidx];
            np.age = 0;
            np.stability = 0.3f; // Initial stability

            if (f0.voiced && f0.frequency > 0.0f) {
                np.relativeFrequency = np.frequency / f0.frequency;
                np.inharmonicDeviation =
                    np.relativeFrequency -
                    static_cast<float>(np.harmonicIndex);
            } else {
                np.relativeFrequency =
                    np.frequency > 0.0f ? np.frequency : 0.0f;
                np.inharmonicDeviation = 0.0f;
            }

            newGrace[static_cast<size_t>(newCount)] = 0;
            ++newCount;
        }

        // Copy results
        partials_ = newPartials;
        gracePeriodCountdown_ = newGrace;
        activeCount_ = newCount;
    }

    // =========================================================================
    // Active Set Management (FR-026, FR-027)
    // =========================================================================

    /// Enforce the 48-partial cap. Rank by energy * stability, with
    /// hysteresis to prevent rapid replacement.
    void enforcePartialCap() noexcept {
        if (activeCount_ <= static_cast<int>(kMaxPartials)) return;

        // Compute ranking score for each partial
        struct RankedPartial {
            int index;
            float score;
        };

        // Use a simple fixed-size array (no allocation)
        std::array<RankedPartial, kMaxPartials * 2> ranked{};
        const int count = std::min(activeCount_,
                                   static_cast<int>(kMaxPartials * 2));

        for (int i = 0; i < count; ++i) {
            const auto& p = partials_[static_cast<size_t>(i)];
            // Score: energy (amplitude^2) * stability
            // Hysteresis bonus: established partials (age > 4) get a bonus
            float score = p.amplitude * p.amplitude * p.stability;
            if (p.age > kGracePeriodFrames) {
                score *= 1.5f; // Hysteresis bonus (FR-027)
            }
            ranked[static_cast<size_t>(i)] = {i, score};
        }

        // Sort by score descending (simple selection sort -- small N)
        for (int i = 0; i < count - 1; ++i) {
            int maxIdx = i;
            for (int j = i + 1; j < count; ++j) {
                if (ranked[static_cast<size_t>(j)].score >
                    ranked[static_cast<size_t>(maxIdx)].score) {
                    maxIdx = j;
                } else if (ranked[static_cast<size_t>(j)].score ==
                           ranked[static_cast<size_t>(maxIdx)].score) {
                    // Tie-break by lower harmonic index
                    const int idxJ = ranked[static_cast<size_t>(j)].index;
                    const int idxMax =
                        ranked[static_cast<size_t>(maxIdx)].index;
                    if (partials_[static_cast<size_t>(idxJ)].harmonicIndex <
                        partials_[static_cast<size_t>(idxMax)].harmonicIndex) {
                        maxIdx = j;
                    }
                }
            }
            if (maxIdx != i) {
                std::swap(ranked[static_cast<size_t>(i)],
                          ranked[static_cast<size_t>(maxIdx)]);
            }
        }

        // Keep only the top kMaxPartials
        std::array<Partial, kMaxPartials> trimmed{};
        std::array<int, kMaxPartials> trimmedGrace{};
        const int keepCount = std::min(count, static_cast<int>(kMaxPartials));

        for (int i = 0; i < keepCount; ++i) {
            const int origIdx = ranked[static_cast<size_t>(i)].index;
            trimmed[static_cast<size_t>(i)] =
                partials_[static_cast<size_t>(origIdx)];
            trimmedGrace[static_cast<size_t>(i)] =
                gracePeriodCountdown_[static_cast<size_t>(origIdx)];
        }

        partials_ = trimmed;
        gracePeriodCountdown_ = trimmedGrace;
        activeCount_ = keepCount;
    }

    // =========================================================================
    // Data Members
    // =========================================================================

    /// Current tracked partials
    std::array<Partial, kMaxPartials> partials_{};

    /// Previous frame's tracked partials (for matching)
    std::array<Partial, kMaxPartials> previousPartials_{};

    /// Grace period countdown per partial slot
    std::array<int, kMaxPartials> gracePeriodCountdown_{};

    /// Whether each previous partial was matched this frame
    std::array<bool, kMaxPartials> matched_{};

    /// Current active partial count
    int activeCount_ = 0;

    /// Previous frame's active count
    int previousActiveCount_ = 0;

    // Peak detection working buffers (pre-allocated in prepare())
    std::vector<float> peakBins_;
    std::vector<float> peakFreqs_;
    std::vector<float> peakAmps_;
    std::vector<float> peakPhases_;
    std::vector<int> peakHarmonicIndex_;
    std::vector<bool> peakMatched_;
    std::vector<int> peakMatchedSlot_{};

    /// Number of detected peaks this frame
    int numPeaks_ = 0;

    /// FFT size
    size_t fftSize_ = 0;

    /// Sample rate
    float sampleRate_ = 44100.0f;

    /// Number of spectral bins (fftSize/2 + 1)
    size_t numBins_ = 0;
};

} // namespace Krate::DSP
