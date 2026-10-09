// Vorago Phase 4 (specs/vorago-phase4-spectral-smear): SpectralSmear lifecycle,
// geometry, control clamps, render-path boundaries and the deterministic
// sample-domain criteria (SC-002, SC-003, SC-007, SC-008, SC-009, SC-011,
// SC-012 (c), SC-017, SC-018 (a)).
//
// NO NON-FINITE VALUE MAY BE NAMED IN THIS TU. It is deliberately NOT in
// dsp/tests/CMakeLists.txt's -fno-fast-math block, so the FR-008/FR-009 guards
// are proved in the /fp:fast + -ffast-math mode the header actually ships in
// (the dsp/tests/unit/systems/resonance_drift_network_test.cpp:38-42 house
// rule). The non-finite arms live in spectral_smear_nonfinite_test.cpp.
#include <catch2/catch_test_macros.hpp>

// allocation_detector.h ONLY. NEVER allocation_operator_overrides.h: the global
// operator new/delete replacements must be included from exactly one TU per
// binary, and for dsp_processors_tests that TU is
// dsp/tests/unit/processors/brownian_drift_test.cpp:28. A second include is a
// duplicate-symbol link error (tools/lint-allocation-operator-overrides.js).
#include <allocation_detector.h>
#include <artifact_detection.h>

#include "render_fingerprint.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/spectral_smear.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

using Krate::DSP::SpectralSmear;

namespace {

// ---------------------------------------------------------------------------
// Shared fixture helpers (introduced by T003; later tasks in this TU reuse them)
// ---------------------------------------------------------------------------

constexpr double      kFs48   = 48000.0;  ///< the reference rate
constexpr std::size_t kRefFft = 2048;     ///< the reference geometry (default fftSize)
constexpr std::size_t kRefHop = 512;      ///< kRefFft / kOverlapFactor

[[nodiscard]] SpectralSmear makePrepared(std::size_t fftSize, bool enabled = true) {
    SpectralSmear smear;
    smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = fftSize, .enabled = enabled});
    return smear;
}

/// Copies the inputs into the output buffers and renders them IN PLACE, calling
/// processBlock with the block sizes of `partition`, cycled. Every partition
/// entry must be non-zero.
void renderStereo(SpectralSmear& smear,
                  const std::vector<float>& inL, const std::vector<float>& inR,
                  std::vector<float>& outL, std::vector<float>& outR,
                  const std::vector<std::size_t>& partition) {
    outL = inL;
    outR = inR;
    const std::size_t total = std::min(outL.size(), outR.size());
    std::size_t pos  = 0;
    std::size_t step = 0;
    while (pos < total) {
        const std::size_t block = std::min(partition[step % partition.size()], total - pos);
        smear.processBlock(outL.data() + pos, outR.data() + pos, block);
        pos += block;
        ++step;
    }
}

void fillNoise(std::vector<float>& buffer, std::uint32_t seed) {
    Krate::DSP::Xorshift32 rng{seed};
    for (auto& sample : buffer) {
        sample = 0.5f * rng.nextFloat();
    }
}

/// The footprint formula of plan S10, recomputed from the PUBLIC geometry reads
/// so a geometry change cannot silently invalidate the assertion.
[[nodiscard]] std::size_t expectedAllocatedBytes(const SpectralSmear& smear) {
    const std::size_t fftSize = smear.getFftSize();
    const std::size_t hop     = smear.getHopSize();
    const std::size_t numBins = smear.getNumBins();
    const std::size_t fifoCapacity =
        std::bit_ceil(fftSize + SpectralSmear::kProcessChunkSamples + hop);
    return (4u * numBins + 2u * numBins + 2u * fifoCapacity + 2u * hop) * sizeof(float);
}

// ---------------------------------------------------------------------------
// Render-path helpers (introduced by T006)
// ---------------------------------------------------------------------------

constexpr double kTestPi = 3.14159265358979323846;

/// Uniform bipolar noise at a chosen RMS. Xorshift32::nextFloat() is bipolar on
/// [-1, +1] (core/random.h:59-63), whose RMS is amplitude / sqrt(3).
void fillNoiseAtRms(std::vector<float>& buffer, std::uint32_t seed, float targetRms) {
    Krate::DSP::Xorshift32 rng{seed};
    const float amplitude = targetRms * 1.7320508f;  // sqrt(3)
    for (auto& sample : buffer) {
        sample = amplitude * rng.nextFloat();
    }
}

/// A five-partial tone with 1/n partial amplitudes, normalised to `peak`.
void fillPartialTone(std::vector<float>& buffer, double sampleRate, double fundamentalHz,
                     float peak) {
    double normaliser = 0.0;
    for (std::size_t partial = 1; partial <= 5u; ++partial) {
        normaliser += 1.0 / static_cast<double>(partial);
    }
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        double value = 0.0;
        for (std::size_t partial = 1; partial <= 5u; ++partial) {
            const double hz = fundamentalHz * static_cast<double>(partial);
            value += std::sin(2.0 * kTestPi * hz * t) / static_cast<double>(partial);
        }
        buffer[i] = static_cast<float>(static_cast<double>(peak) * value / normaliser);
    }
}

/// Index of the largest-magnitude sample; 0 for an empty buffer.
[[nodiscard]] std::size_t argmaxAbs(const std::vector<float>& buffer) {
    std::size_t best      = 0;
    float       bestValue = -1.0f;
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        const float magnitude = std::abs(buffer[i]);
        if (magnitude > bestValue) {
            bestValue = magnitude;
            best      = i;
        }
    }
    return best;
}

[[nodiscard]] double rmsOf(const float* data, std::size_t count) {
    if (count == 0u) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double s = static_cast<double>(data[i]);
        sum += s * s;
    }
    return std::sqrt(sum / static_cast<double>(count));
}

/// SC-011's pseudo-random block schedule, drawn from the stated set. Deliberately
/// mixes sizes below, at and above kProcessChunkSamples and the hop, so a
/// "drain when available" implementation shows up as a partition-dependent
/// offset (960 / 1020 / 1022 measured at chunk 64 / 30 / 7 in
/// effects/aether_reverb.h:643-660) rather than as an equal-but-late render.
[[nodiscard]] std::vector<std::size_t> makeRandomSchedule(std::uint32_t seed, std::size_t length) {
    const std::size_t choices[10] = {1u, 7u, 30u, 63u, 64u, 65u, 300u, 512u, 1024u, 4096u};
    Krate::DSP::Xorshift32 rng{seed};
    std::vector<std::size_t> schedule(length, 0u);
    for (auto& entry : schedule) {
        const float unit  = 0.5f * (rng.nextFloat() + 1.0f);  // bipolar -> [0, 1]
        auto        index = static_cast<std::size_t>(unit * 10.0f);
        if (index > 9u) index = 9u;
        entry = choices[index];
    }
    return schedule;
}

} // namespace

// ---------------------------------------------------------------------------
// Geometry and lifecycle (FR-010, FR-011, FR-018; plan S2 step 1, S9)
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_Geometry", "[spectral_smear]") {
    SECTION("an unprepared instance answers the documented neutrals") {
        SpectralSmear smear;
        REQUIRE(smear.isPrepared() == false);
        REQUIRE(smear.isEnabled() == false);
        REQUIRE(smear.getFftSize() == 0u);
        REQUIRE(smear.getHopSize() == 0u);
        REQUIRE(smear.getNumBins() == 0u);
        REQUIRE(smear.getSampleRate() == 0.0);
        REQUIRE(smear.getLatencySamples() == 0u);
        REQUIRE(smear.getAllocatedBytes() == 0u);
        REQUIRE(smear.getClampEngagements() == 0u);
        REQUIRE(smear.getPoisonEngagements() == 0u);
        REQUIRE(smear.getAppliedSmearAmount() == 0.0f);
        REQUIRE(smear.getAppliedDecoherence() == 0.0f);
        REQUIRE(smear.getAppliedSmearTilt() == 0.0f);
    }

    SECTION("fftSize is clamped into [512, 4096] and THEN bit_floor'd") {
        const std::size_t requested[6] = {
            0u, 1u, 100u, 513u, 5000u, std::numeric_limits<std::size_t>::max()};
        const std::size_t expected[6] = {512u, 512u, 512u, 512u, 4096u, 4096u};

        for (std::size_t i = 0; i < 6u; ++i) {
            SpectralSmear smear;
            smear.prepare(kFs48,
                          SpectralSmear::PrepareConfig{.fftSize = requested[i], .enabled = true});
            INFO("requested fftSize = " << requested[i]);
            REQUIRE(smear.isPrepared() == true);
            REQUIRE(smear.getFftSize() == expected[i]);
            REQUIRE(smear.getHopSize() == expected[i] / 4u);
            REQUIRE(smear.getNumBins() == expected[i] / 2u + 1u);
            REQUIRE(smear.getSampleRate() == kFs48);
        }
    }

    SECTION("a non-positive sample rate leaves the instance unprepared") {
        for (const double rate : {0.0, -48000.0}) {
            SpectralSmear smear;
            smear.prepare(rate, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
            INFO("sample rate = " << rate);
            REQUIRE(smear.isPrepared() == false);
            REQUIRE(smear.getFftSize() == 0u);
            REQUIRE(smear.getHopSize() == 0u);
            REQUIRE(smear.getNumBins() == 0u);
            REQUIRE(smear.getSampleRate() == 0.0);
        }
    }

    SECTION("a bad re-prepare disarms a good one") {
        SpectralSmear smear = makePrepared(kRefFft);
        REQUIRE(smear.getFftSize() == kRefFft);
        REQUIRE(smear.getHopSize() == kRefHop);

        smear.prepare(0.0, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
        REQUIRE(smear.isPrepared() == false);
        REQUIRE(smear.getFftSize() == 0u);
        REQUIRE(smear.getHopSize() == 0u);
        REQUIRE(smear.getNumBins() == 0u);
        REQUIRE(smear.getSampleRate() == 0.0);
        // "this instance will not render", NOT "this instance holds no heap":
        // the vectors are deliberately retained (plan S2 step 1, S9). Only the
        // reads are asserted here; nothing is claimed about the heap.
        REQUIRE(smear.getAllocatedBytes() == 0u);
    }

    SECTION("every geometry read follows the LAST prepare() call") {
        SpectralSmear smear;

        smear.prepare(48000.0, SpectralSmear::PrepareConfig{.fftSize = 2048u, .enabled = true});
        REQUIRE(smear.getSampleRate() == 48000.0);
        REQUIRE(smear.getFftSize() == 2048u);
        REQUIRE(smear.getHopSize() == 512u);
        REQUIRE(smear.getNumBins() == 1025u);

        smear.prepare(96000.0, SpectralSmear::PrepareConfig{.fftSize = 512u, .enabled = true});
        REQUIRE(smear.getSampleRate() == 96000.0);
        REQUIRE(smear.getFftSize() == 512u);
        REQUIRE(smear.getHopSize() == 128u);
        REQUIRE(smear.getNumBins() == 257u);

        smear.prepare(44100.0, SpectralSmear::PrepareConfig{.fftSize = 4096u, .enabled = true});
        REQUIRE(smear.getSampleRate() == 44100.0);
        REQUIRE(smear.getFftSize() == 4096u);
        REQUIRE(smear.getHopSize() == 1024u);
        REQUIRE(smear.getNumBins() == 2049u);
    }
}

// ---------------------------------------------------------------------------
// Latency, arms (a), (d), (e) -- SC-002. Arms (b)/(c) need the render path and
// land in T006.
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_Latency", "[spectral_smear]") {
    SECTION("(a) reported latency is exactly fftSize when prepared enabled") {
        SpectralSmear unprepared;
        REQUIRE(unprepared.getLatencySamples() == 0u);

        for (const std::size_t fftSize : {512u, 1024u, 2048u, 4096u}) {
            SpectralSmear smear = makePrepared(fftSize);
            INFO("fftSize = " << fftSize);
            REQUIRE(smear.getLatencySamples() == fftSize);
        }
    }

    SECTION("(d) a disabled instance is a true bypass: no latency, no heap, bit-identical") {
        SpectralSmear smear;
        smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = false});
        REQUIRE(smear.isPrepared() == true);
        REQUIRE(smear.isEnabled() == false);
        REQUIRE(smear.getLatencySamples() == 0u);
        REQUIRE(smear.getAllocatedBytes() == 0u);

        const std::size_t numSamples = static_cast<std::size_t>(10.0 * kFs48);  // 10 s
        std::vector<float> inL(numSamples, 0.0f);
        std::vector<float> inR(numSamples, 0.0f);
        fillNoise(inL, 0x11111111u);
        fillNoise(inR, 0x22222222u);

        std::vector<float> outL;
        std::vector<float> outR;
        renderStereo(smear, inL, inR, outL, outR, {512u});

        // Exact equality is legitimate here: this asserts "these bytes were NOT
        // written", not a pinned computation (lint-float-bit-goldens.js).
        bool identical = (outL.size() == inL.size()) && (outR.size() == inR.size());
        for (std::size_t i = 0; identical && i < numSamples; ++i) {
            identical = (outL[i] == inL[i]) && (outR[i] == inR[i]);
        }
        REQUIRE(identical);
    }

    SECTION("(e) a disabled instance agrees with an enabled twin on all geometry") {
        SpectralSmear enabled  = makePrepared(kRefFft, true);
        SpectralSmear disabled = makePrepared(kRefFft, false);

        REQUIRE(disabled.getFftSize() == enabled.getFftSize());
        REQUIRE(disabled.getHopSize() == enabled.getHopSize());
        REQUIRE(disabled.getNumBins() == enabled.getNumBins());
        REQUIRE(disabled.getSampleRate() == enabled.getSampleRate());

        for (SpectralSmear* smear : {&enabled, &disabled}) {
            smear->setSmearAmount(0.7f);
            smear->setDecoherence(0.3f);
            smear->setSmearTilt(-0.4f);
        }

        // No frame ever runs on a disabled instance, so the applied reads mirror
        // the targets EXACTLY (plan S9 -- produced by the getters themselves,
        // not by the prepare-time smoother snap).
        REQUIRE(disabled.getAppliedSmearAmount() == 0.7f);
        REQUIRE(disabled.getAppliedDecoherence() == 0.3f);
        REQUIRE(disabled.getAppliedSmearTilt() == -0.4f);

        REQUIRE(disabled.getLatencySamples() == 0u);
        REQUIRE(disabled.getAllocatedBytes() == 0u);
        REQUIRE(enabled.getLatencySamples() == 2048u);
        REQUIRE(enabled.getAllocatedBytes() == 61464u);
    }

    // WHY THE IMPULSE CANNOT SIT AT SAMPLE 0, and why this arm would otherwise
    // "pass" for the wrong reason: Window::generateHann is the PERIODIC variant,
    // so window[0] == 0.5 - 0.5*cos(0) == 0.0f exactly
    // (core/window_functions.h:111-121). STFT::analyze reads the oldest fftSize
    // samples from the stream origin (primitives/stft.h:144-171), so input
    // sample 0 is covered by frame 0 ONLY, at window index 0, where the window
    // is zero. An impulse there is annihilated, the whole output is identically
    // 0.0f, and argmax of an all-zero buffer is index 0 - which a correct
    // implementation and a broken one would both report as "a peak at 0".
    // Index 2 * fftSize also lands the peak at 3 * fftSize, clear of
    // OverlapAdd's COLA ramp-up region [fftSize, 2*fftSize - hop) (plan S11).
    SECTION("(b) an impulse comes back exactly getLatencySamples() late") {
        for (const std::size_t fftSize : {512u, 1024u, 2048u, 4096u}) {
            SpectralSmear     smear   = makePrepared(fftSize);
            const std::size_t latency = smear.getLatencySamples();
            INFO("fftSize = " << fftSize << " latency = " << latency);
            REQUIRE(latency == fftSize);

            const std::size_t impulseIndex = 2u * fftSize;
            const std::size_t numSamples   = 6u * fftSize;
            std::vector<float> inL(numSamples, 0.0f);
            std::vector<float> inR(numSamples, 0.0f);
            inL[impulseIndex] = 1.0f;
            inR[impulseIndex] = 1.0f;

            std::vector<float> outL;
            std::vector<float> outR;
            renderStereo(smear, inL, inR, outL, outR, {512u});

            REQUIRE(argmaxAbs(outL) == impulseIndex + latency);
            REQUIRE(argmaxAbs(outR) == impulseIndex + latency);

            // Exact 0.0f: these are the warm-up counter's zeros, i.e. "these
            // bytes were not written from the FIFO", not a pinned computation.
            bool leadingZeros = true;
            for (std::size_t i = 0; i < latency; ++i) {
                leadingZeros = leadingZeros && (outL[i] == 0.0f) && (outR[i] == 0.0f);
            }
            REQUIRE(leadingZeros);
        }
    }

    SECTION("(c) latency and the leading zeros do not move with the controls") {
        // NO PEAK-INDEX CLAIM IN THIS ARM: at decoherence = 1 there is no
        // impulse left to locate once T010 lands the phase pass. The arm asserts
        // only the reported latency and the warm-up zeros, both of which are
        // control-independent by construction (FR-014).
        for (const std::size_t fftSize : {512u, 1024u, 2048u, 4096u}) {
            for (const float tilt : {-1.0f, 1.0f}) {
                SpectralSmear smear = makePrepared(fftSize);
                smear.setSmearAmount(1.0f);
                smear.setDecoherence(1.0f);
                smear.setSmearTilt(tilt);

                const std::size_t latency = smear.getLatencySamples();
                INFO("fftSize = " << fftSize << " tilt = " << tilt);
                REQUIRE(latency == fftSize);

                const std::size_t numSamples = 6u * fftSize;
                std::vector<float> inL(numSamples, 0.0f);
                std::vector<float> inR(numSamples, 0.0f);
                inL[2u * fftSize] = 1.0f;
                inR[2u * fftSize] = 1.0f;

                std::vector<float> outL;
                std::vector<float> outR;
                renderStereo(smear, inL, inR, outL, outR, {512u});

                REQUIRE(smear.getLatencySamples() == fftSize);
                bool leadingZeros = true;
                for (std::size_t i = 0; i < latency; ++i) {
                    leadingZeros = leadingZeros && (outL[i] == 0.0f) && (outR[i] == 0.0f);
                }
                REQUIRE(leadingZeros);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Self-reported footprint -- SC-008 (FR-017, plan S10)
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_AllocatedBytes", "[spectral_smear]") {
    SECTION("the reported figure is the plan S10 formula at every geometry") {
        const std::size_t sizes[4] = {512u, 1024u, 2048u, 4096u};
        const std::size_t table[4] = {15384u, 30744u, 61464u, 122904u};

        for (std::size_t i = 0; i < 4u; ++i) {
            SpectralSmear smear = makePrepared(sizes[i]);
            INFO("fftSize = " << sizes[i]);
            REQUIRE(smear.getAllocatedBytes() == expectedAllocatedBytes(smear));
            REQUIRE(smear.getAllocatedBytes() == table[i]);
        }
    }

    SECTION("the largest geometry stays inside the 128 KiB budget") {
        SpectralSmear smear = makePrepared(4096u);
        REQUIRE(smear.getAllocatedBytes() <= 128u * 1024u);

        // REPORTED, NEVER ASSERTED: the sub-object heap is STFT / OverlapAdd /
        // SpectralBuffer / FFT policy, not this component's (FR-017).
        const std::size_t fftSize = smear.getFftSize();
        const std::size_t numBins = smear.getNumBins();
        const std::size_t subObjectBytes =
            2u * (fftSize * 8u) * sizeof(float)      // STFT::inputBuffer_,          stft.h:78
            + 2u * (2u * fftSize) * sizeof(float)    // windowedFrame_ + window_,    stft.h:189-191
            + 2u * (2u * fftSize) * sizeof(float)    // OverlapAdd::outputBuffer_,   stft.h:265
            + 2u * (2u * fftSize) * sizeof(float)    // ifftBuffer_ + synthWindow_,  stft.h:372-374
            + 2u * (4u * numBins) * sizeof(float);   // SpectralBuffer,   spectral_buffer.h:61-66
        WARN("informational: sub-object heap at fftSize 4096 is about "
             << subObjectBytes << " bytes");
    }

    SECTION("a disabled instance owns no heap at any geometry") {
        for (const std::size_t fftSize : {512u, 1024u, 2048u, 4096u}) {
            SpectralSmear smear = makePrepared(fftSize, false);
            INFO("fftSize = " << fftSize);
            REQUIRE(smear.getAllocatedBytes() == 0u);
        }
    }
}

// ---------------------------------------------------------------------------
// The control surface -- FR-009's substitution rule, FR-050/FR-051 (plan S8)
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_ControlClamps", "[spectral_smear]") {
    SECTION("defaults hold after construction and after prepare()") {
        SpectralSmear fresh;
        REQUIRE(fresh.getSmearAmount() == 0.0f);
        REQUIRE(fresh.getDecoherence() == 0.0f);
        REQUIRE(fresh.getSmearTilt() == 0.0f);
        REQUIRE(fresh.getSmearTimeLow() == 3.0f);
        REQUIRE(fresh.getSmearTimeHigh() == 0.25f);

        SpectralSmear prepared = makePrepared(kRefFft);
        REQUIRE(prepared.getSmearAmount() == 0.0f);
        REQUIRE(prepared.getDecoherence() == 0.0f);
        REQUIRE(prepared.getSmearTilt() == 0.0f);
        REQUIRE(prepared.getSmearTimeLow() == 3.0f);
        REQUIRE(prepared.getSmearTimeHigh() == 0.25f);
    }

    SECTION("every setter clamps at both range ends") {
        SpectralSmear smear = makePrepared(kRefFft);

        smear.setSmearAmount(-1.0f);
        REQUIRE(smear.getSmearAmount() == 0.0f);
        smear.setSmearAmount(2.0f);
        REQUIRE(smear.getSmearAmount() == 1.0f);

        smear.setDecoherence(-1.0f);
        REQUIRE(smear.getDecoherence() == 0.0f);
        smear.setDecoherence(2.0f);
        REQUIRE(smear.getDecoherence() == 1.0f);

        smear.setSmearTilt(-5.0f);
        REQUIRE(smear.getSmearTilt() == -1.0f);
        smear.setSmearTilt(5.0f);
        REQUIRE(smear.getSmearTilt() == 1.0f);

        smear.setSmearTimeLow(0.0f);
        REQUIRE(smear.getSmearTimeLow() == 0.02f);
        smear.setSmearTimeLow(100.0f);
        REQUIRE(smear.getSmearTimeLow() == 10.0f);

        smear.setSmearTimeHigh(0.0f);
        REQUIRE(smear.getSmearTimeHigh() == 0.02f);
        smear.setSmearTimeHigh(100.0f);
        REQUIRE(smear.getSmearTimeHigh() == 10.0f);
    }

    SECTION("legal finite extremes round-trip unchanged") {
        SpectralSmear smear = makePrepared(kRefFft);

        smear.setSmearAmount(0.0f);
        REQUIRE(smear.getSmearAmount() == 0.0f);
        smear.setSmearAmount(1.0f);
        REQUIRE(smear.getSmearAmount() == 1.0f);
        smear.setDecoherence(0.0f);
        REQUIRE(smear.getDecoherence() == 0.0f);
        smear.setDecoherence(1.0f);
        REQUIRE(smear.getDecoherence() == 1.0f);

        // The smallest positive NORMAL float, and the smallest float above zero:
        // both are legal finite values, neither is a non-finite one.
        const float smallestNormal = std::numeric_limits<float>::min();
        const float justAboveZero  = std::nextafter(0.0f, 1.0f);
        smear.setSmearAmount(smallestNormal);
        REQUIRE(smear.getSmearAmount() == smallestNormal);
        smear.setSmearAmount(justAboveZero);
        REQUIRE(smear.getSmearAmount() == justAboveZero);
        smear.setDecoherence(smallestNormal);
        REQUIRE(smear.getDecoherence() == smallestNormal);
    }

    SECTION("the two time-constant endpoints are stored verbatim, never swapped") {
        SpectralSmear smear = makePrepared(kRefFft);

        smear.setSmearTimeLow(1.0f);
        smear.setSmearTimeHigh(1.0f);
        REQUIRE(smear.getSmearTimeLow() == 1.0f);
        REQUIRE(smear.getSmearTimeHigh() == 1.0f);

        // tauLow < tauHigh inverts the law (legal, FR-031 Edge Cases, never
        // silently swapped); the behavioural consequence is T009's
        // SpectralSmear_TimeConstantLaw.
        smear.setSmearTimeLow(0.25f);
        smear.setSmearTimeHigh(3.0f);
        REQUIRE(smear.getSmearTimeLow() == 0.25f);
        REQUIRE(smear.getSmearTimeHigh() == 3.0f);
    }

    SECTION("every setter stores, clamps and stays inert BEFORE prepare()") {
        SpectralSmear smear;  // never prepared: no table may be touched
        smear.setSmearAmount(2.0f);
        smear.setDecoherence(-1.0f);
        smear.setSmearTilt(-5.0f);
        smear.setSmearTimeLow(100.0f);   // marks the tables dirty while they are zero-sized
        smear.setSmearTimeHigh(0.0f);

        REQUIRE(smear.getSmearAmount() == 1.0f);
        REQUIRE(smear.getDecoherence() == 0.0f);
        REQUIRE(smear.getSmearTilt() == -1.0f);
        REQUIRE(smear.getSmearTimeLow() == 10.0f);
        REQUIRE(smear.getSmearTimeHigh() == 0.02f);
        REQUIRE(smear.isPrepared() == false);
    }

    SECTION("setSeed(0) keeps both derived streams live and distinct") {
        SpectralSmear smear = makePrepared(kRefFft);
        smear.setSeed(0u);
        REQUIRE(smear.getSeed() == 0u);

        // random.h:102-113 guarantees a non-zero derived seed, which is what
        // stops Xorshift32::seed()'s silent 0 substitution from collapsing the
        // two channels onto one stream. The render-level consequence is SC-001
        // arm (c) in T011.
        const std::uint32_t left =
            Krate::DSP::deriveStreamSeed(0u, SpectralSmear::kSaltDecohereL);
        const std::uint32_t right =
            Krate::DSP::deriveStreamSeed(0u, SpectralSmear::kSaltDecohereR);
        REQUIRE(left != 0u);
        REQUIRE(right != 0u);
        REQUIRE(left != right);
    }
}

// ---------------------------------------------------------------------------
// The time-constant law: pole bounds and the coherence make-up interpolant
// (FR-023, FR-030 - FR-034, FR-042; plan S4.1 - S4.3, S7.2, S12)
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_PoleTableBounds", "[spectral_smear]") {
    // poleForTau is a PURE STATIC, which is the whole point: it lets this
    // white-box case drive the law to synthetic extremes no render can reach
    // (spec correction C-6 (i) - FR-023 named a private `poleTable()`).

    SECTION("every pole sits in [0, kMaxPole] across the reachable grid") {
        const float       taus[7]  = {0.02f, 0.25f, 1.0f, 3.0f, 10.0f, 1.0e6f, 1.0e30f};
        const std::size_t hops[3]  = {128u, 512u, 1024u};
        const double      rates[4] = {44100.0, 48000.0, 96000.0, 192000.0};

        for (const float tau : taus) {
            for (const std::size_t hop : hops) {
                for (const double rate : rates) {
                    const float pole = SpectralSmear::poleForTau(tau, hop, rate);
                    INFO("tau = " << tau << " hop = " << hop << " rate = " << rate
                                  << " pole = " << pole);
                    REQUIRE(pole >= 0.0f);
                    REQUIRE(pole <= SpectralSmear::kMaxPole);
                }
            }
        }
    }

    SECTION("degenerate products return exactly 0 through the ordered guard") {
        // `!(denom > 0.0f)` is an ORDERED compare, so a zero tau and a zero rate
        // both fall through to the documented neutral.
        REQUIRE(SpectralSmear::poleForTau(0.0f, 512u, 48000.0) == 0.0f);
        REQUIRE(SpectralSmear::poleForTau(1.0f, 512u, 0.0) == 0.0f);
        REQUIRE(SpectralSmear::poleForTau(0.0f, 512u, 0.0) == 0.0f);
    }

    SECTION("the two reachable extremes sit three orders below kMaxPole") {
        // Plan S12: kMaxSmearSeconds, NOT kMaxPole, is the binding bound. The
        // longest shipped time constant at the coarsest frame rate still lands
        // well short of the defensive backstop, so FR-023's clamp is genuinely
        // unreachable in a shipped configuration.
        const float slowest = SpectralSmear::poleForTau(10.0f, 1024u, 44100.0);
        const float fastest = SpectralSmear::poleForTau(10.0f, 1024u, 192000.0);
        INFO("slowest = " << slowest << " fastest = " << fastest);
        REQUIRE(std::abs(slowest - 0.99768f) <= 1.0e-4f);
        REQUIRE(std::abs(fastest - 0.99947f) <= 1.0e-4f);
        REQUIRE(slowest < SpectralSmear::kMaxPole);
        REQUIRE(fastest < SpectralSmear::kMaxPole);
    }

    SECTION("the only shipped configuration with hop/(rate*tau) > 1 stays convex") {
        // exp(-1024 / (44100 * 0.02)) = exp(-1.1610) = 0.31316. The integrator
        // must remain a convex combination even here - a pole outside (0, 1)
        // would make it an amplifier rather than a leak.
        const float pole = SpectralSmear::poleForTau(0.02f, 1024u, 44100.0);
        INFO("pole = " << pole);
        REQUIRE(std::abs(pole - 0.31316f) <= 1.0e-4f);
        REQUIRE(pole > 0.0f);
        REQUIRE(pole < 1.0f);
    }

    SECTION("strictly increasing in tau") {
        const float       taus[10]  = {0.02f, 0.05f, 0.1f, 0.25f, 0.5f,
                                       1.0f,  2.0f,  3.0f, 5.0f,  10.0f};
        const std::size_t hops[3]   = {128u, 512u, 1024u};
        const double      rates[4]  = {44100.0, 48000.0, 96000.0, 192000.0};

        for (const std::size_t hop : hops) {
            for (const double rate : rates) {
                for (std::size_t i = 1; i < 10u; ++i) {
                    const float lower = SpectralSmear::poleForTau(taus[i - 1u], hop, rate);
                    const float upper = SpectralSmear::poleForTau(taus[i], hop, rate);
                    INFO("hop = " << hop << " rate = " << rate << " tau " << taus[i - 1u]
                                  << " -> " << taus[i] << " : " << lower << " -> " << upper);
                    REQUIRE(upper > lower);
                }
            }
        }
    }

    SECTION("strictly decreasing in hopSize") {
        const float       taus[5]  = {0.02f, 0.25f, 1.0f, 3.0f, 10.0f};
        const std::size_t hops[3]  = {128u, 512u, 1024u};
        const double      rates[4] = {44100.0, 48000.0, 96000.0, 192000.0};

        for (const float tau : taus) {
            for (const double rate : rates) {
                for (std::size_t i = 1; i < 3u; ++i) {
                    const float coarser = SpectralSmear::poleForTau(tau, hops[i], rate);
                    const float finer   = SpectralSmear::poleForTau(tau, hops[i - 1u], rate);
                    INFO("tau = " << tau << " rate = " << rate << " hop " << hops[i - 1u]
                                  << " -> " << hops[i] << " : " << finer << " -> " << coarser);
                    REQUIRE(coarser < finer);
                }
            }
        }
    }
}

TEST_CASE("SpectralSmear_CoherenceMakeupInterpolant", "[spectral_smear]") {
    // The PURE-FUNCTION arm of FR-042. The measured arm - that dividing this
    // gain back out restores the RMS of a decohered render - is SC-006 in T011.

    SECTION("the knot table is the shipped AetherReverb table, verbatim") {
        // Exact equality is legitimate here: these are the literals transcribed
        // from effects/aether_reverb.h:2775, not a computation, so
        // lint-float-bit-goldens.js has nothing to object to.
        REQUIRE(SpectralSmear::kCoherenceKnotCount == 5u);
        REQUIRE(SpectralSmear::kCoherenceMakeup[0] == 1.0000f);
        REQUIRE(SpectralSmear::kCoherenceMakeup[1] == 1.0799f);
        REQUIRE(SpectralSmear::kCoherenceMakeup[2] == 1.3435f);
        REQUIRE(SpectralSmear::kCoherenceMakeup[3] == 1.7746f);
        REQUIRE(SpectralSmear::kCoherenceMakeup[4] == 1.9996f);
    }

    SECTION("unity at zero, exactly, and each knot is reproduced") {
        // cubicHermiteInterpolate returns c0 == y0 at t == 0
        // (core/interpolation.h:92, :98), so the identity is exact rather than
        // merely close - which is what lets FR-041's identity gate be a
        // bit-transparent bypass instead of a near-bypass.
        REQUIRE(SpectralSmear::coherenceMakeup(0.0f) == 1.0f);

        for (std::size_t i = 0; i < SpectralSmear::kCoherenceKnotCount; ++i) {
            const float d     = static_cast<float>(i) / 4.0f;
            const float gain  = SpectralSmear::coherenceMakeup(d);
            INFO("knot " << i << " at d = " << d << " gave " << gain);
            REQUIRE(std::abs(gain - SpectralSmear::kCoherenceMakeup[i]) <= 1.0e-5f);
        }
    }

    SECTION("non-decreasing and bounded over the whole control range") {
        float previous = SpectralSmear::coherenceMakeup(0.0f);
        for (std::size_t i = 0; i <= 100u; ++i) {
            const float d    = static_cast<float>(i) / 100.0f;
            const float gain = SpectralSmear::coherenceMakeup(d);
            INFO("d = " << d << " gain = " << gain << " previous = " << previous);
            REQUIRE(gain >= previous - 1.0e-6f);
            REQUIRE(gain >= 1.0f);
            REQUIRE(gain <= 2.05f);
            previous = gain;
        }
    }

    SECTION("out-of-range amounts clamp onto the endpoints") {
        REQUIRE(SpectralSmear::coherenceMakeup(-1.0f) == SpectralSmear::coherenceMakeup(0.0f));
        REQUIRE(SpectralSmear::coherenceMakeup(2.0f) == SpectralSmear::coherenceMakeup(1.0f));
    }
}

// ---------------------------------------------------------------------------
// The render path -- SC-003, SC-011, SC-017, SC-007, FR-003/FR-012 boundaries
// and FR-061's clamp (T006)
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_NullAtZero", "[spectral_smear]") {
    // SC-003. Hann at 75 % overlap WITH the synthesis window is COLA
    // (primitives/stft.h:224-227), so the analysis-synthesis round trip is
    // analytically exact at smearAmount == 0 / decoherence == 0 and the
    // tolerances below cover FFT-pair round-off only.
    //
    // TILT MUST BE INERT WHEN THE AMOUNT IS ZERO: it only reshapes the pole
    // table, and FR-021's identity gate writes no magnitude at all, so all
    // three tilt settings must null identically.
    const std::size_t numSamples = static_cast<std::size_t>(20.0 * kFs48);  // 20 s
    const std::size_t discard    = 2u * kRefFft;  // warm-up zeros + COLA ramp-up (plan S11)
    const float       inputRms   = 0.2512f;       // -12 dBFS

    std::vector<float> noiseL(numSamples, 0.0f);
    std::vector<float> noiseR(numSamples, 0.0f);
    fillNoiseAtRms(noiseL, 0x51EA5E01u, inputRms);
    fillNoiseAtRms(noiseR, 0x51EA5E02u, inputRms);

    std::vector<float> toneL(numSamples, 0.0f);
    std::vector<float> toneR(numSamples, 0.0f);
    fillPartialTone(toneL, kFs48, 110.0, 0.5f);
    fillPartialTone(toneR, kFs48, 164.0, 0.5f);

    const auto nullCheck = [&](const std::vector<float>& inL, const std::vector<float>& inR,
                               float tilt) {
        SpectralSmear smear = makePrepared(kRefFft);
        smear.setSmearAmount(0.0f);
        smear.setDecoherence(0.0f);
        smear.setSmearTilt(tilt);

        const std::size_t latency = smear.getLatencySamples();
        REQUIRE(latency == kRefFft);

        std::vector<float> outL;
        std::vector<float> outR;
        renderStereo(smear, inL, inR, outL, outR, {512u});

        const std::size_t  count = numSamples - discard;
        std::vector<float> residualL(count, 0.0f);
        std::vector<float> residualR(count, 0.0f);
        float              peakResidual = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t n = discard + i;
            residualL[i]        = outL[n] - inL[n - latency];
            residualR[i]        = outR[n] - inR[n - latency];
            peakResidual        = std::max(peakResidual, std::abs(residualL[i]));
            peakResidual        = std::max(peakResidual, std::abs(residualR[i]));
        }

        const double referenceRms = 0.5 * (rmsOf(inL.data() + discard - latency, count) +
                                           rmsOf(inR.data() + discard - latency, count));
        const double residualRms =
            0.5 * (rmsOf(residualL.data(), count) + rmsOf(residualR.data(), count));
        const double relative = residualRms / std::max(referenceRms, 1.0e-12);

        INFO("tilt = " << tilt << " peak residual = " << peakResidual
                       << " residual RMS ratio = " << relative);
        REQUIRE(peakResidual <= 1.0e-4f);
        REQUIRE(relative <= 1.0e-4);  // -80 dBFS relative to the input RMS
    };

    SECTION("white noise nulls at every tilt") {
        for (const float tilt : {-1.0f, 0.0f, 1.0f}) {
            nullCheck(noiseL, noiseR, tilt);
        }
    }

    SECTION("a five-partial tone nulls at every tilt") {
        for (const float tilt : {-1.0f, 0.0f, 1.0f}) {
            nullCheck(toneL, toneR, tilt);
        }
    }
}

TEST_CASE("SpectralSmear_PartitionInvariance", "[spectral_smear]") {
    // SC-011, the direct assertion of FR-014's warm-up COUNTER. A "drain when
    // available" implementation fails this with a partition-dependent offset of
    // (ceil(fftSize/chunk) - 1) * chunk -- 960 / 1020 / 1022 measured at chunk
    // 64 / 30 / 7 in effects/aether_reverb.h:643-660 -- so the comparison is
    // SAMPLE-ALIGNED WITH NO OFFSET CORRECTION on purpose.
    const std::size_t numSamples = static_cast<std::size_t>(60.0 * kFs48);  // 60 s

    std::vector<float> inL(numSamples, 0.0f);
    std::vector<float> inR(numSamples, 0.0f);
    fillNoiseAtRms(inL, 0x9A17F00Du, 0.2f);
    fillNoiseAtRms(inR, 0x9A17F00Eu, 0.2f);
    {
        // Pink-ish: a one-pole tilt on the white noise, plus a tone so the
        // fingerprint's total-variation metric has a deterministic component.
        float stateL = 0.0f;
        float stateR = 0.0f;
        for (std::size_t i = 0; i < numSamples; ++i) {
            stateL = 0.96f * stateL + 0.04f * inL[i];
            stateR = 0.96f * stateR + 0.04f * inR[i];
            const double t    = static_cast<double>(i) / kFs48;
            const auto   tone = static_cast<float>(0.12 * std::sin(2.0 * kTestPi * 220.0 * t));
            inL[i]            = 6.0f * stateL + tone;
            inR[i]            = 6.0f * stateR + tone;
        }
    }

    const auto render = [&](const std::vector<std::size_t>& partition, std::vector<float>& outL,
                            std::vector<float>& outR) {
        SpectralSmear smear = makePrepared(kRefFft);
        smear.setSeed(0x0C0FFEE1u);
        smear.setSmearAmount(0.5f);
        smear.setDecoherence(0.0f);
        smear.setSmearTilt(0.0f);
        renderStereo(smear, inL, inR, outL, outR, partition);
    };

    std::vector<float> uniformL;
    std::vector<float> uniformR;
    render({512u}, uniformL, uniformR);

    std::vector<float> raggedL;
    std::vector<float> raggedR;
    render(makeRandomSchedule(0xBADC0DE5u, 512u), raggedL, raggedR);

    const auto referenceL = Krate::DSP::TestUtils::fingerprintRender(
        std::span<const float>(uniformL.data(), uniformL.size()));
    const auto referenceR = Krate::DSP::TestUtils::fingerprintRender(
        std::span<const float>(uniformR.data(), uniformR.size()));
    const auto actualL = Krate::DSP::TestUtils::fingerprintRender(
        std::span<const float>(raggedL.data(), raggedL.size()));
    const auto actualR = Krate::DSP::TestUtils::fingerprintRender(
        std::span<const float>(raggedR.data(), raggedR.size()));

    const auto comparisonL = Krate::DSP::TestUtils::compareFingerprints(actualL, referenceL);
    const auto comparisonR = Krate::DSP::TestUtils::compareFingerprints(actualR, referenceR);

    INFO("L: " << comparisonL.detail << " worst metric " << comparisonL.worstMetricRelativeError
               << " worst sample " << comparisonL.worstSampleError);
    REQUIRE(comparisonL.withinTolerance());
    INFO("R: " << comparisonR.detail << " worst metric " << comparisonR.worstMetricRelativeError
               << " worst sample " << comparisonR.worstSampleError);
    REQUIRE(comparisonR.withinTolerance());
}

TEST_CASE("SpectralSmear_ControlCadence", "[spectral_smear]") {
    // SC-017, the criterion that closes FR-013 (a)'s declared silent-failure
    // mode: the three smoothers advance ONCE PER HOP, outside the channel loop,
    // and both channels share one control value. A double advance halves the
    // effective smoothing time; a per-block advance decouples the trajectory
    // from hopSize. Neither is visible to any other criterion.
    //
    // SPEC CORRECTION C-12: the setter is issued BEFORE the first processBlock
    // call, so `f` counts frames from the same origin as the trajectory
    // 1 - coeff^f. The mid-render generalisation is
    //   f = max(0, floor((samplesProcessed - callIndex - fftSize) / hopSize) + 1).
    // The comparison is <= 1e-4, not <: OnePoleSmoother::process() snaps once
    // |current_ - target_| < kCompletionThreshold = 1e-4f
    // (primitives/smoother.h:199-201, :53), so at the frame where the
    // trajectory crosses that threshold predicted and actual differ by exactly
    // up to 1e-4 and a strict < is a coin flip on that frame.
    //
    // f is NOT floor(samplesProcessed / hopSize), which over-counts by
    // fftSize/hopSize - 1 = 3 frames at every 75 % geometry: the smoothers
    // advance only inside `while (stft_[0].canAnalyze())` and canAnalyze() is
    // samplesAvailable_ >= fftSize_ (primitives/stft.h:137).
    const std::vector<std::size_t> uniform = {512u};
    const std::vector<std::size_t> ragged  = makeRandomSchedule(0x51DE0A7Cu, 128u);

    for (const std::size_t fftSize : {2048u, 512u}) {
        for (std::size_t partitionIndex = 0; partitionIndex < 2u; ++partitionIndex) {
            const std::vector<std::size_t>& partition = (partitionIndex == 0u) ? uniform : ragged;

            SpectralSmear     smear     = makePrepared(fftSize);
            const std::size_t hop       = smear.getHopSize();
            const float       frameRate = static_cast<float>(kFs48) / static_cast<float>(hop);
            const float       coeff     = Krate::DSP::calculateOnePolCoefficient(
                SpectralSmear::kControlSmoothMs, frameRate);

            smear.setSmearAmount(1.0f);  // BEFORE the first processBlock (C-12)
            REQUIRE(smear.getAppliedSmearAmount() == 0.0f);

            std::vector<float> bufferL(4096u, 0.25f);
            std::vector<float> bufferR(4096u, 0.25f);

            std::size_t samplesProcessed = 0;
            double      worstError       = 0.0;
            std::size_t worstBlock       = 0;
            for (std::size_t block = 0; block < 64u; ++block) {
                const std::size_t n = partition[block % partition.size()];
                smear.processBlock(bufferL.data(), bufferR.data(), n);
                samplesProcessed += n;

                const auto elapsed = static_cast<std::ptrdiff_t>(samplesProcessed) -
                                     static_cast<std::ptrdiff_t>(fftSize);
                const std::ptrdiff_t frames =
                    (elapsed < 0) ? 0 : (elapsed / static_cast<std::ptrdiff_t>(hop) + 1);
                const double expected =
                    1.0 - std::pow(static_cast<double>(coeff), static_cast<double>(frames));
                const double actual = static_cast<double>(smear.getAppliedSmearAmount());
                const double error  = std::abs(actual - expected);
                if (error > worstError) {
                    worstError = error;
                    worstBlock = block;
                }
            }

            INFO("fftSize = " << fftSize << " partition = " << partitionIndex
                              << " coeff = " << coeff << " worst error " << worstError
                              << " at block " << worstBlock);
            REQUIRE(worstError <= 1.0e-4);
        }
    }
}

TEST_CASE("SpectralSmear_NoAllocation", "[spectral_smear]") {
    // SC-007. Every Catch2 assertion macro allocates, so the loop runs with NO
    // macro inside the scope and the latched count is asserted afterwards.
    // setSmearTimeLow / setSmearTimeHigh are included deliberately: they mark
    // the pole tables dirty, and the deferred rebuild at the top of the next
    // processBlock overwrites three ALREADY-SIZED tables rather than resizing
    // them (FR-064).
    SpectralSmear smear = makePrepared(kRefFft);

    std::vector<float> bufferL(2048u, 0.0f);
    std::vector<float> bufferR(2048u, 0.0f);
    fillNoiseAtRms(bufferL, 0x7A11A10Cu, 0.2f);
    fillNoiseAtRms(bufferR, 0x7A11A10Du, 0.2f);

    const std::size_t blockSizes[8] = {1u, 7u, 30u, 64u, 65u, 511u, 512u, 2048u};

    std::size_t observed = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        for (std::size_t i = 0; i < 5000u; ++i) {
            const float phase = static_cast<float>(i % 32u) / 32.0f;
            smear.setSmearAmount(phase);
            smear.setDecoherence(1.0f - phase);
            smear.setSmearTilt(2.0f * phase - 1.0f);
            smear.setSmearTimeLow(0.02f + phase * 9.0f);
            smear.setSmearTimeHigh(0.02f + (1.0f - phase) * 9.0f);
            smear.setSeed(static_cast<std::uint32_t>(i) + 1u);
            smear.processBlock(bufferL.data(), bufferR.data(), blockSizes[i % 8u]);
        }
        // AllocationScope latches in its DESTRUCTOR (allocation_detector.h:117-119),
        // so the live figure is read from the detector while the scope is open.
        observed = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    INFO("allocations observed during 5 000 processBlock calls: " << observed);
    REQUIRE(observed == 0u);
}

TEST_CASE("SpectralSmear_RenderPathBoundaries", "[spectral_smear]") {
    SECTION("every block size renders finite output and allocates nothing new") {
        SpectralSmear     smear = makePrepared(kRefFft);
        const std::size_t bytes = smear.getAllocatedBytes();

        const std::size_t blockSizes[7] = {0u, 1u, 7u, 63u, 64u, 65u, 16384u};
        for (const std::size_t n : blockSizes) {
            std::vector<float> bufferL(std::max<std::size_t>(n, 1u), 0.0f);
            std::vector<float> bufferR(std::max<std::size_t>(n, 1u), 0.0f);
            fillNoiseAtRms(bufferL, 0x0B10C451u, 0.25f);
            fillNoiseAtRms(bufferR, 0x0B10C452u, 0.25f);

            smear.processBlock(bufferL.data(), bufferR.data(), n);

            // detail::isFinite reads the bit pattern through opaqueFloatBits
            // (core/db_utils.h:118) - std::isfinite would fold away under
            // -ffast-math and is banned by tools/lint-nonfinite-symbols.js.
            bool finite = true;
            for (std::size_t i = 0; i < n; ++i) {
                finite = finite && Krate::DSP::detail::isFinite(bufferL[i]) &&
                         Krate::DSP::detail::isFinite(bufferR[i]);
            }
            INFO("numSamples = " << n);
            REQUIRE(finite);
            REQUIRE(smear.getAllocatedBytes() == bytes);
        }
    }

    SECTION("the entry guards leave the buffers untouched") {
        const std::size_t  numSamples = 512u;
        std::vector<float> sentinelL(numSamples, 0.0f);
        std::vector<float> sentinelR(numSamples, 0.0f);
        for (std::size_t i = 0; i < numSamples; ++i) {
            sentinelL[i] = 0.125f + static_cast<float>(i);
            sentinelR[i] = -0.125f - static_cast<float>(i);
        }

        const auto survives = [&](const std::vector<float>& bufferL,
                                  const std::vector<float>& bufferR) {
            bool intact = true;
            for (std::size_t i = 0; i < numSamples; ++i) {
                intact = intact && (bufferL[i] == 0.125f + static_cast<float>(i)) &&
                         (bufferR[i] == -0.125f - static_cast<float>(i));
            }
            return intact;
        };

        // Unprepared.
        {
            SpectralSmear      unprepared;
            std::vector<float> bufferL = sentinelL;
            std::vector<float> bufferR = sentinelR;
            unprepared.processBlock(bufferL.data(), bufferR.data(), numSamples);
            REQUIRE(survives(bufferL, bufferR));
        }

        // Null left, then null right, on a prepared ENABLED instance.
        {
            SpectralSmear      smear   = makePrepared(kRefFft);
            std::vector<float> bufferL = sentinelL;
            std::vector<float> bufferR = sentinelR;
            smear.processBlock(nullptr, bufferR.data(), numSamples);
            smear.processBlock(bufferL.data(), nullptr, numSamples);
            REQUIRE(survives(bufferL, bufferR));
        }
    }

    SECTION("a disabled instance re-prepared enabled renders like a fresh one") {
        SpectralSmear smear;
        smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = false});
        REQUIRE(smear.getLatencySamples() == 0u);
        REQUIRE(smear.getAllocatedBytes() == 0u);

        smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
        REQUIRE(smear.getLatencySamples() == kRefFft);  // SC-002 (a)
        REQUIRE(smear.getAllocatedBytes() == 61464u);   // SC-008's reference figure
        REQUIRE(smear.getAllocatedBytes() == expectedAllocatedBytes(smear));

        const std::size_t  numSamples = 6u * kRefFft;
        std::vector<float> inL(numSamples, 0.0f);
        std::vector<float> inR(numSamples, 0.0f);
        inL[2u * kRefFft] = 1.0f;
        inR[2u * kRefFft] = 1.0f;

        std::vector<float> outL;
        std::vector<float> outR;
        renderStereo(smear, inL, inR, outL, outR, {512u});

        REQUIRE(argmaxAbs(outL) == 3u * kRefFft);
        REQUIRE(argmaxAbs(outR) == 3u * kRefFft);
    }
}

TEST_CASE("SpectralSmear_OutputClamp", "[spectral_smear]") {
    // THE ONLY CRITERION THAT CAN DETECT FR-061 FAILING (spec correction
    // C-6 (ii)): SC-005's arms (ii) and (iii) are BOTH satisfied by a build
    // with no clamp and no counter -- (ii) because the peak is near 1.0 anyway
    // at realistic drive, and (iii) because it asserts the path is NOT taken.
    SpectralSmear smear = makePrepared(kRefFft);

    const std::size_t  numSamples = static_cast<std::size_t>(kFs48);  // 1 s
    std::vector<float> inL(numSamples, 0.0f);
    std::vector<float> inR(numSamples, 0.0f);
    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / kFs48;
        // Amplitude >= 6.0, comfortably past kOutputClamp = 4.0f. Once T010
        // lands the phase pass, an amplitude >= 3.0 with decoherence = 1 also
        // reaches it through the ~2x make-up.
        const auto value = static_cast<float>(6.5 * std::sin(2.0 * kTestPi * 200.0 * t));
        inL[i]           = value;
        inR[i]           = value;
    }

    std::vector<float> outL;
    std::vector<float> outR;
    renderStereo(smear, inL, inR, outL, outR, {512u});

    float peak = 0.0f;
    for (std::size_t i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(outL[i]));
        peak = std::max(peak, std::abs(outR[i]));
    }
    INFO("peak output = " << peak << " clamp engagements = " << smear.getClampEngagements());
    REQUIRE(peak <= SpectralSmear::kOutputClamp);
    REQUIRE(smear.getClampEngagements() > 0u);

    smear.reset();
    REQUIRE(smear.getClampEngagements() == 0u);  // FR-054

    // Re-engage, then prove a fresh prepare() also zeroes the counter.
    std::vector<float> outAgainL;
    std::vector<float> outAgainR;
    renderStereo(smear, inL, inR, outAgainL, outAgainR, {512u});
    REQUIRE(smear.getClampEngagements() > 0u);

    smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
    REQUIRE(smear.getClampEngagements() == 0u);
}

// ---------------------------------------------------------------------------
// The magnitude pass: priming (FR-022, SC-018 arm (a); plan S6, S13.2)
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_MagnitudePriming", "[spectral_smear]") {
    // SC-018 ARM (a). Arm (b) - the poison-then-reprime arm - lives in
    // spectral_smear_nonfinite_test.cpp (spec correction C-17), because it has
    // to inject a non-finite sample and THIS TU may not name one.
    //
    // THE SPEC'S ORIGINAL PER-BIN |delta| <= 1e-6 BIN-DOMAIN FORM IS NOT
    // EXECUTABLE (plan S13.2, spec correction C-5): FR-014 makes frame 0
    // unobservable after exactly fftSize pushed samples, and the only
    // frame-0-exclusive output is ONE w^2-tapered, COLA-incomplete window that
    // cannot be re-analysed onto the bin grid at 1e-6. The executed form is
    // sample-domain and compares frame 0's exclusive output against the SAME
    // input through the SAME component at smearAmount == 0 - FR-021's
    // exact-identity path, which leaves the analysed spectrum untouched and so
    // reproduces the bare round trip INCLUDING the identical ramp-up taper. The
    // taper cancels, which is the whole point.
    //
    // WHY THIS DISCRIMINATES. A PRIMED memory writes state[k] = mag[k] on
    // frame 0, so at amount == 1 the written magnitude IS the analysed
    // magnitude and the residual is FFT round-off, tens of dB under the gate. A
    // ZERO-INITIALISED memory writes (1 - p) * mag[k] instead - p is ~0.99
    // across the band at the shipped endpoints - leaving a residual of ~99 % of
    // the reference, about -0.1 dB. The margin is more than 30 dB, so the arm
    // can neither pass by accident nor fail on round-off.
    const std::size_t numSamples = kRefFft + kRefHop;  // exactly frame 0's span

    std::vector<float> inL(numSamples, 0.0f);
    std::vector<float> inR(numSamples, 0.0f);
    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t     = static_cast<double>(i) / kFs48;
        const auto   value = static_cast<float>(0.7 * std::sin(2.0 * kTestPi * 1000.0 * t));
        inL[i]             = value;
        inR[i]             = value;
    }

    // The controls are set BEFORE prepare() so the smoothers snap to them
    // (plan S2 step 9) and frame 0 therefore sees the endpoint value exactly,
    // not a 50 ms ramp's first frame.
    const auto renderAt = [&](float amount, std::vector<float>& outL, std::vector<float>& outR) {
        SpectralSmear smear;
        smear.setSmearAmount(amount);
        smear.setDecoherence(0.0f);
        smear.setSmearTilt(0.0f);
        smear.setSmearTimeLow(3.0f);
        smear.setSmearTimeHigh(0.25f);
        smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
        smear.reset();
        REQUIRE(smear.getLatencySamples() == kRefFft);
        renderStereo(smear, inL, inR, outL, outR, {512u});
    };

    std::vector<float> actualL;
    std::vector<float> actualR;
    renderAt(1.0f, actualL, actualR);

    std::vector<float> referenceL;
    std::vector<float> referenceR;
    renderAt(0.0f, referenceL, referenceR);

    // out[fftSize, fftSize + hopSize) is frame 0's exclusive contribution: the
    // first fftSize output samples are the warm-up counter's zeros (FR-014).
    std::vector<float> residual(kRefHop, 0.0f);
    for (std::size_t i = 0; i < kRefHop; ++i) {
        residual[i] = actualL[kRefFft + i] - referenceL[kRefFft + i];
    }

    const double referenceRms = rmsOf(referenceL.data() + kRefFft, kRefHop);
    const double residualRms  = rmsOf(residual.data(), kRefHop);
    REQUIRE(referenceRms > 1.0e-6);  // the window is not degenerate

    const double relative = residualRms / referenceRms;
    INFO("reference RMS = " << referenceRms << " residual RMS = " << residualRms
                            << " ratio = " << relative);
    REQUIRE(relative <= 1.0e-3);  // -60 dB

    // Both channels are driven identically, so R must reproduce L's verdict.
    double residualRmsR = 0.0;
    {
        std::vector<float> residualR(kRefHop, 0.0f);
        for (std::size_t i = 0; i < kRefHop; ++i) {
            residualR[i] = actualR[kRefFft + i] - referenceR[kRefFft + i];
        }
        residualRmsR = rmsOf(residualR.data(), kRefHop);
    }
    const double referenceRmsR = rmsOf(referenceR.data() + kRefFft, kRefHop);
    REQUIRE(referenceRmsR > 1.0e-6);
    REQUIRE(residualRmsR / referenceRmsR <= 1.0e-3);
}

// ---------------------------------------------------------------------------
// The phase pass: seed determinism and FR-043's unconditional burn
// (SC-009; plan S7.1)
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_SeedDeterminism", "[spectral_smear]") {
    // SC-009, all four arms. The comparison is ALWAYS render_fingerprint.h's
    // tolerance-based compare (:122, kSampleTolerance = 5.0e-4f,
    // kMetricTolerance = 2.5e-4) and NEVER a bit-exact digest: the renders here
    // are float STFT round trips whose sample bits are not reproducible across
    // MSVC / GCC / AppleClang(-ffast-math).
    //
    // THE SCRIPT PARKS AT ITS OWN HEAD VALUES FOR ITS LAST kTailPark FRAMES.
    // That is what makes arm (d) exact rather than approximate: reset() clears
    // the audio state and re-derives both streams but DELIBERATELY does not
    // touch the three smoothers (values AND targets), so the replay can only
    // reproduce the original render if the smoothers are sitting on exactly the
    // values the script's first block sets. OnePoleSmoother::process() snaps to
    // the target once it is within kCompletionThreshold
    // (primitives/smoother.h:55, :197-201), and kTailPark = 300 frames is two
    // orders of magnitude past the ~12 frames the 50 ms frame-clock smoother
    // needs, so "exactly" is literal.
    constexpr std::size_t kBlocks     = 2812;  // 2812 * 512 = 1 439 744 samples = 29.99 s
    constexpr std::size_t kHeadPark   = 64;
    constexpr std::size_t kTailPark   = 300;
    constexpr float       kParkAmount = 0.35f;
    constexpr float       kParkDeco   = 0.55f;

    const std::size_t numSamples = kBlocks * kRefHop;

    std::vector<float> inL(numSamples, 0.0f);
    std::vector<float> inR(numSamples, 0.0f);
    fillNoiseAtRms(inL, 0x5EED1001u, 0.15f);
    fillNoiseAtRms(inR, 0x5EED1002u, 0.15f);
    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t    = static_cast<double>(i) / kFs48;
        const auto   tone = static_cast<float>(0.12 * std::sin(2.0 * kTestPi * 220.0 * t));
        inL[i] += tone;
        inR[i] += tone;
    }

    // Both swept controls move, and the two sweep periods are mutually prime so
    // no frame sees a repeating (amount, decoherence) pair.
    const auto sweptScript = [&](SpectralSmear& smear, std::size_t block) {
        if (block < kHeadPark || block >= kBlocks - kTailPark) {
            smear.setSmearAmount(kParkAmount);
            smear.setDecoherence(kParkDeco);
            return;
        }
        const double b = static_cast<double>(block);
        smear.setSmearAmount(
            static_cast<float>(0.5 + 0.5 * std::sin(2.0 * kTestPi * b / 700.0)));
        smear.setDecoherence(
            static_cast<float>(0.5 + 0.5 * std::sin(2.0 * kTestPi * b / 411.0 + 1.0)));
    };

    const auto runScript = [&](SpectralSmear& smear, std::vector<float>& outL,
                               std::vector<float>& outR) {
        outL = inL;
        outR = inR;
        for (std::size_t block = 0; block < kBlocks; ++block) {
            sweptScript(smear, block);
            smear.processBlock(outL.data() + block * kRefHop, outR.data() + block * kRefHop,
                               kRefHop);
        }
    };

    const auto makeInstance = [&](std::uint32_t seed) {
        SpectralSmear smear;
        smear.setSmearAmount(kParkAmount);
        smear.setDecoherence(kParkDeco);
        smear.setSmearTilt(0.0f);
        smear.setSeed(seed);
        smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
        return smear;
    };

    const auto fingerprintSpan = [](const std::vector<float>& buffer, std::size_t from,
                                    std::size_t count) {
        return Krate::DSP::TestUtils::fingerprintRender(
            std::span<const float>(buffer.data() + from, count));
    };

    // --- (a) same seed, same prepare, same script ---------------------------
    SpectralSmear      firstInstance = makeInstance(0x00ABCDEFu);
    std::vector<float> firstL;
    std::vector<float> firstR;
    runScript(firstInstance, firstL, firstR);

    const auto fpFirstL = fingerprintSpan(firstL, 0u, numSamples);
    const auto fpFirstR = fingerprintSpan(firstR, 0u, numSamples);

    // The render must actually contain a decohered signal, or every arm below
    // is vacuous.
    REQUIRE(fpFirstL.rms > 0.01);
    REQUIRE(fpFirstR.rms > 0.01);

    {
        SpectralSmear      twin = makeInstance(0x00ABCDEFu);
        std::vector<float> twinL;
        std::vector<float> twinR;
        runScript(twin, twinL, twinR);

        const auto sameL = Krate::DSP::TestUtils::compareFingerprints(
            fingerprintSpan(twinL, 0u, numSamples), fpFirstL);
        const auto sameR = Krate::DSP::TestUtils::compareFingerprints(
            fingerprintSpan(twinR, 0u, numSamples), fpFirstR);
        INFO("(a) L: " << sameL.detail << " worst metric " << sameL.worstMetricRelativeError
                       << " worst sample " << sameL.worstSampleError);
        REQUIRE(sameL.withinTolerance());
        INFO("(a) R: " << sameR.detail << " worst metric " << sameR.worstMetricRelativeError
                       << " worst sample " << sameR.worstSampleError);
        REQUIRE(sameR.withinTolerance());
    }

    // --- (b) ANTI-VACUITY: a different seed must NOT match ------------------
    {
        SpectralSmear      other = makeInstance(0x00ABCDEFu ^ 0x5A5A5A5Au);
        std::vector<float> otherL;
        std::vector<float> otherR;
        runScript(other, otherL, otherR);

        const auto diffL = Krate::DSP::TestUtils::compareFingerprints(
            fingerprintSpan(otherL, 0u, numSamples), fpFirstL);
        const auto diffR = Krate::DSP::TestUtils::compareFingerprints(
            fingerprintSpan(otherR, 0u, numSamples), fpFirstR);
        INFO("(b) L worst metric " << diffL.worstMetricRelativeError << " worst sample "
                                   << diffL.worstSampleError);
        REQUIRE_FALSE(diffL.withinTolerance());
        INFO("(b) R worst metric " << diffR.worstMetricRelativeError << " worst sample "
                                   << diffR.worstSampleError);
        REQUIRE_FALSE(diffR.withinTolerance());
    }

    // --- (d) reset() then the same script reproduces (a) --------------------
    {
        firstInstance.reset();
        std::vector<float> replayL;
        std::vector<float> replayR;
        runScript(firstInstance, replayL, replayR);

        const auto replayL2 = Krate::DSP::TestUtils::compareFingerprints(
            fingerprintSpan(replayL, 0u, numSamples), fpFirstL);
        const auto replayR2 = Krate::DSP::TestUtils::compareFingerprints(
            fingerprintSpan(replayR, 0u, numSamples), fpFirstR);
        INFO("(d) L: " << replayL2.detail << " worst metric " << replayL2.worstMetricRelativeError
                       << " worst sample " << replayL2.worstSampleError);
        REQUIRE(replayL2.withinTolerance());
        INFO("(d) R: " << replayR2.detail << " worst metric " << replayR2.worstMetricRelativeError
                       << " worst sample " << replayR2.worstSampleError);
        REQUIRE(replayR2.withinTolerance());
    }

    // --- (c) THE ABSOLUTE-WINDOW ARM: FR-043's burn -------------------------
    // Two scripts that differ ONLY in the length of a mid-render park at
    // decoherence == 0. Both parks start on frame kParkStart, both are a whole
    // number of frames, and both end at or before the absolute sample
    // S = kSettledFrame * hop. The fingerprints are taken over the FIXED
    // ABSOLUTE window [S + 2 * fftSize, end).
    //
    // THE WINDOW IS PINNED ABSOLUTELY ON PURPOSE. FR-043 burns numBins - 2
    // draws per channel per frame whether the identity gate engaged or not, so
    // the stream position is a function of ELAPSED FRAMES, never of dwell time:
    // the two renders agree at any given absolute sample index. Aligning the
    // window to "the moment the park ends" instead would FAIL a correct
    // implementation and PASS one that skipped the draws - exactly inverted.
    constexpr std::size_t kParkBlocks   = 1875;  // 1875 * 512 = 960 000 samples = 20 s
    constexpr std::size_t kParkStart    = 200;   // frames
    constexpr std::size_t kParkShort    = 100;   // frames
    constexpr std::size_t kParkLong     = 240;   // frames
    constexpr std::size_t kSettledFrame = 600;   // S / hop; both parks end well before it
    constexpr float       kParkBaseDeco = 0.60f;

    static_assert(kParkStart + kParkLong <= kSettledFrame,
                  "both parks must end at or before the absolute sample S (SC-009 (c))");

    const std::size_t parkSamples = kParkBlocks * kRefHop;
    REQUIRE(parkSamples <= numSamples);

    const auto renderPark = [&](std::size_t parkFrames, std::vector<float>& outL,
                                std::vector<float>& outR) {
        SpectralSmear smear;
        smear.setSmearAmount(0.45f);  // held for the whole render in BOTH scripts
        smear.setDecoherence(kParkBaseDeco);
        smear.setSmearTilt(0.0f);
        smear.setSeed(0x0B04E043u);
        smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});

        outL.assign(inL.begin(), inL.begin() + static_cast<std::ptrdiff_t>(parkSamples));
        outR.assign(inR.begin(), inR.begin() + static_cast<std::ptrdiff_t>(parkSamples));
        for (std::size_t block = 0; block < kParkBlocks; ++block) {
            const bool parked = (block >= kParkStart) && (block < kParkStart + parkFrames);
            smear.setDecoherence(parked ? 0.0f : kParkBaseDeco);
            smear.processBlock(outL.data() + block * kRefHop, outR.data() + block * kRefHop,
                               kRefHop);
        }
    };

    std::vector<float> shortParkL;
    std::vector<float> shortParkR;
    renderPark(kParkShort, shortParkL, shortParkR);

    std::vector<float> longParkL;
    std::vector<float> longParkR;
    renderPark(kParkLong, longParkL, longParkR);

    const std::size_t windowStart = kSettledFrame * kRefHop + 2u * kRefFft;
    const std::size_t windowCount = parkSamples - windowStart;

    const auto settledL = Krate::DSP::TestUtils::compareFingerprints(
        fingerprintSpan(longParkL, windowStart, windowCount),
        fingerprintSpan(shortParkL, windowStart, windowCount));
    const auto settledR = Krate::DSP::TestUtils::compareFingerprints(
        fingerprintSpan(longParkR, windowStart, windowCount),
        fingerprintSpan(shortParkR, windowStart, windowCount));

    INFO("(c) L: " << settledL.detail << " worst metric " << settledL.worstMetricRelativeError
                   << " worst sample " << settledL.worstSampleError);
    REQUIRE(settledL.withinTolerance());
    INFO("(c) R: " << settledR.detail << " worst metric " << settledR.worstMetricRelativeError
                   << " worst sample " << settledR.worstSampleError);
    REQUIRE(settledR.withinTolerance());

    // ANTI-VACUITY for (c): over the span where the two scripts genuinely
    // disagree (the short park released on frame 300, the long park still held
    // to frame 440, plus the fftSize output delay) the same comparison must
    // FAIL. Without this, a component whose decoherence did nothing at all
    // would pass the arm above for the wrong reason.
    const std::size_t divergeStart = (kParkStart + kParkShort + 5u) * kRefHop + kRefFft;
    const std::size_t divergeEnd   = (kParkStart + kParkLong - 5u) * kRefHop + kRefFft;
    REQUIRE(divergeEnd > divergeStart);

    const auto divergedL = Krate::DSP::TestUtils::compareFingerprints(
        fingerprintSpan(longParkL, divergeStart, divergeEnd - divergeStart),
        fingerprintSpan(shortParkL, divergeStart, divergeEnd - divergeStart));
    INFO("(c) anti-vacuity L worst metric " << divergedL.worstMetricRelativeError
                                            << " worst sample " << divergedL.worstSampleError);
    REQUIRE_FALSE(divergedL.withinTolerance());
}

// ---------------------------------------------------------------------------
// No clicks on control jumps (SC-012 arm (c); plan S7.3)
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_PreEchoAndClicks", "[spectral_smear]") {
    // SC-012 ARM (c) ONLY. Arms (a) (pre-echo) and (b) (its anti-vacuity) live
    // in spectral_smear_spectral_test.cpp as SpectralSmear_PreEcho, because
    // they need the [long] tone-burst renders.
    //
    // THE REFERENCE IS THE DESTINATION SETTING HELD CONSTANT FOR THE WHOLE
    // RENDER (spec correction C-10), never the origin setting. At
    // decoherence = 1 the output is a narrowband random-phase process whose RMS
    // FR-042's make-up restores, so its crest factor - and therefore its peak
    // inter-sample delta - is materially higher than a pure 1 kHz tone's
    // A * 2*pi*1000/48000 = 0.131*A WITH NO CLICK PRESENT AT ALL. Measured
    // against the origin, a correct build can exceed 6 dB; measured against the
    // destination, the arm prices the transient cost of the step itself.
    //
    // THE decoherence 0 -> 1 ARM IS THE ONE THAT FAILS WITHOUT FR-046's RAMP.
    // A per-hop-constant make-up jumps 1.000 -> ~1.55 at a single sample
    // boundary on the first frame after the step (the 50 ms smoother runs on
    // the 93.75 Hz frame clock, so one frame lands at 1 - 0.34424 = 0.656) -
    // a ~55 % instantaneous amplitude step against a peak inter-sample delta of
    // 0.131*A, i.e. a textbook 5-sigma derivative outlier. writeHopToFifo's
    // per-sample ramp from prevMakeup_ to g is what this arm exercises.
    constexpr std::size_t kBlock       = 512;
    constexpr std::size_t kTotalBlocks = 564;  // 564 * 512 = 288 768 samples = 6.02 s
    constexpr std::size_t kStepBlock   = 282;  // the single block the jump lands on

    const std::size_t numSamples = kTotalBlocks * kBlock;
    const std::size_t stepSample = kStepBlock * kBlock;

    std::vector<float> toneL(numSamples, 0.0f);
    std::vector<float> toneR(numSamples, 0.0f);
    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t     = static_cast<double>(i) / kFs48;
        const auto   value = static_cast<float>(0.5011872336 *  // -6 dBFS
                                                std::sin(2.0 * kTestPi * 1000.0 * t));
        toneL[i]           = value;
        toneR[i]           = value;
    }

    struct StepArm {
        const char* name;
        float       smearOrigin;
        float       smearDest;
        float       decoOrigin;
        float       decoDest;
        float       tiltOrigin;
        float       tiltDest;
    };

    // Only one control moves per arm; the other two are held at the SAME value
    // in the stepped and the reference render. The tilt arm holds
    // smearAmount = 1 because tilt only reshapes the pole table and FR-021's
    // identity gate writes no magnitude at all at smearAmount == 0 - at the
    // default amount the arm would be vacuous.
    const StepArm arms[3] = {
        {"smearAmount 0 -> 1", 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {"decoherence 0 -> 1", 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {"tilt -1 -> +1", 1.0f, 1.0f, 0.0f, 0.0f, -1.0f, 1.0f},
    };

    const auto renderArm = [&](const StepArm& arm, bool stepped, std::vector<float>& outL,
                               std::vector<float>& outR) {
        SpectralSmear smear;
        smear.setSmearAmount(stepped ? arm.smearOrigin : arm.smearDest);
        smear.setDecoherence(stepped ? arm.decoOrigin : arm.decoDest);
        smear.setSmearTilt(stepped ? arm.tiltOrigin : arm.tiltDest);
        smear.setSeed(0x5C012ACCu);
        smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});

        outL = toneL;
        outR = toneR;
        for (std::size_t block = 0; block < kTotalBlocks; ++block) {
            if (stepped && block == kStepBlock) {
                // All three setters fire in the SAME block - a single-block jump.
                smear.setSmearAmount(arm.smearDest);
                smear.setDecoherence(arm.decoDest);
                smear.setSmearTilt(arm.tiltDest);
            }
            smear.processBlock(outL.data() + block * kBlock, outR.data() + block * kBlock, kBlock);
        }
    };

    const auto peakDelta = [](const std::vector<float>& buffer, std::size_t from, std::size_t to) {
        float worst = 0.0f;
        for (std::size_t i = from + 1u; i < to; ++i) {
            worst = std::max(worst, std::abs(buffer[i] - buffer[i - 1u]));
        }
        return worst;
    };

    // The delta window is the SAME ABSOLUTE span in both renders and covers the
    // whole transition: the 50 ms frame-clock smoother settles in ~12 frames
    // and 4 * fftSize is 16 frames at the reference geometry.
    const std::size_t deltaFrom = stepSample;
    const std::size_t deltaTo   = stepSample + 4u * kRefFft;
    REQUIRE(deltaTo <= numSamples);

    const std::size_t analysisStart = 2u * kRefFft;  // past FR-014's warm-up zeros

    for (const StepArm& arm : arms) {
        INFO("arm: " << arm.name);

        std::vector<float> steppedL;
        std::vector<float> steppedR;
        renderArm(arm, /*stepped=*/true, steppedL, steppedR);

        std::vector<float> referenceL;
        std::vector<float> referenceR;
        renderArm(arm, /*stepped=*/false, referenceL, referenceR);

        // --- zero clicks on the STEPPED render, both channels ---------------
        Krate::DSP::TestUtils::ClickDetector detector{
            Krate::DSP::TestUtils::ClickDetectorConfig{.sampleRate         = 48000.0f,
                                                       .frameSize          = 512u,
                                                       .hopSize            = 256u,
                                                       .detectionThreshold = 5.0f}};
        detector.prepare();

        const std::vector<Krate::DSP::TestUtils::ClickDetection> clicksL =
            detector.detect(steppedL.data() + analysisStart, numSamples - analysisStart);
        detector.reset();
        const std::vector<Krate::DSP::TestUtils::ClickDetection> clicksR =
            detector.detect(steppedR.data() + analysisStart, numSamples - analysisStart);

        INFO("clicks L = " << clicksL.size() << " clicks R = " << clicksR.size());
        REQUIRE(clicksL.empty());
        REQUIRE(clicksR.empty());

        // --- peak inter-sample delta vs the DESTINATION-HELD reference ------
        const std::vector<float>* steppedCh[2]   = {&steppedL, &steppedR};
        const std::vector<float>* referenceCh[2] = {&referenceL, &referenceR};
        for (std::size_t ch = 0; ch < 2u; ++ch) {
            const float steppedDelta   = peakDelta(*steppedCh[ch], deltaFrom, deltaTo);
            const float referenceDelta = peakDelta(*referenceCh[ch], deltaFrom, deltaTo);
            REQUIRE(referenceDelta > 1.0e-6f);  // the reference window is not degenerate

            const double excessDb = 20.0 * std::log10(static_cast<double>(steppedDelta) /
                                                      static_cast<double>(referenceDelta));
            INFO("ch " << ch << " stepped peak delta = " << steppedDelta
                       << " reference peak delta = " << referenceDelta << " excess = " << excessDb
                       << " dB");
            REQUIRE(excessDb <= 6.0);
        }
    }
}
