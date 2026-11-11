#pragma once

// ==============================================================================
// MeterWindow: one level-meter measurement window (processor -> controller)
// ==============================================================================
// The processor accumulates levels between two ~30 Hz spectrum sends
// (MeterAccumulator) and ships the window in the SpectrumBlock (Tier 1/2) and
// through SharedMeters (Tier 3). Measured points:
//   input   L/R: after Input Gain, before the crossover
//   output  L/R: what the host receives (after Mix, Output Gain, latency
//                compensation, the band-count dip and the output safety clipper);
//                its clip bits read the signal before the safety clipper (user
//                ruling 2026-11-10), so the light shows when the clipper catches an over
//   band 0..3  : each band's output after its gain/pan/mute, before summation
// Plain data only: no processor or controller types.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace Disrumpo {

/// Bands carried in a meter window
inline constexpr int kMeterBands = 4;

/// Clip bits of MeterWindow::clipFlags: set when a sample of the window had |x| > 1.0
inline constexpr uint32_t kMeterClipInL = 1u << 0;
inline constexpr uint32_t kMeterClipInR = 1u << 1;
inline constexpr uint32_t kMeterClipOutL = 1u << 2;
inline constexpr uint32_t kMeterClipOutR = 1u << 3;
inline constexpr uint32_t kMeterClipBand0 = 1u << 4;  // band b: kMeterClipBand0 << b

/// One measurement window. Peaks are linear magnitudes, mean squares are per
/// channel over numSamples; every value is finite.
struct MeterWindow {
    float inPeak[2]{};
    float inMeanSq[2]{};
    float outPeak[2]{};
    float outMeanSq[2]{};
    float bandPeak[kMeterBands]{};
    uint32_t clipFlags = 0;
    uint32_t numSamples = 0;

    friend bool operator==(const MeterWindow&, const MeterWindow&) = default;
};

static_assert(std::is_trivially_copyable_v<MeterWindow>);

/// Merges @p b into @p a: the larger peaks, the sample-weighted mean squares,
/// the union of the clip bits and the summed sample count (saturating).
inline void mergeMeterWindow(MeterWindow& a, const MeterWindow& b) noexcept {
    const double total = static_cast<double>(a.numSamples) + static_cast<double>(b.numSamples);
    const auto meanSq = [&](float ma, float mb) noexcept {
        if (total <= 0.0) return 0.0f;
        return static_cast<float>((static_cast<double>(ma) * a.numSamples +
                                   static_cast<double>(mb) * b.numSamples) / total);
    };
    for (int c = 0; c < 2; ++c) {
        a.inPeak[c] = std::max(a.inPeak[c], b.inPeak[c]);
        a.outPeak[c] = std::max(a.outPeak[c], b.outPeak[c]);
        a.inMeanSq[c] = meanSq(a.inMeanSq[c], b.inMeanSq[c]);
        a.outMeanSq[c] = meanSq(a.outMeanSq[c], b.outMeanSq[c]);
    }
    for (int i = 0; i < kMeterBands; ++i) a.bandPeak[i] = std::max(a.bandPeak[i], b.bandPeak[i]);
    a.clipFlags |= b.clipFlags;
    a.numSamples = static_cast<uint32_t>(std::min(total, static_cast<double>(UINT32_MAX)));
}

/// Audio-thread accumulation between two sends: max of |x| and sum of x^2.
/// take() turns it into a window and starts the next one. A non-finite sample
/// costs only the window it falls in: take() reports a non-finite value as 0.
struct MeterAccumulator {
    float inPeak[2]{};
    float inSumSq[2]{};
    float outPeak[2]{};
    float outSumSq[2]{};
    float outPreClipPeak[2]{};  // the output before the safety clipper: its clip bits only
    float bandPeak[kMeterBands]{};
    uint32_t numSamples = 0;

    void reset() noexcept { *this = MeterAccumulator{}; }

    [[nodiscard]] MeterWindow take() noexcept {
        MeterWindow w;
        const auto finite = [](float v) noexcept { return Krate::DSP::detail::isFinite(v) ? v : 0.0f; };
        // NaN is no clip (a fast-math compare may call NaN > 1 true); +Inf is
        const auto clips = [](float peak) noexcept { return !Krate::DSP::detail::isNaN(peak) && peak > 1.0f; };
        const float invCount = numSamples > 0 ? 1.0f / static_cast<float>(numSamples) : 0.0f;
        for (int c = 0; c < 2; ++c) {
            if (clips(inPeak[c])) w.clipFlags |= kMeterClipInL << c;
            if (clips(outPreClipPeak[c])) w.clipFlags |= kMeterClipOutL << c;
            w.inPeak[c] = finite(inPeak[c]);
            w.outPeak[c] = finite(outPeak[c]);
            w.inMeanSq[c] = finite(inSumSq[c] * invCount);
            w.outMeanSq[c] = finite(outSumSq[c] * invCount);
        }
        for (int i = 0; i < kMeterBands; ++i) {
            if (clips(bandPeak[i])) w.clipFlags |= kMeterClipBand0 << i;
            w.bandPeak[i] = finite(bandPeak[i]);
        }
        w.numSamples = numSamples;
        reset();
        return w;
    }
};

}  // namespace Disrumpo
