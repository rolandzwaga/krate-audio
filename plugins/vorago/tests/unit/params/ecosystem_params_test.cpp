// ==============================================================================
// Vorago Phase 12 - ecosystem parameter pack contract (T023)
// ==============================================================================
// The eight-SECTION pack contract of specs/vorago-phase12-parameters/tasks.md
// (Group 7 shared test shape) for the Ecosystem pack: N = 3, B = 4 (+ 8 v3 ext).
//   900 Ecosystem Depth, %, [0, 1], default 0.85, n0 0.85, linear.
// Phase 14 T016 (FR-072/FR-074):
//   901 Ecosystem Sync, "", [0, 0.5], default 0.0, n0 0.0, linear.
//   902 Ecosystem Self Affinity, "", [-2, 2], default -1.0, n0 0.25, linear.
//   v3 extension stream: float syncRate + float selfAffinity = 8 bytes.
// Fast-math-off TU (plugins/vorago/tests/CMakeLists.txt): injects NaN / +-Inf.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/ecosystem_engine.h>

#include "parameters/ecosystem_params.h"
#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kN = 3;                  // registered IDs
constexpr Steinberg::int64 kB = 4;     // v2 pack stream bytes (depth only)
constexpr Steinberg::int64 kBExt = 8;  // v3 extension stream bytes

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

std::uint32_t bitsOf(float f) { return std::bit_cast<std::uint32_t>(f); }

std::string toAsciiString(const Steinberg::Vst::TChar* s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

float bitsToFloat(std::uint32_t bits) {
    volatile std::uint32_t v = bits;  // defeat constant folding under any float model
    const std::uint32_t copy = v;
    float f = 0.0f;
    std::memcpy(&f, &copy, sizeof(f));
    return f;
}

void setNonDefaults(::Vorago::EcosystemParams& p) { p.depth.store(0.3f); }

std::vector<char> saveExtBytes(const ::Vorago::EcosystemParams& p) {
    auto s = makeStream();
    {
        Steinberg::IBStreamer w(s, kLittleEndian);
        ::Vorago::saveEcosystemParamsV3Ext(p, w);
    }
    const auto size = static_cast<std::size_t>(s->getSize());
    return {s->getData(), s->getData() + size};
}

Steinberg::IPtr<Steinberg::MemoryStream> twoFloatStream(float a, float b) {
    auto s = makeStream();
    {
        Steinberg::IBStreamer w(s, kLittleEndian);
        w.writeFloat(a);
        w.writeFloat(b);
    }
    rewindStream(*s);
    return s;
}

std::vector<char> saveBytes(const ::Vorago::EcosystemParams& p) {
    auto s = makeStream();
    {
        Steinberg::IBStreamer w(s, kLittleEndian);
        ::Vorago::saveEcosystemParams(p, w);
    }
    const auto size = static_cast<std::size_t>(s->getSize());
    return {s->getData(), s->getData() + size};
}

Steinberg::IPtr<Steinberg::MemoryStream> streamFrom(const std::vector<char>& bytes,
                                                    std::size_t count) {
    auto s = makeStream();
    if (count > 0) {
        Steinberg::int32 written = 0;
        REQUIRE(s->write(const_cast<char*>(bytes.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                         static_cast<Steinberg::int32>(count), &written) == Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(written, count));
    }
    rewindStream(*s);
    return s;
}

}  // namespace

TEST_CASE("Vorago_EcosystemParamsContract", "[vorago][params]") {
    using Catch::Approx;
    using namespace ::Vorago;
    using namespace Steinberg::Vst;

    SECTION("Registration") {
        ParameterContainer pc;
        registerEcosystemParams(pc);
        REQUIRE(pc.getParameterCount() == kN);

        Parameter* p = pc.getParameter(kEcosystemDepthId);
        REQUIRE(p != nullptr);
        const ParameterInfo& info = p->getInfo();
        REQUIRE(toAsciiString(info.title) == "Ecosystem Depth");
        REQUIRE(toAsciiString(info.units) == "%");
        REQUIRE(info.stepCount == 0);
        REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
        REQUIRE((info.flags & ParameterInfo::kIsList) == 0);
        REQUIRE(info.defaultNormalizedValue == Approx(0.85).margin(1e-9));

        // Phase 14 FR-072: the two roster rule knobs.
        const auto engine = std::make_unique<Krate::DSP::EcosystemEngine>();
        using Kind = Krate::DSP::EcosystemEngine::Kind;

        Parameter* ps = pc.getParameter(kEcosystemSyncRateId);
        REQUIRE(ps != nullptr);
        const ParameterInfo& si = ps->getInfo();
        REQUIRE(toAsciiString(si.title) == "Ecosystem Sync");
        REQUIRE(toAsciiString(si.units).empty());
        REQUIRE(si.stepCount == 0);
        REQUIRE((si.flags & ParameterInfo::kCanAutomate) != 0);
        REQUIRE((si.flags & ParameterInfo::kIsList) == 0);
        REQUIRE(si.defaultNormalizedValue == Approx(0.0).margin(1e-9));
        const double syncPlain = linearFromNormalized(si.defaultNormalizedValue,
                                                      kEcosystemSyncRateMin, kEcosystemSyncRateMax);
        REQUIRE(syncPlain == Approx(kEcosystemSyncRateDefault).margin(1e-9));
        REQUIRE(static_cast<float>(syncPlain) == engine->getSyncRate());
        REQUIRE(engine->getSyncRate() == 0.0f);

        Parameter* pa = pc.getParameter(kEcosystemSelfAffinityId);
        REQUIRE(pa != nullptr);
        const ParameterInfo& ai = pa->getInfo();
        REQUIRE(toAsciiString(ai.title) == "Ecosystem Self Affinity");
        REQUIRE(toAsciiString(ai.units).empty());
        REQUIRE(ai.stepCount == 0);
        REQUIRE((ai.flags & ParameterInfo::kCanAutomate) != 0);
        REQUIRE((ai.flags & ParameterInfo::kIsList) == 0);
        REQUIRE(ai.defaultNormalizedValue == Approx(0.25).margin(1e-9));
        const double affPlain = linearFromNormalized(
            ai.defaultNormalizedValue, kEcosystemSelfAffinityMin, kEcosystemSelfAffinityMax);
        REQUIRE(affPlain == Approx(kEcosystemSelfAffinityDefault).margin(1e-9));
        for (const Kind k : {Kind::Partial, Kind::Resonator, Kind::Noise, Kind::Feedback,
                             Kind::Ghost}) {
            REQUIRE(engine->getAffinity(k, k) == static_cast<float>(affPlain));
            REQUIRE(engine->getAffinity(k, k) == -1.0f);
        }
    }

    SECTION("DefaultsDenormalizeToPlain") {
        const EcosystemParams fresh;
        REQUIRE(fresh.depth.load() == Approx(0.85f).epsilon(1e-6));
        REQUIRE(fresh.syncRate.load() == 0.0f);
        REQUIRE(fresh.selfAffinity.load() == -1.0f);

        EcosystemParams p;
        p.depth.store(0.1f);
        handleEcosystemParamChange(p, kEcosystemDepthId, 0.85);
        REQUIRE(p.depth.load() == Approx(fresh.depth.load()).epsilon(1e-6));

        handleEcosystemParamChange(p, kEcosystemDepthId, 0.0);
        REQUIRE(p.depth.load() == 0.0f);
        handleEcosystemParamChange(p, kEcosystemDepthId, 1.0);
        REQUIRE(p.depth.load() == 1.0f);
    }

    SECTION("UnregisteredInBandIgnored") {
        constexpr std::array<ParamID, 3> kUnused{903, 950, kEcosystemParamRangeEnd - 1};
        EcosystemParams p;
        p.depth.store(0.42f);
        p.syncRate.store(0.13f);
        p.selfAffinity.store(0.7f);
        const std::uint32_t before = bitsOf(p.depth.load());
        const std::uint32_t beforeSync = bitsOf(p.syncRate.load());
        const std::uint32_t beforeAff = bitsOf(p.selfAffinity.load());
        for (const ParamID id : kUnused) {
            for (const double v : {0.0, 0.5, 1.0}) {
                handleEcosystemParamChange(p, id, v);
                REQUIRE(bitsOf(p.depth.load()) == before);
                REQUIRE(bitsOf(p.syncRate.load()) == beforeSync);
                REQUIRE(bitsOf(p.selfAffinity.load()) == beforeAff);
            }
        }
    }

    SECTION("SaveLoadRoundTrip") {
        EcosystemParams src;
        setNonDefaults(src);
        const auto bytes = saveBytes(src);
        REQUIRE(static_cast<Steinberg::int64>(bytes.size()) == kB);

        auto s = streamFrom(bytes, bytes.size());
        EcosystemParams dst;
        Steinberg::IBStreamer r(s, kLittleEndian);
        REQUIRE(loadEcosystemParams(dst, r));
        REQUIRE(bitsOf(dst.depth.load()) == bitsOf(src.depth.load()));
    }

    SECTION("TruncationEveryOffset") {
        EcosystemParams src;
        setNonDefaults(src);
        const auto bytes = saveBytes(src);
        REQUIRE(static_cast<Steinberg::int64>(bytes.size()) == kB);

        for (std::size_t c = 0; c < bytes.size(); ++c) {
            auto s = streamFrom(bytes, c);
            EcosystemParams dst;
            dst.depth.store(0.77f);  // pre-dirtied
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE_FALSE(loadEcosystemParams(dst, r));
            // The only field spans [0, 4), never fully contained in [0, c): unchanged.
            REQUIRE(bitsOf(dst.depth.load()) == bitsOf(0.77f));
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<std::uint32_t, 3> kBadBits{0x7FC00000u, 0x7F800000u, 0xFF800000u};
        for (const std::uint32_t bad : kBadBits) {
            auto s = makeStream();
            {
                Steinberg::IBStreamer w(s, kLittleEndian);
                w.writeFloat(bitsToFloat(bad));
            }
            rewindStream(*s);
            EcosystemParams dst;
            dst.depth.store(0.61f);
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadEcosystemParams(dst, r));  // read succeeded; value rejected
            REQUIRE(bitsOf(dst.depth.load()) == bitsOf(0.61f));
        }

        const std::array<std::pair<float, float>, 2> kClamp{{{1.5f, 1.0f}, {-0.5f, 0.0f}}};
        for (const auto& [in, expected] : kClamp) {
            auto s = makeStream();
            {
                Steinberg::IBStreamer w(s, kLittleEndian);
                w.writeFloat(in);
            }
            rewindStream(*s);
            EcosystemParams dst;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadEcosystemParams(dst, r));
            REQUIRE(bitsOf(dst.depth.load()) == bitsOf(expected));
        }
    }

    SECTION("ControllerMirror") {
        EcosystemParams src;
        setNonDefaults(src);
        const auto bytes = saveBytes(src);
        auto s = streamFrom(bytes, bytes.size());

        std::vector<std::pair<ParamID, double>> calls;
        Steinberg::IBStreamer r(s, kLittleEndian);
        loadEcosystemParamsToController(
            r, [&calls](ParamID id, ParamValue v) { calls.emplace_back(id, v); });

        REQUIRE(calls.size() == 1u);  // the v2 pack carries depth only
        REQUIRE(calls[0].first == kEcosystemDepthId);
        REQUIRE(calls[0].second ==
                Approx(linearToNormalized(static_cast<double>(src.depth.load()), 0.0, 1.0))
                    .margin(1e-9));
    }

    SECTION("FormatNonEmpty") {
        for (const double v : {0.0, 0.5, 1.0}) {
            String128 str{};
            REQUIRE(formatEcosystemParam(kEcosystemDepthId, v, str) == Steinberg::kResultOk);
            REQUIRE_FALSE(toAsciiString(str).empty());
        }
        String128 str{};
        REQUIRE(formatEcosystemParam(kEcosystemSelfAffinityId + 1, 0.5, str) ==
                Steinberg::kResultFalse);
    }

    // ==========================================================================
    // Phase 14 T016 (FR-072 / FR-074): 901 Ecosystem Sync, 902 Self Affinity
    // ==========================================================================

    SECTION("RosterKnobHandler") {
        EcosystemParams p;
        handleEcosystemParamChange(p, kEcosystemSyncRateId, 1.0);
        REQUIRE(p.syncRate.load() == 0.5f);
        handleEcosystemParamChange(p, kEcosystemSyncRateId, 0.0);
        REQUIRE(p.syncRate.load() == 0.0f);
        handleEcosystemParamChange(p, kEcosystemSelfAffinityId, 0.0);
        REQUIRE(p.selfAffinity.load() == -2.0f);
        handleEcosystemParamChange(p, kEcosystemSelfAffinityId, 1.0);
        REQUIRE(p.selfAffinity.load() == 2.0f);
        handleEcosystemParamChange(p, kEcosystemSelfAffinityId, 0.25);
        REQUIRE(p.selfAffinity.load() == -1.0f);
        // Each case writes only its own field.
        REQUIRE(p.depth.load() == Approx(0.85f).epsilon(1e-6));
    }

    SECTION("RosterNormalizedPlainRoundTrip") {
        for (const double n : {0.0, 0.3, 1.0}) {
            EcosystemParams p;
            handleEcosystemParamChange(p, kEcosystemSyncRateId, n);
            handleEcosystemParamChange(p, kEcosystemSelfAffinityId, n);
            const double syncBack =
                linearToNormalized(static_cast<double>(p.syncRate.load()), kEcosystemSyncRateMin,
                                   kEcosystemSyncRateMax);
            const double affBack =
                linearToNormalized(static_cast<double>(p.selfAffinity.load()),
                                   kEcosystemSelfAffinityMin, kEcosystemSelfAffinityMax);
            REQUIRE(syncBack == Approx(n).margin(1e-6));
            REQUIRE(affBack == Approx(n).margin(1e-6));
        }
    }

    SECTION("V3ExtSaveLoadRoundTrip") {
        EcosystemParams src;
        src.syncRate.store(0.4f);
        src.selfAffinity.store(1.2f);
        const auto bytes = saveExtBytes(src);
        REQUIRE(static_cast<Steinberg::int64>(bytes.size()) == kBExt);

        auto s = streamFrom(bytes, bytes.size());
        EcosystemParams dst;
        dst.depth.store(0.33f);
        Steinberg::IBStreamer r(s, kLittleEndian);
        REQUIRE(loadEcosystemParamsV3Ext(dst, r));
        REQUIRE(bitsOf(dst.syncRate.load()) == bitsOf(0.4f));
        REQUIRE(bitsOf(dst.selfAffinity.load()) == bitsOf(1.2f));
        REQUIRE(bitsOf(dst.depth.load()) == bitsOf(0.33f));  // v2 field untouched
    }

    SECTION("V3ExtNonFiniteRejected") {
        const std::array<std::uint32_t, 3> kBadBits{0x7FC00000u, 0x7F800000u, 0xFF800000u};
        for (const std::uint32_t bad : kBadBits) {
            {   // bad sync, good affinity
                auto s = twoFloatStream(bitsToFloat(bad), 1.5f);
                EcosystemParams dst;
                dst.syncRate.store(0.21f);
                dst.selfAffinity.store(-0.5f);
                Steinberg::IBStreamer r(s, kLittleEndian);
                REQUIRE(loadEcosystemParamsV3Ext(dst, r));
                REQUIRE(bitsOf(dst.syncRate.load()) == bitsOf(0.21f));
                REQUIRE(bitsOf(dst.selfAffinity.load()) == bitsOf(1.5f));
            }
            {   // good sync, bad affinity
                auto s = twoFloatStream(0.3f, bitsToFloat(bad));
                EcosystemParams dst;
                dst.syncRate.store(0.21f);
                dst.selfAffinity.store(-0.5f);
                Steinberg::IBStreamer r(s, kLittleEndian);
                REQUIRE(loadEcosystemParamsV3Ext(dst, r));
                REQUIRE(bitsOf(dst.syncRate.load()) == bitsOf(0.3f));
                REQUIRE(bitsOf(dst.selfAffinity.load()) == bitsOf(-0.5f));
            }
        }
    }

    SECTION("V3ExtClamp") {
        auto s = twoFloatStream(0.9f, -3.0f);
        EcosystemParams dst;
        Steinberg::IBStreamer r(s, kLittleEndian);
        REQUIRE(loadEcosystemParamsV3Ext(dst, r));
        REQUIRE(bitsOf(dst.syncRate.load()) == bitsOf(0.5f));
        REQUIRE(bitsOf(dst.selfAffinity.load()) == bitsOf(-2.0f));

        auto s2 = twoFloatStream(-0.1f, 3.0f);
        EcosystemParams dst2;
        Steinberg::IBStreamer r2(s2, kLittleEndian);
        REQUIRE(loadEcosystemParamsV3Ext(dst2, r2));
        REQUIRE(bitsOf(dst2.syncRate.load()) == bitsOf(0.0f));
        REQUIRE(bitsOf(dst2.selfAffinity.load()) == bitsOf(2.0f));
    }

    SECTION("V3ExtTruncationEveryOffset") {
        EcosystemParams src;
        src.syncRate.store(0.4f);
        src.selfAffinity.store(1.2f);
        const auto bytes = saveExtBytes(src);
        REQUIRE(static_cast<Steinberg::int64>(bytes.size()) == kBExt);

        for (std::size_t c = 0; c < bytes.size(); ++c) {
            auto s = streamFrom(bytes, c);
            EcosystemParams dst;
            dst.syncRate.store(0.11f);      // pre-dirtied
            dst.selfAffinity.store(-0.7f);  // pre-dirtied
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE_FALSE(loadEcosystemParamsV3Ext(dst, r));
            // Field 1 spans [0, 4): loaded only once fully contained.
            const float expectedSync = (c >= 4) ? 0.4f : 0.11f;
            REQUIRE(bitsOf(dst.syncRate.load()) == bitsOf(expectedSync));
            // Field 2 spans [4, 8), never fully contained in [0, c): unchanged.
            REQUIRE(bitsOf(dst.selfAffinity.load()) == bitsOf(-0.7f));
        }
    }

    SECTION("RosterFormat") {
        String128 str{};
        REQUIRE(formatEcosystemParam(kEcosystemSyncRateId, 0.5, str) == Steinberg::kResultOk);
        REQUIRE(toAsciiString(str) == "0.25");
        REQUIRE(formatEcosystemParam(kEcosystemSelfAffinityId, 0.25, str) == Steinberg::kResultOk);
        REQUIRE(toAsciiString(str) == "-1.00");
        REQUIRE(formatEcosystemParam(kEcosystemSelfAffinityId, 0.75, str) == Steinberg::kResultOk);
        REQUIRE(toAsciiString(str) == "+1.00");
    }

    SECTION("V3ExtControllerMirror") {
        EcosystemParams src;
        src.syncRate.store(0.25f);
        src.selfAffinity.store(1.0f);
        const auto bytes = saveExtBytes(src);
        auto s = streamFrom(bytes, bytes.size());

        std::vector<std::pair<ParamID, double>> calls;
        Steinberg::IBStreamer r(s, kLittleEndian);
        loadEcosystemParamsV3ExtToController(
            r, [&calls](ParamID id, ParamValue v) { calls.emplace_back(id, v); });

        REQUIRE(calls.size() == 2u);
        REQUIRE(calls[0].first == kEcosystemSyncRateId);
        REQUIRE(calls[0].second == Approx(0.5).margin(1e-9));
        REQUIRE(calls[1].first == kEcosystemSelfAffinityId);
        REQUIRE(calls[1].second == Approx(0.75).margin(1e-9));
    }
}
