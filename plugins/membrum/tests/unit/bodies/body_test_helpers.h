#pragma once

// ==============================================================================
// Shared helpers for Phase 2 body-model contract tests.
// ==============================================================================
// Header-only; each test TU includes it. All functions are static/inline so
// multiple test compilation units do not cause ODR issues.
//
// Provides:
//   - isFiniteSample(): bit-manipulation NaN/Inf check (per CLAUDE.md)
//   - goertzelMag(): single-bin Goertzel DFT magnitude (for modal ratio tests)
//   - makeDefaultParams(): representative VoiceCommonParams for body tests
//   - runBodyImpulse(): trigger BodyBank with an impulse and collect samples
// ==============================================================================

#include "dsp/body_bank.h"
#include "dsp/body_model_type.h"
#include "dsp/voice_common_params.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace membrum_body_tests {

// Bit-manipulation NaN/Inf check. -ffast-math breaks std::isnan()/isfinite(),
// so we inspect the IEEE-754 exponent bits directly (see CLAUDE.md).
inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// Goertzel algorithm: single-bin DFT magnitude at `targetHz` over `numSamples`
// samples at `sampleRate`. Much cheaper than a full FFT when we only need a
// handful of frequency-specific measurements.
inline float goertzelMag(const float* samples,
                         int          numSamples,
                         double       sampleRate,
                         double       targetHz) noexcept
{
    if (numSamples <= 0 || sampleRate <= 0.0 || targetHz <= 0.0)
        return 0.0f;
    const double omega = 2.0 * 3.14159265358979323846 * targetHz / sampleRate;
    const double coeff = 2.0 * std::cos(omega);
    double q0 = 0.0;
    double q1 = 0.0;
    double q2 = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        q0 = coeff * q1 - q2 + static_cast<double>(samples[i]);
        q2 = q1;
        q1 = q0;
    }
    const double mag2 = q1 * q1 + q2 * q2 - q1 * q2 * coeff;
    return static_cast<float>(std::sqrt(std::max(0.0, mag2)));
}

// Find the peak frequency in `samples` (sweeping `numBins` linearly spaced
// bins from `fLow` to `fHigh`). Returns the bin frequency with the largest
// Goertzel magnitude.
inline double findPeakFrequency(const float* samples,
                                int          numSamples,
                                double       sampleRate,
                                double       fLow,
                                double       fHigh,
                                int          numBins = 512) noexcept
{
    double best = fLow;
    float  bestMag = -1.0f;
    const double df = (fHigh - fLow) / static_cast<double>(numBins - 1);
    for (int b = 0; b < numBins; ++b)
    {
        const double f = fLow + df * static_cast<double>(b);
        const float  mag = goertzelMag(samples, numSamples, sampleRate, f);
        if (mag > bestMag)
        {
            bestMag = mag;
            best    = f;
        }
    }
    return best;
}

// Estimate RT60 (time to decay by 60 dB) from an impulse response. Fits a
// linear regression of log-amplitude over time windows (each 25 ms) and
// returns the time required for a 60 dB drop based on the slope. This works
// for both short decays (buffer contains full decay) and long decays (only
// partial decay visible — slope extrapolates).
//
// Returns 0 only if the signal is effectively silent.
inline float estimateRT60(const float* samples,
                          int          numSamples,
                          double       sampleRate) noexcept
{
    if (numSamples <= 2 || sampleRate <= 0.0)
        return 0.0f;

    // RMS window = 25 ms
    const int windowSize = static_cast<int>(sampleRate * 0.025);
    if (windowSize < 16 || numSamples < windowSize * 4)
        return 0.0f;

    // Compute a sequence of per-window RMS values (dB) over the buffer.
    constexpr int kMaxWindows = 512;
    float dbSeq[kMaxWindows];
    int   numWindows = 0;
    float peakDb = -1e9f;
    for (int start = 0;
         start + windowSize <= numSamples && numWindows < kMaxWindows;
         start += windowSize)
    {
        double sumSq = 0.0;
        for (int i = 0; i < windowSize; ++i)
        {
            const float s = samples[start + i];
            sumSq += static_cast<double>(s) * static_cast<double>(s);
        }
        const float rms = static_cast<float>(
            std::sqrt(sumSq / static_cast<double>(windowSize)));
        const float db = (rms > 1e-12f)
            ? 20.0f * std::log10(rms)
            : -240.0f;
        dbSeq[numWindows++] = db;
        if (db > peakDb) peakDb = db;
    }
    if (numWindows < 4 || peakDb < -200.0f)
        return 0.0f;

    // Find the peak-energy window then walk forward, collecting samples
    // while signal is above (peakDb - 50 dB) and above -200 dB (i.e. real).
    int   peakIdx = 0;
    for (int i = 0; i < numWindows; ++i)
        if (dbSeq[i] >= peakDb - 0.001f) { peakIdx = i; break; }

    // Regression points: (t_sec, db) pairs.
    double sumT = 0.0, sumDB = 0.0, sumTT = 0.0, sumTDB = 0.0;
    int count = 0;
    const float floorDb = peakDb - 45.0f;  // well above -120 noise floor
    for (int i = peakIdx; i < numWindows; ++i)
    {
        const float db = dbSeq[i];
        if (db < floorDb || db < -150.0f)
            break;
        const double t = static_cast<double>(i - peakIdx) *
            static_cast<double>(windowSize) / sampleRate;
        sumT   += t;
        sumDB  += db;
        sumTT  += t * t;
        sumTDB += t * db;
        ++count;
    }
    if (count < 3)
        return 0.0f;

    // Linear fit: db(t) = a + b*t
    const double nd = static_cast<double>(count);
    const double denom = nd * sumTT - sumT * sumT;
    if (std::abs(denom) < 1e-12)
        return 0.0f;
    const double slope = (nd * sumTDB - sumT * sumDB) / denom;  // dB/sec
    if (slope >= -0.1)  // effectively not decaying (slope ~0 or positive)
        return 0.0f;

    const float rt60 = static_cast<float>(-60.0 / slope);
    return rt60;
}

inline Membrum::VoiceCommonParams makeDefaultParams() noexcept
{
    Membrum::VoiceCommonParams p;
    p.material    = 0.5f;
    p.size        = 0.5f;
    p.decay       = 0.8f;   // long decay for modal-ratio tests
    p.strikePos   = 0.37f;  // avoid symmetry nulls
    p.level       = 0.8f;
    p.modeStretch = 1.0f;
    p.decaySkew   = 0.0f;
    return p;
}

// Trigger the BodyBank with an impulse and fill `out` with `numSamples`
// samples. `bodyType` selects the body model; the bank is freshly prepared
// before the trigger. Uses the per-sample processSample() path so that unit
// tests can drive the body sample-by-sample.
inline void runBodyImpulse(Membrum::BodyBank&               bank,
                           Membrum::BodyModelType           bodyType,
                           const Membrum::VoiceCommonParams& params,
                           double                           sampleRate,
                           float*                           out,
                           int                              numSamples,
                           float pitchHz = 160.0f) noexcept
{
    bank.prepare(sampleRate, 0u);
    bank.setBodyModel(bodyType);
    bank.configureForNoteOn(params, pitchHz);

    // Kick with a single impulse (amplitude 1.0).
    out[0] = bank.processSample(1.0f);
    for (int i = 1; i < numSamples; ++i)
        out[i] = bank.processSample(0.0f);
}

// Measure Goertzel magnitudes at up to 5 target frequencies over `numSamples`
// samples and fill `weightsOut[0..numFreqs-1]`. `numFreqs` must be <= 5.
// Normalizes the resulting vector so that its L1 norm is 1. Returns false if
// the total magnitude is effectively zero.
inline bool measureNormalizedPeakWeights(const float* samples,
                                         int          numSamples,
                                         double       sampleRate,
                                         const double* targetFreqs,
                                         int          numFreqs,
                                         float*       weightsOut) noexcept
{
    if (numFreqs <= 0 || numFreqs > 5) return false;
    double total = 0.0;
    for (int k = 0; k < numFreqs; ++k)
    {
        const float mag =
            goertzelMag(samples, numSamples, sampleRate, targetFreqs[k]);
        weightsOut[k] = mag;
        total += static_cast<double>(mag);
    }
    if (total < 1e-9) return false;
    const float inv = static_cast<float>(1.0 / total);
    for (int k = 0; k < numFreqs; ++k)
        weightsOut[k] *= inv;
    return true;
}

// L1 distance between two `n`-length vectors.
inline float l1Distance(const float* a, const float* b, int n) noexcept
{
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float d = a[i] - b[i];
        sum += (d < 0.0f ? -d : d);
    }
    return sum;
}

// Compute Goertzel magnitude over a window of `samples` starting at `start`
// with `windowLen` samples. Returns magnitude (not normalized).
inline float goertzelWindowMag(const float* samples,
                               int          start,
                               int          windowLen,
                               double       sampleRate,
                               double       targetHz) noexcept
{
    return goertzelMag(samples + start, windowLen, sampleRate, targetHz);
}

} // namespace membrum_body_tests
