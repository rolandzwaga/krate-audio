#include "tone_shaper_fit.h"
#include "features.h"

#include <algorithm>
#include <cmath>

namespace MembrumFit {

std::vector<PitchTrackPoint> trackPitch(std::span<const float> s,
                                        const SegmentedSample& seg,
                                        double sr) {
    std::vector<PitchTrackPoint> out;
    const std::size_t hop  = static_cast<std::size_t>(0.005 * sr);
    const std::size_t win  = static_cast<std::size_t>(0.020 * sr);
    const std::size_t end  = std::min(s.size(), seg.onsetSample + static_cast<std::size_t>(0.100 * sr));
    for (std::size_t i = seg.onsetSample; i + win < end; i += hop) {
        const float f = estimateFundamental(s.subspan(i, win), sr);
        out.push_back({ static_cast<float>((i - seg.onsetSample) / sr), f });
    }
    return out;
}

bool detectPitchEnvelope(const std::vector<PitchTrackPoint>& t,
                         float& startHz, float& endHz, float& durSec) {
    if (t.size() < 2) return false;
    startHz = t.front().freqHz;
    endHz   = t.front().freqHz;
    float minHz = startHz, maxHz = startHz;
    float minT = 0.0f;
    for (const auto& p : t) {
        if (p.freqHz < minHz) { minHz = p.freqHz; minT = p.timeSec; }
        if (p.freqHz > maxHz) { maxHz = p.freqHz; }
    }
    // Downward sweep detection (kicks).
    if (startHz > 0.0f && minHz < startHz * 0.8f && minT < 0.2f) {
        endHz  = minHz;
        durSec = minT;
        return true;
    }
    return false;
}

void fitToneShaper(std::span<const float> samples,
                   const SegmentedSample& seg,
                   double sr,
                   const ModalDecomposition& /*modes*/,
                   Membrum::PadConfig& config) {
    const auto track = trackPitch(samples, seg, sr);
    float start = 0.0f, end = 0.0f, durSec = 0.0f;
    if (detectPitchEnvelope(track, start, end, durSec)) {
        // Encode normalised against [20, 2000] Hz logarithmically: norm = log(Hz/20) / log(100).
        auto toNorm = [](float hz){
            return static_cast<float>(std::log(std::max(hz, 20.0f) / 20.0f) / std::log(100.0));
        };
        config.tsPitchEnvStart = toNorm(start);
        config.tsPitchEnvEnd   = toNorm(end);
        config.tsPitchEnvTime  = std::clamp(durSec / 0.5f, 0.0f, 1.0f);
        config.tsPitchEnvCurve = 0.0f;  // Exponential
    }
    // Phase 1 leaves filter cutoff at mapper default. Phase 2 adds the 32x8 grid.
}

}  // namespace MembrumFit
