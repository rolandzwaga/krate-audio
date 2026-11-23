// ==============================================================================
// Effects regression tests (audit fix plan T6.4)
// ==============================================================================
// Promoted from the audit probes (specs/ruinae-audit/probes/audit_probes.cpp.txt,
// "4. Silence", "6. Tail and idle", "9. Parameter automation mid-note",
// "10. Effects timing"). Each case drives one real Ruinae::Processor through
// RuinaeAuditRig and asserts at the threshold the audit plan names.
// ==============================================================================

#include "audit_rig.h"

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace RuinaeAuditRig;

namespace {

using Steinberg::Vst::ParamID;

constexpr const char* kDelayNames[5] = {"Digital", "Tape", "PingPong", "Granular", "Spectral"};
constexpr const char* kModTypeNames[4] = {"None", "Phaser", "Flanger", "Chorus"};

int64_t countNonFinite(const std::vector<float>& x) {
    int64_t n = 0;
    for (const float v : x)
        if (!Krate::DSP::detail::isFinite(v)) ++n;
    return n;
}

int64_t nonFiniteAll(const Rig& r) {
    return countNonFinite(r.L) + countNonFinite(r.R);
}

double toDb(double lin) { return lin > 1e-12 ? 20.0 * std::log10(lin) : -240.0; }

struct Stats {
    double peak = 0, rms = 0;
    int64_t zeroRun = 0;
};

Stats statsOf(const std::vector<float>& x, size_t a = 0, size_t b = SIZE_MAX) {
    Stats s;
    b = std::min(b, x.size());
    if (a >= b) return s;
    double sq = 0;
    int64_t run = 0;
    for (size_t i = a; i < b; ++i) {
        const float v = x[i];
        s.peak = std::max(s.peak, static_cast<double>(std::fabs(v)));
        sq += static_cast<double>(v) * v;
        if (v == 0.0f) {
            ++run;
            s.zeroRun = std::max(s.zeroRun, run);
        } else {
            run = 0;
        }
    }
    s.rms = std::sqrt(sq / static_cast<double>(b - a));
    return s;
}

size_t firstAbove(const std::vector<float>& x, size_t from, double thr) {
    for (size_t i = from; i < x.size(); ++i)
        if (std::fabs(x[i]) > thr) return i;
    return SIZE_MAX;
}

/// x^2 energy centroid (sub-sample) of x[lo, hi). Returns -1 when the window is silent.
double energyCentroid(const std::vector<float>& x, int64_t lo, int64_t hi) {
    lo = std::max<int64_t>(lo, 0);
    hi = std::min<int64_t>(hi, static_cast<int64_t>(x.size()));
    double num = 0.0, den = 0.0;
    for (int64_t i = lo; i < hi; ++i) {
        const double e = static_cast<double>(x[static_cast<size_t>(i)]) * x[static_cast<size_t>(i)];
        num += static_cast<double>(i) * e;
        den += e;
    }
    return den > 1e-12 ? num / den : -1.0;
}

// ------------------------------------------------------------------------------
// silence fx
// ------------------------------------------------------------------------------

/// No note is ever played: the output must be silent (RMS <= -120 dBFS), finite.
void requireSilent(const std::string& label, const std::function<void(Rig&)>& cfg) {
    INFO(label);
    Rig r(48000.0);
    basePatch(r);
    cfg(r);
    REQUIRE_NOTHROW(r.render(r.at(2.0)));
    const Stats s = statsOf(r.mono());
    INFO("RMS dBFS " << toDb(s.rms) << ", peak dBFS " << toDb(s.peak));
    CHECK(toDb(s.rms) <= -120.0);
    CHECK(nonFiniteAll(r) == 0);
}

}  // namespace

TEST_CASE("Audit regression: silence with every FX combination and no note",
          "[integration][regression][fx]") {
    requireSilent("dry", [](Rig&) {});
    for (int d = 0; d < 5; ++d) {
        requireSilent(std::string("delay:") + kDelayNames[d], [d](Rig& r) {
            r.param(Ruinae::kDelayEnabledId, 1.0);
            r.param(Ruinae::kDelayTypeId, d / 4.0);
            r.param(Ruinae::kDelayFeedbackId, 0.6);
            r.param(Ruinae::kDelayMixId, 0.5);
        });
    }
    // Tape at Wear 0 and Age 0 (T3.6a): no hiss.
    requireSilent("delay:Tape wear 0 age 0", [](Rig& r) {
        r.param(Ruinae::kDelayEnabledId, 1.0);
        r.param(Ruinae::kDelayTypeId, 1.0 / 4.0);
        r.param(Ruinae::kDelayFeedbackId, 0.6);
        r.param(Ruinae::kDelayMixId, 1.0);
        r.param(Ruinae::kDelayTapeWearId, 0.0);
        r.param(Ruinae::kDelayTapeAgeId, 0.0);
    });
    for (int rv = 0; rv < 2; ++rv) {
        requireSilent(rv == 0 ? "reverb:Plate" : "reverb:Hall(FDN)", [rv](Rig& r) {
            r.param(Ruinae::kReverbEnabledId, 1.0);
            r.param(Ruinae::kReverbTypeId, static_cast<double>(rv));
            r.param(Ruinae::kReverbMixId, 0.5);
        });
    }
    for (int mt = 1; mt < 4; ++mt) {
        requireSilent(std::string("mod:") + kModTypeNames[mt],
                      [mt](Rig& r) { r.param(Ruinae::kModulationTypeId, mt / 3.0); });
    }
    requireSilent("harmonizer", [](Rig& r) { r.param(Ruinae::kHarmonizerEnabledId, 1.0); });
    for (int d = 0; d < 5; ++d) {
        requireSilent(std::string("all-fx:delay=") + kDelayNames[d], [d](Rig& r) {
            r.param(Ruinae::kDelayEnabledId, 1.0);
            r.param(Ruinae::kDelayTypeId, d / 4.0);
            r.param(Ruinae::kDelayFeedbackId, 1.0);
            r.param(Ruinae::kReverbEnabledId, 1.0);
            r.param(Ruinae::kReverbTypeId, 1.0);
            r.param(Ruinae::kModulationTypeId, 2.0 / 3.0);
            r.param(Ruinae::kHarmonizerEnabledId, 1.0);
            r.param(Ruinae::kGlobalFilterEnabledId, 1.0);
            r.param(Ruinae::kGlobalFilterResonanceId, 1.0);
        });
    }
}

// ------------------------------------------------------------------------------
// tails
// ------------------------------------------------------------------------------

namespace {

struct TailResult {
    double voicesZeroSec = -1.0;  // seconds after note-off until no voice is active
    double lastSecondPeak = 0.0;
    int64_t nonFinite = 0;
    double peak = 0.0;
};

TailResult runTail(double renderSec, const std::function<void(Rig&)>& cfg) {
    const double sr = 48000.0;
    Rig r(sr);
    basePatch(r);
    cfg(r);
    for (int n : {60, 64, 67}) r.noteOn(n, 0.8f, 0);
    const int64_t off = r.at(1.0);
    for (int n : {60, 64, 67}) r.noteOff(n, off);
    int64_t voicesZeroAt = -1;
    r.onBlock = [&](int64_t start, Steinberg::int32 len) {
        if (start >= off && voicesZeroAt < 0 && r.proc->engine().getActiveVoiceCount() == 0)
            voicesZeroAt = start + len;
    };
    r.render(r.at(renderSec));
    const auto m = r.mono();
    TailResult t;
    if (voicesZeroAt >= 0) t.voicesZeroSec = static_cast<double>(voicesZeroAt - off) / sr;
    t.lastSecondPeak = statsOf(m, m.size() - static_cast<size_t>(sr)).peak;
    t.peak = statsOf(m).peak;
    t.nonFinite = nonFiniteAll(r);
    return t;
}

}  // namespace

TEST_CASE("Audit regression: voices end within 0.2 s of note-off, dry and with reverb",
          "[integration][regression][fx]") {
    SECTION("dry") {
        const TailResult t = runTail(2.5, [](Rig&) {});
        REQUIRE(t.voicesZeroSec >= 0.0);
        CHECK(t.voicesZeroSec <= 0.2);
        CHECK(t.nonFinite == 0);
    }
    for (int rv = 0; rv < 2; ++rv) {
        DYNAMIC_SECTION(std::string(rv == 0 ? "reverb Plate" : "reverb Hall")) {
            const TailResult t = runTail(2.5, [rv](Rig& r) {
                r.param(Ruinae::kReverbEnabledId, 1.0);
                r.param(Ruinae::kReverbTypeId, static_cast<double>(rv));
                r.param(Ruinae::kReverbMixId, 0.5);
                r.param(Ruinae::kReverbSizeId, 1.0);
            });
            REQUIRE(t.voicesZeroSec >= 0.0);
            CHECK(t.voicesZeroSec <= 0.2);
            CHECK(t.nonFinite == 0);
        }
    }
}

TEST_CASE("Audit regression: reverb tail falls below -100 dBFS within 30 s",
          "[integration][regression][fx][long]") {
    for (int rv = 0; rv < 2; ++rv) {
        DYNAMIC_SECTION(std::string(rv == 0 ? "reverb Plate mix 0.5" : "reverb Hall mix 0.5")) {
            const TailResult t = runTail(31.0, [rv](Rig& r) {
                r.param(Ruinae::kReverbEnabledId, 1.0);
                r.param(Ruinae::kReverbTypeId, static_cast<double>(rv));
                r.param(Ruinae::kReverbMixId, 0.5);
                r.param(Ruinae::kReverbSizeId, 1.0);
            });
            INFO("peak dBFS in the last second " << toDb(t.lastSecondPeak));
            CHECK(toDb(t.lastSecondPeak) <= -100.0);
            CHECK(t.nonFinite == 0);
        }
    }
}

TEST_CASE("Audit regression: delay at maximum feedback stays bounded with the limiter on",
          "[integration][regression][fx]") {
    // Characterisation: feedback 1.0 (normalised, 1.2 real) plus reverb. The soft
    // limiter keeps the output at or below 0 dBFS; nothing goes non-finite.
    for (int d = 0; d < 5; ++d) {
        DYNAMIC_SECTION(std::string("delay ") + kDelayNames[d]) {
            const TailResult t = runTail(10.0, [d](Rig& r) {
                r.param(Ruinae::kDelayEnabledId, 1.0);
                r.param(Ruinae::kDelayTypeId, d / 4.0);
                r.param(Ruinae::kDelayFeedbackId, 1.0);
                r.param(Ruinae::kDelayMixId, 0.5);
                r.param(Ruinae::kDelayTimeId, (300.0 - 1.0) / 4999.0);
                r.param(Ruinae::kDelaySyncId, 0.0);
                r.param(Ruinae::kReverbEnabledId, 1.0);
                r.param(Ruinae::kReverbMixId, 0.3);
                r.param(Ruinae::kReverbFreezeId, 0.0);
            });
            INFO("peak dBFS " << toDb(t.peak));
            CHECK(t.peak <= 1.0);
            CHECK(t.nonFinite == 0);
        }
    }
}

// ------------------------------------------------------------------------------
// latency and echo timing
// ------------------------------------------------------------------------------

namespace {

constexpr double kBlipSr = 48000.0;
constexpr int64_t kBlipOn = 4800;
// The Tape delay glides its time through the motor inertia after a change, so the
// echo timing is measured on a blip played after the glide has settled.
constexpr int64_t kSettledBlipOn = 96000;  // 2 s
constexpr int kFftSize = 1024;  // SpectralDelay frame
constexpr int kHop = 512;       // 50 % overlap

void blipPatch(Rig& r) {
    basePatch(r, 0.0);
    r.param(Ruinae::kAmpEnvAttackId, 0.0);
    r.param(Ruinae::kAmpEnvDecayId, envNorm(15));
    r.param(Ruinae::kAmpEnvSustainId, 0.0);
    r.param(Ruinae::kAmpEnvReleaseId, envNorm(10));
}

void playBlip(Rig& r, int64_t on = kBlipOn) {
    r.noteOn(81, 0.9f, on);
    r.noteOff(81, on + 960);
}

/// Centroid of the dry blip, the reference every echo is measured against.
/// Returned relative to the note-on at @p on.
double dryBlipCentroid(int64_t on = kBlipOn) {
    Rig r(kBlipSr);
    blipPatch(r);
    playBlip(r, on);
    r.render(on + r.at(0.5));
    return energyCentroid(r.mono(), on - 480, on + 4800) - static_cast<double>(on);
}

}  // namespace

TEST_CASE("Audit regression: fx latency and echo timing", "[integration][regression][fx]") {
    const double dryC = dryBlipCentroid(kSettledBlipOn);
    REQUIRE(dryC > 0.0);
    const long dryOnset = static_cast<long>(firstAbove(
        [] {
            Rig r(kBlipSr);
            blipPatch(r);
            playBlip(r);
            r.render(r.at(0.5));
            return r.mono();
        }(),
        0, 1e-6));

    for (int d = 0; d < 5; ++d) {
        DYNAMIC_SECTION(std::string("dry path with delay enabled at mix 0: ") + kDelayNames[d]) {
            Rig r(kBlipSr);
            blipPatch(r);
            r.param(Ruinae::kDelayEnabledId, 1.0);
            r.param(Ruinae::kDelayTypeId, d / 4.0);
            r.param(Ruinae::kDelayMixId, 0.0);
            playBlip(r);
            r.render(r.at(0.5));
            const long onset = static_cast<long>(firstAbove(r.mono(), 0, 1e-6));
            CHECK(onset == dryOnset);
        }

        DYNAMIC_SECTION(std::string("first echo at 250 ms: ") + kDelayNames[d]) {
            Rig r(kBlipSr);
            blipPatch(r);
            r.param(Ruinae::kDelayEnabledId, 1.0);
            r.param(Ruinae::kDelayTypeId, d / 4.0);
            r.param(Ruinae::kDelayMixId, 1.0);
            r.param(Ruinae::kDelaySyncId, 0.0);
            r.param(Ruinae::kDelayTimeId, 249.0 / 4999.0);
            r.param(Ruinae::kDelayFeedbackId, 0.0);
            playBlip(r, kSettledBlipOn);
            r.render(kSettledBlipOn + r.at(1.5));
            const bool spectral = d == 4;
            // Spectral: the FFT analysis puts the first echo one frame late (T3.6b).
            const double expected = static_cast<double>(kSettledBlipOn) + dryC + 0.25 * kBlipSr +
                                    (spectral ? kFftSize : 0);
            const double got = energyCentroid(r.mono(), static_cast<int64_t>(expected) - 2400,
                                              static_cast<int64_t>(expected) + 2400);
            REQUIRE(got > 0.0);
            INFO("echo centroid minus expected: " << got - expected << " samples");
            // +-1 hop for Spectral, 1 ms for the time-domain types.
            CHECK(std::fabs(got - expected) <= (spectral ? static_cast<double>(kHop) : 48.0));
        }
    }
}

TEST_CASE("Audit regression: tempo-synced echoes at 1/4 and 120 BPM", "[integration][regression][fx]") {
    const double grid = 24000.0;  // 500 ms at 48 kHz
    const double dryC = dryBlipCentroid() + static_cast<double>(kBlipOn);
    REQUIRE(dryC > 0.0);

    for (int d : {0, 2, 4}) {  // Digital, PingPong exact; Spectral within one hop
        DYNAMIC_SECTION(kDelayNames[d]) {
            Rig r(kBlipSr);
            r.useCtx = true;
            r.playing = true;
            blipPatch(r);
            r.param(Ruinae::kDelayEnabledId, 1.0);
            r.param(Ruinae::kDelayTypeId, d / 4.0);
            r.param(Ruinae::kDelayMixId, 1.0);
            r.param(Ruinae::kDelaySyncId, 1.0);
            r.param(Ruinae::kDelayNoteValueId, 13.0 / 29.0);
            r.param(Ruinae::kDelayFeedbackId, 0.5 / 1.2);
            playBlip(r);
            r.render(r.at(3.0));
            const auto m = r.mono();
            const bool spectral = d == 4;
            const double bound = spectral ? static_cast<double>(kHop) : 48.0;

            std::vector<double> c;
            for (int k = 1; k <= 3; ++k) {
                const double expected = dryC + k * grid + (spectral && k == 1 ? kFftSize : 0);
                const double got = energyCentroid(m, static_cast<int64_t>(expected - grid / 2),
                                                  static_cast<int64_t>(expected + grid / 2));
                REQUIRE(got > 0.0);
                c.push_back(got);
            }
            // First echo: Spectral sits one FFT frame late, like a pre-delay.
            {
                const double expected = dryC + grid + (spectral ? kFftSize : 0);
                INFO("first echo minus expected: " << c[0] - expected);
                CHECK(std::fabs(c[0] - expected) <= (spectral ? kHop / 2.0 + 128.0 : bound));
            }
            for (size_t k = 1; k < c.size(); ++k) {
                const double spacing = c[k] - c[k - 1];
                INFO("spacing " << k << " = " << spacing << " (grid " << grid << ")");
                CHECK(std::fabs(spacing - grid) <= bound);
            }
            CHECK(nonFiniteAll(r) == 0);
        }
    }
}

// ------------------------------------------------------------------------------
// automation mid-note
// ------------------------------------------------------------------------------

namespace {

using Step = std::vector<std::pair<ParamID, double>>;

struct StepSpec {
    std::string name;
    std::function<void(Rig&)> pre;
    std::vector<Step> steps;  // one entry per automation event, 47 blocks apart
    std::vector<int> chord;
    bool excludeZeroRun = false;  // master gain to 0 legitimately renders exact zeros
};

void requireClickFree(const StepSpec& sp) {
    INFO(sp.name);
    const double sr = 48000.0;
    Rig r(sr);
    basePatch(r);
    if (sp.pre) sp.pre(r);
    const std::vector<int> notes = sp.chord.empty() ? std::vector<int>{57} : sp.chord;
    for (int n : notes) r.noteOn(n, 0.8f, 0);
    std::vector<int64_t> evs;
    const int64_t first = 47 * 512;
    for (size_t k = 0; k < sp.steps.size(); ++k) {
        const int64_t p = first + static_cast<int64_t>(k) * 47 * 512;
        for (const auto& pv : sp.steps[k]) r.param(pv.first, pv.second, p);
        evs.push_back(p);
    }
    r.render(evs.back() + r.at(0.6));
    const auto m = r.mono();
    for (size_t k = 0; k < evs.size(); ++k) {
        const auto c = clickAt(m, static_cast<size_t>(evs[k]), static_cast<size_t>(0.01 * sr),
                               static_cast<size_t>(0.05 * sr));
        INFO("step " << k << " jump ratio " << c.ratio << " (outlier samples " << c.count << ")");
        CHECK(c.ratio <= 1.5);
        if (!sp.excludeZeroRun) {
            const Stats after = statsOf(m, static_cast<size_t>(evs[k]),
                                        static_cast<size_t>(evs[k] + r.at(0.4)));
            CHECK(after.zeroRun <= 16);
        }
    }
    CHECK(nonFiniteAll(r) == 0);
}

}  // namespace

TEST_CASE("Audit regression: automation of filter and gain is click-free",
          "[integration][regression][fx]") {
    requireClickFree({"cutoff 0->1", [](Rig& r) { r.param(Ruinae::kFilterCutoffId, 0.0); },
                      {{{Ruinae::kFilterCutoffId, 1.0}}}, {}});
    requireClickFree({"cutoff 1->0", [](Rig&) {}, {{{Ruinae::kFilterCutoffId, 0.0}}}, {}});
    requireClickFree({"resonance 0->1 (cutoff 0.5)",
                      [](Rig& r) {
                          r.param(Ruinae::kFilterCutoffId, 0.5);
                          r.param(Ruinae::kFilterResonanceId, 0.0);
                      },
                      {{{Ruinae::kFilterResonanceId, 1.0}}}, {}});
    requireClickFree({"master gain 0.25->1.0", [](Rig& r) { r.param(Ruinae::kMasterGainId, 0.25); },
                      {{{Ruinae::kMasterGainId, 1.0}}}, {}});
    requireClickFree({"master gain 0.5->0", [](Rig&) {}, {{{Ruinae::kMasterGainId, 0.0}}}, {}, true});
    requireClickFree({"filter type SVF_LP->Ladder (cutoff 0.6)",
                      [](Rig& r) { r.param(Ruinae::kFilterCutoffId, 0.6); },
                      {{{Ruinae::kFilterTypeId, 4.0 / 12.0}}}, {}});
    requireClickFree({"filter type Ladder->Comb (cutoff 0.6)",
                      [](Rig& r) {
                          r.param(Ruinae::kFilterCutoffId, 0.6);
                          r.param(Ruinae::kFilterTypeId, 4.0 / 12.0);
                      },
                      {{{Ruinae::kFilterTypeId, 6.0 / 12.0}}}, {}});
    requireClickFree({"filter type SVF_LP->SelfOscillating (cutoff 0.6)",
                      [](Rig& r) { r.param(Ruinae::kFilterCutoffId, 0.6); },
                      {{{Ruinae::kFilterTypeId, 1.0}}}, {}});
}

TEST_CASE("Audit regression: automation of oscillator, distortion, mixer and voice is click-free",
          "[integration][regression][fx]") {
    requireClickFree({"osc type PolyBLEP->Wavetable", [](Rig&) {},
                      {{{Ruinae::kOscATypeId, 1.0 / 9.0}}}, {}});
    requireClickFree({"osc type PolyBLEP->Additive", [](Rig&) {},
                      {{{Ruinae::kOscATypeId, 4.0 / 9.0}}}, {}});
    requireClickFree({"osc type PolyBLEP->Noise", [](Rig&) {}, {{{Ruinae::kOscATypeId, 1.0}}}, {}});
    requireClickFree({"distortion Clean->Wavefolder (drive 0.5)",
                      [](Rig& r) { r.param(Ruinae::kDistortionDriveId, 0.5); },
                      {{{Ruinae::kDistortionTypeId, 4.0 / 6.0}}}, {}});
    requireClickFree({"distortion Clean->TapeSaturator (drive 0.5)",
                      [](Rig& r) { r.param(Ruinae::kDistortionDriveId, 0.5); },
                      {{{Ruinae::kDistortionTypeId, 5.0 / 6.0}}}, {}});
    requireClickFree({"distortion Clean->SpectralDistortion (drive 0.5)",
                      [](Rig& r) { r.param(Ruinae::kDistortionDriveId, 0.5); },
                      {{{Ruinae::kDistortionTypeId, 2.0 / 6.0}}}, {}});
    requireClickFree({"mix mode Crossfade->SpectralMorph (pos 0.5, osc B saw)",
                      [](Rig& r) {
                          r.param(Ruinae::kMixerPositionId, 0.5);
                          r.param(Ruinae::kOscBLevelId, 1.0);
                          r.param(Ruinae::kOscBTuneId, 0.5 + 7.0 / 48.0);
                      },
                      {{{Ruinae::kMixerModeId, 1.0}}}, {}});
    requireClickFree({"mix mode SpectralMorph->Crossfade (pos 0.5, osc B saw)",
                      [](Rig& r) {
                          r.param(Ruinae::kMixerPositionId, 0.5);
                          r.param(Ruinae::kOscBLevelId, 1.0);
                          r.param(Ruinae::kOscBTuneId, 0.5 + 7.0 / 48.0);
                          r.param(Ruinae::kMixerModeId, 1.0);
                      },
                      {{{Ruinae::kMixerModeId, 0.0}}}, {}});
    requireClickFree({"voice mode poly->mono (3-note chord held)", [](Rig&) {},
                      {{{Ruinae::kVoiceModeId, 1.0}}}, {57, 61, 64}});
    requireClickFree({"voice mode mono->poly (3-note chord held)",
                      [](Rig& r) { r.param(Ruinae::kVoiceModeId, 1.0); },
                      {{{Ruinae::kVoiceModeId, 0.0}}}, {57, 61, 64}});
    requireClickFree({"polyphony 16->1 (8-note chord held)",
                      [](Rig& r) { r.param(Ruinae::kPolyphonyId, 1.0); },
                      {{{Ruinae::kPolyphonyId, 0.0}}}, {48, 52, 55, 59, 62, 65, 69, 72}});
}

TEST_CASE("Audit regression: automation of the effects is click-free", "[integration][regression][fx]") {
    {
        std::vector<Step> seq;
        for (int d = 1; d < 5; ++d) seq.push_back({{Ruinae::kDelayTypeId, d / 4.0}});
        seq.push_back({{Ruinae::kDelayTypeId, 0.0}});
        requireClickFree({"delay type Digital->Tape->PingPong->Granular->Spectral->Digital",
                          [](Rig& r) {
                              r.param(Ruinae::kDelayEnabledId, 1.0);
                              r.param(Ruinae::kDelayMixId, 0.5);
                              r.param(Ruinae::kDelayTimeId, 299.0 / 4999.0);
                          },
                          seq, {}});
    }
    requireClickFree({"reverb type Plate->Hall->Plate",
                      [](Rig& r) {
                          r.param(Ruinae::kReverbEnabledId, 1.0);
                          r.param(Ruinae::kReverbMixId, 0.5);
                      },
                      {{{Ruinae::kReverbTypeId, 1.0}}, {{Ruinae::kReverbTypeId, 0.0}}}, {}});
    requireClickFree({"mod type None->Phaser->Flanger->Chorus->None", [](Rig&) {},
                      {{{Ruinae::kModulationTypeId, 1.0 / 3.0}},
                       {{Ruinae::kModulationTypeId, 2.0 / 3.0}},
                       {{Ruinae::kModulationTypeId, 1.0}},
                       {{Ruinae::kModulationTypeId, 0.0}}},
                      {}});
    requireClickFree({"delay enable off->on->off", [](Rig& r) { r.param(Ruinae::kDelayMixId, 0.5); },
                      {{{Ruinae::kDelayEnabledId, 1.0}}, {{Ruinae::kDelayEnabledId, 0.0}}}, {}});
    requireClickFree({"reverb enable off->on->off", [](Rig& r) { r.param(Ruinae::kReverbMixId, 0.5); },
                      {{{Ruinae::kReverbEnabledId, 1.0}}, {{Ruinae::kReverbEnabledId, 0.0}}}, {}});
    requireClickFree({"harmonizer enable off->on->off", [](Rig&) {},
                      {{{Ruinae::kHarmonizerEnabledId, 1.0}}, {{Ruinae::kHarmonizerEnabledId, 0.0}}}, {}});
    requireClickFree({"global filter enable off->on->off (cutoff 0.4)",
                      [](Rig& r) { r.param(Ruinae::kGlobalFilterCutoffId, 0.4); },
                      {{{Ruinae::kGlobalFilterEnabledId, 1.0}}, {{Ruinae::kGlobalFilterEnabledId, 0.0}}}, {}});
    requireClickFree({"trance gate enable off->on->off", [](Rig&) {},
                      {{{Ruinae::kTranceGateEnabledId, 1.0}}, {{Ruinae::kTranceGateEnabledId, 0.0}}}, {}});
}
