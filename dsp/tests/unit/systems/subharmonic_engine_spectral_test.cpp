// ==============================================================================
// Layer 3: System Tests - SubharmonicEngine, spectral and long-render cases
// ==============================================================================
// Vorago Phase 6 (specs/vorago-phase6-subharmonic): SubharmonicEngine.
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase6-subharmonic/spec.md
//            specs/vorago-phase6-subharmonic/plan.md   (S10.1, S10.2, S10.3)
//            specs/vorago-phase6-subharmonic/tasks.md  (T001 creates this TU;
//                                                       T006 lands the helper
//                                                       self-check; later tasks
//                                                       land the engine cases)
//
// SCOPE OF THIS TU (plan S10.2): SC-002, SC-003, SC-004, SC-005 and
//   SC-017 (A)+(B) - the latter tagged [long], excluded from the per-push CI
//   filter and run nightly on all three OSes.
//
// This TU also owns the shared IsolatedSub fixture (plan S10.3) and the
//   SubharmonicEngine_LowFrequencyMetricsSelfCheck case that validates
//   tests/test_helpers/low_frequency_metrics.h against synthetic signals of
//   known truth (T006).
//
// Deliberately OUT of the "-fno-fast-math" block in dsp/tests/CMakeLists.txt.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "low_frequency_metrics.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/minblep_table.h>
#include <krate/dsp/primitives/two_pole_lp.h>
#include <krate/dsp/processors/sub_oscillator.h>
#include <krate/dsp/systems/subharmonic_engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// The whole phase measures at 48 kHz; kLowFrequencyFftSize is specified against
// that rate (0.1831 Hz bins, 5.46 s of signal).
constexpr double kSelfCheckSampleRate = 48000.0;
constexpr double kSelfCheckTwoPi = 6.283185307179586;

/// Add a sine of the given frequency and amplitude to `buffer`.
/// Phase is accumulated in double from the sample index, so the frequency of
/// the synthesised tone is exact to well below the 0.005 Hz tolerance the
/// estimator is measured against.
void addSine(std::vector<float>& buffer, double freqHz, double amplitude) {
    const double omega = kSelfCheckTwoPi * freqHz / kSelfCheckSampleRate;
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] += static_cast<float>(amplitude * std::sin(omega * static_cast<double>(i)));
    }
}

namespace lfm = Krate::DSP::TestUtils;

using Krate::DSP::SubharmonicEngine;
using Krate::DSP::SubWaveform;

// ==============================================================================
// T016 - the shared IsolatedSub fixture (spec.md:883-900, plan S10.3)
// ==============================================================================
// Stated once in the spec, so it is coded once here. SC-003, SC-004, SC-005 and
// SC-020 (b) use it verbatim, gate included; SC-002 (T017) reuses the
// configuration but replaces the gate with its own two-part anchor gate,
// because the unconditional -60 dBFS floor would fail a CORRECT implementation
// at its -60 dBFS body step (predicted tap -64.5 dBFS, plan S10.3).
//
// WHY -20 dBFS AND NOT THE FR-020 DEFAULT: FR-041's tanh is in circuit even at
// drive 0, and its own relative third harmonic is ~ A^2/24 - 0.042 % at
// A = 0.1, two orders below SC-004's 2 % ceiling. At the -6 dB default
// (A = 0.5) it is ~1.0 %, which would consume half the budget and turn SC-004
// into a measurement of the shaper rather than of the dividers.
//
// WHY THE ENGINE IS NOT DORMANT HERE (plan S10.3, implementer's note): the two
// unused tones are silent because toneLevelGain(kMinToneLevelDb) is exactly
// 0.0f - but their OSCILLATORS still run (FR-025's deliberate deviation), so
// allTonesDormant() is false and the whole FR-040 chain (low-pass, saturator,
// DC blocker) is live on every sample. That is intended: the fixture measures
// the chain, not a bypass of it.
// ==============================================================================
struct IsolatedSub {
    static constexpr double kSampleRate = 48000.0;
    static constexpr std::size_t kBlockSamples = 512;

    /// The one enabled tone's level. See the note above - this figure is part
    /// of SC-004's error budget, not a cosmetic choice.
    static constexpr float kToneLevelDb = -20.0f;

    /// 2 s of settling discarded before anything is analysed: the FR-022 level
    /// ramps, the FR-032 tracking ramp, the FR-051 wet ramp and the S5.3 cutoff
    /// glide are all 50 ms, so 2 s is 40 time constants of margin.
    static constexpr std::size_t kSettleSamples = 96000;

    /// Plan S10.3's aliveness gate, in dBFS. A below-gate reading means the
    /// tone is silent and every spectral figure taken from it is noise - R-1's
    /// general backstop against a SubOscillator that hard-failed to
    /// prepared_ = false on an unprepared MinBlepTable and returns 0.0f forever
    /// (sub_oscillator.h:144-147, :224-226).
    static constexpr float kRmsGateDb = -60.0f;

    SubharmonicEngine engine;

    /// @param enabledTone   The single tone under measurement.
    /// @param fundamentalHz The note pitch written through FR-013's one setter.
    /// @param waveform      Sine for SC-003/SC-004, Square for SC-005.
    IsolatedSub(std::size_t enabledTone, float fundamentalHz, SubWaveform waveform) {
        engine.prepare(kSampleRate,
                       SubharmonicEngine::PrepareConfig{.maxBlockSamples = kBlockSamples});

        // trackingAmount 0 -> trackGain is exactly 1, so the sub is present
        // without a body to hold it up and the measured level is stationary.
        engine.setTrackingAmount(0.0f);
        engine.setWetGainDb(0.0f);
        engine.setLowpassCutoffHz(SubharmonicEngine::kMaxLowpassHz);
        engine.setDriveDb(SubharmonicEngine::kMinDriveDb);
        engine.setFundamentalHz(fundamentalHz);

        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            engine.setToneBreathDepth(t, 0.0f);
            engine.setToneLevelDb(
                t, (t == enabledTone) ? kToneLevelDb : SubharmonicEngine::kMinToneLevelDb);
        }
        engine.setToneWaveform(enabledTone, waveform);
    }

    /// Render `analysisSamples` of subTap on a SILENT stereo input, after
    /// discarding kSettleSamples. The tap is the measured signal for every
    /// criterion built on this fixture: by FR-062 it is post-chain,
    /// post-tracking, PRE-wet-gain and PRE-clamp.
    [[nodiscard]] std::vector<float> renderTap(std::size_t analysisSamples) {
        const std::vector<float> silence(kBlockSamples, 0.0f);
        std::vector<float> outL(kBlockSamples, 0.0f);
        std::vector<float> outR(kBlockSamples, 0.0f);
        std::vector<float> discardTap(kBlockSamples, 0.0f);

        for (std::size_t done = 0; done < kSettleSamples;) {
            const std::size_t n = std::min(kBlockSamples, kSettleSamples - done);
            engine.processBlockTapped(silence.data(), silence.data(), outL.data(), outR.data(),
                                      discardTap.data(), n);
            done += n;
        }

        std::vector<float> tap(analysisSamples, 0.0f);
        for (std::size_t done = 0; done < analysisSamples;) {
            const std::size_t n = std::min(kBlockSamples, analysisSamples - done);
            engine.processBlockTapped(silence.data(), silence.data(), outL.data(), outR.data(),
                                      tap.data() + done, n);
            done += n;
        }
        return tap;
    }
};

/// The fixture's aliveness gate, applied before any spectral figure is trusted.
void requireTapAboveGate(const std::vector<float>& tap, const std::string& what) {
    const float rms = lfm::lowFreq::frameRms(tap.data(), tap.size());
    const float rmsDb = Krate::DSP::gainToDb(rms);
    INFO(what << ": analysed subTap RMS = " << rmsDb << " dBFS (gate "
              << IsolatedSub::kRmsGateDb << " dBFS)");
    REQUIRE(rmsDb > IsolatedSub::kRmsGateDb);
}

// ==============================================================================
// The per-tone fundamental sweeps SC-003 and SC-004 share (spec.md:940-944)
// ==============================================================================
// One tone at a time, and the fundamentals are chosen PER TONE so the tone
// clears the FR-016 backstop floor (kMinToneHz = 12 Hz) at every point: at
// f = 55 the Div4 tone would be 13.75 Hz - awake, but at f = 44 it would be
// 11 Hz and floored to silence, and a criterion measured on silence is not a
// criterion. Every listed point puts the tone at 27.5 Hz or above.
//
// Listed DESCENDING, because that is the direction SC-004 (b)'s monotonicity is
// stated in ("220 -> 110 -> 55 for Div2/Fifth; 440 -> 220 -> 110 for Div4"), so
// the array order and the assertion order are the same thing.
struct ToneSweep {
    const char* name;
    std::size_t tone;
    double ratio;  // tone frequency = ratio * fundamental (FR-012)
    std::array<float, 3> fundamentals;
};

constexpr std::array<ToneSweep, 3> kToneSweeps{{
    {.name = "Div2",
     .tone = SubharmonicEngine::index(SubharmonicEngine::Tone::Div2),
     .ratio = 0.5,
     .fundamentals = std::array<float, 3>{220.0f, 110.0f, 55.0f}},
    {.name = "Div4",
     .tone = SubharmonicEngine::index(SubharmonicEngine::Tone::Div4),
     .ratio = 0.25,
     .fundamentals = std::array<float, 3>{440.0f, 220.0f, 110.0f}},
    {.name = "FifthBelow",
     .tone = SubharmonicEngine::index(SubharmonicEngine::Tone::FifthBelow),
     .ratio = 2.0 / 3.0,
     .fundamentals = std::array<float, 3>{220.0f, 110.0f, 55.0f}},
}};

/// SC-004's ceiling, in percent. NOT MOVABLE by any implementing agent
/// (FR-071's stop-and-surface rule, tasks.md:133-140). The derivation is plan
/// S4.2: the dominant term is the once-per-sub-period phase reset at
/// sub_oscillator.h:274-278, bounded at 1.44 % (Div2 @ 220 Hz), 1.44 %
/// (Div4 @ 440 Hz) and 1.92 % (Fifth @ 220 Hz); plus FR-041's always-in-circuit
/// tanh at ~0.042 % for A = 0.1; plus the helper's <= 0.001 % leakage floor.
/// If measurement disagrees the pre-authorised lever is D-4's rejected
/// alternative (a private PhaseAccumulator + std::sin for the Sine path),
/// NEVER a relaxed ceiling.
constexpr float kThdCeilingPercent = 2.0f;

// ==============================================================================
// SC-004 (b): unwinding the FR-042 blocker tilt
// ==============================================================================
// SC-004 (b) is asserted on the tilt-corrected figure, not on the raw one. The
// evidence and the spec-amendment request are in the header comment above
// SubharmonicEngine_DividerTHD - read that before touching either helper.
//
// The correction is against the FR-042 DC blocker ONLY, and that is deliberate:
// the chain is lowpass -> saturator -> trackGain -> blocker
// (subharmonic_engine.h:1248-1251), so every harmonic the memoryless shaper
// creates - which that notice's per-harmonic breakdown shows is ~100 % of what
// is measured - passes through exactly one linear filter before reaching the
// tap, the 18 Hz Bessel high-pass. The 2 kHz low-pass sits BEFORE the shaper,
// so none of those harmonics ever meets it; what does meet it is the tone
// itself (27.5-146.67 Hz across these sweeps, where a 2-pole at 2 kHz is flat
// to 1.5e-5) and the generator's own residual harmonics, which the same
// breakdown puts at <= 0.002 % in total.
// ==============================================================================

/// @brief Steady-state magnitude response of the FR-042 blocker at `freqHz`.
///
/// Measured on the shipped Krate::DSP::DCBlocker2 at the engine's own corner
/// (kInfrasonicFilterHz), NOT re-derived from a transcribed biquad formula: a
/// second copy of the coefficient maths would be a second thing to keep in
/// step with dc_blocker.h, and this correction is only sound if it is the
/// response of the filter that is actually in circuit.
///
/// Quadrature demodulation of output AGAINST input at the same frequency, so
/// the finite-window leakage that a bare RMS ratio would leave (~0.1 % at
/// 27.5 Hz) cancels in the quotient.
[[nodiscard]] double infrasonicBlockerGain(double freqHz) {
    if (!(freqHz > 0.0)) {
        return 1.0;
    }

    Krate::DSP::DCBlocker2 blocker;
    blocker.prepare(IsolatedSub::kSampleRate, SubharmonicEngine::kInfrasonicFilterHz);

    // 0.5 s of settling is ~57 time constants of the 18 Hz corner (tau = 8.8 ms).
    constexpr std::size_t kSettleSamples = 24000;
    constexpr std::size_t kMeasureSamples = 240000;
    const double omega = kSelfCheckTwoPi * freqHz / IsolatedSub::kSampleRate;

    for (std::size_t i = 0; i < kSettleSamples; ++i) {
        (void)blocker.process(static_cast<float>(std::sin(omega * static_cast<double>(i))));
    }

    double inCos = 0.0;
    double inSin = 0.0;
    double outCos = 0.0;
    double outSin = 0.0;
    for (std::size_t i = kSettleSamples; i < kSettleSamples + kMeasureSamples; ++i) {
        const double t = omega * static_cast<double>(i);
        const double c = std::cos(t);
        const double s = std::sin(t);
        const double x = s;
        const double y = static_cast<double>(blocker.process(static_cast<float>(x)));
        inCos += x * c;
        inSin += x * s;
        outCos += y * c;
        outSin += y * s;
    }

    const double inMag = std::sqrt(inCos * inCos + inSin * inSin);
    if (!(inMag > 0.0)) {
        return 1.0;
    }
    return std::sqrt(outCos * outCos + outSin * outSin) / inMag;
}

/// The same THD frame measured twice: as the criterion's helper measures it,
/// and with each harmonic's main-lobe power divided by the squared FR-042
/// magnitude at that harmonic's frequency.
struct ThdBreakdown {
    double rawPercent = -1.0;        ///< negative == the frame could not be analysed
    double correctedPercent = -1.0;  ///< negative == the frame could not be analysed
};

/// @brief Raw and FR-042-corrected THD of `tap`, from ONE magnitude spectrum.
///
/// The raw figure is computed by the same rule as
/// lfm::measureLowFrequencyThdPercent (same helper internals, same +/-2-bin
/// main lobes, same k = 2..maxHarmonic), and the case cross-checks the two
/// against each other so this local copy cannot silently drift into measuring
/// something else.
[[nodiscard]] ThdBreakdown measureThdBreakdown(const std::vector<float>& tap, double toneHz,
                                               int maxHarmonic) {
    ThdBreakdown out;
    if (tap.empty() || !(toneHz > 0.0) || maxHarmonic < 2) {
        return out;
    }

    std::vector<float> mags;
    if (!lfm::lowFreq::magnitudeSpectrum(tap.data(), tap.size(), mags)) {
        return out;
    }

    const double binHz = IsolatedSub::kSampleRate / static_cast<double>(tap.size());
    const double p1 = lfm::lowFreq::harmonicMainLobePower(mags, toneHz, binHz);
    if (!(p1 > 0.0)) {
        return out;
    }
    const double g1 = infrasonicBlockerGain(toneHz);
    if (!(g1 > 0.0)) {
        return out;
    }

    double rawPower = 0.0;
    double correctedPower = 0.0;
    for (int k = 2; k <= maxHarmonic; ++k) {
        const double harmonicHz = toneHz * static_cast<double>(k);
        const double pk = lfm::lowFreq::harmonicMainLobePower(mags, harmonicHz, binHz);
        const double gk = infrasonicBlockerGain(harmonicHz);
        rawPower += pk;
        correctedPower += (gk > 0.0) ? pk / (gk * gk) : pk;
    }

    out.rawPercent = 100.0 * std::sqrt(rawPower / p1);
    out.correctedPercent = 100.0 * std::sqrt(correctedPower / (p1 / (g1 * g1)));
    return out;
}

/// @brief Peak-to-peak spread of `values`, in percent of their mean.
[[nodiscard]] double spreadPercentOfMean(const std::array<double, 3>& values) {
    const double lo = *std::min_element(values.begin(), values.end());
    const double hi = *std::max_element(values.begin(), values.end());
    const double mean = (values[0] + values[1] + values[2]) / 3.0;
    if (!(mean > 0.0)) {
        return -1.0;
    }
    return 100.0 * (hi - lo) / mean;
}

/// SC-004 (b) ceiling on the spread of the FR-042-corrected THD within one
/// tone's sweep. MEASURED spreads are 0.325 % (Div2), 0.310 % (Div4) and
/// 0.344 % (FifthBelow), so this is ~12x margin over the widest of them (the
/// WARNs below reprint all three on every run). It is not a
/// round number chosen for comfort: the reproducibility floor of this
/// measurement is ~0.02 % (Div2 @ 220 Hz and Div4 @ 440 Hz render the SAME
/// 110 Hz tone through different slots and different masters, and their
/// corrected figures differ by 0.016 %), so 4 % is 200x the noise and still
/// 5x tighter than the smallest frequency-dependent defect this arm is meant
/// to see.
constexpr double kCorrectedSpreadCeilingPercent = 4.0;

/// The companion non-vacuity floor: the RAW spread must EXCEED this, i.e. the
/// correction must be doing real work. Measured raw spreads are 20.0 % (Div2),
/// 20.0 % (Div4) and 10.4 % (FifthBelow). Without this line, (b) would pass
/// just as happily against a correction that was accidentally the identity.
constexpr double kRawSpreadFloorPercent = 5.0;

}  // namespace

// ==============================================================================
// T006 - low_frequency_metrics.h self-validation (spectral half)
// ==============================================================================
// Plan S10.1: "Each helper is validated against a synthetic signal of known
// truth in the same TU that consumes it, so a helper bug fails as a helper bug."
// This case runs BEFORE any engine measurement in this TU: a peak-picker bug
// must fail as a peak-picker bug and not as an inharmonic-divider claim.
//
// The tolerances are not round numbers pulled from the air - they come from the
// Hann analysis worked in plan S10.1:
//   * log-parabolic (QIFFT) peak interpolation on a Hann-windowed sinusoid has
//     a residual bias of at most 0.016 bin = 0.0029 Hz in this frame, so 0.005 Hz
//     is a real bound with ~40 % margin rather than a rubber stamp;
//   * at 27.5 Hz the harmonic spacing is 150 bins, where the Hann sidelobe
//     envelope is far below -100 dB, so a pure tone measures ~1e-5 % THD against
//     the 0.01 % bound;
//   * the +/-kPeakExclusionBins window covers the Hann main lobe and its first
//     two sidelobes; the highest bin that survives outside it on a single 55 Hz
//     tone sits at -49.4 dB relative, 9.4 dB under the -40 dB threshold.
// ==============================================================================
TEST_CASE("SubharmonicEngine_LowFrequencyMetricsSelfCheck", "[subharmonic_engine]") {
    using namespace Krate::DSP::TestUtils;

    constexpr std::size_t kN = kLowFrequencyFftSize;
    const float sr = static_cast<float>(kSelfCheckSampleRate);
    const double binHz = kSelfCheckSampleRate / static_cast<double>(kN);

    // Plan S10.1's memory note: one shared FFT for the whole TU. Nothing below
    // may be trusted unless it actually prepared (fft.h:255).
    REQUIRE(lowFreq::analysisFft(kN).isPrepared());
    REQUIRE(lowFreq::analysisFft(kN).size() == kN);

    SECTION("estimatePeakFrequencyHz resolves synthetic tones to 0.005 Hz") {
        for (const double targetHz : {27.5, 36.67, 110.0}) {
            std::vector<float> tone(kN, 0.0f);
            addSine(tone, targetHz, 0.5);

            const float estimate = estimatePeakFrequencyHz(tone.data(), kN, sr);

            INFO("target " << targetHz << " Hz, estimate " << estimate
                           << " Hz, bin width " << binHz << " Hz");
            REQUIRE(estimate > 0.0f);
            REQUIRE(std::fabs(static_cast<double>(estimate) - targetHz) <= 0.005);
        }
    }

    SECTION("estimatePeakFrequencyHz returns the sentinel on silence") {
        const std::vector<float> silence(kN, 0.0f);
        REQUIRE(estimatePeakFrequencyHz(silence.data(), kN, sr) < 0.0f);
    }

    SECTION("measureLowFrequencyThdPercent reads ~0 on a pure 27.5 Hz sine") {
        std::vector<float> tone(kN, 0.0f);
        addSine(tone, 27.5, 0.5);

        const float thd = measureLowFrequencyThdPercent(tone.data(), kN, 27.5f, sr, 10);

        INFO("pure-tone THD = " << thd << " %");
        REQUIRE(thd >= 0.0f);
        REQUIRE(thd <= 0.01f);
    }

    SECTION("measureLowFrequencyThdPercent recovers a synthesised 5 % third harmonic") {
        // THD = sqrt(P3)/sqrt(P1) by construction: a third harmonic at 5 % of
        // the fundamental's amplitude and no other partial.
        std::vector<float> tone(kN, 0.0f);
        addSine(tone, 27.5, 0.5);
        addSine(tone, 82.5, 0.5 * 0.05);

        const float thd = measureLowFrequencyThdPercent(tone.data(), kN, 27.5f, sr, 10);

        INFO("third-harmonic reference THD = " << thd << " % (truth 5 %)");
        REQUIRE(thd > 0.0f);
        // Within 1 % RELATIVE of the known 5 %.
        REQUIRE(std::fabs(thd - 5.0f) <= 0.05f);
    }

    SECTION("measureLowFrequencyThdPercent returns a negative sentinel on silence") {
        const std::vector<float> silence(kN, 0.0f);
        const float thd = measureLowFrequencyThdPercent(silence.data(), kN, 27.5f, sr, 10);

        INFO("silent-frame THD = " << thd);
        // A silent tone must FAIL a criterion, not report a flattering 0 %.
        REQUIRE(thd < 0.0f);
    }

    SECTION("findSpectralPeaks reports one peak for one tone") {
        std::vector<float> tone(kN, 0.0f);
        addSine(tone, 55.0, 0.5);

        const std::vector<std::size_t> peaks = findSpectralPeaks(tone.data(), kN, -40.0f);

        INFO("peak count = " << peaks.size());
        REQUIRE(peaks.size() == 1u);
        const double peakHz = static_cast<double>(peaks[0]) * binHz;
        INFO("peak at bin " << peaks[0] << " = " << peakHz << " Hz");
        REQUIRE(std::fabs(peakHz - 55.0) <= binHz);
    }

    SECTION("findSpectralPeaks reports two peaks for two tones") {
        std::vector<float> tones(kN, 0.0f);
        addSine(tones, 55.0, 0.5);
        addSine(tones, 82.5, 0.5);

        const std::vector<std::size_t> peaks = findSpectralPeaks(tones.data(), kN, -40.0f);

        INFO("peak count = " << peaks.size());
        REQUIRE(peaks.size() == 2u);

        // Returned ascending by bin.
        const double firstHz = static_cast<double>(peaks[0]) * binHz;
        const double secondHz = static_cast<double>(peaks[1]) * binHz;
        INFO("peaks at " << firstHz << " Hz and " << secondHz << " Hz");
        REQUIRE(std::fabs(firstHz - 55.0) <= binHz);
        REQUIRE(std::fabs(secondHz - 82.5) <= binHz);
    }
}

// ==============================================================================
// T016 / SC-003 - The dividers are at the right frequencies
// ==============================================================================
// spec.md:935-960. The isolated-sub fixture, SubWaveform::Sine, one tone at a
// time, the per-tone sweeps above, estimatePeakFrequencyHz over 262 144 samples.
//
//   (a) the estimate is within +/-0.5 % of the tone's target, AND
//       getToneFrequencyHz(tone) agrees with the estimate within the same
//       tolerance;
//   (b) FifthBelow is within +/-2 cents of f * 2/3 at every swept fundamental -
//       the arm that proves FR-012's 4f/3-MASTER construction rather than
//       "some low tone appeared". The companion double-precision ratio check
//       (getToneFrequencyHz(2) / getToneFrequencyHz(0) against 4/3 to 1e-6)
//       lives in SubharmonicEngine_ToneMapping (T009); this arm is the AUDIO
//       half of the same claim, measured off the rendered spectrum.
//   (c) the helper is validated first, in this same TU, against synthetic
//       27.5 / 36.67 / 110 Hz sines to 0.005 Hz -
//       SubharmonicEngine_LowFrequencyMetricsSelfCheck, which PRECEDES this
//       case in declaration order (Catch2 default --order decl), so a
//       peak-picker bug fails as a peak-picker bug and never as a divider claim.
//
// FALSIFICATION (tasks.md T016): this case IS its own falsification. The two
// quantities compared in (a) are computed by entirely different routes -
// getToneFrequencyHz derives from the master PhaseAccumulator increments
// (plan S2.3), while the estimate comes from a Hann-windowed 262 144-point
// spectrum of rendered audio. They can only agree if the rendered pitch and the
// read surface are the same pitch.
// ==============================================================================
TEST_CASE("SubharmonicEngine_DividerFrequencyAccuracy", "[subharmonic_engine]") {
    constexpr std::size_t kN = lfm::kLowFrequencyFftSize;
    const float sr = static_cast<float>(IsolatedSub::kSampleRate);

    // Nothing below may be trusted unless the shared FFT actually prepared
    // (fft.h:255).
    REQUIRE(lfm::lowFreq::analysisFft(kN).isPrepared());

    constexpr double kRelativeTolerance = 0.005;  // +/-0.5 %
    constexpr double kCentTolerance = 2.0;        // +/-2 cents, arm (b)

    for (const ToneSweep& sweep : kToneSweeps) {
        for (const float fundamentalHz : sweep.fundamentals) {
            const double targetHz = sweep.ratio * static_cast<double>(fundamentalHz);

            IsolatedSub fixture(sweep.tone, fundamentalHz, SubWaveform::Sine);
            const std::vector<float> tap = fixture.renderTap(kN);

            const std::string label = std::string(sweep.name) + " @ f = " +
                                      std::to_string(fundamentalHz) + " Hz (target " +
                                      std::to_string(targetHz) + " Hz)";
            requireTapAboveGate(tap, label);

            const float estimate = lfm::estimatePeakFrequencyHz(tap.data(), kN, sr);
            const float readSurface = fixture.engine.getToneFrequencyHz(sweep.tone);

            INFO(label << ": estimate = " << estimate
                       << " Hz, getToneFrequencyHz = " << readSurface << " Hz");

            // A negative sentinel means the frame carried no usable signal.
            REQUIRE(estimate > 0.0f);

            // (a) the rendered pitch is the specified pitch ...
            const double estimated = static_cast<double>(estimate);
            REQUIRE(std::fabs(estimated - targetHz) <= kRelativeTolerance * targetHz);

            // ... and the read surface reports the pitch that was rendered.
            REQUIRE(std::fabs(static_cast<double>(readSurface) - estimated) <=
                    kRelativeTolerance * targetHz);

            // (b) the fifth is EXACT, not merely low.
            if (sweep.tone == SubharmonicEngine::index(SubharmonicEngine::Tone::FifthBelow)) {
                const double exactFifthHz = static_cast<double>(fundamentalHz) * 2.0 / 3.0;
                const double cents = 1200.0 * std::log2(estimated / exactFifthHz);
                INFO(label << ": fifth error = " << cents << " cents");
                REQUIRE(std::fabs(cents) <= kCentTolerance);
            }
        }
    }
}

// ==============================================================================
// T016 / SC-004 - Harmonic purity of the dividers
// ==============================================================================
// spec.md:975-1034. Same fixture, same per-tone sweeps, measured with
// measureLowFrequencyThdPercent(..., maxHarmonic = 10) - NOT
// signal_metrics.h:111 calculateTHD, whose 8192-point cap gives 5.86 Hz bins at
// 48 kHz, whose +/-2-bin harmonic windows overlap at these frequencies (so what
// it returns is Hann leakage, first sidelobe -31 dB ~ 2.8 %, i.e. ABOVE the
// ceiling asserted here), and which returns a flattering 0.0f on a silent
// fundamental.
//
//   (a) THD <= 2.0 % for every tone at every fundamental, every value
//       transcribed into the compliance record through the WARN below. A
//       NEGATIVE SENTINEL FAILS the case - a silent tone must not report 0 %.
//       UNCHANGED, and the ceiling is UNTOUCHED: the worst measured point is
//       0.1024 % against the 2.0 % line, 24x of margin.
//
//   (b) *** SPEC AMENDED 2026-09-14 (spec.md D-15). THIS IS NOW WHAT SC-004 (b)
//       SAYS - the assertion and the criterion agree. READ THIS BEFORE TOUCHING
//       THE ASSERTION - IT IS NOT A RELAXED TEST. ***
//
//       An EARLIER spec.md SC-004 (b) and plan S4.2 required THD to be
//       MONOTONICALLY NON-INCREASING as each tone own fundamental sweep
//       descends, "the signature of the phase-reset mechanism", on the premise
//       that the once-per-sub-period phase reset (sub_oscillator.h:274-278)
//       DOMINATES the measurement and is proportional to masterInc, i.e. to f.
//       That premise is false on a correct build; the measurement below is why
//       the criterion was amended to the flatness form this case asserts.
//
//       MEASURED, 48 kHz, this fixture, 262 144-point frame, k = 2..10
//       (dsp_systems_tests.exe, Windows/MSVC Release; every figure is
//       reproduced by the WARNs below on every run):
//
//         tone        f (Hz)  tone (Hz)  raw THD %   FR-042 |H| at tone  corrected %
//         Div2        220     110.00     0.0841637   0.987354            0.0832151
//         Div2        110      55.00     0.0871336   0.948174            0.0830821
//         Div2         55      27.50     0.1024160   0.791537            0.0829454
//         Div4        440     110.00     0.0841772   0.987354            0.0832284
//         Div4        220      55.00     0.0871205   0.948174            0.0830696
//         Div4        110      27.50     0.1024470   0.791537            0.0829706
//         FifthBelow  220     146.67     0.0837550   0.992890            0.0832246
//         FifthBelow  110      73.33     0.0854655   0.971062            0.0832534
//         FifthBelow   55      36.67     0.0928611   0.881341            0.0828906
//
//       THE RAW FIGURE RISES AS f DESCENDS, on ALL THREE tones - so SC-004 (b)
//       as written fails on every sweep, not only on the one that aborts first.
//       Three measured facts say why, and none of them is an engine defect:
//
//       1. THE MEASUREMENT IS ~100 % THIRD HARMONIC. Per-harmonic breakdown at
//          Div2 @ 220 Hz: H3 = 0.084135 %, while H2 and H4..H10 are
//          0.00075-0.00085 % EACH. That is the signature of a MEMORYLESS ODD
//          nonlinearity - FR-041 always-in-circuit Sigmoid::tanh at A = 0.1
//          (saturation_processor.h:342-346 -> FastMath::fastTanh) - whose
//          relative harmonic content does not depend on frequency at all. A
//          once-per-period phase-reset impulse would instead spread energy
//          across the whole series (~1/k).
//       2. THE PHASE-RESET TERM IS ~200x SMALLER THAN plan S4.2 BOUND. Backing
//          the flat shaper floor out in quadrature leaves ~0.007 % at Div2 @
//          220 Hz, against S4.2 1.44 %. S4.2 priced the WORST-CASE phase step
//          as if it landed in the harmonics being summed; in fact the residual
//          at the reset instant is a fraction of masterInc/octaveFactor, the
//          jump happens AT the sine zero crossing (it is phase jitter, not an
//          amplitude step), and its energy spreads over the whole band while
//          THD counts only k = 2..10.
//       3. WHAT REMAINS IS THE FR-042 TILT, WHICH THE SPEC ALREADY KNEW WORKED
//          AGAINST (b). The 18 Hz Bessel high-pass attenuates a 27.5 Hz
//          fundamental by 0.7915 while passing its 82.5 Hz third harmonic at
//          0.9773 - a 1.234x inflation of the RATIO - and only 1.011x at
//          110 Hz. Measured raw/corrected quotients: 1.0114 / 1.0488 / 1.2347
//          (Div2), matching the measured filter response to better than 0.1 %.
//          The spec assumed the phase-reset term would dominate this tilt;
//          measured, the tilt is the ONLY frequency-dependent term left.
//
//       THE PRE-AUTHORISED LEVER DOES NOT APPLY AND WOULD NOT HELP. spec.md and
//       tasks.md pre-authorise D-4 rejected alternative (a private
//       PhaseAccumulator + std::sin for the Sine path) if SC-004 misses. That
//       lever REMOVES the phase reset - i.e. it removes term 2, the only term
//       that trends with f at all - leaving a perfectly flat shaper floor under
//       an unchanged FR-042 tilt. It makes (b) MORE false, not less. Nothing an
//       implementation can do makes the raw figure non-increasing except making
//       the dividers dirtier at high f on purpose.
//
//       WHAT IS ASSERTED INSTEAD, and why it still has teeth: the FR-042 tilt
//       is divided out per harmonic using the response of the shipped
//       DCBlocker2 at the engine own corner, and the corrected THD must be FLAT
//       across each tone sweep (spread <= 4 % of the sweep mean; measured
//       0.32 / 0.31 / 0.44 %). That is the same claim (b) was written to make -
//       the dividers contribute no frequency-dependent distortion of their own,
//       so a coefficient bug cannot hide - stated against the mechanism that is
//       actually measurable. It is NOT weaker than the raw monotonicity check:
//       raw monotonicity would tolerate a 20 % frequency-dependent excursion as
//       long as it pointed downhill, and this does not. The companion assertion
//       (raw spread >= 5 %) keeps the correction load-bearing, so the arm cannot
//       pass by the correction accidentally becoming the identity.
//
//       The corrected figures are non-increasing on Div2 and Div4 and rise by
//       0.035 % at FifthBelow middle point, i.e. the direction (b) predicts is
//       visible where it survives at all but sits at the measurement ~0.02 %
//       reproducibility floor (Div2 @ 220 Hz and Div4 @ 440 Hz render the same
//       110 Hz tone by different routes and differ by 0.016 %). It cannot be
//       asserted, and it is NOT asserted here.
//
//       AMENDMENT LANDED 2026-09-14. spec.md SC-004 (b) now states the
//       flatness-after-FR-042 criterion asserted below (spread <= 4 % of the
//       sweep mean, raw spread >= 5 % as the non-vacuity floor) and records the
//       reasoning as decision D-15; plan S4.2 table is marked as a worst-case
//       bound on the phase STEP rather than a prediction of measured THD, and
//       tasks.md T016 carries the same wording. SC-004 (a) 2 % ceiling is
//       UNTOUCHED. If this comment and spec.md ever disagree, spec.md wins.
// ==============================================================================
TEST_CASE("SubharmonicEngine_DividerTHD", "[subharmonic_engine]") {
    constexpr std::size_t kN = lfm::kLowFrequencyFftSize;
    const float sr = static_cast<float>(IsolatedSub::kSampleRate);
    constexpr int kMaxHarmonic = 10;

    REQUIRE(lfm::lowFreq::analysisFft(kN).isPrepared());

    for (const ToneSweep& sweep : kToneSweeps) {
        std::array<double, 3> rawThd{0.0, 0.0, 0.0};
        std::array<double, 3> correctedThd{0.0, 0.0, 0.0};

        for (std::size_t s = 0; s < sweep.fundamentals.size(); ++s) {
            const float fundamentalHz = sweep.fundamentals[s];
            const double targetHz = sweep.ratio * static_cast<double>(fundamentalHz);

            IsolatedSub fixture(sweep.tone, fundamentalHz, SubWaveform::Sine);
            const std::vector<float> tap = fixture.renderTap(kN);

            const std::string label = std::string(sweep.name) + " @ f = " +
                                      std::to_string(fundamentalHz) + " Hz (tone " +
                                      std::to_string(targetHz) + " Hz)";
            requireTapAboveGate(tap, label);

            const float thd = lfm::measureLowFrequencyThdPercent(
                tap.data(), kN, static_cast<float>(targetHz), sr, kMaxHarmonic);

            // Transcribed into compliance: SC-004 (a) requires every measured
            // value on the record, not just the verdict.
            WARN("SC-004 " << label << ": THD = " << thd << " %");
            INFO(label << ": THD = " << thd << " % (ceiling " << kThdCeilingPercent << " %)");

            // The negative sentinel means the fundamental main-lobe power was
            // below -60 dBFS. That FAILS - it is not a 0 % result.
            REQUIRE(thd >= 0.0f);
            REQUIRE(thd <= kThdCeilingPercent);

            const ThdBreakdown breakdown = measureThdBreakdown(tap, targetHz, kMaxHarmonic);
            REQUIRE(breakdown.rawPercent >= 0.0);
            REQUIRE(breakdown.correctedPercent >= 0.0);

            // The local breakdown must BE the criterion own measurement, not a
            // second opinion: if these ever diverge, (b) is measuring something
            // SC-004 (a) is not, and the arm is void.
            INFO(label << ": helper THD = " << thd
                       << " %, breakdown raw = " << breakdown.rawPercent << " %");
            REQUIRE(std::fabs(breakdown.rawPercent - static_cast<double>(thd)) <=
                    1.0e-3 * static_cast<double>(thd));

            WARN("SC-004 (b) " << label << ": raw = " << breakdown.rawPercent
                               << " %, FR-042-corrected = " << breakdown.correctedPercent << " %");

            rawThd[s] = breakdown.rawPercent;
            correctedThd[s] = breakdown.correctedPercent;
        }

        // (b) within this tone own sweep only - see the deviation notice above.
        const double correctedSpread = spreadPercentOfMean(correctedThd);
        const double rawSpread = spreadPercentOfMean(rawThd);

        WARN("SC-004 (b) " << sweep.name << ": corrected spread = " << correctedSpread
                           << " % of mean (ceiling " << kCorrectedSpreadCeilingPercent
                           << " %), raw spread = " << rawSpread << " % (floor "
                           << kRawSpreadFloorPercent << " %)");

        INFO(sweep.name << ": FR-042-corrected THD = " << correctedThd[0] << " / "
                        << correctedThd[1] << " / " << correctedThd[2] << " %, spread "
                        << correctedSpread << " % of mean");
        REQUIRE(correctedSpread >= 0.0);
        REQUIRE(correctedSpread <= kCorrectedSpreadCeilingPercent);

        // Non-vacuity: the correction must be load-bearing. The RAW figures span
        // far more than the corrected ones do, so this arm cannot pass by the
        // correction quietly becoming the identity.
        INFO(sweep.name << ": raw THD = " << rawThd[0] << " / " << rawThd[1] << " / " << rawThd[2]
                        << " %, spread " << rawSpread << " % of mean");
        REQUIRE(rawSpread >= kRawSpreadFloorPercent);
    }
}

// ==============================================================================
// T016 / SC-004 FALSIFICATION - the 2 % ceiling is not vacuous
// ==============================================================================
// tasks.md T016: "for SC-004, run one arm with setDriveDb(kMaxDriveDb) and
// confirm the THD ceiling is breached, then restore the pin."
//
// Carried as a HIDDEN case ([.falsification]) rather than as a temporary source
// mutation, so the pin is restored BY CONSTRUCTION - the criterion case above
// never leaves kMinDriveDb - and the falsification stays reproducible by name
// instead of being a one-off edit nobody can re-run:
//
//   dsp_systems_tests.exe "SubharmonicEngine_DividerTHD_DriveFalsification"
//
// Hidden tags are excluded from an unfiltered suite run, so this case adds
// nothing to CI time and cannot turn the per-push lane red.
//
// THE ARM IS Div2 @ f = 220 Hz, AND THE INJECTION IS TWO LEVERS, NOT ONE.
// T016 wrote the falsification as drive alone, and drive alone MEASURES 1.28507 %
// against the 2 % line - a 15x response, but SHORT of the breach. T016's own
// note pre-authorised the fix: "that is NOT an SC-004 defect and the ceiling
// must not move; it means the drive lever alone is too weak a falsifier for
// this arm and a stronger injection (e.g. raising the fixture tone level on the
// falsification arm only) is the correct fix." So the driven arm also raises the
// tone level from the fixture's -20 dBFS to -12 dBFS. Nothing about SC-004's
// ceiling, its fixture or its pinned arm changes: kThdCeilingPercent is
// untouched at 2.0 %, and the criterion case above still renders at
// kMinDriveDb / -20 dBFS.
//
// The two levers are DERIVED, not tuned. Numerical evaluation of the shaped
// tone tanh(A sin) over 65 536 points, THD summed over k = 2..10, against the
// measured values from this file:
//
//   tone level   A = level x dbToGain(+12)   predicted THD   measured THD
//   -20 dBFS     0.398                        1.271 %         1.28507 %
//   -12 dBFS     1.000                        6.706 %         (this arm)
//
// The predicted/measured agreement at -20 dBFS is 0.9 %, which is itself worth
// recording: it is independent confirmation that what SC-004 measures at the
// pinned fixture is FR-041's shaper and not the dividers (see the deviation
// notice above SubharmonicEngine_DividerTHD).
//
// Three assertions, in increasing order of strength:
//
//   1. THD STRICTLY INCREASES with the injection on the identical arm. This is
//      the falsification that cannot fail for an uninteresting reason: it proves
//      the measurement RESPONDS to a known, deliberately injected distortion,
//      so a green SC-004 is a statement about the dividers and not about a
//      metric stuck at a constant.
//   2. Drive ALONE already moves the figure by more than 10x. Recorded so the
//      "too weak a falsifier" finding above cannot silently rot into "the drive
//      lever does nothing".
//   3. The ceiling itself is breached by the two-lever arm. This is the
//      falsification as tasks.md words it. If arithmetic and measurement
//      disagree here the correct response is to SURFACE the measured pair,
//      never to move SC-004's line.
// ==============================================================================
TEST_CASE("SubharmonicEngine_DividerTHD_DriveFalsification",
          "[subharmonic_engine][.falsification]") {
    constexpr std::size_t kN = lfm::kLowFrequencyFftSize;
    const float sr = static_cast<float>(IsolatedSub::kSampleRate);
    constexpr float kFundamentalHz = 220.0f;
    constexpr float kToneHz = 110.0f;  // Div2 of 220 Hz
    constexpr std::size_t kDiv2 = SubharmonicEngine::index(SubharmonicEngine::Tone::Div2);

    /// The second lever. See the derivation table above: at kMaxDriveDb this
    /// puts A = 1.0 into the shaper, predicted THD 6.706 %.
    constexpr float kDrivenToneLevelDb = -12.0f;

    REQUIRE(lfm::lowFreq::analysisFft(kN).isPrepared());

    IsolatedSub pinned(kDiv2, kFundamentalHz, SubWaveform::Sine);
    const std::vector<float> pinnedTap = pinned.renderTap(kN);
    requireTapAboveGate(pinnedTap, "Div2 @ 220 Hz, drive kMinDriveDb");
    const float thdPinned =
        lfm::measureLowFrequencyThdPercent(pinnedTap.data(), kN, kToneHz, sr, 10);

    // Lever 1 alone: max drive at the fixture's own -20 dBFS tone level.
    IsolatedSub driveOnly(kDiv2, kFundamentalHz, SubWaveform::Sine);
    driveOnly.engine.setDriveDb(SubharmonicEngine::kMaxDriveDb);
    const std::vector<float> driveOnlyTap = driveOnly.renderTap(kN);
    requireTapAboveGate(driveOnlyTap, "Div2 @ 220 Hz, drive kMaxDriveDb");
    const float thdDriveOnly =
        lfm::measureLowFrequencyThdPercent(driveOnlyTap.data(), kN, kToneHz, sr, 10);

    // Levers 1 + 2: max drive AND a -12 dBFS tone.
    IsolatedSub driven(kDiv2, kFundamentalHz, SubWaveform::Sine);
    driven.engine.setDriveDb(SubharmonicEngine::kMaxDriveDb);
    driven.engine.setToneLevelDb(kDiv2, kDrivenToneLevelDb);
    const std::vector<float> drivenTap = driven.renderTap(kN);
    requireTapAboveGate(drivenTap, "Div2 @ 220 Hz, drive kMaxDriveDb, tone -12 dBFS");
    const float thdDriven =
        lfm::measureLowFrequencyThdPercent(drivenTap.data(), kN, kToneHz, sr, 10);

    WARN("SC-004 falsification: THD at kMinDriveDb = "
         << thdPinned << " %, at kMaxDriveDb = " << thdDriveOnly
         << " %, at kMaxDriveDb + " << kDrivenToneLevelDb << " dBFS tone = " << thdDriven
         << " % (ceiling " << kThdCeilingPercent << " %)");

    REQUIRE(thdPinned >= 0.0f);
    REQUIRE(thdDriveOnly >= 0.0f);
    REQUIRE(thdDriven >= 0.0f);

    INFO("the metric must RESPOND to injected distortion: " << thdPinned << " % -> " << thdDriven
                                                            << " %");
    REQUIRE(thdDriven > thdPinned);

    INFO("drive alone must move the figure by >10x: " << thdPinned << " % -> " << thdDriveOnly
                                                      << " %");
    REQUIRE(thdDriveOnly > 10.0f * thdPinned);

    INFO("the 2 % ceiling must be BREACHED by the two-lever arm: " << thdDriven << " %");
    REQUIRE(thdDriven > kThdCeilingPercent);
}

// ==============================================================================
// T017 - SC-002's sweep fixture (spec.md:913-932, plan S10.3 / plan.md:1395)
// ==============================================================================
// SC-002 reuses the IsolatedSub CONFIGURATION but not the IsolatedSub object,
// and the differences are all stated by the criterion itself, so they are coded
// here rather than bent into the shared fixture (SC-003/004/005 depend on that
// fixture exactly as it stands):
//
//   * tracking is 1.0, not 0.0 - it is the quantity under test;
//   * all THREE tones are at their FR-020 defaults (-18 / -24 / -30 dB), not one
//     tone at -20 dBFS, and every breath depth is 0 so the level is stationary;
//   * the fundamental is the FR-013 default 55 Hz (Div2 = 27.5, Div4 = 13.75,
//     FifthBelow = 36.67 Hz - all clear of FR-016's kMinToneHz = 12 backstop);
//   * the input is NOT silent: it is a mono-identical 55 Hz sine body, 8 s per
//     step, of which the last 4 s are measured;
//   * THE GATE IS DIFFERENT. See the anchor-gate note on requireAnchorGate.
//
// WHY A FRESH ENGINE PER STEP: each step is an independent measurement of the
// steady state, so the follower must not carry the previous (louder or quieter)
// body across the step edge. 8 s of hold against a 120 ms attack / 800 ms
// release follower - tau = 19.1 ms / 127.3 ms, since those figures are ~99 %
// settling times and not time constants (spec.md:137-141) - is >30 release time
// constants before the 4 s measurement window even opens.
//
// WHY THE BODY RMS IS MEASURED AND NOT ASSUMED (plan.md:1395, "Fixture note"):
// the follower is in RMS mode (FR-030), so a sine of peak A reads A/sqrt(2), and
// the sweep is specified in body RMS. Each step therefore synthesises
// A = sqrt(2) * dbToGain(target) and then RE-MEASURES the rendered buffer's
// actual RMS, which is the x-axis every arm below is computed against. No dBFS
// convention is assumed anywhere.
// ==============================================================================

namespace {

constexpr double kTrackingSampleRate = IsolatedSub::kSampleRate;
constexpr std::size_t kTrackingBlockSamples = IsolatedSub::kBlockSamples;

/// 8 s per step, of which the last 4 s are measured (spec.md:917-918).
constexpr std::size_t kTrackingStepSamples = 384000;
constexpr std::size_t kTrackingMeasureSamples = 192000;
constexpr std::size_t kTrackingMeasureStart = kTrackingStepSamples - kTrackingMeasureSamples;

// Both windows are whole blocks, so the measurement window opens exactly on a
// block boundary and the streaming accumulation below needs no partial-block
// arithmetic.
static_assert(kTrackingStepSamples % kTrackingBlockSamples == 0,
              "the step must be a whole number of blocks");
static_assert(kTrackingMeasureSamples % kTrackingBlockSamples == 0,
              "the measurement window must be a whole number of blocks");

/// The swept body RMS levels, in dBFS (spec.md:919).
constexpr std::size_t kSweepPoints = 7;
constexpr std::array<double, kSweepPoints> kBodySweepDb{-60.0, -48.0, -36.0, -24.0,
                                                        -18.0, -12.0, -6.0};

/// The -18 dBFS step: the bottom of arm (b)'s flat region (the reference).
constexpr std::size_t kKneeIndex = 4;
static_assert(kBodySweepDb[kKneeIndex] == -18.0, "kKneeIndex must name the -18 dBFS step");

/// The -24 dBFS step: the top of arm (a)'s rising region. One step below the
/// reference, not at it: EnvelopeFollower in RMS mode smooths asymmetrically in
/// the squared domain (envelope_follower.h:309-325, attack 120 ms / release
/// 800 ms) and reads a steady sine ~1.9 dB above its true RMS (measured 1.8788
/// dB here, simulated 1.8783 dB), so the FR-032 knee sits at reference - 1.9 dB
/// and the -18 dBFS point is already on the plateau. Ruled 2026-09-13.
constexpr std::size_t kRisingTopIndex = 3;
static_assert(kBodySweepDb[kRisingTopIndex] == -24.0, "kRisingTopIndex must name the -24 dBFS step");

/// SC-002's tolerances, verbatim from spec.md:922-931. NOT MOVABLE by any
/// implementing agent (FR-071's stop-and-surface rule, tasks.md:133-140): if a
/// figure misses, the measured table below is surfaced for a user ruling.
constexpr double kUnitySlopeToleranceDb = 1.0;     // (a)
constexpr double kFlatToleranceDb = 0.5;           // (b), (c)
constexpr double kTrackingGainTolerance = 1.0e-4;  // (d) absolute floor
/// (d) relative term. EnvelopeFollower's RMS output carries a ~1.5 % p-p ripple
/// at 2f on a steady 55 Hz body; the 50 ms tracking ramp averages it out while
/// getTrackedEnvelope() reads it instantaneously, so 1e-4 absolute fails at
/// every rising point above -60 dBFS on a correct build (measured 1.8e-4 at
/// -48 dBFS). 2 % is ~2.5x the ripple half-swing and still 5x below the
/// smallest miss a mis-wired getter produces. Ruled 2026-09-13.
constexpr double kTrackingGainRelativeTolerance = 0.02;
constexpr double kKneeToleranceDb = 1.0;           // (e)
/// (e) the knee sits BELOW the reference by the sensor's steady-sine over-read
/// (see kRisingTopIndex): measured -1.8788 dB at the default reference and
/// -1.8791 dB at -30, simulated -1.8783 dB. Recorded in spec.md SC-002 (a)/(e).
constexpr double kSensorSteadySineBiasDb = -1.88;
/// (e) the bias-independent statement: moving the reference by -12 dB moves
/// the knee by -12 dB.
constexpr double kReferenceMoveDb = -12.0;

/// Arm (c) doubles as arm (a)'s falsification (see the case comment): at
/// tracking 0 the sub does not follow the body at all, so the same slope
/// statistic arm (a) asserts must miss unity by the full sweep span. 20 dB is
/// half of the 42 dB the -60 -> -18 leg spans, i.e. this cannot pass by noise.
constexpr double kSlopeFalsificationMarginDb = 20.0;

/// SC-002's TWO-PART ANCHOR GATE (plan.md:1372-1378), which REPLACES the
/// IsolatedSub -60 dBFS floor. The floor is unconditional in the spec text
/// (spec.md:897-899) and applied that way it fails a CORRECT implementation
/// here: at the -60 dBFS body, envNorm = 0.001 / 0.12589 = -42.0 dB and the tap
/// reads -22.5 - 42.0 = -64.5 dBFS. Suppressing that step is not an option -
/// it is the bottom of the unity-slope region arm (a) exists to measure.
constexpr double kAnchorTopGateDb = -40.0;  // "the engine is alive", ~17 dB of margin
constexpr double kAnchorFloorDb = -75.0;    // absolute sanity floor, ~10 dB under -64.5

/// One swept step: the body level actually rendered, the tap it produced, and
/// the two read-surface values sampled at the end of the step (arm (d)).
struct TrackingPoint {
    double bodyRmsDb = 0.0;
    double tapRmsDb = 0.0;
    double trackingGain = 0.0;
    double trackedEnvelope = 0.0;
};

using TrackingSweep = std::array<TrackingPoint, kSweepPoints>;

/// @brief Render one 8 s step and return its measured figures.
///
/// Streaming: one 512-sample block of body / out / tap is live at any moment,
/// and the RMS accumulators are running sums - a 4 s stereo window is never
/// materialised.
[[nodiscard]] TrackingPoint runTrackingStep(double bodyRmsDbTarget, float trackingAmount,
                                            float trackReferenceDb) {
    SubharmonicEngine engine;
    engine.prepare(kTrackingSampleRate,
                   SubharmonicEngine::PrepareConfig{.maxBlockSamples = kTrackingBlockSamples});

    engine.setTrackingAmount(trackingAmount);
    engine.setTrackReferenceDb(trackReferenceDb);
    engine.setWetGainDb(0.0f);
    engine.setLowpassCutoffHz(SubharmonicEngine::kMaxLowpassHz);
    engine.setDriveDb(SubharmonicEngine::kMinDriveDb);
    engine.setFundamentalHz(SubharmonicEngine::kDefaultFundamentalHz);

    // All three tones at their FR-020 defaults - written explicitly rather than
    // left implicit, so the case states its own configuration and a change to
    // the shipped defaults shows up here as a diff rather than as a silent
    // change of what SC-002 measures.
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        engine.setToneLevelDb(t, SubharmonicEngine::kDefaultToneLevelDb[t]);
        engine.setToneBreathDepth(t, 0.0f);
    }

    // RMS -> peak for a sine. The rendered buffer is re-measured below; this is
    // only the drive level.
    const double amplitude =
        std::sqrt(2.0) *
        static_cast<double>(Krate::DSP::dbToGain(static_cast<float>(bodyRmsDbTarget)));
    const double omega = kSelfCheckTwoPi *
                         static_cast<double>(SubharmonicEngine::kDefaultFundamentalHz) /
                         kTrackingSampleRate;

    std::vector<float> body(kTrackingBlockSamples, 0.0f);
    std::vector<float> outL(kTrackingBlockSamples, 0.0f);
    std::vector<float> outR(kTrackingBlockSamples, 0.0f);
    std::vector<float> tap(kTrackingBlockSamples, 0.0f);

    double bodySumSq = 0.0;
    double tapSumSq = 0.0;
    std::size_t measured = 0;

    for (std::size_t done = 0; done < kTrackingStepSamples; done += kTrackingBlockSamples) {
        for (std::size_t i = 0; i < kTrackingBlockSamples; ++i) {
            body[i] =
                static_cast<float>(amplitude * std::sin(omega * static_cast<double>(done + i)));
        }

        // Mono-identical: the same buffer on both channels, so the engine's
        // 0.5 * (xl + xr) sensor input IS the body.
        engine.processBlockTapped(body.data(), body.data(), outL.data(), outR.data(), tap.data(),
                                  kTrackingBlockSamples);

        if (done >= kTrackingMeasureStart) {
            for (std::size_t i = 0; i < kTrackingBlockSamples; ++i) {
                const double b = static_cast<double>(body[i]);
                const double y = static_cast<double>(tap[i]);
                bodySumSq += b * b;
                tapSumSq += y * y;
            }
            measured += kTrackingBlockSamples;
        }
    }

    TrackingPoint point;
    if (measured == 0) {
        return point;  // unreachable: the static_asserts above make the window whole
    }
    const double bodyRms = std::sqrt(bodySumSq / static_cast<double>(measured));
    const double tapRms = std::sqrt(tapSumSq / static_cast<double>(measured));
    point.bodyRmsDb = static_cast<double>(Krate::DSP::gainToDb(static_cast<float>(bodyRms)));
    point.tapRmsDb = static_cast<double>(Krate::DSP::gainToDb(static_cast<float>(tapRms)));
    point.trackingGain = static_cast<double>(engine.getTrackingGain());
    point.trackedEnvelope = static_cast<double>(engine.getTrackedEnvelope());
    return point;
}

/// @brief Run the whole seven-point sweep, transcribing every point.
///
/// Every measured point goes on the record through the WARN (spec.md:919-921,
/// "every measured point is transcribed into compliance"), not just the verdict.
[[nodiscard]] TrackingSweep runTrackingSweep(float trackingAmount, float trackReferenceDb,
                                             const char* label) {
    TrackingSweep sweep{};
    for (std::size_t i = 0; i < kSweepPoints; ++i) {
        sweep[i] = runTrackingStep(kBodySweepDb[i], trackingAmount, trackReferenceDb);
        WARN("SC-002 [" << label << "] body " << kBodySweepDb[i] << " dBFS (rendered "
                        << sweep[i].bodyRmsDb << " dBFS) -> subTap " << sweep[i].tapRmsDb
                        << " dBFS, trackGain " << sweep[i].trackingGain << ", envNorm "
                        << sweep[i].trackedEnvelope);
    }
    return sweep;
}

/// The two-part anchor gate. See kAnchorTopGateDb above for why the shared
/// IsolatedSub floor cannot be used here.
void requireAnchorGate(const TrackingSweep& sweep, const char* label) {
    INFO(label << ": top-of-sweep subTap = " << sweep[kSweepPoints - 1].tapRmsDb
               << " dBFS (anchor " << kAnchorTopGateDb << " dBFS)");
    REQUIRE(sweep[kSweepPoints - 1].tapRmsDb > kAnchorTopGateDb);

    for (std::size_t i = 0; i + 1 < kSweepPoints; ++i) {
        INFO(label << ": body " << kBodySweepDb[i] << " dBFS -> subTap " << sweep[i].tapRmsDb
                   << " dBFS (floor " << kAnchorFloorDb << " dBFS)");
        REQUIRE(sweep[i].tapRmsDb > kAnchorFloorDb);
    }
}

/// @brief The body level, in dBFS, at which the rising region meets the plateau.
///
/// In the rising region the law is tapDb = bodyDb + offset; above the knee it is
/// tapDb = plateauDb. The two lines meet at bodyDb = plateauDb - offset, so the
/// knee is read off the sweep without fitting anything. The offset is taken from
/// the two LOWEST steps (-60 and -48 dBFS), which are below both references
/// under test (-18 and -30) with >= 16 dB to spare.
[[nodiscard]] double estimateKneeDb(const TrackingSweep& sweep) {
    const double offset = 0.5 * ((sweep[0].tapRmsDb - sweep[0].bodyRmsDb) +
                                 (sweep[1].tapRmsDb - sweep[1].bodyRmsDb));
    return sweep[kSweepPoints - 1].tapRmsDb - offset;
}

/// Arm (d): the audio measurement and the read surface must corroborate each
/// other rather than either standing alone (spec.md:926-928).
void requireReadSurfaceAgrees(const TrackingSweep& sweep, double trackingAmount,
                              const char* label) {
    for (std::size_t i = 0; i < kSweepPoints; ++i) {
        const double predicted =
            (1.0 - trackingAmount) + trackingAmount * sweep[i].trackedEnvelope;
        const double tolerance =
            std::max(kTrackingGainTolerance, kTrackingGainRelativeTolerance * predicted);
        INFO(label << " (d) at body " << kBodySweepDb[i] << " dBFS: getTrackingGain() = "
                   << sweep[i].trackingGain << ", (1-a) + a*getTrackedEnvelope() = " << predicted
                   << " (tolerance " << tolerance << ")");
        REQUIRE(std::fabs(sweep[i].trackingGain - predicted) <= tolerance);
    }
}

/// Mean of `count` tap readings starting at `first`.
[[nodiscard]] double meanTapDb(const TrackingSweep& sweep, std::size_t first, std::size_t count) {
    double sum = 0.0;
    for (std::size_t i = first; i < first + count; ++i) {
        sum += sweep[i].tapRmsDb;
    }
    return sum / static_cast<double>(count);
}

/// Every tap reading in [first, first + count) is within `toleranceDb` of their
/// common mean - the "flat within +/- x dB" form arms (b), (c) and (e2) share.
void requireFlatRegion(const TrackingSweep& sweep, std::size_t first, std::size_t count,
                       double toleranceDb, const char* label) {
    const double mean = meanTapDb(sweep, first, count);
    for (std::size_t i = first; i < first + count; ++i) {
        INFO(label << ": body " << kBodySweepDb[i] << " dBFS -> subTap " << sweep[i].tapRmsDb
                   << " dBFS, region mean " << mean << " dBFS (tolerance +/-" << toleranceDb
                   << " dB)");
        REQUIRE(std::fabs(sweep[i].tapRmsDb - mean) <= toleranceDb);
    }
}

}  // namespace

// ==============================================================================
// T017 / SC-002 - The tracking law is the one FR-032 specifies
// ==============================================================================
// spec.md:913-932. Three seven-point sweeps, each step 8 s of a mono-identical
// 55 Hz body with the last 4 s measured:
//
//   tracked   - tracking 1.0 at the default reference (-18 dBFS): arms (a), (b)
//   untracked - tracking 0.0, everything else identical:          arm  (c)
//   moved     - tracking 1.0 with setTrackReferenceDb(-30):       arm  (e)
//
// and arm (d) on all three.
//
// WHY THE TAP AND NOT THE OUTPUT: by FR-062 the tap is post-chain and
// POST-TRACKING (subharmonic_engine.h:1263-1270), which is the only point at
// which the FR-032 law is observable at all - a pre-tracking tap would read a
// flat line at every body level and pass for a broken implementation. The tap
// also carries no body, so the sweep measures the sub alone with no need to
// notch 55 Hz out of the measurement.
//
// FALSIFICATION (tasks.md T017: "run the sweep with setTrackingAmount(0.0f) and
// confirm arm (a)'s unity slope fails"). That sweep is arm (c), which the case
// already renders - so the falsification is carried IN the criterion rather than
// as a temporary source mutation that nobody can re-run: the same slope
// statistic arm (a) asserts is recomputed on the tracking-0 sweep and REQUIREd
// to miss unity by more than kSlopeFalsificationMarginDb. Arm (a) therefore
// cannot pass for an uninteresting reason - a tap that ignored the body would
// fail the falsification clause 42 dB wide.
//
// PREDICTED-RISK NOTICE, recorded BEFORE the first run (this is a test-first
// task; nothing below was measured). The engine's sensor is EnvelopeFollower in
// RMS mode with asymmetric coefficients (attack tau = 19.1 ms, release
// tau = 127.3 ms, envelope_follower.h:312-326). On a steady sine, an asymmetric
// smoother in the SQUARED domain does not settle on the mean square: solving the
// steady-state balance (1 - attackCoeff) * mean((s - u)+) =
// (1 - releaseCoeff) * mean((u - s)+) for s = sin^2 gives u ~ 0.785 A^2, i.e.
// getCurrentValue() ~ 0.886 A against a true RMS of 0.707 A - about +1.96 dB.
// If that arithmetic holds in the render, the knee sits near a -20 dBFS body
// rather than -18 (and near -32 rather than -30 in arm (e)), which would put
// arm (a)'s -18 dBFS point ~2 dB under the unity line and breach its +/-1.0 dB;
// the same ripple (~ +/-0.75 % on envNorm, which the 50 ms trackGain ramp
// averages away while getTrackedEnvelope() reports it instantaneously) is a
// risk to arm (d)'s 1e-4 at the rising steps.
// THE TOLERANCES ARE NOT PRE-EMPTIVELY WIDENED AND THE SWEEP IS NOT TRIMMED:
// FR-071 (tasks.md:133-140) says a missed figure is SURFACED with its measured
// table for a user ruling, never absorbed by moving the line. The WARNs above
// print the whole table for exactly that purpose.
// ==============================================================================
TEST_CASE("SubharmonicEngine_TrackingLaw", "[subharmonic_engine]") {
    const TrackingSweep tracked = runTrackingSweep(
        1.0f, SubharmonicEngine::kDefaultTrackReferenceDb, "tracking 1.0, ref -18");
    const TrackingSweep untracked = runTrackingSweep(
        0.0f, SubharmonicEngine::kDefaultTrackReferenceDb, "tracking 0.0, ref -18");
    const TrackingSweep moved = runTrackingSweep(1.0f, -30.0f, "tracking 1.0, ref -30");

    requireAnchorGate(tracked, "tracking 1.0, ref -18");
    requireAnchorGate(untracked, "tracking 0.0, ref -18");
    requireAnchorGate(moved, "tracking 1.0, ref -30");

    // ---- (a) unity slope in dB, +/-1.0 dB, across -60 ... -24 dBFS ----------
    // Stated twice, because "unity slope within +/-1.0 dB at every point" is
    // both an end-to-end claim and a per-point claim, and only the pair of them
    // excludes a curve that arrives at the right endpoint by the wrong route.
    const double bodyRiseDb = tracked[kRisingTopIndex].bodyRmsDb - tracked[0].bodyRmsDb;
    const double tapRiseDb = tracked[kRisingTopIndex].tapRmsDb - tracked[0].tapRmsDb;

    WARN("SC-002 (a): body rose " << bodyRiseDb << " dB, subTap rose " << tapRiseDb
                                  << " dB across -60 -> -24 dBFS (unity within +/-"
                                  << kUnitySlopeToleranceDb << " dB)");
    INFO("(a) end-to-end: body " << bodyRiseDb << " dB vs subTap " << tapRiseDb << " dB");
    REQUIRE(std::fabs(tapRiseDb - bodyRiseDb) <= kUnitySlopeToleranceDb);

    double offsetSum = 0.0;
    for (std::size_t i = 0; i <= kRisingTopIndex; ++i) {
        offsetSum += tracked[i].tapRmsDb - tracked[i].bodyRmsDb;
    }
    const double meanOffsetDb = offsetSum / static_cast<double>(kRisingTopIndex + 1);
    WARN("SC-002 (a): mean subTap - body offset across the rising region = " << meanOffsetDb
                                                                             << " dB");
    for (std::size_t i = 0; i <= kRisingTopIndex; ++i) {
        const double offsetDb = tracked[i].tapRmsDb - tracked[i].bodyRmsDb;
        INFO("(a) at body " << kBodySweepDb[i] << " dBFS: subTap - body = " << offsetDb
                            << " dB, rising-region mean " << meanOffsetDb << " dB (tolerance +/-"
                            << kUnitySlopeToleranceDb << " dB)");
        REQUIRE(std::fabs(offsetDb - meanOffsetDb) <= kUnitySlopeToleranceDb);
    }

    // ---- (b) flat +/-0.5 dB across -18 ... -6 dBFS --------------------------
    // The trackReferenceRms clamp: above the reference envNorm is exactly 1.
    requireFlatRegion(tracked, kKneeIndex, kSweepPoints - kKneeIndex, kFlatToleranceDb,
                      "SC-002 (b) plateau");

    // ---- (c) tracking 0: flat +/-0.5 dB across the WHOLE sweep --------------
    // Free-running by design; the criterion records that it is stable, not
    // silent (the anchor gate above already established "not silent").
    requireFlatRegion(untracked, 0, kSweepPoints, kFlatToleranceDb, "SC-002 (c) free-running");

    // ... and the same statistic arm (a) asserts, recomputed here, must MISS
    // unity by a mile. This is T017's falsification, carried in the criterion.
    const double untrackedRiseDb = untracked[kKneeIndex].tapRmsDb - untracked[0].tapRmsDb;
    WARN("SC-002 (a) falsification via (c): at tracking 0 the body rose "
         << bodyRiseDb << " dB and subTap rose " << untrackedRiseDb
         << " dB - arm (a)'s unity slope must FAIL here by more than "
         << kSlopeFalsificationMarginDb << " dB");
    INFO("(c) falsification: |" << untrackedRiseDb << " - " << bodyRiseDb << "| must exceed "
                                << kSlopeFalsificationMarginDb << " dB");
    REQUIRE(std::fabs(untrackedRiseDb - bodyRiseDb) > kSlopeFalsificationMarginDb);

    // ---- (d) the read surface corroborates the audio ------------------------
    requireReadSurfaceAgrees(tracked, 1.0, "tracking 1.0, ref -18");
    requireReadSurfaceAgrees(untracked, 0.0, "tracking 0.0, ref -18");
    requireReadSurfaceAgrees(moved, 1.0, "tracking 1.0, ref -30");

    // ---- (e) the reference is load-bearing ----------------------------------
    // setTrackReferenceDb must reach envNorm's DENOMINATOR, not merely its
    // getter (which SubharmonicEngine_ControlSurfaceContract already covers).
    const double defaultKneeDb = estimateKneeDb(tracked);
    const double movedKneeDb = estimateKneeDb(moved);

    const double expectedDefaultKneeDb =
        static_cast<double>(SubharmonicEngine::kDefaultTrackReferenceDb) + kSensorSteadySineBiasDb;
    const double expectedMovedKneeDb = -30.0 + kSensorSteadySineBiasDb;
    WARN("SC-002 (e): knee at the default reference = "
         << defaultKneeDb << " dBFS (expected " << expectedDefaultKneeDb
         << "), knee at setTrackReferenceDb(-30) = " << movedKneeDb << " dBFS (expected "
         << expectedMovedKneeDb << "), shift " << (movedKneeDb - defaultKneeDb)
         << " dB (expected " << kReferenceMoveDb << "), tolerance +/-" << kKneeToleranceDb
         << " dB");

    INFO("(e) default-reference knee = " << defaultKneeDb << " dBFS");
    REQUIRE(std::fabs(defaultKneeDb - expectedDefaultKneeDb) <= kKneeToleranceDb);

    INFO("(e) moved-reference knee = " << movedKneeDb << " dBFS");
    REQUIRE(std::fabs(movedKneeDb - expectedMovedKneeDb) <= kKneeToleranceDb);

    INFO("(e) knee shift = " << (movedKneeDb - defaultKneeDb) << " dB");
    REQUIRE(std::fabs((movedKneeDb - defaultKneeDb) - kReferenceMoveDb) <= kKneeToleranceDb);

    // (e2) the structural consequence of the move, asserted on the sweep points
    // themselves. NOTE ON THE SPEC TEXT, SURFACED RATHER THAN ENCODED: spec.md
    // (e) glosses this as "the -24 dBFS point, flat at the default reference,
    // now falls in the rising region, and the -18 dBFS point ... is now flat",
    // and that parenthetical is INVERTED with respect to the criterion's own
    // arms. At the default -18 reference, -24 dBFS is BELOW the knee and is
    // therefore part of arm (a)'s RISING region; moving the reference DOWN to
    // -30 puts -24 dBFS ABOVE the knee, i.e. flat. What is asserted here is the
    // correct form of that claim: with the reference at -30, the whole
    // -24 ... -6 dBFS group is one flat plateau. The knee assertions above are
    // the criterion; this is its consequence stated on the raw points, and the
    // two cannot both hold unless the reference really moved the denominator.
    requireFlatRegion(moved, kKneeIndex - 1, kSweepPoints - (kKneeIndex - 1), kFlatToleranceDb,
                      "SC-002 (e2) plateau at ref -30");
}

// ==============================================================================
// T017 / SC-005 - The Square path produces no inharmonic content
// ==============================================================================
// spec.md:1035-1049. IsolatedSub with Div2 ALONE (three tones sounding would put
// 55, 27.5 and 73.33 Hz in one frame with no common integer fundamental, and the
// criterion would fail on a correct implementation), SubWaveform::Square,
// f = 110 Hz so the sub fundamental is 55 Hz, drive kMinDriveDb, low-pass
// kMaxLowpassHz, one 262 144-point Hann frame at 48 kHz (0.183 Hz bins).
//
//   (a) every peak more than -40 dB relative to the 55 Hz fundamental lies
//       within +/-1 bin of an integer multiple of 55 Hz;
//   (b) the Square-path subTap RMS is above -40 dBFS;
//   (c) see the note on the tanh below.
//
// THE PEAK-SELECTION RULE IS PART OF THE CRITERION, not an implementation
// detail (low_frequency_metrics.h, findSpectralPeaks): candidates at or above
// the threshold, visited in DESCENDING magnitude order, accepted only if no
// accepted peak lies within +/-kPeakExclusionBins. A plain local-maxima scan
// reports the Hann window's own first sidelobe (-31.5 dB, ~2.4 bins off the main
// lobe) and fails a CORRECT implementation; a global-maximum scan cannot fail at
// all. SubharmonicEngine_LowFrequencyMetricsSelfCheck pins that rule against
// synthetic one- and two-tone frames and PRECEDES this case in declaration order
// (Catch2 default --order decl), so a peak-picker bug fails as a peak-picker bug
// and never as an inharmonic-divider claim.
//
// (c) FR-041's tanh is in circuit even at drive 0. On a SINGLE tone it generates
// only ODD harmonics of that tone - 165, 275, 385 Hz ... - which are integer
// multiples of 55 Hz and are therefore admitted by (a) by construction. They are
// the shaper's, not the divider's, and this case attributes them accordingly
// rather than counting them as evidence either way. The same is true of the
// Square wave's own harmonic series, which is what the tone IS.
// ==============================================================================
TEST_CASE("SubharmonicEngine_SquareSpectrum", "[subharmonic_engine]") {
    constexpr std::size_t kN = lfm::kLowFrequencyFftSize;
    constexpr float kFundamentalHz = 110.0f;
    constexpr double kSubHz = 55.0;  // Div2 of 110 Hz
    constexpr std::size_t kDiv2 = SubharmonicEngine::index(SubharmonicEngine::Tone::Div2);
    constexpr float kPeakThresholdDb = -40.0f;
    constexpr double kSquareRmsGateDb = -40.0;  // arm (b)
    constexpr double kPeakToleranceBins = 1.0;  // arm (a)

    REQUIRE(lfm::lowFreq::analysisFft(kN).isPrepared());

    IsolatedSub fixture(kDiv2, kFundamentalHz, SubWaveform::Square);
    REQUIRE(fixture.engine.getToneWaveform(kDiv2) == SubWaveform::Square);

    const std::vector<float> tap = fixture.renderTap(kN);
    requireTapAboveGate(tap, "Div2 Square @ f = 110 Hz");

    // ---- (b) the minBLEP path is actually engaged ---------------------------
    // This is R-1's positive assertion: an unprepared or null MinBlepTable makes
    // SubOscillator::prepare() hard-fail to prepared_ = false and process()
    // return 0.0f forever (sub_oscillator.h:142-147, :222-226), so the failure
    // mode is SILENCE, not an un-BLEPped tone. Predicted level at the fixture's
    // -20 dBFS tone: ~ -20.5 dBFS, i.e. 20 dB of margin over this line.
    // SubharmonicEngine_SquareSpectrum_UnpreparedTableFalsification below proves
    // the assertion catches that failure.
    const double squareRmsDb =
        static_cast<double>(Krate::DSP::gainToDb(lfm::lowFreq::frameRms(tap.data(), tap.size())));
    WARN("SC-005 (b): Square-path subTap RMS = " << squareRmsDb << " dBFS (floor "
                                                 << kSquareRmsGateDb << " dBFS)");
    INFO("(b) Square-path subTap RMS = " << squareRmsDb << " dBFS");
    REQUIRE(squareRmsDb > kSquareRmsGateDb);

    // ---- (a) every peak is a harmonic of 55 Hz ------------------------------
    const std::vector<std::size_t> peaks = lfm::findSpectralPeaks(tap.data(), kN, kPeakThresholdDb);
    INFO("(a) peak count = " << peaks.size());
    REQUIRE(!peaks.empty());

    const double binHz = IsolatedSub::kSampleRate / static_cast<double>(kN);
    double worstErrorBins = 0.0;
    double worstErrorHz = 0.0;
    double worstHarmonic = 0.0;

    for (const std::size_t bin : peaks) {
        const double peakHz = static_cast<double>(bin) * binHz;
        const double harmonic = std::round(peakHz / kSubHz);

        // A peak below 27.5 Hz would round to harmonic 0 - i.e. DC-adjacent
        // content, not a harmonic of the sub - and must fail rather than pass on
        // the technicality that 0 is an integer multiple.
        INFO("(a) peak at bin " << bin << " = " << peakHz << " Hz, nearest harmonic index "
                                << harmonic);
        REQUIRE(harmonic >= 1.0);

        const double errorHz = std::fabs(peakHz - harmonic * kSubHz);
        const double errorBins = errorHz / binHz;
        INFO("(a) peak at " << peakHz << " Hz vs harmonic " << harmonic << " ("
                            << harmonic * kSubHz << " Hz): error " << errorHz << " Hz = "
                            << errorBins << " bins");
        REQUIRE(errorBins <= kPeakToleranceBins);

        if (errorBins > worstErrorBins) {
            worstErrorBins = errorBins;
            worstErrorHz = errorHz;
            worstHarmonic = harmonic;
        }
    }

    WARN("SC-005 (a): " << peaks.size() << " peaks above " << kPeakThresholdDb
                        << " dB relative, all within +/-" << kPeakToleranceBins
                        << " bin of a multiple of 55 Hz; worst = " << worstErrorBins << " bins ("
                        << worstErrorHz << " Hz) at harmonic " << worstHarmonic << ", bin width "
                        << binHz << " Hz");
}

// ==============================================================================
// T017 / SC-005 (b) FALSIFICATION - the RMS floor catches an unprepared table
// ==============================================================================
// tasks.md T017: "for SC-005 (b), prepare a SubOscillator against an UNPREPARED
// MinBlepTable in a scratch fixture and confirm the RMS assertion catches the
// resulting silence."
//
// Carried as a HIDDEN case ([.falsification], the T016 precedent in this TU)
// rather than as a temporary source mutation: the scratch fixture is
// reproducible by name -
//
//   dsp_systems_tests.exe "SubharmonicEngine_SquareSpectrum_UnpreparedTableFalsification"
//
// - and nothing about SC-005's own fixture is touched, so there is no pin to
// restore. Hidden tags are excluded from an unfiltered suite run, so this adds
// nothing to CI time and cannot turn the per-push lane red.
//
// WHY A SCRATCH SubOscillator AND NOT THE ENGINE: SubharmonicEngine::prepare()
// prepares its one shared MinBlepTable before it prepares the three oscillators
// (D-8), and the table is private - the engine has no reachable state in which
// its oscillators face an unprepared table. The failure R-1 describes is a
// property of the SubOscillator/MinBlepTable contract, so it is injected there,
// at exactly the two lines SC-005 (b) cites (sub_oscillator.h:142-147, :222-226).
// ==============================================================================
TEST_CASE("SubharmonicEngine_SquareSpectrum_UnpreparedTableFalsification",
          "[subharmonic_engine][.falsification]") {
    constexpr float kFundamentalHz = 110.0f;
    constexpr std::size_t kRenderSamples = 96000;  // 2 s at 48 kHz
    constexpr double kSquareRmsGateDb = -40.0;     // SC-005 (b)'s line, verbatim

    // prepare() is deliberately NOT called on this table.
    Krate::DSP::MinBlepTable unpreparedTable{};
    REQUIRE_FALSE(unpreparedTable.isPrepared());

    Krate::DSP::SubOscillator osc(&unpreparedTable);
    osc.prepare(IsolatedSub::kSampleRate);
    osc.setOctave(Krate::DSP::SubOctave::OneOctave);  // master / 2, as Div2
    osc.setWaveform(SubWaveform::Square);

    // Drive it the way the engine does: a master phase accumulator at 110 Hz,
    // reporting its wrap to the divider.
    const double increment = static_cast<double>(kFundamentalHz) / IsolatedSub::kSampleRate;
    double phase = 0.0;
    double sumSq = 0.0;

    for (std::size_t i = 0; i < kRenderSamples; ++i) {
        phase += increment;
        bool wrapped = false;
        if (phase >= 1.0) {
            phase -= 1.0;
            wrapped = true;
        }
        const double y = static_cast<double>(osc.process(wrapped, static_cast<float>(increment)));
        sumSq += y * y;
    }

    const double rms = std::sqrt(sumSq / static_cast<double>(kRenderSamples));
    const double rmsDb = static_cast<double>(Krate::DSP::gainToDb(static_cast<float>(rms)));
    WARN("SC-005 (b) falsification: SubOscillator on an UNPREPARED MinBlepTable renders RMS = "
         << rms << " (" << rmsDb << " dBFS) against SC-005 (b)'s " << kSquareRmsGateDb
         << " dBFS floor");

    // The failure mode is SILENCE, not an un-BLEPped tone - that is the whole
    // reason (b) is an RMS floor and not a spectral assertion. Exact, because
    // process() returns a literal 0.0f when !prepared_.
    INFO("unprepared-table RMS = " << rms);
    REQUIRE(rms == 0.0);

    // ... and SC-005 (b), applied to it, FAILS. If this ever stops failing, (b)
    // has stopped being an aliveness assertion.
    INFO("unprepared-table RMS = " << rmsDb << " dBFS, SC-005 (b) floor " << kSquareRmsGateDb);
    REQUIRE_FALSE(rmsDb > kSquareRmsGateDb);
}

// ==============================================================================
// T018 / SC-017 - the long-render stationarity machinery
// ==============================================================================
// spec.md:1219-1241, plan S10.2 (:1410) and R-13 (:1667). Two 10-minute arms at
// 48 kHz, both measured on the SUB CONTRIBUTION (outL - inL) rather than on the
// output: arm (A)'s dry body is itself a perfectly stationary 55 Hz sine that
// sits INSIDE the < 200 Hz measurement band, so a criterion taken on `out` would
// be dominated by the body and could not see the sub creep at all. `out - in` is
// the same read every other level criterion in this phase takes (SC-001 (b)/(c),
// SC-002, SC-022 (b2)); it is exact here because FR-050's dry path is a pure add.
//
// MEMORY (R-13, tasks.md T018 "Mandatory"): a 10-minute stereo render is 230 MB
// and is NEVER materialised. The render loop below holds five 512-sample buffers
// and one streaming accumulator - the per-minute sub-band RMS, the finiteness
// flag and the clamp counter are all folded in as the blocks go past, so the
// resident set is a few kilobytes whatever the render length.
//
// THE SUB-BAND MEASURE IS THE PHASE'S EXISTING ONE, restated streaming: two
// cascaded 2-pole Butterworth low-passes at 200 Hz (24 dB/oct), exactly
// subharmonic_engine_test.cpp:169-186's `subBandRms`. The only difference is
// that the filter state LIVES ACROSS the whole render instead of being rebuilt
// per buffer, which is what makes the per-minute figures comparable: a filter
// re-prepared at each minute boundary would inject its own settling transient
// into every reading and show up as peak-to-peak spread the engine did not
// produce.
// ==============================================================================

namespace {

/// The band edge SC-017 is stated in ("< 200 Hz"), shared by SC-007 and SC-019.
constexpr float kStationarityBandEdgeHz = 200.0f;

/// The fixture body: a steady -12 dBFS 55 Hz sine on both channels.
/// 10^(-12/20), written as the literal for the same reason
/// subharmonic_engine_test.cpp:306-308 writes it that way - so the number in the
/// source is the number in the spec.
constexpr double kStationarityBodyHz = 55.0;
constexpr double kStationarityBodyAmplitude = 0.251188643150958;

/// Ten minutes at 48 kHz, in 512-sample blocks. 2 880 000 = 60 s at 48 kHz and
/// is an exact multiple of 512 (5625 blocks), so no minute boundary ever falls
/// inside a block and the accumulator never has to carry a partial minute.
constexpr std::size_t kStationarityMinutes = 10;
constexpr std::size_t kStationarityBlockSamples = 512;
constexpr std::size_t kStationarityMinuteSamples = 2880000;

/// 192 blocks = 2.048 s of settling, rendered and finiteness-checked but NOT
/// measured. Every ramp in the component is 50 ms (FR-022 level, FR-032
/// tracking, FR-051 wet, the S5.3 cutoff glide) and the FR-031 follower attack
/// is a 19.1 ms time constant (plan S10.4 C-10), so this is ~40 time constants.
/// Without it, minute 1 would carry the trackGainRamp_ climbing from
/// (1 - amount) = 0 and would read low by its own construction, biasing arm
/// (A)'s trend upward for a reason that is not level creep.
constexpr std::size_t kStationaritySettleSamples = 98304;

/// SC-017 (a) and (b). NOT MOVABLE by any implementing agent (FR-071's
/// stop-and-surface rule): they are the roadmap's own boundedness gate
/// (lines 512-514) in the form this feed-forward component admits.
constexpr double kStationarityPeakToPeakDb = 3.0;
constexpr double kStationarityTrendDbPerMinute = 0.3;

/// @brief Streaming per-minute sub-band RMS of a signal fed one sample at a time.
///
/// Push order is the render order; the two filter stages and the running
/// sum-of-squares are the entire state. A "minute" is `minuteSamples` pushes -
/// parameterised only so the falsification case below can exercise the same
/// arithmetic over 1-second minutes without a ten-minute render.
class SubBandMinuteStats {
  public:
    SubBandMinuteStats(double sampleRate, std::size_t minuteSamples, std::size_t expectedMinutes)
        : minuteSamples_(minuteSamples) {
        stageA_.prepare(sampleRate);
        stageB_.prepare(sampleRate);
        stageA_.setCutoff(kStationarityBandEdgeHz);
        stageB_.setCutoff(kStationarityBandEdgeHz);
        minuteRmsDb_.reserve(expectedMinutes);
    }

    /// Accumulate one sample. A completed minute is folded into `minuteRmsDb_`
    /// and the accumulator rewinds; a trailing partial minute is DISCARDED
    /// rather than reported short, which is why every render length here is an
    /// exact multiple of `minuteSamples`.
    void push(float sample) noexcept {
        const double y = static_cast<double>(stageB_.process(stageA_.process(sample)));
        sumSq_ += y * y;
        ++count_;
        if (count_ == minuteSamples_) {
            const double rms = std::sqrt(sumSq_ / static_cast<double>(minuteSamples_));
            minuteRmsDb_.push_back(
                static_cast<double>(Krate::DSP::gainToDb(static_cast<float>(rms))));
            sumSq_ = 0.0;
            count_ = 0;
        }
    }

    [[nodiscard]] const std::vector<double>& minuteRmsDb() const noexcept { return minuteRmsDb_; }

    /// SC-017 (a): max - min over the completed minutes, in dB. 0.0 for fewer
    /// than two readings - which every caller rejects up front rather than
    /// passes on.
    [[nodiscard]] double peakToPeakDb() const noexcept {
        if (minuteRmsDb_.size() < 2) {
            return 0.0;
        }
        const auto bounds = std::minmax_element(minuteRmsDb_.begin(), minuteRmsDb_.end());
        return *bounds.second - *bounds.first;
    }

    /// SC-017 (b): the ordinary-least-squares slope of the per-minute dB
    /// readings against the minute index, i.e. dB per minute.
    [[nodiscard]] double trendDbPerMinute() const noexcept {
        const std::size_t n = minuteRmsDb_.size();
        if (n < 2) {
            return 0.0;
        }
        const double count = static_cast<double>(n);
        double meanX = 0.0;
        double meanY = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            meanX += static_cast<double>(i);
            meanY += minuteRmsDb_[i];
        }
        meanX /= count;
        meanY /= count;

        double sxy = 0.0;
        double sxx = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double dx = static_cast<double>(i) - meanX;
            sxy += dx * (minuteRmsDb_[i] - meanY);
            sxx += dx * dx;
        }
        return (sxx > 0.0) ? (sxy / sxx) : 0.0;
    }

  private:
    Krate::DSP::TwoPoleLP stageA_;
    Krate::DSP::TwoPoleLP stageB_;
    std::vector<double> minuteRmsDb_;
    std::size_t minuteSamples_;
    double sumSq_ = 0.0;
    std::size_t count_ = 0;
};

/// Everything SC-017 asserts, gathered in one streaming pass.
struct StationarityRender {
    std::vector<double> minuteRmsDb;
    double peakToPeakDb = 0.0;
    double trendDbPerMinute = 0.0;
    bool allFinite = true;                 ///< SC-017 (c)
    std::size_t firstNonFiniteSample = 0;  ///< only meaningful when !allFinite
    std::uint32_t clampEngagements = 0;    ///< SC-017 (d)
};

/// @brief Render `minutes` minutes of the -12 dBFS 55 Hz body through `engine`
///        in 512-sample blocks, folding SC-017's statistics in as it goes.
///
/// The caller has already prepared and configured the engine (arm (A) touches no
/// setter at all; arm (B) is configureWorstCaseArm() below). Nothing longer than
/// one block is ever held.
[[nodiscard]] StationarityRender renderStationarity(SubharmonicEngine& engine, double sampleRate,
                                                    std::size_t minutes) {
    SubBandMinuteStats stats(sampleRate, kStationarityMinuteSamples, minutes);

    std::vector<float> inL(kStationarityBlockSamples, 0.0f);
    std::vector<float> inR(kStationarityBlockSamples, 0.0f);
    std::vector<float> outL(kStationarityBlockSamples, 0.0f);
    std::vector<float> outR(kStationarityBlockSamples, 0.0f);
    std::vector<float> tap(kStationarityBlockSamples, 0.0f);

    // A WRAPPED double accumulator, not omega * sampleIndex: the index reaches
    // 28.9 million here and std::sin of a ~207 000-radian argument spends its
    // accuracy on range reduction for no reason. Wrapped, the argument stays in
    // [0, 2pi) and the body's own purity is never in question.
    const double omega = kSelfCheckTwoPi * kStationarityBodyHz / sampleRate;
    double phase = 0.0;

    StationarityRender result;
    const std::size_t measured = minutes * kStationarityMinuteSamples;
    const std::size_t total = kStationaritySettleSamples + measured;

    for (std::size_t start = 0; start < total; start += kStationarityBlockSamples) {
        const std::size_t n = std::min(kStationarityBlockSamples, total - start);
        for (std::size_t i = 0; i < n; ++i) {
            inL[i] = static_cast<float>(kStationarityBodyAmplitude * std::sin(phase));
            inR[i] = inL[i];
            phase += omega;
            if (phase >= kSelfCheckTwoPi) {
                phase -= kSelfCheckTwoPi;
            }
        }

        engine.processBlockTapped(inL.data(), inR.data(), outL.data(), outR.data(), tap.data(), n);

        for (std::size_t i = 0; i < n; ++i) {
            // SC-017 (c). Krate::DSP::detail::isFinite, never std::isnan: this TU
            // is compiled WITH fast-math on the macOS and Linux legs, where a
            // plain library predicate folds away (db_utils.h:116-123). The
            // settling window is checked too - a non-finite sample is a defect
            // wherever it lands.
            const bool finite = Krate::DSP::detail::isFinite(outL[i]) &&
                                Krate::DSP::detail::isFinite(outR[i]) &&
                                Krate::DSP::detail::isFinite(tap[i]);
            if (!finite && result.allFinite) {
                result.allFinite = false;
                result.firstNonFiniteSample = start + i;
            }

            if (start + i >= kStationaritySettleSamples) {
                stats.push(outL[i] - inL[i]);
            }
        }
    }

    result.minuteRmsDb = stats.minuteRmsDb();
    result.peakToPeakDb = stats.peakToPeakDb();
    result.trendDbPerMinute = stats.trendDbPerMinute();
    result.clampEngagements = engine.getClampEngagementCount();
    return result;
}

/// SC-017 (B): the reachable extreme of every control the spec exposes, and
/// nothing else - the fundamental stays at the FR-013 default 55 Hz so all three
/// tones are awake (27.5 / 13.75 / 36.67 Hz, all clear of FR-016's 12 Hz
/// backstop), which is the configuration the criterion is written against.
void configureWorstCaseArm(SubharmonicEngine& engine) {
    engine.setTrackingAmount(0.0f);  // FR-033: free-running, no envelope holding it down
    engine.setDriveDb(SubharmonicEngine::kMaxDriveDb);
    engine.setWetGainDb(SubharmonicEngine::kMaxWetGainDb);
    engine.setLowpassCutoffHz(SubharmonicEngine::kMaxLowpassHz);
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        engine.setToneLevelDb(t, SubharmonicEngine::kMaxToneLevelDb);
        engine.setToneBreathDepth(t, 1.0f);
    }
}

/// The per-minute readings as one transcribable line.
[[nodiscard]] std::string minuteRmsLine(const std::vector<double>& minuteRmsDb) {
    std::string text;
    for (std::size_t m = 0; m < minuteRmsDb.size(); ++m) {
        if (m != 0) {
            text += ", ";
        }
        text += std::to_string(m + 1);
        text += ":";
        text += std::to_string(minuteRmsDb[m]);
    }
    return text;
}

/// SC-017 (a), (b) and (c) plus the aliveness gate, applied to one arm.
/// (d) differs between the arms and is asserted at the call site.
void requireStationary(const StationarityRender& render, const char* arm) {
    INFO(arm << ": completed minutes = " << render.minuteRmsDb.size());
    REQUIRE(render.minuteRmsDb.size() == kStationarityMinutes);

    // ---- (c) no non-finite sample ------------------------------------------
    INFO(arm << ": first non-finite sample index = " << render.firstNonFiniteSample);
    REQUIRE(render.allFinite);

    // ---- aliveness, before any dB figure is trusted -------------------------
    // A silent sub makes (a) and (b) both exactly 0 and the criterion vacuous;
    // the same -60 dBFS backstop the IsolatedSub fixture applies (plan S10.3,
    // R-1) applies here, minute by minute.
    for (std::size_t m = 0; m < render.minuteRmsDb.size(); ++m) {
        INFO(arm << ": minute " << (m + 1) << " sub-band RMS = " << render.minuteRmsDb[m]
                 << " dBFS (gate " << IsolatedSub::kRmsGateDb << " dBFS)");
        REQUIRE(render.minuteRmsDb[m] > static_cast<double>(IsolatedSub::kRmsGateDb));
    }

    // ---- (a) peak-to-peak ---------------------------------------------------
    INFO(arm << ": per-minute sub-band RMS = [" << minuteRmsLine(render.minuteRmsDb) << "]");
    INFO(arm << ": peak-to-peak = " << render.peakToPeakDb << " dB (ceiling "
             << kStationarityPeakToPeakDb << " dB)");
    REQUIRE(render.peakToPeakDb <= kStationarityPeakToPeakDb);

    // ---- (b) linear trend ---------------------------------------------------
    INFO(arm << ": trend = " << render.trendDbPerMinute << " dB/minute (ceiling +/-"
             << kStationarityTrendDbPerMinute << ")");
    REQUIRE(std::fabs(render.trendDbPerMinute) <= kStationarityTrendDbPerMinute);
}

}  // namespace

// ==============================================================================
// T018 / SC-017 (A) + (B) - long-render stationarity
// ==============================================================================
// spec.md:1219-1241. Tagged [long]: excluded from the per-push CI filter
// (~[performance]~[perf]~[benchmark]~[!benchmark]~[long]), run nightly on all
// three OSes and included in an unfiltered local run. Its assertions are
// toolchain-INDEPENDENT - two dB figures against 3 dB and 0.3 dB/minute
// ceilings, a finiteness flag and a counter - which is exactly the [long]
// tagging rule (CLAUDE.md).
//
//   (A) every control at its default plus a steady -12 dBFS 55 Hz body. At the
//       FR-013 default f = 55 Hz all three tones are awake, which is what makes
//       this the reference arm rather than a partial one.
//   (B) the same body with tracking 0, all three tone levels kMaxToneLevelDb,
//       drive kMaxDriveDb, wet kMaxWetGainDb, all three breath depths 1.0 and
//       the low-pass at kMaxLowpassHz - the free-running boom the roadmap's own
//       Phase-6 criterion (line 319) is aimed at, at every reachable extreme at
//       once. SC-002 (c) only sweeps this configuration briefly; this arm soaks
//       it.
//
// (d) IS ASYMMETRIC BY SPECIFICATION, not by oversight: arm (A) asserts
// getClampEngagementCount() == 0, arm (B) TRANSCRIBES it. Arm (B) sits at
// FR-052's kMaxPreClampMagnitude, so a non-zero count there is information about
// the ladder rather than a failure - but an unreported one would hide it, which
// is why the WARN is not optional.
//
// FALSIFICATION (tasks.md T018):
// SubharmonicEngine_LongRenderStationarity_GateFalsification below, run with
// gateSteady() mutated to `return 1.0f;`. Read that case's header before
// touching either arm here.
// ==============================================================================
TEST_CASE("SubharmonicEngine_LongRenderStationarity", "[subharmonic_engine][long]") {
    constexpr double kRate = IsolatedSub::kSampleRate;  // 48 kHz, the phase's rate

    // ---- (A) the default arm ------------------------------------------------
    {
        SubharmonicEngine engine;
        engine.prepare(kRate,
                       SubharmonicEngine::PrepareConfig{.maxBlockSamples = kStationarityBlockSamples});
        // Deliberately NO setter call: "every control at its default" is the arm.
        REQUIRE(engine.isPrepared());
        REQUIRE(engine.getFundamentalHz() == SubharmonicEngine::kDefaultFundamentalHz);

        const StationarityRender armA = renderStationarity(engine, kRate, kStationarityMinutes);

        WARN("SC-017 (A) defaults, 10 min @ 48 kHz: per-minute sub-band RMS (dBFS) = ["
             << minuteRmsLine(armA.minuteRmsDb) << "], peak-to-peak = " << armA.peakToPeakDb
             << " dB, trend = " << armA.trendDbPerMinute
             << " dB/min, clamp engagements = " << armA.clampEngagements);

        requireStationary(armA, "SC-017 (A) defaults");

        // ---- (d) on arm (A): exactly zero -----------------------------------
        INFO("SC-017 (A): clamp engagements = " << armA.clampEngagements);
        REQUIRE(armA.clampEngagements == 0u);
    }

    // ---- (B) the worst-case arm ---------------------------------------------
    {
        SubharmonicEngine engine;
        engine.prepare(kRate,
                       SubharmonicEngine::PrepareConfig{.maxBlockSamples = kStationarityBlockSamples});
        configureWorstCaseArm(engine);

        // The configuration is asserted, not assumed: a setter that silently
        // clamped one of these extremes away would turn arm (B) into a second,
        // milder copy of arm (A) and the soak would prove nothing.
        REQUIRE(engine.getTrackingAmount() == 0.0f);
        REQUIRE(engine.getDriveDb() == SubharmonicEngine::kMaxDriveDb);
        REQUIRE(engine.getWetGainDb() == SubharmonicEngine::kMaxWetGainDb);
        REQUIRE(engine.getLowpassCutoffHz() == SubharmonicEngine::kMaxLowpassHz);
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            REQUIRE(engine.getToneLevelDb(t) == SubharmonicEngine::kMaxToneLevelDb);
            REQUIRE(engine.getToneBreathDepth(t) == 1.0f);
            REQUIRE_FALSE(engine.isToneInfrasonicFloored(t));
        }

        const StationarityRender armB = renderStationarity(engine, kRate, kStationarityMinutes);

        // (d) TRANSCRIBED, never asserted - see the header note above.
        WARN("SC-017 (B) worst case, 10 min @ 48 kHz: per-minute sub-band RMS (dBFS) = ["
             << minuteRmsLine(armB.minuteRmsDb) << "], peak-to-peak = " << armB.peakToPeakDb
             << " dB, trend = " << armB.trendDbPerMinute
             << " dB/min, clamp engagements (TRANSCRIBED, not gated) = " << armB.clampEngagements);

        requireStationary(armB, "SC-017 (B) worst case");
    }
}

// ==============================================================================
// T018 FALSIFICATION - the stationarity assertions can fail
// ==============================================================================
// tasks.md T018: "run arm (B) with the FR-016 backstop gate forced to 1.0 and a
// fundamental of 20 Hz, and confirm the trend/peak-to-peak assertions can fail;
// restore."
//
// Carried as a HIDDEN case ([.falsification], the precedent set twice already in
// this TU) so it costs an unfiltered suite run nothing and cannot turn the
// per-push lane red, and so the fixture half of the falsification is
// reproducible BY NAME instead of by re-typing a scratch harness:
//
//   dsp_systems_tests.exe "SubharmonicEngine_LongRenderStationarity_GateFalsification"
//
// (The criterion's own verify command, "SubharmonicEngine_LongRenderStationarity",
// is an EXACT name with no wildcard, so it does not pull this case in.)
//
// IT HAS TWO PARTS, because the two halves fail for different reasons and only
// one of them is reachable from a test file.
//
// PART 1 - THE MACHINERY, inverted. Feeds the SAME SubBandMinuteStats, with the
// SAME (a) and (b) predicates and the SAME 3 dB / 0.3 dB-per-minute ceilings,
// two synthetic signals of known truth over 1-second "minutes": a +6 dB linear
// drift (both predicates must reject it) and a 4 dB per-minute alternation (only
// (a) must reject it, proving the two clauses are independent rather than one
// quantity read twice). This part is DETERMINISTIC and costs 20 s of samples: if
// the criterion's arithmetic ever degenerates - a slope divided by the wrong
// span, a peak-to-peak taken on linear gain, an accumulator that reports one
// minute and passes vacuously - it fails, on every platform, with no source
// mutation needed.
//
// PART 2 - THE ENGINE, as tasks.md words it. The one-line MUTATION the executor
// applies is in gateSteady() (subharmonic_engine.h:1034-1039):
//
//     return (getToneFrequencyHz(tone) < kMinToneHz) ? 0.0f : 1.0f;   // shipped
//     return 1.0f;                                                    // MUTATED
//
// It cannot be reached from a test file: gateSteady() is private, the engine
// befriends only detail::SubharmonicEngineNonFiniteProbe
// (subharmonic_engine.h:869, defined solely by
// subharmonic_engine_nonfinite_test.cpp), and that probe's two operations are
// fixed by T019 at the DC blocker and the follower. So part 2 is written to be
// run TWICE, unmutated and mutated:
//
//   * UNMUTATED, at f = 20 Hz, FR-016 floors Div2 (10 Hz) and Div4 (5 Hz) and
//     leaves only FifthBelow (13.33 Hz) sounding - the shipped behaviour the
//     backstop exists to produce - and this case is EXPECTED TO PASS.
//   * MUTATED, all three tones sound at 5 / 10 / 13.33 Hz, every one of them
//     below the FR-042 blocker's 18 Hz corner, where the chain's response is
//     steep, phase-sensitive and modulated by three incommensurate breaths at
//     depth 1.0 - and the (a) / (b) assertions are EXPECTED TO FAIL.
//
// If the mutated run does NOT fail, the falsification has not been demonstrated
// and T018 stops and surfaces rather than recording it as done. Restore the line
// afterwards; the ten FR-080 headers must be byte-unchanged at the end of the
// phase (SC-016).
// ==============================================================================
TEST_CASE("SubharmonicEngine_LongRenderStationarity_GateFalsification",
          "[subharmonic_engine][.falsification][long]") {
    constexpr double kRate = IsolatedSub::kSampleRate;

    // ---- PART 1: the machinery, inverted ------------------------------------
    // 1-second "minutes", so the arithmetic under test is the criterion's while
    // the render is 10 s rather than 10 minutes. The 55 Hz probe tone is inside
    // the 200 Hz band (two-stage response 0.994, flat across every minute, so it
    // cancels out of both a peak-to-peak and a slope).
    {
        constexpr std::size_t kShortMinuteSamples = 48000;  // 1 s at 48 kHz
        constexpr double kProbeAmplitude = 0.1;
        const double omega = kSelfCheckTwoPi * kStationarityBodyHz / kRate;

        // (1a) a +6 dB linear drift across the ten readings: a true slope near
        //      0.67 dB/minute over ~6 dB of spread. BOTH ceilings must reject.
        {
            SubBandMinuteStats drifting(kRate, kShortMinuteSamples, kStationarityMinutes);
            const std::size_t total = kStationarityMinutes * kShortMinuteSamples;
            const double endGain = 1.9952623149688795;  // 10^(6/20)
            for (std::size_t i = 0; i < total; ++i) {
                const double t = static_cast<double>(i) / static_cast<double>(total - 1);
                const double gain = 1.0 + (endGain - 1.0) * t;
                drifting.push(static_cast<float>(kProbeAmplitude * gain *
                                                 std::sin(omega * static_cast<double>(i))));
            }

            REQUIRE(drifting.minuteRmsDb().size() == kStationarityMinutes);
            WARN("T018 falsification part 1a (+6 dB drift): per-minute sub-band RMS (dBFS) = ["
                 << minuteRmsLine(drifting.minuteRmsDb())
                 << "], peak-to-peak = " << drifting.peakToPeakDb()
                 << " dB, trend = " << drifting.trendDbPerMinute() << " dB/min");

            INFO("(1a) peak-to-peak = " << drifting.peakToPeakDb());
            REQUIRE_FALSE(drifting.peakToPeakDb() <= kStationarityPeakToPeakDb);
            INFO("(1a) trend = " << drifting.trendDbPerMinute());
            REQUIRE_FALSE(std::fabs(drifting.trendDbPerMinute()) <= kStationarityTrendDbPerMinute);
        }

        // (1b) a 4 dB alternation with no drift: (a) must reject it, (b) must
        //      NOT. Ten alternating readings starting high have an OLS slope of
        //      -10/82.5 = -0.121 dB/minute, comfortably inside (b)'s ceiling -
        //      so a build in which (a) and (b) had collapsed into one quantity
        //      would fail here.
        {
            SubBandMinuteStats alternating(kRate, kShortMinuteSamples, kStationarityMinutes);
            const double lowGain = 0.6309573444801932;  // 10^(-4/20)
            for (std::size_t m = 0; m < kStationarityMinutes; ++m) {
                const double gain = ((m % 2) == 0) ? 1.0 : lowGain;
                for (std::size_t i = 0; i < kShortMinuteSamples; ++i) {
                    const std::size_t index = m * kShortMinuteSamples + i;
                    alternating.push(static_cast<float>(
                        kProbeAmplitude * gain * std::sin(omega * static_cast<double>(index))));
                }
            }

            REQUIRE(alternating.minuteRmsDb().size() == kStationarityMinutes);
            WARN("T018 falsification part 1b (4 dB alternation): per-minute sub-band RMS (dBFS) = ["
                 << minuteRmsLine(alternating.minuteRmsDb())
                 << "], peak-to-peak = " << alternating.peakToPeakDb()
                 << " dB, trend = " << alternating.trendDbPerMinute() << " dB/min");

            INFO("(1b) peak-to-peak = " << alternating.peakToPeakDb());
            REQUIRE_FALSE(alternating.peakToPeakDb() <= kStationarityPeakToPeakDb);
            INFO("(1b) trend = " << alternating.trendDbPerMinute());
            REQUIRE(std::fabs(alternating.trendDbPerMinute()) <= kStationarityTrendDbPerMinute);
        }
    }

    // ---- PART 2: arm (B) at f = 20 Hz ---------------------------------------
    // Unmutated: expected to PASS (two of the three tones floored by FR-016).
    // With gateSteady() mutated to `return 1.0f;`: expected to FAIL.
    {
        SubharmonicEngine engine;
        engine.prepare(kRate,
                       SubharmonicEngine::PrepareConfig{.maxBlockSamples = kStationarityBlockSamples});
        configureWorstCaseArm(engine);
        engine.setFundamentalHz(20.0f);
        REQUIRE(engine.getFundamentalHz() == 20.0f);

        constexpr std::size_t kDiv2 = SubharmonicEngine::index(SubharmonicEngine::Tone::Div2);
        constexpr std::size_t kDiv4 = SubharmonicEngine::index(SubharmonicEngine::Tone::Div4);
        constexpr std::size_t kFifth =
            SubharmonicEngine::index(SubharmonicEngine::Tone::FifthBelow);

        // getToneFrequencyHz is derived from the accumulator increments and
        // moves with setFundamentalHz IMMEDIATELY (subharmonic_engine.h:616-637).
        WARN("T018 falsification part 2: at f = 20 Hz the tone frequencies are Div2 "
             << engine.getToneFrequencyHz(kDiv2) << " Hz, Div4 "
             << engine.getToneFrequencyHz(kDiv4) << " Hz, FifthBelow "
             << engine.getToneFrequencyHz(kFifth) << " Hz");

        const StationarityRender floored = renderStationarity(engine, kRate, kStationarityMinutes);

        // Transcribed AFTER the render, deliberately: the FR-016 latch is
        // refreshed in updateControl() step (3), not by setFundamentalHz(), so
        // read before a single control step it would still report the f = 55 Hz
        // state from prepare(). This is what makes the two runs of this case
        // distinguishable in the log without diffing the header - unmutated it
        // reads 1, 1, 0; mutated it reads 0, 0, 0.
        WARN("T018 falsification part 2: after the render, isToneInfrasonicFloored = "
             << engine.isToneInfrasonicFloored(kDiv2) << " (Div2), "
             << engine.isToneInfrasonicFloored(kDiv4) << " (Div4), "
             << engine.isToneInfrasonicFloored(kFifth) << " (FifthBelow)");

        WARN("T018 falsification part 2 (arm B @ f = 20 Hz): per-minute sub-band RMS (dBFS) = ["
             << minuteRmsLine(floored.minuteRmsDb) << "], peak-to-peak = " << floored.peakToPeakDb
             << " dB, trend = " << floored.trendDbPerMinute
             << " dB/min, clamp engagements = " << floored.clampEngagements);

        // SC-017's own (a), (b) and (c), verbatim - the aliveness gate excepted.
        // At f = 20 Hz the surviving 13.33 Hz tone sits BELOW the FR-042
        // blocker's 18 Hz corner and is legitimately quiet, so a -60 dBFS floor
        // would fail the shipped build for a reason that has nothing to do with
        // stationarity. What is under test here is whether (a) and (b) CAN fail.
        INFO("part 2: first non-finite sample index = " << floored.firstNonFiniteSample);
        REQUIRE(floored.allFinite);
        REQUIRE(floored.minuteRmsDb.size() == kStationarityMinutes);

        INFO("part 2: peak-to-peak = " << floored.peakToPeakDb << " dB");
        REQUIRE(floored.peakToPeakDb <= kStationarityPeakToPeakDb);
        INFO("part 2: trend = " << floored.trendDbPerMinute << " dB/minute");
        REQUIRE(std::fabs(floored.trendDbPerMinute) <= kStationarityTrendDbPerMinute);
    }
}
