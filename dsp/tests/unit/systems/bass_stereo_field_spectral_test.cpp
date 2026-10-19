// ==============================================================================
// Layer 3: System Tests - BassStereoField, spectral TU
//                          (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// Reference: specs/profundum-phase7-stereo-field/spec.md (SC-003, SC-005 record, SC-006, SC-010, SC-016;
//            FR-012, FR-021 - FR-024, FR-075, FR-076)
//            specs/profundum-phase7-stereo-field/plan.md (S7.4 phase, S12.1 LR4 bands, S12.5 group delay,
//            S13.3 per criterion, R-3; C-11 g_side = floor * (1 - G))
//            specs/profundum-phase7-stereo-field/tasks.md (T002 wires this TU, T010 fills it; deviations 3, 6, 10)
//
// T010:
//   BassStereoField_WidthMonotonic              [long][sc-003]  clean-core grid (SPREAD and WIDE) + WIDE dirty arm
//   BassStereoField_WidthMonotonicSmoke         [sc-003]        E1, Gravity 0.5, crossover 120, curve 0, SPREAD
//   BassStereoField_SafetyStagePhase            [long][sc-005]  the FR-023 record (phase, group delay, Q_band)
//   BassStereoField_SafetyStagePhaseSmoke       [sc-005]        one cell (deviation 6: every [long] has a Smoke)
//   BassStereoField_SafetyStageAttenuation      [long][sc-006]  A(B) >= 24 dB, third octaves, Gravity monotone
//   BassStereoField_SafetyStageAttenuationSmoke [sc-006]        (i) Tube E1 f_c 120 and (iii) at f_c 60
//   BassStereoField_LeversAudible               [long][sc-010]  FR-076: every lever moves a stereo descriptor
//   BassStereoField_LeversAudibleSmoke          [sc-010]        Spread and WIDE-vs-SPREAD at E1 (harvest-dependent)
//   BassStereoField_SampleRates                 [long][sc-016]  FR-075 at 44.1 / 48 / 88.2 / 96 / 192 kHz
//   BassStereoField_SampleRatesSmoke            [sc-016]        44.1 and 96 kHz, E1 W_n only
//   BassStereoField_SampleRatesReprepare        [sc-016]        per-push (deviation 10): prepare 48k -> 96k -> 48k
//
// Every render goes through StereoFieldTest::renderFieldChain (T007) unless the case says field-only. Every
// call into the other helper namespaces is qualified (tasks.md rule 6); TU-local helpers carry a bsf prefix.
// Evidence: specs/profundum-phase7-stereo-field/evidence/{sc003-width,sc005-phase,sc006-attenuation,
// sc010-levers,sc016-rates}.csv (the Smoke siblings write *-smoke.csv so they never clobber the [long] record).
// ==============================================================================

#include "bass_stereo_field_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/profundum_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::BassStereoField;
using BsfControls = Krate::DSP::BassStereoField::Controls;
using BsfMode = Krate::DSP::BassStereoField::ImageMode;
using BsfSource = Krate::DSP::BassStereoField::MotionSource;
using BsfAlg = Krate::DSP::BassCharacterStage::Algorithm;
using BsfPan = std::array<float, Krate::DSP::kMaxPartials>;
namespace Sft = Krate::DSP::StereoFieldTest;
namespace Mdt = Krate::DSP::MassDynamicsTest;
namespace Sct = Krate::DSP::ShapeCharacterTest;
namespace Pft = Krate::DSP::ProfundumTest;

constexpr double kBsfFs = 48000.0;
constexpr double kBsfTwoPi = 6.283185307179586476925286766559;
constexpr std::size_t kBsfHostBlock = 512;
constexpr std::size_t kBsfSpreadSteps = 11;          ///< Spread (and Gravity) i/10, i = 0 .. 10
constexpr double kBsfBandLoHz = 20.0;                ///< B = [20 Hz, f_c) (spec descriptor block)
constexpr double kBsfAttenuationBarDb = 24.0;        ///< SC-006 (ruled Q3)
constexpr double kBsfR3MarginDb = 0.1;               ///< plan R-3: print the per-bin table inside this margin
constexpr double kBsfWidthTolDb = 1e-3;              ///< SC-003(a)(b) float tolerance
constexpr double kBsfRhoTol = 1e-4;                  ///< SC-003(a)(b)
constexpr double kBsfS2mReadFloorDb = -100.0;        ///< SC-003(c), G8-1: a side-to-mid below this is float residue, not read
constexpr double kBsfWReadFloorDb = -100.0;          ///< SC-003(a)(b), SC-016, G10-3/G10-4: a W_n below this is float residue, not read
constexpr double kBsfGravityTolDb = 0.01;            ///< SC-003(c), SC-006 monotone
constexpr double kBsfGravityDropBarDb = 0.5;         ///< SC-003(c) at n f0 >= 5 f_c (drafted)
constexpr double kBsfDirtyTolDb = 0.5;               ///< SC-003 WIDE dirty arm (drafted, D-5)
constexpr double kBsfWideEqTolDb = 1e-6;             ///< SC-003: WIDE clean == SPREAD (FR-044)
constexpr double kBsfLeverS2mBarDb = 3.0;            ///< SC-010 static (drafted)
constexpr double kBsfLeverRhoBar = 0.1;              ///< SC-010 static (drafted)
constexpr double kBsfDepthStdBarDb = 1.0;            ///< SC-010 MOTION depth (drafted)
constexpr double kBsfRateRatioBar = 4.0;             ///< SC-010 MOTION rate (drafted)
constexpr double kBsfRateWDbBar = 0.25;              ///< SC-016 W_n
constexpr double kBsfRateAttenDbBar = 1.0;           ///< SC-016 A(B)
constexpr double kBsfOutputBound = 4.0;              ///< SC-016 |y| bound (tasks.md T010)
constexpr double kBsfMotionSeconds = 61.0;           ///< 1 s settle + 60 s of W_oct(t) (SC-010)

constexpr std::array<BsfAlg, 6> kBsfAlgorithms{BsfAlg::Tube, BsfAlg::Fold,  BsfAlg::Rectify,
                                               BsfAlg::Bite, BsfAlg::Crush, BsfAlg::Fracture};
constexpr std::array<const char*, 6> kBsfAlgNames{"Tube", "Fold", "Rectify", "Bite", "Crush", "Fracture"};
constexpr std::array<float, 3> kBsfCrossovers{60.0f, 120.0f, 250.0f};

struct BsfNote {
    const char* name;
    int midi;
};
constexpr BsfNote kBsfE0{.name = "E0", .midi = 16};
constexpr BsfNote kBsfE1{.name = "E1", .midi = 28};
constexpr BsfNote kBsfC2{.name = "C2", .midi = 36};
constexpr BsfNote kBsfE2{.name = "E2", .midi = 40};
constexpr BsfNote kBsfC3{.name = "C3", .midi = 48};
constexpr std::array<BsfNote, 4> kBsfAttenNotes{kBsfE0, kBsfE1, kBsfE2, kBsfC3};

/// tasks.md rule 9: 440 2^((m - 69)/12) in double via ShapeCharacterTest::noteHz.
double bsfHz(const BsfNote& n) { return Sct::noteHz(n.midi); }

float bsfStep(std::size_t i) { return static_cast<float>(i) / 10.0f; }

std::size_t bsfSettle(double fs) { return static_cast<std::size_t>(std::llround(Mdt::kSettleSeconds * fs)); }

/// Partials up to Nyquist (SC-003 "N up to Nyquist"), capped at the core's partial count.
int bsfHarmonicCount(double fs, double f0) {
    const auto nyq = static_cast<int>(std::floor(0.5 * fs / f0));
    return std::min(BassStereoField::kDefaultPartials, nyq);
}

std::string bsfFmt(double x) {
    std::ostringstream os;
    os << std::setprecision(6) << x;
    return os.str();
}

/// Comma-joined row, setprecision(6).
template <class... Ts>
std::string bsfRow(const Ts&... xs) {
    std::ostringstream os;
    os << std::setprecision(6);
    std::size_t i = 0;
    ((os << (i++ == 0 ? "" : ",") << xs), ...);
    return os.str();
}

std::string bsfEvidenceDir() {
    const std::string dir = std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase7-stereo-field/evidence/";
    std::filesystem::create_directories(dir);
    return dir;
}

/// One evidence CSV: every line goes to the file and to stdout (tasks.md Group 7 shared rules).
class BsfCsv {
public:
    BsfCsv(const std::string& file, const std::string& header) : out_(bsfEvidenceDir() + file, std::ios::trunc) {
        out_ << std::setprecision(6);
        std::cout << std::setprecision(6);
        line(header);
    }
    void line(const std::string& s) {
        out_ << s << '\n';
        std::cout << s << '\n';
    }

private:
    std::ofstream out_;
};

/// A monotonicity / bar tally: excess > 0 is a violation; the worst excess and where it was are kept.
struct BsfTally {
    std::string name;
    std::size_t checks = 0;
    std::size_t violations = 0;
    double worstExcess = -std::numeric_limits<double>::infinity();
    std::string worstWhere;

    template <class F>
    void add(double excess, const F& where) {
        ++checks;
        if (excess > 0.0)
            ++violations;
        if (excess > worstExcess) {
            worstExcess = excess;
            worstWhere = where();
        }
    }
};

void bsfReport(const BsfTally& t) {
    std::cout << "BSF_SPECTRAL,tally," << t.name << ",checks," << t.checks << ",violations," << t.violations
              << ",worst_excess," << bsfFmt(t.worstExcess) << ",at," << t.worstWhere << "\n";
    INFO(t.name << ": " << t.violations << " of " << t.checks << " checks over the bar; worst excess "
                << bsfFmt(t.worstExcess) << " at " << t.worstWhere);
    CHECK(t.checks > 0);
    CHECK(t.violations == 0);
}

/// G10-1 (ruled 2026-10-19): a tally that is recorded, not gated (SC-003's WIDE dirty-arm octave side-to-mid:
/// random-phase cross products of a per-channel nonlinearity after the parity pan are not monotone in anything).
void bsfRecord(const BsfTally& t) {
    std::cout << "BSF_SPECTRAL,tally-recorded," << t.name << ",checks," << t.checks << ",over_bar," << t.violations
              << ",worst_excess," << bsfFmt(t.worstExcess) << ",at," << t.worstWhere << " (G10-1: recorded)\n";
    CHECK(t.checks > 0);
}

// -----------------------------------------------------------------------------
// Fixtures (tasks.md rule 10)
// -----------------------------------------------------------------------------

BsfControls bsfControls(BsfMode mode, float spread, float gravity, float crossoverHz, float curve) {
    BsfControls c{};
    c.mode = mode;
    c.spread = spread;
    c.gravity = gravity;
    c.crossoverHz = crossoverHz;
    c.curve = curve;
    return c;
}

/// Clean core: kSawAnchor, excitation off, no SHAPE, no character, no Anchor.
Sft::FieldChainConfig bsfCleanConfig(double f0, double fs, const BsfControls& c, double seconds = 0.0) {
    Sft::FieldChainConfig cfg{};
    cfg.chain = Mdt::ChainConfig{.base = Krate::DSP::SpectralShapeRecipe::kSawAnchor,
                                 .f0 = f0,
                                 .fs = fs,
                                 .seconds = seconds,
                                 .hostBlock = kBsfHostBlock,
                                 .attachAnchor = false};
    cfg.field = c;
    cfg.shapeOn = false;
    return cfg;
}

/// Dirty arm: SHAPE 1 (BITE) -> Fold 100 -> the character stage at `a`, Amount 1, Tone 0.5; kSawAnchor, no Anchor.
Sft::FieldChainConfig bsfDirtyConfig(BsfAlg a, double f0, double fs, const BsfControls& c, double seconds = 0.0) {
    Sft::FieldChainConfig cfg = bsfCleanConfig(f0, fs, c, seconds);
    cfg.shapeOn = true;
    cfg.shape = 1.0f;
    cfg.foldAmount = 100.0f;
    cfg.algorithm = a;
    cfg.characterAmount = 1.0f;
    cfg.characterTone = 0.5f;
    return cfg;
}

/// Octave bands below f_c (down to 20 Hz) then StereoFieldTest::octaveBands(f_c) (f_c .. 10 kHz), ascending.
/// SC-003(c) reads the side-to-mid ratio "below or above f_c", so one band set serves (a) - (c).
std::vector<Sft::Band> bsfOctaves(double fc) {
    std::vector<Sft::Band> bands;
    for (double hi = fc; hi > kBsfBandLoHz + 1e-9; hi *= 0.5)
        bands.push_back(Sft::Band{.lo = std::max(0.5 * hi, kBsfBandLoHz), .hi = hi});
    std::reverse(bands.begin(), bands.end());
    const std::vector<Sft::Band> above = Sft::octaveBands(fc);
    bands.insert(bands.end(), above.begin(), above.end());
    return bands;
}

std::string bsfList(const std::vector<double>& v) {
    std::ostringstream os;
    os << std::setprecision(6);
    for (std::size_t i = 0; i < v.size(); ++i)
        os << (i == 0 ? "" : ";") << v[i];
    return os.str();
}

enum class BsfBandField : std::uint8_t { Rho, SideToMid };

std::string bsfBandList(const std::vector<Sft::BandReading>& r, BsfBandField f) {
    std::ostringstream os;
    os << std::setprecision(6);
    for (std::size_t i = 0; i < r.size(); ++i) {
        os << (i == 0 ? "" : ";");
        if (!r[i].valid) {
            os << "na";
            continue;
        }
        os << (f == BsfBandField::Rho ? r[i].rho : r[i].sideToMidDb);
    }
    return os.str();
}

/// Seeded uniform [-0.5, 0.5) from the top 24 bits (portable across standard libraries).
float bsfUniform(std::mt19937& rng) {
    const auto bits = static_cast<std::uint32_t>(rng() >> 8);
    return static_cast<float>(bits) * (1.0f / 16777216.0f) - 0.5f;
}

/// Field-only: a fresh field at `c`, prepared and reset at fs, advanceControl() at every multiple of 32
/// (MassDynamicsTest::gridDriver), in place on copies of L / R.
struct BsfStageRun {
    std::vector<float> L, R;
    float sideGain = 0.0f;   ///< currentSideGain() after reset (the snapped g_side; plan S1 C-11)
};

BsfStageRun bsfRunStage(std::vector<float> L, std::vector<float> R, const BsfControls& c, double fs) {
    REQUIRE(L.size() == R.size());
    auto f = std::make_unique<BassStereoField>();
    f->setControls(c);
    f->prepare(fs, kBsfHostBlock);
    (*f).reset();   // the field's reset, not the smart pointer's
    BsfStageRun run;
    run.sideGain = f->currentSideGain();
    Mdt::gridDriver(L.size(), kBsfHostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        if (atGrid) {
            f->setControls(c);
            f->advanceControl();
        }
        f->processBlock(L.data() + start, R.data() + start, len);
    });
    run.L = std::move(L);
    run.R = std::move(R);
    return run;
}

// -----------------------------------------------------------------------------
// SC-003 width readings
// -----------------------------------------------------------------------------

struct BsfTapWidth {
    std::vector<double> w;                  ///< W_n, n = 1 .. N (StereoFieldTest::widthDb, floored at -120 dB)
    std::vector<Sft::BandReading> oct;      ///< bsfOctaves(f_c)
};

struct BsfWidthReading {
    BsfTapWidth core;    ///< coreL/R: the generator output
    BsfTapWidth stage;   ///< stageOutL/R: the safety-stage output
};

BsfTapWidth bsfTapWidth(const std::vector<float>& L, const std::vector<float>& R, double fs, double f0, int N,
                        const std::vector<Sft::Band>& bands) {
    const std::size_t s = bsfSettle(fs);
    REQUIRE(L.size() == R.size());
    REQUIRE(L.size() >= s + Krate::DSP::TestUtils::kLowFrequencyFftSize);
    BsfTapWidth t;
    t.w = Sft::widthDb(L.data() + s, R.data() + s, fs, f0, N);
    t.oct = Sft::bandReadings(L.data() + s, R.data() + s, L.size() - s, fs, bands);
    return t;
}

BsfWidthReading bsfReadWidth(const Sft::FieldChainTaps& t, double fs, double f0, int N,
                             const std::vector<Sft::Band>& bands) {
    return BsfWidthReading{.core = bsfTapWidth(t.coreL, t.coreR, fs, f0, N, bands),
                           .stage = bsfTapWidth(t.stageOutL, t.stageOutR, fs, f0, N, bands)};
}

struct BsfWidthTallies {
    BsfTally aW{.name = "sc003a_W_nondecreasing_in_n"};
    BsfTally aRho{.name = "sc003a_octave_rho_nonincreasing_in_band"};
    BsfTally bW{.name = "sc003b_W_nondecreasing_in_spread"};
    BsfTally bRho{.name = "sc003b_octave_rho_nonincreasing_in_spread"};
    BsfTally cW{.name = "sc003c_gravity_never_raises_W"};
    BsfTally cS2m{.name = "sc003c_gravity_never_raises_octave_s2m"};
    BsfTally cDrop{.name = "sc003c_gravity_drop_le_0.5dB_above_5fc"};
    BsfTally dFloor{.name = "sc003d_spread0_W_at_floor"};
    BsfTally wideEq{.name = "sc003_wide_clean_W_equals_spread"};
    BsfTally dirtyBand{.name = "sc003_wide_dirty_s2m_nondecreasing_in_band"};
    BsfTally dirtySpread{.name = "sc003_wide_dirty_s2m_nondecreasing_in_spread"};
};

std::string bsfWidthCsvHeader() {
    return "arm,note,fc_hz,curve,gravity,spread,mode,tap,band_lo_list,w_db_list,rho_list,s2m_db_list";
}

std::string bsfBandLoList(const std::vector<Sft::Band>& bands) {
    std::vector<double> lo;
    lo.reserve(bands.size());
    for (const Sft::Band& b : bands)
        lo.push_back(b.lo);
    return bsfList(lo);
}

void bsfWidthRows(BsfCsv& csv, const char* arm, const BsfNote& note, float fc, float curve, float gravity,
                  float spread, const char* mode, const BsfWidthReading& r, const std::vector<Sft::Band>& bands) {
    const std::string lo = bsfBandLoList(bands);
    csv.line(bsfRow(arm, note.name, fc, curve, gravity, spread, mode, "core", lo, bsfList(r.core.w),
                    bsfBandList(r.core.oct, BsfBandField::Rho), bsfBandList(r.core.oct, BsfBandField::SideToMid)));
    csv.line(bsfRow(arm, note.name, fc, curve, gravity, spread, mode, "stageOut", lo, bsfList(r.stage.w),
                    bsfBandList(r.stage.oct, BsfBandField::Rho), bsfBandList(r.stage.oct, BsfBandField::SideToMid)));
}

/// SC-003(a): W_n non-decreasing in n f0 (1e-3 dB) and the octave rho non-increasing in band centre (1e-4).
void bsfCheckA(const BsfTapWidth& tw, const std::vector<Sft::Band>& bands, double maxHz, const std::string& where,
               BsfWidthTallies& t) {
    for (std::size_t n = 1; n < tw.w.size(); ++n) {
        if (tw.w[n - 1] < kBsfWReadFloorDb && tw.w[n] < kBsfWReadFloorDb)   // G10-3: float residue, not read
            continue;
        t.aW.add(tw.w[n - 1] - tw.w[n] - kBsfWidthTolDb, [&] { return where + ";n=" + std::to_string(n + 1); });
    }
    std::optional<std::size_t> prev;
    for (std::size_t b = 0; b < tw.oct.size(); ++b) {
        // G10-3: a band truncated by the partial cap is not an octave band.
        if (!tw.oct[b].valid || bands[b].hi > maxHz)
            continue;
        if (prev.has_value()) {
            const std::size_t p = *prev;
            t.aRho.add(tw.oct[b].rho - tw.oct[p].rho - kBsfRhoTol,
                       [&] { return where + ";band_lo=" + bsfFmt(bands[b].lo); });
        }
        prev = b;
    }
}

/// SC-003(b): from Spread step `lo` to the next step `hi`: W_n never falls, the octave rho never rises.
void bsfCheckB(const BsfTapWidth& lo, const BsfTapWidth& hi, const std::vector<Sft::Band>& bands, double maxHz,
               const std::string& where, BsfWidthTallies& t) {
    REQUIRE(lo.w.size() == hi.w.size());
    for (std::size_t n = 0; n < lo.w.size(); ++n) {
        if (lo.w[n] < kBsfWReadFloorDb && hi.w[n] < kBsfWReadFloorDb)   // G10-3
            continue;
        t.bW.add(lo.w[n] - hi.w[n] - kBsfWidthTolDb, [&] { return where + ";n=" + std::to_string(n + 1); });
    }
    for (std::size_t b = 0; b < lo.oct.size(); ++b) {
        if (lo.oct[b].valid && hi.oct[b].valid && bands[b].hi <= maxHz)
            t.bRho.add(hi.oct[b].rho - lo.oct[b].rho - kBsfRhoTol,
                       [&] { return where + ";band_lo=" + bsfFmt(bands[b].lo); });
    }
}

/// SC-003(c): from Gravity `gLo` to the higher Gravity `gHi`: W_n and the octave side-to-mid never rise (0.01 dB).
void bsfCheckC(const BsfTapWidth& gLo, const BsfTapWidth& gHi, const std::vector<Sft::Band>& bands, double maxHz,
               const std::string& where, BsfWidthTallies& t) {
    REQUIRE(gLo.w.size() == gHi.w.size());
    for (std::size_t n = 0; n < gLo.w.size(); ++n) {
        if (gLo.w[n] < kBsfWReadFloorDb && gHi.w[n] < kBsfWReadFloorDb)   // G10-3
            continue;
        t.cW.add(gHi.w[n] - gLo.w[n] - kBsfGravityTolDb, [&] { return where + ";n=" + std::to_string(n + 1); });
    }
    for (std::size_t b = 0; b < gLo.oct.size(); ++b) {
        // G8-1 refinement: at the harvested floor a centred band's side-to-mid is about -116 dB (float residue of
        // the M/S arithmetic) and wanders by 0.1 dB with Gravity; the ratio is read only above kBsfS2mReadFloorDb.
        const bool readable = gLo.oct[b].sideToMidDb >= kBsfS2mReadFloorDb || gHi.oct[b].sideToMidDb >= kBsfS2mReadFloorDb;
        if (gLo.oct[b].valid && gHi.oct[b].valid && readable && bands[b].hi <= maxHz)
            t.cS2m.add(gHi.oct[b].sideToMidDb - gLo.oct[b].sideToMidDb - kBsfGravityTolDb,
                       [&] { return where + ";band_lo=" + bsfFmt(bands[b].lo); });
    }
}

/// SC-003(c): at n f0 >= 5 f_c (= 2.5 f_s) Gravity 0 -> 1 lowers W_n by <= 0.5 dB.
void bsfCheckDrop(const BsfTapWidth& g0, const BsfTapWidth& g1, double f0, float fc, const std::string& where,
                  BsfWidthTallies& t) {
    REQUIRE(g0.w.size() == g1.w.size());
    for (std::size_t i = 0; i < g0.w.size(); ++i) {
        const double hz = static_cast<double>(i + 1) * f0;
        if (hz >= 5.0 * static_cast<double>(fc))
            t.cDrop.add(g0.w[i] - g1.w[i] - kBsfGravityDropBarDb,
                        [&] { return where + ";n=" + std::to_string(i + 1); });
    }
}

/// The clean-core SC-003 grid of one (note, f_c, curve): readings[g][i] for the requested Spread steps of each
/// Gravity row; (a), (d) and the WIDE == SPREAD equality per render, then (b) along each Gravity row and (c)
/// across Gravity at each Spread step present in more than one row.
void bsfRunWidthGrid(const BsfNote& note, float fc, float curve, const std::vector<float>& gravities,
                     const std::vector<std::vector<std::size_t>>& steps, bool withWide, BsfCsv& csv,
                     BsfWidthTallies& t) {
    REQUIRE(gravities.size() == steps.size());
    REQUIRE(!gravities.empty());
    const double fs = kBsfFs;
    const double f0 = bsfHz(note);
    const int N = bsfHarmonicCount(fs, f0);
    const std::vector<Sft::Band> bands = bsfOctaves(static_cast<double>(fc));
    const double maxHz = static_cast<double>(N) * f0;   // G10-3: the partial range
    std::vector<std::vector<BsfWidthReading>> rd(gravities.size(), std::vector<BsfWidthReading>(kBsfSpreadSteps));
    std::vector<std::vector<bool>> have(gravities.size(), std::vector<bool>(kBsfSpreadSteps, false));
    const std::string cellWhere = std::string(note.name) + ";fc=" + bsfFmt(static_cast<double>(fc)) +
                                  ";curve=" + bsfFmt(static_cast<double>(curve));

    for (std::size_t gi = 0; gi < gravities.size(); ++gi) {
        const float gravity = gravities[gi];
        for (const std::size_t i : steps[gi]) {
            REQUIRE(i < kBsfSpreadSteps);
            const float spread = bsfStep(i);
            const std::string where = cellWhere + ";G=" + bsfFmt(static_cast<double>(gravity)) +
                                      ";spread=" + bsfFmt(static_cast<double>(spread));
            BsfWidthReading sp;
            {
                const Sft::FieldChainTaps taps = Sft::renderFieldChain(
                    bsfCleanConfig(f0, fs, bsfControls(BsfMode::Spread, spread, gravity, fc, curve)));
                sp = bsfReadWidth(taps, fs, f0, N, bands);
                if (i == 0) {
                    // SC-003(d): Spread 0 -> L == R bit-identical at both taps.
                    Pft::requireLREqual(taps.coreL.data(), taps.coreR.data(), taps.coreL.size());
                    Pft::requireLREqual(taps.stageOutL.data(), taps.stageOutR.data(), taps.stageOutL.size());
                }
            }
            bsfWidthRows(csv, "clean", note, fc, curve, gravity, spread, "Spread", sp, bands);
            bsfCheckA(sp.core, bands, maxHz, where + ";tap=core", t);
            bsfCheckA(sp.stage, bands, maxHz, where + ";tap=stageOut", t);
            if (i == 0) {
                for (std::size_t n = 0; n < sp.core.w.size(); ++n) {
                    t.dFloor.add(sp.core.w[n] - Sft::kFloorDb - 1e-9,
                                 [&] { return where + ";tap=core;n=" + std::to_string(n + 1); });
                    t.dFloor.add(sp.stage.w[n] - Sft::kFloorDb - 1e-9,
                                 [&] { return where + ";tap=stageOut;n=" + std::to_string(n + 1); });
                }
            }
            if (withWide) {
                // FR-044: WIDE hands SPREAD's vector; the clean core has no character stage, so W_n is equal.
                const Sft::FieldChainTaps wt = Sft::renderFieldChain(
                    bsfCleanConfig(f0, fs, bsfControls(BsfMode::Wide, spread, gravity, fc, curve)));
                const BsfWidthReading wr = bsfReadWidth(wt, fs, f0, N, bands);
                bsfWidthRows(csv, "clean", note, fc, curve, gravity, spread, "Wide", wr, bands);
                for (std::size_t n = 0; n < sp.core.w.size(); ++n) {
                    t.wideEq.add(std::abs(wr.core.w[n] - sp.core.w[n]) - kBsfWideEqTolDb,
                                 [&] { return where + ";tap=core;n=" + std::to_string(n + 1); });
                    t.wideEq.add(std::abs(wr.stage.w[n] - sp.stage.w[n]) - kBsfWideEqTolDb,
                                 [&] { return where + ";tap=stageOut;n=" + std::to_string(n + 1); });
                }
            }
            rd[gi][i] = std::move(sp);
            have[gi][i] = true;
        }
    }

    // (b) along each Gravity row.
    for (std::size_t gi = 0; gi < gravities.size(); ++gi) {
        std::optional<std::size_t> prev;
        for (std::size_t i = 0; i < kBsfSpreadSteps; ++i) {
            if (!have[gi][i])
                continue;
            if (prev.has_value()) {
                const std::size_t p = *prev;
                const std::string where = cellWhere + ";G=" + bsfFmt(static_cast<double>(gravities[gi])) +
                                          ";spread_step=" + std::to_string(p) + "->" + std::to_string(i);
                bsfCheckB(rd[gi][p].core, rd[gi][i].core, bands, maxHz, where + ";tap=core", t);
                bsfCheckB(rd[gi][p].stage, rd[gi][i].stage, bands, maxHz, where + ";tap=stageOut", t);
            }
            prev = i;
        }
    }

    // (c) across Gravity at each Spread step (gravities ascending).
    const std::size_t last = gravities.size() - 1;
    for (std::size_t i = 0; i < kBsfSpreadSteps; ++i) {
        std::optional<std::size_t> prev;
        for (std::size_t gi = 0; gi < gravities.size(); ++gi) {
            if (!have[gi][i])
                continue;
            if (prev.has_value()) {
                const std::size_t p = *prev;
                REQUIRE(gravities[p] < gravities[gi]);
                const std::string where = cellWhere + ";spread=" + bsfFmt(static_cast<double>(bsfStep(i))) +
                                          ";G=" + bsfFmt(static_cast<double>(gravities[p])) + "->" +
                                          bsfFmt(static_cast<double>(gravities[gi]));
                bsfCheckC(rd[p][i].core, rd[gi][i].core, bands, maxHz, where + ";tap=core", t);
                bsfCheckC(rd[p][i].stage, rd[gi][i].stage, bands, maxHz, where + ";tap=stageOut", t);
            }
            prev = gi;
        }
        if (gravities.front() == 0.0f && gravities[last] == 1.0f && have[0][i] && have[last][i]) {
            const std::string where = cellWhere + ";spread=" + bsfFmt(static_cast<double>(bsfStep(i)));
            bsfCheckDrop(rd[0][i].core, rd[last][i].core, f0, fc, where + ";tap=core", t);
            bsfCheckDrop(rd[0][i].stage, rd[last][i].stage, f0, fc, where + ";tap=stageOut", t);
        }
    }
}

/// SC-003 WIDE dirty arm at stageOutL/R: offsets at the ceilings (Spread * committed K_wide), the octave
/// side-to-mid non-decreasing in band centre and over the 11 Spread steps, each within 0.5 dB. The other
/// controls stay at Controls{} (Gravity 0.5, crossover 120, curve 0.5; run-log decision).
void bsfRunWideDirty(const BsfNote& note, BsfAlg a, const char* algName, BsfCsv& csv, BsfWidthTallies& t) {
    const double fs = kBsfFs;
    const double f0 = bsfHz(note);
    const BsfControls base{};
    const std::vector<Sft::Band> bands = bsfOctaves(static_cast<double>(base.crossoverHz));
    const std::string lo = bsfBandLoList(bands);
    const std::size_t s = bsfSettle(fs);
    std::vector<Sft::BandReading> prev;
    for (std::size_t i = 0; i < kBsfSpreadSteps; ++i) {
        BsfControls c = base;
        c.mode = BsfMode::Wide;
        c.spread = bsfStep(i);
        const Sft::FieldChainTaps taps = Sft::renderFieldChain(bsfDirtyConfig(a, f0, fs, c));
        REQUIRE(!taps.offsetsPerInterval.empty());
        // The offsets handed are the ceilings scaled by Spread and the committed K_wide (FR-041, plan S6).
        const float expectK = BassStereoField::wideCeiling(a) * c.spread * BassStereoField::kWideOffsetScale;
        {
            INFO("dirty " << algName << " " << note.name << " spread " << c.spread << " offset "
                          << taps.offsetsPerInterval.back().amount << " expected " << expectK);
            CHECK(std::abs(taps.offsetsPerInterval.back().amount - expectK) <= 1e-6f);
        }
        const std::vector<Sft::BandReading> rs = Sft::bandReadings(
            taps.stageOutL.data() + s, taps.stageOutR.data() + s, taps.stageOutL.size() - s, fs, bands);
        csv.line(bsfRow("dirty", note.name, base.crossoverHz, base.curve, base.gravity, c.spread,
                        std::string("Wide-") + algName, "stageOut", lo, "na", bsfBandList(rs, BsfBandField::Rho),
                        bsfBandList(rs, BsfBandField::SideToMid)));
        const std::string where =
            std::string(note.name) + ";" + algName + ";spread=" + bsfFmt(static_cast<double>(c.spread));
        std::optional<std::size_t> pb;
        for (std::size_t b = 0; b < rs.size(); ++b) {
            if (!rs[b].valid)
                continue;
            if (pb.has_value()) {
                const std::size_t p = *pb;
                t.dirtyBand.add(rs[p].sideToMidDb - rs[b].sideToMidDb - kBsfDirtyTolDb,
                                [&] { return where + ";band_lo=" + bsfFmt(bands[b].lo); });
            }
            pb = b;
        }
        if (!prev.empty()) {
            for (std::size_t b = 0; b < rs.size(); ++b) {
                if (prev[b].valid && rs[b].valid)
                    t.dirtySpread.add(prev[b].sideToMidDb - rs[b].sideToMidDb - kBsfDirtyTolDb,
                                      [&] { return where + ";band_lo=" + bsfFmt(bands[b].lo); });
            }
        }
        prev = rs;
    }
}

// -----------------------------------------------------------------------------
// SC-005 phase record (plan S7.4)
// -----------------------------------------------------------------------------

/// Single-frequency DFT of a response, sum_n h[n] e^{-j 2 pi hz n / fs}.
std::complex<double> bsfDft(const std::vector<double>& h, double fs, double hz) {
    const double w = kBsfTwoPi * hz / fs;
    double re = 0.0;
    double im = 0.0;
    for (std::size_t n = 0; n < h.size(); ++n) {
        const double ph = w * static_cast<double>(n);
        re += h[n] * std::cos(ph);
        im -= h[n] * std::sin(ph);
    }
    return {re, im};
}

/// Plan S7.4: S'/S = g LP4 + HP4 = (g + s^4) / D^2, D = s^2 + sqrt(2) s + 1, s = j f / f_s (analog prototype).
std::complex<double> bsfAnalyticSide(double hz, double fSplit, double g) {
    const std::complex<double> s(0.0, hz / fSplit);
    const std::complex<double> d = s * s + std::sqrt(2.0) * s + 1.0;
    const std::complex<double> s2 = s * s;
    return (g + s2 * s2) / (d * d);
}

/// -d(phase)/d(omega) by a centred difference at hz (+-0.1 %).
template <class H>
double bsfGroupDelaySeconds(const H& h, double hz) {
    const double d = 1e-3 * hz;
    const std::complex<double> a = h(hz - d);
    const std::complex<double> b = h(hz + d);
    return -std::arg(b / a) / (kBsfTwoPi * 2.0 * d);
}

std::string bsfPhaseCsvHeader() {
    return "kind,fc_hz,gravity,g_side,f_hz,meas_mag_db,meas_phase_rad,analytic_mag_db,analytic_phase_rad,"
           "m_mag,m_phase_rad";
}

/// SC-005 (FR-023) record for one (f_c, Gravity): the impulse responses of a pure-S (L = d, R = -d) and a
/// pure-M (L = R = d) input, 2^16 samples, field-only; S_out / S_in and M_out / M_in at 1/12-octave points
/// 20 Hz .. 10 kHz beside the analytic S7.4 values; the S group delay at f_s beside 2 sqrt(2) / (2 pi f_s);
/// optionally Q_band per octave on seeded stereo noise with rho = 0.5. Gated: finite, |M_out / M_in| within
/// 1e-6 of 1.
void bsfPhaseCell(float fc, float gravity, BsfCsv& csv, bool withNoise) {
    const double fs = kBsfFs;
    constexpr std::size_t kLen = std::size_t{1} << 16;
    const BsfControls c = bsfControls(BsfMode::Spread, 1.0f, gravity, fc, BsfControls{}.curve);
    static_assert(BassStereoField::latencySamples() == 0, "FR-001: zero latency (SC-005 gate)");

    std::vector<float> sL(kLen, 0.0f);
    std::vector<float> sR(kLen, 0.0f);
    sL[0] = 1.0f;
    sR[0] = -1.0f;
    std::vector<float> mL(kLen, 0.0f);
    std::vector<float> mR(kLen, 0.0f);
    mL[0] = 1.0f;
    mR[0] = 1.0f;
    const BsfStageRun sRun = bsfRunStage(sL, sR, c, fs);
    const BsfStageRun mRun = bsfRunStage(mL, mR, c, fs);

    std::vector<double> hS(kLen, 0.0);
    std::vector<double> hM(kLen, 0.0);
    bool finite = true;
    for (std::size_t i = 0; i < kLen; ++i) {
        if (!Krate::DSP::detail::isFinite(sRun.L[i]) || !Krate::DSP::detail::isFinite(sRun.R[i]) ||
            !Krate::DSP::detail::isFinite(mRun.L[i]) || !Krate::DSP::detail::isFinite(mRun.R[i]))
            finite = false;
        hS[i] = 0.5 * (static_cast<double>(sRun.L[i]) - static_cast<double>(sRun.R[i]));
        hM[i] = 0.5 * (static_cast<double>(mRun.L[i]) + static_cast<double>(mRun.R[i]));
    }
    INFO("fc " << fc << " gravity " << gravity);
    REQUIRE(finite);

    // plan S1 C-11: g_side = g_floor * (1 - G), the snapped smoother value after reset.
    const auto g = static_cast<double>(sRun.sideGain);
    const double fSplit = static_cast<double>(BassStereoField::kStageCrossoverMultiple) * static_cast<double>(fc);
    double worstM = 0.0;
    for (int i = 0;; ++i) {
        const double hz = kBsfBandLoHz * std::exp2(static_cast<double>(i) / 12.0);
        if (hz > 10000.0)
            break;
        const std::complex<double> hs = bsfDft(hS, fs, hz);
        const std::complex<double> hm = bsfDft(hM, fs, hz);
        const std::complex<double> ha = bsfAnalyticSide(hz, fSplit, g);
        REQUIRE(Krate::DSP::detail::isFinite(std::abs(hs)));
        REQUIRE(Krate::DSP::detail::isFinite(std::abs(hm)));
        worstM = std::max(worstM, std::abs(std::abs(hm) - 1.0));
        csv.line(bsfRow("phase", fc, gravity, g, hz, Pft::safeDb10(std::norm(hs)), Sft::wrapPhase(std::arg(hs)),
                        Pft::safeDb10(std::norm(ha)), Sft::wrapPhase(std::arg(ha)), std::abs(hm),
                        Sft::wrapPhase(std::arg(hm))));
    }
    {
        INFO("worst |M_out/M_in| - 1 = " << worstM);
        CHECK(worstM <= 1e-6);
    }

    const double gdMeas = bsfGroupDelaySeconds([&](double hz) { return bsfDft(hS, fs, hz); }, fSplit);
    const double gdModel = bsfGroupDelaySeconds([&](double hz) { return bsfAnalyticSide(hz, fSplit, g); }, fSplit);
    const double gdAllPass = 2.0 * std::sqrt(2.0) / (kBsfTwoPi * fSplit);   // S7.4 / S12.5, g = 1
    csv.line(bsfRow("group_delay_ms", fc, gravity, g, fSplit, gdMeas * 1000.0, "na", gdModel * 1000.0, "na",
                    gdAllPass * 1000.0, "na"));
    std::cout << "BSF_SC005,group_delay,fc," << fc << ",gravity," << gravity << ",g_side," << g << ",f_s," << fSplit
              << ",measured_ms," << gdMeas * 1000.0 << ",analytic_g_ms," << gdModel * 1000.0
              << ",analytic_allpass_g1_ms," << gdAllPass * 1000.0 << "\n";

    if (withNoise) {
        // Q_band per octave on seeded stereo noise with rho = 0.5: L = a, R = 0.5 a + 0.866 b.
        const auto settle = static_cast<std::size_t>(fs);
        const std::size_t n = settle + (std::size_t{1} << 18);
        std::mt19937 rng{0x5EEDu};
        std::vector<float> L(n, 0.0f);
        std::vector<float> R(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            const float na = bsfUniform(rng);
            const float nb = bsfUniform(rng);
            L[i] = na;
            R[i] = 0.5f * na + 0.866f * nb;
        }
        const BsfStageRun run = bsfRunStage(L, R, c, fs);
        const std::vector<Sft::Band> bands = Sft::octaveBands(kBsfBandLoHz);
        const std::vector<Sft::BandReading> in =
            Sft::bandReadings(L.data() + settle, R.data() + settle, n - settle, fs, bands);
        const std::vector<Sft::BandReading> out =
            Sft::bandReadings(run.L.data() + settle, run.R.data() + settle, n - settle, fs, bands);
        // Columns reused: f_hz = band lo, meas = (Q_in, rho_in), analytic = (Q_out, rho_out), m_mag = band hi.
        for (std::size_t b = 0; b < bands.size(); ++b) {
            csv.line(bsfRow("q_band_noise_rho0.5", fc, gravity, g, bands[b].lo, in[b].q, in[b].rho, out[b].q,
                            out[b].rho, bands[b].hi, "na"));
        }
    }
}

// -----------------------------------------------------------------------------
// SC-006 attenuation (spec descriptor block "Distortion-born side content")
// -----------------------------------------------------------------------------

struct BsfSideBand {
    double inDb = 0.0;      ///< D_side(B) at the stage input (dynL/R), dBFS
    double outDb = 0.0;     ///< D_side'(B) at the stage output (stageOutL/R), dBFS
    double attenDb = 0.0;   ///< A(B) = D_side - D_side'
    bool valid = false;     ///< D_side(B) >= -100 dBFS
};

/// Side mean square ((L - R)/2)^2 over [lo, hi) of one Hann-tapered window (Parseval, deviation 8).
double bsfSideMeanSquare(const Sft::BandSpectra& s, double fs, double lo, double hi) {
    const Sft::BandEnergies e = Sft::bandEnergiesFromSpectra(s, fs, lo, hi);
    return e.sumW2 > 0.0 ? 0.25 * std::max(e.ll + e.rr - 2.0 * e.lr, 0.0) / e.sumW2 : 0.0;
}

BsfSideBand bsfSideBand(const Sft::BandSpectra& in, const Sft::BandSpectra& out, double fs, double lo, double hi) {
    const double a = bsfSideMeanSquare(in, fs, lo, hi);
    const double b = bsfSideMeanSquare(out, fs, lo, hi);
    BsfSideBand r;
    r.inDb = Pft::safeDb10(a);
    r.outDb = Pft::safeDb10(b);
    r.attenDb = r.inDb - r.outDb;
    r.valid = a >= Sft::kValidMeanEnergy;
    return r;
}

/// Plan R-3: the per-bin side table of [lo, hi) (bins within 60 dB of the band's strongest input bin).
void bsfPrintPerBin(const Sft::BandSpectra& in, const Sft::BandSpectra& out, double fs, double lo, double hi,
                    const std::string& label) {
    const std::size_t W = in.length;
    double peak = 0.0;
    for (std::size_t k = 0; k <= W / 2; ++k) {
        const double f = static_cast<double>(k) * fs / static_cast<double>(W);
        if (f >= lo && f < hi)
            peak = std::max(peak, 0.25 * std::norm(in.L[k] - in.R[k]));
    }
    std::cout << "BSF_SC006_R3,per-bin table," << label << ",bin_hz,side_in_db,side_out_db,atten_db\n";
    for (std::size_t k = 0; k <= W / 2; ++k) {
        const double f = static_cast<double>(k) * fs / static_cast<double>(W);
        if (f < lo || f >= hi)
            continue;
        const double pi = 0.25 * std::norm(in.L[k] - in.R[k]);
        if (pi < peak * 1e-6)
            continue;
        const double po = 0.25 * std::norm(out.L[k] - out.R[k]);
        std::cout << "BSF_SC006_R3," << label << "," << f << "," << Pft::safeDb10(pi) << "," << Pft::safeDb10(po)
                  << "," << Pft::safeDb10(pi) - Pft::safeDb10(po) << "\n";
    }
}

enum class BsfAttenArm : std::uint8_t { DirtyWide, Excitation };

/// SC-006 fixtures (i) and (ii): the stage at Gravity 1 and f_c, the generator below f_c forced to centre and
/// held at Gravity 1 (forceSubCentre, generatorGravity; spec descriptor block).
/// (i) the WIDE dirty arm at Spread 1 (offsets at the ceilings); (ii) the core with excitation mix 1, drive 1.
Sft::FieldChainConfig bsfAttenConfig(BsfAttenArm arm, BsfAlg a, double f0, double fs, float fc) {
    const BsfMode mode = arm == BsfAttenArm::DirtyWide ? BsfMode::Wide : BsfMode::Spread;
    const BsfControls c = bsfControls(mode, 1.0f, 1.0f, fc, BsfControls{}.curve);
    Sft::FieldChainConfig cfg =
        arm == BsfAttenArm::DirtyWide ? bsfDirtyConfig(a, f0, fs, c) : bsfCleanConfig(f0, fs, c);
    if (arm == BsfAttenArm::Excitation)
        cfg.excitation = Krate::DSP::ProfundumCore::ExcitationControls{.mix = 1.0f, .drive = 1.0f, .asymmetry = 0.0f};
    cfg.forceSubCentre = true;
    cfg.generatorGravity = 1.0f;
    return cfg;
}

/// Re-runs only the stage on copies of the recorded stage input (dynL/R) at another stage Gravity: the input
/// render is fixed by generatorGravity and the WIDE offsets do not depend on Gravity (plan S6), so this equals
/// a full render at that Gravity. Same field construction, control sequence and host partition as
/// renderFieldChain (StereoFieldTest::makeField / advanceFieldAt).
void bsfRestageGravity(std::vector<float>& L, std::vector<float>& R, const Sft::FieldChainConfig& cfg,
                       float gravity) {
    const std::optional<float> g = gravity;
    std::unique_ptr<BassStereoField> f = Sft::makeField(cfg, g);
    Mdt::gridDriver(L.size(), cfg.chain.hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        if (atGrid)
            Sft::advanceFieldAt(*f, cfg, start, true, g);
        f->processBlock(L.data() + start, R.data() + start, len);
    });
}

struct BsfAttenResult {
    bool valid = false;
    BsfSideBand band{};
    std::size_t thirdsValid = 0;
    double worstThirdDb = std::numeric_limits<double>::infinity();
    double gravity0Db = 0.0;
};

std::string bsfAttenCsvHeader() { return "label,kind,lo_hz,hi_hz,valid,d_in_dbfs,d_out_dbfs,atten_db"; }

/// A(B) and the third octaves of one rendered cell; `gate` asserts the 24 dB bar on valid readings; with
/// `monotone` the stage is re-run over Gravity i/10 and A(B) must be non-decreasing (0.01 dB).
BsfAttenResult bsfAttenuationFromTaps(const Sft::FieldChainTaps& t, const Sft::FieldChainConfig& cfg, float fc,
                                      const std::string& label, BsfCsv& csv, bool gate, BsfTally* monotone) {
    const double fs = cfg.chain.fs;
    const std::size_t s = bsfSettle(fs);
    REQUIRE(t.dynL.size() > s);
    const std::size_t n = t.dynL.size() - s;
    const auto fcD = static_cast<double>(fc);
    const Sft::BandSpectra in = Sft::bandSpectra(t.dynL.data() + s, t.dynR.data() + s, n);
    const Sft::BandSpectra out = Sft::bandSpectra(t.stageOutL.data() + s, t.stageOutR.data() + s, n);
    BsfAttenResult r;
    r.band = bsfSideBand(in, out, fs, kBsfBandLoHz, fcD);
    r.valid = r.band.valid;
    csv.line(bsfRow(label, "B", kBsfBandLoHz, fcD, r.valid ? 1 : 0, r.band.inDb, r.band.outDb, r.band.attenDb));
    if (!r.valid) {
        std::cout << "BSF_SC006," << label << ",no distortion-born content in B\n";
        return r;
    }
    bool nearBar = std::abs(r.band.attenDb - kBsfAttenuationBarDb) <= kBsfR3MarginDb;
    if (gate) {
        INFO(label << " A(B) " << r.band.attenDb << " dB (in " << r.band.inDb << " dBFS, out " << r.band.outDb
                   << ")");
        CHECK(r.band.attenDb >= kBsfAttenuationBarDb);
    }
    for (const Sft::Band& b : Sft::thirdOctaves(kBsfBandLoHz, fcD)) {
        const BsfSideBand tb = bsfSideBand(in, out, fs, b.lo, b.hi);
        csv.line(bsfRow(label, "third", b.lo, b.hi, tb.valid ? 1 : 0, tb.inDb, tb.outDb, tb.attenDb));
        if (!tb.valid) {
            std::cout << "BSF_SC006," << label << ",third " << b.lo << " Hz: no distortion-born content\n";
            continue;
        }
        ++r.thirdsValid;
        r.worstThirdDb = std::min(r.worstThirdDb, tb.attenDb);
        nearBar = nearBar || std::abs(tb.attenDb - kBsfAttenuationBarDb) <= kBsfR3MarginDb;
        if (gate) {
            INFO(label << " third octave [" << b.lo << ", " << b.hi << ") A " << tb.attenDb << " dB");
            CHECK(tb.attenDb >= kBsfAttenuationBarDb);
        }
    }
    if (nearBar)
        bsfPrintPerBin(in, out, fs, kBsfBandLoHz, fcD, label);

    if (monotone != nullptr) {
        double prev = 0.0;
        for (std::size_t i = 0; i < kBsfSpreadSteps; ++i) {
            const float gravity = bsfStep(i);
            std::vector<float> L = t.dynL;
            std::vector<float> R = t.dynR;
            bsfRestageGravity(L, R, cfg, gravity);
            if (i + 1 == kBsfSpreadSteps) {
                // The restage at the render's own Gravity (1) reproduces the render's stage output.
                REQUIRE(Sft::bitEqual(L, t.stageOutL));
                REQUIRE(Sft::bitEqual(R, t.stageOutR));
            }
            const Sft::BandSpectra o = Sft::bandSpectra(L.data() + s, R.data() + s, n);
            const BsfSideBand gb = bsfSideBand(in, o, fs, kBsfBandLoHz, fcD);
            csv.line(bsfRow(label, "gravity_" + bsfFmt(static_cast<double>(gravity)), kBsfBandLoHz, fcD,
                            gb.valid ? 1 : 0, gb.inDb, gb.outDb, gb.attenDb));
            if (i == 0) {
                r.gravity0Db = gb.attenDb;
                std::cout << "BSF_SC006,gravity0," << label << ",A_B_db," << gb.attenDb << "\n";
            } else {
                monotone->add(prev - gb.attenDb - kBsfGravityTolDb,
                              [&] { return label + ";gravity=" + bsfFmt(static_cast<double>(gravity)); });
            }
            prev = gb.attenDb;
        }
    }
    return r;
}

/// SC-006 (iii): a field-only side tone (L = x, R = -x, amplitude 0.25), 2 s, the stage at Gravity 1 and f_c;
/// A over B from the window starting at 1 s. Returns A(B) in dB.
double bsfToneAttenuation(float fc, double hz, const std::string& label, BsfCsv& csv) {
    const double fs = kBsfFs;
    const auto n = static_cast<std::size_t>(2.0 * fs);
    std::vector<float> L(n, 0.0f);
    std::vector<float> R(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const auto x = static_cast<float>(0.25 * std::sin(kBsfTwoPi * hz * static_cast<double>(i) / fs));
        L[i] = x;
        R[i] = -x;
    }
    const BsfStageRun run =
        bsfRunStage(L, R, bsfControls(BsfMode::Spread, 1.0f, 1.0f, fc, BsfControls{}.curve), fs);
    const auto s = static_cast<std::size_t>(fs);
    const Sft::BandSpectra in = Sft::bandSpectra(L.data() + s, R.data() + s, n - s);
    const Sft::BandSpectra out = Sft::bandSpectra(run.L.data() + s, run.R.data() + s, n - s);
    const BsfSideBand b = bsfSideBand(in, out, fs, kBsfBandLoHz, static_cast<double>(fc));
    csv.line(bsfRow(label, "tone_" + bsfFmt(hz), kBsfBandLoHz, fc, b.valid ? 1 : 0, b.inDb, b.outDb, b.attenDb));
    INFO(label << " tone " << hz << " Hz A " << b.attenDb << " dB");
    REQUIRE(b.valid);
    if (std::abs(b.attenDb - kBsfAttenuationBarDb) <= kBsfR3MarginDb)
        bsfPrintPerBin(in, out, fs, kBsfBandLoHz, static_cast<double>(fc), label);
    return b.attenDb;
}

// -----------------------------------------------------------------------------
// SC-010 levers (FR-076)
// -----------------------------------------------------------------------------

std::string bsfLeverCsvHeader() { return "kind,lever,note,reading_a,reading_b,move,bar,pass"; }

/// A static lever: the largest octave-band move at stageOutL/R (StereoFieldTest::staticLeverMove), the bands
/// from kMinCrossoverHz up so every lever reads the same band set (run-log decision).
void bsfStaticLever(const char* lever, const BsfNote& note, const Sft::FieldChainConfig& a,
                    const Sft::FieldChainConfig& b, BsfCsv& csv, std::vector<std::string>& table) {
    Sft::LeverMove m;
    {
        const Sft::FieldChainTaps ta = Sft::renderFieldChain(a);
        const Sft::FieldChainTaps tb = Sft::renderFieldChain(b);
        m = Sft::staticLeverMove(ta, tb, a.chain.fs, static_cast<double>(BassStereoField::kMinCrossoverHz));
    }
    const bool pass = m.sideToMidDb >= kBsfLeverS2mBarDb || m.rho >= kBsfLeverRhoBar;
    // G8-1 refinement (spec SC-010): the Gravity lever is gated at E1 and recorded at C3, where no harmonic lies
    // below f_s = 240 Hz except the centred h1.
    const bool gated = !(std::string(lever) == "gravity_0_1" && std::string(note.name) != "E1");
    const std::string row = bsfRow(gated ? "static" : "static-recorded", lever, note.name, m.sideToMidDb, m.rho,
                                   "s2m_db|rho", "3dB|0.1", pass ? 1 : 0);
    csv.line(row);
    table.push_back(row);
    INFO(row);
    if (gated)
        CHECK(pass);
}

/// The SC-010 static levers of one note: each alone between its extremes from Controls{} (SPREAD, kSawAnchor);
/// WIDE versus SPREAD on the dirty arm (Tube). `all` = false runs Spread and WIDE-vs-SPREAD only (smoke).
void bsfStaticLevers(const BsfNote& note, bool all, BsfCsv& csv, std::vector<std::string>& table) {
    const double fs = kBsfFs;
    const double f0 = bsfHz(note);
    const BsfControls base{};
    auto clean = [&](auto&& edit) {
        BsfControls c = base;
        edit(c);
        return bsfCleanConfig(f0, fs, c);
    };
    bsfStaticLever("spread_0_1", note, clean([](BsfControls& c) { c.spread = 0.0f; }),
                   clean([](BsfControls& c) { c.spread = 1.0f; }), csv, table);
    if (all) {
        bsfStaticLever("gravity_0_1", note, clean([](BsfControls& c) { c.gravity = 0.0f; }),
                       clean([](BsfControls& c) { c.gravity = 1.0f; }), csv, table);
        bsfStaticLever("crossover_60_250", note, clean([](BsfControls& c) { c.crossoverHz = 60.0f; }),
                       clean([](BsfControls& c) { c.crossoverHz = 250.0f; }), csv, table);
        bsfStaticLever("curve_0_1", note, clean([](BsfControls& c) { c.curve = 0.0f; }),
                       clean([](BsfControls& c) { c.curve = 1.0f; }), csv, table);
    }
    BsfControls wide = base;
    wide.mode = BsfMode::Wide;
    // Harvest-dependent (tasks.md Group 7): the WIDE offsets read the committed kWideOffsetScale.
    bsfStaticLever("wide_vs_spread_dirty_Tube", note, bsfDirtyConfig(BsfAlg::Tube, f0, fs, base),
                   bsfDirtyConfig(BsfAlg::Tube, f0, fs, wide), csv, table);
}

/// W_oct(t): the short-window side-to-mid ratio of `band`, a 4096-sample BH7 window with hop 1024 at 48 kHz
/// (scaled with fs), from `start` to the end (SC-010).
std::vector<double> bsfWoctSeries(const std::vector<float>& L, const std::vector<float>& R, double fs, double f0,
                                  const Sft::Band& band, std::size_t start, std::size_t& hopOut) {
    // G10-6: at least 8 periods of f0, so the per-window estimate does not jitter with the window phase (at E1 the
    // 85 ms window held 3.5 periods and read a 45 ms "1/e time" at both Drift rates).
    const std::size_t win = Sft::nearestPow2(std::max(4096.0 * fs / 48000.0, 8.0 * fs / f0));
    const std::size_t hop = win / 4;
    hopOut = hop;
    const std::vector<double> w = Pft::bh7Window(win);
    std::vector<std::complex<double>> a(win);
    std::vector<std::complex<double>> b(win);
    std::vector<double> out;
    for (std::size_t pos = start; pos + win <= L.size(); pos += hop) {
        for (std::size_t k = 0; k < win; ++k) {
            a[k] = std::complex<double>(static_cast<double>(L[pos + k]) * w[k], 0.0);
            b[k] = std::complex<double>(static_cast<double>(R[pos + k]) * w[k], 0.0);
        }
        Pft::fftDouble(a);
        Pft::fftDouble(b);
        double side = 0.0;
        double mid = 0.0;
        for (std::size_t k = 0; k <= win / 2; ++k) {
            const double f = static_cast<double>(k) * fs / static_cast<double>(win);
            if (f < band.lo || f >= band.hi)
                continue;
            side += std::norm(a[k] - b[k]);
            mid += std::norm(a[k] + b[k]);
        }
        out.push_back((side > 0.0 && mid > 0.0) ? std::max(Pft::safeDb10(side / mid), Sft::kFloorDb)
                                                : Sft::kFloorDb);
    }
    return out;
}

double bsfMean(const std::vector<double>& x) {
    REQUIRE(!x.empty());
    return std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(x.size());
}

double bsfStdDev(const std::vector<double>& x) {
    const double m = bsfMean(x);
    double acc = 0.0;
    for (const double v : x)
        acc += (v - m) * (v - m);
    return std::sqrt(acc / static_cast<double>(x.size()));
}

/// The dominant modulation frequency of a series sampled at rateHz: the de-meaned series, rectangular window,
/// zero-padded to a power of two >= 16x its length, the largest bin in [0.01 Hz, rateHz / 2) (run-log decision:
/// a BH7 main lobe at 60 s is +-0.067 Hz wide and would swallow Cycle's 0.05 Hz rate-0 line).
double bsfDominantHz(const std::vector<double>& x, double rateHz) {
    REQUIRE(x.size() >= 8);
    const double m = bsfMean(x);
    std::size_t len = 1;
    while (len < 16 * x.size())
        len <<= 1;
    std::vector<std::complex<double>> a(len, std::complex<double>(0.0, 0.0));
    for (std::size_t i = 0; i < x.size(); ++i)
        a[i] = std::complex<double>(x[i] - m, 0.0);
    Pft::fftDouble(a);
    double best = -1.0;
    double bestHz = 0.0;
    for (std::size_t k = 1; k <= len / 2; ++k) {
        const double hz = static_cast<double>(k) * rateHz / static_cast<double>(len);
        if (hz < 0.01 || hz >= 0.5 * rateHz)
            continue;
        const double p = std::norm(a[k]);
        if (p > best) {
            best = p;
            bestHz = hz;
        }
    }
    return bestHz;
}

/// The first lag at which the de-meaned, biased autocorrelation falls below 1/e (linear interpolation between
/// lags), in seconds; censored at half the record (a lower bound) when it never does.
double bsfOneOverESeconds(const std::vector<double>& x, double hopSeconds, bool& censored) {
    REQUIRE(x.size() >= 8);
    const double m = bsfMean(x);
    std::vector<double> d(x.size(), 0.0);
    for (std::size_t i = 0; i < x.size(); ++i)
        d[i] = x[i] - m;
    double r0 = 0.0;
    for (const double v : d)
        r0 += v * v;
    REQUIRE(r0 > 0.0);
    const double target = std::exp(-1.0);
    const std::size_t maxLag = d.size() / 2;
    double prev = 1.0;
    for (std::size_t lag = 1; lag <= maxLag; ++lag) {
        double acc = 0.0;
        for (std::size_t i = 0; i + lag < d.size(); ++i)
            acc += d[i] * d[i + lag];
        const double r = acc / r0;
        if (r < target) {
            const double frac = (prev - target) / (prev - r);
            censored = false;
            return (static_cast<double>(lag - 1) + frac) * hopSeconds;
        }
        prev = r;
    }
    censored = true;
    return static_cast<double>(maxLag) * hopSeconds;
}

struct BsfMotionRun {
    std::vector<double> series;
    double hopSeconds = 0.0;
    Sft::Band band{};
};

/// A 61 s clean render at `c`; W_oct(t) over [1 s, 61 s) at stageOutL/R. With no band given, the band is the
/// valid octave (StereoFieldTest::octaveBands(f_c)) with the largest static side-to-mid of this render.
BsfMotionRun bsfMotionRun(double f0, double fs, const BsfControls& c, const std::optional<Sft::Band>& band) {
    const Sft::FieldChainTaps t = Sft::renderFieldChain(bsfCleanConfig(f0, fs, c, kBsfMotionSeconds));
    const std::size_t s = bsfSettle(fs);
    BsfMotionRun r;
    if (band.has_value()) {
        r.band = *band;
    } else {
        const std::vector<Sft::Band> bands = Sft::octaveBands(static_cast<double>(c.crossoverHz));
        const std::vector<Sft::BandReading> rs = Sft::bandReadings(t.stageOutL.data() + s, t.stageOutR.data() + s,
                                                                   t.stageOutL.size() - s, fs, bands);
        std::optional<std::size_t> best;
        for (std::size_t b = 0; b < rs.size(); ++b) {
            if (rs[b].valid && (!best.has_value() || rs[b].sideToMidDb > rs[*best].sideToMidDb))
                best = b;
        }
        REQUIRE(best.has_value());
        r.band = bands[*best];
    }
    std::size_t hop = 0;
    r.series = bsfWoctSeries(t.stageOutL, t.stageOutR, fs, f0, r.band, s, hop);
    r.hopSeconds = static_cast<double>(hop) / fs;
    REQUIRE(r.series.size() >= 8);
    return r;
}

/// SC-010 MOTION levers of one note: depth 0 -> 1 (Cycle rate 0.5, Spread 1) raises std(W_oct) by >= 1 dB;
/// rate 0 <-> 1 changes Cycle's dominant modulation frequency and Drift's 1/e time by a ratio >= 4.
void bsfMotionLevers(const BsfNote& note, BsfCsv& csv, std::vector<std::string>& table) {
    const double fs = kBsfFs;
    const double f0 = bsfHz(note);
    BsfControls c{};
    c.spread = 1.0f;
    c.motionSource = BsfSource::Cycle;
    c.motionRate = 0.5f;
    c.motionDepth = 0.0f;
    const BsfMotionRun d0 = bsfMotionRun(f0, fs, c, std::nullopt);
    std::cout << "BSF_SC010," << note.name << ",W_oct band [" << d0.band.lo << ", " << d0.band.hi << ") Hz, hop "
              << d0.hopSeconds << " s, " << d0.series.size() << " windows\n";
    c.motionDepth = 1.0f;
    {
        const BsfMotionRun d1 = bsfMotionRun(f0, fs, c, d0.band);
        const double s0 = bsfStdDev(d0.series);
        const double s1 = bsfStdDev(d1.series);
        const bool pass = (s1 - s0) >= kBsfDepthStdBarDb;
        const std::string row =
            bsfRow("motion", "depth_0_1_cycle_rate0.5_std_db", note.name, s0, s1, s1 - s0, "1dB", pass ? 1 : 0);
        csv.line(row);
        table.push_back(row);
        INFO(row);
        CHECK(pass);
    }
    {
        c.motionRate = 0.0f;
        const BsfMotionRun r0 = bsfMotionRun(f0, fs, c, d0.band);
        c.motionRate = 1.0f;
        const BsfMotionRun r1 = bsfMotionRun(f0, fs, c, d0.band);
        const double h0 = bsfDominantHz(r0.series, 1.0 / r0.hopSeconds);
        const double h1 = bsfDominantHz(r1.series, 1.0 / r1.hopSeconds);
        const double ratio = std::max(h0, h1) / std::max(std::min(h0, h1), 1e-12);
        const bool pass = ratio >= kBsfRateRatioBar;
        const std::string row = bsfRow("motion", "rate_0_1_cycle_hz", note.name, h0, h1, ratio, "x4", pass ? 1 : 0);
        csv.line(row);
        table.push_back(row);
        INFO(row << " (plan S1 C-9 map: 0.05 Hz and 1 Hz)");
        CHECK(pass);
    }
    {
        c.motionSource = BsfSource::Drift;
        c.motionRate = 0.0f;
        const BsfMotionRun r0 = bsfMotionRun(f0, fs, c, d0.band);
        c.motionRate = 1.0f;
        const BsfMotionRun r1 = bsfMotionRun(f0, fs, c, d0.band);
        bool cens0 = false;
        bool cens1 = false;
        const double t0 = bsfOneOverESeconds(r0.series, r0.hopSeconds, cens0);
        const double t1 = bsfOneOverESeconds(r1.series, r1.hopSeconds, cens1);
        const double ratio = std::max(t0, t1) / std::max(std::min(t0, t1), 1e-12);
        const bool pass = ratio >= kBsfRateRatioBar;
        const std::string row =
            bsfRow("motion", "rate_0_1_drift_1e_s", note.name, t0, t1, ratio, "x4", pass ? 1 : 0);
        csv.line(row);
        table.push_back(row);
        std::cout << "BSF_SC010," << note.name << ",drift 1/e censored (lower bound) rate0," << cens0 << ",rate1,"
                  << cens1 << " (plan S1 C-9 map: tau 30 s and 0.5 s)\n";
        INFO(row);
        CHECK(pass);
    }
}

// -----------------------------------------------------------------------------
// SC-016 sample rates
// -----------------------------------------------------------------------------

void bsfRequireBounded(const Sft::FieldChainTaps& t, const std::string& label) {
    bool finite = true;
    double peak = 0.0;
    for (const std::vector<float>* v : {&t.stageOutL, &t.stageOutR, &t.outL, &t.outR}) {
        for (const float x : *v) {
            if (!Krate::DSP::detail::isFinite(x)) {
                finite = false;
                continue;
            }
            peak = std::max(peak, static_cast<double>(std::abs(x)));
        }
    }
    std::cout << "BSF_SC016,bounded," << label << ",finite," << finite << ",peak," << peak << "\n";
    INFO(label << " finite " << finite << " peak " << peak);
    REQUIRE(finite);
    REQUIRE(peak <= kBsfOutputBound);
}

std::string bsfRateCsvHeader() {
    return "kind,fs_hz,n_or_label,ref_core_or_a48,core_or_a,delta_core_or_a,ref_stage,stage,delta_stage";
}

/// SC-016: |W_n(fs) - W_n(48 kHz)| <= 0.25 dB per harmonic, at both taps.
void bsfCheckRateWidth(const BsfWidthReading& ref, const BsfWidthReading& r, double fs, BsfCsv& csv, BsfTally& t) {
    REQUIRE(ref.core.w.size() == r.core.w.size());
    REQUIRE(ref.stage.w.size() == r.stage.w.size());
    for (std::size_t n = 0; n < r.core.w.size(); ++n) {
        const double dc = r.core.w[n] - ref.core.w[n];
        const double ds = r.stage.w[n] - ref.stage.w[n];
        csv.line(bsfRow("W_n", fs, n + 1, ref.core.w[n], r.core.w[n], dc, ref.stage.w[n], r.stage.w[n], ds));
        // G10-4: a W_n below -100 dB at 48 kHz is float residue (the 192 kHz LR4 biquads read -112 ... -119 dB).
        if (ref.core.w[n] >= kBsfWReadFloorDb)
            t.add(std::abs(dc) - kBsfRateWDbBar,
                  [&] { return "fs=" + bsfFmt(fs) + ";tap=core;n=" + std::to_string(n + 1); });
        if (ref.stage.w[n] >= kBsfWReadFloorDb)
            t.add(std::abs(ds) - kBsfRateWDbBar,
                  [&] { return "fs=" + bsfFmt(fs) + ";tap=stageOut;n=" + std::to_string(n + 1); });
    }
}

/// E1, Controls{} at Spread 1, clean core: the W_n reading at fs (optionally the SC-016 bound on the render).
BsfWidthReading bsfRateWidth(double fs, bool bounded) {
    const double f0 = bsfHz(kBsfE1);
    BsfControls c{};
    c.spread = 1.0f;
    const Sft::FieldChainTaps t = Sft::renderFieldChain(bsfCleanConfig(f0, fs, c));
    if (bounded)
        bsfRequireBounded(t, "clean_E1_fs_" + bsfFmt(fs));
    return bsfReadWidth(t, fs, f0, bsfHarmonicCount(fs, f0), bsfOctaves(static_cast<double>(c.crossoverHz)));
}

/// Field-only trace for the re-prepare case: advanceControl + a law query at E1 every interval, processBlock in
/// place; returns the output and the emitted vectors.
struct BsfFieldTrace {
    std::vector<float> L, R;
    std::vector<BsfPan> vectors;
};

BsfFieldTrace bsfFieldTrace(BassStereoField& f, std::vector<float> L, std::vector<float> R,
                            const std::function<BsfControls(std::size_t)>& at) {
    BsfFieldTrace tr;
    BassStereoField::VoiceState vs{};
    BsfPan pan{};
    const auto f0 = static_cast<float>(bsfHz(kBsfE1));
    Mdt::gridDriver(L.size(), kBsfHostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        if (atGrid) {
            f.setControls(at(start));
            f.advanceControl();
            f.computePanVector(vs, f0, pan);
            tr.vectors.push_back(pan);
        }
        f.processBlock(L.data() + start, R.data() + start, len);
    });
    tr.L = std::move(L);
    tr.R = std::move(R);
    return tr;
}

std::pair<std::vector<float>, std::vector<float>> bsfNoise(std::size_t n, std::uint32_t seed) {
    std::mt19937 rng{seed};
    std::vector<float> L(n, 0.0f);
    std::vector<float> R(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        L[i] = bsfUniform(rng);
        R[i] = bsfUniform(rng);
    }
    return {L, R};
}

}  // namespace

// ==============================================================================
// SC-003 (FR-012, FR-044): width monotonic in frequency and Spread; Gravity never widens; Spread 0 is mono.
// The clean-core grid in SPREAD and WIDE at the generator (coreL/R) and stage (stageOutL/R) outputs, and the
// WIDE dirty arm at the stage output. CSV evidence/sc003-width.csv.
// ==============================================================================
TEST_CASE("BassStereoField_WidthMonotonic", "[systems][profundum][stereo-field][sc-003][long]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc003-width.csv", bsfWidthCsvHeader());
    BsfWidthTallies t;
    const std::vector<float> gravities{0.0f, 0.5f, 1.0f};
    std::vector<std::size_t> allSteps(kBsfSpreadSteps);
    std::iota(allSteps.begin(), allSteps.end(), std::size_t{0});
    const std::vector<std::vector<std::size_t>> steps(gravities.size(), allSteps);
    for (const BsfNote& note : {kBsfE1, kBsfC2}) {
        for (const float fc : kBsfCrossovers) {
            for (const float curve : {0.0f, 1.0f})
                bsfRunWidthGrid(note, fc, curve, gravities, steps, true, csv, t);
        }
    }
    for (const BsfNote& note : {kBsfE1, kBsfC2}) {
        for (std::size_t a = 0; a < kBsfAlgorithms.size(); ++a)
            bsfRunWideDirty(note, kBsfAlgorithms[a], kBsfAlgNames[a], csv, t);
    }
    for (const BsfTally* tally : {&t.aW, &t.aRho, &t.bW, &t.bRho, &t.cW, &t.cS2m, &t.cDrop, &t.dFloor, &t.wideEq})
        bsfReport(*tally);
    for (const BsfTally* tally : {&t.dirtyBand, &t.dirtySpread})
        bsfRecord(*tally);   // G10-1
}

// SC-003 smoke (deviation 6): E1, crossover 120, curve 0, SPREAD, clean core: (a) and (b) over the 11 Spread
// steps at Gravity 0.5, (c) over Gravity {0, 0.5, 1} at Spread 1, (d) at Spread 0.
TEST_CASE("BassStereoField_WidthMonotonicSmoke", "[systems][profundum][stereo-field][sc-003]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc003-width-smoke.csv", bsfWidthCsvHeader());
    BsfWidthTallies t;
    std::vector<std::size_t> allSteps(kBsfSpreadSteps);
    std::iota(allSteps.begin(), allSteps.end(), std::size_t{0});
    const std::vector<float> gravities{0.0f, 0.5f, 1.0f};
    const std::vector<std::vector<std::size_t>> steps{{kBsfSpreadSteps - 1}, allSteps, {kBsfSpreadSteps - 1}};
    bsfRunWidthGrid(kBsfE1, 120.0f, 0.0f, gravities, steps, false, csv, t);
    for (const BsfTally* tally : {&t.aW, &t.aRho, &t.bW, &t.bRho, &t.cW, &t.cS2m, &t.cDrop, &t.dFloor})
        bsfReport(*tally);
}

// ==============================================================================
// SC-005 record (FR-023, plan S7.4): the stage's phase on S and on M, the S group delay at f_s, and Q_band on
// rho = 0.5 noise, field-only, at f_c {60, 120, 250} x Gravity {0, 1}. Gated here: finite, |M_out / M_in| = 1
// within 1e-6 (latencySamples() == 0 by static_assert). CSV evidence/sc005-phase.csv.
// ==============================================================================
TEST_CASE("BassStereoField_SafetyStagePhase", "[systems][profundum][stereo-field][sc-005][long]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc005-phase.csv", bsfPhaseCsvHeader());
    std::cout << "BSF_SC005,analytic all-pass group delay at f_s (plan S12.5): 3.751 / 1.876 / 0.900 ms at f_s "
                 "120 / 240 / 500 Hz\n";
    for (const float fc : kBsfCrossovers) {
        for (const float gravity : {0.0f, 1.0f})
            bsfPhaseCell(fc, gravity, csv, true);
    }
}

// SC-005 smoke (deviation 6): f_c 120, Gravity 0, no noise arm.
TEST_CASE("BassStereoField_SafetyStagePhaseSmoke", "[systems][profundum][stereo-field][sc-005]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc005-phase-smoke.csv", bsfPhaseCsvHeader());
    bsfPhaseCell(120.0f, 0.0f, csv, false);
}

// ==============================================================================
// SC-006 (FR-021, FR-024): distortion-born side content below f_c attenuated by >= 24 dB over B = [20 Hz, f_c)
// and every valid third octave, at Gravity 1; A(B) non-decreasing over Gravity i/10 with the input fixed by the
// held generator Gravity. Fixtures (i) WIDE dirty arm, (ii) excitation, (iii) side tones. CSV
// evidence/sc006-attenuation.csv.
// ==============================================================================
TEST_CASE("BassStereoField_SafetyStageAttenuation", "[systems][profundum][stereo-field][sc-006][long]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc006-attenuation.csv", bsfAttenCsvHeader());
    BsfTally monotone{.name = "sc006_A_B_nondecreasing_in_gravity"};
    std::array<std::array<std::size_t, 3>, 2> validPerFc{};   // [arm][crossover]
    std::size_t emptyCells = 0;
    std::size_t mismatches = 0;
    for (std::size_t ci = 0; ci < kBsfCrossovers.size(); ++ci) {
        const float fc = kBsfCrossovers[ci];
        for (const BsfNote& note : kBsfAttenNotes) {
            const double f0 = bsfHz(note);
            // Content expected iff a harmonic lies in [20 Hz, f_c) (plan S13.3 SC-006 "which cells carry content"),
            // or the algorithm makes sub-f0 products (Fracture; G10-5).
            const bool harmonicBelowFc = f0 < static_cast<double>(fc);
            for (std::size_t arm = 0; arm < 2; ++arm) {
                const BsfAttenArm armKind = arm == 0 ? BsfAttenArm::DirtyWide : BsfAttenArm::Excitation;
                const std::size_t algCount = arm == 0 ? kBsfAlgorithms.size() : 1;
                for (std::size_t a = 0; a < algCount; ++a) {
                    const std::string label = std::string(arm == 0 ? "i_wide_dirty_" : "ii_excitation") +
                                              (arm == 0 ? kBsfAlgNames[a] : "") + "_" + note.name + "_fc" +
                                              bsfFmt(static_cast<double>(fc));
                    const bool expected =
                        harmonicBelowFc || (arm == 0 && kBsfAlgorithms[a] == BsfAlg::Fracture);
                    const Sft::FieldChainConfig cfg = bsfAttenConfig(armKind, kBsfAlgorithms[a], f0, kBsfFs, fc);
                    const Sft::FieldChainTaps taps = Sft::renderFieldChain(cfg);
                    const BsfAttenResult r = bsfAttenuationFromTaps(taps, cfg, fc, label, csv, true, &monotone);
                    std::cout << "BSF_SC006,validity," << label << ",valid," << r.valid << ",expected," << expected
                              << (r.valid != expected ? ",MISMATCH" : "") << "\n";
                    if (r.valid)
                        ++validPerFc[arm][ci];
                    else
                        ++emptyCells;
                    if (r.valid != expected && arm == 0)   // G10-5: fixture (ii) is recorded
                        ++mismatches;
                }
            }
        }
    }
    std::cout << "BSF_SC006,summary,empty_cells," << emptyCells << ",validity_mismatches," << mismatches << "\n";
    for (std::size_t ci = 0; ci < kBsfCrossovers.size(); ++ci) {
        INFO("non-vacuity at f_c " << kBsfCrossovers[ci] << ": (i) " << validPerFc[0][ci] << " valid cells, (ii) "
                                   << validPerFc[1][ci]);
        REQUIRE(validPerFc[0][ci] >= 1);
        // G10-5: fixture (ii), the edge excitation, is recorded; below -60 dBFS it makes no audible side content in B.
        if (validPerFc[1][ci] == 0)
            std::cout << "BSF_SC006,record,ii_excitation_fc" << bsfFmt(static_cast<double>(kBsfCrossovers[ci]))
                      << ",no audible distortion-born side content below f_c (G10-5; recorded)\n";
    }
    // (iii) positive controls: side tones at sqrt(20 f_c) and 0.95 f_c, at every crossover.
    for (const float fc : kBsfCrossovers) {
        const auto fcD = static_cast<double>(fc);
        for (const double hz : {std::sqrt(kBsfBandLoHz * fcD), 0.95 * fcD}) {
            const std::string label = "iii_tone_fc" + bsfFmt(fcD);
            const double atten = bsfToneAttenuation(fc, hz, label, csv);
            INFO(label << " " << hz << " Hz: A " << atten << " dB");
            CHECK(atten >= kBsfAttenuationBarDb);
        }
    }
    bsfReport(monotone);
}

// SC-006 smoke (deviation 6): (i) Tube E1 f_c 120 (content expected, so validity is REQUIREd) and (iii) at f_c 60.
TEST_CASE("BassStereoField_SafetyStageAttenuationSmoke", "[systems][profundum][stereo-field][sc-006]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc006-attenuation-smoke.csv", bsfAttenCsvHeader());
    BsfTally monotone{.name = "sc006_smoke_A_B_nondecreasing_in_gravity"};
    const Sft::FieldChainConfig cfg =
        bsfAttenConfig(BsfAttenArm::DirtyWide, BsfAlg::Tube, bsfHz(kBsfE1), kBsfFs, 120.0f);
    const Sft::FieldChainTaps taps = Sft::renderFieldChain(cfg);
    const BsfAttenResult r =
        bsfAttenuationFromTaps(taps, cfg, 120.0f, "i_wide_dirty_Tube_E1_fc120", csv, true, &monotone);
    REQUIRE(r.valid);
    bsfReport(monotone);
    const double fc = 60.0;
    for (const double hz : {std::sqrt(kBsfBandLoHz * fc), 0.95 * fc}) {
        const double atten = bsfToneAttenuation(60.0f, hz, "iii_tone_fc60", csv);
        INFO("tone " << hz << " Hz: A " << atten << " dB");
        CHECK(atten >= kBsfAttenuationBarDb);
    }
}

// ==============================================================================
// SC-010 (FR-076): every lever moves a stereo descriptor on the showcase (SPREAD, kSawAnchor, E1 and C3). One
// table of every lever is printed. CSV evidence/sc010-levers.csv.
// ==============================================================================
TEST_CASE("BassStereoField_LeversAudible", "[systems][profundum][stereo-field][sc-010][long]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc010-levers.csv", bsfLeverCsvHeader());
    std::vector<std::string> table;
    for (const BsfNote& note : {kBsfE1, kBsfC3}) {
        bsfStaticLevers(note, true, csv, table);
        bsfMotionLevers(note, csv, table);
    }
    std::cout << "BSF_SC010,lever table (" << bsfLeverCsvHeader() << ")\n";
    for (const std::string& row : table)
        std::cout << "BSF_SC010_TABLE," << row << "\n";
}

// SC-010 smoke (deviation 6; harvest-dependent for WIDE): Spread and WIDE-vs-SPREAD at E1.
TEST_CASE("BassStereoField_LeversAudibleSmoke", "[systems][profundum][stereo-field][sc-010]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc010-levers-smoke.csv", bsfLeverCsvHeader());
    std::vector<std::string> table;
    bsfStaticLevers(kBsfE1, false, csv, table);
    REQUIRE(table.size() == 2);
}

// ==============================================================================
// SC-016 (FR-075): at 44.1, 48, 88.2, 96 and 192 kHz the output is finite and bounded, E1 W_n is within 0.25 dB
// of the 48 kHz value, and A(B) of fixture (i) (Tube, E1, f_c 120) is within 1 dB of it. CSV
// evidence/sc016-rates.csv (+ sc016-rates-attenuation.csv).
// ==============================================================================
TEST_CASE("BassStereoField_SampleRates", "[systems][profundum][stereo-field][sc-016][long]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc016-rates.csv", bsfRateCsvHeader());
    BsfCsv attenCsv("sc016-rates-attenuation.csv", bsfAttenCsvHeader());
    BsfTally wTally{.name = "sc016_W_n_within_0.25dB_of_48k"};
    const std::array<double, 5> rates{48000.0, 44100.0, 88200.0, 96000.0, 192000.0};
    std::optional<BsfWidthReading> ref;
    double refA = 0.0;
    for (const double fs : rates) {
        const BsfWidthReading w = bsfRateWidth(fs, true);
        const Sft::FieldChainConfig cfg =
            bsfAttenConfig(BsfAttenArm::DirtyWide, BsfAlg::Tube, bsfHz(kBsfE1), fs, 120.0f);
        const Sft::FieldChainTaps taps = Sft::renderFieldChain(cfg);
        bsfRequireBounded(taps, "i_wide_dirty_Tube_E1_fc120_fs_" + bsfFmt(fs));
        const BsfAttenResult a =
            bsfAttenuationFromTaps(taps, cfg, 120.0f, "i_Tube_E1_fc120_fs" + bsfFmt(fs), attenCsv, false, nullptr);
        INFO("fs " << fs);
        REQUIRE(a.valid);
        if (!ref.has_value()) {
            ref = w;
            refA = a.band.attenDb;
            csv.line(bsfRow("A_B", fs, "i_Tube_E1_fc120", refA, refA, 0.0, "na", "na", "na"));
            continue;
        }
        bsfCheckRateWidth(*ref, w, fs, csv, wTally);
        const double dA = a.band.attenDb - refA;
        csv.line(bsfRow("A_B", fs, "i_Tube_E1_fc120", refA, a.band.attenDb, dA, "na", "na", "na"));
        INFO("A(B) " << a.band.attenDb << " dB vs 48 kHz " << refA << " dB");
        CHECK(std::abs(dA) <= kBsfRateAttenDbBar);
    }
    bsfReport(wTally);
}

// SC-016 smoke (deviation 6): 44.1 and 96 kHz against 48 kHz, E1 W_n only.
TEST_CASE("BassStereoField_SampleRatesSmoke", "[systems][profundum][stereo-field][sc-016]") {
    std::cout << std::setprecision(6);
    BsfCsv csv("sc016-rates-smoke.csv", bsfRateCsvHeader());
    BsfTally wTally{.name = "sc016_smoke_W_n_within_0.25dB_of_48k"};
    const BsfWidthReading ref = bsfRateWidth(48000.0, false);
    for (const double fs : {44100.0, 96000.0})
        bsfCheckRateWidth(ref, bsfRateWidth(fs, false), fs, csv, wTally);
    bsfReport(wTally);
}

// SC-016 re-prepare arm (per-push, deviation 10; plan S3 "re-preparing at another rate and back leaves no
// trace"): a field-only noise render after prepare(48k) -> prepare(96k) -> prepare(48k) + reset() is
// memcmp-equal to a fresh 48 kHz field's render, outputs and emitted vectors, with MOTION on (each source) and a
// crossover step.
TEST_CASE("BassStereoField_SampleRatesReprepare", "[systems][profundum][stereo-field][sc-016]") {
    const auto n48 = static_cast<std::size_t>(2.0 * kBsfFs);
    const std::pair<std::vector<float>, std::vector<float>> noise = bsfNoise(n48, 0x5EEDu);
    const std::pair<std::vector<float>, std::vector<float>> dirty48 =
        bsfNoise(static_cast<std::size_t>(0.5 * kBsfFs), 0xD1u);
    const std::pair<std::vector<float>, std::vector<float>> dirty96 =
        bsfNoise(static_cast<std::size_t>(0.5 * 96000.0), 0xD2u);
    const auto stepAt = static_cast<std::size_t>(kBsfFs);   // 48000 % 32 == 0: a grid point
    for (const BsfSource src : {BsfSource::Drift, BsfSource::Cycle}) {
        BsfControls c0 = bsfControls(BsfMode::Spread, 1.0f, 0.5f, 60.0f, BsfControls{}.curve);
        c0.motionDepth = 1.0f;
        c0.motionRate = 1.0f;
        c0.motionSource = src;
        BsfControls c1 = c0;
        c1.crossoverHz = 250.0f;
        const std::function<BsfControls(std::size_t)> at = [&](std::size_t s) { return s < stepAt ? c0 : c1; };
        BsfControls dirty = bsfControls(BsfMode::Wide, 0.7f, 1.0f, 250.0f, 1.0f);
        dirty.motionDepth = 1.0f;
        dirty.motionSource = src == BsfSource::Drift ? BsfSource::Cycle : BsfSource::Drift;
        const std::function<BsfControls(std::size_t)> dirtyAt = [&](std::size_t) { return dirty; };

        auto fresh = std::make_unique<BassStereoField>();
        fresh->setControls(c0);
        fresh->prepare(kBsfFs, kBsfHostBlock);
        (*fresh).reset();
        const BsfFieldTrace a = bsfFieldTrace(*fresh, noise.first, noise.second, at);

        auto reused = std::make_unique<BassStereoField>();
        reused->setControls(c0);
        reused->prepare(kBsfFs, kBsfHostBlock);
        (*reused).reset();
        [[maybe_unused]] const BsfFieldTrace d48 = bsfFieldTrace(*reused, dirty48.first, dirty48.second, dirtyAt);
        reused->prepare(96000.0, kBsfHostBlock);
        [[maybe_unused]] const BsfFieldTrace d96 = bsfFieldTrace(*reused, dirty96.first, dirty96.second, dirtyAt);
        reused->setControls(c0);
        reused->prepare(kBsfFs, kBsfHostBlock);
        (*reused).reset();
        const BsfFieldTrace b = bsfFieldTrace(*reused, noise.first, noise.second, at);

        INFO("source " << (src == BsfSource::Drift ? "Drift" : "Cycle"));
        REQUIRE(Sft::bitEqual(a.L, b.L));
        REQUIRE(Sft::bitEqual(a.R, b.R));
        REQUIRE(a.vectors.size() == b.vectors.size());
        REQUIRE(!a.vectors.empty());
        auto panBitsEqual = [](const BsfPan& x, const BsfPan& y) {
            for (std::size_t k = 0; k < x.size(); ++k) {
                if (std::bit_cast<std::uint32_t>(x[k]) != std::bit_cast<std::uint32_t>(y[k]))
                    return false;
            }
            return true;
        };
        for (std::size_t k = 0; k < a.vectors.size(); ++k)
            REQUIRE(panBitsEqual(a.vectors[k], b.vectors[k]));
        // Non-vacuity: MOTION moves the vector, and the stage changes the signal.
        bool vectorMoves = false;
        for (const BsfPan& v : a.vectors)
            vectorMoves = vectorMoves || !panBitsEqual(v, a.vectors.front());
        CHECK(vectorMoves);
        CHECK_FALSE(Sft::bitEqual(a.L, noise.first));
    }
}
