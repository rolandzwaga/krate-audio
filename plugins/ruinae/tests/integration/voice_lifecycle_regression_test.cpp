// ==============================================================================
// Voice lifecycle and timing regression tests (audit fix plan T6.2)
// ==============================================================================
// Promoted from the audit probes (specs/ruinae-audit/probes/audit_probes.cpp.txt,
// "5. Voice lifecycle", "8. Sample accuracy", "11. Pitch bend and MPE",
// "14. Sample-rate consistency"). Each case drives one real Ruinae::Processor
// through RuinaeAuditRig and asserts at the threshold the audit report names.
// ==============================================================================

#include "audit_rig.h"

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace RuinaeAuditRig;

namespace {

int64_t countNonFinite(const std::vector<float>& x) {
    int64_t n = 0;
    for (const float v : x)
        if (!Krate::DSP::detail::isFinite(v)) ++n;
    return n;
}

int64_t nonFiniteAll(const Rig& r) {
    return countNonFinite(r.L) + countNonFinite(r.R);
}

size_t firstAbove(const std::vector<float>& x, size_t from, double thr) {
    for (size_t i = from; i < x.size(); ++i)
        if (std::fabs(x[i]) > thr) return i;
    return SIZE_MAX;
}

double rmsOf(const std::vector<float>& x, size_t a, size_t b) {
    b = std::min(b, x.size());
    if (a >= b) return 0.0;
    double q = 0.0;
    for (size_t i = a; i < b; ++i) q += static_cast<double>(x[i]) * x[i];
    return std::sqrt(q / static_cast<double>(b - a));
}

double toDb(double lin) { return lin > 1e-12 ? 20.0 * std::log10(lin) : -240.0; }

size_t pow2Below(size_t n) {
    size_t p = 1;
    while (p * 2 <= n) p *= 2;
    return p;
}

/// 1 ms RMS envelope.
std::vector<double> rmsEnv(const std::vector<float>& x, double sr, double winSec = 0.001) {
    const auto w = std::max<size_t>(1, static_cast<size_t>(winSec * sr));
    std::vector<double> e;
    for (size_t s = 0; s + w <= x.size(); s += w) {
        double q = 0;
        for (size_t i = s; i < s + w; ++i) q += static_cast<double>(x[i]) * x[i];
        e.push_back(std::sqrt(q / static_cast<double>(w)));
    }
    return e;
}

/// Clicks inside a continuous render: 2 ms frames whose max |dx| exceeds 4x the
/// median of the neighbouring frames (+-15), with frame peak above -60 dBFS.
int localClickFrames(const std::vector<float>& x, double sr) {
    const size_t fr = static_cast<size_t>(0.002 * sr);
    std::vector<double> fm, pk;
    for (size_t s = 1; s + fr <= x.size(); s += fr) {
        double m = 0, p = 0;
        for (size_t i = s; i < s + fr; ++i) {
            m = std::max(m, static_cast<double>(std::fabs(x[i] - x[i - 1])));
            p = std::max(p, static_cast<double>(std::fabs(x[i])));
        }
        fm.push_back(m);
        pk.push_back(p);
    }
    int clicks = 0;
    std::vector<double> nb;
    for (size_t i = 0; i < fm.size(); ++i) {
        if (pk[i] < 1e-3) continue;
        nb.clear();
        for (size_t j = (i > 15 ? i - 15 : 0); j < std::min(fm.size(), i + 16); ++j)
            if (j != i) nb.push_back(fm[j]);
        if (nb.empty()) continue;
        std::nth_element(nb.begin(), nb.begin() + static_cast<long>(nb.size() / 2), nb.end());
        const double med = std::max(nb[nb.size() / 2], 1e-7);
        if (fm[i] / med > 4.0) ++clicks;
    }
    return clicks;
}

/// Instantaneous frequency per period (upward zero crossings, linear interpolation).
std::vector<std::pair<double, double>> zcFreq(const std::vector<float>& x, size_t a, size_t b, double sr) {
    std::vector<std::pair<double, double>> out;
    double prev = -1;
    for (size_t i = std::max<size_t>(a, 1); i < std::min(b, x.size()); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = static_cast<double>(i - 1) +
                             static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
            if (prev >= 0) out.emplace_back(t, sr / (t - prev));
            prev = t;
        }
    }
    return out;
}

}  // namespace

// -----------------------------------------------------------------------------
// sample accuracy
// -----------------------------------------------------------------------------

TEST_CASE("Lifecycle regression: note-on sample accuracy is identical for offsets 0/1/100/511",
          "[integration][regression]") {
    const double sr = 48000.0;
    const Steinberg::int32 bs = 512;
    const int offs[4] = {0, 1, 100, bs - 1};
    for (int fx = 0; fx < 2; ++fx) {
        long base = 0;
        for (const int o : offs) {
            Rig r(sr);
            basePatch(r);
            r.param(Ruinae::kAmpEnvAttackId, 0.0);
            if (fx) {
                r.param(Ruinae::kDelayEnabledId, 1.0);
                r.param(Ruinae::kDelayMixId, 0.5);
                r.param(Ruinae::kReverbEnabledId, 1.0);
                r.param(Ruinae::kHarmonizerEnabledId, 1.0);
            }
            const int64_t expected = 2 * bs + o;
            r.noteOn(69, 0.8f, expected);
            r.render(r.at(0.5), bs);
            const auto m = r.mono();
            const size_t first = firstAbove(m, 0, 1e-6);
            INFO((fx ? "FX on" : "FX off") << ", offset " << o);
            REQUIRE(first != SIZE_MAX);
            const long delta = static_cast<long>(first) - static_cast<long>(expected);
            if (o == 0) base = delta;
            CHECK(delta >= 0);
            CHECK(delta == base);
            CHECK(nonFiniteAll(r) == 0);
        }
    }
}

// -----------------------------------------------------------------------------
// voice stealing
// -----------------------------------------------------------------------------

TEST_CASE("Lifecycle regression: voice stealing is click-free and every voice releases",
          "[integration][regression]") {
    const double sr = 48000.0;
    const int polys[4] = {1, 4, 8, 16};
    for (const int P : polys) {
        Rig r(sr);
        basePatch(r, 0.0);
        r.param(Ruinae::kPolyphonyId, (P - 1) / 15.0);
        r.param(Ruinae::kAmpEnvReleaseId, envNorm(50.0));
        const int count = P + 4;
        const int64_t step = r.at(0.05);
        for (int k = 0; k < count; ++k) r.noteOn(48 + 2 * (k % 24), 0.7f, 1000 + k * step);
        const int64_t offAt = 1000 + count * step + r.at(0.3);
        for (int k = 0; k < count; ++k) r.noteOff(48 + 2 * (k % 24), offAt);
        r.render(offAt + r.at(0.5));
        const auto m = r.mono();

        double worstSteal = 0;
        for (int k = P; k < count; ++k) {
            const auto c = clickAt(m, static_cast<size_t>(1000 + k * step), static_cast<size_t>(0.004 * sr),
                                   static_cast<size_t>(0.015 * sr));
            worstSteal = std::max(worstSteal, c.ratio);
        }
        INFO("polyphony " << P << ", worst jump ratio at steal events " << worstSteal);
        CHECK(worstSteal <= 1.5);
        CHECK(localClickFrames(m, sr) == 0);
        CHECK(r.proc->engine().getActiveVoiceCount() == 0);
        CHECK(nonFiniteAll(r) == 0);
    }
}

// -----------------------------------------------------------------------------
// fast retrigger
// -----------------------------------------------------------------------------

TEST_CASE("Lifecycle regression: fast retrigger of one note has no click", "[integration][regression]") {
    const double sr = 48000.0;
    const int polys[2] = {8, 1};
    for (const int P : polys) {
        Rig r(sr);
        basePatch(r, 0.0);
        r.param(Ruinae::kPolyphonyId, (P - 1) / 15.0);
        r.param(Ruinae::kAmpEnvReleaseId, envNorm(5.0));
        const int64_t step = 1440;  // 30 ms
        for (int k = 0; k < 20; ++k) {
            r.noteOn(69, 0.8f, 1000 + k * step);
            r.noteOff(69, 1000 + k * step + 1000);
        }
        r.render(1000 + 20 * step + r.at(0.5));
        const auto m = r.mono();
        double worst = 0;
        for (int k = 0; k < 20; ++k)
            worst = std::max(worst, clickAt(m, static_cast<size_t>(1000 + k * step), 200, 400).ratio);
        INFO("polyphony " << P << ", worst jump ratio at note-ons " << worst);
        CHECK(worst <= 1.5);
        CHECK(r.proc->engine().getActiveVoiceCount() == 0);
        CHECK(nonFiniteAll(r) == 0);
    }
}

// -----------------------------------------------------------------------------
// mono legato portamento
// -----------------------------------------------------------------------------

TEST_CASE("Lifecycle regression: mono legato portamento glides monotonically and lands on pitch",
          "[integration][regression]") {
    const double sr = 48000.0;
    for (int pm = 0; pm < 2; ++pm) {
        Rig r(sr);
        basePatch(r, 0.0);
        r.param(Ruinae::kVoiceModeId, 1.0);
        r.param(Ruinae::kMonoLegatoId, 1.0);
        r.param(Ruinae::kMonoPortamentoTimeId, std::cbrt(200.0 / 5000.0));
        r.param(Ruinae::kMonoPortaModeId, static_cast<double>(pm));
        r.noteOn(60, 0.8f, 0);
        const int64_t t2 = r.at(0.5);
        r.noteOn(67, 0.8f, t2);
        r.noteOff(60, r.at(0.7));
        r.noteOff(67, r.at(1.6));
        r.render(r.at(2.2));
        const auto m = r.mono();

        const auto zc = zcFreq(m, static_cast<size_t>(t2), static_cast<size_t>(r.at(1.0)), sr);
        int nonMono = 0;
        for (size_t i = 1; i < zc.size(); ++i)
            if (zc[i].second < zc[i - 1].second * 0.997) ++nonMono;
        const double target = noteHz(67);
        const double cents =
            pitchCents(m.data() + r.at(1.1), static_cast<size_t>(r.at(0.4)), sr, target, 150.0);

        INFO("porta mode " << (pm == 0 ? "Always" : "LegatoOnly") << ", non-monotonic periods " << nonMono
                           << ", final pitch error " << cents << " cents");
        REQUIRE(zc.size() > 10);
        CHECK(nonMono == 0);
        CHECK(std::fabs(cents) <= 5.0);
        CHECK(r.proc->engine().getActiveVoiceCount() == 0);
        CHECK(nonFiniteAll(r) == 0);
    }
}

// -----------------------------------------------------------------------------
// voice edges
// -----------------------------------------------------------------------------

TEST_CASE("Lifecycle regression: attack 0 and release 0 edges do not click", "[integration][regression]") {
    const double sr = 48000.0;
    const double atks[2] = {0.0, 1.0};
    for (const double atkMs : atks) {
        Rig r(sr);
        basePatch(r, 0.0);
        r.param(Ruinae::kAmpEnvAttackId, envNorm(atkMs));
        r.param(Ruinae::kAmpEnvReleaseId, 0.0);
        const int64_t on = 1000 + 37;
        const int64_t off = on + r.at(0.5) + 123;
        r.noteOn(69, 0.8f, on);
        r.noteOff(69, off);
        r.render(off + r.at(0.5));
        const auto m = r.mono();
        const size_t win = static_cast<size_t>(0.005 * sr);
        const size_t ref = static_cast<size_t>(0.02 * sr);
        const auto cOn = clickAt(m, static_cast<size_t>(on), win, ref);
        const auto cOff = clickAt(m, static_cast<size_t>(off), win, ref);
        INFO("attack " << atkMs << " ms, release 0 ms: note-on jump ratio " << cOn.ratio
                       << ", note-off jump ratio " << cOff.ratio);
        CHECK(cOn.ratio <= 1.5);
        CHECK(cOff.ratio <= 1.5);
        CHECK(r.proc->engine().getActiveVoiceCount() == 0);
        CHECK(nonFiniteAll(r) == 0);
    }
}

// -----------------------------------------------------------------------------
// sample rate consistency
// -----------------------------------------------------------------------------

TEST_CASE("Lifecycle regression: pitch, level and envelope times agree from 44.1 to 192 kHz",
          "[integration][regression]") {
    const double rates[4] = {44100.0, 48000.0, 96000.0, 192000.0};
    struct Meas {
        double cents = 0, rmsDb = 0, atkMs = 0, relMs = 0;
    };
    Meas meas[4];
    for (int i = 0; i < 4; ++i) {
        const double sr = rates[i];
        Rig r(sr, 2048);
        basePatch(r);
        r.param(Ruinae::kFilterCutoffId, 0.7);
        r.param(Ruinae::kFilterResonanceId, 0.2);
        r.param(Ruinae::kAmpEnvAttackId, envNorm(100));
        r.param(Ruinae::kAmpEnvDecayId, envNorm(100));
        r.param(Ruinae::kAmpEnvSustainId, 1.0);
        r.param(Ruinae::kAmpEnvReleaseId, envNorm(300));
        r.param(Ruinae::kSoftLimitId, 0.0);
        r.noteOn(57, 0.8f, r.at(0.1));
        r.noteOff(57, r.at(1.6));
        r.render(r.at(2.6));
        const auto m = r.mono();
        REQUIRE(nonFiniteAll(r) == 0);

        const auto env = rmsEnv(m, sr);
        const double steady = rmsOf(m, static_cast<size_t>(r.at(1.0)), static_cast<size_t>(r.at(1.5)));
        auto tCross = [&](double fromSec, double level, bool rising) {
            for (size_t k = static_cast<size_t>(fromSec * 1000.0); k < env.size(); ++k)
                if (rising ? env[k] >= level : env[k] <= level)
                    return static_cast<double>(k) - fromSec * 1000.0;
            return -1.0;
        };
        meas[i].cents =
            pitchCents(m.data() + r.at(1.0), pow2Below(static_cast<size_t>(r.at(0.5))), sr, noteHz(57));
        meas[i].rmsDb = toDb(steady);
        meas[i].atkMs = tCross(0.1, 0.9 * steady, true);
        meas[i].relMs = tCross(1.6, 0.1 * steady, false);
    }

    const Meas& ref = meas[1];  // 48 kHz
    REQUIRE(ref.atkMs > 0.0);
    REQUIRE(ref.relMs > 0.0);
    for (int i = 0; i < 4; ++i) {
        INFO("sample rate " << rates[i] << ": pitch " << meas[i].cents << " cents, RMS " << meas[i].rmsDb
                            << " dB, attack " << meas[i].atkMs << " ms, release " << meas[i].relMs << " ms");
        CHECK(std::fabs(meas[i].cents) <= 5.0);
        CHECK(std::fabs(meas[i].rmsDb - ref.rmsDb) <= 1.0);
        CHECK(std::fabs(meas[i].atkMs - ref.atkMs) / ref.atkMs <= 0.10);
        CHECK(std::fabs(meas[i].relMs - ref.relMs) / ref.relMs <= 0.10);
    }
}

// -----------------------------------------------------------------------------
// MPE tuned / untouched note
// -----------------------------------------------------------------------------

TEST_CASE("Lifecycle regression: MPE tuning moves only the tuned note", "[integration][regression]") {
    const double sr = 48000.0;
    Rig r(sr);
    basePatch(r, 0.0);
    r.noteOn(69, 0.8f, 0, 1);
    r.noteOn(76, 0.8f, 0, 2);
    // +1 semitone on note id 1 (tuning expression spans +-120 semitones, normalised)
    r.expr(1, Steinberg::Vst::kTuningTypeID, 0.5 + 1.0 / 240.0, r.at(0.3));
    r.render(r.at(1.6));
    const auto m = r.mono();
    const size_t n = static_cast<size_t>(r.at(0.8));
    const double c1 = pitchCents(m.data() + r.at(0.8), n, sr, noteHz(70), 60.0);
    const double c2 = pitchCents(m.data() + r.at(0.8), n, sr, noteHz(76), 60.0);
    INFO("tuned note vs A#4 " << c1 << " cents, untouched note vs E5 " << c2 << " cents");
    CHECK(std::fabs(c1) <= 3.0);
    CHECK(std::fabs(c2) <= 3.0);
    CHECK(nonFiniteAll(r) == 0);
}
