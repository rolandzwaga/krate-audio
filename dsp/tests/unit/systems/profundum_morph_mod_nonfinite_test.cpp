// ==============================================================================
// Profundum Phase 14 (specs/profundum-phase14-morph-modulation) - profundum_morph_mod_nonfinite_test.cpp
// ==============================================================================
// The NaN-injection arms of the Phase 14 morph-modulation tests, in dsp_ieee_tests (dsp/CLAUDE.md "Build & test": a
// test that injects NaN / Inf and asserts on it goes in the all -fno-fast-math executable, so no fast-math COMDAT copy
// of an inline guard is the one the linker keeps). Moved here from profundum_morph_modulator_test.cpp (ApiAndRt:
// FR-006 wheel, setConfig and setTempoBpm sanitising) and profundum_engine_morphmod_test.cpp (Composition (e): FR-006
// at the engine level); the finite arms of those sections stay in dsp_systems_tests.
//   ProfundumMorphModulator_NonFinite   FR-006 (a NaN wheel keeps the latch), FR-001 (a NaN amount / decay keeps the
//                                       applied value), setTempoBpm (a NaN tempo keeps the tempo).
//   ProfundumEngine_MorphModNonFinite   FR-006 engine level: a NaN MorphModWheel event and a NaN setMorphModWheel keep
//                                       the latched 0.4.
// NaNs come from a volatile bit pattern (root CLAUDE.md fast-math rules).
// ==============================================================================

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_morph_modulator.h>

#include "profundum_audibility_readers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

using Krate::DSP::ProfundumEngine;
using Krate::DSP::ProfundumMorphModulator;
using Krate::DSP::Waveform;
using NfConfig = ProfundumMorphModulator::Config;
using NfSource = ProfundumMorphModulator::Source;

namespace VE = Krate::DSP::ProfundumTest;
namespace auRd = Krate::DSP::ProfundumAudibility;

/// A quiet NaN made from a volatile bit pattern (no quiet_NaN() constant: the macOS leg is -ffast-math).
[[nodiscard]] float nfQuietNaN() noexcept {
    volatile std::uint32_t q = 0x7FC00000u;
    return std::bit_cast<float>(static_cast<std::uint32_t>(q));
}

/// Mean period (s) between upward zero crossings of a modulator trace at control rate fc.
[[nodiscard]] double nfUpwardCrossingPeriod(const std::vector<float>& trace, double fc, std::size_t& cycles) noexcept {
    double first = 0.0;
    double last = 0.0;
    std::size_t crossings = 0;
    for (std::size_t k = 1; k < trace.size(); ++k) {
        const double a = static_cast<double>(trace[k - 1]);
        const double b = static_cast<double>(trace[k]);
        if (a < 0.0 && b >= 0.0) {
            const double t = static_cast<double>(k - 1) + (-a) / (b - a);
            if (crossings == 0) {
                first = t;
            }
            last = t;
            ++crossings;
        }
    }
    cycles = crossings >= 2 ? crossings - 1 : 0;
    return cycles > 0 ? (last - first) / static_cast<double>(cycles) / fc : 0.0;
}

/// The modulator's s trace over n intervals after setTempoBpm(each of `tempi`, in order) and setConfig.
[[nodiscard]] std::vector<float> nfTrace(double fs, const NfConfig& c, std::size_t n,
                                         std::initializer_list<float> tempi) {
    ProfundumMorphModulator m;
    m.prepare(fs);
    for (const float t : tempi) {
        m.setTempoBpm(t);
    }
    m.setConfig(c);
    std::vector<float> out(n);
    for (std::size_t k = 0; k < n; ++k) {
        out[k] = m.advanceInterval();
    }
    return out;
}

}  // namespace

TEST_CASE("ProfundumMorphModulator_NonFinite", "[systems][profundum][morphmod][nonfinite]") {
    SECTION("FR-006: a NaN Mod Wheel keeps the latched value") {
        ProfundumMorphModulator m;
        m.prepare(48000.0);
        NfConfig cfg;
        cfg.source = NfSource::ModWheel;
        cfg.amount = 1.0f;
        m.setConfig(cfg);
        m.setModWheel(0.4f);
        REQUIRE(m.advanceInterval() == 0.4f);
        m.setModWheel(nfQuietNaN());
        REQUIRE(m.advanceInterval() == 0.4f);
    }

    SECTION("setConfig: a NaN amount or decay keeps the applied value") {
        ProfundumMorphModulator m;
        m.prepare(48000.0);
        NfConfig c;
        c.source = NfSource::Cycle;
        c.amount = 0.5f;
        c.cycleShape = Waveform::Triangle;
        c.cycleRateHz = 2.0f;
        m.setConfig(c);
        REQUIRE(m.config().amount == 0.5f);
        c.amount = nfQuietNaN();
        m.setConfig(c);
        REQUIRE(m.config().amount == 0.5f);
        c.amount = 0.5f;
        c.envDecayMs = nfQuietNaN();
        m.setConfig(c);
        REQUIRE(m.config().envDecayMs == 300.0f);   // the default applied by the first setConfig
    }

    SECTION("setTempoBpm: a NaN keeps the tempo of a synced Cycle") {
        constexpr double kFs = 48000.0;
        const double fc = kFs / 32.0;
        NfConfig synced;
        synced.source = NfSource::Cycle;
        synced.amount = 1.0f;
        synced.cycleShape = Waveform::Sine;
        synced.cycleSync = true;
        synced.cycleNote = Krate::DSP::kNoteValueDropdownMapping[13].note;   // 1/4: one beat per cycle
        synced.cycleModifier = Krate::DSP::kNoteValueDropdownMapping[13].modifier;
        const auto n = static_cast<std::size_t>(std::ceil(11.5 * (60.0 / 90.0) * fc));
        std::size_t cycles90 = 0;
        std::size_t cyclesNaN = 0;
        const double p90 = nfUpwardCrossingPeriod(nfTrace(kFs, synced, n, {90.0f}), fc, cycles90);
        const double pNaN = nfUpwardCrossingPeriod(nfTrace(kFs, synced, n, {90.0f, nfQuietNaN()}), fc, cyclesNaN);
        CAPTURE(p90, pNaN);
        REQUIRE(cycles90 >= 10);
        REQUIRE(cyclesNaN >= 10);
        REQUIRE(std::abs(pNaN - 60.0 / 90.0) <= 0.005 * (60.0 / 90.0));   // 90 kept through the NaN
        REQUIRE(std::abs(pNaN - p90) <= 1.0e-9);
    }
}

TEST_CASE("ProfundumEngine_MorphModNonFinite", "[systems][profundum][morphmod][voice-engine][nonfinite]") {
    enableFTZDAZ();
    using MmKind = VE::VoiceEngineEventKind;

    // FR-006 at the engine level: Mod Wheel source, A 1, b 0; the wheel events land on grid points (the Phase 8
    // showcase pair, MONO, E1, 48 kHz, host block 64; the script of ProfundumEngine_MorphModComposition (e)).
    constexpr std::uint64_t kAt3 = 9600;
    constexpr std::uint64_t kAt4 = 12800;
    NfConfig c{};
    c.source = NfSource::ModWheel;
    c.amount = 1.0f;
    c.envAttackMs = 0.1f;
    c.envDecayMs = 300.0f;
    c.envSustain = 0.0f;
    c.cycleShape = Waveform::Square;
    c.cycleRateHz = 2.0f;
    VE::VoiceEngineScript s = auRd::auScript(
        auRd::AuSetting{.a = auRd::auStateAs(), .b = auRd::auStateB(), .position = 0.0f}, auRd::kAuE1, 16000);
    REQUIRE(s.fs == 48000.0);
    REQUIRE(s.hostBlock == 64u);
    s.events.push_back(VE::VoiceEngineEvent{.sample = 0, .kind = MmKind::MorphRampMs, .value = 40.0f});
    s.morphMod = c;
    s.morphModWheel = 0.0f;
    s.morphModBypass = false;
    s.events.push_back(VE::VoiceEngineEvent{.sample = kAt3, .kind = MmKind::MorphModWheel, .value = 0.4f});
    s.events.push_back(VE::VoiceEngineEvent{.sample = kAt4, .kind = MmKind::MorphModWheel, .value = nfQuietNaN()});
    const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);
    const auto sourceAt = [&t](std::uint64_t sample) {
        for (const VE::VoiceEngineGridRecord& r : t.grid) {
            if (r.sample == sample)
                return r.morphModSource;
        }
        FAIL("no grid record at sample " << sample);
        return -1.0f;
    };
    CHECK(sourceAt(kAt3) == 0.4f);
    CHECK(sourceAt(kAt4) == 0.4f);
    CHECK(sourceAt(kAt4 + 32) == 0.4f);

    // The read-back of the pending wheel (FR-066 pattern): a NaN keeps 0.4.
    auto e = std::make_unique<ProfundumEngine>();
    e->setMorphModWheel(0.4f);
    e->setMorphModWheel(nfQuietNaN());
    CHECK(e->morphModWheel() == 0.4f);
}
