#include "body_classifier.h"

#include "dsp/bodies/bell_modes.h"
#include "dsp/bodies/plate_modes.h"
#include "dsp/bodies/shell_modes.h"
#include "dsp/membrane_modes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace MembrumFit {

namespace {

// Build a reference ratio vector for each body from the compiled mode tables.
std::vector<float> membraneRatios() {
    return std::vector<float>(Membrum::kMembraneRatios.begin(), Membrum::kMembraneRatios.end());
}

std::vector<float> plateRatios() {
    // Phase 4-plate model: f_{m,n}/f_{1,1} = (m^2+n^2)/2.
    std::vector<float> r;
    r.reserve(Membrum::Bodies::kPlateModeCount);
    for (int i = 0; i < Membrum::Bodies::kPlateModeCount; ++i) {
        const auto& idx = Membrum::Bodies::kPlateIndices[i];
        const float base = (idx.m * idx.m + idx.n * idx.n) / 2.0f;
        r.push_back(base / (Membrum::Bodies::kPlateIndices[0].m * Membrum::Bodies::kPlateIndices[0].m
                          + Membrum::Bodies::kPlateIndices[0].n * Membrum::Bodies::kPlateIndices[0].n) * 2.0f);
    }
    return r;
}

std::vector<float> shellRatios() {
    return std::vector<float>(std::begin(Membrum::Bodies::kShellRatios),
                              std::end(Membrum::Bodies::kShellRatios));
}

std::vector<float> bellRatios() {
    return std::vector<float>(std::begin(Membrum::Bodies::kBellRatios),
                              std::end(Membrum::Bodies::kBellRatios));
}

std::vector<float> stringRatios() {
    std::vector<float> r(16);
    for (int i = 0; i < 16; ++i) r[i] = static_cast<float>(i + 1);
    return r;
}

// Amplitude-weighted log-ratio distance between measured and reference mode
// sequences. `measured` is sorted by frequency and normalised to its minimum
// (i.e. ratio to fundamental). `ref` is similarly ratios to fundamental.
float scoreRatios(std::span<const float> measuredRatios,
                  std::span<const float> measuredWeights,
                  std::span<const float> ref) {
    if (measuredRatios.empty() || ref.empty()) {
        return std::numeric_limits<float>::infinity();
    }
    const std::size_t K = std::min(measuredRatios.size(), ref.size());
    double totalW = 0.0;
    double sumW   = 0.0;
    for (std::size_t k = 0; k < K; ++k) {
        const float logM = std::log(std::max(measuredRatios[k], 1e-6f));
        const float logR = std::log(std::max(ref[k],            1e-6f));
        const float w = (k < measuredWeights.size()) ? measuredWeights[k] : 1.0f;
        totalW += std::abs(logM - logR) * w;
        sumW   += w;
    }
    return (sumW > 0.0) ? static_cast<float>(totalW / sumW) : std::numeric_limits<float>::infinity();
}

}  // namespace

BodyScoreList classifyBody(const ModalDecomposition& md,
                           const AttackFeatures& features) {
    BodyScoreList scores{};
    for (int i = 0; i < 6; ++i) {
        scores[i].body  = static_cast<Membrum::BodyModelType>(i);
        scores[i].score = std::numeric_limits<float>::infinity();
    }
    if (md.modes.size() < 2) {
        scores[0].score = 0.0f;
        scores[0].confidence = 1.0f;
        return scores;
    }

    // Sort ascending by frequency (copy so we don't mutate caller).
    std::vector<Mode> byFreq = md.modes;
    std::sort(byFreq.begin(), byFreq.end(),
              [](const Mode& a, const Mode& b){ return a.freqHz < b.freqHz; });
    const float f0 = byFreq.front().freqHz;

    const std::size_t K = std::min<std::size_t>(byFreq.size(), 12);
    std::vector<float> ratios(K), weights(K);
    for (std::size_t k = 0; k < K; ++k) {
        ratios[k]  = byFreq[k].freqHz / std::max(f0, 1e-3f);
        weights[k] = byFreq[k].amplitude;
    }

    scores[static_cast<int>(Membrum::BodyModelType::Membrane)].score  = scoreRatios(ratios, weights, membraneRatios());
    scores[static_cast<int>(Membrum::BodyModelType::Plate)].score     = scoreRatios(ratios, weights, plateRatios());
    scores[static_cast<int>(Membrum::BodyModelType::Shell)].score     = scoreRatios(ratios, weights, shellRatios());
    scores[static_cast<int>(Membrum::BodyModelType::String)].score    = scoreRatios(ratios, weights, stringRatios());
    scores[static_cast<int>(Membrum::BodyModelType::Bell)].score      = scoreRatios(ratios, weights, bellRatios());

    // NoiseBody: plate-like modal spacing AND high noisiness. Penalise clean
    // signals (low residual/total ratio) so that an ordinary Plate never beats
    // NoiseBody by accident; the classifier only picks NoiseBody when the
    // residual truly dominates (spec §4.6 "high modal density + broadband noise residual").
    const float noisiness = (md.totalRms > 1e-6f) ? md.residualRms / md.totalRms : 0.0f;
    const float noiseBodyPenalty = (noisiness > 0.3f) ? 1.0f : (2.0f + (0.3f - noisiness));
    scores[static_cast<int>(Membrum::BodyModelType::NoiseBody)].score =
        scoreRatios(ratios, weights, plateRatios()) * noiseBodyPenalty;

    // Confidence = 1 - (best / 2nd-best).
    std::vector<float> allScores(6);
    for (int i = 0; i < 6; ++i) allScores[i] = scores[i].score;
    std::vector<float> sorted = allScores;
    std::sort(sorted.begin(), sorted.end());
    const float best = sorted[0];
    const float second = sorted[1];
    for (int i = 0; i < 6; ++i) {
        if (scores[i].score <= best + 1e-9f) {
            scores[i].confidence = (second > 0.0f) ? std::clamp(1.0f - best / second, 0.0f, 1.0f) : 1.0f;
        } else {
            scores[i].confidence = 0.0f;
        }
    }
    (void)features;  // Phase 2+ uses attack brightness for Plate/Shell tie-break.
    return scores;
}

Membrum::BodyModelType pickBestBody(const BodyScoreList& scores) {
    Membrum::BodyModelType best = scores[0].body;
    float bestScore = scores[0].score;
    for (const auto& s : scores) {
        if (s.score < bestScore) { bestScore = s.score; best = s.body; }
    }
    return best;
}

}  // namespace MembrumFit
