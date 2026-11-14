#pragma once

// =============================================================================
// Click detection calibrated on no-event controls (test support, header-only)
// =============================================================================
// User ruling 2026-11-10 (drive re-stage): the re-staged drive turns a -12 / -6 dBFS test
// sine into a near-square wave, and the shared ClickDetector (an outlier of |y[n] - y[n-1]|
// above mean + k sigma of its 512-sample frame) reads that wave's own edges as clicks at the
// pinned k = 5. Before a click case is re-baselined it must show that:
// - renders of the same patch and signal WITHOUT the event (the controls: the cursor or the
//   setting held at each end, and between) read as many detections at k = 5 as the event
//   render: the counts are the waveform's own edges;
// - at the lowest k (5, 5.5, 6, ...) at which no control reads a detection, the event render
//   reads none either;
// - a positive control, a step added to the event render from one sample on, is still caught at
//   that k: a click larger than any edge of the waveform cannot hide below the calibrated
//   threshold. Its size is per waveform (user ruling 2026-11-11, round 4): 1.5 x the larger of
//   the controls' largest sample-to-sample step and the detector's own threshold on the controls
//   (mean + k sigma of |y[n] - y[n-1]| in each of its frames, the largest over the window). A
//   waveform whose edge sizes spread evenly (a SineFold, -12 dBFS) has a threshold above 1.5 x its
//   largest edge, and a step of that size read 0 even on the held render. The same step is also
//   added to every control: it must be caught there too, so the control is shown to be visible
//   on that waveform before the event render's 0 counts as a pass.
// A derivative detector cannot see a step smaller than the waveform's own edges; the cases
// that use this also run the event with the input 18 dB lower (the shaper then sees the drive
// it saw before the re-stage, and the wave is smooth) at the pinned k = 5.
// =============================================================================

#include "artifact_detection.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Disrumpo::TestSupport {

/// Detections of the shared ClickDetector (512 / 256 frames, @p energyDb floor) at @p sigma.
inline size_t clicksAtSigma(const float* x, size_t n, double sr, float sigma, float energyDb = -60.0f) {
    Krate::DSP::TestUtils::ClickDetectorConfig cfg;
    cfg.sampleRate = static_cast<float>(sr);
    cfg.frameSize = 512;
    cfg.hopSize = 256;
    cfg.detectionThreshold = sigma;
    cfg.energyThresholdDb = energyDb;
    Krate::DSP::TestUtils::ClickDetector det(cfg);
    det.prepare();
    return det.detect(x, n).size();
}

/// A synthetic no-event control: the render held at the old setting, from @p at a plain linear
/// crossfade over @p fade samples to the render held at the new setting. It is the transition the
/// event is designed to make, with no plugin event in it: a detector that flags it reacts to the
/// two sounds' own edges meeting in one frame, not to an artifact.
inline std::vector<float> plainCrossfade(const std::vector<float>& from, const std::vector<float>& to, size_t at,
                                         size_t fade) {
    std::vector<float> y(from.size());
    for (size_t n = 0; n < y.size(); ++n) {
        double g = 0.0;
        if (n >= at + fade) g = 1.0;
        else if (n >= at) g = static_cast<double>(n - at) / static_cast<double>(fade);
        y[n] = static_cast<float>((1.0 - g) * from[n] + g * to[n]);
    }
    return y;
}

/// A synthetic dip control (the band-count change, D-023): @p from faded out linearly over
/// @p half samples from @p at, then @p to faded in over the next @p half.
inline std::vector<float> plainDip(const std::vector<float>& from, const std::vector<float>& to, size_t at, size_t half) {
    std::vector<float> y(from.size());
    for (size_t n = 0; n < y.size(); ++n) {
        if (n < at) y[n] = from[n];
        else if (n < at + half) y[n] = static_cast<float>(from[n] * (1.0 - static_cast<double>(n - at) / static_cast<double>(half)));
        else if (n < at + 2 * half) y[n] = static_cast<float>(to[n] * static_cast<double>(n - at - half) / static_cast<double>(half));
        else y[n] = to[n];
    }
    return y;
}

/// The largest ClickDetector threshold (mean + @p sigma x std of |x[n] - x[n-1]|, as clicksAtSigma's
/// detector computes it per 512 / 256 frame) over the frames of @p x[0, n).
inline double detectorThreshold(const float* x, size_t n, float sigma) {
    constexpr size_t kFrame = 512;
    constexpr size_t kHop = 256;
    if (n < 2) return 0.0;
    const size_t frames = n < kFrame ? 1 : (n - kFrame) / kHop + 1;
    double largest = 0.0;
    for (size_t f = 0; f < frames; ++f) {
        const size_t start = f * kHop;
        const size_t len = std::min(kFrame, n - start);
        if (len < 2) continue;
        // |derivative| with its first entry 0, mean and Bessel-corrected std (StatisticalUtils)
        double sum = 0.0;
        for (size_t i = 1; i < len; ++i) sum += std::fabs(static_cast<double>(x[start + i]) - x[start + i - 1]);
        const double mean = sum / static_cast<double>(len);
        double var = mean * mean;  // the first entry, 0
        for (size_t i = 1; i < len; ++i) {
            const double d = std::fabs(static_cast<double>(x[start + i]) - x[start + i - 1]) - mean;
            var += d * d;
        }
        const double sd = std::sqrt(var / static_cast<double>(len - 1));
        largest = std::max(largest, mean + static_cast<double>(sigma) * sd);
    }
    return largest;
}

struct CalibratedClicks {
    size_t eventAt5 = 0;      ///< event render at the pinned 5 sigma
    size_t controlsAt5 = 0;   ///< all controls at 5 sigma (summed)
    float sigma = 0.0f;       ///< lowest threshold at which no control reads a detection (0: none up to the cap)
    size_t eventAtSigma = 0;  ///< event render at that threshold
    double largestStep = 0.0;  ///< the controls' largest sample-to-sample step
    double heldThreshold = 0.0;  ///< the detector's largest threshold on the controls at sigma
    double step = 0.0;        ///< positive-control step size: 1.5 x max(largestStep, heldThreshold)
    size_t positiveAtSigma = 0;  ///< event render plus the step, at that threshold
    size_t positiveOnControls = 0;  ///< fewest detections of the step added to any one control
};

/// Calibrates the detector on @p controls over [from, from + count) and reads the event render
/// and the positive control (a step injected at @p stepAt, inside the window) at that threshold.
inline CalibratedClicks calibratedClicks(const std::vector<float>& event, const std::vector<std::vector<float>>& controls,
                                         size_t from, size_t count, size_t stepAt, double sr, float energyDb = -60.0f,
                                         float sigmaCap = 100.0f) {
    CalibratedClicks r;
    r.eventAt5 = clicksAtSigma(event.data() + from, count, sr, 5.0f, energyDb);
    double largestStep = 0.0;
    for (const auto& c : controls) {
        r.controlsAt5 += clicksAtSigma(c.data() + from, count, sr, 5.0f, energyDb);
        for (size_t i = from + 1; i < from + count; ++i) {
            largestStep = std::max(largestStep, std::fabs(static_cast<double>(c[i]) - static_cast<double>(c[i - 1])));
        }
    }
    for (float sigma = 5.0f; sigma <= sigmaCap; sigma += 0.5f) {
        bool quiet = true;
        for (const auto& c : controls) quiet = quiet && clicksAtSigma(c.data() + from, count, sr, sigma, energyDb) == 0;
        if (quiet) {
            r.sigma = sigma;
            break;
        }
    }
    if (r.sigma <= 0.0f) return r;
    r.eventAtSigma = clicksAtSigma(event.data() + from, count, sr, r.sigma, energyDb);
    r.largestStep = largestStep;
    for (const auto& c : controls) {
        r.heldThreshold = std::max(r.heldThreshold, detectorThreshold(c.data() + from, count, r.sigma));
    }
    r.step = 1.5 * std::max(largestStep, r.heldThreshold);
    // The step is added to @p render from where it is flattest within 32 samples of stepAt, so it
    // is not added to (or taken from) one of the waveform's own edges
    const auto positiveCount = [&](const std::vector<float>& render) {
        size_t at = stepAt;
        double flattest = 1e30;
        for (size_t i = std::max(stepAt, from + 33) - 32; i <= std::min(stepAt + 32, from + count - 1); ++i) {
            const double d = std::fabs(static_cast<double>(render[i]) - static_cast<double>(render[i - 1]));
            if (d < flattest) {
                flattest = d;
                at = i;
            }
        }
        std::vector<float> positive(render.begin() + static_cast<std::ptrdiff_t>(from),
                                    render.begin() + static_cast<std::ptrdiff_t>(from + count));
        for (size_t i = at - from; i < positive.size(); ++i) positive[i] += static_cast<float>(r.step);
        return clicksAtSigma(positive.data(), positive.size(), sr, r.sigma, energyDb);
    };
    r.positiveAtSigma = positiveCount(event);
    r.positiveOnControls = controls.empty() ? 0 : static_cast<size_t>(-1);
    for (const auto& c : controls) r.positiveOnControls = std::min(r.positiveOnControls, positiveCount(c));
    return r;
}

}  // namespace Disrumpo::TestSupport
