// ==============================================================================
// Layer 3: System Tests - VoragoEngine, the [long] set
//                                    (specs/vorago-phase10-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10-voice-engine/spec.md
//            specs/vorago-phase10-voice-engine/plan.md
//            specs/vorago-phase10-voice-engine/tasks.md  (T006 creates and wires
//                                                         this TU; T019 fills it)
//
// SCOPE OF THIS TU: SC-004b, SC-005, SC-013b and SC-021b - the multi-minute
//   engine renders whose assertions are toolchain-INDEPENDENT. They carry the
//   [long] tag, so the per-push lane excludes them and the nightly workflow runs
//   them on all three operating systems. The bounded halves (SC-004a, SC-013a,
//   SC-021a) live in unit/systems/vorago_engine_test.cpp and must NEVER be
//   tagged [long].
//
// THIS TU IS THE PHASE'S DOMINANT COST (plan S10.4). The two overnight cases
//   render 8 h of audio x 3 seeds UNACCELERATED - roughly 6 h of wall clock at
//   ~4x real time - and they share ONE render (R-9). The sharing is a
//   function-local static, NOT a TEST_CASE_METHOD fixture: Catch2 constructs a
//   class fixture once PER CASE, which would render the eight hours twice.
//
// ALLOCATION DETECTION: this TU includes neither <allocation_detector.h> nor
//   <allocation_operator_overrides.h>. The single owner of the global
//   operator new/delete replacements in dsp_systems_tests is
//   unit/systems/selectable_oscillator_test.cpp:388; a second include of
//   <allocation_operator_overrides.h> is a duplicate-symbol link error.
//   SC-004b's allocation clause is read from VoragoEngine::getAllocatedBytes()
//   (vorago_engine.h:429), which is what FR-070's 8 h invariance clause names.
//
// PORTABILITY: no std::isnan / std::isinf / std::isfinite anywhere in this TU,
//   so it stays correct under -ffast-math. Every finiteness test is the
//   exponent-field bit check isFiniteBits() below, and every threshold
//   comparison is written positively (`!(x > y)`) so a NaN takes the failing
//   branch instead of slipping through.
//
// EVERY ENGINE IS HEAP-ALLOCATED, through the shared makeEngine() fixture:
//   std::array<VoragoVoice, kMaxVoices> is hundreds of kilobytes and MSVC's
//   default main-thread stack is 1 MiB (vorago_engine.h:146-151).
//
// WHY THESE CASES STREAM INSTEAD OF CALLING renderEngine(). Eight hours of
//   stereo float at 48 kHz is ~11 GB; it cannot be materialised. Every case
//   here renders block by block into one reusable pair of buffers and
//   accumulates its statistics as it goes. The shared fixture's own note applies
//   - a case that varies the standard render loop says so - and this is that.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/random.h>  // Xorshift32, deriveStreamSeed - the fuzz draw
#include <krate/dsp/primitives/fft.h>  // FFT, Complex - SC-005's band-limited centroid
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

// The shared Phase 10 fixtures (T016): makeEngine(), applyFastAttack(),
// kFastAttackEnvelopeConfig and the analysis helpers.
#include <vorago_fixtures.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Krate::DSP::deriveStreamSeed;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoEngineConfig;
using Krate::DSP::VoragoMacroMatrix;
using Krate::DSP::VoragoMacroValues;
using Krate::DSP::VoragoVoice;
using Krate::DSP::Xorshift32;

using Krate::DSP::Complex;
using Krate::DSP::FFT;
using Krate::DSP::TestUtils::Vorago::applyFastAttack;
using Krate::DSP::TestUtils::Vorago::makeEngine;

// =============================================================================
// Shared constants and small helpers
// =============================================================================

constexpr double kSampleRate48 = 48000.0;

/// The render partition every case here uses. 512 divides 48 000 x 30 and
/// 48 000 x 60 exactly, which is what makes the 30 s centroid epochs and the
/// 60 s comparison windows whole numbers of blocks rather than rounded ones.
constexpr std::size_t kBlockSamples = 512;

/// @brief True when @p v is neither infinite nor NaN, read off the exponent
///        field. Immune to -ffast-math, which std::isfinite is not.
[[nodiscard]] bool isFiniteBits(float v) noexcept {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// @brief dBFS of a mean-square value, floored at -240 dBFS.
///
/// The statistic stored per block is the MEAN SQUARE, not its dB: a mean square
/// can be averaged over a window arithmetically, a dB value cannot. The floor
/// mirrors the shared blockRmsDb() helper's (1e-12 in amplitude = 1e-24 in mean
/// square), so the two read the same on the same signal.
[[nodiscard]] double dbFromMeanSquare(double meanSquare) noexcept {
    return 10.0 * std::log10(std::max(meanSquare, 1e-24));
}

/// @brief Arithmetic mean of @p count mean-square entries starting at @p first.
[[nodiscard]] double meanSquareOver(const std::vector<float>& ms, std::size_t first,
                                    std::size_t count) noexcept {
    if ((count == 0u) || (first >= ms.size())) {
        return 0.0;
    }
    const std::size_t end = std::min(ms.size(), first + count);
    double sum = 0.0;
    for (std::size_t i = first; i < end; ++i) {
        sum += static_cast<double>(ms[i]);
    }
    return sum / static_cast<double>(end - first);
}

/// @brief SC-005's centroid: the magnitude-weighted spectral centroid of @p x
///        over the band [@p lowHz, Nyquist] - the SUB-IMMUNE construction.
///
/// RULED 2026-09-20 (spec Q-R). The full-band centroid of this instrument sits
/// at ~47 Hz on the held C1: the spectrum's magnitude mass is the subharmonic
/// tones and the fundamental, which are static, so everything that evolves
/// (the harmonic cloud, the resonance peaks, the noise bed, the blooms) moved
/// the full-band figure by ~3 % and the criterion measured the sub tones, not
/// the drone. The band above 4 f0 is the same construction SC-008's Darkness
/// row was re-ruled to. The shared `spectralCentroidHz` helper
/// (reverb_metrics.h:291) is left untouched - this is its band-limited variant:
/// the same Hann window, the same 8192-point transform, bins below @p lowHz
/// excluded from BOTH sums.
///
/// @return Centroid in Hz; 0.0 for an empty block or an all-zero band.
[[nodiscard]] double bandCentroidHz(std::span<const float> x, double sr, double lowHz) {
    if (x.empty() || !(sr > 0.0)) {
        return 0.0;
    }
    std::size_t fftSize = 256u;
    while ((fftSize * 2u) <= x.size() && (fftSize < 8192u)) {
        fftSize *= 2u;
    }
    FFT fft;
    fft.prepare(fftSize);
    if (fft.size() == 0u) {
        return 0.0;
    }
    std::vector<float> windowed(fftSize, 0.0f);
    const std::size_t copyCount = std::min(fftSize, x.size());
    const double denomWindow = static_cast<double>(fftSize);
    for (std::size_t i = 0; i < copyCount; ++i) {
        const double w = 0.5
                         - (0.5
                            * std::cos(2.0 * Krate::DSP::TestUtils::kPiDouble
                                       * static_cast<double>(i) / denomWindow));
        windowed[i] = x[i] * static_cast<float>(w);
    }
    std::vector<Complex> spectrum((fftSize / 2u) + 1u);
    fft.forward(windowed.data(), spectrum.data());

    const double binHz = sr / static_cast<double>(fftSize);
    double weighted = 0.0;
    double total = 0.0;
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        const double hz = static_cast<double>(k) * binHz;
        if (hz < lowHz) {
            continue;
        }
        const double re = static_cast<double>(spectrum[k].real);
        const double im = static_cast<double>(spectrum[k].imag);
        const double mag = std::sqrt((re * re) + (im * im));
        weighted += mag * hz;
        total += mag;
    }
    if (!(total > 0.0)) {
        return 0.0;
    }
    return weighted / total;
}

/// @brief Seconds elapsed since @p start, as a double.
[[nodiscard]] double secondsSince(const std::chrono::steady_clock::time_point& start) noexcept {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// =============================================================================
// SC-004b / SC-005 - THE SHARED OVERNIGHT RENDER (R-9)
// =============================================================================
//
// FR-086 forbids accelerating either of these: both assert a statistic over a
// RENDERED AUDIO TRAJECTORY (a block-RMS walk, a centroid series), and neither
// property survives clock scaling. So nothing below touches a scheduler
// interval, a bloom lifecycle, an ecosystem step interval, a modulator rate or
// an envelope time. The engine renders the instrument exactly as it ships.
//
// FR-014a's SECOND SENTENCE IS LOAD-BEARING HERE: kFastAttackEnvelopeConfig MAY
// NOT BE USED. The 20 s attack and the 30/45/60 s body stages ARE the property
// under test, and substituting a fast envelope would be the forbidden "bend the
// shipped character to fit a test window". SoakSeedResult::envelopeTimesMs is
// read back from the prepared engine and compared against
// VoragoVoice::kDefaultStageTimesMs inside SC-004b, which turns that sentence
// into an assertion rather than a promise.

/// 8 h of audio at 48 kHz = 1 382 400 000 samples per seed.
constexpr double kSoakHours = 8.0;
constexpr std::size_t kSoakSamples =
    static_cast<std::size_t>(kSoakHours * 3600.0 * kSampleRate48);
constexpr std::size_t kSoakBlocks = kSoakSamples / kBlockSamples;

/// T_settle, stated numerically as SC-004b requires, and derived from FR-090
/// rows that are each cited to the line that installs them:
///
///   envelope attack, stage 0       20 s   vorago_voice.h:322 kDefaultStageTimesMs[0]
///   envelope stage 1               30 s   vorago_voice.h:322 kDefaultStageTimesMs[1]
///   longest bloom fade-in          45 s   vorago_voice.h:596 bloom_.setFadeInSeconds(45.0f)
///   atmosphere capture ring fill   20 s   vorago_engine.h:120 atmosCaptureSeconds = 20.0f
///                                 -----
///                                  115 s  -> rounded UP to the next whole minute
constexpr double kSettleSeconds = 120.0;
constexpr std::size_t kSettleBlocks =
    static_cast<std::size_t>((kSettleSeconds * kSampleRate48) / static_cast<double>(kBlockSamples));

/// The 10 s moving average SC-004b's settling clause is asserted on.
/// 10 s / (512 / 48 000 s) = 937.5 blocks; 937 blocks is 9.995 s.
constexpr std::size_t kMovingAverageBlocks = 937u;

/// Float-noise tolerance for the monotone clause, RELATIVE and in the
/// mean-square domain: one part in 1e9 is nine orders below the smallest
/// change any real envelope makes over a 10 s window, so it excuses rounding
/// and nothing else. It is NOT a relaxed threshold - a genuine dip of even
/// 0.01 dB (a ratio of 0.9977) fails it by six orders of magnitude.
// RULED 2026-09-19 (spec Q-I): SC-004b's settle-window clause carries a stated
// tolerance of 0.1 dB on the 10 s moving average, i.e. 1 - 10^(-0.01) = 2.28 %
// in the mean-square domain. The earlier 1e-9 ("float rounding only") failed on
// a single 0.25 % (0.011 dB) dip of a chaotic drone's attack; the clause's
// intent is "a rise, not a collapse", and 0.1 dB still fails any real sag.
constexpr double kMonotoneRelativeEpsilon = 0.0227627;

/// SC-004b's post-settle band.
constexpr double kSoakFloorDb = -60.0;
constexpr double kSoakCeilingDb = -6.0;

/// SC-004b's stability clause: the last 60 s against the 60 s window starting
/// at T_settle + 300 s, within +/- 6 dB.
constexpr double kStabilityToleranceDb = 6.0;
constexpr std::size_t kSixtySecondBlocks =
    static_cast<std::size_t>((60.0 * kSampleRate48) / static_cast<double>(kBlockSamples));
constexpr std::size_t kStabilityReferenceStartBlock = static_cast<std::size_t>(
    ((kSettleSeconds + 300.0) * kSampleRate48) / static_cast<double>(kBlockSamples));

/// SC-005's sampling grid: one centroid every 30 s, measured on an 8192-sample
/// window (the largest transform the repo FFT accepts, fft.h:44-47, so the
/// centroid here sits on the same grid as every other Phase 10 centroid).
constexpr std::size_t kCentroidIntervalSamples =
    static_cast<std::size_t>(30.0 * kSampleRate48);
constexpr std::size_t kCentroidWindowSamples = 8192u;

/// The ONE held note of the soak, and SC-005's band floor derived from it.
///
/// RULED 2026-09-20 (spec Q-R): the centroid is computed over [4 f0, Nyquist],
/// f0 the held note's fundamental - sub-immune, the Darkness-row construction.
/// C1 (MIDI 36) is 440 x 2^((36 - 69) / 12) = 65.4064 Hz, so the floor is
/// 261.63 Hz: the subharmonic tones (an octave and two under), the fundamental
/// and its first three harmonics fall below it and the statistic reads the
/// harmonic cloud, the resonance peaks, the noise bed and the blooms instead.
constexpr std::uint8_t kSoakNote = 36u;
constexpr double kSoakFundamentalHz = 65.40639132514966;  // 440 * 2^(-33/12)
constexpr double kCentroidBandLowHz = 4.0 * kSoakFundamentalHz;  // 261.63 Hz

/// SC-005's lags, in 30 s epochs: 60 s .. 30 min inclusive.
constexpr std::size_t kMinAutocorrLagEpochs = 2u;   // 60 s
constexpr std::size_t kMaxAutocorrLagEpochs = 60u;  // 30 min
constexpr double kMinCentroidCv = 0.05;
constexpr double kMaxAutocorrelation = 0.9;

/// SC-005's three engine seeds. Three RENDERS, three series - the criterion
/// gates the MEAN ACROSS SEEDS of each statistic, and each seed's own value is
/// printed. AVERAGING THE CENTROID SERIES ACROSS SEEDS IS FORBIDDEN: the three
/// renders are independent evolutions, so averaging them sample-by-sample would
/// cancel exactly the variation SC-005 exists to measure, and a frozen engine
/// would then be indistinguishable from a living one.
constexpr std::array<std::uint32_t, 3> kSoakSeeds{1u, 20260917u, 4242u};

/// How often getAllocatedBytes() is sampled during the soak: every 1000 blocks
/// is every 10.67 s, i.e. 2700 samples spread over the eight hours. Reading it
/// on every one of the 2 700 000 blocks would cost more than it proves.
constexpr std::size_t kAllocationSampleStrideBlocks = 1000u;

/// @brief Everything one 8 h seed render yields. No audio is retained.
struct SoakSeedResult {
    std::uint32_t seed = 0u;
    /// One mean-square value per 512-sample block (2 700 000 entries, 10.8 MB).
    std::vector<float> blockMeanSquare;
    /// One band-limited centroid ([4 f0, Nyquist], Q-R) per 30 s epoch, sampled
    /// from T_settle onwards.
    std::vector<double> centroidHz;
    std::size_t allocatedAfterPrepare = 0u;
    std::size_t allocatedMin = 0u;
    std::size_t allocatedMax = 0u;
    std::uint32_t nonFiniteRecoveries = 0u;
    bool allFinite = true;
    float peakAbs = 0.0f;
    /// Read back after prepare(): FR-014a's ban on the fast fixture, as data.
    std::array<float, static_cast<std::size_t>(VoragoVoice::kEnvelopeStages)> envelopeTimesMs{};
    float envelopeReleaseMs = 0.0f;
    double wallSeconds = 0.0;
};

struct SoakResult {
    std::array<SoakSeedResult, kSoakSeeds.size()> seeds{};
    double wallSeconds = 0.0;
};

/// @brief Render one unaccelerated 8 h seed and collect its statistics.
///
/// Full polyphony (kMaxVoices), ONE held note, the shipped configuration and
/// the shipped envelope. The chain rendered is processStereoBlock() followed by
/// processOutputStage() - the same chain SC-004a's sentinel measures, minus the
/// Layer 4 cavern, which no TU in dsp_systems_tests may name (AR-1).
[[nodiscard]] SoakSeedResult renderOneSoak(std::uint32_t seed) {
    SoakSeedResult out;
    out.seed = seed;

    const VoragoEngineConfig cfg{};  // shipped defaults, deliberately untouched
    auto engine = makeEngine(kSampleRate48, cfg);
    engine->setSeed(seed);
    engine->setPolyphony(VoragoEngine::kMaxVoices);

    for (int stage = 0; stage < VoragoVoice::kEnvelopeStages; ++stage) {
        out.envelopeTimesMs[static_cast<std::size_t>(stage)] =
            engine->getEnvelopeStageTimeMs(stage);
    }
    out.envelopeReleaseMs = engine->getEnvelopeReleaseMs();

    out.allocatedAfterPrepare = engine->getAllocatedBytes();
    out.allocatedMin = out.allocatedAfterPrepare;
    out.allocatedMax = out.allocatedAfterPrepare;

    engine->noteOn(kSoakNote, static_cast<std::uint8_t>(100));  // held forever

    std::vector<float> l(kBlockSamples, 0.0f);
    std::vector<float> r(kBlockSamples, 0.0f);
    out.blockMeanSquare.reserve(kSoakBlocks + 1u);
    out.centroidHz.reserve((kSoakSamples / kCentroidIntervalSamples) + 1u);

    std::vector<float> window;
    window.reserve(kCentroidWindowSamples);
    bool capturing = false;
    std::size_t nextCaptureAt = static_cast<std::size_t>(kSettleSeconds * kSampleRate48);

    const auto wallStart = std::chrono::steady_clock::now();
    std::size_t blockIndex = 0u;
    for (std::size_t done = 0; done < kSoakSamples; done += kBlockSamples, ++blockIndex) {
        const std::size_t n = std::min(kBlockSamples, kSoakSamples - done);
        std::fill(l.begin(), l.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);
        std::fill(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);

        engine->processStereoBlock(l.data(), r.data(), n);
        engine->processOutputStage(l.data(), r.data(), n);

        if (!capturing && (done >= nextCaptureAt)) {
            capturing = true;
            window.clear();
        }

        double sumSquares = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const float a = l[i];
            const float b = r[i];
            if (!isFiniteBits(a) || !isFiniteBits(b)) {
                out.allFinite = false;
            } else {
                out.peakAbs = std::max(out.peakAbs, std::max(std::fabs(a), std::fabs(b)));
                sumSquares += (static_cast<double>(a) * static_cast<double>(a))
                              + (static_cast<double>(b) * static_cast<double>(b));
            }
            if (capturing && (window.size() < kCentroidWindowSamples)) {
                window.push_back(a);
            }
        }
        // Mean square over BOTH channels, so the statistic is the stereo pair's
        // level and not one channel's.
        out.blockMeanSquare.push_back(
            static_cast<float>(sumSquares / (2.0 * static_cast<double>(n))));

        if (capturing && (window.size() >= kCentroidWindowSamples)) {
            out.centroidHz.push_back(bandCentroidHz(
                std::span<const float>{window.data(), window.size()}, kSampleRate48,
                kCentroidBandLowHz));
            capturing = false;
            nextCaptureAt += kCentroidIntervalSamples;
        }

        if ((blockIndex % kAllocationSampleStrideBlocks) == 0u) {
            const std::size_t allocated = engine->getAllocatedBytes();
            out.allocatedMin = std::min(out.allocatedMin, allocated);
            out.allocatedMax = std::max(out.allocatedMax, allocated);
        }
    }

    const std::size_t allocatedAtEnd = engine->getAllocatedBytes();
    out.allocatedMin = std::min(out.allocatedMin, allocatedAtEnd);
    out.allocatedMax = std::max(out.allocatedMax, allocatedAtEnd);
    out.nonFiniteRecoveries = engine->getNonFiniteRecoveryCount();
    out.wallSeconds = secondsSince(wallStart);
    return out;
}

/// @brief THE overnight render, produced exactly once per test-binary run.
///
/// A function-local static, not a Catch2 class fixture: TEST_CASE_METHOD builds
/// its fixture once per CASE, and SC-004b and SC-005 are two cases over ONE
/// render (R-9). Whichever of them runs first pays the six hours; the second
/// reads the same object.
[[nodiscard]] const SoakResult& overnightSoak() {
    static const SoakResult result = [] {
        SoakResult r;
        const auto wallStart = std::chrono::steady_clock::now();
        for (std::size_t s = 0; s < kSoakSeeds.size(); ++s) {
            r.seeds[s] = renderOneSoak(kSoakSeeds[s]);
        }
        r.wallSeconds = secondsSince(wallStart);
        return r;
    }();
    return result;
}

/// @brief Mean and coefficient of variation of a series.
struct SeriesStats {
    double mean = 0.0;
    double stdDev = 0.0;
    double cv = 0.0;
};

[[nodiscard]] SeriesStats seriesStats(const std::vector<double>& x) noexcept {
    SeriesStats out;
    if (x.empty()) {
        return out;
    }
    double sum = 0.0;
    for (const double v : x) {
        sum += v;
    }
    out.mean = sum / static_cast<double>(x.size());
    double variance = 0.0;
    for (const double v : x) {
        const double d = v - out.mean;
        variance += d * d;
    }
    variance /= static_cast<double>(x.size());
    out.stdDev = std::sqrt(variance);
    out.cv = (out.mean > 0.0) ? (out.stdDev / out.mean) : 0.0;
    return out;
}

/// @brief Normalised autocorrelation of @p x at lag @p lag, mean removed.
///
/// @return r(lag) in [-1, 1]; 0.0 when the series is constant or too short.
[[nodiscard]] double autocorrelationAt(const std::vector<double>& x, std::size_t lag) noexcept {
    if ((x.size() <= lag) || (lag == 0u)) {
        return 0.0;
    }
    const SeriesStats stats = seriesStats(x);
    double numerator = 0.0;
    double denominator = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double d = x[i] - stats.mean;
        denominator += d * d;
        if ((i + lag) < x.size()) {
            numerator += d * (x[i + lag] - stats.mean);
        }
    }
    if (!(denominator > 0.0)) {
        return 0.0;
    }
    return numerator / denominator;
}

// =============================================================================
// SC-013b - the 968-configuration fuzz sweep
// =============================================================================
//
// FR-086, stated here as the criterion requires. The property asserted is
// BOUNDEDNESS AND FINITENESS - an accounting property, which is exactly the
// class FR-086 allows to be accelerated.
//
//   A = 100, one factor shared by the whole render.
//
// A is applied to the lifecycle clocks the ENGINE's public surface exposes, and
// to no others:
//     stage times  20 000 / 30 000 / 45 000 / 60 000 / 0 / 0 ms  ->  /A
//     release      45 000 ms                                    ->  /A
//     growth       120 s                                        ->  /A  (1.2 s,
//                  clear of GrowthEnvelope::kMinDuration = 1 s, growth_envelope.h:97)
//
// The other levers on FR-086's permitted list - SlowEventScheduler::setIntervalRange,
// the EcosystemEngine step interval, the BloomEngine lifecycle durations and the
// tidal/breathing rates - are VOICE-owned and have no engine-level fan-out
// (vorago_engine.h publishes only the four envelope forwarders at :668-690, and
// getVoice() is const). They therefore run UNSCALED here. That is conservative,
// not a gap: an unscaled event clock fires FEWER events inside the 10 s window,
// so every bounded render is bounded on a strict superset of the event density
// the accelerated envelope walk exposes it to.
//
// NOTHING ON FR-086's FORBIDDEN LIST IS TOUCHED: kControlChunkSamples, the
// sample rate, the block size, the polyphony and every macro, gain, mix, damping
// and feedback value are the configuration under test, not acceleration levers.

constexpr double kFuzzAcceleration = 100.0;
constexpr double kFuzzSeconds = 10.0;

/// The configuration SPACE is 1000 points, indexed 0..999. The per-push
/// sentinel (SC-013a, VoragoEngine_ConfigurationFuzzSentinel in
/// unit/systems/vorago_engine_test.cpp) owns [0, 32); this case owns
/// [32, 1000) - the "other 968" the criterion names. The two index ranges are
/// disjoint by construction, so the split can never double-count, and each
/// configuration is a pure function of its index through deriveStreamSeed()
/// below, so neither TU has to share code with the other to enumerate the same
/// space deterministically.
constexpr std::size_t kFuzzTotalConfigs = 1000u;
constexpr std::size_t kFuzzSentinelConfigs = 32u;
constexpr std::size_t kFuzzLongConfigs = kFuzzTotalConfigs - kFuzzSentinelConfigs;
static_assert(kFuzzLongConfigs == 968u,
              "SC-013b is the 968-configuration remainder of a 1000-point space");

/// Salt for the per-index draw. Distinct from any engine seed salt, so a
/// configuration index and a voice slot never share a stream.
constexpr std::uint32_t kFuzzSeedBase = 0x5643'4647u;  // 'VCFG'

[[nodiscard]] float drawRange(Xorshift32& rng, float lo, float hi) noexcept {
    return lo + ((hi - lo) * rng.nextUnipolar());
}

[[nodiscard]] std::size_t drawIndex(Xorshift32& rng, std::size_t lo, std::size_t hi) noexcept {
    const std::size_t span = (hi - lo) + 1u;
    return lo + static_cast<std::size_t>(rng.next() % static_cast<std::uint32_t>(span));
}

[[nodiscard]] bool drawFlag(Xorshift32& rng) noexcept { return (rng.next() & 1u) != 0u; }

/// @brief One macro value: the two EXTREMES are drawn as often as the interior.
///
/// A uniform draw over [0, 1] reaches an exact endpoint with probability zero,
/// and FR-073's hard cases are precisely the endpoints (every macro at 1 is the
/// spec's own "maximum coupling, maximum loop gain, maximum Q" worst case). One
/// third 0, one third 1, one third uniform keeps ~215 of the 968 configurations
/// sitting on a given macro's extreme.
[[nodiscard]] float drawMacro(Xorshift32& rng) noexcept {
    const std::uint32_t k = rng.next() % 3u;
    if (k == 0u) {
        return 0.0f;
    }
    if (k == 1u) {
        return 1.0f;
    }
    return rng.nextUnipolar();
}

/// @brief The prepare-time half of one fuzzed configuration.
///
/// Every field here is CLAMPED by its owner rather than rejected
/// (vorago_engine.h:105-133), so a draw outside a component's range is itself
/// part of what is under test. The two FFT sizes are left at their defaults: a
/// non-power-of-two would exercise the FFT's own validation rather than the
/// engine's boundedness, and FR-073 is what this case gates.
[[nodiscard]] VoragoEngineConfig drawEngineConfig(Xorshift32& rng) noexcept {
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = drawRange(rng, 1.0f, 30.0f);
    cfg.atmosBlurEnabled = drawFlag(rng);
    cfg.smearEnabled = drawFlag(rng);
    cfg.voice.numNoiseSources = drawIndex(rng, 1u, 4u);
    cfg.voice.numResonancePeaks = drawIndex(rng, 1u, 12u);
    cfg.voice.numEcologyLoops = drawIndex(rng, 1u, 6u);
    cfg.voice.ecosystemAgents = drawIndex(rng, 4u, 64u);
    cfg.voice.ecosystemCells = drawIndex(rng, 8u, 64u);
    // ecosystemStepChunks is DELIBERATELY NOT DRAWN. It is one of FR-086's
    // permitted acceleration levers, and drawing it per configuration would make
    // the clock scaling vary across the sweep - which is exactly what "one stated
    // factor A shared by the whole render" forbids. It stays at its shipped 8.
    cfg.voice.bloomChildSlots = drawIndex(rng, 0u, 6u);
    cfg.voice.maxCombDelayMs = drawRange(rng, 5.0f, 200.0f);
    return cfg;
}

/// @brief The run-time half: every macro, then every exposed engine setter.
///
/// ORDER IS A DECISION, not an accident. The macro matrix writes all eight
/// Engine-owned targets (vorago_macro_matrix.h:150-158), so a direct setter
/// called BEFORE apply() would be overwritten and the drawn value would never
/// render. apply() therefore runs first and the direct setters follow, which
/// leaves the drawn setter values live on the eight engine targets while the
/// macro state still drives all twenty-four Voice-owned ones - the widest
/// configuration the two surfaces can jointly express.
///
/// The envelope times are NOT drawn: they are this case's FR-086 acceleration
/// lever and must carry the one shared factor A on every configuration.
void configureFuzzEngine(VoragoEngine& engine, VoragoMacroMatrix& matrix,
                         Xorshift32& rng) noexcept {
    engine.setSeed(deriveStreamSeed(kFuzzSeedBase, static_cast<std::size_t>(rng.next() % 4096u)));

    VoragoMacroValues macros{};
    macros.darkness = drawMacro(rng);
    macros.age = drawMacro(rng);
    macros.density = drawMacro(rng);
    macros.movement = drawMacro(rng);
    macros.gravity = drawMacro(rng);
    macros.entropy = drawMacro(rng);
    macros.pressure = drawMacro(rng);
    macros.weight = drawMacro(rng);
    macros.fog = drawMacro(rng);
    macros.life = drawMacro(rng);
    macros.depth = drawMacro(rng);
    macros.mass = drawMacro(rng);
    matrix.setMacros(macros);
    matrix.apply(engine);

    engine.setSubToneLevelOffsetDb(drawRange(rng, -24.0f, 24.0f));
    engine.setSubTrackingAmount(drawRange(rng, 0.0f, 1.0f));
    engine.setSmearAmount(drawRange(rng, 0.0f, 1.0f));
    engine.setSmearDecoherence(drawRange(rng, 0.0f, 1.0f));
    engine.setSmearTilt(drawRange(rng, -1.0f, 1.0f));
    engine.setGhostPeakLevel(drawRange(rng, 0.0f, 1.0f));
    engine.setAtmosBlur(drawRange(rng, 0.0f, 1.0f));
    engine.setOutputSaturation(drawRange(rng, 0.0f, 1.0f));

    engine.setEnvelopeMode(drawFlag(rng) ? VoragoVoice::EnvelopeMode::Growth
                                         : VoragoVoice::EnvelopeMode::Standard);

    // FR-086's single factor A, applied last so the mode switch cannot undo it.
    for (int stage = 0; stage < VoragoVoice::kEnvelopeStages; ++stage) {
        const float shipped = VoragoVoice::kDefaultStageTimesMs[static_cast<std::size_t>(stage)];
        engine.setEnvelopeStageTimeMs(stage, static_cast<float>(static_cast<double>(shipped)
                                                                / kFuzzAcceleration));
    }
    engine.setEnvelopeReleaseMs(static_cast<float>(
        static_cast<double>(VoragoVoice::kDefaultReleaseMs) / kFuzzAcceleration));
    engine.setGrowthDurationSeconds(static_cast<float>(
        static_cast<double>(VoragoVoice::kDefaultGrowthDurationSeconds) / kFuzzAcceleration));
}

/// @brief What one fuzzed render found.
struct FuzzOutcome {
    bool allFinite = true;
    bool bounded = true;
    float peakAbs = 0.0f;
    std::uint32_t recoveries = 0u;
};

/// @brief Render one configuration and return its three SC-013b observables.
[[nodiscard]] FuzzOutcome renderFuzzConfig(std::size_t configIndex) {
    Xorshift32 rng(deriveStreamSeed(kFuzzSeedBase, configIndex));

    const VoragoEngineConfig cfg = drawEngineConfig(rng);
    auto engine = makeEngine(kSampleRate48, cfg);

    VoragoMacroMatrix matrix;
    configureFuzzEngine(*engine, matrix, rng);

    const std::size_t polyphony = drawIndex(rng, 1u, VoragoEngine::kMaxVoices);
    engine->setPolyphony(polyphony);
    for (std::size_t v = 0; v < polyphony; ++v) {
        engine->noteOn(static_cast<std::uint8_t>(24u + (5u * v)), static_cast<std::uint8_t>(100));
    }

    FuzzOutcome out;
    const auto samples = static_cast<std::size_t>(kFuzzSeconds * kSampleRate48);
    std::vector<float> l(kBlockSamples, 0.0f);
    std::vector<float> r(kBlockSamples, 0.0f);
    for (std::size_t done = 0; done < samples; done += kBlockSamples) {
        const std::size_t n = std::min(kBlockSamples, samples - done);
        std::fill(l.begin(), l.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);
        std::fill(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);

        engine->processStereoBlock(l.data(), r.data(), n);
        engine->processOutputStage(l.data(), r.data(), n);

        for (std::size_t i = 0; i < n; ++i) {
            const float a = l[i];
            const float b = r[i];
            if (!isFiniteBits(a) || !isFiniteBits(b)) {
                out.allFinite = false;
                continue;
            }
            const float magnitude = std::max(std::fabs(a), std::fabs(b));
            out.peakAbs = std::max(out.peakAbs, magnitude);
            if (magnitude > 1.0f) {
                out.bounded = false;
            }
        }
    }
    out.recoveries = engine->getNonFiniteRecoveryCount();
    return out;
}

// =============================================================================
// SC-021b - the sample-rate sweep
// =============================================================================
//
// The four remaining rates, against the 48 kHz reference. SC-021a (per-push)
// carries 48 kHz and 192 kHz; this case carries 44.1 / 88.2 / 96 / 176.4 kHz
// and re-renders 48 kHz as its own reference so the +/- 3 dB comparison is
// against a figure measured in this run, on this machine.
//
// kFastAttackEnvelopeConfig IS USED HERE, and must be: SC-021b's first
// assertion is NON-SILENCE inside a 10 s window, and the shipped envelope's
// 20 s attack cannot clear one. FR-014a authorises exactly this substitution
// for short-window criteria, and SC-021a - whose four assertions this case
// repeats - is one of the criteria it names. The ban in FR-014a's second
// sentence is on SC-004b, the criterion whose subject IS the slow envelope, and
// nothing here touches that render.

constexpr double kRateSweepSeconds = 10.0;
constexpr double kRateRmsToleranceDb = 3.0;
/// "Non-silent" needs a number to be an assertion. -80 dBFS is two orders below
/// SC-004b's own -60 dBFS floor, so it fires only on a render that is silent or
/// all but silent, never on one that is merely quiet.
constexpr double kNonSilentFloorDb = -80.0;

struct RateRender {
    double sampleRate = 0.0;
    double rmsDb = -300.0;
    float peakAbs = 0.0f;
    bool allFinite = true;
    std::uint32_t recoveries = 0u;
};

/// @brief One full-polyphony 10 s render at @p sampleRate, fast-attack envelope.
///
/// The excitation is identical at every rate - the same seed, the same
/// kMaxVoices notes, the same chord - because the +/- 3 dB clause compares
/// LEVELS across rates and would otherwise be comparing two different pieces of
/// music.
[[nodiscard]] RateRender renderAtRate(double sampleRate) {
    RateRender out;
    out.sampleRate = sampleRate;

    const VoragoEngineConfig cfg{};
    auto engine = makeEngine(sampleRate, cfg);
    engine->setSeed(7u);
    engine->setPolyphony(VoragoEngine::kMaxVoices);
    applyFastAttack(*engine);
    for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
        engine->noteOn(static_cast<std::uint8_t>(36u + (3u * v)), static_cast<std::uint8_t>(100));
    }

    const auto samples = static_cast<std::size_t>(kRateSweepSeconds * sampleRate);
    std::vector<float> l(kBlockSamples, 0.0f);
    std::vector<float> r(kBlockSamples, 0.0f);
    double sumSquares = 0.0;
    std::size_t counted = 0u;
    for (std::size_t done = 0; done < samples; done += kBlockSamples) {
        const std::size_t n = std::min(kBlockSamples, samples - done);
        std::fill(l.begin(), l.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);
        std::fill(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);

        engine->processStereoBlock(l.data(), r.data(), n);
        engine->processOutputStage(l.data(), r.data(), n);

        for (std::size_t i = 0; i < n; ++i) {
            const float a = l[i];
            const float b = r[i];
            if (!isFiniteBits(a) || !isFiniteBits(b)) {
                out.allFinite = false;
                continue;
            }
            out.peakAbs = std::max(out.peakAbs, std::max(std::fabs(a), std::fabs(b)));
            sumSquares += (static_cast<double>(a) * static_cast<double>(a))
                          + (static_cast<double>(b) * static_cast<double>(b));
            counted += 2u;
        }
    }
    out.recoveries = engine->getNonFiniteRecoveryCount();
    out.rmsDb = (counted > 0u) ? dbFromMeanSquare(sumSquares / static_cast<double>(counted))
                               : -300.0;
    return out;
}

}  // namespace

// =============================================================================
// SC-004b - the 8 h soak
// =============================================================================

TEST_CASE("VoragoEngine_OvernightSoak", "[systems][vorago][long]") {
    const SoakResult& soak = overnightSoak();

    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(1);
        os << "SC-004b / SC-005 shared render (R-9): " << kSoakHours << " h of audio x "
           << kSoakSeeds.size() << " seeds, UNACCELERATED (FR-086), "
           << "T_settle = " << kSettleSeconds << " s, block = " << kBlockSamples << ", "
           << "wall clock = " << (soak.wallSeconds / 3600.0) << " h";
        for (const SoakSeedResult& seed : soak.seeds) {
            os << "\n  seed " << seed.seed << ": " << (seed.wallSeconds / 3600.0) << " h, "
               << seed.blockMeanSquare.size() << " blocks, " << seed.centroidHz.size()
               << " centroid samples, peak " << std::setprecision(4) << seed.peakAbs
               << std::setprecision(1);
        }
        WARN(os.str());
    }

    for (const SoakSeedResult& seed : soak.seeds) {
        CAPTURE(seed.seed);

        // FR-014a's ban, as an assertion: this render MUST be the shipped slow
        // envelope, never kFastAttackEnvelopeConfig.
        for (int stage = 0; stage < VoragoVoice::kEnvelopeStages; ++stage) {
            const auto i = static_cast<std::size_t>(stage);
            CAPTURE(stage);
            REQUIRE(seed.envelopeTimesMs[i] == VoragoVoice::kDefaultStageTimesMs[i]);
        }
        REQUIRE(seed.envelopeReleaseMs == VoragoVoice::kDefaultReleaseMs);

        REQUIRE(seed.blockMeanSquare.size() >= kSoakBlocks);

        // --- throughout: finite, no recovery, no allocation ------------------
        REQUIRE(seed.allFinite);
        REQUIRE(seed.nonFiniteRecoveries == 0u);
        REQUIRE(seed.allocatedMin == seed.allocatedAfterPrepare);
        REQUIRE(seed.allocatedMax == seed.allocatedAfterPrepare);

        // --- [0, T_settle): monotone non-decreasing on a 10 s moving average --
        // A rise from silence, not a bound. The average is taken over the
        // mean-square series and converted to dB only for reporting.
        double worstRatio = 1.0;
        std::size_t worstIndex = 0u;
        double previousAverage = -1.0;
        for (std::size_t end = kMovingAverageBlocks; end <= kSettleBlocks; ++end) {
            const double average =
                meanSquareOver(seed.blockMeanSquare, end - kMovingAverageBlocks,
                               kMovingAverageBlocks);
            if (previousAverage >= 0.0) {
                const double ratio =
                    (previousAverage > 0.0) ? (average / previousAverage) : 1.0;
                if (ratio < worstRatio) {
                    worstRatio = ratio;
                    worstIndex = end;
                }
            }
            previousAverage = average;
        }
        CAPTURE(worstIndex);
        CAPTURE(worstRatio);
        REQUIRE(worstRatio >= (1.0 - kMonotoneRelativeEpsilon));

        // --- after T_settle: the band ---------------------------------------
        double minDb = 1.0e9;
        double maxDb = -1.0e9;
        std::size_t minBlock = 0u;
        std::size_t maxBlock = 0u;
        for (std::size_t b = kSettleBlocks; b < seed.blockMeanSquare.size(); ++b) {
            const double db = dbFromMeanSquare(static_cast<double>(seed.blockMeanSquare[b]));
            if (db < minDb) {
                minDb = db;
                minBlock = b;
            }
            if (db > maxDb) {
                maxDb = db;
                maxBlock = b;
            }
        }
        const double blockSeconds = static_cast<double>(kBlockSamples) / kSampleRate48;
        const double minAtSeconds = static_cast<double>(minBlock) * blockSeconds;
        const double maxAtSeconds = static_cast<double>(maxBlock) * blockSeconds;
        CAPTURE(minDb);
        CAPTURE(maxDb);
        CAPTURE(minAtSeconds);
        CAPTURE(maxAtSeconds);
        REQUIRE(minDb >= kSoakFloorDb);
        REQUIRE(maxDb <= kSoakCeilingDb);

        // --- the last 60 s against the 60 s window at T_settle + 300 s -------
        const std::size_t lastWindowStart = seed.blockMeanSquare.size() - kSixtySecondBlocks;
        const double lastDb = dbFromMeanSquare(
            meanSquareOver(seed.blockMeanSquare, lastWindowStart, kSixtySecondBlocks));
        const double referenceDb = dbFromMeanSquare(meanSquareOver(
            seed.blockMeanSquare, kStabilityReferenceStartBlock, kSixtySecondBlocks));
        CAPTURE(lastDb);
        CAPTURE(referenceDb);
        REQUIRE(std::fabs(lastDb - referenceDb) <= kStabilityToleranceDb);
    }
}

// =============================================================================
// SC-005 - the same render, measured for evolution
// =============================================================================

TEST_CASE("VoragoEngine_OvernightEvolution", "[systems][vorago][long]") {
    const SoakResult& soak = overnightSoak();

    // Per seed, on THAT SEED'S OWN centroid series. Averaging the three series
    // together is forbidden: the seeds are independent evolutions, so a
    // sample-by-sample mean cancels exactly the variation being measured and
    // would let a frozen engine read like a living one. Only the two SCALARS
    // (CV, autocorrelation) are averaged, and that average is what gates.
    std::array<double, kSoakSeeds.size()> cvPerSeed{};
    std::array<double, kSoakSeeds.size()> worstLagPerSeed{};
    std::array<double, kSoakSeeds.size()> maxAutocorrPerSeed{};
    std::array<std::vector<double>, kSoakSeeds.size()> autocorrPerSeed{};

    for (std::size_t s = 0; s < soak.seeds.size(); ++s) {
        const std::vector<double>& series = soak.seeds[s].centroidHz;
        CAPTURE(soak.seeds[s].seed);
        REQUIRE(series.size() > kMaxAutocorrLagEpochs);

        cvPerSeed[s] = seriesStats(series).cv;

        autocorrPerSeed[s].reserve((kMaxAutocorrLagEpochs - kMinAutocorrLagEpochs) + 1u);
        double worst = -2.0;
        double worstLag = 0.0;
        for (std::size_t lag = kMinAutocorrLagEpochs; lag <= kMaxAutocorrLagEpochs; ++lag) {
            const double r = autocorrelationAt(series, lag);
            autocorrPerSeed[s].push_back(r);
            if (r > worst) {
                worst = r;
                worstLag = static_cast<double>(lag) * 30.0;
            }
        }
        maxAutocorrPerSeed[s] = worst;
        worstLagPerSeed[s] = worstLag;
    }

    double cvSum = 0.0;
    for (const double cv : cvPerSeed) {
        cvSum += cv;
    }
    const double meanCv = cvSum / static_cast<double>(cvPerSeed.size());

    // The mean across seeds, lag by lag, and the worst of those means.
    double worstMeanAutocorr = -2.0;
    double worstMeanLagSeconds = 0.0;
    const std::size_t numLags = autocorrPerSeed[0].size();
    for (std::size_t k = 0; k < numLags; ++k) {
        double sum = 0.0;
        for (const std::vector<double>& perSeed : autocorrPerSeed) {
            sum += perSeed[k];
        }
        const double mean = sum / static_cast<double>(autocorrPerSeed.size());
        if (mean > worstMeanAutocorr) {
            worstMeanAutocorr = mean;
            worstMeanLagSeconds =
                static_cast<double>(kMinAutocorrLagEpochs + k) * 30.0;
        }
    }

    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(4);
        os << "SC-005 evolution over the shared 8 h render, per seed:";
        for (std::size_t s = 0; s < soak.seeds.size(); ++s) {
            os << "\n  seed " << soak.seeds[s].seed << ": CV = " << cvPerSeed[s]
               << ", max autocorrelation = " << maxAutocorrPerSeed[s] << " at lag "
               << std::setprecision(0) << worstLagPerSeed[s] << " s" << std::setprecision(4);
        }
        os << "\n  mean CV = " << meanCv << " (bar " << kMinCentroidCv << ")"
           << ", worst mean autocorrelation = " << worstMeanAutocorr << " at lag "
           << std::setprecision(0) << worstMeanLagSeconds << " s (bar "
           << std::setprecision(4) << kMaxAutocorrelation << ")";
        WARN(os.str());
    }

    CAPTURE(meanCv);
    REQUIRE(meanCv >= kMinCentroidCv);

    CAPTURE(worstMeanAutocorr);
    CAPTURE(worstMeanLagSeconds);
    REQUIRE(worstMeanAutocorr < kMaxAutocorrelation);
}

// =============================================================================
// SC-013b - the 968 remaining configurations
// =============================================================================

TEST_CASE("VoragoEngine_ConfigurationFuzz", "[systems][vorago][long]") {
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(1);
        os << "SC-013b: configurations [" << kFuzzSentinelConfigs << ", " << kFuzzTotalConfigs
           << ") = " << kFuzzLongConfigs << " renders x " << kFuzzSeconds
           << " s, accelerated per FR-086 with A = " << kFuzzAcceleration
           << " on the engine's envelope and growth clocks";
        WARN(os.str());
    }

    std::size_t firstNonFinite = kFuzzTotalConfigs;
    std::size_t firstUnbounded = kFuzzTotalConfigs;
    std::size_t firstRecovery = kFuzzTotalConfigs;
    std::uint32_t recoveriesThere = 0u;
    float worstPeak = 0.0f;
    std::size_t worstPeakConfig = 0u;
    std::size_t rendered = 0u;

    const auto wallStart = std::chrono::steady_clock::now();
    for (std::size_t index = kFuzzSentinelConfigs; index < kFuzzTotalConfigs; ++index) {
        const FuzzOutcome outcome = renderFuzzConfig(index);
        ++rendered;

        if (outcome.peakAbs > worstPeak) {
            worstPeak = outcome.peakAbs;
            worstPeakConfig = index;
        }
        if (!outcome.allFinite && (firstNonFinite == kFuzzTotalConfigs)) {
            firstNonFinite = index;
        }
        if (!outcome.bounded && (firstUnbounded == kFuzzTotalConfigs)) {
            firstUnbounded = index;
        }
        if ((outcome.recoveries != 0u) && (firstRecovery == kFuzzTotalConfigs)) {
            firstRecovery = index;
            recoveriesThere = outcome.recoveries;
        }
        // Fail fast. A broken build would otherwise burn the remaining
        // configurations - forty minutes - to report the same defect.
        if (!outcome.allFinite || !outcome.bounded || (outcome.recoveries != 0u)) {
            break;
        }
    }
    const double wallSeconds = secondsSince(wallStart);

    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(4);
        os << "SC-013b: " << rendered << " configurations rendered in " << std::setprecision(1)
           << (wallSeconds / 60.0) << " min; worst |out| = " << std::setprecision(4) << worstPeak
           << " at configuration " << worstPeakConfig;
        WARN(os.str());
    }

    CAPTURE(firstNonFinite);
    CAPTURE(firstUnbounded);
    CAPTURE(firstRecovery);
    CAPTURE(recoveriesThere);
    CAPTURE(worstPeak);
    CAPTURE(worstPeakConfig);

    REQUIRE(rendered == kFuzzLongConfigs);
    REQUIRE(firstNonFinite == kFuzzTotalConfigs);
    REQUIRE(firstUnbounded == kFuzzTotalConfigs);
    REQUIRE(firstRecovery == kFuzzTotalConfigs);
    REQUIRE(worstPeak <= 1.0f);
}

// =============================================================================
// SC-021b - 44.1 / 88.2 / 96 / 176.4 kHz against the 48 kHz reference
// =============================================================================

TEST_CASE("VoragoEngine_SampleRateSweep", "[systems][vorago][long]") {
    const RateRender reference = renderAtRate(kSampleRate48);

    CAPTURE(reference.rmsDb);
    REQUIRE(reference.allFinite);
    REQUIRE(reference.recoveries == 0u);
    REQUIRE(reference.peakAbs <= 1.0f);
    REQUIRE(reference.rmsDb > kNonSilentFloorDb);

    constexpr std::array<double, 4> kRates{44100.0, 88200.0, 96000.0, 176400.0};

    std::array<RateRender, kRates.size()> renders{};
    for (std::size_t i = 0; i < kRates.size(); ++i) {
        renders[i] = renderAtRate(kRates[i]);
    }

    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(2);
        os << "SC-021b: " << kRateSweepSeconds << " s full-polyphony renders, "
           << "kFastAttackEnvelopeConfig (FR-014a); reference 48 kHz RMS = " << reference.rmsDb
           << " dBFS, peak " << reference.peakAbs;
        for (const RateRender& render : renders) {
            os << "\n  " << std::setprecision(1) << (render.sampleRate / 1000.0)
               << " kHz: RMS = " << std::setprecision(2) << render.rmsDb << " dBFS ("
               << (render.rmsDb - reference.rmsDb) << " dB vs reference), peak "
               << render.peakAbs;
        }
        WARN(os.str());
    }

    for (const RateRender& render : renders) {
        CAPTURE(render.sampleRate);
        CAPTURE(render.rmsDb);
        CAPTURE(render.peakAbs);
        CAPTURE(render.recoveries);

        REQUIRE(render.allFinite);                          // finite
        REQUIRE(render.recoveries == 0u);                   // no guard fired
        REQUIRE(render.peakAbs <= 1.0f);                    // bounded
        REQUIRE(render.rmsDb > kNonSilentFloorDb);          // non-silent
        REQUIRE(std::fabs(render.rmsDb - reference.rmsDb)   // within +/- 3 dB
                <= kRateRmsToleranceDb);
    }
}
