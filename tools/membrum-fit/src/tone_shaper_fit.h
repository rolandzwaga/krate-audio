#pragma once

#include "types.h"

#include <span>

namespace MembrumFit {

// Fit ToneShaper parameters (pitch envelope + filter cutoff/Q, filter env,
// drive/fold) into the given PadConfig. Mutates in place.
// Phase 1 fits pitch envelope + filter cutoff only; Phase 2 adds ADSR+drive+fold.
void fitToneShaper(std::span<const float> samples,
                   const SegmentedSample& seg,
                   double sampleRate,
                   const ModalDecomposition& modes,
                   Membrum::PadConfig& config);

// --- Helpers reusable by tests ---

// Tracks the fundamental over the first ~100 ms via parabolic STFT peak.
// Returns vector of (timeSec, freqHz) samples, sampled at hopSamples apart.
struct PitchTrackPoint { float timeSec; float freqHz; };
std::vector<PitchTrackPoint> trackPitch(std::span<const float> samples,
                                        const SegmentedSample& seg,
                                        double sampleRate);

// Returns true if the fundamental falls > 20% within 200 ms (=> enable env).
bool detectPitchEnvelope(const std::vector<PitchTrackPoint>& track,
                         float& startHz, float& endHz, float& durationSec);

}  // namespace MembrumFit
