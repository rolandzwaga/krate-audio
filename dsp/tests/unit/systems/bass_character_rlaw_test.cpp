// ==============================================================================
// Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// Character r-law gates (tasks.md T018; plan S9.1; C-2, C-3, C-4, C-5; deviation 4):
//   SC-012(a)    BassCharacter_RLawSmoke, BassCharacter_RLawMatchesReference_{Sine,Triangle,Saw,Growl} [long]
//   SC-012(b)(g) BassCharacter_RectifyFlipPoint [long]
//   SC-026(a)    BassCharacter_RLawHysteresisBandWidth [long]                (C-4 intersection band)
//   SC-012(c)    Rectifier_RLawEvidence [long]                                (recorded; finiteness only)
//   SC-012(d)    BassCharacter_CrushFractureRLaw [long]                       (C-3, C-5)
//   SC-024       BassCharacter_FractureRLawAndSidebands [long]                (C-5)
//   SC-012(f)    BassCharacter_RIsOneSmoke, BassCharacter_RIsOne [long]       (Tube / Fold / Bite; S6.5)
//   plan T030    BassCharacter_RLawHarvest [.calibration]                     (run by T024 at close)
//
// Conventions: 48 kHz; render = 1 s settle + the 262144-sample SHS frame; host block 512, L == R
// copies; the stage is configured BEFORE prepare (prepare()'s reset() snaps every smoother and
// clears the observer, so each cell is steady from sample 0). The reference is the FR-035
// estimator through ShapeCharacterTest::shsCandidates (C-2 ambiguity rule, C-3 readsF0). Input
// readings are cached per (fixture, note, raw / Fold-100). Every [long] case writes its cells to
// specs/profundum-phase5-shape-character/evidence/sc012-<clause>*.csv (or sc024 / sc026a).
//
// The Rectify descriptor (setInputHalfWaveSymmetric) is observer-only: it is written at
// bass_character_stage.h:268 and read only at :382 (perceivedPitchRatio). SC-012(a)'s two runs
// therefore share one render per cell and read the observer twice; BassCharacter_RLawSmoke proves
// the renders bit-identical with the descriptor true and false.
// ==============================================================================

#include "bass_character_stage_probe.h"
#include "shape_character_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/closed_form_adaa2.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/keytracked_tilt.h>
#include <krate/dsp/processors/oversampled_residual_path.h>
#include <krate/dsp/processors/rectifier.h>
#include <krate/dsp/systems/shape_morph.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/processors/harmonic_fold.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace SCT = Krate::DSP::ShapeCharacterTest;
namespace HFT = Krate::DSP::HarmonicFoldTest;

using RlwAlgo = BassCharacterStage::Algorithm;
using RlwProbe = Krate::DSP::detail::BassCharacterStageProbe;

constexpr double kRlwFs = 48000.0;
constexpr std::size_t kRlwBlock = 512;
constexpr std::size_t kRlwFrame = TestUtils::kLowFrequencyFftSize;   // 262144
constexpr std::size_t kRlwSettle = 48000;                             // 1 s at 48 kHz
constexpr std::size_t kRlwTotal = kRlwSettle + kRlwFrame;
constexpr double kRlwCentsBar = 1.0;                  // SC-012 every clause
constexpr double kRlwMaxAmbiguousFraction = 0.20;     // SC-012 (a)(d)(e)
constexpr int kRlwMinUnambiguous = 8;                 // SC-012 (a)(d)(e)
constexpr double kRlwBisectWidth = 0.01;              // SC-012(b): located to 0.01 of Amount
constexpr double kRlwBandDb = 1.0;                    // C-2 / C-4: A_lo at -1 dB, A_hi at +1 dB
constexpr double kRlwCandidateMatchCents = 50.0;      // mapping a candidate onto k·f0 (octave apart)
constexpr double kRlwOddHalfAmountZeroDb = -60.0;     // SC-024(b)
constexpr double kRlwOddHalfAmountOneDb = -40.0;      // SC-024(b)
constexpr double kRlwMinMoveCents = 100.0;            // SC-024(a)

// Fixture indices into ShapeCharacterTest::kFixtures (sine, triangle, saw, growl).
constexpr std::size_t kRlwSine = 0;
constexpr std::size_t kRlwTriangle = 1;
constexpr std::size_t kRlwSaw = 2;
constexpr std::size_t kRlwGrowl = 3;

struct RlwNote {
    const char* name;
    double hz;
};

/// {E0, E1, E2, C2, C3} (SC-012 (a)(b)(c)).
std::array<RlwNote, 5> rlwNotes5() {
    return {{{"E0", SCT::kE0Hz}, {"E1", SCT::kE1Hz}, {"E2", SCT::kE2Hz}, {"C2", SCT::kC2Hz}, {"C3", SCT::kC3Hz}}};
}

/// {E0, E1, E2, C3} (SC-012 (d)(f), SC-024).
std::array<RlwNote, 4> rlwNotes4() {
    return {{{"E0", SCT::kE0Hz}, {"E1", SCT::kE1Hz}, {"E2", SCT::kE2Hz}, {"C3", SCT::kC3Hz}}};
}

constexpr std::array<float, 3> kRlwTones3{0.0f, 0.5f, 1.0f};
constexpr std::array<float, 5> kRlwTones5{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

/// Index of `tone` in the observer tables (Tone 0, 0.25, 0.5, 0.75, 1 -> 0..4).
std::size_t rlwToneTableIndex(float tone) {
    return static_cast<std::size_t>(std::lround(static_cast<double>(tone) * 4.0));
}

float rlwAmountStep(int i, int steps) { return static_cast<float>(i) / static_cast<float>(steps); }

const char* rlwAlgoName(RlwAlgo a) {
    switch (a) {
        case RlwAlgo::Tube:
            return "Tube";
        case RlwAlgo::Fold:
            return "Fold";
        case RlwAlgo::Rectify:
            return "Rectify";
        case RlwAlgo::Bite:
            return "Bite";
        case RlwAlgo::Crush:
            return "Crush";
        case RlwAlgo::Fracture:
            return "Fracture";
    }
    return "?";
}

const char* rlwVariantName(bool fold) { return fold ? "Fold-100" : "raw"; }

std::string rlwEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase5-shape-character/evidence/";
}

std::ofstream rlwOpenEvidence(const std::string& file) {
    const std::string dir = rlwEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + file);
    REQUIRE(out.is_open());
    out << std::setprecision(10);
    return out;
}

// -----------------------------------------------------------------------------
// Inputs and the reference
// -----------------------------------------------------------------------------

/// SHS candidates on the last kRlwFrame samples (the analysis frame, after the 1 s settle).
SCT::ShsCandidates rlwShs(const std::vector<float>& y, double f0) {
    REQUIRE(y.size() >= kRlwTotal);
    return SCT::shsCandidates(y.data() + (y.size() - kRlwFrame), kRlwFs, f0);
}

struct RlwInput {
    std::vector<float> x;
    SCT::ShsCandidates shs;
};

/// Inputs and their SHS readings, cached per (fixture, note, raw / Fold-100). Entries live in a
/// deque, so returned references stay valid while the cache grows.
class RlwInputCache {
public:
    const RlwInput& get(std::size_t fixture, double f0, bool fold) {
        for (const Entry& e : entries_) {
            if (e.fixture == fixture && e.f0 == f0 && e.fold == fold)
                return e.in;
        }
        RlwInput in;
        if (fold) {
            in.x = SCT::fold100(get(fixture, f0, false).x, kRlwFs, f0);
        } else {
            const double seconds = static_cast<double>(kRlwTotal) / kRlwFs;
            in.x = SCT::fixture(SCT::kFixtures[fixture].controls, f0, kRlwFs, seconds);
        }
        REQUIRE(in.x.size() == kRlwTotal);
        in.shs = rlwShs(in.x, f0);
        entries_.push_back(Entry{.fixture = fixture, .f0 = f0, .fold = fold, .in = std::move(in)});
        return entries_.back().in;
    }

private:
    struct Entry {
        std::size_t fixture;
        double f0;
        bool fold;
        RlwInput in;
    };
    std::deque<Entry> entries_;
};

/// The stage at one steady cell: every control set before prepare (so prepare()'s reset() snaps
/// the smoothers, re-keys the branches and clears the observer: the cell approached from the
/// default state, S5.7).
void rlwConfigure(BassCharacterStage& s, RlwAlgo algo, float amount, float tone, double f0, bool descriptor,
                  float amountOffset = 0.0f, float toneOffset = 0.0f) {
    s.setAlgorithm(algo);
    s.setInputHalfWaveSymmetric(descriptor);
    s.setTone(tone);
    s.setAmount(amount);
    s.setChannelOffsets(amountOffset, toneOffset);
    s.setFundamentalHz(static_cast<float>(f0));
    s.prepare(kRlwFs, kRlwBlock);
}

/// L / R render of `in` (both channels fed the same signal) in 512-sample host blocks.
std::pair<std::vector<float>, std::vector<float>> rlwRenderStereo(BassCharacterStage& s, const std::vector<float>& in) {
    std::vector<float> L = in;
    std::vector<float> R = in;
    for (std::size_t pos = 0; pos < L.size(); pos += kRlwBlock) {
        const std::size_t n = std::min(kRlwBlock, L.size() - pos);
        s.processBlock(L.data() + pos, R.data() + pos, n);
    }
    return {std::move(L), std::move(R)};
}

std::vector<float> rlwRender(BassCharacterStage& s, const std::vector<float>& in) {
    return rlwRenderStereo(s, in).first;
}

bool rlwBitIdentical(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i]))
            return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Matching the observer against the reference (SC-012 ambiguity rule, C-2)
// -----------------------------------------------------------------------------

struct RlwCandidate {
    double hz;
    double db;
};

/// The three candidates {f̂/2, f̂, 2·f̂}, best first.
std::array<RlwCandidate, 3> rlwRankedCandidates(const SCT::ShsCandidates& c) {
    std::array<RlwCandidate, 3> k{{{0.5 * c.fHat, c.sHalfDb}, {c.fHat, c.sOneDb}, {2.0 * c.fHat, c.sDoubleDb}}};
    std::sort(k.begin(), k.end(), [](const RlwCandidate& a, const RlwCandidate& b) { return a.db > b.db; });
    return k;
}

struct RlwMatch {
    bool pass;
    double devCents;   ///< observer re f̂ (unambiguous) or re the nearer of the two best candidates
};

/// Unambiguous: r_obs·f0 within 1 cent of f̂_out. Ambiguous: within 1 cent of one of the two
/// best candidates.
RlwMatch rlwMatch(const SCT::ShsCandidates& c, double observedHz) {
    if (!c.ambiguous) {
        const double d = SCT::centsBetween(observedHz, c.fHat);
        return {.pass = std::abs(d) <= kRlwCentsBar, .devCents = d};
    }
    const std::array<RlwCandidate, 3> k = rlwRankedCandidates(c);
    const double d0 = SCT::centsBetween(observedHz, k[0].hz);
    const double d1 = SCT::centsBetween(observedHz, k[1].hz);
    const double d = std::abs(d0) <= std::abs(d1) ? d0 : d1;
    return {.pass = std::abs(d) <= kRlwCentsBar, .devCents = d};
}

/// The candidate salience (dB re the best) at ratio·f0, if one of the three candidates sits there.
std::optional<double> rlwSalienceAt(const SCT::ShsCandidates& c, double f0, double ratio) {
    const std::array<RlwCandidate, 3> k{{{0.5 * c.fHat, c.sHalfDb}, {c.fHat, c.sOneDb}, {2.0 * c.fHat, c.sDoubleDb}}};
    for (const RlwCandidate& cand : k) {
        if (std::abs(SCT::centsBetween(cand.hz, ratio * f0)) <= kRlwCandidateMatchCents)
            return cand.db;
    }
    return std::nullopt;
}

/// S(num·f0) / S(den·f0) in dB from the candidates. REQUIREs both to be candidates.
double rlwSalienceRatioDb(const SCT::ShsCandidates& c, double f0, double num, double den) {
    const std::optional<double> sNum = rlwSalienceAt(c, f0, num);
    const std::optional<double> sDen = rlwSalienceAt(c, f0, den);
    INFO("f0 = " << f0 << " Hz, f_hat = " << c.fHat << " Hz: no candidate at " << num << "·f0 or " << den
                 << "·f0 (S(f/2) " << c.sHalfDb << ", S(f) " << c.sOneDb << ", S(2f) " << c.sDoubleDb << " dB)");
    REQUIRE(sNum.has_value());
    REQUIRE(sDen.has_value());
    return *sNum - *sDen;
}

/// Per (fixture, note) ambiguity accounting over the gated cells (SC-012 (a)(d)(e)).
struct RlwGroupStats {
    int gated = 0;
    int ambiguous = 0;
};

void rlwCheckCaps(const RlwGroupStats& st, const std::string& group) {
    if (st.gated == 0)
        return;
    const double fraction = static_cast<double>(st.ambiguous) / static_cast<double>(st.gated);
    const int unambiguous = st.gated - st.ambiguous;
    INFO(group << ": " << st.ambiguous << " ambiguous of " << st.gated << " gated cells (fraction " << fraction
               << ", cap 0.20), " << unambiguous << " unambiguous (floor 8)");
    CHECK(fraction <= kRlwMaxAmbiguousFraction);
    CHECK(unambiguous >= kRlwMinUnambiguous);
}

// -----------------------------------------------------------------------------
// Amount sweeps and bisection (SC-012(b), SC-026(a), harvest)
// -----------------------------------------------------------------------------

/// SHS readings of one (algorithm, Tone, f0, input, offsets) cell as a function of the centre
/// Amount, memoised (the 0 dB, -1 dB and +1 dB bisections share their dyadic Amounts).
class RlwAmountSweep {
public:
    RlwAmountSweep(RlwAlgo algo, float tone, double f0, const std::vector<float>& in, bool stereo = false,
                   float amountOffset = 0.0f, float toneOffset = 0.0f)
        : algo_(algo), tone_(tone), f0_(f0), in_(&in), stereo_(stereo), amountOffset_(amountOffset),
          toneOffset_(toneOffset) {}

    /// Channel 0 (L) or 1 (R) reading at Amount a.
    const SCT::ShsCandidates& at(float a, int channel = 0) {
        auto it = cache_.find(a);
        if (it == cache_.end()) {
            BassCharacterStage s;
            rlwConfigure(s, algo_, a, tone_, f0_, true, amountOffset_, toneOffset_);
            const auto [L, R] = rlwRenderStereo(s, *in_);
            Reading r{.left = rlwShs(L, f0_), .right = {}};
            r.right = stereo_ ? rlwShs(R, f0_) : r.left;
            it = cache_.emplace(a, r).first;
        }
        return channel == 0 ? it->second.left : it->second.right;
    }

private:
    struct Reading {
        SCT::ShsCandidates left;
        SCT::ShsCandidates right;
    };
    RlwAlgo algo_;
    float tone_;
    double f0_;
    const std::vector<float>* in_;
    bool stereo_;
    float amountOffset_;
    float toneOffset_;
    std::map<float, Reading> cache_;
};

/// Bisects Amount in [0, 1] for value(A) == target until the bracket is <= 0.01 wide; returns its
/// midpoint. nullopt when the crossing is not bracketed (value(0) < target < value(1) fails).
template <class Value>
std::optional<double> rlwBisect(const Value& value, double target) {
    float lo = 0.0f;
    float hi = 1.0f;
    if (!(value(lo) < target && value(hi) > target))
        return std::nullopt;
    while (static_cast<double>(hi - lo) > kRlwBisectWidth) {
        const float mid = 0.5f * (lo + hi);
        if (value(mid) < target)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (static_cast<double>(lo) + static_cast<double>(hi));
}

struct RlwBand {
    std::optional<double> lo;     ///< -1 dB crossing (A_lo)
    std::optional<double> flip;   ///< 0 dB crossing
    std::optional<double> hi;     ///< +1 dB crossing (A_hi)
};

/// The ambiguous band of the crossing S(num·f0)/S(f0) on channel `channel` of `sweep`.
RlwBand rlwBand(RlwAmountSweep& sweep, double f0, double num, int channel = 0, bool withFlip = true) {
    const auto value = [&](float a) { return rlwSalienceRatioDb(sweep.at(a, channel), f0, num, 1.0); };
    RlwBand b{};
    b.lo = rlwBisect(value, -kRlwBandDb);
    if (withFlip)
        b.flip = rlwBisect(value, 0.0);
    b.hi = rlwBisect(value, kRlwBandDb);
    return b;
}

std::string rlwOpt(const std::optional<double>& v) {
    if (!v.has_value())
        return "none";
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%.4f", *v);
    return buf.data();
}

// -----------------------------------------------------------------------------
// Odd multiples of f0/2 (SC-024(b))
// -----------------------------------------------------------------------------

/// Power within ±8 BH7 bins of the odd multiples of f0/2 (below fs/2), dB re the total power, on
/// the analysis frame (262144 points: bin 0.18 Hz, so the f0/2 lattice is resolved at E0).
double rlwOddHalfLatticeDb(const std::vector<float>& y, double f0) {
    REQUIRE(y.size() >= kRlwTotal);
    constexpr std::size_t L = kRlwFrame;
    const std::vector<double> p = HFT::bh7PowerSpectrum(y.data() + (y.size() - L), L);
    const double binHz = kRlwFs / static_cast<double>(L);
    const std::size_t top = L / 2;
    const double half = 0.5 * f0;
    std::vector<char> inLattice(top + 1, 0);
    for (int k = 0; (2.0 * static_cast<double>(k) + 1.0) * half < 0.5 * kRlwFs; ++k) {
        const double centre = (2.0 * static_cast<double>(k) + 1.0) * half / binHz;
        const double lo = std::max(0.0, std::ceil(centre - 8.0));
        const double hi = std::min(static_cast<double>(top), std::floor(centre + 8.0));
        for (auto b = static_cast<std::size_t>(lo); static_cast<double>(b) <= hi; ++b)
            inLattice[b] = 1;
    }
    double odd = 0.0;
    double total = 0.0;
    for (std::size_t b = 0; b <= top; ++b) {
        total += p[b];
        if (inLattice[b] != 0)
            odd += p[b];
    }
    REQUIRE(total > 0.0);
    return HFT::safeDb10(odd / total);
}

// -----------------------------------------------------------------------------
// SC-012(a) shard body
// -----------------------------------------------------------------------------

/// Rectify, Amount 0..1 in 11 steps × Tone {0, 0.5, 1} × {E0, E1, E2, C2, C3} on one fixture, read
/// with the voice's descriptor (true on sine / triangle, false on saw / growl) and the default
/// (false). Gated cells (C-3: the input reads f0 unambiguously; kGrowl never): the observer matches
/// the reference (ambiguity rule C-2), caps per (fixture, note). The default run gates saw / growl
/// cells only (identical to the voice run there); sine / triangle default deviations are recorded.
void rlwRectifyShard(std::size_t fixture) {
    const char* name = SCT::kFixtures[fixture].name;
    const bool voiceDescriptor = fixture == kRlwSine || fixture == kRlwTriangle;
    std::ofstream csv = rlwOpenEvidence(std::string("sc012-a-") + name + ".csv");
    csv << "run,amount,tone,fixture,note,f0_hz,b,wet,fhat_in_hz,gap_in_db,input_reads_f0,fhat_out_hz,gap_out_db,"
           "ambiguous_out,r_obs,dev_cents,gated,pass\n";

    RlwInputCache cache;
    int failing = 0;
    for (const RlwNote& note : rlwNotes5()) {
        const RlwInput& in = cache.get(fixture, note.hz, false);
        const bool inputGated = in.shs.readsF0 && fixture != kRlwGrowl;   // C-3; kGrowl recorded only
        std::array<RlwGroupStats, 2> stats{};                              // [0] voice run, [1] default run
        for (const float tone : kRlwTones3) {
            for (int i = 0; i <= 10; ++i) {
                const float amount = rlwAmountStep(i, 10);
                BassCharacterStage s;
                rlwConfigure(s, RlwAlgo::Rectify, amount, tone, note.hz, voiceDescriptor);
                const std::vector<float> y = rlwRender(s, in.x);
                const RlwProbe::RectifyMix mix = RlwProbe::rectifyMix(s, 0);
                const float rVoice = s.perceivedPitchRatio();
                s.setInputHalfWaveSymmetric(false);
                const float rDefault = s.perceivedPitchRatio();
                const SCT::ShsCandidates out = rlwShs(y, note.hz);

                for (int run = 0; run < 2; ++run) {
                    const float r = run == 0 ? rVoice : rDefault;
                    const bool gated = inputGated && (run == 0 || !voiceDescriptor);
                    const RlwMatch m = rlwMatch(out, static_cast<double>(r) * note.hz);
                    auto& st = stats[static_cast<std::size_t>(run)];
                    if (gated) {
                        ++st.gated;
                        if (out.ambiguous)
                            ++st.ambiguous;
                        if (!m.pass) {
                            ++failing;
                            FAIL_CHECK("SC-012(a) Rectify " << (run == 0 ? "voice-descriptor" : "default") << " run, "
                                       << name << ", " << note.name << ", Tone " << tone << ", Amount " << amount
                                       << ": r_obs = " << r << ", f_hat_out = " << out.fHat << " Hz (gap "
                                       << out.bestMinusSecondDb << " dB, ambiguous " << out.ambiguous
                                       << "), deviation " << m.devCents << " cents (bar 1)");
                        }
                    }
                    csv << (run == 0 ? "voice" : "default") << ',' << amount << ',' << tone << ',' << name << ','
                        << note.name << ',' << note.hz << ',' << mix.b << ',' << mix.wet << ',' << in.shs.fHat << ','
                        << in.shs.bestMinusSecondDb << ',' << (in.shs.readsF0 ? 1 : 0) << ',' << out.fHat << ','
                        << out.bestMinusSecondDb << ',' << (out.ambiguous ? 1 : 0) << ',' << r << ',' << m.devCents
                        << ',' << (gated ? 1 : 0) << ',' << (m.pass ? 1 : 0) << '\n';
                }
            }
        }
        rlwCheckCaps(stats[0], std::string("SC-012(a) voice run, ") + name + ", " + note.name);
        if (!voiceDescriptor)
            rlwCheckCaps(stats[1], std::string("SC-012(a) default run, ") + name + ", " + note.name);
    }
    INFO("SC-012(a) " << name << ": " << failing << " failing gated cells");
    CHECK(failing == 0);
}

}  // namespace

// =============================================================================
// SC-012(a) smoke
// =============================================================================

TEST_CASE("BassCharacter_RLawSmoke", "[systems][profundum][shape-character][bass-character]") {
    const double f0 = SCT::kE1Hz;
    RlwInputCache cache;
    const RlwInput& in = cache.get(kRlwSine, f0, false);
    REQUIRE(in.shs.readsF0);

    for (const float amount : {0.0f, 1.0f}) {
        BassCharacterStage s;
        rlwConfigure(s, RlwAlgo::Rectify, amount, 0.5f, f0, true);
        const std::vector<float> y = rlwRender(s, in.x);
        const float r = s.perceivedPitchRatio();
        const SCT::ShsCandidates out = rlwShs(y, f0);
        const double expected = amount == 0.0f ? 1.0 : 2.0;
        const double cents = SCT::centsBetween(out.fHat, expected * f0);
        INFO("Rectify, descriptor true, kSineAnchor E1, Tone 0.5, Amount " << amount << ": r_obs = " << r
                                                                          << ", f_hat_out = " << out.fHat << " Hz (gap "
                                                                          << out.bestMinusSecondDb << " dB), "
                                                                          << cents << " cents re " << expected
                                                                          << "·f0");
        CHECK(r == static_cast<float>(expected));
        CHECK_FALSE(out.ambiguous);
        CHECK(std::abs(cents) <= kRlwCentsBar);

        // The descriptor is observer-only: the render with it false is bit-identical (the
        // premise for SC-012(a) reading both runs off one render).
        BassCharacterStage sDefault;
        rlwConfigure(sDefault, RlwAlgo::Rectify, amount, 0.5f, f0, false);
        const std::vector<float> yDefault = rlwRender(sDefault, in.x);
        CHECK(rlwBitIdentical(y, yDefault));
    }
}

// =============================================================================
// SC-012(a): one shard per fixture
// =============================================================================

TEST_CASE("BassCharacter_RLawMatchesReference_Sine", "[systems][profundum][shape-character][bass-character][long]") {
    rlwRectifyShard(kRlwSine);
}

TEST_CASE("BassCharacter_RLawMatchesReference_Triangle",
          "[systems][profundum][shape-character][bass-character][long]") {
    rlwRectifyShard(kRlwTriangle);
}

TEST_CASE("BassCharacter_RLawMatchesReference_Saw", "[systems][profundum][shape-character][bass-character][long]") {
    rlwRectifyShard(kRlwSaw);
}

TEST_CASE("BassCharacter_RLawMatchesReference_Growl", "[systems][profundum][shape-character][bass-character][long]") {
    rlwRectifyShard(kRlwGrowl);
}

// =============================================================================
// SC-012(b) flip point (C-4) and SC-012(g) L/R mismatch (recorded)
// =============================================================================

TEST_CASE("BassCharacter_RectifyFlipPoint", "[systems][profundum][shape-character][bass-character][long]") {
    std::ofstream csvB = rlwOpenEvidence("sc012-b.csv");
    csvB << "fixture,note,tone,a_lo,a_flip,a_hi,width,edge_lo,edge_hi,edges_in_band,ambiguous_at_0,ambiguous_at_1\n";
    std::ofstream csvG = rlwOpenEvidence("sc012-g.csv");
    csvG << "fixture,note,tone,amount_offset,tone_offset,centre_flip,flip_L,flip_R,delta_L,delta_R\n";

    RlwInputCache cache;
    for (const std::size_t fixture : {kRlwSine, kRlwTriangle}) {
        const char* name = SCT::kFixtures[fixture].name;
        for (const RlwNote& note : rlwNotes5()) {
            const RlwInput& in = cache.get(fixture, note.hz, false);
            for (const float tone : kRlwTones3) {
                // (b) centre.
                RlwAmountSweep sweep(RlwAlgo::Rectify, tone, note.hz, in.x);
                const bool amb0 = sweep.at(0.0f).ambiguous;
                const bool amb1 = sweep.at(1.0f).ambiguous;
                const RlwBand band = rlwBand(sweep, note.hz, 2.0);
                const BassCharacterStage::FlipEdges edges =
                    BassCharacterStage::kRectifyFlipEdges[rlwToneTableIndex(tone)];
                INFO("SC-012(b) " << name << ", " << note.name << ", Tone " << tone << ": A_lo " << rlwOpt(band.lo)
                                  << ", flip " << rlwOpt(band.flip) << ", A_hi " << rlwOpt(band.hi)
                                  << "; observer edges [" << edges.lo << ", " << edges.hi << "]");
                CHECK_FALSE(amb0);
                CHECK_FALSE(amb1);
                REQUIRE(band.lo.has_value());
                REQUIRE(band.flip.has_value());
                REQUIRE(band.hi.has_value());
                const double aLo = *band.lo;
                const double aHi = *band.hi;
                const auto inBand = [&](float e) {
                    return static_cast<double>(e) >= aLo && static_cast<double>(e) <= aHi;
                };
                const bool edgesInBand = inBand(edges.lo) && inBand(edges.hi);
                CHECK(inBand(edges.lo));
                CHECK(inBand(edges.hi));
                csvB << name << ',' << note.name << ',' << tone << ',' << aLo << ',' << *band.flip << ',' << aHi << ','
                     << (aHi - aLo) << ',' << edges.lo << ',' << edges.hi << ',' << (edgesInBand ? 1 : 0) << ','
                     << (amb0 ? 1 : 0) << ',' << (amb1 ? 1 : 0) << '\n';

                // (g) recorded: L and R flip Amounts (per channel) at the maximum offsets (D-12).
                RlwAmountSweep stereo(RlwAlgo::Rectify, tone, note.hz, in.x, true, BassCharacterStage::kMaxChannelOffset,
                                      BassCharacterStage::kMaxChannelOffset);
                const auto value = [&](int ch) {
                    return [&stereo, &note, ch](float a) {
                        return rlwSalienceRatioDb(stereo.at(a, ch), note.hz, 2.0, 1.0);
                    };
                };
                const std::optional<double> flipL = rlwBisect(value(0), 0.0);
                const std::optional<double> flipR = rlwBisect(value(1), 0.0);
                const auto delta = [&](const std::optional<double>& f) -> std::optional<double> {
                    if (!f.has_value())
                        return std::nullopt;
                    return *f - *band.flip;
                };
                csvG << name << ',' << note.name << ',' << tone << ',' << BassCharacterStage::kMaxChannelOffset << ','
                     << BassCharacterStage::kMaxChannelOffset << ',' << *band.flip << ',' << rlwOpt(flipL) << ','
                     << rlwOpt(flipR) << ',' << rlwOpt(delta(flipL)) << ',' << rlwOpt(delta(flipR)) << '\n';
            }
        }
    }
}

// =============================================================================
// SC-026(a) under C-4: the observer's hysteresis width equals the intersection band
// =============================================================================

TEST_CASE("BassCharacter_RLawHysteresisBandWidth", "[systems][profundum][shape-character][bass-character][long]") {
    std::ofstream csv = rlwOpenEvidence("sc026a.csv");
    csv << "tone,max_a_lo,min_a_hi,band_width,up_flip,down_flip,hysteresis_width,up_steps,down_steps\n";

    constexpr int kSteps = 1280;  // 1/1280 Amount steps: lands exactly on the k/128 bisection lattice the edges live on
                                  // (close 2026-10-16: a 0.005 grid read the up-flip 0.005 past an exact edge)
    const std::array<RlwNote, 2> notes{{{"E1", SCT::kE1Hz}, {"C3", SCT::kC3Hz}}};
    RlwInputCache cache;
    for (const float tone : kRlwTones3) {
        // The four cells' SC-012(b) bands.
        std::vector<std::pair<double, double>> bands;
        double maxLo = 0.0;
        double minHi = 1.0;
        for (const std::size_t fixture : {kRlwSine, kRlwTriangle}) {
            for (const RlwNote& note : notes) {
                const RlwInput& in = cache.get(fixture, note.hz, false);
                RlwAmountSweep sweep(RlwAlgo::Rectify, tone, note.hz, in.x);
                const RlwBand band = rlwBand(sweep, note.hz, 2.0, 0, false);
                INFO("SC-026(a) " << SCT::kFixtures[fixture].name << ", " << note.name << ", Tone " << tone
                                  << ": A_lo " << rlwOpt(band.lo) << ", A_hi " << rlwOpt(band.hi));
                REQUIRE(band.lo.has_value());
                REQUIRE(band.hi.has_value());
                bands.emplace_back(*band.lo, *band.hi);
                maxLo = std::max(maxLo, *band.lo);
                minHi = std::min(minHi, *band.hi);
            }
        }

        // The observer swept 0 -> 1 -> 0 (it reads the centre targets only, so one stage per Tone).
        BassCharacterStage s;
        s.setAlgorithm(RlwAlgo::Rectify);
        s.setInputHalfWaveSymmetric(true);
        s.setTone(tone);
        s.prepare(kRlwFs, kRlwBlock);
        REQUIRE(s.perceivedPitchRatio() == 1.0f);
        float r = s.perceivedPitchRatio();
        int upSteps = 0;
        int downSteps = 0;
        double upFlip = -1.0;
        double downFlip = -1.0;
        for (int i = 0; i <= kSteps; ++i) {
            const float a = rlwAmountStep(i, kSteps);
            s.setAmount(a);
            const float rNow = s.perceivedPitchRatio();
            if (rNow != r) {
                ++upSteps;
                if (upFlip < 0.0)
                    upFlip = static_cast<double>(a);
                r = rNow;
            }
        }
        for (int i = kSteps; i >= 0; --i) {
            const float a = rlwAmountStep(i, kSteps);
            s.setAmount(a);
            const float rNow = s.perceivedPitchRatio();
            if (rNow != r) {
                ++downSteps;
                if (downFlip < 0.0)
                    downFlip = static_cast<double>(a);
                r = rNow;
            }
        }
        const double width = upFlip - downFlip;
        const double bandWidth = minHi - maxLo;
        csv << tone << ',' << maxLo << ',' << minHi << ',' << bandWidth << ',' << upFlip << ',' << downFlip << ','
            << width << ',' << upSteps << ',' << downSteps << '\n';

        INFO("SC-026(a) Tone " << tone << ": intersection [" << maxLo << ", " << minHi << "] (width " << bandWidth
                               << "); observer up-flip " << upFlip << ", down-flip " << downFlip << " (width " << width
                               << "), steps up " << upSteps << " / down " << downSteps);
        CHECK(maxLo <= minHi);
        CHECK(upSteps == 1);
        CHECK(downSteps == 1);
        CHECK(upFlip >= downFlip);
        CHECK(std::abs(width - bandWidth) <= 0.01);
        for (const auto& [lo, hi] : bands) {
            INFO("cell band [" << lo << ", " << hi << "]");
            CHECK(upFlip >= lo);
            CHECK(upFlip <= hi);
            CHECK(downFlip >= lo);
            CHECK(downFlip <= hi);
        }
    }
}

// =============================================================================
// SC-012(c): Rectifier alone (recorded; finiteness only)
// =============================================================================

TEST_CASE("Rectifier_RLawEvidence", "[systems][profundum][shape-character][rectifier][long]") {
    std::ofstream csv = rlwOpenEvidence("sc012c.csv");
    csv << "b,fixture,note,f0_hz,fhat_in_hz,gap_in_db,fhat_out_hz,gap_out_db,ambiguous_out,r_ref_re_f0,"
           "r_ref_re_input\n";

    RlwInputCache cache;
    for (const float b : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        for (std::size_t fixture = 0; fixture < SCT::kFixtures.size(); ++fixture) {
            for (const RlwNote& note : rlwNotes5()) {
                const RlwInput& in = cache.get(fixture, note.hz, false);
                Rectifier rect;
                rect.setBlend(b);
                rect.prepare(kRlwFs, kRlwBlock);
                std::vector<float> L = in.x;
                std::vector<float> R = in.x;
                for (std::size_t pos = 0; pos < L.size(); pos += kRlwBlock) {
                    const std::size_t n = std::min(kRlwBlock, L.size() - pos);
                    rect.processBlock(L.data() + pos, R.data() + pos, n);
                }
                bool finite = true;
                for (const float v : L)
                    finite = finite && Krate::DSP::detail::isFinite(v);
                INFO("Rectifier b " << b << ", " << SCT::kFixtures[fixture].name << ", " << note.name);
                REQUIRE(finite);
                const SCT::ShsCandidates out = rlwShs(L, note.hz);
                const double rRef = out.fHat / note.hz;
                const double rIn = out.fHat / in.shs.fHat;
                CHECK(Krate::DSP::detail::isFinite(rRef));
                CHECK(Krate::DSP::detail::isFinite(rIn));
                csv << b << ',' << SCT::kFixtures[fixture].name << ',' << note.name << ',' << note.hz << ','
                    << in.shs.fHat << ',' << in.shs.bestMinusSecondDb << ',' << out.fHat << ','
                    << out.bestMinusSecondDb << ',' << (out.ambiguous ? 1 : 0) << ',' << rRef << ',' << rIn << '\n';
            }
        }
    }
}

// =============================================================================
// SC-012(d): Crush and Fracture (C-3, C-5)
// =============================================================================

TEST_CASE("BassCharacter_CrushFractureRLaw", "[systems][profundum][shape-character][bass-character][long]") {
    std::ofstream csv = rlwOpenEvidence("sc012-d.csv");
    csv << "algorithm,amount,tone,fixture,variant,note,f0_hz,fhat_in_hz,gap_in_db,input_reads_f0,fhat_out_hz,"
           "gap_out_db,ambiguous_out,r_obs,dev_cents,gated,pass\n";

    RlwInputCache cache;
    int failing = 0;
    for (const RlwAlgo algo : {RlwAlgo::Crush, RlwAlgo::Fracture}) {
        const std::vector<bool> variants =
            algo == RlwAlgo::Crush ? std::vector<bool>{false} : std::vector<bool>{false, true};
        for (std::size_t fixture = 0; fixture < SCT::kFixtures.size(); ++fixture) {
            const char* name = SCT::kFixtures[fixture].name;
            for (const bool fold : variants) {
                for (const RlwNote& note : rlwNotes4()) {
                    const RlwInput& in = cache.get(fixture, note.hz, fold);
                    // C-3: the input reads f0. C-5(c): Fracture gates on Fold-100 sine / triangle; the saw is
                    // recorded (user ruling 2026-10-16, Clarifications "Group 10 close rulings", C-31).
                    const bool gated = in.shs.readsF0
                                       && (algo == RlwAlgo::Crush
                                           || (fold && fixture != kRlwGrowl && fixture != kRlwSaw));
                    RlwGroupStats st{};
                    for (const float tone : kRlwTones3) {
                        for (int i = 0; i <= 4; ++i) {
                            const float amount = rlwAmountStep(i, 4);
                            BassCharacterStage s;
                            rlwConfigure(s, algo, amount, tone, note.hz, false);
                            const std::vector<float> y = rlwRender(s, in.x);
                            const float r = s.perceivedPitchRatio();
                            const SCT::ShsCandidates out = rlwShs(y, note.hz);
                            const RlwMatch m = rlwMatch(out, static_cast<double>(r) * note.hz);
                            if (gated) {
                                ++st.gated;
                                if (out.ambiguous)
                                    ++st.ambiguous;
                                if (!m.pass) {
                                    ++failing;
                                    FAIL_CHECK("SC-012(d) " << rlwAlgoName(algo) << ", " << name << ", "
                                               << rlwVariantName(fold) << ", " << note.name << ", Tone " << tone
                                               << ", Amount " << amount << ": r_obs = " << r << ", f_hat_out = "
                                               << out.fHat << " Hz (gap " << out.bestMinusSecondDb
                                               << " dB, ambiguous " << out.ambiguous << "), deviation "
                                               << m.devCents << " cents (bar 1)");
                                }
                            }
                            csv << rlwAlgoName(algo) << ',' << amount << ',' << tone << ',' << name << ','
                                << rlwVariantName(fold) << ',' << note.name << ',' << note.hz << ',' << in.shs.fHat
                                << ',' << in.shs.bestMinusSecondDb << ',' << (in.shs.readsF0 ? 1 : 0) << ','
                                << out.fHat << ',' << out.bestMinusSecondDb << ',' << (out.ambiguous ? 1 : 0) << ','
                                << r << ',' << m.devCents << ',' << (gated ? 1 : 0) << ',' << (m.pass ? 1 : 0)
                                << '\n';
                        }
                    }
                    rlwCheckCaps(st, std::string("SC-012(d) ") + rlwAlgoName(algo) + ", " + name + ", "
                                         + rlwVariantName(fold) + ", " + note.name);
                }
            }
        }
    }
    INFO("SC-012(d): " << failing << " failing gated cells");
    CHECK(failing == 0);
}

// =============================================================================
// SC-024 under C-5: Fracture r law and the odd-f0/2 sidebands
// =============================================================================

TEST_CASE("BassCharacter_FractureRLawAndSidebands", "[systems][profundum][shape-character][bass-character][long]") {
    std::ofstream csv = rlwOpenEvidence("sc024.csv");
    csv << "amount,tone,fixture,variant,note,f0_hz,r_obs,fhat_in_hz,input_reads_f0,fhat_out_hz,gap_out_db,"
           "ambiguous_out,dev_cents,move_cents,gated,pass,odd_half_db\n";

    RlwInputCache cache;
    int failing = 0;
    for (std::size_t fixture = 0; fixture < SCT::kFixtures.size(); ++fixture) {
        const char* name = SCT::kFixtures[fixture].name;
        for (const bool fold : {false, true}) {
            for (const RlwNote& note : rlwNotes4()) {
                const RlwInput& in = cache.get(fixture, note.hz, fold);
                // C-3, C-5(c); the Fold-100 saw is recorded (C-31, user ruling 2026-10-16).
                const bool gated = in.shs.readsF0 && fold && fixture != kRlwGrowl && fixture != kRlwSaw;
                RlwGroupStats st{};
                for (const float tone : kRlwTones3) {
                    float rPrev = 1.0f;
                    for (int i = 0; i <= 10; ++i) {
                        const float amount = rlwAmountStep(i, 10);
                        BassCharacterStage s;
                        rlwConfigure(s, RlwAlgo::Fracture, amount, tone, note.hz, false);
                        const std::vector<float> y = rlwRender(s, in.x);
                        const float r = s.perceivedPitchRatio();
                        const SCT::ShsCandidates out = rlwShs(y, note.hz);
                        const RlwMatch m = rlwMatch(out, static_cast<double>(r) * note.hz);
                        const double move = SCT::centsBetween(in.shs.fHat, out.fHat);
                        const double oddHalf = rlwOddHalfLatticeDb(y, note.hz);
                        const std::string cell = std::string(name) + ", " + rlwVariantName(fold) + ", " + note.name
                                                 + ", Tone " + std::to_string(tone) + ", Amount "
                                                 + std::to_string(amount);

                        // (a) the observer's values.
                        INFO("SC-024 " << cell << ": r_obs = " << r << ", f_hat_in = " << in.shs.fHat
                                       << " Hz, f_hat_out = " << out.fHat << " Hz (gap " << out.bestMinusSecondDb
                                       << " dB), move " << move << " cents, odd-f0/2 " << oddHalf << " dB re total");
                        CHECK((r == 1.0f || r == 0.5f));
                        CHECK(r >= 0.25f);
                        CHECK(r <= 4.0f);
                        CHECK(r <= rPrev);
                        if (i == 0)
                            CHECK(r == 1.0f);
                        if (i == 10)
                            CHECK(r == 0.5f);
                        rPrev = r;

                        // (a) the reference on the gated cells; the move at Amount 1.
                        if (gated) {
                            ++st.gated;
                            if (out.ambiguous)
                                ++st.ambiguous;
                            if (!m.pass) {
                                ++failing;
                                FAIL_CHECK("SC-024(a) Fracture " << cell << ": deviation " << m.devCents
                                                                 << " cents (bar 1)");
                            }
                            if (i == 10)
                                CHECK(std::abs(move) >= kRlwMinMoveCents);
                        }

                        // (b) the mechanism.
                        if (i == 0)
                            CHECK(oddHalf <= kRlwOddHalfAmountZeroDb);
                        if (i == 10)
                            CHECK(oddHalf >= kRlwOddHalfAmountOneDb);

                        csv << amount << ',' << tone << ',' << name << ',' << rlwVariantName(fold) << ',' << note.name
                            << ',' << note.hz << ',' << r << ',' << in.shs.fHat << ',' << (in.shs.readsF0 ? 1 : 0)
                            << ',' << out.fHat << ',' << out.bestMinusSecondDb << ',' << (out.ambiguous ? 1 : 0)
                            << ',' << m.devCents << ',' << move << ',' << (gated ? 1 : 0) << ',' << (m.pass ? 1 : 0)
                            << ',' << oddHalf << '\n';
                    }
                }
                rlwCheckCaps(st, std::string("SC-024(a) Fracture, ") + name + ", " + rlwVariantName(fold) + ", "
                                     + note.name);
            }
        }
    }
    INFO("SC-024(a): " << failing << " failing gated cells");
    CHECK(failing == 0);
}

// =============================================================================
// SC-012(f) under C-3: r_ref = f̂_out / f̂_in = 1 (Tube, Fold, Bite)
// =============================================================================

TEST_CASE("BassCharacter_RIsOneSmoke", "[systems][profundum][shape-character][bass-character]") {
    const double f0 = SCT::kE1Hz;
    RlwInputCache cache;
    const RlwInput& in = cache.get(kRlwSaw, f0, false);
    BassCharacterStage s;
    rlwConfigure(s, RlwAlgo::Tube, 1.0f, 0.5f, f0, false);
    const std::vector<float> y = rlwRender(s, in.x);
    const SCT::ShsCandidates out = rlwShs(y, f0);
    const double cents = SCT::centsBetween(out.fHat, in.shs.fHat);
    INFO("Tube, Amount 1, Tone 0.5, raw kSawAnchor E1: f_in = " << in.shs.fHat << " Hz (gap "
                                                               << in.shs.bestMinusSecondDb << " dB), f_out = "
                                                               << out.fHat << " Hz (gap " << out.bestMinusSecondDb
                                                               << " dB), r_ref = " << cents << " cents");
    CHECK_FALSE(in.shs.ambiguous);
    CHECK_FALSE(out.ambiguous);
    CHECK(std::abs(cents) <= kRlwCentsBar);
}

TEST_CASE("BassCharacter_RIsOne", "[systems][profundum][shape-character][bass-character][long]") {
    std::ofstream csv = rlwOpenEvidence("sc012f-character.csv");
    csv << "algorithm,tone,fixture,variant,note,f0_hz,fhat_in_hz,gap_in_db,ambiguous_in,fhat_out_hz,gap_out_db,"
           "ambiguous_out,r_ref_cents,gated,pass\n";

    // C-3 extended to (f) (user ruling 2026-10-16, Clarifications "Group 10 close rulings"): a cell is gated only
    // when the input reads f0 unambiguously; kGrowl (reads 2·f0) is recorded with its readings.
    RlwInputCache cache;
    int failing = 0;
    int cells = 0;
    int recorded = 0;
    for (const RlwAlgo algo : {RlwAlgo::Tube, RlwAlgo::Fold, RlwAlgo::Bite}) {
        for (std::size_t fixture = 0; fixture < SCT::kFixtures.size(); ++fixture) {
            const char* name = SCT::kFixtures[fixture].name;
            for (const bool fold : {false, true}) {
                for (const RlwNote& note : rlwNotes4()) {
                    const RlwInput& in = cache.get(fixture, note.hz, fold);
                    for (const float tone : kRlwTones3) {
                        BassCharacterStage s;
                        rlwConfigure(s, algo, 1.0f, tone, note.hz, false);
                        const std::vector<float> y = rlwRender(s, in.x);
                        const SCT::ShsCandidates out = rlwShs(y, note.hz);
                        const double cents = SCT::centsBetween(out.fHat, in.shs.fHat);
                        const bool gated = in.shs.readsF0;
                        const bool pass = !gated || (!out.ambiguous && std::abs(cents) <= kRlwCentsBar);
                        ++cells;
                        if (!gated)
                            ++recorded;
                        csv << rlwAlgoName(algo) << ',' << tone << ',' << name << ',' << rlwVariantName(fold) << ','
                            << note.name << ',' << note.hz << ',' << in.shs.fHat << ',' << in.shs.bestMinusSecondDb
                            << ',' << (in.shs.ambiguous ? 1 : 0) << ',' << out.fHat << ',' << out.bestMinusSecondDb
                            << ',' << (out.ambiguous ? 1 : 0) << ',' << cents << ',' << (gated ? 1 : 0) << ','
                            << (pass ? 1 : 0) << '\n';
                        if (!pass) {
                            ++failing;
                            FAIL_CHECK("S6.5 CONTINGENCY: SC-012(f) " << rlwAlgoName(algo) << " cell fails (Tube/Fold/"
                                       "Bite route) - Tone " << tone << ", " << name << ", " << rlwVariantName(fold)
                                       << ", " << note.name << ": f_in = " << in.shs.fHat << " Hz (gap "
                                       << in.shs.bestMinusSecondDb << " dB, ambiguous " << in.shs.ambiguous
                                       << "), f_out = " << out.fHat << " Hz (gap " << out.bestMinusSecondDb
                                       << " dB, ambiguous " << out.ambiguous << "), r_ref = " << cents << " cents");
                        }
                    }
                }
            }
        }
    }
    WARN("SC-012(f) Character: " << cells << " cells, " << recorded << " recorded (input does not read f0; C-3), "
                                  << failing << " failing of the gated cells");
    CHECK(recorded == 72);  // the kGrowl cells: 3 algorithms x 2 variants x 4 notes x 3 Tones
    CHECK(failing == 0);
}

// =============================================================================
// Plan T030: the r-law harvest (hidden; run by T024 at close)
// =============================================================================

TEST_CASE("BassCharacter_RLawHarvest", "[systems][profundum][shape-character][bass-character][.calibration]") {
    std::ostringstream table;
    table << std::setprecision(10);
    table << "algorithm,fixture,note,tone,a_lo,a_hi\n";
    bool stop = false;
    std::string why;

    RlwInputCache cache;
    // Per algorithm: per Tone the intersection [max A_lo, min A_hi] and per (Tone, fixture) the
    // A_lo / A_hi spread across notes (the tables have no note axis, C-4).
    std::string recorded;
    const auto harvest = [&](RlwAlgo algo, const std::vector<std::size_t>& fixtures, bool fold,
                             const std::vector<RlwNote>& notes, double num, bool spreadStops) {
        std::array<BassCharacterStage::FlipEdges, 5> edges{};
        for (std::size_t t = 0; t < kRlwTones5.size(); ++t) {
            const float tone = kRlwTones5[t];
            double maxLo = 0.0;
            double minHi = 1.0;
            for (const std::size_t fixture : fixtures) {
                double loMin = 1.0;
                double loMax = 0.0;
                double hiMin = 1.0;
                double hiMax = 0.0;
                for (const RlwNote& note : notes) {
                    const RlwInput& in = cache.get(fixture, note.hz, fold);
                    RlwAmountSweep sweep(algo, tone, note.hz, in.x);
                    const RlwBand band = rlwBand(sweep, note.hz, num, 0, false);
                    table << rlwAlgoName(algo) << ',' << SCT::kFixtures[fixture].name << ',' << note.name << ','
                          << tone << ',' << rlwOpt(band.lo) << ',' << rlwOpt(band.hi) << '\n';
                    if (!band.lo.has_value() || !band.hi.has_value()) {
                        stop = true;
                        why += std::string(rlwAlgoName(algo)) + " " + SCT::kFixtures[fixture].name + " " + note.name
                               + " Tone " + std::to_string(tone) + ": crossing not bracketed; ";
                        continue;
                    }
                    loMin = std::min(loMin, *band.lo);
                    loMax = std::max(loMax, *band.lo);
                    hiMin = std::min(hiMin, *band.hi);
                    hiMax = std::max(hiMax, *band.hi);
                    maxLo = std::max(maxLo, *band.lo);
                    minHi = std::min(minHi, *band.hi);
                }
                if (loMax - loMin > 0.01 || hiMax - hiMin > 0.01) {
                    const std::string msg = std::string(rlwAlgoName(algo)) + " " + SCT::kFixtures[fixture].name + " Tone "
                                            + std::to_string(tone) + ": spread across notes A_lo "
                                            + std::to_string(loMax - loMin) + ", A_hi " + std::to_string(hiMax - hiMin)
                                            + " (> 0.01); ";
                    // C-4 refinement (main loop, 2026-10-16): a stop for Rectify; recorded for Fracture, whose
                    // intersection over notes still lies inside every gated band.
                    if (spreadStops) {
                        stop = true;
                        why += msg;
                    } else {
                        recorded += msg;
                    }
                }
            }
            if (maxLo > minHi) {
                stop = true;
                why += std::string(rlwAlgoName(algo)) + " Tone " + std::to_string(tone) + ": empty intersection ["
                       + std::to_string(maxLo) + ", " + std::to_string(minHi) + "]; ";
            }
            edges[t] = {.lo = static_cast<float>(maxLo), .hi = static_cast<float>(minHi)};
        }
        return edges;
    };

    const std::array<RlwNote, 5> n5 = rlwNotes5();
    const std::array<RlwNote, 4> n4 = rlwNotes4();
    const auto rect = harvest(RlwAlgo::Rectify, {kRlwSine, kRlwTriangle}, false,
                              std::vector<RlwNote>(n5.begin(), n5.end()), 2.0, true);
    // C-31 (user ruling 2026-10-16): Fracture edges from Fold-100 sine and triangle; the saw is recorded.
    const auto frac = harvest(RlwAlgo::Fracture, {kRlwSine, kRlwTriangle}, true,
                              std::vector<RlwNote>(n4.begin(), n4.end()), 0.5, false);
    for (const RlwNote& note : n4) {
        for (const float tone : kRlwTones5) {
            const RlwInput& in = cache.get(kRlwSaw, note.hz, true);
            RlwAmountSweep sweep(RlwAlgo::Fracture, tone, note.hz, in.x);
            const RlwBand band = rlwBand(sweep, note.hz, 0.5, 0, false);
            table << "Fracture(recorded)," << SCT::kFixtures[kRlwSaw].name << ',' << note.name << ',' << tone << ','
                  << rlwOpt(band.lo) << ',' << rlwOpt(band.hi) << '\n';
        }
    }

    const auto line = [](const char* label, const std::array<BassCharacterStage::FlipEdges, 5>& e) {
        std::string s = std::string("HARVEST ") + label + " {";
        for (std::size_t i = 0; i < e.size(); ++i) {
            std::array<char, 64> buf{};
            std::snprintf(buf.data(), buf.size(), "{%.8f, %.8f}", static_cast<double>(e[i].lo),  // exact k/128 bisection values (close 2026-10-16)
                          static_cast<double>(e[i].hi));
            if (i > 0)
                s += ", ";
            s += buf.data();
        }
        return s + "}";
    };
    const std::string rectLine = line("kRectifyFlipEdges", rect);
    const std::string fracLine = line("kFractureFlipEdges", frac);
    std::cout << rectLine << '\n' << fracLine << '\n';

    std::ofstream out = rlwOpenEvidence("harvest-rlaw.txt");
    out << rectLine << '\n' << fracLine << '\n' << '\n' << table.str();
    if (!recorded.empty())
        out << "\nRECORDED (C-4 note spread, Fracture): " << recorded << '\n';
    if (stop)
        out << "\nSTOP AND SURFACE (R-6): " << why << '\n';
    out.close();

    INFO(why);
    if (stop)
        FAIL("STOP AND SURFACE (R-6): " << why);
    SUCCEED();
}
