// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_engine_test.cpp
// ==============================================================================
// Will hold: SC-008, SC-011, SC-012, SC-022, SC-024, SC-026 - SC-029, SC-031, SC-032, SC-036.
// Filled by: T008 (the direct cases below, tasks.md deviation 3), T011 (the driver / observer cases), T014.
// Includes every Phase 9 header so each is compiled by every gate from Group 1 on.
//
// T008 drives ProfundumEngine directly: auto e = std::make_unique<ProfundumEngine>(); endpoints A = A_s (showcase A
// with ampEnv.sustain = 1, plan S1 C-8) and B = showcase B; e->prepare(48000.0); processBlock in host blocks of 64;
// notes issued between blocks (plan S1 C-9: the engine API carries no sample offsets).
//   VoiceEngine_StaticContract          FR-020 / FR-025 / SC-022: constants, latency 0, noexcept audio surface
//   VoiceEngine_GetterReadBack          FR-066 / FR-031 / FR-030: every setter's pending value reads back bitwise
//   VoiceEngine_RendersNonSilent        FR-020: a held E1 is finite and >= -40 dBFS; processBlock overwrites
//   VoiceEngine_EventsLatchedToGrid     FR-063 / C-9: an event applies at the next grid point; queue overflow counted;
//                                       note-offs never dropped
//   VoiceEngine_ReprepareKeepsSettings  FR-064: a re-prepare at a new rate keeps every setting
//   VoiceEngine_PostSumPlacementRuns    FR-014 / C-14: PostSum POLY renders; MONO is PerVoice whatever the config
// TU-local names carry the `pe` / `Pe` prefix inside an anonymous namespace.
//
// T011 (tasks.md T011, deviations 3 and 6) appends the cases that run through the T009 driver / observer
// (ProfundumTest::voiceEngineRender, profundum_voice_engine_test_helpers.h), or drive the engine directly where an
// arm needs reset() / prepare() inside the run: SC-008, SC-011 (in-process arms), SC-012, SC-022, SC-024, SC-026 -
// SC-029, SC-031, SC-032, SC-036; FR-005, FR-021, FR-028, FR-030, FR-045. Decisions: evidence/run-log/T011.txt.
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

// Coexistence proof: the new names compile beside the voice / engine precedents and the two VoiceStates.
#include <krate/dsp/systems/seraphis_voice.h>
#include <krate/dsp/systems/voice_allocator.h>
#include <krate/dsp/systems/vorago_engine.h>

#include "bass_morph_state_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/processors/note_processor.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/morph_state.h>

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>
#include <render_fingerprint.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::BassMorphEngine;
using Krate::DSP::GlideMode;
using Krate::DSP::MonoMode;
using Krate::DSP::MorphState;
using Krate::DSP::NoteMemory;
using Krate::DSP::PortaMode;
using Krate::DSP::ProfundumEngine;
using Krate::DSP::ProfundumEngineConfig;
using Krate::DSP::ProfundumMacro;
using Krate::DSP::ProfundumMacroValues;
using Krate::DSP::ProfundumNonlinearPlacement;

constexpr double kPeFs = 48000.0;
constexpr std::size_t kPeHostBlock = 64;
constexpr std::size_t kPeMaxBlock = 4096;
constexpr std::uint8_t kPeNoteE1 = 28;
constexpr std::uint8_t kPeNoteA1 = 33;
constexpr std::uint8_t kPeNoteE2 = 40;
constexpr std::uint8_t kPeNoteC3 = 48;
constexpr std::uint8_t kPeVelocity = 100;

[[nodiscard]] bool peSameBits(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// Bit test (this TU builds with the shipping FP mode; std::isfinite may be folded away under -ffast-math).
[[nodiscard]] bool peFinite(float x) noexcept {
    return (std::bit_cast<std::uint32_t>(x) & 0x7F800000u) != 0x7F800000u;
}

[[nodiscard]] std::size_t peSamples(double seconds) noexcept {
    return static_cast<std::size_t>(seconds * kPeFs + 0.5);
}

/// Plan S1 C-8: base "A" with ampEnv.sustain = 1 (A_s).
[[nodiscard]] MorphState peStateAs() {
    MorphState s = Krate::DSP::BassMorphTest::showcasePair().first;
    s.ampEnv.sustain = 1.0f;
    return s;
}

/// Endpoints A_s / B set before prepare (BassMorphEngine::prepare preserves them), then prepare(fs, cfg).
[[nodiscard]] std::unique_ptr<ProfundumEngine> peMakeEngine(const ProfundumEngineConfig& cfg = {},
                                                            double fs = kPeFs) {
    auto e = std::make_unique<ProfundumEngine>();
    const auto pair = Krate::DSP::BassMorphTest::showcasePair();
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, peStateAs()));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, pair.second));
    e->prepare(fs, cfg);
    return e;
}

struct PeStereo {
    std::vector<float> l;
    std::vector<float> r;
};

/// Renders n samples in host blocks of `block` (<= kPeMaxBlock), appending to `out`. Every block buffer is
/// pre-filled with `fill`, so a sample the engine does not write keeps it.
void peRender(ProfundumEngine& e, PeStereo& out, std::size_t n, std::size_t block = kPeHostBlock,
              float fill = 0.0f) {
    static std::array<float, kPeMaxBlock> bl{};
    static std::array<float, kPeMaxBlock> br{};
    block = std::clamp<std::size_t>(block, 1, kPeMaxBlock);
    std::size_t done = 0;
    while (done < n) {
        const std::size_t m = std::min(block, n - done);
        std::fill_n(bl.data(), m, fill);
        std::fill_n(br.data(), m, fill);
        e.processBlock(bl.data(), br.data(), m);
        out.l.insert(out.l.end(), bl.begin(), bl.begin() + static_cast<std::ptrdiff_t>(m));
        out.r.insert(out.r.end(), br.begin(), br.begin() + static_cast<std::ptrdiff_t>(m));
        done += m;
    }
}

[[nodiscard]] bool peAllFinite(const PeStereo& s) noexcept {
    for (std::size_t i = 0; i < s.l.size(); ++i) {
        if (!peFinite(s.l[i]) || !peFinite(s.r[i]))
            return false;
    }
    return true;
}

/// RMS over both channels of [from, to), in dBFS (-300 for digital silence).
[[nodiscard]] double peRmsDb(const PeStereo& s, std::size_t from, std::size_t to) noexcept {
    to = std::min(to, s.l.size());
    if (from >= to)
        return -300.0;
    double acc = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        acc += static_cast<double>(s.l[i]) * static_cast<double>(s.l[i]);
        acc += static_cast<double>(s.r[i]) * static_cast<double>(s.r[i]);
    }
    const double rms = std::sqrt(acc / (2.0 * static_cast<double>(to - from)));
    return rms > 0.0 ? 20.0 * std::log10(rms) : -300.0;
}

[[nodiscard]] float pePeak(const PeStereo& s, std::size_t from, std::size_t to) noexcept {
    to = std::min(to, s.l.size());
    float p = 0.0f;
    for (std::size_t i = from; i < to; ++i)
        p = std::max({p, std::fabs(s.l[i]), std::fabs(s.r[i])});
    return p;
}

[[nodiscard]] std::size_t peMismatches(const PeStereo& a, const PeStereo& b) noexcept {
    if (a.l.size() != b.l.size())
        return std::max(a.l.size(), b.l.size());
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.l.size(); ++i) {
        if (!peSameBits(a.l[i], b.l[i]) || !peSameBits(a.r[i], b.r[i]))
            ++n;
    }
    return n;
}

}  // namespace

// ------------------------------------------------------------------------------
// T008 - direct engine cases (FR-020 - FR-032, FR-060 - FR-066; plan S6)
// ------------------------------------------------------------------------------

TEST_CASE("VoiceEngine_StaticContract", "[systems][profundum][voice-engine][fr-020][fr-025][sc-022]") {
    using E = ProfundumEngine;
    STATIC_REQUIRE(E::kLimiterLookaheadSamples == 64);   // FR-025 as amended by G14-1: the limiter look-ahead is the chain's only latency
    {
        E e;
        CHECK(e.latencySamples() == 0u);   // before prepare
        e.prepare(48000.0, ProfundumEngineConfig{});
        CHECK(e.latencySamples() == E::kLimiterLookaheadSamples);
    }
    STATIC_REQUIRE(E::kMaxVoices == 8);
    STATIC_REQUIRE(E::kDefaultPolyphony == 4);
    STATIC_REQUIRE(E::kControlInterval == 32);
    STATIC_REQUIRE(E::kMaxQueuedEvents == 256);
    STATIC_REQUIRE(E::kDefaultSeed == 0xB455u);
    STATIC_REQUIRE(E::kMotionDriftSalt == 1);
    STATIC_REQUIRE(E::kFieldDriftSalt == 2);
    STATIC_REQUIRE(E::kVoiceSaltBase == 16);
    STATIC_REQUIRE(E::kDefaultCeilingDb == Krate::DSP::TruePeakLimiter::kDefaultCeilingDb);
    STATIC_REQUIRE(E::kDefaultTempoBpm == Krate::DSP::BassStereoField::kDefaultTempoBpm);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<E::VoiceMode>, std::uint8_t>);

    // Plan S6.2 / S6.11: every audio-thread method is noexcept (reset() is control-thread, not in the list).
    // Unevaluated operands only.
    STATIC_REQUIRE(noexcept(std::declval<E&>().noteOn(std::uint8_t{0}, std::uint8_t{0})));
    STATIC_REQUIRE(noexcept(std::declval<E&>().noteOff(std::uint8_t{0})));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setPitchBend(0.0f)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setTempoBpm(0.0f)));
    STATIC_REQUIRE(
        noexcept(std::declval<E&>().setEndpoint(BassMorphEngine::Endpoint::A, std::declval<const MorphState&>())));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setMorphPosition(0.0f)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setMorphRampMs(0.0f)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setAnchorMorphMode(BassMorphEngine::AnchorMorphMode::Blend)));
    STATIC_REQUIRE(noexcept(std::declval<const E&>().serializeMorphRuntime(nullptr, std::size_t{0})));
    STATIC_REQUIRE(noexcept(std::declval<E&>().restoreMorphRuntime(nullptr, std::size_t{0})));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setVoiceMode(E::VoiceMode::Mono)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setPolyphony(std::size_t{4})));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setMonoPriority(MonoMode::LastNote)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setLegato(false)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setPortamentoTimeMs(0.0f)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setPortamentoMode(PortaMode::Always)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setGlideMode(GlideMode::ConstantTime)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setNoteMemoryMode(NoteMemory::Mode::AlwaysRetrigger)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setNoteRetrigger(NoteMemory::Retrigger::Reset)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setPitchBendRange(2.0f)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setOutputCeilingDb(-1.0f)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setMacro(ProfundumMacro::Weight, 0.5f)));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setMacros(ProfundumMacroValues{})));
    STATIC_REQUIRE(noexcept(std::declval<E&>().setSeed(0u)));
    STATIC_REQUIRE(noexcept(
        std::declval<E&>().processBlock(static_cast<float*>(nullptr), static_cast<float*>(nullptr), std::size_t{0})));
    STATIC_REQUIRE(noexcept(std::declval<const E&>().droppedEvents()));
    STATIC_REQUIRE(!std::is_copy_constructible_v<E>);

    // The defaults read back after prepare (FR-022, FR-025, FR-028, C-19).
    enableFTZDAZ();
    auto eng = peMakeEngine();
    CHECK(eng->voiceMode() == E::VoiceMode::Mono);
    CHECK(eng->polyphony() == E::kDefaultPolyphony);
    CHECK(peSameBits(eng->outputCeilingDb(), E::kDefaultCeilingDb));
    CHECK(peSameBits(eng->pitchBendRange(), E::kDefaultBendRangeSt));
    CHECK(peSameBits(eng->tempoBpm(), E::kDefaultTempoBpm));
    CHECK(eng->seed() == E::kDefaultSeed);
    CHECK(eng->noteMemoryMode() == NoteMemory::Mode::AlwaysRetrigger);
    CHECK(eng->noteRetrigger() == NoteMemory::Retrigger::Reset);
    CHECK(eng->droppedEvents() == 0u);
}

TEST_CASE("VoiceEngine_GetterReadBack", "[systems][profundum][voice-engine][fr-066][fr-031][fr-030]") {
    using E = ProfundumEngine;
    enableFTZDAZ();
    auto e = peMakeEngine();
    const auto pair = Krate::DSP::BassMorphTest::showcasePair();
    MorphState a2 = peStateAs();
    a2.recipe.edge = 0.375f;
    a2.mass = 62.5f;
    REQUIRE(Krate::DSP::isValidMorphState(a2));

    // Morph surface (FR-030).
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, a2));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, pair.second));
    CHECK(Krate::DSP::BassMorphTest::bitEqual(e->getEndpoint(BassMorphEngine::Endpoint::A), a2));
    CHECK(Krate::DSP::BassMorphTest::bitEqual(e->getEndpoint(BassMorphEngine::Endpoint::B), pair.second));
    e->setMorphPosition(0.375f);
    CHECK(peSameBits(e->morphPosition(), 0.375f));
    e->setMorphRampMs(250.0f);
    CHECK(peSameBits(e->morphRampMs(), 250.0f));
    e->setAnchorMorphMode(BassMorphEngine::AnchorMorphMode::Punch);
    CHECK(e->anchorMorphMode() == BassMorphEngine::AnchorMorphMode::Punch);

    // Events surface.
    e->setPitchBend(0.25f);
    CHECK(peSameBits(e->pitchBend(), 0.25f));
    e->setTempoBpm(97.5f);
    CHECK(peSameBits(e->tempoBpm(), 97.5f));

    // Non-morphed controls (FR-031).
    e->setVoiceMode(E::VoiceMode::Poly);
    CHECK(e->voiceMode() == E::VoiceMode::Poly);
    e->setPolyphony(6);
    CHECK(e->polyphony() == 6u);
    e->setMonoPriority(MonoMode::LowNote);
    CHECK(e->monoPriority() == MonoMode::LowNote);
    e->setLegato(true);
    CHECK(e->legato());
    e->setPortamentoTimeMs(120.5f);
    CHECK(peSameBits(e->portamentoTimeMs(), 120.5f));
    e->setPortamentoMode(PortaMode::LegatoOnly);
    CHECK(e->portamentoMode() == PortaMode::LegatoOnly);
    e->setGlideMode(GlideMode::ConstantRate);
    CHECK(e->glideMode() == GlideMode::ConstantRate);
    e->setNoteMemoryMode(NoteMemory::Mode::EnvelopeRetriggerOnly);
    CHECK(e->noteMemoryMode() == NoteMemory::Mode::EnvelopeRetriggerOnly);
    e->setNoteRetrigger(NoteMemory::Retrigger::Continue);
    CHECK(e->noteRetrigger() == NoteMemory::Retrigger::Continue);
    e->setPitchBendRange(7.0f);
    CHECK(peSameBits(e->pitchBendRange(), 7.0f));
    e->setOutputCeilingDb(-3.5f);
    CHECK(peSameBits(e->outputCeilingDb(), -3.5f));
    e->setSeed(0x1234u);
    CHECK(e->seed() == 0x1234u);

    // Macros (FR-055): per macro and field-wise.
    e->setMacro(ProfundumMacro::Weight, 0.125f);
    e->setMacro(ProfundumMacro::DarkBright, 0.25f);
    e->setMacro(ProfundumMacro::TightLoose, 0.625f);
    e->setMacro(ProfundumMacro::CleanViolent, 0.875f);
    CHECK(peSameBits(e->macro(ProfundumMacro::Weight), 0.125f));
    CHECK(peSameBits(e->macro(ProfundumMacro::DarkBright), 0.25f));
    CHECK(peSameBits(e->macro(ProfundumMacro::TightLoose), 0.625f));
    CHECK(peSameBits(e->macro(ProfundumMacro::CleanViolent), 0.875f));
    e->setMacros(ProfundumMacroValues{.weight = 0.2f, .darkBright = 0.8f, .tightLoose = 0.3f, .cleanViolent = 0.9f});
    const ProfundumMacroValues mv = e->macros();
    CHECK(peSameBits(mv.weight, 0.2f));
    CHECK(peSameBits(mv.darkBright, 0.8f));
    CHECK(peSameBits(mv.tightLoose, 0.3f));
    CHECK(peSameBits(mv.cleanViolent, 0.9f));

    // Clamps (FR-061 ranges).
    e->setPolyphony(0);
    CHECK(e->polyphony() == 1u);
    e->setPolyphony(9);
    CHECK(e->polyphony() == 8u);
    e->setPitchBendRange(30.0f);
    CHECK(peSameBits(e->pitchBendRange(), 24.0f));
    e->setOutputCeilingDb(3.0f);
    CHECK(peSameBits(e->outputCeilingDb(), 0.0f));
    e->setOutputCeilingDb(-20.0f);
    CHECK(peSameBits(e->outputCeilingDb(), -12.0f));

    // The read-back is the pending value: unchanged by rendering.
    PeStereo out;
    peRender(*e, out, 4096);
    CHECK(e->polyphony() == 8u);
    CHECK(peSameBits(e->outputCeilingDb(), -12.0f));
    CHECK(peSameBits(e->morphPosition(), 0.375f));
    CHECK(Krate::DSP::BassMorphTest::bitEqual(e->getEndpoint(BassMorphEngine::Endpoint::A), a2));
    CHECK(peAllFinite(out));
}

TEST_CASE("VoiceEngine_RendersNonSilent", "[systems][profundum][voice-engine][fr-020]") {
    enableFTZDAZ();
    auto e = peMakeEngine();
    REQUIRE(e->voiceMode() == ProfundumEngine::VoiceMode::Mono);

    // processBlock overwrites: before any note and while one sounds, a buffer pre-filled with 7.0f is fully written.
    PeStereo silent;
    peRender(*e, silent, 1024, kPeHostBlock, 7.0f);
    std::size_t unwritten = 0;
    for (std::size_t i = 0; i < silent.l.size(); ++i) {
        if (peSameBits(silent.l[i], 7.0f) || peSameBits(silent.r[i], 7.0f))
            ++unwritten;
    }
    CHECK(unwritten == 0u);

    e->noteOn(kPeNoteE1, kPeVelocity);
    PeStereo out;
    const std::size_t n = peSamples(1.0);
    peRender(*e, out, n, kPeHostBlock, 7.0f);
    for (std::size_t i = 0; i < out.l.size(); ++i) {
        if (peSameBits(out.l[i], 7.0f) || peSameBits(out.r[i], 7.0f))
            ++unwritten;
    }
    const double rms = peRmsDb(out, peSamples(0.1), n);
    std::cout << std::setprecision(6) << "VoiceEngine_RendersNonSilent: held E1 RMS " << rms << " dBFS\n";
    CHECK(unwritten == 0u);
    CHECK(peAllFinite(out));
    CHECK(rms >= -40.0);
    CHECK(pePeak(out, 0, n) <= 1.0f);
}

TEST_CASE("VoiceEngine_EventsLatchedToGrid", "[systems][profundum][voice-engine][fr-063][c-9]") {
    enableFTZDAZ();

    SECTION("a mid-interval note-on applies at the next grid point") {
        // Identical host partition [64][5][27][32][64 ...]; the note-on is issued at sample 69 in one render and at
        // sample 96 (the grid point both latch to) in the other.
        const std::size_t total = peSamples(0.5);
        auto runAt = [total](std::size_t noteAt) {
            auto e = peMakeEngine();
            PeStereo out;
            const std::size_t cuts[] = {64, 69, 96, 128};
            std::size_t prev = 0;
            for (const std::size_t c : cuts) {
                peRender(*e, out, c - prev);
                if (c == noteAt)
                    e->noteOn(kPeNoteE1, kPeVelocity);
                prev = c;
            }
            peRender(*e, out, total - out.l.size());
            return out;
        };
        const PeStereo at69 = runAt(69);
        const PeStereo at96 = runAt(96);
        const PeStereo at64 = runAt(64);   // control: issued at grid 64, applied there
        const std::size_t diff = peMismatches(at69, at96);
        std::cout << "VoiceEngine_EventsLatchedToGrid: note at 69 vs 96 mismatches " << diff << "\n";
        CHECK(diff == 0u);
        CHECK(peRmsDb(at69, peSamples(0.1), total) >= -40.0);
        // Positive control: a note issued at grid 64 lands one interval earlier, so the renders differ.
        CHECK(peMismatches(at64, at96) > 0u);
    }

    SECTION("a full queue drops note-ons and counts them; note-offs are never dropped") {
        for (const auto mode : {ProfundumEngine::VoiceMode::Mono, ProfundumEngine::VoiceMode::Poly}) {
            INFO("mode " << static_cast<int>(mode));
            auto e = peMakeEngine();
            e->setVoiceMode(mode);
            PeStereo out;
            peRender(*e, out, 64);
            for (int i = 0; i < 300; ++i)
                e->noteOn(static_cast<std::uint8_t>(24 + (i % 48)), kPeVelocity);
            CHECK(e->droppedEvents() == 44u);
            peRender(*e, out, peSamples(0.25));
            CHECK(peRmsDb(out, 64, out.l.size()) >= -40.0);   // the accepted note-ons sound
            for (int n = 24; n < 72; ++n)
                e->noteOff(static_cast<std::uint8_t>(n));
            const std::size_t tailFrom = out.l.size();
            peRender(*e, out, peSamples(3.0));
            CHECK(e->droppedEvents() == 44u);
            CHECK(peAllFinite(out));
            // A_s releases in 50 ms: nothing may still sound in the last 100 ms of the 3 s tail.
            const float lastPeak = pePeak(out, out.l.size() - peSamples(0.1), out.l.size());
            std::cout << "VoiceEngine_EventsLatchedToGrid: mode " << static_cast<int>(mode) << " tail peak "
                      << std::setprecision(6) << lastPeak << " (tail from " << tailFrom << ")\n";
            CHECK(lastPeak <= 1.0e-6f);
        }
    }
}

TEST_CASE("VoiceEngine_ReprepareKeepsSettings", "[systems][profundum][voice-engine][fr-064]") {
    enableFTZDAZ();
    auto e = peMakeEngine();
    e->setPolyphony(6);
    e->setOutputCeilingDb(-6.0f);
    e->setMacros(ProfundumMacroValues{.weight = 0.2f, .darkBright = 0.8f, .tightLoose = 0.3f, .cleanViolent = 0.9f});
    e->setNoteMemoryMode(NoteMemory::Mode::Legato);
    e->setNoteRetrigger(NoteMemory::Retrigger::Continue);
    e->setPitchBendRange(12.0f);
    e->setVoiceMode(ProfundumEngine::VoiceMode::Poly);
    e->setMonoPriority(MonoMode::HighNote);
    e->setPortamentoTimeMs(80.0f);
    e->setTempoBpm(140.0f);
    e->setMorphPosition(0.25f);
    e->setSeed(0x2222u);
    PeStereo out;
    e->noteOn(kPeNoteE1, kPeVelocity);
    peRender(*e, out, 4096);

    e->prepare(96000.0);
    CHECK(e->polyphony() == 6u);
    CHECK(peSameBits(e->outputCeilingDb(), -6.0f));
    const ProfundumMacroValues mv = e->macros();
    CHECK(peSameBits(mv.weight, 0.2f));
    CHECK(peSameBits(mv.darkBright, 0.8f));
    CHECK(peSameBits(mv.tightLoose, 0.3f));
    CHECK(peSameBits(mv.cleanViolent, 0.9f));
    CHECK(e->noteMemoryMode() == NoteMemory::Mode::Legato);
    CHECK(e->noteRetrigger() == NoteMemory::Retrigger::Continue);
    CHECK(peSameBits(e->pitchBendRange(), 12.0f));
    CHECK(e->voiceMode() == ProfundumEngine::VoiceMode::Poly);
    CHECK(e->monoPriority() == MonoMode::HighNote);
    CHECK(peSameBits(e->portamentoTimeMs(), 80.0f));
    CHECK(peSameBits(e->tempoBpm(), 140.0f));
    CHECK(peSameBits(e->morphPosition(), 0.25f));
    CHECK(e->seed() == 0x2222u);

    // The re-prepared engine renders at the new rate.
    PeStereo out96;
    e->noteOn(kPeNoteE1, kPeVelocity);
    peRender(*e, out96, 9600);
    CHECK(peAllFinite(out96));
    CHECK(peRmsDb(out96, 960, out96.l.size()) >= -40.0);
}

TEST_CASE("VoiceEngine_PostSumPlacementRuns", "[systems][profundum][voice-engine][fr-014][c-14]") {
    enableFTZDAZ();
    const ProfundumEngineConfig postSum{.placement = ProfundumNonlinearPlacement::PostSum};

    SECTION("POLY PostSum, four notes") {
        auto e = peMakeEngine(postSum);
        e->setVoiceMode(ProfundumEngine::VoiceMode::Poly);
        for (const std::uint8_t note : {kPeNoteE1, kPeNoteA1, kPeNoteE2, kPeNoteC3})
            e->noteOn(note, kPeVelocity);
        PeStereo out;
        const std::size_t n = peSamples(1.0);
        peRender(*e, out, n);
        const double rms = peRmsDb(out, peSamples(0.1), n);
        std::cout << std::setprecision(6) << "VoiceEngine_PostSumPlacementRuns: POLY4 PostSum RMS " << rms
                  << " dBFS\n";
        CHECK(peAllFinite(out));
        CHECK(rms >= -40.0);
    }

    SECTION("MONO is always PerVoice") {
        auto render = [](const ProfundumEngineConfig& cfg) {
            auto e = peMakeEngine(cfg);
            e->noteOn(kPeNoteE1, kPeVelocity);
            PeStereo out;
            peRender(*e, out, peSamples(0.5));
            return out;
        };
        const PeStereo perVoice = render(ProfundumEngineConfig{});
        const PeStereo post = render(postSum);
        const std::size_t diff = peMismatches(perVoice, post);
        std::cout << "VoiceEngine_PostSumPlacementRuns: MONO PostSum vs PerVoice mismatches " << diff << "\n";
        CHECK(diff == 0u);
        CHECK(peRmsDb(perVoice, peSamples(0.1), perVoice.l.size()) >= -40.0);
    }
}

// ------------------------------------------------------------------------------
// T011 - engine criteria through the T009 driver / observer (tasks.md T011; deviations 3, 6)
// ------------------------------------------------------------------------------

namespace {

namespace PT = Krate::DSP::ProfundumTest;
using Krate::DSP::MassLaw;
using Krate::DSP::ProfundumCall;
using Krate::DSP::ProfundumCallRecord;
using PeEvent = Krate::DSP::ProfundumTest::VoiceEngineEvent;
using PeKind = Krate::DSP::ProfundumTest::VoiceEngineEventKind;
using PeScript = Krate::DSP::ProfundumTest::VoiceEngineScript;
using PeTaps = Krate::DSP::ProfundumTest::VoiceEngineTaps;
using PeMotionSource = MorphState::MotionConfig::Source;
using PeMotionTarget = MorphState::MotionConfig::Target;

constexpr std::size_t kPeGrid = ProfundumEngine::kControlInterval;
constexpr std::uint8_t kPeEngineVoice = ProfundumEngine::kEngineRecordVoice;
constexpr std::uint8_t kPeNoteG1 = 31;

[[nodiscard]] std::uint64_t peAt(double seconds, double fs = kPeFs) noexcept {
    return static_cast<std::uint64_t>(seconds * fs + 0.5);
}

template <typename E>
[[nodiscard]] int peIdx(E e) noexcept {
    return static_cast<int>(e);
}

[[nodiscard]] PeEvent peOn(std::uint64_t s, std::uint8_t note) {
    return PeEvent{.sample = s, .kind = PeKind::NoteOn, .note = note, .velocity = kPeVelocity};
}

[[nodiscard]] PeEvent peOff(std::uint64_t s, std::uint8_t note) {
    return PeEvent{.sample = s, .kind = PeKind::NoteOff, .note = note};
}

[[nodiscard]] PeEvent peSet(std::uint64_t s, PeKind kind, float value, int index = 0) {
    return PeEvent{.sample = s, .kind = kind, .value = value, .index = index};
}

[[nodiscard]] bool peNonZero(float x) noexcept {   // the engine's own "+-0 is skipped" test (C-7)
    return (std::bit_cast<std::uint32_t>(x) & 0x7FFFFFFFu) != 0u;
}

void peSetMotion(MorphState& s, PeMotionSource src, PeMotionTarget tgt, float amount, float depth) {
    s.motion.source = src;
    s.motion.target = tgt;
    s.motion.amount = amount;
    s.motion.depth = depth;
}

/// The SC-011 schedule (tasks.md T011, the Vorago kDeterminismSchedule shape): note-ons E1 @ 0, C3 @ 0.25 s,
/// E2 @ 0.5 s, note-offs @ 1.0 / 1.25 / 1.5 s, 3 s, a morph ramp 0 -> 1 (500 ms) issued at 0.2 s and CYCLE -> Depth
/// on B. A = A_s (C-8). Observer detached and no per-voice taps by default (callers opt in).
[[nodiscard]] PeScript peSc011Script(ProfundumEngine::VoiceMode mode) {
    PeScript s;
    s.fs = kPeFs;
    s.hostBlock = kPeHostBlock;
    s.totalSamples = peAt(3.0);
    s.a = peStateAs();
    s.b = Krate::DSP::BassMorphTest::showcasePair().second;
    peSetMotion(s.b, PeMotionSource::Cycle, PeMotionTarget::Depth, 1.0f, 1.0f);
    REQUIRE(Krate::DSP::isValidMorphState(s.b));
    s.position = 0.0f;
    s.mode = mode;
    s.polyphony = ProfundumEngine::kDefaultPolyphony;
    s.attachObserver = false;
    s.tappedVoices = 0;
    s.events = {peOn(0, kPeNoteE1),
                peSet(peAt(0.2), PeKind::MorphRampMs, 500.0f),
                peSet(peAt(0.2), PeKind::MorphPosition, 1.0f),
                peOn(peAt(0.25), kPeNoteC3),
                peOn(peAt(0.5), kPeNoteE2),
                peOff(peAt(1.0), kPeNoteE1),
                peOff(peAt(1.25), kPeNoteC3),
                peOff(peAt(1.5), kPeNoteE2)};
    return s;
}

[[nodiscard]] std::size_t peVecMismatches(const std::vector<float>& al, const std::vector<float>& ar,
                                          const std::vector<float>& bl, const std::vector<float>& br) noexcept {
    if (al.size() != bl.size() || ar.size() != br.size())
        return std::max({al.size(), bl.size(), ar.size(), br.size()});
    std::size_t n = 0;
    for (std::size_t i = 0; i < al.size(); ++i) {
        if (!peSameBits(al[i], bl[i]) || !peSameBits(ar[i], br[i]))
            ++n;
    }
    return n;
}

[[nodiscard]] std::size_t peOutMismatches(const PeTaps& a, const PeTaps& b) noexcept {
    return peVecMismatches(a.outL, a.outR, b.outL, b.outR);
}

[[nodiscard]] bool peVecFinite(const std::vector<float>& l, const std::vector<float>& r) noexcept {
    for (std::size_t i = 0; i < l.size(); ++i) {
        if (!peFinite(l[i]) || !peFinite(r[i]))
            return false;
    }
    return true;
}

/// RMS over both channels of [from, to), dBFS (-300 for digital silence).
[[nodiscard]] double peVecRmsDb(const std::vector<float>& l, const std::vector<float>& r, std::size_t from,
                                std::size_t to) noexcept {
    to = std::min(to, l.size());
    if (from >= to)
        return -300.0;
    double acc = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        acc += static_cast<double>(l[i]) * static_cast<double>(l[i]);
        acc += static_cast<double>(r[i]) * static_cast<double>(r[i]);
    }
    const double rms = std::sqrt(acc / (2.0 * static_cast<double>(to - from)));
    return rms > 0.0 ? 20.0 * std::log10(rms) : -300.0;
}

[[nodiscard]] float peVecPeak(const std::vector<float>& l, const std::vector<float>& r, std::size_t from,
                              std::size_t to) noexcept {
    to = std::min(to, l.size());
    float p = 0.0f;
    for (std::size_t i = from; i < to; ++i)
        p = std::max({p, std::fabs(l[i]), std::fabs(r[i])});
    return p;
}

/// SC-011: 20 log10(rms(a - b) / rms(a)) over the output tap, both channels.
[[nodiscard]] double peResidualDb(const PeTaps& a, const PeTaps& b) {
    REQUIRE(a.outL.size() == b.outL.size());
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < a.outL.size(); ++i) {
        const double dl = static_cast<double>(a.outL[i]) - static_cast<double>(b.outL[i]);
        const double dr = static_cast<double>(a.outR[i]) - static_cast<double>(b.outR[i]);
        num += dl * dl + dr * dr;
        den += static_cast<double>(a.outL[i]) * static_cast<double>(a.outL[i]);
        den += static_cast<double>(a.outR[i]) * static_cast<double>(a.outR[i]);
    }
    REQUIRE(den > 0.0);
    return num > 0.0 ? 10.0 * std::log10(num / den) : -300.0;
}

/// Every grid record k sits at sample k * 32 (the observer appends one per gridStep).
void peRequireGridAligned(const PeTaps& t) {
    REQUIRE(!t.grid.empty());
    for (std::size_t k = 0; k < t.grid.size(); ++k)
        REQUIRE(t.grid[k].sample == static_cast<std::uint64_t>(k * kPeGrid));
}

/// Per interval k: the value of the first `c` record of `voice` stamped at grid sample k * 32 (seen[k] = 0: none).
struct PeCallSeries {
    std::vector<float> value;
    std::vector<std::uint8_t> seen;
};

[[nodiscard]] PeCallSeries peCallSeries(const PeTaps& t, ProfundumCall c, std::uint8_t voice) {
    PeCallSeries r;
    const std::size_t n = t.grid.size();
    r.value.assign(n, 0.0f);
    r.seen.assign(n, 0);
    for (const ProfundumCallRecord& rec : t.calls) {
        if (rec.call != c || rec.voice != voice || rec.sample % kPeGrid != 0)
            continue;
        const auto k = static_cast<std::size_t>(rec.sample / kPeGrid);
        if (k < n && r.seen[k] == 0) {
            r.value[k] = rec.value;
            r.seen[k] = 1;
        }
    }
    return r;
}

/// The engine's law base (profundum_engine.h lawBaseOf, plus MassLaw::setBase's per-row clamp, mass_law.h:170-175):
/// depth / body from the recipe after macros and Motion, foldAmount and the Anchor level from the state, the
/// Mass-driven bases at their Targets{} defaults.
[[nodiscard]] MassLaw::Targets peLawBase(const MorphState& s) noexcept {
    const auto clampRow = [](MassLaw::Target t, float v) noexcept {
        const MassLaw::Row& row = MassLaw::kRows[static_cast<std::size_t>(t)];
        return std::clamp(v, row.lo, row.hi);
    };
    MassLaw::Targets b{};
    b.depth = clampRow(MassLaw::Target::Depth, s.recipe.depth);
    b.body = clampRow(MassLaw::Target::Body, s.recipe.body);
    b.foldAmount = clampRow(MassLaw::Target::FoldAmount, s.foldAmount);
    b.anchorLevel = clampRow(MassLaw::Target::AnchorLevel, s.anchor.level);
    return b;
}

/// The AnchorLevel row, located by row.target (tasks.md T011 SC-027(d)).
[[nodiscard]] MassLaw::Row peAnchorLevelRow() {
    const MassLaw::Row* found = nullptr;
    for (const MassLaw::Row& row : MassLaw::kRows) {
        if (row.target == MassLaw::Target::AnchorLevel)
            found = &row;
    }
    REQUIRE(found != nullptr);
    return *found;
}

[[nodiscard]] bool peSameTargets(const MassLaw::Targets& a, const MassLaw::Targets& b) noexcept {
    return peSameBits(a.depth, b.depth) && peSameBits(a.body, b.body)
           && peSameBits(a.transientDurationMs, b.transientDurationMs) && peSameBits(a.foldAmount, b.foldAmount)
           && peSameBits(a.compressionAmount, b.compressionAmount) && peSameBits(a.anchorLevel, b.anchorLevel)
           && peSameBits(a.outputTrimDb, b.outputTrimDb);
}

/// An engine set up exactly like voiceEngineRender's, for the arms that need reset() / prepare() inside the run.
[[nodiscard]] std::unique_ptr<ProfundumEngine> peScriptEngine(const PeScript& s) {
    auto e = std::make_unique<ProfundumEngine>();
    e->setSeed(s.seed);
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, s.a));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, s.b));
    e->setMorphPosition(s.position);
    e->setMacros(s.macros);
    e->setVoiceMode(s.mode);
    e->setPolyphony(s.polyphony);
    e->prepare(s.fs, s.config);
    return e;
}

/// The settings the SC-011 schedule changes, back to the script's initial values (reset() keeps pending values).
void peRestoreInitial(ProfundumEngine& e, const PeScript& s) {
    REQUIRE(e.setEndpoint(BassMorphEngine::Endpoint::A, s.a));
    REQUIRE(e.setEndpoint(BassMorphEngine::Endpoint::B, s.b));
    e.setMorphPosition(s.position);
    e.setMorphRampMs(BassMorphEngine::kDefaultPositionRampMs);
    e.setAnchorMorphMode(BassMorphEngine::kDefaultAnchorMorphMode);
    e.setMacros(s.macros);
    e.setVoiceMode(s.mode);
    e.setPolyphony(s.polyphony);
    e.setPitchBend(0.0f);
    e.setTempoBpm(ProfundumEngine::kDefaultTempoBpm);
}

[[nodiscard]] std::vector<PeEvent> peSortedEvents(const PeScript& s) {
    std::vector<PeEvent> ev = s.events;
    std::stable_sort(ev.begin(), ev.end(), [](const PeEvent& x, const PeEvent& y) { return x.sample < y.sample; });
    return ev;
}

/// The driver's loop (voiceEngineRender) over [from, to): host blocks of s.hostBlock counted from `from`, each split
/// at every event's sample, the event issued before that sample (C-9). No allocation and no assertion: it runs
/// inside AllocationScope.
void peRunSpan(ProfundumEngine& e, const PeScript& s, const std::vector<PeEvent>& events, std::size_t& ei,
               std::size_t from, std::size_t to, float* outL, float* outR) noexcept {
    std::size_t pos = from;
    const std::size_t block = std::max<std::size_t>(s.hostBlock, 1);
    while (pos < to) {
        const std::size_t blockEnd = std::min(pos + block, to);
        while (pos < blockEnd) {
            while (ei < events.size() && events[ei].sample <= pos)
                PT::voiceEngineIssue(e, events[ei++], s);
            std::size_t next = blockEnd;
            if (ei < events.size() && events[ei].sample < next)
                next = static_cast<std::size_t>(events[ei].sample);
            e.processBlock(outL + pos, outR + pos, next - pos);
            pos = next;
        }
    }
}

/// Upward zero crossings of x (linear interpolation), in index units, for indices >= from.
[[nodiscard]] std::vector<double> peUpCrossings(const std::vector<double>& x, std::size_t from) {
    std::vector<double> c;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < x.size(); ++i) {
        if (x[i - 1] < 0.0 && x[i] >= 0.0) {
            const double f = -x[i - 1] / (x[i] - x[i - 1]);
            c.push_back(static_cast<double>(i - 1) + f);
        }
    }
    return c;
}

/// Mean spacing of consecutive crossings (requires two or more).
[[nodiscard]] double peMeanPeriod(const std::vector<double>& c) {
    REQUIRE(c.size() >= 2);
    return (c.back() - c.front()) / static_cast<double>(c.size() - 1);
}

/// Samples [first, last] that voice v rendered, from its chunk records (a mask over [0, total)).
[[nodiscard]] std::vector<std::uint8_t> peRenderedMask(const PeTaps& t, std::size_t v) {
    std::vector<std::uint8_t> m(t.total, 0);
    for (const PT::VoiceEngineChunkRecord& c : t.voices[v].chunks) {
        const auto s0 = static_cast<std::size_t>(c.sample);
        for (std::size_t i = s0; i < std::min(s0 + c.n, t.total); ++i)
            m[i] = 1;
    }
    return m;
}

[[nodiscard]] double peCents(double f, double ref) {
    REQUIRE(f > 0.0);
    REQUIRE(ref > 0.0);
    return 1200.0 * std::log2(f / ref);
}

}  // namespace

TEST_CASE("VoiceEngine_MacroNeutral", "[systems][profundum][voice-engine][sc-008][fr-051][fr-054]") {
    enableFTZDAZ();
    const ProfundumMacroValues mv{.weight = 0.2f, .darkBright = 0.8f, .tightLoose = 0.3f, .cleanViolent = 0.9f};
    for (const auto mode : {ProfundumEngine::VoiceMode::Mono, ProfundumEngine::VoiceMode::Poly}) {
        INFO("mode " << peIdx(mode));
        // (a) all four at 0.5 vs the matrix bypassed (FR-086 macroBypass).
        const PeScript neutral = peSc011Script(mode);
        PeScript bypass = neutral;
        bypass.config.seams.macroBypass = true;
        const PeTaps tn = PT::voiceEngineRender(neutral);
        const PeTaps tb = PT::voiceEngineRender(bypass);
        const std::size_t diffA = peOutMismatches(tn, tb);
        const double rmsA = peVecRmsDb(tn.outL, tn.outR, peSamples(0.1), peSamples(1.5));
        std::cout << std::setprecision(6) << "VoiceEngine_MacroNeutral: mode " << peIdx(mode)
                  << " (a) neutral vs bypass mismatches " << diffA << ", RMS " << rmsA << " dBFS\n";
        CHECK(diffA == 0u);
        CHECK(rmsA >= -40.0);

        // (b) unchanged non-neutral macros handed by an event at every interval vs once (before prepare = sample 0).
        // Host block 32 in both renders, so the event-split partition is the same (SC-012 is gated separately).
        PeScript once = neutral;
        once.hostBlock = kPeGrid;
        once.macros = mv;
        PeScript every = once;
        const float values[] = {mv.weight, mv.darkBright, mv.tightLoose, mv.cleanViolent};
        for (std::uint64_t g = 0; g < every.totalSamples; g += kPeGrid) {
            for (int m = 0; m < 4; ++m)
                every.events.push_back(peSet(g, PeKind::Macro, values[m], m));
        }
        const PeTaps to = PT::voiceEngineRender(once);
        const PeTaps te = PT::voiceEngineRender(every);
        const std::size_t diffB = peOutMismatches(to, te);
        // Positive control: the non-neutral macros move the sound.
        const std::size_t diffCtl = peOutMismatches(to, tn);
        std::cout << "VoiceEngine_MacroNeutral: mode " << peIdx(mode) << " (b) every-interval vs once mismatches "
                  << diffB << "; control (non-neutral vs neutral) mismatches " << diffCtl << "\n";
        CHECK(diffB == 0u);
        CHECK(diffCtl > 0u);
    }
}

TEST_CASE("VoiceEngine_DeterminismHarness", "[systems][profundum][voice-engine][sc-011][fr-062]") {
    enableFTZDAZ();
    for (const auto mode : {ProfundumEngine::VoiceMode::Mono, ProfundumEngine::VoiceMode::Poly}) {
        INFO("mode " << peIdx(mode));
        const PeScript s = peSc011Script(mode);

        // Two instances.
        const PeTaps a = PT::voiceEngineRender(s);
        const PeTaps b = PT::voiceEngineRender(s);
        const std::size_t diffInst = peOutMismatches(a, b);
        CHECK(diffInst == 0u);
        CHECK(peVecRmsDb(a.outL, a.outR, peSamples(0.1), peSamples(1.5)) >= -40.0);

        // Two runs of one instance, reset() between them (the settings the schedule changed restored first).
        const auto total = static_cast<std::size_t>(s.totalSamples);
        const std::vector<PeEvent> ev = peSortedEvents(s);
        std::vector<float> l1(total, 0.0f), r1(total, 0.0f), l2(total, 0.0f), r2(total, 0.0f);
        auto e = peScriptEngine(s);
        std::size_t ei = 0;
        peRunSpan(*e, s, ev, ei, 0, total, l1.data(), r1.data());
        peRestoreInitial(*e, s);
        (*e).reset();
        ei = 0;
        peRunSpan(*e, s, ev, ei, 0, total, l2.data(), r2.data());
        const std::size_t diffRuns = peVecMismatches(l1, r1, l2, r2);
        const std::size_t diffDriver = peVecMismatches(l1, r1, a.outL, a.outR);
        std::cout << "VoiceEngine_DeterminismHarness: mode " << peIdx(mode) << " instances " << diffInst
                  << ", runs after reset " << diffRuns << ", direct vs driver " << diffDriver << " mismatches\n";
        CHECK(diffRuns == 0u);
        CHECK(diffDriver == 0u);

        // Seeds 0x1111 vs 0x2222: DRIFT on (Motion Drift -> Depth, amount 1, depth 1, both endpoints).
        PeScript drift = s;
        for (MorphState* st : {&drift.a, &drift.b})
            peSetMotion(*st, PeMotionSource::Drift, PeMotionTarget::Depth, 1.0f, 1.0f);
        REQUIRE(Krate::DSP::isValidMorphState(drift.a));
        REQUIRE(Krate::DSP::isValidMorphState(drift.b));
        PeScript driftA = drift;
        driftA.seed = 0x1111u;
        PeScript driftB = drift;
        driftB.seed = 0x2222u;
        const double residual = peResidualDb(PT::voiceEngineRender(driftA), PT::voiceEngineRender(driftB));

        // DRIFT off: Motion Off and the field's own Drift off (field Motion source Cycle) on both endpoints.
        PeScript off = s;
        for (MorphState* st : {&off.a, &off.b}) {
            peSetMotion(*st, PeMotionSource::Off, PeMotionTarget::Mass, 0.0f, 1.0f);
            st->field.motionSource = Krate::DSP::BassStereoField::MotionSource::Cycle;
        }
        REQUIRE(Krate::DSP::isValidMorphState(off.a));
        REQUIRE(Krate::DSP::isValidMorphState(off.b));
        PeScript offA = off;
        offA.seed = 0x1111u;
        PeScript offB = off;
        offB.seed = 0x2222u;
        const std::size_t diffOff = peOutMismatches(PT::voiceEngineRender(offA), PT::voiceEngineRender(offB));
        std::cout << std::setprecision(6) << "VoiceEngine_DeterminismHarness: mode " << peIdx(mode)
                  << " seed residual DRIFT on " << residual << " dB (bar > -60); DRIFT off mismatches " << diffOff
                  << "\n";
        CHECK(residual > -60.0);
        CHECK(diffOff == 0u);
    }
}

TEST_CASE("VoiceEngine_Partition", "[systems][profundum][voice-engine][sc-012][fr-063]") {
    enableFTZDAZ();
    for (const auto mode : {ProfundumEngine::VoiceMode::Mono, ProfundumEngine::VoiceMode::Poly}) {
        INFO("mode " << peIdx(mode));
        PeScript s = peSc011Script(mode);
        // A pitch-bend ramp 0 -> +1 -> 0 over 0.6 ... 1.2 s (every 480 samples) and a tempo change at 0.7 s.
        for (int i = 0; i <= 60; ++i) {
            const float b = i <= 30 ? static_cast<float>(i) / 30.0f : static_cast<float>(60 - i) / 30.0f;
            s.events.push_back(peSet(peAt(0.6) + static_cast<std::uint64_t>(i) * 480u, PeKind::PitchBend, b));
        }
        s.events.push_back(peSet(peAt(0.7), PeKind::Tempo, 90.0f));
        PeTaps ref;
        bool first = true;
        for (const std::size_t block : {std::size_t{1}, std::size_t{7}, std::size_t{32}, std::size_t{64},
                                        std::size_t{511}, std::size_t{4096}}) {
            PeScript sb = s;
            sb.hostBlock = block;
            PeTaps t = PT::voiceEngineRender(sb);
            if (first) {
                ref = std::move(t);
                first = false;
                CHECK(peVecFinite(ref.outL, ref.outR));
                CHECK(peVecRmsDb(ref.outL, ref.outR, peSamples(0.1), peSamples(1.5)) >= -40.0);
                continue;
            }
            const std::size_t diff = peOutMismatches(ref, t);
            std::cout << "VoiceEngine_Partition: mode " << peIdx(mode) << " block " << block
                      << " vs block 1 mismatches " << diff << "\n";
            CHECK(diff == 0u);
        }
    }
}

TEST_CASE("VoiceEngine_RealTimeSafety", "[systems][profundum][voice-engine][sc-022][fr-060]") {
    enableFTZDAZ();
    PeScript s;
    s.fs = kPeFs;
    s.hostBlock = kPeHostBlock;
    s.totalSamples = peAt(10.0);
    s.a = peStateAs();
    s.b = Krate::DSP::BassMorphTest::showcasePair().second;
    s.mode = ProfundumEngine::VoiceMode::Mono;
    s.attachObserver = false;   // observer detached (SC-022)
    s.tappedVoices = 0;
    // Motion automation through endpoint changes: ENV -> Depth on A, CYCLE -> Mass on B.
    MorphState ea = s.a;
    peSetMotion(ea, PeMotionSource::Env, PeMotionTarget::Depth, 1.0f, 1.0f);
    MorphState eb = s.b;
    peSetMotion(eb, PeMotionSource::Cycle, PeMotionTarget::Mass, 1.0f, 1.0f);
    REQUIRE(Krate::DSP::isValidMorphState(ea));
    REQUIRE(Krate::DSP::isValidMorphState(eb));
    s.endpointStates = {ea, eb};

    const std::uint8_t notes[] = {kPeNoteE1, kPeNoteA1, kPeNoteE2, kPeNoteC3, kPeNoteG1};
    auto& ev = s.events;
    // MONO [0, 2 s): overlapping notes every 100 ms (each held 150 ms).
    for (int i = 0; i < 20; ++i) {
        const double t = 0.1 * static_cast<double>(i);
        ev.push_back(peOn(peAt(t), notes[i % 5]));
        ev.push_back(peOff(peAt(t + 0.15), notes[i % 5]));
    }
    ev.push_back(peSet(peAt(0.5), PeKind::MonoPriority, 0.0f, peIdx(MonoMode::LowNote)));
    ev.push_back(peSet(peAt(0.5), PeKind::PortamentoTimeMs, 40.0f));
    ev.push_back(peSet(peAt(1.0), PeKind::Legato, 0.0f, 1));
    ev.push_back(peSet(peAt(1.5), PeKind::AnchorMorphMode, 0.0f, peIdx(BassMorphEngine::AnchorMorphMode::Punch)));
    // Switch 1 (MONO -> POLY) at 2 s; five note-ons into four voices (the fifth steals), released from 3 s.
    ev.push_back(peSet(peAt(2.0), PeKind::VoiceMode, 0.0f, peIdx(ProfundumEngine::VoiceMode::Poly)));
    for (int i = 0; i < 5; ++i) {
        ev.push_back(peOn(peAt(2.05 + 0.05 * i), notes[i]));
        ev.push_back(peOff(peAt(3.0 + 0.05 * i), notes[i]));
    }
    // Switch 2 (POLY -> MONO) at 3.5 s with held notes; MONO notes until the reset at 5 s.
    ev.push_back(peOn(peAt(3.3), kPeNoteE1));
    ev.push_back(peSet(peAt(3.5), PeKind::VoiceMode, 0.0f, peIdx(ProfundumEngine::VoiceMode::Mono)));
    ev.push_back(peOff(peAt(3.6), kPeNoteE1));
    for (int i = 0; i < 8; ++i) {
        ev.push_back(peOn(peAt(3.7 + 0.15 * i), notes[(i + 2) % 5]));
        ev.push_back(peOff(peAt(3.9 + 0.15 * i), notes[(i + 2) % 5]));
    }
    // After the reset (5 s, pending mode MONO): POLY again from 6 s with steals.
    ev.push_back(peSet(peAt(6.0), PeKind::VoiceMode, 0.0f, peIdx(ProfundumEngine::VoiceMode::Poly)));
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 5; ++i) {
            const double t = 6.05 + 1.2 * round + 0.05 * i;
            ev.push_back(peOn(peAt(t), notes[i]));
            ev.push_back(peOff(peAt(t + 0.8), notes[i]));
        }
    }
    // Automation over the whole render.
    for (int k = 0; k < 200; ++k)   // macros every 50 ms
        ev.push_back(peSet(peAt(0.05 * k), PeKind::Macro, 0.5f + 0.4f * std::sin(0.3f * static_cast<float>(k)), k % 4));
    for (int k = 0; k < 40; ++k) {   // morph every 250 ms
        ev.push_back(peSet(peAt(0.25 * k), PeKind::MorphRampMs, 200.0f));
        ev.push_back(peSet(peAt(0.25 * k), PeKind::MorphPosition, (k % 2) != 0 ? 1.0f : 0.0f));
    }
    for (int k = 0; k < 20; ++k) {   // endpoints, Note Memory every 500 ms
        ev.push_back(peSet(peAt(0.5 * k + 0.01), PeKind::Endpoint, 0.0f, k % 2));
        ev.push_back(peSet(peAt(0.5 * k + 0.02), PeKind::NoteMemoryMode, 0.0f, k % 4));
        ev.push_back(peSet(peAt(0.5 * k + 0.02), PeKind::NoteRetrigger, 0.0f, k % 2));
    }
    for (int k = 0; k < 1000; ++k)   // bend every 10 ms
        ev.push_back(peSet(peAt(0.01 * k), PeKind::PitchBend, std::sin(0.05f * static_cast<float>(k))));
    ev.push_back(peSet(peAt(1.0), PeKind::Tempo, 90.0f));
    ev.push_back(peSet(peAt(4.0), PeKind::Tempo, 140.0f));
    ev.push_back(peSet(peAt(8.0), PeKind::Tempo, 100.0f));

    const std::vector<PeEvent> sorted = peSortedEvents(s);
    const auto total = static_cast<std::size_t>(s.totalSamples);
    const auto resetAt = static_cast<std::size_t>(peAt(5.0));
    std::vector<float> l(total, 0.0f);
    std::vector<float> r(total, 0.0f);
    auto e = peScriptEngine(s);   // prepare outside the scope

    std::size_t allocations = 0;
    std::uint64_t droppedBeforeReset = 0;
    std::uint64_t droppedAtEnd = 0;
    {
        const ::TestHelpers::AllocationScope scope;
        std::size_t ei = 0;
        peRunSpan(*e, s, sorted, ei, 0, resetAt, l.data(), r.data());
        droppedBeforeReset = e->droppedEvents();
        (*e).reset();
        peRunSpan(*e, s, sorted, ei, resetAt, total, l.data(), r.data());
        droppedAtEnd = e->droppedEvents();
        allocations = ::TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    std::cout << "VoiceEngine_RealTimeSafety: " << sorted.size() << " events over 10 s, allocations " << allocations
              << ", dropped " << droppedBeforeReset << " / " << droppedAtEnd << "\n";
    CHECK(allocations == 0u);
    CHECK(droppedBeforeReset == 0u);
    CHECK(droppedAtEnd == 0u);
    CHECK(peVecFinite(l, r));
    CHECK(peVecRmsDb(l, r, 0, total) >= -40.0);
}

TEST_CASE("VoiceEngine_SampleRates", "[systems][profundum][voice-engine][sc-024][fr-064]") {
    enableFTZDAZ();
    const double rates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    for (const double fs : rates) {
        INFO("fs " << fs);
        PeScript s;
        s.fs = fs;
        s.hostBlock = kPeHostBlock;
        s.totalSamples = peAt(1.1, fs);
        s.a = peStateAs();
        s.b = Krate::DSP::BassMorphTest::showcasePair().second;
        s.mode = ProfundumEngine::VoiceMode::Mono;
        s.attachObserver = true;
        s.tappedVoices = 0;
        s.events = {peOn(0, kPeNoteE1)};
        const PeTaps t = PT::voiceEngineRender(s);
        const auto total = static_cast<std::size_t>(s.totalSamples);
        const double rms = peVecRmsDb(t.outL, t.outR, static_cast<std::size_t>(peAt(0.1, fs)), total);
        std::vector<std::uint64_t> advances;
        for (const ProfundumCallRecord& c : t.calls) {
            if (c.call == ProfundumCall::MorphAdvance && c.voice == kPeEngineVoice)
                advances.push_back(c.sample);
        }
        bool onGrid = true;
        for (std::size_t i = 0; i < advances.size(); ++i)
            onGrid = onGrid && advances[i] == static_cast<std::uint64_t>(i * kPeGrid);
        std::cout << std::setprecision(6) << "VoiceEngine_SampleRates: fs " << fs << " RMS " << rms
                  << " dBFS, MorphAdvance records " << advances.size() << "\n";
        CHECK(peVecFinite(t.outL, t.outR));
        CHECK(rms >= -40.0);
        CHECK(advances.size() == (total + kPeGrid - 1) / kPeGrid);
        CHECK(onGrid);
    }

    // C-15: a re-prepare at a new rate (maxBlockSize is always 32) allocates nothing.
    auto e = peMakeEngine();
    for (const double fs : {96000.0, 22050.0, 192000.0, 44100.0, 48000.0}) {
        INFO("re-prepare at " << fs);
        std::size_t allocations = 0;
        {
            const ::TestHelpers::AllocationScope scope;
            e->prepare(fs);
            allocations = ::TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        std::cout << "VoiceEngine_SampleRates: re-prepare at " << fs << " allocations " << allocations << "\n";
        CHECK(allocations == 0u);
        e->noteOn(kPeNoteE1, kPeVelocity);
        PeStereo out;
        peRender(*e, out, static_cast<std::size_t>(peAt(0.3, fs)));
        e->noteOff(kPeNoteE1);
        CHECK(peAllFinite(out));
        CHECK(peRmsDb(out, static_cast<std::size_t>(peAt(0.1, fs)), out.l.size()) >= -40.0);
    }
}

TEST_CASE("VoiceEngine_MotionDepthZero", "[systems][profundum][voice-engine][sc-026][fr-042]") {
    enableFTZDAZ();
    const PeScript base = peSc011Script(ProfundumEngine::VoiceMode::Poly);
    const auto withMotion = [&base](PeMotionSource src, PeMotionTarget tgt, float depth) {
        PeScript s = base;
        for (MorphState* st : {&s.a, &s.b})
            peSetMotion(*st, src, tgt, 1.0f, depth);
        REQUIRE(Krate::DSP::isValidMorphState(s.a));
        REQUIRE(Krate::DSP::isValidMorphState(s.b));
        return s;
    };
    const PeTaps ref = PT::voiceEngineRender(withMotion(PeMotionSource::Off, PeMotionTarget::Mass, 0.0f));
    CHECK(peVecRmsDb(ref.outL, ref.outR, peSamples(0.1), peSamples(1.5)) >= -40.0);
    std::size_t renders = 0;
    for (const PeMotionSource src :
         {PeMotionSource::Off, PeMotionSource::Env, PeMotionSource::Cycle, PeMotionSource::Drift}) {
        for (const PeMotionTarget tgt : {PeMotionTarget::Mass, PeMotionTarget::Depth, PeMotionTarget::Body,
                                         PeMotionTarget::Edge, PeMotionTarget::Shift}) {
            INFO("source " << peIdx(src) << " target " << peIdx(tgt));
            const std::size_t diff = peOutMismatches(ref, PT::voiceEngineRender(withMotion(src, tgt, 0.0f)));
            ++renders;
            if (diff != 0u)
                std::cout << "VoiceEngine_MotionDepthZero: source " << peIdx(src) << " target " << peIdx(tgt)
                          << " mismatches " << diff << "\n";
            CHECK(diff == 0u);
        }
    }
    // Positive control: the same source at depth 1 moves the output.
    const std::size_t ctl = peOutMismatches(
        ref, PT::voiceEngineRender(withMotion(PeMotionSource::Cycle, PeMotionTarget::Depth, 1.0f)));
    std::cout << "VoiceEngine_MotionDepthZero: " << renders << " depth-0 renders vs Motion Off; control CYCLE -> "
              << "Depth at depth 1 mismatches " << ctl << "\n";
    CHECK(renders == 20u);
    CHECK(ctl > 0u);
}

namespace {

/// SC-027: MONO held E1; A = A_s with Mass 0, B = showcase B with Mass 100, the morph ramping 0 -> 1 over 1 s from
/// sample 0 (Mass automated 0 -> 100 over 1 s), Anchor morph mode `am`; optionally CYCLE -> Mass (0.5 Hz,
/// amount 1, depth 1) on both endpoints.
[[nodiscard]] PeScript peMassScript(double seconds, BassMorphEngine::AnchorMorphMode am, bool cycleToMass) {
    PeScript s;
    s.fs = kPeFs;
    s.hostBlock = kPeHostBlock;
    s.totalSamples = peAt(seconds);
    s.a = peStateAs();
    s.a.mass = 0.0f;
    s.b = Krate::DSP::BassMorphTest::showcasePair().second;
    s.b.mass = 100.0f;
    if (cycleToMass) {
        for (MorphState* st : {&s.a, &s.b}) {
            peSetMotion(*st, PeMotionSource::Cycle, PeMotionTarget::Mass, 1.0f, 1.0f);
            st->motion.cycleRateHz = 0.5f;
        }
    }
    REQUIRE(Krate::DSP::isValidMorphState(s.a));
    REQUIRE(Krate::DSP::isValidMorphState(s.b));
    s.mode = ProfundumEngine::VoiceMode::Mono;
    s.attachObserver = true;
    s.tappedVoices = 0;
    s.events = {peOn(0, kPeNoteE1),
                peSet(0, PeKind::AnchorMorphMode, 0.0f, peIdx(am)),
                peSet(0, PeKind::MorphRampMs, 1000.0f),
                peSet(0, PeKind::MorphPosition, 1.0f)};
    return s;
}

}  // namespace

TEST_CASE("VoiceEngine_MassSplit", "[systems][profundum][voice-engine][sc-027][fr-027]") {
    enableFTZDAZ();
    const MassLaw::Row anchorRow = peAnchorLevelRow();

    SECTION("(a) receivers equal MassLaw::evaluate(smoothedMass, base); compression per C-7") {
        const ProfundumMacroValues neutral{};
        ProfundumMacroValues loose{};
        loose.tightLoose = 0.2f;
        for (const ProfundumMacroValues& mv : {neutral, loose}) {
            INFO("tightLoose " << mv.tightLoose);
            PeScript s = peMassScript(1.5, BassMorphEngine::kDefaultAnchorMorphMode, false);
            s.macros = mv;
            const PeTaps t = PT::voiceEngineRender(s);
            peRequireGridAligned(t);
            Krate::DSP::ProfundumMacroMatrix mm;
            mm.setMacros(mv);
            const float cTL = mm.compressionContribution();
            if (mv.tightLoose != 0.5f)
                CHECK(peNonZero(cTL));   // the arm exercises the C-7 add
            const PeCallSeries core = peCallSeries(t, ProfundumCall::CoreControls, 0);
            const PeCallSeries fold = peCallSeries(t, ProfundumCall::FoldAmount, 0);
            const PeCallSeries dur = peCallSeries(t, ProfundumCall::DynDuration, kPeEngineVoice);
            const PeCallSeries comp = peCallSeries(t, ProfundumCall::DynCompression, kPeEngineVoice);
            const PeCallSeries trim = peCallSeries(t, ProfundumCall::DynTrim, kPeEngineVoice);
            std::size_t targetMism = 0;
            std::size_t voiceMism = 0;
            std::size_t dynMism = 0;
            std::size_t voiceChecked = 0;
            std::size_t dynUnseen = 0;
            float massMin = 1.0e9f;
            float massMax = -1.0e9f;
            for (std::size_t k = 0; k < t.grid.size(); ++k) {
                const PT::VoiceEngineGridRecord& g = t.grid[k];
                const MassLaw::Targets expected = MassLaw::evaluate(g.smoothedMass, peLawBase(g.afterMotion));
                massMin = std::min(massMin, g.smoothedMass);
                massMax = std::max(massMax, g.smoothedMass);
                if (!peSameTargets(expected, g.targets))
                    ++targetMism;
                if (core.seen[k] != 0 && fold.seen[k] != 0) {
                    ++voiceChecked;
                    if (!peSameBits(core.value[k], expected.depth) || !peSameBits(fold.value[k], expected.foldAmount))
                        ++voiceMism;
                }
                const float expComp = peNonZero(cTL)
                                          ? std::clamp(expected.compressionAmount + cTL, 0.0f, 1.0f)
                                          : expected.compressionAmount;
                if (dur.seen[k] == 0 || comp.seen[k] == 0 || trim.seen[k] == 0) {
                    ++dynUnseen;
                    continue;
                }
                if (!peSameBits(dur.value[k], expected.transientDurationMs) || !peSameBits(comp.value[k], expComp)
                    || !peSameBits(trim.value[k], expected.outputTrimDb) || !peSameBits(g.compressionHanded, expComp))
                    ++dynMism;
            }
            std::cout << std::setprecision(6) << "VoiceEngine_MassSplit (a): tightLoose " << mv.tightLoose
                      << " c_TL " << cTL << ": intervals " << t.grid.size() << ", target mismatches " << targetMism
                      << ", voice receivers checked " << voiceChecked << " mismatches " << voiceMism
                      << ", dynamics mismatches " << dynMism << " (unseen " << dynUnseen << "), Mass "
                      << massMin << " .. " << massMax << "\n";
            CHECK(targetMism == 0u);
            CHECK(voiceMism == 0u);
            CHECK(dynMism == 0u);
            CHECK(dynUnseen == 0u);
            CHECK(voiceChecked + 2 >= t.grid.size());
            CHECK(massMax - massMin >= 90.0f);
            CHECK(t.grid.back().resolved.position >= 0.99f);
        }
    }

    SECTION("(b), (c) Lock: the handed Anchor level stays at its p = 0 value") {
        for (const bool cycle : {false, true}) {
            INFO("CYCLE -> Mass " << cycle);
            const PeScript s = peMassScript(cycle ? 2.5 : 1.5, BassMorphEngine::AnchorMorphMode::Lock, cycle);
            const PeTaps t = PT::voiceEngineRender(s);
            peRequireGridAligned(t);
            const float expected =
                MassLaw::evaluateRow(anchorRow, s.a.mass, std::clamp(s.a.anchor.level, anchorRow.lo, anchorRow.hi));
            const PeCallSeries handed = peCallSeries(t, ProfundumCall::AnchorControls, 0);
            std::size_t gridMism = 0;
            std::size_t callMism = 0;
            std::size_t callChecked = 0;
            float motionMin = 1.0e9f;
            float motionMax = -1.0e9f;
            for (std::size_t k = 0; k < t.grid.size(); ++k) {
                const PT::VoiceEngineGridRecord& g = t.grid[k];
                if (!peSameBits(g.anchorLevel, expected))
                    ++gridMism;
                if (handed.seen[k] != 0) {
                    ++callChecked;
                    if (!peSameBits(handed.value[k], expected))
                        ++callMism;
                }
                motionMin = std::min(motionMin, g.motionOutput);
                motionMax = std::max(motionMax, g.motionOutput);
            }
            std::cout << std::setprecision(6) << "VoiceEngine_MassSplit (" << (cycle ? "c" : "b")
                      << "): Lock level " << expected << ", grid mismatches " << gridMism << ", handed mismatches "
                      << callMism << " of " << callChecked << ", Motion output " << motionMin << " .. "
                      << motionMax << "\n";
            CHECK(gridMism == 0u);
            CHECK(callMism == 0u);
            CHECK(callChecked + 2 >= t.grid.size());
            CHECK(t.grid.back().resolved.position >= 0.99f);   // the morph moved
            if (cycle)
                CHECK(motionMax - motionMin >= 1.5f);   // the Motion -> Mass offset moved
        }
    }

    SECTION("(d) Blend and Punch: level = evaluateRow(row, anchorMass + o.mass, anchor.level), moving") {
        for (const auto am : {BassMorphEngine::AnchorMorphMode::Blend, BassMorphEngine::AnchorMorphMode::Punch}) {
            INFO("Anchor morph mode " << peIdx(am));
            const PeScript s = peMassScript(2.6, am, true);
            const PeTaps t = PT::voiceEngineRender(s);
            peRequireGridAligned(t);
            std::size_t massMism = 0;
            std::size_t levelMism = 0;
            float lo = 1.0e9f;
            float hi = -1.0e9f;
            const std::uint64_t from = peAt(0.5);
            const std::uint64_t to = peAt(2.5);   // one 2 s CYCLE period
            for (const PT::VoiceEngineGridRecord& g : t.grid) {
                const float oMass = g.motionOutput * Krate::DSP::ProfundumMotion::kMassHalfSpan;
                const float mA = peNonZero(oMass) ? g.resolved.anchorMass + oMass : g.resolved.anchorMass;
                const float level = MassLaw::evaluateRow(
                    anchorRow, mA, std::clamp(g.afterMotion.anchor.level, anchorRow.lo, anchorRow.hi));
                if (!peSameBits(g.anchorMassUsed, mA))
                    ++massMism;
                if (!peSameBits(g.anchorLevel, level))
                    ++levelMism;
                if (g.sample >= from && g.sample < to) {
                    lo = std::min(lo, g.anchorLevel);
                    hi = std::max(hi, g.anchorLevel);
                }
            }
            std::cout << std::setprecision(6) << "VoiceEngine_MassSplit (d): mode " << peIdx(am)
                      << " m_a mismatches " << massMism << ", level mismatches " << levelMism
                      << ", level peak-to-peak over one period " << (hi - lo) << "\n";
            CHECK(massMism == 0u);
            CHECK(levelMism == 0u);
            CHECK(hi - lo > 0.0f);
        }
    }
}

TEST_CASE("VoiceEngine_MorphGlobal", "[systems][profundum][voice-engine][sc-028][fr-030]") {
    enableFTZDAZ();
    PeScript s;
    s.fs = kPeFs;
    s.hostBlock = kPeHostBlock;
    s.totalSamples = peAt(1.0) + 7;   // not a grid multiple: ceil(n / 32) is exercised
    s.a = peStateAs();
    s.b = Krate::DSP::BassMorphTest::showcasePair().second;
    s.mode = ProfundumEngine::VoiceMode::Poly;
    s.polyphony = 4;
    s.attachObserver = true;
    s.tappedVoices = 0;
    s.events = {peOn(0, kPeNoteE1), peOn(0, kPeNoteA1), peOn(0, kPeNoteE2), peOn(0, kPeNoteC3),
                peSet(peAt(0.1), PeKind::MorphRampMs, 500.0f), peSet(peAt(0.1), PeKind::MorphPosition, 1.0f)};
    const PeTaps t = PT::voiceEngineRender(s);
    peRequireGridAligned(t);
    const std::size_t n = static_cast<std::size_t>(s.totalSamples);
    const std::size_t intervals = (n + kPeGrid - 1) / kPeGrid;

    // advanceInterval exactly ceil(n / 32) times, on grid multiples (the MorphAdvance record per grid step).
    std::vector<std::uint64_t> advances;
    for (const ProfundumCallRecord& c : t.calls) {
        if (c.call == ProfundumCall::MorphAdvance && c.voice == kPeEngineVoice)
            advances.push_back(c.sample);
    }
    bool onGrid = advances.size() == intervals;
    for (std::size_t i = 0; onGrid && i < advances.size(); ++i)
        onGrid = advances[i] == static_cast<std::uint64_t>(i * kPeGrid);
    CHECK(advances.size() == intervals);
    CHECK(t.grid.size() == intervals);
    CHECK(onGrid);

    // Every voice's handed state comes from the one Resolved: the state-derived call values are equal across the
    // four voices and equal to the grid record's fields.
    const ProfundumCall global[] = {ProfundumCall::VoiceWide,     ProfundumCall::CoreControls,
                                    ProfundumCall::CoreExcitation, ProfundumCall::FoldAmount,
                                    ProfundumCall::Shape,          ProfundumCall::CharAlgorithm,
                                    ProfundumCall::CharAmount,     ProfundumCall::CharTone,
                                    ProfundumCall::AnchorControls, ProfundumCall::LockBypass,
                                    ProfundumCall::DropSetters,    ProfundumCall::AmpEnv,
                                    ProfundumCall::AmpCurve};
    std::size_t acrossMism = 0;
    std::size_t unseen = 0;
    for (const ProfundumCall c : global) {
        std::array<PeCallSeries, 4> per{};
        for (std::uint8_t v = 0; v < 4; ++v)
            per[v] = peCallSeries(t, c, v);
        for (std::size_t k = 0; k < t.grid.size(); ++k) {
            for (std::uint8_t v = 0; v < 4; ++v) {
                if (per[v].seen[k] == 0) {
                    ++unseen;
                    continue;
                }
                if (!peSameBits(per[v].value[k], per[0].value[k]))
                    ++acrossMism;
            }
        }
    }
    const PeCallSeries core = peCallSeries(t, ProfundumCall::CoreControls, 0);
    const PeCallSeries fold = peCallSeries(t, ProfundumCall::FoldAmount, 0);
    const PeCallSeries shape = peCallSeries(t, ProfundumCall::Shape, 0);
    const PeCallSeries anchor = peCallSeries(t, ProfundumCall::AnchorControls, 0);
    std::size_t gridMism = 0;
    for (std::size_t k = 0; k < t.grid.size(); ++k) {
        const PT::VoiceEngineGridRecord& g = t.grid[k];
        if (core.seen[k] == 0)
            continue;
        if (!peSameBits(core.value[k], g.targets.depth) || !peSameBits(fold.value[k], g.targets.foldAmount)
            || !peSameBits(shape.value[k], g.afterMotion.shape) || !peSameBits(anchor.value[k], g.anchorLevel))
            ++gridMism;
    }
    std::cout << "VoiceEngine_MorphGlobal: intervals " << intervals << ", MorphAdvance records " << advances.size()
              << ", across-voice mismatches " << acrossMism << " (unseen " << unseen << "), grid mismatches "
              << gridMism << ", final p " << t.grid.back().resolved.position << "\n";
    CHECK(acrossMism == 0u);
    CHECK(unseen == 0u);
    CHECK(gridMism == 0u);
    CHECK(t.grid.back().resolved.position >= 0.99f);
}

TEST_CASE("VoiceEngine_ModeSwitch", "[systems][profundum][voice-engine][sc-029][fr-023]") {
    enableFTZDAZ();
    const std::uint64_t s1 = peAt(0.5);   // MONO -> POLY, E1 held
    const std::uint64_t s2 = peAt(1.0);   // POLY -> MONO, A1 + C3 held
    REQUIRE(s1 % kPeGrid == 0);
    REQUIRE(s2 % kPeGrid == 0);
    const auto script = [](ProfundumEngine::VoiceMode mode, double seconds) {
        PeScript s;
        s.fs = kPeFs;
        s.hostBlock = kPeHostBlock;
        s.totalSamples = peAt(seconds);
        s.a = peStateAs();
        s.b = s.a;
        s.mode = mode;
        s.polyphony = 4;
        s.attachObserver = true;
        s.tappedVoices = 4;
        return s;
    };
    const PeEvent toPoly = peSet(s1, PeKind::VoiceMode, 0.0f, peIdx(ProfundumEngine::VoiceMode::Poly));
    const PeEvent toMono = peSet(s2, PeKind::VoiceMode, 0.0f, peIdx(ProfundumEngine::VoiceMode::Mono));
    const std::vector<PeEvent> lateOffs = {peOff(peAt(1.2), kPeNoteE1), peOff(peAt(1.2), kPeNoteA1),
                                           peOff(peAt(1.2), kPeNoteC3)};

    PeScript main = script(ProfundumEngine::VoiceMode::Mono, 2.0);
    main.events = {peOn(0, kPeNoteE1), toPoly, peOn(peAt(0.6), kPeNoteA1), peOn(peAt(0.6), kPeNoteC3), toMono};
    main.events.insert(main.events.end(), lateOffs.begin(), lateOffs.end());
    // Reference 1: no switch at s1; E1 released at s1 instead.
    PeScript ref1 = script(ProfundumEngine::VoiceMode::Mono, 2.0);
    ref1.events = {peOn(0, kPeNoteE1), peOff(s1, kPeNoteE1), peOn(peAt(0.6), kPeNoteA1), peOn(peAt(0.6), kPeNoteC3),
                   toMono};
    ref1.events.insert(ref1.events.end(), lateOffs.begin(), lateOffs.end());
    // Reference 2: no switch at s2; A1 and C3 released at s2 instead.
    PeScript ref2 = script(ProfundumEngine::VoiceMode::Mono, 2.0);
    ref2.events = {peOn(0, kPeNoteE1), toPoly, peOn(peAt(0.6), kPeNoteA1), peOn(peAt(0.6), kPeNoteC3),
                   peOff(s2, kPeNoteA1), peOff(s2, kPeNoteC3)};
    ref2.events.insert(ref2.events.end(), lateOffs.begin(), lateOffs.end());

    const PeTaps tm = PT::voiceEngineRender(main);
    const PeTaps t1 = PT::voiceEngineRender(ref1);
    const PeTaps t2 = PT::voiceEngineRender(ref2);

    // The first sample at or after `from` where voice v's g_amp tap reads 0 (its release has ended).
    const auto releaseEnd = [](const PeTaps& t, std::size_t v, std::size_t from) {
        for (std::size_t n = from; n < t.total; ++n) {
            if (t.voices[v].gAmp[n] == 0.0f)
                return n;
        }
        return t.total;
    };
    const auto n1 = static_cast<std::size_t>(s1);
    const auto n2 = static_cast<std::size_t>(s2);
    REQUIRE(tm.voices[0].gAmp[n1 - 1] > 0.0f);
    const std::size_t end1 = releaseEnd(tm, 0, n1);
    REQUIRE(end1 < static_cast<std::size_t>(peAt(0.6)));
    std::size_t end2 = n2;
    std::size_t heldAtS2 = 0;
    for (std::size_t v = 0; v < 4; ++v) {
        if (tm.voices[v].gAmp[n2 - 1] > 0.0f) {
            ++heldAtS2;
            end2 = std::max(end2, releaseEnd(tm, v, n2));
        }
    }
    REQUIRE(heldAtS2 == 2u);
    REQUIRE(end2 < static_cast<std::size_t>(peAt(1.2)));

    // Steady references (S9.9): separate steady renders of the notes involved, Sum tap, both channels.
    const auto steadyOf = [&script](ProfundumEngine::VoiceMode mode, std::initializer_list<std::uint8_t> notes) {
        PeScript s = script(mode, 0.6);
        s.tappedVoices = 0;
        for (const std::uint8_t note : notes)
            s.events.push_back(peOn(0, note));
        const PeTaps t = PT::voiceEngineRender(s);
        const auto from = static_cast<std::size_t>(peAt(0.3));
        const auto len = static_cast<std::size_t>(peAt(0.2));
        return std::array<PT::VoiceEngineSteady, 2>{
            PT::voiceEngineSteadyOf(std::span<const float>(t.sumL.data() + from, len)),
            PT::voiceEngineSteadyOf(std::span<const float>(t.sumR.data() + from, len))};
    };
    const auto steady1 = steadyOf(ProfundumEngine::VoiceMode::Mono, {kPeNoteE1});
    const auto steady2 = steadyOf(ProfundumEngine::VoiceMode::Poly, {kPeNoteA1, kPeNoteC3});
    const double sMax = PT::voiceEngineDeclickSlope(kPeFs);

    struct Gate {
        const char* name;
        PT::VoiceEngineWindow w;
        const PeTaps* ref;
        std::span<const PT::VoiceEngineSteady> steady;
        double f0;
    };
    using PeSteadySpan = std::span<const PT::VoiceEngineSteady>;
    const Gate gates[] = {
        {"MONO->POLY", PT::VoiceEngineWindow{.begin = n1, .end = end1}, &t1, PeSteadySpan(steady1),
         static_cast<double>(Krate::DSP::midiNoteToFrequency(kPeNoteE1))},
        {"POLY->MONO", PT::VoiceEngineWindow{.begin = n2, .end = end2}, &t2, PeSteadySpan(steady2),
         static_cast<double>(Krate::DSP::midiNoteToFrequency(kPeNoteA1))}};
    for (const Gate& g : gates) {
        INFO(g.name);
        const double ratioL = PT::voiceEngineDeltaRatio(tm.sumL, g.ref->sumL, g.w);
        const double ratioR = PT::voiceEngineDeltaRatio(tm.sumR, g.ref->sumR, g.w);
        const double bound = PT::voiceEngineDeltaBound(g.steady, sMax);
        const double maxDelta = std::max(PT::voiceEngineMaxDelta(tm.sumL, g.w), PT::voiceEngineMaxDelta(tm.sumR, g.w));
        const std::size_t len = g.w.end - g.w.begin;
        const auto clicks = Krate::DSP::ShapeCharacterTest::differentialClicks(
            tm.sumL.data() + g.w.begin, g.ref->sumL.data() + g.w.begin, len, kPeFs, g.f0);
        std::cout << std::setprecision(6) << "VoiceEngine_ModeSwitch: " << g.name << " W [" << g.w.begin << ", "
                  << g.w.end << ") (a) ratio L " << ratioL << " R " << ratioR << " (bar 1.059254); (b) max |dy| "
                  << maxDelta << " bound " << bound << " margin " << (bound / std::max(maxDelta, 1.0e-30))
                  << "; (c) differentialClicks " << clicks.counted << " (recorded)\n";
        CHECK(ratioL <= 1.059254);
        CHECK(ratioR <= 1.059254);
        CHECK(maxDelta <= bound);
    }

    // No voice sounds past its release and no note-off is lost: nothing renders after the late note-offs.
    std::size_t lateChunks = 0;
    for (std::size_t v = 0; v < ProfundumEngine::kMaxVoices && v < tm.tappedVoices; ++v) {
        for (const PT::VoiceEngineChunkRecord& c : tm.voices[v].chunks) {
            if (c.sample >= peAt(1.25))
                ++lateChunks;
        }
    }
    // Read on the voices' Sum tap: after 1.3 s the output tap still carries the global 5 Hz DC blocker's own decay
    // (kDcCutoffHz, tau 31.8 ms) of the last voice samples, not a voice; SC-029 is about voices.
    const float tailPeak = peVecPeak(tm.sumL, tm.sumR, static_cast<std::size_t>(peAt(1.3)), tm.total);
    std::cout << "VoiceEngine_ModeSwitch: chunks after 1.25 s " << lateChunks << ", Sum tail peak " << tailPeak << "\n";
    CHECK(lateChunks == 0u);
    CHECK(tailPeak <= 1.0e-6f);
    CHECK(peVecFinite(tm.outL, tm.outR));
}

TEST_CASE("VoiceEngine_StealOrder", "[systems][profundum][voice-engine][sc-031][fr-022]") {
    enableFTZDAZ();
    // Test-side model of the FR-022 policy: an idle slot (the oldest idle first) else the victim is the oldest
    // Releasing slot by note-on time, else the oldest Active. A 10 s release keeps every released slot Releasing for
    // the whole schedule, so no slot returns to idle and the model needs no release timing.
    struct Slot {
        int state = 0;   // 0 idle, 1 active, 2 releasing
        int note = -1;
        std::uint64_t onTime = 0;
    };
    struct Expect {
        std::uint64_t sample;
        std::uint8_t note;
        std::size_t slot;
        bool steal;
    };
    constexpr std::size_t kSlots = 4;
    constexpr std::uint64_t kSpacing = 256;   // a grid multiple: each event applies at its own sample
    std::array<Slot, kSlots> slots{};
    std::uint64_t clock = 0;
    std::mt19937 rng(0x5031u);
    std::vector<PeEvent> events;
    std::vector<Expect> expects;
    for (int k = 0; k < 1000; ++k) {
        const std::uint64_t at = static_cast<std::uint64_t>(k + 1) * kSpacing;
        std::vector<std::size_t> active;
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (slots[i].state == 1)
                active.push_back(i);
        }
        if (!active.empty() && rng() % 3u == 0u) {
            const std::size_t i = active[rng() % active.size()];
            events.push_back(peOff(at, static_cast<std::uint8_t>(slots[i].note)));
            slots[i].state = 2;
            continue;
        }
        std::uint8_t note = 0;
        for (;;) {
            note = static_cast<std::uint8_t>(24u + rng() % 48u);
            bool used = false;
            for (const Slot& sl : slots)
                used = used || (sl.state != 0 && std::cmp_equal(sl.note, note));
            if (!used)
                break;
        }
        ++clock;
        std::size_t slot = kSlots;
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (slots[i].state == 0 && (slot == kSlots || slots[i].onTime < slots[slot].onTime))
                slot = i;
        }
        const bool steal = slot == kSlots;
        for (const int wanted : {2, 1}) {
            if (slot != kSlots)
                break;
            for (std::size_t i = 0; i < kSlots; ++i) {
                if (slots[i].state == wanted && (slot == kSlots || slots[i].onTime < slots[slot].onTime))
                    slot = i;
            }
        }
        REQUIRE(slot < kSlots);
        slots[slot] = Slot{.state = 1, .note = static_cast<int>(note), .onTime = clock};
        events.push_back(peOn(at, note));
        expects.push_back(Expect{.sample = at, .note = note, .slot = slot, .steal = steal});
    }

    PeScript s;
    s.fs = kPeFs;
    s.hostBlock = kPeHostBlock;
    s.totalSamples = 1001u * kSpacing + kSpacing;
    s.a = peStateAs();
    s.a.ampEnv.releaseMs = 10000.0f;
    REQUIRE(Krate::DSP::isValidMorphState(s.a));
    s.b = s.a;
    s.mode = ProfundumEngine::VoiceMode::Poly;
    s.polyphony = kSlots;
    s.attachObserver = true;
    s.tappedVoices = kSlots;
    s.events = events;
    const PeTaps t = PT::voiceEngineRender(s);

    // Observed slot: the voice whose f_res tap at the event's grid sample is the new note's frequency.
    std::size_t steals = 0;
    std::size_t mism = 0;
    std::size_t ambiguous = 0;
    for (const Expect& x : expects) {
        const auto n = static_cast<std::size_t>(x.sample);
        const double f = static_cast<double>(Krate::DSP::midiNoteToFrequency(static_cast<int>(x.note)));
        std::size_t found = kSlots;
        std::size_t count = 0;
        for (std::size_t v = 0; v < kSlots; ++v) {
            const double fr = static_cast<double>(t.voices[v].fRes[n]);
            if (fr > 0.0 && std::abs(fr / f - 1.0) < 1.0e-6) {
                found = v;
                ++count;
            }
        }
        if (count != 1)
            ++ambiguous;
        if (x.steal)
            ++steals;
        if (found != x.slot) {
            if (mism < 5)
                std::cout << "VoiceEngine_StealOrder: at " << x.sample << " note " << static_cast<int>(x.note)
                          << " expected slot " << x.slot << " observed " << found << (x.steal ? " (steal)" : "")
                          << "\n";
            ++mism;
        }
    }
    std::cout << "VoiceEngine_StealOrder: " << events.size() << " events, " << expects.size() << " note-ons, "
              << steals << " steals, mismatches " << mism << ", ambiguous " << ambiguous << "\n";
    CHECK(mism == 0u);
    CHECK(ambiguous == 0u);
    CHECK(steals >= 300u);
}

namespace {

/// FR-026 / plan S6.5 + S5.4: one interval's call sequence.
[[nodiscard]] std::vector<ProfundumCall> peExpectedCallOrder(std::size_t voices, bool dcResync,
                                                             std::vector<std::uint8_t>& who) {
    using C = ProfundumCall;
    std::vector<C> v = {C::Events,     C::MorphAdvance, C::Macros,          C::Motion,      C::LawSetBase,
                        C::LawSetMass, C::LawAdvance,   C::FieldSetControls, C::FieldAdvance};
    if (dcResync)
        v.push_back(C::DcResync);
    who.assign(v.size(), kPeEngineVoice);
    // G14-3 (2026-10-21): the Drop setters lead the voice's interval, ahead of the pitch stage, so a transition's
    // deferred Drop trigger fires with this interval's values (FR-013 as amended).
    const C perVoice[] = {C::DropSetters,   C::VoicePan,   C::VoiceWide,      C::CoreControls,   C::CoreExcitation,
                          C::FoldAmount,    C::FoldF0,     C::Shape,          C::CharAlgorithm,  C::CharAmount,
                          C::CharTone,      C::HalfWave,   C::FollowRatio,    C::AnchorControls, C::LockBypass,
                          C::A1,            C::AmpEnv,     C::AmpCurve};
    for (std::size_t i = 0; i < voices; ++i) {
        for (const C c : perVoice) {
            v.push_back(c);
            who.push_back(static_cast<std::uint8_t>(i));
        }
    }
    for (const C c : {C::DynDuration, C::DynCompression, C::DynTrim, C::DynAttack, C::DynSustain, C::DynKey}) {
        v.push_back(c);
        who.push_back(kPeEngineVoice);
    }
    return v;
}

}  // namespace

TEST_CASE("VoiceEngine_CallOrder", "[systems][profundum][voice-engine][sc-032][fr-026]") {
    enableFTZDAZ();
    for (const std::size_t voices : {std::size_t{1}, std::size_t{4}}) {
        INFO("voices " << voices);
        PeScript s;
        s.fs = kPeFs;
        s.hostBlock = kPeHostBlock;
        s.totalSamples = peAt(0.6);
        s.a = peStateAs();
        s.b = Krate::DSP::BassMorphTest::showcasePair().second;
        s.mode = voices == 1 ? ProfundumEngine::VoiceMode::Mono : ProfundumEngine::VoiceMode::Poly;
        s.polyphony = 4;
        s.attachObserver = true;
        s.tappedVoices = 0;
        const std::uint8_t notes[] = {kPeNoteE1, kPeNoteA1, kPeNoteE2, kPeNoteC3};
        for (std::size_t i = 0; i < voices; ++i)
            s.events.push_back(peOn(0, notes[i]));
        const PeTaps t = PT::voiceEngineRender(s);
        for (const std::uint64_t g : {std::uint64_t{0}, peAt(0.5)}) {
            INFO("interval at " << g);
            REQUIRE(g % kPeGrid == 0);
            std::size_t i0 = t.calls.size();
            bool dc = false;
            for (std::size_t i = 0; i < t.calls.size(); ++i) {
                const ProfundumCallRecord& c = t.calls[i];
                if (c.sample == g && c.call == ProfundumCall::DcResync && c.voice == kPeEngineVoice)
                    dc = true;
                if (i0 == t.calls.size() && c.sample == g && c.call == ProfundumCall::Events
                    && c.voice == kPeEngineVoice)
                    i0 = i;
            }
            REQUIRE(i0 < t.calls.size());
            std::vector<ProfundumCall> got;
            std::vector<std::uint8_t> gotWho;
            for (std::size_t i = i0; i < t.calls.size(); ++i) {
                got.push_back(t.calls[i].call);
                gotWho.push_back(t.calls[i].voice);
                if (t.calls[i].call == ProfundumCall::DynKey && t.calls[i].voice == kPeEngineVoice)
                    break;
            }
            std::vector<std::uint8_t> expWho;
            const std::vector<ProfundumCall> expected = peExpectedCallOrder(voices, dc, expWho);
            std::cout << "VoiceEngine_CallOrder: voices " << voices << " interval " << g << " records " << got.size()
                      << " expected " << expected.size() << (dc ? " (with DcResync)" : "") << "\n";
            CHECK(got == expected);
            CHECK(gotWho == expWho);
        }
    }
}

TEST_CASE("VoiceEngine_TapTransparency", "[systems][profundum][voice-engine][sc-036][fr-086]") {
    enableFTZDAZ();
    for (const auto mode : {ProfundumEngine::VoiceMode::Mono, ProfundumEngine::VoiceMode::Poly}) {
        INFO("mode " << peIdx(mode));
        const PeScript none = peSc011Script(mode);   // no observer
        PeScript attached = none;
        attached.attachObserver = true;
        attached.tappedVoices = 0;
        PeScript phaseTap = attached;
        phaseTap.config.seams.anchorPhaseTap = true;
        PeScript counted = none;
        counted.countAllocations = true;
        const PeTaps tn = PT::voiceEngineRender(none);
        const std::size_t diffAttached = peOutMismatches(tn, PT::voiceEngineRender(attached));
        const std::size_t diffPhase = peOutMismatches(tn, PT::voiceEngineRender(phaseTap));
        const PeTaps tc = PT::voiceEngineRender(counted);
        std::cout << "VoiceEngine_TapTransparency: mode " << peIdx(mode) << " observer attached mismatches "
                  << diffAttached << ", anchorPhaseTap mismatches " << diffPhase << ", no-observer allocations "
                  << tc.allocations << "\n";
        CHECK(diffAttached == 0u);
        CHECK(diffPhase == 0u);
        CHECK(tc.allocations == 0u);
        CHECK(peOutMismatches(tn, tc) == 0u);
        CHECK(peVecRmsDb(tn.outL, tn.outR, peSamples(0.1), peSamples(1.5)) >= -40.0);
    }
}

TEST_CASE("VoiceEngine_A1MatchesUnbiasedH1", "[systems][profundum][voice-engine][fr-005][c-6]") {
    enableFTZDAZ();
    using Krate::DSP::AnchorOscillator;
    for (const auto anchorMode : {AnchorOscillator::Mode::Lock, AnchorOscillator::Mode::Follow}) {
        for (const std::size_t voices : {std::size_t{1}, std::size_t{4}}) {
            INFO("anchor mode " << peIdx(anchorMode) << " voices " << voices);
            PeScript s;
            s.fs = kPeFs;
            s.hostBlock = kPeHostBlock;
            s.totalSamples = peAt(1.0);
            s.a = peStateAs();
            s.b = Krate::DSP::BassMorphTest::showcasePair().second;
            for (MorphState* st : {&s.a, &s.b}) {
                peSetMotion(*st, PeMotionSource::Cycle, PeMotionTarget::Depth, 1.0f, 1.0f);
                st->motion.cycleRateHz = 2.0f;
                st->anchor.mode = anchorMode;
            }
            REQUIRE(Krate::DSP::isValidMorphState(s.a));
            REQUIRE(Krate::DSP::isValidMorphState(s.b));
            s.mode = voices == 1 ? ProfundumEngine::VoiceMode::Mono : ProfundumEngine::VoiceMode::Poly;
            s.polyphony = 4;
            s.attachObserver = true;
            s.tappedVoices = voices;
            const std::uint8_t notes[] = {kPeNoteE1, kPeNoteA1, kPeNoteE2, kPeNoteC3};
            for (std::size_t i = 0; i < voices; ++i)
                s.events.push_back(peOn(0, notes[i]));
            s.events.push_back(peSet(peAt(0.1), PeKind::MorphRampMs, 500.0f));
            s.events.push_back(peSet(peAt(0.1), PeKind::MorphPosition, 1.0f));
            const PeTaps t = PT::voiceEngineRender(s);
            peRequireGridAligned(t);
            std::size_t checked = 0;
            std::size_t mism = 0;
            for (std::size_t v = 0; v < voices; ++v) {
                for (const PT::VoiceEngineChunkRecord& c : t.voices[v].chunks) {
                    if (c.sample % kPeGrid != 0)
                        continue;   // the interval's first sub-chunk carries the a1 handed at its grid point
                    const auto k = static_cast<std::size_t>(c.sample / kPeGrid);
                    REQUIRE(k < t.grid.size());
                    const auto n = static_cast<std::size_t>(c.sample);
                    const float expected = Krate::DSP::MassDynamicsTest::unbiasedH1Amplitude(
                        t.grid[k].afterMotion.recipe, static_cast<double>(t.voices[v].fAnchor[n]), kPeFs);
                    ++checked;
                    if (!peSameBits(c.a1, expected))
                        ++mism;
                }
            }
            std::cout << "VoiceEngine_A1MatchesUnbiasedH1: anchor mode " << peIdx(anchorMode) << " voices "
                      << voices << " intervals checked " << checked << " mismatches " << mism << "\n";
            CHECK(mism == 0u);
            CHECK(checked + 2 * voices >= voices * t.grid.size());
        }
    }
}

namespace {

/// FR-021 reference: a standalone MonoHandler and NoteProcessor configured as the engine configures its own
/// (prepare; reset; the prepare-time settings in applySettings order; then the changed settings at grid 0), the note
/// events applied at the first grid point at or after their sample, then per sample
/// processPortamento() * semitonesToRatio(processPitchBend() * range) (C-11).
[[nodiscard]] std::vector<float> peMonoReference(const std::vector<PeEvent>& sorted, MonoMode priority,
                                                 PortaMode portaMode, float portaMs, std::size_t total) {
    Krate::DSP::MonoHandler mono;
    Krate::DSP::NoteProcessor bend;
    mono.prepare(kPeFs);
    bend.prepare(kPeFs);
    mono.reset();
    bend.reset();
    mono.setMode(MonoMode::LastNote);
    mono.setLegato(false);
    mono.setPortamentoTime(0.0f);
    mono.setPortamentoMode(PortaMode::Always);
    mono.setGlideMode(GlideMode::ConstantTime);
    bend.setPitchBend(0.0f);
    bend.setPitchBendRange(ProfundumEngine::kDefaultBendRangeSt);
    if (priority != MonoMode::LastNote)
        mono.setMode(priority);
    if (!peSameBits(portaMs, 0.0f))
        mono.setPortamentoTime(portaMs);
    if (portaMode != PortaMode::Always)
        mono.setPortamentoMode(portaMode);
    std::vector<float> out(total, 0.0f);
    std::size_t ei = 0;
    for (std::size_t g = 0; g < total; g += kPeGrid) {
        while (ei < sorted.size() && sorted[ei].sample <= g) {
            const PeEvent& e = sorted[ei++];
            if (e.kind == PeKind::NoteOn)
                static_cast<void>(mono.noteOn(static_cast<int>(e.note), static_cast<int>(e.velocity)));
            else if (e.kind == PeKind::NoteOff)
                static_cast<void>(mono.noteOff(static_cast<int>(e.note)));
        }
        for (std::size_t k = 0; k < kPeGrid && g + k < total; ++k) {
            const float b = bend.processPitchBend();
            const float ratio = Krate::DSP::semitonesToRatio(b * ProfundumEngine::kDefaultBendRangeSt);
            out[g + k] = mono.processPortamento() * ratio;
        }
    }
    return out;
}

[[nodiscard]] PeScript peMonoScript(double seconds) {
    PeScript s;
    s.fs = kPeFs;
    s.hostBlock = kPeHostBlock;
    s.totalSamples = peAt(seconds);
    s.a = peStateAs();
    s.b = s.a;
    s.mode = ProfundumEngine::VoiceMode::Mono;
    s.attachObserver = true;
    s.tappedVoices = 1;
    return s;
}

/// Voice 0's f_res tap against the reference on every sample voice 0 rendered: {mismatches, compared}.
[[nodiscard]] std::pair<std::size_t, std::size_t> peFResAgainst(const PeTaps& t, const std::vector<float>& ref) {
    const std::vector<std::uint8_t> mask = peRenderedMask(t, 0);
    std::size_t mism = 0;
    std::size_t compared = 0;
    for (std::size_t n = 0; n < t.total; ++n) {
        if (mask[n] == 0)
            continue;
        ++compared;
        if (!peSameBits(t.voices[0].fRes[n], ref[n]))
            ++mism;
    }
    return {mism, compared};
}

}  // namespace

TEST_CASE("VoiceEngine_MonoPriorityAndPortaMode", "[systems][profundum][voice-engine][fr-021]") {
    enableFTZDAZ();
    const auto freq = [](std::uint8_t note) {
        return static_cast<double>(Krate::DSP::midiNoteToFrequency(static_cast<int>(note)));
    };

    SECTION("priority: f_res equals a standalone MonoHandler times the bend ratio") {
        struct Arm {
            MonoMode priority;
            std::uint8_t at035;   // the sounding key with E2 + E1 held
            std::uint8_t at055;   // the sounding key with E2 + E1 + C3 held
        };
        const Arm arms[] = {{MonoMode::LastNote, kPeNoteE1, kPeNoteC3},
                            {MonoMode::LowNote, kPeNoteE1, kPeNoteE1},
                            {MonoMode::HighNote, kPeNoteE2, kPeNoteC3}};
        for (const Arm& arm : arms) {
            INFO("priority " << peIdx(arm.priority));
            PeScript s = peMonoScript(1.3);
            s.events = {peSet(0, PeKind::MonoPriority, 0.0f, peIdx(arm.priority)),
                        peSet(0, PeKind::PortamentoTimeMs, 30.0f),
                        peOn(0, kPeNoteE2),
                        peOn(peAt(0.2), kPeNoteE1),
                        peOn(peAt(0.4), kPeNoteC3),
                        peOff(peAt(0.6), kPeNoteC3),
                        peOff(peAt(0.8), kPeNoteE1),
                        peOff(peAt(1.0), kPeNoteE2)};
            const PeTaps t = PT::voiceEngineRender(s);
            const std::vector<float> ref =
                peMonoReference(peSortedEvents(s), arm.priority, PortaMode::Always, 30.0f, t.total);
            const auto [mism, compared] = peFResAgainst(t, ref);
            const double c035 = peCents(static_cast<double>(t.voices[0].fRes[static_cast<std::size_t>(peAt(0.35))]),
                                        freq(arm.at035));
            const double c055 = peCents(static_cast<double>(t.voices[0].fRes[static_cast<std::size_t>(peAt(0.55))]),
                                        freq(arm.at055));
            std::cout << std::setprecision(6) << "VoiceEngine_MonoPriorityAndPortaMode: priority "
                      << peIdx(arm.priority) << " f_res mismatches " << mism << " of " << compared
                      << ", cents at 0.35 s " << c035 << ", at 0.55 s " << c055 << "\n";
            CHECK(mism == 0u);
            CHECK(compared >= static_cast<std::size_t>(peAt(1.0)));
            CHECK(std::abs(c035) < 0.1);
            CHECK(std::abs(c055) < 0.1);
        }
    }

    SECTION("PortaMode: LegatoOnly glides only overlapped notes, Always both") {
        for (const PortaMode pm : {PortaMode::Always, PortaMode::LegatoOnly}) {
            INFO("porta mode " << peIdx(pm));
            PeScript s = peMonoScript(1.3);
            s.events = {peSet(0, PeKind::PortamentoMode, 0.0f, peIdx(pm)),
                        peSet(0, PeKind::PortamentoTimeMs, 100.0f),
                        peOn(0, kPeNoteE1),
                        peOff(peAt(0.3), kPeNoteE1),
                        peOn(peAt(0.5), kPeNoteA1),    // not overlapped (E1 released 200 ms earlier)
                        peOn(peAt(0.8), kPeNoteE2),    // overlapped (A1 held)
                        peOff(peAt(1.0), kPeNoteA1),
                        peOff(peAt(1.1), kPeNoteE2)};
            const PeTaps t = PT::voiceEngineRender(s);
            const std::vector<float> ref = peMonoReference(peSortedEvents(s), MonoMode::LastNote, pm, 100.0f, t.total);
            const auto [mism, compared] = peFResAgainst(t, ref);
            const auto gA = static_cast<std::size_t>(peAt(0.5));
            const auto gE = static_cast<std::size_t>(peAt(0.8));
            REQUIRE(gA % kPeGrid == 0);
            REQUIRE(gE % kPeGrid == 0);
            const double centsA = peCents(static_cast<double>(t.voices[0].fRes[gA]), freq(kPeNoteA1));
            const double centsE = peCents(static_cast<double>(t.voices[0].fRes[gE]), freq(kPeNoteE2));
            std::cout << std::setprecision(6) << "VoiceEngine_MonoPriorityAndPortaMode: porta mode " << peIdx(pm)
                      << " f_res mismatches " << mism << " of " << compared << ", cents from target at the "
                      << "non-overlapped onset " << centsA << ", at the overlapped onset " << centsE << "\n";
            CHECK(mism == 0u);
            CHECK(compared > 0u);
            CHECK(std::abs(centsE) > 50.0);   // overlapped: glides in both modes
            if (pm == PortaMode::Always)
                CHECK(std::abs(centsA) > 50.0);   // glides from E1
            else
                CHECK(std::abs(centsA) < 0.1);    // no glide
        }
    }
}

namespace {

/// FR-028: the side-channel (0.5 (L - R)) power of the Field tap, per interval, through a two-f0-period triangular
/// window ending at each grid point (0 before two full periods). The side energy sits in the waveform's edges, one
/// burst per f0 cycle, so a one-period boxcar holds exactly one burst and its level is a staircase that steps once
/// per cycle (~36 intervals at E1): a level crossing then lands anywhere on a stair, and the mean crossing spacing
/// read 95599 samples for a 96000-sample field period. A second one-period boxcar (the triangle) interpolates the
/// stairs linearly, so the crossings read the field Motion period.
[[nodiscard]] std::vector<double> peSideLevelPerInterval(const PeTaps& t, double f0) {
    const auto p = static_cast<std::size_t>(std::llround(t.fs / f0));
    std::vector<double> prefix(t.total + 1, 0.0);
    for (std::size_t n = 0; n < t.total; ++n) {
        const double s = 0.5 * (static_cast<double>(t.stageOutL[n]) - static_cast<double>(t.stageOutR[n]));
        prefix[n + 1] = prefix[n] + s * s;
    }
    // prefix2[n] = sum of the one-period boxcar outputs ending at samples p .. n - 1.
    std::vector<double> prefix2(t.total + 1, 0.0);
    for (std::size_t n = 1; n <= t.total; ++n)
        prefix2[n] = prefix2[n - 1] + (n >= p ? (prefix[n] - prefix[n - p]) / static_cast<double>(p) : 0.0);
    std::vector<double> level(t.grid.size(), 0.0);
    for (std::size_t k = 0; k < level.size(); ++k) {
        const std::size_t n = k * kPeGrid;
        if (n >= 2 * p && n <= t.total)
            level[k] = (prefix2[n] - prefix2[n - p]) / static_cast<double>(p);
    }
    return level;
}

/// Subtracts the mean over [from, end).
void peDetrend(std::vector<double>& x, std::size_t from) {
    REQUIRE(from < x.size());
    double m = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
        m += x[i];
    m /= static_cast<double>(x.size() - from);
    for (double& v : x)
        v -= m;
}

}  // namespace

TEST_CASE("VoiceEngine_HostTempo", "[systems][profundum][voice-engine][fr-028]") {
    enableFTZDAZ();
    // CYCLE synced at 1/4 (Motion Depth 0: the source runs and is tapped, the sound is untouched (SC-026), so the
    // field measurement is not modulated by it) and a synced field Motion (Cycle, Whole, depth 1) in WIDE.
    MorphState st = Krate::DSP::BassMorphTest::showcasePair().second;
    st.ampEnv.sustain = 1.0f;   // a steady level after the attack (C-8 practice)
    peSetMotion(st, PeMotionSource::Cycle, PeMotionTarget::Depth, 1.0f, 0.0f);
    st.motion.cycleSync = true;
    st.motion.cycleNote = Krate::DSP::NoteValue::Quarter;
    st.motion.cycleModifier = Krate::DSP::NoteModifier::None;
    st.field.mode = Krate::DSP::BassStereoField::ImageMode::Wide;
    st.field.spread = 1.0f;
    st.field.motionDepth = 1.0f;
    st.field.motionSource = Krate::DSP::BassStereoField::MotionSource::Cycle;
    st.field.cycleSync = true;
    st.field.cycleNote = Krate::DSP::NoteValue::Whole;   // 4 beats: <= 1 Hz up to 240 BPM, no tempo fold
    st.field.cycleModifier = Krate::DSP::NoteModifier::None;
    REQUIRE(Krate::DSP::isValidMorphState(st));
    const double f0 = static_cast<double>(Krate::DSP::midiNoteToFrequency(kPeNoteE1));

    struct Reading {
        double motion = 0.0;
        double field = 0.0;
    };
    const auto measure = [&](float bpm) {
        PeScript s;
        s.fs = kPeFs;
        s.hostBlock = kPeHostBlock;
        s.totalSamples = peAt(6.5);
        s.a = st;
        s.b = st;
        s.mode = ProfundumEngine::VoiceMode::Mono;
        s.attachObserver = true;
        s.tappedVoices = 0;
        s.events = {peOn(0, kPeNoteE1)};
        if (bpm > 0.0f)
            s.events.push_back(peSet(0, PeKind::Tempo, bpm));
        const PeTaps t = PT::voiceEngineRender(s);
        peRequireGridAligned(t);
        const std::size_t k0 = static_cast<std::size_t>(peAt(0.5)) / kPeGrid;
        std::vector<double> src(t.grid.size(), 0.0);
        for (std::size_t k = 0; k < t.grid.size(); ++k)
            src[k] = static_cast<double>(t.grid[k].motionSource);
        std::vector<double> side = peSideLevelPerInterval(t, f0);
        peDetrend(side, k0);
        Reading r;
        r.motion = peMeanPeriod(peUpCrossings(src, k0)) * static_cast<double>(kPeGrid);
        r.field = peMeanPeriod(peUpCrossings(side, k0)) * static_cast<double>(kPeGrid);
        return r;
    };
    const double tol = static_cast<double>(kPeGrid);   // within one interval
    for (const float bpm : {90.0f, 120.0f, 150.0f}) {
        INFO("bpm " << bpm);
        const Reading r = measure(bpm);
        const double expMotion = 60.0 / static_cast<double>(bpm) * kPeFs;
        const double expField = 4.0 * 60.0 / static_cast<double>(bpm) * kPeFs;
        std::cout << std::setprecision(8) << "VoiceEngine_HostTempo: " << bpm << " BPM CYCLE period " << r.motion
                  << " (expected " << expMotion << "), field Motion period " << r.field << " (expected " << expField
                  << ") samples\n";
        CHECK(std::abs(r.motion - expMotion) <= tol);
        CHECK(std::abs(r.field - expField) <= tol);
    }
    const Reading def = measure(-1.0f);   // no setTempoBpm
    const double exp120Motion = 0.5 * kPeFs;
    const double exp120Field = 2.0 * kPeFs;
    std::cout << std::setprecision(8) << "VoiceEngine_HostTempo: no tempo set: CYCLE period " << def.motion
              << ", field Motion period " << def.field << " samples (120 BPM: " << exp120Motion << ", "
              << exp120Field << ")\n";
    CHECK(std::abs(def.motion - exp120Motion) <= tol);
    CHECK(std::abs(def.field - exp120Field) <= tol);
}

TEST_CASE("VoiceEngine_MorphRuntimeRoundTrip", "[systems][profundum][voice-engine][fr-030]") {
    enableFTZDAZ();
    // Endpoints that differ only in voice-level timbre (recipe, SHAPE, Character Amount, Fold), so a silent pre-roll
    // leaves no field / dynamics history that depends on the morph position.
    const MorphState a = peStateAs();
    MorphState b2 = a;
    b2.recipe = Krate::DSP::SpectralShapeRecipe::kHeavy;
    b2.shape = 0.5f;
    b2.characterAmount = 0.5f;
    b2.foldAmount = 40.0f;
    REQUIRE(Krate::DSP::isValidMorphState(b2));
    const auto make = [&a, &b2]() {
        auto e = std::make_unique<ProfundumEngine>();
        REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, a));
        REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, b2));
        e->prepare(kPeFs);
        return e;
    };
    constexpr std::size_t kPreRoll = 9600;   // 300 intervals: 200 ms of a 500 ms ramp
    constexpr std::size_t kCap = 64;

    auto e = make();
    Krate::DSP::BassMorphEngine m;
    REQUIRE(m.setEndpoint(BassMorphEngine::Endpoint::A, a));
    REQUIRE(m.setEndpoint(BassMorphEngine::Endpoint::B, b2));
    m.prepare(kPeFs);
    e->setMorphRampMs(500.0f);
    e->setMorphPosition(1.0f);
    m.setPositionRampMs(500.0f);
    m.setPosition(1.0f);
    PeStereo pre;
    peRender(*e, pre, kPreRoll);
    for (std::size_t i = 0; i < kPreRoll / kPeGrid; ++i)
        static_cast<void>(m.advanceInterval());
    std::array<std::byte, kCap> blobE{};
    std::array<std::byte, kCap> blobM{};
    const std::size_t nE = e->serializeMorphRuntime(blobE.data(), blobE.size());
    const std::size_t nM = m.serializeRuntime(blobM.data(), blobM.size());
    REQUIRE(nE == BassMorphEngine::kMorphRuntimeBytes);
    REQUIRE(nM == nE);
    CHECK(std::equal(blobE.begin(), blobE.begin() + static_cast<std::ptrdiff_t>(nE), blobM.begin()));
    CHECK(m.position() > 0.1f);   // mid-ramp
    CHECK(m.position() < 0.9f);

    auto e2 = make();
    PeStereo pre2;
    peRender(*e2, pre2, kPreRoll);
    // A corrupt blob (the version byte, offset 0: bass_morph_engine.h kRtVersionOffset) returns false, changes nothing.
    std::array<std::byte, kCap> before{};
    std::array<std::byte, kCap> after{};
    REQUIRE(e2->serializeMorphRuntime(before.data(), before.size()) == nE);
    std::array<std::byte, kCap> bad = blobE;
    bad[0] ^= std::byte{0xFF};
    CHECK_FALSE(e2->restoreMorphRuntime(bad.data(), nE));
    REQUIRE(e2->serializeMorphRuntime(after.data(), after.size()) == nE);
    CHECK(std::equal(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(nE), after.begin()));

    e2->setMorphRampMs(500.0f);
    e2->setMorphPosition(1.0f);
    REQUIRE(e2->restoreMorphRuntime(blobE.data(), nE));
    REQUIRE(e2->serializeMorphRuntime(after.data(), after.size()) == nE);
    CHECK(std::equal(blobE.begin(), blobE.begin() + static_cast<std::ptrdiff_t>(nE), after.begin()));

    e->noteOn(kPeNoteE1, kPeVelocity);
    e2->noteOn(kPeNoteE1, kPeVelocity);
    PeStereo o1;
    PeStereo o2;
    peRender(*e, o1, peSamples(0.5));
    peRender(*e2, o2, peSamples(0.5));
    const std::size_t diff = peMismatches(o1, o2);
    std::cout << "VoiceEngine_MorphRuntimeRoundTrip: restored engine vs original mismatches " << diff << "\n";
    CHECK(diff == 0u);
    CHECK(peRmsDb(o1, peSamples(0.1), o1.l.size()) >= -40.0);
}

TEST_CASE("VoiceEngine_PolyEnvRetrigger", "[systems][profundum][voice-engine][fr-045]") {
    enableFTZDAZ();
    MorphState st = peStateAs();
    peSetMotion(st, PeMotionSource::Env, PeMotionTarget::Depth, 1.0f, 1.0f);
    st.motion.polarity = MorphState::MotionConfig::Polarity::Unipolar;   // v = e
    st.motion.envAttackMs = 10.0f;
    st.motion.envDecayMs = 300.0f;
    REQUIRE(Krate::DSP::isValidMorphState(st));
    const std::uint64_t second = peAt(0.15);   // the first note's ENV is decaying here
    REQUIRE(second % kPeGrid == 0);
    const auto run = [&st, second](ProfundumEngine::VoiceMode mode, bool legatoCell) {
        PeScript s;
        s.fs = kPeFs;
        s.hostBlock = kPeHostBlock;
        s.totalSamples = peAt(0.4);
        s.a = st;
        s.b = st;
        s.mode = mode;
        s.polyphony = 4;
        s.attachObserver = true;
        s.tappedVoices = 0;
        if (legatoCell)
            s.events.push_back(peSet(0, PeKind::NoteMemoryMode, 0.0f, peIdx(NoteMemory::Mode::Legato)));
        s.events.push_back(peOn(0, kPeNoteE1));
        s.events.push_back(peOn(second, kPeNoteA1));   // E1 still held
        const PeTaps t = PT::voiceEngineRender(s);
        peRequireGridAligned(t);
        return t;
    };
    const std::size_t k = static_cast<std::size_t>(second) / kPeGrid;
    const std::size_t kEnd = k + static_cast<std::size_t>(peAt(0.05)) / kPeGrid;
    const auto peakOver = [](const PeTaps& t, std::size_t from, std::size_t to) {
        float p = 0.0f;
        for (std::size_t i = from; i < to && i < t.grid.size(); ++i)
            p = std::max(p, t.grid[i].motionSource);
        return p;
    };

    // POLY: the second note starts another voice (FromSilence, an envelope-restarting cell): ENV restarts at k.
    const PeTaps poly = run(ProfundumEngine::VoiceMode::Poly, false);
    const float pBefore = poly.grid[k - 1].motionSource;
    const float pAt = poly.grid[k].motionSource;
    const float pPeak = peakOver(poly, k, kEnd);
    // MONO Legato, overlapped key (Overlap -> no envelope restart, note_memory.h kMatrix Legato row): ENV continues.
    const PeTaps mono = run(ProfundumEngine::VoiceMode::Mono, true);
    const float mBefore = mono.grid[k - 1].motionSource;
    const float mAt = mono.grid[k].motionSource;
    const float mPeak = peakOver(mono, k, kEnd);
    std::cout << std::setprecision(6) << "VoiceEngine_PolyEnvRetrigger: POLY ENV before " << pBefore << " at "
              << pAt << " peak after " << pPeak << "; MONO Legato overlap before " << mBefore << " at " << mAt
              << " peak after " << mPeak << "\n";
    CHECK(poly.grid[0].motionSource > 0.0f);   // the first note started the attack
    CHECK(pBefore < 0.95f);                    // decaying when the second note arrives
    CHECK(pAt > pBefore);                      // attack restarted at that grid point
    CHECK(pPeak >= 0.99f);
    CHECK(mAt <= mBefore);                     // the non-restarting cell keeps decaying
    CHECK(mPeak <= mBefore);
}

// ------------------------------------------------------------------------------
// T014 - the SC-011 cross-build fingerprint (deviation 6), its harvest, and the H-29 record (plan S10.5, S10.6)
// ------------------------------------------------------------------------------
// VoiceEngine_FingerprintHarvest [.harvest] sits here, not in the probe TU (deviation 4's default home of the harvests):
// it renders peSc011Script, the very schedule VoiceEngine_DeterminismFingerprint compares, so the harvest and the
// comparison cannot drift apart, and the constants are harvested inside the consuming TU of dsp_systems_tests.
// Decisions: evidence/run-log/T014.txt.

namespace {

using PeFingerprint = Krate::DSP::TestUtils::RenderFingerprint;

/// {MONO, POLY} x {L, R} of the SC-011 schedule's output tap.
constexpr std::size_t kPeFingerprintCount = 4;
constexpr std::array<const char*, kPeFingerprintCount> kPeFingerprintNames{"MONO L", "MONO R", "POLY L", "POLY R"};

// Pasted by specs/profundum-phase9-voice-engine/evidence/close/paste-harvest.js from evidence/fingerprint.inc (written
// by VoiceEngine_FingerprintHarvest). Unharvested, kPeFingerprintHarvested is false and
// VoiceEngine_DeterminismFingerprint FAILs "not harvested" (deviation 6; the guard stays against a lost paste).
// HARVEST-BEGIN fingerprint
// harvested 2026-10-21 from evidence/fingerprint.inc (plan S10.5, T014)
constexpr bool kPeFingerprintHarvested = true;
constexpr std::array<PeFingerprint, kPeFingerprintCount> kPeFingerprints{{
    // MONO L
    {.rms = 2.85468489429255901e-01,
     .peak = 8.91250610351562500e-01,
     .meanAbs = 2.01079971607361963e-01,
     .totalVariation = 1.30776739340463973e+03,
     .checkpoints = {{
         0.00000000e+00f, -6.53453529e-01f, -6.39397383e-01f, -9.15564522e-02f,
         -5.53802431e-01f, 2.20889986e-01f, -4.16891396e-01f, -7.98447132e-02f,
         3.99964899e-01f, -9.65944082e-02f, -3.20031255e-01f, 1.09784730e-01f,
         3.70580226e-01f, -3.77560034e-02f, -3.17762256e-01f, 9.84504893e-02f,
         1.37064010e-01f, -1.54584825e-01f, 4.44102734e-02f, 3.10494214e-01f,
         -9.17841494e-02f, -2.97399938e-01f, 1.13288045e-01f, -2.01111481e-01f,
         -2.95580234e-02f, -7.38661474e-05f, -3.89227580e-06f, -2.04818065e-07f,
         -1.07733085e-08f, -5.66570457e-10f, -2.97960476e-11f, -1.56698059e-12f}}},
    // MONO R
    {.rms = 2.90295217840609632e-01,
     .peak = 8.91250908374786377e-01,
     .meanAbs = 2.04403158965032000e-01,
     .totalVariation = 1.13988555390119291e+03,
     .checkpoints = {{
         0.00000000e+00f, -6.53453529e-01f, -6.39397383e-01f, -9.24425274e-02f,
         -5.62449157e-01f, 2.18041465e-01f, -4.09261495e-01f, -8.65796432e-02f,
         3.76679838e-01f, -8.56955275e-02f, -4.20920044e-01f, 9.83461291e-02f,
         6.31757021e-01f, -1.36915386e-01f, -3.15269202e-01f, 1.15952484e-01f,
         2.55150080e-01f, -1.89305797e-01f, 2.82217301e-02f, 2.83152819e-01f,
         -8.43949690e-02f, -2.74676859e-01f, 9.98964235e-02f, 3.17364931e-02f,
         -5.15497513e-02f, -7.10203676e-05f, -3.74231399e-06f, -1.96927175e-07f,
         -1.03582494e-08f, -5.44742251e-10f, -2.86481498e-11f, -1.50661439e-12f}}},
    // POLY L
    {.rms = 3.05361001970383239e-01,
     .peak = 8.91250908374786377e-01,
     .meanAbs = 2.07363823635370564e-01,
     .totalVariation = 1.74701121041401871e+03,
     .checkpoints = {{
         0.00000000e+00f, -6.53452456e-01f, -6.39400005e-01f, -4.78940219e-01f,
         6.54527187e-01f, 5.43578923e-01f, 3.23280513e-01f, -3.43544602e-01f,
         1.63396835e-01f, 1.58261850e-01f, -3.90734375e-01f, -1.84057027e-01f,
         2.67539114e-01f, 1.08349249e-01f, -4.40798216e-02f, 1.18326470e-01f,
         5.73304109e-02f, -4.36611176e-01f, -3.59008133e-01f, 2.99581856e-01f,
         4.29208606e-01f, -3.42164636e-01f, -5.17999113e-01f, 6.38283566e-02f,
         4.48689759e-02f, -7.78315516e-05f, -4.10029224e-06f, -2.15747562e-07f,
         -1.13478507e-08f, -5.96786121e-10f, -3.13851861e-11f, -1.65055545e-12f}}},
    // POLY R
    {.rms = 3.09519169261240346e-01,
     .peak = 8.91250908374786377e-01,
     .meanAbs = 2.10550151039631595e-01,
     .totalVariation = 1.55749716671699844e+03,
     .checkpoints = {{
         0.00000000e+00f, -6.53452456e-01f, -6.39400005e-01f, -4.78760421e-01f,
         6.60947382e-01f, 5.30979753e-01f, 2.87147820e-01f, -3.14599872e-01f,
         1.66383669e-01f, 1.25256062e-01f, -4.56814468e-01f, 1.04265876e-01f,
         2.26046398e-01f, 1.26466408e-01f, 1.11926734e-01f, 2.71306604e-01f,
         1.09178267e-01f, -4.93149042e-01f, -3.33140939e-01f, 3.05949271e-01f,
         4.12038475e-01f, -1.98950440e-01f, -7.16761574e-02f, 7.86855295e-02f,
         -1.08304791e-01f, -7.52892956e-05f, -3.96637824e-06f, -2.08701607e-07f,
         -1.09772493e-08f, -5.77295767e-10f, -3.03601658e-11f, -1.59664881e-12f}}},
}};
// HARVEST-END fingerprint

[[nodiscard]] std::string peEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

void peWriteEvidence(const std::string& relPath, const std::string& text) {
    const std::filesystem::path p = std::filesystem::path(peEvidenceDir()) / relPath;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p);
    out << text;
    CHECK(out.good());
}

/// The SC-011 schedule's four output fingerprints (peSc011Script: observer detached, no voice taps).
[[nodiscard]] std::array<PeFingerprint, kPeFingerprintCount> peSc011Fingerprints() {
    std::array<PeFingerprint, kPeFingerprintCount> out{};
    std::size_t k = 0;
    for (const auto mode : {ProfundumEngine::VoiceMode::Mono, ProfundumEngine::VoiceMode::Poly}) {
        const PeTaps t = PT::voiceEngineRender(peSc011Script(mode));
        out[k++] = Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(t.outL));
        out[k++] = Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(t.outR));
    }
    return out;
}

[[nodiscard]] bool peFingerprintFinite(const PeFingerprint& f) noexcept {
    for (const double d : {f.rms, f.peak, f.meanAbs, f.totalVariation}) {
        if ((std::bit_cast<std::uint64_t>(d) & 0x7FF0000000000000ull) == 0x7FF0000000000000ull)
            return false;
    }
    for (const float c : f.checkpoints) {
        if (!peFinite(c))
            return false;
    }
    return true;
}

/// A double literal that round-trips (17 significant digits, scientific).
[[nodiscard]] std::string peDoubleLiteral(double v) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(17) << v;
    return os.str();
}

/// A float literal with an f suffix that round-trips (9 significant digits, scientific).
[[nodiscard]] std::string peFloatLiteral(float v) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(8) << v;
    return os.str() + "f";
}

/// H-29: RMS dB and h1 dB (single-bin DFT at f0, RMS convention 20 log10(|h1| / sqrt 2)) of the mid of L / R over
/// whole periods of f0 from `start`, within `maxLen` samples.
struct PeLevelH1 {
    double levelDb = 0.0;
    double h1Db = 0.0;
};

[[nodiscard]] PeLevelH1 peLevelH1(const std::vector<float>& l, const std::vector<float>& r, std::size_t start,
                                  std::size_t maxLen, double f0) {
    const double period = kPeFs / f0;
    const double periods = std::floor(static_cast<double>(maxLen) / period);
    REQUIRE(periods >= 1.0);
    const auto n = static_cast<std::size_t>(std::llround(periods * period));
    REQUIRE(start + n <= l.size());
    REQUIRE(start + n <= r.size());
    constexpr double kTwoPi = 6.283185307179586476925;
    double sq = 0.0;
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        const double x = 0.5 * (static_cast<double>(l[start + k]) + static_cast<double>(r[start + k]));
        const double ph = kTwoPi * f0 * static_cast<double>(k) / kPeFs;
        sq += x * x;
        re += x * std::cos(ph);
        im -= x * std::sin(ph);
    }
    const double nn = static_cast<double>(n);
    const double rms = std::sqrt(sq / nn);
    const double h1 = 2.0 * std::sqrt(re * re + im * im) / nn;
    PeLevelH1 out;
    out.levelDb = rms > 0.0 ? 20.0 * std::log10(rms) : -300.0;
    out.h1Db = h1 > 0.0 ? 20.0 * std::log10(h1 / std::sqrt(2.0)) : -300.0;
    return out;
}

}  // namespace

// SC-011 harvest (hidden; tasks.md T014; plan S10.5). Run by evidence/close/harvest.sh, never by a gate. Writes the
// initialiser lines that paste-harvest.js puts between the fingerprint markers above.
TEST_CASE("VoiceEngine_FingerprintHarvest", "[systems][profundum][voice-engine][sc-011][.harvest]") {
    enableFTZDAZ();
    const std::array<PeFingerprint, kPeFingerprintCount> fps = peSc011Fingerprints();
    std::ostringstream inc;
    inc << "constexpr bool kPeFingerprintHarvested = true;\n"
        << "constexpr std::array<PeFingerprint, kPeFingerprintCount> kPeFingerprints{{\n";
    for (std::size_t i = 0; i < kPeFingerprintCount; ++i) {
        const PeFingerprint& f = fps[i];
        INFO(kPeFingerprintNames[i]);
        REQUIRE(peFingerprintFinite(f));
        REQUIRE(f.rms > 0.0);
        std::cout << std::setprecision(9) << "VoiceEngine_FingerprintHarvest: " << kPeFingerprintNames[i] << " rms "
                  << f.rms << " peak " << f.peak << " meanAbs " << f.meanAbs << " totalVariation " << f.totalVariation
                  << "\n";
        inc << "    // " << kPeFingerprintNames[i] << "\n"
            << "    {.rms = " << peDoubleLiteral(f.rms) << ",\n"
            << "     .peak = " << peDoubleLiteral(f.peak) << ",\n"
            << "     .meanAbs = " << peDoubleLiteral(f.meanAbs) << ",\n"
            << "     .totalVariation = " << peDoubleLiteral(f.totalVariation) << ",\n"
            << "     .checkpoints = {{";
        for (std::size_t c = 0; c < f.checkpoints.size(); ++c) {
            if (c % 4 == 0)
                inc << "\n         ";
            else
                inc << " ";
            inc << peFloatLiteral(f.checkpoints[c]) << (c + 1 < f.checkpoints.size() ? "," : "");
        }
        inc << "}}},\n";
    }
    inc << "}};\n";
    peWriteEvidence("fingerprint.inc", inc.str());
}

// SC-011 (the cross-build arm, deviation 6): the SC-011 schedule's output fingerprints against the harvested constants
// through compareFingerprints at the shared bounds (kSampleTolerance 5e-4, kMetricTolerance; render_fingerprint.h).
// Same-path renders only (H-29).
TEST_CASE("VoiceEngine_DeterminismFingerprint", "[systems][profundum][voice-engine][sc-011][fr-062]") {
    enableFTZDAZ();
    if constexpr (!kPeFingerprintHarvested) {
        FAIL("not harvested: run specs/profundum-phase9-voice-engine/evidence/close/harvest.sh, then rebuild");
    } else {
        const std::array<PeFingerprint, kPeFingerprintCount> fps = peSc011Fingerprints();
        for (std::size_t i = 0; i < kPeFingerprintCount; ++i) {
            const Krate::DSP::TestUtils::FingerprintComparison cmp =
                Krate::DSP::TestUtils::compareFingerprints(fps[i], kPeFingerprints[i]);
            std::cout << std::setprecision(6) << "VoiceEngine_DeterminismFingerprint: " << kPeFingerprintNames[i]
                      << " worst metric rel " << cmp.worstMetricRelativeError << " (bound " << cmp.metricTolerance
                      << "), worst sample " << cmp.worstSampleError << " (bound " << cmp.sampleTolerance << ")\n";
            INFO(kPeFingerprintNames[i] << ": " << cmp.detail);
            CHECK(cmp.sampleTolerance == Krate::DSP::TestUtils::kSampleTolerance);
            CHECK(cmp.withinTolerance());
        }
    }
}

// SC-011 / H-29 Record (FR-070; plan S10.6; tasks.md T014): HarmonicFold path dependence above kSoftEnd. One held E1
// on A_s (C-8) reaching Fold 100 by two paths: held at 100 from the note-on (A = B = Fold 100), and a morph ramp
// 0 -> 100 over 500 ms issued at 0.5 s (A = Fold 0, B = Fold 100, nothing else differs). The steady level and h1 of
// the Character tap (mid) over [3 s, 4 s), whole periods, and their differences (ramp - held) in dB, written to
// evidence/h29-fold-path.txt beside Phase 8's ~0.13 dB. Recorded, not gated: the readings are checked finite by the
// bit test only.
TEST_CASE("VoiceEngine_FoldPathRecord", "[systems][profundum][voice-engine][sc-011]") {
    enableFTZDAZ();
    MorphState fold0 = peStateAs();
    fold0.foldAmount = 0.0f;
    MorphState fold100 = peStateAs();
    fold100.foldAmount = 100.0f;
    REQUIRE(Krate::DSP::isValidMorphState(fold0));
    REQUIRE(Krate::DSP::isValidMorphState(fold100));
    const auto run = [](const MorphState& a, const MorphState& b, bool ramp) {
        PeScript s;
        s.fs = kPeFs;
        s.hostBlock = kPeHostBlock;
        s.totalSamples = peAt(4.0);
        s.a = a;
        s.b = b;
        s.position = 0.0f;
        s.mode = ProfundumEngine::VoiceMode::Mono;
        s.attachObserver = true;
        s.tappedVoices = 0;
        s.events.push_back(peOn(0, kPeNoteE1));
        if (ramp) {
            s.events.push_back(peSet(peAt(0.5), PeKind::MorphRampMs, 500.0f));
            s.events.push_back(peSet(peAt(0.5), PeKind::MorphPosition, 1.0f));
        }
        return PT::voiceEngineRender(s);
    };
    const PeTaps held = run(fold100, fold100, false);
    const PeTaps ramped = run(fold0, fold100, true);

    // Both paths end at the same state: the ramp has arrived and the law hands Fold 100 in both.
    REQUIRE(!held.grid.empty());
    REQUIRE(!ramped.grid.empty());
    CHECK(peSameBits(ramped.grid.back().resolved.position, 1.0f));
    CHECK(peSameBits(held.grid.back().targets.foldAmount, ramped.grid.back().targets.foldAmount));
    CHECK(peSameBits(held.grid.back().targets.foldAmount, 100.0f));

    const double f0 = static_cast<double>(Krate::DSP::midiNoteToFrequency(static_cast<int>(kPeNoteE1)));
    const std::size_t start = peSamples(3.0);
    const std::size_t len = peSamples(1.0);
    const PeLevelH1 h = peLevelH1(held.characterL, held.characterR, start, len, f0);
    const PeLevelH1 r = peLevelH1(ramped.characterL, ramped.characterR, start, len, f0);
    const double dLevel = r.levelDb - h.levelDb;
    const double dH1 = r.h1Db - h.h1Db;

    std::ostringstream out;
    out << std::setprecision(6);
    out << "# H-29 VoiceEngine_FoldPathRecord (FR-070 H-29 Record; plan S10.6; tasks.md T014). Recorded, not gated.\n"
        << "# One held E1 (f0 " << f0 << " Hz) on A_s, MONO, 48 kHz, block 64. Path HELD: Fold 100 from the note-on. "
           "Path RAMP: morph 0 -> 1 (Fold 0 -> 100) over 500 ms issued at 0.5 s. Character tap mid, whole periods "
           "in [3 s, 4 s).\n"
        << "H29 HELD level_db=" << h.levelDb << " h1_db=" << h.h1Db << "\n"
        << "H29 RAMP level_db=" << r.levelDb << " h1_db=" << r.h1Db << "\n"
        << "H29 DIFF (ramp - held) level_db=" << dLevel << " h1_db=" << dH1
        << " | Phase 8 reference: ~0.13 dB (spec H-29, Phase 8 handoff 9.12)\n";
    std::cout << out.str();
    peWriteEvidence("h29-fold-path.txt", out.str());
    for (const double x : {h.levelDb, h.h1Db, r.levelDb, r.h1Db, dLevel, dH1})
        CHECK(peFinite(static_cast<float>(x)));
}

// Phase 11 FR-074 (user ruling 2026-10-22, SC-011 close finding): an idle voice's first note must attack with the
// amp-envelope values of the interval it starts in, not the ones the voice last applied. ProfundumVoice gated the
// envelope in applyTransition (at the note event), ahead of this interval's amp setters (step 10); the ADSR's table
// path latches the attack increment in enterAttack, so the first note played the stale attack time (here the 10 ms
// default against a 500 ms state set while the voice was idle). The fix defers the gate like G14-3's Drop trigger.
TEST_CASE("VoiceEngine_IdleVoiceFirstNoteUsesCurrentAmpEnvelope", "[systems][profundum][voice-engine][fr-074]") {
    enableFTZDAZ();
    MorphState slow{};
    slow.ampEnv.attackMs = 500.0f;
    constexpr double fs = 48000.0;
    constexpr std::size_t kSilence = 9600;   // 200 ms
    constexpr std::size_t kNote = 14400;     // 300 ms after the note-on
    auto render = [&](bool changedWhileIdle) {
        auto e = std::make_unique<ProfundumEngine>();
        if (!changedWhileIdle) {
            REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, slow));
            REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, slow));
        }
        e->prepare(fs);
        std::vector<float> l(kSilence), r(kSilence);
        e->processBlock(l.data(), r.data(), kSilence / 2);
        if (changedWhileIdle) {   // the voice was reset with the 10 ms default and has not rendered since
            REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, slow));
            REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, slow));
        }
        e->processBlock(l.data(), r.data(), kSilence / 2);
        e->noteOn(kPeNoteE1, 100);
        std::vector<float> nl(kNote), nr(kNote);
        e->processBlock(nl.data(), nr.data(), kNote);
        double sum = 0.0;
        for (std::size_t i = 0; i < 4800; ++i)   // the first 100 ms: deep inside a 500 ms attack
            sum += static_cast<double>(nl[i]) * nl[i];
        return 10.0 * std::log10(sum / 4800.0 + 1e-30);
    };
    const double fresh = render(false);
    const double changed = render(true);
    CAPTURE(fresh, changed);
    CHECK(std::abs(changed - fresh) <= 0.1);
}

// Phase 11 FR-074 (second clause): a runtime blob restored before any block has rendered since prepare / reset lands
// settled. The engine prepared at the registered position resolves the discrete selections by the hysteresis rule (A
// inside the band); the restored blob installs B, and without a re-snap the voices and post stages met the first note
// mid-transition (the restored instance of Phase 11 SC-011 read 0.37 dB from the original's continued render).
TEST_CASE("VoiceEngine_RestoreBeforeFirstBlockLandsSettled", "[systems][profundum][voice-engine][fr-074]") {
    enableFTZDAZ();
    auto [a, b] = Krate::DSP::BassMorphTest::algorithmCrossingPair();
    // Every discrete group differs: Character (the pair), image mode and the Anchor waveform.
    a.field.mode = Krate::DSP::BassStereoField::ImageMode::Solid;
    b.field.mode = Krate::DSP::BassStereoField::ImageMode::Wide;
    a.field.spread = 1.0f;   // the image mode is audible only with width to give
    b.field.spread = 1.0f;
    a.anchor.waveform = Krate::DSP::AnchorOscillator::Waveform::Sine;
    b.anchor.waveform = Krate::DSP::AnchorOscillator::Waveform::Triangle;
    REQUIRE(Krate::DSP::isValidMorphState(a));
    REQUIRE(Krate::DSP::isValidMorphState(b));
    constexpr double fs = 48000.0;
    constexpr std::size_t kNote = 28800;   // 600 ms after the note-on
    auto noteDb = [](ProfundumEngine& e) {
        std::vector<float> l(kNote), r(kNote);
        e.noteOn(kPeNoteE1, 100);
        e.processBlock(l.data(), r.data(), kNote);
        double sum = 0.0;
        for (std::size_t i = 0; i < 9600; ++i)   // the first 200 ms: attack and the transition window
            sum += 0.25 * (static_cast<double>(l[i]) - r[i]) * (static_cast<double>(l[i]) - r[i]);   // side energy
        return 10.0 * std::log10(sum / 9600.0 + 1e-30);
    };
    // The original: at 1 (every selection B), then 0.5 inside the band (B kept), settled in silence.
    auto src = std::make_unique<ProfundumEngine>();
    REQUIRE(src->setEndpoint(BassMorphEngine::Endpoint::A, a));
    REQUIRE(src->setEndpoint(BassMorphEngine::Endpoint::B, b));
    src->setMacro(Krate::DSP::ProfundumMacro::TightLoose, 0.8f);   // the SC-011 reader: the envelope / transient / compression rows
    src->setMorphPosition(1.0f);
    src->prepare(fs);
    std::vector<float> l(96000), r(96000);
    src->processBlock(l.data(), r.data(), 24000);
    src->setMorphPosition(0.5f);
    src->processBlock(l.data(), r.data(), 96000);
    std::array<std::byte, BassMorphEngine::kMorphRuntimeBytes> blob{};
    REQUIRE(src->serializeMorphRuntime(blob.data(), blob.size()) == blob.size());
    REQUIRE((std::to_integer<unsigned>(blob[18]) & 1u) == 1u);   // Character selection B
    // The restored engine: prepared at 0.5 (Character selection A by the band rule), the blob before any block.
    auto dst = std::make_unique<ProfundumEngine>();
    REQUIRE(dst->setEndpoint(BassMorphEngine::Endpoint::A, a));
    REQUIRE(dst->setEndpoint(BassMorphEngine::Endpoint::B, b));
    dst->setMacro(Krate::DSP::ProfundumMacro::TightLoose, 0.8f);
    dst->setMorphPosition(0.5f);
    dst->prepare(fs);
    std::array<std::byte, BassMorphEngine::kMorphRuntimeBytes> pre{};
    REQUIRE(dst->serializeMorphRuntime(pre.data(), pre.size()) == pre.size());
    CAPTURE(std::to_integer<unsigned>(pre[18]), std::to_integer<unsigned>(blob[18]));
    CHECK((std::to_integer<unsigned>(pre[18]) & 1u) == 0u);   // teeth: the restore changes the Character selection
    REQUIRE(dst->restoreMorphRuntime(blob.data(), blob.size()));
    const double srcDb = noteDb(*src);
    const double dstDb = noteDb(*dst);
    CAPTURE(srcDb, dstDb);
    CHECK(std::abs(srcDb - dstDb) <= 0.1);
}
