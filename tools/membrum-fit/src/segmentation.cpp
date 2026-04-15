#include "segmentation.h"

#include <algorithm>
#include <cmath>

namespace MembrumFit {

namespace {
constexpr float kAttackMs  = 20.0f;
constexpr float kDecayStartMs = 5.0f;
constexpr float kMaxTailSec   = 2.0f;
constexpr float kRmsGateDbfs  = -60.0f;
constexpr std::size_t kHop    = 512;
}

SegmentedSample segmentSample(std::span<const float> s, double sr) {
    SegmentedSample out;
    if (s.empty() || sr <= 0.0) return out;

    // Full-band energy peak (coarse onset).
    std::size_t peakIdx = 0;
    float peakEnergy = 0.0f;
    for (std::size_t i = 0; i + kHop < s.size(); i += kHop) {
        float e = 0.0f;
        for (std::size_t j = i; j < i + kHop; ++j) e += s[j] * s[j];
        if (e > peakEnergy) { peakEnergy = e; peakIdx = i; }
    }

    // Walk backward from peak to first 2 %-of-peak sample (onset).
    const float peakAmp = (peakEnergy > 0.0f) ? std::sqrt(peakEnergy / static_cast<float>(kHop)) : 0.0f;
    const float thresh  = peakAmp * 0.02f;
    std::size_t onset = peakIdx;
    while (onset > 0 && std::abs(s[onset]) > thresh) --onset;

    const auto attackLen = static_cast<std::size_t>(kAttackMs * 0.001 * sr);
    const auto decayStartOffset = static_cast<std::size_t>(kDecayStartMs * 0.001 * sr);

    out.onsetSample      = onset;
    out.attackEndSample  = std::min(onset + attackLen, s.size());
    out.decayStartSample = std::min(onset + decayStartOffset, s.size());

    // Decay window: walk forward from decayStart, end at RMS gate or kMaxTailSec.
    const auto maxTail = static_cast<std::size_t>(kMaxTailSec * sr);
    const std::size_t end = std::min(onset + maxTail, s.size());
    const float gateAmp = std::pow(10.0f, kRmsGateDbfs / 20.0f);
    std::size_t decayEnd = end;
    const std::size_t rmsWin = std::min<std::size_t>(2048, (end - out.decayStartSample) / 8 + 1);
    for (std::size_t i = end; i > out.decayStartSample + rmsWin; i -= rmsWin) {
        float e = 0.0f;
        for (std::size_t j = i - rmsWin; j < i; ++j) e += s[j] * s[j];
        const float rms = std::sqrt(e / static_cast<float>(rmsWin));
        if (rms > gateAmp) { decayEnd = i; break; }
    }
    out.decayEndSample = decayEnd;
    return out;
}

bool isSegmentationUsable(const SegmentedSample& seg, double sr) {
    if (seg.decayEndSample <= seg.decayStartSample) return false;
    const double decayMs = (seg.decayEndSample - seg.decayStartSample) * 1000.0 / sr;
    return decayMs >= 100.0;
}

}  // namespace MembrumFit
