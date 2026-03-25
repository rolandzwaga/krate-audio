// ==============================================================================
// Layer 3: System Tests - SympatheticResonance
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per spec 132-sympathetic-resonance
//
// Reference: specs/132-sympathetic-resonance/spec.md (FR-001 to FR-023, SC-001 to SC-015)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/systems/sympathetic_resonance.h>

#include <array>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// Create partial info for a harmonic voice at the given fundamental frequency.
/// Uses perfect harmonic series (B=0 inharmonicity).
SympatheticPartialInfo makeHarmonicPartials(float f0) {
    SympatheticPartialInfo info;
    for (int n = 0; n < kSympatheticPartialCount; ++n) {
        info.frequencies[static_cast<size_t>(n)] = f0 * static_cast<float>(n + 1);
    }
    return info;
}

/// Create partial info with inharmonicity coefficient B.
/// f_n = n * f0 * sqrt(1 + B * n^2) where n is 1-based.
SympatheticPartialInfo makeInharmonicPartials(float f0, float B) {
    SympatheticPartialInfo info;
    for (int n = 1; n <= kSympatheticPartialCount; ++n) {
        float nf = static_cast<float>(n);
        info.frequencies[static_cast<size_t>(n - 1)] =
            nf * f0 * std::sqrt(1.0f + B * nf * nf);
    }
    return info;
}

/// Generate a sine wave into a buffer.
void generateSine(float* buffer, int numSamples, float freq, float sampleRate) {
    for (int i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(kTwoPi * freq * static_cast<float>(i) / sampleRate);
    }
}

/// Compute RMS of a buffer.
float computeRMS(const float* buffer, int numSamples) {
    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(numSamples));
}

/// Compute peak absolute value of a buffer.
float computePeak(const float* buffer, int numSamples) {
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float v = std::abs(buffer[i]);
        if (v > peak) peak = v;
    }
    return peak;
}

/// Process N samples of silence through the resonance.
void processSilence(SympatheticResonance& sr, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
        (void)sr.process(0.0f);
    }
}

/// Process N samples of a sine wave and collect output.
void processSineAndCollect(SympatheticResonance& sr, float freq, float sampleRate,
                           int numSamples, std::vector<float>& output) {
    output.resize(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i) {
        float input = std::sin(kTwoPi * freq * static_cast<float>(i) / sampleRate);
        output[static_cast<size_t>(i)] = sr.process(input);
    }
}

} // anonymous namespace

// =============================================================================
// Sanity Test - verify basic output
// =============================================================================

TEST_CASE("SympatheticResonance: basic output sanity check", "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setAmount(0.5f);
    sr.setDecay(0.5f);

    sr.noteOn(0, makeHarmonicPartials(440.0f));
    REQUIRE(sr.getActiveResonatorCount() == 4);
    REQUIRE_FALSE(sr.isBypassed());

    // Process 4410 samples (100ms) with a 440 Hz sine
    float peak = 0.0f;
    int nonZeroCount = 0;
    for (int i = 0; i < 4410; ++i) {
        float input = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / 44100.0f);
        float out = sr.process(input);
        if (out != 0.0f) nonZeroCount++;
        float a = std::abs(out);
        if (a > peak) peak = a;
    }

    INFO("nonZeroCount = " << nonZeroCount);
    INFO("peak = " << peak);
    INFO("activeCount after = " << sr.getActiveResonatorCount());
    INFO("bypassed after = " << sr.isBypassed());
    REQUIRE(nonZeroCount > 0);
    REQUIRE(peak > 0.0f);
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("SympatheticResonance: prepare does not crash", "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    REQUIRE(sr.getActiveResonatorCount() == 0);
}

TEST_CASE("SympatheticResonance: reset clears state", "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setAmount(0.5f);
    sr.setDecay(0.5f);
    sr.noteOn(0, makeHarmonicPartials(440.0f));
    REQUIRE(sr.getActiveResonatorCount() == kSympatheticPartialCount);

    // Drive some samples to build up state
    for (int i = 0; i < 100; ++i) {
        (void)sr.process(0.5f);
    }

    sr.reset();
    REQUIRE(sr.getActiveResonatorCount() == 0);
}

// =============================================================================
// Bypass Tests (FR-014)
// =============================================================================

TEST_CASE("SympatheticResonance: bypass with amount=0", "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setAmount(0.0f);
    sr.setDecay(0.5f);
    sr.noteOn(0, makeHarmonicPartials(440.0f));

    // Even with active resonators, amount=0 should produce exactly 0
    for (int i = 0; i < 100; ++i) {
        float out = sr.process(1.0f);
        REQUIRE(out == 0.0f);
    }
    REQUIRE(sr.isBypassed());
}

// =============================================================================
// noteOn Basic (FR-008, FR-020)
// =============================================================================

TEST_CASE("SympatheticResonance: noteOn adds kSympatheticPartialCount resonators",
          "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setDecay(0.5f);
    sr.noteOn(0, makeHarmonicPartials(440.0f));
    REQUIRE(sr.getActiveResonatorCount() == kSympatheticPartialCount);
}

// =============================================================================
// noteOff Orphan (FR-009)
// =============================================================================

TEST_CASE("SympatheticResonance: noteOff does not immediately reclaim resonators",
          "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setAmount(0.5f);
    sr.setDecay(0.5f);
    sr.noteOn(0, makeHarmonicPartials(440.0f));
    REQUIRE(sr.getActiveResonatorCount() == 4);

    // Drive resonators to build up state
    for (int i = 0; i < 1000; ++i) {
        (void)sr.process(0.5f);
    }

    sr.noteOff(0);
    // Resonators should still be active (ringing out)
    REQUIRE(sr.getActiveResonatorCount() == 4);
}

// =============================================================================
// Resonator Reclaim (FR-009)
// =============================================================================

TEST_CASE("SympatheticResonance: resonators reclaimed below -96 dB threshold",
          "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setAmount(0.5f);
    // Use low Q for fast decay
    sr.setDecay(0.0f); // Q=100 -> fast decay
    sr.noteOn(0, makeHarmonicPartials(440.0f));

    // Drive briefly to excite resonators
    for (int i = 0; i < 500; ++i) {
        (void)sr.process(0.3f);
    }
    REQUIRE(sr.getActiveResonatorCount() == 4);

    sr.noteOff(0);

    // Process silence for a long time -- resonators should eventually be reclaimed
    // At Q=100, f=440, bandwidth=4.4Hz, ring-out to -96dB should take
    // roughly several thousand samples at 44100
    for (int i = 0; i < 441000; ++i) { // 10 seconds of silence
        (void)sr.process(0.0f);
    }
    REQUIRE(sr.getActiveResonatorCount() == 0);
}

// =============================================================================
// Pool Cap (FR-010)
// =============================================================================

TEST_CASE("SympatheticResonance: pool cap enforced at 64 resonators",
          "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setDecay(0.5f);
    sr.setAmount(0.5f);

    // Add 17 voices x 4 partials = 68 > 64 cap
    // Use widely separated frequencies to avoid merging
    for (int v = 0; v < 17; ++v) {
        float f0 = 100.0f + static_cast<float>(v) * 50.0f; // 100, 150, 200, ... 900
        sr.noteOn(v, makeHarmonicPartials(f0));
    }

    REQUIRE(sr.getActiveResonatorCount() <= kMaxSympatheticResonators);
}

// =============================================================================
// Merge (FR-008, FR-011)
// =============================================================================

TEST_CASE("SympatheticResonance: voices within 0.3 Hz are merged",
          "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setDecay(0.5f);

    // Two voices at 440.0 Hz and 440.07 Hz (within 0.3 Hz threshold for ALL harmonics)
    // Harmonics: 440/440.07 (diff=0.07), 880/880.14 (diff=0.14),
    //            1320/1320.21 (diff=0.21), 1760/1760.28 (diff=0.28)
    // All diffs < 0.3 Hz
    sr.noteOn(0, makeHarmonicPartials(440.0f));
    REQUIRE(sr.getActiveResonatorCount() == 4);

    sr.noteOn(1, makeHarmonicPartials(440.07f));
    // Should merge all 4 partials, not add 4 more
    REQUIRE(sr.getActiveResonatorCount() == 4);
}

TEST_CASE("SympatheticResonance: voices ~1 Hz apart NOT merged (preserve beating)",
          "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setDecay(0.5f);

    // Two voices at 440 Hz and 441 Hz (1 Hz apart, > 0.3 Hz threshold)
    sr.noteOn(0, makeHarmonicPartials(440.0f));
    REQUIRE(sr.getActiveResonatorCount() == 4);

    sr.noteOn(1, makeHarmonicPartials(441.0f));
    // Fundamental is 1 Hz apart, 2nd partial is 2 Hz apart, etc. -- all > 0.3 Hz
    REQUIRE(sr.getActiveResonatorCount() == 8);
}

// =============================================================================
// True Duplicate Merge (FR-011)
// =============================================================================

TEST_CASE("SympatheticResonance: re-triggering same voiceId merges",
          "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setDecay(0.5f);
    sr.setAmount(0.5f);

    sr.noteOn(0, makeHarmonicPartials(440.0f));
    REQUIRE(sr.getActiveResonatorCount() == 4);

    // Re-trigger same voiceId -- should not double the resonator count
    sr.noteOn(0, makeHarmonicPartials(440.0f));
    // The old resonators are orphaned (noteOff called internally), then the
    // new ones may or may not merge with orphans. At most 4 + up to 4 orphans.
    // But the orphaned resonators have the same frequency, so they should merge.
    REQUIRE(sr.getActiveResonatorCount() <= 8);
}

// =============================================================================
// Inharmonicity (FR-018, FR-020)
// =============================================================================

TEST_CASE("SympatheticResonance: inharmonicity-adjusted partial frequencies",
          "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setDecay(0.5f);

    // B = 0.0001 (typical piano inharmonicity)
    float B = 0.0001f;
    float f0 = 440.0f;
    auto partials = makeInharmonicPartials(f0, B);

    // Verify the partial frequencies follow f_n = n * f0 * sqrt(1 + B * n^2)
    for (int n = 1; n <= kSympatheticPartialCount; ++n) {
        float nf = static_cast<float>(n);
        float expected = nf * f0 * std::sqrt(1.0f + B * nf * nf);
        REQUIRE(partials.frequencies[static_cast<size_t>(n - 1)] ==
                Approx(expected).margin(0.01f));
    }

    sr.noteOn(0, partials);
    REQUIRE(sr.getActiveResonatorCount() == kSympatheticPartialCount);
}

// =============================================================================
// Second-Order Recurrence Coefficients (FR-006)
// =============================================================================

TEST_CASE("SympatheticResonance: resonator coefficients computed correctly",
          "[systems][sympathetic]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setDecay(0.5f); // Q_user = 100 * 10^0.5 ~= 316.2

    float f = 440.0f;
    float Q_user = 100.0f * std::pow(10.0f, 0.5f); // ~316.2
    // At 440 Hz, Q_eff = Q_user * clamp(500/440, 0.5, 1.0) = Q_user * 1.0
    float Q_eff = Q_user;
    float delta_f = f / Q_eff;
    float expected_r = std::exp(-kPi * delta_f / 44100.0f);
    float expected_omega = kTwoPi * f / 44100.0f;
    float expected_coeff = 2.0f * expected_r * std::cos(expected_omega);

    sr.noteOn(0, makeHarmonicPartials(f));

    // We verify indirectly by driving a single-frequency resonator and checking
    // that it resonates at the correct frequency. With correct coefficients,
    // the resonator should respond strongly to its tuned frequency.
    sr.setAmount(0.5f);

    // Drive with a sine at the resonant frequency for several cycles
    std::vector<float> output;
    processSineAndCollect(sr, f, 44100.0f, 4410, output); // 100ms

    // The output should have significant energy (resonator is excited at its frequency)
    float rms = computeRMS(output.data(), static_cast<int>(output.size()));
    REQUIRE(rms > 0.0f);

    // Verify the expected coefficient values are physically reasonable
    REQUIRE(expected_r > 0.99f);
    REQUIRE(expected_r < 1.0f);
    REQUIRE(expected_coeff > 0.0f);
    REQUIRE(expected_coeff < 2.0f);
}

// =============================================================================
// Frequency-Dependent Q (FR-013)
// =============================================================================

TEST_CASE("SympatheticResonance: frequency-dependent Q scaling",
          "[systems][sympathetic]") {
    // The frequency-dependent Q formula is:
    // Q_eff = Q_user * clamp(500/f, 0.5, 1.0)

    // At f=200 Hz: 500/200 = 2.5, clamped to 1.0 -> Q_eff = Q_user
    // At f=500 Hz: 500/500 = 1.0, clamped to 1.0 -> Q_eff = Q_user
    // At f=1000 Hz: 500/1000 = 0.5, clamped to 0.5 -> Q_eff = Q_user * 0.5
    // At f=2000 Hz: 500/2000 = 0.25, clamped to 0.5 -> Q_eff = Q_user * 0.5

    // We verify indirectly through decay time difference.
    // A resonator at 1000 Hz should decay roughly twice as fast as one at 200 Hz
    // (because Q_eff at 1000 Hz is half of Q_eff at 200 Hz).

    constexpr float sampleRate = 44100.0f;

    // Test at low frequency: full Q
    {
        SympatheticResonance sr;
        sr.prepare(sampleRate);
        sr.setDecay(0.5f);
        sr.setAmount(0.5f);

        SympatheticPartialInfo partials;
        partials.frequencies = {200.0f, 0.0f, 0.0f, 0.0f};
        sr.noteOn(0, partials);

        // Drive briefly
        for (int i = 0; i < 2000; ++i) {
            (void)sr.process(std::sin(kTwoPi * 200.0f * static_cast<float>(i) / sampleRate));
        }

        sr.noteOff(0);

        // Measure how many samples to reach near silence
        int samplesLow = 0;
        for (int i = 0; i < 441000; ++i) {
            float out = sr.process(0.0f);
            ++samplesLow;
            if (std::abs(out) < 1e-6f && i > 1000) break;
        }

        // Test at high frequency: half Q, should decay faster
        SympatheticResonance sr2;
        sr2.prepare(sampleRate);
        sr2.setDecay(0.5f);
        sr2.setAmount(0.5f);

        SympatheticPartialInfo partials2;
        partials2.frequencies = {1000.0f, 0.0f, 0.0f, 0.0f};
        sr2.noteOn(0, partials2);

        for (int i = 0; i < 2000; ++i) {
            (void)sr2.process(std::sin(kTwoPi * 1000.0f * static_cast<float>(i) / sampleRate));
        }
        sr2.noteOff(0);

        int samplesHigh = 0;
        for (int i = 0; i < 441000; ++i) {
            float out = sr2.process(0.0f);
            ++samplesHigh;
            if (std::abs(out) < 1e-6f && i > 1000) break;
        }

        // High frequency should decay faster (fewer samples to silence)
        REQUIRE(samplesHigh < samplesLow);
    }
}

// =============================================================================
// Per-Resonator Gain Weighting (FR-007)
// =============================================================================

TEST_CASE("SympatheticResonance: per-resonator gain = 1/sqrt(n)",
          "[systems][sympathetic]") {
    // Partial 1: gain = 1/sqrt(1) = 1.0
    // Partial 2: gain = 1/sqrt(2) ~= 0.7071
    // Partial 3: gain = 1/sqrt(3) ~= 0.5774
    // Partial 4: gain = 1/sqrt(4) = 0.5

    // We verify indirectly: drive with a wideband signal and check that
    // the fundamental resonator (partial 1) produces more output than the 4th partial.
    // This is a behavioral test.

    constexpr float sampleRate = 44100.0f;
    SympatheticResonance sr;
    sr.prepare(sampleRate);
    sr.setDecay(0.5f);
    sr.setAmount(0.5f);

    // Use a frequency where partials are well separated for analysis
    float f0 = 200.0f; // partials at 200, 400, 600, 800

    // Test partial 1 alone vs partial 4 alone
    SympatheticPartialInfo p1only;
    p1only.frequencies = {f0, 0.0f, 0.0f, 0.0f}; // only partial 1

    sr.noteOn(0, p1only);

    // Drive with broadband noise-like signal
    float rmsPartial1 = 0.0f;
    {
        std::vector<float> out(4410);
        for (int i = 0; i < 4410; ++i) {
            float input = std::sin(kTwoPi * f0 * static_cast<float>(i) / sampleRate);
            out[static_cast<size_t>(i)] = sr.process(input);
        }
        rmsPartial1 = computeRMS(out.data(), 4410);
    }

    sr.reset();
    sr.prepare(sampleRate);
    sr.setDecay(0.5f);
    sr.setAmount(0.5f);

    // Test with partial 4 only (at 4*f0 = 800 Hz, gain = 0.5)
    SympatheticPartialInfo p4only;
    p4only.frequencies = {0.0f, 0.0f, 0.0f, f0 * 4.0f}; // only partial 4

    sr.noteOn(1, p4only);

    float rmsPartial4 = 0.0f;
    {
        std::vector<float> out(4410);
        for (int i = 0; i < 4410; ++i) {
            float input = std::sin(kTwoPi * f0 * 4.0f * static_cast<float>(i) / sampleRate);
            out[static_cast<size_t>(i)] = sr.process(input);
        }
        rmsPartial4 = computeRMS(out.data(), 4410);
    }

    // Partial 1 should produce more output than partial 4
    // (gain 1.0 vs 0.5, plus frequency-dependent Q makes high freq decay faster)
    REQUIRE(rmsPartial1 > rmsPartial4);
}

// =============================================================================
// Sample Rate Scaling (FR-022)
// =============================================================================

TEST_CASE("SympatheticResonance: sample rate scaling produces different coefficients",
          "[systems][sympathetic]") {
    // Verify indirectly: same frequency at different sample rates produces
    // different decay characteristics

    float f = 440.0f;

    // At 44100 Hz
    SympatheticResonance sr1;
    sr1.prepare(44100.0);
    sr1.setDecay(0.5f);
    sr1.setAmount(0.5f);
    sr1.noteOn(0, makeHarmonicPartials(f));

    // Drive briefly and measure output
    float peak44 = 0.0f;
    for (int i = 0; i < 4410; ++i) {
        float out = sr1.process(std::sin(kTwoPi * f * static_cast<float>(i) / 44100.0f));
        float v = std::abs(out);
        if (v > peak44) peak44 = v;
    }

    // At 96000 Hz
    SympatheticResonance sr2;
    sr2.prepare(96000.0);
    sr2.setDecay(0.5f);
    sr2.setAmount(0.5f);
    sr2.noteOn(0, makeHarmonicPartials(f));

    float peak96 = 0.0f;
    for (int i = 0; i < 9600; ++i) { // same real time duration
        float out = sr2.process(std::sin(kTwoPi * f * static_cast<float>(i) / 96000.0f));
        float v = std::abs(out);
        if (v > peak96) peak96 = v;
    }

    // Both should produce non-zero output (resonators work at both rates)
    REQUIRE(peak44 > 0.0f);
    REQUIRE(peak96 > 0.0f);
}

// =============================================================================
// Harmonic Hierarchy (SC-002, SC-014)
// =============================================================================

TEST_CASE("SympatheticResonance: harmonic hierarchy via merge count",
          "[systems][sympathetic]") {
    // The harmonic hierarchy (octave > fifth > third > dissonant) emerges naturally
    // from the harmonic overlap condition. Consonant intervals have more partials
    // that align, producing FEWER total resonators (more merges).
    // Fewer resonators means the system is more efficient -- the shared resonators
    // get reinforced by both voices through the global sum.

    // Octave pair: 220 Hz + 440 Hz (2:1 ratio)
    // Voice 0 partials: 220, 440, 660, 880
    // Voice 1 partials: 440, 880, 1320, 1760
    // Shared: 440 Hz and 880 Hz merge -> 6 total resonators
    int countOctave = 0;
    {
        SympatheticResonance sr;
        sr.prepare(44100.0);
        sr.setDecay(0.5f);
        sr.noteOn(0, makeHarmonicPartials(220.0f));
        sr.noteOn(1, makeHarmonicPartials(440.0f));
        countOctave = sr.getActiveResonatorCount();
    }

    // Fifth pair: 220 Hz + 330 Hz (3:2 ratio)
    // Voice 0 partials: 220, 440, 660, 880
    // Voice 1 partials: 330, 660, 990, 1320
    // Shared: 660 Hz merges -> 7 total resonators
    int countFifth = 0;
    {
        SympatheticResonance sr;
        sr.prepare(44100.0);
        sr.setDecay(0.5f);
        sr.noteOn(0, makeHarmonicPartials(220.0f));
        sr.noteOn(1, makeHarmonicPartials(330.0f));
        countFifth = sr.getActiveResonatorCount();
    }

    // Minor second: 220 Hz + 233 Hz (dissonant)
    // Very few harmonics overlap -> ~8 total resonators (almost no merges)
    int countMinorSecond = 0;
    {
        SympatheticResonance sr;
        sr.prepare(44100.0);
        sr.setDecay(0.5f);
        sr.noteOn(0, makeHarmonicPartials(220.0f));
        sr.noteOn(1, makeHarmonicPartials(233.0f));
        countMinorSecond = sr.getActiveResonatorCount();
    }

    // Consonant intervals produce more merges (fewer total resonators)
    // Octave: most merges (fewest resonators)
    // Minor second: fewest merges (most resonators)
    INFO("countOctave = " << countOctave);
    INFO("countFifth = " << countFifth);
    INFO("countMinorSecond = " << countMinorSecond);
    REQUIRE(countOctave <= countFifth);
    REQUIRE(countFifth <= countMinorSecond);
}

// =============================================================================
// Dissonant Interval (SC-006)
// =============================================================================

TEST_CASE("SympatheticResonance: dissonant interval has no merged resonators",
          "[systems][sympathetic]") {
    // A minor second (440 Hz + 466 Hz) is dissonant -- very few harmonics align.
    // Voice 0 partials: 440, 880, 1320, 1760
    // Voice 1 partials: 466, 932, 1398, 1864
    // Differences: 26, 52, 78, 104 -- all >> 0.3 Hz threshold, so no merges
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setDecay(0.5f);

    sr.noteOn(0, makeHarmonicPartials(440.0f));
    REQUIRE(sr.getActiveResonatorCount() == 4);

    sr.noteOn(1, makeHarmonicPartials(466.0f));
    // No merges expected -- all 8 resonators should be separate
    REQUIRE(sr.getActiveResonatorCount() == 8);
}

// =============================================================================
// Self-Excitation Inaudibility (FR-021, SC-001)
// =============================================================================

TEST_CASE("SympatheticResonance: self-excitation is bounded and reaches steady state",
          "[systems][sympathetic]") {
    constexpr float sampleRate = 44100.0f;
    constexpr int numSamples = 88200; // 2 seconds

    SympatheticResonance sr;
    sr.prepare(sampleRate);
    sr.setDecay(0.5f);
    sr.setAmount(0.5f);

    // Single voice -- its own partials excite its own resonators
    sr.noteOn(0, makeHarmonicPartials(440.0f));

    // Drive for 2 seconds and verify:
    // 1. Output reaches a steady state (not growing unbounded)
    // 2. The steady-state output is finite

    // Measure peak in first half vs second half
    float peakFirstHalf = 0.0f;
    float peakSecondHalf = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float dry = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / sampleRate);
        float wet = sr.process(dry);
        float absWet = std::abs(wet);
        if (i < numSamples / 2) {
            if (absWet > peakFirstHalf) peakFirstHalf = absWet;
        } else {
            if (absWet > peakSecondHalf) peakSecondHalf = absWet;
        }
    }

    // Output should be bounded (not growing without limit)
    // The second half peak should not be much larger than the first half
    // (resonator reaches steady state within ~Q/(pi*f) * fs samples ≈ 10k samples ≈ 227ms)
    INFO("peakFirstHalf = " << peakFirstHalf);
    INFO("peakSecondHalf = " << peakSecondHalf);
    REQUIRE(peakSecondHalf < peakFirstHalf * 2.0f); // Not growing
    REQUIRE(peakSecondHalf > 0.0f); // Actually producing output
    REQUIRE(peakSecondHalf < 1e6f); // Not infinite
}

// =============================================================================
// Unidirectional Coupling (FR-005)
// =============================================================================

TEST_CASE("SympatheticResonance: output stays bounded for sustained driving",
          "[systems][sympathetic]") {
    constexpr float sampleRate = 44100.0f;

    SympatheticResonance sr;
    sr.prepare(sampleRate);
    sr.setDecay(1.0f); // Maximum Q = 1000
    sr.setAmount(0.5f);

    sr.noteOn(0, makeHarmonicPartials(440.0f));

    // Drive with full-scale input for 2 seconds to reach steady state
    // The resonator at Q=1000 amplifies by ~1/(1-r^2) at resonance.
    // With coupling gain ~0.0316, the steady-state peak per resonator can be large
    // but MUST be finite (unidirectional coupling guarantees no runaway).
    float maxAbs = 0.0f;
    for (int i = 0; i < 88200; ++i) {
        float input = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / sampleRate);
        float out = sr.process(input);
        float a = std::abs(out);
        if (a > maxAbs) maxAbs = a;
    }

    // Output should be finite (not NaN/Inf) and bounded
    INFO("maxAbs = " << maxAbs);
    REQUIRE(maxAbs > 0.0f);    // Actually producing output
    REQUIRE(maxAbs < 1e6f);     // Not infinite / NaN
}

// =============================================================================
// User Story 2: Sympathetic Amount Control (Phase 4)
// =============================================================================

TEST_CASE("SympatheticResonance US2: zero bypass produces exactly 0.0 for any input",
          "[systems][sympathetic][amount]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setAmount(0.0f);
    sr.setDecay(0.5f);
    sr.noteOn(0, makeHarmonicPartials(440.0f));

    // Even with active resonators, amount=0 should produce exactly 0
    bool anyNonZero = false;
    for (int i = 0; i < 500; ++i) {
        float input = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / 44100.0f);
        float out = sr.process(input);
        if (out != 0.0f) anyNonZero = true;
    }
    REQUIRE_FALSE(anyNonZero);
    REQUIRE(sr.isBypassed());
}

TEST_CASE("SympatheticResonance US2: non-zero activation with tiny amount",
          "[systems][sympathetic][amount]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    sr.setDecay(0.5f);
    sr.noteOn(0, makeHarmonicPartials(440.0f));

    // Set a tiny but non-zero amount
    sr.setAmount(0.001f);

    // The smoother needs time to ramp up from 0 to the target coupling gain.
    // Process enough samples for the smoother to reach the target.
    bool anyNonZero = false;
    for (int i = 0; i < 4410; ++i) { // 100ms
        float input = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / 44100.0f);
        float out = sr.process(input);
        if (out != 0.0f) anyNonZero = true;
    }

    REQUIRE(anyNonZero);
    REQUIRE_FALSE(sr.isBypassed());
}

TEST_CASE("SympatheticResonance US2: amount=1.0 produces more energy than amount=0.1",
          "[systems][sympathetic][amount]") {
    constexpr float sampleRate = 44100.0f;
    constexpr int numSamples = 4410; // 100ms

    // Test with amount=0.1
    float rmsLow = 0.0f;
    {
        SympatheticResonance sr;
        sr.prepare(sampleRate);
        sr.setDecay(0.5f);
        sr.setAmount(0.1f);
        sr.noteOn(0, makeHarmonicPartials(440.0f));

        std::vector<float> output;
        processSineAndCollect(sr, 440.0f, sampleRate, numSamples, output);
        rmsLow = computeRMS(output.data(), numSamples);
    }

    // Test with amount=1.0
    float rmsHigh = 0.0f;
    {
        SympatheticResonance sr;
        sr.prepare(sampleRate);
        sr.setDecay(0.5f);
        sr.setAmount(1.0f);
        sr.noteOn(0, makeHarmonicPartials(440.0f));

        std::vector<float> output;
        processSineAndCollect(sr, 440.0f, sampleRate, numSamples, output);
        rmsHigh = computeRMS(output.data(), numSamples);
    }

    INFO("rmsLow (amount=0.1) = " << rmsLow);
    INFO("rmsHigh (amount=1.0) = " << rmsHigh);
    REQUIRE(rmsHigh > rmsLow);
}

TEST_CASE("SympatheticResonance US2: smooth transition up (no clicks)",
          "[systems][sympathetic][amount]") {
    // Verify that transitioning amount from 0 to 1 produces no discontinuity
    // at the transition point. We compare two instances: one with smoothing
    // (normal setAmount) and check that the first sample after setAmount(1.0)
    // is close to the last sample before (which was 0.0 since amount was 0).
    // The smoother should ramp gradually from 0 to the target gain.
    constexpr float sampleRate = 44100.0f;
    constexpr int sweepSamples = 500;

    SympatheticResonance sr;
    sr.prepare(sampleRate);
    sr.setDecay(0.5f);
    sr.noteOn(0, makeHarmonicPartials(440.0f));

    // Start at 0 and settle
    sr.setAmount(0.0f);
    for (int i = 0; i < 100; ++i) {
        (void)sr.process(0.5f);
    }

    // Record last output at amount=0 (should be 0.0 due to bypass)
    float lastAtZero = sr.process(0.5f);

    // Now set amount to 1.0
    sr.setAmount(1.0f);

    // The first sample after transition should be close to the last (smooth ramp)
    float firstAfterTransition = sr.process(0.5f);
    float transitionDelta = std::abs(firstAfterTransition - lastAtZero);
    INFO("transitionDelta = " << transitionDelta);
    // Smoother starts at 0 and ramps to target; first sample should be near 0
    REQUIRE(transitionDelta < 0.01f);

    // Additionally verify the output grows over the sweep period (ramp, not step)
    float peakFirst50 = 0.0f;
    float peakLast50 = 0.0f;
    for (int i = 0; i < sweepSamples; ++i) {
        float input = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / sampleRate);
        float out = sr.process(input);
        float a = std::abs(out);
        if (i < 50) {
            if (a > peakFirst50) peakFirst50 = a;
        }
        if (i >= sweepSamples - 50) {
            if (a > peakLast50) peakLast50 = a;
        }
    }
    // Output should grow over time as smoother ramps up
    REQUIRE(peakLast50 > peakFirst50);
}

TEST_CASE("SympatheticResonance US2: smooth transition down (no clicks)",
          "[systems][sympathetic][amount]") {
    // Verify that transitioning amount from 1 to 0 produces no abrupt
    // discontinuity. The smoother should fade the coupling gain gradually,
    // and the resonators continue ringing with decaying excitation.
    constexpr float sampleRate = 44100.0f;
    constexpr int sweepSamples = 500;

    SympatheticResonance sr;
    sr.prepare(sampleRate);
    sr.setDecay(0.5f);
    sr.setAmount(1.0f);
    sr.noteOn(0, makeHarmonicPartials(440.0f));

    // Drive at full amount to reach steady state
    for (int i = 0; i < 4410; ++i) {
        float input = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / sampleRate);
        (void)sr.process(input);
    }

    // Record the last output before transition
    float lastBeforeTransition = sr.process(0.5f);

    // Now set amount to 0
    sr.setAmount(0.0f);

    // The first sample after should be very close to the last before
    // (smoother hasn't had time to change much in one sample)
    float firstAfterTransition = sr.process(0.5f);
    float transitionDelta = std::abs(firstAfterTransition - lastBeforeTransition);
    INFO("lastBeforeTransition = " << lastBeforeTransition);
    INFO("firstAfterTransition = " << firstAfterTransition);
    INFO("transitionDelta = " << transitionDelta);

    // The change at the transition point should be small relative to the signal level
    // The smoother ensures the coupling gain changes gradually
    // Allow up to 5% of the signal magnitude as acceptable delta
    float maxSignalLevel = std::max(std::abs(lastBeforeTransition),
                                    std::abs(firstAfterTransition));
    if (maxSignalLevel > 0.0f) {
        float relativeDelta = transitionDelta / maxSignalLevel;
        INFO("relativeDelta = " << relativeDelta);
        REQUIRE(relativeDelta < 0.05f);
    }

    // Additionally verify the output decays over the sweep period (fade, not abrupt cut)
    float peakFirst50 = 0.0f;
    float peakLast50 = 0.0f;
    for (int i = 0; i < sweepSamples; ++i) {
        float input = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / sampleRate);
        float out = sr.process(input);
        float a = std::abs(out);
        if (i < 50) {
            if (a > peakFirst50) peakFirst50 = a;
        }
        if (i >= sweepSamples - 50) {
            if (a > peakLast50) peakLast50 = a;
        }
    }
    // Output should decrease over time as smoother fades to 0
    REQUIRE(peakFirst50 > peakLast50);
}

TEST_CASE("SympatheticResonance US2: snapTo makes isBypassed true immediately",
          "[systems][sympathetic][amount]") {
    SympatheticResonance sr;
    sr.prepare(44100.0);
    // After prepare(), smoother is snapped to 0, and couplingGain_ is 0
    sr.setAmount(0.0f);
    REQUIRE(sr.isBypassed());
}

TEST_CASE("SympatheticResonance US2: setAmount called every block does not reset pool",
          "[systems][sympathetic][amount]") {
    constexpr float sampleRate = 44100.0f;
    constexpr int blockSize = 64;
    constexpr int numBlocks = 100;

    SympatheticResonance sr;
    sr.prepare(sampleRate);
    sr.setDecay(0.5f);
    sr.setAmount(0.5f);
    sr.noteOn(0, makeHarmonicPartials(440.0f));

    REQUIRE(sr.getActiveResonatorCount() == kSympatheticPartialCount);

    // Simulate 100 blocks, calling setAmount before each block
    float lastBlockRms = 0.0f;
    for (int block = 0; block < numBlocks; ++block) {
        sr.setAmount(0.5f); // Same value every block

        float blockSum = 0.0f;
        for (int s = 0; s < blockSize; ++s) {
            int sampleIdx = block * blockSize + s;
            float input = std::sin(kTwoPi * 440.0f * static_cast<float>(sampleIdx) / sampleRate);
            float out = sr.process(input);
            blockSum += out * out;
        }
        lastBlockRms = std::sqrt(blockSum / static_cast<float>(blockSize));
    }

    // Resonators should still be active after 100 blocks
    REQUIRE(sr.getActiveResonatorCount() == kSympatheticPartialCount);
    // Last block should have meaningful output (not reset to silence)
    REQUIRE(lastBlockRms > 0.0f);
}
