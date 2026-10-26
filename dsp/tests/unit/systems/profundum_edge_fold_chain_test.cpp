// ==============================================================================
// Layer 3: System Tests - Profundum excitation + HarmonicFold chain
//                                    (Profundum Phase 2, specs/profundum-phase2-edge-fold)
// ==============================================================================
// Reference: specs/profundum-phase2-edge-fold/plan.md (S8.1, S8.2, S9.4, S9.6)
//            specs/profundum-phase2-edge-fold/tasks.md (T006 wires this TU,
//                                                       T015 fills it)
//
// SCOPE OF THIS TU (plan S8.1/S8.2): SC-005 grid and positive control, the
//   core-input arms of SC-010/011/012/013, SC-017 (SampleRateChange), SC-019
//   (excitation + chain), SC-022 recorded figures, ProfundumEdgeFold_HelperParity,
//   [.listen] renders.
//
// Chain = ProfundumCore (bank -> excitation) -> HarmonicFold, per channel, with the
// Fold's setFundamentalHz = the played f0. Every steady window starts at t_settle =
// max(0.5 s, 8·tau_slow) computed from the header constants of the stages under test
// (tasks.md rule 9): excitation DC 2 Hz and its 5 ms smoothers; Fold DC 2 Hz, detector
// release kDetectorReleasePeriods / f0 and the 10 ms amount smoother.
//
// T015 cases:
//   ProfundumEdgeFold_HelperParity (plan S8.1 drift guard)
//   SC-005: ProfundumCore_ExcitationNoAliasing, HarmonicFold_NoAliasing,
//     ProfundumEdgeFold_ChainNoAliasing (per-push smoke, E1 / 48 kHz) and their [long]
//     ...E0 / ...E1 / ...E2 shards (that note at 44.1 / 48 / 96 kHz);
//     ProfundumEdgeFold_AliasGatePositiveControl (plan S1 C-6)
//   SC-010 core arm: HarmonicFold_TransparentAtZeroCoreInput
//   SC-011: HarmonicFold_DcFreeOnExcitationOutput, ProfundumEdgeFold_ChainDcFree
//   SC-012(d) core arm: HarmonicFold_FundamentalPreservedCoreInput
//   SC-013(b): ProfundumCore_ExcitationMonoSum (plan S1 C-2)
//   SC-013(c) first clause: HarmonicFold_MonoSum (plan S1 C-10; prints SC-022(h))
//   SC-017: ProfundumEdgeFold_SampleRateChange
//   SC-019: ProfundumCore_ExcitationOutputBounded [long] + ...Smoke
//   SC-022: ProfundumEdgeFold_RecordedAliasingByOctave [long] (a),
//     ProfundumEdgeFold_RecordedAliasingRateExtremes (b),
//     ProfundumCore_ExcitationRecordedPhaseAt8Hz (c),
//     ProfundumCore_ExcitationRecordedAsymmetryPhase (e, plan S1 C-2),
//     HarmonicFold_RecordedLevelNonSine (f, plan S1 C-8); (h) by HarmonicFold_MonoSum
//   ProfundumEdgeFold_ListeningRenders [.listen] (not a gate)
//
// Renames (tasks.md deviation 6): the systems arms of SC-010/011/012(d) carry the
// ...CoreInput / ...OnExcitationOutput suffix so no name means two things across the
// processors and systems binaries.
//
// Every core render with an all-zero pan vector calls PT::requireLREqual (tasks.md
// rule 8); the Fold fed L == R is asserted L == R too (SC-013(a)), so its aliasing and DC
// readings on the left channel are the readings of both channels.
// ==============================================================================

#include "profundum_excitation_test_helpers.h"

#include "../processors/harmonic_fold_test_helpers.h"

#include "low_frequency_metrics.h"

#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using Krate::DSP::HarmonicFold;
using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;
namespace hft = Krate::DSP::HarmonicFoldTest;

// SC-019: both ceilings below the core's effective clamp 2·kBankHeadroom = 8 (plan S8.2).
static_assert(HarmonicFold::kOutputCeiling < 2.0f * ProfundumCore::kBankHeadroom, "SC-019");
static_assert(ProfundumCore::kExcitationCeiling < 2.0f * ProfundumCore::kBankHeadroom, "SC-019");

namespace {

using Controls = SpectralShapeRecipe::Controls;
using Excitation = ProfundumCore::ExcitationControls;
using Policy = ProfundumCore::RetriggerPhase;
using Stereo = std::pair<std::vector<float>, std::vector<float>>;

constexpr double kFs = 48000.0;
constexpr int kNoteE0 = 16;   // 20.6017 Hz
constexpr int kNoteE1 = 28;   // 41.2034 Hz
constexpr int kNoteC2 = 36;   // 65.4064 Hz
constexpr int kNoteE2 = 40;   // 82.4069 Hz
constexpr std::size_t kHostBlock = 512;
constexpr std::size_t kLatchGrid = ProfundumCore::kControlInterval;
constexpr double kAliasBarDbfs = -80.0;
constexpr double kPi = PT::kTwoPi * 0.5;
constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};

constexpr Excitation kExcitationOff{.mix = 0.0f, .drive = 0.5f, .asymmetry = 0.0f};
/// "Max excitation" (spec Notation): mix 1, drive 1, asymmetry 1.
constexpr Excitation kMaxExcitation{.mix = 1.0f, .drive = 1.0f, .asymmetry = 1.0f};
/// The second SC-005 excitation fixture: drive 1, asymmetry 0, mix 1.
constexpr Excitation kSymmetricMaxDrive{.mix = 1.0f, .drive = 1.0f, .asymmetry = 0.0f};
/// SC-012(a) / SC-022(c): drive 0, mix 1.
constexpr Excitation kDriveZero{.mix = 1.0f, .drive = 0.0f, .asymmetry = 0.0f};

/// MIDI note -> Hz (A4 = 440), rounded once to the float the core and the Fold receive. Every
/// analysis uses static_cast<double> of this float, so render and analysis agree on f0.
float midiHz(int note) {
    return static_cast<float>(440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0));
}

double degrees(double rad) { return rad * 180.0 / kPi; }

/// Finiteness of a double by bit pattern (this TU is built with -ffast-math; tasks.md rule 2).
bool finiteBits(double v) {
    return (std::bit_cast<std::uint64_t>(v) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

bool sameBits(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }

bool sameBits(const std::complex<double>& a, const std::complex<double>& b) {
    return sameBits(a.real(), b.real()) && sameBits(a.imag(), b.imag());
}

bool sameBits(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
}

bool sameBits(const std::vector<std::complex<double>>& a, const std::vector<std::complex<double>>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (!sameBits(a[k], b[k]))
            return false;
    }
    return true;
}

/// The Phase 1 SC-009 state: kSawAnchor with edge = 1, shift = +1 (spec Notation).
Controls sc009State() {
    Controls c = SpectralShapeRecipe::kSawAnchor;
    c.edge = 1.0f;
    c.shift = 1.0f;
    return c;
}

struct NamedInput {
    const char* name;
    Controls controls;
};

/// The SC-005 inputs (spec "SC-005 method").
std::array<NamedInput, 5> aliasInputs() {
    return {{{"SC-009 state", sc009State()},
             {"kGrowl", SpectralShapeRecipe::kGrowl},
             {"kSawAnchor", SpectralShapeRecipe::kSawAnchor},
             {"mid grid", SpectralShapeRecipe::kDefaultControls},
             {"kSineAnchor", SpectralShapeRecipe::kSineAnchor}}};
}

// ---- t_settle (tasks.md rule 9, from the header constants) ----

double excitationSettleSeconds() {
    const double tauDc = 1.0 / (PT::kTwoPi * static_cast<double>(ProfundumCore::kExcitationDcCornerHz));
    const double tauSmooth = static_cast<double>(ProfundumCore::kExcitationSmoothMs) * 1.0e-3;
    return std::max(0.5, 8.0 * std::max(tauDc, tauSmooth));
}

double foldSettleSeconds(double f0) {
    const double tauDc = 1.0 / (PT::kTwoPi * static_cast<double>(HarmonicFold::kDcCornerHz));
    const double tauRelease = static_cast<double>(HarmonicFold::kDetectorReleasePeriods) / f0;
    const double tauSmooth = static_cast<double>(HarmonicFold::kAmountSmoothMs) * 1.0e-3;
    return std::max(0.5, 8.0 * std::max({tauDc, tauRelease, tauSmooth}));
}

double chainSettleSeconds(double f0) { return std::max(excitationSettleSeconds(), foldSettleSeconds(f0)); }

std::size_t toSamples(double seconds, double fs) { return static_cast<std::size_t>(std::ceil(seconds * fs)); }

// ---- stages ----

/// A Reset note of exactly `total` samples on a prepared core (setExcitation, policy,
/// controls, noteOn, 32-sample blocks), zero pan: L == R asserted (tasks.md rule 8).
PT::Render renderCoreStage(ProfundumCore& core, const Controls& c, const Excitation& ex, float f0,
                           std::size_t total, double fs) {
    PT::Render r = PT::renderExcited(core, c, ex, f0, static_cast<double>(total) / fs);
    REQUIRE(r.L.size() == total);
    PT::requireLREqual(r.L.data(), r.R.data(), total);
    return r;
}

/// Re-arms a prepared Fold: amount (snapped by reset), the played f0, state cleared.
void armFold(HarmonicFold& fold, float amount, float f0) {
    fold.setAmount(amount);
    fold.setFundamentalHz(f0);
    fold.reset();
    REQUIRE(fold.amount() == amount);
}

/// Stereo Fold stage over a rendered pair in kHostBlock-sample host blocks.
Stereo foldStage(HarmonicFold& fold, float amount, float f0, const std::vector<float>& L,
                 const std::vector<float>& R) {
    armFold(fold, amount, f0);
    return hft::renderFold(fold, L, R, kHostBlock);
}

/// One-channel Fold stage (right == left processes channel 0 only).
std::vector<float> foldMono(HarmonicFold& fold, float amount, float f0, const std::vector<float>& x) {
    armFold(fold, amount, f0);
    std::vector<float> y = x;
    for (std::size_t pos = 0; pos < y.size(); pos += kHostBlock) {
        const std::size_t n = std::min(kHostBlock, y.size() - pos);
        fold.processBlock(y.data() + pos, y.data() + pos, n);
    }
    return y;
}

void requireStereoIdentical(const Stereo& y) {
    REQUIRE(y.first.size() == y.second.size());
    PT::requireLREqual(y.first.data(), y.second.data(), y.first.size());
}

/// The chain: max excitation -> Fold 100 (spec "SC-005 method"), L == R asserted.
Stereo renderChain(ProfundumCore& core, HarmonicFold& fold, const Controls& c, float f0, std::size_t total,
                   double fs) {
    const PT::Render r = renderCoreStage(core, c, kMaxExcitation, f0, total, fs);
    Stereo y = foldStage(fold, 100.0f, f0, r.L, r.R);
    requireStereoIdentical(y);
    return y;
}

// ---- SC-005 ----

struct AliasReading {
    double dbfs;
    double excludedFraction;
};

/// aliasedPower on the last fftLen samples (spec "SC-005 method"); guards: excluded fraction
/// <= 50 % and a finite reading.
AliasReading measureAliasTail(const std::vector<float>& y, std::size_t fftLen, double fs, double f0) {
    REQUIRE(y.size() >= fftLen);
    const PT::AliasResult a = PT::aliasedPower(y.data() + (y.size() - fftLen), fftLen, fs, f0);
    REQUIRE(a.excludedFraction <= 0.5);
    REQUIRE(finiteBits(a.aliasedDbfs));
    return AliasReading{.dbfs = a.aliasedDbfs, .excludedFraction = a.excludedFraction};
}

enum class Family : std::uint8_t { Excitation, Fold, Chain };

const char* familyName(Family f) {
    switch (f) {
        case Family::Excitation: return "excitation";
        case Family::Fold: return "Fold";
        case Family::Chain: return "chain";
    }
    return "?";
}

/// One SC-005 fixture family at (fs, f0) over the five inputs, on caller-prepared instances
/// (fresh ones for the grid, re-prepared ones for SC-017). Fixtures: excitation at max and at
/// (drive 1, asymmetry 0), mix 1; Fold at {kSoftEnd, kCompressionEnd, 100} fed the core with the
/// excitation off; the chain max excitation -> Fold 100. FFT length aliasSafeFftLength; each
/// frame starts at t_settle. With `gate`, every cell is CHECKed <= -80 dBFS. Returns the worst.
double runAliasFamily(Family fam, ProfundumCore& core, HarmonicFold& fold, double fs, float f0, bool gate) {
    const auto f0d = static_cast<double>(f0);
    const std::size_t fftLen = PT::aliasSafeFftLength(fs, f0d);
    double settleSec = chainSettleSeconds(f0d);
    if (fam == Family::Excitation)
        settleSec = excitationSettleSeconds();
    else if (fam == Family::Fold)
        settleSec = foldSettleSeconds(f0d);
    const std::size_t total = toSamples(settleSec, fs) + fftLen;
    double worst = -1.0e300;

    const auto record = [&](const char* input, const std::string& fixture, const std::vector<float>& y) {
        const AliasReading a = measureAliasTail(y, fftLen, fs, f0d);
        INFO(familyName(fam) << " " << fixture << ", " << input << ", fs " << fs << ", f0 " << f0d << " Hz, L "
                             << fftLen << ": aliased " << a.dbfs << " dBFS (excluded " << a.excludedFraction << ")");
        std::printf("  SC-005 %s %s, %s, fs %.0f, f0 %.4f Hz, L %zu: %.1f dBFS (excluded %.3f)\n", familyName(fam),
                    fixture.c_str(), input, fs, f0d, fftLen, a.dbfs, a.excludedFraction);
        if (gate)
            CHECK(a.dbfs <= kAliasBarDbfs);
        worst = std::max(worst, a.dbfs);
    };

    for (const NamedInput& in : aliasInputs()) {
        switch (fam) {
            case Family::Excitation: {
                const PT::Render rMax = renderCoreStage(core, in.controls, kMaxExcitation, f0, total, fs);
                record(in.name, "max excitation", rMax.L);
                const PT::Render rSym = renderCoreStage(core, in.controls, kSymmetricMaxDrive, f0, total, fs);
                record(in.name, "drive 1 asymmetry 0", rSym.L);
                break;
            }
            case Family::Fold: {
                const PT::Render r = renderCoreStage(core, in.controls, kExcitationOff, f0, total, fs);
                for (const float amount : {HarmonicFold::kSoftEnd, HarmonicFold::kCompressionEnd, 100.0f}) {
                    const Stereo y = foldStage(fold, amount, f0, r.L, r.R);
                    requireStereoIdentical(y);
                    record(in.name, "amount " + std::to_string(static_cast<int>(amount)), y.first);
                }
                break;
            }
            case Family::Chain: {
                const Stereo y = renderChain(core, fold, in.controls, f0, total, fs);
                record(in.name, "max excitation -> Fold 100", y.first);
                break;
            }
        }
    }
    return worst;
}

/// The family on fresh instances prepared at fs.
void aliasCellFresh(Family fam, double fs, int note, bool gate, const char* caseName) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs);
    auto fold = std::make_unique<HarmonicFold>();
    fold->prepare(fs, kHostBlock);
    const double worst = runAliasFamily(fam, *core, *fold, fs, midiHz(note), gate);
    std::printf("%s: %s, fs %.0f, MIDI %d: worst %.1f dBFS%s\n", caseName, familyName(fam), fs, note, worst,
                gate ? " (bar -80, plan S9.4)" : " (recorded)");
}

void aliasShard(Family fam, int note, const char* caseName) {
    for (const double fs : kRates)
        aliasCellFresh(fam, fs, note, true, caseName);
}

// ---- h1 phasors ----

struct H1Pair {
    std::complex<double> L;
    std::complex<double> R;
};

/// BH7 Goertzel h1 phasors of L and R over the largest integer number of f0 periods (>= 1 s)
/// from `start`.
H1Pair h1Pair(const std::vector<float>& L, const std::vector<float>& R, std::size_t start, double fs, double f0) {
    REQUIRE(L.size() == R.size());
    REQUIRE(L.size() > start);
    const std::size_t len = hft::integerPeriodLength(L.size() - start, fs, f0, 1.0);
    const std::vector<double> w = PT::bh7Window(len);
    const H1Pair h{.L = PT::goertzelPhasorWindowed(L.data() + start, w, len, fs, f0),
                   .R = PT::goertzelPhasorWindowed(R.data() + start, w, len, fs, f0)};
    REQUIRE(std::norm(h.L) > 0.0);
    REQUIRE(std::norm(h.R) > 0.0);
    return h;
}

/// Inter-channel h1 phase, arg(hL / hR), in degrees.
double interPhaseDeg(const H1Pair& h) { return degrees(std::arg(h.L / h.R)); }

/// h1 power of (L+R)/2 re the mean of the L and R h1 powers (SC-013(b)), dB.
double monoSumVsMeanDb(const H1Pair& h) {
    const std::complex<double> m = 0.5 * (h.L + h.R);
    return PT::safeDb10(std::norm(m) / (0.5 * (std::norm(h.L) + std::norm(h.R))));
}

/// h1 power of (L+R)/2 re the coherent sum ((|h1L| + |h1R|)/2)² (SC-013(c-ii), plan S1 C-10), dB.
double monoSumVsCoherentDb(const H1Pair& h) {
    const std::complex<double> m = 0.5 * (h.L + h.R);
    const double coherent = 0.5 * (std::abs(h.L) + std::abs(h.R));
    return PT::safeDb10(std::norm(m) / (coherent * coherent));
}

/// L/R h1 imbalance, 10·log10(|h1L|² / |h1R|²).
double imbalanceDb(const H1Pair& h) { return PT::safeDb10(std::norm(h.L) / std::norm(h.R)); }

/// SC-013(b) pan vector: +0.5 on even n, −0.5 on odd n >= 3, 0 on h1 (index k = n − 1).
std::array<float, Krate::DSP::kMaxPartials> monoSumPan() {
    std::array<float, Krate::DSP::kMaxPartials> p{};
    for (std::size_t k = 1; k < p.size(); ++k)
        p[k] = ((k + 1) % 2 == 0) ? 0.5f : -0.5f;
    return p;
}

/// A fresh core at fs with the SC-013(b) pan vector, Reset note of `total` samples.
PT::Render renderPanned(const Controls& c, const Excitation& ex, float f0, std::size_t total, double fs) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs);
    core->setPartialPanOffsets(monoSumPan());
    PT::Render r = PT::renderExcited(*core, c, ex, f0, static_cast<double>(total) / fs);
    REQUIRE(r.L.size() == total);
    // The pan reached the bank: the channels differ.
    REQUIRE_FALSE(PT::samplesBitEqual(r.L.data(), r.R.data(), total));
    return r;
}

/// T1 of the excitation at (c, ex) on `core` against the mix-0 twin (spec SC-012 "input"), over
/// an integer-period window of at least windowSeconds from t_settle.
std::complex<double> excitationT1(ProfundumCore& core, double fs, const Controls& c, const Excitation& ex, float f0,
                                  double windowSeconds) {
    const auto f0d = static_cast<double>(f0);
    const std::size_t settle = toSamples(excitationSettleSeconds(), fs);
    const std::size_t total = settle + toSamples(windowSeconds + 1.0 / f0d, fs);
    const PT::Render out = renderCoreStage(core, c, ex, f0, total, fs);
    const PT::Render ref = PT::renderMixZeroTwin(fs, c, ex, f0, static_cast<double>(total) / fs);
    REQUIRE(ref.L.size() == total);
    PT::requireLREqual(ref.L.data(), ref.R.data(), total);
    const std::size_t len = hft::integerPeriodLength(total - settle, fs, f0d, windowSeconds);
    return PT::t1Transfer(out.L.data() + settle, ref.L.data() + settle, len, fs, f0d);
}

double magDb(const std::complex<double>& t) { return PT::safeDb10(std::norm(t)); }

// ---- SC-011 ----

/// 20·log10|mean| over [start, start + len), dBFS re 1.0.
double dcDbfs(const std::vector<float>& y, std::size_t start, std::size_t len) {
    REQUIRE(start + len <= y.size());
    double sum = 0.0;
    for (std::size_t k = start; k < start + len; ++k)
        sum += static_cast<double>(y[k]);
    const double mean = sum / static_cast<double>(len);
    return PT::safeDb10(mean * mean);
}

// ---- SC-019 ----

double maxAbs(const std::vector<float>& y) {
    double m = 0.0;
    for (const float v : y)
        m = std::max(m, std::abs(static_cast<double>(v)));
    return m;
}

bool allFinite(const std::vector<float>& y) {
    return std::all_of(y.begin(), y.end(), [](float v) { return Krate::DSP::detail::isFinite(v); });
}

/// SC-019 core + chain: the SC-009 state at max excitation -> Fold 100, 2 s per note, one core
/// and one Fold across the notes; both pre-ceiling peaks cleared per note.
void requireOutputBounded(const std::vector<int>& notes, const char* caseName) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    auto fold = std::make_unique<HarmonicFold>();
    fold->setAmount(100.0f);
    fold->prepare(kFs, kHostBlock);
    const std::size_t total = toSamples(2.0, kFs);
    double worstExc = 0.0;
    double worstFold = 0.0;
    int worstExcNote = 0;
    int worstFoldNote = 0;
    for (const int note : notes) {
        const float f0 = midiHz(note);
        core->clearExcitationPreCeilingPeak();
        fold->clearPreCeilingPeak();
        fold->setFundamentalHz(f0);
        const PT::Render r = renderCoreStage(*core, sc009State(), kMaxExcitation, f0, total, kFs);
        const auto excPeak = static_cast<double>(core->excitationPreCeilingPeak());
        const Stereo y = hft::renderFold(*fold, r.L, r.R, kHostBlock);
        requireStereoIdentical(y);
        const auto foldPeak = static_cast<double>(fold->preCeilingPeak());
        INFO("MIDI " << note << ": excitation pre-ceiling peak " << excPeak << ", Fold pre-ceiling peak " << foldPeak);
        CHECK(excPeak > 0.0);   // the observer is live
        CHECK(foldPeak > 0.0);
        CHECK(excPeak < static_cast<double>(ProfundumCore::kExcitationCeiling));
        CHECK(foldPeak < static_cast<double>(HarmonicFold::kOutputCeiling));
        CHECK(allFinite(r.L));
        CHECK(allFinite(y.first));
        CHECK(maxAbs(r.L) <= static_cast<double>(ProfundumCore::kExcitationCeiling));
        CHECK(maxAbs(y.first) <= static_cast<double>(HarmonicFold::kOutputCeiling));
        if (excPeak > worstExc) {
            worstExc = excPeak;
            worstExcNote = note;
        }
        if (foldPeak > worstFold) {
            worstFold = foldPeak;
            worstFoldNote = note;
        }
    }
    std::printf("%s: worst excitation pre-ceiling peak %.3f (MIDI %d; ceiling %.1f, plan <= 1.42), worst Fold "
                "pre-ceiling peak %.3f (MIDI %d; ceiling %.1f, plan <= 2.40)\n",
                caseName, worstExc, worstExcNote, static_cast<double>(ProfundumCore::kExcitationCeiling), worstFold,
                worstFoldNote, static_cast<double>(HarmonicFold::kOutputCeiling));
}

// ---- helper parity (plan S8.1) ----

/// Every function HarmonicFoldTest duplicates from the Phase 1 systems helper, run on the same
/// data through both copies, bit-identical (memcmp on the double outputs). x[offset ...] must hold
/// max(fftLen, kLowFrequencyFftSize) samples.
void requireHelperParity(const std::vector<float>& x, std::size_t offset, double fs, double f0, std::size_t fftLen,
                         const char* label) {
    constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;
    REQUIRE(x.size() >= offset + std::max(fftLen, kFrame));
    const float* p = x.data() + offset;
    INFO(label << ": fs " << fs << ", f0 " << f0 << ", L " << fftLen);

    CHECK(sameBits(PT::bh7Window(fftLen), hft::bh7Window(fftLen)));
    CHECK(sameBits(PT::goertzelPhase(p, fftLen, fs, f0), hft::goertzelPhase(p, fftLen, fs, f0)));
    CHECK(sameBits(PT::goertzelPhasor(p, fftLen, fs, f0), hft::goertzelPhasor(p, fftLen, fs, f0)));

    std::vector<std::complex<double>> a(fftLen);
    for (std::size_t k = 0; k < fftLen; ++k)
        a[k] = std::complex<double>(static_cast<double>(p[k]), 0.0);
    std::vector<std::complex<double>> b = a;
    PT::fftDouble(a);
    hft::fftDouble(b);
    CHECK(sameBits(a, b));

    CHECK(sameBits(PT::bh7PowerSpectrum(p, fftLen), hft::bh7PowerSpectrum(p, fftLen)));

    const int nHarm = static_cast<int>(std::ceil(fs * 0.5 / f0));
    CHECK(sameBits(PT::renderedHarmonicPowers(p, fs, f0, nHarm), hft::renderedHarmonicPowers(p, fs, f0, nHarm)));

    CHECK(PT::aliasFftLength(fs, f0) == hft::aliasFftLength(fs, f0));

    const PT::AliasResult ra = PT::aliasedPower(p, fftLen, fs, f0);
    const hft::AliasResult rb = hft::aliasedPower(p, fftLen, fs, f0);
    CHECK(sameBits(ra.aliasedDbfs, rb.aliasedDbfs));
    CHECK(sameBits(ra.excludedFraction, rb.excludedFraction));
    CHECK(sameBits(ra.totalDb, rb.totalDb));
}

// ---- SC-012(d) core arm ----

void requireFundamentalPreservedCoreInput() {
    double worstAngle = 0.0;
    double minMagBelow65 = 1.0e300;
    double minMagFold = 1.0e300;
    for (const double fs : kRates) {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(fs);
        auto fold = std::make_unique<HarmonicFold>();
        fold->prepare(fs, kHostBlock);
        for (const int note : {kNoteE0, kNoteE1, kNoteE2}) {
            const float f0 = midiHz(note);
            const auto f0d = static_cast<double>(f0);
            const std::size_t settle = toSamples(foldSettleSeconds(f0d), fs);
            const std::size_t total = settle + toSamples(1.2, fs);
            const PT::Render r =
                renderCoreStage(*core, SpectralShapeRecipe::kSawAnchor, kExcitationOff, f0, total, fs);
            const std::size_t len = hft::integerPeriodLength(total - settle, fs, f0d, 1.0);
            const std::vector<double> w = PT::bh7Window(len);
            const std::complex<double> hIn = PT::goertzelPhasorWindowed(r.L.data() + settle, w, len, fs, f0d);
            REQUIRE(std::norm(hIn) > 0.0);
            for (int i = 0; i <= 40; ++i) {   // 41 points, step 2.5
                const float a = 2.5f * static_cast<float>(i);
                const std::vector<float> y = foldMono(*fold, a, f0, r.L);
                const std::complex<double> t1 = PT::goertzelPhasorWindowed(y.data() + settle, w, len, fs, f0d) / hIn;
                const double angle = degrees(std::arg(t1));
                const double mag = magDb(t1);
                INFO("fs " << fs << ", MIDI " << note << ", amount " << a << ": angle " << angle << " deg, |T1| "
                           << mag << " dB");
                CHECK(std::abs(angle) <= 5.0);
                CHECK(t1.real() > 0.0);   // no polarity flip
                if (a < HarmonicFold::kCompressionEnd) {
                    CHECK(mag >= -6.0);
                    minMagBelow65 = std::min(minMagBelow65, mag);
                } else {
                    CHECK(mag >= -12.0);
                    minMagFold = std::min(minMagFold, mag);
                }
                worstAngle = std::max(worstAngle, std::abs(angle));
            }
        }
    }
    std::printf("HarmonicFold_FundamentalPreservedCoreInput: worst |angle T1| %.3f deg (bar 5), min |T1| %.2f dB "
                "below 65 (bar -6), %.2f dB on [65, 100] (bar -12)\n",
                worstAngle, minMagBelow65, minMagFold);
}

// ---- listening renders ----

/// Relative to the working directory: run the exe from the repository root.
std::filesystem::path phase2ListenDir() {
    return std::filesystem::path("build") / "windows-x64-release" / "profundum-phase2-listen";
}

void appendLe16(std::vector<unsigned char>& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xFFu));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
}

void appendLe32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8)
        b.push_back(static_cast<unsigned char>((v >> shift) & 0xFFu));
}

void appendTag(std::vector<unsigned char>& b, const char* tag) {
    for (std::size_t i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>(tag[i]));
}

/// Minimal RIFF/WAVE writer: WAVE_FORMAT_IEEE_FLOAT (3), stereo, 32-bit, little-endian, 18-byte
/// fmt chunk plus the fact chunk non-PCM formats carry (the chain can exceed 1.0, so no PCM).
bool writeStereoFloatWav(const std::filesystem::path& path, const std::vector<float>& L, const std::vector<float>& R,
                         double fs) {
    if (L.size() != R.size())
        return false;
    const std::uint64_t frames = L.size();
    const std::uint64_t dataBytes = frames * 8u;
    if (dataBytes > 0xFFFF0000ull)
        return false;
    const auto sr = static_cast<std::uint32_t>(std::llround(fs));
    std::vector<unsigned char> b;
    b.reserve(static_cast<std::size_t>(dataBytes) + 64u);
    appendTag(b, "RIFF");
    appendLe32(b, static_cast<std::uint32_t>(4u + (8u + 18u) + (8u + 4u) + 8u + dataBytes));
    appendTag(b, "WAVE");
    appendTag(b, "fmt ");
    appendLe32(b, 18u);
    appendLe16(b, 3u);        // WAVE_FORMAT_IEEE_FLOAT
    appendLe16(b, 2u);        // channels
    appendLe32(b, sr);
    appendLe32(b, sr * 8u);   // byte rate
    appendLe16(b, 8u);        // block align
    appendLe16(b, 32u);       // bits per sample
    appendLe16(b, 0u);        // cbSize
    appendTag(b, "fact");
    appendLe32(b, 4u);
    appendLe32(b, static_cast<std::uint32_t>(frames));
    appendTag(b, "data");
    appendLe32(b, static_cast<std::uint32_t>(dataBytes));
    for (std::size_t i = 0; i < L.size(); ++i) {
        appendLe32(b, std::bit_cast<std::uint32_t>(L[i]));
        appendLe32(b, std::bit_cast<std::uint32_t>(R[i]));
    }
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

void writePhase2Listen(const std::string& name, const std::vector<float>& L, const std::vector<float>& R,
                       std::size_t& written) {
    const std::filesystem::path p = phase2ListenDir() / name;
    INFO("WAV " << p.string());
    REQUIRE(writeStereoFloatWav(p, L, R, kFs));
    ++written;
}

/// Reset note on a fresh 48 kHz core; before every 32-sample block, setter(core, t) with t the
/// render position in [0, 1). Zero pan: L == R asserted.
template <class Setter>
PT::Render renderCoreSweep(const Controls& c, const Excitation& ex, float f0, std::size_t total, Setter setter) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    core->setExcitation(ex);
    core->setRetriggerPhase(Policy::Reset);
    core->setControls(c);
    core->noteOn(f0);
    PT::Render r;
    r.L.assign(total, 0.0f);
    r.R.assign(total, 0.0f);
    for (std::size_t pos = 0; pos < total; pos += kLatchGrid) {
        setter(*core, static_cast<double>(pos) / static_cast<double>(total));
        const std::size_t n = std::min(kLatchGrid, total - pos);
        core->processBlock(r.L.data() + pos, r.R.data() + pos, n);
    }
    PT::requireLREqual(r.L.data(), r.R.data(), total);
    return r;
}

/// A Fold amount ramp 0 -> 100 over the render, one setAmount per 32-sample block.
Stereo foldSweep(const std::vector<float>& L, const std::vector<float>& R, float f0) {
    auto fold = std::make_unique<HarmonicFold>();
    fold->setAmount(0.0f);
    fold->prepare(kFs, kHostBlock);
    fold->setFundamentalHz(f0);
    Stereo y{L, R};
    const std::size_t total = L.size();
    for (std::size_t pos = 0; pos < total; pos += kLatchGrid) {
        fold->setAmount(static_cast<float>(100.0 * static_cast<double>(pos) / static_cast<double>(total)));
        const std::size_t n = std::min(kLatchGrid, total - pos);
        fold->processBlock(y.first.data() + pos, y.second.data() + pos, n);
    }
    return y;
}

}  // namespace

// =============================================================================
// Helper parity (plan S8.1 drift guard): HarmonicFoldTest's transcribed copies are
// bit-identical to the Phase 1 ProfundumTest originals.
// =============================================================================

TEST_CASE("ProfundumEdgeFold_HelperParity", "[systems][profundum][edge-fold]") {
    constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;

    // (1) A 1 kHz full-scale sine at 48 kHz, generated in double.
    {
        const double f0 = 1000.0;
        std::vector<float> x(kFrame, 0.0f);
        for (std::size_t n = 0; n < x.size(); ++n)
            x[n] = static_cast<float>(std::sin(PT::kTwoPi * f0 * static_cast<double>(n) / kFs));
        requireHelperParity(x, 0, kFs, f0, PT::aliasFftLength(kFs, f0), "1 kHz sine");
    }

    const float f0 = midiHz(kNoteE1);
    const auto f0d = static_cast<double>(f0);

    // (2) The SC-009-state core render (excitation off), E1, 48 kHz.
    {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const std::size_t settle = toSamples(excitationSettleSeconds(), kFs);
        const PT::Render r = renderCoreStage(*core, sc009State(), kExcitationOff, f0, settle + kFrame, kFs);
        requireHelperParity(r.L, settle, kFs, f0d, PT::aliasFftLength(kFs, f0d), "SC-009 state core render");
    }

    // (3) The SC-005 alias fixture: the chain smoke cell (SC-009 state, max excitation -> Fold 100).
    {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        auto fold = std::make_unique<HarmonicFold>();
        fold->prepare(kFs, kHostBlock);
        const std::size_t fftLen = PT::aliasSafeFftLength(kFs, f0d);
        const std::size_t settle = toSamples(chainSettleSeconds(f0d), kFs);
        const Stereo y = renderChain(*core, *fold, sc009State(), f0, settle + std::max(fftLen, kFrame), kFs);
        requireHelperParity(y.first, settle, kFs, f0d, fftLen, "SC-005 chain fixture");
    }
}

// =============================================================================
// SC-005: aliasing <= -80 dBFS (spec "SC-005 method", plan S8.2 / S9.4). Each spec name
// is the per-push smoke at E1 / 48 kHz; the [long] ...E0 / ...E1 / ...E2 siblings run that
// note at 44.1 / 48 / 96 kHz, so the grid shards by note into concurrent processes.
// =============================================================================

TEST_CASE("ProfundumCore_ExcitationNoAliasing", "[systems][profundum][edge-fold]") {
    aliasCellFresh(Family::Excitation, kFs, kNoteE1, true, "ProfundumCore_ExcitationNoAliasing");
}

TEST_CASE("ProfundumCore_ExcitationNoAliasingE0", "[systems][profundum][edge-fold][long]") {
    aliasShard(Family::Excitation, kNoteE0, "ProfundumCore_ExcitationNoAliasingE0");
}

TEST_CASE("ProfundumCore_ExcitationNoAliasingE1", "[systems][profundum][edge-fold][long]") {
    aliasShard(Family::Excitation, kNoteE1, "ProfundumCore_ExcitationNoAliasingE1");
}

TEST_CASE("ProfundumCore_ExcitationNoAliasingE2", "[systems][profundum][edge-fold][long]") {
    aliasShard(Family::Excitation, kNoteE2, "ProfundumCore_ExcitationNoAliasingE2");
}

TEST_CASE("HarmonicFold_NoAliasing", "[systems][profundum][edge-fold]") {
    aliasCellFresh(Family::Fold, kFs, kNoteE1, true, "HarmonicFold_NoAliasing");
}

TEST_CASE("HarmonicFold_NoAliasingE0", "[systems][profundum][edge-fold][long]") {
    aliasShard(Family::Fold, kNoteE0, "HarmonicFold_NoAliasingE0");
}

TEST_CASE("HarmonicFold_NoAliasingE1", "[systems][profundum][edge-fold][long]") {
    aliasShard(Family::Fold, kNoteE1, "HarmonicFold_NoAliasingE1");
}

TEST_CASE("HarmonicFold_NoAliasingE2", "[systems][profundum][edge-fold][long]") {
    aliasShard(Family::Fold, kNoteE2, "HarmonicFold_NoAliasingE2");
}

TEST_CASE("ProfundumEdgeFold_ChainNoAliasing", "[systems][profundum][edge-fold]") {
    aliasCellFresh(Family::Chain, kFs, kNoteE1, true, "ProfundumEdgeFold_ChainNoAliasing");
}

TEST_CASE("ProfundumEdgeFold_ChainNoAliasingE0", "[systems][profundum][edge-fold][long]") {
    aliasShard(Family::Chain, kNoteE0, "ProfundumEdgeFold_ChainNoAliasingE0");
}

TEST_CASE("ProfundumEdgeFold_ChainNoAliasingE1", "[systems][profundum][edge-fold][long]") {
    aliasShard(Family::Chain, kNoteE1, "ProfundumEdgeFold_ChainNoAliasingE1");
}

TEST_CASE("ProfundumEdgeFold_ChainNoAliasingE2", "[systems][profundum][edge-fold][long]") {
    aliasShard(Family::Chain, kNoteE2, "ProfundumEdgeFold_ChainNoAliasingE2");
}

// plan S1 C-6: the positive control runs on the SC-009 state at its own maximum effective
// drive (D_eff = 3.98, edge 1), through a naive base-rate std::tanh in double.
TEST_CASE("ProfundumEdgeFold_AliasGatePositiveControl", "[systems][profundum][edge-fold]") {
    const float f0 = midiHz(kNoteE1);
    const auto f0d = static_cast<double>(f0);
    const std::size_t fftLen = PT::aliasSafeFftLength(kFs, f0d);
    const std::size_t total = toSamples(excitationSettleSeconds(), kFs) + fftLen;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    const PT::Render r = renderCoreStage(*core, sc009State(), kExcitationOff, f0, total, kFs);

    const double dEff = static_cast<double>(ProfundumCore::excitationDriveLaw(1.0f))
                        * static_cast<double>(ProfundumCore::edgeDriveGain(1.0f));
    REQUIRE(std::abs(dEff - 3.981) <= 0.001);
    const double invP = 1.0 / static_cast<double>(HarmonicFold::kCalibrationPeak);
    std::vector<float> y(total, 0.0f);
    for (std::size_t k = 0; k < total; ++k)
        y[k] = static_cast<float>(std::tanh(dEff * static_cast<double>(r.L[k]) * invP));

    const AliasReading a = measureAliasTail(y, fftLen, kFs, f0d);
    std::printf("ProfundumEdgeFold_AliasGatePositiveControl: naive tanh(%.3f·x/P) on the SC-009 state, E1/48 kHz: "
                "%.1f dBFS (must read > -80, plan -48.0)\n",
                dEff, a.dbfs);
    REQUIRE(a.dbfs > kAliasBarDbfs);
}

// =============================================================================
// SC-010 core arm: the Fold at amount 0 on the core, residual <= -90 dB (expected exact)
// =============================================================================

TEST_CASE("HarmonicFold_TransparentAtZeroCoreInput", "[systems][profundum][edge-fold]") {
    const std::array<NamedInput, 2> inputs{{{"kSawAnchor", SpectralShapeRecipe::kSawAnchor},
                                            {"SC-009 state", sc009State()}}};
    double worst = -1.0e300;
    std::size_t exact = 0;
    std::size_t cells = 0;
    for (const double fs : kRates) {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(fs);
        auto fold = std::make_unique<HarmonicFold>();
        fold->prepare(fs, kHostBlock);
        const std::size_t total = toSamples(0.5, fs);
        for (int octave = 0; octave <= 5; ++octave) {
            const int note = 12 + 12 * octave;   // C0 ... C5
            const float f0 = midiHz(note);
            for (const NamedInput& in : inputs) {
                const PT::Render r = renderCoreStage(*core, in.controls, kExcitationOff, f0, total, fs);
                const Stereo y = foldStage(*fold, 0.0f, f0, r.L, r.R);
                for (int ch = 0; ch < 2; ++ch) {
                    const std::vector<float>& x = (ch == 0) ? r.L : r.R;
                    const std::vector<float>& o = (ch == 0) ? y.first : y.second;
                    double res = 0.0;
                    double power = 0.0;
                    for (std::size_t k = 0; k < total; ++k) {
                        const double d = static_cast<double>(o[k]) - static_cast<double>(x[k]);
                        res += d * d;
                        power += static_cast<double>(x[k]) * static_cast<double>(x[k]);
                    }
                    REQUIRE(power > 0.0);
                    const double db = PT::safeDb10(res / power);
                    INFO("fs " << fs << ", C" << octave << ", " << in.name << ", channel " << ch << ": residual "
                               << db << " dB");
                    CHECK(db <= -90.0);
                    worst = std::max(worst, db);
                    ++cells;
                    if (PT::samplesBitEqual(o.data(), x.data(), total))
                        ++exact;
                }
            }
        }
    }
    std::printf("HarmonicFold_TransparentAtZeroCoreInput: worst residual %.1f dB (bar -90); %zu of %zu channels "
                "bit-exact (expected all)\n",
                worst, exact, cells);
}

// =============================================================================
// SC-011: DC <= -80 dBFS on the max-excitation output through the Fold, and the chain
// =============================================================================

namespace {

/// DC per channel of the Fold at each amount on the max-excitation output, kSineAnchor and
/// kSawAnchor, E0 / E1 / E2, 48 kHz. Window: >= 1 s of integer periods from t_settle (chain).
double worstDcOnExcitationOutput(const std::vector<float>& amounts, const char* label) {
    const std::array<NamedInput, 2> inputs{{{"kSineAnchor", SpectralShapeRecipe::kSineAnchor},
                                            {"kSawAnchor", SpectralShapeRecipe::kSawAnchor}}};
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    auto fold = std::make_unique<HarmonicFold>();
    fold->prepare(kFs, kHostBlock);
    double worst = -1.0e300;
    for (const int note : {kNoteE0, kNoteE1, kNoteE2}) {
        const float f0 = midiHz(note);
        const auto f0d = static_cast<double>(f0);
        const std::size_t settle = toSamples(chainSettleSeconds(f0d), kFs);
        const std::size_t total = settle + toSamples(1.2, kFs);
        const std::size_t len = hft::integerPeriodLength(total - settle, kFs, f0d, 1.0);
        for (const NamedInput& in : inputs) {
            const PT::Render r = renderCoreStage(*core, in.controls, kMaxExcitation, f0, total, kFs);
            for (const float amount : amounts) {
                const Stereo y = foldStage(*fold, amount, f0, r.L, r.R);
                for (int ch = 0; ch < 2; ++ch) {
                    const double db = dcDbfs(ch == 0 ? y.first : y.second, settle, len);
                    INFO(label << ": MIDI " << note << ", " << in.name << ", amount " << amount << ", channel " << ch
                               << ": DC " << db << " dBFS");
                    CHECK(db <= -80.0);
                    worst = std::max(worst, db);
                }
            }
        }
    }
    return worst;
}

}  // namespace

TEST_CASE("HarmonicFold_DcFreeOnExcitationOutput", "[systems][profundum][edge-fold]") {
    const double worst = worstDcOnExcitationOutput(
        {HarmonicFold::kSoftEnd, HarmonicFold::kCompressionEnd, 100.0f}, "HarmonicFold_DcFreeOnExcitationOutput");
    std::printf("HarmonicFold_DcFreeOnExcitationOutput: worst DC %.1f dBFS (bar -80)\n", worst);
}

TEST_CASE("ProfundumEdgeFold_ChainDcFree", "[systems][profundum][edge-fold]") {
    const std::array<NamedInput, 2> inputs{{{"kSineAnchor", SpectralShapeRecipe::kSineAnchor},
                                            {"kSawAnchor", SpectralShapeRecipe::kSawAnchor}}};
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    auto fold = std::make_unique<HarmonicFold>();
    fold->prepare(kFs, kHostBlock);
    double worst = -1.0e300;
    for (const int note : {kNoteE0, kNoteE1, kNoteE2}) {
        const float f0 = midiHz(note);
        const auto f0d = static_cast<double>(f0);
        const std::size_t settle = toSamples(chainSettleSeconds(f0d), kFs);
        const std::size_t total = settle + toSamples(1.2, kFs);
        const std::size_t len = hft::integerPeriodLength(total - settle, kFs, f0d, 1.0);
        for (const NamedInput& in : inputs) {
            const Stereo y = renderChain(*core, *fold, in.controls, f0, total, kFs);
            for (int ch = 0; ch < 2; ++ch) {
                const double db = dcDbfs(ch == 0 ? y.first : y.second, settle, len);
                INFO("MIDI " << note << ", " << in.name << ", channel " << ch << ": DC " << db << " dBFS");
                CHECK(db <= -80.0);
                worst = std::max(worst, db);
            }
        }
    }
    std::printf("ProfundumEdgeFold_ChainDcFree: worst DC %.1f dBFS (bar -80)\n", worst);
}

// =============================================================================
// SC-012(d) core arm: the Fold 0...100 (41 points) on the core at kSawAnchor, E0-E2
// =============================================================================

TEST_CASE("HarmonicFold_FundamentalPreservedCoreInput", "[systems][profundum][edge-fold]") {
    requireFundamentalPreservedCoreInput();
}

// =============================================================================
// SC-013(b) (plan S1 C-2): mono sum of the panned core at max excitation
// =============================================================================

TEST_CASE("ProfundumCore_ExcitationMonoSum", "[systems][profundum][edge-fold]") {
    const std::array<NamedInput, 2> inputs{{{"mid grid", SpectralShapeRecipe::kDefaultControls},
                                            {"kSawAnchor", SpectralShapeRecipe::kSawAnchor}}};
    double worstPhase0 = 0.0;
    double worstPhase1 = 0.0;
    double worstPower = 0.0;
    for (const int note : {kNoteE1, kNoteC2}) {
        const float f0 = midiHz(note);
        const auto f0d = static_cast<double>(f0);
        const std::size_t settle = toSamples(excitationSettleSeconds(), kFs);
        const std::size_t total = settle + toSamples(1.2, kFs);
        for (const NamedInput& in : inputs) {
            for (const float asym : {0.0f, 1.0f}) {
                const Excitation ex{.mix = 1.0f, .drive = 1.0f, .asymmetry = asym};
                const PT::Render r = renderPanned(in.controls, ex, f0, total, kFs);
                const H1Pair h = h1Pair(r.L, r.R, settle, kFs, f0d);
                const double phase = interPhaseDeg(h);
                const double power = monoSumVsMeanDb(h);
                // plan S1 C-2: <= 2 deg at asymmetry 0, <= 15 deg at asymmetry 1.
                const double phaseBar = (asym == 0.0f) ? 2.0 : 15.0;
                INFO("MIDI " << note << ", " << in.name << ", asymmetry " << asym << ": inter-channel h1 " << phase
                             << " deg (bar " << phaseBar << "), (L+R)/2 h1 power re mean " << power << " dB");
                CHECK(std::abs(phase) <= phaseBar);
                CHECK(std::abs(power) <= 0.5);
                if (asym == 0.0f)
                    worstPhase0 = std::max(worstPhase0, std::abs(phase));
                else
                    worstPhase1 = std::max(worstPhase1, std::abs(phase));
                worstPower = std::max(worstPower, std::abs(power));
            }
        }
    }
    std::printf("ProfundumCore_ExcitationMonoSum: worst inter-channel h1 %.3f deg at asymmetry 0 (bar 2), %.3f deg "
                "at asymmetry 1 (bar 15, plan 9.36); worst mono-sum power %.3f dB (bar 0.5)\n",
                worstPhase0, worstPhase1, worstPower);
}

// =============================================================================
// SC-013(c) first clause (plan S1 C-10): (c-i) Fold-added inter-channel h1 phase on the
// mix-0 panned core; (c-ii) no h1 cancellation at max excitation; (c-iii) SC-022(h)
// =============================================================================

TEST_CASE("HarmonicFold_MonoSum", "[systems][profundum][edge-fold]") {
    const std::array<NamedInput, 2> inputs{{{"mid grid", SpectralShapeRecipe::kDefaultControls},
                                            {"kSawAnchor", SpectralShapeRecipe::kSawAnchor}}};
    auto fold = std::make_unique<HarmonicFold>();
    fold->prepare(kFs, kHostBlock);
    double worstAdded = 0.0;
    double worstCoherent = 0.0;
    for (const int note : {kNoteE1, kNoteC2}) {
        const float f0 = midiHz(note);
        const auto f0d = static_cast<double>(f0);
        for (const NamedInput& in : inputs) {
            // (c-i) mix 0: the Fold's own contribution.
            {
                const std::size_t settle = toSamples(foldSettleSeconds(f0d), kFs);
                const std::size_t total = settle + toSamples(1.2, kFs);
                const PT::Render r = renderPanned(in.controls, kExcitationOff, f0, total, kFs);
                const Stereo y = foldStage(*fold, 100.0f, f0, r.L, r.R);
                const H1Pair hi = h1Pair(r.L, r.R, settle, kFs, f0d);
                const H1Pair ho = h1Pair(y.first, y.second, settle, kFs, f0d);
                const double added = degrees(std::arg((ho.L / ho.R) / (hi.L / hi.R)));
                INFO("(c-i) MIDI " << note << ", " << in.name << ", mix 0: inter-channel h1 in " << interPhaseDeg(hi)
                                   << " deg, out " << interPhaseDeg(ho) << " deg, added " << added << " deg");
                CHECK(std::abs(added) <= 2.0);
                worstAdded = std::max(worstAdded, std::abs(added));
                std::printf("HarmonicFold_MonoSum (c-i) MIDI %d, %s, mix 0: Fold-added inter-channel h1 %.3f deg "
                            "(bar 2, plan worst 1.89)\n",
                            note, in.name, added);
            }
            // (c-ii) / (c-iii) max excitation at asymmetry 0 and 1 (drive 1, mix 1).
            for (const float asym : {0.0f, 1.0f}) {
                const Excitation ex{.mix = 1.0f, .drive = 1.0f, .asymmetry = asym};
                const std::size_t settle = toSamples(chainSettleSeconds(f0d), kFs);
                const std::size_t total = settle + toSamples(1.2, kFs);
                const PT::Render r = renderPanned(in.controls, ex, f0, total, kFs);
                const Stereo y = foldStage(*fold, 100.0f, f0, r.L, r.R);
                const H1Pair hi = h1Pair(r.L, r.R, settle, kFs, f0d);
                const H1Pair ho = h1Pair(y.first, y.second, settle, kFs, f0d);
                const double coherent = monoSumVsCoherentDb(ho);
                const double phaseOut = interPhaseDeg(ho);
                const double imbalance = imbalanceDb(ho);
                INFO("(c-ii) MIDI " << note << ", " << in.name << ", asymmetry " << asym
                                    << ": (L+R)/2 h1 re coherent sum " << coherent << " dB");
                CHECK(std::abs(coherent) <= 0.5);
                REQUIRE(finiteBits(phaseOut));
                REQUIRE(finiteBits(imbalance));
                worstCoherent = std::max(worstCoherent, std::abs(coherent));
                std::printf("SC-022(h) HarmonicFold_MonoSum MIDI %d, %s, max excitation asymmetry %.0f: inter-channel "
                            "h1 in %.2f -> out %.2f deg, L/R h1 imbalance out %.2f dB (in %.2f), (L+R)/2 re coherent "
                            "sum %.3f dB (bar 0.5, plan worst 0.16)\n",
                            note, in.name, static_cast<double>(asym), interPhaseDeg(hi), phaseOut, imbalance,
                            imbalanceDb(hi), coherent);
            }
        }
    }
    std::printf("HarmonicFold_MonoSum: worst Fold-added phase %.3f deg (bar 2), worst coherent-sum loss %.3f dB "
                "(bar 0.5)\n",
                worstAdded, worstCoherent);
}

// =============================================================================
// SC-017: one core + one Fold re-prepared 48 -> 96 -> 44.1 kHz; the E1 SC-005 smoke and
// SC-012(a) hold again after each prepare
// =============================================================================

TEST_CASE("ProfundumEdgeFold_SampleRateChange", "[systems][profundum][edge-fold]") {
    auto core = std::make_unique<ProfundumCore>();
    auto fold = std::make_unique<HarmonicFold>();
    const std::array<NamedInput, 3> t1Inputs{{{"kSineAnchor", SpectralShapeRecipe::kSineAnchor},
                                              {"mid grid", SpectralShapeRecipe::kDefaultControls},
                                              {"kSawAnchor", SpectralShapeRecipe::kSawAnchor}}};
    for (const double fs : {48000.0, 96000.0, 44100.0}) {
        core->prepare(fs);
        fold->prepare(fs, kHostBlock);
        REQUIRE(PT::coreSampleRate(*core) == fs);
        REQUIRE(core->excitationOversamplingFactor() == 4);
        REQUIRE(fold->oversamplingFactor() == 4);

        // SC-005 E1 smoke, all three families, on the re-prepared instances.
        for (const Family fam : {Family::Excitation, Family::Fold, Family::Chain}) {
            const double worst = runAliasFamily(fam, *core, *fold, fs, midiHz(kNoteE1), true);
            std::printf("ProfundumEdgeFold_SampleRateChange fs %.0f: SC-005 E1 %s worst %.1f dBFS (bar -80)\n", fs,
                        familyName(fam), worst);
        }

        // SC-012(a): drive 0, mix 1 against the mix-0 twin, E0-E2 (plan S1 C-6 inputs).
        double worstMag = 0.0;
        double worstAngle = 0.0;
        for (const int note : {kNoteE0, kNoteE1, kNoteE2}) {
            for (const NamedInput& in : t1Inputs) {
                const std::complex<double> t1 = excitationT1(*core, fs, in.controls, kDriveZero, midiHz(note), 1.0);
                const double mag = magDb(t1);
                const double angle = degrees(std::arg(t1));
                INFO("fs " << fs << ", MIDI " << note << ", " << in.name << ": |T1| " << mag << " dB, angle " << angle
                           << " deg");
                CHECK(std::abs(mag) <= 0.1);
                CHECK(std::abs(angle) <= 2.0);
                worstMag = std::max(worstMag, std::abs(mag));
                worstAngle = std::max(worstAngle, std::abs(angle));
            }
        }
        std::printf("ProfundumEdgeFold_SampleRateChange fs %.0f: SC-012(a) worst ||T1|| %.4f dB (bar 0.1), |angle| "
                    "%.4f deg (bar 2)\n",
                    fs, worstMag, worstAngle);
    }
}

// =============================================================================
// SC-019 (core + chain): SC-009 state at max excitation -> Fold 100, MIDI 12-60, 2 s each
// =============================================================================

TEST_CASE("ProfundumCore_ExcitationOutputBoundedSmoke", "[systems][profundum][edge-fold]") {
    requireOutputBounded({12, 28, 40, 60}, "ProfundumCore_ExcitationOutputBoundedSmoke");
}

TEST_CASE("ProfundumCore_ExcitationOutputBounded", "[systems][profundum][edge-fold][long]") {
    std::vector<int> notes;
    for (int note = 12; note <= 60; ++note)
        notes.push_back(note);
    requireOutputBounded(notes, "ProfundumCore_ExcitationOutputBounded");
}

// =============================================================================
// SC-022 recorded figures (printed, REQUIRE finite, no threshold)
// =============================================================================

// (a) aliased power per octave E2 ... C5 for the excitation, the Fold and the chain, 48 kHz.
TEST_CASE("ProfundumEdgeFold_RecordedAliasingByOctave", "[systems][profundum][edge-fold][long]") {
    for (const int note : {40, 52, 64, 72}) {   // E2, E3, E4, C5
        for (const Family fam : {Family::Excitation, Family::Fold, Family::Chain})
            aliasCellFresh(fam, kFs, note, false, "SC-022(a) ProfundumEdgeFold_RecordedAliasingByOctave");
    }
}

// (b) aliased power at 22.05 and 192 kHz at E1.
TEST_CASE("ProfundumEdgeFold_RecordedAliasingRateExtremes", "[systems][profundum][edge-fold]") {
    for (const double fs : {22050.0, 192000.0}) {
        for (const Family fam : {Family::Excitation, Family::Fold, Family::Chain})
            aliasCellFresh(fam, fs, kNoteE1, false, "SC-022(b) ProfundumEdgeFold_RecordedAliasingRateExtremes");
    }
}

// (c) |T1| and angle T1 of the excitation (drive 0, mix 1) at the core's f0 floor, 8 Hz.
TEST_CASE("ProfundumCore_ExcitationRecordedPhaseAt8Hz", "[systems][profundum][edge-fold]") {
    const std::array<NamedInput, 3> inputs{{{"kSineAnchor", SpectralShapeRecipe::kSineAnchor},
                                            {"mid grid", SpectralShapeRecipe::kDefaultControls},
                                            {"kSawAnchor", SpectralShapeRecipe::kSawAnchor}}};
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    for (const NamedInput& in : inputs) {
        const std::complex<double> t1 =
            excitationT1(*core, kFs, in.controls, kDriveZero, ProfundumCore::kMinF0Hz, 2.0);
        const double mag = magDb(t1);
        const double angle = degrees(std::arg(t1));
        REQUIRE(finiteBits(mag));
        REQUIRE(finiteBits(angle));
        std::printf("SC-022(c) ProfundumCore_ExcitationRecordedPhaseAt8Hz %s, 48 kHz: |T1| %.4f dB, angle T1 %.3f "
                    "deg\n",
                    in.name, mag, angle);
    }
}

// (e) plan S1 C-2: angle T1 at max asymmetry (max excitation) for the five SC-005 inputs.
TEST_CASE("ProfundumCore_ExcitationRecordedAsymmetryPhase", "[systems][profundum][edge-fold]") {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    for (const int note : {kNoteE1, kNoteC2}) {
        for (const NamedInput& in : aliasInputs()) {
            const std::complex<double> t1 = excitationT1(*core, kFs, in.controls, kMaxExcitation, midiHz(note), 1.0);
            const double mag = magDb(t1);
            const double angle = degrees(std::arg(t1));
            REQUIRE(finiteBits(mag));
            REQUIRE(finiteBits(angle));
            std::printf("SC-022(e) ProfundumCore_ExcitationRecordedAsymmetryPhase MIDI %d, %s, max excitation: "
                        "angle T1 %.2f deg, |T1| %.2f dB\n",
                        note, in.name, angle, mag);
        }
    }
}

// (f) plan S1 C-8: Fold-100 level change on non-sine inputs at C2.
TEST_CASE("HarmonicFold_RecordedLevelNonSine", "[systems][profundum][edge-fold]") {
    const std::array<NamedInput, 3> inputs{{{"kSawAnchor", SpectralShapeRecipe::kSawAnchor},
                                            {"mid grid", SpectralShapeRecipe::kDefaultControls},
                                            {"SC-009 state", sc009State()}}};
    const std::array<double, 3> plan{1.70, 2.71, 6.51};
    const float f0 = midiHz(kNoteC2);
    const auto f0d = static_cast<double>(f0);
    const std::size_t settle = toSamples(foldSettleSeconds(f0d), kFs);
    const std::size_t total = settle + toSamples(1.2, kFs);
    const std::size_t len = hft::integerPeriodLength(total - settle, kFs, f0d, 1.0);
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    auto fold = std::make_unique<HarmonicFold>();
    fold->prepare(kFs, kHostBlock);
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const PT::Render r = renderCoreStage(*core, inputs[i].controls, kExcitationOff, f0, total, kFs);
        const std::vector<float> y = foldMono(*fold, 100.0f, f0, r.L);
        double sumIn = 0.0;
        double sumOut = 0.0;
        for (std::size_t k = settle; k < settle + len; ++k) {
            sumIn += static_cast<double>(r.L[k]) * static_cast<double>(r.L[k]);
            sumOut += static_cast<double>(y[k]) * static_cast<double>(y[k]);
        }
        REQUIRE(sumIn > 0.0);
        const double levelDb = PT::safeDb10(sumOut / sumIn);
        REQUIRE(finiteBits(levelDb));
        std::printf("SC-022(f) HarmonicFold_RecordedLevelNonSine %s, C2 48 kHz: Fold-100 level change %+.2f dB "
                    "(plan static %+.2f)\n",
                    inputs[i].name, levelDb, plan[i]);
    }
}

// =============================================================================
// Evaluation (not a gate): listening renders, 32-bit float stereo WAVs, 48 kHz (spec
// "Evaluation", plan S8.2). Hidden tag: run explicitly with "[.listen]" from the repo root.
// =============================================================================

TEST_CASE("ProfundumEdgeFold_ListeningRenders", "[systems][profundum][edge-fold][.listen]") {
    std::error_code ec;
    std::filesystem::create_directories(phase2ListenDir(), ec);
    INFO("create " << phase2ListenDir().string() << ": " << ec.message());
    REQUIRE_FALSE(ec);
    std::size_t written = 0;

    const std::size_t sweepTotal = toSamples(6.0, kFs);
    const std::size_t chainTotal = toSamples(4.0, kFs);
    const std::array<NamedInput, 6> sixStates{{{"kSineAnchor", SpectralShapeRecipe::kSineAnchor},
                                               {"kTriangleAnchor", SpectralShapeRecipe::kTriangleAnchor},
                                               {"kSawAnchor", SpectralShapeRecipe::kSawAnchor},
                                               {"kHeavy", SpectralShapeRecipe::kHeavy},
                                               {"kHollow", SpectralShapeRecipe::kHollow},
                                               {"kGrowl", SpectralShapeRecipe::kGrowl}}};
    const std::array<std::pair<int, const char*>, 4> notes{
        {{kNoteE0, "E0"}, {kNoteE1, "E1"}, {kNoteE2, "E2"}, {kNoteC2, "C2"}}};
    const Controls mid = SpectralShapeRecipe::kDefaultControls;

    for (const auto& [note, noteName] : notes) {
        const float f0 = midiHz(note);
        const std::string tag = std::string("_") + noteName + "_48k.wav";

        // Excitation drive sweep 0 -> 1 (mix 1, asymmetry 0), mid grid.
        {
            const PT::Render r = renderCoreSweep(mid, kDriveZero, f0, sweepTotal, [](ProfundumCore& c, double t) {
                c.setExcitation(Excitation{.mix = 1.0f, .drive = static_cast<float>(t), .asymmetry = 0.0f});
            });
            writePhase2Listen("exc_drive_sweep_mid" + tag, r.L, r.R, written);
        }
        // Excitation asymmetry sweep 0 -> 1 (mix 1, drive 0.5), mid grid.
        {
            const Excitation start{.mix = 1.0f, .drive = 0.5f, .asymmetry = 0.0f};
            const PT::Render r = renderCoreSweep(mid, start, f0, sweepTotal, [](ProfundumCore& c, double t) {
                c.setExcitation(Excitation{.mix = 1.0f, .drive = 0.5f, .asymmetry = static_cast<float>(t)});
            });
            writePhase2Listen("exc_asymmetry_sweep_mid" + tag, r.L, r.R, written);
        }
        // Recipe Edge sweep 0 -> 1 into the excitation (mix 1, drive 0.5, asymmetry 0), mid grid.
        {
            const Excitation ex{.mix = 1.0f, .drive = 0.5f, .asymmetry = 0.0f};
            Controls start = mid;
            start.edge = 0.0f;
            const PT::Render r = renderCoreSweep(start, ex, f0, sweepTotal, [mid](ProfundumCore& c, double t) {
                Controls k = mid;
                k.edge = static_cast<float>(t);
                c.setControls(k);
            });
            writePhase2Listen("exc_edge_sweep_mid" + tag, r.L, r.R, written);
        }
        // Fold 0 -> 100 on the six named states (excitation off).
        for (const NamedInput& s : sixStates) {
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(kFs);
            const PT::Render r = renderCoreStage(*core, s.controls, kExcitationOff, f0, sweepTotal, kFs);
            const Stereo y = foldSweep(r.L, r.R, f0);
            writePhase2Listen(std::string("fold_sweep_") + s.name + tag, y.first, y.second, written);
        }
        // The chain at maximum (max excitation -> Fold 100): mid grid and the SC-009 state.
        for (const NamedInput& s : {NamedInput{"mid_grid", mid}, NamedInput{"sc009_state", sc009State()}}) {
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(kFs);
            auto fold = std::make_unique<HarmonicFold>();
            fold->prepare(kFs, kHostBlock);
            const Stereo y = renderChain(*core, *fold, s.controls, f0, chainTotal, kFs);
            writePhase2Listen(std::string("chain_max_") + s.name + tag, y.first, y.second, written);
        }
    }
    std::printf("ProfundumEdgeFold_ListeningRenders: %zu WAVs written to %s\n", written,
                std::filesystem::absolute(phase2ListenDir()).string().c_str());
}
