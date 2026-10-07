// ==============================================================================
// Vorago Phase 14 - ecosystem rule-knob audibility probe (FR-070, SC-025)
// ==============================================================================
// T002 (specs/vorago-phase14-presets-release/tasks.md), plan section 4.3.
// HIDDEN ([.probe]) and run once by hand (T004). The TU IS the measurement: it
// renders the shipped processor at the default surface with the C-6 stimulus
// (NoteOn 36, velocity 100/127, sample 0; 512-sample blocks; 48 kHz), to the
// end of M3 (A + 185 = 340 s, A = 155 s = the default stage-time sum,
// vorago_voice.h:320-322), once per:
//   - the default surface;
//   - the default surface's seed twin (kSeedId -> index 1, normalized 1/15);
//   - every NON-DEFAULT clamp end of every candidate rule knob (P-4: a knob
//     whose default sits at a range end has only the other end).
// Each render's D(P) is the mean of the three minute descriptors
// (VoragoTest::describe / meanOf, preset_test_support.h - ruling R-8, no local
// copy of the math). t0 = d(true-off, true-off seed twin) since the 2026-09-28
// ruling (spec Clarifications "Build stage"): the instrument's randomness with
// the colony silenced, so the gates ask the colony to move the sound by twice
// what everything else does. d(default, seed twin) is printed as t0on.
//
// Phase 13b (specs/vorago-phase13b-ecosystem-audibility, plan 4.5): each
// extreme prints d, d/t0, dOff/t0 (dOff = d(extreme, true-off)), and per minute
// M1..M3 its RMS, dRMS vs the base render's same minute, mean colony output
// and its ratio to the base's. Flags: KILL (any minute |dRMS| > 6 dB or RMS
// < -60 dBFS), OFF-LIKE (dOff < kGateFactor/2 * t0), INAUDIBLE (d < kGateFactor * t0).
// A counted extreme is d >= kGateFactor * t0 && !KILL && !OFF-LIKE; a knob's best
// is its best counted
// extreme. Also printed: DESCRIPTOR lines (all 14 components) for the default
// and true-off renders, GATE1 (d(on, off)/t0 >= kGateFactor), "audible non-kill knobs:
// N of 14", GATE2 (N >= 4; default surface, full table only) and the FR-034
// hand-off line - this phase names no roster. Verdicts are read from the log,
// never asserted (FR-006). Assertions (only these): every render finite by bit
// pattern with peak <= 0.9661 (not on the unlimited twin); the default render's
// M1 stereo RMS >= -60 dBFS; the true-off reference's offValid; GR validity
// checks 1 and 2.
//
// NAMESPACE HAZARD: Krate::DSP::TestUtils::Vorago exists, so plugin types are
// spelled ::Vorago:: and no `using namespace` appears in this TU.
//
// Diagnostic options (environment, all optional; the default run is the FR-070
// measurement exactly as above):
//   VORAGO_PROBE_SURFACE = "id=normalized,id=normalized"  parameter changes
//       delivered at block 0 of EVERY render (base, twin, extremes, reference),
//       e.g. "109=1" = Life macro at maximum;
//   VORAGO_PROBE_KNOBS   = "name,name" restricts the candidates ("-" = none);
//   VORAGO_PROBE_REF     = "id=normalized,..." one extra reference render on top
//       of the surface, reported as d(base, reference) - e.g. "900=0" measures
//       how audible the whole ecosystem is at that surface;
//   VORAGO_PROBE_REF     = "off" selects the TRUE ecosystem-off reference
//       instead (FR-007): no parameter change; every agent of every voice is
//       forced dormant after EVERY process() block (dormancy survives the
//       per-block macro apply that rewrites the depth), and the render asserts
//       REQUIRE(offValid): every rendering voice's every agent output reads 0
//       after every block from M1 start to the end of M3. The true-off
//       reference is ALSO rendered whenever VORAGO_PROBE_KNOBS is not "-"
//       (FR-004 (iii)). With it, when VORAGO_PROBE_SURFACE is empty, the probe
//       renders "900=0" too and prints d(true-off, 900=0)/t0 (cross-check);
//   VORAGO_PROBE_WAKEBASE = "<float>" overwrites every voice's peak and loop
//       wake BASES (vorago_voice.h prepare(): 0.50 / 0.50) after activation, so
//       the colony owns those wakes instead of topping up a half-awake surface
//       (the noise base is the parameter kNoiseWakeId = 301: override it);
//   VORAGO_PROBE_GR      = "1" limiter gain-reduction twin (SC-005, plan 4.4):
//       the base render is kept (y) and repeated with the output limiter's
//       ceiling raised to +60 dB through the friend (u, "unlimited"; its
//       peak <= 0.9661 check is skipped). Over M1..M3 it prints
//       GR max = max over samples with max(|yL|,|yR|) > 1e-6 of
//       20*log10(max(|uL|,|uR|) / max(|yL|,|yR|)) and its time, the fraction
//       of samples with GR > 0.01 dB (includes release tails), the attack-event
//       count (per-sample gain drop > 0.01 dB) and the first index with y != u.
//       Validity (REQUIREd): (1) the limiter never amplifies,
//       max(|yL|,|yR|) <= max(|uL|,|uR|) * (1 + 1e-6) wherever
//       max(|uL|,|uR|) > 1e-6; (2) one linked gain, |yL/uL - yR/uR| <= 1e-5
//       wherever min(|uL|,|uR|) > 1e-3. GR <= 1 dB is NOT asserted (read from
//       the log, FR-006).
//   VORAGO_PROBE_LANES   = "1" (Phase 13d FR-015, plan s2.3) samples voice 0's
//       getEcoLaneMean(k) for all five kinds once per M1..M3 block and prints
//       "LANES <label> <kind>=mean/p10/p90/dabs ..." after the default
//       render's DESCRIPTOR line and after each knob's .lo / .hi table row.
//       Off: nothing is sampled or printed.
// ==============================================================================

#include "plugin_ids.h"
#include "preset_test_support.h"
#include "processor/processor.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/ecosystem_engine.h>
#include <krate/dsp/systems/vorago_engine.h>

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// -----------------------------------------------------------------------------
// FR-070 probe: DEFINED only here (forward-declared and befriended by
// VoragoVoice and VoragoEngine, vorago_voice.h / vorago_engine.h, B-4).
// voices_ is private in VoragoEngine; ecosystem_ is private in VoragoVoice,
// whose public accessor is const only.
// -----------------------------------------------------------------------------
namespace Krate::DSP::detail {
struct VoragoEcosystemRuleProbe {
    template <typename Fn>
    static void forEachEcosystem(VoragoEngine& e, Fn fn) {
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            fn(e.voices_[v].ecosystem_);
        }
    }
    // Diagnostic (VORAGO_PROBE_WAKEBASE): the peak and loop wake bases are
    // private, prepare()-only values with no setter.
    static void setWakeBases(VoragoEngine& e, float w) {
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            e.voices_[v].peakWakeBase_.fill(w);
            e.voices_[v].loopWakeBase_.fill(w);
        }
    }
    // FR-005: mean over rendering voices' agents of getAgentOutput(i)
    // (ecosystem_engine.h:886); -1 if no voice renders.
    static double meanColonyOutput(const VoragoEngine& e) {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            if (!e.isRendering(v)) {
                continue;
            }
            const EcosystemEngine& eco = e.voices_[v].ecosystem();
            for (std::size_t i = 0; i < eco.getAgentCount(); ++i) {
                sum += static_cast<double>(eco.getAgentOutput(i));
                ++count;
            }
        }
        return (count > 0u) ? sum / static_cast<double>(count) : -1.0;
    }
    // FR-007: dormancy on every agent of every voice (ecosystem_engine.h:776).
    // Re-applied after EVERY process() block: the macro apply rewrites depth
    // each block but never dormancy, while reset()/setSeed()/steal clear it.
    static void silenceColony(VoragoEngine& e) {
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            EcosystemEngine& eco = e.voices_[v].ecosystem_;
            for (std::size_t i = 0; i < eco.getAgentCount(); ++i) {
                eco.setAgentDormant(i, true);
            }
        }
    }
    // FR-007 validity: true iff every rendering voice's every agent output
    // (ecosystem_engine.h:886) is exactly 0.
    static bool colonySilent(const VoragoEngine& e) {
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            if (!e.isRendering(v)) {
                continue;
            }
            const EcosystemEngine& eco = e.voices_[v].ecosystem();
            for (std::size_t i = 0; i < eco.getAgentCount(); ++i) {
                if (eco.getAgentOutput(i) != 0.0f) {
                    return false;
                }
            }
        }
        return true;
    }
    // SC-005 (plan 4.4): the limiter ceiling is a prepare()-only private value
    // (vorago_engine.h:439); the unlimited twin raises it after activation.
    static void setLimiterCeilingDb(VoragoEngine& e, float db) { e.limiter_.setCeilingDb(db); }
};
}  // namespace Krate::DSP::detail

namespace {

using Krate::DSP::EcosystemEngine;

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr Steinberg::int16 kNote = 36;
constexpr float kVelocity100 = 100.0f / 127.0f;
constexpr float kOutputCeiling = 0.9661f;  // 10^(-0.3/20), vorago_engine.h:203
constexpr double kSeedTwinNormalized = 1.0 / 15.0;  // kSeedId index 1 of 16

// THE GATE FACTOR (user ruling 2026-09-28, spec Clarifications "Build stage"):
// Gate 1 passes when the colony moves the sound by at least kGateFactor times
// the off-reseed yardstick t0 - on this descriptor's scale, half an off-reseed
// is the Life macro's whole travel. Gate 2's counted extreme uses the same bar,
// and OFF-LIKE is "closer to ecosystem-off than half that bar". Measured on the
// shipped L5 tree: 0.76 (default) and 0.74 (Life max), six-seed medians.
constexpr double kGateFactor = 0.5;

// Timeline (plan 5.1) at the default surface: A = 155 s.
constexpr double kAttackSeconds = 155.0;
constexpr std::size_t kSecond = 48000;
constexpr std::size_t kMinuteSamples = 60u * kSecond;
constexpr std::size_t kM1Start = static_cast<std::size_t>(kAttackSeconds + 5.0) * kSecond;  // 160 s
constexpr std::size_t kTotalSamples =
    static_cast<std::size_t>(kAttackSeconds + 185.0) * kSecond;  // 340 s
constexpr std::size_t kCaptureSamples = kTotalSamples - kM1Start;  // M1..M3, 180 s
static_assert(kCaptureSamples == 3u * kMinuteSamples, "M1, M2, M3 are contiguous minutes");
static_assert(kM1Start % kBlock == 0u && kTotalSamples % kBlock == 0u,
              "capture edges sit on block boundaries");

using KnobApply = void (*)(EcosystemEngine&, float);
using Override = std::pair<Steinberg::Vst::ParamID, double>;

// Diagnostic options (see the file header). _dupenv_s under MSVC avoids C4996.
std::string readEnv(const char* name) {
#ifdef _MSC_VER
    std::array<char, 1024> buf{};
    std::size_t len = 0;
    if (getenv_s(&len, buf.data(), buf.size(), name) != 0 || len == 0u) {
        return {};  // unset, or longer than the buffer (not a valid option value)
    }
    return {buf.data()};
#else
    const char* v = std::getenv(name);
    return (v != nullptr) ? std::string(v) : std::string{};
#endif
}

// "id=normalized,id=normalized" -> parameter changes; malformed items are skipped.
std::vector<Override> parseOverrides(const std::string& csv) {
    std::vector<Override> out;
    std::size_t pos = 0;
    while (pos < csv.size()) {
        std::size_t comma = csv.find(',', pos);
        if (comma == std::string::npos) {
            comma = csv.size();
        }
        const std::string item = csv.substr(pos, comma - pos);
        const std::size_t eq = item.find('=');
        if (eq != std::string::npos) {
            out.emplace_back(
                static_cast<Steinberg::Vst::ParamID>(std::strtoul(item.c_str(), nullptr, 10)),
                std::strtod(item.c_str() + eq + 1, nullptr));
        }
        pos = comma + 1;
    }
    return out;
}

// Empty filter = every candidate; "-" = none; otherwise a comma list of names.
bool knobSelected(const std::string& csv, const char* name) {
    if (csv.empty()) {
        return true;
    }
    std::size_t pos = 0;
    while (pos <= csv.size()) {
        std::size_t comma = csv.find(',', pos);
        if (comma == std::string::npos) {
            comma = csv.size();
        }
        if (csv.compare(pos, comma - pos, name) == 0) {
            return true;
        }
        pos = comma + 1;
    }
    return false;
}

std::string describeOverrides(const std::vector<Override>& o) {
    if (o.empty()) {
        return "none";
    }
    std::string s;
    for (const Override& x : o) {
        std::array<char, 48> buf{};
        std::snprintf(buf.data(), buf.size(), "%s%u=%g", s.empty() ? "" : ", ",
                      static_cast<unsigned>(x.first), x.second);
        s += buf.data();
    }
    return s;
}

void applyAffinityDiagonal(EcosystemEngine& eco, float v) {
    for (std::size_t k = 0; k < EcosystemEngine::kNumKinds; ++k) {
        const auto kind = static_cast<EcosystemEngine::Kind>(k);
        eco.setAffinity(kind, kind, v);
    }
}

void applyAffinityOffDiagonal(EcosystemEngine& eco, float v) {
    for (std::size_t i = 0; i < EcosystemEngine::kNumKinds; ++i) {
        for (std::size_t j = 0; j < EcosystemEngine::kNumKinds; ++j) {
            if (i != j) {
                eco.setAffinity(static_cast<EcosystemEngine::Kind>(i),
                                static_cast<EcosystemEngine::Kind>(j), v);
            }
        }
    }
}

// Candidates (spec C-2.3): clamp ends and defaults from ecosystem_engine.h
// (member defaults :2373-2396, affinity range :237-238, setAffinity :707).
// leakExponent is excluded (plan 4.3: its upper half kills the ecosystem).
struct Candidate {
    const char* name;
    float lo;
    float hi;
    float def;
    KnobApply apply;
};

const std::array<Candidate, 14> kCandidates{{
    {.name = "predation", .lo = 0.0f, .hi = 1.0f, .def = 0.55f, .apply = [](EcosystemEngine& e, float v) { e.setPredation(v); }},
    {.name = "syncRate", .lo = 0.0f, .hi = 0.5f, .def = 0.0f, .apply = [](EcosystemEngine& e, float v) { e.setSyncRate(v); }},
    {.name = "exchangeRate", .lo = 0.0f, .hi = 3.0f, .def = 0.35f, .apply = [](EcosystemEngine& e, float v) { e.setExchangeRate(v); }},
    {.name = "crowding", .lo = 0.0f, .hi = 0.2f, .def = 0.05f, .apply = [](EcosystemEngine& e, float v) { e.setCrowding(v); }},
    {.name = "forageRate", .lo = 0.0f, .hi = 0.05f, .def = 0.01f, .apply = [](EcosystemEngine& e, float v) { e.setForageRate(v); }},
    {.name = "feedRate", .lo = 0.0f, .hi = 1.0f, .def = 0.0f, .apply = [](EcosystemEngine& e, float v) { e.setFeedRate(v); }},
    {.name = "grazeRate", .lo = 0.0f, .hi = 3.0f, .def = 0.75f, .apply = [](EcosystemEngine& e, float v) { e.setGrazeRate(v); }},
    {.name = "leakRate", .lo = 0.0f, .hi = 1.0f, .def = 0.06f, .apply = [](EcosystemEngine& e, float v) { e.setLeakRate(v); }},
    {.name = "moveRate", .lo = 0.0f, .hi = 0.5f, .def = 0.20f, .apply = [](EcosystemEngine& e, float v) { e.setMoveRate(v); }},
    {.name = "maxSpeed", .lo = 0.001f, .hi = 0.05f, .def = 0.03f, .apply = [](EcosystemEngine& e, float v) { e.setMaxSpeed(v); }},
    {.name = "kernelSigma", .lo = 0.01f, .hi = 0.35f, .def = 0.03f, .apply = [](EcosystemEngine& e, float v) { e.setKernelSigma(v); }},
    {.name = "freqDrift", .lo = 0.0f, .hi = 0.0002f, .def = 0.00004f, .apply = [](EcosystemEngine& e, float v) { e.setFreqDrift(v); }},
    {.name = "selfAffinity", .lo = EcosystemEngine::kMinAffinity, .hi = EcosystemEngine::kMaxAffinity,
     .def = -1.0f, .apply = &applyAffinityDiagonal},
    {.name = "crossAffinity", .lo = EcosystemEngine::kMinAffinity, .hi = EcosystemEngine::kMaxAffinity,
     .def = 0.45f, .apply = &applyAffinityOffDiagonal},
}};

// Phase 13d FR-015 (plan s2.3, VORAGO_PROBE_LANES=1): one kind's eco-lane mean
// (VoragoVoice::getEcoLaneMean on voice 0), sampled once per block over M1..M3:
// mean, 10th / 90th percentile, and the mean absolute block-to-block change.
struct LaneStats {
    double mean = 0.0;
    double p10 = 0.0;
    double p90 = 0.0;
    double dabs = 0.0;
};

constexpr std::array<const char*, EcosystemEngine::kNumKinds> kKindNames{
    "Partial", "Resonator", "Noise", "Feedback", "Ghost"};

LaneStats laneStatsOf(std::vector<float> v) {
    LaneStats s{};
    if (v.empty()) {
        return s;
    }
    double sum = 0.0;
    double dsum = 0.0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        sum += static_cast<double>(v[i]);
        if (i > 0u) {
            dsum += std::fabs(static_cast<double>(v[i]) - static_cast<double>(v[i - 1u]));
        }
    }
    const std::size_t n = v.size();
    s.mean = sum / static_cast<double>(n);
    s.dabs = (n > 1u) ? dsum / static_cast<double>(n - 1u) : 0.0;
    std::sort(v.begin(), v.end());
    const auto at = [&v, n](double q) {
        return static_cast<double>(v[static_cast<std::size_t>(q * static_cast<double>(n - 1u))]);
    };
    s.p10 = at(0.10);
    s.p90 = at(0.90);
    return s;
}

struct RenderResult {
    VoragoTest::PresetDescriptor d{};
    bool allFinite = true;
    float peak = 0.0f;
    double m1RmsDb = -240.0;                   // == minuteRmsDb[0]
    std::array<double, 3> minuteRmsDb{};       // FR-004: M1, M2, M3 stereo RMS
    std::array<double, 3> minuteColony{};      // FR-005: mean colony output per minute
    bool offValid = true;                      // FR-007: colony silent after every M1..M3 block
    std::vector<float> capL;                   // SC-005: M1..M3 capture, keepCapture only
    std::vector<float> capR;
    std::array<LaneStats, EcosystemEngine::kNumKinds> lanes{};  // FR-015: VORAGO_PROBE_LANES only
};

// FR-015: "LANES <label> <kind>=mean/p10/p90/dabs ..." (VORAGO_PROBE_LANES=1 only).
void printLanes(const std::string& label, const std::array<LaneStats, EcosystemEngine::kNumKinds>& lanes) {
    std::printf("LANES %s", label.c_str());
    for (std::size_t k = 0; k < EcosystemEngine::kNumKinds; ++k) {
        std::printf(" %s=%.6f/%.6f/%.6f/%.6f", kKindNames[k], lanes[k].mean, lanes[k].p10,
                    lanes[k].p90, lanes[k].dabs);
    }
    std::printf("\n");
}

// Stereo power of a span in dB (plan 5.2): (sum L^2 + sum R^2) / (2n).
double stereoRmsDb(std::span<const float> l, std::span<const float> r) {
    double sum = 0.0;
    for (const float s : l) {
        sum += static_cast<double>(s) * static_cast<double>(s);
    }
    for (const float s : r) {
        sum += static_cast<double>(s) * static_cast<double>(s);
    }
    const double p = sum / (2.0 * static_cast<double>(std::max<std::size_t>(l.size(), 1u)));
    return 10.0 * std::log10(std::max(p, 1e-24));
}

/// One C-6 render of the headless processor. `apply` (may be null) is pushed
/// into all six voices' ecosystems after prepare and before the note-on block;
/// `seedNormalized` < 0 leaves the Seed parameter at its default. `overrides`
/// are delivered as offset-0 parameter changes in block 0 (diagnostic surface).
/// `colonyOff` (FR-007) forces every agent dormant after every process() block
/// and records `offValid` (colony silent after every block of M1..M3).
/// `unlimited` (SC-005) raises the output limiter ceiling to +60 dB once after
/// activation; `keepCapture` returns the M1..M3 capture in capL/capR.
RenderResult renderOnce(KnobApply apply, float value, double seedNormalized,
                        const std::vector<Override>& overrides, double wakeBase,
                        bool colonyOff, bool unlimited, bool keepCapture) {
    auto proc = std::make_unique<::Vorago::Processor>();
    REQUIRE(proc->initialize(nullptr) == Steinberg::kResultOk);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlock);
    setup.sampleRate = kSampleRate;
    REQUIRE(proc->setupProcessing(setup) == Steinberg::kResultOk);
    REQUIRE(proc->setActive(true) == Steinberg::kResultOk);

    Krate::DSP::VoragoEngine* offEngine = nullptr;  // FR-007: non-null iff colonyOff
    if (apply != nullptr || wakeBase >= 0.0 || colonyOff || unlimited) {
        // The engine is a non-const heap object (processor.h), so the cast is
        // well-defined; single-threaded, between process() calls.
        REQUIRE(proc->engineForTest() != nullptr);
        auto& engine = const_cast<Krate::DSP::VoragoEngine&>(  // NOLINT(cppcoreguidelines-pro-type-const-cast): plan 4.3 - the engine is a non-const heap object; engineForTest() is the only accessor
            *proc->engineForTest());
        if (colonyOff) {
            offEngine = &engine;
        }
        if (unlimited) {
            Krate::DSP::detail::VoragoEcosystemRuleProbe::setLimiterCeilingDb(engine, 60.0f);
        }
        if (wakeBase >= 0.0) {
            Krate::DSP::detail::VoragoEcosystemRuleProbe::setWakeBases(
                engine, static_cast<float>(wakeBase));
        }
        if (apply != nullptr) {
            Krate::DSP::detail::VoragoEcosystemRuleProbe::forEachEcosystem(
                engine, [apply, value](EcosystemEngine& eco) { apply(eco, value); });
        }
    }

    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    std::vector<float> capL;
    std::vector<float> capR;
    capL.reserve(kCaptureSamples);
    capR.reserve(kCaptureSamples);

    Krate::Test::EventList noteOnEvents;
    noteOnEvents.addNoteOn(kNote, kVelocity100, 0);
    Krate::Test::ParameterChanges seedChange;
    if (seedNormalized >= 0.0) {
        seedChange.addChange(::Vorago::kSeedId, seedNormalized);
    }
    for (const Override& o : overrides) {
        seedChange.addChange(o.first, o.second);
    }
    const bool hasChanges = (seedNormalized >= 0.0) || !overrides.empty();

    // FR-005: the engine is read (const) after every block for the colony output.
    REQUIRE(proc->engineForTest() != nullptr);
    const Krate::DSP::VoragoEngine& colonyEngine = *proc->engineForTest();
    std::array<double, 3> colonySum{};
    std::array<std::size_t, 3> colonyCount{};

    // FR-015 (plan s2.3): voice 0's per-kind eco-lane mean, once per M1..M3 block.
    const bool sampleLanes = (readEnv("VORAGO_PROBE_LANES") == "1");
    std::array<std::vector<float>, EcosystemEngine::kNumKinds> laneSamples{};
    if (sampleLanes) {
        for (std::vector<float>& s : laneSamples) {
            s.reserve(kCaptureSamples / kBlock);
        }
    }

    RenderResult res;
    for (std::size_t start = 0; start < kTotalSamples; start += kBlock) {
        std::array<float*, 2> channels{outL.data(), outR.data()};
        Steinberg::Vst::AudioBusBuffers outBus{};
        outBus.numChannels = 2;
        outBus.silenceFlags = 0;
        outBus.channelBuffers32 = channels.data();

        const bool first = (start == 0u);
        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlock);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.inputParameterChanges = (first && hasChanges) ? &seedChange : nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = first ? &noteOnEvents : nullptr;
        data.outputEvents = nullptr;
        data.processContext = nullptr;
        REQUIRE(proc->process(data) == Steinberg::kResultOk);

        if (offEngine != nullptr) {
            // FR-007: after EVERY block (plan 4.1); validity from M1 to M3 end.
            Krate::DSP::detail::VoragoEcosystemRuleProbe::silenceColony(*offEngine);
            if (start >= kM1Start) {
                res.offValid = res.offValid &&
                    Krate::DSP::detail::VoragoEcosystemRuleProbe::colonySilent(*offEngine);
            }
        }

        for (std::size_t i = 0; i < kBlock; ++i) {
            const float l = outL[i];
            const float r = outR[i];
            if (!Krate::DSP::detail::isFinite(l) || !Krate::DSP::detail::isFinite(r)) {
                res.allFinite = false;
            } else {
                res.peak = std::max({res.peak, std::fabs(l), std::fabs(r)});
            }
        }
        if (start >= kM1Start) {
            capL.insert(capL.end(), outL.begin(), outL.end());
            capR.insert(capR.end(), outR.begin(), outR.end());
            // Minute edges sit on block boundaries (static_assert above).
            const std::size_t m = (start - kM1Start) / kMinuteSamples;
            const double colony =
                Krate::DSP::detail::VoragoEcosystemRuleProbe::meanColonyOutput(colonyEngine);
            if (colony >= 0.0) {
                colonySum[m] += colony;
                ++colonyCount[m];
            }
            if (sampleLanes) {
                for (std::size_t k = 0; k < EcosystemEngine::kNumKinds; ++k) {
                    laneSamples[k].push_back(colonyEngine.getVoice(0).getEcoLaneMean(
                        static_cast<EcosystemEngine::Kind>(k)));
                }
            }
        }
    }
    if (sampleLanes) {
        for (std::size_t k = 0; k < EcosystemEngine::kNumKinds; ++k) {
            res.lanes[k] = laneStatsOf(std::move(laneSamples[k]));
        }
    }

    REQUIRE(proc->setActive(false) == Steinberg::kResultOk);
    REQUIRE(proc->terminate() == Steinberg::kResultOk);

    REQUIRE(capL.size() == kCaptureSamples);
    REQUIRE(capR.size() == kCaptureSamples);
    const std::span<const float> l(capL);
    const std::span<const float> r(capR);

    std::array<VoragoTest::PresetDescriptor, 3> minutes{};
    for (std::size_t m = 0; m < 3u; ++m) {
        minutes[m] = VoragoTest::describe(l.subspan(m * kMinuteSamples, kMinuteSamples),
                                          r.subspan(m * kMinuteSamples, kMinuteSamples),
                                          kSampleRate);
    }
    res.d = VoragoTest::meanOf(std::span<const VoragoTest::PresetDescriptor>(minutes));
    for (std::size_t m = 0; m < 3u; ++m) {
        res.minuteRmsDb[m] = stereoRmsDb(l.subspan(m * kMinuteSamples, kMinuteSamples),
                                         r.subspan(m * kMinuteSamples, kMinuteSamples));
        res.minuteColony[m] =
            (colonyCount[m] > 0u) ? colonySum[m] / static_cast<double>(colonyCount[m]) : -1.0;
    }
    res.m1RmsDb = res.minuteRmsDb[0];
    if (keepCapture) {
        res.capL = std::move(capL);
        res.capR = std::move(capR);
    }
    return res;
}

// FR-004 / FR-005 (plan 4.5): one extreme's measurements against the base
// render and the true-off reference, with its flags.
struct ExtremeRow {
    float value = 0.0f;
    double d = 0.0;
    double dOff = -1.0;                  // d(extreme, true-off); -1 = no reference
    std::array<double, 3> rmsDb{};       // M1, M2, M3 stereo RMS
    std::array<double, 3> colony{};      // M1, M2, M3 mean colony output
    std::array<LaneStats, EcosystemEngine::kNumKinds> lanes{};  // FR-015: VORAGO_PROBE_LANES only
    bool kill = false;                   // any minute |dRMS| > 6 dB or RMS < -60 dBFS
    bool offLike = false;                // dOff < kGateFactor/2 * t0
    bool inaudible = false;              // d < kGateFactor * t0
    bool counted = false;                // d >= kGateFactor * t0 && !kill && !offLike
};

struct KnobRow {
    const Candidate* c = nullptr;
    std::vector<ExtremeRow> extremes;
    double best = 0.0;     // best COUNTED extreme's d (0 if none counts)
    double maxD = 0.0;     // max d over every extreme (informative)
    bool counted = false;  // at least one counted extreme
    std::string cells;     // P-4: ".lo,.hi" or ".hi"
};

constexpr double kKillDeltaDb = 6.0;     // FR-004: per-minute RMS vs base
constexpr double kKillFloorDbfs = -60.0;  // FR-004: non-silence floor

// FR-030: the 14 PresetDescriptor components (preset_test_support.h:47-54).
void printDescriptor(const char* label, const VoragoTest::PresetDescriptor& d) {
    std::printf("DESCRIPTOR %s band=", label);
    for (std::size_t i = 0; i < VoragoTest::kDescriptorBands; ++i) {
        std::printf("%s%.6f", (i == 0u) ? "" : ",", d.band[i]);
    }
    std::printf(" motion=%.6f flux=%.6f corr=%.6f energySpread=%.6f crest=%.6f\n", d.motion,
                d.flux, d.corr, d.energySpread, d.crest);
}

}  // namespace

TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]") {
    std::size_t renderCount = 0;

    const auto checkBounded = [](const RenderResult& r, const char* label) {
        INFO("render: " << label << " peak " << r.peak);
        CHECK(r.allFinite);
        CHECK(r.peak <= kOutputCeiling);
    };

    const std::string surfaceEnv = readEnv("VORAGO_PROBE_SURFACE");
    const std::vector<Override> surface = parseOverrides(surfaceEnv);
    const std::string knobFilter = readEnv("VORAGO_PROBE_KNOBS");
    // FR-007: "off" selects the true ecosystem-off reference (no parameter
    // change); any other value keeps the parameter-override reference.
    const std::string refEnv = readEnv("VORAGO_PROBE_REF");
    const bool refOff = (refEnv == "off");
    const std::vector<Override> refExtra =
        refOff ? std::vector<Override>{} : parseOverrides(refEnv);
    // FR-004 (iii): every table run needs the true-off reference.
    const bool renderTrueOff = refOff || (knobFilter != "-");
    const std::string wakeBaseEnv = readEnv("VORAGO_PROBE_WAKEBASE");
    const double wakeBase = wakeBaseEnv.empty() ? -1.0 : std::strtod(wakeBaseEnv.c_str(), nullptr);
    std::printf("[probe] surface overrides: %s; knob filter: %s; reference: %s%s; wake bases: %s\n",
                describeOverrides(surface).c_str(), knobFilter.empty() ? "all" : knobFilter.c_str(),
                describeOverrides(refExtra).c_str(), renderTrueOff ? " + true-off" : "",
                wakeBaseEnv.empty() ? "shipped (vorago_voice.h kPeakWakeBase / kLoopWakeBase)" : wakeBaseEnv.c_str());

    // SC-005 (plan 4.4): limiter gain-reduction twin.
    const bool grOn = (readEnv("VORAGO_PROBE_GR") == "1");
    // FR-015 (plan s2.3): LANES lines; renderOnce reads the same option to sample.
    const bool lanesOn = (readEnv("VORAGO_PROBE_LANES") == "1");

    // Multi-seed Gate 1 (user ruling 2026-09-28, spec Clarifications "Build
    // stage"): VORAGO_PROBE_SEEDS=n renders, for seed indices 0..n-1 (0 = the
    // untouched default, i = kSeedId at i/15), the surface with the colony ON
    // and the FR-007 true-off reference, and reports d(on, off) per seed, the
    // off-reseed distances of consecutive seed pairs, and the MEDIANS. The
    // measured cause: at L4 one seed read 2.92 while four others read
    // 0.62-1.28, so a single-seed gate measures that seed's colony, not the
    // instrument. This mode prints the GATE1M line and returns; the knob table
    // and single-seed lines are the other modes' business.
    const std::string seedsEnv = readEnv("VORAGO_PROBE_SEEDS");
    if (!seedsEnv.empty()) {
        const auto nSeeds = static_cast<std::size_t>(std::strtoul(seedsEnv.c_str(), nullptr, 10));
        REQUIRE(nSeeds >= 2u);
        REQUIRE(nSeeds <= 16u);
        std::vector<double> dOnOff;
        std::vector<double> t0Pairs;
        std::vector<VoragoTest::PresetDescriptor> offDescs;
        std::printf("[probe] multi-seed gate: %zu seeds x (on + true-off) on surface %s\n", nSeeds,
                    surfaceEnv.empty() ? "default" : surfaceEnv.c_str());
        std::printf("%-6s %-10s %-12s %-12s\n", "seed", "d(on,off)", "on M1 RMS", "off M1 RMS");
        for (std::size_t i = 0; i < nSeeds; ++i) {
            const double seedNorm = (i == 0u) ? -1.0 : (static_cast<double>(i) / 15.0);
            std::printf("[probe] seed %zu: rendering on (340 s)...\n", i);
            const RenderResult on =
                renderOnce(nullptr, 0.0f, seedNorm, surface, wakeBase, false, false, false);
            checkBounded(on, "multi-seed on");
            REQUIRE(on.m1RmsDb >= -60.0);
            std::printf("[probe] seed %zu: rendering true-off (340 s)...\n", i);
            const RenderResult off =
                renderOnce(nullptr, 0.0f, seedNorm, surface, wakeBase, true, false, false);
            checkBounded(off, "multi-seed true-off");
            REQUIRE(off.offValid);
            const double dd = VoragoTest::descriptorDistance(on.d, off.d);
            dOnOff.push_back(dd);
            if (!offDescs.empty()) {
                t0Pairs.push_back(VoragoTest::descriptorDistance(offDescs.back(), off.d));
            }
            offDescs.push_back(off.d);
            std::printf("%-6zu %-10.4f %-12.2f %-12.2f\n", i, dd, on.m1RmsDb, off.m1RmsDb);
            std::fflush(stdout);
        }
        const auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            const std::size_t n = v.size();
            return (n % 2u == 1u) ? v[n / 2u] : 0.5 * (v[n / 2u - 1u] + v[n / 2u]);
        };
        const double medD = median(dOnOff);
        const double medT0 = median(t0Pairs);
        const double minD = *std::min_element(dOnOff.begin(), dOnOff.end());
        std::printf("off-reseed pairs (consecutive seeds):");
        for (const double t : t0Pairs) {
            std::printf(" %.4f", t);
        }
        std::printf("\n");
        const double r = (medT0 > 0.0) ? medD / medT0 : HUGE_VAL;
        std::printf("GATE1M surface=%s seeds=%zu median_d(on,off)=%.4f min_d(on,off)=%.4f "
                    "median_t0off=%.4f ratio=%.3f verdict=%s (threshold %.2f, ruling 2026-09-28; the "
                    "threshold is ruled on this data)\n",
                    surfaceEnv.empty() ? "default" : surfaceEnv.c_str(), nSeeds, medD, minD, medT0,
                    r, (r >= kGateFactor) ? "PASS" : "FAIL", kGateFactor);
        std::fflush(stdout);
        return;
    }

    std::printf("[probe] rendering default surface (340 s)...\n");
    const RenderResult base =
        renderOnce(nullptr, 0.0f, -1.0, surface, wakeBase, false, false, grOn);
    ++renderCount;
    checkBounded(base, "default");
    INFO("default M1 stereo RMS " << base.m1RmsDb << " dBFS");
    REQUIRE(base.m1RmsDb >= -60.0);
    REQUIRE(base.minuteRmsDb[0] == base.m1RmsDb);
    REQUIRE(base.minuteColony[0] >= 0.0);

    if (grOn) {
        std::printf("[probe] rendering unlimited twin (340 s, limiter ceiling +60 dB)...\n");
        const RenderResult unl =
            renderOnce(nullptr, 0.0f, -1.0, surface, wakeBase, false, true, true);
        CHECK(unl.allFinite);  // the peak <= 0.9661 check does not apply to this render
        REQUIRE(base.capL.size() == kCaptureSamples);
        REQUIRE(base.capR.size() == kCaptureSamples);
        REQUIRE(unl.capL.size() == kCaptureSamples);
        REQUIRE(unl.capR.size() == kCaptureSamples);

        double grMax = -HUGE_VAL;
        std::size_t grMaxIdx = kCaptureSamples;  // == none
        std::size_t grAbove = 0;                 // samples with GR > 0.01 dB
        std::size_t attacks = 0;                 // per-sample gain drop > 0.01 dB
        std::size_t firstDiff = kCaptureSamples;  // first y != u, == none
        double prevGr = 0.0;
        bool prevValid = false;
        std::size_t check1Fail = 0;
        std::size_t check1First = kCaptureSamples;
        std::size_t check2Fail = 0;
        std::size_t check2First = kCaptureSamples;
        for (std::size_t i = 0; i < kCaptureSamples; ++i) {
            const double yL = static_cast<double>(base.capL[i]);
            const double yR = static_cast<double>(base.capR[i]);
            const double uL = static_cast<double>(unl.capL[i]);
            const double uR = static_cast<double>(unl.capR[i]);
            const double yMax = std::max(std::fabs(yL), std::fabs(yR));
            const double uMax = std::max(std::fabs(uL), std::fabs(uR));
            if (firstDiff == kCaptureSamples && (base.capL[i] != unl.capL[i] ||
                                                 base.capR[i] != unl.capR[i])) {
                firstDiff = i;
            }
            // Validity 1: never amplifies.
            if (uMax > 1e-6 && yMax > uMax * (1.0 + 1e-6)) {
                if (check1Fail == 0u) {
                    check1First = i;
                }
                ++check1Fail;
            }
            // Validity 2: one L/R-linked gain.
            if (std::min(std::fabs(uL), std::fabs(uR)) > 1e-3 &&
                std::fabs(yL / uL - yR / uR) > 1e-5) {
                if (check2Fail == 0u) {
                    check2First = i;
                }
                ++check2Fail;
            }
            if (yMax > 1e-6) {
                const double gr = 20.0 * std::log10(uMax / yMax);
                if (gr > grMax) {
                    grMax = gr;
                    grMaxIdx = i;
                }
                if (gr > 0.01) {
                    ++grAbove;
                }
                if (prevValid && gr - prevGr > 0.01) {
                    ++attacks;
                }
                prevGr = gr;
                prevValid = true;
            } else {
                prevValid = false;
            }
        }

        const auto seconds = [](std::size_t i) {
            return static_cast<double>(kM1Start + i) / static_cast<double>(kSecond);
        };
        if (grMaxIdx < kCaptureSamples) {
            std::printf("GR max %.4f dB at %.4f s\n", grMax, seconds(grMaxIdx));
        } else {
            std::printf("GR max n/a (no sample with max(|yL|,|yR|) > 1e-6)\n");
        }
        std::printf("GR > 0.01 dB fraction (includes release tails): %.6f (%zu of %zu samples)\n",
                    static_cast<double>(grAbove) / static_cast<double>(kCaptureSamples), grAbove,
                    kCaptureSamples);
        std::printf("GR attack events (per-sample gain drop > 0.01 dB): %zu\n", attacks);
        if (firstDiff < kCaptureSamples) {
            std::printf("GR first y != u: capture index %zu (%.4f s)\n", firstDiff,
                        seconds(firstDiff));
        } else {
            std::printf("GR first y != u: none\n");
        }
        std::fflush(stdout);

        {
            INFO("GR validity 1 (never amplifies): " << check1Fail << " failing samples, first at "
                                                      "capture index " << check1First);
            REQUIRE(check1Fail == 0u);
        }
        {
            INFO("GR validity 2 (one linked gain): " << check2Fail << " failing samples, first at "
                                                     "capture index " << check2First);
            REQUIRE(check2Fail == 0u);
        }
    }

    std::printf("[probe] rendering seed twin (340 s)...\n");
    const RenderResult twin =
        renderOnce(nullptr, 0.0f, kSeedTwinNormalized, surface, wakeBase, false, false, false);
    ++renderCount;
    checkBounded(twin, "seed twin");
    // Informational since the 2026-09-28 ruling: with the colony driving audible
    // levers, two seeds differ THROUGH those levers, so this distance grows with
    // the ecosystem's own effect and a ratio to it saturates near 1 (ladder
    // record L2 -> L4: d(on,off) 2.22 -> 2.92 while this rose 1.20 -> 2.66).
    const double t0On = VoragoTest::descriptorDistance(base.d, twin.d);

    double refD = -1.0;
    double refRmsDb = 0.0;
    if (!refExtra.empty()) {
        std::vector<Override> refOverrides = surface;
        refOverrides.insert(refOverrides.end(), refExtra.begin(), refExtra.end());
        std::printf("[probe] rendering reference (340 s)...\n");
        const RenderResult ref =
            renderOnce(nullptr, 0.0f, -1.0, refOverrides, wakeBase, false, false, false);
        checkBounded(ref, "reference");
        refD = VoragoTest::descriptorDistance(base.d, ref.d);
        refRmsDb = ref.m1RmsDb;
    }

    // FR-007: the true ecosystem-off reference, on the same surface.
    double offD = -1.0;
    double offRmsDb = 0.0;
    double crossD = -1.0;  // d(true-off, 900=0), default surface only
    double t0Off = -1.0;   // d(true-off, true-off seed twin); the yardstick when rendered
    VoragoTest::PresetDescriptor offDesc{};
    if (renderTrueOff) {
        std::printf("[probe] rendering true-off reference (340 s)...\n");
        const RenderResult ref =
            renderOnce(nullptr, 0.0f, -1.0, surface, wakeBase, true, false, false);
        checkBounded(ref, "true-off reference");
        // FR-006: a reference that is not actually off fails the run.
        REQUIRE(ref.offValid);
        offDesc = ref.d;
        offD = VoragoTest::descriptorDistance(base.d, ref.d);
        offRmsDb = ref.m1RmsDb;
        // THE YARDSTICK (user ruling 2026-09-28, spec Clarifications "Build
        // stage"): t0 = d(true-off, true-off seed twin) - the instrument's
        // randomness with the ecosystem silenced, i.e. everything that is NOT the
        // colony. The gates ask the colony to move the sound by at least twice
        // that; t0On above is printed beside it for the record.
        std::printf("[probe] rendering true-off seed twin (340 s)...\n");
        const RenderResult offTwin =
            renderOnce(nullptr, 0.0f, kSeedTwinNormalized, surface, wakeBase, true, false, false);
        checkBounded(offTwin, "true-off seed twin");
        REQUIRE(offTwin.offValid);
        t0Off = VoragoTest::descriptorDistance(offDesc, offTwin.d);
        if (surface.empty()) {
            std::printf("[probe] rendering 900=0 cross-check reference (340 s)...\n");
            const std::vector<Override> depthZero{{::Vorago::kEcosystemDepthId, 0.0}};
            const RenderResult zero =
                renderOnce(nullptr, 0.0f, -1.0, depthZero, wakeBase, false, false, false);
            checkBounded(zero, "900=0 cross-check");
            crossD = VoragoTest::descriptorDistance(offDesc, zero.d);
        }
    }

    // t0 (ruling 2026-09-28): the true-off seed-twin distance whenever the true-off
    // reference was rendered (every gate and table run); the ecosystem-on twin
    // distance only in the diagnostic modes that render no reference.
    // VORAGO_PROBE_T0=<value> pins the yardstick to the six-seed median off-reseed
    // of the GATE1M run on the same tree and surface (ruling 2026-09-28): one
    // off pair scatters 1.2-5.2 across seeds, so a table run does not rely on
    // its own single pair. The run's own pair stays printed.
    const std::string t0Env = readEnv("VORAGO_PROBE_T0");
    const double t0Pinned = t0Env.empty() ? -1.0 : std::strtod(t0Env.c_str(), nullptr);
    double t0 = t0On;
    if (t0Pinned > 0.0) {
        t0 = t0Pinned;
    } else if (t0Off >= 0.0) {
        t0 = t0Off;
    }
    if (t0Pinned > 0.0) {
        std::printf("t0 pinned by VORAGO_PROBE_T0 = %.4f (six-seed median off-reseed); this run's own "
                    "off pair = %.4f\n",
                    t0Pinned, t0Off);
    }
    const auto ratio = [t0](double d) { return (t0 > 0.0) ? d / t0 : HUGE_VAL; };

    std::vector<KnobRow> rows;
    rows.reserve(kCandidates.size());
    for (const Candidate& c : kCandidates) {
        if (!knobSelected(knobFilter, c.name)) {
            continue;
        }
        KnobRow row;
        row.c = &c;
        const bool hasLo = (c.lo != c.def);
        const bool hasHi = (c.hi != c.def);
        if (hasLo) {
            ExtremeRow x{};
            x.value = c.lo;
            row.extremes.push_back(x);
        }
        if (hasHi) {
            ExtremeRow x{};
            x.value = c.hi;
            row.extremes.push_back(x);
        }
        if (hasLo && hasHi) {
            row.cells = ".lo,.hi";
        } else if (hasLo) {
            row.cells = ".lo";
        } else {
            row.cells = ".hi";
        }
        for (ExtremeRow& x : row.extremes) {
            std::printf("[probe] rendering %s = %g (340 s)...\n", c.name,
                        static_cast<double>(x.value));
            const RenderResult r =
                renderOnce(c.apply, x.value, -1.0, surface, wakeBase, false, false, false);
            ++renderCount;
            checkBounded(r, c.name);
            x.d = VoragoTest::descriptorDistance(base.d, r.d);
            x.rmsDb = r.minuteRmsDb;
            x.colony = r.minuteColony;
            x.lanes = r.lanes;
            // FR-004: KILL in ANY minute against the base render's same minute.
            for (std::size_t m = 0; m < 3u; ++m) {
                if (std::fabs(r.minuteRmsDb[m] - base.minuteRmsDb[m]) > kKillDeltaDb ||
                    r.minuteRmsDb[m] < kKillFloorDbfs) {
                    x.kill = true;
                }
            }
            // FR-004 (iii): OFF-LIKE against the true-off reference, which is
            // always rendered when the knob filter is not "-".
            if (renderTrueOff) {
                x.dOff = VoragoTest::descriptorDistance(r.d, offDesc);
                x.offLike = x.dOff < 0.5 * kGateFactor * t0;
            }
            x.inaudible = x.d < kGateFactor * t0;
            x.counted = !x.inaudible && !x.kill && !x.offLike;
            row.maxD = std::max(row.maxD, x.d);
            if (x.counted) {
                row.counted = true;
                row.best = std::max(row.best, x.d);
            }
        }
        rows.push_back(std::move(row));
    }

    // Counted knobs first (by best counted d), then the rest by max d.
    std::stable_sort(rows.begin(), rows.end(), [](const KnobRow& a, const KnobRow& b) {
        if (a.counted != b.counted) {
            return a.counted;
        }
        return a.counted ? (a.best > b.best) : (a.maxD > b.maxD);
    });

    std::printf("\n=== Vorago_EcosystemRuleProbe (FR-070) ===\n");
    std::printf("renders: %zu (default + seed twin + %zu extremes), 340 s each, 48 kHz, block 512\n",
                renderCount, renderCount - 2u);
    std::printf("surface overrides: %s; wake bases: %s\n", describeOverrides(surface).c_str(),
                wakeBaseEnv.empty() ? "shipped (vorago_voice.h kPeakWakeBase / kLoopWakeBase)" : wakeBaseEnv.c_str());
    std::printf("default M1 stereo RMS: %.2f dBFS\n", base.m1RmsDb);
    std::printf("default per-minute: M1 RMS %.2f dBFS colony %.6f | M2 RMS %.2f dBFS colony %.6f | "
                "M3 RMS %.2f dBFS colony %.6f\n",
                base.minuteRmsDb[0], base.minuteColony[0], base.minuteRmsDb[1],
                base.minuteColony[1], base.minuteRmsDb[2], base.minuteColony[2]);
    std::printf("t0on = d(default, seed twin) = %.4f   (ecosystem on; informational since 2026-09-28)\n",
                t0On);
    if (t0Off >= 0.0) {
        std::printf("t0 = d(true-off, true-off seed twin) = %.4f   INAUDIBLE threshold f*t0 = %.4f"
                    "   (f = %.2f; the yardstick: non-ecosystem randomness, rulings 2026-09-28)\n",
                    t0, kGateFactor * t0, kGateFactor);
    } else {
        std::printf("t0 = t0on = %.4f   INAUDIBLE threshold f*t0 = %.4f   (no true-off reference "
                    "rendered: diagnostic mode, not a gate run)\n",
                    t0, kGateFactor * t0);
    }
    // FR-030 / SC-005: the full descriptor of the base and the true-off reference.
    printDescriptor("default", base.d);
    if (lanesOn) {
        printLanes("default", base.lanes);
    }
    if (renderTrueOff) {
        printDescriptor("trueoff", offDesc);
    } else {
        std::printf("DESCRIPTOR trueoff n/a (true-off reference not rendered)\n");
    }
    if (refD >= 0.0) {
        std::printf("reference (%s): d(default, reference) = %.4f   d/t0 = %.3f   M1 RMS %.2f dBFS\n",
                    describeOverrides(refExtra).c_str(), refD, ratio(refD), refRmsDb);
    }
    if (offD >= 0.0) {
        std::printf("true-off reference (FR-007, offValid): d(default, true-off) = %.4f   "
                    "d/t0 = %.3f   M1 RMS %.2f dBFS\n",
                    offD, ratio(offD), offRmsDb);
        // FR-010 / FR-011: Gate 1 on this surface (read from the log, FR-006).
        std::printf("GATE1 surface=%s t0=%.4f t0on=%.4f d(on,off)=%.4f d/t0=%.3f verdict=%s\n",
                    surfaceEnv.empty() ? "default" : surfaceEnv.c_str(), t0, t0On, offD,
                    ratio(offD), (ratio(offD) >= kGateFactor) ? "PASS" : "FAIL");
    }
    if (crossD >= 0.0) {
        std::printf("cross-check (FR-007): d(true-off, 900=0) = %.4f   d/t0 = %.3f\n", crossD,
                    ratio(crossD));
    }

    if (!rows.empty()) {
        std::printf("\n");
        std::printf("%-14s %-18s %-10s %-12s %-9s %-9s %-9s %s\n", "knob", "range", "default",
                    "extreme", "d", "d/t0", "dOff/t0", "flags / E cells (P-4)");
    }
    // FR-005: colony ratio to the base render's same minute; -1 = undefined.
    const auto colonyRatio = [](double x, double b) {
        return (b > 0.0 && x >= 0.0) ? x / b : -1.0;
    };
    for (const KnobRow& row : rows) {
        std::array<char, 48> range{};
        std::snprintf(range.data(), range.size(), "[%g, %g]", static_cast<double>(row.c->lo),
                      static_cast<double>(row.c->hi));
        std::array<char, 24> def{};
        std::snprintf(def.data(), def.size(), "%g", static_cast<double>(row.c->def));
        bool firstLine = true;
        for (const ExtremeRow& x : row.extremes) {
            std::string flags;
            if (x.kill) {
                flags += "KILL ";
            }
            if (x.offLike) {
                flags += "OFF-LIKE ";
            }
            if (x.inaudible) {
                flags += "INAUDIBLE ";
            }
            if (x.counted) {
                flags += "counted ";
            }
            std::printf("%-14s %-18s %-10s %-12g %-9.4f %-9.3f %-9.3f %s%s\n",
                        firstLine ? row.c->name : "", firstLine ? range.data() : "",
                        firstLine ? def.data() : "", static_cast<double>(x.value), x.d, ratio(x.d),
                        (x.dOff >= 0.0) ? ratio(x.dOff) : -1.0, flags.c_str(),
                        firstLine ? row.cells.c_str() : "");
            for (std::size_t m = 0; m < 3u; ++m) {
                std::printf("%-14s   M%zu rms %.2f dBFS  dRMS %+.2f dB  colony %.6f  "
                            "colony/base %.3f\n",
                            "", m + 1u, x.rmsDb[m], x.rmsDb[m] - base.minuteRmsDb[m], x.colony[m],
                            colonyRatio(x.colony[m], base.minuteColony[m]));
            }
            if (lanesOn) {
                // FR-015: the extreme's cell label, ".lo" or ".hi".
                printLanes(std::string(row.c->name) + ((x.value == row.c->lo) ? ".lo" : ".hi"),
                           x.lanes);
            }
            firstLine = false;
        }
        std::printf("%-14s best counted d %.4f (d/t0 %.3f)%s   max d %.4f (d/t0 %.3f)\n", "",
                    row.best, ratio(row.best), row.counted ? "" : " [none counted]", row.maxD,
                    ratio(row.maxD));
    }

    // FR-005 / FR-012: counted = d >= kGateFactor*t0, not KILL in any minute, not OFF-LIKE.
    std::size_t audibleNonKill = 0;
    for (const KnobRow& row : rows) {
        if (row.counted) {
            ++audibleNonKill;
        }
    }
    if (!rows.empty()) {
        std::printf("\naudible non-kill knobs: %zu of %zu", audibleNonKill, kCandidates.size());
        if (rows.size() != kCandidates.size()) {
            std::printf(" (filtered table: %zu rows)", rows.size());
        }
        std::printf("\n");
    }
    // FR-012: Gate 2 only on a default-surface full-table run. FR-014: it counts
    // only when both GATE1 arms pass on the same tree (read from the logs).
    if (surfaceEnv.empty() && knobFilter.empty()) {
        std::printf("GATE2 verdict=%s (N >= 4)\n", (audibleNonKill >= 4u) ? "PASS" : "FAIL");
    }
    // FR-034: this phase names no roster.
    if (!rows.empty()) {
        std::printf("table for Phase 14 Q2 \xE2\x80\x94 no roster named here\n");
    }
    std::fflush(stdout);
}
