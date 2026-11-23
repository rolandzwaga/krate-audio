// ==============================================================================
// Modulation regression tests (audit fix plan T6.5)
// ==============================================================================
// Promoted from the audit probes (specs/ruinae-audit/probes/audit_probes.cpp.txt,
// "11. Modulation"): LFO1 reaches the global and the all-voice filter cutoff at
// full swing and 2 Hz, nothing is left over once the amount is zero, and every
// modulation source stays finite and under the limiter.
// ==============================================================================

#include "audit_rig.h"

#include <krate/dsp/core/db_utils.h>
#include "ruinae_types.h"

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

int64_t nonFiniteAll(const Rig& r) {
    int64_t n = 0;
    for (const float v : r.L)
        if (!Krate::DSP::detail::isFinite(v)) ++n;
    for (const float v : r.R)
        if (!Krate::DSP::detail::isFinite(v)) ++n;
    return n;
}

}  // namespace

TEST_CASE("Mod: LFO1 to cutoff swings +-1 at 2 Hz and leaves no stale offset at amount 0",
          "[modmatrix][regression]") {
    const double sr = 48000.0;
    for (const int destIdx : {0, 4}) {
        DYNAMIC_SECTION("LFO1 -> " << (destIdx == 0 ? "GlobalFilterCutoff" : "AllVoiceFilterCutoff")) {
            Rig r(sr);
            basePatch(r);
            r.param(Ruinae::kGlobalFilterEnabledId, 1.0);
            r.param(Ruinae::kGlobalFilterCutoffId, 0.5);
            r.param(Ruinae::kLFO1RateId, std::log(2.0 / 0.01) / std::log(5000.0));  // 2 Hz
            r.param(Ruinae::kLFO1DepthId, 1.0);
            r.param(Ruinae::kLFO1SyncId, 0.0);
            r.param(Ruinae::kLFO1ShapeId, 0.0);
            r.param(Ruinae::kModMatrixSlot0SourceId, 1.0 / 14.0);
            r.param(Ruinae::kModMatrixSlot0DestId, destIdx / 14.0);
            r.param(Ruinae::kModMatrixSlot0AmountId, 1.0);
            r.noteOn(48, 0.8f, 0);
            const int64_t zeroAt = r.at(4.0);
            r.param(Ruinae::kModMatrixSlot0AmountId, 0.5, zeroAt);  // 0.5 normalised = amount 0
            const auto dest = destIdx == 0 ? Krate::DSP::RuinaeModDest::GlobalFilterCutoff
                                           : Krate::DSP::RuinaeModDest::AllVoiceFilterCutoff;
            std::vector<std::pair<int64_t, float>> offs;
            r.onBlock = [&](int64_t s, Steinberg::int32 len) {
                offs.emplace_back(s + len, r.proc->engine().getGlobalModOffset(dest));
            };
            r.render(r.at(6.0), 64);

            double mn = 1e9, mx = -1e9, staleMax = 0;
            std::vector<double> upCross;  // interpolated times (s) of upward zero crossings
            bool havePrev = false;
            std::pair<int64_t, float> prev{0, 0.0f};
            for (const auto& o : offs) {
                if (o.first < zeroAt && o.first > r.at(0.5)) {
                    mn = std::min(mn, static_cast<double>(o.second));
                    mx = std::max(mx, static_cast<double>(o.second));
                    if (havePrev && prev.second < 0.0f && o.second >= 0.0f) {
                        const double f = -static_cast<double>(prev.second) /
                                         (static_cast<double>(o.second) - static_cast<double>(prev.second));
                        upCross.push_back((static_cast<double>(prev.first) +
                                           f * static_cast<double>(o.first - prev.first)) / sr);
                    }
                }
                if (o.first > zeroAt + r.at(0.5))
                    staleMax = std::max(staleMax, static_cast<double>(std::fabs(o.second)));
                prev = o;
                havePrev = true;
            }
            REQUIRE(upCross.size() >= 4);
            const double hz = static_cast<double>(upCross.size() - 1) / (upCross.back() - upCross.front());
            INFO("offset min " << mn << " max " << mx << " frequency " << hz << " Hz stale max " << staleMax);
            CHECK(mx == Catch::Approx(1.0).margin(0.001));
            CHECK(mn == Catch::Approx(-1.0).margin(0.001));
            CHECK(hz == Catch::Approx(2.0).margin(0.01));
            CHECK(staleMax <= 1e-4);
            CHECK(nonFiniteAll(r) == 0);
        }
    }
}

TEST_CASE("Mod: every source stays finite and under 0 dBFS", "[modmatrix][regression]") {
    const double sr = 48000.0;
    const char* srcNames[15] = {"None",   "LFO1",   "LFO2",    "EnvFollower", "Random",
                                "Macro1", "Macro2", "Macro3",  "Macro4",      "Chaos",
                                "Rungler", "SampleHold", "PitchFollower", "Transient", "ArpExternal0"};
    for (int s = 1; s <= 14; ++s) {
        DYNAMIC_SECTION("source " << srcNames[s]) {
            Rig r(sr);
            basePatch(r);
            r.param(Ruinae::kGlobalFilterEnabledId, 1.0);
            r.param(Ruinae::kGlobalFilterCutoffId, 0.6);
            r.param(Ruinae::kGlobalFilterResonanceId, 0.6);
            r.param(Ruinae::kMacro1ValueId, 1.0);
            r.param(Ruinae::kMacro2ValueId, 1.0);
            r.param(Ruinae::kMacro3ValueId, 1.0);
            r.param(Ruinae::kMacro4ValueId, 1.0);
            r.param(Ruinae::kRunglerDepthId, 1.0);
            r.param(Ruinae::kModMatrixSlot0SourceId, s / 14.0);
            r.param(Ruinae::kModMatrixSlot0DestId, 0.0);
            r.param(Ruinae::kModMatrixSlot0AmountId, 1.0);
            r.param(Ruinae::kModMatrixSlot1SourceId, s / 14.0);
            r.param(Ruinae::kModMatrixSlot1DestId, 4.0 / 14.0);
            r.param(Ruinae::kModMatrixSlot1AmountId, 1.0);
            r.param(Ruinae::kModMatrixSlot2SourceId, s / 14.0);
            r.param(Ruinae::kModMatrixSlot2DestId, 5.0 / 14.0);
            r.param(Ruinae::kModMatrixSlot2AmountId, 0.0);
            r.param(Ruinae::kModMatrixSlot3SourceId, s / 14.0);
            r.param(Ruinae::kModMatrixSlot3DestId, 8.0 / 14.0);
            r.param(Ruinae::kModMatrixSlot3AmountId, 1.0);
            r.noteOn(48, 0.8f, 0);
            r.noteOn(55, 0.8f, r.at(0.3));
            r.noteOff(48, r.at(0.8));
            r.render(r.at(1.5));

            double peak = 0.0;
            for (const float v : r.mono())
                if (Krate::DSP::detail::isFinite(v)) peak = std::max(peak, static_cast<double>(std::fabs(v)));
            INFO("peak " << peak);
            CHECK(nonFiniteAll(r) == 0);
            CHECK(peak <= 1.0);
        }
    }
}
