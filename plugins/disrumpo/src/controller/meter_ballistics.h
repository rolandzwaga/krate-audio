#pragma once

// ==============================================================================
// MeterBallistics: one level-meter channel on the UI thread (no VSTGUI)
// ==============================================================================
// Fed with the processor's measurement windows (meter_window.h), advanced by the
// editor timer. Peak: instant attack, held kHoldSeconds, then falls at
// kReleaseDbPerSecond. RMS: the mean square over the last kRmsWindowSeconds of
// received audio (a rectangular window over the windows' sample counts; audio
// not received yet counts as silence). Clip: latched by any clipping window
// until clearClip().
// ==============================================================================

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Disrumpo {

class MeterBallistics {
public:
    static constexpr float kFloorDb = -120.0f;
    static constexpr double kHoldSeconds = 1.5;
    static constexpr double kReleaseDbPerSecond = 20.0;
    static constexpr double kRmsWindowSeconds = 0.3;

    /// One measurement window: largest |x|, mean of x^2 over @p samples, any |x| > 1.
    /// Non-finite values count as silence.
    void addWindow(float peak, float meanSq, uint32_t samples, bool clip, double sampleRate) noexcept {
        using Krate::DSP::detail::isFinite;
        if (isFinite(peak)) pendingPeak_ = std::max(pendingPeak_, std::fabs(peak));
        clipLatched_ = clipLatched_ || clip;
        if (samples == 0) return;
        if (sampleRate > 0.0) sampleRate_ = sampleRate;
        const double ms = isFinite(meanSq) ? std::max(0.0, static_cast<double>(meanSq)) : 0.0;
        chunks_[head_] = Chunk{.sumSq = ms * samples, .samples = samples};
        head_ = (head_ + 1) % kChunks;
        used_ = std::min(used_ + 1, kChunks);
    }

    /// Advances the display by @p dtSeconds and takes the windows added since the last tick.
    void tick(double dtSeconds) noexcept {
        const double dt = std::max(0.0, dtSeconds);
        if (holdLeft_ >= dt) {
            holdLeft_ -= dt;
        } else {
            const double fall = (dt - holdLeft_) * kReleaseDbPerSecond;
            holdLeft_ = 0.0;
            peakDb_ = std::max(kFloorDb, static_cast<float>(peakDb_ - fall));
        }
        const float incoming = pendingPeak_ > 0.0f ? std::max(kFloorDb, 20.0f * std::log10(pendingPeak_)) : kFloorDb;
        pendingPeak_ = 0.0f;
        if (incoming >= peakDb_) {
            peakDb_ = incoming;
            holdLeft_ = kHoldSeconds;
        }
        rmsDb_ = windowRmsDb();
    }

    [[nodiscard]] float peakDb() const noexcept { return peakDb_; }
    [[nodiscard]] float rmsDb() const noexcept { return rmsDb_; }
    [[nodiscard]] bool clipLatched() const noexcept { return clipLatched_; }
    void clearClip() noexcept { clipLatched_ = false; }

private:
    struct Chunk {
        double sumSq = 0.0;
        uint32_t samples = 0;
    };
    static constexpr size_t kChunks = 64;  // windows are >= 30 ms: 300 ms needs ~10

    [[nodiscard]] float windowRmsDb() const noexcept {
        const double window = kRmsWindowSeconds * sampleRate_;
        double counted = 0.0;
        double sumSq = 0.0;
        for (size_t i = 0; i < used_ && counted < window; ++i) {
            const Chunk& c = chunks_[(head_ + kChunks - 1 - i) % kChunks];
            const double take = std::min(static_cast<double>(c.samples), window - counted);
            sumSq += c.sumSq * (take / static_cast<double>(c.samples));
            counted += take;
        }
        const double meanSq = sumSq / window;
        return meanSq > 1e-12 ? static_cast<float>(10.0 * std::log10(meanSq)) : kFloorDb;
    }

    std::array<Chunk, kChunks> chunks_{};
    size_t head_ = 0;
    size_t used_ = 0;
    double sampleRate_ = 48000.0;
    float pendingPeak_ = 0.0f;
    float peakDb_ = kFloorDb;
    float rmsDb_ = kFloorDb;
    double holdLeft_ = 0.0;
    bool clipLatched_ = false;
};

}  // namespace Disrumpo
