#pragma once

#include <span>
#include <vector>

namespace MembrumFit {

// Multi-resolution STFT magnitude L1 loss (Yamamoto / auraloss).
// Windows: {64, 128, 256, 512, 1024, 2048}, 75 % overlap, log-magnitude.
float computeMSSLoss(std::span<const float> target,
                     std::span<const float> candidate);

// MFCC L1 distance (20 coefficients). Mel filterbank + log + DCT.
// targetMfcc is a precomputed flat vector with stride = kMfccCoeffCount.
std::vector<float> computeMFCC(std::span<const float> signal, double sampleRate);
float computeMFCCL1(const std::vector<float>& a, const std::vector<float>& b);

// Log-envelope L1 (Hilbert envelope downsampled to ~200 Hz).
std::vector<float> computeLogEnvelope(std::span<const float> signal, double sampleRate);
float computeEnvelopeL1(const std::vector<float>& a, const std::vector<float>& b);

// Combined weighted total loss. targetMfcc / targetEnv are cached; pass empty
// vectors to auto-compute them (slow path for one-shot calls).
struct LossWeights {
    float stft = 0.6f;
    float mfcc = 0.2f;
    float env  = 0.2f;
};

float totalLoss(std::span<const float> target,
                const std::vector<float>& targetMfcc,
                const std::vector<float>& targetEnv,
                std::span<const float> candidate,
                double sampleRate,
                LossWeights weights);

constexpr int kMfccCoeffCount = 20;

}  // namespace MembrumFit
