// ==============================================================================
// Profundum - main-page audibility rows (Phase 12 T039; SC-006 (a), ruled 2026-10-24)
// ==============================================================================
// Profundum_MainPageAudibility: the five Main IDs D3 found without a gated row (1, 200, 201, 204, 1000), each through
//   the processor path only (ppRenderProcessor, param_path_audibility_helpers.h): the base endpoints are loaded through
//   setState, every value is a normalised parameter change, the descriptor is read on the Sum tap over the held
//   window [1.0 s, 2.0 s), at E1 and C3, 48 kHz, host block 64. Bars are the Phase 9 descriptor families' own:
//   E_sub >= 3 dB (1), D_cx >= 3 dB (200, 201), D_br >= 0.25 oct (204), the AT-10 anchor-level E_sub >= 2 dB (1000).
//   1001 Edit Target is exempt (routing control; its audibility is AT-41 / AT-42).
// ==============================================================================

#include "param_path_audibility_helpers.h"
#include "profundum_test_fixture.h"

#include "parameters/morph_params.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace P = ::Profundum;

constexpr double kMpReadFromS = 1.0;
constexpr double kMpReadToS = 2.0;
constexpr double kMpPolySecondOnS = 1.0;
constexpr double kMpPolyReadFromS = 1.25;
constexpr double kMpESubBarDb = 3.0;      // E_sub family (AT-47 / AT-26)
constexpr double kMpDcxBarDb = 3.0;       // D_cx family (AT-14 - AT-17)
constexpr double kMpDbrBarOct = 0.25;     // D_br family (AT-48)
constexpr double kMpAnchorBarDb = 2.0;    // AT-10 anchor-level E_sub

[[nodiscard]] std::size_t mpSample(double seconds) { return static_cast<std::size_t>(auAt(seconds)); }

/// The AT-02 base written through the host: kSawAnchor with Edge 0.5, one control overridden.
[[nodiscard]] std::vector<PpParam> mpSawBase(Steinberg::Vst::ParamID id, double plain) {
    std::vector<PpParam> params = ppBaseParams(PpBase::AsSawEdgeHalf);
    for (PpParam& p : params) {
        if (p.id == id) {
            p.norm = ppNorm(id, plain);
        }
    }
    return params;
}

[[nodiscard]] auPt::VoiceEngineTaps mpRender(std::uint8_t note, const Kd::MorphState& a, const Kd::MorphState& b,
                                             std::vector<PpParam> params) {
    const PpArm arm = ppArm(auOne(auStateAs()), note, auAt(kMpReadToS), 0, a, b, std::move(params));
    return ppRender(PpPath::Processor, arm, false);
}

[[nodiscard]] double mpMeanFrames(const auPt::VoiceEngineTaps& t, double f0, AuFrameDesc d) {
    const std::vector<double> v =
        auFrameSeries(t.sumL, t.sumR, f0, mpSample(kMpReadFromS), mpSample(kMpReadToS), d);
    REQUIRE(!v.empty());
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

[[nodiscard]] double mpESub(const auPt::VoiceEngineTaps& t, double f0, double fromS) {
    const std::size_t from = mpSample(fromS);
    return auWindowESubDb(t.sumL, t.sumR, from, mpSample(kMpReadToS) - from, f0);
}

void mpReport(const char* id, std::uint8_t note, double lo, double hi, double value, double bar) {
    std::cout << "Profundum_MainPageAudibility " << id << " " << auNoteName(note) << ": lo " << lo << " hi " << hi
              << " value " << value << " bar " << bar << '\n';
    INFO(id << " " << auNoteName(note) << ": lo " << lo << ", hi " << hi << ", |diff| " << value << ", bar " << bar);
    CHECK(auFinite(value));
    CHECK(value >= bar);
}

/// A recipe lever on the AT-02 base: lo = control at 0, hi = control at 1, the mean frame descriptor's |difference|.
void mpRecipeRow(const char* id, Steinberg::Vst::ParamID param, AuFrameDesc d, double bar) {
    for (const std::uint8_t note : {kAuE1, kAuC3}) {
        const double f0 = auHz(note);
        const double lo = mpMeanFrames(mpRender(note, auStateAs(), auStateAs(), mpSawBase(param, 0.0)), f0, d);
        const double hi = mpMeanFrames(mpRender(note, auStateAs(), auStateAs(), mpSawBase(param, 1.0)), f0, d);
        mpReport(id, note, lo, hi, std::abs(hi - lo), bar);
    }
}

}  // namespace

TEST_CASE("Profundum_MainPageAudibility", "[profundum][audibility]") {
    SECTION("1 Voice Mode: Mono -> Poly, first note held, second added at 1.0 s (E_sub at the first f0, Sum)") {
        for (const std::uint8_t note : {kAuE1, kAuC3}) {
            const std::uint8_t second = note == kAuE1 ? kAuC3 : kAuC4;
            const double f0 = auHz(note);
            double reading[2] = {0.0, 0.0};
            for (const int poly : {0, 1}) {
                PpArm arm = ppArm(auOne(auStateAs()), note, auAt(kMpReadToS), 0, auStateAs(), auStateAs(),
                                  {{.id = P::kVoiceModeId, .norm = ppNorm(P::kVoiceModeId, poly)}});
                arm.direct.events.push_back(auOn(auAt(kMpPolySecondOnS), second));
                reading[poly] = mpESub(ppRender(PpPath::Processor, arm, false), f0, kMpPolyReadFromS);
            }
            mpReport("1", note, reading[0], reading[1], std::abs(reading[1] - reading[0]), kMpESubBarDb);
        }
    }
    SECTION("200 Depth: 0 -> 1 on kSawAnchor, Edge 0.5 (mean frame D_cx, Sum)") {
        mpRecipeRow("200", P::kCoreDepthId, AuFrameDesc::DCx, kMpDcxBarDb);
    }
    SECTION("201 Body: 0 -> 1 on kSawAnchor, Edge 0.5 (mean frame D_cx, Sum)") {
        mpRecipeRow("201", P::kCoreBodyId, AuFrameDesc::DCx, kMpDcxBarDb);
    }
    SECTION("204 Edge: 0 -> 1 on kSawAnchor (mean frame D_br, Sum)") {
        mpRecipeRow("204", P::kCoreEdgeId, AuFrameDesc::DBr, kMpDbrBarOct);
    }
    SECTION("1000 Anchor Morph Mode: Hold -> Blend at Morph Position 1, A Anchor Level 1, B Anchor Level 0 (E_sub, Sum)") {
        Kd::MorphState a = auStateAs();
        Kd::MorphState b = auStateAs();
        a.anchor.level = 1.0f;
        b.anchor.level = 0.0f;
        REQUIRE(Kd::isValidMorphState(a));
        REQUIRE(Kd::isValidMorphState(b));
        const double maxMode = static_cast<double>(P::kNumAnchorMorphModes - 1);
        for (const std::uint8_t note : {kAuE1, kAuC3}) {
            const double f0 = auHz(note);
            double reading[2] = {0.0, 0.0};
            for (const int mode : {0, 1}) {   // 0 Lock (Hold), 1 Blend
                reading[mode] = mpESub(mpRender(note, a, b,
                                                {{.id = P::kMorphPositionId, .norm = ppNorm(P::kMorphPositionId, 1.0)},
                                                 {.id = P::kMorphAnchorModeId, .norm = mode / maxMode}}),
                                       f0, kMpReadFromS);
            }
            mpReport("1000", note, reading[0], reading[1], std::abs(reading[1] - reading[0]), kMpAnchorBarDb);
        }
    }
}
