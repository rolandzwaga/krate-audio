// ==============================================================================
// Layer 3: System Tests - BassMorph rendered chain
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md T009 (SC-005(a)(d)(e) rendered, SC-006(b), SC-009, SC-010, SC-015, SC-023, SC-024 render, FR-039;
// plan C-9, C-12, S4.10). Renders go through bass_morph_test_helpers.h (read-only, T007).
//   BassMorph_RecipeDomain                  SC-009: no audio member (compile time); core shape at interior p is
//                                           bitwise evaluateShape(resolved recipe); positive control: a 50/50
//                                           audio crossfade differs by >= 1 dB in some harmonic
//   BassMorph_Global                        SC-010: 4-voice poly, one advanceInterval per grid point, every voice
//                                           applies the same Resolved
//   BassMorph_Audibility                    SC-015: D_cx / D_nl / D_st >= 3 dB, D_br >= 0.25 oct, D_t ratio >= 2,
//                                           E_sub(sum, Lock) >= E_sub(sum, Blend) + 2 dB at p = 1
//   BassMorph_DropLatched                   SC-023 / FR-039: a Drop in flight never steps; the next trigger
//                                           follows B
//   BassMorph_AmpEnvelopeImmediateApply     FR-039 (plan S4.10 amp-envelope row): sustain / decayCurve apply at
//                                           the next sample, decayMs is latched at stage entry
//   BassMorph_EndpointEditMidMorphRender    SC-024 discrete render: one setAlgorithm at g_sw, SC-004(a)(b)
//                                           against the direct-setter reference
//   BassMorph_AnchorCurvesRendered [long]   SC-005(a)(d)(e) rendered (+ ...Smoke)
//   BassMorph_IdempotentRender [long]       SC-006(b) (+ ...Smoke)
// FTZ / DAZ: set per case (tasks.md Group 8 header). Every [long] case has a non-long ...Smoke sibling
// (tasks.md deviation 5). Recorded decisions: evidence/run-log/T009.txt.
// ==============================================================================

#include "bass_morph_test_helpers.h"
#include "shape_character_test_helpers.h"   // Phase 6's SC-011 describe / separate (SC-005(e), ruling B9-1)

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <krate/dsp/core/curve_table.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_core.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace Bmt = Krate::DSP::BassMorphTest;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::MorphState;
using BmMode = BassMorphEngine::AnchorMorphMode;

constexpr double kBmcFs = 48000.0;
constexpr std::size_t kBmcGrid = BassMorphEngine::kControlInterval;
/// SC-004(a) / SC-024: 10^(0.5/20) (plan C-15, D-9).
const double kBmcStepBar = std::pow(10.0, 0.5 / 20.0);

double bmcNoteHz(int midi) {
    return static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(midi));
}

/// tasks.md rule 11: evidence goes under specs/profundum-phase8-bass-morph/evidence/.
std::string bmcEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase8-bass-morph/evidence/";
}

void bmcWriteEvidence(const std::string& name, const std::string& text) {
    const std::string dir = bmcEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + name);
    out << text;
}

const char* bmcModeName(BmMode m) {
    switch (m) {
        case BmMode::Lock: return "Lock";
        case BmMode::Blend: return "Blend";
        case BmMode::Punch: return "Punch";
    }
    return "?";
}

bool bmcSameBits(float x, float y) {
    return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
}

/// A steady render (amp envelope bypassed) at a static p (snapped by prepare's reset(), plan S1 C-5).
Bmt::MorphChainConfig bmcSteady(const MorphState& a, const MorphState& b, BmMode mode, float p, double f0) {
    Bmt::MorphChainConfig c{};
    c.a = a;
    c.b = b;
    c.mode = mode;
    c.initialPosition = p;
    c.f0 = f0;
    c.fs = kBmcFs;
    c.ampEnvelope = false;
    return c;
}

Bmt::MorphEvent bmcPositionEvent(std::size_t sample, float p) {
    Bmt::MorphEvent e{};
    e.sample = sample;
    e.kind = Bmt::MorphEvent::Kind::Position;
    e.value = p;
    return e;
}

Bmt::MorphEvent bmcEndpointEvent(std::size_t sample, BassMorphEngine::Endpoint which, const MorphState& s) {
    Bmt::MorphEvent e{};
    e.sample = sample;
    e.kind = (which == BassMorphEngine::Endpoint::A) ? Bmt::MorphEvent::Kind::EndpointA
                                                     : Bmt::MorphEvent::Kind::EndpointB;
    e.state = s;
    return e;
}

/// Harmonics 1..16 of x over one kLowFrequencyFftSize frame starting at `start`.
std::vector<double> bmcFirst16(const std::vector<float>& x, double f0, std::size_t start) {
    REQUIRE(start + Krate::DSP::TestUtils::kLowFrequencyFftSize <= x.size());
    return Krate::DSP::ProfundumTest::renderedHarmonicPowers(x.data() + start, kBmcFs, f0, 16);
}

// ---- SC-009(a): audio-member detectors. A requires-expression only evaluates to false (instead of being
//      ill-formed) when its operand is dependent, so each probe is a variable template. ----
template <class E>
constexpr bool bmcHasStereoBlock = requires(E& e, float* x, std::size_t n) { e.processBlock(x, x, n); };
template <class E>
constexpr bool bmcHasScalarProcess = requires(E& e) { e.process(1.0f); };
template <class E>
constexpr bool bmcHasMonoBlock = requires(E& e, float* x, std::size_t n) { e.processBlock(x, n); };

// ---- FR-039 amp-envelope harness: the engine + one ADSREnvelope with the fixture's change-only setters ----

constexpr std::size_t kBmcNoSample = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kBmcLeafDecayMs = static_cast<std::size_t>(Bmt::AmpEnvLeaf::DecayMs);
constexpr std::size_t kBmcLeafSustain = static_cast<std::size_t>(Bmt::AmpEnvLeaf::Sustain);
constexpr std::size_t kBmcLeafDecayCurve = static_cast<std::size_t>(Bmt::AmpEnvLeaf::DecayCurve);

struct BmcAdsrRun {
    std::vector<float> y;                                         ///< per sample
    std::vector<std::size_t> gridSample;                          ///< per interval
    std::vector<float> step;                                      ///< y(with setters) - y(twin without) at the grid sample
    std::vector<float> phase;                                     ///< replicated decay table phase there (-1 outside Decay)
    std::vector<Krate::DSP::ADSRStage> stageBefore;               ///< stage the grid sample was processed in
    std::vector<std::array<float, Bmt::kAmpEnvLeafCount>> before; ///< leaves in force before the interval's setters
    std::vector<std::array<float, Bmt::kAmpEnvLeafCount>> after;  ///< leaves in force after them
    std::array<std::size_t, Bmt::kAmpEnvLeafCount> setterCalls{};
    std::size_t sustainEntry = kBmcNoSample;                      ///< sample whose process() entered Sustain
};

/// Gate on at sample 0 (after the first interval's setters, the fixture's step order 8 -> 11), ramp
/// kMinPositionRampMs, setPosition(1) at the grid point `morphAt`. At every grid sample a copy of the envelope taken
/// BEFORE the setters (the twin) processes the same sample, so `step` is exactly what the setters changed. The decay
/// table phase is replicated from adsr_envelope.h:313 (logPhase_ += logPhaseInc_) and :471-474 (enterDecay: phase 0,
/// inc = 1 / max(1, decayTimeMs * 0.001 * fs), latched at stage entry).
BmcAdsrRun bmcRunAdsr(const MorphState& a, const MorphState& b, std::size_t morphAt, std::size_t total) {
    REQUIRE(morphAt % kBmcGrid == 0);
    BassMorphEngine engine;
    REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::A, a));
    REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::B, b));
    engine.setPositionRampMs(BassMorphEngine::kMinPositionRampMs);
    engine.prepare(kBmcFs);

    Krate::DSP::ADSREnvelope env;
    env.prepare(static_cast<float>(kBmcFs));
    const float fsF = static_cast<float>(kBmcFs);

    BmcAdsrRun run;
    run.y.reserve(total);
    std::array<float, Bmt::kAmpEnvLeafCount> last{};
    bool applied = false;
    float ph = 0.0f;
    float inc = 0.0f;
    Krate::DSP::ADSREnvelope twin;   // re-assigned at every grid sample, read only there
    for (std::size_t n = 0; n < total; ++n) {
        const bool atGrid = (n % kBmcGrid) == 0;
        if (atGrid) {
            if (n == morphAt)
                engine.setPosition(1.0f);
            const BassMorphEngine::Resolved& r = engine.advanceInterval();
            twin = env;
            const std::array<float, Bmt::kAmpEnvLeafCount> before = last;
            for (std::size_t i = 0; i < Bmt::kAmpEnvLeafCount; ++i) {
                const float x = Bmt::ampEnvLeaf(r.state.ampEnv, i);
                if (applied && bmcSameBits(x, last[i]))
                    continue;
                Bmt::applyAmpEnvLeaf(env, i, x);
                last[i] = x;
                ++run.setterCalls[i];
            }
            applied = true;
            if (n == 0) {
                env.gate(true);
                twin.gate(true);
            }
            run.gridSample.push_back(n);
            run.before.push_back(before);
            run.after.push_back(last);
        }
        const Krate::DSP::ADSRStage sb = env.getStage();
        const float yM = env.process();
        const Krate::DSP::ADSRStage sa = env.getStage();
        if (sb == Krate::DSP::ADSRStage::Decay) {
            ph += inc;
        } else if (sb == Krate::DSP::ADSRStage::Attack && sa == Krate::DSP::ADSRStage::Decay) {
            ph = 0.0f;
            inc = 1.0f / std::max(1.0f, last[kBmcLeafDecayMs] * 0.001f * fsF);
        }
        if (sb == Krate::DSP::ADSRStage::Decay && sa == Krate::DSP::ADSRStage::Sustain
            && run.sustainEntry == kBmcNoSample)
            run.sustainEntry = n;
        if (atGrid) {
            const float yT = twin.process();
            run.step.push_back(yM - yT);
            run.phase.push_back(sb == Krate::DSP::ADSRStage::Decay ? ph : -1.0f);
            run.stageBefore.push_back(sb);
        }
        run.y.push_back(yM);
    }
    return run;
}

/// max |y[n] - y[n-1]| over n in [max(from, 1), to).
double bmcMaxStep(const std::vector<float>& y, std::size_t from, std::size_t to) {
    double m = 0.0;
    for (std::size_t n = std::max<std::size_t>(from, 1); n < std::min(to, y.size()); ++n)
        m = std::max(m, std::fabs(static_cast<double>(y[n]) - static_cast<double>(y[n - 1])));
    return m;
}

/// The decay table ADSREnvelope::setDecayCurve(amount) builds (adsr_envelope.h:167-171).
std::array<float, Krate::DSP::kCurveTableSize> bmcDecayTable(float amount) {
    std::array<float, Krate::DSP::kCurveTableSize> t{};
    Krate::DSP::generatePowerCurveTable(t, std::clamp(amount, -1.0f, 1.0f), 1.0f, 0.0f);
    return t;
}

// ---- SC-005 rendered (shared by the [long] case and its smoke) ----

void bmcAnchorCurvesRendered(const std::vector<float>& ps, const std::string& csvName) {
    REQUIRE(!ps.empty());
    REQUIRE(ps.front() == 0.0f);   // the (d) reference is the p = 0 Lock render
    const auto pair = Bmt::anchorCurvePair();
    const double f0 = bmcNoteHz(28);
    const std::array<BmMode, 3> modes{BmMode::Lock, BmMode::Blend, BmMode::Punch};

    struct Row {
        float p;
        BmMode mode;
        double level;    ///< the resolved anchor.level of the render
        double handed;   ///< the Mass-biased level handed to the Anchor (FR-054; Mass 50 -> the same)
        double w1;       ///< the Anchor tap's w_1 (mid, read window)
        double eSub;     ///< E_sub of the Anchor tap
    };
    std::vector<Row> rows;
    for (const BmMode m : modes) {
        for (const float p : ps) {
            const Bmt::MorphChainTaps t = Bmt::renderMorphChain(bmcSteady(pair.first, pair.second, m, p, f0));
            REQUIRE(!t.resolvedPerInterval.empty());
            const std::vector<double> w = Bmt::readWindowHarmonicPowers(Bmt::midOf(t.anchorL, t.anchorR), kBmcFs, f0);
            REQUIRE(!w.empty());
            rows.push_back(Row{.p = p,
                               .mode = m,
                               .level = static_cast<double>(t.resolvedPerInterval.back().state.anchor.level),
                               .handed = static_cast<double>(t.anchorLevelHanded.back()),
                               .w1 = w[0],
                               .eSub = Bmt::eSubDb(t.anchorL, t.anchorR, f0, kBmcFs)});
        }
    }
    const Row& ref = rows.front();   // Lock, p = 0
    REQUIRE(ref.mode == BmMode::Lock);
    REQUIRE(ref.w1 > 0.0);
    REQUIRE(ref.level > 0.0);

    std::ostringstream csv;
    csv << std::setprecision(6);
    csv << "p,mode,level,handedLevel,anchorW1RelDb,expectedDb,deltaDb,eSubAnchorDb\n";
    double lockMin = std::numeric_limits<double>::max();
    double lockMax = std::numeric_limits<double>::lowest();
    double worstD = 0.0;
    for (const Row& r : rows) {
        const double rel = Krate::DSP::ProfundumTest::safeDb10(r.w1 / ref.w1);
        const double expected = 20.0 * std::log10(r.level / ref.level);
        const double delta = rel - expected;
        worstD = std::max(worstD, std::fabs(delta));
        csv << r.p << "," << bmcModeName(r.mode) << "," << r.level << "," << r.handed << "," << rel << "," << expected
            << "," << delta << "," << r.eSub << "\n";
        if (r.mode == BmMode::Lock) {
            lockMin = std::min(lockMin, r.eSub);
            lockMax = std::max(lockMax, r.eSub);
        }
        INFO("SC-005(d) p " << r.p << " " << bmcModeName(r.mode) << ": w1 " << rel << " dB re Lock p=0, expected "
                            << expected << " dB");
        CHECK(std::fabs(delta) <= 0.1);
    }

    // (e) massOnlyPair in Lock (ruling B9-1, 2026-10-19): the Mass-only morph is audible by Phase 6's own Mass bar
    // (SC-011: >= 3 dB on any of E_low, E_high, THD or I, or >= 0.25 oct on C, ShapeCharacterTest::describe /
    // separate on the output), read between p = 0 and p = 1. D_nl is recorded, not gated: Mass 100 puts Fold at 61.3,
    // inside HarmonicFold's compression regime, which lowers n >= 9 by design (measured -6.44 dB at the Group 8 gate).
    const auto mo = Bmt::massOnlyPair();
    const Bmt::MorphChainTaps m0 = Bmt::renderMorphChain(bmcSteady(mo.first, mo.second, BmMode::Lock, 0.0f, f0));
    const Bmt::MorphChainTaps m1 = Bmt::renderMorphChain(bmcSteady(mo.first, mo.second, BmMode::Lock, 1.0f, f0));
    const double dNl0 = Bmt::dNlDb(m0, f0, kBmcFs);
    const double dNl1 = Bmt::dNlDb(m1, f0, kBmcFs);
    csv << "# (a) Lock E_sub(Anchor tap) range dB," << (lockMax - lockMin) << ",bar,0.05\n";
    csv << "# (d) worst |w1 - 20log10(level)| dB," << worstD << ",bar,0.1\n";
    const auto d0 = Krate::DSP::ShapeCharacterTest::describe(m0.outL.data(), m0.outL.size(), kBmcFs, f0);
    const auto d1 = Krate::DSP::ShapeCharacterTest::describe(m1.outL.data(), m1.outL.size(), kBmcFs, f0);
    const auto sep = Krate::DSP::ShapeCharacterTest::separate(d0, d1);
    csv << "# (e) massOnlyPair Lock Phase-6 audibility: E_low," << d0.Elow << "," << d1.Elow << ",E_high," << d0.Ehigh
        << "," << d1.Ehigh << ",THD," << d0.Thd << "," << d1.Thd << ",I," << d0.I << "," << d1.I << ",C_oct," << d0.C
        << "," << d1.C << ",separation_dB," << sep.maxDb << ",which," << sep.which << ",dC_oct," << sep.dC
        << ",distinct," << sep.distinct << ",bar,3dB_or_0.25oct\n";
    csv << "# (e) massOnlyPair Lock D_nl p0," << dNl0 << ",p1," << dNl1 << ",travel," << (dNl1 - dNl0)
        << " (recorded, B9-1)\n";
    bmcWriteEvidence(csvName, csv.str());
    std::cout << std::setprecision(6) << csv.str();

    INFO("SC-005(a) Lock E_sub(Anchor tap) range " << (lockMax - lockMin) << " dB");
    CHECK(lockMax - lockMin <= 0.05);
    INFO("SC-005(e) massOnlyPair Lock Phase-6 audibility: separation " << sep.maxDb << " dB (" << sep.which << "), dC "
                                                                       << sep.dC << " oct; D_nl recorded " << dNl0
                                                                       << " -> " << dNl1 << " dB");
    CHECK(sep.distinct);
}

// ---- SC-006(b) (shared by the [long] case and its smoke) ----

/// A random pair constrained per SC-006(b): field.motionDepth 0, anchor.mode in {Lock, Follow}, Drop Amount 0.
std::pair<MorphState, MorphState> bmcIdempotentPair(std::mt19937& rng) {
    std::pair<MorphState, MorphState> pr = Bmt::randomValidPair(rng);
    for (MorphState* s : {&pr.first, &pr.second}) {
        s->field.motionDepth = 0.0f;
        s->anchor.mode = (static_cast<std::uint32_t>(rng()) & 1u) != 0u ? Krate::DSP::AnchorOscillator::Mode::Follow
                                                                         : Krate::DSP::AnchorOscillator::Mode::Lock;
        s->dropAmountSt = 0.0f;
    }
    REQUIRE(Krate::DSP::isValidMorphState(pr.first));
    REQUIRE(Krate::DSP::isValidMorphState(pr.second));
    return pr;
}

constexpr std::size_t kBmcIdemUp = 12000;     // 0.25 s
constexpr std::size_t kBmcIdemDown = 36000;   // 0.75 s

double bmcIdempotentSeconds(double settleSeconds = 1.0) {
    return 0.85 + settleSeconds + static_cast<double>(Krate::DSP::TestUtils::kLowFrequencyFftSize) / kBmcFs + 0.05;
}

/// One SC-006(b) cell: A -> B -> A against `ref` (A alone, same note-on and host blocks). Returns the worst
/// per-harmonic difference (dB) over the harmonics within 60 dB of the strongest.
double bmcIdempotentCell(const MorphState& a, const MorphState& b, BmMode mode, const Bmt::MorphChainTaps& ref,
                         std::ostringstream& table, std::size_t pairIdx, double settleSeconds = 1.0) {
    const double f0 = bmcNoteHz(28);
    Bmt::MorphChainConfig c = bmcSteady(a, b, mode, 0.0f, f0);
    c.seconds = bmcIdempotentSeconds(settleSeconds);
    c.automation.push_back(bmcPositionEvent(kBmcIdemUp, 1.0f));
    c.automation.push_back(bmcPositionEvent(kBmcIdemDown, 0.0f));
    const Bmt::MorphChainTaps t = Bmt::renderMorphChain(c);
    REQUIRE(t.outL.size() == ref.outL.size());

    bool reachedOne = false;
    std::size_t tLand = kBmcNoSample;
    for (std::size_t i = 0; i < t.resolvedPerInterval.size(); ++i) {
        const float p = t.resolvedPerInterval[i].position;
        if (bmcSameBits(p, 1.0f))
            reachedOne = true;
        if (t.advanceSamples[i] >= kBmcIdemDown && bmcSameBits(p, 0.0f)) {
            tLand = t.advanceSamples[i];
            break;
        }
    }
    REQUIRE(reachedOne);
    REQUIRE(tLand != kBmcNoSample);
    const std::size_t start = tLand + static_cast<std::size_t>(std::llround(settleSeconds * kBmcFs));
    REQUIRE(start + Krate::DSP::TestUtils::kLowFrequencyFftSize <= t.outL.size());

    const std::vector<double> wT = bmcFirst16(Bmt::midOf(t.outL, t.outR), f0, start);
    const std::vector<double> wR = bmcFirst16(Bmt::midOf(ref.outL, ref.outR), f0, start);
    const double strongT = *std::max_element(wT.begin(), wT.end());
    const double strongR = *std::max_element(wR.begin(), wR.end());
    double worst = 0.0;
    std::size_t worstN = 0;
    std::size_t counted = 0;
    for (std::size_t n = 0; n < wR.size(); ++n) {
        if (wR[n] < strongR * 1e-6 && wT[n] < strongT * 1e-6)
            continue;   // outside 60 dB of the strongest on both renders
        ++counted;
        const double d = std::fabs(Krate::DSP::ProfundumTest::safeDb10(wT[n])
                                   - Krate::DSP::ProfundumTest::safeDb10(wR[n]));
        if (d > worst) {
            worst = d;
            worstN = n + 1;
        }
    }
    REQUIRE(counted > 0);
    table << pairIdx << "," << bmcModeName(mode) << "," << tLand << "," << counted << "," << worstN << "," << worst
        << "\n";
    return worst;
}

/// Hidden diagnostic (main loop, 2026-10-19): the SC-006(b) residue of pairs 2 and 6 against the settle time after
/// landing, with the pair's Mass / Fold / anchor mode / character printed, to attribute the 0.13 dB.
void bmcIdempotentSettleProbe() {
    std::mt19937 rng(0x8066u);
    const double f0 = bmcNoteHz(28);
    std::ostringstream table;
    table << std::setprecision(6);
    for (std::size_t k = 0; k < 7; ++k) {
        const auto pr = bmcIdempotentPair(rng);
        if (k != 2 && k != 6)
            continue;
        std::cout << "SC006 PROBE pair " << k << ": A mass " << pr.first.mass << " fold " << pr.first.foldAmount
                  << " char " << static_cast<int>(pr.first.characterAlgorithm) << "/" << pr.first.characterAmount
                  << " anchorMode " << static_cast<int>(pr.first.anchor.mode) << " level " << pr.first.anchor.level
                  << " exc " << pr.first.excitation.mix << " | B mass " << pr.second.mass << " fold "
                  << pr.second.foldAmount << " char " << static_cast<int>(pr.second.characterAlgorithm) << "/"
                  << pr.second.characterAmount << " anchorMode " << static_cast<int>(pr.second.anchor.mode)
                  << " level " << pr.second.anchor.level << " exc " << pr.second.excitation.mix << "\n";
        for (const double settle : {1.0, 8.0}) {
            Bmt::MorphChainConfig rc = bmcSteady(pr.first, pr.first, BmMode::Blend, 0.0f, f0);
            rc.seconds = bmcIdempotentSeconds(settle);
            const Bmt::MorphChainTaps ref = Bmt::renderMorphChain(rc);
            const double worst = bmcIdempotentCell(pr.first, pr.second, BmMode::Lock, ref, table, k, settle);
            std::cout << "SC006 PROBE pair " << k << " settle " << settle << " s: worst harmonic delta " << worst
                      << " dB\n";
        }
        // Variants (cause attribution): A's anchor mode forced to Lock; A's Fold forced to 20; both.
        struct Variant { const char* label; bool lockAnchor; bool lowFold; };
        for (const Variant v : {Variant{"A anchor Lock", true, false}, Variant{"A fold 20", false, true},
                                Variant{"both", true, true}}) {
            MorphState a = pr.first;
            MorphState b = pr.second;
            if (v.lockAnchor) {
                a.anchor.mode = Krate::DSP::AnchorOscillator::Mode::Lock;
                b.anchor.mode = Krate::DSP::AnchorOscillator::Mode::Lock;
            }
            if (v.lowFold) {
                a.foldAmount = 20.0f;
            }
            Bmt::MorphChainConfig rc = bmcSteady(a, a, BmMode::Blend, 0.0f, f0);
            rc.seconds = bmcIdempotentSeconds(1.0);
            const Bmt::MorphChainTaps ref = Bmt::renderMorphChain(rc);
            const double worst = bmcIdempotentCell(a, b, BmMode::Lock, ref, table, k, 1.0);
            std::cout << "SC006 PROBE pair " << k << " variant " << v.label << ": worst harmonic delta " << worst
                      << " dB\n";
        }
    }
    std::cout << table.str();
}

void bmcIdempotentRender(std::size_t numPairs, std::span<const BmMode> modes) {
    std::mt19937 rng(0x8066u);
    const double f0 = bmcNoteHz(28);
    std::ostringstream table;
    table << std::setprecision(6);
    table << "pair,mode,t_land,harmonics_compared,worst_harmonic,worst_delta_dB\n";
    for (std::size_t k = 0; k < numPairs; ++k) {
        auto pr = bmcIdempotentPair(rng);
        // B10-3 (2026-10-19): A's Fold is constrained to the soft regime (HarmonicFold's detector hysteresis above
        // kSoftEnd is path-dependent by Phase 2 design); a clamped pair is also rendered unclamped and recorded.
        if (pr.first.foldAmount > Krate::DSP::HarmonicFold::kSoftEnd) {
            Bmt::MorphChainConfig rcu = bmcSteady(pr.first, pr.first, BmMode::Blend, 0.0f, f0);
            rcu.seconds = bmcIdempotentSeconds();
            const Bmt::MorphChainTaps refU = Bmt::renderMorphChain(rcu);
            const double worstU = bmcIdempotentCell(pr.first, pr.second, BmMode::Lock, refU, table, k);
            std::cout << "SC006 recorded(b) pair " << k << " unclamped A fold " << pr.first.foldAmount
                      << ": worst harmonic delta " << worstU << " dB (B10-3: fold regime, recorded)\n";
            pr.first.foldAmount = Krate::DSP::HarmonicFold::kSoftEnd;
        }
        // A alone: at p = 0 the resolved state is A bitwise in every mode (SC-001), so one reference serves all three.
        Bmt::MorphChainConfig rc = bmcSteady(pr.first, pr.first, BmMode::Blend, 0.0f, f0);
        rc.seconds = bmcIdempotentSeconds();
        const Bmt::MorphChainTaps ref = Bmt::renderMorphChain(rc);
        for (const BmMode m : modes) {
            const double worst = bmcIdempotentCell(pr.first, pr.second, m, ref, table, k);
            INFO("SC-006(b) pair " << k << " " << bmcModeName(m) << ": worst harmonic delta " << worst << " dB");
            CHECK(worst <= 0.1);
        }
    }
    std::cout << table.str();
}

}  // namespace

// -----------------------------------------------------------------------------
// SC-009 (FR-038): recipe domain, never an audio crossfade
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_RecipeDomain", "[systems][profundum][bass-morph][sc-009]") {
    enableFTZDAZ();
    std::cout << std::setprecision(6);

    // (a) The engine has no member taking an audio buffer. Positive controls: each detector sees a real one.
    static_assert(!bmcHasStereoBlock<BassMorphEngine>, "SC-009(a): no processBlock(L, R, n)");
    static_assert(!bmcHasScalarProcess<BassMorphEngine>, "SC-009(a): no process(x)");
    static_assert(!bmcHasMonoBlock<BassMorphEngine>, "SC-009(a): no processBlock(x, n)");
    static_assert(bmcHasStereoBlock<Krate::DSP::ProfundumCore>, "detector control: ProfundumCore::processBlock");
    static_assert(bmcHasScalarProcess<Krate::DSP::DCBlocker>, "detector control: DCBlocker::process(float)");
    static_assert(bmcHasMonoBlock<Krate::DSP::DropEnvelope>, "detector control: DropEnvelope::processBlock");

    // (b) The core's converged shape at an interior p is evaluateShape of the RESOLVED recipe, bitwise. Mass 50 at
    //     both ends of the showcase pair, so the core receives the resolved recipe unbiased.
    const auto pair = Bmt::showcasePair();
    REQUIRE(pair.first.mass == Krate::DSP::MassLaw::kNeutralMass);
    REQUIRE(pair.second.mass == Krate::DSP::MassLaw::kNeutralMass);
    const double e1 = bmcNoteHz(28);
    for (const float p : {0.25f, 0.5f, 0.75f}) {
        BassMorphEngine engine;
        REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::A, pair.first));
        REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::B, pair.second));
        engine.setPosition(p);
        engine.prepare(kBmcFs);
        const BassMorphEngine::Resolved r = engine.resolved();
        REQUIRE(bmcSameBits(r.position, p));
        const Krate::DSP::SpectralShapeRecipe::Controls recipe = r.state.recipe;

        Krate::DSP::ProfundumCore core;
        core.prepare(kBmcFs);
        core.noteOn(static_cast<float>(e1));
        std::array<float, kBmcGrid> L{};
        std::array<float, kBmcGrid> R{};
        std::vector<float> prev;
        int stable = 0;
        int intervals = 0;
        constexpr int kCap = 4000;
        while (stable < 8 && intervals < kCap) {
            core.setControls(recipe);
            core.processBlock(L.data(), R.data(), L.size());
            const std::span<const float> g = core.shapeGains();
            bool same = prev.size() == g.size();
            for (std::size_t k = 0; same && k < g.size(); ++k)
                same = bmcSameBits(g[k], prev[k]);
            stable = same ? stable + 1 : 0;
            prev.assign(g.begin(), g.end());
            ++intervals;
        }
        INFO("SC-009(b) p " << p << ": " << intervals << " intervals, " << stable << " stable");
        REQUIRE(stable >= 8);   // the 4 000-interval cap reached is a failure

        const std::vector<float> expect = Krate::DSP::ProfundumTest::shapeOf(recipe, core.numPartials());
        const std::span<const float> got = core.shapeGains();
        REQUIRE(got.size() == expect.size());
        std::size_t mismatches = 0;
        std::size_t firstMismatch = kBmcNoSample;
        for (std::size_t k = 0; k < got.size(); ++k) {
            if (!bmcSameBits(got[k], expect[k])) {
                ++mismatches;
                if (firstMismatch == kBmcNoSample)
                    firstMismatch = k;
            }
        }
        INFO("SC-009(b) p " << p << ": " << mismatches << " mismatching partials, first " << firstMismatch);
        CHECK(mismatches == 0);

        // The resolved recipe is interior: its shape is neither endpoint's.
        const int np = core.numPartials();
        CHECK(Krate::DSP::ProfundumTest::shapeOf(pair.first.recipe, np) != expect);
        CHECK(Krate::DSP::ProfundumTest::shapeOf(pair.second.recipe, np) != expect);
    }

    // Positive control: the 50 / 50 audio crossfade of the A and B core taps differs from the p = 0.5 render by
    // >= 1 dB in at least one of harmonics 1 - 16.
    const Bmt::MorphChainTaps tA =
        Bmt::renderMorphChain(bmcSteady(pair.first, pair.second, BmMode::Blend, 0.0f, e1));
    const Bmt::MorphChainTaps tB =
        Bmt::renderMorphChain(bmcSteady(pair.first, pair.second, BmMode::Blend, 1.0f, e1));
    const Bmt::MorphChainTaps tM =
        Bmt::renderMorphChain(bmcSteady(pair.first, pair.second, BmMode::Blend, 0.5f, e1));
    const std::vector<float> midA = Bmt::midOf(tA.coreL, tA.coreR);
    const std::vector<float> midB = Bmt::midOf(tB.coreL, tB.coreR);
    const std::vector<float> midM = Bmt::midOf(tM.coreL, tM.coreR);
    REQUIRE(midA.size() == midB.size());
    std::vector<float> xfade(midA.size(), 0.0f);
    for (std::size_t i = 0; i < xfade.size(); ++i)
        xfade[i] = 0.5f * midA[i] + 0.5f * midB[i];
    const Bmt::ReadWindow w = Bmt::readWindow(xfade.size(), kBmcFs);
    const std::vector<double> wX = bmcFirst16(xfade, e1, w.start);
    const std::vector<double> wM = bmcFirst16(midM, e1, w.start);
    double maxDiff = 0.0;
    std::size_t maxN = 0;
    for (std::size_t n = 0; n < wX.size(); ++n) {
        const double d = std::fabs(Krate::DSP::ProfundumTest::safeDb10(wX[n])
                                   - Krate::DSP::ProfundumTest::safeDb10(wM[n]));
        std::cout << "SC-009 control h" << (n + 1) << ": crossfade " << Krate::DSP::ProfundumTest::safeDb10(wX[n])
                  << " dB, p=0.5 render " << Krate::DSP::ProfundumTest::safeDb10(wM[n]) << " dB\n";
        if (d > maxDiff) {
            maxDiff = d;
            maxN = n + 1;
        }
    }
    INFO("SC-009 positive control: max harmonic difference " << maxDiff << " dB at h" << maxN);
    CHECK(maxDiff >= 1.0);
}

// -----------------------------------------------------------------------------
// SC-010 (FR-015, FR-016): one global position, one advanceInterval per grid point
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_Global", "[systems][profundum][bass-morph][sc-010]") {
    enableFTZDAZ();
    const auto pair = Bmt::showcasePair();
    const std::array<double, 4> f0s{bmcNoteHz(28), bmcNoteHz(33), bmcNoteHz(40), bmcNoteHz(48)};
    constexpr double kSeconds = 0.5;
    const std::size_t total = Krate::DSP::MassDynamicsTest::renderLength(kBmcFs, kSeconds);
    const std::size_t expected = (total + kBmcGrid - 1) / kBmcGrid;

    for (const std::size_t hostBlock : {std::size_t{7}, std::size_t{512}}) {
        INFO("SC-010 host block " << hostBlock);
        Bmt::MorphChainConfig cfg{};
        cfg.a = pair.first;
        cfg.b = pair.second;
        cfg.fs = kBmcFs;
        cfg.seconds = kSeconds;
        cfg.hostBlock = hostBlock;
        cfg.automation.push_back(bmcPositionEvent(4800, 1.0f));   // 0 -> 1 at 0.1 s, default ramp
        const Bmt::MorphChainTaps t = Bmt::renderMorphPoly(std::span<const double>(f0s.data(), f0s.size()), cfg);

        REQUIRE(t.advanceSamples.size() == expected);
        std::size_t offGrid = 0;
        std::size_t notIncreasing = 0;
        for (std::size_t i = 0; i < t.advanceSamples.size(); ++i) {
            if (t.advanceSamples[i] % kBmcGrid != 0)
                ++offGrid;
            if (i > 0 && t.advanceSamples[i] <= t.advanceSamples[i - 1])
                ++notIncreasing;
        }
        CHECK(offGrid == 0);
        CHECK(notIncreasing == 0);

        REQUIRE(t.perVoiceResolved.size() == f0s.size());
        std::size_t mismatches = 0;
        for (std::size_t v = 0; v < t.perVoiceResolved.size(); ++v) {
            REQUIRE(t.perVoiceResolved[v].size() == expected);
            for (std::size_t i = 0; i < expected; ++i) {
                if (!Bmt::bitEqual(t.perVoiceResolved[v][i], t.perVoiceResolved[0][i]))
                    ++mismatches;
            }
        }
        CHECK(mismatches == 0);
        // The voices were compared across a moving state: p left 0 and landed at 1.
        CHECK(bmcSameBits(t.resolvedPerInterval.front().position, 0.0f));
        CHECK(bmcSameBits(t.resolvedPerInterval.back().position, 1.0f));
    }
}

// -----------------------------------------------------------------------------
// SC-015 (FR-084): audibility gate on the showcase pair (plan C-9: T008's C9 verdict GREEN precedes this case)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_Audibility", "[systems][profundum][bass-morph][sc-015]") {
    enableFTZDAZ();
    const auto pair = Bmt::showcasePair();
    struct BmcNote {
        const char* name;
        double f0;
    };
    const std::array<BmcNote, 2> notes{{{"E1", bmcNoteHz(28)}, {"C3", bmcNoteHz(48)}}};

    std::ostringstream csv;
    csv << std::setprecision(6);
    csv << "note,descriptor,at_p0,at_p1,travel,bar,pass\n";
    struct BmcCheck {
        std::string label;
        double travel;
        double bar;
    };
    std::vector<BmcCheck> checks;

    for (const BmcNote& note : notes) {
        const double f0 = note.f0;
        const Bmt::MorphChainTaps s0 =
            Bmt::renderMorphChain(bmcSteady(pair.first, pair.second, BmMode::Blend, 0.0f, f0));
        const Bmt::MorphChainTaps s1 =
            Bmt::renderMorphChain(bmcSteady(pair.first, pair.second, BmMode::Blend, 1.0f, f0));
        const Bmt::MorphChainTaps lock1 =
            Bmt::renderMorphChain(bmcSteady(pair.first, pair.second, BmMode::Lock, 1.0f, f0));
        const Bmt::MorphChainTaps g0 =
            Bmt::gateRender(bmcSteady(pair.first, pair.second, BmMode::Blend, 0.0f, f0));
        const Bmt::MorphChainTaps g1 =
            Bmt::gateRender(bmcSteady(pair.first, pair.second, BmMode::Blend, 1.0f, f0));

        const double fc0 = static_cast<double>(s0.resolvedPerInterval.back().state.field.crossoverHz);
        const double fc1 = static_cast<double>(s1.resolvedPerInterval.back().state.field.crossoverHz);
        const double dCx0 = Bmt::dCxDb(s0, f0, kBmcFs);
        const double dCx1 = Bmt::dCxDb(s1, f0, kBmcFs);
        const double dNl0 = Bmt::dNlDb(s0, f0, kBmcFs);
        const double dNl1 = Bmt::dNlDb(s1, f0, kBmcFs);
        const double dSt0 = Bmt::dStDb(s0, fc0, kBmcFs);
        const double dSt1 = Bmt::dStDb(s1, fc1, kBmcFs);
        const double dBr0 = Bmt::dBrOct(s0, f0, kBmcFs);
        const double dBr1 = Bmt::dBrOct(s1, f0, kBmcFs);
        const double dT0 = Bmt::dTMs(g0, f0, kBmcFs);
        const double dT1 = Bmt::dTMs(g1, f0, kBmcFs);
        const double eLock = Bmt::eSubDb(lock1.sumL, lock1.sumR, f0, kBmcFs);
        const double eBlend = Bmt::eSubDb(s1.sumL, s1.sumR, f0, kBmcFs);
        REQUIRE(dT0 > 0.0);

        const auto row = [&](const char* d, double v0, double v1, double travel, double bar) {
            csv << note.name << "," << d << "," << v0 << "," << v1 << "," << travel << "," << bar << ","
                << (travel >= bar ? "PASS" : "FAIL") << "\n";
            checks.push_back(BmcCheck{.label = std::string(note.name) + " " + d, .travel = travel, .bar = bar});
        };
        row("D_cx_dB", dCx0, dCx1, dCx1 - dCx0, 3.0);
        row("D_nl_dB", dNl0, dNl1, dNl1 - dNl0, 3.0);
        row("D_st_dB", dSt0, dSt1, dSt1 - dSt0, 3.0);
        row("D_br_oct", dBr0, dBr1, dBr1 - dBr0, 0.25);
        row("D_t_ratio", dT0, dT1, dT1 / dT0, 2.0);
        row("E_sub_Lock_minus_Blend_p1_dB", eBlend, eLock, eLock - eBlend, 2.0);
    }
    bmcWriteEvidence("sc015-audibility.csv", csv.str());
    std::cout << std::setprecision(6) << csv.str();
    for (const BmcCheck& c : checks) {
        INFO("SC-015 " << c.label << ": travel " << c.travel << " (bar " << c.bar << ")");
        CHECK(c.travel >= c.bar);
    }
}

// -----------------------------------------------------------------------------
// SC-023 (FR-039; Phase 4 D-4): a Drop in flight is latched; the next trigger follows B
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_DropLatched", "[systems][profundum][bass-morph][sc-023][fr-039]") {
    enableFTZDAZ();
    MorphState a{};
    a.dropAmountSt = 12.0f;
    a.dropTimeMs = 200.0f;
    a.dropShape = 0.0f;
    a.dropRebound = 0.0f;
    MorphState b{};
    b.dropAmountSt = 36.0f;
    b.dropTimeMs = 2000.0f;
    b.dropShape = 1.0f;
    b.dropRebound = 0.0f;
    REQUIRE(Krate::DSP::isValidMorphState(a));
    REQUIRE(Krate::DSP::isValidMorphState(b));

    BassMorphEngine engine;
    REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::A, a));
    REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::B, b));
    engine.prepare(kBmcFs);   // p = 0, default ramp

    const auto setDrop = [](Krate::DSP::DropEnvelope& d, const MorphState& s) {
        d.setAmount(s.dropAmountSt);
        d.setTime(s.dropTimeMs);
        d.setShape(s.dropShape);
        d.setRebound(s.dropRebound);
    };
    Krate::DSP::DropEnvelope morphed;
    morphed.prepare(kBmcFs);
    Krate::DSP::DropEnvelope refA;   // unmorphed A, triggered at sample 0
    refA.prepare(kBmcFs);
    setDrop(refA, a);
    Krate::DSP::DropEnvelope refB;   // fresh, B's values, triggered at the second note's sample
    refB.prepare(kBmcFs);
    setDrop(refB, b);

    constexpr std::size_t kMorphAt = 2400;      // 50 ms
    constexpr std::size_t kSecond = 120000;     // 2.5 s
    constexpr std::size_t kEnd = kSecond + 144000;   // + 3 s
    static_assert(kMorphAt % kBmcGrid == 0 && kSecond % kBmcGrid == 0);

    std::vector<float> aTrajectory;
    aTrajectory.reserve(kSecond);
    std::size_t mismatchA = 0;
    std::size_t mismatchB = 0;
    std::size_t firstMismatch = kBmcNoSample;
    bool leavesMovedInFlight = false;
    bool trajectoriesDiffer = false;
    for (std::size_t n = 0; n < kEnd; ++n) {
        if (n % kBmcGrid == 0) {
            if (n == kMorphAt)
                engine.setPosition(1.0f);
            const BassMorphEngine::Resolved& r = engine.advanceInterval();
            setDrop(morphed, r.state);
            if (morphed.isActive()
                && (!bmcSameBits(r.state.dropAmountSt, a.dropAmountSt) || !bmcSameBits(r.state.dropTimeMs, a.dropTimeMs)
                    || !bmcSameBits(r.state.dropShape, a.dropShape)))
                leavesMovedInFlight = true;
            if (n == 0) {
                morphed.trigger(true);
                refA.trigger(true);
            }
            if (n == kSecond) {
                REQUIRE(bmcSameBits(engine.position(), 1.0f));
                REQUIRE(bmcSameBits(r.state.dropAmountSt, b.dropAmountSt));
                REQUIRE(bmcSameBits(r.state.dropTimeMs, b.dropTimeMs));
                REQUIRE(bmcSameBits(r.state.dropShape, b.dropShape));
                REQUIRE(bmcSameBits(r.state.dropRebound, b.dropRebound));
                morphed.trigger(true);
                refB.trigger(true);
            }
        }
        static_cast<void>(morphed.process());
        if (n < kSecond) {
            static_cast<void>(refA.process());
            aTrajectory.push_back(refA.currentOffset());
            if (!bmcSameBits(morphed.currentOffset(), refA.currentOffset())) {
                ++mismatchA;
                if (firstMismatch == kBmcNoSample)
                    firstMismatch = n;
            }
        } else {
            static_cast<void>(refB.process());
            const std::size_t k = n - kSecond;
            if (k < aTrajectory.size() && !bmcSameBits(refB.currentOffset(), aTrajectory[k]))
                trajectoriesDiffer = true;
            if (!bmcSameBits(morphed.currentOffset(), refB.currentOffset())) {
                ++mismatchB;
                if (firstMismatch == kBmcNoSample)
                    firstMismatch = n;
            }
        }
    }
    INFO("SC-023: first mismatching sample " << firstMismatch);
    CHECK(mismatchA == 0);
    CHECK(mismatchB == 0);
    CHECK(leavesMovedInFlight);   // the setters really fed moving values while the first Drop was in flight
    CHECK(trajectoriesDiffer);    // A's and B's trajectories are distinguishable
}

// -----------------------------------------------------------------------------
// FR-039 (plan S4.10 amp-envelope row): the amp envelope applies sustain and decayCurve immediately; decayMs is
// latched at stage entry
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_AmpEnvelopeImmediateApply", "[systems][profundum][bass-morph][fr-039]") {
    enableFTZDAZ();
    constexpr std::size_t kMorph200 = 9600;   // 200 ms
    static_assert(kMorph200 % kBmcGrid == 0);
    constexpr std::size_t kWindowIntervals = 16;   // the 5 ms ramp is 8 intervals
    constexpr std::size_t kWindow = kWindowIntervals * kBmcGrid;
    constexpr double kTol = 1e-5;

    MorphState base{};
    base.ampEnv.decayMs = 2000.0f;
    std::ostringstream ev;
    ev << std::setprecision(6);
    ev << "FR-039 amp envelope immediate apply (tasks.md T009 case 5; plan S4.10). 48 kHz, ramp "
       << BassMorphEngine::kMinPositionRampMs << " ms, gate on at 0. Ratio = max|dg| / (10^(0.5/20) * static-A "
       << "max|dg|) over the same window: recorded for Phase 9, not gated.\n";
    ev << "case,window_start,window_end,max_abs_dg,static_ref_max_abs_dg,ratio\n";

    const auto record = [&](const char* name, const BmcAdsrRun& run, const BmcAdsrRun& ref, std::size_t from,
                            std::size_t to) {
        const double m = bmcMaxStep(run.y, from, to);
        const double r = bmcMaxStep(ref.y, from, to);
        ev << name << "," << from << "," << to << "," << m << "," << r << ",";
        if (r > 0.0)
            ev << m / (kBmcStepBar * r) << "\n";
        else
            ev << "n/a (static reference flat)\n";
    };

    // The four cases run in sequence (not SECTIONs) so the evidence file collects every row in one run.
    {   // (i) sustain 0 -> 0.8 during Decay
        // Both sides at decayCurve -0.7 (the pair still differs in sustain only). At the C-8 default 0.7 the table is
        // 1 - phase^4.29, so at the 200 ms morph (phase ~0.095) ds·(1 - table) ~ 4e-6 per interval, below kTol: the
        // 1e-5 comparison would be vacuous (Group 8 gate: 0 moving intervals). At -0.7, 1 - table ~ 0.58 there.
        MorphState a = base;
        a.ampEnv.sustain = 0.0f;
        a.ampEnv.decayCurve = -0.7f;
        MorphState b = a;
        b.ampEnv.sustain = 0.8f;
        const std::size_t total = kMorph200 + 2 * kWindow;
        const BmcAdsrRun run = bmcRunAdsr(a, b, kMorph200, total);
        const BmcAdsrRun ref = bmcRunAdsr(a, a, kMorph200, total);
        const auto tbl = bmcDecayTable(a.ampEnv.decayCurve);
        double worst = 0.0;
        std::size_t moving = 0;
        for (std::size_t i = 0; i < run.gridSample.size(); ++i) {
            if (run.gridSample[i] < kMorph200 || run.gridSample[i] >= kMorph200 + kWindow)
                continue;
            REQUIRE(run.stageBefore[i] == Krate::DSP::ADSRStage::Decay);
            const float ds = run.after[i][kBmcLeafSustain] - run.before[i][kBmcLeafSustain];
            const double expected =
                static_cast<double>(ds) * (1.0 - static_cast<double>(Krate::DSP::lookupCurveTable(tbl, run.phase[i])));
            worst = std::max(worst, std::fabs(static_cast<double>(run.step[i]) - expected));
            if (std::fabs(expected) > 1e-3)
                ++moving;
        }
        std::cout << std::setprecision(6) << "FR-039 (i): worst |step - ds(1 - table(phase))| " << worst << ", "
                  << moving << " moving intervals\n";
        INFO("FR-039 (i)");
        CHECK(moving > 0);
        CHECK(worst <= kTol);
        record("(i) sustain 0->0.8 in Decay", run, ref, kMorph200, kMorph200 + kWindow);
    }

    {   // (ii) sustain 0 -> 0.8 after Sustain is reached
        constexpr std::size_t kMorph3s = 144000;   // 3 s
        static_assert(kMorph3s % kBmcGrid == 0);
        const std::size_t total = kMorph3s + 9600;   // + 200 ms
        MorphState a = base;
        a.ampEnv.sustain = 0.0f;
        MorphState b = base;
        b.ampEnv.sustain = 0.8f;
        const BmcAdsrRun run = bmcRunAdsr(a, b, kMorph3s, total);
        const BmcAdsrRun ref = bmcRunAdsr(a, a, kMorph3s, total);
        double maxDs = 0.0;
        for (std::size_t i = 0; i < run.gridSample.size(); ++i) {
            if (run.gridSample[i] == kMorph3s)
                REQUIRE(run.stageBefore[i] == Krate::DSP::ADSRStage::Sustain);
            if (run.gridSample[i] >= kMorph3s)
                maxDs = std::max(maxDs, std::fabs(static_cast<double>(run.after[i][kBmcLeafSustain])
                                                  - static_cast<double>(run.before[i][kBmcLeafSustain])));
        }
        const double maxDg = bmcMaxStep(run.y, kMorph3s, total);
        std::cout << std::setprecision(6) << "FR-039 (ii): max per-sample |dg| " << maxDg
                  << ", max per-interval ds " << maxDs << ", final " << run.y.back() << "\n";
        INFO("FR-039 (ii)");
        REQUIRE(maxDs > 0.0);
        CHECK(maxDg < maxDs);
        CHECK(std::fabs(static_cast<double>(run.y.back()) - 0.8) < 1e-3);   // the new sustain was applied
        record("(ii) sustain 0->0.8 in Sustain", run, ref, kMorph3s, total);
    }

    {   // (iii) decayCurve 0.7 -> -0.7 during Decay
        MorphState a = base;
        a.ampEnv.decayCurve = 0.7f;
        MorphState b = base;
        b.ampEnv.decayCurve = -0.7f;
        const std::size_t total = kMorph200 + 2 * kWindow;
        const BmcAdsrRun run = bmcRunAdsr(a, b, kMorph200, total);
        const BmcAdsrRun ref = bmcRunAdsr(a, a, kMorph200, total);
        double worst = 0.0;
        std::size_t moving = 0;
        for (std::size_t i = 0; i < run.gridSample.size(); ++i) {
            if (run.gridSample[i] < kMorph200 || run.gridSample[i] >= kMorph200 + kWindow)
                continue;
            REQUIRE(run.stageBefore[i] == Krate::DSP::ADSRStage::Decay);
            // adsr_envelope.h:311-322: output = sT + (logStart - sT) * table(phase), logStart = peak = 1.
            const double range = 1.0 - static_cast<double>(run.after[i][kBmcLeafSustain]);
            const auto tNew = bmcDecayTable(run.after[i][kBmcLeafDecayCurve]);
            const auto tOld = bmcDecayTable(run.before[i][kBmcLeafDecayCurve]);
            const double expected = range
                                    * (static_cast<double>(Krate::DSP::lookupCurveTable(tNew, run.phase[i]))
                                       - static_cast<double>(Krate::DSP::lookupCurveTable(tOld, run.phase[i])));
            worst = std::max(worst, std::fabs(static_cast<double>(run.step[i]) - expected));
            if (std::fabs(expected) > 1e-3)
                ++moving;
        }
        std::cout << std::setprecision(6) << "FR-039 (iii): worst |step - range * dtable(phase)| " << worst << ", "
                  << moving << " moving intervals\n";
        INFO("FR-039 (iii)");
        CHECK(moving > 0);
        CHECK(worst <= kTol);
        record("(iii) decayCurve 0.7->-0.7 in Decay", run, ref, kMorph200, kMorph200 + kWindow);
    }
    bmcWriteEvidence("amp-envelope-immediate.txt", ev.str());
    std::cout << ev.str();

    {   // (iv) decayMs 50 -> 2000 during Decay is latched at stage entry
        // Deviation (run-log T009): A's 50 ms decay ends at ~60 ms, so a morph at 200 ms would land after the
        // Sustain entry and assert nothing; this case morphs at 32 ms (inside the Decay stage).
        constexpr std::size_t kMorph32 = 1536;
        static_assert(kMorph32 % kBmcGrid == 0);
        const std::size_t total = 4800;   // 100 ms
        MorphState a{};
        a.ampEnv.decayMs = 50.0f;
        MorphState b{};
        b.ampEnv.decayMs = 2000.0f;
        const BmcAdsrRun run = bmcRunAdsr(a, b, kMorph32, total);
        const BmcAdsrRun ref = bmcRunAdsr(a, a, kMorph32, total);
        REQUIRE(ref.sustainEntry != kBmcNoSample);
        bool changedInDecay = false;
        for (std::size_t i = 0; i < run.gridSample.size(); ++i) {
            if (run.stageBefore[i] == Krate::DSP::ADSRStage::Decay
                && !bmcSameBits(run.after[i][kBmcLeafDecayMs], run.before[i][kBmcLeafDecayMs]))
                changedInDecay = true;
        }
        REQUIRE(run.setterCalls[kBmcLeafDecayMs] > 1);
        REQUIRE(changedInDecay);   // the decayMs setter really fired while the stage was Decay
        std::size_t mismatches = 0;
        for (std::size_t n = 0; n <= ref.sustainEntry; ++n) {
            if (!bmcSameBits(run.y[n], ref.y[n]))
                ++mismatches;
        }
        std::cout << "FR-039 (iv): Sustain entry " << ref.sustainEntry << " (morphed " << run.sustainEntry << "), "
                  << mismatches << " mismatching samples before it\n";
        CHECK(mismatches == 0);
        CHECK(run.sustainEntry == ref.sustainEntry);
    }
}

// -----------------------------------------------------------------------------
// SC-024 (FR-061) discrete render: an endpoint edit of the selected algorithm mid-morph (plan C-10, C-12)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_EndpointEditMidMorphRender", "[systems][profundum][bass-morph][sc-024]") {
    enableFTZDAZ();
    // plan C-10 base: characterAmount 0.8, anchor.level 1.0, Tube; A = B = base.
    MorphState base{};
    base.characterAmount = 0.8f;
    base.anchor.level = 1.0f;
    REQUIRE(base.characterAlgorithm == Krate::DSP::BassCharacterStage::Algorithm::Tube);
    MorphState aPrime = base;
    aPrime.characterAlgorithm = Krate::DSP::BassCharacterStage::Algorithm::Fold;
    REQUIRE(Krate::DSP::isValidMorphState(base));
    REQUIRE(Krate::DSP::isValidMorphState(aPrime));

    constexpr std::size_t kEditAt = 48000;   // 1.0 s
    constexpr double kSeconds = 1.5;
    struct BmcNote {
        const char* name;
        double f0;
    };
    const std::array<BmcNote, 2> notes{{{"E1", bmcNoteHz(28)}, {"C3", bmcNoteHz(48)}}};
    for (const BmcNote& note : notes) {
        INFO("SC-024 " << note.name);
        const double f0 = note.f0;
        const auto mk = [&](const MorphState& a, const MorphState& b, float p) {
            Bmt::MorphChainConfig c = bmcSteady(a, b, BmMode::Blend, p, f0);
            c.seconds = kSeconds;
            return c;
        };
        Bmt::MorphChainConfig trCfg = mk(base, base, 0.3f);
        trCfg.automation.push_back(bmcEndpointEvent(kEditAt, BassMorphEngine::Endpoint::A, aPrime));
        Bmt::MorphChainConfig refCfg = mk(base, base, 0.0f);   // direct-setter reference: p held at 0
        refCfg.automation.push_back(bmcEndpointEvent(kEditAt, BassMorphEngine::Endpoint::A, aPrime));
        refCfg.automation.push_back(bmcEndpointEvent(kEditAt, BassMorphEngine::Endpoint::B, aPrime));
        const Bmt::MorphChainTaps tr = Bmt::renderMorphChain(trCfg);
        const Bmt::MorphChainTaps ref = Bmt::renderMorphChain(refCfg);
        const Bmt::MorphChainTaps refStart = Bmt::renderMorphChain(mk(base, base, 0.3f));
        const Bmt::MorphChainTaps refEnd = Bmt::renderMorphChain(mk(aPrime, aPrime, 0.3f));

        const auto algorithmCalls = [](const Bmt::MorphChainTaps& t) {
            std::vector<Bmt::MorphChainTaps::SetterCall> out;
            for (const auto& c : t.discreteSetterCalls) {
                if (c.leaf == Bmt::kLeafCharacterAlgorithm)
                    out.push_back(c);
            }
            return out;
        };
        const auto trCalls = algorithmCalls(tr);
        const auto refCalls = algorithmCalls(ref);
        REQUIRE(trCalls.size() == 1);
        REQUIRE(refCalls.size() == 1);
        const std::size_t gSw = trCalls[0].sample;
        CHECK(gSw == kEditAt);   // the first grid sample >= 1.0 s
        CHECK(trCalls[0].value == static_cast<std::uint8_t>(aPrime.characterAlgorithm));
        REQUIRE(refCalls[0].sample == gSw);
        // Selection A at p = 0.3: the edited endpoint's algorithm resolves within one interval.
        CHECK(tr.resolvedPerInterval[gSw / kBmcGrid].state.characterAlgorithm == aPrime.characterAlgorithm);

        const auto period = static_cast<std::size_t>(std::ceil(kBmcFs / f0));
        const std::size_t wEnd =
            gSw + Bmt::transitionEndSamples(Bmt::TransitionKind::CharacterAlgorithm, f0, kBmcFs) + period;
        const std::size_t n = tr.outL.size();
        REQUIRE(wEnd < n);
        const std::size_t len = wEnd - gSw;
        const std::size_t centre = gSw + len / 2;
        const std::size_t half = len - len / 2;

        // (a) one bound: max |dy| over W <= 10^(0.5/20) x the reference's.
        const double trL = Krate::DSP::ShapeCharacterTest::maxDeltaAround(tr.outL.data(), n, centre, half);
        const double trR = Krate::DSP::ShapeCharacterTest::maxDeltaAround(tr.outR.data(), n, centre, half);
        const double rfL = Krate::DSP::ShapeCharacterTest::maxDeltaAround(ref.outL.data(), n, centre, half);
        const double rfR = Krate::DSP::ShapeCharacterTest::maxDeltaAround(ref.outR.data(), n, centre, half);
        std::cout << std::setprecision(6) << "SC-024 " << note.name << ": g_sw " << gSw << ", window end " << wEnd
                  << ", max|dy| L " << trL << " (ref " << rfL << ", ratio " << (rfL > 0.0 ? trL / rfL : 0.0)
                  << "), R " << trR << " (ref " << rfR << ", ratio " << (rfR > 0.0 ? trR / rfR : 0.0) << ")\n";
        REQUIRE(rfL > 0.0);
        REQUIRE(rfR > 0.0);
        CHECK(trL <= kBmcStepBar * rfL);
        CHECK(trR <= kBmcStepBar * rfR);

        // (b) differential clicks over [g_sw - 50 ms, window end] against A static and A' static.
        const std::size_t lo = gSw - static_cast<std::size_t>(std::llround(0.050 * kBmcFs));
        const std::size_t clickLen = wEnd - lo + 1;
        const Krate::DSP::ShapeCharacterTest::ClickCount cL = Krate::DSP::ShapeCharacterTest::differentialClicks(
            tr.outL.data() + lo, refStart.outL.data() + lo, refEnd.outL.data() + lo, clickLen, kBmcFs, f0);
        const Krate::DSP::ShapeCharacterTest::ClickCount cR = Krate::DSP::ShapeCharacterTest::differentialClicks(
            tr.outR.data() + lo, refStart.outR.data() + lo, refEnd.outR.data() + lo, clickLen, kBmcFs, f0);
        std::cout << "SC-024 " << note.name << ": clicks L " << cL.counted << " (raw " << cL.transitionDetections
                  << ", guarded " << cL.guardedOut << "), R " << cR.counted << " (raw " << cR.transitionDetections
                  << ", guarded " << cR.guardedOut << ")\n";
        CHECK(cL.counted == 0);
        CHECK(cR.counted == 0);
    }
}

// -----------------------------------------------------------------------------
// SC-005(a)(d)(e) rendered (plan C-12: the engine clauses are T004's per-push BassMorph_AnchorCurves)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_AnchorCurvesRendered", "[systems][profundum][bass-morph][sc-005][long]") {
    enableFTZDAZ();
    std::vector<float> ps;
    for (int k = 0; k <= 20; ++k)
        ps.push_back(static_cast<float>(k) * 0.05f);
    ps.back() = 1.0f;
    bmcAnchorCurvesRendered(ps, "sc005-rendered.csv");
}

TEST_CASE("BassMorph_AnchorCurvesRenderedSmoke", "[systems][profundum][bass-morph][sc-005]") {
    enableFTZDAZ();
    bmcAnchorCurvesRendered({0.0f, 1.0f}, "sc005-rendered-smoke.csv");
}

// -----------------------------------------------------------------------------
// SC-006(b) A -> B -> A render check (SC-006(a) is T005's engine case)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_IdempotentRender", "[systems][profundum][bass-morph][sc-006][long]") {
    enableFTZDAZ();
    const std::array<BmMode, 3> modes{BmMode::Lock, BmMode::Blend, BmMode::Punch};
    bmcIdempotentRender(8, std::span<const BmMode>(modes.data(), modes.size()));
}

TEST_CASE("BassMorph_IdempotentRenderSmoke", "[systems][profundum][bass-morph][sc-006]") {
    enableFTZDAZ();
    const std::array<BmMode, 1> modes{BmMode::Blend};
    bmcIdempotentRender(1, std::span<const BmMode>(modes.data(), modes.size()));
}

// Hidden diagnostic (main loop, 2026-10-19): SC-006(b) residue vs settle time, pairs 2 and 6.
TEST_CASE("BassMorph_IdempotentSettleProbe", "[systems][profundum][bass-morph][.]") {
    enableFTZDAZ();
    bmcIdempotentSettleProbe();
    CHECK(true);
}
