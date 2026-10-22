#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

// Profundum_TailFormula, Profundum_TailCoversRelease — written by T022 (specs/profundum-phase11-parameters/tasks.md)
//
// FR-060 / SC-017 (tasks.md deviation 2): the formula case is gated at every group; the 8 render arms are [long]
// and run first among the [long] cases by the main loop (plan R3). A non-zero sample past the reported tail is a
// failure reported to the user: the 1250 ms term is never re-fitted.

#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

using namespace ProfundumPluginTest;

namespace {

struct ReleasePair {
    float a;
    float b;
};

/// A default MorphState with the amp release set to @p releaseMs.
[[nodiscard]] Krate::DSP::MorphState stateWithRelease(float releaseMs) {
    Krate::DSP::MorphState s{};
    s.ampEnv.releaseMs = releaseMs;
    REQUIRE(Krate::DSP::isValidMorphState(s));
    return s;
}

/// Loads endpoints {A, B} = releases @p rel, Tight <-> Loose = @p tightLoose and Morph Position = @p position through
/// a v2 setState stream (the host surface).
void loadTailState(ProcessorFixture& fx, ReleasePair rel, float tightLoose, float position = 0.0f) {
    V2Fields f;
    f.macros = {0.5f, 0.5f, tightLoose, 0.5f};
    f.position = position;
    const StateBytes bytes =
        withFields(withEndpoints(writeV2Stream(*fx.proc), stateWithRelease(rel.a), stateWithRelease(rel.b)), f);
    REQUIRE(loadStateBytes(*fx.proc, bytes) == Steinberg::kResultOk);
    REQUIRE(std::bit_cast<float>(fx.proc->endpointSlotForTest(Krate::DSP::BassMorphEngine::Endpoint::A, 21)) ==
            rel.a);
    REQUIRE(std::bit_cast<float>(fx.proc->endpointSlotForTest(Krate::DSP::BassMorphEngine::Endpoint::B, 21)) ==
            rel.b);
}

/// The FR-060 formula for an effective release @p rEffMs at @p fs. The TU builds under /fp:fast on MSVC, which folds a
/// constant (rEffMs + 1250) * 0.001 into 1.35 and rounds 1.35 * fs one ulp above the integer, so ceil() read one
/// sample high; the quotient is therefore corrected against q * 1000, which is exact.
[[nodiscard]] Steinberg::uint32 formulaTail(double rEffMs, double fs) {
    const double x = (rEffMs + 1250.0) * fs;
    double q = std::ceil(x / 1000.0);
    if ((q - 1.0) * 1000.0 >= x) {
        q -= 1.0;
    } else if (q * 1000.0 < x) {
        q += 1.0;
    }
    return static_cast<Steinberg::uint32>(q) + 64u;
}

/// R_eff with Tight <-> Loose at @p tightLoose (others neutral), computed independently on a stack-local matrix.
[[nodiscard]] float effectiveRelease(float r, float tightLoose) {
    Krate::DSP::ProfundumMacroMatrix m;
    m.setMacros(Krate::DSP::ProfundumMacroValues{
        .weight = 0.5f, .darkBright = 0.5f, .tightLoose = tightLoose, .cleanViolent = 0.5f});
    const float c = m.contributions()[static_cast<std::size_t>(Krate::DSP::ProfundumMacroTarget::AmpReleaseOct)];
    if ((std::bit_cast<std::uint32_t>(c) & 0x7FFFFFFFu) == 0u) {
        return r;
    }
    return std::clamp(r * std::exp2(c), Krate::DSP::kMinEnvelopeTimeMs, Krate::DSP::kMaxEnvelopeTimeMs);
}

}  // namespace

TEST_CASE("Profundum_TailFormula", "[profundum][tail]") {
    SECTION("0 on an initialised, unprepared processor") {
        ProcessorFixture fx;
        CHECK(fx.proc->getTailSamples() == 0u);
    }

    SECTION("0 again after terminate") {
        ProcessorFixture fx;
        REQUIRE(fx.prepare(48000.0) == Steinberg::kResultOk);
        REQUIRE(fx.proc->getTailSamples() > 0u);
        REQUIRE(fx.proc->setActive(0u) == Steinberg::kResultOk);
        REQUIRE(fx.proc->terminate() == Steinberg::kResultOk);
        CHECK(fx.proc->getTailSamples() == 0u);
    }

    constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
    constexpr std::array<ReleasePair, 2> kPairs{ReleasePair{.a = 100.0f, .b = 5000.0f}, ReleasePair{.a = 5000.0f, .b = 100.0f}};

    SECTION("macros neutral: R_eff = max(A, B)") {
        for (const double fs : kRates) {
            for (const ReleasePair pair : kPairs) {
                ProcessorFixture fx;
                REQUIRE(fx.prepare(fs) == Steinberg::kResultOk);
                loadTailState(fx, pair, 0.5f);
                CAPTURE(fs, pair.a, pair.b);
                CHECK(fx.proc->getTailSamples() == formulaTail(5000.0, fs));
            }
        }
    }

    SECTION("Tight <-> Loose 1.0 lengthens the tail by the macro law") {
        for (const double fs : kRates) {
            CAPTURE(fs);
            // {100, 100}: the clamp does not bite.
            ProcessorFixture neutral;
            REQUIRE(neutral.prepare(fs) == Steinberg::kResultOk);
            loadTailState(neutral, ReleasePair{.a = 100.0f, .b = 100.0f}, 0.5f);
            const Steinberg::uint32 neutralTail = neutral.proc->getTailSamples();
            CHECK(neutralTail == formulaTail(100.0, fs));

            ProcessorFixture loose;
            REQUIRE(loose.prepare(fs) == Steinberg::kResultOk);
            loadTailState(loose, ReleasePair{.a = 100.0f, .b = 100.0f}, 1.0f);
            const float rEff = effectiveRelease(100.0f, 1.0f);
            CAPTURE(rEff);
            REQUIRE(rEff > 100.0f);
            REQUIRE(rEff < Krate::DSP::kMaxEnvelopeTimeMs);
            const Steinberg::uint32 looseTail = loose.proc->getTailSamples();
            CHECK(looseTail == formulaTail(static_cast<double>(rEff), fs));
            CHECK(looseTail > neutralTail);

            // {100, 5000} (spec SC-017 macro arm): the clamp to kMaxEnvelopeTimeMs bites.
            for (const ReleasePair pair : kPairs) {
                ProcessorFixture fx;
                REQUIRE(fx.prepare(fs) == Steinberg::kResultOk);
                loadTailState(fx, pair, 1.0f);
                const float rEffWide = effectiveRelease(5000.0f, 1.0f);
                CAPTURE(pair.a, pair.b, rEffWide);
                const Steinberg::uint32 tail = fx.proc->getTailSamples();
                CHECK(tail == formulaTail(static_cast<double>(rEffWide), fs));
                CHECK(tail > formulaTail(5000.0, fs));
            }
        }
    }
}

TEST_CASE("Profundum_TailCoversRelease", "[profundum][tail][long]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kHostBlock = 512;
    constexpr std::size_t kNoteOffAt = 24000;      // note held 0.5 s
    constexpr std::size_t kObserveAfter = 24000;   // 0.5 s observed beyond the reported tail

    constexpr std::array<float, 2> kTightLoose{0.5f, 1.0f};
    constexpr std::array<ReleasePair, 2> kPairs{ReleasePair{.a = 100.0f, .b = 5000.0f}, ReleasePair{.a = 5000.0f, .b = 100.0f}};
    constexpr std::array<float, 2> kPositions{0.0f, 1.0f};

    for (const float tl : kTightLoose) {
        for (const ReleasePair pair : kPairs) {
            for (const float position : kPositions) {
                CAPTURE(tl, pair.a, pair.b, position);
                ProcessorFixture fx;
                REQUIRE(fx.prepare(kFs) == Steinberg::kResultOk);
                loadTailState(fx, pair, tl, position);  // voice mode MONO (the V2Fields default)

                const Steinberg::uint32 tail = fx.proc->getTailSamples();
                REQUIRE(tail > 0u);
                const std::size_t silentFrom = kNoteOffAt + static_cast<std::size_t>(tail);
                const std::size_t total = silentFrom + kObserveAfter;

                const std::array<ProcessorFixture::ScriptedEvent, 2> script{
                    ProcessorFixture::ScriptedEvent{.sample = 0,
                                                    .kind = ProcessorFixture::ScriptedEvent::Kind::NoteOn,
                                                    .pitch = kE1,
                                                    .velocity = 0.8f},
                    ProcessorFixture::ScriptedEvent{.sample = static_cast<Steinberg::int64>(kNoteOffAt),
                                                    .kind = ProcessorFixture::ScriptedEvent::Kind::NoteOff,
                                                    .pitch = kE1}};
                fx.renderScript(std::span<const ProcessorFixture::ScriptedEvent>(script), total, kHostBlock);
                REQUIRE(fx.capturedL.size() == total);
                REQUIRE(fx.capturedR.size() == total);

                // Non-vacuity: the note sounded.
                REQUIRE(peakOf(std::span<const float>(fx.capturedL).first(kNoteOffAt)) > 0.0f);

                std::size_t firstNonZero = total;
                float firstValue = 0.0f;
                for (std::size_t i = silentFrom; i < total; ++i) {
                    if (fx.capturedL[i] != 0.0f || fx.capturedR[i] != 0.0f) {
                        firstNonZero = i;
                        firstValue = (fx.capturedL[i] != 0.0f) ? fx.capturedL[i] : fx.capturedR[i];
                        break;
                    }
                }
                INFO("tail " << tail << " samples; first non-zero index " << firstNonZero << " (offset "
                             << (firstNonZero - kNoteOffAt) << " after note-off), value " << firstValue);
                CHECK(firstNonZero == total);
            }
        }
    }
}
