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
// copy of the math). t0 = d(default, seed twin); a knob's audibility is the max
// d over its extremes, flagged INAUDIBLE when that is < 2 * t0.
//
// The table and proposed roster are printed with std::printf for T004 to copy
// into compliance.md. Assertions (only these): every render finite by bit
// pattern with peak <= 0.9661; the default render's M1 stereo RMS >= -60 dBFS.
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
//   VORAGO_PROBE_WAKEBASE = "<float>" overwrites every voice's peak and loop
//       wake BASES (vorago_voice.h prepare(): 0.50 / 0.50) after activation, so
//       the colony owns those wakes instead of topping up a half-awake surface
//       (the noise base is the parameter kNoiseWakeId = 301: override it).
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

struct RenderResult {
    VoragoTest::PresetDescriptor d{};
    bool allFinite = true;
    float peak = 0.0f;
    double m1RmsDb = -240.0;
};

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
RenderResult renderOnce(KnobApply apply, float value, double seedNormalized,
                        const std::vector<Override>& overrides, double wakeBase) {
    auto proc = std::make_unique<::Vorago::Processor>();
    REQUIRE(proc->initialize(nullptr) == Steinberg::kResultOk);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlock);
    setup.sampleRate = kSampleRate;
    REQUIRE(proc->setupProcessing(setup) == Steinberg::kResultOk);
    REQUIRE(proc->setActive(true) == Steinberg::kResultOk);

    if (apply != nullptr || wakeBase >= 0.0) {
        // The engine is a non-const heap object (processor.h), so the cast is
        // well-defined; single-threaded, between process() calls.
        REQUIRE(proc->engineForTest() != nullptr);
        auto& engine = const_cast<Krate::DSP::VoragoEngine&>(  // NOLINT(cppcoreguidelines-pro-type-const-cast): plan 4.3 - the engine is a non-const heap object; engineForTest() is the only accessor
            *proc->engineForTest());
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
    res.m1RmsDb = stereoRmsDb(l.first(kMinuteSamples), r.first(kMinuteSamples));
    return res;
}

struct ExtremeRow {
    float value;
    double d;
};

struct KnobRow {
    const Candidate* c = nullptr;
    std::vector<ExtremeRow> extremes;
    double best = 0.0;
    std::string cells;  // P-4: ".lo,.hi" or ".hi"
    std::size_t cellCount = 0;
};

}  // namespace

TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]") {
    std::size_t renderCount = 0;

    const auto checkBounded = [](const RenderResult& r, const char* label) {
        INFO("render: " << label << " peak " << r.peak);
        CHECK(r.allFinite);
        CHECK(r.peak <= kOutputCeiling);
    };

    const std::vector<Override> surface = parseOverrides(readEnv("VORAGO_PROBE_SURFACE"));
    const std::string knobFilter = readEnv("VORAGO_PROBE_KNOBS");
    const std::vector<Override> refExtra = parseOverrides(readEnv("VORAGO_PROBE_REF"));
    const std::string wakeBaseEnv = readEnv("VORAGO_PROBE_WAKEBASE");
    const double wakeBase = wakeBaseEnv.empty() ? -1.0 : std::strtod(wakeBaseEnv.c_str(), nullptr);
    std::printf("[probe] surface overrides: %s; knob filter: %s; reference: %s; wake bases: %s\n",
                describeOverrides(surface).c_str(), knobFilter.empty() ? "all" : knobFilter.c_str(),
                describeOverrides(refExtra).c_str(),
                wakeBaseEnv.empty() ? "shipped (0.50/0.50)" : wakeBaseEnv.c_str());

    std::printf("[probe] rendering default surface (340 s)...\n");
    const RenderResult base = renderOnce(nullptr, 0.0f, -1.0, surface, wakeBase);
    ++renderCount;
    checkBounded(base, "default");
    INFO("default M1 stereo RMS " << base.m1RmsDb << " dBFS");
    REQUIRE(base.m1RmsDb >= -60.0);

    std::printf("[probe] rendering seed twin (340 s)...\n");
    const RenderResult twin = renderOnce(nullptr, 0.0f, kSeedTwinNormalized, surface, wakeBase);
    ++renderCount;
    checkBounded(twin, "seed twin");
    const double t0 = VoragoTest::descriptorDistance(base.d, twin.d);

    double refD = -1.0;
    double refRmsDb = 0.0;
    if (!refExtra.empty()) {
        std::vector<Override> refOverrides = surface;
        refOverrides.insert(refOverrides.end(), refExtra.begin(), refExtra.end());
        std::printf("[probe] rendering reference (340 s)...\n");
        const RenderResult ref = renderOnce(nullptr, 0.0f, -1.0, refOverrides, wakeBase);
        checkBounded(ref, "reference");
        refD = VoragoTest::descriptorDistance(base.d, ref.d);
        refRmsDb = ref.m1RmsDb;
    }

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
            row.extremes.push_back({.value = c.lo, .d = 0.0});
        }
        if (hasHi) {
            row.extremes.push_back({.value = c.hi, .d = 0.0});
        }
        if (hasLo && hasHi) {
            row.cells = ".lo,.hi";
        } else if (hasLo) {
            row.cells = ".lo";
        } else {
            row.cells = ".hi";
        }
        row.cellCount = (hasLo ? 1u : 0u) + (hasHi ? 1u : 0u);
        for (ExtremeRow& x : row.extremes) {
            std::printf("[probe] rendering %s = %g (340 s)...\n", c.name,
                        static_cast<double>(x.value));
            const RenderResult r = renderOnce(c.apply, x.value, -1.0, surface, wakeBase);
            ++renderCount;
            checkBounded(r, c.name);
            x.d = VoragoTest::descriptorDistance(base.d, r.d);
            row.best = std::max(row.best, x.d);
        }
        rows.push_back(std::move(row));
    }

    std::stable_sort(rows.begin(), rows.end(),
                     [](const KnobRow& a, const KnobRow& b) { return a.best > b.best; });

    const auto ratio = [t0](double d) { return (t0 > 0.0) ? d / t0 : HUGE_VAL; };

    std::printf("\n=== Vorago_EcosystemRuleProbe (FR-070) ===\n");
    std::printf("renders: %zu (default + seed twin + %zu extremes), 340 s each, 48 kHz, block 512\n",
                renderCount, renderCount - 2u);
    std::printf("surface overrides: %s; wake bases: %s\n", describeOverrides(surface).c_str(),
                wakeBaseEnv.empty() ? "shipped (0.50/0.50)" : wakeBaseEnv.c_str());
    std::printf("default M1 stereo RMS: %.2f dBFS\n", base.m1RmsDb);
    std::printf("t0 = d(default, seed twin) = %.4f   INAUDIBLE threshold 2*t0 = %.4f\n", t0,
                2.0 * t0);
    if (refD >= 0.0) {
        std::printf("reference (%s): d(default, reference) = %.4f   d/t0 = %.3f   M1 RMS %.2f dBFS\n",
                    describeOverrides(refExtra).c_str(), refD, ratio(refD), refRmsDb);
    }
    std::printf("\n");
    std::printf("%-14s %-18s %-10s %-12s %-9s %-9s %-10s %s\n", "knob", "range", "default",
                "extreme", "d", "d/t0", "flag", "E cells (P-4)");
    for (const KnobRow& row : rows) {
        std::array<char, 48> range{};
        std::snprintf(range.data(), range.size(), "[%g, %g]", static_cast<double>(row.c->lo),
                      static_cast<double>(row.c->hi));
        std::array<char, 24> def{};
        std::snprintf(def.data(), def.size(), "%g", static_cast<double>(row.c->def));
        const bool inaudible = row.best < 2.0 * t0;
        const char* flag = inaudible ? "INAUDIBLE" : "audible";
        bool firstLine = true;
        for (const ExtremeRow& x : row.extremes) {
            std::printf("%-14s %-18s %-10s %-12g %-9.4f %-9.3f %-10s %s\n",
                        firstLine ? row.c->name : "", firstLine ? range.data() : "",
                        firstLine ? def.data() : "",
                        static_cast<double>(x.value), x.d, ratio(x.d),
                        firstLine ? flag : "",
                        firstLine ? row.cells.c_str() : "");
            firstLine = false;
        }
        std::printf("%-14s %-18s %-10s %-12s %-9.4f %-9.3f   (best)\n", "", "", "", "", row.best,
                    ratio(row.best));
    }

    // Proposed roster: the top 4-6 audible knobs; P-3 arithmetic N = 38 + |E-ext|.
    std::vector<const KnobRow*> audible;
    for (const KnobRow& row : rows) {
        if (row.best >= 2.0 * t0) {
            audible.push_back(&row);
        }
    }
    std::printf("\naudible knobs: %zu of %zu\n", audible.size(), rows.size());
    if (audible.size() < 4u) {
        std::printf("WARNING: fewer than 4 audible knobs - a 4-6 roster cannot be proposed "
                    "from audible knobs alone (surface at G1)\n");
    }
    const std::size_t maxK = std::min<std::size_t>(6u, audible.size());
    for (std::size_t k = 4; k <= maxK; ++k) {
        std::size_t eExt = 0;
        std::printf("|R| = %zu: ", k);
        for (std::size_t i = 0; i < k; ++i) {
            eExt += audible[i]->cellCount;
            std::printf("%s%s(%s)", (i == 0u) ? "" : ", ", audible[i]->c->name,
                        audible[i]->cells.c_str());
        }
        std::printf("  -> |E-ext| = %zu, N = 38 + %zu = %zu\n", eExt, eExt, 38u + eExt);
    }
    if (maxK >= 4u) {
        std::printf("PROPOSED R (top %zu audible): ", maxK);
        for (std::size_t i = 0; i < maxK; ++i) {
            std::printf("%s%s", (i == 0u) ? "" : ", ", audible[i]->c->name);
        }
        std::printf("\n");
    }
    std::printf("=== STOP: gate G1 - ratify R with the user (FR-071) ===\n");
    std::fflush(stdout);
}
