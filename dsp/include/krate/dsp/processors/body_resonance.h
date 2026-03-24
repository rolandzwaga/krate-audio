// ==============================================================================
// Layer 2: DSP Processor - Body Resonance
// ==============================================================================
// Hybrid modal bank + FDN body coloring processor for physical modelling.
// Post-resonator stage that adds instrument body character (violin, guitar,
// cello) with size, material, and mix controls.
//
// Signal chain:
//   input -> coupling filter (2 biquads)
//         -> first-order crossover LP -> 8 parallel modal biquads -> sum
//         -> first-order crossover HP -> 4-line Hadamard FDN -> sum
//         -> LP + HP recombination
//         -> radiation HPF (12 dB/oct biquad)
//         -> dry/wet mix -> output
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, RAII, C++20)
// - Principle IX: Layer 2 (depends on Layer 0/1 only)
// - Principle X: Impulse-invariant biquad design, linear fractional delay
//
// Reference: specs/131-body-resonance/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Number of modal resonator modes per body preset.
static constexpr size_t kBodyModeCount = 8;

/// Number of reference body presets (small/medium/large).
static constexpr size_t kBodyPresetCount = 3;

/// Number of FDN delay lines.
static constexpr size_t kBodyFDNLines = 4;

/// Maximum FDN delay buffer size (must be power of 2 for masking).
static constexpr size_t kBodyFDNMaxDelay = 256;

// =============================================================================
// BodyMode - Single modal resonance specification
// =============================================================================

struct BodyMode {
    float freq;     ///< Center frequency in Hz
    float gain;     ///< Relative gain (negative for anti-phase coupling)
    float qWood;    ///< Q factor at material=0 (wood character)
    float qMetal;   ///< Q factor at material=1 (metal character)
};

// =============================================================================
// Reference Modal Presets (FR-006, FR-020, R7)
// =============================================================================
// Physically-informed: guitar has A0/T1 anti-phase coupling (~110 Hz),
// violin has bridge hill (~2-3 kHz), all have sub-Helmholtz gain taper.
// Gains are raw (will be normalized at init so sum(|gain_i|) <= 1.0).

static constexpr std::array<std::array<BodyMode, kBodyModeCount>, kBodyPresetCount>
    kBodyPresets = {{
        // Index 0: Small (violin-scale) -- modes above ~275 Hz
        // Wood Q: higher at LF, drops significantly at HF (frequency-dependent
        // damping characteristic of wood). Metal Q: uniformly high.
        {{
            {275.0f,  0.60f,  50.0f, 200.0f},  // A0 Helmholtz
            {460.0f,  0.80f,  40.0f, 300.0f},  // B1- corpus bending
            {550.0f,  1.00f,  35.0f, 350.0f},  // B1+ (strongest)
            {700.0f,  0.50f,  25.0f, 250.0f},  // Higher plate
            {950.0f,  0.30f,  15.0f, 150.0f},  // Upper plate
            {1400.0f, 0.20f,  10.0f, 100.0f},  //
            {2500.0f, 0.40f,   5.0f,  60.0f},  // Bridge hill
            {3200.0f, 0.15f,   4.0f,  40.0f},  // Bridge hill tail
        }},
        // Index 1: Medium (guitar-scale) -- modes around ~90 Hz
        {{
            { 90.0f,  0.70f,  50.0f, 150.0f},  // A0 Helmholtz
            {110.0f, -0.50f,  45.0f, 200.0f},  // T(1,1) anti-phase w/ A0
            {280.0f,  0.80f,  35.0f, 250.0f},  // T(2,1)
            {370.0f,  1.00f,  30.0f, 300.0f},  // Second plate (strongest)
            {450.0f,  0.50f,  20.0f, 200.0f},  //
            {580.0f,  0.30f,  12.0f, 150.0f},  //
            {750.0f,  0.20f,   8.0f, 100.0f},  //
            {1100.0f, 0.10f,   5.0f,  80.0f},  // Upper register
        }},
        // Index 2: Large (cello-scale) -- modes below ~100 Hz
        // Strong LF emphasis: lowest modes have highest gains, decaying
        // rapidly above 250 Hz to ensure LF spectral concentration.
        {{
            { 60.0f,  0.80f,  45.0f, 120.0f},  // A0 Helmholtz
            {110.0f,  1.00f,  40.0f, 180.0f},  // First plate (strongest)
            {175.0f,  0.90f,  35.0f, 250.0f},  // Main radiating
            {250.0f,  0.50f,  28.0f, 220.0f},  //
            {340.0f,  0.25f,  20.0f, 180.0f},  //
            {500.0f,  0.12f,  12.0f, 140.0f},  //
            {750.0f,  0.08f,   8.0f, 100.0f},  //
            {1200.0f, 0.05f,   5.0f,  60.0f},  //
        }},
    }};

/// Base FDN delay lengths at 44.1 kHz (coprime primes, R4).
static constexpr std::array<float, kBodyFDNLines> kFDNBaseDelays = {11.0f, 17.0f, 23.0f, 31.0f};

// =============================================================================
// BodyResonance - Hybrid Modal Bank + FDN Body Coloring Processor
// =============================================================================

class BodyResonance {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    BodyResonance() noexcept = default;
    ~BodyResonance() noexcept = default;

    BodyResonance(const BodyResonance&) = delete;
    BodyResonance& operator=(const BodyResonance&) = delete;
    BodyResonance(BodyResonance&&) noexcept = default;
    BodyResonance& operator=(BodyResonance&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Initialize for given sample rate.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Configure smoothers (5 ms per R11)
        sizeSmoother_.configure(5.0f, sampleRate_);
        materialSmoother_.configure(5.0f, sampleRate_);
        mixSmoother_.configure(5.0f, sampleRate_);

        // Snap to defaults
        sizeSmoother_.snapTo(0.5f);
        materialSmoother_.snapTo(0.5f);
        mixSmoother_.snapTo(0.0f);

        // Compute smoothing coefficient for per-block pole/zero interpolation
        // ~5ms at the given sample rate, applied once per block (~32 samples)
        smoothCoeff_ = std::exp(-5000.0f / (5.0f * sampleRate_));

        // Initialize modal parameters
        interpolateModes(0.5f, 0.5f);

        // Snap current R/theta to targets (no smoothing on first prepare)
        for (size_t i = 0; i < kBodyModeCount; ++i) {
            currentR_[i] = targetR_[i];
            currentTheta_[i] = targetTheta_[i];
            currentGain_[i] = targetGain_[i];
        }

        // Build initial biquad coefficients
        updateModalBiquads();

        // Compute FDN parameters
        computeFDNDelayLengths(0.5f);
        computeAbsorptionCoeffs(0.5f);

        // Compute crossover
        updateCrossover(0.5f);
        crossoverAlpha_ = targetCrossoverAlpha_;  // Snap on init

        // Compute radiation HPF
        computeRadiationHPF();

        // Compute coupling filter
        updateCouplingFilter(0.5f);

        // Reset all state
        reset();

        prevSize_ = 0.5f;
        prevMaterial_ = 0.5f;
        prepared_ = true;
    }

    /// @brief Reset all filter states and delay buffers.
    void reset() noexcept {
        // Reset modal biquads
        for (auto& bq : modalBiquads_) {
            bq.reset();
        }

        // Reset coupling filters
        couplingPeakEq_.reset();
        couplingHighShelf_.reset();

        // Reset radiation HPF
        radiationHpf_.reset();

        // Reset crossover state
        crossoverLpState_ = 0.0f;

        // Reset FDN
        for (auto& buf : fdnDelayBuffers_) {
            buf.fill(0.0f);
        }
        fdnWritePos_.fill(0);
        fdnAbsorptionState_.fill(0.0f);
    }

    /// @brief Check if prepare() has been called.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set all body resonance parameters (FR-003).
    void setParams(float size, float material, float mix) noexcept {
        size = std::clamp(size, 0.0f, 1.0f);
        material = std::clamp(material, 0.0f, 1.0f);
        mix = std::clamp(mix, 0.0f, 1.0f);

        sizeSmoother_.setTarget(size);
        materialSmoother_.setTarget(material);
        mixSmoother_.setTarget(mix);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample (FR-002).
    [[nodiscard]] float process(float input) noexcept {
        // Advance mix smoother per sample
        const float mix = mixSmoother_.process();

        // Early-out for bypass (FR-018)
        if (mix == 0.0f) {
            return input;
        }

        // Coupling filter (FR-004)
        float coupled = couplingPeakEq_.process(input);
        coupled = couplingHighShelf_.process(coupled);

        // First-order crossover (FR-014)
        crossoverLpState_ = crossoverAlpha_ * crossoverLpState_ +
                            (1.0f - crossoverAlpha_) * coupled;
        const float lpOut = crossoverLpState_;
        const float hpOut = coupled - lpOut;

        // Modal bank on LP path (FR-005)
        float modalSum = 0.0f;
        for (size_t i = 0; i < kBodyModeCount; ++i) {
            modalSum += modalBiquads_[i].process(lpOut) * currentGain_[i];
        }

        // FDN on HP path (FR-010)
        // Scale FDN input by size-dependent gain to balance modal vs
        // broadband character. Larger bodies reduce FDN contribution.
        const float fdnOut = processFDN(hpOut * fdnOutputGain_);

        // Recombine (FR-014)
        const float wet = modalSum + fdnOut;

        // Radiation HPF (FR-015)
        const float filtered = radiationHpf_.process(wet);

        // Dry/wet mix
        return input + mix * (filtered - input);
    }

    /// @brief Process a block of samples (FR-017).
    void processBlock(const float* input, float* output,
                      size_t numSamples) noexcept {
        if (!prepared_) {
            // Safety: copy input to output if not prepared
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = input[i];
            }
            return;
        }

        // Per-block coefficient update
        const float size = sizeSmoother_.process();
        const float material = materialSmoother_.process();

        // Advance smoothers for the rest of the block
        sizeSmoother_.advanceSamples(numSamples > 1 ? numSamples - 1 : 0);
        materialSmoother_.advanceSamples(numSamples > 1 ? numSamples - 1 : 0);

        // Check if parameters changed
        constexpr float kParamEpsilon = 1e-6f;
        const bool sizeChanged =
            std::abs(size - prevSize_) > kParamEpsilon;
        const bool materialChanged =
            std::abs(material - prevMaterial_) > kParamEpsilon;

        if (sizeChanged || materialChanged) {
            interpolateModes(size, material);
            computeFDNDelayLengths(size);
            updateCrossover(size);
            computeRadiationHPF();

            if (materialChanged) {
                computeAbsorptionCoeffs(material);
                updateCouplingFilter(material);
            }

            prevSize_ = size;
            prevMaterial_ = material;
        }

        // Smooth pole/zero toward targets
        smoothModalParams();
        updateModalBiquads();

        // Per-sample processing
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process(input[i]);
        }
    }

private:
    // =========================================================================
    // Impulse-Invariant Biquad Design (FR-008)
    // =========================================================================

    [[nodiscard]] static BiquadCoefficients computeImpulseInvariantCoeffs(
        float freq, float Q, float sampleRate) noexcept {
        const float theta = kTwoPi * freq / sampleRate;
        const float R = std::exp(-kPi * freq / (Q * sampleRate));

        BiquadCoefficients c;
        c.a1 = -2.0f * R * std::cos(theta);
        c.a2 = R * R;
        c.b0 = 1.0f - R;
        c.b1 = 0.0f;
        c.b2 = -(1.0f - R);

        return c;
    }

    // =========================================================================
    // Modal Preset Interpolation (FR-007, FR-023)
    // =========================================================================

    void interpolateModes(float size, float material) noexcept {
        // Determine which two presets to interpolate between
        // size 0..0.5 -> preset 0 (small) to preset 1 (medium)
        // size 0.5..1 -> preset 1 (medium) to preset 2 (large)
        size_t presetA = 0;
        size_t presetB = 1;
        float t = 0.0f;

        if (size <= 0.5f) {
            presetA = 0;
            presetB = 1;
            t = size * 2.0f;  // 0..1
        } else {
            presetA = 1;
            presetB = 2;
            t = (size - 0.5f) * 2.0f;  // 0..1
        }

        float gainSum = 0.0f;

        for (size_t i = 0; i < kBodyModeCount; ++i) {
            const auto& mA = kBodyPresets[presetA][i];
            const auto& mB = kBodyPresets[presetB][i];

            // Log-linear frequency interpolation (FR-007)
            const float logFreqA = std::log(mA.freq);
            const float logFreqB = std::log(mB.freq);
            const float freq = std::exp(logFreqA + t * (logFreqB - logFreqA));

            // Linear gain interpolation
            const float gain = mA.gain + t * (mB.gain - mA.gain);

            // Q interpolation via R-domain (FR-023): log-linear interpolation
            // of pole radius R, then convert back to Q. Since R = exp(-pi*f/(Q*sr)),
            // log(R) = -pi*f/(Q*sr), so log-linear R interpolation is equivalent
            // to linear interpolation of 1/Q (harmonic mean in Q space).
            const float qWoodA = mA.qWood;
            const float qWoodB = mB.qWood;
            const float qMetalA = mA.qMetal;
            const float qMetalB = mB.qMetal;

            // Compute R for each Q value at the interpolated frequency
            // R = exp(-pi * freq / (Q * sampleRate))
            const float piF_sr = kPi * freq / sampleRate_;
            const float rWoodA = std::exp(-piF_sr / qWoodA);
            const float rWoodB = std::exp(-piF_sr / qWoodB);
            const float rMetalA = std::exp(-piF_sr / qMetalA);
            const float rMetalB = std::exp(-piF_sr / qMetalB);

            // Size interpolation in log(R) domain (between preset A and B)
            const float logRWood = std::log(rWoodA) +
                                   t * (std::log(rWoodB) - std::log(rWoodA));
            const float logRMetal = std::log(rMetalA) +
                                    t * (std::log(rMetalB) - std::log(rMetalA));

            // Material interpolation in log(R) domain (between wood and metal)
            const float logR = logRWood + material * (logRMetal - logRWood);

            // Convert back to Q from interpolated R:
            // Since logR = -pi*freq/(Q*sr), we have Q = -pi*freq/(sr*logR)
            // piF_sr = pi*freq/sr, so Q = -piF_sr / logR
            const float Q = (logR < -1e-10f) ? (-piF_sr / logR) : 100.0f;

            // Clamp frequency below Nyquist/2 for stability (data-model.md)
            const float maxFreq = sampleRate_ * 0.25f;
            const float clampedFreq = std::min(freq, maxFreq);

            // Compute target R, theta for pole/zero smoothing (FR-009)
            targetTheta_[i] = kTwoPi * clampedFreq / sampleRate_;
            targetR_[i] = std::exp(-kPi * clampedFreq / (Q * sampleRate_));

            // Ensure stability: R must be < 1
            targetR_[i] = std::min(targetR_[i], 0.9999f);

            targetGain_[i] = gain;
            gainSum += std::abs(gain);
        }

        // Normalize gains so sum(|gain_i|) <= 1.0 (FR-016)
        if (gainSum > 1.0f) {
            const float scale = 1.0f / gainSum;
            for (size_t i = 0; i < kBodyModeCount; ++i) {
                targetGain_[i] *= scale;
            }
        }

        // Store lowest mode frequency for radiation HPF
        lowestModeFreq_ = std::exp(
            std::log(kBodyPresets[presetA][0].freq) +
            t * (std::log(kBodyPresets[presetB][0].freq) -
                 std::log(kBodyPresets[presetA][0].freq)));

        // FDN input gain: scale with size. Larger bodies emphasize the modal
        // bank character; smaller bodies use more FDN for broadband HF.
        fdnOutputGain_ = 0.01f + 0.29f * (1.0f - size);  // 0.30 at size=0, 0.01 at size=1
    }

    // =========================================================================
    // Pole/Zero Domain Smoothing (FR-009, FR-017)
    // =========================================================================

    void smoothModalParams() noexcept {
        for (size_t i = 0; i < kBodyModeCount; ++i) {
            currentR_[i] += (1.0f - smoothCoeff_) *
                            (targetR_[i] - currentR_[i]);
            currentTheta_[i] += (1.0f - smoothCoeff_) *
                                (targetTheta_[i] - currentTheta_[i]);
            currentGain_[i] += (1.0f - smoothCoeff_) *
                               (targetGain_[i] - currentGain_[i]);
        }
        // Smooth crossover alpha to prevent clicks during size sweeps
        crossoverAlpha_ += (1.0f - smoothCoeff_) *
                           (targetCrossoverAlpha_ - crossoverAlpha_);
    }

    void updateModalBiquads() noexcept {
        for (size_t i = 0; i < kBodyModeCount; ++i) {
            const float R = currentR_[i];
            const float theta = currentTheta_[i];

            BiquadCoefficients c;
            c.a1 = -2.0f * R * std::cos(theta);
            c.a2 = R * R;
            c.b0 = 1.0f - R;
            c.b1 = 0.0f;
            c.b2 = -(1.0f - R);

            modalBiquads_[i].setCoefficients(c);
        }
    }

    // =========================================================================
    // FDN Delay Length Scaling (FR-011, FR-022, R4)
    // =========================================================================

    void computeFDNDelayLengths(float size) noexcept {
        const float srRatio = sampleRate_ / 44100.0f;
        const float sizeScale = 0.3f + 0.7f * std::pow(size, 0.7f);

        for (size_t i = 0; i < kBodyFDNLines; ++i) {
            float len = kFDNBaseDelays[i] * sizeScale * srRatio;
            // Clamp to [8, 80] scaled by sample rate ratio
            const float minLen = 8.0f * srRatio;
            const float maxLen = 80.0f * srRatio;
            len = std::clamp(len, minLen, maxLen);
            fdnDelayLengths_[i] = len;
        }
    }

    // =========================================================================
    // FDN Absorption Filters (FR-012, FR-013, R5)
    // =========================================================================

    void computeAbsorptionCoeffs(float material) noexcept {
        // T60 values interpolated by material (log-linear per R5)
        const float t60dc_wood = 0.15f;
        const float t60dc_metal = 1.5f;
        const float t60nyq_wood = 0.008f;
        const float t60nyq_metal = 1.0f;

        // Hard caps (FR-013, SC-001)
        const float t60dc_cap_wood = 0.3f;
        const float t60dc_cap_metal = 2.0f;

        // Log-linear interpolation
        float t60dc = std::exp(std::log(t60dc_wood) +
                               material * (std::log(t60dc_metal) -
                                           std::log(t60dc_wood)));
        float t60nyq = std::exp(std::log(t60nyq_wood) +
                                material * (std::log(t60nyq_metal) -
                                            std::log(t60nyq_wood)));

        // Apply hard cap: interpolate cap with material
        const float t60dc_cap = t60dc_cap_wood +
                                material * (t60dc_cap_metal - t60dc_cap_wood);
        t60dc = std::min(t60dc, t60dc_cap);
        t60nyq = std::min(t60nyq, t60dc);  // Nyquist T60 <= DC T60

        // Per-delay-line Jot absorption (R5)
        for (size_t i = 0; i < kBodyFDNLines; ++i) {
            const float M = fdnDelayLengths_[i];

            // R0 = 10^(-3 / (T60_DC * sampleRate)) per sample
            // R0^M = 10^(-3*M / (T60_DC * sampleRate))
            const float r0m = std::pow(10.0f, -3.0f * M /
                                                   (t60dc * sampleRate_));
            const float rpim = std::pow(10.0f, -3.0f * M /
                                                    (t60nyq * sampleRate_));

            // p_i = (R0^M - Rpi^M) / (R0^M + Rpi^M)
            fdnAbsorptionCoeff_[i] = (r0m - rpim) / (r0m + rpim + 1e-12f);

            // g_i = 2 * R0^M * Rpi^M / (R0^M + Rpi^M)
            fdnAbsorptionGain_[i] = 2.0f * r0m * rpim /
                                    (r0m + rpim + 1e-12f);
        }
    }

    // =========================================================================
    // FDN Processing (FR-010)
    // =========================================================================

    [[nodiscard]] float processFDN(float input) noexcept {
        std::array<float, kBodyFDNLines> readValues{};

        // Read from each delay line with linear interpolation
        for (size_t i = 0; i < kBodyFDNLines; ++i) {
            const float delay = fdnDelayLengths_[i];
            const size_t intDelay = static_cast<size_t>(delay);
            const float frac = delay - static_cast<float>(intDelay);
            const size_t mask = kBodyFDNMaxDelay - 1;

            const size_t idx0 = (fdnWritePos_[i] - intDelay) & mask;
            const size_t idx1 = (fdnWritePos_[i] - intDelay - 1) & mask;

            // Linear interpolation for fractional delay (FR-017)
            readValues[i] = fdnDelayBuffers_[i][idx0] * (1.0f - frac) +
                            fdnDelayBuffers_[i][idx1] * frac;
        }

        // Apply absorption filters (one-pole, FR-012)
        for (size_t i = 0; i < kBodyFDNLines; ++i) {
            const float x = readValues[i];
            fdnAbsorptionState_[i] = fdnAbsorptionGain_[i] * x +
                                     fdnAbsorptionCoeff_[i] *
                                         fdnAbsorptionState_[i];
            readValues[i] = fdnAbsorptionState_[i];
        }

        // Apply 4x4 Hadamard butterfly (FR-010, R3)
        // Stage 1: stride = 2
        const float a0 = readValues[0] + readValues[2];
        const float a1 = readValues[1] + readValues[3];
        const float a2 = readValues[0] - readValues[2];
        const float a3 = readValues[1] - readValues[3];
        // Stage 2: stride = 1
        readValues[0] = (a0 + a1) * 0.5f;
        readValues[1] = (a0 - a1) * 0.5f;
        readValues[2] = (a2 + a3) * 0.5f;
        readValues[3] = (a2 - a3) * 0.5f;

        // Add input and write to delay lines
        float fdnOutput = 0.0f;
        for (size_t i = 0; i < kBodyFDNLines; ++i) {
            const float writeVal = readValues[i] + input *
                                   (1.0f / static_cast<float>(kBodyFDNLines));
            const size_t mask = kBodyFDNMaxDelay - 1;
            fdnWritePos_[i] = (fdnWritePos_[i] + 1) & mask;
            fdnDelayBuffers_[i][fdnWritePos_[i]] = writeVal;
            fdnOutput += readValues[i];
        }

        // Scale output to maintain energy
        return fdnOutput * (1.0f / static_cast<float>(kBodyFDNLines));
    }

    // =========================================================================
    // Crossover (FR-014, R6)
    // =========================================================================

    void updateCrossover(float size) noexcept {
        // Crossover frequency: ~500 Hz at size=0.5, scales with body size
        // Smaller body = higher crossover, larger = lower crossover.
        // The crossover determines the split between modal bank (LP) and FDN (HP).
        const float fc = 500.0f * std::exp(-1.5f * (size - 0.5f));
        const float clampedFc = std::clamp(fc, 150.0f, 3000.0f);
        targetCrossoverAlpha_ = std::exp(-kTwoPi * clampedFc / sampleRate_);
    }

    // =========================================================================
    // Radiation HPF (FR-015, R8)
    // =========================================================================

    void computeRadiationHPF() noexcept {
        float cutoff = lowestModeFreq_ * 0.7f;
        cutoff = std::max(cutoff, 20.0f);
        cutoff = std::min(cutoff, sampleRate_ * 0.4f);
        radiationHpf_.configure(FilterType::Highpass, cutoff,
                                kButterworthQ, 0.0f, sampleRate_);
    }

    // =========================================================================
    // Coupling Filter (FR-004, R9)
    // =========================================================================

    void updateCouplingFilter(float material) noexcept {
        // Peak EQ: wood=+3dB @250Hz Q=1.5, metal=+0.5dB @250Hz Q=0.8
        const float peakGain = 3.0f + material * (0.5f - 3.0f);
        const float peakQ = 1.5f + material * (0.8f - 1.5f);
        couplingPeakEq_.configure(FilterType::Peak, 250.0f, peakQ,
                                  peakGain, sampleRate_);

        // High shelf: wood=-2dB @2kHz, metal=0dB @2kHz
        const float shelfGain = -2.0f + material * (0.0f - (-2.0f));
        couplingHighShelf_.configure(FilterType::HighShelf, 2000.0f,
                                     kButterworthQ, shelfGain, sampleRate_);
    }

    // =========================================================================
    // Internal State
    // =========================================================================

    // Coupling filter (2 biquads: peak EQ + high shelf)
    Biquad couplingPeakEq_;
    Biquad couplingHighShelf_;

    // Modal bank (8 parallel biquads)
    std::array<Biquad, kBodyModeCount> modalBiquads_;

    // Pole/zero interpolation state (per mode)
    std::array<float, kBodyModeCount> currentR_{};
    std::array<float, kBodyModeCount> currentTheta_{};
    std::array<float, kBodyModeCount> targetR_{};
    std::array<float, kBodyModeCount> targetTheta_{};
    std::array<float, kBodyModeCount> currentGain_{};
    std::array<float, kBodyModeCount> targetGain_{};

    // FDN (4 delay lines)
    std::array<std::array<float, kBodyFDNMaxDelay>, kBodyFDNLines> fdnDelayBuffers_{};
    std::array<size_t, kBodyFDNLines> fdnWritePos_{};
    std::array<float, kBodyFDNLines> fdnDelayLengths_{};
    std::array<float, kBodyFDNLines> fdnAbsorptionState_{};
    std::array<float, kBodyFDNLines> fdnAbsorptionCoeff_{};
    std::array<float, kBodyFDNLines> fdnAbsorptionGain_{};

    // First-order crossover state
    float crossoverLpState_ = 0.0f;
    float crossoverAlpha_ = 0.0f;
    float targetCrossoverAlpha_ = 0.0f;

    // Radiation HPF (1 biquad, 12 dB/oct)
    Biquad radiationHpf_;

    // Parameter smoothing
    OnePoleSmoother sizeSmoother_;
    OnePoleSmoother materialSmoother_;
    OnePoleSmoother mixSmoother_;

    // Configuration
    float sampleRate_ = 44100.0f;
    float smoothCoeff_ = 0.0f;
    float lowestModeFreq_ = 90.0f;
    float prevSize_ = 0.5f;
    float prevMaterial_ = 0.5f;
    float fdnOutputGain_ = 0.25f;  ///< FDN input scaling (size-dependent)
    bool prepared_ = false;
};

}  // namespace DSP
}  // namespace Krate
