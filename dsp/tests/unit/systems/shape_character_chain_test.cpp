// ==============================================================================
// Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// Chain gates (tasks.md T019; plan S9.1, C-2, C-3, C-15):
//   SC-012(e) BassCharacter_RLawUpstreamChainSmoke, BassCharacter_RLawUpstreamChain [long]
//   SC-016    ShapeCharacter_NoAllocation
//   SC-018    ShapeCharacter_PartitionInvariant
//   SC-019    ShapeCharacter_SampleRates
//   SC-020    ShapeCharacter_H1TransferRecord (FR-040, FR-041, C-15; recorded, not gated)
//   SC-022    ShapeCharacter_OutputBoundedShape (the Shape arm; the Character arm is T016's)
//   SC-025    ShapeCharacter_DropTracksHarmonicNumbersSmoke,
//             ShapeCharacter_DropTracksHarmonicNumbers [long]
//
// Fixture conventions: tasks.md rule 9 (1 s settle, host block 512, L == R copies unless a case
// says otherwise). Evidence: rule 10. Every measurement decision is recorded in
// specs/profundum-phase5-shape-character/evidence/run-log/T019.txt.
//
// ALLOCATION DETECTION: this TU includes <allocation_detector.h> ONLY. The operator overrides
// have one owner in dsp_systems_tests (selectable_oscillator_test.cpp); a second include is a
// duplicate-symbol link error.
//
// SC-025 measures |H(n)| during the Drop on a phase-domain BH7 frame: the frame spans 8 cycles
// of the core's instantaneous phase Φ (the staircase the core actually sounds, integrated), the
// BH7 window is a function of Φ, and each sample is weighted by dΦ, so the single-bin DFT at n
// cycles is the BH7 analysis of harmonic n of the instantaneous f0 (a time-domain frame would
// smear every harmonic across the 48-semitone chirp). On a steady render (Φ linear) it reduces
// to goertzelPhasor's BH7 frame of 8 periods.
// ==============================================================================

#include "bass_character_stage_probe.h"
#include "shape_character_test_helpers.h"
#include "shape_morph_probe.h"

#include <allocation_detector.h>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/closed_form_adaa2.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/keytracked_tilt.h>
#include <krate/dsp/processors/oversampled_residual_path.h>
#include <krate/dsp/processors/rectifier.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/shape_morph.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace SCT = Krate::DSP::ShapeCharacterTest;
namespace HFT = Krate::DSP::HarmonicFoldTest;
using SccAlgo = BassCharacterStage::Algorithm;
using SccCharProbe = Krate::DSP::detail::BassCharacterStageProbe;
using SccShapeProbe = Krate::DSP::detail::ShapeMorphProbe;
using SccStereo = std::pair<std::vector<float>, std::vector<float>>;

constexpr double kSccFs = 48000.0;
constexpr std::size_t kSccBlock = 512;                          // rule 9 host block
constexpr std::size_t kSccFrame = TestUtils::kLowFrequencyFftSize;
constexpr double kSccSettleSeconds = 1.0;                       // rule 9 settle
constexpr double kSccLongSeconds = 7.0;                         // 1 s settle + the 262144-sample frame
constexpr double kSccCentsBar = 1.0;                            // SC-012
constexpr double kSccCeiling = 6.0;                             // FR-069 / SC-019 / SC-022
constexpr double kSccH1AbsentRatio = 1.0e-6;                    // FR-040: output h1 more than 60 dB below the input's
constexpr double kSccResidualBarDbfs = -120.0;                  // SC-018

constexpr std::array<SccAlgo, 6> kSccAlgos{SccAlgo::Tube, SccAlgo::Fold, SccAlgo::Rectify,
                                           SccAlgo::Bite, SccAlgo::Crush, SccAlgo::Fracture};
constexpr std::array<float, 3> kSccShapeAnchors{0.0f, 0.5f, 1.0f};
constexpr std::array<float, 3> kSccTones{0.0f, 0.5f, 1.0f};

struct SccNote {
    const char* name;
    double hz;
};

struct SccFixture {
    const char* name;
    SpectralShapeRecipe::Controls controls;
};

const char* sccAlgoName(SccAlgo a) {
    switch (a) {
        case SccAlgo::Tube:
            return "Tube";
        case SccAlgo::Fold:
            return "Fold";
        case SccAlgo::Rectify:
            return "Rectify";
        case SccAlgo::Bite:
            return "Bite";
        case SccAlgo::Crush:
            return "Crush";
        case SccAlgo::Fracture:
            return "Fracture";
    }
    return "?";
}

std::string sccEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase5-shape-character/evidence/";
}

bool sccAllFinite(const std::vector<float>& y) {
    for (const float v : y) {
        if (!Krate::DSP::detail::isFinite(v))
            return false;
    }
    return true;
}

double sccPeakAbs(const std::vector<float>& y) {
    double m = 0.0;
    for (const float v : y)
        m = std::max(m, std::abs(static_cast<double>(v)));
    return m;
}

bool sccBitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

/// Peak |a − b| in dBFS (−300 when identical).
double sccResidualDbfs(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    double m = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        m = std::max(m, std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
    return 20.0 * std::log10(std::max(m, 1.0e-15));
}

// -----------------------------------------------------------------------------
// Stages: one cell = ShapeMorph at a position, or Character at (algorithm, Amount, Tone)
// -----------------------------------------------------------------------------

struct SccCell {
    bool isShape;
    float shape;
    SccAlgo algo;
    float amount;
    float tone;
};

SccCell sccShapeCell(float shape) {
    return SccCell{.isShape = true, .shape = shape, .algo = SccAlgo::Tube, .amount = 0.0f, .tone = 0.5f};
}

SccCell sccCharCell(SccAlgo a, float amount, float tone) {
    return SccCell{.isShape = false, .shape = 0.0f, .algo = a, .amount = amount, .tone = tone};
}

std::string sccCellName(const SccCell& c) {
    if (c.isShape)
        return "SHAPE " + std::to_string(c.shape);
    return std::string(sccAlgoName(c.algo)) + " A " + std::to_string(c.amount) + " T " + std::to_string(c.tone);
}

/// A ShapeMorph at `shape`, keyed to f0 (f0 <= 0 keeps the constructor default), prepared at fs:
/// prepare's reset() snaps the SHAPE smoother, so the render starts steady.
std::unique_ptr<ShapeMorph> sccMakeShape(float shape, float f0, double fs) {
    auto m = std::make_unique<ShapeMorph>();
    m->setShape(shape);
    if (f0 > 0.0f)
        m->setFundamentalHz(f0);
    m->prepare(fs, kSccBlock);
    return m;
}

/// A Character stage configured before prepare (prepare's reset() ends any crossfade and snaps the
/// smoothers), keyed to f0 (f0 <= 0 keeps the constructor default).
std::unique_ptr<BassCharacterStage> sccMakeChar(SccAlgo a, float amount, float tone, float f0, double fs) {
    auto s = std::make_unique<BassCharacterStage>();
    s->setAlgorithm(a);
    s->setAmount(amount);
    s->setTone(tone);
    if (f0 > 0.0f)
        s->setFundamentalHz(f0);
    s->prepare(fs, kSccBlock);
    return s;
}

struct SccRunSpec {
    std::size_t block = kSccBlock;
    const float* f0PerSample = nullptr;  ///< whole-render array (FR-052 pointer path)
    const float* perBlockF0 = nullptr;   ///< whole-render array; setFundamentalHz(at block start) per block
};

/// In-place stereo render in host blocks [k·block, (k+1)·block).
template <class Stage>
void sccRunStage(Stage& s, std::vector<float>& L, std::vector<float>& R, const SccRunSpec& spec) {
    REQUIRE(L.size() == R.size());
    REQUIRE(spec.block > 0);
    const std::size_t n = L.size();
    for (std::size_t pos = 0; pos < n; pos += spec.block) {
        const std::size_t len = std::min(spec.block, n - pos);
        if (spec.perBlockF0 != nullptr)
            s.setFundamentalHz(spec.perBlockF0[pos]);
        s.processBlock(L.data() + pos, R.data() + pos, len,
                       spec.f0PerSample != nullptr ? spec.f0PerSample + pos : nullptr);
    }
}

/// In-place stereo render in host blocks [k·block, (k+1)·block), each split at the event indices
/// `at` (strictly increasing); ev(i) runs before the sample at[i] is processed.
template <class Stage, class Event>
void sccRunEvents(Stage& s, std::vector<float>& L, std::vector<float>& R, std::size_t block, const float* f0PerSample,
                  const std::vector<std::size_t>& at, const Event& ev) {
    REQUIRE(L.size() == R.size());
    REQUIRE(block > 0);
    for (std::size_t i = 1; i < at.size(); ++i)
        REQUIRE(at[i] > at[i - 1]);
    const std::size_t n = L.size();
    std::size_t next = 0;
    std::size_t pos = 0;
    while (pos < n) {
        while (next < at.size() && at[next] == pos) {
            ev(next);
            ++next;
        }
        std::size_t end = std::min(n, (pos / block + 1) * block);
        if (next < at.size() && at[next] < end)
            end = at[next];
        s.processBlock(L.data() + pos, R.data() + pos, end - pos, f0PerSample != nullptr ? f0PerSample + pos : nullptr);
        pos = end;
    }
}

/// A fresh stage for `c` (keyed by setFundamentalHz(f0Setter) before prepare; f0Setter <= 0 keeps the
/// default) rendering copies of inL / inR.
SccStereo sccRenderCell(const SccCell& c, const std::vector<float>& inL, const std::vector<float>& inR, double fs,
                        float f0Setter, const SccRunSpec& spec) {
    SccStereo out{inL, inR};
    if (c.isShape) {
        auto m = sccMakeShape(c.shape, f0Setter, fs);
        sccRunStage(*m, out.first, out.second, spec);
    } else {
        auto s = sccMakeChar(c.algo, c.amount, c.tone, f0Setter, fs);
        sccRunStage(*s, out.first, out.second, spec);
    }
    return out;
}

/// Every Character algorithm at Amount 1 × Tone, then every Shape anchor.
std::vector<SccCell> sccAllCells(const std::vector<float>& tones) {
    std::vector<SccCell> cells;
    for (const SccAlgo a : kSccAlgos) {
        for (const float t : tones)
            cells.push_back(sccCharCell(a, 1.0f, t));
    }
    for (const float s : kSccShapeAnchors)
        cells.push_back(sccShapeCell(s));
    return cells;
}

// -----------------------------------------------------------------------------
// The core (rule 9 "fixture input is the core rendering ...")
// -----------------------------------------------------------------------------

/// A fresh ProfundumCore at fs: setControls(c), noteOn(f0), `total` samples in `block`-sized blocks,
/// passing the matching slice of f0PerSample when non-null.
SccStereo sccCoreRender(const SpectralShapeRecipe::Controls& c, float f0, double fs, std::size_t total,
                        std::size_t block, const float* f0PerSample) {
    REQUIRE(total > 0);
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs);
    core->setControls(c);
    core->noteOn(f0);
    SccStereo out{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
    for (std::size_t pos = 0; pos < total; pos += block) {
        const std::size_t n = std::min(block, total - pos);
        core->processBlock(out.first.data() + pos, out.second.data() + pos, n,
                           f0PerSample != nullptr ? f0PerSample + pos : nullptr);
    }
    return out;
}

// -----------------------------------------------------------------------------
// SC-012(e): core -> ShapeMorph SHAPE 1 -> HarmonicFold 100 -> Rectify
// -----------------------------------------------------------------------------

/// The Rectify input of the real upstream chain (each stage keyed to f0, host block 512).
SccStereo sccUpstreamInput(const SpectralShapeRecipe::Controls& c, double f0, double fs, double seconds) {
    const auto total = static_cast<std::size_t>(std::llround(seconds * fs));
    const auto f0f = static_cast<float>(f0);
    SccStereo x = sccCoreRender(c, f0f, fs, total, kSccBlock, nullptr);
    {
        auto m = sccMakeShape(1.0f, f0f, fs);  // BITE
        sccRunStage(*m, x.first, x.second, SccRunSpec{});
    }
    {
        HarmonicFold fold;
        fold.prepare(fs, kSccBlock);
        fold.setAmount(100.0f);
        fold.setFundamentalHz(f0f);
        for (std::size_t pos = 0; pos < total; pos += kSccBlock) {
            const std::size_t n = std::min(kSccBlock, total - pos);
            fold.processBlock(x.first.data() + pos, x.second.data() + pos, n);
        }
    }
    return x;
}

struct SccObserverMatch {
    bool ambiguous;
    bool match;
    double fHat;
    double centsToNearest;  ///< r·f0 against the nearest admissible candidate
};

/// SC-012 matching rule (C-2): unambiguous -> r·f0 within 1 cent of f̂; ambiguous -> within 1 cent of one
/// of the two best candidates of {f̂/2, f̂, 2·f̂}.
SccObserverMatch sccMatchObserver(const SCT::ShsCandidates& out, double fObs) {
    SccObserverMatch r{.ambiguous = out.ambiguous, .match = false, .fHat = out.fHat, .centsToNearest = 0.0};
    if (!out.ambiguous) {
        r.centsToNearest = SCT::centsBetween(fObs, out.fHat);
        r.match = std::abs(r.centsToNearest) <= kSccCentsBar;
        return r;
    }
    std::array<std::pair<double, double>, 3> cand{{{out.sHalfDb, 0.5 * out.fHat},
                                                    {out.sOneDb, out.fHat},
                                                    {out.sDoubleDb, 2.0 * out.fHat}}};
    std::sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const double c0 = SCT::centsBetween(fObs, cand[0].second);
    const double c1 = SCT::centsBetween(fObs, cand[1].second);
    r.centsToNearest = std::abs(c0) <= std::abs(c1) ? c0 : c1;
    r.match = std::abs(r.centsToNearest) <= kSccCentsBar;
    return r;
}

/// One (fixture, note) of SC-012(e) over amounts × tones; with `caps` the clause's per-(fixture, note)
/// ambiguity caps (≤ 20 % ambiguous, ≥ 8 unambiguous) apply to gated inputs. Rows go to csv when non-null.
void sccUpstreamChainCell(const SccFixture& fx, const SccNote& note, const std::vector<float>& amounts,
                          const std::vector<float>& tones, bool caps, bool requireGated, std::ofstream* csv) {
    const double fs = kSccFs;
    const double f0 = note.hz;
    const SccStereo in = sccUpstreamInput(fx.controls, f0, fs, kSccLongSeconds);
    REQUIRE(in.first.size() >= kSccFrame + static_cast<std::size_t>(fs));
    const float* inFrame = in.first.data() + (in.first.size() - kSccFrame);

    // The premise for supplying descriptor false: BITE's clipper leaves even harmonics at the Rectify input.
    const std::vector<double> w = HFT::renderedHarmonicPowers(inFrame, fs, f0, 2);
    REQUIRE(w.size() == 2);
    REQUIRE(w[0] > 0.0);
    const double w2Db = HFT::safeDb10(w[1] / w[0]);
    // FR-031 descriptor rule (user ruling 2026-10-16, Clarifications "Group 10 close rulings", C-30): the voice
    // supplies `true` when h2 re h1 is below -20 dB; cells inside the -24 .. -16 dB uncertainty band are recorded.
    constexpr double kDescriptorDb = -20.0;
    constexpr double kDescriptorBandDb = 4.0;
    const bool descriptor = w2Db < kDescriptorDb;
    const bool inBand = std::abs(w2Db - kDescriptorDb) <= kDescriptorBandDb;
    INFO(fx.name << " " << note.name << ": w2 re w1 at the Rectify input = " << w2Db << " dB -> descriptor "
                 << descriptor << (inBand ? " (inside the uncertainty band: recorded)" : ""));

    const SCT::ShsCandidates inShs = SCT::shsCandidates(inFrame, fs, f0);
    const bool gated = inShs.readsF0 && !inBand;  // C-3 input gating; C-30 band
    INFO("input f_in = " << inShs.fHat << " Hz (" << SCT::centsBetween(inShs.fHat, f0) << " cents), best-second "
                         << inShs.bestMinusSecondDb << " dB, gated " << gated);
    if (requireGated)
        CHECK(gated);

    std::size_t ambiguousCount = 0;
    std::size_t total = 0;
    for (const float tone : tones) {
        for (const float amount : amounts) {
            auto s = sccMakeChar(SccAlgo::Rectify, amount, tone, static_cast<float>(f0), fs);
            s->setInputHalfWaveSymmetric(descriptor);  // the descriptor the voice supplies for this chain (C-30)
            SccStereo y = in;
            sccRunStage(*s, y.first, y.second, SccRunSpec{});
            const double rObs = static_cast<double>(s->perceivedPitchRatio());
            const SCT::ShsCandidates outShs =
                SCT::shsCandidates(y.first.data() + (y.first.size() - kSccFrame), fs, f0);
            const SccObserverMatch m = sccMatchObserver(outShs, rObs * f0);
            ++total;
            if (m.ambiguous)
                ++ambiguousCount;
            if (csv != nullptr) {
                *csv << fx.name << "," << note.name << "," << amount << "," << tone << "," << (gated ? 1 : 0) << ","
                     << inShs.fHat << "," << outShs.fHat << "," << rObs << "," << (m.ambiguous ? 1 : 0) << ","
                     << outShs.bestMinusSecondDb << "," << m.centsToNearest << "," << (m.match ? 1 : 0) << "\n";
            }
            if (gated) {
                INFO("Rectify A " << amount << " T " << tone << ": r_obs " << rObs << ", f_out " << outShs.fHat
                                  << ", ambiguous " << m.ambiguous << " (best-second " << outShs.bestMinusSecondDb
                                  << " dB), cents to the admissible candidate " << m.centsToNearest);
                CHECK(m.match);
            }
        }
    }
    if (gated && caps) {
        const double frac = static_cast<double>(ambiguousCount) / static_cast<double>(total);
        INFO(fx.name << " " << note.name << ": ambiguous " << ambiguousCount << " / " << total << " (" << frac << ")");
        CHECK(frac <= 0.2);
        CHECK(total - ambiguousCount >= 8u);
    }
    if (!gated) {
        WARN("SC-012(e) recorded (input not gated, C-3): " << fx.name << " " << note.name << ", f_in "
                                                           << inShs.fHat << " Hz, best-second "
                                                           << inShs.bestMinusSecondDb << " dB, ambiguous cells "
                                                           << ambiguousCount << " / " << total);
    }
}

// -----------------------------------------------------------------------------
// SC-025: the Drop, the core's staircase and the phase-domain BH7 frame
// -----------------------------------------------------------------------------

constexpr std::size_t kSccDropPreRoll = 24576;  // 48 × 512 = 1536 × 16: block- and grid-aligned
constexpr std::size_t kSccDropTotal = 49152;    // pre-roll + 0.512 s (the Drop lands in 60 ms)
constexpr std::size_t kSccGrid = ProfundumCore::kPitchUpdateInterval;
constexpr double kSccHarmonicCycles = 8.0;      // (a): "the instant's local period × 8"
constexpr double kSccLatticeCycles = 16.0;      // (c): f0/2 lattice and I need 8-bin separation of ν = k + 1/2
constexpr int kSccMaxHarmonic = 16;
constexpr double kSccHBarDb = 1.5;
constexpr double kSccMeasureFloor = 1.0e-6;     // −60 dB re the frame's strongest harmonic (OQ-S floor convention)

struct SccDrop {
    std::vector<double> d;      ///< semitones (DropEnvelope), held at its first value over the pre-roll
    std::vector<float> f;       ///< f[n] = E2·2^{d[n]/12}: the core's f0PerSample
    std::vector<float> stair;   ///< f held over each 16-sample grid step from note on: what the core sounds
    std::vector<double> phi;    ///< Φ[k + 1] = Φ[k] + stair[k]/fs, cycles (size n + 1)
};

std::vector<double> sccPhaseOf(const std::vector<float>& f, double fs) {
    std::vector<double> phi(f.size() + 1, 0.0);
    for (std::size_t k = 0; k < f.size(); ++k)
        phi[k + 1] = phi[k] + static_cast<double>(f[k]) / fs;
    return phi;
}

SccDrop sccMakeDrop(double fs, std::size_t preRoll, std::size_t total) {
    REQUIRE(preRoll % kSccGrid == 0);
    REQUIRE(total > preRoll);
    DropEnvelope env;
    env.prepare(fs);
    env.setAmount(48.0f);
    env.setTime(60.0f);
    env.setShape(0.0f);
    env.setRebound(0.0f);
    env.trigger(true);
    SccDrop drop;
    drop.d.assign(total, 0.0);
    for (std::size_t n = preRoll; n < total; ++n)
        drop.d[n] = static_cast<double>(env.process());
    for (std::size_t n = 0; n < preRoll; ++n)
        drop.d[n] = drop.d[preRoll];
    drop.f.assign(total, 0.0f);
    drop.stair.assign(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n)
        drop.f[n] = static_cast<float>(SCT::kE2Hz * std::exp2(drop.d[n] / 12.0));
    for (std::size_t n = 0; n < total; ++n)
        drop.stair[n] = drop.f[n - n % kSccGrid];
    drop.phi = sccPhaseOf(drop.stair, fs);
    return drop;
}

/// A BH7 frame over `cycles` cycles of Φ starting at sample `begin`: w(u), u = (Φ − Φ[begin])/cycles,
/// times dΦ per sample (so Σ approximates the integral over phase).
struct SccPhaseFrame {
    std::size_t begin = 0;
    std::vector<double> wd;
    std::vector<double> theta;
    double sumWd = 0.0;
    double fMin = 0.0;
    double fMax = 0.0;
};

SccPhaseFrame sccPhaseFrame(const std::vector<double>& phi, std::size_t begin, double cycles, double fs) {
    REQUIRE(phi.size() >= 2);
    REQUIRE(begin + 1 < phi.size());
    std::size_t end = begin;
    while (end + 1 < phi.size() && phi[end] - phi[begin] < cycles)
        ++end;
    REQUIRE(phi[end] - phi[begin] >= cycles);
    SccPhaseFrame fr;
    fr.begin = begin;
    const std::size_t len = end - begin;
    fr.wd.assign(len, 0.0);
    fr.theta.assign(len, 0.0);
    fr.fMin = 1.0e300;
    fr.fMax = 0.0;
    for (std::size_t i = 0; i < len; ++i) {
        const std::size_t n = begin + i;
        const double th = phi[n] - phi[begin];
        const double u = th / cycles;
        double w = 0.0;
        for (std::size_t j = 0; j < HFT::kBh7Coefficients.size(); ++j) {
            const double sign = (j % 2 == 0) ? 1.0 : -1.0;
            w += sign * HFT::kBh7Coefficients[j] * std::cos(HFT::kTwoPi * static_cast<double>(j) * u);
        }
        const double dphi = phi[n + 1] - phi[n];
        fr.theta[i] = th;
        fr.wd[i] = w * dphi;
        fr.sumWd += fr.wd[i];
        fr.fMin = std::min(fr.fMin, dphi * fs);
        fr.fMax = std::max(fr.fMax, dphi * fs);
    }
    REQUIRE(fr.sumWd > 0.0);
    return fr;
}

/// c(ν) = Σ wd·x·e^{−i2πνθ}.
std::complex<double> sccProject(const float* x, const SccPhaseFrame& fr, double nu) {
    double re = 0.0;
    double im = 0.0;
    for (std::size_t i = 0; i < fr.wd.size(); ++i) {
        const double v = fr.wd[i] * static_cast<double>(x[fr.begin + i]);
        const double ph = HFT::kTwoPi * nu * fr.theta[i];
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return {re, im};
}

/// |c(ν)|² for ν = offset + k, k = 0 .. count − 1 (a unit rotor per sample; no trig in the k loop).
std::vector<double> sccLatticeNorms(const float* x, const SccPhaseFrame& fr, double offset, std::size_t count) {
    const std::size_t len = fr.wd.size();
    std::vector<std::complex<double>> acc(len);
    std::vector<std::complex<double>> step(len);
    for (std::size_t i = 0; i < len; ++i) {
        const double th = fr.theta[i];
        const double v = fr.wd[i] * static_cast<double>(x[fr.begin + i]);
        step[i] = {std::cos(HFT::kTwoPi * th), -std::sin(HFT::kTwoPi * th)};
        acc[i] = {v * std::cos(HFT::kTwoPi * offset * th), -v * std::sin(HFT::kTwoPi * offset * th)};
    }
    std::vector<double> out(count, 0.0);
    for (std::size_t k = 0; k < count; ++k) {
        std::complex<double> sum{0.0, 0.0};
        for (std::size_t i = 0; i < len; ++i) {
            sum += acc[i];
            acc[i] *= step[i];
        }
        out[k] = std::norm(sum);
    }
    return out;
}

/// The frame's mean power Σ wd·x² / Σ wd.
double sccFramePower(const float* x, const SccPhaseFrame& fr) {
    double s = 0.0;
    for (std::size_t i = 0; i < fr.wd.size(); ++i) {
        const double v = static_cast<double>(x[fr.begin + i]);
        s += fr.wd[i] * v * v;
    }
    return s / fr.sumWd;
}

/// Inharmonic fraction on the phase frame, dB: power not on the harmonics ν = 0 .. ⌊fs / (2·f_min)⌋ of the
/// instantaneous f0, re the frame's power (each harmonic's power 2|c|²/S², DC |c|²/S²).
double sccInharmonicDb(const float* x, const SccPhaseFrame& fr, double fs) {
    const auto nh = static_cast<std::size_t>(std::floor(0.5 * fs / fr.fMin));
    const std::vector<double> c = sccLatticeNorms(x, fr, 0.0, nh + 1);
    const double s2 = fr.sumWd * fr.sumWd;
    double harm = c[0] / s2;
    for (std::size_t k = 1; k < c.size(); ++k)
        harm += 2.0 * c[k] / s2;
    const double tot = sccFramePower(x, fr);
    REQUIRE(tot > 0.0);
    return HFT::safeDb10(std::max(tot - harm, 0.0) / tot);
}

/// Power on the odd-f0/2 lattice ν = k + 1/2 of the instantaneous f0, re the frame's power, dB.
double sccHalfLatticeDb(const float* x, const SccPhaseFrame& fr, double fs) {
    const auto nh = static_cast<std::size_t>(std::floor(0.5 * fs / fr.fMin));
    const std::vector<double> c = sccLatticeNorms(x, fr, 0.5, nh + 1);
    const double s2 = fr.sumWd * fr.sumWd;
    double half = 0.0;
    for (const double v : c)
        half += 2.0 * v / s2;
    const double tot = sccFramePower(x, fr);
    REQUIRE(tot > 0.0);
    return HFT::safeDb10(half / tot);
}

struct SccHCompare {
    double maxDevDb = 0.0;
    int worstN = 0;
    std::size_t compared = 0;
    std::size_t skipped = 0;
    std::string detail;
};

/// SC-025(a): |H(n)| = |c_out(n)|² / |c_in(n)|² on the drop frame and on the steady frame, n ≤ 16 with
/// n·fc < 0.45·fs. A harmonic whose input is below −60 dB re the frame's strongest input harmonic (in either
/// render) is not compared; output levels are floored at −60 dB re the strongest output harmonic (OQ-S
/// floor convention) before the ratio.
SccHCompare sccCompareH(const float* inD, const float* outD, const SccPhaseFrame& frD, const float* inS,
                        const float* outS, const SccPhaseFrame& frS, double fc, double fs) {
    std::array<double, kSccMaxHarmonic> pid{};
    std::array<double, kSccMaxHarmonic> pod{};
    std::array<double, kSccMaxHarmonic> pis{};
    std::array<double, kSccMaxHarmonic> pos{};
    int nMax = 0;
    for (int n = 1; n <= kSccMaxHarmonic; ++n) {
        if (static_cast<double>(n) * fc >= 0.45 * fs)
            break;
        const auto i = static_cast<std::size_t>(n - 1);
        const auto nu = static_cast<double>(n);
        pid[i] = std::norm(sccProject(inD, frD, nu));
        pod[i] = std::norm(sccProject(outD, frD, nu));
        pis[i] = std::norm(sccProject(inS, frS, nu));
        pos[i] = std::norm(sccProject(outS, frS, nu));
        nMax = n;
    }
    SccHCompare r;
    if (nMax == 0)
        return r;
    const auto count = static_cast<std::ptrdiff_t>(nMax);
    const double inMaxD = *std::max_element(pid.begin(), pid.begin() + count);
    const double inMaxS = *std::max_element(pis.begin(), pis.begin() + count);
    const double outMaxD = *std::max_element(pod.begin(), pod.begin() + count);
    const double outMaxS = *std::max_element(pos.begin(), pos.begin() + count);
    for (int n = 1; n <= nMax; ++n) {
        const auto i = static_cast<std::size_t>(n - 1);
        if (pid[i] < kSccMeasureFloor * inMaxD || pis[i] < kSccMeasureFloor * inMaxS || pid[i] <= 0.0
            || pis[i] <= 0.0) {
            ++r.skipped;
            continue;
        }
        const double hd = HFT::safeDb10(std::max(pod[i], kSccMeasureFloor * outMaxD) / pid[i]);
        const double hs = HFT::safeDb10(std::max(pos[i], kSccMeasureFloor * outMaxS) / pis[i]);
        const double dev = std::abs(hd - hs);
        ++r.compared;
        r.detail += "h" + std::to_string(n) + " " + std::to_string(hd) + "/" + std::to_string(hs) + " ";
        if (dev > r.maxDevDb) {
            r.maxDevDb = dev;
            r.worstN = n;
        }
    }
    return r;
}

struct SccInstant {
    double thresholdSt;
    std::size_t start;  ///< first sample after the pre-roll with d <= threshold (the frame start)
    double fc8;         ///< staircase f at the phase midpoint of the 8-cycle frame
    double fc16;        ///< staircase f at the phase midpoint of the 16-cycle frame
};

double sccMidF(const SccDrop& drop, std::size_t start, double cycles) {
    std::size_t m = start;
    while (m + 1 < drop.phi.size() && drop.phi[m] - drop.phi[start] < 0.5 * cycles)
        ++m;
    REQUIRE(m < drop.stair.size());
    return static_cast<double>(drop.stair[m]);
}

std::vector<SccInstant> sccInstants(const SccDrop& drop, const std::vector<double>& thresholds) {
    std::vector<SccInstant> out;
    for (const double th : thresholds) {
        std::size_t n = kSccDropPreRoll;
        while (n < drop.d.size() && drop.d[n] > th)
            ++n;
        REQUIRE(n < drop.d.size());
        out.push_back(SccInstant{.thresholdSt = th,
                                 .start = n,
                                 .fc8 = sccMidF(drop, n, kSccHarmonicCycles),
                                 .fc16 = sccMidF(drop, n, kSccLatticeCycles)});
    }
    return out;
}

// ---- SC-025(a) keytracked members (round-2 ruling 2026-10-16) -------------------------------

constexpr double kSccKeyedRelBar = 1.0e-3;  // 0.1 % (SC-025(a))

using SccKeyedList = std::vector<std::pair<std::string, double>>;

/// ShapeMorph's keyed members of the running regimes (only running regimes are keyed).
SccKeyedList sccKeyedShape(const ShapeMorph& m) {
    const SccShapeProbe::Keyed k = SccShapeProbe::keyed(m);
    SccKeyedList v;
    if (k.round)
        v.emplace_back("ROUND cutoff", static_cast<double>(k.roundCutoffHz));
    if (k.hollow) {
        v.emplace_back("HOLLOW notch centre", static_cast<double>(k.hollowNotchHz));
        v.emplace_back("HOLLOW peak centre", static_cast<double>(k.hollowPeakHz));
    }
    if (k.bite) {
        v.emplace_back("BITE pivot", static_cast<double>(k.bitePivotHz));
        v.emplace_back("BITE inverse pole", static_cast<double>(k.biteInverseA));
    }
    return v;
}

/// The running algorithm's keyed members: its Tone pivot(s), Crush's m_eff and hold-phasor increment,
/// Fracture's ÷2-phasor increment (its period).
SccKeyedList sccKeyedChar(const BassCharacterStage& st, SccAlgo a) {
    SccKeyedList v;
    v.emplace_back("post-tilt pivot", static_cast<double>(SccCharProbe::postTilt(st, a, 0).pivotHz()));
    if (SccCharProbe::hasPreTilt(a))
        v.emplace_back("pre-tilt pivot", static_cast<double>(SccCharProbe::preTilt(st, a, 0).pivotHz()));
    if (a == SccAlgo::Crush) {
        v.emplace_back("Crush m_eff", SccCharProbe::crushHoldMultiple(st, 0));
        v.emplace_back("Crush phasor increment", SccCharProbe::crushPhaseIncrement(st, 0));
    }
    if (a == SccAlgo::Fracture)
        v.emplace_back("Fracture phasor increment", SccCharProbe::fracturePhaseIncrement(st, 0));
    return v;
}

/// SC-025(a): the stage under test rendered through the Drop input up to and including `sample` (host
/// block `block`, the f0PerSample feed), against a steady stage keyed by setFundamentalHz at the feed's value
/// at that sample and run for two host blocks of silence (the keyed members do not depend on the input).
/// Every member must agree within 0.1 % (relative). Returns the largest relative difference.
double sccCheckKeyed(const SccCell& c, const SccStereo& in, const SccDrop& drop, std::size_t sample, std::size_t block,
                     double fs, const std::string& where) {
    REQUIRE(sample < in.first.size());
    const auto cut = static_cast<std::ptrdiff_t>(sample + 1);
    std::vector<float> l(in.first.begin(), in.first.begin() + cut);
    std::vector<float> r(in.second.begin(), in.second.begin() + cut);
    std::vector<float> zl(2 * kSccBlock, 0.0f);
    std::vector<float> zr(2 * kSccBlock, 0.0f);
    const float f = drop.stair[sample];
    SccKeyedList d;
    SccKeyedList st;
    if (c.isShape) {
        auto m = sccMakeShape(c.shape, drop.stair[0], fs);
        sccRunStage(*m, l, r, SccRunSpec{.block = block, .f0PerSample = drop.stair.data()});
        d = sccKeyedShape(*m);
        auto ms = sccMakeShape(c.shape, f, fs);
        sccRunStage(*ms, zl, zr, SccRunSpec{});
        st = sccKeyedShape(*ms);
    } else {
        auto m = sccMakeChar(c.algo, c.amount, c.tone, drop.stair[0], fs);
        sccRunStage(*m, l, r, SccRunSpec{.block = block, .f0PerSample = drop.stair.data()});
        d = sccKeyedChar(*m, c.algo);
        auto ms = sccMakeChar(c.algo, c.amount, c.tone, f, fs);
        sccRunStage(*ms, zl, zr, SccRunSpec{});
        st = sccKeyedChar(*ms, c.algo);
    }
    REQUIRE(!d.empty());
    REQUIRE(d.size() == st.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < d.size(); ++i) {
        REQUIRE(d[i].first == st[i].first);
        const double rel = std::abs(d[i].second - st[i].second) / std::max(std::abs(st[i].second), 1.0e-300);
        INFO(where << ", sample " << sample << " (f " << f << " Hz): " << d[i].first << " Drop " << d[i].second
                   << " vs steady " << st[i].second << ", relative " << rel << " (bar 0.001)");
        CHECK(rel <= kSccKeyedRelBar);
        worst = std::max(worst, rel);
    }
    return worst;
}

// ---- SC-025(c) Crush follows the feed (round-3 ruling 2026-10-16) ----------------------------

/// Renders Crush cell `c` through the Drop input one sample per processBlock call (the f0 feed is
/// value-driven, so the keying does not depend on the host partition, SC-018) and, at every sample of
/// each frame [begin, end), compares the probe's hold-phasor increment with f0[k] / (F·fs) and its hold
/// multiple with max(1, min(m(Amount), F·fs / (4·f0[k]))). Returns the worst relative difference per frame.
std::vector<double> sccCrushFeedWorst(const SccCell& c, const SccStereo& in, const SccDrop& drop, double fs,
                                      const std::vector<std::pair<std::size_t, std::size_t>>& frames) {
    auto st = sccMakeChar(c.algo, c.amount, c.tone, drop.stair[0], fs);
    const double rate = fs * static_cast<double>(st->oversamplingFactor());
    const double mA = static_cast<double>(BassCharacterStage::kCrushMultipleLo)
                      * std::pow(static_cast<double>(BassCharacterStage::kCrushMultipleHi
                                                     / BassCharacterStage::kCrushMultipleLo),
                                 static_cast<double>(c.amount));
    std::size_t end = 0;
    for (const auto& fr : frames)
        end = std::max(end, fr.second);
    REQUIRE(end <= in.first.size());
    std::vector<float> l(in.first.begin(), in.first.begin() + static_cast<std::ptrdiff_t>(end));
    std::vector<float> r(in.second.begin(), in.second.begin() + static_cast<std::ptrdiff_t>(end));
    std::vector<double> worst(frames.size(), 0.0);
    for (std::size_t k = 0; k < end; ++k) {
        st->processBlock(l.data() + k, r.data() + k, 1, drop.stair.data() + k);
        const auto f = static_cast<double>(drop.stair[k]);
        const double incExp = f / rate;
        const double mExp = std::max(1.0, std::min(mA, rate / (4.0 * f)));
        const double incRel = std::abs(SccCharProbe::crushPhaseIncrement(*st, 0) - incExp) / incExp;
        const double mRel = std::abs(SccCharProbe::crushHoldMultiple(*st, 0) - mExp) / mExp;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (k >= frames[i].first && k < frames[i].second)
                worst[i] = std::max(worst[i], std::max(incRel, mRel));
        }
    }
    return worst;
}

// ---- SC-025(d) positive-control placement (round-3 ruling 2026-10-16) -----------------------

/// Sorted ClickDetector detection indices (documented defaults, sampleRate = fs).
std::vector<std::size_t> sccDetections(const std::vector<float>& y, double fs) {
    TestUtils::ClickDetectorConfig cfg{};
    cfg.sampleRate = static_cast<float>(fs);
    TestUtils::ClickDetector det(cfg);
    det.prepare();
    std::vector<std::size_t> idx;
    for (const TestUtils::ClickDetection& d : det.detect(y.data(), y.size()))
        idx.push_back(d.sampleIndex);
    std::sort(idx.begin(), idx.end());
    return idx;
}

std::size_t sccNearest(const std::vector<std::size_t>& sorted, std::size_t k) {
    std::size_t best = static_cast<std::size_t>(-1);
    const auto it = std::lower_bound(sorted.begin(), sorted.end(), k);
    if (it != sorted.end())
        best = std::min(best, *it - k);
    if (it != sorted.begin())
        best = std::min(best, k - *(it - 1));
    return best;
}

/// One candidate control event: the discontinuity lands at `step` = event + delay; ratio = the step's size
/// proxy over the larger reference's max |dy| within ±N (the amplitude guard's comparison); valid when the
/// step is at least 2·N from every reference detection (the Rectifier control's placement rule).
struct SccCtlPick {
    std::size_t at = 0;
    std::size_t step = 0;
    bool valid = false;
    double ratio = 0.0;
    double jump = 0.0;
    double local = 0.0;
    std::size_t nearestRef = 0;
};

template <class Jump>
SccCtlPick sccScoreControl(std::size_t at, std::size_t delay, const std::vector<std::size_t>& refDet,
                           const std::vector<float>& r0, const std::vector<float>& r1, std::size_t N, const Jump& jump) {
    SccCtlPick c;
    c.at = at;
    c.step = at + delay;
    c.nearestRef = sccNearest(refDet, c.step);
    c.valid = c.step + 2 * N < r0.size() && c.nearestRef >= 2 * N;
    c.jump = jump(c.step);
    c.local = std::max(SCT::maxDeltaAround(r0.data(), r0.size(), c.step, N),
                       SCT::maxDeltaAround(r1.data(), r1.size(), c.step, N));
    c.ratio = c.jump / std::max(c.local, 1.0e-12);
    return c;
}

/// The valid candidate (every 8 samples over [lo, hi)) with the largest ratio.
template <class Jump>
SccCtlPick sccPickControl(std::size_t lo, std::size_t hi, std::size_t delay, const std::vector<std::size_t>& refDet,
                          const std::vector<float>& r0, const std::vector<float>& r1, std::size_t N, const Jump& jump) {
    SccCtlPick best;
    for (std::size_t e = lo; e < hi; e += 8) {
        const SccCtlPick c = sccScoreControl(e, delay, refDet, r0, r1, N, jump);
        if (c.valid && (!best.valid || c.ratio > best.ratio))
            best = c;
    }
    return best;
}

/// Samples from a hard-regime ShapeMorph's setShape(1) (at relative sample 0, SHAPE smoothed from 0) to the
/// first sample processed with HOLLOW as the only regime.
std::size_t sccHardRegimeDelay(double fs, float f0) {
    auto m = sccMakeShape(0.0f, f0, fs);
    SccShapeProbe::setSeamHardRegime(*m, true);
    m->setShape(1.0f);
    float l = 0.0f;
    float r = 0.0f;
    for (std::size_t j = 0; j < static_cast<std::size_t>(fs); ++j) {
        m->processBlock(&l, &r, 1);
        if (SccShapeProbe::weights(*m)[1] > 0.0f)
            return j;
    }
    FAIL_CHECK("hard-regime delay: HOLLOW never entered");
    return 0;
}

std::string sccPickText(const SccCtlPick& c) {
    return "event " + std::to_string(c.at) + ", step at " + std::to_string(c.step) + ", |jump| " +
           std::to_string(c.jump) + ", local ref max|dy| " + std::to_string(c.local) + ", ratio " +
           std::to_string(c.ratio) + ", nearest reference detection " + std::to_string(c.nearestRef) +
           (c.valid ? " (valid)" : " (not clear of reference detections by 2N)");
}

/// Steady-render length: the 1 s settle + 16 cycles at f + margin, rounded up to the host block.
std::size_t sccSteadyTotal(double f, double fs) {
    const auto settle = static_cast<std::size_t>(std::llround(kSccSettleSeconds * fs));
    const auto need = settle + static_cast<std::size_t>(std::ceil(kSccLatticeCycles * fs / f)) + 2 * kSccBlock;
    return (need + kSccBlock - 1) / kSccBlock * kSccBlock;
}

/// The steady phase frame of `cycles` cycles at constant f, starting after the 1 s settle.
SccPhaseFrame sccSteadyFrame(std::size_t total, double f, double cycles, double fs) {
    const std::vector<float> ff(total, static_cast<float>(f));
    const std::vector<double> phi = sccPhaseOf(ff, fs);
    return sccPhaseFrame(phi, static_cast<std::size_t>(std::llround(kSccSettleSeconds * fs)), cycles, fs);
}

/// SC-025(e): a null pointer with f0 set by setFundamentalHz(E2) before prepare is bit-identical to a stage
/// left at its default f0 that receives a constant E2 f0PerSample array.
void sccNullPointerIdentity(const SccCell& c, const SccStereo& in, std::size_t block) {
    const auto f0 = static_cast<float>(SCT::kE2Hz);
    const std::vector<float> f0s(in.first.size(), f0);
    const SccStereo a = sccRenderCell(c, in.first, in.second, kSccFs, f0, SccRunSpec{.block = block});
    const SccStereo b =
        sccRenderCell(c, in.first, in.second, kSccFs, -1.0f, SccRunSpec{.block = block, .f0PerSample = f0s.data()});
    INFO("SC-025(e) " << sccCellName(c) << ", block " << block << ": L residual " << sccResidualDbfs(a.first, b.first)
                      << " dBFS, R residual " << sccResidualDbfs(a.second, b.second) << " dBFS");
    CHECK(sccBitEqual(a.first, b.first));
    CHECK(sccBitEqual(a.second, b.second));
}

}  // namespace

// =============================================================================
// SC-012(e): the real upstream chain
// =============================================================================

TEST_CASE("BassCharacter_RLawUpstreamChainSmoke", "[systems][profundum][shape-character][bass-character]") {
    const SccFixture saw{.name = "kSawAnchor", .controls = SpectralShapeRecipe::kSawAnchor};
    const SccNote e1{.name = "E1", .hz = SCT::kE1Hz};
    sccUpstreamChainCell(saw, e1, {0.0f, 1.0f}, {0.5f}, false, true, nullptr);
}

TEST_CASE("BassCharacter_RLawUpstreamChain", "[systems][profundum][shape-character][bass-character][long]") {
    const std::string dir = sccEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc012e-upstream-chain.csv");
    REQUIRE(csv.good());
    csv << "fixture,note,amount,tone,input_gated,f_in_hz,f_out_hz,r_obs,out_ambiguous,out_best_minus_second_db,"
           "cents_to_candidate,match\n";

    const std::array<SccFixture, 3> fixtures{{
        {.name = "kSawAnchor", .controls = SpectralShapeRecipe::kSawAnchor},
        {.name = "kGrowl", .controls = SpectralShapeRecipe::kGrowl},
        {.name = "kTriangleAnchor", .controls = SpectralShapeRecipe::kTriangleAnchor},
    }};
    const std::array<SccNote, 4> notes{{{"E0", SCT::kE0Hz}, {"E1", SCT::kE1Hz}, {"E2", SCT::kE2Hz}, {"C3", SCT::kC3Hz}}};
    std::vector<float> amounts;
    for (int i = 0; i <= 10; ++i)
        amounts.push_back(static_cast<float>(i) / 10.0f);
    const std::vector<float> tones(kSccTones.begin(), kSccTones.end());
    for (const SccFixture& fx : fixtures) {
        for (const SccNote& note : notes)
            sccUpstreamChainCell(fx, note, amounts, tones, true, false, &csv);
    }
}

// =============================================================================
// SC-016: RT safety
// =============================================================================

TEST_CASE("ShapeCharacter_NoAllocation", "[systems][profundum][shape-character]") {
    constexpr std::size_t kBlock = 512;
    const double fs = kSccFs;
    const auto f0 = static_cast<float>(SCT::kE1Hz);
    const auto f0Alt = static_cast<float>(SCT::kE2Hz);
    const std::vector<float> src = SCT::fixture(SpectralShapeRecipe::kSawAnchor, SCT::kE1Hz, fs, 0.25);
    REQUIRE(src.size() >= kBlock);
    std::vector<float> L(kBlock, 0.0f);
    std::vector<float> R(kBlock, 0.0f);
    const std::vector<float> f0s(kBlock, f0);
    auto c = std::make_unique<BassCharacterStage>();
    c->setFundamentalHz(f0);
    c->prepare(fs, kBlock);
    auto m = std::make_unique<ShapeMorph>();
    m->setFundamentalHz(f0);
    m->prepare(fs, kBlock);

    std::size_t srcPos = 0;
    float sink = 0.0f;
    std::size_t blocks = 0;
    const auto block = [&](bool withPointer) {
        if (srcPos + kBlock > src.size())
            srcPos = 0;
        std::copy(src.begin() + static_cast<std::ptrdiff_t>(srcPos),
                  src.begin() + static_cast<std::ptrdiff_t>(srcPos + kBlock), L.begin());
        std::copy(L.begin(), L.end(), R.begin());
        srcPos += kBlock;
        const float* p = withPointer ? f0s.data() : nullptr;
        m->processBlock(L.data(), R.data(), kBlock, p);
        c->processBlock(L.data(), R.data(), kBlock, p);
        sink += L[kBlock - 1] + R[kBlock - 1];
        ++blocks;
    };

    std::size_t observed = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        for (const SccAlgo a : kSccAlgos) {
            for (const float shape : kSccShapeAnchors) {
                m->setShape(shape);
                m->setFundamentalHz(f0Alt);
                m->setFundamentalHz(f0);
                c->setAlgorithm(a);  // an algorithm switch whenever a differs from the current algorithm
                c->setAmount(1.0f);
                c->setTone(0.5f);
                c->setFundamentalHz(f0);
                c->setInputHalfWaveSymmetric(true);
                c->setInputHalfWaveSymmetric(false);
                block(true);
                block(false);  // through the 10 ms switch crossfade, with and without the pointer
                c->setChannelOffsets(0.15f, -0.15f);  // the first unlink copies channel state (no heap)
                block(true);
                c->setChannelOffsets(0.0f, 0.0f);
                c->setTone(0.25f);
                block(false);
                m->setEnabled(false);
                block(false);
                block(true);
                m->setEnabled(true);  // a ShapeMorph engage
                block(true);
                c->setAmount(0.0f);  // into the Amount-0 bypass (5 ms)
                block(false);
                block(true);
                c->setAmount(1.0f);  // leaving the bypass
                block(true);
                c->clearPreCeilingPeak();
                (*c).reset();
                (*m).reset();
                block(false);
                block(true);
            }
        }
        // AllocationScope latches its count in its destructor (allocation_detector.h:117-119): read the live count.
        observed = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    INFO("allocations observed: " << observed << " over " << blocks << " blocks");
    CHECK(Krate::DSP::detail::isFinite(sink));
    REQUIRE(observed == 0u);

    // Positive control: the detector sees an allocation inside an identical scope.
    std::size_t control = 0;
    int probeObserved = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        int* probe = new int(42);
        volatile int* probeSink = probe;  // defeat new/delete elision
        probeObserved = *probeSink;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete probe;
        control = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(probeObserved == 42);
    CHECK(control >= 1u);
}

// =============================================================================
// SC-018: determinism and partition invariance
// =============================================================================

TEST_CASE("ShapeCharacter_PartitionInvariant", "[systems][profundum][shape-character]") {
    const double fs = kSccFs;
    constexpr std::size_t kTotal = 24000;  // 0.5 s
    const std::array<std::size_t, 5> blocks{1, 17, 32, 64, 512};
    const auto e1 = static_cast<float>(SCT::kE1Hz);
    const auto e2 = static_cast<float>(SCT::kE2Hz);
    const std::vector<float> src = SCT::fixture(SpectralShapeRecipe::kSawAnchor, SCT::kE1Hz, fs, 0.5);
    REQUIRE(src.size() >= kTotal);
    const std::vector<float> in(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(kTotal));

    // A Drop-shaped f0 array: 48 st over 60 ms onto E2, triggered at sample 4096.
    std::vector<float> dropF(kTotal, 0.0f);
    {
        DropEnvelope env;
        env.prepare(fs);
        env.setAmount(48.0f);
        env.setTime(60.0f);
        env.setShape(0.0f);
        env.setRebound(0.0f);
        env.trigger(true);
        std::vector<double> d(kTotal, 0.0);
        for (std::size_t n = 4096; n < kTotal; ++n)
            d[n] = static_cast<double>(env.process());
        for (std::size_t n = 0; n < 4096; ++n)
            d[n] = d[4096];
        for (std::size_t n = 0; n < kTotal; ++n)
            dropF[n] = static_cast<float>(SCT::kE2Hz * std::exp2(d[n] / 12.0));
    }

    // Character: setters at fixed absolute sample indices.
    const std::vector<std::size_t> charAt{3001, 7013, 9000, 12345, 15000, 18000, 20000};
    for (std::size_t ai = 0; ai < kSccAlgos.size(); ++ai) {
        const SccAlgo a = kSccAlgos[ai];
        const SccAlgo next = kSccAlgos[(ai + 1) % kSccAlgos.size()];
        const auto initial = [&](BassCharacterStage& s) {
            s.setAlgorithm(a);
            s.setAmount(1.0f);
            s.setTone(0.5f);
            s.setChannelOffsets(0.0f, 0.0f);
            s.setFundamentalHz(e1);
        };
        const auto events = [&](BassCharacterStage& s) {
            return [&s, next, e2](std::size_t i) {
                switch (i) {
                    case 0: s.setAmount(0.6f); break;
                    case 1: s.setTone(0.2f); break;
                    case 2: s.setAlgorithm(next); break;
                    case 3: s.setChannelOffsets(0.1f, 0.05f); break;
                    case 4: s.setFundamentalHz(e2); break;
                    case 5: s.setAmount(0.0f); break;
                    default: s.setAmount(1.0f); break;
                }
            };
        };
        const auto renderFresh = [&](std::size_t block, const float* ptr) {
            auto s = std::make_unique<BassCharacterStage>();
            initial(*s);
            s->prepare(fs, kSccBlock);
            SccStereo y{in, in};
            sccRunEvents(*s, y.first, y.second, block, ptr, charAt, events(*s));
            REQUIRE(s->stateFinite());
            return y;
        };
        for (const bool withPointer : {false, true}) {
            const float* ptr = withPointer ? dropF.data() : nullptr;
            const SccStereo ref = renderFresh(blocks[0], ptr);
            for (std::size_t b = 1; b < blocks.size(); ++b) {
                const SccStereo y = renderFresh(blocks[b], ptr);
                const bool same = sccBitEqual(y.first, ref.first) && sccBitEqual(y.second, ref.second);
                const double res = std::max(sccResidualDbfs(y.first, ref.first), sccResidualDbfs(y.second, ref.second));
                INFO(sccAlgoName(a) << ", pointer " << withPointer << ", block " << blocks[b]
                                    << " vs block 1: bit-identical " << same << ", residual " << res << " dBFS");
                if (!same)
                    WARN("SC-018 recorded residual: " << sccAlgoName(a) << ", pointer " << withPointer << ", block "
                                                      << blocks[b] << ": " << res << " dBFS");
                CHECK((same || res <= kSccResidualBarDbfs));
            }
        }

        // Two renders from reset() are bit-identical (and the first is recorded against the prepare render).
        {
            auto s = std::make_unique<BassCharacterStage>();
            initial(*s);
            s->prepare(fs, kSccBlock);
            std::array<SccStereo, 3> r;
            for (auto& y : r) {
                initial(*s);
                (*s).reset();
                y = SccStereo{in, in};
                sccRunEvents(*s, y.first, y.second, kSccBlock, dropF.data(), charAt, events(*s));
            }
            INFO(sccAlgoName(a) << ": render after prepare vs after reset bit-identical "
                                << (sccBitEqual(r[0].first, r[1].first) && sccBitEqual(r[0].second, r[1].second)));
            CHECK(sccBitEqual(r[1].first, r[2].first));
            CHECK(sccBitEqual(r[1].second, r[2].second));
        }
        // prepare 48 k -> 96 k -> 48 k equals a fresh 48 k render.
        {
            auto s = std::make_unique<BassCharacterStage>();
            initial(*s);
            s->prepare(fs, kSccBlock);
            s->prepare(96000.0, kSccBlock);
            s->prepare(fs, kSccBlock);
            SccStereo y{in, in};
            sccRunEvents(*s, y.first, y.second, kSccBlock, dropF.data(), charAt, events(*s));
            const SccStereo fresh = renderFresh(kSccBlock, dropF.data());
            INFO(sccAlgoName(a) << ": 48k -> 96k -> 48k residual "
                                << std::max(sccResidualDbfs(y.first, fresh.first), sccResidualDbfs(y.second, fresh.second))
                                << " dBFS");
            CHECK(sccBitEqual(y.first, fresh.first));
            CHECK(sccBitEqual(y.second, fresh.second));
        }
    }

    // ShapeMorph: setters at fixed absolute sample indices.
    const std::vector<std::size_t> shapeAt{3001, 9000, 12000, 15000, 18000};
    for (const float s0 : kSccShapeAnchors) {
        float other = 0.5f;
        if (s0 == 0.0f)
            other = 1.0f;
        else if (s0 == 0.5f)
            other = 0.0f;
        const auto initial = [&](ShapeMorph& m) {
            m.setShape(s0);
            m.setEnabled(true);
            m.setFundamentalHz(e1);
        };
        const auto events = [&](ShapeMorph& m) {
            return [&m, s0, other, e2](std::size_t i) {
                switch (i) {
                    case 0: m.setShape(other); break;
                    case 1: m.setEnabled(false); break;
                    case 2: m.setEnabled(true); break;
                    case 3: m.setFundamentalHz(e2); break;
                    default: m.setShape(s0); break;
                }
            };
        };
        const auto renderFresh = [&](std::size_t block, const float* ptr) {
            auto m = std::make_unique<ShapeMorph>();
            initial(*m);
            m->prepare(fs, kSccBlock);
            SccStereo y{in, in};
            sccRunEvents(*m, y.first, y.second, block, ptr, shapeAt, events(*m));
            REQUIRE(m->stateFinite());
            return y;
        };
        for (const bool withPointer : {false, true}) {
            const float* ptr = withPointer ? dropF.data() : nullptr;
            const SccStereo ref = renderFresh(blocks[0], ptr);
            for (std::size_t b = 1; b < blocks.size(); ++b) {
                const SccStereo y = renderFresh(blocks[b], ptr);
                const bool same = sccBitEqual(y.first, ref.first) && sccBitEqual(y.second, ref.second);
                const double res = std::max(sccResidualDbfs(y.first, ref.first), sccResidualDbfs(y.second, ref.second));
                INFO("SHAPE " << s0 << ", pointer " << withPointer << ", block " << blocks[b]
                              << " vs block 1: bit-identical " << same << ", residual " << res << " dBFS");
                if (!same)
                    WARN("SC-018 recorded residual: SHAPE " << s0 << ", pointer " << withPointer << ", block "
                                                            << blocks[b] << ": " << res << " dBFS");
                CHECK((same || res <= kSccResidualBarDbfs));
            }
        }
        {
            auto m = std::make_unique<ShapeMorph>();
            initial(*m);
            m->prepare(fs, kSccBlock);
            std::array<SccStereo, 3> r;
            for (auto& y : r) {
                initial(*m);
                (*m).reset();
                y = SccStereo{in, in};
                sccRunEvents(*m, y.first, y.second, kSccBlock, dropF.data(), shapeAt, events(*m));
            }
            INFO("SHAPE " << s0 << ": render after prepare vs after reset bit-identical "
                          << (sccBitEqual(r[0].first, r[1].first) && sccBitEqual(r[0].second, r[1].second)));
            CHECK(sccBitEqual(r[1].first, r[2].first));
            CHECK(sccBitEqual(r[1].second, r[2].second));
        }
        {
            auto m = std::make_unique<ShapeMorph>();
            initial(*m);
            m->prepare(fs, kSccBlock);
            m->prepare(96000.0, kSccBlock);
            m->prepare(fs, kSccBlock);
            SccStereo y{in, in};
            sccRunEvents(*m, y.first, y.second, kSccBlock, dropF.data(), shapeAt, events(*m));
            const SccStereo fresh = renderFresh(kSccBlock, dropF.data());
            INFO("SHAPE " << s0 << ": 48k -> 96k -> 48k residual "
                          << std::max(sccResidualDbfs(y.first, fresh.first), sccResidualDbfs(y.second, fresh.second))
                          << " dBFS");
            CHECK(sccBitEqual(y.first, fresh.first));
            CHECK(sccBitEqual(y.second, fresh.second));
        }
    }
}

// =============================================================================
// SC-019: sample-rate behaviour
// =============================================================================

TEST_CASE("ShapeCharacter_SampleRates", "[systems][profundum][shape-character]") {
    const std::array<double, 5> rates{44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    const double f0 = SCT::kE1Hz;
    std::vector<SccCell> cells;
    for (const SccAlgo a : kSccAlgos)
        cells.push_back(sccCharCell(a, 1.0f, 0.5f));
    for (const float s : kSccShapeAnchors)
        cells.push_back(sccShapeCell(s));

    std::vector<double> c48(cells.size(), 0.0);
    std::vector<double> l48(cells.size(), 0.0);
    std::vector<double> c96(cells.size(), 0.0);
    std::vector<double> l96(cells.size(), 0.0);
    for (const double fs : rates) {
        const bool describe = (fs == 48000.0 || fs == 96000.0);
        const double seconds =
            describe ? kSccSettleSeconds + static_cast<double>(HFT::aliasFftLength(fs, f0)) / fs + 0.05 : 1.0;
        const std::vector<float> in = SCT::fixture(SpectralShapeRecipe::kSawAnchor, f0, fs, seconds);
        for (std::size_t k = 0; k < cells.size(); ++k) {
            const SccStereo y = sccRenderCell(cells[k], in, in, fs, static_cast<float>(f0), SccRunSpec{});
            INFO(sccCellName(cells[k]) << ", fs " << fs << ": peak " << sccPeakAbs(y.first));
            CHECK(sccAllFinite(y.first));
            CHECK(sccAllFinite(y.second));
            CHECK(sccPeakAbs(y.first) <= kSccCeiling);
            CHECK(sccPeakAbs(y.second) <= kSccCeiling);
            if (describe) {
                const double c = SCT::centroidOct(y.first.data(), y.first.size(), fs, f0);
                const double l = SCT::levelDb(y.first.data(), y.first.size(), fs, f0);
                if (fs == 48000.0) {
                    c48[k] = c;
                    l48[k] = l;
                } else {
                    c96[k] = c;
                    l96[k] = l;
                }
            }
        }
    }
    for (std::size_t k = 0; k < cells.size(); ++k) {
        INFO(sccCellName(cells[k]) << ", E1: C 48k " << c48[k] << " / 96k " << c96[k] << " oct, L 48k " << l48[k]
                                   << " / 96k " << l96[k] << " dB");
        CHECK(std::abs(c48[k] - c96[k]) <= 0.1);
        CHECK(std::abs(l48[k] - l96[k]) <= 0.5);
    }
}

// =============================================================================
// SC-022 (Shape arm): output bound
// =============================================================================

TEST_CASE("ShapeCharacter_OutputBoundedShape", "[systems][profundum][shape-character][shape-morph]") {
    const double fs = kSccFs;
    constexpr double kSeconds = 1.5;
    struct Input {
        std::string name;
        double f0;
        std::vector<float> x;
    };
    std::vector<Input> inputs;
    for (const SccNote note : {SccNote{"E0", SCT::kE0Hz}, SccNote{"C3", SCT::kC3Hz}}) {
        // Fold-100 kGrowl normalised to peak 1.
        std::vector<float> g = SCT::fold100(SCT::fixture(SpectralShapeRecipe::kGrowl, note.hz, fs, kSeconds), fs, note.hz);
        const double pk = sccPeakAbs(g);
        REQUIRE(pk > 0.0);
        for (float& v : g)
            v = static_cast<float>(static_cast<double>(v) / pk);
        inputs.push_back(Input{.name = std::string("Fold-100 kGrowl ") + note.name, .f0 = note.hz, .x = std::move(g)});
        // Full-scale sine.
        inputs.push_back(Input{.name = std::string("full-scale sine ") + note.name,
                               .f0 = note.hz,
                               .x = HFT::calibrationSine(fs, note.hz, kSeconds, 1.0)});
    }
    for (const Input& in : inputs) {
        for (const float shape : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            auto m = sccMakeShape(shape, static_cast<float>(in.f0), fs);
            SccStereo y{in.x, in.x};
            sccRunStage(*m, y.first, y.second, SccRunSpec{});
            INFO(in.name << ", SHAPE " << shape << ": peak " << sccPeakAbs(y.first) << ", preCeilingPeak "
                         << m->preCeilingPeak());
            CHECK(sccAllFinite(y.first));
            CHECK(sccPeakAbs(y.first) <= kSccCeiling);
            CHECK(sccPeakAbs(y.second) <= kSccCeiling);
        }
    }
}

// =============================================================================
// SC-020: the Phase 9 handoff record (FR-040, FR-041, C-15)
// =============================================================================

TEST_CASE("ShapeCharacter_H1TransferRecord", "[systems][profundum][shape-character]") {
    static_assert(ShapeMorph::latencySamples() == 0, "FR-041: ShapeMorph has no look-ahead");
    static_assert(BassCharacterStage::latencySamples() == 0, "FR-041: BassCharacterStage has no look-ahead");
    static_assert(Rectifier::latencySamples() == 0, "FR-041: Rectifier has no look-ahead");
    CHECK(ShapeMorph::latencySamples() == 0u);
    CHECK(BassCharacterStage::latencySamples() == 0u);
    CHECK(Rectifier::latencySamples() == 0u);

    const double fs = kSccFs;
    constexpr double kSeconds = 1.55;   // 1 s settle + a >= 0.5 s integer-period frame
    constexpr double kFrameSeconds = 0.5;
    constexpr double kDelta = 0.005;    // group delay: forward difference over f0·(1 + δ)
    const std::array<SccFixture, 2> fixtures{{
        {.name = "kSawAnchor", .controls = SpectralShapeRecipe::kSawAnchor},
        {.name = "kGrowl", .controls = SpectralShapeRecipe::kGrowl},
    }};
    const std::array<SccNote, 3> notes{{{"E0", SCT::kE0Hz}, {"E1", SCT::kE1Hz}, {"E2", SCT::kE2Hz}}};
    const std::vector<SccCell> cells = sccAllCells(std::vector<float>(kSccTones.begin(), kSccTones.end()));

    const auto h1 = [&](const std::vector<float>& y, double f) {
        const auto settle = static_cast<std::size_t>(std::llround(kSccSettleSeconds * fs));
        REQUIRE(y.size() > settle);
        const std::size_t len = HFT::integerPeriodLength(y.size() - settle, fs, f, kFrameSeconds);
        return HFT::goertzelPhasor(y.data() + (y.size() - len), len, fs, f);
    };
    const auto wrap = [](double x) {
        while (x > HFT::kTwoPi * 0.5)
            x -= HFT::kTwoPi;
        while (x <= -HFT::kTwoPi * 0.5)
            x += HFT::kTwoPi;
        return x;
    };

    const std::string dir = sccEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc020-h1-transfer.csv");
    REQUIRE(csv.good());
    csv << "stage,setting,tone,fixture,note,f0_hz,h1_db,h1_deg,group_delay_samples,phase_delay_samples,status\n";
    std::size_t rows = 0;
    for (const SccFixture& fx : fixtures) {
        for (const SccNote& note : notes) {
            const double fa = note.hz;
            const double fb = note.hz * (1.0 + kDelta);
            const std::vector<float> inA = SCT::fixture(fx.controls, fa, fs, kSeconds);
            const std::vector<float> inB = SCT::fixture(fx.controls, fb, fs, kSeconds);
            const std::complex<double> hiA = h1(inA, fa);
            const std::complex<double> hiB = h1(inB, fb);
            REQUIRE(std::norm(hiA) > 0.0);
            REQUIRE(std::norm(hiB) > 0.0);
            for (const SccCell& c : cells) {
                const SccStereo yA = sccRenderCell(c, inA, inA, fs, static_cast<float>(fa), SccRunSpec{});
                const SccStereo yB = sccRenderCell(c, inB, inB, fs, static_cast<float>(fb), SccRunSpec{});
                REQUIRE(sccAllFinite(yA.first));
                REQUIRE(sccAllFinite(yB.first));
                const std::complex<double> hoA = h1(yA.first, fa);
                const std::complex<double> hoB = h1(yB.first, fb);
                const std::complex<double> tA = hoA / hiA;
                const std::complex<double> tB = hoB / hiB;
                const bool absent = std::norm(hoA) < kSccH1AbsentRatio * std::norm(hiA);
                const double db = 10.0 * std::log10(std::max(std::norm(tA), 1.0e-300));
                const double phA = std::arg(tA);
                const double deg = phA * 360.0 / HFT::kTwoPi;
                const double dphi = wrap(std::arg(tB) - phA);
                const double groupDelay = -dphi / (HFT::kTwoPi * (fb - fa)) * fs;
                const double phaseDelay = -phA / (HFT::kTwoPi * fa) * fs;
                csv << (c.isShape ? "ShapeMorph" : "BassCharacterStage") << ","
                    << (c.isShape ? std::to_string(c.shape) : std::string(sccAlgoName(c.algo))) << ","
                    << (c.isShape ? std::string("") : std::to_string(c.tone)) << "," << fx.name << "," << note.name
                    << "," << fa << ",";
                if (absent)
                    csv << ",,,,h1 absent\n";
                else
                    csv << db << "," << deg << "," << groupDelay << "," << phaseDelay << ",recorded\n";
                ++rows;
                CHECK(Krate::DSP::detail::isFinite(db));
                CHECK(Krate::DSP::detail::isFinite(groupDelay));
            }
        }
    }
    WARN("SC-020 recorded: " << rows << " h1 transfer rows in " << dir << "sc020-h1-transfer.csv");
    CHECK(rows == fixtures.size() * notes.size() * cells.size());
}

// =============================================================================
// SC-025: f0 feed and Drop tracking
// =============================================================================

TEST_CASE("ShapeCharacter_DropTracksHarmonicNumbersSmoke", "[systems][profundum][shape-character]") {
    // SHAPE 0.5 + Crush, block 64, kSawAnchor: clause (a) at 2 instants and clause (e). (a) per the round-2
    // ruling (2026-10-16): keytracked members within 0.1 % for both cells; |H(n)| gated for SHAPE 0.5, recorded for
    // Crush.
    const double fs = kSccFs;
    constexpr std::size_t kBlock = 64;
    const SccDrop drop = sccMakeDrop(fs, kSccDropPreRoll, kSccDropTotal);
    const std::vector<SccInstant> instants = sccInstants(drop, {24.0, 6.0});
    const SpectralShapeRecipe::Controls& saw = SpectralShapeRecipe::kSawAnchor;
    const SccStereo core = sccCoreRender(saw, drop.f[0], fs, kSccDropTotal, kBlock, drop.f.data());
    REQUIRE(sccAllFinite(core.first));

    const std::array<SccCell, 2> cells{sccShapeCell(0.5f), sccCharCell(SccAlgo::Crush, 1.0f, 0.5f)};
    for (const SccCell& c : cells) {
        const SccStereo y = sccRenderCell(c, core.first, core.second, fs, drop.stair[0],
                                          SccRunSpec{.block = kBlock, .f0PerSample = drop.stair.data()});
        REQUIRE(sccAllFinite(y.first));
        // (a), round-2 ruling: keytracked members within 0.1 %; |H(n)| gated for HOLLOW (SHAPE 0.5) only.
        const bool hGated = c.isShape && c.shape == 0.5f;
        for (const SccInstant& ins : instants) {
            const double kw = sccCheckKeyed(c, core, drop, ins.start, kBlock, fs,
                                            sccCellName(c) + ", instant d <= " + std::to_string(ins.thresholdSt) + " st");
            WARN("SC-025(a) smoke " << sccCellName(c) << ", instant d <= " << ins.thresholdSt
                                    << " st: keytracked members worst relative difference " << kw);
            const SccPhaseFrame frD = sccPhaseFrame(drop.phi, ins.start, kSccHarmonicCycles, fs);
            const std::size_t total = sccSteadyTotal(ins.fc8, fs);
            const SccStereo coreS = sccCoreRender(saw, static_cast<float>(ins.fc8), fs, total, kSccBlock, nullptr);
            const SccStereo yS =
                sccRenderCell(c, coreS.first, coreS.second, fs, static_cast<float>(ins.fc8), SccRunSpec{});
            const SccPhaseFrame frS = sccSteadyFrame(total, ins.fc8, kSccHarmonicCycles, fs);
            const SccHCompare h = sccCompareH(core.first.data(), y.first.data(), frD, coreS.first.data(),
                                              yS.first.data(), frS, ins.fc8, fs);
            if (hGated) {
                INFO(sccCellName(c) << ", instant d <= " << ins.thresholdSt << " st (sample " << ins.start << ", fc "
                                    << ins.fc8 << " Hz): max |dH| " << h.maxDevDb << " dB at h" << h.worstN
                                    << ", compared " << h.compared << ", skipped " << h.skipped
                                    << "; drop/steady dB: " << h.detail);
                CHECK(h.compared >= 8u);
                CHECK(h.maxDevDb <= kSccHBarDb);
            } else {
                WARN("SC-025(a) smoke |H(n)| recorded (not gated): " << sccCellName(c) << ", instant d <= "
                                                                     << ins.thresholdSt << " st: max |dH| "
                                                                     << h.maxDevDb << " dB at h" << h.worstN);
            }
        }
        const SccStereo coreE2 =
            sccCoreRender(saw, static_cast<float>(SCT::kE2Hz), fs, 24000, kBlock, nullptr);
        sccNullPointerIdentity(c, coreE2, kBlock);
    }
}

TEST_CASE("ShapeCharacter_DropTracksHarmonicNumbers", "[systems][profundum][shape-character][long]") {
    const double fs = kSccFs;
    const SccDrop drop = sccMakeDrop(fs, kSccDropPreRoll, kSccDropTotal);
    const std::vector<SccInstant> instants = sccInstants(drop, {24.0, 12.0, 6.0, 3.0, 1.5});
    const std::array<SccFixture, 2> fixtures{{
        {.name = "kSawAnchor", .controls = SpectralShapeRecipe::kSawAnchor},
        {.name = "kGrowl", .controls = SpectralShapeRecipe::kGrowl},
    }};
    const std::array<std::size_t, 2> blocks{64, 512};
    std::vector<SccCell> cells;
    for (const float s : kSccShapeAnchors)
        cells.push_back(sccShapeCell(s));
    for (const SccAlgo a : kSccAlgos) {
        for (const float t : kSccTones)
            cells.push_back(sccCharCell(a, 1.0f, t));
    }
    const auto e2 = static_cast<float>(SCT::kE2Hz);
    const std::size_t ctlAt = kSccDropPreRoll + 1024;  // the controls' step: 21 ms into the Drop, block-aligned

    const std::string dir = sccEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + "sc025-drop.csv");
    REQUIRE(csv.good());
    csv << "clause,fixture,block,cell,instant_st,fc_hz,value,steady_value,bar,pass\n";
    for (const SccInstant& ins : instants)
        csv << "instant,,,," << ins.thresholdSt << "," << ins.fc8 << "," << ins.start << "," << ins.fc16 << ",,\n";

    double controlBMax = 0.0;
    double keyedWorst = 0.0;
    std::string keyedWorstWhere = "none";
    double hollowWorst = 0.0;
    std::string hollowWorstWhere = "none";
    double recordedWorst = 0.0;
    std::string recordedWorstWhere = "none";
    std::string controlBWhere = "none";
    // Positive controls of (d), per arm: [0] hard switch / regime, [1] unsmoothed full-range step.
    std::array<std::size_t, 2> charControlFlags{0, 0};
    std::array<std::size_t, 2> shapeControlFlags{0, 0};

    for (const SccFixture& fx : fixtures) {
        // Steady core inputs at each instant's f0 (8- and 16-cycle frame midpoints).
        std::vector<SccStereo> core8;
        std::vector<SccStereo> core16;
        for (const SccInstant& ins : instants) {
            core8.push_back(sccCoreRender(fx.controls, static_cast<float>(ins.fc8), fs, sccSteadyTotal(ins.fc8, fs),
                                          kSccBlock, nullptr));
            core16.push_back(sccCoreRender(fx.controls, static_cast<float>(ins.fc16), fs,
                                           sccSteadyTotal(ins.fc16, fs), kSccBlock, nullptr));
        }
        std::array<SccStereo, 2> coreDrop;
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            coreDrop[b] = sccCoreRender(fx.controls, drop.f[0], fs, kSccDropTotal, blocks[b], drop.f.data());
            REQUIRE(sccAllFinite(coreDrop[b].first));
        }
        const SccStereo coreE2 = sccCoreRender(fx.controls, e2, fs, 24000, kSccBlock, nullptr);
        // (d) Crush: 21 steady cores at f0 log-spaced from E2·2^(48/12) down to E2 (round-3 ruling).
        constexpr std::size_t kCrushGrid = 21;
        constexpr std::size_t kCrushSteadyLen = 24000;  // 0.5 s; steady max taken from 0.25 s
        std::vector<float> crushGridF(kCrushGrid);
        std::vector<SccStereo> crushCores;
        for (std::size_t g = 0; g < kCrushGrid; ++g) {
            const double oct = 4.0 * (1.0 - static_cast<double>(g) / static_cast<double>(kCrushGrid - 1));
            crushGridF[g] = static_cast<float>(SCT::kE2Hz * std::exp2(oct));
            crushCores.push_back(sccCoreRender(fx.controls, crushGridF[g], fs, kCrushSteadyLen, kSccBlock, nullptr));
        }

        for (const SccCell& c : cells) {
            const bool isCrush = !c.isShape && c.algo == SccAlgo::Crush;
            const bool isFracture = !c.isShape && c.algo == SccAlgo::Fracture;
            double crushSteadyMax = 0.0;
            if (isCrush) {
                for (std::size_t g = 0; g < kCrushGrid; ++g) {
                    const SccStereo yg =
                        sccRenderCell(c, crushCores[g].first, crushCores[g].second, fs, crushGridF[g], SccRunSpec{});
                    crushSteadyMax = std::max(crushSteadyMax,
                                              SCT::steadyDeltaMax(yg.first.data(), kCrushSteadyLen / 2, yg.first.size()));
                }
            }
            // Steady outputs per instant.
            std::vector<SccStereo> steady8;
            std::vector<SccStereo> steady16;
            for (std::size_t i = 0; i < instants.size(); ++i) {
                steady8.push_back(sccRenderCell(c, core8[i].first, core8[i].second, fs,
                                                static_cast<float>(instants[i].fc8), SccRunSpec{}));
                if (isCrush || isFracture)
                    steady16.push_back(sccRenderCell(c, core16[i].first, core16[i].second, fs,
                                                     static_cast<float>(instants[i].fc16), SccRunSpec{}));
            }

            for (std::size_t b = 0; b < blocks.size(); ++b) {
                const std::size_t block = blocks[b];
                const SccStereo& in = coreDrop[b];
                const SccStereo y = sccRenderCell(c, in.first, in.second, fs, drop.stair[0],
                                                  SccRunSpec{.block = block, .f0PerSample = drop.stair.data()});
                REQUIRE(sccAllFinite(y.first));
                const std::string where =
                    std::string(fx.name) + ", block " + std::to_string(block) + ", " + sccCellName(c);

                // (a) harmonic numbers stay fixed (round-2 ruling 2026-10-16): every keytracked member read
                // through the probes at each instant's sample equals a steady render's at that f0 within 0.1 %;
                // |H(n)| is gated (1.5 dB) for the all-linear HOLLOW regime (SHAPE 0.5) only and recorded for
                // every other cell (the core's partials above h14 leave a Drop with phases unlike a fresh
                // note's, so a waveform-dependent stage's per-harmonic transfer is not comparable).
                const bool hGated = c.isShape && c.shape == 0.5f;
                double cellRecorded = 0.0;
                std::string cellRecordedAt;
                for (std::size_t i = 0; i < instants.size(); ++i) {
                    const SccInstant& ins = instants[i];
                    const std::string at = where + ", instant d <= " + std::to_string(ins.thresholdSt) + " st";
                    const double kw = sccCheckKeyed(c, in, drop, ins.start, block, fs, at);
                    if (kw > keyedWorst) {
                        keyedWorst = kw;
                        keyedWorstWhere = at;
                    }
                    const SccPhaseFrame frD = sccPhaseFrame(drop.phi, ins.start, kSccHarmonicCycles, fs);
                    const std::size_t total = core8[i].first.size();
                    const SccPhaseFrame frS = sccSteadyFrame(total, ins.fc8, kSccHarmonicCycles, fs);
                    const SccHCompare h = sccCompareH(in.first.data(), y.first.data(), frD, core8[i].first.data(),
                                                      steady8[i].first.data(), frS, ins.fc8, fs);
                    csv << "a," << fx.name << "," << block << "," << sccCellName(c) << "," << ins.thresholdSt << ","
                        << ins.fc8 << "," << h.maxDevDb << ",," << (hGated ? kSccHBarDb : 0.0) << ","
                        << ((!hGated || h.maxDevDb <= kSccHBarDb) ? 1 : 0) << "\n";
                    if (hGated) {
                        INFO(at << " (fc " << ins.fc8 << " Hz): max |dH| " << h.maxDevDb << " dB at h" << h.worstN
                                << ", compared " << h.compared << ", skipped " << h.skipped
                                << "; drop/steady dB: " << h.detail);
                        CHECK(h.compared >= 8u);
                        CHECK(h.maxDevDb <= kSccHBarDb);
                        if (h.maxDevDb > hollowWorst) {
                            hollowWorst = h.maxDevDb;
                            hollowWorstWhere = at + ", h" + std::to_string(h.worstN);
                        }
                    } else if (h.maxDevDb > cellRecorded) {
                        cellRecorded = h.maxDevDb;
                        cellRecordedAt = "instant d <= " + std::to_string(ins.thresholdSt) + " st, h"
                                         + std::to_string(h.worstN);
                    }
                }
                if (!hGated) {
                    WARN("SC-025(a) |H(n)| recorded (not gated): " << where << ": max |dH| " << cellRecorded
                                                                   << " dB (" << cellRecordedAt << ")");
                    if (cellRecorded > recordedWorst) {
                        recordedWorst = cellRecorded;
                        recordedWorstWhere = where;
                        recordedWorstWhere += ", ";
                        recordedWorstWhere += cellRecordedAt;
                    }
                }

                // (b) positive control: per-block setFundamentalHz only, block 512, ShapeMorph and Crush.
                if (block == 512 && (c.isShape || (isCrush && c.tone == 0.5f))) {
                    const SccStereo yc = sccRenderCell(c, in.first, in.second, fs, drop.stair[0],
                                                       SccRunSpec{.block = block, .perBlockF0 = drop.stair.data()});
                    for (std::size_t i = 0; i < instants.size(); ++i) {
                        const SccInstant& ins = instants[i];
                        const SccPhaseFrame frD = sccPhaseFrame(drop.phi, ins.start, kSccHarmonicCycles, fs);
                        const SccPhaseFrame frS =
                            sccSteadyFrame(core8[i].first.size(), ins.fc8, kSccHarmonicCycles, fs);
                        const SccHCompare h = sccCompareH(in.first.data(), yc.first.data(), frD,
                                                          core8[i].first.data(), steady8[i].first.data(), frS,
                                                          ins.fc8, fs);
                        csv << "b," << fx.name << "," << block << "," << sccCellName(c) << "," << ins.thresholdSt
                            << "," << ins.fc8 << "," << h.maxDevDb << ",," << kSccHBarDb << ","
                            << (h.maxDevDb > kSccHBarDb ? 1 : 0) << "\n";
                        if (h.maxDevDb > controlBMax) {
                            controlBMax = h.maxDevDb;
                            controlBWhere = where + ", instant d <= " + std::to_string(ins.thresholdSt) + " st, h"
                                            + std::to_string(h.worstN);
                        }
                    }
                }

                // (c) Crush follows the feed (round-3 ruling 2026-10-16): at every sample of each instant's
                // 16-cycle frame the hold-phasor increment and the hold multiple read through the probe equal
                // f0[k] / (F·fs) and max(1, min(m(A), F·fs / (4·f0[k]))) within 0.1 % (every sample: the stage
                // reads the feed per sample, the feed itself steps on the 16-sample cadence); I is recorded.
                // Fracture's sidebands sit on the instantaneous f0/2 lattice.
                std::vector<double> crushFeedWorst;
                if (isCrush) {
                    std::vector<std::pair<std::size_t, std::size_t>> frames;
                    for (const SccInstant& ins : instants) {
                        const SccPhaseFrame fr = sccPhaseFrame(drop.phi, ins.start, kSccLatticeCycles, fs);
                        frames.emplace_back(fr.begin, fr.begin + fr.wd.size());
                    }
                    crushFeedWorst = sccCrushFeedWorst(c, in, drop, fs, frames);
                }
                if (isCrush || isFracture) {
                    for (std::size_t i = 0; i < instants.size(); ++i) {
                        const SccInstant& ins = instants[i];
                        const SccPhaseFrame frD = sccPhaseFrame(drop.phi, ins.start, kSccLatticeCycles, fs);
                        const SccPhaseFrame frS =
                            sccSteadyFrame(core16[i].first.size(), ins.fc16, kSccLatticeCycles, fs);
                        if (isCrush) {
                            const double iD = std::max(sccInharmonicDb(y.first.data(), frD, fs), SCT::kFloorClampDb);
                            const double iS =
                                std::max(sccInharmonicDb(steady16[i].first.data(), frS, fs), SCT::kFloorClampDb);
                            const double iIn = sccInharmonicDb(in.first.data(), frD, fs);
                            csv << "c-crush-I," << fx.name << "," << block << "," << sccCellName(c) << ","
                                << ins.thresholdSt << "," << ins.fc16 << "," << iD << "," << iS << ",recorded,1\n";
                            csv << "c-crush-feed," << fx.name << "," << block << "," << sccCellName(c) << ","
                                << ins.thresholdSt << "," << ins.fc16 << "," << crushFeedWorst[i] << ",,"
                                << kSccKeyedRelBar << "," << (crushFeedWorst[i] <= kSccKeyedRelBar ? 1 : 0) << "\n";
                            WARN("SC-025(c) " << where << ", instant d <= " << ins.thresholdSt
                                              << " st: I (floored, recorded) during the Drop " << iD << " dB, steady "
                                              << iS << " dB (input during the Drop " << iIn
                                              << " dB); feed tracking worst relative " << crushFeedWorst[i]);
                            INFO(where << ", instant d <= " << ins.thresholdSt
                                       << " st: Crush phasor increment / hold multiple vs the feed, worst relative "
                                       << crushFeedWorst[i] << " over the frame (bar 0.001)");
                            CHECK(crushFeedWorst[i] <= kSccKeyedRelBar);
                        } else {
                            const double hD = sccHalfLatticeDb(y.first.data(), frD, fs);
                            const double hS = sccHalfLatticeDb(steady16[i].first.data(), frS, fs);
                            csv << "c-fracture-half," << fx.name << "," << block << "," << sccCellName(c) << ","
                                << ins.thresholdSt << "," << ins.fc16 << "," << hD << "," << hS << ",-40,"
                                << (hD >= -40.0 ? 1 : 0) << "\n";
                            INFO(where << ", instant d <= " << ins.thresholdSt
                                       << " st: odd-f0/2 lattice power re total during the Drop " << hD
                                       << " dB, steady " << hS << " dB (SC-024(b) Amount-1 bar -40 dB)");
                            CHECK(hD >= -40.0);
                        }
                    }
                }

                // (d) click-free: references hold the stage's f0 at the Drop's start and end values.
                {
                    const SccStereo refStart =
                        sccRenderCell(c, in.first, in.second, fs, drop.stair[0], SccRunSpec{.block = block});
                    const SccStereo refEnd = sccRenderCell(c, in.first, in.second, fs, e2, SccRunSpec{.block = block});
                    const SCT::ClickCount cc = SCT::differentialClicks(y.first.data(), refStart.first.data(),
                                                                       refEnd.first.data(), kSccDropTotal, fs,
                                                                       SCT::kE2Hz);
                    if (isCrush) {
                        // Sample-delta bound over the Drop window (the 60 ms fall + 50 ms), the count recorded.
                        // Round-6 ruling (2026-10-16, Clarifications "Group 9 rulings, round 6"): the reference is
                        // the largest of (i) the 21-point held-f0 grid's steady max |dy| (round 3), (ii) the Drop
                        // render's own pre-fall settled segment [0.25 s, fall start) and (iii) its own post-fall
                        // settled segment [fall end + 0.25 s, end): the core leaves the Drop with partial phases
                        // unlike a fresh note's, so no fresh-note render bounds the post-fall staircase.
                        const std::size_t w0 = kSccDropPreRoll;
                        const std::size_t w1 = kSccDropPreRoll + static_cast<std::size_t>(0.110 * fs);
                        const double dropMax = SCT::steadyDeltaMax(y.first.data(), w0, w1);
                        const auto settle = static_cast<std::size_t>(0.25 * fs);
                        const std::size_t fallEndSample = kSccDropPreRoll + static_cast<std::size_t>(0.060 * fs);
                        REQUIRE(settle < kSccDropPreRoll);
                        REQUIRE(fallEndSample + settle + 1 < y.first.size());
                        const double preMax = SCT::steadyDeltaMax(y.first.data(), settle, kSccDropPreRoll);
                        const double postMax =
                            SCT::steadyDeltaMax(y.first.data(), fallEndSample + settle, y.first.size());
                        const double refMax = std::max({crushSteadyMax, preMax, postMax});
                        const char* refWhich = "post-fall segment";
                        if (refMax == crushSteadyMax)
                            refWhich = "held-f0 grid";
                        else if (refMax == preMax)
                            refWhich = "pre-fall segment";
                        const double ratio = dropMax / refMax;
                        // Recorded: the five largest |dy| in the window, as offsets from the fall start (w0) and
                        // from the end of the 60 ms fall.
                        std::vector<std::pair<double, std::size_t>> steps;
                        for (std::size_t k = std::max<std::size_t>(w0, 1); k < w1; ++k)
                            steps.emplace_back(std::abs(static_cast<double>(y.first[k]) - static_cast<double>(y.first[k - 1])), k);
                        std::partial_sort(steps.begin(), steps.begin() + 5, steps.end(),
                                          [](const auto& a, const auto& b) { return a.first > b.first; });
                        const auto fallEnd = static_cast<long long>(kSccDropPreRoll) + static_cast<long long>(0.060 * fs);
                        std::string top;
                        for (std::size_t j = 0; j < 5; ++j) {
                            const auto k = static_cast<long long>(steps[j].second);
                            top += " " + std::to_string(steps[j].first) + " @ fall+" +
                                   std::to_string(k - static_cast<long long>(w0)) + " (end" +
                                   (k >= fallEnd ? "+" : "") + std::to_string(k - fallEnd) + ")";
                        }
                        csv << "d-crush-delta," << fx.name << "," << block << "," << sccCellName(c) << ",,,"
                            << ratio << "," << cc.counted << ",1.1," << (ratio <= 1.1 ? 1 : 0) << "\n";
                        WARN("SC-025(d) Crush " << where << ": Drop max |dy| " << dropMax << " vs reference max "
                                                << refMax << " (" << refWhich << "; grid " << crushSteadyMax
                                                << ", pre-fall " << preMax << ", post-fall " << postMax
                                                << "), ratio " << ratio
                                                << " (bar 1.1); largest |dy|:" << top
                                                << "; differential count recorded: counted " << cc.counted
                                                << " (guarded out " << cc.guardedOut << ")");
                        INFO(where << ": Crush sample-delta ratio " << ratio << " (bar 1.1)");
                        CHECK(ratio <= 1.1);
                    } else {
                        csv << "d," << fx.name << "," << block << "," << sccCellName(c) << ",,," << cc.counted << ","
                            << cc.referenceDetections << ",0," << (cc.counted == 0u ? 1 : 0) << "\n";
                        INFO(where << ": counted " << cc.counted << " (guarded out " << cc.guardedOut
                                   << ", transition detections " << cc.transitionDetections
                                   << ", reference detections " << cc.referenceDetections << ", N " << cc.windowN << ")");
                        CHECK(cc.counted == 0u);
                    }
                }

                // (e) null pointer == constant array.
                sccNullPointerIdentity(c, coreE2, block);
            }
        }

        // (d) positive controls, per arm, on this fixture's Drop at both host blocks.
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            const std::size_t block = blocks[b];
            const SccStereo& in = coreDrop[b];
            const SccRunSpec ptrSpec{.block = block, .f0PerSample = drop.stair.data()};
            const std::vector<std::size_t> at{ctlAt};
            // Character (i): a hard Fold -> Crush switch; references Fold and Crush through the same Drop.
            {
                const SccStereo refA = sccRenderCell(sccCharCell(SccAlgo::Fold, 1.0f, 0.5f), in.first, in.second, fs,
                                                     drop.stair[0], ptrSpec);
                const SccStereo refB = sccRenderCell(sccCharCell(SccAlgo::Crush, 1.0f, 0.5f), in.first, in.second, fs,
                                                     drop.stair[0], ptrSpec);
                auto s = std::make_unique<BassCharacterStage>();
                s->setAlgorithm(SccAlgo::Fold);
                s->setAmount(1.0f);
                s->setTone(0.5f);
                s->setFundamentalHz(drop.stair[0]);
                s->prepare(fs, kSccBlock);
                SccCharProbe::setSeamHardSwitch(*s, true);
                SccStereo t = in;
                BassCharacterStage& st = *s;
                sccRunEvents(st, t.first, t.second, block, drop.stair.data(), at,
                             [&st](std::size_t) { st.setAlgorithm(SccAlgo::Crush); });
                const SCT::ClickCount cc = SCT::differentialClicks(t.first.data(), refA.first.data(),
                                                                   refB.first.data(), kSccDropTotal, fs, SCT::kE2Hz);
                WARN("SC-025(d) control (i) Character hard switch, " << fx.name << ", block " << block << ": counted "
                                                                      << cc.counted << " (guarded out " << cc.guardedOut << ")"
                                                                      << (cc.counted >= 1u ? " - FLAGGED" : ""));
                if (cc.counted >= 1u)
                    ++charControlFlags[0];
            }
            // Control placement (round-3 ruling 2026-10-16): the step goes where it exceeds the references'
            // local steady steps (the amplitude guard's comparison) and lies at least 2N from every reference
            // detection, searched every 8 samples over the Drop and the 85 ms after it (the Rectifier rule).
            const auto ctlN = static_cast<std::size_t>(std::floor(fs / SCT::kE2Hz / 8.0));
            const std::size_t ctlLo = kSccDropPreRoll;
            const std::size_t ctlHi = kSccDropPreRoll + static_cast<std::size_t>(0.145 * fs);
            // Character (ii): an unsmoothed, hard-bypassed Fold Amount 1 -> 0 step; references Fold Amount 1 through
            // the same Drop (the stage without the step at the same settings) and the input (Amount 0's copy).
            {
                const SccStereo refA = sccRenderCell(sccCharCell(SccAlgo::Fold, 1.0f, 0.5f), in.first, in.second, fs,
                                                     drop.stair[0], ptrSpec);
                std::vector<std::size_t> refDet = sccDetections(refA.first, fs);
                {
                    const std::vector<std::size_t> d2 = sccDetections(in.first, fs);
                    refDet.insert(refDet.end(), d2.begin(), d2.end());
                    std::sort(refDet.begin(), refDet.end());
                }
                const auto jump = [&](std::size_t k) {
                    return std::abs(static_cast<double>(in.first[k]) - static_cast<double>(refA.first[k]));
                };
                const SccCtlPick old = sccScoreControl(ctlAt, 0, refDet, refA.first, in.first, ctlN, jump);
                const SccCtlPick pick = sccPickControl(ctlLo, ctlHi, 0, refDet, refA.first, in.first, ctlN, jump);
                WARN("SC-025(d) control (ii) Character Amount step placement, " << fx.name << ", block " << block
                                                                                << ": previous " << sccPickText(old)
                                                                                << "; chosen " << sccPickText(pick));
                const std::vector<std::size_t> ctlEvent{pick.at};
                auto s = std::make_unique<BassCharacterStage>();
                s->setAlgorithm(SccAlgo::Fold);
                s->setAmount(1.0f);
                s->setTone(0.5f);
                s->setFundamentalHz(drop.stair[0]);
                s->prepare(fs, kSccBlock);
                SccCharProbe::setSeamUnsmoothed(*s, true);
                SccCharProbe::setSeamHardBypass(*s, true);
                SccStereo t = in;
                BassCharacterStage& st = *s;
                sccRunEvents(st, t.first, t.second, block, drop.stair.data(), ctlEvent,
                             [&st](std::size_t) { st.setAmount(0.0f); });
                const SCT::ClickCount cc = SCT::differentialClicks(t.first.data(), refA.first.data(),
                                                                   in.first.data(), kSccDropTotal, fs, SCT::kE2Hz);
                WARN("SC-025(d) control (ii) Character unsmoothed Amount step, "
                     << fx.name << ", block " << block << ": counted " << cc.counted << " (guarded out " << cc.guardedOut << ")"
                     << (cc.counted >= 1u ? " - FLAGGED" : ""));
                if (cc.counted >= 1u)
                    ++charControlFlags[1];
            }
            // ShapeMorph (i): hard regimes with a smoothed SHAPE 0 -> 1 (its first discontinuity is the ROUND ->
            // HOLLOW switch, sccHardRegimeDelay samples after setShape, sized by a SHAPE 0.5 render); (ii) an
            // unsmoothed SHAPE 0 -> 1 step (ROUND -> BITE at the event). References: SHAPE 0 and SHAPE 1.
            const SccStereo ref0 = sccRenderCell(sccShapeCell(0.0f), in.first, in.second, fs, drop.stair[0], ptrSpec);
            const SccStereo ref1 = sccRenderCell(sccShapeCell(1.0f), in.first, in.second, fs, drop.stair[0], ptrSpec);
            const SccStereo ref05 =
                sccRenderCell(sccShapeCell(0.5f), in.first, in.second, fs, drop.stair[0], ptrSpec);
            std::vector<std::size_t> shapeRefDet = sccDetections(ref0.first, fs);
            {
                const std::vector<std::size_t> d2 = sccDetections(ref1.first, fs);
                shapeRefDet.insert(shapeRefDet.end(), d2.begin(), d2.end());
                std::sort(shapeRefDet.begin(), shapeRefDet.end());
            }
            const std::size_t hardDelay = sccHardRegimeDelay(fs, drop.stair[0]);
            for (std::size_t k = 0; k < 2; ++k) {
                const std::size_t delay = (k == 0) ? hardDelay : std::size_t{0};
                const std::vector<float>& to = (k == 0) ? ref05.first : ref1.first;
                const auto jump = [&](std::size_t j) {
                    return std::abs(static_cast<double>(to[j]) - static_cast<double>(ref0.first[j]));
                };
                const SccCtlPick old = sccScoreControl(ctlAt, delay, shapeRefDet, ref0.first, ref1.first, ctlN, jump);
                const SccCtlPick pick =
                    sccPickControl(ctlLo, ctlHi, delay, shapeRefDet, ref0.first, ref1.first, ctlN, jump);
                WARN("SC-025(d) control (" << (k == 0 ? "i" : "ii") << ") ShapeMorph placement, " << fx.name
                                           << ", block " << block << " (delay " << delay
                                           << "): previous " << sccPickText(old) << "; chosen " << sccPickText(pick));
                const std::vector<std::size_t> ctlEvent{pick.at};
                auto m = sccMakeShape(0.0f, drop.stair[0], fs);
                if (k == 0)
                    SccShapeProbe::setSeamHardRegime(*m, true);
                else
                    SccShapeProbe::setSeamUnsmoothed(*m, true);
                SccStereo t = in;
                ShapeMorph& mm = *m;
                sccRunEvents(mm, t.first, t.second, block, drop.stair.data(), ctlEvent,
                             [&mm](std::size_t) { mm.setShape(1.0f); });
                const SCT::ClickCount cc = SCT::differentialClicks(t.first.data(), ref0.first.data(),
                                                                   ref1.first.data(), kSccDropTotal, fs, SCT::kE2Hz);
                WARN("SC-025(d) control (" << (k == 0 ? "i" : "ii") << ") ShapeMorph "
                                           << (k == 0 ? "hard regime switch" : "unsmoothed SHAPE step") << ", "
                                           << fx.name << ", block " << block << ": counted " << cc.counted << " (guarded out " << cc.guardedOut << ")"
                                           << (cc.counted >= 1u ? " - FLAGGED" : ""));
                if (cc.counted >= 1u)
                    ++shapeControlFlags[k];
            }
        }
    }

    WARN("SC-025(a) summary: keytracked members worst relative difference " << keyedWorst << " (" << keyedWorstWhere
                                                                           << "); HOLLOW |H(n)| worst " << hollowWorst
                                                                           << " dB (" << hollowWorstWhere
                                                                           << "); recorded nonlinear |H(n)| worst "
                                                                           << recordedWorst << " dB ("
                                                                           << recordedWorstWhere << ")");
    // (b) the pointer matters.
    csv << "b-summary,,,," << controlBWhere << ",," << controlBMax << ",," << kSccHBarDb << ","
        << (controlBMax > kSccHBarDb ? 1 : 0) << "\n";
    INFO("SC-025(b) per-block setFundamentalHz at block 512: largest |dH| " << controlBMax << " dB (" << controlBWhere
                                                                            << ")");
    CHECK(controlBMax > kSccHBarDb);
    // (d) both positive controls flagged in each arm at the note (at least one fixture / block cell each).
    INFO("SC-025(d) control flags: Character hard switch " << charControlFlags[0] << ", Character Amount step "
                                                           << charControlFlags[1] << ", ShapeMorph hard regime "
                                                           << shapeControlFlags[0] << ", ShapeMorph SHAPE step "
                                                           << shapeControlFlags[1]);
    CHECK(charControlFlags[0] >= 1u);
    CHECK(charControlFlags[1] >= 1u);
    CHECK(shapeControlFlags[0] >= 1u);
    CHECK(shapeControlFlags[1] >= 1u);
}
