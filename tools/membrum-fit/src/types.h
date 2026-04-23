#pragma once

// ==============================================================================
// Shared data types for the membrum-fit pipeline
// ==============================================================================
// Every pipeline stage consumes one of these structs and produces another so
// stages stay pure and testable (no globals, no hidden state). See
// specs/membrum-fit-tool.md §3 for the full pipeline diagram.
// ==============================================================================

#include "dsp/body_model_type.h"
#include "dsp/exciter_type.h"
#include "dsp/pad_config.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace MembrumFit {

// Stage 1 output: loaded, normalised, mono audio.
struct LoadedSample {
    std::vector<float> samples;          // mono, float32, peak-normalised
    double             sampleRate = 0.0;
    float              originalPeakDbfs = 0.0f;  // before normalisation (dB)
    float              channelCorrelation = 1.0f; // 1.0 for mono; -1..1 for stereo
    std::string        sourcePath;
};

// Stage 2 output: onset + attack/decay windows.
struct SegmentedSample {
    std::size_t onsetSample      = 0;
    std::size_t attackEndSample  = 0;   // onset + 20 ms
    std::size_t decayStartSample = 0;   // onset + 5 ms
    std::size_t decayEndSample   = 0;   // RMS-gated tail
};

// Stage 3 output: attack-window features (Peeters 2011 / Lerch 2012).
struct AttackFeatures {
    float logAttackTime        = 0.0f;   // log10 seconds (2 % -> 90 % of peak)
    float spectralFlatness     = 0.0f;   // [0,1] noise-ness
    float peakSpectralCentroid = 0.0f;   // Hz
    std::array<float, 5> centroidTrajectory{};  // 5 hops across attack
    float preOnsetARPeak       = 0.0f;   // autocorrelation peak [0.5..20] ms lag
    float inharmonicity        = 0.0f;   // Σ |f_k - k*f1|² / (N*f1²)
    float decayTailEnergyRatio = 0.0f;   // decay / total energy
    float velocityEstimate     = 1.0f;   // from peak pre-normalisation
};

// Stage 5 output: one estimated mode (damped complex exponential).
struct Mode {
    float freqHz    = 0.0f;
    float decayRate = 0.0f;   // γ (s⁻¹), so t60 = log(1000)/γ
    float amplitude = 0.0f;   // linear
    float phase     = 0.0f;   // radians
    float quality   = 0.0f;   // per-mode SNR proxy for ranking
};

struct ModalDecomposition {
    std::vector<Mode> modes;         // sorted by amplitude descending
    float             residualRms = 0.0f;
    float             totalRms    = 0.0f;
};

enum class ModalMethod {
    MatrixPencil,
    ESPRIT,
};

// Stage 6 output: one body-model score.
struct BodyScore {
    Membrum::BodyModelType body = Membrum::BodyModelType::Membrane;
    float                  score      = 0.0f; // lower is better
    float                  confidence = 0.0f; // margin over 2nd-best, [0,1]
};

using BodyScoreList = std::array<BodyScore, 6>;

// End-to-end fit report for a single sample.
struct QualityReport {
    float initialLoss = 0.0f;
    float finalLoss   = 0.0f;
    int   bobyqaEvals = 0;
    bool  bobyqaConverged = false;
    bool  cmaesUsed   = false;
    bool  hasWarnings = false;
    std::vector<std::string> warnings;
};

struct FitResult {
    Membrum::PadConfig padConfig{};
    QualityReport      quality{};
};

// CLI/config bundle passed from main into the pipeline stages.
struct FitOptions {
    ModalMethod         modalMethod   = ModalMethod::MatrixPencil;
    double              targetSampleRate = 44100.0;
    int                 maxBobyqaEvals = 300;
    bool                enableGlobalCMAES = false;
    float               wSTFT = 0.6f;
    float               wMFCC = 0.2f;
    float               wEnv  = 0.2f;
    bool                writeJson = false;
    // Per-run classifier override. When set, fitSample() skips the body
    // classifier and dispatches straight to the named body's inversion.
    // Kit mode sets this transiently per pad from the parsed `--body-override`
    // MIDI->body map.
    std::optional<Membrum::BodyModelType> forcedBody;
    // Parsed `--body-override` map (MIDI note -> body). Consumed by the kit
    // loop in runMembrumFit(); unused in per-pad mode.
    std::map<int, Membrum::BodyModelType> bodyOverrides;
};

}  // namespace MembrumFit
