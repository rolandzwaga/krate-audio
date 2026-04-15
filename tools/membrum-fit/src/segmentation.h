#pragma once

#include "types.h"

#include <span>

namespace MembrumFit {

// Locate the hit onset, attack window (onset + 20 ms), and decay window
// (ends at RMS gate -60 dBFS or 2 s, whichever first). Returns a zero-width
// segmentation if the sample has no usable onset or the decay is shorter than
// 100 ms (spec §9 risk #4 -- reject short samples).
SegmentedSample segmentSample(std::span<const float> samples, double sampleRate);

// Convenience overload for LoadedSample.
inline SegmentedSample segmentSample(const LoadedSample& s) {
    return segmentSample(s.samples, s.sampleRate);
}

// True if the segmentation is usable for downstream modal extraction.
bool isSegmentationUsable(const SegmentedSample& seg, double sampleRate);

}  // namespace MembrumFit
