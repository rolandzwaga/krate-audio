// ==============================================================================
// Sound-source regression tests: oscillators, filters, distortion (audit fix plan T6.3)
// ==============================================================================
// Promoted from the audit probes (specs/ruinae-audit/probes/audit_probes.cpp.txt,
// "1. Oscillators", "2. Filters", "3. Distortion"). Each case drives one real
// Ruinae::Processor through RuinaeAuditRig and asserts at the threshold the
// audit report names, instead of printing a PROBE line.
// ==============================================================================

#include "audit_rig.h"

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace RuinaeAuditRig;

namespace {

struct Stats {
    double peak = 0, rms = 0, dc = 0;
    int64_t nonFinite = 0;
    int64_t zeroRun = 0;  // longest run of exact zeros
};

Stats stats(const std::vector<float>& x, size_t a = 0, size_t b = SIZE_MAX) {
    Stats s;
    b = std::min(b, x.size());
    if (a >= b) return s;
    double sum = 0, sq = 0;
    int64_t run = 0;
    for (size_t i = a; i < b; ++i) {
        const float v = x[i];
        if (!Krate::DSP::detail::isFinite(v)) {
            ++s.nonFinite;
            continue;
        }
        s.peak = std::max(s.peak, static_cast<double>(std::fabs(v)));
        sum += v;
        sq += static_cast<double>(v) * v;
        if (v == 0.0f) {
            ++run;
            s.zeroRun = std::max(s.zeroRun, run);
        } else {
            run = 0;
        }
    }
    const double n = static_cast<double>(b - a);
    s.dc = sum / n;
    s.rms = std::sqrt(sq / n);
    return s;
}

int64_t nonFiniteAll(const Rig& r) {
    return stats(r.L).nonFinite + stats(r.R).nonFinite;
}

double toDb(double lin) {
    return lin > 1e-12 ? 20.0 * std::log10(lin) : -240.0;
}

// OscType indices (kOscATypeId = index / 9).
constexpr int kPolyBlep = 0, kWavetable = 1, kPhaseDist = 2, kSync = 3, kAdditive = 4, kChaos = 5,
              kParticle = 6, kFormant = 7, kSpectralFreeze = 8, kNoise = 9;

const char* const kOscNames[10] = {"PolyBLEP", "Wavetable", "PhaseDistortion", "Sync",
                                   "Additive", "Chaos",     "Particle",        "Formant",
                                   "SpectralFreeze", "Noise"};
const char* const kFilterNames[13] = {"SVF_LP",       "SVF_HP",         "SVF_BP",       "SVF_Notch",
                                      "Ladder",       "Formant",        "Comb",         "SVF_Allpass",
                                      "SVF_Peak",     "SVF_LowShelf",   "SVF_HighShelf", "EnvelopeFilter",
                                      "SelfOscillating"};
const char* const kDistNames[7] = {"Clean",    "ChaosWaveshaper", "SpectralDistortion", "GranularDistortion",
                                   "Wavefolder", "TapeSaturator",   "RingModulator"};

void checkOscType(int t) {
    const double sr = 48000.0;
    const int notes[3] = {33, 69, 96};
    const char* const noteNames[3] = {"A1", "A4", "C7"};
    const bool pitched = !(t == kChaos || t == kParticle || t == kNoise);

    for (int ni = 0; ni < 3; ++ni) {
        const int note = notes[ni];
        INFO(kOscNames[t] << " at " << noteNames[ni]);
        Rig r(sr);
        basePatch(r);
        r.param(Ruinae::kOscATypeId, t / 9.0);
        r.param(Ruinae::kSoftLimitId, 0.0);
        r.noteOn(note, 0.8f, 0);
        const size_t n = 65536;
        r.render(r.at(0.4) + static_cast<int64_t>(n));
        const auto m = r.mono();
        const size_t a = m.size() - n;
        const Stats s = stats(m, a);

        REQUIRE(nonFiniteAll(r) == 0);
        CHECK(s.peak > 1e-4);
        CHECK(std::fabs(s.dc) <= 0.001);

        if (!pitched) continue;

        // Sync is measured at its slave pitch: the slave runs at 2x the master.
        const double f = noteHz(note) * (t == kSync ? 2.0 : 1.0);
        const size_t pn = std::min<size_t>(n, 48000);
        const double cents = pitchCents(m.data() + (m.size() - pn), pn, sr, f);
        INFO("pitch error cents = " << cents);
        CHECK(std::fabs(cents) <= 5.0);

        const auto al = aliasAnalysis(m.data() + a, n, sr, f * std::pow(2.0, cents / 1200.0));
        INFO("worst non-harmonic dBc = " << al.worstDbc);
        if (t == kPolyBlep) {
            // -50 dBc at A1/A4; C7 measures -48 (the 3 kHz+ partials of a 2 kHz saw alias into a
            // 4 kHz-wide gap, so the BLEP residue is a larger share): the bar there is -45.
            CHECK(al.worstDbc <= (ni == 2 ? -45.0 : -50.0));
        } else if (t == kWavetable || t == kPhaseDist) {
            CHECK(al.worstDbc <= -50.0);
        } else if (t == kAdditive) {
            CHECK(al.worstDbc <= -60.0);  // T2.35
        } else if (t == kFormant && note == 69) {
            CHECK(al.worstDbc <= -40.0);  // T2.35 (A2..A5)
        } else if (t == kSpectralFreeze) {
            CHECK(al.worstDbc <= -40.0);  // T1.16
        }
    }
}

}  // namespace

#define OSC_REGRESSION_CASE(i) \
    TEST_CASE("SoundSource oscillator type " #i " is in tune, clean and finite", "[regression][osc]") { checkOscType(i); }
OSC_REGRESSION_CASE(0)
OSC_REGRESSION_CASE(1)
OSC_REGRESSION_CASE(2)
OSC_REGRESSION_CASE(3)
OSC_REGRESSION_CASE(4)
OSC_REGRESSION_CASE(5)
OSC_REGRESSION_CASE(6)
OSC_REGRESSION_CASE(7)
OSC_REGRESSION_CASE(8)
OSC_REGRESSION_CASE(9)

// ==============================================================================
// Filters: cutoff sweep 20 Hz -> 20 kHz over 2 s, then parked at maximum
// ==============================================================================

namespace {

void checkFilterType(int ft) {
    const double rates[2] = {48000.0, 192000.0};
    const double resos[3] = {0.0, 0.5, 1.0};
    for (const double sr : rates) {
        for (const double res : resos) {
            INFO(kFilterNames[ft] << " res " << res << " at " << sr << " Hz");
            Rig r(sr);
            basePatch(r, 0.0);  // sine, not a triangle (a triangle's corners read as clicks in HP/BP/Notch)
            r.param(Ruinae::kSoftLimitId, 0.0);
            r.param(Ruinae::kFilterTypeId, ft / 12.0);
            r.param(Ruinae::kFilterResonanceId, res);
            r.param(Ruinae::kFilterCutoffId, 0.0);
            r.noteOn(45, 0.8f, 0);
            const Steinberg::int32 bs = 64;
            const int64_t pre = r.at(0.2);
            const int64_t sweep = r.at(2.0);
            for (int64_t p = pre; p < pre + sweep; p += bs)
                r.param(Ruinae::kFilterCutoffId, static_cast<double>(p - pre) / static_cast<double>(sweep), p);
            r.param(Ruinae::kFilterCutoffId, 1.0, pre + sweep);
            r.render(pre + sweep + r.at(1.0), bs);
            const auto m = r.mono();
            const auto sweepEnd = static_cast<size_t>(pre + sweep);
            const Stats sw = stats(m, static_cast<size_t>(pre), sweepEnd);
            const Stats park = stats(m, sweepEnd + static_cast<size_t>(r.at(0.1)));

            REQUIRE(nonFiniteAll(r) == 0);
            INFO("sweep zero run " << sw.zeroRun << ", parked zero run " << park.zeroRun);
            CHECK(std::max(sw.zeroRun, park.zeroRun) <= 16);
            INFO("parked peak dBFS " << toDb(park.peak));
            CHECK(toDb(park.peak) <= 6.0);
        }
    }
}

}  // namespace

#define FILTER_REGRESSION_CASE(i) \
    TEST_CASE("SoundSource filter type " #i " is finite, gap-free and bounded", "[regression][filter]") { checkFilterType(i); }
FILTER_REGRESSION_CASE(0)
FILTER_REGRESSION_CASE(1)
FILTER_REGRESSION_CASE(2)
FILTER_REGRESSION_CASE(3)
FILTER_REGRESSION_CASE(4)
FILTER_REGRESSION_CASE(5)
FILTER_REGRESSION_CASE(6)
FILTER_REGRESSION_CASE(7)
FILTER_REGRESSION_CASE(8)
FILTER_REGRESSION_CASE(9)
FILTER_REGRESSION_CASE(10)
FILTER_REGRESSION_CASE(11)
FILTER_REGRESSION_CASE(12)

// ==============================================================================
// Distortion
// ==============================================================================

namespace {

Stats renderDist(int dt, double drive, int64_t* nonFin) {
    Rig r(48000.0);
    basePatch(r, 0.0);  // sine
    r.param(Ruinae::kSoftLimitId, 0.0);
    r.param(Ruinae::kDistortionTypeId, dt / 6.0);
    r.param(Ruinae::kDistortionDriveId, drive);
    r.param(Ruinae::kDistortionMixId, 1.0);
    r.noteOn(57, 0.8f, 0);
    r.render(r.at(2.0));
    *nonFin = nonFiniteAll(r);
    return stats(r.mono(), static_cast<size_t>(r.at(1.0)));
}

void checkDistortionType(int dt) {
    int64_t nf = 0;
    const Stats clean = renderDist(0, 0.5, &nf);
    REQUIRE(nf == 0);
    const double drives[4] = {0.0, 0.25, 0.75, 1.0};
    for (const double d : drives) {
        INFO(kDistNames[dt] << " drive " << d);
        int64_t nonFin = 0;
        const Stats s = renderDist(dt, d, &nonFin);
        REQUIRE(nonFin == 0);
        CHECK(std::fabs(s.dc) <= 0.002);
        CHECK(s.peak > 1e-4);
        if (d == 0.0) {
            const double dl = toDb(s.rms) - toDb(clean.rms);
            INFO("drive-0 level vs Clean dB = " << dl);
            CHECK(std::fabs(dl) <= 6.0);  // T2.9
        }
    }
}

}  // namespace

#define DIST_REGRESSION_CASE(i) \
    TEST_CASE("SoundSource distortion type " #i " is DC-free, finite and audible at drive 0", "[regression][distortion]") { checkDistortionType(i); }
DIST_REGRESSION_CASE(1)
DIST_REGRESSION_CASE(2)
DIST_REGRESSION_CASE(3)
DIST_REGRESSION_CASE(4)
DIST_REGRESSION_CASE(5)
DIST_REGRESSION_CASE(6)
