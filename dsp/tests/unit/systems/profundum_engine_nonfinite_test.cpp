// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_engine_nonfinite_test.cpp
// ==============================================================================
// Holds: SC-023, FR-061 [nonfinite]. Filled by T012 (T001 registered this TU in the -fno-fast-math
// -fno-finite-math-only block of dsp/tests/CMakeLists.txt).
// Includes every Phase 9 header so each is compiled by every gate from Group 1 on.
//
//   VoiceEngine_NonFinite  every float setter of plan S6.2 (pitch bend, tempo, morph position, morph ramp ms,
//                          portamento time, bend range, ceiling, each macro through setMacro and through setMacros)
//                          and every F32 leaf of both endpoints through setEndpoint (the engine relies on
//                          BassMorphEngine's own validation, isValidMorphState): NaN (0x7FC00000), +Inf, -Inf and
//                          the denormal 0x00000001, issued mid-interval while E1 is held on B.
//     non-finite: the getter returns the previous value bitwise and the output is bitwise equal to a lockstep
//                 twin engine that never received the call (and finite);
//     denormal:   the getter returns the value the setter documents (clamp / keep / +0, computed here with the
//                 setter's own rule on this thread, so FTZ / DAZ reads the same on both sides) and the output stays
//                 finite.
//
// Every special value is built from its bit pattern through a volatile std::uint32_t and std::bit_cast<float>;
// finiteness is read only through the bit test (bits & 0x7F800000) != 0x7F800000. TU-local names carry the `pnf` /
// `Pnf` prefix inside an anonymous namespace.
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "bass_morph_state_helpers.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ios>
#include <memory>
#include <type_traits>

namespace {

using Krate::DSP::BassMorphEngine;
using Krate::DSP::MonoHandler;
using Krate::DSP::MorphState;
using Krate::DSP::ProfundumEngine;
using Krate::DSP::ProfundumMacro;
using Krate::DSP::ProfundumMacroValues;
namespace io = Krate::DSP::detail::morph_state_io;

constexpr double kPnfFs = 48000.0;
constexpr std::uint8_t kPnfNoteE1 = 28;
constexpr std::uint8_t kPnfVelocity = 100;
constexpr std::size_t kPnfWarmup = 4800;       // 100 ms: the note is held and audible before the first injection
constexpr std::size_t kPnfPre = 13;            // mid-interval: 13 is not a multiple of the 32-sample grid
constexpr std::size_t kPnfPost = 83;           // crosses at least two grid points, where a latched value applies
constexpr std::size_t kPnfChunk = 128;
constexpr float kPnfAudibleFloor = 1.0e-3f;    // -60 dBFS: the lockstep comparison is not against silence

// ---- special values (tasks.md T012) ----
constexpr std::uint32_t kPnfQuietNaN = 0x7FC00000u;
constexpr std::uint32_t kPnfPosInf = 0x7F800000u;
constexpr std::uint32_t kPnfNegInf = 0xFF800000u;
constexpr std::uint32_t kPnfDenormal = 0x00000001u;
constexpr std::array<std::uint32_t, 3> kPnfNonFiniteBits{kPnfQuietNaN, kPnfPosInf, kPnfNegInf};

/// A float from its bit pattern, through a volatile so no compiler folding can see it.
float pnfFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t v = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(v));
}

std::uint32_t pnfBits(float x) noexcept { return std::bit_cast<std::uint32_t>(x); }

bool pnfFinite(float x) noexcept { return (pnfBits(x) & 0x7F800000u) != 0x7F800000u; }

/// Plan S1 C-8: base "A" with ampEnv.sustain = 1 (A_s).
MorphState pnfStateAs() {
    MorphState s = Krate::DSP::BassMorphTest::showcasePair().first;
    s.ampEnv.sustain = 1.0f;
    return s;
}

/// Endpoints A_s / B, every float setter at a non-default value (so a reset-to-default would be caught by the
/// getter checks), morph position 1 (on B), prepare, E1 held, 100 ms rendered. Returns the warm-up peak.
std::unique_ptr<ProfundumEngine> pnfMakeHeldEngine(float& warmupPeak) {
    auto e = std::make_unique<ProfundumEngine>();
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, pnfStateAs()));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, Krate::DSP::BassMorphTest::showcasePair().second));
    e->setMorphPosition(1.0f);
    e->setMorphRampMs(37.0f);
    e->setPitchBend(0.25f);
    e->setTempoBpm(97.0f);
    e->setPortamentoTimeMs(23.0f);
    e->setPitchBendRange(7.0f);
    e->setOutputCeilingDb(-2.5f);
    e->setMacros(ProfundumMacroValues{.weight = 0.61f, .darkBright = 0.42f, .tightLoose = 0.57f, .cleanViolent = 0.38f});
    e->prepare(kPnfFs);
    e->noteOn(kPnfNoteE1, kPnfVelocity);

    std::array<float, kPnfChunk> l{};
    std::array<float, kPnfChunk> r{};
    warmupPeak = 0.0f;
    std::size_t done = 0;
    while (done < kPnfWarmup) {
        const std::size_t m = std::min(kPnfChunk, kPnfWarmup - done);
        e->processBlock(l.data(), r.data(), m);
        for (std::size_t i = 0; i < m; ++i) {
            REQUIRE(pnfFinite(l[i]));
            REQUIRE(pnfFinite(r[i]));
            warmupPeak = std::max({warmupPeak, std::fabs(l[i]), std::fabs(r[i])});
        }
        done += m;
    }
    return e;
}

/// Renders n samples on the reference and the test engine and counts bitwise L / R mismatches and non-finite
/// samples of the test engine.
struct PnfLockstep {
    std::size_t mismatches = 0;
    std::size_t nonFinite = 0;
};

void pnfRenderLockstep(ProfundumEngine& ref, ProfundumEngine& test, std::size_t n, PnfLockstep& acc) {
    REQUIRE(n <= kPnfChunk);
    std::array<float, kPnfChunk> rl{};
    std::array<float, kPnfChunk> rr{};
    std::array<float, kPnfChunk> tl{};
    std::array<float, kPnfChunk> tr{};
    ref.processBlock(rl.data(), rr.data(), n);
    test.processBlock(tl.data(), tr.data(), n);
    for (std::size_t i = 0; i < n; ++i) {
        if (pnfBits(rl[i]) != pnfBits(tl[i]) || pnfBits(rr[i]) != pnfBits(tr[i]))
            ++acc.mismatches;
        if (!pnfFinite(tl[i]) || !pnfFinite(tr[i]))
            ++acc.nonFinite;
    }
}

/// Renders n samples on one engine and counts non-finite samples.
std::size_t pnfRenderCountNonFinite(ProfundumEngine& e, std::size_t n) {
    REQUIRE(n <= kPnfChunk);
    std::array<float, kPnfChunk> l{};
    std::array<float, kPnfChunk> r{};
    e.processBlock(l.data(), r.data(), n);
    std::size_t bad = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!pnfFinite(l[i]) || !pnfFinite(r[i]))
            ++bad;
    }
    return bad;
}

/// One float setter of plan S6.2: how to issue it, how to read it back (FR-066), and the value its documented rule
/// gives for a finite input (used for the denormal arm).
struct PnfSetter {
    const char* name;
    void (*apply)(ProfundumEngine&, float);
    float (*read)(const ProfundumEngine&);
    float (*documented)(float previous, float value);
};

float pnfMacroField(const ProfundumMacroValues& v, std::size_t i) noexcept {
    switch (i) {
        case 0: return v.weight;
        case 1: return v.darkBright;
        case 2: return v.tightLoose;
        default: return v.cleanViolent;
    }
}

void pnfSetMacroField(ProfundumMacroValues& v, std::size_t i, float x) noexcept {
    switch (i) {
        case 0: v.weight = x; break;
        case 1: v.darkBright = x; break;
        case 2: v.tightLoose = x; break;
        default: v.cleanViolent = x; break;
    }
}

// Documented rules (read this session):
//  setPitchBend: non-finite keeps, clamp [-1, 1] (profundum_engine.h:283-287).
//  setTempoBpm: non-finite or !(bpm > 0) keeps (profundum_engine.h:290-294).
//  setMorphPosition -> BassMorphEngine::setPosition: non-finite keeps; p < kDenormalThreshold -> +0; p >= 1 -> 1
//    (bass_morph_engine.h:140-147).
//  setMorphRampMs -> BassMorphEngine::setPositionRampMs: non-finite keeps, clamp [5, 2000] (bass_morph_engine.h:155-163).
//  setPortamentoTimeMs: non-finite keeps, clamp [MonoHandler::kMin/kMaxPortamentoTimeMs] (profundum_engine.h:327-331).
//  setPitchBendRange: non-finite keeps, clamp [0, 24] (profundum_engine.h:349-353).
//  setOutputCeilingDb: non-finite keeps, clamp [-12, 0] (profundum_engine.h:355-359).
//  setMacro / setMacros -> ProfundumMacroMatrix: non-finite keeps, clamp [0, 1], field-wise
//    (profundum_macro_matrix.h:162-176).
template <ProfundumMacro M>
PnfSetter pnfMacroSetter(const char* name) {
    return PnfSetter{
        name,
        [](ProfundumEngine& e, float v) { e.setMacro(M, v); },
        [](const ProfundumEngine& e) { return e.macro(M); },
        [](float, float v) { return std::clamp(v, 0.0f, 1.0f); },
    };
}

template <std::size_t I>
PnfSetter pnfMacrosSetter(const char* name) {
    return PnfSetter{
        name,
        [](ProfundumEngine& e, float v) {
            ProfundumMacroValues m = e.macros();
            pnfSetMacroField(m, I, v);
            e.setMacros(m);
        },
        [](const ProfundumEngine& e) { return pnfMacroField(e.macros(), I); },
        [](float, float v) { return std::clamp(v, 0.0f, 1.0f); },
    };
}

std::array<PnfSetter, 15> pnfSetters() {
    return {{
        PnfSetter{"setPitchBend", [](ProfundumEngine& e, float v) { e.setPitchBend(v); },
                  [](const ProfundumEngine& e) { return e.pitchBend(); },
                  [](float, float v) { return std::clamp(v, -1.0f, 1.0f); }},
        PnfSetter{"setTempoBpm", [](ProfundumEngine& e, float v) { e.setTempoBpm(v); },
                  [](const ProfundumEngine& e) { return e.tempoBpm(); },
                  [](float prev, float v) { return (v > 0.0f) ? v : prev; }},
        PnfSetter{"setMorphPosition", [](ProfundumEngine& e, float v) { e.setMorphPosition(v); },
                  [](const ProfundumEngine& e) { return e.morphPosition(); },
                  [](float, float v) {
                      if (v < Krate::DSP::kDenormalThreshold)
                          return 0.0f;
                      return v >= 1.0f ? 1.0f : v;
                  }},
        PnfSetter{"setMorphRampMs", [](ProfundumEngine& e, float v) { e.setMorphRampMs(v); },
                  [](const ProfundumEngine& e) { return e.morphRampMs(); },
                  [](float, float v) {
                      return std::clamp(v, BassMorphEngine::kMinPositionRampMs, BassMorphEngine::kMaxPositionRampMs);
                  }},
        PnfSetter{"setPortamentoTimeMs", [](ProfundumEngine& e, float v) { e.setPortamentoTimeMs(v); },
                  [](const ProfundumEngine& e) { return e.portamentoTimeMs(); },
                  [](float, float v) {
                      return std::clamp(v, MonoHandler::kMinPortamentoTimeMs, MonoHandler::kMaxPortamentoTimeMs);
                  }},
        PnfSetter{"setPitchBendRange", [](ProfundumEngine& e, float v) { e.setPitchBendRange(v); },
                  [](const ProfundumEngine& e) { return e.pitchBendRange(); },
                  [](float, float v) { return std::clamp(v, 0.0f, ProfundumEngine::kMaxBendRangeSt); }},
        PnfSetter{"setOutputCeilingDb", [](ProfundumEngine& e, float v) { e.setOutputCeilingDb(v); },
                  [](const ProfundumEngine& e) { return e.outputCeilingDb(); },
                  [](float, float v) {
                      return std::clamp(v, ProfundumEngine::kMinCeilingDb, ProfundumEngine::kMaxCeilingDb);
                  }},
        pnfMacroSetter<ProfundumMacro::Weight>("setMacro(Weight)"),
        pnfMacroSetter<ProfundumMacro::DarkBright>("setMacro(DarkBright)"),
        pnfMacroSetter<ProfundumMacro::TightLoose>("setMacro(TightLoose)"),
        pnfMacroSetter<ProfundumMacro::CleanViolent>("setMacro(CleanViolent)"),
        pnfMacrosSetter<0>("setMacros(.weight)"),
        pnfMacrosSetter<1>("setMacros(.darkBright)"),
        pnfMacrosSetter<2>("setMacros(.tightLoose)"),
        pnfMacrosSetter<3>("setMacros(.cleanViolent)"),
    }};
}

/// @p s with F32 leaf @p index set to @p v (ported from bmWithFloatLeaf, bass_morph_nonfinite_test.cpp).
MorphState pnfWithFloatLeaf(MorphState s, std::size_t index, float v) {
    io::forEachMorphLeaf(
        [&]([[maybe_unused]] auto leaf, auto& x) {
            using T = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::is_same_v<T, float>) {
                if (decltype(leaf)::value == index) {
                    x = v;
                }
            }
        },
        s);
    return s;
}

constexpr std::array<BassMorphEngine::Endpoint, 2> kPnfEndpoints{BassMorphEngine::Endpoint::A,
                                                                 BassMorphEngine::Endpoint::B};

}  // namespace

TEST_CASE("VoiceEngine_NonFinite", "[systems][profundum][voice-engine][sc-023][fr-061][nonfinite]") {
    const std::array<PnfSetter, 15> setters = pnfSetters();

    SECTION("non-finite values keep the previous value and change no output sample") {
        float refPeak = 0.0f;
        float testPeak = 0.0f;
        const auto ref = pnfMakeHeldEngine(refPeak);
        const auto test = pnfMakeHeldEngine(testPeak);
        REQUIRE(refPeak > kPnfAudibleFloor);
        REQUIRE(pnfBits(refPeak) == pnfBits(testPeak));   // FR-062: the twins start bitwise equal

        std::size_t cases = 0;

        // ---- every float setter ----
        for (const PnfSetter& s : setters) {
            for (const std::uint32_t bits : kPnfNonFiniteBits) {
                INFO("setter " << s.name << " bits 0x" << std::hex << bits << std::dec);
                PnfLockstep acc{};
                pnfRenderLockstep(*ref, *test, kPnfPre, acc);
                const float before = s.read(*test);
                s.apply(*test, pnfFromBits(bits));
                CHECK(pnfBits(s.read(*test)) == pnfBits(before));
                pnfRenderLockstep(*ref, *test, kPnfPost, acc);
                CHECK(acc.nonFinite == 0);
                CHECK(acc.mismatches == 0);
                ++cases;
            }
        }

        // ---- every F32 leaf of both endpoints, through setEndpoint (BassMorphEngine validates) ----
        for (const BassMorphEngine::Endpoint ep : kPnfEndpoints) {
            for (std::size_t leaf = 0; leaf < Krate::DSP::kMorphLeafFieldCount; ++leaf) {
                if (io::kFieldTable[leaf].kind != MorphState::Kind::F32)
                    continue;
                for (const std::uint32_t bits : kPnfNonFiniteBits) {
                    INFO("endpoint " << (ep == BassMorphEngine::Endpoint::B ? "B" : "A") << " leaf " << leaf << " ("
                                     << io::kFieldTable[leaf].name << ") bits 0x" << std::hex << bits << std::dec);
                    PnfLockstep acc{};
                    pnfRenderLockstep(*ref, *test, kPnfPre, acc);
                    const MorphState before = test->getEndpoint(ep);
                    const MorphState candidate = pnfWithFloatLeaf(before, leaf, pnfFromBits(bits));
                    CHECK_FALSE(Krate::DSP::isValidMorphState(candidate));
                    CHECK_FALSE(test->setEndpoint(ep, candidate));
                    CHECK(Krate::DSP::BassMorphTest::bitEqual(test->getEndpoint(ep), before));
                    pnfRenderLockstep(*ref, *test, kPnfPost, acc);
                    CHECK(acc.nonFinite == 0);
                    CHECK(acc.mismatches == 0);
                    ++cases;
                }
            }
        }
        // 15 setters x 3 + (F32 leaves x 2 endpoints x 3): the loops ran.
        CHECK(cases > setters.size() * kPnfNonFiniteBits.size());
    }

    SECTION("a denormal is clamped or kept as each setter documents and the output stays finite") {
        float peak = 0.0f;
        const auto e = pnfMakeHeldEngine(peak);
        REQUIRE(peak > kPnfAudibleFloor);
        const float denormal = pnfFromBits(kPnfDenormal);
        REQUIRE(Krate::DSP::detail::isFinite(denormal));

        for (const PnfSetter& s : setters) {
            INFO("setter " << s.name);
            std::size_t bad = pnfRenderCountNonFinite(*e, kPnfPre);
            const float before = s.read(*e);
            const float expected = s.documented(before, denormal);
            s.apply(*e, denormal);
            CHECK(pnfBits(s.read(*e)) == pnfBits(expected));
            bad += pnfRenderCountNonFinite(*e, kPnfPost);
            CHECK(bad == 0);
        }

        // setEndpoint: stored iff isValidMorphState accepts it (bass_morph_engine.h:113-127); otherwise unchanged.
        for (const BassMorphEngine::Endpoint ep : kPnfEndpoints) {
            for (std::size_t leaf = 0; leaf < Krate::DSP::kMorphLeafFieldCount; ++leaf) {
                if (io::kFieldTable[leaf].kind != MorphState::Kind::F32)
                    continue;
                INFO("endpoint " << (ep == BassMorphEngine::Endpoint::B ? "B" : "A") << " leaf " << leaf << " ("
                                 << io::kFieldTable[leaf].name << ")");
                std::size_t bad = pnfRenderCountNonFinite(*e, kPnfPre);
                const MorphState before = e->getEndpoint(ep);
                const MorphState candidate = pnfWithFloatLeaf(before, leaf, denormal);
                const bool valid = Krate::DSP::isValidMorphState(candidate);
                CHECK(e->setEndpoint(ep, candidate) == valid);
                CHECK(Krate::DSP::BassMorphTest::bitEqual(e->getEndpoint(ep), valid ? candidate : before));
                bad += pnfRenderCountNonFinite(*e, kPnfPost);
                CHECK(bad == 0);
            }
        }
    }
}
